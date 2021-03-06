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

#include "leak.h"
#include "leaked_data.h"
#include "monetdb_config.h"

//#ifdef HAVE_RAPTOR
//# include <rdf.h>
//#endif
#include "mal_instruction.h"
#include "mal_exception.h"
#include "stream.h"

//#include "sql_list.h"
#include "gdk.h"

#include <Rdefines.h>

#define LEAK_DEBUG

// TODO: malloc leaked_data in init function

static int colc = 0;

/* str rs{unsafe}(int); */
str
leak_rs(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int ncol = *(int *)(getArgReference(stk, pci, 1));

	(void) cntxt;
	(void) mb;

	if (ncol && leaked_data) {
		colc = 0;
		leaked_data->type = LD_PROCESSING;
		leaked_data->value = PROTECT(NEW_LIST(ncol));
		leaked_data->name = PROTECT(NEW_STRING(ncol));
		leaked_data->tname = PROTECT(NEW_STRING(ncol));
		mnstr_flush(leaked_data->msg);
	} else {
		throw(MAL, "leak.resultSet", ILLEGAL_ARGUMENT);
	}
	return MAL_SUCCEED;
}


/* str leak_seal(void); */
str
leak_seal(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
#ifndef LEAK_DEBUG
	(void) cntxt;
#endif
	(void) mb;
	(void) stk;
	(void) pci;

	if(leaked_data->value && colc == LENGTH(leaked_data->value)) {
		leaked_data->type = LD_RESULT;
	} else {
		throw(MAL, "leak.seal", PROGRAM_GENERAL);
	}
#ifdef LEAK_DEBUG
	mnstr_printf(cntxt->fdout, "LEAK_FINISH: will return\n");
#endif

	//throw(MAL, "leaker.finish", PROGRAM_NYI);
	return MAL_SUCCEED;
}

static void destroyBat(SEXP s) {
	if(TYPEOF(s) != EXTPTRSXP) {
		Rf_error("MDB finalizer: not an external pointer");
	}
	BBPreleaseref(*((int *)(EXTPTR_PTR(s))));
	GDKfree(EXTPTR_PTR(s));
	return;
}

/* str addColumn{unsafe}(tname:str, name:str, typename:str, digits:int, scale:int, col:bat[:oid,:any_1] ); */
str
leak_addColumn(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	BAT *b;
	SEXP col;
	int nrow;
	bte success = 0;
	int *bid = (int*) getArgReference(stk, pci, 6);
	str tname = GDKstrdup(*((str*) getArgReference(stk, pci, 1)));
	str name = GDKstrdup(*((str*) getArgReference(stk, pci, 2)));
	str type = GDKstrdup(*((str*) getArgReference(stk, pci, 3)));
	int type_length = *((int*) getArgReference(stk, pci, 4));
	int scale = *((int*) getArgReference(stk, pci, 5));
	int *biddup = GDKmalloc(sizeof(int));

#ifndef LEAK_DEBUG
	(void) cntxt;
#endif
	(void) mb;

	//mnstr_printf(cntxt->fdout, "%d, %d\n", c->md_type_length, c->md_scale);
	assert(scale == 0); // I don't know at what this parameter refers to.
							  // When it's 0 it works so assert 0 to immediately see when it reacts otherwise.

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "leak.addColumn", RUNTIME_OBJECT_MISSING);

	// Can't add a header to a view, we need to copy the BAT...
	// To be discussed: Or we might define that a view is a non-native type in R and treat it like that.
	// ... But for now, non-native types are not yet implemented.
	if (isVIEW(b)) {
		b = BATcopy(b, TYPE_void, b->ttype, 1);
		BBPincref(b->batCacheid, 0);
		if ((b = BATdescriptor(b->batCacheid)) == NULL) // To be sure the copy is in memory
			throw(MAL, "leak.addColumn", RUNTIME_OBJECT_MISSING);
	}

	// I keep it, my precious !!!!
	//BBPreleaseref(b->batCacheid); // Moved to R finalizer callback

	if (leaked_data == NULL) {
		mnstr_printf(cntxt->fdout, "LEAK init failed somewhere\n");
		throw(MAL, "leaker.addColumn", PROGRAM_GENERAL);
	}

	nrow = BATcount(b);

	if (!biddup)
		throw(MAL, "leaker.addColumn", PROGRAM_GENERAL);
	*biddup = *bid;

	if(strcmp(type, "int") == 0) {
		PROTECT(col = Rf_allocVectorInPlace(INTSXP, nrow, Tloc(b, BUNfirst(b)), Tloc(b, BUNfirst(b)) - Rf_sizeofHeader(), &destroyBat, (void *)(biddup)));
		if (col != NULL) {
			success = 1;
			SET_TRUELENGTH(col, nrow);
			SET_VECTOR_ELT(leaked_data->value, colc, col);
		}
		UNPROTECT(1);
	}
	else if(strcmp(type, "double") == 0) {
		PROTECT(col = Rf_allocVectorInPlace(REALSXP, nrow, Tloc(b, BUNfirst(b)), Tloc(b, BUNfirst(b)) - Rf_sizeofHeader(), &destroyBat, (void *)(biddup)));
		if (col != NULL) {
			success = 1;
			SET_TRUELENGTH(col, nrow);
			SET_VECTOR_ELT(leaked_data->value, colc, col);
		}
		UNPROTECT(1);
	}

	if(success) {
		SET_STRING_ELT(leaked_data->name, colc, mkChar(name));
		SET_STRING_ELT(leaked_data->tname, colc, mkChar(tname));
		colc++;
		leaked_bids = CINT_pushValue(*bid, leaked_bids);
	} else {
		(void) type_length;
		throw(MAL, "leaker.addColumn", PROGRAM_NYI);
	}

	return MAL_SUCCEED;
}

// Some macros

#define SI(val) ScalarInteger((int) val )
#define SR(val) ScalarReal((double) val )
#define SS(val) ScalarString(mkChar((str) val ))

#define SCALE_IT(val, scale) ((scale == 0) ? SI(val) : ScalarReal((double)(val) * pow(10.0, -scale)))
#define SCALE_IT2(val, scale) ((scale == 0) ? SR(val) : ScalarReal((double)(val) * pow(10.0, -scale)))
#define CAST_AND_ASSIGN(otype, otypearg, cast_into) { \
	otype tmp = otypearg ; leaked_data->value = PROTECT( (cast_into) ); \
}
#define VV(name) (val.val.name)
#define BATval(otype) ( *(otype *)(Tloc(b, BUNfirst(b))) )
#define CAST_AND_ASSIGN_BAT(otype, cast_into) CAST_AND_ASSIGN(otype, BATval(otype), cast_into)

#define HACK_PROTECT { \
		leaked_data->value = PROTECT(ScalarInteger(NA_INTEGER)); \
		leaked_data->name = PROTECT(ScalarString(mkChar("ERR"))); \
		leaked_data->tname = PROTECT(ScalarString(mkChar("ERR"))); \
 		leaked_data->type = LD_ERROR; \
}

/* leakValue{unsafe}(tname:str, name:str, typename:str, digits:int, scale:int, val:any_1) :void */
str
leak_value(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str tname = GDKstrdup(*((str*) getArgReference(stk, pci, 1)));
	str name = GDKstrdup(*((str*) getArgReference(stk, pci, 2)));
	//str type = GDKstrdup(*((str*) getArgReference(stk, pci, 3)));
	//int type_length = *((int*) getArgReference(stk, pci, 4));
	int scale = *((int*) getArgReference(stk, pci, 5));
	ValRecord val = stk->stk[pci->argv[6]];

	(void) cntxt;
	(void) mb;
	// TODO

	//assert(scale == 0); // scale is used for decimals, the value is internally stored as a not-floating point and is represented with value*10^(-scale)

	switch(val.vtype) {
	case TYPE_void: leaked_data->value = PROTECT(ScalarInteger(NA_INTEGER)); break;
	case TYPE_bit: leaked_data->value = PROTECT(ScalarLogical(VV(ival))); break;
	case TYPE_bte: CAST_AND_ASSIGN(bte, VV(btval), SCALE_IT(tmp, scale)) break;
	case TYPE_wrd: CAST_AND_ASSIGN(wrd, VV(wval), SCALE_IT(tmp, scale)) break;
	case TYPE_int: leaked_data->value = PROTECT(SCALE_IT(VV(ival), scale)); break;
	case TYPE_oid: CAST_AND_ASSIGN(oid, VV(oval), SR(tmp)) break;
	case TYPE_sht: CAST_AND_ASSIGN(sht, VV(shval), SCALE_IT(tmp, scale)) break;
	case TYPE_lng: CAST_AND_ASSIGN(lng, VV(lval), SCALE_IT2(tmp, scale)) break;
	case TYPE_flt: CAST_AND_ASSIGN(flt, VV(fval), SR(tmp)) break;
	case TYPE_dbl: leaked_data->value = PROTECT(SR(VV(dval))); break;
	case TYPE_str: leaked_data->value = PROTECT(SS(VV(sval))); break;
	case TYPE_bat: { BAT *b; bat tmp = VV(bval);
					 if((b = BATdescriptor(tmp)) == NULL) goto invalid_bid;
					 assert(BATcount(b) == 1);
					 switch(BATttype(b)) {
					 	case TYPE_void: leaked_data->value = PROTECT(ScalarInteger(NA_INTEGER)); break;
					 	case TYPE_bit: leaked_data->value = PROTECT(ScalarLogical(BATval(bit))); break;
					 	case TYPE_bte: CAST_AND_ASSIGN_BAT(bte, SI(tmp)) break;
					 	case TYPE_wrd: CAST_AND_ASSIGN_BAT(wrd, SI(tmp)) break;
					 	case TYPE_int: leaked_data->value = PROTECT(SI(BATval(int))); break;
					 	case TYPE_oid: CAST_AND_ASSIGN_BAT(oid, SR(tmp)) break;
					 	case TYPE_sht: CAST_AND_ASSIGN_BAT(sht, SI(tmp)) break;
					 	case TYPE_lng: CAST_AND_ASSIGN_BAT(lng, SR(tmp)) break;
					 	case TYPE_flt: CAST_AND_ASSIGN_BAT(flt, SR(tmp)) break;
					 	case TYPE_dbl: leaked_data->value = PROTECT(SR(BATval(double))); break;
					 	case TYPE_str: leaked_data->value = PROTECT(SS(Tbase(b))); break;
					 	default:
					 		/* Hack against PROTECT imbalance */
					 		HACK_PROTECT
					 		throw(MAL, "leaker.leakValue", PROGRAM_NYI); break;
					 }
					} break;
	default:
		/* Hack against PROTECT imbalance */
		invalid_bid:
		HACK_PROTECT
		throw(MAL, "leaker.leakValue", PROGRAM_NYI); break;
	}

	leaked_data->name = PROTECT(ScalarString(mkChar(name)));
	leaked_data->tname = PROTECT(ScalarString(mkChar(tname)));
	leaked_data->type = LD_RESULT;

	return MAL_SUCCEED;
}
