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

#ifndef _MAL_RESOURCE_H
#define _MAL_RESOURCE_H

#include "mal_interpreter.h"

#define TIMESLICE  2000 /* ms */
#define DELAYUNIT 5 /* ms delay in parallel processing decisions */

#define USE_MAL_ADMISSION
#ifdef USE_MAL_ADMISSION
mal_export int MALadmission(lng argclaim, lng hotclaim);
#endif

mal_export lng getMemoryClaim(MalBlkPtr mb, MalStkPtr stk, int pc, int i, int flag);
mal_export void MALresourceFairness(lng usec);

#endif /*  _MAL_RESOURCE_H*/
