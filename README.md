# Ziran

Ziran is a general-purpose language. Its source files use `.zi`, its checked
intermediate representation uses `.zir`, and its portable linked programs use
`.zib`. None of those formats assumes a graphical application.

Kryon is a separate UI library written in Ziran. Programs import its widgets
as ordinary library declarations. The Ziran compiler, IR, linker, and runtime
have no widget list, `#ui` mode, renderer policy, or implicit Kryon dependency.
See [Architecture](docs/ARCHITECTURE.md) for the intended boundary,
[Language direction](docs/LANGUAGE_DIRECTION.md) for laws, LLM tooling, and
parallelism, and [Migration](docs/MIGRATION.md) for the two-repository cutover.

The split is underway. The current compiler can check and compile a tested
subset of non-UI `.zi` to C and native Go. It cannot yet read `.zir` back or
produce or run `.zib`. [Implementation status](docs/IMPLEMENTATION_STATUS.md)
lists the remaining work. The [IR](docs/ZIR.md) and [bundle](docs/ZIB.md)
documents specify their intended contracts, not shipped formats.

Run `make` to build the current toolchain and `make check` for its standalone
smoke test. `build/bin/ziran check`, `ir`, and `build --target=c|go` are the
currently exposed commands.
