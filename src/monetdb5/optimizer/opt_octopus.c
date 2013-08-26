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
 * @a M. Kersten
 * @- Map-reduce processing
 * Query execution can be improved significantly using distributed processing.
 * Traditionally, this encompasses fragmentation and allocation of the base
 * tables over multiple sites and query plans that include on the fly transport
 * of intermediate results.
 *
 * Breaking the database into pieces itself is a well-studied area.
 * Most approaches consider the workload and search for a good split
 * of the base tables, such that the workload performance improves.
 *
 * In the Octopus we focus on SQL for map-reduce processing.
 * The first approach is to capitalize upon the metosis and
 * mergetable optimizers. They break the table into pieces
 * based on the head and propagate the effect through the plan.
 * Breaking the table depends on the number of sites to play with,
 * which can be obtained dynamically from the merovingian.
 *
 * The octopus untangles the resulting plan into a controlling function
 * and a plan for each tentacle.
 * The octopus is to be called after the mergetable optimizer.
 *
 * A snippet of an octopus plan with two tentacles is shown below
 *
 * @verbatim
 * function octopus.exec_qry(srv:str,fcn:str, ver:int):bat[:oid,:int];
 * 	conn:= remote.connect(srv,"monetdb","monetdb");
 * 	v:= remote.put(conn,ver);
 * 	r:= remote.exec(conn,"octopus",fcn,v);
 * 	b:bat[:oid,:int]:= remote.get(conn,r);
 * catch REMOTEexception;
 * 	octopus.reschedule(srv);
 * exit REMOTEexception;
 * 	return b;
 * end exec_qry;
 * function octopus.leg0(version:int):bat[:oid,:int];
 * 	conn:= remote.connect("demo","monetdb","monetdb");
 *     _54:bat[:oid,:int] := octopus.bind(conn,mvc,"sys","squida","bid",0,0@0,25@0,version);
 *     _64:bat[:oid,:int] := octopus.bind(conn,mvc,"sys","squida","bid",2,0@0,25@0,version);
 *     _72 := algebra.kdifference(_54,_64);
 *     _78 := algebra.kunion(_72,_64);
 *     _13:bat[:oid,:oid]  := octopus.bind_dbat(conn,mvc,"sys","squida",1,version);
 *     _14 := bat.reverse(_13);
 *     _85 := algebra.kdifference(_78,_14);
 *     return leg0:= _85;
 * end octopus.leg0;
 * function octopus.leg1( version:int):bat[:oid,:int];
 * 	conn:= remote.connect("demo","monetdb","monetdb");
 *     _56:bat[:oid,:int] := octopus.bind(conn,mvc,"sys","squida","bid",0,version,2,1);
 *     _65:bat[:oid,:int] := octopus.bind(conn,mvc,"sys","squida","bid",2,version,2,1);
 *     _72 := algebra.kdifference(_56,_65);
 *     _78 := algebra.kunion(_72,_65);
 *     _13:bat[:oid,:oid]  := octopus.bind_dbat(conn,mvc,"sys","squida",1,version);
 *     _14 := bat.reverse(_13);
 *     _85 := algebra.kdifference(_78,_14);
 *     return leg1= _85;
 * end octopus.leg1;
 *
 * function user.qry():void;
 * barrier (_100,version:int):= scheduler.octopus(10);
 *     _87 := octopus.exec_qry("tbd","leg0",version);
 *     _88 := octopus.exec_qry("tbd","leg1",version);
 * exit (_100,version);
 *     _15 := mat.pack(_87,_88);
 *     _16 := sql.resultSet(1,1,_15);
 *     sql.rsColumn(_16,"sys.squida","bid","int",32,0,_15);
 *     _21 := io.stdout();
 *     sql.exportResult(_21,_16);
 * end qry;
 * @end verbatim
 *
 * [versioning]
 * The argument to the tentacle is the expected version of the database
 * to be used and optional arguments for the query.
 *
 * [partition access]
 * For a tentacle to work it needs access to its storage layer.
 * It is encapsulated in the operation octopus.bind(Conn,S,T,C,V,P,I),
 * which performs an upcall to the octopus Conn to deliver
 * the Ith piece of the column S.T.C partitioned into P pieces
 * matching version V.
 *
 * Ideally, the interaction with the head is restricted
 * to calling for a handle to access the partition from
 * its source directly.
 * In a shared disk setup, we merely ensure that a
 * possibly cached version is not out of date.
 * The last resort is to deliver the partition by the
 * octopus using a remote BAT copy.
 * Doing so would effectively implement
 * a kind of distributed disk system. The current state of
 * affairs allows for a track where even a low level
 * ftp- call would suffice to obtain the complete BAT
 * from anywhere, or we can provide a NAS functionality
 * over the database store that delivers the partitions
 * of interest.
 *
 * The octopus upcalls for delivering the partitions should
 * be encapsulated into a function to shield all possible
 * remote access operations. It is a function registered
 * by the SQL front-end.
 *
 * [recycling]
 * The version argument passed during the tentacle call
 * is used to enforce synchronisation of the recycler caches
 * between the octopus and its tentacles.
 * If the version does not change between calls, the
 * recylcer will simply step over the bind() calls,
 * reusing with it already have instead.
 * If for local resource management decisions the cache
 * is cleared, then it will automatically trigger a
 * re-execution of the bind().
 *
 * [Naming]
 * The tentacles received from the octopus should be ensured
 * not to clash with those already known. Therefore, we simply
 * tag them by orginating site.
 *
 * [Caveats]
 * Updates on the persistent tables are not handled yet. They should
 * lead to an upcall to the head for inclusion. Any update invalidates
 * the request to distributed processing.
 * In the same line, multi-statement SQL transactions and
 * updates to global variables are ignored.
 *
 * Global variables are tricky, because they are part of the
 * session context. To make it work, we need to be able to perform
 * an upcall to that context (=dangerous).
 * The solution is that any variable context should be
 * passed through a relation.
 */
#include "monetdb_config.h"
#include "opt_octopus.h"
#include "opt_deadcode.h"
#include "mal_interpreter.h"	/* for showErrors() */
#include "mal_builder.h"
#include "mal_sabaoth.h"
#include "remote.h"
#include "mal_recycle.h"

typedef struct MALPART {
	str sch;
	str tab;
	/*	oid low;
	oid hgh;*/
	int part_nr;
	int nr_parts;
	int *ret;
	int retcnt;
	wrd rows;
	int top;
} MalPart, *MalPartPtr;

#define memb(x,i) ( x & ((int)1 << i) )
#define memb1(x,i) (!( x & ~((int)1 << i) ))


/*
 * The algorithm consists of several steps. The first one
 * replaces the original query and creates the tentacle
 * functions. In the second phase the should be registered
 * at the different sites.
 *
 * The key observation is that whenever we encouter a mat.pack,
 * there is a need to bring information together for inspection.
 * Therefore, we recursively break a plan by looking for the
 * pack instructions and collect all dependent instructions.
 * The original block is trimmed as far as needed.
 */
static lng octopusSeq = 0;
sht bidStrategy = 1;

static MalPartPtr octCluster = NULL;
static int octClCnt = 0;
static int octClResSize = 32; /* maximum number of results per tentacle */
static int octFullRepl = 0;

static void
OCTinitMalPart(void)
{
	if ( octClCnt ){
		int i;
		for (i = 0; i < octClCnt; i++){
			GDKfree(octCluster[i].sch);
			GDKfree(octCluster[i].tab);
			GDKfree(octCluster[i].ret);
		}
		GDKfree(octCluster);
	}
	octCluster = (MalPartPtr) GDKzalloc( sizeof(MalPart) * MAXSLICES);
	octClCnt = 0;
}

static int
OCTgetMalPart(str sch, str tab, int pn, int np)
{
	int i;

	for (i = 0; i < octClCnt; i++)
		if ( strcmp(tab, octCluster[i].tab) == 0  &&
			pn == octCluster[i].part_nr &&	np == octCluster[i].nr_parts )
			break;
	if ( i < octClCnt )
		return i;
	octCluster[i].sch = GDKstrdup(sch);
	octCluster[i].tab = GDKstrdup(tab);
	octCluster[i].part_nr = pn;
	octCluster[i].nr_parts = np;
	octCluster[i].ret = (int*) GDKzalloc( sizeof(int) * octClResSize);
	octCluster[i].retcnt = 0;
	octClCnt++;
	return i; 
}

static void
OCTaddResult(int cl, int residx, int iidx)
{
	MalPartPtr c;
	int i, found = 0;

	if ( cl <= 0 || cl >= octClCnt ) {
		mnstr_printf(GDKout, "Illegal instruction partition index \n");
		return;
	}
	c = &octCluster[cl];
	for (i = 0; i < c->retcnt; i++)
		if ( c->ret[i] == residx )
			found = 1;
	if (!found){
		if ( c->retcnt < octClResSize ){
			c->ret[c->retcnt++] = residx;
			if ( c->top < iidx )
				c->top = iidx;
		}
		else 
			mnstr_printf(GDKout, "No room for more results \n");
	}
}

static bit 
isAView(InstrPtr p)
{
	if ( ( getModuleId(p) == batRef && 
		  ( getFunctionId(p) == reverseRef || getFunctionId(p) == mirrorRef )) ||
		 ( getModuleId(p) == algebraRef && getFunctionId(p) == markTRef ))
		 return 1;
	return 0;
}

static int
OCTinitcode(Client cntxt, MalBlkPtr mb){
	InstrPtr p;
	str s;
	str l = NULL;

	(void) cntxt;

	p = newStmt(mb, remoteRef,connectRef);
	s = GDKgetenv("merovingian_uri");
	if (s == NULL) /* aparently not under Merovingian control, fall back to local only */
		SABAOTHgetLocalConnection(&l);
	p= pushStr(mb,p, s == NULL ? l : s);
	p= pushStr(mb,p,"monetdb");
	p= pushStr(mb,p,"monetdb");
	p= pushStr(mb,p,"msql");
	if (l)
		GDKfree(l);
	return getArg(p,0);
}

/* extract cluster from the mask */
static int
OCTgetCluster(int mask)
{
	int i = 0;

	while( mask ){
		mask = mask >> 1;
		i++;
	}
	if ( i > octClCnt )
		return 0;
	else
		return i-1; 
}

/*
 * Be prepared to catch errors from the remote site.
 * You should catch them, otherwise the session is not closed.
 * Beware, exceptions should be catched and thrown after the
 * connection has been closed.
 */

static int
getJoinPathType(MalBlkPtr mb, InstrPtr p)
{
	int tpe;
	int ht, tt;

	if ( p->argc < 3)
		return TYPE_any;
	if ( !isaBatType(getArgType(mb,p,1)) ||
		!isaBatType(getArgType(mb,p,p->argc-1))) 
		return TYPE_any;
	ht = getHeadType(getArgType(mb,p,1));
	tt = getTailType(getArgType(mb,p,p->argc-1));
	tpe = newBatType(ht,tt);
	return tpe;
}

static MalBlkPtr
OCTnewTentacle(Client cntxt, MalBlkPtr mb, bte tidx, int v2, int *cl)
{
	Symbol s;
	MalBlkPtr tmb;
	MalPartPtr ocl;
	InstrPtr p, tp, sig = mb->stmt[0], tsig;
	char buf[BUFSIZ];
	int *alias= (int*) GDKzalloc(mb->vtop * sizeof(int));
	int i, j, k, conn= 0, last = mb->stop-1, tpe;
	str nm;

	ocl = &octCluster[tidx];

	snprintf(buf,BUFSIZ,"tentacle_"LLFMT"_%d", mb->legid,tidx); 
	nm = putName(buf,strlen(buf));
	s = newFunction(octopusRef,nm,FUNCTIONsymbol);
	tmb = s->def;
	tmb->keephistory= mb->keephistory;

	/* tentacle signature */
	p = getInstrPtr(tmb,0);
	k = getArg(p,0);
	setVarType(tmb,k,getVarType(mb,ocl->ret[0]));
	setVarUDFtype(tmb,k);
	if ( ocl->retcnt > 1 ) {
		if ( tmb->var[k]->name ) 
			GDKfree(tmb->var[k]->name);
		tmb->var[k]->name = GDKstrdup("res0");
		for ( i = 1; i < ocl->retcnt; i++){
			snprintf(buf,BUFSIZ,"res%d", i);
			tpe = getVarType(mb,ocl->ret[i]); 
			k= newVariable(tmb,GDKstrdup(buf),tpe);
			setVarUDFtype(tmb,k);
			p = pushReturn(tmb,p,k);
		}
	}

	alias[v2] = cloneVariable(tmb,mb,v2);
	p = pushArgument(tmb,p, alias[v2]);
	for(i = sig->retc; i < sig->argc; i++){
		alias[getArg(sig,i)] = cloneVariable(tmb,mb,getArg(sig,i));
		p = pushArgument(tmb,p, alias[getArg(sig,i)]);
	}
	tsig = p;

	if ( !octFullRepl )
		conn = OCTinitcode(cntxt, tmb);

	for ( i = 1; i < mb->stop; i++ ){	/* copy all instr of cluster tidx or -1 */
		p = mb->stmt[i];
		if (p->token == ENDsymbol){
			last = i;
			break;
		}
		if ( i > ocl->top )
			continue;
		if ( ! memb(cl[getArg(p,0)], tidx) )
			continue;
		if (getModuleId(p) == sqlRef && getFunctionId(p) == mvcRef 
			&& !octFullRepl)
			continue;
		tp = copyInstruction(p);
		for ( j = 0; j< p->argc; j++){
			int a = getArg(p,j);
			if ( alias[a]==0 ) 
				alias[a] = cloneVariable(tmb,mb,a);
			getArg(tp,j) = alias[a];
			setVarUDFtype(tmb, alias[a]);
		}
		if ( !octFullRepl ) {
		if (getModuleId(p) == sqlRef &&
			strcmp(getFunctionId(p), "getVariable") == 0 ){
			getModuleId(tp) = octopusRef;
			tp= setArgument(tmb,tp,1,conn);
			setVarUDFtype(tmb,getArg(tp,0));
		} else
		if (getModuleId(p) == sqlRef &&
			( getFunctionId(p) == bindRef ||
			  getFunctionId(p) == bindidxRef ||
			  getFunctionId(p) == binddbatRef )){
			setModuleId(tp, octopusRef);
			setArgType(tmb,tp,0, getArgType(mb,p,0));
			setVarUDFtype(tmb,getArg(tp,0));
			getArg(tp,1) = conn;
			tp = pushArgument(tmb, tp, getArg(tsig,tsig->retc));
		} 
		}
		if (getModuleId(p) == algebraRef &&
			( getFunctionId(p) == joinPathRef ||
			  getFunctionId(p) == leftjoinPathRef )){
			setArgType(tmb,tp,0, getJoinPathType(mb,p));
			setVarUDFtype(tmb,getArg(tp,0));
		} 
		pushInstruction(tmb, tp);
	}  

	if ( !octFullRepl ) {
	/* exeption block */
	newCatchStmt(tmb, "ANYexception");
	tp = newStmt(tmb, remoteRef, disconnectRef);
	pushArgument(tmb, tp, conn);
	newRaiseStmt(tmb, "ANYexception");
	newExitStmt(tmb, "ANYexception");

	tp = newStmt(tmb, remoteRef, disconnectRef);
	pushArgument(tmb, tp, conn);
	}

	/* return stmt */
	tp = newReturnStmt(tmb);
	getArg(tp,0) = getArg(tsig,0);
	for ( i = 1; i < tsig->retc; i++)
		tp = pushReturn(tmb, tp, getArg(tsig,i));
	for ( i = 0; i < ocl->retcnt; i++)
		tp = pushArgument(tmb, tp, alias[ocl->ret[i]]);

	pushEndInstruction(tmb);
	tp = newStmt(tmb, optimizerRef, putName("deadcode", 8));
	tp = pushStr(tmb, tp, octopusRef);
	tp = pushStr(tmb, tp, getFunctionId(tsig));
	tp = newStmt(tmb, optimizerRef, putName("aliases", 7));
	tp = pushStr(tmb, tp, octopusRef);
	tp = pushStr(tmb, tp, getFunctionId(tsig));

	/*
	 * The tentacle code should be optimized by the remaining optimizers too.
	 */
	for (i = last + 1; i < mb->stop; i++){
		p = mb->stmt[i];
		if ( p->token != REMsymbol) {
			if (getModuleId(p) == optimizerRef &&
				getFunctionId(p) == putName("reduce", 6)) {
				tp = newStmt(tmb, optimizerRef, putName("recycle", 7));
				tp = pushStr(tmb, tp, octopusRef);
				tp = pushStr(tmb, tp, getFunctionId(tsig));
			}
			tp = newStmt(tmb, getModuleId(p), getFunctionId(p));
			tp = pushStr(tmb, tp, octopusRef);
			tp = pushStr(tmb, tp, getFunctionId(tsig));
		}
	}

	insertSymbol(findModule(cntxt->nspace,octopusRef),s);
	clrDeclarations(tmb);
	chkProgram(cntxt->fdout, cntxt->nspace, tmb);
	OPTDEBUGoctopus{
	printFunction(cntxt->fdout, tmb, 0, LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS);
	/*		printFunction(cntxt->fdout, tmb, 0, LIST_MAL_ALL); */
	}
	GDKfree(alias);

	return tmb;
}

/* create the parallel block in mb */
static void
OCTnewOctBlk(MalBlkPtr mb, InstrPtr *old, int v2)
{
	InstrPtr q, r, sig = old[0];
	int i, j, rexit, tcnt = octClCnt-1, tpe, bs;
	int *wnm=NULL,*tnm=NULL, *wvar=NULL;
	char buf[BUFSIZ];
	ValRecord cst;
	MalPartPtr ocl;
	int **bid=NULL;		/* array index to bids variables */
	int **res=NULL;	/* array index to result variables */
	str name2, bname;

	/* generate string constants holding worker and tentacle names,
	and variables to hold scheduled nodes and results */
	cst.vtype = TYPE_str;
			
	wnm = (int*) GDKzalloc(sizeof(int) * tcnt);
	wvar = (int*) GDKzalloc(sizeof(int) * tcnt);
	tnm = (int*) GDKzalloc(sizeof(int) * tcnt);
	bid = (int**) GDKzalloc(sizeof(int*) * tcnt);
	res = (int**) GDKzalloc(sizeof(int*) * tcnt);

	for ( j= 0; j < tcnt; j++){
		snprintf(buf,BUFSIZ,"worker_%d",j); 
		cst.val.sval= GDKstrdup(buf);
		cst.len= (int) strlen(cst.val.sval);
		wnm[j] = defConstant(mb, TYPE_str, &cst);
		wvar[j] = newVariable(mb,GDKstrdup(buf),TYPE_str);	

		snprintf(buf,BUFSIZ,"tentacle_"LLFMT"_%d", mb->legid,j+1); 
		cst.val.sval= GDKstrdup(buf);
		cst.len= (int) strlen(cst.val.sval);
		tnm[j] = defConstant(mb, TYPE_str, &cst);
		bid[j] = (int*) GDKzalloc(sizeof(int) * tcnt);
			
		ocl = &octCluster[j+1];
		res[j] = (int*) GDKzalloc(sizeof(int) * ocl->retcnt);
		for ( i = 0; i < ocl->retcnt; i++){
			snprintf(buf,BUFSIZ,"res_%d_%d",j+1,i); 
			res[j][i] = newVariable(mb,GDKstrdup(buf),getVarType(mb,ocl->ret[i]));	
		}
	}

	/* Generate register block */
	r = newStmt2(mb,schedulerRef,registerRef);
	r->barrier = BARRIERsymbol;
	setArgType(mb,r,0, TYPE_bit);
	rexit = getArg(r,0);
			
	for ( i= 0; i < tcnt; i++){
		r = newFcnCall(mb,octopusRef, registerRef);
		r = pushArgument(mb,r,wnm[i]);
		for ( j= 0; j < tcnt; j++)
			r = pushArgument(mb,r,tnm[j]);
	}

	r = newAssignment(mb);
	r->barrier= EXITsymbol;
	getArg(r,0) = rexit;

	/* Generate bidding block */
	for ( i = 0; i < tcnt; i++){
		r = newAssignment(mb);
		setArgType(mb, r, 0, TYPE_lng);
		bid[0][i] = getArg(r, 0);
		for ( j = 1; j < tcnt; j++){
			bid[j][i] = newTmpVariable(mb, TYPE_lng);
			r = pushArgument(mb,r,bid[j][i]);
		}
		for ( j = 0; j < tcnt; j++)
			r = pushLng(mb,r,(lng)-1);
		r->retc = tcnt; 
	}
	r = newStmt2(mb,schedulerRef,putName("bidding",7));
	r->barrier = BARRIERsymbol;
	setArgType(mb,r,0, TYPE_bit);
	rexit = getArg(r,0);

	cst.vtype = TYPE_sht;
	cst.val.shval = bidStrategy;
	bs = defConstant(mb, TYPE_sht, &cst);
	snprintf(buf,BUFSIZ,"getBid%d", tcnt);
	bname = putName(buf,strlen(buf));
	for ( i = 0; i < tcnt; i++){
		r = newStmt2(mb,octopusRef, bname);
		getArg(r,0) = bid[0][i];
		setVarUDFtype(mb,getArg(r,0));
		for ( j = 1; j < tcnt; j++){
			r = pushReturn(mb,r,bid[j][i]);
			setVarUDFtype(mb,getArg(r,j));
		}
		r = pushArgument(mb,r,wnm[i]);
		r = pushArgument(mb,r,bs);
		for ( j = 0; j < tcnt; j++)
			r = pushArgument(mb,r,tnm[j]);
	}
	r = newAssignment(mb);
	r->barrier= EXITsymbol;
	getArg(r,0) = rexit;

	/* Generate call to the scheduler */
	r = newStmt2(mb, schedulerRef, putName("makeSchedule", 12));
	setArgType(mb, r, 0, TYPE_str);
	getArg(r, 0) = wvar[0];
	setVarUDFtype(mb, getArg(r, 0));
	for (j = 1; j < tcnt; j++)
	r = pushReturn(mb, r, wvar[j]);

	r = pushInt(mb, r, tcnt);
	for (j = 0; j < tcnt; j++)
		for (i = 0; i < tcnt; i++)
			r = pushArgument(mb, r, bid[j][i]);

	/* Execution block */
	/* Initialize result variables */
	for (j = 0; j < tcnt; j++) {
		ocl = &octCluster[j+1];
		for ( i = 0; i < ocl->retcnt; i++){
			tpe = getVarType(mb,res[j][i]);
			if (isaBatType(tpe)) {
				r = newFcnCall(mb, batRef, newRef);
				r = pushType(mb, r, getHeadType(tpe));
				r = pushType(mb, r, getTailType(tpe));
			} else {
				r = newAssignment(mb);
				r = pushNil(mb, r, tpe);
			}
			getArg(r,0) = res[j][i];
		}
	}

	/* barrier (go,version):= scheduler.octopus(timeout); */
	q = newStmt(mb, octopusRef, putName("getVersion",10));
	setDestVar(q,v2);

	q = newStmt(mb, schedulerRef, octopusRef);
	setArgType(mb, q, 0, TYPE_bit);
	/*	pushReturn(mb, q, v2);*/
	q = pushInt(mb, q, 10);
	q->barrier = BARRIERsymbol;
	rexit = getArg(q,0);

	for (j = 0; j < tcnt; j++){		/* generate tentacle CALLs: every tentacle is called once */
		snprintf(buf,BUFSIZ,"exec_"LLFMT"_%d", mb->legid, j+1);
		name2 =  putName(buf,strlen(buf));
		q= newStmt2(mb,octopusRef, name2);
		getArg(q,0) = res[j][0];
		setVarUDFtype(mb,getArg(q,0));
		for ( i = 1; i < octCluster[j+1].retcnt; i++)
			q = pushReturn(mb, q, res[j][i]);
		q= pushArgument(mb,q,wvar[j]);
		q= pushArgument(mb,q,tnm[j]);
		q= pushArgument(mb,q,v2);

		for ( i= sig->retc; i <  sig->argc; i++)
			q = pushArgument(mb,q,getArg(sig, i));
	}
	/* exit c; */
	q = newAssignment(mb);
	q->barrier = EXITsymbol;
	getArg(q, 0) = rexit;

	GDKfree(wnm);
	GDKfree(wvar);
	GDKfree(tnm);
	for ( j= 0; j < tcnt; j++){
		GDKfree(bid[j]);
		GDKfree(res[j]);
	}
}

/* create the bidding function if needed */
static void
OCTnewBidding(Client cntxt, int tcnt)
{
	InstrPtr q, sig;
	MalBlkPtr sm;
	char buf[BUFSIZ];
	Symbol s;
	int i, k, arg[128], lres[128], rres[128];
	int conn, dbvar, bvar, rbvar, bexit;

	snprintf(buf,BUFSIZ,"getBid%d", tcnt);

    s = findSymbol(cntxt->nspace, putName("octopus",7), buf);
	if ( s )	/* bidding function already defined */
		return;
	s = newFunction(octopusRef, putName(buf,strlen(buf)), FUNCTIONsymbol);
	sm = s->def;

	q = getInstrPtr(sm,0);

	k = getArg(q,0);
	setVarType(sm,k,TYPE_lng);
	setVarUDFtype(sm,k);
	if ( tcnt > 1 ) {
		if ( sm->var[k]->name ) 
			GDKfree(sm->var[k]->name);
		sm->var[k]->name = GDKstrdup("res0");
		for (i = 1; i < tcnt; i++ ){
			snprintf(buf,BUFSIZ,"res%d", i);
			k = newVariable(sm,GDKstrdup(buf),TYPE_lng);
			setVarUDFtype(sm,k);
			q = pushReturn(sm,q,k);
		}
	}
	dbvar= newVariable(sm,GDKstrdup("dbname"),TYPE_str);
	bvar = newVariable(sm,GDKstrdup("bidtype"),TYPE_sht);
	q= pushArgument(sm,q,dbvar);
	q= pushArgument(sm,q,bvar);
	/* add all tentacle names */
	for ( i = 0; i < tcnt; i++){
		snprintf(buf,BUFSIZ,"fn%d", i);
		k = newVariable(sm,GDKstrdup(buf),TYPE_str);
		q = pushArgument(sm,q,k);
	}
	sig = q;
	
	/* initialization block */
	q = newAssignment(sm);
	setArgType(sm, q, 0, TYPE_lng);
	k = lres[0] = getArg(q, 0);
	if ( sm->var[k]->name ) 
		GDKfree(sm->var[k]->name);
	sm->var[k]->name = GDKstrdup("lres0");
	for ( i = 1; i < tcnt; i++){
		snprintf(buf,BUFSIZ,"lres%d", i);
		lres[i]= newVariable(sm,GDKstrdup(buf),TYPE_lng);
		q = pushReturn(sm,q,lres[i]);
	}
	for ( i = 0; i < tcnt; i++)
		q = pushLng(sm,q,(lng)-1);
		
	/* barrier remotewrk := calc.!=(dbname,"NOTworker"); */
	q = newFcnCall(sm,calcRef,putName("!=",2));
	q->barrier = BARRIERsymbol;
	bexit = getArg(q,0) = newVariable(sm,GDKstrdup("remotewrk"),TYPE_bit);	
	q = pushArgument(sm,q,dbvar);
	q = pushStr(sm,q,"NOTworker"); 

	q = newStmt(sm, octopusRef,connectRef);
	conn= getArg(q,0);
	setVarUDFtype(sm,conn);
	q = pushArgument(sm, q, dbvar);

	/* x:= remote.put(conn,...)  for each argument*/
	q = newFcnCall(sm,remoteRef,putRef);
	setArgType(sm,q,0,TYPE_str);
	rbvar= getArg(q,0);
	pushArgument(sm,q,conn);
	pushArgument(sm,q,bvar);

	for (i= 0; i< tcnt; i++){
		q = newFcnCall(sm,remoteRef,putRef);
		setArgType(sm,q,0,TYPE_str);
		arg[i]= getArg(q,0);
		pushArgument(sm,q,conn);
		pushArgument(sm,q,getArg(sig,sig->retc + 2 + i));
	}

	/* k:= remote.put(conn,kvar) for each result */
	for (i = 0; i < tcnt; i++ ){
		q= newFcnCall(sm,remoteRef,putRef);
		setArgType(sm,q,0,TYPE_str);
		rres[i]= getArg(q,0);
		q= pushArgument(sm,q,conn);
		q= pushArgument(sm,q, lres[i]);
	}

	/* k:= remote.exec(conn,"trader","makeBids",bidtype,fn1, ...) */
	q = newFcnCall(sm,remoteRef,execRef);
	getArg(q,0)= rres[0];
	for (i = 1; i < sig->retc; i++ )
		q= pushReturn(sm,q,rres[i]);
	q= pushArgument(sm,q,conn);
	q= pushStr(sm,q,putName("trader",6));
	q= pushStr(sm,q,putName("makeBids",8));
	q= pushArgument(sm,q,rbvar);
	for (i= 0; i< tcnt; i++)
		q = pushArgument(sm,q,arg[i]);

	/* l:=remote.get(conn,k) */
	for (i = 0; i < sig->retc; i++ ){
		q= newFcnCall(sm,remoteRef,getRef);
		q= pushArgument(sm,q,conn);
		q= pushArgument(sm,q,rres[i]);
		getArg(q,0) = lres[i];
	}

	/* catch and propagate errors */
	newCatchStmt(sm, "ANYexception");
	newRaiseStmt(sm,"ANYexception");
	newExitStmt(sm, "ANYexception");

	/* exit */
	q = newAssignment(sm);
	q->barrier= EXITsymbol;
	getArg(q,0) = bexit;

	/* return (res0, ...) := (lres0, ...); */
	q = newReturnStmt(sm);
	getArg(q,0)= getArg(sig,0);
	for (i = 1; i < sig->retc; i++ )
		q = pushReturn(sm,q,getArg(sig,i));
	for (i = 0; i < sig->retc; i++ )
		q = pushArgument(sm,q,lres[i]);

	pushEndInstruction(sm);
	insertSymbol(findModule(cntxt->nspace,octopusRef),s);
	clrDeclarations(sm);
	chkProgram(cntxt->fdout, cntxt->nspace, sm);
	OPTDEBUGoctopus{
		/*		printFunction(cntxt->fdout, sm, 0, LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS ); */
	}
}

/* create the exec function for the tentacles */
static void
OCTnewExec(Client cntxt, MalBlkPtr mb, MalBlkPtr t, int tno)
{
	InstrPtr q, tsig = t->stmt[0], sig;
	MalBlkPtr sm;
	char buf[BUFSIZ];
	Symbol s;
	int i, ai, l, tpe, arg[1024], res[1024], rres[1024], lres[1024];
	int conn, dbvar,qvar;

	snprintf(buf,BUFSIZ,"exec_"LLFMT"_%d", mb->legid,tno);
	s = newFunction(octopusRef, putName(buf,strlen(buf)), FUNCTIONsymbol);
	sm = s->def;

	q = getInstrPtr(sm,0);

	res[0] = l = getArg(q,0);
	setVarType(sm,l,getArgType(t,tsig,0));
	setVarUDFtype(sm,l);
	if ( sm->var[l]->name )
		GDKfree(sm->var[l]->name);
	sm->var[l]->name = GDKstrdup("res0");
	for (i = 1; i < tsig->retc; i++ ){
		snprintf(buf,BUFSIZ,"res%d", i);
		tpe = getArgType(t, tsig,i);
		res[i]= newVariable(sm,GDKstrdup(buf),tpe);
		setVarUDFtype(sm,res[i]);
		q = pushReturn(sm,q,res[i]);
	}

	dbvar= newVariable(sm,GDKstrdup("dbname"),TYPE_str);
	qvar = newVariable(sm,GDKstrdup("query"),TYPE_str);
	q= pushArgument(sm,q,dbvar);
	q= pushArgument(sm,q,qvar);
	/* add all tentacle arguments */
	for ( i = tsig->retc; i < tsig->argc; i++){
		ai = cloneVariable(sm,t,getArg(tsig,i));
		q = pushArgument(sm,q,ai);
	}

	/* initialization block */
	sig = sm->stmt[0];
	for (i = 0; i < sig->retc; i++ ){
		tpe = getVarType(sm, getArg(sig,i)); 
		if (isaBatType(tpe)) {                /* exec_qry:= bat.new(:htp,:ttp); */
			q = newFcnCall(sm, batRef, newRef);
			q = pushType(sm, q, getHeadType(tpe));
			q = pushType(sm, q, getTailType(tpe));
		}else  {                        /* exec_qry:= nil:tp; */
			q = newAssignment(sm);
			q = pushNil(sm, q, tpe);
		}
		getArg(q, 0) = getArg(sig, i);
	}
		
	conn = newVariable(sm,GDKstrdup("conn"),TYPE_str);
	q = newStmt(sm, octopusRef,connectRef);
	getArg(q,0) = conn;
	setVarUDFtype(sm,conn);
	q = pushArgument(sm, q, dbvar);

	/* x:= remote.put(conn,...)  for each argument*/
	assert(sig->argc <1024);
	for (i= sig->retc+2; i< sig->argc; i++){
		snprintf(buf,BUFSIZ,"arg%d", i);
		l= newVariable(sm,GDKstrdup(buf),TYPE_str);
		q= newFcnCall(sm,remoteRef,putRef);
		arg[i]= getArg(q,0)= l;
		pushArgument(sm,q, conn);
		pushArgument(sm,q,getArg(sig,i));
	}

	/* k:= remote.put(conn,kvar) for each result */
	for (i = 0; i < sig->retc; i++ ){
		snprintf(buf,BUFSIZ,"rres%d", i);
		l= newVariable(sm,GDKstrdup(buf),TYPE_str);
		q= newFcnCall(sm,remoteRef,putRef);
		rres[i]= getArg(q,0)= l;
		q= pushArgument(sm,q,conn);
		q= pushArgument(sm,q, getArg(sig,i));
		setVarUDFtype(sm,getArg(q,q->argc-1));
	}

	/* k:= remote.exec(conn,octopus,qry,version....) */
	q= newFcnCall(sm,remoteRef,execRef);
	getArg(q,0)= rres[0];
	for (i = 1; i < sig->retc; i++ )
		q= pushReturn(sm,q,rres[i]);
	q= pushArgument(sm,q,conn);
	q= pushStr(sm,q,octopusRef);
	q= pushArgument(sm,q,qvar);
	for (i= sig->retc+2; i< sig->argc; i++)
		q = pushArgument(sm,q,arg[i]);

	/* l:=remote.get(conn,k) */
	for (i = 0; i < sig->retc; i++ ){
		q= newFcnCall(sm,remoteRef,getRef);
		q= pushArgument(sm,q,conn);
		q= pushArgument(sm,q,rres[i]);
		snprintf(buf,BUFSIZ,"lres%d", i);
		l= newVariable(sm,GDKstrdup(buf),getArgType(sm,sig,i));
		lres[i] = getArg(q,0)= l;
		setVarUDFtype(sm,lres[i]);
	}

	/* catch and propagate errors */
	newCatchStmt(sm, "ANYexception");
	newRaiseStmt(sm,"ANYexception");
	newExitStmt(sm, "ANYexception");

	/* return exec_qry; */
	q= newReturnStmt(sm);
	getArg(q,0)= res[0];
	for (i = 1; i < sig->retc; i++ )
		q = pushReturn(sm,q,res[i]);
	for (i = 0; i < sig->retc; i++ )
		q = pushArgument(sm,q,lres[i]);

	pushEndInstruction(sm);
	insertSymbol(findModule(cntxt->nspace,octopusRef),s);
	clrDeclarations(sm);
	chkProgram(cntxt->fdout, cntxt->nspace,sm);
	OPTDEBUGoctopus{
				printFunction(cntxt->fdout, sm, 0, LIST_MAL_STMT | LIST_MAL_UDF | LIST_MAL_PROPS );
	}
}

int
OPToctopusImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int i, j, k, m, limit, cl, last, v2, z, v;
	int update=0, autocommit=0, actions=0, target = -1, varadd = 0;
	InstrPtr p, *old, sig, q, *pref = NULL;
	bte *set = NULL, *bnd = NULL;
	int *malPart = NULL, *alias = NULL, *src = NULL;
	int pn, np;
	str tnm, tblname = NULL;
	char rname[BUFSIZ];
	MalBlkPtr *tentacle = NULL;

	(void) stk;

	optDebug |= 1 << DEBUG_OPT_OCTOPUS;
	OPTDEBUGoctopus{ 
		mnstr_printf(cntxt->fdout, "#Octopus optimizer called\n");
		chkProgram(cntxt->fdout, cntxt->nspace,mb);  
		/*		printFunction(cntxt->fdout, mb, 0, LIST_MAL_STMT | LIST_MAL_TYPE | LIST_MAPI);*/
	}

	(void) fixModule(cntxt->nspace,octopusRef);
	old = mb->stmt;
	limit = mb->stop;

	autocommit = varGetProp(mb, getArg(old[0],0), PropertyIndex("autoCommit")) != NULL;
	for (i = 1; i < limit; i++){
		p = old[i];		
		if( getModuleId(p)== sqlRef ){
			update |= getFunctionId(p)== appendRef || getFunctionId(p)== deleteRef;
			update |= strcmp(getFunctionId(p),"getVariable")==0;
			update |= strcmp(getFunctionId(p),"setVariable")==0;
		}
	}
	/* we do not  support yet update operations in the octopus */
	if ( update || autocommit==0 ) 
		return 0;

	/* find table leading the split */
	for (i = 1; i < limit; i++) {
		p = old[i];
		if ( isBindInstr(p) && p->argc>=7 ){
			tblname = GDKstrdup((str) getVarValue(mb,getArg(p,3)));
			break;
		}
	}
	if ( !tblname )
		return 0;

	mb->legid = octopusSeq++;

	/* exclude variable reuse */
	alias = (int*) GDKzalloc(mb->vtop * sizeof(int));
	set = (bte*) GDKzalloc(mb->vtop);
	for (i = 0; i < mb->vtop; i++) 
		alias[i] = i;
	
	for (i = 1; i < limit; i++) {
		p = old[i];
		if ( varadd )
            for( j = p->retc; j < p->argc; j++ ){
				z = getArg(p,j);
				getArg(p,j) = alias[z];
			}
		if ( p->barrier == EXITsymbol ) { /* exits use the same variable */
			if ( varadd )
				getArg(p,0) = alias[getArg(p,0)];
			continue;
		}
		for( j = 0; j < p->retc; j++ ){
			z = getArg(p,j);
			if ( set[z] ){
				alias[z] = cloneVariable(mb,mb,z);
				getArg(p,j) = alias[z];
				varadd++;
			}
			else set[z] = 1;
		}
	}
	GDKfree(alias);
	GDKfree(set);

	/* create cluster 0 for instructions to be executed at the head */
	OCTinitMalPart();
	OCTgetMalPart("","",1, 1);

	malPart = (int*) GDKzalloc(mb->vtop * sizeof(int)); /* mask for cluster inclusion */
	memset((char *) malPart,~(char)0,mb->vtop * sizeof(int));
	bnd = (bte*) GDKzalloc(mb->vtop);
	src = (int*) GDKzalloc(mb->vtop * sizeof(int));
	pref = (InstrPtr*) GDKzalloc(mb->stop * sizeof(InstrPtr));


	/* analysis and clustering of instructions */
	for (i = 1; i < limit; i++) {
		p = old[i];
		src[getArg(p,0)] = i;
		/* bind instructions over pieces of the largest table become cluster cores */
		if ( isBindInstr(p) ){
			bnd[getArg(p,0)] = 1;
			if ( p->argc >= 7 ){
				tnm = (str) getVarValue(mb,getArg(p,3));
				pn = *(int*) getVarValue(mb,getArg(p,6));
				np = *(int*) getVarValue(mb,getArg(p,7));
				malPart[getArg(p,0)] = (int)1 << OCTgetMalPart("",tnm,pn,np);
				continue;
			}
		}
		if ( isBindInstr(p) && p->argc == 6 ){
			/*	tnm = (str) getVarValue(mb,getArg(p,3));*/
			malPart[getArg(p,0)] = ~(int)0;
			/*			if ( strcmp(tnm,tblname) )
				malPart[getArg(p,0)] = ~(int)0;
			else
			malPart[getArg(p,0)] = 1;*/
			continue;
		}

		/* check partitions associated to arguments */
		cl = ~(int)0;
		for ( j = p->retc; j < p->argc; j++) {
			if ( j == p->retc ) 
				cl = malPart[getArg(p,j)] ;
			else cl = cl &  malPart[getArg(p,j)];
		}

		if ( cl )			/* assign to the arguments cluster */
			malPart[getArg(p,0)] = cl;	

		else {		/* combines arguments from different clusters */
			malPart[getArg(p,0)] = 1;	/* assign to cluster 1 */
			for ( j = p->retc; j < p->argc; j++) 
				if ( !(malPart[getArg(p,j)] & malPart[getArg(p,0)]) ){
				/* arguments coming from another cluster should be either added as cluster result or added to cluster 1 */
					v = getArg(p,j);
					q = getInstrPtr(mb, src[v]);

					/* special case of projection join */
					if ( getModuleId(q) == algebraRef &&
						getFunctionId(q) == leftjoinRef &&
						bnd[getArg(q,2)]){
						malPart[v] =  malPart[v] | (int)1; /* add join to cluster 1*/
						if ( target < 0 || src[v] < target )
							target = src[v];
						v = getArg(q,2);	/* add bind to cluster 1 */
						malPart[v] = malPart[v] | (int) 1;
						v = getArg(q,1); /* 1 arg. should be added as cluster res. */
						q = getInstrPtr(mb, src[v]);
					}

					/* don't materialize reverse, mark, mirror */
					k = 0;
					while ( isAView(q) ){
						pref[k++] = q;
						v = getArg(q,1);
						q = getInstrPtr(mb, src[v]);
					}
					k--;
					if ( bnd[v] ) {/* add bind instr. to cluster 1 */
						malPart[v] = malPart[v] | (int) 1;
						for ( ; k >= 0; k--){
							v = getArg(pref[k],0);
							malPart[v] = malPart[v] | (int) 1;
						}
					}
					else {			/* extend cluster results */
						for ( m = 0; m < q->retc; m++){
							v = getArg(q,m);
							cl = OCTgetCluster(malPart[v]);
							if ( cl > 0 )
								OCTaddResult(cl, v, src[v]);
						}
						for ( ; k >= 0; k--){
							v = getArg(pref[k],0);
							malPart[v] = malPart[v] | (int) 1;
							if ( target < 0 || src[v] < target )
								target = src[v];
						}
					}
			}
			if (target < 0 ) /* place of octopus block is before 1st comb. instruction */
				target = i;
		}

	} /* for */

	GDKfree(bnd);
	GDKfree(src);
	GDKfree(pref);

	/* print mal block annotated with partitions */
	OPTDEBUGoctopus{
		for (i = 0; i < limit; i++) {
			p = old[i];
			mnstr_printf(cntxt->fdout, "%3d\t", malPart[getArg(p,0)]); 
			printInstruction(cntxt->fdout,mb,0,p, LIST_MAL_STMT);
		}
		for (i = 0; i < octClCnt; i++){
			mnstr_printf(cntxt->fdout, "Cluster %3d\n", i);
			k = 0;
			if (octCluster[i].retcnt > 0 )
				for ( j = 0; j< octCluster[i].retcnt; j++ ){
					int v = octCluster[i].ret[j];
					wrd vrows = getVarRows(mb,v);
					if ( vrows > 0 )
						octCluster[i].rows += vrows;
					else k++;
					mnstr_printf(cntxt->fdout, "%3d\t", v);
				}
			mnstr_printf(cntxt->fdout, "\nIntermediate size "LLFMT" tuples\n\n", (lng) octCluster[i].rows);
			if ( k )
				mnstr_printf(cntxt->fdout, "No estimate for %d results\n", k);
		}
	}

	/* check cluster results */
	for (i = 1; i < octClCnt; i++){
		if ( octCluster[i].retcnt == 0 ){
			mnstr_printf(cntxt->fdout, "Tentacle %d without result\n", i); 
			goto cleanup;
		}
	}

	/* create tentacles */

	tentacle= (MalBlkPtr *) GDKmalloc(sizeof(MalBlkPtr) * MAXSLICES);
	if ( tentacle == NULL)
		goto cleanup;

	v2 = newVariable(mb, GDKstrdup("version"), TYPE_int);
	for (i = 1; i < octClCnt; i++){
		tentacle[i] = OCTnewTentacle(cntxt, mb,i, v2, malPart);
		OCTnewExec(cntxt, mb, tentacle[i], i);
		actions++;
	}

	if ( !actions )
		goto cleanup;

	OCTnewBidding(cntxt, octClCnt-1);

	/* modify plan at the head */
	sig = old[0];

	if ( newMalBlkStmt(mb, mb->ssize) < 0)
		goto cleanup;

	pushInstruction(mb, sig); 

	last = limit;
	for (i = 1; i < limit; i++) {	/* copy all instructions of cluster 0 and -1 */	
		p = old[i];
		if (p->token == ENDsymbol){
			last = i;
			pushEndInstruction(mb);
			break;
		}
		if ( memb(malPart[getArg(p,0)],0) ){
		
			if (target == i)
				OCTnewOctBlk(mb, old, v2);

			/*	 instruction combines partitions */
			if ( malPart[getArg(p,0)] > 0 ){ 
	
				/* replace arguments with returns from octopus block */
				for ( j = p->retc; j < p->argc; j++) 
					/*					if ( !(malPart[getArg(p,0)] & malPart[getArg(p,j)]) ){*/
					if ( malPart[getArg(p,j)] > 1 && !memb(malPart[getArg(p,j)],0) ){
						cl = OCTgetCluster(malPart[getArg(p,j)]);
						if ( cl > 0 ) {
							k = 0;		
							while ( k < octCluster[cl].retcnt && 
								octCluster[cl].ret[k] != getArg(p,j) ) k++;
							if ( k < octCluster[cl].retcnt) {
								snprintf(rname,BUFSIZ,"res_%d_%d",cl,k); 
								getArg(p,j) = findVariable (mb, rname);
							}
							else 
								mnstr_printf(cntxt->fdout, "mat.pack argument %2d outside cluster %d\n", getArg(p,j),cl);
					}
				}
			}
			pushInstruction(mb, p);
		}
	}
	
	for (i = last + 1; i < limit; i++){
		if ( old[i] == pci){
			freeInstruction(pci);
			old[i]= 0;
			/*			p = newStmt(mb, optimizerRef, putName("deadcode", 8));*/
			continue;
		}
		pushInstruction(mb, old[i]);
	}	

	clrDeclarations(mb);
	OPTDEBUGoctopus{
		chkProgram(cntxt->fdout, cntxt->nspace,mb); 
		/*		printFunction(cntxt->fdout, mb, 0,  LIST_MAL_STMT | LIST_MAL_TYPE | LIST_MAPI);*/
	}

	GDKfree(old);

cleanup:
	GDKfree(malPart);
	if (tentacle)
		GDKfree(tentacle);
	if ( octClCnt ){
		for (i = 0; i < octClCnt; i++){
			GDKfree(octCluster[i].sch);
			GDKfree(octCluster[i].tab);
			GDKfree(octCluster[i].ret);
		}
		GDKfree(octCluster);
		octCluster = NULL;
		octClCnt = 0;
	}
	return actions;
}


/*
 * The remainder contains the octopus support routines.
 * The legAdviceInternal function has to negotiate with merovingian to
 * determine the number of legs to use for a specific query plan.
 * This may involve the size and structure of the database tables
 * accessed as well.
 * Furthermore, the advice should only be given when the octopus
 * optimizer is enabled.
 */
int 
OPTlegAdviceInternal(MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	bat bid = 0;
	BAT *b;
	char buf[BUFSIZ]= "*/octopus", *s= buf;
	str msg = MAL_SUCCEED;

	(void) stk;
	(void) pci;
	if ( isOptimizerEnabled(mb,octopusRef) ){

		msg = RMTresolve(&bid,&s);
		if ( msg == MAL_SUCCEED) {
			b = BBPquickdesc(bid, FALSE);
			if ( b != NULL && BATcount(b) > 0 )
				return (int) BATcount(b);
		}
		/*	return GDKnr_threads;*/
	}
	return -1;
}

str 
OPTlegAdvice(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);

	(void) cntxt;
	*ret = OPTlegAdviceInternal(mb,stk,pci);
	return MAL_SUCCEED;
}
