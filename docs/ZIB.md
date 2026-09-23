# Ziran portable bundle (`.zib`)

This describes the target contract and the experimental subset that ships now.
See [Implementation status](IMPLEMENTATION_STATUS.md) for the remaining work.

## Experimental version 1

`ziran bundle --root DIR --entry module:function -o FILE file.zi|file.zir ...`
links explicitly supplied modules. `ziran run FILE` validates and executes
zero-argument integer, bool, or void entry functions on a portable interpreter.
Called functions can use `i32`, `u32`, `bool`, `float`, and `double` scalars,
enums, and plain records with scalar, enum, or nested record fields. Record defaults and literals,
member reads and writes, scalar compound assignments, parameters, returns,
and value copies execute in the interpreter. The `u32` subset includes
bitwise `&`, `|`, `^`, `~`, shifts, and their compound assignments. Portable
enum initializers support integer literals and references to preceding members joined by `+`
or `-`; other constant expressions remain outside this subset. The bundle is
`ZIB` plus a zero byte, a little-endian version, length-prefixed entry module
and function names, a host capability count (currently zero), and a
length-prefixed version 3 `.zir` payload. The current linker follows direct
function calls from the entry, keeps record and enum declarations used by
those functions (including types from imported modules and nested record
fields), and removes unreachable functions, modules, types, and imports before
writing the payload. Enum member references keep their declaration even when
the expression uses the member as an integer without an explicit enum cast.
The reader rejects unsupported versions, truncated or trailing data, malformed
embedded IR, semantic errors and divergent fields in saved IR, unresolved imports,
unsupported capabilities, and functions outside the current portable subset.
Source and saved-IR bundle bytes match in scalar, record, and enum tests,
including Kryon's complete geometry, layout, and group calculation test. Version 1 is
experimental and has no compatibility promise. Host imports, graphical
programs, strings, arrays, slots, state, globals,
`for`, and `switch` remain unsupported.

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
