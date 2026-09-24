#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_serial.h"
#include "zir_diagnostic.h"
#include "zir_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void
usage(void)
{
    fprintf(stderr, "usage: zi2zir [--diagnostics=text|json] [--module-path DIR] --root DIR -o DIR file.zi|file.zir ...\n");
}

static int
write_program(const ZirProgram *program, const char *out_dir)
{
    const char *source = program->modules[0].source_path;
    size_t length = strlen(source);
    char output[ZIR_PATH_MAX * 2];
    FILE *file;

    if(length > 3 && strcmp(source + length - 3, ".zi") == 0)
        length -= 3;
    if(snprintf(output, sizeof(output), "%s/%.*s.zir", out_dir,
                (int)length, source) >= (int)sizeof(output)) {
        Diagnostic(program->modules[0].span, "zir.output",
                      "IR output path is too long");
        return 0;
    }
    for(char *cursor = output + 1; *cursor; cursor++) {
        if(*cursor != '/')
            continue;
        *cursor = '\0';
        mkdir(output, 0755);
        *cursor = '/';
    }
    file = fopen(output, "wb");
    if(file == NULL) {
        Diagnostic(program->modules[0].span, "zir.output",
                      "cannot open IR output: %s", output);
        return 0;
    }
    if(!ProgramWrite(program, file)) {
        Diagnostic(program->modules[0].span, "zir.output",
                      "cannot serialize checked IR: %s", output);
        fclose(file);
        remove(output);
        return 0;
    }
    if(fclose(file) != 0) {
        Diagnostic(program->modules[0].span, "zir.output",
                      "cannot finish IR output: %s", output);
        remove(output);
        return 0;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    const char *root = NULL;
    const char *out_dir = NULL;
    int check_only = 0;
    int first_file = 0;
    int result = 1;
    ProgramSet set = {0};
    const char *module_paths[64];
    int module_path_count = 0;
    int count;

    for(int i = 1; i < argc; i++) {
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
        } else if(strcmp(argv[i], "--check-only") == 0) {
            check_only = 1;
        } else if(argv[i][0] == '-') {
            usage();
            return 1;
        } else {
            first_file = i;
            break;
        }
    }
    if(root == NULL || (!check_only && out_dir == NULL) || first_file == 0) {
        usage();
        return 1;
    }
    if(!ProgramsLoad(&set, root, module_paths, module_path_count,
                     (const char *const *)(argv + first_file), argc - first_file))
        goto done;
    count = set.count;
    if(!CheckCanonicalPrograms(set.programs, count, 1,
                               (const char *const *)set.paths) ||
       !CheckLaws(set.programs, count))
        goto done;
    if(!check_only) {
        for(int i = 0; i < count; i++) {
            if(set.programs[i]->module_count < 1 ||
               !write_program(set.programs[i], out_dir))
                goto done;
        }
    }
    result = 0;
done:
    ProgramsFree(&set);
    return result;
}
