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

#ifndef _REL_PLANNER_H_
#define _REL_PLANNER_H_

#include "rel_semantic.h"

extern sql_rel * rel_planner(mvc *sql, list *rels, list *jes);

extern int rel_has_exp(sql_rel *rel, sql_exp *e);
extern sql_rel *find_one_rel(list *rels, sql_exp *e);

#endif /*_REL_PLANNER_H_ */