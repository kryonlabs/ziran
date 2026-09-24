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
    int failed, depth, internal_array_literal;
} ExprParser;

static void
next(ExprParser *p)
{
    p->token = LexerNext(&p->lexer);
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
    trim_in_place(text);
    span.column += (int)start;
    e = FunctionAddExpr(p->fn, kind, text, span);
    if(!e) { p->failed = 1; return -1; }
    copy_text(e->name, sizeof(e->name), name);
    copy_text(e->op, sizeof(e->op), op);
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
    return p->module != NULL && FindType(p->module, s, NULL) != NULL;
}

static int expression(ExprParser *p, int minimum);

static int
jai_char_byte(const char *text, int *value)
{
    size_t length = strlen(text);
    if(length == 3 && text[0] == '"' && text[2] == '"' &&
       (unsigned char)text[1] < 128) {
        *value = (unsigned char)text[1];
        return 1;
    }
    if(length == 4 && text[0] == '"' && text[1] == '\\' && text[3] == '"') {
        switch(text[2]) {
        case '0': *value = 0; return 1;
        case 'n': *value = '\n'; return 1;
        case 'r': *value = '\r'; return 1;
        case 't': *value = '\t'; return 1;
        case '"': *value = '"'; return 1;
        case '\\': *value = '\\'; return 1;
        default: return 0;
        }
    }
    if(length == 6 && text[0] == '"' && text[1] == '\\' &&
       text[2] == 'x' && text[5] == '"' &&
       isxdigit((unsigned char)text[3]) &&
       isxdigit((unsigned char)text[4])) {
        char digits[3] = {text[3], text[4], 0};
        *value = (int)strtol(digits, NULL, 16);
        return 1;
    }
    return 0;
}

static int
record_initializer(ExprParser *p, size_t start, const char *type)
{
    int first = -1;
    int last = -1;
    int ordinal = 0;
    const ZirType *record = p->module ? FindType(p->module, type, NULL) : NULL;
    expect(p, "{");
    while(!p->failed && !is(p, "}") && p->token.kind != ZIR_TOKEN_EOF) {
        size_t field_start = p->begin;
        char name[ZIR_NAME_MAX] = "";
        int named = take(p, ".");
        if(!named && p->token.kind == ZIR_TOKEN_IDENT) {
            ZirLexer lookahead = p->lexer;
            ZirToken following = LexerNext(&lookahead);
            named = !strcmp(following.text, "=");
        }
        if(named) {
            if(p->token.kind != ZIR_TOKEN_IDENT) {
                p->failed = 1;
                break;
            }
            copy_text(name, sizeof(name), p->token.text);
            next(p);
            expect(p, "=");
        }
        int value;
        if(is(p, "{")) {
            ZirTypeField field;
            size_t offset = 0;
            int position = 0;
            char field_type[ZIR_NAME_MAX] = "";
            ArrayElementType(type, field_type, sizeof(field_type), NULL);
            while(record != NULL && TypeNextField(record, &offset, &field) == 1) {
                if(named ? !strcmp(field.name, name) : position == ordinal) {
                    copy_text(field_type, sizeof(field_type), field.type);
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
    if(take(p, "#char")) {
        int value;
        if(p->token.kind != ZIR_TOKEN_STRING ||
           !jai_char_byte(p->token.text, &value)) {
            Diagnostic(p->span, "parse.jai_char",
                       "#char requires a one-byte string literal");
            exit(1);
        }
        next(p);
        result = node(p, ZIR_EXPR_INT, start, "", "", -1, -1);
        if(result >= 0)
            snprintf(p->fn->exprs[result].text,
                     sizeof(p->fn->exprs[result].text), "%d", value);
    } else if(take(p, "ifx")) {
        int condition = expression(p, 2);
        take(p, "then");
        int selected = expression(p, 1);
        expect(p, "else");
        int alternative = expression(p, 1);
        result = node(p, ZIR_EXPR_CONDITIONAL, start, "", "ifx",
                      condition, selected);
        if(result >= 0)
            p->fn->exprs[result].third = alternative;
    } else if(take(p, "sizeof")) {
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
    } else if(take(p, "cast")) {
        char type[ZIR_NAME_MAX];
        size_t ts, length;
        expect(p, "(");
        ts = p->begin;
        if(!type_name(p, p->token.text) && !is(p, "[") && !is(p, "*"))
            p->failed = 1;
        while(p->token.kind != ZIR_TOKEN_EOF && !is(p, ")")) next(p);
        length = p->begin - ts;
        if(length >= sizeof(type)) { p->failed = 1; length = 0; }
        memcpy(type, p->source + ts, length);
        type[length] = 0;
        trim_in_place(type);
        expect(p, ")");
        int right = prefix(p);
        result = node(p, ZIR_EXPR_CAST, start, type, "", -1, right);
    } else if(is(p, "+") || is(p, "-") || is(p, "!") || is(p, "~") ||
              is(p, "*") || is(p, "<<") || is(p, "++") || is(p, "--")) {
        int right;
        const char *op = !strcmp(tok.text, "*") ? "&" :
                         !strcmp(tok.text, "<<") ? "*" : tok.text;
        next(p);
        right = prefix(p);
        result = node(p, ZIR_EXPR_UNARY, start, "", op, -1, right);
    } else if(is(p, "&")) {
        Diagnostic(p->span, "parse.jai_syntax",
                   "C-style address-of is not valid Jai syntax; use *value");
        exit(1);
    } else if(take(p, "(")) {
        if(is(p, "[") && p->internal_array_literal) {
            char type[ZIR_NAME_MAX];
            size_t ts = p->begin;
            while(p->token.kind != ZIR_TOKEN_EOF && !is(p, ")")) next(p);
            size_t length = p->begin - ts;
            if(length >= sizeof(type)) { p->failed = 1; length = 0; }
            memcpy(type, p->source + ts, length);
            type[length] = 0;
            trim_in_place(type);
            expect(p, ")");
            result = record_initializer(p, start, type);
        } else if(type_name(p, p->token.text) || is(p, "[")) {
            Diagnostic(p->span, "parse.jai_syntax",
                       "C-style cast or literal is not valid Jai syntax; use cast(Type) value or Type.{...}");
            exit(1);
        } else {
            result = expression(p, 1);
            expect(p, ")");
            if(is(p, "{")) {
                Diagnostic(p->span, "parse.jai_syntax",
                           "C-style literal is not valid Jai syntax; use Type.{...}");
                exit(1);
            }
        }
    } else {
        ZirExprKind kind;
        switch(tok.kind) {
        case ZIR_TOKEN_IDENT: kind = ZIR_EXPR_IDENT; break;
        case ZIR_TOKEN_INT: kind = ZIR_EXPR_INT; break;
        case ZIR_TOKEN_FLOAT: kind = ZIR_EXPR_FLOAT; break;
        case ZIR_TOKEN_STRING: kind = ZIR_EXPR_STRING; break;
        case ZIR_TOKEN_CHAR:
            Diagnostic(p->span, "parse.jai_char",
                       "single-quoted character literals are not valid Jai syntax; use #char \"x\"");
            exit(1);
        default: p->failed = 1; p->depth--; return -1;
        }
        next(p);
        if(kind == ZIR_EXPR_IDENT && is(p, ".")) {
            ZirLexer lookahead = p->lexer;
            ZirToken following = LexerNext(&lookahead);
            if(!strcmp(following.text, "{")) {
                const ZirType *record = p->module ?
                    FindType(p->module, tok.text, NULL) : NULL;
                if(record == NULL || record->is_enum) p->failed = 1;
                next(p);
                result = record_initializer(p, start, tok.text);
            }
        }
        if(result < 0 && !p->failed)
            result = node(p, kind, start,
                          kind == ZIR_EXPR_IDENT ? tok.text : "", "", -1, -1);
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
        } else if(is(p, "->")) {
            Diagnostic(p->span, "parse.jai_syntax",
                       "C-style pointer member access is not valid Jai syntax; use .field");
            exit(1);
        } else if(is(p, ".")) {
            char name[ZIR_NAME_MAX];
            next(p);
            if(p->token.kind != ZIR_TOKEN_IDENT) p->failed = 1;
            copy_text(name, sizeof(name), p->token.text);
            next(p);
            result = node(p, ZIR_EXPR_MEMBER, start, name, ".", result, -1);
        } else if(take(p, "(")) {
            int first = -1, last = -1;
            char name[ZIR_NAME_MAX] = "";
            int callee = result;
            if(callee >= 0 && p->fn->exprs[callee].kind == ZIR_EXPR_IDENT)
                copy_text(name, sizeof(name), p->fn->exprs[callee].name);
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
            char op[8]; copy_text(op, sizeof(op), p->token.text); next(p);
            result = node(p, ZIR_EXPR_POSTFIX, start, "", op, result, -1);
        } else if(is(p, "?") && p->begin > 0 &&
                  !isspace((unsigned char)p->source[p->begin - 1])) {
            /* An adjacent '?' unwraps a Result. Spaced '?' remains the
             * conditional operator and keeps existing source unambiguous. */
            next(p);
            result = node(p, ZIR_EXPR_POSTFIX, start, "", "?", result, -1);
        } else break;
    }
    p->depth--;
    return result;
}

static int
precedence(const char *op)
{
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
        copy_text(op, sizeof(op), p->token.text);
        next(p);
        right = expression(p, prec + 1);
        left = node(p, ZIR_EXPR_BINARY, start, "", op, left, right);
    }
    if(is(p, "?")) {
        Diagnostic(p->span, "parse.jai_syntax",
                   "C-style conditional is not valid Jai syntax; use ifx ... then ... else ...");
        exit(1);
    }
    p->depth--;
    return left;
}

static int
parse_expr(ZirFunction *fn, const ZirModule *module, const char *text,
           ZirSourceSpan span, int internal_array_literal)
{
    ExprParser p = {0};
    int initial = fn->expr_count, result;
    if(!*skip_ws(text)) return -1;
    p.fn = fn; p.module = module; p.span = span; p.source = text;
    p.internal_array_literal = internal_array_literal;
    LexerInit(&p.lexer, text, span.path);
    next(&p);
    result = expression(&p, 1);
    take(&p, ";");
    if(p.failed || p.token.kind != ZIR_TOKEN_EOF || result < 0) {
        fn->expr_count = initial;
        if(!FunctionAddExpr(fn, ZIR_EXPR_UNKNOWN, text, span)) return -1;
        return fn->expr_count - 1;
    }
    return result;
}

int
ParseExpr(ZirFunction *fn, const ZirModule *module, const char *text,
          ZirSourceSpan span)
{
    return parse_expr(fn, module, text, span, 0);
}

void
StructureFunction(ZirFunction *fn, const ZirModule *module)
{
    free(fn->exprs); fn->exprs = NULL; fn->expr_count = fn->expr_cap = 0;
    for(int i = 0; i < fn->stmt_count; i++) {
        ZirStmt *st = &fn->stmts[i];
        char text[ZIR_TEXT_MAX];
        char *value = NULL;
        copy_text(text, sizeof(text), st->text);
        st->expr_root = st->lhs_root = -1;
        st->is_else = st->kind == ZIR_STMT_IF &&
                      strncmp(text, "else", 4) == 0 &&
                      (text[4] == 0 || isspace((unsigned char)text[4]));
        st->is_guard = st->kind == ZIR_STMT_IF &&
                       strncmp(text, "guard", 5) == 0 &&
                       (text[5] == 0 || isspace((unsigned char)text[5]));
        if(st->kind == ZIR_STMT_DECL) {
            char *colon = strchr(text, ':');
            if(colon) {
                *colon++ = 0; trim_in_place(text);
                copy_text(st->name, sizeof(st->name), text);
                value = strchr(colon, '=');
                if(value) *value++ = 0;
                trim_in_place(colon);
                size_t type_length = strlen(colon);
                if(type_length && colon[type_length - 1] == ';') {
                    colon[--type_length] = '\0';
                    trim_in_place(colon);
                }
                if(strchr(colon, '#') != NULL) {
                    Diagnostic(st->span, "parse.modifier",
                        "unknown declaration modifier: %s", strchr(colon, '#'));
                    exit(1);
                }
                copy_text(st->type, sizeof(st->type), colon);
            }
        } else if(st->kind == ZIR_STMT_ASSIGN) {
            ZirLexer lexer; ZirToken tok;
            LexerInit(&lexer, text, st->span.path);
            do {
                tok = LexerNext(&lexer);
                if(!strcmp(tok.text, "=") || !strcmp(tok.text, "+=") ||
                   !strcmp(tok.text, "-=") || !strcmp(tok.text, "*=") ||
                   !strcmp(tok.text, "/=") || !strcmp(tok.text, "%=") ||
                   !strcmp(tok.text, "&=") || !strcmp(tok.text, "|=") ||
                   !strcmp(tok.text, "^=") || !strcmp(tok.text, "<<=") || !strcmp(tok.text, ">>=")) {
                    value = text + lexer.pos;
                    copy_text(st->assignment_op, sizeof(st->assignment_op), tok.text);
                    text[lexer.pos - strlen(tok.text)] = 0;
                    st->lhs_root = ParseExpr(fn, module, text, st->span);
                    break;
                }
            } while(tok.kind != ZIR_TOKEN_EOF);
        } else if(st->kind == ZIR_STMT_RETURN) value = text + 6;
        else if(st->kind == ZIR_STMT_UNUSED) value = text + 6;
        else if(st->kind == ZIR_STMT_EXPR || st->kind == ZIR_STMT_BLOCK_CALL) value = text;
        else if(st->kind == ZIR_STMT_WHILE || st->kind == ZIR_STMT_IF ||
                st->kind == ZIR_STMT_SWITCH || st->kind == ZIR_STMT_MATCH) {
            strip_block_brace(text);
            value = text;
            if(!strncmp(value, "else", 4)) value = (char *)skip_ws(value + 4);
            while(*value && !isspace((unsigned char)*value) && *value != '(') value++;
        }
        if(value && strcmp(skip_ws(value), ";")) {
            if(st->kind == ZIR_STMT_DECL && st->type[0] == '[' &&
               *skip_ws(value) == '{') {
                Diagnostic(st->span, "parse.jai_syntax",
                           "C-style array literal is not valid Jai syntax; use .[...]");
                exit(1);
            }
            if(st->kind == ZIR_STMT_DECL && st->type[0] == '[' &&
               skip_ws(value)[0] == '.' && skip_ws(value)[1] == '[') {
                char initializer[ZIR_TEXT_MAX];
                const char *contents = skip_ws(value) + 2;
                size_t content_length = strlen(contents);
                while(content_length && isspace((unsigned char)contents[content_length - 1]))
                    content_length--;
                if(content_length && contents[content_length - 1] == ';')
                    content_length--;
                while(content_length && isspace((unsigned char)contents[content_length - 1]))
                    content_length--;
                if(!content_length || contents[content_length - 1] != ']') {
                    Diagnostic(st->span, "parse.array", "invalid Jai array literal");
                    exit(1);
                }
                int length = snprintf(initializer, sizeof(initializer),
                    "(%s){%.*s}", st->type, (int)(content_length - 1), contents);
                if(length < 0 || (size_t)length >= sizeof(initializer)) {
                    Diagnostic(st->span, "parse.array", "array initializer exceeds expression limit");
                    exit(1);
                }
                st->expr_root = parse_expr(fn, module, initializer, st->span, 1);
            } else {
                st->expr_root = ParseExpr(fn, module, value, st->span);
            }
        }
    }
}
