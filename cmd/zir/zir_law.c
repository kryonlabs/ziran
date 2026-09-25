/* Named law obligations (LANGUAGE_DIRECTION.md): evaluation, gate, and the
 * machine-readable result schema shared by `ziran check` and bundles. */
#include "zir.h"
#include "zir_parse.h"
#include "zir_diagnostic.h"
#include "zir_check.h"
#include "zir_text.h"
#include "zir_law.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    LAW_PROVED = 0,
    LAW_DISPROVED,
    LAW_UNKNOWN
} LawStatus;

static const char *
law_status_name(LawStatus status)
{
    return status == LAW_PROVED ? "proved" :
           status == LAW_DISPROVED ? "disproved" : "unknown";
}

/* kind: type ------------------------------------------------------------ */

static LawStatus
evaluate_type_law(const ZirModule *module, const ZirLaw *law,
                  char *detail, size_t size)
{
    const ZirType *type = FindType(module, law->payload, NULL);
    if(type == NULL) {
        snprintf(detail, size, "no visible type named %s", law->payload);
        return LAW_DISPROVED;
    }
    if(type->is_record_template || type->is_extern ||
       type->is_procedure_type) {
        snprintf(detail, size,
                 "%s is generic or external; no concrete layout to check",
                 law->payload);
        return LAW_UNKNOWN;
    }
    if(type->is_enum) {
        if(EnumMemberValue(type, NULL, NULL)) {
            snprintf(detail, size, "%s is a concrete enum", law->payload);
            return LAW_PROVED;
        }
        snprintf(detail, size, "%s has no valid member", law->payload);
        return LAW_DISPROVED;
    }
    {
        size_t bytes, alignment;
        if(TypeLayout(module, law->payload, &bytes, &alignment)) {
            snprintf(detail, size, "%s lays out as %zu bytes", law->payload,
                     bytes);
            return LAW_PROVED;
        }
        snprintf(detail, size, "%s has an incomplete field layout",
                 law->payload);
        return LAW_DISPROVED;
    }
}

/* kind: bounds ---------------------------------------------------------- */

static LawStatus
evaluate_bounds_law(const ZirModule *module, const ZirLaw *law,
                    char *detail, size_t size)
{
    const char *payload = skip_ws(law->payload);
    const char *resolved = payload;
    char buffer[ZIR_TEXT_MAX];
    char element[ZIR_NAME_MAX];
    int capacity;
    for(int depth = 0; depth < 8; depth++) {
        if(resolved[0] == '[')
            break;
        {
            const ZirDefine *found = NULL;
            for(int d = 0; d < module->define_count; d++)
                if(strcmp(module->defines[d].name, resolved) == 0) {
                    found = &module->defines[d];
                    break;
                }
            if(found == NULL)
                break;
            resolved = skip_ws(found->value);
        }
    }
    if(strlen(resolved) >= sizeof(buffer) ||
       !ArrayElementType(resolved, element, sizeof(element), &capacity)) {
        snprintf(detail, size, "%s does not name a fixed-array type",
                 law->payload);
        return LAW_DISPROVED;
    }
    if(capacity > 0) {
        snprintf(detail, size, "%s has the resolved bound %d",
                 law->payload, capacity);
        return LAW_PROVED;
    }
    {
        const char *close = strchr(resolved, ']');
        char bound[ZIR_NAME_MAX];
        long value = 0;
        size_t length;
        if(close == NULL || (length = (size_t)(close - resolved - 1)) == 0 ||
            length >= sizeof(bound)) {
            snprintf(detail, size, "%s has a malformed bound",
                     law->payload);
            return LAW_DISPROVED;
        }
        memcpy(bound, resolved + 1, length);
        bound[length] = '\0';
        trim_in_place(bound);
        if(EvaluateCompileExpression(module, bound, law->span, 0, &value)) {
            if(value > 0) {
                snprintf(detail, size, "%s folds its bound to %ld",
                         law->payload, value);
                return LAW_PROVED;
            }
            snprintf(detail, size, "%s folds its bound to %ld",
                     law->payload, value);
            return LAW_DISPROVED;
        }
    }
    snprintf(detail, size,
             "%s has a bound the compile-time evaluator cannot fold",
             law->payload);
    return LAW_UNKNOWN;
}

/* kind: effect ---------------------------------------------------------- */

static int
statement_writes_global(const ZirModule *module, const ZirFunction *fn,
                        int index)
{
    const ZirStmt *st = &fn->stmts[index];
    const char *name;
    if(st->kind != ZIR_STMT_ASSIGN || st->lhs_root < 0)
        return 0;
    if(fn->exprs[st->lhs_root].kind != ZIR_EXPR_IDENT)
        return 0;
    name = fn->exprs[st->lhs_root].name;
    for(int g = 0; g < module->global_count; g++)
        if(strcmp(module->globals[g].name, name) == 0)
            return 1;
    return 0;
}

static int
body_calls_external(const ZirModule *module, const ZirFunction *fn)
{
    for(int e = 0; e < fn->expr_count; e++) {
        const ZirExpr *expr = &fn->exprs[e];
        const ZirModule *owner = NULL;
        const ZirFunction *callee = NULL;
        if(expr->kind != ZIR_EXPR_CALL || !expr->name[0])
            continue;
        if(ResolveFunction(module, expr->name, &owner, &callee) <= 0) {
            for(int i = 0; i < module->import_count; i++)
                if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
                   strcmp(module->imports[i].name, expr->name) == 0)
                    return 1;
            continue;
        }
        if(callee != NULL && callee->is_extern)
            return 1;
    }
    return 0;
}

static const ZirImport *
find_extern_import(const ZirModule *module, const char *name)
{
    for(int i = 0; i < module->import_count; i++)
        if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
           strcmp(module->imports[i].name, name) == 0)
            return &module->imports[i];
    return NULL;
}

static LawStatus
evaluate_effect_law(const ZirModule *module, const ZirLaw *law,
                    char *detail, size_t size)
{
    const ZirModule *owner = NULL;
    const ZirFunction *fn = NULL;
    const ZirImport *import = NULL;
    if(ResolveFunction(module, law->payload, &owner, &fn) <= 0 ||
        fn == NULL) {
        import = find_extern_import(module, law->payload);
        if(import != NULL) {
            snprintf(detail, size,
                     "%s is foreign; its effects are not visible",
                     law->payload);
            return LAW_UNKNOWN;
        }
        snprintf(detail, size, "no visible procedure named %s",
                 law->payload);
        return LAW_DISPROVED;
    }
    if(fn->is_extern) {
        snprintf(detail, size,
                 "%s is foreign; its effects are not visible", law->payload);
        return LAW_UNKNOWN;
    }
    if(body_calls_external(module, fn)) {
        snprintf(detail, size,
                 "%s calls foreign code; its effects are not visible",
                 law->payload);
        return LAW_UNKNOWN;
    }
    for(int s = 0; s < fn->stmt_count; s++)
        if(statement_writes_global(module, fn, s)) {
            snprintf(detail, size,
                     "%s writes a file-scope global", law->payload);
            return LAW_DISPROVED;
        }
    snprintf(detail, size,
             "%s reads only parameters and calls checked code",
             law->payload);
    return LAW_PROVED;
}

/* kind: abi ------------------------------------------------------------- */

/* Local portable-signature shape check; the VM's portable_type stays private
 * to its own translation unit. */
static int
portable_signature_type(const ZirModule *module, const char *type, int depth)
{
    const ZirType *record;
    const char *dot;
    char name[ZIR_NAME_MAX];
    if(depth >= 32)
        return 0;
    type = skip_ws(type);
    if(*ScalarType(type))
        return 1;
    dot = strchr(type, '.');
    if(dot != NULL && dot - type < (ptrdiff_t)sizeof(name)) {
        memcpy(name, type, (size_t)(dot - type));
        name[dot - type] = '\0';
        type = name;
    }
    record = FindType(module, type, NULL);
    if(record == NULL)
        return 0;
    if(record->is_enum)
        return EnumMemberValue(record, NULL, NULL);
    if(record->is_extern || record->is_record_template ||
       record->is_procedure_type || record->is_union)
        return 0;
    {
        size_t offset = 0;
        ZirTypeField field;
        int status;
        while((status = TypeNextField(record, &offset, &field)) == 1)
            if(!portable_signature_type(module, field.type, depth + 1))
                return 0;
        return status == 0;
    }
}

static LawStatus
evaluate_abi_law(const ZirModule *module, const ZirLaw *law,
                 char *detail, size_t size)
{
    const ZirModule *owner = NULL;
    const ZirFunction *fn = NULL;
    char parameters[64][ZIR_TEXT_MAX];
    int count;
    if(ResolveFunction(module, law->payload, &owner, &fn) <= 0 ||
        fn == NULL) {
        if(find_extern_import(module, law->payload) != NULL) {
            snprintf(detail, size,
                     "%s is foreign; its ABI is the host contract",
                     law->payload);
            return LAW_UNKNOWN;
        }
        snprintf(detail, size, "no visible procedure named %s",
                 law->payload);
        return LAW_DISPROVED;
    }
    if(fn->is_extern) {
        snprintf(detail, size,
                 "%s is foreign; its ABI is the host contract",
                 law->payload);
        return LAW_UNKNOWN;
    }
    count = *skip_ws(fn->args) ?
        split_top_level(fn->args, parameters[0], 64,
                        sizeof(parameters[0])) : 0;
    for(int p = 0; p < count; p++) {
        char *colon = strchr(parameters[p], ':');
        if(colon == NULL ||
           !portable_signature_type(module, skip_ws(colon + 1), 0)) {
            snprintf(detail, size,
                     "%s parameter %d is outside the portable ABI",
                     law->payload, p);
            return LAW_DISPROVED;
        }
    }
    if(!portable_signature_type(module, fn->return_type, 0)) {
        snprintf(detail, size, "%s result is outside the portable ABI",
                 law->payload);
        return LAW_DISPROVED;
    }
    snprintf(detail, size, "%s has a portable signature", law->payload);
    return LAW_PROVED;
}

/* kind: custom ---------------------------------------------------------- */

static LawStatus
evaluate_custom_law(const ZirModule *module, const ZirLaw *law,
                    char *detail, size_t size)
{
    long value = 0;
    if(!EvaluateCompileExpression(module, law->payload, law->span, 0,
                                  &value)) {
        snprintf(detail, size,
                 "payload is outside the compile-time evaluator");
        return LAW_UNKNOWN;
    }
    snprintf(detail, size, "payload evaluated to %ld", value);
    return value != 0 ? LAW_PROVED : LAW_DISPROVED;
}

int
EvaluateLaw(const ZirProgram *program, const ZirModule *module,
            const ZirLaw *law, char *detail, size_t size)
{
    LawStatus status;
    (void)program;
    detail[0] = '\0';
    if(!strcmp(law->kind, "type"))
        status = evaluate_type_law(module, law, detail, size);
    else if(!strcmp(law->kind, "bounds"))
        status = evaluate_bounds_law(module, law, detail, size);
    else if(!strcmp(law->kind, "effect"))
        status = evaluate_effect_law(module, law, detail, size);
    else if(!strcmp(law->kind, "abi"))
        status = evaluate_abi_law(module, law, detail, size);
    else if(!strcmp(law->kind, "custom"))
        status = evaluate_custom_law(module, law, detail, size);
    else {
        snprintf(detail, size, "no checker for kind %s", law->kind);
        status = LAW_UNKNOWN;
    }
    return (int)status;
}

const char *
LawStatusName(int status)
{
    return law_status_name((LawStatus)status);
}

static int
law_waived(const ZirProgram *program, const char *name)
{
    for(int p = 0; p < 1; p++)
        for(int m = 0; m < program->module_count; m++)
            for(int w = 0; w < program->modules[m].law_waiver_count; w++)
                if(strcmp(program->modules[m].law_waivers[w].name,
                          name) == 0)
                    return 1;
    return 0;
}

int
CheckLawGates(ZirProgram **programs, int count)
{
    int failures = 0;
    for(int p = 0; p < count; p++)
        for(int m = 0; m < programs[p]->module_count; m++) {
            const ZirModule *module = &programs[p]->modules[m];
            for(int l = 0; l < module->law_count; l++) {
                const ZirLaw *law = &module->laws[l];
                char detail[ZIR_TEXT_MAX];
                int status = EvaluateLaw(programs[p], module, law, detail,
                                         sizeof(detail));
                if(status == 0)
                    continue;
                if(status == 2 && law_waived(programs[p], law->name))
                    continue;
                Diagnostic(law->span, "law.gate",
                           "law %s is %s: %s", law->name,
                           law_status_name((LawStatus)status), detail);
                failures++;
            }
        }
    return failures == 0;
}

static void
print_json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for(const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if(*p == '"' || *p == '\\')
            fprintf(out, "\\%c", *p);
        else if(*p < 0x20)
            fprintf(out, "\\u%04x", *p);
        else
            fputc(*p, out);
    }
    fputc('"', out);
}

void
PrintLawResults(ZirProgram **programs, int count, FILE *out)
{
    if(out == NULL)
        return;
    for(int p = 0; p < count; p++)
        for(int m = 0; m < programs[p]->module_count; m++) {
            const ZirModule *module = &programs[p]->modules[m];
            for(int l = 0; l < module->law_count; l++) {
                const ZirLaw *law = &module->laws[l];
                char detail[ZIR_TEXT_MAX];
                int status = EvaluateLaw(programs[p], module, law, detail,
                                         sizeof(detail));
                fprintf(out, "{\"law\":");
                print_json_string(out, law->name);
                fprintf(out, ",\"kind\":");
                print_json_string(out, law->kind);
                fprintf(out, ",\"span\":{\"file\":");
                print_json_string(out, law->span.path);
                fprintf(out, ",\"line\":%d,\"column\":%d},",
                        law->span.line, law->span.column);
                fprintf(out, "\"status\":\"%s\",\"detail\":",
                        law_status_name((LawStatus)status));
                print_json_string(out, detail);
                fprintf(out, "}\n");
            }
            for(int w = 0; w < module->law_waiver_count; w++) {
                const ZirLawWaiver *waiver = &module->law_waivers[w];
                fprintf(out, "{\"law\":");
                print_json_string(out, waiver->name);
                fprintf(out, ",\"kind\":\"waiver\",\"span\":{\"file\":");
                print_json_string(out, waiver->span.path);
                fprintf(out, ",\"line\":%d,\"column\":%d},",
                        waiver->span.line, waiver->span.column);
                fprintf(out, "\"status\":\"waived\",\"detail\":");
                print_json_string(out, waiver->reason);
                fprintf(out, "}\n");
            }
        }
}
