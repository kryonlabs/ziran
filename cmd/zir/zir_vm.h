#ifndef ZIRAN_ZIR_VM_H
#define ZIRAN_ZIR_VM_H

#include "zir.h"
#include <stdint.h>

typedef enum VmHostValueKind {
    VM_HOST_VOID,
    VM_HOST_INTEGER,
    VM_HOST_UNSIGNED,
    VM_HOST_REAL,
    VM_HOST_STRING
} VmHostValueKind;

typedef struct VmHostValue {
    VmHostValueKind kind;
    const char *type;
    int64_t integer;
    uint64_t bits;
    double real;
    const unsigned char *data;
    size_t length;
} VmHostValue;

/* Return nonzero only after writing a result of the declared return type.
 * String bytes returned by a host must remain valid until VmRunWithHost ends. */
typedef int (*VmHostCall)(void *context, const char *module,
                          const char *function, const VmHostValue *args,
                          int arg_count, VmHostValue *result);

/* Initial portable scalar execution subset. Verification precedes execution. */
int VmVerify(const ZirProgram *program, const char *entry_module,
                const char *entry_function);
int VmRun(const ZirProgram *program, const char *entry_module,
             const char *entry_function, long long *result, int *has_result);
int VmRunWithHost(const ZirProgram *program, const char *entry_module,
                  const char *entry_function, VmHostCall host, void *context,
                  long long *result, int *has_result);

#endif
