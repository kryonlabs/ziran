#include "ziran_host.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
host_call(void *context, const char *module, const char *function,
          const VmHostValue *args, int arg_count, VmHostValue *result)
{
    int *calls = context;
    if(strcmp(module, "host_api") != 0 || arg_count != 1)
        return 0;
    (*calls)++;
    if(strcmp(function, "AddTenHost") == 0 &&
       args[0].kind == VM_HOST_INTEGER &&
       strcmp(args[0].type, "i32") == 0) {
        result->kind = VM_HOST_INTEGER;
        result->integer = args[0].integer + 10;
        return 1;
    }
    if(strcmp(function, "ByteCountHost") == 0 &&
       args[0].kind == VM_HOST_STRING &&
       args[0].length == 2 &&
       memcmp(args[0].data, "hi", 2) == 0) {
        result->kind = VM_HOST_INTEGER;
        result->integer = (int64_t)args[0].length;
        return 1;
    }
    if(strcmp(function, "DoubleHost") == 0 &&
       args[0].kind == VM_HOST_REAL &&
       strcmp(args[0].type, "float") == 0) {
        result->kind = VM_HOST_REAL;
        result->real = args[0].real * 2.0;
        return 1;
    }
    if(strcmp(function, "EchoHost") == 0 &&
       args[0].kind == VM_HOST_UNSIGNED &&
       strcmp(args[0].type, "u32") == 0) {
        result->kind = VM_HOST_UNSIGNED;
        result->bits = args[0].bits;
        return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    long long result = 0;
    int has_result = 0, calls = 0;
    const char *names[] = {"AddTenHost", "ByteCountHost", "DoubleHost",
                           "EchoHost"};
    HostBinding bindings[4];
    assert(argc == 2);
    Bundle *bundle = BundleOpen(argv[1]);
    assert(bundle != NULL);
    assert(BundleCapabilityCount(bundle) == 4);
    for(size_t i = 0; i < 4; i++) {
        assert(strcmp(BundleCapabilityModule(bundle, i), "host_api") == 0);
        assert(strcmp(BundleCapabilityFunction(bundle, i), names[i]) == 0);
        bindings[i] = (HostBinding){"host_api", names[i], host_call, &calls};
    }
    assert(BundleCapabilityModule(bundle, 4) == NULL);
    assert(BundleCapabilityFunction(bundle, 4) == NULL);
    assert(!BundleRun(bundle, bindings, 3, &result, &has_result));
    assert(calls == 0);
    assert(BundleRun(bundle, bindings, 4, &result, &has_result));
    assert(has_result && result == 42 && calls == 4);
    BundleClose(bundle);
    return 0;
}
