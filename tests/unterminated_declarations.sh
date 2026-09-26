#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/main.zi" <<'ZI'
values: [3]s32 = .[1,
    2, 3];
#program_export
Answer :: () -> s32 { return values[2] }
ZI
"$ziran" check --root "$work" "$work/main.zi"

cat > "$work/main.zi" <<'ZI'
values: [3]s32 = .[1,
    2, 3};
#program_export
Answer :: () -> s32 { return values[2] }
ZI
if "$ziran" check --root "$work" "$work/main.zi" \
    2> "$work/unclosed.err"; then
    echo 'accepted an unfinished array and silently omitted the export' >&2
    exit 1
fi
grep -Fq 'unterminated source declaration or expression' \
    "$work/unclosed.err"

cat > "$work/part.zi" <<'ZI'
values: [3]s32 = .[1, 2, 3};
ZI
cat > "$work/main.zi" <<'ZI'
#load "part.zi";
#program_export
Answer :: () -> s32 { return 42 }
ZI
if "$ziran" check --root "$work" "$work/main.zi" \
    2> "$work/load.err"; then
    echo 'accepted an unfinished declaration in a loaded file' >&2
    exit 1
fi
grep -Fq 'unterminated source declaration or expression' "$work/load.err"
