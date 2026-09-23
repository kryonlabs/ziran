#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/arraylib.zi" <<'EOF'
#module "arraylib"

BASE :: 1
CAPACITY :: BASE + 1
THREE :: CAPACITY + 1

Point :: struct {
    x: i32
}

Box :: struct {
    values: [CAPACITY]Point
}

LargeBox :: struct {
    values: [4096]Point
}

RoundTripLarge :: (box: LargeBox) -> LargeBox #export {
    copy: LargeBox = box
    copy.values[0].x = 42
    return copy
}

CheckLarge :: (box: LargeBox) -> bool #export {
    updated: LargeBox = RoundTripLarge(box)
    return box.values[0].x == 41 && updated.values[0].x == 42
}

SumBox :: (box: Box) -> i32 #export {
    return box.values[0].x + box.values[1].x
}

SumArray :: (values: [THREE]i32) -> i32 #export {
    return values[0] + values[1]
}
EOF

cat > "$work/arrayapp.zi" <<'EOF'
#module "arrayapp"
#import "arraylib"

ReadLoop :: () -> i32 {
    values: [4096]i32
    values[0] = 42
    total: i32 = 0
    index: i32 = 0
    while index < 300 {
        total += values[0]
        index += 1
    }
    return total / 300
}

Answer :: () -> i32 #export {
    if ReadLoop() != 42 { return 0 }
    large: LargeBox
    large.values[0].x = 41
    iteration: i32 = 0
    while iteration < 80 {
        if !CheckLarge(large) { return 0 }
        iteration += 1
    }
    moving: LargeBox = large
    iteration = 0
    while iteration < 80 {
        moving = RoundTripLarge(moving)
        iteration += 1
    }
    if large.values[0].x != 41 || moving.values[0].x != 42 {
        return 0
    }
    values: [THREE]i32 = {41, 1, 0}
    copy: [THREE]i32 = values
    values[0] = 0
    if SumArray(copy) != 42 { return 0 }
    point: Point
    point.x = 41
    box: Box
    box.values[0] = point
    point.x = 1
    box.values[1] = point
    saved: Box = box
    point.x = 0
    box.values[0] = point
    return SumBox(saved)
}

OutOfBounds :: () -> i32 #export {
    values: [1]i32
    index: i32 = 1
    return values[index]
}
EOF

"$ziran" ir --root "$work" -o "$work/ir" "$work/arrayapp.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/arrayapp.zi
        module_dir=$work
    else
        module=$work/ir/arrayapp.zir
        module_dir=$work/ir
    fi
    "$ziran" bundle --root "$module_dir" --entry arrayapp:Answer \
        -o "$work/$input.zib" "$module"
    test "$("$ziran" run "$work/$input.zib")" = 42

    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            "$ziran" build --target=go --strict --pkg main \
                --root "$module_dir" -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if Arrayapp_Answer() != 42 { panic("array result") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --strict --root "$module_dir" \
                -o "$output" "$module"
            cat > "$output/main.c" <<'C'
#include "arrayapp.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        else
            "$ziran" build --target=cpp --strict --root "$module_dir" \
                -o "$output" "$module"
            cat > "$output/main.cpp" <<'CPP'
#include "arrayapp.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done

cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/capacity.zi" <<'EOF'
#module "capacity"
LENGTH :: 1 + 1
EOF
cat > "$work/capacity_app.zi" <<'EOF'
#module "capacity_app"
#import "capacity"
Answer :: () -> i32 #export {
    values: [LENGTH]i32 = {40, 2}
    return values[0] + values[1]
}
EOF
"$ziran" ir --root "$work" -o "$work/capacity-ir" "$work/capacity_app.zi"
"$ziran" bundle --root "$work" --entry capacity_app:Answer \
    -o "$work/capacity-source.zib" "$work/capacity_app.zi"
"$ziran" bundle --root "$work/capacity-ir" --entry capacity_app:Answer \
    -o "$work/capacity-saved.zib" "$work/capacity-ir/capacity_app.zir"
cmp "$work/capacity-source.zib" "$work/capacity-saved.zib"
test "$("$ziran" run "$work/capacity-source.zib")" = 42

"$ziran" bundle --root "$work" --entry arrayapp:OutOfBounds \
    -o "$work/out-of-bounds.zib" "$work/arrayapp.zi"
if "$ziran" run "$work/out-of-bounds.zib" \
    2> "$work/out-of-bounds.err"; then
    echo 'portable array read escaped its bounds' >&2
    exit 1
fi
grep -Fq 'portable execution failed' "$work/out-of-bounds.err"
