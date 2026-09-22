#ifndef KIR_LAWS_H
#define KIR_LAWS_H
#include "kir.h"

/* Enforce named post-check Ziran IR invariants before backend lowering.
 * Violations carry stable law names in diagnostics. */
int KirCheckLaws(KirProgram **programs, int count);
#endif
