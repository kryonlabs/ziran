#ifndef ZIRAN_HOST_H
#define ZIRAN_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Bundle Bundle;
typedef struct BundleInstance BundleInstance;

typedef enum VmHostValueKind {
    VM_HOST_VOID,
    VM_HOST_INTEGER,
    VM_HOST_UNSIGNED,
    VM_HOST_REAL,
    VM_HOST_STRING,
    VM_HOST_RECORD,
    VM_HOST_SLICE
} VmHostValueKind;

typedef struct VmHostField VmHostField;

typedef struct VmHostValue {
    VmHostValueKind kind;
    const char *type;
    int64_t integer;
    uint64_t bits;
    double real;
    const unsigned char *data;
    size_t length;
    const VmHostField *fields;
    size_t field_count;
    struct VmHostValue *elements;
} VmHostValue;

struct VmHostField {
    const char *name;
    VmHostValue value;
};

/* Return nonzero after writing a result of the declared return type.
 * Record fields are in declaration order and carry their declared names and
 * types. Argument fields are borrowed for the call. A scalar or string slice
 * argument has kind VM_HOST_SLICE, length elements, and a mutable elements array.
 * Modify elements in place and keep the elements pointer and length intact;
 * the VM validates and copies every element back after the call. Returned
 * fields and string bytes must remain valid until BundleRun returns. */
typedef int (*VmHostCall)(void *context, const char *module,
                          const char *function, const VmHostValue *args,
                          int arg_count, VmHostValue *result);

typedef struct HostBinding {
    const char *module;
    const char *function;
    VmHostCall call;
    void *context;
} HostBinding;

/* Open and validate a version 2 portable bundle. Close releases all names. */
Bundle *BundleOpen(const char *path);
void BundleClose(Bundle *bundle);
size_t BundleCapabilityCount(const Bundle *bundle);
const char *BundleCapabilityModule(const Bundle *bundle, size_t index);
const char *BundleCapabilityFunction(const Bundle *bundle, size_t index);

/* Every required capability must have exactly one binding before execution.
 * Integer/bool entry results are returned in result; void sets has_result=0. */
int BundleRun(const Bundle *bundle, const HostBinding *bindings,
              size_t binding_count, long long *result, int *has_result);

/* An instance preserves module globals across runs of the bundle entry.
 * Keep the Bundle and any binding contexts alive until the instance closes.
 * Binding names and the binding array are copied during instantiation.
 * A runtime failure makes the instance unusable. */
BundleInstance *BundleInstantiate(const Bundle *bundle,
                                   const HostBinding *bindings,
                                   size_t binding_count);
int BundleInstanceRun(BundleInstance *instance, long long *result,
                      int *has_result);
void BundleInstanceClose(BundleInstance *instance);

#ifdef __cplusplus
}
#endif

#endif
