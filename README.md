# Ziran

Ziran is a general-purpose language. Its source files use `.zi`, its checked
intermediate representation uses `.zir`, and its portable linked programs use
`.zib`. None of those formats assumes a graphical application.

Kryon is being migrated into a separate UI library written in Ziran. Programs
import its checked widgets as ordinary library declarations. Ziran rejects
`#ui` and has no implicit Kryon dependency or widget-specific backend calls.
See [Architecture](docs/ARCHITECTURE.md) for the intended boundary,
[Language direction](docs/LANGUAGE_DIRECTION.md) for laws, LLM tooling, and
parallelism, the [Jai parity roadmap](docs/JAI_PARITY_ROADMAP.md) for the
remaining language work, and [Migration](docs/MIGRATION.md) for the
two-repository cutover.

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
backend, and portable bundle tests. `make check` runs independent test scripts
with four workers by default; set `CHECK_JOBS=1` to run them serially or choose
another positive worker count. The compiler commands are `zi2zir` (including
`--check-only`), `zi2c`, `zi2cpp`, `zi2go`, and `zi2zib`.
`zi-fmt` formats source. `ziran check|ir|inspect|build|bundle|run|fmt` remains a
convenience dispatcher for the same tools.
Use `ziran build --target=c` for C99 output.
Pass `--entry module:function` to a C99 build to retain functions, types,
globals, and constants reachable from that entry. A native host implementation
of a called foreign function is retained when it is among the input modules.
Type-only linked modules emit headers without empty C files. Without `--entry`,
all checked declarations and module C files are emitted as before. Runtime
branches in reachable functions remain; this pass does not specialize them.
`ziran ir --entry module:function` saves that reachable checked graph as
per-module `.zir` files before native code generation.
Saved `.zir` files are binary. `ziran inspect path/to/module.zir` shows their
declarations, statements, and checked expression links as readable text;
`ziran inspect --hex path/to/module.zir` shows bytes and offsets. Both views
read the saved file and leave it unchanged.

For ordinary imports, pass the entry file and a library directory with
`--module-path DIR` (repeat for multiple directories). `check`, `ir`,
`build`, and `bundle` load extensionless `#import "module"` dependencies
transitively from `.zi` or saved `.zir`. For example:

```sh
build/bin/zi2zir --check-only --root app --module-path ../kryon/src/ui app/main.zi
```

`#import, file "../lib/helper.zi";` loads an explicit source file relative to
the importing file. Saved `.zir` builds resolve its module by name from the
saved IR directory or a module path.
`Helper :: #import "helper";` and
`Helper :: #import, file "../lib/helper.zi";` expose public declarations as
`Helper.Name` without adding them to the unqualified scope.
`Helper :: #import, dir "../lib/helper";` loads that directory's `module.zi`.
An ordinary `#import "helper"` also discovers `helper/module.zi` on a module
search path.
`#load "relative/file.zi";` adds another file to the current module. Loads
can nest, and imports inside loaded files resolve relative to those files.
`#import, string "Value :: () -> s32 { return 42 }";` compiles source text
as a module. It also accepts a raw `#string` body, and
`Generated :: #import, string "...";` exposes its declarations as
`Generated.Name`.

Ordinary `#import` accepts a module identifier. C headers are no longer
imported as source modules; foreign procedures use `#system_library` and
`#foreign` declarations.
For a native symbol with an unmangled name, put `#program_export` on its own
line immediately before the function declaration.
Foreign procedures use a Jai-style library declaration and `#foreign`:

```jai
libc :: #system_library "libc";
Abs :: (value: s32) -> s32 #foreign libc "abs";
```

Ziran also resolves `host_api :: #system_library "host_api";` to its host
capability bridge. `ziran bundle --bind caller:capability=provider:function`
can satisfy a portable host capability with an exported Ziran function in the
bundle; the provider must have the same signature.
Use `#scope_file`, `#scope_module`, and `#scope_export` to change the
visibility of following declarations.

Raw multiline text uses Jai's `#string` delimiter form. The body keeps its
whitespace and the newline before the closing delimiter:

```jai
Greeting :: #string END
Hello, "world"!
END;
```

## Standard library

`std/text.zi` supplies ASCII case folding, prefix matching, and substring
matching over immutable UTF-8 strings. Non-ASCII bytes compare unchanged. These
functions use only portable Ziran operations, so the same source builds for C,
C++, Go, and `.zib`. Add `--module-path std` when importing it from an app.
`std/string_range.zi` provides `Substring(source, start, length)` over borrowed
bytes, using Ziran's checked `source[low:high]` string range syntax. Choose
UTF-8 codepoint boundaries when the result must remain valid text.

`std/utf8.zi` advances through UTF-8 scalar boundaries and counts codepoints.
Invalid bytes advance one byte, which keeps a scanner moving while preserving
valid multibyte sequences.

`std/option.zi` and `std/result.zi` define generic records. Import a
template and create a concrete type with `Number :: Option(s32)` or
`Outcome :: Result(s32, string)`. An `Option` has `has_value` and `value`
fields; a `Result` has `is_ok`, `value`, and `error` fields. Check the status
field explicitly before reading the associated value.

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

Native Linux C builds can import `std/file_linux.zi` for positional byte I/O,
`std/binary_linux.zi` for little-endian numbers, `std/date_time_linux.zi` for
Unix time, and `std/byte_text_linux.zi` for borrowed byte-to-text views.
`std/process_capture_linux.zi` captures a child process without a shell;
`std/net_http_linux.zi` uses it to send JSON over HTTPS through `curl`.
These native adapters keep libc calls out of applications and require glibc
Linux and a `curl` executable. Portable bundles should use the host capabilities
above instead.
