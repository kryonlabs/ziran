#include "zir_cleanup.h"
#include "zir_text.h"
#include "zir_token.h"
#include "zir_expr.h"

#include <stdlib.h>
#include <string.h>

typedef struct Cleanup {
    ZirStmt statement;
    int depth;
} Cleanup;

static int append(ZirFunction *out, const ZirStmt *st);

static int
fail(const ZirStmt *st, const char *message)
{
    fprintf(stderr, "%s:%d:%d: %s\n", st->span.path, st->span.line,
            st->span.column, message);
    return 0;
}

static int
mentions(const char *text, const char *name)
{
    ZirLexer lexer;
    ZirToken token;
    ZirLexerInit(&lexer, text, "");
    do {
        token = ZirLexerNext(&lexer);
        if(token.kind == ZIR_TOKEN_IDENT && !strcmp(token.text, name)) return 1;
    } while(token.kind != ZIR_TOKEN_EOF);
    return 0;
}

static int
action_kind(const char *text, ZirStmt *action)
{
    ZirLexer lexer;
    ZirToken token;
    ZirFunction temporary = {0};
    int valid;
    action->kind = ZIR_STMT_EXPR;
    ZirLexerInit(&lexer, text, action->span.path);
    token = ZirLexerNext(&lexer);
    if(!strcmp(token.text, "return") || !strcmp(token.text, "break") ||
       !strcmp(token.text, "continue") || !strcmp(token.text, "goto") ||
       !strcmp(token.text, "defer") || !strcmp(token.text, "if") ||
       !strcmp(token.text, "while") || !strcmp(token.text, "for")) return 0;
    ZirLexerInit(&lexer, text, action->span.path);
    do {
        token = ZirLexerNext(&lexer);
        if(!strcmp(token.text, "=") || !strcmp(token.text, "+=") ||
           !strcmp(token.text, "-=") || !strcmp(token.text, "*=") ||
           !strcmp(token.text, "/=") || !strcmp(token.text, "%=") ||
           !strcmp(token.text, "&=") || !strcmp(token.text, "|=") ||
           !strcmp(token.text, "^=") || !strcmp(token.text, "<<=") || !strcmp(token.text, ">>="))
            action->kind = ZIR_STMT_ASSIGN;
    } while(token.kind != ZIR_TOKEN_EOF);
    zir_copy(action->text, sizeof(action->text), text);
    if(!append(&temporary, action)) return 0;
    ZirStructureFunction(&temporary, NULL);
    valid = temporary.stmts[0].expr_root >= 0 &&
            temporary.exprs[temporary.stmts[0].expr_root].kind != ZIR_EXPR_UNKNOWN;
    free(temporary.stmts); free(temporary.exprs);
    return valid;
}

static int
append(ZirFunction *out, const ZirStmt *st)
{
    ZirStmt *copy = ZirFunctionAddStmt(out, st->kind, st->text, st->callee,
                                      st->span);
    if(!copy)
        return 0;
    *copy = *st;
    return 1;
}

static int
emit(ZirFunction *out, Cleanup *entries, int count, int minimum)
{
    for(int i = count - 1; i >= 0 && entries[i].depth >= minimum; i--)
        if(!append(out, &entries[i].statement))
            return 0;
    return 1;
}

static int
opens(ZirStmtKind kind)
{
    return kind == ZIR_STMT_BLOCK_OPEN || kind == ZIR_STMT_IF ||
           kind == ZIR_STMT_WHILE || kind == ZIR_STMT_FOR || kind == ZIR_STMT_SWITCH;
}

static int
opens_stmt(const ZirStmt *st)
{
    return opens(st->kind) ||
           (st->kind == ZIR_STMT_CASE && strchr(st->text, '{') != NULL);
}

static int
end_block(const ZirFunction *fn, int start, int end)
{
    int depth = 1;
    for(int i = start + 1; i < end; i++) {
        if(opens_stmt(&fn->stmts[i])) depth++;
        if(fn->stmts[i].kind == ZIR_STMT_BLOCK_CLOSE && !--depth) return i;
    }
    return end;
}

static int
falls_through(const ZirFunction *fn, int start, int end)
{
    for(int i = start; i < end; i++) {
        ZirStmtKind kind = fn->stmts[i].kind;
        if(kind == ZIR_STMT_RETURN || kind == ZIR_STMT_BREAK || kind == ZIR_STMT_CONTINUE)
            return 0;
        if(opens_stmt(&fn->stmts[i])) {
            int close = end_block(fn, i, end);
            int falls = falls_through(fn, i + 1, close);
            if(kind == ZIR_STMT_BLOCK_OPEN && !falls) return 0;
            if(kind == ZIR_STMT_IF) {
                int has_else = 0;
                while(close + 1 < end && fn->stmts[close + 1].kind == ZIR_STMT_IF &&
                      strncmp(fn->stmts[close + 1].text, "else", 4) == 0) {
                    int next = close + 1;
                    has_else = strncmp(zir_skip_ws(fn->stmts[next].text + 4), "if", 2) != 0;
                    close = end_block(fn, next, end);
                    falls |= falls_through(fn, next + 1, close);
                }
                if(has_else && !falls) return 0;
            }
            i = close;
        }
    }
    return 1;
}

int
ZirLowerCleanup(ZirFunction *fn)
{
    int has_cleanup = 0, depth = 0, count = 0, serial = 0, ok = 0;
    Cleanup *entries;
    ZirStmtKind *scopes;
    int *scope_start;
    ZirFunction out = {0};

    for(int i = 0; i < fn->stmt_count; i++)
        has_cleanup |= fn->stmts[i].kind == ZIR_STMT_DEFER;
    if(!has_cleanup)
        return 1;
    entries = calloc((size_t)fn->stmt_count + 1, sizeof(*entries));
    scopes = calloc((size_t)fn->stmt_count + 1, sizeof(*scopes));
    scope_start = calloc((size_t)fn->stmt_count + 1, sizeof(*scope_start));
    if(!entries || !scopes || !scope_start)
        goto done;
    for(int i = 0; i < fn->stmt_count; i++) {
        const ZirStmt *st = &fn->stmts[i];
        if(st->kind == ZIR_STMT_IF && !strncmp(st->text, "guard ", 6) && count) {
            fail(st, "guard with defer requires an explicit if and return");
            goto done;
        }
        if((st->kind == ZIR_STMT_GOTO || st->kind == ZIR_STMT_LABEL) && count) {
            fail(st, "goto and labels in functions with defer are not supported");
            goto done;
        }
        if(st->kind == ZIR_STMT_RAW) {
            fail(st, "raw C and conditional preprocessing in functions with defer are not supported");
            goto done;
        }
        if(st->kind == ZIR_STMT_DECL) {
            ZirLexer lexer;
            ZirToken name;
            ZirLexerInit(&lexer, st->text, st->span.path);
            name = ZirLexerNext(&lexer);
            for(int d = 0; d < count; d++) {
                if(mentions(entries[d].statement.text, name.text)) {
                    fail(st, "a declaration cannot shadow a name referenced by an active defer");
                    goto done;
                }
            }
        }
        if(st->kind == ZIR_STMT_FOR) {
            for(int d = 0; d < count; d++) {
                ZirLexer lexer;
                ZirToken token;
                ZirLexerInit(&lexer, st->text, st->span.path);
                do {
                    token = ZirLexerNext(&lexer);
                    if(token.kind == ZIR_TOKEN_IDENT &&
                       mentions(entries[d].statement.text, token.text)) {
                        fail(st, "a for header cannot reuse a name referenced by an active defer; use a while loop");
                        goto done;
                    }
                } while(token.kind != ZIR_TOKEN_EOF);
            }
        }
        if(st->kind == ZIR_STMT_DEFER) {
            const char *body = zir_skip_ws(st->text + 5);
            ZirStmt *action = &entries[count].statement;
            if(scopes[depth] == ZIR_STMT_SWITCH) {
                fail(st, "defer in a switch case requires an explicit block");
                goto done;
            }
            /* Cleanup actions are expressions or assignments, never jumps or
             * declarations. Keep their source span for backend diagnostics. */
            *action = *st;
            if(!action_kind(body, action)) {
                fail(st, "defer requires one expression or assignment");
                goto done;
            }
            action->expr_root = -1;
            zir_copy(action->text, sizeof(action->text), body);
            entries[count++].depth = depth;
            continue;
        }
        if(st->kind == ZIR_STMT_RETURN && count) {
            ZirStmt result = *st;
            const char *value = zir_skip_ws(st->text + 6);
            if(*value && strcmp(value, ";")) {
                char name[ZIR_NAME_MAX];
                int collision;
                /* Reserve a fresh local without reserving user identifiers. */
                do {
                    snprintf(name, sizeof(name), "cleanup_return_%d", serial++);
                    collision = strstr(fn->args, name) != NULL;
                    for(int k = 0; k < fn->stmt_count; k++)
                        collision |= strstr(fn->stmts[k].text, name) != NULL;
                } while(collision);
                result.kind = ZIR_STMT_DECL;
                result.expr_root = st->expr_root;
                if(snprintf(result.text, sizeof(result.text), "%s: %s = %s",
                            name, fn->return_type, value) >= (int)sizeof(result.text)) {
                    fail(st, "return expression exceeds cleanup lowering limit");
                    goto done;
                }
                if(!append(&out, &result))
                    goto done;
                result.kind = ZIR_STMT_RETURN;
                result.expr_root = -1;
                snprintf(result.text, sizeof(result.text), "return %s", name);
            }
            if(!emit(&out, entries, count, 0) || !append(&out, &result))
                goto done;
            continue;
        }
        if(st->kind == ZIR_STMT_BREAK || st->kind == ZIR_STMT_CONTINUE) {
            int target = depth;
            while(target > 0 && scopes[target] != ZIR_STMT_FOR &&
                  scopes[target] != ZIR_STMT_WHILE &&
                  !(st->kind == ZIR_STMT_BREAK && scopes[target] == ZIR_STMT_SWITCH))
                target--;
            if(!target) {
                fail(st, "loop control outside a loop or switch");
                goto done;
            }
            if(!emit(&out, entries, count, target))
                goto done;
        }
        if(st->kind == ZIR_STMT_BLOCK_CLOSE) {
            if(!depth) {
                fail(st, "unbalanced cleanup scope");
                goto done;
            }
            if(falls_through(fn, scope_start[depth], i) &&
               !emit(&out, entries, count, depth))
                goto done;
            while(count && entries[count - 1].depth >= depth)
                count--;
            depth--;
        }
        if(!append(&out, st))
            goto done;
        if(opens_stmt(st)) {
            scopes[++depth] = st->kind;
            scope_start[depth] = i + 1;
        }
    }
    if(falls_through(fn, 0, fn->stmt_count) && !emit(&out, entries, count, 0))
        goto done;
    free(fn->stmts);
    fn->stmts = out.stmts;
    fn->stmt_count = out.stmt_count;
    fn->stmt_cap = out.stmt_cap;
    out.stmts = NULL;
    ok = 1;
done:
    free(out.stmts);
    free(entries);
    free(scopes);
    free(scope_start);
    return ok;
}
