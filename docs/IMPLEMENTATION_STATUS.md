# Implementation status

This page reports the local repository as it exists now. [Architecture](ARCHITECTURE.md),
[`.zir`](ZIR.md), and [`.zib`](ZIB.md) define the target state.

## Working now

- The compiler frontend and C/Go backends have been extracted into a separate
  Ziran repository. `build/bin/ziran` exposes `check`, `ir`, and
  `build --target=c|go` for `.zi` input.
- `make check` passes standalone non-UI programs, a two-module import, and an
  imported typed record block call named `Button` through generated C and
  native Go. The call is resolved from its declaration, not its name.
- `#ui`, `#style`, and old app/route forms are rejected. The embedded Kryon
  runtime declarations and copied `src/kry_std` implementation have been
  removed. JSON diagnostics no longer require Kryon code.
- UI tree and DOM property fields have been removed from the statement IR.
  The extracted implementation still has other UI-era structures and paths.

## Still required

- Remove the remaining UI-specific parser, checker, IR, and backend paths,
  including old style/app/route structures. Finish
  general typed block calls, named blocks, callable child slots, and imports
  for ordinary libraries. Current block-call coverage is a small leaf subset.
- Replace the `.zir` diagnostic dump with a versioned writer, validating
  reader, and saved-IR input for every backend. The current backends still
  parse `.zi` directly.
- Implement the general `.zib` linker, verifier, loader, runtime, and explicit
  host capability contract. No generic portable output is available today.
  Kryon's old `.krb` compiler remains in Kryon.
- Complete native C++, C, and Go backend parity, FFI, capability checks, and
  language law tests independent of UI assumptions. The current `make check`
  only covers the stated C/Go subset.
- Rewrite Kryon's maintained runtime and widgets as importable `.zi` modules
  with platform access through declared interfaces. Kryon is still a C
  library today; no downstream application has been cut over.
- Pass the full supported target and downstream application gates, then make
  the planned single breaking cutover. See [Migration](MIGRATION.md).
