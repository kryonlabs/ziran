#include "zir_serial.h"
#include "zir_diagnostic.h"
#include "zir_parse.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum FieldKind {
    FIELD_STRING,
    FIELD_INTEGER,
    FIELD_SPAN
} FieldKind;

typedef struct Field {
    size_t offset;
    size_t size;
    FieldKind kind;
} Field;

typedef struct Reader {
    FILE *in;
    const char *path;
    const char *problem;
    size_t budget;
} Reader;

#define STRING_FIELD(type, name) \
    {offsetof(type, name), sizeof(((type *)0)->name), FIELD_STRING}
#define INTEGER_FIELD(type, name) \
    {offsetof(type, name), sizeof(((type *)0)->name), FIELD_INTEGER}
#define SPAN_FIELD(type, name) \
    {offsetof(type, name), sizeof(((type *)0)->name), FIELD_SPAN}
#define FIELD_COUNT(fields) (sizeof(fields) / sizeof((fields)[0]))
#define ZIR_FORMAT_VERSION 3u

static const Field state_fields[] = {
    STRING_FIELD(ZirStateField, name), STRING_FIELD(ZirStateField, type),
    STRING_FIELD(ZirStateField, init), STRING_FIELD(ZirStateField, guard),
    SPAN_FIELD(ZirStateField, span)
};
static const Field import_fields[] = {
    INTEGER_FIELD(ZirImport, kind), INTEGER_FIELD(ZirImport, extern_kind),
    INTEGER_FIELD(ZirImport, is_public), STRING_FIELD(ZirImport, name),
    STRING_FIELD(ZirImport, target), STRING_FIELD(ZirImport, extern_symbol),
    STRING_FIELD(ZirImport, signature), STRING_FIELD(ZirImport, args),
    STRING_FIELD(ZirImport, return_type), INTEGER_FIELD(ZirImport, required),
    STRING_FIELD(ZirImport, guard), SPAN_FIELD(ZirImport, span)
};
static const Field statement_fields[] = {
    INTEGER_FIELD(ZirStmt, kind), STRING_FIELD(ZirStmt, text),
    STRING_FIELD(ZirStmt, callee), STRING_FIELD(ZirStmt, args),
    INTEGER_FIELD(ZirStmt, declared_block_call),
    INTEGER_FIELD(ZirStmt, is_instance), INTEGER_FIELD(ZirStmt, expr_root),
    INTEGER_FIELD(ZirStmt, lhs_root), STRING_FIELD(ZirStmt, name),
    STRING_FIELD(ZirStmt, type), STRING_FIELD(ZirStmt, assignment_op),
    SPAN_FIELD(ZirStmt, span)
};
static const Field expression_fields[] = {
    INTEGER_FIELD(ZirExpr, kind), INTEGER_FIELD(ZirExpr, is_function_value),
    STRING_FIELD(ZirExpr, slot_type), STRING_FIELD(ZirExpr, text),
    STRING_FIELD(ZirExpr, name), STRING_FIELD(ZirExpr, op),
    INTEGER_FIELD(ZirExpr, left), INTEGER_FIELD(ZirExpr, right),
    INTEGER_FIELD(ZirExpr, first_child), INTEGER_FIELD(ZirExpr, next_sibling),
    INTEGER_FIELD(ZirExpr, third), STRING_FIELD(ZirExpr, type),
    SPAN_FIELD(ZirExpr, span)
};
static const Field capture_fields[] = {
    INTEGER_FIELD(ZirCapture, is_instance), STRING_FIELD(ZirCapture, name),
    STRING_FIELD(ZirCapture, type)
};
static const Field function_fields[] = {
    STRING_FIELD(ZirFunction, name), STRING_FIELD(ZirFunction, args),
    STRING_FIELD(ZirFunction, return_type), INTEGER_FIELD(ZirFunction, exported),
    INTEGER_FIELD(ZirFunction, is_extern), INTEGER_FIELD(ZirFunction, extern_kind),
    INTEGER_FIELD(ZirFunction, is_closure),
    INTEGER_FIELD(ZirFunction, is_public), INTEGER_FIELD(ZirFunction, checked),
    INTEGER_FIELD(ZirFunction, uses_host), STRING_FIELD(ZirFunction, extern_target),
    STRING_FIELD(ZirFunction, extern_symbol), STRING_FIELD(ZirFunction, guard),
    SPAN_FIELD(ZirFunction, span)
};
static const Field global_fields[] = {
    STRING_FIELD(ZirGlobal, name), STRING_FIELD(ZirGlobal, type),
    STRING_FIELD(ZirGlobal, init), INTEGER_FIELD(ZirGlobal, is_static),
    STRING_FIELD(ZirGlobal, guard), SPAN_FIELD(ZirGlobal, span)
};
static const Field define_fields[] = {
    STRING_FIELD(ZirDefine, name), STRING_FIELD(ZirDefine, value),
    STRING_FIELD(ZirDefine, guard), SPAN_FIELD(ZirDefine, span)
};
static const Field assert_fields[] = {
    STRING_FIELD(ZirAssert, condition), STRING_FIELD(ZirAssert, message),
    INTEGER_FIELD(ZirAssert, known), INTEGER_FIELD(ZirAssert, value),
    STRING_FIELD(ZirAssert, guard), SPAN_FIELD(ZirAssert, span)
};
static const Field type_fields[] = {
    STRING_FIELD(ZirType, name), STRING_FIELD(ZirType, body),
    INTEGER_FIELD(ZirType, is_slot), INTEGER_FIELD(ZirType, is_enum),
    INTEGER_FIELD(ZirType, is_extern), STRING_FIELD(ZirType, guard),
    SPAN_FIELD(ZirType, span)
};
static const Field module_fields[] = {
    STRING_FIELD(ZirModule, name), STRING_FIELD(ZirModule, source_path),
    SPAN_FIELD(ZirModule, span)
};

static int
write_u32(FILE *out, uint32_t value)
{
    for(int i = 0; i < 4; i++) {
        if(fputc((int)(value & 255u), out) == EOF)
            return 0;
        value >>= 8;
    }
    return 1;
}

static int
read_u32(Reader *reader, uint32_t *value)
{
    uint32_t result = 0;
    for(int i = 0; i < 4; i++) {
        int byte = fgetc(reader->in);
        if(byte == EOF) {
            reader->problem = "truncated integer";
            return 0;
        }
        result |= (uint32_t)(unsigned char)byte << (8 * i);
    }
    *value = result;
    return 1;
}

static int
write_string(FILE *out, const char *value, size_t capacity)
{
    size_t length = strnlen(value, capacity);
    if(length == capacity || length > UINT32_MAX)
        return 0;
    return write_u32(out, (uint32_t)length) &&
           fwrite(value, 1, length, out) == length;
}

static int
read_string(Reader *reader, char *value, size_t capacity)
{
    uint32_t length;
    if(!read_u32(reader, &length))
        return 0;
    if(length >= capacity) {
        reader->problem = "string exceeds field limit";
        return 0;
    }
    if(fread(value, 1, length, reader->in) != length) {
        reader->problem = "truncated string";
        return 0;
    }
    if(memchr(value, '\0', length) != NULL) {
        reader->problem = "embedded NUL in string";
        return 0;
    }
    value[length] = '\0';
    return 1;
}

static int
write_span(FILE *out, const ZirSourceSpan *span)
{
    return write_string(out, span->path, sizeof(span->path)) &&
           write_u32(out, (uint32_t)span->line) &&
           write_u32(out, (uint32_t)span->column) &&
           write_u32(out, (uint32_t)span->end_line) &&
           write_u32(out, (uint32_t)span->end_column);
}

static int
read_span(Reader *reader, ZirSourceSpan *span)
{
    uint32_t numbers[4];
    if(!read_string(reader, span->path, sizeof(span->path)))
        return 0;
    for(int i = 0; i < 4; i++)
        if(!read_u32(reader, &numbers[i]))
            return 0;
    for(int i = 0; i < 4; i++) {
        if(numbers[i] > INT32_MAX) {
            reader->problem = "invalid source position";
            return 0;
        }
    }
    span->line = (int)numbers[0];
    span->column = (int)numbers[1];
    span->end_line = (int)numbers[2];
    span->end_column = (int)numbers[3];
    return 1;
}

static int
write_fields(FILE *out, const void *record, const Field *fields, size_t count)
{
    for(size_t i = 0; i < count; i++) {
        const char *value = (const char *)record + fields[i].offset;
        if(fields[i].kind == FIELD_STRING) {
            if(!write_string(out, value, fields[i].size))
                return 0;
        } else if(fields[i].kind == FIELD_INTEGER) {
            if(!write_u32(out, (uint32_t)*(const int *)value))
                return 0;
        } else if(!write_span(out, (const ZirSourceSpan *)value)) {
            return 0;
        }
    }
    return 1;
}

static int
read_fields(Reader *reader, void *record, const Field *fields, size_t count)
{
    for(size_t i = 0; i < count; i++) {
        char *value = (char *)record + fields[i].offset;
        if(fields[i].kind == FIELD_STRING) {
            if(!read_string(reader, value, fields[i].size))
                return 0;
        } else if(fields[i].kind == FIELD_INTEGER) {
            uint32_t number;
            if(!read_u32(reader, &number))
                return 0;
            *(int *)value = (int)(int32_t)number;
        } else if(!read_span(reader, (ZirSourceSpan *)value)) {
            return 0;
        }
    }
    return 1;
}

static int
write_records(FILE *out, const void *records, int count, size_t size,
              const Field *fields, size_t field_count)
{
    if(count < 0 || (count > 0 && records == NULL) ||
       !write_u32(out, (uint32_t)count))
        return 0;
    for(int i = 0; i < count; i++)
        if(!write_fields(out, (const char *)records + (size_t)i * size,
                         fields, field_count))
            return 0;
    return 1;
}

static void *
read_records(Reader *reader, int *count, size_t size,
             const Field *fields, size_t field_count)
{
    uint32_t length;
    if(!read_u32(reader, &length))
        return NULL;
    if(length > 100000 || size == 0 || length > reader->budget / size) {
        reader->problem = "record count exceeds IR limit";
        return NULL;
    }
    reader->budget -= (size_t)length * size;
    *count = (int)length;
    if(length == 0)
        return NULL;
    void *records = calloc(length, size);
    if(records == NULL) {
        reader->problem = "out of memory reading IR";
        return NULL;
    }
    for(uint32_t i = 0; i < length; i++) {
        if(!read_fields(reader, (char *)records + (size_t)i * size,
                        fields, field_count)) {
            free(records);
            return NULL;
        }
    }
    return records;
}

/* Arrays whose count names are not formed by adding _count to the pointer. */
#define WRITE_ARRAY(out, owner, pointer, count, fields) \
    write_records((out), (owner)->pointer, (owner)->count, \
                  sizeof(*(owner)->pointer), (fields), FIELD_COUNT(fields))
#define READ_ARRAY(reader, owner, pointer, count, fields) do { \
    (owner)->pointer = read_records((reader), &(owner)->count, \
                                     sizeof(*(owner)->pointer), (fields), \
                                     FIELD_COUNT(fields)); \
    if((reader)->problem != NULL) return 0; \
} while(0)

static int
write_function(FILE *out, const ZirFunction *function)
{
    return write_fields(out, function, function_fields, FIELD_COUNT(function_fields)) &&
           WRITE_ARRAY(out, function, captures, capture_count, capture_fields) &&
           WRITE_ARRAY(out, function, stmts, stmt_count, statement_fields) &&
           WRITE_ARRAY(out, function, exprs, expr_count, expression_fields);
}

static int
read_function(Reader *reader, ZirFunction *function)
{
    if(!read_fields(reader, function, function_fields, FIELD_COUNT(function_fields)))
        return 0;
    READ_ARRAY(reader, function, captures, capture_count, capture_fields);
    READ_ARRAY(reader, function, stmts, stmt_count, statement_fields);
    READ_ARRAY(reader, function, exprs, expr_count, expression_fields);
    function->stmt_cap = function->stmt_count;
    function->expr_cap = function->expr_count;
    return 1;
}

static int
write_module(FILE *out, const ZirModule *module)
{
    if(!write_fields(out, module, module_fields, FIELD_COUNT(module_fields)) ||
       !WRITE_ARRAY(out, module, globals, global_count, global_fields) ||
       !WRITE_ARRAY(out, module, defines, define_count, define_fields) ||
       !WRITE_ARRAY(out, module, asserts, assert_count, assert_fields) ||
       !WRITE_ARRAY(out, module, types, type_count, type_fields) ||
       !WRITE_ARRAY(out, module, state_fields, state_count, state_fields) ||
       !WRITE_ARRAY(out, module, imports, import_count, import_fields) ||
       !write_u32(out, (uint32_t)module->function_count))
        return 0;
    for(int i = 0; i < module->function_count; i++)
        if(!write_function(out, &module->functions[i]))
            return 0;
    return 1;
}

static int
read_module(Reader *reader, ZirModule *module)
{
    uint32_t count;
    if(!read_fields(reader, module, module_fields, FIELD_COUNT(module_fields)))
        return 0;
    READ_ARRAY(reader, module, globals, global_count, global_fields);
    READ_ARRAY(reader, module, defines, define_count, define_fields);
    READ_ARRAY(reader, module, asserts, assert_count, assert_fields);
    READ_ARRAY(reader, module, types, type_count, type_fields);
    READ_ARRAY(reader, module, state_fields, state_count, state_fields);
    READ_ARRAY(reader, module, imports, import_count, import_fields);
    if(!read_u32(reader, &count))
        return 0;
    if(count > 100000 || count > reader->budget / sizeof(ZirFunction)) {
        reader->problem = "function count exceeds IR limit";
        return 0;
    }
    reader->budget -= (size_t)count * sizeof(ZirFunction);
    module->function_count = module->function_cap = (int)count;
    module->functions = calloc(count ? count : 1, sizeof(ZirFunction));
    if(module->functions == NULL) {
        reader->problem = "out of memory reading functions";
        return 0;
    }
    for(uint32_t i = 0; i < count; i++)
        if(!read_function(reader, &module->functions[i]))
            return 0;
    module->global_cap = module->global_count;
    module->define_cap = module->define_count;
    module->assert_cap = module->assert_count;
    module->type_cap = module->type_count;
    module->state_cap = module->state_count;
    module->import_cap = module->import_count;
    return 1;
}

static int
reference_valid(int index, int count)
{
    return index >= -1 && index < count;
}

static int
validate_program(const ZirProgram *program)
{
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        if(!module->name[0] || !module->source_path[0])
            return 0;
        for(int i = 0; i < module->import_count; i++)
            if(module->imports[i].kind < ZIR_IMPORT_HEADER ||
               module->imports[i].kind > ZIR_IMPORT_HOST)
                return 0;
        for(int f = 0; f < module->function_count; f++) {
            const ZirFunction *function = &module->functions[f];
            if(!function->name[0])
                return 0;
            for(int s = 0; s < function->stmt_count; s++) {
                const ZirStmt *statement = &function->stmts[s];
                if(statement->kind <= ZIR_STMT_UNKNOWN ||
                   statement->kind > ZIR_STMT_BLOCK_CALL ||
                   !reference_valid(statement->expr_root, function->expr_count) ||
                   !reference_valid(statement->lhs_root, function->expr_count))
                    return 0;
            }
            for(int e = 0; e < function->expr_count; e++) {
                const ZirExpr *expression = &function->exprs[e];
                if(expression->kind <= ZIR_EXPR_UNKNOWN ||
                   expression->kind > ZIR_EXPR_SLICE ||
                   !reference_valid(expression->left, function->expr_count) ||
                   !reference_valid(expression->right, function->expr_count) ||
                   !reference_valid(expression->first_child, function->expr_count) ||
                   !reference_valid(expression->next_sibling, function->expr_count) ||
                   !reference_valid(expression->third, function->expr_count))
                    return 0;
            }
        }
    }
    return 1;
}

int
ZirProgramWriteZir(const ZirProgram *program, FILE *out)
{
    static const unsigned char magic[4] = {'Z', 'I', 'R', 0};
    if(program == NULL || out == NULL || !validate_program(program) ||
       fwrite(magic, 1, sizeof(magic), out) != sizeof(magic) ||
       !write_u32(out, ZIR_FORMAT_VERSION) ||
       !write_u32(out, (uint32_t)program->module_count))
        return 0;
    for(int i = 0; i < program->module_count; i++)
        if(!write_module(out, &program->modules[i]))
            return 0;
    return fflush(out) == 0;
}

ZirProgram *
ZirProgramReadZir(FILE *in, const char *path)
{
    unsigned char magic[4];
    uint32_t version, count;
    Reader reader = {in, path, NULL, 256u * 1024u * 1024u};
    ZirProgram *program = NULL;
    if(in == NULL || path == NULL)
        return NULL;
    if(fread(magic, 1, sizeof(magic), in) != sizeof(magic) ||
       memcmp(magic, "ZIR\0", sizeof(magic)) != 0) {
        reader.problem = "invalid ZIR signature";
        goto failed;
    }
    if(!read_u32(&reader, &version))
        goto failed;
    if(version != ZIR_FORMAT_VERSION) {
        reader.problem = "unsupported ZIR version";
        goto failed;
    }
    if(!read_u32(&reader, &count))
        goto failed;
    if(count == 0 || count > 100000 || count > reader.budget / sizeof(ZirModule)) {
        reader.problem = "invalid module count";
        goto failed;
    }
    reader.budget -= (size_t)count * sizeof(ZirModule);
    program = ZirProgramNew();
    if(program == NULL) {
        reader.problem = "out of memory reading IR";
        goto failed;
    }
    program->module_count = program->module_cap = (int)count;
    program->modules = calloc(count, sizeof(ZirModule));
    if(program->modules == NULL) {
        reader.problem = "out of memory reading modules";
        goto failed;
    }
    for(uint32_t i = 0; i < count; i++)
        if(!read_module(&reader, &program->modules[i]))
            goto failed;
    if(fgetc(in) != EOF || ferror(in)) {
        reader.problem = "trailing or unreadable ZIR data";
        goto failed;
    }
    if(!validate_program(program)) {
        reader.problem = "invalid checked IR structure";
        goto failed;
    }
    return program;
failed:
    ZirDiagnostic(ZirSpan(path, 1, 1), "zir.invalid", "%s",
                  reader.problem ? reader.problem : "truncated ZIR data");
    ZirProgramFree(program);
    return NULL;
}

int
ZirPathIsZir(const char *path)
{
    size_t length = path ? strlen(path) : 0;
    return length > 4 && strcmp(path + length - 4, ".zir") == 0;
}

ZirProgram *
ZirProgramLoad(const char *path, const char *root)
{
    ZirProgram *program;
    FILE *file;
    if(!ZirPathIsZir(path))
        return zir_parse_file(path, root);
    file = fopen(path, "rb");
    if(file == NULL) {
        ZirDiagnostic(ZirSpan(path, 1, 1), "zir.input", "cannot open IR input");
        return NULL;
    }
    program = ZirProgramReadZir(file, path);
    fclose(file);
    return program;
}
