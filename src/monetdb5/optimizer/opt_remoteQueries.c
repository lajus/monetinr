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
#include "opt_remoteQueries.h"
#include "mal_interpreter.h"	/* for showErrors() */
#include "mal_builder.h"
/*
 * The instruction sent is produced with a variation of call2str
 * from the debugger.
 */
static str
RQcall2str(MalBlkPtr mb, InstrPtr p)
{
	int k,len=1;
	str msg;
	str s,cv= NULL;

	msg = (str) GDKmalloc(BUFSIZ);
	if (msg == NULL)
		return NULL;
	msg[0]='#';
	msg[1]=0;
	if( p->barrier)
		strcat(msg, operatorName(p->barrier));
	
	if( p->retc > 1) strcat(msg,"(");
	len= (int) strlen(msg);
	for (k = 0; k < p->retc; k++) {
		VarPtr v = getVar(mb, getArg(p, k));
		if( isVarUDFtype(mb, getArg(p,k)) ){
			str tpe = getTypeName(getVarType(mb, getArg(p, k)));
			sprintf(msg+len, "%s:%s ", v->name, tpe);
			GDKfree(tpe);
		} else
		if (isTmpVar(mb, getArg(p,k)))
			sprintf(msg+len, "%c%d", REFMARKER, v->tmpindex);
		else
			sprintf(msg+len, "%s", v->name);
		if (k < p->retc - 1)
			strcat(msg,",");
		len= (int) strlen(msg);
	}
	if( p->retc > 1) strcat(msg,")");
	sprintf(msg+len,":= %s.%s(",getModuleId(p),getFunctionId(p));
	s = strchr(msg, '(');
	if (s) {
		s++;
		*s = 0;
		len = (int) strlen(msg);
		for (k = p->retc; k < p->argc; k++) {
			VarPtr v = getVar(mb, getArg(p, k));
			if( isVarConstant(mb, getArg(p,k)) ){
				if( v->type == TYPE_void) {
					sprintf(msg+len, "nil");
				} else {
					VALformat(&cv, &v->value);
					sprintf(msg+len,"%s:%s",cv, ATOMname(v->type));
					GDKfree(cv);
				}

			} else if (isTmpVar(mb, getArg(p,k)))
				sprintf(msg+len, "%c%d", REFMARKER, v->tmpindex);
			else
				sprintf(msg+len, "%s", v->name);
			if (k < p->argc - 1)
				strcat(msg,",");
			len= (int) strlen(msg);
		}
		strcat(msg,");");
	}
/* printf("#RQcall:%s\n",msg);*/
	return msg;
}
/*
 * The algorithm follows the common scheme used so far.
 * Instructions are taken out one-by-one and copied
 * to the new block.
 *
 * A local cache of connections is established, because
 * the statements related to a single remote database
 * should be executed in the same stack context.
 * A pitfall is to create multiple connections with
 * their isolated runtime environment.
 */
#define lookupServer(X)\
	/* lookup the server connection */\
	if( location[getArg(p,0)] == 0){\
		db = 0;\
		if( isVarConstant(mb,getArg(p,X)) )\
			db= getVarConstant(mb, getArg(p,X)).val.sval;\
		for(k=0; k<dbtop; k++)\
			if( strcmp(db, dbalias[k].dbname)== 0)\
				break;\
		\
		if( k== dbtop){\
			r= newInstruction(mb,ASSIGNsymbol);\
			getModuleId(r)= mapiRef;\
			getFunctionId(r)= lookupRef;\
			j= getArg(r,0)= newTmpVariable(mb, TYPE_int);\
			r= pushArgument(mb,r, getArg(p,X));\
			pushInstruction(mb,r);\
			dbalias[dbtop].dbhdl= j;\
			dbalias[dbtop++].dbname= db;\
			if( dbtop== 127) dbtop--;\
		} else j= dbalias[k].dbhdl;\
		location[getArg(p,0)]= j;\
	} else j= location[getArg(p,0)];

#define prepareRemote(X)\
	r= newInstruction(mb,ASSIGNsymbol);\
	getModuleId(r)= mapiRef;\
	getFunctionId(r)= rpcRef;\
	getArg(r,0)= newTmpVariable(mb, X);\
	r= pushArgument(mb,r,j);

#define putRemoteVariables()\
	for(j=p->retc; j<p->argc; j++)\
	if( location[getArg(p,j)] == 0 && !isVarConstant(mb,getArg(p,j)) ){\
		q= newStmt(mb,mapiRef,putRef);\
		getArg(q,0)= newTmpVariable(mb, TYPE_void);\
		q= pushArgument(mb,q,location[getArg(p,j)]);\
		q= pushStr(mb,q, getRefName(mb,getArg(p,j)));\
		(void) pushArgument(mb,q,getArg(p,j));\
	}

#define remoteAction()\
	s= RQcall2str(mb,p);\
	r= pushStr(mb,r,s+1);\
	GDKfree(s);\
	pushInstruction(mb,r);\
	freeInstruction(p);\
	doit++;

typedef struct{
	str dbname;
	int dbhdl;
} DBalias;

int
OPTremoteQueriesImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	InstrPtr p, q, r, *old;
	int i, j, cnt, limit, slimit, doit=0;
	int remoteSite,collectFirst;
	int *location;
	DBalias *dbalias;
	int dbtop,k;
	char buf[BUFSIZ],*s, *db;
	ValRecord cst;
	cst.vtype= TYPE_int;
	cst.val.ival= 0;


	OPTDEBUGremoteQueries
	mnstr_printf(cntxt->fdout, "RemoteQueries optimizer started\n");
	(void) cntxt;
	(void) stk;
	(void) pci;

	limit = mb->stop;
	slimit = mb->ssize;
	old = mb->stmt;

	location= (int*) GDKzalloc(mb->vsize * sizeof(int));
	if ( location == NULL)
		return 0;
	dbalias= (DBalias*) GDKzalloc(128 * sizeof(DBalias));
	if (dbalias == NULL){
		GDKfree(location);
		return 0;
	}
	dbtop= 0;

	if ( newMalBlkStmt(mb, mb->ssize) < 0){
		GDKfree(dbalias);
		GDKfree(location);
		return 0;
	}

	for (i = 0; i < limit; i++) {
		p = old[i];

		/* detect remote instructions */
		cnt=0;
		for(j=0; j<p->argc; j++)
			if (location[getArg(p,j)])
				cnt++;

		/* detect remote variable binding */
		
		if( (getModuleId(p)== mapiRef && getFunctionId(p)==bindRef)){
			if( p->argc == 3 && getArgType(mb,p,1) == TYPE_int ) {
				int tpe;
				setVarUDFtype(mb,getArg(p,0));
				j = getArg(p,1); /* lookupServer with key */
				tpe = getArgType(mb,p,0);
				/* result is remote */
				location[getArg(p,0)]= j;

				/* turn the instruction into a local one */
				/* one argument less */
				p->argc--;
				/* only use the second argument (string) */
				getArg(p,1)= getArg(p,2);

				getModuleId(p) = bbpRef;

				prepareRemote(tpe)
				putRemoteVariables()
				remoteAction()
			} else
				pushInstruction(mb,p);
		} else if( (getModuleId(p)== sqlRef && getFunctionId(p)==evalRef) ){
			if( p->argc == 3){
				/* a remote sql eval is needed */
				lookupServer(1)

				/* turn the instruction into a local one */
				/* one argument less */
				p->argc--;
				/* only use the second argument (string) */
				getArg(p,1)= getArg(p,2);

				prepareRemote(TYPE_void)

				s= RQcall2str(mb,p);
				r= pushStr(mb,r,s+1);
				GDKfree(s);
				pushInstruction(mb,r);
				freeInstruction(p);
				doit++;
			}
		} else if( (getModuleId(p)== sqlRef && getFunctionId(p)==bindRef) ){

			if( p->argc == 6 && getArgType(mb,p,4) == TYPE_str ) {
				int tpe;
				setVarUDFtype(mb,getArg(p,0));
				j = getArg(p,1); /* lookupServer with key */
				tpe = getArgType(mb,p,0);

				lookupServer(4)

				/* turn the instruction into a local one */
				getArg(p,4)= defConstant(mb, TYPE_int, &cst);

				prepareRemote(tpe)
				putRemoteVariables()
				remoteAction()
			} else
				pushInstruction(mb,p);
		} else
		if(getModuleId(p)== sqlRef && getFunctionId(p)== binddbatRef) {

			if( p->argc == 5 && getArgType(mb,p,3) == TYPE_str ) {
				lookupServer(3)

				/* turn the instruction into a local one */
				getArg(p,3)= defConstant(mb, TYPE_int, &cst);

				prepareRemote(TYPE_void)
				putRemoteVariables()
				remoteAction()
			} else
				pushInstruction(mb,p);
#ifdef DEBUG_OPT_REMOTE
			printf("found remote variable %s ad %d\n",
				getVarName(mb,getArg(p,0)), location[getArg(p,0)]);
#endif
		} else
		if( getModuleId(p) && strcmp(getModuleId(p),"optimizer")==0 &&
		    getFunctionId(p) && strcmp(getFunctionId(p),"remoteQueries")==0 )
			freeInstruction(p);
		else if (cnt == 0 || p->barrier) /* local only or flow control statement */
			pushInstruction(mb,p);
		else {
			/*
			 * The hard part is to decide what to do with instructions that
			 * contain a reference to a remote variable.
			 * In the first implementation we use the following policy.
			 * If there are multiple sites involved, all arguments are
			 * moved local for processing. Moreover, all local arguments
			 * to be shipped should be simple.
			 */
			remoteSite=0;
			collectFirst= FALSE;
			for(j=0; j<p->argc; j++)
			if( location[getArg(p,j)]){
				if (remoteSite == 0)
					remoteSite= location[getArg(p,j)];
				else if( remoteSite != location[getArg(p,j)])
					collectFirst= TRUE;
			}
			if( getModuleId(p)== ioRef || (getModuleId(p)== sqlRef
		            && (getFunctionId(p)== resultSetRef ||
				getFunctionId(p)== rsColumnRef)))
				 collectFirst= TRUE;

			/* local BATs are not shipped */
			if( remoteSite && collectFirst== FALSE)
				for(j=p->retc; j<p->argc; j++)
				if( location[getArg(p,j)] == 0 &&
					isaBatType(getVarType(mb,getArg(p,j))))
						collectFirst= TRUE;

			if (collectFirst){
				/* perform locally */
				for(j=p->retc; j<p->argc; j++)
				if( location[getArg(p,j)]){
					q= newStmt(mb,mapiRef,rpcRef);
					getArg(q,0)= getArg(p,j);
					q= pushArgument(mb,q,location[getArg(p,j)]);
					snprintf(buf,BUFSIZ,"io.print(%s);",
						getRefName(mb,getArg(p,j)) );
					(void) pushStr(mb,q,buf);
				}
				pushInstruction(mb,p);
				/* as of now all the targets are also local */
				for(j=0; j<p->retc; j++)
					location[getArg(p,j)]= 0;
				doit++;
			} else if (remoteSite){
				/* single remote site involved */
				r= newInstruction(mb,ASSIGNsymbol);
				getModuleId(r)= mapiRef;
				getFunctionId(r)= rpcRef;
				getArg(r,0)= newTmpVariable(mb, TYPE_void);
				r= pushArgument(mb, r, remoteSite);

				for(j=p->retc; j<p->argc; j++)
				if( location[getArg(p,j)] == 0 && !isVarConstant(mb,getArg(p,j)) ){
					q= newStmt(mb,mapiRef,putRef);
					getArg(q,0)= newTmpVariable(mb, TYPE_void);
					q= pushArgument(mb, q, remoteSite);
					q= pushStr(mb,q, getRefName(mb,getArg(p,j)));
					(void) pushArgument(mb, q, getArg(p,j));
				}
				s= RQcall2str(mb, p);
				pushInstruction(mb,r);
				(void) pushStr(mb,r,s+1);
				GDKfree(s);
				for(j=0; j<p->retc; j++)
					location[getArg(p,j)]= remoteSite;
				freeInstruction(p);
				doit++;
			} else
				pushInstruction(mb,p);
		}
	}
	for(; i<slimit; i++)
	if( old[i])
		freeInstruction(old[i]);
	GDKfree(old);
#ifdef DEBUG_OPT_REMOTE
	if (doit) {
		mnstr_printf(cntxt->fdout, "remoteQueries %d\n", doit);
		printFunction(cntxt->fdout, mb, 0, LIST_MAL_ALL);
	}
#endif
	GDKfree(location);
	GDKfree(dbalias);
	return doit;
}
