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
 *
 * @* Database Storage Management
 * Contains routines for writing and reading GDK data to and from
 * disk.  This section contains the primitives to manage the
 * disk-based images of the BATs. It relies on the existence of a UNIX
 * file system, including memory mapped files. Solaris and IRIX have
 * different implementations of madvise().
 *
 * The current version assumes that all BATs are stored on a single
 * disk partition. This simplistic assumption should be replaced in
 * the near future by a multi-volume version. The intension is to use
 * several BAT home locations.  The files should be owned by the
 * database server. Otherwise, IO operations are likely to fail. This
 * is accomplished by setting the GID and UID upon system start.
 */
#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"
#include <stdlib.h>
#include "gdk_storage.h"
#include "mutils.h"
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <Rinternals.h>
#ifndef RHS
#define RHS Rf_sizeofHeader()
#endif

void
GDKfilepath(str path, const char *dir, const char *name, const char *ext)
{
	*path = 0;
	if (dir && *dir && *name != DIR_SEP) {
		strcpy(path, dir);
		path += strlen(dir);
		if (path[-1] != DIR_SEP) {
			*path++ = DIR_SEP;
			*path = 0;
		}
	}
	strcpy(path, name);
	if (ext) {
		path += strlen(name);
		*path++ = '.';
		strcpy(path, ext);
	}
}

int
GDKcreatedir(const char *dir)
{
	char path[PATHLENGTH];
	char *r;
	int ret = FALSE;

	assert(strlen(dir) < sizeof(path));
	strncpy(path, dir, sizeof(path)-1);
	path[sizeof(path)-1] = 0;
	r = strrchr(path, DIR_SEP);
	IODEBUG THRprintf(GDKstdout, "#GDKcreatedir(%s)\n", path);

	if (r) {
		DIR *dirp;

		*r = 0;
		dirp = opendir(path);
		if (dirp) {
			closedir(dirp);
		} else {
			GDKcreatedir(path);
			ret = mkdir(path, 0755);
			IODEBUG THRprintf(GDKstdout, "#mkdir %s = %d\n", path, ret);
			if (ret < 0 && (dirp = opendir(path)) != NULL) {
				/* resolve race */
				ret = 0;
				closedir(dirp);
			}
		}
		*r = DIR_SEP;
	}
	return !ret;
}

int
GDKremovedir(const char *dirname)
{
	DIR *dirp = opendir(dirname);
	char path[PATHLENGTH];
	struct dirent *dent;
	int ret;

	IODEBUG THRprintf(GDKstdout, "#GDKremovedir(%s)\n", dirname);

	if (dirp == NULL)
		return 0;
	while ((dent = readdir(dirp)) != NULL) {
		if ((dent->d_name[0] == '.') && ((dent->d_name[1] == 0) || (dent->d_name[1] == '.' && dent->d_name[2] == 0))) {
			continue;
		}
		GDKfilepath(path, dirname, dent->d_name, NULL);
		ret = unlink(path);
		IODEBUG THRprintf(GDKstdout, "#unlink %s = %d\n", path, ret);
	}
	closedir(dirp);
	ret = rmdir(dirname);
	if (ret < 0) {
		GDKsyserror("GDKremovedir: rmdir(%s) failed.\n", dirname);
	}
	IODEBUG THRprintf(GDKstdout, "#rmdir %s = %d\n", dirname, ret);

	return ret;
}

#define _FUNBUF         0x040000
#define _FWRTHR         0x080000
#define _FRDSEQ         0x100000

int
GDKfdlocate(const char *nme, const char *mode, const char *extension)
{
	char buf[PATHLENGTH], *path = buf;
	int fd, flags = 0;

	if ((nme == NULL) || (*nme == 0)) {
		return 0;
	}
	GDKfilepath(path, BATDIR, nme, extension);

	if (*mode == 'm') {	/* file open for mmap? */
		mode++;
#ifdef _CYGNUS_H_
	} else {
		flags = _FRDSEQ;	/* WIN32 CreateFile(FILE_FLAG_SEQUENTIAL_SCAN) */
#endif
	}

	if (strchr(mode, 'w')) {
		flags |= O_WRONLY | O_CREAT;
	} else if (!strchr(mode, '+')) {
		flags |= O_RDONLY;
	} else {
		flags |= O_RDWR;
	}
#ifdef WIN32
	flags |= strchr(mode, 'b') ? O_BINARY : O_TEXT;
#endif
	fd = open(path, flags, MONETDB_MODE);
	if (fd < 0 && *mode == 'w') {
		/* try to create the directory, if that was the problem */
		char tmp[PATHLENGTH];

		strcpy(tmp, buf);
		if (GDKcreatedir(tmp)) {
			fd = open(path, flags, MONETDB_MODE);
		}
	}
	return fd;
}

FILE *
GDKfilelocate(const char *nme, const char *mode, const char *extension)
{
	int fd = GDKfdlocate(nme, mode, extension);

	if (*mode == 'm')
		mode++;
	return (fd < 0) ? NULL : fdopen(fd, mode);
}


/*
 * Unlink the file.
 */
int
GDKunlink(const char *dir, const char *nme, const char *ext)
{
	char path[PATHLENGTH];

	if (nme && *nme) {
		GDKfilepath(path, dir, nme, ext);
		/* if file already doesn't exist, we don't care */
		if (unlink(path) == -1 && errno != ENOENT) {
			GDKsyserror("GDKunlink(%s)\n", path);
			IODEBUG THRprintf(GDKstdout, "#unlink %s = -1\n", path);
			return -1;
		}
		return 0;
	}
	return -1;
}

/*
 * A move routine is overloaded to deal with extensions.
 */
int
GDKmove(const char *dir1, const char *nme1, const char *ext1, const char *dir2, const char *nme2, const char *ext2)
{
	char path1[PATHLENGTH];
	char path2[PATHLENGTH];
	int ret, t0 = 0;

	IODEBUG t0 = GDKms();

	if ((nme1 == NULL) || (*nme1 == 0)) {
		return -1;
	}
	GDKfilepath(path1, dir1, nme1, ext1);
	GDKfilepath(path2, dir2, nme2, ext2);
	ret = rename(path1, path2);

	IODEBUG THRprintf(GDKstdout, "#move %s %s = %d (%dms)\n", path1, path2, ret, GDKms() - t0);

	return ret;
}

int
GDKextend(const char *fn, size_t size)
{
	FILE *fp;
	int t0 = 0;

	IODEBUG t0 = GDKms();
	if ((fp = fopen(fn, "rb+")) == NULL)
		return -1;
#if defined(_WIN64)
	if (_fseeki64(fp, (ssize_t) size - 1, SEEK_SET) < 0)
		goto bailout;
#elif defined(HAVE_FSEEKO)
	if (fseeko(fp, (off_t) size - 1, SEEK_SET) < 0)
		goto bailout;
#else
	if (fseek(fp, size - 1, SEEK_SET) < 0)
		goto bailout;
#endif
	if (fputc('\n', fp) < 0)
		goto bailout;
	if (fflush(fp) < 0)
		goto bailout;
	if (fclose(fp) < 0)
		return -1;
	IODEBUG fprintf(stderr, "#GDKextend %s " SZFMT " %dms\n", fn, size, GDKms() - t0);
	return 0;
  bailout:
	fclose(fp);
	IODEBUG fprintf(stderr, "#GDKextend %s failed " SZFMT " %dms\n", fn, size, GDKms() - t0);
	return -1;
}

/*
 * @+ Save and load.
 * The BAT is saved on disk in several files. The extension DESC
 * denotes the descriptor, BUNs the bun heap, and HHEAP and THEAP the
 * other heaps. The storage mechanism off a file can be memory mapped
 * (STORE_MMAP) or malloced (STORE_MEM).
 *
 * These modes indicates the disk-layout and the intended mapping.
 * The primary concern here is to handle STORE_MMAP and STORE_MEM.
 */
int
GDKsave(const char *nme, const char *ext, void *buf, size_t size, storage_t mode)
{
	int fd = -1, err = 0;

	IODEBUG THRprintf(GDKstdout, "#GDKsave: name=%s, ext=%s, mode %d\n", nme, ext ? ext : "", (int) mode);

	if (mode == STORE_MMAP) {
		/*
		 * Only dirty pages must be written to disk.
		 * Unchanged block will still be mapped on the file,
		 * reading those will be cheap.  Only the changed
		 * blocks are now mapped to swap space.  PUSHED OUT:
		 * due to rather horrendous performance caused by
		 * updating the image on disk.
		 *
		 * Maybe it is better to make use of MT_msync().  But
		 * then we would need to bring in a backup mechanism,
		 * in which stable images of the BATs are created at
		 * commit-time.
		 */
		if (size)
			err = MT_msync(buf, 0, size, MMAP_SYNC);
		if (err)
			GDKsyserror("GDKsave: error on: name=%s, ext=%s, mode=%d\n", nme, ext ? ext : "", (int) mode);
		IODEBUG THRprintf(GDKstdout, "#MT_msync(buf " PTRFMT ", size " SZFMT ", MMAP_SYNC) = %d\n", PTRFMTCAST buf, size, err);
	} else {
		if ((fd = GDKfdlocate(nme, "wb", ext)) >= 0) {
			/* write() on 64-bits Redhat for IA64 returns
			 * 32-bits signed result (= OS BUG)! write()
			 * on Windows only takes int as size */
			while (size > 0) {
				/* circumvent problems by writing huge
				 * buffers in chunks <= 1GB */
				ssize_t ret = write(fd, buf, (unsigned) MIN(1 << 30, size));

				if (ret < 0) {
					err = -1;
					GDKsyserror("GDKsave: error " SSZFMT " on: name=%s, ext=%s, mode=%d\n", ret, nme, ext ? ext : "", (int) mode);
					break;
				}
				size -= ret;
				buf = (void *) ((char *) buf + ret);
				IODEBUG THRprintf(GDKstdout, "#write(fd %d, buf " PTRFMT ", size %u) = " SSZFMT "\n", fd, PTRFMTCAST buf, (unsigned) MIN(1 << 30, size), ret);
			}
		} else {
			err = -1;
		}
	}
	if (fd >= 0) {
		err |= close(fd);
		if (err && GDKunlink(BATDIR, nme, ext)) {
			/* do not tolerate corrupt heap images
			 * (BBPrecover on restart will kill them) */
			GDKfatal("GDKsave: could not open: name=%s, ext=%s, mode %d\n", nme, ext ? ext : "", (int) mode);
		}
	} else if (mode != STORE_MMAP) {
		GDKerror("GDKsave: failed name=%s, ext=%s, mode %d\n", nme, ext ? ext : "", (int) mode);
	}
	return err;
}

/*
 * Space for the load is directly allocated and the heaps are mapped.
 * Further initialization of the atom heaps require a separate action
 * defined in their implementation.
 */
char *
GDKload(const char *nme, const char *ext, size_t size, size_t maxsize, storage_t mode)
{
	char *ret = NULL;

	IODEBUG {
		THRprintf(GDKstdout, "#GDKload: name=%s, ext=%s, mode %d\n", nme, ext ? ext : "", (int) mode);
	}
	if (mode == STORE_MEM) {
		int fd = GDKfdlocate(nme, "rb", ext);

		if (fd >= 0) {
			// R hack
			char *dst = ret = ((char *) GDKmalloc(maxsize + RHS)) + RHS;
			ssize_t n_expected, n = 0;

			if (ret) {
				/* read in chunks, some OSs do not
				 * give you all at once and Windows
				 * only accepts int */
				for (n_expected = (ssize_t) size; n_expected > 0; n_expected -= n) {
					n = read(fd, dst, (unsigned) MIN(1 << 30, n_expected));
					IODEBUG THRprintf(GDKstdout, "#read(dst " PTRFMT ", n_expected " SSZFMT ", fd %d) = " SSZFMT "\n", PTRFMTCAST(void *)dst, n_expected, fd, n);

					if (n <= 0)
						break;
					dst += n;
				}
				if (n_expected > 0) {
					GDKfree(ret);
					GDKsyserror("GDKload: cannot read: name=%s, ext=%s, " SZFMT " bytes missing.\n", nme, ext ? ext : "", (size_t) n_expected);
					ret = NULL;
				}
#ifndef NDEBUG
				/* just to make valgrind happy, we
				 * initialize the whole thing */
				if (ret && maxsize > size)
					memset(ret + size, 0, maxsize - size);
#endif
			}
			close(fd);
		} else {
			GDKsyserror("GDKload: cannot open: name=%s, ext=%s\n", nme, ext ? ext : "");
		}
	} else {
		char path[PATHLENGTH];
		struct stat st;

		GDKfilepath(path, BATDIR, nme, ext);
		if (stat(path, &st) >= 0 &&
		    (maxsize < (size_t) st.st_size ||
		     /* mmap storage is auto-extended here */
		     GDKextend(path, maxsize) == 0)) {
			int mod = MMAP_READ | MMAP_WRITE | MMAP_SEQUENTIAL | MMAP_SYNC;

			if (mode == STORE_PRIV)
				mod |= MMAP_COPY;
			ret = (char *) GDKmmap(path, mod, maxsize);
			if (ret == (char *) -1L) {
				ret = NULL;
			}
			IODEBUG THRprintf(GDKstdout, "#mmap(NULL, 0, maxsize " SZFMT ", mod %d, path %s, 0) = " PTRFMT "\n", maxsize, mod, path, PTRFMTCAST(void *)ret);
		}
	}
	return ret;
}

/*
 * @+ BAT disk storage
 *
 * Between sessions the BATs comprising the database are saved on
 * disk.  To simplify code, we assume a UNIX directory called its
 * physical @%home@ where they are to be located.  The subdirectories
 * BAT and PRG contain what its name says.
 *
 * A BAT created by @%BATnew@ is considered temporary until one calls
 * the routine @%BATsave@. This routine reserves disk space and checks
 * for name clashes.
 *
 * Saving and restoring BATs is left to the upper layers. The library
 * merely copies the data into place.  Failure to read or write the
 * BAT results in a NULL, otherwise it returns the BAT pointer.
 */
static BATstore *
DESCload(int i)
{
	str s, nme = BBP_physical(i);
	BATstore *bs;
	BAT *b = NULL;
	int ht, tt;

	IODEBUG {
		THRprintf(GDKstdout, "#DESCload %s\n", nme ? nme : "<noname>");
	}
	bs = BBP_desc(i);

	if (bs == NULL)
		return 0;
	b = &bs->B;

	ht = b->htype;
	tt = b->ttype;
	if ((ht < 0 && (ht = ATOMindex(s = ATOMunknown_name(ht))) < 0) ||
	    (tt < 0 && (tt = ATOMindex(s = ATOMunknown_name(tt))) < 0)) {
		GDKerror("DESCload: atom '%s' unknown, in BAT '%s'.\n", s, nme);
		return NULL;
	}
	b->htype = ht;
	b->ttype = tt;
	b->H->hash = b->T->hash = NULL;
	/* mil shouldn't mess with just loaded bats */
	if (b->batStamp > 0)
		b->batStamp = -b->batStamp;

	/* reconstruct mode from BBP status (BATmode doesn't flush
	 * descriptor, so loaded mode may be stale) */
	b->batPersistence = (BBP_status(b->batCacheid) & BBPPERSISTENT) ? PERSISTENT : TRANSIENT;
	b->batCopiedtodisk = 1;
	DESCclean(b);
	return bs;
}

#define STORE_MODE(m,r,e) (((m) == STORE_MEM)?STORE_MEM:((r)&&(e))?STORE_PRIV:STORE_MMAP)
int
DESCsetmodes(BAT *b)
{
	int existing = (BBPstatus(b->batCacheid) & BBPEXISTING);
	int brestrict = (b->batRestricted == BAT_WRITE);
	int ret = 0;
	storage_t m;

	if (b->batMaphead) {
		m = STORE_MODE(b->batMaphead, brestrict, existing);
		ret |= m != b->H->heap.newstorage || m != b->H->heap.storage;
		b->H->heap.newstorage = b->H->heap.storage = m;
	}
	if (b->batMaptail) {
		m = STORE_MODE(b->batMaptail, brestrict, existing);
		ret |= b->T->heap.newstorage != m || b->T->heap.storage != m;
		b->T->heap.newstorage = b->T->heap.storage = m;
	}
	if (b->H->vheap && b->batMaphheap) {
		int hrestrict = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->htype, b->H->vheap);
		m = STORE_MODE(b->batMaphheap, brestrict || hrestrict, existing);
		ret |= b->H->vheap->newstorage != m || b->H->vheap->storage != m;
		b->H->vheap->newstorage = b->H->vheap->storage = m;
	}
	if (b->T->vheap && b->batMaptheap) {
		int trestrict = (b->batRestricted == BAT_APPEND) && ATOMappendpriv(b->ttype, b->T->vheap);
		m = STORE_MODE(b->batMaptheap, brestrict || trestrict, existing);
		ret |= b->T->vheap->newstorage != m || b->T->vheap->storage != m;
		b->T->vheap->newstorage = b->T->vheap->storage = m;
	}
	return ret;
}

void
DESCclean(BAT *b)
{
	b->batDirtyflushed = DELTAdirty(b) ? TRUE : FALSE;
	b->batDirty = 0;
	b->batDirtydesc = 0;
	b->H->heap.dirty = 0;
	b->T->heap.dirty = 0;
	if (b->H->vheap)
		b->H->vheap->dirty = 0;
	if (b->T->vheap)
		b->T->vheap->dirty = 0;
}

BAT *
BATsave(BAT *bd)
{
	int err = 0;
	char *nme;
	BATstore bs;
	BAT *b = bd;

	BATcheck(b, "BATsave");

	/* views cannot be saved, but make an exception for
	 * force-remapped views */
	if (isVIEW(b) &&
	    !(b->H->heap.copied && b->H->heap.storage == STORE_MMAP) &&
	    !(b->T->heap.copied && b->T->heap.storage == STORE_MMAP)) {
		GDKerror("BATsave: %s is a view on %s; cannot be saved\n", BATgetId(b), VIEWhparent(b) ? BBPname(VIEWhparent(b)) : BBPname(VIEWtparent(b)));
		return NULL;
	}
	if (!BATdirty(b)) {
		return b;
	}
	if (b->batCacheid < 0) {
		b = BATmirror(b);
	}
	if (!DELTAdirty(b))
		ALIGNcommit(b);
	if (!b->halign)
		b->halign = OIDnew(1);
	if (!b->talign)
		b->talign = OIDnew(1);

	/* copy the descriptor to a local variable in order to let our
	 * messing in the BAT descriptor not affect other threads that
	 * only read it. */
	bs = *BBP_desc(b->batCacheid);
	/* fix up internal pointers */
	b = &bs.BM;		/* first the mirror */
	b->P = &bs.P;
	b->H = &bs.T;
	b->T = &bs.H;
	b = &bs.B;		/* then the unmirrored version */
	b->P = &bs.P;
	b->H = &bs.H;
	b->T = &bs.T;

	if (b->H->vheap) {
		b->H->vheap = (Heap *) GDKmalloc(sizeof(Heap));
		if (b->H->vheap == NULL)
			return NULL;
		*b->H->vheap = *bd->H->vheap;
	}
	if (b->T->vheap) {
		b->T->vheap = (Heap *) GDKmalloc(sizeof(Heap));
		if (b->T->vheap == NULL) {
			if (b->H->vheap)
				GDKfree(b->H->vheap);
			return NULL;
		}
		*b->T->vheap = *bd->T->vheap;
	}

	/* start saving data */
	nme = BBP_physical(b->batCacheid);
	if (b->batCopiedtodisk == 0 || b->batDirty || b->H->heap.dirty)
		if (err == 0 && b->htype)
			err = HEAPsave(&b->H->heap, nme, "head");
	if (b->batCopiedtodisk == 0 || b->batDirty || b->T->heap.dirty)
		if (err == 0 && b->ttype)
			err = HEAPsave(&b->T->heap, nme, "tail");
	if (b->H->vheap && (b->batCopiedtodisk == 0 || b->batDirty || b->H->vheap->dirty))
		if (b->htype && b->hvarsized) {
			if (err == 0)
				err = HEAPsave(b->H->vheap, nme, "hheap");
		}
	if (b->T->vheap && (b->batCopiedtodisk == 0 || b->batDirty || b->T->vheap->dirty))
		if (b->ttype && b->tvarsized) {
			if (err == 0)
				err = HEAPsave(b->T->vheap, nme, "theap");
		}

	if (b->H->vheap)
		GDKfree(b->H->vheap);
	if (b->T->vheap)
		GDKfree(b->T->vheap);

	if (err == 0) {
		bd->batCopiedtodisk = 1;
		DESCclean(bd);
		return bd;
	}
	return NULL;
}


/*
 * TODO: move to gdk_bbp.mx
 */
BAT *
BATload_intern(bat i, int lock)
{
	bat bid = ABS(i);
	str nme = BBP_physical(bid);
	BATstore *bs = DESCload(bid);
	BAT *b;
	int batmapdirty;

	if (bs == NULL) {
		return NULL;
	}
	b = &bs->B;
	batmapdirty = DESCsetmodes(b);

	/* LOAD bun heap */
	if (b->htype != TYPE_void) {
		if (HEAPload(&b->H->heap, nme, "head", b->batRestricted == BAT_READ) < 0) {
			return NULL;
		}
		assert(b->H->heap.size >> b->H->shift <= BUN_MAX);
		b->batCapacity = (BUN) (b->H->heap.size >> b->H->shift);
	} else {
		b->H->heap.base = NULL;
	}
	if (b->ttype != TYPE_void) {
		if (HEAPload(&b->T->heap, nme, "tail", b->batRestricted == BAT_READ) < 0) {
			HEAPfree(&b->H->heap);
			return NULL;
		}
		if (b->htype == TYPE_void) {
			assert(b->T->heap.size >> b->T->shift <= BUN_MAX);
			b->batCapacity = (BUN) (b->T->heap.size >> b->T->shift);
		}
		if (b->batCapacity != (b->T->heap.size >> b->T->shift)) {
			BUN cap = b->batCapacity;
			if (cap < (b->T->heap.size >> b->T->shift)) {
				cap = (BUN) (b->T->heap.size >> b->T->shift);
				HEAPDEBUG fprintf(stderr, "#HEAPextend in BATload_inter %s " SZFMT " " SZFMT "\n", b->H->heap.filename, b->H->heap.size, headsize(b, cap));
				HEAPextend(&b->H->heap, headsize(b, cap));
				b->batCapacity = cap;
			} else {
				HEAPDEBUG fprintf(stderr, "#HEAPextend in BATload_intern %s " SZFMT " " SZFMT "\n", b->T->heap.filename, b->T->heap.size, tailsize(b, cap));
				HEAPextend(&b->T->heap, tailsize(b, cap));
			}
		}
	} else {
		b->T->heap.base = NULL;
	}

	/* LOAD head heap */
	if (ATOMvarsized(b->htype)) {
		if (HEAPload(b->H->vheap, nme, "hheap", b->batRestricted == BAT_READ) < 0) {
			HEAPfree(&b->H->heap);
			HEAPfree(&b->T->heap);
			return NULL;
		}
		if (BATatoms[b->htype].atomHeapCheck == HEAP_check) {
			HEAP_init(b->H->vheap, b->htype);
		} else if (ATOMstorage(b->htype) == TYPE_str) {
			strCleanHash(b->H->vheap, FALSE);	/* ensure consistency */
		}
	}

	/* LOAD tail heap */
	if (ATOMvarsized(b->ttype)) {
		if (HEAPload(b->T->vheap, nme, "theap", b->batRestricted == BAT_READ) < 0) {
			if (b->H->vheap)
				HEAPfree(b->H->vheap);
			HEAPfree(&b->H->heap);
			HEAPfree(&b->T->heap);
			return NULL;
		}
		if (BATatoms[b->ttype].atomHeapCheck == HEAP_check) {
			HEAP_init(b->T->vheap, b->ttype);
		} else if (ATOMstorage(b->ttype) == TYPE_str) {
			strCleanHash(b->T->vheap, FALSE);	/* ensure consistency */
		}
	}

	/* initialize descriptor */
	b->batDirtydesc = FALSE;
	b->H->heap.parentid = b->T->heap.parentid = 0;

	/* load succeeded; register it in BBP */
	BBPcacheit(bs, lock);

	if (!DELTAdirty(b)) {
		ALIGNcommit(b);
	}
	b->batDirtydesc |= batmapdirty;	/* if some heap mode changed, make desc dirty */

	if ((b->batRestricted == BAT_WRITE && (GDKdebug & CHECKMASK)) ||
	    (GDKdebug & PROPMASK)) {
		++b->batSharecnt;
		--b->batSharecnt;
	}
	return (i < 0) ? BATmirror(b) : b;
}

/*
 * @- BATdelete
 * The new behavior is to let the routine produce warnings but always
 * succeed.  rationale: on a delete, we must get rid of *all* the
 * files. We do not have to care about preserving them or be too much
 * concerned if a file that had to be deleted was not found (end
 * result is still that it does not exist). The past behavior to
 * delete some files and then fail was erroneous. The BAT would
 * continue to exist with an incorrect disk status, causing havoc
 * later on.
 *
 * NT forces us to close all files before deleting them; in case of
 * memory mapped files this means that we have to unload the BATs
 * before deleting. This is enforced now.
 */
int
BATdelete(BAT *b)
{
	bat bid = ABS(b->batCacheid);
	str o = BBP_physical(bid);
	BAT *loaded = BBP_cache(bid);

	if (loaded) {
		b = loaded;
		HASHdestroy(b);
		IMPSdestroy(b);
	}
	assert(!b->H->heap.base || !b->T->heap.base || b->H->heap.base != b->T->heap.base);
	if (b->batCopiedtodisk || (b->H->heap.storage != STORE_MEM)) {
		if (b->htype != TYPE_void &&
		    HEAPdelete(&b->H->heap, o, "head") &&
		    b->batCopiedtodisk)
			IODEBUG THRprintf(GDKstdout, "#BATdelete(%s): bun heap\n", BATgetId(b));
	} else if (b->H->heap.base) {
		HEAPfree(&b->H->heap);
	}
	if (b->batCopiedtodisk || (b->T->heap.storage != STORE_MEM)) {
		if (b->ttype != TYPE_void &&
		    HEAPdelete(&b->T->heap, o, "tail") &&
		    b->batCopiedtodisk)
			IODEBUG THRprintf(GDKstdout, "#BATdelete(%s): bun heap\n", BATgetId(b));
	} else if (b->T->heap.base) {
		HEAPfree(&b->T->heap);
	}
	if (b->H->vheap) {
		assert(b->H->vheap->parentid == bid);
		if (b->batCopiedtodisk || (b->H->vheap->storage != STORE_MEM)) {
			if (HEAPdelete(b->H->vheap, o, "hheap") && b->batCopiedtodisk)
				IODEBUG THRprintf(GDKstdout, "#BATdelete(%s): head heap\n", BATgetId(b));
		} else {
			HEAPfree(b->H->vheap);
		}
	}
	if (b->T->vheap) {
		assert(b->T->vheap->parentid == bid);
		if (b->batCopiedtodisk || (b->T->vheap->storage != STORE_MEM)) {
			if (HEAPdelete(b->T->vheap, o, "theap") && b->batCopiedtodisk)
				IODEBUG THRprintf(GDKstdout, "#BATdelete(%s): tail heap\n", BATgetId(b));
		} else {
			HEAPfree(b->T->vheap);
		}
	}
	b->batCopiedtodisk = FALSE;
	return 0;
}

/*
 * @+ Printing and debugging
 * Printing BATs is based on the multi-join on heads. The multijoin
 * exploits all possible Monet properties and accelerators. Due to
 * this property, the n-ary table printing is quite fast and can be
 * used for producing ASCII dumps of large tables.
 *
 * It all works with hooks.  The multijoin routine finds matching
 * ranges of rows. For each found match in a column it first calls a
 * value-routine hook. This routine we use to format a substring.  For
 * each found match-tuple (the Cartesian product of all matches across
 * columns) a match routine hook is called. We use this routine to
 * print a line.  Due to this setup, we only format each value once,
 * though it might participate in many lines (due to the Cartesian
 * product).
 *
 * The multijoin is quite complex, and we use a @%col_format_t@ struct
 * to keep track of column specific data.  The multiprint can indicate
 * arbitrary orderings. This is done by passing a pattern-string that
 * matches the following regexp:
 *
 * @verbatim
 *	"[X:] Y0 {,Yi}"
 * @end verbatim
 *
 * where X and Yi are column numbers, @strong{starting at 1} for the
 * first BAT parameter.
 *
 * The table ordering has two aspects:
 * @enumerate
 * @item (1)
 *	the order in which the matches appear (a.k.a. the major
 *	ordering).  This is equivalent to the order of the head values
 *	of the BATs (as we match=multijoin on head value).
 * @item (2)
 *	within each match, the order in which the Cartesian product is
 *	produced. This is used to sub-order on the tail values of the
 *	BATs = the columns in the table.
 * @end enumerate
 *
 * Concerning (1), the multijoin limits itself to *respecting* the
 * order one one elected BAT, that can be identified with X.  Using
 * this, a major ordering on tail value can be enforced, by first
 * passing "Bx.reverse.sort.reverse" (BAT ordered on tail).  As the
 * multijoin will respect the order of X, its tail values will be
 * printed in sorted order.
 *
 * Concerning sub-ordering on other columns (2), the multijoin itself
 * employs qsort() to order the Cartesian product on the matched tail
 * values.
 */
#define LINE(s,	X)	do {						\
				int n=X-1;				\
				if (mnstr_write(s, "#", 1, 1) != 1)	\
					break;				\
				while(n-->0)				\
					if (mnstr_write(s, "-", 1, 1) != 1) \
						break;			\
				if (!mnstr_errnr(s))			\
					mnstr_write(s, "#\n", 2, 1);	\
			} while (0)
#define TABS(s,	X)	do {						\
				int n=X;				\
				while (n-->0)				\
					if (mnstr_write(s, "\t", 1, 1) != 1) \
						break;			\
			} while (0)

typedef int (*strFcn) (str *s, int *len, const void *val);

typedef struct {
	int tabs;		/* tab width of output */
	strFcn format;		/* tostr function */
	/* dynamic fields, set by print_format */
	str buf;		/* tail value as string */
	str tpe;		/* type of this column as string */
	int size;		/* size of buf */
	int len;		/* strlen(buf) */
} col_format_t;

static int
print_nil(char **dst, int *len, const void *dummy)
{
	(void) dummy;
	if (*len < 3) {
		if (*dst)
			GDKfree(*dst);
		*dst = (char *) GDKmalloc(*len = 40);
	}
	strcpy(*dst, "nil");
	return 3;
}

#define printfcn(b)	((b->ttype==TYPE_void && b->tseqbase==oid_nil)?\
			          print_nil:BATatoms[b->ttype].atomToStr)

static int
print_tabwidth(BAT *b, str title, col_format_t *c)
{
	strFcn tostr = printfcn(b);
	BUN cnt = BATcount(b);
	int max, t = BATttype(b);

	c->tpe = ATOMname(b->ttype);
	c->buf = (char *) GDKmalloc(c->size = strLen(title));
	max = (int) MAX((2 + strlen(c->tpe)), strlen(title));

	if (t >= 0 && t < GDKatomcnt && tostr) {
		BATiter bi = bat_iterator(b);
		BUN off = BUNfirst(b);
		int k;
		BUN j, i, probe = MIN(cnt, MAX(200, MIN(1024, cnt / 100)));

		for (i = 0; i < probe; i++) {
			j = off + ((probe == cnt) ? i : (rand() % MIN(16384, cnt)));
			k = (*tostr) (&c->buf, &c->size, BUNtail(bi, j));
			if (k > max)
				max = k;
		}
	}
	strcpy(c->buf, title);
	max += 2;		/* account for ", " separator */
	/* if (max > 60) max = 60; */
	return 1 + (max - 1) / 8;
}

static void
print_line(stream *s, col_format_t **l)
{
	col_format_t *c = *(l++);

	if (mnstr_write(s, "[ ", 2, 1) != 1)
		return;
	if (c->format) {
		if (mnstr_write(s, c->buf, c->len, 1) != 1)
			return;
		if (mnstr_write(s, ",", 1, 1) != 1)
			return;
		TABS(s, c->tabs - ((c->len + 3) / 8));
		if (mnstr_errnr(s))
			return;
		if (c->tabs * 8 >= c->len + 3 && mnstr_write(s, " ", 1, 1) != 1)
			return;
		if (mnstr_write(s, " ", 1, 1) != 1)
			return;
	}
	for (c = *l; *(++l); c = *l) {
		if (!c->format)
			continue;
		if (mnstr_write(s, c->buf, c->len, 1) != 1)
			return;
		if (mnstr_write(s, ",", 1, 1) != 1)
			return;
		TABS(s, c->tabs - ((c->len + 3) / 8));
		if (mnstr_errnr(s))
			return;
		if (c->tabs * 8 >= c->len + 3 && mnstr_write(s, " ", 1, 1) != 1)
			return;
		if (mnstr_write(s, " ", 1, 1) != 1)
			return;
	}
	if (mnstr_write(s, c->buf, c->len, 1) != 1)
		return;
	TABS(s, c->tabs - ((c->len + 2) / 8));
	if (mnstr_errnr(s))
		return;
	mnstr_printf(s, "  ]\n");
}

static void
print_format(col_format_t *c, const void *v)
{
	if (c->format)
		c->len = (*c->format) (&c->buf, &c->size, v);
}

static int
print_header(int argc, col_format_t *argv, stream *s)
{
	int k;
	str buf;
	int len;

	if (mnstr_write(s, "# ", 2, 1) != 1)
		return -1;
	for (k = argv[0].format ? 0 : 1; k <= argc; k++) {
		buf = argv[k].buf; /* contains column title */
		len = (int) strlen(buf);

		if (mnstr_write(s, buf, len, 1) != 1)
			return -1;
		TABS(s, argv[k].tabs - ((0 + len - 1) / 8));
		if (mnstr_errnr(s))
			return -1;
	}
	if (mnstr_printf(s, "  # name\n") < 0)
		return -1;
	if (mnstr_write(s, "# ", 2, 1) != 1)
		return -1;
	for (k = argv[0].format ? 0 : 1; k <= argc; k++) {
		buf = argv[k].tpe; /* contains column title */
		len = (int) strlen(buf);

		if (mnstr_write(s, buf, len, 1) != 1)
			return -1;
		TABS(s, argv[k].tabs - ((2 + len - 1) / 8));
		if (mnstr_errnr(s))
			return -1;
	}
	if (mnstr_printf(s, "  # type\n") < 0)
		return -1;
	return 0;
}

/*
 * The simple BAT printing routines make use of the complex case.
 */
int
BATprint(BAT *b)
{
	ERRORcheck(b == NULL, "BATprint: BAT expected");
	return BATmultiprintf(GDKstdout, 2, &b, TRUE, 0, 1);
}

int
BATprintf(stream *s, BAT *b)
{
	ERRORcheck(b == NULL, "BATprintf: BAT expected");
	return BATmultiprintf(s, 2, &b, TRUE, 0, 1);
}

/*
 * @+ Multi-Bat Printing
 * This routines uses the multi-join operation to print
 * an n-ary table. Such a table is the reconstruction of
 * the relational model from Monet's BATs, and consists of
 * all tail values of matching head-values in n-ary equijoin.
 */

int
BATmultiprintf(stream *s,	/* output stream */
	       int argc,	/* #ncolumns = #nbats +  */
	       BAT *argv[],	/* the bats 2b printed */
	       int printhead,	/* boolean: print the head column? */
	       int order,	/* respect order of bat X (X=0 is none) */
	       int printorder	/* boolean: print the orderby column? */
    )
{
	col_format_t *c = (col_format_t *) GDKzalloc((unsigned) (argc * sizeof(col_format_t)));
	col_format_t **cp = (col_format_t **) GDKmalloc((unsigned) ((argc + 1) * sizeof(void *)));
	ColFcn *value_fcn = (ColFcn *) GDKmalloc((unsigned) (argc * sizeof(ColFcn)));
	int ret = 0, j, total = 0;

	if (c == NULL)
		return -1;
	if (cp == NULL) {
		GDKfree(c);
		return -1;
	}
	if (value_fcn == NULL) {
		GDKfree(c);
		GDKfree(cp);
		return -1;
	}

	/*
	 * Init the column descriptor of the head column.
	 */
	cp[argc] = NULL;	/* terminator */
	cp[0] = c;
	argc--;

	/*
	 * Init the column descriptors of the tail columns.
	 */
	value_fcn[0] = (ColFcn) print_format;
	if (printhead) {
		BAT *b = BATmirror(argv[0]);

		total = c[0].tabs = print_tabwidth(b, b->tident, c + 0);
		c[0].format = printfcn(b);
	}
	for (j = 0; j < argc; j++, total += c[j].tabs) {
		cp[j + 1] = c + (j + 1);
		if (!printorder && order == j + 1)
			c[j + 1].format = NULL;
		else
			c[j + 1].format = printfcn(argv[j]);
		c[j + 1].tabs = print_tabwidth(argv[j], argv[j]->tident, c + (j + 1));
		value_fcn[j + 1] = (ColFcn) print_format;
	}
	total = 2 + (total * 8);
	/*
	 * Print the table header and then the multijoin.
	 */
	ret = -1;
	LINE(s, total);
	if (mnstr_errnr(s))
		goto cleanup;
	if (print_header(argc, c, s) < 0)
		goto cleanup;
	LINE(s, total);
	if (mnstr_errnr(s))
		goto cleanup;
	else if (argc == 1) {
		BAT *b = argv[0];
		BUN p, q;
		BATiter bi = bat_iterator(b);

		BATloop(b, p, q) {
			print_format(cp[0], BUNhead(bi, p));
			print_format(cp[1], BUNtail(bi, p));
			print_line(s, cp);
			if (mnstr_errnr(s))
				goto cleanup;
		}
		MULTIJOIN_LEAD(ret) = 1;
		MULTIJOIN_SORTED(ret) = BAThordered(b);
		MULTIJOIN_KEY(ret) = BAThkey(b);
		MULTIJOIN_SYNCED(ret) = 1;
	} else {
		ret = BATmultijoin(argc, argv, (RowFcn) print_line, (void *) s, value_fcn, (void **) cp, order);
	}
      /*
       * Cleanup.
       */
cleanup:
	for (j = 0; j <= argc; j++) {
		if (c[j].buf)
			GDKfree(c[j].buf);
	}
	GDKfree(c);
	GDKfree(cp);
	GDKfree(value_fcn);
	return ret;
}

