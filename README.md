# Ziran

Ziran is an independent language toolchain. Its source extension is `.zi`.
The compiler's intermediate representation uses `.zir`. Kryon will be an
optional UI library. This extraction no longer embeds Kryon's runtime type
declarations, but widget-specific parser and backend code still needs to move
out of Ziran.

Build the current compiler tools with `make`, then run `make check` for
standalone programs that do not import Kryon, including a two-module program.

The toolchain split is in progress. The current `.zir` writer is a diagnostic
dump; the reader, linker, portable `.zib` format, and Kryon library migration
are still required before the replacement is complete.
