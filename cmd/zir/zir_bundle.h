#ifndef ZIRAN_ZIR_BUNDLE_H
#define ZIRAN_ZIR_BUNDLE_H

#include "zir.h"

/* Experimental portable bundle version 1. The reader owns the returned IR. */
ZirProgram *BundleLink(const ZirProgram *program, const char *entry_module,
                       const char *entry_function);
int BundleWrite(FILE *out, const ZirProgram *program,
                   const char *entry_module, const char *entry_function);
ZirProgram *BundleRead(FILE *in, const char *path,
                          char *entry_module, size_t module_size,
                          char *entry_function, size_t function_size);

#endif
