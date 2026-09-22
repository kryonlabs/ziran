/*
 * Emit a debuggable ZIR text artifact from .zi source.
 * Thin wrapper around the shared zir_parse_file frontend.
 */
#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void
usage(void)
{
    fprintf(stderr, "usage: ziran ir [--diagnostics=text|json] --root DIR -o DIR file.zi ...\n");
}

int
main(int argc, char **argv)
{
    const char *root = NULL;
    const char *out_dir = NULL;
    int check_only = 0;
    int check_ok;
    int laws_ok;
    int i;

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
        } else if(strcmp(argv[i], "--check-only") == 0) {
            check_only = 1;
        } else if(argv[i][0] == '-') {
            usage();
            return 1;
        } else {
            break;
        }
    }
    if(root == NULL || (!check_only && out_dir == NULL) || i >= argc) {
        usage();
        return 1;
    }
    for(; i < argc; i++) {
        ZirProgram *program = zir_parse_file(argv[i], root);
        if(program == NULL)
            return 1;
        check_ok = ZirCheckPrograms(&program, 1, 0);
        laws_ok = ZirCheckLaws(&program, 1);
        if(!check_ok || !laws_ok) {
            ZirProgramFree(program);
            return 1;
        }
        if(check_only) {
            ZirProgramFree(program);
            continue;
        }
        /* Write the .zir dump using the module's source_path (relative to root)
         * so directory structure is preserved. */
        if(program->module_count > 0) {
            const char *src = program->modules[0].source_path;
            size_t slen = strlen(src);
            char stem[512];
            char out_path[1024];
            FILE *out;

            if(slen > 3 && strcmp(src + slen - 3, ".zi") == 0)
                slen -= 3;
            if(slen >= sizeof(stem))
                slen = sizeof(stem) - 1;
            memcpy(stem, src, slen);
            stem[slen] = '\0';
            snprintf(out_path, sizeof(out_path), "%s/%s.zir", out_dir, stem);
            /* mkdir -p */
            {
                char tmp[1024];
                size_t j;

                snprintf(tmp, sizeof(tmp), "%s", out_path);
                for(j = 1; j < strlen(tmp); j++) {
                    if(tmp[j] == '/') {
                        tmp[j] = '\0';
                        mkdir(tmp, 0755);
                        tmp[j] = '/';
                    }
                }
            }
            out = fopen(out_path, "wb");
            if(out == NULL) {
                fprintf(stderr, "ziran-ir: %s: open failed\n", out_path);
                ZirProgramFree(program);
                return 1;
            }
            ZirProgramDump(program, out);
            fclose(out);
        }
        ZirProgramFree(program);
    }
    return 0;
}
