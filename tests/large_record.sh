#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

{
    printf 'Large :: struct {\n'
    field=0
    while test "$field" -lt 180; do
        printf '    long_record_field_%03d: s32\n' "$field"
        field=$((field + 1))
    done
    printf '    link: *#this\n}\n'
    printf '#program_export\nAnswer :: () -> s32 { value: Large; value.long_record_field_179 = 42; return value.long_record_field_179 }\n'
} > "$work/large_record.zi"

"$ziran" check --root "$work" "$work/large_record.zi"
"$ziran" build --target=c --root "$work" \
    -o "$work/c" "$work/large_record.zi"
cat > "$work/main.c" <<'C'
#include "large_record.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
"${CC:-cc}" -std=c11 -I"$repo/include" -I"$work/c" \
    "$work/c/large_record.c" "$work/main.c" -o "$work/test"
"$work/test"
