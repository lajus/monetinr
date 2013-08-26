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
@a F. Groffen, M. Kersten
@- Map-Reduce
The Map-Reduce infrastructure requires a little optimizer to turn
an arbitrary query into a plan to be executed on the systems in the cloud.
Each cloud consists of a series of named servers, managed by Merovingian
with the pattern "cloudname/node//".  The cloudname is detected from
the schema in which an SQL table is stored.  Only schemas starting with
"mr_" are considered to be mapreduce schemas on the query node.  The
cloudname is the schema name without the leading "mr_" prefix.
#
Determining the clould is an expensive operation and for the time being
performed each time when a query is compiled.
#
In the first implementation we don't optimize the plan against the mapping scheme.
We simply assume that the complete query can be executed and that only the
result sets should be assembled.
*/
#include "monetdb_config.h"
#include "opt_mapreduce.h"
#include "mal_interpreter.h"
#include "remote.h"

typedef struct _mapnode {
	str uri;
	str user;
	str pass;
} mapnode;

static mapnode *mapnodes;

static void
MRcleanCloud(void)
{
	int i;

	MT_lock_set(&mal_contextLock, "mapreduce");
	for (i = 0; mapnodes[i].uri; i++) {
		if (mapnodes[i].uri != NULL)
			GDKfree(mapnodes[i].uri);
		if (mapnodes[i].user != NULL)
			GDKfree(mapnodes[i].user);
		if (mapnodes[i].pass != NULL)
			GDKfree(mapnodes[i].pass);
		mapnodes[i].uri = mapnodes[i].user = mapnodes[i].pass = 0;
	}
	MT_lock_unset(&mal_contextLock, "mapreduce");
}

str
MRgetCloud(int *ret, str *mrcluster)
{
	str msg;
	BAT *cloud;
	BUN p, q;
	BATiter bi;
	char nodes[BUFSIZ];
	char *n = nodes;
	int mapcount = 0;

	snprintf(nodes, sizeof(nodes), "*/%s/node/*", *mrcluster);
	
	if ((msg = RMTresolve(ret, &n)) != MAL_SUCCEED)
		return msg;

	MT_lock_set(&mal_contextLock, "mapreduce");
	cloud = BATdescriptor(*ret); /* should succeed */

	mapnodes = (mapnode*)GDKzalloc(sizeof(mapnode) * (BATcount(cloud) + 1));
	if (mapnodes == NULL) {
		BBPreleaseref(*ret);
		throw(MAL, "mapreduce.getCloud", MAL_MALLOC_FAIL);
	}

	bi = bat_iterator(cloud);
	BATloop(cloud, p, q) {
		str t = (str)BUNtail(bi, p);
		mapnodes[mapcount].uri = GDKstrdup(t);
		mapnodes[mapcount].user = GDKstrdup("monetdb");
		mapnodes[mapcount].pass = GDKstrdup("monetdb");
		mapcount++;
	}

	BBPkeepref(*ret); /* we're done, keep for caller */
	cloud = NULL;
	MT_lock_unset(&mal_contextLock, "mapreduce");

	return MAL_SUCCEED;
}

static int
MRcloudSize(str mrcluster)
{
	str msg;
	int bid;
	BAT *cloud;
	int cnt;

	msg = MRgetCloud(&bid, &mrcluster);
	if (msg) {
		GDKfree(msg); /* bad programming */
		return 0;
	}
	cloud = BATdescriptor(bid);
	cnt = (int)BATcount(cloud);
	BBPreleaseref(bid); /* we're done with it */
	return(cnt);
}


enum poper { pBAT = 1, SUM, MAX, MIN, SORT, SORTDESC, LIMIT };

typedef struct _mapcol {
	int mapid;        /* var in map plan that is in its signature
	                     and return */
	int reduceid;     /* original column var in reduce program we
	                     eventually need to replace */
	int type;         /* type of the map plan var */
	int mapbat;       /* the var that is a BAT containing all values
	                     returned from map nodes (function), can only be
	                     used *after* MRdistributework */
	enum poper postop;/* the operation that needs to be performed on
	                     mapbat to turn it into reduceid */
	struct _mapcol *next;
} mapcol;

static void
MRdistributework(
		Client cntxt,
		MalBlkPtr reduce,
		mapcol *col,
		InstrPtr sig,
		str mrcluster)
{
	InstrPtr o, p = NULL, *packs;
	int i, n, j, q, v, retc;
	int *gets, *w;
	mapcol *lcol;
	(void)cntxt;

	n = MRcloudSize(mrcluster);

	assert(n);
	assert(col);

	retc = 0;
	for (lcol = col; lcol != NULL; lcol = lcol->next)
		retc++;

	assert(retc);

	packs = (InstrPtr *)GDKmalloc(retc * sizeof(InstrPtr));
	gets = (int *)GDKmalloc(n * retc * sizeof(int));
	w = (int *)GDKmalloc(retc * sizeof(int));

	for (lcol = col, j = 0; lcol != NULL; lcol = lcol->next, j++) {
		/* define and create the container bat for all results from the
		 * map nodes */
		packs[j] = p = newFcnCall(reduce, batRef, newRef);
		if (isaBatType(lcol->type)) {
			p = pushType(reduce, p, getHeadType(lcol->type));
			p = pushType(reduce, p, getTailType(lcol->type));
			setArgType(reduce, p, 0, lcol->type);
		} else {
			p = pushNil(reduce, p, TYPE_void);
			p = pushType(reduce, p, lcol->type);
			setArgType(reduce, p, 0, newBatType(TYPE_void, lcol->type));
		}
		lcol->mapbat = getArg(p, 0);

		/* we need to declare the variables that we will use with put,
		 * exec and get */
		for (i = 0; i < n; i++) {
			if (isaBatType(lcol->type)) {
				p = newFcnCall(reduce, batRef, newRef);
				p = pushType(reduce, p, getHeadType(lcol->type));
				p = pushType(reduce, p, getTailType(lcol->type));
			} else {
				p = newAssignment(reduce);
				p = pushNil(reduce, p, lcol->type);
			}
			setArgType(reduce, p, 0, lcol->type);
			gets[(i * retc) + j] = getArg(p, 0);
		}
	}

	for (i = 0; i < n; i++) {
		/* q := remote.connect("uri", "user", "pass"); */
		p = newStmt(reduce, remoteRef, connectRef);
		p = pushStr(reduce, p, mapnodes[i].uri);
		p = pushStr(reduce, p, mapnodes[i].user);
		p = pushStr(reduce, p, mapnodes[i].pass);
		p = pushStr(reduce, p, "msql");
		q = getArg(p, 0);

		/* remote.register(q, "mod", "fcn"); */
		p = newStmt(reduce, remoteRef, putName("register", 8));
		p = pushArgument(reduce, p, q);
		p = pushStr(reduce, p, getModuleId(sig));
		p = pushStr(reduce, p, getFunctionId(sig));

		/* (x1, x2, ..., xn) := remote.exec(q, "mod", "fcn"); */
		p = newInstruction(reduce, ASSIGNsymbol);
		setModuleId(p, remoteRef);
		setFunctionId(p, execRef);
		p = pushArgument(reduce, p, q);
		p = pushStr(reduce, p, getModuleId(sig));
		p = pushStr(reduce, p, getFunctionId(sig));
		for (j = 0; j < retc; j++) {
			/* x1 := remote.put(q, :type) */
			o = newFcnCall(reduce, remoteRef, putRef);
			o = pushArgument(reduce, o, q);
			o = pushArgument(reduce, o, gets[(i * retc) + j]);
			v = getArg(o, 0);
			p = pushReturn(reduce, p, v);
			w[j] = v;
		}
		for (j = sig->retc; j < sig->argc; j++) {
			/* x1 := remote.put(q, A0); */
			o = newStmt(reduce, remoteRef, putRef);
			o = pushArgument(reduce, o, q);
			o = pushArgument(reduce, o, getArg(sig, j));
			p = pushArgument(reduce, p, getArg(o, 0));
		}
		pushInstruction(reduce, p);

		/* y1 := remote.get(q, x1); */
		for (j = 0; j < retc; j++) {
			p = newFcnCall(reduce, remoteRef, getRef);
			p = pushArgument(reduce, p, q);
			p = pushArgument(reduce, p, w[j]);
			getArg(p, 0) = gets[(i * retc) + j];
		}

		/* remote.disconnect(q); */
		p = newStmt(reduce, remoteRef, disconnectRef);
		p = pushArgument(reduce, p, q);
	}

	/* delayed bat.inserts for easily creating a deterministic flow */
	for (lcol = col, j = 0; lcol != NULL; lcol = lcol->next, j++) {
		q = lcol->mapbat;
		/* p := bat.insert(b, y1) */
		for (i = 0; i < n; i++) {
			p = newStmt(reduce, batRef, insertRef);
			p = pushArgument(reduce, p, q);
			if (!isaBatType(lcol->type))
				p = pushNil(reduce, p, TYPE_void);
			p = pushArgument(reduce, p, gets[(i * retc) + j]);
			q = getArg(p, 0);
		}

		if (isaBatType(lcol->type)) {
			/* markH all result bats such that further operations don't get
			 * confused by possible duplicate ids */
			p = newFcnCall(reduce, algebraRef, markHRef);
			p = pushArgument(reduce, p, q);
		}
		lcol->mapbat = getArg(p, 0);

		/* We must deliver here the variables (reduceid) that the rest
		 * of the reduce plan uses in such a way that they can deal with
		 * it.  Since this code is ran at latest (after the possible
		 * optimisations are known) the optimisation code cannot know
		 * what vars come out of here (in particular mapbat), so it must
		 * be able to rely on what it knows (reduceid). */
		switch (lcol->postop) {
			case pBAT:
				getArg(p, 0) = lcol->reduceid;
			break;
			case SUM:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, aggrRef, sumRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
			case MAX:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, aggrRef, maxRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
			case MIN:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, aggrRef, minRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
			case SORT:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, algebraRef, sortTailRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
			case SORTDESC:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, algebraRef, sortReverseTailRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
			case LIMIT:
				lcol->type = newBatType(TYPE_void, lcol->type);
				p = newFcnCall(reduce, algebraRef, sliceRef);
				p = pushArgument(reduce, p, lcol->mapbat);
				getArg(p, 0) = lcol->reduceid;
			break;
		}
	}

#if defined(_DEBUG_OPT_MAPREDUCE) && _DEBUG_OPT_MAPREDUCE == 0
	chkProgram(cntxtx->fdout, cntxtx->fdout, cntxt->nspace, reduce);
	printFunction(cntxt->fdout, reduce, 0, LIST_MAL_STMT | LIST_MAPI);
#endif

	GDKfree(packs);
	GDKfree(gets);
	GDKfree(w);

	MRcleanCloud();
}

enum copymode { cNONE, FREE, STICK, SINGLE, SINGLE_DUP, DUP, LEAVE };

struct stack {
	int *stack;   /* array of ints */
	size_t len;   /* max capacity of alloced array stack */
	size_t cur;   /* current tail pointer */
	size_t pos;   /* current head pointer */
};

static inline void
trackstack_push(struct stack *stk, int val)
{
	if (stk->stack == NULL) {
		stk->len = 10;
		stk->cur = 0;
		stk->pos = 0;
		stk->stack = GDKmalloc(sizeof(int) * stk->len);
	} else if (stk->cur == stk->len) {
		stk->len *= 2;
		stk->stack = GDKrealloc(stk->stack, sizeof(int) * stk->len);
	}

	assert(stk->stack);

	stk->stack[stk->cur++] = val;
}

static inline char
trackstack_isempty(struct stack *stk)
{
	return(stk->stack == NULL || stk->cur == stk->pos);
}

static inline char
trackstack_contains(struct stack *stk, int val)
{
	size_t tsci;

	if (trackstack_isempty(stk))
		return(0);

	for (tsci = stk->cur; tsci > stk->pos; tsci--) {
		if (stk->stack[tsci - 1] == val)
			return(1);
	}
	return(0);
}

static inline int
trackstack_head(struct stack *stk)
{
	if (trackstack_isempty(stk))
		return(0);

	return(stk->stack[stk->pos++]);
}

static inline void
trackstack_clear(struct stack *stk)
{
	if (!trackstack_isempty(stk)) {
		stk->cur = 0;
		stk->pos = 0;
	}
}

static inline void
trackstack_destroy(struct stack *stk)
{
	if (!trackstack_isempty(stk)) {
		GDKfree(stk->stack);
		stk->stack = NULL;
	}
}

/* Mx macro just for readability issues such that the indenting level
 * isn't already halfway the screen. */

static void
MRaddDepInstrs(
		InstrPtr *omb,    /* original stmts */
		int limit,        /* the number of orig stmts */
		MalBlkPtr mb,     /* the new malblock to append to */
		struct stack *s,  /* trackstack with already inserted instrs */
		InstrPtr p        /* current instruction to evaluate */
) {
	int i, j, k, c;
	InstrPtr q = NULL;

	/* did we include all dependencies? */
	for (j = p->retc; j < p->argc; j++) {
		c = getArg(p, j);
		if (!isVarConstant(mb, c) && !trackstack_contains(s, c)) {
			/* we need to inject the missing instruction first */
			q = NULL;
			for (k = 0; k < limit; k++) {
				if (omb[k] == NULL)
					continue;
				for (i = 0; i < omb[k]->retc; i++) {
					if (getArg(omb[k], i) == c) {
						q = omb[k];
						break;
					}
				}
			}
			/* not found. this could happen with argument variables (A0) */
			if (q == NULL)
				break;
			/* make sure we don't add it multiple times */
			for (i = 0; i < q->retc; i++)
				trackstack_push(s, getArg(q, i));
			/* recursively get instructions that are needed for this one */
			MRaddDepInstrs(omb, limit, mb, s, q);
			/* add the instruction */
			pushInstruction(mb, q);
		}
	}
	/* don't forget to add this instruction itself */
	for (i = 0; i < p->retc; i++)
		trackstack_push(s, getArg(p, i));
}

int
OPTmapreduceImplementation(
		Client cntxt,
		MalBlkPtr reduce,
		MalStkPtr stk,
		InstrPtr pc)
{
	int i, j, limit;
	int lastUnion = -1;
	enum copymode copy;
	InstrPtr p, *omap, *oreduce, ret, sig;
	MalBlkPtr map;
	char nme[IDLENGTH];
	char mrcluster[BUFSIZ];
	Symbol new;
	mapcol *col, *lastcol;
	struct stack tracker = { NULL, 0, 0, 0 };
	struct stack instrp = { NULL, 0, 0, 0 };
	struct stack avgtrack = { NULL, 0, 0, 0 };
	char hadBinds = 0;
	str mapreduceRef = putName("mapreduce", 9);/* move global when we're done */

	(void)stk; /* useless, is NULL */
	(void)pc;

#if defined(_DEBUG_OPT_MAPREDUCE) && _DEBUG_OPT_MAPREDUCE == 0
	printFunction(cntxt->fdout, reduce, 0, LIST_MAL_ALL);
#endif

	/* For now we assume that the default ritual of the SQL compiler is
	 * as follows:
	 *   sql.bind(xx, 0)
	 *   sql.bind(xx, 2)
	 *   kdiff
	 *   kunion
	 *   sql.bind(xx, 1)
	 *   kunion
	 *   sql.bind_dbat(xx, 1)
	 *   reverse
	 *   final := kdifference()
	 * This means we can spot the bind_dbat, and predict the
	 * reverse/kdifference to know the bare minimum map-reducable input.
	 * Any plan should work from that point on.  Anything further pushed
	 * into the map plan is an optimisation. */

	col = lastcol = NULL;

	map = copyMalBlk(reduce); /* for the map-program */
	omap = map->stmt;
	oreduce = reduce->stmt;
	limit = map->stop;

	snprintf(nme, IDLENGTH, "%smap", getFunctionId(getInstrPtr(reduce, 0)));

	/* zap */
	if (newMalBlkStmt(map, map->ssize) < 0) {
		return 0;
	}
	if (newMalBlkStmt(reduce, reduce->ssize) < 0) {
		freeMalBlk(map);
		return 0;
	}

	new = newFunction(userRef, putName(nme, strlen(nme)), FUNCTIONsymbol);
	sig = copyInstruction(getInstrPtr(new->def, 0));
	freeMalBlk(new->def);
	new->def = map;
	map->keephistory = reduce->keephistory;
	pushInstruction(map, sig);

	/* SQL uses canned queries, such as a WHERE a > X clause, where the
	 * X is factored out in a variable and used as argument to the
	 * original function.  We will simply copy them and pass them on to
	 * the map function. */
	p = oreduce[0];
	for (j = p->retc; j < p->argc; j++)
		sig = pushArgument(map, sig, getArg(p, j));
	map->stmt[0] = sig; /* many args realloc sig, so reset it */

	/* We do a two-phase scan over the original plan to get a MAP and
	 * REDUCE program.  We cannot do it in a single scan, because
	 * sql.bind patterns (for columns) are possibly scattered over the
	 * full plan.  We need them all first to determine the signature and
	 * return correctly. */
	copy = FREE; /* free original copied signature */
	mrcluster[0] = '\0';
	for (i = 0; i < limit; i++) { /* phase 1 */
		p = omap[i];

		if (getModuleId(p) == sqlRef) {
			if (getFunctionId(p) == mvcRef) {
				/* sql.mvc(): we need this statement everywhere */
				copy = SINGLE_DUP;
			} else if (getFunctionId(p) == bindRef) {
				if (*(const int *)VALptr(&getVar(map, getArg(p, 5))->value) == 0) {
					str schema = VALget(&getVar(map, getArg(p, 2))->value);
					/* check if this is a column from a mapreduce schema (mr_*) */
					if (strncmp(schema, "mr_", 3) != 0)
						break;

					/* and that we don't mix 'n' match mapreduce schemas */
					if (mrcluster[0] == '\0') {
						snprintf(mrcluster, sizeof(mrcluster),
								"%s", schema + 3);
					} else if (strcmp(mrcluster, schema + 3) != 0) {
						break;
					}

					hadBinds = 1;
				}

				/* start of sql.bind, kdiff, kunion, etc. sequence */
				trackstack_push(&tracker, getArg(p, 0));
				copy = SINGLE;
			} else if (getFunctionId(p) == binddbatRef) {
				trackstack_push(&tracker, getArg(p, 0));
				copy = SINGLE;
			} else {
				copy = LEAVE;
			}
		}

		/* move over statements that depend (indirectly) on the sql.bind
		 * calls */
		for (j = p->retc; j < p->argc; j++) {
			if (trackstack_contains(&tracker, getArg(p, j))) {
				if (getModuleId(p) == algebraRef) {
					if (getFunctionId(p) == kunionRef) {
						/* store last seen kunion instruction for
						 * comparison with kdifference later */
						lastUnion = getArg(p, 0);
					} else if (getFunctionId(p) == kdifferenceRef ||
							getFunctionId(p) == leftjoinRef)
					{
						int k;
						/* a kdifference after a kunion results in the
						 * final column result */
						if (getArg(p, 1) == lastUnion ||
								getArg(p, 2) == lastUnion)
						{
							/* this is a column reference, keep it */
							MRaddDepInstrs(omap, limit, map, &instrp, p);
							if (lastcol == NULL) {
								col = lastcol = GDKmalloc(sizeof(mapcol));
								/* this is the first one, leave a marker */
								oreduce[i]->token = REMsymbol;
								setModuleId(oreduce[i], mapreduceRef);
								pushInstruction(map, p);
								copy = LEAVE;
							} else {
								lastcol = lastcol->next =
									GDKmalloc(sizeof(mapcol));
								/* push manually to get consistent
								 * comment marking (after kdifference) */
								pushInstruction(map, p);
								oreduce[i]->token = NOOPsymbol;
								copy = LEAVE;
							}
							lastcol->mapid = getArg(p, 0);
							lastcol->reduceid = getArg(oreduce[i], 0);
							lastcol->type = getArgType(map, p, 0);
							lastcol->mapbat = -1;
							lastcol->postop = pBAT;
							lastcol->next = NULL;
							newComment(map, "= sql column bat");

							/* To push an AVG operation down to the map nodes, we need two
							 * columns instead of one to be returned.  We need to know if this
							 * is the case now, hence we have to perform a forward search for a
							 * calc./ operating on a sum and count of the result column we just
							 * found. */
							/* _18 := batcalc.dbl(_17);
							 * _19 := algebra.selectNotNil(_18);
							 * _20:dbl  := aggr.sum(_19);
							 * _22 := batcalc.dbl(_17);
							 * _23 := algebra.selectNotNil(_22);
							 * _24 := aggr.count(_23);
							 * _27 := calc.==(_24,0:wrd);
							 * _30 := calc.dbl(_24);
							 * _31 := calc.ifthenelse(_27,nil,_30);
							 * _32 := calc./(_20,_31); */
							trackstack_push(&avgtrack, getArg(p, 0));
							k = i;
							i++;
							for (; i < limit; i++) {
								p = omap[i];
								/* can stop when we see other sql.*
								 * stuff */
								if (getModuleId(p) == sqlRef)
									break;
								for (j = p->retc; j < p->argc; j++) {
									if (trackstack_contains(&avgtrack, getArg(p, j))) {
										if (getModuleId(p) == calcRef && getFunctionId(p) == divRef) {
											/* this is pretty dirty, we basically assume that if
											 * we find calc./ and both arguments are in the
											 * stack, then it's probably an AVG originating from
											 * the column (original sole input) ... I can't find
											 * counter cases */
											if (trackstack_contains(&avgtrack, getArg(p, j + 1))) {
												/* go from a single to two columns */
												lastcol->mapid = getArg(p, 1);
												lastcol->reduceid = getArg(oreduce[i], 1);
												lastcol->type = getArgType(map, p, 1);
												lastcol->postop = SUM;
												lastcol = lastcol->next = GDKmalloc(sizeof(mapcol));
												lastcol->mapid = getArg(p, 2);
												lastcol->reduceid = getArg(oreduce[i], 2);
												lastcol->type = getArgType(map, p, 2);
												lastcol->postop = SUM;
												lastcol->next = NULL;
												/* got it, time to copy instructions */
												j = i;
												for (i = k + 1; i < j; i++) {
													p = omap[i];
													if (trackstack_contains(&avgtrack, getArg(p, 0))) {
														MRaddDepInstrs(omap, limit, map, &instrp, p);
														pushInstruction(map, p);
														oreduce[i]->token = NOOPsymbol;
													}
												}
												newComment(map, "= AVG columns");

												i = limit;
												break;
											}
										}
										trackstack_push(&avgtrack, getArg(p, 0));
									}
								}
							}
							trackstack_clear(&avgtrack);
							i = k;
							p = omap[i];

							/* break to avoid tracking the return */
							break;
						}
					}
				}

				/* track all returns */
				for (j = 0; j < p->retc; j++)
					trackstack_push(&tracker, getArg(p, j));

				copy = SINGLE;
				break;
			}
		}

		/* terminate ASAP here, we finish in 2nd phase */
		if (p->token == ENDsymbol)
			break;

		switch (copy) {
			case FREE:
				freeInstruction(p);
				omap[i] = NULL;
			break;
			case LEAVE:
				/* make GCC happy */
			break;
			case SINGLE_DUP:
				copy = FREE;
				MRaddDepInstrs(omap, limit, map, &instrp, p);
				pushInstruction(map, p);
			break;
			case SINGLE:
				copy = LEAVE;
				MRaddDepInstrs(omap, limit, map, &instrp, p);
				pushInstruction(map, p);
				oreduce[i]->token = NOOPsymbol;
			break;
			case STICK:
			case DUP:
			case cNONE:
				assert(0); /* make GCC happy */
			break;
		}
	}
	trackstack_clear(&avgtrack);
	trackstack_destroy(&instrp);

	if (hadBinds == 0) {
		GDKfree(reduce->stmt);
		reduce->stmt = oreduce;
		reduce->stop = limit;
		GDKfree(omap);
		freeSymbol(new);
		trackstack_destroy(&tracker);
		return 0;
	}

	trackstack_clear(&tracker);
	copy = STICK;
	for (i = 0; i < limit; i++) { /* phase 2 */
		p = oreduce[i];

		/* placeholder for MRdistributework */
		if (p->token == REMsymbol && getModuleId(p) == mapreduceRef) {
			trackstack_push(&tracker, -1);
			continue;
		}

		/* skip all NOOPs */
		if (p->token == NOOPsymbol) {
			trackstack_push(&avgtrack, i);
			continue;
		}

		if (getModuleId(p) == algebraRef) {
			if (getFunctionId(p) == sortTailRef) {
				/* simple ORDER BY */
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {	
					if (getArg(p, 1) == lastcol->reduceid) {
						pushInstruction(map, omap[i]);
						newComment(map, "= ORDER BY");
						/* fix return */
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = SORT;
						copy = cNONE;
						break;
					}
				}
			} else if (getFunctionId(p) == sortReverseTailRef) {
				/* simple ORDER BY DESC */
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {	
					if (getArg(p, 1) == lastcol->reduceid) {
						pushInstruction(map, omap[i]);
						newComment(map, "= ORDER BY DESC");
						/* fix return */
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = SORTDESC;
						copy = cNONE;
						break;
					}
				}
			} else if (getFunctionId(p) == sliceRef) {
				/* simple LIMIT/OFFSET */
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {	
					if (getArg(p, 1) == lastcol->reduceid) {
						pushInstruction(map, omap[i]);
						newComment(map, "= LIMIT/OFFSET");
						/* fix return */
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = LIMIT;
						copy = cNONE;
						break;
					}
				}
			}
		} else if (getModuleId(p) == aggrRef) {
			if (getFunctionId(p) == maxRef) {
				/* MAX aggregation */
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {	
					if (getArg(p, 1) == lastcol->reduceid) {
						/* basically perform a MAX over all MAXes */
						pushInstruction(map, omap[i]);
						newComment(map, "= MAX");
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = MAX;
						copy = cNONE;
						break;
					}
				}
			} else if (getFunctionId(p) == minRef) {
				/* MIN aggregation */
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {	
					if (getArg(p, 1) == lastcol->reduceid) {
						/* basically perform a MIN over all MINs */
						pushInstruction(map, omap[i]);
						newComment(map, "= MIN");
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = MIN;
						copy = cNONE;
						break;
					}
				}
			}
			/* COUNT/SUM push down, replace with SUM in REDUCE program,
			 * fix up the return type */
			if (getFunctionId(p) == countRef) {
				/* The aggr.count will be prepended by a bat.mirror if
				 * it came from a SELECT COUNT(*) ... */
				if (getModuleId(oreduce[i - 1]) == batRef &&
							getFunctionId(oreduce[i - 1]) == mirrorRef &&
							getArg(oreduce[i - 1], 0) == getArg(p, 1))
						getArg(p, 1) = getArg(oreduce[i - 1], 1);
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {
					if (getArg(p, 1) == lastcol->reduceid) {
						pushInstruction(map, omap[i]);
						newComment(map, "= COUNT");
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = SUM;
						copy = cNONE;
						break;
					}
				}
			} else if (getFunctionId(p) == sumRef) {
				for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {
					if (getArg(p, 1) == lastcol->reduceid) {
						pushInstruction(map, omap[i]);
						newComment(map, "= SUM");
						lastcol->mapid = getArg(omap[i], 0);
						lastcol->type = getArgType(map, p, 0);
						lastcol->reduceid = getArg(p, 0);
						lastcol->postop = SUM;
						copy = cNONE;
						break;
					}
				}
			}
		} else if (getModuleId(p) == batRef && getFunctionId(p) == mirrorRef) {
			/* prepare for a count(*) where the aggr.count has a leading
			 * bat.mirror */
			if (getModuleId(oreduce[i + 1]) == aggrRef &&
						getFunctionId(oreduce[i + 1]) == countRef &&
						getArg(p, 0) == getArg(oreduce[i + 1], 1))
			{
				pushInstruction(map, omap[i]);
				newComment(map, "= COUNT(*)");
				copy = cNONE;
				/* NOTE: rest is handled in case below */
			}
		}

		/* terminate both map and reduce functions properly */
		if (p->token == ENDsymbol) {
			/* make sure the return comes at the end, as we may have
			 * added some stuff to the MAP program in this phase,
			 * changing the actual return variable */
			ret = newInstruction(map, ASSIGNsymbol);
			ret->barrier = RETURNsymbol;
			/* TODO: this is the moment that we should call MRdistribute
			 * work as well */

			/* nothing can change now any more, so finally set the
			 * calling signature of the map program */
			getArg(sig, 0) = -1; /* get rid of default retval */
			for (lastcol = col; lastcol != NULL; lastcol = lastcol->next) {
				sig = pushReturn(map, sig, lastcol->mapid);
				ret = pushReturn(map, ret, lastcol->mapid);
			}
			map->stmt[0] = sig; /* many args realloc sig, so reset it */
			pushInstruction(map, ret);

			while (!trackstack_isempty(&tracker)) {
				j = trackstack_head(&tracker);
				if (j == -1) {
					newComment(reduce, "{ call-map");
					MRdistributework(cntxt, reduce, col, sig, mrcluster);
					newComment(reduce, "} call-map");
				} else {
					pushInstruction(reduce, oreduce[j]);
				}
			}

			copy = DUP;
		}

		switch (copy) {
			case STICK:
				trackstack_push(&tracker, i);
			break;
			case SINGLE_DUP:
				copy = STICK;
				pushInstruction(map, omap[i]);
				trackstack_push(&tracker, i);
			break;
			case DUP:
				pushInstruction(map, omap[i]);
				pushInstruction(reduce, p);
			break;
			case cNONE:
				copy = STICK;
			break;
			case SINGLE:
			case FREE:
			case LEAVE:
				assert(0); /* make GCC happy */
			break;
		}
	}

#if defined(_DEBUG_OPT_MAPREDUCE) && _DEBUG_OPT_MAPREDUCE == 10
	mnstr_printf(cntxt->fdout, "MAP program\n");
	chkProgram(cntxtx->fdout, cntxt->nspace, map);
	printFunction(cntxt->fdout, map, 0, LIST_MAL_STMT | LIST_MAPI);

	mnstr_printf(cntxt->fdout, "REDUCE program\n");
	chkProgram(cntxtx->fdout, cntxt->nspace, reduce);
	printFunction(cntxt->fdout, reduce, 0, LIST_MAL_STMT | LIST_MAPI);
#endif

	lastcol = col;
	while (lastcol != NULL) {
		col = lastcol->next;
		GDKfree(lastcol);
		lastcol = col;
	}

	GDKfree(omap);
	GDKfree(oreduce);
	trackstack_destroy(&tracker);
	trackstack_destroy(&avgtrack);

	insertSymbol(findModule(cntxt->nspace, userRef), new);
	return 1;
}

