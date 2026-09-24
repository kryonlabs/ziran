#include "ziran_host.h"

#include <assert.h>
#include <string.h>

static VmHostField fields[3];

static VmHostValue
string_value(const char *text)
{
    VmHostValue value = {0};
    value.kind = VM_HOST_STRING;
    value.type = "string";
    value.data = (const unsigned char *)text;
    value.length = strlen(text);
    return value;
}

static VmHostValue
integer_value(const char *type, long long number)
{
    VmHostValue value = {0};
    value.kind = VM_HOST_INTEGER;
    value.type = type;
    value.integer = number;
    return value;
}

static int
host_call(void *context, const char *module, const char *function,
          const VmHostValue *args, int count, VmHostValue *result)
{
    int *calls = context;
    assert(strcmp(module, "process") == 0);
    if(strcmp(function, "StartHost") == 0) {
        assert(count == 2 && args[0].kind == VM_HOST_RECORD);
        assert(args[1].kind == VM_HOST_SLICE && args[1].length == 1);
        assert(args[0].fields[5].value.integer == 1);
        assert(args[1].elements[0].length == 3);
        assert(memcmp(args[1].elements[0].data, "arg", 3) == 0);
        fields[0] = (VmHostField){"handle", integer_value("i32", 7)};
        fields[1] = (VmHostField){"error", string_value("")};
        result->kind = VM_HOST_RECORD;
        result->fields = fields;
        result->field_count = 2;
    } else if(strcmp(function, "NextLineHost") == 0) {
        assert(count == 1 && args[0].integer == 7);
        fields[0] = (VmHostField){"text", string_value("ready")};
        fields[1] = (VmHostField){"eof", integer_value("bool", 0)};
        fields[2] = (VmHostField){"error", string_value("")};
        result->kind = VM_HOST_RECORD;
        result->fields = fields;
        result->field_count = 3;
    } else if(strcmp(function, "WaitHost") == 0) {
        assert(count == 1 && args[0].integer == 7);
        fields[0] = (VmHostField){"code", integer_value("i32", 0)};
        fields[1] = (VmHostField){"stderr", string_value("")};
        fields[2] = (VmHostField){"error", string_value("")};
        result->kind = VM_HOST_RECORD;
        result->fields = fields;
        result->field_count = 3;
    } else {
        return 0;
    }
    (*calls)++;
    return 1;
}

int
main(int argc, char **argv)
{
    assert(argc == 2);
    Bundle *bundle = BundleOpen(argv[1]);
    assert(bundle != NULL);
    HostBinding bindings[] = {
        {"process", "StartHost", host_call, NULL},
        {"process", "NextLineHost", host_call, NULL},
        {"process", "WaitHost", host_call, NULL},
    };
    int calls = 0;
    for(int i = 0; i < 3; i++)
        bindings[i].context = &calls;
    long long value = 0;
    int has_result = 0;
    assert(BundleRun(bundle, bindings, 3, &value, &has_result));
    assert(has_result && value == 42 && calls == 3);
    BundleClose(bundle);
    return 0;
}
