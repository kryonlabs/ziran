#!/bin/sh
set -eu

ziran=$1
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/file_type_of.zi" <<'ZI'
Pair :: struct { left: s32; right: s64; }
pair: Pair;
calls: s32;
Measure :: () -> s32 { calls += 1; return 40; }
GLOBAL_SIZE :: size_of(type_of(calls));
FIELD_SIZE :: size_of(type_of(pair.right));
CALL_SIZE :: size_of(type_of(Measure()));
RUN_SIZE :: #run size_of(type_of(Measure())) + 1;
SELECTED_IFX :: #ifx size_of(type_of(calls)) == 4 then 1 else 0;
#assert GLOBAL_SIZE == 4
#assert FIELD_SIZE == 8
#assert size_of(type_of(pair.right)) == 8
#if CALL_SIZE == 4 {
SELECTED :: 1;
} else {
SELECTED :: 0;
}
#program_export
Answer :: () -> s32 {
    if calls != 0 { return 0 }
    return cast(s32)(GLOBAL_SIZE + FIELD_SIZE + CALL_SIZE +
                     RUN_SIZE + SELECTED + SELECTED_IFX + 19)
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/file_type_of.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/file_type_of.zi
        root=$work
    else
        module=$work/ir/file_type_of.zir
        root=$work/ir
    fi
    "$ziran" bundle --root "$root" --entry file_type_of:Answer \
        -o "$work/$input.zib" "$module"
    test "$("$ziran" run "$work/$input.zib")" = 42
    for target in c cpp go; do
        output=$work/$target-$input
        "$ziran" build "--target=$target" --root "$root" \
            -o "$output" "$module"
        case "$target" in
            c)
                printf '#include "file_type_of.h"\nint main(void) { return Answer() == 42 ? 0 : 1; }\n' > "$work/main.c"
                "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                    "$output"/*.c "$work/main.c" -o "$output/app"
                "$output/app"
                ;;
            cpp)
                printf '#include "file_type_of.hpp"\nint main() { return Answer() == 42 ? 0 : 1; }\n' > "$work/main.cpp"
                "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                    "$output"/*.cpp "$work/main.cpp" -o "$output/app"
                "$output/app"
                ;;
            go)
                cat > "$output/file_type_of_test.go" <<'GO'
package ziran
import "testing"
func TestFileTypeOf(t *testing.T) {
    if FileTypeOf_Answer() != 42 { t.Fatal("file-scope type_of") }
}
GO
                GO111MODULE=off go test "$output"/*.go
                ;;
        esac
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/global_query.zi" <<'ZI'
pair: s64;
byte_size: int = size_of(type_of(pair));
#program_export
Answer :: () -> int { return byte_size }
ZI
"$ziran" ir --root "$work" -o "$work/global-ir" "$work/global_query.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/global_query.zi
        root=$work
    else
        module=$work/global-ir/global_query.zir
        root=$work/global-ir
    fi
    for target in c cpp go; do
        output=$work/global-$target-$input
        "$ziran" build "--target=$target" --root "$root" \
            -o "$output" "$module"
        case "$target" in
            c)
                printf '#include "global_query.h"\nint main(void) { return Answer() == 8 ? 0 : 1; }\n' > "$work/global-main.c"
                "${CC:-cc}" -std=c11 -I"$repo/include" -I"$output" \
                    "$output"/*.c "$work/global-main.c" -o "$output/app"
                "$output/app"
                ;;
            cpp)
                printf '#include "global_query.hpp"\nint main() { return Answer() == 8 ? 0 : 1; }\n' > "$work/global-main.cpp"
                "${CXX:-c++}" -std=c++17 -I"$repo/include" -I"$output" \
                    "$output"/*.cpp "$work/global-main.cpp" -o "$output/app"
                "$output/app"
                ;;
            go)
                cat > "$output/global_query_test.go" <<'GO'
package ziran
import "testing"
func TestGlobalQuery(t *testing.T) {
    if GlobalQuery_Answer() != 8 { t.Fatal("global size query") }
}
GO
                GO111MODULE=off go test "$output"/*.go
                ;;
        esac
    done
done

cat > "$work/unknown.zi" <<'ZI'
BAD :: size_of(type_of(MissingValue));
ZI
if "$ziran" check --root "$work" "$work/unknown.zi" \
    2> "$work/unknown.err"; then
    echo 'unknown file-scope type_of operand was accepted' >&2
    exit 1
fi
rg -q 'unknown|checked expression' "$work/unknown.err"
