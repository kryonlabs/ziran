# Ziran

Ziran is a general-purpose programming language. Source files use `.zi`,
checked intermediate representation uses `.zir`, and portable linked programs
use `.zib`. None of these formats requires a UI. Kryon is a separate UI
library written in Ziran and imported like any other library.

The intended architecture is specified in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
[docs/IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) records what the
current toolchain actually implements. The split is underway; the current
compiler is not yet a complete implementation of the architecture.

For the current standalone compiler subset, run `make` and `make check`.
The smoke test compiles non-UI `.zi` modules to C and native Go and runs them.
