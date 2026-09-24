#include "zir_emit.h"
#include "zir_check.h"
#include "zir_text.h"
#include "zir_expr.h"
#include "zir_diagnostic.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int
format(char *out, size_t size, const char *format_string, ...)
{
    va_list ap;
    va_start(ap,format_string);
    int n=vsnprintf(out,size,format_string,ap);
    va_end(ap);
    if(n<0 || (size_t)n>=size) {
        fprintf(stderr,"generated expression exceeds output limit\n"); exit(1);
    }
    return n;
}

int
ArrayValueType(const char *type)
{
    char element[ZIR_NAME_MAX];
    /* Host char buffers retain their explicit C-string interop convention. */
    return ArrayElementType(type, element, sizeof(element), NULL) &&
           strcmp(element, "char") != 0 && strcmp(element, "const char") != 0;
}

int
ModuleUsesSlices(const ZirModule *module)
{
    for(int i = 0; i < module->function_count; i++) {
        const ZirFunction *fn = &module->functions[i];
        if(SliceElementType(fn->return_type, NULL, 0) || strstr(fn->args, "[]") != NULL)
            return 1;
        for(int j = 0; j < fn->capture_count; j++)
            if(SliceElementType(fn->captures[j].type, NULL, 0))
                return 1;
        for(int j = 0; j < fn->stmt_count; j++)
            if(SliceElementType(fn->stmts[j].type, NULL, 0))
                return 1;
        for(int j = 0; j < fn->expr_count; j++)
            if(SliceElementType(fn->exprs[j].type, NULL, 0))
                return 1;
    }
    return 0;
}

/* These names share a deterministic collision check between declarations and
 * bodies. Negative parameter denotes the hidden array result. */
void
ArrayAbiName(const ZirFunction *fn, int parameter, char *out, size_t size)
{
    int serial = 0;
    int collision;
    do {
        format(out, size, "array_%s_%d_%d", parameter < 0 ? "result" : "input",
               parameter < 0 ? 0 : parameter, serial++);
        collision = strstr(fn->args, out) != NULL;
        for(int i = 0; i < fn->stmt_count; i++) {
            collision |= strstr(fn->stmts[i].text, out) != NULL;
            collision |= strstr(fn->stmts[i].args, out) != NULL;
        }
        for(int i = 0; i < fn->capture_count; i++)
            collision |= strstr(fn->captures[i].name, out) != NULL;
    } while(collision);
}

void
ArrayAbiArgs(const ZirFunction *fn, char *out, size_t size)
{
    if(fn->return_type[0] != '[' && strchr(fn->args, '[') == NULL) {
        copy_text(out, size, fn->args);
        return;
    }
    char parameters[64][ZIR_TEXT_MAX];
    int count = *skip_ws(fn->args) ?
        split_top_level(fn->args, parameters[0], 64, sizeof(parameters[0])) : 0;
    size_t used = 0;
    out[0] = '\0';
    if(ArrayElementType(fn->return_type, NULL, 0, NULL)) {
        char name[ZIR_NAME_MAX];
        ArrayAbiName(fn, -1, name, sizeof(name));
        used += (size_t)format(out, size, "%s: %s", name, fn->return_type);
    }
    for(int i = 0; i < count; i++) {
        char *colon = strchr(parameters[i], ':');
        if(colon != NULL && ArrayValueType(skip_ws(colon + 1))) {
            char name[ZIR_NAME_MAX];
            ArrayAbiName(fn, i, name, sizeof(name));
            used += (size_t)format(out + used, size - used, "%s%s: %s",
                                   used ? ", " : "", name, skip_ws(colon + 1));
        } else {
            used += (size_t)format(out + used, size - used, "%s%s",
                                   used ? ", " : "", parameters[i]);
        }
    }
}

static const char *
canonical(const char *type)
{
    const char *result = ScalarType(type);
    if(!strcmp(type, "integer")) return "i32";
    if(!strcmp(type, "real")) return "f64";
    return *result ? result : type;
}

const char *
TargetType(const char *type, ZirTarget target)
{
    static const struct { const char *type, *c, *go; } map[] = {
        {"i8", "int8_t", "int8"}, {"i16", "int16_t", "int16"},
        {"i32", "int32_t", "int32"}, {"i64", "int64_t", "int64"},
        {"u8", "uint8_t", "uint8"}, {"u16", "uint16_t", "uint16"},
        {"u32", "uint32_t", "uint32"}, {"u64", "uint64_t", "uint64"},
        {"f32", "float", "float32"}, {"f64", "double", "float64"},
        {"bool", "bool", "bool"}, {"void", "void", ""},
        {"char", "char", "byte"},
        {"string", "String", "string"},
        {"const char*", "const char*", "string"}, {NULL, NULL, NULL}
    };
    type = canonical(type);
    if(SliceElementType(type, NULL, 0) && (target == ZIR_C || target == ZIR_CPP))
        return "Slice";
    for(int i = 0; map[i].type; i++)
        if(!strcmp(type, map[i].type)) return target == ZIR_GO ? map[i].go : map[i].c;
    return NULL;
}

void
EmitSlotType(FILE *out, const ZirType *slot, ZirTarget target,
                ZirResolveTarget resolve_type, void *context)
{
    char parameters[64][ZIR_TEXT_MAX];
    int count = *skip_ws(slot->body) ?
        split_top_level(slot->body, parameters[0], 64, sizeof(parameters[0])) : 0;
    if(target == ZIR_GO)
        fprintf(out, "type %s func(", slot->name);
    else
        fprintf(out, "typedef struct %s {\n    void *context;\n    void (*call)(void *", slot->name);
    for(int i = 0; i < count; i++) {
        char *colon = strchr(parameters[i], ':');
        char type[ZIR_NAME_MAX];
        const char *source = skip_ws(colon + 1);
        const char *scalar = TargetType(source, target);
        copy_text(type, sizeof(type), scalar ? scalar : source);
        if(resolve_type)
            resolve_type(context, source, type, sizeof(type));
        fprintf(out, "%s%s", i || target != ZIR_GO ? ", " : "", type);
    }
    if(target == ZIR_GO)
        fputs(")\n\n", out);
    else
        fprintf(out, ");\n} %s;\n", slot->name);
}

static int width(const char *type) { return (*type == 'i' || *type == 'u') ? atoi(type + 1) : 0; }
static int signed_type(const char *type) { return *type == 'i'; }

static int
enum_type(const ZirModule *module, const char *type)
{
    const ZirType *declared = FindType(module, type, NULL);
    return declared != NULL && declared->is_enum;
}

static int
record_type(const ZirModule *module, const char *type)
{
    const ZirType *declared = FindType(module, type, NULL);
    return declared != NULL && !declared->is_enum && !declared->is_slot;
}

void
EmitStringType(FILE *out)
{
    fputs("\n#ifndef ZIR_STRING_VALUE_DEFINED\n#define ZIR_STRING_VALUE_DEFINED\n"
          "#include <stddef.h>\n#include <string.h>\n"
          "/* Immutable borrowed UTF-8 bytes; length preserves embedded nulls. */\n"
          "typedef struct String { const char *data; size_t length; } String;\n"
          "static inline String StringView(const char *data, size_t length) {\n"
          "    String value = {data, length};\n    return value;\n}\n"
          "static inline bool StringEqual(String a, String b) {\n"
          "    return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);\n"
          "}\n#endif\n", out);
}

static const char *
zero_value(const char *type, ZirTarget target)
{
    if(SliceElementType(type, NULL, 0))
        return target == ZIR_GO ? "nil" : "{0}";
    if(type[0] == '[')
        return "";
    if(!strcmp(type, "const char*"))
        return target == ZIR_C || target == ZIR_CPP ? "NULL" : "\"\"";
    if(type[0] != '[' && strchr(type, '*') != NULL)
        return target == ZIR_GO ? "nil" :
               target == ZIR_CPP ? "nullptr" : "((void *)0)";
    if(!strcmp(canonical(type), "string"))
        return target == ZIR_C || target == ZIR_CPP ? "StringView(NULL, 0)" : "\"\"";
    if(!strcmp(canonical(type), "bool")) return "false";
    return "0";
}

typedef struct TypePath {
    const ZirType *record;
    const struct TypePath *parent;
} TypePath;

static int
portable_type_path(const ZirModule *module, const char *type, const TypePath *path)
{
    const ZirModule *owner = NULL;
    const ZirType *record;
    size_t offset = 0;
    ZirTypeField field;
    int fields = 0;
    int status;

    char slice_element[ZIR_NAME_MAX];
    if(SliceElementType(type, slice_element, sizeof(slice_element)))
        return portable_type_path(module, slice_element, path);
    if(!strcmp(type, "null")) return 1;
    if(TargetType(type, ZIR_C)) return 1;
    if(strchr(type, '*') != NULL) return 1;
    {
        char element[ZIR_NAME_MAX];

        if(ArrayElementType(type, element, sizeof(element), NULL))
            return portable_type_path(module, element, path);
    }
    record = FindType(module, type, &owner);
    if(record == NULL)
        return 0;
    if(record->is_enum)
        return 1;
    if(record->is_slot) {
        char parameters[64][ZIR_TEXT_MAX];
        int count = *skip_ws(record->body) ?
            split_top_level(record->body, parameters[0], 64, sizeof(parameters[0])) : 0;
        for(int i = 0; i < count; i++) {
            char *colon = strchr(parameters[i], ':');
            if(colon == NULL || !portable_type_path(owner, skip_ws(colon + 1), path))
                return 0;
        }
        return 1;
    }
    for(const TypePath *ancestor = path; ancestor; ancestor = ancestor->parent) {
        if(ancestor->record == record)
            return 0;
    }
    TypePath current = {record, path};
    while((status = TypeNextField(record, &offset, &field)) == 1) {
        if(!portable_type_path(owner, field.type, &current) || !strcmp(field.type, "void"))
            return 0;
        fields++;
    }
    return status == 0 && fields > 0;
}

static int
portable_type(const ZirModule *module, const char *type)
{
    return portable_type_path(module, type, NULL);
}

static int
supported_expression(const ZirModule *module, const ZirFunction *fn, int index)
{
    const ZirExpr *e;
    if(index < 0) return 1;
    e = &fn->exprs[index];
    if(!portable_type(module, e->type)) return 0;
    switch(e->kind) {
    case ZIR_EXPR_COMPOUND:
        if(!record_type(module, e->name) && !ArrayElementType(e->name, NULL, 0, NULL)) return 0;
        break;
    case ZIR_EXPR_FIELD_INIT: break;
    case ZIR_EXPR_INT: case ZIR_EXPR_FLOAT: case ZIR_EXPR_IDENT: case ZIR_EXPR_STRING: break;
    case ZIR_EXPR_MEMBER: case ZIR_EXPR_INDEX: case ZIR_EXPR_SLICE: break;
    case ZIR_EXPR_BINARY: case ZIR_EXPR_CONDITIONAL: break;
    case ZIR_EXPR_UNARY:
        if(!strcmp(e->op, "&") || !strcmp(e->op, "*")) return 0;
        break;
    case ZIR_EXPR_POSTFIX: break;
    case ZIR_EXPR_CAST:
        if(!TargetType(e->name, ZIR_C) && !enum_type(module, e->name)) return 0;
        break;
    case ZIR_EXPR_CALL: if(!e->name[0]) return 0; break;
    default: return 0;
    }
    if(!supported_expression(module, fn, e->left) || !supported_expression(module, fn, e->right) ||
       !supported_expression(module, fn, e->third)) return 0;
    for(int child = e->first_child; child >= 0; child = fn->exprs[child].next_sibling)
        if(!supported_expression(module, fn, child)) return 0;
    return 1;
}

int
CanEmitBody(const ZirModule *module, const ZirFunction *fn)
{
    char params[64][ZIR_TEXT_MAX];
    int count;
    /* Eligibility follows the typed function body and its operations.
     * Host calls and unsupported composition fail the same checks. */
    if(!fn->checked || fn->is_extern || !portable_type(module, fn->return_type)) return 0;
    count = *skip_ws(fn->args) ? split_top_level(fn->args, params[0], 64, sizeof(params[0])) : 0;
    for(int i = 0; i < count; i++) {
        char *colon = strchr(params[i], ':');
        if(!colon || !portable_type(module, skip_ws(colon + 1))) return 0;
    }
    for(int i = 0; i < fn->stmt_count; i++) {
        const ZirStmt *st = &fn->stmts[i];
        switch(st->kind) {
        case ZIR_STMT_DECL: if(!portable_type(module, st->type)) return 0; break;
        case ZIR_STMT_ASSIGN:
            if(st->lhs_root < 0 || (fn->exprs[st->lhs_root].kind != ZIR_EXPR_IDENT &&
                fn->exprs[st->lhs_root].kind != ZIR_EXPR_MEMBER &&
                fn->exprs[st->lhs_root].kind != ZIR_EXPR_INDEX)) return 0;
            break;
        case ZIR_STMT_IF: if(!strncmp(st->text, "guard", 5)) return 0; break;
        case ZIR_STMT_BLOCK_OPEN: case ZIR_STMT_BLOCK_CLOSE:
        case ZIR_STMT_WHILE: case ZIR_STMT_RETURN: case ZIR_STMT_BREAK:
        case ZIR_STMT_CONTINUE: case ZIR_STMT_UNUSED: case ZIR_STMT_EXPR: break;
        default: return 0;
        }
        if(!supported_expression(module, fn, st->expr_root) || !supported_expression(module, fn, st->lhs_root)) return 0;
    }
    return 1;
}

static void
number_prefix(const ZirModule *module, char *out, size_t size)
{
    uint32_t hash = 2166136261u;
    for(const unsigned char *p = (const unsigned char *)module->source_path; *p; p++)
        hash = (hash ^ *p) * 16777619u;
    format(out, size, "number_%08x", hash);
}

void
EmitNumbers(FILE *out, const ZirModule *module, ZirTarget target)
{
    char p[64];
    int used = 0;
    for(int i = 0; i < module->function_count; i++) used |= CanEmitBody(module, &module->functions[i]);
    if(!used) return;
    number_prefix(module, p, sizeof(p));
    EmitNumberSupport(out, target, p);
}

void
EmitNumberSupport(FILE *out, ZirTarget target, const char *p)
{
    if(target == ZIR_C || target == ZIR_CPP) {
        fprintf(out, "#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\n\n");
        fprintf(out,
            "static inline int64_t %s_signed(uint64_t x, int w) {\n"
            "    uint64_t mask = w == 64 ? UINT64_MAX : (UINT64_C(1) << w) - 1;\n"
            "    x &= mask;\n"
            "    return x <= (mask >> 1) ? (int64_t)x : -1 - (int64_t)(mask - x);\n}\n", p);
        fprintf(out,
            "static inline uint64_t %s_bits(uint64_t a, uint64_t b, int w, int sign, int op) {\n"
            "    uint64_t mask = w == 64 ? UINT64_MAX : (UINT64_C(1) << w) - 1;\n"
            "    uint64_t shift = b; a &= mask; b &= mask;\n"
            "    switch(op) {\n"
            "    case 0: return a; case 1: return (a + b) & mask;\n"
            "    case 2: return (a - b) & mask; case 3: return (a * b) & mask;\n"
            "    case 4: case 5:\n"
            "        if(!b) abort();\n"
            "        if(sign) { int64_t x = %s_signed(a,w), y = %s_signed(b,w);\n"
            "            if(x == INT64_MIN && y == -1) return op == 4 ? a : 0;\n"
            "            return (uint64_t)(op == 4 ? x / y : x %% y) & mask; }\n"
            "        return op == 4 ? a / b : a %% b;\n"
            "    case 6: case 7:\n"
            "        if(shift >= (uint64_t)w) abort();\n"
            "        if(op == 6) return (a << shift) & mask;\n"
            "        if(!shift) return a;\n"
            "        return (a >> shift) | ((sign && (a & (UINT64_C(1) << (w-1)))) ? mask ^ (mask >> shift) : 0);\n"
            "    case 8: return a & b; case 9: return a | b; case 10: return a ^ b;\n"
            "    default: abort(); }\n    return 0;\n}\n\n", p, p, p);
        fprintf(out,
            "static inline uint64_t %s_float(double x, int w, int sign) {\n"
            "    double bound = 1; for(int i = 0; i < w-sign; i++) bound *= 2;\n"
            "    if(!(x >= (sign ? -bound : 0) && x < bound)) abort();\n"
            "    return sign ? (uint64_t)(int64_t)x : (uint64_t)x;\n}\n", p);
    } else if(target == ZIR_GO) {
        fprintf(out,
            "func %s_float(x float64, w uint, sign bool) uint64 {\n"
            "    bits := w; if sign { bits-- }; bound := float64(1); for i := uint(0); i < bits; i++ { bound *= 2 }; lower := float64(0); if sign { lower = -bound }\n"
            "    if !(x >= lower && x < bound) { panic(\"float conversion out of range\") }; if sign { return uint64(int64(x)) }; return uint64(x)\n}\n", p);
        fprintf(out,
            "func %s_bits(a, b uint64, w uint, sign bool, op int) uint64 {\n"
            "    mask := ^uint64(0); if w < 64 { mask = (uint64(1) << w) - 1 }; shift := b; a &= mask; b &= mask\n"
            "    switch op {\n"
            "    case 0: return a\n    case 1: return (a+b)&mask\n    case 2: return (a-b)&mask\n    case 3: return (a*b)&mask\n"
            "    case 4,5:\n        if b == 0 { panic(\"integer division by zero\") }\n"
            "        if sign { x := int64(a << (64-w)) >> (64-w); y := int64(b << (64-w)) >> (64-w); if op == 4 { return uint64(x/y)&mask }; return uint64(x%%y)&mask }; if op == 4 { return a/b }; return a%%b\n"
            "    case 6,7:\n        if shift >= uint64(w) { panic(\"invalid shift count\") }; if op == 6 { return (a<<shift)&mask }; if shift == 0 { return a }; result := a>>shift; if sign && (a & (uint64(1)<<(w-1))) != 0 { result |= mask ^ (mask>>shift) }; return result\n"
            "    case 8: return a&b\n    case 9: return a|b\n    case 10: return a^b\n    }; panic(\"invalid numeric operation\")\n}\n\n", p);
    }
}

typedef struct Local {
    char name[ZIR_NAME_MAX];
    int depth;
} Local;
typedef struct Emitter {
    FILE *out;
    const ZirModule *module;
    const ZirFunction *fn;
    ZirTarget target;
    ZirResolveTarget resolve;
    void *context;
    int indent, serial, depth, local_count;
    Local *locals;
    char numbers[64];
    /* Expression folding (Go target): "pure" marks the last emitted expression
     * as free of side effects, so it can be inlined into its consumer instead
     * of being captured in a value_N temporary. "minify" removes the inline
     * length bound for callers that want the densest possible output. */
    int pure;
    int minify;
} Emitter;

/* Bound for readable inlined expressions. Longer results stay in named
 * temporaries so the generated code keeps human-auditable steps. */
#define ZIR_INLINE_MAX 96

static int zir_minify_output;

/* Debug escape hatch: ZIR_NO_FOLD=1 restores the pre-folding temp-per-
 * expression output for A/B verification of the folded codegen. */
static int
disable_folding(void)
{
    const char *disable = getenv("ZIR_NO_FOLD");
    return disable != NULL && disable[0] != '\0' && strcmp(disable, "0") != 0;
}

void
EmitUseMinifiedOutput(int enabled)
{
    zir_minify_output = enabled != 0;
}

static int
plain_identifier(const char *text)
{
    if(!(*text == '_' || (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z')))
        return 0;
    for(const char *p = text + 1; *p; p++)
        if(!(*p == '_' || (*p >= 'a' && *p <= 'z') ||
             (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
            return 0;
    return 1;
}

/* A materialized temporary only re-copies a value that earlier statements
 * already captured, so identifiers always pass through. Pure expressions
 * inline while they stay short enough to read. */
static int
go_folds_text(const Emitter *e, const char *text)
{
    if(e->target != ZIR_GO)
        return 0;
    if(disable_folding())
        return 0;
    if(plain_identifier(text))
        return 1;
    return e->pure && (e->minify || strlen(text) <= ZIR_INLINE_MAX);
}

static void
line(Emitter *e, const char *format, ...)
{
    va_list ap;
    for(int i = 0; i < e->indent; i++) fputs("    ", e->out);
    va_start(ap, format); vfprintf(e->out, format, ap); va_end(ap);
    fputc('\n', e->out);
}

static void
fatal(const ZirExpr *expr, const char *message)
{
    fprintf(stderr, "%s:%d:%d: %s: %s\n", expr->span.path, expr->span.line,
            expr->span.column, message, expr->text);
    exit(1);
}

static void
fresh(Emitter *e, char *name)
{
    int collision;
    do {
        format(name, ZIR_NAME_MAX, "value_%d", e->serial++);
        collision = strstr(e->fn->args, name) != NULL;
        for(int i = 0; i < e->fn->capture_count; i++)
            collision |= !strcmp(e->fn->captures[i].name, name);
        for(int i = 0; i < e->fn->stmt_count; i++) collision |= strstr(e->fn->stmts[i].text, name) != NULL;
        for(int i = 0; i < e->module->state_count; i++) collision |= !strcmp(e->module->state_fields[i].name, name);
        for(int i = 0; i < e->module->global_count; i++) collision |= !strcmp(e->module->globals[i].name, name);
        for(int i = 0; i < e->module->define_count; i++) collision |= !strcmp(e->module->defines[i].name, name);
        for(int i = 0; i < e->module->function_count; i++) collision |= !strcmp(e->module->functions[i].name, name);
    } while(collision);
}

/* Stream the declared shape instead of expanding a nested record into a
 * bounded expression buffer. Copies retain C/Go's independent value semantics. */

/* Array expressions are values. Their source is captured before any write,
 * so even a self-assignment or an overlapping record destination is safe. */
static void
assign_value(Emitter *e, const char *destination, const char *type, const char *source)
{
    if(ArrayElementType(type, NULL, 0, NULL) &&
       (e->target == ZIR_C || e->target == ZIR_CPP)) {
        line(e, "memmove(%s, %s, sizeof(%s));", destination, source, destination);
    } else {
        line(e, "%s = %s%s", destination, source, e->target == ZIR_GO ? "" : ";");
    }
}

static void
declare_array(Emitter *e, const char *name, const char *type, const char *value)
{
    char element[ZIR_NAME_MAX];
    char target_element[ZIR_NAME_MAX * 2];
    char bound[ZIR_NAME_MAX];
    int capacity;

    ArrayElementType(type, element, sizeof(element), &capacity);
    const char *scalar = TargetType(element, e->target);
    if(scalar != NULL)
        copy_text(target_element, sizeof(target_element), scalar);
    else
        e->resolve(e->context, element, target_element, sizeof(target_element));
    if(capacity >= 0) {
        format(bound, sizeof(bound), "%d", capacity);
    } else {
        char symbol[ZIR_NAME_MAX];
        format(symbol, sizeof(symbol), "%.*s", (int)(strchr(type, ']') - type - 1), type + 1);
        e->resolve(e->context, symbol, bound, sizeof(bound));
    }
    if(e->target == ZIR_GO) {
        if(value != NULL && *value)
            line(e, "var %s [%s]%s = %s", name, bound, target_element, value);
        else
            line(e, "var %s [%s]%s", name, bound, target_element);
    } else if(e->target == ZIR_C || e->target == ZIR_CPP) {
        line(e, "%s %s[%s] = %s;", target_element, name, bound,
             e->target == ZIR_C ? "{0}" : "{}");
        if(value != NULL && *value)
            assign_value(e, name, type, value);
    } else {
        Diagnostic(e->fn->span, "emit.array_target", "array values are supported only by native targets");
        exit(1);
    }
}

static void
declare(Emitter *e, const char *name, const char *type, const char *value)
{
    if(ArrayElementType(type, NULL, 0, NULL)) {
        declare_array(e, name, type, value);
        return;
    }
    const char *target_type = TargetType(type, e->target);
    char resolved_type[ZIR_NAME_MAX * 2];
    if(type[0] == '*') {
        const char *pointee = type;
        char base_type[ZIR_NAME_MAX];
        size_t pointer_depth = 0;
        while(pointee[pointer_depth] == '*')
            pointer_depth++;
        pointee += pointer_depth;
        const char *scalar = TargetType(pointee, e->target);
        if(e->target == ZIR_GO && !strcmp(pointee, "void"))
            copy_text(base_type, sizeof(base_type), "byte");
        else if(scalar != NULL)
            copy_text(base_type, sizeof(base_type), scalar);
        else
            e->resolve(e->context, pointee, base_type, sizeof(base_type));
        if(e->target == ZIR_GO) {
            size_t used = 0;
            for(size_t depth = 0;
                depth < pointer_depth && used + 1 < sizeof(resolved_type);
                depth++)
                resolved_type[used++] = '*';
            resolved_type[used] = '\0';
            copy_text(resolved_type + used, sizeof(resolved_type) - used,
                      base_type);
        } else {
            format(resolved_type, sizeof(resolved_type), "%s", base_type);
            size_t used = strlen(resolved_type);
            for(size_t depth = 0;
                depth < pointer_depth && used + 1 < sizeof(resolved_type);
                depth++)
                resolved_type[used++] = '*';
            resolved_type[used] = '\0';
        }
        target_type = resolved_type;
    } else if(target_type == NULL) {
        e->resolve(e->context, type, resolved_type, sizeof(resolved_type));
        target_type = resolved_type;
    }
    if(enum_type(e->module, type)) {
        if(e->target == ZIR_GO)
            line(e, "var %s %s = %s(%s)", name, target_type, target_type, value);
        else
            line(e, "%s %s = (%s)(%s);", type, name, type, value);
        return;
    }
    if(e->target == ZIR_GO) line(e, "var %s %s = %s", name, target_type, value);
    else line(e, "%s %s = %s;", target_type, name, value);
}

static void
capture_context_name(const ZirFunction *fn, char *out, size_t size)
{
    int serial = 0, collision;
    do {
        format(out, size, "capture_context_%d", serial++);
        collision = strstr(fn->args, out) != NULL;
        for(int i = 0; i < fn->stmt_count; i++)
            collision |= strstr(fn->stmts[i].text, out) != NULL;
        for(int i = 0; i < fn->capture_count; i++)
            collision |= !strcmp(fn->captures[i].name, out);
    } while(collision);
}

static void
resolve(Emitter *e, const char *name, char *out, size_t size)
{
    for(int i = e->local_count - 1; i >= 0; i--)
        if(!strcmp(e->locals[i].name, name)) {
            copy_text(out, size, name);
            return;
        }
    for(int i = 0; i < e->fn->capture_count; i++) {
        const ZirCapture *capture = &e->fn->captures[i];
        if(strcmp(capture->name, name))
            continue;
        if(e->target == ZIR_C || e->target == ZIR_CPP) {
            char context[ZIR_NAME_MAX];
            capture_context_name(e->fn, context, sizeof(context));
            format(out, size, "(*%s->%s)", context, name);
        } else {
            copy_text(out, size, name);
        }
        return;
    }
    e->resolve(e->context, name, out, size);
}

static int
operation(const char *op)
{
    static const char *const ops[] = {"", "+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^"};
    for(int i = 1; i <= 10; i++) if(!strcmp(op,ops[i])) return i;
    return 0;
}

static void
go_bits_operand(const char *value, char *out, size_t size)
{
    /* Go rejects uint64(-1) as a constant conversion. A negative integer
     * literal here represents its two's-complement bits, not an arithmetic
     * conversion of a Go constant. Runtime expressions already convert. */
    if(value[0] == '-' && isdigit((unsigned char)value[1])) {
        char *end;
        long long signed_value;
        errno = 0;
        signed_value = strtoll(value, &end, 0);
        if(errno == 0 && *end == '\0') {
            format(out, size, "uint64(%llu)",
                   (unsigned long long)(uint64_t)signed_value);
            return;
        }
    }
    format(out, size, "uint64(%s)", value);
}

static void
number(Emitter *e, const char *type, const char *a, const char *b, int op, char *out, size_t size)
{
    char bits[ZIR_TEXT_MAX];
    int w = width(type), sign = signed_type(type);
    if(e->target == ZIR_GO) {
        char left[ZIR_TEXT_MAX], right[ZIR_TEXT_MAX];
        go_bits_operand(a, left, sizeof(left));
        go_bits_operand(b, right, sizeof(right));
        format(bits,sizeof(bits),"%s_bits(%s,%s,%d,%s,%d)",e->numbers,left,right,w,sign?"true":"false",op);
        format(out,size,"%s(%s)",TargetType(type,e->target),bits);
    } else {
        format(bits,sizeof(bits),"%s_bits((uint64_t)(%s),(uint64_t)(%s),%d,%d,%d)",e->numbers,a,b,w,sign,op);
        if(sign) format(out,size,"(%s)%s_signed(%s,%d)",TargetType(type,e->target),e->numbers,bits,w);
        else format(out,size,"(%s)(%s)",TargetType(type,e->target),bits);
    }
}

static void emit_expr(Emitter *e, int index, const char *expected, char *out, size_t size);
static void zero_record(Emitter *e, const char *type, char *out, size_t size);

static void
slice_index(Emitter *e, const char *type, const char *base, const char *index,
            char *out, size_t size)
{
    if(e->target == ZIR_GO) {
        format(out, size, "%s[%s]", base, index);
        return;
    }
    char element[ZIR_NAME_MAX], mapped[ZIR_NAME_MAX];
    SliceElementType(type, element, sizeof(element));
    const char *scalar = TargetType(element, e->target);
    if(scalar != NULL)
        copy_text(mapped, sizeof(mapped), scalar);
    else
        e->resolve(e->context, element, mapped, sizeof(mapped));
    format(out, size, "((%s *)%s.data)[SliceIndex(%s, (int64_t)%s)]", mapped, base, base, index);
}

static void
emit_destination(Emitter *e, int index, char *out, size_t size)
{
    const ZirExpr *expr = &e->fn->exprs[index];
    if(expr->kind == ZIR_EXPR_IDENT) {
        resolve(e, expr->name, out, size);
        e->pure = 1;
        return;
    }
    if(expr->kind == ZIR_EXPR_MEMBER) {
        char base[ZIR_TEXT_MAX], field[ZIR_NAME_MAX];
        emit_destination(e, expr->left, base, sizeof(base));
        if(e->target == ZIR_GO) go_field_ident(expr->name, field, sizeof(field));
        else copy_text(field, sizeof(field), expr->name);
        format(out, size, "%s.%s", base, field);
        e->pure = 1;
        return;
    }
    if(expr->kind == ZIR_EXPR_INDEX) {
        char base[ZIR_TEXT_MAX], index[ZIR_TEXT_MAX];

        if(!strcmp(e->fn->exprs[expr->left].type, "string"))
            fatal(expr, "string bytes are read-only");
        if(SliceElementType(e->fn->exprs[expr->left].type, NULL, 0))
            emit_expr(e, expr->left, e->fn->exprs[expr->left].type, base, sizeof(base));
        else
            emit_destination(e, expr->left, base, sizeof(base));
        {
            int base_pure = e->pure;
            emit_expr(e, expr->right, "i32", index, sizeof(index));
            e->pure = base_pure && e->pure;
        }
        if(SliceElementType(e->fn->exprs[expr->left].type, NULL, 0))
            slice_index(e, e->fn->exprs[expr->left].type, base, index, out, size);
        else if((e->target == ZIR_C || e->target == ZIR_CPP) &&
           ArrayElementType(e->fn->exprs[expr->left].type, NULL, 0, NULL))
            format(out, size, "ZIRAN_INDEX(%s, sizeof(%s) / sizeof(%s[0]), %s)",
                   base, base, base, index);
        else
            format(out, size, "%s[%s]", base, index);
        return;
    }
    fatal(expr, "unsupported assignment destination");
}

static void
literal(Emitter *e, const ZirExpr *expr, const char *type, int negative, char *out, size_t size)
{
    char raw[ZIR_TEXT_MAX], *end;
    size_t length = 0;
    unsigned long long value;
    for(const char *p = expr->text; *p; p++) if(*p != '_') raw[length++] = *p;
    raw[length] = 0;
    if(raw[0] == '-') {
        negative = !negative;
        memmove(raw, raw + 1, length);
    }
    errno = 0;
    value = strtoull(raw,&end,0);
    if(end == raw || errno == ERANGE) fatal(expr,"integer literal is out of range");
    if(*end && strcmp(end,"u") && strcmp(end,"U") && strcmp(end,"l") && strcmp(end,"L") &&
       strcmp(end,"ll") && strcmp(end,"LL") && strcmp(end,"ull") && strcmp(end,"ULL")) fatal(expr,"invalid integer literal");
    int w = width(type);
    if(w) {
        uint64_t max = w == 64 ? UINT64_MAX : (UINT64_C(1)<<w)-1;
        if(signed_type(type)) max = (max>>1) + (negative ? 1 : 0);
        if(value > max || (negative && !signed_type(type) && value != 0)) fatal(expr,"integer literal does not fit its type");
    }
    if(e->target == ZIR_GO) format(out,size,"%s%llu",negative?"-":"",value);
    else if(negative && value == (UINT64_C(1)<<63)) format(out,size,"(-INT64_C(9223372036854775807)-1)");
    else format(out,size,"%s%llu%s",negative?"-":"",value,w>32?(signed_type(type)?"LL":"ULL"):"");
}

static void
string_literal(const ZirExpr *expr, ZirTarget target, int terminated, char *out, size_t size)
{
    size_t used = 0;
    int remaining = 0;
    unsigned int scalar = 0, minimum = 0;
    out[used++] = '"';
    const unsigned char *cursor = (const unsigned char *)expr->text + 1;
    while(*cursor && *cursor != '"') {
        unsigned int value = *cursor++;
        int unicode_escape = 0;
        if(value == '\\') {
            value = *cursor++;
            const char *escapes = "0abfnrtv\\\"";
            const unsigned char values[] = {0, 7, 8, 12, 10, 13, 9, 11, '\\', '"'};
            const char *found = value ? strchr(escapes, (int)value) : NULL;
            if(found) {
                value = values[found - escapes];
            } else if(value == 'x' || value == 'u' || value == 'U') {
                unicode_escape = value != 'x';
                int digits = value == 'x' ? 2 : value == 'u' ? 4 : 8;
                value = 0;
                for(int index = 0; index < digits; index++) {
                    int digit = *cursor++;
                    if(digit >= '0' && digit <= '9') digit -= '0';
                    else if(digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
                    else if(digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
                    else fatal(expr, "invalid string escape");
                    value = value * 16 + (unsigned int)digit;
                }
                if(value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                    fatal(expr, "string escape is not a Unicode scalar value");
            } else {
                fatal(expr, "unknown string escape");
            }
        }
        unsigned char bytes[4];
        int count = 1;
        bytes[0] = (unsigned char)value;
        /* Raw source bytes are already UTF-8. Escaped scalar values above
         * ASCII are encoded explicitly, independently of the C compiler. */
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
        for(int index = 0; index < count; index++) {
            unsigned char byte = bytes[index];
            if(terminated && byte == 0)
                fatal(expr, "borrowed string literal cannot contain a null byte");
            if(remaining) {
                if((byte & 0xc0) != 0x80) fatal(expr, "string is not valid UTF-8");
                scalar = (scalar << 6) | (byte & 63);
                remaining--;
                if(!remaining && (scalar < minimum || scalar > 0x10ffff ||
                    (scalar >= 0xd800 && scalar <= 0xdfff)))
                    fatal(expr, "string is not valid UTF-8");
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
                    fatal(expr, "string is not valid UTF-8");
                }
            }
            if(used + 8 >= size) fatal(expr, "string literal exceeds output limit");
            if((target == ZIR_C || target == ZIR_CPP) &&
               (byte < 32 || byte >= 127 || byte == '"' || byte == '\\' || byte == '?'))
                used += (size_t)format(out + used, size - used, "\\%03o", byte);
            else if(byte < 32 || byte == '"' || byte == '\\' || byte == 127)
                used += (size_t)format(out + used, size - used, "\\u%04x", byte);
            else
                out[used++] = (char)byte;
        }
    }
    if(*cursor != '"') fatal(expr, "unterminated string literal");
    if(remaining) fatal(expr, "string is not valid UTF-8");
    out[used++] = '"';
    out[used] = 0;
}

int
ScalarLiteral(const char *type, const char *text, ZirTarget target,
                  ZirSourceSpan span, char *out, size_t size)
{
    ZirFunction fn={0};Emitter e={0};int ok=0;
    type=canonical(type);e.target=target;
    if(!*type)return 0;
    if(!strcmp(type,"bool")) {
        if(!strcmp(text,"true") || !strcmp(text,"false")) {copy_text(out,size,text);return 1;}
        return 0;
    }
    int index=ParseExpr(&fn,NULL,text,span);
    if(index>=0) {
        ZirExpr *expr=&fn.exprs[index];
        if(expr->kind == ZIR_EXPR_STRING &&
           (!strcmp(type, "string") || !strcmp(type, "const char*"))) {
            char value[ZIR_TEXT_MAX];
            string_literal(expr, target, !strcmp(type, "const char*"), value, sizeof(value));
            if(!strcmp(type, "string") && (target == ZIR_C || target == ZIR_CPP))
                format(out, size, "{%s, sizeof(%s) - 1}", value, value);
            else
                copy_text(out, size, value);
            ok = 1;
        } else if(expr->kind==ZIR_EXPR_INT && width(type)) {
            literal(&e,expr,type,0,out,size);ok=1;
        } else if(expr->kind==ZIR_EXPR_UNARY && !strcmp(expr->op,"-") &&
                  fn.exprs[expr->right].kind==ZIR_EXPR_INT && width(type)) {
            literal(&e,&fn.exprs[expr->right],type,1,out,size);ok=1;
        } else if((expr->kind==ZIR_EXPR_FLOAT || expr->kind==ZIR_EXPR_INT) && type[0]=='f') {
            copy_text(out,size,text);size_t n=strlen(out);
            if(n && (out[n-1]=='f'||out[n-1]=='F'))out[n-1]=0;
            ok=1;
        }
    }
    free(fn.exprs);return ok;
}

static void
call_parameter_type(Emitter *e, const ZirExpr *call, int ordinal,
                    char *out, size_t size)
{
    const char *signature = NULL;
    if(call->slot_type[0]) {
        const ZirType *slot = FindType(e->module, call->slot_type, NULL);
        if(slot != NULL && slot->is_slot)
            signature = slot->body;
    } else {
        const ZirModule *owner = NULL;
        const ZirFunction *function = NULL;
        if(ResolveFunction(e->module, call->name, &owner, &function) == 1 &&
           function != NULL)
            signature = function->args;
        else
            for(int i = 0; i < e->module->import_count; i++)
                if(e->module->imports[i].kind == ZIR_IMPORT_EXTERN &&
                   !strcmp(e->module->imports[i].name, call->name)) {
                    signature = e->module->imports[i].args;
                    break;
                }
    }
    if(signature == NULL || !*signature)
        return;
    char (*parts)[ZIR_TEXT_MAX] = calloc(64, sizeof(*parts));
    if(parts == NULL)
        return;
    int count = split_top_level(signature, parts[0], 64, sizeof(parts[0]));
    if(ordinal >= 0 && ordinal < count) {
        char *colon = strchr(parts[ordinal], ':');
        if(colon != NULL) {
            colon = (char *)skip_ws(colon + 1);
            trim_in_place(colon);
            copy_text(out, size, colon);
        }
    }
    free(parts);
}

static void
emit_call(Emitter *e, const ZirExpr *expr, const char *array_result, char *out, size_t size)
{
    char text[ZIR_TEXT_MAX];
    int count=0;
    size_t n;
    if(*expr->slot_type) {
        char callable[ZIR_NAME_MAX];
        fresh(e, callable);
        char source[ZIR_TEXT_MAX];
        resolve(e, expr->name, source, sizeof(source));
        declare(e, callable, expr->slot_type, source);
        if(e->target == ZIR_C || e->target == ZIR_CPP) {
            n = (size_t)format(text, sizeof(text), "%s.call(%s.context", callable, callable);
            count = 1;
        } else {
            n = (size_t)format(text, sizeof(text), "%s(", callable);
        }
    } else {
        n = (size_t)format(text, sizeof(text), "%s(", expr->name);
    }
    if(array_result != NULL) {
        n += (size_t)format(text + n, sizeof(text) - n, "%s%s", count ? "," : "", array_result);
        count++;
    }
    int ordinal = 0;
    for(int child=expr->first_child;child>=0;child=e->fn->exprs[child].next_sibling) {
        char argument[ZIR_TEXT_MAX];
        char argument_type[ZIR_NAME_MAX];
        copy_text(argument_type, sizeof(argument_type),
                  e->fn->exprs[child].type);
        if(!strcmp(argument_type, "null"))
            call_parameter_type(e, expr, ordinal, argument_type,
                                sizeof(argument_type));
        emit_expr(e,child,argument_type,argument,sizeof(argument));
        /* The whole call text passes through the target resolver, which only
         * understands identifiers as arguments — the shape every emitted
         * argument had before folding. Capture folded expressions in a
         * temporary first; sub-expressions inside the argument still fold. */
        if(!plain_identifier(argument)) {
            char captured[ZIR_NAME_MAX];
            fresh(e, captured);
            declare(e, captured, argument_type, argument);
            copy_text(argument, sizeof(argument), captured);
        }
        n+=(size_t)format(text+n,sizeof(text)-n,"%s%s",count?",":"",argument);
        count++;
        ordinal++;
    }
    format(text+n,sizeof(text)-n,")");
    if(*expr->slot_type)
        copy_text(out, size, text);
    else
        e->resolve(e->context,text,out,size);
}

static void
slot_wrapper_name(const ZirModule *module, const ZirFunction *fn, int index,
                   char *out, size_t size)
{
    char prefix[64];
    number_prefix(module, prefix, sizeof(prefix));
    format(out, size, "%s_slot_%ld_%d", prefix, (long)(fn - module->functions), index);
}

/* C has no lexical function values. A file-scope adapter supplies the uniform
 * borrowed-context slot ABI while ordinary Ziran functions keep their own ABI. */
void
EmitSlotWrappers(FILE *out, const ZirModule *module, const ZirFunction *fn,
                    ZirTarget target, ZirResolveTarget resolver, void *context)
{
    if((target != ZIR_C && target != ZIR_CPP) || !CanEmitBody(module, fn))
        return;
    for(int index = 0; index < fn->expr_count; index++) {
        const ZirExpr *value = &fn->exprs[index];
        if(!value->is_function_value)
            continue;
        const ZirType *slot = FindType(module, value->type, NULL);
        const ZirModule *owner = NULL;
        const ZirFunction *body = NULL;
        ResolveFunction(module, value->name, &owner, &body);
        if(body != NULL && body->is_closure) {
            EmitSlotWrappers(out, module, body, target, resolver, context);
            char wrapper[ZIR_NAME_MAX], capture_name[ZIR_NAME_MAX];
            slot_wrapper_name(module, fn, index, wrapper, sizeof(wrapper));
            capture_context_name(body, capture_name, sizeof(capture_name));
            if(body->capture_count) {
                fprintf(out, "struct %s_environment {\n", wrapper);
                for(int capture = 0; capture < body->capture_count; capture++) {
                    const ZirCapture *field = &body->captures[capture];
                    char element[ZIR_NAME_MAX];
                    int capacity;
                    if(ArrayElementType(field->type, element, sizeof(element), &capacity)) {
                        char bound[ZIR_NAME_MAX];
                        if(capacity >= 0)
                            format(bound, sizeof(bound), "%d", capacity);
                        else
                            format(bound, sizeof(bound), "%.*s",
                                   (int)(strchr(field->type, ']') - field->type - 1), field->type + 1);
                        const char *scalar = TargetType(element, target);
                        fprintf(out, "    %s (*%s)[%s];\n", scalar ? scalar : element, field->name, bound);
                    } else {
                        const char *scalar = TargetType(field->type, target);
                        fprintf(out, "    %s *%s;\n", scalar ? scalar : field->type, field->name);
                    }
                }
                fputs("};\n", out);
            }
            fprintf(out, "static void %s(void *%s_opaque", wrapper, capture_name);
            char arguments[64][ZIR_TEXT_MAX];
            int count = *skip_ws(body->args) ?
                split_top_level(body->args, arguments[0], 64, sizeof(arguments[0])) : 0;
            for(int argument = 0; argument < count; argument++) {
                char *colon = strchr(arguments[argument], ':');
                *colon++ = '\0';
                trim_in_place(arguments[argument]);
                trim_in_place(colon);
                const char *scalar = TargetType(colon, target);
                fprintf(out, ", %s %s", scalar ? scalar : colon, arguments[argument]);
            }
            fputs(")\n{\n", out);
            if(body->capture_count)
                fprintf(out, "    struct %s_environment *%s = (struct %s_environment *)%s_opaque;\n",
                        wrapper, capture_name, wrapper, capture_name);
            else
                fprintf(out, "    (void)%s_opaque;\n", capture_name);
            EmitBody(out, module, body, target, resolver, context, NULL);
            fputs("}\n", out);
            continue;
        }
        char parameters[64][ZIR_TEXT_MAX];
        int count = *skip_ws(slot->body) ?
            split_top_level(slot->body, parameters[0], 64, sizeof(parameters[0])) : 0;
        char wrapper[ZIR_NAME_MAX], call[ZIR_TEXT_MAX], resolved[ZIR_TEXT_MAX];
        slot_wrapper_name(module, fn, index, wrapper, sizeof(wrapper));
        fprintf(out, "static void %s(void *context", wrapper);
        size_t length = (size_t)format(call, sizeof(call), "%s(", value->name);
        for(int argument = 0; argument < count; argument++) {
            const char *source = skip_ws(strchr(parameters[argument], ':') + 1);
            const char *scalar = TargetType(source, target);
            fprintf(out, ", %s slot_arg_%d", scalar ? scalar : source, argument);
            length += (size_t)format(call + length, sizeof(call) - length,
                                      "%sslot_arg_%d", argument ? ", " : "", argument);
        }
        format(call + length, sizeof(call) - length, ")");
        resolver(context, call, resolved, sizeof(resolved));
        fprintf(out, ")\n{\n    (void)context;\n    %s;\n}\n", resolved);
    }
}

static void
emit_function_value(Emitter *e, int index, char *out, size_t size)
{
    const ZirExpr *value = &e->fn->exprs[index];
    const ZirModule *owner = NULL;
    const ZirFunction *body = NULL;
    ResolveFunction(e->module, value->name, &owner, &body);
    if(body != NULL && body->is_closure) {
        char temporary[ZIR_NAME_MAX];
        fresh(e, temporary);
        if(e->target == ZIR_C || e->target == ZIR_CPP) {
            char wrapper[ZIR_NAME_MAX];
            slot_wrapper_name(e->module, e->fn, index, wrapper, sizeof(wrapper));
            if(body->capture_count) {
                char initializer[ZIR_TEXT_MAX] = "";
                size_t length = 0;
                for(int capture = 0; capture < body->capture_count; capture++) {
                    char source[ZIR_TEXT_MAX];
                    resolve(e, body->captures[capture].name, source, sizeof(source));
                    length += (size_t)format(initializer + length, sizeof(initializer) - length,
                        "%s&(%s)", capture ? ", " : "", source);
                }
                line(e, "struct %s_environment %s = {%s};", wrapper, temporary, initializer);
                format(out, size, "(%s){&%s, %s}", value->type, temporary, wrapper);
            } else {
                format(out, size, "(%s){NULL, %s}", value->type, wrapper);
            }
            return;
        }
        char arguments[64][ZIR_TEXT_MAX], signature[ZIR_TEXT_MAX] = "";
        int count = *skip_ws(body->args) ?
            split_top_level(body->args, arguments[0], 64, sizeof(arguments[0])) : 0;
        size_t length = 0;
        for(int argument = 0; argument < count; argument++) {
            char *colon = strchr(arguments[argument], ':');
            *colon++ = '\0';
            trim_in_place(arguments[argument]);
            trim_in_place(colon);
            char type[ZIR_NAME_MAX] = "";
            if(e->target == ZIR_GO) {
                const char *scalar = TargetType(colon, ZIR_GO);
                if(scalar)
                    copy_text(type, sizeof(type), scalar);
                else
                    e->resolve(e->context, colon, type, sizeof(type));
            }
            length += (size_t)format(signature + length, sizeof(signature) - length,
                "%s%s%s%s", argument ? ", " : "", arguments[argument], *type ? " " : "", type);
        }
        if(e->target == ZIR_GO)
            line(e, "%s := func(%s) {", temporary, signature);
        else
            line(e, "const %s = (%s) => {", temporary, signature);
        EmitBody(e->out, e->module, body, e->target, e->resolve, e->context,
                    e->numbers);
        line(e, e->target == ZIR_GO ? "}" : "};");
        copy_text(out, size, temporary);
        return;
    }
    if(e->target == ZIR_C || e->target == ZIR_CPP) {
        char wrapper[ZIR_NAME_MAX];
        slot_wrapper_name(e->module, e->fn, index, wrapper, sizeof(wrapper));
        format(out, size, "(%s){NULL, %s}", value->type, wrapper);
        return;
    }
    const ZirType *slot = FindType(e->module, value->type, NULL);
    char parameters[64][ZIR_TEXT_MAX];
    int count = *skip_ws(slot->body) ?
        split_top_level(slot->body, parameters[0], 64, sizeof(parameters[0])) : 0;
    char arguments[ZIR_TEXT_MAX] = "", signature[ZIR_TEXT_MAX] = "";
    char call[ZIR_TEXT_MAX], resolved[ZIR_TEXT_MAX];
    size_t length = 0, signature_length = 0;
    for(int argument = 0; argument < count; argument++) {
        char parameter[ZIR_NAME_MAX];
        fresh(e, parameter);
        length += (size_t)format(arguments + length, sizeof(arguments) - length,
                                  "%s%s", argument ? ", " : "", parameter);
        if(e->target == ZIR_GO) {
            const char *source = skip_ws(strchr(parameters[argument], ':') + 1);
            const char *scalar = TargetType(source, ZIR_GO);
            char type[ZIR_NAME_MAX];
            if(scalar)
                copy_text(type, sizeof(type), scalar);
            else
                e->resolve(e->context, source, type, sizeof(type));
            signature_length += (size_t)format(signature + signature_length,
                sizeof(signature) - signature_length, "%s%s %s", argument ? ", " : "", parameter, type);
        }
    }
    format(call, sizeof(call), "%s(%s)", value->name, arguments);
    e->resolve(e->context, call, resolved, sizeof(resolved));
    if(e->target == ZIR_GO)
        format(out, size, "func(%s) { %s }", signature, resolved);
    else
        format(out, size, "(%s) => %s", arguments, resolved);
}

static int
member_path(const ZirFunction *fn, int index)
{
    const ZirExpr *expr = &fn->exprs[index];
    if(expr->kind == ZIR_EXPR_IDENT)
        return 1;
    return expr->kind == ZIR_EXPR_MEMBER && member_path(fn, expr->left);
}

static void
emit_expr(Emitter *e, int index, const char *expected, char *out, size_t size)
{
    const ZirExpr *expr=&e->fn->exprs[index];
    const char *type=canonical(expr->type);
    char a[ZIR_TEXT_MAX],b[ZIR_TEXT_MAX],result[ZIR_TEXT_MAX],temp[ZIR_NAME_MAX];
    int pure=0;
    /* Binary-shaped results re-bind when spliced into a parent expression, so
     * the tail parenthesizes them; identifiers, literals, calls, and slices
     * are operand-safe without wrapping. */
    int atom=1;
    if(!strcmp(expr->type,"integer") || !strcmp(expr->type,"real")) {
        const char *want=canonical(expected); if(*want && strcmp(want,"bool") && strcmp(want,"void")) type=want;
    }
    if(!strcmp(expr->type, "null") && strchr(expected, '*') != NULL)
        type = canonical(expected);
    switch(expr->kind) {
    case ZIR_EXPR_COMPOUND: {
        if(ArrayElementType(type, NULL, 0, NULL)) {
            fresh(e, temp);
            declare(e, temp, type, NULL);
            int ordinal = 0;
            for(int child = expr->first_child; child >= 0; child = e->fn->exprs[child].next_sibling) {
                const ZirExpr *entry = &e->fn->exprs[child];
                emit_expr(e, entry->right, entry->type, a, sizeof(a));
                format(b, sizeof(b), "%s[%d]", temp, ordinal++);
                assign_value(e, b, entry->type, a);
            }
            copy_text(out, size, temp);
            e->pure = 1;
            return;
        }
        zero_record(e, type, a, sizeof(a));
        fresh(e, temp);
        declare(e, temp, type, a);
        for(int child = expr->first_child; child >= 0; child = e->fn->exprs[child].next_sibling) {
            const ZirExpr *field = &e->fn->exprs[child];
            char field_name[ZIR_NAME_MAX];
            if(e->target == ZIR_GO)
                go_field_ident(field->name, field_name, sizeof(field_name));
            else
                copy_text(field_name, sizeof(field_name), field->name);
            emit_expr(e, field->right, field->type, a, sizeof(a));
            format(b, sizeof(b), "%s.%s", temp, field_name);
            assign_value(e, b, field->type, a);
        }
        copy_text(out, size, temp);
        e->pure = 1;
        return;
    }
    case ZIR_EXPR_MEMBER: {
        char field[ZIR_NAME_MAX];
        int base_pure;
        /* Reading a field needs a snapshot of that field, not a copy of every
         * enclosing record. Calls and other computed bases still evaluate once. */
        if(member_path(e->fn, expr->left))
            emit_destination(e, expr->left, a, sizeof(a));
        else
            emit_expr(e, expr->left, e->fn->exprs[expr->left].type, a, sizeof(a));
        base_pure = e->pure;
        if((!strcmp(e->fn->exprs[expr->left].type, "string") ||
            SliceElementType(e->fn->exprs[expr->left].type, NULL, 0)) &&
           !strcmp(expr->name, "length")) {
            if(e->target == ZIR_GO)
                format(result, sizeof(result), "int32(len(%s))", a);
            else
                format(result, sizeof(result), "(int32_t)%s.length", a);
            pure = base_pure;
            break;
        }
        if(e->target == ZIR_GO) go_field_ident(expr->name, field, sizeof(field));
        else copy_text(field, sizeof(field), expr->name);
        format(result, sizeof(result), "%s.%s", a, field);
        pure = base_pure;
        break;
    }
    case ZIR_EXPR_SLICE: {
        const char *base_type = e->fn->exprs[expr->left].type;
        char source[ZIR_TEXT_MAX], low[ZIR_TEXT_MAX], high[ZIR_TEXT_MAX];
        char element[ZIR_NAME_MAX], mapped[ZIR_NAME_MAX];
        char view[ZIR_NAME_MAX];
        int capacity = 0;
        int base_pure;
        int low_pure = 1;
        int high_pure = 1;
        int array = ArrayElementType(base_type, element, sizeof(element), &capacity);
        if(array) {
            emit_destination(e, expr->left, source, sizeof(source));
        } else {
            SliceElementType(base_type, element, sizeof(element));
            emit_expr(e, expr->left, base_type, source, sizeof(source));
        }
        base_pure = e->pure;
        fresh(e, view);
        if(e->target == ZIR_GO) {
            line(e, "%s := %s[:]", view, source);
        } else if(array) {
            line(e, "Slice %s = {%s, %d};", view, source, capacity);
        } else {
            line(e, "Slice %s = %s;", view, source);
        }
        if(expr->right >= 0) {
            emit_expr(e, expr->right, "i64", low, sizeof(low));
            low_pure = e->pure;
        }
        else
            copy_text(low, sizeof(low), "0");
        if(expr->third >= 0) {
            emit_expr(e, expr->third, "i64", high, sizeof(high));
            high_pure = e->pure;
        }
        else
            format(high, sizeof(high), e->target == ZIR_GO ? "len(%s)" : "%s.length", view);
        pure = base_pure && low_pure && high_pure;
        if(e->target == ZIR_GO) {
            line(e, "if int64(%s) < 0 || int64(%s) < int64(%s) || int64(%s) > int64(len(%s)) { panic(\"slice range out of bounds\") }",
                 low, high, low, high, view);
            format(result, sizeof(result), "%s[int64(%s):int64(%s):int64(%s)]", view, low, high, high);
        } else {
            const char *scalar = TargetType(element, e->target);
            if(scalar != NULL)
                copy_text(mapped, sizeof(mapped), scalar);
            else
                e->resolve(e->context, element, mapped, sizeof(mapped));
            format(result, sizeof(result), "SliceRange(%s, (int64_t)%s, (int64_t)%s, sizeof(%s))",
                   view, low, high, mapped);
        }
        break;
    }
    case ZIR_EXPR_INDEX: {
        const char *base_type = e->fn->exprs[expr->left].type;
        int capacity = 0;
        int base_pure;

        if(member_path(e->fn, expr->left))
            emit_destination(e, expr->left, a, sizeof(a));
        else
            emit_expr(e, expr->left, base_type, a, sizeof(a));
        base_pure = e->pure;
        emit_expr(e, expr->right, "i32", b, sizeof(b));
        pure = base_pure && e->pure;
        ArrayElementType(base_type, NULL, 0, &capacity);
        if(SliceElementType(base_type, NULL, 0)) {
            slice_index(e, base_type, a, b, result, sizeof(result));
        } else if(!strcmp(base_type, "string")) {
            if(e->target == ZIR_GO)
                format(result, sizeof(result), "%s[%s]", a, b);
            else
                format(result, sizeof(result), "(uint8_t)ZIRAN_INDEX(%s.data, %s.length, %s)", a, a, b);
        } else if((e->target == ZIR_C || e->target == ZIR_CPP) && capacity != 0) {
            /* Fixed-capacity arrays are bounds-checked in debug builds. */
            format(result, sizeof(result), "ZIRAN_INDEX(%s, sizeof(%s) / sizeof(%s[0]), %s)",
                   a, a, a, b);
        } else {
            format(result, sizeof(result), "%s[%s]", a, b);
        }
        break;
    }
    case ZIR_EXPR_IDENT:
        if(expr->is_function_value) {
            emit_function_value(e, index, result, sizeof(result));
            break;
        }
        if(!strcmp(expr->name, "nil")) {
            copy_text(result, sizeof(result), e->target == ZIR_GO ? "nil" :
                      e->target == ZIR_CPP ? "nullptr" : "((void *)0)");
            pure = 1;
            break;
        }
        resolve(e, expr->name, result, sizeof(result));
        pure = 1;
        break;
    case ZIR_EXPR_STRING:
        if(!strcmp(expected, "const char*"))
            type = expected;
        string_literal(expr, e->target, !strcmp(type, "const char*"), a, sizeof(a));
        if(strcmp(type, "const char*") && (e->target == ZIR_C || e->target == ZIR_CPP))
            format(result, sizeof(result), "StringView(%s, sizeof(%s) - 1)", a, a);
        else
            copy_text(result, sizeof(result), a);
        pure = 1;
        break;
    case ZIR_EXPR_INT: literal(e,expr,type,0,result,sizeof(result));pure=1;break;
    case ZIR_EXPR_FLOAT: {
        copy_text(result,sizeof(result),expr->text);size_t n=strlen(result);
        if(n && (result[n-1]=='f' || result[n-1]=='F')) result[n-1]=0;
        pure = 1;
        break;
    }
    case ZIR_EXPR_CALL:
        if((e->target == ZIR_C || e->target == ZIR_CPP) &&
           ArrayElementType(type, NULL, 0, NULL)) {
            fresh(e, temp);
            declare_array(e, temp, type, NULL);
            emit_call(e, expr, temp, result, sizeof(result));
            line(e, "%s;", result);
            copy_text(out, size, temp);
            e->pure = 1;
            return;
        }
        emit_call(e, expr, NULL, result, sizeof(result));
        if(!strcmp(type,"void")) {line(e,"%s%s",result,e->target==ZIR_GO?"":";");out[0]=0;e->pure=0;return;}
        break;
    case ZIR_EXPR_CONDITIONAL:
        emit_expr(e,expr->left,"bool",a,sizeof(a));fresh(e,temp);
        const ZirType *declared = FindType(e->module, type, NULL);
        if(declared != NULL && declared->is_slot) {
            if(e->target == ZIR_C || e->target == ZIR_CPP)
                format(b, sizeof(b), "(%s){0}", type);
            else
                copy_text(b, sizeof(b), e->target == ZIR_GO ? "nil" : "null");
        } else if(record_type(e->module, type))
            zero_record(e, type, b, sizeof(b));
        else
            copy_text(b, sizeof(b), zero_value(type, e->target));
        declare(e, temp, type, b);
        line(e, e->target == ZIR_GO ? "if %s {" : "if (%s) {", a);
        e->indent++;
        emit_expr(e, expr->right, type, b, sizeof(b));
        assign_value(e, temp, type, b);
        e->indent--;
        line(e, "} else {");
        e->indent++;
        emit_expr(e, expr->third, type, b, sizeof(b));
        assign_value(e, temp, type, b);
        e->indent--;
        line(e, "}");
        copy_text(out, size, temp);
        e->pure = 1;
        return;
    case ZIR_EXPR_BINARY: {
        const char *operand_type=type;
        int left_pure;
        if(!strcmp(type,"bool")) {
            operand_type=canonical(e->fn->exprs[expr->left].type);
            if(!strcmp(e->fn->exprs[expr->left].type,"integer") || !strcmp(e->fn->exprs[expr->left].type,"real")) operand_type=canonical(e->fn->exprs[expr->right].type);
            if(!strcmp(e->fn->exprs[expr->left].type, "null"))
                operand_type = canonical(e->fn->exprs[expr->right].type);
            if(!strcmp(e->fn->exprs[expr->right].type, "const char*"))
                operand_type = "const char*";
        }
        emit_expr(e,expr->left,operand_type,a,sizeof(a));
        left_pure = e->pure;
        if(!strcmp(expr->op,"&&") || !strcmp(expr->op,"||")) {
            fresh(e,temp);declare(e,temp,"bool",a);
            line(e,e->target==ZIR_GO?"if %s%s {":"if (%s%s) {",!strcmp(expr->op,"||")?"!":"",temp);e->indent++;
            emit_expr(e,expr->right,"bool",b,sizeof(b));line(e,"%s = %s%s",temp,b,e->target==ZIR_GO?"":";");
            e->indent--;line(e,"}");copy_text(out,size,temp);e->pure=1;return;
        }
        emit_expr(e,expr->right,(!strcmp(expr->op,"<<")||!strcmp(expr->op,">>"))?"i32":operand_type,b,sizeof(b));
        pure = left_pure && e->pure;
        if(!strcmp(operand_type, "const char*") &&
           strcmp(e->fn->exprs[expr->left].type, "null") &&
           strcmp(e->fn->exprs[expr->right].type, "null") &&
           (e->target == ZIR_C || e->target == ZIR_CPP))
            format(result, sizeof(result), "strcmp(%s ? %s : \"\", %s ? %s : \"\") %s 0",
                   a, a, b, b, expr->op);
        else if(!strcmp(operand_type, "string") && (e->target == ZIR_C || e->target == ZIR_CPP))
            format(result, sizeof(result), "%sStringEqual(%s, %s)", !strcmp(expr->op, "!=") ? "!" : "", a, b);
        else if(width(type) && operation(expr->op)) number(e,type,a,b,operation(expr->op),result,sizeof(result));
        else format(result,sizeof(result),"%s %s %s",a,expr->op,b);
        atom=0;
        break;
    }
    case ZIR_EXPR_UNARY:
        if(!strcmp(expr->op,"-") && e->fn->exprs[expr->right].kind==ZIR_EXPR_INT) {
            literal(e,&e->fn->exprs[expr->right],type,1,result,sizeof(result));pure=1;break;
        }
        if(!strcmp(expr->op,"++") || !strcmp(expr->op,"--")) {
            emit_destination(e, expr->right, a, sizeof(a));
            if(width(type)) number(e,type,a,"1",expr->op[0]=='+'?1:2,result,sizeof(result));
            else format(result,sizeof(result),"(%s %c 1)",a,expr->op[0]);
            line(e,"%s = %s%s",a,result,e->target==ZIR_GO?"":";");
            copy_text(result,sizeof(result),a);pure=1;break;
        }
        emit_expr(e,expr->right,type,a,sizeof(a));
        pure = e->pure;
        if(width(type) && !strcmp(expr->op,"-")) number(e,type,"0",a,2,result,sizeof(result));
        else if(width(type) && !strcmp(expr->op,"~")) {
            number(e,type,a,e->target==ZIR_GO?"^uint64(0)":"UINT64_MAX",10,result,sizeof(result));
        } else format(result,sizeof(result),"%s%s",expr->op,a);
        break;
    case ZIR_EXPR_POSTFIX:
        emit_destination(e, expr->left, a, sizeof(a));fresh(e,temp);declare(e,temp,type,a);
        if(width(type)) number(e,type,a,"1",expr->op[0]=='+'?1:2,result,sizeof(result));
        else format(result,sizeof(result),"(%s %c 1)",a,expr->op[0]);
        line(e,"%s = %s%s",a,result,e->target==ZIR_GO?"":";");copy_text(out,size,temp);e->pure=1;return;
    case ZIR_EXPR_CAST: {
        const char *declared_type = type;
        const char *operand_type = e->fn->exprs[expr->right].type;
        int right_pure;
        if(enum_type(e->module, type))
            type = "i32";
        if(e->fn->exprs[expr->right].kind == ZIR_EXPR_INT &&
           width(type) >= 32 && !signed_type(type))
            operand_type = type;
        emit_expr(e,expr->right,operand_type,a,sizeof(a));
        right_pure = e->pure;
        pure = right_pure;
        if(!strcmp(type, "string") || !strcmp(type, "const char*")) {
            copy_text(result, sizeof(result), a);
        } else if(!strcmp(type,"bool")) {
            format(result,sizeof(result),"%s != %s",a,!strcmp(canonical(e->fn->exprs[expr->right].type),"bool")?"false":"0");
            atom=0;
        } else if(!strcmp(canonical(e->fn->exprs[expr->right].type),"bool")) {
            fresh(e,temp);declare(e,temp,type,"0");
            line(e,e->target==ZIR_GO?"if %s {":"if (%s) {",a);e->indent++;
            line(e,"%s = 1%s",temp,e->target==ZIR_GO?"":";");
            e->indent--;
            line(e, "}");
            copy_text(result, sizeof(result), temp);
        } else if(width(type)) {
            if(canonical(e->fn->exprs[expr->right].type)[0]=='f') {
                if(e->target==ZIR_GO) format(b,sizeof(b),"%s_float(float64(%s),%d,%s)",e->numbers,a,width(type),signed_type(type)?"true":"false");
                else format(b,sizeof(b),"%s_float(%s,%d,%s)",e->numbers,a,width(type),signed_type(type)?"true":"false");
                number(e,type,b,"0",0,result,sizeof(result));
            } else number(e,type,a,"0",0,result,sizeof(result));
        }
        else if(e->target==ZIR_GO) format(result,sizeof(result),"%s(%s)",TargetType(type,e->target),a);
        else format(result,sizeof(result),"(%s)(%s)",TargetType(type,e->target),a);
        type = declared_type;
        break;
    }
    default: fatal(expr,"unsupported structured expression");
    }
    e->pure = pure;
    if(go_folds_text(e, result)) {
        /* declare() applies this cast for named enum types; inlined text has
         * to carry it so Go sees matching operand types. */
        if(!plain_identifier(result) && enum_type(e->module, type)) {
            const char *scalar = TargetType(type, e->target);
            char resolved[ZIR_NAME_MAX * 2];
            if(scalar == NULL) {
                e->resolve(e->context, type, resolved, sizeof(resolved));
                scalar = resolved;
            }
            format(out, size, "%s(%s)", scalar, result);
        } else if(atom) {
            copy_text(out, size, result);
        } else {
            format(out, size, "(%s)", result);
        }
        e->pure = plain_identifier(result) ? 1 : pure;
        return;
    }
    fresh(e,temp);declare(e,temp,type,result);copy_text(out,size,temp);
    e->pure = 1;
}

static int
block_end(const ZirFunction *fn,int begin,int end)
{
    int depth=1;
    for(int i=begin+1;i<end;i++) {
        ZirStmtKind k=fn->stmts[i].kind;
        if(k==ZIR_STMT_IF||k==ZIR_STMT_WHILE||k==ZIR_STMT_BLOCK_OPEN)depth++;
        if(k==ZIR_STMT_BLOCK_CLOSE && !--depth)return i;
    }
    return end;
}

static void emit_sequence(Emitter *e,int begin,int end);

static void
zero_record(Emitter *e, const char *type, char *out, size_t size)
{
    if(e->target == ZIR_GO) {
        char target_type[ZIR_NAME_MAX * 2];
        e->resolve(e->context, type, target_type, sizeof(target_type));
        format(out, size, "%s{}", target_type);
        return;
    }
    copy_text(out, size, e->target == ZIR_CPP ? "{}" : "{0}");
}

static int
emit_if(Emitter *e,int i,int end)
{
    char cond[ZIR_TEXT_MAX];
    int close=block_end(e->fn,i,end);
    emit_expr(e,e->fn->stmts[i].expr_root,"bool",cond,sizeof(cond));
    line(e,e->target==ZIR_GO?"if %s {":"if (%s) {",cond);e->indent++;
    emit_sequence(e,i+1,close);e->indent--;
    if(close+1<end && e->fn->stmts[close+1].kind==ZIR_STMT_IF && !strncmp(e->fn->stmts[close+1].text,"else",4)) {
        int next=close+1;
        line(e,"} else {");e->indent++;
        if(e->fn->stmts[next].expr_root>=0)close=emit_if(e,next,end);
        else {close=block_end(e->fn,next,end);emit_sequence(e,next+1,close);}
        e->indent--;
    }
    line(e,"}");return close;
}

static void
emit_sequence(Emitter *e,int begin,int end)
{
    int saved=e->local_count;e->depth++;
    for(int i=begin;i<end;i++) {
        const ZirStmt *st=&e->fn->stmts[i];
        char value[ZIR_TEXT_MAX],lhs[ZIR_TEXT_MAX],result[ZIR_TEXT_MAX];
        switch(st->kind) {
        case ZIR_STMT_DECL:
            if(st->expr_root>=0)emit_expr(e,st->expr_root,st->type,value,sizeof(value));
            else if(record_type(e->module, st->type)) {
                zero_record(e, st->type, value, sizeof(value));
            }
            else copy_text(value, sizeof(value), zero_value(st->type, e->target));
            declare(e,st->name,st->type,value);
            copy_text(e->locals[e->local_count].name,ZIR_NAME_MAX,st->name);e->locals[e->local_count++].depth=e->depth;
            break;
        case ZIR_STMT_ASSIGN:
            emit_destination(e, st->lhs_root, lhs, sizeof(lhs));
            if(strcmp(st->assignment_op,"=")) {
                char old[ZIR_NAME_MAX];fresh(e,old);
                declare(e,old,e->fn->exprs[st->lhs_root].type,lhs);
                emit_expr(e,st->expr_root,e->fn->exprs[st->lhs_root].type,value,sizeof(value));
                char op[4];copy_text(op,sizeof(op),st->assignment_op);op[strlen(op)-1]=0;
                const char *type=canonical(e->fn->exprs[st->lhs_root].type);
                if(width(type))number(e,type,old,value,operation(op),result,sizeof(result));
                else format(result,sizeof(result),"%s %s %s",old,op,value);
            } else {
                emit_expr(e,st->expr_root,e->fn->exprs[st->lhs_root].type,value,sizeof(value));
                copy_text(result,sizeof(result),value);
            }
            assign_value(e, lhs, e->fn->exprs[st->lhs_root].type, result);
            break;
        case ZIR_STMT_RETURN:
            if(st->expr_root >= 0) {
                emit_expr(e, st->expr_root, e->fn->return_type, value, sizeof(value));
                if((e->target == ZIR_C || e->target == ZIR_CPP) &&
                   ArrayElementType(e->fn->return_type, NULL, 0, NULL)) {
                    char output[ZIR_NAME_MAX];
                    ArrayAbiName(e->fn, -1, output, sizeof(output));
                    /* value is a captured true array; output is an ABI pointer. */
                    line(e, "memmove(%s, %s, sizeof(%s));", output, value, value);
                    line(e, "return;");
                } else {
                    line(e, "return %s%s", value, e->target == ZIR_GO ? "" : ";");
                }
            }
            else line(e,e->target==ZIR_GO?"return":"return;");
            e->local_count=saved;e->depth--;return;
        case ZIR_STMT_IF:i=emit_if(e,i,end);break;
        case ZIR_STMT_WHILE: {
            int close=block_end(e->fn,i,end);
            line(e,e->target==ZIR_GO?"for {":"while (true) {");e->indent++;
            emit_expr(e,st->expr_root,"bool",value,sizeof(value));
            line(e,e->target==ZIR_GO?"if !%s { break }":"if (!%s) { break; }",value);
            emit_sequence(e,i+1,close);e->indent--;line(e,"}");i=close;break;
        }
        case ZIR_STMT_BLOCK_OPEN: {
            int close=block_end(e->fn,i,end);line(e,"{");e->indent++;emit_sequence(e,i+1,close);e->indent--;line(e,"}");i=close;break;
        }
        case ZIR_STMT_BREAK:case ZIR_STMT_CONTINUE:
            line(e,"%s%s",st->kind==ZIR_STMT_BREAK?"break":"continue",e->target==ZIR_GO?"":";");
            e->local_count=saved;e->depth--;return;
        case ZIR_STMT_EXPR:case ZIR_STMT_UNUSED:
            if(st->expr_root>=0) {
                emit_expr(e,st->expr_root,e->fn->exprs[st->expr_root].type,value,sizeof(value));
                if(*value)line(e,e->target==ZIR_GO?"_ = %s":"(void)%s;",value);
            }break;
        default:break;
        }
    }
    e->local_count=saved;e->depth--;
}

int
EmitBody(FILE *out,const ZirModule *module,const ZirFunction *fn,ZirTarget target,
            ZirResolveTarget resolver,void *context,const char *number_support)
{
    Emitter e={0};char params[64][ZIR_TEXT_MAX];int count;
    if(!CanEmitBody(module, fn))return 0;
    e.out=out;e.module=module;e.fn=fn;e.target=target;e.resolve=resolver;e.context=context;e.indent=1;
    e.minify = zir_minify_output;
    e.locals=calloc((size_t)fn->stmt_count+65,sizeof(*e.locals));
    if(!e.locals) { fprintf(stderr,"out of memory during scalar emission\n"); exit(1); }
    if(number_support && *number_support)
        copy_text(e.numbers, sizeof(e.numbers), number_support);
    else
        number_prefix(module,e.numbers,sizeof(e.numbers));
    count=*skip_ws(fn->args)?split_top_level(fn->args,params[0],64,sizeof(params[0])):0;
    for(int i=0;i<count;i++) {
        char *colon=strchr(params[i],':');*colon++=0;trim_in_place(params[i]);
        copy_text(e.locals[e.local_count++].name,ZIR_NAME_MAX,params[i]);
        const char *type=canonical(skip_ws(colon));
        if((target == ZIR_C || target == ZIR_CPP) &&
           ArrayValueType(type)) {
            char incoming[ZIR_NAME_MAX];
            ArrayAbiName(fn, i, incoming, sizeof(incoming));
            declare_array(&e, params[i], type, incoming);
        }
    }
    emit_sequence(&e,0,fn->stmt_count);free(e.locals);return 1;
}
