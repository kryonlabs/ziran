#include "zir_bundle.h"
#include "zir_vm.h"

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
    char module[ZIR_NAME_MAX], function[ZIR_NAME_MAX];
    long long result = 0;
    int has_result = 0, calls = 0;
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL);
    ZirProgram *program = BundleRead(file, argv[1], module, sizeof(module),
                                     function, sizeof(function));
    fclose(file);
    assert(program != NULL);
    assert(VmRunWithHost(program, module, function, host_call, &calls,
                         &result, &has_result));
    assert(has_result && result == 42 && calls == 4);
    ProgramFree(program);
    return 0;
}
