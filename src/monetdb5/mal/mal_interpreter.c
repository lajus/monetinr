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
 * Author M. Kersten
 * The MAL Interpreter
 */
#include "monetdb_config.h"
#include "mal_runtime.h"
#include "mal_interpreter.h"
#include "mal_resource.h"
#include "mal_listing.h"
#include "mal_debugger.h"   /* for mdbStep() */
#include "mal_recycle.h"
#include "mal_type.h"

/*
 * The struct alignment leads to 40% gain in simple instructions when set.
 */
inline
ptr getArgReference(MalStkPtr stk, InstrPtr pci, int k)
{
#ifdef STRUCT_ALIGNED
	return (ptr) & stk->stk[pci->argv[k]].val.ival;
#else
	int j = 0;
	ValRecord *v = 0;
	ptr ret = NULL;

	j = pci->argv[k];
	v = &stk->stk[j];

	switch (ATOMstorage(v->vtype)) {
	case TYPE_void: ret = (ptr) & v->val.ival; break;
	case TYPE_bit: ret = (ptr) & v->val.btval; break;
	case TYPE_sht: ret = (ptr) & v->val.shval; break;
	case TYPE_bat: ret = (ptr) & v->val.bval; break;
	case TYPE_int: ret = (ptr) & v->val.ival; break;
	case TYPE_wrd: ret = (ptr) & v->val.wval; break;
	case TYPE_bte: ret = (ptr) & v->val.btval; break;
	case TYPE_oid: ret = (ptr) & v->val.oval; break;
	case TYPE_ptr: ret = (ptr) & v->val.pval; break;
	case TYPE_flt: ret = (ptr) & v->val.fval; break;
	case TYPE_dbl: ret = (ptr) & v->val.dval; break;
	case TYPE_lng: ret = (ptr) & v->val.lval; break;
	case TYPE_str: ret = (ptr) & v->val.sval; break;
	default:
		ret = (ptr) & v->val.pval;
	}
	return ret;
#endif
}

/* code is obsolete, because all should be handled as exceptions */
void showErrors(Client cntxt)
{
	int i;
	char *errbuf = GDKerrbuf;
	if (errbuf && *errbuf) {
		i = (int)strlen(errbuf);
		mnstr_printf(cntxt->fdout, "%s", errbuf);
		if (errbuf[i - 1] != '\n')
			mnstr_printf(cntxt->fdout, "\n");
		errbuf[0] = '\0';
	}
}

str malCommandCall(MalStkPtr stk, InstrPtr pci)
{
	str ret= MAL_SUCCEED;
	
	switch (pci->argc) {
	case 0: ret = (str)(*pci->fcn)();
		break;
	case 1: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0));
		break;
	case 2: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1));
		break;
	case 3: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2));
		break;
	case 4: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3));
		break;
	case 5: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4));
		break;
	case 6: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5));
		break;
	case 7: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6));
		break;
	case 8: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7));
		break;
	case 9: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8));
		break;
	case 10: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9));
		break;
	case 11: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10));
		break;
	case 12: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10),
			(ptr)getArgReference(stk, pci, 11));
		break;
	case 13: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10),
			(ptr)getArgReference(stk, pci, 11),
			(ptr)getArgReference(stk, pci, 12));
		break;
	case 14: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10),
			(ptr)getArgReference(stk, pci, 11),
			(ptr)getArgReference(stk, pci, 12),
			(ptr)getArgReference(stk, pci, 13));
		break;
	case 15: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10),
			(ptr)getArgReference(stk, pci, 11),
			(ptr)getArgReference(stk, pci, 12),
			(ptr)getArgReference(stk, pci, 13),
			(ptr)getArgReference(stk, pci, 14));
		break;
	case 16: ret = (str)(*pci->fcn)(
			(ptr)getArgReference(stk, pci, 0),
			(ptr)getArgReference(stk, pci, 1),
			(ptr)getArgReference(stk, pci, 2),
			(ptr)getArgReference(stk, pci, 3),
			(ptr)getArgReference(stk, pci, 4),
			(ptr)getArgReference(stk, pci, 5),
			(ptr)getArgReference(stk, pci, 6),
			(ptr)getArgReference(stk, pci, 7),
			(ptr)getArgReference(stk, pci, 8),
			(ptr)getArgReference(stk, pci, 9),
			(ptr)getArgReference(stk, pci, 10),
			(ptr)getArgReference(stk, pci, 11),
			(ptr)getArgReference(stk, pci, 12),
			(ptr)getArgReference(stk, pci, 13),
			(ptr)getArgReference(stk, pci, 14),
			(ptr)getArgReference(stk, pci, 15));
		break;
	default:
		throw(MAL, "mal.interpreter", "too many arguments for command call");
	}
	return ret;
}

/*
 * Copy the constant values onto the stack frame
 * Also we cannot overwrite values on the stack as this maybe part of a
 * sequence of factory calls.
 */
#define initStack(S)\
	for (i = S; i < mb->vtop; i++) {\
		lhs = &stk->stk[i];\
		if (isVarConstant(mb, i) > 0) {\
			if (!isVarDisabled(mb, i)) {\
				rhs = &getVarConstant(mb, i);\
				VALcopy(lhs, rhs);\
			}\
		} else {\
			lhs->vtype = getVarGDKType(mb, i);\
			lhs->val.pval = 0;\
			lhs->len = 0;\
		}\
	} 

int
isNotUsedIn(InstrPtr p, int start, int a)
{
	int k;
	for (k = start; k < p->argc; k++)
		if (getArg(p, k) == a)
			return 0;
	return 1;
}

MalStkPtr
prepareMALstack(MalBlkPtr mb, int size)
{
	MalStkPtr stk = NULL;
	int i;
	ValPtr lhs, rhs;

	assert(size >= mb->vsize);
	stk = newGlobalStack(size);
	//memset((char *)stk, 0, stackSize(size)); already set
	//stk->stksize = size;
	stk->stktop = mb->vtop;
	stk->blk = mb;

	initStack(0);
	return stk;
}

str runMAL(Client cntxt, MalBlkPtr mb, MalBlkPtr mbcaller, MalStkPtr env)
{
	MalStkPtr stk = NULL;
	int i;
	ValPtr lhs, rhs;
	str ret;
	(void) mbcaller;

	if (mb->errors) {
		if (cntxt->itrace == 0) /* permit debugger analysis */
			throw( MAL, "mal.interpreter", "Syntax error in script");
	}
/*
 * Prepare a new interpreter call. This involves two steps, (1) allocate
 * the minimum amount of stack space needed, some slack resources
 * are included to permit code optimizers to add a few variables at run time,
 * (2) copying the arguments into the new stack frame.
 *
 * The env stackframe is set when a MAL function is called recursively.
 * Alternatively, there is no caller but a stk to be re-used for interpretation.
 * We assume here that it aligns with the variable table of the routine
 * being called.
 *
 * allocate space for value stack
 * the global stack should be large enough
*/
	if (env != NULL) {
		stk = env;
		if (mb != stk->blk)
			showScriptException(cntxt->fdout, mb, 0, MAL, "runMAL:misalignment of symbols\n");
		if (mb->vtop > stk->stksize)
			showScriptException(cntxt->fdout, mb, 0, MAL, "stack too small\n");
		initStack(env->stkbot);
	} else {
		stk = prepareMALstack(mb, mb->vsize);
		if (stk == 0)
			throw(MAL, "mal.interpreter", MAL_STACK_FAIL);
		stk->blk = mb;
		stk->cmd = cntxt->itrace;    /* set debug mode */
		/*safeguardStack*/
		if (env) {
			stk->stkdepth = stk->stksize + env->stkdepth;
			stk->calldepth = env->calldepth + 1;
			stk->up = env;
			if (stk->calldepth > 256)
				throw(MAL, "mal.interpreter", MAL_CALLDEPTH_FAIL);
			if ((unsigned)stk->stkdepth > THREAD_STACK_SIZE / sizeof(mb->var[0]) / 4 && THRhighwater())
				/* we are running low on stack space */
				throw(MAL, "mal.interpreter", MAL_STACK_FAIL);
		}
/*
 * An optimization is to copy all constant variables used in functions immediately
 * onto the value stack. Then we do not have to check for their location
 * later on any more. At some point, the effect is optimal, if at least several
 * constants are referenced in a function (a gain on tst400a of 20% has been
 * observed due the small size of the function).
 */
	}
	if (stk->cmd && env && stk->cmd != 'f')
		stk->cmd = env->cmd;
	ret = runMALsequence(cntxt, mb, 1, 0, stk, env, 0);

	/* pass the new debug mode to the caller */
	if (stk->cmd && env && stk->cmd != 'f')
		env->cmd = stk->cmd;
	if (!stk->keepAlive && garbageControl(getInstrPtr(mb, 0)))
		garbageCollector(cntxt, mb, stk, env != stk);
	if (stk && stk != env) {
		GDKfree(stk);
	}
	if (cntxt->qtimeout && GDKms() > cntxt->qtimeout)
		throw(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
	return ret;
}

/* Single instruction
 * It is possible to re-enter the interpreter at a specific place.
 * This is used in the area where we need to support co-routines.
 *
 * A special case for MAL interpretation is to execute just one instruction.
 * This is typically used by optimizers and schedulers that need part of the
 * answer to direct their actions. Or, a dataflow scheduler could step in
 * to enforce a completely different execution order.
 */
str reenterMAL(Client cntxt, MalBlkPtr mb, int startpc, int stoppc, MalStkPtr stk)
{
	str ret;
	int keepAlive;

	if (stk == NULL)
		throw(MAL, "mal.interpreter", MAL_STACK_FAIL);
	keepAlive = stk->keepAlive;
	ret = runMALsequence(cntxt, mb, startpc, stoppc, stk, 0, 0);

	/* pass the new debug mode to the caller */
	if (keepAlive == 0 && garbageControl(getInstrPtr(mb, 0)))
		garbageCollector(cntxt, mb, stk, stk != 0);
	return ret;
}

/*
 * Front ends may benefit from a more direct call to any of the MAL
 * procedural abstractions. The argument list points to the arguments
 * for the block to be executed. An old stack frame may be re-used,
 * but it is then up to the caller to ensure it is properly
 * initialized.
 * The call does not return values, they are ignored.
 */
str
callMAL(Client cntxt, MalBlkPtr mb, MalStkPtr *env, ValPtr argv[], char debug)
{
	MalStkPtr stk = NULL;
	str ret = MAL_SUCCEED;
	int i;
	ValPtr lhs;
	InstrPtr pci = getInstrPtr(mb, 0);
 
/*
 * Control the level of parallelism. The maximum number of concurrent MAL plans
 * is determined by an environment variable. It is initially set equal to the
 * number of cores, which may be too coarse.
 */
	MT_sema_down(&mal_parallelism,"callMAL");
#ifdef DEBUG_CALLMAL
	mnstr_printf(cntxt->fdout, "callMAL\n");
	printInstruction(cntxt->fdout, mb, 0, pci, LIST_MAL_ALL);
#endif
	switch (pci->token) {
	case FUNCTIONsymbol:
	case FCNcall:
		/*
		 * Prepare the stack frame for this operation. Copy all the arguments
		 * in place. We assume that the caller has supplied pointers for
		 * all arguments and return values.
		 */
		if (*env == NULL) {
			stk = prepareMALstack(mb, mb->vsize);
			stk->up = 0;
			*env = stk;
		} else stk = *env;
		assert(stk);
		for (i = pci->retc; i < pci->argc; i++) {
			lhs = &stk->stk[pci->argv[i]];
			VALcopy(lhs, argv[i]);
			if (lhs->vtype == TYPE_bat)
				BBPincref(lhs->val.bval, TRUE);
		}
		stk->cmd = debug;
		ret = runMALsequence(cntxt, mb, 1, 0, stk, 0, 0);
		break;
	case FACTORYsymbol:
	case FACcall:
		ret = callFactory(cntxt, mb, argv, debug);
		break;
	case PATcall:
	case CMDcall:
	default:
		MT_sema_up(&mal_parallelism,"callMAL");
		throw(MAL, "mal.interpreter", RUNTIME_UNKNOWN_INSTRUCTION);
	}
	MT_sema_up(&mal_parallelism,"callMAL");
	if (cntxt->qtimeout && GDKms() > cntxt->qtimeout)
		throw(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
	return ret;
}

/*
 * The core of the interpreter is presented next. It takes the context information
 * and starts the interpretation at the designated instruction.
 * Note that the stack frame is aligned and initialized in the enclosing routine.
 * When we start executing the first instruction, we take the wall-clock time for
 * resource management.
 */
str runMALsequence(Client cntxt, MalBlkPtr mb, int startpc,
				   int stoppc, MalStkPtr stk, MalStkPtr env, InstrPtr pcicaller)
{
	ValPtr lhs, rhs, v;
	int i, k;
	InstrPtr pci = 0;
	int exceptionVar;
	str ret = 0, localGDKerrbuf= GDKerrbuf;
	int stamp = -1;
	ValRecord backups[16];
	ValPtr backup;
	int garbages[16], *garbage;
	int stkpc = 0;
	RuntimeProfileRecord runtimeProfile, runtimeProfileFunction;
	runtimeProfile.stkpc = runtimeProfileFunction.stkpc = 0;

	if (stk == NULL)
		throw(MAL, "mal.interpreter", MAL_STACK_FAIL);

	/* prepare extended backup and garbage structures */
	if ( mb->maxarg > 16 ){
		backup = GDKzalloc(mb->maxarg * sizeof(ValRecord));
		garbage = (int*)GDKzalloc(mb->maxarg * sizeof(int));
	} else {
		backup = backups;
		garbage = garbages;
		memset((char*) garbages, 0, 16 * sizeof(int));
	}

	/* also produce event record for start of function */
	if ( startpc == 1 ){
		runtimeProfileInit(cntxt, mb, stk);
		runtimeProfileBegin(cntxt, mb, stk, 0, &runtimeProfileFunction, 1);
	} 
	stkpc = startpc;
	exceptionVar = -1;

	while (stkpc < mb->stop && stkpc != stoppc) {
		pci = getInstrPtr(mb, stkpc);
		if (cntxt->itrace || mb->trap || stk->status) {
			if (stk->status == 'p'){
				// execution is paused
				while ( stk->status == 'p')
					MT_sleep_ms(50);
				continue;
			}
			if ( stk->status == 'q')
				stk->cmd = 'x';

			if (stk->cmd == 0)
				stk->cmd = cntxt->itrace;
			mdbStep(cntxt, mb, stk, stkpc);
			if (stk->cmd == 'x' || cntxt->mode == FINISHING) {
				stk->cmd = 0;
				stkpc = mb->stop;
				continue;
			}
		}

		runtimeProfileBegin(cntxt, mb, stk, stkpc, &runtimeProfile, 1);
		if (pci->recycle > 0)
			stk->clk = GDKms();
		if (!RECYCLEentry(cntxt, mb, stk, pci)){
/* The interpreter loop
 * The interpreter is geared towards execution a MAL procedure together
 * with all its decendant invocations. As such, it provides the
 * MAL abtract machine processor.
 *
 * The value-stack frame of the surrounding scope is needed to resolve
 * binding values.  Getting (putting) a value from (into) a surrounding
 * scope should be guarded with the exclusive access lock.
 * This situation is encapsulated by a bind() function call, whose parameters
 * contain the access mode required.
 *
 * The formal procedure arguments are assumed to always occupy the first
 * elements in the value stack.
 *
 * Before we execute an instruction the variables to be garbage collected
 * are identified. In the post-execution phase they are removed.
 */
			if (garbageControl(pci)) {
				for (i = 0; i < pci->argc; i++) {
					int a = getArg(pci, i);

					backup[i].vtype = 0;
					backup[i].len = 0;
					backup[i].val.pval = 0;
					garbage[i] = -1;
					if (stk->stk[a].vtype == TYPE_bat && getEndOfLife(mb, a) == stkpc && isNotUsedIn(pci, i + 1, a))
						garbage[i] = a;

					if (i < pci->retc && stk->stk[a].vtype == TYPE_bat) {
						backup[i] = stk->stk[a];
						stamp = BBPcurstamp();
					} else if (i < pci->retc &&
							   0 < stk->stk[a].vtype &&
							   stk->stk[a].vtype < TYPE_any &&
							   ATOMextern(stk->stk[a].vtype)) {
						backup[i] = stk->stk[a];
					}
				}
			}

			FREE_EXCEPTION(ret);
			ret = 0;
			switch (pci->token) {
			case ASSIGNsymbol:
/* Assignment command
 * The assignment statement copies values around on the stack frame,
 * including multiple assignments.
 *
 * Pushing constants/initial values onto the stack is a separate operation.
 * It takes the constant value discovered at compile time and stored in the
 * symbol table and moves it to the stackframe location. This activity
 * is made part of the start-up procedure.
 *
 * The before after calls should be reconsidered here, because
 * their. They seem superflous and the way they are used will
 * cause errors in multi-assignment statements.
 */
				for (k = 0, i = pci->retc; k < pci->retc && i < pci->argc; i++, k++) {
					lhs = &stk->stk[pci->argv[k]];
					rhs = &stk->stk[pci->argv[i]];
					VALcopy(lhs, rhs);
					if (lhs->vtype == TYPE_bat && lhs->val.bval)
						BBPincref(lhs->val.bval, TRUE);
				}
				FREE_EXCEPTION(ret);
				ret = 0;
				break;
			case PATcall:
				if (pci->fcn == NULL) {
					ret = createScriptException(mb, stkpc, MAL, NULL,
						"address of pattern %s.%s missing", pci->modname, pci->fcnname);
				} else
					ret = (str)(*pci->fcn)(cntxt, mb, stk, pci);
				break;
			case CMDcall:
				ret =malCommandCall(stk, pci);
				break;
			case FACcall:
/*
 * Factory calls are more involved. At this stage it is a synchrononous
 * call to the factory manager.
 * Factory calls should deal with the reference counting.
 */
				if (pci->blk == NULL)
					ret = createScriptException(mb, stkpc, MAL, NULL,
						"reference to MAL function missing");
				else {
					/* show call before entering the factory */
					if (cntxt->itrace || mb->trap) {
						if (stk->cmd == 0)
							stk->cmd = cntxt->itrace;
						mdbStep(cntxt, pci->blk, stk, 0);
						if (stk->cmd == 'x' || cntxt->mode == FINISHING) {
							stk->cmd = 0;
							stkpc = mb->stop;
						}
					}
					ret = runFactory(cntxt, pci->blk, mb, stk, pci);
				}
				break;
			case FCNcall:
/*
 * MAL function calls are relatively expensive, because they have to assemble
 * a new stack frame and do housekeeping, such as garbagecollection of all
 * non-returned values.
 */
				{	MalStkPtr nstk;
					InstrPtr q;
					int ii, arg;

					stk->pcup = stkpc;
					nstk = prepareMALstack(pci->blk, pci->blk->vsize);
					if (nstk == 0){
						ret= createException(MAL,"mal.interpreter",MAL_STACK_FAIL);
						break;
					}

					/*safeguardStack*/
					nstk->stkdepth = nstk->stksize + stk->stkdepth;
					nstk->calldepth = stk->calldepth + 1;
					nstk->up = stk;
					if (nstk->calldepth > 256) {
						ret= createException(MAL, "mal.interpreter", MAL_CALLDEPTH_FAIL);
						break;
					}
					if ((unsigned)nstk->stkdepth > THREAD_STACK_SIZE / sizeof(mb->var[0]) / 4 && THRhighwater()){
						/* we are running low on stack space */
						ret= createException(MAL, "mal.interpreter", MAL_STACK_FAIL);
						break;
					}

					/* copy arguments onto destination stack */
					q= getInstrPtr(pci->blk,0);
					arg = q->retc;
					for (ii = pci->retc; ii < pci->argc; ii++,arg++) {
						lhs = &nstk->stk[q->argv[arg]];
						rhs = &stk->stk[pci->argv[ii]];
						VALcopy(lhs, rhs);
						if (lhs->vtype == TYPE_bat)
							BBPincref(lhs->val.bval, TRUE);
					}
					ret = runMALsequence(cntxt, pci->blk, 1, pci->blk->stop, nstk, stk, pci);
					GDKfree(nstk);
				}
				break;
			case NOOPsymbol:
			case REMsymbol:
				break;
			case ENDsymbol:
				if (getInstrPtr(mb, 0)->token == FACTORYsymbol)
					ret = shutdownFactory(cntxt, mb);
				runtimeProfileExit(cntxt, mb, stk, pci, &runtimeProfile);
				runtimeProfileExit(cntxt, mb, stk, getInstrPtr(mb,0), &runtimeProfileFunction);
				runtimeProfileFinish(cntxt, mb);
				if (pcicaller && garbageControl(getInstrPtr(mb, 0)))
					garbageCollector(cntxt, mb, stk, TRUE);
				if (cntxt->qtimeout && GDKms() > cntxt->qtimeout){
					ret= createException(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
					break;
				}
				stkpc = mb->stop;	// force end of loop
				continue;
			default: {
				str w;
				if (pci->token < 0) {
					/* temporary NOOP instruction */
					break;
				}
				w= instruction2str(mb, 0, pci, FALSE);
				ret = createScriptException(mb, stkpc, MAL, NULL, "unkown operation:%s",w);
				GDKfree(w);
				if (cntxt->qtimeout && GDKms() > cntxt->qtimeout){
					ret= createException(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
					break;
				}
				stkpc= mb->stop;
				continue;
			}	}

			/* monitoring information should reflect the input arguments,
			   which may be removed by garbage collection  */
			runtimeProfileExit(cntxt, mb, stk, pci, &runtimeProfile);
			/* check for strong debugging after each MAL statement */
			if ( pci->token != FACcall && ret== MAL_SUCCEED) {
				if (GDKdebug & (CHECKMASK|PROPMASK) && exceptionVar < 0) {
					BAT *b;

					for (i = 0; i < pci->retc; i++) {
						if (garbage[i] == -1 && stk->stk[getArg(pci, i)].vtype == TYPE_bat &&
							stk->stk[getArg(pci, i)].val.bval) {
							b = BBPquickdesc(ABS(stk->stk[getArg(pci, i)].val.bval), FALSE);
							if (b == NULL) {
								ret = createException(MAL, "mal.propertyCheck", RUNTIME_OBJECT_MISSING);
								continue;
							}
							if (b->batStamp <= stamp) {
								if (GDKdebug & PROPMASK) {
									b = BATdescriptor(stk->stk[getArg(pci, i)].val.bval);
									BATassertProps(b);
									BBPunfix(b->batCacheid);
								}
							} else if (GDKdebug & CHECKMASK) {
								b = BATdescriptor(stk->stk[getArg(pci, i)].val.bval);
								BATassertProps(b);
								BBPunfix(b->batCacheid);
							}
						}
					}
				}

				/* If needed recycle intermediate result */
				if (pci->recycle > 0) {
					RECYCLEexit(cntxt, mb, stk, pci, stk->clk);
				}

				/* general garbage collection */
				if (ret == MAL_SUCCEED && garbageControl(pci)) {
					for (i = 0; i < pci->argc; i++) {
						int a = getArg(pci, i);

						if (isaBatType(getArgType(mb, pci, i))) {
							bat bid = stk->stk[a].val.bval;

							if (i < pci->retc && backup[i].val.bval) {
								bat bx = backup[i].val.bval;
								backup[i].val.bval = 0;
								BBPdecref(bx, TRUE);
							}
							if (garbage[i] >= 0) {
								PARDEBUG mnstr_printf(GDKstdout, "#GC pc=%d bid=%d %s done\n", stkpc, bid, getVarName(mb, garbage[i]));
								bid = ABS(stk->stk[garbage[i]].val.bval);
								stk->stk[garbage[i]].val.bval = 0;
								BBPdecref(bid, TRUE);
							}
						} else if (i < pci->retc &&
								   0 < stk->stk[a].vtype &&
								   stk->stk[a].vtype < TYPE_any &&
								   ATOMextern(stk->stk[a].vtype)) {
							if (backup[i].val.pval &&
								backup[i].val.pval != stk->stk[a].val.pval) {
								if (backup[i].val.pval)
									GDKfree(backup[i].val.pval);
								if (i >= pci->retc) {
									stk->stk[a].val.pval = 0;
									stk->stk[a].len = 0;
								}
								backup[i].len = 0;
								backup[i].val.pval = 0;
							}
						}
					}
				}
			}

			/* Exception handling */
			if (localGDKerrbuf && localGDKerrbuf[0]) {
				str oldret = ret;
				ret = catchKernelException(cntxt, ret);
				if (ret != oldret)
					FREE_EXCEPTION(oldret);
			}

			if (ret != MAL_SUCCEED) {
				str msg = 0;

				if (stk->cmd || mb->trap) {
					mnstr_printf(cntxt->fdout, "!ERROR: %s\n", ret);
					stk->cmd = '\n'; /* in debugging go to step mode */
					mdbStep(cntxt, mb, stk, stkpc);
					if (stk->cmd == 'x' || stk->cmd == 'q' || cntxt->mode == FINISHING) {
						stkpc = mb->stop;
						continue;
					}
					if (stk->cmd == 'r') {
						stk->cmd = 'n';
						stkpc = startpc;
						exceptionVar = -1;
						continue;
					}
				}
				/* Detect any exception received from the implementation. */
				/* The first identifier is an optional exception name */
				if (strstr(ret, "!skip-to-end")) {
					GDKfree(ret);       /* no need to check for M5OutOfMemory */
					ret = MAL_SUCCEED;
					stkpc = mb->stop;
					continue;
				}
				/*
				 * Exceptions are caught based on their name, which is part of the
				 * exception message. The ANYexception variable catches all.
				 */
				exceptionVar = -1;
				msg = strchr(ret, ':');
				if (msg) {
					*msg = 0;
					exceptionVar = findVariableLength(mb, ret, (int)(msg - ret));
					*msg = ':';
				}
				if (exceptionVar == -1)
					exceptionVar = findVariableLength(mb, (str)"ANYexception", 12);

				/* unknown exceptions lead to propagation */
				if (exceptionVar == -1) {
					runtimeProfileExit(cntxt, mb, stk, pci, &runtimeProfile);
					runtimeProfileFinish(cntxt, mb);
					if (cntxt->qtimeout && GDKms() > cntxt->qtimeout)
						ret= createException(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
					stkpc = mb->stop;
					continue;
				}
				/* assure correct variable type */
				if (getVarType(mb, exceptionVar) == TYPE_str) {
					/* watch out for concurrent access */
					MT_lock_set(&mal_contextLock, "exception handler");
					v = &stk->stk[exceptionVar];
					if (v->val.sval)
						FREE_EXCEPTION(v->val.sval);    /* old exception*/
					v->vtype = TYPE_str;
					v->val.sval = ret;
					v->len = (int)strlen(v->val.sval);
					ret = 0;
					MT_lock_unset(&mal_contextLock, "exception handler");
				} else {
					mnstr_printf(cntxt->fdout, "%s", ret);
					FREE_EXCEPTION(ret);
				}
				/* position yourself at the catch instruction for further decisions */
				/* skipToCatch(exceptionVar,@2,@3) */
				if (stk->cmd == 'C' || mb->trap) {
					stk->cmd = 'n';
					mdbStep(cntxt, mb, stk, stkpc);
					if (stk->cmd == 'x' || cntxt->mode == FINISHING) {
						stkpc = mb->stop;
						continue;
					}
				}
				/* skip to catch block or end */
				for (; stkpc < mb->stop; stkpc++) {
					InstrPtr l = getInstrPtr(mb, stkpc);
					if (l->barrier == CATCHsymbol) {
						int j;
						for (j = 0; j < l->retc; j++)
							if (getArg(l, j) == exceptionVar)
								break;
							else if (getArgName(mb, l, j) ||
									 strcmp(getArgName(mb, l, j), "ANYexception") == 0)
								break;
						if (j < l->retc)
							break;
					}
				}
				if (stkpc == mb->stop) {
					if (cntxt->qtimeout && GDKms() > cntxt->qtimeout){
						ret= createException(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
						stkpc = mb->stop;
					}
					continue;
				}
				pci = getInstrPtr(mb, stkpc);
			}
		}

/*
 * After the expression has been evaluated we should check for a
 * possible change in the control flow.
 */
		switch (pci->barrier) {
		case BARRIERsymbol:
			v = &stk->stk[getDestVar(pci)];
			/* skip to end of barrier, depends on the type */
			switch (v->vtype) {
			case TYPE_bit:
				if (v->val.btval == FALSE || v->val.btval == bit_nil)
					stkpc = pci->jump;
				break;
			case TYPE_bte:
				if (v->val.btval == bte_nil)
					stkpc = pci->jump;
				break;
			case TYPE_oid:
				if (v->val.oval == oid_nil)
					stkpc = pci->jump;
				break;
			case TYPE_sht:
				if (v->val.shval == sht_nil)
					stkpc = pci->jump;
				break;
			case TYPE_int:
				if (v->val.ival == int_nil)
					stkpc = pci->jump;
				break;
			case TYPE_lng:
				if (v->val.lval == lng_nil)
					stkpc = pci->jump;
				break;
			case TYPE_flt:
				if (v->val.fval == flt_nil)
					stkpc = pci->jump;
				break;
			case TYPE_dbl:
				if (v->val.dval == dbl_nil)
					stkpc = pci->jump;
				break;
			case TYPE_str:
				if (v->val.sval == str_nil)
					stkpc = pci->jump;
				break;
			default:
				ret = createScriptException(mb, stkpc, MAL, NULL,
					"%s: Unknown barrier type",
					getVarName(mb, getDestVar(pci)));
			}
			stkpc++;
			break;
		case LEAVEsymbol:
		case REDOsymbol:
			v = &stk->stk[getDestVar(pci)];
			/* skip to end of barrier, depending on the type */
			switch (v->vtype) {
			case TYPE_bit:
				if (v->val.btval == TRUE )
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_str:
				if (v->val.sval != str_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_oid:
				if (v->val.oval != oid_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_sht:
				if (v->val.shval != sht_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_int:
				if (v->val.ival != int_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_wrd:
				if (v->val.wval != wrd_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_bte:
				if (v->val.btval != bte_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_lng:
				if (v->val.lval != lng_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_flt:
				if (v->val.fval != flt_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			case TYPE_dbl:
				if (v->val.dval != dbl_nil)
					stkpc = pci->jump;
				else
					stkpc++;
				break;
			default:
				break;
			}
			break;
		case CATCHsymbol:
			/* catch blocks are skipped unless
			   searched for explicitly*/
			if (exceptionVar < 0) {
				stkpc = pci->jump;
				break;
			}
			exceptionVar = -1;
			stkpc++;
			break;
		case EXITsymbol:
			if (getDestVar(pci) == exceptionVar)
				exceptionVar = -1;
			stkpc++;
			break;
		case RAISEsymbol:
			exceptionVar = getDestVar(pci);
			FREE_EXCEPTION(ret);
			ret = NULL;
			if (getVarType(mb, getDestVar(pci)) == TYPE_str) {
				ret = createScriptException(mb, stkpc, MAL, NULL,
					"%s", stk->stk[getDestVar(pci)].val.sval);
			}
			/* skipToCatch(exceptionVar, @2, stk) */
			if (stk->cmd == 'C' || mb->trap) {
				stk->cmd = 'n';
				mdbStep(cntxt, mb, stk, stkpc);
				if (stk->cmd == 'x' || cntxt->mode == FINISHING) {
					stkpc = mb->stop;
					continue;
				}
			}
			/* skip to catch block or end */
			for (; stkpc < mb->stop; stkpc++) {
				InstrPtr l = getInstrPtr(mb, stkpc);
				if (l->barrier == CATCHsymbol) {
					int j;
					for (j = 0; j < l->retc; j++)
						if (getArg(l, j) == exceptionVar)
							break;
						else if (getArgName(mb, l, j) ||
								 strcmp(getArgName(mb, l, j), "ANYexception") == 0)
							break;
					if (j < l->retc)
						break;
				}
			}
			if (stkpc == mb->stop) {
				runtimeProfileExit(cntxt, mb, stk, pci, &runtimeProfile);
				runtimeProfileExit(cntxt, mb, stk, getInstrPtr(mb,0), &runtimeProfileFunction);
				runtimeProfileFinish(cntxt, mb);
				break;
			}
			if (stkpc == mb->stop)
				ret = createScriptException(mb, stkpc, MAL, ret,
					"Exception raised");
			break;
		case YIELDsymbol:     /* to be defined */
			if ( backup != backups) GDKfree(backup);
			if ( garbage != garbages) GDKfree(garbage);
			return yieldFactory(mb, pci, stkpc);
		case RETURNsymbol:
			/* Return from factory involves cleanup */

			if (getInstrPtr(mb, 0)->token == FACTORYsymbol) {
				yieldResult(mb, pci, stkpc);
				shutdownFactory(cntxt, mb);
			} else {
				/* a fake multi-assignment */
				if (env != NULL && pcicaller != NULL) {
					InstrPtr pp = pci;
					pci = pcicaller;
					for (i = 0; i < pci->retc; i++) {
						rhs = &stk->stk[pp->argv[i]];
						lhs = &env->stk[pci->argv[i]];
						VALcopy(lhs, rhs);
						if (lhs->vtype == TYPE_bat)
							BBPincref(lhs->val.bval, TRUE);
					}
					if (garbageControl(getInstrPtr(mb, 0)))
						garbageCollector(cntxt, mb, stk, TRUE);
					/* reset the clock */
					runtimeProfileExit(cntxt, mb, stk, pp, &runtimeProfile);
					runtimeProfileExit(cntxt, mb, stk, getInstrPtr(mb,0), &runtimeProfileFunction);
					runtimeProfileFinish(cntxt, mb);
				} 
			}
			stkpc = mb->stop;
			continue;
		default:
			stkpc++;
		}
		if (cntxt->qtimeout && GDKms() > cntxt->qtimeout){
			ret= createException(MAL, "mal.interpreter", RUNTIME_QRY_TIMEOUT);
			stkpc= mb->stop;
		}
	}

	/* if we could not find the exception variable, cascade a new one */
	if (exceptionVar >= 0) {
		str oldret = ret;
		if (ret) {
			ret = createScriptException(mb, mb->stop - 1,
				getExceptionType(getVarName(mb, exceptionVar)),
				ret, "Exception not caught");
		} else {
			if (stk->stk[exceptionVar].vtype == TYPE_str) {
				ret = createScriptException(mb, mb->stop - 1, MAL,
					stk->stk[exceptionVar].val.sval,
					"Exception not caught");
			} else {
				ret = createScriptException(mb, mb->stop - 1, MAL,
					NULL, "Exception not caught");
			}
		}
		FREE_EXCEPTION(oldret);
	}
	if ( backup != backups) GDKfree(backup);
	if ( garbage != garbages) GDKfree(garbage);
	return ret;
}

/* Safeguarding
 * The physical stack for each thread is an operating system parameter.
 * We do not want recursive programs crashing the server, so once in
 * a while we check whether we are running dangerously low on available
 * stack space.
 *
 * This situation can be detected by calling upon the GDK functionality
 * of by limiting the depth of a function calls.
 * Expensive? 70 msec for 1M calls. Use with care.
 */
str
safeguardStack(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int depth = *(int*)getArgReference(stk, pci, 1);
	(void)cntxt;
	if (stk->stkdepth > depth * mb->vtop && THRhighwater()) {
		throw(MAL, "mal.interpreter", MAL_STACK_FAIL);
	}
	return MAL_SUCCEED;
}



/*
 * MAL API
 * The linkage between MAL interpreter and compiled C-routines
 * is kept as simple as possible.
 * Basically we distinguish four kinds of calling conventions:
 * CMDcall, FCNcall, FACcall, and  PATcall.
 * The FCNcall indicates calling a MAL procedure, which leads
 * to a recursive call to the interpreter.
 *
 * CMDcall initiates calling a linked function, passing pointers
 * to the parameters and result variable, i.e.  f(ptr a0,..., ptr aN)
 * The function returns a MAL-SUCCEED upon success and a pointer
 * to an exception string upon failure.
 * Failure leads to raise-ing an exception in the interpreter loop,
 * by either looking up the relevant exception message in the module
 * administration or construction of a standard string.
 *
 * The PATcall initiates a call which contains the MAL context,
 * i.e. f(MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
 * The mb provides access to the code definitions. It is primarilly
 * used by routines intended to manipulate the code base itself, such
 * as the optimizers. The Mal stack frame pointer provides access
 * to the values maintained. The arguments passed are offsets
 * into the stack frame rather than pointers to the actual value.
 *
 * BAT parameters require some care. Ideally, a BAT should not be kept
 * around long. This would mean that each time we access a BAT it has to be
 * pinned in memory and upon leaving the function, it is unpinned.
 * This degrades performance significantly.
 * After the parameters are fixed, we can safely free the destination
 * variable and re-initialize it to nil.
 *
 */

/*
 * The type dispatching table in getArgReference can be removed if we
 * determine at compile time the address offset within a ValRecord.
 * We leave this optimization for the future, it leads to about 10%
 * improvement (100ms for 1M calls).
 *
 * Flow of control statements
 * Each assignment (function call) may be part of the initialization
 * of a barrier- block. In that case we have to test the
 * outcome of the operation and possibly skip the block altogether.
 * The latter is implemented as a linear scan for the corresponding
 * labeled statemtent. This might be optimized later.
 *
 * You can skip to a catch block by searching for the corresponding 'lab'
 * The return value should be set to pass the error automatically upon
 * reaching end of function block.
 */

/*
 * Each time we enter a barrier block, we could keep its position in the
 * interpreter stack frame. It forms the starting point to issue a redo.
 * Unfortunately, this does not easily work in the presence of optimizers, which
 * may change the order/block structure. Therefore, we simple have to search
 * the beginning or ensure that during chkProgram the barrier/redo/leave/catch
 * jumps are re-established.
 *
 * Exception handling
 * Calling a built-in or user-defined routine may lead to an error or a
 * cached status message to be dealt with in MAL.
 * To improve error handling in MAL, an exception handling
 * scheme based on @sc{catch}-@sc{exit} blocks. The @sc{catch}
 * statement identifies a (string-valued) variable, which carries the
 * exception message from
 * the originally failed routine or @sc{raise} exception assignment.
 * During normal processing @sc{catch}-@sc{exit} blocks are simply skipped.
 * Upon receiving an exception status from a function call, we set the
 * exception variable and skip to the first associated @sc{catch}-@sc{exit}
 * block.
 * MAL interpretation then continues until it reaches the end of the block.
 * If no exception variable was defined, we should abandon the function
 * alltogether searching for a catch block at a higher layer.
 *
 * For the time being we have ignored cascaded/stacked exceptions.
 * The policy is to pass the first recognized exception to a context
 * in which it can be handled.
 *
 * Exceptions raised within a linked-in function requires some care.
 * First, the called procedure does not know anything about the MAL
 * interpreter context. Thus, we need to return all relevant information
 * upon leaving the linked library routine.
 *
 * Second, exceptional cases can be handled deeply in the recursion, where they
 * may also be handled, i.e. by issueing an GDKerror message. The upper layers
 * merely receive a negative integer value to indicate occurrence of an
 * error somewhere in the calling sequence.
 * We then have to also look into GDKerrbuf to see if there was
 * an error raised deeply inside the system.
 *
 * The policy is to require all C-functions to return a string-pointer.
 * Upon a successfull call, it is a NULL string. Otherwise it contains an
 * encoding of the exceptional state encountered. This message
 * starts with the exception identifer, followed by contextual details.
 */
str catchKernelException(Client cntxt, str ret)
{
	str z;
	char *errbuf = GDKerrbuf;
	(void) cntxt;
	if (errbuf && errbuf[0]) {
		if (ret != MAL_SUCCEED) {
			z = (char*)GDKmalloc(strlen(ret) + strlen(errbuf) + 2);
			if (z) {
				strcpy(z, ret);
				if (z[strlen(z) - 1] != '\n') strcat(z, "\n");
				strcat(z, errbuf);
			}
		} else {
			/* trap hidden (GDK) exception */
			z = (char*)GDKmalloc(strlen("GDKerror:") + strlen(errbuf) + 2);
			if (z)
				sprintf(z, "GDKerror:%s\n", errbuf);
		}
		/* did we eat the error away of not */
		if (z)
			errbuf[0] = '\0';
	} else
		z = ret;
	return z;
}

/*
 * Garbage collection
 * Garbage collection is relatively straightforward, because most values are
 * retained on the stackframe of an interpreter call. However, two storage
 * types and possibly user-defined type garbage collector definitions
 * require attention: BATs and strings.
 *
 * A key issue is to deal with temporary BATs in an efficient way.
 * References to bats in the buffer pool may cause dangling references
 * at the language level. This appears as soons as your share
 * a reference and delete the BAT from one angle. If not carefull, the
 * dangling pointer may subsequently be associated with another BAT
 *
 * All string values are private to the VALrecord, which means they
 * have to be freed explicitly before a MAL function returns.
 * The first step is to always safe the destination variable
 * before a function call is made.
 */
void garbageElement(Client cntxt, ValPtr v)
{
	(void) cntxt;
	if (v->vtype == TYPE_str) {
		if (v->val.sval) {
			GDKfree(v->val.sval);
			v->val.sval = NULL;
		}
		v->len = 0;
	} else if (v->vtype == TYPE_bat) {
		/*
		 * All operations are responsible to properly set the
		 * reference count of the BATs being produced or destroyed.
		 * The libraries should not leave the
		 * physical reference count being set. This is only
		 * allowed during the execution of a GDK operation.
		 * All references should be logical.
		 */
		bat bid = ABS(v->val.bval);
		/* printf("garbage collecting: %d lrefs=%d refs=%d\n",
		   bid, BBP_lrefs(bid),BBP_refs(bid));*/
		v->val.bval = 0;
		if (bid == 0)
			return;
		if (!BBP_lrefs(bid))
			return;
		BBPdecref(bid, TRUE);
	} else if (0 < v->vtype && v->vtype < TYPE_any && ATOMextern(v->vtype)) {
		if (v->val.pval)
			GDKfree(v->val.pval);
		v->val.pval = 0;
		v->len = 0;
	}
}
/*
 *
 * Before we return from the interpreter, we should free all
 * dynamically allocated objects and adjust the BAT reference counts.
 * Early experience shows that for small stack frames the overhead
 * is about 200 ms for a 1M function call loop (tst400e). This means that
 * for the time being we do not introduce more complex garbage
 * administration code.
 *
 * Also note that for top-level stack frames (no environment available),
 * we should retain the value stack because it acts as a global variables.
 * This situation is indicated by the 'global' in the stack frame.
 * Upon termination of the session, the stack should be cleared.
 * Beware that variables may be know polymorphic, their actual
 * type should be saved for variables that recide on a global
 * stack frame.
 */
void garbageCollector(Client cntxt, MalBlkPtr mb, MalStkPtr stk, int flag)
{
	int k;
	ValPtr v;

#ifdef STACKTRACE
	mnstr_printf(cntxt->fdout, "#--->stack before garbage collector\n");
	printStack(cntxt->fdout, mb, stk, 0);
#endif
	for (k = 0; k < mb->vtop; k++) {
		if (isVarCleanup(mb, k) && (flag || isTmpVar(mb, k))) {
			garbageElement(cntxt, v = &stk->stk[k]);
			v->vtype = TYPE_int;
			v->val.ival = int_nil;
		}
	}
#ifdef STACKTRACE
	mnstr_printf(cntxt->fdout, "#-->stack after garbage collector\n");
	printStack(cntxt->fdout, mb, stk, 0);
#else
	(void)cntxt;
#endif
}
/*
 * Sometimes it helps to release a BAT when it won;t be used anymore.
 * In this case, we have to assure that all references are cleared
 * as well. The routine below performs this action in the local
 * stack frame and its parents only.
 */
void releaseBAT(MalBlkPtr mb, MalStkPtr stk, int bid)
{
	int k;

	do {
		for (k = 0; k < mb->vtop; k++)
			if (stk->stk[k].vtype == TYPE_bat && abs(stk->stk[k].val.bval) == bid) {
				stk->stk[k].val.ival = 0;
				BBPdecref(bid, TRUE);
			}
		if (stk->up) {
			stk = stk->up;
			mb = stk->blk;
		} else
			break;
	} while (stk);
}
