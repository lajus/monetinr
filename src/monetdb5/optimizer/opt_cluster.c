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
 * @a N.J. Nes
 * @- Cluster
 * The simple goal of the cluster optimizer is to reduce the size(s) of hash
 * tables. For example the hash tables in group.new or algebra.join.
 * The goal is reached by partitioning, the grouping columns or both sides
 * of the join.
 * The base decision for clustering stems from the size estimates of xxx optimizer.
 *
 * Example
 */

#include "monetdb_config.h"
#include "opt_cluster.h"
#include "mal_interpreter.h"

#define MAX_STMTS 	64

#define GRP_NEW 	1
#define GRP_DERIVE 	2
#define GRP_DONE 	3
#define AGGR_CNTS 	4
#define AGGR_CNT 	5
#define AGGR_SUM 	6

#define ORDERBY_NONE 	1
#define ORDERBY_SORT 	2
#define ORDERBY_REFINE 	3
#define ORDERBY_MARK 	4
#define ORDERBY_REVERSE	5
#define ORDERBY_JOIN 	6

#define JOIN_NONE 	1
#define JOIN_JOIN 	2
#define JOIN_MARK 	3
#define JOIN_REVERSE 	4
#define JOIN_PRJ 	5

#if 0
static int
cluster_groupby(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i, j = 0;
	InstrPtr q;
	int *grp = (int*) GDKzalloc(sizeof(int) * MAX_STMTS);
	int state = GRP_NEW;
	int h = 0, g = 0, cnts = 0, cntl = 0, cntr = 0;

	(void)cntxt;
	(void)stk;
	(void)p;
	
	/* locate the a sequence of group.new/derive statements */
	for (i=1; i< mb->stop && j < MAX_STMTS && !cntl && !cntr; i++) {
		q = getInstrPtr(mb,i);

		/* TODO check we don't cluster twice */
		if (state == GRP_DERIVE && 
		    q->token == ASSIGNsymbol &&
		    q->argc == 2 &&
		    g == getArg(q,1)) {
			g = getArg(q,0);
		} else if (state == GRP_NEW && 
		    getModuleId(q) == groupRef &&
		    getFunctionId(q) == subgroupRef &&
		    q->argc == 2) {
			state = GRP_DERIVE;
			h = getArg(q,0);
			g = getArg(q,1);
		} else if (state == GRP_NEW && 
		    getModuleId(q) == groupRef &&
		    getFunctionId(q) == subgroupdoneRef &&
		    q->argc == 2) {
			state = GRP_DONE;
			h = getArg(q,0);
			g = getArg(q,1);
		} else if (state == GRP_DERIVE &&
		    getModuleId(q) == groupRef &&
		    getFunctionId(q) == subgroupRef &&
		    q->argc == 4 &&
		    h == getArg(q,2) && g == getArg(q,3)) {
			state = GRP_DERIVE;
			h = getArg(q,0);
			g = getArg(q,1);
		} else if (state == GRP_DERIVE &&
		    getModuleId(q) == groupRef &&
		    getFunctionId(q) == subgroupdoneRef &&
		    q->argc == 4 &&
		    h == getArg(q,2) && g == getArg(q,3)) {
			state = GRP_DONE;
			h = getArg(q,0);
			g = getArg(q,1);
		} else if (state == GRP_DONE &&
		    getModuleId(q) == aggrRef &&
		    q->argc == 4 &&
		    getFunctionId(q) == countRef) {
			state = AGGR_CNTS;
			cnts = getArg(q,0);
		} else if (state == AGGR_CNTS &&
		    getModuleId(q) == aggrRef &&
		    getFunctionId(q) == countRef &&
		    q->argc == 2 &&
		    cnts == getArg(q,1)) {
			cntl = getArg(q,0);
		} else if (state == AGGR_CNTS &&
		    getModuleId(q) == aggrRef &&
		    getFunctionId(q) == sumRef && 
		    q->argc == 2 &&
		    cnts == getArg(q,1)) {
			cntr = getArg(q,0);
		} else
			continue;
		grp[j++] = i;
	}
#if 0
	if (lcnt && rcnt) {
		/* lets cluster */
	}
#endif
	GDKfree(grp);
	return 1;
}
#endif

static int
copyStmts( MalBlkPtr mb, InstrPtr *old, int cur, int s)
{
	for(; cur < s; cur++)
		if (old[cur])
			pushInstruction(mb, copyInstruction(old[cur]));
	/* skip s */
	return s+1;
}

static int
_cluster_orderby(MalBlkPtr mb, int *ord, int ol, int *prj, int pl)
{
	int nr_parts = 16;
	InstrPtr *old = mb->stmt;
	int oldtop, slimit, size;

	oldtop = mb->stop;
	slimit = mb->ssize;
	size = (mb->stop * 1.2 < mb->ssize)? mb->ssize:(int)(mb->stop * 1.2);

	/* for now we cluster only on the first order by column */
	if (ol) { 
		InstrPtr c, q = old[ord[0]], *no, m, s, o;
		int i, j, p, cur = 0;
		int ht = getHeadType(getArgType(mb, q, 1)), 
		    tt = getTailType(getArgType(mb, q, 1)); 
		int bits = 5; 
		int offset = 3; 
		
		/* only cluster for bte,..,lng,flt,dbl atoms */
		if (tt < TYPE_bte || tt > TYPE_dbl || tt == TYPE_oid ||
	           (ht != TYPE_void && ht != TYPE_oid)) 
			return 0;

		if (tt == TYPE_int)
			offset = 19;

		mb->stmt = (InstrPtr *) GDKzalloc(size * sizeof(InstrPtr));
		if (mb->stmt == NULL){
			mb->stmt = old;
			return 0;
		}
		mb->ssize = size;
		mb->stop = 0;

		cur = copyStmts(mb, old, cur, ord[0]);

		/* cluster into 'nr_parts' parts based on the values */
		c = newStmt( mb, "cluster", "new" );
		/* second return (map) value */
        	c = pushReturn(mb, c, newTmpVariable(mb, TYPE_any));
		c = pushArgument( mb, c, getArg(q, 1) );
		c = pushInt( mb, c, bits);
		c = pushInt( mb, c, offset);
		c = pushBit( mb, c, TRUE);	/* Used for sorting */
		
		m = newStmt( mb, "cluster", "map" );
		m = pushArgument( mb, m, getArg(c, 0) );
		m = pushArgument( mb, m, getArg(c, 1) );
		m = pushArgument( mb, m, getArg(q, 1) );

		/* split clustered column */
		s = newStmt( mb, "cluster", "split" );
		/* split returns 'nr_parts' parts */
		for (p = 1; p<nr_parts; p++)
        		s = pushReturn(mb, s, newTmpVariable(mb, TYPE_any));
		s = pushArgument( mb, s, getArg(m, 0) ); 
		s = pushArgument( mb, s, getArg(c, 0) ); /* psum */
		
		/* order these parts then pack2 */
		no = (InstrPtr*)GDKzalloc(sizeof(InstrPtr)*nr_parts);
		o = old[ord[0]];
		for (p = 0; p<nr_parts; p++) {
			no[p] = copyInstruction(o);
			getArg(no[p], 0) = newTmpVariable(mb, TYPE_any); 
			getArg(no[p], 1) = getArg(s, p);
			pushInstruction(mb, no[p]);
		}

		for (j=1; j<ol; j++) {
			InstrPtr nno, o = old[ord[j]];

			cur = copyStmts(mb, old, cur, ord[j]);

			if (getFunctionId(o) == refineRef ||
			    getFunctionId(o) == refine_reverseRef) {

				m = newStmt( mb, "cluster", "map" );
				m = pushArgument( mb, m, getArg(c, 0) );
				m = pushArgument( mb, m, getArg(c, 1) );
				m = pushArgument( mb, m, getArg(o, 2) );
		
				/* split input column first */
				s = newStmt( mb, "cluster", "split" );
				/* split returns 'nr_parts' parts */
				for (p = 1; p<nr_parts; p++)
        				s = pushReturn(mb, s, newTmpVariable(mb, TYPE_any));
				s = pushArgument( mb, s, getArg(m, 0) ); 
				s = pushArgument( mb, s, getArg(c, 0) ); /* psum */
				for (p = 0; p<nr_parts; p++) {
					nno = copyInstruction(o);
					getArg(nno, 0) = newTmpVariable(mb, TYPE_any); 
					getArg(nno, 1) = getArg(no[p], 0);
					getArg(nno, 2) = getArg(s, p);
					pushInstruction(mb, nno);
					no[p] = nno;
				}
			} else {
				for (p = 0; p<nr_parts; p++) {
					nno = copyInstruction(o);
					getArg(nno, 0) = newTmpVariable(mb, TYPE_any); 
					getArg(nno, 1) = getArg(no[p], 0);
					pushInstruction(mb, nno);
					no[p] = nno;
				}
			}
		}

		/* projection join using parts followed by pack2s */
		for(i=0; i < pl; i++) {
			InstrPtr pj, ppj, opj = old[prj[i]];

			cur = copyStmts(mb, old, cur, prj[i]);

			/* if algo 0 pass psum */
			m = newStmt( mb, "cluster", "map" );
			m = pushArgument( mb, m, getArg(c, 0) );
			m = pushArgument( mb, m, getArg(c, 1) );
			m = pushArgument( mb, m, getArg(opj, 2) );
		
			s = newStmt( mb, "cluster", "split" );
			/* split returns 'nr_parts' parts */
			for (p = 1; p<nr_parts; p++)
        			s = pushReturn(mb, s, newTmpVariable(mb, TYPE_any));
			s = pushArgument( mb, s, getArg(m, 0) ); 
			s = pushArgument( mb, s, getArg(c, 0) ); /* psum */

			pj = newInstruction(mb, ASSIGNsymbol);
			setModuleId(pj,matRef);
			setFunctionId(pj,pack2Ref);
			getArg(pj,0) = getArg(opj,0);

			for (p = 0; p<nr_parts; p++) {
				ppj = copyInstruction(opj);
				getArg(ppj, 0) = newTmpVariable(mb, TYPE_any); 
				getArg(ppj, 1) = getArg(no[p], 0);
				getArg(ppj, 2) = getArg(s, p);
				
				pushInstruction(mb, ppj);
				pj = pushArgument(mb, pj, getArg(ppj, 0));
			}
			pushInstruction(mb, pj);
		}
		cur = copyStmts(mb, old, cur, oldtop);
		for(i=0; i<slimit; i++)
			if (old[i]) 
				freeInstruction(old[i]);
		GDKfree(old);
		GDKfree(no);
	}
	return 1;
}

static int
cluster_orderby(MalBlkPtr mb)
{
	int i, j = 0, k = 0, o = 0, state = ORDERBY_NONE, actions = 0;
	int *ord = (int*) GDKzalloc(sizeof(int) * MAX_STMTS);
	int *prj = (int*) GDKzalloc(sizeof(int) * MAX_STMTS);
	InstrPtr q;

	/* TODO only cluster on large inputs */
	/* TODO reuse ordered columns */

	/* locate the a sequence of group.new/derive statements */
	for (i=1; i< mb->stop && j < MAX_STMTS && k < MAX_STMTS; i++) {
		q = getInstrPtr(mb,i);

		if (state == ORDERBY_NONE && 
		    getModuleId(q) == algebraRef &&
			(getFunctionId(q) == sortTailRef ||
			 getFunctionId(q) == sortReverseTailRef) &&
		    q->argc == 2) {
			state = ORDERBY_SORT;
			o = getArg(q,0);
			ord[j++] = i;
			/* TODO + markT + reverse */
		} else if ((state == ORDERBY_SORT || state == ORDERBY_REFINE) &&
		    getModuleId(q) == algebraRef &&
		    getFunctionId(q) == markTRef &&
			/* mark with base */
		    (q->argc == 2 || q->argc == 3) && o == getArg(q,1)) {
			state = ORDERBY_MARK;
			o = getArg(q,0);
			ord[j++] = i;
		} else if (state == ORDERBY_MARK &&
		    getModuleId(q) == batRef &&
		    getFunctionId(q) == reverseRef &&
		    q->argc == 2 && o == getArg(q,1)) {
			state = ORDERBY_JOIN;
			o = getArg(q,0);
			ord[j++] = i;
		} else if ((state == ORDERBY_SORT || state == ORDERBY_REFINE) &&
		    getModuleId(q) == groupRef &&
			(getFunctionId(q) == refineRef ||
			 getFunctionId(q) == refine_reverseRef) &&
		    q->argc == 3 && o == getArg(q,1)) {
			state = ORDERBY_REFINE;
			o = getArg(q,0);
			ord[j++] = i;
		} else if (state == ORDERBY_JOIN &&
		    getModuleId(q) == algebraRef &&
		    getFunctionId(q) == leftjoinRef &&
		    /* has join estimate */
		    q->argc == 4 && o == getArg(q,1)) {
			prj[k++] = i;
		} else
			continue;
	}
	if (k && j && state == ORDERBY_JOIN) 
		actions = _cluster_orderby(mb, ord, j, prj, k);
	GDKfree(ord);
	GDKfree(prj);
	return actions;
}

static int
_cluster_join(MalBlkPtr mb, int *join, int jl, int *prj, int pjl)
{
	int nr_parts = 16;
	InstrPtr *old = mb->stmt;
	int oldtop, slimit, size;

	oldtop = mb->stop;
	slimit = mb->ssize;
	size = (mb->stop * 1.2 < mb->ssize)? mb->ssize:(int)(mb->stop * 1.2);

	/* for now we cluster only on the first join by column */
	if (jl) { 
		InstrPtr cl, cr, sl, sr, q = old[join[0]], *njn, *mr, *rmr, m, s, jn;
		int njn0 = -1, mr0 = -1, rmr0 = -1, p;
		int i, j, cur = 0;
		int ht = getHeadType(getArgType(mb, q, 1)), 
		    tt = getTailType(getArgType(mb, q, 1)); 
		int t2 = getHeadType(getArgType(mb, q, 2)), 
		    h2 = getTailType(getArgType(mb, q, 2)); 
		int bits = 5; 
		int offset = 0; 
		
		/* only cluster for bte,..,lng,flt,dbl atoms */
		if ((tt < TYPE_bte || tt > TYPE_dbl || tt == TYPE_oid ||
	            (ht != TYPE_void && ht != TYPE_oid)) ||
		    (t2 < TYPE_bte || t2 > TYPE_dbl || t2 == TYPE_oid ||
	            (h2 != TYPE_void && h2 != TYPE_oid))) 
			return 0;

		mb->stmt = (InstrPtr *) GDKzalloc(size * sizeof(InstrPtr));
		if ( mb->stmt == NULL){
			mb->stmt = old;
			return 0;
		}
		mb->ssize = size;
		mb->stop = 0;

		cur = copyStmts(mb, old, cur, join[0]);

		/* cluster left col into 'nr_parts' parts based on the values */
		cl = newStmt( mb, "cluster", "new" );
		/* second return (map) value */
        	cl = pushReturn(mb, cl, newTmpVariable(mb, TYPE_any));
		cl = pushArgument( mb, cl, getArg(q, 1) );
		cl = pushInt( mb, cl, bits);
		cl = pushInt( mb, cl, offset);
		cl = pushBit( mb, cl, FALSE);	

		/* map left column */
		m = newStmt( mb, "cluster", "map" );
		m = pushArgument( mb, m, getArg(cl, 0) );
		m = pushArgument( mb, m, getArg(cl, 1) );
		m = pushArgument( mb, m, getArg(q, 1) );

		/* split clustered left column */
		sl = newStmt( mb, "cluster", "split" );
		/* split returns 'nr_parts' parts */
		for (p = 1; p<nr_parts; p++)
        		sl = pushReturn(mb, sl, newTmpVariable(mb, TYPE_any));
		sl = pushArgument( mb, sl, getArg(m, 0) ); 
		sl = pushArgument( mb, sl, getArg(cl, 0) ); /* psum */

		/* cluster right col into 'nr_parts' parts based on the values*/
		s = newStmt2(mb, batRef, reverseRef);
		s = pushArgument( mb, s, getArg(q, 2) );

		cr = newStmt( mb, "cluster", "new" );
		/* second return (map) value */
        	cr = pushReturn(mb, cr, newTmpVariable(mb, TYPE_any));
		cr = pushArgument( mb, cr, getArg(s, 0) );
		cr = pushInt( mb, cr, bits);
		cr = pushInt( mb, cr, offset);
		cr = pushBit( mb, cr, FALSE);
		
		/* map right column */
		m = newStmt( mb, "cluster", "map" );
		m = pushArgument( mb, m, getArg(cr, 0) );
		m = pushArgument( mb, m, getArg(cr, 1) );
		m = pushArgument( mb, m, getArg(s, 0) );

		/* split clustered right column */
		sr = newStmt( mb, "cluster", "split" );
		/* split returns 'nr_parts' parts */
		for (p = 1; p<nr_parts; p++)
        		sr = pushReturn(mb, sr, newTmpVariable(mb, TYPE_any));
		sr = pushArgument( mb, sr, getArg(m, 0) ); 
		sr = pushArgument( mb, sr, getArg(cr, 0) ); /* psum */

		/* join these parts */
		njn0 = getArg(q, 0);
		njn = (InstrPtr*)GDKzalloc(sizeof(InstrPtr)*nr_parts);
		mr = (InstrPtr*)GDKzalloc(sizeof(InstrPtr)*nr_parts);
		rmr = (InstrPtr*)GDKzalloc(sizeof(InstrPtr)*nr_parts);
		jn = old[join[0]];
		for (p = 0; p<nr_parts; p++) {
			InstrPtr r = newStmt2( mb, batRef, reverseRef);

			r = pushArgument(mb, r, getArg(sr, p));

			njn[p] = copyInstruction(jn);
			getArg(njn[p], 0) = newTmpVariable(mb, TYPE_any); 
			getArg(njn[p], 1) = getArg(sl, p);
			getArg(njn[p], 2) = getArg(r, 0);
			pushInstruction(mb, njn[p]);
		}
		/* now handle the reverses and marks for the nr_part parts */
		for (j=1; j<jl; j++) {
			InstrPtr *res = rmr, *in = njn, o = old[join[j]];

			old[join[j]] = NULL;
			/* find mark - reverse and reverse - mark - reverse */
			if (getFunctionId(o) == markTRef &&
			    getArg(o, 1) == njn0) {
				res = mr;
				mr0 = getArg(o, 0);
			}
			if (getFunctionId(o) == reverseRef &&
			    getArg(o, 1) == mr0) {
				in = mr;
				res = mr;
				mr0 = getArg(o, 0);
			}
			if (getFunctionId(o) == reverseRef &&
			    getArg(o, 1) == njn0) {
				res = rmr;
				rmr0 = getArg(o, 0);
			}
			if (getFunctionId(o) == markTRef &&
			    getArg(o, 1) == rmr0) {
				in = rmr;
				res = rmr;
				rmr0 = getArg(o, 0);
			}
			if (getFunctionId(o) == reverseRef &&
			    getArg(o, 1) == rmr0) {
				in = rmr;
				res = rmr;
				rmr0 = getArg(o, 0);
			}
	
			for (p = 0; p<nr_parts; p++) {
				InstrPtr n = copyInstruction(o);
				getArg(n, 0) = newTmpVariable(mb, TYPE_any); 
				getArg(n, 1) = getArg(in[p], 0);
				pushInstruction(mb, n);
				res[p] = n;
			}
			freeInstruction(o);
		}

		/* projection join using parts followed by pack2s */
		for(i=0; i < pjl; i++) {
			InstrPtr pj, ppj, opj = old[prj[i]], c, *jr;

			cur = copyStmts(mb, old, cur, prj[i]);
			c = (mr0 == getArg(opj, 1))? cl : cr;
			jr = (mr0 == getArg(opj, 1))? mr : rmr;

			m = newStmt( mb, "cluster", "map" );
			m = pushArgument( mb, m, getArg(c, 0) );
			m = pushArgument( mb, m, getArg(c, 1) );
			m = pushArgument( mb, m, getArg(opj, 2) );
		
			s = newStmt( mb, "cluster", "split" );
			/* split returns 'nr_parts' parts */
			for (p = 1; p<nr_parts; p++)
        			s = pushReturn(mb, s, newTmpVariable(mb, TYPE_any));
			s = pushArgument( mb, s, getArg(m, 0) ); 
			s = pushArgument( mb, s, getArg(c, 0) ); /* psum */

			pj = newInstruction(mb, ASSIGNsymbol);
			setModuleId(pj,matRef);
			setFunctionId(pj,pack2Ref);
			getArg(pj,0) = getArg(opj,0);

			for (p = 0; p<nr_parts; p++) {
				ppj = copyInstruction(opj); 
				getArg(ppj, 0) = newTmpVariable(mb, TYPE_any); 
				getArg(ppj, 1) = getArg(jr[p], 0);
				getArg(ppj, 2) = getArg(s, p);
				pushInstruction(mb, ppj);
				pj = pushArgument(mb, pj, getArg(ppj, 0));
			}
			pushInstruction(mb, pj);
		}
		cur = copyStmts(mb, old, cur, oldtop);
		for(i=0; i<slimit; i++)
			if (old[i]) 
				freeInstruction(old[i]);
		GDKfree(old);
		GDKfree(njn);
		GDKfree(mr);
		GDKfree(rmr);
	}
	return 1;
}

static int
cluster_join(MalBlkPtr mb)
{
	int i, j = 0, k = 0, jn = 0, mr = -1, rmr = -1, state = JOIN_NONE, state_mr = JOIN_NONE, state_rmr = JOIN_NONE, actions = 0;
	int *join = (int*) GDKzalloc(sizeof(int) * MAX_STMTS);
	int *prj = (int*) GDKzalloc(sizeof(int) * MAX_STMTS);
	InstrPtr q;

	/* locate the a sequence of group.new/derive statements */
	for (i=1; i< mb->stop && j < MAX_STMTS && k < MAX_STMTS; i++) {
		q = getInstrPtr(mb,i);

		if (state == JOIN_NONE && 
		    getModuleId(q) == algebraRef &&
			(getFunctionId(q) == joinRef) &&
		    q->argc == 3) {
			state = JOIN_JOIN;
			state_mr = JOIN_JOIN;
			state_rmr = JOIN_JOIN;
			jn = getArg(q,0);
			join[j++] = i;
		} else if (state == JOIN_JOIN &&
		    getModuleId(q) == algebraRef &&
		    getFunctionId(q) == markTRef &&
			/* mark with base */
		    (q->argc == 2 || q->argc == 3) && jn == getArg(q,1)) {
			state_mr = JOIN_MARK;
			mr = getArg(q,0);
			join[j++] = i;
		} else if (state_mr == JOIN_MARK &&
		    getModuleId(q) == batRef &&
		    getFunctionId(q) == reverseRef &&
		    q->argc == 2 && mr == getArg(q,1)) {
			state_mr = JOIN_PRJ;
			mr = getArg(q,0);
			join[j++] = i;
		} else if (state == JOIN_JOIN &&
		    getModuleId(q) == batRef &&
		    getFunctionId(q) == reverseRef &&
		    q->argc == 2 && jn == getArg(q,1)) {
			state_rmr = JOIN_REVERSE;
			rmr = getArg(q,0);
			join[j++] = i;
		} else if (state_rmr == JOIN_REVERSE &&
		    getModuleId(q) == algebraRef &&
		    getFunctionId(q) == markTRef &&
			/* mark with base */
		    (q->argc == 2 || q->argc == 3) && rmr == getArg(q,1)) {
			state_rmr = JOIN_MARK;
			rmr = getArg(q,0);
			join[j++] = i;
		} else if (state_rmr == JOIN_MARK &&
		    getModuleId(q) == batRef &&
		    getFunctionId(q) == reverseRef &&
		    q->argc == 2 && rmr == getArg(q,1)) {
			state_rmr = JOIN_PRJ;
			rmr = getArg(q,0);
			join[j++] = i;
		/* if a mark is used in a kdifference
			(ie an outer-join implementation) we don't cluster */
		} else if (getModuleId(q) == algebraRef &&
		    	   getFunctionId(q) == kdifferenceRef &&
		    q->argc == 3 && 
			((state_mr == JOIN_MARK && mr == getArg(q,2)) || 
			 (state_rmr == JOIN_MARK && rmr == getArg(q,2)))) {
			return 0;
		} else if (getModuleId(q) == algebraRef &&
		    	   getFunctionId(q) == leftjoinRef &&
		    /* has join estimate */
		    q->argc == 4 && 
			((state_mr == JOIN_PRJ && mr == getArg(q,1)) || 
			 (state_rmr == JOIN_PRJ && rmr == getArg(q,1)))) {
			prj[k++] = i;
		} else
			continue;
	}
	if (k && j && (state_mr == JOIN_PRJ || state_rmr == JOIN_PRJ)) 
		actions = _cluster_join(mb, join, j, prj, k);
	GDKfree(join);
	GDKfree(prj);
	return actions;
}

int
OPTclusterImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int actions = cluster_orderby(mb);

	actions += cluster_join(mb);

	(void)cntxt; (void)stk; (void)p;
	return actions;
}
