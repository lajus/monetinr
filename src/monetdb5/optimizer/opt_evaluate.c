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
#include "opt_evaluate.h"
#include "opt_aliases.h"

static int
OPTallConstant(Client cntxt, MalBlkPtr mb, InstrPtr p)
{
	int i;
	(void)cntxt;

	if ( !( p->token == ASSIGNsymbol ||
			getModuleId(p) == calcRef ||
		   getModuleId(p) == strRef ||
		   getModuleId(p) == mmathRef ))
		return FALSE;

	for (i = p->retc; i < p->argc; i++)
		if (isVarConstant(mb, getArg(p, i)) == FALSE)
			return FALSE;
	for (i = 0; i < p->retc; i++)
		if (isaBatType(getArgType(mb, p, i)))
			return FALSE;
	return p->argc != p->retc;
}

static int OPTsimpleflow(MalBlkPtr mb, int pc)
{
	int i, block =0, simple= TRUE;
	InstrPtr p;

	for ( i= pc; i< mb->stop; i++){
		p =getInstrPtr(mb,i);
		if (blockStart(p))
			block++;
		if ( blockExit(p))
			block--;
		if ( blockCntrl(p))
			simple= FALSE;
		if ( block == 0){
			return simple;
		}
	}
	return FALSE;
}
/* barrier blocks can only be dropped when they are fully excluded.  */
static int
OPTremoveUnusedBlocks(Client cntxt, MalBlkPtr mb)
{
	/* catch and remove constant bounded blocks */
	int i, j = 0, action = 0, block = 0, skip = 0, top =0, skiplist[10];
	InstrPtr p;

	for (i = 0; i < mb->stop; i++) {
		p = mb->stmt[i];
		if (blockStart(p)) {
			block++;
			if (p->argc == 2 && isVarConstant(mb, getArg(p, 1)) &&
					getArgType(mb, p, 1) == TYPE_bit &&
					getVarConstant(mb, getArg(p, 1)).val.btval == 0)
			{
				if (skip == 0)
					skip = block;
				action++;
			}
			// Try to remove the barrier statement itself (when true).
			if (p->argc == 2 && isVarConstant(mb, getArg(p, 1)) &&
					getArgType(mb, p, 1) == TYPE_bit &&
					getVarConstant(mb, getArg(p, 1)).val.btval == 1 && 
					top <10 && OPTsimpleflow(mb,i))
			{
				skiplist[top++]= getArg(p,0);
				freeInstruction(p);
				continue;
			}
		}
		if (blockExit(p)) {
			if (top > 0 && skiplist[top-1] == getArg(p,0) ){
				top--;
				freeInstruction(p);
				continue;
			} 
			if (skip )
				freeInstruction(p);
			else
				mb->stmt[j++] = p;
			if (skip == block)
				skip = 0;
			block--;
			if (block == 0)
				skip = 0;
		} else if (skip)
			freeInstruction(p);
		else
			mb->stmt[j++] = p;
	}
	mb->stop = j;
	for (; j < i; j++)
		mb->stmt[j] = NULL;
	if (action) {
		chkTypes(cntxt->fdout, cntxt->nspace, mb, TRUE);
		return mb->errors ? 0 : action;
	}
	return action;
}

int
OPTevaluateImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	InstrPtr p;
	int i, k, limit, *alias, barrier;
	MalStkPtr env = NULL;
	int profiler;
	str msg;
	int debugstate = cntxt->itrace, actions = 0, constantblock = 0;
	int *assigned, setonce; 

	cntxt->itrace = 0;
	(void)stk;
	(void)pci;

	if (varGetProp(mb, getArg(mb->stmt[0], 0), inlineProp) != NULL)
		return 0;

	(void)cntxt;
	OPTDEBUGevaluate mnstr_printf(cntxt->fdout, "Constant expression optimizer started\n");

	assigned = (int*) GDKzalloc(sizeof(int) * mb->vtop);
	if (assigned == NULL)
		return 0;

	alias = (int*)GDKzalloc(mb->vsize * sizeof(int) * 2); /* we introduce more */
	if (alias == NULL){
		GDKfree(assigned);
		return 0;
	}

	// arguments are implicitly assigned by context
	p = getInstrPtr(mb, 0);
	for ( k =p->retc;  k < p->argc; k++)
		assigned[getArg(p,k)]++;
	limit = mb->stop;
	for (i = 1; i < limit; i++) {
		p = getInstrPtr(mb, i);
		// The double count emerging from a barrier exit is ignored.
		if (! blockExit(p) || (blockExit(p) && p->retc != p->argc))
		for ( k =0;  k < p->retc; k++)
			assigned[getArg(p,k)]++;
	}

	for (i = 1; i < limit; i++) {
		p = getInstrPtr(mb, i);
		for (k = p->retc; k < p->argc; k++)
			if (alias[getArg(p, k)])
				getArg(p, k) = alias[getArg(p, k)];
		// to avoid management of duplicate assignments over multiple blocks
		// we limit ourselfs to evaluation of the first assignment only.
		setonce = assigned[getArg(p,0)] == 1;
		OPTDEBUGevaluate printInstruction(cntxt->fdout, mb, 0, p, LIST_MAL_ALL);
		constantblock +=  blockStart(p) && OPTallConstant(cntxt,mb,p);

		/* be aware that you only assign once to a variable */
		if (setonce && p->retc == 1 && OPTallConstant(cntxt, mb, p) && !isUnsafeFunction(p)) {
			barrier = p->barrier;
			p->barrier = 0;
			profiler = malProfileMode;	/* we don't trace it */
			malProfileMode = 0;
			if ( env == NULL) {
				env = prepareMALstack(mb,  2 * mb->vsize );
				env->keepAlive = TRUE;
			}
			msg = reenterMAL(cntxt, mb, i, i + 1, env);
			malProfileMode= profiler;
			p->barrier = barrier;
			OPTDEBUGevaluate {
				mnstr_printf(cntxt->fdout, "#retc var %s\n", getVarName(mb, getArg(p, 0)));
				mnstr_printf(cntxt->fdout, "#result:%s\n", msg == MAL_SUCCEED ? "ok" : msg);
			}
			if (msg == MAL_SUCCEED) {
				int nvar;
				ValRecord cst;

				actions++;
				cst.vtype = 0;
				VALcopy(&cst, &env->stk[getArg(p, 0)]);
				/* You may not overwrite constants.  They may be used by
				 * other instructions */
				nvar = getArg(p, 1) = defConstant(mb, getArgType(mb, p, 0), &cst);
				if (nvar >= env->stktop) {
					VALcopy(&env->stk[getArg(p, 1)], &getVarConstant(mb, getArg(p, 1)));
					env->stktop = getArg(p, 1) + 1;
				}
				alias[getArg(p, 0)] = getArg(p, 1);
				p->argc = 2;
				p->token = ASSIGNsymbol;
				clrFunction(p);
				p->barrier = barrier;
				/* freeze the type */
				setVarFixed(mb,getArg(p,1));
				setVarUDFtype(mb,getArg(p,1));
				OPTDEBUGevaluate {
					mnstr_printf(cntxt->fdout, "Evaluated new constant=%d -> %d:%s\n",
						getArg(p, 0), getArg(p, 1), getTypeName(getArgType(mb, p, 1)));
				}
			} else {
				/* if there is an error, we should postpone message handling,
					as the actual error (eg. division by zero ) may not happen) */
				OPTDEBUGevaluate mnstr_printf(cntxt->fdout, "Evaluated %s\n", msg);
				GDKfree(msg);
				mb->errors = 0;
			}
		}
	}
	if ( constantblock )
		actions += OPTremoveUnusedBlocks(cntxt, mb);
	GDKfree(assigned);
	GDKfree(alias);
	if ( env) 
		freeStack(env);
	cntxt->itrace = debugstate;
	return actions;
}
