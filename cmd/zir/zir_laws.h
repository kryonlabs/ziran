#ifndef ZIR_LAWS_H
#define ZIR_LAWS_H
#include "zir.h"

/* Enforce named post-check Ziran IR invariants before backend lowering.
 * Violations carry stable law names in diagnostics. */
int CheckLaws(ZirProgram **programs, int count);
#endif
