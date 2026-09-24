#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$repo/std/option.zi" "$work/option.zi"
cp "$repo/std/result.zi" "$work/result.zi"
cp "$repo/std/pair.zi" "$work/pair.zi"

cat > "$work/app.zi" <<'ZI'
#import "option"
#import "result"
#import "pair"

Number :: Option(s32)
Outcome :: Result(s32, string)
Nested :: Result(Number, string)
Flag :: Result(bool, string)
PairNumberText :: Pair(s32, string);
NestedPair :: Pair(Pair(s32, string), string)
Envelope :: struct($T: Type) { item: Pair(T, string); }
IntEnvelope :: Envelope(s32)
Payload :: variant($T: Type) {
    Some: Pair(T, string)
    None
}
IntPayload :: Payload(s32)

ReadNumber :: (value: Number) -> s32 {
    match value {
        case None:
            return 0
        case Some(number):
            return number
    }
}

ReadOutcome :: (value: Outcome) -> s32 {
    match value {
        case Ok(number):
            return number
        case Err(message):
            return message.length - 3
    }
}

ReadNested :: (value: Nested) -> s32 {
    match value {
        case Ok(number):
            return ReadNumber(number)
        case Err:
            return 0
    }
}

ReadPair :: (value: PairNumberText) -> s32 {
    return value.first + value.second.length
}

DirectPair :: () -> Pair(s32, string) {
    value: Pair(s32,string) = Pair( s32 , string ).{first = 40, second = "hi"}
    return value
}

ReadDirectPair :: (value: Pair(s32, string)) -> s32 {
    return value.first + value.second.length
}

ReadPayload :: (value: IntPayload) -> s32 {
    match value {
        case Some(pair):
            return pair.first + pair.second.length
        case None:
            return 0
    }
}

Get :: (ok: bool) -> Outcome {
    if ok {
        return Outcome_Ok(20)
    }
    return Outcome_Err("bad")
}

Propagate :: (ok: bool) -> Outcome {
    number: s32 = Get(ok)?
    return Outcome_Ok(number + 22)
}

Assign :: (ok: bool) -> Outcome {
    number: s32 = 0
    number = Get(ok)?
    return Outcome_Ok(number - 20)
}

Fail :: (ok: bool) -> Outcome {
    if ok {
        return Outcome_Ok(3)
    }
    return Outcome_Err("failure")
}

Execute :: (ok: bool) -> Outcome {
    Fail(ok)?
    return Outcome_Ok(7)
}

Compose :: (ok: bool) -> Outcome {
    return Outcome_Ok(Get(ok)? + Get(ok)? + 2)
}

GetFlag :: (ok: bool) -> Flag {
    if ok { return Flag_Ok(true) }
    return Flag_Err("failure")
}

Conditional :: (ok: bool) -> Outcome {
    if GetFlag(ok)? {
        return Outcome_Ok(5)
    }
    return Outcome_Ok(0)
}

Loop :: (ok: bool) -> Outcome {
    count: s32 = 0
    while Get(ok)? > 0 {
        count += 1
        if count == 1 { continue }
        if count == 2 { break }
    }
    return Outcome_Ok(count)
}

LazyAnd :: (left: bool, right_ok: bool) -> Outcome {
    if left && GetFlag(right_ok)? {
        return Outcome_Ok(1)
    }
    return Outcome_Ok(0)
}

LazyOr :: (left: bool, right_ok: bool) -> Outcome {
    if left || GetFlag(right_ok)? {
        return Outcome_Ok(2)
    }
    return Outcome_Ok(0)
}

Select :: (condition: bool, ok: bool) -> Outcome {
    value: s32 = ifx condition then Get(ok)? else 0
    return Outcome_Ok(value)
}

NestedLazy :: (first: bool, second: bool, ok: bool) -> Outcome {
    if first && (second || GetFlag(ok)?) {
        return Outcome_Ok(3)
    }
    return Outcome_Ok(0)
}

LazyLoop :: (left: bool, ok: bool) -> Outcome {
    count: s32 = 0
    while left && GetFlag(ok)? {
        count += 1
        if count == 1 { continue }
        if count == 2 { break }
    }
    return Outcome_Ok(count)
}

ConditionalLoop :: (condition: bool, ok: bool) -> Outcome {
    count: s32 = 0
    while ifx condition then GetFlag(ok)? else false {
        count += 1
        if count == 2 { break }
    }
    return Outcome_Ok(count)
}

#program_export
Answer :: () -> s32 {
    return ReadNumber(Number_Some(0)) +
           ReadOutcome(Propagate(true)) +
           ReadOutcome(Propagate(false)) +
           ReadOutcome(Assign(true)) +
           ReadOutcome(Assign(false)) +
           ReadOutcome(Execute(true)) +
           ReadOutcome(Execute(false)) - 11 +
           ReadOutcome(Compose(true)) +
           ReadOutcome(Compose(false)) - 42 +
           ReadOutcome(Conditional(true)) +
           ReadOutcome(Conditional(false)) - 9 +
           ReadOutcome(Loop(true)) +
           ReadOutcome(Loop(false)) - 2 +
           ReadOutcome(LazyAnd(false, false)) +
           ReadOutcome(LazyAnd(true, false)) +
           ReadOutcome(LazyAnd(true, true)) +
           ReadOutcome(LazyOr(true, false)) +
           ReadOutcome(LazyOr(false, false)) +
           ReadOutcome(LazyOr(false, true)) - 13 +
           ReadOutcome(Select(false, false)) +
           ReadOutcome(Select(true, false)) +
           ReadOutcome(Select(true, true)) - 20 +
           ReadOutcome(NestedLazy(false, false, false)) +
           ReadOutcome(NestedLazy(true, true, false)) +
           ReadOutcome(NestedLazy(true, false, false)) +
           ReadOutcome(NestedLazy(true, false, true)) - 10 +
           ReadOutcome(LazyLoop(false, false)) +
           ReadOutcome(LazyLoop(true, false)) +
           ReadOutcome(LazyLoop(true, true)) - 6 +
           ReadOutcome(ConditionalLoop(false, false)) +
           ReadOutcome(ConditionalLoop(true, false)) +
           ReadOutcome(ConditionalLoop(true, true)) - 6 +
           ReadPair(PairNumberText.{first = 40, second = "hi"}) - 42 +
           ReadDirectPair(DirectPair()) - 42 +
           ReadPayload(IntPayload_Some(Pair(s32,string).{first = 40, second = "hi"})) - 42 +
           cast(s32)#char "A" - 65 +
           ReadNested(Nested_Ok(Number_None()))
}
ZI

"$ziran" check --root "$work" "$work/app.zi"
"$ziran" ir --root "$work" \
    -o "$work/ir" "$work/app.zi"
for input in source saved; do
    if test "$input" = source; then
        modules="$work/app.zi $work/option.zi $work/result.zi $work/pair.zi"
        root=$work
    else
        modules="$work/ir/app.zir $work/ir/option.zir $work/ir/result.zir $work/ir/pair.zir"
        root=$work/ir
    fi
    # shellcheck disable=SC2086
    "$ziran" bundle --root "$root" --entry app:Answer \
        -o "$work/$input.zib" $modules
    test "$("$ziran" run "$work/$input.zib")" = 42
    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            # shellcheck disable=SC2086
            "$ziran" build --target=go --strict --pkg main --root "$root" \
                -o "$output" $modules
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("wrong generic result") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            # shellcheck disable=SC2086
            "$ziran" build --target=c --strict --root "$root" \
                -o "$output" $modules
            cat > "$output/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -I"$repo/include" -I"$output" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        else
            # shellcheck disable=SC2086
            "$ziran" build --target=cpp --strict --root "$root" \
                -o "$output" $modules
            cat > "$output/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -I"$repo/include" -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"

mkdir -p "$work/external"
cp "$work/app.zi" "$work/external/app.zi"
"$ziran" bundle --root "$work/external" --module-path "$repo/std" \
    --entry app:Answer -o "$work/external.zib" "$work/external/app.zi"
test "$("$ziran" run "$work/external.zib")" = 42
"$ziran" build --target=c --strict --root "$work/external" \
    --module-path "$repo/std" -o "$work/external/c" \
    "$work/external/app.zi"
cat > "$work/external/c/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
"${CC:-cc}" -I"$repo/include" -I"$work/external/c" \
    "$work/external/c"/*.c -o "$work/external/c/app"
"$work/external/c/app"

cat > "$work/wrong.zi" <<'ZI'
#import "result"
Bad :: Result(s32)
ZI
if "$ziran" check --root "$work" \
    "$work/wrong.zi" 2> "$work/wrong.err"; then
    echo 'wrong generic arity was accepted' >&2
    exit 1
fi
grep -Fq 'invalid generic type specialization: Bad' "$work/wrong.err"

cat > "$work/wrong_direct.zi" <<'ZI'
#import "pair"
Use :: (value: Pair(s32)) -> s32 { return 0 }
ZI
if "$ziran" check --root "$work" \
    "$work/wrong_direct.zi" 2> "$work/wrong_direct.err"; then
    echo 'wrong direct generic arity was accepted' >&2
    exit 1
fi
grep -Fq 'invalid generic type specialization:' "$work/wrong_direct.err"

cat > "$work/bare.zi" <<'ZI'
#import "option"
Use :: (value: Option) -> s32 {
    return 0
}
ZI
if "$ziran" check --root "$work" \
    "$work/bare.zi" 2> "$work/bare.err"; then
    echo 'unspecialized generic variant was accepted' >&2
    exit 1
fi
grep -Fq 'generic types require a concrete specialization: Option' \
    "$work/bare.err"

cat > "$work/bare_record.zi" <<'ZI'
#import "pair"
Use :: (value: Pair) -> s32 {
    return 0
}
ZI
if "$ziran" check --root "$work" \
    "$work/bare_record.zi" 2> "$work/bare_record.err"; then
    echo 'unspecialized generic record was accepted' >&2
    exit 1
fi
grep -Fq 'generic types require a concrete specialization: Pair' \
    "$work/bare_record.err"

cat > "$work/wrong_record.zi" <<'ZI'
#import "pair"
Bad :: Pair(s32)
ZI
if "$ziran" check --root "$work" \
    "$work/wrong_record.zi" 2> "$work/wrong_record.err"; then
    echo 'wrong generic record arity was accepted' >&2
    exit 1
fi
grep -Fq 'invalid generic type specialization: Bad' \
    "$work/wrong_record.err"

cat > "$work/old_generic.zi" <<'ZI'
Old[T] :: struct {
    value: T
}
ZI
if "$ziran" check --root "$work" "$work/old_generic.zi" \
    2> "$work/old_generic.err"; then
    echo 'bracketed generic declaration was accepted' >&2
    exit 1
fi
grep -Fq 'type declarations require a plain name before ::' \
    "$work/old_generic.err"

cat > "$work/old_specialize.zi" <<'ZI'
#import "option"
Old :: specialize Option[s32]
ZI
if "$ziran" check --root "$work" "$work/old_specialize.zi" \
    2> "$work/old_specialize.err"; then
    echo 'specialize keyword was accepted' >&2
    exit 1
fi
grep -Fq 'use Jai-style type application' "$work/old_specialize.err"

cat > "$work/bare_parameter.zi" <<'ZI'
Box :: struct(T: Type) {
    value: T;
}
NumberBox :: Box(s32);
Read :: (box: NumberBox) -> s32 {
    return box.value;
}
Two :: struct(T: Type) { first: T; second: T; }
TwoInts :: Two(s32);
Sum :: (value: TwoInts) -> s32 { return value.first + value.second; }
ZI
"$ziran" check --root "$work" "$work/bare_parameter.zi"

cat > "$work/wrong_error.zi" <<'ZI'
#import "result"
Input :: Result(s32, string)
Output :: Result(s32, s32)
Get :: () -> Input {
    return Input_Ok(1)
}
Bad :: () -> Output {
    value: s32 = Get()?
    return Output_Ok(value)
}
ZI
if "$ziran" check --root "$work" "$work/wrong_error.zi" \
    2> "$work/wrong_error.err"; then
    echo '? accepted a mismatched error payload' >&2
    exit 1
fi
grep -Fq '? requires Ok/Err variants with the same error payload type' \
    "$work/wrong_error.err"

cat > "$work/duplicate_record.zi" <<'ZI'
Bad :: struct($T: Type) {
    value: T
    value: T
}
ZI
if "$ziran" check --root "$work" "$work/duplicate_record.zi" \
    2> "$work/duplicate_record.err"; then
    echo 'duplicate generic record field was accepted' >&2
    exit 1
fi
grep -Fq 'duplicate generic record field: Bad.value' "$work/duplicate_record.err"

cat > "$work/unknown_template_type.zi" <<'ZI'
Bad :: struct($T: Type) {
    value: Missing
}
ZI
if "$ziran" check --root "$work" "$work/unknown_template_type.zi" \
    2> "$work/unknown_template_type.err"; then
    echo 'unused generic record with unknown field type was accepted' >&2
    exit 1
fi
grep -Fq 'unknown stored type: Bad.value' \
    "$work/unknown_template_type.err"

cat > "$work/unknown_variant_type.zi" <<'ZI'
Bad :: variant($T: Type) {
    Found: T
    Missing: Unknown
}
ZI
if "$ziran" check --root "$work" "$work/unknown_variant_type.zi" \
    2> "$work/unknown_variant_type.err"; then
    echo 'unused generic variant with unknown payload type was accepted' >&2
    exit 1
fi
grep -Fq 'unknown stored type: Bad.Missing' \
    "$work/unknown_variant_type.err"

cat > "$work/lazy_try.zi" <<'ZI'
#import "result"
Outcome :: Result(s32, string)
Get :: () -> Outcome { return Outcome_Ok(1) }
Bad :: (condition: bool) -> Outcome {
    return Outcome_Ok(ifx condition then Get()? else 0)
}
ZI
if "$ziran" check --root "$work" "$work/lazy_try.zi" \
    2> "$work/lazy_try.err"; then
    echo 'conditional error propagation was hoisted across a lazy branch' >&2
    exit 1
fi
grep -Fq '? requires left-to-right eager evaluation in this expression' \
    "$work/lazy_try.err"

cat > "$work/order_try.zi" <<'ZI'
#import "result"
Outcome :: Result(s32, string)
Get :: () -> Outcome { return Outcome_Ok(1) }
Bad :: (prior: s32) -> Outcome {
    return Outcome_Ok(prior + Get()?)
}
ZI
if "$ziran" check --root "$work" "$work/order_try.zi" \
    2> "$work/order_try.err"; then
    echo 'error propagation moved a prior read after a call' >&2
    exit 1
fi
grep -Fq '? requires left-to-right eager evaluation in this expression' \
    "$work/order_try.err"

cat > "$work/old_cast.zi" <<'ZI'
Answer :: () -> s32 {
    return (s32)42
}
ZI
if "$ziran" check --root "$work" "$work/old_cast.zi" \
    2> "$work/old_cast.err"; then
    echo 'C-style cast was accepted' >&2
    exit 1
fi
grep -Fq 'C-style cast or literal is not valid Jai syntax' "$work/old_cast.err"

cat > "$work/old_literal.zi" <<'ZI'
Box :: struct {
    value: s32;
}
Answer :: () -> s32 {
    value: Box = (Box){.value = 42}
    return value.value
}
ZI
if "$ziran" check --root "$work" "$work/old_literal.zi" \
    2> "$work/old_literal.err"; then
    echo 'C-style record literal was accepted' >&2
    exit 1
fi
grep -Fq 'C-style cast or literal is not valid Jai syntax' "$work/old_literal.err"

cat > "$work/old_array.zi" <<'ZI'
Answer :: () -> s32 {
    values: [2]s32 = {40, 2}
    return values[0] + values[1]
}
ZI
if "$ziran" check --root "$work" "$work/old_array.zi" \
    2> "$work/old_array.err"; then
    echo 'C-style array literal was accepted' >&2
    exit 1
fi
grep -Fq 'C-style array literal is not valid Jai syntax' "$work/old_array.err"

for old_type in i8 i16 i32 i64 f32 f64 double; do
    printf 'Answer :: () -> %s {\n    return 0\n}\n' "$old_type" \
        > "$work/old_type.zi"
    if "$ziran" check --root "$work" "$work/old_type.zi" \
        2> "$work/old_type.err"; then
        echo "non-Jai primitive spelling was accepted: $old_type" >&2
        exit 1
    fi
    grep -Fq "non-Jai primitive type spelling: $old_type" \
        "$work/old_type.err"
done

cat > "$work/type_mentions.zi" <<'ZI'
// i32, i64, and double in comments are ordinary text.
Answer :: () -> s32 {
    label: string = "i32 i64 double"
    return 42
}
ZI
"$ziran" check --root "$work" "$work/type_mentions.zi"

cat > "$work/jai_types.zi" <<'ZI'
Value :: (byte: s8, wide: s16, real: float32, precise: float64) -> s32 {
    return 42
}
ZI
"$ziran" check --root "$work" "$work/jai_types.zi"

cat > "$work/old_char.zi" <<'ZI'
Answer :: () -> u8 {
    return 'A'
}
ZI
if "$ziran" check --root "$work" "$work/old_char.zi" \
    2> "$work/old_char.err"; then
    echo 'single-quoted character literal was accepted' >&2
    exit 1
fi
grep -Fq 'single-quoted character literals are not valid Jai syntax' \
    "$work/old_char.err"
