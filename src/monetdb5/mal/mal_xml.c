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
 * @f mal_xml
 * @a M. Kersten
 * @+ XML interface
 * The primitives to manipulate XML objects and to prepare for XML output
 * is collected in this single file. It is expected to grow out into
 * all the primitives needed to support an XML front-end.
 */
#include "monetdb_config.h"
#include "mal_xml.h"
char *
xmlChr(str s)
{
	static char buf[BUFSIZ + 5];
	char *c = buf, *lim = buf + BUFSIZ;

	while (s && c < lim && *s) {
		switch (*s) {
		case '_':
			sprintf(c, "\\_");
			c += 2;
			break;
		case '$':
			sprintf(c, "\\$");
			c += 2;
			break;
		case '%':
			sprintf(c, "\\%%");
			c += 4;
			break;
		case '<':
			sprintf(c, "$&lt;$");
			c += 6;
			break;
		case '>':
			sprintf(c, "$&gt;$");
			c += 6;
			break;
		case '&':
			sprintf(c, "&amp;");
			c += 5;
			break;
		default:
			*c++ = *s;
		}
		s++;
	}
	*c = 0;
	return buf;
}

