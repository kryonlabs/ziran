# Compiler capability output

`ziran capabilities --json` prints one JSON object describing the currently
supported output targets and shared limits. Use `--target=c`, `cpp`, `go`, or
`zib` to select one target. The command works without a project or network
access, so an editor or coding agent can query the installed compiler before
generating code. `schema_version` starts at 1; consumers should reject schema
versions they do not understand and ignore unknown fields within a version.

`source_and_saved_ir` reports that the target accepts checked source and saved
`.zir` inputs. `parallel_execution` is `threads` for C/C++ forward CPU regions
and `serial` for Go and the portable VM. It does not promise a parallel result
reduction. `gpu_execution` is `cpu_fallback` for every target: there is no
device backend. `text_view_mutable_bytes` reports the current observable
divergence when backing bytes are mutated after making a view: native C/C++
borrow those bytes, while Go and `.zib` copy them. Checking rejects direct
mutation of local backing storage while a direct or record-held view is live,
but pointer-derived views, globals, calls, and unmodeled aliases are not yet
covered. Portable code should avoid those mutations until the rule is complete.

`automatic_vec_drop` is `true` for every target: direct owned `Vec` locals
and parameters are released at scope exit or return. This does not cover
vectors stored in aggregates or global storage.
`text_view_local_mutation_check` is `true` for every target: checking rejects
direct mutation of local backing storage while a direct or record-held
`TextView` is live. Pointer-derived views, globals, calls, and unmodeled
aliases are outside this field.
`aggregate_vec_transfer` remains `false`: the checker rejects transfers of
aggregates containing a `Vec`. `diagnostics_json` is `partial` because some compiler paths
still print plain errors even when JSON is requested. These are current
boundaries, not feature requests or guarantees about unlisted behavior. See
[implementation status](IMPLEMENTATION_STATUS.md) and the
[owned-value contract](OWNED_VALUES.md) for detail.
