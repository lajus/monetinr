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
 * @v 0.0
 * @+ Factories
 * A convenient programming construct is the co-routine, which
 * is specified as an ordinary function, but maintains its
 * own state between calls, and permits re-entry other than
 * by the first statement.
 *
 * The random generator example is used to illustrate its definition and use.
 * @example
 * factory random(seed:int,limit:int):int;
 *     rnd:=seed;
 *     lim:= limit;
 * barrier lim;
 *     leave lim:= lim-1;
 *     rnd:= rnd*125;
 *     yield rnd:= rnd % 32676;
 *     redo lim;
 * exit lim;
 * end random;
 * @end example
 *
 * The first time this factory is called, a @emph{plant}
 * is created in the local system to handle the requests.
 * The plant contains the stack frame and synchronizes access.
 *
 * In this case it initializes the generator.
 * The random number is generated and @sc{yield}  as
 * a result of the call. The factory plant is then put to sleep.
 * The second call received by the factory wakes it up at the
 * point where it went to sleep. In this case it will
 * find a @sc{redo} statement and produces the next random number.
 * Note that also in this case a seed and limit value are
 * expected, but they are ignored in the body.
 * This factory can be called upon to generate at most 'limit'
 * random numbers using the 'seed' to initialize the generator.
 * Thereafter it is being removed, i.e. reset to the original state.
 *
 * A cooperative group of factories can be readily constructed.
 * For example, assume we would like the
 * random factories to respond to both @sc{random(seed,limit)}
 * and @sc{random()}. This can be defined as follows:
 * @example
 * factory random(seed:int,limit:int):int;
 *     rnd:=seed;
 *     lim:= limit;
 * barrier lim;
 *     leave lim:= lim-1;
 *     rnd:= rnd*125;
 *     yield rnd:= rnd % 32676;
 *     redo lim;
 * exit lim;
 * end random;
 *
 * factory random():int;
 * barrier forever:=true;
 *     yield random(0,0);
 *     redo forever;
 * exit forever;
 * end random;
 * @end example
 *
 * @- Factory Ownership
 * For simple cases, e.g. implementation of a random function,
 * it suffices to ensure that the state is secured between calls.
 * But, in a database context there are multiple clients
 * active. This means we have to be more precise on the relationship
 * between a co-routine and the client for which it works.
 *
 * The co-routine concept researched in Monet 5 is the notion of a 'factory',
 * which consists of 'factory plants' at possibly different locations and
 * with different policies to handle requests.
 * Factory management is limited to its owner, which is derived from the module
 * in which it is placed. By default Admin is the owner of all modules.
 *
 * The factory produces elements for multiple clients.
 * Sharing the factory state or even remote processing is
 * up to the factory owner.
 * They are set through properties for the factory plant.
 *
 * The default policy is to instantiate one shared
 * plant for each factory. If necessary, the factory
 * can keep track of a client list to differentiate
 * the states.
 * A possible implementation would be:
 * @example
 * factory random(seed:int,clientid:int):int;
 *     clt:= bat.new(:int,:int);
 *     bat.insert(clt,clientid,seed);
 * barrier always:=true;
 *     rnd:= algebra.find(clt,clientid);
 * catch   rnd; #failed to find client
 *     bat.insert(clt,clientid,seed);
 *     rnd:= algebra.find(clt,clientid);
 * exit    rnd;
 *     rnd:= rnd * 125;
 *     rnd:= rnd % 32676;
 *     algebra.replace(clt,clientid,rnd);
 *     yield rnd;
 *     redo always;
 * exit always;
 * end random;
 * @end example
 *
 * The operators to built client aware factories are,
 * @sc{factories.getCaller()}, which returns a client
 * index, @sc{factories.getModule()} and @sc{factories.getFunction()},
 * which returns the identity of scope enclosed.
 *
 * To illustrate, the client specific random generator
 * can be shielded using the factory:
 * @example
 * factory random(seed:int):int;
 * barrier always:=true;
 *     clientid:= factories.getCaller();
 *     yield user.random(seed, clientid);
 *     redo always;
 * exit always;
 * end random;
 * @end example
 *
 * @- Complex Factories
 * The factory scheme can be used to model
 * a volcano-style query processor. Each node in the query
 * tree is an iterator that calls upon the operands to produce
 * a chunk, which are combined into a new  chunk for consumption
 * of the parent. The prototypical join(R,S) query illustrates it.
 * The plan does not test for all boundary conditions, it merely
 * implements a nested loop. The end of a sequence is identified
 * by a NIL chunk.
 *
 * @example
 * factory query();
 *     Left:= sql.bind("relationA");
 *     Right:= sql.bind("relationB");
 *     rc:= sql.joinStep(Left,Right);
 * barrier rc!= nil;
 *     io.print(rc);
 *     rc:= sql.joinStep(Left,Right);
 *     redo rc!= nil;
 * exit rc;
 * end query;
 *
 * #nested loop join
 * factory sql.joinStep(Left:bat[:any,:any],Right:bat[:any,:any]):bat[:any,:any];
 *     lc:= bat.chunkStep(Left);
 * barrier outer:= lc != nil;
 *     rc:= bat.chunkStep(Right);
 *     barrier inner:= rc != nil;
 *         chunk:= algebra.join(lc,rc);
 *         yield chunk;
 *         rc:= bat.chunkStep(Right);
 *         redo inner:= rc != nil;
 *     exit inner;
 *     lc:= bat.chunkStep(Left);
 *     redo outer:= lc != nil;
 * exit outer;
 *     # we have seen everything
 *     return nil;
 * end joinStep;
 *
 * #factory for left branch
 * factory chunkStepL(L:bat[:any,:any]):bat[:any,:any];
 *     i:= 0;
 *     j:= 20;
 *     cnt:= algebra.count(L);
 * barrier outer:= j<cnt;
 *     chunk:= algebra.slice(L,i,j);
 *     i:= j;
 *     j:= i+ 20;
 *     yield chunk;
 *     redo loop:= j<cnt;
 * exit outer;
 *     # send last portion
 *     chunk:= algebra.slice(L,i,cnt);
 *     yielD chunk;
 *     return nil;
 * end chunkStep;
 *
 * #factory for right leg
 * factory chunkStepR(L:bat[:any,:any]):bat[:any,:any];
 * @end example
 *
 * So far we haven't re-used the pattern that both legs are
 * identical. This could be modeled by a generic chunk factory.
 * Choosing a new factory for each query steps reduces the
 * administrative overhead.
 *
 * @- Materialized Views
 * An area where factories might be useful are support
 * for materialized views, i.e. the result of a query
 * is retained for ease of access.
 * A simple strategy is to prepare the result once
 * and return it on each successive call.
 * Provided the arguments have not been changed.
 * For example:
 *
 * @example
 * factory view1(l:int, h:int):bat[:oid,:str];
 *     a:bat[:oid,:int]:= bbp.bind("emp","age");
 *     b:bat[:oid,:str]:= bbp.bind("emp","name");
 * barrier always := true;
 *     lOld := l;
 *     hOld := h;
 *     c := algebra.select(a,l,h);
 *     d := algebra.semijoin(b,c);
 * barrier available := true;
 *     yield d;
 *     leave available := calc.!=(lOld,l);
 *     leave available := calc.!=(hOld,h);
 *     redo available := true;
 * exit available;
 *     redo always;
 * exit always;
 * end view1;
 * @end example
 *
 * The code should be extended to also check validity of the BATs.
 * It requires a check against the last transaction identifier known.
 *
 * The Factory concept is still rather experimental and many
 * questions should be considered, e.g.
 * What is the lifetime of a factory? Does it persists after
 * all clients has disappeared?
 * What additional control do you need? Can you throw an
 * exception to a Factory?
 *
 */
/*
 * @-
 * The initial implementation is geared at a central
 * factory plant manager, which is called to forward
 * any factory call to their proper destination.
 *
 * The factory plants are collected in a global,
 * limited table for now.
 */
#include "monetdb_config.h"
#include "mal_factory.h"

typedef struct {
	int id;			/* unique plant number */
	MalBlkPtr factory;
	MalStkPtr stk;		/* private state */
	int pc;			/* where we are */
	int inuse;		/* able to handle it */
	int next;		/* next plant of same factory */
	int policy;		/* flags to control behavior */

	Client client;		/* who called it */
	MalBlkPtr caller;	/* from routine */
	MalStkPtr env;		/* with the stack  */
	InstrPtr pci;		/* with the instruction */
} PlantRecord, *Plant;

#define MAXPLANTS 256
static PlantRecord plants[MAXPLANTS];
static int lastPlant= 0;
static int plantId = 1;

mal_export Plant newPlant(MalBlkPtr mb);

int
factoryHasFreeSpace(void){
	return lastPlant <MAXPLANTS-1;
}
static int
findPlant(MalBlkPtr mb){
	int i;
	for(i=0; i<lastPlant; i++)
	if( plants[i].factory == mb)
		return i;
	return -1;
}
str
runFactory(Client cntxt, MalBlkPtr mb, MalBlkPtr mbcaller, MalStkPtr stk, InstrPtr pci)
{
	Plant pl=0;
	int firstcall= TRUE, i, k;
	InstrPtr psig = getInstrPtr(mb, 0);
	ValPtr lhs, rhs;
	char cmd;
	str msg;

#ifdef DEBUG_MAL_FACTORY
	mnstr_printf(cntxt->fdout, "#factoryMgr called\n");
#endif
	/* the lookup can be largely avoided by handing out the index
	   upon factory definition. todo
		Alternative is to move them to the front
	 */
	for(i=0; i< lastPlant; i++)
	if( plants[i].factory == mb){
		if(i > 0 && i< lastPlant ){
			PlantRecord prec= plants[i-1];
			plants[i-1] = plants[i];
			plants[i]= prec;
			i--;
		}
		pl= plants+i;
		firstcall= FALSE;
		break;
	}
	if (pl == 0) {
		/* compress the plant table*/
		for(k=i=0;i<=lastPlant; i++)
		if( plants[i].inuse)
			plants[k++]= plants[i];
		lastPlant = k;
		/* initialize a new plant using the owner policy */
		pl = newPlant(mb);
		if (pl == NULL)
			throw(MAL, "factory.new", MAL_MALLOC_FAIL);
	}
	/*
	 * @-
	 * We have found a factory to process the request.
	 * Let's call it as a synchronous action, without concern on parallelism.
	 */
	/* remember context */
	pl->client = cntxt;
	pl->caller = mbcaller;
	pl->env = stk;
	pl->pci = pci;
	pl->inuse = 1;
	/* inherit debugging */
	cmd = stk->cmd;
	if ( pl->stk == NULL)
			throw(MAL, "factory.new", "internal error, stack frame missing");

	/* copy the calling arguments onto the stack
	   of the factory */
	i = psig->retc;
	for (k = pci->retc; i < pci->argc; i++, k++) {
		lhs = getArgReference(pl->stk,psig,k);
		/* variable arguments ? */
		if (k == psig->argc - 1)
			k--;

		rhs = &pl->env->stk[getArg(pci, i)];
		VALcopy(lhs, rhs);
		if( lhs->vtype == TYPE_bat )
			BBPincref(lhs->val.bval, TRUE);
	}
	if (mb->errors)
		throw(MAL, "factory.call", PROGRAM_GENERAL);
	if (firstcall ){
		/* initialize the stack */
		for(i= psig->argc; i< mb->vtop; i++) {
			lhs = &pl->stk->stk[i];
			if( isVarConstant(mb,i) > 0 ){
				if( !isVarDisabled(mb,i)){
					rhs = &getVarConstant(mb,i);
					VALcopy(lhs,rhs);
				}
			} else{
				lhs->vtype = getVarGDKType(mb,i);
				lhs->val.pval = 0;
				lhs->len = 0;
			}
		}
		pl->stk->stkbot= mb->vtop;	/* stack already initialized */
		msg = runMAL(cntxt, mb, 0, pl->stk);
	 } else {
		msg = reenterMAL(cntxt, mb, pl->pc, -1, pl->stk);
	}
	/* propagate change in debugging status */
	if (cmd && pl->stk && pl->stk->cmd != cmd && cmd != 'x')
		for (; stk; stk = stk->up)
			stk->cmd = pl->stk->cmd;
	return msg;
}
/*
 * @-
 * The shortcut operator for factory calls assumes that the user is
 * not interested in the results produced.
 */
str
callFactory(Client cntxt, MalBlkPtr mb, ValPtr argv[], char flag){
	Plant pl;
	InstrPtr psig = getInstrPtr(mb, 0);
	int i;
	ValPtr lhs,rhs;
	MalStkPtr stk;
	str ret;

	i= findPlant(mb);
	if( i< 0) {
		/* first call? prepare the factory */
		pl = newPlant(mb);
		if (pl == NULL)
			throw(MAL, "factory.call", MAL_MALLOC_FAIL);
		/* remember context, which does not exist. */
		pl->client = cntxt;
		pl->caller = 0;
		pl->env = 0;
		pl->pci = 0;
		pl->inuse = 1;
		stk = pl->stk;
		/* initialize the stack */
		stk->stktop= mb->vtop;
		stk->stksize= mb->vsize;
		stk->blk= mb;
		stk->up = 0;
		stk->cmd= flag;
		/* initialize the stack */
		for(i= psig->argc; i< mb->vtop; i++)
		if( isVarConstant(mb,i) > 0 ){
			lhs = &stk->stk[i];
			rhs = &getVarConstant(mb,i);
			VALcopy(lhs,rhs);
		} else {
			lhs = &stk->stk[i];
			lhs->vtype = getVarGDKType(mb,i);
		}
		pl->stk= stk;
	} else  {
		pl= plants+i;
		/*
		 * @-
		 * When you re-enter the factory the old arguments should be
		 * released to make room for the new ones.
		 */
		for (i = psig->retc; i < psig->argc; i++) {
			lhs = getArgReference(pl->stk,psig,i);
			if( lhs->vtype == TYPE_bat )
				BBPdecref(lhs->val.bval, TRUE);
		}
	}
	/* copy the calling arguments onto the stack of the factory */
	i = psig->retc;
	for (i = psig->retc; i < psig->argc; i++) {
		lhs = getArgReference(pl->stk,psig,i);
		VALcopy(lhs, argv[i]);
		if( lhs->vtype == TYPE_bat )
			BBPincref(lhs->val.bval, TRUE);
	}
	ret=  reenterMAL(cntxt, mb, pl->pc, -1, pl->stk);
	/* garbage collect the string arguments, these positions
	   will simply be overwritten the next time.
	for (i = psig->retc; i < psig->argc; i++)
		garbageElement(lhs = getArgReference(pl->stk,psig,i));
	*/
	return ret;
}
/*
 * @-
 * A new plant is constructed. The properties of the factory
 * should be known upon compile time. They are retrieved from
 * the signature of the factory definition.
 */
Plant
newPlant(MalBlkPtr mb)
{
	Plant p, plim;
	MalStkPtr stk;

	plim = plants + lastPlant;
	for (p = plants; p < plim && p->factory; p++)
		;
	stk = newGlobalStack(mb->vsize);
	if (lastPlant == MAXPLANTS || stk == NULL)
		return 0;
	if (p == plim)
		lastPlant++;
	p->factory = mb;
	p->id = plantId++;

	p->pc = 1;		/* where we start */
	p->stk = stk;
	p->stk->blk = mb;
	p->stk->keepAlive = TRUE;
	return p;
}

/*
 * @-
 * Upon reaching the yield operator, the factory is
 * suspended until the next request arrives.
 * The information in the target list should be delivered
 * to the caller stack frame.
 */
int
yieldResult(MalBlkPtr mb, InstrPtr p, int pc)
{
	Plant pl, plim = plants + lastPlant;
	ValPtr lhs, rhs;
	int i;

	(void) p;
	(void) pc;
	for (pl = plants; pl < plim; pl++)
		if (pl->factory == mb ) {
			if( pl->env == NULL)
				return(int) (pl-plants);
			for (i = 0; i < p->retc; i++) {
#ifdef DEBUG_MAL_FACTORY
				printf("lhs %d rhs %d\n", getArg(pl->pci, i), getArg(p, i));
#endif
				rhs = &pl->stk->stk[getArg(p, i)];
				lhs = &pl->env->stk[getArg(pl->pci, i)];
				VALcopy(lhs, rhs);
			}
			return (int) (pl-plants);
		}
	return -1;
}

str
yieldFactory(MalBlkPtr mb, InstrPtr p, int pc)
{
	Plant pl;
	int i;

	i = yieldResult(mb, p, pc);

	if (i>=0) {
		pl = plants+i;
		pl->pc = pc + 1;
		pl->client = NULL;
		pl->caller = NULL;
		pl->pci = NULL;
		pl->env = NULL;
		return MAL_SUCCEED;
	}
	throw(MAL, "factory.yield", RUNTIME_OBJECT_MISSING);
}

/*
 * @-
 * A return from a factory body implies removal of
 * all state information.
 * This code should also prepare for handling factories
 * that are still running threads in parallel.
 */

str
shutdownFactory(Client cntxt, MalBlkPtr mb)
{
	Plant pl, plim;

	plim = plants + lastPlant;
	for (pl = plants; pl < plim; pl++)
		if (pl->factory == mb) {
			/* MSresetVariables(mb, pl->stk, 0);*/
			/* freeStack(pl->stk); there may be a reference?*/
			/* we are inside the body of the factory and about to return */
			pl->factory = 0;
			if (pl->stk)
				pl->stk->keepAlive = FALSE;
			if ( pl->stk) {
				garbageCollector(cntxt, mb, pl->stk,TRUE);
				GDKfree(pl->stk);
			}
			pl->stk=0;
			pl->pc = 0;
			pl->inuse = 0;
			pl->client = NULL;
			pl->caller = NULL;
			pl->pci = NULL;
			pl->env = NULL;
			pl->client = NULL;
			pl->caller = NULL;
			pl->env= NULL;
			pl->pci = NULL;
		}
	return MAL_SUCCEED;
}

str
shutdownFactoryByName(Client cntxt, Module m, str nme){
	Plant pl, plim;
	InstrPtr p;
	Symbol s;

	plim = plants + lastPlant;
	for (pl = plants; pl < plim; pl++)
		if (pl->factory ) {
			MalStkPtr stk;

			p= getInstrPtr(pl->factory,0);
			if( strcmp(nme, getFunctionId(p)) != 0) continue;
			s = findSymbolInModule(m, nme );
			if (s == NULL){
				throw(MAL, "factory.remove",
					OPERATION_FAILED " SQL entry '%s' not found",
					putName(nme, strlen(nme)));
			}
			stk = pl->stk;
			MSresetVariables(cntxt, pl->factory, stk, 0);
			shutdownFactory(cntxt, pl->factory);
			freeStack(stk);
			deleteSymbol(m,s);
			return MAL_SUCCEED;
		}
	return MAL_SUCCEED;
}
str
finishFactory(Client cntxt, MalBlkPtr mb, InstrPtr pp, int pc)
{
	(void) pp;
	(void) pc;
	return shutdownFactory(cntxt, mb);
}

/*
 * @- Enquiry operations.
 * All access to the plant administration is organized here.
 */

