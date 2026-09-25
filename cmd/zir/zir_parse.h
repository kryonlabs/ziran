#ifndef ZIRAN_ZIR_PARSE_H
#define ZIRAN_ZIR_PARSE_H

#include "zir.h"

/*
 * Parse a .zi source file into a ZirProgram. Returns a heap-allocated
 * program (caller frees with ProgramFree), or NULL on fatal error.
 * This is the shared Ziran frontend used by the IR and native backends.
 */
ZirProgram *parse_file(const char *path, const char *root);

/* Resolve already encountered imports before ordinary graph loading finishes.
 * A NULL condition requests every module import, for type inference. */
typedef int (*ZirCompileImportResolver)(void *context, ZirProgram *program,
                                       ZirModule *module,
                                       const char *source_path,
                                       const char *root,
                                       const char *condition);
ZirProgram *parse_file_with_imports(const char *path, const char *root,
                                    ZirCompileImportResolver resolver,
                                    void *context);

/* Parse immutable embedded text with the same frontend and source spans. */
ZirProgram *parse_source_text(const char *path, const char *source);

/* Evaluate the pure translation-time integer/boolean subset after imports
 * have been linked. The result is stored as a literal before IR emission. */
int EvaluateCompileExpression(const ZirModule *module, const char *source,
                              ZirSourceSpan span, long *value);
int EvaluateCompileLiteral(const ZirModule *module, const char *source,
                           ZirSourceSpan span, char *literal,
                           size_t literal_size);

/* Finish compile-time expressions whose conditions depend on linked imports.
 * With allow_deferred, unresolved conditions remain for a later pass. */
int LowerLinkedCompileExpressions(ZirModule *module, int allow_deferred);

int InstantiateGenericRecord(ZirType *instance, const ZirType *generic);
int SubstituteGenericType(const char *source, char *output, size_t capacity,
                          char params[][ZIR_NAME_MAX],
                          char actual[][ZIR_NAME_MAX], int count);

#endif /* ZIRAN_ZIR_PARSE_H */
