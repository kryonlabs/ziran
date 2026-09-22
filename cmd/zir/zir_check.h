#ifndef ZIR_CHECK_H
#define ZIR_CHECK_H
#include "zir.h"

/* Resolve scalar expression types and lexical bindings. Strict mode rejects
 * unresolved expressions instead of delegating them to target-language text. */
int ZirCheckPrograms(ZirProgram **programs, int count, int strict);
const char *ZirScalarType(const char *type);
#endif
