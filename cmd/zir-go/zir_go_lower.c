/*
 * zir_go_lower.c - ZIR to Go backend. See zir_go_lower.h for scope.
 */
#include "zir_go_lower.h"
#include "zir_text.h"
#include "zir_emit.h"
#include "zir_check.h"
#include "zir_expr.h"
#include "zir_diagnostic.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ZIR_GO_TEXT_MAX 8192
#define ZIR_GO_NAME_MAX 256
#define ZIR_GO_EXTERN_PARAM_MAX 16
#define ZIR_GO_RUNTIME_IMPORT "github.com/waozixyz/kryon/go/kryon"
#define ZIR_GO_RUNTIME_PKG "kr"

static int runtime_output;
static const ZirModule *type_scope;
static char instance_receiver[ZIR_NAME_MAX];
static char state_receiver[ZIR_NAME_MAX] = "st";

typedef struct {
    char kry[ZIR_GO_NAME_MAX];
    char go[ZIR_GO_NAME_MAX];
} ZirGoLocalName;

static ZirGoLocalName zir_go_locals[256];
static int zir_go_local_count;

/* ---------------------------------------------------------------- helpers */

static void
mkdir_parent(const char *path)
{
    char tmp[1024];
    size_t i;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for(i = 1; i < strlen(tmp); i++) {
        if(tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
}

static void
go_string(FILE *f, const char *s)
{
    fputc('"', f);
    for(const unsigned char *p = (const unsigned char *)(s != NULL ? s : "");
        *p != '\0'; p++) {
        if(*p == '\\' || *p == '"')
            fprintf(f, "\\%c", *p);
        else if(*p == '\n')
            fputs("\\n", f);
        else if(*p == '\r')
            fputs("\\r", f);
        else if(*p == '\t')
            fputs("\\t", f);
        else if(*p < 0x20)
            fprintf(f, "\\x%02x", *p);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}

static int
go_keyword(const char *name)
{
    static const char *const words[] = {
        "break", "default", "func", "interface", "select",
        "case", "defer", "go", "map", "struct",
        "chan", "else", "goto", "package", "switch",
        "const", "fallthrough", "if", "range", "type",
        "continue", "for", "import", "return", "var", NULL
    };

    for(int i = 0; words[i] != NULL; i++)
        if(strcmp(name, words[i]) == 0)
            return 1;
    return 0;
}

static void
go_local_ident(const char *name, char *dst, size_t dst_size)
{
    if(go_keyword(name))
        snprintf(dst, dst_size, "%s_", name);
    else
        snprintf(dst, dst_size, "%s", name);
}

static const char *
go_local_name_for(const char *name)
{
    for(int i = zir_go_local_count - 1; i >= 0; i--)
        if(strcmp(zir_go_locals[i].kry, name) == 0)
            return zir_go_locals[i].go;
    return NULL;
}

static void
go_register_local_name(const char *name)
{
    char mapped[ZIR_GO_NAME_MAX];

    if(name == NULL || name[0] == '\0')
        return;
    for(int i = 0; i < zir_go_local_count; i++)
        if(strcmp(zir_go_locals[i].kry, name) == 0)
            return;
    go_local_ident(name, mapped, sizeof(mapped));
    if(strcmp(mapped, name) == 0 || zir_go_local_count >= 256)
        return;
    snprintf(zir_go_locals[zir_go_local_count].kry,
             sizeof(zir_go_locals[zir_go_local_count].kry), "%s", name);
    snprintf(zir_go_locals[zir_go_local_count].go,
             sizeof(zir_go_locals[zir_go_local_count].go), "%s", mapped);
    zir_go_local_count++;
}

/* stem of "path/app.kry" -> "app": Go output is flat (one package per
 * output directory), so nested source trees must not nest the output. */
static void
stem_from_source(const char *src, char *dst, size_t dst_size)
{
    const char *base = strrchr(src, '/');
    size_t n;

    base = base != NULL ? base + 1 : src;
    n = strlen(base);
    if(n > 3 && strcmp(base + n - 3, ".zi") == 0)
        n -= 3;
    if(n >= dst_size)
        n = dst_size - 1;
    memcpy(dst, base, n);
    dst[n] = '\0';
}

static int is_module_constant(const ZirModule *module, const char *name);

/* C-ish type -> Go type. Unknown shapes must be diagnosed by the caller. */
static int
go_type(const char *type, char *dst, size_t dst_size)
{
    struct {
        const char *c;
        const char *go;
    } map[] = {
        {"int", "int32"},   {"unsigned int", "uint32"},
        {"unsigned", "uint32"}, {"uint", "uint32"},
        {"long", "int64"},  {"unsigned long", "uint64"},
        {"long long", "int64"}, {"unsigned long long", "uint64"},
        {"short", "int16"}, {"unsigned short", "uint16"},
        {"size_t", "int64"}, {"ssize_t", "int64"},
        {"float", "float32"}, {"double", "float64"},
        {"bool", "bool"},   {"char*", "string"}, {"const char*", "string"},
        {"char**", "[]string"}, {"const char**", "[]string"},
        {"string", "string"},
        {"char", "byte"}, {"unsigned char", "byte"}, {"byte", "byte"},
        {"int8", "int8"}, {"int16", "int16"}, {"int32", "int32"},
        {"int64", "int64"}, {"float32", "float32"}, {"float64", "float64"},
        {"void*", "*byte"}, {"const void*", "*byte"},
        {"void", ""},
        {"Canvas", "Canvas"},
        {"CanvasResult", "CanvasResult"},
        {NULL, NULL}
    };
    char t[ZIR_GO_NAME_MAX];
    size_t n;

    snprintf(t, sizeof(t), "%s", type);
    n = strlen(t);
    while(n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t'))
        t[--n] = '\0';
    {
        const char *p = t;

        while(*p == ' ' || *p == '\t')
            p++;
        if(p != t)
            memmove(t, p, strlen(p) + 1);
        n = strlen(t);
    }
    const char *scalar = ScalarType(t);
    if(*scalar && (!strcmp(t,scalar) || strstr(t,"_t")) && TargetType(t,ZIR_GO)) {
        snprintf(dst,dst_size,"%s",TargetType(t,ZIR_GO)); return 1;
    }
    /* 'char *' / 'char  *' mean 'char*': drop spaces adjacent to '*' */
    for(size_t k = 0; t[k] != '\0'; k++) {
        if(t[k] == ' ' && t[k + 1] == '*') {
            memmove(t + k, t + k + 1, strlen(t + k));
            k = (size_t)-1;
        }
    }
    /* strip a leading 'const ' */
    if(strncmp(t, "const ", 6) == 0)
        memmove(t, t + 6, strlen(t + 6) + 1);
    if(t[0] == '[') {
        char *close = strchr(t, ']');
        const char *base;

        if(close != NULL) {
            char bound[ZIR_GO_NAME_MAX];
            snprintf(bound, sizeof(bound), "%.*s", (int)(close - t - 1), t + 1);
            if(type_scope != NULL && is_module_constant(type_scope, bound)) {
                char mapped[ZIR_GO_NAME_MAX];
                camel_ident(bound, mapped, sizeof(mapped));
                snprintf(bound, sizeof(bound), "%s", mapped);
            }
            base = close + 1;
            while(*base == ' ' || *base == '\t')
                base++;
            if(strcmp(base, "char") == 0) {
                *close = '\0';
                snprintf(dst, dst_size, "[%s]byte", bound);
                return 1;
            }
            if(strcmp(base, "char*") == 0 || strcmp(base, "string") == 0) {
                *close = '\0';
                snprintf(dst, dst_size, "[%s]string", bound);
                return 1;
            }
            {
                char gt[ZIR_GO_NAME_MAX];

                if(go_type(base, gt, sizeof(gt))) {
                    *close = '\0';
                    snprintf(dst, dst_size, "[%s]%s", bound, gt);
                    return 1;
                }
            }
        }
        return 0;
    }
    if(t[0] == '*' && t[1] != '\0') {
        char gt[ZIR_GO_NAME_MAX];

        if(go_type(t + 1, gt, sizeof(gt)) && strcmp(gt, "string") != 0) {
            snprintf(dst, dst_size, "*%s", gt);
            return 1;
        }
        return 0;
    }
    for(int i = 0; map[i].c != NULL; i++) {
        if(strcmp(t, map[i].c) == 0) {
            snprintf(dst, dst_size, "%s", map[i].go);
            return 1;
        }
    }
    /* trailing '*': pointer to a mapped scalar or a module typedef */
    n = strlen(t);
    if(n > 1 && t[n - 1] == '*') {
        char base[ZIR_GO_NAME_MAX];
        char gt[ZIR_GO_NAME_MAX];

        snprintf(base, sizeof(base), "%.*s", (int)(n - 1), t);
        /* trailing spaces before the '*' */
        {
            size_t bn = strlen(base);

            while(bn > 0 && base[bn - 1] == ' ')
                base[--bn] = '\0';
        }
        if(go_type(base, gt, sizeof(gt)) && strcmp(gt, "string") != 0) {
            snprintf(dst, dst_size, "*%s", gt);
            return 1;
        }
        /* Native contracts and declared module types retain their names. */
        {
            int identish = base[0] != '\0';

            for(char *c = base; *c != '\0'; c++)
                if(!is_ident_char((unsigned char)*c))
                    identish = 0;
            if(identish && FindType(type_scope, base, NULL) != NULL) {
                snprintf(dst, dst_size, "*%s", base);
                return 1;
            }
        }
        return 0;
    }
    /* A name alone is not evidence that a type exists. */
    {
        int identish = t[0] != '\0';

        for(char *c = t; *c != '\0'; c++)
            if(!is_ident_char((unsigned char)*c))
                identish = 0;
        if(identish && FindType(type_scope, t, NULL) != NULL) {
            snprintf(dst, dst_size, "%s", t);
            return 1;
        }
    }
    return 0;
}

static void
require_go_type(const char *type, char *dst, size_t size, ZirSourceSpan span)
{
    if(go_type(type, dst, size))
        return;
    Diagnostic(span, "zir_go.type", "unsupported or unresolved Go type: %s", type);
    exit(1);
}

static int
state_field_index(const ZirModule *m, const char *name, size_t len)
{
    for(int i = 0; i < m->state_count; i++) {
        if(strlen(m->state_fields[i].name) == len &&
           strncmp(m->state_fields[i].name, name, len) == 0)
            return i;
    }
    return -1;
}

static int
module_fn_index(const ZirModule *m, const char *name, size_t len)
{
    for(int i = 0; i < m->function_count; i++) {
        if(strlen(m->functions[i].name) == len &&
           strncmp(m->functions[i].name, name, len) == 0)
            return i;
    }
    return -1;
}

typedef struct {
    char kry[ZIR_GO_NAME_MAX];
    char go[ZIR_GO_NAME_MAX * 2];
    char guard[ZIR_GO_NAME_MAX];
    int state_count;
    const ZirModule *module;
} ZirGoGlobalFunction;

enum { ZIR_GO_GLOBAL_FUNCTION_MAX = 4096 };

static ZirGoGlobalFunction g_functions[ZIR_GO_GLOBAL_FUNCTION_MAX];
static int g_function_count;

static int
go_global_function_index(const ZirModule *module, const char *name, size_t len)
{
    char ident[ZIR_NAME_MAX];
    const ZirModule *owner = NULL;
    const ZirFunction *function = NULL;
    if(len >= sizeof(ident))
        return -1;
    memcpy(ident, name, len);
    ident[len] = '\0';
    if(ResolveFunction(module, ident, &owner, &function) != 1)
        return -1;
    for(int i = 0; i < g_function_count; i++) {
        if(g_functions[i].module == owner && strcmp(g_functions[i].kry, ident) == 0)
            return i;
    }
    return -1;
}

static void
go_build_global_functions(const ZirProgram *const *progs, int prog_count)
{
    g_function_count = 0;
    for(int pi = 0; pi < prog_count; pi++) {
        const ZirProgram *prog = progs[pi];

        for(int mi = 0; mi < prog->module_count; mi++) {
            const ZirModule *m = &prog->modules[mi];
            char stem[ZIR_PATH_MAX];
            char guard[ZIR_GO_NAME_MAX];

            stem_from_source(m->source_path, stem, sizeof(stem));
            camel_ident(stem, guard, sizeof(guard));
            for(int fi = 0; fi < m->function_count; fi++) {
                const ZirFunction *fn = &m->functions[fi];
                char fname[ZIR_GO_NAME_MAX];

                if(fn->is_extern || g_function_count >= ZIR_GO_GLOBAL_FUNCTION_MAX)
                    continue;
                camel_ident(fn->name, fname, sizeof(fname));
                snprintf(g_functions[g_function_count].kry,
                         sizeof(g_functions[0].kry), "%s", fn->name);
                snprintf(g_functions[g_function_count].guard,
                         sizeof(g_functions[0].guard), "%s", guard);
                snprintf(g_functions[g_function_count].go,
                         sizeof(g_functions[0].go), "%s_%s", guard, fname);
                g_functions[g_function_count].state_count = m->state_count;
                g_functions[g_function_count].module = m;
                g_function_count++;
            }
        }
    }
}

/* ------------------------------------------------ module lowering context */

static int split_top(const char *s, char parts[][ZIR_GO_TEXT_MAX], int max);

static int
go_is_go_elided_lifecycle(const char *text)
{
    const char *p = skip_ws(text);

    return strncmp(p, "BeginTree", 9) == 0 && skip_ws(p + 9)[0] == '('
        ? 1
        : (strncmp(p, "EndTree", 7) == 0 && skip_ws(p + 7)[0] == '(');
}

/* One '#extern' declaration bridged to a Go host method. */
typedef struct {
    char kry[ZIR_GO_NAME_MAX];        /* kry call name */
    char go[ZIR_GO_NAME_MAX];         /* Host interface method */
    char go_import_path[ZIR_PATH_MAX];  /* direct Go package import path */
    char go_import_alias[ZIR_GO_NAME_MAX]; /* import alias for direct calls */
    char pnames[ZIR_GO_EXTERN_PARAM_MAX][ZIR_GO_NAME_MAX];  /* parameter names */
    char ptypes[ZIR_GO_EXTERN_PARAM_MAX][ZIR_GO_NAME_MAX];  /* parameter kry types */
    int pcount;
    char ret[ZIR_GO_NAME_MAX];
    char host_var[ZIR_GO_NAME_MAX + 8];   /* guard-prefixed host var */
    int direct_go;
} ZirGoExtern;

/* One enum member visible to expressions (bare kry name -> qualified Go const). */
typedef struct {
    char kry[ZIR_GO_NAME_MAX];
    char go[ZIR_GO_NAME_MAX * 2];
    char val[ZIR_GO_TEXT_MAX];        /* explicit value text, or "" */
} ZirGoEnumMember;

typedef struct {
    char go_type[ZIR_GO_NAME_MAX];    /* "" for anonymous '#enum' blocks */
    char prefix[ZIR_GO_NAME_MAX];     /* enum-name prefix for Go const names */
    ZirGoEnumMember members[64];
    int count;
} ZirGoEnum;

/* Lowering is single-threaded and per-module sequential: one cached context. */
static const ZirModule *g_mod;
static char g_guard[ZIR_GO_NAME_MAX];
static ZirGoExtern g_externs[64];
static int g_extern_count;
static ZirGoEnum g_enums[32];
static int g_enum_count;
static ZirGoEnumMember *g_const_table[512];
static int g_const_count;

static int
go_extern_index(const char *name, size_t len)
{
    for(int i = 0; i < g_extern_count; i++) {
        if(strlen(g_externs[i].kry) == len &&
           strncmp(g_externs[i].kry, name, len) == 0)
            return i;
    }
    return -1;
}

static ZirGoEnumMember *
go_const_entry(const char *name, size_t len)
{
    for(int i = 0; i < g_const_count; i++) {
        if(strlen(g_const_table[i]->kry) == len &&
           strncmp(g_const_table[i]->kry, name, len) == 0)
            return g_const_table[i];
    }
    return NULL;
}

/* "a: int, b: char*" -> parameter names/types on ex. */
static void
split_params(const char *args, ZirGoExtern *ex)
{
    char parts[ZIR_GO_EXTERN_PARAM_MAX][ZIR_GO_TEXT_MAX];
    int n = split_top(args, parts, ZIR_GO_EXTERN_PARAM_MAX);

    ex->pcount = 0;
    for(int i = 0; i < n; i++) {
        char *colon = strchr(parts[i], ':');
        size_t nl;

        if(colon == NULL)
            continue;
        nl = (size_t)(colon - parts[i]);
        while(nl > 0 && parts[i][nl - 1] == ' ')
            nl--;
        if(nl == 0 || ex->pcount >= ZIR_GO_EXTERN_PARAM_MAX)
            continue;
        snprintf(ex->pnames[ex->pcount], ZIR_GO_NAME_MAX, "%.*s", (int)nl,
                 parts[i]);
        {
            const char *pt = colon + 1;

            while(*pt == ' ' || *pt == '\t')
                pt++;
            snprintf(ex->ptypes[ex->pcount], ZIR_GO_NAME_MAX, "%s", pt);
        }
        ex->pcount++;
    }
}

/* Extract the Go method name from a '#extern "pkg.Fn"' target: the segment
 * after the last dot. */
static void
extern_go_name(const char *target, const char *kry, char *dst, size_t dst_size)
{
    const char *dot = strrchr(target, '.');
    const char *base = dot != NULL ? dot + 1 : target;

    if(base[0] != '\0') {
        snprintf(dst, dst_size, "%s", base);
        for(char *c = dst; *c != '\0'; c++)
            if(!is_ident_char((unsigned char)*c))
                *c = '_';
    } else {
        camel_ident(kry, dst, dst_size);
    }
}

/* A fully-qualified Go extern target uses an import path plus function name,
 * e.g. '#extern "github.com/waozixyz/pass.Generate"'. Short targets like
 * 'smoke.QueryJobs' intentionally keep the historical host-interface bridge. */
static int
extern_direct_go_target(const char *target, char *import_path,
                        size_t import_path_size, char *alias,
                        size_t alias_size)
{
    const char *dot;
    const char *slash;
    const char *base;
    size_t n = 0;

    if(target == NULL)
        return 0;
    dot = strrchr(target, '.');
    slash = strrchr(target, '/');
    if(dot == NULL || slash == NULL || slash > dot)
        return 0;
    snprintf(import_path, import_path_size, "%.*s", (int)(dot - target),
             target);
    base = slash + 1;
    while(base < dot && n + 1 < alias_size) {
        char c = *base++;

        if(isalnum((unsigned char)c) || c == '_')
            alias[n++] = c;
        else
            alias[n++] = '_';
    }
    if(n == 0 && alias_size > 1)
        alias[n++] = 'x';
    if(isdigit((unsigned char)alias[0]) && n + 1 < alias_size) {
        memmove(alias + 1, alias, n + 1);
        alias[0] = 'x';
        n++;
    }
    alias[n] = '\0';
    if(strcmp(alias, ZIR_GO_RUNTIME_PKG) == 0 && n + 2 < alias_size)
        snprintf(alias + n, alias_size - n, "pkg");
    return import_path[0] != '\0';
}

/* Register one extern (idempotent: first declaration wins). */
static void
add_extern(const char *kry, const char *args, const char *ret,
           const char *target, ZirSourceSpan span)
{
    ZirGoExtern *ex;
    if(target && !strncmp(target, "c.", 2)) {
        fprintf(stderr, "%s:%d:%d: native Go cannot import a C ABI symbol: %s; use a Go package or host interface\n", span.path, span.line, span.column, target);
        exit(1);
    }

    if(go_extern_index(kry, strlen(kry)) >= 0 || g_extern_count >= 64)
        return;
    ex = &g_externs[g_extern_count++];
    memset(ex, 0, sizeof(*ex));
    snprintf(ex->kry, sizeof(ex->kry), "%s", kry);
    snprintf(ex->ret, sizeof(ex->ret), "%s", ret);
    split_params(args, ex);
    extern_go_name(target, kry, ex->go, sizeof(ex->go));
    ex->direct_go = extern_direct_go_target(target, ex->go_import_path,
                                            sizeof(ex->go_import_path),
                                            ex->go_import_alias,
                                            sizeof(ex->go_import_alias));
    if(ex->direct_go &&
       strcmp(ex->go_import_path, ZIR_GO_RUNTIME_IMPORT) == 0)
        snprintf(ex->go_import_alias, sizeof(ex->go_import_alias), "%s",
                 ZIR_GO_RUNTIME_PKG);
    /* guard "KryApp" -> host var "kryAppHost" */
    snprintf(ex->host_var, sizeof(ex->host_var), "%c%sHost",
             (char)tolower((unsigned char)g_guard[0]), g_guard + 1);
}

/* Parse "name :: (args) -> ret #extern \"pkg.Fn\"" from a raw extern import
 * line (the ZirImport.signature keeps the whole declaration). */
static void
parse_extern_import(const ZirImport *imp)
{
    char args[ZIR_GO_TEXT_MAX];
    char ret[ZIR_GO_NAME_MAX];
    char target[ZIR_PATH_MAX];
    const char *lp = strchr(imp->signature, '(');
    const char *rp = lp != NULL ? strchr(lp, ')') : NULL;
    const char *dir = strstr(imp->signature, "#extern");

    args[0] = '\0';
    snprintf(ret, sizeof(ret), "void");
    target[0] = '\0';
    if(lp != NULL && rp != NULL && rp > lp)
        snprintf(args, sizeof(args), "%.*s", (int)(rp - lp - 1), lp + 1);
    if(rp != NULL) {
        const char *arrow = strstr(rp, "->");

        if(arrow != NULL && (dir == NULL || arrow < dir)) {
            const char *r = arrow + 2;
            size_t n = 0;

            while(*r == ' ' || *r == '\t')
                r++;
            while(*r != '\0' && *r != '#' && n + 1 < sizeof(ret))
                ret[n++] = *r++;
            while(n > 0 && (ret[n - 1] == ' ' || ret[n - 1] == '\t'))
                n--;
            ret[n] = '\0';
        }
    }
    if(imp->target[0] != '\0' && strcmp(imp->target, imp->name) != 0) {
        snprintf(target, sizeof(target), "%s", imp->target);
    } else if(dir != NULL) {
        const char *q = strchr(dir + 7, '"');

        if(q != NULL) {
            size_t n = 0;
            const char *r = q + 1;

            while(*r != '\0' && *r != '"' && n + 1 < sizeof(target))
                target[n++] = *r++;
            target[n] = '\0';
        }
    }
    add_extern(imp->name, args, ret, target, imp->span);
}

/* Parse enum body members. The parser may deliver them newline-separated or
 * comma-joined on one line ('A = 0, B, C'), so split on both. */
static void
parse_enum(const ZirType *t)
{
    ZirGoEnum *e;
    const char *p = t->body;

    if(g_enum_count >= 32)
        return;
    if(strcmp(t->name, "#enum") == 0) {
        e = &g_enums[g_enum_count++];
        memset(e, 0, sizeof(*e));
        e->prefix[0] = '\0';
    } else {
        e = &g_enums[g_enum_count++];
        memset(e, 0, sizeof(*e));
        camel_ident(t->name, e->go_type, sizeof(e->go_type));
        camel_ident(t->name, e->prefix, sizeof(e->prefix));
    }
    while(*p != '\0') {
        char line[ZIR_GO_TEXT_MAX];
        char *name, *val;
        size_t n = 0;

        /* collect one member: up to ',' or newline */
        while(*p != '\0' && *p != ',' && *p != '\n' &&
              n + 1 < sizeof(line))
            line[n++] = *p++;
        if(*p == ',' || *p == '\n')
            p++;
        line[n] = '\0';
        /* trim */
        {
            char *s = line;

            while(*s == ' ' || *s == '\t' || *s == '\r')
                s++;
            memmove(line, s, strlen(s) + 1);
        }
        {
            size_t ln = strlen(line);

            while(ln > 0 && (line[ln - 1] == ' ' || line[ln - 1] == '\t' ||
                             line[ln - 1] == '\r'))
                line[--ln] = '\0';
        }
        if(line[0] == '\0')
            continue;
        name = line;
        val = strchr(line, '=');
        if(val != NULL) {
            *val = '\0';
            val++;
            while(*val == ' ' || *val == '\t')
                val++;
        }
        {
            size_t nn = strlen(name);

            while(nn > 0 && (name[nn - 1] == ' ' || name[nn - 1] == '\t'))
                name[--nn] = '\0';
        }
        if(name[0] == '\0' || e->count >= 64)
            continue;
        ZirGoEnumMember *m = &e->members[e->count++];
        memset(m, 0, sizeof(*m));
        snprintf(m->kry, sizeof(m->kry), "%s", name);
        if(e->prefix[0] != '\0' &&
           strncmp(m->kry, e->prefix, strlen(e->prefix)) == 0)
            camel_ident(m->kry, m->go, sizeof(m->go));
        else if(e->prefix[0] != '\0')
            snprintf(m->go, sizeof(m->go), "%s%s", e->prefix, m->kry);
        else
            camel_ident(m->kry, m->go, sizeof(m->go));
        if(val != NULL)
            snprintf(m->val, sizeof(m->val), "%s", val);
        if(g_const_count < 512)
            g_const_table[g_const_count++] = m;
    }
}

static int
module_uses_identifier(const ZirModule *module, const char *name)
{
    size_t length = strlen(name);
    for(int function = 0; function < module->function_count; function++) {
        const ZirFunction *fn = &module->functions[function];
        for(int statement = -1; statement < fn->stmt_count; statement++) {
            const char *text = statement < 0 ? fn->args : fn->stmts[statement].text;
            const char *found = text;
            while((found = strstr(found, name)) != NULL) {
                if((found == text || !is_ident_char(found[-1])) &&
                   !is_ident_char(found[length]))
                    return 1;
                found += length;
            }
        }
    }
    return 0;
}

static int
is_state_reference(const char *text)
{
    size_t length = strlen(state_receiver);
    return !strncmp(text, state_receiver, length) && text[length] == '.';
}

/* (Re)build the per-module context: extern bridge + enum constants. Must run
 * before any expression or statement is emitted for the module. */
static void
go_set_module(const ZirModule *m, const char *guard)
{
    instance_receiver[0] = '\0';
    copy_text(state_receiver, sizeof(state_receiver), "st");
    for(int serial = 0; module_uses_identifier(m, state_receiver); serial++)
        snprintf(state_receiver, sizeof(state_receiver), "module_state_%d", serial);
    g_mod = m;
    snprintf(g_guard, sizeof(g_guard), "%s", guard);
    g_extern_count = 0;
    g_enum_count = 0;
    g_const_count = 0;
    for(int i = 0; i < m->import_count; i++) {
        if(m->imports[i].kind == ZIR_IMPORT_EXTERN)
            parse_extern_import(&m->imports[i]);
    }
    for(int i = 0; i < m->function_count; i++) {
        const ZirFunction *fn = &m->functions[i];

        if(!fn->is_extern)
            continue;
        if(fn->extern_target[0] != '\0')
            add_extern(fn->name, fn->args, fn->return_type,
                       fn->extern_target, fn->span);
        else
            add_extern(fn->name, fn->args, fn->return_type, "", fn->span);
    }
    for(int i = 0; i < m->type_count; i++) {
        if(m->types[i].is_enum)
            parse_enum(&m->types[i]);
    }
}

static int
go_char_ptr_type(const char *type)
{
    char t[ZIR_GO_NAME_MAX];
    size_t n;

    snprintf(t, sizeof(t), "%s", type);
    n = strlen(t);
    while(n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t'))
        t[--n] = '\0';
    while(t[0] == ' ' || t[0] == '\t')
        memmove(t, t + 1, strlen(t));
    if(strncmp(t, "const ", 6) == 0)
        memmove(t, t + 6, strlen(t + 6) + 1);
    for(size_t i = 0; t[i] != '\0'; i++) {
        if(t[i] == ' ' && t[i + 1] == '*') {
            memmove(t + i, t + i + 1, strlen(t + i));
            i = (size_t)-1;
        }
    }
    return strcmp(t, "char*") == 0 || strcmp(t, "string") == 0;
}

static int
go_expr_is_state_char_buffer(const char *expr)
{
    if(g_mod == NULL || !is_state_reference(expr))
        return 0;
    for(int i = 0; i < g_mod->state_count; i++) {
        char name[ZIR_GO_NAME_MAX];

        if(g_mod->state_fields[i].type[0] != '[' ||
           strstr(g_mod->state_fields[i].type, "char") == NULL ||
           strchr(g_mod->state_fields[i].type, '*') != NULL)
            continue;
        camel_ident(g_mod->state_fields[i].name, name, sizeof(name));
        if(strcmp(expr + strlen(state_receiver) + 1, name) == 0)
            return 1;
    }
    return 0;
}

static int
go_expr_is_char_buffer_slice(const char *expr)
{
    size_t n = strlen(expr);

    return n >= 3 && strcmp(expr + n - 3, "[:]") == 0;
}

/* Wrap a translated argument in a Go conversion for its kry parameter type,
 * so int/long/float widening across the host bridge always compiles. */
static const char *
conv_arg(const char *kry_type, const char *expr)
{
    static char buf[ZIR_GO_TEXT_MAX];
    char t[ZIR_GO_NAME_MAX];

    snprintf(t, sizeof(t), "%s", kry_type);
    {
        size_t n = strlen(t);

        while(n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t'))
            t[--n] = '\0';
    }
    if(go_char_ptr_type(t) && go_expr_is_state_char_buffer(expr))
        snprintf(buf, sizeof(buf), "%s.CString(%s[:])", ZIR_GO_RUNTIME_PKG,
                 expr);
    else if(go_char_ptr_type(t) && go_expr_is_char_buffer_slice(expr))
        snprintf(buf, sizeof(buf), "%s.CString(%s)", ZIR_GO_RUNTIME_PKG, expr);
    else if(strcmp(t, "int") == 0 || strcmp(t, "int32") == 0)
        snprintf(buf, sizeof(buf), "int32(%s)", expr);
    else if(strcmp(t, "long") == 0 || strcmp(t, "long long") == 0 ||
            strcmp(t, "size_t") == 0 || strcmp(t, "ssize_t") == 0)
        snprintf(buf, sizeof(buf), "int64(%s)", expr);
    else if(strcmp(t, "unsigned int") == 0 || strcmp(t, "uint") == 0 ||
            strcmp(t, "unsigned") == 0)
        snprintf(buf, sizeof(buf), "uint32(%s)", expr);
    else if(strcmp(t, "unsigned long") == 0)
        snprintf(buf, sizeof(buf), "uint64(%s)", expr);
    else if(strcmp(t, "short") == 0)
        snprintf(buf, sizeof(buf), "int16(%s)", expr);
    else if(strcmp(t, "float") == 0 || strcmp(t, "float32") == 0)
        snprintf(buf, sizeof(buf), "float32(%s)", expr);
    else if(strcmp(t, "double") == 0 || strcmp(t, "float64") == 0)
        snprintf(buf, sizeof(buf), "float64(%s)", expr);
    else
        snprintf(buf, sizeof(buf), "%s", expr);
    return buf;
}

/* ------------------------------------------------------- expression pass */

/* Forward */
static void tx_expr(const ZirModule *m, const char *src, char *dst,
                    size_t dst_size);

/* Locate a postfix group's end without translating its contents. Quoted
 * delimiters, including escaped quotes, must not terminate calls or indices. */
static const char *
postfix_group_end(const char *p)
{
    char open = *p;
    char close = open == '(' ? ')' : ']';
    int depth = 1;

    p++;
    while(*p != '\0' && depth > 0) {
        if(*p == '"' || *p == '\'') {
            char quote = *p++;

            while(*p != '\0' && *p != quote) {
                if(*p == '\\' && p[1] != '\0')
                    p++;
                p++;
            }
            if(*p == quote)
                p++;
            continue;
        }
        if(*p == open)
            depth++;
        else if(*p == close)
            depth--;
        p++;
    }
    return p;
}

/* Translate the inside of a braced/paren group starting after the opener;
 * returns the position after the matching closer. */
static const char *
tx_group(const ZirModule *m, const char *src, char *dst, size_t *dn,
         char open, char close)
{
    int depth = 1;
    char mid[ZIR_GO_TEXT_MAX];
    size_t mn = 0;
    const char *p = src;

    while(*p != '\0' && depth > 0 && mn + 1 < sizeof(mid)) {
        if(*p == open)
            depth++;
        else if(*p == close) {
            depth--;
            if(depth == 0)
                break;
        } else if(*p == '"' || *p == '\'') {
            char q = *p;
            mid[mn++] = *p++;
            while(*p != '\0' && *p != q && mn + 1 < sizeof(mid))
                mid[mn++] = *p++;
            if(*p == q)
                mid[mn++] = *p++;
            continue;
        }
        mid[mn++] = *p++;
    }
    mid[mn] = '\0';
    {
        char out[ZIR_GO_TEXT_MAX];

        tx_expr(m, mid, out, sizeof(out));
        if(*dn + strlen(out) + 1 < ZIR_GO_TEXT_MAX) {
            memcpy(dst + *dn, out, strlen(out));
            *dn += strlen(out);
        }
    }
    return *p == close && depth == 0 ? p + 1 : p;
}

/* Split top-level (depth-0) comma parts. */
static int
split_top(const char *s, char parts[][ZIR_GO_TEXT_MAX], int max)
{
    int depth = 0, n = 0;
    const char *start = s;

    for(const char *p = s; ; p++) {
        if(*p == '\0' || (*p == ',' && depth == 0)) {
            size_t len = (size_t)(p - start);

            if(n < max && len < ZIR_GO_TEXT_MAX) {
                memcpy(parts[n], start, len);
                parts[n][len] = '\0';
                n++;
            }
            if(*p == '\0')
                break;
            start = p + 1;
        } else if(*p == '(' || *p == '[' || *p == '{')
            depth++;
        else if(*p == ')' || *p == ']' || *p == '}')
            depth--;
    }
    return n;
}

/* "(Vector2){a,b}" style compound literal: p points after "(". */
/* C field order for the Props/Spec types .kry writes positionally, e.g.
 * Image((ImageProps){"path", ...}). Designated initializers do not need
 * this table; positional parts index into it. Names are the Go field names. */
static void
resolve_slot_type(void *context, const char *source, char *out, size_t size)
{
    (void)context;
    if(!go_type(source, out, size)) {
        fprintf(stderr, "unsupported slot parameter type: %s\n", source);
        exit(1);
    }
}

static int
source_record(const ZirModule *module, const char *name)
{
    const ZirType *record = FindType(module, name, NULL);
    return record != NULL && !record->is_enum;
}

static int
props_field_at(const ZirModule *module, const char *type, int index,
               char *name, size_t name_size)
{
    const ZirType *record = FindType(module, type, NULL);
    if(record != NULL && !record->is_enum) {
        size_t offset = 0;
        ZirTypeField field;
        int position = 0;

        while(TypeNextField(record, &offset, &field) == 1) {
            if(position++ == index) {
                go_field_ident(field.name, name, name_size);
                return 1;
            }
        }
        return 0;
    }

    return 0;
}

/* .kry array variables ('name: [N] T' state or local): references used in
 * slice-typed Go fields append '[:]' so one .kry table/list definition can
 * feed both generated C arrays and generated Go slices. */
static char zir_go_array_names[24][ZIR_GO_NAME_MAX];
static int zir_go_array_count;

static void go_trim_ws(char *s);

static void
go_register_arrays_module(const ZirModule *m)
{
    int i;

    for(i = 0; i < m->state_count && zir_go_array_count < 24; i++) {
        const ZirStateField *sf = &m->state_fields[i];

        if(sf->type[0] == '[') {
            snprintf(zir_go_array_names[zir_go_array_count], ZIR_NAME_MAX, "%s",
                     sf->name);
            zir_go_array_count++;
        }
    }
}

static void
go_register_arrays_stmt(const char *text)
{
    const char *t = text;
    const char *colon;
    const char *eq;

    while(*t == ' ' || *t == '\t')
        t++;
    colon = strchr(t, ':');
    eq = colon != NULL ? strchr(colon, '=') : NULL;
    if(colon == NULL || eq == NULL || zir_go_array_count >= 24)
        return;
    {
        char decl_type[ZIR_NAME_MAX];
        size_t tl = (size_t)(eq - colon - 1);

        if(tl >= sizeof(decl_type))
            tl = sizeof(decl_type) - 1;
        memcpy(decl_type, colon + 1, tl);
        decl_type[tl] = '\0';
        go_trim_ws(decl_type);
        if(decl_type[0] != '[')
            return;
    }
    {
        size_t nl = (size_t)(colon - t);

        if(nl > 0 && nl < ZIR_NAME_MAX) {
            memcpy(zir_go_array_names[zir_go_array_count], t, nl);
            zir_go_array_names[zir_go_array_count][nl] = '\0';
            zir_go_array_count++;
        }
    }
}

static void
go_trim_ws(char *s)
{
    size_t n;

    while(*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                    s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

static void
go_register_arrays_args(const char *args)
{
    char parts[32][ZIR_GO_TEXT_MAX];
    int n, i;

    if(args[0] == '\0')
        return;
    n = split_top(args, parts, 32);
    for(i = 0; i < n && zir_go_array_count < 24; i++) {
        char *colon = strchr(parts[i], ':');
        char *name = parts[i];
        size_t al;
        char atype[ZIR_GO_NAME_MAX];

        if(colon == NULL)
            continue;
        snprintf(atype, sizeof(atype), "%s", colon + 1);
        go_trim_ws(atype);
        if(atype[0] != '[')
            continue;
        while(*name == ' ' || *name == '\t')
            name++;
        al = (size_t)(colon - name);
        while(al > 0 && (name[al - 1] == ' ' || name[al - 1] == '\t'))
            al--;
        if(al == 0 || al >= ZIR_GO_NAME_MAX)
            continue;
        memcpy(zir_go_array_names[zir_go_array_count], name, al);
        zir_go_array_names[zir_go_array_count][al] = '\0';
        zir_go_array_count++;
    }
}

static int
go_is_array_name(const char *ident, size_t len)
{
    int i;

    for(i = 0; i < zir_go_array_count; i++)
        if(strlen(zir_go_array_names[i]) == len &&
           strncmp(zir_go_array_names[i], ident, len) == 0)
            return 1;
    return 0;
}

/* Go Props fields that are bool while .kry writes C ints (0/1). */
static int
bool_prop_field(const char *field)
{
    static const char *names[] = {"Disabled", "DrawMenu", "Active",
                                  "Secure", "Closeable", "Italic",
                                  "FocusSelected", "Resizable",
                                  "SeparatorBefore", "HasLeadingAction",
                                  "HasDropdown", "Vertical", "Angle", NULL};
    int i;

    for(i = 0; names[i] != NULL; i++)
        if(strcmp(names[i], field) == 0)
            return 1;
    return 0;
}

static int
slice_prop_field(const char *type, const char *field)
{
    if(strcmp(type, "DropdownProps") == 0 &&
       (strcmp(field, "Options") == 0 || strcmp(field, "Items") == 0))
        return 1;
    if(strcmp(type, "SegmentedControlProps") == 0 &&
       strcmp(field, "Options") == 0)
        return 1;
    if(strcmp(type, "ButtonProps") == 0 && strcmp(field, "Items") == 0)
        return 1;
    if(strcmp(type, "ListBoxProps") == 0 &&
       (strcmp(field, "Items") == 0 || strcmp(field, "Selected") == 0 || strcmp(field, "ItemKeys") == 0))
        return 1;
    if(strcmp(type, "TreeViewProps") == 0 && strcmp(field, "Items") == 0)
        return 1;
    if(strcmp(type, "NavigationBarProps") == 0 && strcmp(field, "Items") == 0)
        return 1;
    if(strcmp(type, "TabBarProps") == 0 && strcmp(field, "Tabs") == 0)
        return 1;
    if(strcmp(type, "BottomIconRowProps") == 0 && strcmp(field, "Items") == 0)
        return 1;
    if(strcmp(type, "ToolbarProps") == 0 &&
       (strcmp(field, "Options") == 0 || strcmp(field, "Actions") == 0))
        return 1;
    if(strcmp(type, "ModalProps") == 0 &&
       (strcmp(field, "Actions") == 0 || strcmp(field, "Text") == 0))
        return 1;
    if(strcmp(type, "DragDropProps") == 0 &&
       (strcmp(field, "Data") == 0 || strcmp(field, "Output") == 0))
        return 1;
    if(strcmp(type, "PlotProps") == 0 && strcmp(field, "Values") == 0)
        return 1;
    if(strcmp(type, "DragProps") == 0 &&
       (strcmp(field, "FloatValues") == 0 || strcmp(field, "IntValues") == 0))
        return 1;
    if(strcmp(type, "SliderProps") == 0 &&
       (strcmp(field, "FloatValues") == 0 || strcmp(field, "IntValues") == 0))
        return 1;
    if(strcmp(type, "InputProps") == 0 &&
       (strcmp(field, "FloatValues") == 0 || strcmp(field, "IntValues") == 0 ||
        strcmp(field, "DoubleValues") == 0))
        return 1;
    if(strcmp(type, "ColorPickerProps") == 0 && strcmp(field, "Values") == 0)
        return 1;
    if(strcmp(type, "MenuGroup") == 0 && strcmp(field, "Items") == 0)
        return 1;
    if(strcmp(type, "MenuItem") == 0 && strcmp(field, "Submenu") == 0)
        return 1;
    if(strcmp(type, "MenuProps") == 0 &&
       (strcmp(field, "Items") == 0 || strcmp(field, "Menus") == 0))
        return 1;
    if(strcmp(type, "RouterProps") == 0 && strcmp(field, "Routes") == 0)
        return 1;
    if(strcmp(type, "TableViewProps") == 0 &&
       (strcmp(field, "Columns") == 0 || strcmp(field, "Rows") == 0 ||
        strcmp(field, "ColumnWidths") == 0 ||
        strcmp(field, "ColumnEnabled") == 0 ||
        strcmp(field, "ColumnOrder") == 0))
        return 1;
    if(strcmp(type, "TableRow") == 0 && strcmp(field, "Cells") == 0)
        return 1;
    return 0;
}

static int
go_array_element_type(const char *gt, char *elem, size_t elem_size)
{
    const char *close;
    const char *base;

    if(gt[0] != '[')
        return 0;
    close = strchr(gt, ']');
    if(close == NULL)
        return 0;
    base = close + 1;
    while(*base == ' ' || *base == '\t')
        base++;
    snprintf(elem, elem_size, "%s", base);
    if(strncmp(elem, ZIR_GO_RUNTIME_PKG ".", strlen(ZIR_GO_RUNTIME_PKG) + 1) == 0)
        memmove(elem, elem + strlen(ZIR_GO_RUNTIME_PKG) + 1,
                strlen(elem + strlen(ZIR_GO_RUNTIME_PKG) + 1) + 1);
    return elem[0] != '\0';
}

static void
go_collapse_duplicate_slices(char *s)
{
    char *p;

    while((p = strstr(s, "[:][:]")) != NULL)
        memmove(p + 3, p + 6, strlen(p + 6) + 1);
}

static int
go_translate_array_literal(const ZirModule *m, const char *gt,
                            const char *init, char *dst, size_t dst_size)
{
    char elem[ZIR_GO_NAME_MAX];
    char raw[ZIR_GO_TEXT_MAX];
    char parts[64][ZIR_GO_TEXT_MAX];
    const char *q;
    size_t rn = 0;
    size_t dn = 0;
    int depth = 1;
    int count;

    if(!go_array_element_type(gt, elem, sizeof(elem)))
        return 0;
    init = skip_ws(init);
    if(*init != '{')
        return 0;
    q = init + 1;
    while(*q != '\0' && depth > 0 && rn + 1 < sizeof(raw)) {
        if(*q == '"') {
            raw[rn++] = *q++;
            while(*q != '\0' && *q != '"' && rn + 1 < sizeof(raw))
                raw[rn++] = *q++;
            if(*q == '"')
                raw[rn++] = *q++;
            continue;
        }
        if(*q == '{')
            depth++;
        else if(*q == '}') {
            depth--;
            if(depth == 0)
                break;
        }
        raw[rn++] = *q++;
    }
    if(depth != 0)
        return 0;
    raw[rn] = '\0';
    count = split_top(raw, parts, 64);
    dn += (size_t)snprintf(dst + dn, dst_size - dn, "%s{", gt);
    for(int i = 0; i < count; i++) {
        const char *part = skip_ws(parts[i]);
        char value[ZIR_GO_TEXT_MAX];

        if(i > 0)
            dn += (size_t)snprintf(dst + dn, dst_size - dn, ",");
        if(*part == '{' && elem[0] != '[') {
            char typed[ZIR_GO_TEXT_MAX];

            snprintf(typed, sizeof(typed), "(%s)%s", elem, part);
            tx_expr(m, typed, value, sizeof(value));
        } else {
            tx_expr(m, part, value, sizeof(value));
        }
        go_collapse_duplicate_slices(value);
        dn += (size_t)snprintf(dst + dn, dst_size - dn, "%s", value);
    }
    snprintf(dst + dn, dst_size - dn, "}");
    return 1;
}

/* Legacy host props still accept integer flags. Expressions that already
 * produce bool must not acquire a second integer-to-bool conversion. */
static int
boolean_expression(const ZirModule *module, const char *source)
{
    ZirFunction parsed = {0};
    int root = ParseExpr(&parsed, module, source, Span(module->source_path, 1, 1));
    int result = 0;
    if(root >= 0) {
        const ZirExpr *expr = &parsed.exprs[root];
        result = (expr->kind == ZIR_EXPR_IDENT &&
                  (!strcmp(expr->name, "true") || !strcmp(expr->name, "false"))) ||
                 (expr->kind == ZIR_EXPR_CAST && !strcmp(expr->name, "bool")) ||
                 (expr->kind == ZIR_EXPR_UNARY && !strcmp(expr->op, "!")) ||
                 (expr->kind == ZIR_EXPR_BINARY &&
                  (!strcmp(expr->op, "==") || !strcmp(expr->op, "!=") ||
                   !strcmp(expr->op, "<") || !strcmp(expr->op, ">") ||
                   !strcmp(expr->op, "<=") || !strcmp(expr->op, ">=") ||
                   !strcmp(expr->op, "&&") || !strcmp(expr->op, "||")));
        if(!result && expr->kind == ZIR_EXPR_IDENT) {
            int field = state_field_index(module, expr->name,
                                          strlen(expr->name));
            result = field >= 0 &&
                     !strcmp(ScalarType(module->state_fields[field].type),
                             "bool");
        }
    }
    free(parsed.exprs);
    return result;
}

static const char *
tx_compound(const ZirModule *m, const char *p, char *dst, size_t *dn)
{
    char type[ZIR_GO_NAME_MAX];
    char first_field[ZIR_GO_NAME_MAX];
    size_t tn = 0;

    while(*p != '\0' && *p != ')' && tn + 1 < sizeof(type))
        type[tn++] = *p++;
    if(*p != ')')
        return p;
    p++;
    type[tn] = '\0';
    while(tn > 0 && (type[tn - 1] == ' ' || type[tn - 1] == '\t'))
        type[--tn] = '\0';

    if(*p == '{') {
        char qtype[ZIR_GO_NAME_MAX * 2];

        snprintf(qtype, sizeof(qtype), "%s", type);
        p++;
        /* Props/Spec use C designated initializers. Translate them to named
         * Go fields and give the untyped bounds literal its Rectangle type. */
        int declared_record = source_record(m, type);
        if(declared_record || strstr(type, "Props") != NULL ||
           strstr(type, "Spec") != NULL ||
           props_field_at(m, type, 0, first_field, sizeof(first_field))) {
            char raw[ZIR_GO_TEXT_MAX], parts[32][ZIR_GO_TEXT_MAX];
            size_t rn = 0;
            int depth = 1;
            const char *q = p;
            int count;

            while(*q != '\0' && depth > 0 && rn + 1 < sizeof(raw)) {
                if(*q == '{')
                    depth++;
                else if(*q == '}') {
                    depth--;
                    if(depth == 0)
                        break;
                }
                raw[rn++] = *q++;
            }
            raw[rn] = '\0';
            p = *q == '}' ? q + 1 : q;
            count = split_top(raw, parts, 32);
            *dn += (size_t)snprintf(dst + *dn, ZIR_GO_TEXT_MAX - *dn,
                                    "%s{", qtype);
            for(int i = 0, emitted = 0, positional = 0; i < count; i++) {
                char *part = (char *)skip_ws(parts[i]);
                char *eq;
                char field[ZIR_GO_NAME_MAX];
                char value[ZIR_GO_TEXT_MAX];

                const char *source = part;
                if(*part != '.') {
                    if(*part == '\0') {
                        positional++;
                        continue;
                    }
                    if(!props_field_at(m, type, positional++, field, sizeof(field))) {
                        fprintf(stderr, "zir_go: no positional field %d in %s\n", positional, type);
                        exit(1);
                    }
                } else {
                    eq = strchr(part, '=');
                    if(eq == NULL)
                        continue;
                    *eq = '\0';
                    go_field_ident(part + 1, field, sizeof(field));
                    source = skip_ws(eq + 1);
                }
                if(strcmp(field, "TextSize") == 0 &&
                   (!declared_record ||
                    strcmp(type, "TextFieldProps") == 0 ||
                    strcmp(type, "TextAreaProps") == 0))
                    continue;
                char field_type[ZIR_NAME_MAX] = "";
                const ZirType *contract = FindType(m, type, NULL);
                ZirTypeField member;
                size_t offset = 0;
                while(contract != NULL && TypeNextField(contract, &offset, &member) == 1) {
                    char member_name[ZIR_NAME_MAX];
                    go_field_ident(member.name, member_name, sizeof(member_name));
                    if(!strcmp(member_name, field)) {
                        copy_text(field_type, sizeof(field_type), member.type);
                        break;
                    }
                }
                /* Remaining host records use their legacy geometry convention. */
                if(!*field_type && !declared_record) {
                    if(!strcmp(field, "Bounds") || !strcmp(field, "Trigger") ||
                       !strcmp(field, "Indicators"))
                        copy_text(field_type, sizeof(field_type), "Rectangle");
                    else if(!strcmp(field, "Color"))
                        copy_text(field_type, sizeof(field_type), "Color");
                }
                if(*source == '{' && *field_type) {
                    char typed[ZIR_GO_TEXT_MAX];
                    snprintf(typed, sizeof(typed), "(%s)%s", field_type, source);
                    tx_expr(m, typed, value, sizeof(value));
                } else {
                    tx_expr(m, source, value, sizeof(value));
                }
                const char *scalar = ScalarType(field_type);
                if(*scalar && strcmp(scalar, "bool") && strcmp(scalar, "string")) {
                    char scalar_type[ZIR_NAME_MAX];
                    char converted[ZIR_GO_TEXT_MAX];
                    if(go_type(field_type, scalar_type, sizeof(scalar_type))) {
                        snprintf(converted, sizeof(converted), "%s(%s)", scalar_type, value);
                        copy_text(value, sizeof(value), converted);
                    }
                }
                if((strcmp(type, "TextFieldProps") == 0 ||
                    strcmp(type, "TextAreaProps") == 0) &&
                   strcmp(field, "Text") == 0 && is_state_reference(value))
                    strncat(value, "[:]", sizeof(value) - strlen(value) - 1);
                if(strcmp(type, "MenuItem") == 0 &&
                   (strcmp(field, "Label") == 0 ||
                    strcmp(field, "Accelerator") == 0) &&
                   strcmp(value, "nil") == 0)
                    snprintf(value, sizeof(value), "\"\"");
                if(slice_prop_field(type, field) && strcmp(value, "nil") != 0 &&
                   !go_expr_is_char_buffer_slice(value))
                    strncat(value, "[:]", sizeof(value) - strlen(value) - 1);
                go_collapse_duplicate_slices(value);
                if(emitted++)
                    *dn += (size_t)snprintf(dst + *dn, ZIR_GO_TEXT_MAX - *dn, ", ");
                if(!declared_record && ((!strcmp(field_type, "bool") ||
                    (!*field_type && bool_prop_field(field))) ||
                    (strcmp(type, "TextProps") == 0 &&
                     strcmp(field, "Selectable") == 0) ||
                    (strcmp(type, "TableViewProps") == 0 && strcmp(field, "CustomCells") == 0) ||
                    (strcmp(type, "CollapsibleProps") == 0 &&
                     (strcmp(field, "Tree") == 0 || strcmp(field, "Leaf") == 0 ||
                      strcmp(field, "Selected") == 0)) ||
                    (strcmp(type, "MenuItem") == 0 &&
                     strcmp(field, "Checked") == 0)) &&
                   !boolean_expression(m, source))
                    *dn += (size_t)snprintf(dst + *dn, ZIR_GO_TEXT_MAX - *dn,
                                            "%s: (%s != 0)", field, value);
                else
                    *dn += (size_t)snprintf(dst + *dn, ZIR_GO_TEXT_MAX - *dn,
                                            "%s: %s", field, value);
            }
            if(*dn + 2 < ZIR_GO_TEXT_MAX)
                dst[(*dn)++] = '}';
            return p;
        }
        /* other struct literals: Type{...} — recurse and keep braces */
        {
            char ctor[ZIR_GO_NAME_MAX + 8];

            snprintf(ctor, sizeof(ctor), "%s{", qtype);
            if(*dn + strlen(ctor) + 1 < ZIR_GO_TEXT_MAX) {
                memcpy(dst + *dn, ctor, strlen(ctor));
                *dn += strlen(ctor);
            }
            p = tx_group(m, p, dst, dn, '{', '}');
            if(*dn + 2 < ZIR_GO_TEXT_MAX)
                dst[(*dn)++] = '}';
            return p;
        }
    }
    /* plain cast "(T)expr" */
    {
        char gt[ZIR_GO_NAME_MAX];

        if(strcmp(type, "char*") == 0 || strcmp(type, "const char*") == 0) {
            /* string cast: drop it, translate the operand below */
            return p;
        }
        if(go_type(type, gt, sizeof(gt)) && gt[0] != '\0') {
            if(*dn + strlen(gt) + 1 < ZIR_GO_TEXT_MAX) {
                memcpy(dst + *dn, gt, strlen(gt));
                *dn += strlen(gt);
            }
            return p; /* caller emits the operand as the cast argument;
                         Go cast syntax is T(operand), so open a paren */
        }
        return p; /* unknown cast: drop */
    }
}

static int
is_module_constant(const ZirModule *module, const char *name)
{
    for(int pass = 0; pass < 2; pass++) {
        int count = pass == 0 ? 1 : module->import_count;
        for(int i = 0; i < count; i++) {
            const ZirModule *scope = pass == 0 ? module : module->imports[i].resolved_module;
            if(scope == NULL)
                continue;
            for(int j = 0; j < scope->define_count; j++) {
                if(!strcmp(scope->defines[j].name, name))
                    return 1;
            }
        }
    }
    return 0;
}

static void
tx_expr(const ZirModule *m, const char *src, char *dst, size_t dst_size)
{
    size_t dn = 0;
    const char *p = src;
    char out[ZIR_GO_TEXT_MAX];

    if(dst_size > ZIR_GO_TEXT_MAX)
        dst_size = ZIR_GO_TEXT_MAX;
    while(*p != '\0' && dn + 8 < dst_size) {
        if(*p == ';' || *p == '\n' || *p == '\r') {
            p++;
            continue;
        }
        if(*p == '"') { /* string literal, verbatim */
            dst[dn++] = *p++;
            while(*p != '\0' && *p != '"' && dn + 2 < dst_size)
                dst[dn++] = *p++;
            if(*p == '"')
                dst[dn++] = *p++;
            continue;
        }
        /* cast / compound literal: '(' ident ')' */
        if(*p == '(' &&
           (p == src || (!is_ident_char((unsigned char)p[-1]) &&
                         p[-1] != ')' && p[-1] != ']'))) {
            const char *q = p + 1;
            size_t tl = 0;

            if(!isalpha((unsigned char)*q) && *q != '_')
                q = p; /* numeric or expression: not a cast */
            else
                while(is_ident_char((unsigned char)*q) || *q == ' ' || *q == '*')
                    q++;
            tl = (size_t)(q - (p + 1));
            if(*q == ')' && tl > 0 && tl < sizeof(char) * ZIR_GO_NAME_MAX) {
                char maybe[ZIR_GO_NAME_MAX];
                int identish = 1;
                const char *after = skip_ws(q + 1);

                /* A cast needs an operand. A grouped argument at the end of
                 * a call is not a type, even if it is a lone identifier. */
                if(*after == '\0' || *after == ')' || *after == ',' || *after == '}')
                    identish = 0;

                memcpy(maybe, p + 1, tl < ZIR_GO_NAME_MAX - 1 ? tl : ZIR_GO_NAME_MAX - 1);
                maybe[tl < ZIR_GO_NAME_MAX - 1 ? tl : ZIR_GO_NAME_MAX - 1] = '\0';
                for(char *c = maybe; *c != '\0'; c++)
                    if(!is_ident_char((unsigned char)*c) && *c != ' ' && *c != '*')
                        identish = 0;
                if(identish) {
                    p = tx_compound(m, p + 1, dst, &dn);
                    /* Go conversions wrap one operand, including its
                     * postfix operations, rather than the remaining binary
                     * expression. Compound literals are already complete. */
                    if(*(p - 1) == ')' && strchr(maybe, '{') == NULL) {
                        /* Scalar casts bind after postfix calls, indexing,
                         * and member access, but before binary operators. */
                        const char *op = skip_ws(p);
                        char primary[ZIR_GO_TEXT_MAX];
                        size_t pn = 0;

                        if(dn + 1 < dst_size)
                            dst[dn++] = '(';
                        if(*op == '(') {
                            const char *after;

                            dst[dn++] = '(';
                            after = tx_group(m, op + 1, dst, &dn, '(', ')');
                            if(dn + 1 < dst_size)
                                dst[dn++] = ')';
                            p = after;
                        } else if(is_ident_char((unsigned char)*op)) {
                            const char *start = op;

                            while(is_ident_char((unsigned char)*op))
                                op++;
                            for(;;) {
                                const char *next = skip_ws(op);

                                if(*next == '[' || *next == '(') {
                                    op = postfix_group_end(next);
                                } else if(*next == '.' &&
                                          is_ident_char((unsigned char)next[1])) {
                                    op = next + 1;
                                    while(is_ident_char((unsigned char)*op))
                                        op++;
                                } else {
                                    break;
                                }
                            }
                            pn = (size_t)(op - start);
                            if(pn >= sizeof(primary))
                                pn = sizeof(primary) - 1;
                            memcpy(primary, start, pn);
                            primary[pn] = '\0';
                            p = op;
                            {
                                char po[ZIR_GO_TEXT_MAX];

                                tx_expr(m, primary, po, sizeof(po));
                                size_t pl = strlen(po);
                                if(dn + pl + 1 < dst_size) {
                                    memcpy(dst + dn, po, pl);
                                    dn += pl;
                                }
                            }
                        }
                        if(dn + 1 < dst_size)
                            dst[dn++] = ')';
                    }
                    continue;
                }
            }
            dst[dn++] = *p++;
            p = tx_group(m, p, dst, &dn, '(', ')');
            if(dn + 1 < dst_size)
                dst[dn++] = ')';
            continue;
        }
        if(*p == '{') {
            dst[dn++] = *p++;
            p = tx_group(m, p, dst, &dn, '{', '}');
            if(dn + 1 < dst_size)
                dst[dn++] = '}';
            continue;
        }
        /* NULL -> nil */
        if(strncmp(p, "NULL", 4) == 0 && !is_ident_char((unsigned char)p[4])) {
            const char *r = "nil";
            if(dn + 4 < dst_size) {
                memcpy(dst + dn, r, 3);
                dn += 3;
            }
            p += 4;
            continue;
        }
        /* 1.0f -> 1.0 */
        if(isdigit((unsigned char)*p)) {
            const char *q = p;
            const char *num_end;

            if(*q == '0' && (q[1] == 'x' || q[1] == 'X')) {
                q += 2;
                while(isxdigit((unsigned char)*q))
                    q++;
            } else {
                while(isdigit((unsigned char)*q) || *q == '.')
                    q++;
                if(*q == 'e' || *q == 'E') {  /* exponent */
                    q++;
                    if(*q == '+' || *q == '-')
                        q++;
                    while(isdigit((unsigned char)*q))
                        q++;
                }
            }
            num_end = q;
            if(*q == 'f' || *q == 'F')
                q++;   /* C float suffix: drop it */
            while(p < num_end && dn + 1 < dst_size)
                dst[dn++] = *p++;
            p = q;
            continue;
        }
        if(is_ident_char((unsigned char)*p) || *p == '&') {
            int addr = *p == '&';
            const char *q = addr ? p + 1 : p;
            char ident[ZIR_GO_NAME_MAX];
            size_t il = 0;
            int sfi, fni;

            while(is_ident_char((unsigned char)*q) && il + 1 < sizeof(ident))
                ident[il++] = *q++;
            ident[il] = '\0';
            if(il == 0) {
                dst[dn++] = *p++;
                continue;
            }
            sfi = state_field_index(m, ident, il);
            if(sfi >= 0) {
                char camel_name[ZIR_GO_NAME_MAX];
                camel_ident(m->state_fields[sfi].name, camel_name, sizeof(camel_name));
                size_t needed = strlen(state_receiver) + strlen(camel_name) + 1 + (addr != 0);
                if(needed >= dst_size - dn)
                    break;
                dn += (size_t)snprintf(dst + dn, dst_size - dn, "%s%s.%s",
                                       addr ? "&" : "", state_receiver, camel_name);
                p = q;
                continue;
            }
            /* '#extern' bridge: direct Go import for fully-qualified targets,
             * otherwise the historical host-interface method. The check
             * precedes the module-function path because extern prototypes
             * also sit in the function table (bodyless). */
            {
                int xi = go_extern_index(ident, il);

                if(xi >= 0 && *skip_ws(q) == '(') {
                    char raw[ZIR_GO_TEXT_MAX];
                    const char *ap = skip_ws(q) + 1;
                    const char *ae = ap;
                    int depth = 1;
                    size_t rn = 0;
                    int all_ws = 1;

                    while(*ae != '\0' && depth > 0 && rn + 1 < sizeof(raw)) {
                        if(*ae == '"') {
                            all_ws = 0;
                            raw[rn++] = *ae++;
                            while(*ae != '\0' && *ae != '"' &&
                                  rn + 1 < sizeof(raw))
                                raw[rn++] = *ae++;
                            if(*ae == '"')
                                raw[rn++] = *ae++;
                            continue;
                        }
                        if(*ae == '(' || *ae == '[' || *ae == '{')
                            depth++;
                        else if(*ae == ')' || *ae == ']' || *ae == '}') {
                            depth--;
                            if(depth == 0)
                                break;
                        } else if(*ae != ' ' && *ae != '\t')
                            all_ws = 0;
                        raw[rn++] = *ae++;
                    }
                    raw[rn] = '\0';
                    if(*ae == ')')
                        ae++;
                    p = ae;
                    if(g_externs[xi].direct_go)
                        dn += (size_t)snprintf(dst + dn, ZIR_GO_TEXT_MAX - dn,
                                               "%s.%s(",
                                               g_externs[xi].go_import_alias,
                                               g_externs[xi].go);
                    else {
                        if(runtime_output && !*instance_receiver) {
                            fprintf(stderr, "zir_go: runtime host calls require a function receiver: %s\n", ident);
                            exit(1);
                        }
                        dn += (size_t)snprintf(dst + dn, ZIR_GO_TEXT_MAX - dn,
                                               "%s.%s(",
                                               runtime_output ? instance_receiver : g_externs[xi].host_var,
                                               g_externs[xi].go);
                    }
                    if(!all_ws) {
                        char parts[ZIR_GO_EXTERN_PARAM_MAX][ZIR_GO_TEXT_MAX];
                        int n = split_top(raw, parts, ZIR_GO_EXTERN_PARAM_MAX);

                        for(int i = 0; i < n; i++) {
                            char arg[ZIR_GO_TEXT_MAX];

                            tx_expr(m, skip_ws(parts[i]), arg, sizeof(arg));
                            if(i > 0)
                                dn += (size_t)snprintf(dst + dn,
                                                       ZIR_GO_TEXT_MAX - dn, ", ");
                            if(i < g_externs[xi].pcount) {
                                dn += (size_t)snprintf(
                                    dst + dn, ZIR_GO_TEXT_MAX - dn, "%s",
                                    conv_arg(g_externs[xi].ptypes[i], arg));
                            } else {
                                dn += (size_t)snprintf(dst + dn,
                                                       ZIR_GO_TEXT_MAX - dn, "%s",
                                                       arg);
                            }
                        }
                    }
                    if(dn + 1 < dst_size)
                        dst[dn++] = ')';
                    continue;
                }
            }
            fni = module_fn_index(m, ident, il);
            if(fni >= 0 && *skip_ws(q) == '(') {
                char fname[ZIR_GO_NAME_MAX * 2];
                size_t fl;

                {
                    char local[ZIR_GO_NAME_MAX];

                    camel_ident(m->functions[fni].name, local, sizeof(local));
                    snprintf(fname, sizeof(fname), "%s_%s", g_guard, local);
                }
                fl = strlen(fname);
                if(dn + fl + 16 < dst_size) {
                    memcpy(dst + dn, fname, fl);
                    dn += fl;
                    dst[dn++] = '(';
                }
                p = skip_ws(q) + 1;
                /* empty arg list? */
                if(m->state_count > 0) {
                    const char *separator = *skip_ws(p) == ')' ? "" : ", ";
                    if(strlen(state_receiver) + strlen(separator) >= dst_size - dn)
                        break;
                    dn += (size_t)snprintf(dst + dn, dst_size - dn, "%s%s",
                                           state_receiver, separator);
                }
                continue;
            }
            if(*skip_ws(q) == '(') {
                int gfi = go_global_function_index(m, ident, il);

                if(gfi >= 0 && strcmp(g_functions[gfi].guard, g_guard) != 0) {
                    size_t fl = strlen(g_functions[gfi].go);

                    if(dn + fl + 16 < dst_size) {
                        memcpy(dst + dn, g_functions[gfi].go, fl);
                        dn += fl;
                        dst[dn++] = '(';
                    }
                    p = skip_ws(q) + 1;
                    if(*skip_ws(p) == ')') {
                        if(g_functions[gfi].state_count > 0 &&
                           dn + strlen(g_functions[gfi].guard) + 16 < dst_size) {
                            dn += (size_t)snprintf(
                                dst + dn, dst_size - dn, "%sStateValue",
                                g_functions[gfi].guard);
                        }
                    } else if(g_functions[gfi].state_count > 0 &&
                              dn + strlen(g_functions[gfi].guard) + 18 < dst_size) {
                        dn += (size_t)snprintf(
                            dst + dn, dst_size - dn, "%sStateValue, ",
                            g_functions[gfi].guard);
                    }
                    continue;
                }
            }
            /* enum members: bare ALL_CAPS name -> qualified Go const */
            {
                const ZirModule *owner = NULL;
                const ZirType *type = NULL;
                if(ResolveEnumMember(g_mod, ident, &owner, &type) == 1 && owner != g_mod) {
                    char prefix[ZIR_GO_NAME_MAX] = "";
                    char member[ZIR_GO_NAME_MAX];
                    copy_text(member, sizeof(member), ident);
                    if(strcmp(type->name, "#enum") != 0) {
                        camel_ident(type->name, prefix, sizeof(prefix));
                        /* The definition side (parse_enum) keeps members that
                         * already carry the enum prefix flat; mirror that
                         * here so references match the emitted constants. */
                        if(!(prefix[0] != '\0' &&
                             strncmp(member, prefix, strlen(prefix)) == 0))
                            camel_ident(type->name, prefix, sizeof(prefix));
                        else
                            prefix[0] = '\0';
                    } else {
                        camel_ident(ident, member, sizeof(member));
                    }
                    dn += (size_t)snprintf(dst + dn, dst_size - dn, "%s%s", prefix, member);
                    p = q;
                    continue;
                }
                if(strlen("NumericFloat") == il &&
                   strncmp("NumericFloat", ident, il) == 0) {
                    dn += (size_t)snprintf(dst + dn, dst_size - dn,
                        "%sNumericFloat", runtime_output ? "" : ZIR_GO_RUNTIME_PKG ".");
                    p = q;
                    continue;
                }
                if(strlen("NumericInt") == il &&
                   strncmp("NumericInt", ident, il) == 0) {
                    dn += (size_t)snprintf(dst + dn, dst_size - dn,
                        "%sNumericInt", runtime_output ? "" : ZIR_GO_RUNTIME_PKG ".");
                    p = q;
                    continue;
                }
                if(strlen("NumericDouble") == il &&
                   strncmp("NumericDouble", ident, il) == 0) {
                    dn += (size_t)snprintf(dst + dn, dst_size - dn,
                        "%sNumericDouble", runtime_output ? "" : ZIR_GO_RUNTIME_PKG ".");
                    p = q;
                    continue;
                }
                if(strlen("DragSingle") == il &&
                   strncmp("DragSingle", ident, il) == 0) {
                    dn += (size_t)snprintf(dst + dn, dst_size - dn,
                        "%sDragSingle", runtime_output ? "" : ZIR_GO_RUNTIME_PKG ".");
                    p = q;
                    continue;
                }
                if(strlen("DragRange") == il &&
                   strncmp("DragRange", ident, il) == 0) {
                    dn += (size_t)snprintf(dst + dn, dst_size - dn,
                        "%sDragRange", runtime_output ? "" : ZIR_GO_RUNTIME_PKG ".");
                    p = q;
                    continue;
                }
                ZirGoEnumMember *mem = go_const_entry(ident, il);

                if(mem != NULL) {
                    size_t gl = strlen(mem->go);

                    if(dn + gl + 1 < dst_size) {
                        memcpy(dst + dn, mem->go, gl);
                        dn += gl;
                    }
                    p = q;
                    continue;
                }
            }
            if(go_is_array_name(ident, il)) {
                if(q[0] == '[' && q[1] == ':' && q[2] == ']') {
                    if(dn + il + 1 < dst_size) {
                        memcpy(dst + dn, ident, il);
                        dn += il;
                    }
                    p = q;
                    continue;
                }
                if(addr) {
                    /* &arr[i]: keep the address, the index suffix follows */
                    if(dn + il + 2 < dst_size) {
                        dst[dn++] = '&';
                        memcpy(dst + dn, ident, il);
                        dn += il;
                    }
                    p = q;
                    continue;
                }
                if(dn + il + 4 < dst_size) {
                    memcpy(dst + dn, ident, il);
                    dn += il;
                    dst[dn++] = '[';
                    dst[dn++] = ':';
                    dst[dn++] = ']';
                }
                p = q;
                continue;
            }
            /* runtime call? Capitalized identifiers route to the package API. */
            if(isupper((unsigned char)ident[0]) && *skip_ws(q) == '(' &&
               sfi < 0) {
                if(runtime_output) {
                    int match = -1;

                    for(int ii = 0; ii < m->import_count && match < 0; ii++) {
                        char import_guard[ZIR_GO_NAME_MAX];

                        if(m->imports[ii].kind != ZIR_IMPORT_HEADER &&
                           m->imports[ii].kind != ZIR_IMPORT_MODULE)
                            continue;
                        camel_ident(m->imports[ii].target, import_guard,
                                        sizeof(import_guard));
                        for(int gi = 0; gi < g_function_count; gi++) {
                            if(strcmp(g_functions[gi].guard, import_guard) == 0 &&
                               strlen(g_functions[gi].kry) == il &&
                               strncmp(g_functions[gi].kry, ident, il) == 0) {
                                match = gi;
                                break;
                            }
                        }
                    }
                    for(int gi = 0; gi < g_function_count; gi++) {
                        if(match >= 0)
                            break;
                        if(strlen(g_functions[gi].kry) == il &&
                           strncmp(g_functions[gi].kry, ident, il) == 0) {
                            match = gi;
                        }
                    }
                    if(match >= 0) {
                        size_t fl = strlen(g_functions[match].go);

                        if(dn + fl + 16 < dst_size) {
                            memcpy(dst + dn, g_functions[match].go, fl);
                            dn += fl;
                            dst[dn++] = '(';
                        }
                        p = skip_ws(q) + 1;
                        continue;
                    }
                }
                {
                    int written = runtime_output ? 0 :
                        snprintf(dst + dn, dst_size - dn,
                                 "%s.", ZIR_GO_RUNTIME_PKG);

                    if(written > 0)
                        dn += (size_t)written;
                }
                if(dn + il + 1 < dst_size) {
                    memcpy(dst + dn, ident, il);
                    dn += il;
                }
                p = q;
                continue;
            }
            /* plain identifier: verbatim */
            if(dn + il + 1 < dst_size) {
                const char *local = go_local_name_for(ident);

                if(addr && dn + 1 < dst_size)
                    dst[dn++] = '&';
                if(local != NULL) {
                    size_t ll = strlen(local);
                    if(dn + ll + 1 < dst_size) {
                        memcpy(dst + dn, local, ll);
                        dn += ll;
                    }
                } else if(is_module_constant(m, ident)) {
                    char mapped[ZIR_GO_NAME_MAX];
                    camel_ident(ident, mapped, sizeof(mapped));
                    size_t length = strlen(mapped);
                    if(dn + length + 1 < dst_size) {
                        memcpy(dst + dn, mapped, length);
                        dn += length;
                    }
                } else {
                    memcpy(dst + dn, ident, il);
                    dn += il;
                }
            }
            p = q;
            continue;
        }
        if(*p == '.' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
            const char *q = p + 1;
            char field[ZIR_GO_NAME_MAX], mapped[ZIR_GO_NAME_MAX];
            size_t fl = 0;

            while(is_ident_char((unsigned char)*q) && fl + 1 < sizeof(field))
                field[fl++] = *q++;
            field[fl] = '\0';
            go_field_ident(field, mapped, sizeof(mapped));
            if(dn + strlen(mapped) + 2 < dst_size) {
                dst[dn++] = '.';
                memcpy(dst + dn, mapped, strlen(mapped));
                dn += strlen(mapped);
            }
            p = q;
            continue;
        }
        dst[dn++] = *p++;
    }
    dst[dn] = '\0';
    (void)out;
}

/* -------------------------------------------------------- module lowering */

static void
emit_indent(FILE *f, int n)
{
    for(int i = 0; i < n; i++)
        fputc('\t', f);
}

/* Rewrite a C-style three-clause for header into Go:
 *   for int i = 0; i < n; i++   ->  for i := int32(0); i < n; i++
 * The header arrives raw (kry names, semicolons intact, no 'for', no '{');
 * each clause is translated separately so tx_expr never sees the ';'. */
static void
lower_for_header(const ZirModule *m, char *head, size_t head_size)
{
    char initraw[ZIR_GO_TEXT_MAX], condraw[ZIR_GO_TEXT_MAX], stepraw[ZIR_GO_TEXT_MAX];
    char cond[ZIR_GO_TEXT_MAX], step[ZIR_GO_TEXT_MAX], out[ZIR_GO_TEXT_MAX];
    size_t seps[2];
    int nsep = 0;
    int depth = 0;

    for(size_t i = 0; head[i] != '\0' && nsep < 2; i++) {
        char ch = head[i];

        if(ch == '(' || ch == '[' || ch == '{')
            depth++;
        else if(ch == ')' || ch == ']' || ch == '}')
            depth--;
        else if(ch == ';' && depth == 0)
            seps[nsep++] = i;
    }
    if(nsep < 2) {
        /* Go-style header (cond-only or infinite): translate verbatim */
        if(head[0] != '\0') {
            char tmp[ZIR_GO_TEXT_MAX];

            tx_expr(m, head, tmp, sizeof(tmp));
            snprintf(head, head_size, "%s", tmp);
        }
        return;
    }
    {
        size_t len = strlen(head);

        snprintf(initraw, sizeof(initraw), "%.*s", (int)seps[0], head);
        snprintf(condraw, sizeof(condraw), "%.*s", (int)(seps[1] - seps[0] - 1),
                 head + seps[0] + 1);
        snprintf(stepraw, sizeof(stepraw), "%.*s",
                 (int)(len - seps[1] - 1), head + seps[1] + 1);
    }
    {
        char *c = condraw, *s2 = stepraw;

        while(*c == ' ' || *c == '\t')
            c++;
        snprintf(condraw, sizeof(condraw), "%s", c);
        while(*s2 == ' ' || *s2 == '\t')
            s2++;
        snprintf(stepraw, sizeof(stepraw), "%s", s2);
    }
    tx_expr(m, condraw, cond, sizeof(cond));
    tx_expr(m, stepraw, step, sizeof(step));
    /* init: 'T name = expr' -> 'name := GoT(expr)'; 'name = expr' stays. */
    {
        char trimmed[ZIR_GO_TEXT_MAX];
        char *src2 = trimmed;
        char *eq;

        snprintf(trimmed, sizeof(trimmed), "%s", initraw);
        while(*src2 == ' ' || *src2 == '\t')
            src2++;
        eq = strchr(src2, '=');
        if(eq != NULL && eq > src2 && eq[-1] != '=' && eq[-1] != '!' &&
           eq[-1] != '<' && eq[-1] != '>') {
            char *name_end = eq;
            char *name_start;

            while(name_end > src2 &&
                  (name_end[-1] == ' ' || name_end[-1] == '\t'))
                name_end--;
            name_start = name_end;
            while(name_start > src2 && is_ident_char((unsigned char)name_start[-1]))
                name_start--;
            if(name_start < name_end) {
                char before[ZIR_GO_NAME_MAX];
                size_t bl = (size_t)(name_start - src2);
                char name[ZIR_GO_NAME_MAX];
                size_t nl = (size_t)(name_end - name_start);
                char *valraw = eq + 1;
                char val[ZIR_GO_TEXT_MAX];

                while(*valraw == ' ' || *valraw == '\t')
                    valraw++;
                if(bl >= sizeof(before))
                    bl = sizeof(before) - 1;
                memcpy(before, src2, bl);
                before[bl] = '\0';
                if(nl >= sizeof(name))
                    nl = sizeof(name) - 1;
                memcpy(name, name_start, nl);
                name[nl] = '\0';
                tx_expr(m, valraw, val, sizeof(val));
                /* one C type identifier before the name? */
                {
                    char ctype[ZIR_GO_NAME_MAX];
                    size_t cn = strlen(before);
                    int wordstart = -1;
                    int words = 0;

                    while(cn > 0 && (before[cn - 1] == ' ' ||
                                     before[cn - 1] == '\t'))
                        cn--;
                    for(size_t k = 0; k < cn; k++) {
                        if(is_ident_char((unsigned char)before[k])) {
                            if(wordstart < 0)
                                wordstart = (int)k;
                        } else if(wordstart >= 0) {
                            words++;
                            wordstart = -1;
                        }
                    }
                    if(wordstart >= 0)
                        words++;
                    if(words == 1 && wordstart == 0) {
                        char gt[ZIR_GO_NAME_MAX];

                        snprintf(ctype, sizeof(ctype), "%.*s", (int)cn,
                                 before);
                        if(go_type(ctype, gt, sizeof(gt)) && gt[0] != '\0')
                            snprintf(out, sizeof(out), "%s := %s(%s)", name,
                                     gt, val);
                        else
                            snprintf(out, sizeof(out), "%s := %s", name, val);
                        goto init_done;
                    }
                }
                snprintf(out, sizeof(out), "%s = %s", name, val);
                goto init_done;
            }
        }
        snprintf(out, sizeof(out), "%s", initraw);
    }
init_done:
    snprintf(head, head_size, "%s; %s; %s", out, cond, step);
}

static void
resolve_body_symbol(void *context, const char *text, char *out, size_t size)
{
    const ZirModule *module = context;
    if((SliceElementType(text, NULL, 0) || FindType(module, text, NULL) != NULL) &&
       go_type(text, out, size))
        return;
    for(int i = 0; i < module->global_count; i++) {
        if(!strcmp(text, module->globals[i].name)) {
            camel_ident(text, out, size);
            return;
        }
    }
    tx_expr(context, text, out, size);
    const char *arguments = strchr(text, '(');
    if(runtime_output && arguments != NULL && *instance_receiver) {
        char name[ZIR_NAME_MAX];
        const ZirModule *owner = NULL;
        const ZirFunction *callee = NULL;
        size_t length = (size_t)(arguments - text);
        if(length < sizeof(name)) {
            memcpy(name, text, length);
            name[length] = '\0';
            trim_in_place(name);
            if(ResolveFunction(module, name, &owner, &callee) == 1 &&
               !callee->is_extern && callee->uses_host) {
                char call[ZIR_GO_TEXT_MAX];
                snprintf(call, sizeof(call), "%s.%s", instance_receiver, out);
                copy_text(out, size, call);
            }
        }
    }
}

static void
lower_function(FILE *f, const ZirModule *m, const ZirFunction *fn,
               const char *guard)
{
    char fname[ZIR_GO_NAME_MAX * 2];
    char ret[ZIR_GO_NAME_MAX];
    int indent = 1;
    int saved_array_count = zir_go_array_count;
    int saved_local_count = zir_go_local_count;

    instance_receiver[0] = '\0';
    zir_go_local_count = 0;
    if(runtime_output && fn->uses_host) {
        int collision;
        int serial = 0;
        do {
            snprintf(instance_receiver, sizeof(instance_receiver), "instance_host_%d", serial++);
            collision = module_uses_identifier(m, instance_receiver);
        } while(collision);
    }
    camel_ident(fn->name, fname, sizeof(fname));
    /* signature: (st *State, <converted args>) */
    {
        char parts[32][ZIR_GO_TEXT_MAX];
        int n, i;
        int emitted = 0;

        fputs("func ", f);
        if(*instance_receiver)
            fprintf(f, "(%s *runtime) ", instance_receiver);
        fprintf(f, "%s_%s(", guard, fname);
        if(m->state_count > 0) {
            fprintf(f, "%s *%sState", state_receiver, guard);
            emitted = 1;
        }
        if(fn->args[0] != '\0') {
            n = split_top(fn->args, parts, 32);
            for(i = 0; i < n; i++) {
                char *colon = strchr(parts[i], ':');
                char aname[ZIR_GO_NAME_MAX], atype[ZIR_GO_NAME_MAX];
                char gt[ZIR_GO_NAME_MAX];
                size_t al;

                if(colon == NULL) {
                    Diagnostic(fn->span, "zir_go.parameter", "parameters require name: type: %s", parts[i]);
                    exit(1);
                }
                al = (size_t)(colon - parts[i]);
                while(al > 0 && parts[i][al - 1] == ' ')
                    al--;
                memcpy(aname, parts[i], al);
                aname[al] = '\0';
                snprintf(atype, sizeof(atype), "%s", colon + 1);
                go_register_local_name(aname);
                {
                    char mapped[ZIR_GO_NAME_MAX];
                    go_local_ident(aname, mapped, sizeof(mapped));
                    snprintf(aname, sizeof(aname), "%s", mapped);
                }
                require_go_type(atype, gt, sizeof(gt), fn->span);
                fprintf(f, "%s%s %s", emitted ? ", " : "", aname, gt);
                emitted = 1;
            }
        }
        fprintf(f, ")");
        require_go_type(fn->return_type, ret, sizeof(ret), fn->span);
        if(ret[0] != '\0')
            fprintf(f, " %s", ret);
        fprintf(f, " {\n");
    }
    if(EmitBody(f,m,fn,ZIR_GO,resolve_body_symbol,(void *)m, instance_receiver,
                   runtime_output ? "number_runtime" : NULL)) {
        fprintf(f,"}\n\n");
        zir_go_array_count=saved_array_count;
        zir_go_local_count=saved_local_count;
        return;
    }
    go_register_arrays_args(fn->args);
    for(int j = 0; j < fn->stmt_count; j++) {
        const ZirStmt *st = &fn->stmts[j];
        char raw[ZIR_GO_TEXT_MAX];
        char rw[ZIR_GO_TEXT_MAX];

        /* Strip the block-open brace BEFORE translating: tx_expr treats a
         * trailing '{' as a braced group and would invent a matching '}',
         * and it drops the ';' separators C-style for headers need. */
        snprintf(raw, sizeof(raw), "%s", st->text);
        if(st->kind == ZIR_STMT_IF || st->kind == ZIR_STMT_WHILE ||
           st->kind == ZIR_STMT_FOR || st->kind == ZIR_STMT_SWITCH)
            strip_block_brace(raw);
        if(st->kind == ZIR_STMT_EXPR && go_is_go_elided_lifecycle(raw)) {
            rw[0] = '\0';
        } else {
            tx_expr(m, raw, rw, sizeof(rw));
        }
        switch(st->kind) {
        case ZIR_STMT_BLOCK_OPEN:
            emit_indent(f, indent++);
            fprintf(f, "{\n");
            if(indent < 1)
                indent = 1;
            break;
        case ZIR_STMT_BLOCK_CLOSE:
            /* '} else {' must share one line in Go: when the next statement
             * is an else/else-if line, let it emit the merged close. */
            if(j + 1 < fn->stmt_count &&
               fn->stmts[j + 1].kind == ZIR_STMT_IF) {
                char peek[ZIR_GO_TEXT_MAX];

                snprintf(peek, sizeof(peek), "%s", fn->stmts[j + 1].text);
                strip_block_brace(peek);
                if(strncmp(peek, "else", 4) == 0 &&
                   (peek[4] == '\0' || peek[4] == ' '))
                    break;
            }
            if(--indent < 1)
                indent = 1;
            emit_indent(f, indent);
            fprintf(f, "}\n");
            break;
        case ZIR_STMT_IF: {
            char cond[ZIR_GO_TEXT_MAX];
            int chained = 0;

            snprintf(cond, sizeof(cond), "%s", rw);
            strip_block_brace(cond);
            /* 'guard cond' lowers to a plain if: the body is the exit path
             * and must return by itself (k2c fires defers + returns; on the
             * Go path defer runs at function exit anyway). */
            if(strncmp(cond, "guard ", 6) == 0)
                memmove(cond, cond + 6, strlen(cond + 6) + 1);
            if(strncmp(cond, "else", 4) == 0 &&
               (cond[4] == '\0' || cond[4] == ' '))
                chained = 1;
            emit_indent(f, indent);
            if(strncmp(cond, "else if ", 8) == 0)
                fprintf(f, "} else if %s {\n", cond + 8);
            else if(chained)
                fprintf(f, "} else {\n");
            else if(strncmp(cond, "if ", 3) == 0)
                fprintf(f, "if %s {\n", cond + 3);
            else
                fprintf(f, "if %s {\n", cond);
            /* an else branch reuses the level its BLOCK_CLOSE skipped */
            if(!chained)
                indent++;
            break;
        }
        case ZIR_STMT_WHILE: {
            char cond[ZIR_GO_TEXT_MAX];

            snprintf(cond, sizeof(cond), "%s", rw);
            strip_block_brace(cond);
            if(strncmp(cond, "while ", 6) == 0)
                memmove(cond, cond + 6, strlen(cond + 6) + 1);
            emit_indent(f, indent);
            fprintf(f, "for %s {\n", cond);
            indent++;
            break;
        }
        case ZIR_STMT_FOR: {
            char head[ZIR_GO_TEXT_MAX];

            snprintf(head, sizeof(head), "%s", raw);
            if(strncmp(head, "for ", 4) == 0)
                memmove(head, head + 4, strlen(head + 4) + 1);
            lower_for_header(m, head, sizeof(head));
            emit_indent(f, indent);
            if(head[0] != '\0')
                fprintf(f, "for %s {\n", head);
            else
                fprintf(f, "for {\n");
            indent++;
            break;
        }
        case ZIR_STMT_SWITCH: {
            char head[ZIR_GO_TEXT_MAX];

            snprintf(head, sizeof(head), "%s", rw);
            strip_block_brace(head);
            emit_indent(f, indent);
            if(strncmp(head, "switch", 6) == 0 &&
                (head[6] == '\0' || head[6] == ' '))
                fprintf(f, "%s {\n", head);
            else
                fprintf(f, "switch %s {\n", head);
            indent++;
            break;
        }
        case ZIR_STMT_CASE: {
            char head[ZIR_GO_TEXT_MAX];
            int had_brace = 0;
            size_t hl;

            snprintf(head, sizeof(head), "%s", st->text);
            hl = strlen(head);
            while(hl > 0 && (head[hl - 1] == ' ' || head[hl - 1] == '\t' ||
                             head[hl - 1] == '\r'))
                head[--hl] = '\0';
            if(hl > 0 && head[hl - 1] == '{') {
                had_brace = 1;
                head[--hl] = '\0';
                while(hl > 0 && (head[hl - 1] == ' ' || head[hl - 1] == '\t'))
                    head[--hl] = '\0';
            }
            /* ensure the trailing ':' survived expression translation */
            if(hl > 0 && head[hl - 1] != ':') {
                /* a same-line body ('case 1: foo()') keeps the statement;
                 * split it so the label stands alone */
                char *colon;

                {
                    char tmp[ZIR_GO_TEXT_MAX];

                    tx_expr(m, head, tmp, sizeof(tmp));
                    snprintf(head, sizeof(head), "%s", tmp);
                }
                colon = strchr(head, ':');
                if(colon != NULL && colon[1] != '\0') {
                    char rest[ZIR_GO_TEXT_MAX];

                    snprintf(rest, sizeof(rest), "%s", colon + 1);
                    colon[1] = '\0';
                    emit_indent(f, indent > 1 ? indent - 1 : 1);
                    fprintf(f, "%s\n", head);
                    emit_indent(f, indent);
                    fprintf(f, "%s\n", rest);
                    if(had_brace) {
                        emit_indent(f, indent);
                        fprintf(f, "{\n");
                        indent++;
                    }
                    break;
                }
                snprintf(head + hl, sizeof(head) - hl, ":");
            } else {
                char tmp[ZIR_GO_TEXT_MAX];

                tx_expr(m, head, tmp, sizeof(tmp));
                snprintf(head, sizeof(head), "%s", tmp);
            }
            emit_indent(f, indent > 1 ? indent - 1 : 1);
            fprintf(f, "%s\n", head);
            if(had_brace) {
                emit_indent(f, indent);
                fprintf(f, "{\n");
                indent++;
            }
            break;
        }
        case ZIR_STMT_DECL: {
            char *colon = strchr(rw, ':');
            char *assign;

            go_register_arrays_stmt(st->text);
            emit_indent(f, indent);
            if(colon != NULL && colon[1] != '=') {
                char aname[ZIR_GO_NAME_MAX], gt[ZIR_GO_NAME_MAX];
                char tbuf[ZIR_GO_TEXT_MAX];
                size_t al = (size_t)(colon - rw);

                while(al > 0 && rw[al - 1] == ' ')
                    al--;
                memcpy(aname, rw, al);
                aname[al] = '\0';
                go_register_local_name(aname);
                {
                    char mapped[ZIR_GO_NAME_MAX];
                    go_local_ident(aname, mapped, sizeof(mapped));
                    snprintf(aname, sizeof(aname), "%s", mapped);
                }
                assign = strstr(colon, "= ");
                /* the declared type ends where the initializer begins */
                snprintf(tbuf, sizeof(tbuf), "%s", colon + 1);
                if(assign != NULL)
                    tbuf[assign - (colon + 1)] = '\0';
                require_go_type(tbuf, gt, sizeof(gt), st->span);
                if(assign != NULL) {
                    const char *init = skip_ws(assign + 2);
                    const char *source_assign = strstr(st->text, "= ");
                    char translated[ZIR_GO_TEXT_MAX];

                    /* Go composite literals carry their type: 'var x = T{...}'.
                     * Re-enter the source compound-literal path so designated
                     * fields on props records become Go field names. */
                    if(*init == '{' && source_assign != NULL &&
                       gt[0] != '[') {
                        char typed[ZIR_GO_TEXT_MAX];
                        char source_type[ZIR_GO_TEXT_MAX];

                        /* Use the same conversions as an explicit compound
                         * literal, starting from source rather than already
                         * translated Go expressions. */
                        snprintf(source_type, sizeof(source_type), "%s", tbuf);
                        go_trim_ws(source_type);
                        snprintf(typed, sizeof(typed), "(%s)%s", source_type,
                                 skip_ws(source_assign + 2));
                        tx_expr(m, typed, translated, sizeof(translated));
                        fprintf(f, "var %s = %s\n", aname, translated);
                    } else if(*init == '{' &&
                       go_translate_array_literal(m, gt, init, translated,
                                                   sizeof(translated)))
                        fprintf(f, "var %s = %s\n", aname, translated);
                    else if(*init == '{')
                        fprintf(f, "var %s = %s%s\n", aname, gt, init);
                    else
                        fprintf(f, "var %s %s = %s\n", aname, gt, assign + 2);
                } else
                    fprintf(f, "var %s %s\n", aname, gt);
            } else if(colon != NULL) { /* ':=' */
                fprintf(f, "%s\n", rw);
            } else {
                Diagnostic(st->span, "zir_go.declaration", "unsupported declaration: %s", st->text);
                exit(1);
            }
            break;
        }
        case ZIR_STMT_BLOCK_CALL:
            Diagnostic(st->span, "zir_go.block_call",
                          "unlowered block call: %s", st->callee);
            exit(1);
        case ZIR_STMT_RETURN:
        {
            const char *value = skip_ws(rw);

            if(strncmp(value, "return", 6) == 0 &&
               !is_ident_char((unsigned char)value[6]))
                value = skip_ws(value + 6);
            emit_indent(f, indent);
            if(value[0] != '\0')
                fprintf(f, "return %s\n", value);
            else
                fprintf(f, "return\n");
            break;
        }
        case ZIR_STMT_BREAK:
        case ZIR_STMT_CONTINUE:
            emit_indent(f, indent);
            fprintf(f, "%s\n", StmtKindName(st->kind));
            break;
        case ZIR_STMT_DEFER:
            fprintf(stderr, "internal error: cleanup was not lowered\n");
            exit(1);
        case ZIR_STMT_LABEL:
        case ZIR_STMT_GOTO:
            /* 'name:' and 'goto name' are valid Go; forward jumps over
             * declarations are a Go compile error, not a silent miscompile */
            emit_indent(f, indent);
            fprintf(f, "%s\n", rw);
            break;
        case ZIR_STMT_ASSIGN:
        case ZIR_STMT_EXPR:
            if(rw[0] != '\0') {
                emit_indent(f, indent);
                fprintf(f, "%s\n", rw);
            }
            break;
        case ZIR_STMT_UNUSED: {
            const char *value = skip_ws(rw);
            if(strncmp(value, "unused", 6) == 0 && !is_ident_char((unsigned char)value[6]))
                value = skip_ws(value + 6);
            emit_indent(f, indent);
            fprintf(f, "_ = %s\n", value);
            break;
        }
        default:
            Diagnostic(st->span, "zir_go.statement", "unsupported %s statement: %s",
                          StmtKindName(st->kind), st->text);
            exit(1);
        }
    }
    fprintf(f, "}\n\n");
    zir_go_array_count = saved_array_count;
    zir_go_local_count = saved_local_count;
}

static int
go_validate_asserts(const ZirModule *m)
{
    for(int i = 0; i < m->assert_count; i++) {
        const ZirAssert *a = &m->asserts[i];

        if(a->guard[0] != '\0') {
            fprintf(stderr,
                    "zir_go: %s:%d: guarded #assert is not supported by the Go backend: %s\n",
                    a->span.path, a->span.line, a->message);
            return 0;
        }
        if(!a->known) {
            fprintf(stderr,
                    "zir_go: %s:%d: unresolved #assert is not supported by the Go backend: %s\n",
                    a->span.path, a->span.line, a->condition);
            return 0;
        }
        if(!a->value) {
            fprintf(stderr, "zir_go: %s:%d: #assert failed: %s\n",
                    a->span.path, a->span.line, a->message);
            return 0;
        }
    }
    return 1;
}

/* Pure language modules must compile without a UI runtime import. Scan the
 * generated body, ignoring Go comments and quoted text, before keeping it. */
static int
uses_runtime(FILE *f, long begin)
{
    int ch, quote = 0, line_comment = 0, block_comment = 0, previous = 0;
    char ident[128];
    size_t length = 0;
    fflush(f);
    fseek(f, begin, SEEK_SET);
    while((ch = fgetc(f)) != EOF) {
        if(line_comment) { if(ch == '\n') line_comment = 0; continue; }
        if(block_comment) {
            if(previous == '*' && ch == '/') block_comment = 0;
            previous = ch; continue;
        }
        if(quote) {
            if(ch == '\\' && quote != '`') { (void)fgetc(f); continue; }
            if(ch == quote) quote = 0;
            continue;
        }
        if(ch == '/' && previous == '/') { line_comment = 1; previous = 0; continue; }
        if(ch == '*' && previous == '/') { block_comment = 1; previous = 0; continue; }
        if(ch == '"' || ch == '\'' || ch == '`') { quote = ch; length = 0; continue; }
        if(isalnum((unsigned char)ch) || ch == '_') {
            if(length + 1 < sizeof(ident)) ident[length++] = (char)ch;
        } else {
            ident[length] = 0;
            if(ch == '.' && !strcmp(ident, ZIR_GO_RUNTIME_PKG)) return 1;
            length = 0;
        }
        previous = ch;
    }
    return 0;
}

int
go_lower(const ZirProgram *const *progs, int prog_count,
          const char *root, const char *out_dir, const char *pkg,
          int no_main, int runtime_implementation)
{
    char path[1024];
    char seen_stems[64][512];
    int seen_count = 0;
    if(runtime_implementation) {
        /* Reserve the package support file before considering source basenames. */
        snprintf(seen_stems[seen_count++], sizeof(seen_stems[0]), "numeric_support");
    }

    go_build_global_functions(progs, prog_count);
    for(int pi = 0; pi < prog_count; pi++) {
        const ZirProgram *prog = progs[pi];

        for(int mi = 0; mi < prog->module_count; mi++) {
            const ZirModule *m = &prog->modules[mi];
            char stem[512], guard[ZIR_GO_NAME_MAX];
            FILE *f;

            stem_from_source(m->source_path, stem, sizeof(stem));
            /* flat output: two sources with the same basename would collide */
            for(int si = 0; si < seen_count; si++) {
                if(strcmp(seen_stems[si], stem) == 0) {
                    fprintf(stderr,
                            "zir_go: duplicate source basename %s "
                            "(Go output is flat)\n", stem);
                    return 1;
                }
            }
            if(!go_validate_asserts(m))
                return 1;
            if(seen_count < 64)
                snprintf(seen_stems[seen_count++], sizeof(seen_stems[0]),
                         "%s", stem);
            camel_ident(stem, guard, sizeof(guard));
            zir_go_array_count = 0;
            go_register_arrays_module(m);
            snprintf(path, sizeof(path), "%s/%s.go", out_dir, stem);
            mkdir_parent(path);
            f = tmpfile();
            if(f == NULL) {
                fprintf(stderr, "zir_go: cannot write %s\n", path);
                return 1;
            }
            type_scope = m;
            runtime_output = runtime_implementation;
            go_set_module(m, guard);
            fprintf(f, "// Code generated by zir_go from %s. DO NOT EDIT.\n",
                    m->source_path);
            fprintf(f, "package %s\n\n", pkg);
            long runtime_import_begin = ftell(f);
            fprintf(f, "import %s \"%s\"\n\n", ZIR_GO_RUNTIME_PKG,
                    ZIR_GO_RUNTIME_IMPORT);
            long runtime_import_end = ftell(f);
            for(int i = 0; i < g_extern_count; i++) {
                int duplicate = 0;

                if(!g_externs[i].direct_go)
                    continue;
                for(int j = 0; j < i; j++) {
                    if(g_externs[j].direct_go &&
                       strcmp(g_externs[j].go_import_path,
                              g_externs[i].go_import_path) == 0) {
                        duplicate = 1;
                        break;
                    }
                }
                if(strcmp(g_externs[i].go_import_path,
                          ZIR_GO_RUNTIME_IMPORT) == 0)
                    duplicate = 1;
                if(!duplicate)
                    fprintf(f, "import %s \"%s\"\n",
                            g_externs[i].go_import_alias,
                            g_externs[i].go_import_path);
            }
            if(g_extern_count > 0)
                fprintf(f, "\n");

            for(int i = 0; i < m->import_count; i++) {
                const ZirImport *imp = &m->imports[i];

                if(imp->kind == ZIR_IMPORT_HEADER)
                    fprintf(f, "// #import %s\n", imp->target);
                /* ZIR_IMPORT_EXTERN lowers either to direct Go imports above
                 * or to the Host interface below. */
            }
            if(!runtime_implementation)
                EmitNumbers(f,m,ZIR_GO);
            /* '#extern' host bridge: one interface, one package var, one
             * setter. Generated frames call hostVar.Method(...) directly. */
            {
                int first_host = -1;
                int host_count = 0;

                for(int i = 0; i < g_extern_count; i++) {
                    if(g_externs[i].direct_go)
                        continue;
                    if(first_host < 0)
                        first_host = i;
                    host_count++;
                }
            if(host_count > 0 && !runtime_output) {
                fprintf(f, "// %sHost bridges '#extern' declarations to the",
                        guard);
                fprintf(f, " embedding Go program.\ntype %sHost interface {\n",
                        guard);
                for(int i = 0; i < g_extern_count; i++) {
                    const ZirGoExtern *ex = &g_externs[i];
                    char gt[ZIR_GO_NAME_MAX];

                    if(ex->direct_go)
                        continue;
                    fprintf(f, "\t%s(", ex->go);
                    for(int a = 0; a < ex->pcount; a++) {
                        char aname[ZIR_GO_NAME_MAX];

                        camel_ident(ex->pnames[a], aname, sizeof(aname));
                        require_go_type(ex->ptypes[a], gt, sizeof(gt), m->span);
                        fprintf(f, "%s%s %s", a > 0 ? ", " : "", aname, gt);
                    }
                    fprintf(f, ")");
                    require_go_type(ex->ret, gt, sizeof(gt), m->span);
                    if(gt[0] != '\0')
                        fprintf(f, " %s", gt);
                    fprintf(f, "\n");
                }
                fprintf(f, "}\n\n");
                fprintf(f, "var %s %sHost\n\n",
                        g_externs[first_host].host_var, guard);
                fprintf(f, "// Set%sHost wires the '#extern' bridge before",
                        guard);
                fprintf(f, " the first frame runs.\nfunc Set%sHost(host %sHost)",
                        guard, guard);
                fprintf(f, " {\n\t%s = host\n}\n\n",
                        g_externs[first_host].host_var);
            }
            }
            /* types */
            int enum_idx = 0;

            for(int i = 0; i < m->type_count; i++) {
                const ZirType *t = &m->types[i];
                if(t->is_extern && !runtime_output)
                    continue;

                if(!t->is_enum && t->name[0] == '#') {
                    if(!runtime_output) {
                        Diagnostic(t->span, "zir_go.type", "C typedef has no portable Go representation: %s", t->body);
                        exit(1);
                    }
                    /* C-only typedef ('#type') in a runtime-implementation
                     * module declares the C ABI — function-pointer callback
                     * types and similar. Go emits nothing for it; extern
                     * signatures are skipped below, so a reference from a
                     * Go-visible function still fails as an unresolved type. */
                    continue;
                }

                if(t->is_slot) {
                    EmitSlotType(f, t, ZIR_GO, resolve_slot_type, NULL);
                    continue;
                }
                if(t->is_enum) {
                    /* enums: typed constants with C counter semantics
                         * (g_enums was built in the same order as m->types) */
                        ZirGoEnum *e = enum_idx < g_enum_count
                                         ? &g_enums[enum_idx++] : NULL;

                        if(e == NULL)
                            continue;
                        if(e->go_type[0] != '\0')
                            fprintf(f, "type %s int32\n\n", e->go_type);
                        fprintf(f, "const (\n");
                        for(int mI = 0; mI < e->count; mI++) {
                            ZirGoEnumMember *mem = &e->members[mI];
                            char val[ZIR_GO_TEXT_MAX];

                            if(mem->val[0] != '\0') {
                                tx_expr(m, mem->val, val, sizeof(val));
                            } else if(mI == 0) {
                                snprintf(val, sizeof(val), "0");
                            } else {
                                /* Refer to the previous constant: negative
                                 * values and expressions need no host-side
                                 * evaluator or sentinel counter. */
                                snprintf(val, sizeof(val), "%s + 1",
                                         e->members[mI - 1].go);
                            }
                            fprintf(f, "\t%s = %s\n", mem->go, val);
                        }
                        fprintf(f, ")\n\n");
                    } else {
                        fprintf(f, "type %s struct {\n", t->name);
                    {
                        size_t offset = 0;
                        ZirTypeField field;
                        int status;

                        while((status = TypeNextField(t, &offset, &field)) == 1) {
                            char fname[ZIR_GO_NAME_MAX], gt[ZIR_GO_NAME_MAX];
                            go_field_ident(field.name, fname, sizeof(fname));
                            require_go_type(field.type, gt, sizeof(gt), t->span);
                            if(strcmp(t->name, "TableViewProps") == 0 &&
                               (strcmp(fname, "CopyText") == 0 ||
                                strcmp(fname, "PastedText") == 0))
                                snprintf(gt, sizeof(gt), "*string");
                            if(strcmp(t->name, "ModalProps") == 0 &&
                               strcmp(fname, "Text") == 0)
                                snprintf(gt, sizeof(gt), "[]byte");
                            if((strcmp(t->name, "TextFieldProps") == 0 ||
                                strcmp(t->name, "TextAreaProps") == 0) &&
                               strcmp(fname, "Text") == 0)
                                snprintf(gt, sizeof(gt), "[]byte");
                            if((strcmp(t->name, "TextFieldProps") == 0 ||
                                strcmp(t->name, "TextAreaProps") == 0) &&
                               strcmp(fname, "Focused") == 0)
                                snprintf(gt, sizeof(gt), "*bool");
                            if(strcmp(t->name, "TextFieldProps") == 0 &&
                               strcmp(fname, "CommitPressed") == 0)
                                snprintf(gt, sizeof(gt), "*bool");
                            if((strcmp(t->name, "TextFieldProps") == 0 ||
                                strcmp(t->name, "TextAreaProps") == 0) &&
                               strcmp(fname, "TextSize") == 0)
                                continue;
                            if(slice_prop_field(t->name, fname) && gt[0] == '*') {
                                char elem[ZIR_GO_NAME_MAX];
                                snprintf(elem, sizeof(elem), "%s", gt + 1);
                                snprintf(gt, sizeof(gt), "[]%s", elem);
                            }
                            fprintf(f, "\t%s %s\n", fname, gt);
                        }
                        if(status < 0) {
                            fprintf(stderr, "%s:%d: malformed field in %s\n",
                                    t->span.path, t->span.line, t->name);
                            exit(1);
                        }
                    }
                    fprintf(f, "}\n\n");
                }
            }
            /* defines -> consts */
            for(int i = 0; i < m->define_count; i++) {
                char cname[ZIR_GO_NAME_MAX];
                char cval[ZIR_GO_TEXT_MAX];

                camel_ident(m->defines[i].name, cname, sizeof(cname));
                tx_expr(m, m->defines[i].value, cval, sizeof(cval));
                fprintf(f, "const %s = %s\n", cname, cval);
            }
            /* globals */
            for(int i = 0; i < m->global_count; i++) {
                const ZirGlobal *g = &m->globals[i];
                char gname[ZIR_GO_NAME_MAX], gt[ZIR_GO_NAME_MAX], ginit[ZIR_GO_TEXT_MAX];

                camel_ident(g->name, gname, sizeof(gname));
                require_go_type(g->type, gt, sizeof(gt), g->span);
                if(!ScalarLiteral(g->type,g->init,ZIR_GO,g->span,ginit,sizeof(ginit)))
                    tx_expr(m, g->init, ginit, sizeof(ginit));
                if(ginit[0] != '\0') {
                    if(ginit[0] == '{')
                        fprintf(f, "var %s = %s%s\n", gname, gt, ginit);
                    else
                        fprintf(f, "var %s %s = %s\n", gname, gt, ginit);
                } else
                    fprintf(f, "var %s %s\n", gname, gt);
            }
            /* state struct + instance */
            if(m->state_count > 0) {
                fprintf(f, "type %sState struct {\n", guard);
                for(int i = 0; i < m->state_count; i++) {
                    const ZirStateField *sf = &m->state_fields[i];
                    char fname[ZIR_GO_NAME_MAX], gt[ZIR_GO_NAME_MAX];

                    camel_ident(sf->name, fname, sizeof(fname));
                    require_go_type(sf->type, gt, sizeof(gt), sf->span);
                    fprintf(f, "\t%s %s\n", fname, gt);
                }
                fprintf(f, "}\n\n");
                fprintf(f, "var %sStateValue = &%sState{\n", guard, guard);
                for(int i = 0; i < m->state_count; i++) {
                    const ZirStateField *sf = &m->state_fields[i];
                    char fname[ZIR_GO_NAME_MAX], finit[ZIR_GO_TEXT_MAX];
                    char gt[ZIR_GO_NAME_MAX];

                    camel_ident(sf->name, fname, sizeof(fname));
                    require_go_type(sf->type, gt, sizeof(gt), sf->span);
                    if(!ScalarLiteral(sf->type,sf->init,ZIR_GO,sf->span,finit,sizeof(finit)))
                        tx_expr(m, sf->init, finit, sizeof(finit));
                    if(finit[0] != '\0' &&
                       !(sf->type[0] == '[' && strstr(sf->type, "char") != NULL &&
                         strcmp(finit, "\"\"") == 0)) {
                        if(sf->type[0] == '[' && strstr(sf->type, "char") != NULL &&
                           finit[0] == '"')
                            fprintf(f,
                                    "\t%s: func() %s { var v %s; copy(v[:], %s); return v }(),\n",
                                    fname, gt, gt, finit);
                        else if(finit[0] == '{')
                            fprintf(f, "\t%s: %s%s,\n", fname, gt, finit);
                        else
                            fprintf(f, "\t%s: %s,\n", fname, finit);
                    }
                }
                fprintf(f, "}\n\n");
            }
            /* functions ('#extern' prototypes have no body: they lower to
             * Host interface methods, not Go functions) */
            for(int i = 0; i < m->function_count; i++) {
                if(m->functions[i].is_extern || m->functions[i].is_closure)
                    continue;
                lower_function(f, m, &m->functions[i], guard);
            }

            if(!uses_runtime(f, runtime_import_end)) {
                fseek(f, runtime_import_begin, SEEK_SET);
                for(long k = runtime_import_begin; k < runtime_import_end; k++)
                    fputc(k == runtime_import_end - 1 ? '\n' : ' ', f);
            }
            rewind(f);
            FILE *output = fopen(path, "wb");
            if(output == NULL) {
                fprintf(stderr, "zir_go: cannot write %s\n", path);
                fclose(f);
                return 1;
            }
            char buffer[8192];
            size_t bytes;
            int failed = 0;
            while((bytes = fread(buffer, 1, sizeof(buffer), f)) != 0) {
                if(fwrite(buffer, 1, bytes, output) != bytes) {
                    failed = 1;
                    break;
                }
            }
            failed |= ferror(f) != 0;
            failed |= fclose(output) != 0;
            fclose(f);
            if(failed) {
                fprintf(stderr, "zir_go: cannot finish %s\n", path);
                return 1;
            }
        }
    }
    if(runtime_implementation) {
        snprintf(path, sizeof(path), "%s/numeric_support.go", out_dir);
        mkdir_parent(path);
        FILE *support = fopen(path, "w");
        if(support == NULL) {
            fprintf(stderr, "zir_go: cannot write %s\n", path);
            return 1;
        }
        fprintf(support, "// Code generated by zir_go for shared runtime numeric support. DO NOT EDIT.\n");
        fprintf(support, "package %s\n\n", pkg);
        EmitNumberSupport(support, ZIR_GO, "number_runtime");
        fclose(support);
    }
    return 0;
}
