#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/record.zi" <<'ZI'
Inner :: struct {
    resource: MissingResource
}
Outer :: struct {
    inner: Inner
}
ZI

if "$ziran" check --root "$work" "$work/record.zi" > "$work/out" 2> "$work/err"; then
    echo 'record with missing field type unexpectedly checked' >&2
    exit 1
fi
grep -Fq 'unknown stored type: Inner.MissingResource' "$work/err"
