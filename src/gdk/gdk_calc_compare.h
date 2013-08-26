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

/* this file is included multiple times by gdk_calc.c */

static BUN
op_typeswitchloop(const void *lft, int tp1, int incr1, const char *hp1, int wd1,
		  const void *rgt, int tp2, int incr2, const char *hp2, int wd2,
		  TPE *dst, BUN cnt, BUN start, BUN end, const oid *cand,
		  const oid *candend, oid candoff, int nonil, const char *func)
{
	BUN nils = 0;
	BUN i, j, k;
	const void *nil;
	int (*atomcmp)(const void *, const void *);

	switch (tp1) {
	case TYPE_void: {
		oid v;

		assert(incr1 == 1);
		assert(tp2 == TYPE_oid || incr2 == 1); /* if void, incr2==1 */
		v = * (const oid *) lft;
		CANDLOOP(dst, k, TPE_nil, 0, start);
		if (v == oid_nil || tp2 == TYPE_void) {
			TPE res = v == oid_nil || * (const oid *) rgt == oid_nil ?
				TPE_nil :
				OP(v, * (const oid *) rgt);

			if (res == TPE_nil || cand == NULL) {
				for (k = start; k < end; k++)
					dst[k] = res;
				if (res == TPE_nil)
					nils = end - start;
			} else {
				for (k = start; k < end; k++) {
					CHECKCAND(dst, k, candoff, TPE_nil);
					dst[k] = res;
				}
			}
		} else {
			for (v += start, j = start * incr2, k = start;
			     k < end;
			     v++, j += incr2, k++) {
				CHECKCAND(dst, k, candoff, TPE_nil);
				if (((const oid *) rgt)[j] == oid_nil) {
					nils++;
					dst[k] = TPE_nil;
				} else {
					dst[k] = OP(v, ((const oid *) rgt)[j]);
				}
			}
		}
		CANDLOOP(dst, k, TPE_nil, end, cnt);
		break;
	}
	case TYPE_bit:
		if (tp2 != TYPE_bit)
			goto unsupported;
		if (nonil)
			BINARY_3TYPE_FUNC_nonil(bit, bit, TPE, OP);
		else
			BINARY_3TYPE_FUNC(bit, bit, TPE, OP);
		break;
	case TYPE_bte:
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(bte, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(bte, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_sht:
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(sht, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(sht, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
	case TYPE_wrd:
#endif
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(int, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(int, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
	case TYPE_wrd:
#endif
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(lng, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(lng, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_flt:
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(flt, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(flt, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_dbl:
		switch (tp2) {
		case TYPE_bte:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, bte, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, bte, TPE, OP);
			break;
		case TYPE_sht:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, sht, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, sht, TPE, OP);
			break;
		case TYPE_int:
#if SIZEOF_WRD == SIZEOF_INT
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, int, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, int, TPE, OP);
			break;
		case TYPE_lng:
#if SIZEOF_WRD == SIZEOF_LNG
		case TYPE_wrd:
#endif
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, lng, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, lng, TPE, OP);
			break;
		case TYPE_flt:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, flt, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, flt, TPE, OP);
			break;
		case TYPE_dbl:
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(dbl, dbl, TPE, OP);
			else
				BINARY_3TYPE_FUNC(dbl, dbl, TPE, OP);
			break;
		default:
			goto unsupported;
		}
		break;
	case TYPE_oid:
		if (tp2 == TYPE_void) {
			oid v;

			v = * (const oid *) rgt;
			if (v == oid_nil) {
				for (k = 0; k < cnt; k++)
					dst[k] = TPE_nil;
				nils = cnt;
			} else {
				CANDLOOP(dst, k, TPE_nil, 0, start);
				for (i = start * incr1, v += start, k = start;
				     k < end; i += incr1, v++, k++) {
					CHECKCAND(dst, k, candoff, TPE_nil);
					if (((const oid *) lft)[i] == oid_nil) {
						nils++;
						dst[k] = TPE_nil;
					} else {
						dst[k] = OP(((const oid *) lft)[i], v);
					}
				}
				CANDLOOP(dst, k, TPE_nil, end, cnt);
			}
		} else if (tp2 == TYPE_oid) {
			if (nonil)
				BINARY_3TYPE_FUNC_nonil(oid, oid, TPE, OP);
			else
				BINARY_3TYPE_FUNC(oid, oid, TPE, OP);
		} else {
			goto unsupported;
		}
		break;
	case TYPE_str:
		if (tp1 != tp2)
			goto unsupported;
		CANDLOOP(dst, k, TPE_nil, 0, start);
		for (i = start * incr1, j = start * incr2, k = start;
		     k < end; i += incr1, j += incr2, k++) {
			const char *s1, *s2;
			CHECKCAND(dst, k, candoff, TPE_nil);
			s1 = hp1 ? hp1 + VarHeapVal(lft, i, wd1) : (const char *) lft;
			s2 = hp2 ? hp2 + VarHeapVal(rgt, j, wd2) : (const char *) rgt;
			if (s1 == NULL || strcmp(s1, str_nil) == 0 ||
			    s2 == NULL || strcmp(s2, str_nil) == 0) {
				nils++;
				dst[k] = TPE_nil;
			} else {
				int x = strcmp(s1, s2);
				dst[k] = OP(x, 0);
			}
		}
		CANDLOOP(dst, k, TPE_nil, end, cnt);
		break;
	default:
		if (tp1 != tp2 ||
		    !BATatoms[tp1].linear ||
		    (atomcmp = BATatoms[tp1].atomCmp) == NULL)
			goto unsupported;
		nil = ATOMnilptr(tp1);
		CANDLOOP(dst, k, TPE_nil, 0, start);
		for (i = start * incr1, j = start * incr2, k = start;
		     k < end; i += incr1, j += incr2, k++) {
			const void *p1, *p2;
			CHECKCAND(dst, k, candoff, TPE_nil);
			p1 = hp1 ? (const void *) (hp1 + VarHeapVal(lft, i, wd1)) : lft;
			p2 = hp2 ? (const void *) (hp2 + VarHeapVal(rgt, i, wd2)) : rgt;
			if (p1 == NULL || p2 == NULL ||
			    (*atomcmp)(p1, nil) == 0 ||
			    (*atomcmp)(p2, nil) == 0) {
				nils++;
				dst[k] = TPE_nil;
			} else {
				int x = (*atomcmp)(p1, p2);
				dst[k] = OP(x, 0);
			}
			if (hp1 == NULL && incr1)
				lft = (const void *) ((const char *) lft + wd1);
			if (hp2 == NULL && incr2)
				rgt = (const void *) ((const char *) rgt + wd2);
		}
		CANDLOOP(dst, k, TPE_nil, end, cnt);
		break;
	}

	return nils;

  unsupported:
	GDKerror("%s: bad input types %s,%s.\n", func,
		 ATOMname(tp1), ATOMname(tp2));
	return BUN_NONE;
}

static BAT *
BATcalcop_intern(const void *lft, int tp1, int incr1, const char *hp1, int wd1,
		 const void *rgt, int tp2, int incr2, const char *hp2, int wd2,
		 BUN cnt, BUN start, BUN end, const oid *cand,
		 const oid *candend, oid candoff, int nonil, oid seqbase,
		 const char *func)
{
	BAT *bn;
	BUN nils = 0;
	TPE *dst;

	bn = BATnew(TYPE_void, TYPE_TPE, cnt);
	if (bn == NULL)
		return NULL;

	dst = (TPE *) Tloc(bn, bn->U->first);

	nils = op_typeswitchloop(lft, tp1, incr1, hp1, wd1,
				 rgt, tp2, incr2, hp2, wd2,
				 dst, cnt, start, end, cand, candend, candoff,
				 nonil, func);

	if (nils == BUN_NONE) {
		BBPunfix(bn->batCacheid);
		return NULL;
	}

	BATsetcount(bn, cnt);
	bn = BATseqbase(bn, seqbase);

	bn->T->sorted = cnt <= 1 || nils == cnt;
	bn->T->revsorted = cnt <= 1 || nils == cnt;
	bn->T->key = cnt <= 1;
	bn->T->nil = nils != 0;
	bn->T->nonil = nils == 0;

	return bn;
}

BAT *
BATcalcop(BAT *b1, BAT *b2, BAT *s)
{
	BAT *bn;
	BUN start, end, cnt;
	const oid *cand = NULL, *candend = NULL;

	BATcheck(b1, BATcalcop_name);
	BATcheck(b2, BATcalcop_name);

	if (checkbats(b1, b2, BATcalcop_name) == GDK_FAIL)
		return NULL;

	CANDINIT(b1, s, start, end, cnt, cand, candend);

	if (BATtvoid(b1) && BATtvoid(b2) && cand == NULL) {
		TPE res;

		if (b1->T->seq == oid_nil || b2->T->seq == oid_nil)
			res = TPE_nil;
		else
			res = OP(b1->T->seq, b2->T->seq);

		bn = BATconstant(TYPE_TPE, &res, cnt);
		BATseqbase(bn, b1->hseqbase);
		return bn;
	}

	bn = BATcalcop_intern(b1->T->type == TYPE_void ? (void *) &b1->T->seq : (void *) Tloc(b1, b1->U->first), b1->T->type, 1,
			      b1->T->vheap ? b1->T->vheap->base : NULL,
			      b1->T->width,
			      b2->T->type == TYPE_void ? (void *) &b2->T->seq : (void *) Tloc(b2, b2->U->first), b2->T->type, 1,
			      b2->T->vheap ? b2->T->vheap->base : NULL,
			      b2->T->width,
			      cnt, start, end, cand, candend, b1->hseqbase,
			      cand == NULL && b1->T->nonil && b2->T->nonil,
			      b1->H->seq, BATcalcop_name);

	return bn;
}

BAT *
BATcalcopcst(BAT *b, const ValRecord *v, BAT *s)
{
	BAT *bn;
	BUN start, end, cnt;
	const oid *cand = NULL, *candend = NULL;

	BATcheck(b, BATcalcopcst_name);

	if (checkbats(b, NULL, BATcalcopcst_name) == GDK_FAIL)
		return NULL;

	CANDINIT(b, s, start, end, cnt, cand, candend);

	bn = BATcalcop_intern(Tloc(b, b->U->first), b->T->type, 1,
			      b->T->vheap ? b->T->vheap->base : NULL,
			      b->T->width,
			      VALptr(v), v->vtype, 0,
			      NULL, 0,
			      cnt, start, end, cand, candend, b->hseqbase,
			      cand == NULL && b->T->nonil && ATOMcmp(v->vtype, VALptr(v), ATOMnilptr(v->vtype)) != 0,
			      b->H->seq, BATcalcopcst_name);

	return bn;
}

BAT *
BATcalccstop(const ValRecord *v, BAT *b, BAT *s)
{
	BAT *bn;
	BUN start, end, cnt;
	const oid *cand = NULL, *candend = NULL;

	BATcheck(b, BATcalccstop_name);

	if (checkbats(b, NULL, BATcalccstop_name) == GDK_FAIL)
		return NULL;

	CANDINIT(b, s, start, end, cnt, cand, candend);

	bn = BATcalcop_intern(VALptr(v), v->vtype, 0,
			      NULL, 0,
			      Tloc(b, b->U->first), b->T->type, 1,
			      b->T->vheap ? b->T->vheap->base : NULL,
			      b->T->width,
			      cnt, start, end, cand, candend, b->hseqbase,
			      cand == NULL && b->T->nonil && ATOMcmp(v->vtype, VALptr(v), ATOMnilptr(v->vtype)) != 0,
			      b->H->seq, BATcalccstop_name);

	return bn;
}

int
VARcalcop(ValPtr ret, const ValRecord *lft, const ValRecord *rgt)
{
	ret->vtype = TYPE_TPE;
	if (op_typeswitchloop(VALptr(lft), lft->vtype, 0, NULL, 0,
			      VALptr(rgt), rgt->vtype, 0, NULL, 0,
			      VALget(ret), 1, 0, 1, NULL, NULL, 0, 0,
			      VARcalcop_name) == BUN_NONE)
		return GDK_FAIL;
	return GDK_SUCCEED;
}
