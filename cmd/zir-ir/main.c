#include "zir.h"
#include "zir_parse.h"
#include "zir_check.h"
#include "zir_laws.h"
#include "zir_serial.h"
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
    ZirProgram **programs = NULL;
    int count;

    for(int i = 1; i < argc; i++) {
        if(strncmp(argv[i], "--diagnostics=", 14) == 0) {
            if(!SetDiagnosticFormat(argv[i] + 14)) {
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
            first_file = i;
            break;
        }
    }
    if(root == NULL || (!check_only && out_dir == NULL) || first_file == 0) {
        usage();
        return 1;
    }
    count = argc - first_file;
    programs = calloc((size_t)count, sizeof(*programs));
    if(programs == NULL)
        return 1;
    for(int i = 0; i < count; i++) {
        programs[i] = parse_file(argv[first_file + i], root);
        if(programs[i] == NULL)
            goto done;
    }
    if(!CheckPrograms(programs, count, 1) || !CheckLaws(programs, count))
        goto done;
    if(!check_only) {
        for(int i = 0; i < count; i++) {
            if(programs[i]->module_count < 1 ||
               !write_program(programs[i], out_dir))
                goto done;
        }
    }
    result = 0;
done:
    for(int i = 0; i < count; i++)
        ProgramFree(programs[i]);
    free(programs);
    return result;
}
