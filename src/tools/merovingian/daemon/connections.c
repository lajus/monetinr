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
#include <stdio.h> /* fprintf */
#include <sys/types.h>
#include <sys/stat.h> /* stat */
#include <sys/wait.h> /* wait */
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h> /* strerror */
#include <errno.h>

#include <stream.h>
#include <stream_socket.h>

#include "merovingian.h"
#include "connections.h"


err
openConnectionTCP(int *ret, unsigned short port, FILE *log)
{
	struct sockaddr_in server;
	int sock = -1;
	socklen_t length = 0;
	int on = 1;
	int i = 0;
	struct hostent *hoste;
	char *host;
	char hostip[24];
#ifdef CONTROL_BINDADDR
	char bindaddr[512];   /* eligable for configuration */
#endif

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return(newErr("creation of stream socket failed: %s",
					strerror(errno)));

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof on);

	server.sin_family = AF_INET;
#ifdef CONTROL_BINDADDR
	gethostname(bindaddr, 512);
	hoste = gethostbyname(bindaddr);
	memcpy(&server.sin_addr.s_addr, *(hoste->h_addr_list),
			sizeof(server.sin_addr.s_addr));
#else
	server.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
	for (i = 0; i < 8; i++)
		server.sin_zero[i] = 0;
	length = (socklen_t) sizeof(server);

	server.sin_port = htons((unsigned short) ((port) & 0xFFFF));
	if (bind(sock, (SOCKPTR) &server, length) < 0) {
		return(newErr("binding to stream socket port %hu failed: %s",
				port, strerror(errno)));
	}

	if (getsockname(sock, (SOCKPTR) &server, &length) < 0)
		return(newErr("failed getting socket name: %s",
				strerror(errno)));
	hoste = gethostbyaddr(&server.sin_addr.s_addr, 4, server.sin_family);
	if (hoste == NULL) {
		snprintf(hostip, sizeof(hostip), "%u.%u.%u.%u",
				(unsigned) ((ntohl(server.sin_addr.s_addr) >> 24) & 0xff),
				(unsigned) ((ntohl(server.sin_addr.s_addr) >> 16) & 0xff),
				(unsigned) ((ntohl(server.sin_addr.s_addr) >> 8) & 0xff),
				(unsigned) (ntohl(server.sin_addr.s_addr) & 0xff));
		host = hostip;
	} else {
		host = hoste->h_name;
	}

	/* keep queue of 5 */
	listen(sock, 5);

	Mfprintf(log, "accepting connections on TCP socket %s:%hu\n", host, port);

	*ret = sock;
	return(NO_ERR);
}

err
openConnectionUDP(int *ret, unsigned short port)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sock = -1;

	char sport[10];
	char host[512];

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;      /* Allow IPv4 only (broadcasting) */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
	hints.ai_protocol = 0;          /* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	snprintf(sport, 10, "%hu", port);
	sock = getaddrinfo(NULL, sport, &hints, &result);
	if (sock != 0)
		return(newErr("failed getting address info: %s", gai_strerror(sock)));

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock == -1)
			continue;

		if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break; /* working */

		close(sock);
	}

	if (rp == NULL)
		return(newErr("binding to datagram socket port %hu failed: "
					"no available address", port));

	/* retrieve information from the socket */
	getnameinfo(rp->ai_addr, rp->ai_addrlen,
			host, sizeof(host),
			sport, sizeof(sport),
			NI_NUMERICSERV | NI_DGRAM);

	freeaddrinfo(result);

	Mfprintf(_mero_discout, "listening for UDP messages on %s:%s\n", host, sport);

	*ret = sock;
	return(NO_ERR);
}

err
openConnectionUNIX(int *ret, char *path, int mode, FILE *log)
{
	struct sockaddr_un server;
	int sock = -1;
	int omask;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		return(newErr("creation of UNIX stream socket failed: %s",
					strerror(errno)));

	memset(&server, 0, sizeof(struct sockaddr_un));
	server.sun_family = AF_UNIX;
	strncpy(server.sun_path, path, sizeof(server.sun_path) - 1);

	/* have to use umask to restrict permissions to avoid a race
	 * condition */
	omask = umask(mode);
	if (bind(sock, (SOCKPTR) &server, sizeof(struct sockaddr_un)) < 0) {
		umask(omask);
		return(newErr("binding to UNIX stream socket at %s failed: %s",
				path, strerror(errno)));
	}
	umask(omask);

	/* keep queue of 5 */
	listen(sock, 5);

	Mfprintf(log, "accepting connections on UNIX domain socket %s\n", path);

	*ret = sock;
	return(NO_ERR);
}

/* vim:set ts=4 sw=4 noexpandtab: */
