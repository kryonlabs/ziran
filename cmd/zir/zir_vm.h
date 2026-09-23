#ifndef ZIRAN_ZIR_VM_H
#define ZIRAN_ZIR_VM_H

#include "zir.h"
#include "ziran_host.h"
#include <stdint.h>

/* Initial portable scalar execution subset. Verification precedes execution. */
int VmVerify(const ZirProgram *program, const char *entry_module,
                const char *entry_function);
int VmRun(const ZirProgram *program, const char *entry_module,
             const char *entry_function, long long *result, int *has_result);
int VmRunWithHost(const ZirProgram *program, const char *entry_module,
                  const char *entry_function, VmHostCall host, void *context,
                  long long *result, int *has_result);

typedef struct VmInstance VmInstance;
VmInstance *VmInstanceOpen(const ZirProgram *program,
                           const char *entry_module,
                           const char *entry_function,
                           VmHostCall host, void *context);
int VmInstanceRun(VmInstance *instance, long long *result, int *has_result);
void VmInstanceClose(VmInstance *instance);

#endif
