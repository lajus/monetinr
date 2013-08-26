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
#include "opt_pushselect.h"
#include "mal_interpreter.h"	/* for showErrors() */

static InstrPtr
PushArgument(MalBlkPtr mb, InstrPtr p, int arg, int pos)
{
	int i;

	p = pushArgument(mb, p, arg); /* push at end */
	for (i = p->argc-1; i > pos; i--) 
		getArg(p, i) = getArg(p, i-1);
	getArg(p, pos) = arg;
	return p;
}

static InstrPtr
RemoveArgument(InstrPtr p, int pos)
{
	int i;

	p->argc--;
	for (i = pos; i < p->argc; i++) 
		getArg(p, i) = getArg(p, i+1);
	return p;
}


#define MAX_TABLES 64

typedef struct subselect_t {
	int nr;
	int tid[MAX_TABLES];
	int subselect[MAX_TABLES];
} subselect_t;

static int
subselect_add( subselect_t *subselects, int tid, int subselect )
{
	int i;

	for (i = 0; i<subselects->nr; i++) {
		if (subselects->tid[i] == tid) {
			if (subselects->subselect[i] == subselect) 
				return i;
			else
				return -1;
		}
	}
	if (i >= MAX_TABLES)
		return -1;
	subselects->nr++;
	subselects->tid[i] = tid;
	subselects->subselect[i] = subselect;
	return i;
}

static int
subselect_find_tids( subselect_t *subselects, int subselect)
{
	int i;

	for (i = 0; i<subselects->nr; i++) {
		if (subselects->subselect[i] == subselect) {
			return subselects->tid[i];
		}
	}
	return -1;
}

static int
subselect_find_subselect( subselect_t *subselects, int tid)
{
	int i;

	for (i = 0; i<subselects->nr; i++) {
		if (subselects->tid[i] == tid) {
			return subselects->subselect[i];
		}
	}
	return -1;
}

int
OPTpushselectImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int i, j, limit, slimit, actions=0, *vars, push_down_delta = 0, nr_topn = 0, nr_likes = 0;
	InstrPtr p, *old;
	subselect_t subselects;

	subselects.nr = 0;
	if( mb->errors) 
		return 0;

	OPTDEBUGpushselect
		mnstr_printf(cntxt->fdout,"#Range select optimizer started\n");
	(void) stk;
	(void) pci;
        vars= (int*) GDKmalloc(sizeof(int)* mb->vtop);
	limit = mb->stop;
	slimit= mb->ssize;
	old = mb->stmt;

	/* check for bailout conditions */
	for (i = 1; i < limit; i++) {
		p = old[i];

		for (j = 0; j<p->retc; j++) {
 			int res = getArg(p, j);
			vars[res] = i;
		}

		if (getModuleId(p) == algebraRef && 
			(getFunctionId(p) == tintersectRef || getFunctionId(p) == tdifferenceRef)) 
			return 0;

		if (getModuleId(p) == algebraRef && getFunctionId(p) == sliceRef)
			nr_topn++;

		if (isLikeOp(p))
			nr_likes++;

		if (getModuleId(p) == sqlRef && getFunctionId(p) == deltaRef)
			push_down_delta++;

		if (getModuleId(p) == sqlRef && getFunctionId(p) == tidRef) { /* rewrite equal table ids */
			int sname = getArg(p, 2), tname = getArg(p, 3), s;

			for (s = 0; s < subselects.nr; s++) {
				InstrPtr q = old[vars[subselects.tid[s]]];
				int Qsname = getArg(q, 2), Qtname = getArg(q, 3);

				if ((sname == Qsname && tname == Qtname) ||
				    (0 && strcmp(getVarConstant(mb, sname).val.sval, getVarConstant(mb, Qsname).val.sval) == 0 &&
				     strcmp(getVarConstant(mb, tname).val.sval, getVarConstant(mb, Qtname).val.sval) == 0)) {
					clrFunction(p);
					p->retc = 1;
					p->argc = 2;
					getArg(p, 1) = getArg(q, 0);
					break;
				}
			}
		}
		if (isSubSelect(p) && p->retc == 1 &&
		   /* no cand list */ getArgType(mb, p, 2) != newBatType(TYPE_oid, TYPE_oid)) {
			int i1 = getArg(p, 1), tid = 0;
			InstrPtr q = old[vars[i1]];

			/* find the table ids */
			while(!tid) {
				if (getModuleId(q) == algebraRef && getFunctionId(q) == leftfetchjoinRef) {
					int i1 = getArg(q, 1);
					InstrPtr s = old[vars[i1]];
	
					if (getModuleId(s) == sqlRef && getFunctionId(s) == tidRef) 
						tid = getArg(q, 1);
					break;
				} else if (isMapOp(q) && q->argc >= 2 && isaBatType(getArgType(mb, q, 1))) {
					int i1 = getArg(q, 1);
					q = old[vars[i1]];
				} else if (isMapOp(q) && q->argc >= 3 && isaBatType(getArgType(mb, q, 2))) {
					int i2 = getArg(q, 2);
					q = old[vars[i2]];
				} else {
					break;
				}
			}
			if (tid && subselect_add(&subselects, tid, getArg(p, 0)) < 0) {
				GDKfree(vars);
				return 0;
			}
		}
	}

	if ((!subselects.nr && !nr_topn && !nr_likes) || newMalBlkStmt(mb, mb->ssize+20) <0 ) {
		GDKfree(vars);
		return 0;
	}
	pushInstruction(mb,old[0]);

	for (i = 1; i < limit; i++) {
		p = old[i];

		/* rewrite batstr.like + subselect -> likesubselect */
		if (getModuleId(p) == algebraRef && p->retc == 1 && getFunctionId(p) == subselectRef) { 
			int var = getArg(p, 1);
			InstrPtr q = mb->stmt[vars[var]]; /* BEWARE: the optimizer may not add or remove statements ! */

			if (isLikeOp(q)) { /* TODO check if getArg(p, 3) value == TRUE */
				InstrPtr r = newInstruction(mb, ASSIGNsymbol);
				int has_cand = (getArgType(mb, p, 2) == newBatType(TYPE_oid, TYPE_oid)); 
				int a, anti = (getFunctionId(q)[0] == 'n'), ignore_case = (getFunctionId(q)[anti?4:0] == 'i');

				setModuleId(r, algebraRef);
				setFunctionId(r, likesubselectRef);
				getArg(r,0) = getArg(p,0);
				r = pushArgument(mb, r, getArg(q, 1));
				if (has_cand)
					r = pushArgument(mb, r, getArg(p, 2));
				for(a = 2; a<q->argc; a++)
					r = pushArgument(mb, r, getArg(q, a));
				if (r->argc < (4+has_cand))
					r = pushStr(mb, r, ""); /* default esc */ 
				if (r->argc < (5+has_cand))
					r = pushBit(mb, r, ignore_case);
				if (r->argc < (6+has_cand))
					r = pushBit(mb, r, anti);
				freeInstruction(p);
				p = r;
				actions++;
			}
		}

		/* inject table ids into subselect 
		 * s = subselect(c, C1..) => subselect(c, t, C1..)
		 */
		if (isSubSelect(p) && p->retc == 1) { 
			int tid = 0;

			if ((tid = subselect_find_tids(&subselects, getArg(p, 0))) >= 0) {
				p = PushArgument(mb, p, tid, 2);
				/* make sure to resolve again */
				p->token = ASSIGNsymbol; 
				p->typechk = TYPE_UNKNOWN;
        			p->fcn = NULL;
        			p->blk = NULL;
				actions++;
			}
		}
		/* Leftfetchjoins involving rewriten table ids need to be flattend
		 * l = leftfetchjoin(t, c); => l = c;
		 * and
		 * l = leftfetchjoin(s, ntids); => l = s;
		 */
		else if (getModuleId(p) == algebraRef && getFunctionId(p) == leftfetchjoinRef) {
			int var = getArg(p, 1);
			
			if (subselect_find_subselect(&subselects, var) > 0) {
				InstrPtr q = newAssignment(mb);

				getArg(q, 0) = getArg(p, 0); 
				q = pushArgument(mb, q, getArg(p, 2));
				actions++;
				freeInstruction(p);
				continue;
			} else { /* deletes/updates use table ids */
				int var = getArg(p, 2);
				InstrPtr q = mb->stmt[vars[var]]; /* BEWARE: the optimizer may not add or remove statements ! */

				if (q->token == ASSIGNsymbol) {
					var = getArg(q, 1);
					q = mb->stmt[vars[var]]; 
				}
				if (subselect_find_subselect(&subselects, var) > 0) {
					InstrPtr q = newAssignment(mb);

					getArg(q, 0) = getArg(p, 0); 
					q = pushArgument(mb, q, getArg(p, 1));
					actions++;
					freeInstruction(p);
					continue;
				}
				/* c = sql.delta(b,uid,uval,ins);
		 		 * l = leftfetchjoin(x, c); 
		 		 * into
		 		 * l = sql.projectdelta(x,b,uid,uval,ins);
		 		 */
				else if (getModuleId(q) == sqlRef && getFunctionId(q) == deltaRef && q->argc == 5) {
					q = copyInstruction(q);
					setFunctionId(q, projectdeltaRef);
					getArg(q, 0) = getArg(p, 0); 
					q = PushArgument(mb, q, getArg(p, 1), 1);
					freeInstruction(p);
					p = q;
					actions++;
				}
			}
		}
		pushInstruction(mb,p);
	}
	for (; i<limit; i++) 
		if (old[i])
			pushInstruction(mb,old[i]);
	for (; i<slimit; i++) 
		if (old[i])
			freeInstruction(old[i]);
	GDKfree(old);
	if (!push_down_delta) {
		GDKfree(vars);
		return actions;
	}

	/* now push selects through delta's */
	limit = mb->stop;
	slimit= mb->ssize;
	old = mb->stmt;

	if (newMalBlkStmt(mb, mb->ssize+(5*push_down_delta)) <0 ) {
		mb->stmt = old;
		GDKfree(vars);
		return actions;

	}
	pushInstruction(mb,old[0]);

	for (i = 1; i < limit; i++) {
		p = old[i];

		for (j = 0; j<p->retc; j++) {
 			int res = getArg(p, j);
			vars[res] = i;
		}

		/* c = delta(b, uid, uvl, ins)
		 * s = subselect(c, C1..)
		 *
		 * nc = subselect(b, C1..)
		 * ni = subselect(ins, C1..)
		 * nu = subselect(uvl, C1..)
		 * s = subdelta(nc, uid, nu, ni);
		 */
		if (isSubSelect(p) && p->retc == 1) {
			int var = getArg(p, 1);
			InstrPtr q = old[vars[var]];

			if (q->token == ASSIGNsymbol) {
				var = getArg(q, 1);
				q = old[vars[var]]; 
			}
			if (getModuleId(q) == sqlRef && getFunctionId(q) == deltaRef) {
				InstrPtr r = copyInstruction(p);
				InstrPtr s = copyInstruction(p);
				InstrPtr t = copyInstruction(p);
				InstrPtr u = copyInstruction(q);
		
				getArg(r, 0) = newTmpVariable(mb, newBatType(TYPE_oid, TYPE_oid));
				getArg(r, 1) = getArg(q, 1); /* column */
				pushInstruction(mb,r);
				getArg(s, 0) = newTmpVariable(mb, newBatType(TYPE_oid, TYPE_oid));
				getArg(s, 1) = getArg(q, 3); /* updates */
				RemoveArgument(s, 2); 	/* no candidate list on updates */
				/* make sure to resolve again */
				s->token = ASSIGNsymbol; 
				s->typechk = TYPE_UNKNOWN;
        			s->fcn = NULL;
        			s->blk = NULL;
				pushInstruction(mb,s);
				getArg(t, 0) = newTmpVariable(mb, newBatType(TYPE_oid, TYPE_oid));
				getArg(t, 1) = getArg(q, 4); /* inserts */
				pushInstruction(mb,t);

				setFunctionId(u, subdeltaRef);
				getArg(u, 0) = getArg(p,0);
				getArg(u, 1) = getArg(r,0);
				getArg(u, 2) = getArg(q,2); /* update ids */
				getArg(u, 3) = getArg(s,0);
				getArg(u, 4) = getArg(t,0);
				pushInstruction(mb,u);	
				freeInstruction(p);
				continue;
			}
		}
		pushInstruction(mb,p);
	}
	for (; i<limit; i++) 
		if (old[i])
			pushInstruction(mb,old[i]);
	GDKfree(vars);
	GDKfree(old);
	return actions;
}
