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
 * @f rdf
 * @a L.Sidirourgos
 *
 * @* The RDF module For MonetDB5 (aka. MonetDB/RDF)
 *
 */
#ifndef _RDF_H_
#define _RDF_H_

#include <gdk.h>

#ifdef WIN32
#ifndef LIBRDF
#define rdf_export extern __declspec(dllimport)
#else
#define rdf_export extern __declspec(dllexport)
#endif
#else
#define rdf_export extern
#endif

/* internal debug messages */
#define _RDF_DEBUG

rdf_export str
RDFParser(BAT **graph, str *location, str *graphname, str *schemam);

rdf_export str 
RDFleftfetchjoin_sortedestimate(int *result, int *lid, int *rid, lng *estimate);
rdf_export str 
RDFleftfetchjoin_sorted(int *result, int* lid, int *rid);

rdf_export str 
TKNZRrdf2str (bat *res, bat *bid, bat *map);

#define RDF_MIN_LITERAL (((oid) 1) << ((sizeof(oid)==8)?62:30))

#define TRIPLE_STORE 1
#define MLA_STORE    2

#define STORE TRIPLE_STORE /* this should become a compile time option */

#if STORE == TRIPLE_STORE
 typedef enum {
	S_sort, P_sort, O_sort, /* sorted */
	P_PO, O_PO, /* spo */
	P_OP, O_OP, /* sop */
	S_SO, O_SO, /* pso */
	S_OS, O_OS, /* pos */
	S_SP, P_SP, /* osp */
	S_PS, P_PS, /* ops */
	MAP_LEX
 } graphBATType;
#elif STORE == MLA_STORE
 typedef enum {
	S_sort, P_sort, O_sort,
	MAP_LEX
 } graphBATType;
#endif /* STORE */

#define N_GRAPH_BAT (MAP_LEX+1)

#endif /* _RDF_H_ */
