#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Greeting :: (source: string = "hello") -> string {
    return source
}
Scale :: (source: float64 = 2.0) -> float64 {
    return source * 1.5
}
ZI
cat > "$work/app.zi" <<'ZI'
Lib :: #import "lib";
MESSAGE :: #run Lib.Greeting("hello");
FACTOR :: #run Lib.Scale(2.0);
RAW :: #run 2.0;
#assert MESSAGE == "hello"
#assert FACTOR == 3.0
#assert RAW == 2.0
#assert Lib.Greeting("h\u0065llo") == "hel\x6co"
#assert "\U0001F642".count == 4
#assert Lib.Greeting() == "hello"
#assert Lib.Scale() == 3.0
#if 2 + 2 == 4 {
SELECTED :: 42;
} else {
SELECTED :: Missing();
}
#if Lib.Greeting("hello") == "hello" && Lib.Scale(2.0) == 3.0 {
IMPORTED :: 42;
} else {
IMPORTED :: Missing();
}
#if MESSAGE == "hello" && FACTOR == 3.0 {
DERIVED :: 42;
} else {
DERIVED :: Missing();
}
#program_export
Answer :: () -> s64 {
    if MESSAGE != "hello" || FACTOR != 3.0 || RAW != 2.0 { return 0 }
    return #ifx FACTOR == 3.0 then SELECTED + IMPORTED + DERIVED - 84 else Missing()
}
ZI

"$ziran" check --root "$work" "$work/app.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/app.zi"
if rg -a -q '#run|#ifx|Missing' "$work/ir/app.zir"; then
    echo 'unresolved compile-time expression reached saved IR' >&2
    exit 1
fi
"$ziran" bundle --root "$work" --entry app:Answer \
    -o "$work/source.zib" "$work/app.zi"
"$ziran" bundle --root "$work/ir" --entry app:Answer \
    -o "$work/saved.zib" "$work/ir/app.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/saved.zib")" = 42

for input in source saved; do
    if test "$input" = source; then
        root=$work
        file=$work/app.zi
    else
        root=$work/ir
        file=$work/ir/app.zir
    fi
    for target in c cpp go; do
        out=$work/$target-$input
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$root" \
                -o "$out" "$file"
            cat > "$out/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("typed #run") } }
GO
            GO111MODULE=off go run "$out"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$root" -o "$out" "$file"
            cat > "$out/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                "$out"/*.c -o "$out/app"
            "$out/app"
        else
            "$ziran" build --target=cpp --root "$root" -o "$out" "$file"
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

cat > "$work/effect.zi" <<'ZI'
counter: s64;
Touch :: () -> string {
    counter += 1
    return "bad"
}
MESSAGE :: #run Touch();
ZI
if "$ziran" check --root "$work" "$work/effect.zi" \
    2> "$work/effect.err"; then
    echo 'effectful typed compile-time call was accepted' >&2
    exit 1
fi
rg -q '#run expression is not a constant' "$work/effect.err"
