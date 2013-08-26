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
#include "monetdb_config.h"
#include "opt_querylog.h"
#include "querylog.h"

int 
OPTquerylogImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int i, limit, slimit;
	InstrPtr p = 0, *old= mb->stmt, q,r;
	int argc, io, user,nice,sys,idle,iowait,load, arg, start,finish, name;
	int xtime=0, rtime = 0, space =0, tuples=0;
	InstrPtr defineQuery = NULL;


	// query log needed?
	if ( !QLOGisset() )
		return 0;
	(void) pci;
	(void) stk;		/* to fool compilers */
	(void) cntxt;
	/* gather information */
	for (i = 1; i < mb->stop; i++) {
		p = getInstrPtr(mb,i);
		if ( getModuleId(p) && idcmp(getModuleId(p), "querylog") == 0 && idcmp(getFunctionId(p),"define")==0){
			defineQuery= p;
			getVarConstant(mb,getArg(p,3)).val.lval = GDKusec()-getVarConstant(mb,getArg(p,3)).val.lval ;
		}
	}
	if ( defineQuery == NULL)
		/* nothing to do */
		return 0;

	limit= mb->stop;
	slimit= mb->ssize;
	if ( newMalBlkStmt(mb, 2 * mb->ssize) < 0)
		return 0; 

	pushInstruction(mb, old[0]);
	/* run the querylog.define operation */
	defineQuery = copyInstruction(defineQuery);
	defineQuery->token = ASSIGNsymbol;
	setModuleId(defineQuery,querylogRef);

	/* collect the initial statistics */
	q = newStmt(mb, "clients", "getUsername");
	name= getArg(q,0)= newVariable(mb,GDKstrdup("name"),TYPE_str);
	defineQuery = pushArgument(mb,defineQuery,name);
	q = newStmt(mb, "mtime", "current_timestamp");
	start= getArg(q,0)= newVariable(mb,GDKstrdup("start"),TYPE_any);
	defineQuery = pushArgument(mb,defineQuery,start);
	pushInstruction(mb, defineQuery);

	q = newStmt1(mb, sqlRef, "argRecord");
	for ( argc=1; argc < old[0]->argc; argc++)
		q = pushArgument(mb, q, getArg(old[0],argc));

	arg= getArg(q,0)= newVariable(mb,GDKstrdup("args"),TYPE_str);


	newFcnCall(mb,"profiler","setFootprintFlag");
	q = newStmt(mb, "alarm", "usec");
	xtime = getArg(q,0)= newVariable(mb,GDKstrdup("xtime"),TYPE_lng);
	user = newVariable(mb,GDKstrdup("user"),TYPE_lng);
	nice = newVariable(mb,GDKstrdup("nice"),TYPE_lng);
	sys = newVariable(mb,GDKstrdup("sys"),TYPE_lng);
	idle = newVariable(mb,GDKstrdup("idle"),TYPE_lng);
	iowait = newVariable(mb,GDKstrdup("iowait"),TYPE_lng);
	q = newStmt(mb, "profiler", "cpustats");
	q->retc= q->argc =0;
	q = pushReturn(mb,q,user);
	q = pushReturn(mb,q,nice);
	q = pushReturn(mb,q,sys);
	q = pushReturn(mb,q,idle);
	q = pushReturn(mb,q,iowait);
	q = newAssignment(mb);
	tuples= getArg(q,0) = newVariable(mb,GDKstrdup("tuples"),TYPE_wrd);
	q= pushWrd(mb,q,1);

	for (i = 1; i < limit; i++) {
		p = old[i];
		
		if (getModuleId(p)==sqlRef && 
			(idcmp(getFunctionId(p),"exportValue")==0 ||
			 idcmp(getFunctionId(p),"exportResult")==0  ) ) {

			q = newStmt(mb, "alarm", "usec");
			r = newStmt1(mb, calcRef, "-");
			r = pushArgument(mb, r, getArg(q,0));
			r = pushArgument(mb, r, xtime);
			getArg(r,0)=xtime;

			q = newStmt(mb, "alarm", "usec");
			rtime= getArg(q,0)= newVariable(mb,GDKstrdup("rtime"),TYPE_lng);
			pushInstruction(mb,p);
			continue;
		}
		if ( getModuleId(p) == sqlRef && idcmp(getFunctionId(p),"resultSet")==0  && isaBatType(getVarType(mb,getArg(p,3)))){
			q = newStmt(mb, "aggr", "count");
			getArg(q,0) = tuples;
			(void) pushArgument(mb,q, getArg(p,3));
			pushInstruction(mb,p);
			continue;
		}	
		if ( p->token== ENDsymbol || p->barrier == RETURNsymbol || p->barrier == YIELDsymbol){
			if ( rtime == 0){
				q = newStmt(mb, "alarm", "usec");
				r = newStmt1(mb, calcRef, "-");
				r = pushArgument(mb, r, getArg(q,0));
				r = pushArgument(mb, r, xtime);
				getArg(r,0)=xtime;
				q = newStmt(mb, "alarm", "usec");
				rtime= getArg(q,0)= newVariable(mb,GDKstrdup("rtime"),TYPE_lng);
			}
			q = newStmt(mb, "alarm", "usec");
			r = newStmt1(mb, calcRef, "-");
			r = pushArgument(mb, r, getArg(q,0));
			r = pushArgument(mb, r, rtime);
			getArg(r,0)=rtime;
			/*
			 * Post execution statistics gathering
			 */
			q = newStmt(mb, "mtime", "current_timestamp");
			finish= getArg(q,0)= newVariable(mb,GDKstrdup("finish"),TYPE_any);
			q = newStmt(mb, "profiler", "getFootprint");
			space= getArg(q,0)= newVariable(mb,GDKstrdup("space"),TYPE_lng);

			q = newStmt(mb, "profiler", "cpuload");
			load = newVariable(mb,GDKstrdup("load"),TYPE_int);
			getArg(q,0)= load;
			io = newVariable(mb,GDKstrdup("io"),TYPE_int);
			q= pushReturn(mb,q,io);
			q = pushArgument(mb,q,user);
			q = pushArgument(mb,q,nice);
			q = pushArgument(mb,q,sys);
			q = pushArgument(mb,q,idle);
			q = pushArgument(mb,q,iowait);

			q = newStmt(mb, querylogRef, "call");
			q = pushArgument(mb, q, start);
			q = pushArgument(mb, q, finish); 
			q = pushArgument(mb, q, arg);
			q = pushArgument(mb, q, tuples); 
			q = pushArgument(mb, q, xtime); 
			q = pushArgument(mb, q, rtime); 
			q = pushArgument(mb, q, load); 
			q = pushArgument(mb, q, io); 
			(void) pushArgument(mb, q, space); 
			pushInstruction(mb,p);
			continue;
		}

		pushInstruction(mb,p);
		if (p->barrier == YIELDsymbol){
			/* the factory yield may return */
			q = newStmt(mb, "mtime", "current_timestamp");
			start= getArg(q,0)= newVariable(mb,GDKstrdup("start"),TYPE_any);
			q = newStmt1(mb, sqlRef, "argRecord");
			for ( argc=1; argc < old[0]->argc; argc++)
				q = pushArgument(mb, q, getArg(old[0],argc));
			arg= getArg(q,0)= newVariable(mb,GDKstrdup("args"),TYPE_str);
			q = newAssignment(mb);
			q = pushLng(mb,q,0);
			q = newAssignment(mb);
			q = pushWrd(mb,q,0);
			tuples= getArg(q,0)= newVariable(mb,GDKstrdup("tuples"),TYPE_wrd);
			newFcnCall(mb,"profiler","setFootprintFlag");
			newFcnCall(mb,"profiler","setMemoryFlag");
			q->argc--;
			pushWrd(mb,q,1);
			q = newStmt(mb, "alarm", "usec");
			xtime = getArg(q,0)= newVariable(mb,GDKstrdup("xtime"),TYPE_lng);
		}
	}

	for( ; i<slimit; i++)
		if(old[i])
			freeInstruction(old[i]);
	GDKfree(old);
	return 1;
}
