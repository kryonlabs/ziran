#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/app.zi" <<'ZI'
host_api :: #system_library "host_api";
Effect :: (value: s32) -> s32 #foreign host_api;
#program_export
main :: () -> s32 { return Effect(41) }
ZI
cat > "$work/provider.zi" <<'ZI'
#program_export
Effect :: (value: s32) -> s32 { return value + 1 }
ZI
cat > "$work/bad_provider.zi" <<'ZI'
#program_export
Effect :: (value: bool) -> s32 { return 1 }
ZI

"$ziran" ir --root "$work" -o "$work/ir" \
    "$work/app.zi" "$work/provider.zi"
"$ziran" bundle --root "$work" --entry app:main \
    --bind app:Effect=provider:Effect -o "$work/source.zib" \
    "$work/app.zi" "$work/provider.zi"
"$ziran" bundle --root "$work/ir" --entry app:main \
    --bind app:Effect=provider:Effect -o "$work/saved.zib" \
    "$work/ir/app.zir" "$work/ir/provider.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42

"$ziran" bundle --root "$work" --entry app:main \
    -o "$work/unbound.zib" "$work/app.zi"
if "$ziran" run "$work/unbound.zib" > "$work/unbound.out" 2> "$work/unbound.err"; then
    echo 'unbound capability unexpectedly ran' >&2
    exit 1
fi
rg -q 'missing host capability: app:Effect' "$work/unbound.err"

if "$ziran" bundle --root "$work" --entry app:main \
    --bind app:Effect=bad_provider:Effect -o "$work/bad.zib" \
    "$work/app.zi" "$work/bad_provider.zi" > "$work/bad.out" 2> "$work/bad.err"; then
    echo 'mismatched provider unexpectedly linked' >&2
    exit 1
fi
rg -q 'mismatched signature' "$work/bad.err"
