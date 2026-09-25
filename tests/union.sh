#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/overlay.zi" <<'ZI'
Cell :: union {
    narrow: u8
    wide: u64
}

Generic :: union($T: Type) {
    value: T
    bytes: [8]u8
}
#assert size_of(Generic(s32)) == 8

#program_export
Answer :: () -> s32 {
    if size_of(Cell) != 8 { return 0 }
    if size_of(Generic(s32)) != 8 { return 0 }
    cell: Cell
    cell.narrow = cast(u8)1
    cell.wide = cast(u64)40
    other: Generic(s32)
    other.value = 2
    return cast(s32)cell.wide + other.value
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/overlay.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/overlay.zi
        root=$work
    else
        module=$work/ir/overlay.zir
        root=$work/ir
    fi
    for target in c cpp; do
        out="$work/$target-$input"
        "$ziran" build --target="$target" --root "$root" \
            -o "$out" "$module"
        if test "$target" = c; then
            cat > "$out/main.c" <<'C'
#include <stddef.h>
#include "overlay.h"
_Static_assert(sizeof(Cell) == 8, "union layout");
_Static_assert(offsetof(Cell, narrow) == 0, "union first field");
_Static_assert(offsetof(Cell, wide) == 0, "union second field");
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                "$out"/*.c -o "$out/app"
        else
            cat > "$out/main.cpp" <<'CPP'
#include <cstddef>
#include "overlay.hpp"
static_assert(sizeof(Cell) == 8, "union layout");
static_assert(offsetof(Cell, narrow) == 0, "union first field");
static_assert(offsetof(Cell, wide) == 0, "union second field");
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$out" \
                "$out"/*.cpp -o "$out/app"
        fi
        "$out/app"
    done
    if "$ziran" build --target=go --root "$root" \
        -o "$work/go-$input" "$module" 2> "$work/go-$input.err"; then
        echo 'Go emitted incorrect struct storage for a union' >&2
        exit 1
    fi
    grep -Fq 'union storage is not supported by the Go target' \
        "$work/go-$input.err"
    if "$ziran" bundle --root "$root" --entry overlay:Answer \
        -o "$work/$input.zib" "$module" 2> "$work/zib-$input.err"; then
        echo 'portable bundle accepted an unsupported union' >&2
        exit 1
    fi
done
