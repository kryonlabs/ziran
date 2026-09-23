#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/wide.zi" <<'EOF'
#module "wide"

Answer :: () -> i32 #export {
    high: i64 = (i64)2147483647 + (i64)100
    if high != (i64)2147483747 { return 0 }
    low: i64 = (i64)(-2147483648) - (i64)100
    if low != (i64)(-2147483748) { return 0 }
    if ((i64)(-1) >> (i64)1) != (i64)(-1) { return 0 }
    maximum: i64 = (i64)9223372036854775807
    minimum: i64 = maximum + (i64)1
    if minimum >= (i64)0 { return 0 }
    if minimum / (i64)(-1) != minimum { return 0 }
    if ((i64)1 << (i64)63) >= (i64)0 { return 0 }
    return 42
}
EOF
"$ziran" ir --root "$work" -o "$work/ir" "$work/wide.zi"
"$ziran" bundle --root "$work" --entry wide:Answer \
    -o "$work/source.zib" "$work/wide.zi"
"$ziran" bundle --root "$work/ir" --entry wide:Answer \
    -o "$work/saved.zib" "$work/ir/wide.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42
