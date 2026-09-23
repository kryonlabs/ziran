# Ziran

Ziran is a general-purpose language. Its source files use `.zi`, its checked
intermediate representation uses `.zir`, and its portable linked programs use
`.zib`. None of those formats assumes a graphical application.

Kryon is being migrated into a separate UI library written in Ziran. In the
intended design, programs import its widgets as ordinary library declarations.
Ziran rejects `#ui` and has no implicit Kryon dependency, but inherited
Kryon-specific backend paths still need removal.
See [Architecture](docs/ARCHITECTURE.md) for the intended boundary,
[Language direction](docs/LANGUAGE_DIRECTION.md) for laws, LLM tooling, and
parallelism, and [Migration](docs/MIGRATION.md) for the two-repository cutover.

The split is underway. The current compiler can check and compile a tested
subset of non-UI `.zi` to C, C++, and native Go. It can also save that subset as an
experimental versioned `.zir` and build each native target from saved modules. It
can build and run experimental `.zib` bundles for a non-graphical subset with
scalars and plain records.
[Implementation status](docs/IMPLEMENTATION_STATUS.md)
lists the remaining work. The [IR](docs/ZIR.md) and [bundle](docs/ZIB.md)
documents distinguish the current format from the intended contracts.

Run `make` to build the current toolchain and `make check` for its standalone
smoke test. `build/bin/ziran` exposes `check`, `ir`, `fmt`,
`build --target=c|cpp|go`, `bundle`, and `run`.
