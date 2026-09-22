#ifndef ZIR_EXPR_H
#define ZIR_EXPR_H
#include "zir.h"

/* Parse with token precedence; unsupported syntax is an explicit unknown node. */
int ParseExpr(ZirFunction *fn, const ZirModule *module, const char *text,
                 ZirSourceSpan span);
void StructureFunction(ZirFunction *fn, const ZirModule *module);
#endif
