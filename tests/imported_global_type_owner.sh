#!/bin/sh
set -eu

tool_dir=$(dirname "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/types.zi" <<'ZI'
AppConfig :: struct { width: s32 }
ZI
cat > "$work/state.zi" <<'ZI'
#import, file "types.zi";
config: AppConfig = AppConfig.{.width = 42};
ZI
cat > "$work/host.zi" <<'ZI'
#import, file "types.zi";
#import, file "state.zi";
#program_export
Read :: () -> s32 { return config.width }
ZI
cat > "$work/shadow.zi" <<'ZI'
Types :: #import "types"
State :: #import "state"
AppConfig :: struct { height: s32 }
Read :: () -> s32 { return State.config.width }
ZI

"$tool_dir/zi2zir" --check-only --root "$work" "$work/host.zi"
"$tool_dir/zi2zir" --check-only --root "$work" "$work/shadow.zi"
"$tool_dir/zi2c" --no-main --root "$work" -o "$work/c" "$work/host.zi"
cat > "$work/c/test_main.c" <<'C'
#include "host.h"
int main(void) { return Read() == 42 ? 0 : 1; }
C
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -Iinclude -I"$work/c" "$work/c"/*.c -o "$work/c/test_app"
"$work/c/test_app"
