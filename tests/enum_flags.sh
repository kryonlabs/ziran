#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/shared.zi" <<'EOF'
Shared :: enum { SharedFirst :: 5; }
EOF

cat > "$work/flags.zi" <<'EOF'
#import "shared"
Mask :: enum_flags u32
{
    A;
    B;
    C :: 16;
    D;
    Top :: 2147483648;
}
Small :: enum u8 { Low; High :: 200; }
Tiny :: enum_flags u8 { First; Last :: 128; }
Specified :: enum u16 #specified { Third :: 3; Zeroth :: 0; }
SpecifiedDefault :: enum #specified { Chosen :: 7; }
SpecifiedFlags :: enum_flags u8 #specified { FlagOne :: 1; FlagFour :: 4; }
Default :: enum { Wide :: 4294967296; Shifted :: 1 << 1 + 2; }
Prefixed :: enum { PrefixedFirst :: 1; PrefixedSecond :: 2; }
Pair :: struct {
    mask: Mask
    small: Small
    wide: Default
}
#assert size_of(Mask) == 4
Has :: (flags: Mask) -> bool { return (flags & .B) == .B }
IsHigh :: (small: Small) -> bool { return small == .High }
Pick :: () -> Small { return .Low }
Shadow :: () -> s32 {
    PrefixedFirst: s32 = 9
    return PrefixedFirst
}
#program_export
Answer :: () -> s32 {
    if PrefixedFirst != 1 || PrefixedSecond != 2 { return 0 }
    prefixed: Prefixed = Prefixed.PrefixedSecond
    if prefixed != Prefixed.PrefixedSecond { return 0 }
    if SharedFirst != 5 || Shadow() != 9 { return 0 }
    small: Small = .Low
    if small != Small.Low { return 0 }
    small = .High
    if !IsHigh(small) || !IsHigh(.High) { return 0 }
    small = Pick()
    if small != .Low { return 0 }
    tiny: Tiny = .Last
    if (tiny + .Last) != 0 { return 0 }
    tiny += .Last
    if tiny != 0 { return 0 }
    flags: Mask = .A | .B
    if flags != 3 { return 0 }
    if !Has(flags) { return 0 }
    if !Has(.B) { return 0 }
    flags |= Mask.C
    if flags != 19 || (flags & .B) != 2 { return 0 }
    flags ^= .A
    if flags != 18 { return 0 }
    flags += .D
    if flags != 50 { return 0 }
    flags = .Top
    if flags != Mask.Top { return 0 }
    flags = 1
    if flags != .A { return 0 }
    if size_of(Mask) != 4 { return 0 }
    if size_of(Small) != 1 { return 0 }
    if size_of(Specified) != 2 || size_of(SpecifiedDefault) != 8 ||
       size_of(SpecifiedFlags) != 1 { return 0 }
    if size_of(Default) != 8 { return 0 }
    if size_of(Pair) != 16 { return 0 }
    if A != 1 || B != 2 || C != 16 || D != 32 { return 0 }
    if Top != 2147483648 { return 0 }
    if (A | B) != 3 { return 0 }
    if High != 200 || Wide != 4294967296 || Shifted != 8 { return 0 }
    if Third != 3 || Zeroth != 0 || Chosen != 7 ||
       FlagOne != 1 || FlagFour != 4 { return 0 }
    return 42
}
EOF

"$ziran" check --root "$work" "$work/flags.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/flags.zi"
"$ziran" bundle --root "$work" --entry flags:Answer \
    -o "$work/source.zib" "$work/flags.zi"
"$ziran" bundle --root "$work/ir" --entry flags:Answer \
    -o "$work/saved.zib" "$work/ir/flags.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42

for input in "$work/flags.zi" "$work/ir/flags.zir"; do
    case "$input" in
        *.zi) suffix=source ;;
        *) suffix=saved ;;
    esac
    for target in c cpp go; do
        out="$work/$target-$suffix"
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$work" \
                -o "$out" "$input"
            cat > "$out/main.go" <<'EOF'
package main
import "unsafe"
func main() {
    if unsafe.Sizeof(Mask(0)) != 4 || unsafe.Sizeof(Small(0)) != 1 ||
       unsafe.Sizeof(Default(0)) != 8 || unsafe.Sizeof(Pair{}) != 16 {
        panic("enum layout differs from size_of")
    }
    if Flags_Answer() != 42 { panic("enum values") }
}
EOF
            GO111MODULE=off go run "$out/flags.go" "$out/main.go"
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$work" \
                -o "$out" "$input"
            cat > "$out/main.c" <<'EOF'
#include "flags.h"
_Static_assert(sizeof(Mask) == 4, "mask width");
_Static_assert(sizeof(Small) == 1, "small width");
_Static_assert(sizeof(Default) == 8, "default width");
_Static_assert(sizeof(Pair) == 16, "record layout");
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
            ${CC:-cc} -std=c11 -Iinclude -I"$out" "$out/flags.c" \
                "$out/main.c" -o "$out/app"
            "$out/app"
        else
            "$ziran" build --target=cpp --root "$work" \
                -o "$out" "$input"
            cat > "$out/main.cpp" <<'EOF'
#include "flags.hpp"
static_assert(sizeof(Mask) == 4, "mask width");
static_assert(sizeof(Small) == 1, "small width");
static_assert(sizeof(Default) == 8, "default width");
static_assert(sizeof(Pair) == 16, "record layout");
int main() { return Answer() == 42 ? 0 : 1; }
EOF
            ${CXX:-c++} -std=c++17 -Iinclude -I"$out" \
                "$out/flags.cpp" "$out/main.cpp" -o "$out/app"
            "$out/app"
        fi
    done
done

cat > "$work/too_wide.zi" <<'EOF'
Bad :: enum u8 { TooWide :: 256; }
EOF
if "$ziran" check --root "$work" "$work/too_wide.zi" \
    2> "$work/too_wide.err"; then
    echo 'enum member outside u8 backing type was accepted' >&2
    exit 1
fi
grep -Fq 'invalid enum value or value outside backing type' \
    "$work/too_wide.err"

cat > "$work/negative_unsigned.zi" <<'EOF'
Bad :: enum_flags u8 { Negative :: -1; }
EOF
if "$ziran" check --root "$work" "$work/negative_unsigned.zi" \
    2> "$work/negative_unsigned.err"; then
    echo 'negative unsigned flag was accepted' >&2
    exit 1
fi
grep -Fq 'invalid enum value or value outside backing type' \
    "$work/negative_unsigned.err"

cat > "$work/signed_minimum.zi" <<'EOF'
Minimum :: enum { Floor :: -9223372036854775808; }
#assert size_of(Minimum) == 8
EOF
"$ziran" check --root "$work" "$work/signed_minimum.zi"

cat > "$work/unspecified_member.zi" <<'EOF'
Bad :: enum u8 #specified { First :: 1; Second; }
EOF
if "$ziran" check --root "$work" "$work/unspecified_member.zi" \
    2> "$work/unspecified_member.err"; then
    echo '#specified enum accepted an implicit member' >&2
    exit 1
fi
grep -Fq '#specified enum requires an explicit value' \
    "$work/unspecified_member.err"

cat > "$work/unspecified_flag.zi" <<'EOF'
Bad :: enum_flags u8 #specified { First :: 1; Second; }
EOF
if "$ziran" check --root "$work" "$work/unspecified_flag.zi" \
    2> "$work/unspecified_flag.err"; then
    echo '#specified enum_flags accepted an implicit member' >&2
    exit 1
fi
grep -Fq '#specified enum requires an explicit value' \
    "$work/unspecified_flag.err"

cat > "$work/legacy_enum_value.zi" <<'EOF'
Bad :: enum u8 #specified { First = 1; }
EOF
if "$ziran" check --root "$work" "$work/legacy_enum_value.zi" \
    2> "$work/legacy_enum_value.err"; then
    echo 'C-style enum value assignment was accepted' >&2
    exit 1
fi
grep -Fq 'Member :: value syntax' "$work/legacy_enum_value.err"

cat > "$work/unknown_flag.zi" <<'EOF'
Mask :: enum_flags u8 { A; B; }
Bad :: () -> Mask { return .Missing }
EOF
if "$ziran" check --root "$work" "$work/unknown_flag.zi" \
    2> "$work/unknown_flag.err"; then
    echo 'unknown inferred flag member was accepted' >&2
    exit 1
fi
grep -Fq 'unknown enum member' "$work/unknown_flag.err"

cat > "$work/global_enum.zi" <<'ZI'
Mask :: enum u8 { A :: 1; B :: 2; }
flags: Mask = Mask.A;
#program_export
Answer :: () -> s32 { return cast(s32) flags }
ZI
"$ziran" ir --root "$work" -o "$work/global-enum-ir" \
    "$work/global_enum.zi"
for input in source saved; do
    if test "$input" = source; then
        file=$work/global_enum.zi
        root=$work
    else
        file=$work/global-enum-ir/global_enum.zir
        root=$work/global-enum-ir
    fi
    for target in c cpp go; do
        output=$work/global-enum-$input-$target
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --root "$root" \
                -o "$output" "$file"
            cat > "$output/entry.go" <<'GO'
package main
func main() { if GlobalEnum_Answer() != 1 { panic("enum global initializer") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --root "$root" -o "$output" "$file"
            cat > "$output/entry.c" <<'C'
#include "global_enum.h"
int main(void) { return Answer() == 1 ? 0 : 1; }
C
            "${CC:-cc}" -Iinclude -I"$output" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        else
            "$ziran" build --target=cpp --root "$root" -o "$output" "$file"
            cat > "$output/entry.cpp" <<'CPP'
#include "global_enum.hpp"
int main() { return Answer() == 1 ? 0 : 1; }
CPP
            "${CXX:-c++}" -Iinclude -I"$output" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        fi
    done
done
