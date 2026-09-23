/*
 * ziran-go - .zi -> native Go compiler. Uses the shared Ziran frontend.
 */
#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"
#include "zir_emit.h"
#include "zir_go_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: ziran-go [--strict] [--no-main] [--runtime-implementation] [--minify] [--pkg NAME] "
            "[--diagnostics=text|json] --root DIR -o DIR file.zi ...\n");
}

int
main(int argc, char **argv)
{
    const char *root = NULL;
    const char *out_dir = NULL;
    const char *pkg = "ziran";
    int no_main = 0;
    int strict = 0;
    int minify = 0;
    int check_ok;
    int laws_ok;
    int runtime_implementation = 0;
    ZirProgram **progs;
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
        } else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if(strcmp(argv[i], "--pkg") == 0 && i + 1 < argc) {
            pkg = argv[++i];
        } else if(strcmp(argv[i], "--no-main") == 0) {
            no_main = 1;
        } else if(strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else if(strcmp(argv[i], "--no-strict") == 0) {
            strict = 0;
        } else if(strcmp(argv[i], "--runtime-implementation") == 0) {
            runtime_implementation = 1;
        } else if(strcmp(argv[i], "--minify") == 0) {
            minify = 1;
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
    if(runtime_implementation && !no_main) {
        fprintf(stderr, "ziran-go: --runtime-implementation requires --no-main\n");
        return 1;
    }
    EmitUseMinifiedOutput(minify);

    file_count = argc - first_file;
    progs = calloc((size_t)file_count, sizeof(*progs));
    if(progs == NULL) {
        fprintf(stderr, "ziran-go: out of memory\n");
        return 1;
    }
    for(i = 0; i < file_count; i++) {
        progs[i] = ProgramLoad(argv[first_file + i], root);
        if(progs[i] == NULL) {
            fprintf(stderr, "ziran-go: failed to parse %s\n", argv[first_file + i]);
            return 1;
        }
    }
    check_ok = CheckCanonicalPrograms(progs, file_count, strict,
                                      (const char *const *)(argv + first_file));
    laws_ok = CheckLaws(progs, file_count);
    if(!check_ok || !laws_ok) {
        for(i = 0; i < file_count; i++) ProgramFree(progs[i]);
        free(progs);
        return 1;
    }
    if(go_lower((const ZirProgram *const *)progs, file_count, root, out_dir,
                 pkg, no_main, runtime_implementation) != 0) {
        for(i = 0; i < file_count; i++)
            ProgramFree(progs[i]);
        free(progs);
        return 1;
    }
    for(i = 0; i < file_count; i++)
        ProgramFree(progs[i]);
    free(progs);
    return 0;
}
