#!/bin/sh
set -eu

ziran=$1
tool_dir=$(dirname "$ziran")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$ziran" check --root tests/spec --module-path std tests/spec/zip_test.zi
"$ziran" ir --root tests/spec --module-path std -o "$work/ir" tests/spec/zip_test.zi
"$ziran" bundle --root tests/spec --module-path std \
    --entry zip_test:SelfTest -o "$work/source.zib" tests/spec/zip_test.zi
"$ziran" bundle --root "$work/ir" --module-path "$work/ir" \
    --entry zip_test:SelfTest -o "$work/saved.zib" "$work/ir/zip_test.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 0

"$ziran" check --root tests/spec --module-path std tests/spec/zip_linux_test.zi
"$tool_dir/zi2c" --root tests/spec --module-path std \
    --entry zip_linux_test:SelfTest -o "$work/native" tests/spec/zip_linux_test.zi
"${CC:-cc}" -std=c11 -Iinclude -I"$work/native" \
    "$work/native/zip.c" "$work/native/zip_linux.c" \
    "$work/native/zip_linux_test.c" -x c - -lz -o "$work/zip-test" <<'EOF'
int SelfTest(void);
int main(void) { return SelfTest(); }
EOF
env -u DISPLAY -u WAYLAND_DISPLAY "$work/zip-test"
