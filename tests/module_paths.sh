#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/app" "$work/lib"
cat > "$work/lib/base.zi" <<'EOF'
#program_export
Value :: () -> s32 {
    return 40
}
ConstValue :: 2;
Thing :: struct {
    number: s32
}
#scope_file
Secret :: () -> s32 { return 99 }
PrivateThing :: struct {
    number: s32
}
#scope_export
EOF
cat > "$work/lib/middle.zi" <<'EOF'
#import "base"
#program_export
Answer :: () -> s32 {
    return Value() + 2
}
EOF
cat > "$work/app/main.zi" <<'EOF'
#import "middle"
#program_export
Result :: () -> s32 {
    return Answer()
}
EOF

"$ziran" check --root "$work/app" --module-path "$work/lib" \
    "$work/app/main.zi"
cat > "$work/app/compile_import.zi" <<'EOF'
Base :: #import "base";
#if Base.Value() == 40 {
Answer :: () -> s32 { return 42 }
} else {
Answer :: () -> s32 { return MissingValue() }
#import "missing_module"
}
EOF
"$ziran" check --root "$work/app" --module-path "$work/lib" \
    "$work/app/compile_import.zi"
cat > "$work/app/compile_file_import.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#if Base.Value() == 40 {
Answer :: () -> s32 { return 42 }
} else {
Answer :: () -> s32 { return MissingValue() }
}
EOF
"$ziran" check --root "$work/app" "$work/app/compile_file_import.zi"
"$ziran" ir --root "$work/app" --module-path "$work/lib" \
    -o "$work/ir" "$work/app/main.zi"
for module in main middle base; do
    test -s "$work/ir/$module.zir"
done

cat > "$work/app/old_module.zi" <<'EOF'
#module "old_module"
Answer :: () -> s32 { return 42; }
EOF
if "$ziran" check --root "$work/app" "$work/app/old_module.zi" \
    2> "$work/old_module.err"; then
    echo 'non-Jai #module directive was accepted' >&2
    exit 1
fi
grep -Fq 'unknown directive: #module' "$work/old_module.err"
cat > "$work/app/angled_import.zi" <<'EOF'
#import <stdio.h>
EOF
if "$ziran" check --root "$work/app" "$work/app/angled_import.zi" \
    2> "$work/angled_import.err"; then
    echo 'C-style angled import unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'Jai #import requires a quoted module or file name' \
    "$work/angled_import.err"
cat > "$work/app/header_import.zi" <<'EOF'
#import "stdio.h"
EOF
if "$ziran" check --root "$work/app" "$work/app/header_import.zi" \
    2> "$work/header_import.err"; then
    echo 'C-header import unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'Jai #import requires a module identifier' \
    "$work/header_import.err"
"$ziran" bundle --root "$work/app" --module-path "$work/lib" \
    --entry main:Result -o "$work/source.zib" "$work/app/main.zi"
"$ziran" bundle --root "$work/ir" --module-path "$work/ir" \
    --entry main:Result -o "$work/saved.zib" "$work/ir/main.zir"
cmp "$work/source.zib" "$work/saved.zib"
test "$("$ziran" run "$work/saved.zib")" = 42

"$ziran" build --target=c --root "$work/app" \
    --module-path "$work/lib" -o "$work/c" "$work/app/main.zi"
cat > "$work/c/test.c" <<'EOF'
#include "main.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/base.c" "$work/c/middle.c" \
    "$work/c/main.c" "$work/c/test.c" -o "$work/test"
"$work/test"

cat > "$work/app/explicit.zi" <<'EOF'
#import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { return Value() + 2 }
EOF
"$ziran" check --root "$work/app" "$work/app/explicit.zi"
"$ziran" ir --root "$work/app" -o "$work/explicit-ir" \
    "$work/app/explicit.zi"
if ! test -s "$work/explicit-ir/base.zir"; then
    ls -R "$work/explicit-ir" >&2
    exit 1
fi
"$ziran" bundle --root "$work/app" --entry explicit:Result \
    -o "$work/explicit-source.zib" "$work/app/explicit.zi"
"$ziran" bundle --root "$work/explicit-ir" --entry explicit:Result \
    -o "$work/explicit-saved.zib" "$work/explicit-ir/explicit.zir"
cmp "$work/explicit-source.zib" "$work/explicit-saved.zib"
test "$("$ziran" run "$work/explicit-source.zib")" = 42
"$ziran" build --target=c --root "$work/app" \
    -o "$work/explicit-c" "$work/app/explicit.zi"
cat > "$work/explicit-c/main.c" <<'EOF'
#include "explicit.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/explicit-c" \
    "$work/explicit-c/base.c" "$work/explicit-c/explicit.c" \
    "$work/explicit-c/main.c" -o "$work/explicit-app"
"$work/explicit-app"
for input in source saved; do
    if test "$input" = source; then
        file=$work/app/explicit.zi
        root=$work/app
    else
        file=$work/explicit-ir/explicit.zir
        root=$work/explicit-ir
    fi
    "$ziran" build --target=cpp --root "$root" \
        -o "$work/explicit-cpp-$input" "$file"
    cat > "$work/explicit-cpp-$input/main.cpp" <<'EOF'
#include "explicit.hpp"
int main() { return Result() == 42 ? 0 : 1; }
EOF
    ${CXX:-c++} -Iinclude -I"$work/explicit-cpp-$input" \
        "$work/explicit-cpp-$input/base.cpp" \
        "$work/explicit-cpp-$input/explicit.cpp" \
        "$work/explicit-cpp-$input/main.cpp" \
        -o "$work/explicit-cpp-app-$input"
    "$work/explicit-cpp-app-$input"
    "$ziran" build --target=go --pkg main --root "$root" \
        -o "$work/explicit-go-$input" "$file"
    cat > "$work/explicit-go-$input/main.go" <<'EOF'
package main
func main() { if Explicit_Result() != 42 { panic("explicit import changed") } }
EOF
    GO111MODULE=off go run "$work/explicit-go-$input"/*.go
done

cat > "$work/app/named.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 {
    item: Base.Thing
    item.number = Base.Value() + Base.ConstValue
    return item.number
}
EOF
"$ziran" check --root "$work/app" "$work/app/named.zi"
"$ziran" ir --root "$work/app" -o "$work/named-ir" \
    "$work/app/named.zi"
"$ziran" bundle --root "$work/app" --entry named:Result \
    -o "$work/named-source.zib" "$work/app/named.zi"
"$ziran" bundle --root "$work/named-ir" --entry named:Result \
    -o "$work/named-saved.zib" "$work/named-ir/named.zir"
cmp "$work/named-source.zib" "$work/named-saved.zib"
test "$("$ziran" run "$work/named-saved.zib")" = 42
for target in c cpp go; do
    if test "$target" = go; then
        "$ziran" build --target=go --pkg main --root "$work/app" \
            -o "$work/named-go" "$work/app/named.zi"
    else
        "$ziran" build --target="$target" --root "$work/app" \
            -o "$work/named-$target" "$work/app/named.zi"
    fi
done
cat > "$work/named-c/main.c" <<'EOF'
#include "named.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/named-c" "$work/named-c/base.c" \
    "$work/named-c/named.c" "$work/named-c/main.c" -o "$work/named-c-app"
"$work/named-c-app"
cat > "$work/named-cpp/main.cpp" <<'EOF'
#include "named.hpp"
int main() { return Result() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/named-cpp" "$work/named-cpp/base.cpp" \
    "$work/named-cpp/named.cpp" "$work/named-cpp/main.cpp" \
    -o "$work/named-cpp-app"
"$work/named-cpp-app"
cat > "$work/named-go/main.go" <<'EOF'
package main
func main() { if Named_Result() != 42 { panic("named import changed") } }
EOF
GO111MODULE=off go run "$work/named-go"/*.go
cat > "$work/app/named_unqualified.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { return Value() }
EOF
if "$ziran" check --root "$work/app" "$work/app/named_unqualified.zi" \
    2> "$work/named_unqualified.err"; then
    echo 'named import leaked an unqualified function' >&2
    exit 1
fi
cat > "$work/app/named_constant_unqualified.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { return ConstValue }
EOF
if "$ziran" check --root "$work/app" \
    "$work/app/named_constant_unqualified.zi" \
    2> "$work/named_constant_unqualified.err"; then
    echo 'named import leaked an unqualified constant' >&2
    exit 1
fi
cat > "$work/app/named_private.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { return Base.Secret() }
EOF
if "$ziran" check --root "$work/app" "$work/app/named_private.zi" \
    2> "$work/named_private.err"; then
    echo 'named import exposed a private function' >&2
    exit 1
fi
cat > "$work/app/named_private_type.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { item: Base.PrivateThing; return item.number }
EOF
if "$ziran" check --root "$work/app" "$work/app/named_private_type.zi" \
    2> "$work/named_private_type.err"; then
    echo 'named import exposed a private type' >&2
    exit 1
fi
cat > "$work/app/named_duplicate.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
Base :: #import, file "../lib/base.zi";
EOF
if "$ziran" check --root "$work/app" "$work/app/named_duplicate.zi" \
    2> "$work/named_duplicate.err"; then
    echo 'duplicate import alias unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'duplicate import alias' "$work/named_duplicate.err"
cat > "$work/app/named_shadow.zi" <<'EOF'
Base :: #import, file "../lib/base.zi";
#program_export
Result :: () -> s32 { Base: s32 = 1; return Base }
EOF
if "$ziran" check --root "$work/app" "$work/app/named_shadow.zi" \
    2> "$work/named_shadow.err"; then
    echo 'local binding shadowed an import alias' >&2
    exit 1
fi
grep -Fq 'binding shadows an imported module' "$work/named_shadow.err"
cat > "$work/app/named_search.zi" <<'EOF'
Base :: #import "base";
#program_export
Result :: () -> s32 { return Base.Value() + Base.ConstValue }
EOF
"$ziran" check --root "$work/app" --module-path "$work/lib" \
    "$work/app/named_search.zi"
"$ziran" bundle --root "$work/app" --module-path "$work/lib" \
    --entry named_search:Result -o "$work/named-search.zib" \
    "$work/app/named_search.zi"
test "$("$ziran" run "$work/named-search.zib")" = 42
mkdir -p "$work/lib/suite"
cat > "$work/lib/suite/module.zi" <<'EOF'
#load "answer.zi";
EOF
cat > "$work/lib/suite/answer.zi" <<'EOF'
#program_export
Answer :: () -> s32 { return 42 }
EOF
cat > "$work/app/directory.zi" <<'EOF'
Suite :: #import, dir "../lib/suite";
#program_export
Result :: () -> s32 { return Suite.Answer() }
EOF
"$ziran" check --root "$work/app" "$work/app/directory.zi"
"$ziran" ir --root "$work/app" -o "$work/directory-ir" \
    "$work/app/directory.zi"
test -s "$work/directory-ir/suite.zir"
"$ziran" bundle --root "$work/app" --entry directory:Result \
    -o "$work/directory-source.zib" "$work/app/directory.zi"
"$ziran" bundle --root "$work/directory-ir" --entry directory:Result \
    -o "$work/directory-saved.zib" "$work/directory-ir/directory.zir"
cmp "$work/directory-source.zib" "$work/directory-saved.zib"
test "$("$ziran" run "$work/directory-saved.zib")" = 42
for target in c cpp go; do
    if test "$target" = go; then
        "$ziran" build --target=go --pkg main --root "$work/app" \
            -o "$work/directory-go" "$work/app/directory.zi"
    else
        "$ziran" build --target="$target" --root "$work/app" \
            -o "$work/directory-$target" "$work/app/directory.zi"
    fi
done
cat > "$work/directory-c/main.c" <<'EOF'
#include "directory.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/directory-c" \
    "$work/directory-c/suite.c" "$work/directory-c/directory.c" \
    "$work/directory-c/main.c" -o "$work/directory-c-app"
"$work/directory-c-app"
cat > "$work/directory-cpp/main.cpp" <<'EOF'
#include "directory.hpp"
int main() { return Result() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/directory-cpp" \
    "$work/directory-cpp/suite.cpp" "$work/directory-cpp/directory.cpp" \
    "$work/directory-cpp/main.cpp" -o "$work/directory-cpp-app"
"$work/directory-cpp-app"
cat > "$work/directory-go/main.go" <<'EOF'
package main
func main() { if Directory_Result() != 42 { panic("directory import changed") } }
EOF
GO111MODULE=off go run "$work/directory-go"/*.go
for target in c cpp go; do
    if test "$target" = go; then
        "$ziran" build --target=go --pkg main \
            --root "$work/directory-ir" -o "$work/directory-go-saved" \
            "$work/directory-ir/directory.zir"
        for module in suite directory; do
            cmp "$work/directory-go/$module.go" \
                "$work/directory-go-saved/$module.go"
        done
    else
        "$ziran" build --target="$target" \
            --root "$work/directory-ir" -o "$work/directory-$target-saved" \
            "$work/directory-ir/directory.zir"
        if test "$target" = c; then suffix=c; header=h
        else suffix=cpp; header=hpp
        fi
        for module in suite directory; do
            cmp "$work/directory-$target/$module.$suffix" \
                "$work/directory-$target-saved/$module.$suffix"
            cmp "$work/directory-$target/$module.$header" \
                "$work/directory-$target-saved/$module.$header"
        done
    fi
done
cat > "$work/app/directory_search.zi" <<'EOF'
#import "suite"
#program_export
Result :: () -> s32 { return Answer() }
EOF
"$ziran" check --root "$work/app" --module-path "$work/lib" \
    "$work/app/directory_search.zi"
"$ziran" ir --root "$work/app" --module-path "$work/lib" \
    -o "$work/directory-search-ir" "$work/app/directory_search.zi"
mkdir -p "$work/directory-main-only"
cp "$work/directory-search-ir/directory_search.zir" \
    "$work/directory-main-only/"
"$ziran" check --root "$work/directory-main-only" \
    --module-path "$work/lib" \
    "$work/directory-main-only/directory_search.zir"
cat > "$work/app/directory_missing.zi" <<'EOF'
Suite :: #import, dir "../lib/absent";
EOF
if "$ziran" check --root "$work/app" "$work/app/directory_missing.zi" \
    2> "$work/directory_missing.err"; then
    echo 'missing directory module unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'cannot find imported directory module' \
    "$work/directory_missing.err"
mkdir -p "$work/app/loaded"
cat > "$work/app/loaded/inner.zi" <<'EOF'
Inner :: () -> s32 { return 40 }
EOF
cat > "$work/app/loaded/part.zi" <<'EOF'
#load "inner.zi";
#import, file "../../lib/base.zi";
Part :: () -> s32 { return Inner() + ConstValue }
EOF
cat > "$work/app/loaded_main.zi" <<'EOF'
#load "loaded/part.zi";
#program_export
Result :: () -> s32 { return Part() }
EOF
"$ziran" check --root "$work/app" "$work/app/loaded_main.zi"
"$ziran" ir --root "$work/app" -o "$work/loaded-ir" \
    "$work/app/loaded_main.zi"
"$ziran" bundle --root "$work/app" --entry loaded_main:Result \
    -o "$work/loaded-source.zib" "$work/app/loaded_main.zi"
"$ziran" bundle --root "$work/loaded-ir" --entry loaded_main:Result \
    -o "$work/loaded-saved.zib" "$work/loaded-ir/loaded_main.zir"
cmp "$work/loaded-source.zib" "$work/loaded-saved.zib"
test "$("$ziran" run "$work/loaded-saved.zib")" = 42
"$ziran" build --target=c --root "$work/app" \
    -o "$work/loaded-c" "$work/app/loaded_main.zi"
cat > "$work/loaded-c/main.c" <<'EOF'
#include "loaded_main.h"
int main(void) { return Result() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/loaded-c" "$work/loaded-c/base.c" \
    "$work/loaded-c/loaded_main.c" \
    "$work/loaded-c/main.c" -o "$work/loaded-app"
"$work/loaded-app"
"$ziran" build --target=cpp --root "$work/app" \
    -o "$work/loaded-cpp" "$work/app/loaded_main.zi"
cat > "$work/loaded-cpp/main.cpp" <<'EOF'
#include "loaded_main.hpp"
int main() { return Result() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/loaded-cpp" \
    "$work/loaded-cpp/base.cpp" "$work/loaded-cpp/loaded_main.cpp" \
    "$work/loaded-cpp/main.cpp" -o "$work/loaded-cpp-app"
"$work/loaded-cpp-app"
"$ziran" build --target=go --pkg main --root "$work/app" \
    -o "$work/loaded-go" "$work/app/loaded_main.zi"
cat > "$work/loaded-go/main.go" <<'EOF'
package main
func main() { if LoadedMain_Result() != 42 { panic("loaded module changed") } }
EOF
GO111MODULE=off go run "$work/loaded-go"/*.go
cat > "$work/app/load_cycle.zi" <<'EOF'
#load "load_cycle.zi";
EOF
if "$ziran" check --root "$work/app" "$work/app/load_cycle.zi" \
    2> "$work/load_cycle.err"; then
    echo 'cyclic #load unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'cyclic #load' "$work/load_cycle.err"
cat > "$work/app/loaded/bad.zi" <<'EOF'
Broken :: () -> s32 {
    return MissingFromLoadedFile()
}
EOF
cat > "$work/app/load_bad.zi" <<'EOF'
#load "loaded/bad.zi";
EOF
if "$ziran" check --root "$work/app" "$work/app/load_bad.zi" \
    2> "$work/load_bad.err"; then
    echo 'invalid loaded file unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'loaded/bad.zi:2:' "$work/load_bad.err"
cat > "$work/app/load_inactive.zi" <<'EOF'
#if false {
#load "absent.zi";
}
#program_export
Result :: () -> s32 { return 42 }
EOF
"$ziran" check --root "$work/app" "$work/app/load_inactive.zi"

cat > "$work/app/missing_file.zi" <<'EOF'
#import, file "../lib/missing.zi";
EOF
if "$ziran" check --diagnostics=json --root "$work/app" \
    "$work/app/missing_file.zi" 2> "$work/missing_file.err"; then
    echo 'missing explicit file import unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'cannot find imported file' "$work/missing_file.err"

if "$ziran" check --diagnostics=json --root "$work/app" "$work/app/main.zi" \
    2> "$work/missing.err"; then
    echo 'missing module search path unexpectedly succeeded' >&2
    exit 1
fi
grep -Fq 'module.not_found' "$work/missing.err"

for extension in kry kir krb; do
    cp "$work/lib/base.zi" "$work/app/legacy.$extension"
    if "$ziran" check --diagnostics=json --root "$work/app" \
        "$work/app/legacy.$extension" 2> "$work/legacy.err"; then
        echo "legacy .$extension module unexpectedly succeeded" >&2
        exit 1
    fi
    grep -Fq 'module.extension' "$work/legacy.err"
done
