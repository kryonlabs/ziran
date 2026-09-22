# Ziran intermediate representation (`.zir`)

This is the target contract. An experimental binary version 3 now exists for
the tested C/C++/Go and portable scalar subsets. It is not yet the complete contract below; see
[Implementation status](IMPLEMENTATION_STATUS.md).

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

## Experimental version 3

The current writer emits `ZIR` followed by a zero byte, a little-endian
version number, and length-prefixed checked module records. Strings and
integers are encoded explicitly; process pointers and C struct padding are
not serialized. The reader rejects wrong versions, truncated records,
oversized fields, invalid references, and trailing bytes. The C, C++, and native Go
commands can build saved `.zir` modules, including an imported two-module
block-call example. The experimental `.zib` bundler also consumes saved `.zir`
for its restricted scalar subset. Native and bundle builds rerun the language
checker on saved IR, rejecting invalid expressions in a structural-validity
test. The resulting checked serialization must match the saved bytes, which
rejects divergent structured fields. The checker still reconstructs expressions
from statement text, so backends do not yet consume those structured expressions
as authoritative input. Version 3 is experimental and has no compatibility
promise.
