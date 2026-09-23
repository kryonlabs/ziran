#!/bin/sh
set -eu

ziran=$1
host_test=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/host_api.zi" <<'EOF'
#module "host_api"
AddTenHost :: (value: i32) -> i32 #extern
ByteCountHost :: (value: string) -> i32 #extern
DoubleHost :: (value: float) -> float #extern
EchoHost :: (value: u32) -> u32 #extern
UnusedHost :: () -> i32 #extern
AddTen :: (value: i32) -> i32 #export {
    return AddTenHost(value)
}
ByteCount :: (value: string) -> i32 #export {
    return ByteCountHost(value)
}
Double :: (value: float) -> float #export {
    return DoubleHost(value)
}
Echo :: (value: u32) -> u32 #export {
    return EchoHost(value)
}
EOF
cat > "$work/application.zi" <<'EOF'
#module "application"
#import "host_api"
Answer :: () -> i32 #export {
    if Echo((u32)4294967295) != (u32)4294967295 { return 0 }
    return AddTen(28) + ByteCount("hi") + (i32)Double(1.0)
}
EOF
"$ziran" ir --root "$work" -o "$work/ir" "$work/application.zi"
"$ziran" bundle --root "$work" --entry application:Answer \
    -o "$work/source.zib" "$work/application.zi"
"$ziran" bundle --root "$work/ir" --entry application:Answer \
    -o "$work/saved.zib" "$work/ir/application.zir"
cmp "$work/source.zib" "$work/saved.zib"
python3 - "$work/source.zib" <<'PY'
from pathlib import Path
import struct
import sys
data = Path(sys.argv[1]).read_bytes()
offset = 8
for _ in range(2):
    length, = struct.unpack_from('<I', data, offset)
    offset += 4 + length
count, = struct.unpack_from('<I', data, offset)
assert count == 4, count
assert b'UnusedHost' not in data
PY
"$host_test" "$work/source.zib"
"$host_test" "$work/saved.zib"
if "$ziran" run "$work/source.zib" 2> "$work/missing.err"; then
    echo 'bundle unexpectedly ran without its host capabilities' >&2
    exit 1
fi
grep -Fq 'missing host capability: host_api:AddTenHost' "$work/missing.err"
python3 - "$work/source.zib" "$work/tampered.zib" <<'PY'
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
assert data[:8] == b'ZIB\0\x02\0\0\0'
assert b'AddTenHost' in data
data[data.index(b'AddTenHost')] = ord('X')
Path(sys.argv[2]).write_bytes(data)
PY
if "$ziran" run "$work/tampered.zib" 2> "$work/tampered.err"; then
    echo 'tampered capability list unexpectedly ran' >&2
    exit 1
fi
grep -Fq 'bundle capability list differs from linked IR' "$work/tampered.err"
cat > "$work/unsupported.zi" <<'EOF'
#module "unsupported"
Box :: struct {
    value: i32*
}
Borrow :: () -> Box #extern
Answer :: () -> i32 #export {
    box: Box = Borrow()
    return 42
}
EOF
if "$ziran" bundle --root "$work" --entry unsupported:Answer \
    -o "$work/unsupported.zib" "$work/unsupported.zi" \
    2> "$work/unsupported.err"; then
    echo 'pointer capability unexpectedly bundled' >&2
    exit 1
fi
grep -Fq 'supported host capability' "$work/unsupported.err"
