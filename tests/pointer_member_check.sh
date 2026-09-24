#!/bin/sh
set -eu

compiler=${1:-build/bin/zi2zir}
bin=$(CDPATH= cd -- "$(dirname -- "$compiler")" && pwd)
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/types.zi" <<'ZI'
#module "types"
Child :: struct {
    value: i32
}
Node :: struct {
    value: i32
    child: Child
}
ZI

cat > "$work/read.zi" <<'ZI'
#module "read"
#import "types"
Read :: (node: const Node*) -> i32 #export {
    return node->value + node->child.value
}
Write :: (node: Node*, value: i32) -> i32 #export {
    if node == nil { return -1 }
    node->value = value
    node->child.value = value + 1
    return node->value + node->child.value
}
Forward :: (node: Node*) -> Node* #export {
    return node
}
WriteForward :: (node: Node*, value: i32) -> i32 #export {
    if node == nil { return -1 }
    Forward(node)->value = value
    return node->value
}
ZI
"$compiler" --check-only --root "$work" "$work/read.zi"
"$compiler" --root "$work" -o "$work/ir" "$work/read.zi"
for input in source saved; do
    if test "$input" = source; then
        root=$work
        file=$work/read.zi
    else
        root=$work/ir
        file=$work/ir/read.zir
    fi
    output=$work/$input

    "$bin/zi2c" --no-main --strict --root "$root" -o "$output/c" "$file"
    cat > "$output/c/main.c" <<'C'
#include "read.h"
int main(void) {
    Node node = {0};
    return Write(&node, 20) == 41 && Read(&node) == 41 &&
           WriteForward(&node, 23) == 23 && node.value == 23 &&
           node.child.value == 21 ? 0 : 1;
}
C
    "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output/c" \
        "$output/c/types.c" "$output/c/read.c" "$output/c/main.c" \
        -o "$output/c/test"
    env -u DISPLAY -u WAYLAND_DISPLAY "$output/c/test"

    "$bin/zi2cpp" --no-main --strict --root "$root" -o "$output/cpp" "$file"
    cat > "$output/cpp/main.cpp" <<'CPP'
#include "read.hpp"
int main() {
    Node node = {};
    return Write(&node, 20) == 41 && Read(&node) == 41 &&
           WriteForward(&node, 23) == 23 && node.value == 23 &&
           node.child.value == 21 ? 0 : 1;
}
CPP
    "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output/cpp" \
        "$output/cpp/types.cpp" "$output/cpp/read.cpp" "$output/cpp/main.cpp" \
        -o "$output/cpp/test"
    env -u DISPLAY -u WAYLAND_DISPLAY "$output/cpp/test"

    "$bin/zi2go" --no-main --strict --root "$root" -o "$output/go" "$file"
    cat > "$output/go/read_test.go" <<'GO'
package ziran
import "testing"
func TestPointerMember(t *testing.T) {
    node := Node{}
    if Read_Write(&node, 20) != 41 || Read_Read(&node) != 41 ||
       Read_WriteForward(&node, 23) != 23 || node.Value != 23 ||
       node.Child.Value != 21 {
        t.Fatal("pointer member read/write changed")
    }
}
GO
    if ! GO111MODULE=off go test "$output/go"/*.go; then
        sed -n '35,65p' "$output/go/read.go" >&2
        exit 1
    fi
done

if "$bin/zi2zib" bundle --root "$work" --entry read:Write \
    -o "$work/read.zib" "$work/read.zi" 2>"$work/bundle.err"; then
    echo "native pointer entered portable bundle" >&2
    exit 1
fi

sed 's/node->value/node->missing/' "$work/read.zi" > "$work/bad.zi"
if "$compiler" --check-only --root "$work" "$work/bad.zi" \
    >"$work/out" 2>"$work/err"; then
    echo "unknown pointer field unexpectedly passed checking" >&2
    exit 1
fi
grep -q 'unknown record field: missing' "$work/err"

sed 's/const Node\*/Node/' "$work/read.zi" > "$work/not_pointer.zi"
if "$compiler" --check-only --root "$work" "$work/not_pointer.zi" \
    >"$work/out" 2>"$work/err"; then
    echo "non-pointer field access unexpectedly passed checking" >&2
    exit 1
fi
grep -q 'pointer member requires a record pointer: Node' "$work/err"

echo "pointer member checking and emission test passed"
