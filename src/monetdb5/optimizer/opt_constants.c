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
 * Constant Duplicate Removal
 * The compilers may generate an abundance of constants on
 * the stack. This simple optimizer re-organizes performs a complete
 * job to use constants only once.
 * This makes it easier to search for statement duplicates
 * and alias their variables.
 */

/*
 * We have to keep an alias table to reorganize the program
 * after the variable stack has changed.
 */
#include "monetdb_config.h"
#include "mal_instruction.h"
#include "opt_constants.h"

int
OPTconstantsImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i,k=1, n=0, fnd=0, actions=0;
	int *alias, *index;
	VarPtr x,y, *cst;

	OPTDEBUGconstants mnstr_printf(cntxt->fdout,"#OPT_CONSTANTS: MATCHING CONSTANTS ELEMENTS\n");

	alias= (int*) GDKzalloc(sizeof(int) * mb->vtop);
	cst= (VarPtr*) GDKzalloc(sizeof(VarPtr) * mb->vtop);
	index= (int*) GDKzalloc(sizeof(int) * mb->vtop);

	if ( alias == NULL || cst == NULL || index == NULL){
		if( alias) GDKfree(alias);
		if( cst) GDKfree(cst);
		if( index) GDKfree(index);
		return 0;
	}

	(void) stk;
	(void) cntxt;

	for (i=0; i< mb->vtop; i++)
		alias[ i]= i;
	for (i=0; i< mb->vtop; i++)
		if ( isVarConstant(mb,i)  && isVarFixed(mb,i) ){
			x= getVar(mb,i); 
			fnd = 0;
			if ( x->type && x->value.vtype)
			for( k= n-1; k>=0; k--){
				y= cst[k];
				if ( x->type == y->type &&
					 x->value.vtype == y->value.vtype &&
					ATOMcmp(x->value.vtype, VALptr(&x->value), VALptr(&y->value)) == 0){
					OPTDEBUGconstants {
						mnstr_printf(cntxt->fdout,"#opt_constants: matching elements %s %d %d ", getVarName(mb,i), i,k);
						ATOMprint(x->value.vtype,VALptr(&x->value),cntxt->fdout);
						mnstr_printf(cntxt->fdout,"\n");
					}
					/* re-use a constant */
					alias[i]= index[k];
					fnd=1;
					actions++;
					break;
				}
			}
			if ( fnd == 0){
				OPTDEBUGconstants mnstr_printf(cntxt->fdout,"swith elements %d %d\n", i,n);
				cst[n]= x;
				index[n]= i;
				n++;
			} 
		} 

	for (i = 0; i < mb->stop; i++){
		p= getInstrPtr(mb,i);
		for (k=0; k < p->argc; k++)
			getArg(p,k) = alias[getArg(p,k)];
	}
	GDKfree(alias);
	GDKfree(cst);
	GDKfree(index);
	return actions;
}
