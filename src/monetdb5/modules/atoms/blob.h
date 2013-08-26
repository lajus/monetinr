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
 * @* Implementation Code
 */
#ifndef __BLOB_H__
#define __BLOB_H__
#include "mal.h"
#include "mal_exception.h"

typedef struct blob {
	size_t nitems;
	/*unsigned */ char data[1];
} blob;

#define sqlblob blob

#ifdef WIN32
#if !defined(LIBMAL) && !defined(LIBATOMS) && !defined(LIBKERNEL) && !defined(LIBMAL) && !defined(LIBOPTIMIZER) && !defined(LIBSCHEDULER) && !defined(LIBMONETDB5)
#define blob_export extern __declspec(dllimport)
#else
#define blob_export extern __declspec(dllexport)
#endif
#else
#define blob_export extern
#endif

blob_export int TYPE_blob;
blob_export int TYPE_sqlblob;

blob_export var_t blobsize(size_t nitems);
blob_export int sqlblob_tostr(str *tostr, int *l, blob *p);
blob_export int sqlblob_fromstr(char *instr, int *l, blob **val);

#endif /* __BLOB_H__ */
