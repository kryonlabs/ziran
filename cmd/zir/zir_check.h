#ifndef ZIR_CHECK_H
#define ZIR_CHECK_H
#include "zir.h"

/* Resolve scalar expression types and lexical bindings. Strict mode rejects
 * unresolved expressions instead of delegating them to target-language text. */
int CheckPrograms(ZirProgram **programs, int count, int strict);
int LinkImports(ZirProgram **programs, int count);
const char *ScalarType(const char *type);
#endif
