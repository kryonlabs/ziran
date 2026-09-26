#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/forward_type.zi" <<'ZI'
#if size_of(Later) == 16 && Red == 1 {
Selected :: 1;
} else {
Selected :: MissingValue;
}
RUN :: #run size_of(Later);
Later :: struct {
    first: int;
    second: s64;
}
Color :: enum { Red :: 1; }
using Color;
#program_export
Answer :: () -> s64 { return Selected + RUN + 25 }
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/forward_type.zi"
if grep -aFq 'MissingValue' "$work/ir/forward_type.zir"; then
    echo 'unselected forward-type branch was saved in IR' >&2
    exit 1
fi
for input in "$work/forward_type.zi" "$work/ir/forward_type.zir"; do
    case "$input" in
        *.zi) root=$work; label=source ;;
        *.zir) root=$work/ir; label=saved ;;
    esac
    "$ziran" bundle --root "$root" --entry forward_type:Answer \
        -o "$work/$label.zib" "$input"
    test "$("$ziran" run "$work/$label.zib")" = 42
    for target in c cpp go; do
        out="$work/$label-$target"
        "$ziran" build "--target=$target" --root "$root" -o "$out" "$input"
        case "$target" in
            c)
                printf '#include "forward_type.h"\nint main(void) { return Answer() == 42 ? 0 : 1; }\n' > "$work/main.c"
                ${CC:-cc} -Iinclude -I"$out" "$out"/*.c \
                    "$work/main.c" -o "$out/app"
                "$out/app"
                ;;
            cpp)
                printf '#include "forward_type.hpp"\nint main() { return Answer() == 42 ? 0 : 1; }\n' > "$work/main.cpp"
                ${CXX:-c++} -Iinclude -I"$out" "$out"/*.cpp \
                    "$work/main.cpp" -o "$out/app"
                "$out/app"
                ;;
            go)
                cat > "$out/forward_type_test.go" <<'GO'
package ziran
import "testing"
func TestForwardType(t *testing.T) {
    if ForwardType_Answer() != 42 { t.Fatal("forward type") }
}
GO
                GO111MODULE=off go test "$out"/*.go
                ;;
        esac
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/inactive_type.zi" <<'ZI'
#if false {
Hidden :: struct { value: s64; }
}
#if size_of(Hidden) == 8 {
Answer :: () -> s64 { return 42 }
}
ZI
if "$ziran" check --root "$work" "$work/inactive_type.zi" \
    2> "$work/inactive_type.err"; then
    echo 'type in inactive branch became visible' >&2
    exit 1
fi
grep -Fq 'size_of requires a known sized type' "$work/inactive_type.err"

cat > "$work/inactive_field.zi" <<'ZI'
Later :: struct {
    #if false {
        bad: i64;
    } else {
        good: s64;
    }
}
#if size_of(Later) == 8 {
Selected :: 42;
} else {
Selected :: 0;
}
#program_export
Answer :: () -> s64 { return Selected }
ZI
"$ziran" check --root "$work" "$work/inactive_field.zi"
