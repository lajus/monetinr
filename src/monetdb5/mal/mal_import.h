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
 * Copyright August 2008-2011 MonetDB B.V.
 * All Rights Reserved.
 */

#ifndef _MAL_IMPORT_H
#define _MAL_IMPORT_H

#include "mal_exception.h"
#include "mal_client.h"
#include "mal_session.h"
#include "mal_utils.h"

mal_export void slash_2_dir_sep(str fname);
mal_export str malLoadScript(Client c, str name, bstream **fdin);
mal_export str malInclude(Client c, str name, int listing);
mal_export str evalFile(Client c, str fname, int listing);
mal_export str compileString(Symbol *fcn, Client c, str s);
mal_export int callString(Client c, str s, int listing);
#endif /*  _MAL_IMPORT_H */
