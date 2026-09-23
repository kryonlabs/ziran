#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/pointers.zi" <<'EOF'
#module "pointers"
#import "helper"

Present :: (value: *i32) -> bool #export {
    return value != nil
}

Choose :: (use_value: bool, value: *i32) -> *i32 #export {
    return use_value ? value : nil
}

Answer :: () -> i32 #export {
    pointer: *i32 = nil
    if Present(pointer) { return 0 }
    if Present(nil) { return 0 }
    if RejectNull(nil) { return 0 }
    if Choose(false, pointer) != nil { return 0 }
    pointer = nil
    return 42
}
EOF

cat > "$work/helper.zi" <<'EOF'
#module "helper"
RejectNull :: (value: *i32) -> bool #export {
    return value != nil
}
EOF

"$ziran" ir --root "$work" -o "$work/ir" "$work/pointers.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/pointers.zi
    else
        module=$work/ir/pointers.zir
    fi
    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            "$ziran" build --target=go --strict --pkg main --root "$work" \
                -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if Pointers_Answer() != 42 { panic("nil pointer") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --strict --root "$work" \
                -o "$output" "$module"
            cat > "$output/main.c" <<'C'
#include "pointers.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        else
            "$ziran" build --target=cpp --strict --root "$work" \
                -o "$output" "$module"
            cat > "$output/main.cpp" <<'CPP'
#include "pointers.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done

cat > "$work/invalid.zi" <<'EOF'
#module "invalid"
Wrong :: () -> i32 {
    value: i32 = nil
    return value
}
EOF
if "$ziran" check --root "$work" "$work/invalid.zi" \
    2> "$work/invalid.err"; then
    echo 'integer accepted nil initializer' >&2
    exit 1
fi
grep -Fq 'initializer type mismatch' "$work/invalid.err"

cat > "$work/invalid.zi" <<'EOF'
#module "invalid"
Wrong :: () -> i32 {
    value := nil
    return 0
}
EOF
if "$ziran" check --root "$work" "$work/invalid.zi" \
    2> "$work/invalid.err"; then
    echo 'untyped nil binding was accepted' >&2
    exit 1
fi
grep -Fq 'nil requires an explicit pointer type' "$work/invalid.err"

cat > "$work/invalid.zi" <<'EOF'
#module "invalid"
Wrong :: () -> const char* {
    return nil
}
EOF
if "$ziran" check --root "$work" "$work/invalid.zi" \
    2> "$work/invalid.err"; then
    echo 'Go string ABI accepted a nullable char pointer' >&2
    exit 1
fi
grep -Fq 'return type mismatch' "$work/invalid.err"

if "$ziran" bundle --root "$work" --entry pointers:Answer \
    -o "$work/pointers.zib" "$work/pointers.zi" \
    2> "$work/bundle.err"; then
    echo 'raw pointers entered portable bundle' >&2
    exit 1
fi
