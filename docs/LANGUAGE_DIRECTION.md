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

## Evolution

The language can grow without embedding one library's concepts in its core.
Versioned `.zir`, `.zib`, library interfaces, and law results make changes
explicit. Incompatible changes require clear diagnostics and a deliberate
migration path. Reproducible compiler output and conformance suites let new
backends and LLM-authored code be checked against the same semantics.
