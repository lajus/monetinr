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
 * @f blob
 * @v 1.0
 * @a Wilko Quak, Peter Boncz, M. Kersten, N. Nes
 * @+ The blob data type
 * The datatype 'blob' introduced here illustrates the power
 * in the hands of a programmer to extend the functionality of the
 * Monet GDK library. It consists of an interface specification for
 * the necessary operators, a startup routine to register the
 * type in thekernel, and some additional operators used outside
 * the kernel itself.
 *
 * The 'blob' data type is used in many database engines to
 * store a variable sized atomary value.
 * Its definition forms a generic base to store arbitrary structures
 * in the database, without knowing its internal coding, layout,
 * or interpretation.
 *
 * The blob memory layout consists of first 4 bytes containing
 * the bytes-size of the blob (excluding the integer), and then just binary data.
 *
 * @+ Module Definition
 */
#include "monetdb_config.h"
#include "blob.h"

int TYPE_blob;
int TYPE_sqlblob;

blob_export str BLOBprelude(void);

blob_export int BLOBtostr(str *tostr, int *l, blob *pin);
blob_export int BLOBfromstr(char *instr, int *l, blob **val);
blob_export int BLOBnequal(blob *l, blob *r);
blob_export BUN BLOBhash(blob *b);
blob_export blob * BLOBnull(void);
blob_export void BLOBconvert(blob *b, int direction);
blob_export var_t BLOBput(Heap *h, var_t *bun, blob *val);
blob_export void BLOBdel(Heap *h, var_t *index);
blob_export int BLOBlength(blob *p);
blob_export void BLOBheap(Heap *heap, size_t capacity);
blob_export int SQLBLOBfromstr(char *instr, int *l, blob **val);
blob_export int SQLBLOBtostr(str *tostr, int *l, blob *pin);
blob_export str BLOBtoblob(blob **retval, str *s);
blob_export str BLOBfromblob(str *retval, blob **b);
blob_export str BLOBfromidx(str *retval, blob **binp, int *index);
blob_export str BLOBeoln(char *src, char *end);
blob_export int BLOBnitems(int *ret, blob *b);
blob_export int BLOBget(Heap *h, int *bun, int *l, blob **val);
blob_export blob * BLOBread(blob *a, stream *s, size_t cnt);
blob_export int BLOBwrite(blob *a, stream *s, size_t cnt);

blob_export str BLOBblob_blob(blob **d, blob **s);
blob_export str BLOBblob_fromstr(blob **b, str *d);

blob_export str BLOBsqlblob_fromstr(sqlblob **b, str *d);
blob_export str BLOB_isnil(bit *retval, blob *val);

str
BLOBprelude(void)
{
	TYPE_blob = ATOMindex("blob");
	TYPE_sqlblob = ATOMindex("sqlblob");
	return MAL_SUCCEED;
}

var_t
blobsize(size_t nitems)
{
	if (nitems == ~(size_t) 0)
		nitems = 0;
	assert(sizeof(size_t) + nitems <= VAR_MAX);
	return (var_t) (sizeof(size_t) + nitems);
}

static var_t
blob_put(Heap *h, var_t *bun, blob *val)
{
	char *base = NULL;

	*bun = HEAP_malloc(h, blobsize(val->nitems));
 	base = h->base;
	if (*bun)
		memcpy(&base[*bun << GDK_VARSHIFT], (char *) val, blobsize(val->nitems));
	return *bun;
}

#if 0
int
blob_get(Heap *h, int *bun, int *l, blob **val)
{
	blob *from = HEAP_index(h, *bun, blob);
	var_t size = blobsize(from->nitems);

	if (*val == NULL) {
		*val = (blob *) GDKmalloc(size);
		*l = size;
	} else if (*l < size) {
		GDKfree(*val);
		*val = (blob *) GDKmalloc(size);
		*l = size;
	}
	memcpy(*val, from, size);

	return size;
}
#endif

static int
blob_nequal(blob *l, blob *r)
{
	size_t len = l->nitems;

	if (len != r->nitems)
		return (1);

	if (len == ~(size_t) 0)
		return (0);

	return memcmp(l->data, r->data, len) != 0;
}

static void
blob_del(Heap *h, var_t *idx)
{
	HEAP_free(h, *idx);
}

static BUN
blob_hash(blob *b)
{
	return (BUN) b->nitems;
}

static blob *
blob_null(void)
{
	static blob nullval;

	nullval.nitems = ~(size_t) 0;
	return (&nullval);
}

static blob *
blob_read(blob *a, stream *s, size_t cnt)
{
	int len;

	(void) cnt;
	assert(cnt == 1);
	if (!mnstr_readInt(s, &len))
		return NULL;
	if ((a = GDKmalloc(len)) == NULL)
		return NULL;
	if (mnstr_read(s, (char *) a, len, 1) != 1) {
		GDKfree(a);
		return NULL;
	}
	return a;
}

static int
blob_write(blob *a, stream *s, size_t cnt)
{
	var_t len = blobsize(a->nitems);

	(void) cnt;
	assert(cnt == 1);
	if (mnstr_writeInt(s, (int) len)) { /* 64bit: check for overflow */
		mnstr_write(s, (char *) a, len, 1);
		return GDK_SUCCEED;
	}
	return GDK_FAIL;
}

#if SIZEOF_SIZE_T == SIZEOF_INT
#define normal_vart_SWAP(x)	((var_t) normal_int_SWAP((int)x))
#else
#define normal_vart_SWAP(x)	((var_t) long_long_SWAP((lng)x))
#endif

static void
blob_convert(blob *b, int direction)
{
	(void) direction;
	b->nitems = normal_vart_SWAP(b->nitems);
}

static int
blob_length(blob *p)
{
	var_t l = blobsize(p->nitems); /* 64bit: check for overflow */
	assert(l <= GDK_int_max);
	return (int) l; /* 64bit: check for overflow */
}


static void
blob_heap(Heap *heap, size_t capacity)
{
	HEAP_initialize(heap, capacity, 0, (int) sizeof(var_t));
}

#ifndef OLDSTYLE
static char hexit[] = "0123456789ABCDEF";

static int
blob_tostr(str *tostr, int *l, blob *p)
{
	char *s;
	size_t i;
	size_t expectedlen;

	if (p->nitems == ~(size_t) 0)
		expectedlen = 4;
	else
		expectedlen = 24 + (p->nitems * 3);
	if (*l < 0 || (size_t) * l < expectedlen) {
		if (*tostr != NULL)
			GDKfree(*tostr);
		*tostr = (str) GDKmalloc(expectedlen);
		*l = (int) expectedlen;
	}
	if (p->nitems == ~(size_t) 0) {
		strcpy(*tostr, "nil");
		return 3;
	}

	sprintf(*tostr, "(" SZFMT ":", p->nitems);
	s = *tostr + strlen(*tostr);

	for (i = 0; i < p->nitems; i++) {
		int val = (p->data[i] >> 4) & 15;

		*s++ = ' ';
		*s++ = hexit[val];
		val = p->data[i] & 15;
		*s++ = hexit[val];
	}
	*s++ = ')';
	*s = '\0';
	return (int) (s - *tostr); /* 64bit: check for overflow */
}

/* SQL 99 compatible BLOB output string
 * differs from the MonetDB BLOB output in that it does not start with a size
 * no brackets and no spaces in between the hexits
 */
int
sqlblob_tostr(str *tostr, int *l, blob *p)
{
	char *s;
	size_t i;
	size_t expectedlen;

	if (p->nitems == ~(size_t) 0)
		expectedlen = 4;
	else
		expectedlen = 24 + (p->nitems * 3);
	if (*l < 0 || (size_t) * l < expectedlen) {
		if (*tostr != NULL)
			GDKfree(*tostr);
		*tostr = (str) GDKmalloc(expectedlen);
		*l = (int) expectedlen;
	}
	if (p->nitems == ~(size_t) 0) {
		strcpy(*tostr, "nil");
		return 3;
	}

	strcpy(*tostr, "\0");
	s = *tostr;

	for (i = 0; i < p->nitems; i++) {
		int val = (p->data[i] >> 4) & 15;

		*s++ = hexit[val];
		val = p->data[i] & 15;
		*s++ = hexit[val];
	}
	*s = '\0';
	return (int) (s - *tostr); /* 64bit: check for overflow */
}

static int
blob_fromstr(char *instr, int *l, blob **val)
{
	size_t i;
	size_t nitems;
	var_t nbytes;
	blob *result;
	char *s = instr;

	s = strchr(s, '(');
	if (s == NULL) {
		GDKerror("Missing ( in blob\n");
		*val = (blob *) NULL;
		return (0);
	}
	nitems = (size_t) strtoul(s + 1, &s, 10);
	if (s == NULL) {
		GDKerror("Missing nitems in blob\n");
		*val = (blob *) NULL;
		return (0);
	}
#if SIZEOF_SIZE_T > SIZEOF_INT
	if (nitems > 0x7fffffff) {
		GDKerror("Blob too large\n");
		*val = (blob *) NULL;
		return (0);
	}
#endif
	nbytes = blobsize(nitems);
	s = strchr(s, ':');
	if (s == NULL) {
		GDKerror("Missing ':' in blob\n");
		*val = (blob *) NULL;
		return (0);
	}
	++s;

	if (*val == (blob *) NULL) {
		*val = (blob *) GDKmalloc(nbytes);
		*l = (int) nbytes;
	} else if (*l < 0 || (size_t) * l < nbytes) {
		GDKfree(*val);
		*val = (blob *) GDKmalloc(nbytes);
		*l = (int) nbytes;
	}
	result = *val;
	result->nitems = nitems;

	/*
	   // Read the values of the blob.
	 */
	for (i = 0; i < nitems; ++i) {
		char res = 0;

		if (*s == ' ')
			s++;

		if (*s >= '0' && *s <= '9') {
			res = *s - '0';
		} else if (*s >= 'A' && *s <= 'F') {
			res = 10 + *s - 'A';
		} else if (*s >= 'a' && *s <= 'f') {
			res = 10 + *s - 'a';
		} else {
			break;
		}
		s++;
		res <<= 4;
		if (*s >= '0' && *s <= '9') {
			res += *s - '0';
		} else if (*s >= 'A' && *s <= 'F') {
			res += 10 + *s - 'A';
		} else if (*s >= 'a' && *s <= 'f') {
			res += 10 + *s - 'a';
		} else {
			break;
		}
		s++;		/* skip space */

		result->data[i] = res;
	}

	if (i < nitems) {
		GDKerror("blob_fromstr: blob too short \n");
		return -1;
	}

	s = strchr(s, ')');
	if (s == 0) {
		GDKerror("blob_fromstr: Missing ')' in blob\n");
	}

	return (int) (s - instr);
}

/* SQL 99 compatible BLOB input string
 * differs from the MonetDB BLOB input in that it does not start with a size
 * no brackets and no spaces in between the hexits
 */
int
sqlblob_fromstr(char *instr, int *l, blob **val)
{
	size_t i;
	size_t nitems;
	var_t nbytes;
	blob *result;
	char *s = instr;
	int nil = 0;

	/* since the string is built of (only) hexits the number of bytes
	 * required for it is the length of the string divided by two
	 */
	i = strlen(instr);
	if (i % 2 == 1) {
		GDKerror("sqlblob_fromstr: Illegal blob length '" SZFMT "' (should be even)\n", i);
		instr = 0;
		nil = 1;
	}
	nitems = i / 2;
	nbytes = blobsize(nitems);

	if (*val == (blob *) NULL) {
		*val = (blob *) GDKmalloc(nbytes);
		*l = (int) nbytes;
	} else if (*l < 0 || (size_t) * l < nbytes) {
		GDKfree(*val);
		*val = (blob *) GDKmalloc(nbytes);
		*l = (int) nbytes;
	}
	if (nil) {
		**val = *blob_null();
		return 0;
	}
	result = *val;
	result->nitems = nitems;

	/*
	   // Read the values of the blob.
	 */
	for (i = 0; i < nitems; ++i) {
		char res = 0;

		if (*s >= '0' && *s <= '9') {
			res = *s - '0';
		} else if (*s >= 'A' && *s <= 'F') {
			res = 10 + *s - 'A';
		} else if (*s >= 'a' && *s <= 'f') {
			res = 10 + *s - 'a';
		} else {
			GDKerror("sqlblob_fromstr: Illegal char '%c' in blob\n", *s);
		}
		s++;
		res <<= 4;
		if (*s >= '0' && *s <= '9') {
			res += *s - '0';
		} else if (*s >= 'A' && *s <= 'F') {
			res += 10 + *s - 'A';
		} else if (*s >= 'a' && *s <= 'f') {
			res += 10 + *s - 'a';
		} else {
			GDKerror("sqlblob_fromstr: Illegal char '%c' in blob\n", *s);
		}
		s++;

		result->data[i] = res;
	}

	return (int) (s - instr);
}

#else

/* the code in this branch of the #if is not being maintained */

#define MAXCHAR 127
#define LINE.LEN 80
#define CODEDLN 61
#define NORM.LEN 45

char blob_chtbl[MAXCHAR] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 63,
	64, 65, 66, 67, 68, 69, 70, 71,
	72, 73, 74, 75, 76, 77, 78, 79,
	80, 81, 82, 83, 84, 85, 86, 87,
	88, 89, 90, 91, 92, 93, 94, 95,
	32, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 94
};

#define ENC(c) (((c) & 077) + ' ')

char *
blob_outdec(char *p, char *dst)
{
	int c1, c2, c3, c4;

	c1 = *p >> 2;
	c2 = (*p << 4) & 060 | (p[1] >> 4) & 017;
	c3 = (p[1] << 2) & 074 | (p[2] >> 6) & 03;
	c4 = p[2] & 077;
	dst[0] = ENC(c1);
	dst[1] = ENC(c1);
	dst[2] = ENC(c1);
	dst[3] = ENC(c1);
	return dst + 4;
}

int
blob_tostr(str *dst, int *size, blob *src)
{
	int i, n, len = *(int *) src;
	int memsize = 4 + (len * 4) / 3;
	char *out, *end;

	/*
	 * @-
	 * correct memsize for first and last chars of each line
	 */
	memsize += 1 + 2 * (memsize / CODEDLN);

	if (*dst == 0) {
		*dst = (char *) GDKmalloc(*size = memsize);
	} else if (*size < memsize) {
		GDKfree(*dst);
		*dst = (char *) GDKmalloc(*size = memsize);
	}
	if (len == -1) {
		strcpy(*dst, "nil");
		return 3;
	}
	src += sizeof(int);
	end = ((char *) src) + len;

	for (out = *dst; ((char *) src) < end; src += n) {
		n = MIN(end, src + 45) - src;
		*out++ = ENC(n);
		for (i = 0; i < n; i += 3)
			out = blob_outdec(&((char *) src)[i], out);
		*out++ = '\n';
	}
	if (out > *dst)
		out--;
	*out = 0;
	return out - *dst;
}

str
blob_eoln(char *src, char *end)
{
	char *r = strchr(src, '\n');

	if (r)
		return r;
	return end;
}

static int
blob_fromstr(char *src, int *size, blob **dst)
{
	int l = strlen(src), memsize = sizeof(int) + 1 + (l * 3) / 4;
	char *ut, *r, *end = src + l;

	if (!*dst) {
		*dst = (blob *) GDKmalloc(*size = memsize);
	} else if (*size < memsize) {
		GDKfree(*dst);
		*dst = (blob *) GDKmalloc(*size = memsize);
	}
	ut = (char *) *dst + sizeof(int);

	for (r = blob_eoln(src, end); src < end; src = r, r = blob_eoln(src, end)) {
		unsigned int n, c, len = r - src;
		char buf[LINELEN], *bp;

		/*
		 * @-
		 * PETER: this code wanted to modify the source line. So we copy it.
		 */
		memcpy(buf, src, len);
		buf[--len] = '\0';
		/*
		 * @-
		 * Get the binary line length.
		 */
		n = blob_chtbl[*buf];
		if (n == NORMLEN)
			goto decod;
	/*
	 * @-
	 * Pad with blanks.
	 */
	decod:for (bp = &buf[c = len]; c < CODEDLN; c++, bp++)
		*bp = ' ';
		/*
		 * @-
		 * Output a group of 3 bytes (4 input characters).  The input chars are pointed
		 * to by p, they are to be output to file f.  n is used to tell us not to
		 * output all of them at the end of the file.
		 */
		bp = &buf[1];
		while (n > 0) {
			*(ut++) = blob_chtbl[*bp] << 2 | blob_chtbl[bp[1]] >> 4;
			n--;
			if (n) {
				*(ut++) = blob_chtbl[bp[1]] << 4 | blob_chtbl[bp[2]] >> 2;
				n--;
			}
			if (n) {
				*(ut++) = blob_chtbl[bp[2]] << 6 | blob_chtbl[bp[3]];
				n--;
			}
			bp += 4;
		}
	}
	/*
	 * @-
	 * PETER: set the blob size.
	 */
	*(int *) *dst = ut - ((char *) *dst + sizeof(int));
	return l;
}
#endif /* OLDSTYLE */

static int
fromblob_idx(str *retval, blob *b, int *idx)
{
	str s, p = b->data + *idx;
	str r, q = b->data + b->nitems;

	for (r = p; r < q; r++) {
		if (*r == 0)
			break;
	}
	*retval = s = (str) GDKmalloc(1 + r - p);
	for (; p < r; p++, s++)
		*s = *p;
	*s = 0;
	return GDK_SUCCEED;
}

static int
fromblob(str *retval, blob *b)
{
	int zero = 0;

	return fromblob_idx(retval, b, &zero);
}

static int
toblob(blob **retval, str s)
{
	int len = strLen(s);
	blob *b = (blob *) GDKmalloc(blobsize(len));

	b->nitems = len;
	memcpy(b->data, s, len);
	*retval = b;
	return GDK_SUCCEED;
}

#if 0
static int
blob_nitems(int *ret, blob *b)
{
	*ret = (int) b->nitems;
	return GDK_SUCCEED;
}
#endif

/*
 * @- Wrapping section
 * This section contains the wrappers to re-use the implementation
 * section of the blob modules from MonetDB 4.3
 * @-
 */
int
BLOBnequal(blob *l, blob *r)
{
	return blob_nequal(l, r);
}

void
BLOBdel(Heap *h, var_t *idx)
{
	blob_del(h, idx);
}

BUN
BLOBhash(blob *b)
{
	return blob_hash(b);
}

blob *
BLOBnull(void)
{
	blob *b= (blob*) GDKmalloc(sizeof(blob));
	b->nitems = ~(size_t) 0;
	return b;
}

blob *
BLOBread(blob *a, stream *s, size_t cnt)
{
	return blob_read(a,s,cnt);
}

int
BLOBwrite(blob *a, stream *s, size_t cnt)
{
	return blob_write(a,s,cnt);
}

void
BLOBconvert(blob *b, int direction)
{
	blob_convert(b, direction);
}

int
BLOBlength(blob *p)
{
	return blob_length(p);
}

void
BLOBheap(Heap *heap, size_t capacity)
{
	blob_heap(heap, capacity);
}

var_t
BLOBput(Heap *h, var_t *bun, blob *val)
{
	return blob_put(h, bun, val);
}

#if 0
int
BLOBget(Heap *h, int *bun, int *l, blob **val)
{
	return blob_get(h, bun, l, val);
}
#endif
int
BLOBnitems(int *ret, blob *b)
{
	assert(b->nitems <INT_MAX);
	*ret = (int) b->nitems;
	return GDK_SUCCEED;
}

#ifndef OLDSTYLE
int
BLOBtostr(str *tostr, int *l, blob *pin)
{
	return blob_tostr(tostr, l, pin);
}

int
BLOBfromstr(char *instr, int *l, blob **val)
{
	return blob_fromstr(instr, l, val);
}
#else
int
BLOBtostr(str *dst, int *size, blob *srcin)
{
	return blob_tostr(dst, size, srcin);
}

str
BLOBeoln(char *src, char *end)
{
	return blob_eoln(src, end)
}
int
BLOBfromstr(char *instr, int *l, blob **val)
{
	return blob_fromstr(instr, l, val);
}
#endif /* OLDSTYLE */

str
BLOBfromidx(str *retval, blob **binp, int *idx)
{
	fromblob_idx(retval, *binp, idx);
	return MAL_SUCCEED;
}

str
BLOBfromblob(str *retval, blob **b)
{
	fromblob(retval, *b);
	return MAL_SUCCEED;
}

str
BLOBtoblob(blob **retval, str *s)
{
	toblob(retval, *s);
	return MAL_SUCCEED;
}

int
SQLBLOBtostr(str *retval, int *l, blob *b)
{
	return sqlblob_tostr(retval, l, b);
}

int
SQLBLOBfromstr(char *s, int *l, blob **val)
{
	return sqlblob_fromstr(s, l, val);
}

str
BLOBblob_blob(blob **d, blob **s)
{
	size_t len = blobsize((*s)->nitems);
	blob *b;

	if( (*s)->nitems == ~(size_t) 0){
		*d= BLOBnull();
	} else {
		*d= b= (blob *) GDKmalloc(len);
		b->nitems = len;
		memcpy(b->data, (*s)->data, len);
	}
	return MAL_SUCCEED;
}

str
BLOBblob_fromstr(blob **b, str *s)
{
	int len = 0;

	blob_fromstr(*s, &len, b);
	return MAL_SUCCEED;
}


str
BLOBsqlblob_fromstr(blob **b, str *s)
{
	int len = 0;

	sqlblob_fromstr(*s, &len, b);
	return MAL_SUCCEED;
}

str BLOB_isnil(bit *ret, blob *v)
{
	*ret = (v->nitems == ~(size_t)0);
	return MAL_SUCCEED;
}

