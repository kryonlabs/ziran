# Owned values and recoverable failures

This is the target contract for owned `Vec[T]` and a string builder. A scoped
`Vec(T)` subset now supports default initialization (including locals in the
portable VM), checked indexing, fallible `VecPush`, `VecClear`, `VecFree`,
`VecSwap`, `VecPop` and `VecGet` results carried as `Option(T)` records, and a
`Vec(u8)` string builder through `BuilderAppend` and `BuilderFinish`, in C99,
C++, Go, and the portable VM. A vector can live in a local, global, or record
field. The checker rejects value
copies, parameters, returns, and nested vectors until move and drop rules are
implemented. Go can report capacity overflow but its runtime may terminate on
physical allocation failure; this is not yet the full recoverable failure
contract below. Code that creates a local vector must call `VecFree` before
leaving its scope. `VecPop` and `VecGet` require the `Option` record template
from `std/option.zi` to be visible in the using module.

`BuilderFinish` consumes the builder: the returned `string` keeps the builder's
bytes without copying them. C99 and C++ detach the buffer instead of freeing
it, so a finished string stays valid for the remaining process lifetime; Go
must copy into its immutable string representation, and the portable VM copies
into its interned string storage. Appending an empty string always succeeds
without allocating.

Generic
`Option` and `Result` are currently ordinary records with explicit status
fields, applied as `Name :: Option(T)` or `Name :: Result(T, E)`. They copy
their fields. Fixed arrays and borrowed slices remain value and view types,
respectively.

## Values and ownership

Scalars, immutable strings, and aggregates whose members are copyable retain
value-copy semantics. An owned value, including `Vec[T]`, moves on assignment,
argument passing, and return. The checker rejects use after move and rejects a
move while a live view borrows the value. An explicit `clone` may allocate and
therefore returns a recoverable result. Every path drops each owned value once;
native output and the portable VM must agree on this rule.

## Recoverable errors

`Result(T, E)` stores `is_ok`, `value`, and `error`; a caller checks `is_ok`
and handles the error explicitly. `Option(T)` stores `has_value` and `value`.
Neither record has a hidden active case, and the language does not provide
postfix error propagation. An owned error type will need defined move and drop
behavior across all backends before it can be used in a portable `Result`.

## Growable storage

`Vec[T]` owns its allocation and reports length and capacity separately.
Growth checks size overflow and returns an allocation error without changing
the vector. `VecPush` consumes its input on either outcome; `VecPop` removes
the last element and returns `Option(T)`, leaving the vector unchanged when it
is empty. Indexing has the same checked bounds behavior as fixed arrays, and
`VecGet` returns `Option(T)` instead of trapping on an out-of-range index. A
string builder owns UTF-8 bytes in a `Vec[u8]`; `BuilderAppend` can fail, and
`BuilderFinish` consumes the builder without copying its bytes. Immutable
`string` values remain unchanged.

The parser, checker, checked `.zir`, C/C++/Go emitters, portable verifier and
VM, and host boundary implement these observable ownership and error
behaviors for the supported subset; move and automatic drop rules remain the
open part of this contract.
