#ifndef ZIR_EMIT_H
#define ZIR_EMIT_H
#include "zir.h"

typedef enum ZirTarget { ZIR_C, ZIR_CPP, ZIR_GO, ZIR_JS } ZirTarget;
/* Resolve target symbol spelling and call ABI. Input contains identifiers and
 * captured argument names only, never a source expression to reinterpret. */
typedef void (*ZirResolveTarget)(void *context, const char *text, char *out, size_t size);
const char *ZirTargetType(const char *type, ZirTarget target);
int ZirScalarLiteral(const char *type, const char *text, ZirTarget target,
                      ZirSourceSpan span, char *out, size_t size);
/* C/C++ private ABI names and source-shaped arguments for array values. */
int ZirArrayValueType(const char *type);
int ZirModuleUsesSlices(const ZirModule *module);
void ZirArrayAbiName(const ZirFunction *fn, int parameter, char *out, size_t size);
void ZirArrayAbiArgs(const ZirFunction *fn, char *out, size_t size);
int ZirCanEmitBody(const ZirModule *module, const ZirFunction *fn);
/* Stream a portable record's zero value or deep copy; no output if unsupported. */
int ZirEmitJsRecordValue(FILE *out, const ZirModule *module, const char *type,
                         const char *source);
void ZirEmitNumbers(FILE *out, const ZirModule *module, ZirTarget target);
void ZirEmitNumberSupport(FILE *out, ZirTarget target, const char *prefix);
void ZirEmitStringType(FILE *out);
void ZirEmitSlotWrappers(FILE *out, const ZirModule *module, const ZirFunction *fn,
                         ZirTarget target, ZirResolveTarget resolver, void *context);
void ZirEmitSlotType(FILE *out, const ZirType *slot, ZirTarget target,
                     ZirResolveTarget resolve_type, void *context);
/* instance_host is the native Go implementation receiver, or an empty string
 * for application functions and the other targets. number_support overrides
 * the module-local numeric helper prefix when a package shares one definition. */
int ZirEmitBody(FILE *out, const ZirModule *module, const ZirFunction *fn,
                ZirTarget target, ZirResolveTarget resolve, void *context,
                const char *instance_host, const char *number_support);
/* Dense-output switch for the Go target: remove the inlined-expression length
 * bound so single-use temporaries fold without a readability cap. Default off. */
void ZirEmitUseMinifiedOutput(int enabled);
#endif
