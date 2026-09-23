#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/app" "$work/lib"
cat > "$work/lib/base.zi" <<'EOF'
#module "base"
Value :: () -> i32 #export {
    return 40
}
EOF
cat > "$work/lib/middle.zi" <<'EOF'
#module "middle"
#import "base"
Answer :: () -> i32 #export {
    return Value() + 2
}
EOF
cat > "$work/app/main.zi" <<'EOF'
#module "main"
#import "middle"
Result :: () -> i32 #export {
    return Answer()
}
EOF

"$ziran" check --root "$work/app" --module-path "$work/lib" \
    "$work/app/main.zi"
"$ziran" ir --root "$work/app" --module-path "$work/lib" \
    -o "$work/ir" "$work/app/main.zi"
for module in main middle base; do
    test -s "$work/ir/$module.zir"
done
"$ziran" bundle --root "$work/app" --module-path "$work/lib" \
    --entry main:Result -o "$work/source.zib" "$work/app/main.zi"
"$ziran" bundle --root "$work/ir" --module-path "$work/ir" \
    --entry main:Result -o "$work/saved.zib" "$work/ir/main.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/saved.zib")" = 42

"$ziran" build --target=c --strict --root "$work/app" \
    --module-path "$work/lib" -o "$work/c" "$work/app/main.zi"
cat > "$work/c/test.c" <<'EOF'
#include "main.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/base.c" "$work/c/middle.c" \
    "$work/c/main.c" "$work/c/test.c" -o "$work/test"
"$work/test"

if "$ziran" check --diagnostics=json --root "$work/app" "$work/app/main.zi" \
    2> "$work/missing.err"; then
    echo 'missing module search path unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'module.not_found' "$work/missing.err"
