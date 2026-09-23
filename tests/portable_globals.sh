#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ziran=${1:-"$repo/build/bin/ziran"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/state.zi" <<'ZI'
#module "state"

Cell :: struct {
    value: i32
}
Store :: struct {
    cells: [64]Cell
    count: i32
}
current :: Store #global
unused :: i32 = 7 #global

Install :: (next: Store) #export {
    current = next
}
Put :: (index: i32, value: i32) #export {
    current.cells[index].value = value
    current.count += 1
}
Read :: (index: i32) -> i32 #export {
    return current.cells[index].value
}
Snapshot :: () -> Store #export {
    return current
}
Count :: () -> i32 #export {
    return current.count
}
ZI

cat > "$work/app.zi" <<'ZI'
#module "app"
#import "state"

Answer :: () -> i32 #export {
    source: Store
    source.cells[0].value = 41
    Install(source)
    source.cells[0].value = 0
    if Read(0) != 41 { return 0 }
    iteration: i32 = 0
    while iteration < 80 {
        Put(1, iteration)
        iteration += 1
    }
    snapshot: Store = Snapshot()
    snapshot.cells[0].value = 0
    if Read(0) != 41 || Read(1) != 79 || Count() != 80 {
        return 0
    }
    Install(source)
    if Read(0) != 0 || Count() != 0 { return 0 }
    return 42
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/app.zi"
"$ziran" bundle --root "$work" --entry app:Answer \
    -o "$work/source.zib" "$work/app.zi"
"$ziran" bundle --root "$work/ir" --entry app:Answer \
    -o "$work/saved.zib" "$work/ir/app.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42

for input in source saved; do
    if test "$input" = source; then
        module=$work/app.zi
    else
        module=$work/ir/app.zir
    fi
    for target in c cpp go; do
        output=$work/$target-$input
        if test "$target" = go; then
            "$ziran" build --target=go --strict --pkg main --root "$work" \
                -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("global value") } }
GO
            GO111MODULE=off go run "$output"/*.go
        else
            "$ziran" build --target="$target" --strict --root "$work" \
                -o "$output" "$module"
            if test "$target" = c; then
                cat > "$output/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
                "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                    "$output"/*.c -o "$output/app"
            else
                cat > "$output/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
                "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                    "$output"/*.cpp -o "$output/app"
            fi
            "$output/app"
        fi
    done
done

cat > "$work/invalid.zi" <<'ZI'
#module "invalid"
initial :: i32 = 7 #global
Answer :: () -> i32 #export {
    return initial
}
ZI
if "$ziran" bundle --root "$work" --entry invalid:Answer \
    -o "$work/invalid.zib" "$work/invalid.zi" >"$work/error" 2>&1; then
    exit 1
fi
grep -q 'portable globals need a value type and default initialization' \
    "$work/error"
