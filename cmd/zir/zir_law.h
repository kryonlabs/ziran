#ifndef ZIR_LAW_H
#define ZIR_LAW_H
#include "zir.h"
#include <stdio.h>
int EvaluateLaw(const ZirProgram *program, const ZirModule *module,
                const ZirLaw *law, char *detail, size_t size);
const char *LawStatusName(int status);
int CheckLawGates(ZirProgram **programs, int count);
void PrintLawResults(ZirProgram **programs, int count, FILE *out);
#endif
