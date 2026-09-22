#include "zir_vm.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_text.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { VM_MAX_PARAMS = 16, VM_MAX_LOCALS = 64, VM_MAX_DEPTH = 128,
       VM_MAX_STEPS = 1000000 };

typedef struct Parameter {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
} Parameter;

typedef struct Local {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
    int64_t value;
} Local;

typedef struct Vm {
    int depth;
    int steps;
    int failed;
} Vm;

typedef struct Frame {
    Vm *vm;
    const ZirModule *module;
    const ZirFunction *function;
    Local locals[VM_MAX_LOCALS];
    int local_count;
} Frame;

static int
scalar_type(const char *type)
{
    return strcmp(type, "i32") == 0 || strcmp(type, "int") == 0 ||
           strcmp(type, "bool") == 0 || strcmp(type, "void") == 0;
}

static int64_t
coerce(int64_t value, const char *type)
{
    if(strcmp(type, "bool") == 0)
        return value != 0;
    if(strcmp(type, "i32") == 0 || strcmp(type, "int") == 0)
        return (int32_t)value;
    return value;
}

static int
parse_parameters(const ZirFunction *function, Parameter *parameters)
{
    const char *cursor = function->args;
    int count = 0;
    while(*cursor != 0) {
        const char *start;
        size_t length;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor == 0)
            break;
        if(count >= VM_MAX_PARAMS)
            return -1;
        start = cursor;
        while((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')
            cursor++;
        length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX ||
           (start[0] >= '0' && start[0] <= '9'))
            return -1;
        memcpy(parameters[count].name, start, length);
        parameters[count].name[length] = 0;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor++ != ':')
            return -1;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        start = cursor;
        while((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')
            cursor++;
        length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX)
            return -1;
        memcpy(parameters[count].type, start, length);
        parameters[count].type[length] = 0;
        if(!scalar_type(parameters[count].type) ||
           strcmp(parameters[count].type, "void") == 0)
            return -1;
        count++;
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor == 0)
            break;
        if(*cursor++ != ',')
            return -1;
        if(*cursor == 0)
            return -1;
    }
    return count;
}

static int
binding_index(const Parameter *bindings, int count, const char *name)
{
    for(int i = count - 1; i >= 0; i--)
        if(strcmp(bindings[i].name, name) == 0)
            return i;
    return -1;
}

static int
binary_operator(const char *op)
{
    static const char *const supported[] = {
        "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=",
        "&&", "||", NULL
    };
    for(int i = 0; supported[i] != NULL; i++)
        if(strcmp(op, supported[i]) == 0)
            return 1;
    return 0;
}

static int
verify_expression(const ZirModule *module, const ZirFunction *function,
                  const Parameter *bindings, int binding_count,
                  int index, int depth)
{
    const ZirExpr *expression;
    const ZirModule *owner = NULL;
    const ZirFunction *callee = NULL;
    int children = 0;
    Parameter parameters[VM_MAX_PARAMS];
    if(index < 0 || index >= function->expr_count || depth >= VM_MAX_DEPTH)
        return 0;
    expression = &function->exprs[index];
    switch(expression->kind) {
    case ZIR_EXPR_INT: {
        char *end;
        long long number;
        errno = 0;
        number = strtoll(expression->text, &end, 0);
        return errno == 0 && end != expression->text && *end == 0 &&
               number >= INT32_MIN && number <= INT32_MAX;
    }
    case ZIR_EXPR_IDENT:
        return strcmp(expression->name, "true") == 0 ||
               strcmp(expression->name, "false") == 0 ||
               binding_index(bindings, binding_count, expression->name) >= 0;
    case ZIR_EXPR_UNARY:
        return (strcmp(expression->op, "+") == 0 ||
                strcmp(expression->op, "-") == 0 ||
                strcmp(expression->op, "!") == 0 ||
                strcmp(expression->op, "~") == 0) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1);
    case ZIR_EXPR_BINARY:
        return binary_operator(expression->op) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->left, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1);
    case ZIR_EXPR_CAST:
        return scalar_type(expression->name) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1);
    case ZIR_EXPR_CONDITIONAL:
        return verify_expression(module, function, bindings, binding_count,
                                 expression->left, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->right, depth + 1) &&
               verify_expression(module, function, bindings, binding_count,
                                 expression->third, depth + 1);
    case ZIR_EXPR_CALL:
        if(expression->name[0] == 0 ||
           ResolveFunction(module, expression->name, &owner, &callee) != 1 ||
           callee == NULL || callee->is_extern)
            return 0;
        for(int child = expression->first_child; child >= 0;
            child = function->exprs[child].next_sibling) {
            if(++children > VM_MAX_PARAMS ||
               !verify_expression(module, function, bindings, binding_count,
                                  child, depth + 1))
                return 0;
        }
        return parse_parameters(callee, parameters) == children;
    default:
        return 0;
    }
}

static const ZirFunction *
find_entry(const ZirProgram *program, const char *module_name,
           const char *function_name, const ZirModule **module_out)
{
    const ZirFunction *entry = NULL;
    *module_out = NULL;
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        if(strcmp(module->name, module_name) != 0)
            continue;
        if(*module_out != NULL)
            return NULL;
        *module_out = module;
        for(int f = 0; f < module->function_count; f++)
            if(strcmp(module->functions[f].name, function_name) == 0) {
                if(entry != NULL)
                    return NULL;
                entry = &module->functions[f];
            }
    }
    return entry;
}

int
VmVerify(const ZirProgram *program, const char *entry_module,
            const char *entry_function)
{
    const ZirModule *entry_owner;
    const ZirFunction *entry;
    Parameter bindings[VM_MAX_LOCALS];
    if(program == NULL || entry_module == NULL || entry_function == NULL)
        return 0;
    entry = find_entry(program, entry_module, entry_function, &entry_owner);
    if(entry == NULL || entry->is_extern || parse_parameters(entry, bindings) != 0) {
        Diagnostic(Span("<bundle>", 1, 1), "zib.entry",
                      "entry must be one unique zero-argument Ziran function");
        return 0;
    }
    for(int m = 0; m < program->module_count; m++) {
        const ZirModule *module = &program->modules[m];
        for(int other = 0; other < m; other++)
            if(strcmp(program->modules[other].name, module->name) == 0) {
                Diagnostic(module->span, "zib.module",
                              "duplicate module identity: %s", module->name);
                return 0;
            }
        for(int i = 0; i < module->import_count; i++)
            if(module->imports[i].kind != ZIR_IMPORT_HEADER ||
               module->imports[i].resolved_module == NULL) {
                Diagnostic(module->imports[i].span, "zib.capability",
                              "portable scalar runner requires an included Ziran module");
                return 0;
            }
        if(module->global_count || module->state_count || module->define_count) {
            Diagnostic(module->span, "zib.module",
                          "globals, state, and defines are outside the portable scalar subset");
            return 0;
        }
        for(int f = 0; f < module->function_count; f++) {
            const ZirFunction *function = &module->functions[f];
            int binding_count = parse_parameters(function, bindings);
            if(!function->checked || function->is_extern ||
               !scalar_type(function->return_type) ||
               binding_count < 0) {
                Diagnostic(function->span, "zib.function",
                              "function is outside the portable scalar subset: %s",
                              function->name);
                return 0;
            }
            if(strcmp(function->return_type, "void") != 0 &&
               (function->stmt_count == 0 ||
                function->stmts[function->stmt_count - 1].kind != ZIR_STMT_RETURN)) {
                Diagnostic(function->span, "zib.return",
                           "portable scalar functions require a final return: %s",
                           function->name);
                return 0;
            }
            for(int s = 0; s < function->stmt_count; s++) {
                const ZirStmt *statement = &function->stmts[s];
                int valid = 0;
                switch(statement->kind) {
                case ZIR_STMT_DECL:
                    valid = !statement->is_instance &&
                            scalar_type(statement->type) &&
                            strcmp(statement->type, "void") != 0 &&
                            statement->name[0] != 0 &&
                            binding_count < VM_MAX_LOCALS &&
                            (statement->expr_root < 0 ||
                             verify_expression(module, function,
                                               bindings, binding_count,
                                               statement->expr_root, 0));
                    if(valid) {
                        copy_text(bindings[binding_count].name,
                                 sizeof(bindings[binding_count].name),
                                 statement->name);
                        copy_text(bindings[binding_count].type,
                                 sizeof(bindings[binding_count].type),
                                 statement->type);
                        binding_count++;
                    }
                    break;
                case ZIR_STMT_ASSIGN:
                    valid = statement->lhs_root >= 0 &&
                            function->exprs[statement->lhs_root].kind == ZIR_EXPR_IDENT &&
                            binding_index(bindings, binding_count,
                                          function->exprs[statement->lhs_root].name) >= 0 &&
                            strcmp(statement->assignment_op, "=") == 0 &&
                            statement->expr_root >= 0 &&
                            verify_expression(module, function, bindings,
                                              binding_count, statement->expr_root, 0);
                    break;
                case ZIR_STMT_RETURN:
                    valid = statement->expr_root < 0 ||
                            verify_expression(module, function, bindings,
                                              binding_count, statement->expr_root, 0);
                    break;
                case ZIR_STMT_EXPR:
                case ZIR_STMT_UNUSED:
                    valid = statement->expr_root >= 0 &&
                            verify_expression(module, function, bindings,
                                              binding_count, statement->expr_root, 0);
                    break;
                default:
                    break;
                }
                if(!valid) {
                    Diagnostic(statement->span, "zib.statement",
                                  "statement is outside the portable scalar subset");
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int64_t run_function(Vm *vm, const ZirModule *module,
                            const ZirFunction *function, const int64_t *args,
                            int arg_count);

static int64_t
eval(Frame *frame, int index, int depth)
{
    const ZirExpr *expression;
    int64_t left, right, value = 0;
    if(frame->vm->failed || index < 0 || index >= frame->function->expr_count ||
       depth >= VM_MAX_DEPTH) {
        frame->vm->failed = 1;
        return 0;
    }
    expression = &frame->function->exprs[index];
    switch(expression->kind) {
    case ZIR_EXPR_INT: {
        char *end;
        errno = 0;
        value = strtoll(expression->text, &end, 0);
        if(errno || end == expression->text || *end)
            frame->vm->failed = 1;
        break;
    }
    case ZIR_EXPR_IDENT:
        if(strcmp(expression->name, "true") == 0)
            return 1;
        if(strcmp(expression->name, "false") == 0)
            return 0;
        for(int i = frame->local_count - 1; i >= 0; i--)
            if(strcmp(frame->locals[i].name, expression->name) == 0)
                return frame->locals[i].value;
        frame->vm->failed = 1;
        break;
    case ZIR_EXPR_UNARY:
        right = eval(frame, expression->right, depth + 1);
        if(strcmp(expression->op, "-") == 0) value = -right;
        else if(strcmp(expression->op, "+") == 0) value = right;
        else if(strcmp(expression->op, "!") == 0) value = !right;
        else if(strcmp(expression->op, "~") == 0) value = ~right;
        else frame->vm->failed = 1;
        break;
    case ZIR_EXPR_BINARY:
        left = eval(frame, expression->left, depth + 1);
        if(strcmp(expression->op, "&&") == 0 && !left) return 0;
        if(strcmp(expression->op, "||") == 0 && left) return 1;
        right = eval(frame, expression->right, depth + 1);
        if(strcmp(expression->op, "+") == 0) value = left + right;
        else if(strcmp(expression->op, "-") == 0) value = left - right;
        else if(strcmp(expression->op, "*") == 0) value = left * right;
        else if(strcmp(expression->op, "/") == 0 && right) value = left / right;
        else if(strcmp(expression->op, "%") == 0 && right) value = left % right;
        else if(strcmp(expression->op, "==") == 0) value = left == right;
        else if(strcmp(expression->op, "!=") == 0) value = left != right;
        else if(strcmp(expression->op, "<") == 0) value = left < right;
        else if(strcmp(expression->op, "<=") == 0) value = left <= right;
        else if(strcmp(expression->op, ">") == 0) value = left > right;
        else if(strcmp(expression->op, ">=") == 0) value = left >= right;
        else if(strcmp(expression->op, "&&") == 0) value = left && right;
        else if(strcmp(expression->op, "||") == 0) value = left || right;
        else frame->vm->failed = 1;
        break;
    case ZIR_EXPR_CAST:
        value = coerce(eval(frame, expression->right, depth + 1), expression->name);
        break;
    case ZIR_EXPR_CONDITIONAL:
        left = eval(frame, expression->left, depth + 1);
        value = eval(frame, left ? expression->right : expression->third,
                     depth + 1);
        break;
    case ZIR_EXPR_CALL: {
        int64_t args[VM_MAX_PARAMS];
        int count = 0;
        const ZirModule *owner = NULL;
        const ZirFunction *callee = NULL;
        if(ResolveFunction(frame->module, expression->name,
                              &owner, &callee) != 1 || callee == NULL) {
            frame->vm->failed = 1;
            break;
        }
        for(int child = expression->first_child; child >= 0;
            child = frame->function->exprs[child].next_sibling) {
            if(count >= VM_MAX_PARAMS) {
                frame->vm->failed = 1;
                break;
            }
            args[count++] = eval(frame, child, depth + 1);
        }
        if(!frame->vm->failed)
            value = run_function(frame->vm, owner, callee, args, count);
        break;
    }
    default:
        frame->vm->failed = 1;
        break;
    }
    return coerce(value, scalar_type(expression->type) ? expression->type : "i32");
}

static int64_t
run_function(Vm *vm, const ZirModule *module, const ZirFunction *function,
             const int64_t *args, int arg_count)
{
    Frame frame = {0};
    Parameter parameters[VM_MAX_PARAMS];
    int count = parse_parameters(function, parameters);
    int64_t result = 0;
    if(vm->failed || ++vm->depth > VM_MAX_DEPTH || count != arg_count) {
        vm->failed = 1;
        return 0;
    }
    frame.vm = vm;
    frame.module = module;
    frame.function = function;
    for(int i = 0; i < count; i++) {
        copy_text(frame.locals[i].name, sizeof(frame.locals[i].name),
                 parameters[i].name);
        copy_text(frame.locals[i].type, sizeof(frame.locals[i].type),
                 parameters[i].type);
        frame.locals[i].value = coerce(args[i], parameters[i].type);
    }
    frame.local_count = count;
    for(int s = 0; s < function->stmt_count && !vm->failed; s++) {
        const ZirStmt *statement = &function->stmts[s];
        if(++vm->steps > VM_MAX_STEPS) {
            vm->failed = 1;
            break;
        }
        switch(statement->kind) {
        case ZIR_STMT_DECL:
            if(frame.local_count >= VM_MAX_LOCALS) {
                vm->failed = 1;
                break;
            }
            copy_text(frame.locals[frame.local_count].name,
                     sizeof(frame.locals[frame.local_count].name), statement->name);
            copy_text(frame.locals[frame.local_count].type,
                     sizeof(frame.locals[frame.local_count].type), statement->type);
            frame.locals[frame.local_count++].value =
                statement->expr_root >= 0 ?
                coerce(eval(&frame, statement->expr_root, 0), statement->type) : 0;
            break;
        case ZIR_STMT_ASSIGN: {
            const char *name = function->exprs[statement->lhs_root].name;
            int i = frame.local_count - 1;
            while(i >= 0 && strcmp(frame.locals[i].name, name) != 0)
                i--;
            if(i < 0 || strcmp(statement->assignment_op, "=") != 0) {
                vm->failed = 1;
                break;
            }
            frame.locals[i].value = coerce(eval(&frame, statement->expr_root, 0),
                                           frame.locals[i].type);
            break;
        }
        case ZIR_STMT_RETURN:
            if(statement->expr_root >= 0)
                result = eval(&frame, statement->expr_root, 0);
            vm->depth--;
            return coerce(result, function->return_type);
        case ZIR_STMT_EXPR:
        case ZIR_STMT_UNUSED:
            (void)eval(&frame, statement->expr_root, 0);
            break;
        default:
            vm->failed = 1;
            break;
        }
    }
    vm->depth--;
    return coerce(result, function->return_type);
}

int
VmRun(const ZirProgram *program, const char *entry_module,
         const char *entry_function, long long *result, int *has_result)
{
    const ZirModule *module;
    const ZirFunction *entry;
    Vm vm = {0};
    if(!VmVerify(program, entry_module, entry_function))
        return 0;
    entry = find_entry(program, entry_module, entry_function, &module);
    *result = run_function(&vm, module, entry, NULL, 0);
    *has_result = strcmp(entry->return_type, "void") != 0;
    if(vm.failed) {
        Diagnostic(entry->span, "zib.runtime",
                      "portable execution failed in the scalar subset");
        return 0;
    }
    return 1;
}
