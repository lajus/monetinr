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
 * @f mal_exception
 * @a F. Groffen, M. Kersten
 * @v 1.0
 *
 * @+ Exception handling
 * MAL comes with an exception handling mechanism, similar in style
 * as found in modern programming languages.
 * Exceptions are considered rare situations that alter
 * the flow of control to a place where they can be handled.
 * After the exceptional case has been handled the following options exist
 * a) continue where it went wrong, b) retry the failed instruction,
 * c) leave the block where the exception was handled,
 * or d) pass the exception to an enclosing call.
 * The current implementation of the MAL interpreter only supports c) and d).
 *
 * @- Exception control
 * The exception handling keywords are: @sc{catch} and @sc{raise}
 * The @sc{catch}  marks a point in the dataflow where
 * an exception raised can be dealt with. Any statement between the
 * point where it is raised and the catch block is ignored.
 * Moreover, the @sc{ catch} ... @sc{ exit} block is ignored when
 * no errors have occurred in the preceeding dataflow structure.
 * Within the catch block, the exception variable can be manipulated
 * without constraints.
 *
 * An exception message is linked with a exception variable of
 * type string. If this variable is defined in the receiving block,
 * the exception message can be delivered. Otherwise, it
 * implicitly raises the exception in the surrounding scope.
 * The variable ANYexception can be used to catch them
 * irrespective of their class.
 *
 * After an exception has been dealt with the catch block can be left
 * at the normal @sc{exit} with the option @sc{leave} or
 * continue after the failed instruction using a @sc{redo}.
 * The latter case assumes the caught code block has been able to provide
 * an alternative for the failed instruction.
 * [todo, the redo from failed instruction is not implemented yet]
 *
 * Both @sc{leave} and @sc{redo} are conditional flow of control modifiers,
 * which trigger on a non-empty string variable.
 * An exception raised within a catch-block terminates
 * the function and returns control to the enclosing environment.
 *
 * The argument to the catch statement is a target list,
 * which holds the exception variables you are interested in.
 *
 * The snippet below illustrates how an exception raised
 * in the function @sc{io.read} is caught using the exception variable IOerror.
 * After dealing with it locally, it raises a new exception @sc{ FATALerror}
 * for the enclosing call.
 *
 * @example
 * 	io.write("Welcome");
 * 	...
 * catch IOerror:str;
 * 	print("input error on reading password");
 * raise FATALerror:= "Can't handle it";
 * exit IOerror;
 * @end example
 *
 * Since @sc{catch} is a flow control modifier it can be attached to any
 * assignment statement. This statement is executed whenever there is no
 * exception outstanding, but will be ignored when control is moved
 * to the block otherwise.
 *
 * @- Builtin exceptions
 * The policy implemented in the MAL modules, and recognized by
 * the interpreter, is to return a string value by default.
 * A NULL return value indicates succesful execution; otherwise
 * the string encodes information to analyse the error occurred.
 *
 * This string pattern is strictly formatted and easy to analyse.
 * It starts with the name of the exception variable to
 * be set, followed by an indication where the exception was
 * raise, i.e. the function name and the program counter,
 * and concludes with specific information needed to interpret
 * and handle the exception.
 *
 * For example, the exception string
 * @sc{ 'MALException:Admin.main[2]:address of function missing'}
 * denotes an exception raised while typechecking a MAL program.
 *
 * The exceptions captured within the kernel are marked as 'GDKerror'.
 * At that level there is no knowledge about the MAL context, which
 * makes interpretation difficult for the average programmer.
 * Exceptions in the MAL language layer are denoted by 'MALerror',
 * and query language exceptiosn fall in their own class, e.g. 'SQLerror'.
 * Exceptions can be cascaded to form a trail of exceptions recognized
 * during the exection.
 *
 * @f mal_exception
 * @+ Error Handling
 * Internationalization and consistent error reporting is helped by a
 * central place of all kernel error messages.
 * They are split into @sc{fatal}, code{errors} and @sc{warnings}.
 * The second category is the system component in which it is raised.
 *
 * The first attempt: grep on src/MAL module.
 *
 * @-
 * @f mal_exception
 */
/*
 * @-
 */
#include "monetdb_config.h"
#include "mal_exception.h"

static char *exceptionNames[] = {
/* 0 */	"MALException",
/* 1 */	"IllegalArgumentException",
/* 2 */	"OutOfBoundsException",
/* 3 */	"IOException",
/* 4 */	"InvalidCredentialsException",
/* 5 */	"OptimizerException",
/* 6 */	"StackOverflowException",
/* 7 */	"SyntaxException",
/* 8 */	"TypeException",
/* 9 */	"LoaderException",
/*10 */	"ParseException",
/*11 */	"ArithmeticException",
/*12 */	"PermissionDeniedException",
/*13 */	"SQLException",
/*14 */	"RDFException",
/*15 */	"XQUERYException",
/*EOE*/	NULL
};

int
isExceptionVariable(str nme){
	int i;
	if( nme)
		for(i=0; exceptionNames[i]; i++)
		if( strcmp(exceptionNames[i],nme)==0)
			return 1;
	return 0;
}

char *M5OutOfMemory = "Memory allocation failed.";

/**
 * Internal helper function for createException and
 * showException such that they share the same code, because reuse
 * is good.
 */
static str createExceptionInternal(enum malexception type, const char *fcn, const char *format, va_list ap)
	__attribute__((__format__(__printf__, 3, 0)));
static str
createExceptionInternal(enum malexception type, const char *fcn, const char *format, va_list ap)
{
	char *message;
	int len;

	message = GDKmalloc(GDKMAXERRLEN);
	if (message == NULL)
		return M5OutOfMemory;
	len = snprintf(message, GDKMAXERRLEN, "%s:%s:", exceptionNames[type], fcn);
	if (len >= GDKMAXERRLEN)	/* shouldn't happen */
		return message;
	len += vsnprintf(message + len, GDKMAXERRLEN - len, format, ap);
	/* realloc to reduce amount of allocated memory (GDKMAXERRLEN is
	 * way more than what is normally needed) */
	if (len < GDKMAXERRLEN)
		message = GDKrealloc(message, len + 1);
	return message;
}

/**
 * Returns an exception string for the given type of exception, function
 * and additional formatting parameters.  This function will crash the
 * system or return bogus when the malexception enum is not aligned with
 * the exceptionNames array.
 */
str
createException(enum malexception type, const char *fcn, const char *format, ...)
{
	va_list ap;
	str ret;

	va_start(ap, format);
	ret = createExceptionInternal(type, fcn, format, ap);
	va_end(ap);

	return(ret);
}

/**
 * Internal helper function to properly emit the given string to out,
 * thereby abiding to all the protocol laws.
 */
void
dumpExceptionsToStream(stream *out, str whatever) {
	size_t i;
	size_t last = 0;
	size_t len ;

	if (whatever == NULL)
		return;
	len = strlen(whatever);
	/* make sure each line starts with a ! */
	for (i = 0; i < len; i++) {
		if (whatever[i] == '\n') {
			whatever[i] = '\0';
			if (i - last > 0) { /* skip empty lines */
				if (whatever[last] == '!') /* no need for double ! */
					last++;
				mnstr_printf(out, "!%s\n", whatever + last);
			}
			last = i + 1;
		}
	}
	/* flush last part */
	if (i - last > 0) /* skip if empty */
		mnstr_printf(out, "!%s\n", whatever + last);
}

/**
 * Dump an error message using the exception structure 
 */
void
showException(stream *out, enum malexception type, const char *fcn, const char *format, ...)
{
	va_list ap;
	str msg;

	va_start(ap, format);
	msg = createExceptionInternal(type, fcn, format, ap);
	va_end(ap);

	dumpExceptionsToStream(out, msg);
	GDKfree(msg);
}

/**
 * Internal helper function for createScriptException and
 * showScriptException such that they share the same code, because reuse
 * is good.
 */
static str
createScriptExceptionInternal(MalBlkPtr mb, int pc, enum malexception type, const char *prev, const char *format, va_list ap)
	__attribute__((__format__(__printf__, 5, 0)));
static str
createScriptExceptionInternal(MalBlkPtr mb, int pc, enum malexception type, const char *prev, const char *format, va_list ap)
{
	char buf[GDKMAXERRLEN];
	size_t i;
	str s, fcn;

	s = mb ? getModName(mb) : "unknown";
	fcn = mb ? getFcnName(mb) : "unknown";
	i = 0;

	if (prev)
		i += snprintf(buf + i, GDKMAXERRLEN - 1 - i, "%s\n", prev);
	i += snprintf(buf + i, GDKMAXERRLEN - 1 - i, "%s:%s.%s[%d]:",
			exceptionNames[type], s, fcn, pc);
	i += vsnprintf(buf + i, GDKMAXERRLEN - 1 - i, format, ap);
	buf[i] = '\0';

	return GDKstrdup(buf);
}

/**
 * Returns an exception string for the use of MAL scripts.  These
 * exceptions are newline terminated, and determine module and function
 * from the given MalBlkPtr.  An old exception can be given, such that
 * this exception is chained to the previous one.  Conceptually this
 * creates a "stack" of exceptions.
 * This function will crash the system or return bogus when the
 * malexception enum is not aligned with the exceptionNames array.
 */
str
createScriptException(MalBlkPtr mb, int pc, enum malexception type, const char *prev, const char *format, ...)
{
	va_list ap;
	str ret;

	va_start(ap, format);
	ret = createScriptExceptionInternal(mb, pc, type, prev, format, ap);
	va_end(ap);

	return(ret);
}

/**
 * Sends the exception as generated by a call to
 * createScriptException(mb, pc, type, NULL, format, ...) to a stream
 */
void
showScriptException(stream *out, MalBlkPtr mb, int pc, enum malexception type, const char *format, ...)
{
	va_list ap;
	str msg;

	va_start(ap, format);
	msg = createScriptExceptionInternal(mb, pc, type, NULL, format, ap);
	va_end(ap);

	dumpExceptionsToStream(out,msg);
	GDKfree(msg);
}

/**
 * Returns the malexception number for the given exception string.  If no
 * exception could be found in the string, MAL is returned indicating a
 * generic MALException.
 */
enum malexception
getExceptionType(str exception)
{
	enum malexception ret = MAL;
	str s;
	enum malexception i;

	if ((s = strchr(exception, ':')) != NULL)
		*s = '\0';

	for (i = MAL; exceptionNames[i] != NULL; i++) {
		if (strcmp(exceptionNames[i], exception) == 0) {
			ret = i;
			break;
		}
	}

	/* restore original string */
	if (s != NULL)
		*s = ':';

	return(ret);
}

/**
 * Returns the location the exception was raised, if known.  It
 * depends on how the exception was created, what the location looks
 * like.  The returned string is mallocced with GDKmalloc, and hence
 * needs to be GDKfreed.
 */
str
getExceptionPlace(str exception)
{
	str ret, s, t;
	enum malexception i;
	size_t l;

	for (i = MAL; exceptionNames[i] != NULL; i++) {
		l = strlen(exceptionNames[i]);
		if (strncmp(exceptionNames[i], exception, l) == 0 &&
			exception[l] == ':') {
			s = exception + l + 1;
			if ((t = strchr(s, ':')) != NULL) {
				if ((ret = GDKmalloc(t - s + 1)) == NULL)
					return NULL;
				strncpy(ret, s, t - s);
				ret[t - s] = 0;
				return ret;
			}
			break;
		}
	}
	return GDKstrdup("(unknown)");
}

/**
 * Returns the informational message of the exception given.
 */
str
getExceptionMessage(str exception)
{
	str s, t;
	enum malexception i;
	size_t l;

	for (i = MAL; exceptionNames[i] != NULL; i++) {
		l = strlen(exceptionNames[i]);
		if (strncmp(exceptionNames[i], exception, l) == 0 &&
			exception[l] == ':') {
			s = exception + l + 1;
			if ((t = strchr(s, ':')) != NULL)
				return t + 1;
			return s;
		}
	}
	if (strncmp(exception, "!ERROR: ", 8) == 0)
		return exception + 8;
	return exception;
}

/**
 * Returns the string representation of the given exception.  This is
 * the string as used when creating an exception of the same type.
 */
str
exceptionToString(enum malexception e)
{
	return(exceptionNames[e]);
}
