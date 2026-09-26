#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/library.zi" <<'ZI'
Point :: struct { value: s64; }
Wrapper :: struct { point: Point; text: string; }
Make :: () -> Wrapper {
    return Wrapper.{.point = Point.{.value = 42}, .text = "ready"}
}
Read :: (point: Point) -> s64 { return point.value }
WRAPPER :: #run Make();
NUMBERS :: #run s64.[41, 42];
RECORDS :: #run Point.[Point.{.value = 41}, Point.{.value = 42}];
#scope_file
Hidden :: #run Make();
ZI
cat > "$work/app.zi" <<'ZI'
Lib :: #import "library";
saved: Lib.Wrapper = Lib.WRAPPER;
numbers: [2]s64 = Lib.NUMBERS;
records: [2]Lib.Point = Lib.RECORDS;
selected_point: Lib.Point = Lib.WRAPPER.point;
selected_record: Lib.Point = Lib.RECORDS[1];
selected_number: s64 = Lib.WRAPPER.point.value + Lib.NUMBERS[1] - 42;
selected_choice: Lib.Point = #ifx true then Lib.RECORDS[1] else Lib.RECORDS[0];
DERIVED :: #run Lib.WRAPPER.point.value;
ARRAY_DERIVED :: #run Lib.RECORDS[1].value;
#assert Lib.WRAPPER.point.value == 42
#assert Lib.WRAPPER.text == "ready"
#assert Lib.NUMBERS.count == 2
#assert Lib.RECORDS[1].value == 42
#assert DERIVED == 42 && ARRAY_DERIVED == 42
#if Lib.WRAPPER.point.value == 42 && Lib.RECORDS[1].value == 42 {
SELECTED :: 42;
} else {
SELECTED :: Missing();
}
#program_export
Answer :: () -> s64 {
    local: Lib.Wrapper = Lib.WRAPPER;
    via_call: Lib.Wrapper = Lib.Make();
    if local.text != "ready" || via_call.point.value != 42 || Lib.Read(via_call.point) != 42 || saved.text != "ready" || selected_point.value != 42 || selected_record.value != 42 || selected_number != 42 || selected_choice.value != 42 { return 0 }
    return Lib.WRAPPER.point.value + local.point.value + saved.point.value + Lib.NUMBERS[1] + numbers[1] + Lib.RECORDS[1].value + records[1].value + SELECTED - 294
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/app.zi"
for input in source saved; do
    if test "$input" = source; then
        root=$work
        file=$work/app.zi
    else
        root=$work/ir
        file=$work/ir/app.zir
    fi
    "$ziran" bundle --root "$root" --entry app:Answer \
        -o "$work/$input.zib" "$file"
    test "$("$ziran" run "$work/$input.zib")" = 42
    for target in c cpp go; do
        out=$work/$input-$target
        case "$target" in
            c)
                "$ziran" build --target=c --root "$root" -o "$out" "$file"
                cat > "$out/main.c" <<'C'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
                "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                    "$out"/*.c -o "$out/app"
                "$out/app"
                ;;
            cpp)
                "$ziran" build --target=cpp --root "$root" -o "$out" "$file"
                cat > "$out/main.cpp" <<'CPP'
#include "app.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
                "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$out" \
                    "$out"/*.cpp -o "$out/app"
                "$out/app"
                ;;
            go)
                "$ziran" build --target=go --pkg main --root "$root" \
                    -o "$out" "$file"
                cat > "$out/main.go" <<'GO'
package main
func main() { if App_Answer() != 42 { panic("imported aggregate") } }
GO
                GO111MODULE=off go run "$out"/*.go
                ;;
        esac
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/open_case.zi" <<'ZI'
#import "library";
point: Point = WRAPPER.point;
#program_export
Answer :: () -> s64 { return WRAPPER.point.value + NUMBERS[1] + point.value - 84 }
ZI
cat > "$work/using_case.zi" <<'ZI'
using Lib :: #import "library";
point: Point = WRAPPER.point;
#program_export
Answer :: () -> s64 { return WRAPPER.point.value + Lib.NUMBERS[1] + point.value - 84 }
ZI
cat > "$work/forward_case.zi" <<'ZI'
#if Lib.WRAPPER.point.value == 42 && Lib.RECORDS[1].value == 42 {
SELECTED :: 42;
} else {
SELECTED :: Missing();
}
Lib :: #import "library";
#program_export
Answer :: () -> s64 { return SELECTED }
ZI
for name in open_case using_case forward_case; do
    case "$name" in
        open_case) go_entry=OpenCase_Answer ;;
        using_case) go_entry=UsingCase_Answer ;;
        forward_case) go_entry=ForwardCase_Answer ;;
    esac
    "$ziran" ir --root "$work" -o "$work/ir" "$work/$name.zi"
    for input in "$work/$name.zi" "$work/ir/$name.zir"; do
        case "$input" in
            *.zi) root=$work ;;
            *.zir) root=$work/ir ;;
        esac
        "$ziran" bundle --root "$root" --entry "$name:Answer" \
            -o "$work/$name.zib" "$input"
        test "$("$ziran" run "$work/$name.zib")" = 42
        for target in c cpp go; do
            out=$work/$name-$target-$(basename "$root")
            case "$target" in
                c)
                    "$ziran" build --target=c --root "$root" \
                        -o "$out" "$input"
                    cat > "$out/main.c" <<C
#include "$name.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
                    "${CC:-cc}" -std=c11 -I"$repo/include" -I"$out" \
                        "$out"/*.c -o "$out/app"
                    "$out/app"
                    ;;
                cpp)
                    "$ziran" build --target=cpp --root "$root" \
                        -o "$out" "$input"
                    cat > "$out/main.cpp" <<CPP
#include "$name.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
                    "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$out" \
                        "$out"/*.cpp -o "$out/app"
                    "$out/app"
                    ;;
                go)
                    "$ziran" build --target=go --pkg main --root "$root" \
                        -o "$out" "$input"
                    cat > "$out/main.go" <<GO
package main
func main() { if $go_entry() != 42 { panic("imported aggregate") } }
GO
                    GO111MODULE=off go run "$out"/*.go
                    ;;
            esac
        done
    done
done

cat > "$work/private.zi" <<'ZI'
Lib :: #import "library";
Bad :: () -> s64 { return Lib.Hidden.point.value }
ZI
if "$ziran" check --root "$work" "$work/private.zi" \
    2> "$work/private.err"; then
    echo 'private imported aggregate became visible' >&2
    exit 1
fi
rg -q 'invalid or ambiguous constant: Lib.Hidden|unresolved name: Lib.Hidden' \
    "$work/private.err"

cat > "$work/shadow.zi" <<'ZI'
Lib :: #import "library";
Point :: struct { decoy: s64; }
Bad :: () -> s64 { return Lib.WRAPPER.point.value }
ZI
if "$ziran" check --root "$work" "$work/shadow.zi" \
    2> "$work/shadow.err"; then
    echo 'shadowed imported aggregate type was silently accepted' >&2
    exit 1
fi
rg -q 'cannot bind aggregate constant|imported record field type is shadowed' \
    "$work/shadow.err"

cat > "$work/call_shadow.zi" <<'ZI'
Lib :: #import "library";
Wrapper :: struct { decoy: s64; }
Bad :: () -> s64 { return Lib.Make().point.value }
ZI
if "$ziran" check --root "$work" "$work/call_shadow.zi" \
    2> "$work/call_shadow.err"; then
    echo 'shadowed imported procedure result type was silently accepted' >&2
    exit 1
fi
grep -Fq 'imported procedure result type is shadowed' \
    "$work/call_shadow.err"

cat > "$work/value_shadow.zi" <<'ZI'
#import "library";
WRAPPER: Wrapper = Wrapper.{.point = Point.{.value = 7}, .text = "local"};
selected: s64 = WRAPPER.point.value;
#program_export
Answer :: () -> s64 { return selected }
ZI
if "$ziran" bundle --root "$work" --entry value_shadow:Answer \
    -o "$work/value_shadow.zib" "$work/value_shadow.zi" \
    2> "$work/value_shadow.err"; then
    echo 'open-import constant displaced a local global' >&2
    exit 1
fi
rg -q 'portable global initializers need' "$work/value_shadow.err"
