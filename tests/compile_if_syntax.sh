#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/branches.zi" <<'EOF'
MODE :: 2;
#if MODE == 1 {
Value :: () -> s32 { return MissingValue() }
Unused :: struct {
    missing: MissingType
}
#if MISSING_COMPILE_CONSTANT {
AlsoUnused :: MissingType;
}
}
else #if MODE == 2 {
Value :: () -> s32 { return 39 }
} else #if MISSING_COMPILE_CONSTANT {
Value :: () -> s32 { return MissingValue() }
} else {
Value :: () -> s32 { return MissingValue() }
}
Choice :: struct {
    #if MODE == 2 {
        value: s32
    } else {
        missing: MissingType
    }
}
ChoiceEnum :: enum {
    #if MODE == 2 {
        Selected
    } else {
        Missing
    }
}
#program_export
Answer :: () -> s32 {
    using ChoiceEnum;
    result: s32 = Value()
    choice: Choice
    choice.value = 1
    result += choice.value
    if Selected != 0 { return 0 }
    #if MODE == 1 {
        result += MissingValue()
        #if MISSING_COMPILE_CONSTANT {
            result += MissingValue()
        }
    }
    else #if MODE == 2 {
        result += 2
        #if false {
            if true {
                result += MissingValue()
            }
        } else {
            result += 0
        }
    } else #if MISSING_COMPILE_CONSTANT {
        result += MissingValue()
    } else {
        result += MissingValue()
    }
    #if OS == .LINUX || OS != .LINUX {
        result += 0
    }
    return result
}
EOF

"$ziran" check --root "$work" "$work/branches.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/branches.zi"
if grep -aFq 'MissingValue' "$work/ir/branches.zir" ||
   grep -aFq 'MissingType' "$work/ir/branches.zir"; then
    echo 'unselected #if branch was saved in IR' >&2
    exit 1
fi
"$ziran" bundle --root "$work" --entry branches:Answer \
    -o "$work/source.zib" "$work/branches.zi"
"$ziran" bundle --root "$work/ir" --entry branches:Answer \
    -o "$work/saved.zib" "$work/ir/branches.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/source.zib")" = 42
test "$("$ziran" run "$work/saved.zib")" = 42
"$ziran" build --target=c --root "$work" \
    -o "$work/c" "$work/branches.zi"
cat > "$work/c/main.c" <<'EOF'
#include "branches.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/branches.c" \
    "$work/c/main.c" -o "$work/c/app"
"$work/c/app"

for target in cpp go; do
    out="$work/$target"
    if test "$target" = go; then
        "$ziran" build --target=go --pkg main --root "$work" \
            -o "$out" "$work/branches.zi"
        cat > "$out/main.go" <<'EOF'
package main
func main() { if Branches_Answer() != 42 { panic("wrong #if result") } }
EOF
        GO111MODULE=off go run "$out/branches.go" "$out/main.go"
    else
        "$ziran" build --target=cpp --root "$work" \
            -o "$out" "$work/branches.zi"
        cat > "$out/main.cpp" <<'EOF'
#include "branches.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
        ${CXX:-c++} -Iinclude -I"$out" "$out/branches.cpp" \
            "$out/main.cpp" -o "$out/app"
        "$out/app"
    fi
done

cat > "$work/old_branch.zi" <<'EOF'
#if 1 {
Value :: 1;
} #else_if 0 {
Value :: 2;
}
EOF
if "$ziran" check --root "$work" "$work/old_branch.zi" \
    2> "$work/old_branch.err"; then
    echo 'obsolete #else_if was accepted' >&2
    exit 1
fi
grep -Fq "Jai compile-time branches use 'else #if'" \
    "$work/old_branch.err"

cat > "$work/old_else.zi" <<'EOF'
#if 1 {
Value :: 1;
} #else {
Value :: 2;
}
EOF
if "$ziran" check --root "$work" "$work/old_else.zi" \
    2> "$work/old_else.err"; then
    echo 'obsolete #else was accepted' >&2
    exit 1
fi
grep -Fq "Jai compile-time branches use 'else #if'" \
    "$work/old_else.err"

cat > "$work/runtime_condition.zi" <<'EOF'
#if UNKNOWN {
Answer :: () -> s32 { return 42 }
}
EOF
if "$ziran" check --root "$work" "$work/runtime_condition.zi" \
    2> "$work/runtime_condition.err"; then
    echo 'unknown #if condition was accepted' >&2
    exit 1
fi
grep -Fq '#if condition is not a compile-time constant' \
    "$work/runtime_condition.err"

cat > "$work/unterminated.zi" <<'EOF'
#if true {
Answer :: () -> s32 { return 42 }
EOF
if "$ziran" check --root "$work" "$work/unterminated.zi" \
    2> "$work/unterminated.err"; then
    echo 'unterminated #if was accepted' >&2
    exit 1
fi
grep -Fq 'unterminated #if block' "$work/unterminated.err"
