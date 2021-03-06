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
#ifndef _MAL_MACRO_H_
#define _MAL_MACRO_H_

opt_export str MACROprocessor(Client cntxt, MalBlkPtr mb, Symbol t);
opt_export int inlineMALblock(MalBlkPtr mb, int pc, MalBlkPtr mc);
opt_export int OPTmacroImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p);
opt_export int OPTorcamImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p);
opt_export str OPTmacro(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p);
opt_export str OPTorcam(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p);

#define OPTDEBUGmacro  if ( optDebug & (1 <<DEBUG_OPT_MACRO) )
#define OPTDEBUGorcam  if ( optDebug & (1 <<DEBUG_OPT_ORCAM) )

#endif /* _MAL_MACRO_H_ */
