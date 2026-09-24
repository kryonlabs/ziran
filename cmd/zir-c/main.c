/*
 * zi2c - .zi -> C compiler. Zir is the shared frontend: every .zi parses
 * into a ZirProgram (zir_parse.c) and lowers to C (zir_c_lower.c). All inputs
 * are parsed first so cross-module calls resolve through one symbol table.
 */
#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"
#include "zir_load.h"
#include "zir_c_lower.h"
#include "zir_c_plan9.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: zir_c [--strict] [--no-main] [--plan9] [--include-dir DIR] "
            "[--diagnostics=text|json] [--module-path DIR] --root DIR -o DIR file.zi|file.zir ...\n");
}

int
main(int argc, char **argv)
{
    const char *root = NULL;
    const char *out_dir = NULL;
    int no_main = 0;
    int strict = 0;
    int check_ok;
    int laws_ok;
    int plan9 = 0;
    int unresolved = 0;
    ProgramSet set = {0};
    const char *module_paths[64];
    int module_path_count = 0;
    ZirProgram **progs;
    ZirCModuleSyms *syms;
    int file_count;
    int i;
    int first_file = 0;

    for(i = 1; i < argc; i++) {
        if(strncmp(argv[i], "--diagnostics=", 14) == 0) {
            if(!SetDiagnosticFormat(argv[i] + 14)) {
                usage();
                return 1;
            }
        } else if(strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        } else if(strcmp(argv[i], "--module-path") == 0 && i + 1 < argc && module_path_count < 64) {
            module_paths[module_path_count++] = argv[++i];
        } else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if(strcmp(argv[i], "--no-main") == 0) {
            no_main = 1;
        } else if(strcmp(argv[i], "--plan9") == 0) {
            plan9 = 1;
        } else if(strcmp(argv[i], "--include-dir") == 0 && i + 1 < argc) {
            c_plan9_add_include_dir(argv[++i]);
        } else if(strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else if(strcmp(argv[i], "--no-strict") == 0) {
            strict = 0;
        } else if(argv[i][0] == '-') {
            usage();
            return 1;
        } else {
            first_file = i;
            break;
        }
    }
    if(root == NULL || out_dir == NULL || first_file == 0) {
        usage();
        return 1;
    }
    if(!ProgramsLoad(&set, root, module_paths, module_path_count,
                     (const char *const *)(argv + first_file), argc - first_file))
        return 1;
    file_count = set.count;
    progs = set.programs;
    syms = calloc((size_t)file_count, sizeof(*syms));
    if(syms == NULL) {
        fprintf(stderr, "zi2c: out of memory\n");
        ProgramsFree(&set);
        return 1;
    }
    c_plan9_set_enabled(plan9);
    /* Pass 1: build the cross-module symbol table. */
    for(i = 0; i < file_count; i++)
        c_build_syms(progs[i], &syms[i]);
    check_ok = CheckCanonicalPrograms(progs, file_count, strict,
                                      (const char *const *)set.paths);
    laws_ok = CheckLaws(progs, file_count);
    if(!check_ok || !laws_ok) {
        ProgramsFree(&set);
        free(syms);
        return 1;
    }
    /* Pass 2: lower with full cross-module resolution. */
    for(i = 0; i < file_count; i++)
        c_lower(progs[i], root, out_dir, syms, file_count);
    (void)no_main;
    ProgramsFree(&set);
    free(syms);
    unresolved = c_plan9_unresolved();
    if(plan9 && unresolved > 0) {
        /* Unresolved declarations are usually inside platform guards the
         * native build compiles out; the in-guest compile is the final
         * arbiter, so warn rather than fail. */
        fprintf(stderr,
                "zi2c: --plan9 left %d __auto_type declarations unresolved "
                "(guarded code compiles out; the rest must be resolvable)\n",
                unresolved);
    }
    return 0;
}
