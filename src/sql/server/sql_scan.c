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
#include <sql_mem.h>
#include "sql_scan.h"
#include "sql_types.h"
#include "sql_symbol.h"
#include "sql_mvc.h"
#include "sql_parser.tab.h"
#include "sql_semantic.h"
#include "sql_parser.h"		/* for sql_error() */

#include <stream.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sql_keyword.h>

void
scanner_init_keywords(void)
{
	keywords_insert("false", BOOL_FALSE);
	keywords_insert("true", BOOL_TRUE);

	keywords_insert("ALTER", ALTER);
	keywords_insert("ADD", ADD);
	keywords_insert("AND", AND);
	keywords_insert("MEDIAN", AGGR);
	keywords_insert("CORR", AGGR2);
	keywords_insert("AVG", AGGR);
	keywords_insert("MIN", AGGR);
	keywords_insert("MAX", AGGR);
	keywords_insert("SUM", AGGR);
	keywords_insert("PROD", AGGR);
	keywords_insert("COUNT", AGGR);

	keywords_insert("LAG", AGGR);
	keywords_insert("LEAD", AGGR);
	keywords_insert("LAG", AGGR2);
	keywords_insert("LEAD", AGGR2);

	keywords_insert("RANK", RANK);
	keywords_insert("DENSE_RANK", RANK);
	keywords_insert("PERCENT_RANK", RANK);
	keywords_insert("CUME_DIST", RANK);
	keywords_insert("ROW_NUMBER", RANK);

	keywords_insert("AS", AS);
	keywords_insert("ASC", ASC);
	keywords_insert("AUTHORIZATION", AUTHORIZATION);
	keywords_insert("BETWEEN", BETWEEN);
	keywords_insert("SYMMETRIC", SYMMETRIC);
	keywords_insert("ASYMMETRIC", ASYMMETRIC);
	keywords_insert("BY", BY);
	keywords_insert("CAST", CAST);
	keywords_insert("CONVERT", CONVERT);
	keywords_insert("CHARACTER", CHARACTER);
	keywords_insert("CHAR", CHARACTER);
	keywords_insert("VARYING", VARYING);
	keywords_insert("VARCHAR", VARCHAR);
	keywords_insert("BINARY", BINARY);
	keywords_insert("LARGE", LARGE);
	keywords_insert("OBJECT", OBJECT);
	keywords_insert("CLOB", CLOB);
	keywords_insert("BLOB", sqlBLOB);
	keywords_insert("TEXT", sqlTEXT);
	keywords_insert("TINYTEXT", sqlTEXT);
	keywords_insert("STRING", CLOB);	/* ? */
	keywords_insert("CHECK", CHECK);
	keywords_insert("CONSTRAINT", CONSTRAINT);
	keywords_insert("CREATE", CREATE);
	keywords_insert("CROSS", CROSS);
	keywords_insert("COPY", COPY);
	keywords_insert("RECORDS", RECORDS);
	keywords_insert("DELIMITERS", DELIMITERS);
	keywords_insert("STDIN", STDIN);
	keywords_insert("STDOUT", STDOUT);

	keywords_insert("TINYINT", TINYINT);
	keywords_insert("SMALLINT", SMALLINT);
	keywords_insert("INTEGER", sqlINTEGER);
	keywords_insert("INT", sqlINTEGER);
	keywords_insert("MEDIUMINT", sqlINTEGER);
	keywords_insert("BIGINT", BIGINT);
	keywords_insert("DEC", sqlDECIMAL);
	keywords_insert("DECIMAL", sqlDECIMAL);
	keywords_insert("NUMERIC", sqlDECIMAL);
	keywords_insert("DECLARE", DECLARE);
	keywords_insert("DEFAULT", DEFAULT);
	keywords_insert("DESC", DESC);
	keywords_insert("DISTINCT", DISTINCT);
	keywords_insert("DOUBLE", sqlDOUBLE);
	keywords_insert("REAL", sqlREAL);
	keywords_insert("DROP", DROP);
	keywords_insert("ESCAPE", ESCAPE);
	keywords_insert("EXISTS", EXISTS);
	keywords_insert("EXTRACT", EXTRACT);
	keywords_insert("FLOAT", sqlFLOAT);
	keywords_insert("FOR", FOR);
	keywords_insert("FOREIGN", FOREIGN);
	keywords_insert("FROM", FROM);
	keywords_insert("REFERENCES", REFERENCES);

	keywords_insert("MATCH", MATCH);
	keywords_insert("FULL", FULL);
	keywords_insert("PARTIAL", PARTIAL);
	keywords_insert("SIMPLE", SIMPLE);

	keywords_insert("INSERT", INSERT);
	keywords_insert("UPDATE", UPDATE);
	keywords_insert("DATABASE", DATABASE);
	keywords_insert("DELETE", sqlDELETE);

	keywords_insert("ACTION", ACTION);
	keywords_insert("CASCADE", CASCADE);
	keywords_insert("RESTRICT", RESTRICT);
	keywords_insert("GLOBAL", GLOBAL);
	keywords_insert("GROUP", sqlGROUP);
	keywords_insert("HAVING", HAVING);
	keywords_insert("ILIKE", ILIKE);
	keywords_insert("IN", sqlIN);
	keywords_insert("INNER", INNER);
	keywords_insert("INTO", INTO);
	keywords_insert("IS", IS);
	keywords_insert("JOIN", JOIN);
	keywords_insert("KEY", KEY);
	keywords_insert("LEFT", LEFT);
	keywords_insert("LIKE", LIKE);
	keywords_insert("LIMIT", LIMIT);
	keywords_insert("SAMPLE", SAMPLE);
	keywords_insert("LOCAL", LOCAL);
	keywords_insert("LOCKED", LOCKED);
	keywords_insert("NATURAL", NATURAL);
	keywords_insert("NOT", NOT);
	keywords_insert("NULL", sqlNULL);
	keywords_insert("OFFSET", OFFSET);
	keywords_insert("ON", ON);
	keywords_insert("OPTIONS", OPTIONS);
	keywords_insert("OPTION", OPTION);
	keywords_insert("OR", OR);
	keywords_insert("ORDER", ORDER);
	keywords_insert("OUTER", OUTER);
	keywords_insert("OVER", OVER);
	keywords_insert("PARTITION", PARTITION);
	keywords_insert("PATH", PATH);
	keywords_insert("PRECISION", PRECISION);
	keywords_insert("PRIMARY", PRIMARY);

	keywords_insert("USER", USER);
	keywords_insert("RENAME", RENAME);
	keywords_insert("UNENCRYPTED", UNENCRYPTED);
	keywords_insert("ENCRYPTED", ENCRYPTED);
	keywords_insert("PASSWORD", PASSWORD);
	keywords_insert("GRANT", GRANT);
	keywords_insert("REVOKE", REVOKE);
	keywords_insert("ROLE", ROLE);
	keywords_insert("ADMIN", ADMIN);
	keywords_insert("PRIVILEGES", PRIVILEGES);
	keywords_insert("PUBLIC", PUBLIC);
	keywords_insert("CURRENT_USER", CURRENT_USER);
	keywords_insert("CURRENT_ROLE", CURRENT_ROLE);
	keywords_insert("SESSION_USER", SESSION_USER);
	keywords_insert("SESSION", sqlSESSION);

	keywords_insert("RIGHT", RIGHT);
	keywords_insert("SCHEMA", SCHEMA);
	keywords_insert("SELECT", SELECT);
	keywords_insert("SET", SET);
	keywords_insert("AUTO_COMMIT", AUTO_COMMIT);

	keywords_insert("ALL", ALL);
	keywords_insert("ANY", ANY);
	keywords_insert("SOME", SOME);
	keywords_insert("EVERY", ANY);
	/*
	   keywords_insert("SQLCODE", SQLCODE );
	 */
	keywords_insert("COLUMN", COLUMN);
	keywords_insert("TABLE", TABLE);
	keywords_insert("TEMPORARY", TEMPORARY);
	keywords_insert("TEMP", TEMPORARY);
	keywords_insert("STREAM", STREAM);
	keywords_insert("REMOTE", REMOTE);
	keywords_insert("MERGE", MERGE);
	keywords_insert("REPLICA", REPLICA);
	keywords_insert("TO", TO);
	keywords_insert("UNION", UNION);
	keywords_insert("EXCEPT", EXCEPT);
	keywords_insert("INTERSECT", INTERSECT);
	keywords_insert("CORRESPONDING", CORRESPONDING);
	keywords_insert("UNIQUE", UNIQUE);
	keywords_insert("USING", USING);
	keywords_insert("VALUES", VALUES);
	keywords_insert("VIEW", VIEW);
	keywords_insert("WHERE", WHERE);
	keywords_insert("WITH", WITH);
	keywords_insert("DATA", DATA);

	keywords_insert("DATE", sqlDATE);
	keywords_insert("TIME", TIME);
	keywords_insert("TIMESTAMP", TIMESTAMP);
	keywords_insert("INTERVAL", INTERVAL);
	keywords_insert("CURRENT_DATE", CURRENT_DATE);
	keywords_insert("CURRENT_TIME", CURRENT_TIME);
	keywords_insert("CURRENT_TIMESTAMP", CURRENT_TIMESTAMP);
	keywords_insert("NOW", CURRENT_TIMESTAMP);
	keywords_insert("LOCALTIME", LOCALTIME);
	keywords_insert("LOCALTIMESTAMP", LOCALTIMESTAMP);
	keywords_insert("ZONE", ZONE);

	keywords_insert("YEAR", YEAR);
	keywords_insert("MONTH", MONTH);
	keywords_insert("DAY", DAY);
	keywords_insert("HOUR", HOUR);
	keywords_insert("MINUTE", MINUTE);
	keywords_insert("SECOND", SECOND);

	keywords_insert("POSITION", POSITION);
	keywords_insert("SUBSTRING", SUBSTRING);

	keywords_insert("CASE", CASE);
	keywords_insert("WHEN", WHEN);
	keywords_insert("THEN", THEN);
	keywords_insert("ELSE", ELSE);
	keywords_insert("END", END);
	keywords_insert("NULLIF", NULLIF);
	keywords_insert("COALESCE", COALESCE);
	keywords_insert("ELSEIF", ELSEIF);
	keywords_insert("IF", IF);
	keywords_insert("WHILE", WHILE);
	keywords_insert("DO", DO);

	keywords_insert("COMMIT", COMMIT);
	keywords_insert("ROLLBACK", ROLLBACK);
	keywords_insert("SAVEPOINT", SAVEPOINT);
	keywords_insert("RELEASE", RELEASE);
	keywords_insert("WORK", WORK);
	keywords_insert("CHAIN", CHAIN);
	keywords_insert("PRESERVE", PRESERVE);
	keywords_insert("ROWS", ROWS);
	keywords_insert("NO", NO);
	keywords_insert("START", START);
	keywords_insert("TRANSACTION", TRANSACTION);
	keywords_insert("READ", READ);
	keywords_insert("WRITE", WRITE);
	keywords_insert("ONLY", ONLY);
	keywords_insert("ISOLATION", ISOLATION);
	keywords_insert("LEVEL", LEVEL);
	keywords_insert("UNCOMMITTED", UNCOMMITTED);
	keywords_insert("COMMITTED", COMMITTED);
	keywords_insert("REPEATABLE", sqlREPEATABLE);
	keywords_insert("SERIALIZABLE", SERIALIZABLE);
	keywords_insert("DIAGNOSTICS", DIAGNOSTICS);
	keywords_insert("SIZE", sqlSIZE);

	keywords_insert("TYPE", TYPE);
	keywords_insert("PROCEDURE", PROCEDURE);
	keywords_insert("FUNCTION", FUNCTION);
	keywords_insert("FILTER", FILTER);
	keywords_insert("AGGREGATE", AGGREGATE);
	keywords_insert("RETURNS", RETURNS);
	keywords_insert("EXTERNAL", EXTERNAL);
	keywords_insert("NAME", sqlNAME);
	keywords_insert("RETURN", RETURN);
	keywords_insert("CALL", CALL);
	keywords_insert("LANGUAGE", LANGUAGE);

	keywords_insert("EXPLAIN", SQL_EXPLAIN);
	keywords_insert("PLAN", SQL_PLAN);
	keywords_insert("DEBUG", SQL_DEBUG);
	keywords_insert("TRACE", SQL_TRACE);
	keywords_insert("DOT", SQL_DOT);
	keywords_insert("PREPARE", PREPARE);
	keywords_insert("PREP", PREPARE);
	keywords_insert("EXECUTE", EXECUTE);
	keywords_insert("EXEC", EXECUTE);

	keywords_insert("INDEX", INDEX);

	keywords_insert("SEQUENCE", SEQUENCE);
	keywords_insert("RESTART", RESTART);
	keywords_insert("INCREMENT", INCREMENT);
	keywords_insert("MAXVALUE", MAXVALUE);
	keywords_insert("MINVALUE", MINVALUE);
	keywords_insert("CYCLE", CYCLE);
	keywords_insert("NOMAXVALUE", NOMAXVALUE);
	keywords_insert("NOMINVALUE", NOMINVALUE);
	keywords_insert("NOCYCLE", NOCYCLE);
	keywords_insert("CACHE", CACHE);
	keywords_insert("NEXT", NEXT);
	keywords_insert("VALUE", VALUE);
	keywords_insert("GENERATED", GENERATED);
	keywords_insert("ALWAYS", ALWAYS);
	keywords_insert("IDENTITY", IDENTITY);
	keywords_insert("SERIAL", SERIAL);
	keywords_insert("BIGSERIAL", BIGSERIAL);
	keywords_insert("AUTO_INCREMENT", AUTO_INCREMENT);

	keywords_insert("TRIGGER", TRIGGER);
	keywords_insert("ATOMIC", ATOMIC);
	keywords_insert("BEGIN", BEGIN);
	keywords_insert("OF", OF);
	keywords_insert("BEFORE", BEFORE);
	keywords_insert("AFTER", AFTER);
	keywords_insert("ROW", ROW);
	keywords_insert("STATEMENT", STATEMENT);
	keywords_insert("NEW", sqlNEW);
	keywords_insert("OLD", OLD);
	keywords_insert("EACH", EACH);
	keywords_insert("REFERENCING", REFERENCING);

	keywords_insert("RANGE", RANGE);
	keywords_insert("UNBOUNDED", UNBOUNDED);
	keywords_insert("PRECEDING", PRECEDING);
	keywords_insert("FOLLOWING", FOLLOWING);
	keywords_insert("CURRENT", CURRENT);
	keywords_insert("EXCLUDE", EXCLUDE);
	keywords_insert("OTHERS", OTHERS);
	keywords_insert("TIES", TIES);

	/* special SQL/XML keywords */
	keywords_insert("XMLCOMMENT", XMLCOMMENT);
	keywords_insert("XMLCONCAT", XMLCONCAT);
	keywords_insert("XMLDOCUMENT", XMLDOCUMENT);
	keywords_insert("XMLELEMENT", XMLELEMENT);
	keywords_insert("XMLATTRIBUTES", XMLATTRIBUTES);
	keywords_insert("XMLFOREST", XMLFOREST);
	keywords_insert("XMLPARSE", XMLPARSE);
	keywords_insert("STRIP", STRIP);
	keywords_insert("WHITESPACE", WHITESPACE);
	keywords_insert("XMLPI", XMLPI);
	keywords_insert("XMLQUERY", XMLQUERY);
	keywords_insert("PASSING", PASSING);
	keywords_insert("XMLTEXT", XMLTEXT);
	keywords_insert("NIL", NIL);
	keywords_insert("REF", REF);
	keywords_insert("ABSENT", ABSENT);
	keywords_insert("DOCUMENT", DOCUMENT);
	keywords_insert("ELEMENT", ELEMENT);
	keywords_insert("CONTENT", CONTENT);
	keywords_insert("XMLNAMESPACES", XMLNAMESPACES);
	keywords_insert("NAMESPACE", NAMESPACE);
	keywords_insert("XMLVALIDATE", XMLVALIDATE);
	keywords_insert("RETURNING", RETURNING);
	keywords_insert("LOCATION", LOCATION);
	keywords_insert("ID", ID);
	keywords_insert("ACCORDING", ACCORDING);
	keywords_insert("XMLSCHEMA", XMLSCHEMA);
	keywords_insert("URI", URI);
	keywords_insert("XMLAGG", XMLAGG);

}

#define find_keyword_bs(lc, s) find_keyword(lc->rs->buf+lc->rs->pos+s)

void
scanner_init(struct scanner *s, bstream *rs, stream *ws)
{
	s->rs = rs;
	s->ws = ws;
	s->log = NULL;

	s->yynext = 0;
	s->yylast = 0;
	s->yyval = 0;
	s->yybak = 0;		/* keep backup of char replaced by EOS */
	s->yycur = 0;

	s->key = 0;		/* keep a hash key of the query */
	s->started = 0;
	s->as = 0;

	s->mode = LINE_N;
	s->schema = NULL;
}

void
scanner_query_processed(struct scanner *s)
{
	int cur;

	if (s->yybak) {
		s->rs->buf[s->rs->pos + s->yycur] = s->yybak;
		s->yybak = 0;
	}

	s->rs->pos += s->yycur;
	/* completely eat the query including white space after the ; */
	while (s->rs->pos < s->rs->len &&
	       (cur = s->rs->buf[s->rs->pos], isascii(cur) && isspace(cur))) {
		s->rs->pos++;
	}
	/*assert(s->rs->pos <= s->rs->len);*/
	s->yycur = 0;
	s->key = 0;		/* keep a hash key of the query */
	s->started = 0;
	s->as = 0;
	s->schema = NULL;
}

static int
scanner_error(mvc *lc, int cur)
{
	switch (cur) {
	case EOF:
		(void) sql_error(lc, 1, "unexpected end of input");
		return -1;	/* EOF needs -1 result */
	default:
		(void) sql_error(lc, 1, "unexpected%s character (U+%04X)", iscntrl(cur) ? " control" : "", cur);
	}
	return LEX_ERROR;
}


/*
   UTF-8 encoding is as follows:
U-00000000 - U-0000007F: 0xxxxxxx
U-00000080 - U-000007FF: 110xxxxx 10xxxxxx
U-00000800 - U-0000FFFF: 1110xxxx 10xxxxxx 10xxxxxx
U-00010000 - U-001FFFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
U-00200000 - U-03FFFFFF: 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
U-04000000 - U-7FFFFFFF: 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
*/
/* To be correctly coded UTF-8, the sequence should be the shortest
   possible encoding of the value being encoded.  This means that for
   an encoding of length n+1 (1 <= n <= 5), at least one of the bits in
   utf8chkmsk[n] should be non-zero (else the encoding could be
   shorter).
*/
static int utf8chkmsk[] = {
	0x0000007f,
	0x00000780,
	0x0000f800,
	0x001f0000,
	0x03e00000,
	0x7c000000
};

static void
utf8_putchar(struct scanner *lc, int ch)
{
	if ((ch) < 0x80) {
		lc->yycur--;
	} else if ((ch) < 0x800) {
		lc->yycur -= 2;
	} else if ((ch) < 0x10000) {
		lc->yycur -= 3;
	} else {
		lc->yycur -= 4;
	}
}

static inline int
scanner_read_more(struct scanner *lc, int n)
{
	bstream *b = lc->rs;
	int more = 0;


	while (b->len < b->pos + lc->yycur + n) {

		if (lc->mode == LINE_1 || !lc->started)
			return EOF;

		/* query is not finished ask for more */
		if (b->eof || !isa_block_stream(b->s)) {
			if (mnstr_write(lc->ws, PROMPT2, sizeof(PROMPT2) - 1, 1) == 1)
				mnstr_flush(lc->ws);
			b->eof = 0;
			more = 1;
		}
		/* we need more query text */
		if (bstream_next(b) < 0 ||
		    /* we asked for more data but didn't get any */
		    (more && b->eof && b->len < b->pos + lc->yycur + n))
			return EOF;
	}
	return 1;
}

static inline int
scanner_getc(struct scanner *lc)
{
	bstream *b = lc->rs;
	unsigned char *s = NULL;
	int c;
	int n, m, mask;

	if (scanner_read_more(lc, 1) == EOF)
		return EOF;

	s = (unsigned char *) b->buf + b->pos + lc->yycur++;
	if (((c = *s) & 0x80) == 0) {
		/* 7-bit char */
		return c;
	}
	for (n = 0, m = 0x40; c & m; n++, m >>= 1)
		;
	/* n now is number of 10xxxxxx bytes that should follow */
	if (n == 0 || n >= 6 || (b->pos + n) > b->len) {
		/* incorrect UTF-8 sequence */
		/* n==0: c == 10xxxxxx */
		/* n>=6: c == 1111111x */
		goto error;
	}

	if (scanner_read_more(lc, n) == EOF)
		return EOF;
	s = (unsigned char *) b->buf + b->pos + lc->yycur;

	mask = utf8chkmsk[n];
	c &= ~(0xFFC0 >> n);	/* remove non-x bits */
	while (--n >= 0) {
		c <<= 6;
		lc->yycur++;
		if (((m = *s++) & 0xC0) != 0x80) {
			/* incorrect UTF-8 sequence: byte is not 10xxxxxx */
			/* this includes end-of-string (m == 0) */
			goto error;
		}
		c |= m & 0x3F;
	}
	if ((c & mask) == 0) {
		/* incorrect UTF-8 sequence: not shortest possible */
		goto error;
	}

	/* if we find a BOM interpret it as a "zero-width non-breaking
	 * space" by just skipping it */
	if (c == 0xFEFF) {
		/* shift stuff so we won't "see" this BOM when it's in the
		 * middle of some word */
		memmove(b->buf + b->pos + 3, b->buf + b->pos, lc->yycur - 3);
		for (n = 0; n < 3; n++) {
			b->buf[b->pos++] = ' ';
			lc->yycur--;
		}
		return(scanner_getc(lc));
	}

	return c;

error:
	if (b->pos + lc->yycur < b->len)	/* skip bogus char */
		lc->yycur++;
	return EOF;
}

static int
scanner_token(struct scanner *lc, int token)
{
	lc->yybak = lc->rs->buf[lc->rs->pos + lc->yycur];
	lc->rs->buf[lc->rs->pos + lc->yycur] = 0;
	lc->yyval = token;
	return lc->yyval;
}

static int
scanner_string(mvc *c, int quote)
{
	struct scanner *lc = &c->scanner;
	bstream *rs = lc->rs;
	int cur = quote;
	int escape = 0;

	lc->started = 1;
	while (cur != EOF) {
		unsigned int pos = (int)rs->pos + lc->yycur;

		while ((((cur = rs->buf[pos++]) & 0x80) == 0) && cur && (cur != quote || escape)) {
			if (cur != '\\')
				escape = 0;
			else
				escape = !escape;
		}
		lc->yycur = pos - (int)rs->pos;
		/* check for quote escaped quote: Obscure SQL Rule */
		/* TODO also handle double "" */
		if (cur == quote && rs->buf[pos] == quote) {
			rs->buf[pos - 1] = '\\';
			lc->yycur++;
			continue;
		}
		assert(pos <= rs->len + 1);
		if (cur == quote && !escape) {
			return scanner_token(lc, STRING);
		}
		lc->yycur--;	/* go back to current (possibly invalid) char */
		/* long utf8, if correct isn't the quote */
		if (!cur) {
			if (lc->rs->len >= lc->rs->pos + lc->yycur + 1) {
				(void) sql_error(c, 2, "NULL byte in string");
				return LEX_ERROR;
			}
			cur = scanner_read_more(lc, 1);
		} else {
			cur = scanner_getc(lc);
		}
	}
	(void) sql_error(c, 2, "unexpected end of input");
	return LEX_ERROR;
}

static int 
keyword_or_ident(mvc * c, int cur)
{
	struct scanner *lc = &c->scanner;
	keyword *k = NULL;
	int s;

	lc->started = 1;
	utf8_putchar(lc, cur);
	s = lc->yycur;
	lc->yyval = IDENT;
	while ((cur = scanner_getc(lc)) != EOF) {
		if ((isascii(cur) && !isalnum(cur)) && cur != '_') {
			utf8_putchar(lc, cur);
			(void)scanner_token(lc, IDENT);
			k = find_keyword_bs(lc,s);
			if (k) 
				lc->yyval = k->token;
			/* find keyword in SELECT/JOIN/UNION FUNCTIONS */
			else if (sql_find_func(c->sa, cur_schema(c), lc->rs->buf+lc->rs->pos+s, -1, F_FILT)) 
				lc->yyval = FILTER_FUNC;
			return lc->yyval;
		}
	}
	(void)scanner_token(lc, IDENT);
	k = find_keyword_bs(lc,s);
	if (k) 
		lc->yyval = k->token;
	/* find keyword in SELECT/JOIN/UNION FUNCTIONS */
	else if (sql_find_func(c->sa, cur_schema(c), lc->rs->buf+lc->rs->pos+s, -1, F_FILT)) 
		lc->yyval = FILTER_FUNC;
	return lc->yyval;
}

static int 
skip_white_space(struct scanner * lc)
{
	int cur;

	lc->yysval = lc->yycur;
	while ((cur = scanner_getc(lc)) != EOF && isspace(cur))
		lc->yysval = lc->yycur;
	return cur;
}

static int 
skip_c_comment(struct scanner * lc)
{
	int cur;
	int prev = 0;
	int started = lc->started;

	lc->started = 1;
	while ((cur = scanner_getc(lc)) != EOF && 
	       !(cur == '/' && prev == '*')) 
		prev = cur;
	lc->yysval = lc->yycur;
	lc->started = started;
	if (cur == '/')
		cur = scanner_getc(lc);
	return cur;
}

static int 
skip_sql_comment(struct scanner * lc)
{
	int cur;
	int started = lc->started;

	lc->started = 1;
	while ((cur = scanner_getc(lc)) != EOF && (cur != '\n'))
		;
	lc->yysval = lc->yycur;
	lc->started = started;
	if (cur == '\n')
		cur = scanner_getc(lc);
	return cur;
}

static int tokenize(mvc * lc, int cur);

static int 
number(mvc * c, int cur)
{
	struct scanner *lc = &c->scanner;
	int token = sqlINT;
	int before_cur = EOF;

	lc->started = 1;
	if (cur == '0' && (cur = scanner_getc(lc)) == 'x') {
		while ((cur = scanner_getc(lc)) != EOF && 
				(isdigit(cur) || 
				 (cur >= 'A' && cur <= 'F') || 
				 (cur >= 'a' && cur <= 'f')))
			token = HEXADECIMAL; 
		if (token == sqlINT)
			before_cur = 'x';
	} else {
		if (isdigit(cur))
			while ((cur = scanner_getc(lc)) != EOF && isdigit(cur)) 
				;
		if (cur == '@') {
			token = OIDNUM;
			cur = scanner_getc(lc);
			if (cur == '0')
				cur = scanner_getc(lc);
		}

		if (cur == '.') {
			token = INTNUM;
	
			while ((cur = scanner_getc(lc)) != EOF && isdigit(cur)) 
				;
		}
		if (cur == 'e' || cur == 'E') {
			token = APPROXNUM;
			cur = scanner_getc(lc);
			if (cur == '-' || cur == '+') 
				token = 0;
			while ((cur = scanner_getc(lc)) != EOF && isdigit(cur)) 
				token = APPROXNUM;
		}
	}

	if (cur == EOF && lc->rs->buf == NULL) /* malloc failure */
		return EOF;

	if (token) {
		if (cur != EOF)
			utf8_putchar(lc, cur);
		if (before_cur != EOF)
			utf8_putchar(lc, before_cur);
		return scanner_token(lc, token);
	} else {
		(void)sql_error( c, 2, "unexpected symbol %c", cur);
		return LEX_ERROR;
	}
}

static
int scanner_symbol(mvc * c, int cur)
{
	struct scanner *lc = &c->scanner;
	int next = 0;
	int started = lc->started;

	switch (cur) {
	case '/':
		lc->started = 1;
		next = scanner_getc(lc);
		if (next == '*') {
			lc->started = started;
			cur = skip_c_comment(lc);
			return tokenize(c, cur);
		} else {
			utf8_putchar(lc, next); 
			return scanner_token(lc, cur);
		}
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return number(c, cur);
	case '#':
		if ((cur = skip_sql_comment(lc)) == EOF)
			return cur;
		return tokenize(c, cur);
	case '\'':
	case '"':
		return scanner_string(c, cur);
	case '-':
		lc->started = 1;
		next = scanner_getc(lc);
		if (next == '-') {
			lc->started = started;
			if ((cur = skip_sql_comment(lc)) == EOF)
				return cur;
			return tokenize(c, cur);
		}
		lc->started = 1;
		utf8_putchar(lc, next); 
		return scanner_token(lc, cur);
	case '~': /* binary not */
	case '^': /* binary xor */
	case '&': /* binary and */
	case '*':
	case '?':
	case '%':
	case '+':
	case '(':
	case ')':
	case ',':
	case '=':
	case '[':
	case ']':
		lc->started = 1;
		return scanner_token(lc, cur);
	case '@':
		lc->started = 1;
		return scanner_token(lc, AT);
	case ';':
		lc->started = 0;
		return scanner_token(lc, SCOLON);
	case '<':
		lc->started = 1;
		cur = scanner_getc(lc);
		if (cur == '=') {
			return scanner_token( lc, COMPARISON);
		} else if (cur == '>') {
			return scanner_token( lc, COMPARISON);
		} else if (cur == '<') {
			return scanner_token( lc, LEFT_SHIFT);
		} else {
			utf8_putchar(lc, cur); 
			return scanner_token( lc, COMPARISON);
		}
	case '>':
		lc->started = 1;
		cur = scanner_getc(lc);
		if (cur == '>') {
			return scanner_token( lc, RIGHT_SHIFT);
		} else if (cur != '=') {
			utf8_putchar(lc, cur); 
			return scanner_token( lc, COMPARISON);
		} else {
			return scanner_token( lc, COMPARISON);
		}
	case '.':
		lc->started = 1;
		cur = scanner_getc(lc);
		if (!isdigit(cur)) {
			utf8_putchar(lc, cur); 
			return scanner_token( lc, '.');
		} else {
			utf8_putchar(lc, cur); 
			cur = '.';
			return number(c, cur);
		}
	case '|': /* binary or or string concat */
		lc->started = 1;
		cur = scanner_getc(lc);
		if (cur == '|') {
			return scanner_token(lc, CONCATSTRING);
		} else {
			utf8_putchar(lc, cur); 
			return scanner_token(lc, '|');
		}
	}
	(void)sql_error( c, 3, "unexpected symbol (%c)", cur);
	return LEX_ERROR;
}

static int 
tokenize(mvc * c, int cur)
{
	struct scanner *lc = &c->scanner;
	while (1) {
		if (isspace(cur)) {
			if ((cur = skip_white_space(lc)) == EOF)
				return cur;
			continue;  /* try again */
		} else if (isdigit(cur)) {
			return number(c, cur);
		} else if (isalpha(cur) || cur == '_') {
			return keyword_or_ident(c, cur);
		} else if (ispunct(cur)) {
			return scanner_symbol(c, cur);
		}
		if (cur == EOF) {
			if (lc->mode == LINE_1 || !lc->started )
				return cur;
			return scanner_error(c, cur);
		}
		/* none of the above: error */
		return scanner_error(c, cur);
	}
}

static int
valid_ident(char *s, char *dst)
{
	int escaped = 0;
	int p = 0;
	
	if (*s != '_' && !(isascii((int) *s) && isalnum((int) *s)) && *s != ' ' &&
	    *s != '(' && *s != ')') 
		return 0;
	/* do unescaping in the loop */
	while (*s && (*s != '"' || escaped)) {
		if (*s == '\\') {
			escaped = !escaped;
			if (!escaped) {
				dst[p++] = *s;
			}
		} else if (*s == '"' && escaped) {
			escaped = 0;
			dst[p++] = *s;
		} else if (*s != '_' && !(isascii((int) *s) && isalnum((int) *s)) && *s != ' ' &&
			   *s != '(' && *s != ')') {
			return 0;
		} else {
			escaped = 0;
			dst[p++] = *s;
		}
		s++;
		if (p >= 1024)
			return 0;
	}
	dst[p] = '\0';
	return 1;
}

static inline int
sql_get_next_token(YYSTYPE *yylval, void *parm) {
	mvc *c = (mvc*)parm;
	struct scanner *lc = &c->scanner;
	int token = 0;

	if (lc->rs->buf == NULL) /* malloc failure */
		return EOF;

	if (lc->yynext) {
		int next = lc->yynext;

		lc->yynext = 0;
		return(next);
	}

	if (lc->yybak) {
		lc->rs->buf[lc->rs->pos + lc->yycur] = lc->yybak;
		lc->yybak = 0;
	}
	
	lc->yysval = lc->yycur;
	lc->yylast = lc->yyval;
	token = tokenize(c, scanner_getc(lc));

	yylval->sval = (lc->rs->buf + (int)lc->rs->pos + lc->yysval);

	/* This is needed as ALIAS and aTYPE get defined too late, see
	   sql_keyword.mx */
	if (token == KW_ALIAS)
		token = ALIAS;

	if (token == KW_TYPE)
		token = aTYPE;

	if (token == IDENT || token == COMPARISON || token == FILTER_FUNC || token == AGGR || token == AGGR2 || token == RANK || token == aTYPE || token == ALIAS)
		yylval->sval = sa_strndup(c->sa, yylval->sval, lc->yycur-lc->yysval);
	else if (token == STRING) {
		char quote = *yylval->sval;
		char *str = sa_alloc( c->sa, (lc->yycur-lc->yysval-2)*2 +1 );
		assert(quote == '"' || quote == '\'');

		lc->rs->buf[lc->rs->pos+lc->yycur- 1] = 0; 
		if (quote == '"') {
			/* Todo check if what valid 'quoted' idents are */
			if (valid_ident(yylval->sval+1,str)) {
				token = IDENT;
			} else {
				sql_error(c, 1, "Invalid identifier '%s'", yylval->sval+1);
				return LEX_ERROR;
			}
		} else {
			memcpy(str, yylval->sval+1, lc->yycur-lc->yysval - 1);
		}
		yylval->sval = str;

		/* reset original */
		lc->rs->buf[lc->rs->pos+lc->yycur- 1] = quote; 
	}

	return(token);
}

/* also see sql_parser.y */
extern int sqllex( YYSTYPE *yylval, void *m );

int
sqllex(YYSTYPE * yylval, void *parm)
{
	int token;
	mvc *c = (mvc *) parm;
	struct scanner *lc = &c->scanner;
	int pos;

	/* store position for when view's query ends */
	pos = (int)lc->rs->pos + lc->yycur;

	token = sql_get_next_token(yylval, parm);
	
	if (token == UNION) {
		int next = sqllex(yylval, parm);

		if (next == JOIN) {
			token = UNIONJOIN;
		} else {
			lc->yynext = next;
		}
	} else if (token == NO) {
		int next = sqllex(yylval, parm);

		switch (next) {
			case MAXVALUE:
				token = NOMAXVALUE;
			break;
			case MINVALUE:
				token = NOMINVALUE;
			break;
			case CYCLE:
				token = NOCYCLE;
			break;
			default:
				lc->yynext = next;
			break;
		}
	} else if (token == SCOLON) {
		/* ignore semi-colon(s) following a semi-colon */
		if (lc->yylast == SCOLON) {
			int prev = lc->yycur;
			while ((token = sql_get_next_token(yylval, parm)) == SCOLON)
				prev = lc->yycur;

			/* skip the skipped stuff also in the buffer */
			lc->rs->pos += prev;
			lc->yycur -= prev;
			assert(lc->yycur >= 0);
		}
	}

	if (lc->log) 
		mnstr_write(lc->log, lc->rs->buf+pos, lc->rs->pos + lc->yycur - pos, 1);

	/* Don't include literals in the calculation of the key */
	if (token != STRING && token != sqlINT && token != OIDNUM && token != INTNUM && token != APPROXNUM && token != sqlNULL)
		lc->key ^= token;
	lc->started += (token != EOF);
	return token;
}
