#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/slotlib.zi" <<'EOF'
#module "slotlib"

Child :: (value: i32) #slot

Props :: struct {
    value: i32
}

Container :: (props: Props, child: Child) -> i32 #export {
    child(props.value)
    return props.value + 1
}
EOF
cat > "$work/slotapp.zi" <<'EOF'
#module "slotapp"
#import "slotlib"

Answer :: () -> i32 #export {
    observed: i32 = 0
    Container chosen: {
        value = 41
        child = (value: i32) #slot {
            observed = value
        }
    }
    if observed != 41 { return 0 }
    return chosen
}
EOF

"$ziran" ir --root "$work" -o "$work/ir" \
    "$work/slotlib.zi" "$work/slotapp.zi"

for input in source saved; do
    if test "$input" = source; then
        set -- "$work/slotlib.zi" "$work/slotapp.zi"
    else
        set -- "$work/ir/slotlib.zir" "$work/ir/slotapp.zir"
    fi
    c_out="$work/c-$input"
    cpp_out="$work/cpp-$input"
    go_out="$work/go-$input"
    "$ziran" build --target=c --strict --root "$work" -o "$c_out" "$@"
    "$ziran" build --target=cpp --strict --root "$work" -o "$cpp_out" "$@"
    "$ziran" build --target=go --strict --pkg main \
        --root "$work" -o "$go_out" "$@"

    cat > "$c_out/main.c" <<'C'
#include "slotapp.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
    "${CC:-cc}" -std=c11 -I"$repo/include" -I"$c_out" \
        "$c_out/slotlib.c" "$c_out/slotapp.c" "$c_out/main.c" \
        -o "$c_out/app"
    "$c_out/app"

    cat > "$cpp_out/main.cpp" <<'CPP'
#include "slotapp.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
    "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$cpp_out" \
        "$cpp_out/slotlib.cpp" "$cpp_out/slotapp.cpp" \
        "$cpp_out/main.cpp" -o "$cpp_out/app"
    "$cpp_out/app"

    cat > "$go_out/main.go" <<'GO'
package main
func main() { if Slotapp_Answer() != 42 { panic("slot result") } }
GO
    GO111MODULE=off go run "$go_out/slotlib.go" \
        "$go_out/slotapp.go" "$go_out/main.go"
done
