#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

mkdir "$work/src"
cat > "$work/src/main.zi" <<'EOF'
Inner :: struct { value: s32 }
Props :: struct { inner: Inner; scale: s32 }
Make :: () -> Props { return .{inner = .{value = 40}, scale = 2} }
Read :: (props: Props) -> s32 { return props.inner.value + props.scale }
#program_export
main :: () -> s32 {
    local := Make()
    if Read(local) != 42 { return 1 }
    return 0
}
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

zi2c="${ziran%/*}/zi2c"
"$zi2c" --target=plan9-c --root "$work/src" \
    -o "$work/direct" "$work/src/main.zi"
cmp "$work/generated/main.c" "$work/direct/main.c"
