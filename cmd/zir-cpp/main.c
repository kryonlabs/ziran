/*
 * ziran-cpp - .zi -> C++ compiler. Zir is the shared frontend: every .zi parses
 * into a ZirProgram and lowers to C++ source. All inputs are loaded before
 * cross-module symbol resolution.
 */
#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"
#include "zir_cpp_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: ziran-cpp [--strict] [--no-main] "
            "[--diagnostics=text|json] --root DIR -o DIR file.zi ...\n");
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
    int all_ir = 1;
    ZirProgram **progs;
    ZirCppModuleSyms *syms;
    int file_count;
    int i;
    int first_file = 0;

    for(i = 1; i < argc; i++) {
        if(strncmp(argv[i], "--diagnostics=", 14) == 0) {
            if(!ZirSetDiagnosticFormat(argv[i] + 14)) {
                usage();
                return 1;
            }
        } else if(strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        } else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if(strcmp(argv[i], "--no-main") == 0) {
            no_main = 1;
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
    file_count = argc - first_file;
    progs = calloc((size_t)file_count, sizeof(*progs));
    syms = calloc((size_t)file_count, sizeof(*syms));
    if(progs == NULL || syms == NULL) {
        fprintf(stderr, "ziran-cpp: out of memory\n");
        return 1;
    }
    /* Pass 1: parse every file, build the cross-module symbol table. */
    for(i = 0; i < file_count; i++) {
        all_ir &= ZirPathIsZir(argv[first_file + i]);
        progs[i] = ZirProgramLoad(argv[first_file + i], root);
        if(progs[i] == NULL) {
            fprintf(stderr, "ziran-cpp: failed to parse %s\n",
                    argv[first_file + i]);
            return 1;
        }
        zir_cpp_build_syms(progs[i], &syms[i]);
    }
    check_ok = all_ir ? ZirLinkImports(progs, file_count) :
                        ZirCheckPrograms(progs, file_count, strict);
    laws_ok = ZirCheckLaws(progs, file_count);
    if(!check_ok || !laws_ok) {
        for(i = 0; i < file_count; i++) ZirProgramFree(progs[i]);
        free(progs);
        free(syms);
        return 1;
    }
    /* Pass 2: lower with full cross-module resolution. */
    for(i = 0; i < file_count; i++)
        zir_cpp_lower(progs[i], root, out_dir, syms, file_count);
    (void)no_main;
    for(i = 0; i < file_count; i++)
        ZirProgramFree(progs[i]);
    free(progs);
    free(syms);
    return 0;
}
