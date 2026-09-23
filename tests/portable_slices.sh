#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/viewlib.zi" <<'ZI'
#module "viewlib"
Tail :: (values: []i32) -> []i32 #export {
    return values[1:]
}

Sum :: (values: []i32) -> i32 #export {
    return values[0] + values[1]
}
ZI

cat > "$work/slices.zi" <<'ZI'
#module "slices"
#import "viewlib"

Point :: struct {
    x: i32
}

state :: [2]i32 #global

GlobalView :: () -> []i32 {
    return state[:]
}

ReplaceGlobalWhileBorrowed :: () -> i32 {
    state[0] = 1
    state[1] = 2
    view: []i32 = GlobalView()
    replacement: [2]i32 = {40, 2}
    state = replacement
    return view[0] + view[1]
}

ReplaceWhileBorrowed :: () -> i32 {
    values: [2]i32 = {1, 2}
    view: []i32 = values[:]
    replacement: [2]i32 = {40, 2}
    values = replacement
    return view[0] + view[1]
}

Answer :: () -> i32 #export {
    if ReplaceWhileBorrowed() != 42 { return -1 }
    global_result: i32 = ReplaceGlobalWhileBorrowed()
    if global_result != 42 { return global_result }
    empty: []i32
    if empty.length != 0 { return -3 }
    values: [4]i32 = {1, 2, 3, 4}
    part: []i32 = values[1:3]
    part[0] = 40
    tail: []i32 = Tail(part)
    if part.length != 2 || tail.length != 1 ||
        tail[0] != 3 || values[1] != 40 { return -4 }
    points: [2]Point
    view: []Point = points[:]
    view[1].x = 7
    if points[1].x != 7 { return -5 }
    return Sum(part) - 1
}

ReadOutside :: () -> i32 #export {
    values: [2]i32 = {1, 2}
    view: []i32 = values[:]
    return view[2]
}

WriteOutside :: () -> i32 #export {
    values: [2]i32 = {1, 2}
    view: []i32 = values[:]
    view[2] = 3
    return values[0]
}

RangeOutside :: () -> i32 #export {
    values: [2]i32 = {1, 2}
    view: []i32 = values[0:3]
    return view.length
}

RangeReverse :: () -> i32 #export {
    values: [2]i32 = {1, 2}
    low: i32 = 2
    high: i32 = 1
    view: []i32 = values[low:high]
    return view.length
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/slices.zi"
for input in source saved; do
    if test "$input" = source; then
        source=$work/slices.zi
        root=$work
    else
        source=$work/ir/slices.zir
        root=$work/ir
    fi
    "$ziran" bundle --root "$root" --entry slices:Answer \
        -o "$work/$input.zib" "$source"
    test "$("$ziran" run "$work/$input.zib")" = 42
    for target in c cpp go; do
        output="$work/$target-$input"
        "$ziran" build --target="$target" --strict --root "$root" \
            -o "$output" "$source"
        if test "$target" = go; then
            cat > "$output/main_test.go" <<'GO'
package ziran
import "testing"
func TestSlice(t *testing.T) {
    if Slices_Answer() != 42 { t.Fatal("slice result") }
}
GO
            GO111MODULE=off go test "$output"/*.go
        elif test "$target" = c; then
            cat > "$output/main.c" <<'C'
#include "slices.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        else
            cat > "$output/main.cpp" <<'CPP'
#include "slices.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done

cat > "$work/invalid_slice_record.zi" <<'ZI'
#module "invalid_slice_record"
Bad :: struct {
    values: []i32
}
ZI
if "$ziran" check --root "$work" "$work/invalid_slice_record.zi" \
    2> "$work/invalid_slice_record.err"; then
    echo 'slice descriptor entered a record' >&2
    exit 1
fi

cat > "$work/invalid_slice_global.zi" <<'ZI'
#module "invalid_slice_global"
view :: []i32 #global
ZI
if "$ziran" check --root "$work" "$work/invalid_slice_global.zi" \
    2> "$work/invalid_slice_global.err"; then
    echo 'slice descriptor entered a global' >&2
    exit 1
fi

cat > "$work/invalid_slice_escape.zi" <<'ZI'
#module "invalid_slice_escape"
Escape :: () -> []i32 #export {
    values: [2]i32 = {1, 2}
    return values[:]
}
ZI
if "$ziran" check --root "$work" "$work/invalid_slice_escape.zi" \
    2> "$work/invalid_slice_escape.err"; then
    echo 'local slice escaped its backing array' >&2
    exit 1
fi

cat > "$work/invalid_slice_host.zi" <<'ZI'
#module "invalid_slice_host"
ReadHost :: (values: []i32) -> i32 #extern
Run :: () -> i32 #export {
    values: [1]i32 = {1}
    return ReadHost(values[:])
}
ZI
if "$ziran" check --root "$work" "$work/invalid_slice_host.zi" \
    2> "$work/invalid_slice_host.err"; then
    echo 'slice descriptor crossed a host call' >&2
    exit 1
fi
cmp "$work/source.zib" "$work/saved.zib"

for entry in ReadOutside WriteOutside RangeOutside RangeReverse; do
    "$ziran" bundle --root "$work" --entry "slices:$entry" \
        -o "$work/$entry.zib" "$work/slices.zi"
    if "$ziran" run "$work/$entry.zib" 2> "$work/$entry.err"; then
        echo "portable slice $entry escaped its bounds" >&2
        exit 1
    fi
    rg -q 'portable execution failed' "$work/$entry.err"
done
