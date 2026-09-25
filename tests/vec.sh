#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/app.zi" <<'ZI'
#import "vec"
#import "option"

Item :: struct {
    value: s32
    label: string
}

items: Vec(Item);

PopDrain :: () -> s32 {
    values: Vec(s32)
    VecPush(values, 10)
    VecPush(values, 20)
    VecPush(values, 30)
    result: s32 = 0
    popped: Option(s32) = VecPop(values)
    if !popped.has_value { return -10 }
    result += popped.value
    found: Option(s32) = VecGet(values, 0)
    if !found.has_value { return -11 }
    result += found.value
    missing: Option(s32) = VecGet(values, 9)
    if missing.has_value { return -12 }
    while values.count > 0 {
        tail: Option(s32) = VecPop(values)
        if !tail.has_value { return -13 }
        result += tail.value
    }
    drained: Option(s32) = VecPop(values)
    if drained.has_value { return -14 }
    VecFree(values)
    return result
}

BuiltText :: () -> s32 {
    builder: Vec(u8)
    if !BuilderAppend(builder, "hello, ") { return -20 }
    if !BuilderAppend(builder, "ziran") { return -21 }
    text: string = BuilderFinish(builder)
    if text.count != 12 { return -22 }
    if builder.count != 0 || builder.capacity != 0 { return -23 }
    if !BuilderAppend(builder, "") { return -24 }
    if BuilderFinish(builder).count != 0 { return -25 }
    VecFree(builder)
    total: s64 = 0
    index: s64 = 0
    while index < text.count {
        total += cast(s64)text[index]
        index += 1
    }
    return cast(s32)total
}

RecordPop :: () -> s32 {
    entries: Vec(Item)
    entry: Item
    entry.value = 5
    entry.label = "five"
    VecPush(entries, entry)
    entry.value = 6
    entry.label = "six"
    VecPush(entries, entry)
    taken: Option(Item) = VecPop(entries)
    if !taken.has_value { return -30 }
    if taken.value.value != 6 { return -31 }
    if taken.value.label.count != 3 { return -32 }
    first: Option(Item) = VecGet(entries, 0)
    if !first.has_value { return -33 }
    if first.value.label.count != 4 { return -34 }
    VecFree(entries)
    return first.value.value + taken.value.value
}

#program_export
Answer :: () -> s32 {
    index: s32 = 0
    while index < 100 {
        item: Item
        item.value = index
        item.label = "entry"
        if !VecPush(items, item) { return -1 }
        index += 1
    }
    if items.count != 100 || items.capacity < items.count {
        return -2
    }
    items[5].value += 1
    result: s32 = items[5].value + items[99].value
    VecClear(items)
    if items.count != 0 { return -3 }
    item: Item
    item.value = 1
    if !VecPush(items, item) { return -4 }
    result += items[0].value
    VecFree(items)
    if items.count != 0 || items.capacity != 0 { return -5 }
    if PopDrain() != 70 { return -6 }
    if BuiltText() != 1156 { return -7 }
    if RecordPop() != 11 { return -8 }
    return result
}
ZI

"$ziran" ir --root "$work" --module-path "$repo/std" \
    -o "$work/ir" "$work/app.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/app.zi
        root=$work
    else
        module=$work/ir/app.zir
        root=$work/ir
    fi
    "$ziran" bundle --root "$root" --module-path "$repo/std" \
        --entry app:Answer -o "$work/$input.zib" "$module"
    test "$("$ziran" run "$work/$input.zib")" = 106
    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --entry app:Answer \
                --root "$root" --module-path "$repo/std" \
                -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 106 { panic("Vec result") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --entry app:Answer --root "$root" \
                --module-path "$repo/std" -o "$output" "$module"
            cat > "$output/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 106 ? 0 : 1; }
C
            "${CC:-cc}" -std=c99 -pedantic-errors -DZIRAN_BOUNDS_CHECK \
                -I"$repo/include" -I"$output" "$output"/*.c \
                -o "$output/app"
            "$output/app"
        else
            "$ziran" build --target=cpp --entry app:Answer --root "$root" \
                --module-path "$repo/std" -o "$output" "$module"
            cat > "$output/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 106 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -DZIRAN_BOUNDS_CHECK \
                -I"$repo/include" -I"$output" "$output"/*.cpp \
                -o "$output/app"
            "$output/app"
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/private.zi" <<'ZI'
#import "vec"
items: Vec(s32);
#program_export
Bad :: () -> s32 { return items.data[0] }
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/private.zi" 2> "$work/private.err"; then
    echo 'Vec storage was accessible' >&2
    exit 1
fi
rg -q 'Vec storage is private' "$work/private.err"

cat > "$work/copy.zi" <<'ZI'
#import "vec"
items: Vec(s32);
#program_export
Bad :: () -> s32 {
    alias: Vec(s32) = items
    VecFree(alias)
    return 0
}
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/copy.zi" 2> "$work/copy.err"; then
    echo 'Vec copy was accepted' >&2
    exit 1
fi
rg -q 'Vec values cannot be copied' "$work/copy.err"

cat > "$work/count.zi" <<'ZI'
#import "vec"
items: Vec(s32);
#program_export
Bad :: () { items.count = 5 }
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/count.zi" 2> "$work/count.err"; then
    echo 'Vec count mutation was accepted' >&2
    exit 1
fi
rg -q 'read-only' "$work/count.err"

cat > "$work/nooption.zi" <<'ZI'
#import "vec"
#program_export
Bad :: () -> s32 {
    values: Vec(s32)
    VecPush(values, 1)
    popped: Option(s32) = VecPop(values)
    return 0
}
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/nooption.zi" 2> "$work/nooption.err"; then
    echo 'VecPop without option was accepted' >&2
    exit 1
fi
rg -q 'import option' "$work/nooption.err"

cat > "$work/badindex.zi" <<'ZI'
#import "vec"
#import "option"
#program_export
Bad :: () -> s32 {
    values: Vec(s32)
    found: Option(s32) = VecGet(values, "zero")
    return 0
}
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/badindex.zi" 2> "$work/badindex.err"; then
    echo 'VecGet with a string index was accepted' >&2
    exit 1
fi
rg -q 'integer index' "$work/badindex.err"

cat > "$work/badbuilder.zi" <<'ZI'
#import "vec"
#program_export
Bad :: () -> s32 {
    values: Vec(s32)
    VecPush(values, 1)
    text: string = BuilderFinish(values)
    return 0
}
ZI
if "$ziran" check --root "$work" --module-path "$repo/std" \
    "$work/badbuilder.zi" 2> "$work/badbuilder.err"; then
    echo 'BuilderFinish on a non-u8 Vec was accepted' >&2
    exit 1
fi
rg -q -F 'Vec(u8)' "$work/badbuilder.err"
