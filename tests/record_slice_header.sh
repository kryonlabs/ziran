#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:-"$root/build/bin/ziran"}
zi2c=$(dirname "$compiler")/zi2c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$zi2c" --no-main --root "$root/std" \
    --module-path "$root/std" -o "$work/generated" \
    "$root/std/net_http.zi"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsyntax-only \
    -I"$root/include" -I"$work/generated" \
    "$work/generated/net_http.c"
