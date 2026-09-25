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

cat > "$work/scalar.zi" <<'ZI'
Scalar :: union {
    narrow: u8
    wide: u64
    real: float64
}

#program_export
Overlap :: () -> s32 {
    cell: Scalar
    cell.narrow = cast(u8)255
    if cell.wide != 255 { return 1 }
    other: Scalar
    other.wide = cast(u64)513
    if other.narrow != 1 { return 2 }
    cell.wide = cast(u64)10
    cell.wide += cast(u64)32
    if cell.narrow != 42 { return 3 }
    stored: Scalar
    stored.real = 2.0
    if stored.real != 2.0 { return 4 }
    return 42
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/overlay.zi"
"$ziran" ir --root "$work" -o "$work/scalar-ir" "$work/scalar.zi"
mkdir -p "$work/ir"
cp "$work/scalar-ir/scalar.zir" "$work/ir/scalar.zir" 2>/dev/null || true
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
    if test "$input" = source; then
        scalar=$work/scalar.zi
        scalar_root=$work
    else
        scalar=$work/ir/scalar.zir
        scalar_root=$work/ir
    fi
    "$ziran" build --target=go --pkg main --entry scalar:Overlap \
        --root "$scalar_root" -o "$work/go-$input" "$scalar"
    cat > "$work/go-$input/main.go" <<'GO'
package main
func main() { if Scalar_Overlap() != 42 { panic("union overlap go") } }
GO
    GO111MODULE=off go run "$work/go-$input"/*.go
    "$ziran" bundle --root "$scalar_root" --entry scalar:Overlap \
        -o "$work/overlap-$input.zib" "$scalar"
    test "$("$ziran" run "$work/overlap-$input.zib")" = 42
done
cmp "$work/overlap-source.zib" "$work/overlap-saved.zib"

cat > "$work/badunion.zi" <<'ZI'
BadUnion :: union {
    text: string
    value: s32
}
#program_export
Bad :: () -> s32 {
    u: BadUnion
    return 0
}
ZI
if "$ziran" bundle --root "$work" --entry badunion:Bad \
    -o "$work/badunion.zib" "$work/badunion.zi" \
    2> "$work/badunion.err"; then
    echo 'portable bundle accepted a non-scalar union field' >&2
    exit 1
fi
grep -Fq 'portable unions support scalar fields only' \
    "$work/badunion.err"
