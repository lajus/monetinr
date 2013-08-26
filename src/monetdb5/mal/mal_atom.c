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
 * @a M.L.Kersten
 * @- User Defined Types
 * MonetDB supports an extensible type system to accomodate a wide
 * spectrum of database kernels and application needs.
 * The type administration keeps track of their properties and
 * provides access to the underlying implementations.
 *
 * MAL recognizes the definition of a new
 * type by replacing the @sc{module} keyword with @sc{atom}.
 * Atoms definitions require special care, because their definition and
 * properties should be communicated with the kernel library.
 * The commands defined in an @sc{atom } block are screened as of interest
 * to the library.
 *
 * MonetDB comes with the hardwired types @sc{bit, bte, sht, int, lng, oid, flt,
 * dbl, str} and @sc{bat}, the representation of a bat identifier.
 * The kernel code has been optimized to deal with these types efficiently,
 * i.e. without unnecessary function call overheads.
 *
 * A small collection of user-defined @sc{atom} types is shipped with the sysem.
 * They implement types considered essential for end-user applications,
 * such as @sc{color, date, daytime,  time, timestamp, timezone, blob},
 * and @sc{inet, url}.
 * They are implemented using the type extension mechanism described below.
 * As such, they provide examples for future extensions.
 * A concrete example is the 'blob' datatype in the MonetDB atom module
 * library(see ../modules/atoms/blob.mx)
 *
 * @- Defining your own types
 * For the courageous at heart, you may enter the difficult world
 * of extending the kernel library. The easiest way is to derive
 * the atom modules from one shipped in the source distributed.
 * More involved atomary types require a study of the
 * documentation associated with the atom structures (gdk_atoms),
 * because you have to develop a handful routines complying with the
 * signatures required in the kernel library.
 * They are registered upon loading the @sc{atom} module.
 * @-
 * The atom registration functions perform the necessary
 * type checks, but relies on the user to comply with this signature in
 * its C-implementation. The ruler calls are part of a module
 * initialization routine.
 * @-
 * Functions passed to the GDK kernel are not directly accessible
 * as MAL routines, because their implementation requires a
 * GDK-specific signature. (See GDK documentation)
 * They are renamed to an non-parseable function, effectively shielding
 * them from the MAL programmer.
 * @-
 * This feature is of particular interest to system experts.
 * It is not meant for end-users trying to intruduce record- or
 * struct-like objects in the database. They better decompose
 * the complex object structure and represent the components in
 * different BATs.
 */
/*
 * Every MAL command introduced in an atom module should be checked
 * to detect overloading of a predefined function.
 * Subsequently, we update the BAT atom structure.
 * The function signatures should be parameter-less, which
 * enables additional functions with the same name to appear
 * as ordinary mal operators.
 *
 * A few fields are set only once, at creation time.
 * They should be implemented with parameter-less functions.
 */
#include "monetdb_config.h"
#include "mal_atom.h"
#include "mal_namespace.h"
#include "mal_exception.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

static void setAtomName(InstrPtr pci)
{
	char buf[MAXPATHLEN];
	snprintf(buf, MAXPATHLEN, "#%s", getFunctionId(pci));
	setFunctionId(pci, putName(buf, strlen(buf)));
}

int malAtomProperty(MalBlkPtr mb, InstrPtr pci)
{
	str name;
	int tpe;
	(void)mb;  /* fool compilers */
	assert(pci != 0);
	name = getFunctionId(pci);
	tpe = getTypeIndex(getModuleId(pci), (int)strlen(getModuleId(pci)), TYPE_any);
	if (tpe < 0 || tpe >= GDKatomcnt)
		return 0;
	assert(pci->fcn != NULL);
	switch (name[0]) {
	case 'd':
		if (idcmp("del", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomDel = (void (*)(Heap *, var_t *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'c':
		if (idcmp("cmp", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomCmp = (int (*)(const void *, const void *))pci->fcn;
			BATatoms[tpe].linear = 1;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("convert", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomConvert = (void (*)(ptr, int))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'f':
		if (idcmp("fromstr", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomFromStr = (int (*)(const char *, int *, ptr *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("fix", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomFix = (int (*)(const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'h':
		if (idcmp("heap", name) == 0 && pci->argc == 1) {
			/* heap function makes an atom varsized */
			BATatoms[tpe].size = sizeof(var_t);
			assert_shift_width(ATOMelmshift(BATatoms[tpe].size), BATatoms[tpe].size);
			BATatoms[tpe].varsized = 1;
			BATatoms[tpe].align = sizeof(var_t);
			BATatoms[tpe].atomHeap = (void (*)(Heap *, size_t))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("heapconvert", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomHeapConvert = (void (*)(Heap *, int))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("hash", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomHash = (BUN (*)(const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("heapcheck", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomHeapCheck = (int (*)(Heap *, HeapRepair *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'l':
		if (idcmp("length", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomLen = (int (*)(const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'n':
		if (idcmp("null", name) == 0 && pci->argc == 1) {
			ptr atmnull = ((ptr (*)(void))pci->fcn)();

			BATatoms[tpe].atomNull = atmnull;
			setAtomName(pci);
			return 1;
		}
		if (idcmp("nequal", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomCmp = (int (*)(const void *, const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'p':
		if (idcmp("put", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomPut = (var_t (*)(Heap *, var_t *, const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 's':
		if (idcmp("storage", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].storage = (*(long (*)(void))pci->fcn)();
			setAtomName(pci);
			return 1;
		}
		break;
	case 't':
		if (idcmp("tostr", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomToStr = (int (*)(str *, int *, const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'u':
		if (idcmp("unfix", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomUnfix = (int (*)(const void *))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'v':
		if (idcmp("varsized", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].varsized = (*(long (*)(void))pci->fcn)();
			setAtomName(pci);
			return 1;
		}
		break;
	case 'r':
		if (idcmp("read", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomRead = (void *(*)(void *, stream *, size_t))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	case 'w':
		if (idcmp("write", name) == 0 && pci->argc == 1) {
			BATatoms[tpe].atomWrite = (int (*)(const void *, stream *, size_t))pci->fcn;
			setAtomName(pci);
			return 1;
		}
		break;
	}
	return 0;
}
/*
 * @-
 * Atoms are constructed incrementally in the kernel using the
 * ATOMproperty function. It takes an existing type as a base
 * to derive a new one.
 * The most tedisous work is to check the signature types of the functions
 * acceptable for the kernel.
 */

void malAtomDefinition(stream *out, str name, int tpe)
{
	int i;

	if (strlen(name) >= IDLENGTH) {
		showException(out, SYNTAX, "atomDefinition", "Atom name '%s' too long", name);
		return;
	}
	if (ATOMindex(name) >= 0) {
		showException(out, TYPE, "atomDefinition", "Redefinition of atom '%s'", name);
		return;
	}
	if (tpe < 0 || tpe >= GDKatomcnt) {
		showException(out, TYPE, "atomDefinition", "Undefined atom inheritance '%s'", name);
		return;
	}

	ATOMproperty(name, "", (int (*)()) 0, 0);
	if (strlen(name) >= sizeof(BATatoms[0].name))
		return;
	i = ATOMindex(name);
	/* overload atom ? */
	if (tpe) {
		BATatoms[i] = BATatoms[tpe];
		strncpy(BATatoms[i].name, name, sizeof(BATatoms[i].name));
		BATatoms[i].name[sizeof(BATatoms[i].name) - 1] = 0; /* make coverity happy */
		BATatoms[i].storage = BATatoms[tpe].storage;
	} else { /* cannot overload void atoms */
		BATatoms[i].storage = i;
		BATatoms[i].linear = 0;
	}
}
/*
 * @-
 * User defined modules may introduce fixed sized types
 * to store information in BATs.
 */
int malAtomFixed(int size, int align, char *name)
{
	int i = 0;

	ATOMproperty(name, "", (int (*)()) 0, 0);
	if (strlen(name) >= sizeof(BATatoms[0].name))
		return -1;
	i = ATOMindex(name);
	BATatoms[i] = BATatoms[TYPE_bte];
	strncpy(BATatoms[i].name, name, sizeof(BATatoms[i].name));
	BATatoms[i].name[sizeof(BATatoms[i].name) - 1] = 0;
	BATatoms[i].storage = i;
	BATatoms[i].size = size;
	assert_shift_width(ATOMelmshift(BATatoms[i].size), BATatoms[i].size);
	BATatoms[i].align = align;
	BATatoms[i].linear = FALSE;
	return i;
}
int malAtomSize(int size, int align, char *name)
{
	int i = 0;

	i = ATOMindex(name);
	BATatoms[i].storage = i;
	BATatoms[i].size = size;
	assert_shift_width(ATOMelmshift(BATatoms[i].size), BATatoms[i].size);
	BATatoms[i].align = align;
	return i;
}

void showAtoms(stream *fd)
{
	int i;
	for (i = 0; BATatoms[i].name[0] && i < MAXATOMS; i++) {
		mnstr_printf(fd, "%s", BATatoms[i].name);
		if (BATatoms[i + 1].name[0]) mnstr_printf(fd, ",");
	}
	mnstr_printf(fd, "\n");
}
