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
 * Event baskets
 * Continuous query processing relies on event baskets
 * passed through a processing pipeline. The baskets
 * are derived from ordinary SQL tables where the delta
 * processing is ignored.
 *
 */

#include "monetdb_config.h"
#include "basket.h"
#ifdef WIN32
#include "winsock2.h"
#endif
#include "mal_builder.h"
#include "opt_prelude.h"

str schema_default = "datacell";
str statusname[6] = { "<unknown>", "init", "paused", "running", "stop", "error" };
str modename[3] = { "<unknown>", "active", "passive" };
str protocolname[4] = { "<unknown>", "TCP", "UDP", "CSV" };

BSKTbasketRec *baskets;   /* the datacell catalog */
int bsktTop = 0, bsktLimit = 0;

/* We have to obtain the precise wall-clock time
 * This is not produced by GDKusec, which returns microseconds
 * since the start of the program.
 * Notice that this routine consumes noticable time.
 */
lng usec(void)
{
	struct timeval tp;

	gettimeofday(&tp, NULL);
	return ((lng) tp.tv_sec) * LL_CONSTANT(1000000) + (lng) tp.tv_usec;
}


/* assume BUFSIZ buffer space */
void
BSKTelements(str nme, str buf, str *schema, str *tbl)
{
	char *c;
	strncpy(buf, nme, BUFSIZ);
	buf[BUFSIZ - 1] = 0;
	c = strchr(buf, (int) '.');
	if (c == 0)
		snprintf(buf, BUFSIZ, "datacell.%s", nme);
	c = strchr(buf, (int) '.');
	*schema = buf;
	*c++ = 0;
	*tbl = c;
}

static int BSKTnewEntry(void)
{
	int i;
	for (i = 1; i < bsktLimit; i++)
		if (baskets[i].name == NULL)
			break;
	if (i < bsktLimit) {
		if (i == bsktTop)
			bsktTop++;
		return i;
	}
	if (bsktLimit == 0) {
		bsktLimit = MAXBSK;
		baskets = (BSKTbasketRec *) GDKzalloc(bsktLimit * sizeof(BSKTbasketRec));
		bsktTop = 1; /* entry 0 is used as non-initialized */
	} else if (bsktTop == bsktLimit) {
		bsktLimit += MAXBSK;
		baskets = (BSKTbasketRec *) GDKrealloc(baskets, bsktLimit * sizeof(BSKTbasketRec));
	}
	return bsktTop++;
}

void
BSKTtolower(char *src)
{
	int i;
	for (i = 0; i < BUFSIZ - 1 && src[i]; i++)
		src[i] = (char) tolower((int) src[i]);
}

int
BSKTlocate(str tbl)
{
	int i;
	char buf[BUFSIZ];

	strncpy(buf, tbl, BUFSIZ);
	BSKTtolower(buf);

	for (i = 1; i < bsktTop; i++)
		if (tbl && baskets[i].name && strcmp(tbl, baskets[i].name) == 0)
			return i;
	/* try prefixing it with datacell */
	snprintf(buf,BUFSIZ,"datacell.%s",tbl);
	BSKTtolower(buf);
	for (i = 1; i < bsktTop; i++)
		if (baskets[i].name && strcmp(buf, baskets[i].name) == 0)
			return i;
	return 0;
}

str
BSKTnewbasket(sql_schema *s, sql_table *t, sql_trans *tr)
{
	int idx, i;
	node *o;
	str msg = MAL_SUCCEED;
	BAT *b;
	sql_column  *c;
	char buf[BUFSIZ];

	MT_lock_set(&mal_contextLock, "register");
	idx = BSKTnewEntry();
	MT_lock_init(&baskets[idx].lock, "register");

	snprintf(buf, BUFSIZ, "%s.%s", s->base.name, t->base.name);
	baskets[idx].name = GDKstrdup(buf);
	baskets[idx].seen = * timestamp_nil;

	baskets[idx].colcount = 0;
	for (o = t->columns.set->h; o; o = o->next)
		baskets[idx].colcount++;
	baskets[idx].cols = GDKzalloc((baskets[idx].colcount + 1) * sizeof(str));
	baskets[idx].primary = GDKzalloc((baskets[idx].colcount + 1) * sizeof(BAT *));
	baskets[idx].errors = BATnew(TYPE_void, TYPE_str, BATTINY);

	i = 0;
	for (o = t->columns.set->h; msg == MAL_SUCCEED && o; o = o->next) {
		c = o->data;
		b = store_funcs.bind_col(tr, c, 0);
		if (b == NULL) {
			MT_lock_unset(&mal_contextLock, "register");
			throw(SQL, "sql.basket", "Can not access descriptor");
		}
		baskets[idx].primary[i] = b;
		baskets[idx].cols[i++] = GDKstrdup(c->base.name);
	}
	MT_lock_unset(&mal_contextLock, "register");
	return MAL_SUCCEED;
}

str
BSKTregister(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	sql_schema  *s;
	sql_table   *t;
	mvc *m = NULL;
	str msg = getSQLContext(cntxt, mb, &m, NULL);
	sql_trans *tr;
	char buf[BUFSIZ], *lsch, *ltbl;
	str tbl;

	BSKTelements(tbl = *(str *) getArgReference(stk, pci, 1), buf, &lsch, &ltbl);
	BSKTtolower(lsch);
	BSKTtolower(ltbl);


	if (msg != MAL_SUCCEED)
		return msg;

	tr = m->session->tr;
	s = mvc_bind_schema(m, lsch);
	if (s == NULL)
		throw(SQL, "datacell.register", "Schema missing");

	t = mvc_bind_table(m, s, ltbl);
	if (t == NULL)
		throw(SQL, "datacell.register", "Table missing '%s'", ltbl);

	/* check double registration */
	if (BSKTlocate(tbl))
		throw(SQL, "datacell.register", "Basket defined twice.");

	return BSKTnewbasket(s, t, tr);
}

int BSKTmemberCount(str tbl)
{
	int idx = BSKTlocate(tbl);
	return baskets[idx].colcount;
}

/*
 * @-
 * The locks are designated towards the baskets.
 * If you can not grab the lock then we have to wait.
 */
str BSKTlock(int *ret, str *tbl, int *delay)
{
	int bskt;

	*ret = 0;
	bskt = BSKTlocate(*tbl);
	if (bskt == 0)
		throw(MAL, "basket.lock", "Could not find the basket");
#ifdef _DEBUG_BASKET
	stream_printf(BSKTout, "lock group %s\n", *tbl);
#endif
	MT_lock_set(&baskets[bskt].lock, "lock basket");
#ifdef _DEBUG_BASKET
	stream_printf(BSKTout, "got  group locked %s\n", *tbl);
#endif
	(void) delay;  /* control spinlock */
	(void) ret;
	*ret = 1;
	return MAL_SUCCEED;
}


str BSKTlock2(int *ret, str *tbl)
{
	int delay = 0;
	return BSKTlock(ret, tbl, &delay);
}

str BSKTunlock(int *ret, str *tbl)
{
	int bskt;

	bskt = BSKTlocate(*tbl);
	if (bskt == 0)
		throw(MAL, "basket.lock", "Could not find the basket");
	*ret = 0;
	MT_lock_unset(&baskets[bskt].lock, "lock basket");
	return MAL_SUCCEED;
}


str
BSKTdrop(int *ret, str *tbl)
{
	int bskt;

	bskt = BSKTlocate(*tbl);
	if (bskt == 0)
		throw(MAL, "basket.drop", "Could not find the basket");
	baskets[bskt].colcount = 0;
	GDKfree(baskets[bskt].name);
	GDKfree(baskets[bskt].cols);
	GDKfree(baskets[bskt].primary);
	baskets[bskt].name = 0;
	baskets[bskt].cols = 0;
	baskets[bskt].primary = 0;

	(void) ret;
	return MAL_SUCCEED;
}

str
BSKTreset(int *ret)
{
	int i;
	for (i = 1; i < bsktLimit; i++)
		if (baskets[i].name)
			BSKTdrop(ret, &baskets[i].name);
	return MAL_SUCCEED;
}
str
BSKTdump(int *ret)
{
	int bskt;

	for (bskt = 0; bskt < bsktLimit; bskt++)
		if (baskets[bskt].name) {
			mnstr_printf(GDKout, "#baskets[%2d] %s columns %d threshold %d window=[%d,%d] time window=[" LLFMT "," LLFMT "] beat " LLFMT " milliseconds events " BUNFMT "\n",
					bskt,
					baskets[bskt].name,
					baskets[bskt].colcount,
					baskets[bskt].threshold,
					baskets[bskt].winsize,
					baskets[bskt].winstride,
					baskets[bskt].timeslice,
					baskets[bskt].timestride,
					baskets[bskt].beat,
					(baskets[bskt].primary[0] ? BATcount(baskets[bskt].primary[0]) : 0));
		}

	(void) ret;
	return MAL_SUCCEED;
}

str
BSKTgrab(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str tbl;
	int bskt, i, k, *ret;
	BAT *b, *bn = 0, *bo = 0, *bs, *v;
	int cnt = 0;
	timestamp start, finish;
	char sbuf[BUFSIZ], *sptr = sbuf, fbuf[BUFSIZ], *fptr = fbuf;

	(void) cntxt;
	(void) mb;
	tbl = *(str *) getArgReference(stk, pci, pci->argc - 1);

	bskt = BSKTlocate(tbl);
	if (bskt == 0)
		throw(MAL, "basket.grab", "Basket not found");
	if (baskets[bskt].colcount != pci->retc)
		throw(MAL, "basket.grab", "Incompatible arguments");

	if (baskets[bskt].timeslice) {
		/* perform time slicing */
		MT_lock_set(&baskets[bskt].lock, "lock basket");

		/* search the first timestamp colum */
		for (k = 0; k < baskets[bskt].colcount; k++)
			if (baskets[bskt].primary[k]->ttype == TYPE_timestamp)
				break;
		if (k == baskets[bskt].colcount)
			throw(MAL, "basket.grab", "Timestamp column missing");

		/* collect all tuples that satisfy seen < t < seen+winsize */
		start = baskets[bskt].seen;
		MTIMEtimestamp_add(&finish, &start, &baskets[bskt].timeslice);
		i = BUFSIZ;
		timestamp_tostr(&sptr, &i, &start);
		timestamp_tostr(&fptr, &i, &finish);
		mnstr_printf(GDKout, "#range %s - %s\n", sbuf, fbuf);

		bo = BATuselect(baskets[bskt].primary[k], &start, &finish);
		baskets[bskt].seen = finish;

		/* remove all those before cutoff time from basket */
		MTIMEtimestamp_add(&start, &start, &baskets[bskt].timestride);
		finish = *timestamp_nil;
		bs = BATselect(baskets[bskt].primary[k], &start, &finish);

		for (i = 0; i < baskets[bskt].colcount; i++) {
			ret = (int *) getArgReference(stk, pci, i);
			b = baskets[bskt].primary[i];
			if (BATcount(bo) == 0)
				bn = BATnew(b->htype, b->ttype, BATTINY);
			else
				bn = BATjoin(BATmirror(bo), b, BUN_NONE);
			*ret = bn->batCacheid;
			BBPkeepref(*ret);

			/* clean out basket */
			bn = BATjoin(BATmirror(bs), b, BUN_NONE);
			b = BATsetaccess(b, BAT_WRITE);
			BATclear(b, TRUE);
			BATins(b, bn, FALSE);
			cnt = (int) BATcount(bn);
			BBPreleaseref(bn->batCacheid);
		}
		BBPreleaseref(bo->batCacheid);
		BBPreleaseref(bs->batCacheid);

		MT_lock_unset(&baskets[bskt].lock, "unlock basket");
	} else if (baskets[bskt].winsize) {
		/* take care of sliding windows */
		MT_lock_set(&baskets[bskt].lock, "lock basket");
		for (i = 0; i < baskets[bskt].colcount; i++) {
			ret = (int *) getArgReference(stk, pci, i);
			b = baskets[bskt].primary[i];

			/* we may be too early, all BATs are aligned */
			if (BATcount(b) < (BUN) baskets[bskt].winsize) {
				MT_lock_unset(&baskets[bskt].lock, "unlock basket");
				throw(MAL, "basket.grab", "too early");
			}

			bn = BATcopy(b, b->htype, b->ttype, TRUE);
			v = BATslice(bn, baskets[bskt].winstride, BATcount(bn));
			b = BATsetaccess(b, BAT_WRITE);
			BATclear(b, TRUE);
			BATins(b, v, FALSE);
			BATsetcount(bn, baskets[bskt].winsize);
			cnt = (int) BATcount(bn);
			BBPunfix(v->batCacheid);
			*ret = bn->batCacheid;
			BBPkeepref(*ret);
		}
		MT_lock_unset(&baskets[bskt].lock, "unlock basket");
	} else {
		/* straight copy of the basket */
		MT_lock_set(&baskets[bskt].lock, "lock basket");
		for (i = 0; i < baskets[bskt].colcount; i++) {
			ret = (int *) getArgReference(stk, pci, i);
			b = baskets[bskt].primary[i];
			bn = BATcopy(b, b->htype, b->ttype, TRUE);
			cnt = (int) BATcount(b);
			BATclear(b, TRUE);
			*ret = bn->batCacheid;
			BBPkeepref(*ret);
		}
		MT_lock_unset(&baskets[bskt].lock, "unlock basket");
	}
	baskets[bskt].cycles++;
	baskets[bskt].events += cnt;
	return MAL_SUCCEED;
}

str
BSKTupdate(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str tbl;
	int bskt, i, j, ret;
	BAT *b, *bn;

	(void) cntxt;
	(void) mb;
	tbl = *(str *) getArgReference(stk, pci, pci->retc);

	bskt = BSKTlocate(tbl);
	if (bskt == 0)
		throw(MAL, "basket.update", "Basket not found");
	if (baskets[bskt].colcount != pci->argc - 2)
		throw(MAL, "basket.update", "Non-matching arguments");

	/* copy the content of the temporary BATs into the basket */
	MT_lock_set(&baskets[bskt].lock, "lock basket");
	for (j = 2, i = 0; i < baskets[bskt].colcount; i++, j++) {
		ret = *(int *) getArgReference(stk, pci, j);
		b = baskets[bskt].primary[i];
		bn = BATdescriptor(ret);
		BATappend(b, bn, TRUE);
		BBPreleaseref(ret);
	}
	MT_lock_unset(&baskets[bskt].lock, "unlock basket");
	return MAL_SUCCEED;
}

InstrPtr
BSKTgrabInstruction(MalBlkPtr mb, str tbl)
{
	int i, j, bskt;
	InstrPtr p;
	BAT *b;

	bskt = BSKTlocate(tbl);
	if (bskt == 0)
		return 0;
	p = newFcnCall(mb, basketRef, grabRef);
	p->argc = 0;
	for (i = 0; i < baskets[bskt].colcount; i++) {
		b = baskets[bskt].primary[i];
		j = newTmpVariable(mb, newBatType(TYPE_oid, b->ttype));
		setVarUDFtype(mb, j);
		setVarFixed(mb, j);
		p = pushArgument(mb, p, j);
	}
	p->retc = p->argc;
	p = pushStr(mb, p, tbl);
	return p;
}

InstrPtr
BSKTupdateInstruction(MalBlkPtr mb, str tbl)
{
	int i, j, bskt;
	InstrPtr p;
	BAT *b;

	bskt = BSKTlocate(tbl);
	if (bskt == 0)
		return 0;
	p = newInstruction(mb, ASSIGNsymbol);
	getArg(p, 0) = newTmpVariable(mb, TYPE_any);
	getModuleId(p) = basketRef;
	getFunctionId(p) = putName("update", 6);
	p = pushStr(mb, p, tbl);
	for (i = 0; i < baskets[bskt].colcount; i++) {
		b = baskets[bskt].primary[i];
		j = newTmpVariable(mb, newBatType(TYPE_oid, b->ttype));
		p = pushArgument(mb, p, j);
	}
	return p;
}

str
BSKTthreshold(int *ret, str *tbl, int *sz)
{
	int bskt;
	bskt = BSKTlocate(*tbl);
	if (bskt == 0)
		throw(MAL, "basket.threshold", "Basket not found");
	if (*sz < 0)
		throw(MAL, "basket.threshold", "Illegal value");
	if (*sz < baskets[bskt].winsize)
		throw(MAL, "basket.threshold", "Threshold smaller than window size");
	baskets[bskt].threshold = *sz;
	*ret = TRUE;
	return MAL_SUCCEED;
}

str
BSKTwindow(int *ret, str *tbl, lng *sz, lng *stride)
{
	int idx;

	idx = BSKTlocate(*tbl);
	if (idx == 0)
		throw(MAL, "basket.window", "Basket not found");
	if (*stride < 0 || *stride > *sz)
		throw(MAL, "basket.window", "Illegal window stride");
	if (*sz < 0)
		throw(MAL, "basket.window", "Illegal window size");
	if (baskets[idx].timeslice)
		throw(MAL, "basket.window", "Ambiguous sliding window, temporal window size already set");

	/* administer the required size */
	baskets[idx].winsize = *sz;
	if (baskets[idx].threshold < *sz)
		baskets[idx].threshold = *sz;
	baskets[idx].winstride = *stride;
	*ret = TRUE;
	return MAL_SUCCEED;
}

str
BSKTtimewindow(int *ret, str *tbl, lng *sz, lng *stride)
{
	int idx;

	idx = BSKTlocate(*tbl);
	if (idx == 0)
		throw(MAL, "basket.window", "Basket not found");
	if (*stride < 0 || *stride > *sz)
		throw(MAL, "basket.window", "Illegal window stride");
	if (*sz < 0)
		throw(MAL, "basket.window", "Illegal window size");
	if (baskets[idx].winsize)
		throw(MAL, "basket.window", "Ambiguous time window, window size already set");

	/* administer the required time window size */
	baskets[idx].timeslice = *sz;
	baskets[idx].timestride = *stride;
	*ret = TRUE;
	return MAL_SUCCEED;
}

str
BSKTbeat(int *ret, str *tbl, lng *sz)
{
	int bskt, tst;
	timestamp ts, tn;
	bskt = BSKTlocate(*tbl);
	if (bskt == 0)
		throw(MAL, "basket.beat", "Basket not found");
	if (*sz < 0)
		throw(MAL, "basket.beat", "Illegal value");
	baskets[bskt].beat = *sz;
	*ret = TRUE;
	(void) MTIMEunix_epoch(&ts);
	(void) MTIMEtimestamp_add(&tn, &baskets[bskt].seen, &baskets[bskt].beat);
	tst = tn.days < ts.days || (tn.days == ts.days && tn.msecs < ts.msecs);
	if (tst)
		throw(MAL, "basket.heat", "too early");
	return MAL_SUCCEED;
}

/* provide a tabular view for inspection */
str
BSKTtable(int *nameId, int *thresholdId, int * winsizeId, int *winstrideId, int *timesliceId, int *timestrideId, int *beatId, int *seenId, int *eventsId)
{
	BAT *name = NULL, *seen = NULL, *events = NULL;
	BAT *threshold = NULL, *winsize = NULL, *winstride = NULL, *beat = NULL;
	BAT *timeslice = NULL, *timestride = NULL;
	int i;

	name = BATnew(TYPE_oid, TYPE_str, BATTINY);
	if (name == 0)
		goto wrapup;
	BATseqbase(name, 0);
	threshold = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (threshold == 0)
		goto wrapup;
	BATseqbase(threshold, 0);
	winsize = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (winsize == 0)
		goto wrapup;
	BATseqbase(winsize, 0);
	winstride = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (winstride == 0)
		goto wrapup;
	BATseqbase(winstride, 0);
	beat = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (beat == 0)
		goto wrapup;
	BATseqbase(beat, 0);
	seen = BATnew(TYPE_oid, TYPE_timestamp, BATTINY);
	if (seen == 0)
		goto wrapup;
	BATseqbase(seen, 0);
	events = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (events == 0)
		goto wrapup;
	BATseqbase(events, 0);

	timeslice = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (timeslice == 0)
		goto wrapup;
	BATseqbase(timeslice, 0);
	timestride = BATnew(TYPE_oid, TYPE_int, BATTINY);
	if (timestride == 0)
		goto wrapup;
	BATseqbase(timestride, 0);

	for (i = 1; i < bsktTop; i++)
		if (baskets[i].name) {
			BUNappend(name, baskets[i].name, FALSE);
			BUNappend(threshold, &baskets[i].threshold, FALSE);
			BUNappend(winsize, &baskets[i].winsize, FALSE);
			BUNappend(winstride, &baskets[i].winstride, FALSE);
			BUNappend(beat, &baskets[i].beat, FALSE);
			BUNappend(seen, &baskets[i].seen, FALSE);
			baskets[i].events = (int) BATcount( baskets[i].primary[0]);
			BUNappend(events, &baskets[i].events, FALSE);
			BUNappend(timeslice, &baskets[i].timeslice, FALSE);
			BUNappend(timestride, &baskets[i].timestride, FALSE);
		}

	BBPkeepref(*nameId = name->batCacheid);
	BBPkeepref(*thresholdId = threshold->batCacheid);
	BBPkeepref(*winsizeId = winsize->batCacheid);
	BBPkeepref(*winstrideId = winstride->batCacheid);
	BBPkeepref(*timesliceId = timeslice->batCacheid);
	BBPkeepref(*timestrideId = timestride->batCacheid);
	BBPkeepref(*beatId = beat->batCacheid);
	BBPkeepref(*seenId = seen->batCacheid);
	BBPkeepref(*eventsId = events->batCacheid);
	return MAL_SUCCEED;
wrapup:
	if (name)
		BBPreleaseref(name->batCacheid);
	if (threshold)
		BBPreleaseref(threshold->batCacheid);
	if (winsize)
		BBPreleaseref(winsize->batCacheid);
	if (winstride)
		BBPreleaseref(winstride->batCacheid);
	if (timeslice)
		BBPreleaseref(timeslice->batCacheid);
	if (timestride)
		BBPreleaseref(timestride->batCacheid);
	if (beat)
		BBPreleaseref(beat->batCacheid);
	if (seen)
		BBPreleaseref(seen->batCacheid);
	if (events)
		BBPreleaseref(events->batCacheid);
	throw(MAL, "datacell.baskets", MAL_MALLOC_FAIL);
}

str
BSKTtableerrors(int *nameId, int *errorId)
{
	BAT  *name, *error;
	BATiter bi;
	BUN p, q;
	int i;
	name = BATnew(TYPE_void, TYPE_str, BATTINY);
	if (name == 0)
		throw(SQL, "baskets.errors", MAL_MALLOC_FAIL);
	error = BATnew(TYPE_void, TYPE_str, BATTINY);
	if (error == 0) {
		BBPreleaseref(name->batCacheid);
		throw(SQL, "baskets.errors", MAL_MALLOC_FAIL);
	}

	for (i = 1; i < bsktTop; i++)
		if (BATcount(baskets[i].errors) > 0) {
			bi = bat_iterator(baskets[i].errors);
			BATloop(baskets[i].errors, p, q)
			{
				str err = BUNtail(bi, p);
				BUNappend(name, &baskets[i].name, FALSE);
				BUNappend(error, err, FALSE);
			}
		}


	BBPkeepref(*nameId = name->batCacheid);
	BBPkeepref(*errorId = error->batCacheid);
	return MAL_SUCCEED;
}
