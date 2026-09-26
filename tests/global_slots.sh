#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
#scope_export
AddTwo :: (value: s32) -> s32 { return value + 2 }
ZI
cat > "$work/main.zi" <<'ZI'
#import, file "lib.zi";
Callback :: #type (value: s32) -> s32;
Holder :: struct { callback: Callback }
AddOne :: (value: s32) -> s32 { return value + 1 }
holders: [2] Holder = .[Holder.{callback = AddOne}, Holder.{callback = AddTwo}];
current: Callback = AddOne;
#program_export
Answer :: () -> s32 {
    return holders[0].callback(41) + holders[1].callback(40) + current(41)
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/lib.zi" "$work/main.zi"
for input in source saved; do
    if test "$input" = source; then
        set -- "$work/lib.zi" "$work/main.zi"
    else
        set -- "$work/ir/lib.zir" "$work/ir/main.zir"
    fi
    for target in c cpp go; do
        "$ziran" build --target="$target" --root "$work" \
            -o "$work/$target-$input" "$@"
    done

    cat > "$work/c-$input/host.c" <<'C'
#include "main.h"
int main(void) { return Answer() == 126 ? 0 : 1; }
C
    "${CC:-cc}" -std=c11 -pedantic-errors -I"$repo/include" \
        -I"$work/c-$input" "$work/c-$input/lib.c" \
        "$work/c-$input/main.c" "$work/c-$input/host.c" \
        -o "$work/c-$input/app"
    "$work/c-$input/app"

    cat > "$work/cpp-$input/host.cpp" <<'CPP'
#include "main.hpp"
int main() { return Answer() == 126 ? 0 : 1; }
CPP
    "${CXX:-c++}" -std=c++17 -I"$repo/include" \
        -I"$work/cpp-$input" "$work/cpp-$input/lib.cpp" \
        "$work/cpp-$input/main.cpp" "$work/cpp-$input/host.cpp" \
        -o "$work/cpp-$input/app"
    "$work/cpp-$input/app"

    cat > "$work/go-$input/main_test.go" <<'GO'
package ziran
import "testing"
func TestGlobalSlot(t *testing.T) {
    if got := Main_Answer(); got != 126 { t.Fatalf("Answer() = %d", got) }
}
GO
    (cd "$work/go-$input" && GO111MODULE=off go test)
done
