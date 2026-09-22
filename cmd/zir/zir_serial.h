#ifndef ZIRAN_ZIR_SERIAL_H
#define ZIRAN_ZIR_SERIAL_H

#include "zir.h"

/* Versioned, pointer-free checked IR serialization. */
int ProgramWriteZir(const ZirProgram *program, FILE *out);
ZirProgram *ProgramReadZir(FILE *in, const char *path);
int PathIsZir(const char *path);
ZirProgram *ProgramLoad(const char *path, const char *root);
/* Check all inputs and reject saved IR that changes during semantic checking.
 * A NULL input_paths pointer marks every program as saved IR. */
int CheckCanonicalPrograms(ZirProgram **programs, int count, int strict,
                           const char *const *input_paths);

#endif
