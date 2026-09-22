#ifndef ZIR_CLEANUP_H
#define ZIR_CLEANUP_H
#include "zir.h"

/* Lower lexical cleanup once, before backend emission. Returns zero on error. */
int ZirLowerCleanup(ZirFunction *fn);
#endif
