# Implementation status

This page describes the repository as it exists today. The other documents
describe the intended Ziran contract.

## Working now

- The compiler history has been extracted from Kryon into this repository.
- `make check` verifies non-UI `.zi` programs, ordinary named functions, and
  a two-module import; generated C and native Go execute without Kryon.
- `ziran check` checks source. `ziran ir` writes a `.zir` diagnostic dump.
  `ziran build --target=c|go` generates native source for the tested subset.
- The compiler no longer embeds Kryon's runtime type declarations. Its JSON
  diagnostics no longer depend on the copied `src/kry_std` implementation.

## Required before the split is complete

- Remove remaining `#ui`, widget block, DOM, KSS, app, and route logic from
  the extracted parser, IR, checker, and native backends. Replace UI block
  handling with ordinary imported function signatures, records, and slots.
- Make `.zir` a versioned readable and writable checked IR. Today it is a
  text dump, and backends still parse `.zi` directly.
- Build the general `.zib` linker, loader, verifier, and runtime. The old
  Kryon-specific `.krb` compiler remains in Kryon; it is not a `.zib` backend.
- Complete native C++ and portable target parity, FFI and capability checks,
  and language law tests independently of Kryon.
- Move Kryon runtime source to `.zi`, make it an ordinary importable library,
  and pass every supported target and downstream application gate before the
  breaking cutover.
