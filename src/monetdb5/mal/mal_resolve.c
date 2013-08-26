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
 * @a M. Kersten
 * @v 1.0
 * @+ Type Resolution
 * Given the interpretative nature of many of the MAL instructions,
 * when and where type resolution takes place is a critical design issue.
 * Performing it too late, i.e. at each instruction call, leads to
 * performance problems if we derive the same information over and over again.
 * However, many built-in operators have polymorphic typed signatures,
 * so we cannot escape it altogether.
 *
 * Consider the small illustrative MAL program:
 * @example
 * function sample(nme:str, val:any_1):bit;
 *    c := 2 * 3;
 *    b := bbp.bind(nme);  #find a BAT
 *    h := algebra.select(b,val,val);
 *    t := aggr.count(h);
 *    x := io.print(t);
 *    y := io.print(val);
 * end sample;
 * @end example
 *
 * The function definition is polymorphic typed on the 2nd argument,
 * it becomes a concrete type upon invocation. The system could attempt
 * a type check, but quickly runs into assumptions that generally do not hold.
 * The first assignment can be type checked during parsing
 * and a symbolic optimizer could even evaluate the expression once.
 * Looking up a BAT in the buffer pool leads to
 * an element @sc{:bat[@emph{ht,tt}]} where @emph{ht} and @emph{tt}
 * are runtime dependent types, which means that the selection operation can
 * not be type-checked immediately. It is an example of an embedded
 * polypmorphic statement, which requires intervention of the user/optimizer
 * to make the type explicit before the type resolver becomes active.
 * The operation @sc{count} can be checked, if it is given a BAT argument.
 * This assumes that we can infer that 'h' is indeed a BAT, which requires
 * assurance that @sc{algebra.select} produces one. However, there are
 * no rules to avoid addition of new operators, or to differentiate among
 * different implementations based on the argument types.
 * Since @sc{print(t)} contains an undetermined typed
 * argument we should postpone typechecking as well.
 * The last print statement can be checked upon function invocation.
 *
 * Life becomes really complex if the body contains a loop with
 * variable types. For then we also have to keep track of the original
 * state of the function. Or alternatively, type checking should consider
 * the runtime stack rather than the function definition itself.
 *
 * These examples give little room to achieve our prime objective, i.e.
 * a fast and early type resolution scheme. Any non-polymorphic function
 * can be type checked and marked type-safe upon completion.
 * Type checking polymorphic functions are postponed until a concrete
 * type instance is known. It leads to a clone, which can be type checked
 * and is entered into the symbol table.
 * The type resolution status is marked in each instruction.
 * TYPE_RESOLVED implies that the type of the instruction is fully
 * resolved, it is marked TYPE_DYNAMIC otherwise.
 */
/*
 * @- Function call resolution
 * Search the first definition of the operator in the current module
 * and check the parameter types.
 * For a polymorphic MAL function we make a fully instantiated clone.
 * It will be prepended to the symbol list as it is more restrictive.
 * This effectively overloads the MAL procedure.
 */
#include "monetdb_config.h"
#include "mal_resolve.h"
#include "mal_namespace.h"

static malType getPolyType(malType t, int *polytype);
static int updateTypeMap(int formal, int actual, int polytype[MAXTYPEVAR]);
static int typeKind(MalBlkPtr mb, InstrPtr p, int i);

#define MAXMALARG 256

str traceFcnName = "____";
int tracefcn;
int polyVector[MAXTYPEVAR];
#if 0
void
polyInit(void)
{
	int i;
	for (i = 0; i < MAXTYPEVAR; i++)
		polyVector[i] = TYPE_any;
}
#endif

/*
 * We found the proper function. Copy some properties. In particular,
 * determine the calling strategy, i.e. FCNcall, CMDcall, FACcall, PATcall
 * Beware that polymorphic functions may produce type-incorrect clones.
 * This piece of code may be shared by the separate binder
 */
#define bindFunction(s, p, mb, out)									\
	do {															\
		if (s->def->errors) {										\
			p->typechk = TYPE_UNKNOWN;								\
			mb->errors++;											\
			goto wrapup;											\
		}															\
		if (p->token == ASSIGNsymbol) {								\
			switch (getSignature(s)->token) {						\
			case COMMANDsymbol:										\
				p->token = CMDcall;									\
				p->fcn = getSignature(s)->fcn;      /* C implementation mandatory */ \
				if (p->fcn == NULL) {								\
					showScriptException(out, mb, getPC(mb, p), TYPE, \
										"object code for command %s.%s missing", \
										p->modname, p->fcnname);	\
					p->typechk = TYPE_UNKNOWN;						\
					mb->errors++;									\
					goto wrapup;									\
				}													\
				break;												\
			case PATTERNsymbol:										\
				p->token = PATcall;									\
				p->fcn = getSignature(s)->fcn;      /* C implementation optional */	\
				break;												\
			case FACTORYsymbol:										\
				p->token = FACcall;									\
				p->fcn = getSignature(s)->fcn;      /* C implementation optional */	\
				break;												\
			case FUNCTIONsymbol:									\
				p->token = FCNcall;									\
				if (getSignature(s)->fcn)							\
					p->fcn = getSignature(s)->fcn;     /* C implementation optional */ \
				break;												\
			default: {												\
				if (!silent)										\
					showScriptException(out, mb, getPC(mb, p), MAL,	\
										"MALresolve: unexpected token type"); \
				mb->errors++;										\
				goto wrapup;										\
			}														\
			}														\
			p->blk = s->def;										\
		}															\
	} while (0)

/*
 * Since we now know the storage type of the receiving variable, we can
 * set the garbage collection flag.
 */
#define prepostProcess(tp, p, b, mb)					\
	do {												\
		if (findGDKtype(tp) == TYPE_bat ||				\
			isaBatType(tp) ||							\
			findGDKtype(tp) == TYPE_str ||				\
			(!isPolyType(tp) && tp < TYPE_any &&		\
			 tp >= 0 && ATOMextern(tp))) {				\
			getInstrPtr(mb, 0)->gc |= GARBAGECONTROL;	\
			setVarCleanup(mb, getArg(p, b));			\
			p->gc |= GARBAGECONTROL;					\
		}												\
	} while (0)

static malType
findFunctionType(stream *out, Module scope, MalBlkPtr mb, InstrPtr p, int silent)
{
	Module m;
	Symbol s;
	InstrPtr sig;
	int i, k, unmatched = 0, s1;
	/* int foundbutwrong=0; */
	int polytype[MAXTYPEVAR];
	int returns[256];
	int *returntype = NULL;
	/*
	 * Within a module find the subscope to locate the element in its list
	 * of symbols. A skiplist is used to speed up the search for the
	 * definition of the function.
	 *
	 * For the implementation we should be aware that over 90% of the
	 * functions in the kernel have just a few arguments and a single
	 * return value.
	 * A point of concern is that polymorphic arithmetic operations
	 * lead to an explosion in the symbol table. This increase the
	 * loop to find a candidate.
	 *
	 * Consider to collect the argument type into a separate structure, because
	 * it will be looked up multiple types to resolve the instruction.[todo]
	 * Simplify polytype using a map into the concrete argument table.
	 */
	m = scope;
	s = m->subscope[(int) (getSubScope(getFunctionId(p)))];
	if (s == 0)
		return -1;

	if ( p->retc < 256){
		for(i=0; i< p->retc; i++) returns[i] = 0;
		returntype = returns;
	} else 
		returntype = (int *) GDKzalloc(p->retc * sizeof(int));
	if (returntype == 0)
		return -1;

	while (s != NULL) {			/* single scope element check */
		if (getFunctionId(p) != s->name) {
			s = s->skip;
			continue;
		}
		/*
		 * Perform a strong type-check on the actual arguments. If it
		 * turns out to be a polymorphic MAL function, we have to
		 * clone it.  Provided the actual/formal parameters are
		 * compliant throughout the function call.
		 *
		 * Also look out for variable argument lists. This means that
		 * we have to keep two iterators, one for the caller (i) and
		 * one for the callee (k). Since a variable argument only
		 * occurs as the last one, we simple avoid an increment when
		 * running out of formal arguments.
		 *
		 * A call of the form (X1,..., Xi) := f(Y1,....,Yn) can be
		 * matched against the function signature (B1,...,Bk):=
		 * f(A1,...,Am) where i==k , n<=m and
		 * type(Ai)=type(Yi). Furthermore, the variables Xi obtain
		 * their type from Bi (or type(Bi)==type(Xi)).
		 */
		sig = getSignature(s);
		unmatched = 0;

#ifdef DEBUG_MAL_RESOLVE
		if (tracefcn) {
			mnstr_printf(out, "-->resolving\n");
			printInstruction(out, mb, 0, p, LIST_MAL_ALL);
			mnstr_printf(out, "++> test against signature\n");
			printInstruction(out, s->def, 0, getSignature(s), LIST_MAL_ALL);
			mnstr_printf(out, " %s \n", sig->polymorphic ? "polymorphic" : "");
		}
#endif
		/*
		 * The simple case could be taken care of separately to
		 * speedup processing
		 * However, it turned out not to make a big difference.  The
		 * first time we encounter a polymorphic argument in the
		 * signature.
		 * Subsequently, the polymorphic arguments update this table
		 * and check for any type mismatches that might occur.  There
		 * are at most 2 type variables involved per argument due to
		 * the limited type nesting permitted.  Note, each function
		 * returns at least one value.
		 */
		if (sig->polymorphic) {
			int limit = sig->polymorphic;
			if (!(sig->argc == p->argc ||
				  (sig->argc < p->argc && sig->varargs & (VARARGS | VARRETS)))
				) {
				s = s->peer;
				continue;
			}
			if (sig->retc != p->retc && !(sig->varargs & VARRETS)) {
				s = s->peer;
				continue;
			}
/*  if(polyVector[0]==0) polyInit();
    memcpy(polytype,polyVector, 2*sig->argc*sizeof(int)); */

			for (k = 0; k < limit; k++)
				polytype[k] = TYPE_any;
			/*
			 * Most polymorphic functions don;t have a variable argument
			 * list. So we save some instructions factoring this caise out.
			 * Be careful, the variable number of return arguments should
			 * be considered as well.
			 */
			i = p->retc;
			/* first handle the variable argument list */
			for (k = sig->retc; i < p->argc; k++, i++) {
				int actual = getArgType(mb, p, i);
				int formal = getArgType(s->def, sig, k);
				if (k == sig->argc - 1 && sig->varargs & VARARGS)
					k--;
				/*
				 * Take care of variable argument lists.
				 * They are allowed as the last in the signature only.
				 * Furthermore, for patterns if the formal type is
				 * 'any' then all remaining arguments are acceptable
				 * and detailed type analysis becomes part of the
				 * pattern implementation.
				 * In all other cases the type should apply to all
				 * remaining arguments.
				 */
				if (formal == actual)
					continue;
				if (updateTypeMap(formal, actual, polytype)) {
					unmatched = i;
					break;
				}
				formal = getPolyType(formal, polytype);
				/*
				 * Collect the polymorphic types and resolve them.
				 * If it fails, we know this isn;t the function we are
				 * looking for.
				 */
				if (resolveType(formal, actual) == -1) {
					unmatched = i;
					break;
				}
			}
			/*
			 * The last argument/result type could be a polymorphic
			 * variable list.  It should only be allowed for patterns,
			 * where it can deal with the stack.  If the type is
			 * specified as :any then any mix of arguments is allowed.
			 * If the type is a new numbered type variable then the
			 * first element in the list determines the required type
			 * of all.
			 */
			if (sig->varargs) {
				if (sig->token != PATTERNsymbol)
					unmatched = i;
				else {
					/* resolve the arguments */
					for (; i < p->argc; i++) {
						/* the type of the last one has already been set */
						int actual = getArgType(mb, p, i);
						int formal = getArgType(s->def, sig, k);
						if (k == sig->argc - 1 && sig->varargs & VARARGS)
							k--;

						formal = getPolyType(formal, polytype);
						if (formal == actual || formal == TYPE_any)
							continue;
						if (resolveType(formal, actual) == -1) {
							unmatched = i;
							break;
						}
					}
				}
			}
		} else {
			/*
			 * We have to check the argument types to determine a
			 * possible match for the non-polymorphic case.
			 */
			if (sig->argc != p->argc || sig->retc != p->retc) {
				s = s->peer;
				continue;
			}
			for (i = p->retc; i < p->argc; i++) {
				int actual = getArgType(mb, p, i);
				int formal = getArgType(s->def, sig, i);
				if (resolveType(formal, actual) == -1) {
#ifdef DEBUG_MAL_RESOLVE
					mnstr_printf(out, "unmatched %d formal %s actual %s\n",
								 i, getTypeName(formal), getTypeName(actual));
#endif
					unmatched = i;
					break;
				}
			}
		}
		/*
		 * It is possible that you may have to coerce the value to
		 * another type.  We assume that coercions are explicit at the
		 * MAL level. (e.g. var2:= var0:int). This avoids repeated
		 * type analysis just before you execute a function.
		 * An optimizer may at a later stage automatically insert such
		 * coercion requests.
		 */
#ifdef DEBUG_MAL_RESOLVE
		if (tracefcn) {
			mnstr_printf(out, "finished %s.%s unmatched=%d polymorphic=%d %d\n",
						 getModuleId(sig), getFunctionId(sig), unmatched,
						 sig->polymorphic, p == sig);
			if (sig->polymorphic) {
				int l;
				for (l = 0; l < 2 * p->argc; l++)
					if (polytype[l] != TYPE_any)
						mnstr_printf(out, "poly %d %s\n",
									 l, getTypeName(polytype[l]));
			}
			mnstr_printf(out, "-->resolving\n");
			printInstruction(out, mb, 0, p, LIST_MAL_ALL);
			mnstr_printf(out, "++> test against signature\n");
			printInstruction(out, s->def, 0, getSignature(s), LIST_MAL_ALL);
			mnstr_printf(out, "\nmismatch unmatched %d test %s poly %s\n",
						 unmatched, getTypeName(getArgType(mb, p, unmatched)),
						 getTypeName(getArgType(s->def, sig, unmatched)));
		}
#endif
		if (unmatched) {
			s = s->peer;
			continue;
		}
		/*
		 * At this stage we know all arguments are type compatible
		 * with the signature.
		 * We should assure that also the target variables have the
		 * proper types or can inherit them from the signature. The
		 * result type vector should be build separately first,
		 * because we may encounter an error later on.
		 *
		 * If any of the arguments refer to a constraint type, any_x,
		 * then the resulting type can not be determined.
		 */
		s1 = 0;
		if (sig->polymorphic)
			for (k = i = 0; i < p->retc; k++, i++) {
				int actual = getArgType(mb, p, i);
				int formal = getArgType(s->def, sig, k);

				if (k == sig->retc - 1 && sig->varargs & VARRETS)
					k--;

				s1 = getPolyType(formal, polytype);

				returntype[i] = resolveType(s1, actual);
				if (returntype[i] == -1) {
					s1 = -1;
					break;
				}
			}
		else
			/* check for non-polymorphic return */
			for (k = i = 0; i < p->retc; i++) {
				int actual = getArgType(mb, p, i);
				int formal = getArgType(s->def, sig, i);

				if (k == sig->retc - 1 && sig->varargs & VARRETS)
					k--;

				if (actual == formal)
					returntype[i] = actual;
				else {
					returntype[i] = resolveType(formal, actual);
					if (returntype[i] == -1) {
						s1 = -1;
						break;
					}
				}
			}
		if (s1 < 0) {
			/* if(getSignature(s)->token !=PATTERNsymbol) foundbutwrong++; */
			s = s->peer;
			continue;
		}
		/*
		 * If the return types are correct, copy them in place.
		 * Beware that signatures should be left untouched, which
		 * means that we may not overwrite any formal argument.
		 * Using the knowledge dat the arguments occupy the header
		 * of the symbol stack, it is easy to filter such errors.
		 * Also mark all variables that are subject to garbage control.
		 * Beware, this is not yet effectuated in the interpreter.
		 */
		p->typechk = TYPE_RESOLVED;
		for (i = 0; i < p->retc; i++) {
			int ts = returntype[i];
			if (isVarConstant(mb, getArg(p, i))) {
				showScriptException(out, mb, getPC(mb, p), TYPE, "Assignment to constant");
				p->typechk = TYPE_UNKNOWN;
				mb->errors++;
				goto wrapup;
			}
			if (!isVarFixed(mb, getArg(p, i)) && ts >= 0) {
				setVarType(mb, getArg(p, i), ts);
				setVarFixed(mb, getArg(p, i));
			}
			prepostProcess(ts, p, i, mb);
		}
		/*
		 * Also the arguments may contain constants
		 * to be garbage collected.
		 */
		for (i = p->retc; i < p->argc; i++)
			if (findGDKtype(getArgType(mb, p, i)) == TYPE_str ||
				getArgType(mb, p, i) == TYPE_bat ||
				isaBatType(getArgType(mb, p, i)) ||
				(!isPolyType(getArgType(mb, p, i)) &&
				 getArgType(mb, p, i) < TYPE_any &&
				 getArgType(mb, p, i) >= 0 &&
				 ATOMstorage(getArgType(mb, p, i)) == TYPE_str)) {
				getInstrPtr(mb, 0)->gc |= GARBAGECONTROL;
				p->gc |= GARBAGECONTROL;
			}
		/*
		 * It may happen that an argument was still untyped and as a
		 * result of the polymorphism matching became strongly
		 * typed. This should be reflected in the symbol table.
		 */
		s1 = returntype[0];		/* for those interested */
		/* foundbutwrong = 0; */
		/*
		 * If the call refers to a polymorphic function, we clone it
		 * to arrive at a bounded instance. Polymorphic patterns and
		 * commands are responsible for type resolution themselves.
		 * Note that cloning pre-supposes that the function being
		 * cloned does not contain errors detected earlier in the
		 * process, nor does it contain polymorphic actual arguments.
		 */
		if (sig->polymorphic) {
			int cnt = 0;
			for (k = i = p->retc; i < p->argc; i++) {
				int actual = getArgType(mb, p, i);
				if (isAnyExpression(actual))
					cnt++;
			}
			if (cnt == 0 && s->kind != COMMANDsymbol && s->kind != PATTERNsymbol) {
				s = cloneFunction(out, scope, s, mb, p);
				if (s->def->errors)
					goto wrapup;
			}
		}
		bindFunction(s, p, mb, out);

#ifdef DEBUG_MAL_RESOLVE
		if (tracefcn) {
			printInstruction(out, mb, 0, p, LIST_MAL_ALL);
			mnstr_printf(out, "Finished matching\n");
		}
#endif
		if (returntype && returntype != returns)
			GDKfree(returntype);
		return s1;
	} /* while */
	/*
	 * We haven't found the correct function.  To ease debugging, we
	 * may reveal that we found an instruction with the proper
	 * arguments, but that clashes with one of the target variables.
	 */
  wrapup:
	/* foundbutwrong has not been changed, commented out code above
		if (foundbutwrong && !silent) {
			showScriptException(out, mb, getPC(mb, p), TYPE,
								"type conflict in assignment");
		}
	 */
	if (returntype && returntype != returns)
		GDKfree(returntype);
	return -3;
}

int
resolveType(int dsttype, int srctype)
{
#ifdef DEBUG_MAL_RESOLVE
	if (tracefcn) {
		mnstr_printf(GDKout, "resolveType dst %s (%d) %s(%d)\n",
					 getTypeName(dsttype), dsttype,
					 getTypeName(srctype), srctype);
	}
#endif
	if (dsttype == srctype)
		return dsttype;
	if (dsttype == TYPE_any)
		return srctype;
	if (srctype == TYPE_any)
		return dsttype;
	/*
	 * A bat reference can be coerced to bat type.
	 */
	if (isaBatType(srctype) && dsttype == TYPE_bat)
		return srctype;
	if (isaBatType(dsttype) && srctype == TYPE_bat)
		return dsttype;
	if (isaBatType(dsttype) && isaBatType(srctype)) {
		int h1, t1, h2, t2, h3, t3;
		h1 = getHeadType(dsttype);
		h2 = getHeadType(srctype);
		if (h1 == h2)
			h3 = h1;
		else if (h1 == TYPE_any)
			h3 = h2;
		else if (h2 == TYPE_any)
			h3 = h1;
		else {
#ifdef DEBUG_MAL_RESOLVE
			if (tracefcn)
				mnstr_printf(GDKout, "Head can not be resolved \n");
#endif
			return -1;
		}
		t1 = getTailType(dsttype);
		t2 = getTailType(srctype);
		if (t1 == t2)
			t3 = t1;
		else if (t1 == TYPE_any)
			t3 = t2;
		else if (t2 == TYPE_any)
			t3 = t1;
		else {
#ifdef DEBUG_MAL_RESOLVE
			if (tracefcn)
				mnstr_printf(GDKout, "Tail can not be resolved \n");
#endif
			return -1;
		}
#ifdef DEBUG_MAL_RESOLVE
		if (tracefcn) {
			int i1 = getHeadIndex(dsttype);
			int i2 = getTailIndex(dsttype);
			mnstr_printf(GDKout, "resolved to bat[:%s,:%s] bat[:%s,:%s]->bat[%s:%d,%s:%d]\n",
						 getTypeName(h1), getTypeName(t1),
						 getTypeName(h2), getTypeName(t2),
						 getTypeName(h3), i1, getTypeName(t3), i2);
		}
#endif
		return newBatType(h3, t3);
	}
#ifdef DEBUG_MAL_RESOLVE
	if (tracefcn)
		mnstr_printf(GDKout, "Can not be resolved \n");
#endif
	return -1;
}

/*
 * We try to clear the type check flag by looking up the
 * functions. Errors are simply ignored at this point of the game,
 * because they may be resolved as part of the calling sequence.
 */
static void
typeMismatch(stream *out, MalBlkPtr mb, InstrPtr p, int lhs, int rhs, int silent)
{
	str n1;
	str n2;

	if (!silent) {
		n1 = getTypeName(lhs);
		n2 = getTypeName(rhs);
		showScriptException(out, mb, getPC(mb, p), TYPE,
							"type mismatch %s := %s", n1, n2);
		GDKfree(n1);
		GDKfree(n2);
	}
	mb->errors++;
	p->typechk = TYPE_UNKNOWN;
}

/*
 * A function search should inspect all modules unless a specific module
 * is given. Preference is given to the lower scopes.
 * The type check is set to TYPE_UNKNOWN first to enforce a proper
 * analysis. This way it forms a cheap mechanism to resolve
 * the type after a change by an optimizer.
 * If we can not find the function, the type check returns unsuccessfully.
 * In this case we should issue an error message to the user.
 *
 * A re-check after the optimizer call should reset the token
 * to assignment.
 */
void
typeChecker(stream *out, Module scope, MalBlkPtr mb, InstrPtr p, int silent)
{
	int s1 = -1, i, k, olderrors;
	Module m = 0;

	p->typechk = TYPE_UNKNOWN;
	olderrors = mb->errors;
	if (p->fcn && p->token >= FCNcall && p->token <= PATcall) {
		p->token = ASSIGNsymbol;
		p->fcn = NULL;
		p->blk = NULL;
	}

	if (isaSignature(p)) {
		for (k = 0; k < p->argc; k++)
			setVarFixed(mb, getArg(p, k));
		for (k = p->retc; k < p->argc; k++) {
			prepostProcess(getArgType(mb, p, k), p, k, mb);
		}
		p->typechk = TYPE_RESOLVED;
		for (k = 0; k < p->retc; k++)
			p->typechk = MIN(p->typechk, typeKind(mb, p, 0));
		return;
	}
	if (getFunctionId(p) && getModuleId(p)) {
#ifdef DEBUG_MAL_RESOLVE
		tracefcn = idcmp(getFunctionId(p), traceFcnName) == 0;
#endif
		m = findModule(scope, getModuleId(p));
		s1 = findFunctionType(out, m, mb, p, silent);
		if (s1 >= 0)
			return;
		/*
		 * Could not find a function that statisfies the constraints.
		 * If the instruction is just a function header we may
		 * continue.  Likewise, the function and module may refer to
		 * string variables known only at runtime.
		 *
		 * In all other cases we should generate a message, but only
		 * if we know that the error was not caused by checking the
		 * definition of a polymorphic function or the module or
		 * function name are variables, In those cases, the detailed
		 * analysis is performed upon an actual call.
		 */
		if (!isaSignature(p) && !getInstrPtr(mb, 0)->polymorphic) {
			mb->errors++;
			if (!silent) {
				char errsig[4 * PATHLENGTH] = "";

				instructionCall(mb, p, errsig, errsig, sizeof(errsig) - 20 - 2 * strlen(getModuleId(p)) - strlen(getFunctionId(p)) - strlen(errsig));
				showScriptException(out, mb, getPC(mb, p), TYPE,
									"'%s%s%s' undefined in: %s",
									(getModuleId(p) ? getModuleId(p) : ""),
									(getModuleId(p) ? "." : ""),
									getFunctionId(p), errsig);
			} else
				mb->errors = olderrors;
			p->typechk = TYPE_UNKNOWN;
		} else
			p->typechk = TYPE_RESOLVED;
		return;
	}
	/*
	 * @- Assignment
	 * When we arrive here the operator is an assignment.
	 * The language should also recognize (a,b):=(1,2);
	 * This is achieved by propagation of the rhs types to the lhs
	 * variables.
	 */
	if (getFunctionId(p))
		return;
	if (p->retc >= 1 && p->argc > p->retc && p->argc != 2 * p->retc) {
		if (!silent)
			showScriptException(out, mb, getPC(mb, p), TYPE,
								"Multiple assignment mismatch");
		mb->errors++;
	} else
		p->typechk = TYPE_RESOLVED;
	for (k = 0, i = p->retc; k < p->retc && i < p->argc; i++, k++) {
		int rhs = getArgType(mb, p, i);
		int lhs = getArgType(mb, p, k);

		if (rhs != TYPE_void) {
			s1 = resolveType(lhs, rhs);
			if (s1 == -1) {
				typeMismatch(out, mb, p, lhs, rhs, silent);
				return;
			}
		} else {
			/*
			 * The language permits assignment of 'nil' to any variable,
			 * using the target type.
			 */
			if (lhs != TYPE_void && lhs != TYPE_any) {
				ValRecord cst;
				cst.vtype = TYPE_void;
				cst.val.oval = void_nil;

				rhs = isaBatType(lhs) ? TYPE_bat : lhs;
				p->argv[i] = defConstant(mb, rhs, &cst);
				rhs = lhs;
			}
		}

		if (!isVarFixed(mb, getArg(p, k))) {
			setVarType(mb, getArg(p, k), rhs);
			setVarFixed(mb, getArg(p, k));
		}
		prepostProcess(s1, p, i, mb);
		prepostProcess(s1, p, k, mb);
	}
	/* the case where we have no rhs */
	if (p->barrier && p->retc == p->argc)
		for (k = 0; k < p->retc; k++) {
			int tpe = getArgType(mb, p, k);
			if (findGDKtype(tpe) == TYPE_bat ||
				findGDKtype(tpe) == TYPE_str ||
				(!isPolyType(tpe) && tpe < TYPE_any && ATOMextern(tpe)))
				setVarCleanup(mb, getArg(p, k));
		}
}

/*
 * @- Function binder
 * In some cases the front-end may already assure type correctness
 * of the MAL instruction generated (e.g. the SQL front-end)
 * In that case we merely have to locate the function address and
 * finalize the code for execution. Beware that we should be able to
 * distinguish the function by module name, function name, and
 * number of arguments only. Whether this is sufficient remains
 * to be seen.
 */
int
fcnBinder(stream *out, Module scope, MalBlkPtr mb, InstrPtr p)
{
	Module m = 0;
	Symbol s;
	int silent = FALSE;

	if (p->token != ASSIGNsymbol)
		return 0;
	if (getModuleId(p) == NULL || getFunctionId(p) == NULL)
		return 0;
	for (m = findModule(scope, getModuleId(p)); m; m = m->outer)
		if (m->name == getModuleId(p)) {
			s = m->subscope[(int) (getSubScope(getFunctionId(p)))];
			for (; s; s = s->peer)
				if (getFunctionId(p) == s->name &&
					p->argc == getSignature(s)->argc) {
					/* found it */
					bindFunction(s, p, mb, out);
				}
		}
  wrapup:
	return 0;
}

/*
 * After the parser finishes, we have to look for semantic errors,
 * such as flow of control problems and possible typeing conflicts.
 * The nesting of BARRIER and CATCH statements with their associated
 * flow of control primitives LEAVE and RETRY should form a valid
 * hierarchy. Failure to comply is considered a structural error
 * and leads to flagging the function as erroneous.
 * Also check general conformaty of the ML block structure.
 * It should start with a signature and finish with and ENDsymbol
 *
 * Type checking a program is limited to those instructions that are
 * not resolved yet. Once the program is completely checked, further calls
 * should be ignored. This should be separately administered for the flow
 * as well, because a dynamically typed instruction should later on not
 * lead to a re-check when it was already fully analyzed.
 */
void
chkTypes(stream *out, Module s, MalBlkPtr mb, int silent)
{
	InstrPtr p = 0;
	int i, chk = 0;

	for (i = 0; i < mb->stop; i++) {
		p = getInstrPtr(mb, i);
		if (p == NULL)
			continue;
		typeChecker(out, s, mb, p, silent);

		if (getFunctionId(p)) {
			if (p->fcn != NULL && p->typechk == TYPE_RESOLVED)
				chk++;
		} else if (p->typechk == TYPE_RESOLVED)
			chk++;
	}
}

/*
 * Type checking an individual instruction is dangerous,
 * because it ignores data flow and declarations issues.
 * It is only to be used in isolated cases.
 */
void
chkInstruction(stream *out, Module s, MalBlkPtr mb, InstrPtr p)
{
	typeChecker(out, s, mb, p, FALSE);
}

void
chkProgram(stream *out, Module s, MalBlkPtr mb)
{
/* it is not ready yet, too fragile
		mb->typefixed = mb->stop == chk; ignored END */
/*	if( mb->flowfixed == 0)*/

	chkTypes(out, s, mb, FALSE);
	chkFlow(out, mb);
	chkDeclarations(out, mb);
	/* malGarbageCollector(mb); */
}

/*
 * @- Polymorphic type analysis
 * MAL provides for type variables of the form any$N. This feature
 * supports polymorphic types, but also complicates the subsequent
 * analysis. A variable typed with any$N not occuring in the function
 * header leads to a dynamic typed statement. In principle we have
 * to type check the function upon each call.
 */
static int
typeKind(MalBlkPtr mb, InstrPtr p, int i)
{
	malType t = getArgType(mb, p, i);
	if (t == TYPE_any || isAnyExpression(t)) {
		return TYPE_UNKNOWN;
	}
	return TYPE_RESOLVED;
}

/*
 * For a polymorphic commands we do not generate a cloned version.
 * It suffices to determine the actual return value taking into
 * account the type variable constraints.
 */
static malType
getPolyType(malType t, int *polytype)
{
	int hi, ti;
	int head, tail;

	ti = getTailIndex(t);
	if (!isaBatType(t) && ti > 0)
		return polytype[ti];

	tail = ti == 0 ? getTailType(t) : polytype[ti];
	if (isaBatType(t)) {
		hi = getHeadIndex(t);
		head = hi == 0 ? getHeadType(t) : polytype[hi];
		return newBatType(head, tail);
	}
	return tail;
}

/*
 * Each argument is checked for binding of polymorphic arguments.
 * This routine assumes that the type index is indeed smaller than maxarg.
 * (The parser currently enforces a single digit from 1-9 )
 * The polymorphic type 'any', i.e. any_0, does never constraint an operation
 * it can match with all polymorphic types.
 * The routine returns the instanciated formal type for subsequent
 * type resolution.
 */
static int
updateTypeMap(int formal, int actual, int polytype[MAXTYPEVAR])
{
	int h, t, ret = 0;

	if (formal == TYPE_bat && isaBatType(actual))
		return 0;
#ifdef DEBUG_MAL_RESOLVE
	mnstr_printf(GDKout, "updateTypeMap:");
	mnstr_printf(GDKout, "formal %s ", getTypeName(formal));
	mnstr_printf(GDKout, "actual %s\n", getTypeName(actual));
#endif

	if ((h = getTailIndex(formal))) {
		if (isaBatType(actual) && !isaBatType(formal) &&
			(polytype[h] == TYPE_any || polytype[h] == actual)) {
			polytype[h] = actual;
			ret = 0;
			goto updLabel;
		}
		t = getTailType(actual);
		if (t != polytype[h]) {
			if (polytype[h] == TYPE_bat && isaBatType(actual))
				ret = 0;
			else if (polytype[h] == TYPE_any)
				polytype[h] = t;
			else {
				ret = -1;
				goto updLabel;
			}
		}
	}
	if (isaBatType(formal)) {
		if (!isaBatType(actual) && actual != TYPE_bat)
			return -1;
		if ((h = getHeadIndex(formal))) {
			t = actual == TYPE_bat ? actual : getHeadType(actual);
			if (t != polytype[h]) {
				if (polytype[h] == TYPE_any)
					polytype[h] = t;
				else {
					ret = -1;
					goto updLabel;
				}
			}
		}
	}
  updLabel:
#ifdef DEBUG_MAL_RESOLVE
	mnstr_printf(GDKout, "updateTypeMap returns: %d\n", ret);
#endif
	return ret;
}
