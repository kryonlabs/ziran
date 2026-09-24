#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/choice.zi" <<'ZI'
Box :: struct {
    value: s32
}
Choice :: variant {
    Empty
    Number: s32
    Message: string
    Boxed: Box
}
ZI
cat > "$work/app.zi" <<'ZI'
#import "choice"
Value :: (choice: Choice) -> s32 {
    match choice {
        case Empty:
            return 0
        case Number(number):
            return number
        case Message(message):
            return message.length
        case Boxed(box):
            return box.value
    }
}
Default :: () -> s32 {
    value: Choice
    return Value(value)
}
#program_export
Answer :: () -> s32 {
    return Value(Choice_Number(20)) + Value(Choice_Message("hi")) +
           Value(Choice_Boxed(Box.{value = 20})) + Default()
}
#program_export
Invalid :: () -> s32 {
    return Choice_NumberValue(Choice_Empty())
}
ZI

"$ziran" check --root "$work" "$work/choice.zi" "$work/app.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/choice.zi" "$work/app.zi"
for input in source saved; do
    if test "$input" = source; then
        modules="$work/choice.zi $work/app.zi"
        root=$work
    else
        modules="$work/ir/choice.zir $work/ir/app.zir"
        root=$work/ir
    fi
    # shellcheck disable=SC2086
    "$ziran" bundle --root "$root" --entry app:Answer \
        -o "$work/$input.zib" $modules
    test "$("$ziran" run "$work/$input.zib")" = 42
    # shellcheck disable=SC2086
    "$ziran" bundle --root "$root" --entry app:Invalid \
        -o "$work/invalid-$input.zib" $modules
    if "$ziran" run "$work/invalid-$input.zib" 2> "$work/invalid.err"; then
        echo 'inactive variant payload was read' >&2
        exit 1
    fi
    grep -Fq 'unreachable code executed' "$work/invalid.err"

    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            # shellcheck disable=SC2086
            "$ziran" build --target=go --strict --pkg main --root "$root" \
                -o "$output" $modules
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("wrong variant result") } }
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

cat > "$work/missing.zi" <<'ZI'
#import "choice"
Value :: (choice: Choice) -> s32 {
    match choice {
        case Empty:
            return 0
        case Number(number):
            return number
    }
}
ZI
if "$ziran" check --root "$work" "$work/missing.zi" 2> "$work/missing.err"; then
    echo 'non-exhaustive variant match was accepted' >&2
    exit 1
fi
grep -Fq 'non-exhaustive match; missing case: Message' "$work/missing.err"

cat > "$work/private.zi" <<'ZI'
#import "choice"
Forge :: () -> Choice {
    value: Choice = Choice_Number(42)
    value.variant_tag = 0
    return value
}
ZI
if "$ziran" check --root "$work" "$work/private.zi" 2> "$work/private.err"; then
    echo 'variant storage was exposed' >&2
    exit 1
fi
grep -Fq 'variant storage is private; use match' "$work/private.err"

cat > "$work/inline.zi" <<'ZI'
Choice :: variant { Empty, Number: s32 }
#program_export
Answer :: () -> s32 {
    match Choice_Number(42) {
        case Empty:
            return 0
        case Number(number):
            return number
    }
}
ZI
"$ziran" bundle --root "$work" --entry inline:Answer \
    -o "$work/inline.zib" "$work/inline.zi"
test "$("$ziran" run "$work/inline.zib")" = 42

cat > "$work/duplicate.zi" <<'ZI'
Choice :: variant {
    Empty
    Empty
}
ZI
if "$ziran" check --root "$work" "$work/duplicate.zi" \
    2> "$work/duplicate.err"; then
    echo 'duplicate variant case was accepted' >&2
    exit 1
fi
grep -Fq 'duplicate variant case: Empty' "$work/duplicate.err"

cat > "$work/no_payload.zi" <<'ZI'
Choice :: variant {
    Empty
    Number: s32
}
Value :: (choice: Choice) -> s32 {
    match choice {
        case Empty(value):
            return value
        case Number(number):
            return number
    }
}
ZI
if "$ziran" check --root "$work" "$work/no_payload.zi" \
    2> "$work/no_payload.err"; then
    echo 'payloadless case binding was accepted' >&2
    exit 1
fi
grep -Fq 'case has no payload to bind: Empty' "$work/no_payload.err"

cat > "$work/host.zi" <<'ZI'
host_api :: #system_library "host_api";
Choice :: variant {
    Empty
    Number: s32
}
Receive :: (value: Choice) -> s32 #foreign host_api;
#program_export
Answer :: () -> s32 {
    return Receive(Choice_Number(42))
}
ZI
if "$ziran" bundle --root "$work" --entry host:Answer \
    -o "$work/host.zib" "$work/host.zi" 2> "$work/host.err"; then
    echo 'variant host boundary was accepted' >&2
    exit 1
fi
grep -Fq 'supported host capability' "$work/host.err"

{
    echo 'Wide :: variant {'
    case_number=0
    while test "$case_number" -lt 70; do
        echo "    Case$case_number: s32"
        case_number=$((case_number + 1))
    done
    echo '}'
    echo '#program_export'
    echo 'Answer :: () -> s32 {'
    echo '    return Wide_Case69Value(Wide_Case69(42))'
    echo '}'
} > "$work/wide.zi"
"$ziran" bundle --root "$work" --entry wide:Answer \
    -o "$work/wide.zib" "$work/wide.zi"
test "$("$ziran" run "$work/wide.zib")" = 42
