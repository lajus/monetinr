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

#ifndef LEAKED_DATA
#define LEAKED_DATA

#include "monetdb_config.h"
//#include "sql_list.h"
#include <R.h>
#include <Rdefines.h>
#include "stream.h"

typedef enum { LD_PROCESSING, LD_RESULT, LD_ERROR, LD_MESSAGE } LD_MSGTYPE;
typedef struct RRESULT {
	LD_MSGTYPE type;
	SEXP name;
	SEXP tname;
	SEXP value;
	stream *msg;
} *RResultPtr, RResultRec;

extern RResultPtr leaked_data;
//extern int leaked_resultc;

extern int leak_init(void);
extern char *mR_getMsg(stream *);
extern void mR_destroyMsg(stream *);
//extern int leak_init_query(str sqlQuery);

#endif

// Previous implementation
//typedef struct EXTCOLUMN {
//	str md_type;
//	int md_type_length;
//	str md_tname;
//	str md_name;
//	int md_scale; // md for metadata
//	int bid;
//	BAT *value;
//} *ExtColumnPtr, ExtColumnRecord;

//typedef struct QRESULT {
//	bte finished;
//	bte cleaned;
//	int id;
//	int fieldc; // number of columns
//	str sqlQuery; // It should not be very usefull, debug ?
//	list *colums;
//} *QResultPtr, QResultRecord;
