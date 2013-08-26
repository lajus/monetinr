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
 * Author M. Kersten, F.Groffen, E. Liarou, R. Goncalves
   The Sensor Simulation program

   The sensor tool can be used to simulate a simple sensor, such as an on/off sensor or
   a heat sensor. It is an independent tool, which runs on any platform where a C-compiler
   is available for compilation, and where the fire-wall settings permit connection to
   specific host:port channels.

   The sensor generator runs in two modes, active or passive. In the former case, the tool
   contacts the server and establishes an UDF channel to pass CSV encoded event strings.
   In the latter case, it is the datacell server that periodically contacts the sensor to
   deliver an event.
   Optionally, the event can be tagged with a serial key and a time-stamp.

   Events are generated using a built-in random number generator or a file.
   The latter can be used to re-play a scenario, precisely mimicking the time
   an event was raised.
   The events are sent over the channel with an optional delay, expressed in number of microseconds.

   To stress the system under various situations, the events can be sent in batches,
   where between each batch an optional delay is obeyed. The delay is specified in
   number of milliseconds. Setting the delay to zero results in an event stream burst.

   The sensor program provides a template to experiment with your own sensors.
   The structure has been set up to generate MonetDB tuple formatted events,
   comma-separated-input (CSV) and tab-separated-input (TSV). The protocol
   can readily be extended with SOAP and JSON based schemes.

   More esotheric solutions, e.g. to mimick real sensor systems, calls for
   development of a simple event constructor and serialization scheme.
   Moreover, in those cases a receptor should be constructed, which understands
   the event message protocol.
 */
#ifndef SENSOR
#define SENSOR
#include "monetdb_config.h"
#include <gdk.h>
#include <mapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifndef HAVE_GETOPT_LONG
#  include "monet_getopt.h"
#else
# ifdef HAVE_GETOPT_H
#  include "getopt.h"
#endif
#endif

#endif

#include "dcsocket.h"
/* #define SENSOR_DEBUG*/
#define SEout GDKout

FILE *fd;

typedef struct SENSORCLI {
	str name;
	stream *toServer;
	SOCKET newsockfd;
	struct SENSORCLI *nxt, *prev;
} SErecord, *Sensor;

static Sensor seAnchor = NULL;

static Sensor
SEnew(str nme)
{
	Sensor se;
	se = (Sensor) GDKzalloc(sizeof(SErecord));
	se->name = GDKstrdup(nme);
	se->nxt = seAnchor;
	seAnchor = se;
	return se;
}

/*
   Defaults:
   The default settings for the sensor simulator are single millisecond delay,
   sending each event in isolatio, e.g. no batching, using a single payload columns.
   The datacell is presumed to life on the same host and listens to the port 50500.
 */
static int delay = 1;   /* intra batch delay in ms, use 1 to avoid loosing too many */
static int batchsize = 1;
static int events = -1;
static int columns = 1;
static int autoincrement = 1;   /* if id != 0 then we increment and send it along */
static int timestamp = 1; /*if (timestamp!=0) sensor sends also the tsmp */
static int timecolumn = -1;		/* use first column for derivation of delay */
static char *host = "localhost";
static int port = 50500;
static int trace = 0;
static int replay = 0;
static char *sensor = "X";
static char *datafile= 0;
static int server = 0;


static void
usage(void)
{
	mnstr_printf(SEout, "The sensor tool can be used to generate a sequence of\n");
	mnstr_printf(SEout, "events and direct them to a receptor port at a DataCell.\n");
	mnstr_printf(SEout, "Several options are provided to control this process.\n");
	mnstr_printf(SEout, "\nsensor [options]\n");
	mnstr_printf(SEout, "--host=<host name>, default=localhost\n");
	mnstr_printf(SEout, "--port=<portnr>, default=50500 \n");
	mnstr_printf(SEout, "--sensor=<name> \n");
	mnstr_printf(SEout, "--protocol=<name> udp or tcp(default) or csv \n");
	mnstr_printf(SEout, "--increment=<number>, default=1 \n");
	mnstr_printf(SEout, "--timestamp, default=on\n");
	mnstr_printf(SEout, "--columns=<number>, default=1\n");
	mnstr_printf(SEout, "--events=<events length>, (-1=forever,>0), default=1\n");
	mnstr_printf(SEout, "--file=<data file> \n");
	mnstr_printf(SEout, "--replay use file or standard input\n");
	mnstr_printf(SEout, "--time=<column> where to find the exact time\n");
	mnstr_printf(SEout, "--batch=<batchsize> , default=1\n");
	mnstr_printf(SEout, "--delay=<ticks> interbatch delay in ms, default=1\n");
	mnstr_printf(SEout, "--trace=<trace> interaction\n");
	mnstr_printf(SEout, "--server run as a server\n");
	mnstr_printf(SEout, "--client run as a client\n");
}


#define TCP 1
#define UDP 2
#define CSV 3
#define TSV 4
#define DEB 5
static int protocol = TCP;
static char *protocolname[6] = { "<unknown>", "tcp", "udp", "csv", "tsv", "debug" };
static char *separator[6] = { "", ",", ",", ",", "\t", "," };

/*static Mapi dbh;
   static MapiHdl hdl = NULL;*/

#define die(dbh, hdl) (hdl ? mapi_explain_query(hdl, stderr) :		 \
					   dbh ? mapi_explain(dbh, stderr) :			\
					   fprintf(stderr, "command failed\n"),	 \
					   exit(-1))

#define doQ(X) \
	if ((hdl = mapi_query(dbh, X)) == NULL || mapi_error(dbh) != MOK) \
		die(dbh, hdl);

static void
stopSend(int i)
{
	(void) i;
	signal(i, SIG_IGN);
	exit(0);
}
static void produceStream(Sensor se);
static void produceServerStream(Sensor se);
static void produceDataStream(Sensor se);
static lng estimateOverhead(void);

int main(int argc, char **argv)
{
	MT_Id pid;
	int i, j = 0;
	char *err = NULL;
	char name[MYBUFSIZ + 1];
	char hostname[1024];
	Sensor se = NULL;
	static SOCKET sockfd;
	static struct option long_options[18] = {
		{ "increment", 0, 0, 'i' },
		{ "batch", 1, 0, 'b' },
		{ "columns", 1, 0, 'c' },
		{ "client", 0, 0, 'c' },
		{ "port", 1, 0, 'p' },
		{ "protocol", 1, 0, 'p' },
		{ "timestamp", 0, 0, 't' },
		{ "time", 1, 0, 't' },
		{ "events", 1, 0, 'e' },
		{ "sensor", 1, 0, 's' },
		{ "server", 0, 0, 's' },
		{ "replay", 0, 0, 'r' },
		{ "delay", 1, 0, 'd' },
		{ "file", 1, 0, 'f' },
		{ "host", 1, 0, 'h' },
		{ "trace", 0, 0, 't' },
		{ "help", 1, 0, '?' },
		{ 0, 0, 0, 0 }
	};
	THRdata[0] = (void *) file_wastream(stdout, "stdout");
	THRdata[1] = (void *) file_rastream(stdin, "stdin");
	for (i = 0; i < THREADS; i++) {
		GDKthreads[i].tid = i + 1;
	}
	for (;;) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "i:b:c:c:p:p:t:t:e:s:s:r:d:f:h:t:?:0",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'b':
			batchsize = optarg ? atol(optarg) : -1;
			if (batchsize <= 0) {
				mnstr_printf(SEout, "Illegal batch %d\n", batchsize);
				exit(0);
			}
			break;
		case 'c':
			if (strcmp(long_options[option_index].name, "client") == 0) {
				server = 0;
				break;
			}
			columns = optarg ? atol(optarg) : -1;
			if (columns <= 0) {
				mnstr_printf(SEout, "Illegal columns %d\n", columns);
				exit(0);
			}
			break;
		case 'd':
			delay = optarg ? atol(optarg) : -1;
			if (delay < 0) {
				mnstr_printf(SEout, "Illegal delay %d\n", delay);
				exit(0);
			}
			break;
		case 'i':
			autoincrement = optarg ? atol(optarg) : 0;
			if (autoincrement < 0) {
				mnstr_printf(SEout, "Illegal increment %d\n", autoincrement);
				exit(0);
			}
			break;
		case 't':
			if (strcmp(long_options[option_index].name, "timestamp") == 0) {
				timestamp = 1;
				break;
			}
			if (strcmp(long_options[option_index].name, "time") == 0) {
				timecolumn = optarg ? atol(optarg) : 0;
				break;
			}
			if (strcmp(long_options[option_index].name, "trace") == 0) {
				trace = optarg ? atol(optarg) : 1;
			} else {
				usage();
				exit(0);
			}
			break;
		case 'f':
			datafile = optarg && *optarg? optarg:0;
			break;
		case 'e':
			if (strcmp(long_options[option_index].name, "events") == 0) {
				events = optarg ? atol(optarg) : -1;
				if (events < -1) {
					mnstr_printf(SEout, "illegal events value, reset to -1\n");
					events = -1;
				} else if (events == 0) {
					mnstr_printf(SEout, "Illegal events value %d\n", events);
					exit(0);
				}
				break;
			} else {
				usage();
				exit(0);
			}
			break;
		case 'r':
			replay= 1;
			break;
		case 's':
			if (strcmp(long_options[option_index].name, "sensor") == 0) {
				sensor = optarg;
				break;
			}
			if (strcmp(long_options[option_index].name, "server") == 0) {
				server = 1;
				break;
			} else {
				usage();
				exit(0);
			}
			break;
		case 'p':
			if (strcmp(long_options[option_index].name, "protocol") == 0) {
				char *name= optarg? optarg: "xyz";
				if (strcmp(name, "TCP") == 0 || strcmp(name, "tcp") == 0) {
					protocol = TCP;
					break;
				}
				if (strcmp(name, "UDP") == 0 || strcmp(name, "udp") == 0) {
					protocol = UDP;
					break;
				}
				if (strcmp(name, "CSV") == 0 || strcmp(name, "csv") == 0) {
					protocol = CSV;
					break;
				}
				if (strcmp(name, "debug") == 0) {
					protocol = DEB;
					break;
				}
			}
			if (strcmp(long_options[option_index].name, "port") == 0) {
				port = optarg ? atol(optarg) : -1;
#ifdef SENSOR_DEBUG
				mnstr_printf(SEout, "#PORT : %d\n", port);
#endif
				break;
			} else {
				usage();
				exit(0);
			}
			break;
		case 'h':
			host = optarg;
			break;
		case '?':
		default:
			usage();
			exit(0);
		}
	}


	signal(SIGABRT, stopSend);
#ifdef SIGPIPE
	signal(SIGPIPE, stopSend);
#endif
#ifdef SIGHUP
	signal(SIGHUP, stopSend);
#endif
	signal(SIGTERM, stopSend);
	signal(SIGINT, stopSend);

	/* display properties */
	if (trace) {
		mnstr_printf(SEout, "--host=%s\n", host);
		mnstr_printf(SEout, "--port=%d\n", port);
		mnstr_printf(SEout, "--sensor=%s\n", sensor);
		mnstr_printf(SEout, "--columns=%d\n", columns);
		mnstr_printf(SEout, "--autoincrement=%d\n", autoincrement);
		mnstr_printf(SEout, "--timestamp=%d\n", timestamp);
		mnstr_printf(SEout, "--time=%d\n", timecolumn);
		mnstr_printf(SEout, "--events=%d\n", events);
		mnstr_printf(SEout, "--batch=%d\n", batchsize);
		mnstr_printf(SEout, "--replay=%d\n", replay);
		mnstr_printf(SEout, "--delay=%d\n", delay);
		mnstr_printf(SEout, "--protocol %s\n", protocolname[protocol]);
		mnstr_printf(SEout, "--trace=%d\n", trace);
		mnstr_printf(SEout, "--server=%d\n", server);
		mnstr_printf(SEout, "--client=%d\n", server);
		if (datafile)
			mnstr_printf(SEout, "--input=%s\n", datafile);
	}
	estimateOverhead();
	strncpy(hostname, host, 1024);
	if (strcmp(host, "localhost") == 0)
		gethostname(hostname, 1024);
	host = hostname;

	/*
	 * We limit the protocols for the time being to what can be
	 * considered a safe method.
	 */
	if (protocol == DEB) {
		/* save event stream in a file */
		Sensor se = SEnew(sensor);
		if (events == -1 || batchsize != 1) {
			printf("Provide an event limit using --events=<nr> and --batch=1\n");
			return 0;
		}
		if (datafile)
			se->toServer = open_wastream(datafile);
		else
			se->toServer = file_wastream(stdout, "stdout");
		produceStream(se);
	}
	if (protocol == UDP) {
		Sensor se = SEnew(sensor);
		se->toServer = udp_wastream(host, port, sensor);
		if (se->toServer == NULL) {
			perror("Sensor: Could not open stream");
			mnstr_printf(SEout, "#stream %s.%d.%s\n", host, port, sensor);
			return 0;
		}
		produceStream(se);
	}
	if (protocol == TCP) {
		if (server && (err = socket_server_connect(&sockfd, port))) {
			mnstr_printf(SEout, "#SENSOR:start server:%s\n", err);
			return 0;
		}
		do {
			int createThread = 0;
			snprintf(name, MYBUFSIZ - (strlen(sensor) + sizeof(j)), "%s%d", sensor, j++);

			se = SEnew(name);
			name[0] = 0;
			err = NULL;
			if (server) {
#ifdef SENSOR_DEBUG
				mnstr_printf(SEout, "#listen %s as server is %d \n", se->name, server);
#endif
				err = socket_server_listen(sockfd, &(se->newsockfd));
				if (err) {
					mnstr_printf(SEout, "#SENSOR:server listen:%s\n", err);
					break;
				}
			} else {
#ifdef SENSOR_DEBUG
				mnstr_printf(SEout, "#%s is client \n", se->name);
#endif
				err = socket_client_connect(&(se->newsockfd), host, port);
				if (err) {
					mnstr_printf(SEout, "#SENSOR:client start:%s\n", err);
					break;
				}
			}
			se->toServer = socket_wastream(se->newsockfd, se->name);
			if (se->toServer == NULL) {
				perror("Sensor: Could not open stream");
				mnstr_printf(SEout, "#stream %s.%d.%s\n", host, port, sensor);
				socket_close(se->newsockfd);
				return 0;
			}
			if (server) {
				createThread = MT_create_thread(&pid, (void (*)(void *))produceServerStream, se, MT_THR_DETACHED);
#ifdef SENSOR_DEBUG
				if (createThread)
					mnstr_printf(SEout, "#Create thread is : %d \n", createThread);
#else
				(void) createThread;
#endif
			} else { /* client part */
				produceServerStream(se);
			}
		} while (server);
		if (server)
			socket_close(sockfd);

		if (se)
			shutdown(se->newsockfd, SHUT_RDWR);
	}
	return 0;
}

#define L(X) (X + strlen(X))

static lng
estimateOverhead(void)
{
	int i;
	lng l;
	lng t0 = GDKusec();
	for (i = 0; i < 10000; i++)
		l = GDKusec();
	t0 = (GDKusec() - t0) / 10000;
#ifdef SENSOR_DEBUG
	mnstr_printf(SEout, "#Timing overhead " LLFMT " GDKusec\n", t0);
#endif
	return l;
}

static void
produceStream(Sensor se)
{
	int b;
	int slen;
	char buf[MYBUFSIZ + 1]; /* compose an event message locally first */
	char tuple[MYBUFSIZ + 1];  /* scratch area for a single tuple element */
	int tlen, maxtuple = MYBUFSIZ;
	int buflen;
	int numberOFtuples = 0;

#ifdef SENSOR_DEBUG
	mnstr_printf(SEout, "#Start producing the stream\n");
	mnstr_printf(SEout, "#%d events, batchsize is %d, columns are %d\n", events, batchsize, columns);
#endif

	/* create scratch space for a single tuple, this should be enough for integer fields */
	buflen = 0;
	while (numberOFtuples < (events * batchsize) || events == -1) {
		int i;
		lng currenttsmp = 0;

		if (delay > 0) {
			/* wait */
			MT_sleep_ms(delay);
		}

		if (delay < 0) {
			mnstr_printf(SEout, "#send next?");
			getchar();
		}

		buf[0] = 0;
		slen = 0;
		tlen = 0;
		if (batchsize > 1) {
			snprintf(tuple, maxtuple, "#%d\n", batchsize);
			tlen += (int) strlen(tuple + tlen);
			strncpy(buf, tuple, tlen);
			slen += tlen;
		}
		/* construct a single event record batch */
		for (b = batchsize; b > 0; b--) {
			/* the first column is used for event identifier tagging */
			tlen = 0;
			if (autoincrement) {
				snprintf(tuple + tlen, maxtuple - tlen, "%d", autoincrement);
				tlen += (int) strlen(tuple + tlen);
				autoincrement++;
			}
			/* if timestamp is set then the next colum will contain
			   the wall-clock microstamp. This reduces the number of
			   additional columns to be produced by 1 */
			if (timestamp) {
				currenttsmp = GDKusec();

				snprintf(tuple + tlen, maxtuple - tlen, "%s" LLFMT "", (autoincrement ? separator[protocol] : ""), currenttsmp);
				tlen += (int) strlen(tuple + tlen);
				if (tlen >= maxtuple) {
					mnstr_printf(SEout, "Buffer not large enough to handle request.\n");
					mnstr_printf(SEout, "recompile with larger constant \n");
					return;
				}
			}

			/* we only generate integer based events for now */
			for (i = (timestamp ? 1 : 0) + (autoincrement ? 1 : 0); i < columns; i++) {
				if (i)
					snprintf(tuple + tlen, maxtuple - tlen, "%s%d", separator[protocol], rand());
				else
					snprintf(tuple + tlen, maxtuple - tlen, "%d", rand());
				tlen += (int) strlen(tuple + tlen);
			}
			snprintf(tuple + tlen, maxtuple - tlen, "\n");
			tlen += (int) strlen(tuple + tlen);
			/* now add the tuple to the buffer if there is room left*/
			if (MYBUFSIZ - buflen <= tlen) {
				mnstr_printf(SEout, "Buffer not large enough to handle request.\n");
				mnstr_printf(SEout, "recompile with larger constant \n");
				return;
			}
			strncpy(buf + slen, tuple, tlen);
			slen += tlen;
			numberOFtuples++;
		}
		/* the batch has now been created, it should be shipped */
		/* watch out, the buffer is not NULL terminated */
		if (mnstr_write(se->toServer, buf, 1, slen) == -1 &&
			((errno == EPIPE) || (errno == ECONNRESET))) {
			mnstr_printf(SEout, "errno:%s %d\n", strerror(errno), errno);
			close_stream(se->toServer);
			se->toServer = NULL;
			return;
		}
		if (trace) {
			buf[slen] = 0;
			mnstr_printf(SEout, "%s", buf);
			/*mnstr_flush(SEout);*/
		}
	}
	/* you should not close the stream to quickly
	   because then you may loose part of the input */

	if (protocol != DEB) {
		mnstr_printf(SEout, "Columns: %d\n", columns);
		mnstr_printf(SEout, "Batch size: %d\n", batchsize);
		mnstr_printf(SEout, "total Number of batches: %d\n", events);
		mnstr_printf(SEout, "Delay: %d\n", delay);
	}
	mnstr_printf(SEout, "ready to close connection?");
	(void) getchar();
	close_stream(se->toServer);
	se->toServer = NULL;
}

static void
produceDataStream(Sensor se)
{
	char buf[MYBUFSIZ + 1], *tuple, *c, *d;
	FILE *fd;
	int i, snr;
	int multiply=1000, usec = 0; 
	time_t lasttime = 0, tm = 0;
	struct tm stm;

	/* read a events of messages from a file or standard input.
	   It is processed multiple times.
	   The header is the delay imposed */

	snr = 0;
	do {
		if ( datafile == 0){
			fd = stdin;
		} else {
			fd = fopen(datafile, "r");
			if (fd == NULL) {
				mnstr_printf(SEout, "Could not open file '%s'\n", datafile);
				close_stream(se->toServer);
				se->toServer = NULL;
				return;
			}
		}

		/* read the event requests and sent when the becomes */
		while (fgets(buf, MYBUFSIZ, fd) != 0) {
			int newdelay = delay;
			tuple = buf;

			if ( timecolumn >= 0 ) {
				/* calculate the difference with previous event */
				/* use a simplistic csv file format */
				c= buf;
				for ( i = timecolumn; i> 0; i--){
					if ( (d=strchr(c,(int)','))){
						c= d+1;
					}
				}
				/* convert time to epoch in seconds*/
				memset(&stm, 0, sizeof(struct tm));
				if ( c ){
					c = strptime(c,"%Y-%m-%d %H:%M:%S", &stm);
					tm = mktime(&stm);
					if ( *c == '.') {
						/* microseconds */
						usec = atoi(c+1);
						if ( usec)
							multiply = 1;
					}
				} 
				/* calculate time differential in seconds */
				if ( lasttime )
					newdelay = (int) difftime(tm,lasttime) * multiply + usec;
				lasttime = tm;
				if (trace && newdelay)
					mnstr_printf(SEout, "delayed %d\n", newdelay);
				MT_sleep_ms(newdelay);
			} else
			if (delay > 0) {
				/* wait */
				MT_sleep_ms(delay);
			}
			if (delay < 0) {
				mnstr_printf(SEout, "%s", tuple);
				mnstr_printf(SEout, "send it?");
				getchar();
			}
			if (trace)
				mnstr_printf(SEout, "%s", tuple);
			if ((mnstr_write(se->toServer, tuple, 1, strlen(tuple))) == -1 && (errno == EPIPE)) {
				mnstr_printf(SEout, "errno:%s\n", strerror(errno));
				return;
			}
		}
		fclose(fd);
		snr++;
	} while (snr != events);
}

static void
produceServerStream(Sensor se)
{
	if (replay)
		produceDataStream(se);
	else
		produceStream(se);
	if (se->toServer)
		close_stream(se->toServer);
	se->toServer = NULL;
}
