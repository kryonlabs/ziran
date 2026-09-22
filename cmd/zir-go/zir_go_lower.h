#ifndef ZIR_GO_LOWER_H
#define ZIR_GO_LOWER_H

#include "zir.h"

/*
 * zir_go_lower - Zir -> Go backend.
 *
 * Emits one .go file per module into out_dir, all in one Go package.
 * Standalone language modules need no Kryon runtime. The backend still has
 * inherited Kryon-specific lowering paths; see IMPLEMENTATION_STATUS.md.
 * Unsupported types and statements fail with a source diagnostic.
 */
int zir_go_lower(const ZirProgram *const *progs, int prog_count,
              const char *root, const char *out_dir, const char *pkg,
              int no_main, int runtime_implementation);

#endif
