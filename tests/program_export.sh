#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/exported.zi" <<'ZI'
#program_export "native_answer" Answer :: () -> s32 {
    return Helper()
}
#program_export
Helper :: () -> s32 { return 42 }
#program_export "standalone_answer"
Standalone :: () -> s32 { return Answer() }
ZI
"$ziran" check --root "$work" "$work/exported.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/exported.zi"
"$ziran" check --root "$work/ir" "$work/ir/exported.zir"
"$ziran" bundle --root "$work" --entry exported:Answer \
    -o "$work/source.zib" "$work/exported.zi"
"$ziran" bundle --root "$work/ir" --entry exported:Answer \
    -o "$work/saved.zib" "$work/ir/exported.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/saved.zib")" = 42

cat > "$work/main.c" <<'C'
#include "exported.h"
int main(void) {
    return native_answer() == 42 && standalone_answer() == 42 &&
           Helper() == 42 ? 0 : 1;
}
C
cat > "$work/main.cpp" <<'CPP'
#include "exported.hpp"
int main() {
    return native_answer() == 42 && standalone_answer() == 42 &&
           Helper() == 42 ? 0 : 1;
}
CPP
for target in c cpp; do
    for input in source saved; do
        if test "$input" = source; then
            root=$work
            file=$work/exported.zi
        else
            root=$work/ir
            file=$work/ir/exported.zir
        fi
        out="$work/$target-$input"
        "$ziran" build "--target=$target" --root "$root" -o "$out" "$file"
        if test "$target" = c; then
            ${CC:-cc} -Iinclude -I"$out" "$out/exported.c" \
                "$work/main.c" -o "$out/main"
        else
            ${CXX:-c++} -Iinclude -I"$out" "$out/exported.cpp" \
                "$work/main.cpp" -o "$out/main"
        fi
        "$out/main"
    done
done

cat > "$work/plain.zi" <<'ZI'
#program_export Plain :: () -> s32 { return 42 }
ZI
"$ziran" ir --root "$work" -o "$work/plain-ir" "$work/plain.zi"
for input in "$work/plain.zi" "$work/plain-ir/plain.zir"; do
    "$ziran" build --target=go --root "$work" -o "$work/plain-go" "$input"
done

if "$ziran" build --target=go --root "$work" \
    -o "$work/alias-go" "$work/exported.zi" 2> "$work/alias-go.err"; then
    echo 'Go silently accepted a quoted linker name' >&2
    exit 1
fi
grep -Fq 'quoted #program_export symbol requires the C or C++ target' \
    "$work/alias-go.err"

cat > "$work/invalid.zi" <<'ZI'
#program_export "bad-name" Broken :: () -> s32 { return 42 }
ZI
if "$ziran" check --root "$work" "$work/invalid.zi" \
    2> "$work/invalid.err"; then
    echo 'invalid linker symbol was accepted' >&2
    exit 1
fi
grep -Fq '#program_export symbol must be an identifier' "$work/invalid.err"
