# Ziran portable bundle (`.zib`)

This document defines the intended format boundary. No `.zib` writer or
runtime is shipped yet; see [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

A `.zib` is a versioned, portable executable bundle produced by linking
checked `.zir` modules. Its format identifier is `ZIB`. It contains executable
code, data, linked module identities, exports, and an explicit list of host
capabilities. The loader validates the format version, section bounds,
imports, and capability requirements before executing code.

The format has no UI-specific node records or implicit Kryon dependency.
A command-line program with no Kryon import is a valid `.zib`. Importing
Kryon embeds the reachable portable Kryon modules into the same bundle;
graphics and input become requirements only when those modules use them.
The host implements declared capabilities. A missing capability is an error,
not a reason to omit an operation.

The former `.krb` UI cartridge is not a `.zib` version. Programs migrate by
recompiling `.zi` source; old cartridge bytes are not reinterpreted as Ziran
bundles.
