#ifndef ZIRAN_ZIR_VM_H
#define ZIRAN_ZIR_VM_H

#include "zir.h"

/* Initial portable scalar execution subset. Verification precedes execution. */
int VmVerify(const ZirProgram *program, const char *entry_module,
                const char *entry_function);
int VmRun(const ZirProgram *program, const char *entry_module,
             const char *entry_function, long long *result, int *has_result);

#endif
