#ifndef ZIR_GO_LOWER_H
#define ZIR_GO_LOWER_H

#include "zir.h"

/*
 * go_lower - Zir -> Go backend.
 *
 * Emits one .go file per module into out_dir, all in one Go package.
 * Package imports come from declared Go extern targets. Other externs use a
 * host interface supplied by the embedding program.
 * Unsupported types and statements fail with a source diagnostic.
 */
int go_lower(const ZirProgram *const *progs, int prog_count,
              const char *root, const char *out_dir, const char *pkg,
              int no_main);

#endif
