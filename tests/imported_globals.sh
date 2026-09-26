#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Point :: struct { x: s64; y: s64; }
shared: Point;
Start :: 2;
count: s64 = Start;
ZI
cat > "$work/app.zi" <<'ZI'
#import "lib"
Lib :: #import "lib"
using shared;
#program_export
Answer :: () -> s64 {
    x = 38
    Lib.count = Lib.count + 1
    count = count + 1
    return x + count
}
ZI

"$ziran" check --root "$work" "$work/app.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/app.zi"
"$ziran" bundle --root "$work" --entry app:Answer \
    -o "$work/source.zib" "$work/app.zi"
"$ziran" bundle --root "$work/ir" --entry app:Answer \
    -o "$work/saved.zib" "$work/ir/app.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42

for input in "$work/app.zi" "$work/ir/app.zir"; do
    case "$input" in
        *.zi) suffix=source; root=$work ;;
        *) suffix=saved; root=$work/ir ;;
    esac
    for target in c cpp go; do
        out="$work/$target-$suffix"
        "$ziran" build "--target=$target" --root "$root" -o "$out" "$input"
        if test "$target" = go; then
            cat > "$out/app_test.go" <<'GO'
package ziran
import "testing"
func TestImportedGlobals(t *testing.T) {
    if App_Answer() != 42 { t.Fatal("imported globals") }
}
GO
            GO111MODULE=off go test "$out"/*.go
        elif test "$target" = c; then
            cat > "$out/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -Wall -Werror -I"$repo/include" \
                -I"$out" "$out"/*.c -o "$out/app"
            "$out/app"
        else
            cat > "$out/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -Wall -Werror -I"$repo/include" \
                -I"$out" "$out"/*.cpp -o "$out/app"
            "$out/app"
        fi
    done
done

cat > "$work/named_using.zi" <<'ZI'
Lib :: #import "lib"
using Lib.shared;
#program_export
Answer :: () -> s64 { y = 42; return y }
ZI
"$ziran" check --root "$work" "$work/named_using.zi"
"$ziran" ir --root "$work" -o "$work/named-ir" "$work/named_using.zi"
for input in "$work/named_using.zi" "$work/named-ir/named_using.zir"; do
    case "$input" in
        *.zi) root=$work; suffix=source ;;
        *) root=$work/named-ir; suffix=saved ;;
    esac
    "$ziran" bundle --root "$root" --entry named_using:Answer \
        -o "$work/named.zib" "$input"
    test "$("$ziran" run "$work/named.zib")" = 42
    for target in c cpp go; do
        out="$work/named-$target-$suffix"
        "$ziran" build "--target=$target" --root "$root" -o "$out" "$input"
        if test "$target" = go; then
            cat > "$out/named_using_test.go" <<'GO'
package ziran
import "testing"
func TestNamedUsing(t *testing.T) {
    if NamedUsing_Answer() != 42 { t.Fatal("named global using") }
}
GO
            GO111MODULE=off go test "$out"/*.go
        elif test "$target" = c; then
            cat > "$out/main.c" <<'C'
#include "named_using.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -Wall -Werror -I"$repo/include" \
                -I"$out" "$out"/*.c -o "$out/app"
            "$out/app"
        else
            cat > "$out/main.cpp" <<'CPP'
#include "named_using.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -Wall -Werror -I"$repo/include" \
                -I"$out" "$out"/*.cpp -o "$out/app"
            "$out/app"
        fi
    done
done

cat > "$work/other.zi" <<'ZI'
count: s64;
ZI
cat > "$work/ambiguous.zi" <<'ZI'
#import "lib"
#import "other"
Bad :: () -> s64 { return count }
ZI
if "$ziran" check --root "$work" "$work/ambiguous.zi" \
    2> "$work/ambiguous.err"; then
    echo 'ambiguous imported global was accepted' >&2
    exit 1
fi
grep -Fq 'ambiguous global name: count' "$work/ambiguous.err"

cat > "$work/ambiguous_using.zi" <<'ZI'
#import "lib"
#import "other"
using count;
ZI
if "$ziran" check --root "$work" "$work/ambiguous_using.zi" \
    2> "$work/ambiguous_using.err"; then
    echo 'ambiguous imported using root was accepted' >&2
    exit 1
fi
grep -Fq 'ambiguous global name: count' "$work/ambiguous_using.err"

cat > "$work/type_shadow.zi" <<'ZI'
#import "lib"
Point :: struct { z: s64; }
Bad :: () -> s64 { return shared.x }
ZI
if "$ziran" check --root "$work" "$work/type_shadow.zi" \
    2> "$work/type_shadow.err"; then
    echo 'shadowed imported global type was silently accepted' >&2
    exit 1
fi
grep -Fq 'imported global type is shadowed: shared' "$work/type_shadow.err"

cat > "$work/named_type_shadow.zi" <<'ZI'
Lib :: #import "lib"
Point :: struct { z: s64; }
using Lib.shared;
Bad :: () -> s64 { return x }
ZI
if "$ziran" check --root "$work" "$work/named_type_shadow.zi" \
    2> "$work/named_type_shadow.err"; then
    echo 'named imported global with shadowed type was accepted' >&2
    exit 1
fi
grep -Fq 'imported global type is shadowed: Lib.shared' \
    "$work/named_type_shadow.err"

cat > "$work/private.zi" <<'ZI'
Lib :: #import "private_lib"
Bad :: () -> s64 { return Lib.hidden }
ZI
cat > "$work/private_lib.zi" <<'ZI'
#scope_file
hidden: s64;
#scope_module
ZI
if "$ziran" check --root "$work" "$work/private.zi" \
    2> "$work/private.err"; then
    echo 'file-private imported global was accepted' >&2
    exit 1
fi
grep -Fq 'unresolved name: Lib.hidden' "$work/private.err"
