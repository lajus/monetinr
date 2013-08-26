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
#include "rel_psm.h"
#include "rel_semantic.h"
#include "rel_schema.h"
#include "rel_select.h"
#include "rel_exp.h"
#include "rel_updates.h"
#include "sql_privileges.h"

static sql_rel *
rel_psm_block(sql_allocator *sa, list *l)
{
	if (l) {
		sql_rel *r = rel_create(sa);

		r->op = op_ddl;
		r->flag = DDL_PSM;
		r->exps = l;
		return r;
	}
	return NULL;
}

static sql_rel *
rel_psm_stmt(sql_allocator *sa, sql_exp *e)
{
	if (e) {
		list *l = sa_list(sa);

		list_append(l, e);
		return rel_psm_block(sa, l);
	}
	return NULL;
}

/* SET variable = value */
static sql_exp *
psm_set_exp(mvc *sql, dnode *n)
{
	exp_kind ek = {type_value, card_value, FALSE};
	char *name = n->data.sval;
	symbol *val = n->next->data.sym;
	sql_exp *e = NULL;
	int level = 0, is_last = 0;
	sql_subtype *tpe = NULL;
	sql_rel *rel = NULL;
	sql_exp *res = NULL;

	/* name can be 
		'parameter of the function' (ie in the param list)
		or a local or global variable, declared earlier
	*/

	/* check if variable is known from the stack */
	if (!stack_find_var(sql, name)) {
		sql_arg *a = sql_bind_param(sql, name);

		if (!a) /* not parameter, ie local var ? */
			return sql_error(sql, 01, "Variable %s unknown", name);
		tpe = &a->type;
	} else { 
		tpe = stack_find_type(sql, name);
	}

	e = rel_value_exp2(sql, &rel, val, sql_sel, ek, &is_last);
	if (!e || (rel && e->card > CARD_AGGR))
		return NULL;

	level = stack_find_frame(sql, name);
	e = rel_check_type(sql, tpe, e, type_cast); 
	if (!e)
		return NULL;
	if (rel) {
		sql_exp *er = exp_rel(sql->sa, rel);
		list *b = sa_list(sql->sa);

		append(b, er);
		append(b, exp_set(sql->sa, name, e, level));
		res = exp_rel(sql->sa, rel_psm_block(sql->sa, b));
	} else {
		res = exp_set(sql->sa, name, e, level);
	}
	return res;
}

static sql_exp*
rel_psm_call(mvc * sql, symbol *se)
{
	sql_subtype *t;
	sql_exp *res = NULL;
	exp_kind ek = {type_value, card_none, FALSE};
	sql_rel *rel = NULL;

	res = rel_value_exp(sql, &rel, se, sql_sel, ek);
	if (!res || rel || ((t=exp_subtype(res)) && t->type))  /* only procedures */
		return sql_error(sql, 01, "function calls are ignored");
	return res;
}

static list *
rel_psm_declare(mvc *sql, dnode *n)
{
	list *l = sa_list(sql->sa);

	while(n) { /* list of 'identfiers with type' */
		dnode *ids = n->data.sym->data.lval->h->data.lval->h;
		sql_subtype *ctype = &n->data.sym->data.lval->h->next->data.typeval;
		while(ids) {
			char *name = ids->data.sval;
			sql_exp *r = NULL;

			/* check if we overwrite a scope local variable declare x; declare x; */
			if (frame_find_var(sql, name)) {
				return sql_error(sql, 01, 
					"Variable '%s' already declared", name);
			}
			/* variables are put on stack, 
 			 * TODO make sure on plan/explain etc they only 
 			 * exist during plan phase */
			stack_push_var(sql, name, ctype);
			r = exp_var(sql->sa, sa_strdup(sql->sa, name), ctype, sql->frame);
			append(l, r);
			ids = ids->next;
		}
		n = n->next;
	}
	return l;
}

static sql_exp *
rel_psm_declare_table(mvc *sql, dnode *n)
{
	sql_rel *rel = NULL;
	dlist *qname = n->next->data.lval;
	char *name = qname_table(qname);
	char *sname = qname_schema(qname);
	sql_subtype ctype = *sql_bind_localtype("bat");

	if (sname)  /* not allowed here */
		return sql_error(sql, 02, "DECLARE TABLE: qualified name not allowed");
	if (frame_find_var(sql, name)) 
		return sql_error(sql, 01, "Variable '%s' already declared", name);
	
	assert(n->next->next->next->type == type_int);
	
	rel = rel_create_table(sql, cur_schema(sql), SQL_DECLARED_TABLE, NULL, name, n->next->next->data.sym, n->next->next->next->data.i_val, NULL);

	if (!rel || rel->op != op_ddl || rel->flag != DDL_CREATE_TABLE)
		return NULL;

	ctype.comp_type = (sql_table*)((atom*)((sql_exp*)rel->exps->t->data)->l)->data.val.pval;
	stack_push_rel_var(sql, name, rel_dup(rel), &ctype);
	return exp_var(sql->sa, sa_strdup(sql->sa, name), &ctype, sql->frame);
}

/* [ label: ]
   while (cond) do 
	statement_list
   end [ label ]
   currently we only parse the labels, they cannot be used as there is no

   support for LEAVE and ITERATE (sql multi-level break and continue)
 */
static sql_exp * 
rel_psm_while_do( mvc *sql, sql_subtype *res, dnode *w, int is_func )
{
	if (!w)
		return NULL;
	if (w->type == type_symbol) { 
		sql_exp *cond;
		list *whilestmts;
		dnode *n = w;
		sql_rel *rel = NULL;

		cond = rel_logical_value_exp(sql, &rel, n->data.sym, sql_sel); 
		n = n->next;
		whilestmts = sequential_block(sql, res, n->data.lval, n->next->data.sval, is_func);

		if (sql->session->status || !cond || !whilestmts || rel) 
			return NULL;
		return exp_while( sql->sa, cond, whilestmts );
	}
	return NULL;
}


/* if (cond) then statement_list
   [ elseif (cond) then statement_list ]*
   [ else statement_list ]
   end if
 */
static list * 
psm_if_then_else( mvc *sql, sql_subtype *res, dnode *elseif, int is_func)
{
	if (!elseif)
		return NULL;
	if (elseif->next && elseif->type == type_symbol) { /* if or elseif */
		sql_exp *cond;
		list *ifstmts, *elsestmts;
		dnode *n = elseif;
		sql_rel *rel = NULL;

		cond = rel_logical_value_exp(sql, &rel, n->data.sym, sql_sel); 
		n = n->next;
		ifstmts = sequential_block(sql, res, n->data.lval, NULL, is_func);
		n = n->next;
		elsestmts = psm_if_then_else( sql, res, n, is_func);

		if (sql->session->status || !cond || !ifstmts || rel) {
			if (rel)
				return sql_error(sql, 02, "IF THEN: No SELECT statements allowed within the IF condition");
			return NULL;
		}
		return append(sa_list(sql->sa), exp_if( sql->sa, cond, ifstmts, elsestmts));
	} else { /* else */
		symbol *e = elseif->data.sym;

		if (e==NULL || (e->token != SQL_ELSE))
			return NULL;
		return sequential_block( sql, res, e->data.lval, NULL, is_func);
	}
}

static sql_exp * 
rel_psm_if_then_else( mvc *sql, sql_subtype *res, dnode *elseif, int is_func)
{
	if (!elseif)
		return NULL;
	if (elseif->next && elseif->type == type_symbol) { /* if or elseif */
		sql_exp *cond;
		list *ifstmts, *elsestmts;
		dnode *n = elseif;
		sql_rel *rel = NULL;

		cond = rel_logical_value_exp(sql, &rel, n->data.sym, sql_sel); 
		n = n->next;
		ifstmts = sequential_block(sql, res, n->data.lval, NULL, is_func);
		n = n->next;
		elsestmts = psm_if_then_else( sql, res, n, is_func);
		if (sql->session->status || !cond || !ifstmts || rel) {
			if (rel)
				return sql_error(sql, 02, "IF THEN ELSE: No SELECT statements allowed within the IF condition");
			return NULL;
		}
		return exp_if( sql->sa, cond, ifstmts, elsestmts);
	}
	return NULL;
}

/* 	1
	CASE
	WHEN search_condition THEN statements
	[ WHEN search_condition THEN statements ]
	[ ELSE statements ]
	END CASE

	2
	CASE case_value
	WHEN when_value THEN statements
	[ WHEN when_value THEN statements ]
	[ ELSE statements ]
	END CASE
 */
static list * 
rel_psm_case( mvc *sql, sql_subtype *res, dnode *case_when, int is_func )
{
	list *case_stmts = sa_list(sql->sa);

	if (!case_when)
		return NULL;

	/* case 1 */
	if (case_when->type == type_symbol) {
		dnode *n = case_when;
		symbol *case_value = n->data.sym;
		dlist *when_statements = n->next->data.lval;
		dlist *else_statements = n->next->next->data.lval;
		list *else_stmt = NULL;
		sql_rel *rel = NULL;
		exp_kind ek = {type_value, card_value, FALSE};
		sql_exp *v = rel_value_exp(sql, &rel, case_value, sql_sel, ek);

		if (!v)
			return NULL;
		if (rel)
			return sql_error(sql, 02, "CASE: No SELECT statements allowed within the CASE condition");
		if (else_statements) {
			else_stmt = sequential_block( sql, res, else_statements, NULL, is_func);
			if (!else_stmt) 
				return NULL;
		}
		n = when_statements->h;
		while(n) {
			dnode *m = n->data.sym->data.lval->h;
			sql_exp *cond=0, *when_value = rel_value_exp(sql, &rel, m->data.sym, sql_sel, ek);
			list *if_stmts = NULL;
			sql_exp *case_stmt = NULL;

			if (!when_value || rel ||
			   (cond = rel_binop_(sql, v, when_value, NULL, "=", card_value)) == NULL || 
			   (if_stmts = sequential_block( sql, res, m->next->data.lval, NULL, is_func)) == NULL ) {
				if (rel)
					return sql_error(sql, 02, "CASE: No SELECT statements allowed within the CASE condition");
				return NULL;
			}
			case_stmt = exp_if(sql->sa, cond, if_stmts, NULL);
			list_append(case_stmts, case_stmt);
			n = n->next;
		}
		if (else_stmt)
			list_merge(case_stmts, else_stmt, NULL);
		return case_stmts;
	} else { 
		/* case 2 */
		dnode *n = case_when;
		dlist *whenlist = n->data.lval;
		dlist *else_statements = n->next->data.lval;
		list *else_stmt = NULL;

		if (else_statements) {
			else_stmt = sequential_block( sql, res, else_statements, NULL, is_func);
			if (!else_stmt) 
				return NULL;
		}
		n = whenlist->h;
		while(n) {
			dnode *m = n->data.sym->data.lval->h;
			sql_rel *rel = NULL;
			sql_exp *cond = rel_logical_value_exp(sql, &rel, m->data.sym, sql_sel);
			list *if_stmts = NULL;
			sql_exp *case_stmt = NULL;

			if (!cond || rel ||
			   (if_stmts = sequential_block( sql, res, m->next->data.lval, NULL, is_func)) == NULL ) {
				if (rel)
					return sql_error(sql, 02, "CASE: No SELECT statements allowed within the CASE condition");
				return NULL;
			}
			case_stmt = exp_if(sql->sa, cond, if_stmts, NULL);
			list_append(case_stmts, case_stmt);
			n = n->next;
		}
		if (else_stmt)
			list_merge(case_stmts, else_stmt, NULL);
		return case_stmts;
	}
}

/* return val;
 */
static list * 
rel_psm_return( mvc *sql, sql_subtype *restype, symbol *return_sym )
{
	exp_kind ek = {type_value, card_value, FALSE};
	sql_exp *res;
	sql_rel *rel = NULL;
	int is_last = 0;
	list *l = sa_list(sql->sa);

	if (restype->comp_type)
		ek.card = card_relation;
	res = rel_value_exp2(sql, &rel, return_sym, sql_sel, ek, &is_last);
	if (!res)
		return NULL;
	if (ek.card != card_relation && (!res || 
           	(res = rel_check_type(sql, restype, res, type_equal)) == NULL))
		return NULL;
	else if (ek.card == card_relation && (!rel && !res->tpe.comp_type))
		return NULL;
	
	if (rel && ek.card != card_relation)
		append(l, exp_rel(sql->sa, rel));
	else if (rel) {
		list *exps = sa_list(sql->sa);
		node *n, *m;
		int isproject = is_project(rel->op);
		list *oexps = rel->exps;
		sql_rel *l = rel->l;

		if (is_topn(rel->op))
			oexps = l->exps;
		for (n = oexps->h, m = restype->comp_type->columns.set->h; n && m; n = n->next, m = m->next) {
			sql_exp *e = n->data;
			sql_column *ce = m->data;
			char *cname = exp_name(e);
			char name[16];

			if (!cname)
				cname = number2name(name, 16, ++sql->label);
			if (!isproject) 
				e = exp_column(sql->sa, exp_relname(e), cname, exp_subtype(e), exp_card(e), has_nil(e), is_intern(e));
			e = rel_check_type(sql, &ce->type, e, type_equal);
			if (!e)
				return NULL;
			append(exps, e);
		}
		if (isproject)
			rel -> exps = exps;
		else
			rel = rel_project(sql->sa, rel, exps);
		res = exp_rel(sql->sa, rel);
	} else if (!rel && res->tpe.comp_type){ /* handle return table-var */
		sql_rel *rel = stack_find_rel_var(sql, res->r);
		list *exps = sa_list(sql->sa);
		sql_table *t = rel_ddl_table_get(rel);
		node *n, *m;
		char *tname = t->base.name;

		if (cs_size(&t->columns) != cs_size(&restype->comp_type->columns))
			return sql_error(sql, 02, "RETURN: number of columns do not match");
		for (n = t->columns.set->h, m = restype->comp_type->columns.set->h; n && m; n = n->next, m = m->next) {
			sql_column *c = n->data;
			sql_column *ce = m->data;
			sql_exp *e = exp_alias(sql->sa, tname, c->base.name, tname, c->base.name, &c->type, CARD_MULTI, c->null, 0);

			e = rel_check_type(sql, &ce->type, e, type_equal);
			if (!e)
				return NULL;
			append(exps, e);
		}
		rel = rel_project(sql->sa, rel, exps);
		res = exp_rel(sql->sa, rel);
	}
	append(l, exp_return(sql->sa, res, stack_nr_of_declared_tables(sql)));
	return l;
}

static list *
rel_select_into( mvc *sql, symbol *sq, exp_kind ek)
{
        SelectNode *sn = (SelectNode*)sq;
        dlist *into = sn->into;
	node *m;
	dnode *n;
	sql_rel *r;
	list *nl = NULL;

        /* SELECT ... INTO var_list */
        sn->into = NULL;
	r = rel_subquery(sql, NULL, sq, ek);
	if (!r) 
		return NULL;
	nl = sa_list(sql->sa);
	append(nl, exp_rel(sql->sa, r));
	for (m = r->exps->h, n = into->h; m && n; m = m->next, n = n->next) {
		sql_subtype *tpe = NULL;
		char *nme = n->data.sval;
		sql_exp *v = m->data;
		int level;

		if (!stack_find_var(sql, nme)) 
			return sql_error(sql, 02, "SELECT INTO: variable '%s' unknown", nme);
		/* dynamic check for single values */
		if (v->card > CARD_AGGR) {
			sql_subaggr *zero_or_one = sql_bind_aggr(sql->sa, sql->session->schema, "zero_or_one", exp_subtype(v));
			assert(zero_or_one);
			v = exp_aggr1(sql->sa, v, zero_or_one, 0, 0, CARD_ATOM, 0);
		}
		tpe = stack_find_type(sql, nme);
		level = stack_find_frame(sql, nme);
		if (!v || !(v = rel_check_type(sql, tpe, v, type_equal))) 
			return NULL;
		v = exp_set(sql->sa, nme, v, level);
		list_append(nl, v);
	}
	return nl;
}

static int has_return( list *l );

static int
exp_has_return(sql_exp *e) 
{
	if (e->type == e_psm) {
		if (e->flag & PSM_RETURN) 
			return 1;
		if (e->flag & PSM_IF) 
			return has_return(e->r) && (!e->f || has_return(e->f));
	}
	return 0;
}

static int
has_return( list *l )
{
	node *n = l->t;
	sql_exp *e = n->data;

	/* last statment of sequential block */
	if (exp_has_return(e)) 
		return 1;
	return 0;
}

list *
sequential_block (mvc *sql, sql_subtype *restype, dlist *blk, char *opt_label, int is_func) 
{
	list *l=0;
	dnode *n;

 	if (THRhighwater())
		return sql_error(sql, 10, "SELECT: too many nested operators");

	if (blk->h)
 		l = sa_list(sql->sa);
	stack_push_frame(sql, opt_label);
	for (n = blk->h; n; n = n->next ) {
		sql_exp *res = NULL;
		list *reslist = NULL;
		symbol *s = n->data.sym;

		switch (s->token) {
		case SQL_SET:
			res = psm_set_exp(sql, s->data.lval->h);
			break;
		case SQL_DECLARE:
			reslist = rel_psm_declare(sql, s->data.lval->h);
			break;
		case SQL_CREATE_TABLE: 
			res = rel_psm_declare_table(sql, s->data.lval->h);
			break;
		case SQL_WHILE:
			res = rel_psm_while_do(sql, restype, s->data.lval->h, is_func);
			break;
		case SQL_IF:
			res = rel_psm_if_then_else(sql, restype, s->data.lval->h, is_func);
			break;
		case SQL_CASE:
			reslist = rel_psm_case(sql, restype, s->data.lval->h, is_func);
			break;
		case SQL_CALL:
			res = rel_psm_call(sql, s->data.sym);
			break;
		case SQL_RETURN:
			/*If it is not a function it cannot have a return statement*/
			if (!is_func)
				res = sql_error(sql, 01, 
					"Return statement in the procedure body");
			else {
				/* should be last statement of a sequential_block */
				if (n->next) { 
					res = sql_error(sql, 01, 
						"Statement after return");
				} else {
					reslist = rel_psm_return(sql, restype, s->data.sym);
				}
			}
			break;
		case SQL_SELECT: { /* row selections (into variables) */
			exp_kind ek = {type_value, card_row, TRUE};
			reslist = rel_select_into(sql, s, ek);
		}	break;
		case SQL_COPYFROM:
		case SQL_BINCOPYFROM:
		case SQL_INSERT:
		case SQL_UPDATE:
		case SQL_DELETE: {
			sql_rel *r = rel_updates(sql, s);
			if (!r)
				return NULL;
			res = exp_rel(sql->sa, r);
		}	break;
		default:
			res = sql_error(sql, 01, 
			 "Statement '%s' is not a valid flow control statement",
			 token2string(s->token));
		}
		if (!res && !reslist) {
			l = NULL;
			break;
		}
		if (res)
			list_append(l, res);
		else
			list_merge(l, reslist, NULL);
	}
	stack_pop_frame(sql);
	return l;
}

static sql_subtype *
result_type(mvc *sql, sql_subfunc *f, char *fname, symbol *res) 
{
	if (res->token == SQL_TYPE) {
		return &res->data.lval->h->data.typeval;
	} else if (res->token == SQL_TABLE) {
		/* here we create a new table-type */
		sql_schema *sys = find_sql_schema(sql->session->tr, "sys");
		sql_subtype *t = SA_NEW(sql->sa, sql_subtype);
		sql_table *tbl;
		char *tnme = NEW_ARRAY(char, strlen(fname) + 2);

		tnme[0] = '#';
		strcpy(tnme+1, fname);
		if (f && f->res.digits) {
			tbl = find_sql_table_id(sys, f->res.digits);
			_DELETE(tnme);
			if (!tbl)
				return NULL;
		} else {
			dnode *n = res->data.lval->h;

			tbl = mvc_create_generated(sql, sys, tnme, NULL, 1 /* system ?*/);
			for(;n; n = n->next->next) {
				sql_subtype *ct = &n->next->data.typeval;
		    		mvc_create_column(sql, tbl, n->data.sval, ct);
			}
		}
		_DELETE(tnme);

		sql_find_subtype(t, "table", 0, 0);
		t->comp_type = tbl;
		t->digits = tbl->base.id; /* pass the table through digits */
		return t;
	}
	return NULL;
}

static list *
create_type_list(mvc *sql, dlist *params, int param)
{
	sql_subtype *par_subtype;
	list * type_list = sa_list(sql->sa);
	dnode * n = NULL;
	
	if (params) {
		for (n = params->h; n; n = n->next) {
			dnode *an = n;
	
			if (param) {
				an = n->data.lval->h;
				par_subtype = &an->next->data.typeval;
				list_append(type_list, par_subtype);
			} else { 
				par_subtype = &an->data.typeval;
				list_prepend(type_list, par_subtype);
			}
		}
	}
	return type_list;
}

static sql_rel*
rel_create_function(sql_allocator *sa, char *sname, sql_func *f)
{
	sql_rel *rel = rel_create(sa);
	list *exps = new_exp_list(sa);

	append(exps, exp_atom_clob(sa, sname));
	append(exps, exp_atom_ptr(sa, f));
	rel->l = NULL;
	rel->r = NULL;
	rel->op = op_ddl;
	rel->flag = DDL_CREATE_FUNCTION;
	rel->exps = exps;
	rel->card = 0;
	rel->nrcols = 0;
	return rel;
}

static sql_rel *
rel_create_func(mvc *sql, dlist *qname, dlist *params, symbol *res, dlist *ext_name, dlist *body, int type)
{
	char *fname = qname_table(qname);
	char *sname = qname_schema(qname);
	sql_schema *s = NULL;
	sql_func *f = NULL;
	sql_subfunc *sf;
	dnode *n;
	list *type_list = NULL;
	sql_subtype *restype = NULL;
	int instantiate = (sql->emode == m_instantiate);
	int deps = (sql->emode == m_deps);
	int create = (!instantiate && !deps);

	char is_aggr = (type == F_AGGR);
	char is_func = (type != F_PROC);
	char *F = is_aggr?"AGGREGATE":(is_func?"FUNCTION":"PROCEDURE");
	char *KF = type==F_FILT?"FILTER ": type==F_UNION?"UNION ": "";

	if (STORE_READONLY && create) 
		return sql_error(sql, 06, "schema statements cannot be executed on a readonly database.");
			
	if (sname && !(s = mvc_bind_schema(sql, sname)))
		return sql_error(sql, 02, "3F000!CREATE %s%s: no such schema '%s'", KF, F, sname);
	if (s == NULL)
		s = cur_schema(sql);

	type_list = create_type_list(sql, params, 1);
	if ((sf = sql_bind_func_(sql->sa, s, fname, type_list, type)) != NULL && create) {
		if (params) {
			char *arg_list = NULL;
			node *n;
			
			for (n = type_list->h; n; n = n->next) {
				char *tpe =  subtype2string((sql_subtype *) n->data);
				
				if (arg_list) {
					arg_list = sql_message("%s, %s", arg_list, tpe);
					_DELETE(tpe);	
				} else {
					arg_list = tpe;
				}
			}
			(void)sql_error(sql, 02, "CREATE %s%s: name '%s' (%s) already in use", KF, F, fname, arg_list);
			_DELETE(arg_list);
			return NULL;
		} else {
			return sql_error(sql, 02, "CREATE %s%s: name '%s' already in use", KF, F, fname);
		}
	} else {
		if (create && !schema_privs(sql->role_id, s)) {
			return sql_error(sql, 02, "CREATE %s%s: insufficient privileges "
					"for user '%s' in schema '%s'", KF, F,
					stack_get_string(sql, "current_user"), s->base.name);
		} else {
			char *q = QUERY(sql->scanner);
			list *l = NULL;

		 	if (params) {
				for (n = params->h; n; n = n->next) {
					dnode *an = n->data.lval->h;
		
					sql_add_param(sql, an->data.sval, &an->next->data.typeval);
				}
				l = sql->params;
			}
			if (!l)
				l = sa_list(sql->sa);
			if (res) {
				restype = result_type(sql, sf, fname, res);
				if (!restype)
					return sql_error(sql, 01,
							"CREATE %s%s: failed to get restype", KF, F);
			}

		 	if (body) {		/* sql func */
				list *b = NULL;
				sql_schema *old_schema = cur_schema(sql);
	
				if (s)
					sql->session->schema = s;
				b = sequential_block(sql, restype, body, NULL, is_func);
				sql->session->schema = old_schema;
				sql->params = NULL;
				if (!b) 
					return NULL;
			
				/* check if we have a return statement */
				if (is_func && restype && !has_return(b)) {
					return sql_error(sql, 01,
							"CREATE %s%s: missing return statement", KF, F);
				}
				if (!is_func && !restype && has_return(b)) {
					return sql_error(sql, 01, "CREATE %s%s: procedures "
							"cannot have return statements", KF, F);
				}
	
				/* in execute mode we instantiate the function */
				if (instantiate || deps) {
					return rel_psm_block(sql->sa, b);
				} else if (create) {
					f = mvc_create_func(sql, sql->sa, s, fname, l, restype, type, "user", q, q);
				}
			} else {
				char *fmod = qname_module(ext_name);
				char *fnme = qname_fname(ext_name);

				sql->params = NULL;
				if (create) {
					f = mvc_create_func(sql, sql->sa, s, fname, l, restype, type, fmod, fnme, q);
				} else if (!sf) {
					return sql_error(sql, 01, "CREATE %s%s: external name %s.%s not bound (%s,%s)", KF, F, fmod, fnme, s->base.name, fname );
				} else {
					sql_func *f = sf->func;
					f->mod = _STRDUP(fmod);
					f->imp = _STRDUP(fnme);
					if (res && restype)
						f->res = *restype;
					f->sql = 0; /* native */
				}
			}
		}
	}
	return rel_create_function(sql->sa, s->base.name, f);
}

static sql_rel*
rel_drop_function(sql_allocator *sa, char *sname, char *name, int nr, int type, int action)
{
	sql_rel *rel = rel_create(sa);
	list *exps = new_exp_list(sa);

	append(exps, exp_atom_clob(sa, sname));
	append(exps, exp_atom_clob(sa, name));
	append(exps, exp_atom_int(sa, nr));
	append(exps, exp_atom_int(sa, type));
	append(exps, exp_atom_int(sa, action));
	rel->l = NULL;
	rel->r = NULL;
	rel->op = op_ddl;
	rel->flag = DDL_DROP_FUNCTION;
	rel->exps = exps;
	rel->card = 0;
	rel->nrcols = 0;
	return rel;
}

static sql_rel* 
rel_drop_func(mvc *sql, dlist *qname, dlist *typelist, int drop_action, int type)
{
	char *name = qname_table(qname);
	char *sname = qname_schema(qname);
	sql_schema *s = NULL;
	list * list_func = NULL, *type_list = NULL; 
	sql_subfunc *sub_func = NULL;
	sql_func *func = NULL;

	char is_aggr = (type == F_AGGR);
	char is_func = (type != F_PROC);
	char *F = is_aggr?"AGGREGATE":(is_func?"FUNCTION":"PROCEDURE");
	char *f = is_aggr?"aggregate":(is_func?"function":"procedure");
	char *KF = type==F_FILT?"FILTER ": type==F_UNION?"UNION ": "";
	char *kf = type==F_FILT?"filter ": type==F_UNION?"union ": "";

	if (sname && !(s = mvc_bind_schema(sql, sname)))
		return sql_error(sql, 02, "3F000!DROP %s%s: no such schema '%s'", KF, F, sname);

	if (s == NULL) 
		s =  cur_schema(sql);
	
	if (typelist) {	
		type_list = create_type_list(sql, typelist, 0);
		sub_func = sql_bind_func_(sql->sa, s, name, type_list, type);
		if (!sub_func && !sname) {
			s = tmp_schema(sql);
			sub_func = sql_bind_func_(sql->sa, s, name, type_list, type);
		}
		if ( sub_func && sub_func->func->type == type)
			func = sub_func->func;
	} else {
		list_func = schema_bind_func(sql,s,name, type);
		if (list_func && list_func->cnt > 1) {
			list_destroy(list_func);
			return sql_error(sql, 02, "DROP %s%s: there are more than one %s%s called '%s', please use the full signature", KF, F, kf, f,name);
		}
		if (list_func && list_func->cnt == 1)
			func = (sql_func*) list_func->h->data;
	}
	
	if (!func) { 
		if (typelist) {
			char *arg_list = NULL;
			node *n;
			
			if (type_list->cnt > 0) {
				for (n = type_list->h; n; n = n->next) {
					char *tpe =  subtype2string((sql_subtype *) n->data);
				
					if (arg_list) {
						arg_list = sql_message("%s, %s", arg_list, tpe);
						_DELETE(tpe);	
					} else {
						arg_list = tpe;
					}
				}
				list_destroy(list_func);
				return sql_error(sql, 02, "DROP %s%s: no such %s%s '%s' (%s)", KF, F, kf, f, name, arg_list);
			}
			list_destroy(list_func);
			return sql_error(sql, 02, "DROP %s%s: no such %s%s '%s' ()", KF, F, kf, f, name);

		} else {
			return sql_error(sql, 02, "DROP %s%s: no such %s%s '%s'", KF, F, kf, f, name);
		}
	} else if (((is_func && type != F_FILT) && !func->res.type) || 
		   (!is_func && func->res.type)) {
		if (list_func)
			list_destroy(list_func);
		return sql_error(sql, 02, "DROP %s%s: cannot drop %s '%s'", KF, F, is_func?"procedure":"function", name);
	}

	if (list_func)
		list_destroy(list_func);
	return rel_drop_function(sql->sa, s->base.name, name, func->base.id, type, drop_action);
}

static sql_rel* 
rel_drop_all_func(mvc *sql, dlist *qname, int drop_action, int type)
{
	char *name = qname_table(qname);
	char *sname = qname_schema(qname);
	sql_schema *s = NULL;
	list * list_func = NULL; 

	char is_aggr = (type == F_AGGR);
	char is_func = (type != F_PROC);
	char *F = is_aggr?"AGGREGATE":(is_func?"FUNCTION":"PROCEDURE");
	char *f = is_aggr?"aggregate":(is_func?"function":"procedure");
	char *KF = type==F_FILT?"FILTER ": type==F_UNION?"UNION ": "";
	char *kf = type==F_FILT?"filter ": type==F_UNION?"union ": "";

	if (sname && !(s = mvc_bind_schema(sql, sname)))
		return sql_error(sql, 02, "3F000!DROP %s%s: no such schema '%s'", KF, F, sname);

	if (s == NULL) 
		s =  cur_schema(sql);
	
	list_func = schema_bind_func(sql, s, name, type);
	if (!list_func) 
		return sql_error(sql, 02, "DROP ALL %s%s: no such %s%s '%s'", KF, F, kf, f, name);
	list_destroy(list_func);
	return rel_drop_function(sql->sa, s->base.name, name, -1, type, drop_action);
}

static sql_rel *
rel_create_trigger(mvc *sql, char *sname, char *tname, char *triggername, int time, int orientation, int event, char *old_name, char *new_name, char *condition, char *query)
{
	sql_rel *rel = rel_create(sql->sa);
	list *exps = new_exp_list(sql->sa);

	append(exps, exp_atom_str(sql->sa, sname, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, tname, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, triggername, sql_bind_localtype("str") ));
	append(exps, exp_atom_int(sql->sa, time));
	append(exps, exp_atom_int(sql->sa, orientation));
	append(exps, exp_atom_int(sql->sa, event));
	append(exps, exp_atom_str(sql->sa, old_name, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, new_name, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, condition, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, query, sql_bind_localtype("str") ));
	rel->l = NULL;
	rel->r = NULL;
	rel->op = op_ddl;
	rel->flag = DDL_CREATE_TRIGGER;
	rel->exps = exps;
	rel->card = CARD_MULTI;
	rel->nrcols = 0;
	return rel;
}

static void
stack_push_table(mvc *sql, char *tname, sql_table *t)
{
	sql_rel *r = rel_basetable(sql, t, tname );
		
	stack_push_rel_view(sql, tname, r);
}

static sql_rel *
create_trigger(mvc *sql, dlist *qname, int time, symbol *trigger_event, char *table_name, dlist *opt_ref, dlist *triggered_action)
{
	char *tname = qname_table(qname);
	sql_schema *ss = cur_schema(sql);
	sql_table *t = NULL;
	int instantiate = (sql->emode == m_instantiate);
	int create = (!instantiate && sql->emode != m_deps);
	list *sq = NULL;
	sql_rel *r = NULL;

	dlist *columns = trigger_event->data.lval;
	char *old_name = NULL, *new_name = NULL; 
	dlist *stmts = triggered_action->h->next->next->data.lval;
	
	if (opt_ref) {
		dnode *dl = opt_ref->h;
		for ( ; dl; dl = dl->next) {
			/* list (new(1)/old(0)), char */
			char *n = dl->data.lval->h->next->data.sval;

			assert(dl->data.lval->h->type == type_int);
			if (!dl->data.lval->h->data.i_val) /*?l_val?*/
				old_name = n;
			else
				new_name = n;
		}
	}
	if (create && !schema_privs(sql->role_id, ss)) 
		return sql_error(sql, 02, "CREATE TRIGGER: access denied for %s to schema ;'%s'", stack_get_string(sql, "current_user"), ss->base.name);
	if (create && mvc_bind_trigger(sql, ss, tname) != NULL) 
		return sql_error(sql, 02, "CREATE TRIGGER: name '%s' already in use", tname);
	
	if (create && !(t = mvc_bind_table(sql, ss, table_name)))
		return sql_error(sql, 02, "CREATE TRIGGER: unknown table '%s'", table_name);
	if (create && isView(t)) 
		return sql_error(sql, 02, "CREATE TRIGGER: cannot create trigger on view '%s'", table_name);
	
	if (create) {
		int event = (trigger_event->token == SQL_INSERT)?0:
			    (trigger_event->token == SQL_DELETE)?1:2;
		int orientation = triggered_action->h->data.i_val;
		char *condition = triggered_action->h->next->data.sval;
		char *q = QUERY(sql->scanner);

		assert(triggered_action->h->type == type_int);
		return rel_create_trigger(sql, t->s->base.name, t->base.name, tname, time, orientation, event, old_name, new_name, condition, q);
	}

	t = mvc_bind_table(sql, ss, table_name);
	stack_push_frame(sql, "OLD-NEW");
	/* we need to add the old and new tables */
	if (new_name)
		stack_push_table(sql, new_name, t);
	if (old_name)
		stack_push_table(sql, old_name, t);
	sq = sequential_block(sql, NULL, stmts, NULL, 1);
	r = rel_psm_block(sql->sa, sq);

	/* todo trigger_columns */
	(void)columns;
	return r;
}

static sql_rel *
rel_drop_trigger(mvc *sql, char *sname, char *tname)
{
	sql_rel *rel = rel_create(sql->sa);
	list *exps = new_exp_list(sql->sa);

	append(exps, exp_atom_str(sql->sa, sname, sql_bind_localtype("str") ));
	append(exps, exp_atom_str(sql->sa, tname, sql_bind_localtype("str") ));
	rel->l = NULL;
	rel->r = NULL;
	rel->op = op_ddl;
	rel->flag = DDL_DROP_TRIGGER;
	rel->exps = exps;
	rel->card = CARD_MULTI;
	rel->nrcols = 0;
	return rel;
}

static sql_rel *
drop_trigger(mvc *sql, dlist *qname)
{
	char *tname = qname_table(qname);
	sql_schema *ss = cur_schema(sql);

	if (!schema_privs(sql->role_id, ss)) 
		return sql_error(sql, 02, "DROP TRIGGER: access denied for %s to schema ;'%s'", stack_get_string(sql, "current_user"), ss->base.name);
	return rel_drop_trigger(sql, ss->base.name, tname);
}

sql_rel *
rel_psm(mvc *sql, symbol *s)
{
	sql_rel *ret = NULL;

	switch (s->token) {
	case SQL_CREATE_FUNC:
	{
		dlist *l = s->data.lval;
		int type = l->h->next->next->next->next->next->data.i_val;

		ret = rel_create_func(sql, l->h->data.lval, l->h->next->data.lval, l->h->next->next->data.sym, l->h->next->next->next->data.lval, l->h->next->next->next->next->data.lval, type);
		sql->type = Q_SCHEMA;
	} 	break;
	case SQL_DROP_FUNC:
	{
		dlist *l = s->data.lval;
		int type = l->h->next->next->next->next->data.i_val;

		if (STORE_READONLY) 
			return sql_error(sql, 06, "schema statements cannot be executed on a readonly database.");
			
		assert(l->h->next->type == type_int);
		assert(l->h->next->next->next->type == type_int);
		if (l->h->next->data.i_val) /*?l_val?*/
			ret = rel_drop_all_func(sql, l->h->data.lval, l->h->next->next->next->data.i_val, type);
		else
			ret = rel_drop_func(sql, l->h->data.lval, l->h->next->next->data.lval, l->h->next->next->next->data.i_val, type);

		sql->type = Q_SCHEMA;
	}	break;
	case SQL_SET:
		ret = rel_psm_stmt(sql->sa, psm_set_exp(sql, s->data.lval->h));
		sql->type = Q_SCHEMA;
		break;
	case SQL_DECLARE:
		ret = rel_psm_block(sql->sa, rel_psm_declare(sql, s->data.lval->h));
		sql->type = Q_SCHEMA;
		break;
	case SQL_CALL:
		ret = rel_psm_stmt(sql->sa, rel_psm_call(sql, s->data.sym));
		sql->type = Q_UPDATE;
		break;
	case SQL_CREATE_TRIGGER:
	{
		dlist *l = s->data.lval;

		assert(l->h->next->type == type_int);
		ret = create_trigger(sql, l->h->data.lval, l->h->next->data.i_val, l->h->next->next->data.sym, l->h->next->next->next->data.sval, l->h->next->next->next->next->data.lval, l->h->next->next->next->next->next->data.lval);
		sql->type = Q_SCHEMA;
	}
		break;

	case SQL_DROP_TRIGGER:
	{
		dlist *l = s->data.lval;

		ret = drop_trigger(sql, l);
		sql->type = Q_SCHEMA;
	}
		break;

	default:
		return sql_error(sql, 01, "schema statement unknown symbol(" PTRFMT ")->token = %s", PTRFMTCAST s, token2string(s->token));
	}
	return ret;
}
