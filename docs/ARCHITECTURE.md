# Ziran architecture

This document defines the intended ownership and data flow. The current
implementation differs where listed in [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

## Ownership

Ziran owns the source language, parser, checker, language laws, standard
library, intermediate representation, linker, portable bundle format, and
execution runtime. Its core contains no widget names, DOM properties, KSS
syntax, rendering policy, or UI-specific IR nodes. An identifier such as
`Button` has no meaning to Ziran unless a program declares or imports it.

Kryon owns widgets, layout, styling, interaction, accessibility, renderers,
and its host capability implementations. Kryon's runtime source is `.zi`.
Applications import Kryon modules through ordinary Ziran imports. UI APIs
such as `Text(TextProps)` and `Image(ImageProps)` are Kryon declarations,
not compiler intrinsics. An application that does not import Kryon has no
Kryon code or graphics capability requirement.

## Pipeline

```text
.zi source -> parse and check -> .zir
.zir modules + imported libraries -> link -> .zib
.zir -> C, C++, or native Go code when a native target is requested
.zib -> Ziran runtime + declared host capabilities
```

The compiler uses the same checked IR for every backend. `.zi` input is a
convenience path through the parser to that IR; a backend never reparses
source after accepting `.zir`. The linker embeds reachable portable library
modules in a `.zib`, including Kryon when imported. It reports unresolved
symbols and unavailable capabilities rather than silently dropping behavior.

The portable runtime starts a non-UI `.zib` without graphics services. UI
programs request graphics and input through explicit capabilities supplied by
the selected host. Native C, C++, and Go backends can bind external libraries
through declared FFI interfaces. A portable `.zib` cannot execute arbitrary
target-specific C or Go fragments.

## Language boundary

The language supports ordinary functions, records, modules, typed block calls,
and typed slots. These constructs are useful beyond UI code. A block call is
resolved from the imported function signature and its record and slot types;
the compiler has no list of special widget names and no `#ui` mode. Kryon may
provide a `Button` function and a `ButtonProps` record, while another library
may use the same constructs for an unrelated purpose.

Compiler laws cover language semantics, type safety, evaluation order, IR
validity, and backend parity. Kryon owns laws about widgets and rendering.
