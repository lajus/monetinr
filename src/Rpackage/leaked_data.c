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

#include "leaked_data.h"
//#include "sql_list.h"
#include "monetdb_config.h"
#include "gdk.h"
#include "stream.h"

RResultPtr  leaked_data;
ChainedINT *leaked_bids;
//int leaked_resultc;

//static int max_size = 100;

ChainedINT *CINT_pushValue(int val, ChainedINT *c) {
	ChainedINT *r = (ChainedINT *) GDKmalloc(sizeof(ChainedINT));
	r->val = val;
	r->next = c;
	return r;
}

ChainedINT *CINT_free(ChainedINT *c) {
	if (c != NULL) {
		ChainedINT *next = c->next;
		GDKfree(c);
		return CINT_free(next);
	}
	return NULL;
}

int leakedBatInUse(ChainedINT *c) {
	return (c == NULL) ? 0 : (BBP_refs(c->val) > 0 || leakedBatInUse(c->next));
}

int
leak_init(void)
{
	leaked_bids = NULL;
	leaked_data = GDKmalloc(sizeof(RResultRec));
	leaked_data->msg = buffer_wastream(buffer_create(GDKMAXERRLEN), "STDOUT_R_REDIRECT");
	leaked_data->type = LD_ERROR;
	return (leaked_data == NULL);
}

/* NB: Thanks to burn after reading */
char *
mR_getMsg(stream *msg)
{
	return buffer_get_buf(mnstr_get_buffer(msg));
}

void mR_destroyMsg(stream *msg) {
	buffer_destroy(mnstr_get_buffer(msg));
}
//static void *
//destroy_column(void *p)
//{
//	ExtColumnPtr p2 = (ExtColumnPtr) p;
//	GDKfree(p2->value);
//	p2->value = NULL;
//	return 0;
//}
//
//int
//leak_init_query(str sqlQuery)
//{
//	QResultPtr new;
//	int nid = leaked_resultc;
//
//	if (++leaked_resultc >= max_size)
//	{
//		// If not enough place,
//		// reallocate some memory, 100 queries by 100 queries
//		max_size += 100;
//		new = GDKrealloc(leaked_data, max_size * sizeof(QResultRecord));
//		if(!new)
//			return -1;
//		leaked_data = new;
//	}
//	leaked_data[nid].finished = 0;
//	leaked_data[nid].cleaned = 0;
//	leaked_data[nid].sqlQuery = sqlQuery;
//	leaked_data[nid].id = nid;
//	leaked_data[nid].colums = list_create((void *)destroy_column);
//	return nid;
//}
