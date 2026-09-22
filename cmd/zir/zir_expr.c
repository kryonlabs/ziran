#include "zir_expr.h"
#include "zir_diagnostic.h"
#include "zir_token.h"
#include "zir_text.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExprParser {
    ZirLexer lexer;
    ZirToken token;
    ZirFunction *fn;
    const ZirModule *module;
    ZirSourceSpan span;
    const char *source;
    size_t begin;
    int failed, depth;
} ExprParser;

static void
next(ExprParser *p)
{
    p->token = ZirLexerNext(&p->lexer);
    if(p->token.truncated)
        p->failed = 1;   /* a cut token cannot round-trip to source text */
    p->begin = p->lexer.pos - strlen(p->token.text);
}

static int
is(ExprParser *p, const char *s)
{
    return strcmp(p->token.text, s) == 0;
}

static int
take(ExprParser *p, const char *s)
{
    if(!is(p, s)) return 0;
    next(p);
    return 1;
}

static void
expect(ExprParser *p, const char *s)
{
    if(!take(p, s)) p->failed = 1;
}

static int
node(ExprParser *p, ZirExprKind kind, size_t start, const char *name,
     const char *op, int left, int right)
{
    char text[ZIR_TEXT_MAX];
    size_t n = p->begin > start ? p->begin - start : 0;
    ZirSourceSpan span = p->span;
    ZirExpr *e;
    if(n >= sizeof(text)) { p->failed = 1; return -1; }
    memcpy(text, p->source + start, n);
    text[n] = 0;
    zir_trim_in_place(text);
    span.column += (int)start;
    e = ZirFunctionAddExpr(p->fn, kind, text, span);
    if(!e) { p->failed = 1; return -1; }
    zir_copy(e->name, sizeof(e->name), name);
    zir_copy(e->op, sizeof(e->op), op);
    e->left = left;
    e->right = right;
    return p->fn->expr_count - 1;
}

static int
type_name(const ExprParser *p, const char *s)
{
    static const char *const names[] = {
        "void", "bool", "char", "int", "float", "double", "short", "long",
        "signed", "unsigned", "const", "volatile", "size_t", "ptrdiff_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "intptr_t", "uintptr_t", "i8", "i16", "i32",
        "i64", "u8", "u16", "u32", "u64", "isize", "usize", "f32", "f64", "string", NULL
    };
    for(int i = 0; names[i]; i++) if(!strcmp(s, names[i])) return 1;
    return p->module != NULL && ZirFindType(p->module, s, NULL) != NULL;
}

static int expression(ExprParser *p, int minimum);

static int
record_initializer(ExprParser *p, size_t start, const char *type)
{
    int first = -1;
    int last = -1;
    int ordinal = 0;
    const ZirType *record = p->module ? ZirFindType(p->module, type, NULL) : NULL;
    expect(p, "{");
    while(!p->failed && !is(p, "}") && p->token.kind != ZIR_TOKEN_EOF) {
        size_t field_start = p->begin;
        char name[ZIR_NAME_MAX] = "";
        int named = take(p, ".");
        if(named) {
            if(p->token.kind != ZIR_TOKEN_IDENT) {
                p->failed = 1;
                break;
            }
            zir_copy(name, sizeof(name), p->token.text);
            next(p);
            expect(p, "=");
        }
        int value;
        if(is(p, "{")) {
            ZirTypeField field;
            size_t offset = 0;
            int position = 0;
            char field_type[ZIR_NAME_MAX] = "";
            ZirArrayElementType(type, field_type, sizeof(field_type), NULL);
            while(record != NULL && ZirTypeNextField(record, &offset, &field) == 1) {
                if(named ? !strcmp(field.name, name) : position == ordinal) {
                    zir_copy(field_type, sizeof(field_type), field.type);
                    break;
                }
                position++;
            }
            if(++p->depth > 128) {
                p->failed = 1;
                p->depth--;
                return -1;
            }
            value = record_initializer(p, p->begin, field_type);
            p->depth--;
        } else {
            value = expression(p, 1);
        }
        ordinal++;
        if(value < 0) {
            p->failed = 1;
            break;
        }
        int field = node(p, ZIR_EXPR_FIELD_INIT, field_start, name,
                         named ? "=" : "", -1, value);
        if(field < 0)
            break;
        if(last >= 0)
            p->fn->exprs[last].next_sibling = field;
        else
            first = field;
        last = field;
        if(!take(p, ","))
            break;
    }
    expect(p, "}");
    int result = node(p, ZIR_EXPR_COMPOUND, start, type, "", -1, -1);
    if(result >= 0)
        p->fn->exprs[result].first_child = first;
    return result;
}

static int
prefix(ExprParser *p)
{
    size_t start = p->begin;
    ZirToken tok = p->token;
    int result = -1;
    if(++p->depth > 128) { p->failed = 1; p->depth--; return -1; }
    if(take(p, "sizeof")) {
        int right;
        if(take(p, "(")) {
            if(type_name(p, p->token.text)) {
                size_t ts = p->begin;
                while(p->token.kind != ZIR_TOKEN_EOF && !is(p, ")")) next(p);
                right = node(p, ZIR_EXPR_IDENT, ts, "", "", -1, -1);
                expect(p, ")");
            } else {
                right = expression(p, 1);
                expect(p, ")");
            }
        } else right = prefix(p);
        result = node(p, ZIR_EXPR_SIZEOF, start, "", "", -1, right);
    } else if(is(p, "+") || is(p, "-") || is(p, "!") || is(p, "~") ||
              is(p, "*") || is(p, "&") || is(p, "++") || is(p, "--")) {
        int right;
        next(p);
        right = prefix(p);
        result = node(p, ZIR_EXPR_UNARY, start, "", tok.text, -1, right);
    } else if(take(p, "(")) {
        if(type_name(p, p->token.text) || is(p, "[")) {
            char type[ZIR_NAME_MAX];
            size_t ts = p->begin, length;
            if(is(p, "[")) {
                while(p->token.kind != ZIR_TOKEN_EOF && !is(p, ")"))
                    next(p);
            } else {
                while(p->token.kind == ZIR_TOKEN_IDENT || is(p, "*"))
                    next(p);
            }
            length = p->begin - ts;
            if(length >= sizeof(type)) { p->failed = 1; length = 0; }
            memcpy(type, p->source + ts, length); type[length] = 0;
            zir_trim_in_place(type);
            expect(p, ")");
            const ZirType *record = p->module ? ZirFindType(p->module, type, NULL) : NULL;
            if(is(p, "{") && ((record != NULL && !record->is_enum) ||
                               ZirArrayElementType(type, NULL, 0, NULL))) {
                result = record_initializer(p, start, type);
            } else if(is(p, "{")) {
                /* Foreign C aggregates retain their backend-owned syntax. */
                int depth = 0;
                do {
                    if(is(p, "{")) depth++;
                    if(is(p, "}")) depth--;
                    next(p);
                } while(depth && p->token.kind != ZIR_TOKEN_EOF);
                if(depth) p->failed = 1;
                result = node(p, ZIR_EXPR_COMPOUND, start, "", "", -1, -1);
            } else {
                int right = prefix(p);
                result = node(p, ZIR_EXPR_CAST, start, type, "", -1, right);
            }
        } else {
            result = expression(p, 1);
            expect(p, ")");
            /* Unknown imported C types still preserve compound literals. */
            if(is(p, "{")) {
                int depth = 0;
                do {
                    if(is(p, "{")) depth++;
                    if(is(p, "}")) depth--;
                    next(p);
                } while(depth && p->token.kind != ZIR_TOKEN_EOF);
                if(depth) p->failed = 1;
                result = node(p, ZIR_EXPR_COMPOUND, start, "", "", -1, -1);
            }
        }
    } else {
        ZirExprKind kind;
        switch(tok.kind) {
        case ZIR_TOKEN_IDENT: kind = ZIR_EXPR_IDENT; break;
        case ZIR_TOKEN_INT: kind = ZIR_EXPR_INT; break;
        case ZIR_TOKEN_FLOAT: kind = ZIR_EXPR_FLOAT; break;
        case ZIR_TOKEN_STRING: kind = ZIR_EXPR_STRING; break;
        case ZIR_TOKEN_CHAR: kind = ZIR_EXPR_CHAR; break;
        default: p->failed = 1; p->depth--; return -1;
        }
        next(p);
        result = node(p, kind, start, kind == ZIR_EXPR_IDENT ? tok.text : "", "", -1, -1);
    }
    while(!p->failed) {
        if(take(p, "[")) {
            int low = is(p, ":") ? -1 : expression(p, 1);
            if(take(p, ":")) {
                int high = is(p, "]") ? -1 : expression(p, 1);
                expect(p, "]");
                result = node(p, ZIR_EXPR_SLICE, start, "", "", result, low);
                if(result >= 0)
                    p->fn->exprs[result].third = high;
            } else {
                expect(p, "]");
                result = node(p, ZIR_EXPR_INDEX, start, "", "", result, low);
            }
        } else if(is(p, ".") || is(p, "->")) {
            int pointer = is(p, "->");
            char name[ZIR_NAME_MAX];
            next(p);
            if(p->token.kind != ZIR_TOKEN_IDENT) p->failed = 1;
            zir_copy(name, sizeof(name), p->token.text);
            next(p);
            result = node(p, pointer ? ZIR_EXPR_POINTER_MEMBER : ZIR_EXPR_MEMBER,
                          start, name, pointer ? "->" : ".", result, -1);
        } else if(take(p, "(")) {
            int first = -1, last = -1;
            char name[ZIR_NAME_MAX] = "";
            int callee = result;
            if(callee >= 0 && p->fn->exprs[callee].kind == ZIR_EXPR_IDENT)
                zir_copy(name, sizeof(name), p->fn->exprs[callee].name);
            if(!is(p, ")")) do {
                int child = expression(p, 1);
                if(child < 0) { p->failed = 1; break; }
                if(last >= 0) p->fn->exprs[last].next_sibling = child;
                else first = child;
                last = child;
            } while(take(p, ","));
            expect(p, ")");
            result = node(p, ZIR_EXPR_CALL, start, name, "", name[0] ? -1 : callee, -1);
            if(result >= 0) p->fn->exprs[result].first_child = first;
        } else if(is(p, "++") || is(p, "--")) {
            char op[8]; zir_copy(op, sizeof(op), p->token.text); next(p);
            result = node(p, ZIR_EXPR_POSTFIX, start, "", op, result, -1);
        } else break;
    }
    p->depth--;
    return result;
}

static int
precedence(const char *op)
{
    if(!strcmp(op, "?")) return 1;
    if(!strcmp(op, "||")) return 2;
    if(!strcmp(op, "&&")) return 3;
    if(!strcmp(op, "|")) return 4;
    if(!strcmp(op, "^")) return 5;
    if(!strcmp(op, "&")) return 6;
    if(!strcmp(op, "==") || !strcmp(op, "!=")) return 7;
    if(!strcmp(op, "<") || !strcmp(op, "<=") || !strcmp(op, ">") || !strcmp(op, ">=")) return 8;
    if(!strcmp(op, "<<") || !strcmp(op, ">>")) return 9;
    if(!strcmp(op, "+") || !strcmp(op, "-")) return 10;
    if(!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) return 11;
    return 0;
}

static int
expression(ExprParser *p, int minimum)
{
    size_t start = p->begin;
    int left = prefix(p), prec;
    if(++p->depth > 128) { p->failed = 1; p->depth--; return -1; }
    while(!p->failed && (prec = precedence(p->token.text)) >= minimum) {
        char op[8];
        int right;
        zir_copy(op, sizeof(op), p->token.text);
        next(p);
        right = expression(p, !strcmp(op, "?") ? 1 : prec + 1);
        if(!strcmp(op, "?")) {
            int third;
            expect(p, ":");
            third = expression(p, 1);
            left = node(p, ZIR_EXPR_CONDITIONAL, start, "", "?", left, right);
            if(left >= 0) p->fn->exprs[left].third = third;
        } else left = node(p, ZIR_EXPR_BINARY, start, "", op, left, right);
    }
    p->depth--;
    return left;
}

int
ZirParseExpr(ZirFunction *fn, const ZirModule *module, const char *text, ZirSourceSpan span)
{
    ExprParser p = {0};
    int initial = fn->expr_count, result;
    if(!*zir_skip_ws(text)) return -1;
    p.fn = fn; p.module = module; p.span = span; p.source = text;
    ZirLexerInit(&p.lexer, text, span.path);
    next(&p);
    result = expression(&p, 1);
    take(&p, ";");
    if(p.failed || p.token.kind != ZIR_TOKEN_EOF || result < 0) {
        fn->expr_count = initial;
        if(!ZirFunctionAddExpr(fn, ZIR_EXPR_UNKNOWN, text, span)) return -1;
        return fn->expr_count - 1;
    }
    return result;
}

void
ZirStructureFunction(ZirFunction *fn, const ZirModule *module)
{
    free(fn->exprs); fn->exprs = NULL; fn->expr_count = fn->expr_cap = 0;
    for(int i = 0; i < fn->stmt_count; i++) {
        ZirStmt *st = &fn->stmts[i];
        char text[ZIR_TEXT_MAX];
        char *value = NULL;
        zir_copy(text, sizeof(text), st->text);
        st->expr_root = st->lhs_root = -1;
        st->is_instance = 0;
        if(st->kind == ZIR_STMT_DECL) {
            char *colon = strchr(text, ':');
            if(colon) {
                *colon++ = 0; zir_trim_in_place(text);
                zir_copy(st->name, sizeof(st->name), text);
                char *annotation = strstr(colon, "#instance");
                char *assignment = strchr(colon, '=');
                if(annotation != NULL && (assignment == NULL || annotation < assignment)) {
                    char *key = annotation + strlen("#instance");
                    char *end = strrchr(key, ')');
                    const char *tail = end != NULL ? zir_skip_ws(end + 1) : "";
                    if(*tail == ';')
                        tail = zir_skip_ws(tail + 1);
                    key = (char *)zir_skip_ws(key);
                    if(*key != '(' || end == NULL || *tail != '\0') {
                        ZirDiagnostic(st->span, "parse.instance", "expected #instance(key) after a record type");
                        exit(1);
                    }
                    *annotation = '\0';
                    *end = '\0';
                    value = key + 1;
                    st->is_instance = 1;
                } else {
                    value = assignment;
                    if(value) *value++ = 0;
                }
                zir_trim_in_place(colon);
                zir_copy(st->type, sizeof(st->type), colon);
            }
        } else if(st->kind == ZIR_STMT_ASSIGN) {
            ZirLexer lexer; ZirToken tok;
            ZirLexerInit(&lexer, text, st->span.path);
            do {
                tok = ZirLexerNext(&lexer);
                if(!strcmp(tok.text, "=") || !strcmp(tok.text, "+=") ||
                   !strcmp(tok.text, "-=") || !strcmp(tok.text, "*=") ||
                   !strcmp(tok.text, "/=") || !strcmp(tok.text, "%=") ||
                   !strcmp(tok.text, "&=") || !strcmp(tok.text, "|=") ||
                   !strcmp(tok.text, "^=") || !strcmp(tok.text, "<<=") || !strcmp(tok.text, ">>=")) {
                    value = text + lexer.pos;
                    zir_copy(st->assignment_op, sizeof(st->assignment_op), tok.text);
                    text[lexer.pos - strlen(tok.text)] = 0;
                    st->lhs_root = ZirParseExpr(fn, module, text, st->span);
                    break;
                }
            } while(tok.kind != ZIR_TOKEN_EOF);
        } else if(st->kind == ZIR_STMT_RETURN) value = text + 6;
        else if(st->kind == ZIR_STMT_UNUSED) value = text + 6;
        else if(st->kind == ZIR_STMT_EXPR || st->kind == ZIR_STMT_BLOCK_CALL) value = text;
        else if(st->kind == ZIR_STMT_WHILE || st->kind == ZIR_STMT_IF || st->kind == ZIR_STMT_SWITCH) {
            zir_strip_block_brace(text);
            value = text;
            if(!strncmp(value, "else", 4)) value = (char *)zir_skip_ws(value + 4);
            while(*value && !isspace((unsigned char)*value) && *value != '(') value++;
        }
        if(value && strcmp(zir_skip_ws(value), ";")) {
            if(st->kind == ZIR_STMT_DECL && st->type[0] == '[' &&
               *zir_skip_ws(value) == '{') {
                char initializer[ZIR_TEXT_MAX];
                int length = snprintf(initializer, sizeof(initializer), "(%s)%s", st->type, value);
                if(length < 0 || (size_t)length >= sizeof(initializer)) {
                    ZirDiagnostic(st->span, "parse.array", "array initializer exceeds expression limit");
                    exit(1);
                }
                st->expr_root = ZirParseExpr(fn, module, initializer, st->span);
            } else {
                st->expr_root = ZirParseExpr(fn, module, value, st->span);
            }
        }
    }
}
