#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_laws.h"
#include "zir_serial.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ZIB_VERSION = 1, ZIB_MAX_IR_BYTES = 256 * 1024 * 1024 };

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
import_is_called(const ZirModule *module, const unsigned char *keep,
                 const ZirImport *import)
{
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
    return 0;
}

ZirProgram *
BundleLink(const ZirProgram *program, const char *entry_module,
           const char *entry_function)
{
    unsigned char **keep = NULL;
    ZirProgram *linked = NULL;
    int entry_m = -1, entry_f = -1;
    int selected_modules = 0;
    if(program == NULL || entry_module == NULL || entry_function == NULL)
        return NULL;
    keep = calloc((size_t)program->module_count, sizeof(*keep));
    if(keep == NULL)
        return NULL;
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
        if(keep[m] == NULL)
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
    for(int m = 0; m < program->module_count; m++)
        for(int f = 0; f < program->modules[m].function_count; f++)
            if(keep[m][f]) {
                selected_modules++;
                break;
            }
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
        int kept_functions = 0, kept_imports = 0;
        for(int f = 0; f < source->function_count; f++)
            kept_functions += keep[m][f] != 0;
        if(kept_functions == 0)
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
        for(int i = 0; i < source->import_count; i++)
            kept_imports += import_is_called(source, keep[m],
                                             &source->imports[i]);
        if(kept_imports > 0) {
            target->imports = calloc((size_t)kept_imports,
                                     sizeof(*target->imports));
            if(target->imports == NULL)
                goto failed;
            target->import_count = target->import_cap = kept_imports;
            int next_import = 0;
            for(int i = 0; i < source->import_count; i++)
                if(import_is_called(source, keep[m], &source->imports[i]))
                    target->imports[next_import++] = source->imports[i];
        }
    }
    if(!LinkImports(&linked, 1))
        goto failed;
    for(int m = 0; m < program->module_count; m++)
        free(keep[m]);
    free(keep);
    return linked;
failed:
    for(int m = 0; m < program->module_count; m++)
        free(keep[m]);
    free(keep);
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
    ok = ProgramWriteZir(program, payload);
    length = ok ? ftell(payload) : -1;
    if(length <= 0 || length > ZIB_MAX_IR_BYTES || fseek(payload, 0, SEEK_SET))
        ok = 0;
    if(ok) {
        ok = fwrite("ZIB\0", 1, 4, out) == 4 &&
             write_u32(out, ZIB_VERSION) &&
             write_name(out, entry_module) &&
             write_name(out, entry_function) &&
             write_u32(out, 0) && /* explicit host capability count */
             write_u32(out, (uint32_t)length) &&
             copy_bytes(payload, out, (uint32_t)length) &&
             fflush(out) == 0;
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
    if(capability_count != 0) {
        problem = "bundle requires unsupported host capabilities";
        goto failed;
    }
    if(!read_u32(in, &length) || length == 0 || length > ZIB_MAX_IR_BYTES)
        goto failed;
    payload = tmpfile();
    if(payload == NULL || !copy_bytes(in, payload, length) ||
       fgetc(in) != EOF || ferror(in) || fseek(payload, 0, SEEK_SET))
        goto failed;
    program = ProgramReadZir(payload, path);
    if(program == NULL) {
        problem = "invalid embedded ZIR";
        goto failed;
    }
    if(!CheckCanonicalPrograms(&program, 1, 1, NULL) ||
       !CheckLaws(&program, 1)) {
        problem = "embedded ZIR failed semantic checking";
        goto failed;
    }
    fclose(payload);
    return program;
failed:
    Diagnostic(Span(path, 1, 1), "zib.invalid", "%s", problem);
    if(payload != NULL)
        fclose(payload);
    ProgramFree(program);
    return NULL;
}
