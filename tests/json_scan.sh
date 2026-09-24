#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$ziran" check --root tests/spec --module-path std \
    tests/spec/json_scan_test.zi
"$ziran" ir --root tests/spec --module-path std -o "$work/ir" \
    tests/spec/json_scan_test.zi
"$ziran" bundle --root tests/spec --module-path std \
    --entry json_scan_test:SelfTest -o "$work/source.zib" \
    tests/spec/json_scan_test.zi
"$ziran" bundle --root "$work/ir" --module-path "$work/ir" \
    --entry json_scan_test:SelfTest -o "$work/saved.zib" \
    "$work/ir/json_scan_test.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
