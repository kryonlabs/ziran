# Implementation status

This page reports the local repository as it exists now. [Architecture](ARCHITECTURE.md),
[`.zir`](ZIR.md), and [`.zib`](ZIB.md) define the target state.

## Working now

- The compiler frontend and C/C++/Go backends have been extracted into a separate
  Ziran repository. `build/bin/ziran` exposes `check`, `ir`, and
  `build --target=c|cpp|go` for `.zi` or saved `.zir` input.
- `make check` passes standalone non-UI programs, a two-module import, and an
  imported typed record block call named `Button` through generated C, C++,
  and native Go. Source and saved `.zir` builds execute for the tested subset.
  The call is resolved from its declaration, not its name.
- `ziran ir` writes experimental binary `.zir` version 3 after checking all
  input modules together. C, C++, and Go can read saved modules without reparsing
  `.zi`; their imports are relinked from serialized module identities. The
  reader rejects malformed headers, versions, truncated data, and invalid
  structural references. C, C++, Go, and bundle builds now rerun the language
  checker on saved IR and compare the resulting serialization byte for byte
  with the input. They reject invalid return expressions and inconsistent
  expression nodes. Deterministic output is checked on a sample, and a mixed
  source/IR C build runs.
- `ziran bundle --root DIR --entry module:function -o FILE` builds an
  experimental version 1 `.zib` from source or saved IR. `ziran run FILE`
  loads and executes the validated scalar subset without a display or Kryon.
  The test runs an imported two-module call and compares bundle bytes from
  source and saved IR. The current runner supports zero-argument entry
  functions, `i32`/`int`/`bool`/`void` functions, scalar parameters and locals,
  calls, arithmetic, comparisons, assignments, `if`/`else`, `while`, lexical
  blocks, `break`, `continue`, and returns. It rejects host imports and
  unsupported statements before writing a bundle. A loop and branch program
  is compared against generated C, C++, and Go in `make check`. The bundle
  loader also reruns the language checker and laws before execution.
- Extracted compiler function names no longer carry `Zir` or `zir_` prefixes;
  IR types retain `Zir` names for now.
- `#ui`, `#style`, and old app/route forms are rejected. The embedded Kryon
  runtime declarations and copied `src/kry_std` implementation have been
  removed. JSON diagnostics no longer require Kryon code.
- UI tree and DOM property fields have been removed from the statement IR.
  App, route, style, and inspector metadata have been removed from the IR and
  backend entry paths. Generic calls named `Image` and record block calls
  named `Button` are covered by standalone tests. The extracted implementation
  still has other UI-era paths.

## Still required

- Remove the remaining UI-specific parser, checker, IR, and backend paths,
  including Go's legacy Kryon runtime and record conversion branches. Finish
  general typed block calls, named blocks, callable child slots, and imports
  for ordinary libraries. Current block-call coverage is a small leaf subset.
- Make structured `.zir` authoritative: the current checker reconstructs
  expressions from serialized statement text, then compares the full checked
  serialization with the original. This rejects divergent stored fields but
  backends still reparse text rather than consume the structured expressions
  directly. Remove UI-era fields from the IR model and make every other
  supported backend consume the same saved IR. Source, saved-IR, and mixed
  builds currently rerun the source checker across all modules.
- Extend the `.zib` linker and verifier beyond the scalar subset: remaining
  control flow, records, strings, arrays, slots, state, and all checked
  expressions. Add a real host capability contract and equivalent behavior
  for all supported language features. The current bundle embeds checked
  `.zir` and contains every supplied module; it does not prune unreachable
  code. Kryon's old `.krb` compiler remains in Kryon.
- Complete native C++, C, and Go backend parity, FFI, capability checks, and
  language law tests independent of UI assumptions. The current `make check`
  only covers the stated C/C++/Go subset.
- Define and implement the law checking and parallel execution contracts in
  [Language direction](LANGUAGE_DIRECTION.md). The current law pass checks a
  generic post-check IR invariant; there is no general proof system, automatic
  parallel execution, or GPU backend in this repository.
- Rewrite Kryon's maintained runtime and widgets as importable `.zi` modules
  with platform access through declared interfaces. Kryon is still a C
  library today; no downstream application has been cut over.
- Pass the full supported target and downstream application gates, then make
  the planned single breaking cutover. See [Migration](MIGRATION.md).
