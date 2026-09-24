#!/bin/sh
set -eu

ziran=$1
host_test=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/host_buffer.zi" <<'ZI'
host_api :: #system_library "host_api";

Fill :: (values: []u8) -> s32 #foreign host_api;
ReplaceWord :: (values: []string) -> s32 #foreign host_api;

#program_export
Answer :: () -> s32 {
    values: [4]u8 = .[9, 1, 2, 9]
    empty: []u8 = values[:0]
    if Fill(empty) != 0 { return -1 }
    middle: []u8 = values[1:3]
    if Fill(middle) != 2 || values[0] != 9 ||
        values[1] != 40 || values[2] != 2 || values[3] != 9 {
        return -2
    }
    words: [2]string = .["old", "second"]
    if ReplaceWord(words[:]) != 2 || words[0] != "new" ||
        words[1] != "second" { return -3 }
    return cast(s32)values[1] + cast(s32)values[2]
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/host_buffer.zi"
for input in source saved; do
    if test "$input" = source; then
        source=$work/host_buffer.zi
        root=$work
    else
        source=$work/ir/host_buffer.zir
        root=$work/ir
    fi
    "$ziran" bundle --root "$root" --entry host_buffer:Answer \
        -o "$work/$input.zib" "$source"
    "$host_test" "$work/$input.zib"
    for target in c cpp go; do
        output=$work/$input-$target
        "$ziran" build --target="$target" --strict --root "$root" \
            -o "$output" "$source"
        if test "$target" = c; then
            cat > "$output/main.c" <<'C'
#include "host_buffer.h"
int32_t Fill(Slice values) {
    if(values.length == 0) return 0;
    ((uint8_t *)values.data)[0] = 40;
    return values.length;
}
int32_t ReplaceWord(Slice values) {
    ((String *)values.data)[0] = StringView("new", 3);
    return values.length;
}
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$output" -I"$(dirname "$ziran")/../../include" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        elif test "$target" = cpp; then
            cat > "$output/main.cpp" <<'CPP'
#include "host_buffer.hpp"
extern "C" int32_t Fill(Slice values) {
    if(values.length == 0) return 0;
    static_cast<uint8_t *>(values.data)[0] = 40;
    return values.length;
}
extern "C" int32_t ReplaceWord(Slice values) {
    static_cast<String *>(values.data)[0] = StringView("new", 3);
    return values.length;
}
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$output" \
                -I"$(dirname "$ziran")/../../include" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        else
            cat > "$output/host_buffer_test.go" <<'GO'
package ziran
import "testing"
type bufferHost struct{}
func (bufferHost) Fill(values []uint8) int32 {
    if len(values) == 0 { return 0 }
    values[0] = 40
    return int32(len(values))
}
func (bufferHost) ReplaceWord(values []string) int32 {
    values[0] = "new"
    return int32(len(values))
}
func TestHostSlice(t *testing.T) {
    SetHostBufferHost(bufferHost{})
    if HostBuffer_Answer() != 42 { t.Fatal("host slice") }
}
GO
            GO111MODULE=off go test "$output"/*.go
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"

cat > "$work/bad_return.zi" <<'ZI'
host_api :: #system_library "host_api";
Borrow :: () -> []u8 #foreign host_api;
#program_export
Answer :: () -> s32 {
    return Borrow().length
}
ZI
if "$ziran" check --root "$work" "$work/bad_return.zi" \
    2> "$work/bad_return.err"; then
    echo 'host slice return was accepted' >&2
    exit 1
fi
grep -Fq 'host calls cannot return borrowed slices' "$work/bad_return.err"

cat > "$work/overlap.zi" <<'ZI'
host_api :: #system_library "host_api";
Touch :: (left: []u8, right: []u8) -> s32 #foreign host_api;
#program_export
Answer :: () -> s32 {
    values: [3]u8 = .[1, 2, 3]
    return Touch(values[:], values[1:])
}
ZI
"$ziran" bundle --root "$work" --entry overlap:Answer \
    -o "$work/overlap.zib" "$work/overlap.zi"
"$host_test" "$work/overlap.zib" overlap
