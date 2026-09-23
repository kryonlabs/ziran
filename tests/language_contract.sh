#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source=$repo/tests/spec/language_contract.zi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$ziran" check --root "$repo/tests/spec" "$source"
"$ziran" ir --root "$repo/tests/spec" -o "$work/ir" "$source"

for input in "$source" "$work/ir/language_contract.zir"; do
    kind=$(basename "$input")
    c_out=$work/c-$kind
    cpp_out=$work/cpp-$kind
    go_out=$work/go-$kind
    "$ziran" build --target=c --strict --root "$repo/tests/spec" \
        -o "$c_out" "$input"
    "$ziran" build --target=cpp --strict --root "$repo/tests/spec" \
        -o "$cpp_out" "$input"
    "$ziran" build --target=go --strict --pkg main \
        --root "$repo/tests/spec" -o "$go_out" "$input"

    cat > "$c_out/main.c" <<'EOF'
#include "language_contract.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
    ${CC:-cc} -std=c11 -I"$repo/include" -I"$c_out" \
        "$c_out/language_contract.c" "$c_out/main.c" -o "$c_out/app"
    "$c_out/app"

    cat > "$cpp_out/main.cpp" <<'EOF'
#include "language_contract.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
    ${CXX:-c++} -std=c++17 -I"$repo/include" -I"$cpp_out" \
        "$cpp_out/language_contract.cpp" "$cpp_out/main.cpp" \
        -o "$cpp_out/app"
    "$cpp_out/app"

    cat > "$go_out/main.go" <<'EOF'
package main
func main() { if LanguageContract_Answer() != 42 { panic("language contract") } }
EOF
    GO111MODULE=off go run "$go_out/language_contract.go" "$go_out/main.go"
done
