#include "zir.h"
#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"
#include "zir_load.h"
#include "zir_vm.h"
#include "ziran_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: zi2zib bundle [--module-path DIR] [--bind module:function=module:function] --root DIR --entry module:function -o FILE file.zi|file.zir ...\n"
            "       zi2zib run file.zib\n");
}

static int
split_entry(const char *text, char *module, char *function)
{
    const char *separator = text ? strrchr(text, ':') : NULL;
    size_t length;
    if(separator == NULL || separator == text || separator[1] == 0)
        return 0;
    length = (size_t)(separator - text);
    if(length >= ZIR_NAME_MAX || strlen(separator + 1) >= ZIR_NAME_MAX)
        return 0;
    memcpy(module, text, length);
    module[length] = 0;
    strcpy(function, separator + 1);
    return 1;
}

static int
bundle_command(int argc, char **argv)
{
    const char *root = NULL;
    const char *output = NULL;
    const char *entry = NULL;
    int first_file = 0;
    int count, result = 1;
    char entry_module[ZIR_NAME_MAX], entry_function[ZIR_NAME_MAX];
    ProgramSet set = {0};
    const char *module_paths[64];
    int module_path_count = 0;
    const char *bindings[64];
    int binding_count = 0;
    ZirProgram **programs = NULL;
    ZirProgram merged = {0};
    ZirProgram *linked = NULL;
    FILE *file = NULL;
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            root = argv[++i];
        else if(strcmp(argv[i], "--module-path") == 0 && i + 1 < argc && module_path_count < 64)
            module_paths[module_path_count++] = argv[++i];
        else if(strcmp(argv[i], "--bind") == 0 && i + 1 < argc && binding_count < 64)
            bindings[binding_count++] = argv[++i];
        else if(strcmp(argv[i], "--entry") == 0 && i + 1 < argc)
            entry = argv[++i];
        else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output = argv[++i];
        else if(strncmp(argv[i], "--diagnostics=", 14) == 0) {
            if(!SetDiagnosticFormat(argv[i] + 14))
                return 1;
        } else if(argv[i][0] == '-') {
            usage();
            return 1;
        } else {
            first_file = i;
            break;
        }
    }
    if(root == NULL || output == NULL || first_file == 0 ||
       !split_entry(entry, entry_module, entry_function)) {
        usage();
        return 1;
    }
    if(!ProgramsLoad(&set, root, module_paths, module_path_count,
                     (const char *const *)(argv + first_file), argc - first_file))
        goto done;
    count = set.count;
    programs = set.programs;
    for(int i = 0; i < count; i++)
        merged.module_count += programs[i]->module_count;
    if(merged.module_count == 0 ||
       !CheckCanonicalPrograms(programs, count,
                               (const char *const *)set.paths))
        goto done;
    merged.modules = calloc((size_t)merged.module_count, sizeof(*merged.modules));
    if(merged.modules == NULL)
        goto done;
    int position = 0;
    for(int i = 0; i < count; i++)
        for(int m = 0; m < programs[i]->module_count; m++)
            merged.modules[position++] = programs[i]->modules[m];
    ZirProgram *merged_ptr = &merged;
    if(!LinkImports(&merged_ptr, 1))
        goto done;
    for(int binding = 0; binding < binding_count; binding++) {
        const char *spec = bindings[binding];
        const char *equals = strchr(spec, '=');
        char source[2 * ZIR_NAME_MAX], provider[2 * ZIR_NAME_MAX];
        char source_module[ZIR_NAME_MAX], source_function[ZIR_NAME_MAX];
        char provider_module[ZIR_NAME_MAX], provider_function[ZIR_NAME_MAX];
        ZirImport *matched = NULL;
        const ZirFunction *implementation = NULL;
        if(equals == NULL || (size_t)(equals - spec) >= sizeof(source) ||
           strlen(equals + 1) >= sizeof(provider)) {
            Diagnostic(Span("<command>", 1, 1), "zib.bind",
                       "invalid host binding: %s", spec);
            goto done;
        }
        memcpy(source, spec, (size_t)(equals - spec));
        source[equals - spec] = '\0';
        strcpy(provider, equals + 1);
        if(!split_entry(source, source_module, source_function) ||
           !split_entry(provider, provider_module, provider_function)) {
            Diagnostic(Span("<command>", 1, 1), "zib.bind",
                       "invalid host binding: %s", spec);
            goto done;
        }
        for(int m = 0; m < merged.module_count; m++) {
            ZirModule *module = &merged.modules[m];
            if(strcmp(module->name, source_module) == 0)
                for(int i = 0; i < module->import_count; i++)
                    if(module->imports[i].kind == ZIR_IMPORT_EXTERN &&
                       module->imports[i].extern_kind == ZIR_EXTERN_HOST &&
                       strcmp(module->imports[i].name, source_function) == 0) {
                        if(matched != NULL) {
                            Diagnostic(module->imports[i].span, "zib.bind",
                                       "ambiguous host capability: %s", source);
                            goto done;
                        }
                        matched = &module->imports[i];
                    }
            if(strcmp(module->name, provider_module) == 0)
                for(int i = 0; i < module->function_count; i++)
                    if(strcmp(module->functions[i].name, provider_function) == 0 &&
                       module->functions[i].exported &&
                       !module->functions[i].is_extern) {
                        if(implementation != NULL) {
                            Diagnostic(module->functions[i].span, "zib.bind",
                                       "ambiguous Ziran provider: %s", provider);
                            goto done;
                        }
                        implementation = &module->functions[i];
                    }
        }
        if(matched == NULL || implementation == NULL ||
           strncmp(matched->target, "ziran:", 6) == 0) {
            Diagnostic(Span("<command>", 1, 1), "zib.bind",
                       "host capability or exported Ziran provider is missing or already bound: %s", spec);
            goto done;
        }
        snprintf(matched->target, sizeof(matched->target), "ziran:%s",
                 provider_module);
        snprintf(matched->extern_symbol, sizeof(matched->extern_symbol), "%s",
                 provider_function);
    }
    linked = BundleLink(&merged, entry_module, entry_function);
    if(linked == NULL || !VmVerify(linked, entry_module, entry_function))
        goto done;
    file = fopen(output, "wb");
    if(file == NULL) {
        Diagnostic(Span(output, 1, 1), "zib.output",
                      "cannot open bundle output");
        goto done;
    }
    if(!BundleWrite(file, linked, entry_module, entry_function)) {
        Diagnostic(Span(output, 1, 1), "zib.output",
                      "cannot write bundle");
        goto done;
    }
    result = 0;
done:
    if(file != NULL && fclose(file) != 0)
        result = 1;
    if(result != 0 && file != NULL)
        remove(output);
    ProgramFree(linked);
    free(merged.modules);
    ProgramsFree(&set);
    return result;
}

static int
run_command(int argc, char **argv)
{
    long long result;
    int has_result;
    if(argc != 1) {
        usage();
        return 1;
    }
    Bundle *bundle = BundleOpen(argv[0]);
    if(bundle == NULL)
        return 1;
    int ok = BundleRun(bundle, NULL, 0, &result, &has_result);
    if(ok && has_result)
        printf("%lld\n", result);
    BundleClose(bundle);
    return ok ? 0 : 1;
}

int
main(int argc, char **argv)
{
    if(argc > 1 && strcmp(argv[1], "bundle") == 0)
        return bundle_command(argc - 2, argv + 2);
    if(argc > 1 && strcmp(argv[1], "run") == 0)
        return run_command(argc - 2, argv + 2);
    usage();
    return 1;
}
