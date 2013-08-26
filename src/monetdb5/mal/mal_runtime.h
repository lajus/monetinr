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

#ifndef _MAL_RUNTIME_H
#define _MAL_RUNTIME_H

#include "mal.h"
#include "mal_client.h"
#include "mal_instruction.h"

/* During MAL interpretation we collect performance event data.
 * Their management is orchestrated from here.
 * We need to maintain some state from ProfileBegin
*/
typedef struct{
	int stkpc;	
} *RuntimeProfile, RuntimeProfileRecord;

/* The actual running queries are assembled in a queue
 * for external inspection and manipulation
 */
typedef struct QRYQUEUE{
	Client cntxt;
	MalBlkPtr mb;
	MalStkPtr stk;
	lng tag;
	str query;
	str status;
	lng start;
	lng runtime;
} *QueryQueue;

mal_export void runtimeProfileInit(Client cntxt, MalBlkPtr mb, MalStkPtr stk);
mal_export void runtimeProfileFinish(Client cntxt, MalBlkPtr mb);
mal_export void runtimeProfileBegin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, int stkpc, RuntimeProfile prof, int start);
mal_export void runtimeProfileExit(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci, RuntimeProfile prof);
mal_export lng getVolume(MalStkPtr stk, InstrPtr pci, int rd);
mal_export void displayVolume(Client cntxt, lng vol);
mal_export void updateFootPrint(MalBlkPtr mb, MalStkPtr stk, int varid);

mal_export QueryQueue QRYqueue;
#endif
