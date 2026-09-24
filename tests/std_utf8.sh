#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$ziran" check --root tests/spec --module-path std tests/spec/std_utf8.zi
"$ziran" ir --root tests/spec --module-path std -o "$work/ir" \
    tests/spec/std_utf8.zi
"$ziran" bundle --root tests/spec --module-path std \
    --entry std_utf8:SelfTest -o "$work/source.zib" \
    tests/spec/std_utf8.zi
"$ziran" bundle --root "$work/ir" --module-path "$work/ir" \
    --entry std_utf8:SelfTest -o "$work/saved.zib" \
    "$work/ir/std_utf8.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42
