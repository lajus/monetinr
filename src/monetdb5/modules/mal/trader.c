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
 * @f trader
 * @- This module contains primitives for bidding of (sub)-query execution
 * among mservers.
 *
 * trader.makeBid() is used to ask another server to make a bid for a mal function (sub-query) execution.
 *
 * Currently the bid estimate is made using only the recycle cache.
 * ToDo: use the info about server load
 * FactFinder
 * @-
 */
/*
 * @-
 * Bidding described ...
 */
#include "monetdb_config.h"
#include <mal.h>
#include <mal_exception.h>
#include <mal_instruction.h>
#include <mal_module.h>
#include <mal_recycle.h>
#include "trader.h"
#include <time.h>

/*
static lng estimateSavings0(MalBlkPtr mb,  sht bidtype)
{
	(void) mb;
	(void) bidtype;
	return (lng)100;
}
*/

static lng estimateSavings(MalBlkPtr mb, sht bidtype)
{
	MalStkPtr stk = NULL;
	int i, j, k, marked = 0, maxparam = 0;
	ValPtr lhs, rhs;
	InstrPtr p, q;
	lng savedInstr = 0, savedKB = 0;
	static str octopusRef = 0, bindRef = 0, bindidxRef = 0;

	if (octopusRef == 0)
		octopusRef = putName("octopus",7);
	if (bindRef == 0)
		bindRef = putName("bind",4);
	if (bindidxRef == 0)
		bindidxRef = putName("bind_idxbat",11);

	if( recycleBlk == 0 || reusePolicy == 0)
		return 0;

	/* Create a phony exec. stack */
	stk = newGlobalStack(mb->vsize);
	stk->stktop = mb->vtop;
	stk->stkbot = 0;
	stk->blk = mb;

	/* Init symbol table of the phony stack */
	for(i= 0; i< mb->vtop; i++) {
		lhs = &stk->stk[i];
		if( isVarConstant(mb,i) > 0 ){
/*			assert(!isVarCleanup(mb,i)); */
			if( !isVarDisabled(mb,i)){
				rhs = &getVarConstant(mb,i);
				*lhs = *rhs;
				if (rhs->vtype == TYPE_str && rhs->val.sval != 0)
					lhs->val.sval = GDKstrdup(rhs->val.sval);
			}
		} else{
			lhs->vtype = getVarGDKType(mb,i);
			lhs->val.pval = 0;
			lhs->len = 0;
		}
	}
	maxparam = getArg(mb->stmt[0], mb->stmt[0]->argc - 1);
	/* don't compare function parameters */

	for (k = 0; k < mb->stop; k++){
		p = getInstrPtr(mb,k);
		if ( !RECYCLEinterest(p) )
			continue;
		marked++;
		if ( bidtype == BID_TRANS )  /* check only octopus.bind */
			if ( getModuleId(p) != octopusRef ||
				 getFunctionId(p) != bindRef )
				continue;

		/* Match p against the recycle pool */
		for (i = 0; i < recycleBlk->stop; i++){
			q = getInstrPtr(recycleBlk,i);

			if ((getFunctionId(p) != getFunctionId(q)) ||
		   	 (getModuleId(p) != getModuleId(q)))
				continue;

			if (p->argc < q->argc-1) continue;
			/* sub-range instructions can be subsumed from entire table */

			else if (p->argc == q->argc-1) { /* check for exact match */

				if ( bidtype == BID_TRANS ) j = p->retc + 1;
				else j = p->retc;
				for ( ; j < p->argc; j++){
					if (getArg(p,j) <= maxparam) continue;
					if (VALcmp(&stk->stk[getArg(p,j)], &getVarConstant(recycleBlk,getArg(q,j))))
						goto nomatch;
				}

				/* found an exact match - get the results on the stack */
				for( j=0; j<p->retc; j++){
					VALcopy(&stk->stk[getArg(p,j)],
						&getVarConstant(recycleBlk,getArg(q,j)) );
	            }

				if ( bidtype == BID_TRANS )
					savedKB += recycleBlk->profiler[i].wbytes;
				else savedInstr++;
				break;
			}
			else { 	/* check for bind subsumption */
				int nr_part = 0;
				if ( bidtype != BID_TRANS )
					continue;
				for (j = p->retc + 1; j < 6; j++)
					if ( VALcmp(&stk->stk[getArg(p,j)],
						&getVarConstant(recycleBlk, getArg(q,j))) )
					goto nomatch;
				nr_part = * (int*) getVarValue(mb, getArg(p,7));
				savedKB += nr_part?recycleBlk->profiler[i].wbytes/nr_part : 0;
			}

			nomatch:
				continue;
		} 	/* end loop over RP */
	}

	/* clean up the phony stack */
	freeStack(stk);

	if ( bidtype == BID_TRANS )
		return savedKB;
	return (marked? (lng)100*savedInstr/marked: 0) ;
}


str TRADERmakeBid(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	lng *c = (lng *) getArgReference(stk,pci,0);
	str fnname = *(str *) getArgReference(stk,pci,1);
	sht bidtype = *(sht *) getArgReference(stk,pci,2);
	Symbol sym;
	static char fname[BUFSIZ] = "";
	static str biddingLog = NULL;
	stream *s;
	time_t now;
	struct tm *nowtm;
	char timestr[20];

	(void) mb;
    sym = findSymbol(cntxt->nspace, putName("octopus",7), fnname);
    if ( sym == NULL)
        throw(MAL,"trader.makeBid", RUNTIME_SIGNATURE_MISSING "%s", fnname);

    *c = estimateSavings(sym->def, bidtype);

	/* log bidding */
	if ( biddingLog == NULL) {
		sprintf(fname,"%s%cbidding.log", GDKgetenv("gdk_dbpath"), DIR_SEP);
		biddingLog = fname;
	}
	s = append_wastream(biddingLog);
	if (s == NULL )
			throw(MAL,"trader.makeBid", RUNTIME_FILE_NOT_FOUND "%s", biddingLog);
	if (mnstr_errnr(s)) {
			mnstr_close(s);
			throw(MAL,"trader.makeBid", RUNTIME_FILE_NOT_FOUND "%s", biddingLog);
	}
	now = time(NULL);
	nowtm = localtime(&now);
	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", nowtm);
	mnstr_printf(s,"%s\t%s\t%1d\t" LLFMT "\n",timestr, fnname, bidtype, *c);
	close_stream(s);

	return MAL_SUCCEED;
}

str TRADERmakeBids(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	lng *bid;
	str fnname;
	sht bidtype;
	Symbol sym;
	static str octopusRef = NULL;
	int n = pci->argc - pci->retc -1;
	int i;

	(void) mb;

	if (octopusRef == 0)
		octopusRef = putName("octopus",7);

	bidtype = *(sht *) getArgReference(stk,pci,pci->retc);
	for ( i = 0; i < n ; i++){
		fnname = *(str *) getArgReference(stk,pci,i + pci->retc + 1);
		bid = (lng *) getArgReference(stk,pci,i);
	    sym = findSymbol(cntxt->nspace, octopusRef, fnname);
	    if ( sym == NULL)
    	    throw(MAL,"trader.makeBids", RUNTIME_SIGNATURE_MISSING "%s", fnname);

    	*bid = estimateSavings(sym->def, bidtype);
	}

	return MAL_SUCCEED;
}

