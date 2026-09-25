# Language direction

This records the intended language requirements behind the two-repository
split. It is a target, not a list of features already shipped. See
[Implementation status](IMPLEMENTATION_STATUS.md) for the current compiler.

## General language first

Ziran must be useful for command-line tools, services, libraries, numerical
programs, and UI applications. Kryon is the first demanding library client,
not the definition of the language. Modules, typed records, ordinary calls,
procedure values, FFI, and host capabilities have the same meaning
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
keyword are no longer accepted. The compiler rejects Ziran-only `#enum` and `variant`
declarations, `match` statements, postfix `?`, `guard`, C-style `switch` and
`goto` with labels, `state` blocks, C-style locals, and raw C statements in
source. Jai enum cases use `if value == { case ... }`. Native builds run the
language checker unconditionally; `--strict` and `--no-strict` are rejected. Other syntax
differences remain under audit.
Direct polymorphic procedure calls use `Identity :: (value: $T) -> T {
return value }`. A procedure may bind one type name, use it in later parameter
types and its result, and call another polymorphic procedure. Calls specialize
for concrete argument types, including records from an importing module.
Untyped numeric literals need a concrete context to infer `T`. Polymorphic
procedures cannot be used as procedure values or exported with
`#program_export`.
Calls accept Jai named arguments such as `Combine(second = 2, first = 1)`.
Names bind to declared parameters, and argument expressions run in written
order before the call. The checker rejects unknown, duplicate, and missing
arguments. Named arguments work with direct, imported, polymorphic, and
procedure-value calls. Procedures accept defaults in parameter declarations,
including inferred `value := expression` defaults and defaults before required
parameters. Inferred defaults use the checked expression type, including
imported constants and procedure results. A missing default expression is
evaluated after the caller's explicit argument expressions. Defaults on
procedure-type values and call-site directives such as `#caller_location`
remain unsupported. Concrete procedure defaults use declaration-scope name
resolution. Polymorphic defaults with a concrete expression type also use a
checked helper in the declaring module. Scope-independent literals retain
their call context for type inference; defaults that require a polymorphic
type to resolve their expression still need specialization support.
Pure named calls with supported scalar values also work in `#run` and other
compile-time expressions.
The `#slot` declaration, capturing body forms, and record block calls are
rejected. Record values use `Type.{field = value}` in ordinary calls. A
procedure type can be named with `Child :: #type (s32) -> ();` or
`Compute :: #type (s32) -> s32;` and passed a named
function value. Capture-free procedure values can be stored in records, fixed
arrays, and globals, and returned from functions. The callbacks do not capture
caller locals, so callback code uses explicit state or a named function.
Procedure type results are checked across the portable runtime and generated
targets. `#c_call` procedure types use C callback pointers in native C and
C++ output and may be stored in native records. A named Ziran function with
the exact callback signature can supply a `#c_call` value in those targets.
Reachable `#c_call` values cannot enter portable bundles, and Go output rejects
these declarations.
Direct type applications such as `Box(s32)` now work in
record fields, function signatures, globals, and local declarations. These
extensions are tracked as syntax gaps rather than described as Jai features.
Type applications may nest, and whitespace around their arguments does not
change the resulting type.
The expression parser accepts Jai `cast(Type) value`, `Type.{field = value}`,
and inferred `.{field = value}` when a record type is known from a declaration,
assignment, return, call parameter, or enclosing record field.
`defer { ... }` blocks preserve statement order at scope exit and run after
later defers in the same scope. A single deferred expression or assignment
uses the same cleanup lowering.
Inclusive integer ranges use `for i: first..last { ... }`, with `for
first..last { ... }` binding the value as `it`. Each iteration also binds
`it_index`, starting at zero. `for < i: first..last { ... }` visits the same
range in reverse. Bounds are evaluated once; `continue` advances the range,
and the endpoints do not overflow when the last iteration completes. The
compiler rejects C-style three-clause `for` headers. Fixed arrays and
borrowed slices use `for values { ... }` or `for value, index: values { ... }`;
`for < values` visits elements in reverse. The default value binding is a
copy. `for * value: values` binds a pointer to each element in native builds,
so the loop can modify the collection. Collection expressions are evaluated
once. Custom Jai `for_expansion` iterables remain unsupported.
`break name` and `continue name` target an enclosing `for name: ...` loop or
`while name := condition` loop. Loop exits run defers for every scope crossed,
and range continuations advance the named range before starting its next
iteration.
`if`, `while`, and `for` may use one following statement as their body without
braces. `else` attaches to the nearest unmatched `if`; a brace on the next
line begins an ordinary block body. Jai's optional `then` separates an `if`
or `else if` condition from its body, on the same or a later line. An `else`
may also carry its single-statement body on the same line.
Semicolons may separate declarations and statements on one line. One-line
record bodies accept semicolon-separated fields; expression delimiters such
as the semicolon before a `#ifx` else arm retain their expression meaning.
Fixed arrays, borrowed slices, and strings expose Jai `.count`. Counts have
`int` type and cannot be assigned. The older `.length` source spelling is
rejected for strings, slices, and fixed arrays.
String ranges use `value[low:high]` and return an immutable borrowed `string`
view over those bytes. Bounds are checked, and callers must keep UTF-8
codepoint boundaries intact when they need valid text.
An ordinary record may still declare a field named `length`; generated C ABI
structures also retain their internal length fields.
Array declarations accept `.[values]` directly in the expression parser and
preserve that spelling in saved `.zir`. C-style casts and compound literals
are rejected at the source boundary.
Jai `#this` selects the enclosing procedure for recursive calls and typed
procedure values, even if a local binding has the same name. In record field
types it names the enclosing struct, including a concrete application of a
polymorphic struct. Saved `.zir` keeps procedure self references explicit.
Source uses Jai primitive spellings `s8`, `s16`, `s32`, `s64`, `float32`, and
`float64`; checked IR keeps those spellings. Jai `float`
is an alias for `float32`. The old `i8`/`i16`/`i32`/`i64`/`f32`/`f64`/`double`
spellings are rejected in source.
`char` is rejected as a type; it can name a local binding, record field,
global, or constant.
Jai `int` is an alias for `s64`. `size_of(Type)` is a compile-time integer for
supported scalars, pointers, `string`, borrowed slices, fixed arrays, records,
unions, and concrete generic type applications such as `Box(s32)`.
An untyped integer local inferred with `:=` also has `s64` storage.
The C `sizeof` spelling is rejected. In function bodies,
`size_of(type_of(expression))` uses the checked expression type without
evaluating the expression. File-scope queries also infer types from previously
declared same-module expressions in constants, globals, and compile-time
directives. Standalone `type_of` values and forward or imported file-scope
queries remain unsupported. `size_of` for foreign records still needs their
Jai layouts.
ASCII byte characters use `#char "A"` and infer `s64`, as integer literals
do; single-quoted character literals are rejected.
Jai raw multiline strings use `#string END` followed by their exact text and
a closing `END` at the start of a line. The opening line contains only the
delimiter, and the newline before the closing line belongs to the string.
Quotes, backslashes, tabs, and UTF-8 text in the body are preserved. The
current source line and string literal size limit still applies after lowering
to the checked IR.
Nested `/* ... */` comments and `//` line comments may appear between tokens;
comment delimiters inside quoted strings remain text. Legacy `# ` comments
are rejected. An unclosed block comment is a source error, including in a
loaded file.
Raw pointer null values use Jai `null`; the old `nil` source spelling is
rejected.
Pointer expressions use Jai `*value` to take an address and `pointer.*` to
read or write through a pointer. The older prefix `<<pointer` and C-style
`&value` forms are rejected; binary `<<` and `&` remain shift and bitwise
operations. Jai has no `++` or `--` increment/decrement operators; use
`+= 1` or `-= 1`.
Member access through a record pointer uses `pointer.field`, with an implicit
dereference. C-style `pointer->field` is rejected.
Pointer type annotations use prefix `*Type`. C-style suffix `Type*` and
`const` qualifiers are rejected in source and saved IR.
Runtime conditional expressions use Jai `ifx condition then a else b`.
The optional `then` spelling is also accepted. C-style `?:` is rejected.
Compile-time conditional expressions use `#ifx condition then a; else b`.
The semicolon between arms is optional. The frontend selects one arm before
checking or serializing expressions; its condition evaluator currently accepts
boolean and integer constants, constant aliases, arithmetic and logical
operations, and compiler-host `OS` comparisons with `.WINDOWS`, `.MACOS`,
or `.LINUX`. During translation, `#run`, `#ifx`, and `#assert` can call pure
same-module or imported procedures with signed integer, up to `u32`, or
boolean parameters and results. These procedures can use local variables,
assignments, branches, bounded `while` and numeric range loops, and nested
calls. `#run` also accepts pure string and floating-point expressions, including
imported calls, local assignments, and default parameters; `#assert` can
compare their results. String comparisons decode escapes using the same UTF-8
rules as the portable runner. `#run` results and selected `#ifx` arms are written into checked
`.zir`, so native targets and bundles receive the same result. File and system
access, aggregate results, and general metaprogramming are not available to
this evaluator. `#if` selects
source branches while parsing. It can call earlier same-module procedures or
procedures from imports declared before the condition. Earlier `#run` values
can participate when those procedures and imports are already available.
Imported type layouts can also be used in `size_of` conditions.
`#assert` must resolve in the Ziran frontend; native backends do not defer its
condition to the C preprocessor.
Statement and declaration branches use Jai `#if`, `else #if`, and `else`.
Conditions must be compile-time constants;
the frontend removes unselected branches before checking or writing IR.
Conditions in unreachable nested arms and arms after a selected branch are
not evaluated.
The older `#else_if` and `#else` spellings are rejected. Conditional fields
and enum members are also selected while parsing a declaration.
Exported native symbols use Jai's standalone `#program_export` immediately
before a function declaration. The old `#export` signature modifier is rejected.
File-scope variables use `name: Type;` or `name: Type = value;`. The old
`name :: Type #global` and C-style `static name: Type` forms are rejected.
Visibility for following declarations uses Jai's `#scope_file`,
`#scope_module`, and `#scope_export` directives. The old `#private` suffix is
rejected. `#load` adds files to one module. File-private declarations and
imports remain visible to their own file; module-scoped declarations remain
visible across loaded files. Private procedures, constants, globals, and types
with the same source name in separate loaded files receive distinct checked
identities.
Foreign procedures use `lib :: #system_library "lib";` and
`Call :: (...) -> Ret #foreign lib;`. An alternate symbol may follow the
library name in quotes. `#extern` is rejected in source. Ziran's `host_api`
library name still denotes a host capability in its backends.
Unqualified explicit file imports use `#import, file "relative/path.zi";`
and resolve relative to the importing source file. Saved IR uses the imported
module name. Named `Alias :: #import "module";` and
`Alias :: #import, file "relative/path.zi";` expose public procedures,
constants, and record types through `Alias.Name`.
`Alias :: #import, dir "relative/directory";` loads `module.zi` within the
directory. `#load "relative/file.zi";` adds that file's declarations to the
current module and may nest. `#import, string "...";` and a raw `#string`
body compile embedded source as a module. The named form
`Alias :: #import, string "...";` exposes declarations through `Alias.Name`.
`#import, file` loads `.zi` source only. C headers are outside the Ziran
module graph; foreign declarations use `#system_library` and `#foreign`.
An entry source file derives its module name from its filename. The old
`#module` directive is rejected. Jai `if value == { case ... }` branches now
accept enums, integers, booleans, floating values, and strings. Scalar cases
may end with `case;` as a default. `#complete` requires every enum member and
source `match` on enums is rejected. A case may end with `#through;` to run
the next case body. The final case cannot use `#through;`. Explicit enum member values use Jai
`Member :: value` declarations and lower to checked values in saved IR.
Each case is a lexical scope, so its deferred actions run before the next
case starts, including when the arm ends with `#through;`.
Named enums default to `s64` storage, and declarations may specify an integer
backing type. `enum_flags` assigns successive powers of two to implicit
members. `size_of` follows the backing width. Unsigned values above `s64`'s
maximum remain an implementation gap. `#specified` after the optional enum
backing type requires every member to have an explicit `::` value; it also
works with `enum_flags`.
Typed `enum_flags` values accept integer initializers, `Type.Member`, and
contextual `.Member` references. Flag values support `&`, `|`, `^`, `+`,
`-`, equality, and the corresponding compound assignments. Member references
are checked against their enum before lowering to their numeric values.
Regular enum values also accept `Type.Member` and contextual `.Member`
references in typed declarations, comparisons, returns, and calls.

## Evolution

The language can grow without embedding one library's concepts in its core.
While Ziran is experimental, source syntax, library APIs, `.zir`, and `.zib`
have no backward-compatibility promise. Prefer a coherent language design over
compatibility shims; version incompatible binary formats and reject old input
with clear diagnostics. Reproducible compiler output and conformance suites let
new backends and LLM-authored code be checked against the same semantics.
