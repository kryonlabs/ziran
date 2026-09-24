# Implementation status

This page reports the local repository as it exists now. [Architecture](ARCHITECTURE.md),
[`.zir`](ZIR.md), and [`.zib`](ZIB.md) define the target state.

## Working now

- Jai-style `#program_export` on its own line exports the next procedure's
  native symbol, and file-scope variables use `name: Type` declarations.
  `#export`, `#global`, and C-style `static` source forms are rejected. Source
  and saved IR retain these exports and globals across native and portable
  builds.
- Source foreign declarations use Jai's `#system_library` and `#foreign`
  forms. The compiler resolves those library declarations to its existing
  host, C, or Go import model before saving IR. The old `#extern` source
  modifier is rejected.
- Source accepts Jai `#scope_file`, `#scope_module`, and `#scope_export` for
  following procedures, globals, types, and constants. Private declarations
  are excluded from imported lookup, native headers hide private types and
  constants, and private globals use internal linkage. Scope visibility
  survives saved IR. `#private` is rejected. Since one source file is one
  Ziran module today, file and module scope currently have the same reach.

- The compiler frontend and C/C++/Go backends have been extracted into a separate
  Ziran repository. `zi2zir` checks and saves `.zir`; `zi2c`, `zi2cpp`, and
  `zi2go` compile `.zi` or saved `.zir` input.
- `make check` passes standalone non-UI programs, a two-module import, and an
  imported typed record block call named `Button` through generated C, C++,
  and native Go. A named block binds its non-void result, with source and saved
  `.zir` builds across those targets and matching source/saved `.zib` execution.
  The call is resolved from its declaration, not its name.
- A two-module named block can pass a synchronous `#slot` child that captures
  a caller local. Source and saved `.zir` builds execute the same result in C,
  C++, Go, and `.zib`. The portable linker retains both captured inline bodies
  and imported named functions passed as slot values.
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
- `zi2zir` writes experimental binary `.zir` version 10 after checking all
  input modules together. C, C++, and Go can read saved modules without reparsing
  `.zi`; their imports are relinked from serialized module identities. The
  reader rejects malformed headers, versions, truncated data, and invalid
  structural references and expression cycles. C, C++, Go, and bundle builds
  rerun strict language checking on saved expression graphs and compare the
  resulting serialization byte for byte with the input. Generated variant
  operations are rebuilt from declared cases when saved IR is loaded; their
  executable bodies are never trusted from a `.zir` file. The tested typed
  subset takes behavior from those graphs and serialized branch flags even
  when stored statement text differs. Deterministic output is checked on a
  sample, and a mixed source/IR
  C build runs.
- `zi2zib bundle --root DIR --entry module:function -o FILE` builds an
  experimental version 7 `.zib` from source or saved IR. `zi2zib run FILE`
  loads and executes the validated scalar, plain record, enum, and fixed-array
  subset without a
  display or Kryon.
  The test runs an imported two-module call and compares bundle bytes from
  source and saved IR. The current runner supports zero-argument entry
  functions with `s32`/`int`/`bool`/`void` results, and helper functions with
  `s64`/`u8`/`u32`/`u64`/`char`/`float`/`float64`/`string`, enum, or plain record results. It supports scalar, enum,
  string, record, and fixed-array parameters and locals, nested record fields,
  defaults and record and array literals, value copies, member and array reads
  and writes, scalar compound assignments,
  `s64`, `u32`, and `u64` bitwise operations and shifts, calls,
  arithmetic, comparisons, casts, assignments, `if`/`else`, `while`, lexical
  blocks, `break`, `continue`, `unreachable`, Jai-style enum `if #complete`
  cases, and returns. Complete enum cases lower to checked branches with a trap for values
  created by an out-of-range integer cast. It rejects host imports outside the
  scalar/string/plain-record subset and
  unsupported statements before writing a bundle. Loop, branch, and real
  arithmetic programs are compared against generated C, C++, and Go in
  `make check`. The bundle loader also reruns the language checker and laws
  before execution. The linker follows direct calls and drops unreachable
  functions and modules, while retaining reachable record and enum declarations.
  Fixed arrays with numeric or resolved constant-expression bounds, including
  imported constants and nested arrays in records, are tested through source
  and saved IR in C, C++, Go, and `.zib`.
  Tests cover ASCII values in nested `char` arrays, value-copy isolation, and
  portable bounds failures. Linked bundles retain needed
  compile-time definitions for bound expressions, including constants-only
  imports.
  Integer `NAME :: value` definitions in the checked arithmetic subset can be
  used in function expressions. Imported and negative values fold to checked
  IR literals. String literal definitions and aliases also fold into checked
  expressions, including in saved IR and portable bundles. The checker rejects
  local bindings that would collide with
  generated C/C++ definitions.
  Portable slices borrow fixed-array storage. Source and saved IR bundles and
  generated C, C++, and Go agree on range creation, nested views, function
  parameters and returns, `.length`, indexed reads and writes, and array
  replacement while a view is live. Bounds failures reject execution. The
  checker rejects slices in records and globals, host slice returns, and
  returns that borrow a local array. Unresolved fixed-array bounds remain
  outside the portable subset.
  Borrowed arrays retain record elements written by called functions after
  those functions return; the portable slice test covers replacing a nested
  record in a live view across calls.
  The test runs record and enum functions across imported modules.
  Monomorphic `variant` declarations now generate sealed tagged storage,
  case constructors, checked payload accessors, and exhaustive `match` with
  optional payload binding. The checker rejects direct access to variant
  storage from source. Source and saved IR, C, C++, Go, and `.zib` agree on
  an imported variant with integer and string cases. Host calls cannot carry
  variant storage until a safe boundary contract is defined. Generic variant
  templates can be applied with named `Name :: Generic(Type)` declarations
  or direct `Generic(Type)` annotations,
  including the standard `Option` and `Result` templates. Generic
  record templates use the same type application syntax; `std/pair.zi` provides
  `Pair`. Their concrete record fields work in source, saved IR, C, C++,
  Go, and `.zib`. A terminal
  `?` on a declaration, assignment, or standalone call propagates a matching
  `Err` payload. Adjacent postfix `?` also lowers in eager return and other
  value expressions and plain `if` and `while` conditions. Top-level boolean
  `&&` and `||` expressions in simple statements and loop conditions retain
  short-circuit evaluation, including nested forms. Top-level conditional expressions with
  typed destinations or loop conditions preserve selected-arm evaluation. Lazy expressions
  embedded in outer calls remain unsupported.
  Source, saved IR, native targets, and `.zib` agree on both success and error
  paths. Kryon's complete
  geometry, layout, and group calculation test also runs in `.zib`
  from source and saved `.zir`. Kryon's accessibility, drag and drop, theme,
  popup, transition fade, numeric input, text row, and drag policies also build
  and run as `.zib` bundles
  from both inputs, independently of the UI runtime. The popup ownership test
  includes frame IDs above `INT64_MAX`. Swipe direction, drag, cancellation,
  and release policy also runs from source and saved `.zir` across native
  targets and `.zib` through ordinary module imports. Image fit, placeholder,
  and clipped-strip geometry have the same source and saved-IR target coverage.
  Kryon's `ImageProps` record now uses portable strings for asset paths and
  alt text. Its fit, source selection, draw eligibility, and default tint
  decisions are tested with that record across C, C++, Go, and `.zib`. The
  checked `Image(ImageProps)` widget now resolves class styles, selects asset
  dimensions or a supplied texture handle, and queues a portable clipped image
  draw or missing-asset label. Source and saved `.zib` tests exercise the host
  dimension and raster contracts; C, C++, and Go run the texture-backed path.
  Concrete desktop and browser image hosts remain unfinished.
  Kryon's shared style values and fill-state records now support Separator and
  Progress paint decisions as ordinary imported modules in those targets.
  Kryon's Progress composition now selects fonts and computes layout in checked
  `.zi`, using generic host glyph measurement and raster capabilities. Source
  and saved `.zir`, `.zib`, C, C++, and Go tests cover that path. Kryon now
  resolves Progress's track, fill, and label roles from a portable owned
  320-rule table through its checked style cascade. Kryon's one-argument
  `Progress` painter now reads an installed rule table from an ordinary Ziran
  global and uses Ziran-defined default faces. KSS text can populate that
  table through a checked parser bridge. Kryon's checked retained tree now
  keeps node identity across persistent bundle runs. Progress and Separator
  submit generic paint commands that are rasterized after a successful tree
  commit. Retained layout, input routing, other widgets, and the pointer-backed
  native style loader remain unfinished.
  Window placement and drag policy now runs from source and saved IR in native
  targets and `.zib`, with window flags imported from a normal Ziran module.
- Reachable `#foreign host_api` calls now appear as module/function
  capabilities in `.zib`. The loader verifies the capability list against the
  linked IR. `build/libziran.a` and `include/ziran_host.h` expose an opaque
  bundle handle, capability enumeration, and bindings for integer, real, string,
  enum, plain record, and void calls. Record fields retain declared names and
  types, including nested records; returned fields are checked before use.
  Numeric, boolean, and string slice parameters can cross a synchronous host
  call. The host edits a typed element array, which the VM validates and copies
  back into the borrowed Ziran array. Source and saved bundles and generated
  C, C++, and Go run the same buffer example. The VM rejects overlapping
  mutable host slice arguments and invalid returned element types.
  `BundleRun` checks all required bindings before execution.
  The CLI runner also checks required bindings before execution. Source and
  saved-IR bundles, unused extern pruning, list tampering, missing binding
  preflight, malformed record returns, and pointer-bearing record rejection
  are tested. Pointer, array, and slot host calls remain unsupported.
- Portable strings now carry immutable UTF-8 bytes with exact byte lengths,
  including embedded nulls. The verifier and interpreter cover literals,
  equality, read-only byte indexing, `.length`, parameters, returns, and
  record fields. `make check` compares source and saved-IR bundles with C,
  C++, and Go on a string program and rejects out-of-range indexing.
- Portable bundles retain referenced module globals with default values,
  including records and fixed arrays. The VM reads and writes them across
  imported function calls, and reclaims replaced values without losing
  globals reached by a caller. Source and saved `.zir`, `.zib`, C, C++, and Go
  tests cover value isolation and repeated mutation. Each `BundleRun` starts
  fresh; `BundleInstantiate` preserves globals across repeated
  `BundleInstanceRun` calls on one instance. Explicit initializers remain
  unsupported.
- Portable execution borrows read-only record and array parameters and
  reclaims temporary values after ordinary function calls while retaining
  copied results. A repeated nested call over a 4,096-record array checks
  value isolation without exhausting the VM's allocation limit. Replaced
  record and array storage is reclaimed after a value copy; the portable VM
  currently allows up to 256 MiB each of tracked record and array allocations
  so large checked parsers such as Kryon's KSS parser can execute.
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
- Native C++ lowers Jai prefix pointer record fields through the same scalar
  type mapping as C. A non-UI source and saved-IR record with `*s32` and
  `*char` fields compiles and runs on C++.
- Native C, C++, and Go accept Jai `null` for explicitly typed raw pointers, in
  comparisons, assignments, calls, and pointer-returning conditionals. The
  checker rejects untyped `null` bindings and scalar assignments. Legacy
  Native Go's legacy internal `const char*` type still maps to a string, so
  nullable C text-pointer interop is not supported across targets. Native
  pointer expressions use Jai `*value`
  for address-of and `<<pointer` for dereference, including writes through a
  pointer. The checker rejects addresses of temporaries and dereferences of
  non-pointers. Raw pointers remain outside `.zib`.
- The shared checker resolves `record_pointer.field` against declared record
  fields, including imported records, and reports unknown fields. Checked
  pointer-member reads and writes now emit from source and saved `.zir` on
  native C, C++, and Go, including nested fields and computed pointer bases.
  The C-style `->` source operator is rejected. Type annotations require Jai
  prefix `*Type`; suffix `Type*` and `const` qualifiers are rejected in source
  and saved IR. Existing downstream Kryon source using those spellings still
  needs migration before cutover.
  Raw pointers remain outside `.zib`; broader host-bound native bodies still
  need checked lowering.
- The inherited `#intrinsic "web"` modifier and its two name-based browser
  shims are gone. Browser behavior now requires an explicit host or target
  extern. C/C++ output no longer injects Kryon's inspection header for
  ordinary functions, and generated temporaries and header guards use Ziran
  names. Saved IR rejects the retired intrinsic import tag.
- Unknown directives, function modifiers, declaration modifiers, and top-level
  forms are rejected generically; this includes old `#ui`, `#style`, and
  app/route forms. The embedded Kryon
  runtime declarations and copied `src/kry_std` implementation have been
  removed. JSON diagnostics no longer require Kryon code.
- `#instance` is rejected as a language modifier. Its special statement and
  capture fields, retained-state emission, and IR serialization have been
  removed; a state library can supply that behavior through ordinary calls.
- UI tree and DOM property fields have been removed from the statement IR.
  App, route, style, and inspector metadata have been removed from the IR and
  backend entry paths. Generic calls named `Image` and record block calls
  named `Button` are covered by standalone tests. The Go backend no longer
  drops standalone `BeginTree` and `EndTree` calls or maps `Canvas` and
  `CanvasResult` by name. The parser no longer recognizes old `app` and
  `route` block words. Source, saved-IR, and portable bundle tests cover
  these names as ordinary declarations and calls.

## Still required

- Complete Jai syntax coverage and replace Ziran-only `variant`, payload
  `match`, postfix `?`, and host directives with language-compatible forms.
  Direct type application now works in record fields, signatures, globals,
  locals, and nested generic variant payloads; further expression forms need
  conformance coverage. Jai `cast(Type)` and `Type.{...}` are accepted and
  C-style casts and compound literals are rejected; array declarations accept
  `.[...]`. Jai primitive spellings and ASCII `#char` are accepted. Also add
  owned growable collections.
  The current language has
  plain records, enums, applied generic records and variants
  with exhaustive variant matching, fixed arrays, and borrowed slices;
  defining ownership and allocation failure across C, C++, Go, and `.zib`
  remains necessary before a portable `Vec` API can ship. The intended
  [owned-value contract](OWNED_VALUES.md) records those semantics.
- Audit parser, checker, IR, and backend paths for remaining UI assumptions.
  Finish general typed block calls, callable child slots, and imports for
  ordinary libraries. Named result binding now works for the tested typed
  record call, and one synchronous captured child slot works in native targets
  and `.zib`; block-call and slot coverage is still a small subset.
- Make structured `.zir` authoritative across all features: the checker now
  validates saved expression graphs directly, and the shared typed emitter and
  portable VM consume them. Legacy native lowering still reads statement text
  for functions outside that subset. Remove that fallback and remaining UI-era
  fields from the IR model. Source, saved-IR, and mixed builds rerun the checker
  across all modules.
- Extend the `.zib` linker and verifier beyond the current subset: remaining
  control flow, enum initializer expressions, unresolved array bounds,
  slice storage in aggregates, host slice returns and record slice parameters,
  state, and all checked expressions. Extend host capabilities to portable handles, arrays, and
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
