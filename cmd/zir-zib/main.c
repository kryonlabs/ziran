#include "zir.h"
#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_laws.h"
#include "zir_serial.h"
#include "zir_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: ziran bundle --root DIR --entry module:function -o FILE file.zi|file.zir ...\n"
            "       ziran run file.zib\n");
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
    ZirProgram **programs = NULL;
    ZirProgram merged = {0};
    ZirProgram *linked = NULL;
    FILE *file = NULL;
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            root = argv[++i];
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
    count = argc - first_file;
    programs = calloc((size_t)count, sizeof(*programs));
    if(programs == NULL)
        return 1;
    for(int i = 0; i < count; i++) {
        programs[i] = ProgramLoad(argv[first_file + i], root);
        if(programs[i] == NULL)
            goto done;
        merged.module_count += programs[i]->module_count;
    }
    if(merged.module_count == 0 ||
       !CheckCanonicalPrograms(programs, count, 1,
                               (const char *const *)(argv + first_file)) ||
       !CheckLaws(programs, count))
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
    for(int i = 0; i < count; i++)
        ProgramFree(programs[i]);
    free(programs);
    return result;
}

static int
run_command(int argc, char **argv)
{
    char entry_module[ZIR_NAME_MAX], entry_function[ZIR_NAME_MAX];
    long long result;
    int has_result;
    FILE *file;
    ZirProgram *program;
    if(argc != 1) {
        usage();
        return 1;
    }
    file = fopen(argv[0], "rb");
    if(file == NULL) {
        Diagnostic(Span(argv[0], 1, 1), "zib.input",
                      "cannot open bundle");
        return 1;
    }
    program = BundleRead(file, argv[0], entry_module,
                            sizeof(entry_module), entry_function,
                            sizeof(entry_function));
    fclose(file);
    if(program == NULL)
        return 1;
    int ok = VmRun(program, entry_module, entry_function,
                      &result, &has_result);
    if(ok && has_result)
        printf("%lld\n", result);
    ProgramFree(program);
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
