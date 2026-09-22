# Ziran intermediate representation (`.zir`)

This is the target contract. Today's `ziran ir` output is a diagnostic text
dump and is **not** this format; see [Implementation status](IMPLEMENTATION_STATUS.md).

`.zir` is a versioned, checked, serializable module representation. It is the
shared input to native backends and the portable linker. It records module and
symbol identities, imports/exports, resolved types, constants, function
bodies, control flow, source locations, declared FFI, and host capability
requirements. Backends may attach target-specific output metadata outside the
language model, but they must not reinterpret source text to recover program
meaning.

The Ziran compiler provides a canonical writer and a validating reader. The
reader rejects unknown incompatible versions, truncated or malformed data,
invalid symbol references, and invalid types with useful diagnostics. The
writer produces deterministic bytes for equivalent checked modules. A backend
given saved `.zir` has the same language semantics as one given the `.zi`
source that produced it. Diagnostics retain source paths and spans where
available.

The IR represents general calls, records, slots, imports, and capabilities.
It has no widget kind, `#ui` flag, DOM attribute cache, KSS node, or implicit
Kryon module. A Kryon widget call has the same representation as a call to an
unrelated imported function with the same type shape.

`.zir` is the compiler interchange and cache format. Portable distribution
uses a linked `.zib`, not an unlinked `.zir`. See [Bundle format](ZIB.md).
