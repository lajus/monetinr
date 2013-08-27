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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "merovingian.h"
#include "discoveryrunner.h"
#include "peering.h"


void
peeringServerThread(void *d)
{
	int s = *(int *)d;
	int msock;
	ssize_t len;
	char data[1024];
	char *masquerade;
	int discreader[2];
	struct timeval tv;
	fd_set fds;

	/* start speaking the peering initialisation language, client tells
	 * what it wants, we reply
	 * ritual is as follows:
	 *   for a bi-directional tunnel over which all traffic is routed
	 *   (with masquerading of the discovery announcements such that
	 *   traffic from both networks is directed over the two border
	 *   hosts as advertised by the request and response:
	 * > tunnel host:port
	 * < tunnel myhost:myport
	 *   for one-sided proxying of traffic, where the network from the
	 *   client connects to the border host advertised in the response,
	 *   and the network from the server connects to each host from the
	 *   client's network individually (as typically in a NAT
	 *   situation):
	 * > proxy
	 * < proxy myhost:myport
	 *   for full connectable networks, were masquerading is not
	 *   necessary on any side and all hosts from the one network
	 *   directly connect any of the hosts from the other network:
	 * > direct
	 * < direct
	 * after this (on error, the server disconnects), the regular
	 * discovery protocol (HELO, ANNC, LEAV) is spoken on the line until
	 * disconnected by either party (typically a shutdown). */

	masquerade = NULL;
	len = read(s, data, sizeof(data));
	if (len > 0 && strncmp(data, "tunnel ", 7) == 0) {
		/* tunnel mode */
		masquerade = strdup(data + 7);
		snprintf(data, sizeof(data),
				"tunnel %s:%u\n", _mero_hostname,
				(unsigned int)getConfNum(_mero_props, "port"));
		if (write(s, data, strlen(data)) == -1) {
			close(s);
			return;
		}
	} else if (len > 0 && strcmp(data, "proxy") == 0) {
		/* proxy mode */
		snprintf(data, sizeof(data),
				"proxy %s:%u\n", _mero_hostname,
				(unsigned int)getConfNum(_mero_props, "port"));
		if (write(s, data, strlen(data)) == -1) {
			close(s);
			return;
		}
	} else if (len > 0 && strcmp(data, "direct") == 0) {
		/* direct mode */
		snprintf(data, sizeof(data), "direct\n");
		if (write(s, data, strlen(data)) == -1) {
			close(s);
			return;
		}
	} else {
		/* invalid, abort here */
		snprintf(data, sizeof(data), "invalid request\n");
		if (write(s, data, strlen(data)) == -1) {
			/* next thing we do is closing anyway, so just keep this
			 * condition to keep fortification warnings off */
		}
		close(s);
		return;
	}

	if (pipe(discreader) == -1) {
		/* bla error */
		close(s);
		return;
	}
	registerMessageTap(discreader[0]);

	/* now just forward and inject announce messages, doing the
	 * masquerading if necessary */
	while (_mero_keep_listening == 1) {
		FD_ZERO(&fds);
		FD_SET(s, &fds);
		FD_SET(discreader[1], &fds);
		msock = s > discreader[1] ? s : discreader[1];
		/* wait up to 5 seconds. */
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		len = select(msock + 1, &fds, NULL, NULL, &tv);
		/* nothing interesting has happened */
		if (len <= 0)
			continue;
		if (FD_ISSET(s, &fds)) {
			/* from client, forward to our network */
			if ((len = read(s, data, sizeof(data) - 1)) == -1)
				break;
			data[len] = '\0';
			/* FIXME: simple form, no masquerading */
			(void)masquerade;
			broadcast(data);
		} else if (FD_ISSET(discreader[1], &fds)) {
			/* from our network, forward to client */
			len = read(discreader[1], data, sizeof(data));
			/* FIXME: simple form, no masquerading */
			if (write(s, data, len) == -1)
				break;
		}
	}

	unregisterMessageTap(discreader[0]);
	close(discreader[0]);
	close(discreader[1]);
	close(s);
}

/* vim:set ts=4 sw=4 noexpandtab: */
