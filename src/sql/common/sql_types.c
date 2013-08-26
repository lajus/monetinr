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
 * The typing scheme of SQL is quite elaborate. The standard introduces
 * several basic types with a plethora of functions.
 * As long as we haven't implemented a scheme to accept the
 * function type signature and relate it to a C-function linked
 * with the system, we have to patch the code below.
 *
 * Given the large number of examples, it should be relatively
 * easy to find something akin you intend to enter.
 */

#include "monetdb_config.h"
#include "sql_types.h"
#include "sql_keyword.h"	/* for keyword_exists(), keywords_insert(), init_keywords(), exit_keywords() */
#include <string.h>

#define END_SUBAGGR	1
#define END_AGGR	2
#define END_SUBTYPE	3
#define END_TYPE	4

list *aliases = NULL;
list *types = NULL;
list *aggrs = NULL;
list *funcs = NULL;

static list *localtypes = NULL;

int digits2bits(int digits) 
{
	if (digits < 3) 
		return 8;
	else if (digits < 5) 
		return 16;
	else if (digits < 10) 
		return 32;
	else if (digits < 17) 
		return 51;
	return 64;
}

int bits2digits(int bits) 
{
	if (bits < 4) 
		return 1;
	else if (bits < 7) 
		return 2;
	else if (bits < 10) 
		return 3;
	else if (bits < 14) 
		return 4;
	else if (bits < 16) 
		return 5;
	else if (bits < 20) 
		return 6;
	else if (bits < 24) 
		return 7;
	else if (bits < 27) 
		return 8;
	else if (bits < 30) 
		return 9;
	else if (bits <= 32) 
		return 10;
	return 19;
}

/* 0 cannot convert */
/* 1 set operations have very limited coersion rules */
/* 2 automatic coersion (could still require dynamic checks for overflow) */
/* 3 casts are allowed (requires dynamic checks) (sofar not used) */
static int convert_matrix[EC_MAX][EC_MAX] = {

/* EC_ANY */	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, /* NULL */
/* EC_TABLE */	{ 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/* EC_BIT */	{ 0, 0, 1, 1, 1, 0, 2, 2, 2, 0, 0, 0, 0, 0 },
/* EC_CHAR */	{ 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
/* EC_STRING */	{ 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
/* EC_BLOB */	{ 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
/* EC_NUM */	{ 0, 0, 2, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
/* EC_INTERVAL*/{ 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
/* EC_DEC */	{ 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
/* EC_FLT */	{ 0, 0, 0, 1, 1, 0, 1, 3, 1, 1, 0, 0, 0, 0 },
/* EC_TIME */	{ 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0 },
/* EC_DATE */	{ 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 3, 0 },
/* EC_TSTAMP */	{ 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0 },
/* EC_EXTERNAL*/{ 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int sql_type_convert (int from, int to) 
{
	int c = convert_matrix[from][to];
	return c;
}

int is_commutative(char *fnm)
{
	if (strcmp("sql_add", fnm) == 0 ||
	    strcmp("sql_mul", fnm) == 0)
	    	return 1;
	return 0;
}

void
base_init(sql_allocator *sa, sql_base * b, sqlid id, int flag, char *name)
{
	b->id = id;

	assert(sa);
	b->wtime = 0;
	b->rtime = 0;
	b->flag = flag;
	b->name = NULL;
	if (name)
		b->name = sa_strdup(sa,name);
}

void
sql_init_subtype(sql_subtype *res, sql_type *t, unsigned int digits, unsigned int scale)
{
	res->type = t;
	res->digits = digits ? digits : t->digits;
	if (t->digits && res->digits > t->digits)
		res->digits = t->digits;
	res->scale = scale;
	res->comp_type = NULL;
}

sql_subtype *
sql_create_subtype(sql_allocator *sa, sql_type *t, unsigned int digits, unsigned int scale)
{
	sql_subtype *res = SA_ZNEW(sa, sql_subtype);

	sql_init_subtype(res, t, digits, scale);
	return res;
}

static int
localtypes_cmp(int nlt, int olt)
{
	if (nlt == TYPE_flt || nlt == TYPE_dbl) {
		nlt = TYPE_dbl;
	} else if (nlt == TYPE_bte || nlt == TYPE_sht || nlt == TYPE_int || nlt == TYPE_wrd || nlt == TYPE_lng) {
		nlt = TYPE_lng;
	}
	if (nlt == olt)
		return 1;
	return 0;
}

sql_subtype *
sql_find_numeric(sql_subtype *r, int localtype, unsigned int digits)
{
	node *m, *n;

	if (localtype == TYPE_flt || localtype == TYPE_dbl) {
		localtype = TYPE_dbl;
	} else {
		localtype = TYPE_lng;
		if (digits > 64)
			digits = 64;
	}

	for (n = types->h; n; n = n->next) {
		sql_type *t = n->data;

		if (localtypes_cmp(t->localtype, localtype)) {
			if ((digits && t->digits >= digits) || (digits == t->digits)) {
				sql_init_subtype(r, t, digits, 0);
				return r;
			}
			for (m = n->next; m; m = m->next) {
				t = m->data;
				if (!localtypes_cmp(t->localtype, localtype)) {
					break;
				}
				n = m;
				if ((digits && t->digits >= digits) || (digits == t->digits)) {
					sql_init_subtype(r, t, digits, 0);
					return r;
				}
			}
		}
	}
	return NULL;
}

int 
sql_find_subtype(sql_subtype *res, char *name, unsigned int digits, unsigned int scale)
{
	/* todo add approximate info
	 * if digits/scale == 0 and no approximate with digits/scale == 0
	 * exists we could return the type with largest digits
	 *
	 * returning the largest when no exact match is found is now the
	 * (wrong?) default
	 */
	/* assumes the types are ordered on name,digits,scale where is always
	 * 0 > n
	 */
	node *m, *n;

	for (n = types->h; n; n = n->next) {
		sql_type *t = n->data;

		if (t->sqlname[0] == name[0] && strcmp(t->sqlname, name) == 0) {
			if ((digits && t->digits >= digits) || (digits == t->digits)) {
				sql_init_subtype(res, t, digits, scale);
				return 1;
			}
			for (m = n->next; m; m = m->next) {
				t = m->data;
				if (strcmp(t->sqlname, name) != 0) {
					break;
				}
				n = m;
				if ((digits && t->digits >= digits) || (digits == t->digits)) {
					sql_init_subtype(res, t, digits, scale);
					return 1;
				}
			}
			t = n->data;
			sql_init_subtype(res, t, digits, scale);
			return 1;
		}
	}
	return 0;
}

sql_subtype *
sql_bind_subtype(sql_allocator *sa, char *name, unsigned int digits, unsigned int scale)
{
	sql_subtype *res = SA_ZNEW(sa, sql_subtype);
	
	if (!sql_find_subtype(res, name, digits, scale)) {
		return NULL;
	}
	return res;
}

char *
sql_subtype_string(sql_subtype *t)
{
	char buf[BUFSIZ];

	if (t->digits && t->scale)
		snprintf(buf, BUFSIZ, "%s(%u,%u)", t->type->sqlname, t->digits, t->scale);
	else if (t->digits && t->type->radix != 2)
		snprintf(buf, BUFSIZ, "%s(%u)", t->type->sqlname, t->digits);

	else
		snprintf(buf, BUFSIZ, "%s", t->type->sqlname);
	return _STRDUP(buf);
}

sql_subtype *
sql_bind_localtype(char *name)
{
	node *n = localtypes->h;

	while (n) {
		sql_subtype *t = n->data;

		if (strcmp(t->type->base.name, name) == 0) {
			return t;
		}
		n = n->next;
	}
	assert(0);
	return NULL;
}

int
type_cmp(sql_type *t1, sql_type *t2)
{
	int res = 0;

	if (!t1 || !t2)
		return -1;
	/* types are only equal
	   iff they map onto the same systemtype */
	res = (t1->localtype - t2->localtype);
	if (res)
		return res;

	/* iff they fall into the same equivalence class */
	res = (t1->eclass - t2->eclass);
	if (res)
		return res;

	/* external types with the same system type are treated equaly */
	if (t1->eclass == EC_EXTERNAL)
		return res;

	/* sql base types need the same 'sql' name */
	return (strcmp(t1->sqlname, t2->sqlname));
}

int
subtype_cmp(sql_subtype *t1, sql_subtype *t2)
{
	if (!t1->type || !t2->type)
		return -1;
	if ( !(t1->type->eclass == t2->type->eclass && 
	       t1->type->eclass == EC_INTERVAL) &&
	      (t1->digits != t2->digits || t1->scale != t2->scale) )
		return -1;

	/* subtypes are only equal iff
	   they map onto the same systemtype */
	return (type_cmp(t1->type, t2->type));
}

int
is_subtype(sql_subtype *sub, sql_subtype *super)
/* returns true if sub is a sub type of super */
{
	if (!sub || !super)
		return 0;
	if (super->digits > 0 && sub->digits > super->digits) 
		return 0;
	if (super->digits == 0 && super->type->eclass == EC_STRING && 
	    (sub->type->eclass == EC_STRING || sub->type->eclass == EC_CHAR))
		return 1;
	/* subtypes are only equal iff
	   they map onto the same systemtype */
	return (type_cmp(sub->type, super->type) == 0);
}

char *
subtype2string(sql_subtype *t)
{
	char buf[BUFSIZ]; 

	if (t->digits > 0) {
		if (t->scale > 0)
			snprintf(buf, BUFSIZ, "%s(%u,%u)", 
				t->type->sqlname, t->digits, t->scale);
		else
			snprintf(buf, BUFSIZ, "%s(%u)", 
				t->type->sqlname, t->digits);
	} else {
			snprintf(buf, BUFSIZ, "%s", t->type->sqlname);
	}
	return _STRDUP(buf);
}

int 
subaggr_cmp( sql_subaggr *a1, sql_subaggr *a2)
{
	if (a1->aggr == a2->aggr) 
	    return subtype_cmp(&a1->res, &a2->res);
	return -1;
}

int 
subfunc_cmp( sql_subfunc *f1, sql_subfunc *f2)
{
	if (f1->func == f2->func) 
	    return subtype_cmp(&f1->res, &f2->res);
	return -1;
}

static int
arg_subtype_cmp(sql_arg *a, sql_subtype *t)
{
	if (a->type.type->eclass == EC_ANY)
		return 0;
	return (is_subtype(t, &a->type )?0:-1);
}

sql_subaggr *
sql_bind_aggr(sql_allocator *sa, sql_schema *s, char *sqlaname, sql_subtype *type)
{
	node *n = aggrs->h;

	while (n) {
		sql_func *a = n->data;
		sql_arg *arg = NULL;

		if (a->ops->h)
			arg = a->ops->h->data;

		if (strcmp(a->base.name, sqlaname) == 0 && (!arg ||
		    arg->type.type->eclass == EC_ANY || 
		    (type && is_subtype(type, &arg->type )))) {
			int scale = 0;
			int digits = 0;
			sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);

			ares->aggr = a;
			digits = a->res.digits;
			scale = a->res.scale;
			/* same scale as the input */
			if (type) {
				digits = type->digits;
				scale = type->scale;
			}
			/* same type as the input */
			if (a->res.type->eclass == EC_ANY) 
				sql_init_subtype(&ares->res, type->type, digits, scale);
			else
				sql_init_subtype(&ares->res, a->res.type, digits, scale);
			return ares;
		}
		n = n->next;
	}
	if (s) {
		node *n;

		if (s->funcs.set) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *a = n->data;
			sql_arg *arg = NULL;

			if ((!IS_AGGR(a) || !a->res.type))
				continue;

			if (a->ops->h)
				arg = a->ops->h->data;

			if (strcmp(a->base.name, sqlaname) == 0 && (!arg ||
		    	 	arg->type.type->eclass == EC_ANY || 
		    		(type && is_subtype(type, &arg->type )))) {
				int scale = 0;
				int digits = 0;
				sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);
		
				ares->aggr = a;
				digits = a->res.digits;
				scale = a->res.scale;
				/* same scale as the input */
				if (type) {
					digits = type->digits;
					scale = type->scale;
				}
				/* same type as the input */
				if (a->res.type->eclass == EC_ANY) 
					sql_init_subtype(&ares->res, type->type, digits, scale);
				else
					sql_init_subtype(&ares->res, a->res.type, digits, scale);
				return ares;
			}
		}
	}
	return NULL;
}

sql_subaggr *
sql_bind_aggr_(sql_allocator *sa, sql_schema *s, char *sqlaname, list *ops)
{
	node *n = aggrs->h;
	sql_subtype *type = NULL;

	if (ops->h)
		type = ops->h->data;

	while (n) {
		sql_func *a = n->data;

		if (strcmp(a->base.name, sqlaname) == 0 &&  
		    list_cmp(a->ops, ops, (fcmp) &arg_subtype_cmp) == 0) { 
			int scale = 0;
			int digits = 0;
			sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);

			ares->aggr = a;
			digits = a->res.digits;
			scale = a->res.scale;
			/* same scale as the input */
			if (type) {
				digits = type->digits;
				scale = type->scale;
			}
			/* same type as the input */
			if (a->res.type->eclass == EC_ANY) 
				sql_init_subtype(&ares->res, type->type, digits, scale);
			else
				sql_init_subtype(&ares->res, a->res.type, digits, scale);
			return ares;
		}
		n = n->next;
	}
	if (s) {
		node *n;

		if (s->funcs.set) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *a = n->data;

			if ((!IS_AGGR(a) || !a->res.type))
				continue;

			if (strcmp(a->base.name, sqlaname) == 0 && 
		    	    list_cmp(a->ops, ops, (fcmp) &arg_subtype_cmp) == 0) { 
				int scale = 0;
				int digits = 0;
				sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);
		
				ares->aggr = a;
				digits = a->res.digits;
				scale = a->res.scale;
				/* same scale as the input */
				if (type) {
					digits = type->digits;
					scale = type->scale;
				}
				/* same type as the input */
				if (a->res.type->eclass == EC_ANY) 
					sql_init_subtype(&ares->res, type->type, digits, scale);
				else
					sql_init_subtype(&ares->res, a->res.type, digits, scale);
				return ares;
			}
		}
	}
	return NULL;
}

sql_subaggr *
sql_find_aggr(sql_allocator *sa, sql_schema *s, char *sqlaname)
{
	node *n = aggrs->h;

	(void)s;
	while (n) {
		sql_func *a = n->data;

		if (strcmp(a->base.name, sqlaname) == 0) {
			int scale = 0;
			int digits = 0;
			sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);

			ares->aggr = a;
			digits = a->res.digits;
			scale = a->res.scale;
			sql_init_subtype(&ares->res, a->res.type, digits, scale);
			return ares;
		}
		n = n->next;
	}
	if (s) {
		node *n;

		if (s->funcs.set) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *a = n->data;

			if ((!IS_AGGR(a) || !a->res.type))
				continue;

			if (strcmp(a->base.name, sqlaname) == 0) {
				int scale = 0;
				int digits = 0;
				sql_subaggr *ares = SA_ZNEW(sa, sql_subaggr);
		
				ares->aggr = a;
				digits = a->res.digits;
				scale = a->res.scale;
				sql_init_subtype(&ares->res, a->res.type, digits, scale);
				return ares;
			}
		}
	}
	return NULL;
}

char *
sql_func_imp(sql_func *f)
{
	if (f->sql)
		return f->base.name;
	else
		return f->imp;
}

char *
sql_func_mod(sql_func *f)
{
	return f->mod;
}

int
is_sqlfunc(sql_func *f)
{
	return f->sql;
}

static sql_subfunc *
func_cmp(sql_allocator *sa, sql_func *f, char *name, int nrargs) 
{
	if (strcmp(f->base.name, name) == 0) {
		if (nrargs < 0 || list_length(f->ops) == nrargs) {
			sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

			fres->func = f;
			if (f->res.type)
				sql_init_subtype(&fres->res, f->res.type, f->res.digits, f->res.scale);
			if (f->res.comp_type) 
				fres->res.comp_type = f->res.comp_type;
			return fres;
		}
	}
	return NULL;
}

sql_subfunc *
sql_find_func(sql_allocator *sa, sql_schema *s, char *sqlfname, int nrargs, int type)
{
	sql_subfunc *fres;
	int key = hash_key(sqlfname);
	sql_hash_e *he = funcs->ht->buckets[key&(funcs->ht->size-1)]; 

	assert(nrargs);
	for (; he; he = he->chain) {
		sql_func *f = he->value;

		if (f->type != type) 
			continue;
		if ((fres = func_cmp(sa, f, sqlfname, nrargs )) != NULL) {
			return fres;
		}
	}
	if (s) {
		node *n;
		sql_func * f = find_sql_func(s, sqlfname);

		if (f && f->type == type && (fres = func_cmp(sa, f, sqlfname, nrargs )) != NULL) 
			return fres;
		if (s->funcs.set && s->funcs.set->ht) for (he=s->funcs.set->ht->buckets[key&(s->funcs.set->ht->size-1)]; he; he = he->chain) {
			sql_func *f = he->value;

			if (f->type != type) 
				continue;
			if ((fres = func_cmp(sa, f, sqlfname, nrargs )) != NULL) {
				return fres;
			}
		} else if (s->funcs.set && !s->funcs.set->ht) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *f = n->data;

			if (f->type != type) 
				continue;
			if ((fres = func_cmp(sa, f, sqlfname, nrargs )) != NULL) {
				return fres;
			}
		}
	}
	return NULL;
}


/* find function based on first argument */
sql_subfunc *
sql_bind_member(sql_allocator *sa, sql_schema *s, char *sqlfname, sql_subtype *tp, int nrargs)
{
	node *n;

	(void)s;
	assert(nrargs);
	for (n = funcs->h; n; n = n->next) {
		sql_func *f = n->data;

		if (!f->res.type)
			continue;
		if (strcmp(f->base.name, sqlfname) == 0) {
			if (list_length(f->ops) == nrargs && is_subtype(tp, &((sql_arg *) f->ops->h->data)->type)) {

				unsigned int scale = 0, digits;
				sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

				fres->func = f;
				/* same scale as the input */
				if (tp && tp->scale > scale)
					scale = tp->scale;
				digits = f->res.digits;
				if (tp && f->fix_scale == INOUT)
					digits = tp->digits;
				sql_init_subtype(&fres->res, f->res.type, digits, scale);
				if (f->res.comp_type) 
					fres->res.comp_type = f->res.comp_type;
				return fres;
			}
		}
	}
	if (tp->type->eclass == EC_NUM) {
	 	/* add second round but now look for Decimals only */
		for (n = funcs->h; n; n = n->next) {
			sql_func *f = n->data;

			if (!f->res.type)
				continue;
			if (strcmp(f->base.name, sqlfname) == 0 && list_length(f->ops) == nrargs) {
				if (((sql_arg *) f->ops->h->data)->type.type->eclass == EC_DEC && 
				    ((sql_arg *) f->ops->h->data)->type.type->localtype == tp->type->localtype) {

					unsigned int scale = 0, digits;
					sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

					fres->func = f;
					digits = f->res.digits;
					sql_init_subtype(&fres->res, f->res.type, digits, scale);
					if (f->res.comp_type) 
						fres->res.comp_type = f->res.comp_type;
					return fres;
				}
			}
		}
	}
	if (s) {
		node *n;

		if (s->funcs.set) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *f = n->data;

			if (!f->res.type)
				continue;
			if (strcmp(f->base.name, sqlfname) == 0) {
				if (list_length(f->ops) == nrargs && is_subtype(tp, &((sql_arg *) f->ops->h->data)->type)) {

					unsigned int scale = 0, digits;
					sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

					fres->func = f;
					/* same scale as the input */
					if (tp && tp->scale > scale)
						scale = tp->scale;
					digits = f->res.digits;
					if (tp && f->fix_scale == INOUT)
						digits = tp->digits;
					sql_init_subtype(&fres->res, f->res.type, digits, scale);
					if (f->res.comp_type) 
						fres->res.comp_type = f->res.comp_type;
					return fres;
				}
			}
		}
	}
	return NULL;
}

sql_subfunc *
sql_bind_func(sql_allocator *sa, sql_schema *s, char *sqlfname, sql_subtype *tp1, sql_subtype *tp2, int type)
{
	list *l = sa_list(sa);
	sql_subfunc *fres;

	if (tp1)
		list_append(l, tp1);
	if (tp2)
		list_append(l, tp2);

	fres = sql_bind_func_(sa, s, sqlfname, l, type);
	return fres;
}

sql_subfunc *
sql_bind_func3(sql_allocator *sa, sql_schema *s, char *sqlfname, sql_subtype *tp1, sql_subtype *tp2, sql_subtype *tp3, int type)
{
	list *l = sa_list(sa);
	sql_subfunc *fres;

	if (tp1)
		list_append(l, tp1);
	if (tp2)
		list_append(l, tp2);
	if (tp3)
		list_append(l, tp3);

	fres = sql_bind_func_(sa, s, sqlfname, l, type);
	return fres;
}

sql_subfunc *
sql_bind_func_(sql_allocator *sa, sql_schema *s, char *sqlfname, list *ops, int type)
{
	node *n = funcs->h;

	(void)s;
	for (; n; n = n->next) {
		sql_func *f = n->data;

		if (f->type != type) 
			continue;
		if (strcmp(f->base.name, sqlfname) == 0) {
			if (list_cmp(f->ops, ops, (fcmp) &arg_subtype_cmp) == 0) {
				unsigned int scale = 0, digits;
				sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

				fres->func = f;
				if (IS_FUNC(f)) { /* not needed for PROC/FILT */
					/* fix the scale */
					digits = f->res.digits;
					if (f->fix_scale > SCALE_NONE && f->fix_scale < SCALE_EQ) {
						for (n = ops->h; n; n = n->next) {
							sql_subtype *a = n->data;

							/* same scale as the input */
							if (a && a->scale > scale)
								scale = a->scale;
							if (a && f->fix_scale == INOUT)
								digits = a->digits;
						}
					} else if (f->res.scale) 
						scale = f->res.scale;
					/* same type as the first input */
					if (f->res.type->eclass == EC_ANY) {
						node *m;
						sql_subtype *a = NULL;
						for (n = ops->h, m = f->ops->h; n; n = n->next, m = m->next) {
							sql_arg *s = m->data;
							if (s->type.type->eclass == EC_ANY) {
								a = n->data;
							}
						}
						sql_init_subtype(&fres->res, a->type, digits, scale);
					} else {
						sql_init_subtype(&fres->res, f->res.type, digits, scale);
					}
				} else {
					fres->res.type = NULL;
				}
				return fres;
			}
		}
	}
	if (s) {
		node *n;

		if (s->funcs.set) for (n=s->funcs.set->h; n; n = n->next) {
			sql_func *f = n->data;

			if (f->type != type) 
				continue;
			if (strcmp(f->base.name, sqlfname) == 0) {
				if (list_cmp(f->ops, ops, (fcmp) &arg_subtype_cmp) == 0) {
					unsigned int scale = 0;
					sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

					fres->func = f;
					if (f->fix_scale > SCALE_NONE && f->fix_scale < SCALE_EQ) {
						for (n = ops->h; n; n = n->next) {
							sql_subtype *a = n->data;

							/* same scale as the input */
							if (a && a->scale > scale)
								scale = a->scale;
						}
					} else if (f->res.scale) 
						scale = f->res.scale;
					if (IS_FUNC(f)) {
						sql_init_subtype(&fres->res, f->res.type, f->res.digits, scale);
						if (f->res.comp_type) 
							fres->res.comp_type = f->res.comp_type;
					}
					return fres;
				}
			}
		}
	}
	return NULL;
}

sql_subfunc *
sql_bind_func_result(sql_allocator *sa, sql_schema *s, char *sqlfname, sql_subtype *tp1, sql_subtype *tp2, sql_subtype *res)
{
	list *l = sa_list(sa);
	sql_subfunc *fres;

	if (tp1)
		list_append(l, tp1);
	if (tp2)
		list_append(l, tp2);

	fres = sql_bind_func_result_(sa, s, sqlfname, l, res);
	return fres;
}

sql_subfunc *
sql_bind_func_result3(sql_allocator *sa, sql_schema *s, char *sqlfname, sql_subtype *tp1, sql_subtype *tp2, sql_subtype *tp3, sql_subtype *res)
{
	list *l = sa_list(sa);
	sql_subfunc *fres;

	if (tp1)
		list_append(l, tp1);
	if (tp2)
		list_append(l, tp2);
	if (tp3)
		list_append(l, tp3);

	fres = sql_bind_func_result_(sa, s, sqlfname, l, res);
	return fres;
}


sql_subfunc *
sql_bind_func_result_(sql_allocator *sa, sql_schema *s, char *sqlfname, list *ops, sql_subtype *res)
{
	node *n = funcs->h;

	(void)s;
	for (; n; n = n->next) {
		sql_func *f = n->data;

		if (!f->res.type) 
			continue;
		if (strcmp(f->base.name, sqlfname) == 0 && (is_subtype(&f->res, res) || f->res.type->eclass == EC_ANY) && list_cmp(f->ops, ops, (fcmp) &arg_subtype_cmp) == 0) {
			unsigned int scale = 0;
			sql_subfunc *fres = SA_ZNEW(sa, sql_subfunc);

			fres->func = f;
			for (n = ops->h; n; n = n->next) {
				sql_subtype *a = n->data;

				/* same scale as the input */
				if (a && a->scale > scale)
					scale = a->scale;
			}
			/* same type as the first input */
			if (f->res.type->eclass == EC_ANY) {
				node *m;
				sql_subtype *a = NULL;
				for (n = ops->h, m = f->ops->h; n; n = n->next, m = m->next) {
					sql_arg *s = m->data;
					if (s->type.type->eclass == EC_ANY) {
						a = n->data;
					}
				}
				sql_init_subtype(&fres->res, a->type, f->res.digits, scale);
			} else {
				sql_init_subtype(&fres->res, f->res.type, f->res.digits, scale);
				if (f->res.comp_type) 
					fres->res.comp_type = f->res.comp_type;
			}
			return fres;
		}
	}
	return NULL;
}

static void
sql_create_alias(sql_allocator *sa, char *name, char *alias)
{
	sql_alias *a = SA_ZNEW(sa, sql_alias);

	a->name = sa_strdup(sa, name);
	a->alias = sa_strdup(sa, alias);
	list_append(aliases, a);
	if (!keyword_exists(a->alias) )
		keywords_insert(a->alias, KW_ALIAS);
}

char *
sql_bind_alias(char *alias)
{
	node *n;

	for (n = aliases->h; n; n = n->next) {
		sql_alias *a = n->data;

		if (strcmp(a->alias, alias) == 0) {
			return a->name;
		}
	}
	return NULL;
}


sql_type *
sql_create_type(sql_allocator *sa, char *sqlname, unsigned int digits, unsigned int scale, unsigned char radix, unsigned char eclass, char *name)
{
	sql_type *t = SA_ZNEW(sa, sql_type);

	base_init(sa, &t->base, store_next_oid(), TR_OLD, name);
	t->sqlname = sa_strdup(sa, sqlname);
	t->digits = digits;
	t->scale = scale;
	t->localtype = ATOMindex(t->base.name);
	t->radix = radix;
	t->eclass = eclass;
	t->s = NULL;
	if (!keyword_exists(t->sqlname) && eclass != EC_INTERVAL) 
		keywords_insert(t->sqlname, KW_TYPE);
	list_append(types, t);

	list_append(localtypes, sql_create_subtype(sa, t, 0, 0));

	return t;
}

static sql_arg *
create_arg(sql_allocator *sa, char *name, sql_subtype *t)
{
	sql_arg *a = SA_ZNEW(sa, sql_arg);

	a->name = name;
	a->type = *t;
	return a;
}

sql_arg *
arg_dup(sql_allocator *sa, sql_arg *oa)
{
	sql_arg *a = SA_ZNEW(sa, sql_arg);

	a->name = sa_strdup(sa, oa->name);
	a->type = oa->type;
	return a;
}

sql_func *
sql_create_aggr(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tpe, sql_type *res)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	if (tpe)
		list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe, 0, 0)));
	assert(res);
	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, FALSE, TRUE, SCALE_NONE);
}

sql_func *
sql_create_aggr2(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tp1, sql_type *tp2, sql_type *res)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tp1, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tp2, 0, 0)));
	assert(res);
	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, FALSE, TRUE, SCALE_NONE);
}

sql_func *
sql_create_func(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tpe1, sql_type *tpe2, sql_type *res, int fix_scale)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	if (tpe1)
		list_append(l,create_arg(sa, NULL, sql_create_subtype(sa, tpe1, 0, 0)));
	if (tpe2)
		list_append(l,create_arg(sa, NULL, sql_create_subtype(sa, tpe2, 0, 0)));

	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, FALSE, FALSE, fix_scale);
}

sql_func *
sql_create_funcSE(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tpe1, sql_type *tpe2, sql_type *res, int fix_scale)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	if (tpe1)
		list_append(l,create_arg(sa, NULL, sql_create_subtype(sa, tpe1, 0, 0)));
	if (tpe2)
		list_append(l,create_arg(sa, NULL, sql_create_subtype(sa, tpe2, 0, 0)));

	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, TRUE, FALSE, fix_scale);
}


sql_func *
sql_create_func3(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tpe1, sql_type *tpe2, sql_type *tpe3, sql_type *res, int fix_scale)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe1, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe2, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe3, 0, 0)));

	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, FALSE, FALSE, fix_scale);
}

sql_func *
sql_create_func4(sql_allocator *sa, char *name, char *mod, char *imp, sql_type *tpe1, sql_type *tpe2, sql_type *tpe3, sql_type *tpe4, sql_type *res, int fix_scale)
{
	list *l = sa_list(sa);
	sql_subtype sres;

	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe1, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe2, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe3, 0, 0)));
	list_append(l, create_arg(sa, NULL, sql_create_subtype(sa, tpe4, 0, 0)));

	sql_init_subtype(&sres, res, 0, 0);
	return sql_create_func_(sa, name, mod, imp, l, &sres, FALSE, FALSE, fix_scale);
}


sql_func *
sql_create_func_(sql_allocator *sa, char *name, char *mod, char *imp, list *ops, sql_subtype *res, bit side_effect, bit aggr, int fix_scale)
{
	sql_func *t = SA_ZNEW(sa, sql_func);

	assert(res && ops);
	base_init(sa, &t->base, store_next_oid(), TR_OLD, name);
	t->imp = sa_strdup(sa, imp);
	t->mod = sa_strdup(sa, mod);
	t->ops = ops;
	if (aggr) {
		t->res = *res;
		t->type = F_AGGR;
	} else if (res) {	
		t->res = *res;
		t->type = F_FUNC;
	} else {
		t->res.type = NULL;
		t->type = F_PROC;
	}
	t->nr = list_length(funcs);
	t->sql = 0;
	t->side_effect = side_effect;
	t->fix_scale = fix_scale;
	t->s = NULL;
	if (aggr) {
		list_append(aggrs, t);
	} else {
		list_append(funcs, t);
		hash_add(funcs->ht, base_key(&t->base), t);
	}
	return t;
}

sql_func *
sql_create_sqlfunc(sql_allocator *sa, char *name, char *imp, list *ops, sql_subtype *res)
{
	sql_func *t = SA_ZNEW(sa, sql_func);

	assert(res && ops);
	base_init(sa, &t->base, store_next_oid(), TR_OLD, name);
	t->imp = sa_strdup(sa, imp);
	t->mod = sa_strdup(sa, "SQL");
	t->ops = ops;
	if (res) {	
		t->res = *res;
		t->type = F_FUNC;
	} else {
		t->res.type = NULL;
		t->type = F_PROC;
	}
	t->nr = list_length(funcs);
	t->sql = 1;
	t->side_effect = FALSE;
	list_append(funcs, t);
	hash_add(funcs->ht, base_key(&t->base), t);
	return t;
}

/* SQL service initialization
This C-code version initializes the
parser catalogs with typing information. Although, in principle,
many of the function signatures can be obtained from the underlying
database kernel, we have chosen for this explicit scheme for one
simple reason. The SQL standard dictates the types and we have to
check their availability in the kernel only. The kernel itself could
include manyfunctions for which their is no standard.
lead to unexpected
*/

static void
sqltypeinit( sql_allocator *sa)
{
	sql_type *ts[100];
	sql_type **strings, **numerical;
	sql_type **decimals, **floats, **dates, **end, **t;
	sql_type *STR, *BTE, *SHT, *INT, *LNG, *OID, *BIT, *DBL, *WRD, *DEC;
	sql_type *SECINT, *MONINT, *DTE; 
	sql_type *TME, *TMETZ, *TMESTAMP, *TMESTAMPTZ;
	sql_type *ANY, *TABLE;
	sql_func *f;

	ANY = sql_create_type(sa, "ANY", 0, 0, 0, EC_ANY, "void");

	t = ts;
	TABLE = *t++ = sql_create_type(sa, "TABLE", 0, 0, 0, EC_TABLE, "bat");
	*t++ = sql_create_type(sa, "PTR", 0, 0, 0, EC_TABLE, "ptr");

	BIT = *t++ = sql_create_type(sa, "BOOLEAN", 1, 0, 2, EC_BIT, "bit");
	sql_create_alias(sa, BIT->sqlname, "BOOL");

	strings = t;
	*t++ = sql_create_type(sa, "CHAR",    0, 0, 0, EC_CHAR,   "str");
	STR = *t++ = sql_create_type(sa, "VARCHAR", 0, 0, 0, EC_STRING, "str");
	*t++ = sql_create_type(sa, "CLOB",    0, 0, 0, EC_STRING, "str");

	numerical = t;

	BTE = *t++ = sql_create_type(sa, "TINYINT",   8, SCALE_FIX, 2, EC_NUM, "bte");
	SHT = *t++ = sql_create_type(sa, "SMALLINT", 16, SCALE_FIX, 2, EC_NUM, "sht");
	INT = *t++ = sql_create_type(sa, "INT",      32, SCALE_FIX, 2, EC_NUM, "int");
#if SIZEOF_WRD == SIZEOF_INT
	OID = *t++ = sql_create_type(sa, "OID", 31, 0, 2, EC_NUM, "oid");
	WRD = *t++ = sql_create_type(sa, "WRD", 32, SCALE_FIX, 2, EC_NUM, "wrd");
#endif
	LNG = *t++ = sql_create_type(sa, "BIGINT",   64, SCALE_FIX, 2, EC_NUM, "lng");
#if SIZEOF_WRD == SIZEOF_LNG
	OID = *t++ = sql_create_type(sa, "OID", 63, 0, 2, EC_NUM, "oid");
	WRD = *t++ = sql_create_type(sa, "WRD", 64, SCALE_FIX, 2, EC_NUM, "wrd");
#endif

	decimals = t;
	/* decimal(d,s) (d indicates nr digits,
	   s scale indicates nr of digits after the dot .) */
	*t++ = sql_create_type(sa, "DECIMAL",  2, SCALE_FIX, 10, EC_DEC, "bte");
	*t++ = sql_create_type(sa, "DECIMAL",  4, SCALE_FIX, 10, EC_DEC, "sht");
	DEC = *t++ = sql_create_type(sa, "DECIMAL",  9, SCALE_FIX, 10, EC_DEC, "int");
	*t++ = sql_create_type(sa, "DECIMAL", 19, SCALE_FIX, 10, EC_DEC, "lng");

	/* float(n) (n indicates precision of atleast n digits) */
	/* ie n <= 23 -> flt */
	/*    n <= 51 -> dbl */
	/*    n <= 62 -> long long dbl (with -ieee) (not supported) */
	/* this requires a type definition */

	floats = t;
	*t++ = sql_create_type(sa, "REAL", 24, SCALE_NOFIX, 2, EC_FLT, "flt");
	DBL = *t++ = sql_create_type(sa, "DOUBLE", 53, SCALE_NOFIX, 2, EC_FLT, "dbl");

	dates = t;
	MONINT = *t++ = sql_create_type(sa, "MONTH_INTERVAL", 32, 0, 2, EC_INTERVAL, "int");
	SECINT = *t++ = sql_create_type(sa, "SEC_INTERVAL", 19, SCALE_FIX, 10, EC_INTERVAL, "lng");
	TME = *t++ = sql_create_type(sa, "TIME", 7, 0, 0, EC_TIME, "daytime");
	TMETZ = *t++ = sql_create_type(sa, "TIMETZ", 7, SCALE_FIX, 0, EC_TIME, "daytime");
	DTE = *t++ = sql_create_type(sa, "DATE", 0, 0, 0, EC_DATE, "date");
	TMESTAMP = *t++ = sql_create_type(sa, "TIMESTAMP", 7, 0, 0, EC_TIMESTAMP, "timestamp");
	TMESTAMPTZ = *t++ = sql_create_type(sa, "TIMESTAMPTZ", 7, SCALE_FIX, 0, EC_TIMESTAMP, "timestamp");

	*t++ = sql_create_type(sa, "BLOB", 0, 0, 0, EC_BLOB, "sqlblob");
	end = t;
	*t = NULL;

	sql_create_aggr(sa, "not_unique", "sql", "not_unique", OID, BIT);
	/* well to be precise it does reduce and map */
	sql_create_func(sa, "not_uniques", "sql", "not_uniques", WRD, NULL, OID, SCALE_NONE);
	sql_create_func(sa, "not_uniques", "sql", "not_uniques", OID, NULL, OID, SCALE_NONE);

	/* functions needed for all types */
	sql_create_func(sa, "hash", "calc", "hash", ANY, NULL, WRD, SCALE_FIX);
	sql_create_func3(sa, "rotate_xor_hash", "calc", "rotate_xor_hash", WRD, INT, ANY, WRD, SCALE_NONE);
	sql_create_func(sa, "=", "calc", "=", ANY, ANY, BIT, SCALE_FIX);
	sql_create_func(sa, "<>", "calc", "!=", ANY, ANY, BIT, SCALE_FIX);
	sql_create_func(sa, "isnull", "calc", "isnil", ANY, NULL, BIT, SCALE_FIX);
	sql_create_func(sa, ">", "calc", ">", ANY, ANY, BIT, SCALE_FIX);
	sql_create_func(sa, ">=", "calc", ">=", ANY, ANY, BIT, SCALE_FIX);
	sql_create_func(sa, "<", "calc", "<", ANY, ANY, BIT, SCALE_FIX);
	sql_create_func(sa, "<=", "calc", "<=", ANY, ANY, BIT, SCALE_FIX);
	sql_create_aggr(sa, "zero_or_one", "sql", "zero_or_one", ANY, ANY);
	sql_create_aggr(sa, "exist", "aggr", "exist", ANY, BIT);
	sql_create_aggr(sa, "not_exist", "aggr", "not_exist", ANY, BIT);
	/* needed for relational version */
	sql_create_func(sa, "in", "calc", "in", ANY, ANY, BIT, SCALE_NONE);
	sql_create_func(sa, "identity", "batcalc", "identity", ANY, NULL, OID, SCALE_NONE);
	sql_create_func(sa, "rowid", "calc", "identity", ANY, NULL, INT, SCALE_NONE);
	/* needed for indices/clusters oid(schema.table,val) returns max(head(schema.table))+1 */
	sql_create_func3(sa, "rowid", "calc", "rowid", ANY, STR, STR, OID, SCALE_NONE);
	sql_create_aggr(sa, "min", "aggr", "min", ANY, ANY);
	sql_create_aggr(sa, "max", "aggr", "max", ANY, ANY);
	sql_create_func(sa, "sql_min", "calc", "min", ANY, ANY, ANY, SCALE_FIX);
	sql_create_func(sa, "sql_max", "calc", "max", ANY, ANY, ANY, SCALE_FIX);
	sql_create_func3(sa, "ifthenelse", "calc", "ifthenelse", BIT, ANY, ANY, ANY, SCALE_FIX);

	/* sum for numerical and decimals */
	sql_create_aggr(sa, "sum", "aggr", "sum", BTE, LNG);
	sql_create_aggr(sa, "sum", "aggr", "sum", SHT, LNG);
	sql_create_aggr(sa, "sum", "aggr", "sum", INT, LNG);
	sql_create_aggr(sa, "sum", "aggr", "sum", LNG, LNG);
	sql_create_aggr(sa, "sum", "aggr", "sum", WRD, WRD);

	t = decimals; /* BTE */
	sql_create_aggr(sa, "sum", "aggr", "sum", *(t), *(t+3));
	t++; /* SHT */
	sql_create_aggr(sa, "sum", "aggr", "sum", *(t), *(t+2));
	t++; /* INT */
	sql_create_aggr(sa, "sum", "aggr", "sum", *(t), *(t+1));
	t++; /* LNG */
	sql_create_aggr(sa, "sum", "aggr", "sum", *(t), *(t));

	/* prod for numerical and decimals */
	sql_create_aggr(sa, "prod", "aggr", "prod", BTE, LNG);
	sql_create_aggr(sa, "prod", "aggr", "prod", SHT, LNG);
	sql_create_aggr(sa, "prod", "aggr", "prod", INT, LNG);
	sql_create_aggr(sa, "prod", "aggr", "prod", LNG, LNG);
	/*sql_create_aggr(sa, "prod", "aggr", "prod", WRD, WRD);*/

	t = decimals; /* BTE */
	sql_create_aggr(sa, "prod", "aggr", "prod", *(t), *(t+3));
	t++; /* SHT */
	sql_create_aggr(sa, "prod", "aggr", "prod", *(t), *(t+2));
	t++; /* INT */
	sql_create_aggr(sa, "prod", "aggr", "prod", *(t), *(t+1));
	t++; /* LNG */
	sql_create_aggr(sa, "prod", "aggr", "prod", *(t), *(t));

	for (t = numerical; t < dates; t++) 
		sql_create_func(sa, "mod", "calc", "%", *t, *t, *t, SCALE_FIX);

	for (t = floats; t < dates; t++) {
		sql_create_aggr(sa, "sum", "aggr", "sum", *t, *t);
		sql_create_aggr(sa, "prod", "aggr", "prod", *t, *t);
	}
	/*
	sql_create_aggr(sa, "avg", "aggr", "avg", BTE, DBL);
	sql_create_aggr(sa, "avg", "aggr", "avg", SHT, DBL);
	sql_create_aggr(sa, "avg", "aggr", "avg", INT, DBL);
	sql_create_aggr(sa, "avg", "aggr", "avg", LNG, DBL);
	*/
	sql_create_aggr(sa, "avg", "aggr", "avg", DBL, DBL);

	sql_create_aggr(sa, "count_no_nil", "aggr", "count_no_nil", NULL, WRD);
	sql_create_aggr(sa, "count", "aggr", "count", NULL, WRD);

	sql_create_func(sa, "rank", "calc", "rank_grp", ANY, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "dense_rank", "calc", "dense_rank_grp", ANY, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "percent_rank", "calc", "precent_rank_grp", ANY, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "cume_dist", "calc", "cume_dist_grp", ANY, NULL, ANY, SCALE_NONE);
	sql_create_func(sa, "row_number", "calc", "mark_grp", ANY, NULL, INT, SCALE_NONE);

	sql_create_func3(sa, "rank", "calc", "rank_grp", ANY, OID, ANY, INT, SCALE_NONE);
	sql_create_func3(sa, "dense_rank", "calc", "dense_rank_grp", ANY, OID, ANY, INT, SCALE_NONE);
	sql_create_func3(sa, "percent_rank", "calc", "precent_rank_grp", ANY, OID, ANY, INT, SCALE_NONE);
	sql_create_func3(sa, "cume_dist", "calc", "cume_dist_grp", ANY, OID, ANY, ANY, SCALE_NONE);
	sql_create_func3(sa, "row_number", "calc", "mark_grp", ANY, OID, ANY, INT, SCALE_NONE);

	sql_create_func4(sa, "rank", "calc", "rank_grp", ANY, OID, OID, OID, INT, SCALE_NONE);
	sql_create_func4(sa, "dense_rank", "calc", "dense_rank_grp", ANY, OID, OID, OID, INT, SCALE_NONE);
	sql_create_func4(sa, "percent_rank", "calc", "precent_rank_grp", ANY, OID, OID, OID, INT, SCALE_NONE);
	sql_create_func4(sa, "cume_dist", "calc", "cume_dist_grp", ANY, OID, OID, OID, ANY, SCALE_NONE);
	sql_create_func4(sa, "row_number", "calc", "mark_grp", ANY, OID, OID, OID, INT, SCALE_NONE);

	sql_create_func(sa, "lag", "calc", "lag_grp", ANY, NULL, ANY, SCALE_NONE);
	sql_create_func(sa, "lead", "calc", "lead_grp", ANY, NULL, ANY, SCALE_NONE);
	sql_create_func(sa, "lag", "calc", "lag_grp", ANY, INT, ANY, SCALE_NONE);
	sql_create_func(sa, "lead", "calc", "lead_grp", ANY, INT, ANY, SCALE_NONE);

	sql_create_func3(sa, "lag", "calc", "lag_grp", ANY, OID, OID, ANY, SCALE_NONE);
	sql_create_func3(sa, "lead", "calc", "lead_grp", ANY, OID, OID, ANY, SCALE_NONE);
	sql_create_func4(sa, "lag", "calc", "lag_grp", ANY, INT, OID, OID, ANY, SCALE_NONE);
	sql_create_func4(sa, "lead", "calc", "lead_grp", ANY, INT, OID, OID, ANY, SCALE_NONE);

	sql_create_func(sa, "and", "calc", "and", BIT, BIT, BIT, SCALE_FIX);
	sql_create_func(sa, "or",  "calc",  "or", BIT, BIT, BIT, SCALE_FIX);
	sql_create_func(sa, "xor", "calc", "xor", BIT, BIT, BIT, SCALE_FIX);
	sql_create_func(sa, "not", "calc", "not", BIT, NULL,BIT, SCALE_FIX);

	/* all numericals */
	for (t = numerical; *t != TME; t++) {
		sql_subtype *lt = sql_bind_localtype((*t)->base.name);

		sql_create_func(sa, "sql_sub", "calc", "-", *t, *t, *t, SCALE_FIX);
		sql_create_func(sa, "sql_add", "calc", "+", *t, *t, *t, SCALE_FIX);
		sql_create_func(sa, "sql_mul", "calc", "*", *t, *t, *t, SCALE_MUL);
		sql_create_func(sa, "sql_div", "calc", "/", *t, *t, *t, SCALE_DIV);
		if (t < floats) {
			sql_create_func(sa, "bit_and", "calc", "and", *t, *t, *t, SCALE_FIX);
			sql_create_func(sa, "bit_or", "calc", "or", *t, *t, *t, SCALE_FIX);
			sql_create_func(sa, "bit_xor", "calc", "xor", *t, *t, *t, SCALE_FIX);
			sql_create_func(sa, "bit_not", "calc", "not", *t, NULL, *t, SCALE_FIX);
			sql_create_func(sa, "left_shift", "calc", "<<", *t, INT, *t, SCALE_FIX);
			sql_create_func(sa, "right_shift", "calc", ">>", *t, INT, *t, SCALE_FIX);
		}
		sql_create_func(sa, "sql_neg", "calc", "-", *t, NULL, *t, INOUT);
		sql_create_func(sa, "abs", "calc", "abs", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "sign", "calc", "sign", *t, NULL, INT, SCALE_NONE);
		/* scale fixing for all numbers */
		sql_create_func(sa, "scale_up", "calc", "*", *t, lt->type, *t, SCALE_NONE);
		sql_create_func(sa, "scale_down", "sql", "dec_round", *t, lt->type, *t, SCALE_NONE);
		/* numeric function on INTERVALS */
		if (*t != MONINT && *t != SECINT){
			sql_create_func(sa, "sql_sub", "calc", "-", MONINT, *t, MONINT, SCALE_FIX);
			sql_create_func(sa, "sql_add", "calc", "+", MONINT, *t, MONINT, SCALE_FIX);
			sql_create_func(sa, "sql_mul", "calc", "*", MONINT, *t, MONINT, SCALE_MUL);
			sql_create_func(sa, "sql_div", "calc", "/", MONINT, *t, MONINT, SCALE_DIV);
			sql_create_func(sa, "sql_sub", "calc", "-", SECINT, *t, SECINT, SCALE_FIX);
			sql_create_func(sa, "sql_add", "calc", "+", SECINT, *t, SECINT, SCALE_FIX);
			sql_create_func(sa, "sql_mul", "calc", "*", SECINT, *t, SECINT, SCALE_MUL);
			sql_create_func(sa, "sql_div", "calc", "/", SECINT, *t, SECINT, SCALE_DIV);
		}
	}
	for (t = decimals, t++; t != floats; t++) {
		sql_type **u;
		for (u = numerical; u != floats; u++) {
			if (*u == OID)
				continue;
			if ((*t)->localtype >  (*u)->localtype) {
				sql_create_func(sa, "sql_mul", "calc", "*", *t, *u, *t, SCALE_MUL);
				sql_create_func(sa, "sql_mul", "calc", "*", *u, *t, *t, SCALE_MUL);
			}
		}
	}

	for (t = decimals; t < dates; t++) 
		sql_create_func(sa, "round", "sql", "round", *t, BTE, *t, INOUT);

	for (t = numerical; t < end; t++) {
		sql_type **u;

		for (u = numerical; u < end; u++) 
			sql_create_func(sa, "scale_up", "calc", "*", *u, *t, *t, SCALE_NONE);
	}

	/* initial assignment to t is on purpose like this, such that the
	 * compiler (as of gcc-4.6) "sees" that we never go below the
	 * initial pointer, and hence don't get a
	 * error: array subscript is below array bounds */
	for (t = floats + (dates - floats - 1); t >= floats; t--) {
		sql_create_func(sa, "power", "mmath", "pow", *t, *t, *t, SCALE_FIX);
		sql_create_func(sa, "floor", "mmath", "floor", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "ceil", "mmath", "ceil", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "ceiling", "mmath", "ceil", *t, NULL, *t, SCALE_FIX);	/* JDBC */
		sql_create_func(sa, "sin", "mmath", "sin", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "cos", "mmath", "cos", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "tan", "mmath", "tan", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "asin", "mmath", "asin", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "acos", "mmath", "acos", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "atan", "mmath", "atan", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "atan", "mmath", "atan2", *t, *t, *t, SCALE_FIX);
		sql_create_func(sa, "sinh", "mmath", "sinh", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "cot", "mmath", "cot", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "cosh", "mmath", "cosh", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "tanh", "mmath", "tanh", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "sqrt", "mmath", "sqrt", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "exp", "mmath", "exp", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "log", "mmath", "log", *t, NULL, *t, SCALE_FIX);
		sql_create_func(sa, "log10", "mmath", "log10", *t, NULL, *t, SCALE_FIX);
	}
	sql_create_func(sa, "pi", "mmath", "pi", NULL, NULL, DBL, SCALE_NONE);

	sql_create_funcSE(sa, "rand", "mmath", "rand", NULL, NULL, INT, SCALE_NONE);
	sql_create_funcSE(sa, "rand", "mmath", "sqlrand", INT, NULL, INT, SCALE_NONE);

	/* Date functions */
	sql_create_func(sa, "curdate", "mtime", "current_date", NULL, NULL, DTE, SCALE_NONE);
	sql_create_func(sa, "current_date", "mtime", "current_date", NULL, NULL, DTE, SCALE_NONE);
	sql_create_func(sa, "curtime", "mtime", "current_time", NULL, NULL, TMETZ, SCALE_NONE);
	sql_create_func(sa, "current_time", "mtime", "current_time", NULL, NULL, TMETZ, SCALE_NONE);
	sql_create_func(sa, "current_timestamp", "mtime", "current_timestamp", NULL, NULL, TMESTAMPTZ, SCALE_NONE);
	sql_create_func(sa, "localtime", "mtime", "current_time", NULL, NULL, TME, SCALE_NONE);
	sql_create_func(sa, "localtimestamp", "mtime", "current_timestamp", NULL, NULL, TMESTAMP, SCALE_NONE);

	sql_create_func(sa, "sql_sub", "mtime", "date_sub_msec_interval", DTE, SECINT, DTE, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "date_sub_month_interval", DTE, MONINT, DTE, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "timestamp_sub_msec_interval", TMESTAMP, SECINT, TMESTAMP, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "timestamp_sub_month_interval", TMESTAMP, MONINT, TMESTAMP, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "timestamp_sub_msec_interval", TMESTAMPTZ, SECINT, TMESTAMPTZ, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "timestamp_sub_month_interval", TMESTAMPTZ, MONINT, TMESTAMPTZ, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "time_sub_msec_interval", TME, SECINT, TME, SCALE_FIX);

	sql_create_func(sa, "sql_sub", "mtime", "diff", DTE, DTE, INT, SCALE_FIX);
	sql_create_func(sa, "sql_sub", "mtime", "diff", TMESTAMP, TMESTAMP, LNG, SCALE_FIX);

	sql_create_func(sa, "sql_add", "mtime", "date_add_msec_interval", DTE, SECINT, DTE, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "addmonths", DTE, MONINT, DTE, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "timestamp_add_msec_interval", TMESTAMP, SECINT, TMESTAMP, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "timestamp_add_month_interval", TMESTAMP, MONINT, TMESTAMP, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "timestamp_add_msec_interval", TMESTAMPTZ, SECINT, TMESTAMPTZ, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "timestamp_add_month_interval", TMESTAMPTZ, MONINT, TMESTAMPTZ, SCALE_NONE);
	sql_create_func(sa, "sql_add", "mtime", "time_add_msec_interval", TME, SECINT, TME, SCALE_NONE);
	sql_create_func(sa, "local_timezone", "mtime", "local_timezone", NULL, NULL, SECINT, SCALE_FIX);

	sql_create_func(sa, "year", "mtime", "year", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "month", "mtime", "month", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "day", "mtime", "day", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "hour", "mtime", "hours", TME, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "minute", "mtime", "minutes", TME, NULL, INT, SCALE_FIX);
	f = sql_create_func(sa, "second", "mtime", "sql_seconds", TME, NULL, DEC, SCALE_NONE);
	/* fix result type */
	f->res.scale = 3;

	sql_create_func(sa, "year", "mtime", "year", TMESTAMP, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "month", "mtime", "month", TMESTAMP, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "day", "mtime", "day", TMESTAMP, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "hour", "mtime", "hours", TMESTAMP, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "minute", "mtime", "minutes", TMESTAMP, NULL, INT, SCALE_FIX);
	f = sql_create_func(sa, "second", "mtime", "sql_seconds", TMESTAMP, NULL, DEC, SCALE_NONE);
	/* fix result type */
	f->res.scale = 3;

	sql_create_func(sa, "year", "mtime", "year", MONINT, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "month", "mtime", "month", MONINT, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "day", "mtime", "day", SECINT, NULL, LNG, SCALE_NONE);
	sql_create_func(sa, "hour", "mtime", "hours", SECINT, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "minute", "mtime", "minutes", SECINT, NULL, INT, SCALE_NONE);
	sql_create_func(sa, "second", "mtime", "seconds", SECINT, NULL, INT, SCALE_NONE);

	sql_create_func(sa, "dayofyear", "mtime", "dayofyear", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "weekofyear", "mtime", "weekofyear", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "dayofweek", "mtime", "dayofweek", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "dayofmonth", "mtime", "day", DTE, NULL, INT, SCALE_FIX);
	sql_create_func(sa, "week", "mtime", "weekofyear", DTE, NULL, INT, SCALE_FIX);

	sql_create_funcSE(sa, "next_value_for", "sql", "next_value", STR, STR, LNG, SCALE_NONE);
	sql_create_func(sa, "get_value_for", "sql", "get_value", STR, STR, LNG, SCALE_NONE);
	sql_create_func3(sa, "restart", "sql", "restart", STR, STR, LNG, LNG, SCALE_NONE);
	for (t = strings; t < numerical; t++) {
		sql_create_func(sa, "locate", "str", "locate", *t, *t, INT, SCALE_NONE);
		sql_create_func3(sa, "locate", "str", "locate", *t, *t, INT, INT, SCALE_NONE);
		sql_create_func(sa, "substring", "str", "substring", *t, INT, *t, INOUT);
		sql_create_func3(sa, "substring", "str", "substring", *t, INT, INT, *t, INOUT);
		sql_create_func(sa, "like", "str", "like", *t, *t, BIT, SCALE_NONE);
		sql_create_func3(sa, "like", "str", "like", *t, *t, *t, BIT, SCALE_NONE);
		sql_create_func(sa, "ilike", "str", "ilike", *t, *t, BIT, SCALE_NONE);
		sql_create_func3(sa, "ilike", "str", "ilike", *t, *t, *t, BIT, SCALE_NONE);
		sql_create_func(sa, "not_like", "str", "not_like", *t, *t, BIT, SCALE_NONE);
		sql_create_func3(sa, "not_like", "str", "not_like", *t, *t, *t, BIT, SCALE_NONE);
		sql_create_func(sa, "not_ilike", "str", "not_ilike", *t, *t, BIT, SCALE_NONE);
		sql_create_func3(sa, "not_ilike", "str", "not_ilike", *t, *t, *t, BIT, SCALE_NONE);
		sql_create_func(sa, "patindex", "pcre", "patindex", *t, *t, INT, SCALE_NONE);
		sql_create_func(sa, "truncate", "str", "stringleft", *t, INT, *t, SCALE_NONE);
		sql_create_func(sa, "concat", "calc", "+", *t, *t, *t, DIGITS_ADD);
		sql_create_func(sa, "ascii", "str", "ascii", *t, NULL, INT, SCALE_NONE);
		sql_create_func(sa, "code", "str", "unicode", INT, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "length", "str", "stringlength", *t, NULL, INT, SCALE_NONE);
		sql_create_func(sa, "right", "str", "stringright", *t, INT, *t, SCALE_NONE);
		sql_create_func(sa, "left", "str", "stringleft", *t, INT, *t, SCALE_NONE);
		sql_create_func(sa, "upper", "str", "toUpper", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "ucase", "str", "toUpper", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "lower", "str", "toLower", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "lcase", "str", "toLower", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "trim", "str", "trim", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "ltrim", "str", "ltrim", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "rtrim", "str", "rtrim", *t, NULL, *t, SCALE_NONE);

		sql_create_func4(sa, "insert", "str", "insert", *t, INT, INT, *t, *t, SCALE_NONE);
		sql_create_func3(sa, "replace", "str", "replace", *t, *t, *t, *t, SCALE_NONE);
		sql_create_func(sa, "repeat", "str", "repeat", *t, INT, *t, SCALE_NONE);
		sql_create_func(sa, "space", "str", "space", INT, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "char_length", "str", "length", *t, NULL, INT, SCALE_NONE);
		sql_create_func(sa, "character_length", "str", "length", *t, NULL, INT, SCALE_NONE);
		sql_create_func(sa, "octet_length", "str", "nbytes", *t, NULL, INT, SCALE_NONE);

		sql_create_func(sa, "soundex", "txtsim", "soundex", *t, NULL, *t, SCALE_NONE);
		sql_create_func(sa, "difference", "txtsim", "stringdiff", *t, *t, INT, SCALE_NONE);
		sql_create_func(sa, "editdistance", "txtsim", "editdistance", *t, *t, INT, SCALE_FIX);
		sql_create_func(sa, "editdistance2", "txtsim", "editdistance2", *t, *t, INT, SCALE_FIX);

		sql_create_func(sa, "similarity", "txtsim", "similarity", *t, *t, DBL, SCALE_FIX);
		sql_create_func(sa, "qgramnormalize", "txtsim", "qgramnormalize", *t, NULL, *t, SCALE_NONE);

		sql_create_func(sa, "levenshtein", "txtsim", "levenshtein", *t, *t, INT, SCALE_FIX);
		{ sql_subtype sres;
		sql_init_subtype(&sres, INT, 0, 0);
		sql_create_func_(sa, "levenshtein", "txtsim", "levenshtein",
				 list_append(list_append (list_append (list_append(list_append(sa_list(sa), 
					create_arg(sa, NULL, sql_create_subtype(sa, *t, 0, 0))), 
					create_arg(sa, NULL, sql_create_subtype(sa, *t, 0, 0))), 
					create_arg(sa, NULL, sql_create_subtype(sa, INT, 0, 0))), 
					create_arg(sa, NULL, sql_create_subtype(sa, INT, 0, 0))), 
					create_arg(sa, NULL, sql_create_subtype(sa, INT, 0, 0))), 
					&sres, FALSE, FALSE, SCALE_FIX);
		}
	}
	{ sql_subtype sres;
	sql_init_subtype(&sres, TABLE, 0, 0);
	/* copyfrom fname (arg 6) */
	sql_create_func_(sa, "copyfrom", "sql", "copy_from",
	 	list_append( list_append( list_append( list_append(list_append (list_append (list_append(list_append(sa_list(sa), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, LNG, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, LNG, 0, 0))), &sres, FALSE, FALSE, SCALE_FIX);

	/* copyfrom stdin */
	sql_create_func_(sa, "copyfrom", "sql", "copyfrom",
	 	list_append( list_append( list_append(list_append (list_append (list_append(list_append(sa_list(sa), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, LNG, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, LNG, 0, 0))), &sres, FALSE, FALSE, SCALE_FIX);

	/* bincopyfrom */
	sql_create_func_(sa, "copyfrom", "sql", "importTable",
	 	list_append(list_append(sa_list(sa), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), 
			create_arg(sa, NULL, sql_create_subtype(sa, STR, 0, 0))), &sres, FALSE, FALSE, SCALE_FIX);
	}
}

void
types_init(sql_allocator *sa, int debug)
{
	(void)debug;
	aliases = sa_list(sa);
	types = sa_list(sa);
	localtypes = sa_list(sa);
	aggrs = sa_list(sa);
	funcs = sa_list(sa);
	funcs->ht = hash_new(sa, 1024, (fkeyvalue)&base_key);
	sqltypeinit( sa );
}

