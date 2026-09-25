#!/bin/sh
set -eu

ziran=${1:-build/bin/ziran}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Native emission of pointer-target casts (cast(*T)) and nested fixed-array
# locals ([N][M]u8); the VM keeps rejecting pointer casts on its own.
cat > "$work/game_surface.zi" <<'ZI'
Crab :: struct {
    dir: s32
    x: float32
}

crab_storage: Crab;

TakeProps :: (props: *void) -> s32 {
    crab := cast(*Crab)props
    return crab.dir
}

#program_export
Answer :: () -> s32 {
    crab_storage.dir = 7
    handle := cast(*void)*crab_storage
    if TakeProps(handle) != 7 { return 1 }
    back := cast(*Crab)handle
    back.dir = 9
    if crab_storage.dir != 9 { return 2 }
    names: [4][8]u8
    names[0][0] = cast(u8)65
    names[3][7] = cast(u8)66
    if names[0][0] != cast(u8)65 { return 3 }
    if names[3][7] != cast(u8)66 { return 4 }
    return 0
}
ZI

"$ziran" check --root "$work" "$work/game_surface.zi"
"$ziran" build --target=c --root "$work" -o "$work/c" "$work/game_surface.zi"
cat > "$work/c/smoke.c" <<'C'
#include "game_surface.h"
int main(void) { return Answer(); }
C
"${CC:-cc}" -std=c11 -Wall -Werror -I"$repo/include" -I"$work/c" \
    "$work/c/game_surface.c" "$work/c/smoke.c" -o "$work/c/app"
"$work/c/app"

# Portable bundles keep rejecting reachable pointer casts.
cat > "$work/portable.zi" <<'ZI'
Crab :: struct {
    dir: s32
}

storage: Crab;

Probe :: () -> s32 {
    handle := cast(*Crab)*storage
    return handle.dir
}
ZI
if "$ziran" bundle --entry portable:Probe --root "$work" -o "$work/b" \
    "$work/portable.zi" > "$work/portable.out" 2>&1; then
    echo "pointer cast unexpectedly entered a portable bundle" >&2
    exit 1
fi
echo "pointer casts emit for native targets and stay out of bundles"
