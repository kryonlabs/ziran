#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_laws.h"
#include "zir_serial.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ZIB_VERSION = 2, ZIB_MAX_IR_BYTES = 256 * 1024 * 1024,
       ZIB_MAX_CAPABILITIES = 4096 };

typedef struct CapabilityName {
    char module[ZIR_NAME_MAX];
    char function[ZIR_NAME_MAX];
} CapabilityName;

static int
count_capabilities(const ZirProgram *program)
{
    int count = 0;
    for(int m = 0; m < program->module_count; m++)
        for(int i = 0; i < program->modules[m].import_count; i++)
            count += program->modules[m].imports[i].kind == ZIR_IMPORT_EXTERN;
    return count;
}

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
read_u32(FILE *in, uint32_t *value)
{
    uint32_t result = 0;
    for(int i = 0; i < 4; i++) {
        int byte = fgetc(in);
        if(byte == EOF)
            return 0;
        result |= (uint32_t)(unsigned char)byte << (8 * i);
    }
    *value = result;
    return 1;
}

static int
write_name(FILE *out, const char *name)
{
    size_t length = strnlen(name, ZIR_NAME_MAX);
    return length > 0 && length < ZIR_NAME_MAX &&
           write_u32(out, (uint32_t)length) &&
           fwrite(name, 1, length, out) == length;
}

static int
read_name(FILE *in, char *name, size_t capacity)
{
    uint32_t length;
    if(!read_u32(in, &length) || length == 0 || length >= capacity)
        return 0;
    if(fread(name, 1, length, in) != length || memchr(name, 0, length))
        return 0;
    name[length] = 0;
    return 1;
}

static int
copy_bytes(FILE *in, FILE *out, uint32_t count)
{
    unsigned char buffer[8192];
    while(count > 0) {
        size_t amount = count < sizeof(buffer) ? count : sizeof(buffer);
        if(fread(buffer, 1, amount, in) != amount ||
           fwrite(buffer, 1, amount, out) != amount)
            return 0;
        count -= (uint32_t)amount;
    }
    return 1;
}

static int
copy_function(ZirFunction *target, const ZirFunction *source)
{
    *target = *source;
    target->stmts = NULL;
    target->exprs = NULL;
    target->captures = NULL;
    if(source->stmt_count > 0) {
        target->stmts = malloc((size_t)source->stmt_count * sizeof(*target->stmts));
        if(target->stmts == NULL)
            return 0;
        memcpy(target->stmts, source->stmts,
               (size_t)source->stmt_count * sizeof(*target->stmts));
    }
    if(source->expr_count > 0) {
        target->exprs = malloc((size_t)source->expr_count * sizeof(*target->exprs));
        if(target->exprs == NULL)
            return 0;
        memcpy(target->exprs, source->exprs,
               (size_t)source->expr_count * sizeof(*target->exprs));
    }
    if(source->capture_count > 0) {
        target->captures = malloc((size_t)source->capture_count *
                                  sizeof(*target->captures));
        if(target->captures == NULL)
            return 0;
        memcpy(target->captures, source->captures,
               (size_t)source->capture_count * sizeof(*target->captures));
    }
    return 1;
}

static int
mark_type_pointer(const ZirProgram *program, const ZirModule *owner,
                  const ZirType *type, unsigned char **keep_types,
                  int *changed)
{
    for(int m = 0; m < program->module_count; m++) {
        if(owner != &program->modules[m])
            continue;
        for(int t = 0; t < owner->type_count; t++)
            if(type == &owner->types[t]) {
                if(!keep_types[m][t]) {
                    keep_types[m][t] = 1;
                    *changed = 1;
                }
                return 1;
            }
    }
    return 0;
}

static int
mark_type(const ZirProgram *program, const ZirModule *scope,
          const char *name, unsigned char **keep_types, int *changed)
{
    const ZirModule *owner = NULL;
    const ZirType *type = FindType(scope, name, &owner);
    return type == NULL ||
           mark_type_pointer(program, owner, type, keep_types, changed);
}

static int
mark_parameters(const ZirProgram *program, const ZirModule *module,
                const ZirFunction *function, unsigned char **keep_types,
                int *changed)
{
    const char *cursor = function->args;
    while(*cursor) {
        const char *colon = strchr(cursor, ':');
        if(colon == NULL)
            return 0;
        cursor = colon + 1;
        while(isspace((unsigned char)*cursor))
            cursor++;
        char type[ZIR_NAME_MAX];
        size_t length = 0;
        while((isalnum((unsigned char)*cursor) || *cursor == '_') &&
              length + 1 < sizeof(type))
            type[length++] = *cursor++;
        type[length] = 0;
        if(length == 0 || !mark_type(program, module, type,
                                      keep_types, changed))
            return 0;
        cursor = strchr(cursor, ',');
        if(cursor == NULL)
            break;
        cursor++;
    }
    return 1;
}

static int
import_is_used(const ZirProgram *program, const ZirModule *module,
               const unsigned char *keep, unsigned char **keep_types,
               const ZirImport *import)
{
    if(import->kind == ZIR_IMPORT_EXTERN) {
        for(int f = 0; f < module->function_count; f++)
            if(keep[f])
                for(int e = 0; e < module->functions[f].expr_count; e++)
                    if(module->functions[f].exprs[e].kind == ZIR_EXPR_CALL &&
                       strcmp(module->functions[f].exprs[e].name,
                              import->name) == 0)
                        return 1;
        return 0;
    }
    if(import->kind != ZIR_IMPORT_HEADER ||
       import->resolved_module == NULL)
        return 0;
    for(int f = 0; f < module->function_count; f++) {
        if(!keep[f])
            continue;
        const ZirFunction *function = &module->functions[f];
        for(int e = 0; e < function->expr_count; e++) {
            const ZirModule *owner = NULL;
            const ZirFunction *callee = NULL;
            if(function->exprs[e].kind == ZIR_EXPR_CALL &&
               ResolveFunction(module, function->exprs[e].name,
                               &owner, &callee) == 1 &&
               owner == import->resolved_module && callee != NULL)
                return 1;
        }
    }
    for(int m = 0; m < program->module_count; m++) {
        if(import->resolved_module != &program->modules[m])
            continue;
        for(int t = 0; t < program->modules[m].type_count; t++)
            if(keep_types[m][t])
                return 1;
        break;
    }
    return 0;
}

ZirProgram *
BundleLink(const ZirProgram *program, const char *entry_module,
           const char *entry_function)
{
    unsigned char **keep = NULL;
    unsigned char **keep_types = NULL;
    ZirProgram *linked = NULL;
    int entry_m = -1, entry_f = -1;
    int selected_modules = 0;
    if(program == NULL || program->module_count <= 0 ||
       entry_module == NULL || entry_function == NULL)
        return NULL;
    keep = calloc((size_t)program->module_count, sizeof(*keep));
    keep_types = calloc((size_t)program->module_count, sizeof(*keep_types));
    if(keep == NULL || keep_types == NULL)
        goto failed;
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        for(int other = 0; other < m; other++)
            if(strcmp(program->modules[other].name, module->name) == 0) {
                Diagnostic(module->span, "zib.module",
                           "duplicate module identity: %s", module->name);
                goto failed;
            }
        keep[m] = calloc((size_t)(module->function_count > 0 ?
                                  module->function_count : 1), 1);
        keep_types[m] = calloc((size_t)(module->type_count > 0 ?
                                        module->type_count : 1), 1);
        if(keep[m] == NULL || keep_types[m] == NULL)
            goto failed;
        if(strcmp(module->name, entry_module) != 0)
            continue;
        for(int f = 0; f < module->function_count; f++)
            if(strcmp(module->functions[f].name, entry_function) == 0) {
                if(entry_f >= 0) {
                    Diagnostic(module->functions[f].span, "zib.entry",
                               "ambiguous bundle entry: %s", entry_function);
                    goto failed;
                }
                entry_m = m;
                entry_f = f;
            }
    }
    if(entry_m < 0) {
        Diagnostic(Span("<bundle>", 1, 1), "zib.entry",
                   "bundle entry was not found: %s:%s",
                   entry_module, entry_function);
        goto failed;
    }
    keep[entry_m][entry_f] = 1;
    for(int changed = 1; changed;) {
        changed = 0;
        for(int m = 0; m < program->module_count; m++) {
            const ZirModule *module = &program->modules[m];
            for(int f = 0; f < module->function_count; f++) {
                const ZirFunction *function = &module->functions[f];
                if(!keep[m][f])
                    continue;
                for(int e = 0; e < function->expr_count; e++) {
                    const ZirExpr *expression = &function->exprs[e];
                    const ZirModule *owner = NULL;
                    const ZirFunction *callee = NULL;
                    int target_m = -1, target_f = -1;
                    if(expression->kind != ZIR_EXPR_CALL)
                        continue;
                    if(ResolveFunction(module, expression->name,
                                       &owner, &callee) != 1 ||
                       owner == NULL || callee == NULL) {
                        if(expression->slot_type[0] != '\0') {
                            Diagnostic(expression->span, "zib.slot",
                                       "callable slots are outside the portable subset: %s",
                                       expression->name);
                            goto failed;
                        }
                        int external = 0;
                        for(int i = 0; i < module->import_count; i++)
                            if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
                               strcmp(module->imports[i].name,
                                      expression->name) == 0)
                                external = 1;
                        if(external)
                            continue;
                        Diagnostic(expression->span, "zib.call",
                                   "unresolved portable call: %s",
                                   expression->name);
                        goto failed;
                    }
                    for(int candidate = 0; candidate < program->module_count;
                        candidate++)
                        if(owner == &program->modules[candidate])
                            target_m = candidate;
                    if(target_m >= 0)
                        for(int candidate = 0;
                            candidate < program->modules[target_m].function_count;
                            candidate++)
                            if(callee == &program->modules[target_m].functions[candidate])
                                target_f = candidate;
                    if(target_f < 0) {
                        Diagnostic(expression->span, "zib.call",
                                   "call is outside the linked program: %s",
                                   expression->name);
                        goto failed;
                    }
                    if(!keep[target_m][target_f]) {
                        keep[target_m][target_f] = 1;
                        changed = 1;
                    }
                }
            }
        }
    }
    for(int changed = 1; changed;) {
        changed = 0;
        for(int m = 0; m < program->module_count; m++) {
            const ZirModule *module = &program->modules[m];
            for(int f = 0; f < module->function_count; f++) {
                const ZirFunction *function = &module->functions[f];
                if(!keep[m][f])
                    continue;
                if(!mark_type(program, module, function->return_type,
                              keep_types, &changed) ||
                   !mark_parameters(program, module, function,
                                    keep_types, &changed))
                    goto failed;
                for(int s = 0; s < function->stmt_count; s++)
                    if(!mark_type(program, module, function->stmts[s].type,
                                  keep_types, &changed))
                        goto failed;
                for(int e = 0; e < function->expr_count; e++) {
                    const ZirExpr *expression = &function->exprs[e];
                    if(!mark_type(program, module, expression->type,
                                  keep_types, &changed))
                        goto failed;
                    if((expression->kind == ZIR_EXPR_CAST ||
                        expression->kind == ZIR_EXPR_COMPOUND) &&
                       !mark_type(program, module, expression->name,
                                  keep_types, &changed))
                        goto failed;
                    if(expression->kind == ZIR_EXPR_IDENT) {
                        const ZirModule *owner = NULL;
                        const ZirType *type = NULL;
                        int resolved = ResolveEnumMember(module,
                            expression->name, &owner, &type);
                        if(resolved > 0 &&
                            !mark_type_pointer(program, owner, type,
                                               keep_types, &changed))
                            goto failed;
                    }
                }
            }
            for(int t = 0; t < module->type_count; t++) {
                if(!keep_types[m][t])
                    continue;
                if(module->types[t].is_enum || module->types[t].is_slot)
                    continue;
                size_t offset = 0;
                ZirTypeField field;
                int status;
                while((status = TypeNextField(&module->types[t], &offset,
                                              &field)) == 1)
                    if(!mark_type(program, module, field.type,
                                  keep_types, &changed))
                        goto failed;
                if(status < 0)
                    goto failed;
            }
        }
    }
    for(int m = 0; m < program->module_count; m++)
        if(memchr(keep[m], 1, (size_t)program->modules[m].function_count) ||
           memchr(keep_types[m], 1, (size_t)program->modules[m].type_count))
            selected_modules++;
    linked = ProgramNew();
    if(linked == NULL)
        goto failed;
    linked->modules = calloc((size_t)selected_modules, sizeof(*linked->modules));
    if(linked->modules == NULL)
        goto failed;
    linked->module_count = linked->module_cap = selected_modules;
    int out = 0;
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *source = &program->modules[m];
        ZirModule *target;
        int kept_functions = 0, kept_types = 0, kept_imports = 0;
        for(int f = 0; f < source->function_count; f++)
            kept_functions += keep[m][f] != 0;
        for(int t = 0; t < source->type_count; t++)
            kept_types += keep_types[m][t] != 0;
        if(kept_functions == 0 && kept_types == 0)
            continue;
        target = &linked->modules[out++];
        *target = *source;
        target->globals = NULL; target->global_count = target->global_cap = 0;
        target->state_fields = NULL; target->state_count = target->state_cap = 0;
        target->defines = NULL; target->define_count = target->define_cap = 0;
        target->asserts = NULL; target->assert_count = target->assert_cap = 0;
        target->types = NULL; target->type_count = target->type_cap = 0;
        target->imports = NULL; target->import_count = target->import_cap = 0;
        target->functions = NULL;
        target->function_count = target->function_cap = 0;
        if(kept_functions > 0) {
            target->functions = calloc((size_t)kept_functions,
                                       sizeof(*target->functions));
            if(target->functions == NULL)
                goto failed;
            target->function_count = target->function_cap = kept_functions;
            int next = 0;
            for(int f = 0; f < source->function_count; f++)
                if(keep[m][f] && !copy_function(&target->functions[next++],
                                               &source->functions[f]))
                    goto failed;
        }
        if(kept_types > 0) {
            target->types = calloc((size_t)kept_types, sizeof(*target->types));
            if(target->types == NULL)
                goto failed;
            target->type_count = target->type_cap = kept_types;
            int next = 0;
            for(int t = 0; t < source->type_count; t++)
                if(keep_types[m][t])
                    target->types[next++] = source->types[t];
        }
        for(int i = 0; i < source->import_count; i++)
            kept_imports += import_is_used(program, source, keep[m],
                                           keep_types, &source->imports[i]);
        if(kept_imports > 0) {
            target->imports = calloc((size_t)kept_imports,
                                     sizeof(*target->imports));
            if(target->imports == NULL)
                goto failed;
            target->import_count = target->import_cap = kept_imports;
            int next_import = 0;
            for(int i = 0; i < source->import_count; i++)
                if(import_is_used(program, source, keep[m], keep_types,
                                  &source->imports[i]))
                    target->imports[next_import++] = source->imports[i];
        }
    }
    if(!LinkImports(&linked, 1))
        goto failed;
    for(int m = 0; m < program->module_count; m++)
        free(keep[m]);
    for(int m = 0; m < program->module_count; m++)
        free(keep_types[m]);
    free(keep);
    free(keep_types);
    return linked;
failed:
    if(keep != NULL)
        for(int m = 0; m < program->module_count; m++)
            free(keep[m]);
    if(keep_types != NULL)
        for(int m = 0; m < program->module_count; m++)
            free(keep_types[m]);
    free(keep);
    free(keep_types);
    ProgramFree(linked);
    return NULL;
}

int
BundleWrite(FILE *out, const ZirProgram *program,
               const char *entry_module, const char *entry_function)
{
    FILE *payload;
    long length;
    int ok;
    if(out == NULL || program == NULL || entry_module == NULL ||
       entry_function == NULL)
        return 0;
    payload = tmpfile();
    if(payload == NULL)
        return 0;
    ok = ProgramWrite(program, payload);
    length = ok ? ftell(payload) : -1;
    if(length <= 0 || length > ZIB_MAX_IR_BYTES || fseek(payload, 0, SEEK_SET))
        ok = 0;
    int capabilities = count_capabilities(program);
    if(capabilities > ZIB_MAX_CAPABILITIES)
        ok = 0;
    if(ok) {
        ok = fwrite("ZIB\0", 1, 4, out) == 4 &&
             write_u32(out, ZIB_VERSION) &&
             write_name(out, entry_module) &&
             write_name(out, entry_function) &&
             write_u32(out, (uint32_t)capabilities);
        for(int m = 0; ok && m < program->module_count; m++)
            for(int i = 0; ok && i < program->modules[m].import_count; i++)
                if(program->modules[m].imports[i].kind == ZIR_IMPORT_EXTERN)
                    ok = write_name(out, program->modules[m].name) &&
                         write_name(out, program->modules[m].imports[i].name);
        ok = ok && write_u32(out, (uint32_t)length) &&
             copy_bytes(payload, out, (uint32_t)length) && fflush(out) == 0;
    }
    fclose(payload);
    return ok;
}

ZirProgram *
BundleRead(FILE *in, const char *path,
              char *entry_module, size_t module_size,
              char *entry_function, size_t function_size)
{
    unsigned char signature[4];
    uint32_t version, capability_count, length;
    FILE *payload = NULL;
    ZirProgram *program = NULL;
    CapabilityName *capabilities = NULL;
    const char *problem = "invalid or truncated bundle";
    if(in == NULL || path == NULL || entry_module == NULL ||
       entry_function == NULL)
        return NULL;
    if(fread(signature, 1, 4, in) != 4 || memcmp(signature, "ZIB\0", 4))
        goto failed;
    if(!read_u32(in, &version))
        goto failed;
    if(version != ZIB_VERSION) {
        problem = "unsupported ZIB version";
        goto failed;
    }
    if(!read_name(in, entry_module, module_size) ||
       !read_name(in, entry_function, function_size) ||
       !read_u32(in, &capability_count))
        goto failed;
    if(capability_count > ZIB_MAX_CAPABILITIES) {
        problem = "bundle has too many host capabilities";
        goto failed;
    }
    capabilities = calloc(capability_count ? capability_count : 1,
                          sizeof(*capabilities));
    if(capabilities == NULL)
        goto failed;
    for(uint32_t i = 0; i < capability_count; i++)
        if(!read_name(in, capabilities[i].module,
                      sizeof(capabilities[i].module)) ||
           !read_name(in, capabilities[i].function,
                      sizeof(capabilities[i].function)))
            goto failed;
    if(!read_u32(in, &length) || length == 0 || length > ZIB_MAX_IR_BYTES)
        goto failed;
    payload = tmpfile();
    if(payload == NULL || !copy_bytes(in, payload, length) ||
       fgetc(in) != EOF || ferror(in) || fseek(payload, 0, SEEK_SET))
        goto failed;
    program = ProgramRead(payload, path);
    if(program == NULL) {
        problem = "invalid embedded ZIR";
        goto failed;
    }
    if(capability_count != (uint32_t)count_capabilities(program)) {
        problem = "bundle capability list differs from linked IR";
        goto failed;
    }
    uint32_t next_capability = 0;
    for(int m = 0; m < program->module_count; m++)
        for(int i = 0; i < program->modules[m].import_count; i++)
            if(program->modules[m].imports[i].kind == ZIR_IMPORT_EXTERN) {
                if(strcmp(capabilities[next_capability].module,
                          program->modules[m].name) != 0 ||
                   strcmp(capabilities[next_capability].function,
                          program->modules[m].imports[i].name) != 0) {
                    problem = "bundle capability list differs from linked IR";
                    goto failed;
                }
                next_capability++;
            }
    if(!CheckCanonicalPrograms(&program, 1, 1, NULL) ||
       !CheckLaws(&program, 1)) {
        problem = "embedded ZIR failed semantic checking";
        goto failed;
    }
    fclose(payload);
    free(capabilities);
    return program;
failed:
    Diagnostic(Span(path, 1, 1), "zib.invalid", "%s", problem);
    if(payload != NULL)
        fclose(payload);
    free(capabilities);
    ProgramFree(program);
    return NULL;
}
