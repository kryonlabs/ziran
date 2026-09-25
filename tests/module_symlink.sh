#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

ln -s "$repo/std/math.zi" "$work/math.zi"
cat > "$work/app.zi" <<'ZI'
#import "math"
#program_export
Answer :: () -> s32 { return FloorInteger(4.5) }
ZI

"$ziran" build --target=c --root "$work" -o "$work/c" "$work/app.zi"
test -f "$work/c/math.h"
test -f "$work/c/math.c"
cat > "$work/c/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 4 ? 0 : 1; }
C
"${CC:-cc}" -std=c99 -pedantic-errors -I"$repo/include" -I"$work/c" \
    "$work/c"/*.c -o "$work/app"
"$work/app"
