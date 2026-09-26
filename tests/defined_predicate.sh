#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/predicate.zi" <<'EOF'
#if #defined(PLATFORM_WEB) || #defined(ANDROID_BUILD) {
PlatformPick :: 20
} else {
PlatformPick :: 2
}

PRESENT :: true;

#if #defined(PRESENT) {
ProbePick :: 100
} else {
ProbePick :: 200
}

#if #defined(NEVER_DECLARED) {
MissingPick :: MissingValue()
} else {
MissingPick :: 7
}

WEB :: #defined(PLATFORM_WEB);

#if WEB {
WebPick :: 1000
} else {
WebPick :: 3
}

#program_export
Answer :: () -> s32 {
    result: s32 = PlatformPick + ProbePick + MissingPick + WebPick
    #if !#defined(PLATFORM_WEB) && !#defined(ANDROID_BUILD) {
        result += 1
    } else {
        result += 1000
    }
    return result
}
EOF

"$ziran" check --root "$work" "$work/predicate.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/predicate.zi"
if grep -aFq 'MissingValue' "$work/ir/predicate.zir"; then
    echo 'undefined probe selected its branch' >&2
    exit 1
fi
"$ziran" bundle --root "$work" --entry predicate:Answer \
    -o "$work/source.zib" "$work/predicate.zi"
"$ziran" bundle --root "$work/ir" --entry predicate:Answer \
    -o "$work/saved.zib" "$work/ir/predicate.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 113
test "$("$ziran" run "$work/saved.zib")" = 113

"$ziran" build --target=c --root "$work" \
    -o "$work/c" "$work/predicate.zi"
cat > "$work/c/main.c" <<'EOF'
#include "predicate.h"
int main(void) { return Answer() == 113 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/predicate.c" \
    "$work/c/main.c" -o "$work/c/app"
"$work/c/app"
