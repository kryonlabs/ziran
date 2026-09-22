#ifndef ZIRAN_ZIR_PARSE_H
#define ZIRAN_ZIR_PARSE_H

#include "zir.h"

/*
 * Parse a .zi source file into a ZirProgram. Returns a heap-allocated
 * program (caller frees with ProgramFree), or NULL on fatal error.
 * This is the shared Ziran frontend used by the IR and native backends.
 */
ZirProgram *parse_file(const char *path, const char *root);

/* Parse immutable embedded text with the same frontend and source spans. */
ZirProgram *parse_source_text(const char *path, const char *source);

#endif /* ZIRAN_ZIR_PARSE_H */
