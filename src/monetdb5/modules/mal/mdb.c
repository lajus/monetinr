/*
 *The contents of this file are subject to the MonetDB Public License
 *Version 1.1 (the "License"); you may not use this file except in
 *compliance with the License. You may obtain a copy of the License at
 *http://www.monetdb.org/Legal/MonetDBLicense
 *
 *Software distributed under the License is distributed on an "AS IS"
 *basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 *License for the specific language governing rights and limitations
 *under the License.
 *
 *The Original Code is the MonetDB Database System.
 *
 *The Initial Developer of the Original Code is CWI.
 *Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 *Copyright August 2008-2013 MonetDB B.V.
 *All Rights Reserved.
*/
/*
 * author Martin Kersten
 * MAL debugger interface
 * This module provides access to the functionality offered
 * by the MonetDB debugger and interpreter status.
 * It is primarilly used in interactive sessions to activate
 * the debugger at a given point. Furthermore, the instructions
 * provide the necessary handle to generate information
 * for post-mortum analysis.
 *
 * To enable ease of debugging and performance monitoring, the MAL interpreter
 * comes with a hardwired gdb-like text-based debugger.
 * A limited set of instructions can be included in the programs themselves,
 * but beware that debugging has a global effect. Any concurrent user
 * will be affected by breakpoints being set.
 *
 * The prime scheme to inspect the MAL interpreter status is to use
 * the MAL debugger directly. However, in case of automatic exception handling
 * it helps to be able to obtain BAT versions of the critical information,
 * such as stack frame table, stack trace,
 * and the instruction(s) where an exception occurred.
 * The inspection typically occurs in the exception handling part of the
 * MAL block.
 *
 * Beware, a large class of internal errors can not easily captured this way.
 * For example, bus-errors and segmentation faults lead to premature
 * termination of the process. Similar, creation of the post-mortum
 * information may fail due to an inconsistent state or insufficient resources.
 */

#include "mdb.h"

#define MDBstatus(X) \
	if( stk->cmd && X==0 ) \
		mnstr_printf(cntxt->fdout,"#Monet Debugger off\n"); \
	else if(stk->cmd==0 && X) \
		mnstr_printf(cntxt->fdout,"#Monet Debugger on\n");

static void
pseudo(int *ret, BAT *b, str X1,str X2, str X3) {
	char buf[BUFSIZ];
	snprintf(buf,BUFSIZ,"%s_%s_%s", X1,X2,X3);
	if (BBPindex(buf) <= 0)
		BATname(b,buf);
	BATroles(b,X1,X2);
	BATmode(b,TRANSIENT);
	BATfakeCommit(b);
	*ret = b->batCacheid;
	BBPkeepref(*ret);
}
#if 0
str
MDBtoggle(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int b = 0;

	(void) mb;		/* still unused */
	if (p->argc == 1) {
		/* Toggle */
		stk->cmd = stk->cmd ? 0 : 's';
		cntxt->itrace = cntxt->itrace ? 0 : 's';
		if (stk->cmd)
			MDBdelay = 1;	/* wait for real command */
		if (stk->up)
			stk->up->cmd = 0;
		return MAL_SUCCEED;
	}
	if (p->argc > 1) {
		b = *(int *) getArgReference(stk, p, 1);
	} else
		b = stk->cmd;
	if (b)
		MDBdelay = 1;	/* wait for real command */
	MDBstatus(b);
	stk->cmd = b ? 'n' : 0;
	if (stk->up)
		stk->up->cmd = b ? 'n' : 0;
	cntxt->itrace = b ? 'n' : 0;
	return MAL_SUCCEED;
}
#endif

str
MDBstart(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	Client c;
	int pid;

	if( p->argc == 2){
		/* debug running process */
		pid = *(int *) getArgReference(stk, p, 1);
		if( pid< 0 || pid > MAL_MAXCLIENTS || mal_clients[pid].mode <= FINISHING)
			throw(MAL, "mdb.start", ILLEGAL_ARGUMENT " Illegal process id");
		c= mal_clients+pid;
		/* make client aware of being debugged */
		cntxt= c;
	} else
	if ( stk->cmd == 0)
		stk->cmd = 'n';
	cntxt->itrace = stk->cmd;
	cntxt->debugOptimizer= TRUE;
	(void) mb;
	(void) p;
	return MAL_SUCCEED;
}

str
MDBstartFactory(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) p;
		throw(MAL, "mdb.start", PROGRAM_NYI);
}

str
MDBstop(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	stk->cmd = 0;
	cntxt->itrace = 0;
	cntxt->debugOptimizer= FALSE;
	mnstr_printf(cntxt->fdout,"mdb>#EOD\n");
	(void) mb;
	(void) p;
	return MAL_SUCCEED;
}

static void
MDBtraceFlag(Client cntxt, MalStkPtr stk, int b)
{
	if (b) {
		stk->cmd = b;
		cntxt->itrace = b;
	} else {
		stk->cmd = 0;
		cntxt->itrace = 0;
	}
}

str
MDBsetTrace(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int b;

	(void) cntxt;
	(void) mb;		/* still unused */
	b = *(bit *) getArgReference(stk, p, 1);
	MDBtraceFlag(cntxt, stk, (b? (int) 't':0));
	return MAL_SUCCEED;
}

str
MDBsetVarTrace(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str v;

	(void) cntxt;
	v = *(str *) getArgReference(stk, p, 1);
	mdbSetBreakRequest(cntxt, mb, v, 't');
	stk->cmd = 'c';
	cntxt->itrace = 'c';
	return MAL_SUCCEED;
}

str
MDBgetDebug(int *ret)
{
    *ret = GDKdebug;
    return MAL_SUCCEED;
}

str
MDBsetDebug(int *ret, int *flg)
{
    *ret = GDKdebug;
    GDKdebug = *flg;
    return MAL_SUCCEED;
}
str
MDBsetDebugStr(int *ret, str *flg)
{
	*ret = GDKdebug;
	if( strcmp("threads",*flg)==0)
		GDKdebug |= GRPthreads;
	if( strcmp("memory",*flg)==0)
		GDKdebug |= GRPmemory;
	if( strcmp("properties",*flg)==0)
		GDKdebug |= GRPproperties;
	if( strcmp("io",*flg)==0)
		GDKdebug |= GRPio;
	if( strcmp("heaps",*flg)==0)
		GDKdebug |= GRPheaps;
	if( strcmp("transactions",*flg)==0)
		GDKdebug |= GRPtransactions;
	if( strcmp("modules",*flg)==0)
		GDKdebug |= GRPmodules;
	if( strcmp("algorithms",*flg)==0)
		GDKdebug |= GRPalgorithms;
	if( strcmp("performance",*flg)==0)
		GDKdebug |= GRPperformance;
#if 0
	if( strcmp("xproperties",*flg)==0)
		GDKdebug |= GRPxproperties;
#endif
	if( strcmp("forcemito",*flg)==0)
		GDKdebug |= GRPforcemito;
    return MAL_SUCCEED;
}

str
MDBsetCatch(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int b;

	(void) mb;		/* still unused */
	b = *(bit *) getArgReference(stk, p, 1);
	stk->cmd = cntxt->itrace = (b? (int) 'C':0);
	return MAL_SUCCEED;
}

str
MDBinspect(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str modnme;
	str fcnnme;
	Symbol s = NULL;

	(void) cntxt;
	if (stk != 0) {
		modnme = *(str*) getArgReference(stk, p, 1);
		fcnnme = *(str*) getArgReference(stk, p, 2);
	} else {
		modnme = getArgDefault(mb, p, 1);
		fcnnme = getArgDefault(mb, p, 2);
	}

	s = findSymbol(cntxt->nspace, putName(modnme, strlen(modnme)), putName(fcnnme, strlen(fcnnme)));

	if (s == NULL)
		throw(MAL, "mdb.inspect", RUNTIME_SIGNATURE_MISSING);
	return runMALDebugger(cntxt, s);
}

/*
 * Variables and stack information
 * The variable information can be turned into a BAT for inspection as well.
 */

static int
getStkDepth(MalStkPtr s)
{
	int i = 0;

	while (s != 0) {
		i++;
		s = s->up;
	}
	return i;
}

str
MDBStkDepth(Client cntxt, MalBlkPtr mb, MalStkPtr s, InstrPtr p)
{
	int *ret = (int *) getArgReference(s, p, 0);

	(void) cntxt;
	(void) mb;		/* fool compiler */
	*ret = getStkDepth(s);
	return MAL_SUCCEED;
}

static str
MDBgetFrame(BAT *b, BAT*bn, Client cntxt, MalBlkPtr mb, MalStkPtr s, int depth)
{
	ValPtr v;
	int i;
	char *buf = 0;

	if (depth > 0)
		return MDBgetFrame(b,bn, cntxt, mb, s->up, depth - 1);
	if (s != 0)
		for (i = 0; i < s->stktop; i++, v++) {
			v = &s->stk[i];
			ATOMformat(v->vtype, VALptr(v), &buf);
			BUNappend(b, getVarName(mb, i), FALSE);
			BUNappend(bn, buf, FALSE);
		}
	return MAL_SUCCEED;
}

str
MDBgetStackFrame(Client cntxt, MalBlkPtr m, MalStkPtr s, InstrPtr p)
{
	int *ret = (int *) getArgReference(s, p, 0);
	int *ret2 = (int *) getArgReference(s, p, 1);
	BAT *b = BATnew(TYPE_void, TYPE_str, 256);
	BAT *bn = BATnew(TYPE_void, TYPE_str, 256);

	if (b == 0)
		throw(MAL, "mdb.getStackFrame", MAL_MALLOC_FAIL);
	if (bn == 0)
		throw(MAL, "mdb.getStackFrame", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	BATseqbase(bn,0);
	pseudo(ret,b,"view","stk","frame");
	pseudo(ret2,bn,"view","stk","frame");
	return MDBgetFrame(b,bn, cntxt, m, s, 0);
}

str
MDBgetStackFrameN(Client cntxt, MalBlkPtr m, MalStkPtr s, InstrPtr p)
{
	int n, *ret = (int *) getArgReference(s, p, 0);
	int *ret2 = (int *) getArgReference(s, p, 1);
	BAT *b = BATnew(TYPE_void, TYPE_str, 256);
	BAT *bn = BATnew(TYPE_void, TYPE_str, 256);
	
	if ( b== NULL)
		throw(MAL, "mdb.getStackFrame", MAL_MALLOC_FAIL);
	if ( bn== NULL)
		throw(MAL, "mdb.getStackFrame", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	BATseqbase(bn,0);

	n = *(int *) getArgReference(s, p, 2);
	if (n < 0 || n >= getStkDepth(s)){
		BBPreleaseref(b->batCacheid);
		throw(MAL, "mdb.getStackFrame", ILLEGAL_ARGUMENT " Illegal depth.");
	}
	pseudo(ret,b,"view","stk","frame");
	pseudo(ret2,bn,"view","stk","frameB");
	return MDBgetFrame(b, bn, cntxt, m, s, n);
}

str
MDBStkTrace(Client cntxt, MalBlkPtr m, MalStkPtr s, InstrPtr p)
{
	BAT *b, *bn;
	str msg;
	char *buf;
	int *ret = (int *) getArgReference(s, p, 0);
	int *ret2 = (int *) getArgReference(s, p, 1);
	int k = 0;
	size_t len,l;

	b = BATnew(TYPE_void, TYPE_int, 256);
	if ( b== NULL)
		throw(MAL, "mdb.getStackTrace", MAL_MALLOC_FAIL);
	bn = BATnew(TYPE_void, TYPE_str, 256);
	if ( bn== NULL)
		throw(MAL, "mdb.getStackTrace", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	BATseqbase(bn,0);
	(void) cntxt;
	msg = instruction2str(s->blk, s, p, LIST_MAL_DEBUG);
	len = strlen(msg);
	buf = (char*) GDKmalloc(len +1024);
	if ( buf == NULL){
		GDKfree(msg);
		throw(MAL,"mdb.setTrace",MAL_MALLOC_FAIL);
	}
	snprintf(buf,len+1024,"%s at %s.%s[%d]", msg,
		getModuleId(getInstrPtr(m,0)),
		getFunctionId(getInstrPtr(m,0)), getPC(m, p));
	BUNappend(b, &k, FALSE);
	BUNappend(bn, buf, FALSE);
	GDKfree(msg);

	for (s = s->up, k++; s != NULL; s = s->up, k++) {
		msg = instruction2str(s->blk, s, getInstrPtr(s->blk,s->pcup),LIST_MAL_DEBUG);
		l = strlen(msg);
		if (l>len){
			GDKfree(buf);
			len=l;
			buf = (char*) GDKmalloc(len +1024);
			if ( buf == NULL){
				GDKfree(msg);
				BBPreleaseref(b->batCacheid);
				BBPreleaseref(bn->batCacheid);
				throw(MAL,"mdb.setTrace",MAL_MALLOC_FAIL);
			}
		}
		snprintf(buf,len+1024,"%s at %s.%s[%d]", msg,
			getModuleId(getInstrPtr(s->blk,0)),
			getFunctionId(getInstrPtr(s->blk,0)), s->pcup);
		BUNappend(b, &k, FALSE);
		BUNappend(bn, buf, FALSE);
		GDKfree(msg);
	}
	GDKfree(buf);
	if (!(b->batDirty&2)) b = BATsetaccess(b, BAT_READ);
	if (!(bn->batDirty&2)) bn = BATsetaccess(bn, BAT_READ);
	pseudo(ret,b,"view","stk","trace");
	pseudo(ret2,bn,"view","stk","traceB");
	return MAL_SUCCEED;
}

/*
 * Display routines
 */
str
MDBlifespan(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	Lifespan span;
	str modnme;
	str fcnnme;
	Symbol s = NULL;

	(void) cntxt;
	if (stk != 0) {
		modnme = *(str*) getArgReference(stk, p, 1);
		fcnnme = *(str*) getArgReference(stk, p, 2);
	} else {
		modnme = getArgDefault(mb, p, 1);
		fcnnme = getArgDefault(mb, p, 2);
	}

	s = findSymbol(cntxt->nspace, putName(modnme, strlen(modnme)), putName(fcnnme, strlen(fcnnme)));

	if (s == NULL)
		throw(MAL, "mdb.inspect", RUNTIME_SIGNATURE_MISSING);
	span = setLifespan(s->def);
	if( span == NULL)
		throw(MAL,"mdb.inspect", MAL_MALLOC_FAIL);
	debugLifespan(cntxt, s->def, span);
	GDKfree(span);
	(void) p;
	(void) stk;
	return MAL_SUCCEED;
}

str
MDBlist(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) p;
	(void) stk;
	printFunction(cntxt->fdout, mb, 0,  LIST_MAL_STMT | LIST_MAL_UDF );
	return MAL_SUCCEED;
}

str
MDBlistMapi(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) p;
	(void) stk;
	printFunction(cntxt->fdout, mb, 0,  LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAPI);
	return MAL_SUCCEED;
}

str
MDBlist3(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str modnme = *(str*) getArgReference(stk, p, 1);
	str fcnnme = *(str*) getArgReference(stk, p, 2);
	Symbol s = NULL;

	s = findSymbol(cntxt->nspace, putName(modnme,strlen(modnme)), putName(fcnnme, strlen(fcnnme)));
	if (s == NULL)
		throw(MAL,"mdb.list","Could not find %s.%s", modnme, fcnnme);
	printFunction(cntxt->fdout, s->def, 0,  LIST_MAL_STMT | LIST_MAL_UDF );
	(void) mb;		/* fool compiler */
	return MAL_SUCCEED;
}

str
MDBlistDetail(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) p;
	(void) stk;
	printFunction(cntxt->fdout, mb, 0, LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS | LIST_MAL_DETAIL);
	return MAL_SUCCEED;
}

str
MDBlist3Detail(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str modnme = *(str*) getArgReference(stk, p, 1);
	str fcnnme = *(str*) getArgReference(stk, p, 2);
	Symbol s = NULL;

	s = findSymbol(cntxt->nspace, putName(modnme,strlen(modnme)), putName(fcnnme, strlen(fcnnme)));
	if (s == NULL)
		throw(MAL,"mdb.list","Could not find %s.%s", modnme, fcnnme);
	printFunction(cntxt->fdout, s->def, 0,  LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS | LIST_MAL_DETAIL);
	(void) mb;		/* fool compiler */
	return NULL;
}

str
MDBvar(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) p;
	(void) stk;
	printStack(cntxt->fdout, mb, stk);
	return MAL_SUCCEED;
}

str
MDBvar3(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str modnme = *(str*) getArgReference(stk, p, 1);
	str fcnnme = *(str*) getArgReference(stk, p, 2);
	Symbol s = NULL;

	s = findSymbol(cntxt->nspace, putName(modnme,strlen(modnme)), putName(fcnnme, strlen(fcnnme)));
	if (s == NULL)
		throw(MAL,"mdb.var","Could not find %s.%s", modnme, fcnnme);
	printStack(cntxt->fdout, s->def, (s->def == mb ? stk : 0));
	(void) mb;
	return NULL;
}

/*
 * It is illustrative to dump the code when you
 * have encountered an error.
 */
str
MDBgetDefinition(Client cntxt, MalBlkPtr m, MalStkPtr stk, InstrPtr p)
{
	int i, *ret = (int *) getArgReference(stk, p, 0);
	str ps;
	BAT *b = BATnew(TYPE_void, TYPE_str, 256);

	(void) cntxt;
	if (b == 0)
		throw(MAL, "mdb.getDefinition",  MAL_MALLOC_FAIL);
	BATseqbase(b,0);

	for (i = 0; i < m->stop; i++) {
		ps = instruction2str(m,0, getInstrPtr(m, i), 1);
		BUNappend(b, ps, FALSE);
		GDKfree(ps);
	}
	if (!(b->batDirty&2)) b = BATsetaccess(b, BAT_READ);
	pseudo(ret,b,"view","fcn","stmt");

	return MAL_SUCCEED;
}

str
MDBgetExceptionVariable(str *ret, str *msg)
{
	str tail;

	tail = strchr(*msg, ':');
	if (tail == 0)
		throw(MAL, "mdb.getExceptionVariable", OPERATION_FAILED " ':'<name> missing");

	*tail = 0;
	*ret = GDKstrdup(*msg);
	*tail = ':';
	return MAL_SUCCEED;
}

str
MDBgetExceptionContext(str *ret, str *msg)
{
	str tail, tail2;

	tail = strchr(*msg, ':');
	if (tail == 0)
		throw(MAL, "mdb.getExceptionContext", OPERATION_FAILED " ':'<name> missing");
	tail2 = strchr(tail + 1, ':');
	if (tail2 == 0)
		throw(MAL, "mdb.getExceptionContext", OPERATION_FAILED " <name> missing");

	*tail2 = 0;
	*ret = GDKstrdup(tail + 1);
	*tail2 = ':';
	return MAL_SUCCEED;
}

str
MDBgetExceptionReason(str *ret, str *msg)
{
	str tail;

	tail = strchr(*msg, ':');
	if (tail == 0)
		throw(MAL, "mdb.getExceptionReason", OPERATION_FAILED " '::' missing");
	tail = strchr(tail + 1, ':');
	if (tail == 0)
		throw(MAL, "mdb.getExceptionReason", OPERATION_FAILED " ':' missing");

	*ret = GDKstrdup(tail + 1);
	return MAL_SUCCEED;
}


str
MDBshowFlowGraph(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	str fname;
	str modnme;
	str fcnnme;
	Symbol s = NULL;

	(void)cntxt;

	if (stk != 0) {
		if (p->argc == 2) {
			modnme = fcnnme = NULL;
			fname = *(str*) getArgReference(stk, p, 1);
		} else {
			modnme = *(str*) getArgReference(stk, p, 1);
			fcnnme = *(str*) getArgReference(stk, p, 2);
			fname = *(str*) getArgReference(stk, p, 3);
		}
	} else {
		modnme = getArgDefault(mb, p, 1);
		fcnnme = getArgDefault(mb, p, 2);
		fname = getArgDefault(mb, p, 3);
	}

	if (modnme != NULL) {
		s = findSymbol(cntxt->nspace, putName(modnme,strlen(modnme)), putName(fcnnme, strlen(fcnnme)));

		if (s == NULL) {
			char buf[1024];
			snprintf(buf,1024, "Could not find %s.%s\n", modnme, fcnnme);
			throw(MAL, "mdb.dot", "%s", buf);
		}
		showFlowGraph(s->def, stk, fname);
	} else {
		showFlowGraph(mb, stk, fname);
	}
	return MAL_SUCCEED;
}

str MDBdump(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci){
	(void) cntxt;
	mdbDump(cntxt,mb,stk,pci);
	return MAL_SUCCEED;
}
str MDBdummy(int *ret){
	(void) ret;
	throw(MAL, "mdb.dummy", OPERATION_FAILED);
}

str
MDBtrapFunction(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str modnme = *(str*) getArgReference(stk, pci, 1);
	str fcnnme = *(str*) getArgReference(stk, pci, 2);
	bit b=  *(bit*) getArgReference(stk,pci,3);
	(void) cntxt;
	(void) mb;
	if ( mdbSetTrap(cntxt,modnme,fcnnme,b) )
		throw(MAL,"mdb.trap", RUNTIME_SIGNATURE_MISSING);
	return MAL_SUCCEED;
}
	
/*
 * CMDmodules
 * Obtains a list of modules by looking at what files are present in the
 * module directory.
 */
static BAT *
TBL_getdir(void)
{
	BAT *b = BATnew(TYPE_void, TYPE_str, 100);
	int i = 0;

	char *mod_path;
	size_t extlen = strlen(MAL_EXT);
	size_t len;
	struct dirent *dent;
	DIR *dirp = NULL;

	if ( b == 0)
		return 0;
	BATseqbase(b,0);
	mod_path = GDKgetenv("monet_mod_path");
	if (mod_path == NULL)
		return b;
	while (*mod_path == PATH_SEP)
		mod_path++;
	if (*mod_path == 0)
		return b;

	while (mod_path || dirp) {
		if (dirp == NULL) {
			char *cur_dir;
			char *p;
			size_t l;

			if ((p = strchr(mod_path, PATH_SEP)) != NULL) {
				l = p - mod_path;
			} else {
				l = strlen(mod_path);
			}
			cur_dir = GDKmalloc(l + 1);
			if ( cur_dir == NULL){
				GDKsyserror("mdb.modules"MAL_MALLOC_FAIL);
				return b;
			}
			strncpy(cur_dir, mod_path, l);
			cur_dir[l] = 0;
			if ((mod_path = p) != NULL) {
				while (*mod_path == PATH_SEP)
					mod_path++;
			}
			dirp = opendir(cur_dir);
			GDKfree(cur_dir);
			if (dirp == NULL)
				continue;
		}
		if ((dent = readdir(dirp)) == NULL) {
			closedir(dirp);
			dirp = NULL;
			continue;
		}
		len = strlen(dent->d_name);
		if (len < extlen || strcmp(dent->d_name + len - extlen, MAL_EXT) != 0)
			continue;
		dent->d_name[len - extlen] = 0;
		BUNappend(b, dent->d_name, FALSE);
		i++;
	}
	return b;
}

str
CMDmodules(int *bid)
{
	BAT *b = TBL_getdir();

	if (b) {
		*bid = b->batCacheid;
		BBPkeepref(*bid);
	}
	return MAL_SUCCEED;
}

