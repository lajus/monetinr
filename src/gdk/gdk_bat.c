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
 * @* BAT Module
 * In this Chapter we describe the BAT implementation in more detail.
 * The routines mentioned are primarily meant to simplify the library
 * implementation.
 *
 * @+ BAT Construction
 * BATs are implemented in several blocks of memory, prepared for disk
 * storage and easy shipment over a network.
 *
 * The BAT starts with a descriptor, which indicates the required BAT
 * library version and the BAT administration details.  In particular,
 * it describes the binary relationship maintained and the location of
 * fields required for storage.
 *
 * The general layout of the BAT in this implementation is as follows.
 * Each BAT comes with a heap for the loc-size buns and, optionally,
 * with heaps to manage the variable-sized data items of both
 * dimensions.  The buns are assumed to be stored as loc-size objects.
 * This is essentially an array of structs to store the associations.
 * The size is determined at BAT creation time using an upper bound on
 * the number of elements to be accommodated.  In case of overflow,
 * its storage space is extended automatically.
 *
 * The capacity of a BAT places an upper limit on the number of BUNs
 * to be stored initially. The actual space set aside may be quite
 * large.  Moreover, the size is aligned to int boundaries to speedup
 * access and avoid some machine limitations.
 *
 * Initialization of the variable parts rely on type specific routines
 * called atomHeap.
 */
#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"

#ifdef ALIGN
#undef ALIGN
#endif
#define ALIGN(n,b)	((b)?(b)*(1+(((n)-1)/(b))):n)

#define ATOMneedheap(tpe) (BATatoms[tpe].atomHeap != NULL)

char *BATstring_h = "h";
char *BATstring_t = "t";

static int
default_ident(char *s)
{
	return ((s) == BATstring_h || (s) == BATstring_t);
}

void
BATinit_idents(BAT *bn)
{
	bn->hident = (char *) BATstring_h;
	bn->tident = (char *) BATstring_t;
}

BATstore *
BATcreatedesc(int ht, int tt, int heapnames)
{
	BAT *bn;

	/*
	 * Alloc space for the BAT and its dependent records.
	 */
	BATstore *bs = (BATstore *) GDKzalloc(sizeof(BATstore));

	if (bs == NULL)
		return NULL;
	HEADLESSDEBUG {
		if ( ht != TYPE_void && ht != TYPE_oid)
			fprintf(stderr, "#headless violation in BATcreatedesc %d\n", ht);

	}
	/*
	 * assert needed in the kernel to get symbol eprintf resolved.
	 * Else modules using assert fail to load.
	 */
	assert(ht >= 0 && tt >= 0);
	bs->BM.H = &bs->T;
	bs->BM.T = &bs->H;
	bs->BM.P = &bs->P;
	bs->BM.U = &bs->U;
	bs->B.H = &bs->H;
	bs->B.T = &bs->T;
	bs->B.P = &bs->P;
	bs->B.U = &bs->U;

	bn = &bs->B;

	/*
	 * Fill in basic column info
	 */
	bn->htype = ht;
	bn->ttype = tt;
	bn->hkey = FALSE;
	bn->tkey = FALSE;
	bn->H->nonil = TRUE;
	bn->T->nonil = TRUE;
	bn->hsorted = bn->hrevsorted = ATOMlinear(ht) != 0;
	bn->tsorted = bn->trevsorted = ATOMlinear(tt) != 0;

	bn->hident = (char *) BATstring_h;
	bn->tident = (char *) BATstring_t;
	bn->halign = OIDnew(2);
	bn->talign = bn->halign + 1;
	bn->hseqbase = (ht == TYPE_void) ? oid_nil : 0;
	bn->tseqbase = (tt == TYPE_void) ? oid_nil : 0;
	bn->batPersistence = TRANSIENT;
	bn->H->props = bn->T->props = NULL;
	/*
	 * add to BBP
	 */
	BBPinsert(bs);
	/*
	 * fill in heap names, so HEAPallocs can resort to disk for
	 * very large writes.
	 */
	assert(bn->batCacheid > 0);
	bn->H->heap.filename = NULL;
	bn->T->heap.filename = NULL;
	bn->batMaphead = 0;
	bn->batMaptail = 0;
	bn->batMaphheap = 0;
	bn->batMaptheap = 0;
	if (heapnames) {
		const char *nme = BBP_physical(bn->batCacheid);

		if (ht) {
			bn->H->heap.filename = GDKmalloc(strlen(nme) + 12);
			if (bn->H->heap.filename == NULL)
				goto bailout;
			GDKfilepath(bn->H->heap.filename, NULL, nme, "head");
		}

		if (tt) {
			bn->T->heap.filename = GDKmalloc(strlen(nme) + 12);
			if (bn->T->heap.filename == NULL)
				goto bailout;
			GDKfilepath(bn->T->heap.filename, NULL, nme, "tail");
		}

		if (ATOMneedheap(ht)) {
			if ((bn->H->vheap = (Heap *) GDKzalloc(sizeof(Heap))) == NULL || (bn->H->vheap->filename = GDKmalloc(strlen(nme) + 12)) == NULL)
				goto bailout;
			GDKfilepath(bn->H->vheap->filename, NULL, nme, "hheap");
			bn->H->vheap->parentid = bn->batCacheid;
		}

		if (ATOMneedheap(tt)) {
			if ((bn->T->vheap = (Heap *) GDKzalloc(sizeof(Heap))) == NULL || (bn->T->vheap->filename = GDKmalloc(strlen(nme) + 12)) == NULL)
				goto bailout;
			GDKfilepath(bn->T->vheap->filename, NULL, nme, "theap");
			bn->T->vheap->parentid = bn->batCacheid;
		}
	}
	bn->batDirty = TRUE;
	return bs;
      bailout:
	if (ht)
		HEAPfree(&bn->H->heap);
	if (tt)
		HEAPfree(&bn->T->heap);
	if (bn->H->vheap) {
		HEAPfree(bn->H->vheap);
		GDKfree(bn->H->vheap);
	}
	if (bn->T->vheap) {
		HEAPfree(bn->T->vheap);
		GDKfree(bn->T->vheap);
	}
	GDKfree(bs);
	return NULL;
}

bte
ATOMelmshift(int sz)
{
	bte sh;
	int i = sz >> 1;

	for (sh = 0; i != 0; sh++) {
		i >>= 1;
	}
	return sh;
}


void
BATsetdims(BAT *b)
{
	if (b->htype == TYPE_str)
		b->H->width = 1;
	else
		b->H->width = ATOMsize(b->htype);
	if (b->ttype == TYPE_str)
		b->T->width = 1;
	else
		b->T->width = ATOMsize(b->ttype);
	b->H->shift = ATOMelmshift(Hsize(b));
	b->T->shift = ATOMelmshift(Tsize(b));
	assert_shift_width(b->H->shift, b->H->width);
	assert_shift_width(b->T->shift, b->T->width);
	b->H->varsized = BATatoms[b->htype].varsized;
	b->T->varsized = BATatoms[b->ttype].varsized;
}

/*
 * @- BAT allocation
 * Allocate BUN heap and variable-size atomheaps (see e.g. strHeap).
 * We now initialize new BATs with their heapname such that the
 * modified HEAPalloc/HEAPextend primitives can possibly use memory
 * mapped files as temporary heap storage.
 *
 * In case of huge bats, we want HEAPalloc to write a file to disk,
 * and memory map it. To make this possible, we must provide it with
 * filenames.
 */
static BATstore *
BATnewstorage(int ht, int tt, BUN cap)
{
	BATstore *bs;
	BAT *bn;

	assert(cap <= BUN_MAX);
	/* and in case we don't have assertions enabled: limit the size */
	if (cap > BUN_MAX)
		cap = BUN_MAX;
	bs = BATcreatedesc(ht, tt, (ht || tt));
	if (bs == NULL)
		return NULL;
	bn = &bs->B;

	BATsetdims(bn);
	bn->U->capacity = cap;

	/* alloc the main heaps */
	if (ht && HEAPalloc(&bn->H->heap, cap, bn->H->width) < 0) {
		return NULL;
	}
	if (tt && HEAPalloc(&bn->T->heap, cap, bn->T->width) < 0) {
		if (ht)
			HEAPfree(&bn->H->heap);
		return NULL;
	}

	if (ATOMheap(ht, bn->H->vheap, cap) < 0) {
		if (ht)
			HEAPfree(&bn->H->heap);
		if (tt)
			HEAPfree(&bn->T->heap);
		GDKfree(bn->H->vheap);
		if (bn->T->vheap)
			GDKfree(bn->T->vheap);
		return NULL;
	}
	if (ATOMheap(tt, bn->T->vheap, cap) < 0) {
		if (ht)
			HEAPfree(&bn->H->heap);
		if (tt)
			HEAPfree(&bn->T->heap);
		if (bn->H->vheap) {
			HEAPfree(bn->H->vheap);
			GDKfree(bn->H->vheap);
		}
		GDKfree(bn->T->vheap);
		return NULL;
	}
	DELTAinit(bn);
	BBPcacheit(bs, 1);
	return bs;
}

BAT *
BATnew(int ht, int tt, BUN cap)
{
	BATstore *bs;

	assert(cap <= BUN_MAX);
	assert(ht != TYPE_bat);
	assert(tt != TYPE_bat);
	ERRORcheck((ht < 0) || (ht > GDKatomcnt), "BATnew:ht error\n");
	ERRORcheck((tt < 0) || (tt > GDKatomcnt), "BATnew:tt error\n");

	/* round up to multiple of BATTINY */
	if (cap < BUN_MAX - BATTINY)
		cap = (cap + BATTINY - 1) & ~(BATTINY - 1);
	if (cap < BATTINY)
		cap = BATTINY;
	/* and in case we don't have assertions enabled: limit the size */
	if (cap > BUN_MAX)
		cap = BUN_MAX;
	bs = BATnewstorage(ht, tt, cap);
	return bs == NULL ? NULL : &bs->B;
}

BAT *
BATattach(int tt, const char *heapfile)
{
	BATstore *bs;
	BAT *bn;
	struct stat st;
	int atomsize;
	BUN cap;
	char path[PATHLENGTH];

	ERRORcheck(tt <= 0 , "BATattach: bad tail type (<=0)\n");
	ERRORcheck(ATOMvarsized(tt), "BATattach: bad tail type (varsized)\n");
	ERRORcheck(heapfile == 0, "BATattach: bad heapfile name\n");
	if (lstat(heapfile, &st) < 0) {
		GDKerror("BATattach: cannot stat heapfile\n");
		return 0;
	}
	ERRORcheck(!S_ISREG(st.st_mode), "BATattach: heapfile must be a regular file\n");
	ERRORcheck(st.st_nlink != 1, "BATattach: heapfile must have only one link\n");
	atomsize = ATOMsize(tt);
	ERRORcheck(st.st_size % atomsize != 0, "BATattach: heapfile size not integral number of atoms\n");
	ERRORcheck(st.st_size / atomsize > (off_t) BUN_MAX, "BATattach: heapfile too large\n");
	cap = (BUN) (st.st_size / atomsize);
	bs = BATcreatedesc(TYPE_void, tt, 1);
	if (bs == NULL)
		return NULL;
	bn = &bs->B;
	BATsetdims(bn);
	GDKfilepath(path, BATDIR, bn->T->heap.filename, "new");
	GDKcreatedir(path);
	if (rename(heapfile, path) < 0) {
		GDKsyserror("BATattach: cannot rename heapfile\n");
		HEAPfree(&bn->T->heap);
		GDKfree(bs);
		return NULL;
	}
	bn->hseqbase = 0;
	BATkey(bn, TRUE);
	BATsetcapacity(bn, cap);
	BATsetcount(bn, cap);
	if (cap > 1) {
		bn->tsorted = 0;
		bn->trevsorted = 0;
		bn->tdense = 0;
		bn->tkey = 0;
	}
	bn->batRestricted = BAT_READ;
	bn->T->heap.size = (size_t) st.st_size;
	bn->T->heap.newstorage = bn->T->heap.storage = (bn->T->heap.size < REMAP_PAGE_MAXSIZE) ? STORE_MEM : STORE_MMAP;
	if (HEAPload(&bn->T->heap, BBP_physical(bn->batCacheid), "tail", TRUE) < 0) {
		HEAPfree(&bn->T->heap);
		GDKfree(bs);
		return NULL;
	}
	BBPcacheit(bs, 1);
	return bn;
}

/*
 * The routine BATclone creates a bat with the same types as b.
 */
BAT *
BATclone(BAT *b, BUN cap)
{
	BAT *c = BATnew(b->htype, b->ttype, cap);

	if (c && c->htype == TYPE_void && b->hseqbase != oid_nil)
		BATseqbase(c, b->hseqbase);
	if (c && c->ttype == TYPE_void && b->tseqbase != oid_nil)
		BATseqbase(BATmirror(c), b->tseqbase);
	return c;
}

/*
 * If the BAT runs out of storage for BUNS it will reallocate space.
 * For memory mapped BATs we simple extend the administration after
 * having an assurance that the BAT still can be safely stored away.
 *
 * Most BAT operations use a BAT to assemble the result. In several
 * cases it is rather difficult to give a precise estimate of the
 * required space.  The routine BATguess is used internally for this
 * purpose.  It balances the cost of small BATs with their probability
 * of occurrence.  Small results BATs are more likely than 100M BATs.
 *
 * Likewise, the routines Hgrows and Tgrows provides a heuristic to
 * enlarge the space.
 */
BUN
BATguess(BAT *b)
{
	BUN newcap;

	BATcheck(b, "BATguess");
	newcap = b->batCount;
	if (newcap < 10 * BATTINY)
		return newcap;
	if (newcap < 50 * BATTINY)
		return newcap / 2;
	if (newcap < 100 * BATTINY)
		return newcap / 10;
	return newcap / 100;
}

BUN
BATgrows(BAT *b)
{
	BUN oldcap, newcap;

	BATcheck(b, "BATgrows");

	newcap = oldcap = BATcapacity(b);
	if (newcap < BATTINY)
		newcap = 2 * BATTINY;
	else if (newcap < 10 * BATTINY)
		newcap = 4 * newcap;
	else if (newcap < 50 * BATTINY)
		newcap = 2 * newcap;
	else if ((double) newcap * BATMARGIN <= (double) BUN_MAX)
		newcap = (BUN) ((double) newcap * BATMARGIN);
	else
		newcap = BUN_MAX;
	if (newcap == oldcap) {
		if (newcap <= BUN_MAX - 10)
			newcap += 10;
		else
			newcap = BUN_MAX;
	}
	return newcap;
}

/*
 * The routine should ensure that the BAT keeps its location in the
 * BAT buffer.
 *
 * Overflow in the other heaps are dealt with in the atom routines.
 * Here we merely copy their references into the new administration
 * space.
 */
BAT *
BATextend(BAT *b, BUN newcap)
{
	size_t hheap_size = newcap, theap_size = newcap;

	assert(newcap <= BUN_MAX);
	BATcheck(b, "BATextend");
	/*
	 * The main issue is to properly predict the new BAT size.
	 * storage overflow. The assumption taken is that capacity
	 * overflow is rare. It is changed only when the position of
	 * the next available BUN surpasses the free area marker.  Be
	 * aware that the newcap should be greater than the old value,
	 * otherwise you may easily corrupt the administration of
	 * malloc.
	 */
	if (newcap <= BATcapacity(b)) {
		return b;
	}

	b->batCapacity = newcap;

	hheap_size *= Hsize(b);
	if (b->H->heap.base && GDKdebug & HEAPMASK)
		fprintf(stderr, "#HEAPextend in BATextend %s " SZFMT " " SZFMT "\n", b->H->heap.filename, b->H->heap.size, hheap_size);
	if (b->H->heap.base && HEAPextend(&b->H->heap, hheap_size) < 0)
		return NULL;
	theap_size *= Tsize(b);
	if (b->T->heap.base && GDKdebug & HEAPMASK)
		fprintf(stderr, "#HEAPextend in BATextend %s " SZFMT " " SZFMT "\n", b->T->heap.filename, b->T->heap.size, theap_size);
	if (b->T->heap.base && HEAPextend(&b->T->heap, theap_size) < 0)
		return NULL;
	HASHdestroy(b);
	IMPSdestroy(b);
	return b;
}



/*
 * @+ BAT destruction
 * BATclear quickly removes all elements from a BAT. It must respect
 * the transaction rules; so stable elements must be moved to the
 * "deleted" section of the BAT (they cannot be fully deleted
 * yet). For the elements that really disappear, we must free
 * heapspace and unfix the atoms if they have fix/unfix handles. As an
 * optimization, in the case of no stable elements, we quickly empty
 * the heaps by copying a standard small empty image over them.
 */
BAT *
BATclear(BAT *b, int force)
{
	BUN p, q;
	int voidbat;
	BAT *bm;

	BATcheck(b, "BATclear");

	voidbat = 0;
	bm = BATmirror(b);

	if (BAThdense(b) && b->htype == TYPE_void) {
		voidbat = 1;
	}
	if (BATtdense(b) && b->ttype == TYPE_void) {
		voidbat = 1;
	}

	/* small BAT: delete all elements by hand */
	if (!force && !voidbat && b->batCount < 20) {
		BATloopDEL(b, p, q) {
			p = BUNdelete(b, p, FALSE);
		}
		return b;
	}

	/* kill all search accelerators */
	if (b->H->hash) {
		HASHremove(b);
	}
	if (b->T->hash) {
		HASHremove(bm);
	}
	IMPSdestroy(b);

	/* we must dispose of all inserted atoms */
	if (b->batDeleted == b->batInserted &&
	    BATatoms[b->htype].atomDel == NULL &&
	    BATatoms[b->ttype].atomDel == NULL) {
		Heap hh, th;

		/* no stable elements: we do a quick heap clean */
		/* need to clean heap which keep data even though the
		   BUNs got removed. This means reinitialize when
		   free > 0
		*/
		size_t cap = 0;

		memset(&hh, 0, sizeof(hh));
		memset(&th, 0, sizeof(th));
		if (b->H->vheap &&
		    b->H->vheap->free > 0 &&
		    ATOMheap(b->htype, &hh, cap) < 0) {
			return NULL;
		}
		if (b->T->vheap &&
		    b->T->vheap->free > 0 &&
		    ATOMheap(b->ttype, &th, cap) < 0) {
			if (b->H->vheap && b->H->vheap->free > 0)
				HEAPfree(&hh);
			return NULL;
		}
		assert(b->H->vheap == NULL || b->H->vheap->parentid == ABS(b->batCacheid));
		if (b->H->vheap && b->H->vheap->free > 0) {
			hh.parentid = b->H->vheap->parentid;
			HEAPfree(b->H->vheap);
			*b->H->vheap = hh;
		}
		assert(b->T->vheap == NULL || b->T->vheap->parentid == ABS(b->batCacheid));
		if (b->T->vheap && b->T->vheap->free > 0) {
			th.parentid = b->T->vheap->parentid;
			HEAPfree(b->T->vheap);
			*b->T->vheap = th;
		}
	} else {
		/* do heap-delete of all inserted atoms */
		void (*hatmdel)(Heap*,var_t*) = BATatoms[b->htype].atomDel;
		void (*tatmdel)(Heap*,var_t*) = BATatoms[b->ttype].atomDel;

		/* TYPE_str has no del method, so we shouldn't get here */
		assert(hatmdel == NULL || b->H->width == sizeof(var_t));
		assert(tatmdel == NULL || b->T->width == sizeof(var_t));
		if (hatmdel || tatmdel) {
			BATiter bi = bat_iterator(b);

			for(p = b->batInserted, q = BUNlast(b); p < q; p++) {
				if (hatmdel)
					(*hatmdel)(b->H->vheap, (var_t*) BUNhloc(bi,p));
				if (tatmdel)
					(*tatmdel)(b->T->vheap, (var_t*) BUNtloc(bi,p));
			}
		}
	}

	if (force)
                b->batFirst = b->batDeleted = b->batInserted = 0;
	else
		b->batFirst = b->batInserted;
	BATsetcount(b,0);
	b->batDirty = TRUE;
	BATsettrivprop(b);
	return b;
}

/* free a cached BAT; leave the bat descriptor cached */
int
BATfree(BAT *b)
{
	BATcheck(b, "BATfree");

	/* deallocate all memory for a bat */
	if (b->batCacheid < 0)
		b = BBP_cache(-(b->batCacheid));
	if (b->hident && !default_ident(b->hident))
		GDKfree(b->hident);
	b->hident = BATstring_h;
	if (b->tident && !default_ident(b->tident))
		GDKfree(b->tident);
	b->tident = BATstring_t;
	if (b->H->props)
		PROPdestroy(b->H->props);
	b->H->props = NULL;
	if (b->T->props)
		PROPdestroy(b->T->props);
	b->T->props = NULL;
	HASHdestroy(b);
	IMPSdestroy(b);
	if (b->htype)
		HEAPfree(&b->H->heap);
	else
		assert(!b->H->heap.base);
	if (b->ttype)
		HEAPfree(&b->T->heap);
	else
		assert(!b->T->heap.base);
	if (b->H->vheap) {
		assert(b->H->vheap->parentid == b->batCacheid);
		HEAPfree(b->H->vheap);
	}
	if (b->T->vheap) {
		assert(b->T->vheap->parentid == b->batCacheid);
		HEAPfree(b->T->vheap);
	}

	b = BBP_cache(-b->batCacheid);
	if (b) {
		BBP_cache(b->batCacheid) = NULL;
	}
	return 0;
}

/* free a cached BAT descriptor */
void
BATdestroy( BATstore *bs )
{
	if (bs->H.id && !default_ident(bs->H.id))
		GDKfree(bs->H.id);
	bs->H.id = BATstring_h;
	if (bs->T.id && !default_ident(bs->T.id))
		GDKfree(bs->T.id);
	bs->T.id = BATstring_t;
	if (bs->H.vheap)
		GDKfree(bs->H.vheap);
	if (bs->T.vheap)
		GDKfree(bs->T.vheap);
	if (bs->H.props)
		PROPdestroy(bs->H.props);
	if (bs->T.props)
		PROPdestroy(bs->T.props);
	GDKfree(bs);
}

/*
 * @+ BAT copying
 *
 * BAT copying is an often used operation. So it deserves attention.
 * When making a copy of a BAT, the following aspects are of
 * importance:
 *
 * - the requested head and tail types. The purpose of the copy may be
 *   to slightly change these types (e.g. void <-> oid). We may also
 *   remap between types as long as they share the same
 *   ATOMstorage(type), i.e. the types have the same physical
 *   implementation. We may even want to allow 'dirty' trick such as
 *   viewing a flt-column suddenly as int.
 *
 *   To allow such changes, the desired head- and tail-types are a
 *   parameter of BATcopy.
 *
 * - access mode. If we want a read-only copy of a read-only BAT, a
 *   VIEW may do (in this case, the user may be after just an
 *   independent BAT header and id). This is indicated by the
 *   parameter (writable = FALSE).
 *
 *   In other cases, we really want an independent physical copy
 *   (writable = TRUE).  Changing the mode to BAT_WRITE will be a
 *   zero-cost operation if the BAT was copied with (writable = TRUE).
 *
 * In GDK, the result is a BAT that is BAT_WRITE iff (writable ==
 * TRUE).
 *
 * In these cases the copy becomes a logical view on the original,
 * which ensures that the original cannot be modified or destroyed
 * (which could affect the shared heaps).
 */
static int
heapcopy(BAT *bn, char *ext, Heap *dst, Heap *src)
{
	if (src->filename && src->newstorage != STORE_MEM) {
		const char *nme = BBP_physical(bn->batCacheid);

		if ((dst->filename = GDKmalloc(strlen(nme) + 12)) == NULL)
			return -1;
		GDKfilepath(dst->filename, NULL, nme, ext);
	}
	return HEAPcopy(dst, src);
}

static void
heapfree(Heap *dst, Heap *src)
{
	if (src->filename == NULL) {
		src->filename = dst->filename;
		dst->filename = NULL;
	}
	HEAPfree(dst);
	*dst = *src;
}

static int
wrongtype(int t1, int t2)
{
	/* check if types are compatible. be extremely forgiving */
	if (t1) {
		t1 = ATOMtype(ATOMstorage(t1));
		t2 = ATOMtype(ATOMstorage(t2));
		if (t1 != t2) {
			if (ATOMvarsized(t1) ||
			    ATOMvarsized(t2) ||
			    ATOMsize(t1) != ATOMsize(t2) ||
			    ATOMalign(t1) != ATOMalign(t2) ||
			    BATatoms[t1].atomFix ||
			    BATatoms[t2].atomFix)
				return TRUE;
		}
	}
	return FALSE;
}

/*
 * There are four main implementation cases:
 * (1) we are allowed to return a view (zero effort),
 * (2) the result is void,void (zero effort),
 * (3) we can copy the heaps (memcopy, or even VM page sharing)
 * (4) we must insert BUN-by-BUN into the result (fallback)
 * The latter case is still optimized for the case that the result
 * is bat[void,T] for a simple fixed-size type T. In that case we
 * do inline array[T] inserts.
 */
/* TODO make it simpler, ie copy per column */
BAT *
BATcopy(BAT *b, int ht, int tt, int writable)
{
	BUN bunstocopy = BUN_NONE;
	BUN cnt;
	BAT *bn = NULL;

	BATcheck(b, "BATcopy");
	assert(ht != TYPE_bat);
	assert(tt != TYPE_bat);
	cnt = b->batCount;

	/* maybe a bit ugly to change the requested bat types?? */
	if (b->htype == TYPE_void && !writable)
		ht = TYPE_void;
	if (b->ttype == TYPE_void && !writable)
		tt = TYPE_void;

	if (ht != b->htype && wrongtype(ht, b->htype)) {
		GDKerror("BATcopy: wrong head-type requested\n");
		return NULL;
	}
	if (tt != b->ttype && wrongtype(tt, b->ttype)) {
		GDKerror("BATcopy: wrong tail-type requested\n");
		return NULL;
	}

	/* first try case (1); create a view, possibly with different atom-types */
	if (BAThrestricted(b) == BAT_READ && BATtrestricted(b) == BAT_READ && !writable) {
		bn = VIEWcreate(b, b);
		if (bn == NULL)
			return NULL;
		if (ht != bn->htype) {
			assert(bn->H != bn->T);
			bn->htype = ht;
			bn->hvarsized = ATOMvarsized(ht);
			bn->hseqbase = b->hseqbase;
		}
		if (tt != bn->ttype) {
			assert(bn->H != bn->T);
			bn->ttype = tt;
			bn->tvarsized = ATOMvarsized(tt);
			bn->tseqbase = b->tseqbase;
		}
	} else {
		/* check whether we need case (4); BUN-by-BUN copy (by
		 * setting bunstocopy != BUN_NONE) */
		if (ATOMsize(ht) != ATOMsize(b->htype) ||
		    ATOMsize(tt) != ATOMsize(b->ttype)) {
			/* oops, void materialization */
			bunstocopy = cnt;
		} else if (BATatoms[ht].atomFix || BATatoms[tt].atomFix) {
			/* oops, we need to fix/unfix atoms */
			bunstocopy = cnt;
		} else if (isVIEW(b)) {
			/* extra checks needed for views */
			bat hp = VIEWhparent(b), tp = VIEWtparent(b);

			if (isVIEWCOMBINE(b) ||	/* oops, mirror view! */
			    /* reduced slice view: do not copy too
			     * much garbage */
			    (hp != 0 && BATcapacity(BBP_cache(hp)) > cnt + cnt) ||
			    (tp != 0 && BATcapacity(BBP_cache(tp)) > cnt + cnt))
				bunstocopy = cnt;
		}

		bn = BATnew(ht, tt, MAX(1, bunstocopy == BUN_NONE ? 0 : bunstocopy));
		if (bn == NULL)
			return NULL;

		if (bn->hvarsized && bn->htype) {
			bn->H->shift = b->H->shift;
			bn->H->width = b->H->width;
			if (HEAPextend(&bn->H->heap, BATcapacity(bn) << bn->H->shift) < 0)
				goto bunins_failed;
		}
		if (bn->tvarsized && bn->ttype) {
			bn->T->shift = b->T->shift;
			bn->T->width = b->T->width;
			if (HEAPextend(&bn->T->heap, BATcapacity(bn) << bn->T->shift) < 0)
				goto bunins_failed;
		}

		if (ht == TYPE_void && tt == TYPE_void) {
			/* case (2): a void,void result => nothing to
			 * copy! */
			bn->H->heap.free = 0;
			bn->T->heap.free = 0;
		} else if (bunstocopy == BUN_NONE) {
			/* case (3): just copy the heaps; if possible
			 * with copy-on-write VM support */
			BUN hcap = 0, tcap = 0;
			Heap bhhp, bthp, hhp, thp;
			memset(&bhhp, 0, sizeof(Heap));
			memset(&bthp, 0, sizeof(Heap));
			memset(&hhp, 0, sizeof(Heap));
			memset(&thp, 0, sizeof(Heap));

			if ((b->htype && heapcopy(bn, "head", &bhhp, &b->H->heap) < 0) ||
			    (b->ttype && heapcopy(bn, "tail", &bthp, &b->T->heap) < 0) ||
			    (bn->H->vheap && heapcopy(bn, "hheap", &hhp, b->H->vheap) < 0) ||
			    (bn->T->vheap && heapcopy(bn, "theap", &thp, b->T->vheap) < 0)) {
				HEAPfree(&thp);
				HEAPfree(&hhp);
				HEAPfree(&bthp);
				HEAPfree(&bhhp);
				BBPreclaim(bn);
				return NULL;
			}
			/* succeeded; replace dummy small heaps by the
			 * real ones */
			heapfree(&bn->H->heap, &bhhp);
			heapfree(&bn->T->heap, &bthp);
			hhp.parentid = bn->batCacheid;
			thp.parentid = bn->batCacheid;
			if (bn->H->vheap)
				heapfree(bn->H->vheap, &hhp);
			if (bn->T->vheap)
				heapfree(bn->T->vheap, &thp);

			/* make sure we use the correct capacity */
			hcap = (BUN) (bn->htype ? bn->H->heap.size >> bn->H->shift : 0);
			tcap = (BUN) (bn->ttype ? bn->T->heap.size >> bn->T->shift : 0);
			if (hcap && tcap)
				bn->U->capacity = MIN(hcap, tcap);
			else if (hcap)
				bn->U->capacity = hcap;
			else
				bn->U->capacity = tcap;


			/* first/inserted must point equally far into
			 * the heap as in the source */
			bn->batFirst = b->batFirst;
			bn->batInserted = b->batInserted;
		} else if (BATatoms[ht].atomFix || BATatoms[tt].atomFix || (ht && tt) || ATOMstorage(MAX(ht, tt)) >= TYPE_str) {
			/* case (4): one-by-one BUN insert (really slow) */
			BUN p, q, r = BUNfirst(bn);
			BATiter bi = bat_iterator(b);

			BATloop(b, p, q) {
				const void *h = BUNhead(bi, p);
				const void *t = BUNtail(bi, p);

				bunfastins_nocheck(bn, r, h, t, Hsize(bn), Tsize(bn));
				r++;
			}
		} else if ((ht && b->htype == TYPE_void) || (tt && b->ttype == TYPE_void)) {
			/* case (4): optimized for unary void
			 * materialization */
			oid cur = ht ? b->hseqbase : b->tseqbase, *dst = (oid *) (ht ? bn->H->heap.base : bn->T->heap.base);
			oid inc = (cur != oid_nil);

			bn->H->heap.free = bn->T->heap.free = 0;
			if (ht)
				bn->H->heap.free = bunstocopy * sizeof(oid);
			else
				bn->T->heap.free = bunstocopy * sizeof(oid);
			while (bunstocopy--) {
				*dst++ = cur;
				cur += inc;
			}
		} else {
			/* case (4): optimized for simple array copy */
			int tpe = ATOMstorage(ht | tt);
			BUN p = BUNfirst(b);
			char *cur = (ht ? Hloc(b, p) : Tloc(b, p));
			char *d = (ht ? Hloc(bn, 0) : Tloc(bn, 0));

			bn->H->heap.free = bn->T->heap.free = 0;
			if (ht)
				bn->H->heap.free = bunstocopy * Hsize(bn);
			else
				bn->T->heap.free = bunstocopy * Tsize(bn);

			if (tpe == TYPE_bte) {
				bte *src = (bte *) cur, *dst = (bte *) d;

				while (bunstocopy--) {
					*dst++ = *src++;
				}
			} else if (tpe == TYPE_sht) {
				sht *src = (sht *) cur, *dst = (sht *) d;

				while (bunstocopy--) {
					*dst++ = *src++;
				}
			} else if ((tpe == TYPE_int) || (tpe == TYPE_flt)) {
				int *src = (int *) cur, *dst = (int *) d;

				while (bunstocopy--) {
					*dst++ = *src++;
				}
			} else {
				lng *src = (lng *) cur, *dst = (lng *) d;

				while (bunstocopy--) {
					*dst++ = *src++;
				}
			}
		}
		/* copy all properties (size+other) from the source bat */
		BATsetcount(bn, cnt);
	}
	/* set properties (note that types may have changed in the copy) */
	if (ATOMtype(ht) == ATOMtype(b->htype)) {
		ALIGNsetH(bn, b);
	} else if (ATOMtype(ATOMstorage(ht)) == ATOMtype(ATOMstorage(b->htype))) {
		bn->hsorted = b->hsorted || (cnt <= 1 && BATatoms[b->htype].linear);
		bn->hrevsorted = b->hrevsorted || (cnt <= 1 && BATatoms[b->htype].linear);
		bn->hdense = b->hdense;
		if (b->hkey)
			BATkey(bn, TRUE);
		bn->H->nonil = b->H->nonil;
	} else {
		bn->hsorted = bn->hrevsorted = (cnt <= 1 && BATatoms[b->htype].linear);
		bn->hdense = bn->H->nonil = 0;
	}
	if (ATOMtype(tt) == ATOMtype(b->ttype)) {
		ALIGNsetT(bn, b);
	} else if (ATOMtype(ATOMstorage(tt)) == ATOMtype(ATOMstorage(b->ttype))) {
		bn->tsorted = b->tsorted || (cnt <= 1 && BATatoms[b->ttype].linear);
		bn->trevsorted = b->trevsorted || (cnt <= 1 && BATatoms[b->ttype].linear);
		bn->tdense = b->tdense;
		if (b->tkey)
			BATkey(BATmirror(bn), TRUE);
		bn->T->nonil = b->T->nonil;
	} else {
		bn->tsorted = bn->trevsorted = (cnt <= 1 && BATatoms[b->ttype].linear);
		bn->tdense = bn->T->nonil = 0;
	}
	if (writable != TRUE)
		bn->batRestricted = BAT_READ;
	return bn;
      bunins_failed:
	BBPreclaim(bn);
	return NULL;
}

#define un_move(src, dst, sz)						\
	do {								\
		if (sz == 8) {						\
			* (lng *) dst = * (lng *) src;			\
		} else if (sz == 4) {					\
			* (int *) dst = * (int *) src;			\
		} else {						\
			str _dst = (str) dst, _src = (str) src, _end = _src + sz; \
									\
			while (_src < _end)				\
				*_dst++ = *_src++;			\
		}							\
	} while (0)
#define hacc_update(func, get, p, idx)					\
	do {								\
		if (b->H->hash) {					\
			func(b->H->hash, idx, get(bi, p), p < last);	\
		}							\
	} while (0)
#define tacc_update(func, get, p, idx)					\
	do {								\
		if (b->T->hash) {					\
			func(b->T->hash, idx, get(bi, p), p < last);	\
		}							\
	} while (0)
#define acc_move(l, p, idx2, idx1)					\
	do {								\
		char tmp[16];						\
		/* avoid compiler warning: dereferencing type-punned pointer \
		 * will break strict-aliasing rules */			\
		char *tmpp = tmp;					\
									\
		assert(hs <= 16);					\
		assert(ts <= 16);					\
		if (b->H->hash) {					\
			HASHmove(b->H->hash, idx2, idx1, BUNhead(bi, l), l < last); \
		}							\
		if (b->T->hash) {					\
			HASHmove(b->T->hash, idx2, idx1, BUNtail(bi, l), l < last); \
		}							\
									\
		/* move first to tmp */					\
		un_move(Hloc(b, l), tmpp, hs);				\
		/* move delete to first */				\
		un_move(Hloc(b, p), Hloc(b, l), hs);			\
		/* move first to deleted */				\
		un_move(tmpp, Hloc(b, p), hs);				\
									\
		/* move first to tmp */					\
		un_move(Tloc(b, l), tmpp, ts);				\
		/* move delete to first */				\
		un_move(Tloc(b, p), Tloc(b, l), ts);			\
		/* move first to deleted */				\
		un_move(tmpp, Tloc(b, p), ts);				\
	} while (0)

/*
 * @- BUN Insertion
 * Insertion into a BAT is split into two operations BUNins and
 * BUNfastins.  The former should be used when integrity enforcement
 * and index maintenance is required.  The latter is used to quickly
 * insert the BUN into the result without any additional check.  For
 * those cases where speed is required, the type decoding can be
 * circumvented by asking for a BUN using BATbunalloc and fill it
 * directly. See gdk.mx for the bunfastins(b,h,t) macros.
 */
BAT *
BUNfastins(BAT *b, const void *h, const void *t)
{
	bunfastins(b, h, t);
	if (!b->batDirty)
		b->batDirty = TRUE;
	return b;
      bunins_failed:
	return NULL;
}


static void
setcolprops(BAT *b, COLrec *col, const void *x)
{
	int isnil = col->type != TYPE_void &&
		atom_CMP(x, ATOMnilptr(col->type), col->type) == 0;
	BATiter bi;
	BUN pos;
	const void *prv;
	int cmp;

	/* x may only be NULL if the column type is VOID */
	assert(x != NULL || col->type == TYPE_void);
	if (b->batCount == 0) {
		/* first value */
		col->sorted = col->revsorted = BATatoms[col->type].linear != 0;
		col->key |= 1;
		if (col->type == TYPE_void) {
			if (x) {
				col->seq = * (const oid *) x;
			}
			col->nil = col->seq == oid_nil;
			col->nonil = !col->nil;
		} else {
			col->nil = isnil;
			col->nonil = !isnil;
			if (col->type == TYPE_oid) {
				col->dense = !isnil;
				col->seq = * (const oid *) x;
			}
		}
	} else if (col->type == TYPE_void) {
		/* not the first value in a VOID column: we keep the
		 * seqbase and x is not used, so only some properties
		 * are affected */
		if (col->seq != oid_nil) {
			col->revsorted = 0;
			col->nil = 0;
			col->nonil = 1;
		} else {
			col->key = 0;
			col->nil = 1;
			col->nonil = 0;
		}
	} else {
		bi = bat_iterator(b);
		pos = BUNlast(b);
		prv = col == b->H ? BUNhead(bi, pos - 1) : BUNtail(bi, pos - 1);
		cmp = atom_CMP(prv, x, col->type);

		if (col->key == 1 && /* assume outside check if BOUND2BTRUE */
		    (cmp == 0 || /* definitely not KEY */
		     (b->batCount > 1 && /* can't guarantee KEY if unordered */
		      ((col->sorted && cmp > 0) ||
		       (col->revsorted && cmp < 0) ||
		       (!col->sorted && !col->revsorted))))) {
			col->key = 0;
			col->nokey[0] = pos - 1;
			col->nokey[1] = pos;
		}
		if (col->sorted && cmp > 0) {
			/* out of order */
			col->sorted = 0;
			col->nosorted = pos;
		}
		if (col->revsorted && cmp < 0) {
			/* out of order */
			col->revsorted = 0;
			col->norevsorted = pos;
		}
		if (col->dense && (cmp >= 0 || * (const oid *) prv + 1 != * (const oid *) x)) {
			col->dense = 0;
			col->nodense = pos;
		}
		if (isnil) {
			col->nonil = 0;
			col->nil = 1;
		}
	}
}

/* maybe materialize a VOID column */
#define void_materialize(b, x)						\
	do {								\
		if ((b)->x##type == TYPE_void &&			\
		    (b)->x##seqbase != oid_nil) {			\
			if (* (oid *) x == oid_nil ||			\
			    ((b)->batCount > 0 &&			\
			     (b)->x##seqbase + (b)->batCount != *(oid *) x)) { \
				if (((b) = BATmaterialize##x(b)) == NULL) \
					return NULL;			\
				countonly = 0;				\
			} else if ((b)->batCount == 0) {		\
				(b)->x##seqbase = * (oid *) x;	\
			}						\
		}							\
	} while (0)

/*
 * The interface routine should also perform integrity checks.  Null
 * values should have been obtained at a higher level.  This code
 * assumes that new elements are appended to the BUN list.
 */
BAT *
BUNins(BAT *b, const void *h, const void *t, bit force)
{
	int countonly;
	BUN p;
	BAT *bm;

	BATcheck(b, "BUNins");
	BATcheck(h, "BUNins: head value is nil");

	countonly = (b->htype == TYPE_void && b->ttype == TYPE_void);
	bm = BBP_cache(-b->batCacheid);

	void_materialize(b, h);
	void_materialize(b, t);

	if (b->batSet && BUNlocate(b, h, t) != BUN_NONE) {
		return b;
	}
	if ((b->hkey & BOUND2BTRUE) && (p = BUNfnd(b, h)) != BUN_NONE) {
		if (BUNinplace(b, p, h, t, force) == NULL)
			return NULL;
	} else if ((b->tkey & BOUND2BTRUE) && (p = BUNfnd(bm, t)) != BUN_NONE) {
		if (BUNinplace(bm, p, t, h, force) == NULL)
			return NULL;
	} else {
		size_t hsize = 0, tsize = 0;

		p = BUNlast(b);	/* insert at end */
		if (p == BUN_MAX || b->batCount == BUN_MAX) {
			GDKerror("BUNins: bat too large\n");
			return NULL;
		}

		ALIGNins(b, "BUNins", force);
		b->batDirty = 1;
		if (b->H->hash && b->H->vheap)
			hsize = b->H->vheap->size;
		if (b->T->hash && b->T->vheap)
			tsize = b->T->vheap->size;

		setcolprops(b, b->H, h);
		setcolprops(b, b->T, t);

		if (!countonly) {
			bunfastins(b, h, t);
		} else {
			BATsetcount(b, b->batCount + 1);
		}

		if (b->H->hash) {
			HASHins(b, p, h);
			if (hsize && hsize != b->H->vheap->size)
				HEAPwarm(b->H->vheap);
		}
		if (b->T->hash) {
			HASHins(bm, p, t);

			if (tsize && tsize != b->T->vheap->size)
				HEAPwarm(b->T->vheap);
		}
	}
	IMPSdestroy(b); /* no support for inserts in imprints yet */
	return b;
      bunins_failed:
	return NULL;
}

oid
MAXoid(BAT *i)
{
	BATiter ii = bat_iterator(i);
	oid o = i->hseqbase - 1;

	if (i->batCount)
		o = *(oid *) BUNhead(ii, BUNlast(i) - 1);
	if (!BAThordered(i)) {
		BUN r, s;

		BATloop(i, r, s) {
			oid v = *(oid *) BUNhead(ii, r);

			if (v > o)
				o = v;
		}
	}
	return o;
}

/*
 * @+ BUNappend
 * The BUNappend function can be used to add a single value to void
 * and oid headed bats. The new head value will be a unique number,
 * (max(bat)+1).
 */
BAT *
BUNappend(BAT *b, const void *t, bit force)
{
	BUN i;
	BUN p;
	BAT *bm;
	const void *h = NULL;
	oid id = 0;
	int countonly;
	size_t hsize = 0, tsize = 0;

	BATcheck(b, "BUNappend");

	if (b->htype != TYPE_void && b->htype != TYPE_oid) {
		GDKerror("BUNappend: can only append to void and oid bats\n");
		return NULL;
	}

	bm = BBP_cache(-b->batCacheid);
	if ((b->tkey & BOUND2BTRUE) && BUNfnd(bm, t) != BUN_NONE) {
		return b;
	}

	p = BUNlast(b);		/* insert at end */
	if (p == BUN_MAX || b->batCount == BUN_MAX) {
		GDKerror("BUNappend: bat too large\n");
		return NULL;
	}

	i = p;
	ALIGNapp(b, "BUNappend", force);
	b->batDirty = 1;
	countonly = (b->htype == TYPE_void && b->ttype == TYPE_void);
	if (b->H->hash && b->H->vheap)
		hsize = b->H->vheap->size;
	if (b->T->hash && b->T->vheap)
		tsize = b->T->vheap->size;

	if (b->htype == TYPE_oid) {
		h = &id;
		id = b->batCount == 0 ? 0 : MAXoid(b) + 1;
	}
	void_materialize(b, t);

	setcolprops(b, b->H, h);
	setcolprops(b, b->T, t);

	if (!countonly) {
		bunfastins(b, h, t);
	} else {
		BATsetcount(b, b->batCount + 1);
	}


	IMPSdestroy(b); /* no support for inserts in imprints yet */

	/* first adapt the hashes; then the user-defined accelerators.
	 * REASON: some accelerator updates (qsignature) use the hashes!
	 */
	if (b->H->hash && h) {
		HASHins(b, i, h);
		if (hsize && hsize != b->H->vheap->size)
			HEAPwarm(b->H->vheap);
	}
	if (b->T->hash) {
		HASHins(bm, i, t);

		if (tsize && tsize != b->T->vheap->size)
			HEAPwarm(b->T->vheap);
	}
	return b;
      bunins_failed:
	return NULL;
}


/*
 * @- BUN Delete
 * Deletes should maintain the BAT as a contiguous array. This
 * implementation permits using a BATloop for(;;) construction to use
 * the BUNdelete routines, by not modifying what is in front of the
 * deleted bun.
 *
 * This routine returns the next BUN in b after deletion of p.  Note:
 * to cause less trouble when updating BATs with void columns the
 * delete policy has been changed. Deleted volatile elements are now
 * being overwritten by the last element; instead of causing a cascade
 * of moves. The sequential deletability property is changed somewhat:
 * instead of doing
 * 	BATloop(b,p,q) BUNdelete(b,p,FALSE)
 * one now must do:
 *	BATloopDEL(b,p) p = BUNdelete(b,p,FALSE)
 */
#define hashins(h,i,v,n) HASHins_any(h,i,v)
#define hashdel(h,i,v,n) HASHdel(h,i,v,n)

static inline BUN
BUNdelete_(BAT *b, BUN p, bit force)
{
	BATiter bi = bat_iterator(b);
	BAT *bm = BBP_cache(-b->batCacheid);
	BUN l, last = BUNlast(b) - 1;
	BUN idx1, idx2;

	ALIGNdel(b, "BUNdelete", force);	/* zap alignment info */

	/*
	 * @- Committed Delete.
	 * Deleting a (committed) bun: the first and deleted swap position.
	 */
	if (p < b->batInserted && !force) {
		idx1 = p;
		if (p == b->batFirst) {	/* first can simply be discarded */
			hacc_update(hashdel,BUNhead,p,idx1);
			tacc_update(hashdel,BUNtail,p,idx1);

			if (BAThdense(b)) {
				bm->tseqbase = ++b->hseqbase;
			}
			if (BATtdense(b)) {
				bm->hseqbase = ++b->tseqbase;
			}
		} else {
			unsigned short hs = Hsize(b), ts = Tsize(b);

			hacc_update(hashdel,BUNhead,p,idx1);
			tacc_update(hashdel,BUNtail,p,idx1);

			l = BUNfirst(b);
			idx2 = l;
			acc_move(l,p,idx2,idx1);
			if (b->hsorted) {
				b->hsorted = FALSE;
				b->H->nosorted = idx1;
			}
			if (b->hrevsorted) {
				b->hrevsorted = FALSE;
				b->H->norevsorted = idx1;
			}
			if (b->hdense) {
				b->hdense = FALSE;
				b->H->nodense = idx1;
			}
			if (b->tsorted) {
				b->tsorted = FALSE;
				b->T->nosorted = idx1;
			}
			if (b->trevsorted) {
				b->trevsorted = FALSE;
				b->T->norevsorted = idx1;
			}
			if (b->tdense) {
				b->tdense = FALSE;
				b->T->nodense = idx1;
			}
		}
		b->batFirst++;
	} else {
		/*
		 * @- Uncommitted Delete.
		 * This bun was not committed, and should therefore
		 * disappear. The last inserted bun (if present) is
		 * copied over it.
		 */
		int (*hunfix) (const void *) = BATatoms[b->htype].atomUnfix;
		int (*tunfix) (const void *) = BATatoms[b->ttype].atomUnfix;
		void (*hatmdel) (Heap *, var_t *) = BATatoms[b->htype].atomDel;
		void (*tatmdel) (Heap *, var_t *) = BATatoms[b->ttype].atomDel;

		if (hunfix) {
			(*hunfix) (BUNhead(bi, p));
		}
		if (tunfix) {
			(*tunfix) (BUNtail(bi, p));
		}
		if (hatmdel) {
			assert(b->H->width == sizeof(var_t));
			(*hatmdel) (b->H->vheap, (var_t *) BUNhloc(bi, p));
		}
		if (tatmdel) {
			assert(b->T->width == sizeof(var_t));
			(*tatmdel) (b->T->vheap, (var_t *) BUNtloc(bi, p));
		}
		idx1 = p;
		hacc_update(hashdel,BUNhead,p,idx1);
		tacc_update(hashdel,BUNtail,p,idx1);
		idx2 = last;
		if (p != last) {
			unsigned short hs = Hsize(b), ts = Tsize(b);
			BATiter bi2 = bat_iterator(b);

			acc_move(last,p,idx2,idx1);
			/* If a column was sorted before the BUN was
			   deleted, check whether it is still sorted
			   afterward.  This is done by comparing the
			   value that was put in place of the deleted
			   value is still ordered correctly with
			   respect to the following value.  Note that
			   if p+1==last, the new value is now the
			   last, so no comparison is needed. */
			if (b->hsorted) {
				if (p + 1 < last &&
				    ATOMcmp(b->htype, BUNhead(bi, p), BUNhead(bi2, p + 1)) > 0) {
					b->hsorted = FALSE;
					b->H->nosorted = idx1;
				}
				if (b->hdense) {
					b->hdense = FALSE;
					b->H->nodense = idx1;
				}
			}
			if (b->hrevsorted) {
				if (p + 1 < last &&
				    ATOMcmp(b->htype, BUNhead(bi, p), BUNhead(bi2, p + 1)) < 0) {
					b->hrevsorted = FALSE;
					b->H->norevsorted = idx1;
				}
			}
			if (b->tsorted) {
				if (p + 1 < last &&
				    ATOMcmp(b->ttype, BUNtail(bi, p), BUNtail(bi2, p + 1)) > 0) {
					b->tsorted = FALSE;
					b->H->nosorted = idx1;
				}
				if (b->tdense) {
					b->tdense = FALSE;
					b->T->nodense = idx1;
				}
			}
			if (b->trevsorted) {
				if (p + 1 < last &&
				    ATOMcmp(b->ttype, BUNtail(bi, p), BUNtail(bi2, p + 1)) < 0) {
					b->trevsorted = FALSE;
					b->H->norevsorted = idx1;
				}
			}
		}
		b->H->heap.free -= Hsize(b);
		b->T->heap.free -= Tsize(b);
		p--;
	}
	b->batCount--;
	b->batDirty = 1;	/* bat is dirty */
	IMPSdestroy(b); /* no support for inserts in imprints yet */
	return p;
}

BUN
BUNdelete(BAT *b, BUN p, bit force)
{
	if (p == BUN_NONE) {
		return p;
	}
	if ((b->htype == TYPE_void && b->hseqbase != oid_nil) || (b->ttype == TYPE_void && b->tseqbase != oid_nil)) {
		BUN last = BUNlast(b) - 1;

		if ((p < b->batInserted || p != last) && !force) {
			b = BATmaterialize(b);
			if (b == NULL)
				return BUN_NONE;
		}
	}
	return BUNdelete_(b, p, force);
}

BAT *
BUNdel(BAT *b, const void *x, const void *y, bit force)
{
	BUN p;

	BATcheck(b, "BUNdel");
	BATcheck(x, "BUNdel: head value is nil");

	if ((p = BUNlocate(b, x, y)) != BUN_NONE) {
		ALIGNdel(b, "BUNdel", force);	/* zap alignment info */
		BUNdelete(b, p, force);
		return b;
	}
	return 0;
}

/*
 * The routine BUNdelHead is similar, but removes all BUNs whose head
 * matches the argument passed.
 */
BAT *
BUNdelHead(BAT *b, const void *x, bit force)
{
	BUN p;

	BATcheck(b, "BUNdelHead");

	if (x == NULL) {
		x = ATOMnilptr(b->htype);
	}
	if ((p = BUNfnd(b, x)) != BUN_NONE) {
		ALIGNdel(b, "BUNdelHead", force);	/* zap alignment info */
		do {
			BUNdelete(b, p, force);
		} while ((p = BUNfnd(b, x)) != BUN_NONE);
	}
	return b;
}

/*
 * Deletion of strings leads to garbage on the variable stack.  This
 * can be removed by compaction of the BAT through copying it.
 *
 * @-  BUN replace
 * The last operation in this context is BUN replace. It assumes that
 * the header denotes a key. The old value association is destroyed
 * (if it exists in the first place) and the new value takes its
 * place.
 *
 * In order to make updates on void columns workable; replaces on them
 * are always done in-place. Performing them without bun-movements
 * greatly simplifies the problem. The 'downside' is that when
 * transaction management has to be performed, replaced values should
 * be saved explicitly.
 */
BAT *
BUNinplace(BAT *b, BUN p, const void *h, const void *t, bit force)
{
	if (p >= b->batInserted || force) {
		/* uncommitted BUN elements */
		BUN last = BUNlast(b) - 1;
		BAT *bm = BBP_cache(-b->batCacheid);
		BUN pit = p;
		BATiter bi = bat_iterator(b);
		size_t tsize = b->tvarsized ? b->T->vheap->size : 0;
		int tt;
		BUN prv, nxt;

		ALIGNinp(b, "BUNreplace", force);	/* zap alignment info */
		if (b->T->nil &&
		    atom_CMP(BUNtail(bi, p), ATOMnilptr(b->ttype), b->ttype) == 0 &&
		    atom_CMP(t, ATOMnilptr(b->ttype), b->ttype) != 0) {
			/* if old value is nil and new value isn't,
			 * we're not sure anymore about the nil
			 * property, so we must clear it */
			b->T->nil = 0;
		}
		tacc_update(hashdel,BUNtail,p,pit);
		Treplacevalue(b, BUNtloc(bi, p), t);
		tacc_update(hashins,BUNtail,p,pit);

		tt = b->ttype;
		prv = p > b->batFirst ? p - 1 : BUN_NONE;
		nxt = p < last ? p + 1 : BUN_NONE;

		if (BATtordered(b)) {
			if ((prv != BUN_NONE &&
			     ATOMcmp(tt, t, BUNtail(bi, prv)) < 0) ||
			    (nxt != BUN_NONE &&
			     ATOMcmp(tt, t, BUNtail(bi, nxt)) > 0)) {
				b->tsorted = FALSE;
				b->T->nosorted = pit;
			} else if (b->ttype != TYPE_void && b->tdense) {
				if ((prv != BUN_NONE &&
				     1 + * (oid *) BUNtloc(bi, prv) != * (oid *) t) ||
				    (nxt != BUN_NONE &&
				     * (oid *) BUNtloc(bi, nxt) != 1 + * (oid *) t)) {
					b->tdense = FALSE;
					b->T->nodense = pit;
				} else if (prv == BUN_NONE &&
					   nxt == BUN_NONE) {
					bm->hseqbase = b->tseqbase = * (oid *) t;
				}
			}
		}
		if (BATtrevordered(b)) {
			if ((prv != BUN_NONE &&
			     ATOMcmp(tt, t, BUNtail(bi, prv)) > 0) ||
			    (nxt != BUN_NONE &&
			     ATOMcmp(tt, t, BUNtail(bi, nxt)) < 0)) {
				b->trevsorted = FALSE;
				b->T->norevsorted = pit;
			}
		}
		if (b->tvarsized && b->T->hash && tsize != b->T->vheap->size)
			HEAPwarm(b->T->vheap);
		if (((b->ttype != TYPE_void) & b->tkey & !(b->tkey & BOUND2BTRUE)) && b->batCount > 1) {
			BATkey(bm, FALSE);
		}
		if (b->T->nonil)
			b->T->nonil = t && atom_CMP(t, ATOMnilptr(b->ttype), b->ttype) != 0;
		b->T->heap.dirty = TRUE;
		if (b->T->vheap)
			b->T->vheap->dirty = TRUE;
	} else {
		/* committed BUN */
		BUNdelete(b, p, force);
		if (BUNins(b, h, t, force) == NULL) {
		      bunins_failed:
			return NULL;
		}
	}
	return b;
}

BAT *
BUNreplace(BAT *b, const void *h, const void *t, bit force)
{
	BUN p;

	BATcheck(b, "BUNreplace");
	BATcheck(h, "BUNreplace: head value is nil");
	BATcheck(t, "BUNreplace: tail value is nil");

	if ((p = BUNfnd(b, h)) == BUN_NONE)
		return b;

	if ((b->tkey & BOUND2BTRUE) && BUNfnd(BATmirror(b), t) != BUN_NONE) {
		return b;
	}
	if (b->ttype == TYPE_void) {
		BUN i;

		/* no need to materialize if value doesn't change */
		if (b->tseqbase == oid_nil || (b->hseqbase + p) == *(oid *) t)
			return b;
		i = p;
		b = BATmaterializet(b);
		if (b == NULL)
			return NULL;
		p = i;
	}

	return BUNinplace(b, p, h, t, force);
}

int
void_inplace(BAT *b, oid id, const void *val, bit force)
{
	int res = GDK_SUCCEED;
	BUN p = BUN_NONE;
	BUN oldInserted = b->batInserted;
	BATiter bi = bat_iterator(b);

	assert(b->htype == TYPE_void);
	assert(b->hseqbase != oid_nil);
	assert(b->batCount > (id -b->hseqbase));

	b->batInserted = 0;
	BUNfndVOID(p, bi, (ptr) &id);

	assert(force || p >= b->batInserted);	/* we don't want delete/ins */
	assert(force || !b->batRestricted);
	if (!BUNinplace(b, p, (ptr) &id, val, force))
		 res = GDK_FAIL;

	b->batInserted = oldInserted;
	return res;
}

BUN
void_replace_bat(BAT *b, BAT *u, bit force)
{
	BUN nr = 0;
	BUN r, s;
	BATiter ui = bat_iterator(u);

	BATloop(u, r, s) {
		oid updid = *(oid *) BUNhead(ui, r);
		const void *val = BUNtail(ui, r);

		if (void_inplace(b, updid, val, force) == GDK_FAIL)
			return BUN_NONE;
		nr++;
	}
	return nr;
}

/*
 * @- BUN Lookup
 * Location of a BUN using a value should use the available indexes to
 * speed up access. If indexes are lacking then a hash index is
 * constructed under the assumption that 1) multiple access to the BAT
 * can be expected and 2) building the hash is only slightly more
 * expensive than the full linear scan.  BUN_NONE is returned if no
 * such element could be found.  In those cases where the type is
 * known and a hash index is available, one should use the inline
 * functions to speed-up processing.
 */
BUN
BUNfnd(BAT *b, const void *v)
{
	BUN r = BUN_NONE;
	BATiter bi = bat_iterator(b);

	BATcheck(b, "BUNfnd");
	if (!v)
		return r;
	if (BAThvoid(b)) {
		BUNfndVOID(r, bi, v);
		return r;
	}
	if (!b->H->hash) {
		if (BAThordered(b) || BAThrevordered(b))
			return SORTfnd(b, v);
	}
	switch (ATOMstorage(b->htype)) {
	case TYPE_bte:
		HASHfnd_bte(r, bi, v);
		break;
	case TYPE_sht:
		HASHfnd_sht(r, bi, v);
		break;
	case TYPE_int:
	case TYPE_flt:
		HASHfnd_int(r, bi, v);
		break;
	case TYPE_dbl:
	case TYPE_lng:
		HASHfnd_lng(r, bi, v);
		break;
	case TYPE_str:
		HASHfnd_str(r, bi, v);
		break;
	default:
		HASHfnd(r, bi, v);
	}
	return r;
}

#define usemirror()						\
	do {							\
		int (*_cmp) (const void *, const void *);	\
		const void *_p;					\
								\
		_cmp = hcmp;					\
		hcmp = tcmp;					\
		tcmp = _cmp;					\
		_p = x;						\
		x = y;						\
		y = _p;						\
		bi.b = b = BATmirror(b);			\
	} while (0)

#define dohash(hp)        (ATOMstorage(hp->type) != TYPE_bte &&		\
			   (ATOMstorage(hp->type) != TYPE_str ||	\
			    !GDK_ELIMDOUBLES(hp->vheap)))

BUN
BUNlocate(BAT *b, const void *x, const void *y)
{
	BATiter bi = bat_iterator(b);
	int (*hcmp) (const void *, const void *);
	int (*tcmp) (const void *, const void *);
	int htpe, ttpe, hint = 0, tint = 0, hlng = 0, tlng = 0;
	union {
		var_t v;
		int i;
		lng l;
	} hidx, tidx;
	BUN p, q;
	BAT *v = NULL;

	BATcheck(b, "BUNlocate: BAT parameter required");
	BATcheck(x, "BUNlocate: value parameter required");
	hcmp = BATatoms[b->htype].atomCmp;
	tcmp = BATatoms[b->ttype].atomCmp;
	p = BUNfirst(b);
	q = BUNlast(b);
	if (p == q)
		return BUN_NONE;	/* empty bat */

	/* sometimes BUNlocate is just about a single column */
	if (y &&
	    BAThordered(b) &&
	    (*hcmp) (x, BUNhead(bi, p)) == 0 &&
	    (*hcmp) (x, BUNhead(bi, q - 1)) == 0)
		usemirror();
	if (y == NULL ||
	    (BATtordered(b) &&
	     (*tcmp) (y, BUNtail(bi, p)) == 0 &&
	     (*tcmp) (y, BUNtail(bi, q - 1)) == 0)) {
		return BUNfnd(b, x);
	}

	/* positional lookup is always the best choice */
	if (BATtdense(b))
		usemirror();
	if (BAThdense(b)) {
		BUN i = (BUN) (*(oid *) x - b->hseqbase);

		if (i < b->batCount) {
			i += BUNfirst(b);
			p = i;
			if ((*tcmp) (y, BUNtail(bi, p)) == 0)
				return p;
		}
		return BUN_NONE;
	}

	/* next, try to restrict the range using sorted columns */
	if (BATtordered(b) || BATtrevordered(b)) {
		p = SORTfndfirst(b, y);
		q = SORTfndlast(b, y);
	}
	if (BAThordered(b) || BAThrevordered(b)) {
		BUN mp = SORTfndfirst(BATmirror(b), x);
		BUN mq = SORTfndlast(BATmirror(b), x);

		if (mp > p)
			p = mp;
		if (mq < p)
			q = mq;
	}
	if (p >= q)
		return BUN_NONE;	/* value combination cannot occur */

	/* if the range is still larger than 32 BUNs, consider
	 * investing in a hash table */
	if ((q - p) > (1 << 5)) {
		/* regrettably MonetDB support only single-column hashes
		 * strategy: create a hash on both columns, and select
		 * the column with the best distribution
		 */
		if ((b->T->hash && b->H->hash == NULL) || !dohash(b->H))
			usemirror();
		if (b->H->hash == NULL && (v = VIEWcreate_(b, b, TRUE)) != NULL) {
			/* As we are going to remove the worst hash
			 * table later, we must do everything in a
			 * view, as it is not permitted to remove a
			 * hash table from a read-only operation (like
			 * BUNlocate). Other threads might then crash.
			 */
			if (dohash(v->H))
				(void) BATprepareHash(v);
			if (dohash(v->T))
				(void) BATprepareHash(BATmirror(v));
			if (v->H->hash && v->T->hash) {	/* we can choose between two hash tables */
				BUN hcnt = 0, tcnt = 0;
				BUN i;

				for (i = 0; i <= v->H->hash->mask; i++)
					hcnt += HASHget(v->H->hash,i) != HASHnil(v->H->hash);
				for (i = 0; i <= v->T->hash->mask; i++)
					tcnt += HASHget(v->T->hash,i) != HASHnil(v->T->hash);
				if (hcnt < tcnt) {
					usemirror();
					v = BATmirror(v);
				}
				/* remove the least selective hash table */
				HASHremove(BATmirror(v));
			}
			if (v->H->hash == NULL) {
				usemirror();
				v = BATmirror(v);
			}
			if (v->H->hash) {
				MT_lock_set(&GDKhashLock(ABS(b->batCacheid)), "BUNlocate");
				if (b->H->hash == NULL) {	/* give it to the parent */
					b->H->hash = v->H->hash;
				}
				MT_lock_unset(&GDKhashLock(ABS(b->batCacheid)), "BUNlocate");
			}
			BBPreclaim(v);
			v = NULL;
		}
	}

	/* exploit string double elimination, when present */
	htpe = ATOMstorage(b->htype);
	ttpe = ATOMstorage(b->ttype);
	if (htpe == TYPE_str && GDK_ELIMDOUBLES(b->H->vheap) && b->H->width > 2) {
		hidx.v = strLocate(b->H->vheap, x);
		if (hidx.v == 0)
			return BUN_NONE;	/* x does not occur */
		if (b->H->hash == NULL) {
			switch (b->H->width) {
			case SIZEOF_INT:
				hidx.i = (int) hidx.v;
				x = &hidx.i;
				htpe = TYPE_int;
				break;
			case SIZEOF_LNG:
				hidx.l = (lng) hidx.v;
				x = &hidx.l;
				htpe = TYPE_lng;
				break;
			}
		}
	}
	if (ttpe == TYPE_str && GDK_ELIMDOUBLES(b->T->vheap) && b->T->width > 2) {
		tidx.v = strLocate(b->T->vheap, y);
		if (tidx.v == 0)
			return BUN_NONE;	/* y does not occur */
		if (b->T->hash == NULL) {
			switch (b->T->width) {
			case SIZEOF_INT:
				tidx.i = (int) tidx.v;
				y = &tidx.i;
				ttpe = TYPE_int;
				break;
			case SIZEOF_LNG:
				tidx.l = (lng) tidx.v;
				y = &tidx.l;
				ttpe = TYPE_lng;
				break;
			}
		}
	}

	/* type analysis. For equi-lookup {flt,dbl,wrd,oid} can all be
	 * treated as either int or lng */
	if (!ATOMvarsized(htpe)) {
		hint = (ATOMsize(htpe) == sizeof(int));
		hlng = (ATOMsize(htpe) == sizeof(lng));
	}
	if (!ATOMvarsized(ttpe)) {
		tint = (ATOMsize(ttpe) == sizeof(int));
		tlng = (ATOMsize(ttpe) == sizeof(lng));
	}

	/* hashloop over head values, check tail values */
	if (b->H->hash) {
		BUN h;

		if (hint && tint) {
			HASHloop_int(bi, b->H->hash, h, x)
			    if (*(int *) y == *(int *) BUNtloc(bi, h))
				return h;
		} else if (hint && tlng) {
			HASHloop_int(bi, b->H->hash, h, x)
			    if (*(lng *) y == *(lng *) BUNtloc(bi, h))
				return h;
		} else if (hlng && tint) {
			HASHloop_lng(bi, b->H->hash, h, x)
			    if (*(int *) y == *(int *) BUNtloc(bi, h))
				return h;
		} else if (hlng && tlng) {
			HASHloop_lng(bi, b->H->hash, h, x)
			    if (*(lng *) y == *(lng *) BUNtloc(bi, h))
				return h;
		} else {
			HASHloop(bi, b->H->hash, h, x)
			    if ((*tcmp) (y, BUNtail(bi, h)) == 0)
				return h;
		}
		return BUN_NONE;
	}

	/* linear check; we get here for small ranges, [bte,bte] bats,
	 * and hash alloc failure */
	if (ATOMstorage(b->htype) == TYPE_bte && ATOMstorage(b->ttype) == TYPE_bte) {
		for (; p < q; p++)
			if (*(bte *) BUNhloc(bi, p) == *(bte *) x &&
			    *(bte *) BUNtloc(bi, p) == *(bte *) y)
				return p;
	} else if (hint && tint) {
		for (; p < q; p++)
			if (*(int *) BUNhloc(bi, p) == *(int *) x &&
			    *(int *) BUNtloc(bi, p) == *(int *) y)
				return p;
	} else if (hint && tlng) {
		for (; p < q; p++)
			if (*(int *) BUNhloc(bi, p) == *(int *) x &&
			    *(lng *) BUNtloc(bi, p) == *(lng *) y)
				return p;
	} else if (hlng && tint) {
		for (; p < q; p++)
			if (*(lng *) BUNhloc(bi, p) == *(lng *) x &&
			    *(int *) BUNtloc(bi, p) == *(int *) y)
				return p;
	} else if (hlng && tlng) {
		for (; p < q; p++)
			if (*(lng *) BUNhloc(bi, p) == *(lng *) x &&
			    *(lng *) BUNtloc(bi, p) == *(lng *) y)
				return p;
	} else {
		for (; p < q; p++)
			if ((*hcmp) (x, BUNhead(bi, p)) == 0 &&
			    (*tcmp) (y, BUNtail(bi, p)) == 0)
				return p;
	}
	return BUN_NONE;
}



/*
 * @+ BAT Property Management
 *
 * The function BATcount returns the number of active elements in a
 * BAT.  Counting is type independent.  It can be implemented quickly,
 * because the system ensures a dense BUN list.
 */
void
BATsetcapacity(BAT *b, BUN cnt)
{
	b->U->capacity = cnt;
	assert(b->batCount <= cnt);
}

void
BATsetcount(BAT *b, BUN cnt)
{
	b->batCount = cnt;
	b->batDirtydesc = TRUE;
	b->H->heap.free = headsize(b, BUNfirst(b) + cnt);
	b->T->heap.free = tailsize(b, BUNfirst(b) + cnt);
	if (b->H->type == TYPE_void && b->T->type == TYPE_void)
		b->batCapacity = cnt;
	assert(b->batCapacity >= cnt);
}

size_t
BATvmsize(BAT *b, int dirty)
{
	BATcheck(b, "BATvmsize");
	if (b->batDirty || (b->batPersistence != TRANSIENT && !b->batCopiedtodisk))
		dirty = 0;
	return (!dirty || b->H->heap.dirty ? HEAPvmsize(&b->H->heap) : 0) +
		(!dirty || b->T->heap.dirty ? HEAPvmsize(&b->T->heap) : 0) +
		((!dirty || b->H->heap.dirty) && b->H->hash ? HEAPvmsize(b->H->hash->heap) : 0) +
		((!dirty || b->T->heap.dirty) && b->T->hash ? HEAPvmsize(b->T->hash->heap) : 0) +
		(b->H->vheap && (!dirty || b->H->vheap->dirty) ? HEAPvmsize(b->H->vheap) : 0) +
		(b->T->vheap && (!dirty || b->T->vheap->dirty) ? HEAPvmsize(b->T->vheap) : 0);
}

size_t
BATmemsize(BAT *b, int dirty)
{
	BATcheck(b, "BATmemsize");
	if (b->batDirty ||
	    (b->batPersistence != TRANSIENT && !b->batCopiedtodisk))
		dirty = 0;
	return (!dirty || b->batDirtydesc ? sizeof(BATstore) : 0) +
		(!dirty || b->H->heap.dirty ? HEAPmemsize(&b->H->heap) : 0) +
		(!dirty || b->T->heap.dirty ? HEAPmemsize(&b->T->heap) : 0) +
		((!dirty || b->H->heap.dirty) && b->H->hash ? HEAPmemsize(b->H->hash->heap) : 0) +
		((!dirty || b->T->heap.dirty) && b->T->hash ? HEAPmemsize(b->T->hash->heap) : 0) +
		(b->H->vheap && (!dirty || b->H->vheap->dirty) ? HEAPmemsize(b->H->vheap) : 0) +
		(b->T->vheap && (!dirty || b->T->vheap->dirty) ? HEAPmemsize(b->T->vheap) : 0);
}

/*
 * The key and name properties can be changed at any time.  Keyed
 * dimensions are automatically supported by an auxiliary hash-based
 * access structure to speed up searching. Turning off the key
 * integrity property does not cause the index to disappear. It can
 * still be used to speed-up retrieval. The routine BATkey sets the
 * key property of the association head.
 */
BAT *
BATkey(BAT *b, int flag)
{
	bat parent;

	BATcheck(b, "BATkey");
	parent = VIEWparentcol(b);
	if (b->htype == TYPE_void) {
		if (b->hseqbase == oid_nil && flag == BOUND2BTRUE) {
			GDKerror("BATkey: nil-column cannot be kept unique.\n");
		}
		if (b->hseqbase != oid_nil && flag == FALSE) {
			GDKerror("BATkey: dense column must be unique.\n");
		}
		if (b->hseqbase == oid_nil && flag == TRUE && b->batCount > 1) {
			GDKerror("BATkey: void column cannot be unique.\n");
		}
	}
	if (flag)
		flag |= (1 | b->hkey);
	if (b->hkey != flag)
		b->batDirtydesc = TRUE;
	b->hkey = flag;
	if (!flag)
		b->hdense = 0;
	if (flag && parent && ALIGNsynced(b, BBP_cache(parent)))
		BATkey(BBP_cache(parent), TRUE);
	return b;
}


BAT *
BATset(BAT *b, int flag)
{
	BATcheck(b, "BATset");
	if (b->htype == TYPE_void) {
		if (b->hseqbase == oid_nil && flag == BOUND2BTRUE)
			BATkey(BATmirror(b), flag);
	} else if (b->ttype == TYPE_void) {
		if (b->tseqbase == oid_nil && flag == BOUND2BTRUE)
			BATkey(b, flag);
	} else {
		if (flag)
			flag = TRUE;
		if (b->batSet != flag)
			b->batDirtydesc = TRUE;
		b->batSet = flag;
	}
	return b;
}

BAT *
BATseqbase(BAT *b, oid o)
{
	BATcheck(b, "BATseqbase");
	assert(o <= oid_nil);
	if (ATOMtype(b->htype) == TYPE_oid) {
		if (b->hseqbase != o) {
			b->batDirtydesc = TRUE;
			/* zap alignment if column is changed by new
			 * seqbase */
			if (b->htype == TYPE_void)
				b->halign = 0;
		}
		b->hseqbase = o;

		/* adapt keyness */
		if (BAThvoid(b)) {
			if (o == oid_nil) {
				b->hkey = b->U->count <= 1;
				b->H->nonil = b->U->count == 0;
				b->H->nil = b->U->count > 0;
				b->hsorted = b->hrevsorted = 1;
			} else {
				if (!b->hkey) {
					b->hkey = TRUE;
					b->H->nokey[0] = b->H->nokey[1] = 0;
				}
				b->H->nonil = 1;
				b->H->nil = 0;
				b->hsorted = 1;
				b->hrevsorted = b->U->count <= 1;
			}
		}
	}
	return b;
}

/*
 * BATs have a logical name that is independent of their location in
 * the file system (this depends on batCacheid).  The dimensions of
 * the BAT can be given a separate name.  It helps front-ends in
 * identifying the column of interest.  The new name should be
 * recognizable as an identifier.  Otherwise interaction through the
 * front-ends becomes complicated.
 */
int
BATname(BAT *b, const char *nme)
{
	BATcheck(b, "BATname");
	return BBPrename(b->batCacheid, nme);
}

str
BATrename(BAT *b, const char *nme)
{
	int ret;

	BATcheck(b, "BATrename");
	ret = BATname(b, nme);
	if (ret == 1) {
		GDKerror("BATrename: identifier expected: %s\n", nme);
	} else if (ret == BBPRENAME_ALREADY) {
		GDKerror("BATrename: name is in use: '%s'.\n", nme);
	} else if (ret == BBPRENAME_ILLEGAL) {
		GDKerror("BATrename: illegal temporary name: '%s'\n", nme);
	} else if (ret == BBPRENAME_LONG) {
		GDKerror("BATrename: name too long: '%s'\n", nme);
	}
	return BBPname(b->batCacheid);
}


BAT *
BATroles(BAT *b, const char *hnme, const char *tnme)
{
	BATcheck(b, "BATroles");
	if (b->hident && !default_ident(b->hident))
		GDKfree(b->hident);
	if (hnme)
		b->hident = GDKstrdup(hnme);
	else
		b->hident = BATstring_h;
	if (b->tident && !default_ident(b->tident))
		GDKfree(b->tident);
	if (tnme)
		b->tident = GDKstrdup(tnme);
	else
		b->tident = BATstring_t;
	return b;
}

/*
 * @- BATmmap
 * Changing the storage status of heaps in a BAT is done in BATmmap.
 * The new semantics is to do nothing: the new mapping only takes
 * effect the next time the bat is loaded or extended. The latter is
 * needed for loading large data sets. These transient bats should
 * switch cheaply between malloced and memory mapped modes.
 *
 * We modify the hp->storage fields using HEAPnewstorage and store
 * that we want malloced or memory mapped heaps in special binary
 * batMap fields that are used when the BAT descriptor is saved.
 */
/* TODO niels: merge with BATsetmodes in gdk_storage */
#define STORE_MODE(m,r,e,s,f) (((m) == STORE_MEM)?STORE_MEM:((r)&&(e)&&!(f))||(s)==STORE_PRIV?STORE_PRIV:STORE_MMAP)
static void
HEAPnewstorage(BAT *b, int force)
{
	int existing = (BBPstatus(b->batCacheid) & BBPEXISTING);
	int brestrict = (b->batRestricted == BAT_WRITE);

	if (b->batMaphead) {
		b->H->heap.newstorage = STORE_MODE(b->batMaphead, brestrict, existing, b->H->heap.storage, force);
		if (force)
			b->H->heap.forcemap = 1;
	}
	if (b->batMaptail) {
		b->T->heap.newstorage = STORE_MODE(b->batMaptail, brestrict, existing, b->T->heap.storage, force);
		if (force)
			b->T->heap.forcemap = 1;
	}
	if (b->H->vheap && b->batMaphheap) {
		int hrestrict = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->htype, b->H->vheap);
		b->H->vheap->newstorage = STORE_MODE(b->batMaphheap, brestrict || hrestrict, existing, b->H->vheap->storage, force);
		if (force)
			b->H->vheap->forcemap = 1;
	}
	if (b->T->vheap && b->batMaptheap) {
		int trestrict = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->ttype, b->T->vheap);
		b->T->vheap->newstorage = STORE_MODE(b->batMaptheap, brestrict || trestrict, existing, b->T->vheap->storage, force);
		if (force)
			b->T->vheap->forcemap = 1;
	}
}

int
BATmmap(BAT *b, int hb, int tb, int hhp, int thp, int force)
{
	BATcheck(b, "BATmmap");
	IODEBUG THRprintf(GDKstdout, "#BATmmap(%s,%d,%d,%d,%d%s)\n", BATgetId(b), hb, tb, hhp, thp, force ? ",force" : "");

	/* Reverse back if required, as this determines which heap is
	 * saved in the "hheap" file and which in the "theap" file.
	 */
	if (b->batCacheid < 0) {
		int swap = hb;
		hb = tb;
		tb = swap;
		swap = hhp;
		hhp = thp;
		thp = swap;
		b = BATmirror(b);
	}
	b->batMaphead = hb;
	b->batMaptail = tb;
	b->batMaphheap = hhp;
	b->batMaptheap = thp;
	HEAPnewstorage(b, force);
	b->batDirtydesc = 1;
	return 0;
}

/*
 * @- BATmadvise
 * deprecated
 */
int
BATmadvise(BAT *b, int hb, int tb, int hhp, int thp)
{
	(void) b;
	(void) hb;
	(void) tb;
	(void) hhp;
	(void) thp;
	BATcheck(b, "BATmadvise");

	return 0;
}

/*
 * @- Change the BAT access permissions (read, append, write)
 * Regrettably, BAT access-permissions, persistent status and memory
 * map modes, interact in ways that makes one's brain sizzle. This
 * makes BATsetaccess and TMcommit (where a change in BAT persistence
 * mode is made permanent) points in which the memory map status of
 * bats needs to be carefully re-assessed and ensured.
 *
 * Another complication is the fact that during commit, concurrent
 * users may access the heaps, such that the simple solution
 * unmap;re-map is out of the question.
 * Even worse, it is not possible to even rename an open mmap file in
 * Windows. For this purpose, we dropped the old .priv scheme, which
 * relied on file moves. Now, the file that is opened with mmap is
 * always the X file, in case of newstorage=STORE_PRIV, we save in a
 * new file X.new
 *
 * we must consider the following dimensions:
 *
 * persistence:
 *     not simply the current persistence mode but whether the bat *was*
 *     present at the last commit point (BBP status & BBPEXISTING).
 *     The crucial issue is namely whether we must guarantee recovery
 *     to a previous sane state.
 *
 * access:
 *     whether the BAT is BAT_READ or BAT_WRITE. Note that BAT_APPEND
 *     is usually the same as BAT_READ (as our concern are only data pages
 *     that already existed at the last commit).
 *
 * storage:
 *     the current way the heap file X is memory-mapped;
 *     STORE_MMAP uses direct mapping (so dirty pages may be flushed
 *     at any time to disk), STORE_PRIV uses copy-on-write.
 *
 * newstorage:
 *     the current save-regime. STORE_MMAP calls msync() on the heap X,
 *     whereas STORE_PRIV writes the *entire* heap in a file: X.new
 *     If a BAT is loaded from disk, the field newstorage is used
 *     to set storage as well (so before change-access and commit-
 *     persistence mayhem, we always have newstorage=storage).
 *
 * change-access:
 *     what happens if the bat-access mode is changed from
 *     BAT_READ into BAT_WRITE (or vice versa).
 *
 * commit-persistence:
 *     what happens during commit if the bat-persistence mode was
 *     changed (from TRANSIENT into PERSISTENT, or vice versa).
 *
 * this is the scheme:
 *
 *  persistence access    newstorage storage    change-access commit-persistence
 *  =========== ========= ========== ========== ============= ==================
 * 0 transient  BAT_READ  STORE_MMAP STORE_MMAP =>2           =>4
 * 1 transient  BAT_READ  STORE_PRIV STORE_PRIV =>3           =>5
 * 2 transient  BAT_WRITE STORE_MMAP STORE_MMAP =>0           =>6+
 * 3 transient  BAT_WRITE STORE_PRIV STORE_PRIV =>1           =>7
 * 4 persistent BAT_READ  STORE_MMAP STORE_MMAP =>6+          =>0
 * 5 persistent BAT_READ  STORE_PRIV STORE_PRIV =>7           =>1
 * 6 persistent BAT_WRITE STORE_PRIV STORE_MMAP del X.new=>4+ del X.new;=>2+
 * 7 persistent BAT_WRITE STORE_PRIV STORE_PRIV =>5           =>3
 *
 * exception states:
 * a transient  BAT_READ  STORE_PRIV STORE_MMAP =>b           =>c
 * b transient  BAT_WRITE STORE_PRIV STORE_MMAP =>a           =>6
 * c persistent BAT_READ  STORE_PRIV STORE_MMAP =>6           =>a
 *
 * (+) indicates that we must ensure that the heap gets saved in its new mode
 *
 * Note that we now allow a heap with save-regime STORE_PRIV that was
 * actually mapped STORE_MMAP. In effect, the potential corruption of
 * the X file is compensated by writing out full X.new files that take
 * precedence.  When transitioning out of this state towards one with
 * both storage regime and OS as STORE_MMAP we need to move the X.new
 * files into the backup directory. Then msync the X file and (on
 * success) remove the X.new; see backup_new().
 *
 * Exception states are only reachable if the commit fails and those
 * new persistent bats have already been processed (but never become
 * part of a committed state). In that case a transition 2=>6 may end
 * up 2=>b.  Exception states a and c are reachable from b.
 *
 * Errors in HEAPchangeaccess() can be handled atomically inside the
 * routine.  The work on changing mmap modes HEAPcommitpersistence()
 * is done during the BBPsync() for all bats that are newly persistent
 * (BBPNEW). After the TMcommit(), it is done for those bats that are
 * no longer persistent after the commit (BBPDELETED), only if it
 * succeeds.  Such transient bats cannot be processed before the
 * commit, because the commit may fail and then the more unsafe
 * transient mmap modes would be present on a persistent bat.
 *
 * See dirty_bat() in BBPsync() -- gdk_bbp.mx and epilogue() in
 * gdk_tm.mx
 *
 * Including the exception states, we have 11 of the 16
 * combinations. As for the 5 avoided states, all four
 * (persistence,access) states with (STORE_MMAP,STORE_PRIV) are
 * omitted (this would amount to an msync() save regime on a
 * copy-on-write heap -- which does not work). The remaining avoided
 * state is the patently unsafe
 * (persistent,BAT_WRITE,STORE_MMAP,STORE_MMAP).
 *
 * Note that after a server restart exception states are gone, as on
 * BAT loads the saved descriptor is inspected again (which will
 * reproduce the state at the last succeeded commit).
 *
 * To avoid exception states, a TMsubcommit protocol would need to be
 * used which is too heavy for BATsetaccess().
 *
 * Note that this code is not about making heaps mmap-ed in the first
 * place.  It is just about determining which flavor of mmap should be
 * used. The MAL user is oblivious of such details.
 *
 * The route for making heaps mmapped in the first place (or make them
 * no longer so) is to request a mode change with BATmmap. The
 * requested modes are remembered in b->batMap*. At the next re-load
 * of the BAT, they are applied after a sanity check (DESCsetmodes()
 * in gdk_storage.mx).  @end verbatim
 */
/* rather than deleting X.new, we comply with the commit protocol and
 * move it to backup storage */
static int
backup_new(Heap *hp, int lockbat)
{
	int batret, bakret, xx, ret = 0;
	long_str batpath, bakpath;
	struct stat st;

	/* file actions here interact with the global commits */
	for (xx = 0; xx <= lockbat; xx++)
		MT_lock_set(&GDKtrimLock(xx), "TMsubcommit");

	/* check for an existing X.new in BATDIR, BAKDIR and SUBDIR */
	GDKfilepath(batpath, BATDIR, hp->filename, ".new");
	GDKfilepath(bakpath, BAKDIR, hp->filename, ".new");
	batret = stat(batpath, &st);
	bakret = stat(bakpath, &st);

	if (batret == 0 && bakret) {
		/* no backup yet, so move the existing X.new there out
		 * of the way */
		ret = rename(batpath, bakpath);
		IODEBUG THRprintf(GDKstdout, "#rename(%s,%s) = %d\n", batpath, bakpath, ret);
	} else if (batret == 0) {
		/* there is a backup already; just remove the X.new */
		ret = unlink(batpath);
		IODEBUG THRprintf(GDKstdout, "#unlink(%s) = %d\n", batpath, ret);
	}
	for (xx = lockbat; xx >= 0; xx--)
		MT_lock_unset(&GDKtrimLock(xx), "TMsubcommit");
	return ret;
}

#define ACCESSMODE(wr,rd) ((wr)?BAT_WRITE:(rd)?BAT_READ:-1)

/* transition heap from readonly to writable */
static storage_t
HEAPchangeaccess(Heap *hp, int dstmode, int existing)
{
	if (hp->base == NULL || hp->newstorage == STORE_MEM || !existing || dstmode == -1)
		return hp->newstorage;	/* 0<=>2,1<=>3,a<=>b */

	if (dstmode == BAT_WRITE) {
		if (hp->storage != STORE_PRIV)
			hp->dirty = 1;	/* exception c does not make it dirty */
		return STORE_PRIV;	/* 4=>6,5=>7,c=>6 persistent BAT_WRITE needs STORE_PRIV */
	}
	if (hp->storage == STORE_MMAP) {	/* 6=>4 */
		hp->dirty = 1;
		return backup_new(hp, BBP_THREADMASK) ? STORE_INVALID : STORE_MMAP;	/* only called for existing bats */
	}
	return hp->storage;	/* 7=>5 */
}

/* heap changes persistence mode (at commit point) */
static storage_t
HEAPcommitpersistence(Heap *hp, int writable, int existing)
{
	if (existing) {		/* existing, ie will become transient */
		if (hp->storage == STORE_MMAP && hp->newstorage == STORE_PRIV && writable) {	/* 6=>2 */
			hp->dirty = 1;
			return backup_new(hp, -1) ? STORE_INVALID : STORE_MMAP;	/* only called for existing bats */
		}
		return hp->newstorage;	/* 4=>0,5=>1,7=>3,c=>a no change */
	}
	/* !existing, ie will become persistent */
	if (hp->newstorage == STORE_MEM)
		return hp->newstorage;
	if (hp->newstorage == STORE_MMAP && !writable)
		return STORE_MMAP;	/* 0=>4 STORE_MMAP */

	if (hp->newstorage == STORE_MMAP)
		hp->dirty = 1;	/* 2=>6 */
	return STORE_PRIV;	/* 1=>5,2=>6,3=>7,a=>c,b=>6 states */
}


/* change the heap modes at a commit */
int
BATcheckmodes(BAT *b, int existing)
{
	int wr = (b->batRestricted == BAT_WRITE);
	storage_t m0 = STORE_MEM, m1 = STORE_MEM, m2 = STORE_MEM, m3 = STORE_MEM;
	int dirty = 0;

	BATcheck(b, "BATcheckmodes");

	if (b->htype) {
		m0 = HEAPcommitpersistence(&b->H->heap, wr, existing);
		dirty |= (b->H->heap.newstorage != m0);
	}

	if (b->ttype) {
		m1 = HEAPcommitpersistence(&b->T->heap, wr, existing);
		dirty |= (b->T->heap.newstorage != m1);
	}

	if (b->H->vheap) {
		int ha = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->htype, b->H->vheap);
		m2 = HEAPcommitpersistence(b->H->vheap, wr || ha, existing);
		dirty |= (b->H->vheap->newstorage != m2);
	}
	if (b->T->vheap) {
		int ta = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->ttype, b->T->vheap);
		m3 = HEAPcommitpersistence(b->T->vheap, wr || ta, existing);
		dirty |= (b->T->vheap->newstorage != m3);
	}
	if (m0 == STORE_INVALID || m1 == STORE_INVALID ||
	    m2 == STORE_INVALID || m3 == STORE_INVALID)
		return -1;

	if (dirty) {
		b->batDirtydesc = 1;
		b->H->heap.newstorage = m0;
		b->T->heap.newstorage = m1;
		if (b->H->vheap)
			b->H->vheap->newstorage = m2;
		if (b->T->vheap)
			b->T->vheap->newstorage = m3;
	}
	return 0;
}

#define heap_unshare(heap, heapname, id)				\
	do {								\
		if ((heap)->copied) {					\
			Heap hp;					\
									\
			memset(&hp, 0, sizeof(Heap));			\
			if (HEAPcopy(&hp, (heap)) < 0) {		\
				GDKerror("%s: remapped " #heapname	\
					 " of %s could not be copied.\n", \
					 fcn, BATgetId(b));		\
				return -1;				\
									\
			}						\
			hp.parentid = (id);				\
			if ((heap)->parentid == (id))			\
				HEAPfree(heap);				\
			else						\
				BBPunshare((heap)->parentid);		\
			* (heap) = hp;					\
			(heap)->copied = 0;				\
		}							\
	} while (0)

BAT *
BATsetaccess(BAT *b, int newmode)
{
	int bakmode, bakdirty;
	BATcheck(b, "BATsetaccess");
	if (isVIEW(b) && newmode != BAT_READ) {
		if (VIEWreset(b) == NULL)
			return NULL;
	}
	bakmode = b->batRestricted;
	bakdirty = b->batDirtydesc;
	if (bakmode != newmode || (b->batSharecnt && newmode != BAT_READ)) {
		int existing = BBP_status(b->batCacheid) & BBPEXISTING;
		int wr = (newmode == BAT_WRITE);
		int rd = (bakmode == BAT_WRITE);
		storage_t m0, m1, m2 = STORE_MEM, m3 = STORE_MEM;
		storage_t b0, b1, b2 = STORE_MEM, b3 = STORE_MEM;

		if (b->batSharecnt && newmode != BAT_READ) {
			BATDEBUG THRprintf(GDKout, "#BATsetaccess: %s has %d views; creating a copy\n", BATgetId(b), b->batSharecnt);
			b = BATsetaccess(BATcopy(b, b->htype, b->ttype, TRUE), newmode);
			if (b && b->batStamp > 0)
				b->batStamp = -b->batStamp;	/* prevent MIL setaccess */
			return b;
		}

		b0 = b->H->heap.newstorage;
		m0 = HEAPchangeaccess(&b->H->heap, ACCESSMODE(wr, rd), existing);
		b1 = b->T->heap.newstorage;
		m1 = HEAPchangeaccess(&b->T->heap, ACCESSMODE(wr, rd), existing);
		if (b->H->vheap) {
			int ha = (newmode == BAT_APPEND && ATOMappendpriv(b->htype, b->H->vheap));
			b2 = b->H->vheap->newstorage;
			m2 = HEAPchangeaccess(b->H->vheap, ACCESSMODE(wr && ha, rd && ha), existing);
		}
		if (b->T->vheap) {
			int ta = (newmode == BAT_APPEND && ATOMappendpriv(b->ttype, b->T->vheap));
			b3 = b->T->vheap->newstorage;
			m3 = HEAPchangeaccess(b->T->vheap, ACCESSMODE(wr && ta, rd && ta), existing);
		}
		if (m0 == STORE_INVALID || m1 == STORE_INVALID ||
		    m2 == STORE_INVALID || m3 == STORE_INVALID)
			return NULL;

		/* set new access mode and mmap modes */
		b->batRestricted = newmode;
		b->batDirtydesc = TRUE;
		b->H->heap.newstorage = m0;
		b->T->heap.newstorage = m1;
		if (b->H->vheap)
			b->H->vheap->newstorage = m2;
		if (b->T->vheap)
			b->T->vheap->newstorage = m3;

		if (existing && BBPsave(b) < 0) {
			/* roll back all changes */
			b->batRestricted = bakmode;
			b->batDirtydesc = bakdirty;
			b->H->heap.newstorage = b0;
			b->T->heap.newstorage = b1;
			if (b->H->vheap)
				b->H->vheap->newstorage = b2;
			if (b->T->vheap)
				b->T->vheap->newstorage = b3;
			return NULL;
		}
	}
	return b;
}

int
BATgetaccess(BAT *b)
{
	BATcheck(b, "BATgetaccess");
	return b->batRestricted;
}

/*
 * @- change BAT persistency (persistent,session,transient)
 * In the past, we prevented BATS with certain types from being saved at all:
 * - BATs of BATs, as having recursive bats creates cascading
 *   complexities in commits/aborts.
 * - any atom with refcounts, as the BBP has no overview of such
 *   user-defined refcounts.
 * - pointer types, as the values they point to are bound to be transient.
 *
 * However, nowadays we do allow such saves, as the BBP swapping
 * mechanism was altered to be able to save transient bats temporarily
 * to disk in order to make room.  Thus, we must be able to save any
 * transient BAT to disk.
 *
 * What we don't allow is to make such bats persistent.
 *
 * Although the persistent state does influence the allowed mmap
 * modes, this only goes for the *real* committed persistent
 * state. Making the bat persistent with BATmode does not matter for
 * the heap modes until the commit point is reached. So we do not need
 * to do anything with heap modes yet at this point.
 */
#define check_type(tp)							\
	do {								\
		if (ATOMisdescendant((tp), TYPE_ptr) ||			\
		    BATatoms[tp].atomUnfix ||				\
		    BATatoms[tp].atomFix) {				\
			GDKerror("BATmode: %s type implies that %s[%s,%s] " \
				 "cannot be made persistent.\n",	\
				 ATOMname(tp), BATgetId(b),		\
				 ATOMname(b->htype), ATOMname(b->ttype)); \
			return NULL;					\
		}							\
	} while (0)

BAT *
BATmode(BAT *b, int mode)
{
	BATcheck(b, "BATmode");

	if (mode != b->batPersistence) {
		bat bid = ABS(b->batCacheid);

		if (mode == PERSISTENT) {
			check_type(b->htype);
			check_type(b->ttype);
		}
		BBPdirty(1);

		if (mode == PERSISTENT && isVIEW(b)) {
			VIEWreset(b);
		}
		/* persistent BATs get a logical reference */
		if (mode == PERSISTENT) {
			BBPincref(bid, TRUE);
		} else if (b->batPersistence == PERSISTENT) {
			BBPdecref(bid, TRUE);
		}
		MT_lock_set(&GDKswapLock(bid), "BATmode");
		if (mode == PERSISTENT) {
			if (!(BBP_status(bid) & BBPDELETED))
				BBP_status_on(bid, BBPNEW, "BATmode");
			else
				BBP_status_on(bid, BBPEXISTING, "BATmode");
			BBP_status_off(bid, BBPDELETED, "BATmode");
		} else if (b->batPersistence == PERSISTENT) {
			if (!(BBP_status(bid) & BBPNEW))
				BBP_status_on(bid, BBPDELETED, "BATmode");
			BBP_status_off(bid, BBPPERSISTENT, "BATmode");
		}
		/* session bats or persistent bats that did not
		 * witness a commit yet may have been saved */
		if (b->batCopiedtodisk) {
			if (mode == PERSISTENT) {
				BBP_status_off(bid, BBPTMP, "BATmode");
			} else {
				/* TMcommit must remove it to
				 * guarantee free space */
				BBP_status_on(bid, BBPTMP, "BATmode");
			}
		}
		b->batPersistence = mode;
		MT_lock_unset(&GDKswapLock(bid), "BATmode");
	}
	return b;
}

/* BATassertProps checks whether properties are set correctly.  Under
 * no circumstances will it change any properties.  Note that the
 * "set" property is not checked.  Also note that the "nil" property
 * is not actually used anywhere, but it is checked. */

#ifndef NDEBUG
static void
BATassertHeadProps(BAT *b)
{
	BATiter bi = bat_iterator(b);
	BUN p, q;
	int (*cmpf)(const void *, const void *);
	int cmp;
	const void *prev = NULL, *valp, *nilp;
	int seennil = 0;

	assert(b != NULL);
	assert(b->htype >= TYPE_void);
	assert(b->htype < GDKatomcnt);
	assert(b->htype != TYPE_bat);

	cmpf = BATatoms[b->htype].atomCmp;
	nilp = ATOMnilptr(b->htype);
	p = BUNfirst(b);
	q = BUNlast(b);

	assert(b->H->heap.size <= b->H->heap.maxsize);
	if (b->htype != TYPE_void) {
		assert(b->batCount <= b->batCapacity);
		assert(b->H->heap.size >= b->H->heap.free);
		assert(b->H->heap.size >> b->H->shift >= b->batCapacity);
	}

	/* void and str imply varsized */
	if (b->htype == TYPE_void ||
	    ATOMstorage(b->htype) == TYPE_str)
		assert(b->hvarsized);
	/* other "known" types are not varsized */
	if (ATOMstorage(b->htype) > TYPE_void &&
	    ATOMstorage(b->htype) < TYPE_str)
		assert(!b->hvarsized);
	/* shift and width have a particular relationship */
	assert(b->H->shift >= 0);
	if (b->hdense)
		assert(b->htype == TYPE_oid || b->htype == TYPE_void);
	/* a column cannot both have and not have NILs */
	assert(!b->H->nil || !b->H->nonil);
	assert(b->hseqbase <= oid_nil);
	if (b->htype == TYPE_void) {
		assert(b->H->shift == 0);
		assert(b->H->width == 0);
		if (b->hseqbase == oid_nil) {
			assert(BATcount(b) == 0 || !b->H->nonil);
			assert(BATcount(b) <= 1 || !b->hkey);
			/* assert(!b->hdense); */
			assert(b->hsorted);
			assert(b->hrevsorted);
		} else {
			assert(BATcount(b) == 0 || !b->H->nil);
			assert(BATcount(b) <= 1 || !b->hrevsorted);
			/* assert(b->hdense); */
			assert(b->hkey);
			assert(b->hsorted);
		}
		return;
	}
	if (ATOMstorage(b->htype) == TYPE_str)
		assert(b->H->width >= 1 && b->H->width <= ATOMsize(b->htype));
	else
		assert(b->H->width == ATOMsize(b->htype));
	assert(1 << b->H->shift == b->H->width);
	if (b->htype == TYPE_oid && b->hdense) {
		assert(b->hsorted);
		assert(b->hseqbase != oid_nil);
		if (b->batCount > 0) {
			assert(b->hseqbase != oid_nil);
			assert(* (oid *) BUNhead(bi, p) == b->hseqbase);
		}
	}
	/* only linear atoms can be sorted */
	assert(!b->hsorted || BATatoms[b->htype].linear);
	assert(!b->hrevsorted || BATatoms[b->htype].linear);

	if (!b->hkey && !b->hsorted && !b->hrevsorted &&
	    !b->H->nonil && !b->H->nil) {
		/* nothing more to prove */
		return;
	}

	PROPDEBUG { /* only do a scan if property checking is requested */
		if (b->hsorted || b->hrevsorted || !b->hkey) {
			/* if sorted (either way), or we don't have to
			 * prove uniqueness, we can do a simple
			 * scan */
			/* only call compare function if we have to */
			int cmpprv = b->hsorted | b->hrevsorted | b->hkey;
			int cmpnil = b->H->nonil | b->H->nil;

			BATloop(b, p, q) {
				valp = BUNhead(bi, p);
				if (prev && cmpprv) {
					cmp = cmpf(prev, valp);
					assert(!b->hsorted || cmp <= 0);
					assert(!b->hrevsorted || cmp >= 0);
					assert(!b->hkey || cmp != 0);
					assert(!b->hdense || * (oid *) prev + 1 == * (oid *) valp);
				}
				if (cmpnil) {
					cmp = cmpf(valp, nilp);
					assert(!b->H->nonil || cmp != 0);
					if (cmp == 0) {
						/* we found a nil:
						 * we're done checking
						 * for them */
						seennil = 1;
						cmpnil = 0;
						if (!cmpprv) {
							/* we were
							 * only
							 * checking
							 * for nils,
							 * so nothing
							 * more to
							 * do */
							break;
						}
					}
				}
				prev = valp;
			}
		} else {	/* b->hkey && !b->hsorted && !b->hrevsorted */
			/* we need to check for uniqueness the hard
			 * way (i.e. using a hash table) */
			const char *nme = BBP_physical(b->batCacheid);
			char *ext;
			size_t nmelen = strlen(nme);
			Heap *hp;
			Hash *hs;

			if ((hp = GDKzalloc(sizeof(Heap))) == NULL ||
			    (hp->filename = GDKmalloc(nmelen + 30)) == NULL) {
				if (hp)
					GDKfree(hp);
				THRprintf(GDKstdout,
					  "#BATassertProps: cannot allocate "
					  "hash table\n");
				goto abort_check;
			}
			snprintf(hp->filename, nmelen + 30,
				 "%s.hash" SZFMT, nme, MT_getpid());
			ext = GDKstrdup(hp->filename + nmelen + 1);
			if ((hs = HASHnew(hp, b->htype, BUNlast(b),
					  HASHmask(b->batCount))) == NULL) {
				GDKfree(ext);
				GDKfree(hp->filename);
				GDKfree(hp);
				THRprintf(GDKstdout,
					  "#BATassertProps: cannot allocate "
					  "hash table\n");
				goto abort_check;
			}
			BATloop(b, p, q) {
				BUN hb;
				BUN prb;
				valp = BUNhead(bi, p);
				prb = HASHprobe(hs, valp);
				for (hb = HASHget(hs,prb);
				     hb != HASHnil(hs);
				     hb = HASHgetlink(hs,hb))
					if (cmpf(valp, BUNhead(bi, hb)) == 0)
						assert(!b->hkey);
				HASHputlink(hs,p, HASHget(hs,prb));
				HASHput(hs,prb,p);
				cmp = cmpf(valp, nilp);
				assert(!b->H->nonil || cmp != 0);
				if (cmp == 0)
					seennil = 1;
			}
			if (hp->storage == STORE_MEM)
				HEAPfree(hp);
			else
				HEAPdelete(hp, nme, ext);
			GDKfree(hp);
			GDKfree(hs);
			GDKfree(ext);
		}
	  abort_check:
		assert(!b->H->nil || seennil);
	}
}
#endif

/* Assert that properties are set correctly.
 *
 * A BAT can have a bunch of properties set.  Mostly, the property
 * bits are set if we *know* the property holds, and not set if we
 * don't know whether the property holds (or if we know it doesn't
 * hold).  Most properties are per column, only the "set" property is
 * over two columns.
 *
 * The properties currently maintained are:
 *
 * dense	Only valid for TYPE_oid columns: each value in the
 *		column is exactly one more than the previous value.
 *		This implies sorted, key, nonil.
 * nil		There is at least one NIL value in the column.
 * nonil	There are no NIL values in the column.
 * key		All values in the column are distinct.
 * sorted	The column is sorted (ascending).  If also revsorted,
 *		then all values are equal.
 * revsorted	The column is reversely sorted (descending).  If
 *		also sorted, then all values are equal.
 * set		The combinations of head and tail values are distinct.
 *
 * The "key" property consists of two bits.  The lower bit, when set,
 * indicates that all values in the column are distinct.  The upper
 * bit, when set, indicates that all values must be distinct
 * (BOUND2BTRUE).
 *
 * Note also that the "set" property is somewhat confused.  On the one
 * hand, some comments suggest it is merely an indication of the
 * current state of affairs, i.e. all head/tail combinations are
 * distinct.  The code in BUNins suggests that it means that the
 * combinations must be distinct.
 *
 * Note that the functions BATseqbase and BATkey also set more
 * properties than you might suspect.  When setting properties on a
 * newly created and filled BAT, you may want to first make sure the
 * batCount is set correctly (e.g. by calling BATsetcount), then use
 * BATseqbase and BATkey, and finally set the other properties.
 */

void
BATassertProps(BAT *b)
{
#ifndef NDEBUG
	BAT *bm;
	int bbpstatus;

	/* general BAT sanity */
	assert(b != NULL);
	bm = BATmirror(b);
	assert(bm != NULL);
	assert(b->H == bm->T);
	assert(b->T == bm->H);
	assert(b->U == bm->U);
	assert(b->P == bm->P);
	assert(b->batDeleted < BUN_MAX);
	assert(b->batFirst >= b->batDeleted);
	assert(b->batInserted >= b->batFirst);
	assert(b->batFirst + b->batCount >= b->batInserted);
	assert(b->U->first == 0);
	bbpstatus = BBP_status(b->batCacheid);
	/* only at most one of BBPDELETED, BBPEXISTING, BBPNEW may be set */
	assert(((bbpstatus & BBPDELETED) != 0) +
	       ((bbpstatus & BBPEXISTING) != 0) +
	       ((bbpstatus & BBPNEW) != 0) <= 1);

	BATassertHeadProps(b);
	if (b->H != bm->H)
		BATassertHeadProps(bm);
#else
	(void) b;
#endif
}

/* derive properties that can be derived with a simple scan: sorted,
 * revsorted, dense; if expensive is set, we also check the key
 * property
 * note that we don't check nil/nonil: we usually know pretty quickly
 * that a column is not sorted, but we usually need a full scan for
 * nonil.
 */
void
BATderiveHeadProps(BAT *b, int expensive)
{
	BATiter bi = bat_iterator(b);
	BUN p, q;
	int (*cmpf)(const void *, const void *);
	int cmp;
	const void *prev = NULL, *valp, *nilp;
	int sorted, revsorted, key, dense;
	const char *nme = NULL;
	char *ext = NULL;
	size_t nmelen;
	Heap *hp = NULL;
	Hash *hs = NULL;
	BUN hb, prb;
	oid sqbs = oid_nil;

	assert(b != NULL);
	if (b == NULL)
		return;
	assert((b->hkey & BOUND2BTRUE) == 0);
	COLsettrivprop(b, b->H);
	cmpf = BATatoms[b->htype].atomCmp;
	nilp = ATOMnilptr(b->htype);
	b->batDirtydesc = 1;	/* we will be changing things */
	if (b->htype == TYPE_void || b->batCount <= 1) {
		/* COLsettrivprop has already taken care of all
		 * properties except for (no)nil if count == 1 */
		if (b->batCount == 1) {
			valp = BUNhead(bi, BUNfirst(b));
			if (cmpf(valp, nilp) == 0) {
				b->H->nil = 1;
				b->H->nonil = 0;
			} else {
				b->H->nil = 0;
				b->H->nonil = 1;
			}
		}
		return;
	}
	/* tentatively set until proven otherwise */
	key = 1;
	sorted = revsorted = (BATatoms[b->htype].linear != 0);
	dense = (b->htype == TYPE_oid);
	/* if no* props already set correctly, we can maybe speed
	 * things up, if not set correctly, reset them now and set
	 * them later */
	if (!b->hkey &&
	    b->H->nokey[0] >= b->batFirst &&
	    b->H->nokey[0] < b->batFirst + b->batCount &&
	    b->H->nokey[1] >= b->batFirst &&
	    b->H->nokey[1] < b->batFirst + b->batCount &&
	    b->H->nokey[0] != b->H->nokey[1] &&
	    cmpf(BUNhead(bi, b->H->nokey[0]),
		 BUNhead(bi, b->H->nokey[1])) == 0) {
		/* we found proof that the column doesn't deserve the
		 * key property, no need to check the hard way */
		expensive = 0;
		key = 0;
	} else {
		b->H->nokey[0] = 0;
		b->H->nokey[1] = 0;
	}
	if (!b->hsorted &&
	    b->H->nosorted > b->batFirst &&
	    b->H->nosorted < b->batFirst + b->batCount &&
	    cmpf(BUNhead(bi, b->H->nosorted - 1),
		 BUNhead(bi, b->H->nosorted)) > 0) {
		sorted = 0;
		dense = 0;
	} else {
		b->H->nosorted = 0;
	}
	if (!b->hrevsorted &&
	    b->H->norevsorted > b->batFirst &&
	    b->H->norevsorted < b->batFirst + b->batCount &&
	    cmpf(BUNhead(bi, b->H->norevsorted - 1),
		 BUNhead(bi, b->H->norevsorted)) < 0) {
		revsorted = 0;
	} else {
		b->H->norevsorted = 0;
	}
	if (dense &&
	    !b->hdense &&
	    b->H->nodense >= b->batFirst &&
	    b->H->nodense < b->batFirst + b->batCount &&
	    (b->H->nodense == b->batFirst ?
	     * (oid *) BUNhead(bi, b->H->nodense) == oid_nil :
	     * (oid *) BUNhead(bi, b->H->nodense - 1) + 1 != * (oid *) BUNhead(bi, b->H->nodense))) {
		dense = 0;
	} else {
		b->H->nodense = 0;
	}
	if (expensive) {
		nme = BBP_physical(b->batCacheid);
		nmelen = strlen(nme);
		if ((hp = GDKzalloc(sizeof(Heap))) == NULL ||
		    (hp->filename = GDKmalloc(nmelen + 30)) == NULL ||
		    snprintf(hp->filename, nmelen + 30,
			     "%s.hash" SZFMT, nme, MT_getpid()) < 0 ||
		    (ext = GDKstrdup(hp->filename + nmelen + 1)) == NULL ||
		    (hs = HASHnew(hp, b->htype, BUNlast(b),
				  HASHmask(b->batCount))) == NULL) {
			if (hp) {
				if (hp->filename)
					GDKfree(hp->filename);
				GDKfree(hp);
			}
			if (ext)
				GDKfree(ext);
			hp = NULL;
			ext = NULL;
			THRprintf(GDKstdout,
				  "#BATderiveProps: cannot allocate "
				  "hash table: not doing full check\n");
		}
	}
	for (q = BUNlast(b), p = BUNfirst(b);
	     p < q && (sorted || revsorted || (key && hs));
	     p++) {
		valp = BUNhead(bi, p);
		if (prev) {
			cmp = cmpf(prev, valp);
			if (cmp < 0) {
				revsorted = 0;
				if (b->H->norevsorted == 0)
					b->H->norevsorted = p;
				if (dense &&
				    * (oid *) prev + 1 != * (oid *) valp) {
					dense = 0;
					if (b->H->nodense == 0)
						b->H->nodense = p;
				}
			} else {
				if (cmp > 0) {
					sorted = 0;
					if (b->H->nosorted == 0)
						b->H->nosorted = p;
				} else {
					key = 0;
					if (b->H->nokey[0] == 0 &&
					    b->H->nokey[1] == 0) {
						b->H->nokey[0] = p - 1;
						b->H->nokey[1] = p;
					}
				}
				if (dense) {
					dense = 0;
					if (b->H->nodense == 0)
						b->H->nodense = p;
				}
			}
		} else if (dense && (sqbs = * (oid *) valp) == oid_nil) {
			dense = 0;
			b->H->nodense = p;
		}
		prev = valp;
		if (key && hs) {
			prb = HASHprobe(hs, valp);
			for (hb = HASHget(hs,prb);
			     hb != HASHnil(hs);
			     hb = HASHgetlink(hs,hb)) {
				if (cmpf(valp, BUNhead(bi, hb)) == 0) {
					key = 0;
					b->H->nokey[0] = hb;
					b->H->nokey[1] = p;
					break;
				}
			}
			HASHputlink(hs,p, HASHget(hs,prb));
			HASHput(hs,prb,p);
		}
	}
	if (hs) {
		if (hp->storage == STORE_MEM)
			HEAPfree(hp);
		else
			HEAPdelete(hp, nme, ext);
		GDKfree(hp);
		GDKfree(hs);
		GDKfree(ext);
	}
	b->hsorted = sorted;
	b->hrevsorted = revsorted;
	b->hdense = dense;
	if (dense)
		b->hseqbase = sqbs;
	if (hs) {
		b->hkey = key;
	} else {
		/* we can only say something about keyness if the
		 * column is sorted */
		b->hkey = key & (sorted | revsorted);
	}
	if (sorted || revsorted) {
		/* if sorted, we only need to check the extremes to
		 * know whether there are any nils */
		if (cmpf(BUNhead(bi, BUNfirst(b)), nilp) != 0 &&
		    cmpf(BUNhead(bi, BUNlast(b) - 1), nilp) != 0) {
			b->H->nonil = 1;
			b->H->nil = 0;
		} else {
			b->H->nonil = 0;
			b->H->nil = 1;
		}
	}
#ifndef NDEBUG
	BATassertHeadProps(b);
#endif
}

void
BATderiveProps(BAT *b, int expensive)
{
	assert(b != NULL);

	if (b == NULL)
		return;
	BATderiveHeadProps(b, expensive);
	if (b->H != b->T)
		BATderiveHeadProps(BATmirror(b), expensive);
}
