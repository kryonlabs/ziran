#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/hello.zi" <<'EOF'
#module "hello"
Text :: () -> i32 #export {
    return 42
}
Answer :: () -> i32 #export {
    return Text()
}
EOF

"$ziran" check --root "$work" "$work/hello.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/hello.zi"
test -s "$work/ir/hello.zir"
grep -Fq 'zir 1' "$work/ir/hello.zir"
"$ziran" build --target=c --root "$work" -o "$work/c" "$work/hello.zi"
test -s "$work/c/hello.c"
cat > "$work/main.c" <<'EOF'
#include "hello.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/hello.c" "$work/main.c" -o "$work/hello"
"$work/hello"
"$ziran" build --target=go --strict --pkg main --root "$work" -o "$work/go" "$work/hello.zi"
test -s "$work/go/hello.go"
cat > "$work/go/main.go" <<'EOF'
package main
func main() { if Hello_Answer() != 42 { panic("wrong result") } }
EOF
GO111MODULE=off go run "$work/go/hello.go" "$work/go/main.go"

cat > "$work/library.zi" <<'EOF'
#module "library"
Button :: (value: i32) -> i32 #export {
    return value + 1
}
EOF
cat > "$work/app.zi" <<'EOF'
#module "app"
#import "library"
Answer :: () -> i32 #export {
    return Button(41)
}
EOF
"$ziran" build --target=c --strict --root "$work" -o "$work/modules" \
    "$work/library.zi" "$work/app.zi"
cat > "$work/modules/main.c" <<'EOF'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/modules" "$work/modules/library.c" \
    "$work/modules/app.c" "$work/modules/main.c" -o "$work/modules/app"
"$work/modules/app"

cat > "$work/bad.zi" <<'EOF'
#module "bad"
#assert 0, "expected failure"
EOF
if "$ziran" check --diagnostics=json --root "$work" "$work/bad.zi" \
    2> "$work/diagnostic.json"; then
    echo 'invalid source unexpectedly passed' >&2
    exit 1
fi
python3 -m json.tool "$work/diagnostic.json" > /dev/null

cat > "$work/ui_mode.zi" <<'EOF'
Screen :: () #ui {
}
EOF
if "$ziran" check --root "$work" "$work/ui_mode.zi" \
    2> "$work/ui_mode.err"; then
    echo '#ui unexpectedly passed in Ziran' >&2
    exit 1
fi
grep -Fq '#ui is not a Ziran modifier' "$work/ui_mode.err"
