/*
 * Shared helpers for resolving .kry #style imports to KSS source.
 */
#ifndef ZIRAN_ZIR_STYLE_IMPORTS_H
#define ZIRAN_ZIR_STYLE_IMPORTS_H

#include "zir.h"

int ZirStyleImportIsBuiltIn(const char *target);
char *ZirReadStyleImportSource(const ZirModule *module, const char *root,
                               const ZirStyleImport *style);

#endif
