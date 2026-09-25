# Implementation status

This page reports the local repository as it exists now. [Architecture](ARCHITECTURE.md),
[`.zir`](ZIR.md), and [`.zib`](ZIB.md) define the target state.

## Working now

- Jai `using record: Type` parameters and local declarations, plus `using
  record;` and nested paths such as `using entity.position;` in a procedure
  body, promote record fields into lexical lookup. Explicit local bindings
  shadow promoted fields; conflicting promoted
  names are diagnosed. Struct fields declared `using field: Record` promote
  contained fields through nested records and concrete generic applications.
  Reads and writes retain their nested storage layout in checked IR; source
  and saved IR agree in C, C++, Go, and `.zib`. Pointer-backed `using` fields
  work in native targets, while portable bundles retain their pointer limit.
  Polymorphic procedures preserve parameter, local, and imperative `using`
  declarations in saved templates and lower them for each concrete call.
  Import re-exports, `#as`, and
  `using,only`/`except`/`map` modifiers remain gaps.
- Jai-style `#program_export` on its own line or inline exports a procedure's
  native symbol. A quoted linker name works in C/C++; Go reports that override
  as unsupported. File-scope variables use `name: Type` declarations.
  `#export`, `#global`, and C-style `static` source forms are rejected. Source
  and saved IR retain these exports and globals across native and portable
  builds.
- File-scope variable initializers and constants pass through the Jai
  expression parser during checking. C-style compound literals and ternary
  expressions are rejected there before native output, including from saved
  IR. The C++ and Go backends no longer carry C-style compound-literal
  rewrites; Go also no longer translates C casts, `NULL`, or C scalar type
  aliases in file-scope expressions. The source reader no longer joins C-style
  adjacent string literals or multi-line `?:` fragments.
- Jai `#must` after a procedure result type requires callers to use the
  result. The checker enforces it for ordinary, imported, polymorphic, and
  foreign procedures in source and saved IR.
- Jai `#compile_time` evaluates to true in supported compile-time procedure
  execution and false in C, C++, Go, and portable runtime code. It is a
  boolean expression, not a declaration constant or direct `#if` condition.
- Jai `#caller_location` in a parameter default supplies the calling file's
  full path and physical line as a `Source_Code_Location` record. Inferred and
  explicitly typed defaults work through imports, polymorphic calls, and
  `#load`; explicit arguments override the default. Source and saved IR agree
  in C, C++, Go, and portable builds. Compile-time calls consuming this record
  remain outside the current `#run` evaluator.
- Jai `#string DELIMITER` raw multiline literals preserve their body bytes,
  including whitespace and the newline before the closing delimiter. Source
  and saved IR execute identically on the portable runner, C, C++, and Go.
  `zi-fmt` preserves raw bodies and closing delimiter columns. The existing
  source line and string literal size limit applies after the literal is
  lowered.
- Nested block comments and inline `//` comments are stripped without changing
  quoted strings or source line locations. Unclosed comments are diagnosed in
  entry and loaded files; source and saved IR agree across portable, C, C++,
  and Go execution.
- Legacy `# ` comments are rejected at file, type, and function scope. Use
  `//` or nested `/* ... */` comments.
- Compile-time `#if` branches and `#assert` are resolved by the frontend.
  Declaration guards and target-specific assertion fallbacks have been removed;
  a selected failing assertion stops source or saved-IR checking before C,
  C++, Go, or portable output.
- Semicolons separate multiple declarations or statements on one source line,
  including one-line record bodies and function bodies. The parser retains
  `#ifx` arm separators and still rejects C-style `for` headers. Source and
  saved IR execute alike in the portable runner, C, C++, and Go.
- `if` and `else if` accept Jai's optional `then` before a block or one
  statement. Inline `else` statements also work, including after a closing
  brace. Multiple braced arms, nested arms, and empty blocks may share a
  source line; source and saved IR agree in portable, C, C++, and Go execution.
- Source foreign declarations use Jai's `#system_library` and `#foreign`
  forms. The compiler resolves those library declarations to its existing
  host, C, or Go import model before saving IR. The old `#extern` source
  modifier is rejected.
- Source accepts Jai `#scope_file`, `#scope_module`, and `#scope_export` for
  following procedures, globals, types, and constants. Private declarations
  are excluded from imported lookup, native headers hide private types and
  constants, and private globals use internal linkage. Scope visibility
  survives saved IR. `#private` is rejected. Loaded files join their caller's
  module; file-private declarations, imports, and system libraries are visible
  only to their own source file, including compile-time conditions and size
  queries. Separate loaded files may reuse private procedure, constant, global,
  and type names; checked IR assigns distinct identities before native or
  portable output. Constant aliases, native global initializers, record
  construction, and local shadowing retain the correct file-local binding in
  source and saved IR.

- The compiler frontend and C/C++/Go backends have been extracted into a separate
  Ziran repository. `zi2zir` checks and saves `.zir`; `zi2c`, `zi2cpp`, and
  `zi2go` compile `.zi` or saved `.zir` input.
- `make check` passes standalone non-UI programs, a two-module import, and an
  imported typed record call named `Button` through generated C, C++, and
  native Go. A local declaration binds its non-void result, with source and
  saved `.zir` builds across those targets and matching source/saved `.zib`
  execution. The call is resolved from its declaration, not its name.
- Jai procedure type aliases such as `Child :: #type (s32) -> ();` and
  `Compute :: #type (s32) -> s32;` can be passed as callbacks to imported
  functions. Source and saved `.zir` builds execute named callbacks with void,
  scalar, and record results in C, C++, Go, and `.zib`. Capture-free values
  can be stored in records, fixed arrays, and globals, and returned from
  functions. Entry builds retain their named targets and remove unread record
  fields from closed-program layouts. The former `#slot` declaration and
  capturing body syntax are rejected.
- Context-inferred `.{...}` record literals work in declarations, assignments,
  returns, record fields, and arguments to imported functions. Source and saved
  IR produce the same result in C, C++, Go, and `.zib`; missing type context
  and unknown fields are rejected.
- Jai `defer { ... }` blocks run their statements in order on scope exit,
  including early returns and loop exits. Source and saved IR agree in C,
  C++, Go, and `.zib`. Control transfer or a nested `defer` inside a deferred
  block still needs lowering support.
- Jai inclusive integer ranges accept named binders, implicit `it`,
  `it_index`, and `for <` reverse iteration. Bounds run once, and `break`,
  `continue`, nested ranges, and `defer` work across source and saved IR,
  C, C++, Go, and `.zib`. Fixed arrays and borrowed slices also support
  value and index bindings, reverse iteration, and nested loop control across
  those targets. Native C, C++, and Go support `for *` pointer iteration for
  in-place element changes; portable bundles reject pointers. Collection
  expressions run once and value bindings copy elements. C-style three-clause
  headers are rejected. Jai `for_expansion` iterables remain unsupported.
- Fixed arrays, borrowed slices, and strings now expose read-only Jai
  `.count` values of type `int`, including in range bounds. The inherited
  `.length` source spelling is rejected. Single-statement `if`, `while`, and
  `for` bodies now normalize to checked blocks. Named `break` and `continue`
  target enclosing `for` binders or named `while` condition binders; defer
  cleanup and range advancement apply when those controls cross nested loops.
- `check`, `ir`, `build`, and `bundle` discover extensionless imports
  transitively. Repeated `--module-path DIR` options locate ordinary libraries
  outside an app root; explicitly supplied modules take precedence. A separate
  app root and two-module library are tested from source and saved `.zir`,
  including generated C and `.zib`. Kryon's drag policy test loads its `src/ui/`
  library from an app entry file through the same path, across C, C++, Go,
  and portable bundles.
- Jai `#import, file "relative/path.zi";` resolves a source file relative to
  its importer. Source and saved IR link the same module, and C, C++, Go, and
  portable builds agree. Named `Alias :: #import "module";` and
  `Alias :: #import, file "relative/path.zi";` expose public procedures,
  constants, and record types through `Alias.Name`; named imports do not leak
  unqualified names. `Alias :: #import, dir "relative/directory";` loads the
  directory's `module.zi`; ordinary module lookup also finds
  `name/module.zi`. Source and saved IR produce the same C, C++, Go, and
  portable output for this form. Nested `#load "relative/file.zi";` adds
  declarations to the current module, keeps source file diagnostics, and
  resolves imports relative to each loaded file. Cycles and missing files are
  rejected; inactive compile-time branches skip loads. `#import, string`
  compiles quoted or raw `#string` source as an embedded module, with named
  and unqualified imports. Compile-time branches inside embedded source can
  call modules imported earlier in that source. Source and saved IR bundles
  agree, and C, C++, and Go execute the generated modules.
- Ordinary `#import` now accepts only module identifiers. C header paths and
  C-style angled imports are rejected, including from saved IR; unknown calls,
  fields, and stored types no longer pass checking merely because a C header
  was imported. Downstream C-backed modules that used header imports require
  source migration.
- Module inputs accept `.zi` source and `.zir` IR only; `.kry`, `.kir`, and
  `.krb` are rejected by the shared loader. Kryon owns the UI test fixtures;
  Ziran keeps a non-UI array and UTF-8 byte law fixture that runs from source
  and saved IR through native C, C++, and Go.
- `zi2zir` writes experimental binary `.zir` version 33 after checking all
  input modules together. C, C++, and Go can read saved modules without reparsing
  `.zi`; their imports are relinked from serialized module identities. The
  reader rejects malformed headers, versions, truncated data, invalid
  structural references, expression cycles, retired
  statement kinds. Retained-state fields are absent from the IR schema. C, C++, Go, and bundle builds
  rerun strict language checking on saved expression graphs and compare the
  resulting serialization byte for byte with the input. The tested typed
  subset takes behavior from those graphs and serialized branch flags even
  when stored statement text differs. Deterministic output is checked on a
  sample, and a mixed source/IR
  C build runs.
- Jai `union` declarations, including generic unions, retain their shared
  field storage and maximum-field layout in checked `.zir`. C and C++ compile
  source and saved IR unions with overlapping fields. Go generation and the
  portable linker reject reachable unions until those targets have overlapping
  storage semantics.
- `zi2zib bundle --root DIR --entry module:function -o FILE` builds an
  experimental version 20 `.zib` from source or saved IR. `zi2zib run FILE`
  loads and executes the validated scalar, plain record, enum, and fixed-array
  subset without a
  display or Kryon.
  The test runs an imported two-module call and compares bundle bytes from
  source and saved IR. The current runner supports zero-argument entry
  functions with `s32`/`int`/`bool`/`void` results, and helper functions with
  `s64`/`u8`/`u32`/`u64`/`float32`/`float64`/`string`, enum, or plain record results. It supports scalar, enum,
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
  Entry linking now removes effect-free constant branches, short-circuited
  expressions, false loops, and statements after unconditional exits before
  tracing calls or stored procedure values.
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
  parameters and returns, `.count`, indexed reads and writes, and array
  replacement while a view is live. Bounds failures reject execution. The
  checker rejects slices in records and globals, host slice returns, and
  returns that borrow a local array. Unresolved fixed-array bounds remain
  outside the portable subset.
  Native C, C++, and Go targets also permit a temporary slice of a fixed
  array field reached through a direct record-pointer parameter. The caller
  owns that record for the call; the checker rejects returning the slice.
  Portable bundles still reject pointer programs.
  Borrowed arrays retain record elements written by called functions after
  those functions return; the portable slice test covers replacing a nested
  record in a live view across calls.
  The test runs record and enum functions across imported modules.
  Source `#enum`, `variant`, payload `match`, postfix `?`, `guard`, C-style `switch` and
  `goto` with labels, `state` blocks, C-style locals, and raw C statements are
  rejected. Native C, C++, and Go builds check source unconditionally and reject
  `--strict` and `--no-strict`. The standard `Option` and `Result` templates
  are now generic records with explicit status fields. The checked native
  emitters escape target keywords used as parameter or local names; source and
  saved IR tests run `switch` and `goto` bindings, including a generated-name
  collision, across C, C++, and Go. Go globals keep source spelling when
  legal; native emitters escape target keyword globals, including collisions
  with the generated escape name. A Go global and type whose names differ only
  by case or underscores remain distinct. Constants use the same keyword and
  collision handling without defining C keywords as macros; source and saved
  IR tests include dependent constants and fixed-array bounds. Generic record templates use named
  `Name :: Generic(Type)` declarations or direct
  `Generic(Type)` annotations; `std/pair.zi` also provides `Pair`. Concrete
  record fields work in source, saved IR, C, C++, Go, and `.zib`. Top-level boolean
  `&&` and `||` expressions in simple statements and loop conditions retain
  short-circuit evaluation, including nested forms. Jai `ifx` conditional
  expressions, with or without `then`, replace C-style `?:`; top-level
  conditional expressions with
  typed destinations or loop conditions preserve selected-arm evaluation. Lazy expressions
  embedded in outer calls remain unsupported.
  Jai `#ifx` selects a constant, global initializer, or function expression
  arm before strict checking and saved-IR generation. It accepts the Jai
  semicolon before `else`, nested selections, boolean and integer constants,
  constant aliases, and compiler-host `OS` comparisons with `.WINDOWS`,
  `.MACOS`, or `.LINUX`; unused arms are not type-checked. `#run`, `#if`,
  `#ifx`, and `#assert` evaluate pure procedures with signed integer (up to
  `s64`), unsigned integer (up to `u32`), or boolean parameters/results. Local
  declarations and assignments, branches, bounded `while` and numeric range
  loops, nested calls, and imported calls work in `#run`, `#if`, `#ifx`, and
  `#assert`. `#if` can resolve imports declared before its condition while
  parsing, including imported `size_of` layouts. Other imported calls and
  dependent definitions resolve after module linking; their literal values
  and selected arms are saved in `.zir`. Pure string and floating-point
  expressions, including imported calls, local assignments, and default
  parameters, work in `#run`; `#assert` can compare those results. String
  comparisons use the same escape and UTF-8 decoder as the portable runner.
  Source-selecting `#if` can use earlier `#run` definitions when their
  procedures and imports are available while parsing. Effects, unbounded
  execution, aggregate results, and general metaprogramming are unsupported.
  One-line function bodies are parsed as ordinary statements instead of
  being silently skipped.
  Jai `#if ... else #if ... else` selects file-scope declarations and function
  statements at parse time. Unselected branches do not enter source or saved
  IR; source and saved bundles, C, C++, and Go agree for nested constant
  branches. The old `#else_if` and `#else` forms are rejected. Conditional
  record fields and enum members are selected before their layouts are checked.
  Explicit enum members accept Jai `Member :: value` declarations. Source and
  saved IR produce the same values in C, C++, Go, and portable bundles.
  Named enums now use Jai's default `s64` storage or an explicit integer
  backing type. `enum_flags` assigns successive bits to members without an
  explicit value. The checker rejects members outside their backing range,
  and `size_of` uses the backing width. Source and saved IR agree across the
  portable runner and native targets. Jai `#specified` requires explicit
  member values for both `enum` and `enum_flags`, and saved IR records the
  modifier. Values above `s64` remain unsupported
  in the current portable enum evaluator, even for `u64` backing. Typed flags
  accept integer initializers and assignments, qualified `Type.Member`, and
  contextual `.Member` references. The checker validates flag arithmetic,
  bitwise operations, equality, and corresponding compound assignments.
  Portable execution and C, C++, and Go output agree on these operations.
  Regular enum members also have typed `Type.Member` and contextual `.Member`
  expressions, including call arguments and returns. Enum and scalar
  `if`/`case` arms support terminal `#through;` with source, saved IR,
  portable, C, C++, and Go agreement. Integer, boolean, floating, and string
  cases accept a final `case;` default. Deferred actions run at case boundaries
  and before `#through;`. Unknown function-body directives are rejected.
  Jai `size_of(Type)` folds before source or saved IR emission for supported
  scalars, pointers, `string`, borrowed slices, fixed arrays, records, unions,
  named concrete generic types, and nested applications such as
  `Wrapper(Box(s32))`. It works in
  constants, array bounds, globals, `#run`, `#if`, `#ifx`, and function
  expressions. The old `sizeof` spelling is rejected in source. Jai `int`
  maps to `s64`. In function bodies, `size_of(type_of(expression))` uses the
  checker's inferred type and does not evaluate its operand. File-scope
  `size_of(type_of(expression))` works for previously declared same-module
  globals, record fields, and procedure calls in constants, global
  initializers, `#run`, `#assert`, `#if`, and `#ifx`; the operand is not run.
  Standalone `type_of` values and forward or imported file-scope queries remain
  unsupported. Foreign-record size queries still require their Jai layouts.
  Jai `#line` resolves to the physical source line before checking and remains
  stable through saved IR, native targets, and `.zib`, including `#load` files.
  `#file` and `#filepath` resolve to the full source filename and containing
  directory, including in loaded files. `#procedure_name()` resolves to the
  declared procedure name in bodies, defaults, and pure `#run` calls; source
  use outside a procedure is rejected.
  Untyped integer locals inferred with `:=` now use `s64` storage, including
  when their values exceed 32 bits, across saved IR and all supported targets.
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
  equality, read-only byte indexing and ranges, `.count`, parameters, returns,
  and record fields. `text[low:high]` returns a borrowed byte range as a
  `string`; bounds are checked in the portable VM and generated C, C++, and
  Go. Callers must choose UTF-8 codepoint boundaries when slicing text.
  `make check` compares source and saved-IR bundles with C, C++, and Go on a
  string program and rejects out-of-range indexing.
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
  `*u8` fields compiles and runs on C++.
- Native C, C++, and Go accept Jai `null` for explicitly typed raw pointers, in
  comparisons, assignments, calls, and pointer-returning conditionals. The
  checker rejects untyped `null` bindings and scalar assignments.
  Native pointer expressions use Jai `*value`
  for address-of and `pointer.*` or prefix `<<pointer` for dereference,
  including writes through a pointer. `++`/`--` are rejected. The checker rejects
  addresses of temporaries and dereferences of non-pointers. Raw pointers
  remain outside `.zib`.
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
  names. Retired intrinsic, capability, and host import tags have no slot in
  the new IR schema; host calls use ordinary extern imports.
- Unknown directives, function modifiers, declaration modifiers, and top-level
  forms are rejected generically; this includes old `#ui`, `#style`, and
  app/route forms. The embedded Kryon
  runtime declarations and copied `src/kry_std` implementation have been
  removed. JSON diagnostics no longer require Kryon code.
- `#instance` is rejected as a language modifier. Its special statement and
  capture fields, retained-state emission, and IR serialization have been
  removed; a state library can supply that behavior through ordinary calls.
- The parser no longer creates inline closures. Their capture fields, bundle
  cloning, backend wrappers, and VM parent frames have been removed. Named
  procedure values remain supported.
- UI tree and DOM property fields have been removed from the statement IR.
  App, route, style, and inspector metadata have been removed from the IR and
  backend entry paths. Generic calls named `Image` and record calls
  named `Button` are covered by standalone tests. The Go backend no longer
  drops standalone `BeginTree` and `EndTree` calls or maps `Canvas` and
  `CanvasResult` by name. The parser no longer recognizes old `app` and
  `route` block words. Source, saved-IR, and portable bundle tests cover
  these names as ordinary declarations and calls.

## Still required

- Complete Jai syntax coverage and replace remaining Ziran-only host
  conventions with language-compatible forms. Variant declarations and
  generated operations, checker branches, and IR fields have been removed;
  postfix `?` lowering has also been removed.
  The inherited `Name :: C_declarator #type` spelling is rejected and its
  parser, checker, IR exception, and emitters are removed. Kryon's host adapter
  now declares callbacks with Jai `#type (...) -> Result #c_call;` and stores
  the resulting C callback pointers in records. Native C and C++ emit that ABI;
  reachable callback values cannot enter portable bundles, and Go output
  rejects the declarations.
  Direct type application now works in record fields, signatures, globals,
  locals, and nested generic records; further expression forms need
  conformance coverage. Jai `cast(Type)`, `Type.{...}`, and context-inferred
  `.{...}` record literals are accepted and
  C-style casts and compound literals are rejected; array declarations accept
  `.[...]` directly in the expression parser without rewriting through a
  C-style literal. Jai `#this` resolves the enclosing procedure for calls and
  typed procedure values across native and portable targets, and resolves the
  enclosing type in plain and polymorphic struct fields. Saved IR preserves
  self references even when a parameter shadows the procedure name. Jai
  primitive spellings, including Jai `float` as `float32`, and ASCII `#char`
  are accepted; `#char` lowers to an integer expression and the retired
  character IR kind and scalar handling are gone. The inherited source
  `char` type is rejected while `char` can name local
  bindings and record fields; C and C++ lower reserved field names without
  changing source names. Kryon's C callback declarations use Jai `*u8` for borrowed byte
  pointers. Remaining top-level type and exported procedure keyword
  names and cross-module name collisions
  still need package-wide native name mapping. `Vec(T)` now provides growable
  storage for globals and record fields across C99, C++, Go, and `.zib`, with
  checked indexing, push, clear, free, and swap. The checker rejects copies and
  value passing. Move, automatic drop, pop, fallible lookup, and a string
  builder remain to complete the owned-value model. Go cannot recover from
  physical allocation failure. The intended
  [owned-value contract](OWNED_VALUES.md) records those semantics.
- Audit parser, checker, IR, and backend paths for remaining UI assumptions.
  Finish general procedure values and imports for ordinary libraries.
  Named capture-free callbacks, including stored values, and typed record
  calls work in native targets and `.zib`. Jai named call arguments retain
  source evaluation order while binding by parameter name in source, saved IR,
  native targets, `.zib`, and pure integer compile-time calls. Declared
  procedures now accept Jai default arguments, including defaults before
  required named parameters and inferred `name := expression` parameters.
  Inference resolves literals, imported constants, and procedure results in
  the declaring module. Omitted defaults are added to checked calls in
  source order and survive saved IR, native targets, `.zib`, and supported
  compile-time evaluation. Concrete procedure defaults execute through a
  checked helper in the declaration module, so local and imported caller names
  cannot capture the default's constants, globals, or procedure calls.
  Polymorphic defaults with a declaration-resolvable expression type now use
  the same helper path; scope-independent literals and literal `T.{...}`
  record defaults preserve their specialized type context. Concrete field
  expressions in polymorphic record defaults resolve in the declaring module,
  including procedure calls when an importing caller has a same-named local
  procedure. Fields whose expression type depends on the specialization still
  need specialization support. Defaults on
  procedure-type values and broader procedure value support also remain.
- Make structured `.zir` authoritative across all features: the checker now
  validates saved expression graphs directly, and the shared typed emitter and
  portable VM consume them. The C, C++, and Go body emitters no longer have a
  statement-text fallback. Source, saved-IR, and mixed builds rerun the checker
  across all modules. Remaining target-specific declaration and import
  lowering still needs a typed representation.
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
  declared interfaces. The checked Kryon archive builds all 191 maintained UI
  modules; native host integration, full renderer behavior, and downstream
  cutover remain incomplete.
- Pass the full supported target and downstream application gates, then make
  the planned single breaking cutover. See [Migration](MIGRATION.md).
