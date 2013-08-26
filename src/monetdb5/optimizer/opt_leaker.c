#include "monetdb_config.h"
#include "opt_leaker.h"
#include "mal_instruction.h"


int
OPTleakerImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i, j, actions = 0;
	//bte one = 1;

	InstrPtr *old;
	int limit;

	(void) cntxt;
	(void) stk;

	for (i = 1; i < mb->stop; i++) {
		p = mb->stmt[i];
		if (getModuleId(p) == ioRef) {
			removeInstruction(mb, p);
			actions++;
		}
	}

	limit = mb->stop;
	old = mb->stmt;

	for (i = 1; i < limit; i++) {
		p = old[i];
		if (getModuleId(p) == sqlRef) {
			if (getFunctionId(p) == rsColumnRef) {
				setModuleId(p, leakRef);
				setFunctionId(p, addColumnRef);

				for(j = 2; j < p->argc; j++) p->argv[j-1] = p->argv[j];
				p->argc--;
				actions++;
			}
			else if (getFunctionId(p) == getName("exportResult",12)) {
				setModuleId(p, leakRef);
				setFunctionId(p, sealRef);
				p->argc = 1;
				actions++;
			}
			else if (getFunctionId(p) == getName("exportValue",11)) {
				setModuleId(p, leakRef);
				setFunctionId(p, leakValueRef);
				p->argv[p->argc-3] = p->argv[p->argc-2];
				for(j = 2; j < p->argc; j++) p->argv[j-1] = p->argv[j];
				p->argc -= 3;
				actions++;
			}
			else if (getFunctionId(p) == resultSetRef) {
//				int c = 0;
//				for (j = i; j < limit; j++) {
//					if (getModuleId(p) == sqlRef && getFunctionId(p) == rsColumnRef)
//						c++;
//				}
				setModuleId(p, leakRef);
				setFunctionId(p, rsRef);
				getArg(p,0) = newTmpVariable(mb, TYPE_void);
				p->argc = 2;
				actions++;
			}
		}
	}
	return actions;
}
