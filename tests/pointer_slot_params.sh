#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/slots.zi" <<'EOF'
Record :: struct {
    value: s32
}

Reader :: #type (record: *Record, index: s32) -> s32;
Factory :: #type () -> *Record;
Sink :: #type (record: *Record) -> ();

#program_export
Answer :: () -> s32 {
    return 42
}
EOF

"$ziran" check --root "$work" "$work/slots.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/slots.zi"
"$ziran" bundle --root "$work" --entry slots:Answer \
    -o "$work/source.zib" "$work/slots.zi"
"$ziran" bundle --root "$work/ir" --entry slots:Answer \
    -o "$work/saved.zib" "$work/ir/slots.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
