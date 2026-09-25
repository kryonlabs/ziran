#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Pair :: struct { left: s64; right: s64; }
Sum :: (using pair: Pair) -> s64 {
    return left + right
}
Explicit :: (pair: Pair) -> s64 {
    using pair;
    left += 1
    return left + right
}
ZI
cat > "$work/app.zi" <<'ZI'
#import "lib"
#program_export
Answer :: () -> s32 {
    using pair: Pair = Pair.{.left = 20, .right = 22}
    if Sum(pair) != 42 { return 0 }
    if Explicit(Pair.{.left = 19, .right = 22}) != 42 { return 0 }
    left += 1
    if left != 21 { return 0 }
    {
        left: s64 = 9
        if left != 9 { return 0 }
    }
    if left != 21 { return 0 }
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
func main() { if App_Answer() != 42 { panic("using") } }
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
Bad :: (using one: Box, using two: Box) -> s64 {
    return value
}
ZI
if "$ziran" check --root "$work" "$work/ambiguous.zi" \
    2> "$work/ambiguous.err"; then
    echo 'ambiguous using field was accepted' >&2
    exit 1
fi
grep -Fq 'ambiguous using field' "$work/ambiguous.err"

cat > "$work/scalar.zi" <<'ZI'
Bad :: (using value: s64) -> s64 { return value }
ZI
if "$ziran" check --root "$work" "$work/scalar.zi" \
    2> "$work/scalar.err"; then
    echo 'scalar using binding was accepted' >&2
    exit 1
fi
grep -Fq 'using requires a concrete record binding' "$work/scalar.err"

cat > "$work/scope.zi" <<'ZI'
Box :: struct { value: s64; }
Bad :: () -> s64 {
    {
        using box: Box = Box.{.value = 1}
    }
    return value
}
ZI
if "$ziran" check --root "$work" "$work/scope.zi" \
    2> "$work/scope.err"; then
    echo 'out-of-scope using field was accepted' >&2
    exit 1
fi
grep -Fq 'unresolved name: value' "$work/scope.err"
