# Owned values and recoverable failures

This is the target contract for owned `Vec[T]` and a string builder. A scoped
`Vec(T)` subset now supports default initialization, checked indexing, fallible
`VecPush`, `VecClear`, `VecFree`, and `VecSwap` in C99, C++, Go, and the portable
VM. A vector can live in a global or record field. The checker rejects value
copies, parameters, returns, and nested vectors until move and drop rules are
implemented. Go can report capacity overflow but its runtime may terminate on
physical allocation failure; this is not yet the full recoverable failure
contract below. Code that creates a local vector must call `VecFree` before
leaving its scope. The string builder and `pop`/fallible lookup remain planned.

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
the vector. `push` consumes its input on either outcome; `pop` returns
`Option(T)`. Indexing has the same checked bounds behavior as fixed arrays,
and a fallible lookup returns `Option` instead. A string builder owns UTF-8
bytes in a `Vec[u8]`; appending can fail, and finishing consumes the builder
without copying its bytes. Immutable `string` values remain unchanged.

The parser, checker, checked `.zir`, C/C++/Go emitters, portable verifier and
VM, and host boundary must implement the same observable ownership and error
behavior before these APIs are declared supported.
