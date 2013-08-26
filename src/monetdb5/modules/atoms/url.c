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
 *  M. Kersten
 *  Y. Zhang
 * The URL module
 * The URL module contains a collection of commands to manipulate
 * Uniform Resource Locators - a resource on the World Wide Web-
 * represented as a string in Monet. The URL can represent
 * anything from a file, a directory or a complete movie.
 * This module is geared towards manipulation of their name only.
 * A complementary module can be used to gain access.[IOgate]
 *
 * The URL syntax is specified in RFC2396, Uniform Resource Identifiers
 * (URI): Generic Syntax. The URL syntax is dependent upon the scheme.
 * In general, a URL has the form <scheme>:<scheme-specific-part>.
 * Thus, accepting a valid URL is a simple proccess, unless the scheme
 * is known and schema-specific syntax is checked (e.g., http or ftp
 * scheme). For the URL module implemented here, we assume some common
 * fields of the <scheme-specific-part> that are shared among different
 * schemes.
 *
 * The core of the extension involves several operators to extract
 * portions of the URLs for further manipulation. In particular,
 * the domain, the server, and the protocol, and the file extension
 * can be extracted without copying the complete URL from the heap
 * into a string variable first.
 *
 * The commands provided are based on the corresponding Java class.
 *
 * A future version should use a special atom, because this may save
 * considerable space. Alternatively, break the URL strings into
 * components and represent them with a bunch of BATs. An intermediate
 * step would be to refine the atom STR, then it would be possible to
 * redefine hashing.
 */

#include "monetdb_config.h"
#include "url.h"
#include "mal.h"
#include "mal_exception.h"

#if 0
static void getword(char *word, char *line, char stop);
static void plustospace(char *str);
#endif
static char x2c(char *what);

/* COMMAND "getAnchor": Extract an anchor (reference) from the URL
 * SIGNATURE: getAnchor(url) : str; */
static str
url_getAnchor(str *retval, /* put string: pointer to char here. */
		url Str1)          /* string: pointer to char. */
{
	str s, d;

	if (Str1 == 0)
		throw(ILLARG, "url.getAnchor", "url missing");
	s = strchr(Str1, '#');
	if (s == 0) 
		s= (str) str_nil;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getAnchor", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getBasename": Extract the base of the last file name of the URL,
 *                        thus, excluding the file extension.
 * SIGNATURE: getBasename(str) : str; */
static str
url_getBasename(str *retval, url t)
{
	str d = 0, s;

	if (t == 0)
		throw(ILLARG, "url.getBasename", "url missing");
	s = strrchr(t, '/');
	if (s)
		s++;
	else
		s = (str) str_nil;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getBasename", "Allocation failed");
	s = strchr(d, '.');
	if (s)
		*s = 0;
	*retval = d;
	return MAL_SUCCEED;
}

#if 0
/* COMMAND "getContent": Retrieve the file referenced
 * SIGNATURE: getContent(str) : str; */
static str
url_getContent(str *retval, /* put string: pointer to char here. */
		url Str1)           /* string: pointer to char. */
{
	/* TODO: getContent should not return a string */
	if (!Str1)
		throw(ILLARG, "url.getContent", "url missing");
	strcpy(*retval, "functions not implemented");
	return MAL_SUCCEED;
}
#endif

/* COMMAND "getContext": Extract the path context from the URL
 * SIGNATURE: getContext(str) : str; */
static str
url_getContext(str *retval, url Str1)
{
	const char *s;
	str d;

	if (Str1 == 0)
		throw(ILLARG, "url.getContext", "url missing");

	s = strstr(Str1, "://");
	if (s)
		s += 3;
	else
		s = Str1;

	s = strchr(s, '/');
	if (s == 0)
		s = str_nil;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getContext", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

#if 0
/* COMMAND "getDirectory": Extract the directory names from the URL
 * SIGNATURE: getDirectory(str) : bat[int,str]; */
static str
url_getDirectory(BAT **retval, /* put pointer to BAT[int,str] record here. */
		url t)
{
	static char buf[1024];
	char *s;
	int i = 0, k = 0;
	BAT *b = NULL;

	if (t == 0)
		throw(ILLARG, "url.getDirectory", "url missing");

	while (*t && *t != ':')
		t++;
	t++;
	if (*t != '/')
		goto getDir_done;
	t++;
	if (*t != '/')
		goto getDir_done;
	t++;
	while (*t && *t != '/')
		t++;
	b = BATnew(TYPE_int, TYPE_str, 40);
	if (b == 0)
		throw(MAL, "url.getDirectory", "could not create BAT");

	s = buf;
	for (t++; *t; t++) {
		if (*t == '/') {
			*s = 0;
			BUNins(b, &k, buf, FALSE);
			k++;
			s = buf;
			*s = 0;
			i = 0;
			continue;
		}
		*s++ = *t;
		if (i++ == 1023)
			throw(PARSE, "url.getDirectory","server name too long");
	}
getDir_done:
	BATrename(b,"dir_name");
	BATroles(b,"dir","name");
	BATmode(b,TRANSIENT);
	*retval= b;
	return MAL_SUCCEED;
}
#endif

/* COMMAND "getDomain": Extract the Internet domain from the URL
 * SIGNATURE: getDomain(str) : str; */
str
URLgetDomain(str *retval, str *u)
{
	static char buf[1024];
	char *b, *d, *s = buf;
	int i = 0;
	url t= *u;

	*retval = 0;
	s = (str)str_nil;
	if (t == 0)
		throw(ILLARG, "url.getDomain", "domain missing");
	while (*t && *t != ':')
		t++;
	t++;
	if (*t != '/')
		goto getDomain_done;
	t++;
	if (*t != '/')
		goto getDomain_done;
	t++;
	b = buf;
	d = 0;
	for (; *t && *t != '/'; t++) {
		if (*t == '.')
			d = b;
		if (*t == ':')
			break;
		*b++ = *t;
		if (i++ == 1023)
			throw(PARSE, "url.getDomain", "server name too long\n");
	}
	*b = 0;
	if (d)
		s = d + 1;
getDomain_done:
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getDomain", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getExtension": Extract the file extension of the URL
 * SIGNATURE: getExtension(str) : str; */
static str
url_getExtension(str *retval, url t)
{
	str d = 0, s;

	if (t == 0)
		throw(ILLARG, "url.getExtension", "url missing");
	s = strrchr(t, '/');
	if (s) {
		s++;
		s = strchr(s + 1, '.');
		if (s)
			s++;
		else
			s = (str) str_nil;
	} else
		s = (str) str_nil;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getExtension", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getFile": Extract the last file name of the URL
 * SIGNATURE: getFile(str) : str; */
static str
url_getFile(str *retval, url t)
{
	str d = 0, s;

	if (t == 0)
		throw(ILLARG, "url.getFile", "url missing");
	s = strrchr(t, '/');
	if (s)
		s++;
	else
		s = (str) str_nil;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getFile", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getHost": Extract the server identity from the URL */
/* SIGNATURE: getHost(str) : str; */
static str
url_getHost(str *retval, url t)
{
	static char buf[1024];
	char *b, *d, *s;
	int i = 0;

	s = (str)str_nil;
	if (t == 0)
		throw(ILLARG, "url.getHost", "url missing");
	while (*t && *t != ':')
		t++;
	t++;
	if (*t != '/')
		goto getHost_done;
	t++;
	if (*t != '/')
		goto getHost_done;
	t++;
	b = buf;
	s = buf;
	for (; *t && *t != '/'; t++) {
		*b++ = *t;
		if (i++ == 1023)
			throw(PARSE, "url.getHost", "server name too long");
	}
	*b = 0;
getHost_done:
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getHost", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getPort": Extract the port id from the URL
 * SIGNATURE: getPort(str) : str; */
static str
url_getPort(str *retval, url t)
{
	static char buf[1024];
	char *b, *d = 0, *s = buf;
	int i = 0;

	if (t == 0)
		throw(ILLARG, "url.getPort", "url missing");
	s = (str)str_nil;
	while (*t && *t != ':')
		t++;
	t++;
	if (*t != '/')
		goto getPort_done;
	t++;
	if (*t != '/')
		goto getPort_done;
	t++;
	b = buf;
	for (; *t && *t != '/'; t++) {
		if (*t == ':')
			d = b;
		*b++ = *t;
		if (i++ == 1023)
			throw(PARSE, "url.getPort", "server name too long");
	}
	*b = 0;
	if (d)
		s = d + 1;
	else
		s = (str)str_nil;
getPort_done:
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getPort", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getProtocol": Extract the protocol from the URL
 * SIGNATURE: getProtocol(str) : str; */
static str
url_getProtocol(str *retval, /* put string: pointer to char here. */
		url t)
{
	static char buf[1024];
	char *b, *d = 0;
	int i = 0;

	if (t == 0)
		throw(ILLARG, "url.getProtocol", "url missing");
	b = buf;
	for (; *t && *t != ':'; t++) {
		*b++ = *t;
		if (i++ == 1023)
			throw(PARSE, "url.getProtocol", "server name too long");
	}
	*b = 0;
	d = GDKstrdup(buf);
	if (d == NULL)
		throw(MAL, "url.getProtocol", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "getQuery": Extract the query part from the URL
 * SIGNATURE: getQuery(str) : str; */
static str
url_getQuery(str *retval, url Str1)
{
	char *s, *d;

	if (Str1 == 0)
		throw(ILLARG, "url.getQuery", "url missing");
	s = strchr(Str1, '?');
	if (s == 0)
		s= (str) str_nil;
	 else 
		s++;
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getQuery", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

#if 0
/* COMMAND "getQueryArg": Extract the argument mappings from the URL query
 * SIGNATURE: getQueryArg(str) : bat[str,str]; */
static str
url_getQueryArg(BAT **retval, url t)
{
	char query[1024];
	char val[1024];
	char name[1024];
	char *unescapedval;
	BAT *b;

	if (t == 0)
		throw(ILLARG, "url.getQueryArg", "url missing");
	if (unescape_str(&unescapedval, t) != MAL_SUCCEED)
		throw(MAL, "url.getQueryArg", "failure to unescape");

	t = strchr(unescapedval, '?');
	if (t == 0)
		throw(ILLARG, "url.getQueryArg", "variable missing");
	t++;

	b = BATnew(TYPE_str, TYPE_str, 40);
	if (b == 0)
		throw(MAL, "url.getQueryArg","could not create BAT");
	if (strlen(t) > 1023)
		throw(PARSE, "url.getQueryArg", "string too long");
	strcpy(query, t);

	for (; query[0] != '\0';) {
		getword(val, query, '&');
		plustospace(val);
		getword(name, val, '=');
		BUNins(b, name, val, FALSE);
	}
	BATrename(b,"dir_name");
	BATroles(b,"dir","name");
	BATmode(b,TRANSIENT);
	*retval= b;
	return MAL_SUCCEED;
}
#endif

/* COMMAND "getRobotURL": Extract the location of the robot control file
 * SIGNATURE: getRobotURL(str) : str; */
static str
url_getRobotURL(str *retval, /* put string: pointer to char here. */
		url t)               /* string: pointer to char. */
{
	static char buf[1024];
	char *b, *d, *s = buf;
	int i = 0;

	if (t == 0)
		throw(ILLARG, "url.getRobotURL", "url missing");
	b = buf;
	while (*t && *t != ':')
		*b++ = *t++;
	*b++ = *t++;
	if (*t != '/')
		goto getRobot_done;
	*b++ = *t++;
	if (*t != '/')
		goto getRobot_done;
	*b++ = *t++;
	for (; *t && *t != '/'; t++) {
		*b++ = *t;
		if (i++ == 1000)
			throw(PARSE, "url.getRobotURL", "server name too long");
	}
	strcpy(b, "/robots.txt");
getRobot_done:
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getRobotURL", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
#if 0 /* not reached */
	if (i > 1000)
		s = (str)str_nil;
	else
		strcpy(b, "/robots.txt");
#endif
}


/* COMMAND "getUser": Extract the user identity from the URL
 * SIGNATURE: getUser(str) : str; */
static str
url_getUser(str *retval, url t)
{
	static char buf[1024];
	char *b, *d = 0, *s;
	int i = 0;

	if (t == 0)
		throw(ILLARG, "url.getUser", "url missing");
	s = (str)str_nil;
	while (*t && *t != ':')
		t++;
	if (*t == 0)
		goto getUser_done;
	t++;
	if (*t != '/')
		goto getUser_done;
	t++;
	if (*t != '/')
		goto getUser_done;
	t++;
	for (; *t && *t != '/'; t++)
		;
	if (*t == 0)
		goto getUser_done;
	t++;
	if (*t == '~') {
		t++;
		b = buf;
		s = buf;
		for (; *t && *t != '/'; t++) {
			*b++ = *t;
			if (i++ == 1023)
				throw(PARSE, "url.getUser", "server name too long");
		}
		*b = 0;
	}
getUser_done:
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.getUser", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "isaURL": Check conformity of the URL syntax
 * SIGNATURE: isaURL(str) : bit; */
static str
url_isaURL(bit *retval, /* put return atom here. */
		url t)          /* string: pointer to char. */
{
	if (t == 0)
		throw(ILLARG, "url.isaURL", "url missing");

	while (*t && *t != ':')
		t++;
	if (*t == ':')
		*retval = TRUE;
	else
		*retval = FALSE;

	return MAL_SUCCEED;
}

static int needEscape(char c){
	if( isalnum((int)c) )
		return 0;
	if( c == '#' || c == '-' || c == '_' || c == '.' || c == '!' ||
		c == '~' || c == '*' || c == '\'' || c == '(' || c == ')' )
		return 0;
	return 1;
}

/* COMMAND "escape": this function applies the URI escaping rules defined in
 * section 2 of [RFC 3986] to the string supplied as 's'.
 * The effect of the function is to escape a set of identified characters in
 * the string. Each such character is replaced in the string by an escape
 * sequence, which is formed by encoding the character as a sequence of octets
 * in UTF-8, and then reprensenting each of these octets in the form %HH.
 *
 * All characters are escaped other than:
 * [a-z], [A-Z], [0-9], "#", "-", "_", ".", "!", "~", "*", "'", "(", ")"
 *
 * This function must always generate hexadecimal values using the upper-case
 * letters A-F.
 *
 * SIGNATURE: escape(str) : str; */
str
escape_str(str *retval, str s)
{
	int x, y;
	str res;

	if (!s)
		throw(ILLARG, "url.escape", "url missing");

	if (!( res = (str) GDKmalloc( strlen(s) * 3 ) ))
		throw(MAL, "url.escape", "malloc failed");
	for (x = 0, y = 0; s[x]; ++x, ++y) {
		if (needEscape(s[x])) {
			if (s[x] == ' ') {
				res[y] = '+';
			} else {
				sprintf(res+y, "%%%2x", s[x]);
				y += 2;
			}
		} else {
			res[y] = s[x];
		}
	}
	res[y] = '\0';

	*retval = GDKrealloc(res, strlen(res)+1);
	return MAL_SUCCEED;
}

/* COMMAND "unescape": Convert hexadecimal representations to ASCII characters.
 *                     All sequences of the form "% HEX HEX" are unescaped.
 * SIGNATURE: unescape(str) : str; */
str
unescape_str(str *retval, str s)
{
	int x, y;
	str res;

	if (!s)
		throw(ILLARG, "url.escape", "url missing");

	res = (str) GDKmalloc(strlen(s));
	if (!res)
		throw(MAL, "url.unescape", "malloc failed");

	for (x = 0, y = 0; s[x]; ++x, ++y) {
		if (s[x] == '%') {
			res[y] = x2c(&s[x + 1]);
			x += 2;
		} else {
			res[y] = s[x];
		}
	}
	res[y] = '\0';

	*retval = GDKrealloc(res, strlen(res)+1);
	return MAL_SUCCEED;
}

#if 0
/* COMMAND "newurl": Construct a URL from protocol, host,and file
 * SIGNATURE: newurl(str, str, str) : str; */
str
url_new3(str *retval,   /* put string: pointer to char here. */
		str Protocol,   /* string: pointer to char. */
		str Server,     /* string: pointer to char. */
		str File)       /* string: pointer to char. */
{
	char buf[1024];
	str d, s = buf;

	if (strlen(File) + strlen(Server) + strlen(Protocol) > 1000)
		s = (str) str_nil;
	else
		sprintf(buf, "%s://%s/%s", Protocol, Server, File);
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.newurl", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}

/* COMMAND "newurl": Construct a URL from protocol, host,port,and file
 * SIGNATURE: newurl(str, str, int, str) : str; */
str
url_new4(str *retval,   /* put string: pointer to char here. */
		str Protocol,   /* string: pointer to char. */
		str Server,     /* string: pointer to char. */
		int *Port,      /* pointer to integer. */
		str File)       /* string: pointer to char. */
{
	char buf[1024];
	str d, s = buf;

	if (strlen(File) + strlen(Server) + strlen(Protocol) > 1000)
		s = (str) str_nil;
	else
		sprintf(buf, "%s://%s:%d/%s", Protocol, Server, *Port, File);
	d = GDKstrdup(s);
	if (d == NULL)
		throw(MAL, "url.newurl", "Allocation failed");
	*retval = d;
	return MAL_SUCCEED;
}
#endif

/*
 * Utilities
 */

#define LF 10
#define CR 13

#if 0
static void
getword(char *word, char *line, char stop)
{
	int x = 0, y;

	for (x = 0; ((line[x]) && (line[x] != stop)); x++)
		word[x] = line[x];

	word[x] = '\0';
	if (line[x])
		++x;
	y = 0;

	while ((line[y++] = line[x++]) != 0)
		;
}

char *
makeword(char *line, char stop)
{
	int x = 0, y;
	char *word = (char *) malloc(sizeof(char) * (strlen(line) + 1));

	for (x = 0; ((line[x]) && (line[x] != stop)); x++)
		word[x] = line[x];

	word[x] = '\0';
	if (line[x])
		++x;
	y = 0;

	while ((line[y++] = line[x++]) != 0)
		;
	return word;
}
#endif

static char
x2c(char *what)
{
	char digit;

	digit = (what[0] >= 'A' ? ((what[0] & 0xdf) - 'A') + 10 : (what[0] - '0'));
	digit *= 16;
	digit += (what[1] >= 'A' ? ((what[1] & 0xdf) - 'A') + 10 : (what[1] - '0'));
	return (digit);
}

#if 0
static void
plustospace(char *str)
{
	int x;

	for (x = 0; str[x]; x++)
		if (str[x] == '+')
			str[x] = ' ';
}
#endif


/*
 * Wrapping
 * Here you find the wrappers around the V4 url library included above.
 */

int
URLfromString(str src, int *len, str *url)
{
	/* actually parse the message for valid url */
	if (*url !=0)
		GDKfree(*url);

	*len = (int) strlen(src);
	*url = GDKstrdup(src);

	return *len;
}

int
URLtoString(str *s, int *len, str src)
{
	int l;

	if (GDK_STRNIL(src)) {
		*s = GDKstrdup("nil");
		return 0;
	}
	l = (int) strlen(src) + 3;
	/* if( !*s) *s= (str)GDKmalloc(*len = l); */

	if (l >= *len) {
		GDKfree(*s);
		*s = (str) GDKmalloc(l);
		if (*s == NULL)
			return 0;
	}
	snprintf(*s, l, "\"%s\"", src);
	*len = l - 1;
	return *len;
}

str
URLgetAnchor(str *retval, str *val)
{
	return url_getAnchor(retval, *val);
}

str
URLgetBasename(str *retval, str *t)
{
	return url_getBasename(retval, *t);
}

str
URLgetContent(str *retval, str *Str1)
{
	stream *f;
	str retbuf = NULL;
	str oldbuf = NULL;
	char *buf[8096];
	size_t len;
	size_t rlen;

	if ((f = open_urlstream(*Str1)) == NULL)
		throw(MAL, "url.getContent", "failed to open urlstream");

	if (mnstr_errnr(f) != 0) {
		str err = createException(MAL, "url.getContent",
				"opening stream failed: %s", mnstr_error(f));
		mnstr_destroy(f);
		*retval = NULL;
		return err;
	}

	rlen = 0;
	while ((len = mnstr_read(f, buf, 1, sizeof(buf))) != 0) {
		if (retbuf != NULL) {
			oldbuf = retbuf;
			retbuf = GDKrealloc(retbuf, rlen + len + 1);
		} else {
			retbuf = GDKmalloc(len + 1);
		}
		if (retbuf == NULL) {
			if (oldbuf != NULL)
				GDKfree(oldbuf);
			mnstr_destroy(f);
			throw(MAL, "url.getContent", "contents too large");
		}
		oldbuf = NULL;
		(void)memcpy(retbuf + rlen, buf, len);
		rlen += len;
	}
	retbuf[rlen] = '\0';

	*retval = retbuf;
	return MAL_SUCCEED;
}

str
URLgetContext(str *retval, str *val)
{
	return url_getContext(retval, *val);
}

str
URLgetExtension(str *retval, str *tv)
{
	return url_getExtension(retval, *tv);
}

str
URLgetFile(str *retval, str *tv)
{
	return url_getFile(retval, *tv);
}

str
URLgetHost(str *retval, str *tv)
{
	return url_getHost(retval, *tv);
}

str
URLgetPort(str *retval, str *tv)
{
	return url_getPort(retval, *tv);
}

str
URLgetProtocol(str *retval, str *tv)
{
	return url_getProtocol(retval, *tv);
}

str
URLgetQuery(str *retval, str *tv)
{
	return url_getQuery(retval, *tv);
}

str
URLgetRobotURL(str *retval, str *tv)
{
	return url_getRobotURL(retval, *tv);
}


str
URLgetUser(str *retval, str *tv)
{
	return url_getUser(retval, *tv);
}

str
URLisaURL(bit *retval, str *tv)
{
	return url_isaURL(retval, *tv);
}

str
URLnew(str *url, str *val)
{
	(void) url; /* fool compiler */
	*url = GDKstrdup(*val);

	return MAL_SUCCEED;
}

str
URLnew3(str *url, str *protocol, str *server, str *file)
{
	int l, i;

	l = (int) (GDK_STRLEN(*file) + GDK_STRLEN(*server) +
			GDK_STRLEN(*protocol) + 10);
	*url = (str) GDKmalloc(l);
	if (*url == NULL)
		throw(MAL, "url.newurl", "Allocation failed");
	snprintf(*url, l, "%s://", *protocol);
	i = (int) strlen(*url);
	snprintf(*url +i, l - i, "%s", *server);
	i = (int) strlen(*url);
	snprintf(*url +i, l - i, "/%s", *file);

	return MAL_SUCCEED;
}

str
URLnew4(str *url, str *protocol, str *server, int *port, str *file)
{
	str Protocol = *protocol;
	str Server = *server;
	str File = *file;
	int l, i;

	if (GDK_STRNIL(File))
		File = "";
	if (GDK_STRNIL(Server))
		Server = "";
	if (GDK_STRNIL(Protocol))
		Protocol = "";
	l = (int) (strlen(File) + strlen(Server) + strlen(Protocol) + 20);
	*url = (str) GDKmalloc(l);
	if (*url == NULL)
		throw(MAL, "url.newurl", "Allocation failed");
	snprintf(*url, l, "%s://", Protocol);
	i = (int) strlen(*url);
	snprintf(*url +i, l - i, "%s", Server);
	i = (int) strlen(*url);
	snprintf(*url +i, l - i, ":%d", *port);
	i = (int) strlen(*url);
	snprintf(*url +i, l - i, "/%s", File);
	return MAL_SUCCEED;
}

str URLnoop(str *url, str *val)
{
	*url = GDKstrdup(*val);
	return MAL_SUCCEED;
}
