#include "zir.h"
#include "zir_emit.h"

#include <stdio.h>
#include <stdlib.h>

enum { LEVELS = 24, LANES = 2 };

int
main(void)
{
    ZirModule *modules = calloc(LEVELS * LANES, sizeof(*modules));
    ZirFunction *initializer = calloc(1, sizeof(*initializer));
    if(modules == NULL || initializer == NULL)
        return 2;

    for(int level = 0; level < LEVELS - 1; level++) {
        for(int lane = 0; lane < LANES; lane++) {
            ZirModule *module = &modules[level * LANES + lane];
            module->imports = calloc(LANES, sizeof(*module->imports));
            if(module->imports == NULL)
                return 2;
            module->import_count = LANES;
            for(int next = 0; next < LANES; next++)
                module->imports[next].resolved_module =
                    &modules[(level + 1) * LANES + next];
        }
    }

    ZirModule *leaf = &modules[(LEVELS - 1) * LANES];
    leaf->imports = calloc(1, sizeof(*leaf->imports));
    if(leaf->imports == NULL)
        return 2;
    leaf->import_count = 1;
    leaf->imports[0].resolved_module = &modules[0];

    if(ModuleNeedsStartup(&modules[0])) {
        fputs("startup reported for a graph without an initializer\n", stderr);
        return 1;
    }

    initializer->is_global_initializer = 1;
    leaf = &modules[LEVELS * LANES - 1];
    leaf->functions = initializer;
    leaf->function_count = 1;
    if(!ModuleNeedsStartup(&modules[0])) {
        fputs("reachable startup initializer was missed\n", stderr);
        return 1;
    }

    for(int level = 0; level < LEVELS - 1; level++)
        for(int lane = 0; lane < LANES; lane++)
            free(modules[level * LANES + lane].imports);
    free(modules[(LEVELS - 1) * LANES].imports);
    free(initializer);
    free(modules);
    return 0;
}
