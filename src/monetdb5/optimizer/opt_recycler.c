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
 * The variables are all checked for being eligible as a variable
 * subject to recycling control. A variable may only be assigned
 * a value once. The target function is a sql.bind(-,-,-,0) or all arguments
 * are already recycle enabled or constant.
 *
 * The arguments of a function call cannot be recycled.
 * They change with each call. This does not mean
 * that the instructions using them can not be a
 * target of recycling.
 *
 * Just looking at a target result kept is not good enough.
 * You have to sure that the arguments are also the same.
 * This rules out function arguments.
 *
 * The recycler is targeted towards a read-only database.
 * The best effect is obtained for a single-user mode (sql_debug=32 )
 * when the delta-bats are not processed which allows longer instruction
 * chains to be recycled.
 * Update statements are not recycled. They trigger cleaning of
 * the recycle cache at the end of the query. Only intermediates
 * derived from the updated columns are invalidated.
 * Separate update instructions in queries, such as bat.append implementing 'OR',
 * are monitored and also trigger cleaning the cache.
 */
#include "monetdb_config.h"
#include "opt_recycler.h"
#include "mal_instruction.h"

static lng recycleSeq = 0;		/* should become part of MAL block basics */
static bte baseTableMode = 0;	/* only recycle base tables */

int
OPTrecyclerImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i, j, cnt, tp, c, actions = 0, marks = 0, delta = 0;
	Lifespan span;
	InstrPtr *old, q;
	int limit, updstmt = 0;
	char *recycled;
	short app_sc = -1, in = 0;
	ValRecord cst;

	(void) cntxt;
	(void) stk;

	limit = mb->stop;
	old = mb->stmt;

	for (i = 1; i < limit; i++) {
		p = old[i];
		if (getModuleId(p) == sqlRef &&
				(getFunctionId(p) == affectedRowsRef ||
				 getFunctionId(p) == exportOperationRef ||
				 getFunctionId(p) == appendRef ||
				 getFunctionId(p) == updateRef ||
				 getFunctionId(p) == deleteRef))
			updstmt = 1;
	}

	span = setLifespan(mb);
	if (span == NULL)
		return 0;

	/* watch out, newly created instructions may introduce new variables */
	recycled = GDKzalloc(sizeof(char) * mb->vtop * 2);
	if (recycled == NULL)
		return 0;
	if (newMalBlkStmt(mb, mb->ssize) < 0) {
		GDKfree(recycled);
		return 0;
	}
	pushInstruction(mb, old[0]);
	mb->recid = recycleSeq++;

	/* create a handle for recycler */
	(void) newFcnCall(mb, "recycle", "prelude");
	in = 1;
	for (i = 1; i < limit; i++) {
		p = old[i];
		if (hasSideEffects(p, TRUE) || isUpdateInstruction(p) || isUnsafeFunction(p)) {
			if (getModuleId(p) == recycleRef) { /*don't inline recycle instr. */
				freeInstruction(p);
				continue;
			}
			pushInstruction(mb, p);
			/*  update instructions are not recycled but monitored*/
			if (isUpdateInstruction(p)) {
				if (getModuleId(p) == batRef &&
					(getArgType(mb, p, 1) == TYPE_bat
					 || isaBatType(getArgType(mb, p, 1)))) {
					recycled[getArg(p, 1)] = 0;
					q = newFcnCall(mb, "recycle", "reset");
					pushArgument(mb, q, getArg(p, 1));
					actions++;
				}
				if (getModuleId(p) == sqlRef) {
					if (getFunctionId(p) == appendRef) {
						if (app_sc >= 0)
							continue;
						else
							app_sc = getArg(p, 2);
					}
					VALset(&cst, TYPE_int, &delta);
					c = defConstant(mb, TYPE_int, &cst);
					q = newFcnCall(mb, "recycle", "reset");
					pushArgument(mb, q, c);
					pushArgument(mb, q, getArg(p, 2));
					pushArgument(mb, q, getArg(p, 3));
					if (getFunctionId(p) == updateRef)
						pushArgument(mb, q, getArg(p, 4));
					actions++;
				}
			}
			/* take care of SQL catalog update instructions */
			if (getModuleId(p) == sqlRef && getFunctionId(p) == catalogRef) {
				tp = *(int *) getVarValue(mb, getArg(p, 1));
				if (tp == 22 || tp == 25) {
					delta = 2;
					VALset(&cst, TYPE_int, &delta);
					c = defConstant(mb, TYPE_int, &cst);
					q = newFcnCall(mb, "recycle", "reset");
					pushArgument(mb, q, c);
					pushArgument(mb, q, getArg(p, 2));
					if (tp == 25)
						pushArgument(mb, q, getArg(p, 3));
					actions++;
				}
			}
			continue;
		}
		if (p->token == ENDsymbol || p->barrier == RETURNsymbol) {
			if (in) {
				/*
				if (updstmt && app_sc >= 0) {
					q = newFcnCall(mb, "recycle", "reset");
					pushArgument(mb, q, app_sc);
					pushArgument(mb, q, app_tbl);
				}
				 */
				(void) newFcnCall(mb, "recycle", "epilogue");
				in = 0;
			}
			pushInstruction(mb, p);
			continue;
		}

		if (p->barrier && p->token != CMDcall) {
			/* never save a barrier unless it is a command and side-effect free */
			pushInstruction(mb, p);
			continue;
		}

		/* don't change instructions in update statements */
		if (updstmt) {
			pushInstruction(mb, p);
			continue;
		}

		/* skip simple assignments */
		if (p->token == ASSIGNsymbol) {
			pushInstruction(mb, p);
			continue;
		}

		if (getModuleId(p) == octopusRef &&
			(getFunctionId(p) == bindRef || getFunctionId(p) == bindidxRef)) {
			recycled[getArg(p, 0)] = 1;
			p->recycle = recycleMaxInterest;
			marks++;
		}
		/* During base table recycling skip marking instructions other than octopus.bind */
		if (baseTableMode) {
			pushInstruction(mb, p);
			continue;
		}

		/* general rule: all arguments are constants or recycled,
		   ignore C pointer arguments from mvc */
		cnt = 0;
		for (j = p->retc; j < p->argc; j++)
			if (recycled[getArg(p, j)] || isVarConstant(mb, getArg(p, j))
					|| ignoreVar(mb, getArg(p, j)))
				cnt++;
		if (cnt == p->argc - p->retc) {
			OPTDEBUGrecycle {
				mnstr_printf(cntxt->fdout, "#recycle instruction\n");
				printInstruction(cntxt->fdout, mb, 0, p, LIST_MAL_ALL);
			}
			marks++;
			p->recycle = recycleMaxInterest; /* this instruction is to be monitored */
			for (j = 0; j < p->retc; j++)
				if (getLastUpdate(span, getArg(p, j)) == i)
					recycled[getArg(p, j)] = 1;
		}
		/*
		 * The expected gain is largest if we can re-use selections
		 * on the base tables in SQL. These, however, are marked as
		 * uselect() calls, which only produce the oid head.
		 * For cheap types we preselect using select() and re-map uselect() back
		 * over this temporary.
		 * For the time being for all possible selects encountered
		 * are marked for re-use.
		 */
		/* take care of semantic driven recyling */
		/* for selections check the bat argument only
		   the range is often template parameter*/
		if ((getFunctionId(p) == selectRef ||
					getFunctionId(p) == antiuselectRef ||
					getFunctionId(p) == likeselectRef ||
					getFunctionId(p) == likeRef ||
					getFunctionId(p) == thetaselectRef) &&
				recycled[getArg(p, 1)])
		{
			p->recycle = recycleMaxInterest;
			marks++;
			if (getLastUpdate(span, getArg(p, 0)) == i)
				recycled[getArg(p, 0)] = 1;
		}
		if ((getFunctionId(p) == uselectRef || getFunctionId(p) == thetauselectRef)
				&& recycled[getArg(p, 1)])
		{
			if (!ATOMvarsized(getGDKType(getArgType(mb, p, 2)))) {
				q = copyInstruction(p);
				getArg(q, 0) = newTmpVariable(mb, TYPE_any);
				if (getFunctionId(p) == uselectRef)
					setFunctionId(q, selectRef);
				else
					setFunctionId(q, thetaselectRef);
				q->recycle = recycleMaxInterest;
				marks++;
				recycled[getArg(q, 0)] = 1;
				pushInstruction(mb, q);
				getArg(p, 1) = getArg(q, 0);
				setFunctionId(p, projectRef);
				p->argc = 2;
			}
			p->recycle = recycleMaxInterest;
			marks++;
			if (getLastUpdate(span, getArg(p, 0)) == i)
				recycled[getArg(p, 0)] = 1;
		}

		if (getModuleId(p) == pcreRef) {
			if ((getFunctionId(p) == selectRef && recycled[getArg(p, 2)]) ||
				(getFunctionId(p) == uselectRef && recycled[getArg(p, 2)])) {
				p->recycle = recycleMaxInterest;
				marks++;
				if (getLastUpdate(span, getArg(p, 0)) == i)
					recycled[getArg(p, 0)] = 1;
			} else if (getFunctionId(p) == likeuselectRef && recycled[getArg(p, 1)]) {
				q = copyInstruction(p);
				getArg(q, 0) = newTmpVariable(mb, TYPE_any);
				setFunctionId(q, likeselectRef);
				q->recycle = recycleMaxInterest;
				recycled[getArg(q, 0)] = 1;
				pushInstruction(mb, q);
				getArg(p, 1) = getArg(q, 0);
				setFunctionId(p, projectRef);
				setModuleId(p, algebraRef);
				p->argc = 2;
				p->recycle = recycleMaxInterest;
				marks += 2;
				if (getLastUpdate(span, getArg(p, 0)) == i)
					recycled[getArg(p, 0)] = 1;
			}
		}

		/*
		 * The sql.bind instructions should be handled carefully
		 * The delete and update BATs should not be recycled,
		 * because they may lead to view dependencies that later interferes
		 * with the transaction commits.
		 */
		/* enable recycling of delta-bats
		if (getModuleId(p) == sqlRef &&
				(((getFunctionId(p) == bindRef || getFunctionId(p) == putName("bind_idxbat", 11)) &&
				  getVarConstant(mb, getArg(p, 5)).val.ival != 0) ||
				 getFunctionId(p) == binddbatRef)) {
			recycled[getArg(p, 0)] = 0;
			p->recycle = REC_NO_INTEREST;
		}
		*/

/*
 * The sql.bind instructions should be handled carefully
 * The delete and update BATs should not be recycled,
 * because they may lead to view dependencies that later interferes
 * with the transaction commits.
 */
/* enable recycling of delta-bats
		if (getModuleId(p)== sqlRef && 
			(((getFunctionId(p)==bindRef || getFunctionId(p) == putName("bind_idxbat",11)) && 
				getVarConstant(mb, getArg(p,5)).val.ival != 0) ||
				getFunctionId(p)== binddbatRef) ) {
				recycled[getArg(p,0)]=0;
				p->recycle = REC_NO_INTEREST; 
			}
*/

		pushInstruction(mb, p);
	}
	GDKfree(span);
	GDKfree(old);
	GDKfree(recycled);
	mb->recycle = marks > 0;
	return actions + marks;
}
