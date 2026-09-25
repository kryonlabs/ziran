#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/lib.zi" <<'ZI'
Captured :: (loc := #caller_location) -> Source_Code_Location {
    return loc
}
Typed :: (loc: Source_Code_Location = #caller_location) -> Source_Code_Location {
    return loc
}
Generic :: (value: $T, loc := #caller_location) -> s64 {
    return loc.line_number
}
ZI
cat > "$work/extra.zi" <<'ZI'
Loaded :: () -> bool {
    if Captured().line_number != #line { return false }
    if Captured().fully_pathed_filename != #file { return false }
    return true
}
ZI
cat > "$work/app.zi" <<'ZI'
#import "lib"
Library :: #import "lib";
#load "extra.zi";
#program_export
Answer :: () -> s32 {
    if Captured().line_number != #line { return 0 }
    if Captured().fully_pathed_filename != #file { return 0 }
    if Typed().line_number != #line { return 0 }
    if Library.Captured().line_number != #line { return 0 }
    value: s32 = 42
    if Generic(value) != #line { return 0 }
    if !Loaded() { return 0 }
    provided := Source_Code_Location.{.fully_pathed_filename = "manual",
                                      .line_number = 77}
    if Captured(loc = provided).line_number != 77 { return 0 }
    if Captured(loc = provided).fully_pathed_filename != "manual" { return 0 }
    continued: bool =
        Captured().line_number == #line
    if !continued { return 0 }
    return 42
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
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$root" \
                -o "$out" "$input"
            cat > "$out/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("caller location") } }
GO
            GO111MODULE=off go run "$out"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$root" -o "$out" "$input"
            cat > "$out/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                "$out"/*.c -o "$out/app"
            "$out/app"
        else
            "$ziran" build --target=cpp --root "$root" -o "$out" "$input"
            cat > "$out/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$out" \
                "$out"/*.cpp -o "$out/app"
            "$out/app"
        fi
    done
done

cat > "$work/direct.zi" <<'ZI'
Answer :: () -> Source_Code_Location { return #caller_location }
ZI
if "$ziran" check --root "$work" "$work/direct.zi" 2> "$work/direct.err"; then
    echo '#caller_location outside a parameter default was accepted' >&2
    exit 1
fi
grep -Fq '#caller_location is only valid as a parameter default' \
    "$work/direct.err"
cat > "$work/file_scope.zi" <<'ZI'
HERE :: #caller_location;
ZI
if "$ziran" check --root "$work" "$work/file_scope.zi" \
    2> "$work/file_scope.err"; then
    echo '#caller_location at file scope was accepted' >&2
    exit 1
fi
grep -Fq '#caller_location is only valid as a parameter default' \
    "$work/file_scope.err"

cat > "$work/wrong_type.zi" <<'ZI'
Bad :: (loc: s32 = #caller_location) -> s32 { return loc }
ZI
if "$ziran" check --root "$work" "$work/wrong_type.zi" \
    2> "$work/wrong_type.err"; then
    echo '#caller_location with a wrong parameter type was accepted' >&2
    exit 1
fi
grep -Fq '#caller_location requires a Source_Code_Location parameter' \
    "$work/wrong_type.err"

cat > "$work/shadow_type.zi" <<'ZI'
Source_Code_Location :: struct { value: s32; }
ZI
if "$ziran" check --root "$work" "$work/shadow_type.zi" \
    2> "$work/shadow_type.err"; then
    echo 'built-in Source_Code_Location was redeclared' >&2
    exit 1
fi
grep -Fq 'built-in type cannot be redeclared' "$work/shadow_type.err"
