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
 * @f unix
 * @a Tim Ruhl, Martin Kersten, A.R. van Ballegooij
 * @v 0.2
 * @+ Unix standard library calls
 * The unix module is currently of rather limited size.
 * It should include only those facilities that are UNIX
 * specific, i.e. not portable to other platforms.
 * Similar modules may be defined for Windows platforms.
 */
/*
 * @- Implementation
 * The remainder is a straight forward mapping to the underlying
 * facilities
 */
#include "monetdb_config.h"
#include "mal.h"
#include "mal_exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/*----------------------------------------------------------------------------
 * The Basic UNIX commands.
 */

static int
unix_getenv(str *res, str varname)
{
	char *p = getenv(varname);

	if (p)
		*res = GDKstrdup(p);
	else
		*res = GDKstrdup("");
	return GDK_SUCCEED;
}

static int
unix_setenv(bit *res, str varname, str valname)
{
#ifdef HAVE_PUTENV		/* prefer POSIX putenv */
	/* leaks memory when varname value reused */
	size_t len = strlen(varname) + strlen(valname) + 2;
	char *envval = GDKmalloc(len);

	if ( envval == NULL)
		return GDK_FAIL;
	strcpy(envval, varname);
	strcat(envval, "=");
	strcat(envval, valname);
	*res = putenv(envval);
	/* don't free envval! */
#else
#ifdef HAVE_SETENV
	*res = setenv(varname, valname, TRUE);
#else
	*res = -1;
#endif
#endif
	return GDK_SUCCEED;
}
/*
 * @- Wrapping
 * The remainder simply wraps the old code.
 */
#ifdef WIN32
#if !defined(LIBMAL) && !defined(LIBATOMS) && !defined(LIBKERNEL) && !defined(LIBMAL) && !defined(LIBOPTIMIZER) && !defined(LIBSCHEDULER) && !defined(LIBMONETDB5)
#define unix_export extern __declspec(dllimport)
#else
#define unix_export extern __declspec(dllexport)
#endif
#else
#define unix_export extern
#endif

unix_export str UNIXgetenv(str *res, str *varname);
str
UNIXgetenv(str *res, str *varname)
{
	unix_getenv(res, *varname);
	return MAL_SUCCEED;
}

unix_export str UNIXsetenv(bit *res, str *name, str *value);
str
UNIXsetenv(bit *res, str *name, str *value)
{
	unix_setenv(res, *name, *value);
	return MAL_SUCCEED;
}

unix_export str UNIXsync(int *res);
str UNIXsync(int *res){
	(void) res;
#ifndef _MSC_VER				/* not on Windows */
	sync();
#endif
	return MAL_SUCCEED;
}

unix_export str UNIXgetRSS(lng *res);
str UNIXgetRSS(lng *res) {
	*res = MT_getrss();
	return MAL_SUCCEED;
}
