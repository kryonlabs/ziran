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
    VALUE_ENUM,
    VALUE_RECORD
} ValueKind;

typedef struct Record Record;

typedef struct Value {
    ValueKind kind;
    int64_t integer;
    double real;
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
} Vm;

typedef struct Frame {
    Vm *vm;
    const ZirModule *module;
    const ZirFunction *function;
    Local locals[VM_MAX_LOCALS];
    int local_count;
} Frame;

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
       strcmp(type, "integer") == 0 || strcmp(type, "bool") == 0)
        return VALUE_INT;
    if(strcmp(type, "float") == 0 || strcmp(type, "f32") == 0 ||
       strcmp(type, "double") == 0 || strcmp(type, "f64") == 0 ||
       strcmp(type, "real") == 0)
        return VALUE_REAL;
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
    Value value = {.kind = VALUE_INT, .integer = integer};
    return value;
}

static Value
real_value(double real)
{
    Value value = {.kind = VALUE_REAL, .real = real};
    return value;
}

static Value
enum_value(const ZirType *type, int32_t integer)
{
    Value value = {.kind = VALUE_ENUM, .integer = integer,
                   .enumeration = type};
    return value;
}

static double
as_real(Value value)
{
    return value.kind == VALUE_REAL ? value.real : (double)value.integer;
}

static int
truthy(Value value)
{
    return value.kind == VALUE_REAL ? value.real != 0.0 : value.integer != 0;
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
    if(target == VALUE_INVALID) {
        const ZirType *record = FindType(module, type, NULL);
        if(record != NULL && record->is_enum &&
           value.kind != VALUE_RECORD && value.kind != VALUE_VOID &&
           value.kind != VALUE_INVALID) {
            if(value.kind == VALUE_REAL)
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
    if(value.kind == VALUE_RECORD || value.kind == VALUE_VOID ||
       value.kind == VALUE_INVALID) {
        vm->failed = 1;
        return int_value(0);
    }
    if(strcmp(type, "bool") == 0)
        return int_value(truthy(value));
    if(target == VALUE_REAL) {
        double number = as_real(value);
        if(strcmp(type, "float") == 0 || strcmp(type, "f32") == 0)
            number = (float)number;
        if(!isfinite(number))
            vm->failed = 1;
        return real_value(number);
    }
    if(value.kind == VALUE_REAL) {
        if(!isfinite(value.real) || value.real < INT32_MIN ||
           value.real >= (double)INT32_MAX + 1.0) {
            vm->failed = 1;
            return int_value(0);
        }
        return int_value((int32_t)value.real);
    }
    uint32_t bits = (uint32_t)value.integer;
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
        "&&", "||", NULL
    };
    for(int i = 0; supported[i] != NULL; i++)
        if(strcmp(op, supported[i]) == 0)
            return 1;
    return 0;
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
        long long number;
        errno = 0;
        number = strtoll(expression->text, &end, 0);
        return errno == 0 && end != expression->text && *end == 0 &&
               number >= INT32_MIN && number <= INT32_MAX;
    }
    case ZIR_EXPR_FLOAT: {
        char *end;
        double number;
        errno = 0;
        number = strtod(expression->text, &end);
        return errno == 0 && end != expression->text && *end == 0 &&
               isfinite(number);
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
               scalar_type(function->exprs[expression->right].type);
    case ZIR_EXPR_BINARY: {
        if(!binary_operator(expression->op) ||
           !verify_expression(module, function, bindings, binding_count,
                              expression->left, depth + 1) ||
           !verify_expression(module, function, bindings, binding_count,
                              expression->right, depth + 1))
            return 0;
        const char *left_type = function->exprs[expression->left].type;
        const char *right_type = function->exprs[expression->right].type;
        const ZirType *left_enum = FindType(module, left_type, NULL);
        const ZirType *right_enum = FindType(module, right_type, NULL);
        if(left_enum != NULL && left_enum->is_enum)
            return left_enum == right_enum &&
                   (strcmp(expression->op, "==") == 0 ||
                    strcmp(expression->op, "!=") == 0);
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
    case ZIR_EXPR_CONDITIONAL:
        return verify_expression(module, function, bindings, binding_count,
                                 expression->left, depth + 1) &&
               scalar_type(function->exprs[expression->left].type) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->third, depth + 1);
    case ZIR_EXPR_CALL:
        if(expression->name[0] == 0 ||
           ResolveFunction(module, expression->name, &owner, &callee) != 1 ||
           callee == NULL || callee->is_extern)
            return 0;
        for(int child = expression->first_child; child >= 0;
            child = function->exprs[child].next_sibling) {
            if(++children > VM_MAX_PARAMS ||
               !verify_expression(module, function, bindings, binding_count,
                                  child, depth + 1))
                return 0;
        }
        return parse_parameters(owner, callee, parameters) == children;
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
            if(statement->is_instance || !portable_type(module, statement->type) ||
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
                   strcmp(destination, "bool") == 0 ||
                   strcmp(destination, "void") == 0 ||
                   strcmp(source, "bool") == 0 ||
                   strcmp(source, "void") == 0 ||
                   (strcmp(operation, "%") == 0 &&
                    (value_kind(destination) != VALUE_INT ||
                     value_kind(source) != VALUE_INT)))
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
                        !scalar_type(function->exprs[head->expr_root].type))
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
               !scalar_type(function->exprs[statement->expr_root].type) ||
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
        for(int i = 0; i < module->import_count; i++)
            if(module->imports[i].kind != ZIR_IMPORT_HEADER ||
               module->imports[i].resolved_module == NULL) {
                Diagnostic(module->imports[i].span, "zib.capability",
                              "portable execution requires an included Ziran module");
                return 0;
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
binary_value(Vm *vm, const char *op, Value left, Value right)
{
    int real = left.kind == VALUE_REAL || right.kind == VALUE_REAL;
    double a = as_real(left), b = as_real(right);
    if(vm->failed || left.kind == VALUE_VOID || right.kind == VALUE_VOID)
        goto failed;
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
        return int_value(real ? a == b : left.integer == right.integer);
    if(strcmp(op, "!=") == 0)
        return int_value(real ? a != b : left.integer != right.integer);
    if(strcmp(op, "<") == 0)
        return int_value(real ? a < b : left.integer < right.integer);
    if(strcmp(op, "<=") == 0)
        return int_value(real ? a <= b : left.integer <= right.integer);
    if(strcmp(op, ">") == 0)
        return int_value(real ? a > b : left.integer > right.integer);
    if(strcmp(op, ">=") == 0)
        return int_value(real ? a >= b : left.integer >= right.integer);
    if(real) {
        if(strcmp(op, "+") == 0) return real_value(a + b);
        if(strcmp(op, "-") == 0) return real_value(a - b);
        if(strcmp(op, "*") == 0) return real_value(a * b);
        if(strcmp(op, "/") == 0 && b != 0.0) return real_value(a / b);
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
        value = int_value(strtoll(expression->text, &end, 0));
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
                                               int_value(-right.integer);
        else if(strcmp(expression->op, "+") == 0)
            value = right;
        else if(strcmp(expression->op, "!") == 0)
            value = int_value(!truthy(right));
        else if(strcmp(expression->op, "~") == 0 && right.kind == VALUE_INT)
            value = int_value(~right.integer);
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
            value = binary_value(frame->vm, expression->op, left, right);
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
        Value *field = left.kind == VALUE_RECORD ?
                       record_field(left.record, expression->name) : NULL;
        if(field == NULL)
            frame->vm->failed = 1;
        else
            value = *field;
        break;
    }
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
        if(ResolveFunction(frame->module, expression->name,
                           &owner, &callee) != 1 || callee == NULL) {
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
        if(!frame->vm->failed)
            value = run_function(frame->vm, owner, callee, args, count);
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
                right = binary_value(vm, operation, *slot, right);
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

int
VmRun(const ZirProgram *program, const char *entry_module,
         const char *entry_function, long long *result, int *has_result)
{
    const ZirModule *module;
    const ZirFunction *entry;
    Vm vm = {0};
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
        return 0;
    }
    free_records(&vm);
    return 1;
}
