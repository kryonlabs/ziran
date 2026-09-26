#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Point :: struct { x: s64; y: s64; }
Entity :: struct { position: Point; }
using shared: Point;
LibValue :: () -> s64 { x = 7; return x }
ZI
cat > "$work/app.zi" <<'ZI'
#import "lib"
using, only("x") point;
point: Point;
using, only("x") point;
using, map("nested_x" = "x") entity.position;
entity: Entity;
using, only("y") prepared: Point = Point.{.x = 20, .y = 22};
Overlay :: union { left: s64; right: s64; }
using, map("union_left" = "left") overlay: Overlay;
#program_export
Answer :: () -> s32 {
    x = prepared.x
    nested_x = y
    union_left = 42
    {
        x: s64 = 9
        if x != 9 { return 0 }
    }
    if point.x != 20 || entity.position.x != 22 { return 0 }
    if overlay.right != 42 { return 0 }
    if LibValue() != 7 { return 0 }
    return cast(s32) (x + nested_x)
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
func main() { if App_Answer() != 42 { panic("data scope using") } }
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
Box :: struct { value: s64; }
using first: Box;
using second: Box;
Bad :: () -> s64 { return value }
ZI
if "$ziran" check --root "$work" "$work/ambiguous.zi" \
    2> "$work/ambiguous.err"; then
    echo 'ambiguous file-scope record using was accepted' >&2
    exit 1
fi
grep -Fq 'ambiguous using field: value' "$work/ambiguous.err"

cat > "$work/scalar.zi" <<'ZI'
using number;
number: s64;
ZI
if "$ziran" check --root "$work" "$work/scalar.zi" \
    2> "$work/scalar.err"; then
    echo 'file-scope scalar using was accepted' >&2
    exit 1
fi
grep -Fq 'using requires a concrete record binding' "$work/scalar.err"

cat > "$work/shadow.zi" <<'ZI'
Point :: struct { x: s64; }
using point: Point;
Bad :: () -> s64 {
    point: Point;
    return x
}
ZI
if "$ziran" check --root "$work" "$work/shadow.zi" \
    2> "$work/shadow.err"; then
    echo 'shadowed file-scope using root still promoted fields' >&2
    exit 1
fi
grep -Fq 'unresolved name: x' "$work/shadow.err"

cat > "$work/private.zi" <<'ZI'
#scope_file
using private: Point;
#scope_module
Inside :: () -> s64 { x = 11; return x }
ZI
cat > "$work/private_app.zi" <<'ZI'
Point :: struct { x: s64; }
#load "private.zi";
Bad :: () -> s64 { return x }
ZI
cat > "$work/private_ok.zi" <<'ZI'
Point :: struct { x: s64; }
#load "private.zi";
Answer :: () -> s64 { return Inside() }
ZI
"$ziran" check --root "$work" "$work/private_ok.zi"
if "$ziran" check --root "$work" "$work/private_app.zi" \
    2> "$work/private.err"; then
    echo 'file-private record using leaked across #load' >&2
    exit 1
fi
grep -Fq 'unresolved name: x' "$work/private.err"
