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
 * The first attempt of the multiple optimizer is to locate
 * a properly typed multi-plexed implementation.
 * The policy is to search for bat<mod>.<fcn> before going
 * into the iterator code generation.
 */
#include "monetdb_config.h"
#include "opt_remap.h"
#include "opt_macro.h"

static int
OPTremapDirect(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci, Module scope){
	str mod,fcn;
	char buf[1024];
	int i;
	InstrPtr p;

	(void) stk;
	mod = VALget(&getVar(mb, getArg(pci, 1))->value);
	fcn = VALget(&getVar(mb, getArg(pci, 2))->value);

	if(strncmp(mod,"bat",3)==0)
		return 0;
	OPTDEBUGremap 
		mnstr_printf(cntxt->fdout,"#Found a candidate %s.%s\n",mod,fcn);

	snprintf(buf,1024,"bat%s",mod);
	p= newInstruction(mb,ASSIGNsymbol);
	setModuleId(p,putName(buf, strlen(buf)));
	setFunctionId(p,putName(fcn, strlen(fcn)));

	for(i=0; i<pci->retc; i++)
		getArg(p,i)= getArg(pci,i);
	p->retc= p->argc= pci->retc;
	for(i= pci->retc+2; i<pci->argc; i++)
		p= pushArgument(mb,p,getArg(pci,i));
	OPTDEBUGremap 
		printInstruction(cntxt->fdout,mb,0,p,LIST_MAL_ALL);

	/* now see if we can resolve the instruction */
	typeChecker(cntxt->fdout, scope,mb,p,TRUE);
	if( p->typechk== TYPE_UNKNOWN) {
		OPTDEBUGremap {
			mnstr_printf(cntxt->fdout,"#type error\n");
			printInstruction(cntxt->fdout,mb,0,p,LIST_MAL_ALL);
		}

		freeInstruction(p);
		return 0;
	}
	pushInstruction(mb,p);
	OPTDEBUGremap mnstr_printf(cntxt->fdout,"success\n");
	return 1;
}

/*
 * Multiplex inline functions should be done with care.
 * The approach taken is to make a temporary copy of the function to be inlined.
 * To change all the statements to reflect the new situation
 * and, if no error occurs, replaces the target instruction
 * with this new block.
 *
 * By the time we get here, we know that function is
 * side-effect free.
 *
 * The multiplex upgrade is targeted at all function
 * arguments whose actual is a BAT and its formal
 * is a scalar.
 * This seems sufficient for the SQL generated PSM code,
 * but does in general not hold.
 * For example,
 *
 * function foo(b:int,c:bat[:oid,:int])
 * 	... d:= batcalc.+(b,c)
 * and
 * multiplex("user","foo",ba:bat[:oid,:int],ca:bat[:oid,:int])
 * upgrades the first argument. The naive upgrade of
 * the statement that would fail. The code below catches
 * most of them by simple prepending "bat" to the MAL function
 * name and leave it to the type resolver to generate the
 * error.
 *
 * The process terminates as soon as we
 * find an instruction that does not have a multiplex
 * counterpart.
 */
static int 
OPTmultiplexInline(Client cntxt, MalBlkPtr mb, InstrPtr p, int pc )
{
	MalBlkPtr mq;
	InstrPtr q = NULL, sig;
	char buf[1024];
	int i,j,k, actions=0;
	int refbat=0;
	bit *upgrade;
	Symbol s;
	s= findSymbol(cntxt->nspace, VALget(&getVar(mb, getArg(p, 1))->value),
			VALget(&getVar(mb, getArg(p, 2))->value));

	if( s== NULL || !isSideEffectFree(s->def) || 
		getInstrPtr(s->def,0)->retc != p->retc ) {
		OPTDEBUGremap {
			if( s== NULL)
				mnstr_printf(cntxt->fdout,"#not found \n");
			else
				mnstr_printf(cntxt->fdout,"#side-effects\n");
		}
		return 0;
	}
	/*
	 * Determine the variables to be upgraded and adjust their type
	 */
	mq= copyMalBlk(s->def);
	sig= getInstrPtr(mq,0);
	OPTDEBUGremap {
		mnstr_printf(cntxt->fdout,"#Modify the code\n");
		printFunction(cntxt->fdout,mq, 0, LIST_MAL_ALL);
		printInstruction(cntxt->fdout,mb, 0, p,LIST_MAL_ALL);
	}

	upgrade = (bit*) GDKzalloc(sizeof(bit)*mq->vtop);
	if( upgrade == NULL) 
		return 0;

	setVarType(mq, 0,newBatType(TYPE_oid, getArgType(mb,p,0)));
	clrVarFixed(mq,getArg(getInstrPtr(mq,0),0)); /* for typing */
	upgrade[getArg(getInstrPtr(mq,0),0)] = TRUE;

	for(i=3; i<p->argc; i++){
		if( !isaBatType( getArgType(mq,sig,i-2)) &&
			isaBatType( getArgType(mb,p,i)) ){

			if( getTailType(getArgType(mb,p,i)) != getArgType(mq,sig,i-2)){
				OPTDEBUGremap
					mnstr_printf(cntxt->fdout,"#Type mismatch %d\n",i);
				goto terminateMX;
			}
			OPTDEBUGremap
				mnstr_printf(cntxt->fdout,"#Upgrade type %d %d\n",i, getArg(sig,i-2));
			setVarType(mq, i-2,newBatType(TYPE_oid, getArgType(mb,p,i)));
			upgrade[getArg(sig,i-2)]= TRUE;
			refbat= getArg(sig,i-2);
		}
	}
	/*
	 * The next step is to check each instruction of the
	 * to-be-inlined function for arguments that require
	 * an upgrade and resolve it afterwards.
	 */
	for(i=1; i<mq->stop; i++) {
		int fnd = 0;

		q = getInstrPtr(mq,i);
		if (q->token == ENDsymbol)
			break;
		for(j=0; j<q->argc && !fnd; j++) 
			if (upgrade[getArg(q,j)]) {
				for(k=0; k<q->retc; k++){
					setVarType(mq,getArg(q,j),newBatType(TYPE_oid,getArgType(mq, q, j)));
					/* for typing */
					clrVarFixed(mq,getArg(q,k)); 
					if (!upgrade[getArg(q,k)]) {
						upgrade[getArg(q,k)]= TRUE;
						/* lets restart */
						i = 0;
					}
				}
				fnd = 1;
			}
		/* nil:type -> nil:bat[:oid,:type] */
		if (!getModuleId(q) && q->token == ASSIGNsymbol &&
		    q->argc == 2 && isVarConstant(mq, getArg(q,1)) && 
		    upgrade[getArg(q,0)] &&
			getArgType(mq,q,0) == TYPE_void &&
		    !isaBatType(getArgType(mq, q, 1)) ){
				/* handle nil assignment */
				if( ATOMcmp(getArgGDKType(mq, q, 1),
					VALptr(&getVar(mq, getArg(q,1))->value),
					ATOMnilptr(getArgType(mq, q, 1))) == 0) {
				ValRecord cst;
				int tpe = newBatType(TYPE_oid,getArgType(mq, q, 1));

				setVarType(mq,getArg(q,0),tpe);
				cst.vtype = TYPE_bat;
				cst.val.bval = bat_nil;
				getArg(q,1) = defConstant(mq, tpe, &cst);
				setVarType(mq, getArg(q,1), tpe);
			} else{
				/* handle constant tail setting */
				int tpe = newBatType(TYPE_oid,getArgType(mq, q, 1));

				setVarType(mq,getArg(q,0),tpe);
				setModuleId(q,algebraRef);
				setFunctionId(q,projectRef);
				q= pushArgument(mb,q, getArg(q,1));
				getArg(q,1)= refbat;
			}
		}
	}

	/* now upgrade the statements */
	for(i=1; i<mq->stop; i++){
		q= getInstrPtr(mq,i);
		if( q->token== ENDsymbol)
			break;
		for(j=0; j<q->argc; j++)
			if ( upgrade[getArg(q,j)]){
				if ( blockStart(q) || 
					 q->barrier== REDOsymbol || q->barrier==LEAVEsymbol )
					goto terminateMX;
				if (getModuleId(q)){
					snprintf(buf,1024,"bat%s",getModuleId(q));
					setModuleId(q,putName(buf,strlen(buf)));

					actions++;
					/* now see if we can resolve the instruction */
					typeChecker(cntxt->fdout, cntxt->nspace,mq,q,TRUE);
					if( q->typechk== TYPE_UNKNOWN)
						goto terminateMX;
					break;
				}
				/* handle simple upgraded assignments as well */
				if ( q->token== ASSIGNsymbol &&
					 q->argc == 2  &&
					!(isaBatType( getArgType(mq,q,1))) ){
					setModuleId(q,algebraRef);
					setFunctionId(q,projectRef);
					q= pushArgument(mq,q, getArg(q,1));
					getArg(q,1)= refbat;
				
					actions++;
					typeChecker(cntxt->fdout, cntxt->nspace,mq,q,TRUE);
					if( q->typechk== TYPE_UNKNOWN)
						goto terminateMX;
					break;
				}
		}
	}


	if(mq->errors){
terminateMX:
		OPTDEBUGremap {
			mnstr_printf(cntxt->fdout,"Abort remap\n");
			if (q)
				printInstruction(cntxt->fdout,mb,0,q,LIST_MAL_ALL);
		}
		freeMalBlk(mq);
		GDKfree(upgrade);
		return 0;
	}
	/*
	 * We have successfully constructed a variant
	 * of the to-be-inlined function. Put it in place
	 * of the original multiplex.
	 * But first, shift the arguments of the multiplex.
	 */
	delArgument(p,2);
	delArgument(p,1);
	inlineMALblock(mb,pc,mq);
	OPTDEBUGremap {
		printInstruction(cntxt->fdout,mb,0,p,LIST_MAL_ALL);
		mnstr_printf(cntxt->fdout,"#NEW BLOCK\n");
		printFunction(cntxt->fdout,mq, 0, LIST_MAL_ALL);
		mnstr_printf(cntxt->fdout,"#INLINED RESULT\n");
		printFunction(cntxt->fdout,mb, 0, LIST_MAL_ALL);
	}
	freeMalBlk(mq);
	GDKfree(upgrade);
	return 1;
}
/*
 * The comparison multiplex operations with a constant head may be supported
 * by reverse of the operation.
 */
static struct{
	char *src, *dst;
	int len;
}OperatorMap[]={
{"<", ">",1},
{">", "<",1},
{">=", "<=",2},
{"<=", ">=",2},
{"==", "==",2},
{"!=", "!=",2},
{0,0,0}};

static int
OPTremapSwitched(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci, Module scope){
	char *fcn;
	int r,i;
	(void) stk;
	(void) scope;

	if( getModuleId(pci)!= malRef &&
	    getFunctionId(pci) != multiplexRef &&
	    !isVarConstant(mb,getArg(pci,1)) &&
	    !isVarConstant(mb,getArg(pci,2)) &&
	    !isVarConstant(mb,getArg(pci,4)) &&
		pci->argc != 5) 
			return 0;
	fcn = VALget(&getVar(mb, getArg(pci, 2))->value);
	for(i=0;OperatorMap[i].src;i++)
	if( strcmp(fcn,OperatorMap[i].src)==0){
		/* found a candidate for a switch */
		getVarConstant(mb, getArg(pci, 2)).val.sval = putName(OperatorMap[i].dst,OperatorMap[i].len);
		getVarConstant(mb, getArg(pci, 2)).len = OperatorMap[i].len;
		r= getArg(pci,3); getArg(pci,3)=getArg(pci,4);getArg(pci,4)=r;
		r= OPTremapDirect(cntxt,mb, stk, pci, scope);

		/* always restore the allocated function name */
		getVarConstant(mb, getArg(pci, 2)).val.sval= fcn;
		assert(strlen(fcn) <= INT_MAX);
		getVarConstant(mb, getArg(pci, 2)).len= (int) strlen(fcn);

		if (r) return 1;

		/* restore the arguments */
		r= getArg(pci,3); getArg(pci,3)=getArg(pci,4);getArg(pci,4)=r;
	}
	return 0;
}
int
OPTremapImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{

	InstrPtr *old, p;
	int i, limit, slimit, doit= 0;
	Module scope = cntxt->nspace;

	(void) pci;
	old = mb->stmt;
	limit = mb->stop;
	slimit = mb->ssize;
	if ( newMalBlkStmt(mb, mb->ssize) < 0 )
		return 0;

	for (i = 0; i < limit; i++) {
		p = old[i];
		if ( getModuleId(p) == malRef && 
			getFunctionId(p) == multiplexRef) {
			/*
			 * The next step considered is to handle inlined functions.
			 * It means we have already skipped the most obvious ones,
			 * such as the calculator functions. It is particularly
			 * geared at handling the PSM code.
			 */
			if ( varGetProp(mb,getArg(p,0),inlineProp)!= NULL) {
				OPTDEBUGremap{
					mnstr_printf(cntxt->fdout,"#Multiplex inline\n");
					printInstruction(cntxt->fdout,mb,0,p,LIST_MAL_ALL);
				}
				pushInstruction(mb, p);
				if( OPTmultiplexInline(cntxt,mb,p,mb->stop-1) )
					doit++;
				OPTDEBUGremap
					mnstr_printf(cntxt->fdout,"#doit %d\n",doit);
			} else 
			if(	OPTremapDirect(cntxt, mb, stk, p, scope) ||
				OPTremapSwitched(cntxt, mb, stk, p, scope)){
				freeInstruction(p); 
				doit++;
			} else {
				pushInstruction(mb, p);
			}
		} else if (p->argc == 4 && 
			getModuleId(p) == aggrRef && 
			getFunctionId(p) == avgRef) {
			/* group aggr.avg -> aggr.sum/aggr.count */	
			InstrPtr sum = copyInstruction(p), avg, t, iszero;
			InstrPtr cnt = copyInstruction(p);
			setFunctionId(sum, sumRef);
			setFunctionId(cnt, countRef);
			getArg(sum,0) = newTmpVariable(mb, getArgType(mb, p, 1));
			getArg(cnt,0) = newTmpVariable(mb, newBatType(TYPE_oid,TYPE_wrd));
			pushInstruction(mb, sum);
			pushInstruction(mb, cnt);

			t = newInstruction(mb, ASSIGNsymbol);
			setModuleId(t, batcalcRef);
			setFunctionId(t, putName("==", strlen("==")));
			getArg(t,0) = newTmpVariable(mb, newBatType(TYPE_oid,TYPE_bit));
			t = pushArgument(mb, t, getDestVar(cnt));
			t = pushWrd(mb, t, 0);
			pushInstruction(mb, t);
			iszero = t;

			t = newInstruction(mb, ASSIGNsymbol);
			setModuleId(t, batcalcRef);
			setFunctionId(t, dblRef);
			getArg(t,0) = newTmpVariable(mb, getArgType(mb, p, 0));
			t = pushArgument(mb, t, getDestVar(sum));
			pushInstruction(mb, t);
			sum = t;

			t = newInstruction(mb, ASSIGNsymbol);
			setModuleId(t, batcalcRef);
			setFunctionId(t, putName("ifthenelse", strlen("ifthenelse")));
			getArg(t,0) = newTmpVariable(mb, getArgType(mb, p, 0));
			t = pushArgument(mb, t, getDestVar(iszero));
			t = pushNil(mb, t, TYPE_dbl);
			t = pushArgument(mb, t, getDestVar(sum));
			pushInstruction(mb, t);
			sum = t;

			t = newInstruction(mb, ASSIGNsymbol);
			setModuleId(t, batcalcRef);
			setFunctionId(t, dblRef);
			getArg(t,0) = newTmpVariable(mb, getArgType(mb, p, 0));
			t = pushArgument(mb, t, getDestVar(cnt));
			pushInstruction(mb, t);
			cnt = t;

			avg = newInstruction(mb, ASSIGNsymbol);
			setModuleId(avg, batcalcRef);
			setFunctionId(avg, divRef);
			getArg(avg, 0) = getArg(p, 0);
			avg = pushArgument(mb, avg, getDestVar(sum));
			avg = pushArgument(mb, avg, getDestVar(cnt));
			freeInstruction(p);
			pushInstruction(mb, avg);
		} else {
			pushInstruction(mb, p);
		}
	}
	for(; i<slimit; i++)
	if( old[i])
		freeInstruction(old[i]);
	GDKfree(old);
	OPTDEBUGremap
	if (doit){
		mnstr_printf(cntxt->fdout,"#After remap, before type check\n");
		printFunction(cntxt->fdout, mb, 0,  LIST_MAL_ALL);
	}

	if (doit) {
		chkTypes(cntxt->fdout, cntxt->nspace,mb,TRUE);
		/* clean out on errors by resetting the block */
		if ( mb->errors)
		for( i=1;i<slimit; i++){
		}
	}
	return mb->errors? 0: doit;
}

str
OPTremapMultiplex(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p){
	(void) mb;
	(void)stk;
	(void) p;
	OPTDEBUGremap
		printInstruction(cntxt->fdout,mb,0,p,LIST_MAL_ALL);
	throw(MAL, "opt.remap", PROGRAM_NYI);
}
