# Checked API discovery

`ziran api --json --root DIR file.zi` loads and checks the input and its
transitive imports, then prints one JSON object describing visible declarations
in each loaded module. The same command accepts saved `.zir` inputs. Use
`--module-path DIR` for import search roots and `--define NAME` for the same
conditional source selection as `ziran check`. A project
can use `ziran api --project --locked --json` to query its entry and imports.
Without `--json`, the command prints concise text signatures with source
locations. A failed load or check exits nonzero and does not print an API
object.

The JSON object has `schema_version: 1` and a `modules` array. Each module has
`name`, `source`, `types`, and `functions`. A type includes its name, checked
body text, type parameters, record-template and enum flags, and source path
and line. A function includes its name, checked parameter signature, return
type, declaration defaults, inferred effect, `must_use`, template and host-use
flags, and source path and line. Empty `defaults` and `effect` strings mean the
corresponding information is absent. Consumers should reject unknown schema
versions and ignore new fields within a known version.

Only declarations visible across module boundaries appear. File/module-private
declarations, generated specializations, and compiler-generated default
argument helpers are omitted. The output lists each loaded module separately;
it does not flatten import names into the entry module. For example:

```sh
ziran api --json --root std std/json_scan.zi
```

This first schema covers procedures and types. It does not yet describe
constants, globals, foreign imports, per-symbol target availability, ownership
preconditions, or tested examples. Use `ziran capabilities --json` for
target-wide limits, and compile a small call before relying on a target that
has not been exercised by your project.
