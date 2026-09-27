# Diagnostics and explanations

Compiler failures use a diagnostic code, source span, and message. With
`--diagnostics=json`, supported compiler paths emit one JSON object per line:

```json
{"severity":"error","code":"check.type","message":"...","path":"main.zi",
 "line":1,"column":1,"end_line":1,"end_column":1}
```

The code identity is the stable part. Messages and spans can improve; do not
parse human-readable message text when a code is present. Code names use a
phase prefix such as `parse`, `check`, `emit`, `module`, `package`, `law`,
`parallel`, `gpu`, `zib`, `zir`, `vm`, or `host`.

Query remediation with:

```sh
ziran explain check.slice_lifetime
ziran explain zir.noncanonical --json
ziran explain --list
ziran explain --list --json
```

A single explanation has schema version 1:

```json
{"schema_version":1,"stability":"stable-code","code":"...",
 "summary":"...","next_step":"..."}
```

The list output has the same schema version and stability field, with a
`codes` array containing every code currently emitted by the compiler. The
`tests/explain.sh` conformance test scans compiler diagnostic calls and fails
if a new code is missing from the registry. Unknown codes exit nonzero rather
than receiving a guessed explanation.

The current JSON diagnostic shape is intentionally small. It has only error
severity and does not yet include related spans, expected/actual types, target
capability details, or machine-readable suggested edits. Some loading and tool
failures can still be plain text. Consumers should reject unknown schema
versions and preserve diagnostic ordering when displaying multiple lines.
