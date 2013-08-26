/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY 1KIND, either express or implied. See the
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
 * @f mal_module
 * @a M. L. Kersten
 * @+ Module Management
 *
 * The operations are organized in separate MAL modules.
 * Each module contains a local symbol table of all function names
 * known to it so far. These names are stored in the global namespace
 * pool and never removed to guarantee stability of remote references.
 *
 * All dynamically loaded functions remain in existing
 * for the duration of the server session. Only the administrator can
 * load functions (upon system restart). Therefore, we do not have
 * to lock the dictionary table while traversing it for information.
 * Global (private) variables can be realized keeping the symbol table
 * and runtime stack around between requests.
 *
 * The symbol descriptors within a scope are organized in a list of subscopes
 * indexed by the first character of the function name.
 *
 * The modules are still collected in a linked list to ease access
 * and debug its content. It should be used with care for
 * resolving unknown modules.
 *
 * The function symbols are collected in a global name space table,
 * indexed by a small hash table. Once added, the name may not be removed
 * anymore.
 * Care should be taken not to generate many unique function names.
 */
/*
 * @+ Module scope management
 * Upon system restart, the global scope is created. It is called "root" and
 * does not contain any symbol definitions. It merely functions as an anchor
 * point for the modules to be added later.
 */

#include "monetdb_config.h"
#include "mal_module.h"
#include "mal_function.h"   /* for printFunction() */
#include "mal_namespace.h"
#include "mal_client.h"
#include "mal_interpreter.h"
#include "mal_listing.h"

Module mal_scope;    /* the root of the tree */
Module scopeJump[256][256];  /* to speedup access to correct scope */

static void newSubScope(Module scope){
	int len = (MAXSCOPE)*sizeof(Module);
	scope->subscope = (Symbol *) GDKzalloc(len);
}
/*
 * @-
 * Definition of a new module scope may interfere with concurrent
 * actions of multiple threads. This calls for a secure update
 * of the scope tree structure.
 * A jump table is mainted to provide a quick start in the module
 * table to find the correct one. This simple scheme safes about
 * 100ms/100K calls
 */

static void clrModuleJump(str nme, Module cur){
		if( scopeJump[(int)(*nme)][(int)(*(nme+1))]== cur)
			scopeJump[(int)(*nme)][(int)(*(nme+1))]= cur->sibling;
}
void setModuleJump(str nme, Module cur){
		cur->sibling= scopeJump[(int)(*nme)][(int)(*(nme+1))];
		scopeJump[(int)(*nme)][(int)(*(nme+1))]= cur;
}

/*
 * @+ Module scope management
 * Upon system restart, the global scope is created. It is called "root" and
 * does not contain any symbol definitions. It merely functions as an anchor
 * point for the modules to be added later.
 */
Module newModule(Module scope, str nme){
	Module cur;

	nme = putName(nme,strlen(nme));
	assert(nme != NULL);
	cur = (Module) GDKzalloc(sizeof(ModuleRecord));
	if( cur == NULL){
		GDKerror("newModule:"MAL_MALLOC_FAIL);
	} else {
		cur->name = nme;
		cur->outer = NULL;
		cur->sibling = NULL;
		cur->inheritance = TRUE;
		cur->subscope = NULL;
		cur->isAtomModule = FALSE;
	}
	if ( cur == NULL)
		return scope;
	newSubScope(cur);
	if( scope != NULL){
		cur->outer = scope->outer;
		scope->outer= cur;
		setModuleJump(nme,cur);
	}
	return cur;
}
/*
 * @-
 * The scope can be fixed. This is used by the parser to avoid creation of
 * a string structure when possible. Subsequently we can
 * replace the module name in the instructions to become a pointer
 * to the scope directly.
 * Reading a module often calls for opening a scope level
 * if it didn't exist.
 */
Module fixModule(Module scope, str nme){
	Module s= scope;
	if( scopeJump[(int)(*nme)][(int)(*(nme+1))])
		s= scopeJump[(int)(*nme)][(int)(*(nme+1))];
	while(s != NULL){
		if( nme == s->name )
			return s;
		s= s->outer;
	}
	return newModule(scope, nme);
}
/*
 * @-
 * A derived module inherits copies of all known
 * functions in the parent module. These can be
 * refined or expanded.
 */
void
deriveModule(Module scope, str nme){
	Module src= findModule(scope,nme);
	Symbol s;
	int i;
	if( src == scope) return;
	for(i=0; i<256; i++){
		s= src->subscope[i];
		while( s){
			/* copy the symbol */
			s= s->peer;
		}
	}
}
/*
 * @-
 * The freeModule operation throws away a symbol without
 * concerns on it whereabouts in the scope structure.
 * This routine therefore assumes care in use.
 * The final action of the system is to remove all
 * instructions and procedures. This forcefull action
 * helps in localization of memory leakages.
 */
static void freeSubScope(Module scope)
{
	int i;

	if (scope->subscope == NULL) 
		return;
	for(i=0;i<MAXSCOPE;i++) {
		if( scope->subscope[i]){
			freeSymbolList(scope->subscope[i]);
			scope->subscope[i]= NULL;
		}
	}
	GDKfree(scope->subscope);
	scope->subscope = 0;
}
void freeModule(Module m)
{
	Symbol s;

	if (m==NULL) 
		return;
	if ((s=findSymbolInModule(m, "epilogue")) != NULL) {
		InstrPtr pci = getInstrPtr(s->def,0);
		if (pci && pci->token == COMMANDsymbol && pci->argc == 1) {
			int ret = 0;

			assert(pci->fcn != NULL);
			(*pci->fcn)(&ret);
			(void)ret;
		}
	}
	freeSubScope(m);
	clrModuleJump(m->name, m);
	if (m->help)
		GDKfree(m->help);
	GDKfree(m);
}

void freeModuleList(Module s){
	Module t=s;
	while(s){
		t= s->outer;
		s->outer= NULL;
		freeModule(s);
		s=t;
	}
}

/*
 * After filling in a structure it is added to the multi-level symbol
 * table.  We keep a skip list of similarly named function symbols.
 * This speeds up searching provided the modules adhere to the
 * structure and group the functions as well.
 */
void insertSymbol(Module scope, Symbol prg){
	InstrPtr sig;
	int t;
	Module c;

	sig = getSignature(prg);
	if(getModuleId(sig) && getModuleId(sig)!= scope->name){
		/* move the definition to the proper place */
		/* default scope is the last resort */
		c= findModule(scope,getModuleId(sig));
		if ( c )
			scope = c;
	}
	t = getSubScope(getFunctionId(sig));
	if( scope->subscope == NULL)
		newSubScope(scope);
	if(scope->subscope[t] == prg){
		/* already known, last inserted */
	 } else  {
		prg->peer= scope->subscope[t];
		scope->subscope[t] = prg;
		if( prg->peer &&
			idcmp(prg->name,prg->peer->name) == 0)
			prg->skip = prg->peer->skip;
		else
			prg->skip = prg->peer;
	}
	assert(prg != prg->peer);
}
/*
 * @-
 * Removal of elements from the symbol table should be
 * done with care. For, it should be assured that
 * there are no references to the definition at the
 * moment of removal. This situation can not easily
 * checked at runtime, without tremendous overhead.
 */
void deleteSymbol(Module scope, Symbol prg){
	InstrPtr sig;
	int t;

	sig = getSignature(prg);
	if( getModuleId(sig) && getModuleId(sig)!= scope->name ){
		/* move the definition to the proper place */
		/* default scope is the last resort */
		Module c= findModule(scope,getModuleId(sig));
		if(c )
			scope = c;
	}
	t = getSubScope(getFunctionId(sig));
	if (scope->subscope[t] == prg) {
		scope->subscope[t] = scope->subscope[t]->peer;
		freeSymbol(prg);
	} else {
		Symbol nxt = scope->subscope[t];
		while (nxt->peer != NULL) {
			if (nxt->peer == prg) {
				nxt->peer = prg->peer;
				nxt->skip = prg->peer;
				freeSymbol(prg);
				return;
			}
			nxt = nxt->peer;
		}
	}
}

/*
 * @+ Inheritance
 * The MAL type checker does not apply type inheritance. Such a functionality
 * can, however, be readily implemented with a MAL rewriter.
 *
 * An early version of the interpreter contained a simple inheritance scheme,
 * which quickly became too complex to manage properly.
 * The default inheritance sequence is derived from the include order.
 * Most recently modules shield older modules, unless the user has specified
 * an explicit module name. This dus not work for dynamically loaded modules,
 * which may cause existing programs to be falsely bound to new implementations.
 *
 * The mechanism to control the inheritance structure consists of three features.
 * First, a module can be excluded from automatic inheritance by setting a flag.
 * This leads to skipping it during function resoluation.
 * The second mechanism is to enforce a partial order among the scopes.
 * The routine setInheritance(F,S) ensures that module F is inspected
 * before module S. In case it does not hold, S is placed just after F.
 *
 * Since the modules are currently shared between all clients,
 * such reshufflings may have drastic side effects.
 * To circumvent this, we have to make a private copy of the Scope chain
 * and the lookup table. [TODO]
 */
void setInheritanceMode(Module m,int flag){
	(void) flag;
	m->inheritance = 0;
}

Module setInheritance(Module h, Module f, Module s){
	Module fp, sp;
	int i=0, j=0;
	sp= h;
	while(sp->outer && sp->outer->outer !=s) {
		sp= s;
		i++;
	}
	fp= h;
	while(fp->outer !=f) {
		fp= f;
		j++;
	}
	if( j<i) return h;

	if(h==s){
		h= s->outer;
		s->outer= f->outer;
		f->outer=s;
	} else {
		sp->outer=s->outer;
		s->outer = f->outer;
		f->outer =s;
	}
	return h;
}
/*
 * @+ Searching the scope structure.
 * Finding a scope is unrestricted. For modules we explicitly look for
 * the start of a new module scope.
 * All core modules are accessed through the jumptable.
 * The 'user' module is an alias for the scope attached
 * to the current user.
 */
Module findModule(Module scope, str name){
	Module def=scope;
	if( name==NULL) return scope;
	scope= scopeJump[(int)(*name)][(int)(*(name+1))];
	while(scope != NULL){
			if( name == scope->name )
					return scope;
			scope= scope->sibling;
	}
	/* default is always matched with current */
	if( def->name==NULL) return NULL;
	return def;
}
int isModuleDefined(Module scope, str name){
	if( name==NULL || scope==NULL) return FALSE;
	if( name == scope->name) return TRUE;
	scope= scopeJump[(int)(*name)][(int)(*(name+1))];
	while(scope != NULL){
			if( name == scope->name )
					return TRUE;
			scope= scope->sibling;
	}
	return FALSE;
}
/*
 * @-
 * The routine findSymbolInModule starts at a MAL scope level and searches
 * an element amongst the peers. If it fails, it will recursively
 * inspect the outer scopes.
 *
 * In principal, external variables are subject to synchronization actions
 * to avoid concurrency conflicts. This also implies, that any parallel
 * block introduces a temporary scope.
 *
 * The variation on this routine is to dump the definition of
 * all matching definitions.
 */
Symbol findSymbolInModule(Module v, str fcn){
	Symbol s;
	if( v == NULL || fcn == NULL) return NULL;
	s= v->subscope[(int)(*fcn)];
	while(s!=NULL){
		if( idcmp(s->name,fcn)==0 ) return s;
		s= s->skip;
	}
	return NULL;
}

Symbol findSymbol(Module nspace, str mod, str fcn){
	Module m= findModule(nspace,mod);
	return findSymbolInModule(m,fcn);
}

int
findInstruction(Module scope, MalBlkPtr mb, InstrPtr pci){
	Module m;
	Symbol s;
	int i,fnd;

	for(m= findModule(scope,getModuleId(pci)); m; m= m->outer)
	if( m->name == getModuleId(pci) ) {
		s= m->subscope[(int)(getSubScope(getFunctionId(pci)))];
		for(; s; s= s->peer)
		if( getFunctionId(pci)==s->name && pci->argc == getSignature(s)->argc ){
			/* found it check argtypes */
			for( fnd=1, i = 0; i < pci->argc; i++)
				if ( getArgType(mb,pci,i) != getArgType(s->def,getSignature(s),i))
					fnd = 0;
			if( fnd)
				return 1;
		}
	}

	return 0;
}

int displayModule(stream *f, Module v, str fcn, int listing){
	Symbol s;
	int k=0;

	if( v == NULL || fcn == NULL) return 0;
	s= v->subscope[(int)(*fcn)];
	while(s!=NULL){
		if( idcmp(s->name,fcn)==0 ) {
			printFunction(f,s->def,0,listing);
			k++;
		}
		s= s->peer;
	}
	return k;
}
/*
 * @- Utilities
 */
static void  printModuleScope(stream *fd, Module scope, int tab, int outer)
{
	int j;
	Module s=scope;
	Symbol t;

	mnstr_printf(fd,"%smodule %s",
		(scope->isAtomModule?"atom ":""),s->name);
	mnstr_printf(fd,"\n");
	if( s->subscope)
	for(j=0;j<MAXSCOPE;j++)
	if(s->subscope[j]){
		mnstr_printf(fd,"[%c]",j);
		for(t= s->subscope[j];t!=NULL;t=t->peer) {
			mnstr_printf(fd," %s",t->name);
			if( getSignature(t)==NULL ||
			    (getSignature(t)->fcn==0 &&
			     getSignature(t)->token == COMMANDsymbol &&
			     getSignature(t)->blk==0) )
			    mnstr_printf(fd,"(?)");
		}
		mnstr_printf(fd,"\n");
	}
	mnstr_printf(fd,"\n");
	if(outer && scope->outer) printModuleScope(fd,scope->outer,tab+1, outer);
}

void showModules(stream *f, Module s)
{
	for(; s; s= s->outer) {
		mnstr_printf(f,"%s",s->name);
		if( s->subscope==0) mnstr_printf(f,"?");
		if(s->outer) mnstr_printf(f,",");
	}
	mnstr_printf(f,"\n");
}

void debugModule(stream *f, Module start, str nme){
	Module m;
	if( nme==0 || *nme ==0) printModuleScope(f, start,0,TRUE);
	else{
		char *s;
		for(s=nme;*s && (isalnum((int) *s) ||*s=='_');s++);
		*s = 0;
		m= findModule(start,nme);
		if( m== NULL) mnstr_printf(f,"Module '%s' not found\n",nme);
		else printModuleScope(f,m,0,FALSE);
	}
}
/*
 * @-
 * The commands and operators come with a short description.
 * The dumpManual() command produces a single XML file for post
 * processing and producing a system manual.
 */
void dumpManualHeader(stream *f){
	mnstr_printf(f,"<?xml version=\"1.0\"?>\n");
	mnstr_printf(f,"<manual>\n");
}
void dumpManualFooter(stream *f){
	mnstr_printf(f,"</manual>\n");
}
static int cmpModName(Module *f, Module *l){
	return strcmp((*f)->name, (*l)->name);
}
#if 0
int cmpFcnName(InstrPtr f, InstrPtr l){
	if(getFunctionId(f) && getFunctionId(l))
		return strcmp(getFunctionId(f), getFunctionId(l));
	return 0;
}
#endif
void dumpManual(stream *f, Module s, int recursive){
	int j;
	Symbol t;
	str ps, lnk, op=0, endtag=0;
	InstrPtr sig;
	Module list[256]; int k, top=0;


	if(s==NULL || f==NULL){
		return;
	}
	list[top++]=s;
	while(s->outer && recursive){ list[top++]= s->outer;s=s->outer;}

	if(top>1) qsort(list, top, sizeof(Module),
		(int(*)(const void *, const void *))cmpModName);

	for(k=0;k<top;k++){
	s= list[k];
	mnstr_printf(f,"<%smodule name=\"%s\">\n",
		(s->isAtomModule?"atom":""),xmlChr(s->name));
	if(s->help)
		mnstr_printf(f,"%s\n",s->help);
	if( s->subscope)
	for(j=0;j<MAXSCOPE;j++)
	if(s->subscope[j]){
		for(t= s->subscope[j];t!=NULL;t=t->peer) {
			sig= getSignature(t);

			if(op==0 || strcmp(op,t->name)){
			    if(endtag) mnstr_printf(f,"  </%s>\n",endtag);
			    mnstr_printf(f,"  <%s",operatorName(sig->token));
			    op = t->name;
			    mnstr_printf(f,"  name=\"%s\">\n",xmlChr(op));
			    if(t->def->help)
			    mnstr_printf(f,"    <comment>%s</comment>\n",
			        xmlChr(t->def->help));
			    op= t->name;
			    endtag= operatorName(sig->token);
			}

			ps= instruction2str(t->def,0,sig,0);
			lnk= strrchr(ps,'=');
			if(lnk && *(lnk+1)!='(') *lnk=0;

			mnstr_printf(f,"  <instantiation>\n");
			mnstr_printf(f,"    <signature>%s</signature>\n",
			    xmlChr(strchr(ps,'(')));
			if(lnk)
			mnstr_printf(f,"    <implementation>%s</implementation>\n",xmlChr(lnk+1));
			GDKfree(ps);
			if(t->def->help){
			    mnstr_printf(f,"    <comment>%s</comment>\n",
			        xmlChr(t->def->help));
			}
			mnstr_printf(f,"  </instantiation>\n");
		}
	}
	if(endtag) mnstr_printf(f,"  </%s>\n",endtag);
	mnstr_printf(f,"</%smodule>\n", (s->isAtomModule?"atom":""));
	endtag=0;
	}
}
void dumpManualSection(stream *f, Module s){
	int j;
	Symbol t;
	InstrPtr sig;
	str ps;
	char *pt;

	if(s==NULL || f==NULL || s->subscope== NULL)
		return;

	mnstr_printf(f,"@table\n");
	for(j=0;j<MAXSCOPE;j++)
	if(s->subscope[j]){
		for(t= s->subscope[j];t!=NULL;t=t->peer) {
			sig= getSignature(t);
			ps= instruction2str(t->def,0,sig,0);
			pt= strchr(ps,')');
			if(pt){
				pt++;
				*pt=0;
				mnstr_printf(f,"@tab %s\n",ps+1);
			} else
			mnstr_printf(f,"@tab %s\n",t->name);
			if( t->def->help)
				mnstr_printf(f," %s\n",t->def->help);
		}
	}
	mnstr_printf(f,"@end table\n");
}
/*
 * @-
 * The manual overview merely lists the mod.function names
 * in texi format for inclusion in the documetation.
 */
void dumpManualOverview(stream *f, Module s, int recursive){
	int j,z,rows,cols;
	Symbol t;
	InstrPtr sig;
	Module list[256]; int k, top=0, ftop, fnd;
	InstrPtr fcn[5000];
	int r, c, *x = NULL, x_sze = 0;


	if(s==NULL || f==NULL){
		return;
	}
	list[top++]=s;
	while(s->outer && recursive){ list[top++]= s->outer;s=s->outer;}

	if(top>1) qsort(list, top, sizeof(Module),
		(int(*)(const void *, const void *))cmpModName);

	cols = 4;
	mnstr_printf(f,"@multitable @columnfractions .24 .24 .24 .24\n");
	for(k=0;k<top;k++){
		s= list[k];
		ftop = 0;
		if( s->subscope)
		for(j=0;j<MAXSCOPE;j++)
		if(s->subscope[j]){
			for(t= s->subscope[j];t!=NULL;t=t->peer) {
				sig= getSignature(t);
				fnd = 0;
				fnd= *getFunctionId(sig) == '#';
				for(z=0; z<ftop; z++)
				if( strcmp(getFunctionId(fcn[z]),getFunctionId(sig))==0){
					fnd++;
					break;
				}
				if( fnd == 0 && ftop<5000)
					fcn[ftop++] = sig;
			}
		}
		for(j=0; j<ftop; j++)
		for(z=j+1; z<ftop; z++)
		if( strcmp(getFunctionId(fcn[j]),getFunctionId(fcn[z]))  >0) {
			 sig= fcn[j]; fcn[j]=fcn[z]; fcn[z]= sig;
		}
		mnstr_printf(f,"@" "item\n");
		rows = (ftop + cols - 1) / cols;
		if (x == NULL) {
			/* 2x* to allow for empty/skipped fields/columns */
			x_sze = 2 * cols * rows;
			x = (int*) GDKmalloc(x_sze * sizeof(int));
		} else if (2 * cols * rows > x_sze) {
			x_sze = 2 * cols * rows;
			x = (int*) GDKrealloc(x, x_sze * sizeof(int));
		}
		assert(x != NULL);
		for (z = 0; z < rows; z++) {
			x[cols * z] = z;
		}
		for (c = 1; c < cols; c++) {
			for (r = 0; r < rows; r++) {
				int i = (cols * r) + c - 1;
				if (z < ftop &&
				    (x[i] < 0 || strlen(getModuleId(fcn[x[i]])) + strlen(getFunctionId(fcn[x[i]])) < (size_t)(80 / cols))) {
					x[i+1] = z++;
				} else {
					/* HACK to avoid long names running into next column in printed version */
					x[i+1] = -1;
				}
			}
		}
		z = 0;
		for (r = 0; r < rows; r++) {
			for (c = 0; c < cols; c++) {
				str it[] = {"item", "tab"};
				mnstr_printf(f,"@" "%s\n", it[(c > 0)]);
				if (x[z] != -1) {
					mnstr_printf(f,"%s.%s\n",
						getModuleId(fcn[x[z]]), getFunctionId(fcn[x[z]]));
				}
				z++;
			}
		}
	}
	mnstr_printf(f,"@end multitable\n");
	if (x != NULL)
		GDKfree(x);
}
/*
 * @-
 * The manual help overview merely lists the mod.function names
 * together with the help oneliner in texi format for inclusion in the documentation.
 */
void dumpManualHelp(stream *f, Module s, int recursive){
	int j,z;
	Symbol t;
	InstrPtr sig;
	Module list[256]; int k, ftop, fnd,top=0;
	InstrPtr fcn[5000];
	str hlp[5000],msg;
	str hlp_texi = NULL;
	size_t hlp_texi_len = 0;


	if(s==NULL || f==NULL){
		return;
	}
	list[top++]=s;
	while(s->outer && recursive){ list[top++]= s->outer;s=s->outer;}

	if(top>1) qsort(list, top, sizeof(Module),
		(int(*)(const void *, const void *))cmpModName);

	mnstr_printf(f,"@multitable @columnfractions .2 .8 \n");
	for(k=0;k<top;k++){
		s= list[k];
		ftop = 0;
		if( s->subscope)
		for(j=0;j<MAXSCOPE;j++)
		if(s->subscope[j]){
			for(t= s->subscope[j];t!=NULL;t=t->peer) {
				sig= getSignature(t);
				fnd = 0;
				fnd= *getFunctionId(sig) == '#';
				for(z=0; z<ftop; z++)
				if( strcmp(getFunctionId(fcn[z]),getFunctionId(sig))==0){
					if( hlp[z] == 0)
						hlp[z]= t->def->help;
					fnd++;
					break;
				}
				if( fnd == 0 && ftop<5000){
					hlp[ftop]= t->def->help;
					fcn[ftop++] = sig;
				}
			}
		}
		for(j=0; j<ftop; j++)
		for(z=j+1; z<ftop; z++)
		if( strcmp(getFunctionId(fcn[j]),getFunctionId(fcn[z]))  >0) {
			msg= hlp[j]; hlp[j]=hlp[z]; hlp[z]= msg;
			 sig= fcn[j]; fcn[j]=fcn[z]; fcn[z]= sig;
		}
		mnstr_printf(f,"@" "item\n");
		for(z=0; z<ftop; z++){
			mnstr_printf(f,"@" "item %s.%s\n",
				getModuleId(fcn[z]), getFunctionId(fcn[z]));
			if( hlp[z] ) {
				str hlp_ = hlp[z];
				size_t hlp_len = 2*strlen(hlp[z]) + 1;
				if (hlp_texi == NULL) {
					hlp_texi = (str) GDKmalloc(hlp_len);
					hlp_texi_len = hlp_len;
				} else if (hlp_len > hlp_texi_len) {
					hlp_texi = (str) GDKrealloc(hlp_texi, hlp_len);
					hlp_texi_len = hlp_len;
				}
				if (hlp_texi != NULL) {
					str i = hlp[z];
					str o = hlp_texi;
					char c;
					while ((c = (*i++))) {
						/* quote special texi characters with '@' */
						switch (c) {
						case '@':
						case '{':
						case '}':
							*o++ = '@';
							break;
						}
						*o++ = c;
					}
					*o++ = '\0';
					hlp_ = hlp_texi;
				}
				if (strlen(getModuleId(fcn[z])) + strlen(getFunctionId(fcn[z])) >= 20) {
					/* HACK to avoid long names running into help text in printed version */
					mnstr_printf(f,"@" "item\n");
				}
				mnstr_printf(f,"@" "tab %s\n", hlp_);
			}
		}
	}
	mnstr_printf(f,"@end multitable\n");
	if (hlp_texi != NULL)
		GDKfree(hlp_texi);
}
/*
 * @-
 * Summarize the type resolution table.
 */

static void showModuleStat(stream *f, Module v,int cnt[256]){
	int sigs=0,i,max=0,m;
	Symbol s;
	int c[256];
	for(i=0;i<256;i++) c[i]=0;
	for(i=0;i<256;i++){
		m=0;
		s= v->subscope[i];
		while(s!=NULL){
			m++;
			sigs++;
			cnt[i]++;
			c[i]++;
			s= s->peer;
		}
		if(m>max)max=m;
	}
	m=0;
	for(i=0;i<256;i++)
	if(v->subscope[i]){
		mnstr_printf(f,"%20s",(m++ ==0?v->name:""));
		mnstr_printf(f,"[%c] %5d %5d\n",i,c[i],(cnt[i]-c[i]/2));
	}
	if(v->outer) showModuleStat(f,v->outer,cnt);
}
void showModuleStatistics(stream *f,Module s){
	int i,cnt[256];

	mnstr_printf(f,"%20s%5s%5s\n","module","#sig","avg chk");
	for(i=0;i<256;i++) cnt[i]=0;
	showModuleStat(f,s,cnt);
}
/*
 * @-
 * Some primitives to aid online help and completions.
 * Note that pattern matching is on string prefix.
 */
static int tstDuplicate(char **msg, char *s){
	int i;
	size_t len;
	len= strlen(s);
	for(i=0; msg[i]; i++)
		if( strncmp(s, msg[i], MAX(len,strlen(msg[i]))) == 0 &&
			strlen(s) == strlen(msg[i]) )
			return 1;
	return 0;
}

#define MAXHELP 500
char **getHelp(Module m, str inputpat, int completion)
{
	str pat, modnme, fcnnme = 0;
	Module m1;
	Symbol s;
	size_t len1 = 0,len2 = 0;
	int fnd=0;
	char *t, **msg, buf[BUFSIZ];
	int top=0, i,j,k, sig = 0, doc = 0;
	int maxhelp= MAXHELP;

#ifdef MAL_SCOPE_DEBUG
	printf("showHelp: %s",pat);
#endif
	msg= (char **) GDKmalloc( MAXHELP * sizeof(str));
	msg[top]=0;

	if (!inputpat)
		return msg;

	pat= GDKstrdup(inputpat);
	t= strchr(pat,'\n');
	if( t) *t=0;

	t = strchr(pat,')');
	if( t) { doc++; *t=0; completion=0; }
	t= strchr(pat,'(');
	if( t) { sig++; *t=0; completion=0; }

	/* rudimentary patterns only.
	 	*.nme  nme.* nme.nme *.*
	   ignore the rest.
	*/
	modnme= pat;
	if( (fcnnme = strchr(pat,'.')) ){
		*fcnnme++ = 0;
		if( strchr(modnme,'*'))
			modnme="*";
		if( strchr(fcnnme,'*') || *fcnnme==0)
			fcnnme="*";
	} else {
		modnme="*";
		fcnnme=pat;
		if( strchr(fcnnme,'*') || *fcnnme==0)
			fcnnme="*";
	}

	if( fcnnme && *fcnnme){
		len2 = strlen(fcnnme);
	}

	len1 = (int)strlen(modnme);

	/* display module information if there is no function */
	if( fcnnme == NULL){
		for(i=0; i< MAXSCOPE; i++)
		for(j=0; j< MAXSCOPE; j++){
			m= scopeJump[i][j];
			while(m != NULL){
				if( strncmp(modnme,m->name,len1) ==0  || *modnme=='*'){
					msg[top++] = GDKstrdup(m->name);
					msg[top] =0;
					if( top == maxhelp-1) {
						msg= (char **) GDKrealloc(msg,sizeof(str)* maxhelp);
						maxhelp+= MAXHELP;
					}
				}
				m= m->sibling;
			}
		}
		GDKfree(pat);
		return msg;
	}

	/* display module.function */
	m1 = findModule(m,modnme);
	if( m1 == 0  && *modnme != '*') {
		GDKfree(pat);
		return msg;
	}
	if( m1 ) m = m1;

#ifdef MAL_SCOPE_DEBUG
	printf("showHelp: %s %s [" SZFMT "] %s %s\n",
			modnme,fcnnme,len2, (doc?"doc":""), (sig?"sig":""));
#endif
	for(i=0; i< MAXSCOPE; i++)
	for(k=0; k< MAXSCOPE; k++){
	  m= scopeJump[i][k];
	  while( m){
		if( strncmp(modnme,m->name,len1) && *modnme!='*' ) {
			m= m->sibling;
			continue;
		}
		for(j=0;j<MAXSCOPE;j++)
		for(s= m->subscope[j]; s; s= s->peer)
			if( strncmp(fcnnme,s->name,len2)==0 || *fcnnme=='*') {
				fnd=0;
				if( completion ) {
					snprintf(buf,BUFSIZ," %s.%s",
						((*modnme=='*' || *modnme==0)? m->name:modnme),s->name);
					if( tstDuplicate(msg,buf+1) ) {
						fnd=1;
						continue;
					}
				} else
				if( doc) {
					char *v;

					fcnDefinition(s->def,s->def->stmt[0],buf,FALSE,buf,BUFSIZ);
					buf[0]=' ';

					v= strstr(buf,"address");
					if( v) *v=0;
					if( tstDuplicate(msg,buf+1) && s->def->help==0 ) fnd++;
					if(fnd) continue;

					msg[top++]= GDKstrdup(buf+1);
					if(v){
						*v='a';
						msg[top++]= GDKstrdup(v);
					}
					msg[top] = 0;

					if( s->def->help) {
						char *w;
						strcpy(buf+1,"comment ");
						v= buf+1+8;
						for( w= s->def->help; *w && v <buf+BUFSIZ-2; w++)
						if( *w == '\n'){
							/*ignore */
						} else *v++ = *w;
						*v = 0;
					} else fnd = 1; /* ignore non-existing comment */
					if(v){
						*v++ ='\n';
						*v=0;
					}
				} else if( strncmp(fcnnme,s->name,strlen(fcnnme))==0 ||
							*fcnnme=='*' ) {
					fcnDefinition(s->def,s->def->stmt[0],buf,FALSE,buf,BUFSIZ);
					buf[0]=' ';
					t= strstr(buf,"address");
					if( t) *t= 0;
				}
				if( fnd == 0 && buf[1]){
					msg[top++] = GDKstrdup(buf+1);
					msg[top] = 0;
				}
				if( top >= maxhelp-3){
					msg= (char **) GDKrealloc(msg,sizeof(str)* (maxhelp+MAXHELP));
					maxhelp+= MAXHELP;
				}
			}
			m= m->sibling;
		}
	}
	GDKfree(pat);
	return msg;
}
/*
 * @-
 * The second primitive of relevance is to find documentation matching
 * a keyword. Since we can not assume pcre to be everywhere, we keep
 * it simple.
 */
char **getHelpMatch(char *pat){
	char **msg, buf[BUFSIZ];
	Module m;
	Symbol s;
	int top = 0, i,j,k;
	int maxhelp= MAXHELP;

	msg= (char **) GDKmalloc( maxhelp * sizeof(str));
	msg[top]=0;

	if (!pat)
		return msg;

	for(i=0; i< MAXSCOPE; i++)
	for(k=0; k< MAXSCOPE; k++){
		m= scopeJump[i][k];
		while( m){
			for(j=0;j<MAXSCOPE;j++)
			if( m->subscope[j])
				for(s= m->subscope[j]; s; s= s->peer)
				if( strstr(m->name,pat) || strstr(s->name,pat) ||
					(s->def->help && strstr(s->def->help,pat))) {
					char *v,*w;
					fcnDefinition(s->def,s->def->stmt[0],buf,FALSE,buf,BUFSIZ);
					buf[0]=' ';
					if( s->def->help ){
						v= strchr(buf,0);
						assert (v != NULL); /* fool Coverity */
						*v++ = '\\';
						*v++ = 'n';
						*v++ = '#';
						for( w= s->def->help; *w && v <buf+BUFSIZ-3; w++)
						if( *w == '\n'){
							*v++ = '\\';
							*v++ = 'n';
							*v++ = '#';
							w++;
							if( isspace((int) *w)) {
								for(; *w && isspace((int) *w); w++);
								w--;
							}
						} else *v++ = *w;
						*v++ = '\\';
						*v++ = 'n';
						*v = 0;
					}
					msg[top++] = GDKstrdup(buf);
					msg[top] = 0;
					if( top == maxhelp-1){
						msg= (char **) GDKrealloc(msg,sizeof(str)* (maxhelp+MAXHELP));
						maxhelp+= MAXHELP;
					}
				}
			m= m->sibling;
		}
	}
	return msg;
}

void
showHelp(Module m, str txt, stream *fs){
	int i;
	char **msg = getHelp(m,txt,TRUE);
	for(i=0; msg[i]; i++)
		mnstr_printf(fs,"%s\n",msg[i]);
	if( i == 0){
		msg = getHelp(m,txt,0);
		for(i=0; msg[i]; i++)
			mnstr_printf(fs,"%s\n",msg[i]);
	}
}
/*
 * @-
 * The tags file is used by the mclient frontend to
 * enable language specific word completion.
 */
void dumpHelpTable(stream *f, Module s, str text, int flag){
	str *msg;
	int j,m;

	msg= getHelp(s,text,flag);
	for(m=0; msg[m]; m++ ) ;

	mnstr_printf(f,"&1 0 %d 1 %d\n",m,m);
	mnstr_printf(f,"# help # table_name\n");
	mnstr_printf(f,"# name # name\n");
	mnstr_printf(f,"# varchar # type\n");
	mnstr_printf(f,"# 0 # length\n");
	for(j=0; j<m; j++) {
		mnstr_printf(f,"[ \"%s\" ]\n",msg[j]);
		GDKfree(msg[j]);
	}
	GDKfree(msg);
}
void dumpSearchTable(stream *f, str text){
	str *msg;
	int j,m;

	msg= getHelpMatch(text);
	for(m=0; msg[m]; m++ ) ;

	mnstr_printf(f,"&1 0 %d 1 %d\n",m,m);
	mnstr_printf(f,"# help # table_name\n");
	mnstr_printf(f,"# name # name\n");
	mnstr_printf(f,"# varchar # type\n");
	mnstr_printf(f,"# 0 # length\n");
	for(j=0; j<m; j++) {
		mnstr_printf(f,"[ \"%s\" ]\n",msg[j]);
		GDKfree(msg[j]);
	}
	GDKfree(msg);
}
