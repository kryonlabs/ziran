#include "ziran_host.h"

#include <assert.h>
#include <string.h>

static int malformed;
static int calls;
static VmHostValue boxes[2];
static VmHostField fields[2][2];

static int
host_call(void *context, const char *module, const char *function,
          const VmHostValue *args, int arg_count, VmHostValue *result)
{
    (void)context;
    assert(strcmp(module, "arrays") == 0);
    assert(arg_count == 1);
    assert(args[0].kind == VM_HOST_ARRAY);
    assert(args[0].field_count == 0 && args[0].fields == NULL);
    calls++;
    if(strcmp(function, "EchoEmpty") == 0) {
        assert(strcmp(args[0].type, "[0]s32") == 0);
        assert(args[0].length == 0 && args[0].elements == NULL);
        result->kind = VM_HOST_ARRAY;
        result->length = malformed == 1 ? 1 : 0;
        result->elements = NULL;
        return 1;
    }
    assert(strcmp(function, "EchoBoxes") == 0);
    assert(strcmp(args[0].type, "[2]Box") == 0);
    assert(args[0].length == 2 && args[0].elements != NULL);
    for(int i = 0; i < 2; i++) {
        const VmHostValue *box = &args[0].elements[i];
        assert(box->kind == VM_HOST_RECORD);
        assert(strcmp(box->type, "Box") == 0);
        assert(box->field_count == 2);
        assert(strcmp(box->fields[0].name, "empty") == 0);
        assert(box->fields[0].value.kind == VM_HOST_ARRAY);
        assert(strcmp(box->fields[0].value.type, "[0]s32") == 0);
        assert(box->fields[0].value.length == 0);
        assert(box->fields[0].value.elements == NULL);
        assert(strcmp(box->fields[1].name, "value") == 0);
        assert(box->fields[1].value.integer == (i == 0 ? 40 : 2));
        fields[i][0] = (VmHostField){"empty", {
            .kind = VM_HOST_ARRAY, .type = "[0]s32"}};
        fields[i][1] = (VmHostField){"value", {
            .kind = VM_HOST_INTEGER, .type = "s32", .integer = i == 0 ? 40 : 2}};
        boxes[i] = (VmHostValue){.kind = VM_HOST_RECORD, .type = "Box",
            .fields = fields[i], .field_count = 2};
    }
    if(malformed == 2)
        fields[1][0].value.kind = VM_HOST_INTEGER;
    result->kind = VM_HOST_ARRAY;
    result->length = malformed == 3 ? 1 : 2;
    result->elements = boxes;
    return 1;
}

int
main(int argc, char **argv)
{
    assert(argc == 2);
    Bundle *bundle = BundleOpen(argv[1]);
    assert(bundle != NULL);
    assert(BundleCapabilityCount(bundle) == 2);
    HostBinding bindings[2] = {
        {"arrays", "EchoEmpty", host_call, NULL},
        {"arrays", "EchoBoxes", host_call, NULL}
    };
    long long result = 0;
    int has_result = 0;
    assert(BundleRun(bundle, bindings, 2, &result, &has_result));
    assert(has_result && result == 42 && calls == 2);
    for(malformed = 1; malformed <= 3; malformed++)
        assert(!BundleRun(bundle, bindings, 2, &result, &has_result));
    malformed = 0;
    assert(BundleRun(bundle, bindings, 2, &result, &has_result));
    assert(has_result && result == 42);
    BundleClose(bundle);
    return 0;
}
