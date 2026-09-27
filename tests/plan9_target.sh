#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

mkdir "$work/src"
cat > "$work/src/main.zi" <<'EOF'
#program_export
main :: () -> s32 { return 42 }
EOF

"$ziran" build --target=plan9-c --root "$work/src" \
    -o "$work/generated" "$work/src/main.zi"

test -f "$work/generated/main.c"
if rg -n '__auto_type|\{\s*\.|for\s*\(\s*(int|s32|u32)' \
        "$work/generated"/*.c; then
    echo 'plan9-c output retained unsupported C constructs' >&2
    exit 1
fi

if rg -n 'plan9-c' "$work/generated"/*.c "$work/generated"/*.h; then
    echo 'plan9-c leaked dispatcher metadata into generated C' >&2
    exit 1
fi
