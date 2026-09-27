#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/copy.zi" <<'ZI'
#import "vec"
Holder :: struct { items: Vec(s32); }
Check :: () -> s32 {
    first: Holder
    VecPush(first.items, 1)
    second := first
    VecFree(first.items)
    VecFree(second.items)
    return 0
}
ZI
cat > "$work/assign.zi" <<'ZI'
#import "vec"
Holder :: struct { items: Vec(s32); }
Check :: () -> s32 {
    first: Holder
    second: Holder
    second = first
    return 0
}
ZI
cat > "$work/argument.zi" <<'ZI'
#import "vec"
Holder :: struct { items: Vec(s32); }
Take :: (value: Holder) -> s32 { return 0 }
ZI
cat > "$work/return.zi" <<'ZI'
#import "vec"
Holder :: struct { items: Vec(s32); }
Make :: () -> Holder {
    value: Holder
    return value
}
ZI
cat > "$work/field.zi" <<'ZI'
#import "vec"
Holder :: struct { items: Vec(s32); }
Check :: () -> s32 {
    value: Holder
    VecPush(value.items, 42)
    answer: s32 = value.items[0]
    VecFree(value.items)
    return answer
}
ZI

for name in copy assign argument return; do
    if "$ziran" check --diagnostics=json --root "$work" \
        --module-path "$repo/std" "$work/$name.zi" \
        > "$work/$name.out" 2> "$work/$name.err"; then
        echo "$name unexpectedly copied aggregate-owned storage" >&2
        exit 1
    fi
    python3 - "$work/$name.err" <<'PY'
import json
from pathlib import Path
import sys

diagnostics = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
assert any('aggregate containing Vec' in item['message'] for item in diagnostics), diagnostics
PY
done

"$ziran" check --root "$work" --module-path "$repo/std" "$work/field.zi"
