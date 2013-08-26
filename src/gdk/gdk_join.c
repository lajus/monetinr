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
#include "gdk.h"
#include "gdk_private.h"
#include "gdk_calc_private.h"

/* Perform a bunch of sanity checks on the inputs to a join. */
static gdk_return
joinparamcheck(BAT *l, BAT *r, BAT *sl, BAT *sr, const char *func)
{
	if (!BAThdense(l) || !BAThdense(r)) {
		GDKerror("%s: inputs must have dense head.\n", func);
		return GDK_FAIL;
	}
	if (l->ttype == TYPE_void || r->ttype == TYPE_void) {
		GDKerror("%s: tail type must not be VOID.\n", func);
		return GDK_FAIL;
	}
	if (l->ttype != r->ttype) {
		GDKerror("%s: inputs not compatible.\n", func);
		return GDK_FAIL;
	}
	if ((sl && !BAThdense(sl)) || (sr && !BAThdense(sr))) {
		GDKerror("%s: candidate lists must have dense head.\n", func);
		return GDK_FAIL;
	}
	if ((sl && ATOMtype(sl->ttype) != TYPE_oid) ||
	    (sr && ATOMtype(sr->ttype) != TYPE_oid)) {
		GDKerror("%s: candidate lists must have OID tail.\n", func);
		return GDK_FAIL;
	}
	if ((sl && !BATtordered(sl)) ||
	    (sr && !BATtordered(sr))) {
		GDKerror("%s: candidate lists must be sorted.\n", func);
		return GDK_FAIL;
	}
	if ((sl && !BATtkey(sl)) ||
	    (sr && !BATtkey(sr))) {
		GDKerror("%s: candidate lists must be unique.\n", func);
		return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

/* Create the result bats for a join. */
static gdk_return
joininitresults(BAT **r1p, BAT **r2p, BUN size, const char *func)
{
	BAT *r1, *r2;

	r1 = BATnew(TYPE_void, TYPE_oid, size);
	r2 = BATnew(TYPE_void, TYPE_oid, size);
	if (r1 == NULL || r2 == NULL) {
		if (r1)
			BBPreclaim(r1);
		if (r2)
			BBPreclaim(r2);
		*r1p = *r2p = NULL;
		GDKerror("%s: cannot create output BATs.\n", func);
		return GDK_FAIL;
	}
	BATseqbase(r1, 0);
	BATseqbase(r2, 0);
	r1->T->nil = 0;
	r1->T->nonil = 1;
	r1->tkey = 1;
	r1->tsorted = 1;
	r1->trevsorted = 1;
	r2->T->nil = 0;
	r2->T->nonil = 1;
	r2->tkey = 1;
	r2->tsorted = 1;
	r2->trevsorted = 1;
	*r1p = r1;
	*r2p = r2;
	return GDK_SUCCEED;
}

#define VALUE(s, x)	(s##vars ? \
			 s##vars + VarHeapVal(s##vals, (x), s##width) : \
			 s##vals + ((x) * s##width))

/* Do a binary search for the first/last occurrence of v between lo and hi
 * (lo inclusive, hi not inclusive) in rvals/rvars.
 * If last is set, return the index of the first value > v; if last is
 * not set, return the index of the first value >= v.
 * If reverse is -1, the values are sorted in reverse order and hence
 * all comparisons are reversed.
 */
static BUN
binsearch(const oid *rcand, oid offset,
	  const char *rvals, const char *rvars,
	  int rwidth, BUN lo, BUN hi, const char *v,
	  int (*cmp)(const void *, const void *), int reverse, int last)
{
	BUN mid;
	int c;

	assert(reverse == 1 || reverse == -1);
	assert(lo < hi);

	--hi;			/* now hi is inclusive */
	if ((c = reverse * cmp(VALUE(r, rcand ? rcand[lo] - offset : lo), v)) > 0 ||
	    (!last && c == 0))
		return lo;
	if ((c = reverse * cmp(VALUE(r, rcand ? rcand[hi] - offset : hi), v)) < 0 ||
	    (last && c == 0))
		return hi + 1;
	/* loop invariant:
	 * last ? value@lo <= v < value@hi : value@lo < v <= value@hi */
	while (hi - lo > 1) {
		mid = (hi + lo) / 2;
		if ((c = reverse * cmp(VALUE(r, rcand ? rcand[mid] - offset : mid), v)) > 0 ||
		    (!last && c == 0))
			hi = mid;
		else
			lo = mid;
	}
	return hi;
}

#define APPEND(b, o)		(((oid *) b->T->heap.base)[b->batFirst + b->batCount++] = (o))

/* Perform a "merge" join on l and r (if both are sorted) with
 * optional candidate lists, or join using binary search on r if l is
 * not sorted.  The return BATs have already been created by the
 * caller.
 */
static gdk_return
mergejoin(BAT *r1, BAT *r2, BAT *l, BAT *r, BAT *sl, BAT *sr, int nil_matches, int nil_on_miss, int semi)
{
	BUN lstart, lend, lcnt;
	const oid *lcand, *lcandend;
	BUN rstart, rend, rcnt, rstartorig;
	const oid *rcand, *rcandend, *rcandorig;
	BUN lscan, rscan;
	const char *lvals, *rvals;
	const char *lvars, *rvars;
	int lwidth, rwidth;
	const void *nil = ATOMnilptr(l->ttype);
	int (*cmp)(const void *, const void *) = BATatoms[l->ttype].atomCmp;
	const char *v, *prev = NULL;
	BUN nl, nr;
	int insert_nil;
	int equal_order;
	int lreverse, rreverse;
	oid lv;
	BUN i;

	ALGODEBUG fprintf(stderr, "#mergejoin(l=%s#" BUNFMT "[%s]%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s,sl=%s#" BUNFMT "%s%s,"
			  "sr=%s#" BUNFMT "%s%s,nil_matches=%d,"
			  "nil_on_miss=%d,semi=%d)\n",
			  BATgetId(l), BATcount(l), ATOMname(l->ttype),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "",
			  sl ? BATgetId(sl) : "NULL", sl ? BATcount(sl) : 0,
			  sl && sl->tsorted ? "-sorted" : "",
			  sl && sl->trevsorted ? "-revsorted" : "",
			  sr ? BATgetId(sr) : "NULL", sr ? BATcount(sr) : 0,
			  sr && sr->tsorted ? "-sorted" : "",
			  sr && sr->trevsorted ? "-revsorted" : "",
			  nil_matches, nil_on_miss, semi);

	assert(BAThdense(l));
	assert(BAThdense(r));
	assert(l->ttype != TYPE_void);
	assert(r->ttype != TYPE_void);
	assert(l->ttype == r->ttype);
	assert(r->tsorted || r->trevsorted);
	assert(sl == NULL || sl->tsorted);
	assert(sr == NULL || sr->tsorted);

	CANDINIT(l, sl, lstart, lend, lcnt, lcand, lcandend);
	CANDINIT(r, sr, rstart, rend, rcnt, rcand, rcandend);
	rcandorig = rcand;
	rstartorig = rstart;
	lvals = (const char *) Tloc(l, BUNfirst(l));
	rvals = (const char *) Tloc(r, BUNfirst(r));
	if (l->tvarsized && l->ttype) {
		assert(r->tvarsized && r->ttype);
		lvars = l->T->vheap->base;
		rvars = r->T->vheap->base;
	} else {
		assert(!r->tvarsized || !r->ttype);
		lvars = rvars = NULL;
	}
	lwidth = l->T->width;
	rwidth = r->T->width;

	/* basic properties will be adjusted if necessary later on,
	 * they were initially set by joininitresults() */

	if (lstart == lend || (!nil_on_miss && rstart == rend)) {
		/* nothing to do: there are no matches */
		return GDK_SUCCEED;
	}

	if (l->tsorted || l->trevsorted) {
		/* determine opportunistic scan window for l */
		for (nl = lcand ? (BUN) (lcandend - lcand) : lend - lstart, lscan = 4;
		     nl > 0;
		     lscan++)
			nl >>= 1;

		/* equal_order is set if we can scan both BATs in the
		 * same order, so when both are sorted or both are
		 * reverse sorted */
		equal_order = l->tsorted == r->tsorted || l->trevsorted == r->trevsorted;
		/* [lr]reverse is either 1 or -1 depending on the
		 * order of l/r: it determines the comparison function
		 * used */
		lreverse = l->tsorted ? 1 : -1;
	} else {
		/* if l not sorted, we will always use binary search
		 * on r */
		lscan = 0;
		equal_order = 1;
		lreverse = 1;
		/* if l not sorted, we only know for sure that r2 is
		 * key if l is, and that r1 is key if r is */
		r2->tkey = l->tkey != 0;
		r1->tkey = r->tkey != 0;

	}
	/* determine opportunistic scan window for r; if l is not
	 * sorted this is only used to find range of equal values */
	for (nl = rcand ? (BUN) (rcandend - rcand) : rend - rstart, rscan = 4;
	     nl > 0;
	     rscan++)
		nl >>= 1;
	rreverse = r->tsorted ? 1 : -1;

	while (lcand ? lcand < lcandend : lstart < lend) {
		if (!nil_on_miss && lscan > 0) {
			/* if next value in r too far away (more than
			 * lscan from current position in l), use
			 * binary search on l to skip over
			 * non-matching values */
			if (lcand) {
				if (lscan < (BUN) (lcandend - lcand) &&
				    lreverse * cmp(VALUE(l, lcand[lscan] - l->hseqbase),
						   (v = VALUE(r, rcand ? rcand[0] - r->hseqbase : rstart))) < 0)
					lcand += binsearch(lcand, l->hseqbase, lvals, lvars, lwidth, lscan, lcandend - lcand, v, cmp, lreverse, 0);
			} else {
				if (lscan < lend - lstart &&
				    lreverse * cmp(VALUE(l, lstart + lscan),
						   (v = VALUE(r, rcand ? rcand[0] - r->hseqbase : rstart))) < 0)
					lstart = binsearch(NULL, 0, lvals, lvars, lwidth, lstart + lscan, lend, v, cmp, lreverse, 0);
			}
		} else if (lscan == 0) {
			/* always search r completely */
			rcand = rcandorig;
			rstart = rstartorig;
		}
		/* v is the value we're going to work with in this
		 * iteration */
		v = VALUE(l, lcand ? lcand[0] - l->hseqbase : lstart);
		nl = 1;
		/* count number of equal values in left */
		if (lcand) {
			while (++lcand < lcandend &&
			       cmp(v, VALUE(l, lcand[0] - l->hseqbase)) == 0)
				nl++;
		} else {
			while (++lstart < lend &&
			       cmp(v, VALUE(l, lstart)) == 0)
				nl++;
		}
		/* lcand/lstart points one beyond the value we're
		 * going to match: ready for the next */
		if (!nil_matches && cmp(v, nil) == 0) {
			/* v is nil and nils don't match anything */
			continue;
		}
		if (equal_order) {
			if (rcand) {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search, but if l is not
				 * sorted (lscan == 0) we'll always do
				 * a binary search */
				if (lscan == 0 ||
				    (rscan < (BUN) (rcandend - rcand) &&
				     rreverse * cmp(v, VALUE(r, rcand[rscan] - r->hseqbase)) > 0)) {
					/* value too far away in r or
					 * l not sorted: use binary
					 * search */
					rcand += binsearch(rcand, r->hseqbase, rvals, rvars, rwidth, lscan == 0 ? 0 : rscan, rcandend - rcand, v, cmp, rreverse, 0);
				} else {
					/* scan r for v */
					while (rcand < rcandend &&
					       rreverse * cmp(v, VALUE(r, rcand[0] - r->hseqbase)) > 0)
						rcand++;
				}
			} else {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search, but if l is not
				 * sorted (lscan == 0) we'll always do
				 * a binary search */
				if (lscan == 0 ||
				    (rscan < rend - rstart &&
				     rreverse * cmp(v, VALUE(r, rstart + rscan)) > 0)) {
					/* value too far away in r or
					 * l not sorted: use binary
					 * search */
					rstart = binsearch(NULL, 0, rvals, rvars, rwidth, rstart + (lscan == 0 ? 0 : rscan), rend, v, cmp, rreverse, 0);
				} else {
					/* scan r for v */
					while (rstart < rend &&
					       rreverse * cmp(v, VALUE(r, rstart)) > 0)
						rstart++;
				}
			}
			/* rstart or rcand points to first value >= v
			 * or end of r */
		} else {
			if (rcand) {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search, but if l is not
				 * sorted (lscan == 0) we'll always do
				 * a binary search */
				if (rscan < (BUN) (rcandend - rcand) &&
				    rreverse * cmp(v, VALUE(r, rcandend[-rscan - 1] - r->hseqbase)) < 0) {
					/* value too far away in r or
					 * l not sorted: use binary
					 * search */
					rcandend = rcand + binsearch(rcand, r->hseqbase, rvals, rvars, rwidth, 0, (BUN) (rcandend - rcand) - (lscan == 0 ? 0 : rscan), v, cmp, rreverse, 1);
				} else {
					/* scan r for v */
					while (rcand < rcandend &&
					       rreverse * cmp(v, VALUE(r, rcandend[-1] - r->hseqbase)) < 0)
						rcandend--;
				}
			} else {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search, but if l is not
				 * sorted (lscan == 0) we'll always do
				 * a binary search */
				if (rscan < rend - rstart &&
				    rreverse * cmp(v, VALUE(r, rend - rscan - 1)) < 0) {
					/* value too far away in r or
					 * l not sorted: use binary
					 * search */
					rend = binsearch(NULL, 0, rvals, rvars, rwidth, rstart, rend - (lscan == 0 ? 0 : rscan), v, cmp, rreverse, 1);
				} else {
					/* scan r for v */
					while (rstart < rend &&
					       rreverse * cmp(v, VALUE(r, rend - 1)) < 0)
						rend--;
				}
			}
			/* rend/rcandend now points to first value > v
			 * or start of r */
		}
		/* count number of entries in r that are equal to v */
		nr = 0;
		if (equal_order) {
			if (rcand) {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search */
				if (rscan < (BUN) (rcandend - rcand) &&
				    cmp(v, VALUE(r, rcand[rscan] - r->hseqbase)) == 0) {
					/* range too large: use binary
					 * search */
					nr = binsearch(rcand, r->hseqbase, rvals, rvars, rwidth, rscan, rcandend - rcand, v, cmp, rreverse, 1);
					rcand += nr;
				} else {
					/* scan r for end of range */
					while (rcand < rcandend &&
					       cmp(v, VALUE(r, rcand[0] - r->hseqbase)) == 0) {
						nr++;
						rcand++;
					}
				}
			} else {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search */
				if (rscan < rend - rstart &&
				    cmp(v, VALUE(r, rstart + rscan)) == 0) {
					/* range too large: use binary
					 * search */
					nr = binsearch(NULL, 0, rvals, rvars, rwidth, rstart + rscan, rend, v, cmp, rreverse, 1);
					nr -= rstart;
					rstart += nr;
				} else {
					/* scan r for end of range */
					while (rstart < rend &&
					       cmp(v, VALUE(r, rstart)) == 0) {
						nr++;
						rstart++;
					}
				}
			}
		} else {
			if (rcand) {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search */
				if (rscan < (BUN) (rcandend - rcand) &&
				    cmp(v, VALUE(r, rcandend[-rscan - 1] - r->hseqbase)) == 0) {
					nr = binsearch(rcand, r->hseqbase, rvals, rvars, rwidth, 0, (BUN) (rcandend - rcand) - rscan, v, cmp, rreverse, 0);
					nr = (BUN) (rcandend - rcand) - nr;
					rcandend -= nr;
				} else {
					/* scan r for start of range */
					while (rcand < rcandend &&
					       cmp(v, VALUE(r, rcandend[-1] - r->hseqbase)) == 0) {
						nr++;
						rcandend--;
					}
				}
			} else {
				/* look ahead a little (rscan) in r to
				 * see whether we're better off doing
				 * a binary search */
				if (rscan < rend - rstart &&
				    cmp(v, VALUE(r, rend - rscan - 1)) == 0) {
					nr = binsearch(NULL, 0, rvals, rvars, rwidth, rstart, rend - rscan, v, cmp, rreverse, 0);
					nr = rend - nr;
					rend -= nr;
				} else {
					/* scan r for start of range */
					while (rstart < rend &&
					       cmp(v, VALUE(r, rend - 1)) == 0) {
						nr++;
						rend--;
					}
				}
			}
		}
		if (nr == 0) {
			/* no entries in r found */
			if (!nil_on_miss) {
				if (lscan > 0 &&
				    (rcand ? rcand == rcandend : rstart == rend)) {
					/* nothing more left to match
					 * in r */
					break;
				}
				continue;
			}
			/* insert a nil to indicate a non-match */
			insert_nil = 1;
			nr = 1;
			r2->T->nil = 1;
			r2->T->nonil = 0;
			r2->tsorted = 0;
			r2->trevsorted = 0;
		} else {
			insert_nil = 0;
			if (semi) {
				/* for semi-join, only insert single
				 * value */
				nr = 1;
			}
		}
		/* make space: nl values in l match nr values in r, so
		 * we need to add nl * nr values in the results */
		if (BATcount(r1) + nl * nr > BATcapacity(r1)) {
			/* make some extra space by extrapolating how
			 * much more we need */
			BUN newcap = BATcount(r1) + nl * nr * (lcand ? (BUN) (lcandend + 1 - lcand) : lend + 1 - lstart);
			BATsetcount(r1, BATcount(r1));
			BATsetcount(r2, BATcount(r2));
			r1 = BATextend(r1, newcap);
			r2 = BATextend(r2, newcap);
			if (r1 == NULL || r2 == NULL) {
				if (r1)
					BBPreclaim(r1);
				if (r2)
					BBPreclaim(r2);
				return GDK_FAIL;
			}
			assert(BATcapacity(r1) == BATcapacity(r2));
		}

		/* maintain properties */
		if (nl > 1) {
			/* value occurs multiple times in l, so entry
			 * in r will be repeated multiple times: hence
			 * r2 is not key */
			r2->tkey = 0;
			/* multiple different values will be inserted
			 * in r1 (always in order), so not reverse
			 * ordered anymore */
			r1->trevsorted = 0;
		}
		if (nr > 1) {
			/* value occurs multiple times in r, so entry
			 * in l will be repeated multiple times: hence
			 * r1 is not key */
			r1->tkey = 0;
			/* multiple different values will be inserted
			 * in r2 (in order), so not reverse ordered
			 * anymore */
			r2->trevsorted = 0;
			if (nl > 1) {
				/* multiple values in l match multiple
				 * values in r, so an ordered sequence
				 * will be inserted multiple times in
				 * r2, so r2 is not ordered anymore */
				r2->tsorted = 0;
			}
		}
		if (lscan == 0) {
			/* deduce relative positions of r matches for
			 * this and previous value in v */
			if (prev) {
				if (rreverse * cmp(prev, v) < 0) {
					/* previous value in l was
					 * less than current */
					r2->trevsorted = 0;
				} else {
					r2->tsorted = 0;
				}
			}
			prev = v;
		}
		if (BATcount(r1) > 0) {
			/* a new, higher value will be inserted into
			 * r1, so r1 is not reverse ordered anymore */
			r1->trevsorted = 0;
			/* depending on whether l and r are ordered
			 * the same or not, a new higher or lower
			 * value will be added to r2 */
			if (equal_order)
				r2->trevsorted = 0;
			else
				r2->tsorted = 0;
		}

		/* insert values: various different ways of doing it */
		if (insert_nil) {
			do {
				lv = lcand ? lcand[-(ssize_t)nl] : lstart + l->hseqbase - nl;

				for (i = 0; i < nr; i++) {
					APPEND(r1, lv);
					APPEND(r2, oid_nil);
				}
			} while (--nl > 0);
		} else if (rcand && equal_order) {
			do {
				lv = lcand ? lcand[-(ssize_t)nl] : lstart + l->hseqbase - nl;

				for (i = nr; i > 0; i--) {
					APPEND(r1, lv);
					APPEND(r2, rcand[-(ssize_t)i]);
				}
			} while (--nl > 0);
		} else if (rcand) {
			do {
				lv = lcand ? lcand[-(ssize_t)nl] : lstart + l->hseqbase - nl;

				for (i = 0; i < nr; i++) {
					APPEND(r1, lv);
					APPEND(r2, rcandend[i]);
				}
			} while (--nl > 0);
		} else if (equal_order) {
			do {
				lv = lcand ? lcand[-(ssize_t)nl] : lstart + l->hseqbase - nl;

				for (i = nr; i > 0; i--) {
					APPEND(r1, lv);
					APPEND(r2, rstart + r->hseqbase - i);
				}
			} while (--nl > 0);
		} else {
			do {
				lv = lcand ? lcand[-(ssize_t)nl] : lstart + l->hseqbase - nl;

				for (i = 0; i < nr; i++) {
					APPEND(r1, lv);
					APPEND(r2, rend + r->hseqbase + i);
				}
			} while (--nl > 0);
		}
	}
	assert(BATcount(r1) == BATcount(r2));
	/* also set other bits of heap to correct value to indicate size */
	BATsetcount(r1, BATcount(r1));
	BATsetcount(r2, BATcount(r2));
	return GDK_SUCCEED;
}

/* binary search in a candidate list, return 1 if found, 0 if not */
static int
binsearchcand(const oid *cand, BUN lo, BUN hi, oid v)
{
	BUN mid;

	--hi;			/* now hi is inclusive */
	if (v < cand[lo] || v > cand[hi])
		return 0;
	while (hi > lo) {
		mid = (lo + hi) / 2;
		if (cand[mid] == v)
			return 1;
		if (cand[mid] < v)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return cand[lo] == v;
}

static gdk_return
hashjoin(BAT *r1, BAT *r2, BAT *l, BAT *r, BAT *sl, BAT *sr, int nil_matches, int nil_on_miss, int semi)
{
	BUN lstart, lend, lcnt;
	const oid *lcand = NULL, *lcandend = NULL;
	BUN rstart, rend, rcnt;
	const oid *rcand = NULL, *rcandend = NULL;
	oid lo, ro;
	BATiter ri;
	BUN rb;
	wrd off;
	BUN nr, nrcand, newcap;
	const char *lvals;
	const char *lvars;
	int lwidth;
	const void *nil = ATOMnilptr(l->ttype);
	int (*cmp)(const void *, const void *) = BATatoms[l->ttype].atomCmp;
	const char *v;

	ALGODEBUG fprintf(stderr, "#hashjoin(l=%s#" BUNFMT "[%s]%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s,sl=%s#" BUNFMT "%s%s,"
			  "sr=%s#" BUNFMT "%s%s,nil_matches=%d,"
			  "nil_on_miss=%d,semi=%d)\n",
			  BATgetId(l), BATcount(l), ATOMname(l->ttype),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "",
			  sl ? BATgetId(sl) : "NULL", sl ? BATcount(sl) : 0,
			  sl && sl->tsorted ? "-sorted" : "",
			  sl && sl->trevsorted ? "-revsorted" : "",
			  sr ? BATgetId(sr) : "NULL", sr ? BATcount(sr) : 0,
			  sr && sr->tsorted ? "-sorted" : "",
			  sr && sr->trevsorted ? "-revsorted" : "",
			  nil_matches, nil_on_miss, semi);

	assert(BAThdense(l));
	assert(BAThdense(r));
	assert(l->ttype != TYPE_void);
	assert(r->ttype != TYPE_void);
	assert(l->ttype == r->ttype);
	assert(sl == NULL || sl->tsorted);
	assert(sr == NULL || sr->tsorted);

	CANDINIT(l, sl, lstart, lend, lcnt, lcand, lcandend);
	CANDINIT(r, sr, rstart, rend, rcnt, rcand, rcandend);
	lwidth = l->T->width;
	lvals = (const char *) Tloc(l, BUNfirst(l));
	if (l->tvarsized && l->ttype) {
		assert(r->tvarsized && r->ttype);
		lvars = l->T->vheap->base;
	} else {
		assert(!r->tvarsized || !r->ttype);
		lvars = NULL;
	}
	off = r->hseqbase - BUNfirst(r);

	/* basic properties will be adjusted if necessary later on,
	 * they were initially set by joininitresults() */

	/* if an input columns is key, the opposite output column will
	 * be key */
	r1->tkey = r->tkey != 0;
	r2->tkey = l->tkey != 0;
	/* r2 is not likely to be sorted (although it is certainly
	 * possible) */
	r2->tsorted = 0;
	r2->trevsorted = 0;

	if (lstart == lend || (!nil_on_miss && rstart == rend)) {
		/* nothing to do: there are no matches */
		return GDK_SUCCEED;
	}

	/* hashes work on HEAD column */
	r = BATmirror(r);
	if (BATprepareHash(r))
		goto bailout;
	ri = bat_iterator(r);
	nrcand = (BUN) (rcandend - rcand);

	if (lcand) {
		while (lcand < lcandend) {
			lo = *lcand++;
			v = VALUE(l, lo - l->hseqbase);
			if (!nil_matches && cmp(v, nil) == 0)
				continue;
			nr = 0;
			if (rcand) {
				HASHloop(ri, r->H->hash, rb, v) {
					ro = (oid) rb + off;
					if (!binsearchcand(rcand, 0, nrcand, ro))
						continue;
					if (BUNlast(r1) == BATcapacity(r1)) {
						newcap = BATgrows(r1);
						BATsetcount(r1, BATcount(r1));
						BATsetcount(r2, BATcount(r2));
						r1 = BATextend(r1, newcap);
						r2 = BATextend(r2, newcap);
						if (r1 == NULL || r2 == NULL)
							goto bailout;
						assert(BATcapacity(r1) == BATcapacity(r2));
					}
					APPEND(r1, lo);
					APPEND(r2, ro);
					nr++;
					if (semi)
						break;
				}
			} else {
				HASHloop(ri, r->H->hash, rb, v) {
					ro = (oid) rb + off;
					if (ro < rstart || ro >= rend)
						continue;
					if (BUNlast(r1) == BATcapacity(r1)) {
						newcap = BATgrows(r1);
						BATsetcount(r1, BATcount(r1));
						BATsetcount(r2, BATcount(r2));
						r1 = BATextend(r1, newcap);
						r2 = BATextend(r2, newcap);
						if (r1 == NULL || r2 == NULL)
							goto bailout;
						assert(BATcapacity(r1) == BATcapacity(r2));
					}
					APPEND(r1, lo);
					APPEND(r2, ro);
					nr++;
					if (semi)
						break;
				}
			}
			if (nr == 0 && nil_on_miss) {
				nr = 1;
				r2->T->nil = 1;
				r2->T->nonil = 0;
				r2->tkey = 0;
				if (BUNlast(r1) == BATcapacity(r1)) {
					newcap = BATgrows(r1);
					BATsetcount(r1, BATcount(r1));
					BATsetcount(r2, BATcount(r2));
					r1 = BATextend(r1, newcap);
					r2 = BATextend(r2, newcap);
					if (r1 == NULL || r2 == NULL)
						goto bailout;
					assert(BATcapacity(r1) == BATcapacity(r2));
				}
				APPEND(r1, lo);
				APPEND(r2, oid_nil);
			} else if (nr > 1)
				r1->tkey = 0;
			if (nr > 0 && BATcount(r1) > nr)
				r1->trevsorted = 0;
		}
	} else {
		for (lo = lstart - BUNfirst(l) + l->hseqbase; lstart < lend; lo++) {
			v = VALUE(l, lstart);
			lstart++;
			if (!nil_matches && cmp(v, nil) == 0)
				continue;
			nr = 0;
			if (rcand) {
				HASHloop(ri, r->H->hash, rb, v) {
					ro = (oid) rb + off;
					if (!binsearchcand(rcand, 0, nrcand, ro))
						continue;
					if (BUNlast(r1) == BATcapacity(r1)) {
						newcap = BATgrows(r1);
						BATsetcount(r1, BATcount(r1));
						BATsetcount(r2, BATcount(r2));
						r1 = BATextend(r1, newcap);
						r2 = BATextend(r2, newcap);
						if (r1 == NULL || r2 == NULL)
							goto bailout;
						assert(BATcapacity(r1) == BATcapacity(r2));
					}
					APPEND(r1, lo);
					APPEND(r2, ro);
					nr++;
					if (semi)
						break;
				}
			} else {
				HASHloop(ri, r->H->hash, rb, v) {
					ro = (oid) rb + off;
					if (ro < rstart || ro >= rend)
						continue;
					if (BUNlast(r1) == BATcapacity(r1)) {
						newcap = BATgrows(r1);
						BATsetcount(r1, BATcount(r1));
						BATsetcount(r2, BATcount(r2));
						r1 = BATextend(r1, newcap);
						r2 = BATextend(r2, newcap);
						if (r1 == NULL || r2 == NULL)
							goto bailout;
						assert(BATcapacity(r1) == BATcapacity(r2));
					}
					APPEND(r1, lo);
					APPEND(r2, ro);
					nr++;
					if (semi)
						break;
				}
			}
			if (nr == 0 && nil_on_miss) {
				nr = 1;
				r2->T->nil = 1;
				r2->T->nonil = 0;
				if (BUNlast(r1) == BATcapacity(r1)) {
					newcap = BATgrows(r1);
					BATsetcount(r1, BATcount(r1));
					BATsetcount(r2, BATcount(r2));
					r1 = BATextend(r1, newcap);
					r2 = BATextend(r2, newcap);
					if (r1 == NULL || r2 == NULL)
						goto bailout;
					assert(BATcapacity(r1) == BATcapacity(r2));
				}
				APPEND(r1, lo);
				APPEND(r2, oid_nil);
			} else if (nr > 1)
				r1->tkey = 0;
			if (nr > 0 && BATcount(r1) > nr)
				r1->trevsorted = 0;
		}
	}
	assert(BATcount(r1) == BATcount(r2));
	/* also set other bits of heap to correct value to indicate size */
	BATsetcount(r1, BATcount(r1));
	BATsetcount(r2, BATcount(r2));
	if (BATcount(r1) <= 1) {
		r1->tsorted = 1;
		r1->trevsorted = 1;
		r1->tkey = 1;
		r2->tsorted = 1;
		r2->trevsorted = 1;
		r2->tkey = 1;
	}
	return GDK_SUCCEED;

  bailout:
	if (r1)
		BBPreclaim(r1);
	if (r2)
		BBPreclaim(r2);
	return GDK_FAIL;
}

#define MASK_EQ		1
#define MASK_LT		2
#define MASK_GT		4
#define MASK_LE		(MASK_EQ | MASK_LT)
#define MASK_GE		(MASK_EQ | MASK_GT)
#define MASK_NE		(MASK_LT | MASK_GT)

static gdk_return
thetajoin(BAT *r1, BAT *r2, BAT *l, BAT *r, BAT *sl, BAT *sr, const char *op)
{
	BUN lstart, lend, lcnt;
	const oid *lcand = NULL, *lcandend = NULL;
	BUN rstart, rend, rcnt;
	const oid *rcand = NULL, *rcandend = NULL;
	const char *lvals, *rvals;
	const char *lvars, *rvars;
	int lwidth, rwidth;
	const void *nil = ATOMnilptr(l->ttype);
	int (*cmp)(const void *, const void *) = BATatoms[l->ttype].atomCmp;
	const char *vl, *vr;
	const oid *p;
	oid lastr = 0;		/* last value inserted into r2 */
	BUN n, nr;
	BUN newcap;
	int opcode = 0;
	oid lo, ro;
	int c;

	assert(BAThdense(l));
	assert(BAThdense(r));
	assert(l->ttype != TYPE_void);
	assert(r->ttype != TYPE_void);
	assert(l->ttype == r->ttype);
	assert(sl == NULL || sl->tsorted);
	assert(sr == NULL || sr->tsorted);

	/* encode operator as a bit mask into opcode */
	if (op[0] == '=' && ((op[1] == '=' && op[2] == 0) || op[2] == 0)) {
		/* "=" or "==" */
		opcode |= MASK_EQ;
	} else if (op[0] == '!' && op[1] == '=' && op[2] == 0) {
		/* "!=" (equivalent to "<>") */
		opcode |= MASK_NE;
	} else 	if (op[0] == '<') {
		if (op[1] == 0) {
			/* "<" */
			opcode |= MASK_LT;
		} else if (op[1] == '=' && op[2] == 0) {
			/* "<=" */
			opcode |= MASK_LE;
		} else if (op[1] == '>' && op[2] == 0) {
			/* "<>" (equivalent to "!=") */
			opcode |= MASK_NE;
		}
	} else if (op[0] == '>') {
		if (op[1] == 0) {
			/* ">" */
			opcode |= MASK_GT;
		} else if (op[1] == '=' && op[2] == 0) {
			/* ">=" */
			opcode |= MASK_GE;
		}
	}
	if (opcode == 0) {
		GDKerror("BATthetasubjoin: unknown operator.\n");
		return GDK_FAIL;
	}

	CANDINIT(l, sl, lstart, lend, lcnt, lcand, lcandend);
	CANDINIT(r, sr, rstart, rend, rcnt, rcand, rcandend);

	lvals = (const char *) Tloc(l, BUNfirst(l));
	rvals = (const char *) Tloc(r, BUNfirst(r));
	if (l->tvarsized && l->ttype) {
		assert(r->tvarsized && r->ttype);
		lvars = l->T->vheap->base;
		rvars = r->T->vheap->base;
	} else {
		assert(!r->tvarsized || !r->ttype);
		lvars = rvars = NULL;
	}
	lwidth = l->T->width;
	rwidth = r->T->width;

	r1->tkey = 1;
	r1->tsorted = 1;
	r1->trevsorted = 1;
	r2->tkey = 1;
	r2->tsorted = 1;
	r2->trevsorted = 1;

	/* nested loop implementation for theta join */
	for (;;) {
		if (lcand) {
			if (lcand == lcandend)
				break;
			lo = *lcand++;
			vl = VALUE(l, lo - l->hseqbase);
		} else {
			if (lstart == lend)
				break;
			vl = VALUE(l, lstart);
			lo = lstart++ + l->hseqbase;
		}
		if (cmp(vl, nil) == 0)
			continue;
		nr = 0;
		p = rcand;
		n = rstart;
		for (;;) {
			if (rcand) {
				if (p == rcandend)
					break;
				ro = *p++;
				vr = VALUE(r, ro - r->hseqbase);
			} else {
				if (n == rend)
					break;
				vr = VALUE(r, n);
				ro = n++ + r->hseqbase;
			}
			if (cmp(vr, nil) == 0)
				continue;
			c = cmp(vl, vr);
			if (!((opcode & MASK_LT && c < 0) ||
			      (opcode & MASK_GT && c > 0) ||
			      (opcode & MASK_EQ && c == 0)))
				continue;
			if (BUNlast(r1) == BATcapacity(r1)) {
				newcap = BATgrows(r1);
				BATsetcount(r1, BATcount(r1));
				BATsetcount(r2, BATcount(r2));
				r1 = BATextend(r1, newcap);
				r2 = BATextend(r2, newcap);
				if (r1 == NULL || r2 == NULL)
					goto bailout;
				assert(BATcapacity(r1) == BATcapacity(r2));
			}
			if (nr == 0 && BATcount(r2) > 0) {
				r1->trevsorted = 0;
				if (lastr > ro) {
					r2->tsorted = 0;
					r2->tkey = 0;
				} else if (lastr < ro)
					r2->trevsorted = 0;
			}
			APPEND(r1, lo);
			APPEND(r2, ro);
			lastr = ro;
			nr++;
		}
		if (nr > 1) {
			r1->tkey = 0;
			r2->trevsorted = 0;
		}
	}
	assert(BATcount(r1) == BATcount(r2));
	/* also set other bits of heap to correct value to indicate size */
	BATsetcount(r1, BATcount(r1));
	BATsetcount(r2, BATcount(r2));
	return GDK_SUCCEED;

  bailout:
	if (r1)
		BBPreclaim(r1);
	if (r2)
		BBPreclaim(r2);
	return GDK_FAIL;
}

/* Perform an equi-join over l and r.  Returns two new, aligned,
 * dense-headed bats with in the tail the oids (head column values) of
 * matching tuples.  The result is in the same order as l (i.e. r1 is
 * sorted). */
gdk_return
BATsubleftjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate)
{
	BAT *r1, *r2;

	*r1p = NULL;
	*r2p = NULL;
	if (joinparamcheck(l, r, sl, sr, "BATsubleftjoin") == GDK_FAIL)
		return GDK_FAIL;
	if (joininitresults(&r1, &r2, estimate != BUN_NONE ? estimate : sl ? BATcount(sl) : BATcount(l), "BATsubleftjoin") == GDK_FAIL)
		return GDK_FAIL;
	*r1p = r1;
	*r2p = r2;
	if (r->tsorted || r->trevsorted)
		return mergejoin(r1, r2, l, r, sl, sr, 0, 0, 0);
	return hashjoin(r1, r2, l, r, sl, sr, 0, 0, 0);
}

/* Performs a left outer join over l and r.  Returns two new, aligned,
 * dense-headed bats with in the tail the oids (head column values) of
 * matching tuples, or the oid in the first output bat and nil in the
 * second output bat if the value in l does not occur in r.  The
 * result is in the same order as l (i.e. r1 is sorted). */
gdk_return
BATsubouterjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate)
{
	BAT *r1, *r2;

	*r1p = NULL;
	*r2p = NULL;
	if (joinparamcheck(l, r, sl, sr, "BATsubouterjoin") == GDK_FAIL)
		return GDK_FAIL;
	if (joininitresults(&r1, &r2, estimate != BUN_NONE ? estimate : sl ? BATcount(sl) : BATcount(l), "BATsubouterjoin") == GDK_FAIL)
		return GDK_FAIL;
	*r1p = r1;
	*r2p = r2;
	if (r->tsorted || r->trevsorted)
		return mergejoin(r1, r2, l, r, sl, sr, 0, 1, 0);
	return hashjoin(r1, r2, l, r, sl, sr, 0, 1, 0);
}

/* Perform a semi-join over l and r.  Returns two new, aligned,
 * dense-headed bats with in the tail the oids (head column values) of
 * matching tuples.  The result is in the same order as l (i.e. r1 is
 * sorted). */
gdk_return
BATsubsemijoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate)
{
	BAT *r1, *r2;

	*r1p = NULL;
	*r2p = NULL;
	if (joinparamcheck(l, r, sl, sr, "BATsubsemijoin") == GDK_FAIL)
		return GDK_FAIL;
	if (joininitresults(&r1, &r2, estimate != BUN_NONE ? estimate : sl ? BATcount(sl) : BATcount(l), "BATsubsemijoin") == GDK_FAIL)
		return GDK_FAIL;
	*r1p = r1;
	*r2p = r2;
	if (r->tsorted || r->trevsorted)
		return mergejoin(r1, r2, l, r, sl, sr, 0, 0, 1);
	return hashjoin(r1, r2, l, r, sl, sr, 0, 0, 1);
}

gdk_return
BATsubthetajoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, const char *op, BUN estimate)
{
	BAT *r1, *r2;

	*r1p = NULL;
	*r2p = NULL;
	if (joinparamcheck(l, r, sl, sr, "BATsubthetajoin") == GDK_FAIL)
		return GDK_FAIL;
	if (joininitresults(&r1, &r2, estimate != BUN_NONE ? estimate : sl ? BATcount(sl) : BATcount(l), "BATsubthetajoin") == GDK_FAIL)
		return GDK_FAIL;
	*r1p = r1;
	*r2p = r2;
	return thetajoin(r1, r2, l, r, sl, sr, op);
}

gdk_return
BATsubjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate)
{
	BAT *r1, *r2;
	BUN lcount, rcount;
	int swap;

	*r1p = NULL;
	*r2p = NULL;
	if (joinparamcheck(l, r, sl, sr, "BATsubjoin") == GDK_FAIL)
		return GDK_FAIL;
	lcount = BATcount(l);
	if (sl)
		lcount = MIN(lcount, BATcount(sl));
	rcount = BATcount(r);
	if (sr)
		rcount = MIN(rcount, BATcount(sr));
	if (lcount == 0 || rcount == 0) {
		r1 = BATnew(TYPE_void, TYPE_void, 0);
		BATseqbase(r1, 0);
		BATseqbase(BATmirror(r1), 0);
		r2 = BATnew(TYPE_void, TYPE_void, 0);
		BATseqbase(r2, 0);
		BATseqbase(BATmirror(r2), 0);
		*r1p = r1;
		*r2p = r2;
		return GDK_SUCCEED;
	}
	if (joininitresults(&r1, &r2, estimate != BUN_NONE ? estimate : sl ? BATcount(sl) : BATcount(l), "BATsubjoin") == GDK_FAIL)
		return GDK_FAIL;
	*r1p = r1;
	*r2p = r2;
	swap = 0;
	if ((l->tsorted || l->trevsorted) && (r->tsorted || r->trevsorted)) {
		/* both sorted, don't swap */
		return mergejoin(r1, r2, l, r, sl, sr, 0, 0, 0);
	} else if (l->T->hash && r->T->hash) {
		/* both have hash, smallest on right */
		if (lcount < rcount)
			swap = 1;
	} else if (l->T->hash) {
		/* only left has hash, swap */
		swap = 1;
	} else if (r->T->hash) {
		/* only right has hash, don't swap */
		swap = 0;
	} else if (l->tsorted || l->trevsorted) {
		/* left is sorted, swap */
		return mergejoin(r2, r1, r, l, sr, sl, 0, 0, 0);
	} else if (r->tsorted || r->trevsorted) {
		/* right is sorted, don't swap */
		return mergejoin(r1, r2, l, r, sl, sr, 0, 0, 0);
	} else if (BATcount(r1) < BATcount(r2)) {
		/* no hashes, not sorted, create hash on smallest BAT */
		swap = 1;
	}
	if (swap) {
		return hashjoin(r2, r1, r, l, sr, sl, 0, 0, 0);
	} else {
		return hashjoin(r1, r2, l, r, sl, sr, 0, 0, 0);
	}
}

BAT *
BATproject(BAT *l, BAT *r)
{
	BAT *bn;
	const oid *o;
	const void *nil = ATOMnilptr(r->ttype);
	const void *v, *prev;
	BATiter ri, bni;
	oid lo, hi;
	BUN n;
	int (*cmp)(const void *, const void *) = BATatoms[r->ttype].atomCmp;
	int c;

	ALGODEBUG fprintf(stderr, "#BATproject(l=%s#" BUNFMT "%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s)\n",
			  BATgetId(l), BATcount(l),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "");

	assert(BAThdense(l));
	assert(BAThdense(r));
	assert(ATOMtype(l->ttype) == TYPE_oid);

	if (BATtdense(l) && BATcount(l) > 0) {
		lo = l->tseqbase;
		hi = l->tseqbase + BATcount(l);
		if (lo < r->hseqbase || hi > r->hseqbase + BATcount(r)) {
			GDKerror("BATproject: does not match always\n");
			return NULL;
		}
		bn = BATslice(r, lo - r->hseqbase, hi - r->hseqbase);
		if (bn == NULL)
			return NULL;
		return BATseqbase(bn, l->hseqbase + (lo - l->tseqbase));
	}
	if (l->ttype == TYPE_void || BATcount(l) == 0) {
		assert(BATcount(l) == 0 || l->tseqbase == oid_nil);
		bn = BATconstant(r->ttype, nil, BATcount(l));
		if (bn != NULL) {
			bn = BATseqbase(bn, l->hseqbase);
			if (bn->ttype == TYPE_void && BATcount(bn) == 0)
				BATseqbase(BATmirror(bn), 0);
		}
		return bn;
	}
	assert(l->ttype == TYPE_oid);
	bn = BATnew(TYPE_void, ATOMtype(r->ttype), BATcount(l));
	if (bn == NULL)
		return NULL;
	o = (const oid *) Tloc(l, BUNfirst(l));
	n = BUNfirst(bn);
	ri = bat_iterator(r);
	bni = bat_iterator(bn);
	/* be optimistic, we'll change this as needed */
	bn->T->nonil = 1;
	bn->T->nil = 0;
	bn->tsorted = 1;
	bn->trevsorted = 1;
	bn->tkey = 1;
	prev = NULL;
	for (lo = l->hseqbase, hi = lo + BATcount(l); lo < hi; lo++, o++, n++) {
		if (*o == oid_nil) {
			tfastins_nocheck(bn, n, nil, Tsize(bn));
			bn->T->nonil = 0;
			bn->T->nil = 1;
			bn->tsorted = 0;
			bn->trevsorted = 0;
			bn->tkey = 0;
		} else if (*o < r->hseqbase ||
			   *o >= r->hseqbase + BATcount(r)) {
			GDKerror("BATproject: does not match always\n");
			goto bunins_failed;
		} else {
			v = BUNtail(ri, *o - r->hseqbase + BUNfirst(r));
			tfastins_nocheck(bn, n, v, Tsize(bn));
			if (bn->T->nonil && cmp(v, nil) == 0) {
				bn->T->nonil = 0;
				bn->T->nil = 1;
			}
			if (prev && (bn->trevsorted | bn->tsorted | bn->tkey)) {
				c = cmp(prev, v);
				if (c < 0) {
					bn->trevsorted = 0;
					if (!bn->tsorted)
						bn->tkey = 0; /* can't be sure */
				} else if (c > 0) {
					bn->tsorted = 0;
					if (!bn->trevsorted)
						bn->tkey = 0; /* can't be sure */
				} else {
					bn->tkey = 0; /* definitely */
				}
			}
			prev = BUNtail(bni, n);
		}
	}
	assert(n == BATcount(l));
	BATsetcount(bn, n);
	BATseqbase(bn, l->hseqbase);
	return bn;

  bunins_failed:
	BBPreclaim(bn);
	return NULL;
}
