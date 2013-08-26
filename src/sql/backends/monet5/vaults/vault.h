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
 * @-
 * @+ Implementation
 */
#ifndef _VAULT_H
#define _VAULT_H
#include "mal.h"
#include "mtime.h"
#include "clients.h"

#ifdef WIN32
#ifndef LIBVAULT
#define vault_export extern __declspec(dllimport)
#else
#define vault_export extern __declspec(dllexport)
#endif
#else
#define vault_export extern
#endif

#define _VAULT_DEBUG_

vault_export str VLTprelude(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci);
vault_export str VLTimport(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci);
vault_export str VLTsetLocation(str *ret, str *src);
vault_export str VLTgetLocation(str *ret);
vault_export str VLTremove(timestamp *ret, str *t);
vault_export str VLTbasename(str *ret, str *fnme, str *splot);
vault_export  str VLTepilogue(int *ret);

vault_export char vaultpath[BUFSIZ];
#endif /* _VAULT_H */
