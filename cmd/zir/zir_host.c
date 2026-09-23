#include "ziran_host.h"
#include "zir_bundle.h"
#include "zir_diagnostic.h"
#include "zir_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Bundle {
    ZirProgram *program;
    char entry_module[ZIR_NAME_MAX];
    char entry_function[ZIR_NAME_MAX];
};

static const ZirImport *
capability_at(const Bundle *bundle, size_t index, const ZirModule **owner)
{
    if(bundle == NULL)
        return NULL;
    for(int m = 0; m < bundle->program->module_count; m++) {
        const ZirModule *module = &bundle->program->modules[m];
        for(int i = 0; i < module->import_count; i++) {
            if(module->imports[i].kind != ZIR_IMPORT_EXTERN)
                continue;
            if(index-- == 0) {
                if(owner != NULL)
                    *owner = module;
                return &module->imports[i];
            }
        }
    }
    return NULL;
}

Bundle *
BundleOpen(const char *path)
{
    FILE *file;
    Bundle *bundle;
    if(path == NULL)
        return NULL;
    file = fopen(path, "rb");
    if(file == NULL) {
        Diagnostic(Span(path, 1, 1), "zib.input", "cannot open bundle");
        return NULL;
    }
    bundle = calloc(1, sizeof(*bundle));
    if(bundle != NULL)
        bundle->program = BundleRead(file, path, bundle->entry_module,
                                     sizeof(bundle->entry_module),
                                     bundle->entry_function,
                                     sizeof(bundle->entry_function));
    fclose(file);
    if(bundle == NULL)
        return NULL;
    if(bundle->program == NULL ||
       !VmVerify(bundle->program, bundle->entry_module,
                 bundle->entry_function)) {
        BundleClose(bundle);
        return NULL;
    }
    return bundle;
}

void
BundleClose(Bundle *bundle)
{
    if(bundle != NULL) {
        ProgramFree(bundle->program);
        free(bundle);
    }
}

size_t
BundleCapabilityCount(const Bundle *bundle)
{
    size_t count = 0;
    if(bundle != NULL)
        for(int m = 0; m < bundle->program->module_count; m++)
            for(int i = 0; i < bundle->program->modules[m].import_count; i++)
                count += bundle->program->modules[m].imports[i].kind ==
                         ZIR_IMPORT_EXTERN;
    return count;
}

const char *
BundleCapabilityModule(const Bundle *bundle, size_t index)
{
    const ZirModule *owner = NULL;
    return capability_at(bundle, index, &owner) != NULL ? owner->name : NULL;
}

const char *
BundleCapabilityFunction(const Bundle *bundle, size_t index)
{
    const ZirImport *import = capability_at(bundle, index, NULL);
    return import != NULL ? import->name : NULL;
}

typedef struct BindingSet {
    const HostBinding *bindings;
    size_t count;
} BindingSet;

static const HostBinding *
find_binding(const BindingSet *set, const char *module, const char *function)
{
    for(size_t i = 0; i < set->count; i++)
        if(strcmp(set->bindings[i].module, module) == 0 &&
           strcmp(set->bindings[i].function, function) == 0)
            return &set->bindings[i];
    return NULL;
}

static int
dispatch(void *context, const char *module, const char *function,
         const VmHostValue *args, int arg_count, VmHostValue *result)
{
    const BindingSet *set = context;
    const HostBinding *binding = find_binding(set, module, function);
    return binding != NULL && binding->call(binding->context, module,
                                            function, args, arg_count, result);
}

int
BundleRun(const Bundle *bundle, const HostBinding *bindings,
          size_t binding_count, long long *result, int *has_result)
{
    BindingSet set = {bindings, binding_count};
    if(bundle == NULL || result == NULL || has_result == NULL ||
       (binding_count != 0 && bindings == NULL))
        return 0;
    for(size_t i = 0; i < binding_count; i++) {
        if(bindings[i].module == NULL || bindings[i].function == NULL ||
           bindings[i].call == NULL)
            return 0;
        for(size_t j = 0; j < i; j++)
            if(strcmp(bindings[i].module, bindings[j].module) == 0 &&
               strcmp(bindings[i].function, bindings[j].function) == 0) {
                Diagnostic(Span("<host>", 1, 1), "zib.capability",
                           "duplicate host capability binding: %s:%s",
                           bindings[i].module, bindings[i].function);
                return 0;
            }
    }
    for(size_t i = 0; i < BundleCapabilityCount(bundle); i++) {
        const char *module = BundleCapabilityModule(bundle, i);
        const char *function = BundleCapabilityFunction(bundle, i);
        if(find_binding(&set, module, function) == NULL) {
            Diagnostic(Span("<host>", 1, 1), "zib.capability",
                       "missing host capability: %s:%s", module, function);
            return 0;
        }
    }
    return VmRunWithHost(bundle->program, bundle->entry_module,
                          bundle->entry_function, dispatch, &set,
                          result, has_result);
}
