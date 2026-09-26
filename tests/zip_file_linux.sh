#!/bin/sh
set -eu

tool_dir=$(dirname "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$1" check --root tests/spec --module-path std \
    tests/spec/zip_file_linux_test.zi
"$tool_dir/zi2c" --no-main --root tests/spec --module-path std \
    -o "$work/c" tests/spec/zip_file_linux_test.zi
"${CC:-cc}" -std=c11 -Iinclude -I"$work/c" \
    "$work/c/zip.c" "$work/c/zip_file_linux.c" \
    "$work/c/file_linux.c" "$work/c/c_string.c" \
    "$work/c/mapped_file_linux.c" "$work/c/byte_text_linux.c" \
    "$work/c/zip_file_linux_test.c" -x c - -o "$work/test" <<'EOF'
#include "zip_file_linux_test.h"
#include <stdint.h>
int main(int argc, char **argv) {
    return argc == 3 ? SelfTest((uint8_t *)argv[1], (uint8_t *)argv[2]) : 12;
}
EOF
env -u DISPLAY -u WAYLAND_DISPLAY "$work/test" "$work/archive.tmp" "$work/archive.zip"
python3 - "$work/archive.zip" <<'PY'
import sys
from zipfile import ZipFile
with ZipFile(sys.argv[1]) as archive:
    assert archive.namelist() == ['meta', 'a']
    assert archive.read('meta') == b'{}'
    assert archive.read('a') == b'hello'
PY
