/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * Fabian Groffen, Martin Kersten
 * Remote querying functionality
 * Communication with other mservers at the MAL level is a delicate task.
 * However, it is indispensable for any distributed functionality.  This
 * module provides an abstract way to store and retrieve objects on a
 * remote site.  Additionally, functions on a remote site can be executed
 * using objects available in the remote session context.  This yields in
 * four primitive functions that form the basis for distribution methods:
 * get, put, register and exec.
 *
 * The get method simply retrieves a copy of a remote object.  Objects can
 * be simple values, strings or BATs.  The same holds for the put method,
 * but the other way around.  A local object can be stored on a remote
 * site.  Upon a successful store, the put method returns the remote
 * identifier for the stored object.  With this identifier the object can
 * be addressed, e.g. using the get method to retrieve the object that was
 * stored using put.
 *
 * The get and put methods are symmetric.  Performing a get on an
 * identifier that was returned by put, results in an object with the same
 * value and type as the one that was put.  The result of such an operation is
 * equivalent to making an (expensive) copy of the original object.
 *
 * The register function takes a local MAL function and makes it known at a
 * remote site. It ensures that it does not overload an already known
 * operation remotely, which could create a semantic conflict.
 * Deregistering a function is forbidden, because it would allow for taking
 * over the remote site completely.
 * C-implemented functions, such as io.print() cannot be remotely stored.
 * It would require even more complicated (byte) code shipping and remote
 * compilation to make it work.
 *
 * The choice to let exec only execute functions was made to avoid problems
 * to decide what should be returned to the caller.  With a function it is
 * clear and simple to return that what the function signature prescribes.
 * Any side effect (e.g. io.print calls) may cause havoc in the system,
 * but are currently ignored.
 *
 * This leads to the final contract of this module.  The methods should be
 * used correctly, by obeying their contract.  Failing to do so will result
 * in errors and possibly undefined behaviour.
 *
 * The resolve() function can be used to query Merovingian.  It returns one
 * or more databases discovered in its vicinity matching the given pattern.
 *
 */
#include "monetdb_config.h"
#include "remote.h"

/*
 * Technically, these methods need to be serialised per connection,
 * hence a scheduler that interleaves e.g. multiple get calls, simply
 * violates this constraint.  If parallelism to the same site is
 * desired, a user could create a second connection.  This is not always
 * easy to generate at the proper place, e.g. overloading the dataflow
 * optimizer to patch connections structures is not acceptable.
 *
 * Instead, we maintain a simple lock with each connection, which can be
 * used to issue a safe, but blocking get/put/exec/register request.
 */

static connection conns = NULL;
static unsigned char localtype = 0;

static inline str RMTquery(MapiHdl *ret, str func, Mapi conn, str query);
static inline str RMTinternalcopyfrom(BAT **ret, char *hdr, stream *in);

/**
 * Returns a BAT with valid redirects for the given pattern.  If
 * merovingian is not running, this function throws an error.
 */
str RMTresolve(int *ret, str *pat) {
#ifdef WIN32
	throw(MAL, "remote.resolve", "merovingian is not available on "
			"your platform, sorry"); /* please upgrade to Linux, etc. */
#else
	BAT *list;
	char *mero_uri;
	char *p;
	unsigned int port;
	char **redirs;
	char **or;

	if (pat == NULL || *pat == NULL || strcmp(*pat, (str)str_nil) == 0)
		throw(ILLARG, "remote.resolve",
				ILLEGAL_ARGUMENT ": pattern is NULL or nil");

	mero_uri = GDKgetenv("merovingian_uri");
	if (mero_uri == NULL)
		throw(MAL, "remote.resolve", "this function needs the mserver "
				"have been started by merovingian");

	list = BATnew(TYPE_oid, TYPE_str, 20);
	if (list == NULL)
		throw(MAL, "remote.resolve", MAL_MALLOC_FAIL);

	/* extract port from mero_uri, let mapi figure out the rest */
	mero_uri+=strlen("mapi:monetdb://");
	if ((p = strchr(mero_uri, ':')) == NULL)
		throw(MAL, "remote.resolve", "illegal merovingian_uri setting: %s",
				GDKgetenv("merovingian_uri"));
	port = (unsigned int)atoi(p + 1);

	or = redirs = mapi_resolve(NULL, port, *pat);

	if (redirs == NULL)
		throw(MAL, "remote.resolve", "unknown failure when resolving pattern");

	while (*redirs != NULL) {
		BUNappend(list, (ptr)*redirs, FALSE);
		free(*redirs);
		redirs++;
	}
	free(or);

	BBPkeepref(*ret = list->batCacheid);
	return(MAL_SUCCEED);
#endif
}


/* for unique connection identifiers */
static size_t connection_id = 0;

/**
 * Returns a connection to the given uri.  It always returns a newly
 * created connection.
 */
str RMTconnectScen(
		str *ret,
		str *ouri,
		str *user,
		str *passwd,
		str *scen)
{
	connection c;
	char conn[BUFSIZ];
	char *s;
	Mapi m;
	MapiHdl hdl;

	/* just make sure the return isn't garbage */
	*ret = 0;

	if (ouri == NULL || *ouri == NULL || strcmp(*ouri, (str)str_nil) == 0)
		throw(ILLARG, "remote.connect", ILLEGAL_ARGUMENT ": database uri "
				"is NULL or nil");
	if (user == NULL || *user == NULL || strcmp(*user, (str)str_nil) == 0)
		throw(ILLARG, "remote.connect", ILLEGAL_ARGUMENT ": username is "
				"NULL or nil");
	if (passwd == NULL || *passwd == NULL || strcmp(*passwd, (str)str_nil) == 0)
		throw(ILLARG, "remote.connect", ILLEGAL_ARGUMENT ": password is "
				"NULL or nil");
	if (scen == NULL || *scen == NULL || strcmp(*scen, (str)str_nil) == 0)
		throw(ILLARG, "remote.connect", ILLEGAL_ARGUMENT ": scenario is "
				"NULL or nil");
	if (strcmp(*scen, "mal") != 0 && strcmp(*scen, "msql") != 0)
		throw(ILLARG, "remote.connect", ILLEGAL_ARGUMENT ": scenation '%s' "
				"is not supported", *scen);

	m = mapi_mapiuri(*ouri, *user, *passwd, *scen);
	if (mapi_error(m))
		throw(MAL, "remote.connect", "unable to connect to '%s': %s",
				*ouri, mapi_error_str(m));

	MT_lock_set(&mal_remoteLock, "remote.connect");

	/* generate an unique connection name, they are only known
	 * within one mserver, id is primary key, the rest is super key */
	s = mapi_get_dbname(m);
	snprintf(conn, BUFSIZ, "%s_%s_" SZFMT, s, *user, connection_id++);
	/* make sure we can construct MAL identifiers using conn */
	for (s = conn; *s != '\0'; s++) {
		if (!isalpha((int)*s) && !isdigit((int)*s)) {
			*s = '_';
		}
	}

	if (mapi_reconnect(m) != MOK) {
		MT_lock_unset(&mal_remoteLock, "remote.connect");
		throw(IO, "remote.connect", "unable to connect to '%s': %s",
				*ouri, mapi_error_str(m));
	}

	/* connection established, add to list */
	c = GDKzalloc(sizeof(struct _connection));
	c->mconn = m;
	c->name = GDKstrdup(conn);
	c->nextid = 0;
	c->next = conns;
	conns = c;

	RMTquery(&hdl, "remote.connect", m, "remote.bintype();");
	if (hdl != NULL && mapi_fetch_row(hdl)) {
		char *val = mapi_fetch_field(hdl, 0);
		c->type = (unsigned char)atoi(val);
		mapi_close_handle(hdl);
	} else {
		c->type = 0;
	}

	MT_lock_init(&c->lock, "remote connection lock");

#ifdef _DEBUG_MAPI_
	mapi_trace(c->mconn, TRUE);
#endif

	MT_lock_unset(&mal_remoteLock, "remote.connect");

	*ret = GDKstrdup(conn);
	return(MAL_SUCCEED);
}

str RMTconnect(
		str *ret,
		str *uri,
		str *user,
		str *passwd)
{
	str scen = "mal";
	return RMTconnectScen(ret, uri, user, passwd, &scen);
}


/**
 * Disconnects a connection.  The connection needs not to exist in the
 * system, it only needs to exist for the client (i.e. it was once
 * created).
 */
str RMTdisconnect(Client cntxt, str *conn) {
	connection c, t;

	if (conn == NULL || *conn == NULL || strcmp(*conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.disconnect", ILLEGAL_ARGUMENT ": connection "
				"is NULL or nil");


	/* The return is obfuscated by the debug cntxt argument */
#ifdef _DEBUG_REMOTE
	mnstr_printf(cntxt->fdout, "#disconnect link %s\n", *conn);
#else
	(void) cntxt;
#endif

	/* we need a lock because the same user can be handled by multiple
	 * threads */
	MT_lock_set(&mal_remoteLock, "remote.disconnect");
	c = conns;
	t = NULL; /* parent */
	/* walk through the list */
	while (c != NULL) {
		if (strcmp(c->name, *conn) == 0) {
			/* ok, delete it... */
			if (t == NULL) {
				conns = c->next;
			} else {
				t->next = c->next;
			}

			MT_lock_set(&c->lock, "remote.disconnect"); /* shared connection */
#ifdef _DEBUG_REMOTE
			mnstr_printf(cntxt->fdout, "#disconnect link %s\n", c->name);
#endif
			mapi_disconnect(c->mconn);
			mapi_destroy(c->mconn);
			MT_lock_unset(&c->lock, "remote.disconnect");
			MT_lock_destroy(&c->lock);
			GDKfree(c->name);
			GDKfree(c);
			MT_lock_unset(&mal_remoteLock, "remote.disconnect");
			return MAL_SUCCEED;
		}
		t = c;
		c = c->next;
	}

	MT_lock_unset(&mal_remoteLock, "remote.disconnect");
	throw(MAL, "remote.disconnect", "no such connection: %s", *conn);
}

/**
 * Helper function to return a connection matching a given string, or an
 * error if it does not exist.  Since this function is internal, it
 * doesn't check the argument conn, as it should have been checked
 * already.
 * NOTE: this function acquires the mal_remoteLock before accessing conns
 */
static inline str
RMTfindconn(connection *ret, str conn) {
	connection c;

	/* just make sure the return isn't garbage */
	*ret = NULL;
	MT_lock_set(&mal_remoteLock, "remote.<findconn>"); /* protect c */
	c = conns;
	while (c != NULL) {
		if (strcmp(c->name, conn) == 0) {
			*ret = c;
			MT_lock_unset(&mal_remoteLock, "remote.<findconn>");
			return(MAL_SUCCEED);
		}
		c = c->next;
	}
	MT_lock_unset(&mal_remoteLock, "remote.<findconn>");
	throw(MAL, "remote.<findconn>", "no such connection: %s", conn);
}

/**
 * Little helper function that returns a GDKmalloced string containing a
 * valid identifier that is supposed to be unique in the connection's
 * remote context.  The generated string depends on the module and
 * function the caller is in. But also the runtime context is important.
 * The format is rmt<id>_<retvar>_<type>.  Every RMTgetId uses a fresh id,
 * to distinguish amongst different (parallel) execution context.
 * Re-use of this remote identifier should be done with care.
 * The encoding of the type allows for ease of type checking later on.
 */
static inline str
RMTgetId(char *buf, MalBlkPtr mb, InstrPtr p, int arg) {
	InstrPtr f;
	char *mod;
	char *var;
	str rt;
	static int idtag=0;

	assert (p->retc);

	var = getArgName(mb, p, arg);
	f = getInstrPtr(mb, 0); /* top level function */
	mod = getModuleId(f);
	if (mod == NULL)
		mod = "user";
	rt = getTypeIdentifier(getArgType(mb,p,arg));

	snprintf(buf, BUFSIZ, "rmt%d_%s_%s", idtag++, var, rt);

	GDKfree(rt);
	return(MAL_SUCCEED);
}

/**
 * Helper function to execute a query over the given connection,
 * returning the result handle.  If communication fails in one way or
 * another, an error is returned.  Since this function is internal, it
 * doesn't check the input arguments func, conn and query, as they
 * should have been checked already.
 * NOTE: this function assumes a lock for conn is set
 */
static inline str
RMTquery(MapiHdl *ret, str func, Mapi conn, str query) {
	MapiHdl mhdl;

	*ret = NULL;
	mhdl = mapi_query(conn, query);
	if (mhdl) {
		if (mapi_result_error(mhdl) != NULL) {
			str err = createException(
					getExceptionType(mapi_result_error(mhdl)),
					func,
					"(mapi:monetdb://%s@%s/%s) %s",
					mapi_get_user(conn),
					mapi_get_host(conn),
					mapi_get_dbname(conn),
					getExceptionMessage(mapi_result_error(mhdl)));
			mapi_close_handle(mhdl);
			return(err);
		}
	} else {
		if (mapi_error(conn) != MOK) {
			throw(IO, func, "an error occurred on connection: %s",
					mapi_error_str(conn));
		} else {
			throw(MAL, func, "remote function invocation didn't return a result");
		}
	}

	*ret = mhdl;
	return(MAL_SUCCEED);
}

str RMTprelude(int *ret) {
	int type = 0;

	(void)ret;
#ifdef WORDS_BIGENDIAN
	type |= RMTT_B_ENDIAN;
#else
	type |= RMTT_L_ENDIAN;
#endif
#if SIZEOF_SIZE_T == SIZEOF_LONG_LONG
	type |= RMTT_64_BITS;
#else
	type |= RMTT_32_BITS;
#endif
#if SIZEOF_SIZE_T == SIZEOF_INT || defined(MONET_OID32)
	type |= RMTT_32_OIDS;
#else
	type |= RMTT_64_OIDS;
#endif
	localtype = (unsigned char)type;

	return(MAL_SUCCEED);
}

str RMTepilogue(int *ret) {
	connection c, t;

	(void)ret;

	MT_lock_set(&mal_remoteLock, "remote.epilogue"); /* nobody allowed here */
	/* free connections list */
	c = conns;
	while (c != NULL) {
		t = c;
		c = c->next;
		MT_lock_set(&t->lock, "remote.epilogue");
		mapi_destroy(t->mconn);
		MT_lock_unset(&t->lock, "remote.epilogue");
		MT_lock_destroy(&t->lock);
		GDKfree(t->name);
		GDKfree(t);
	}
	/* not sure, but better be safe than sorry */
	conns = NULL;
	MT_lock_unset(&mal_remoteLock, "remote.epilogue");

	return(MAL_SUCCEED);
}

/**
 * get fetches the object referenced by ident over connection conn.
 */
str RMTget(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	str conn, ident, tmp, rt;
	connection c;
	char qbuf[BUFSIZ + 1];
	MapiHdl mhdl = NULL;
	int rtype;
	ValPtr v;

	(void)mb;

	conn = *(str*) getArgReference(stk, pci, 1);
	if (conn == NULL || strcmp(conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.get", ILLEGAL_ARGUMENT ": connection name is NULL or nil");
	ident = *(str*) getArgReference(stk, pci, 2);
	if (ident == 0 || isIdentifier(ident) < 0)
		throw(ILLARG, "remote.get", ILLEGAL_ARGUMENT ": identifier expected, got '%s'", ident);

	/* lookup conn, set c if valid */
	rethrow("remote.get", tmp, RMTfindconn(&c, conn));

	rtype = getArgType(mb, pci, 0);
	v = getArgReference(stk, pci, 0);

	if (rtype == TYPE_any || isAnyExpression(rtype)) {
		throw(MAL, "remote.get", ILLEGAL_ARGUMENT ": unsupported any type: %s",
				getTypeName(rtype));
	}
	/* check if the remote type complies with what we expect.
	   Since the put() encodes the type as known to the remote site
	   we can simple compare it here */
	rt = getTypeIdentifier(rtype);
	if (strcmp(ident + strlen(ident) - strlen(rt), rt))
		throw(MAL, "remote.get", ILLEGAL_ARGUMENT
			": remote object type %s does not match expected type %s",
			rt, ident);
	GDKfree(rt);

	if (isaBatType(rtype) && (localtype == 0 || localtype != c->type || (ATOMvarsized(getHeadType(rtype)))))
	{
		int h, t, s;
		ptr l, r;
		str val, var;
		BAT *b;

		snprintf(qbuf, BUFSIZ, "io.print(%s);", ident);
#ifdef _DEBUG_REMOTE
		mnstr_printf(cntxt->fdout, "#remote.get:%s\n", qbuf);
#else
		(void) cntxt;
#endif
		/* this call should be a single transaction over the channel*/
		MT_lock_set(&c->lock, "remote.get");

		if ((tmp = RMTquery(&mhdl, "remote.get", c->mconn, qbuf))
				!= MAL_SUCCEED)
		{
#ifdef _DEBUG_REMOTE
			mnstr_printf(cntxt->fdout, "#REMOTE GET error: %s\n%s\n",
					qbuf, tmp);
#endif
			MT_lock_unset(&c->lock, "remote.get");
			throw(MAL, "remote.get", "%s", tmp);
		}
		h = getHeadType(rtype);
		t = getTailType(rtype);
		b = BATnew(h, t, (BUN)BATTINY);

		while (mapi_fetch_row(mhdl)) {
			val = mapi_fetch_field(mhdl, 0); /* should both be there */
			var = mapi_fetch_field(mhdl, 1);
			if (ATOMvarsized(h)) {
				l = (ptr)(val == NULL ? str_nil : val);
			} else {
				s = 0;
				l = NULL;
				if (val == NULL)
					val = "nil";
				ATOMfromstr(h, &l, &s, val);
			}
			if (ATOMvarsized(t)) {
				r = (ptr)(var == NULL ? str_nil : var);
			} else {
				s = 0;
				r = NULL;
				if (var == NULL)
					var = "nil";
				ATOMfromstr(t, &r, &s, var);
			}

			BUNins(b, l, r, FALSE);

			if (!ATOMvarsized(h)) GDKfree(l);
			if (!ATOMvarsized(t)) GDKfree(r);
		}

		v->val.bval = b->batCacheid;
		v->vtype = TYPE_bat;
		BBPkeepref(b->batCacheid);
	} else if (isaBatType(rtype)) {
		/* binary compatible remote host, transfer BAT in binary form */
		stream *sout;
		stream *sin;
		char buf[256];
		ssize_t sz = 0, rd;
		BAT *b = NULL;

		/* this call should be a single transaction over the channel*/
		MT_lock_set(&c->lock, "remote.get");

		/* bypass Mapi from this point to efficiently write all data to
		 * the server */
		sout = mapi_get_to(c->mconn);
		sin = mapi_get_from(c->mconn);
		if (sin == NULL || sout == NULL) {
			MT_lock_unset(&c->lock, "remote.get");
			throw(MAL, "remote.get", "Connection lost");
		}

		/* call our remote helper to do this more efficiently */
		mnstr_printf(sout, "remote.batbincopy(%s);\n", ident);
		mnstr_flush(sout);

		/* read the JSON header */
		while ((rd = mnstr_read(sin, &buf[sz], 1, 1)) == 1 && buf[sz] != '\n') {
			sz += rd;
		}
		if (rd < 0) {
			MT_lock_unset(&c->lock, "remote.get");
			throw(MAL, "remote.get", "could not read BAT JSON header");
		}
		if (buf[0] == '!') {
			MT_lock_unset(&c->lock, "remote.get");
			return(GDKstrdup(buf));
		}

		buf[sz] = '\0';
		if ((tmp = RMTinternalcopyfrom(&b, buf, sin)) != NULL) {
			MT_lock_unset(&c->lock, "remote.get");
			return(tmp);
		}

		v->val.bval = b->batCacheid;
		v->vtype = TYPE_bat;
		BBPkeepref(b->batCacheid);
	} else {
		ptr p = NULL;
		str val;
		int len = 0;

		snprintf(qbuf, BUFSIZ, "io.print(%s);", ident);
#ifdef _DEBUG_REMOTE
		mnstr_printf(cntxt->fdout, "#remote:%s:%s\n", c->name, qbuf);
#endif
		if ((tmp=RMTquery(&mhdl, "remote.get", c->mconn, qbuf)) != MAL_SUCCEED)
		{
			MT_lock_unset(&c->lock, "remote.get");
			return tmp;
		}
		(void) mapi_fetch_row(mhdl); /* should succeed */
		val = mapi_fetch_field(mhdl, 0);

		if (ATOMvarsized(rtype)) {
			VALset(v, rtype, GDKstrdup(val == NULL ? str_nil : val));
		} else {
			ATOMfromstr(rtype, &p, &len, val == NULL ? "nil" : val);
			if (p != NULL) {
				VALset(v, rtype, p);
				if (ATOMextern(rtype) == 0)
					GDKfree(p);
			} else {
				char tval[BUFSIZ + 1];
				snprintf(tval, BUFSIZ, "%s", val);
				tval[BUFSIZ] = '\0';
				mapi_close_handle(mhdl);
				MT_lock_unset(&c->lock, "remote.get");
				throw(MAL, "remote.get", "unable to parse value: %s", tval);
			}
		}
	}

	if (mhdl != NULL)
		mapi_close_handle(mhdl);
	MT_lock_unset(&c->lock, "remote.get");

	return(MAL_SUCCEED);
}

/**
 * stores the given object on the remote host.  The identifier of the
 * object on the remote host is returned for later use.
 */
str RMTput(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	str conn, tmp;
	char ident[BUFSIZ];
	connection c;
	ValPtr v;
	int type;
	ptr value;
	MapiHdl mhdl = NULL;

	(void)cntxt;

	conn = *(str*) getArgReference(stk, pci, 1);
	if (conn == NULL || strcmp(conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.put", ILLEGAL_ARGUMENT ": connection name is NULL or nil");

	/* lookup conn */
	rethrow("remote.put", tmp, RMTfindconn(&c, conn));

	/* put the thing */
	type = getArgType(mb, pci, 2);
	value = getArgReference(stk, pci, 2);

	/* this call should be a single transaction over the channel*/
	MT_lock_set(&c->lock, "remote.put");

	/* get a free, typed identifier for the remote host */
	RMTgetId(ident, mb, pci, 2);

	/* depending on the input object generate actions to store the
	 * object remotely*/
	if (type == TYPE_any || isAnyExpression(type)) {
		MT_lock_unset(&c->lock, "remote.put");
		throw(MAL, "remote.put", "unsupported type: %s",
				getTypeName(type));
	} else if (isaBatType(type)) {
		BATiter bi;
		/* naive approach using bat.new() and bat.insert() calls */
		char *head, *tail;
		char qbuf[BUFSIZ];
		int bid;
		BAT *b = NULL;
		BUN p, q;
		str headv, tailv;
		stream *sout;

		head = getTypeIdentifier(getHeadType(type));
		tail = getTypeIdentifier(getTailType(type));

		bid = *(int *)value;
		if (bid != 0 && (b = BATdescriptor(bid)) == NULL){
			MT_lock_unset(&c->lock, "remote.put");
			throw(MAL, "remote.put", RUNTIME_OBJECT_MISSING);
		}

		/* bypass Mapi from this point to efficiently write all data to
		 * the server */
		sout = mapi_get_to(c->mconn);

		/* call our remote helper to do this more efficiently */
		mnstr_printf(sout,
				"%s := remote.batload(:%s, :%s, " BUNFMT ");\n",
				ident, head, tail, (bid == 0 ? 0 : BATcount(b)));
		mnstr_flush(sout);

		/* b can be NULL if bid == 0 (only type given, ugh) */
		if (b) {
			bi = bat_iterator(b);
			BATloop(b, p, q) {
				headv = tailv = NULL;
				ATOMformat(getHeadType(type), BUNhead(bi, p), &headv);
				ATOMformat(getTailType(type), BUNtail(bi, p), &tailv);
				if (getTailType(type) <= TYPE_str &&
						getHeadType(type) <= TYPE_str)
				{
					mnstr_printf(sout, "%s,%s\n", headv, tailv);
				} else if (getTailType(type) > TYPE_str &&
						getHeadType(type) > TYPE_str)
				{
					mnstr_printf(sout, "\"%s\",\"%s\"\n", headv, tailv);
				} else if (getTailType(type) > TYPE_str) {
					mnstr_printf(sout, "%s,\"%s\"\n", headv, tailv);
				} else {
					mnstr_printf(sout, "\"%s\",%s\n", headv, tailv);
				}
				GDKfree(headv);
				GDKfree(tailv);
			}
			BBPunfix(b->batCacheid);
		}

		/* write the empty line the server is waiting for, handles
		 * all errors at the same time, if any */
		qbuf[0] = '\0';
		if ((tmp = RMTquery(&mhdl, "remote.put", c->mconn, qbuf))
				!= MAL_SUCCEED)
		{
			MT_lock_unset(&c->lock, "remote.put");
			return tmp;
		}
		mapi_close_handle(mhdl);
	} else {
		str val = NULL;
		char qbuf[BUFSIZ + 1]; /* FIXME: this should be dynamic */
		if (ATOMvarsized(type)) {
			ATOMformat(type, *(str *)value, &val);
		} else {
			ATOMformat(type, value, &val);
		}
		if (type <= TYPE_str)
			snprintf(qbuf, BUFSIZ, "%s := %s:%s;\n", ident, val, getTypeIdentifier(type));
		else
			snprintf(qbuf, BUFSIZ, "%s := \"%s\":%s;\n", ident, val, getTypeIdentifier(type));
		qbuf[BUFSIZ] = '\0';
		GDKfree(val);
#ifdef _DEBUG_REMOTE
		mnstr_printf(cntxt->fdout, "#remote.put:%s:%s\n", c->name, qbuf);
#endif
		if ((tmp = RMTquery(&mhdl, "remote.put", c->mconn, qbuf))
				!= MAL_SUCCEED)
		{
			MT_lock_unset(&c->lock, "remote.put");
			return tmp;
		}
		mapi_close_handle(mhdl);
	}
	MT_lock_unset(&c->lock, "remote.put");

	/* return the identifier */
	v = getArgReference(stk, pci, 0);
	v->vtype = TYPE_str;
	v->val.sval = GDKstrdup(ident);
	return(MAL_SUCCEED);
}

/**
 * stores the given <mod>.<fcn> on the remote host.
 * An error is returned if the function is already known at the remote site.
 * The implementation is based on serialisation of the block into a string
 * followed by remote parsing.
 */
str RMTregisterInternal(Client cntxt, str conn, str mod, str fcn)
{
	str tmp, qry, msg;
	connection c;
	char buf[BUFSIZ];
	MapiHdl mhdl = NULL;
	Symbol sym;

	if (conn == NULL || strcmp(conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.register", ILLEGAL_ARGUMENT ": connection name is NULL or nil");

	/* find local definition */
	sym = findSymbol(cntxt->nspace,
			putName(mod, strlen(mod)),
			putName(fcn, strlen(fcn)));
	if (sym == NULL)
		throw(MAL, "remote.register", ILLEGAL_ARGUMENT ": no such function: %s.%s", mod, fcn);

	/* lookup conn */
	rethrow("remote.register", tmp, RMTfindconn(&c, conn));

	/* this call should be a single transaction over the channel*/
	MT_lock_set(&c->lock, "remote.register");

	/* check remote definition */
	snprintf(buf, BUFSIZ, "inspect.getSignature(\"%s\",\"%s\");", mod, fcn);
#ifdef _DEBUG_REMOTE
	mnstr_printf(cntxt->fdout, "#remote.register:%s:%s\n", c->name, buf);
#endif
	msg = RMTquery(&mhdl, "remote.register", c->mconn, buf);
	if (msg == MAL_SUCCEED) {
		MT_lock_unset(&c->lock, "remote.register");
		throw(MAL, "remote.register",
				"function already exists at the remote site: %s.%s",
				mod, fcn);
	} else {
		/* we basically hope/assume this is a "doesn't exist" error */
		GDKfree(msg);
	}
	if (mhdl)
		mapi_close_handle(mhdl);

	/* make sure the program is error free */
	chkProgram(cntxt->fdout, cntxt->nspace, sym->def);
	if (sym->def->errors) {
		MT_lock_unset(&c->lock, "remote.register");
		throw(MAL, "remote.register",
				"function '%s.%s' contains syntax or type errors",
				mod, fcn);
	}

	qry = function2str(sym->def, LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS);
#ifdef _DEBUG_REMOTE
	mnstr_printf(cntxt->fdout, "#remote.register:%s:%s\n", c->name, qry);
#endif
	msg = RMTquery(&mhdl, "remote.register", c->mconn, qry);
	if (mhdl)
		mapi_close_handle(mhdl);

	MT_lock_unset(&c->lock, "remote.register");
	return msg;
}

str RMTregister(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	str conn = *(str*) getArgReference(stk, pci, 1);
	str mod = *(str*) getArgReference(stk, pci, 2);
	str fcn = *(str*) getArgReference(stk, pci, 3);
	(void)mb;
	return RMTregisterInternal(cntxt, conn, mod, fcn);
}

/**
 * exec executes the function with its given arguments on the remote
 * host, returning the function's return value.  exec is purposely kept
 * very spartan.  All arguments need to be handles to previously put()
 * values.  It calls the function with the given arguments at the remote
 * site, and returns the handle which stores the return value of the
 * remotely executed function.  This return value can be retrieved using
 * a get call. It handles multiple return arguments.
 */
str RMTexec(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	str conn, mod, func, tmp;
	int i, len;
	connection c= NULL;
	char qbuf[BUFSIZ+1];	/* FIXME: make this dynamic */
	MapiHdl mhdl;

	(void)cntxt;
	(void)mb;

	for (i = 0; i < pci->retc; i++) {
		tmp = *(str *)getArgReference(stk, pci, i);
		if (tmp == NULL || strcmp(tmp, (str)str_nil) == 0)
			throw(ILLARG, "remote.exec", ILLEGAL_ARGUMENT
					": return value %d is NULL or nil", i);
	}
	conn = *(str*) getArgReference(stk, pci, i++);
	if (conn == NULL || strcmp(conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.exec", ILLEGAL_ARGUMENT ": connection name is NULL or nil");
	mod = *(str*) getArgReference(stk, pci, i++);
	if (mod == NULL || strcmp(mod, (str)str_nil) == 0)
		throw(ILLARG, "remote.exec", ILLEGAL_ARGUMENT ": module name is NULL or nil");
	func = *(str*) getArgReference(stk, pci, i++);
	if (func == NULL || strcmp(func, (str)str_nil) == 0)
		throw(ILLARG, "remote.exec", ILLEGAL_ARGUMENT ": function name is NULL or nil");

	/* lookup conn */
	rethrow("remote.exec", tmp, RMTfindconn(&c, conn));

	/* this call should be a single transaction over the channel*/
	MT_lock_set(&c->lock, "remote.exec");

	len = 0;

	/* use previous defined remote objects to keep result */
	if (pci->retc > 1)
		qbuf[len++] = '(';
	for (i = 0; i < pci->retc; i++)
		len += snprintf(&qbuf[len], BUFSIZ - len, "%s%s",
				(i > 0 ? ", " : ""), *(str *) getArgReference(stk, pci, i));

	if (pci->retc > 1 && len < BUFSIZ)
		qbuf[len++] = ')';

	/* build the function invocation string in qbuf */
	len += snprintf(&qbuf[len], BUFSIZ - len, " := %s.%s(", mod, func);

	/* handle the arguments to the function */
	assert(pci->argc - pci->retc >= 3); /* conn, mod, func, ... */

	/* put the arguments one by one, and dynamically build the
	 * invocation string */
	for (i = 3; i < pci->argc - pci->retc; i++) {
		len += snprintf(&qbuf[len], BUFSIZ - len, "%s%s",
				(i > 3 ? ", " : ""),
				*((str *)getArgReference(stk, pci, pci->retc + i)));
	}

	/* finish end execute the invocation string */
	len += snprintf(&qbuf[len], BUFSIZ - len, ");");
#ifdef _DEBUG_REMOTE
	mnstr_printf(cntxt->fdout,"#remote.exec:%s:%s\n",c->name,qbuf);
#endif
	tmp = RMTquery(&mhdl, "remote.exec", c->mconn, qbuf);
	if (mhdl)
		mapi_close_handle(mhdl);
	MT_lock_unset(&c->lock, "remote.exec");
	return tmp;
}

/**
 * batload is a helper function to make transferring a BAT with RMTput
 * more efficient.  It works by creating a BAT, and loading it with the
 * data as comma separated values from the input stream, until an empty
 * line is read.  The given size argument is taken as a hint only, and
 * is not enforced to match the number of rows read.
 */
str RMTbatload(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	ValPtr v;
	int h, t;
	int size;
	ptr l, r;
	int s;
	BAT *b;
	size_t len;
	size_t pos;
	char *val, *var;
	bit escaped = 0, instr = 0;
	bstream *fdin = cntxt->fdin;

	v = getArgReference(stk, pci, 0); /* return */
	h = getArgType(mb, pci, 1); /* head type */
	t = getArgType(mb, pci, 2); /* tail type */
	size = *(int *)getArgReference(stk, pci, 3); /* size */

	b = BATnew(h, t, size);

	/* grab the input stream and start reading */
	fdin->eof = 0;
	len = fdin->pos;
	while (len < fdin->len || bstream_next(fdin) > 0) {
		/* newline hunting (how spartan) */
		for (len = fdin->pos; len < fdin->len && fdin->buf[len] != '\n'; len++)
			;
		/* unterminated line, request more */
		if (fdin->buf[len] != '\n')
			continue;
		/* empty line, end of input */
		if (fdin->pos == len)
			break;
		fdin->buf[len] = '\0'; /* kill \n */
		/* we need to slice and dice here, bah */
		var = val = NULL;
		for (pos = fdin->pos; pos < len; pos++) {
			switch (fdin->buf[pos]) {
				case '"':
					if (!escaped)
						instr = !instr;
				break;
				case '\\':
					escaped = !escaped;
				break;
				case ',':
					if (!instr) {
						/* we know it's only two values, so end here */
						val = &fdin->buf[fdin->pos];
						fdin->buf[pos] = '\0';
						var = &fdin->buf[pos + 1];
						pos = len; /* break out of the for-loop */
					}
				break;
			}
		}
		/* skip over this line */
		fdin->pos = ++len;

		if (val == NULL || var == NULL) {
			/* now what? */
			assert(0); /* FIXME */
		}

		s = 0;
		l = NULL;
		ATOMfromstr(h, &l, &s, val);

		s = 0;
		r = NULL;
		ATOMfromstr(t, &r, &s, var);

		BUNins(b, l, r, FALSE);

		GDKfree(l);
		GDKfree(r);
	}

	v->val.bval = b->batCacheid;
	v->vtype = TYPE_bat;
	BBPkeepref(b->batCacheid);

	return(MAL_SUCCEED);
}

/**
 * dump given BAT to stream
 */
str RMTbincopyto(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int bid = *(int *)getArgReference(stk, pci, 1);
	BAT *b = BBPquickdesc(ABS(bid), FALSE);
	char sendhead = 0;
	char sendtheap = 0;

	(void)mb;
	(void)stk;
	(void)pci;

	if (b == NULL)
		throw(MAL, "remote.bincopyto", RUNTIME_OBJECT_UNDEFINED);

	/* mirror when argument is mirrored */
	if (bid < 0)
		b = BATmirror(b);

	if (b->htype != TYPE_void && b->hvarsized)
		throw(ILLARG, "remote.bincopyto", "varsized-headed BATs are not supported");

	BBPincref(bid, FALSE);

	sendhead = !BAThvoid(b);
	sendtheap = b->ttype != TYPE_void && b->tvarsized;

	mnstr_printf(cntxt->fdout, /*JSON*/"{"
			"\"version\":1,"
			"\"htype\":%d,"
			"\"ttype\":%d,"
			"\"hseqbase\":" OIDFMT ","
			"\"tseqbase\":" OIDFMT ","
			"\"hsorted\":%d,"
			"\"hrevsorted\":%d,"
			"\"tsorted\":%d,"
			"\"trevsorted\":%d,"
			"\"hkey\":%d,"
			"\"tkey\":%d,"
			"\"hnonil\":%d,"
			"\"tnonil\":%d,"
			"\"tdense\":%d,"
			"\"size\":" BUNFMT ","
			"\"headsize\":" SZFMT ","
			"\"tailsize\":" SZFMT ","
			"\"theapsize\":" SZFMT
			"}\n",
			sendhead ? b->htype : TYPE_void, b->ttype,
			b->hseqbase, b->tseqbase,
			b->hsorted, b->hrevsorted,
			b->tsorted, b->trevsorted,
			b->hkey, b->tkey,
			b->H->nonil, b->T->nonil,
			b->tdense,
			b->batCount,
			sendhead ? (size_t)b->batCount * Hsize(b) : 0,
			(size_t)b->batCount * Tsize(b),
			sendtheap && b->batCount > 0 ? b->T->vheap->free : 0
			);

	if (b->batCount > 0) {
		if (sendhead)
			mnstr_write(cntxt->fdout, /* head */
					Hloc(b, BUNfirst(b)), b->batCount * Hsize(b), 1);
		mnstr_write(cntxt->fdout, /* tail */
				Tloc(b, BUNfirst(b)), b->batCount * Tsize(b), 1);
		if (sendtheap)
			mnstr_write(cntxt->fdout, /* theap */
					Tbase(b), b->T->vheap->free, 1);
	}
	/* flush is done by the calling environment (MAL) */

	BBPdecref(bid, FALSE);

	return(MAL_SUCCEED);
}

typedef struct _binbat_v1 {
	int Htype;
	int Ttype;
	oid Hseqbase;
	oid Tseqbase;
	bit Hsorted;
	bit Hrevsorted;
	bit Tsorted;
	bit Trevsorted;
	unsigned int
		Hkey:2,
		Tkey:2,
		Hnonil:1,
		Tnonil:1,
		Tdense:1;
	BUN size;
	size_t headsize;
	size_t tailsize;
	size_t theapsize;
} binbat;

static inline str
RMTinternalcopyfrom(BAT **ret, char *hdr, stream *in)
{
	binbat bb = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	char *nme = NULL;
	char *val = NULL;
	char tmp;

	BAT *b;

	/* hdr is a JSON structure that looks like
	 * {"version":1,"htype":0,"ttype":6,"seqbase":0,"tailsize":4,"theapsize":0}
	 * we take the binary data directly from the stream */

	/* could skip whitespace, but we just don't allow that */
	if (*hdr++ != '{')
		throw(MAL, "remote.bincopyfrom", "illegal input, not a JSON header (got '%s')", hdr - 1);
	while (*hdr != '\0') {
		switch (*hdr) {
			case '"':
				/* we assume only numeric values, so all strings are
				 * elems */
				if (nme != NULL) {
					*hdr = '\0';
				} else {
					nme = hdr + 1;
				}
				break;
			case ':':
				val = hdr + 1;
				break;
			case ',':
			case '}':
				if (val == NULL)
					throw(MAL, "remote.bincopyfrom",
							"illegal input, JSON value missing");
				*hdr = '\0';

				/* deal with nme and val */
				if (strcmp(nme, "version") == 0) {
					if (strcmp(val, "1") != 0)
						throw(MAL, "remote.bincopyfrom",
								"unsupported version: %s", val);
				} else if (strcmp(nme, "htype") == 0) {
					bb.Htype = atoi(val);
				} else if (strcmp(nme, "ttype") == 0) {
					bb.Ttype = atoi(val);
				} else if (strcmp(nme, "hseqbase") == 0) {
					bb.Hseqbase = (oid)atol(val);
				} else if (strcmp(nme, "tseqbase") == 0) {
					bb.Tseqbase = (oid)atol(val);
				} else if (strcmp(nme, "hsorted") == 0) {
					bb.Hsorted = *val != '0';
				} else if (strcmp(nme, "hrevsorted") == 0) {
					bb.Hrevsorted = *val != '0';
				} else if (strcmp(nme, "tsorted") == 0) {
					bb.Tsorted = *val != '0';
				} else if (strcmp(nme, "trevsorted") == 0) {
					bb.Trevsorted = *val != '0';
				} else if (strcmp(nme, "hkey") == 0) {
					bb.Hkey = *val != '0';
				} else if (strcmp(nme, "tkey") == 0) {
					bb.Tkey = *val != '0';
				} else if (strcmp(nme, "hnonil") == 0) {
					bb.Hnonil = *val != '0';
				} else if (strcmp(nme, "tnonil") == 0) {
					bb.Tnonil = *val != '0';
				} else if (strcmp(nme, "tdense") == 0) {
					bb.Tdense = *val != '0';
				} else if (strcmp(nme, "size") == 0) {
					bb.size = (BUN)atol(val);
				} else if (strcmp(nme, "headsize") == 0) {
					bb.headsize = atol(val);
				} else if (strcmp(nme, "tailsize") == 0) {
					bb.tailsize = atol(val);
				} else if (strcmp(nme, "theapsize") == 0) {
					bb.theapsize = atol(val);
				} else {
					throw(MAL, "remote.bincopyfrom",
							"unknown element: %s", nme);
				}
				nme = val = NULL;
				break;
		}
		hdr++;
	}

	/* the BAT we will return */
	b = BATnew(bb.Htype, bb.Ttype, bb.size);

	/* for strings, the width may not match, fix it to match what we
	 * retrieved */
	if (bb.Ttype == TYPE_str && bb.size) {
		b->T->width = (unsigned short) (bb.tailsize / bb.size);
		b->T->shift = ATOMelmshift(Tsize(b));
	}

	if (bb.headsize > 0) {
		HEAPextend(&b->H->heap, bb.headsize); /* cheap if already done */
		mnstr_read(in, b->H->heap.base, bb.headsize, 1);
		b->H->heap.dirty = TRUE;
	}
	if (bb.tailsize > 0) {
		HEAPextend(&b->T->heap, bb.tailsize);
		mnstr_read(in, b->T->heap.base, bb.tailsize, 1);
		b->T->heap.dirty = TRUE;
	}
	if (bb.theapsize > 0) {
		HEAPextend(b->T->vheap, bb.theapsize);
		mnstr_read(in, b->T->vheap->base, bb.theapsize, 1);
		b->T->vheap->free = bb.theapsize;
		b->T->vheap->dirty = TRUE;
	}

	/* set properties */
	b->hseqbase = bb.Hseqbase;
	b->tseqbase = bb.Tseqbase;
	b->hsorted = bb.Hsorted;
	b->hrevsorted = bb.Hrevsorted;
	b->tsorted = bb.Tsorted;
	b->trevsorted = bb.Trevsorted;
	b->hkey = bb.Hkey;
	b->tkey = bb.Tkey;
	b->H->nonil = bb.Hnonil;
	b->T->nonil = bb.Tnonil;
	if (bb.Htype == TYPE_void)
		b->hdense = b->hkey = TRUE;
	b->tdense = bb.Tdense;
	if (bb.Ttype == TYPE_str && bb.size)
		BATsetcapacity(b, (BUN) (bb.tailsize >> b->T->shift));
	BATsetcount(b, bb.size);
	b->batDirty = TRUE;

	/* read blockmode flush */
	while (mnstr_read(in, &tmp, 1, 1) > 0) {
		mnstr_printf(GDKout, "!MALexception:remote.bincopyfrom: expected flush, got: %c\n", tmp);
	}

	BATderiveHeadProps(b, 1);

	*ret = b;
	return(MAL_SUCCEED);
}

/**
 * read from the input stream and give the BAT handle back to the caller
 */
str RMTbincopyfrom(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	BAT *b = NULL;
	ValPtr v;
	str err;

	(void)mb;

	/* We receive a normal line, which contains the JSON header, the
	 * rest is binary data directly on the stream.  We get the first
	 * line from the buffered stream we have here, and pass it on
	 * together with the raw stream we have. */
	cntxt->fdin->eof = 0; /* in case it was before */
	if (bstream_next(cntxt->fdin) <= 0)
		throw(MAL, "remote.bincopyfrom", "expected JSON header");

	cntxt->fdin->buf[cntxt->fdin->len] = '\0';
	err = RMTinternalcopyfrom(&b,
			&cntxt->fdin->buf[cntxt->fdin->pos], cntxt->fdin->s);
	/* skip the JSON line */
	cntxt->fdin->pos = ++cntxt->fdin->len;
	if (err != MAL_SUCCEED)
		return(err);

	v = getArgReference(stk, pci, 0);
	v->val.bval = b->batCacheid;
	v->vtype = TYPE_bat;
	BBPkeepref(b->batCacheid);

	return(MAL_SUCCEED);
}

/**
 * bintype identifies the system on its binary profile.  This is mainly
 * used to determine if BATs can be sent binary across.
 */
str RMTbintype(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci) {
	int type = 0;
	(void)mb;
	(void)stk;
	(void)pci;

#ifdef WORDS_BIGENDIAN
	type |= RMTT_B_ENDIAN;
#else
	type |= RMTT_L_ENDIAN;
#endif
#if SIZEOF_SIZE_T == SIZEOF_LONG_LONG
	type |= RMTT_64_BITS;
#else
	type |= RMTT_32_BITS;
#endif
#if SIZEOF_SIZE_T == SIZEOF_INT || defined(MONET_OID32)
	type |= RMTT_32_OIDS;
#else
	type |= RMTT_64_OIDS;
#endif

	mnstr_printf(cntxt->fdout, "[ %d ]\n", type);

	return(MAL_SUCCEED);
}

/**
 * Returns whether the underlying connection is still connected or not.
 * Best effort implementation on top of mapi using a ping.
 */
str
RMTisalive(int *ret, str *conn)
{
	str tmp;
	connection c;

	if (*conn == NULL || strcmp(*conn, (str)str_nil) == 0)
		throw(ILLARG, "remote.get", ILLEGAL_ARGUMENT ": connection name is NULL or nil");

	/* lookup conn, set c if valid */
	rethrow("remote.get", tmp, RMTfindconn(&c, *conn));

	*ret = 0;
	if (mapi_is_connected(c->mconn) != 0 && mapi_ping(c->mconn) == MOK)
		*ret = 1;

	return MAL_SUCCEED;
}
