# Ziran

Ziran is a general-purpose language. Its source files use `.zi`, its checked
intermediate representation uses `.zir`, and its portable linked programs use
`.zib`. None of those formats assumes a graphical application.

Kryon is being migrated into a separate UI library written in Ziran. Programs
import its checked widgets as ordinary library declarations. Ziran rejects
`#ui` and has no implicit Kryon dependency or widget-specific backend calls.
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
backend, and portable bundle tests. The compiler commands are `zi2zir`
(including `--check-only`), `zi2c`, `zi2cpp`, `zi2go`, and `zi2zib`.
`zi-fmt` formats source. `ziran check|ir|build|bundle|run|fmt` remains a
convenience dispatcher for the same tools.

For ordinary imports, pass the entry file and a library directory with
`--module-path DIR` (repeat for multiple directories). `check`, `ir`,
`build`, and `bundle` load extensionless `#import "module"` dependencies
transitively from `.zi` or saved `.zir`. For example:

```sh
build/bin/zi2zir --check-only --root app --module-path ../kryon/src/ui app/main.zi
```

Imports with a dotted target, such as `#import "stdio.h"`, remain host headers.
For a native symbol with an unmangled name, put `#program_export` on its own
line immediately before the function declaration.
Foreign procedures use a Jai-style library declaration and `#foreign`:

```jai
libc :: #system_library "libc";
Abs :: (value: s32) -> s32 #foreign libc "abs";
```

Ziran also resolves `host_api :: #system_library "host_api";` to its host
capability bridge.
Use `#scope_file`, `#scope_module`, and `#scope_export` to change the
visibility of following declarations.

## Standard library

`std/text.zi` supplies ASCII case folding, prefix matching, and substring
matching over immutable UTF-8 strings. Non-ASCII bytes compare unchanged. These
functions use only portable Ziran operations, so the same source builds for C,
C++, Go, and `.zib`. Add `--module-path std` when importing it from an app.

`std/utf8.zi` advances through UTF-8 scalar boundaries and counts codepoints.
Invalid bytes advance one byte, which keeps a scanner moving while preserving
valid multibyte sequences.

`std/option.zi` and `std/result.zi` define generic payload variants. Import a
template and create a concrete type with, for example,
`Number :: Option(s32)` or
`Outcome :: Result(s32, string)`. The concrete type has exhaustive
`match`, case constructors, and terminal `?` error propagation in declarations,
assignments, and standalone calls. See [Variants](docs/VARIANTS.md) for the
current syntax.

`std/pair.zi` defines a generic record. Import it and write
`PairNumberText :: Pair(s32, string)` to create a concrete record
with `first: s32` and `second: string` fields.

`std/json_scan.zi` supplies allocation-free JSON value skipping, object member
and array element lookup, string spans, and decimal number reading. It validates
the value shape and escapes while scanning; member names match unescaped ASCII
bytes. Callers keep the original input string and use byte offsets returned by
the scanner. Its tests compare source and saved-IR portable bundles, and the
same module is exercised through native C, C++, and Go by a downstream client.

`std/net_http.zi` defines an explicit request/response host capability. A
platform adapter supplies transport and TLS; checked Ziran code owns request
construction and response interpretation. A missing host binding is reported
before a portable bundle runs.

`std/process.zi` defines a line-oriented child-process capability with explicit
arguments, stdin, an optional credential binding, a timeout, and a desktop
isolation request. The host starts the child, yields output lines, and returns
its exit result. Its contract runs from source and saved `.zir` as a portable
bundle and through native C, C++, and Go mocks.
