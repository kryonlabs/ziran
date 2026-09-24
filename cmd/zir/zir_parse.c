/*
 * zir_parse.c - shared Ziran frontend: parse .zi source into a ZirProgram.
 * Linked by the IR and native backends.
 */
#include "zir.h"
#include "zir_parse.h"
#include "zir_text.h"
#include "zir_cleanup.h"
#include "zir_expr.h"
#include "zir_diagnostic.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    K2ZIR_PATH_MAX = 1024,
    K2ZIR_LINE_MAX = 4096
};

static void
die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    DiagnosticV(Span("", 0, 0), "parse.fatal", fmt, ap);
    va_end(ap);
    exit(1);
}

static void
die_at(ZirSourceSpan span, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    DiagnosticV(span, "parse.syntax", fmt, args);
    va_end(args);
    exit(1);
}

static int
starts_word(const char *s, const char *word)
{
    size_t n = strlen(word);

    return strncmp(s, word, n) == 0 &&
           (s[n] == '\0' || s[n] == ' ' || s[n] == '\t' ||
            s[n] == '(' || s[n] == '"' || s[n] == '{' ||
            s[n] == ':');   /* 'default:' — label-style case keyword */
}

static int
is_identifier_text(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if(cursor == NULL || (!isalpha(*cursor) && *cursor != '_'))
        return 0;
    cursor++;
    while(isalnum(*cursor) || *cursor == '_')
        cursor++;
    return *cursor == '\0';
}

static int
contains_source_directive(const char *source, const char *directive)
{
    size_t length = strlen(directive);
    int quoted = 0;

    for(const char *cursor = source; *cursor != '\0'; cursor++) {
        if(quoted) {
            if(*cursor == '\\' && cursor[1] != '\0')
                cursor++;
            else if(*cursor == '"')
                quoted = 0;
        } else if(cursor[0] == '/' && cursor[1] == '/') {
            return 0;
        } else if(*cursor == '"') {
            quoted = 1;
        } else if(strncmp(cursor, directive, length) == 0 &&
                  !isalnum((unsigned char)cursor[length]) &&
                  cursor[length] != '_') {
            return 1;
        }
    }
    return 0;
}

static int
source_column_for_trimmed(const char *line, const char *trimmed)
{
    if(line == NULL || trimmed == NULL || trimmed < line)
        return 1;
    return (int)(trimmed - line) + 1;
}

static int
source_end_column_for_trimmed(const char *line, const char *trimmed)
{
    return source_column_for_trimmed(line, trimmed) + (int)strlen(trimmed);
}

static int
parse_symbol_before_colons(const char *s, char *out, size_t out_size)
{
    const char *p;
    const char *q;
    size_t n = 0;

    out[0] = '\0';
    p = strstr(s, "::");
    if(p == NULL)
        return 0;
    q = s;
    while(q < p && (*q == ' ' || *q == '\t'))
        q++;
    while(q < p && (isalnum((unsigned char)*q) || *q == '_') &&
          n + 1 < out_size)
        out[n++] = *q++;
    out[n] = '\0';
    while(q < p && (*q == ' ' || *q == '\t'))
        q++;
    return out[0] != '\0' && q == p;
}

/* Jai places polymorphic parameters on the type constructor, not on the
 * declaration name: Box :: struct($T: Type) { value: T; }.  The checker keeps
 * just the bound names for substitution. */
static int
parse_type_parameters(const char *after, const char *keyword,
                      char *names, size_t capacity)
{
    const char *cursor = skip_ws(after + strlen(keyword));
    char source[ZIR_TEXT_MAX];
    char parts[16][ZIR_NAME_MAX];
    size_t used = 0;
    int count;

    names[0] = '\0';
    if(*cursor != '(')
        return 1;
    const char *start = ++cursor;
    int depth = 1;
    while(*cursor && depth) {
        if(*cursor == '(') depth++;
        else if(*cursor == ')') depth--;
        if(depth) cursor++;
    }
    if(depth || cursor == start ||
       (size_t)(cursor - start) >= sizeof(source) ||
       *skip_ws(cursor + 1) != '{')
        return 0;
    memcpy(source, start, (size_t)(cursor - start));
    source[cursor - start] = '\0';
    count = split_top_level(source, parts[0], 16, sizeof(parts[0]));
    if(count < 1 || count >= 16)
        return 0;
    for(int i = 0; i < count; i++) {
        const char *part = parts[i];
        if(*part == '$') part++;
        const char *colon = strchr(part, ':');
        if(colon == NULL) return 0;
        size_t length = (size_t)(colon - part);
        while(length && isspace((unsigned char)part[length - 1])) length--;
        if(length == 0 || length >= ZIR_NAME_MAX ||
           strcmp(skip_ws(colon + 1), "Type") != 0)
            return 0;
        char name[ZIR_NAME_MAX];
        memcpy(name, part, length);
        name[length] = '\0';
        if(!is_identifier_text(name) ||
           used + length + (i ? 1u : 0u) >= capacity)
            return 0;
        if(i) names[used++] = ',';
        memcpy(names + used, name, length);
        used += length;
        names[used] = '\0';
    }
    return 1;
}

static const char *
relative_path(const char *root, const char *path)
{
    size_t n;

    if(root == NULL || root[0] == '\0')
        return path;
    n = strlen(root);
    if(strncmp(path, root, n) == 0 && (path[n] == '/' || path[n] == '\0')) {
        if(path[n] == '/')
            return path + n + 1;
        return path + n;
    }
    return path;
}

static int
parse_quoted(const char *s, char *out, size_t out_size)
{
    const char *q;
    size_t n = 0;

    q = strchr(s, '"');
    if(q == NULL)
        return 0;
    q++;
    while(*q != '\0' && *q != '"' && n + 1 < out_size)
        out[n++] = *q++;
    out[n] = '\0';
    return *q == '"';
}

static int
parse_angled(const char *s, char *out, size_t out_size)
{
    const char *q;
    size_t n = 0;

    q = strchr(s, '<');
    if(q == NULL)
        return 0;
    q++;
    while(*q != '\0' && *q != '>' && n + 1 < out_size)
        out[n++] = *q++;
    out[n] = '\0';
    return *q == '>';
}

static int
is_c_ident(const char *s)
{
    if(s == NULL || s[0] == '\0')
        return 0;
    if(!(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return 0;
    for(const char *p = s + 1; *p != '\0'; p++) {
        if(!(isalnum((unsigned char)*p) || *p == '_'))
            return 0;
    }
    return 1;
}

static ZirExternKind
classify_extern_target(const char *target, char *symbol, size_t symbol_size,
                       const char *path, int line_no)
{
    const char *dot;
    const char *slash;

    symbol[0] = '\0';
    if(target == NULL || target[0] == '\0')
        return ZIR_EXTERN_HOST;
    if(strncmp(target, "c.", 2) == 0) {
        if(!is_c_ident(target + 2))
            die_at(Span(path, line_no, 1), "C extern target must be c.<symbol>");
        snprintf(symbol, symbol_size, "%s", target + 2);
        return ZIR_EXTERN_C;
    }
    dot = strrchr(target, '.');
    slash = strrchr(target, '/');
    if(dot != NULL && slash != NULL && slash < dot)
        return ZIR_EXTERN_GO;
    return ZIR_EXTERN_HOST;
}

/* Net block braces: only '{'/'}' at paren/bracket depth 0 open/close
 * blocks. Braces inside parens (compound literals like (Props){...}) are
 * expression braces, not blocks. */
static int
net_block_braces(const char *s)
{
    int pd = 0;
    int in_s = 0;
    int in_c = 0;
    int delta = 0;

    for(const char *p = s; *p != '\0'; p++) {
        if(in_s) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '"')
                in_s = 0;
        } else if(in_c) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '\'')
                in_c = 0;
        } else if(*p == '"') {
            in_s = 1;
        } else if(*p == '\'') {
            in_c = 1;
        } else if(*p == '(' || *p == '[') {
            pd++;
        } else if(*p == ')' || *p == ']') {
            if(pd > 0)
                pd--;
        } else if(pd == 0) {
            if(*p == '{')
                delta++;
            else if(*p == '}')
                delta--;
        }
    }
    return delta;
}

static ZirStmtKind
classify_stmt(const char *s)
{
    if(s[0] == '}')
        return ZIR_STMT_BLOCK_CLOSE;
    if(strcmp(s, "{") == 0)
        return ZIR_STMT_BLOCK_OPEN;
    if(starts_word(s, "if") || starts_word(s, "else"))
        return ZIR_STMT_IF;
    if(starts_word(s, "guard"))
        return ZIR_STMT_IF;   /* 'guard cond' lowers to if(cond) return */
    if(starts_word(s, "while"))
        return ZIR_STMT_WHILE;
    if(starts_word(s, "for"))
        return ZIR_STMT_FOR;
    if(starts_word(s, "switch"))
        return ZIR_STMT_SWITCH;
    if(starts_word(s, "match"))
        return ZIR_STMT_MATCH;
    if(starts_word(s, "case") || starts_word(s, "default"))
        return ZIR_STMT_CASE;
    if(strcmp(s, "default:") == 0)
        return ZIR_STMT_CASE;
    if(starts_word(s, "return"))
        return ZIR_STMT_RETURN;
    if(strcmp(s, "unreachable") == 0 || strcmp(s, "unreachable;") == 0)
        return ZIR_STMT_UNREACHABLE;
    if(strcmp(s, "break") == 0 || strcmp(s, "break;") == 0)
        return ZIR_STMT_BREAK;
    if(strcmp(s, "continue") == 0 || strcmp(s, "continue;") == 0)
        return ZIR_STMT_CONTINUE;
    if(starts_word(s, "goto"))
        return ZIR_STMT_GOTO;
    /* goto label: a bare 'name:' / 'name: ;' — a following type marks a
     * declaration ('scalar: int = 5'), not a label */
    {
        size_t n = 0;

        while(isalnum((unsigned char)s[n]) || s[n] == '_')
            n++;
        if(n > 0 && s[n] == ':') {
            const char *rest = s + n + 1;

            while(*rest == ' ' || *rest == '\t')
                rest++;
            if(*rest == '\0' || strcmp(rest, ";") == 0)
                return ZIR_STMT_LABEL;
        }
    }
    if(starts_word(s, "defer"))
        return ZIR_STMT_DEFER;
    if(starts_word(s, "unused"))
        return ZIR_STMT_UNUSED;
    if(strstr(s, ":=") != NULL)
        return ZIR_STMT_DECL;   /* ':=' wins over the raw 'c' prefix (a
                                   variable may be named 'c') */
    if(starts_word(s, "c") && s[1] != ':') {
        /* 'c <raw C line>' glue — but 'c = ...' / 'c += ...' is an
         * assignment to a local named c, which needs the normal statement
         * path (raw lines emit without a trailing semicolon). 'c:' is a
         * typed decl of a variable named c. */
        const char *rest = s + 1;

        while(*rest == ' ' || *rest == '\t')
            rest++;
        if(*rest != '=' && strncmp(rest, "+=", 2) != 0 &&
           strncmp(rest, "-=", 2) != 0 && strncmp(rest, "*=", 2) != 0 &&
           strncmp(rest, "/=", 2) != 0 && strncmp(rest, "%=", 2) != 0 &&
           strncmp(rest, "&=", 2) != 0 && strncmp(rest, "|=", 2) != 0 &&
           strncmp(rest, "^=", 2) != 0 && strncmp(rest, "<<=", 3) != 0 &&
           strncmp(rest, ">>=", 3) != 0)
            return ZIR_STMT_RAW;
    }
    if(strstr(s, "::") != NULL)
        return ZIR_STMT_RAW;   /* nested '::' definitions stay raw */
    if(strstr(s, ": ") != NULL || strstr(s, ": [") != NULL) {
        /* typed decl only when an identifier precedes the colon */
        const char *c2 = strstr(s, ": ");

        if(c2 == NULL)
            c2 = strstr(s, ": [");
        if(c2 != NULL && c2 > s &&
           (isalpha((unsigned char)s[0]) || s[0] == '_')) {
            int ident_only = 1;

            for(const char *q = s; q < c2; q++)
                if(!(isalnum((unsigned char)*q) || *q == '_'))
                    ident_only = 0;
            if(ident_only)
                return ZIR_STMT_DECL;  /* 'x: T' / 'x: [N] T' */
        }
    }
    /* C-style locals remain accepted at the language boundary. Lowerers
     * already treat declaration statements without ':' as an opaque typed
     * declaration and add the target terminator. */
    {
        static const char *const types[] = {
            "int ", "unsigned ", "long ", "float ", "double ",
            "char ", "bool ", "const ", "struct ", NULL
        };
        int i;

        for(i = 0; types[i] != NULL; i++)
            if(strncmp(s, types[i], strlen(types[i])) == 0)
                return ZIR_STMT_DECL;
    }
    /* An '=' inside a call's compound literal is a designated initializer,
     * not an assignment statement (Make((Props){.value = input})). */
    {
        int depth = 0;
        int quote = 0;
        const char *p;

        for(p = s; *p != '\0'; p++) {
            if(quote) {
                if(*p == '\\' && p[1] != '\0')
                    p++;
                else if(*p == quote)
                    quote = 0;
                continue;
            }
            if(*p == '"' || *p == '\'')
                quote = *p;
            else if(*p == '(' || *p == '[' || *p == '{')
                depth++;
            else if(*p == ')' || *p == ']' || *p == '}') {
                if(depth > 0)
                    depth--;
            } else if(*p == '=' && depth == 0)
                return ZIR_STMT_ASSIGN;
        }
    }
    if(strchr(s, '(') != NULL || strchr(s, '+') != NULL ||
       strchr(s, '-') != NULL)
        return ZIR_STMT_EXPR;
    return ZIR_STMT_UNKNOWN;
}

static int
unwrap_outer_parentheses(const char *text, char *out, size_t out_size)
{
    const char *p = text;
    const char *open;
    const char *close = NULL;
    int depth = 0;
    int in_string = 0;

    while(*p == ' ' || *p == '\t')
        p++;
    if(*p != '(')
        return 0;
    open = p++;
    depth = 1;
    while(*p != '\0') {
        if(in_string) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '"')
                in_string = 0;
        } else if(*p == '"') {
            in_string = 1;
        } else if(*p == '(') {
            depth++;
        } else if(*p == ')' && --depth == 0) {
            close = p;
            break;
        }
        p++;
    }
    if(close == NULL)
        return 0;
    p = close + 1;
    while(*p == ' ' || *p == '\t' || *p == ';')
        p++;
    if(*p != '\0' || (size_t)(close - open) >= out_size)
        return 0;
    memcpy(out, open + 1, (size_t)(close - open - 1));
    out[close - open - 1] = '\0';
    trim_in_place(out);
    return out[0] != '\0';
}

static int
parse_direct_call_statement(const char *text, char *name, size_t name_size,
                            char *args, size_t args_size)
{
    const char *p = text;
    const char *open;
    const char *close;
    size_t length;
    int depth = 0;
    int in_string = 0;
    char inner[ZIR_TEXT_MAX];

    while(*p == ' ' || *p == '\t')
        p++;
    if(unwrap_outer_parentheses(p, inner, sizeof(inner)))
        return parse_direct_call_statement(inner, name, name_size, args,
                                           args_size);
    open = p;
    while(isalnum((unsigned char)*p) || *p == '_')
        p++;
    length = (size_t)(p - open);
    if(length == 0 || length >= name_size)
        return 0;
    memcpy(name, open, length);
    name[length] = '\0';
    while(*p == ' ' || *p == '\t')
        p++;
    if(*p != '(')
        return 0;
    open = p++;
    close = NULL;
    depth = 1;
    while(*p != '\0') {
        if(in_string) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '"')
                in_string = 0;
        } else if(*p == '"') {
            in_string = 1;
        } else if(*p == '(') {
            depth++;
        } else if(*p == ')' && --depth == 0) {
            close = p;
            break;
        }
        p++;
    }
    if(close == NULL || (size_t)(close - open) >= args_size)
        return 0;
    p = close + 1;
    while(*p == ' ' || *p == '\t' || *p == ';')
        p++;
    if(*p != '\0')
        return 0;
    memcpy(args, open + 1, (size_t)(close - open - 1));
    args[close - open - 1] = '\0';
    return 1;
}

typedef struct BlockCall {
    char callee[ZIR_NAME_MAX];
    char name[ZIR_NAME_MAX];
    ZirSourceSpan span;
    char props[ZIR_TEXT_MAX];
    int close_depth;
    int statement_index;
    int opened;
} BlockCall;

typedef struct SlotParseFrame {
    int function_index;
    int depth;
    int root_anonymous_count;
    int block_count;
    BlockCall *blocks;
    int body_count;
    int body_depth[8];
} SlotParseFrame;

static int
parse_block_call_header(const char *text, char *callee, size_t callee_size,
                      char *name, size_t name_size)
{
    const char *p = text;
    const char *start;
    size_t n = 0;

    while(*p == ' ' || *p == '\t')
        p++;
    start = p;
    while(isalnum((unsigned char)*p) || *p == '_')
        p++;
    n = (size_t)(p - start);
    if(n == 0 || n >= callee_size)
        return 0;
    memcpy(callee, start, n);
    callee[n] = '\0';
    /* Switch labels share the colon-and-brace shape of a block call. */
    if(strcmp(callee, "case") == 0 || strcmp(callee, "default") == 0)
        return 0;
    while(*p == ' ' || *p == '\t')
        p++;
    start = p;
    n = 0;
    while(isalnum((unsigned char)*p) || *p == '_') {
        if(n + 1 < name_size)
            name[n++] = *p;
        p++;
    }
    if((size_t)(p - start) >= name_size)
        return 0;
    name[n] = '\0';
    while(*p == ' ' || *p == '\t')
        p++;
    int has_colon = *p == ':';
    if(has_colon)
        p++;
    while(*p == ' ' || *p == '\t')
        p++;
    if(p[0] != '{' || p[1] != '\0')
        return 0;
    if(!has_colon)
        return 0;
    return 1;
}

static int
parse_block_field_line(char *text, char *field, size_t field_size,
                   char *value, size_t value_size)
{
    char *eq;
    char *name;
    char *expr;
    size_t n;

    eq = strchr(text, '=');
    /* Only the property separator must be assignment. Comparisons in its
     * value are valid expressions (for example, when = count >= limit). */
    if(eq == NULL || eq[1] == '=')
        return 0;
    *eq = '\0';
    name = trim(text);
    expr = trim(eq + 1);
    if(name[0] == '\0' || expr[0] == '\0')
        return 0;
    for(const char *p = name; *p != '\0'; p++)
        if(!isalnum((unsigned char)*p) && *p != '_')
            return 0;
    n = strlen(expr);
    if(n > 0 && expr[n - 1] == ';') {
        expr[n - 1] = '\0';
        expr = trim(expr);
    }
    snprintf(field, field_size, "%s", name);
    snprintf(value, value_size, "%s", expr);
    return 1;
}

static void
block_call_append_field(BlockCall *block, const char *field, const char *value,
                     ZirSourceSpan span)
{
    size_t used = strlen(block->props);
    int length = snprintf(block->props + used, sizeof(block->props) - used,
                          ".%s = %s, ", field, value);
    if(length < 0 || (size_t)length >= sizeof(block->props) - used)
        die_at(span, "block-call fields exceed the declaration size limit: %s", block->callee);
}

static void
block_call_open(ZirFunction *fn, BlockCall *block, ZirSourceSpan span, int closing)
{
    ZirSourceSpan source_span;
    ZirStmt *statement;

    if(block == NULL || block->opened)
        return;
    if(!closing)
        die_at(span, "block content requires a declared slot parameter");
    source_span = block->span.path[0] != '\0' ? block->span : span;
    statement = FunctionAddBlockCall(fn, block->callee, block->props,
                                     block->name,
                                     source_span);
    if(statement == NULL)
        die("out of memory parsing block call");
    statement->declared_block_call = 1;
    block->statement_index = (int)(statement - fn->stmts);
    block->opened = 1;
}

static ZirSourceSpan
block_call_close_span(const BlockCall *block, const char *path, int line_no,
                    const char *line)
{
    ZirSourceSpan start = block != NULL && block->span.path[0] != '\0'
                            ? block->span
                            : Span(path, line_no, 1);
    int end_column = (int)strlen(line) + 1;

    return SpanEnd(start.path, start.line, start.column, line_no,
                      end_column > 0 ? end_column : 1);
}

static int
split_oneline_block(const char *t, char *head, size_t hsz,
                    char *body, size_t bsz)
{
    static const char *kws[] = { "if", "else", "while", "for", "switch",
                                 "case", "default", "guard", "do" };
    size_t n = strlen(t);
    size_t brace_pos = 0;
    int depth = 0;
    int in_str = 0;
    int in_chr = 0;
    char w0[16];
    size_t wl = 0;
    size_t i;

    if(n < 8 || t[n - 1] != '}')
        return 0;
    for(i = 0; t[i] != '\0' && (isalnum((unsigned char)t[i]) || t[i] == '_') &&
        wl + 1 < sizeof(w0); i++)
        w0[wl++] = t[i];
    w0[wl] = '\0';
    {
        int is_kw = 0;

        for(size_t k = 0; k < sizeof(kws) / sizeof(kws[0]); k++)
            if(strcmp(w0, kws[k]) == 0)
                is_kw = 1;
        if(!is_kw)
            return 0;
    }
    for(i = 0; i < n; i++) {
        char ch = t[i];

        if(in_str) {
            if(ch == '\\' && i + 1 < n)
                i++;
            else if(ch == '"')
                in_str = 0;
        } else if(in_chr) {
            if(ch == '\\' && i + 1 < n)
                i++;
            else if(ch == '\'')
                in_chr = 0;
        } else if(ch == '"') {
            in_str = 1;
        } else if(ch == '\'') {
            in_chr = 1;
        } else if(ch == '(' || ch == '[') {
            depth++;
        } else if(ch == ')' || ch == ']') {
            depth--;
        } else if(ch == '{' && depth == 0) {
            /* first top-level '{' preceded by a space opens the block */
            if(i > 0 && t[i - 1] == ' ' && i + 2 < n && t[i + 1] == ' ' &&
                t[i + 2] != '}') {
                brace_pos = i;
                break;
            }
            return 0;   /* '{' used as expression on a control line */
        }
    }
    if(brace_pos == 0)
        return 0;
    /* no other top-level brace may appear before the trailing closer */
    depth = 0;
    in_str = in_chr = 0;
    for(i = brace_pos + 1; i + 1 < n; i++) {
        char ch = t[i];

        if(in_str) {
            if(ch == '\\' && i + 1 < n)
                i++;
            else if(ch == '"')
                in_str = 0;
        } else if(in_chr) {
            if(ch == '\\' && i + 1 < n)
                i++;
            else if(ch == '\'')
                in_chr = 0;
        } else if(ch == '"') {
            in_str = 1;
        } else if(ch == '\'') {
            in_chr = 1;
        } else if(ch == '(' || ch == '[') {
            depth++;
        } else if(ch == ')' || ch == ']') {
            depth--;
        } else if((ch == '{' || ch == '}') && depth == 0) {
            return 0;
        }
    }
    snprintf(head, hsz, "%.*s", (int)(brace_pos + 1), t);
    snprintf(body, bsz, "%.*s", (int)(n - brace_pos - 3), t + brace_pos + 2);
    return 1;
}

static void
parse_state_field(ZirModule *module, const char *path, int line_no, char *line)
{
    char *colon;
    char *eq;
    char *name;
    char *type;
    char *init;

    colon = strchr(line, ':');
    if(colon == NULL)
        return;
    *colon = '\0';
    name = trim(line);
    type = trim(colon + 1);
    init = "";
    eq = strchr(type, '=');
    if(eq != NULL) {
        *eq = '\0';
        init = trim(eq + 1);
    }
    ModuleAddStateField(module, name, trim(type), init,
                           Span(path, line_no, 1));
}

static void
parse_function_header(char *name, size_t name_size, char *args,
                      size_t args_size, char *ret, size_t ret_size,
                      const char *line)
{
    const char *p;
    const char *q;
    size_t n = 0;

    name[0] = '\0';
    args[0] = '\0';
    snprintf(ret, ret_size, "void");
    p = strstr(line, "::");
    if(p != NULL) {
        q = line;
        while(q < p && (*q == ' ' || *q == '\t'))
            q++;
        while(q < p && (isalnum((unsigned char)*q) || *q == '_') &&
              n + 1 < name_size)
            name[n++] = *q++;
        name[n] = '\0';
    }
    p = strchr(line, '(');
    q = NULL;
    if(p != NULL) {
        int depth = 0;
        for(const char *cursor = p; *cursor; cursor++) {
            if(*cursor == '(') depth++;
            else if(*cursor == ')' && --depth == 0) {
                q = cursor;
                break;
            }
        }
    }
    if(p != NULL && q != NULL && q > p) {
        n = (size_t)(q - p - 1);
        if(n >= args_size)
            n = args_size - 1;
        memcpy(args, p + 1, n);
        args[n] = '\0';
        /* Return type: after the closing ')', an optional '-> T' before any
         * trailing directive (#foreign / #slot / ...). */
        q++;
        while(*q == ' ' || *q == '\t')
            q++;
        if(q[0] == '-' && q[1] == '>') {
            q += 2;
            while(*q == ' ' || *q == '\t')
                q++;
            n = 0;
            while(*q != '\0' && *q != '#' && *q != '{' && n + 1 < ret_size)
                ret[n++] = *q++;
            while(n > 0 && (ret[n - 1] == ' ' || ret[n - 1] == '\t'))
                n--;
            ret[n] = '\0';
        }
    }
}

static int
parse_import_line(ZirModule *module, const char *path, int line_no,
                  const char *line, int scope_public)
{
    const char *directive;
    char target[K2ZIR_PATH_MAX];
    char name[ZIR_NAME_MAX];
    ZirImportKind kind;
    int quoted;

    directive = strstr(line, "#import");
    if(directive == NULL)
        return 0;
    target[0] = '\0';
    name[0] = '\0';
    quoted = parse_quoted(directive, target, sizeof(target));
    if(!quoted && !parse_angled(directive, target, sizeof(target)))
        return 0;
    /* Import targets are emitted verbatim inside #include "..." lines;
     * quotes and control bytes would let a crafted path escape the literal. */
    for(const char *p = target; *p != '\0'; p++)
        if(*p == '"' || *p == '>' || (unsigned char)*p < 0x20)
            die_at(Span(path, line_no, 1), "#import target contains a character that cannot "
                "appear in an include path");
    if(parse_symbol_before_colons(line, name, sizeof(name)))
        kind = ZIR_IMPORT_MODULE;
    else {
        copy_text(name, sizeof(name), target);
        kind = ZIR_IMPORT_HEADER;
    }
    /* Signature records the bracket style so backends can keep angled
     * includes angled ("<") instead of quoted. A private-scope include
     * belongs in the implementation, not the generated header. */
    ModuleAddImport(module, kind, name, target, quoted ? "" : "<",
                       scope_public,
                       Span(path, line_no, 1));
    return 1;
}

static int
parse_foreign_library_line(const char *path, int line_no, const char *line,
                           char names[][ZIR_NAME_MAX],
                           char targets[][ZIR_PATH_MAX], int *count)
{
    char name[ZIR_NAME_MAX];
    char target[ZIR_PATH_MAX];
    const char *colons = strstr(line, "::");
    const char *declaration;
    const char *quote;
    const char *end;

    if(colons == NULL ||
       !parse_symbol_before_colons(line, name, sizeof(name)))
        return 0;
    declaration = skip_ws(colons + 2);
    if(!starts_word(declaration, "#system_library"))
        return 0;
    quote = skip_ws(declaration + strlen("#system_library"));
    if(*quote != '"' || !parse_quoted(quote, target, sizeof(target)) ||
       target[0] == '\0')
        die_at(Span(path, line_no, 1),
               "#system_library requires a quoted library name");
    end = strchr(quote + 1, '"');
    if(end == NULL || strcmp(skip_ws(end + 1), ";") != 0)
        die_at(Span(path, line_no, 1),
               "#system_library declaration must end with ';'");
    for(const unsigned char *byte = (const unsigned char *)target;
        *byte != '\0'; byte++)
        if(*byte < 0x20 || *byte == '"' || *byte == '\\')
            die_at(Span(path, line_no, 1),
                   "#system_library name contains an invalid character");
    for(int i = 0; i < *count; i++)
        if(strcmp(names[i], name) == 0)
            die_at(Span(path, line_no, 1),
                   "duplicate #system_library name: %s", name);
    if(*count >= 32)
        die_at(Span(path, line_no, 1), "too many #system_library declarations");
    copy_text(names[*count], ZIR_NAME_MAX, name);
    copy_text(targets[*count], ZIR_PATH_MAX, target);
    (*count)++;
    return 1;
}

static int
parse_foreign_line(ZirModule *module, const char *path, int line_no,
                   const char *line, int scope_public,
                   char names[][ZIR_NAME_MAX],
                   char targets[][ZIR_PATH_MAX], int count)
{
    char name[ZIR_NAME_MAX];
    char target[ZIR_PATH_MAX];
    char symbol[ZIR_NAME_MAX];
    char library[ZIR_NAME_MAX];
    char foreign_name[ZIR_NAME_MAX];
    ZirImport *imp;
    ZirExternKind extern_kind;
    if(!parse_symbol_before_colons(line, name, sizeof(name)))
        return 0;
    const char *declaration = skip_ws(strstr(line, "::") + 2);
    const char *dir = strstr(line, "#foreign");
    if(dir == NULL)
        return 0;
    if(*declaration != '(')
        return 0;
    if(strchr(line, '{') != NULL)
        die_at(Span(path, line_no, 1),
               "#foreign procedure declarations cannot have a body");
    const char *cursor = skip_ws(dir + strlen("#foreign"));
    size_t used = 0;
    while((isalnum((unsigned char)*cursor) || *cursor == '_') &&
          used + 1 < sizeof(library))
        library[used++] = *cursor++;
    library[used] = '\0';
    if(!is_identifier_text(library))
        die_at(Span(path, line_no, 1),
               "#foreign requires a named #system_library");
    cursor = skip_ws(cursor);
    copy_text(foreign_name, sizeof(foreign_name), name);
    if(*cursor == '"') {
        size_t length = 0;
        cursor++;
        while(*cursor != '\0' && *cursor != '"' &&
              length + 1 < sizeof(foreign_name))
            foreign_name[length++] = *cursor++;
        foreign_name[length] = '\0';
        if(*cursor != '"' || !is_c_ident(foreign_name))
            die_at(Span(path, line_no, 1),
                   "#foreign alternate symbol must be an identifier");
        cursor = skip_ws(cursor + 1);
    }
    if(strcmp(cursor, ";") != 0)
        die_at(Span(path, line_no, 1),
               "#foreign declaration must end with ';'");
    const char *library_target = NULL;
    for(int i = 0; i < count; i++)
        if(strcmp(names[i], library) == 0) {
            library_target = targets[i];
            break;
        }
    if(library_target == NULL)
        die_at(Span(path, line_no, 1),
               "#foreign library is not declared: %s", library);
    target[0] = '\0';
    symbol[0] = '\0';
    if(strcmp(library_target, "host_api") == 0) {
        if(strcmp(foreign_name, name) != 0)
            die_at(Span(path, line_no, 1),
                   "host capability cannot rename a #foreign symbol");
    } else if(strchr(library_target, '/') != NULL) {
        if(snprintf(target, sizeof(target), "%s.%s", library_target,
                    foreign_name) >= (int)sizeof(target))
            die_at(Span(path, line_no, 1), "#foreign target is too long");
    } else {
        if(snprintf(target, sizeof(target), "c.%s", foreign_name) >=
           (int)sizeof(target))
            die_at(Span(path, line_no, 1), "#foreign target is too long");
    }
    extern_kind = classify_extern_target(target, symbol, sizeof(symbol),
                                         path, line_no);
    imp = ModuleAddImport(module, ZIR_IMPORT_EXTERN,
                             name, target[0] ? target : name, line, 1,
                             Span(path, line_no, 1));
    if(imp != NULL) {
        char parsed_name[ZIR_NAME_MAX];
        imp->is_public = scope_public;
        parse_function_header(parsed_name, sizeof(parsed_name), imp->args,
                              sizeof(imp->args), imp->return_type,
                              sizeof(imp->return_type), line);
        imp->extern_kind = extern_kind;
        snprintf(imp->extern_symbol, sizeof(imp->extern_symbol), "%s",
                 symbol);
    }
    return 1;
}

static int
paren_params_are_typed(const char *body)
{
    const char *inner = NULL;
    const char *end = NULL;
    const char *p;
    size_t depth = 0;

    /* Find the top-level parameter list and its matching close. */
    for(p = body; *p != '\0'; p++) {
        if(*p == '(') {
            if(depth == 0)
                inner = p + 1;
            depth++;
        } else if(*p == ')') {
            depth--;
            if(depth == 0) {
                end = p;
                break;
            }
        }
    }
    if(inner == NULL || end == NULL)
        return 0;
    while(inner < end && (*inner == ' ' || *inner == '\t'))
        inner++;
    if(inner >= end)
        return 1; /* () — empty parameter list */
    /* Jai-style rule: a binding like 'X :: (expr)' stays a constant unless
     * the parenthesized text is a typed parameter list. Parameter lists
     * name their arguments ('name: Type'); bare expressions — numbers,
     * arithmetic, ternaries, literals — do not. */
    if(*inner == '"' || *inner == '\'' || isdigit((unsigned char)*inner))
        return 0;
    if(strchr(inner, '?') != NULL)
        return 0;
    return strchr(inner, ':') != NULL;
}

static int
looks_like_function_header(const char *line)
{
    char tmp[K2ZIR_LINE_MAX];
    char *p;
    char *body;

    p = strstr(line, "::");
    if(p == NULL)
        return 0;
    snprintf(tmp, sizeof(tmp), "%s", p + 2);
    body = trim(tmp);
    if(starts_word(body, "#import") || starts_word(body, "#defined") ||
       starts_word(body, "#define") || starts_word(body, "struct") ||
       starts_word(body, "enum"))
        return 0;
    if(strstr(body, "#type") != NULL)
        return 0;
    if(body[0] != '(')
        return 0;
    /* A body or return type makes it a procedure regardless of params. */
    if(strchr(body, '{') != NULL || strstr(body, "->") != NULL)
        return 1;
    return paren_params_are_typed(body);
}


/* Inline bodies retain ordinary function parameter syntax, with an explicit
 * #slot marker separating them from record and array initializers. */
static int
parse_slot_header(const char *text, char *binding, size_t binding_size,
                   char *arguments, size_t arguments_size)
{
    const char *equals = strchr(text, '=');
    const char *annotation = strstr(text, "#slot");
    if(equals == NULL || annotation == NULL || equals >= annotation ||
       strcmp(skip_ws(annotation + 5), "{") != 0 ||
       *skip_ws(equals + 1) != '(')
        return 0;
    size_t length = (size_t)(equals - text);
    if(length >= binding_size)
        return 0;
    memcpy(binding, text, length);
    binding[length] = '\0';
    trim_in_place(binding);
    char header[ZIR_TEXT_MAX], name[ZIR_NAME_MAX], result[ZIR_NAME_MAX];
    int written = snprintf(header, sizeof(header), "slot_body :: %s", equals + 1);
    if(written < 0 || (size_t)written >= sizeof(header))
        return 0;
    parse_function_header(name, sizeof(name), arguments, arguments_size,
                          result, sizeof(result), header);
    return name[0] && !strcmp(result, "void");
}

/* ---- compile-time conditionals ------------------------------------------
 * '#if COND { ... } #else { ... }' regions use one model: top-level
 * captures inside a region are stamped with the expanded C preprocessor
 * condition and the emitter wraps each item in '#if cond / #endif'; the
 * condition's 'Name' constants ('WEB :: #defined(PLATFORM_WEB)') expand to
 * their expressions. Body-level regions lower to raw #if/#elif/#else/#endif
 * statements whose braces are consumed here. */

typedef struct {
    char name[ZIR_NAME_MAX];
    char expr[ZIR_TEXT_MAX];
} ZirConst;

typedef struct {
    ZirConst *items;
    int count;
    int capacity;
} ZirConsts;

typedef struct {
    char cond[ZIR_TEXT_MAX];      /* active branch condition (C form) */
    char excluded[ZIR_TEXT_MAX];  /* conditions handled by earlier branches */
    int braces;                   /* net '{' until the region's closing '}' */
} ZirCondFrame;

static int
line_is_hash_else(const char *line)
{
    return strcmp(line, "} #else {") == 0 || strcmp(line, "#else {") == 0;
}

/* '#if COND {' / '#else_if COND {' (with optional leading '}'): strips the
 * trailing '{' — region braces are consumed, never emitted. Returns 1 for
 * '#if', 2 for '#else_if', 0 otherwise; *condition points into line. */
static int
parse_cond_start(char *line, char **condition)
{
    char *q = NULL;
    int kind = 0;
    size_t n;

    if(strncmp(line, "#if", 3) == 0 &&
       (line[3] == '\0' || isspace((unsigned char)line[3]))) {
        q = line + 3;
        kind = 1;
    } else if(strncmp(line, "#else_if", 8) == 0 &&
              (line[8] == '\0' || isspace((unsigned char)line[8]))) {
        q = line + 8;
        kind = 2;
    } else if(strncmp(line, "} #else_if", 10) == 0 &&
              (line[10] == '\0' || isspace((unsigned char)line[10]))) {
        q = line + 10;
        kind = 2;
    } else {
        return 0;
    }
    q = trim(q);
    n = strlen(q);
    if(n == 0 || q[n - 1] != '{')
        return 0;
    q[n - 1] = '\0';
    q = trim(q);
    if(q[0] == '\0')
        return 0;
    *condition = q;
    return kind;
}

static void
expand_compile_expr_depth(char *dst, size_t dst_size, const ZirConsts *consts,
                          const char *src, int depth)
{
    size_t n = 0;
    int in_string = 0;
    int escaped = 0;
    int i;

    if(dst_size == 0)
        return;
    if(depth > 16) {
        dst[0] = '\0';
        return;
    }
    for(const char *p = src; p != NULL && *p != '\0' && n + 1 < dst_size;) {
        if(in_string) {
            dst[n++] = *p;
            if(escaped)
                escaped = 0;
            else if(*p == '\\')
                escaped = 1;
            else if(*p == '"')
                in_string = 0;
            p++;
            continue;
        }
        if(*p == '"') {
            in_string = 1;
            dst[n++] = *p++;
            continue;
        }
        if(*p == '#' && strncmp(p, "#defined", 8) == 0) {
            const char *word_end = p + 8;

            if(*word_end == '\0' || *word_end == '(' ||
               isspace((unsigned char)*word_end)) {
                if(n + 7 >= dst_size)
                    break;
                memcpy(dst + n, "defined", 7);
                n += 7;
                p += 8;
                continue;
            }
        }
        if(isalpha((unsigned char)*p) || *p == '_') {
            char ident[ZIR_NAME_MAX];
            size_t il = 0;
            int found = 0;

            while(isalnum((unsigned char)*p) || *p == '_') {
                if(il + 1 < sizeof(ident))
                    ident[il++] = *p;
                p++;
            }
            ident[il] = '\0';
            for(i = 0; i < consts->count; i++) {
                if(strcmp(consts->items[i].name, ident) == 0) {
                    char expanded[ZIR_TEXT_MAX];
                    int written;

                    expand_compile_expr_depth(expanded, sizeof(expanded),
                                              consts, consts->items[i].expr,
                                              depth + 1);
                    written = snprintf(dst + n, dst_size - n, "(%s)", expanded);
                    if(written < 0)
                        written = 0;
                    if((size_t)written >= dst_size - n)
                        n = dst_size - 1;
                    else
                        n += (size_t)written;
                    found = 1;
                    break;
                }
            }
            if(!found) {
                if(n + il >= dst_size)
                    break;
                memcpy(dst + n, ident, il);
                n += il;
            }
            continue;
        }
        dst[n++] = *p++;
    }
    dst[n] = '\0';
}

static void
expand_compile_expr(char *dst, size_t dst_size, const ZirConsts *consts,
                    const char *src)
{
    expand_compile_expr_depth(dst, dst_size, consts, src, 0);
}

static char *
find_top_comma(char *s)
{
    int depth = 0;
    int in_string = 0;
    int in_char = 0;

    for(char *p = s; *p != '\0'; p++) {
        if(in_string) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '"')
                in_string = 0;
        } else if(in_char) {
            if(*p == '\\' && p[1] != '\0')
                p++;
            else if(*p == '\'')
                in_char = 0;
        } else if(*p == '"') {
            in_string = 1;
        } else if(*p == '\'') {
            in_char = 1;
        } else if(*p == '(' || *p == '[' || *p == '{') {
            depth++;
        } else if(*p == ')' || *p == ']' || *p == '}') {
            if(depth > 0)
                depth--;
        } else if(*p == ',' && depth == 0) {
            return p;
        }
    }
    return NULL;
}

typedef struct ZirEval {
    const char *p;
    int known;
    long value;
} ZirEval;

static void
eval_skip(ZirEval *ev)
{
    while(*ev->p == ' ' || *ev->p == '\t')
        ev->p++;
}

static long eval_or(ZirEval *ev);

static long
eval_primary(ZirEval *ev)
{
    char *end;
    long value;

    eval_skip(ev);
    if(*ev->p == '(') {
        ev->p++;
        value = eval_or(ev);
        eval_skip(ev);
        if(*ev->p == ')')
            ev->p++;
        else
            ev->known = 0;
        return value;
    }
    if(isdigit((unsigned char)*ev->p)) {
        value = strtol(ev->p, &end, 0);
        if(end == ev->p) {
            ev->known = 0;
            return 0;
        }
        ev->p = end;
        while(isalnum((unsigned char)*ev->p) || *ev->p == '_')
            ev->p++;   /* integer suffixes: U, L, UL */
        return value;
    }
    if(isalpha((unsigned char)*ev->p) || *ev->p == '_') {
        while(isalnum((unsigned char)*ev->p) || *ev->p == '_')
            ev->p++;
        ev->known = 0;
        return 0;
    }
    ev->known = 0;
    return 0;
}

static long
eval_unary(ZirEval *ev)
{
    eval_skip(ev);
    if(*ev->p == '!') {
        long v;

        ev->p++;
        v = eval_unary(ev);
        return ev->known ? !v : 0;
    }
    if(*ev->p == '-') {
        ev->p++;
        return -eval_unary(ev);
    }
    if(*ev->p == '+') {
        ev->p++;
        return eval_unary(ev);
    }
    return eval_primary(ev);
}

static long
eval_mul(ZirEval *ev)
{
    long left = eval_unary(ev);

    while(1) {
        char op;
        long right;
        int left_known;

        eval_skip(ev);
        if(*ev->p != '*' && *ev->p != '/' && *ev->p != '%')
            return left;
        op = *ev->p++;
        left_known = ev->known;
        right = eval_unary(ev);
        if(!left_known || !ev->known || (right == 0 && op != '*')) {
            ev->known = 0;
            left = 0;
        } else if(op == '*') {
            left *= right;
        } else if(op == '/') {
            left /= right;
        } else {
            left %= right;
        }
    }
}

static long
eval_add(ZirEval *ev)
{
    long left = eval_mul(ev);

    while(1) {
        char op;
        long right;
        int left_known;

        eval_skip(ev);
        if(*ev->p != '+' && *ev->p != '-')
            return left;
        op = *ev->p++;
        left_known = ev->known;
        right = eval_mul(ev);
        if(!left_known || !ev->known) {
            ev->known = 0;
            left = 0;
        } else if(op == '+') {
            left += right;
        } else {
            left -= right;
        }
    }
}

static long
eval_rel(ZirEval *ev)
{
    long left = eval_add(ev);

    while(1) {
        const char *op = NULL;
        long right;
        int left_known;

        eval_skip(ev);
        if(strncmp(ev->p, "<=", 2) == 0 || strncmp(ev->p, ">=", 2) == 0)
            op = ev->p, ev->p += 2;
        else if(*ev->p == '<' || *ev->p == '>')
            op = ev->p, ev->p++;
        else
            return left;
        left_known = ev->known;
        right = eval_add(ev);
        if(!left_known || !ev->known) {
            ev->known = 0;
            left = 0;
        } else if(op[0] == '<' && op[1] == '=') {
            left = left <= right;
        } else if(op[0] == '>' && op[1] == '=') {
            left = left >= right;
        } else if(op[0] == '<') {
            left = left < right;
        } else {
            left = left > right;
        }
    }
}

static long
eval_eq(ZirEval *ev)
{
    long left = eval_rel(ev);

    while(1) {
        int neq = 0;
        long right;
        int left_known;

        eval_skip(ev);
        if(strncmp(ev->p, "==", 2) == 0) {
            ev->p += 2;
        } else if(strncmp(ev->p, "!=", 2) == 0) {
            ev->p += 2;
            neq = 1;
        } else {
            return left;
        }
        left_known = ev->known;
        right = eval_rel(ev);
        if(!left_known || !ev->known) {
            ev->known = 0;
            left = 0;
        } else {
            left = neq ? left != right : left == right;
        }
    }
}

static long
eval_and(ZirEval *ev)
{
    long left = eval_eq(ev);

    while(1) {
        long right;
        int left_known;
        long left_value;

        eval_skip(ev);
        if(strncmp(ev->p, "&&", 2) != 0)
            return left;
        ev->p += 2;
        left_known = ev->known;
        left_value = left;
        right = eval_eq(ev);
        if(left_known && !left_value) {
            ev->known = 1;
            left = 0;
        } else if(left_known && ev->known) {
            left = left_value && right;
        } else {
            ev->known = 0;
            left = 0;
        }
    }
}

static long
eval_or(ZirEval *ev)
{
    long left = eval_and(ev);

    while(1) {
        long right;
        int left_known;
        long left_value;

        eval_skip(ev);
        if(strncmp(ev->p, "||", 2) != 0)
            return left;
        ev->p += 2;
        left_known = ev->known;
        left_value = left;
        right = eval_and(ev);
        if(left_known && left_value) {
            ev->known = 1;
            left = 1;
        } else if(left_known && ev->known) {
            left = left_value || right;
        } else {
            ev->known = 0;
            left = 0;
        }
    }
}

static int
eval_const_condition(const char *src, long *value)
{
    ZirEval ev;

    ev.p = src;
    ev.known = 1;
    ev.value = eval_or(&ev);
    eval_skip(&ev);
    if(*ev.p != '\0')
        ev.known = 0;
    if(value != NULL)
        *value = ev.value;
    return ev.known;
}

static int
parse_compile_check(ZirModule *module, const char *path, int line_no,
                    char *line, const ZirConsts *consts, const char *guard)
{
    char cond[ZIR_TEXT_MAX];
    char msg[ZIR_TEXT_MAX];
    ZirAssert *a;

    if(strncmp(line, "#assert", 7) == 0 &&
       (line[7] == '\0' || isspace((unsigned char)line[7]))) {
        char *body = trim(line + 7);
        char *comma;

        if(body[0] == '\0')
            die_at(Span(path, line_no, 1), "#assert needs a condition");
        comma = find_top_comma(body);
        if(comma != NULL) {
            *comma = '\0';
            snprintf(msg, sizeof(msg), "%s", trim(comma + 1));
            if(msg[0] == '\0')
                snprintf(msg, sizeof(msg), "\"Ziran #assert failed\"");
        } else {
            snprintf(msg, sizeof(msg), "\"Ziran #assert failed\"");
        }
        expand_compile_expr(cond, sizeof(cond), consts, trim(body));
        {
            long value = 0;
            int known = eval_const_condition(cond, &value);

            if(guard[0] == '\0' && known && !value)
                die_at(Span(path, line_no, 1), "#assert failed: %s", msg);
            a = ModuleAddAssert(module, cond, msg, Span(path, line_no, 1));
            if(a != NULL) {
                a->known = known;
                a->value = value != 0;
                snprintf(a->guard, sizeof(a->guard), "%s", guard);
            }
        }
        return 1;
    }
    if(strncmp(line, "#error", 6) == 0 &&
       (line[6] == '\0' || isspace((unsigned char)line[6]))) {
        char *body = trim(line + 6);

        if(body[0] == '\0')
            die_at(Span(path, line_no, 1), "#error needs a message");
        a = ModuleAddAssert(module, "0", body, Span(path, line_no, 1));
        if(a != NULL) {
            a->known = 1;
            a->value = 0;
            snprintf(a->guard, sizeof(a->guard), "%s", guard);
        }
        return 1;
    }
    return 0;
}

static void
text_append(char *dst, size_t dst_size, const char *part)
{
    size_t used;
    size_t remaining;

    if(dst == NULL || dst_size == 0 || part == NULL)
        return;
    used = strlen(dst);
    if(used >= dst_size - 1)
        return;
    remaining = dst_size - used - 1;
    if(strlen(part) < remaining)
        remaining = strlen(part);
    memcpy(dst + used, part, remaining);
    dst[used + remaining] = '\0';
}

static void
format_else_if_guard(char *dst, size_t dst_size, const char *excluded,
                     const char *expanded)
{
    copy_text(dst, dst_size, "!(");
    text_append(dst, dst_size, excluded);
    text_append(dst, dst_size, ") && (");
    text_append(dst, dst_size, expanded);
    text_append(dst, dst_size, ")");
}

static void
format_else_guard(char *dst, size_t dst_size, const char *excluded)
{
    copy_text(dst, dst_size, "!(");
    text_append(dst, dst_size, excluded);
    text_append(dst, dst_size, ")");
}

static void
format_excluded_guard(char *dst, size_t dst_size, const char *previous,
                      const char *expanded)
{
    copy_text(dst, dst_size, "(");
    text_append(dst, dst_size, previous);
    text_append(dst, dst_size, ") || (");
    text_append(dst, dst_size, expanded);
    text_append(dst, dst_size, ")");
}

static void
format_preprocessor_cond(char *dst, size_t dst_size, const char *directive,
                         const char *expanded)
{
    copy_text(dst, dst_size, directive);
    text_append(dst, dst_size, " ");
    text_append(dst, dst_size, expanded);
}

static void
combine_active_guard(char *dst, size_t dst_size, const ZirCondFrame *frames,
                     int count)
{
    int i;

    dst[0] = '\0';
    for(i = 0; i < count; i++) {
        if(dst[0] == '\0')
            snprintf(dst, dst_size, "%s", frames[i].cond);
        else
            snprintf(dst + strlen(dst), dst_size - strlen(dst), " && %s",
                     frames[i].cond);
    }
}

/* Returns 1 when the line is consumed by top-level conditional handling
 * ('#if'/'#else'/'#else_if' open or retarget a frame; the matching '}'
 * pops one). A plain line inside a region only settles the frame's brace
 * count and returns 0, so normal captures proceed — stamped with the
 * active guard by the caller. */
static int
cond_top_step(char *line, ZirCondFrame *frames, int *count, char *guard,
              size_t guard_size, const ZirConsts *consts, const char *path,
              int line_no)
{
    char *cnd = NULL;
    int ck = parse_cond_start(line, &cnd);
    ZirCondFrame *fr;

    if(ck == 1) {
        char expanded[ZIR_TEXT_MAX];

        if(*count >= 8)
            die_at(Span(path, line_no, 1), "too many nested #if blocks");
        expand_compile_expr(expanded, sizeof(expanded), consts, cnd);
        fr = &frames[(*count)++];
        copy_text(fr->cond, sizeof(fr->cond), expanded);
        copy_text(fr->excluded, sizeof(fr->excluded), expanded);
        fr->braces = 1;
        combine_active_guard(guard, guard_size, frames, *count);
        return 1;
    }
    if(*count <= 0)
        return 0;
    fr = &frames[*count - 1];
    if(ck == 2) {
        char expanded[ZIR_TEXT_MAX];
        char next[ZIR_TEXT_MAX * 2];

        expand_compile_expr(expanded, sizeof(expanded), consts, cnd);
        format_else_if_guard(fr->cond, sizeof(fr->cond), fr->excluded,
                             expanded);
        format_excluded_guard(next, sizeof(next), fr->excluded, expanded);
        copy_text(fr->excluded, sizeof(fr->excluded), next);
        fr->braces = 1;
        combine_active_guard(guard, guard_size, frames, *count);
        return 1;
    }
    if(line_is_hash_else(line)) {
        format_else_guard(fr->cond, sizeof(fr->cond), fr->excluded);
        fr->braces = 1;
        combine_active_guard(guard, guard_size, frames, *count);
        return 1;
    }
    fr->braces += net_block_braces(line);
    if(fr->braces <= 0) {
        (*count)--;
        combine_active_guard(guard, guard_size, frames, *count);
        return 1;
    }
    return 0;
}

/* A sub-mode (state/type/enum/function) consumed exactly one net '{'
 * from the enclosing region — settle the frame count. */
static void
cond_frame_settle(ZirCondFrame *frames, int count)
{
    if(count > 0)
        frames[count - 1].braces--;
}

/* Strip C-style block comments in place, preserving newlines so line
 * numbers stay honest. *in_comment carries the state across lines (a
 * comment opened on one line keeps stripping on the next). String and
 * char literals are respected, and reset at each newline since Ziran
 * literals never span lines. Without this, a comment close at end of
 * line trips the trailing-slash continuation rule and glues the comment
 * onto the next function header, silently dropping the function. */
static void
strip_block_comments(char *s, int *in_comment)
{
    char *w = s;
    char *r = s;
    int in_str = 0;
    int in_chr = 0;

    while(*r != '\0') {
        if(*in_comment) {
            while(*r != '\0' && !(*r == '*' && r[1] == '/')) {
                if(*r == '\n')
                    *w++ = '\n';
                r++;
            }
            if(*r != '\0') {
                r += 2;
                *in_comment = 0;
                *w++ = ' ';   /* keep tokens on either side apart */
            }
            continue;
        }
        if(in_str || in_chr) {
            if(*r == '\\' && r[1] != '\0') {
                *w++ = *r++;
                *w++ = *r++;
                continue;
            }
            if((in_str && *r == '"') || (in_chr && *r == '\''))
                in_str = in_chr = 0;
            else if(*r == '\n')
                in_str = in_chr = 0;
            *w++ = *r++;
        } else if(*r == '"') {
            in_str = 1;
            *w++ = *r++;
        } else if(*r == '\'') {
            in_chr = 1;
            *w++ = *r++;
        } else if(*r == '/' && r[1] == '*') {
            *in_comment = 1;
            r += 2;
        } else if(*r == '\n') {
            *w++ = *r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/* Keep the existing checked IR type names while requiring Jai spellings in
 * source. Scan tokens after block comments are stripped so text in comments
 * and quoted literals does not become a type or a migration error. */
static void
normalize_jai_type_names(char *line, const char *path, int line_no)
{
    static const struct { const char *jai, *internal; } names[] = {
        {"s8", "i8"}, {"s16", "i16"}, {"s32", "i32"}, {"s64", "i64"},
        {"float32", "float"}, {"float64", "double"}
    };
    static const char *const old[] = {
        "i8", "i16", "i32", "i64", "f32", "f64", "double", NULL
    };
    char normalized[K2ZIR_LINE_MAX];
    size_t used = 0;
    for(const char *p = line; *p; ) {
        if(*p == '/' && p[1] == '/') {
            size_t remaining = strlen(p);
            if(used + remaining >= sizeof(normalized))
                die_at(Span(path, line_no, 1), "source line exceeds size limit");
            memcpy(normalized + used, p, remaining + 1);
            used += remaining;
            break;
        }
        if(*p == '"' || *p == '\'') {
            char quote = *p;
            if(used + 1 >= sizeof(normalized))
                die_at(Span(path, line_no, 1), "source line exceeds size limit");
            normalized[used++] = *p++;
            while(*p) {
                char next = *p++;
                if(used + 1 >= sizeof(normalized))
                    die_at(Span(path, line_no, 1), "source line exceeds size limit");
                normalized[used++] = next;
                if(next == '\\' && *p) {
                    if(used + 1 >= sizeof(normalized))
                        die_at(Span(path, line_no, 1), "source line exceeds size limit");
                    normalized[used++] = *p++;
                } else if(next == quote) {
                    break;
                }
            }
            continue;
        }
        if(isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p;
            while(isalnum((unsigned char)*p) || *p == '_') p++;
            size_t length = (size_t)(p - start);
            const char *replacement = NULL;
            if(length == 3 && strncmp(start, "nil", 3) == 0)
                die_at(Span(path, line_no, (int)(start - line) + 1),
                       "nil is not Jai syntax; use null");
            for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
                if(strlen(names[i].jai) == length &&
                   strncmp(start, names[i].jai, length) == 0) {
                    replacement = names[i].internal;
                    break;
                }
            for(int i = 0; old[i]; i++)
                if(strlen(old[i]) == length &&
                   strncmp(start, old[i], length) == 0)
                    die_at(Span(path, line_no, (int)(start - line) + 1),
                           "non-Jai primitive type spelling: %s", old[i]);
            if(replacement != NULL) {
                start = replacement;
                length = strlen(replacement);
            }
            if(used + length >= sizeof(normalized))
                die_at(Span(path, line_no, 1), "source line exceeds size limit");
            memcpy(normalized + used, start, length);
            used += length;
            continue;
        }
        if(used + 1 >= sizeof(normalized))
            die_at(Span(path, line_no, 1), "source line exceeds size limit");
        normalized[used++] = *p++;
    }
    normalized[used] = '\0';
    copy_text(line, K2ZIR_LINE_MAX, normalized);
}

/* File and embedded declarations share the complete frontend. A line that
 * does not fit whole is refused: silently splitting it would change meaning. */
static char *
read_source_line(char *line, size_t size, FILE *file, const char **source,
                 const char *path, int line_no)
{
    if(file != NULL) {
        if(fgets(line, (int)size, file) == NULL)
            return NULL;
        if(!feof(file) && strchr(line, '\n') == NULL)
            die_at(Span(path, line_no + 1, 1), "source line exceeds %d characters", (int)size - 1);
        return line;
    }
    if(**source == '\0')
        return NULL;
    size_t length = 0;
    while(length + 1 < size && **source != '\0') {
        char next = *(*source)++;
        line[length++] = next;
        if(next == '\n')
            break;
    }
    if(**source != '\0' && length > 0 && line[length - 1] != '\n')
        die_at(Span(path, line_no + 1, 1), "source line exceeds %d characters", (int)size - 1);
    line[length] = '\0';
    return line;
}

static ZirFunction *
variant_function(ZirModule *module, const ZirType *type,
                 const char *name, const char *args, const char *result)
{
    ZirFunction *fn = ModuleAddFunction(module, name, args, result, 0, type->span);
    if(fn == NULL)
        die_at(type->span, "cannot allocate variant operation");
    fn->is_generated = 1;
    fn->is_public = 1;
    copy_text(fn->guard, sizeof(fn->guard), type->guard);
    return fn;
}

static void
variant_statement(ZirFunction *fn, ZirStmtKind kind,
                  const char *text, ZirSourceSpan span)
{
    if(FunctionAddStmt(fn, kind, text, "", span) == NULL)
        die_at(span, "cannot allocate variant operation body");
}

static void
finalize_variant(ZirModule *module, ZirType *type)
{
    ZirVariantCase item, earlier;
    size_t offset = 0;
    int ordinal = 0, status;
    char name[ZIR_NAME_MAX], args[ZIR_TEXT_MAX], line[ZIR_TEXT_MAX];
    int length = snprintf(type->body, sizeof(type->body), "variant_tag: i32\n");
    if(length < 0 || (size_t)length >= sizeof(type->body))
        die_at(type->span, "variant storage exceeds size limit");
    size_t used = (size_t)length;
    while((status = VariantNextCase(type, &offset, &item)) == 1) {
        size_t previous = 0;
        while(previous < offset) {
            size_t before = previous;
            if(VariantNextCase(type, &previous, &earlier) != 1 ||
               previous <= before)
                die_at(type->span, "malformed variant case");
            if(previous == offset) break;
            if(!strcmp(earlier.name, item.name))
                die_at(type->span, "duplicate variant case: %s", item.name);
        }
        if(ordinal == 0) {
            length = snprintf(name, sizeof(name), "%s_Tag", type->name);
            if(length < 0 || (size_t)length >= sizeof(name))
                die_at(type->span, "variant operation name exceeds size limit");
            length = snprintf(args, sizeof(args), "value: %s", type->name);
            if(length < 0 || (size_t)length >= sizeof(args))
                die_at(type->span, "variant operation signature exceeds size limit");
            ZirFunction *tag = variant_function(module, type, name, args, "i32");
            variant_statement(tag, ZIR_STMT_RETURN,
                              "return value.variant_tag", type->span);
        }
        if(item.type[0]) {
            length = snprintf(type->body + used, sizeof(type->body) - used,
                              "variant_payload_%s: %s\n", item.name, item.type);
            if(length < 0 || (size_t)length >= sizeof(type->body) - used)
                die_at(type->span, "variant storage exceeds size limit");
            used += (size_t)length;
        }
        length = snprintf(name, sizeof(name), "%s_%s", type->name, item.name);
        if(length < 0 || (size_t)length >= sizeof(name))
            die_at(type->span, "variant constructor name exceeds size limit");
        args[0] = 0;
        if(item.type[0]) {
            length = snprintf(args, sizeof(args), "value: %s", item.type);
            if(length < 0 || (size_t)length >= sizeof(args))
                die_at(type->span, "variant operation signature exceeds size limit");
        }
        ZirFunction *constructor = variant_function(module, type,
            name, args, type->name);
        length = snprintf(line, sizeof(line), "result: %s", type->name);
        if(length < 0 || (size_t)length >= sizeof(line))
            die_at(type->span, "variant statement exceeds size limit");
        variant_statement(constructor, ZIR_STMT_DECL, line, type->span);
        length = snprintf(line, sizeof(line), "result.variant_tag = %d", ordinal);
        if(length < 0 || (size_t)length >= sizeof(line))
            die_at(type->span, "variant statement exceeds size limit");
        variant_statement(constructor, ZIR_STMT_ASSIGN, line, type->span);
        if(item.type[0]) {
            length = snprintf(line, sizeof(line),
                              "result.variant_payload_%s = value", item.name);
            if(length < 0 || (size_t)length >= sizeof(line))
                die_at(type->span, "variant statement exceeds size limit");
            variant_statement(constructor, ZIR_STMT_ASSIGN, line, type->span);
        }
        variant_statement(constructor, ZIR_STMT_RETURN,
                          "return result", type->span);
        if(item.type[0]) {
            length = snprintf(name, sizeof(name),
                              "%s_%sValue", type->name, item.name);
            if(length < 0 || (size_t)length >= sizeof(name))
                die_at(type->span, "variant accessor name exceeds size limit");
            length = snprintf(args, sizeof(args), "value: %s", type->name);
            if(length < 0 || (size_t)length >= sizeof(args))
                die_at(type->span, "variant operation signature exceeds size limit");
            ZirFunction *accessor = variant_function(module, type,
                name, args, item.type);
            length = snprintf(line, sizeof(line),
                              "if value.variant_tag != %d {", ordinal);
            if(length < 0 || (size_t)length >= sizeof(line))
                die_at(type->span, "variant statement exceeds size limit");
            variant_statement(accessor, ZIR_STMT_IF, line, type->span);
            variant_statement(accessor, ZIR_STMT_UNREACHABLE,
                              "unreachable", type->span);
            variant_statement(accessor, ZIR_STMT_BLOCK_CLOSE, "}", type->span);
            length = snprintf(line, sizeof(line),
                              "return value.variant_payload_%s", item.name);
            if(length < 0 || (size_t)length >= sizeof(line))
                die_at(type->span, "variant statement exceeds size limit");
            variant_statement(accessor, ZIR_STMT_RETURN, line, type->span);
        }
        ordinal++;
    }
    if(status < 0 || ordinal == 0)
        die_at(type->span, "variant requires valid cases");
}

int
SubstituteGenericType(const char *source, char *output, size_t capacity,
                      char params[][ZIR_NAME_MAX],
                      char actual[][ZIR_NAME_MAX], int count)
{
    size_t used = 0;
    while(*source) {
        const char *replacement = NULL;
        size_t length = 1;
        if(isalpha((unsigned char)*source) || *source == '_') {
            while(isalnum((unsigned char)source[length]) ||
                  source[length] == '_')
                length++;
            for(int i = 0; i < count; i++)
                if(strlen(params[i]) == length &&
                   strncmp(source, params[i], length) == 0) {
                    replacement = actual[i];
                    break;
                }
        }
        if(replacement != NULL) {
            size_t replacement_length = strlen(replacement);
            if(used + replacement_length >= capacity)
                return 0;
            memcpy(output + used, replacement, replacement_length);
            used += replacement_length;
        } else {
            if(used + length >= capacity)
                return 0;
            memcpy(output + used, source, length);
            used += length;
        }
        source += length;
    }
    output[used] = '\0';
    return 1;
}

int
InstantiateGenericType(ZirModule *module, ZirType *instance,
                       const ZirType *generic)
{
    char params[16][ZIR_NAME_MAX], actual[16][ZIR_NAME_MAX];
    int parameter_count, argument_count;
    if(!instance->is_type_instance ||
       (!generic->is_variant_template && !generic->is_record_template))
        return 0;
    parameter_count = split_top_level(generic->template_params, params[0],
                                      16, sizeof(params[0]));
    argument_count = split_top_level(instance->template_args, actual[0],
                                     16, sizeof(actual[0]));
    if(parameter_count < 1 || parameter_count != argument_count)
        return 0;
    for(int i = 0; i < parameter_count; i++) {
        if(!is_identifier_text(params[i]) || !actual[i][0])
            return 0;
        for(int previous = 0; previous < i; previous++)
            if(!strcmp(params[previous], params[i]))
                return 0;
    }
    size_t offset = 0, used = 0;
    int members = 0, status;
    if(generic->is_variant_template) {
        ZirVariantCase item;
        while((status = VariantNextCase(generic, &offset, &item)) == 1) {
            char payload[ZIR_NAME_MAX];
            if(item.type[0] && !SubstituteGenericType(item.type, payload,
                    sizeof(payload), params, actual, parameter_count))
                return 0;
            int length = snprintf(instance->variant_cases + used,
                sizeof(instance->variant_cases) - used, "%s%s%s\n",
                item.name, item.type[0] ? ": " : "",
                item.type[0] ? payload : "");
            if(length < 0 || (size_t)length >=
               sizeof(instance->variant_cases) - used)
                return 0;
            used += (size_t)length;
            members++;
        }
    } else {
        ZirTypeField field;
        while((status = TypeNextField(generic, &offset, &field)) == 1) {
            char type[ZIR_NAME_MAX];
            if(!SubstituteGenericType(field.type, type, sizeof(type),
                                        params, actual, parameter_count))
                return 0;
            int length = snprintf(instance->body + used,
                sizeof(instance->body) - used, "%s: %s\n",
                field.name, type);
            if(length < 0 || (size_t)length >= sizeof(instance->body) - used)
                return 0;
            used += (size_t)length;
            members++;
        }
    }
    if(status < 0 || members == 0)
        return 0;
    instance->is_type_instance = 0;
    instance->template_name[0] = '\0';
    instance->template_args[0] = '\0';
    if(generic->is_variant_template) {
        instance->is_variant = 1;
        finalize_variant(module, instance);
    }
    return 1;
}

int
RegenerateVariantOperations(ZirModule *module)
{
    ZirModule generated = {0};
    unsigned char *matched = calloc((size_t)module->function_count + 1, 1);
    int valid = matched != NULL;

    for(int t = 0; valid && t < module->type_count; t++) {
        const ZirType *original = &module->types[t];
        if(!original->is_variant)
            continue;
        if(!VariantLayoutValid(original)) {
            valid = 0;
            break;
        }
        size_t offset = 0;
        ZirVariantCase item;
        int status;
        while((status = VariantNextCase(original, &offset, &item)) == 1) {
            /* The generator uses these exact names for the constructor and
             * payload accessor. Reject long saved names before it runs. */
            if(strlen(original->name) + strlen(item.name) +
               strlen("_Value") >= ZIR_NAME_MAX) {
                valid = 0;
                break;
            }
        }
        if(!valid || status < 0) {
            valid = 0;
            break;
        }
        ZirType copy = *original;
        finalize_variant(&generated, &copy);
    }
    for(int g = 0; valid && g < generated.function_count; g++) {
        ZirFunction *expected = &generated.functions[g];
        int found = -1;
        for(int f = 0; f < module->function_count; f++) {
            ZirFunction *saved = &module->functions[f];
            if(!saved->is_generated || strcmp(saved->name, expected->name))
                continue;
            if(found >= 0 || matched[f] ||
               strcmp(saved->args, expected->args) ||
               strcmp(saved->return_type, expected->return_type) ||
               strcmp(saved->guard, expected->guard) ||
               strcmp(saved->span.path, expected->span.path) ||
               saved->span.line != expected->span.line ||
               saved->span.column != expected->span.column ||
               saved->span.end_line != expected->span.end_line ||
               saved->span.end_column != expected->span.end_column ||
               !saved->is_public || saved->is_extern || saved->is_closure ||
               saved->exported || saved->extern_kind != ZIR_EXTERN_NONE ||
               saved->extern_target[0] || saved->extern_symbol[0] ||
               saved->checked != 1 || saved->uses_host != 0 ||
               saved->capture_count || saved->stmt_count || saved->expr_count) {
                valid = 0;
                break;
            }
            found = f;
        }
        /* Portable bundles may retain only reachable generated functions.
         * Every retained slot must still match its canonical declaration. */
        if(!valid)
            break;
        if(found < 0)
            continue;
        ZirFunction *saved = &module->functions[found];
        expected->checked = 1;
        free(saved->captures);
        free(saved->stmts);
        free(saved->exprs);
        *saved = *expected;
        memset(expected, 0, sizeof(*expected));
        matched[found] = 1;
    }
    for(int f = 0; valid && f < module->function_count; f++)
        if(module->functions[f].is_generated && !matched[f])
            valid = 0;
    for(int g = 0; g < generated.function_count; g++) {
        free(generated.functions[g].captures);
        free(generated.functions[g].stmts);
        free(generated.functions[g].exprs);
    }
    free(generated.functions);
    free(matched);
    return valid;
}

static ZirProgram *
parse_source(const char *path, const char *root, FILE *in, const char *source)
{
    ZirProgram *program;
    ZirModule *module;
    ZirFunction *fn = NULL;
    char line[K2ZIR_LINE_MAX];
    char module_name[ZIR_NAME_MAX];
    char rel[K2ZIR_PATH_MAX];
    int line_no = 0;
    enum { TOP, STATE, TYPE, ENUM, FUNCTION } mode = TOP;
    int enum_return = TOP;
    int depth = 0;
    char pending[K2ZIR_LINE_MAX * 4];
    pending[0] = '\0';
    char lookahead[K2ZIR_LINE_MAX];
    int have_look = 0;
    /* One-line control blocks ('if cond { body }') are split into header /
     * body / '}' logical lines; the body and closer re-enter the main loop
     * through this FIFO so they flow through the normal join machinery. */
    char onelineq[16][K2ZIR_LINE_MAX * 2];
    int onelineq_count = 0;
    int from_queue = 0;
    int pending_len = 0;
    int pending_start_line = 1;
    int pending_start_column = 1;
    int pending_end_column = 1;
    int paren_depth = 0;
    int bracket_depth = 0;
    int in_string = 0;
    int expr_brace = 0;
    ZirCondFrame tframes[8];
    int tframe_count = 0;
    ZirConsts consts;
    char cur_guard[ZIR_TEXT_MAX];
    int body_mdepth[8];
    int body_mcount = 0;
    int in_block_comment = 0;
    int program_export = 0;
    int program_export_line = 0;
    int scope_public = 1;
    char foreign_library_names[32][ZIR_NAME_MAX];
    char foreign_library_targets[32][ZIR_PATH_MAX];
    int foreign_library_count = 0;
    enum { BLOCK_CALL_CAP = 64 };
    BlockCall *block_calls = calloc(BLOCK_CALL_CAP, sizeof(*block_calls));
    int block_call_count = 0;
    int root_anonymous_block_count = 0;
    SlotParseFrame slot_frames[64];
    int slot_frame_count = 0;

    if(block_calls == NULL)
        die("out of memory");
    memset(&consts, 0, sizeof(consts));
    cur_guard[0] = '\0';
    snprintf(rel, sizeof(rel), "%s", relative_path(root, path));
    const char *basename = strrchr(path, '/');
    basename = basename != NULL ? basename + 1 : path;
    size_t stem_length = strlen(basename);
    if(stem_length > 3 && strcmp(basename + stem_length - 3, ".zi") == 0)
        stem_length -= 3;
    if(stem_length == 0 || stem_length >= sizeof(module_name))
        die_at(Span(rel, 1, 1), "source filename cannot be used as a module name");
    memcpy(module_name, basename, stem_length);
    module_name[stem_length] = '\0';
    program = ProgramNew();
    if(program == NULL)
        die("out of memory");

    module = ProgramAddModule(program, module_name, rel, Span(rel, 1, 1));
    if(module == NULL)
        die("out of memory");

    while(have_look || onelineq_count > 0 || read_source_line(line, sizeof(line), in, &source, path, line_no) != NULL) {
        char raw[K2ZIR_LINE_MAX];
        char *t;
        int from_lookahead = 0;

        /* Queued one-liner parts outrank the stashed lookahead: they belong
         * before the next source line, and have_look persists until the
         * queue drains. */
        from_queue = onelineq_count > 0;
        if(onelineq_count > 0) {
            copy_text(line, sizeof(line), onelineq[0]);
            memmove(onelineq[0], onelineq[1],
                    sizeof(onelineq[0]) * (size_t)(onelineq_count - 1));
            onelineq_count--;
        } else if(have_look) {
            snprintf(line, sizeof(line), "%s", lookahead);
            have_look = 0;
            from_lookahead = 1;
        }

        line_no++;
        strip_block_comments(line, &in_block_comment);
        if(!from_queue && !from_lookahead)
            normalize_jai_type_names(line, rel, line_no);
        snprintf(raw, sizeof(raw), "%s", line);
        {
            char *trimmed = trim(raw);
            int trimmed_column = from_queue ? 1 :
                                 source_column_for_trimmed(raw, trimmed);

            if(trimmed[0] == '\0' || strncmp(trimmed, "//", 2) == 0) {
                if(pending_len == 0)
                    continue;
                continue;
            }
            if(pending_len == 0) {
                pending_start_line = line_no;
                pending_start_column = trimmed_column;
            }
            pending_end_column = source_end_column_for_trimmed(raw, trimmed);
            if(pending_len > 0 && pending_len + 2 < (int)sizeof(pending)) {
                pending[pending_len++] = ' ';
                pending[pending_len] = '\0';
            }
            strncat(pending, trimmed, sizeof(pending) - pending_len - 1);
            pending_len = (int)strlen(pending);
            pending_len = (int)strlen(pending);
            /* Decide whether braces at paren-depth 0 on this logical line are
             * block braces (control/headers open scopes) or expression braces
             * (compound literals / initializers continue the statement). */
            {
                int header_line = 0;
                char w0[16];
                size_t wl = 0;

                for(const char *w = pending;
                    *w != '\0' && (isalnum((unsigned char)*w) || *w == '_') &&
                    wl + 1 < sizeof(w0); w++)
                    w0[wl++] = *w;
                w0[wl] = '\0';
                /* Keyword headers must be followed by ' ', '(' or '{':
                 * 'item->x = ...' / 'state.x' are member statements, not
                 * block headers (their compound-literal braces are
                 * expression braces). */
                {
                    char nc = pending[wl];
                    char block_callee[ZIR_NAME_MAX];
                    char block_name[ZIR_NAME_MAX];
                    char slot_binding[ZIR_TEXT_MAX], slot_arguments[ZIR_TEXT_MAX];

                    header_line =
                        pending[0] == '#' ||
                        strcmp(pending, "{") == 0 ||   /* bare scope-open */
                        parse_slot_header(pending, slot_binding, sizeof(slot_binding),
                                          slot_arguments, sizeof(slot_arguments)) ||
                        parse_block_call_header(pending, block_callee,
                                                sizeof(block_callee), block_name,
                                                sizeof(block_name)) ||
                        /* 'name :: Type = {' carries an initializer, not a
                         * body: its braces are expression braces so the
                         * logical line continues until they balance. Header
                         * forms ('name :: struct {', 'f :: (args) {', typedefs,
                         * externs) never contain ' = '. */
                        (strstr(pending, " :: ") != NULL &&
                         strstr(pending, " = ") == NULL) ||
                        (nc != '\0' && nc != '-' && nc != '.' &&
                         (strchr(" ({", nc) != NULL || nc == ':') &&
                         (strcmp(w0, "if") == 0 ||
                          strcmp(w0, "else") == 0 ||
                          strcmp(w0, "while") == 0 ||
                          strcmp(w0, "for") == 0 ||
                          strcmp(w0, "switch") == 0 ||
                          strcmp(w0, "match") == 0 ||
                          strcmp(w0, "do") == 0 ||
                          strcmp(w0, "case") == 0 ||
                          strcmp(w0, "default") == 0 ||
                          strcmp(w0, "struct") == 0 ||
                          strcmp(w0, "enum") == 0 ||
                          strcmp(w0, "state") == 0));
                }
                /* K&R "} else {" / "} else if (...) {": the leading '}' closes
                 * the if-body and the trailing '{' re-opens the else-body, so
                 * both braces are block braces even though the leading word
                 * extraction above saw only '}'. The same holds for chained
                 * regions: "} #else_if COND {" / "} #else {" / "} #if COND {"
                 * (parse_cond_start accepts the optional leading '}'), whose
                 * braces belong to the region, not the statement. */
                if(!header_line && pending[0] == '}') {
                    const char *eq = pending + 1;

                    while(*eq == ' ' || *eq == '\t')
                        eq++;
                    if(starts_word(eq, "else") ||
                       strncmp(eq, "#else", 5) == 0 ||
                       starts_word(eq, "#if ") ||
                       starts_word(eq, "#elif "))
                        header_line = 1;
                }
                {
                int in_chr = 0;

                for(const char *p = trimmed; *p != '\0'; p++) {
                    if(in_string) {
                        if(*p == '\\' && p[1] != '\0')
                            p++;
                        else if(*p == '"')
                            in_string = 0;
                    } else if(in_chr) {
                        if(*p == '\\' && p[1] != '\0')
                            p++;
                        else if(*p == '\'')
                            in_chr = 0;
                    } else if(*p == '"') {
                        in_string = 1;
                    } else if(*p == '\'') {
                        in_chr = 1;
                    } else if(*p == '(') {
                        paren_depth++;
                    } else if(*p == ')') {
                        paren_depth--;
                    } else if(*p == '[') {
                        bracket_depth++;
                    } else if(*p == ']') {
                        bracket_depth--;
                    } else if(paren_depth == 0 && bracket_depth == 0) {
                        if(*p == '{') {
                            if(!header_line)
                                expr_brace++;
                        } else if(*p == '}') {
                            if(!header_line && expr_brace > 0)
                                expr_brace--;
                        }
                    }
                }
                }
            }
            if(paren_depth > 0 || bracket_depth > 0 || in_string ||
               expr_brace > 0)
                continue;
            /* Continuation: a line ending in a binary operator or comma
             * continues onto the next. Exclude ++/-- (they end statements). */
            {
                size_t pl = (size_t)pending_len;
                char last;
                char prev;

                while(pl > 0 && (pending[pl - 1] == ' ' ||
                                 pending[pl - 1] == '\t'))
                    pl--;
                last = pl > 0 ? pending[pl - 1] : '\0';
                prev = pl > 1 ? pending[pl - 2] : '\0';
                /* Continuation operators: ',','=','%','/' always; '+','-','*',
                 * '<','>' only in binary position (prev is space — excludes
                 * 'char*','x++' handled below,'<stdlib.h>'); '&','|' when
                 * doubled ('&&','||') or space-preceded; ':' only with an
                 * open ternary ('?' pending) — 'case 1:' and goto labels
                 * ('fail:') end their statement. */
                if(last == ',' || last == '=' || last == '%' ||
                   (last == ':' && prev != ':' &&
                    strchr(pending, '?') != NULL) ||
                   (last == '/' && prev != '>'))
                    continue;
                if((last == '+' || last == '-' || last == '*' ||
                    last == '<' || last == '>') &&
                   (prev == ' ' || prev == '\t') &&
                   !(prev == last))
                    continue;
                if((last == '&' || last == '|') &&
                   (prev == last || prev == ' ' || prev == '\t'))
                    continue;
                /* Look ahead: a next line starting with a continuation
                 * token ('?' / ':' ternary branches, '.', ',', leading
                 * binary operators) continues this statement. Skipped for queued one-liner
                 * parts and while a stash is pending: their "next line" is
                 * not the next physical source line, and reading ahead here
                 * would overwrite/lose the stashed one. */
                if(!from_queue && !have_look) {
                    char la[K2ZIR_LINE_MAX];
                    int pend_str;

                    /* C adjacent-literal concatenation: a statement whose
                     * last token closes a string ("...") continues when the
                     * next line opens a new literal ("...") — otherwise each
                     * fragment becomes its own orphan expression statement. */
                    {
                        int pl2 = (int)strlen(pending);

                        while(pl2 > 0 && (pending[pl2 - 1] == ' ' ||
                                          pending[pl2 - 1] == '\t'))
                            pl2--;
                        pend_str = pl2 > 0 && pending[pl2 - 1] == '"';
                    }

                    /* Keep consuming lookahead lines while they continue this
                     * statement; the first non-continuation line is stashed
                     * for the next iteration (appending it blindly here is
                     * how block-closing '}'s used to get swallowed). */
                    while(read_source_line(la, sizeof(la), in, &source, path, line_no) != NULL) {
                        const char *lt;
                        int cont;

                        strip_block_comments(la, &in_block_comment);
                        normalize_jai_type_names(la, rel, line_no + 1);
                        /* trim in place: the lookahead is appended verbatim,
                         * and a raw fgets line would carry its '\n' into the
                         * joined statement text. */
                        lt = trim(la);
                        if(lt[0] == '\0') {
                            line_no++;   /* blank lookaheads still count */
                            continue;
                        }
                        cont =
                            *lt == '?' || (*lt == ':' && lt[1] != ':') ||
                            *lt == '.' || *lt == ',' || *lt == '+' ||
                            *lt == '/' || *lt == '%' ||
                            (*lt == '-' && lt[1] != '>') ||
                            ((*lt == '&' && lt[1] == '&') ||
                             (*lt == '|' && lt[1] == '|') ||
                             (*lt == '=' && lt[1] == '=') ||
                             (*lt == '!' && lt[1] == '=') ||
                             (*lt == '<' && lt[1] == '=') ||
                             (*lt == '>' && lt[1] == '=')) ||
                            (*lt == '"' && pend_str);
                        if(!cont) {
                            snprintf(lookahead, sizeof(lookahead), "%s", la);
                            have_look = 1;
                            break;
                        }
                        pending_end_column = source_end_column_for_trimmed(la, lt);
                        if(pending_len > 0 &&
                           pending_len + 2 < (int)sizeof(pending)) {
                            pending[pending_len++] = ' ';
                            pending[pending_len] = '\0';
                        }
                        strncat(pending, lt,
                                sizeof(pending) - pending_len - 1);
                        pending_len = (int)strlen(pending);
                        line_no++;
                        /* the joined statement now ends with whatever this
                         * fragment ended with */
                        {
                            int pl2 = pending_len;

                            while(pl2 > 0 && (pending[pl2 - 1] == ' ' ||
                                              pending[pl2 - 1] == '\t'))
                                pl2--;
                            pend_str = pl2 > 0 && pending[pl2 - 1] == '"';
                        }
                    }
                }
            }
        }
        t = pending;
        {
            static char logical[K2ZIR_LINE_MAX * 4];

            snprintf(logical, sizeof(logical), "%s", pending);
            t = logical;
            /* One-line control block: keep the header as this logical line
             * and queue the body + closer for the next iterations (nested
             * one-liners split again when their body is finalized). */
            if(mode != TOP) {
                char head[K2ZIR_LINE_MAX * 2];
                char body[K2ZIR_LINE_MAX * 2];

                if(split_oneline_block(t, head, sizeof(head),
                                       body, sizeof(body))) {
                    if(onelineq_count + 2 <=
                       (int)(sizeof(onelineq) / sizeof(onelineq[0]))) {
                        snprintf(onelineq[onelineq_count++],
                                 sizeof(onelineq[0]), "%s", body);
                        snprintf(onelineq[onelineq_count++],
                                 sizeof(onelineq[0]), "}");
                        snprintf(logical, sizeof(logical), "%s", head);
                        t = logical;
                    }
                }
            }
        }
        pending[0] = '\0';
        pending_len = 0;
        if(mode == TOP && contains_source_directive(t, "#private"))
            die_at(Span(rel, line_no, 1),
                   "#private is not Jai syntax; use #scope_file");
        if(mode == TOP && looks_like_function_header(t))
            for(const char *modifier = strchr(t, '#'); modifier != NULL;
                modifier = strchr(modifier + 1, '#'))
                if(!starts_word(modifier, "#foreign") &&
                   !starts_word(modifier, "#slot"))
                    die_at(Span(rel, line_no, 1),
                           "unknown function modifier: %s", modifier);
        if(mode == TOP &&
           cond_top_step(t, tframes, &tframe_count, cur_guard,
                         sizeof(cur_guard), &consts, rel, line_no)) {
            continue;
        } else if(mode == TOP &&
                  parse_compile_check(module, rel, line_no, t, &consts,
                                      cur_guard)) {
            continue;
        } else if(mode == TOP &&
                  (strcmp(t, "#scope_file") == 0 ||
                   strcmp(t, "#scope_module") == 0 ||
                   strcmp(t, "#scope_export") == 0)) {
            scope_public = strcmp(t, "#scope_export") == 0;
            continue;
        } else if(mode == TOP && strcmp(t, "#program_export") == 0) {
            if(program_export)
                die_at(Span(rel, line_no, 1),
                       "#program_export must precede exactly one function");
            program_export = 1;
            program_export_line = line_no;
            continue;
        } else if(mode == TOP && program_export &&
                  !looks_like_function_header(t)) {
            die_at(Span(rel, program_export_line, 1),
                   "#program_export must precede a function declaration");
        } else if(mode == TOP && t[0] == '#' &&
                  !starts_word(t, "#import") &&
                  !starts_word(t, "#enum")) {
            if(t[1] != '\0' && t[1] != ' ' && t[1] != '\t')
                die_at(Span(rel, line_no, 1),
                       "unknown directive: %s", t);
        } else if(mode == TOP &&
                  parse_foreign_library_line(rel, line_no, t,
                      foreign_library_names, foreign_library_targets,
                      &foreign_library_count)) {
            continue;
        } else if(mode == TOP &&
                  (parse_import_line(module, rel, line_no, t,
                                     scope_public) ||
                   parse_foreign_line(module, rel, line_no, t,
                       scope_public, foreign_library_names,
                       foreign_library_targets, foreign_library_count))) {
            program_export = 0;
            if(module->import_count > 0)
                snprintf(module->imports[module->import_count - 1].guard,
                         sizeof(module->imports[0].guard), "%s", cur_guard);
            continue;
        } else if(mode == TOP && starts_word(t, "state") && strchr(t, '{') != NULL) {
            mode = STATE;
        } else if(mode == STATE) {
            if(t[0] == '}') {
                mode = TOP;
                cond_frame_settle(tframes, tframe_count);
            } else {
                parse_state_field(module, rel, line_no, t);
                if(module->state_count > 0)
                    snprintf(module->state_fields[module->state_count - 1].guard,
                             sizeof(module->state_fields[0].guard), "%s",
                             cur_guard);
            }
        } else if(mode == TOP && looks_like_function_header(t)) {
            char name[ZIR_NAME_MAX];
            char args[ZIR_TEXT_MAX];
            char ret[ZIR_NAME_MAX];
            int is_extern = strstr(t, "#foreign") != NULL;
            int has_body = strchr(t, '{') != NULL;

            parse_function_header(name, sizeof(name), args, sizeof(args),
                                  ret, sizeof(ret), t);
            if(strstr(t, "#slot") != NULL) {
                if(program_export)
                    die_at(Span(rel, program_export_line, 1),
                           "#program_export requires a function");
                if(has_body || is_extern || strcmp(ret, "void") != 0)
                    die_at(Span(rel, line_no, 1), "slot declarations require a bodyless void signature");
                ZirType *slot = ModuleAddType(module, name, Span(rel, line_no, 1));
                slot->is_slot = 1;
                slot->is_public = scope_public;
                copy_text(slot->body, sizeof(slot->body), args);
                copy_text(slot->guard, sizeof(slot->guard), cur_guard);
            } else if(name[0] != '\0') {
                fn = ModuleAddFunction(module, name, args, ret, 0,
                                          Span(rel, line_no, 1));
                snprintf(fn->guard, sizeof(fn->guard), "%s", cur_guard);
                fn->is_extern = is_extern;
                /* Foreign declarations are normally imports, handled above.
                 * Keep function metadata complete if this path is reached. */
                if(is_extern) {
                    const char *dir = strstr(t, "#foreign");

                    if(dir != NULL) {
                        const char *q = strchr(dir + 8, '"');

                        if(q != NULL) {
                            size_t n = 0;
                            const char *r = q + 1;

                            while(*r != '\0' && *r != '"' &&
                                  n + 1 < sizeof(fn->extern_target))
                                fn->extern_target[n++] = *r++;
                            fn->extern_target[n] = '\0';
                        }
                    }
                    fn->extern_kind = classify_extern_target(
                        fn->extern_target, fn->extern_symbol,
                        sizeof(fn->extern_symbol), rel, line_no);
                }
                /* Jai's standalone #program_export keeps the plain symbol
                 * for native callers. */
                fn->exported = program_export;
                program_export = 0;
                /* Public functions are emitted in headers. */
                fn->is_public = !is_extern && scope_public;
                if(has_body && !is_extern) {
                    mode = FUNCTION;
                    depth = 1;
                    block_call_count = 0;
                    root_anonymous_block_count = 0;
                } else {
                    /* extern / body-less prototype: no body follows */
                    fn = NULL;
                }
            }
        } else if(mode == TOP && contains_source_directive(t, "#global")) {
            die_at(Span(rel, line_no, 1),
                   "#global is not Jai syntax; declare a variable with name: Type");
        } else if(mode == TOP &&
                  (isalpha((unsigned char)t[0]) || t[0] == '_') &&
                  strchr(t, ':') != NULL && strstr(t, "::") == NULL) {
            /* A Jai file-scope variable uses the same typed declaration as
             * a local: name: Type; or name: Type = initializer; */
            char gname[ZIR_NAME_MAX];
            char gtype[ZIR_TEXT_MAX];
            char ginit[ZIR_TEXT_MAX];
            const char *colon = strchr(t, ':');
            const char *ty = colon + 1;
            const char *eq = strchr(ty, '=');
            const char *tyend = eq != NULL ? eq : t + strlen(t);
            size_t nn = 0;

            if(t[strlen(t) - 1] != ';')
                die_at(Span(rel, line_no, 1),
                       "file-scope variable declaration needs ';'");

            for(const char *p = t; p < colon; p++) {
                if(!isalnum((unsigned char)*p) && *p != '_')
                    die_at(Span(rel, line_no, 1),
                           "invalid file-scope variable name");
                if(nn + 1 >= sizeof(gname))
                    die_at(Span(rel, line_no, 1),
                           "file-scope variable name is too long");
                gname[nn++] = *p;
            }
            gname[nn] = '\0';
            while(*ty == ' ' || *ty == '\t')
                ty++;
            nn = 0;
            while(ty < tyend && nn + 1 < sizeof(gtype))
                gtype[nn++] = *ty++;
            if(ty != tyend)
                die_at(Span(rel, line_no, 1),
                       "file-scope variable type is too long");
            while(nn > 0 && (isspace((unsigned char)gtype[nn - 1]) ||
                             gtype[nn - 1] == ';'))
                nn--;
            gtype[nn] = '\0';
            if(gtype[0] == '\0')
                die_at(Span(rel, line_no, 1),
                       "file-scope variable needs a type");
            nn = 0;
            if(eq != NULL) {
                const char *ib = eq + 1;

                while(*ib == ' ' || *ib == '\t')
                    ib++;
                while(*ib != '\0' && nn + 1 < sizeof(ginit))
                    ginit[nn++] = *ib++;
                if(*ib != '\0')
                    die_at(Span(rel, line_no, 1),
                           "file-scope variable initializer is too long");
                while(nn > 0 && (isspace((unsigned char)ginit[nn - 1]) ||
                                 ginit[nn - 1] == ';'))
                    nn--;
                if(nn == 0)
                    die_at(Span(rel, line_no, 1),
                           "file-scope variable initializer is empty");
            }
            ginit[nn] = '\0';
            if(scope_public)
                ModuleAddGlobal(module, gname, gtype, ginit,
                                Span(rel, line_no, 1));
            else
                ModuleAddStatic(module, gname, gtype, ginit,
                                Span(rel, line_no, 1));
            if(module->global_count > 0)
                snprintf(module->globals[module->global_count - 1].guard,
                         sizeof(module->globals[0].guard), "%s", cur_guard);
        } else if(mode == TOP && strstr(t, "::") != NULL &&
                  strstr(t, "#type") != NULL) {
            /* 'Name :: C-type #type' — a typedef. Build the C declarator:
             * function-pointer types insert the name after '(*'; others
             * append ' NAME'. Must precede the struct catch-all below. */
            ZirType *tty;
            const char *colons0 = strstr(t, "::");
            const char *tybegin = colons0 + 2;
            const char *hash = strstr(t, "#type");
            char tname[ZIR_NAME_MAX];
            size_t tn = 0;
            const char *q = t;

            while(q < colons0 && (isalnum((unsigned char)*q) || *q == '_') &&
                  tn + 1 < sizeof(tname))
                tname[tn++] = *q++;
            tname[tn] = '\0';
            tty = ModuleAddType(module, "#typedef",
                                   Span(rel, line_no, 1));
            if(tty != NULL) {
                tty->is_public = scope_public;
                snprintf(tty->guard, sizeof(tty->guard), "%s", cur_guard);
            }
            if(tty != NULL && tname[0] != '\0') {
                char tytext[ZIR_TEXT_MAX];
                size_t tl;
                const char *lp;

                while(tybegin < hash && (*tybegin == ' ' || *tybegin == '\t'))
                    tybegin++;
                tl = (size_t)(hash - tybegin);
                while(tl > 0 && (tybegin[tl - 1] == ' ' || tybegin[tl - 1] == '\t'))
                    tl--;
                if(tl >= sizeof(tytext))
                    tl = sizeof(tytext) - 1;
                memcpy(tytext, tybegin, tl);
                tytext[tl] = '\0';
                lp = strstr(tytext, "(*");
                if(lp != NULL) {
                    size_t off = (size_t)(lp - tytext) + 2;

                    snprintf(tty->body, sizeof(tty->body), "%.*s%s%s",
                             (int)off, tytext, tname, tytext + off);
                } else {
                    snprintf(tty->body, sizeof(tty->body), "%s %s",
                             tytext, tname);
                }
            }
        } else if(mode == TOP && strstr(t, "::") != NULL &&
                  starts_word(skip_ws(strstr(t, "::") + 2),
                              "specialize")) {
            die_at(Span(rel, line_no, 1),
                   "use Jai-style type application: Name :: Generic(Type)");
        } else if(mode == TOP && strstr(t, "::") != NULL &&
                  strchr(t, '{') == NULL &&
                  !looks_like_function_header(t)) {
            /* 'Name :: expr' is the Ziran constant declaration. Ordinary
             * constants are also emitted into generated interfaces so public
             * types can use them in array bounds. Platform predicates remain
             * frontend-only because #defined is not a C expression. */
            const char *colons = strstr(t, "::");
            char *expr = (char *)colons + 2;
            char cname[ZIR_NAME_MAX];
            size_t cn = 0;
            const char *q = t;

            while(q < colons && (isalnum((unsigned char)*q) || *q == '_') &&
                  cn + 1 < sizeof(cname))
                cname[cn++] = *q++;
            cname[cn] = '\0';
            while(*expr == ' ' || *expr == '\t')
                expr++;
            size_t expression_length = strlen(expr);
            while(expression_length > 0 &&
                  isspace((unsigned char)expr[expression_length - 1]))
                expr[--expression_length] = '\0';
            if(expression_length > 0 && expr[expression_length - 1] == ';')
                expr[--expression_length] = '\0';
            while(expression_length > 0 &&
                  isspace((unsigned char)expr[expression_length - 1]))
                expr[--expression_length] = '\0';
            if(cname[0] != '\0' && *expr != '\0') {
                if(starts_word(expr, "#define"))
                    die_at(Span(rel, line_no, 1), "use '%s :: value'; #define is not Ziran syntax", cname);
                else {
                    char run_value[ZIR_TEXT_MAX];
                    ZirDefine *def;

                    if(consts.count == consts.capacity) {
                        int capacity = consts.capacity > 0 ? consts.capacity * 2 : 16;
                        ZirConst *items = realloc(consts.items,
                            (size_t)capacity * sizeof(*items));

                        if(items == NULL)
                            die("out of memory");
                        consts.items = items;
                        consts.capacity = capacity;
                    }
                    if(starts_word(expr, "#run")) {
                        char expanded[ZIR_TEXT_MAX];
                        long value = 0;

                        expand_compile_expr(expanded, sizeof(expanded),
                                            &consts, trim((char *)(expr + 4)));
                        if(!eval_const_condition(expanded, &value))
                            die_at(Span(rel, line_no, 1), "#run expression is not a constant: %s", expanded);
                        snprintf(run_value, sizeof(run_value), "%ld", value);
                        expr = run_value;
                    }
                    int emitted_alias = 0;
                    if(is_identifier_text(expr)) {
                        for(int i = 0; i < module->define_count; i++) {
                            if(!strcmp(module->defines[i].name, expr)) {
                                emitted_alias = 1;
                                break;
                            }
                        }
                    }
                    if(!starts_word(expr, "#defined") &&
                       (!is_identifier_text(expr) || emitted_alias)) {
                        def = ModuleAddDefine(module, cname, expr,
                                                 Span(rel, line_no, 1));
                        if(def != NULL) {
                            def->is_public = scope_public;
                            snprintf(def->guard, sizeof(def->guard), "%s",
                                     cur_guard);
                        }
                    }
                    snprintf(consts.items[consts.count].name,
                             sizeof(consts.items[0].name), "%s", cname);
                    snprintf(consts.items[consts.count].expr,
                             sizeof(consts.items[0].expr), "%s", expr);
                    consts.count++;
                }
            }
        } else if(mode == TOP && strstr(t, "::") != NULL) {
            /* Named records, enums, and variants. Variant cases elaborate
             * into sealed tagged storage and ordinary checked operations. */
            const char *colons = strstr(t, "::");
            const char *after = colons + 2;
            char tname[ZIR_NAME_MAX];
            size_t tn = 0;

            after = skip_ws(after);
            if((strncmp(after, "struct", 6) == 0 &&
                (after[6] == '\0' || after[6] == ' ' ||
                 after[6] == '(' || after[6] == '{')) ||
               (strncmp(after, "enum", 4) == 0 &&
                (after[4] == '\0' || after[4] == ' ' || after[4] == '{')) ||
               (strncmp(after, "variant", 7) == 0 &&
                (after[7] == '\0' || after[7] == ' ' ||
                 after[7] == '(' || after[7] == '{'))) {
                const char *q = t;
                ZirType *ty;

                while(q < colons && (isalnum((unsigned char)*q) || *q == '_') &&
                      tn + 1 < sizeof(tname))
                    tname[tn++] = *q++;
                tname[tn] = '\0';
                char parameters[ZIR_NAME_MAX] = "";
                if(skip_ws(q) != colons)
                    die_at(Span(rel, line_no, 1),
                           "type declarations require a plain name before ::");
                if(!is_identifier_text(tname))
                    die_at(Span(rel, line_no, 1), "invalid type name");
                if((strncmp(after, "struct", 6) == 0 &&
                     !parse_type_parameters(after, "struct", parameters,
                                            sizeof(parameters))) ||
                   (strncmp(after, "variant", 7) == 0 &&
                     !parse_type_parameters(after, "variant", parameters,
                                            sizeof(parameters))))
                    die_at(Span(rel, line_no, 1),
                           "polymorphic type parameters require (T: Type, ...)");
                ty = ModuleAddType(module, tname,
                                      Span(rel, line_no, 1));
                if(ty != NULL) {
                    ty->is_public = scope_public;
                    ty->is_enum = strncmp(after, "enum", 4) == 0;
                    ty->is_variant_template = parameters[0] != '\0' &&
                        strncmp(after, "variant", 7) == 0;
                    ty->is_record_template = parameters[0] != '\0' &&
                        strncmp(after, "struct", 6) == 0;
                    ty->is_variant = strncmp(after, "variant", 7) == 0 &&
                                     !ty->is_variant_template;
                    copy_text(ty->template_params,
                              sizeof(ty->template_params), parameters);
                    if(strstr(after, "#extern") != NULL)
                        die_at(Span(rel, line_no, 1),
                               "#extern is not Jai syntax for a type declaration");
                    snprintf(ty->guard, sizeof(ty->guard), "%s", cur_guard);
                    const char *opening =
                        (ty->is_enum || ty->is_variant ||
                         ty->is_variant_template || ty->is_record_template) ?
                        strchr(after, '{') : NULL;
                    const char *closing = opening ? strchr(opening + 1, '}') : NULL;
                    if(closing != NULL) {
                        size_t body_length = (size_t)(closing - opening - 1);
                        if(ty->is_variant || ty->is_variant_template) {
                            if(body_length + 1 >= sizeof(ty->variant_cases))
                                die_at(ty->span, "variant body exceeds size limit");
                            memcpy(ty->variant_cases, opening + 1, body_length);
                            int nesting = 0;
                            for(size_t byte = 0; byte < body_length; byte++) {
                                if(ty->variant_cases[byte] == '[' ||
                                   ty->variant_cases[byte] == '(') nesting++;
                                if(ty->variant_cases[byte] == ']' ||
                                   ty->variant_cases[byte] == ')') nesting--;
                                if(ty->variant_cases[byte] == ',' && nesting == 0)
                                    ty->variant_cases[byte] = '\n';
                            }
                            ty->variant_cases[body_length] = '\n';
                            ty->variant_cases[body_length + 1] = '\0';
                            if(ty->is_variant)
                                finalize_variant(module, ty);
                        } else if(ty->is_record_template) {
                            if(body_length + 1 >= sizeof(ty->body))
                                die_at(ty->span, "generic record body exceeds size limit");
                            memcpy(ty->body, opening + 1, body_length);
                            int nesting = 0;
                            for(size_t byte = 0; byte < body_length; byte++) {
                                if(ty->body[byte] == '[' || ty->body[byte] == '(')
                                    nesting++;
                                if(ty->body[byte] == ']' || ty->body[byte] == ')')
                                    nesting--;
                                if(ty->body[byte] == ',' && nesting == 0)
                                    ty->body[byte] = '\n';
                            }
                            ty->body[body_length] = '\n';
                            ty->body[body_length + 1] = '\0';
                        } else {
                            if(body_length >= sizeof(ty->body))
                                die_at(ty->span, "enum body exceeds size limit");
                            memcpy(ty->body, opening + 1, body_length);
                            ty->body[body_length] = '\0';
                            if(ty->is_enum)
                                for(size_t byte = 0; byte < body_length; byte++)
                                    if(ty->body[byte] == ';') ty->body[byte] = '\n';
                        }
                    } else {
                        mode = TYPE;
                    }
                }
                fn = NULL;
            }
        } else if((mode == TOP || mode == TYPE) &&
                  strncmp(t, "#enum", 5) == 0) {
            /* #enum { ... } — capture the constants as a type body. */
            ZirType *ety = ModuleAddType(module, "#enum",
                                            Span(rel, line_no, 1));

            if(ety != NULL)
                ety->is_public = scope_public;
            if(ety != NULL)
                snprintf(ety->guard, sizeof(ety->guard), "%s", cur_guard);
            (void)ety;
            enum_return = mode;
            if(strchr(t, '}') == NULL)
                mode = ENUM;
        } else if(mode == ENUM) {
            size_t tl = strlen(t);

            if(t[0] == '}') {
                mode = enum_return;
                cond_frame_settle(tframes, tframe_count);
            } else if(tl > 0 && t[tl - 1] == '}') {
                /* joined constants + closing brace on one line */
                ZirType *ety = &module->types[module->type_count - 1];
                size_t used = strlen(ety->body);

                snprintf(ety->body + used, sizeof(ety->body) - used,
                         "%.*s\n", (int)(tl - 1), t);
                mode = enum_return;
                cond_frame_settle(tframes, tframe_count);
            } else {
                ZirType *ety = &module->types[module->type_count - 1];
                size_t used = strlen(ety->body);

                snprintf(ety->body + used, sizeof(ety->body) - used,
                         "%s\n", t);
            }
        } else if(mode == TYPE) {
            if(t[0] == '}') {
                ZirType *ty = &module->types[module->type_count - 1];
                if(ty->is_variant)
                    finalize_variant(module, ty);
                mode = TOP;
                cond_frame_settle(tframes, tframe_count);
            } else if(t[0] == '#') {
                /* comment inside a struct body — skip */
            } else {
                ZirType *ty = &module->types[module->type_count - 1];
                char *body = (ty->is_variant || ty->is_variant_template) ?
                    ty->variant_cases : ty->body;
                size_t capacity = (ty->is_variant || ty->is_variant_template) ?
                    sizeof(ty->variant_cases) : sizeof(ty->body);
                size_t used = strlen(body);
                int written = snprintf(body + used, capacity - used, "%s\n", t);
                if(written < 0 || (size_t)written >= capacity - used)
                    die_at(ty->span, "type body exceeds size limit");
                if(ty->is_enum)
                    for(size_t byte = used; byte < used + (size_t)written; byte++)
                        if(body[byte] == ';') body[byte] = '\n';
            }
        } else if(mode == FUNCTION) {
            char *bcnd = NULL;
            int bck = parse_cond_start(t, &bcnd);

            if(bck != 0 || line_is_hash_else(t)) {
                /* body-level '#if COND {' — the braces are consumed here;
                 * the region lowers to raw #if/#elif/#else/#endif lines.
                 * (bck was computed once above: parse_cond_start strips the
                 * trailing '{' in place, so re-parsing would misfire.) */
                char raw[ZIR_TEXT_MAX];
                char expanded[ZIR_TEXT_MAX];

                if(line_is_hash_else(t)) {
                    snprintf(raw, sizeof(raw), "#else");
                } else if(bck == 1) {
                    if(body_mcount >= 8)
                        die_at(Span(rel, line_no, 1), "too many nested #if blocks");
                    body_mdepth[body_mcount++] = depth;
                    expand_compile_expr(expanded, sizeof(expanded), &consts,
                                        bcnd);
                    format_preprocessor_cond(raw, sizeof(raw), "#if",
                                             expanded);
                } else {
                    expand_compile_expr(expanded, sizeof(expanded), &consts,
                                        bcnd);
                    format_preprocessor_cond(raw, sizeof(raw), "#elif",
                                             expanded);
                }
                FunctionAddStmt(fn, ZIR_STMT_RAW, raw, "",
                                   Span(rel, line_no, 1));
            } else if(t[0] == '}' && body_mcount > 0 &&
                      depth == body_mdepth[body_mcount - 1]) {
                /* this '}' closes a body-level '#if' region, not a block */
                body_mcount--;
                FunctionAddStmt(fn, ZIR_STMT_RAW, "#endif", "",
                                   Span(rel, line_no, 1));
            } else if(t[0] == '#') {
                /* comment inside a body — skip (directives are top-level) */
            } else if(t[0] == '}' && slot_frame_count > 0 && depth == 1 && block_call_count == 0) {
                if(*skip_ws(t + 1))
                    die_at(Span(rel, line_no, 1), "slot body closing brace must be on its own line");
                SlotParseFrame *frame = &slot_frames[--slot_frame_count];
                fn = &module->functions[frame->function_index];
                depth = frame->depth;
                root_anonymous_block_count = frame->root_anonymous_count;
                block_call_count = frame->block_count;
                if(block_call_count)
                    memcpy(block_calls, frame->blocks, (size_t)block_call_count * sizeof(*block_calls));
                free(frame->blocks);
                frame->blocks = NULL;
                body_mcount = frame->body_count;
                memcpy(body_mdepth, frame->body_depth, sizeof(body_mdepth));
            } else if(t[0] == '}' && block_call_count > 0 &&
                      depth == block_calls[block_call_count - 1].close_depth) {
                BlockCall *block = &block_calls[block_call_count - 1];
                ZirSourceSpan block_span = block_call_close_span(block, rel,
                                                               line_no, t);

                block_call_open(fn, block, block_span, 1);
                if(block->statement_index >= 0 &&
                   block->statement_index < fn->stmt_count)
                    fn->stmts[block->statement_index].span = block_span;
                block_call_count--;
                if(depth > 0)
                    depth--;
            } else if(t[0] == '}') {
                /* K&R "} else {" / "} else if (...) {": the brace closes the
                 * if-body and the else re-opens a new one, so depth is net
                 * unchanged. Recorded as BLOCK_CLOSE plus an IF whose text
                 * starts with "else" — the C lowering emits the "} else"
                 * itself and suppresses the duplicate close. */
                const char *eq = t + 1;

                while(*eq == ' ' || *eq == '\t')
                    eq++;
                if(depth > 1 && starts_word(eq, "else")) {
                    ZirSourceSpan span = SpanEnd(rel, line_no,
                                                    pending_start_column +
                                                    (int)(eq - t),
                                                    line_no,
                                                    pending_end_column);
                    FunctionAddStmt(fn, ZIR_STMT_BLOCK_CLOSE, "}", "",
                                       Span(rel, line_no, 1));
                    FunctionAddStmt(fn, ZIR_STMT_IF, eq, "", span);
                } else {
                    if(depth > 0)
                        depth--;
                    if(depth == 0) {
                        mode = TOP;
                        cond_frame_settle(tframes, tframe_count);
                        fn = NULL;
                        block_call_count = 0;
                    } else {
                        FunctionAddStmt(fn, ZIR_STMT_BLOCK_CLOSE, t, "",
                                           Span(rel, line_no, 1));
                    }
                }
            } else {
                ZirStmtKind kind = classify_stmt(t);
                int brace_delta = net_block_braces(t);
                char block_callee[ZIR_NAME_MAX];
                char block_name[ZIR_NAME_MAX];
                char prop_field[ZIR_NAME_MAX];
                char prop_value[ZIR_TEXT_MAX];
                char prop_line[ZIR_TEXT_MAX];

                char slot_binding[ZIR_TEXT_MAX], slot_arguments[ZIR_TEXT_MAX];
                if(parse_slot_header(t, slot_binding, sizeof(slot_binding),
                                     slot_arguments, sizeof(slot_arguments))) {
                    if(slot_frame_count == 64)
                        die_at(Span(rel, line_no, 1), "too many nested slot bodies");
                    SlotParseFrame *frame = &slot_frames[slot_frame_count++];
                    frame->function_index = (int)(fn - module->functions);
                    frame->depth = depth;
                    frame->root_anonymous_count = root_anonymous_block_count;
                    frame->body_count = body_mcount;
                    memcpy(frame->body_depth, body_mdepth, sizeof(body_mdepth));
                    char name[ZIR_NAME_MAX];
                    snprintf(name, sizeof(name), "slot_body_%d_%d", module->function_count, line_no);
                    ZirSourceSpan span = Span(rel, line_no, 1);
                    if(block_call_count > 0 && !block_calls[block_call_count - 1].opened &&
                       depth == block_calls[block_call_count - 1].close_depth &&
                       is_identifier_text(slot_binding)) {
                        block_call_append_field(&block_calls[block_call_count - 1], slot_binding, name, span);
                    } else {
                        char initializer[ZIR_TEXT_MAX];
                        int written = snprintf(initializer, sizeof(initializer),
                                               "%s = %s", slot_binding, name);
                        if(written < 0 || (size_t)written >= sizeof(initializer))
                            die_at(span, "slot initializer is too long");
                        FunctionAddStmt(fn, strchr(slot_binding, ':') ? ZIR_STMT_DECL : ZIR_STMT_ASSIGN,
                                           initializer, "", span);
                    }
                    frame->block_count = block_call_count;
                    frame->blocks = block_call_count ? malloc((size_t)block_call_count * sizeof(*block_calls)) : NULL;
                    if(block_call_count && frame->blocks == NULL)
                        die("out of memory parsing slot body");
                    if(block_call_count)
                        memcpy(frame->blocks, block_calls, (size_t)block_call_count * sizeof(*block_calls));
                    fn = ModuleAddFunction(module, name, slot_arguments, "void", 0, span);
                    fn->is_closure = 1;
                    copy_text(fn->guard, sizeof(fn->guard), cur_guard);
                    depth = 1;
                    root_anonymous_block_count = 0;
                    block_call_count = body_mcount = 0;
                    continue;
                }
                if(parse_block_call_header(t, block_callee,
                                         sizeof(block_callee),
                                         block_name, sizeof(block_name))) {
                    BlockCall *block;

                    if(block_name[0] != '\0' &&
                       !is_identifier_text(block_name))
                        die_at(Span(rel, line_no, 1),
                               "invalid block result name: %s", block_name);
                    if(block_call_count > 0)
                        block_call_open(fn, &block_calls[block_call_count - 1],
                                      Span(rel, line_no,
                                              pending_start_column), 0);
                    if(block_call_count >= BLOCK_CALL_CAP)
                        die_at(Span(rel, line_no, 1), "too many nested block calls");
                    block = &block_calls[block_call_count++];
                    memset(block, 0, sizeof(*block));
                    block->statement_index = -1;
                    block->span = SpanEnd(rel, pending_start_line,
                                             pending_start_column, line_no,
                                             pending_end_column);
                    snprintf(block->callee, sizeof(block->callee), "%s",
                             block_callee);
                    snprintf(block->name, sizeof(block->name), "%s",
                             block_name);
                    block->close_depth = depth + 1;
                    depth++;
                    continue;
                }

                copy_text(prop_line, sizeof(prop_line), t);
                if(block_call_count > 0 &&
                   !block_calls[block_call_count - 1].opened &&
                   depth == block_calls[block_call_count - 1].close_depth &&
                   parse_block_field_line(prop_line, prop_field,
                                      sizeof(prop_field), prop_value,
                                      sizeof(prop_value))) {
                    BlockCall *block = &block_calls[block_call_count - 1];
                    block_call_append_field(block, prop_field, prop_value,
                                             Span(rel, line_no, 1));
                    continue;
                }


                if(block_call_count > 0 &&
                   !block_calls[block_call_count - 1].opened &&
                   depth == block_calls[block_call_count - 1].close_depth)
                    block_call_open(fn, &block_calls[block_call_count - 1],
                                  Span(rel, line_no, pending_start_column),
                                  0);

                {
                    ZirSourceSpan span = SpanEnd(rel, pending_start_line,
                                                    pending_start_column,
                                                    line_no,
                                                    pending_end_column);
                    char jai_case[K2ZIR_LINE_MAX * 4];
                    const char *statement = t;
                    if(kind == ZIR_STMT_IF && starts_word(t, "if")) {
                        const char *condition = skip_ws(t + 2);
                        int complete = starts_word(condition, "#complete");
                        if(complete)
                            condition = skip_ws(condition + strlen("#complete"));
                        const char *equals = strstr(condition, "==");
                        if(equals != NULL && *skip_ws(equals + 2) == '{' &&
                           *skip_ws(skip_ws(equals + 2) + 1) == '\0') {
                            int length = snprintf(jai_case, sizeof(jai_case),
                                "%s %.*s {", complete ? "match!" : "match?",
                                (int)(equals - condition), condition);
                            if(length < 0 || (size_t)length >= sizeof(jai_case))
                                die_at(span, "if-case condition exceeds statement limit");
                            statement = jai_case;
                            kind = ZIR_STMT_MATCH;
                        }
                    }
                    FunctionAddStmt(fn, kind, statement, "", span);
                }
                depth += brace_delta;
                if(depth < 0)
                    depth = 0;
            }
        } else if(mode == TOP && t[0] != '\0') {
            die_at(Span(rel, line_no, 1), "invalid top-level declaration");
        }
    }
    if(program_export)
        die_at(Span(rel, program_export_line, 1),
               "#program_export must precede a function declaration");
    if(slot_frame_count)
        die_at(Span(rel, line_no, 1), "unterminated slot body");
    if(in != NULL)
        fclose(in);
    for(int mi = 0; mi < program->module_count; mi++)
        for(int fi = 0; fi < program->modules[mi].function_count; fi++) {
            if(!LowerCleanup(&program->modules[mi].functions[fi])) {
                ProgramFree(program);
                free(consts.items);
                free(block_calls);
                return NULL;
            }
            StructureFunction(&program->modules[mi].functions[fi], &program->modules[mi]);
        }
    free(consts.items);
    free(block_calls);
    return program;
}

ZirProgram *
parse_file(const char *path, const char *root)
{
    FILE *in = fopen(path, "rb");
    if(in == NULL)
        die_at(Span(path, 0, 0), "open failed: %s", strerror(errno));
    return parse_source(path, root, in, NULL);
}

ZirProgram *
parse_source_text(const char *path, const char *source)
{
    return parse_source(path, ".", NULL, source);
}
