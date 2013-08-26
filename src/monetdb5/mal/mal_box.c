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
 * @a M.L. Kersten
 * @+ Boxed Variables
 * Clients sessions often come with a global scope of variable settings.
 * Access to these global variables should be easy,
 * but they should also provide protection against concurrent update
 * when the client wishes to perform parallel processing.
 * Likewise, databases, query languages, etc. may define constants and variables
 * accessible, e.g., relational schemas, to a selected user group.
 *
 * The approach taken is to rely on persistent object spaces
 * as pioniered in Lynda and -later- JavaSpaces.
 * They are called boxes in MonetDB and act as managed containers
 * for persistent variables.
 *
 * Before a client program  can interact with a box, it
 * should open it, passing qualifying authorization
 * information and parameters to instruct the box-manager
 * of the intended use. A built-in box is implicitly
 * opened when you request for its service.
 *
 * At the end of a session, the box should be closed. Some box-managers
 * may implement a lease-scheme to automatically close interaction
 * with a client when the lease runs out. Likewise, the box can
 * be notified when the last reference to a leased object
 * ceases to exist.
 *
 * A box can be extended with a new object using the function
 * deposit(name) with name a local variable.
 * The default implementation silently accepts any new definition of the box.
 * If the variable was known already in the box, its value is overwritten.
 *
 * A local copy of an object can be obtained using the
 * pattern 'take(name,[param])', where name denotes the variable
 * of interest. The type of the receiving variable should
 * match the one known for the object. Whether an actual
 * copy is produced or a reference to a shared object
 * is returned is defined by the box manager.
 *
 * The object is given back to the box manager calling 'release(name)'.
 * It may update the content of the repository accordingly, release locks,
 * and move the value to persistent store. Whatever the semantics
 * of the box requires. [The default implementation is a no-op]
 *
 * Finally, the object manager can be requested to 'discard(name)'
 * a variable completely.  The default implementation is to reclaim
 * the space in the box.
 *
 * Concurrency control, replication services, as well as access
 * to remote stores may be delegated to a box manager.
 * Depending on the intended semantics, the box manager
 * may keep track of the clients holding links to this members,
 * provide a traditional 2-phase
 * locking scheme, optimistic control, or check-out/check-in scheme.
 * In all cases, these management issues are transparant to the
 * main thread (=client) of control, which operates on a temporary
 * snapshot. For the time being we realize the managers as critical
 * code sections, i.e. one client is permitted access to the box space
 * at a time.
 *
 * Fo example, consider the client function:
 * @example
 * function myfcn():void;
 *     b:bat[:oid,:int] := bbp.take("mytable");
 *     c:bat[:int,:str] := sql.take("person","age");
 *     d:= intersect(b,c);
 *     io.print(d);
 *     u:str:= client.take(user);
 *     io.print(u);
 *     client.release(user);
 * end function;
 * @end example
 *
 * The function binds to a copy from the local persistent BAT space,
 * much like bat-names are resolved in earlier MonetDB versions. The second
 * statement uses an implementation of take that searches a variable
 * of interest using two string properties. It illustrates that
 * a box manager is free to extend/overload the predefined scheme,
 * which is geared towards storing MAL variables.
 *
 * The result bat @sc{c} is temporary and disappears upon garbage
 * collection. The variable @sc{u} is looked up as the string object user.
 *
 * Note that BATs @sc{b} and @sc{c} need be released at some point. In general
 * this point in time does not coincide with a computational boundary
 * like a function return. During a session, several bats may be taken
 * out of the box, being processed, and only at the end of a session
 * being released. In this example, it means that the reference to
 * b and c is lost at the end of the function (due to garbarge collection)
 * and that subsequent use requires another take() call.
 * The box manager bbp is notified of the implicit release and
 * can take garbage collection actions.
 *
 * The box may be inspected at several times during a scenario run.
 * The first time is when the MAL program is type-checked for the
 * box operations. Typechecking a take() function is tricky.
 * If the argument is a string literal, the box can be queried
 * directly for the objects' type.
 * If found, its type is matched against the lhs variable.
 * This strategy fails in the situation when at
 * runtime the object is subsequently replaced by another
 * typed-instance in the box. We assume this not to happen and
 * the exceptions it raises a valuable advice to reconsider
 * the programming style.
 *
 * The type indicator for the destination variable should be
 * provided to proceed with proper type checking.
 * It can resolve overloaded function selection.
 *
 * Inspection of the Box can be encoded using an iterator at the MAL
 * layer and relying on the functionality of the box.
 * However, to improve introspection, we assume that all box
 * implementations provide a few rudimentary functions, called objects(arglist)
 * and dir(arglist). The function objects() produces a BAT with
 * the object names, possibly limited to those identified by
 * the arglist.
 *
 * The world of boxes has not been explored deeply yet.
 * It is envisioned that it could play a role to import/export
 * different objects, e.g.,
 * introduce xml.take() which converts an XML document to a BAT,
 * jpeg.take() similer for an image.
 *
 * Nesting boxes is possible. It provides a simple
 * containment scheme between boxes, but in general will interfere with
 * the semantics of each box.
 *
 * Each box has (should) have an access control list, which names
 * the users having permission to read/write its content.
 * The first one to create the box becomes the owner. He may grant/revoke
 * access to the box to users on a selective basis.
 *
 * @- Session Box
 * Aside from box associated with the modules, a session box is created
 * dynamically on behalf of each client. Such boxes are considered private
 * and require access by the user name (and password).
 * At the end of a session they are closed, which means that they are
 * saved in persistent store until the next session starts.
 * For example:
 * @example
 * function m():void;
 *     box.open("client_name");
 *     box.deposit("client_name","pi",3.417:flt);
 *     f:flt := box.take("client_name","pi");
 *     io.print(t);
 *     box.close("client_name");
 * end function;
 * @end example
 * @-
 * In the namespace it is placed subordinate to any space introduced by the
 * system administrator. It will contain global client data, e.g.,
 * user, language, database, port, and any other session parameter.
 * The boxes are all collected in the context of the database directory,
 * i.e. the directory <dbpath>/box
 *
 * @- Garbage Collection
 * The key objects managed by MonetDB are the persistent BATs, which
 * call for an efficient scheme to make them accessible for manipulation
 * in the MAL procedures taking into account a possibly hostile
 * parallel access.
 *
 * Most kernel routines produce BATs as a result, which will be referenced
 * from the runtime stack. They should be garbage collected as soon as
 * deemed possible to free-up space. By default, temporary results are
 * garbage collected before returning from a MAL function.
 *
 * @- Globale Environment
 * The top level interaction keeps a 'box' with global variables,
 * i.e. each MAL statement is interpreted in an already initialized
 * stack frame.
 * This causes the following problems: 1) how to get rid of global variables
 * and 2) how to deal with variables that can take 'any' type.
 * It is illustrated as follows:
 * @example
 * f:= const.take("dbname");
 * io.print(f);
 * @end example
 * When executed in the context of a function, the answer will be
 * simple [ nil ]. The reason is that the expecteed type is not known
 * at compilation time. The correct definition would have been
 * @example
 * f:str:= const.take("dbname");
 * io.print(f);
 * @end example
 */
/*
 * The hierarchy of object spaces ends at the root of the tree.
 * This is a dummy element and should contain system-wide objects
 * only.
 */

#include "monetdb_config.h"
#include "mal_box.h"
#include "mal_interpreter.h"	/* for garbageCollector() & garbageElement() */
#include "mal_client.h"

#if defined(_MSC_VER) && _MSC_VER >= 1400
#define access _access
#define chmod _chmod
#endif

#define MAXSPACES 64		/* >MAXCLIENTS+ max modules !! */
static Box malbox[MAXSPACES];
static int topbox=0;

static str boxFileName(Box box, str extension);

static Box
newBox(str name)
{
	Box obj = 0;
	int i;

	MT_lock_set(&mal_contextLock, "newBox");
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "create new box '%s'\n", name);
#endif
	for (i = 0; i < topbox; i++)
		if (malbox[i] != NULL && idcmp(name, malbox[i]->name) == 0) {
			MT_lock_unset(&mal_contextLock, "newBox");
#ifdef DEBUG_MAL_BOX
			mnstr_printf(GDKout,"newBox:duplicate box definition\n");
#endif
			return malbox[i];
		}
	for (i = 0; i < topbox; i++)
		if (malbox[i] == NULL) {
			obj= (Box) GDKzalloc(sizeof(BoxRecord));
			obj->name= GDKstrdup(name);
			obj->sym=  newMalBlk(MAXVARS,STMT_INCREMENT);
			obj->val = newGlobalStack(MAXVARS);
			if ( obj->val == NULL)
				showException(GDKout, MAL,"box.new", MAL_MALLOC_FAIL);
			MT_lock_init(&obj->lock,"M5_box_lock");
			malbox[i] = obj;
			break;
		}
	MT_lock_unset(&mal_contextLock, "newBox");
	if (i == topbox) {
		if ( topbox < MAXSPACES){
			obj= (Box) GDKzalloc(sizeof(BoxRecord));
			obj->name= GDKstrdup(name);
			obj->sym=  newMalBlk(MAXVARS,STMT_INCREMENT);
			obj->val = newGlobalStack(MAXVARS);
			if ( obj->val == NULL)
				showException(GDKout, MAL,"box.new", MAL_MALLOC_FAIL);
			MT_lock_init(&obj->lock,"M5_box_lock");
			malbox[topbox++] = obj;
		} else
			return NULL;
	}
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "succeeded at %d\n", i);
#endif
	return obj;

}

#if 0
static void
freeBox(Client cntxt, int i){
		if (malbox[i] && malbox[i]->val) {
			garbageCollector(cntxt, malbox[i]->sym, malbox[i]->val,TRUE);
			GDKfree(malbox[i]->name);
			freeMalBlk(malbox[i]->sym);
			GDKfree(malbox[i]->val);
			GDKfree(malbox[i]);
			malbox[i] = 0;
		}
}
void
freeBoxes(Client cntxt)
{
	int i;

	for (i = 0; i < topbox;  i++)
		freeBox(cntxt, i);
}
#endif

Box
findBox(str name)
{
	int i;

	MT_lock_set(&mal_contextLock, "findBox");

	for (i = 0; i < topbox; i++)
		if (malbox[i] != NULL && name && idcmp(name, malbox[i]->name) == 0) {
#ifdef DEBUG_MAL_BOX
			mnstr_printf(GDKout, "found the box '%s' %d\n", name, i);
#endif
			MT_lock_unset(&mal_contextLock, "findBox");
			return malbox[i];
		}
	MT_lock_unset(&mal_contextLock, "findBox");
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "could not find the box '%s' \n", name);
#endif
	return 0;

}

Box
openBox(str name)
{
	Box box = findBox(name);

	if (box)
		return box;
	box = newBox(name);
	if ( box){
		loadBox(name);
		box->dirty= FALSE;
	}
	return box;
}

int
closeBox(str name, int flag)
{
	Box box;

	if ((box = findBox(name))) {
		saveBox(box, flag);
		return 0;
	}
	return -1;
}

void
destroyBox(str name)
{
	int i, j;
	str boxfile;

	MT_lock_set(&mal_contextLock, "destroyBox");
	for (i = j = 0; i < topbox; i++) {
		if (idcmp(malbox[i]->name, name) == 0) {
			freeMalBlk(malbox[i]->sym);
			malbox[i]->sym = NULL;
			if ( malbox[i]->val)
				freeStack(malbox[i]->val);
			malbox[i]->val = NULL;
			boxfile = boxFileName(malbox[i], 0);
			unlink(boxfile);
			GDKfree(boxfile);
			GDKfree(malbox[i]->name);
			malbox[i]->name = NULL;
			MT_lock_destroy(&malbox[i]->lock);
		} else {
			if (j < i)
				malbox[j] = malbox[i];
			j++;
		}
	}
	for (i = j; i < topbox; i++)
		malbox[i] = NULL;
	topbox = j;
	MT_lock_unset(&mal_contextLock, "destroyBox");
}

/*
 * @- Operations
 * The deposit operation silently accepts any request to store
 * an element.
 */
int
depositBox(Box box, str name, int type, ValPtr val)
{
	int i;
	ValPtr v;

	i = findVariable(box->sym, name);
	if ( box->val == NULL)
		return 0;
	if (i < 0) {
		i = newVariable(box->sym, GDKstrdup(name), type);
		if (box->val->stksize <= i)
			box->val =reallocStack(box->val, STACKINCR);
	}
	v = &box->val->stk[i];
	VALclear(v);
	box->val->stktop++;
	VALcopy(v, val);
	box->dirty= TRUE;
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "depositBox: entered '%s' at %d type %d\n", name, i, type);
#endif
	return 0;
}

void
insertToBox(Box box, str nme, str val)
{
	ValRecord vr;

	vr.vtype = TYPE_str;
	vr.val.sval = val? val: (str)str_nil;
	vr.len = (int)strlen(vr.val.sval);
	(void) depositBox(box, nme, TYPE_str, &vr);
}

/*
 * @-
 * Taking an element from a box is only permitted if the type of
 * the receiver matches the type of the source.
 */
int
takeBox(Box box, str name, ValPtr val, int tpe)
{
	int i;
	ValPtr v;

	i = findVariable(box->sym, name);
	if ( box->val == NULL)
		return 0;

#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "takeBox: found '%s' at %d\n", name, i);
#endif
	if (i < 0)
		return i;
	v = &box->val->stk[i];
	if (val->vtype != v->vtype && v->vtype != TYPE_any && tpe != TYPE_any) {
#ifdef DEBUG_MAL_BOX
		mnstr_printf(GDKout, "takeBox:type error %d,%d\n", val->vtype, box->val->stk[i].vtype);
#endif
		return 0;
	}
	VALcopy(val, &box->val->stk[i]);
	if (val->vtype == TYPE_bat)
		BBPincref(val->val.bval, TRUE);

#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "takeBox:'%s' from '%s'\n", name, box->name);
#endif
	return 0;
}

/*
 * @-
 * The function bindBAT relates a logical BAT name with a physical
 * representation. The bind commands are typically found in the boxes,
 * which provide users a partial view over the BAT storage area.
 * That is, all bats bound can be taken out of the box upon need.
 * A variable can be rebound to another BAT at any time.
 */
int
bindBAT(Box box, str name, str location)
{
	int i;
	ValPtr v;

	i = findVariable(box->sym, name);
	if ( box->val == NULL)
		return 0;
	if (i < 0)
		i = newVariable(box->sym, GDKstrdup(name), TYPE_any);
	v = &box->val->stk[i];
	v->val.bval = BBPindex(location);

	if (v->val.bval == 0)
		return -1;
	v->vtype = TYPE_bat;
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "bindBox:'%s' to '%s' [%d] in '%s'\n", name, location, v->val.bval, box->name);
#endif
	return 0;
}

int
releaseBox(Box box, str name)
{
	int i;

#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "releaseBox:%s from %s\n", name, box->name);
#endif
	i = findVariable(box->sym, name);
	if (i < 0)
		return i;
	return 0;
}

int
releaseAllBox(Box box)
{
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "releaseAllBox:%s \n", box->name);
#else
	(void) box;
#endif
	return 0;
}

int
discardBox(Box box, str name)
{
	int i, j;

#ifdef DEBUG_MAL_BOX
	mnstr_printf(cntxt->fdout, "discardBox:%s from %s\n", name, box->name);
#endif
	i = findVariable(box->sym, name);
	if (i < 0)
		return i;
	if ( box->val == NULL)
		return 0;

	garbageElement(NULL, &box->val->stk[i]);
	for (j = i; j < box->sym->vtop - 2; j++) {
		box->sym->var[j] = box->sym->var[j + 1];

		VALcopy(&box->val->stk[j], &box->val->stk[j + 1]);
	}
	box->sym->vtop--;
	box->val->stktop--;
	box->dirty= TRUE;
	return 0;
}

/*
 * @-
 * The elements can be obtained using iterator, which returns the name
 * of the next element in the box.
 */
oid
nextBoxElement(Box box, oid *cursor, ValPtr v)
{
	if (*cursor >= (oid) box->sym->vtop) {
		*cursor = (oid) oid_nil;
		return 0;
	}
	if ( box->val == NULL)
		return oid_nil;
	v->vtype = TYPE_str;
	v->val.sval = getBoxName(box, *cursor);
	*cursor = *cursor + 1;
	return 0;
}

str
getBoxName(Box box, lng i)
{
	str s;

	s = getVarName(box->sym, (int) i);
	return GDKstrdup(s);
}

str
toString(Box box, lng i)
{
	(void) box;
	(void) i;		/* fool the compiler */
	return GDKstrdup("");
}

str
getBoxNames(int *bid)
{
	BAT *b;
	int i;

	b = BATnew(TYPE_int, TYPE_str, (BUN) MAXSPACES);
	if (b == NULL)
		throw(MAL, "box.getBoxNames", MAL_MALLOC_FAIL);
	for (i = 0; i < topbox; i++)
		if (malbox[i] != NULL) {
			BUNins(b, &i, malbox[i]->name, FALSE);
		}
	BBPkeepref(*bid = b->batCacheid);
	return MAL_SUCCEED;
}

/*
 * @- Persistency
 * The content of a box is saved on a file for subsequent re-use.
 * The storage is explicitly in terms of MAL operations.
 * For BAT objects, the corresponding object is made persistent.
 * Note that the user can not control these operations directly.
 * The box container is created if it did not yet exist.
 */
str
boxFileName(Box box, str backup)
{
	char boxfile[PATHLENGTH];
	size_t i = 0;

	snprintf(boxfile, PATHLENGTH, "%s%cbox", GDKgetenv("gdk_dbpath"), DIR_SEP);
	if (mkdir(boxfile, 0755) < 0 && errno != EEXIST) {
		showException(GDKout, MAL,"box.fileName", "can not create box directory");
		return NULL;
	}
	i = strlen(boxfile);
	if (backup) {
		snprintf(boxfile + i, PATHLENGTH - i, "%c%s", DIR_SEP, backup);
		if (mkdir(boxfile, 0755) < 0 && errno != EEXIST) {
			showException(GDKout, MAL,"box.fileName", "can not create box directory");
			return NULL;
		}
		i += strlen(backup) + 1;
	}
	snprintf(boxfile + i, PATHLENGTH - i, "%c%s.box", DIR_SEP, box->name);
	return GDKstrdup(boxfile);
}

#ifndef R_OK
#define R_OK 4
#endif
static stream *
prepareSaveBox(Box box, str *boxfile, str *boxfilebak)
{
	stream *f;

	*boxfile = boxFileName(box, 0);
	*boxfilebak = boxFileName(box, "backup");

	if (*boxfile == 0)
		return 0;
	if (access(*boxfile,R_OK)==0 &&
		(unlink(*boxfilebak), rename(*boxfile, *boxfilebak) < 0)) {
#ifdef DEBUG_MAL_BOX
		mnstr_printf(GDKout, "saveBox:can not rename %s to %s\n", *boxfile, *boxfilebak);
#endif
		showException(GDKout, MAL,"box.saveBox", "can not rename backup");
		GDKfree(*boxfile); *boxfile = NULL;
		GDKfree(*boxfilebak); *boxfilebak = NULL;
		return 0;
		}

	f = open_wastream(*boxfile);
#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#endif
	if (f != NULL)
		chmod(*boxfile, (S_IRUSR | S_IWUSR));
	else
		showException(GDKout, MAL,"box.saveBox", "can not create box file");
	if (f == NULL) {
		GDKfree(*boxfile); *boxfile= NULL;
		GDKfree(*boxfilebak); *boxfilebak= NULL;
	}
	return f;
}

int
saveBox(Box box, int flag)
{
	int i;
	stream *f;
	ValPtr v;
	str boxfile, boxfilebak;

	(void) flag;		/* fool the compiler */

	if( box->dirty== FALSE)
		return 0;
	if ( box->val == NULL){
		showException(GDKout, MAL,"box.save","No box storage");
		return 0;
	}
	f = prepareSaveBox(box, &boxfile, &boxfilebak);
	if (f == NULL)
		return 1;
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "saveBox:created %s\n", boxfile);
#endif
	for (i = 0; i < box->sym->vtop; i++) {
		str tnme;
		v = &box->val->stk[i];
		if (v->vtype == TYPE_bat) {
			BAT *b = (BAT *) BATdescriptor(v->val.bval);
			if (b) {
				if (b->batPersistence == PERSISTENT){
					str ht = getTypeName(getHeadType(getVarType(box->sym,i)));
					str tt = getTypeName(getTailType(getVarType(box->sym,i)));
					mnstr_printf(f, "%s:bat[:%s,:%s]:= %s.bind(%d);\n",
						getVarName(box->sym, i),  ht, tt,
						box->name, b->batCacheid);
					GDKfree(ht);
					GDKfree(tt);
					BATsave(b);
				}
				BBPreleaseref(b->batCacheid);
			}
		} else {
			tnme = getTypeName(getVarType(box->sym,i));
			mnstr_printf(f, "%s := ", getVarName(box->sym, i));
			ATOMprint(v->vtype, VALptr(v), f);
			mnstr_printf(f, ":%s;\n", tnme);
			mnstr_printf(f, "%s.deposit(\"%s\",%s);\n",
				box->name, getVarName(box->sym, i), getVarName(box->sym, i));
			GDKfree(tnme);
		}
	}
	close_stream(f);
	GDKfree(boxfile);
	GDKfree(boxfilebak);
	return 0;
}

/*
 * @-
 * Loading a box is equivalent to reading a script.
 * Beware to execute it into its own context.
 */
void
loadBox(str name)
{
	char boxfile[PATHLENGTH];
	size_t i = 0;

	snprintf(boxfile, PATHLENGTH, "%s%cbox", GDKgetenv("gdk_dbpath"), DIR_SEP);
	mkdir(boxfile,0755); /* ignore errors */
	i = strlen(boxfile);
	snprintf(boxfile + i, PATHLENGTH - i, "%c%s.box", DIR_SEP, name);
#ifdef DEBUG_MAL_BOX
	mnstr_printf(GDKout, "loadBox:start loading the file %s\n", boxfile);
#endif
	if (access(boxfile, R_OK) == 0) {
		Client child= MCforkClient(mal_clients);
		if( child != mal_clients){
			defaultScenario(child);
			evalFile(child, boxfile, 0);
			MCcloseClient(child);
#ifdef DEBUG_MAL_BOX
			mnstr_printf(GDKout, "loadBox:loaded the file %s\n", boxfile);
#endif
		}
	}
}

