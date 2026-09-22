# Two-repository migration

This is the agreed end state and cutover plan. The current state is recorded
in [Implementation status](IMPLEMENTATION_STATUS.md).

## End state

| Concern | Repository after cutover |
| --- | --- |
| `.zi` language, checker, laws, standard library, `.zir`, native backends, `.zib`, portable runtime | Ziran |
| Reusable widgets, layout, styling, accessibility, interaction, renderers, host adapters implemented in `.zi` | Kryon |
| Product behavior and application UI | Each application repository |

Ziran must build, check, and run non-graphical programs independently. Kryon
is a normal importable Ziran library. No language file, IR node, compiler
branch, linker rule, or loader rule recognizes Kryon widgets by name. UI
behavior is expressed by Kryon's `.zi` modules. External C libraries and OS
services enter through declared FFI or host capabilities; they do not hold
Kryon's widget or runtime policy.

## Cutover sequence

1. Finish the generic Ziran language surface and remove inherited UI paths.
   Validate ordinary library imports, typed blocks and slots, laws, and
   diagnostics without importing Kryon.
2. Ship a readable/writable `.zir` contract used by every backend, then a
   generic `.zib` linker and runtime. Test non-graphical modules through both
   native and portable paths first.
3. Implement Kryon in `.zi`, with explicit host interfaces and equivalent
   behavior on each supported renderer and platform. Port representative
   applications against the ordinary import surface.
4. Run language conformance, Kryon behavior, all supported target builds,
   and downstream application gates. Fix parity failures in their owning
   repositories.
5. Perform one breaking public cutover to `.zi`, `.zir`, and `.zib`; migrate
   downstream source and dependency pointers. Do not pretend old `.krb` bytes
   are `.zib` or keep a hidden UI compiler path for compatibility.

All Kryon changes are made in the upstream Kryon repository, then consumed by
applications through clean dependency updates. The Ziran repository owns only
language and portable execution work. The two repositories may progress
independently before the public cutover, but the cutover is gated as one
supported change.

## Completion gates

- Non-UI `.zi` and `.zir` builds work with no Kryon checkout or graphics
  dependency; equivalent `.zib` programs run without a display.
- A saved `.zir` can be read by every supported backend; equivalent source and
  saved-IR builds behave the same. The `.zib` loader rejects malformed bundles
  and missing capabilities before execution.
- Kryon builds from maintained `.zi` source and is linked only when imported.
  Widget calls are ordinary typed calls, and supported UI examples have no
  compiler or IR widget exceptions.
- Supported C, C++, Go, and portable targets, renderer paths, and downstream
  applications pass their relevant behavior and build gates before changing
  their public input and output extensions.
