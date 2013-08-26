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
 * @f sql_optimizer
 * @t SQL catalog management
 * @a N. Nes, M.L. Kersten
 */
/*
 * The queries are stored in the user cache after they have been
 * type checked and optimized.
 * The Factory optimizer encapsulates the query with a re-entrance
 * structure. However, this structure is only effective if
 * quite some (expensive) instructions can be safed.
 * The current heuristic is geared at avoiding trivial
 * factory structures.
 */
#include "monetdb_config.h"
#include "mal_builder.h"
#include "mal_debugger.h"
#include "opt_prelude.h"
#include "sql_mvc.h"
#include "sql_optimizer.h"
#include "sql_scenario.h"
#include "sql_gencode.h"
#include "opt_pipes.h"

#define TOSMALL 10

/*
 * Cost-based optimization and semantic evaluations require statistics to work with.
 * They should come from the SQL catalog or the BATs themselves.
 * The properties passed at this point are the number of rows.
 * A better way is to mark all BATs used as a constant, because that permits
 * access to all properties. However, this creates unnecessary locking during stack
 * initialization. Therfore, we store the BAT id as a property for the optimizer
 * to work with. It can pick up the BAT if needed.
 *
 * Care should be taken in marking the delta bats as empty, because their
 * purpose is to fill them during the query. Therefore, we keep track
 * of all bound tables and mark them not-empty when a direct update
 * takes place using append().
 *
 * We also reduce the number of bind operations by keeping track
 * of those seen already.  This can not be handled by the
 * common term optimizer, because the first bind has a side-effect.
 */

static int
BATlocation(str *fnme, int *bid)
{
	/* this function was formerly ATTlocation in removed file
	 * monetdb5/modules/mal/attach.c */
	BAT *b = BBPquickdesc(*bid, FALSE);
	char path[BUFSIZ], *s;

	if (b == NULL)
		return 0;

	snprintf(path, BUFSIZ, "%s%c", GDKgetenv("gdk_dbpath"), DIR_SEP);
	GDKfilepath(path + strlen(path), BATDIR,
		    (b->T->heap.filename ? b->T->heap.filename: b->H->heap.filename), 0);
	s = strrchr(path, '.');
	if (s)
		*s = 0;
	*fnme = GDKstrdup(path);
	return 1;
}

static void
SQLgetStatistics(Client cntxt, mvc *m, MalBlkPtr mb)
{
	InstrPtr *old = NULL;
	int oldtop, i, actions = 0, size = 0;
	lng clk = GDKusec();
	sql_trans *tr = m->session->tr;

	old = mb->stmt;
	oldtop= mb->stop;
	size = (mb->stop *1.2 < mb->ssize)? mb->ssize: (int) (mb->stop *1.2);
	mb->stmt = (InstrPtr *) GDKzalloc(size  * sizeof(InstrPtr));
	mb->ssize = size ;
	mb->stop = 0;

	for(i=0; i<oldtop; i++){
		InstrPtr p = old[i];
		char *f = getFunctionId(p);

		if( getModuleId(p) == sqlRef &&
		    (f == bindRef || f == bindidxRef || f == binddbatRef ) ){
			ValRecord vr;
			int upd = (p->argc == 7 || p->argc == 9);
			char *sname = getVarConstant(mb, getArg(p,2+upd)).val.sval;
			char *tname = getVarConstant(mb, getArg(p,3+upd)).val.sval;
			char *cname = NULL;
			int not_null = 0;
			wrd rows = 1; /* default to cope with delta bats */
			int mode = 0;
			int k = getArg(p,0);
			sql_schema *s = mvc_bind_schema(m, sname);
			BAT *b;

			if (!s || strcmp(s->base.name, dt_schema) == 0) {
				pushInstruction(mb,p);
				continue;
			}
			if (f == binddbatRef) {
				mode = getVarConstant(mb, getArg(p,4)).val.ival;
			} else {
				cname = getVarConstant(mb, getArg(p,4+upd)).val.sval;
				mode = getVarConstant(mb, getArg(p,5+upd)).val.ival;
			}

			if (s && f == bindidxRef && cname) {
				size_t cnt;
				sql_idx *i = mvc_bind_idx(m, s, cname);

				/* skip alter on remote statements */
				if (i && (!isRemote(i->t) && !isMergeTable(i->t))) {
					cnt = store_funcs.count_idx(tr, i, 1);
					assert(cnt <= (size_t) GDK_oid_max);
					b = store_funcs.bind_idx(m->session->tr,i,0);
					if ( b ) {
						str loc;
						if (b->batPersistence == PERSISTENT && BATlocation(&loc,&b->batCacheid) && loc)
							varSetProp(mb, k, fileProp, op_eq, VALset(&vr, TYPE_str, loc));
						cnt = BATcount(b);
						BBPreleaseref(b->batCacheid);
					}
					rows = (wrd) cnt;
				}
			} else if (s && f == bindRef && cname) {
				size_t cnt;
				sql_table *t = mvc_bind_table(m, s, tname);
				sql_column *c = mvc_bind_column(m, t, cname);

				if (c && (!isRemote(c->t) && !isMergeTable(c->t))) {
					not_null = !c->null;

					cnt = store_funcs.count_col(tr, c, 1);
					assert(cnt <= (size_t) GDK_oid_max);
					b = store_funcs.bind_col(m->session->tr,c,0);
					if ( b ){
						str loc;
						if (b->batPersistence == PERSISTENT &&  BATlocation(&loc,&b->batCacheid) && loc)
							varSetProp(mb, k, fileProp, op_eq, VALset(&vr, TYPE_str, loc));
						cnt = BATcount(b);
						BBPreleaseref(b->batCacheid);
					}
					rows = (wrd) cnt;
				}
			} else if (s && f == binddbatRef) {
				size_t cnt;
				sql_table *t = mvc_bind_table(m, s, tname);
				sql_column *c = NULL;

				if (t->columns.set->h) {
					c = t->columns.set->h->data;

					cnt = store_funcs.count_col(tr, c, 1);
					rows = (wrd) cnt;
				}
			}
			if (rows > 1 && mode != RD_INS)
				varSetProp(mb, k, rowsProp, op_eq, VALset(&vr, TYPE_wrd, &rows));
			if (not_null)
				varSetProp(mb, k, notnilProp, op_eq, NULL);

			{
				int lowprop = hlbProp, highprop = hubProp;
				/* rows == cnt has been checked above to be <= GDK_oid_max */
				oid low = 0, high = low + (oid)rows;
				pushInstruction(mb, p);

				if (mode == RD_INS) {
					if (f != binddbatRef)
						low = high;
					high += 1024*1024;
				}
				if (f == binddbatRef) {
					lowprop = tlbProp;
					highprop = tubProp;
				}
				varSetProp(mb, getArg(p,0), lowprop, op_gte, VALset(&vr, TYPE_oid, &low));
				varSetProp(mb, getArg(p,0), highprop, op_lt, VALset(&vr, TYPE_oid, &high));
			}

			if (not_null)
				actions++;
		} else {
			pushInstruction(mb,p);
		}
	}
	GDKfree(old);
	optimizerCheck(cntxt,mb,"optimizer.SQLgetstatistics",actions,GDKusec()-clk,0);
}
/*
 * Optimizers steps are identified by a pipeline name. The default pipeline in the distribution has been
 * tested extensively and should provide overall good performance.
 * Additional pipelines are defined in the opt_pipes.mx file.
 *
 */
static str optimizerpipe;		/* the active pipeline */

str
initSQLoptimizer(void)
{
	char *pipe;

	/* do nothing if the pipe line is already set */
	if (optimizerpipe == NULL ){
		pipe = GDKgetenv("sql_optimizer");
		if ( pipe == NULL)
			optimizerpipe = GDKstrdup("default_pipe");
		else optimizerpipe= GDKstrdup(pipe);
	} 
	return GDKstrdup(optimizerpipe);
}

void
addOptimizers(Client c, MalBlkPtr mb, char *pipe)
{
	int i;
	InstrPtr q;
	backend *be;

	be = (backend *) c->sqlcontext;
	assert( be && be->mvc ); 	/* SQL clients should always have their state set */

	addOptimizerPipe(c, mb, pipe? pipe:"default_pipe");
	/* point queries do not require mitosis and dataflow */
	if ( be->mvc->point_query)
	for( i = mb->stop -1; i > 0; i--){
		q= getInstrPtr(mb,i);
		if (q->token == ENDsymbol)
			break;
		if ( getFunctionId(q) == mitosisRef || getFunctionId(q) == dataflowRef)
			q->token = REMsymbol;	/* they are ignored */
	}
}

void
addQueryToCache(Client c)
{
	MalBlkPtr mb;
	mvc *m;
	ValRecord *val;
	backend *be;
	str msg = 0;

	be = (backend *) c->sqlcontext;
	assert( be && be->mvc ); 	/* SQL clients should always have their state set */
	val = stack_get_var(be->mvc,"optimizer");

	insertSymbol(c->nspace, c->curprg);
	trimMalBlk(c->curprg->def);
	c->blkmode = 0;
	mb = c->curprg->def;
	chkProgram(c->fdout, c->nspace, mb);
	m = ((backend *)c->sqlcontext)->mvc;
#ifdef _SQL_OPTIMIZER_DEBUG
	mnstr_printf(GDKout, "ADD QUERY TO CACHE\n");
	printFunction(GDKout,mb,0,LIST_MAL_ALL);
#endif
	/*
	 * An error in the compilation should be reported to the user.
	 * And if the debugging option is set, the debugger is called
	 * to allow inspection.
	 */
	if (mb->errors) {
		showErrors(c);

		if (c->listing)
			printFunction(c->fdout, mb,0, c->listing);
		if ( m->debug )
			runMALDebugger(c,c->curprg);
		return;
	}
	addOptimizers(c, mb, val->val.sval);
	SQLgetStatistics(c,m,mb);
	if ( m->emod & mod_debug )
		addtoMalBlkHistory(mb,"getStatistics");

	msg = optimizeMALBlock(c,mb);
	if (msg != MAL_SUCCEED) {
		showScriptException(c->fdout, mb, 0, MAL, "%s", msg);
		return;
	}

	/* time to execute the optimizers */
	if( c->debug)
		optimizerCheck(c,mb,"sql.baseline",-1,0, OPT_CHECK_ALL);
#ifdef _SQL_OPTIMIZER_DEBUG
	mnstr_printf(GDKout, "ADD optimized QUERY TO CACHE\n");
	printFunction(GDKout,mb,0,LIST_MAL_ALL);
#endif
}

/*
 * The default SQL optimizer performs a limited set of operations
 * that are known to be (reasonably) stable and effective.
 * Finegrained control over the optimizer steps is available thru
 * setting the corresponding SQL variable.
 *
 * This version simply runs through the MAL script and re-orders the instructions
 * into catalog operations, query graph, and result preparation.
 * This distinction is used to turn the function into a factory, which would
 * enable re-entry when used as a cache-optimized query.
 * The second optimization is move access mode changes on the base tables
 * to the front of the plan.
 *
 *
 */
str
SQLoptimizer(Client c)
{
	(void) c;
#ifdef _SQL_OPTIMIZER_DEBUG
	mnstr_printf(GDKout, "SQLoptimizer\n");
	printFunction(c->fdout, c->curprg->def,0, LIST_MAL_STMT | LIST_MAPI);
	mnstr_printf(GDKout, "done\n");
#endif
	return MAL_SUCCEED;
}
