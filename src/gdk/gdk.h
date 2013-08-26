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
 * @t The Goblin Database Kernel
 * @v Version 3.05
 * @a Martin L. Kersten, Peter Boncz, Niels Nes
 *
 * @+ The Inner Core
 * The innermost library of the MonetDB database system is formed by
 * the library called GDK, an abbreviation of Goblin Database Kernel.
 * Its development was originally rooted in the design of a pure
 * active-object-oriented programming language, before development
 * was shifted towards a re-usable database kernel engine.
 *
 * GDK is a C library that provides ACID properties on a DSM model
 * @tex
 * [@cite{Copeland85}]
 * @end tex
 * , using main-memory
 * database algorithms
 * @tex
 * [@cite{Garcia-Molina92}]
 * @end tex
 *  built on virtual-memory
 * OS primitives and multi-threaded parallelism.
 * Its implementation has undergone various changes over its decade
 * of development, many of which were driven by external needs to
 * obtain a robust and fast database system.
 *
 * The coding scheme explored in GDK has also laid a foundation to
 * communicate over time experiences and to provide (hopefully)
 * helpful advice near to the place where the code-reader needs it.
 * Of course, over such a long time the documentation diverges from
 * reality. Especially in areas where the environment of this package
 * is being described.
 * Consider such deviations as historic landmarks, e.g. crystallization
 * of brave ideas and mistakes rectified at a later stage.
 *
 * @+ Short Outline
 * The facilities provided in this implementation are:
 * @itemize
 * @item
 * GDK or Goblin Database Kernel routines for session management
 * @item
 *  BAT routines that define the primitive operations on the
 * database tables (BATs).
 * @item
 *  BBP routines to manage the BAT Buffer Pool (BBP).
 * @item
 *  ATOM routines to manipulate primitive types, define new types
 * using an ADT interface.
 * @item
 *  HEAP routines for manipulating heaps: linear spaces of memory
 * that are GDK's vehicle of mass storage (on which BATs are built).
 * @item
 *  DELTA routines to access inserted/deleted elements within a
 * transaction.
 * @item
 *  HASH routines for manipulating GDK's built-in linear-chained
 * hash tables, for accelerating lookup searches on BATs.
 * @item
 *  TM routines that provide basic transaction management primitives.
 * @item
 *  TRG routines that provided active database support. [DEPRECATED]
 * @item
 *  ALIGN routines that implement BAT alignment management.
 * @end itemize
 *
 * The Binary Association Table (BAT) is the lowest level of storage
 * considered in the Goblin runtime system
 * @tex
 * [@cite{Goblin}]
 * @end tex
 * .  A BAT is a
 * self-descriptive main-memory structure that represents the
 * @strong{binary relationship} between two atomic types.  The
 * association can be defined over:
 * @table @code
 * @item void:
 *  virtual-OIDs: a densely ascending column of OIDs (takes zero-storage).
 * @item bit:
 *  Booleans, implemented as one byte values.
 * @item bte:
 *  Tiny (1-byte) integers (8-bit @strong{integer}s).
 * @item sht:
 *  Short integers (16-bit @strong{integer}s).
 * @item int:
 *  This is the C @strong{int} type (32-bit).
 * @item oid:
 *  Unique @strong{long int} values uses as object identifier. Highest
 *	    bit cleared always.  Thus, oids-s are 31-bit numbers on
 *	    32-bit systems, and 63-bit numbers on 64-bit systems.
 * @item wrd:
 *  Machine-word sized integers
 *  (32-bit on 32-bit systems, 64-bit on 64-bit systems).
 * @item ptr:
 * Memory pointer values. DEPRECATED.  Can only be stored in transient
 * BATs.
 * @item flt:
 *  The IEEE @strong{float} type.
 * @item dbl:
 *  The IEEE @strong{double} type.
 * @item lng:
 *  Longs: the C @strong{long long} type (64-bit integers).
 * @item str:
 *  UTF-8 strings (Unicode). A zero-terminated byte sequence.
 * @item bat:
 *  Bat descriptor. This allows for recursive administered tables, but
 *  severely complicates transaction management. Therefore, they CAN
 *  ONLY BE STORED IN TRANSIENT BATs.
 * @end table
 *
 * This model can be used as a back-end model underlying other -higher
 * level- models, in order to achieve @strong{better performance} and
 * @strong{data independence} in one go. The relational model and the
 * object-oriented model can be mapped on BATs by vertically splitting
 * every table (or class) for each attribute. Each such a column is
 * then stored in a BAT with type @strong{bat[oid,attribute]}, where
 * the unique object identifiers link tuples in the different BATs.
 * Relationship attributes in the object-oriented model hence are
 * mapped to @strong{bat[oid,oid]} tables, being equivalent to the
 * concept of @emph{join indexes} @tex [@cite{Valduriez87}] @end tex .
 *
 * The set of built-in types can be extended with user-defined types
 * through an ADT interface.  They are linked with the kernel to
 * obtain an enhanced library, or they are dynamically loaded upon
 * request.
 *
 * Types can be derived from other types. They represent something
 * different than that from which they are derived, but their internal
 * storage management is equal. This feature facilitates the work of
 * extension programmers, by enabling reuse of implementation code,
 * but is also used to keep the GDK code portable from 32-bits to
 * 64-bits machines: the @strong{oid} and @strong{ptr} types are
 * derived from @strong{int} on 32-bits machines, but is derived from
 * @strong{lng} on 64 bits machines. This requires changes in only two
 * lines of code each.
 *
 * To accelerate lookup and search in BATs, GDK supports one built-in
 * search accelerator: hash tables. We choose an implementation
 * efficient for main-memory: bucket chained hash
 * @tex
 * [@cite{LehCar86,Analyti92}]
 * @end tex
 * . Alternatively, when the table is sorted, it will resort to
 * merge-scan operations or binary lookups.
 *
 * BATs are built on the concept of heaps, which are large pieces of
 * main memory. They can also consist of virtual memory, in case the
 * working set exceeds main-memory. In this case, GDK supports
 * operations that cluster the heaps of a BAT, in order to improve
 * performance of its main-memory.
 *
 *
 * @- Rationale
 * The rationale for choosing a BAT as the building block for both
 * relational and object-oriented system is based on the following
 * observations:
 *
 * @itemize
 * @item -
 * Given the fact that CPU speed and main-memory increase in current
 * workstation hardware for the last years has been exceeding IO
 * access speed increase, traditional disk-page oriented algorithms do
 * no longer take best advantage of hardware, in most database
 * operations.
 *
 * Instead of having a disk-block oriented kernel with a large memory
 * cache, we choose to build a main-memory kernel, that only under
 * large data volumes slowly degrades to IO-bound performance,
 * comparable to traditional systems
 * @tex
 * [@cite{boncz95,boncz96}]
 * @end tex
 * .
 *
 * @item -
 * Traditional (disk-based) relational systems move too much data
 * around to save on (main-memory) join operations.
 *
 * The fully decomposed store (DSM
 * @tex
 * [@cite{Copeland85})]
 * @end tex
 * assures that only those attributes of a relation that are needed,
 * will have to be accessed.
 *
 * @item -
 * The data management issues for a binary association is much
 * easier to deal with than traditional @emph{struct}-based approaches
 * encountered in relational systems.
 *
 * @item -
 * Object-oriented systems often maintain a double cache, one with the
 * disk-based representation and a C pointer-based main-memory
 * structure.  This causes expensive conversions and replicated
 * storage management.  GDK does not do such `pointer swizzling'. It
 * used virtual-memory (@strong{mmap()}) and buffer management advice
 * (@strong{madvise()}) OS primitives to cache only once. Tables take
 * the same form in memory as on disk, making the use of this
 * technique transparent
 * @tex
 * [@cite{oo7}]
 * @end tex
 * .
 * @end itemize
 *
 * A RDBMS or OODBMS based on BATs strongly depends on our ability to
 * efficiently support tuples and to handle small joins, respectively.
 *
 * The remainder of this document describes the Goblin Database kernel
 * implementation at greater detail. It is organized as follows:
 * @table @code
 * @item @strong{GDK Interface}:
 *
 * It describes the global interface with which GDK sessions can be
 * started and ended, and environment variables used.
 *
 * @item @strong{Binary Association Tables}:
 *
 * As already mentioned, these are the primary data structure of GDK.
 * This chapter describes the kernel operations for creation,
 * destruction and basic manipulation of BATs and BUNs (i.e. tuples:
 * Binary UNits).
 *
 * @item @strong{BAT Buffer Pool:}
 *
 * All BATs are registered in the BAT Buffer Pool. This directory is
 * used to guide swapping in and out of BATs. Here we find routines
 * that guide this swapping process.
 *
 * @item @strong{GDK Extensibility:}
 *
 * Atoms can be defined using a unified ADT interface.  There is also
 * an interface to extend the GDK library with dynamically linked
 * object code.
 *
 * @item @strong{GDK Utilities:}
 *
 * Memory allocation and error handling primitives are
 * provided. Layers built on top of GDK should use them, for proper
 * system monitoring.  Thread management is also included here.
 *
 * @item @strong{Transaction Management:}
 *
 * For the time being, we just provide BAT-grained concurrency and
 * global transactions. Work is needed here.
 *
 * @item @strong{BAT Alignment:}
 * Due to the mapping of multi-ary datamodels onto the BAT model, we
 * expect many correspondences among BATs, e.g.
 * @emph{bat(oid,attr1),..  bat(oid,attrN)} vertical
 * decompositions. Frequent activities will be to jump from one
 * attribute to the other (`bunhopping'). If the head columns are
 * equal lists in two BATs, merge or even array lookups can be used
 * instead of hash lookups. The alignment interface makes these
 * relations explicitly manageable.
 *
 * In GDK, complex data models are mapped with DSM on binary tables.
 * Usually, one decomposes @emph{N}-ary relations into @emph{N} BATs
 * with an @strong{oid} in the head column, and the attribute in the
 * tail column.  There may well be groups of tables that have the same
 * sets of @strong{oid}s, equally ordered. The alignment interface is
 * intended to make this explicit.  Implementations can use this
 * interface to detect this situation, and use cheaper algorithms
 * (like merge-join, or even array lookup) instead.
 *
 * @item @strong{BAT Iterators:}
 *
 * Iterators are C macros that generally encapsulate a complex
 * for-loop.  They would be the equivalent of cursors in the SQL
 * model. The macro interface (instead of a function call interface)
 * is chosen to achieve speed when iterating main-memory tables.
 *
 * @item @strong{Common BAT Operations:}
 *
 * These are much used operations on BATs, such as aggregate functions
 * and relational operators. They are implemented in terms of BAT- and
 * BUN-manipulation GDK primitives.
 * @end table
 *
 * @+ Interface Files
 * In this section we summarize the user interface to the GDK library.
 * It consist of a header file (gdk.h) and an object library
 * (gdklib.a), which implements the required functionality. The header
 * file must be included in any program that uses the library. The
 * library must be linked with such a program.
 *
 * @- Database Context
 *
 * The MonetDB environment settings are collected in a configuration
 * file. Amongst others it contains the location of the database
 * directory.  First, the database directory is closed for other
 * servers running at the same time.  Second, performance enhancements
 * may take effect, such as locking the code into memory (if the OS
 * permits) and preloading the data dictionary.  An error at this
 * stage normally lead to an abort.
 */

#ifndef _GDK_H_
#define _GDK_H_

/* standard includes upon which all configure tests depend */
#include <stdio.h>
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_STRING_H
# if !defined(STDC_HEADERS) && defined(HAVE_MEMORY_H)
#  include <memory.h>
# endif
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#else
# ifdef HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <ctype.h>		/* isspace etc. */

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>		/* MAXPATHLEN */
#endif

#ifdef HAVE_DIRENT_H
# include <dirent.h>
#else
# define dirent direct
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include <limits.h>		/* for *_MIN and *_MAX */
#include <float.h>		/* for FLT_MAX and DBL_MAX */
#ifndef LLONG_MAX
#ifdef LONGLONG_MAX
#define LLONG_MAX LONGLONG_MAX
#define LLONG_MIN LONGLONG_MIN
#else
#define LLONG_MAX LL_CONSTANT(9223372036854775807)
#define LLONG_MIN (-LL_CONSTANT(9223372036854775807) - LL_CONSTANT(1))
#endif
#endif

#include "gdk_system.h"
#include "gdk_posix.h"
#include <stream.h>

#undef MIN
#undef MAX
#define MAX(A,B)	((A)<(B)?(B):(A))
#define MIN(A,B)	((A)>(B)?(B):(A))

/* defines from ctype with casts that allow passing char values */
#define GDKisprint(c)	isprint((int) (unsigned char) (c))
#define GDKisspace(c)	isspace((int) (unsigned char) (c))
#define GDKisalnum(c)	isalnum((int) (unsigned char) (c))
#define GDKisgraph(c)	isgraph((int) (unsigned char) (c))
#define GDKisdigit(c)	(((unsigned char) (c)) >= '0' && ((unsigned char) (c)) <= '9')
#define GDKisxcntrl(c)  (((unsigned char) (c)) >= 128 && ((unsigned char) (c)) <= 160)
#define GDKisspecial(c) (((unsigned char) (c)) >= 161 && ((unsigned char) (c)) <= 191)
#define GDKisupperl(c)  (((unsigned char) (c)) >= 192 && ((unsigned char) (c)) <= 223)
#define GDKislowerl(c)  (((unsigned char) (c)) >= 224 && ((unsigned char) (c)) <= 255)

#define GDKPROP		6	/* use one spare! */
#define MONETHOME	"MONETHOME"
#ifndef NATIVE_WIN32
#define BATDIR		"bat"
#define DELDIR		"bat/DELETE_ME"
#define BAKDIR		"bat/BACKUP"
#define SUBDIR		"bat/BACKUP/SUBCOMMIT"
#define LEFTDIR		"bat/LEFTOVERS"
#define HCDIR		"bat/HC"
#else
#define BATDIR		"bat"
#define DELDIR		"bat\\DELETE_ME"
#define BAKDIR		"bat\\BACKUP"
#define SUBDIR		"bat\\BACKUP\\SUBCOMMIT"
#define LEFTDIR		"bat\\LEFTOVERS"
#define HCDIR		"bat\\HC"
#endif

#ifdef MAXPATHLEN
#define PATHLENGTH	MAXPATHLEN
#else
#define PATHLENGTH	1024	/* maximum file pathname length */
#endif

/*
 * @- GDK session handling
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab GDKinit (char *db, char *dbpath, int allocmap)
 * @item int
 * @tab GDKexit (int status)
 * @end multitable
 *
 * The session is bracketed by GDKinit and GDKexit. Initialization
 * involves setting up the administration for database access, such as
 * memory allocation for the database buffer pool.  During the exit
 * phase any pending transaction is aborted and the database is freed
 * for access by other users.  A zero is returned upon encountering an
 * erroneous situation.
 *
 * @- Definitions
 * The interface definitions for the application programs are shown
 * below.  The global variables should not be modified directly.
 */
#define NEG(A)	(((int)(A))>0?-((int)(A)):((int)(A)))
#define ABS(A)	(((int)(A))>0?((int)(A)):-((int)(A)))

#ifndef TRUE
#define TRUE		1
#define FALSE		0
#endif
#define BOUND2BTRUE	2	/* TRUE, and bound to be so */

#define IDLENGTH	64	/* maximum BAT id length */
#define BATMARGIN	1.2	/* extra free margin for new heaps */
#define BATTINY_BITS	8
#define BATTINY		((BUN)1<<BATTINY_BITS)	/* minimum allocation buncnt for a BAT */

#define TYPE_void	0
#define TYPE_bit	1
#define TYPE_bte	2
#define TYPE_sht	3
#define TYPE_bat	4	/* BAT id: index in BBPcache */
#define TYPE_int	5
#define TYPE_oid	6
#define TYPE_wrd	7
#define TYPE_ptr	8	/* C pointer! */
#define TYPE_flt	9
#define TYPE_dbl	10
#define TYPE_lng	11
#define TYPE_str	12
#define TYPE_any	255	/* limit types to <255! */

typedef signed char bit;
typedef signed char bte;
typedef short sht;

#ifdef MONET_OID32
#define SIZEOF_OID	SIZEOF_INT
typedef unsigned int oid;
#else
#define SIZEOF_OID	SIZEOF_SIZE_T
typedef size_t oid;
#endif
#if SIZEOF_OID == SIZEOF_SIZE_T
#define OIDFMT		SZFMT
#else
#if SIZEOF_OID == SIZEOF_INT
#define OIDFMT		"%u"
#else
#define OIDFMT		ULLFMT
#endif
#endif

#define SIZEOF_WRD	SIZEOF_SSIZE_T
typedef ssize_t wrd;
typedef int bat;		/* Index into BBP */
typedef void *ptr;		/* Internal coding of types */

#define SIZEOF_PTR	SIZEOF_VOID_P
typedef float flt;
typedef double dbl;
typedef char *str;

#if SIZEOF_INT==8
#	define LL_CONSTANT(val)	(val)
#elif SIZEOF_LONG==8
#	define LL_CONSTANT(val)	(val##L)
#elif defined(HAVE_LONG_LONG)
#	define LL_CONSTANT(val)	(val##LL)
#elif defined(HAVE___INT64)
#	define LL_CONSTANT(val)	(val##i64)
#endif

typedef char long_str[IDLENGTH];	/* standard GDK static string */

typedef oid var_t;		/* type used for heap index of var-sized BAT */
#define SIZEOF_VAR_T	SIZEOF_OID
#define VARFMT		OIDFMT

#if SIZEOF_VAR_T == SIZEOF_INT	/* a type compatible with var_t */
#define TYPE_var	TYPE_int
#define VAR_MAX		((var_t) INT_MAX)
#else
#define TYPE_var	TYPE_lng
#define VAR_MAX		((var_t) LLONG_MAX)
#endif

typedef oid BUN;		/* BUN position */
#define SIZEOF_BUN	SIZEOF_OID
#define BUNFMT		OIDFMT
/* alternatively:
typedef size_t BUN;
#define SIZEOF_BUN	SIZEOF_SIZE_T
#define BUNFMT		SZFMT
*/
#if SIZEOF_BUN == SIZEOF_INT
#define BUN_NONE ((BUN) INT_MAX)
#else
#define BUN_NONE ((BUN) LLONG_MAX)
#endif
#define BUN_MAX (BUN_NONE - 1)	/* maximum allowed size of a BAT */

#define BUN1 1
#define BUN2 2
#define BUN4 4
#if SIZEOF_BUN > 4
#define BUN8 8
#endif
typedef uint8_t  BUN1type;
typedef uint16_t BUN2type;
typedef uint32_t BUN4type;
#if SIZEOF_BUN > 4
typedef uint64_t BUN8type;
#endif
#define BUN1_NONE ((BUN1type) 0xFF)
#define BUN2_NONE ((BUN2type) 0xFFFF)
#define BUN4_NONE ((BUN4type) 0xFFFFFFFF)
#if SIZEOF_BUN > 4
#define BUN8_NONE ((BUN8type) LL_CONSTANT(0xFFFFFFFFFFFFFFFF))
#endif


/*
 * @- Checking and Error definitions:
 */
typedef enum { GDK_FAIL, GDK_SUCCEED } gdk_return;

#define ERRORcheck(tst,	msg) do if (tst) { GDKerror(msg); return 0; } while (0)
#define BATcheck(tst,	msg)						\
	do {								\
		if ((tst) == NULL) {					\
			if (strchr((msg), ':'))				\
				GDKerror("%s.\n", (msg));		\
			else						\
				GDKerror("%s: BAT required.\n", (msg));	\
			return 0;					\
		}							\
	} while (0)

#define ATOMextern(t)	(ATOMstorage(t) >= TYPE_str)

#define TYPEcastable(t1,t2)	(ATOMtype(t1)==ATOMtype(t2))
#define TYPEequal(t1,t2)	(ATOMtype(t1)==ATOMtype(t2))
#define TYPEcomp(t1,t2)	(ATOMstorage(ATOMtype(t1))==ATOMstorage(ATOMtype(t2)))
#define TYPEerror(t1,t2)	(!TYPEcomp(t1,t2))
#define TYPEcheck(t1,t2)						\
	do {								\
		if (TYPEerror(t1, t2)) {				\
			GDKerror("TYPEcheck: Incompatible types %s and %s.\n", \
				ATOMname(t2), ATOMname(t1));		\
			return 0;					\
		} else if (!TYPEcomp(t1, t2)) {				\
			CHECKDEBUG THRprintf(GDKstdout,"#Interpreting %s as %s.\n", \
				ATOMname(t2), ATOMname(t1));		\
		}							\
	} while (0)
#define BATcompatible(P1,P2)						\
	do {								\
		ERRORcheck((P1) == NULL, "BATcompatible: BAT required\n"); \
		ERRORcheck((P2) == NULL, "BATcompatible: BAT required\n"); \
		if (TYPEerror(BAThtype(P1),BAThtype(P2)) ||		\
		    TYPEerror(BATttype(P1),BATttype(P2)))		\
		{							\
			GDKerror("Incompatible operands.\n");		\
			return 0;					\
		}							\
		if (BAThtype(P1) != BAThtype(P2) &&			\
		    ATOMtype((P1)->htype) != ATOMtype((P2)->htype)) {	\
			CHECKDEBUG THRprintf(GDKstdout,"#Interpreting %s as %s.\n", \
				ATOMname(BAThtype(P2)), ATOMname(BAThtype(P1))); \
		}							\
		if (BATttype(P1) != BATttype(P2) &&			\
		    ATOMtype((P1)->ttype) != ATOMtype((P2)->ttype)) {	\
			CHECKDEBUG THRprintf(GDKstdout,"#Interpreting %s as %s.\n", \
				ATOMname(BATttype(P2)), ATOMname(BATttype(P1))); \
		}							\
	} while (0)

/* Heap storage modes */
typedef enum {
	STORE_MEM = 0,		/* load into GDKmalloced memory */
	STORE_MMAP = 1,		/* mmap() into virtual memory */
	STORE_PRIV = 2,		/* BAT copy of copy-on-write mmap */
	STORE_INVALID		/* invalid value, used to indicate error */
} storage_t;

typedef struct {
	size_t maxsize;		/* maximum realloc size (bytes) */
	size_t free;		/* index where free area starts. */
	size_t size;		/* size of the heap (bytes) */
	char *base;		/* base pointer in memory. */
	str filename;		/* file containing image of the heap */

	unsigned int copied:1,	/* a copy of an existing map. */
		      hashash:1,/* the string heap contains hash values */
		      forcemap:1;  /* force STORE_MMAP even if heap exists */
	storage_t storage;	/* storage mode (mmap/malloc). */
	storage_t newstorage;	/* new desired storage mode at re-allocation. */
	bte dirty;		/* specific heap dirty marker */
	bat parentid;		/* cache id of VIEW parent bat */
} Heap;

typedef struct {
	int type;		/* type of index entity */
	int width;		/* width of hash entries */
	BUN nil;		/* nil representation */
	BUN lim;		/* collision list size */
	BUN mask;		/* number of hash buckets-1 (power of 2) */
	void *Hash;		/* hash table */
	void *Link;		/* collision list */
	Heap *heap;		/* heap where the hash is stored */
} Hash;

typedef struct {
	bte bits;        /* how many bits in imprints */
	Heap *bins;      /* ranges of bins */
	Heap *imps;      /* heap of imprints */
	BUN impcnt;      /* counter for imprints*/
	Heap *dict;      /* cache dictionary for compressing imprints */
	BUN dictcnt;     /* counter for cache dictionary */
} Imprints;


/*
 * @+ Binary Association Tables
 * Having gone to the previous preliminary definitions, we will now
 * introduce the structure of Binary Association Tables (BATs) in
 * detail. They are the basic storage unit on which GDK is modeled.
 *
 * The BAT holds an unlimited number of binary associations, called
 * BUNs (@strong{Binary UNits}).  The two attributes of a BUN are
 * called @strong{head} (left) and @strong{tail} (right) in the
 * remainder of this document.
 *
 *  @c image{http://monetdb.cwi.nl/projects/monetdb-mk/imgs/bat1,,,,feps}
 *
 * The above figure shows what a BAT looks like. It consists of two
 * columns, called head and tail, such that we have always binary
 * tuples (BUNs). The overlooking structure is the @strong{BAT
 * record}.  It points to a heap structure called the @strong{BUN
 * heap}.  This heap contains the atomic values inside the two
 * columns. If they are fixed-sized atoms, these atoms reside directly
 * in the BUN heap. If they are variable-sized atoms (such as string
 * or polygon), however, the columns has an extra heap for storing
 * those (such @strong{variable-sized atom heaps} are then referred to
 * as @strong{Head Heap}s and @strong{Tail Heap}s). The BUN heap then
 * contains integer byte-offsets (fixed-sized, of course) into a head-
 * or tail-heap.
 *
 * The BUN heap contains a contiguous range of BUNs. It starts after
 * the @strong{first} pointer, and finishes at the end in the
 * @strong{free} area of the BUN. All BUNs after the @strong{inserted}
 * pointer have been added in the last transaction (and will be
 * deleted on a transaction abort). All BUNs between the
 * @strong{deleted} pointer and the @strong{first} have been deleted
 * in this transaction (and will be reinserted at a transaction
 * abort).
 *
 * The location of a certain BUN in a BAT may change between
 * successive library routine invocations.  Therefore, one should
 * avoid keeping references into the BAT storage area for long
 * periods.
 *
 * Passing values between the library routines and the enclosing C
 * program is primarily through value pointers of type ptr. Pointers
 * into the BAT storage area should only be used for retrieval. Direct
 * updates of data stored in a BAT is forbidden. The user should
 * adhere to the interface conventions to guarantee the integrity
 * rules and to maintain the (hidden) auxiliary search structures.
 *
 * @- GDK variant record type
 * When manipulating values, MonetDB puts them into value records.
 * The built-in types have a direct entry in the union. Others should
 * be represented as a pointer of memory in pval or as a string, which
 * is basically the same. In such cases the len field indicates the
 * size of this piece of memory.
 */
typedef struct {
	union {			/* storage is first in the record */
		int ival;
		oid oval;
		sht shval;
		bte btval;
		wrd wval;
		flt fval;
		ptr pval;
		struct BAT *Bval; /* this field is only used by mel */
		bat bval;
		str sval;
		dbl dval;
		lng lval;
	} val;
	int len, vtype;
} *ValPtr, ValRecord;

/* definition of VALptr lower down in file after include of gdk_atoms.h */
#define VALnil(v,t) VALset(v,t,ATOMextern(t)?ATOMnil(t):ATOMnilptr(t))

/* interface definitions */
gdk_export ptr VALconvert(int typ, ValPtr t);
gdk_export int VALformat(char **buf, const ValRecord *res);
gdk_export ValPtr VALcopy(ValPtr dst, const ValRecord *src);
gdk_export ValPtr VALinit(ValPtr d, int tpe, const void *s);
gdk_export void VALempty(ValPtr v);
gdk_export void VALclear(ValPtr v);
gdk_export ValPtr VALset(ValPtr v, int t, ptr p);
gdk_export void *VALget(ValPtr v);
gdk_export int VALcmp(const ValRecord *p, const ValRecord *q);
gdk_export int VALisnil(const ValRecord *v);

/*
 * @- The BAT record
 * The elements of the BAT structure are introduced in the remainder.
 * Instead of using the underlying types hidden beneath it, one should
 * use a @emph{BAT} type that is supposed to look like this:
 * @verbatim
 * typedef struct {
 *           // static BAT properties
 *           bat    batCacheid;       // bat id: index in BBPcache
 *           int    batPersistence;   // persistence mode
 *           bit    batCopiedtodisk;  // BAT is saved on disk?
 *           bit    batSet;           // all tuples in the BAT are unique?
 *           // dynamic BAT properties
 *           int    batHeat;          // heat of BAT in the BBP
 *           sht    batDirty;         // BAT modified after last commit?
 *           bit    batDirtydesc;     // BAT descriptor specific dirty flag
 *           Heap*  batBuns;          // Heap where the buns are stored
 *           // DELTA status
 *           BUN    batDeleted;       // first deleted BUN
 *           BUN    batFirst;         // empty BUN before the first alive BUN
 *           BUN    batInserted;      // first inserted BUN
 *           BUN    batCount;         // Tuple count
 *           // Head properties
 *           int    htype;            // Head type number
 *           str    hident;           // name for head column
 *           bit    hkey;             // head values should be unique?
 *           bit    hsorted;          // are head values currently ordered?
 *           bit    hvarsized;        // for speed: head type is varsized?
 *           bit    hnonil;           // head has no nils
 *           oid    halign;          // alignment OID for head.
 *           // Head storage
 *           int    hloc;             // byte-offset in BUN for head elements
 *           Heap   *hheap;           // heap for varsized head values
 *           Hash   *hhash;           // linear chained hash table on head
 *           Imprints *himprints;     // column imprints index on head
 *           // Tail properties
 *           int    ttype;            // Tail type number
 *           str    tident;           // name for tail column
 *           bit    tkey;             // tail values should be unique?
 *           bit    tnonil;           // tail has no nils
 *           bit    tsorted;          // are tail values currently ordered?
 *           bit    tvarsized;        // for speed: tail type is varsized?
 *           oid    talign;           // alignment OID for head.
 *           // Tail storage
 *           int    tloc;             // byte-offset in BUN for tail elements
 *           Heap   *theap;           // heap for varsized tail values
 *           Hash   *thash;           // linear chained hash table on tail
 *           Imprints *timprints;     // column imprints index on tail
 *  } BAT;
 * @end verbatim
 *
 * The internal structure of the @strong{BAT} record is in fact much
 * more complex, but GDK programmers should refrain of making use of
 * that.
 *
 * The reason for this complex structure is to allow for a BAT to
 * exist in two incarnations at the time: the @emph{normal view} and
 * the @emph{reversed view}. Each bat @emph{b} has a
 * BATmirror(@emph{b}) which has the negative @strong{cacheid} of b in
 * the BBP.
 *
 * Since we don't want to pay cost to keep both views in line with
 * each other under BAT updates, we work with shared pieces of memory
 * between the two views. An update to one will thus automatically
 * update the other.  In the same line, we allow @strong{synchronized
 * BATs} (BATs with identical head columns, and marked as such in the
 * @strong{BAT Alignment} interface) now to be clustered horizontally.
 *
 *  @c image{http://monetdb.cwi.nl/projects/monetdb-mk/imgs/bat2,,,,feps}
 */

typedef struct {
	MT_Id tid;		/* which thread created it */
	int stamp;		/* BAT recent creation stamp */
	unsigned int
	 copiedtodisk:1,	/* once written */
	 dirty:2,		/* dirty wrt disk? */
	 dirtyflushed:1,	/* was dirty before commit started? */
	 descdirty:1,		/* bat descriptor dirty marker */
	 set:1,			/* real set semantics */
	 restricted:2,		/* access priviliges */
	 persistence:1,		/* should the BAT persist on disk? */
	 unused:23;		/* value=0 for now */
	int sharecnt;		/* incoming view count */
	char map_head;		/* mmap mode for head bun heap */
	char map_tail;		/* mmap mode for tail bun heap */
	char map_hheap;		/* mmap mode for head atom heap */
	char map_theap;		/* mmap mode for tail atom heap */
} BATrec;

typedef struct {
	/* delta status administration */
	BUN deleted;		/* start of deleted elements */
	BUN first;		/* to store next deletion */
	BUN inserted;		/* start of inserted elements */
	BUN count;		/* tuple count */
	BUN capacity;		/* tuple capacity */
} BUNrec;

typedef struct PROPrec {
	int id;
	ValRecord v;
	struct PROPrec *next;	/* simple chain of properties */
} PROPrec;

/* see also comment near BATassertProps() for more information about
 * the properties */
typedef struct {
	str id;				/* label for head/tail column */

	unsigned short width;	/* byte-width of the atom array */
	bte type;			/* type id. */
	bte shift;			/* log2 of bunwidth */
	unsigned int
	 varsized:1,		/* varsized (1) or fixedsized (0) */
	 key:2,				/* duplicates allowed? */
	 dense:1,			/* OID only: only consecutive values */
	 nonil:1,			/* nonil isn't propchecked yet */
	 nil:1,				/* there is a nil in the column */
	 sorted:1,			/* column is sorted in ascending order */
	 revsorted:1;		/* column is sorted in descending order */
	oid align;			/* OID for sync alignment */
	BUN nokey[2];		/* positions that prove key ==FALSE */
	BUN nosorted;		/* position that proves sorted==FALSE */
	BUN norevsorted;	/* position that proves revsorted==FALSE */
	BUN nodense;		/* position that proves dense==FALSE */
	oid seq;			/* start of dense head sequence */

	Heap heap;			/* space for the column. */
	Heap *vheap;		/* space for the varsized data. */
	Hash *hash;			/* hash table */
	Imprints *imprints;	/* column imprints index */

	PROPrec *props;		/* list of dynamic properties stored in the bat descriptor */
} COLrec;

/* assert that atom width is power of 2, i.e., width == 1<<shift */
#define assert_shift_width(shift,width) assert(((shift) == 0 && (width) == 0) || ((unsigned)1<<(shift)) == (unsigned)(width))

#define GDKLIBRARY_PRE_VARWIDTH 061023  /* backward compatible version */
#define GDKLIBRARY_CHR		061024	/* version that still had chr type */
#define GDKLIBRARY_SORTED_BYTE	061025	/* version that still had byte-sized sorted flag */
#define GDKLIBRARY		061026

typedef struct BAT {
	/* static bat properties */
	bat batCacheid;		/* index into BBP */

	/* dynamic column properties */
	COLrec *H;		/* column info */
	COLrec *T;		/* column info */

	/* dynamic bat properties */
	BATrec *P;		/* cache and sort info */
	BUNrec *U;		/* cache and sort info */
} BAT;

typedef struct BATiter {
	BAT *b;
	oid hvid, tvid;
} BATiter;

/*
 * The different parts of which a BAT consists are physically stored
 * next to each other in the BATstore type.
 */
typedef struct {
	BAT B;			/* storage for BAT descriptor */
	BAT BM;			/* mirror (reverse) BAT */
	COLrec H;		/* storage for head column */
	COLrec T;		/* storage for tail column */
	BATrec P;		/* storage for BATrec */
	BUNrec U;		/* storage for BUNrec */
} BATstore;

typedef int (*GDKfcn) ();

/* macros's to hide complexity of BAT structure */
#define batPersistence	P->persistence
#define batCopiedtodisk	P->copiedtodisk
#define batSet		P->set
#define batDirty	P->dirty
#define batConvert	P->convert
#define batDirtyflushed	P->dirtyflushed
#define batDirtydesc	P->descdirty
#define batFirst	U->first
#define batInserted	U->inserted
#define batDeleted	U->deleted
#define batCount	U->count
#define batCapacity	U->capacity
#define batStamp	P->stamp
#define batSharecnt	P->sharecnt
#define batRestricted	P->restricted
#define creator_tid	P->tid
#define htype		H->type
#define ttype		T->type
#define hkey		H->key
#define tkey		T->key
#define hvarsized	H->varsized
#define tvarsized	T->varsized
#define hseqbase	H->seq
#define tseqbase	T->seq
#define hsorted		H->sorted
#define hrevsorted	H->revsorted
#define tsorted		T->sorted
#define trevsorted	T->revsorted
#define hdense		H->dense
#define tdense		T->dense
#define hident		H->id
#define tident		T->id
#define halign		H->align
#define talign		T->align

#define batMaphead	P->map_head
#define batMaptail	P->map_tail
#define batMaphheap	P->map_hheap
#define batMaptheap	P->map_theap
/*
 * @- Heap Management
 * Heaps are the low-level entities of mass storage in
 * BATs. Currently, they can either be stored on disk, loaded into
 * memory, or memory mapped.
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab
 *  HEAPalloc (Heap *h, size_t nitems, size_t itemsize);
 * @item int
 * @tab
 *  HEAPfree (Heap *h);
 * @item int
 * @tab
 *  HEAPextend (Heap *h, size_t size);
 * @item int
 * @tab
 *  HEAPload (Heap *h, str nme,ext, int trunc);
 * @item int
 * @tab
 *  HEAPsave (Heap *h, str nme,ext);
 * @item int
 * @tab
 *  HEAPcopy (Heap *dst,*src);
 * @item int
 * @tab
 *  HEAPdelete (Heap *dst, str o, str ext);
 * @item int
 * @tab
 *  HEAPwarm (Heap *h);
 * @end multitable
 *
 *
 * These routines should be used to alloc free or extend heaps; they
 * isolate you from the different ways heaps can be accessed.
 */
gdk_export int HEAPfree(Heap *h);
gdk_export int HEAPextend(Heap *h, size_t size);
gdk_export int HEAPcopy(Heap *dst, Heap *src);
gdk_export size_t HEAPvmsize(Heap *h);
gdk_export size_t HEAPmemsize(Heap *h);

/*
 * @- Internal HEAP Chunk Management
 * Heaps are used in BATs to store data for variable-size atoms.  The
 * implementor must manage malloc()/free() functionality for atoms in
 * this heap. A standard implementation is provided here.
 *
 * @table @code
 * @item void
 * HEAP_initialize  (Heap* h, size_t nbytes, size_t nprivate, int align )
 * @item void
 * HEAP_destroy     (Heap* h)
 * @item var_t
 * HEAP_malloc      (Heap* heap, size_t nbytes)
 * @item void
 * HEAP_free        (Heap *heap, var_t block)
 * @item int
 * HEAP_private     (Heap* h)
 * @item void
 * HEAP_printstatus (Heap* h)
 * @item void
 * HEAP_check       (Heap* h)
 * @end table
 *
 * The heap space starts with a private space that is left untouched
 * by the normal chunk allocation.  You can use this private space
 * e.g. to store the root of an rtree HEAP_malloc allocates a chunk of
 * memory on the heap, and returns an index to it.  HEAP_free frees a
 * previously allocated chunk HEAP_private returns an integer index to
 * private space.
 */
/* structure used by HEAP_check functions */
typedef struct {
	size_t minpos;		/* minimum block byte-index */
	size_t maxpos;		/* maximum block byte-index */
	int alignment;		/* block index alignment */
	int *validmask;		/* bitmap with all valid byte-indices
				 * first bit corresponds with 'minpos';
				 * 2nd bit with 'minpos+alignment', etc
				 */
} HeapRepair;

gdk_export void HEAP_initialize(
	Heap *heap,		/* nbytes -- Initial size of the heap. */
	size_t nbytes,		/* alignment -- for objects on the heap. */
	size_t nprivate,	/* nprivate -- Size of private space */
	int alignment		/* alignment restriction for allocated chunks */
	);

gdk_export var_t HEAP_malloc(Heap *heap, size_t nbytes);
gdk_export void HEAP_free(Heap *heap, var_t block);

#define HEAP_index(HEAP,INDEX,TYPE)	((TYPE *)((char *) (HEAP)->base + (INDEX)))

/*
 * @- BAT construction
 * @multitable @columnfractions 0.08 0.7
 * @item @code{BAT* }
 * @tab BATnew (int headtype, int tailtype, BUN cap)
 * @item @code{BAT* }
 * @tab BATextend (BAT *b, BUN newcap)
 * @end multitable
 *
 * A temporary BAT is instantiated using BATnew with the type aliases
 * of the required binary association. The aliases include the
 * built-in types, such as TYPE_int....TYPE_ptr, and the atomic types
 * introduced by the user. The initial capacity to be accommodated
 * within a BAT is indicated by cap.  Their extend is automatically
 * incremented upon storage overflow.  Failure to create the BAT
 * results in a NULL pointer.
 *
 * The routine BATclone creates an empty BAT storage area with the
 * properties inherited from its argument.
 */
#define BATDELETE	(-9999)

gdk_export BAT *BATnew(int hdtype, int tltype, BUN capacity);
gdk_export BAT *BATextend(BAT *b, BUN newcap);

/* internal */
gdk_export bte ATOMelmshift(int sz);

/*
 * @- BUN manipulation
 * @multitable @columnfractions 0.08 0.7
 * @item BAT*
 * @tab BATins (BAT *b, BAT *c, bit force)
 * @item BAT*
 * @tab BATappend (BAT *b, BAT *c, bit force)
 * @item BAT*
 * @tab BATdel (BAT *b, BAT *c, bit force)
 * @item BAT*
 * @tab BUNins (BAT *b, ptr left, ptr right, bit force)
 * @item BAT*
 * @tab BUNappend (BAT *b, ptr right, bit force)
 * @item BAT*
 * @tab BUNreplace (BAT *b, ptr left, ptr right, bit force)
 * @item int
 * @tab BUNdel (BAT *b, ptr left, ptr right, bit force)
 * @item int
 * @tab BUNdelHead (BAT *b, ptr left, bit force)
 * @item BUN
 * @tab BUNfnd (BAT *b, ptr head)
 * @item void
 * @tab BUNfndOID (BUN result, BATiter bi, oid *head)
 * @item void
 * @tab BUNfndSTD (BUN result, BATiter bi, ptr head)
 * @item BUN
 * @tab BUNlocate (BAT *b, ptr head, ptr tail)
 * @item ptr
 * @tab BUNhead (BAT *b, BUN p)
 * @item ptr
 * @tab BUNtail (BAT *b, BUN p)
 * @end multitable
 *
 * The BATs contain a number of fixed-sized slots to store the binary
 * associations.  These slots are called BUNs or BAT units. A BUN
 * variable is a pointer into the storage area of the BAT, but it has
 * limited validity. After a BAT modification, previously obtained
 * BUNs may no longer reside at the same location.
 *
 * The association list does not contain holes.  This density permits
 * users to quickly access successive elements without the need to
 * test the items for validity. Moreover, it simplifies transport to
 * disk and other systems. The negative effect is that the user should
 * be aware of the evolving nature of the sequence, which may require
 * copying the BAT first.
 *
 * The update operations come in three flavors. Element-wise updates
 * can use BUNins, BUNappend, BUNreplace, BUNdel, and BUNdelHead.  The
 * batch update operations are BATins, BATappend and BATdel.
 *
 * Only experts interested in speed may use BUNfastins, since it skips
 * most consistency checks, does not update search accelerators, and
 * does not maintain properties such as the hsorted and tsorted
 * flags. Beware!
 *
 * The routine BUNfnd provides fast access to a single BUN providing a
 * value for the head of the binary association.  A very fast shortcut
 * for BUNfnd if the selection type is known to be integer or OID, is
 * provided in the form of the macro BUNfndOID.
 *
 * To select on a tail, one should use the reverse view obtained by
 * BATmirror.
 *
 * The routines BUNhead and BUNtail return a pointer to the first and
 * second value in an association, respectively.  To guard against
 * side effects on the BAT, one should normally copy this value into a
 * scratch variable for further processing.
 *
 * Behind the interface we use several macros to access the BUN fixed
 * part and the variable part. The BUN operators always require a BAT
 * pointer and BUN identifier.
 * @itemize
 * @item
 * BAThtype(b) and  BATttype(b) find out the head and tail type of a BAT.
 * @item
 * BUNfirst(b) returns a BUN pointer to the first BUN as a BAT.
 * @item
 * BUNlast(b) returns the BUN pointer directly after the last BUN
 * in the BAT.
 * @item
 * BUNhead(b, p) and BUNtail(b, p) return pointers to the
 * head-value and tail-value in a given BUN.
 * @item
 * BUNhloc(b, p) and BUNtloc(b, p) do the same thing, but knowing
 * in advance that the head-atom resp. tail-atom of a BAT is fixed size.
 * @item
 * BUNhvar(b, p) and BUNtvar(b, p) do the same thing, but knowing
 * in advance that the head-atom resp. tail-atom of a BAT is variable sized.
 * @end itemize
 */
/* NOTE: `p' is evaluated after a possible upgrade of the heap */
#define HTputvalue(b, p, v, copyall, HT)				\
	do {								\
		if ((b)->HT->varsized && (b)->HT->type) {		\
			var_t _d;					\
			ptr _ptr;					\
			ATOMput((b)->HT->type, (b)->HT->vheap, &_d, v);	\
			if ((b)->HT->width < SIZEOF_VAR_T &&		\
			    ((b)->HT->width <= 2 ? _d - GDK_VAROFFSET : _d) >= ((size_t) 1 << (8 * (b)->HT->width))) { \
				/* doesn't fit in current heap, upgrade it */ \
				GDKupgradevarheap((b)->HT, _d, (copyall)); \
			}						\
			_ptr = (p);					\
			switch ((b)->HT->width) {			\
			case 1:						\
				* (unsigned char *) _ptr = (unsigned char) (_d - GDK_VAROFFSET); \
				break;					\
			case 2:						\
				* (unsigned short *) _ptr = (unsigned short) (_d - GDK_VAROFFSET); \
				break;					\
			case 4:						\
				* (unsigned int *) _ptr = (unsigned int) _d; \
				break;					\
			case 8:						\
				* (var_t *) _ptr = _d;			\
				break;					\
			}						\
		} else							\
			ATOMput((b)->HT->type, (b)->HT->vheap, (p), v);	\
	} while (0)
#define Hputvalue(b, p, v, copyall)	HTputvalue(b, p, v, copyall, H)
#define Tputvalue(b, p, v, copyall)	HTputvalue(b, p, v, copyall, T)
#define HTreplacevalue(b, p, v, HT)					\
	do {								\
		if ((b)->HT->varsized && (b)->HT->type) {		\
			var_t _d;					\
			ptr _ptr;					\
			_ptr = (p);					\
			switch ((b)->HT->width) {			\
			case 1:						\
				_d = (var_t) * (unsigned char *) _ptr + GDK_VAROFFSET; \
				break;					\
			case 2:						\
				_d = (var_t) * (unsigned short *) _ptr + GDK_VAROFFSET; \
				break;					\
			case 4:						\
				_d = (var_t) * (unsigned int *) _ptr;	\
				break;					\
			case 8:						\
				_d = * (var_t *) _ptr;			\
				break;					\
			}						\
			ATOMreplace((b)->HT->type, (b)->HT->vheap, &_d, v); \
			if ((b)->HT->width < SIZEOF_VAR_T &&		\
			    ((b)->HT->width <= 2 ? _d - GDK_VAROFFSET : _d) >= ((size_t) 1 << (8 * (b)->HT->width))) { \
				/* doesn't fit in current heap, upgrade it */ \
				GDKupgradevarheap((b)->HT, _d, 0);	\
			}						\
			_ptr = (p);					\
			switch ((b)->HT->width) {			\
			case 1:						\
				* (unsigned char *) _ptr = (unsigned char) (_d - GDK_VAROFFSET); \
				break;					\
			case 2:						\
				* (unsigned short *) _ptr = (unsigned short) (_d - GDK_VAROFFSET); \
				break;					\
			case 4:						\
				* (unsigned int *) _ptr = (unsigned int) _d; \
				break;					\
			case 8:						\
				* (var_t *) _ptr = _d;			\
				break;					\
			}						\
		} else							\
			ATOMreplace((b)->HT->type, (b)->HT->vheap, (p), v); \
	} while (0)
#define Hreplacevalue(b, p, v)		HTreplacevalue(b, p, v, H)
#define Treplacevalue(b, p, v)		HTreplacevalue(b, p, v, T)
#define HTfastins_nocheck(b, p, v, s, HT)			\
	do {							\
		assert((b)->HT->width == (s));			\
		(b)->HT->heap.free += (s);			\
		HTputvalue((b), HT##loc((b), (p)), (v), 0, HT);	\
	} while (0)
#define hfastins_nocheck(b, p, v, s)	HTfastins_nocheck(b, p, v, s, H)
#define tfastins_nocheck(b, p, v, s)	HTfastins_nocheck(b, p, v, s, T)

#define bunfastins_nocheck(b, p, h, t, hs, ts)		\
	do {						\
		hfastins_nocheck(b, p, h, hs);		\
		tfastins_nocheck(b, p, t, ts);		\
		(b)->batCount++;			\
	} while (0)

#define bunfastins_nocheck_inc(b, p, h, t)				\
	do {								\
		bunfastins_nocheck(b, p, h, t, Hsize(b), Tsize(b));	\
		p++;							\
	} while (0)

#define bunfastins(b, h, t)						\
	do {								\
		register BUN _p = BUNlast(b);				\
		if (_p == BUN_MAX || BATcount(b) == BUN_MAX) {		\
			GDKerror("bunfastins: too many elements to accomodate (INT_MAX)\n");	\
			goto bunins_failed;				\
		}							\
		if (_p + 1 > BATcapacity(b)) {				\
			if (BATextend((b), BATgrows(b)) == NULL)	\
				goto bunins_failed;			\
		}							\
		bunfastins_nocheck(b, _p, h, t, Hsize(b), Tsize(b));	\
	} while (0)

#define bunfastins_check(b, p, h, t) bunfastins(b, h, t)

gdk_export int GDKupgradevarheap(COLrec *c, var_t v, int copyall);
gdk_export BAT *BUNfastins(BAT *b, const void *left, const void *right);
gdk_export BAT *BUNins(BAT *b, const void *left, const void *right, bit force);
gdk_export BAT *BUNappend(BAT *b, const void *right, bit force);
gdk_export BAT *BATins(BAT *b, BAT *c, bit force);
gdk_export BAT *BATappend(BAT *b, BAT *c, bit force);
gdk_export BAT *BUNdel(BAT *b, const void *left, const void *right, bit force);
gdk_export BAT *BUNdelHead(BAT *b, const void *left, bit force);
gdk_export BUN BUNdelete(BAT *b, BUN p, bit force);
gdk_export BAT *BATdel(BAT *b, BAT *c, bit force);
gdk_export BAT *BATdelHead(BAT *b, BAT *c, bit force);

gdk_export BAT *BUNreplace(BAT *b, const void *left, const void *right, bit force);
gdk_export BAT *BUNinplace(BAT *b, BUN p, const void *left, const void *right, bit force);
gdk_export BAT *BATreplace(BAT *b, BAT *n, bit force);

gdk_export BUN BUNlocate(BAT *b, const void *left, const void *right);
gdk_export BUN BUNfnd(BAT *b, const void *left);

#define BUNfndVOID(p,bi,v)						\
	do {								\
		BUN result = BUNfirst((bi).b) + (BUN) (*(const oid*)(v) - (bi).b->hseqbase); \
		int check =						\
			(((*(const oid*)(v) == oid_nil) ^ ((bi).b->hseqbase == oid_nil)) | \
			 (*(const oid*) (v) < (bi).b->hseqbase) |		\
			 (*(const oid*) (v) >= (bi).b->hseqbase + (bi).b->batCount));	\
		(p) = check?BUN_NONE:result; /* and with 0xFF...FF or 0x00..00 */ \
	} while (0)

#define BUNfndOID(p,bi,v)			\
	do {					\
		if (BAThdense(bi.b)) {		\
			BUNfndVOID(p,bi,v);	\
		} else {			\
			HASHfnd_oid(p,bi,v);	\
		}				\
	} while (0)
#define BUNfndSTD(p,bi,v) ((p) = BUNfnd(bi.b,v))

#define BAThtype(b)	((b)->htype == TYPE_void && (b)->hseqbase != oid_nil ? \
			 TYPE_oid : (b)->htype)
#define BATttype(b)	((b)->ttype == TYPE_void && (b)->tseqbase != oid_nil ? \
			 TYPE_oid : (b)->ttype)
#define BAThstore(b)	(BAThdense(b) ? TYPE_void : (b)->htype)
#define BATtstore(b)	(BATtdense(b) ? TYPE_void : (b)->ttype)
#define Hbase(b)	((b)->H->vheap->base)
#define Tbase(b)	((b)->T->vheap->base)

#define Hsize(b)	((b)->H->width)
#define Tsize(b)	((b)->T->width)

/* new semantics ! */
#define headsize(b,p)	((b)->H->type?((size_t)(p))<<(b)->H->shift:0)
#define tailsize(b,p)	((b)->T->type?((size_t)(p))<<(b)->T->shift:0)

#define Hloc(b,p)	((b)->H->heap.base+((p)<<(b)->H->shift))
#define Tloc(b,p)	((b)->T->heap.base+((p)<<(b)->T->shift))

#if SIZEOF_VAR_T < SIZEOF_VOID_P
/* NEW 11/4/2009: when compiled with 32-bits oids/var_t on 64-bits
 * systems, align heap strings on 8 byte boundaries always (wasting 4
 * padding bytes on avg). Note that in heaps where duplicate
 * elimination is successful, such padding occurs anyway (as an aside,
 * a better implementation with two-bytes pointers in the string heap
 * hash table, could reduce that padding to avg 1 byte wasted -- see
 * TODO below).
 *
 * This 8 byte alignment allows the offset in the fixed part of the
 * BAT string column to be interpreted as an index, which should be
 * multiplied by 8 to get the position (VARSHIFT). The overall effect
 * is that 32GB heaps can be addressed even when oids are limited to
 * 4G tuples.
 *
 * In the future, we might extend this such that the string alignment
 * is set in the BAT header (columns with long strings take more
 * storage space, but could tolerate more padding).  It would mostly
 * work, only the sort routine and strPut/strLocate (which do not see
 * the BAT header) extra parameters would be needed in their APIs.
 */
typedef unsigned short stridx_t;
#define SIZEOF_STRIDX_T SIZEOF_SHORT
#define GDK_VARSHIFT 3
#define GDK_VARALIGN (1<<GDK_VARSHIFT)
#else
typedef var_t stridx_t; /* TODO: should also be unsigned short, but kept at var_t not to break BAT images */
#define SIZEOF_STRIDX_T SIZEOF_VAR_T
#define GDK_VARSHIFT 0
#define GDK_VARALIGN SIZEOF_STRIDX_T
#endif

#if SIZEOF_VAR_T == 8
#define VarHeapValRaw(b,p,w)						\
	((w)==1 ? (var_t)*((unsigned char *)(b)+(p))+GDK_VAROFFSET :	\
	 ((w)==2 ? (var_t)*((unsigned short *)(b)+(p))+GDK_VAROFFSET :	\
	  ((w)==4 ? (var_t)*((unsigned int *)(b)+(p)) :			\
	   *((var_t *)(b)+(p)))))
#else
#define VarHeapValRaw(b,p,w)						\
	((w)==1 ? (var_t)*((unsigned char *)(b)+(p))+GDK_VAROFFSET :	\
	 ((w)==2 ? (var_t)*((unsigned short *)(b)+(p))+GDK_VAROFFSET :	\
	  *((var_t *)(b)+(p))))
#endif
#define VarHeapVal(b,p,w) ((size_t) VarHeapValRaw(b,p,w)  << GDK_VARSHIFT)
#define BUNhvaroff(bi,p) VarHeapVal((bi).b->H->heap.base, (p), (bi).b->H->width)
#define BUNtvaroff(bi,p) VarHeapVal((bi).b->T->heap.base, (p), (bi).b->T->width)

#define BUNhloc(bi,p)	Hloc((bi).b,p)
#define BUNtloc(bi,p)	Tloc((bi).b,p)
#define BUNhpos(bi,p)	(Hpos(&(bi),p))
#define BUNtpos(bi,p)	(Tpos(&(bi),p))
#define BUNhvar(bi,p)	((bi).b->htype?Hbase((bi).b)+BUNhvaroff(bi,p):BUNhpos(bi,p))
#define BUNtvar(bi,p)	((bi).b->ttype?Tbase((bi).b)+BUNtvaroff(bi,p):BUNtpos(bi,p))
#define BUNhead(bi,p)	((bi).b->hvarsized?BUNhvar(bi,p):BUNhloc(bi,p))
#define BUNtail(bi,p)	((bi).b->tvarsized?BUNtvar(bi,p):BUNtloc(bi,p))

static inline BATiter
bat_iterator(BAT *b)
{
	BATiter bi;

	bi.b = b;
	bi.hvid = bi.tvid = 0;
	return bi;
}

#define BUNfirst(b)	(assert((b)->batFirst <= BUN_MAX), (b)->batFirst)
#define BUNlast(b)	(assert((b)->batFirst <= BUN_MAX),		\
			 assert((b)->batCount <= BUN_MAX),		\
			 assert((b)->batCount <= BUN_MAX - (b)->batFirst), \
			 (b)->batFirst + (b)->batCount)

#define BATcount(b)	((b)->batCount)

/*
 * @- BAT properties
 * @multitable @columnfractions 0.08 0.7
 * @item BUN
 * @tab BATcount (BAT *b)
 * @item void
 * @tab BATsetcapacity (BAT *b, BUN cnt)
 * @item void
 * @tab BATsetcount (BAT *b, BUN cnt)
 * @item BUN
 * @tab BATrename (BAT *b, str nme)
 * @item BAT *
 * @tab BATkey (BAT *b, int onoff)
 * @item BAT *
 * @tab BATset (BAT *b, int onoff)
 * @item BAT *
 * @tab BATmode (BAT *b, int mode)
 * @item BAT *
 * @tab BATsetaccess (BAT *b, int mode)
 * @item int
 * @tab BATdirty (BAT *b)
 * @item int
 * @tab BATgetaccess (BAT *b)
 * @end multitable
 *
 * The function BATcount returns the number of associations stored in
 * the BAT.
 *
 * The BAT is given a new logical name using BATrename.
 *
 * The integrity properties to be maintained for the BAT are
 * controlled separately.  A key property indicates that duplicates in
 * the association dimension are not permitted. The BAT is turned into
 * a set of associations using BATset. Key and set properties are
 * orthogonal integrity constraints.  The strongest reduction is
 * obtained by making the BAT a set with key restrictions on both
 * dimensions.
 *
 * The persistency indicator tells the retention period of BATs.  The
 * system support three modes: PERSISTENT and TRANSIENT.
 * The PERSISTENT BATs are automatically saved upon session boundary
 * or transaction commit.  TRANSIENT BATs are removed upon transaction
 * boundary.  All BATs are initially TRANSIENT unless their mode is
 * changed using the routine BATmode.
 *
 * The BAT properties may be changed at any time using BATkey, BATset,
 * and BATmode.
 *
 * Valid BAT access properties can be set with BATsetaccess and
 * BATgetaccess: BAT_READ, BAT_APPEND, and BAT_WRITE.  BATs can be
 * designated to be read-only. In this case some memory optimizations
 * may be made (slice and fragment bats can point to stable subsets of
 * a parent bat).  A special mode is append-only. It is then allowed
 * to insert BUNs at the end of the BAT, but not to modify anything
 * that already was in there.
 */
#ifndef BATcount
gdk_export BUN BATcount(BAT *b);
#endif
gdk_export BUN BATcount_no_nil(BAT *b);
gdk_export void BATsetcapacity(BAT *b, BUN cnt);
gdk_export void BATsetcount(BAT *b, BUN cnt);
gdk_export BUN BATgrows(BAT *b);
gdk_export BAT *BATkey(BAT *b, int onoff);
gdk_export BAT *BATset(BAT *b, int onoff);
gdk_export BAT *BATmode(BAT *b, int onoff);
gdk_export BAT *BATroles(BAT *b, const char *hnme, const char *tnme);
gdk_export int BATname(BAT *b, const char *nme);
gdk_export BAT *BATseqbase(BAT *b, oid o);
gdk_export BAT *BATsetaccess(BAT *b, int mode);
gdk_export int BATgetaccess(BAT *b);


#define BATdirty(b)	((b)->batCopiedtodisk == 0 || (b)->batDirty ||	\
			 (b)->batDirtydesc ||				\
			 (b)->H->heap.dirty || (b)->T->heap.dirty ||	\
			 ((b)->H->vheap?(b)->H->vheap->dirty:0) ||	\
			 ((b)->T->vheap?(b)->T->vheap->dirty:0))

#define PERSISTENT		0
#define TRANSIENT		1

#define BAT_WRITE		0	/* all kinds of access allowed */
#define BAT_READ		1	/* only read-access allowed */
#define BAT_APPEND		2	/* only reads and appends allowed */

#define BATcapacity(b)	(b)->batCapacity
/*
 * @- BAT manipulation
 * @multitable @columnfractions 0.08 0.7
 * @item BAT *
 * @tab BATclear (BAT *b, int force)
 * @item BAT *
 * @tab BATcopy (BAT *b, int ht, int tt, int writeable)
 * @item BAT *
 * @tab BATmark (BAT *b, oid base)
 * @item BAT *
 * @tab BATmark_grp (BAT *b, BAT *g, oid *s)
 * @item BAT *
 * @tab BATmirror (BAT *b)
 * @item BAT *
 * @tab BATreset (BAT *b)
 * @end multitable
 *
 * The routine BATclear removes the binary associations, leading to an
 * empty, but (re-)initialized BAT. Its properties are retained.  A
 * temporary copy is obtained with BATcopy. The new BAT has an unique
 * name.  The routine BATmark creates a binary association that
 * introduces a new tail column of fresh densely ascending OIDs.  The
 * base OID can be given explicitly, or if oid_nil is passed, is
 * chosen as a new unique range by the system.
 *
 * The routine BATmirror returns the mirror image BAT (where tail is
 * head and head is tail) of that same BAT. This does not involve a
 * state change in the BAT (as previously): both views on the BAT
 * exist at the same time.
 */
gdk_export BAT *BATclear(BAT *b, int force);
gdk_export BAT *BATcopy(BAT *b, int ht, int tt, int writeable);
gdk_export BAT *BATmark(BAT *b, oid base);
gdk_export BAT *BATmark_grp(BAT *b, BAT *g, oid *base);

gdk_export gdk_return BATgroup(BAT **groups, BAT **extents, BAT **histo, BAT *b, BAT *g, BAT *e, BAT *h);

/*
 * @- BAT Input/Output
 * @multitable @columnfractions 0.08 0.7
 * @item BAT *
 * @tab BATload (str name)
 * @item BAT *
 * @tab BATsave (BAT *b)
 * @item int
 * @tab BATmmap (BAT *b, int hb, int tb, int hh, int th, int force )
 * @item int
 * @tab BATmadvise (BAT *b, int hb, int tb, int hh, int th )
 * @item int
 * @tab BATdelete (BAT *b)
 * @end multitable
 *
 * A BAT created by BATnew is considered temporary until one calls the
 * routine BATsave or BATmode.  This routine reserves disk space and
 * checks for name clashes in the BAT directory. It also makes the BAT
 * persistent. The empty BAT is initially marked as ordered on both
 * columns.
 *
 * Failure to read or write the BAT results in a NULL, otherwise it
 * returns the BAT pointer.
 *
 * @- Heap Storage Modes
 * The discriminative storage modes are memory-mapped, compressed, or
 * loaded in memory.  The @strong{BATmmap()} changes the storage mode
 * of each heap associated to a BAT.  As can be seen in the bat
 * record, each BAT has one BUN-heap (@emph{bn}), and possibly two
 * heaps (@emph{hh} and @emph{th}) for variable-sized atoms.
 *
 * The BATmadvise call works in the same way. Using the madvise()
 * system call it issues buffer management advice to the OS kernel, as
 * for the expected usage pattern of the memory in a heap.
 */

gdk_export int GDK_mem_pagebits;	/* page size for non-linear mmaps */

#define REMAP_PAGE_BITS	GDK_mem_pagebits
#define REMAP_PAGE_SIZE	((size_t) 1 << REMAP_PAGE_BITS)
#define REMAP_PAGE_MASK	(REMAP_PAGE_SIZE - 1)
#define REMAP_PAGE_MAXBITS (REMAP_PAGE_BITS+3)
#define REMAP_PAGE_MAXSIZE ((size_t) 1 << REMAP_PAGE_MAXBITS) /* max page bytesize of unary BUN heap (8-byte atom) */

/* Buffer management advice for heaps */
#define BUF_NORMAL	0	/* No further special treatment */
#define BUF_RANDOM	1	/* Expect random page references */
#define BUF_SEQUENTIAL	2	/* Expect sequential page references */
#define BUF_WILLNEED	3	/* Will need these pages */
#define BUF_DONTNEED	4	/* Don't need these pages */

/* Heaps that are use and hence should to be loaded by BATaccess */
#define USE_HEAD	1	/* BUNs & string heap */
#define USE_TAIL	2	/* BUNs & string heap */
#define USE_HHASH	4	/* hash index */
#define USE_THASH	8	/* hash index */
#define USE_ALL	(USE_HEAD|USE_TAIL|USE_HHASH|USE_THASH)

gdk_export BAT *BATsave(BAT *b);
gdk_export int BATmmap(BAT *b, int hb, int tb, int hh, int th, int force);
gdk_export int BATmadvise(BAT *b, int hb, int tb, int hh, int th);
gdk_export int BATdelete(BAT *b);
gdk_export size_t BATmemsize(BAT *b, int dirty);

gdk_export void GDKfilepath(str path, const char *nme, const char *mode, const char *ext);
gdk_export int GDKcreatedir(const char *nme);

/*
 * @- Printing
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab BATprintf (stream *f, BAT *b)
 * @item int
 * @tab BATmultiprintf (stream *f, int argc, BAT *b[], int printoid,
 * int order, int printorderby)
 * @end multitable
 *
 * The functions to convert BATs into ASCII and the reverse use
 * internally defined formats. They are primarily meant for ease of
 * debugging and to a lesser extent for output processing.  Printing a
 * BAT is done essentially by looping through its components, printing
 * each association.  If an index is available, it will be used.
 *
 * The BATmultiprintf command assumes a set of BATs with corresponding
 * oid-s in the head columns. It performs the multijoin over them, and
 * prints the multi-column result on the file.
 */
gdk_export int BATprint(BAT *b);
gdk_export int BATprintf(stream *f, BAT *b);
gdk_export int BATmultiprintf(stream *f, int argc, BAT *argv[], int printoid, int order, int printorderby);

/*
 * @- BAT clustering
 * @multitable @columnfractions 0.08 0.7
 * @item BAT *
 * @tab BATsort (BAT *b)
 * @item BAT *
 * @tab BATsort_rev (BAT *b)
 * @item BAT *
 * @tab BATorder (BAT *b)
 * @item BAT *
 * @tab BATorder_rev (BAT *b)
 * @item BAT *
 * @tab BATrevert (BAT *b)
 * @item int
 * @tab BATordered (BAT *b)
 * @end multitable
 *
 * When working in a main-memory situation, clustering of data on
 * disk-pages is not important. Whenever mmap()-ed data is used
 * intensively, reducing the number of page faults is a hot issue.
 *
 * The above functions rearrange data in MonetDB heaps (used for
 * storing BUNs var-sized atoms, or accelerators). Applying these
 * clusterings will allow that MonetDB's main-memory oriented
 * algorithms work efficiently also in a disk-oriented context.
 *
 * The BATsort functions return a copy of the input BAT, sorted in
 * ascending order on the head column. BATordered starts a check on
 * the head values to see if they are ordered. The result is returned
 * and stored in the hsorted field of the BAT.  BATorder is similar to
 * BATsort, but sorts the BAT itself, rather than returning a copy
 * (BEWARE: this operation destroys the delta
 * information. TODO:fix). The BATrevert puts all the live BUNs of a
 * BAT in reverse order. It just reverses the sequence, so this does
 * not necessarily mean that they are sorted in reverse order!
 */
gdk_export BAT *BATsort(BAT *b);
gdk_export BAT *BATsort_rev(BAT *b);
gdk_export BAT *BATorder(BAT *b);
gdk_export BAT *BATorder_rev(BAT *b);
gdk_export BAT *BATrevert(BAT *b);
gdk_export int BATordered(BAT *b);
gdk_export int BATordered_rev(BAT *b);
gdk_export BAT *BATssort(BAT *b);
gdk_export BAT *BATssort_rev(BAT *b);
gdk_export gdk_return BATsubsort(BAT **sorted, BAT **order, BAT **groups, BAT *b, BAT *o, BAT *g, int reverse, int stable);


gdk_export void GDKqsort(void *h, void *t, const void *base, size_t n, int hs, int ts, int tpe);
gdk_export void GDKqsort_rev(void *h, void *t, const void *base, size_t n, int hs, int ts, int tpe);

#define BAThordered(b)	((b)->htype == TYPE_void || (b)->hsorted)
#define BATtordered(b)	((b)->ttype == TYPE_void || (b)->tsorted)
#define BAThrevordered(b) (((b)->htype == TYPE_void && (b)->hseqbase == oid_nil) || (b)->hrevsorted)
#define BATtrevordered(b) (((b)->ttype == TYPE_void && (b)->tseqbase == oid_nil) || (b)->trevsorted)
#define BAThdense(b)	(BAThvoid(b) && (b)->hseqbase != oid_nil)
#define BATtdense(b)	(BATtvoid(b) && (b)->tseqbase != oid_nil)
#define BAThvoid(b)	(((b)->hdense && (b)->hsorted) || (b)->htype==TYPE_void)
#define BATtvoid(b)	(((b)->tdense && (b)->tsorted) || (b)->ttype==TYPE_void)
#define BAThkey(b)	(b->hkey != FALSE || BAThdense(b))
#define BATtkey(b)	(b->tkey != FALSE || BATtdense(b))

/* set some properties that are trivial to deduce */
#define COLsettrivprop(b, col)						\
	do {								\
		if ((col)->type == TYPE_void) {				\
			if ((col)->seq == oid_nil) {			\
				if (!(col)->nil && (b)->batCount >= 1) { \
					(col)->nil = 1;			\
					(b)->batDirtydesc = 1;		\
				}					\
				if (!(col)->revsorted) {		\
					(col)->revsorted = 1;		\
					(b)->batDirtydesc = 1;		\
				}					\
			} else {					\
				if (!(col)->dense) {			\
					(col)->dense = 1;		\
					(b)->batDirtydesc = 1;		\
				}					\
				if (!(col)->nonil) {			\
					(col)->nonil = 1;		\
					(b)->batDirtydesc = 1;		\
				}					\
				if (!(col)->key) {			\
					(col)->key = 1;			\
					(b)->batDirtydesc = 1;		\
				}					\
				if ((col)->revsorted && (b)->batCount > 1) { \
					(col)->revsorted = 0;		\
					(b)->batDirtydesc = 1;		\
				}					\
			}						\
			if (!(col)->sorted) {				\
				(col)->sorted = 1;			\
				(b)->batDirtydesc = 1;			\
			}						\
		} else if ((b)->batCount <= 1) {			\
			oid sqbs;					\
			if (BATatoms[(col)->type].linear) {		\
				if (!(col)->sorted) {			\
					(col)->sorted = 1;		\
					(b)->batDirtydesc = 1;		\
				}					\
				if (!(col)->revsorted) {		\
					(col)->revsorted = 1;		\
					(b)->batDirtydesc = 1;		\
				}					\
			}						\
			if (!(col)->key) {				\
				(col)->key = 1;				\
				(b)->batDirtydesc = 1;			\
			}						\
			if ((b)->batCount == 0) {			\
				(col)->nonil = 1;			\
				(col)->nil = 0;				\
			} else if (!(col)->dense &&			\
				   (col)->type == TYPE_oid &&		\
				   (sqbs = ((oid *) (col)->heap.base)[(b)->batFirst]) != oid_nil) { \
				(col)->dense = 1;			\
				(col)->seq = sqbs;			\
				(col)->nonil = 1;			\
				(col)->nil = 0;				\
				(b)->batDirtydesc = 1;			\
			}						\
		}							\
		if (!BATatoms[(col)->type].linear) {			\
			if ((col)->sorted) {				\
				(col)->sorted = 0;			\
				(b)->batDirtydesc = 1;			\
			}						\
			if ((col)->revsorted) {				\
				(col)->revsorted = 0;			\
				(b)->batDirtydesc = 1;			\
			}						\
		}							\
	} while (0)
#define BATsettrivprop(b)			\
	do {					\
		COLsettrivprop((b), (b)->H);	\
		COLsettrivprop((b), (b)->T);	\
	} while (0)

/*
 * @+ BAT Buffer Pool
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab BBPfix (bat bi)
 * @item int
 * @tab BBPunfix (bat bi)
 * @item int
 * @tab BBPincref (bat bi, int logical)
 * @item int
 * @tab BBPdecref (bat bi, int logical)
 * @item void
 * @tab BBPhot (bat bi)
 * @item void
 * @tab BBPcold (bat bi)
 * @item str
 * @tab BBPname (bat bi)
 * @item bat
 * @tab BBPindex  (str nme)
 * @item BAT*
 * @tab BATdescriptor (bat bi)
 * @item bat
 * @tab BBPcacheid (BAT *b)
 * @end multitable
 *
 * The BAT Buffer Pool module contains the code to manage the storage
 * location of BATs. It uses two tables BBPlogical and BBphysical to
 * relate the BAT name with its corresponding file system name.  This
 * information is retained in an ASCII file within the database home
 * directory for ease of inspection. It is loaded upon restart of the
 * server and saved upon transaction commit (if necessary).
 *
 * The remaining BBP tables contain status information to load, swap
 * and migrate the BATs. The core table is BBPcache which contains a
 * pointer to the BAT descriptor with its heaps.  A zero entry means
 * that the file resides on disk. Otherwise it has been read or mapped
 * into memory.
 *
 * BATs loaded into memory are retained in a BAT buffer pool.  They
 * retain their position within the cache during their life cycle,
 * which make indexing BATs a stable operation.  Their descriptor can
 * be obtained using BBPcacheid.
 *
 * The BBPindex routine checks if a BAT with a certain name is
 * registered in the buffer pools. If so, it returns its BAT id.  The
 * BATdescriptor routine has a BAT id parameter, and returns a pointer
 * to the corresponding BAT record (after incrementing the reference
 * count). The BAT will be loaded into memory, if necessary.
 *
 * The structure of the BBP file obeys the tuple format for GDK.
 *
 * The status and BAT persistency information is encoded in the status
 * field.
 */
typedef struct {
	BAT *cache[2];		/* if loaded: BAT* handle + reverse */
	str logical[2];		/* logical name + reverse */
	str bak[2];		/* logical name + reverse backups */
	bat next[2];		/* next BBP slot in linked list */
	BATstore *desc;		/* the BAT descriptor */
	str physical;		/* dir + basename for storage */
	str options;		/* A string list of options */
	int refs;		/* in-memory references on which the loaded status of a BAT relies */
	int lrefs;		/* logical references on which the existence of a BAT relies */
	int lastused;		/* BBP LRU stamp */
	volatile int status;	/* status mask used for spin locking */
	/* MT_Id pid;           non-zero thread-id if this BAT is private */
} BBPrec;

gdk_export bat BBPlimit;
#define N_BBPINIT	1000
#if SIZEOF_VOID_P == 4
#define BBPINITLOG	11
#else
#define BBPINITLOG	14
#endif
#define BBPINIT		(1 << BBPINITLOG)
/* absolute maximum number of BATs is N_BBPINIT * BBPINIT */
gdk_export BBPrec *BBP[N_BBPINIT];

/* fast defines without checks; internal use only  */
#define BBP_cache(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].cache[(i)<0]
#define BBP_logical(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].logical[(i)<0]
#define BBP_bak(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].bak[(i)<0]
#define BBP_next(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].next[(i)<0]
#define BBP_physical(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].physical
#define BBP_options(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].options
#define BBP_desc(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].desc
#define BBP_refs(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].refs
#define BBP_lrefs(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].lrefs
#define BBP_lastused(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].lastused
#define BBP_status(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].status
#define BBP_pid(i)	BBP[ABS(i)>>BBPINITLOG][ABS(i)&(BBPINIT-1)].pid

/* macros that nicely check parameters */
#define BBPcacheid(b)	((b)->batCacheid)
#define BBPstatus(i)	(BBPcheck((i),"BBPstatus")?BBP_status(i):-1)
#define BBPcurstamp()	BBP_curstamp
#define BBPrefs(i)	(BBPcheck((i),"BBPrefs")?BBP_refs(i):-1)
#define BBPcache(i)	(BBPcheck((i),"BBPcache")?BBP_cache(i):(BAT*) NULL)
#define BBPname(i)							\
	(BBPcheck((i), "BBPname") ?					\
	 ((i) > 0 ?							\
	  BBP[(i) >> BBPINITLOG][(i) & (BBPINIT - 1)].logical[0] :	\
	  (BBP[-(i) >> BBPINITLOG][-(i) & (BBPINIT - 1)].logical[1] ?	\
	   BBP[-(i) >> BBPINITLOG][-(i) & (BBPINIT - 1)].logical[1] :	\
	   BBP[-(i) >> BBPINITLOG][-(i) & (BBPINIT - 1)].logical[0])) : \
	 "")
#define BBPvalid(i)	(BBP_logical(i) != NULL && *BBP_logical(i) != '.')
#define BATgetId(b)	BBPname((b)->batCacheid)
#define BBPfix(i)	BBPincref((i), FALSE)
#define BBPunfix(i)	BBPdecref((i), FALSE)

#define BBPRENAME_ALREADY	(-1)
#define BBPRENAME_ILLEGAL	(-2)
#define BBPRENAME_LONG		(-3)

gdk_export void BBPlock(const char *s);

gdk_export void BBPhot(bat b);
gdk_export void BBPcold(bat b);
gdk_export void BBPunlock(const char *s);

gdk_export str BBPlogical(bat b, str buf);
gdk_export str BBPphysical(bat b, str buf);
gdk_export int BBP_curstamp;
gdk_export BATstore *BBPgetdesc(bat i);
gdk_export BAT *BBPquickdesc(bat b, int delaccess);

/*
 * @+ GDK Extensibility
 * GDK can be extended with new atoms, search accelerators and storage
 * modes.
 *
 * @- Atomic Type Descriptors
 * The atomic types over which the binary associations are maintained
 * are described by an atom descriptor.
 *  @multitable @columnfractions 0.08 0.7
 * @item void
 * @tab ATOMproperty    (str   nme, char *property, int (*fcn)(), int val);
 * @item int
 * @tab ATOMindex       (char *nme);
 * @item int
 * @tab ATOMdump        ();
 * @item void
 * @tab ATOMdelete      (int id);
 * @item str
 * @tab ATOMname        (int id);
 * @item int
 * @tab ATOMsize        (int id);
 * @item int
 * @tab ATOMalign       (int id);
 * @item int
 * @tab ATOMvarsized    (int id);
 * @item ptr
 * @tab ATOMnilptr      (int id);
 * @item int
 * @tab ATOMfromstr     (int id, str s, int* len, ptr* v_dst);
 * @item int
 * @tab ATOMtostr       (int id, str s, int* len, ptr* v_dst);
 * @item hash_t
 * @tab ATOMhash        (int id, ptr val, in mask);
 * @item int
 * @tab ATOMcmp         (int id, ptr val_1, ptr val_2);
 * @item int
 * @tab ATOMconvert     (int id, ptr v, int direction);
 * @item int
 * @tab ATOMfix         (int id, ptr v);
 * @item int
 * @tab ATOMunfix       (int id, ptr v);
 * @item int
 * @tab ATOMheap        (int id, Heap *hp, size_t cap);
 * @item void
 * @tab ATOMheapconvert (int id, Heap *hp, int direction);
 * @item int
 * @tab ATOMheapcheck   (int id, Heap *hp, HeapRepair *hr);
 * @item int
 * @tab ATOMput         (int id, Heap *hp, BUN pos_dst, ptr val_src);
 * @item int
 * @tab ATOMdel         (int id, Heap *hp, BUN v_src);
 * @item int
 * @tab ATOMlen         (int id, ptr val);
 * @item ptr
 * @tab ATOMnil         (int id);
 * @item int
 * @tab ATOMformat      (int id, ptr val, char** buf);
 * @item int
 * @tab ATOMprint       (int id, ptr val, stream *fd);
 * @item ptr
 * @tab ATOMdup         (int id, ptr val );
 * @end multitable
 *
 * @- Atom Definition
 * User defined atomic types can be added to a running system with the
 * following interface:.
 *
 * @itemize
 * @item @emph{ATOMproperty()} registers a new atom definition, if
 * there is no atom registered yet under that name.  It then installs
 * the attribute of the named property.  Valid names are "size",
 * "align", "null", "fromstr", "tostr", "cmp", "hash", "put", "get",
 * "del", "length" and "heap".
 *
 * @item @emph{ATOMdelete()} unregisters an atom definition.
 *
 * @item @emph{ATOMindex()} looks up the atom descriptor with a certain name.
 * @end itemize
 *
 * @- Atom Manipulation
 *
 * @itemize
 * @item The @emph{ATOMname()} operation retrieves the name of an atom
 * using its id.
 *
 * @item The @emph{ATOMsize()} operation returns the atoms fixed size.
 *
 * @item The @emph{ATOMalign()} operation returns the atoms minimum
 * alignment. If the alignment info was not specified explicitly
 * during atom install, it assumes the maximum value of @verb{ {
 * }1,2,4,8@verb{ } } smaller than the atom size.
 *
 * @item The @emph{ATOMnilptr()} operation returns a pointer to the
 * nil-value of an atom. We usually take one dedicated value halfway
 * down the negative extreme of the atom range (if such a concept
 * fits), as the nil value.
 *
 * @item The @emph{ATOMnil()} operation returns a copy of the nil
 * value, allocated with GDKmalloc().
 *
 * @item The @emph{ATOMheap()} operation creates a new var-sized atom
 * heap in 'hp' with capacity 'cap'.
 *
 * @item The @emph{ATOMhash()} computes a hash index for a
 * value. `val' is a direct pointer to the atom value. Its return
 * value should be an hash_t between 0 and 'mask'.
 *
 * @item The @emph{ATOMcmp()} operation computes two atomic
 * values. Its parameters are pointers to atomic values.
 *
 * @item The @emph{ATOMlen()} operation computes the byte length for a
 * value.  `val' is a direct pointer to the atom value. Its return
 * value should be an integer between 0 and 'mask'.
 *
 * @item The @emph{ATOMdel()} operation deletes a var-sized atom from
 * its heap `hp'.  The integer byte-index of this value in the heap is
 * pointed to by `val_src'.
 *
 * @item The @emph{ATOMput()} operation inserts an atom `src_val' in a
 * BUN at `dst_pos'. This involves copying the fixed sized part in the
 * BUN. In case of a var-sized atom, this fixed sized part is an
 * integer byte-index into a heap of var-sized atoms. The atom is then
 * also copied into that heap `hp'.
 *
 * @item The @emph{ATOMfix()} and @emph{ATOMunfix()} operations do
 * bookkeeping on the number of references that a GDK application
 * maintains to the atom.  In MonetDB, we use this to count the number
 * of references directly, or through BATs that have columns of these
 * atoms. The only operator for which this is currently relevant is
 * BAT. The operators return the POST reference count to the
 * atom. BATs with fixable atoms may not be stored persistently.
 *
 * @item The @emph{ATOMfromstr()} parses an atom value from string
 * `s'. The memory allocation policy is the same as in
 * @emph{ATOMget()}. The return value is the number of parsed
 * characters.
 *
 * @item The @emph{ATOMprint()} prints an ASCII description of the
 * atom value pointed to by `val' on file descriptor `fd'. The return
 * value is the number of parsed characters.
 *
 * @item The @emph{ATOMformat()} is similar to @emph{ATOMprint()}. It
 * prints an atom on a newly allocated string. It must later be freed
 * with @strong{GDKfree}.  The number of characters written is
 * returned. This is minimally the size of the allocated buffer.
 *
 * @item The @emph{ATOMdup()} makes a copy of the given atom. The
 * storage needed for this is allocated and should be removed by the
 * user.
 * @end itemize
 *
 * These wrapper functions correspond closely to the interface
 * functions one has to provide for a user-defined atom. They
 * basically (with exception of @emph{ATOMput()}, @emph{ATOMprint()}
 * and @emph{ATOMformat()}) just have the atom id parameter prepended
 * to them.
 */
typedef struct {
	/* simple attributes */
	char name[IDLENGTH];
	int storage;		/* stored as another type? */
	short linear;		/* atom can be ordered linearly */
	short size;		/* fixed size of atom */
	short align;		/* alignment condition for values */
	short deleting;		/* set if unloading */
	int varsized;		/* variable-size or fixed-sized */

	/* automatically generated fields */
	ptr atomNull;		/* global nil value */

	/* generic (fixed + varsized atom) ADT functions */
	int (*atomFromStr) (const char *s, int *len, ptr *dst);
	int (*atomToStr) (str *s, int *len, const void *src);
	void *(*atomRead) (ptr a, stream *s, size_t cnt);
	int (*atomWrite) (const void *a, stream *s, size_t cnt);
	int (*atomCmp) (const void *v1, const void *v2);
	BUN (*atomHash) (const void *v);
	/* optional functions */
	void (*atomConvert) (ptr v, int direction);
	int (*atomFix) (const void *atom);
	int (*atomUnfix) (const void *atom);

	/* varsized atom-only ADT functions */
	var_t (*atomPut) (Heap *, var_t *off, const void *src);
	void (*atomDel) (Heap *, var_t *atom);
	int (*atomLen) (const void *atom);
	void (*atomHeap) (Heap *, size_t);
	/* optional functions */
	void (*atomHeapConvert) (Heap *, int direction);
	int (*atomHeapCheck) (Heap *, HeapRepair *);
} atomDesc;

gdk_export atomDesc BATatoms[];
gdk_export int GDKatomcnt;

gdk_export void ATOMproperty(char *nme, char *property, GDKfcn fcn, int val);
gdk_export int ATOMindex(char *nme);

gdk_export str ATOMname(int id);
gdk_export int ATOMlen(int id, const void *v);
gdk_export ptr ATOMnil(int id);
gdk_export int ATOMcmp(int id, const void *v_1, const void *v_2);
gdk_export int ATOMprint(int id, const void *val, stream *fd);
gdk_export int ATOMformat(int id, const void *val, char **buf);

gdk_export ptr ATOMdup(int id, const void *val);

/*
 * @- Unique OIDs
 * @multitable @columnfractions 0.08 0.7
 * @item oid
 * @tab
 * OIDseed (oid seed);
 * @item oid
 * @tab
 * OIDnew (oid inc);
 * @end multitable
 *
 * OIDs are special kinds of unsigned integers because the system
 * guarantees uniqueness. For system simplicity and performance, OIDs
 * are now represented as (signed) integers; however this is hidden in
 * the system internals and shouldn't affect semantics.
 *
 * The OIDnew(N) claims a range of N contiguous unique, unused OIDs,
 * and returns the starting value of this range.  The highest OIDBITS
 * designate site. [ DEPRECATED]
 */
gdk_export oid OIDbase(oid base);
gdk_export oid OIDnew(oid inc);

/*
 * @- Built-in Accelerator Functions
 *
 * @multitable @columnfractions 0.08 0.7
 * @item BAT*
 * @tab
 *  BAThash (BAT *b, BUN masksize)
 * @end multitable
 *
 * The current BAT implementation supports one search accelerator:
 * hashing. The routine BAThash makes sure that a hash accelerator on
 * the head of the BAT exists. A zero is returned upon failure to
 * create the supportive structures.
 *
 * The hash data structures are currently maintained during update
 * operations.
 */
gdk_export BAT *BAThash(BAT *b, BUN masksize);
gdk_export BAT *BAThashjoin(BAT *l, BAT *r, BUN estimate);

/* low level functions */

#define BATprepareHash(X) (((X)->H->hash == NULL) && !BAThash(X, 0))

/*
 * @- Column Imprints Functions
 *
 * @multitable @columnfractions 0.08 0.7
 * @item BAT*
 * @tab
 *  BATimprints (BAT *b)
 * @end multitable
 *
 * The column imprints index structure.
 *
 */

gdk_export void IMPSdestroy(BAT *b);
gdk_export BAT *BATimprints(BAT *b);

/*
 * @- Multilevel Storage Modes
 *
 * We should bring in the compressed mode as the first, maybe
 * built-in, mode. We could than add for instance HTTP remote storage,
 * SQL storage, and READONLY (cd-rom) storage.
 *
 * @+ GDK Utilities
 * Interfaces for memory management, error handling, thread management
 * and system information.
 *
 * @- GDK memory management
 * @multitable @columnfractions 0.08 0.8
 * @item void*
 * @tab GDKmalloc (size_t size)
 * @item void*
 * @tab GDKzalloc (size_t size)
 * @item void*
 * @tab GDKmallocmax (size_t size, size_t *maxsize, int emergency)
 * @item void*
 * @tab GDKrealloc (void* pold, size_t size)
 * @item void*
 * @tab GDKreallocmax (void* pold, size_t size, size_t *maxsize, int emergency)
 * @item void
 * @tab GDKfree (void* blk)
 * @item str
 * @tab GDKstrdup (str s)
 * @end multitable
 *
 * These utilities are primarily used to maintain control over
 * critical interfaces to the C library.  Moreover, the statistic
 * routines help in identifying performance and bottlenecks in the
 * current implementation.
 *
 * Compiled with -DMEMLEAKS the GDK memory management log their
 * activities, and are checked on inconsistent frees and memory leaks.
 */
#define GDK_HISTO_MAX_BIT	((int) (sizeof(size_t)<<3))

/* we prefer to use vm_alloc routines on size > GDKmmap */
gdk_export void *GDKmmap(const char *path, int mode, size_t len);

gdk_export size_t GDK_mem_bigsize;	/* size after which we use anonymous VM rather than malloc */
gdk_export size_t GDK_mem_maxsize;	/* max allowed size of committed memory */
gdk_export size_t GDK_vm_maxsize;	/* max allowed size of reserved vm */
gdk_export int	GDK_vm_trim;		/* allow trimming */

gdk_export size_t GDKmem_inuse(void);	/* RAM/swapmem that MonetDB is really using now */
gdk_export size_t GDKmem_cursize(void);	/* RAM/swapmem that MonetDB has claimed from OS */
gdk_export size_t GDKvm_cursize(void);	/* current MonetDB VM address space usage */

gdk_export void *GDKmalloc(size_t size);
gdk_export void *GDKzalloc(size_t size);
gdk_export void *GDKrealloc(void *pold, size_t size);
gdk_export void GDKfree(void *blk);
gdk_export str GDKstrdup(const char *s);

/*
 * @- GDK error handling
 *  @multitable @columnfractions 0.08 0.7
 * @item str
 * @tab
 *  GDKmessage
 * @item bit
 * @tab
 *  GDKfatal(str msg)
 * @item int
 * @tab
 *  GDKwarning(str msg)
 * @item int
 * @tab
 *  GDKerror (str msg)
 * @item int
 * @tab
 *  GDKgoterrors ()
 * @item int
 * @tab
 *  GDKsyserror (str msg)
 * @item str
 * @tab
 *  GDKerrbuf
 *  @item
 * @tab GDKsetbuf (str buf)
 * @end multitable
 *
 * The error handling mechanism is not sophisticated yet. Experience
 * should show if this mechanism is sufficient.  Most routines return
 * a pointer with zero to indicate an error.
 *
 * The error messages are also copied to standard output.  The last
 * error message is kept around in a global variable.
 *
 * Error messages can also be collected in a user-provided buffer,
 * instead of being echoed to a stream. This is a thread-specific
 * issue; you want to decide on the error mechanism on a
 * thread-specific basis.  This effect is established with
 * GDKsetbuf. The memory (de)allocation of this buffer, that must at
 * least be 1024 chars long, is entirely by the user. A pointer to
 * this buffer is kept in the pseudo-variable GDKerrbuf. Normally,
 * this is a NULL pointer.
 */
#define GDKMAXERRLEN	10240
#define GDKWARNING	"!WARNING: "
#define GDKERROR	"!ERROR: "
#define GDKMESSAGE	"!OS: "
#define GDKFATAL	"!FATAL: "

/* Data Distilleries uses ICU for internationalization of some MonetDB error messages */

gdk_export int GDKerror(_In_z_ _Printf_format_string_ const char *format, ...)
	__attribute__((__format__(__printf__, 1, 2)));
gdk_export int GDKsyserror(_In_z_ _Printf_format_string_ const char *format, ...)
	__attribute__((__format__(__printf__, 1, 2)));
gdk_export int GDKfatal(_In_z_ _Printf_format_string_ const char *format, ...)
	__attribute__((__format__(__printf__, 1, 2)));

/*
 * @
 */
#include "gdk_delta.h"
#include "gdk_search.h"
#include "gdk_atoms.h"
#include "gdk_bbp.h"
#include "gdk_utils.h"

/* functions defined in gdk_bat.c */
gdk_export BUN void_replace_bat(BAT *b, BAT *u, bit force);
gdk_export int void_inplace(BAT *b, oid id, const void *val, bit force);
gdk_export BAT *BATattach(int tt, const char *heapfile);

#ifdef NATIVE_WIN32
#ifdef _MSC_VER
#define fileno _fileno
#endif
#define fdopen _fdopen
#define putenv _putenv
#endif

/* also see VALget */
static inline const void *
VALptr(const ValRecord *v)
{
	switch (ATOMstorage(v->vtype)) {
	case TYPE_void: return (const void *) &v->val.oval;
	case TYPE_bte: return (const void *) &v->val.btval;
	case TYPE_sht: return (const void *) &v->val.shval;
	case TYPE_int: return (const void *) &v->val.ival;
	case TYPE_flt: return (const void *) &v->val.fval;
	case TYPE_dbl: return (const void *) &v->val.dval;
	case TYPE_lng: return (const void *) &v->val.lval;
	case TYPE_str: return (const void *) v->val.sval;
	default:       return (const void *) v->val.pval;
	}
}

/*
   See `man mserver5` or tools/mserver/mserver5.1
   for a documentation of the following debug options.
*/

#define THRDMASK	(1)
#define CHECKMASK	(1<<1)
#define CHECKDEBUG	if (GDKdebug & CHECKMASK)
#define MEMMASK		(1<<2)
#define MEMDEBUG	if (GDKdebug & MEMMASK)
#define PROPMASK	(1<<3)
#define PROPDEBUG	if (GDKdebug & PROPMASK)
#define IOMASK		(1<<4)
#define IODEBUG		if (GDKdebug & IOMASK)
#define BATMASK		(1<<5)
#define BATDEBUG	if (GDKdebug & BATMASK)
/* PARSEMASK not used anymore
#define PARSEMASK	(1<<6)
#define PARSEDEBUG	if (GDKdebug & PARSEMASK)
*/
#define PARMASK		(1<<7)
#define PARDEBUG	if (GDKdebug & PARMASK)
#define HEADLESSMASK	(1<<8)
#define HEADLESSDEBUG	if ( GDKdebug & HEADLESSMASK)
#define TMMASK		(1<<9)
#define TMDEBUG		if (GDKdebug & TMMASK)
#define TEMMASK		(1<<10)
#define TEMDEBUG	if (GDKdebug & TEMMASK)
/* DLMASK not used anymore
#define DLMASK		(1<<11)
#define DLDEBUG		if (GDKdebug & DLMASK)
*/
#define PERFMASK	(1<<12)
#define PERFDEBUG	if (GDKdebug & PERFMASK)
#define DELTAMASK	(1<<13)
#define DELTADEBUG	if (GDKdebug & DELTAMASK)
#define LOADMASK	(1<<14)
#define LOADDEBUG	if (GDKdebug & LOADMASK)
/* YACCMASK not used anymore
#define YACCMASK	(1<<15)
#define YACCDEBUG	if (GDKdebug & YACCMASK)
*/
/*
#define ?tcpip?		if (GDKdebug&(1<<16))
#define ?monet_multiplex?	if (GDKdebug&(1<<17))
#define ?ddbench?	if (GDKdebug&(1<<18))
#define ?ddbench?	if (GDKdebug&(1<<19))
#define ?ddbench?	if (GDKdebug&(1<<20))
*/
#define ALGOMASK	(1<<21)
#define ALGODEBUG	if (GDKdebug & ALGOMASK)
#define ESTIMASK	(1<<22)
#define ESTIDEBUG	if (GDKdebug & ESTIMASK)
/* XPROPMASK not used anymore
#define XPROPMASK	(1<<23)
#define XPROPDEBUG	if (GDKdebug & XPROPMASK)
*/

#define JOINPROPMASK	(1<<24)
#define JOINPROPCHK	if (!(GDKdebug & JOINPROPMASK))
#define DEADBEEFMASK	(1<<25)
#define DEADBEEFCHK	if (!(GDKdebug & DEADBEEFMASK))

#define ALLOCMASK	(1<<26)
#define ALLOCDEBUG	if (GDKdebug & ALLOCMASK)

/* M5, only; cf.,
 * monetdb5/mal/mal.h
 */
#define OPTMASK		(1<<27)
#define OPTDEBUG	if (GDKdebug & OPTMASK)

#define HEAPMASK	(1<<28)
#define HEAPDEBUG	if (GDKdebug & HEAPMASK)

#define FORCEMITOMASK	(1<<29)
#define FORCEMITODEBUG	if (GDKdebug & FORCEMITOMASK)

#define short_int_SWAP(s) ((short)(((0x00ff&(s))<<8) | ((0xff00&(s))>>8)))

#define normal_int_SWAP(i) (((0x000000ff&(i))<<24) | ((0x0000ff00&(i))<<8) | \
	               ((0x00ff0000&(i))>>8)  | ((0xff000000&(i))>>24))

#define long_long_SWAP(l) \
		((((lng)normal_int_SWAP(l))<<32) |\
		 (0xffffffff&normal_int_SWAP(l>>32)))

/*
 * The kernel maintains a central table of all active threads.  They
 * are indexed by their tid. The structure contains information on the
 * input/output file descriptors, which should be set before a
 * database operation is started. It ensures that output is delivered
 * to the proper client.
 *
 * The Thread structure should be ideally made directly accessible to
 * each thread. This speeds up access to tid and file descriptors.
 */
#define THREADS	1024
#define THREADDATA	16

typedef struct threadStruct {
	int tid;		/* logical ID by MonetDB; val == index into this array + 1 (0 is invalid) */
	MT_Id pid;		/* physical thread id (pointer-sized) from the OS thread library */
	str name;
	ptr data[THREADDATA];
	size_t sp;
} ThreadRec, *Thread;


gdk_export ThreadRec GDKthreads[THREADS];

gdk_export int THRgettid(void);
gdk_export Thread THRget(int tid);
gdk_export Thread THRnew(str name);
gdk_export void THRdel(Thread t);
gdk_export void THRsetdata(int, ptr);
gdk_export void *THRgetdata(int);
gdk_export int THRhighwater(void);
gdk_export int THRprintf(stream *s, _In_z_ _Printf_format_string_ const char *format, ...)
	__attribute__((__format__(__printf__, 2, 3)));

gdk_export void *THRdata[16];

#define GDKstdout	((stream*)THRdata[0])
#define GDKstdin	((stream*)THRdata[1])

#define GDKout		((stream*)THRgetdata(0))
#define GDKin		((stream*)THRgetdata(1))
#define GDKerrbuf	((char*)THRgetdata(2))
#define GDKsetbuf(x)	THRsetdata(2,(ptr)(x))
#define GDKerr		GDKout

#define THRget_errbuf(t)	((char*)t->data[2])
#define THRset_errbuf(t,b)	(t->data[2] = b)

#ifndef GDK_NOLINK

static inline bat
BBPcheck(register bat x, register const char *y)
{
	if (x && x != bat_nil) {
		register bat z = ABS(x);

		if (z >= BBPsize || BBP_logical(z) == NULL) {
			CHECKDEBUG THRprintf(GDKstdout,"#%s: range error %d\n", y, (int) x);
		} else {
			return z;
		}
	}
	return 0;
}

static inline BAT *
BATdescriptor(register bat i)
{
	register BAT *b = NULL;

	if (BBPcheck(i, "BATdescriptor")) {
		BBPfix(i);
		b = BBP_cache(i);
		if (b == NULL)
			b = BBPdescriptor(i);
	}
	return b;
}

static inline char *
Hpos(BATiter *bi, BUN p)
{
	bi->hvid = bi->b->hseqbase;
	if (bi->hvid != oid_nil)
		bi->hvid += p - BUNfirst(bi->b);
	return (char*)&bi->hvid;
}

static inline char *
Tpos(BATiter *bi, BUN p)
{
	bi->tvid = bi->b->tseqbase;
	if (bi->tvid != oid_nil)
		bi->tvid += p - BUNfirst(bi->b);
	return (char*)&bi->tvid;
}

static inline BAT *
BATmirror(register BAT *b)
{
	if (b == NULL)
		return NULL;
	return BBP_cache(-b->batCacheid);
}

#endif

/*
 * @+ Transaction Management
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab
 *  TMcommit ()
 * @item int
 * @tab
 *  TMabort ()
 * @item int
 * @tab
 *  TMsubcommit ()
 * @end multitable
 *
 * MonetDB by default offers a global transaction environment.  The
 * global transaction involves all activities on all persistent BATs
 * by all threads.  Each global transaction ends with either TMabort
 * or TMcommit, and immediately starts a new transaction.  TMcommit
 * implements atomic commit to disk on the collection of all
 * persistent BATs. For all persistent BATs, the global commit also
 * flushes the delta status for these BATs (see
 * BATcommit/BATabort). This allows to perform TMabort quickly in
 * memory (without re-reading all disk images from disk).  The
 * collection of which BATs is persistent is also part of the global
 * transaction state. All BATs that where persistent at the last
 * commit, but were made transient since then, are made persistent
 * again by TMabort.  In other words, BATs that are deleted, are only
 * physically deleted at TMcommit time. Until that time, rollback
 * (TMabort) is possible.
 *
 * Use of TMabort is currently NOT RECOMMENDED due to two bugs:
 *
 * @itemize
 * @item
 * TMabort after a failed %TMcommit@ does not bring us back to the
 * previous committed state; but to the state at the failed TMcommit.
 * @item
 * At runtime, TMabort does not undo BAT name changes, whereas a cold
 * MonetDB restart does.
 * @end itemize
 *
 * In effect, the problems with TMabort reduce the functionality of
 * the global transaction mechanism to consistent checkpointing at
 * each TMcommit. For many applications, consistent checkpointingis
 * enough.
 *
 * Extension modules exist that provide fine grained locking (lock
 * module) and Write Ahead Logging (sqlserver).  Applications that
 * need more fine-grained transactions, should build this on top of
 * these extension primitives.
 *
 * TMsubcommit is intended to quickly add or remove BATs from the
 * persistent set. In both cases, rollback is not necessary, such that
 * the commit protocol can be accelerated. It comes down to writing a
 * new BBP.dir.
 *
 * Its parameter is a BAT-of-BATs (in the tail); the persistence
 * status of that BAT is committed. We assume here that the calling
 * thread has exclusive access to these bats.  An error is reported if
 * you try to partially commit an already committed persistent BAT (it
 * needs the rollback mechanism).
 */
gdk_export int TMcommit(void);
gdk_export int TMabort(void);
gdk_export int TMsubcommit(BAT *bl);
gdk_export int TMsubcommit_list(bat *subcommit, int cnt);

/*
 * @- Delta Management
 *  @multitable @columnfractions 0.08 0.6
 * @item BAT *
 * @tab BATcommit (BAT *b)
 * @item BAT *
 * @tab BATfakeCommit (BAT *b)
 * @item BAT *
 * @tab BATundo (BAT *b)
 * @item BAT *
 * @tab BATprev (BAT *b)
 * @item BAT *
 * @tab BATalpha (BAT *b)
 * @item BAT *
 * @tab BATdelta (BAT *b)
 * @end multitable
 *
 * The BAT keeps track of updates with respect to a 'previous state'.
 * Do not confuse 'previous state' with 'stable' or
 * 'commited-on-disk', because these concepts are not always the
 * same. In particular, they diverge when BATcommit, BATfakecommit,
 * and BATundo are called explictly, bypassing the normal global
 * TMcommit protocol (some applications need that flexibility).
 *
 * BATcommit make the current BAT state the new 'stable state'.  This
 * happens inside the global TMcommit on all persistent BATs previous
 * to writing all bats to persistent storage using a BBPsync.
 *
 * EXPERT USE ONLY: The routine BATfakeCommit updates the delta
 * information on BATs and clears the dirty bit. This avoids any
 * copying to disk.  Expert usage only, as it bypasses the global
 * commit protocol, and changes may be lost after quitting or crashing
 * MonetDB.
 *
 * BATabort undo-s all changes since the previous state. The global
 * TMabort achieves a rollback to the previously committed state by
 * doing BATabort on all persistent bats.
 *
 * BUG: after a failed TMcommit, TMabort does not do anything because
 * TMcommit does the BATcommits @emph{before} attempting to sync to
 * disk instead of @sc{after} doing this.
 *
 * The previous state can also be queried. BATprev is a view on the
 * current BAT as it was in the previous state.  BATalpha shows only
 * the BUNs inserted since the previous state, and BATdelta the
 * deleted buns.
 *
 * CAVEAT: BATprev, BATalpha and BATdelta only return views if the
 * underlying BATs are read-only (often not the case when BATs are
 * being updated).  Otherwise, copies must be made anyway.
 */
gdk_export BAT *BATcommit(BAT *b);
gdk_export BAT *BATfakeCommit(BAT *b);
gdk_export BAT *BATundo(BAT *b);
gdk_export BAT *BATalpha(BAT *b);
gdk_export BAT *BATdelta(BAT *b);
gdk_export BAT *BATprev(BAT *b);

/*
 * @+ BAT Alignment and BAT views
 * @multitable @columnfractions 0.08 0.7
 * @item int
 * @tab ALIGNsynced (BAT* b1, BAT* b2)
 * @item int
 * @tab ALIGNsync   (BAT *b1, BAT *b2)
 * @item int
 * @tab ALIGNrelated (BAT *b1, BAT *b2)
 * @item int
 * @tab ALIGNsetH    ((BAT *dst, BAT *src)
 *
 * @item BAT*
 * @tab VIEWcreate   (BAT *h, BAT *t)
 * @item int
 * @tab isVIEW   (BAT *b)
 * @item bat
 * @tab VIEWhparent   (BAT *b)
 * @item bat
 * @tab VIEWtparent   (BAT *b)
 * @item BAT*
 * @tab VIEWhead     (BAT *b)
 * @item BAT*
 * @tab VIEWcombine  (BAT *b)
 * @item BAT*
 * @tab VIEWreset    (BAT *b)
 * @item BAT*
 * @tab BATmaterialize  (BAT *b)
 * @end multitable
 *
 * Alignments of two columns of a BAT means that the system knows
 * whether these two columns are exactly equal. Relatedness of two
 * BATs means that one pair of columns (either head or tail) of both
 * BATs is aligned. The first property is checked by ALIGNsynced, the
 * latter by ALIGNrelated.
 *
 * All algebraic BAT commands propagate the properties - including
 * alignment properly on their results.
 *
 * VIEW BATs are BATs that lend their storage from a parent BAT.  They
 * are just a descriptor that points to the data in this parent BAT. A
 * view is created with VIEWcreate. The cache id of the parent (if
 * any) is returned by VIEWhparent and VIEWtparent (otherwise it
 * returns 0).
 *
 * VIEW bats are read-only!!
 *
 * The VIEWcombine gives a view on a BAT that has two head columns of
 * the parent.  The VIEWhead constructs a BAT view that has the same
 * head column as the parent, but has a void column with seqbase=nil
 * in the tail. VIEWreset creates a normal BAT with the same contents
 * as its view parameter (it converts void columns with seqbase!=nil
 * to materialized oid columns).
 *
 * The BATmaterialize materializes a VIEW (TODO) or void bat inplace.
 * This is useful as materialization is usually needed for updates.
 */
gdk_export int ALIGNsynced(BAT *b1, BAT *b2);

gdk_export void BATassertProps(BAT *b);
gdk_export void BATderiveProps(BAT *b, int expensive);
gdk_export void BATderiveHeadProps(BAT *b, int expensive);

#define BATPROPS_QUICK  0	/* only derive easy (non-resource consuming) properties */
#define BATPROPS_ALL	1	/* derive all possible properties; no matter what cost (key=hash) */
#define BATPROPS_CHECK  3	/* BATPROPS_ALL, but start from scratch and report illegally set properties */

gdk_export BAT *VIEWcreate(BAT *h, BAT *t);
gdk_export BAT *VIEWcreate_(BAT *h, BAT *t, int stable);
gdk_export BAT *VIEWhead(BAT *b);
gdk_export BAT *VIEWhead_(BAT *b, int mode);
gdk_export BAT *VIEWcombine(BAT *b);
gdk_export BAT *BATmaterialize(BAT *b);
gdk_export BAT *BATmaterializeh(BAT *b);
gdk_export void VIEWbounds(BAT *b, BAT *view, BUN l, BUN h);

/* low level functions */
gdk_export int ALIGNsetH(BAT *b1, BAT *b2);

#define ALIGNset(x,y)	do {ALIGNsetH(x,y);ALIGNsetT(x,y);} while (0)
#define ALIGNsetT(x,y)	ALIGNsetH(BATmirror(x),BATmirror(y))
#define ALIGNins(x,y,f)	do {if (!(f)) VIEWchk(x,y,BAT_READ);(x)->halign=(x)->talign=0; } while (0)
#define ALIGNdel(x,y,f)	do {if (!(f)) VIEWchk(x,y,BAT_READ|BAT_APPEND);(x)->halign=(x)->talign=0; } while (0)
#define ALIGNinp(x,y,f) do {if (!(f)) VIEWchk(x,y,BAT_READ|BAT_APPEND);(x)->talign=0; } while (0)
#define ALIGNapp(x,y,f) do {if (!(f)) VIEWchk(x,y,BAT_READ);(x)->talign=0; } while (0)

#define BAThrestricted(b) (VIEWhparent(b) ? BBP_cache(VIEWhparent(b))->batRestricted : (b)->batRestricted)
#define BATtrestricted(b) (VIEWtparent(b) ? BBP_cache(VIEWtparent(b))->batRestricted : (b)->batRestricted)

/* The batRestricted field indicates whether a BAT is readonly.
 * we have modes: BAT_WRITE  = all permitted
 *                BAT_APPEND = append-only
 *                BAT_READ   = read-only
 * VIEW bats are always mapped read-only.
 */
#define	VIEWchk(x,y,z)							\
	do {								\
		if ((((x)->batRestricted & (z)) != 0) | ((x)->batSharecnt > 0)) { \
			GDKerror("%s: access denied to %s, aborting.\n", \
				 (y), BATgetId(x));			\
			return 0;					\
		}							\
	} while (0)

/* the parentid in a VIEW is correct for the normal view. We must
 * correct for the reversed view. A special case are the VIEWcombine
 * bats, these always refer to the same parent column (i.e. no
 * correction needed)
 */
#define isVIEW(x)							\
	((x)->H->heap.parentid ||					\
	 (x)->T->heap.parentid ||					\
	 ((x)->H->vheap && (x)->H->vheap->parentid != ABS((x)->batCacheid)) || \
	 ((x)->T->vheap && (x)->T->vheap->parentid != ABS((x)->batCacheid)))

#define isVIEWCOMBINE(x) ((x)->H == (x)->T)
#define VIEWhparent(x)	((x)->H->heap.parentid)
#define VIEWvhparent(x)	(((x)->H->vheap==NULL||(x)->H->vheap->parentid==ABS((x)->batCacheid))?0:(x)->H->vheap->parentid)
#define VIEWtparent(x)	((x)->T->heap.parentid)
#define VIEWvtparent(x)	(((x)->T->vheap==NULL||(x)->T->vheap->parentid==ABS((x)->batCacheid))?0:(x)->T->vheap->parentid)

/* VIEWparentcol(b) tells whether the head column was inherited from
 * the parent "as is". We must check whether the type was not
 * overridden in the view.
 */
#define VIEWparentcol(b)					\
	((VIEWhparent(b) && (b)->htype				\
	  && (b)->htype == BBP_cache(VIEWhparent(b))->htype)	\
	 ?VIEWhparent(b):0)
/*
 * @+ BAT Iterators
 *  @multitable @columnfractions 0.15 0.7
 * @item BATloop
 * @tab
 *  (BAT *b; BUN p, BUN q)
 * @item BATloopDEL
 * @tab
 *  (BAT *b; BUN p; BUN q; int dummy)
 * @item DELloop
 * @tab
 *  (BAT *b; BUN p, BUN q, int dummy)
 * @item HASHloop
 * @tab
 *  (BAT *b; Hash *h, size_t dummy; ptr value)
 * @item HASHloop_bit
 * @tab
 *  (BAT *b; Hash *h, size_t idx; bit *value, BUN w)
 * @item HASHloop_bte
 * @tab
 *  (BAT *b; Hash *h, size_t idx; bte *value, BUN w)
 * @item HASHloop_sht
 * @tab
 *  (BAT *b; Hash *h, size_t idx; sht *value, BUN w)
 * @item HASHloop_bat
 * @tab
 *  (BAT *b; Hash *h, size_t idx; bat *value, BUN w)
 * @item HASHloop_ptr
 * @tab
 *  (BAT *b; Hash *h, size_t idx; ptr *value, BUN w)
 * @item HASHloop_int
 * @tab
 *  (BAT *b; Hash *h, size_t idx; int *value, BUN w)
 * @item HASHloop_oid
 * @tab
 *  (BAT *b; Hash *h, size_t idx; oid *value, BUN w)
 * @item HASHloop_wrd
 * @tab
 *  (BAT *b; Hash *h, size_t idx; wrd *value, BUN w)
 * @item HASHloop_flt
 * @tab
 *  (BAT *b; Hash *h, size_t idx; flt *value, BUN w)
 * @item HASHloop_lng
 * @tab
 *  (BAT *b; Hash *h, size_t idx; lng *value, BUN w)
 * @item HASHloop_dbl
 * @tab
 *  (BAT *b; Hash *h, size_t idx; dbl *value, BUN w)
 * @item  HASHloop_str
 * @tab
 *  (BAT *b; Hash *h, size_t idx; str value, BUN w)
 * @item HASHlooploc
 * @tab
 *  (BAT *b; Hash *h, size_t idx; ptr value, BUN w)
 * @item HASHloopvar
 * @tab
 *  (BAT *b; Hash *h, size_t idx; ptr value, BUN w)
 * @item SORTloop
 * @tab
 *  (BAT *b,p,q,tl,th,s)
 * @end multitable
 *
 * The @emph{BATloop()} looks like a function call, but is actually a
 * macro.  The following example gives an indication of how they are
 * to be used:
 * @verbatim
 * void
 * print_a_bat(BAT *b)
 * {
 *	BATiter bi = bat_iterator(b);
 *	BUN p, q;
 *
 *	BATloop(b, p, q)
 *		printf("Element %3d has value %d\n",
 *			   *(int*) BUNhead(bi, p), *(int*) BUNtail(bi, p));
 * }
 * @end verbatim
 *
 * @- simple sequential scan
 * The first parameter is a BAT, the p and q are BUN pointers, where p
 * is the iteration variable.
 */
#define BATloop(r, p, q)					\
	for (q = BUNlast(r), p = BUNfirst(r);p < q; p++)

/*
 * @- batloop where the current element can be deleted/updated
 * Normally it is strictly forbidden to update the BAT over which is
 * being iterated, or delete the current element. This can only be
 * done with the specialized batloop below. When doing a delete, do
 * not forget to update the current pointer with a p = BUNdelete(b,p)
 * (the delete may modify the current pointer p).  After the
 * delete/update has taken place, the pointer p is in an inconsistent
 * state till the next iteration of the batloop starts.
 */
#define BATloopDEL(r, p, q)						\
	for (p = BUNfirst(r), q = BUNlast(r); p < q; q = MIN(q,BUNlast(r)), p++)

/*
 * @- sequential scan over deleted BUNs
 * Stable BUNS that were deleted, are conserved to transaction
 * end. You may inspect these data items.  Again, the b is a BAT, p
 * and q are BUNs, where p is the iteration variable.
 */
#define DELloop(b, p, q)						\
	for (q = (b)->batFirst, p = (b)->batDeleted; p < q; p++)

/*
 * @- hash-table supported loop over BUNs
 * The first parameter `b' is a BAT, the second (`h') should point to
 * `b->H->hash', and `v' a pointer to an atomic value (corresponding
 * to the head column of `b'). The 'hb' is an integer index, pointing
 * out the `hb'-th BUN.
 */
#define GDK_STREQ(l,r) (*(char*) (l) == *(char*) (r) && !strcmp(l,r))

#define HASHloop(bi, h, hb, v)					\
	for (hb = HASHget(h, HASHprobe((h), v));			\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (ATOMcmp(h->type, v, BUNhead(bi, hb)) == 0)
#define HASHloop_str_hv(bi, h, hb, v)				\
	for (hb = HASHget((h),((BUN *) (v))[-1]&(h)->mask);	\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (GDK_STREQ(v, BUNhvar(bi, hb)))
#define HASHloop_str(bi, h, hb, v)			\
	for (hb = HASHget((h),strHash(v)&(h)->mask);	\
	     hb != HASHnil(h);				\
	     hb = HASHgetlink(h,hb))			\
		if (GDK_STREQ(v, BUNhvar(bi, hb)))

/*
 * For string search, we can optimize if the string heap has
 * eliminated all doubles. This is the case when not too many
 * different strings are stored in the heap. You can check this with
 * the macro strElimDoubles() If so, we can just compare integer index
 * numbers instead of strings:
 */
#define HASHloop_fstr(bi, h, hb, idx, v)				\
	for (hb = HASHget(h, strHash(v)&h->mask), idx = strLocate((bi.b)->H->vheap,v); \
	     hb != HASHnil(h); hb = HASHgetlink(h,hb))				\
		if (VarHeapValRaw((bi).b->H->heap.base, hb, (bi).b->H->width) == idx)
/*
 * The following example shows how the hashloop is used:
 *
 * @verbatim
 * void
 * print_books(BAT *author_books, str author)
 * {
 *         BAT *b = author_books;
 *         BUN i;
 *
 *         printf("%s\n==================\n", author);
 *         HASHloop(b, (b)->H->hash, i, author)
 *			printf("%s\n", ((str) BUNtail(b, i));
 * }
 * @end verbatim
 *
 * Note that for optimization purposes, we could have used a
 * HASHloop_str instead, and also a BUNtvar instead of a BUNtail
 * (since we know the tail-type of author_books is string, hence
 * variable-sized). However, this would make the code less general.
 *
 * @- specialized hashloops
 * HASHloops come in various flavors, from the general HASHloop, as
 * above, to specialized versions (for speed) where the type is known
 * (e.g. HASHloop_int), or the fact that the atom is fixed-sized
 * (HASHlooploc) or variable-sized (HASHloopvar).
 */
#define HASHlooploc(bi, h, hb, v)				\
	for (hb = HASHget(h, HASHprobe(h, v));			\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (ATOMcmp(h->type, v, BUNhloc(bi, hb)) == 0)
#define HASHloopvar(bi, h, hb, v)				\
	for (hb = HASHget(h,HASHprobe(h, v));			\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (ATOMcmp(h->type, v, BUNhvar(bi, hb)) == 0)

#define HASHloop_TYPE(bi, h, hb, v, TYPE)			\
	for (hb = HASHget(h, hash_##TYPE(h, v));			\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (simple_EQ(v, BUNhloc(bi, hb), TYPE))

#define HASHloop_bit(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, bte)
#define HASHloop_bte(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, bte)
#define HASHloop_sht(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, sht)
#define HASHloop_int(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, int)
#define HASHloop_wrd(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, wrd)
#define HASHloop_lng(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, lng)
#define HASHloop_oid(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, oid)
#define HASHloop_bat(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, bat)
#define HASHloop_flt(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, flt)
#define HASHloop_dbl(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, dbl)
#define HASHloop_ptr(bi, h, hb, v)	HASHloop_TYPE(bi, h, hb, v, ptr)

#define HASHloop_any(bi, h, hb, v)				\
	for (hb = HASHget(h, hash_any(h, v));			\
	     hb != HASHnil(h);					\
	     hb = HASHgetlink(h,hb))				\
		if (atom_EQ(v, BUNhead(bi, hb), (bi).b->htype))

/*
 * @- loop over a BAT with ordered tail
 * Here we loop over a BAT with an ordered tail column (see for
 * instance BATsort). Again, 'p' and 'q' are iteration variables,
 * where 'p' points at the current BUN. 'tl' and 'th' are pointers to
 * atom corresponding to the minimum (included) and maximum (included)
 * bound in the selected range of BUNs. A nil-value means that there
 * is no bound.  The 's' finally is an integer denoting the bunsize,
 * used for speed.
 */
#define SORTloop(b, p, q, tl, th)					\
	if (!BATtordered(b))						\
		GDKerror("SORTloop: BAT not sorted.\n");		\
	else for (p = (ATOMcmp((b)->ttype, tl, ATOMnilptr((b)->ttype)) ? \
		       SORTfndfirst((b), tl) : BUNfirst(b)),		\
		  q = (ATOMcmp((b)->ttype, th, ATOMnilptr((b)->ttype)) ? \
		       SORTfndlast((b), th) : BUNlast(b));		\
		  p < q;						\
		  p++)

/*
 * @+ Common BAT Operations
 * Much used, but not necessarily kernel-operations on BATs.
 *
 * @- BAT aggregates
 * @multitable @columnfractions 0.08 0.7
 * @item BAT*
 * @tab
 *  BAThistogram(BAT *b)
 * @item BAT*
 * @end multitable
 *
 * The routine BAThistogram produces a new BAT with a frequency
 * distribution of the tail of its operand.
 *
 * For each BAT we maintain its dimensions as separately accessible
 * properties. They can be used to improve query processing at higher
 * levels.
 */

#define GDK_AGGR_SIZE 1
#define GDK_AGGR_CARD 2
#define GDK_MIN_VALUE 3
#define GDK_MAX_VALUE 4

gdk_export void PROPdestroy(PROPrec *p);
gdk_export PROPrec * BATgetprop(BAT *b, int idx);
gdk_export void BATsetprop(BAT *b, int idx, int type, void *v);
gdk_export BAT *BAThistogram(BAT *b);
gdk_export int BATtopN(BAT *b, BUN topN);	/* used in monet5/src/modules/kernel/algebra.mx */

/*
 * @- Alignment transformations
 * Some classes of algebraic operators transform a sequence in an
 * input BAT always in the same way in the output result. An example
 * are the @{X@}() function (including histogram(b), which is
 * identical to @{count@}(b.reverse)).  That is to say, if
 * synced(b2,b2) => synced(@{X@}(b1),@{Y@}(b2))
 *
 * Another example is b.fetch(position-bat). If synced(b2,b2) and the
 * same position-bat is fetched with, the results will again be
 * synced.  This can be mimicked by transforming the
 * @emph{alignment-id} of the input BAT with a one-way function onto
 * the result.
 *
 * We use @strong{output->halign = NOID_AGGR(input->halign)} for the
 * @strong{output = @{X@}(input)} case, and @strong{output->align =
 * NOID_MULT(input1->align,input2->halign)} for the fetch.
 */
#define AGGR_MAGIC	111
#define NOID(x)		((oid)(x))
#define NOID_AGGR(x)	NOID_MULT(AGGR_MAGIC,x)
#define NOID_MULT(x,y)	NOID( (lng)(y)*(lng)(x) )

/*
 * @- BAT relational operators
 *  @multitable @columnfractions 0.08 0.7
 * @item BAT *
 * @tab BATjoin (BAT *l, BAT *r, BUN estimate)
 * @item BAT *
 * @tab BATouterjoin (BAT *l, BAT *r, BUN estimate)
 * @item BAT *
 * @tab BATthetajoin (BAT *l, BAT *r, int mode, BUN estimate)
 * @item BAT *
 * @tab BATsemijoin (BAT *l, BAT *r)
 * @item BAT *
 * @tab BATselect (BAT *b, ptr tl, ptr th)
 * @item BAT *
 * @tab BATfragment (BAT *b, ptr l, ptr h, ptr L, ptr H)
 * @item
 * @item BAT *
 * @tab BATsunique (BAT *b)
 * @item BAT *
 * @tab BATkunique (BAT *b)
 * @item BAT *
 * @tab BATsunion (BAT *b, BAT *c)
 * @item BAT *
 * @tab BATkunion (BAT *b, BAT *c)
 * @item BAT *
 * @tab BATsintersect (BAT *b, BAT *c)
 * @item BAT *
 * @tab BATkintersect (BAT *b, BAT *c)
 * @item BAT *
 * @tab BATsdiff (BAT *b, BAT *c)
 * @item BAT *
 * @tab BATkdiff (BAT *b, BAT *c)
 * @end multitable
 *
 * The BAT library comes with a full-fledged collection of relational
 * operators. The two selection operators BATselect and BATfragment
 * produce a partial copy of the BAT. The former performs a search on
 * the tail; the latter considers both dimensions.  The BATselect
 * operation takes two inclusive ranges as search arguments.
 * Interpretation of a NULL argument depends on the position, i.e. a
 * domain lower or upper bound.
 *
 * The operation BATsort sorts the BAT on the header and produces a
 * new BAT. A side effect is the clustering of the BAT store on the
 * sort key.
 *
 * The BATjoin over R[A, B] and S[C, D] performs an equi-join over B
 * and C. It results in a BAT over A and D.  The BATouterjoin
 * implements a left outerjoin over the BATs involved.  The
 * BATsemijoin over R[A, B] and S[C, D] produces the subset of R[A, B]
 * that satisfies the semijoin over A and C.
 *
 * The full-materialization policy intermediate results in MonetDB
 * means that a join can produce an arbitrarily large result and choke
 * the system. The Data Distilleries tool therefore first computes the
 * join result size before the actual join (better waste time than
 * crash the server). To exploit that perfect result size knowledge,
 * an result-size estimate parameter was added to all equi-join
 * implementations.  TODO: add this for
 * semijoin/select/unique/diff/intersect
 *
 * The routine BATsunique considers both dimensions in the double
 * elimination it performs; it produces a set.  The routine BATtunique
 * considers only the head column, and produces a unique head column.
 *
 * BATs that satisfy the set property can be further processed with
 * the set operations BATsunion, BATsintersect, and BATsdiff.  The
 * same operations are also available in versions that only look at
 * the head column:BATkunion, BATkdiff, and BATkintersect (which
 * shares its implementation with BATsemijoin).  @- modes for
 * thethajoin
 */
#define JOIN_EQ		0
#define JOIN_LT		(-1)
#define JOIN_LE		(-2)
#define JOIN_GT		1
#define JOIN_GE		2
#define JOIN_BAND	3

gdk_export BAT *BATsubselect(BAT *b, BAT *s, const void *tl, const void *th, int li, int hi, int anti);
gdk_export BAT *BATthetasubselect(BAT *b, BAT *s, const void *val, const char *op);
gdk_export BAT *BATselect_(BAT *b, const void *tl, const void *th, bit li, bit hi);
gdk_export BAT *BATuselect_(BAT *b, const void *tl, const void *th, bit li, bit hi);
gdk_export BAT *BATantiuselect_(BAT *b, const void *tl, const void *th, bit li, bit hi);
gdk_export BAT *BATselect(BAT *b, const void *tl, const void *th);
gdk_export BAT *BATuselect(BAT *b, const void *tl, const void *th);
gdk_export BAT *BATrestrict(BAT *b, const void *hl, const void *hh, const void *tl, const void *th);

gdk_export BAT *BATconstant(int tt, const void *val, BUN cnt);
gdk_export BAT *BATconst(BAT *l, int tt, const void *val);
gdk_export BAT *BATthetajoin(BAT *l, BAT *r, int mode, BUN estimate);
gdk_export BAT *BATsemijoin(BAT *l, BAT *r);
gdk_export BAT *BATmergejoin(BAT *l, BAT *r, BUN estimate);
gdk_export BAT *BATjoin(BAT *l, BAT *r, BUN estimate);
gdk_export BAT *BATantijoin(BAT *l, BAT *r);
gdk_export BAT *BATleftjoin(BAT *l, BAT *r, BUN estimate);
gdk_export BAT *BATouterjoin(BAT *l, BAT *r, BUN estimate);
gdk_export BAT *BATcross(BAT *l, BAT *r);

gdk_export gdk_return BATsubleftjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate);
gdk_export gdk_return BATsubouterjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate);
gdk_export gdk_return BATsubthetajoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, const char *op, BUN estimate);
gdk_export gdk_return BATsubsemijoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate);
gdk_export gdk_return BATsubjoin(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, BUN estimate);
gdk_export BAT *BATproject(BAT *l, BAT *r);

gdk_export BAT *BATslice(BAT *b, BUN low, BUN high);
gdk_export BAT *BATfetch(BAT *b, BAT *s);
gdk_export BAT *BATfetchjoin(BAT *b, BAT *s, BUN estimate);
gdk_export BAT *BATleftfetchjoin(BAT *b, BAT *s, BUN estimate);

gdk_export BAT *BATsunique(BAT *b);
gdk_export BAT *BATkunique(BAT *b);
gdk_export BAT *BATsintersect(BAT *b, BAT *c);
gdk_export BAT *BATkintersect(BAT *b, BAT *c);
gdk_export BAT *BATsunion(BAT *b, BAT *c);
gdk_export BAT *BATkunion(BAT *b, BAT *c);
gdk_export BAT *BATsdiff(BAT *b, BAT *c);
gdk_export BAT *BATkdiff(BAT *b, BAT *c);

gdk_export BAT *BATmergecand(BAT *a, BAT *b);
gdk_export BAT *BATintersectcand(BAT *a, BAT *b);

#include "gdk_calc.h"

/*
 * @- BAT sample operators
 *
 * @multitable @columnfractions 0.08 0.7
 * @item BAT *
 * @tab BATsample (BAT *b, n)
 * @end multitable
 *
 * The routine BATsample returns a random sample on n BUNs of a BAT.
 *
 */
gdk_export BAT *BATsample(BAT *b, BUN n);
gdk_export BAT *BATsample_(BAT *b, BUN n); /* version that expects void head and returns oids */

/* generic n-ary multijoin beast, with defines to interpret retval */
#define MULTIJOIN_SORTED(r)	((char*) &r)[0]
#define MULTIJOIN_KEY(r)	((char*) &r)[1]
#define MULTIJOIN_SYNCED(r)	((char*) &r)[2]
#define MULTIJOIN_LEAD(r)	((char*) &r)[3]

typedef void (*ColFcn) (ptr, const void *);
typedef void (*RowFcn) (ptr, ptr *);
/*
 *
 */
#define ILLEGALVALUE	((ptr)-1L)
#define MAXPARAMS	32

#endif /* _GDK_H_ */
