# Ziran portable bundle (`.zib`)

This describes the target contract and the experimental subset that ships now.
See [Implementation status](IMPLEMENTATION_STATUS.md) for the remaining work.

## Experimental version 1

`ziran bundle --root DIR --entry module:function -o FILE file.zi|file.zir ...`
links explicitly supplied modules. `ziran run FILE` validates and executes
zero-argument scalar entry functions on a portable interpreter. The bundle is
`ZIB` plus a zero byte, a little-endian version, length-prefixed entry module
and function names, a host capability count (currently zero), and a
length-prefixed version 3 `.zir` payload. It includes all supplied modules.
The reader rejects unsupported versions, truncated or trailing data, malformed
embedded IR, semantic errors in serialized statement text, unresolved imports,
unsupported capabilities, and functions
outside the current scalar execution subset. Source and saved-IR bundle bytes
match in the two-module test. Version 1 is experimental and has no compatibility
promise. Host imports, graphical programs, records, strings, arrays, slots,
and control flow beyond `if`/`else` and `while` remain unsupported.

## Target contract

A `.zib` is a versioned, portable executable bundle linked from checked
`.zir` modules. Its format identifier is `ZIB`. It contains the program's
portable executable content, data, entry point, linked module identities,
exports, and explicit host capability requirements. The exact instruction and
section encoding will be specified before the format is called stable.

The linker resolves imports and includes reachable code from ordinary
libraries. That includes Kryon only when the program imports it. A non-UI
command-line program is a first-class `.zib`; it launches without a graphical
host. Kryon UI code is portable Ziran code in the bundle, with rendering and
input supplied through declared host capabilities. The same bundle format
serves graphical and non-graphical programs.

Before execution, the loader validates the format version, section bounds,
symbols, entry point, and capability contract. Missing symbols, unsupported
versions, malformed bundles, and unavailable required capabilities fail with
diagnostics. A portable bundle cannot contain an undeclared dependency on
target-specific C, C++, or Go code. Hosts may implement the declared
capability interfaces with native libraries.

The old `.krb` cartridge is a Kryon-specific format. It is not renamed or
reinterpreted as `.zib`. Existing programs must be recompiled from `.zi` into
the new pipeline during the breaking cutover.
