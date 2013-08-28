
#include "monetdb_config.h"
#include "Rinterface.h"
#include <stdio.h>
#include <errno.h>
#include <string.h> /* strerror */
#include <locale.h>
#include <R.h>
#include <Rdefines.h>

#include <Rinternals.h>

#ifdef NEW
#undef NEW
#endif

#include "monet_options.h"
//#include "gdk.h"
#include "mal.h"
#include "mal_session.h"
#include "mal_import.h"
#include "mal_client.h"
#include "mal_function.h"
#include "mal_linker.h"
#include "mal_authorize.h"
#include "mal_scenario.h"
#include "sql_scenario.h"
#include "msabaoth.h"
#include "mutils.h"
#include "opt_pipes.h"
#include "leaked_data.h"
#include "stream.h"

#include <signal.h>

#if 0
#define RDEBUG
#endif

/* ----------------------------------- Dirty Macros ----------------------------------------*/

static ChainedINT **LB__;
static RResultPtr *LD__;
static void *HD__ = NULL;

// Macros to access leaked_data.h global variables from the dynamic library.
// If you access it statically, you may access not the same variables than ones edited by the MAL code.
#define LB (((LB__ = (ChainedINT **) dlsym(HD__, "leaked_bids")) == NULL) ? ((ChainedINT **)(access_error("load leaked_bids failed"))) : LB__)
#define LD (((LD__ = (RResultPtr *) dlsym(HD__, "leaked_data")) == NULL) ? *((RResultPtr *)(access_error("load leaked_data failed"))) : *LD__)
static void *
access_error(const char *msg)
{
	Rf_error(msg);
	return NULL;
}

static ChainedSEXP *preserved = NULL;
static ChainedSEXP *CSEXP_pushValue(SEXP val, ChainedSEXP *c) {
	ChainedSEXP *r = (ChainedSEXP *) malloc(sizeof(ChainedSEXP));
	r->val = val;
	r->next = c;
	return r;
}
static ChainedSEXP *CSEXP_free(ChainedSEXP *c) {
	if (c != NULL) {
		ChainedSEXP *next = c->next;
		R_ReleaseObject(c->val);
		free(c);
		return CSEXP_free(next);
	}
	return NULL;
}

SEXP monetinR_stop(void) {
	preserved = CSEXP_free(preserved);
	CINT_free(*LB);
	*LB = NULL;
	return ScalarLogical(1);
}

SEXP monetinR_isRunning(void) {
	return ScalarLogical(preserved != NULL);
}

SEXP monetinR_batinUse(void) {
	return ScalarLogical(preserved != NULL && leakedBatInUse(*LB));
}

static void executeOnExit(R_CFinalizer_t finalizer, void *finalizerArg) {
	SEXP extptr;
	R_PreserveObject(extptr = R_MakeExternalPtr(finalizerArg, R_NilValue, R_NilValue));
	preserved = CSEXP_pushValue(extptr, preserved);
	R_MakeWeakRefC(extptr, R_NilValue, finalizer, TRUE);
}

#define EXITFUN_def( destroyer ) static void (R_CFinalizer_from_ ## destroyer)(SEXP p) { destroyer(R_ExternalPtrAddr(p)); }
#define EXITFUN_fun( destroyer ) (R_CFinalizer_from_ ## destroyer)

EXITFUN_def(bstream_destroy)
EXITFUN_def(buffer_destroy)
EXITFUN_def(mR_destroyMsg)
EXITFUN_def(GDKfree)

static void
monetinR_on_exit(SEXP ptr) { (void) ptr; GDKexit(0); }

static void
mysighandler(int sigint) {
	system("pstack $(pgrep R) &> /export/scratch2/lajus/stack");
	system("gcore -o /export/scratch2/lajus/core $(pgrep R)");
	GDKfatal("SIGNAL %d (core dumped)\n", sigint);
	abort();
}

// Based on the mserver5 main()
str monetinR_init(const char *dbpath, int debug) {

	long_str buf;
	opt *set = NULL;
	int setlen = 0, listing = 0;
	str dbinit = NULL;
	str err = "OK";
	//char prmodpath[1024];
	//char *modpath = NULL;
	//char *binpath = NULL;

	if(preserved != NULL) Rf_error("Only one instance of monetinR at a time. Sorry, can't do better than that.");

#define N_OPTIONS	6	/*MUST MATCH # OPTIONS BELOW */
	set = malloc(sizeof(opt) * N_OPTIONS);
	if (set == NULL) {
		err = "Malloc of set failed"; return err;
	}
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("gdk_dbpath");
	set[setlen].value = strdup((str) dbpath);
	setlen++;
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("gdk_debug");
	snprintf(buf, sizeof(long_str) - 1, "%d", debug);
	set[setlen].value = strdup(buf);
	setlen++;
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("monet_daemon");
	set[setlen].value = strdup("yes");
	setlen++;
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("sql_optimizer");
	set[setlen].value = strdup("leaker_pipe");
	setlen++;
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("sql_debug");
	set[setlen].value = strdup("0");
	setlen++;
	set[setlen].kind = opt_builtin;
	set[setlen].name = strdup("gdk_single_user");
	set[setlen].value = strdup("yes");
	setlen++;

	assert(setlen == N_OPTIONS);

	if (!GDKinit(set, setlen)) {
		err = "GDKInit failed"; return err;
	}

	executeOnExit(monetinR_on_exit, NULL);
	R_ShowMessage("Powered by MonetDB 5\n");

	GDKsetenv("monet_mod_path", MONETINR_MOD_PATH);
	GDKsetenv("monet_prompt", "");


	/* configure sabaoth to use the right dbpath and active database */
	msab_dbpathinit(GDKgetenv("gdk_dbpath"));
	/* wipe out all cruft, if left over */
	if ((err = msab_wildRetreat()) != NULL) {
		/* just swallow the error */
		free(err);
	}
	/* From this point, the server should exit cleanly.  Discussion:
	 * even earlier?  Sabaoth here registers the server is starting up. */
	if ((err = msab_registerStarting()) != NULL) {
		/* throw the error at the user, but don't die */
		fprintf(stderr, "!%s\n", err);
		free(err);
	}

	{
		str lang = "mal";
		/* we inited mal before, so publish its existence */
		if ((err = msab_marchScenario(lang)) != NULL) {
			/* throw the error at the user, but don't die */
			fprintf(stderr, "!%s\n", err);
			free(err);
		}
	}

#ifdef RDEBUG
	printf("some stuff\n");
#endif

	{
		/* unlock the vault, first see if we can find the file which
		 * holds the secret */
		char secret[1024];
		char *secretp = secret;
		FILE *secretf;
		size_t len;

		if (GDKgetenv("monet_vault_key") == NULL) {
			/* use a default (hard coded, non safe) key */
			snprintf(secret, sizeof(secret), "%s", "Xas632jsi2whjds8");
		} else {
			if ((secretf = fopen(GDKgetenv("monet_vault_key"), "r")) == NULL) {
				snprintf(secret, sizeof(secret),
						"unable to open vault_key_file %s: %s",
						GDKgetenv("monet_vault_key"), strerror(errno));
				GDKfatal("%s", secret);
			}
			len = fread(secret, 1, sizeof(secret), secretf);
			secret[len] = '\0';
			len = strlen(secret); /* secret can contain null-bytes */
			if (len == 0) {
				snprintf(secret, sizeof(secret), "vault key has zero-length!");
				GDKfatal("%s", secret);
			} else if (len < 5) {
				fprintf(stderr, "#warning: your vault key is too short "
								"(" SZFMT "), enlarge your vault key!\n", len);
			}
			fclose(secretf);
		}
		if ((err = AUTHunlockVault(&secretp)) != MAL_SUCCEED) {
			GDKfatal("%s", err);
		}
	}
	/* make sure the authorisation BATs are loaded */
	if ((err = AUTHinitTables()) != MAL_SUCCEED) {
		GDKfatal("%s", err);
	}

#ifdef RDEBUG
	R_ShowMessage("vaultkey ok\n");
#endif

	loadCoreLibrary();
	mal_init();

#ifdef RDEBUG
	R_ShowMessage("mal_init ok\n");
#endif

	if (GDKgetenv("mal_listing"))
		sscanf(GDKgetenv("mal_listing"), "%d", &listing);

	MSinitClientPrg(mal_clients, "user", "main");

#ifdef RDEBUG
	R_ShowMessage("initClientPrg ok\n");
#endif

	if (dbinit == NULL)
		dbinit = GDKgetenv("dbinit");
	if (dbinit)
		callString(mal_clients, dbinit, listing);

#ifdef RDEBUG
	R_ShowMessage("callString dbinit ok\n");
#endif

	if ((err = compileOptimizer(mal_clients, "leaker_pipe")) != MAL_SUCCEED)
		return err;

#ifdef RDEBUG
	R_ShowMessage("compile optimizer ok\n");
#endif

	{
		buffer *b = buffer_create(1024);
		bstream *fdin = mal_clients->fdin = bstream_create(buffer_rastream(b, "Rinput"), 0);
		executeOnExit(EXITFUN_fun(buffer_destroy), b);
		executeOnExit(EXITFUN_fun(bstream_destroy), fdin);
	}
	callString(mal_clients, "include leak;\n", 0);
	callString(mal_clients, "sql.init();\n", 0);
	callString(mal_clients, "sql.start();\n", 0);

	// We call leak_init dynamically. Same problems than describe above.
	HD__ = getHandler("leak");
	if(HD__ == NULL)
		Rf_error("leak module not found");
	{
		int (*adr)();
		*(void **)(&adr) = dlsym(HD__, "leak_init");
		if(adr == NULL)
			Rf_error("leak_init function not found");
		if ((*adr)())
			Rf_error("leak_init failed");
		mal_clients->fdout = LD->msg;
		executeOnExit(EXITFUN_fun(mR_destroyMsg), LD->msg);
		executeOnExit(EXITFUN_fun(GDKfree), LD);
	}

#ifdef RDEBUG
	R_ShowMessage("customs callString ok\n");
#endif

	if ((err = msab_registerStarted()) != NULL) {
		/* throw the error at the user, but don't die */
		fprintf(stderr, "!%s\n", err);
		free(err);
	}

#ifdef HAVE_SIGACTION
	{

	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = mysighandler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGSEGV, &sigIntHandler, NULL);

	}
#endif

//	if ((err = SQLinitClient(mal_clients)) != NULL) {
//		fprintf(stderr, "!%s\n", err);
//	}
#ifdef RDEBUG
	R_ShowMessage("will return\n");
#endif
	return err;
}

SEXP monetinR_wrapper(SEXP dbpath, SEXP debug)
{
	//str err;
	//SEXP e;
	/*err =*/ monetinR_init(STRING_VALUE(dbpath), INTEGER_VALUE(debug));
	//PROTECT(e = allocVector(STRSXP, 1));
	//SET_STRING_ELT(e, 0, mkChar(err));
	//UNPROTECT(1);
	return ScalarInteger(0);
}
SEXP
monetinR_dummy(void)
{
//	SEXP result = NULL;
	str dummy = "EXPLAIN SELECT OPTIMIZER;\n";
	SQLstatementIntern(mal_clients, &dummy, "main", 1, 1);
//
//	PROTECT(result=allocVector(STRSXP, 1));
//	SET_STRING_ELT(result, 0, mkChar(0));
//	UNPROTECT(1);
//
	return ScalarInteger(0);
}

//void
//monetinR_varFinalizer(SEXP v)
//{
//	qid = ATTRIB(v, "id");
//	list_destroy(LD[qid].colums);
//	LD[qid].cleaned = 1;
//}

//void
//monetinR_end()
//{
//	int i;
//	for(int i = 0; i < LC; i++) {
//		if(!LD[i].cleaned)
//			list_destroy(LD[i].columns);
//	}
//	GDKfree(LD);
//	mal_exit();
//}



SEXP
monetinR_executeQuery(SEXP q)
{
	SEXP res;
	str query = strdup(STRING_VALUE(q));
	RResultPtr ld = LD;
	char *msg, *msgdup;

	SQLstatementIntern(mal_clients, &query, "main", 1, 1);
	msg = mR_getMsg(ld->msg);
	msgdup = strdup(msg);
	free(msg);
	switch(ld->type) {
	case LD_PROCESSING:
		UNPROTECT(3);
	case LD_ERROR:
		Rf_error("ERROR: %s", msgdup);
		break;
	case LD_MESSAGE:
		if (*msgdup) R_ShowMessage(msgdup);
		return ScalarInteger(0);
	case LD_RESULT:
		if (*msgdup) R_ShowMessage(msgdup);
		/* TODO: Bind name and tname in attributes */
		res = ld->value;
		UNPROTECT(3);
		return res;
	default:
		Rf_error("You should never see this message [%d]", ld->type);
	}
	return ScalarInteger(1); // never reached
}

SEXP
monetinR_explainQuery(SEXP q)
{
	str query = malloc(GDKMAXERRLEN);
	RResultPtr ld = LD;
	char *msg, *msgdup;
	if (!query)
		return ScalarInteger(1);
	sprintf(query, "EXPLAIN %s\n", STRING_VALUE(q));
	callString(mal_clients, query, 0);
	free(query);
	msg = mR_getMsg(ld->msg);
	msgdup = strdup(msg);
	free(msg);
	if (*msgdup) R_ShowMessage(msgdup);
	return ScalarInteger(0); // never reached
}
