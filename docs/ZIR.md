# Ziran intermediate representation (`.zir`)

This is the target contract. An experimental binary version 10 now exists for
the tested C/C++/Go and portable scalar/record/enum subsets. It is not yet the complete contract below; see
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

## Experimental version 10

The current writer emits `ZIR` followed by a zero byte, a little-endian
version number, and length-prefixed checked module records. Strings and
integers are encoded explicitly; process pointers and C struct padding are
not serialized. The reader rejects wrong versions, truncated records,
oversized fields, invalid references, and trailing bytes. The C, C++, and native Go
commands can build saved `.zir` modules, including an imported two-module
block-call example. The experimental `.zib` bundler also consumes saved `.zir`
for its restricted scalar, record, and enum subset. Native and bundle builds rerun
the language checker on the saved expression graph. The reader rejects invalid
references and cycles, and the checked serialization must match the saved bytes.
Every build containing saved `.zir` uses strict checking, even when the native
CLI was invoked without `--strict`; unchecked source-only lowering cannot
execute from a saved module.
For functions supported by the shared typed emitter and portable VM, statement
text is source metadata; the expression graph and explicit branch flags determine
behavior. Exhaustive enum `match` is lowered to typed branches and an
`unreachable` guard before writing IR, so saved modules contain no `match`
statement to reinterpret. The same lowering applies to monomorphic payload
variants. Version 10 retains declared cases and checked storage layout, but
serializes generated operation slots without bodies. The reader rebuilds those
bodies from the variant declaration and rejects forged operation slots. Generic
variant templates retain their parameter and case declarations; generic record
templates retain their parameter and field declarations. Explicit
specializations are stored as ordinary concrete types. Terminal and eager
expression postfix `?` lower to typed error propagation before IR is saved.
Type and constant records preserve scope visibility for imported-name checking.
Legacy native lowering still uses statement text outside that subset. Version 10 is
experimental and has no compatibility promise.
