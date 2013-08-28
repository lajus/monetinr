
#include <R.h>
#include <Rdefines.h>
#include "gdk.h"

#ifndef RINTERFACE_
#define RINTERFACE_

typedef struct CHAINEDSEXP {
	SEXP val;
	struct CHAINEDSEXP *next;
} ChainedSEXP;

//void *access_error(const char *msg);

SEXP monetinR_wrapper(SEXP dbpath, SEXP debug);

str monetinR_init(const char *dbpath, int debug);
SEXP monetinR_stop(void);
SEXP monetinR_isRunning(void);
SEXP monetinR_batinUse(void);

//SEXP monetinR_sqlQuery(SEXP sqlq);
//SEXP monetinR_getValue(SEXP qid, SEXP column, SEXP row);
//SEXP monetinR_getIterator(SEXP qid);
//SEXP monetinR_getIteratorValue(SEXP iterator, SEXP column);

//SEXP monetinR_exit(void);

SEXP monetinR_dummy(void);

SEXP monetinR_executeQuery(SEXP query);
SEXP monetinR_explainQuery(SEXP query);
void destroyBat(SEXP);

#endif
