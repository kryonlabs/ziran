# Jai syntax parity roadmap

Status: 2026-09-25. This is a work plan, not a claim that Ziran already accepts
every Jai program. [Implementation status](IMPLEMENTATION_STATUS.md) records
what works today; [Language direction](LANGUAGE_DIRECTION.md) records the broader
design. The current checked source grammar aims to use Jai syntax. Ziran is
experimental: breaking changes are welcome, and there is no requirement to
load old source, `.zir`, `.zib`, flags, or runtime entry points.

## The finish line

For each supported Jai construct, the same program must parse, type-check, and
behave consistently when built from `.zi` source or saved `.zir`, on C, C++, Go,
and the portable `.zib` runner. A backend may explicitly reject a capability
it cannot implement yet. It must never quietly substitute a different layout,
evaluation order, visibility rule, or bounds behavior. Every removed Ziran-only
spelling needs a source rejection test, and saved IR must reject forged obsolete
forms. No compatibility shim is needed.

"Jai syntax parity" is a larger claim than passing the existing Ziran suite.
Build a conformance corpus from runnable Jai examples and compare diagnostics,
inferred types, constant values, layouts, and observable execution with a Jai
compiler whenever one is available. The public [OpenJai language
specification](https://github.com/withlang-dev/open-jai/blob/main/docs/open_jai_spec.md)
is useful for finding candidate cases, but it describes an independent
implementation and cannot settle differences from Jai itself. Record uncertain
behavior as an open question and probe it before codifying a language rule.

## Current baseline

The current branch has Jai-style declarations, lexical `using` for local and
parameter records, nested and generic `using` fields, lexical enum `using`, and
data-scope enum `using`. It supports nonempty typed array literals, typed and
inferred record literals, named and default arguments, direct polymorphic
calls, scopes, `#load`, several `#import` forms, bounded pure `#run`, and
checked `.zir` graphs. The compiler rejects many inherited spellings,
including `#enum`, `variant`, postfix `?`, `#global`, `#export`, `#private`,
`#extern`, C pointer suffixes, C conditional expressions, and old Kryon file
extensions. The rejection tests should stay.

The latest code commits are `0b4f157` through `43ca012` (record and enum
`using`, typed array literals, and associated saved-IR and target tests). The
full local `make check` suite has 72 checks. Re-run it after any change to the
checker, IR, or emitters, with `DISPLAY` and `WAYLAND_DISPLAY` unset.

| Area | Current subset | Next proof of parity |
| --- | --- | --- |
| Names and scopes | Local/field record `using`; local/data enum `using`; named imports | Data record `using`, import re-exports, modifiers, order independence |
| Values and expressions | Record literals, nonempty typed arrays, direct generic calls, selected `ifx` forms | True empty arrays, nested lazy expressions, fuller type queries |
| Procedures | Named values, defaults, named arguments, direct polymorphism | Overload/variadic/operator and broader procedure-form audit |
| Compile time | Bounded pure scalars, strings, and floats | One typed evaluator, aggregates, verified effect rules |
| Native output | C/C++/Go checked body graphs; C/C++ unions | Declaration lowering, whole-program names, Go union layout |
| Portable output | Verified `.zib` scalar/record/array subset and host calls | Globals, aggregates, pointers/handles, unions, host shapes |
| Ownership | Scoped `Vec(T)` operations | Move, drop, pop, lookup, builder, borrowing across calls |

This matrix records known implementation work, not an exhaustive catalog of
Jai syntax. Each completed item should move from “next proof” to a tested
current subset with an exact test name and target list.

## Priority 1: close source grammar and name-resolution gaps

### 1. Data-scope `using` for record values

The parser currently records top-level `using Path;`, and the checker accepts
it only when `Path` resolves to an enum. Procedures already have the machinery
to promote a record's fields into lexical lookup. Extend that machinery to a
visible file-scope record or union binding, including `using value;`, a nested
path, and declaration syntax such as `using value: Record;`. Preserve explicit
binding shadowing, ambiguity diagnostics, imported visibility, and `#scope_file`
across `#load`. Lower every promoted reference into checked member access
before writing `.zir`; the backends should not interpret `using` themselves.

Start in `cmd/zir/zir_parse.c` (top-level declaration parsing) and
`cmd/zir/zir_check.c` (`activate_using`, `check_function`, and the top-level
`using` validation). Extend `tests/using.sh` with reads, writes, collisions,
forward declarations, and source/saved-IR execution on every target. Keep the
test that rejects `using StructType;` if Jai requires a value there. Confirm the
exact rule with a Jai probe before treating union promotion as settled.

### 2. Declaration-order-independent compile-time lookup

The current parser selects some `#if` branches before all later declarations
are known. Consequently a `#if` before a later `using` cannot always see the
opened enum members; some forward/imported compile-time queries have the same
ordering limit. The OpenJai reference describes top-level declaration order as
independent. Investigate Jai's actual behavior, then split top-level discovery
from branch selection or add a bounded dependency-resolution pass. Do not
resolve a condition by parsing code from an inactive branch or by changing
the meaning of file-private declarations.

Acceptance: a forward constant, import, type, and `using` in conditions and
`#run` resolve when legal; cycles produce an explicit diagnostic; source and
saved `.zir` contain only selected declarations; `#load` and embedded imports
retain their visibility rules. Relevant code is in `cmd/zir/zir_parse.c` and
the compile-time evaluator there, with `tests/compile_if_syntax.sh`,
`tests/compile_run.sh`, and `tests/using.sh` as starting points.

### 3. Import re-exports and composition modifiers

`using Alias :: #import "Module";`, `using,only(...)`, `using,except(...)`,
`using,map(...)`, and `#as` have no complete checked representation. Define
their visibility, collision, conversion, and storage rules before changing
the parser. In particular, `#as` affects conversions, so a text substitution
in a backend is insufficient. Model namespace import and conversion metadata
explicitly, resolve it in the checker, and serialize the checked result.

Acceptance: importing through a re-export preserves public/private scopes;
filters and maps diagnose unknown or duplicate names; `#as` conversions work
for values and pointers where legal; all targets agree or explicitly reject an
unsupported ABI. Cover nested imports and saved `.zir`. The [OpenJai `using`
examples](https://raw.githubusercontent.com/withlang-dev/open-jai/main/docs/open_jai_spec.md)
give candidate syntax, to be checked against Jai.

### 4. Complete the remaining procedure and expression grammar

Audit Jai examples against the parser instead of inferring coverage from a
handful of demos. High-value candidates include overload resolution, operator
declarations and calls, variadic arguments, local and anonymous procedures,
more general procedure values, polymorphic procedures as values, and more
forms of type application. These have not been certified by Ziran's present
conformance tests. Implement one family at a time with a typed IR shape and
clear evaluation-order rules. Extend the existing direct polymorphic call
support to field expressions in specialized record defaults and defaults on
procedure-type values.

Nested lazy expressions inside larger calls are a known gap: `ifx`/`#ifx`
lowering currently covers selected placements, but a conditional used inside
an outer call is not fully supported. Standalone `type_of`, forward and
imported file-scope `type_of`, and foreign-record `size_of` also remain open.
Put each accepted expression through source, saved IR, and all applicable
targets. Rejection for an unavailable construct must occur in checking, not
as a later C/Go compiler error.

### 5. Array syntax and true zero capacity

`T.[item, ...]` works for nonempty arrays. `T.[]` is currently rejected by
the positive-capacity rule. This needs a representation change, not a parser
exception: native C emission uses `T name[bound]`, and the portable verifier
and VM assume positive capacity. A fake one-element allocation would produce
the wrong `size_of`, `.count`, `.data`, and bounds behavior. The OpenJai
reference explicitly describes an empty typed literal with count zero and a
null data pointer. Verify those rules with Jai, choose a checked zero-capacity
representation, then update layout calculation, C/C++/Go lowering, `.zir`,
`.zib`, host boundaries, and array borrowing together.

Acceptance: `T.[]`, context-inferred `.[]`, globals, records, arguments,
returns, `size_of`, `.count`, `.data`, indexing failures, and source/saved-IR
builds agree. Also audit nonempty arrays for Jai's `.data` contract rather
than treating native C arrays as proof of layout parity. Add focused cases to
`tests/portable_arrays.sh` and native array tests.

## Priority 2: make the checked language portable and predictable

### 6. Finish compile-time execution

`#run`, `#if`, `#ifx`, and `#assert` currently evaluate a bounded pure subset.
Effects, aggregate results, and general metaprogramming are outside it.
`#caller_location` parameters in compile-time calls are also missing. The
parser contains an integer-only procedure evaluator and a typed evaluator;
both are active. Consolidate them only after tests prove equal behavior for
integer overflow, short-circuiting, defaults, imports, recursion limits, and
compile-time phase handling. Then extend the one evaluator to aggregate
constants and supported metaprogramming features with explicit effect and
resource limits. Avoid leaving two subtly different language interpreters.

Acceptance: supported compile-time programs produce the same typed constants
in source and saved IR on every target; non-constant or effectful code fails
with a source span; recursion and instruction limits are deterministic. Use
`tests/compile_run.sh`, `tests/compile_values.sh`, and
`tests/compile_time.sh`.

### 7. Complete checked IR and native backend equivalence

Function bodies use checked expression graphs, but declaration and import
lowering still contain target-specific text handling. Replace that with typed
declaration/import nodes or a checked target-neutral lowering plan. Audit name
mapping across an entire program: top-level types, exported procedures,
foreign symbols, imports, target keywords, and names differing by case or
underscores. Test C99, C++, Go, mixed source/IR input, and malformed saved
IR. Bump the experimental `.zir` version whenever its contract changes and
reject earlier versions; there is no migration path to maintain.

Unions are another concrete parity gap: C/C++ have overlapping storage, while
Go and `.zib` reject reachable unions. Select a layout-preserving Go and VM
representation before claiming those targets support union programs. Include
size, alignment, field overlap, and copy tests.

### 8. Expand `.zib` and host capabilities

The portable verifier and VM support a useful scalar/record/array subset, and
carry raw pointers as opaque host handles that can be stored, compared, and
passed across host capabilities. They still reject slices inside globals or
records, reachable unions, and several host shapes. Explicit global
initializers are not executed portably. Host calls support scalar, string,
plain-record, pointer, and selected synchronous slice arguments; array, slot,
slice-return, and record-slice shapes remain open. Implement each boundary as
a versioned checked contract in `cmd/zir/zir_bundle.c`, `cmd/zir/zir_vm.c`, and
`include/ziran_host.h`. Retain preflight verification so a bundle cannot reach
an unsupported operation at runtime. Test ownership and repeated instance
runs, not only one-shot calls.

### 9. Owned storage and standard-library usability

`Vec(T)` has checked push, indexing, clear, free, swap, `VecPop`/`VecGet`
through `Option(T)`, a `Vec(u8)` string builder, and move/drop rules for
assignment, argument passing, and return, including use-after-move, leak, and
double-drop rejection with `defer`-based cleanup, an explicit `VecClone`, and
`VecSlice` borrowed views with live-borrow rejection. Automatic drop insertion
beyond `defer` remains to finish
[Owned values](OWNED_VALUES.md). Match C/C++/Go and VM behavior on failure and
destruction; document
where Go cannot recover from physical allocation failure. This is needed for
ordinary non-UI programs to use Ziran without writing a new storage layer.

## Priority 3: broader language ambitions

Jai syntax parity does not itself deliver Ziran's planned laws, automatic CPU
parallel execution, GPU target, or complete platform integration. Keep those
as separately gated milestones in [Language direction](LANGUAGE_DIRECTION.md).
First specify observable semantics and prove a serial reference path; then add
parallel and GPU implementations with race, determinism, and backend tests.
Kryon and downstream application cutover belong to their repositories and
must not introduce UI-specific compiler branches here.

## Conformance workflow for every item

1. Save the smallest positive and negative Jai examples, their expected types,
   diagnostics, and output. Mark unverified examples as hypotheses.
2. Add Ziran source tests and forged saved-IR rejection tests for the rule.
   Include imports, `#load`, file-private visibility, and a target keyword
   collision when relevant.
3. Make the parser, checker, serialization, emitters, linker, verifier, and VM
   agree. Unsupported target behavior must have an explicit diagnostic.
4. Run the focused test, then `env -u DISPLAY -u WAYLAND_DISPLAY make -j4
   check`. Any display-dependent test runs only under a private Xvfb/Xephyr
   display. Never inherit the developer's desktop display.
5. Update `IMPLEMENTATION_STATUS.md` and the conformance matrix with the exact
   newly supported subset. Commit directly to `master`; use breaking format
   versions and remove superseded execution paths rather than preserving them.

## Legacy-code removal gate

An audit of current source paths found no executing Kryon compiler/runtime
mode, `.kry`/`.kir`/`.krb` loader, or old directive implementation. Mentions
of old spellings in `cmd/zir/zir_parse.c` are diagnostics that enforce their
rejection; the corresponding tests should remain. The two active compile-time
procedure evaluators are duplicated machinery, not proven compatibility
shims. The next removal task is to consolidate those evaluators behind one
typed implementation with behavior tests, then delete the unused one. Future
audits should distinguish rejection code from executable legacy behavior and
remove the latter in the same commit that replaces it.
