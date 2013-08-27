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

#ifndef _DISCOVERYRUNNER_H
#define _DISCOVERYRUNNER_H 1

#include <pthread.h>

#include <msabaoth.h>

void broadcast(char *msg);
void registerMessageTap(int fd);
void unregisterMessageTap(int fd);
void discoveryRunner(void *d);

typedef struct _remotedb {
	char *dbname;       /* remote database name */
	char *tag;          /* database tag, if any, default = "" */
	char *fullname;     /* dbname + tag */
	char *conn;         /* remote connection, use in redirect */
	int ttl;            /* time-to-live in seconds */
	struct _remotedb* next;
}* remotedb;

sabdb *getRemoteDB(char *database);

extern remotedb _mero_remotedbs;
extern pthread_mutex_t _mero_remotedb_lock;

#endif

/* vim:set ts=4 sw=4 noexpandtab: */
