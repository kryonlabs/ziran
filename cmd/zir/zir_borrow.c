#include "zir_borrow.h"
#include "zir_diagnostic.h"
#include "zir_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Origin {
    uint64_t parameters;
    int depth;
    int invalid;
} Origin;

typedef struct BorrowFunction {
    const ZirModule *module;
    const ZirFunction *fn;
    Origin returned;
    Origin *locals;
} BorrowFunction;

typedef struct BorrowBinding {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
    Origin origin;
    int local;
    int depth;
    int captured;
} BorrowBinding;

typedef struct BorrowCheck {
    BorrowFunction *functions;
    int count;
    BorrowFunction *current;
    BorrowBinding *bindings;
    int binding_count;
    int depth;
    int changed;
    int failed;
} BorrowCheck;

static Origin
merge(Origin a, Origin b)
{
    a.parameters |= b.parameters;
    if(b.depth > a.depth)
        a.depth = b.depth;
    a.invalid |= b.invalid;
    return a;
}

static void
accumulate(BorrowCheck *check, Origin *destination, Origin source)
{
    Origin next = merge(*destination, source);
    if(next.parameters != destination->parameters || next.depth != destination->depth ||
       next.invalid != destination->invalid) {
        *destination = next;
        check->changed = 1;
    }
}

static void
reject(BorrowCheck *check, ZirSourceSpan span, const char *message)
{
    if(!check->failed)
        Diagnostic(span, "check.slice_lifetime", "%s", message);
    check->failed = 1;
}

static BorrowBinding *
binding(BorrowCheck *check, const char *name)
{
    for(int i = check->binding_count - 1; i >= 0; i--)
        if(!strcmp(check->bindings[i].name, name))
            return &check->bindings[i];
    return NULL;
}

static Origin expression_origin(BorrowCheck *check, int index);

static Origin
call_origin(BorrowCheck *check, const ZirExpr *expression)
{
    const ZirModule *owner = NULL;
    const ZirFunction *callee = NULL;
    if(ResolveFunction(check->current->module, expression->name, &owner, &callee) <= 0)
        return (Origin){0, 0, 1};
    BorrowFunction *summary = NULL;
    for(int i = 0; i < check->count; i++)
        if(check->functions[i].fn == callee)
            summary = &check->functions[i];
    if(summary == NULL)
        return (Origin){0, 0, 1};
    Origin result = {0, summary->returned.depth, summary->returned.invalid};
    for(int child = expression->first_child; child >= 0;
        child = check->current->fn->exprs[child].next_sibling) {
        int parameter = check->current->fn->exprs[child].argument_index;
        if(parameter >= 0 && parameter < 64 &&
           (summary->returned.parameters & (UINT64_C(1) << parameter)))
            result = merge(result, expression_origin(check, child));
    }
    return result;
}

static Origin
expression_origin(BorrowCheck *check, int index)
{
    if(index < 0)
        return (Origin){0};
    const ZirFunction *fn = check->current->fn;
    const ZirExpr *expression = &fn->exprs[index];
    switch(expression->kind) {
    case ZIR_EXPR_IDENT: {
        BorrowBinding *source = binding(check, expression->name);
        if(expression->type[0] != '[' && strchr(expression->type, '*') != NULL) {
            /* A direct pointer parameter owns no storage, but its caller
             * keeps the pointee live for this invocation. Views of its
             * fixed-array fields may be used here, never returned. */
            if(source != NULL && source->local < 0 &&
               source->origin.depth == 1 && !source->origin.invalid)
                return source->origin;
            return (Origin){0, 0, 1};
        }
        if(source == NULL)
            return (Origin){0}; /* Typed module-owned array or record storage. */
        if(SliceElementType(source->type, NULL, 0))
            return source->local >= 0 ? check->current->locals[source->local] : source->origin;
        return (Origin){0, source->depth, 0};
    }
    case ZIR_EXPR_MEMBER:
    case ZIR_EXPR_POINTER_MEMBER:
    case ZIR_EXPR_INDEX:
    case ZIR_EXPR_SLICE:
        return expression_origin(check, expression->left);
    case ZIR_EXPR_CONDITIONAL:
        return merge(expression_origin(check, expression->right),
                     expression_origin(check, expression->third));
    case ZIR_EXPR_CALL:
        if(SliceElementType(expression->type, NULL, 0))
            return call_origin(check, expression);
        return (Origin){0, 0, 1}; /* A returned array/record is temporary storage. */
    default:
        return (Origin){0, 0, 1};
    }
}

/* Inspect every range, including ranges passed directly to void/scalar calls. */
static void
check_ranges(BorrowCheck *check, int index)
{
    if(index < 0 || check->failed)
        return;
    const ZirExpr *expression = &check->current->fn->exprs[index];
    if(expression->kind == ZIR_EXPR_SLICE &&
       strcmp(check->current->fn->exprs[expression->left].type, "string") != 0 &&
       expression_origin(check, index).invalid)
        reject(check, expression->span, "slice range requires live owned backing storage");
    check_ranges(check, expression->left);
    check_ranges(check, expression->right);
    check_ranges(check, expression->third);
    for(int child = expression->first_child; child >= 0;
        child = check->current->fn->exprs[child].next_sibling)
        check_ranges(check, child);
}

static void
add_binding(BorrowCheck *check, const char *name, const char *type,
            Origin origin, int local, int depth, int captured)
{
    BorrowBinding *item = &check->bindings[check->binding_count++];
    copy_text(item->name, sizeof(item->name), name);
    copy_text(item->type, sizeof(item->type), type);
    item->origin = origin;
    item->local = local;
    item->depth = depth;
    item->captured = captured;
}

static void
check_function(BorrowCheck *check, BorrowFunction *function)
{
    const ZirFunction *fn = function->fn;
    check->current = function;
    check->binding_count = 0;
    check->depth = 1;
    check->bindings = calloc((size_t)fn->stmt_count + 65, sizeof(*check->bindings));
    if(check->bindings == NULL) {
        reject(check, fn->span, "out of memory checking slice lifetimes");
        return;
    }
    char parameters[64][ZIR_TEXT_MAX];
    int count = *skip_ws(fn->args) ?
        split_top_level(fn->args, parameters[0], 64, sizeof(parameters[0])) : 0;
    for(int i = 0; i < count; i++) {
        char *colon = strchr(parameters[i], ':');
        if(colon == NULL)
            continue;
        *colon++ = '\0';
        trim_in_place(parameters[i]);
        trim_in_place(colon);
        Origin origin = {0};
        int local = -1;
        if(SliceElementType(colon, NULL, 0)) {
            origin.parameters = UINT64_C(1) << i;
            local = fn->stmt_count + i;
            accumulate(check, &function->locals[local], origin);
        } else if(colon[0] == '*') {
            origin.depth = 1;
        }
        add_binding(check, parameters[i], colon, origin, local, 1, 0);
    }
    for(int i = 0; i < fn->stmt_count && !check->failed; i++) {
        const ZirStmt *statement = &fn->stmts[i];
        if(statement->kind == ZIR_STMT_BLOCK_CLOSE) {
            while(check->binding_count && check->bindings[check->binding_count - 1].depth == check->depth)
                check->binding_count--;
            check->depth--;
            continue;
        }
        check_ranges(check, statement->lhs_root);
        check_ranges(check, statement->expr_root);
        if(statement->kind == ZIR_STMT_DECL) {
            if(SliceElementType(statement->type, NULL, 0)) {
                Origin source = expression_origin(check, statement->expr_root);
                accumulate(check, &function->locals[i], source);
                if(source.invalid || source.depth > check->depth)
                    reject(check, statement->span, "slice initializer outlives its backing storage");
            }
            add_binding(check, statement->name, statement->type, (Origin){0}, i,
                        check->depth, 0);
        } else if(statement->kind == ZIR_STMT_ASSIGN && statement->lhs_root >= 0) {
            const ZirExpr *destination = &fn->exprs[statement->lhs_root];
            if(SliceElementType(destination->type, NULL, 0)) {
                BorrowBinding *target = destination->kind == ZIR_EXPR_IDENT ?
                    binding(check, destination->name) : NULL;
                Origin source = expression_origin(check, statement->expr_root);
                if(target == NULL || target->captured || source.invalid || source.depth > target->depth) {
                    reject(check, statement->span, "slice assignment may escape its backing storage");
                } else if(target->local >= 0) {
                    accumulate(check, &function->locals[target->local], source);
                }
            }
        } else if(statement->kind == ZIR_STMT_RETURN && SliceElementType(fn->return_type, NULL, 0)) {
            Origin source = expression_origin(check, statement->expr_root);
            if(source.invalid || source.depth > 0)
                reject(check, statement->span, "returned slice borrows local or temporary storage");
            accumulate(check, &function->returned, source);
        }
        if(statement->kind == ZIR_STMT_IF || statement->kind == ZIR_STMT_WHILE ||
           statement->kind == ZIR_STMT_BLOCK_OPEN)
            check->depth++;
    }
    free(check->bindings);
    check->bindings = NULL;
}

int
CheckSliceLifetimes(ZirProgram **programs, int count)
{
    BorrowCheck check = {0};
    for(int p = 0; p < count; p++)
        for(int m = 0; m < programs[p]->module_count; m++)
            check.count += programs[p]->modules[m].function_count;
    check.functions = calloc((size_t)check.count, sizeof(*check.functions));
    if(check.functions == NULL && check.count != 0)
        return 0;
    int index = 0;
    for(int p = 0; p < count; p++) {
        for(int m = 0; m < programs[p]->module_count; m++) {
            const ZirModule *module = &programs[p]->modules[m];
            for(int f = 0; f < module->function_count; f++) {
                BorrowFunction *function = &check.functions[index++];
                function->module = module;
                function->fn = &module->functions[f];
                function->locals = calloc((size_t)function->fn->stmt_count + 64, sizeof(Origin));
                if(function->locals == NULL)
                    check.failed = 1;
            }
        }
    }
    do {
        check.changed = 0;
        for(int i = 0; i < check.count && !check.failed; i++) {
            if(check.functions[i].fn->checked && !check.functions[i].fn->is_extern)
                check_function(&check, &check.functions[i]);
        }
    } while(check.changed && !check.failed);
    for(int i = 0; i < check.count; i++)
        free(check.functions[i].locals);
    free(check.functions);
    return !check.failed;
}
