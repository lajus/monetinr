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
 * @f sema
 * @a Peter Boncz
 * @v 1.0
 * @-
 * This module provides simple SMP lock and thread functionality
 * as already present in the MonetDB system.
 * @+ semaphores
 */
/*
 * @+ Implementation
 */
#include "monetdb_config.h"
#include <gdk.h>
#include "mal_exception.h"
typedef ptr sema;
typedef ptr monet_sema;

#ifdef WIN32
#if !defined(LIBMAL) && !defined(LIBATOMS) && !defined(LIBKERNEL) && !defined(LIBMAL) && !defined(LIBOPTIMIZER) && !defined(LIBSCHEDULER) && !defined(LIBMONETDB5)
#define sema_export extern __declspec(dllimport)
#else
#define sema_export extern __declspec(dllexport)
#endif
#else
#define sema_export extern
#endif

static int
create_sema(monet_sema *s, int *init)
{
	*s = (monet_sema) GDKmalloc(sizeof(MT_Sema));
	if (*s == NULL || *s == ptr_nil) return GDK_FAIL;
	MT_sema_init((MT_Sema *) *s, *init, "M5_create_sema");
	return GDK_SUCCEED;
}

static int
up_sema(monet_sema *s)
{
	if (*s == NULL || *s == ptr_nil) return GDK_FAIL;
	MT_sema_up((MT_Sema*) *s, "up_sema");
	return GDK_SUCCEED;
}

static int
down_sema(monet_sema *s)
{
    if (*s == NULL || *s == ptr_nil) return GDK_FAIL;
    MT_sema_down((MT_Sema*) *s, "down_sema");
	return GDK_SUCCEED;
}

static int
destroy_sema(monet_sema *s)
{
	if (*s == NULL || *s == ptr_nil) return GDK_FAIL;
	MT_sema_destroy((MT_Sema*) *s);
	GDKfree(*s);
	return GDK_SUCCEED;
}

/*
 * @-
 * The old code base is wrapped to ease update propagation.
 */
#include "mal.h"
sema_export str SEMAcreate(monet_sema *res, int *init);
str
SEMAcreate(monet_sema *res, int *init)
{
	create_sema(res, init);
	return MAL_SUCCEED;
}

sema_export str SEMAup(int *res, monet_sema *s);
str
SEMAup(int *res, monet_sema *s)
{
	up_sema(s);
	*res = 1;
	return MAL_SUCCEED;
}

sema_export str SEMAdown(int *res, monet_sema *s);
str
SEMAdown(int *res, monet_sema *s)
{
	down_sema(s);
	*res = 1;
	return MAL_SUCCEED;
}

sema_export str SEMAdestroy(int *res, monet_sema *s);
str
SEMAdestroy(int *res, monet_sema *s)
{
	destroy_sema(s);
	*res = 1;
	return MAL_SUCCEED;
}

