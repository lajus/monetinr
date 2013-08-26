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
 * @f run_adder
 * @a The ROX Team
 * @+ Dynamic Statement Generation Example
 *
 * we have a function: adder.generate(target, batch)
 * that is recognized by this scheduler.
 * @example
 *         x := 0;
 * 	x := adder.generate(10,2)
 * 	io.print(x);
 * @end example
 * should produce on first iteration
 * @example
 * 	x := 0;
 * 	x := calc.+(x,1);
 * 	x := calc.+(x,1);
 * 	x := adder.generate(8,2)
 * 	io.print(x);
 * @end example
 * next when generate() is found:
 * @example
 * 	x := calc.+(x,1);
 * 	x := calc.+(x,1);
 * 	x := adder.generate(6,2)
 * 	io.print(x);
 * @end example
 * etc, until x = base
 *
 */
/*
 * @+ Adder implementation
 * The code below is a mixture of generic routines and
 * sample implementations to run the tests.
 */
#include "monetdb_config.h"
#include "mal_builder.h"
#include "opt_prelude.h"
#include "run_adder.h"

/*
 * @-
 * THe choice operator first searches the next one to identify
 * the fragment to be optimized and to gain access to the variables
 * without the need to declare them upfront.
 */
/* helper routine that at runtime propagates values to the stack */
static void adder_addval(MalBlkPtr mb, MalStkPtr stk, int i) {
	ValPtr rhs, lhs = &stk->stk[i];
	if (isVarConstant(mb,i) > 0 ){
		rhs = &getVarConstant(mb,i);
		VALcopy(lhs,rhs);
	} else {
		lhs->vtype = getVarGDKType(mb,i);
		lhs->val.pval = 0;
		lhs->len = 0;
	}
}

str
RUNadder(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int total;
	int batch;
	int size;
	int i,pc;
	InstrPtr q, *old;
	int oldtop;

	(void) cntxt;
	pc = getPC(mb,p);
	total = *(int*) getArgReference(stk,p,1);
	batch = *(int*) getArgReference(stk,p,2);
	if (total == 0) return MAL_SUCCEED;

	old = mb->stmt;
	oldtop= mb->stop;
	size = ((mb->stop+batch) < mb->ssize)? mb->ssize:(mb->stop+batch);
	mb->stmt = (InstrPtr *) GDKzalloc(size * sizeof(InstrPtr));
	mb->ssize = size;
	memcpy( mb->stmt, old, sizeof(InstrPtr)*(pc+1));
	mb->stop = pc+1;

	if (batch > total) total = batch;
	for (i=0; i<batch; i++) {
		/* tmp := calc.+(x,1) */
		q = newStmt(mb, calcRef, plusRef);
		getArg(q,0) = getArg(p,0);
		q = pushArgument(mb, q, getArg(p, 0));
		q = pushInt(mb, q, 1);
		adder_addval(mb, stk, getArg(q,2));
	}
	total -= batch;
	*(int*) getArgReference(stk,p,1) = total;
	mb->var[getArg(p,1)]->value.val.ival = total; /* also set in symbol table */
	if (total > 0) {
		q = copyInstruction(p);
		pushInstruction(mb, q);
	}
	memcpy(mb->stmt+mb->stop, old+pc+1, sizeof(InstrPtr) * (oldtop-pc)-1);
	mb->stop += (oldtop-pc)-1;

	/* check new statments for sanity */
	chkTypes(cntxt->fdout, cntxt->nspace, mb, FALSE);
	chkFlow(cntxt->fdout, mb);
	chkDeclarations(cntxt->fdout, mb);

	GDKfree(old);
	return MAL_SUCCEED;
}
