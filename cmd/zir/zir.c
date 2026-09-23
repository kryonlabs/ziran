#include "zir.h"
#include "zir_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int
TypeNextField(const ZirType *record, size_t *offset, ZirTypeField *field)
{
    size_t length = strlen(record->body);

    memset(field, 0, sizeof(*field));
    if(record->is_enum || record->is_slot)
        return -1;
    while(*offset < length) {
        const char *start = record->body + *offset;
        const char *newline = strchr(start, '\n');
        const char *end = newline != NULL ? newline : record->body + length;
        const char *colon;
        const char *name_end;
        const char *type_start;
        size_t name_length;
        size_t type_length;

        *offset = newline != NULL ? (size_t)(newline - record->body) + 1 : length;
        while(start < end && isspace((unsigned char)*start))
            start++;
        while(end > start && isspace((unsigned char)end[-1]))
            end--;
        if(start == end)
            continue;
        colon = memchr(start, ':', (size_t)(end - start));
        if(colon == NULL)
            return -1;
        name_end = colon;
        while(name_end > start && isspace((unsigned char)name_end[-1]))
            name_end--;
        type_start = colon + 1;
        while(type_start < end && isspace((unsigned char)*type_start))
            type_start++;
        name_length = (size_t)(name_end - start);
        type_length = (size_t)(end - type_start);
        if(name_length == 0 || name_length >= sizeof(field->name) ||
           type_length == 0 || type_length >= sizeof(field->type))
            return -1;
        if(!isalpha((unsigned char)*start) && *start != '_')
            return -1;
        for(const char *cursor = start + 1; cursor < name_end; cursor++) {
            if(!isalnum((unsigned char)*cursor) && *cursor != '_')
                return -1;
        }
        memcpy(field->name, start, name_length);
        memcpy(field->type, type_start, type_length);
        return 1;
    }
    return 0;
}

int
SliceElementType(const char *type, char *element, size_t element_size)
{
    if(type == NULL || type[0] != '[' || type[1] != ']')
        return 0;
    const char *start = type + 2;
    while(isspace((unsigned char)*start))
        start++;
    size_t length = strlen(start);
    while(length > 0 && isspace((unsigned char)start[length - 1]))
        length--;
    if(length == 0)
        return 0;
    if(element != NULL) {
        if(length >= element_size)
            return 0;
        memcpy(element, start, length);
        element[length] = '\0';
    }
    return 1;
}

int
ArrayElementType(const char *type, char *element, size_t element_size,
                    int *capacity)
{
    const char *cursor;
    const char *close;
    size_t length;
    long count = 0;
    int digits = 0;

    if(type == NULL || type[0] != '[')
        return 0;
    cursor = type + 1;
    if(*cursor >= '0' && *cursor <= '9') {
        while(*cursor >= '0' && *cursor <= '9') {
            count = count * 10 + (*cursor - '0');
            if(count > 1048576)
                return 0;
            cursor++;
            digits++;
        }
        if(digits == 0 || *cursor != ']')
            return 0;
    } else {
        /* Symbolic capacity: a named module constant. The bound value is
         * resolved by the backend emitters; only the element type matters
         * here. */
        if(!isalpha((unsigned char)*cursor) && *cursor != '_')
            return 0;
        while(isalnum((unsigned char)*cursor) || *cursor == '_')
            cursor++;
        if(*cursor != ']')
            return 0;
        count = -1;
    }
    close = cursor + 1;
    while(isspace((unsigned char)*close))
        close++;
    length = strlen(close);
    while(length > 0 && isspace((unsigned char)close[length - 1]))
        length--;
    if(length == 0)
        return 0;
    if(element != NULL) {
        if(length >= element_size)
            return 0;
        memcpy(element, close, length);
        element[length] = '\0';
    }
    if(capacity != NULL)
        *capacity = (int)count;
    return 1;
}

static int
enum_has_member(const ZirType *type, const char *name)
{
    if(!type->is_enum && strcmp(type->name, "#enum") != 0)
        return 0;
    const char *cursor = type->body;
    while(*cursor) {
        while(isspace((unsigned char)*cursor) || *cursor == ',')
            cursor++;
        const char *start = cursor;
        while(isalnum((unsigned char)*cursor) || *cursor == '_')
            cursor++;
        if((size_t)(cursor - start) == strlen(name) &&
           strncmp(start, name, (size_t)(cursor - start)) == 0)
            return 1;
        while(*cursor && *cursor != ',' && *cursor != '\n')
            cursor++;
    }
    return 0;
}

int
ResolveEnumMember(const ZirModule *module, const char *name,
                     const ZirModule **owner, const ZirType **type)
{
    *owner = NULL;
    *type = NULL;
    /* An empty identifier is never a member reference: unsupported host
     * expressions lower to empty names and the lenient checker relies on
     * them staying unresolved instead of turning into ambiguity errors. */
    if(!*name)
        return 0;
    /* Local declarations shadow imports, just as functions and types do. */
    for(int pass = 0; pass < 2; pass++) {
        int count = pass == 0 ? 1 : module->import_count;
        for(int i = 0; i < count; i++) {
            const ZirModule *scope = module;
            if(pass != 0) {
                if(module->imports[i].kind != ZIR_IMPORT_HEADER)
                    continue;
                scope = module->imports[i].resolved_module;
            }
            if(scope == NULL)
                continue;
            for(int j = 0; j < scope->type_count; j++) {
                const ZirType *candidate = &scope->types[j];
                if(!enum_has_member(candidate, name))
                    continue;
                if(*type != NULL && *type != candidate) {
                    *owner = NULL;
                    *type = NULL;
                    return -1;
                }
                *owner = scope;
                *type = candidate;
            }
        }
        if(*type != NULL)
            return 1;
    }
    return 0;
}

int
ResolveFunction(const ZirModule *module, const char *name,
                   const ZirModule **owner, const ZirFunction **function)
{
    *owner = NULL;
    *function = NULL;
    for(int i = 0; i < module->function_count; i++) {
        if(strcmp(module->functions[i].name, name) == 0) {
            *owner = module;
            *function = &module->functions[i];
            return 1;
        }
    }
    for(int i = 0; i < module->import_count; i++) {
        const ZirModule *imported = module->imports[i].resolved_module;
        if(imported == NULL)
            continue;
        for(int f = 0; f < imported->function_count; f++) {
            const ZirFunction *candidate = &imported->functions[f];
            if(!candidate->is_public || strcmp(candidate->name, name) != 0)
                continue;
            if(*function != NULL && *function != candidate) {
                *owner = NULL;
                *function = NULL;
                return -1;
            }
            *owner = imported;
            *function = candidate;
        }
    }
    return *function != NULL;
}

const ZirType *
FindType(const ZirModule *module, const char *name, const ZirModule **owner)
{
    const ZirType *found = NULL;
    const ZirModule *scope = NULL;

    if(owner)
        *owner = NULL;

    for(int i = 0; i < module->type_count; i++) {
        if(strcmp(module->types[i].name, name) == 0) {
            if(owner)
                *owner = module;
            return &module->types[i];
        }
    }
    for(int i = 0; i < module->import_count; i++) {
        const ZirImport *import = &module->imports[i];
        const ZirModule *target = import->resolved_module;
        if(import->kind != ZIR_IMPORT_HEADER || target == NULL)
            continue;
        for(int j = 0; j < target->type_count; j++) {
            const ZirType *candidate = &target->types[j];
            if(strcmp(candidate->name, name) != 0)
                continue;
            if(found && found != candidate)
                return NULL;
            found = candidate;
            scope = target;
        }
    }
    if(owner)
        *owner = scope;
    return found;
}

static void *
realloc_array(void *ptr, int *cap, int count, size_t elem_size)
{
    void *next;
    int ncap;

    if(count < *cap)
        return ptr;
    ncap = *cap == 0 ? 4 : *cap * 2;
    next = realloc(ptr, (size_t)ncap * elem_size);
    if(next == NULL)
        return NULL;
    memset((char *)next + (size_t)(*cap) * elem_size, 0,
           (size_t)(ncap - *cap) * elem_size);
    *cap = ncap;
    return next;
}

void
copy_text(char *dst, size_t dst_size, const char *src)
{
    if(dst_size == 0)
        return;
    if(src == NULL)
        src = "";
    snprintf(dst, dst_size, "%s", src);
}

ZirProgram *
ProgramNew(void)
{
    return calloc(1, sizeof(ZirProgram));
}

void
ProgramFree(ZirProgram *program)
{
    int i;

    if(program == NULL)
        return;
    for(i = 0; i < program->module_count; i++) {
        ZirModule *m = &program->modules[i];
        int j;

        for(j = 0; j < m->function_count; j++) {
            free(m->functions[j].stmts);
            free(m->functions[j].exprs);
            free(m->functions[j].captures);
        }
        free(m->state_fields);
        free(m->globals);
        free(m->imports);
        free(m->functions);
        free(m->defines);
        free(m->asserts);
        free(m->types);
    }
    free(program->modules);
    free(program);
}

ZirSourceSpan
Span(const char *path, int line, int column)
{
    return SpanEnd(path, line, column, line, column);
}

ZirSourceSpan
SpanEnd(const char *path, int line, int column, int end_line, int end_column)
{
    ZirSourceSpan span;

    memset(&span, 0, sizeof(span));
    copy_text(span.path, sizeof(span.path), path);
    span.line = line;
    span.column = column;
    span.end_line = end_line;
    span.end_column = end_column;
    return span;
}

ZirModule *
ProgramAddModule(ZirProgram *program, const char *name,
                    const char *source_path, ZirSourceSpan span)
{
    ZirModule *modules;
    ZirModule *m;

    if(program == NULL)
        return NULL;
    modules = realloc_array(program->modules, &program->module_cap,
                                program->module_count, sizeof(ZirModule));
    if(modules == NULL)
        return NULL;
    program->modules = modules;
    m = &program->modules[program->module_count++];
    memset(m, 0, sizeof(*m));
    copy_text(m->name, sizeof(m->name), name);
    copy_text(m->source_path, sizeof(m->source_path), source_path);
    m->span = span;
    return m;
}

ZirStateField *
ModuleAddStateField(ZirModule *module, const char *name, const char *type,
                       const char *init, ZirSourceSpan span)
{
    ZirStateField *fields;
    ZirStateField *f;

    if(module == NULL)
        return NULL;
    fields = realloc_array(module->state_fields, &module->state_cap,
                               module->state_count, sizeof(ZirStateField));
    if(fields == NULL)
        return NULL;
    module->state_fields = fields;
    f = &module->state_fields[module->state_count++];
    memset(f, 0, sizeof(*f));
    copy_text(f->name, sizeof(f->name), name);
    copy_text(f->type, sizeof(f->type), type);
    copy_text(f->init, sizeof(f->init), init);
    f->span = span;
    return f;
}

ZirImport *
ModuleAddImport(ZirModule *module, ZirImportKind kind, const char *name,
                   const char *target, const char *signature, int required,
                   ZirSourceSpan span)
{
    ZirImport *imports;
    ZirImport *imp;

    if(module == NULL)
        return NULL;
    imports = realloc_array(module->imports, &module->import_cap,
                                module->import_count, sizeof(ZirImport));
    if(imports == NULL)
        return NULL;
    module->imports = imports;
    imp = &module->imports[module->import_count++];
    memset(imp, 0, sizeof(*imp));
    imp->kind = kind;
    copy_text(imp->name, sizeof(imp->name), name);
    copy_text(imp->target, sizeof(imp->target), target);
    copy_text(imp->signature, sizeof(imp->signature), signature);
    imp->required = required;
    imp->span = span;
    return imp;
}

ZirFunction *
ModuleAddFunction(ZirModule *module, const char *name, const char *args,
                     const char *return_type, int exported, ZirSourceSpan span)
{
    ZirFunction *functions;
    ZirFunction *fn;

    if(module == NULL)
        return NULL;
    functions = realloc_array(module->functions, &module->function_cap,
                                  module->function_count, sizeof(ZirFunction));
    if(functions == NULL)
        return NULL;
    module->functions = functions;
    fn = &module->functions[module->function_count++];
    memset(fn, 0, sizeof(*fn));
    copy_text(fn->name, sizeof(fn->name), name);
    copy_text(fn->args, sizeof(fn->args), args);
    copy_text(fn->return_type, sizeof(fn->return_type), return_type);
    fn->exported = exported;
    fn->span = span;
    return fn;
}

void
ModuleAddGlobal(ZirModule *module, const char *name, const char *type,
                   const char *init, ZirSourceSpan span)
{
    ZirGlobal *globals;

    if(module == NULL)
        return;
    globals = realloc_array(module->globals, &module->global_cap,
                                module->global_count, sizeof(ZirGlobal));
    if(globals == NULL)
        return;
    module->globals = globals;
    memset(&module->globals[module->global_count], 0, sizeof(ZirGlobal));
    copy_text(module->globals[module->global_count].name,
             sizeof(module->globals[0].name), name);
    copy_text(module->globals[module->global_count].type,
             sizeof(module->globals[0].type), type);
    copy_text(module->globals[module->global_count].init,
             sizeof(module->globals[0].init), init);
    module->globals[module->global_count].span = span;
    module->global_count++;
}

void
ModuleAddStatic(ZirModule *module, const char *name, const char *type,
                   const char *init, ZirSourceSpan span)
{
    ModuleAddGlobal(module, name, type, init, span);
    if(module != NULL && module->global_count > 0)
        module->globals[module->global_count - 1].is_static = 1;
}

ZirDefine *
ModuleAddDefine(ZirModule *module, const char *name, const char *value,
                   ZirSourceSpan span)
{
    ZirDefine *defines;
    ZirDefine *d;

    if(module == NULL)
        return NULL;
    defines = realloc_array(module->defines, &module->define_cap,
                                module->define_count, sizeof(ZirDefine));
    if(defines == NULL)
        return NULL;
    module->defines = defines;
    d = &module->defines[module->define_count++];
    memset(d, 0, sizeof(*d));
    copy_text(d->name, sizeof(d->name), name);
    copy_text(d->value, sizeof(d->value), value);
    d->span = span;
    return d;
}

ZirAssert *
ModuleAddAssert(ZirModule *module, const char *condition,
                   const char *message, ZirSourceSpan span)
{
    ZirAssert *asserts;
    ZirAssert *a;

    if(module == NULL)
        return NULL;
    asserts = realloc_array(module->asserts, &module->assert_cap,
                                module->assert_count, sizeof(ZirAssert));
    if(asserts == NULL)
        return NULL;
    module->asserts = asserts;
    a = &module->asserts[module->assert_count++];
    memset(a, 0, sizeof(*a));
    copy_text(a->condition, sizeof(a->condition), condition);
    copy_text(a->message, sizeof(a->message), message);
    a->span = span;
    return a;
}

ZirType *
ModuleAddType(ZirModule *module, const char *name, ZirSourceSpan span)
{
    ZirType *types;

    if(module == NULL)
        return NULL;
    types = realloc_array(module->types, &module->type_cap,
                              module->type_count, sizeof(ZirType));
    if(types == NULL)
        return NULL;
    module->types = types;
    memset(&module->types[module->type_count], 0, sizeof(ZirType));
    copy_text(module->types[module->type_count].name,
             sizeof(module->types[0].name), name);
    module->types[module->type_count].span = span;
    return &module->types[module->type_count++];
}

ZirStmt *
FunctionAddStmt(ZirFunction *fn, ZirStmtKind kind, const char *text,
                   const char *callee, ZirSourceSpan span)
{
    ZirStmt *stmts;
    ZirStmt *st;

    if(fn == NULL)
        return NULL;
    stmts = realloc_array(fn->stmts, &fn->stmt_cap, fn->stmt_count,
                              sizeof(ZirStmt));
    if(stmts == NULL)
        return NULL;
    fn->stmts = stmts;
    st = &fn->stmts[fn->stmt_count++];
    memset(st, 0, sizeof(*st));
    st->kind = kind;
    copy_text(st->text, sizeof(st->text), text);
    copy_text(st->callee, sizeof(st->callee), callee);
    st->expr_root = -1;
    st->lhs_root = -1;
    st->span = span;
    return st;
}

ZirStmt *
FunctionAddBlockCall(ZirFunction *fn, const char *callee, const char *args,
                     const char *text, ZirSourceSpan span)
{
    ZirStmt *st = FunctionAddStmt(fn, ZIR_STMT_BLOCK_CALL, text, callee, span);

    if(st != NULL)
        copy_text(st->args, sizeof(st->args), args);
    return st;
}

ZirExpr *
FunctionAddExpr(ZirFunction *fn, ZirExprKind kind, const char *text,
                   ZirSourceSpan span)
{
    ZirExpr *exprs;
    ZirExpr *expr;

    if(fn == NULL)
        return NULL;
    exprs = realloc_array(fn->exprs, &fn->expr_cap, fn->expr_count,
                              sizeof(ZirExpr));
    if(exprs == NULL)
        return NULL;
    fn->exprs = exprs;
    expr = &fn->exprs[fn->expr_count++];
    memset(expr, 0, sizeof(*expr));
    expr->kind = kind;
    expr->left = -1;
    expr->right = -1;
    expr->first_child = -1;
    expr->next_sibling = -1;
    expr->third = -1;
    copy_text(expr->text, sizeof(expr->text), text);
    expr->span = span;
    return expr;
}

const char *
ImportKindName(ZirImportKind kind)
{
    switch(kind) {
    case ZIR_IMPORT_HEADER: return "header";
    case ZIR_IMPORT_MODULE: return "module";
    case ZIR_IMPORT_EXTERN: return "extern";
    case ZIR_IMPORT_CAPABILITY: return "capability";
    case ZIR_IMPORT_HOST: return "host";
    default: return "unknown";
    }
}

const char *
ExternKindName(ZirExternKind kind)
{
    switch(kind) {
    case ZIR_EXTERN_HOST: return "host";
    case ZIR_EXTERN_GO: return "go";
    case ZIR_EXTERN_C: return "c";
    default: return "none";
    }
}

const char *
ExprKindName(ZirExprKind kind)
{
    switch(kind) {
    case ZIR_EXPR_IDENT: return "ident";
    case ZIR_EXPR_INT: return "int";
    case ZIR_EXPR_FLOAT: return "float";
    case ZIR_EXPR_STRING: return "string";
    case ZIR_EXPR_CALL: return "call";
    case ZIR_EXPR_BINARY: return "binary";
    case ZIR_EXPR_UNARY: return "unary";
    case ZIR_EXPR_MEMBER: return "member";
    case ZIR_EXPR_POINTER_MEMBER: return "pointer_member";
    case ZIR_EXPR_INDEX: return "index";
    case ZIR_EXPR_SLICE: return "slice";
    case ZIR_EXPR_CAST: return "cast";
    case ZIR_EXPR_COMPOUND: return "compound";
    case ZIR_EXPR_FIELD_INIT: return "field_initializer";
    case ZIR_EXPR_SIZEOF: return "sizeof";
    case ZIR_EXPR_CHAR: return "char";
    case ZIR_EXPR_CONDITIONAL: return "conditional";
    case ZIR_EXPR_POSTFIX: return "postfix";
    default: return "unknown";
    }
}

const char *
StmtKindName(ZirStmtKind kind)
{
    switch(kind) {
    case ZIR_STMT_BLOCK_OPEN: return "block_open";
    case ZIR_STMT_BLOCK_CLOSE: return "block_close";
    case ZIR_STMT_DECL: return "decl";
    case ZIR_STMT_ASSIGN: return "assign";
    case ZIR_STMT_EXPR: return "expr";
    case ZIR_STMT_IF: return "if";
    case ZIR_STMT_WHILE: return "while";
    case ZIR_STMT_FOR: return "for";
    case ZIR_STMT_SWITCH: return "switch";
    case ZIR_STMT_CASE: return "case";
    case ZIR_STMT_RETURN: return "return";
    case ZIR_STMT_BREAK: return "break";
    case ZIR_STMT_CONTINUE: return "continue";
    case ZIR_STMT_GOTO: return "goto";
    case ZIR_STMT_LABEL: return "label";
    case ZIR_STMT_DEFER: return "defer";
    case ZIR_STMT_UNUSED: return "unused";
    case ZIR_STMT_RAW: return "raw";
    case ZIR_STMT_BLOCK_CALL: return "block_call";
    default: return "unknown";
    }
}

static void
dump_span(FILE *out, ZirSourceSpan span)
{
    fprintf(out, "%s:%d:%d", span.path, span.line, span.column);
    if(span.end_line > 0 && span.end_column > 0 &&
       (span.end_line != span.line || span.end_column != span.column))
        fprintf(out, "-%d:%d", span.end_line, span.end_column);
}

static void
dump_expr(const ZirFunction *fn, int index, FILE *out, int indent)
{
    const ZirExpr *expr;

    if(fn == NULL || index < 0 || index >= fn->expr_count)
        return;
    expr = &fn->exprs[index];
    for(int i = 0; i < indent; i++)
        fputs("  ", out);
    fprintf(out, "expr %s text %s name %s op %s span ",
            ExprKindName(expr->kind), expr->text, expr->name, expr->op);
    dump_span(out, expr->span);
    if(expr->type[0]) fprintf(out, " type %s", expr->type);
    fprintf(out, "\n");
    if(expr->left >= 0)
        dump_expr(fn, expr->left, out, indent + 1);
    if(expr->right >= 0)
        dump_expr(fn, expr->right, out, indent + 1);
    if(expr->third >= 0)
        dump_expr(fn, expr->third, out, indent + 1);
    for(int child = expr->first_child; child >= 0 &&
         child < fn->expr_count; child = fn->exprs[child].next_sibling)
        dump_expr(fn, child, out, indent + 1);
}

void
ProgramDump(const ZirProgram *program, FILE *out)
{
    int i;

    if(out == NULL)
        return;
    fprintf(out, "zir 1\n");
    if(program == NULL)
        return;
    for(i = 0; i < program->module_count; i++) {
        const ZirModule *m = &program->modules[i];
        int j;

        fprintf(out, "module %s source %s span ", m->name, m->source_path);
        dump_span(out, m->span);
        fprintf(out, "\n");
        for(j = 0; j < m->import_count; j++) {
            const ZirImport *imp = &m->imports[j];

            fprintf(out, "  import %s %s target %s",
                    ImportKindName(imp->kind), imp->name, imp->target);
            if(imp->kind == ZIR_IMPORT_EXTERN)
                fprintf(out, " extern_kind %s extern_symbol %s",
                        ExternKindName(imp->extern_kind),
                        imp->extern_symbol);
            fprintf(out, " required %d signature %s span ",
                    imp->required, imp->signature);
            dump_span(out, imp->span);
            fprintf(out, "\n");
        }
        for(j = 0; j < m->state_count; j++) {
            const ZirStateField *f = &m->state_fields[j];

            fprintf(out, "  state %s type %s init %s span ",
                    f->name, f->type, f->init);
            dump_span(out, f->span);
            fprintf(out, "\n");
        }
        for(j = 0; j < m->assert_count; j++) {
            const ZirAssert *a = &m->asserts[j];

            fprintf(out, "  assert condition %s known %d value %d message %s span ",
                    a->condition, a->known, a->value, a->message);
            dump_span(out, a->span);
            fprintf(out, "\n");
        }
        for(j = 0; j < m->function_count; j++) {
            const ZirFunction *fn = &m->functions[j];
            int k;

            fprintf(out, "  function %s args %s return %s exported %d span ",
                    fn->name, fn->args, fn->return_type, fn->exported);
            dump_span(out, fn->span);
            fprintf(out, "\n");
            for(k = 0; k < fn->stmt_count; k++) {
                const ZirStmt *st = &fn->stmts[k];

                fprintf(out, "    stmt %s callee %s args %s text %s span ",
                        StmtKindName(st->kind), st->callee, st->args,
                        st->text);
                dump_span(out, st->span);
                fprintf(out, "\n");
                if(st->expr_root >= 0)
                    dump_expr(fn, st->expr_root, out, 3);
                if(st->lhs_root >= 0)
                    dump_expr(fn, st->lhs_root, out, 3);
            }
        }
    }
}
