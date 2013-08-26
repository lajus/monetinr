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
#include "monetdb_config.h"
#include "opt_prelude.h"
#include "opt_dictionary.h"
#include "mal_instruction.h"
#include "mal_interpreter.h"
#include "algebra.h"

#define isDiction(X) (idx[X] && val[X])

static BAT *dictIndex, *dictValue, *dictBase;

static int DICTinit(void);

static int 
DICTfind(int *idx, int *val, int *base, str nme)
{
	BUN p;
	BATiter bidx = bat_iterator(dictIndex);
	BATiter bval = bat_iterator(dictValue);
	BATiter bbase = bat_iterator(dictBase);
	p = BUNfnd(BATmirror(dictIndex), (ptr) nme);
	if ( p != BUN_NONE ){
		*idx = *(int*) BUNhead(bidx, p);
		*val = *(int*) BUNhead(bval, BUNfnd(BATmirror(dictValue), (ptr) nme ) );
		*base = *(int*) BUNhead(bbase, BUNfnd(BATmirror(dictBase), (ptr) nme ) );
		return 0;
	}
	return -1;
}

int
OPTdictionaryImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int i,j,k, actions=0;
	int *idx, *val;
	InstrPtr *old,q, qq;
	int x, v, limit;
	BUN p;
	char buf[BUFSIZ];
	BAT *bi, *bv;
	str dictionaryRef = putName("dictionary",10);

	(void) cntxt;
	(void) stk;
	(void) pci;
	if ( DICTinit() == 0)
		return 0;
	limit = mb->stop;
	old = mb->stmt;
	
	OPTDEBUGdictionary{
		mnstr_printf(cntxt->fdout,"#dictionary optimizer\n");
		printFunction(cntxt->fdout,mb,0,LIST_MAL_ALL);
	}

    if ( newMalBlkStmt(mb, mb->ssize) < 0)
        return 0;

	/* we should be prepared that the variable list gets extended */
	idx = (int*) GDKzalloc(2 * mb->vtop * sizeof(int));
	if ( idx == 0)
		return 0;
	val = (int*) GDKzalloc(2 * mb->vtop * sizeof(int));
	if ( val == 0) {
		GDKfree(idx);
		return 0;
	}

	for (i=0; i< limit; i++){
        	q= old[i];

		if (getModuleId(q) == NULL) {
			pushInstruction(mb, q);
			continue;
		}
		/* replace the BAT if a dictionary exists */
		buf[0]= 0;
		if ( getModuleId(q) == sqlRef && getFunctionId(q) == bindRef)
			snprintf(buf,BUFSIZ,"%s/%s/%s/%d", 
				getVarConstant(mb,getArg(q,1)).val.sval, getVarConstant(mb, getArg(q,2)).val.sval,
				getVarConstant(mb,getArg(q,3)).val.sval, getVarConstant(mb, getArg(q,4)).val.ival);
				/*
				 * The dbat is not part of the dictionary encoding. It should be dealt with in the context
				 * of OID sizes. The same holds for idx bats.
				 */

		if ( buf[0] ){
			BATiter bidx = bat_iterator(dictIndex);
			BATiter bval = bat_iterator(dictValue);

			p = BUNfnd(BATmirror(dictIndex), (ptr) buf);
			if ( p != BUN_NONE ){
				x = *(int*) BUNhead(bidx, p);
				v = *(int*) BUNhead(bval, BUNfnd(BATmirror(dictValue), (ptr) buf ) );
				OPTDEBUGdictionary
					mnstr_printf(GDKout, "#Located a dictionary %s ? %s %d %d\n",buf, (p?"ok":"no"), x,v);
				/* replace the sql.bind */
				bi = BATdescriptor(x);
				if ( bi == NULL){
					GDKfree(idx);
					GDKfree(val);
					return 0;
				}
				bv = BATdescriptor(v);
				if ( bv == NULL){
					BBPreleaseref(bi->batCacheid);
					GDKfree(idx);
					GDKfree(val);
					return 0;
				}
				/* construct: (bi:bat[:oid,:type], bv:bat[:type,:any2]):= dictionary.bind(name); */
				qq = newStmt(mb,"dictionary",bindRef);
				qq = pushReturn(mb,qq, newTmpVariable(mb,newBatType(bi->ttype,bv->ttype)));
				qq = pushStr(mb,qq,buf);
				setVarType(mb, getArg(qq,0), newBatType(TYPE_oid,bi->ttype));
				setVarUDFtype(mb, getArg(qq,0));
				setVarUDFtype(mb, getArg(qq,1));
				/* let's remember them */
				idx[getArg(q,0)] = getArg(qq,0);
				val[getArg(q,0)] = getArg(qq,1);
				OPTDEBUGdictionary
					mnstr_printf(GDKout, "#Assigned %s to  %d %d\n",buf, idx[getArg(q,0)], val[getArg(q,0)]);
				actions++;
				continue;
			} else  
				pushInstruction(mb,q);
		/*
		 * The trigger to convert tables is postponed.
		 * For all 0-kind binds we will try to create a dictionary pair.
		 * This means it becomes visible at the next query, then it
		 * is also the right moment to zap the base table itself.
		 * Moreover, we have to invalidate the plan when a new
		 * dictionary element has been added since its construction.
		 * Recompilation is then necessary, and preferably done automatically.
		 * 			if ( getModuleId(q) == sqlRef && getFunctionId(q) == bindRef && getVarConstant(mb,getArg(q,4)).val.ival == 0 ){
		 * 				int one=1;
		 * 				pushInstruction(mb,q);
		 * 				qq = newStmt(mb, dictionaryRef, putName("compress",8));
		 * 				qq = pushStr(mb,qq,buf);
		 * 				varSetProp(mb, getArg(getInstrPtr(mb,0), 0), runonceProp, op_eq, (ptr) &one);
		 * 				pushArgument(mb,qq, getArg(q,0));
		 * 			} else
		 * 				pushInstruction(mb,q);
		 */
		} else {
			/*
			 * We have to re-cast each instruction based on a possible dictionary
			 * representation of an argument. For some operators we may postpone
			 * reconstruction and introduce a new dictionary pair.
			 */
			if (  getModuleId(q) == algebraRef ){
				if ( (getFunctionId(q) == selectRef ||
					 getFunctionId(q) == thetaselectRef ) &&
					isDiction(getArg(q,1))  ){
#ifdef DEBUG_OPT_DICTIONARY 
						mnstr_printf(GDKout,"#select dictionary %d %s %s\n", getArg(q,1), getTypeName(getVarType(mb, idx[getArg(q,1)])), getTypeName(getVarType(mb, val[getArg(q,1)])));
#endif
						j = getArg(q,0);
						idx[j] = idx[getArg(q,1)];
						getArg(q,1) = val[getArg(q,1)];
						getArg(q,0)= newTmpVariable(mb, getVarType(mb,getArg(q,1)));
						val[j] = getArg(q,0);
						setVarUDFtype(mb,idx[j]);
						setVarUDFtype(mb,val[j]);
						pushInstruction(mb,q);
#ifdef DEBUG_OPT_DICTIONARY 
						mnstr_printf(GDKout,"dictionary %d %s -> %d %d\n", j, getVarName(mb,j), idx[j],val[j]);
						mnstr_printf(GDKout,"#dictionary %d %s %s\n", j, getTypeName(getVarType(mb, idx[j])), getTypeName(getVarType(mb, val[j])));
#endif
						actions++;
						continue;
				} 
				if ( (getFunctionId(q) == uselectRef  ||
					 getFunctionId(q) == thetauselectRef ) &&
					 isDiction(getArg(q,1)) ){
						/* case : r:bat[:oid,:void] := algebra.uselect(dict,arg) */
						/* become: k:= algebra.select(val,arg) ; v:= join(idx,k); r:= algebra.project(v);  */
						OPTDEBUGdictionary{
							mnstr_printf(GDKout,"#uselect dictionary %d %s %s\n", getArg(q,1), 
								getTypeName(getVarType(mb, idx[getArg(q,1)])), getTypeName(getVarType(mb, val[getArg(q,1)])));
						}
						setFunctionId(q, (getFunctionId(q)== uselectRef?selectRef:putName("thetaselect",11)));
						j = getArg(q,0);
						pushInstruction(mb,q);
						getArg(q,0)= newTmpVariable(mb, getVarType(mb,val[getArg(q,1)]));

						qq= newStmt(mb, algebraRef,joinRef);
						qq= pushArgument(mb,qq, idx[getArg(q,1)]);
						qq= pushArgument(mb,qq, getArg(q,0));
						qq= pushLng(mb,qq, lng_nil);
						getArg(q,1) = val[getArg(q,1)];
						setVarType(mb,getArg(qq,0), newBatType(getHeadType(getVarType(mb,getArg(qq,1))), getTailType(getVarType(mb,getArg(qq,2))))) ;
						setVarUDFtype(mb,getArg(qq,0));

						q = newStmt(mb, algebraRef, projectRef);
						q= pushArgument(mb,q, getArg(qq,0));
						getArg(q,0) = j;
						OPTDEBUGdictionary{
							mnstr_printf(GDKout,"#dictionary %d %s -> %d %d\n", j, getVarName(mb,j), idx[j],val[j]);
							mnstr_printf(GDKout,"#dictionary %d %s %s\n", j, getTypeName(getVarType(mb, idx[j])), getTypeName(getVarType(mb, val[j])));
						}
						actions++;
						continue;
				}
				/*
				 * The arguments can be singular or both ref to a dictionary pair.
				 * We deal with the most prominent case provided by SQL only.
				 * After a kdifference, the value table may contain too much information.
				 */
				if ( getFunctionId(q) == kdifferenceRef   && isDiction(getArg(q,1)) && ! isDiction(getArg(q,2))){
					j = getArg(q,0);
					val[j] = val[getArg(q,1)];
					getArg(q,1) = idx[getArg(q,1)];
					getArg(q,0)= newTmpVariable(mb, getVarType(mb,getArg(q,1)));
					idx[j] = getArg(q,0);
					pushInstruction(mb,q);
					setVarUDFtype(mb,idx[j]);
					setVarUDFtype(mb,val[j]);
					continue;
				} 
				/*
				 * The kunion requires that both operands have the same signature.
				 * This means we have to possibly expand the encoding table and derive an encoding for the 2nd argument.
				 */
				if ( getFunctionId(q) == kunionRef && isDiction(getArg(q,1))   && ! isDiction(getArg(q,2))){
					actions++;
					if( getVarType(mb,idx[getArg(q,2)]) == getArgType(mb,q,1)  ){
						j = getArg(q,0);
						val[j] = val[getArg(q,1)];
						getArg(q,1) = idx[getArg(q,1)];
						getArg(q,0)= newTmpVariable(mb, getVarType(mb,getArg(q,1)));
						idx[j] = getArg(q,0);
						setVarUDFtype(mb,idx[j]);
						setVarUDFtype(mb,val[j]);
						pushInstruction(mb,q);
						continue;
					}
					/* case: kunion(idx:bat[:oid,:bte], b:bat[:oid,:str] */
					/* avalnew := dictionary.expand(aval,b) */

					qq= newStmt(mb,dictionaryRef,"expand");
					setVarType(mb, getArg(qq,0), getVarType(mb,val[getArg(q,1)]));
					qq = pushArgument(mb,qq, val[getArg(q,1)]);
					qq = pushArgument(mb,qq, getArg(q,2));
					j = getArg(q,0);
					val[j] = getArg(qq,0);

					/* bidx := dictionary.encode(aval,b) */
					qq = newStmt(mb,"dictionary","encode");
					qq = pushArgument(mb,qq, val[j]);
					qq = pushArgument(mb,qq, getArg(q,2));

					pushInstruction(mb,q);
					getArg(q,1) = idx[getArg(q,1)];
					getArg(q,2) = getArg(qq,0);
					getArg(q,0)= newTmpVariable(mb, getVarType(mb,getArg(q,1)));
					idx[j] = getArg(q,0);
					setVarUDFtype(mb,idx[j]);
					setVarUDFtype(mb,val[j]);
					OPTDEBUGdictionary
						mnstr_printf(GDKout,"#dictionary %d %s -> %d %d\n", j, getVarName(mb,j), idx[j],val[j]);
					continue;
				}
				/*
				 * Marking the dictionary pair idx(oid,bte) and val(bte,int) can be realised by performing
				 * a semijoin on idx followed by the markT. The value is of no interested.
				 */
				if ( getFunctionId(q) == markTRef && isDiction(getArg(q,1)) ) {
					qq= newStmt(mb,batRef,reverseRef);
					qq= pushArgument(mb, qq, idx[getArg(q,1)]);
					j = getArg(qq,0);

					qq= newStmt(mb,algebraRef,semijoinRef);
					qq= pushArgument(mb, qq, j);
					qq= pushArgument(mb, qq, val[getArg(q,1)]);
					j = getArg(qq,0);

					qq= newStmt(mb,batRef,reverseRef);
					qq= pushArgument(mb, qq, j);
					actions++;
					getArg(q,1) = getArg(qq,0);
					pushInstruction(mb,q);
					continue;
				}
				/*
				 * Combination of the next column with the pivot table.
				 */
				if ( getFunctionId(q) == semijoinRef && isDiction(getArg(q,1))  && !isDiction(getArg(q,2)) ) {
					j = getArg(q,0);
					val[j] = val[getArg(q,1)];
					getArg(q,1) = idx[getArg(q,1)];
					getArg(q,0) = newTmpVariable(mb, getVarType(mb,getArg(q,1)));
					idx[j] = getArg(q,0);
					setVarUDFtype(mb,idx[j]);
					setVarUDFtype(mb,val[j]);
					pushInstruction(mb,q);
					continue;
				}
				/*
				 * In the (left)join you can prejoin the value encoding.
				 */
				if ( (getFunctionId(q) == leftjoinRef  || getFunctionId(q) == joinRef) &&
					isDiction(getArg(q,2)) && !isDiction(getArg(q,1)) ){
					j = getArg(q,0);
					val[j] = val[getArg(q,2)];
					getArg(q,2) = idx[getArg(q,2)];
					idx[j] = getArg(q,0) = newTmpVariable(mb, newBatType(getHeadType(getVarType(mb,getArg(q,1))), getTailType(getVarType(mb,getArg(q,2)))) );
					pushInstruction(mb,q);
					setVarUDFtype(mb,idx[j]);
					setVarUDFtype(mb,val[j]);
					continue;
				}
				if ( (getFunctionId(q) == leftjoinRef  || getFunctionId(q) == joinRef) &&
					!isDiction(getArg(q,2)) && isDiction(getArg(q,1)) ){
					k = getArg(q,0);
					j = getArg(q,1);
					pushInstruction(mb,q);
					getArg(q,1) = val[j];
					getArg(q,0) = newTmpVariable(mb, newBatType(getHeadType(getVarType(mb,val[j])), getTailType(getVarType(mb,getArg(q,2)))) );

					qq= newStmt(mb,algebraRef,joinRef);
					qq= pushArgument(mb,qq, idx[j]);
					qq= pushArgument(mb,qq, getArg(q,0));
					qq= pushLng(mb,qq, lng_nil);
					getArg(qq,0) = k;
					continue;
				}
				if ( (getFunctionId(q) == leftjoinRef  || getFunctionId(q) == joinRef) &&
					isDiction(getArg(q,2)) && isDiction(getArg(q,1)) ){
					/* j:= join((i1,v1), (i2,v2)) -> v:= join(v1,i2); i3:= join(v,v2); j:= join(i1,i3) */
					int n;
					j = getArg(q,0);
					k = getArg(q,1);
					n = getArg(q,2);

					pushInstruction(mb,q);
					getArg(q,1) = val[k];
					getArg(q,2) = idx[n];
					getArg(q,0) = newTmpVariable(mb, newBatType(getHeadType(getVarType(mb,getArg(q,1))), getTailType(getVarType(mb,getArg(q,2)))) );

					qq= newStmt(mb,algebraRef,joinRef);
					qq= pushArgument(mb,qq, getArg(q,0));
					qq= pushArgument(mb,qq, val[n]);
					qq= pushLng(mb,qq, lng_nil);
					getArg(qq,0) = newTmpVariable(mb, newBatType(getHeadType(getVarType(mb,getArg(qq,1))), getTailType(getVarType(mb,getArg(qq,2)))) );

					q= newStmt(mb,algebraRef,joinRef);
					q= pushArgument(mb,q, idx[k]);
					q= pushArgument(mb,q, getArg(qq,0));
					q= pushLng(mb,q, lng_nil);
					getArg(q,0) = j;
					continue;
				}
				/*
				 * Projections are easy. We simply drop the encoding table.
				 */
				if ( getFunctionId(q) == projectRef && isDiction(getArg(q,1))   && q->argc==2) {
					getArg(q,1) = idx[getArg(q,1)];
					pushInstruction(mb,q);
					continue;
				}
			}
			/*
			 * The aggregation group is more involved, because we use the expensive OID scheme to
			 * administer the groups. This calls for a modification of the encoding table.
			 * case : (ext,grp):= group.new((idx,val)),
			 * gives: (ext,grp) := group.new(idx); val:= dictionary.group(idx,val)
			 * We have to remember the grouping only.
			 * Derived paths are similar to the group new.
			 */
			if (  getModuleId(q) == groupRef ){
				if (q->argc == 3 &&
					getFunctionId(q) == subgroupdoneRef &&
				    isDiction(getArg(q,2))) {
					j = getArg(q,0);
					qq = newStmt(mb,dictionaryRef,groupRef);
					pushArgument(mb,qq, idx[getArg(q,2)]);
					pushArgument(mb,qq, val[getArg(q,2)]);
					val[j] = getArg(qq,0);

					getArg(q,2) = idx[getArg(q,2)];
					idx[j] = getArg(q,0);
					setVarUDFtype(mb,idx[j]);
					setVarUDFtype(mb,val[j]);
					pushInstruction(mb,q);
					actions++;
					continue;
				}
				if (q->argc == 5 &&
					getFunctionId(q) == subgroupdoneRef &&
				    isDiction(getArg(q,4))) {
					j = getArg(q,0);
					getArg(q,4) = idx[getArg(q,4)];
					if ( isDiction(getArg(q,3)) ){
						val[j] = val[getArg(q,0)];
						idx[j] = getArg(q,0);
						setVarUDFtype(mb,idx[j]);
						setVarUDFtype(mb,val[j]);
					} 
					pushInstruction(mb,q);
					actions++;
					continue;
				}
			}
			if (getModuleId(q) == batRef ){
				if (getFunctionId(q) == mirrorRef && isDiction(getArg(q,1))) {
					getArg(q,1) = idx[getArg(q,1)];
				}
			}
			/* default cases call for source reconstruction */
			for (j = q->retc; j < q->argc; j++)
			if ( isDiction(getArg(q,j)) ){
				/* recast void to type needed for the join */
				qq= newStmt(mb,algebraRef,joinRef);
				getArg(qq,0) = getArg(q,j);
				setVarType(mb,getArg(qq,0), newBatType(getHeadType(getVarType(mb,idx[getArg(q,j)])), getTailType(getVarType(mb,val[getArg(q,j)]))));
				qq= pushArgument(mb,qq,idx[getArg(q,j)]);
				qq= pushArgument(mb,qq,val[getArg(q,j)]);
				(void) pushLng(mb,qq, lng_nil);
				idx[getArg(q,j)] = 0;
				val[getArg(q,j)] = 0;
			}
			pushInstruction(mb,q);
		}
	}
	GDKfree(idx);
	GDKfree(val);
	return actions;
}
/*
 * Dictionary implementation.
 */

static void 
DICTcommit(BAT *b1, BAT *b2, BAT *b3, BAT *b4 )
{
	bat bl[5];
	int i = 0;

	bl[i++] = 0;
	if (b1)
		bl[i++] = ABS(b1->batCacheid);
	if (b2)
		bl[i++] = ABS(b2->batCacheid);
	if (b3)
		bl[i++] = ABS(b3->batCacheid);
	if (b4)
		bl[i++] = ABS(b4->batCacheid);
	TMsubcommit_list(bl, i);
}

/*
 * We should distinguish between enabling the dictionary optimizer
 * and initialization of the current run.
 */

int
DICTinit(void)
{
    	BAT *b, *bn, *bs;
	if ( dictIndex == NULL){
		MT_lock_set(&mal_contextLock, "dictionary");
		if ( dictIndex ){
			/* parallel initialization action */
			MT_lock_unset(&mal_contextLock, "dictionary");
			return 0;
		}
		b = BATdescriptor(BBPindex("dictIndex"));
		if (b) {
			bn = BATdescriptor(BBPindex("dictValue"));
			if (bn){
				bs = BATdescriptor(BBPindex("dictBase"));
				if ( bs ) {
					dictIndex = b;
					dictValue = bn;
					dictBase = bs;
				}
			} else {
				BBPreleaseref(b->batCacheid);
			}
		}
		MT_lock_unset(&mal_contextLock, "dictionary");
	}
	return dictIndex != 0 && dictValue != 0 && dictBase != 0;
}

str DICTinitialize(int *ret)
{
    	BAT *b, *bn, *bs;
	if ( DICTinit() == 0)
		return MAL_SUCCEED;
	MT_lock_set(&mal_contextLock, "dictionary");
	b = BATnew(TYPE_int,TYPE_str, 255);
	if (b == NULL) {
		MT_lock_unset(&mal_contextLock, "dictionary");
        	throw(MAL,"dictionary.initialize",RUNTIME_OBJECT_MISSING);
	}
	bn = BATnew(TYPE_int, TYPE_str, 255);
	if (bn == NULL) {
		BBPreleaseref(b->batCacheid);
		MT_lock_unset(&mal_contextLock, "dictionary");
        	throw(MAL,"dictionary.initialize",RUNTIME_OBJECT_MISSING);
	}
	bs = BATnew(TYPE_int, TYPE_str, 255);
	if (bs == NULL) {
		BBPreleaseref(b->batCacheid);
		BBPreleaseref(bn->batCacheid);
		MT_lock_unset(&mal_contextLock, "dictionary");
        	throw(MAL,"dictionary.initialize",RUNTIME_OBJECT_MISSING);
	}

	BATkey(b, TRUE);
	BBPrename(b->batCacheid, "dictIndex");
	BATmode(b, PERSISTENT);
	BBPkeepref(b->batCacheid);
	dictIndex = b;

	BATkey(bn, TRUE);
	BBPrename(bn->batCacheid, "dictValue");
	BATmode(bn, PERSISTENT);
	BBPkeepref(bn->batCacheid);
	dictValue = bn;

	BATkey(bs, TRUE);
	BBPrename(bs->batCacheid, "dictBase");
	BATmode(bs, PERSISTENT);
	BBPkeepref(bs->batCacheid);
	dictBase = bs;
	MT_lock_unset(&mal_contextLock, "dictionary");
	DICTcommit(dictIndex,dictValue, dictBase,0);
	(void) ret;
	return MAL_SUCCEED;
}

str DICTbind(int *idx, int *val, str * nme)
{
	int base = 0;
	*idx = *val = 0;
	if( DICTinit() == 0 )
		throw(MAL,"dictionary.bind","No catalog table");
	if ( DICTfind(idx, val, &base, *nme) )
		throw(MAL,"dictionary.bind","Not found in catalog table");
	BBPkeepref(*idx);
	BBPkeepref(*val);
	(void) base;
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.bind %d %d\n",*idx, *val);
#endif
	return MAL_SUCCEED;
}

static int
DICTtype(size_t cnt){
	if ( cnt < 255 )
		return TYPE_bte;
	if ( cnt < (1<<15)  - 1)
		return TYPE_sht;
	if ( cnt < (size_t) (1<<31) - 1 )
		return TYPE_int;
	return TYPE_lng;
}



str DICTcompress(int *ret, str *nme, int *bid)
{
	int idx,val,base, typ;
	size_t cnt;
	BAT *b, *bo, *bx = 0, *bv = 0, *bs, *bh =0;
	BATiter bi;
	BUN p,q;
	double ratio = 0.0;

	if( DICTinit()  == 0)
		throw(MAL,"dictionary.new","No catalog table");

	if ( DICTfind(&idx,&val, &base, *nme) == 0){
		/* duplicate found, the base table may have been changed, drop the old one */
		bx = (BAT *) BATdescriptor(idx);
		if ( bx)
			BBPreclaim(bx);
		bv = (BAT *) BATdescriptor(val);
		if ( bv)
			BBPreclaim(bv);
		BUNdelete(dictIndex, BUNfnd(dictIndex,(ptr) &idx), TRUE);
		BUNdelete(dictValue,BUNfnd(dictValue, (ptr) &val), TRUE);
		BUNdelete(dictBase,BUNfnd(dictBase, bid), TRUE);
	}
	(void) base;

	b = (BAT *) BATdescriptor(*bid);
    if (b == 0)
        /* Simple ignore the binding if you can;t find the bat */
        throw(MAL, "dict.new", RUNTIME_OBJECT_MISSING);

	/* compression is only relevant for larger tables */
	if ( BATcount(b) < (size_t) 10000)
		return MAL_SUCCEED; 
	/* or determine its memory footprint */

	/* alternatively, we sample the table */
	bs= BATsample(b, (size_t) 1000);
	if ( bs )
		bh= BAThistogram(bs);
	if ( bs && bh  && BATcount(bs) > 0)
		ratio = (double) BATcount(bh) / (double) BATcount(bs);
#ifdef DEBUG_OPT_DICTIONARY 
	if ( bs && bh )
		mnstr_printf(GDKout,"#dictionary.compress sample " BUNFMT " " BUNFMT" %f %f\n", BATcount(bs), BATcount(bh),ratio, ratio * BATcount(b));
#endif
	if ( bs) BBPreleaseref(bs->batCacheid);
	if ( bh) BBPreleaseref(bh->batCacheid);
	if ( ratio > 0.2 ){
		BBPreleaseref(b->batCacheid);
		return MAL_SUCCEED;
	}

	bo= BAThistogram(b); /* BATkunique(BATmirror(b));*/
	if ( bo == NULL){
		BBPreleaseref(b->batCacheid);
		throw(MAL,"dict.new","Can not access unique list");
	}
	bo = BATmirror(bo);
	
	cnt = BATcount(bo);
	typ= DICTtype(cnt);
	if( typ == TYPE_lng || typ == b->ttype ){
		/* don't create a new dictionary */
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.new %s not compressed\n",*nme);
#endif
		BBPreleaseref(b->batCacheid);
		BBPreleaseref(bo->batCacheid);
		return MAL_SUCCEED;
	}
	mnstr_printf(GDKout,"#dictionary.new %s compressed from type %s to %s " SZFMT" elm\n", *nme, getTypeName(b->ttype), getTypeName(typ), cnt);
	bv =  BATnew(typ, b->ttype, BATcount(b));
	/* create the dictionary representation */

	switch(typ){
	case TYPE_bte: 
		{	bte o;
			/* complete encoding table */
			bi = bat_iterator(bo);
			o = (bte) bte_nil+1;
			BATloop(bo,p,q){
				BUNins(bv, &o, BUNtail(bi,p),FALSE);
				o++;
			}
		} break;
	case TYPE_sht: 
		{	sht o;
			/* complete encoding table */
			bi = bat_iterator(bo);
			o = (sht) sht_nil+1;
			BATloop(bo,p,q){
				BUNins(bv, &o, BUNtail(bi,p),FALSE);
				o++;
			}
		} break;
	case TYPE_int:
		{	int o;
			/* complete encoding table */
			bi = bat_iterator(bo);
			o = (int)  int_nil+1;
			BATloop(bo,p,q){
				BUNins(bv, &o, BUNtail(bi,p),FALSE);
				o++;
			}
		} break;
	}
	bv->hsorted = 1;
	bv->hrevsorted = 0;
	if (!(bv->batDirty&2)) bv = BATsetaccess(bv, BAT_READ);
	BATderiveHeadProps(bv, 0);

	bx = BATjoin(b,BATmirror(bv), BUN_NONE);
	BATderiveHeadProps(bx, 0);
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.new values table " BUNFMT " \n", BATcount(bv));
#endif

	if (!(bx->batDirty&2)) bx = BATsetaccess(bx, BAT_READ);
	BUNins(dictIndex,&bx->batCacheid, *nme, FALSE);
	BUNins(dictValue,&bv->batCacheid, *nme, FALSE);
	BUNins(dictBase,&b->batCacheid, *nme, FALSE);
	BATmode(bx, PERSISTENT);
	BATmode(bv, PERSISTENT);
	BBPkeepref(bx->batCacheid);
	BBPkeepref(bv->batCacheid);
	BBPreleaseref(*bid);
	BBPreleaseref(bo->batCacheid);
	DICTcommit(bx,bv, dictIndex,dictValue);

	/* now the storage space of the BAT can be recycled */
	BBPreleaseref(b->batCacheid);
	/* you may drop the base table to safe diskspace, but this would
	   render the SQL catalog inconsistent, unless you leave an empty 
	   persistent BAT behind. */
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.new  %d->%d %d\n",typ,bx->batCacheid, bv->batCacheid);
#endif
	(void) ret;
	return MAL_SUCCEED;
}

/*
 * Extending a dictionary could lead to overflow of the reference type.
 * This means, we end up with larger idx tables.
 * This should trigger a partial re-compilation of the MAL program.
 */
str 
DICTexpand(int *rval, int *val, int *bid)
{
	BAT *bv, *b, *bn, *rv;
	BATiter bi;
	BUN p,q;

	b = (BAT *) BATdescriptor(*bid);
	rv = (BAT *) BATdescriptor(*val);
    if (rv == 0 || b == 0 ){
		if ( b  ) BBPreleaseref(b->batCacheid);
		if ( rv ) BBPreleaseref(rv->batCacheid);
        throw(MAL,"dictionary.expand",RUNTIME_OBJECT_MISSING);
	}

	/* check how many new elements should be added to the encoding table */
	bn = BATsemijoin(BATmirror(b), BATmirror(rv));
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.expand %d %d\n",*val,*bid);
	mnstr_printf(GDKout,"#dictionary.expand " BUNFMT " " BUNFMT " " BUNFMT "\n", 
		BATcount(rv), BATcount(b), BATcount(bn));
#endif
	if ( BATcount(bn) == 0){
		BBPreleaseref(bn->batCacheid);
		BBPreleaseref(b->batCacheid);
		BBPkeepref(*rval= rv->batCacheid);
		return MAL_SUCCEED;
	}
	bv= BATcopy(rv, DICTtype(BATcount(rv) + BATcount(bn)), rv->ttype,FALSE);
	BBPreleaseref(bn->batCacheid);
	BBPreleaseref(rv->batCacheid);
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
#endif
	switch(bv->htype){
	case TYPE_bte: 
		{	bte o;
			bi = bat_iterator(b);
			o = (bte) BATcount(bv);
			BATloop(b,p,q){
				BUNins(bv, &o, BUNtail(bi,p), FALSE);
				o++;
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	case TYPE_sht: 
		{	sht o;
			bi = bat_iterator(b);
			o = (sht) BATcount(bv);
			BATloop(b,p,q){
				BUNins(bv, &o, BUNtail(bi,p), FALSE);
				o++;
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	case TYPE_int: 
		{	int o;
			bi = bat_iterator(b);
			o = (int) BATcount(bv);
			BATloop(b,p,q){
				BUNins(bv, &o, BUNtail(bi,p), FALSE);
				o++;
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	}
	BATderiveHeadProps(bv, 0);

	BBPreleaseref(b->batCacheid);
	BBPkeepref(*rval = bv->batCacheid);
	return MAL_SUCCEED;
}
/*
 * Use a value table to encode the BAT.
 */
str 
DICTencode(int *ridx, int *vid, int *bid)
{
	BAT *b, *bv, *bx;

	b= (BAT *) BATdescriptor(*bid);
    if (b == 0)
        throw(MAL,"dictionary.expand",RUNTIME_OBJECT_MISSING);
	bv= (BAT *) BATdescriptor(*vid);
    if (bv == 0){
		BBPreleaseref(b->batCacheid);
        throw(MAL,"dictionary.expand",RUNTIME_OBJECT_MISSING);
	}
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.encode %d %d\n",*vid,*bid);
#endif

	bx = BATjoin(b,BATmirror(bv), BUN_NONE);
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.encode index in encoding table" BUNFMT " \n", BATcount(bx));
#endif
	BBPkeepref(*ridx= bx->batCacheid);
	BBPreleaseref(b->batCacheid);
	BBPreleaseref(bv->batCacheid);
	return MAL_SUCCEED;
}

str DICTdecompress(int *ret, str *nme)
{
	int idx = 0, val= 0, base = 0;
	BAT *bx, *bv, *bs, *b;
	BUN p;

	if( DICTinit() == 0 )
		throw(MAL,"dictionary.decompress","No catalog table");
#ifdef DEBUG_OPT_DICTIONARY 
	mnstr_printf(GDKout,"#dictionary.decompress %s\n",*nme);
#endif
	if ( DICTfind(&idx,&val,&base,*nme) == 0)
		return MAL_SUCCEED;
	bx= (BAT *) BATdescriptor(idx);
    if (bx == 0)
        throw(MAL,"dictionary.compress",RUNTIME_OBJECT_MISSING);
	bv= (BAT *) BATdescriptor(val);
    if (bv == 0){
		BBPreleaseref(bx->batCacheid);
        throw(MAL,"dictionary.compress",RUNTIME_OBJECT_MISSING);
	}
	bs= (BAT *) BATdescriptor(base);
    if (bs == 0){
		BBPreleaseref(bv->batCacheid);
		BBPreleaseref(bx->batCacheid);
        throw(MAL,"dictionary.compress",RUNTIME_OBJECT_MISSING);
	}
	MT_lock_set(&mal_contextLock, "dictionary");
	b = BATjoin(bx,bv,BUN_NONE);
	BATappend(bs,b,TRUE);
	BBPreleaseref(b->batCacheid);

	/* remove the element from the dictionary catalog */
	p = BUNfnd(BATmirror(dictIndex), (ptr) nme);
	if ( p != BUN_NONE ){
		BUNdelete(bx, p, TRUE);
		BUNdelete(bv, BUNfnd(BATmirror(dictValue), (ptr) nme ) , TRUE);
		BUNdelete(bs, BUNfnd(BATmirror(dictBase), (ptr) nme ) , TRUE);
	}
	BBPreleaseref(bv->batCacheid); BBPreleaseref(bx->batCacheid);
	BBPreleaseref(bs->batCacheid);
	MT_lock_unset(&mal_contextLock, "dictionary");
	(void) ret;
	return MAL_SUCCEED;
}

str DICTgroupid(int *ret, int *idx, int *val)
{
	BAT *bi, *bv, *b;
	BATiter bii,bvi;
	BUN p,q;

	bi= (BAT *) BATdescriptor(*idx);
    if (bi == 0)
        throw(MAL,"dictionary.map",RUNTIME_OBJECT_MISSING);
	bv= (BAT *) BATdescriptor(*val);
    if (bv == 0){
		BBPreleaseref(bi->batCacheid);
        throw(MAL,"dictionary.map",RUNTIME_OBJECT_MISSING);
	}
	b = BATnew(TYPE_oid,bv->ttype, BATcount(bv));
	bii = bat_iterator(bi);
	bvi = bat_iterator(bv);
	switch ( bv->htype){
	case TYPE_bte:
		{	bte v;
			oid o;
			BATloop(bv,p,q){
				v = *(bte*) BUNhead(bvi,p);
				o = *(oid*) BUNhead(bii,BUNfnd( BATmirror(bi),&v));
				BUNins(b, &o, BUNtail(bvi,p), FALSE);
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	case TYPE_sht:
		{	sht v;
			oid o;
			BATloop(bv,p,q){
				v = *(sht*) BUNhead(bvi,p);
				o = *(oid*) BUNhead(bii,BUNfnd( BATmirror(bi),&v));
				BUNins(b, &o, BUNtail(bvi,p), FALSE);
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	case TYPE_int: 
		{	int v;
			oid o;
			BATloop(bv,p,q){
				v = *(int*) BUNhead(bvi,p);
				o = *(oid*) BUNhead(bii,BUNfnd( BATmirror(bi),&v));
				BUNins(b, &o, BUNtail(bvi,p), FALSE);
			}
	#ifdef DEBUG_OPT_DICTIONARY 
		mnstr_printf(GDKout,"#dictionary.new values in encoding table" BUNFMT " \n", BATcount(bv));
	#endif
		} break;
	}

	BBPreleaseref(bi->batCacheid);
	BBPreleaseref(bv->batCacheid);
	BBPkeepref(*ret = b->batCacheid);
	return MAL_SUCCEED;
}
