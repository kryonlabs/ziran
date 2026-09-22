#ifndef ZIRAN_ZIR_SERIAL_H
#define ZIRAN_ZIR_SERIAL_H

#include "zir.h"

/* Versioned, pointer-free checked IR serialization. */
int ZirProgramWriteZir(const ZirProgram *program, FILE *out);
ZirProgram *ZirProgramReadZir(FILE *in, const char *path);
int ZirPathIsZir(const char *path);
ZirProgram *ZirProgramLoad(const char *path, const char *root);

#endif
