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
 * @f groupby
 * @a Martin Kersten
 * @v 1.0
 * @+ Group-by support
 * The group-by support module is meant to replace and speedup the kernel grouping routines.
 * The latter was originally designed in a memory constraint setting and an exercise in
 * performing column-wise grouping incrementally. The effect is that these routines are
 * now a major performance hindrances.
 *
 * This module again takes the columnar approach to grouping, but supports for more
 * parallelism in achieving these goals.
 *
 * The target is to support SQL-like group_by operations, which are lists of
 * attributes (reduced by a pivot list) followed by a group aggregate function.
 * Consider the query "select count(*), max(A) from R group by A, B,C." whose code
 * snippet in MAL would become something like:
 * @verbatim
 * _1:bat[:oid,:int]  := sql.bind("sys","r","a",0);
 * _2:bat[:oid,:str]  := sql.bind("sys","r","b",0);
 * _3:bat[:oid,:date]  := sql.bind("sys","r","c",0);
 * ...
 * _9 := algebra.select(_1,0,100);
 * ..
 * grp:bat[:oid,:oid] := groupby.id(_9, _1, _2, _3);
 * grp_4:bat[:oid,:wrd] := groupby.count(_9, _1, _2, _3);
 * grp_5:bat[:oid,:lng] := groupby.max(_9,_2, _3, _1);
 * @end verbatim
 *
 * The id() function merely becomes the old-fashioned oid-based group identification list.
 * This way related values can be obtained from the attribute columns. It can be the input
 * for the count() function, which saves some re-computation.
 *
 * The implementation is optimized for a limited number of groups. The default is
 * to fall back on the old code sequences.
 *
 */
#include "monetdb_config.h"
#include "groupby.h"

/*
 * The implementation is based on a two-phase process. In phase 1, we estimate
 * the number of groups to deal with using column independence.
 * The grouping is performed in parallel over slices of the tables.
 * The final pieces are glued together.
 */
typedef struct{
	BAT *bn;	/* result */
	BAT **cols;
	BUN *estimate; /* number of different values */
	BATiter *iter;
	int last;
	int maxcol;
} AGGRtask;

static AGGRtask*
GROUPcollect( Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci){
	AGGRtask *a;
	int *bid;
	int i,j;
	BAT *b, *bs, *bh = NULL;
	BUN sample;

	(void) mb;
	a= (AGGRtask *) GDKzalloc(sizeof(*a));
	if ( a == NULL)
		return NULL;
	a->cols = (BAT**) GDKzalloc(pci->argc * sizeof(BAT*));
	a->estimate = (BUN *) GDKzalloc(pci->argc * sizeof(BUN));
	a->iter = (BATiter *) GDKzalloc(pci->argc * sizeof(BATiter));
	if ( a->cols == NULL){
		GDKfree(a);
		return NULL;
	}
	a->maxcol= pci->argc;
	for ( i= pci->retc; i< pci->argc; i++,a->last++) {
		bid = (int*) getArgReference(stk,pci,i);
		b = a->cols[a->last]= BATdescriptor(*bid);
		if ( a->cols[a->last] == NULL){
			for(a->last--; a->last>=0; a->last--)
				BBPreleaseref(a->cols[a->last]->batCacheid);
			return NULL;
		}
		sample = BATcount(b) < 2000 ? BATcount(b): 2000;
		bs = BATsample( b, sample);
		if (bs) {
			bh = BAThistogram(bs);
			a->estimate[a->last] = BATcount(bh);
		}
		if ( bs ) BBPreleaseref(bs->batCacheid);
		if ( bh ) BBPreleaseref(bh->batCacheid);
	}
	/* sort the columns by decreasing estimate */
	for (i = 1; i< a->last; i++)
	for( j = i+1; j<a->last; j++)
	if ( a->estimate[i] < a->estimate[j]){
		b= a->cols[i];
		a->cols[i] = a->cols[j];
		a->cols[j] = b;
		sample = a->estimate[i];
		a->estimate[i] = a->estimate[j];
		a->estimate[j] = sample;
	}
#ifdef _DEBUG_GROUPBY_
	for(i=0; i<a->last; i++)
		mnstr_printf(cntxt->fdout,"#group %d estimate "BUNFMT "\n", i, a->estimate[i]);
#endif
	/* get iterator stuff ready as well */
	for(i=0; i<a->last; i++)
		a->iter[i] = bat_iterator(a->cols[i]);

	return a;
}

static void
GROUPdelete(AGGRtask *a){
	for(a->last--; a->last>=0; a->last--)
		BBPreleaseref(a->cols[a->last]->batCacheid);
	GDKfree(a->cols);
	GDKfree(a->estimate);
	GDKfree(a->iter);
	GDKfree(a);
}

str
GROUPid(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);
	AGGRtask *a;
	BAT *bn;

	a = GROUPcollect(cntxt,mb,stk,pci);
	bn = a->bn = BATnew(TYPE_oid,TYPE_wrd,a->estimate[1]);
	if ( bn == NULL) {
		GROUPdelete(a);
		throw(MAL,"groupby.count",MAL_MALLOC_FAIL);
	}

	GROUPdelete(a);
	BBPkeepref(*ret= bn->batCacheid);
	return MAL_SUCCEED;
}

str
GROUPcount(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);
	AGGRtask *a;
	BAT *bn;

	a = GROUPcollect(cntxt,mb,stk,pci);
	bn = a->bn = BATnew(TYPE_oid,TYPE_wrd,a->estimate[1]);
	if ( bn == NULL) {
		GROUPdelete(a);
		throw(MAL,"groupby.count",MAL_MALLOC_FAIL);
	}

	GROUPdelete(a);
	BBPkeepref(*ret= bn->batCacheid);
	return MAL_SUCCEED;
}

str
GROUPmax(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);
	AGGRtask *a;
	BAT *bn;

	a = GROUPcollect(cntxt,mb,stk,pci);
	bn = a->bn = BATnew(TYPE_oid,TYPE_wrd,a->estimate[1]);
	if ( bn == NULL) {
		GROUPdelete(a);
		throw(MAL,"groupby.count",MAL_MALLOC_FAIL);
	}

	GROUPdelete(a);
	BBPkeepref(*ret= bn->batCacheid);
	return MAL_SUCCEED;
}

str
GROUPmin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);
	AGGRtask *a;
	BAT *bn;

	a = GROUPcollect(cntxt,mb,stk,pci);
	bn = a->bn = BATnew(TYPE_oid,TYPE_wrd,a->estimate[1]);
	if ( bn == NULL) {
		GROUPdelete(a);
		throw(MAL,"groupby.count",MAL_MALLOC_FAIL);
	}

	GROUPdelete(a);
	BBPkeepref(*ret= bn->batCacheid);
	return MAL_SUCCEED;
}

str
GROUPavg(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int *ret = (int*) getArgReference(stk,pci,0);
	AGGRtask *a;
	BAT *bn;

	a = GROUPcollect(cntxt,mb,stk,pci);
	bn = a->bn = BATnew(TYPE_oid,TYPE_wrd,a->estimate[1]);
	if ( bn == NULL) {
		GROUPdelete(a);
		throw(MAL,"groupby.count",MAL_MALLOC_FAIL);
	}

	GROUPdelete(a);
	BBPkeepref(*ret= bn->batCacheid);
	return MAL_SUCCEED;
}
