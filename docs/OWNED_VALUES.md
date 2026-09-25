# Owned values and recoverable failures

This is the target contract for owned `Vec[T]` and a string builder. A scoped
`Vec(T)` subset now supports default initialization (including locals in the
portable VM), checked indexing, fallible `VecPush`, `VecClear`, `VecFree`,
`VecSwap`, `VecPop` and `VecGet` results carried as `Option(T)` records, a
`Vec(u8)` string builder through `BuilderAppend` and `BuilderFinish`, an
explicit `VecClone(dest, src)` with a recoverable failure result, and
`VecSlice(values, low, high)` borrowed views, in C99,
C++, Go, and the portable VM. A vector can live in a local, global, or record
field, and moves on assignment, argument passing, and return. The checker
rejects use after a move, assignment over an owned vector, a leaked local or
parameter at any return or scope exit, moving nested record storage, and
moving a global. Go can report capacity overflow but its runtime may terminate
on physical allocation failure; this is not yet the full recoverable failure
contract below. A local vector must be freed or moved on every exit path;
`defer { VecFree(v) }` covers them all because deferred cleanup runs before
every rewritten return. `VecPop` and `VecGet` require the `Option` record
template from `std/option.zi` to be visible in the using module.

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
argument passing, and return. A move source must be a local binding or a fresh
call result: the checker rejects moving nested record storage and moving a
global, because both keep shared storage that other code can reach. The
checker rejects use after move, assignment over an owned vector, and a leaked
local or parameter at any return or scope exit; moves inside an `if` whose
every arm returns do not reach the join. Deferred cleanup runs before each
rewritten return, so `defer { VecFree(v) }` satisfies the drop rule on every
path, and an explicit drop plus a deferred drop of the same binding is
rejected as a double drop. A view declared as `[]T` from
`VecSlice(values, low, high)` borrows its source binding until the view's
scope closes: moving or mutating the source, including `VecPush`, `VecPop`,
`VecClear`, `VecFree`, `VecSwap`, and the string builder operations, is
rejected while the view is live; `VecGet` and `VecClone` sources stay
readable. `VecClone(dest, src)` requires a fresh or moved-from destination
and returns `false` on allocation failure without changing the source.
Native output and the portable VM agree on the observable value of
every moved binding: the source is unreachable after the move.

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

The parser, checker, checked `.zir`, C/C++/Go emitters, portable verifier,
VM, and host boundary implement these observable ownership, move, drop,
clone, and borrow behaviors for the supported subset. Automatic drop
insertion beyond `defer` remains the open part of this contract.
