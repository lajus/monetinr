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
 * @f monet_options
 * @a N.J. Nes
 * @* A simple option handling library
 * @T
 * The monet server and clients make use of command line options and a (possibly)
 * shared config file. With this library a set (represented by set,setlen) of
 * options is created. An option is stored as name and value strings with a
 * special flag indicating the origin of the options, (builtin, system config
 * file, special config file or command line option).
 *
 */
#include "monetdb_config.h"
#include "monet_options.h"
#ifndef HAVE_GETOPT_LONG
#  include "monet_getopt.h"
#else
# ifdef HAVE_GETOPT_H
#  include "getopt.h"
# endif
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef HAVE_GETOPT_LONG
#  include "getopt.c"
#  include "getopt1.c"
#endif

#ifdef NATIVE_WIN32
#define getpid _getpid
#endif

/* these two are used of the set parameter passed into functions is NULL */
static int default_setlen = 0;
static opt *default_set = NULL;

static int
mo_default_set(opt **Set, int setlen)
{
	if (*Set == NULL) {
		if (default_set == NULL) {
			default_setlen = mo_builtin_settings(&default_set);
			default_setlen = mo_system_config(&default_set, default_setlen);
		}
		*Set = default_set;
		setlen = default_setlen;
	}
	return setlen;
}

void
mo_print_options(opt *set, int setlen)
{
	int i = 0;

	setlen = mo_default_set(&set, setlen);
	for (i = 0; i < setlen; i++) {
		if (set[i].kind == opt_builtin) {
			fprintf(stderr, "# builtin opt \t%s = %s\n", set[i].name, set[i].value);
		}
	}
	for (i = 0; i < setlen; i++) {
		if (set[i].kind == opt_config) {
			fprintf(stderr, "# config opt \t%s = %s\n", set[i].name, set[i].value);
		}
	}
	for (i = 0; i < setlen; i++) {
		if (set[i].kind == opt_cmdline) {
			fprintf(stderr, "# cmdline opt \t%s = %s\n", set[i].name, set[i].value);
		}
	}
}


char *
mo_find_option(opt *set, int setlen, const char *name)
{
	opt *o = NULL;
	int i;

	setlen = mo_default_set(&set, setlen);
	for (i = 0; i < setlen; i++) {
		if (strcmp(set[i].name, name) == 0)
			if (!o || o->kind < set[i].kind)
				o = set + i;
	}
	if (o)
		return o->value;
	return NULL;
}

static int
mo_config_file(opt **Set, int setlen, char *file)
{
	char buf[BUFSIZ];
	FILE *fd = NULL;
	opt *set;

	if (Set == NULL) {
		if (default_set == NULL) {
			set = NULL;
			setlen = mo_default_set(&set, 0);
		}
		Set = &default_set;
		setlen = default_setlen;
	}
	set = *Set;
	fd = fopen(file, "r");
	if (fd == NULL) {
		fprintf(stderr, "Could not open file %s\n", file);
		return setlen;
	}
	while (fgets(buf, BUFSIZ, fd) != NULL) {
		char *s, *t, *val;
		int quote;

		for (s = buf; *s && isspace((int) (unsigned char) *s); s++)
			;
		if (*s == '#')
			continue;	/* commentary */
		if (*s == 0)
			continue;	/* empty line */

		val = strchr(s, '=');
		if (val == NULL) {
			fprintf(stderr, "mo_config_file: syntax error in %s at %s\n", file, s);
			fclose(fd);
			exit(1);
		}
		*val = 0;

		for (t = s; *t && !isspace((int) (unsigned char) *t); t++)
			;
		*t = 0;

		/* skip any leading blanks in the value part */
		for (val++; *val && isspace((int) (unsigned char) *val); val++)
			;

		/* search to unquoted # */
		quote = 0;
		for (t = val; *t; t++) {
			if (*t == '"')
				quote = !quote;
			else if (!quote && *t == '#')
				break;
		}
		if (quote) {
			fprintf(stderr, "mo_config_file: wrong number of quotes in %s at %s\n", file, val);
			fclose(fd);
			exit(1);
		}
		/* remove trailing white space */
		while (isspace((int) (unsigned char) t[-1]))
			t--;
		*t++ = 0;

		/* treat value as empty if it consists only of white space */
		if (t <= val)
			val = t - 1;

		set = (opt *) realloc(set, (setlen + 1) * sizeof(opt));
		set[setlen].kind = opt_config;
		set[setlen].name = strdup(s);
		set[setlen].value = malloc((size_t) (t - val));
		for (t = val, s = set[setlen].value; *t; t++)
			if (*t != '"')
				*s++ = *t;
		*s = 0;
		setlen++;
	}
	(void) fclose(fd);
	*Set = set;
	return setlen;
}

int
mo_system_config(opt **Set, int setlen)
{
	char *cfg;

	if (Set == NULL) {
		if (default_set == NULL) {
			opt *set = NULL;

			setlen = mo_default_set(&set, 0);
		}
		Set = &default_set;
		setlen = default_setlen;
	}
	cfg = mo_find_option(*Set, setlen, "config");
	if (!cfg)
		return setlen;
	setlen = mo_config_file(Set, setlen, cfg);
	free(cfg);
	return setlen;
}


int
mo_builtin_settings(opt **Set)
{
	int i = 0;
	opt *set;
	char buf[BUFSIZ];

	if (Set == NULL)
		return 0;

#define N_OPTIONS	10	/*MUST MATCH # OPTIONS BELOW */
	set = malloc(sizeof(opt) * N_OPTIONS);
	if (set == NULL)
		return 0;

	set[i].kind = opt_builtin;
	set[i].name = strdup("gdk_dbpath");
	snprintf(buf, BUFSIZ, "%s%c%s%c%s%c%s",
		 LOCALSTATEDIR, DIR_SEP, "monetdb5", DIR_SEP, "dbfarm",
		 DIR_SEP, "demo");
	set[i].value = strdup(buf);
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("gdk_debug");
	set[i].value = strdup("0");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("gdk_vmtrim");
	set[i].value = strdup("yes");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("monet_prompt");
	set[i].value = strdup(">");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("monet_daemon");
	set[i].value = strdup("no");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("mapi_port");
	set[i].value = strdup("50000");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("mapi_open");
	set[i].value = strdup("false");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("mapi_autosense");
	set[i].value = strdup("false");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("sql_optimizer");
	set[i].value = strdup("default_pipe");
	i++;
	set[i].kind = opt_builtin;
	set[i].name = strdup("sql_debug");
	set[i].value = strdup("0");
	i++;

	assert(i == N_OPTIONS);
	*Set = set;
	return i;
}

int
mo_add_option(opt **Set, int setlen, opt_kind kind, const char *name, const char *value)
{
	opt *set;

	if (Set == NULL) {
		if (default_set == NULL) {
			set = NULL;
			setlen = mo_default_set(&set, 0);
		}
		Set = &default_set;
		setlen = default_setlen;
	}
	set = (opt *) realloc(*Set, (setlen + 1) * sizeof(opt));
	set[setlen].kind = kind;
	set[setlen].name = strdup(name);
	set[setlen].value = strdup(value);
	*Set = set;
	return setlen + 1;
}

void
mo_free_options(opt *set, int setlen)
{
	int i;

	if (set == NULL) {
		set = default_set;
		setlen = default_setlen;
		default_set = NULL;
		default_setlen = 0;
	}
	for (i = 0; i < setlen; i++) {
		if (set[i].name)
			free(set[i].name);
		if (set[i].value)
			free(set[i].value);
	}
	free(set);
}
