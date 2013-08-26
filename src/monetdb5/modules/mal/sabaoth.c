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
 * @f sabaoth
 * @a Fabian Groffen
 * @+ Disk-based local database administration
 * "Sophia starts creation, without her male half.  That causes a shade,
 * the chaos or the darkness.  The ruler of this darkness is called
 * Yaldabaoth.  He thinks he is the only god, which makes Sophia angry.
 * She shows herself as reflection in the water.  Later, Yaldabaoth's son,
 * Sabaoth, revolts against his father.  Sophia rewards him for that with a
 * place in the seventh heaven."
 *
 * Local servers within a dbfarm go largerly by themselves.  However,
 * multiple databases are a common good among the dinosaurs, and prove to
 * be useful for us mammals as well.  In particular Merovingian exploits
 * the multi-database nature to perform it's services.  The Sabaoth module
 * breaks with the tradition that a database goes by itself, alone.  It
 * provides means to query which local databases exist and to retrieve
 * properties of those.  This includes whether or not a database is
 * running, if it accepts connections and where, if it is under maintance,
 * etc.
 *
 * This module is a product of the Armada way of thinking, which nowadays
 * is best coined under "cloud".  While this module does not perform any
 * actions itself, let alone remote onces, it is a local building block for
 * components that do facilitate the cloud, such as Merovingian.
 */
#include "monetdb_config.h"
#include <mal.h>
#include <mal_exception.h>
#include <mal_sabaoth.h>	/* for the implementation of the functions */
#include "sabaoth.h"

str SABprelude(int *ret) {
	(void)ret;

	return(MAL_SUCCEED);
}

str SABepilogue(int *ret) {
	(void)ret;

	return(MAL_SUCCEED);
}

str SABmarchScenario(int *ret, str *lang){
	return SABAOTHmarchScenario(ret, lang);
}

str SABretreatScenario(int *ret, str *lang){
	return SABAOTHretreatScenario(ret, lang);
}

str SABmarchConnection(int *ret, str *host, int *port) {
	return SABAOTHmarchConnection(ret, host, port);
}

str SABgetLocalConnectionURI(str *ret) {
	str tmp, con;

	rethrow("sabaoth.getLocalConnectionURI", tmp,
			SABAOTHgetLocalConnection(&con));

	*ret = con;
	return(MAL_SUCCEED);
}

str SABgetLocalConnectionHost(str *ret) {
	str tmp, con, p;

	rethrow("sabaoth.getLocalConnectionHost", tmp,
			SABAOTHgetLocalConnection(&con));

	/* this happens if no connection is available */
	if (strcmp(con, (str)str_nil) == 0) {
		*ret = con;
		return(MAL_SUCCEED);
	}

	/* con looks like mapi:monetdb://hostname:port */
	/* do some poor man's parsing */
	tmp = con;
	if ((p = strchr(con, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	if ((con = strchr(p + 1, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	if ((p = strchr(con + 3, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	*p = '\0';

	*ret = GDKstrdup(con + 3);
	GDKfree(tmp);
	return(MAL_SUCCEED);
}

str SABgetLocalConnectionPort(int *ret) {
	str tmp, con, p;

	rethrow("sabaoth.getLocalConnectionHost", tmp,
			SABAOTHgetLocalConnection(&con));

	/* this happens if no connection is available */
	if (strcmp(con, (str)str_nil) == 0) {
		*ret = 0;
		GDKfree(con);
		return(MAL_SUCCEED);
	}

	/* con looks like mapi:monetdb://hostname:port */
	/* do some poor man's parsing */
	tmp = con;
	if ((p = strchr(con, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	if ((p = strchr(p + 1, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	if ((con = strchr(p + 1, ':')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	if ((p = strchr(con + 1, '/')) == NULL) {
		p = createException(MAL, "sabaoth.getLocalConnectionHost",
				"invalid local connection string: %s", tmp);
		GDKfree(tmp);
		return(p);
	}
	*p = '\0';

	*ret = atoi(con + 1);
	GDKfree(tmp);
	return(MAL_SUCCEED);
}
