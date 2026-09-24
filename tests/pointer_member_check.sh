#!/bin/sh
set -eu

compiler=${1:-build/bin/zi2zir}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/types.zi" <<'ZI'
#module "types"
Node :: struct {
    value: i32
}
ZI

cat > "$work/read.zi" <<'ZI'
#module "read"
#import "types"
Read :: (node: const Node*) -> i32 #export {
    return node->value
}
ZI
if "$compiler" --check-only --root "$work" "$work/read.zi" \
    >"$work/out" 2>"$work/err"; then
    echo "pointer member unexpectedly passed portable checking" >&2
    exit 1
fi
grep -q 'function is not supported by portable scalar emission: Read' "$work/err"
if grep -q 'expression is not supported by strict checking\|unknown record field' "$work/err"; then
    cat "$work/err" >&2
    exit 1
fi

sed 's/node->value/node->missing/' "$work/read.zi" > "$work/bad.zi"
if "$compiler" --check-only --root "$work" "$work/bad.zi" \
    >"$work/out" 2>"$work/err"; then
    echo "unknown pointer field unexpectedly passed checking" >&2
    exit 1
fi
grep -q 'unknown record field: missing' "$work/err"

sed 's/const Node\*/Node/' "$work/read.zi" > "$work/not_pointer.zi"
if "$compiler" --check-only --root "$work" "$work/not_pointer.zi" \
    >"$work/out" 2>"$work/err"; then
    echo "non-pointer field access unexpectedly passed checking" >&2
    exit 1
fi
grep -q 'pointer member requires a record pointer: Node' "$work/err"

echo "pointer member checking test passed"
