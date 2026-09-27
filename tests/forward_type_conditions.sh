#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/loaded_flags.zi" <<'ZI'
LoadedMask :: enum_flags u16 { LoadedOne; LoadedTwo; }
using LoadedMask;
ZI

cat > "$work/forward_type.zi" <<'ZI'
#if size_of(Later) == 16 && Red == 1 {
Selected :: 1;
} else {
Selected :: MissingValue;
}
#if size_of(LaterMask) == 4 && MaskB == 2 && MaskC == 16 {
FlagsSelected :: 1;
} else {
FlagsSelected :: MissingFlags;
}
#if size_of(SpecifiedMask) == 1 && BitFour == 4 {
SpecifiedSelected :: 1;
} else {
SpecifiedSelected :: MissingSpecified;
}
#if size_of(LoadedMask) == 2 && LoadedTwo == 2 {
LoadedSelected :: 1;
} else {
LoadedSelected :: MissingLoaded;
}
#if size_of(Box(s64)) == 8 {
GenericSelected :: 1;
} else {
GenericSelected :: MissingGeneric;
}
#if size_of(LaterBox) == 8 {
AliasSelected :: 1;
} else {
AliasSelected :: MissingAlias;
}
RUN :: #run size_of(Later);
FlagsRun :: #run size_of(LaterMask);
SpecifiedRun :: #run size_of(SpecifiedMask);
LoadedRun :: #run size_of(LoadedMask);
AliasRun :: #run size_of(LaterBox);
Later :: struct {
    first: int;
    second: s64;
}
Color :: enum { Red :: 1; }
using Color;
LaterMask :: enum_flags u32 { MaskA; MaskB; MaskC :: 16; }
using LaterMask;
SpecifiedMask :: enum_flags u8 #specified { BitOne :: 1; BitFour :: 4; }
using SpecifiedMask;
#load "loaded_flags.zi";
LaterBox :: Box(s64)
Box :: struct($T: Type) { value: T; }
#program_export
Answer :: () -> s64 {
    return Selected + RUN + FlagsSelected + FlagsRun +
           SpecifiedSelected + SpecifiedRun +
           LoadedSelected + LoadedRun +
           GenericSelected + AliasSelected + AliasRun + 5
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/forward_type.zi"
if grep -aFq 'MissingValue' "$work/ir/forward_type.zir"; then
    echo 'unselected forward-type branch was saved in IR' >&2
    exit 1
fi
if grep -aFq 'MissingFlags' "$work/ir/forward_type.zir"; then
    echo 'unselected forward enum_flags branch was saved in IR' >&2
    exit 1
fi
if grep -aFq 'MissingSpecified' "$work/ir/forward_type.zir"; then
    echo 'unselected forward specified enum_flags branch was saved in IR' >&2
    exit 1
fi
if grep -aFq 'MissingLoaded' "$work/ir/forward_type.zir"; then
    echo 'unselected forward loaded enum_flags branch was saved in IR' >&2
    exit 1
fi
if grep -aFq 'MissingGeneric' "$work/ir/forward_type.zir"; then
    echo 'unselected forward generic branch was saved in IR' >&2
    exit 1
fi
if grep -aFq 'MissingAlias' "$work/ir/forward_type.zir"; then
    echo 'unselected forward generic alias branch was saved in IR' >&2
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

cat > "$work/inactive_flags.zi" <<'ZI'
#if false {
HiddenMask :: enum_flags u8 { HiddenBit; }
}
#if size_of(HiddenMask) == 1 {
Answer :: () -> s64 { return 42 }
}
ZI
if "$ziran" check --root "$work" "$work/inactive_flags.zi" \
    2> "$work/inactive_flags.err"; then
    echo 'enum_flags type in inactive branch became visible' >&2
    exit 1
fi
grep -Fq 'size_of requires a known sized type' "$work/inactive_flags.err"

cat > "$work/private_flags.zi" <<'ZI'
#scope_file
PrivateMask :: enum_flags u8 { PrivateBit; }
using PrivateMask;
ZI
cat > "$work/private_flags_top.zi" <<'ZI'
#if size_of(PrivateMask) == 1 {
Answer :: () -> s64 { return 42 }
}
#load "private_flags.zi";
ZI
if "$ziran" check --root "$work" "$work/private_flags_top.zi" \
    2> "$work/private_flags_top.err"; then
    echo 'file-private loaded enum_flags type became visible' >&2
    exit 1
fi
grep -Fq 'size_of requires a known sized type' \
    "$work/private_flags_top.err"

cat > "$work/inactive_alias.zi" <<'ZI'
#if false {
HiddenBox :: Box(s64)
}
#if size_of(HiddenBox) == 8 {
Answer :: () -> s64 { return 42 }
}
Box :: struct($T: Type) { value: T; }
ZI
if "$ziran" check --root "$work" "$work/inactive_alias.zi" \
    2> "$work/inactive_alias.err"; then
    echo 'generic alias in inactive branch became visible' >&2
    exit 1
fi
grep -Fq 'size_of requires a known sized type' "$work/inactive_alias.err"

cat > "$work/private_alias.zi" <<'ZI'
#scope_file
PrivateBox :: Box(s64)
ZI
cat > "$work/private_alias_top.zi" <<'ZI'
#if size_of(PrivateBox) == 8 {
Answer :: () -> s64 { return 42 }
}
#load "private_alias.zi";
Box :: struct($T: Type) { value: T; }
ZI
if "$ziran" check --root "$work" "$work/private_alias_top.zi" \
    2> "$work/private_alias_top.err"; then
    echo 'file-private loaded generic alias became visible' >&2
    exit 1
fi
grep -Fq 'size_of requires a known sized type' \
    "$work/private_alias_top.err"

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
