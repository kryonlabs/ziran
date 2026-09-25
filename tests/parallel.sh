#!/bin/sh
set -eu

ziran=${1:?pass the ziran command}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/par.zi" <<'ZI'
Square :: (n: s64) -> s64 { return n * n }
Reader :: () -> s64 { return limit }
limit: s64;

#program_export
Answer :: () -> s64 {
    observed: s64 = 0
    #parallel for 0..10 {
        partial := Square(it)
        if partial > Reader() { partial = Reader() }
        if partial < 4 { partial = 4 }
    }
    return 285
}
ZI

"$ziran" ir --root "$work" -o "$work/ir" "$work/par.zi"
for input in source saved; do
    if test "$input" = source; then
        module=$work/par.zi
        root=$work
    else
        module=$work/ir/par.zir
        root=$work/ir
    fi
    "$ziran" bundle --root "$root" --entry par:Answer \
        -o "$work/$input.zib" "$module"
    test "$("$ziran" run "$work/$input.zib")" = 285
    for target in c cpp go; do
        output="$work/$target-$input"
        if test "$target" = go; then
            "$ziran" build --target=go --pkg main --entry par:Answer \
                --root "$root" -o "$output" "$module"
            cat > "$output/main.go" <<'GO'
package main
func main() { if Par_Answer() != 285 { panic("parallel") } }
GO
            GO111MODULE=off go run "$output"/*.go
        elif test "$target" = c; then
            "$ziran" build --target=c --entry par:Answer --root "$root" \
                -o "$output" "$module"
            cat > "$output/main.c" <<'C'
#include "par.h"
int main(void) { return Answer() == 285 ? 0 : 1; }
C
            "${CC:-cc}" -std=c99 -pedantic-errors \
                -I"$repo/include" -I"$output" "$output"/*.c \
                -o "$output/app"
            "$output/app"
        else
            "$ziran" build --target=cpp --entry par:Answer --root "$root" \
                -o "$output" "$module"
            cat > "$output/main.cpp" <<'CPP'
#include "par.hpp"
int main() { return Answer() == 285 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 \
                -I"$repo/include" -I"$output" "$output"/*.cpp \
                -o "$output/app"
            "$output/app"
        fi
    done
done
cmp "$work/source.zib" "$work/saved.zib"

reject() {
    name=$1
    pattern=$2
    if "$ziran" check --root "$work" "$work/$name.zi" \
        2> "$work/$name.err"; then
        echo "$3 was accepted" >&2
        exit 1
    fi
    grep -Fq "$pattern" "$work/$name.err"
}

cat > "$work/bad_write.zi" <<'ZI'
Square :: (n: s64) -> s64 { return n * n }
#program_export
Bad :: () -> s64 {
    total := 0
    #parallel for 0..10 {
        total += Square(it)
    }
    return total
}
ZI
reject bad_write "iteration-local binding" "an outside write"

cat > "$work/bad_mutating.zi" <<'ZI'
shared: s64;
Bump :: () -> s64 { shared += 1; return shared }
#program_export
Bad :: () -> s64 {
    #parallel for 0..4 {
        Bump()
    }
    return shared
}
ZI
reject bad_mutating "mutating code" "a mutating callee"

cat > "$work/bad_foreign.zi" <<'ZI'
host_api :: #system_library "host_api";
Ping :: () -> s64 #foreign host_api;
#program_export
Bad :: () -> s64 {
    #parallel for 0..4 {
        Ping()
    }
    return 0
}
ZI
reject bad_foreign "foreign code" "a foreign callee"

cat > "$work/bad_nest.zi" <<'ZI'
#program_export
Bad :: () -> s64 {
    #parallel for 0..4 {
        #parallel for 0..2 {
        }
    }
    return 0
}
ZI
reject bad_nest "cannot nest" "a nested region"

cat > "$work/bad_shape.zi" <<'ZI'
#program_export
Bad :: () -> s64 {
    #parallel while true {
    }
    return 0
}
ZI
reject bad_shape "#parallel requires a for region" "a parallel while"
