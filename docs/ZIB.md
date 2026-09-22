# Ziran portable bundle (`.zib`)

This is the target contract. No `.zib` writer, loader, or runtime ships yet;
see [Implementation status](IMPLEMENTATION_STATUS.md).

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
