# Consistency, tooling, and performance roadmap

This records the remaining work found in the 2026-09-27 source, documentation,
and cross-target audit. It distinguishes current behavior from desired
behavior. [Implementation status](IMPLEMENTATION_STATUS.md) covers shipped
features in detail; [`ziran capabilities --json`](CAPABILITIES.md) gives a small
machine-readable summary from the installed compiler.

## What exists now

Ziran checks source and saved `.zir`, emits C/C++/Go, runs portable `.zib`,
supports compile-time evaluation, named laws, partial ownership checks,
structured diagnostics, a formatter, packages, and a tested set of standard
modules. `ziran guide` supplies a compiler-local starting point. The
[benchmark harness](../bench/README.md) measures source/saved-IR compilation,
downstream native builds, and validated runtime output for all four current
execution paths. Its [first baseline](../bench/results/2026-09-27/benchmark.md)
contains 206 samples across 50 cases on one machine. That integer kernel is a
smoke check, not a general speed ranking.

Recent differential tests aligned narrow integer shifts, narrowing casts,
and floating division between native and portable execution. Cleanup and one
emitter path now use structured diagnostics. A checker guard rejects value
transfers of aggregates containing `Vec`, preventing the reproduced record-copy
double-free case. These fixes do not complete the ownership or diagnostic
contracts.

## Priority 1: one safe meaning across targets

1. **Automatic ownership cleanup.** Native code still leaves a local `Vec`
   allocation live when a function returns without explicit cleanup, despite
   the prior documented promise. Lower drops into checked IR for every scope
   exit, including return, break, continue, and branch joins. Track whether a
   direct vector or aggregate is moved, explicitly freed, or still owned.
   Extend the checker and lowering to nested records and arrays before
   allowing aggregate-owned transfers again. Validate with allocation counts,
   AddressSanitizer, and source/saved-IR tests across backends. A moved source
   must be empty so its later drop is harmless.
2. **Borrowed views.** `TextView` of mutable bytes observes later mutation in
   generated C/C++ but retains the earlier bytes in Go and `.zib`. Choose one
   language rule, then enforce it consistently. An immutable-borrow rule needs
   alias and lifetime tracking through records, arrays, calls, returns, globals,
   and host boundaries. The checker currently accepts a record that returns a
   view of a local byte array. Reject escapes that cannot be proved safe.
3. **Numerical edge cases.** Expand differential tests beyond the corrected
   8/16-bit shifts and floating zero division: signed overflow, minimum
   integer divided by minus one, all cast widths, negative and oversized
   shifts, NaN comparisons, infinities, and compile-time evaluation. Record
   the specified trap or value for each operation. A saved-IR path must agree
   with source before the result is called portable.
4. **Target boundaries.** Give checked programs an explicit target capability
   error for unavailable FFI, pointer, union, host, or GPU behavior. Replace
   remaining textual declaration/import lowering with typed IR. A CPU fallback
   must be reported as CPU execution, never as measured GPU work.

Acceptance for this phase: the conformance corpus has the same observable
result on every supported execution path, and the memory-sensitive corpus has
no accepted safe program that unintentionally leaks, double-frees, or returns
a dangling view. The currently process-lifetime `BuilderFinish` storage needs
its own reclaimable-string or region policy.

## Priority 2: one discoverable language contract

- Maintain a versioned feature registry with syntax, target support, known
  limits, examples, and conformance-test IDs. Use it to verify the guide,
  website, reference docs, and `capabilities` output. Compile accepted examples
  and verify explicitly rejected examples. Keep roadmap goals separate from
  shipped behavior.
- Finish JSON diagnostics across loading, parsing, checking, cleanup, laws,
  lowering, verification, and execution. Some error paths still print plain
  text under `--diagnostics=json`. Define stable codes, spans, related spans,
  expected/actual types, target capability, and machine-readable suggested
  edits where reliable. Test every negative fixture as JSON lines.
- Clarify law evidence. `custom` laws evaluate closed compile-time expressions;
  `abi` laws check a supported shape, not foreign implementation correctness.
  Require unique qualified law IDs, validate waiver targets and scope, and
  report waived obligations explicitly. A quantified proof system would be a
  separate project with a trusted kernel, termination and effect rules, and
  explicit unsafe/FFI boundaries.
- Publish exact supported limits for fixed-size names and source-text buffers,
  then diagnose excess input precisely. Longer term, intern type identities and
  use structured syntax rather than relying on spelling-sized buffers.

## Priority 3: a fast agent and editor loop

The useful default cycle is query the compiler's guide and capabilities, edit
a small unit, format, check laws, run tests, and inspect structured failures.
Next add API discovery for standard and imported modules, including signatures,
defaults, effects, ownership, target availability, and tested examples. Add an
`explain` command for stable diagnostic codes, then editor diagnostics,
definition, hover, completion, and rename from compiler data. A watch mode
should reuse checked work safely; live state reload needs an explicit
state-versioning contract. The formatter should have an idempotence and
semantic-preservation corpus, especially for strings and newer syntax.

Measure agent usability rather than assuming it: pin model, prompt, context,
tool budget, and tasks; record first-attempt compilation, hidden-test success,
repair attempts, invented APIs, elapsed time, tokens, and cross-target
agreement. Include multi-file edits, ownership mistakes, and requests for an
unavailable feature. No such LLM task evaluation has yet been run.

## Priority 4: useful language growth

| Area | Current boundary | Next useful step |
|---|---|---|
| Errors | `Option` and `Result` are ordinary records; invalid payload reads are representable | Checked active-case access and exhaustive handling |
| Generics and procedures | Generic records take multiple type parameters; procedures bind one, and procedure values are capture-free | Multiple independent procedure parameters, constraints, then capturing closures if needed |
| Collections | `Vec`, text/UTF-8, JSON scanning, ZIP, and selected host contracts exist | Map/set, sort/search, more portable I/O and error utilities |
| Parallel work | C/C++ launch workers for legal forward regions, but useful loop results cannot be combined | Deterministic reduction or disjoint output ownership with omitted-iteration tests |
| GPU | Checked annotation reaches a CPU fallback | A real device backend with transfer/lifetime rules and differential tests |

Do not reintroduce rejected legacy syntax merely to grow the feature list.
Select features from tested programs in CLI, parsing, services, numerical work,
and Kryon applications. Kapsule-specific terminal behavior belongs in Kapsule,
not the general language runtime.

## Performance evidence to collect next

The current benchmark covers one integer recurrence, with process startup in
runtime timings. It separates Ziran emission from host compiler work and marks
Go build times as cache-warm. Generated C/C++ were close to handwritten
equivalents on this kernel. Generated Go took about 2.6 times handwritten Go
runtime in the initial audit; its emitted loop calls general numeric helpers
five times per iteration, a focused backend optimization candidate. This
observation is workload-specific and needs a before/after measurement.

Add validated workloads for arrays, records/calls, generic specialization,
vector growth, text/UTF-8/JSON scanning, sorting/maps when available, local
I/O, and real applications. Scale compiler inputs by module count, source size,
generic instances, compile-time evaluation, and laws. Instrument compiler
phases and allocations; do not present sums of separately sampled phases as
measured end-to-end builds. Retain raw samples, exact source/input hashes,
tool versions, flags, cache state, hardware, device, and unsupported cases.
Run a short semantic and compilation corpus on changes, and a broader runtime
matrix on a stable machine. Benchmarks for Bend or any other implementation
belong in the same validated workload and hardware setup; unavailable tools
are recorded as unavailable, not assigned estimated results. No finite suite
can rank every language for every workload.
