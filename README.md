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
scalars, strings, plain records, enums, fixed arrays, borrowed slices, and
declared host capabilities. The portable runner does not yet execute every
checked program.
[Implementation status](docs/IMPLEMENTATION_STATUS.md)
lists the remaining work. The [IR](docs/ZIR.md) and [bundle](docs/ZIB.md)
documents distinguish the current format from the intended contracts.

Run `make` to build the current toolchain and `make check` for its language,
backend, and portable bundle tests. `build/bin/ziran` exposes `check`, `ir`, `fmt`,
`build --target=c|cpp|go`, `bundle`, and `run`.

For ordinary imports, pass the entry file and a library directory with
`--module-path DIR` (repeat for multiple directories). `check`, `ir`,
`build`, and `bundle` load extensionless `#import "module"` dependencies
transitively from `.zi` or saved `.zir`. For example:

```sh
build/bin/ziran check --root app --module-path ../kryon/src/ui app/main.zi
```

Imports with a dotted target, such as `#import "stdio.h"`, remain host headers.
