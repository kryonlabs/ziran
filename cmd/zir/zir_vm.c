#include "zir_vm.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_text.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { VM_MAX_PARAMS = 16, VM_MAX_LOCALS = 64, VM_MAX_DEPTH = 128,
       VM_MAX_STEPS = 1000000, VM_MAX_FIELDS = 64,
       VM_MAX_ENUM_MEMBERS = 64,
       VM_MAX_RECORD_BYTES = 64 * 1024 * 1024 };

typedef struct Parameter {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
} Parameter;

typedef enum ValueKind {
    VALUE_INVALID,
    VALUE_VOID,
    VALUE_INT,
    VALUE_REAL,
    VALUE_STRING,
    VALUE_ENUM,
    VALUE_RECORD
} ValueKind;

typedef struct Record Record;

typedef struct Value {
    ValueKind kind;
    int64_t integer;
    uint64_t bits;
    int unsigned64;
    double real;
    const unsigned char *data;
    size_t length;
    const ZirType *enumeration;
    Record *record;
} Value;

typedef struct RecordField {
    ZirTypeField field;
    Value value;
} RecordField;

struct Record {
    Record *next;
    const ZirModule *owner;
    const ZirType *type;
    int field_count;
    RecordField fields[];
};

typedef struct StringLiteral {
    struct StringLiteral *next;
    const ZirExpr *expression;
    size_t length;
    unsigned char data[];
} StringLiteral;

typedef struct Local {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
    Value value;
} Local;

typedef struct Vm {
    int depth;
    int steps;
    int failed;
    size_t record_bytes;
    Record *records;
    StringLiteral *strings;
    VmHostCall host;
    void *host_context;
} Vm;

typedef struct Frame {
    Vm *vm;
    const ZirModule *module;
    const ZirFunction *function;
    Local locals[VM_MAX_LOCALS];
    int local_count;
} Frame;

static const ZirImport *
host_import(const ZirModule *module, const char *name)
{
    for(int i = 0; i < module->import_count; i++)
        if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
           strcmp(module->imports[i].name, name) == 0)
            return &module->imports[i];
    return NULL;
}

typedef enum Flow {
    FLOW_NEXT,
    FLOW_RETURN,
    FLOW_BREAK,
    FLOW_CONTINUE,
    FLOW_ERROR
} Flow;

static ValueKind
value_kind(const char *type)
{
    if(strcmp(type, "void") == 0)
        return VALUE_VOID;
    if(strcmp(type, "i32") == 0 || strcmp(type, "int") == 0 ||
       strcmp(type, "integer") == 0 || strcmp(type, "bool") == 0 ||
       strcmp(type, "u32") == 0 || strcmp(type, "u8") == 0 ||
       strcmp(type, "u64") == 0 || strcmp(type, "i64") == 0)
        return VALUE_INT;
    if(strcmp(type, "float") == 0 || strcmp(type, "f32") == 0 ||
       strcmp(type, "double") == 0 || strcmp(type, "f64") == 0 ||
       strcmp(type, "real") == 0)
        return VALUE_REAL;
    if(strcmp(type, "string") == 0)
        return VALUE_STRING;
    return VALUE_INVALID;
}

static int
scalar_type(const char *type)
{
    return value_kind(type) != VALUE_INVALID;
}

typedef struct EnumEntry {
    char name[ZIR_NAME_MAX];
    int32_t value;
} EnumEntry;

static void
skip_enum_space(const char **cursor, const char *end)
{
    while(*cursor < end && isspace((unsigned char)**cursor))
        (*cursor)++;
}

static int
enum_term(const char **cursor, const char *end,
          const EnumEntry *entries, int count, int64_t *value)
{
    const char *start;
    int sign = 1;
    skip_enum_space(cursor, end);
    if(*cursor < end && (**cursor == '+' || **cursor == '-')) {
        if(**cursor == '-')
            sign = -1;
        (*cursor)++;
    }
    skip_enum_space(cursor, end);
    start = *cursor;
    if(start >= end)
        return 0;
    if(isdigit((unsigned char)*start)) {
        char *after;
        errno = 0;
        *value = strtoll(start, &after, 0);
        if(errno || after == start || after > end)
            return 0;
        *cursor = after;
    } else if(isalpha((unsigned char)*start) || *start == '_') {
        while(*cursor < end &&
              (isalnum((unsigned char)**cursor) || **cursor == '_'))
            (*cursor)++;
        *value = 0;
        int found = 0;
        for(int i = 0; i < count; i++) {
            if(strlen(entries[i].name) == (size_t)(*cursor - start) &&
               strncmp(entries[i].name, start, (size_t)(*cursor - start)) == 0) {
                *value = entries[i].value;
                found = 1;
                break;
            }
        }
        if(!found)
            return 0;
    } else {
        return 0;
    }
    *value *= sign;
    return *value >= INT32_MIN && *value <= INT32_MAX;
}

static int
enum_expression(const char *cursor, const char *end,
                const EnumEntry *entries, int count, int64_t *value)
{
    if(!enum_term(&cursor, end, entries, count, value))
        return 0;
    for(;;) {
        int64_t term;
        skip_enum_space(&cursor, end);
        if(cursor == end)
            return 1;
        char op = *cursor++;
        if((op != '+' && op != '-') ||
           !enum_term(&cursor, end, entries, count, &term))
            return 0;
        *value += op == '+' ? term : -term;
        if(*value < INT32_MIN || *value > INT32_MAX)
            return 0;
    }
}

static int
enum_member_value(const ZirType *type, const char *wanted, int32_t *value)
{
    EnumEntry entries[VM_MAX_ENUM_MEMBERS];
    int count = 0;
    int found = 0;
    int64_t next = 0;
    const char *cursor = type->body;
    if(!type->is_enum)
        return 0;
    while(*cursor) {
        while(*cursor == ',' || isspace((unsigned char)*cursor))
            cursor++;
        if(*cursor == 0)
            break;
        const char *end = cursor;
        while(*end && *end != ',' && *end != '\n')
            end++;
        const char *start = cursor;
        if(!isalpha((unsigned char)*cursor) && *cursor != '_')
            return 0;
        while(cursor < end &&
              (isalnum((unsigned char)*cursor) || *cursor == '_'))
            cursor++;
        size_t length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX ||
           count >= VM_MAX_ENUM_MEMBERS)
            return 0;
        for(int i = 0; i < count; i++)
            if(strlen(entries[i].name) == length &&
               strncmp(entries[i].name, start, length) == 0)
                return 0;
        skip_enum_space(&cursor, end);
        int64_t number = next;
        if(cursor < end) {
            if(*cursor++ != '=' ||
               !enum_expression(cursor, end, entries, count, &number))
                return 0;
        }
        if(number < INT32_MIN || number > INT32_MAX)
            return 0;
        memcpy(entries[count].name, start, length);
        entries[count].name[length] = 0;
        entries[count].value = (int32_t)number;
        if(wanted != NULL && strcmp(wanted, entries[count].name) == 0) {
            *value = (int32_t)number;
            found = 1;
        }
        count++;
        next = number + 1;
        cursor = *end ? end + 1 : end;
    }
    return wanted == NULL ? count > 0 : found;
}

static int
portable_type_at(const ZirModule *module, const char *type, int depth)
{
    const ZirModule *owner = NULL;
    const ZirType *record;
    size_t offset = 0;
    ZirTypeField field;
    int count = 0;
    int status;
    if(scalar_type(type))
        return 1;
    if(depth >= VM_MAX_DEPTH)
        return 0;
    record = FindType(module, type, &owner);
    if(record == NULL || record->is_slot || record->is_extern)
        return 0;
    if(record->is_enum)
        return enum_member_value(record, NULL, NULL);
    while((status = TypeNextField(record, &offset, &field)) == 1) {
        if(++count > VM_MAX_FIELDS || strcmp(field.type, "void") == 0 ||
           !portable_type_at(owner, field.type, depth + 1))
            return 0;
    }
    return status == 0;
}

static int
portable_type(const ZirModule *module, const char *type)
{
    return portable_type_at(module, type, 0);
}

static Value
int_value(int64_t integer)
{
    Value value = {.kind = VALUE_INT, .integer = integer,
                   .bits = (uint64_t)integer};
    return value;
}

static Value
uint_value(uint64_t bits)
{
    Value value = {.kind = VALUE_INT, .bits = bits, .unsigned64 = 1};
    return value;
}

static Value
real_value(double real)
{
    Value value = {.kind = VALUE_REAL, .real = real};
    return value;
}

static Value
string_value(const unsigned char *data, size_t length)
{
    Value value = {.kind = VALUE_STRING, .data = data, .length = length};
    return value;
}

/* Decode one checked source literal into immutable UTF-8 bytes. The same
 * escapes and Unicode validity rules apply to native and portable strings. */
static int
decode_string(const char *source, unsigned char *out, size_t capacity,
              size_t *length)
{
    size_t size = strlen(source);
    size_t used = 0;
    int remaining = 0;
    unsigned int scalar = 0, minimum = 0;
    if(size < 2 || source[0] != '"' || source[size - 1] != '"')
        return 0;
    for(size_t i = 1; i + 1 < size; i++) {
        unsigned int value = (unsigned char)source[i];
        int unicode_escape = 0;
        if(value == '\\') {
            if(++i + 1 >= size)
                return 0;
            value = (unsigned char)source[i];
            const char *escapes = "0abfnrtv\\\"";
            const unsigned char values[] = {0, 7, 8, 12, 10, 13, 9, 11, '\\', '"'};
            const char *found = strchr(escapes, (int)value);
            if(found != NULL) {
                value = values[found - escapes];
            } else if(value == 'x' || value == 'u' || value == 'U') {
                unicode_escape = value != 'x';
                int digits = value == 'x' ? 2 : value == 'u' ? 4 : 8;
                value = 0;
                for(int d = 0; d < digits; d++) {
                    if(++i + 1 >= size)
                        return 0;
                    int digit = (unsigned char)source[i];
                    if(digit >= '0' && digit <= '9') digit -= '0';
                    else if(digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
                    else if(digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
                    else return 0;
                    value = value * 16 + (unsigned int)digit;
                }
                if(value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                    return 0;
            } else {
                return 0;
            }
        } else if(value == '"') {
            return 0;
        }
        unsigned char bytes[4] = {(unsigned char)value, 0, 0, 0};
        int count = 1;
        if(unicode_escape && value >= 128) {
            if(value < 0x800) {
                bytes[0] = 0xc0 | (value >> 6);
                bytes[1] = 0x80 | (value & 63);
                count = 2;
            } else if(value < 0x10000) {
                bytes[0] = 0xe0 | (value >> 12);
                bytes[1] = 0x80 | ((value >> 6) & 63);
                bytes[2] = 0x80 | (value & 63);
                count = 3;
            } else {
                bytes[0] = 0xf0 | (value >> 18);
                bytes[1] = 0x80 | ((value >> 12) & 63);
                bytes[2] = 0x80 | ((value >> 6) & 63);
                bytes[3] = 0x80 | (value & 63);
                count = 4;
            }
        }
        for(int d = 0; d < count; d++) {
            unsigned char byte = bytes[d];
            if(remaining) {
                if((byte & 0xc0) != 0x80)
                    return 0;
                scalar = (scalar << 6) | (byte & 63);
                remaining--;
                if(!remaining && (scalar < minimum || scalar > 0x10ffff ||
                    (scalar >= 0xd800 && scalar <= 0xdfff)))
                    return 0;
            } else if(byte >= 128) {
                if(byte >= 0xc2 && byte <= 0xdf) {
                    remaining = 1;
                    scalar = byte & 31;
                    minimum = 0x80;
                } else if(byte >= 0xe0 && byte <= 0xef) {
                    remaining = 2;
                    scalar = byte & 15;
                    minimum = 0x800;
                } else if(byte >= 0xf0 && byte <= 0xf4) {
                    remaining = 3;
                    scalar = byte & 7;
                    minimum = 0x10000;
                } else {
                    return 0;
                }
            }
            if(used >= capacity)
                return 0;
            out[used++] = byte;
        }
    }
    if(remaining)
        return 0;
    *length = used;
    return 1;
}

static Value
literal_string(Vm *vm, const ZirExpr *expression)
{
    for(StringLiteral *item = vm->strings; item != NULL; item = item->next)
        if(item->expression == expression)
            return string_value(item->data, item->length);
    size_t capacity = strlen(expression->text);
    StringLiteral *item = malloc(sizeof(*item) + capacity + 1);
    if(item == NULL || !decode_string(expression->text, item->data,
                                      capacity + 1, &item->length)) {
        free(item);
        vm->failed = 1;
        return string_value((const unsigned char *)"", 0);
    }
    item->expression = expression;
    item->next = vm->strings;
    vm->strings = item;
    return string_value(item->data, item->length);
}

static Value
enum_value(const ZirType *type, int32_t integer)
{
    Value value = {.kind = VALUE_ENUM, .integer = integer,
                   .bits = (uint64_t)(int64_t)integer,
                   .enumeration = type};
    return value;
}

static uint64_t
integer_bits(Value value)
{
    return value.unsigned64 ? value.bits : (uint64_t)value.integer;
}

static int64_t
signed64(uint64_t bits)
{
    return bits <= INT64_MAX ? (int64_t)bits :
           -1 - (int64_t)(UINT64_MAX - bits);
}

static double
as_real(Value value)
{
    if(value.kind == VALUE_REAL)
        return value.real;
    return value.unsigned64 ? (double)value.bits : (double)value.integer;
}

static int
truthy(Value value)
{
    return value.kind == VALUE_REAL ? value.real != 0.0 :
           integer_bits(value) != 0;
}

static Record *
allocate_record(Vm *vm, const ZirModule *owner,
                const ZirType *type, int count)
{
    size_t bytes = sizeof(Record) + (size_t)count * sizeof(RecordField);
    if(count < 0 || count > VM_MAX_FIELDS ||
       bytes > VM_MAX_RECORD_BYTES - vm->record_bytes) {
        vm->failed = 1;
        return NULL;
    }
    Record *record = calloc(1, bytes);
    if(record == NULL) {
        vm->failed = 1;
        return NULL;
    }
    record->next = vm->records;
    record->owner = owner;
    record->type = type;
    record->field_count = count;
    vm->records = record;
    vm->record_bytes += bytes;
    return record;
}

static Value default_value(Vm *vm, const ZirModule *module,
                           const char *type, int depth);

static Value
clone_value(Vm *vm, Value value, int depth)
{
    if(value.kind != VALUE_RECORD || vm->failed)
        return value;
    if(value.record == NULL || depth >= VM_MAX_DEPTH) {
        vm->failed = 1;
        return int_value(0);
    }
    Record *copy = allocate_record(vm, value.record->owner,
                                   value.record->type,
                                   value.record->field_count);
    if(copy == NULL)
        return int_value(0);
    for(int i = 0; i < copy->field_count && !vm->failed; i++) {
        copy->fields[i].field = value.record->fields[i].field;
        copy->fields[i].value = clone_value(vm,
            value.record->fields[i].value, depth + 1);
    }
    return (Value){.kind = VALUE_RECORD, .record = copy};
}

static Value
default_value(Vm *vm, const ZirModule *module, const char *type, int depth)
{
    ValueKind kind = value_kind(type);
    if(kind == VALUE_INT)
        return int_value(0);
    if(kind == VALUE_REAL)
        return real_value(0.0);
    if(kind == VALUE_STRING)
        return string_value((const unsigned char *)"", 0);
    const ZirModule *owner = NULL;
    const ZirType *record_type = FindType(module, type, &owner);
    if(record_type != NULL && record_type->is_enum)
        return enum_value(record_type, 0);
    if(kind != VALUE_INVALID || record_type == NULL ||
       record_type->is_slot ||
       record_type->is_extern || depth >= VM_MAX_DEPTH) {
        vm->failed = 1;
        return int_value(0);
    }
    size_t offset = 0;
    ZirTypeField field;
    int count = 0;
    int status;
    while((status = TypeNextField(record_type, &offset, &field)) == 1)
        count++;
    if(status < 0 || count > VM_MAX_FIELDS) {
        vm->failed = 1;
        return int_value(0);
    }
    Record *record = allocate_record(vm, owner, record_type, count);
    if(record == NULL)
        return int_value(0);
    offset = 0;
    for(int i = 0; i < count && !vm->failed; i++) {
        if(TypeNextField(record_type, &offset, &record->fields[i].field) != 1) {
            vm->failed = 1;
            break;
        }
        record->fields[i].value = default_value(vm, owner,
            record->fields[i].field.type, depth + 1);
    }
    return (Value){.kind = VALUE_RECORD, .record = record};
}

static Value
coerce(Vm *vm, const ZirModule *module, Value value, const char *type)
{
    ValueKind target = value_kind(type);
    if(target == VALUE_VOID) {
        Value empty = {.kind = VALUE_VOID};
        return empty;
    }
    if(target == VALUE_STRING) {
        if(value.kind == VALUE_STRING)
            return value;
        vm->failed = 1;
        return string_value((const unsigned char *)"", 0);
    }
    if(target == VALUE_INVALID) {
        const ZirType *record = FindType(module, type, NULL);
        if(record != NULL && record->is_enum &&
           value.kind != VALUE_RECORD && value.kind != VALUE_VOID &&
           value.kind != VALUE_INVALID) {
            value = coerce(vm, module, value, "i32");
            return enum_value(record, (int32_t)value.integer);
        }
        if(record != NULL && !record->is_enum && !record->is_slot &&
           !record->is_extern && value.kind == VALUE_RECORD &&
           value.record != NULL && value.record->type == record)
            return clone_value(vm, value, 0);
        vm->failed = 1;
        return int_value(0);
    }
    if(value.kind == VALUE_STRING || value.kind == VALUE_RECORD ||
       value.kind == VALUE_VOID ||
       value.kind == VALUE_INVALID) {
        vm->failed = 1;
        return int_value(0);
    }
    if(strcmp(type, "bool") == 0)
        return int_value(truthy(value));
    if(strcmp(type, "integer") == 0)
        return value;
    if(target == VALUE_REAL) {
        double number = as_real(value);
        if(strcmp(type, "float") == 0 || strcmp(type, "f32") == 0)
            number = (float)number;
        if(!isfinite(number))
            vm->failed = 1;
        return real_value(number);
    }
    if(value.kind == VALUE_REAL) {
        int unsigned_type = strcmp(type, "u32") == 0 ||
                            strcmp(type, "u8") == 0 ||
                            strcmp(type, "u64") == 0;
        double lower = unsigned_type ? 0.0 :
                       strcmp(type, "i64") == 0 ? -9223372036854775808.0 :
                       INT32_MIN;
        double upper = strcmp(type, "u64") == 0 ?
                       18446744073709551616.0 :
                       strcmp(type, "i64") == 0 ?
                       9223372036854775808.0 :
                       strcmp(type, "u32") == 0 ?
                       (double)UINT32_MAX + 1.0 :
                       strcmp(type, "u8") == 0 ? 256.0 :
                       (double)INT32_MAX + 1.0;
        if(!isfinite(value.real) || value.real < lower ||
           value.real >= upper) {
            vm->failed = 1;
            return int_value(0);
        }
        if(strcmp(type, "u64") == 0)
            return uint_value((uint64_t)value.real);
        if(strcmp(type, "i64") == 0)
            return int_value((int64_t)value.real);
        if(strcmp(type, "u32") == 0)
            return int_value((uint32_t)value.real);
        if(strcmp(type, "u8") == 0)
            return int_value((uint8_t)value.real);
        return int_value((int32_t)value.real);
    }
    if(strcmp(type, "u64") == 0)
        return uint_value(integer_bits(value));
    if(strcmp(type, "i64") == 0)
        return int_value(signed64(integer_bits(value)));
    uint32_t bits = (uint32_t)integer_bits(value);
    if(strcmp(type, "u32") == 0)
        return int_value(bits);
    if(strcmp(type, "u8") == 0)
        return int_value((uint8_t)bits);
    return int_value(bits <= INT32_MAX ? (int64_t)bits :
                     (int64_t)bits - 4294967296LL);
}

static int
parse_parameters(const ZirModule *module, const ZirFunction *function,
                 Parameter *parameters)
{
    const char *cursor = function->args;
    int count = 0;
    while(*cursor != 0) {
        const char *start;
        size_t length;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor == 0)
            break;
        if(count >= VM_MAX_PARAMS)
            return -1;
        start = cursor;
        while((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')
            cursor++;
        length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX ||
           (start[0] >= '0' && start[0] <= '9'))
            return -1;
        memcpy(parameters[count].name, start, length);
        parameters[count].name[length] = 0;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor++ != ':')
            return -1;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        start = cursor;
        while((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')
            cursor++;
        length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX)
            return -1;
        memcpy(parameters[count].type, start, length);
        parameters[count].type[length] = 0;
        if(!portable_type(module, parameters[count].type) ||
           strcmp(parameters[count].type, "void") == 0)
            return -1;
        count++;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor == 0)
            break;
        if(*cursor++ != ',')
            return -1;
        if(*cursor == 0)
            return -1;
    }
    return count;
}

static int
parse_import_parameters(const ZirModule *module, const ZirImport *import,
                        Parameter *parameters)
{
    ZirFunction signature = {0};
    copy_text(signature.args, sizeof(signature.args), import->args);
    return parse_parameters(module, &signature, parameters);
}

static int
binding_index(const Parameter *bindings, int count, const char *name)
{
    for(int i = count - 1; i >= 0; i--)
        if(strcmp(bindings[i].name, name) == 0)
            return i;
    return -1;
}

static const char *
assignment_root(const ZirFunction *function, int index)
{
    for(int depth = 0; depth < VM_MAX_DEPTH; depth++) {
        if(index < 0 || index >= function->expr_count)
            return NULL;
        const ZirExpr *expression = &function->exprs[index];
        if(expression->kind == ZIR_EXPR_IDENT)
            return expression->name;
        if(expression->kind != ZIR_EXPR_MEMBER)
            return NULL;
        index = expression->left;
    }
    return NULL;
}

static int
binary_operator(const char *op)
{
    static const char *const supported[] = {
        "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=",
        "&&", "||", "&", "|", "^", "<<", ">>", NULL
    };
    for(int i = 0; supported[i] != NULL; i++)
        if(strcmp(op, supported[i]) == 0)
            return 1;
    return 0;
}

static int
integer_type(const char *type)
{
    return value_kind(type) == VALUE_INT &&
           strcmp(type, "bool") != 0;
}

static int
bitwise_operator(const char *op)
{
    return strcmp(op, "&") == 0 || strcmp(op, "|") == 0 ||
           strcmp(op, "^") == 0 || strcmp(op, "<<") == 0 ||
           strcmp(op, ">>") == 0;
}

static const char *
assignment_binary_operator(const char *op)
{
    if(strcmp(op, "+=") == 0)
        return "+";
    if(strcmp(op, "-=") == 0)
        return "-";
    if(strcmp(op, "*=") == 0)
        return "*";
    if(strcmp(op, "/=") == 0)
        return "/";
    if(strcmp(op, "%=") == 0)
        return "%";
    if(strcmp(op, "&=") == 0)
        return "&";
    if(strcmp(op, "|=") == 0)
        return "|";
    if(strcmp(op, "^=") == 0)
        return "^";
    if(strcmp(op, "<<=") == 0)
        return "<<";
    if(strcmp(op, ">>=") == 0)
        return ">>";
    return NULL;
}

static int
verify_expression(const ZirModule *module, const ZirFunction *function,
                  const Parameter *bindings, int binding_count,
                  int index, int depth)
{
    const ZirExpr *expression;
    const ZirModule *owner = NULL;
    const ZirFunction *callee = NULL;
    int children = 0;
    Parameter parameters[VM_MAX_PARAMS];
    if(index < 0 || index >= function->expr_count || depth >= VM_MAX_DEPTH)
        return 0;
    expression = &function->exprs[index];
    if(!portable_type(module, expression->type))
        return 0;
    switch(expression->kind) {
    case ZIR_EXPR_INT: {
        char *end;
        errno = 0;
        if(expression->text[0] == '-') {
            (void)strtoll(expression->text, &end, 0);
            return errno == 0 && end != expression->text && *end == 0;
        }
        (void)strtoull(expression->text, &end, 0);
        return errno == 0 && end != expression->text && *end == 0;
    }
    case ZIR_EXPR_FLOAT: {
        char *end;
        double number;
        errno = 0;
        number = strtod(expression->text, &end);
        return errno == 0 && end != expression->text && *end == 0 &&
               isfinite(number);
    }
    case ZIR_EXPR_STRING: {
        unsigned char bytes[ZIR_TEXT_MAX];
        size_t length;
        return strcmp(expression->type, "string") == 0 &&
               decode_string(expression->text, bytes, sizeof(bytes), &length);
    }
    case ZIR_EXPR_IDENT: {
        if(strcmp(expression->name, "true") == 0 ||
           strcmp(expression->name, "false") == 0 ||
           binding_index(bindings, binding_count, expression->name) >= 0)
            return 1;
        const ZirType *enumeration = NULL;
        int32_t number;
        return ResolveEnumMember(module, expression->name,
                                 &owner, &enumeration) == 1 &&
               enum_member_value(enumeration, expression->name, &number);
    }
    case ZIR_EXPR_UNARY:
        return (strcmp(expression->op, "+") == 0 ||
                strcmp(expression->op, "-") == 0 ||
                strcmp(expression->op, "!") == 0 ||
                strcmp(expression->op, "~") == 0) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1) &&
               (strcmp(expression->op, "~") == 0 ?
                integer_type(function->exprs[expression->right].type) :
                strcmp(expression->op, "!") == 0 ?
                strcmp(function->exprs[expression->right].type, "bool") == 0 :
                value_kind(function->exprs[expression->right].type) == VALUE_INT ||
                value_kind(function->exprs[expression->right].type) == VALUE_REAL);
    case ZIR_EXPR_BINARY: {
        if(!binary_operator(expression->op) ||
           !verify_expression(module, function, bindings, binding_count,
                              expression->left, depth + 1) ||
           !verify_expression(module, function, bindings, binding_count,
                              expression->right, depth + 1))
            return 0;
        const char *left_type = function->exprs[expression->left].type;
        const char *right_type = function->exprs[expression->right].type;
        if(strcmp(left_type, "string") == 0 ||
           strcmp(right_type, "string") == 0)
            return strcmp(left_type, "string") == 0 &&
                   strcmp(right_type, "string") == 0 &&
                   (strcmp(expression->op, "==") == 0 ||
                    strcmp(expression->op, "!=") == 0);
        const ZirType *left_enum = FindType(module, left_type, NULL);
        const ZirType *right_enum = FindType(module, right_type, NULL);
        if(left_enum != NULL && left_enum->is_enum)
            return left_enum == right_enum &&
                   (strcmp(expression->op, "==") == 0 ||
                    strcmp(expression->op, "!=") == 0);
        if(bitwise_operator(expression->op))
            return integer_type(left_type) && integer_type(right_type);
        return scalar_type(left_type) && scalar_type(right_type);
    }
    case ZIR_EXPR_CAST: {
        const ZirType *destination = FindType(module, expression->name, NULL);
        if(!scalar_type(expression->name) &&
           (destination == NULL || !destination->is_enum))
            return 0;
        if(!verify_expression(module, function, bindings, binding_count,
                              expression->right, depth + 1))
            return 0;
        const char *source_type = function->exprs[expression->right].type;
        const ZirType *source = FindType(module, source_type, NULL);
        if(strcmp(expression->name, "string") == 0 ||
           strcmp(source_type, "string") == 0)
            return strcmp(expression->name, "string") == 0 &&
                   strcmp(source_type, "string") == 0;
        return scalar_type(source_type) ||
               (source != NULL && source->is_enum);
    }
    case ZIR_EXPR_COMPOUND: {
        const ZirType *record = FindType(module, expression->name, NULL);
        unsigned char seen[VM_MAX_FIELDS] = {0};
        int children = 0;
        if(record == NULL || record->is_enum || record->is_slot ||
           strcmp(expression->type, expression->name) != 0)
            return 0;
        for(int child = expression->first_child; child >= 0;
            child = function->exprs[child].next_sibling) {
            if(child >= function->expr_count || ++children > VM_MAX_FIELDS)
                return 0;
            const ZirExpr *initializer = &function->exprs[child];
            if(initializer->kind != ZIR_EXPR_FIELD_INIT ||
               !verify_expression(module, function, bindings, binding_count,
                                  initializer->right, depth + 1))
                return 0;
            size_t offset = 0;
            ZirTypeField field;
            int field_index = 0;
            int found = 0;
            while(TypeNextField(record, &offset, &field) == 1) {
                if(strcmp(field.name, initializer->name) == 0) {
                    if(seen[field_index] ||
                       (strcmp(field.type, initializer->type) != 0 &&
                        strcmp(ScalarType(field.type),
                               initializer->type) != 0))
                        return 0;
                    seen[field_index] = 1;
                    found = 1;
                    break;
                }
                field_index++;
            }
            if(!found)
                return 0;
        }
        return 1;
    }
    case ZIR_EXPR_MEMBER: {
        if(expression->left < 0 || expression->left >= function->expr_count)
            return 0;
        const ZirExpr *base = &function->exprs[expression->left];
        if(strcmp(base->type, "string") == 0)
            return strcmp(expression->name, "length") == 0 &&
                   strcmp(expression->type, "i32") == 0 &&
                   verify_expression(module, function, bindings, binding_count,
                                     expression->left, depth + 1);
        const ZirType *record = FindType(module, base->type, NULL);
        size_t offset = 0;
        ZirTypeField field;
        if(record == NULL || record->is_enum || record->is_slot ||
           !verify_expression(module, function, bindings, binding_count,
                              expression->left, depth + 1))
            return 0;
        while(TypeNextField(record, &offset, &field) == 1)
            if(strcmp(field.name, expression->name) == 0)
                return strcmp(field.type, expression->type) == 0 ||
                       (ScalarType(field.type)[0] != 0 &&
                        strcmp(ScalarType(field.type), expression->type) == 0);
        return 0;
    }
    case ZIR_EXPR_INDEX:
        return expression->left >= 0 && expression->left < function->expr_count &&
               expression->right >= 0 && expression->right < function->expr_count &&
               strcmp(function->exprs[expression->left].type, "string") == 0 &&
               strcmp(expression->type, "u8") == 0 &&
               integer_type(function->exprs[expression->right].type) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->left, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1);
    case ZIR_EXPR_CONDITIONAL:
        return verify_expression(module, function, bindings, binding_count,
                                 expression->left, depth + 1) &&
               strcmp(function->exprs[expression->left].type, "bool") == 0 &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->third, depth + 1);
    case ZIR_EXPR_CALL:
        if(expression->name[0] == 0)
            return 0;
        int resolved = ResolveFunction(module, expression->name,
                                       &owner, &callee);
        const ZirImport *external = resolved == 0 ?
            host_import(module, expression->name) : NULL;
        if((resolved != 1 || callee == NULL) &&
           (external == NULL || external->extern_kind != ZIR_EXTERN_HOST))
            return 0;
        for(int child = expression->first_child; child >= 0;
            child = function->exprs[child].next_sibling) {
            if(++children > VM_MAX_PARAMS ||
               !verify_expression(module, function, bindings, binding_count,
                                  child, depth + 1))
                return 0;
        }
        return external != NULL ?
            parse_import_parameters(module, external, parameters) == children :
            parse_parameters(owner, callee, parameters) == children;
    default:
        return 0;
    }
}

static const ZirFunction *
find_entry(const ZirProgram *program, const char *module_name,
           const char *function_name, const ZirModule **module_out)
{
    const ZirFunction *entry = NULL;
    *module_out = NULL;
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        if(strcmp(module->name, module_name) != 0)
            continue;
        if(*module_out != NULL)
            return NULL;
        *module_out = module;
        for(int f = 0; f < module->function_count; f++)
            if(strcmp(module->functions[f].name, function_name) == 0) {
                if(entry != NULL)
                    return NULL;
                entry = &module->functions[f];
            }
    }
    return entry;
}

static int
is_else_branch(const ZirStmt *statement)
{
    return statement->kind == ZIR_STMT_IF &&
           strncmp(statement->text, "else", 4) == 0 &&
           (statement->text[4] == 0 || statement->text[4] == ' ' ||
            statement->text[4] == '\t');
}

static int
statement_close(const ZirFunction *function, int begin, int end)
{
    int depth = 1;
    for(int i = begin + 1; i < end; i++) {
        ZirStmtKind kind = function->stmts[i].kind;
        if(kind == ZIR_STMT_IF || kind == ZIR_STMT_WHILE ||
           kind == ZIR_STMT_BLOCK_OPEN)
            depth++;
        else if(kind == ZIR_STMT_BLOCK_CLOSE && --depth == 0)
            return i;
    }
    return -1;
}

static int
verify_sequence(const ZirModule *module, const ZirFunction *function,
                int begin, int end, Parameter *bindings, int binding_count,
                int loop_depth, int depth)
{
    if(depth >= VM_MAX_DEPTH)
        return 0;
    for(int i = begin; i < end; i++) {
        const ZirStmt *statement = &function->stmts[i];
        int close;
        switch(statement->kind) {
        case ZIR_STMT_DECL:
            if(!portable_type(module, statement->type) ||
               strcmp(statement->type, "void") == 0 ||
               statement->name[0] == 0 || binding_count >= VM_MAX_LOCALS ||
               (statement->expr_root >= 0 &&
                !verify_expression(module, function, bindings, binding_count,
                                   statement->expr_root, 0)))
                return 0;
            copy_text(bindings[binding_count].name,
                      sizeof(bindings[binding_count].name), statement->name);
            copy_text(bindings[binding_count].type,
                      sizeof(bindings[binding_count].type), statement->type);
            binding_count++;
            break;
        case ZIR_STMT_ASSIGN:
            if(statement->lhs_root < 0 ||
               assignment_root(function, statement->lhs_root) == NULL ||
               binding_index(bindings, binding_count,
                             assignment_root(function, statement->lhs_root)) < 0 ||
               statement->expr_root < 0 ||
               !verify_expression(module, function, bindings, binding_count,
                                  statement->lhs_root, 0) ||
               !verify_expression(module, function, bindings, binding_count,
                                  statement->expr_root, 0))
                return 0;
            if(strcmp(statement->assignment_op, "=") != 0) {
                const char *operation = assignment_binary_operator(
                    statement->assignment_op);
                const char *destination =
                    function->exprs[statement->lhs_root].type;
                const char *source =
                    function->exprs[statement->expr_root].type;
                if(operation == NULL || !scalar_type(destination) ||
                   !scalar_type(source) ||
                   strcmp(destination, "string") == 0 ||
                   strcmp(source, "string") == 0 ||
                   strcmp(destination, "bool") == 0 ||
                   strcmp(destination, "void") == 0 ||
                   strcmp(source, "bool") == 0 ||
                   strcmp(source, "void") == 0 ||
                   ((strcmp(operation, "%") == 0 ||
                     bitwise_operator(operation)) &&
                    (!integer_type(destination) ||
                     !integer_type(source))))
                    return 0;
            }
            break;
        case ZIR_STMT_RETURN:
            if((statement->expr_root < 0) !=
               (strcmp(function->return_type, "void") == 0))
                return 0;
            if(statement->expr_root >= 0 &&
               !verify_expression(module, function, bindings, binding_count,
                                  statement->expr_root, 0))
                return 0;
            break;
        case ZIR_STMT_EXPR:
        case ZIR_STMT_UNUSED:
            if(statement->expr_root < 0 ||
               !verify_expression(module, function, bindings, binding_count,
                                  statement->expr_root, 0))
                return 0;
            break;
        case ZIR_STMT_IF: {
            int branch = i;
            int saw_else = 0;
            if(is_else_branch(statement))
                return 0;
            do {
                const ZirStmt *head = &function->stmts[branch];
                close = statement_close(function, branch, end);
                if(close < 0 || (saw_else && branch != i) ||
                   (head->expr_root < 0 && !is_else_branch(head)))
                    return 0;
                if(head->expr_root < 0)
                    saw_else = 1;
                else if(!verify_expression(module, function, bindings,
                                           binding_count, head->expr_root, 0) ||
                        strcmp(function->exprs[head->expr_root].type,
                               "bool") != 0)
                    return 0;
                if(!verify_sequence(module, function, branch + 1, close,
                                    bindings, binding_count, loop_depth,
                                    depth + 1))
                    return 0;
                branch = close + 1;
            } while(branch < end &&
                    is_else_branch(&function->stmts[branch]));
            i = branch - 1;
            break;
        }
        case ZIR_STMT_WHILE:
            close = statement_close(function, i, end);
            if(close < 0 || statement->expr_root < 0 ||
               !verify_expression(module, function, bindings, binding_count,
                                  statement->expr_root, 0) ||
               strcmp(function->exprs[statement->expr_root].type,
                      "bool") != 0 ||
               !verify_sequence(module, function, i + 1, close, bindings,
                                binding_count, loop_depth + 1, depth + 1))
                return 0;
            i = close;
            break;
        case ZIR_STMT_BLOCK_OPEN:
            close = statement_close(function, i, end);
            if(close < 0 || !verify_sequence(module, function, i + 1, close,
                                             bindings, binding_count,
                                             loop_depth, depth + 1))
                return 0;
            i = close;
            break;
        case ZIR_STMT_BREAK:
        case ZIR_STMT_CONTINUE:
            if(loop_depth == 0)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static int
sequence_guarantees_return(const ZirFunction *function, int begin, int end,
                           int depth)
{
    if(depth >= VM_MAX_DEPTH)
        return 0;
    for(int i = begin; i < end; i++) {
        const ZirStmt *statement = &function->stmts[i];
        int close;
        if(statement->kind == ZIR_STMT_RETURN)
            return 1;
        if(statement->kind == ZIR_STMT_IF &&
           !is_else_branch(statement)) {
            int branch = i;
            int all_return = 1;
            int has_else = 0;
            do {
                close = statement_close(function, branch, end);
                if(close < 0)
                    return 0;
                all_return &= sequence_guarantees_return(function,
                              branch + 1, close, depth + 1);
                has_else |= function->stmts[branch].expr_root < 0;
                branch = close + 1;
            } while(branch < end &&
                    is_else_branch(&function->stmts[branch]));
            if(all_return && has_else)
                return 1;
            i = branch - 1;
        } else if(statement->kind == ZIR_STMT_BLOCK_OPEN) {
            close = statement_close(function, i, end);
            if(close < 0)
                return 0;
            if(sequence_guarantees_return(function, i + 1, close, depth + 1))
                return 1;
            i = close;
        } else if(statement->kind == ZIR_STMT_WHILE) {
            close = statement_close(function, i, end);
            if(close < 0)
                return 0;
            i = close;
        } else if(statement->kind == ZIR_STMT_BREAK ||
                  statement->kind == ZIR_STMT_CONTINUE) {
            return 0;
        }
    }
    return 0;
}

int
VmVerify(const ZirProgram *program, const char *entry_module,
            const char *entry_function)
{
    const ZirModule *entry_owner;
    const ZirFunction *entry;
    Parameter bindings[VM_MAX_LOCALS];
    if(program == NULL || entry_module == NULL || entry_function == NULL)
        return 0;
    entry = find_entry(program, entry_module, entry_function, &entry_owner);
    if(entry == NULL || entry->is_extern ||
       parse_parameters(entry_owner, entry, bindings) != 0 ||
       (strcmp(entry->return_type, "i32") != 0 &&
        strcmp(entry->return_type, "int") != 0 &&
        strcmp(entry->return_type, "bool") != 0 &&
        strcmp(entry->return_type, "void") != 0)) {
        Diagnostic(Span("<bundle>", 1, 1), "zib.entry",
                      "entry must be one unique zero-argument integer, bool, or void Ziran function");
        return 0;
    }
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        for(int other = 0; other < m; other++)
            if(strcmp(program->modules[other].name, module->name) == 0) {
                Diagnostic(module->span, "zib.module",
                              "duplicate module identity: %s", module->name);
                return 0;
            }
        for(int i = 0; i < module->import_count; i++) {
            if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
               module->imports[i].extern_kind == ZIR_EXTERN_HOST) {
                Parameter parameters[VM_MAX_PARAMS];
                int count = parse_import_parameters(module,
                    &module->imports[i], parameters);
                int scalar_signature =
                    value_kind(module->imports[i].return_type) != VALUE_INVALID &&
                    count >= 0;
                for(int p = 0; scalar_signature && p < count; p++)
                    scalar_signature = value_kind(parameters[p].type) != VALUE_INVALID &&
                        value_kind(parameters[p].type) != VALUE_VOID;
                if(scalar_signature)
                    continue;
            }
            if(module->imports[i].kind != ZIR_IMPORT_HEADER ||
               module->imports[i].resolved_module == NULL) {
                Diagnostic(module->imports[i].span, "zib.capability",
                              "portable execution requires an included Ziran module or scalar host capability");
                return 0;
            }
        }
        if(module->global_count || module->state_count || module->define_count) {
            Diagnostic(module->span, "zib.module",
                          "globals, state, and defines are outside the portable subset");
            return 0;
        }
        for(int f = 0; f < module->function_count; f++) {
            const ZirFunction *function = &module->functions[f];
            int binding_count = parse_parameters(module, function, bindings);
            if(!function->checked || function->is_extern ||
               function->is_closure || function->capture_count != 0 ||
               !portable_type(module, function->return_type) ||
               binding_count < 0) {
                Diagnostic(function->span, "zib.function",
                              "function is outside the portable subset: %s",
                              function->name);
                return 0;
            }
            if(!verify_sequence(module, function, 0, function->stmt_count,
                                bindings, binding_count, 0, 0)) {
                Diagnostic(function->span, "zib.statement",
                           "statement is outside the portable subset in %s",
                           function->name);
                return 0;
            }
            if(strcmp(function->return_type, "void") != 0 &&
               !sequence_guarantees_return(function, 0,
                                           function->stmt_count, 0)) {
                Diagnostic(function->span, "zib.return",
                           "portable functions must return on every path: %s",
                           function->name);
                return 0;
            }
        }
    }
    return 1;
}

static Value run_function(Vm *vm, const ZirModule *module,
                          const ZirFunction *function, const Value *args,
                          int arg_count);

static Value *
record_field(Record *record, const char *name)
{
    if(record == NULL)
        return NULL;
    for(int i = 0; i < record->field_count; i++)
        if(strcmp(record->fields[i].field.name, name) == 0)
            return &record->fields[i].value;
    return NULL;
}

static Value *
assignment_slot(Frame *frame, int index, int depth)
{
    if(index < 0 || index >= frame->function->expr_count ||
       depth >= VM_MAX_DEPTH)
        return NULL;
    const ZirExpr *expression = &frame->function->exprs[index];
    if(expression->kind == ZIR_EXPR_IDENT) {
        for(int i = frame->local_count - 1; i >= 0; i--)
            if(strcmp(frame->locals[i].name, expression->name) == 0)
                return &frame->locals[i].value;
        return NULL;
    }
    if(expression->kind != ZIR_EXPR_MEMBER)
        return NULL;
    Value *base = assignment_slot(frame, expression->left, depth + 1);
    if(base == NULL || base->kind != VALUE_RECORD)
        return NULL;
    return record_field(base->record, expression->name);
}

static Value
binary_value(Vm *vm, const char *op, Value left, Value right,
             const char *left_type, const char *right_type)
{
    int real = left.kind == VALUE_REAL || right.kind == VALUE_REAL;
    int shift = strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0;
    int unsigned64 = strcmp(left_type, "u64") == 0 ||
                     (!shift && strcmp(right_type, "u64") == 0);
    int signed_wide = !unsigned64 &&
        (strcmp(left_type, "i64") == 0 ||
         (!shift && strcmp(right_type, "i64") == 0));
    uint64_t left_bits = integer_bits(left);
    uint64_t right_bits = integer_bits(right);
    double a = as_real(left), b = as_real(right);
    if(vm->failed || left.kind == VALUE_VOID || right.kind == VALUE_VOID)
        goto failed;
    if(left.kind == VALUE_STRING || right.kind == VALUE_STRING) {
        if(left.kind != VALUE_STRING || right.kind != VALUE_STRING)
            goto failed;
        int equal = left.length == right.length &&
                    (left.length == 0 ||
                     memcmp(left.data, right.data, left.length) == 0);
        if(strcmp(op, "==") == 0)
            return int_value(equal);
        if(strcmp(op, "!=") == 0)
            return int_value(!equal);
        goto failed;
    }
    if(left.kind == VALUE_ENUM || right.kind == VALUE_ENUM) {
        if(left.kind != VALUE_ENUM || right.kind != VALUE_ENUM ||
           left.enumeration != right.enumeration)
            goto failed;
        if(strcmp(op, "==") == 0)
            return int_value(left.integer == right.integer);
        if(strcmp(op, "!=") == 0)
            return int_value(left.integer != right.integer);
        goto failed;
    }
    if(strcmp(op, "==") == 0)
        return int_value(real ? a == b :
                         unsigned64 ? left_bits == right_bits :
                         left.integer == right.integer);
    if(strcmp(op, "!=") == 0)
        return int_value(real ? a != b :
                         unsigned64 ? left_bits != right_bits :
                         left.integer != right.integer);
    if(strcmp(op, "<") == 0)
        return int_value(real ? a < b :
                         unsigned64 ? left_bits < right_bits :
                         left.integer < right.integer);
    if(strcmp(op, "<=") == 0)
        return int_value(real ? a <= b :
                         unsigned64 ? left_bits <= right_bits :
                         left.integer <= right.integer);
    if(strcmp(op, ">") == 0)
        return int_value(real ? a > b :
                         unsigned64 ? left_bits > right_bits :
                         left.integer > right.integer);
    if(strcmp(op, ">=") == 0)
        return int_value(real ? a >= b :
                         unsigned64 ? left_bits >= right_bits :
                         left.integer >= right.integer);
    if(real) {
        if(strcmp(op, "+") == 0) return real_value(a + b);
        if(strcmp(op, "-") == 0) return real_value(a - b);
        if(strcmp(op, "*") == 0) return real_value(a * b);
        if(strcmp(op, "/") == 0 && b != 0.0) return real_value(a / b);
        goto failed;
    }
    if(bitwise_operator(op)) {
        if(unsigned64 || signed_wide) {
            if(strcmp(op, "&") == 0)
                return unsigned64 ? uint_value(left_bits & right_bits) :
                                    int_value(signed64(left_bits & right_bits));
            if(strcmp(op, "|") == 0)
                return unsigned64 ? uint_value(left_bits | right_bits) :
                                    int_value(signed64(left_bits | right_bits));
            if(strcmp(op, "^") == 0)
                return unsigned64 ? uint_value(left_bits ^ right_bits) :
                                    int_value(signed64(left_bits ^ right_bits));
            if((!right.unsigned64 && right.integer < 0) || right_bits >= 64)
                goto failed;
            if(strcmp(op, "<<") == 0)
                return unsigned64 ? uint_value(left_bits << right_bits) :
                                    int_value(signed64(left_bits << right_bits));
            if(unsigned64)
                return uint_value(left_bits >> right_bits);
            uint64_t shifted = left_bits >> right_bits;
            if((left_bits & UINT64_C(0x8000000000000000)) != 0 &&
               right_bits > 0)
                shifted |= UINT64_MAX << (64 - right_bits);
            return int_value(signed64(shifted));
        }
        uint32_t a_bits = (uint32_t)left_bits;
        uint32_t b_bits = (uint32_t)right_bits;
        if(strcmp(op, "&") == 0)
            return int_value(a_bits & b_bits);
        if(strcmp(op, "|") == 0)
            return int_value(a_bits | b_bits);
        if(strcmp(op, "^") == 0)
            return int_value(a_bits ^ b_bits);
        if((!right.unsigned64 && right.integer < 0) || right_bits >= 32)
            goto failed;
        unsigned amount = (unsigned)right_bits;
        if(strcmp(op, "<<") == 0)
            return int_value(a_bits << amount);
        uint32_t shifted = a_bits >> amount;
        if(strcmp(left_type, "u32") != 0 &&
           (a_bits & UINT32_C(0x80000000)) != 0 && amount > 0)
            shifted |= UINT32_MAX << (32 - amount);
        return int_value(shifted);
    }
    if(unsigned64) {
        if(strcmp(op, "+") == 0) return uint_value(left_bits + right_bits);
        if(strcmp(op, "-") == 0) return uint_value(left_bits - right_bits);
        if(strcmp(op, "*") == 0) return uint_value(left_bits * right_bits);
        if(strcmp(op, "/") == 0 && right_bits != 0)
            return uint_value(left_bits / right_bits);
        if(strcmp(op, "%") == 0 && right_bits != 0)
            return uint_value(left_bits % right_bits);
        goto failed;
    }
    if(signed_wide) {
        if(strcmp(op, "+") == 0)
            return int_value(signed64(left_bits + right_bits));
        if(strcmp(op, "-") == 0)
            return int_value(signed64(left_bits - right_bits));
        if(strcmp(op, "*") == 0)
            return int_value(signed64(left_bits * right_bits));
        if(right.integer == 0)
            goto failed;
        if(left.integer == INT64_MIN && right.integer == -1)
            return int_value(strcmp(op, "/") == 0 ? INT64_MIN : 0);
        if(strcmp(op, "/") == 0)
            return int_value(left.integer / right.integer);
        if(strcmp(op, "%") == 0)
            return int_value(left.integer % right.integer);
        goto failed;
    }
    if(strcmp(op, "+") == 0) return int_value(left.integer + right.integer);
    if(strcmp(op, "-") == 0) return int_value(left.integer - right.integer);
    if(strcmp(op, "*") == 0) return int_value(left.integer * right.integer);
    if(strcmp(op, "/") == 0 && right.integer != 0)
        return int_value(left.integer / right.integer);
    if(strcmp(op, "%") == 0 && right.integer != 0)
        return int_value(left.integer % right.integer);
failed:
    vm->failed = 1;
    return int_value(0);
}

static Value
eval(Frame *frame, int index, int depth)
{
    const ZirExpr *expression;
    Value value = int_value(0), left, right;
    if(frame->vm->failed || index < 0 || index >= frame->function->expr_count ||
       depth >= VM_MAX_DEPTH) {
        frame->vm->failed = 1;
        return value;
    }
    expression = &frame->function->exprs[index];
    switch(expression->kind) {
    case ZIR_EXPR_INT: {
        char *end;
        errno = 0;
        if(expression->text[0] == '-') {
            value = int_value(strtoll(expression->text, &end, 0));
        } else {
            unsigned long long bits = strtoull(expression->text, &end, 0);
            value = bits > INT64_MAX ? uint_value(bits) : int_value((int64_t)bits);
        }
        if(errno || end == expression->text || *end)
            frame->vm->failed = 1;
        break;
    }
    case ZIR_EXPR_FLOAT: {
        char *end;
        errno = 0;
        value = real_value(strtod(expression->text, &end));
        if(errno || end == expression->text || *end || !isfinite(value.real))
            frame->vm->failed = 1;
        break;
    }
    case ZIR_EXPR_STRING:
        value = literal_string(frame->vm, expression);
        break;
    case ZIR_EXPR_IDENT: {
        if(strcmp(expression->name, "true") == 0)
            return int_value(1);
        if(strcmp(expression->name, "false") == 0)
            return int_value(0);
        for(int i = frame->local_count - 1; i >= 0; i--) {
            if(strcmp(frame->locals[i].name, expression->name) == 0) {
                value = frame->locals[i].value;
                return coerce(frame->vm, frame->module, value,
                              expression->type);
            }
        }
        const ZirModule *owner = NULL;
        const ZirType *enumeration = NULL;
        int32_t number;
        if(ResolveEnumMember(frame->module, expression->name,
                             &owner, &enumeration) == 1 &&
           enum_member_value(enumeration, expression->name, &number))
            return coerce(frame->vm, frame->module,
                          int_value(number), expression->type);
        frame->vm->failed = 1;
        break;
    }
    case ZIR_EXPR_UNARY:
        right = eval(frame, expression->right, depth + 1);
        if(frame->vm->failed)
            break;
        if(strcmp(expression->op, "-") == 0)
            value = right.kind == VALUE_REAL ? real_value(-right.real) :
                    right.unsigned64 ? uint_value(UINT64_C(0) - right.bits) :
                                       int_value(signed64(UINT64_C(0) -
                                                          integer_bits(right)));
        else if(strcmp(expression->op, "+") == 0)
            value = right;
        else if(strcmp(expression->op, "!") == 0)
            value = int_value(!truthy(right));
        else if(strcmp(expression->op, "~") == 0 && right.kind == VALUE_INT)
            value = right.unsigned64 ? uint_value(~right.bits) :
                                       int_value(~right.integer);
        else
            frame->vm->failed = 1;
        break;
    case ZIR_EXPR_BINARY:
        left = eval(frame, expression->left, depth + 1);
        if(frame->vm->failed)
            break;
        if(strcmp(expression->op, "&&") == 0 && !truthy(left))
            return int_value(0);
        if(strcmp(expression->op, "||") == 0 && truthy(left))
            return int_value(1);
        right = eval(frame, expression->right, depth + 1);
        if(strcmp(expression->op, "&&") == 0)
            value = int_value(truthy(left) && truthy(right));
        else if(strcmp(expression->op, "||") == 0)
            value = int_value(truthy(left) || truthy(right));
        else
            value = binary_value(frame->vm, expression->op, left, right,
                frame->function->exprs[expression->left].type,
                frame->function->exprs[expression->right].type);
        break;
    case ZIR_EXPR_CAST:
        value = coerce(frame->vm, frame->module,
                       eval(frame, expression->right, depth + 1),
                       expression->name);
        break;
    case ZIR_EXPR_COMPOUND: {
        value = default_value(frame->vm, frame->module,
                              expression->name, depth + 1);
        if(frame->vm->failed || value.kind != VALUE_RECORD)
            break;
        int count = 0;
        for(int child = expression->first_child; child >= 0;
            child = frame->function->exprs[child].next_sibling) {
            if(child >= frame->function->expr_count ||
               ++count > VM_MAX_FIELDS) {
                frame->vm->failed = 1;
                break;
            }
            const ZirExpr *initializer = &frame->function->exprs[child];
            RecordField *field = NULL;
            for(int i = 0; i < value.record->field_count; i++) {
                if(strcmp(value.record->fields[i].field.name,
                          initializer->name) == 0) {
                    field = &value.record->fields[i];
                    break;
                }
            }
            if(field == NULL) {
                frame->vm->failed = 1;
                break;
            }
            Value initialized = eval(frame, initializer->right, depth + 1);
            if(frame->vm->failed)
                break;
            field->value = coerce(frame->vm, value.record->owner,
                                  initialized, field->field.type);
        }
        break;
    }
    case ZIR_EXPR_MEMBER: {
        left = eval(frame, expression->left, depth + 1);
        if(left.kind == VALUE_STRING &&
           strcmp(expression->name, "length") == 0) {
            value = int_value((int64_t)left.length);
            break;
        }
        Value *field = left.kind == VALUE_RECORD ?
                       record_field(left.record, expression->name) : NULL;
        if(field == NULL)
            frame->vm->failed = 1;
        else
            value = *field;
        break;
    }
    case ZIR_EXPR_INDEX:
        left = eval(frame, expression->left, depth + 1);
        right = eval(frame, expression->right, depth + 1);
        if(left.kind != VALUE_STRING || right.kind != VALUE_INT ||
           (!right.unsigned64 && right.integer < 0) ||
           integer_bits(right) >= left.length) {
            frame->vm->failed = 1;
            break;
        }
        value = int_value(left.data[integer_bits(right)]);
        break;
    case ZIR_EXPR_CONDITIONAL:
        left = eval(frame, expression->left, depth + 1);
        if(!frame->vm->failed)
            value = eval(frame, truthy(left) ? expression->right :
                          expression->third, depth + 1);
        break;
    case ZIR_EXPR_CALL: {
        Value args[VM_MAX_PARAMS];
        int count = 0;
        const ZirModule *owner = NULL;
        const ZirFunction *callee = NULL;
        int resolved = ResolveFunction(frame->module, expression->name,
                                       &owner, &callee);
        const ZirImport *external = resolved == 0 ?
            host_import(frame->module, expression->name) : NULL;
        if((resolved != 1 || callee == NULL) && external == NULL) {
            frame->vm->failed = 1;
            break;
        }
        for(int child = expression->first_child; child >= 0;
            child = frame->function->exprs[child].next_sibling) {
            if(count >= VM_MAX_PARAMS) {
                frame->vm->failed = 1;
                break;
            }
            args[count++] = eval(frame, child, depth + 1);
        }
        if(!frame->vm->failed) {
            if(external != NULL) {
                ZirFunction signature = {0};
                copy_text(signature.name, sizeof(signature.name),
                          external->name);
                copy_text(signature.args, sizeof(signature.args),
                          external->args);
                copy_text(signature.return_type,
                          sizeof(signature.return_type),
                          external->return_type);
                signature.is_extern = 1;
                signature.span = external->span;
                value = run_function(frame->vm, frame->module,
                                     &signature, args, count);
            } else {
                value = run_function(frame->vm, owner, callee, args, count);
            }
        }
        break;
    }
    default:
        frame->vm->failed = 1;
        break;
    }
    return coerce(frame->vm, frame->module, value, expression->type);
}

static Flow
execute_sequence(Frame *frame, int begin, int end, int depth,
                 Value *result)
{
    Vm *vm = frame->vm;
    const ZirFunction *function = frame->function;
    if(depth >= VM_MAX_DEPTH)
        return FLOW_ERROR;
    for(int s = begin; s < end && !vm->failed; s++) {
        const ZirStmt *statement = &function->stmts[s];
        int close;
        int saved_locals;
        Flow flow;
        if(++vm->steps > VM_MAX_STEPS)
            return FLOW_ERROR;
        switch(statement->kind) {
        case ZIR_STMT_DECL: {
            Value value = statement->expr_root >= 0 ?
                eval(frame, statement->expr_root, 0) :
                default_value(vm, frame->module, statement->type, 0);
            if(vm->failed || frame->local_count >= VM_MAX_LOCALS)
                return FLOW_ERROR;
            Local *local = &frame->locals[frame->local_count++];
            copy_text(local->name, sizeof(local->name), statement->name);
            copy_text(local->type, sizeof(local->type), statement->type);
            local->value = coerce(vm, frame->module, value, statement->type);
            break;
        }
        case ZIR_STMT_ASSIGN: {
            Value *slot = assignment_slot(frame, statement->lhs_root, 0);
            if(slot == NULL)
                return FLOW_ERROR;
            Value right = eval(frame, statement->expr_root, 0);
            if(vm->failed)
                return FLOW_ERROR;
            if(strcmp(statement->assignment_op, "=") != 0) {
                const char *operation = assignment_binary_operator(
                    statement->assignment_op);
                if(operation == NULL)
                    return FLOW_ERROR;
                right = binary_value(vm, operation, *slot, right,
                    function->exprs[statement->lhs_root].type,
                    function->exprs[statement->expr_root].type);
            }
            *slot = coerce(vm, frame->module, right,
                           function->exprs[statement->lhs_root].type);
            break;
        }
        case ZIR_STMT_RETURN:
            *result = statement->expr_root >= 0 ?
                eval(frame, statement->expr_root, 0) : int_value(0);
            return vm->failed ? FLOW_ERROR : FLOW_RETURN;
        case ZIR_STMT_EXPR:
        case ZIR_STMT_UNUSED:
            (void)eval(frame, statement->expr_root, 0);
            break;
        case ZIR_STMT_IF: {
            int branch = s;
            int executed = 0;
            do {
                const ZirStmt *head = &function->stmts[branch];
                close = statement_close(function, branch, end);
                if(close < 0)
                    return FLOW_ERROR;
                if(!executed && (head->expr_root < 0 ||
                                 truthy(eval(frame, head->expr_root, 0)))) {
                    if(vm->failed)
                        return FLOW_ERROR;
                    saved_locals = frame->local_count;
                    flow = execute_sequence(frame, branch + 1, close,
                                            depth + 1, result);
                    frame->local_count = saved_locals;
                    if(flow != FLOW_NEXT)
                        return flow;
                    executed = 1;
                }
                if(vm->failed)
                    return FLOW_ERROR;
                branch = close + 1;
            } while(branch < end &&
                    is_else_branch(&function->stmts[branch]));
            s = branch - 1;
            break;
        }
        case ZIR_STMT_WHILE:
            close = statement_close(function, s, end);
            if(close < 0)
                return FLOW_ERROR;
            saved_locals = frame->local_count;
            while(1) {
                if(++vm->steps > VM_MAX_STEPS)
                    return FLOW_ERROR;
                Value condition = eval(frame, statement->expr_root, 0);
                if(vm->failed)
                    return FLOW_ERROR;
                if(!truthy(condition))
                    break;
                frame->local_count = saved_locals;
                flow = execute_sequence(frame, s + 1, close, depth + 1,
                                        result);
                frame->local_count = saved_locals;
                if(flow == FLOW_RETURN || flow == FLOW_ERROR)
                    return flow;
                if(flow == FLOW_BREAK)
                    break;
            }
            s = close;
            break;
        case ZIR_STMT_BLOCK_OPEN:
            close = statement_close(function, s, end);
            if(close < 0)
                return FLOW_ERROR;
            saved_locals = frame->local_count;
            flow = execute_sequence(frame, s + 1, close, depth + 1, result);
            frame->local_count = saved_locals;
            if(flow != FLOW_NEXT)
                return flow;
            s = close;
            break;
        case ZIR_STMT_BREAK:
            return FLOW_BREAK;
        case ZIR_STMT_CONTINUE:
            return FLOW_CONTINUE;
        default:
            return FLOW_ERROR;
        }
    }
    return vm->failed ? FLOW_ERROR : FLOW_NEXT;
}

static Value
run_function(Vm *vm, const ZirModule *module, const ZirFunction *function,
             const Value *args, int arg_count)
{
    Frame frame = {0};
    Parameter parameters[VM_MAX_PARAMS];
    int count = parse_parameters(module, function, parameters);
    Value result = int_value(0);
    if(vm->failed || vm->depth >= VM_MAX_DEPTH || count != arg_count) {
        vm->failed = 1;
        return result;
    }
    if(function->is_extern) {
        VmHostValue host_args[VM_MAX_PARAMS] = {0};
        VmHostValue host_result = {0};
        if(vm->host == NULL) {
            Diagnostic(function->span, "zib.capability",
                       "missing host capability: %s:%s",
                       module->name, function->name);
            vm->failed = 1;
            return result;
        }
        for(int i = 0; i < count; i++) {
            Value argument = coerce(vm, module, args[i], parameters[i].type);
            host_args[i].type = parameters[i].type;
            host_args[i].integer = argument.integer;
            host_args[i].bits = argument.bits;
            host_args[i].real = argument.real;
            host_args[i].data = argument.data;
            host_args[i].length = argument.length;
            host_args[i].kind = argument.kind == VALUE_REAL ? VM_HOST_REAL :
                argument.kind == VALUE_STRING ? VM_HOST_STRING :
                (parameters[i].type[0] == 'u' ? VM_HOST_UNSIGNED :
                 VM_HOST_INTEGER);
        }
        host_result.type = function->return_type;
        if(vm->failed || !vm->host(vm->host_context, module->name,
                                   function->name, host_args, count,
                                   &host_result)) {
            vm->failed = 1;
            return result;
        }
        ValueKind expected = value_kind(function->return_type);
        VmHostValueKind expected_host = expected == VALUE_VOID ? VM_HOST_VOID :
            expected == VALUE_REAL ? VM_HOST_REAL :
            expected == VALUE_STRING ? VM_HOST_STRING :
            function->return_type[0] == 'u' ? VM_HOST_UNSIGNED :
            VM_HOST_INTEGER;
        if(host_result.kind != expected_host ||
           (expected_host == VM_HOST_STRING && host_result.data == NULL &&
            host_result.length != 0)) {
            vm->failed = 1;
            return result;
        }
        if(expected_host == VM_HOST_REAL)
            result = real_value(host_result.real);
        else if(expected_host == VM_HOST_STRING)
            result = string_value(host_result.data, host_result.length);
        else if(expected_host == VM_HOST_UNSIGNED)
            result = uint_value(host_result.bits);
        else if(expected_host == VM_HOST_INTEGER)
            result = int_value(host_result.integer);
        else
            result.kind = VALUE_VOID;
        return coerce(vm, module, result, function->return_type);
    }
    vm->depth++;
    frame.vm = vm;
    frame.module = module;
    frame.function = function;
    for(int i = 0; i < count; i++) {
        copy_text(frame.locals[i].name, sizeof(frame.locals[i].name),
                 parameters[i].name);
        copy_text(frame.locals[i].type, sizeof(frame.locals[i].type),
                 parameters[i].type);
        frame.locals[i].value = coerce(vm, module, args[i], parameters[i].type);
    }
    frame.local_count = count;
    Flow flow = execute_sequence(&frame, 0, function->stmt_count, 0, &result);
    if(flow == FLOW_ERROR || flow == FLOW_BREAK || flow == FLOW_CONTINUE ||
       (flow != FLOW_RETURN && strcmp(function->return_type, "void") != 0))
        vm->failed = 1;
    vm->depth--;
    return coerce(vm, module, result, function->return_type);
}

static void
free_records(Vm *vm)
{
    while(vm->records != NULL) {
        Record *next = vm->records->next;
        free(vm->records);
        vm->records = next;
    }
}

static void
free_strings(Vm *vm)
{
    while(vm->strings != NULL) {
        StringLiteral *next = vm->strings->next;
        free(vm->strings);
        vm->strings = next;
    }
}

int
VmRunWithHost(const ZirProgram *program, const char *entry_module,
              const char *entry_function, VmHostCall host, void *context,
              long long *result, int *has_result)
{
    const ZirModule *module;
    const ZirFunction *entry;
    Vm vm = {.host = host, .host_context = context};
    if(!VmVerify(program, entry_module, entry_function))
        return 0;
    entry = find_entry(program, entry_module, entry_function, &module);
    Value value = run_function(&vm, module, entry, NULL, 0);
    *result = value.integer;
    *has_result = strcmp(entry->return_type, "void") != 0;
    if(vm.failed) {
        Diagnostic(entry->span, "zib.runtime",
                      "portable execution failed");
        free_records(&vm);
        free_strings(&vm);
        return 0;
    }
    free_records(&vm);
    free_strings(&vm);
    return 1;
}

int
VmRun(const ZirProgram *program, const char *entry_module,
      const char *entry_function, long long *result, int *has_result)
{
    return VmRunWithHost(program, entry_module, entry_function,
                         NULL, NULL, result, has_result);
}
