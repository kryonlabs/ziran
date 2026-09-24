# Variants

Ziran supports payload variants in the checked C, C++, Go, and
portable `.zib` subset:

```zi
Answer :: variant {
    Missing
    Number: s32
    Message: string
}

Read :: (answer: Answer) -> s32 {
    match answer {
        case Missing:
            return 0
        case Number(number):
            return number
        case Message(message):
            return message.length
    }
}
```

The declaration provides `Answer_Missing()`, `Answer_Number(value: s32)`, and
`Answer_Message(value: string)` constructors. `match` requires every case
exactly once and evaluates its subject once. A payload case may omit its
binding (`case Number:`). The generated payload accessor traps if called for
another active case. A default-initialized variant is its first case with a
default-initialized payload.

The backing tag and payload fields are private to generated operations in
Ziran source. Portable host calls reject variant values, including variants
nested inside records, until a safe host representation is specified.

Generic variants use polymorphic constructor parameters. The standard library
provides `Option` and `Result`:

```zi
#import "option"
#import "result"

Number :: Option(s32)
Outcome :: Result(s32, string)

Get :: (ok: bool) -> Outcome {
    if ok { return Outcome_Ok(42) }
    return Outcome_Err("unavailable")
}

Read :: (ok: bool) -> Outcome {
    value: s32 = Get(ok)?
    return Outcome_Ok(value)
}
```

The concrete type declarations create `Number_None()`, `Number_Some(value: s32)`,
`Outcome_Ok(value: s32)`, and `Outcome_Err(value: string)`. The `?` form works
at the end of an initializer, simple binding assignment, or standalone call
whose source is an `Ok/Err` variant. It evaluates that source once, returns `Err` early when needed, and
requires the enclosing function to return an `Ok/Err` variant with the same
error payload type. Adjacent postfix `?` also works inside eager return,
initializer, assignment, and standalone expressions, and in plain `if` and
`while` conditions. A spaced `?` remains the conditional operator. A top-level
boolean `&&` or `||` expression in a simple value statement or plain `if` or
`while` condition preserves short-circuit evaluation, including nested boolean
forms.
A top-level conditional expression also works when its destination has an
explicit type, or when it is a plain `if` or `while` condition. The checker
rejects lazy expressions embedded in an outer call
and earlier reads or calls whose evaluation order cannot be preserved.
Templates must be applied before use as values. Direct type application works
in type annotations. Remaining expression contexts and move ownership remain
future work.
