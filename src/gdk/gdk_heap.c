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
 * @f gdk_heap
 * @a Peter Boncz, Wilko Quak
 * @+ Atom Heaps
 * Heaps are the basic mass storage structure of Monet. A heap is a
 * handle to a large, possibly huge, contiguous area of main memory,
 * that can be allocated in various ways (discriminated by the
 * heap->storage field):
 *
 * @table @code
 * @item STORE_MEM: malloc-ed memory
 * small (or rather: not huge) heaps are allocated with GDKmalloc.
 * Notice that GDKmalloc may redirect big requests to anonymous
 * virtual memory to prevent @emph{memory fragmentation} in the malloc
 * library (see gdk_utils.mx).
 *
 * @item STORE_MMAP: read-only mapped region
 * this is a file on disk that is mapped into virtual memory.  This is
 * normally done MAP_SHARED, so we can use msync() to commit dirty
 * data using the OS virtual memory management.
 *
 * @item STORE_PRIV: read-write mapped region
 * in order to preserve ACID properties, we use a different memory
 * mapping on virtual memory that is writable. This is because in case
 * of a crash on a dirty STORE_MMAP heap, the OS may have written some
 * of the dirty pages to disk and other not (but it is impossible to
 * determine which).  The OS MAP_PRIVATE mode does not modify the file
 * on which is being mapped, rather creates substitute pages
 * dynamically taken from the swap file when modifications occur. This
 * is the only way to make writing to mmap()-ed regions safe.  To save
 * changes, we created a new file X.new; as some OS-es do not allow to
 * write into a file that has a mmap open on it (e.g. Windows).  Such
 * X.new files take preference over X files when opening them.
 * @end table
 * Read also the discussion in BATsetaccess (gdk_bat.mx).
 *
 * Todo: check DESCsetmodes/HEAPcheckmode (gdk_storage.mx).
 */
#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"

#include <Rinternals.h>
#ifndef RHS
#define RHS Rf_sizeofHeader()
#endif

/* The heap cache should reduce mmap/munmap calls which are very
 * expensive.  Instead we try to reuse mmap's. This however requires
 * file renames.  The cache has a limited size!
 */

#define HEAP_CACHE_SIZE 5

typedef struct heap_cache_e {
	void *base;
	size_t maxsz;
	char fn[PATHLENGTH];	/* tmp file name */
} heap_cache_e;

typedef struct heap_cache {
	int sz;
	int used;
	heap_cache_e *hc;
} heap_cache;

static heap_cache *hc = NULL;
static MT_Lock HEAPcacheLock MT_LOCK_INITIALIZER("HEAPcacheLock");

void
HEAPcacheInit(void)
{
#if HEAP_CACHE_SIZE > 0
	if (!hc) {
		int i;

#ifdef NEED_MT_LOCK_INIT
		MT_lock_init(&HEAPcacheLock, "HEAPcache_init");
#endif
		MT_lock_set(&HEAPcacheLock, "HEAPcache_init");
		hc = (heap_cache *) GDKmalloc(sizeof(heap_cache));
		hc->used = 0;
		hc->sz = HEAP_CACHE_SIZE;
		hc->hc = (heap_cache_e *) GDKmalloc(sizeof(heap_cache_e) * hc->sz);
		GDKcreatedir(HCDIR DIR_SEP_STR);
		/* clean old leftovers */
		for (i = 0; i < HEAP_CACHE_SIZE; i++) {
			char fn[PATHLENGTH];

			snprintf(fn, PATHLENGTH, "%d", i);
			GDKunlink(HCDIR, fn, NULL);
		}
		MT_lock_unset(&HEAPcacheLock, "HEAPcache_init");
	}
#endif
}

static int
HEAPcacheAdd(void *base, size_t maxsz, char *fn, storage_t storage, int free_file)
{
	int added = 0;


	MT_lock_set(&HEAPcacheLock, "HEAPcache_init");
	if (hc && free_file && fn && storage == STORE_MMAP && hc->used < hc->sz) {
		heap_cache_e *e = hc->hc + hc->used;

		e->base = base;
		e->maxsz = maxsz;
		snprintf(e->fn, PATHLENGTH, "%d", hc->used);
		GDKunlink(HCDIR, e->fn, NULL);
		added = 1;
		if (GDKmove(BATDIR, fn, NULL, HCDIR, e->fn, NULL) < 0) {
			/* try to create the directory, if that was
			 * the problem */
			char path[PATHLENGTH];

			GDKfilepath(path, HCDIR, e->fn, NULL);
			GDKcreatedir(path);
			if (GDKmove(BATDIR, fn, NULL, HCDIR, e->fn, NULL) < 0)
				added = 0;
		}
		if (added)
			hc->used++;
	}
	MT_lock_unset(&HEAPcacheLock, "HEAPcache_init");
	if (!added)
		return GDKmunmap(base, maxsz);
	HEAPDEBUG fprintf(stderr, "#HEAPcacheAdd (%s) " SZFMT " " PTRFMT " %d %d %d\n", fn, maxsz, PTRFMTCAST base, (int) storage, free_file, hc->used);
	return 0;
}

static void *
HEAPcacheFind(size_t *maxsz, char *fn, storage_t mode)
{
	void *base = NULL;

	*maxsz = (1 + (*maxsz >> 16)) << 16;	/* round up to 64K */
	MT_lock_set(&HEAPcacheLock, "HEAPcache_init");
	if (hc && mode == STORE_MMAP && hc->used < hc->sz) {
		HEAPDEBUG fprintf(stderr, "#HEAPcacheFind (%s)" SZFMT " %d %d\n", fn, *maxsz, (int) mode, hc->used);

		if (hc->used) {
			int i;
			heap_cache_e *e = NULL;
			size_t cursz = 0;

			/* find best match: prefer smallest larger
			 * than or equal to requested, otherwise
			 * largest smaller than requested */
			for (i = 0; i < hc->used; i++) {
				if ((hc->hc[i].maxsz >= *maxsz &&
				     (e == NULL || hc->hc[i].maxsz < cursz)) ||
				    (hc->hc[i].maxsz < *maxsz &&
				     cursz < *maxsz &&
				     hc->hc[i].maxsz > cursz)) {
					e = hc->hc + i;
					cursz = e->maxsz;
				}
			}
			if (e != NULL && e->maxsz < *maxsz) {
				/* resize file ? */
				long_str fn;

				GDKfilepath(fn, HCDIR, e->fn, NULL);
				if (GDKextend(fn, *maxsz) == 0) {
					void *base = GDKload(fn, NULL, *maxsz, *maxsz, STORE_MMAP);
					GDKmunmap(e->base, e->maxsz);
					e->base = base;
					e->maxsz = *maxsz;
				} else {
					/* extending may have
					 * failed */
					e = NULL;
				}
			}
			if (e != NULL) {
				/* move cached heap to its new location */
				base = e->base;
				*maxsz = e->maxsz;
				if (GDKmove(HCDIR, e->fn, NULL, BATDIR, fn, NULL) < 0) {
					/* try to create the directory, if
					 * that was the problem */
					char path[PATHLENGTH];

					GDKfilepath(path, BATDIR, fn, NULL);
					GDKcreatedir(path);
					if (GDKmove(HCDIR, e->fn, NULL, BATDIR, fn, NULL) < 0)
						e = NULL;
				}
			}
			if (e != NULL) {
				hc->used--;
				i = (int) (e - hc->hc);
				if (i < hc->used) {
					e->base = hc->hc[hc->used].base;
					e->maxsz = hc->hc[hc->used].maxsz;
					GDKmove(HCDIR, hc->hc[hc->used].fn, NULL, HCDIR, e->fn, NULL);
				}
			}
		}
	}
	MT_lock_unset(&HEAPcacheLock, "HEAPcache_init");
	if (!base) {
		int fd = GDKfdlocate(fn, "wb", NULL);

		if (fd >= 0) {
			close(fd);
			return GDKload(fn, NULL, *maxsz, *maxsz, mode);
		}
	} else
		HEAPDEBUG fprintf(stderr, "#HEAPcacheFind (%s) re-used\n", fn);
	return base;
}

static int HEAPload_intern(Heap *h, const char *nme, const char *ext, const char *suffix, int trunc);
static int HEAPsave_intern(Heap *h, const char *nme, const char *ext, const char *suffix);

static char *
decompose_filename(str nme)
{
	char *ext;

	ext = strchr(nme, '.');	/* extract base and ext from heap file name */
	if (ext) {
		*ext++ = 0;
	}
	return ext;
}

/*
 * @- HEAPalloc
 *
 * Normally, we use GDKmalloc for creating a new heap.  Huge heaps,
 * though, come from memory mapped files that we create with a large
 * seek. This is fast, and leads to files-with-holes on Unixes (on
 * Windows, it actually always performs I/O which is not nice).
 */
static size_t
HEAPmargin(size_t maxsize)
{
	size_t ret;
#if SIZEOF_VOID_P == 8
	/* in 64-bits systems, try to enforce in-place realloc, but
	 * provoke the memcpy on 256MB, then 4GB */
	size_t use = GDKvm_cursize();
	ret = MIN(GDK_mem_maxsize, MAX(((size_t) 1) << 26, 16 * maxsize));
	if ((ret + ret) > (GDK_vm_maxsize - MIN(GDK_vm_maxsize, use)))	/* only if room */
#endif
		ret = ((size_t) (((double) BATMARGIN) * (double) maxsize)) - 1;	/* do not waste VM on 32-bits */
	HEAPDEBUG fprintf(stderr, "#HEAPmargin " SZFMT " -> " SZFMT "\n",
			  maxsize, (1 + (MAX(maxsize, ret) >> 16)) << 16);
	return (1 + (MAX(maxsize, ret) >> 16)) << 16;	/* round up to 64K */
}

/* in 64-bits space, use very large margins to accommodate reallocations */
int
HEAPalloc(Heap *h, size_t nitems, size_t itemsize)
{
	char nme[PATHLENGTH];
	size_t minsize = GDK_mmap_minsize;
	struct stat st;

	h->base = NULL;
	h->maxsize = h->size = 1;
	h->copied = 0;
	if (itemsize)
		h->maxsize = h->size = MAX(1, nitems) * itemsize;
	h->free = 0;

	/* check for overflow */
	if (itemsize && nitems > (h->size / itemsize))
		return -1;

	if (h->filename) {
		GDKfilepath(nme, BATDIR, h->filename, NULL);
		/* if we're going to use mmap anyway (size >=
		 * GDK_mem_bigsize -- see GDKmallocmax), and the file
		 * we want to use already exists and is large enough
		 * for the size we want, force non-anonymous mmap */
		if (h->size >= GDK_mem_bigsize &&
		    stat(nme, &st) == 0 && st.st_size >= (off_t) h->size) {
			minsize = GDK_mem_bigsize; /* force mmap */
		}
	}

	if (h->filename == NULL || (h->size < minsize)) {
		h->storage = STORE_MEM;
// R fix
		h->size += RHS; h->maxsize += RHS;
		h->base = (char *) GDKmallocmax(h->size, &h->maxsize, 0);
		h->size -= RHS; h->maxsize -= RHS;
		if (h->base) h->base += RHS;
		HEAPDEBUG fprintf(stderr, "#HEAPalloc " SZFMT " " SZFMT " " PTRFMT "\n", h->size, h->maxsize, PTRFMTCAST h->base);
	}
	if (h->filename && h->base == NULL) {
		char *of = h->filename;

		h->filename = NULL;

		if (stat(nme, &st) != 0) {
			h->storage = STORE_MMAP;
			h->base = HEAPcacheFind(&h->maxsize, of, h->storage);
			h->filename = of;
		} else {
			char *ext;
			int fd;

			strncpy(nme, of, sizeof(nme));
			nme[sizeof(nme) - 1] = 0;
			ext = decompose_filename(nme);
			fd = GDKfdlocate(nme, "wb", ext);
			if (fd >= 0) {
				close(fd);
				h->newstorage = STORE_MMAP;
				HEAPload(h, nme, ext, FALSE);
			}
			GDKfree(of);
		}
	}
	if (h->base == NULL) {
		GDKerror("HEAPalloc: Insufficient space for HEAP of " SZFMT " bytes.", h->size);
		return -1;
	}
	h->newstorage = h->storage;
	return 0;
}

/*
 * @- HEAPextend
 *
 * Normally (last case in the below code), we use GDKrealloc, except
 * for the case that the heap extends to a huge size, in which case we
 * open memory mapped file.
 *
 * Observe that we may assume that the BAT is writable here
 * (otherwise, why extend?).
 *
 * For memory mapped files, we may try to extend the file after the
 * end, and also extend the VM space we already have. This may fail,
 * e.g. due to VM fragmentation or no swap space (we map the new
 * segment STORE_PRIV). Also, some OS-es might not support this at all
 * (NOEXTEND_PRIVMAP).
 *
 * The other way is to just save the mmap-ed heap, free it and reload
 * it.
 */
int
HEAPextend(Heap *h, size_t size)
{
	char nme[PATHLENGTH], *ext = NULL;

	if (h->filename) {
		strncpy(nme, h->filename, sizeof(nme));
		nme[sizeof(nme) - 1] = 0;
		ext = decompose_filename(nme);
	}
	if (size <= h->size)
		return 0;

	if (h->storage != STORE_MEM) {
		HEAPDEBUG fprintf(stderr, "#HEAPextend: extending %s mmapped heap\n", h->storage == STORE_MMAP ? "shared" : "privately");
		/* memory mapped files extend: save and remap */
		if (HEAPsave_intern(h, nme, ext, ".tmp") < 0)
			return -1;
		HEAPfree(h);
		h->maxsize = h->size = size;
		if (HEAPload_intern(h, nme, ext, ".tmp", FALSE) >= 0) {
			return 0;
		}
	} else {
		/* extend a malloced heap, possibly switching over to
		 * file-mapped storage */
		Heap bak = *h;
		size_t cur = GDKmem_inuse(), tot = GDK_mem_maxsize;
		int exceeds_swap = size > (tot + tot - MIN(tot + tot, cur));
		int can_mmap = h->filename && (size >= GDK_mem_bigsize || h->newstorage != STORE_MEM);
		int small_cpy = (h->size * 4 < size) && (size >= GDK_mmap_minsize);
		/* the last condition is to use explicit MMAP instead
		 * of anonymous MMAP in GDKmalloc */
		int must_mmap = can_mmap && (small_cpy || exceeds_swap || h->newstorage != STORE_MEM || size >= GDK_mem_bigsize);

		h->size = size;

		if (can_mmap) {
			/* in anonymous vm, if have to realloc anyway,
			 * we reserve some extra space */
			h->maxsize = HEAPmargin(MAX(size, h->maxsize));
		} else {
			h->maxsize = size;	/* for normal GDKmalloc, maxsize = size */
		}

		/* try GDKrealloc if the heap size stays within
		 * reasonable limits */
		if (!must_mmap) {
			// R fix
			void *p = h->base;
			HEAPDEBUG fprintf(stderr, "#HEAPextend: try extending malloced heap " SZFMT " " SZFMT " " PTRFMT "\n", size, h->maxsize, PTRFMTCAST p);
			h->newstorage = h->storage = STORE_MEM;
			h->base -= RHS; size += RHS; h->maxsize += RHS;
			h->base = (char *) GDKreallocmax(h->base, size, &h->maxsize, 0);
			size -= RHS; h->maxsize -= RHS;
			if (h->base) h->base += RHS;
			HEAPDEBUG fprintf(stderr, "#HEAPextend: extending malloced heap " SZFMT " " SZFMT " " PTRFMT " " PTRFMT "\n", size, h->maxsize, PTRFMTCAST p, PTRFMTCAST h->base);
			if (h->base)
				return 0;
		}
		/* too big: convert it to a disk-based temporary heap */
		if (can_mmap) {
			int fd;
			char *of = h->filename;
			int existing = 0;

			/* if the heap file already exists, we want to
			 * switch to STORE_PRIV (copy-on-write memory
			 * mapped files), but if the heap file doesn't
			 * exist yet, the BAT is new and we can use
			 * STORE_MMAP */
			fd = GDKfdlocate(nme, "rb", ext);
			if (fd >= 0) {
				existing = 1;
				close(fd);
			}
			h->filename = NULL;
			fd = GDKfdlocate(nme, "wb", ext);
			if (fd >= 0) {
				close(fd);
				if (h->storage == STORE_MEM) {
					storage_t newmode = h->newstorage == STORE_MMAP && existing && !h->forcemap ? STORE_PRIV : h->newstorage;
					/* make sure we really MMAP */
					if (must_mmap && h->newstorage == STORE_MEM)
						newmode = STORE_MMAP;
					h->newstorage = h->storage = newmode;
					h->forcemap = 0;
				}
				h->base = NULL;
				HEAPDEBUG fprintf(stderr, "#HEAPextend: converting malloced to %s mmapped heap\n", h->newstorage == STORE_MMAP ? "shared" : "privately");
				/* try to allocate a memory-mapped
				 * based heap */
				if (HEAPload(h, nme, ext, FALSE) >= 0) {
					/* copy data to heap and free
					 * old memory */
					memcpy(h->base, bak.base, bak.free);
					HEAPfree(&bak);
					return 0;
				}
				/* couldn't allocate, now first save
				 * data to file */
				if (HEAPsave_intern(&bak, nme, ext, ".tmp") < 0) {
					*h = bak;
					return -1;
				}
				/* then free memory */
				HEAPfree(&bak);
				of = NULL;	/* file name is freed by HEAPfree */
				/* and load heap back in via
				 * memory-mapped file */
				if (HEAPload_intern(h, nme, ext, ".tmp", FALSE) >= 0) {
					/* success! */
					GDKclrerr();	/* don't leak errors from e.g. HEAPload */
					return 0;
				}
				/* we failed */
			}
			if (of)
				GDKfree(of);
		}
		*h = bak;
	}
	GDKerror("HEAPextend: failed to extend to " SZFMT " for %s%s%s\n",
		 size, nme, ext ? "." : "", ext ? ext : "");
	return -1;
}

int
GDKupgradevarheap(COLrec *c, var_t v, int copyall)
{
	bte shift = c->shift;
	unsigned short width = c->width;
	unsigned char *pc;
	unsigned short *ps;
	unsigned int *pi;
#if SIZEOF_VAR_T == 8
	var_t *pv;
#endif
	size_t i, n;
	size_t savefree;

	assert(c->heap.parentid == 0);
	assert(width != 0);
	assert(v >= GDK_VAROFFSET);
	assert(width < SIZEOF_VAR_T && (width <= 2 ? v - GDK_VAROFFSET : v) >= ((var_t) 1 << (8 * width)));
	while (width < SIZEOF_VAR_T && (width <= 2 ? v - GDK_VAROFFSET : v) >= ((var_t) 1 << (8 * width))) {
		width <<= 1;
		shift++;
	}
	assert(c->width < width);
	assert(c->shift < shift);
	/* if copyall is set, we need to convert the whole heap, since
	 * we may be in the middle of an insert loop that adjusts the
	 * free value at the end; otherwise only copy the area
	 * indicated by the "free" pointer */
	n = (copyall ? c->heap.size : c->heap.free) >> c->shift;
	savefree = c->heap.free;
	if (copyall)
		c->heap.free = c->heap.size;
	if (HEAPextend(&c->heap, (c->heap.size >> c->shift) << shift) < 0)
		return GDK_FAIL;
	if (copyall)
		c->heap.free = savefree;
	/* note, cast binds more closely than addition */
	pc = (unsigned char *) c->heap.base + n;
	ps = (unsigned short *) c->heap.base + n;
	pi = (unsigned int *) c->heap.base + n;
#if SIZEOF_VAR_T == 8
	pv = (var_t *) c->heap.base + n;
#endif

	/* convert from back to front so that we can do it in-place */
	switch (c->width) {
	case 1:
		switch (width) {
		case 2:
			for (i = 0; i < n; i++)
				*--ps = *--pc;
			break;
		case 4:
			for (i = 0; i < n; i++)
				*--pi = *--pc + GDK_VAROFFSET;
			break;
#if SIZEOF_VAR_T == 8
		case 8:
			for (i = 0; i < n; i++)
				*--pv = *--pc + GDK_VAROFFSET;
			break;
#endif
		}
		break;
	case 2:
		switch (width) {
		case 4:
			for (i = 0; i < n; i++)
				*--pi = *--ps + GDK_VAROFFSET;
			break;
#if SIZEOF_VAR_T == 8
		case 8:
			for (i = 0; i < n; i++)
				*--pv = *--ps + GDK_VAROFFSET;
			break;
#endif
		}
		break;
#if SIZEOF_VAR_T == 8
	case 4:
		for (i = 0; i < n; i++)
			*--pv = *--pi;
		break;
#endif
	}
	c->heap.free <<= shift - c->shift;
	c->shift = shift;
	c->width = width;
	return GDK_SUCCEED;
}

/*
 * @- HEAPcopy
 * simple: alloc and copy. Notice that we suppose a preallocated
 * dst->filename (or NULL), which might be used in HEAPalloc().
 */
int
HEAPcopy(Heap *dst, Heap *src)
{
	if (HEAPalloc(dst, src->size, 1) == 0) {
		dst->free = src->free;
		memcpy(dst->base, src->base, src->free);
		dst->hashash = src->hashash;
		return 0;
	}
	return -1;
}

/*
 * @- HEAPfree
 * Is now called even on heaps without memory, just to free the
 * pre-allocated filename.  simple: alloc and copy.
 */
static int
HEAPfree_(Heap *h, int free_file)
{
	if (h->base) {
		if (h->storage == STORE_MEM) {	/* plain memory */
			HEAPDEBUG fprintf(stderr, "#HEAPfree " SZFMT " " SZFMT " " PTRFMT "\n", h->size, h->maxsize, PTRFMTCAST h->base);
			GDKfree(h->base - RHS);
		} else {	/* mapped file, or STORE_PRIV */
			int ret = HEAPcacheAdd(h->base, h->maxsize, h->filename, h->storage, free_file);

			if (ret < 0) {
				GDKsyserror("HEAPfree: %s was not mapped\n", h->filename);
				assert(0);
			}
			HEAPDEBUG fprintf(stderr,
					  "#munmap(base=" PTRFMT ", size=" SZFMT ") = %d\n",
					  PTRFMTCAST(void *)h->base,
					  h->maxsize, ret);
		}
	}
	h->base = NULL;
	if (h->filename) {
		GDKfree(h->filename);
		h->filename = NULL;
	}
	return 0;
}

int
HEAPfree(Heap *h)
{
	return HEAPfree_(h, 0);
}

/*
 * @- HEAPload
 *
 * If we find file X.new, we move it over X (if present) and open it.
 *
 * This routine initializes the h->filename without deallocating its
 * previous contents.
 */
static int
HEAPload_intern(Heap *h, const char *nme, const char *ext, const char *suffix, int trunc)
{
	size_t truncsize = (1 + (((size_t) (h->free * 1.05)) >> REMAP_PAGE_MAXBITS)) << REMAP_PAGE_MAXBITS;
	size_t minsize = (1 + ((h->size - 1) >> REMAP_PAGE_MAXBITS)) << REMAP_PAGE_MAXBITS;
	int ret = 0, desc_status = 0;
	long_str srcpath, dstpath;
	struct stat st;

	h->storage = h->newstorage;
	h->maxsize = h->size;
	if (h->filename == NULL)
		h->filename = (char *) GDKmalloc(strlen(nme) + strlen(ext) + 2);
	if (h->filename == NULL)
		return -1;
	sprintf(h->filename, "%s.%s", nme, ext);

	/* round up mmap heap sizes to REMAP_PAGE_MAXSIZE (usually
	 * 512KB) segments */
	if ((h->storage != STORE_MEM) && (minsize != h->size)) {
		h->size = minsize;
		h->maxsize = MAX(minsize, h->maxsize);
	}

	/* when a bat is made read-only, we can truncate any unused
	 * space at the end of the heap */
	if (trunc && truncsize < h->size) {
		int fd = GDKfdlocate(nme, "mrb+", ext);
		if (fd >= 0) {
			ret = ftruncate(fd, (off_t) truncsize);
			HEAPDEBUG fprintf(stderr, "#ftruncate(file=%s.%s, size=" SZFMT ") = %d\n", nme, ext, truncsize, ret);
			close(fd);
			if (ret == 0) {
				h->size = h->maxsize = truncsize;
				desc_status = 1;
			}
		}
	}

	HEAPDEBUG {
		fprintf(stderr, "#HEAPload(%s.%s,storage=%d,free=" SZFMT ",size=" SZFMT ")\n", nme, ext, (int) h->storage, h->free, h->size);
	}
	/* On some OSs (WIN32,Solaris), it is prohibited to write to a
	 * file that is open in MAP_PRIVATE (FILE_MAP_COPY) solution:
	 * we write to a file named .ext.new.  This file, if present,
	 * takes precedence. */
	GDKfilepath(srcpath, BATDIR, nme, ext);
	GDKfilepath(dstpath, BATDIR, nme, ext);
	assert(strlen(srcpath) + strlen(suffix) < sizeof(srcpath));
	strcat(srcpath, suffix);
	ret = stat(dstpath, &st);
	if (stat(srcpath, &st) == 0) {
		int t0;
		if (ret == 0) {
			t0 = GDKms();
			ret = unlink(dstpath);
			HEAPDEBUG fprintf(stderr, "#unlink %s = %d (%dms)\n", dstpath, ret, GDKms() - t0);
		}
		t0 = GDKms();
		ret = rename(srcpath, dstpath);
		if (ret < 0) {
			GDKsyserror("HEAPload: rename of %s failed\n", srcpath);
			return -1;
		}
		HEAPDEBUG fprintf(stderr, "#rename %s %s = %d (%dms)\n", srcpath, dstpath, ret, GDKms() - t0);
	}

	h->base = (char *) GDKload(nme, ext, h->free, h->size, h->newstorage);
	HEAPDEBUG fprintf(stderr, "#HEAPload: GDKLoad gave %p, mode = %d\n", h->base, h->newstorage);
	if (h->base == NULL)
		return -1;	/* file could  not be read satisfactorily */

	return desc_status;
}

int
HEAPload(Heap *h, const char *nme, const char *ext, int trunc)
{
	return HEAPload_intern(h, nme, ext, ".new", trunc);
}

/*
 * @- HEAPsave
 *
 * Saving STORE_MEM will do a write(fd, buf, size) in GDKsave
 * (explicit IO).
 *
 * Saving a STORE_PRIV heap X means that we must actually write to
 * X.new, thus we convert the mode passed to GDKsave to STORE_MEM.
 *
 * Saving STORE_MMAP will do a msync(buf, MSSYNC) in GDKsave (implicit
 * IO).
 *
 * After GDKsave returns successfully (>=0), we assume the heaps are
 * safe on stable storage.
 */
static int
HEAPsave_intern(Heap *h, const char *nme, const char *ext, const char *suffix)
{
	storage_t store = h->newstorage;
	long_str extension;

	if (h->base == NULL) {
		return -1;
	}
	if (h->storage != STORE_MEM && store == STORE_PRIV) {
		/* anonymous or private VM is saved as if it were malloced */
		store = STORE_MEM;
		assert(strlen(ext) + strlen(suffix) < sizeof(extension));
		snprintf(extension, sizeof(extension), "%s%s", ext, suffix);
		ext = extension;
	} else if (store != STORE_MEM) {
		store = h->storage;
	}
	HEAPDEBUG {
		fprintf(stderr, "#HEAPsave(%s.%s,storage=%d,free=" SZFMT ",size=" SZFMT ")\n", nme, ext, (int) h->newstorage, h->free, h->size);
	}
	return GDKsave(nme, ext, h->base, h->free, store);
}

int
HEAPsave(Heap *h, const char *nme, const char *ext)
{
	return HEAPsave_intern(h, nme, ext, ".new");
}

/*
 * @- HEAPdelete
 * Delete any saved heap file. For memory mapped files, also try to
 * remove any remaining X.new
 */
int
HEAPdelete(Heap *h, const char *o, const char *ext)
{
	char ext2[64];

	if (h->size <= 0) {
		assert(h->base == 0);
		return 0;
	}
	if (h->base)
		HEAPfree_(h, 1);
	if (h->copied) {
		return 0;
	}
	assert(strlen(ext) + strlen(".new") < sizeof(ext2));
	snprintf(ext2, sizeof(ext2), "%s%s", ext, ".new");
	return (GDKunlink(BATDIR, o, ext) == 0) | (GDKunlink(BATDIR, o, ext2) == 0) ? 0 : -1;
}

int
HEAPwarm(Heap *h)
{
	int bogus_result = 0;

	if (h->storage != STORE_MEM) {
		/* touch the heap sequentially */
		int *cur = (int *) h->base;
		int *lim = (int *) (h->base + h->free) - 4096;

		for (; cur < lim; cur += 4096)	/* try to schedule 4 parallel memory accesses */
			bogus_result += cur[0] + cur[1024] + cur[2048] + cur[3072];
	}
	return bogus_result;
}


/*
 * @- HEAPvmsize
 * count all memory that takes up address space.
 */
size_t
HEAPvmsize(Heap *h)
{
	if (h && h->free)
		return h->maxsize;
	return 0;
}

/*
 * @- HEAPmemsize
 * count all memory that takes up swap space. We conservatively count
 * STORE_PRIV heaps as fully backed by swap space.
 */
size_t
HEAPmemsize(Heap *h)
{
	if (h && h->free && h->storage != STORE_MMAP)
		return h->size;
	return 0;
}


/*
 * @+ Standard Heap Library
 * This library contains some routines which implement a @emph{
 * malloc} and @emph{ free} function on the Monet @emph{Heap}
 * structure. They are useful when implementing a new @emph{
 * variable-size} atomic data type, or for implementing new search
 * accelerators.  All functions start with the prefix @emph{HEAP_}. T
 *
 * Due to non-careful design, the HEADER field was found to be
 * 32/64-bit dependent.  As we do not (yet) want to change the BAT
 * image on disk, This is now fixed by switching on-the-fly between
 * two representations. We ensure that the 64-bit memory
 * representation is just as long as the 32-bits version (20 bytes) so
 * the rest of the heap never needs to shift. The function
 * HEAP_checkformat converts at load time dynamically between the
 * layout found on disk and the memory format.  Recognition of the
 * header mode is done by looking at the first two ints: alignment
 * must be 4 or 8, and head can never be 4 or eight.
 *
 * TODO: user HEADER64 for both 32 and 64 bits (requires BAT format
 * change)
 */
/* #define DEBUG */
/* #define TRACE */

#define HEAPVERSION	20030408

typedef struct heapheader {
	size_t head;		/* index to first free block            */
	int alignment;		/* alignment of objects on heap         */
	size_t firstblock;	/* first block in heap                  */
	int version;
	int (*sizefcn)(const void *);	/* ADT function to ask length           */
} HEADER32;

typedef struct {
	int version;
	int alignment;
	size_t head;
	size_t firstblock;
	int (*sizefcn)(const void *);
} HEADER64;

#if SIZEOF_SIZE_T==8
typedef HEADER64 HEADER;
typedef HEADER32 HEADER_OTHER;
#else
typedef HEADER32 HEADER;
typedef HEADER64 HEADER_OTHER;
#endif
typedef struct hfblock {
	size_t size;		/* Size of this block in freelist        */
	size_t next;		/* index of next block                   */
} CHUNK;

#define roundup_8(x)	(((x)+7)&~7)
#define roundup_4(x)	(((x)+3)&~3)
#define blocksize(h,p)	((p)->size)

static inline size_t
roundup_num(size_t number, int alignment)
{
	size_t rval;

	rval = number + (size_t) alignment - 1;
	rval -= (rval % (size_t) alignment);
	return rval;
}

#ifdef TRACE
static void
HEAP_printstatus(Heap *heap)
{
	HEADER *hheader = HEAP_index(heap, 0, HEADER);
	size_t block, cur_free = hheader->head;
	CHUNK *blockp;

	THRprintf(GDKstdout,
		  "#HEAP has head " SZFMT " and alignment %d and size " SZFMT "\n",
		  hheader->head, hheader->alignment, heap->free);

	/* Walk the blocklist */
	block = hheader->firstblock;

	while (block < heap->free) {
		blockp = HEAP_index(heap, block, CHUNK);

		if (block == cur_free) {
			THRprintf(GDKstdout,
				  "#   free block at " PTRFMT " has size " SZFMT " and next " SZFMT "\n",
				  PTRFMTCAST(void *)block,
				  blockp->size, blockp->next);

			cur_free = blockp->next;
			block += blockp->size;
		} else {
			size_t size = blocksize(hheader, blockp);

			THRprintf(GDKstdout,
				  "#   block at " SZFMT " with size " SZFMT "\n",
				  block, size);
			block += size;
		}
	}
}
#endif /* TRACE */

static void
HEAP_empty(Heap *heap, size_t nprivate, int alignment)
{
	/* Find position of header block. */
	HEADER *hheader = HEAP_index(heap, 0, HEADER);

	/* Calculate position of first and only free block. */
	size_t head = roundup_num((size_t) (roundup_8(sizeof(HEADER)) + roundup_8(nprivate)), alignment);
	CHUNK *headp = HEAP_index(heap, head, CHUNK);

	assert(roundup_8(sizeof(HEADER)) + roundup_8(nprivate) <= VAR_MAX);

	/* Fill header block. */
	hheader->head = head;
	hheader->sizefcn = NULL;
	hheader->alignment = alignment;
	hheader->firstblock = head;
	hheader->version = HEAPVERSION;

	/* Fill first free block. */
	assert(heap->size - head <= VAR_MAX);
	headp->size = (size_t) (heap->size - head);
	headp->next = 0;
#ifdef TRACE
	THRprintf(GDKstdout, "#We created the following heap\n");
	HEAP_printstatus(heap);
#endif
}

void
HEAP_initialize(Heap *heap, size_t nbytes, size_t nprivate, int alignment)
{
	/* For now we know about two alignments. */
	if (alignment != 8) {
		alignment = 4;
	}
	if ((size_t) alignment < sizeof(size_t))
		alignment = (int) sizeof(size_t);

	/* Calculate number of bytes needed for heap + structures. */
	{
		size_t total = 100 + nbytes + nprivate + sizeof(HEADER) + sizeof(CHUNK);

		total = roundup_8(total);
		if (HEAPalloc(heap, total, 1) < 0)
			return;
		heap->free = heap->size;
	}

	/* initialize heap as empty */
	HEAP_empty(heap, nprivate, alignment);
}


var_t
HEAP_malloc(Heap *heap, size_t nbytes)
{
	size_t block, trail, ttrail;
	CHUNK *blockp;
	CHUNK *trailp;
	HEADER *hheader = HEAP_index(heap, 0, HEADER);

#ifdef TRACE
	THRprintf(GDKstdout, "#Enter malloc with " SZFMT " bytes\n", nbytes);
#endif

	/* add space for size field */
	nbytes += hheader->alignment;
	nbytes = roundup_8(nbytes);
	if (nbytes < sizeof(CHUNK))
		nbytes = (size_t) sizeof(CHUNK);

	/* block  -- points to block with acceptable size (if available).
	 * trail  -- points to predecessor of block.
	 * ttrail -- points to predecessor of trail.
	 */
	ttrail = 0;
	trail = 0;
	for (block = hheader->head; block != 0; block = HEAP_index(heap, block, CHUNK)->next) {
		blockp = HEAP_index(heap, block, CHUNK);

#ifdef TRACE
		THRprintf(GDKstdout, "#block " SZFMT " is " SZFMT " bytes\n", block, blockp->size);
#endif
		if ((trail != 0) && (block <= trail))
			GDKfatal("HEAP_malloc: Free list is not orderered\n");

		if (blockp->size >= nbytes)
			break;
		ttrail = trail;
		trail = block;
	}

	/* If no block of acceptable size is found we try to enlarge
	 * the heap. */
	if (block == 0) {
		size_t newsize;

		assert(heap->free + MAX(heap->free, nbytes) <= VAR_MAX);
		newsize = (size_t) roundup_8(heap->free + MAX(heap->free, nbytes));
		assert(heap->free <= VAR_MAX);
		block = (size_t) heap->free;	/* current end-of-heap */

#ifdef TRACE
		THRprintf(GDKstdout, "#No block found\n");
#endif

		/* Double the size of the heap.
		 * TUNE: increase heap by diffent amount. */
		HEAPDEBUG fprintf(stderr, "#HEAPextend in HEAP_malloc %s " SZFMT " " SZFMT "\n", heap->filename, heap->size, newsize);
		if (HEAPextend(heap, newsize) < 0)
			return 0;
		heap->free = newsize;
		hheader = HEAP_index(heap, 0, HEADER);

		blockp = HEAP_index(heap, block, CHUNK);
		trailp = HEAP_index(heap, trail, CHUNK);

#ifdef TRACE
		THRprintf(GDKstdout, "#New block made at pos " SZFMT " with size " SZFMT "\n", block, heap->size - block);
#endif

		blockp->next = 0;
		assert(heap->free - block <= VAR_MAX);
		blockp->size = (size_t) (heap->free - block);	/* determine size of allocated block */

		/* Try to join the last block in the freelist and the
		 * newly allocated memory */
		if ((trail != 0) && (trail + trailp->size == block)) {
#ifdef TRACE
			THRprintf(GDKstdout, "#Glue newly generated block to adjacent last\n");
#endif

			trailp->size += blockp->size;
			trailp->next = blockp->next;

			block = trail;
			trail = ttrail;
		}
	}

	/* Now we have found a block which is big enough in block.
	 * The predecessor of this block is in trail. */
	trailp = HEAP_index(heap, trail, CHUNK);
	blockp = HEAP_index(heap, block, CHUNK);

	/* If selected block is bigger than block needed split block
	 * in two.
	 * TUNE: use different amount than 2*sizeof(CHUNK) */
	if (blockp->size >= nbytes + 2 * sizeof(CHUNK)) {
		size_t newblock = block + nbytes;
		CHUNK *newblockp = HEAP_index(heap, newblock, CHUNK);

		newblockp->size = blockp->size - nbytes;
		newblockp->next = blockp->next;

		blockp->next = newblock;
		blockp->size = nbytes;
	}

	/* Delete block from freelist */
	if (trail == 0) {
		hheader->head = blockp->next;
	} else {
		trailp = HEAP_index(heap, trail, CHUNK);

		trailp->next = blockp->next;
	}

	block += hheader->alignment;
	return (var_t) (block >> GDK_VARSHIFT);
}

void
HEAP_free(Heap *heap, var_t mem)
{
	HEADER *hheader = HEAP_index(heap, 0, HEADER);
	CHUNK *beforep;
	CHUNK *blockp;
	CHUNK *afterp;
	size_t after, before, block = mem << GDK_VARSHIFT;

	if (hheader->alignment != 8 && hheader->alignment != 4) {
		GDKfatal("HEAP_free: Heap structure corrupt\n");
	}

	block -= hheader->alignment;
	blockp = HEAP_index(heap, block, CHUNK);

	/* block   -- block which we want to free
	 * before  -- first free block before block
	 * after   -- first free block after block
	 */

	before = 0;
	for (after = hheader->head; after != 0; after = HEAP_index(heap, after, CHUNK)->next) {
		if (after > block)
			break;
		before = after;
	}

	beforep = HEAP_index(heap, before, CHUNK);
	afterp = HEAP_index(heap, after, CHUNK);

	/* If it is not the last free block. */
	if (after != 0) {
		/*
		 * If this block and the block after are consecutive.
		 */
		if (block + blockp->size == after) {
			/*
			 * We unite them.
			 */
			blockp->size += afterp->size;
			blockp->next = afterp->next;
		} else
			blockp->next = after;
	} else {
		/*
		 * It is the last block in the freelist.
		 */
		blockp->next = 0;
	}

	/*
	 * If it is not the first block in the list.
	 */
	if (before != 0) {
		/*
		 * If the before block and this block are consecutive.
		 */
		if (before + beforep->size == block) {
			/*
			 * We unite them.
			 */
			beforep->size += blockp->size;
			beforep->next = blockp->next;
		} else
			beforep->next = block;
	} else {
		/*
		 * Add block at head of free list.
		 */
		hheader->head = block;
	}
}

int
HEAP_check(Heap *heap, HeapRepair *hr)
{
	HEADER *hheader = HEAP_index(heap, 0, HEADER);
	size_t head = hheader->head, alignshift = 2;
	size_t block, nwords = (size_t) ((heap->free - 1) >> 7);
	int *freemask;
	size_t prevblock = 0;
	CHUNK *blockp;

	hr->alignment = hheader->alignment;
	hr->minpos = sizeof(HEADER);
	hr->maxpos = heap->free;
	hr->validmask = NULL;

	if (hheader->alignment == 8) {
		nwords >>= 1;
		alignshift = 3;
	} else if (hheader->alignment != 4) {
		GDKerror("HEAP_check: Heap structure corrupt alignment = %d\n", hheader->alignment);
		return FALSE;
	}
	if ((head != roundup_num(head, hheader->alignment))) {
		GDKerror("HEAP_check: Heap structure corrupt: head = " SZFMT "\n", head);
		return FALSE;
	}

	/*
	 * Create bitmasks that will hold all valid block positions
	 */
	hr->validmask = (int *) GDKzalloc(sizeof(int) * ++nwords);
	freemask = (int *) GDKzalloc(sizeof(int) * nwords);
	if (hr->validmask == NULL || freemask == NULL) {
		GDKfree(hr->validmask);
		GDKfree(freemask);
		return FALSE;
	}

	/*
	 * Walk the freelist; register them in freemask
	 */
	for (block = hheader->head; block != 0; block = HEAP_index(heap, block, CHUNK)->next) {
		size_t idx = block >> alignshift;
		size_t pos = idx >> 5;
		int mask = 1 << (idx & 31);

		if ((block <= prevblock) && (block != 0)) {
			GDKerror("HEAP_check: Freelist is not ordered\n");
		} else if (block <= 0 || block > heap->free) {
			GDKerror("HEAP_check: Entry freelist corrupt: block " SZFMT " not in heap\n", block);
		} else {
			freemask[pos] |= mask;
			continue;
		}
		goto xit;
	}

	/*
	 * Walk the blocklist; register in validmask/eliminate from freemask
	 */
	block = hheader->firstblock;
	while (block < heap->free) {
		size_t idx = block >> alignshift;
		size_t pos = idx >> 5;
		int mask = 1 << (idx & 31);

		hr->validmask[pos] |= mask;
		blockp = HEAP_index(heap, block, CHUNK);

		if (freemask[pos] & mask) {
			freemask[pos] &= ~mask;
			block += blockp->size;
		} else {
			block += blocksize(hheader, blockp);
		}
	}
	if (block != heap->free) {
		GDKerror("HEAP_check: Something wrong with heap\n");
		goto xit;
	}

	/*
	 * Check if there are left over free blocks
	 */
	for (block = hheader->head; block != 0; block = HEAP_index(heap, block, CHUNK)->next) {
		size_t idx = block >> alignshift;
		size_t pos = idx >> 5;
		int mask = 1 << (idx & 31);

		if (freemask[pos] & mask) {
			GDKerror("HEAP_check: Entry freelist corrupt: block " SZFMT " not in blocklist\n", block);
			goto xit;
		}
	}
	GDKfree(freemask);
	return TRUE;
      xit:
	GDKfree(freemask);
	GDKfree(hr->validmask);
	hr->validmask = NULL;
	return FALSE;
}

/*
 * The HEAP_init() function is called in the BAT load sequence, if
 * Monet sees that a standard heap is being loaded (it looks for a
 * directly registered HEAP_check ADT function).
 */
/* reinitialize the size function after a load */
void
HEAP_init(Heap *heap, int tpe)
{
	HEADER *hheader = HEAP_index(heap, 0, HEADER);

	if (hheader->sizefcn) {
		hheader->sizefcn = BATatoms[tpe].atomLen;
	}

	/* make sure the freelist does not point after the end of the
	 * heap */
	if (hheader->head > heap->free) {
		hheader->head = 0;	/* cut off free block */
	} else if (hheader->head) {
		size_t idx = hheader->head;

		while (idx) {
			CHUNK *blk = HEAP_index(heap, idx, CHUNK);

			if (idx + blk->size > heap->free) {
				assert(heap->free - idx <= VAR_MAX);
				blk->size = (size_t) (heap->free - idx);	/* cut off illegal tail of block */
			}
			if (blk->next > heap->free || blk->next < (idx + blk->size) || (blk->next & (hheader->alignment - 1))) {
				blk->next = 0;	/* cut off next block */
				break;
			}
			idx = blk->next;
		}
	}
}

/* a heap is mmapabble (in append-only mode) if it only has a hole at
 * the end */
int
HEAP_mmappable(Heap *heap)
{
	HEADER *hheader = HEAP_index(heap, 0, HEADER);

	if (hheader->head) {
		CHUNK *blk = HEAP_index(heap, hheader->head, CHUNK);

		if (hheader->head + blk->size >= heap->free) {
			return TRUE;
		}
	}
	return FALSE;
}
