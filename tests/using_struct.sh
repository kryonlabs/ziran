#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Point :: struct { x: s64; y: s64; }
Middle :: struct { using point: Point; extra: s64; }
Outer :: struct { using middle: Middle; tag: s64; }
Wrapper :: struct (T: Type) { using value: T; bonus: s64; }
Make :: () -> Outer {
    return Outer.{.middle = Middle.{.point = Point.{.x = 18, .y = 0},
                                    .extra = 20}, .tag = 3}
}
Adjust :: (outer: Outer) -> s64 {
    outer.x += 1
    outer.y = outer.x + outer.extra
    return outer.y + outer.tag
}
ZI
cat > "$work/app.zi" <<'ZI'
#import "lib"
#program_export
Answer :: () -> s32 {
    outer := Outer.{.middle = Middle.{.point = Point.{.x = 18, .y = 0},
                                     .extra = 20}, .tag = 3}
    if Adjust(outer) != 42 { return 0 }
    if outer.x != 18 { return 0 }
    if outer.middle.point.x != outer.x { return 0 }
    if Make().x != outer.x { return 0 }
    wrapped := Wrapper(Point).{.value = Point.{.x = 40, .y = 0},
                              .bonus = 2}
    if wrapped.x + wrapped.bonus != 42 { return 0 }
    return 42
}
ZI

"$ziran" check --root "$work" "$work/app.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/app.zi"
"$ziran" bundle --root "$work" --entry app:Answer \
    -o "$work/source.zib" "$work/app.zi"
"$ziran" bundle --root "$work/ir" --entry app:Answer \
    -o "$work/saved.zib" "$work/ir/app.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42

for input in "$work/app.zi" "$work/ir/app.zir"; do
    case "$input" in
        *.zi) suffix=source; root=$work ;;
        *) suffix=saved; root=$work/ir ;;
    esac
    for target in c cpp go; do
        out="$work/$target-$suffix"
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$root" \
                -o "$out" "$input"
            cat > "$out/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("using struct") } }
GO
            GO111MODULE=off go run "$out"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$root" -o "$out" "$input"
            cat > "$out/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                "$out"/*.c -o "$out/app"
            "$out/app"
        else
            "$ziran" build --target=cpp --root "$root" -o "$out" "$input"
            cat > "$out/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$out" \
                "$out"/*.cpp -o "$out/app"
            "$out/app"
        fi
    done
done

cat > "$work/ambiguous.zi" <<'ZI'
One :: struct { value: s64; }
Two :: struct { value: s64; }
Both :: struct { using one: One; using two: Two; }
Bad :: (both: Both) -> s64 { return both.value }
ZI
if "$ziran" check --root "$work" "$work/ambiguous.zi" \
    2> "$work/ambiguous.err"; then
    echo 'ambiguous promoted record field was accepted' >&2
    exit 1
fi
grep -Fq 'ambiguous using record field' "$work/ambiguous.err"

cat > "$work/scalar.zi" <<'ZI'
Bad :: struct { using value: s64; }
ZI
if "$ziran" check --root "$work" "$work/scalar.zi" \
    2> "$work/scalar.err"; then
    echo 'scalar using field was accepted' >&2
    exit 1
fi
grep -Fq 'using field requires a concrete record type' "$work/scalar.err"
