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
 * @a M. L. Kersten, P. Boncz, N. Nes
 * @* Delta management
 * The basis for transaction management is to keep track of elements
 * inserted, deleted, and replaced.  This information is stored within
 * the BAT structure using three delta markers.  Inserted denotes the
 * first added BUN since the last commit. Deleted points to the BUNs
 * removed.  The deletion list is terminated at @%first@, where space
 * is reserved for swapping BUNs upon deletion. Initialization of the
 * BAT is extended as follows:
 */

/*
 * Impact on hashing and indexing.  The hash structure is maintained
 * for all elements to be deleted ?.
 */
#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"

/*
 * batcommit really forgets the atoms guarded for an undo; we just
 * need to free their heap space (only if necessary).
 */
BAT *
BATcommit(BAT *b)
{
	BATcheck(b, "BATcommit");
	DELTADEBUG printf("#BATcommit1 %s free " SZFMT "," SZFMT " ins " BUNFMT " del " BUNFMT " first " BUNFMT " base " PTRFMT "," PTRFMT "\n",
			  BATgetId(b),
			  b->H->heap.free,
			  b->T->heap.free,
			  b->batInserted,
			  b->batDeleted,
			  b->batFirst,
			  PTRFMTCAST b->H->heap.base,
			  PTRFMTCAST b->T->heap.base);
	ALIGNcommit(b);
	if (b->batDeleted < b->batFirst && BBP_cache(b->batCacheid)) {
		BATiter bi = bat_iterator(b);
		int (*hunfix) (const void *) = BATatoms[b->htype].atomUnfix;
		int (*tunfix) (const void *) = BATatoms[b->ttype].atomUnfix;
		void (*hatmdel) (Heap *, var_t *) = BATatoms[b->htype].atomDel;
		void (*tatmdel) (Heap *, var_t *) = BATatoms[b->ttype].atomDel;
		BUN p, q;

		if (hatmdel || hunfix || tatmdel || tunfix) {
			DELloop(b, p, q) {
				ptr h = BUNhead(bi, p);
				ptr t = BUNtail(bi, p);

				if (hunfix) {
					(*hunfix) (h);
				}
				if (hatmdel) {
					(*hatmdel) (b->H->vheap, (var_t *) BUNhloc(bi, p));
				}
				if (tunfix) {
					(*tunfix) (t);
				}
				if (tatmdel) {
					(*tatmdel) (b->T->vheap, (var_t *) BUNtloc(bi, p));
				}
			}
		}
	}
	if (!BATdirty(b)) {
		b->batDirtyflushed = 0;
	}
	if (DELTAdirty(b)) {
		b->batDirtydesc = 1;
	}
	b->batDeleted = b->batFirst;
	b->batInserted = BUNlast(b);
	DELTADEBUG printf("#BATcommit2 %s free " SZFMT "," SZFMT " ins " BUNFMT " del " BUNFMT " first " BUNFMT " base " PTRFMT "," PTRFMT "\n",
			  BATgetId(b),
			  b->H->heap.free,
			  b->T->heap.free,
			  b->batInserted,
			  b->batDeleted,
			  b->batFirst,
			  PTRFMTCAST b->H->heap.base,
			  PTRFMTCAST b->T->heap.base);
	return b;
}

/*
 * BATfakeCommit() flushed the delta info, but leaves the BAT marked
 * clean.
 */
BAT *
BATfakeCommit(BAT *b)
{
	if (b) {
		BATcommit(b);
		b->batDirty = 0;
		b->batDirtydesc = b->H->heap.dirty = b->T->heap.dirty = 0;
		if (b->H->vheap)
			b->H->vheap->dirty = 0;
		if (b->T->vheap)
			b->T->vheap->dirty = 0;
	}
	return b;
}

/*
 * The routine @%BATundo@ restores the BAT to the previous commit
 * point.  The inserted elements are removed from the accelerators,
 * deleted from the heap. The guarded elements from uncommitted
 * deletes are inserted into the accelerators.
 */
BAT *
BATundo(BAT *b)
{
	BATiter bi = bat_iterator(b);
	BUN p, bunlast, bunfirst;

	BATcheck(b, "BATundo");
	DELTADEBUG printf("#BATundo %s \n", BATgetId(b));
	ALIGNundo(b);
	if (b->batDirtyflushed) {
		b->batDirtydesc = b->H->heap.dirty = b->T->heap.dirty = 1;
	} else {
		b->batDirty = 0;
		b->batDirtydesc = b->H->heap.dirty = b->T->heap.dirty = 0;
		if (b->H->vheap)
			b->H->vheap->dirty = 0;
		if (b->T->vheap)
			b->T->vheap->dirty = 0;
	}
	bunfirst = b->batInserted;
	bunlast = BUNlast(b) - 1;
	if (bunlast >= b->batInserted) {
		BUN i = bunfirst;
		int (*hunfix) (const void *) = BATatoms[b->htype].atomUnfix;
		int (*tunfix) (const void *) = BATatoms[b->ttype].atomUnfix;
		void (*hatmdel) (Heap *, var_t *) = BATatoms[b->htype].atomDel;
		void (*tatmdel) (Heap *, var_t *) = BATatoms[b->ttype].atomDel;

		if (hunfix || tunfix || hatmdel || tatmdel || b->H->hash || b->T->hash) {
			for (p = bunfirst; p <= bunlast; p++, i++) {
				ptr h = BUNhead(bi, p);
				ptr t = BUNtail(bi, p);

				if (b->H->hash) {
					HASHdel(b->H->hash, i, h, p < bunlast);
				}
				if (hunfix) {
					(*hunfix) (h);
				}
				if (hatmdel) {
					(*hatmdel) (b->H->vheap, (var_t *) BUNhloc(bi, p));
				}
				if (b->T->hash) {
					HASHdel(b->T->hash, i, t, p < bunlast);
				}
				if (tunfix) {
					(*tunfix) (t);
				}
				if (tatmdel) {
					(*tatmdel) (b->T->vheap, (var_t *) BUNtloc(bi, p));
				}
			}
		}
	}
	b->H->heap.free = headsize(b, b->batInserted);
	b->T->heap.free = tailsize(b, b->batInserted);

	bunfirst = b->batDeleted;
	bunlast = b->batFirst;
	if (bunlast > b->batDeleted) {
		BUN i = bunfirst;
		BAT *bm = BBP_cache(-b->batCacheid);

		/* elements are 'inserted' => zap properties */
		b->hsorted = 0;
		b->hrevsorted = 0;
		b->tsorted = 0;
		b->trevsorted = 0;
		if (b->hkey)
			BATkey(b, FALSE);
		if (b->tkey)
			BATkey(BATmirror(b), FALSE);

		for (p = bunfirst; p < bunlast; p++, i++) {
			ptr h = BUNhead(bi, p);
			ptr t = BUNtail(bi, p);

			if (b->H->hash) {
				HASHins(b, i, h);
			}
			if (b->T->hash) {
				HASHins(bm, i, t);
			}
		}
	}
	b->batFirst = b->batDeleted;
	BATsetcount(b, b->batInserted);
	return b;
}

/*
 * The proposed modifications can be obtained through the @%BATalpha@
 * and @%BATdelta@ routines , which return the inserted and deleted
 * BUNs, respectively.
 */
BAT *
BATprev(BAT *b)
{
	BUN p;
	BAT *bn;
	BATiter bi = bat_iterator(b);

	BATcheck(b, "BATprev");
	if (b->batRestricted == BAT_READ) {
		bn = VIEWcreate(b, b);
		if (bn) {
			bn->U->count = bn->batInserted - bn->batDeleted;
			bn->batInserted = bn->batFirst = bn->batDeleted;
		}
		return bn;
	}
	bn = BATnew(BAThtype(b), BATttype(b), BATcapacity(b));
	if (bn == NULL) {
		return bn;
	}
	for (p = b->batDeleted; p < b->batInserted; p++) {
		if (BUNins(bn, BUNhead(bi, p), BUNtail(bi, p), FALSE) == NULL) {
			BBPreclaim(bn);
			return NULL;
		}
	}
	return bn;
}

BAT *
BATalpha(BAT *b)
{
	BUN p;
	BAT *bn;
	BATiter bi = bat_iterator(b);

	BATcheck(b, "BATalpha");
	if (b->batRestricted == BAT_READ) {
		bn = VIEWcreate(b, b);
		if (bn) {
			bn->U->count -= (bn->batInserted - bn->batFirst);
			bn->batDeleted = bn->batFirst = bn->batInserted;
		}
		return bn;
	}
	bn = BATnew(BAThtype(b), BATttype(b), BATcapacity(b));
	if (bn == NULL) {
		return bn;
	}
	for (p = b->batInserted; p < BUNlast(b); p++) {
		if (BUNins(bn, BUNhead(bi, p), BUNtail(bi, p), FALSE) == NULL) {
			BBPreclaim(bn);
			return NULL;
		}
	}
	return bn;
}

BAT *
BATdelta(BAT *b)
{
	BUN p;
	BAT *bn;
	BATiter bi = bat_iterator(b);

	BATcheck(b, "BATdelta");
	if (b->batRestricted == BAT_READ) {
		bn = VIEWcreate(b, b);
		if (bn) {
			bn->U->count = bn->batFirst - bn->batDeleted;
			bn->batFirst = bn->batInserted = bn->batDeleted;
		}
		return bn;
	}
	bn = BATnew(BAThtype(b), BATttype(b), BATcapacity(b));
	if (bn == NULL) {
		return bn;
	}
	for (p = b->batDeleted; p < b->batFirst; p++) {
		if (BUNins(bn, BUNhead(bi, p), BUNtail(bi, p), FALSE) == NULL) {
			BBPreclaim(bn);
			return NULL;
		}
	}
	return bn;
}
