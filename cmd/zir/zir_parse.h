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

/* Recreate compiler-owned variant operations from validated saved cases.
 * Saved IR carries only slots for these operations, never executable bodies. */
int RegenerateVariantOperations(ZirModule *module);
int InstantiateGenericType(ZirModule *module, ZirType *instance,
                           const ZirType *generic);
int SubstituteGenericType(const char *source, char *output, size_t capacity,
                          char params[][ZIR_NAME_MAX],
                          char actual[][ZIR_NAME_MAX], int count);

#endif /* ZIRAN_ZIR_PARSE_H */
