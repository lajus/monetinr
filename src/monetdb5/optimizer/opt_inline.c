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
#include "opt_inline.h"

extern int OPTinlineMultiplex(Client cntxt, MalBlkPtr mb, InstrPtr p);

static int
isCorrectInline(MalBlkPtr mb){
	/* make sure we have a simple inline function with a singe return */
	InstrPtr p;
	int i, retseen=0;

	for( i= 1; i < mb->stop; i++){
		p= getInstrPtr(mb,i);
		if ( p->token == RETURNsymbol || p->token == YIELDsymbol || 
			 p->barrier == RETURNsymbol || p->barrier == YIELDsymbol)
			retseen++;
	}
	return retseen <= 1;
}


int
OPTinlineImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i;
	InstrPtr q,sig;
	int actions = 0;

	(void) p;
	(void)stk;

	for (i = 1; i < mb->stop; i++) {
		q = getInstrPtr(mb, i);
		if( q->blk ){
			sig = getInstrPtr(q->blk,0);
			/*
			 * Time for inlining functions that are used in multiplex operations.
			 * They are produced by SQL compiler.
			 */
			if( getFunctionId(q)== multiplexRef &&
				getModuleId(q) == malRef &&
				OPTinlineMultiplex(cntxt,mb,q)){

				OPTDEBUGinline {
					mnstr_printf(cntxt->fdout,"#multiplex inline function\n");
					printInstruction(cntxt->fdout,mb,0,q,LIST_MAL_ALL);
				}

			    varSetProp(mb, getArg(q,0), inlineProp, op_eq, NULL);
			} else
			/*
			 * Check if the function definition is tagged as being inlined.
			 */
			if (sig->token == FUNCTIONsymbol &&
			    varGetProp(q->blk, getArg(sig, 0), inlineProp) != NULL &&
				isCorrectInline(q->blk) ) {
				(void) inlineMALblock(mb,i,q->blk);
				i--;
				actions++;
				OPTDEBUGinline {
					mnstr_printf(cntxt->fdout,"#inline function at %d\n",i);
					printFunction(cntxt->fdout, mb, 0, LIST_MAL_ALL);
					printInstruction(cntxt->fdout,q->blk,0,sig,LIST_MAL_ALL);
				}
			} else 
			/*
			 * Check if the local call is tagged as being inlined.
			 */
			if (varGetProp(mb, getArg(q,0), inlineProp) != NULL) {
				inlineMALblock(mb,i,q->blk);
				i--;
				actions++;
				OPTDEBUGinline {
					mnstr_printf(cntxt->fdout,"#inlined called at %d\n",i);
					printFunction(cntxt->fdout, mb, 0, LIST_MAL_ALL);
					printInstruction(cntxt->fdout,q->blk,0,sig,LIST_MAL_ALL);
				}
			} 
		}
	}
	return actions;
}


int OPTinlineMultiplex(Client cntxt, MalBlkPtr mb, InstrPtr p){
	Symbol s;
	str mod,fcn;
	int res;

	mod = VALget(&getVar(mb, getArg(p, 1))->value);
	fcn = VALget(&getVar(mb, getArg(p, 2))->value);
	if( (s= findSymbol(cntxt->nspace, mod,fcn)) ==0 )
		return FALSE;
	/*
	 * Before we decide to propagate the inline request
	 * to the multiplex operation, we check some basic properties
	 * of the target function. Moreover, we apply the inline optimizer
	 * to the target function as well.
	 * This code should be protected against overflow due to recursive calls.
	 * In general, this is a hard problem. For now, we just expand.
	 */
	(void) OPTinlineImplementation(cntxt, s->def, NULL, p);
	res= varGetProp(s->def , getArg(getInstrPtr(s->def,0), 0),
				inlineProp) != NULL;
	return res;
}
