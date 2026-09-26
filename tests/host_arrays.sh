#!/bin/sh
set -eu
ulimit -c 0

ziran=${1:?pass the ziran command}
host_test=${2:?pass the host array test binary}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/arrays.zi" <<'ZI'
host_api :: #system_library "host_api";
Box :: struct {
    empty: [0]s32
    value: s32
}
EchoEmpty :: (values: [0]s32) -> [0]s32 #foreign host_api;
EchoBoxes :: (values: [2]Box) -> [2]Box #foreign host_api;

#program_export
Answer :: () -> s32 {
    empty: [0]s32
    returned: [0]s32 = EchoEmpty(empty)
    boxes: [2]Box
    boxes[0].value = 40
    boxes[1].value = 2
    changed: [2]Box = EchoBoxes(boxes)
    if returned.count != 0 || returned.data != null ||
       changed.count != 2 || changed[0].empty.count != 0 ||
       changed[1].empty.count != 0 { return 0 }
    return changed[0].value + changed[1].value
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/arrays.zi"
"$ziran" bundle --root "$work" --entry arrays:Answer \
    -o "$work/source.zib" "$work/arrays.zi"
"$ziran" bundle --root "$work/ir" --entry arrays:Answer \
    -o "$work/saved.zib" "$work/ir/arrays.zir"
cmp "$work/source.zib" "$work/saved.zib"
"$host_test" "$work/source.zib"
"$host_test" "$work/saved.zib"
