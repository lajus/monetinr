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
 * author Martin Kersten
 *  URL box
 * This module implements the flattened tree model for URLs.
 * It is targeted at the GGLETICK student project
 */
/*
 *  Module initializaton
 * The content of this box my only be changed by the Administrator.
 */
#include "monetdb_config.h"
#include "urlbox.h"
#include "mal_linker.h"
#include "mal_authorize.h"

/*
 * Access to a box calls for resolving the first parameter
 * to a named box.
 */
#define authorize(X) { str tmp = NULL; rethrow("urlBox."X, tmp, AUTHrequireAdmin(&cntxt)); }
#define OpenBox(X) authorize(X); box= findBox("urlbox"); if( box ==0) throw(MAL, "urlbox."X, BOX_CLOSED);

#define MAXURLDEPTH 50
static int urlDepth = 0;
static BAT *urlBAT[MAXURLDEPTH];

str
URLBOXprelude(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;
	int depth;

	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;		/* fool compiler */
	authorize("prelude");
	box = openBox("urlbox");
	if (box == 0)
		throw(MAL, "urlbox.prelude", BOX_CLOSED);
	/* if the box was already filled we can skip initialization */
	for(depth=0; depth<MAXURLDEPTH; depth++) {
		urlBAT[depth]=0;
	}
	urlDepth= 0;
	return MAL_SUCCEED;
}

/*
 * Operator implementation
 */
str
URLBOXopen(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;		/* fool compiler */
	authorize("open");
	if (openBox("urlbox") != 0)
		return MAL_SUCCEED;
	throw(MAL, "urlbox.open", BOX_CLOSED);
}

str
URLBOXclose(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;		/* fool compiler */
	authorize("close");
	closeBox("urlbox", TRUE);
	return MAL_SUCCEED;
}

str
URLBOXdestroy(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;

	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;		/* fool compiler */
	OpenBox("destroy");
	destroyBox("urlbox");
	return MAL_SUCCEED;
}

/*
 * The real work starts here. We have to insert an URL.
 */
static int
URLBOXchop(str url, str *parts){
	char *s, *t;
	int depth=0;

	s= url;
	while( *s && *s != '\n'){
		t= s+1;
		while(*t && *t !='\n' && *t!= '/') t++;
		if( *t ){
			*t= 0;
		} else break;
		parts[depth++]= s;
		for( t++; *t && (*t == '\n' || *t== '/'); t++) ;
		s= t;
	}
	return depth;
}
static str
URLBOXinsert(char *tuple)
{
	str parts[MAXURLDEPTH];
	int i=0,depth;
	BAT *b;
	BUN p;
	BUN idx= 0;
	int prv=0;
	char buf[128];

	depth= URLBOXchop(tuple, parts);
	if( depth == 0) return MAL_SUCCEED;
	if( depth > urlDepth || urlBAT[0]== NULL){
		for(i=0; i<=depth; i++){
			/* make new bat */
			snprintf(buf, 128, "urlbox_%d", i);
			b = BATdescriptor(BBPindex(buf));
			if (b){
				urlBAT[i] = b;
				continue;
			}

			b = BATnew(TYPE_void, TYPE_str, 1024);
			if (b == NULL)
				throw(MAL, "urlbox.deposit", MAL_MALLOC_FAIL);
			BATseqbase(b,0);

			BATkey(b,TRUE);
			BBPrename(b->batCacheid, buf);
			BATmode(b, PERSISTENT);
			BATcommit(b);
			urlBAT[i] = b;
		}
		urlDepth= depth;
	}
	/*
	 * Find the common prefix first
	 */
	p= BUNfnd(BATmirror(urlBAT[0]),parts[0]);
	if (p != BUN_NONE) 
		for( i=1; i<depth; i++){
			/* printf("search [%d]:%s\n",i,parts[i]);*/
			p = BUNfnd(BATmirror(urlBAT[i]),parts[i]);
			if (p == BUN_NONE) 
				break;
			prv = *(int*)Hloc(urlBAT[i], p);
		}
	else i = 0;
	/*
	 * Insert the remainder as a new url string
	 */
	for( ; i<depth; i++){
		/* printf("update [%d]:%s\n",i,parts[i]);*/
		idx = BATcount(urlBAT[i]);
		BUNins(urlBAT[i], (ptr) &prv, parts[i], FALSE);
		assert(idx <= GDK_int_max);
		prv = (int) idx;
	}
	return MAL_SUCCEED;
}
#define SIZE 1*1024*1024

str
URLBOXdepositFile(int *r, str *fnme){

	stream *fs;
	bstream *bs;
	char *s,*t;
	int len=0;
	char buf[PATHLENGTH];

	(void) r; 
	if( **fnme == '/')
		snprintf(buf,PATHLENGTH,"%s", *fnme);
	else 
		snprintf(buf,PATHLENGTH,"%s/%s", monet_cwd, *fnme);
	/* later, handle directory separator */
	fs= open_rastream(buf);
	if( fs == NULL )
		throw(MAL, "urlbox.deposit", RUNTIME_FILE_NOT_FOUND "%s",buf);
	if( mnstr_errnr(fs) ) {
		close_stream(fs);
		throw(MAL, "urlbox.deposit", RUNTIME_FILE_NOT_FOUND "%s",buf);
	}
	bs= bstream_create(fs,SIZE);
	if( bs == NULL) 
		throw(MAL, "urlbox.deposit", MAL_MALLOC_FAIL);
	while( bstream_read(bs,bs->size-(bs->len-bs->pos)) != 0 &&
		!mnstr_errnr(bs->s) ){
		s= bs->buf;
		for( t=s; *t ; ){
			while(t < bs->buf+bs->len && *t && *t != '\n') t++;
			if(t== bs->buf+bs->len || *t != '\n'){
				/* read next block if possible after shift  */
				assert(t-s <= INT_MAX);
				len = (int) (t-s);
				memcpy(bs->buf, s, len);
				bs->len = len;
				bs->pos = 0;
				break;
			}
			/* found a string to be processed */
			*t = 0;
			URLBOXinsert(s);
			*t= '\n';
			s= t+1;
			t= s;
		}
	}

	bstream_destroy(bs);
	mnstr_close(fs);
	mnstr_destroy(fs);
	return MAL_SUCCEED;
}
str
URLBOXdeposit(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str url;
	Box box;
	char tuple[2048];

	(void) cntxt;
	(void) mb;
	OpenBox("deposit");
	url = *(str*) getArgReference(stk, pci, 1);
	if( strlen(url) <2048)
		strcpy(tuple,url);
	else throw(MAL, "urlbox.deposit", ILLEGAL_ARGUMENT " URL too long");
	return URLBOXinsert(url);
}

str
URLBOXtake(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str url, parts[MAXURLDEPTH];
	Box box;

	(void) cntxt;
	OpenBox("take");
	url = *(str*) getArgReference(stk, pci, 1);
	url = GDKstrdup(url);
	URLBOXchop(url, parts);
	GDKfree(url);
	(void) mb;
	return MAL_SUCCEED;
}

str
URLBOXrelease(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str name;
	Box box;

	(void) cntxt;
	(void) mb;		/* fool compiler */

	OpenBox("release");
	name = *(str*) getArgReference(stk, pci, 1);
	if (releaseBox(box, name))
		throw(MAL, "urlbox.release", OPERATION_FAILED);
	return MAL_SUCCEED;
}
str
URLBOXreleaseOid(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str name;
	Box box;

	(void) cntxt;
	(void) mb;		/* fool compiler */

	OpenBox("release");
	name = *(str*) getArgReference(stk, pci, 1);
	if (releaseBox(box, name))
		throw(MAL, "urlbox.release", OPERATION_FAILED);
	return MAL_SUCCEED;
}

str
URLBOXreleaseAll(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;

	(void) cntxt;
	(void) mb;
	(void) stk;
	(void) pci;		/* fool compiler */
	OpenBox("release");
	releaseAllBox(box);
	return MAL_SUCCEED;
}

str
URLBOXdiscard(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str name;
	Box box;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("discard");
	name = *(str*) getArgReference(stk, pci, 1);
	if (discardBox(box, name) == 0)
		throw(MAL, "urlbox.discard", OPERATION_FAILED);
	return MAL_SUCCEED;
}
str
URLBOXdiscardOid(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str name;
	Box box;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("discard");
	name = *(str*) getArgReference(stk, pci, 1);
	if (discardBox(box, name) == 0)
		throw(MAL, "urlbox.discard", OPERATION_FAILED);
	return MAL_SUCCEED;
}
str
URLBOXdiscardAll(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str name;
	Box box;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("discard");
	name = *(str*) getArgReference(stk, pci, 1);
	if (discardBox(box, name) == 0)
		throw(MAL, "urlbox.discard", OPERATION_FAILED);
	return MAL_SUCCEED;
}

str
URLBOXtoString(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;
	int i, len = 0;
	ValPtr v;
	str nme, s = 0;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("toString");
	nme = *(str*) getArgReference(stk, pci, 1);
	i = findVariable(box->sym, nme);
	if (i < 0)
		throw(MAL, "urlbox.toString", OPERATION_FAILED);

	v = &box->val->stk[i];
	if (v->vtype == TYPE_str)
		s = v->val.sval;
	else
		(*BATatoms[v->vtype].atomToStr) (&s, &len, v);
	if (s == NULL)
		throw(MAL, "urlbox.toString", OPERATION_FAILED "illegal value");
	VALset(getArgReference(stk,pci,0), TYPE_str, s);
	return MAL_SUCCEED;
}

str
URLBOXnewIterator(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;
	oid *cursor;
	ValPtr v;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("iterator");
	cursor = (oid *) getArgReference(stk, pci, 0);
	v = getArgReference(stk,pci,1);
	if ( nextBoxElement(box, cursor, v) == oid_nil)
		throw(MAL, "urlbox.iterator", OPERATION_FAILED);
	return MAL_SUCCEED;
}

str
URLBOXhasMoreElements(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	Box box;
	oid *cursor;
	ValPtr v;

	(void) cntxt;
	(void) mb;		/* fool compiler */
	OpenBox("iterator");
	cursor=  (oid *) getArgReference(stk, pci, 0);
	v = getArgReference(stk,pci,1);
	if ( nextBoxElement(box, cursor, v) == oid_nil)
		throw(MAL, "urlbox.iterator", OPERATION_FAILED);
	return MAL_SUCCEED;
}

str
URLBOXgetLevel(int *r, int *level){
	if( *level < 0 || *level >= urlDepth)
		throw(MAL, "urlbox.getLevel", OPERATION_FAILED "Illegal level");
	*r = urlBAT[*level]->batCacheid;
	BBPincref(*r,TRUE);
	return MAL_SUCCEED;
}

str
URLBOXgetNames(int *r){
	BAT *b;
	int i;
	b= BATnew(TYPE_void,TYPE_str, urlDepth+1);
	if( b== NULL)
		throw(MAL, "urlbox.getNames", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	for(i=0; i<urlDepth; i++){
		BUNappend(b, BBPname(urlBAT[i]->batCacheid), FALSE);
	}
	*r = b->batCacheid;
	BBPkeepref(*r);
	return MAL_SUCCEED;
}
str
URLBOXgetCount(int *r){
	BAT *b;
	int i;
	lng cnt;

	b= BATnew(TYPE_oid,TYPE_lng, urlDepth+1);
	if( b== NULL)
		throw(MAL, "urlbox.getNames", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	for(i=0; i<urlDepth; i++){
		cnt = (lng) BATcount(urlBAT[i]);
		BUNappend(b, &cnt, FALSE);
	}
	*r = b->batCacheid;
	BBPkeepref(*r);
	return MAL_SUCCEED;
}
str
URLBOXgetCardinality(int *r){
	BAT *b, *bn;
	int i;
	lng cnt;

	b= BATnew(TYPE_void,TYPE_lng, urlDepth+1);
	if( b== NULL)
		throw(MAL, "urlbox.getNames", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	for(i=0; i<urlDepth; i++){
		bn = (BAT *) BATkunique(BATmirror(urlBAT[i]));
		cnt = (oid) BATcount(bn);
		BBPunfix(bn->batCacheid);
		BUNins(b,&i, &cnt, FALSE);
	}
	*r = b->batCacheid;
	BBPkeepref(*r);
	return MAL_SUCCEED;
}

/* #define ROUND_UP(x,y) ((y)*(((x)+(y)-1)/(y)))*/
#define ROUND_UP(x,y) (x)

str
URLBOXgetSize(int *r){
	BAT *b, *bn;
	int i;
	lng tot;
	size_t size;

	b= BATnew(TYPE_void,TYPE_lng, urlDepth+1);
	if( b== NULL)
		throw(MAL, "urlbox.getNames", MAL_MALLOC_FAIL);
	BATseqbase(b,0);
	for(i=0; i<urlDepth; i++){
		bn= urlBAT[i];
		size = ROUND_UP(sizeof(BATstore), blksize);
		if (!isVIEW(bn)) {
			BUN cnt = BATcapacity(bn);

			size += ROUND_UP(bn->H->heap.size, blksize);
			size += ROUND_UP(bn->T->heap.size, blksize);
			if (b->H->vheap)
				size += ROUND_UP(bn->H->vheap->size, blksize);
			if (b->T->vheap)
				size += ROUND_UP(bn->T->vheap->size, blksize);
			if (bn->H->hash)
				size += ROUND_UP(sizeof(BUN) * cnt, blksize);
			if (bn->T->hash)
				size += ROUND_UP(sizeof(BUN) * cnt, blksize);
		}
		tot = size;
		BBPunfix(bn->batCacheid);
		BUNappend(b, &tot, FALSE);
	}
	*r = b->batCacheid;
	BBPkeepref(*r);
	return MAL_SUCCEED;
}
