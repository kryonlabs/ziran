# Language direction

This records the intended language requirements behind the two-repository
split. It is a target, not a list of features already shipped. See
[Implementation status](IMPLEMENTATION_STATUS.md) for the current compiler.

## General language first

Ziran must be useful for command-line tools, services, libraries, numerical
programs, and UI applications. Kryon is the first demanding library client,
not the definition of the language. Modules, typed records, ordinary calls,
block calls, callable slots, FFI, and host capabilities have the same meaning
whether a program imports Kryon or never uses a display.

The standard library belongs to Ziran and provides general facilities.
Kryon's widgets, layout, rendering, styling, and accessibility remain in
Kryon. No copied `kry_std` tree or UI-specific standard-library namespace is
part of the end state.

## Laws and LLM development

Laws are executable or mechanically checked constraints on a program's
behavior and interfaces. They must have stable names, source spans, and
machine-readable results, so an LLM-driven edit can identify what failed and
repair it. A law is a build gate: an unsupported or unproved obligation cannot
be silently treated as passed. Tests complement laws by checking examples and
integration behavior; they are not presented as proofs.

The intended development loop is to state invariants, implement code, check
laws, run conformance tests, and retain the checked result in `.zir` and
`.zib` metadata where needed for independent validation. Diagnostics should
identify the violated law and the smallest relevant source span. This is a
tooling requirement for LLM-generated code and human-written code alike.
The exact source syntax and proof format are still to be designed and should
not be inferred from today's inherited compiler law checks.

## Parallel computation

Independent computation should be expressible without hand-written thread or
GPU management in every call site. The language and runtime must define which
operations may run concurrently, their memory and effect rules, and the
observable ordering guarantees. CPU and GPU implementations must preserve
those semantics; unsupported execution paths must report a capability or
target error instead of silently changing the result.

Parallel execution is a target requirement, not a claim that today's C/Go
subset auto-parallelizes or has a GPU backend. The implementation must be
verified with deterministic reference behavior, parallel runs, and tests for
data races and backend parity before being treated as supported.

## Jai source syntax

Ziran's source grammar targets Jai syntax. Polymorphic type declarations use
`Box :: struct($T: Type) { value: T; }`, and named type values use
`IntBox :: Box(s32)`. Bracketed declaration parameters and the `specialize`
keyword are no longer accepted. The compiler is not yet a complete Jai parser:
`variant`, payload `match`, postfix `?`, and several host directives are
Ziran extensions. Direct type applications such as `Box(s32)` now work in
record fields, function signatures, globals, and local declarations. These
extensions are tracked as syntax gaps rather than described as Jai features.
Type applications may nest, and whitespace around their arguments does not
change the resulting type. Generic variant payloads may contain applied types.
The expression parser accepts Jai `cast(Type) value` and `Type.{field = value}`.
Array declarations accept `.[values]`. C-style casts and compound literals
are rejected, including when strict checking is disabled.
Source uses Jai primitive spellings `s8`, `s16`, `s32`, `s64`, `float32`, and
`float64`; the compiler translates them to its existing IR types. The old
`i8`/`i16`/`i32`/`i64`/`f32`/`f64`/`double` spellings are rejected in source.
ASCII byte characters use `#char "A"`; single-quoted character literals are
rejected.
Raw pointer null values use Jai `null`; the old `nil` source spelling is
rejected.
Pointer expressions use Jai `*value` to take an address and `<<pointer` to
read or write through a pointer. Unary C-style `&value` is rejected; binary
`&` remains the bitwise operation.
Member access through a record pointer uses `pointer.field`, with an implicit
dereference. C-style `pointer->field` is rejected.
Pointer type annotations use prefix `*Type`. C-style suffix `Type*` and
`const` qualifiers are rejected in source and saved IR.
Exported native symbols use Jai's standalone `#program_export` immediately
before a function declaration. The old `#export` signature modifier is rejected.
File-scope variables use `name: Type;` or `name: Type = value;`. The old
`name :: Type #global` and C-style `static name: Type` forms are rejected.
Visibility for following declarations uses Jai's `#scope_file`,
`#scope_module`, and `#scope_export` directives. The old `#private` suffix is
rejected. The current one-file-per-module model treats file and module scope
the same for declarations in that file.
Foreign procedures use `lib :: #system_library "lib";` and
`Call :: (...) -> Ret #foreign lib;`. An alternate symbol may follow the
library name in quotes. `#extern` is rejected in source. Ziran's `host_api`
library name still denotes a host capability in its backends.
Every source file derives its module name from its filename. The old
`#module` directive is rejected. Enum branches use Jai's `if value == {
case Type.Member; ... }` form; `#complete` requires every enum member and
source `match` on enums is rejected.

## Evolution

The language can grow without embedding one library's concepts in its core.
While Ziran is experimental, source syntax, library APIs, `.zir`, and `.zib`
have no backward-compatibility promise. Prefer a coherent language design over
compatibility shims; version incompatible binary formats and reject old input
with clear diagnostics. Reproducible compiler output and conformance suites let
new backends and LLM-authored code be checked against the same semantics.
