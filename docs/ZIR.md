# Ziran intermediate representation (`.zir`)

This is the intended `.zir` contract; the current writer is still a diagnostic
dump, as recorded in [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

`.zir` is a versioned, checked representation of modules. It preserves source
locations, symbol identities, types, constants, imports, function bodies,
control flow, and capability requirements. It contains no source-language
reparse dependency and no Kryon-specific node or metadata field. The reader
validates the version and structure before a backend or linker uses it.

The canonical writer emits the same bytes for equivalent checked input. C,
C++, Go, and `.zib` compilation accept saved `.zir` modules. A build from
`.zi` must produce the same observable program and diagnostics as a build
from its saved `.zir`. Incompatible versions and malformed files fail with
explicit diagnostics. `.zir` is a compiler format; portable distribution uses
`.zib`.
