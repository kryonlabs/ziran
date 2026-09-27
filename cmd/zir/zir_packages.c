#include "zir_packages.h"
#include "zir_diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PackageRoot {
    char *id;
    char *path;
} PackageRoot;

typedef struct PackageModule {
    char *id;
    char *name;
    char *path;
    char identity[ZIR_NAME_MAX];
} PackageModule;

typedef struct PackageDependency {
    char *owner;
    char *visible;
    char *target;
    char *exported;
} PackageDependency;

struct ZirPackageMap {
    PackageRoot *roots;
    int root_count;
    PackageModule *modules;
    int module_count;
    PackageDependency *dependencies;
    int dependency_count;
};

static char *field(char **cursor)
{
    char *start = *cursor;
    char *tab = strchr(start, '\t');
    if(tab != NULL) {
        *tab = '\0';
        *cursor = tab + 1;
    } else {
        char *end = strchr(start, '\n');
        if(end != NULL) *end = '\0';
        *cursor = NULL;
    }
    return start;
}

void PackageMapFree(ZirPackageMap *map)
{
    if(map == NULL) return;
    for(int i = 0; i < map->root_count; i++) {
        free(map->roots[i].id);
        free(map->roots[i].path);
    }
    for(int i = 0; i < map->module_count; i++) {
        free(map->modules[i].id);
        free(map->modules[i].name);
        free(map->modules[i].path);
    }
    for(int i = 0; i < map->dependency_count; i++) {
        free(map->dependencies[i].owner);
        free(map->dependencies[i].visible);
        free(map->dependencies[i].target);
        free(map->dependencies[i].exported);
    }
    free(map->roots);
    free(map->modules);
    free(map->dependencies);
    free(map);
}

ZirPackageMap *PackageMapLoad(const char *path)
{
    FILE *file = fopen(path, "r");
    if(file == NULL) {
        Diagnostic(Span(path, 1, 1), "package.map", "cannot open package map");
        return NULL;
    }
    ZirPackageMap *map = calloc(1, sizeof(*map));
    char *line = NULL;
    size_t capacity = 0;
    int okay = map != NULL;
    while(okay && getline(&line, &capacity, file) >= 0) {
        char *cursor = line;
        char *kind = field(&cursor);
        if(cursor == NULL) { okay = 0; break; }
        if(strcmp(kind, "P") == 0) {
            PackageRoot *items = realloc(map->roots,
                (size_t)(map->root_count + 1) * sizeof(*items));
            if(items == NULL) { okay = 0; break; }
            map->roots = items;
            PackageRoot *item = &map->roots[map->root_count++];
            item->id = strdup(field(&cursor));
            item->path = cursor == NULL ? NULL : strdup(field(&cursor));
            okay = item->id != NULL && item->path != NULL;
        } else if(strcmp(kind, "M") == 0) {
            PackageModule *items = realloc(map->modules,
                (size_t)(map->module_count + 1) * sizeof(*items));
            if(items == NULL) { okay = 0; break; }
            map->modules = items;
            PackageModule *item = &map->modules[map->module_count++];
            memset(item, 0, sizeof(*item));
            item->id = strdup(field(&cursor));
            item->name = cursor == NULL ? NULL : strdup(field(&cursor));
            item->path = cursor == NULL ? NULL : strdup(field(&cursor));
            okay = item->id != NULL && item->name != NULL && item->path != NULL;
            if(okay) {
                int n = strcmp(item->id, "root") == 0 ?
                    snprintf(item->identity, sizeof(item->identity), "%s", item->name) :
                    snprintf(item->identity, sizeof(item->identity), "%s_%s", item->id, item->name);
                okay = n > 0 && n < (int)sizeof(item->identity);
            }
        } else if(strcmp(kind, "D") == 0) {
            PackageDependency *items = realloc(map->dependencies,
                (size_t)(map->dependency_count + 1) * sizeof(*items));
            if(items == NULL) { okay = 0; break; }
            map->dependencies = items;
            PackageDependency *item = &map->dependencies[map->dependency_count++];
            memset(item, 0, sizeof(*item));
            item->owner = strdup(field(&cursor));
            item->visible = cursor == NULL ? NULL : strdup(field(&cursor));
            item->target = cursor == NULL ? NULL : strdup(field(&cursor));
            item->exported = cursor == NULL ? NULL : strdup(field(&cursor));
            okay = item->owner && item->visible && item->target && item->exported;
        } else okay = 0;
    }
    free(line);
    fclose(file);
    if(!okay) {
        Diagnostic(Span(path, 1, 1), "package.map", "invalid package map");
        PackageMapFree(map);
        return NULL;
    }
    return map;
}

const char *PackageOwner(const ZirPackageMap *map, const char *path)
{
    const char *owner = NULL;
    size_t best = 0;
    for(int i = 0; i < map->root_count; i++) {
        size_t length = strlen(map->roots[i].path);
        if(length > best && strncmp(path, map->roots[i].path, length) == 0 &&
           (path[length] == '/' || path[length] == '\0')) {
            owner = map->roots[i].id;
            best = length;
        }
    }
    return owner;
}

const char *PackageModuleName(const ZirPackageMap *map, const char *path)
{
    for(int i = 0; i < map->module_count; i++)
        if(strcmp(map->modules[i].path, path) == 0)
            return map->modules[i].identity;
    return NULL;
}

int PackageResolve(const ZirPackageMap *map, const char *owner,
                   const char *visible, char *path, size_t path_size,
                   char *identity, size_t identity_size)
{
    const char *target = owner;
    const char *name = visible;
    int local = 0;
    for(int i = 0; i < map->module_count; i++)
        if(strcmp(map->modules[i].id, owner) == 0 &&
           strcmp(map->modules[i].name, visible) == 0) {
            local = 1;
            break;
        }
    if(!local) {
        target = NULL;
        for(int i = 0; i < map->dependency_count; i++) {
            const PackageDependency *edge = &map->dependencies[i];
            if(strcmp(edge->owner, owner) == 0 &&
               strcmp(edge->visible, visible) == 0) {
                target = edge->target;
                name = edge->exported;
                break;
            }
        }
        if(target == NULL) {
            target = "std";
            name = visible;
        }
    }
    for(int i = 0; i < map->module_count; i++) {
        const PackageModule *module = &map->modules[i];
        if(strcmp(module->id, target) != 0 ||
           strcmp(module->name, name) != 0) continue;
        if(snprintf(path, path_size, "%s", module->path) >= (int)path_size ||
           snprintf(identity, identity_size, "%s", module->identity) >=
           (int)identity_size) return 0;
        return 1;
    }
    return 0;
}
