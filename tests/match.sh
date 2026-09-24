#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/modes.zi" <<'ZI'
Mode :: enum {
    Off = -1
    On
    Later = On + 4
}
ZI
cat > "$work/app.zi" <<'ZI'
#import "modes"
calls: s32;
Next :: () -> Mode {
    calls += 1
    return cast(Mode)On
}
Value :: (mode: Mode) -> s32 {
    if #complete mode == {
        case Mode.Off;
            return 30
        case Mode.On;
            if true { return 41 }
            return 0
        case Mode.Later;
            return 42
    }
}
Nested :: (mode: Mode) -> s32 {
    if #complete mode == {
        case Mode.Off;
            if #complete cast(Mode)On == {
                case Mode.Off;
                    return 0
                case Mode.On;
                    return 42
                case Mode.Later;
                    return 0
            }
        case Mode.On;
            return 0
        case Mode.Later;
            return 0
    }
}
Once :: () -> s32 {
    if #complete Next() == {
        case Mode.Off;
            return 0
        case Mode.On;
            return calls
        case Mode.Later;
            return 0
    }
}
#program_export
Answer :: () -> s32 {
    if ~(cast(s32)0) != cast(s32)(-1) { return 0 }
    return Value(cast(Mode)Off) + Value(cast(Mode)On) + Value(cast(Mode)Later) + Nested(cast(Mode)Off) + Once()
}
#program_export
Invalid :: () -> s32 {
    return Value(cast(Mode)99)
}
ZI

"$ziran" check --root "$work" "$work/modes.zi" "$work/app.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/modes.zi" "$work/app.zi"
for input in source saved; do
    if test "$input" = source; then
        modules="$work/modes.zi $work/app.zi"
        root=$work
    else
        modules="$work/ir/modes.zir $work/ir/app.zir"
        root=$work/ir
    fi
    # Paths from mktemp contain no whitespace on supported test systems.
    # shellcheck disable=SC2086
    "$ziran" bundle --root "$root" --entry app:Answer \
        -o "$work/$input.zib" $modules
    test "$("$ziran" run "$work/$input.zib")" = 156
    # shellcheck disable=SC2086
    "$ziran" bundle --root "$root" --entry app:Invalid \
        -o "$work/invalid-$input.zib" $modules
    if "$ziran" run "$work/invalid-$input.zib" 2> "$work/invalid.err"; then
        echo 'invalid enum value unexpectedly matched' >&2
        exit 1
    fi
    grep -Fq 'unreachable code executed' "$work/invalid.err"

    for target in c cpp go; do
        output="$work/$target-$input"
        # shellcheck disable=SC2086
        if test "$target" = go; then
            # shellcheck disable=SC2086
            "$ziran" build --target=go --strict --pkg main --root "$root" \
                -o "$output" $modules
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 156 { panic("wrong match result") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            # shellcheck disable=SC2086
            "$ziran" build --target=c --strict --root "$root" \
                -o "$output" $modules
            cat > "$output/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 156 ? 0 : 1; }
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
int main() { return Answer() == 156 ? 0 : 1; }
CPP
            "${CXX:-c++}" -I"$repo/include" -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/missing.zi" <<'ZI'
#import "modes"
Value :: (mode: Mode) -> s32 {
    if #complete mode == {
        case Mode.Off;
            return 1
        case Mode.On;
            return 2
    }
}
ZI
if "$ziran" check --diagnostics=json --root "$work" "$work/modes.zi" "$work/missing.zi" \
    2> "$work/missing.err"; then
    echo 'non-exhaustive match unexpectedly accepted' >&2
    exit 1
fi
grep -Fq '"code":"check.match"' "$work/missing.err"
grep -Fq 'non-exhaustive match; missing case: Later' "$work/missing.err"

cat > "$work/duplicate.zi" <<'ZI'
#import "modes"
Value :: (mode: Mode) -> s32 {
    if #complete mode == {
        case Mode.Off;
            return 1
        case Mode.On;
            return 2
        case Mode.On;
            return 3
    }
}
ZI
if "$ziran" check --root "$work" "$work/modes.zi" "$work/duplicate.zi" \
    2> "$work/duplicate.err"; then
    echo 'duplicate match case unexpectedly accepted' >&2
    exit 1
fi
grep -Fq 'duplicate match case: On' "$work/duplicate.err"

cat > "$work/not_enum.zi" <<'ZI'
Value :: (number: s32) -> s32 {
    if #complete number == {
        case .Zero;
            return 0
    }
}
ZI
if "$ziran" check --root "$work" "$work/not_enum.zi" \
    2> "$work/not_enum.err"; then
    echo 'non-enum match unexpectedly accepted' >&2
    exit 1
fi
grep -Fq 'match requires a checked enum or variant value' "$work/not_enum.err"

cat > "$work/inline.zi" <<'ZI'
Mode :: enum { Off = -1, On = 1 }
#program_export
Answer :: () -> s32 {
    if #complete cast(Mode)On == {
        case Mode.Off;
            return 0
        case Mode.On;
            return 42
    }
}
ZI
"$ziran" bundle --root "$work" --entry inline:Answer \
    -o "$work/inline.zib" "$work/inline.zi"
test "$("$ziran" run "$work/inline.zib")" = 42

cat > "$work/partial.zi" <<'ZI'
Color :: enum { Red; Blue; }
Value :: (color: Color) -> s32 {
    if color == {
    case .Red;
        return 1;
    }
    return 2;
}
#program_export
Answer :: () -> s32 {
    return Value(cast(Color)0) + Value(cast(Color)1) + 39;
}
ZI
"$ziran" bundle --root "$work" --entry partial:Answer \
    -o "$work/partial.zib" "$work/partial.zi"
test "$("$ziran" run "$work/partial.zib")" = 42

cat > "$work/unqualified.zi" <<'ZI'
Color :: enum { Red; Blue; }
Value :: (color: Color) -> s32 {
    if #complete color == {
        case Red;
            return 1
        case .Blue;
            return 2
    }
}
ZI
if "$ziran" check --root "$work" "$work/unqualified.zi" \
    2> "$work/unqualified.err"; then
    echo 'unqualified Jai enum case was accepted' >&2
    exit 1
fi
grep -Fq "enum case requires 'case .Member;'" "$work/unqualified.err"

cat > "$work/old_match.zi" <<'ZI'
Color :: enum { Red; Blue; }
Value :: (color: Color) -> s32 {
    match color {
        case Red: return 1
        case Blue: return 2
    }
}
ZI
if "$ziran" check --root "$work" "$work/old_match.zi" \
    2> "$work/old_match.err"; then
    echo 'non-Jai enum match was accepted' >&2
    exit 1
fi
grep -Fq 'enum cases use Jai if-case syntax' "$work/old_match.err"

{
    printf 'Many :: enum {\n'
    index=0
    while test "$index" -lt 70; do
        printf '    Item%d = %d\n' "$index" "$index"
        index=$((index + 1))
    done
    printf '}\n#program_export\nAnswer :: () -> s32 {\n    if #complete cast(Many)Item69 == {\n'
    index=0
    while test "$index" -lt 70; do
        printf '        case Many.Item%d;\n            return %d\n' "$index" "$index"
        index=$((index + 1))
    done
    printf '    }\n}\n'
} > "$work/many.zi"
"$ziran" bundle --root "$work" --entry many:Answer \
    -o "$work/many.zib" "$work/many.zi"
test "$("$ziran" run "$work/many.zib")" = 69
