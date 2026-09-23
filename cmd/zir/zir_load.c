#include "zir_load.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { MAX_MODULES = 1024 };

void
ProgramsFree(ProgramSet *set)
{
    for(int i = 0; i < set->count; i++) {
        ProgramFree(set->programs[i]);
        free(set->paths[i]);
        free(set->roots[i]);
    }
    free(set->programs);
    free(set->paths);
    free(set->roots);
    *set = (ProgramSet){0};
}

static int
module_named(const ProgramSet *set, const char *target)
{
    for(int i = 0; i < set->count; i++)
        for(int m = 0; m < set->programs[i]->module_count; m++)
            if(strcmp(set->programs[i]->modules[m].name, target) == 0)
                return 1;
    return 0;
}

static int
module_loaded(const ProgramSet *set, const char *target)
{
    for(int i = 0; i < set->count; i++)
        for(int m = 0; m < set->programs[i]->module_count; m++) {
            const ZirModule *module = &set->programs[i]->modules[m];
            char stem[ZIR_PATH_MAX];
            size_t length;
            if(strcmp(module->name, target) == 0)
                return 1;
            if(snprintf(stem, sizeof(stem), "%s", module->source_path) >=
               (int)sizeof(stem))
                continue;
            length = strlen(stem);
            if(length > 3 && strcmp(stem + length - 3, ".zi") == 0)
                stem[length - 3] = '\0';
            if(strcmp(stem, target) == 0)
                return 1;
        }
    return 0;
}

static int
add_program(ProgramSet *set, const char *path, const char *root)
{
    char *canonical = realpath(path, NULL);
    ZirProgram *program;
    if(canonical == NULL) {
        Diagnostic(Span(path, 1, 1), "module.input",
                   "cannot open module input: %s", path);
        return 0;
    }
    for(int i = 0; i < set->count; i++)
        if(strcmp(set->paths[i], canonical) == 0) {
            free(canonical);
            return 1;
        }
    if(set->count >= MAX_MODULES) {
        Diagnostic(Span(path, 1, 1), "module.limit",
                   "module graph exceeds %d files", MAX_MODULES);
        free(canonical);
        return 0;
    }
    program = ProgramLoad(canonical, root);
    if(program == NULL) {
        free(canonical);
        return 0;
    }
    for(int m = 0; m < program->module_count; m++)
        if(module_named(set, program->modules[m].name)) {
            Diagnostic(program->modules[m].span, "module.duplicate",
                       "duplicate module identity: %s",
                       program->modules[m].name);
            ProgramFree(program);
            free(canonical);
            return 0;
        }
    ZirProgram **programs = realloc(set->programs,
                                   (size_t)(set->count + 1) * sizeof(*programs));
    if(programs == NULL) {
        ProgramFree(program);
        free(canonical);
        return 0;
    }
    set->programs = programs;
    char **paths = realloc(set->paths,
                           (size_t)(set->count + 1) * sizeof(*paths));
    if(paths == NULL) {
        ProgramFree(program);
        free(canonical);
        return 0;
    }
    set->paths = paths;
    char **roots = realloc(set->roots,
                           (size_t)(set->count + 1) * sizeof(*roots));
    if(roots == NULL) {
        ProgramFree(program);
        free(canonical);
        return 0;
    }
    set->roots = roots;
    char *saved_root = strdup(root);
    if(saved_root == NULL) {
        ProgramFree(program);
        free(canonical);
        return 0;
    }
    set->programs[set->count] = program;
    set->paths[set->count] = canonical;
    set->roots[set->count] = saved_root;
    set->count++;
    return 1;
}

static int
module_target(const char *target)
{
    if(target[0] == '\0' || strchr(target, '.') != NULL ||
       target[0] == '/' || strchr(target, '\\') != NULL)
        return 0;
    for(const unsigned char *p = (const unsigned char *)target; *p; p++)
        if(!isalnum(*p) && *p != '_' && *p != '/' && *p != '-')
            return 0;
    return 1;
}

static int
try_module(ProgramSet *set, const char *directory, const char *load_root,
           const char *target, int prefer_ir)
{
    char candidate[ZIR_PATH_MAX * 2];
    const char *extensions[2] = {prefer_ir ? ".zir" : ".zi",
                                  prefer_ir ? ".zi" : ".zir"};
    for(int i = 0; i < 2; i++) {
        if(snprintf(candidate, sizeof(candidate), "%s/%s%s", directory,
                    target, extensions[i]) >= (int)sizeof(candidate))
            continue;
        struct stat info;
        if(stat(candidate, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        return add_program(set, candidate, load_root) ? 1 : -1;
    }
    return 0;
}

static int
load_import(ProgramSet *set, int owner, const ZirImport *import,
            const char *root, const char *const *module_paths,
            int module_path_count)
{
    const char *target = import->target;
    char directory[ZIR_PATH_MAX * 2];
    const char *slash = strrchr(set->paths[owner], '/');
    int prefer_ir = PathIsIR(set->paths[owner]);
    if(module_loaded(set, target))
        return 1;
    if(slash != NULL) {
        size_t length = (size_t)(slash - set->paths[owner]);
        if(length < sizeof(directory)) {
            memcpy(directory, set->paths[owner], length);
            directory[length] = '\0';
            int result = try_module(set, directory, set->roots[owner],
                                    target, prefer_ir);
            if(result != 0)
                return result > 0;
        }
    }
    int result = try_module(set, set->roots[owner], set->roots[owner],
                            target, prefer_ir);
    if(result != 0)
        return result > 0;
    if(strcmp(root, set->roots[owner]) != 0) {
        result = try_module(set, root, root, target, prefer_ir);
        if(result != 0)
            return result > 0;
    }
    for(int i = 0; i < module_path_count; i++) {
        result = try_module(set, module_paths[i], module_paths[i],
                            target, prefer_ir);
        if(result != 0)
            return result > 0;
    }
    Diagnostic(import->span, "module.not_found",
               "cannot find imported module: %s", target);
    return 0;
}

int
ProgramsLoad(ProgramSet *set, const char *root,
             const char *const *module_paths, int module_path_count,
             const char *const *inputs, int input_count)
{
    char *canonical_root = realpath(root, NULL);
    char **canonical_paths = NULL;
    int ok = 0;
    *set = (ProgramSet){0};
    if(canonical_root == NULL || input_count <= 0 || module_path_count < 0) {
        Diagnostic(Span(root, 1, 1), "module.root",
                   "module root is unavailable");
        goto done;
    }
    canonical_paths = calloc((size_t)module_path_count, sizeof(*canonical_paths));
    if(module_path_count > 0 && canonical_paths == NULL)
        goto done;
    for(int i = 0; i < module_path_count; i++) {
        canonical_paths[i] = realpath(module_paths[i], NULL);
        if(canonical_paths[i] == NULL) {
            Diagnostic(Span(module_paths[i], 1, 1), "module.path",
                       "module search path is unavailable");
            goto done;
        }
    }
    for(int i = 0; i < input_count; i++)
        if(!add_program(set, inputs[i], canonical_root))
            goto done;
    for(int p = 0; p < set->count; p++)
        for(int m = 0; m < set->programs[p]->module_count; m++) {
            const ZirModule *module = &set->programs[p]->modules[m];
            for(int i = 0; i < module->import_count; i++) {
                const ZirImport *import = &module->imports[i];
                if(import->kind != ZIR_IMPORT_HEADER ||
                   !module_target(import->target))
                    continue;
                if(!load_import(set, p, import, canonical_root,
                                (const char *const *)canonical_paths,
                                module_path_count))
                    goto done;
            }
        }
    ok = 1;
done:
    for(int i = 0; i < module_path_count; i++)
        if(canonical_paths != NULL)
            free(canonical_paths[i]);
    free(canonical_paths);
    free(canonical_root);
    if(!ok)
        ProgramsFree(set);
    return ok;
}
