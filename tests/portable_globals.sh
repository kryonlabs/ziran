#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ziran=${1:-"$repo/build/bin/ziran"}
ziran_lib=${ZIRAN_LIB:-"$repo/build/libziran.a"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/state.zi" <<'ZI'

Cell :: struct {
    value: s32
}
Store :: struct {
    cells: [64]Cell
    count: s32
}
current: Store;
unused: s32 = 7;
base :: 5
limit: s64 = 7;
scale: s32 = 3;
factor: s32 = base + 2;
ratio: float32 = 2.5;
name: string = "ziran";
flag: bool = true;
origin: Cell = Cell.{.value = 9};
Track :: struct {
    title: string
    weight: s32
}
tracks: [3]Track = .[Track.{.title = "first", .weight = 3}, Track.{.title = "second", .weight = 5}, Track.{.title = "third", .weight = 7}];
marks: [4]s32 = .[2, 4, 6, 8];

#program_export
InitChecks :: () -> s32 {
    if limit != 7 { return 0 }
    if scale != 3 { return 0 }
    if factor != 7 { return 0 }
    if ratio != 2.5 { return 0 }
    if flag != true { return 0 }
    if name.count != 5 { return 0 }
    if origin.value != 9 { return 0 }
    if marks[0] != 2 || marks[3] != 8 { return 0 }
    if tracks[0].weight != 3 || tracks[2].weight != 7 { return 0 }
    if tracks[1].title.count != 6 { return 0 }
    return 1
}

#program_export
Install :: (next: Store) {
    current = next
}
#program_export
Put :: (index: s32, value: s32) {
    current.cells[index].value = value
    current.count += 1
}
#program_export
Read :: (index: s32) -> s32 {
    return current.cells[index].value
}
#program_export
Snapshot :: () -> Store {
    return current
}
#program_export
Count :: () -> s32 {
    return current.count
}
ZI

cat > "$work/app.zi" <<'ZI'
#import "state"

#program_export
Answer :: () -> s32 {
    if InitChecks() != 1 { return -1 }
    source: Store
    source.cells[0].value = 41
    Install(source)
    source.cells[0].value = 0
    if Read(0) != 41 { return 0 }
    iteration: s32 = 0
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
            "$ziran" build --target=go --pkg main --root "$work" \
                -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("global value") } }
GO
            GO111MODULE=off go run "$output"/*.go
        else
            "$ziran" build --target="$target" --root "$work" \
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
initial: s32 = compute();
compute :: () -> s32 { return 3 }
#program_export
Answer :: () -> s32 {
    return initial
}
ZI
if "$ziran" bundle --root "$work" --entry invalid:Answer \
    -o "$work/invalid.zib" "$work/invalid.zi" >"$work/error" 2>&1; then
    exit 1
fi
grep -q 'portable global initializers need a scalar, string, record, or array literal value' \
    "$work/error"

cat > "$work/session.zi" <<'ZI'
counter: s32;
#program_export
Answer :: () -> s32 {
    counter += 1
    return counter
}
ZI
"$ziran" ir --root "$work" -o "$work/session-ir" "$work/session.zi"
"$ziran" bundle --root "$work" --entry session:Answer \
    -o "$work/session-source.zib" "$work/session.zi"
"$ziran" bundle --root "$work/session-ir" --entry session:Answer \
    -o "$work/session-saved.zib" "$work/session-ir/session.zir"
cmp "$work/session-source.zib" "$work/session-saved.zib"
cat > "$work/instance.c" <<'C'
#include "ziran_host.h"
#include <assert.h>

static void check(BundleInstance *instance, long long expected)
{
    long long value = 0;
    int has_value = 0;
    assert(BundleInstanceRun(instance, &value, &has_value));
    assert(has_value && value == expected);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    Bundle *bundle = BundleOpen(argv[1]);
    assert(bundle != NULL);
    BundleInstance *first = BundleInstantiate(bundle, NULL, 0);
    BundleInstance *second = BundleInstantiate(bundle, NULL, 0);
    assert(first != NULL && second != NULL);
    check(first, 1);
    check(first, 2);
    check(second, 1);
    check(first, 3);
    BundleInstanceClose(first);
    BundleInstanceClose(second);
    long long value = 0;
    int has_value = 0;
    assert(BundleRun(bundle, NULL, 0, &value, &has_value));
    assert(has_value && value == 1);
    assert(BundleRun(bundle, NULL, 0, &value, &has_value));
    assert(has_value && value == 1);
    BundleClose(bundle);
    return 0;
}
C
"${CC:-cc}" ${VM_CFLAGS:-} -std=c11 -I"$repo/include" \
    "$work/instance.c" "$ziran_lib" ${VM_LDFLAGS:-} -o "$work/instance"
"$work/instance" "$work/session-source.zib"
"$work/instance" "$work/session-saved.zib"
