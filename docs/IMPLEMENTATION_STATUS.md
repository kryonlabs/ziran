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
- `check`, `ir`, `build`, and `bundle` discover extensionless imports
  transitively. Repeated `--module-path DIR` options locate ordinary libraries
  outside an app root; explicitly supplied modules take precedence. A separate
  app root and two-module library are tested from source and saved `.zir`,
  including generated C and `.zib`. Kryon's drag policy test loads its `src/ui/`
  library from an app entry file through the same path, across C, C++, Go,
  and portable bundles.
- Module inputs accept `.zi` source and `.zir` IR only; `.kry`, `.kir`, and
  `.krb` are rejected by the shared loader. Kryon owns the UI test fixtures;
  Ziran keeps a non-UI array and UTF-8 byte law fixture that runs from source
  and saved IR through native C, C++, and Go.
- `ziran ir` writes experimental binary `.zir` version 4 after checking all
  input modules together. C, C++, and Go can read saved modules without reparsing
  `.zi`; their imports are relinked from serialized module identities. The
  reader rejects malformed headers, versions, truncated data, and invalid
  structural references. C, C++, Go, and bundle builds now rerun the language
  checker on saved IR and compare the resulting serialization byte for byte
  with the input. They reject invalid return expressions and inconsistent
  expression nodes. Deterministic output is checked on a sample, and a mixed
  source/IR C build runs.
- `ziran bundle --root DIR --entry module:function -o FILE` builds an
  experimental version 2 `.zib` from source or saved IR. `ziran run FILE`
  loads and executes the validated scalar, plain record, and enum subset without a
  display or Kryon.
  The test runs an imported two-module call and compares bundle bytes from
  source and saved IR. The current runner supports zero-argument entry
  functions with `i32`/`int`/`bool`/`void` results, and helper functions with
  `i64`/`u8`/`u32`/`u64`/`float`/`double`/`string`, enum, or plain record results. It supports scalar, enum,
  string, and record parameters and locals, nested record fields, defaults and record
  literals, value copies, member reads and writes, scalar compound assignments,
  `i64`, `u32`, and `u64` bitwise operations and shifts, calls,
  arithmetic, comparisons, casts, assignments, `if`/`else`, `while`, lexical
  blocks, `break`, `continue`, and returns. It rejects host imports outside the
  scalar/string/void subset and
  unsupported statements before writing a bundle. Loop, branch, and real
  arithmetic programs are compared against generated C, C++, and Go in
  `make check`. The bundle loader also reruns the language checker and laws
  before execution. The linker follows direct calls and drops unreachable
  functions and modules, while retaining reachable record and enum declarations.
  The test runs record and enum functions across imported modules. Kryon's
  complete geometry, layout, and group calculation test also runs in `.zib`
  from source and saved `.zir`. Kryon's accessibility, drag and drop, theme,
  popup, transition fade, numeric input, text row, and drag policies also build
  and run as `.zib` bundles
  from both inputs, independently of the UI runtime. The popup ownership test
  includes frame IDs above `INT64_MAX`. Swipe direction, drag, cancellation,
  and release policy also runs from source and saved `.zir` across native
  targets and `.zib` through ordinary module imports. Image fit, placeholder,
  and clipped-strip geometry have the same source and saved-IR target coverage.
  Kryon's shared style values and fill-state records now support Separator and
  Progress paint decisions as ordinary imported modules in those targets.
  Window placement and drag policy now runs from source and saved IR in native
  targets and `.zib`, with window flags imported from a normal Ziran module.
- Reachable scalar `#extern` host calls now appear as module/function
  capabilities in `.zib`. The loader verifies the capability list against the
  linked IR. `build/libziran.a` and `include/ziran_host.h` expose an opaque
  bundle handle, capability enumeration, and bindings for integer, real, string,
  and void calls. `BundleRun` checks all required bindings before execution.
  The CLI runner also checks required bindings before execution. Source and
  saved-IR bundles, unused extern pruning, list tampering, missing binding
  preflight, and unsupported record signatures are tested. Record and pointer
  host calls remain unsupported.
- Portable strings now carry immutable UTF-8 bytes with exact byte lengths,
  including embedded nulls. The verifier and interpreter cover literals,
  equality, read-only byte indexing, `.length`, parameters, returns, and
  record fields. `make check` compares source and saved-IR bundles with C,
  C++, and Go on a string program and rejects out-of-range indexing.
- The portable verifier still rejects records with raw pointer fields. Kryon's
  `CardButtonProps` builds for native C, C++, and Go, but a `.zib` entry that
  carries its pointer-bearing `ButtonProps` result does not yet verify. A
  portable host handle or capability contract is required for that case.
- Compiler functions use short names without `Zir` or `zir_` prefixes,
  including the IR serializer's `ProgramWrite`, `ProgramRead`, and `PathIsIR`.
  IR data types retain `Zir` names for now.
- The inherited runtime type and enum-member lookup stubs are gone. Local and
  imported declarations now supply those names without a compiler fallback.
- Native Go no longer treats ordinary imported record types as types from the
  legacy Kryon Go runtime. The standalone record-call test asserts that no
  Kryon package import is generated.
- Native Go now uses declared record fields and ordinary call resolution even
  when a record or function has a name previously reserved for Kryon UI. A
  standalone source and saved-IR test covers these name collisions. Generated
  Go defaults to package `ziran` unless `--pkg` overrides it.
- Native Go no longer has an implicit Kryon package import or a separate
  `--runtime-implementation` mode. Explicit Go extern targets generate package
  imports; host externs use a declared embedding interface. Source and saved
  `.zir` tests run both forms without Kryon.
- The shared emitter now has only C, C++, and Go targets. Its unused JavaScript
  path and hardcoded Kryon calls have been removed.
- Native C++ now lowers pointer and `const` record field types through the
  same scalar type mapping as C. A non-UI source and saved-IR record with an
  `i32*` field compiles and runs on C++.
- The inherited `#intrinsic "web"` modifier and its two name-based browser
  shims are gone. Browser behavior now requires an explicit host or target
  extern. C/C++ output no longer injects Kryon's inspection header for
  ordinary functions, and generated temporaries and header guards use Ziran
  names. Saved IR rejects the retired intrinsic import tag.
- `#ui`, `#style`, and old app/route forms are rejected. The embedded Kryon
  runtime declarations and copied `src/kry_std` implementation have been
  removed. JSON diagnostics no longer require Kryon code.
- `#instance` is rejected as a language modifier. Its special statement and
  capture fields, retained-state emission, and IR serialization have been
  removed; a state library can supply that behavior through ordinary calls.
- UI tree and DOM property fields have been removed from the statement IR.
  App, route, style, and inspector metadata have been removed from the IR and
  backend entry paths. Generic calls named `Image` and record block calls
  named `Button` are covered by standalone tests. The extracted implementation
  still has other UI-era paths.

## Still required

- Remove the remaining UI-specific parser, checker, IR, and backend paths.
  Finish general typed block calls, named blocks, callable child slots, and imports
  for ordinary libraries. Current block-call coverage is a small leaf subset.
- Make structured `.zir` authoritative: the current checker reconstructs
  expressions from serialized statement text, then compares the full checked
  serialization with the original. This rejects divergent stored fields but
  backends still reparse text rather than consume the structured expressions
  directly. Remove UI-era fields from the IR model and make every other
  supported backend consume the same saved IR. Source, saved-IR, and mixed
  builds currently rerun the source checker across all modules.
- Extend the `.zib` linker and verifier beyond the current subset: remaining
  control flow, enum initializer expressions, arrays, slots, state, and all checked
  expressions. Extend host capabilities to portable records, handles, and
  equivalent behavior
  for all supported language features. The current bundle embeds checked
  `.zir`; the linker follows direct function calls and retains the types
  those functions use. The portable interpreter executes plain records and
  enums but does not yet cover every checked expression or native feature.
  Kryon's old `.krb` compiler has been removed. Existing `.krb` files cannot
  be loaded as `.zib`.
- Complete native C++, C, and Go backend parity, FFI, capability checks, and
  language law tests independent of UI assumptions. The current `make check`
  only covers the stated C/C++/Go subset.
- Define and implement the law checking and parallel execution contracts in
  [Language direction](LANGUAGE_DIRECTION.md). The current law pass checks a
  generic post-check IR invariant; there is no general proof system, automatic
  parallel execution, or GPU backend in this repository.
- Complete Kryon's moved `.zi` widget modules with platform access through
  declared interfaces. The checked Kryon archive currently contains a policy
  subset; native host integration and downstream cutover remain incomplete.
- Pass the full supported target and downstream application gates, then make
  the planned single breaking cutover. See [Migration](MIGRATION.md).
