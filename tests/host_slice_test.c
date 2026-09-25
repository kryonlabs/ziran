#include "ziran_host.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
fill(void *context, const char *module, const char *function,
     const VmHostValue *args, int arg_count, VmHostValue *result)
{
    int *calls = context;
    if(strcmp(module, "returned") == 0)
        return 0;
    assert(strcmp(module, "host_buffer") == 0);
    assert(arg_count == 1);
    if(strcmp(function, "ReplaceWord") == 0) {
        assert(args[0].kind == VM_HOST_SLICE);
        assert(strcmp(args[0].type, "[]string") == 0);
        assert(args[0].length == 2);
        assert(args[0].elements[0].kind == VM_HOST_STRING);
        assert(args[0].elements[0].length == 3);
        assert(memcmp(args[0].elements[0].data, "old", 3) == 0);
        assert(args[0].elements[1].length == 6);
        assert(memcmp(args[0].elements[1].data, "second", 6) == 0);
        (*calls)++;
        args[0].elements[0].data = (const unsigned char *)"new";
        args[0].elements[0].length = 3;
        result->kind = VM_HOST_INTEGER;
        result->integer = 2;
        return 1;
    }
    assert(strcmp(function, "Fill") == 0);
    assert(args[0].kind == VM_HOST_SLICE);
    assert(strcmp(args[0].type, "[]u8") == 0);
    (*calls)++;
    result->kind = VM_HOST_INTEGER;
    result->integer = (int64_t)args[0].length;
    if(args[0].length == 0)
        return 1;
    assert(args[0].length == 2);
    assert(args[0].elements[0].kind == VM_HOST_UNSIGNED);
    assert(strcmp(args[0].elements[0].type, "u8") == 0);
    assert(args[0].elements[0].bits == 1);
    assert(args[0].elements[1].bits == 2);
    args[0].elements[0].bits = 40;
    if(*calls == 5)
        args[0].elements[1].kind = VM_HOST_INTEGER;
    return 1;
}

static int view[3] = {10, 20, 30};

static int
return_view(void *context, const char *module, const char *function,
            const VmHostValue *args, int arg_count, VmHostValue *result)
{
    static VmHostValue elements[3];
    (void)context; (void)args; (void)arg_count;
    if(strcmp(module, "returned") != 0 || strcmp(function, "MakeView") != 0)
        return 0;
    for(int i = 0; i < 3; i++) {
        elements[i].kind = VM_HOST_INTEGER;
        elements[i].type = "s32";
        elements[i].integer = view[i];
    }
    result->kind = VM_HOST_SLICE;
    result->type = "[]s32";
    result->elements = elements;
    result->length = 3;
    return 1;
}

int
main(int argc, char **argv)
{
    long long result = 0;
    int has_result = 0, calls = 0;
    assert(argc == 2 ||
           (argc == 3 && (strcmp(argv[2], "overlap") == 0 ||
                          strcmp(argv[2], "return_view") == 0)));
    Bundle *bundle = BundleOpen(argv[1]);
    assert(bundle != NULL);
    if(argc == 3 && strcmp(argv[2], "return_view") == 0) {
        HostBinding make = {"returned", "MakeView", fill, &calls};
        assert(BundleCapabilityCount(bundle) == 1);
        (void)make;
        {
            HostBinding bindings[1] = {{"returned", "MakeView",
                                        return_view, &calls}};
            if(!BundleRun(bundle, bindings, 1, &result, &has_result)) {
                fprintf(stderr, "returned view run failed\n");
                return 1;
            }
            assert(has_result && result == 60);
        }
        BundleClose(bundle);
        return 0;
    }
    if(argc == 3) {
        assert(BundleCapabilityCount(bundle) == 1);
        HostBinding overlap = {"overlap", "Touch", fill, &calls};
        assert(!BundleRun(bundle, &overlap, 1, &result, &has_result));
        assert(calls == 0);
        BundleClose(bundle);
        return 0;
    }
    assert(BundleCapabilityCount(bundle) == 2);
    HostBinding bindings[2] = {
        {"host_buffer", "Fill", fill, &calls},
        {"host_buffer", "ReplaceWord", fill, &calls}
    };
    if(!BundleRun(bundle, bindings, 2, &result, &has_result)) {
        fprintf(stderr, "slice host run failed after %d calls\n", calls);
        return 1;
    }
    assert(has_result && result == 42 && calls == 3);
    assert(!BundleRun(bundle, bindings, 2, &result, &has_result));
    assert(calls == 5);
    BundleClose(bundle);
    return 0;
}
