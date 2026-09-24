# Owned values and recoverable failures

This is the contract for owned `Vec[T]` and a string builder. Generic
`Option` and `Result` variant templates can now be applied as named concrete
types with `Name :: Option(T)` or `Name :: Result(T, E)`, and those types have
exhaustive `match`.
They currently copy their payloads. Fixed arrays and borrowed slices remain
value and view types, respectively.

## Values and ownership

Scalars, immutable strings, and aggregates whose members are copyable retain
value-copy semantics. An owned value, including `Vec[T]`, moves on assignment,
argument passing, and return. The checker rejects use after move and rejects a
move while a live view borrows the value. An explicit `clone` may allocate and
therefore returns a recoverable result. Every path drops each owned value once;
native output and the portable VM must agree on this rule.

## Variants and errors

A variant has one active case and exposes no writable tag or inactive payload.
Construction chooses a case; `match` checks every case at compile time. Today,
payload binding copies a value. Future owned variants will move or borrow their
payload according to the binding. `Option` and `Result` are ordinary
generic variant declarations in the standard library. A terminal `?` on an
initialized declaration or simple binding assignment extracts `Ok(T)` or returns `Err(E)`
from a function whose result has the same error payload type. No implicit error
conversion occurs. A standalone `Result` call followed by `?` checks the error
and discards its success payload. Adjacent postfix `?` works in eager
expressions and plain `if` and `while` conditions. Top-level boolean `&&` and
`||` expressions in simple statements preserve short-circuit evaluation;
typed conditional arms also preserve their selected evaluation path. Lazy
expressions embedded in outer calls still need control-flow lowering.

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
