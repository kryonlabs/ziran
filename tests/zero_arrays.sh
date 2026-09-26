#!/bin/sh
set -eu
ulimit -c 0

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/defs.zi" <<'ZI'
ZERO :: 0
ZI
cat > "$work/empty.zi" <<'ZI'
#import "defs"
EMPTY_SIZE :: size_of([0]s32)
EMPTY :: s32.[]
Box :: struct {
    empty: [ZERO]s32
    value: s32
}
Only :: struct { empty: [0]s32 }
Nested :: struct {
    empty: Only
    value: s32
}
EmptyAlias :: [ZERO]s32
global_empty: [0]s32;
global_literal: [0]s32 = s32.[];

#assert EMPTY_SIZE == 0
#assert size_of(Box) == 4
#assert size_of(Only) == 0
#assert size_of(Nested) == 4
#assert size_of([2][0]s32) == 0
#assert size_of(EmptyAlias) == 0
#assert s32.[].count == 0
#assert EMPTY.count == 0

Count :: (values: [0]s32) -> s64 { return values.count }
ReturnEmpty :: () -> [0]s32 { return s32.[] }

#program_export
Answer :: () -> s32 {
    typed := s32.[]
    contextual: [0]s32 = .[]
    returned: [0]s32 = ReturnEmpty()
    view: []s32 = typed[:]
    aliased: EmptyAlias = s32.[]
    record_empty := Box.[]
    matrix: [2][0]s32
    box: Box
    box.value = 42
    nested: Nested
    nested.value = box.value
    if typed.count != 0 || contextual.count != 0 ||
       returned.count != 0 || view.count != 0 || global_empty.count != 0 ||
       global_literal.count != 0 ||
       aliased.count != 0 || record_empty.count != 0 || matrix.count != 2 ||
       matrix[0].count != 0 || box.empty.count != 0 ||
       nested.empty.empty.count != 0 || Count(typed) != 0 ||
       typed.data != null || contextual.data != null ||
       returned.data != null || global_empty.data != null ||
       global_literal.data != null ||
       record_empty.data != null ||
       matrix[0].data != null || box.empty.data != null ||
       EMPTY_SIZE != 0 { return 0 }
    return nested.value
}

#program_export
Bad :: () -> s32 {
    values := s32.[]
    index: s32 = 0
    return values[index]
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/empty.zi"
for input in source saved; do
    if test "$input" = source; then
        root=$work
        file=$work/empty.zi
    else
        root=$work/ir
        file=$work/ir/empty.zir
    fi
    "$ziran" bundle --root "$root" --entry empty:Answer \
        -o "$work/$input.zib" "$file"
    test "$("$ziran" run "$work/$input.zib")" = 42
    "$ziran" bundle --root "$root" --entry empty:Bad \
        -o "$work/$input-bad.zib" "$file"
    if "$ziran" run "$work/$input-bad.zib" >"$work/bad.err" 2>&1; then
        echo "portable zero array index succeeded" >&2
        exit 1
    fi
    for target in c cpp go; do
        out="$work/$target-$input"
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$root" \
                -o "$out" "$file"
            cat > "$out/main.go" <<'GO'
package main
func main() { if Empty_Answer() != 42 { panic("empty arrays") } }
GO
            GO111MODULE=off go run "$out"/*.go
            cat > "$out/bad.go" <<'GO'
package main
func main() { Empty_Bad() }
GO
            if GO111MODULE=off go run "$out/empty.go" "$out/bad.go" \
                >"$work/bad.err" 2>&1; then
                echo "Go zero array index succeeded" >&2
                exit 1
            fi
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$root" -o "$out" "$file"
            cat > "$out/main.c" <<'C'
#include "empty.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c99 -Wall -Werror -pedantic-errors \
                -I"$repo/include" -I"$out" "$out"/*.c -o "$out/program"
            "$out/program"
            cat > "$out/bad.c" <<'C'
#include "empty.h"
int main(void) { return Bad(); }
C
            "${CC:-cc}" -std=c99 -Wall -Werror -pedantic-errors \
                -I"$repo/include" -I"$out" \
                "$out/empty.c" "$out/bad.c" -o "$out/bad"
            if "$out/bad" >"$work/bad.err" 2>&1; then
                echo "C zero array index succeeded" >&2
                exit 1
            fi
        else
            "$ziran" build --target=cpp --root "$root" -o "$out" "$file"
            cat > "$out/main.cpp" <<'CPP'
#include "empty.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -Wall -Werror -pedantic-errors \
                -I"$repo/include" -I"$out" "$out"/*.cpp -o "$out/program"
            "$out/program"
            cat > "$out/bad.cpp" <<'CPP'
#include "empty.hpp"
int main() { return Bad(); }
CPP
            "${CXX:-c++}" -std=c++17 -Wall -Werror -pedantic-errors \
                -I"$repo/include" -I"$out" \
                "$out/empty.cpp" "$out/bad.cpp" -o "$out/bad"
            if "$out/bad" >"$work/bad.err" 2>&1; then
                echo "C++ zero array index succeeded" >&2
                exit 1
            fi
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"
