#ifndef ZIR_EMIT_H
#define ZIR_EMIT_H
#include "zir.h"

typedef enum ZirTarget { ZIR_C, ZIR_CPP, ZIR_GO, ZIR_JS } ZirTarget;
/* Resolve target symbol spelling and call ABI. Input contains identifiers and
 * captured argument names only, never a source expression to reinterpret. */
typedef void (*ZirResolveTarget)(void *context, const char *text, char *out, size_t size);
const char *TargetType(const char *type, ZirTarget target);
int ScalarLiteral(const char *type, const char *text, ZirTarget target,
                      ZirSourceSpan span, char *out, size_t size);
/* C/C++ private ABI names and source-shaped arguments for array values. */
int ArrayValueType(const char *type);
int ModuleUsesSlices(const ZirModule *module);
void ArrayAbiName(const ZirFunction *fn, int parameter, char *out, size_t size);
void ArrayAbiArgs(const ZirFunction *fn, char *out, size_t size);
int CanEmitBody(const ZirModule *module, const ZirFunction *fn);
/* Stream a portable record's zero value or deep copy; no output if unsupported. */
int EmitJsRecordValue(FILE *out, const ZirModule *module, const char *type,
                         const char *source);
void EmitNumbers(FILE *out, const ZirModule *module, ZirTarget target);
void EmitNumberSupport(FILE *out, ZirTarget target, const char *prefix);
void EmitStringType(FILE *out);
void EmitSlotWrappers(FILE *out, const ZirModule *module, const ZirFunction *fn,
                         ZirTarget target, ZirResolveTarget resolver, void *context);
void EmitSlotType(FILE *out, const ZirType *slot, ZirTarget target,
                     ZirResolveTarget resolve_type, void *context);
/* number_support overrides the module-local numeric helper prefix when a
 * package shares one definition. */
int EmitBody(FILE *out, const ZirModule *module, const ZirFunction *fn,
                ZirTarget target, ZirResolveTarget resolve, void *context,
                const char *number_support);
/* Dense-output switch for the Go target: remove the inlined-expression length
 * bound so single-use temporaries fold without a readability cap. Default off. */
void EmitUseMinifiedOutput(int enabled);
#endif
