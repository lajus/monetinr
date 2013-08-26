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
 * @a M. Ivanova, M. Kersten
 * @f recycle
 * @- The Recycler
 * Just the interface to the recycler.
 * @-
 * The Recycler is a variation of the interpreter
 * which inspects the variable table for alternative results.
 */
/*
 * @-
 */
#include "monetdb_config.h"
#include "mal_interpreter.h"
#include "mal_function.h"
#include "mal_listing.h"
#include "mal_recycle.h"
#include "recycle.h"
#include "algebra.h"

str recycleLog = NULL;

/*
 * @-
 * The recycler is started when the first function is called for its support.
 * Upon exit of the last function, the content of the recycle cache is destroyed.
 */
str
RECYCLEstart(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) pci;
	(void) stk;
	(void) mb;
	(void) cntxt;

	return MAL_SUCCEED;
}


str
RECYCLEstop(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;

	return MAL_SUCCEED;
}


str
RECYCLEresetCMD(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	RECYCLEreset(cntxt, mb,stk,pci);
	return MAL_SUCCEED;
}

static void
RECYCLEdump(stream *s)
{
	int i, incache;
	str msg;
	lng sz, persmem=0;
	ValPtr v;
    Client c;
    lng statements=0, recycled=0, recycleMiss=0, recycleRem=0;
    lng ccCalls=0, ccInstr=0, crdInstr=0;

	if (!recycleBlk) return;

	mnstr_printf(s,"#Recycler  catalog\n");
    mnstr_printf(s,"#admission= %d time ="LLFMT" alpha= %4.3f\n",
                admissionPolicy, recycleTime, recycleAlpha);
    mnstr_printf(s,"#reuse= %d\n", reusePolicy);
    mnstr_printf(s,"#rcache= %d limit= %d memlimit="LLFMT"\n", rcachePolicy, recycleCacheLimit, recycleMemory);
    mnstr_printf(s,"#hard stmt = %d hard var = %d hard mem="LLFMT"\n",
                 HARDLIMIT_STMT, HARDLIMIT_VAR, HARDLIMIT_MEM);

	for(i=0; i< recycleBlk->stop; i++){
#ifdef _DEBUG_CACHE_
                if ( getInstrPtr(recycleBlk,i)->token == NOOPsymbol ) continue;
#endif
		v = &getVarConstant(recycleBlk,getArg(recycleBlk->stmt[i],0));
		if ((v->vtype == TYPE_bat) &&
			 (BBP_status( *(const int*)VALptr(v)) & BBPPERSISTENT)) {
			msg = BKCbatsize(&sz, (int*)VALget(v));
			if ( msg == MAL_SUCCEED )
				persmem += sz;
		}
	}
	persmem = (lng) persmem/RU;

    for(c = mal_clients; c < mal_clients+MAL_MAXCLIENTS; c++)
        if (c->mode != FREECLIENT) {
            recycled += c->rcc->recycled;
            statements += c->rcc->statements;
            recycleMiss += c->rcc->recycleMiss;
            recycleRem += c->rcc->recycleRem;
            ccCalls += c->rcc->ccCalls;
            ccInstr += c->rcc->ccInstr;
            crdInstr += c->rcc->crdInstr;
        };

	incache = recycleBlk->stop;
	mnstr_printf(s,"#recycled = "LLFMT" incache= %d executed = "LLFMT" memory(KB)= "LLFMT" PersBat memory="LLFMT"\n",
		 recycled, incache,statements, recyclerUsedMemory, persmem);
#ifdef _DEBUG_CACHE_
	mnstr_printf(s,"#RPremoved = %d RPactive= %d RPmisses = %d\n",
                 recycleRem, incache-recycleRem, recycleMiss);
#endif
	mnstr_printf(s,"#Cache search time= "LLFMT"(usec) cleanCache: "LLFMT" calls evicted "LLFMT" instructions \t Discarded by CRD "LLFMT"\n",recycleSearchTime, ccCalls,ccInstr, crdInstr);

	/* and dump the statistics per instruction*/
        mnstr_printf(s,"# CL\t   lru\t\tcnt\t ticks\t rd\t wr\t Instr\n");
	for(i=0; i< recycleBlk->stop; i++){
		if (getInstrPtr(recycleBlk,i)->token == NOOPsymbol)
			mnstr_printf(s,"#NOOP ");
		else mnstr_printf(s,"#     ");
		mnstr_printf(s,"%4d\t"LLFMT"\t%d\t"LLFMT"\t"LLFMT"\t"LLFMT"\t%s\n", i,
			recycleBlk->profiler[i].clk,
			recycleBlk->profiler[i].counter,
			recycleBlk->profiler[i].ticks,
			recycleBlk->profiler[i].rbytes,
			recycleBlk->profiler[i].wbytes,
			instruction2str(recycleBlk,0,getInstrPtr(recycleBlk,i),TRUE));
	}

}

static void
RECYCLEdumpQPat(stream *s)
{
	int i;
	QryStatPtr qs;

	if (!recycleQPat) {
		mnstr_printf(s,"#No query patterns\n");
		return;
	}

	mnstr_printf(s,"#Query patterns %d\n",	recycleQPat->cnt);
	mnstr_printf(s,"#RecID\tcalls\tglobRec\tlocRec\tCreditWL\n");
	for(i=0; i< recycleQPat->cnt; i++){
		qs = recycleQPat->ptrn[i];
		mnstr_printf(s,"# "LLFMT"\t%2d\t%2d\t%2d\t%2d\n",
			qs->recid, qs->calls, qs->greuse, qs->lreuse, qs->wl);
	}
}

static void
RECYCLEdumpDataTrans(stream *s)
{
	int i, n;
	lng dt, sum = 0, rdt, rsum = 0;

	if (!recycleBlk || !recycleQPat)
		return;

	n = recycleQPat->cnt;

	mnstr_printf(s,"#Query  \t Data   \t DT Reused\n");
	mnstr_printf(s,"#pattern\t transf.\t from others\n");
	for( i=0; i < n; i++){
		rdt = recycleQPat->ptrn[i]->dtreuse;
        dt = recycleQPat->ptrn[i]->dt;
        mnstr_printf(s,"# %d \t\t "LLFMT"\t\t"LLFMT"\n", i, dt, rdt);
        sum += dt;
        rsum += rdt;
	}
	mnstr_printf(s,"#########\n# Total transfer "LLFMT" Total reused "LLFMT"\n", sum, rsum);
}

str
RECYCLEdumpWrap(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	stream *s = cntxt->fdout;
	str fname;
	int tp;

	(void) mb;

	if (pci->argc >1)
		tp = * (int*) getArgReference(stk, pci,1);
	else tp = 1;

	if (pci->argc >2){
		fname = * (str*) getArgReference(stk, pci,2);
		s = open_wastream(fname);
		if (s == NULL )
			throw(MAL,"recycle.dumpQ", RUNTIME_FILE_NOT_FOUND" %s", fname);
		if (mnstr_errnr(s)){
			mnstr_close(s);
			throw(MAL,"recycle.dumpQ", RUNTIME_FILE_NOT_FOUND" %s", fname);
		}
	}

	switch(tp){
		case 2:	RECYCLEdumpQPat(s);
				break;
		case 3: RECYCLEdumpDataTrans(s);
				break;
		case 1:
		default:RECYCLEdump(s);
	}

	if( s != cntxt->fdout)
		close_stream(s);
	return MAL_SUCCEED;
}


/*
 * @-
 * Called to collect statistics at the end of each query.
 */
static str
RECYCLErunningStat(Client cntxt, MalBlkPtr mb)
{
	static int q=0;
	stream *s;
	InstrPtr p;
	int potrec=0, nonbind=0, i, trans=0;
	lng reusedmem=0;

	if (recycleLog == NULL)
		s = cntxt->fdout;
	else {
		s = append_wastream(recycleLog);
		if (s == NULL || mnstr_errnr(s)) {
			if (s)
				mnstr_destroy(s);
			throw(MAL,"recycle", RUNTIME_FILE_NOT_FOUND ":%s", recycleLog);
		}
	}

	for(i=0; i< mb->stop; i++){
		p = mb->stmt[i];
		if ( RECYCLEinterest(p) ){
			potrec++;
			if ( !isBindInstr(p) ) nonbind++;
			else if ( getModuleId(p) == putName("octopus",7) ) trans++;
		}
	}

	for(i=0; i < recycleBlk->stop; i++)
#ifdef _DEBUG_CACHE_
		if ( getInstrPtr(recycleBlk,i)->token != NOOPsymbol )
#endif
		if ( recycleBlk->profiler[i].counter >1)
			reusedmem += recycleBlk->profiler[i].wbytes;

	mnstr_printf(s,"%d\t %7.2f\t ", ++q, (GDKusec()-cntxt->rcc->time0)/1000.0);
	if ( monitorRecycler & 2) { /* Current query stat */
		mnstr_printf(s,"%3d\t %3d\t %3d\t ", mb->stop, potrec, nonbind);
		mnstr_printf(s,"%3d\t %3d\t ", cntxt->rcc->recycled0, cntxt->rcc->recycled);
		mnstr_printf(s,"|| %3d\t %3d\t ", cntxt->rcc->RPadded0, cntxt->rcc->RPreset0);
		mnstr_printf(s,"%3d\t%5.2f\t"LLFMT"\t"LLFMT"\t", recycleBlk?recycleBlk->stop:0, recycleTime/1000.0,recyclerUsedMemory,reusedmem);
	}

	if ( monitorRecycler & 1) { /* RP stat */
		mnstr_printf(s,"| %4d\t %4d\t ",cntxt->rcc->statements,recycleBlk?recycleBlk->stop:0);
		mnstr_printf(s, LLFMT "\t" LLFMT "\t ", recyclerUsedMemory, reusedmem);
#ifdef _DEBUG_CACHE_
		mnstr_printf(s,"%d\t %d\t ",cntxt->rcc->recycleRem,cntxt->rcc->recycleMiss);
#endif
	}

	if ( monitorRecycler & 4) { /* Data transfer stat */
		mnstr_printf(s,"| %2d\t "LLFMT"\t ",cntxt->rcc->trans, cntxt->rcc->transKB);
		mnstr_printf(s,"%2d\t "LLFMT"\t ",cntxt->rcc->recTrans, cntxt->rcc->recTransKB);
	}

	if ( reusePolicy == REUSE_MULTI )
		mnstr_printf(s, " \t%5.2f \t%5.2f\n", msFindTime/1000.0, msComputeTime/1000.0);
	else mnstr_printf(s,"\n");

	if( s != cntxt->fdout )
		close_stream(s);
	return MAL_SUCCEED;
}


str
RECYCLEsetAdmission(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int crd;
	(void) cntxt;
	(void) mb;

	admissionPolicy = * (int*) getArgReference(stk,p,1);
	if( p->argc > 2 && admissionPolicy >= ADM_INTEREST ){
		crd = * (int*) getArgReference(stk,p,2);
		if ( crd > 0 )
			recycleMaxInterest = crd + REC_MIN_INTEREST;
	}
	return MAL_SUCCEED;
}

str
RECYCLEsetReuse(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) cntxt;
	(void) mb;
	reusePolicy = * (int*) getArgReference(stk, p,1);
	return MAL_SUCCEED;
}

str
RECYCLEsetCache(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) cntxt;
	(void) mb;
	rcachePolicy = * (int*) getArgReference(stk, p, 1);
	if( rcachePolicy && p->argc > 2)
		recycleCacheLimit = * (int*) getArgReference(stk, p, 2);
	if( rcachePolicy && p->argc > 3)
		recycleMemory= * (int*) getArgReference(stk, p, 3);
	if( rcachePolicy && p->argc > 4)
		recycleAlpha = * (flt*) getArgReference(stk, p, 4);
	return MAL_SUCCEED;
}

str
RECYCLEgetAdmission(int *p)
{
	*p = admissionPolicy;
	return MAL_SUCCEED;
}

str
RECYCLEgetReuse(int *p)
{
	*p = reusePolicy;
	return MAL_SUCCEED;
}

str
RECYCLEgetCache(int *p)
{
	*p = rcachePolicy;
	return MAL_SUCCEED;
}

/*
 * @-
 * At the end of the session we have to cleanup the recycle cache.
 */
str
RECYCLEshutdownWrap(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p){

	(void) mb;
	(void) stk;
	(void) p;
	RECYCLEshutdown(cntxt);
	return MAL_SUCCEED;
}
str
RECYCLEmonitor(int *ret, int *p)
{
	(void) ret;
	monitorRecycler = *p;
	return MAL_SUCCEED;
}

str
RECYCLElog(int *ret, str *nm)
{
	stream *s;
	(void) ret;
	recycleLog = GDKstrdup(*nm);
	s = open_wastream(recycleLog);
    if (s){

		mnstr_printf(s,"# Q\t TimeQ(ms)\t");
		if ( monitorRecycler & 2) { /* Current query stat */
			mnstr_printf(s,"InstrQ\t PotRecQ NonBind ");
			mnstr_printf(s,"RecQ\t TotRec\t ");
			mnstr_printf(s,"|| RPadded  RPreset RPtotal ResetTime(ms) RPMem(KB)");
		}

		if ( monitorRecycler & 1) { /* RP stat */
			mnstr_printf(s,"| TotExec\tTotCL\tMem(KB)\tReused\t ");
#ifdef _DEBUG_CACHE_
			mnstr_printf(s,"RPRem\tRPMiss\t ");
#endif
		}

		if ( monitorRecycler & 4) { /* Data transfer stat */
			mnstr_printf(s,"| Trans#\t Trans(KB)\t RecTrans#\t RecTrans(KB)\t ");
		}

		if ( reusePolicy == REUSE_MULTI )
			mnstr_printf(s, "MSFind\t MSCompute\n");
		else mnstr_printf(s,"\n");

		close_stream(s);
	}

	return MAL_SUCCEED;
}

str
RECYCLEprelude(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	(void) stk;
	(void) p;
	cntxt->rcc->recent = -1;
	cntxt->rcc->recycled0 = 0;
	cntxt->rcc->time0 = GDKusec();
  	if (recycleQPat == NULL)
   		RECYCLEinitQPat(20);
	cntxt->rcc->curQ = RECYCLEnewQryStat(mb);
	minAggr = ALGminany;
	maxAggr = ALGmaxany;
	msFindTime = 0;			/* multi-subsume measurements */
	msComputeTime = 0;
	recycleTime = 0;
	cntxt->rcc->trans = cntxt->rcc->recTrans = 0;
	cntxt->rcc->transKB = cntxt->rcc->recTransKB =0;
	cntxt->rcc->RPadded0 = 0;
	cntxt->rcc->RPreset0 = 0;
	return MAL_SUCCEED;
}
str
RECYCLEepilogue(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p){
	(void) mb;
	(void) stk;
	(void) p;
	cntxt->rcc->curQ = -1;
	cntxt->rcc->recycled += cntxt->rcc->recycled0;
	if ( monitorRecycler )
		return RECYCLErunningStat(cntxt,mb);
	return MAL_SUCCEED;
}
