#!/bin/sh
set -eu

ziran=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/hello.zi" <<'EOF'
#module "hello"
Image :: () -> i32 #export {
    return 42
}
Answer :: () -> i32 #export {
    return Image()
}
EOF

"$ziran" check --root "$work" "$work/hello.zi"
"$ziran" fmt --check "$work/hello.zi"
cat > "$work/wrapped.zi" <<'EOF'
#module "wrapped"
Add :: (first: i32,
    second: i32) -> i32 #export {
    return first + second
}
EOF
"$ziran" fmt --check "$work/wrapped.zi"
"$ziran" ir --root "$work" -o "$work/ir" "$work/hello.zi"
"$ziran" ir --root "$work" -o "$work/ir-again" "$work/hello.zi"
cmp "$work/ir/hello.zir" "$work/ir-again/hello.zir"
test -s "$work/ir/hello.zir"
python3 - "$work/ir/hello.zir" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data[:8] == b'ZIR\0\x04\0\0\0', data[:8]
PY
"$ziran" build --target=c --root "$work" -o "$work/c" "$work/hello.zi"
test -s "$work/c/hello.c"
grep -Fq 'Image(' "$work/c/hello.c"
if grep -Fq 'RenderImage(' "$work/c/hello.c"; then
    echo 'generic Image call was rewritten as a UI host call' >&2
    exit 1
fi
cat > "$work/main.c" <<'EOF'
#include "hello.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/c" "$work/c/hello.c" "$work/main.c" -o "$work/hello"
"$work/hello"
"$ziran" build --target=c --root "$work" -o "$work/c-ir" "$work/ir/hello.zir"
${CC:-cc} -Iinclude -I"$work/c-ir" "$work/c-ir/hello.c" "$work/main.c" \
    -o "$work/hello-from-ir"
"$work/hello-from-ir"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/cpp" \
    "$work/hello.zi"
cat > "$work/cpp/main.cpp" <<'EOF'
#include "hello.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/cpp" "$work/cpp/hello.cpp" \
    "$work/cpp/main.cpp" -o "$work/hello-cpp"
"$work/hello-cpp"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/cpp-ir" \
    "$work/ir/hello.zir"
cp "$work/cpp/main.cpp" "$work/cpp-ir/main.cpp"
${CXX:-c++} -Iinclude -I"$work/cpp-ir" "$work/cpp-ir/hello.cpp" \
    "$work/cpp-ir/main.cpp" -o "$work/hello-cpp-ir"
"$work/hello-cpp-ir"
"$ziran" build --target=go --strict --pkg main --root "$work" -o "$work/go" "$work/hello.zi"
test -s "$work/go/hello.go"
cat > "$work/go/main.go" <<'EOF'
package main
func main() { if Hello_Answer() != 42 { panic("wrong result") } }
EOF
GO111MODULE=off go run "$work/go/hello.go" "$work/go/main.go"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/go-ir" "$work/ir/hello.zir"
cp "$work/go/main.go" "$work/go-ir/main.go"
GO111MODULE=off go run "$work/go-ir/hello.go" "$work/go-ir/main.go"
"$ziran" build --target=go --strict --root "$work" \
    -o "$work/go-default-package" "$work/hello.zi"
grep -Fxq 'package ziran' "$work/go-default-package/hello.go"

cat > "$work/ordinary_names.zi" <<'EOF'
#module "ordinary_names"
ModalProps :: struct {
    text: string
}
TextFieldProps :: struct {
    text: string
    text_size: i32
}
TableViewProps :: struct {
    copy_text: string
}
NumericFloat :: () -> i32 #export {
    return 20
}
DragSingle :: () -> i32 #export {
    return 22
}
Answer :: () -> i32 #export {
    modal: ModalProps
    modal.text = "hello"
    field: TextFieldProps
    field.text = "hello"
    field.text_size = 5
    table: TableViewProps
    table.copy_text = "hello"
    if modal.text.length != 5 || field.text.length != field.text_size ||
        table.copy_text.length != 5 { return 0 }
    return NumericFloat() + DragSingle()
}
EOF
"$ziran" ir --root "$work" -o "$work/ordinary-ir" \
    "$work/ordinary_names.zi"
for input in "$work/ordinary_names.zi" "$work/ordinary-ir/ordinary_names.zir"; do
    output="$work/ordinary-go-$(basename "$input")"
    "$ziran" build --target=go --strict --pkg main --root "$work" \
        -o "$output" "$input"
    if grep -Fq 'github.com/waozixyz/kryon' "$output/ordinary_names.go"; then
        echo 'ordinary names pulled in the Kryon Go runtime' >&2
        exit 1
    fi
    cat > "$output/main.go" <<'GO'
package main
func main() { if OrdinaryNames_Answer() != 42 { panic("wrong result") } }
GO
    GO111MODULE=off go run "$output/ordinary_names.go" "$output/main.go"
done

cat > "$work/ffi_direct.zi" <<'EOF'
#module "ffi_direct"
Reverse :: (value: u32) -> u32 #extern "math/bits.Reverse32" #export
Answer :: () -> i32 #export {
    return (i32)Reverse((u32)0x54000000)
}
EOF
cat > "$work/ffi_host.zi" <<'EOF'
#module "ffi_host"
Value :: () -> i32 #extern #export
Answer :: () -> i32 #export {
    return Value() + 1
}
EOF
"$ziran" ir --root "$work" -o "$work/ffi-ir" \
    "$work/ffi_direct.zi" "$work/ffi_host.zi"
for input in source ir; do
    if test "$input" = source; then
        extension=zi
        input_dir=$work
    else
        extension=zir
        input_dir=$work/ffi-ir
    fi
    "$ziran" build --target=go --strict --pkg main --root "$work" \
        -o "$work/ffi-go-$input" "$input_dir/ffi_direct.$extension" \
        "$input_dir/ffi_host.$extension"
    if grep -Fq 'github.com/waozixyz/kryon' \
        "$work/ffi-go-$input/ffi_direct.go" \
        "$work/ffi-go-$input/ffi_host.go"; then
        echo 'Go FFI imported Kryon implicitly' >&2
        exit 1
    fi
    cat > "$work/ffi-go-$input/main.go" <<'GO'
package main
type host struct{}
func (host) Value() int32 { return 41 }
func main() {
    SetFfiHostHost(host{})
    if FfiDirect_Answer() != 42 || FfiHost_Answer() != 42 {
        panic("wrong FFI result")
    }
}
GO
    GO111MODULE=off go run "$work/ffi-go-$input/ffi_direct.go" \
        "$work/ffi-go-$input/ffi_host.go" "$work/ffi-go-$input/main.go"
done
if "$ziran" build --target=go --runtime-implementation --root "$work" \
    -o "$work/legacy-go" "$work/hello.zi" 2> "$work/legacy-go.err"; then
    echo 'legacy Kryon runtime mode unexpectedly passed' >&2
    exit 1
fi

cat > "$work/library.zi" <<'EOF'
#module "library"
Button :: (value: i32) -> i32 #export {
    return value + 1
}
EOF
cat > "$work/app.zi" <<'EOF'
#module "app"
#import "library"
Answer :: () -> i32 #export {
    return Button(41)
}
EOF
"$ziran" build --target=c --strict --root "$work" -o "$work/modules" \
    "$work/library.zi" "$work/app.zi"
cat > "$work/modules/main.c" <<'EOF'
#include "app.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/modules" "$work/modules/library.c" \
    "$work/modules/app.c" "$work/modules/main.c" -o "$work/modules/app"
"$work/modules/app"

"$ziran" bundle --root "$work" --entry app:Answer -o "$work/app.zib" \
    "$work/library.zi" "$work/app.zi"
test "$("$ziran" run "$work/app.zib")" = 42
"$ziran" ir --root "$work" -o "$work/modules-ir" \
    "$work/library.zi" "$work/app.zi"
"$ziran" bundle --root "$work" --entry app:Answer -o "$work/app-from-ir.zib" \
    "$work/modules-ir/library.zir" "$work/modules-ir/app.zir"
cmp "$work/app.zib" "$work/app-from-ir.zib"
test "$("$ziran" run "$work/app-from-ir.zib")" = 42
"$ziran" build --target=c --strict --root "$work" -o "$work/mixed-modules" \
    "$work/library.zi" "$work/modules-ir/app.zir"
cp "$work/modules/main.c" "$work/mixed-modules/main.c"
${CC:-cc} -Iinclude -I"$work/mixed-modules" \
    "$work/mixed-modules/library.c" "$work/mixed-modules/app.c" \
    "$work/mixed-modules/main.c" -o "$work/mixed-modules/app"
"$work/mixed-modules/app"
cat > "$work/local-bundle.zi" <<'EOF'
#module "local_bundle"
Answer :: () -> i32 #export {
    value: i32 = 40
    value = value + 2
    return value
}
EOF
"$ziran" bundle --root "$work" --entry local_bundle:Answer \
    -o "$work/local-bundle.zib" "$work/local-bundle.zi"
test "$("$ziran" run "$work/local-bundle.zib")" = 42
cat > "$work/flow.zi" <<'EOF'
#module "flow"
Answer :: () -> i32 #export {
    value: i32 = 0
    index: i32 = 0
    while index < 10 {
        index = index + 1
        if index < 3 {
            continue
        }
        if index > 5 {
            break
        }
        value = value + index
    }
    if value == 0 {
        return 0
    } else if value == 12 {
        return 42
    } else {
        return 0
    }
}
EOF
"$ziran" build --target=c --strict --root "$work" -o "$work/flow-c" \
    "$work/flow.zi"
cat > "$work/flow-c/main.c" <<'EOF'
#include "flow.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/flow-c" "$work/flow-c/flow.c" \
    "$work/flow-c/main.c" -o "$work/flow-c/app"
"$work/flow-c/app"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/flow-cpp" \
    "$work/flow.zi"
cat > "$work/flow-cpp/main.cpp" <<'EOF'
#include "flow.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/flow-cpp" "$work/flow-cpp/flow.cpp" \
    "$work/flow-cpp/main.cpp" -o "$work/flow-cpp/app"
"$work/flow-cpp/app"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/flow-go" "$work/flow.zi"
cat > "$work/flow-go/main.go" <<'EOF'
package main
func main() { if Flow_Answer() != 42 { panic("wrong result") } }
EOF
GO111MODULE=off go run "$work/flow-go/flow.go" "$work/flow-go/main.go"
"$ziran" bundle --root "$work" --entry flow:Answer \
    -o "$work/flow.zib" "$work/flow.zi"
test "$("$ziran" run "$work/flow.zib")" = 42
"$ziran" ir --root "$work" -o "$work/flow-ir" "$work/flow.zi"
"$ziran" bundle --root "$work" --entry flow:Answer \
    -o "$work/flow-from-ir.zib" "$work/flow-ir/flow.zir"
cmp "$work/flow.zib" "$work/flow-from-ir.zib"
test "$("$ziran" run "$work/flow-from-ir.zib")" = 42
cat > "$work/reals.zi" <<'EOF'
#module "reals"
Scale :: (value: float) -> float #export {
    return value * 2.0
}
Half :: (value: double) -> double #export {
    return value / 2.0
}
Answer :: () -> i32 #export {
    scaled: float = Scale(1.25)
    half: double = Half(7.0)
    if scaled != 2.5 || half != 3.5 { return 0 }
    if (i32)(scaled + 0.5) != 3 { return 0 }
    positive: bool = scaled > 2.0 ? true : false
    if !positive || 1.0 / 2.0 != 0.5 { return 0 }
    return 42
}
EOF
"$ziran" build --target=c --strict --root "$work" -o "$work/reals-c" \
    "$work/reals.zi"
cat > "$work/reals-c/main.c" <<'EOF'
#include "reals.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/reals-c" "$work/reals-c/reals.c" \
    "$work/reals-c/main.c" -o "$work/reals-c/app"
"$work/reals-c/app"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/reals-cpp" \
    "$work/reals.zi"
cat > "$work/reals-cpp/main.cpp" <<'EOF'
#include "reals.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/reals-cpp" "$work/reals-cpp/reals.cpp" \
    "$work/reals-cpp/main.cpp" -o "$work/reals-cpp/app"
"$work/reals-cpp/app"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/reals-go" "$work/reals.zi"
cat > "$work/reals-go/main.go" <<'EOF'
package main
func main() { if Reals_Answer() != 42 { panic("wrong real result") } }
EOF
GO111MODULE=off go run "$work/reals-go/reals.go" "$work/reals-go/main.go"
"$ziran" bundle --root "$work" --entry reals:Answer \
    -o "$work/reals.zib" "$work/reals.zi"
test "$("$ziran" run "$work/reals.zib")" = 42
"$ziran" ir --root "$work" -o "$work/reals-ir" "$work/reals.zi"
"$ziran" bundle --root "$work" --entry reals:Answer \
    -o "$work/reals-from-ir.zib" "$work/reals-ir/reals.zir"
cmp "$work/reals.zib" "$work/reals-from-ir.zib"
test "$("$ziran" run "$work/reals-from-ir.zib")" = 42
cat > "$work/reachable_library.zi" <<'EOF'
#module "reachable_library"
Props :: struct {
    value: i32
}
UnusedRecord :: (props: Props) -> i32 #export {
    return props.value
}
Scale :: (value: float) -> float #export {
    return value * 1.5
}
EOF
cat > "$work/dead_library.zi" <<'EOF'
#module "dead_library"
UnusedDead :: () -> i32 #export {
    return 99
}
EOF
cat > "$work/reachable_app.zi" <<'EOF'
#module "reachable_app"
#import "reachable_library"
Answer :: () -> i32 #export {
    if Scale(2.0) == 3.0 { return 42 }
    return 0
}
EOF
"$ziran" bundle --root "$work" --entry reachable_app:Answer \
    -o "$work/reachable.zib" "$work/reachable_library.zi" \
    "$work/dead_library.zi" "$work/reachable_app.zi"
test "$("$ziran" run "$work/reachable.zib")" = 42
"$ziran" ir --root "$work" -o "$work/reachable-ir" \
    "$work/reachable_library.zi" "$work/dead_library.zi" \
    "$work/reachable_app.zi"
"$ziran" bundle --root "$work" --entry reachable_app:Answer \
    -o "$work/reachable-from-ir.zib" \
    "$work/reachable-ir/reachable_library.zir" \
    "$work/reachable-ir/dead_library.zir" \
    "$work/reachable-ir/reachable_app.zir"
cmp "$work/reachable.zib" "$work/reachable-from-ir.zib"
test "$("$ziran" run "$work/reachable-from-ir.zib")" = 42
python3 - "$work/reachable.zib" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert b'UnusedRecord' not in data
assert b'dead_library' not in data
assert b'UnusedDead' not in data
PY
cat > "$work/record_shapes.zi" <<'EOF'
#module "record_shapes"
Point :: struct {
    x: i32
}
Rectangle :: struct {
    origin: Point
    width: i32
}
Unused :: struct {
    value: i32
}
EOF
cat > "$work/record_operations.zi" <<'EOF'
#module "record_operations"
#import "record_shapes"
Measure :: (rect: Rectangle) -> i32 #export {
    return rect.origin.x + rect.width
}
Widen :: (rect: Rectangle) -> Rectangle #export {
    out: Rectangle = rect
    out.width = 100
    return out
}
EOF
cat > "$work/record_application.zi" <<'EOF'
#module "record_application"
#import "record_shapes"
#import "record_operations"
Answer :: () -> i32 #export {
    rect: Rectangle
    rect.origin.x = 2
    rect.width = 40
    wide: Rectangle = Widen(rect)
    if rect.width != 40 || wide.width != 100 { return 0 }
    return Measure(rect)
}
EOF
"$ziran" bundle --root "$work" --entry record_application:Answer \
    -o "$work/record.zib" "$work/record_shapes.zi" \
    "$work/record_operations.zi" "$work/record_application.zi"
test "$("$ziran" run "$work/record.zib")" = 42
"$ziran" ir --root "$work" -o "$work/record-ir" \
    "$work/record_shapes.zi" "$work/record_operations.zi" \
    "$work/record_application.zi"
"$ziran" bundle --root "$work" --entry record_application:Answer \
    -o "$work/record-from-ir.zib" \
    "$work/record-ir/record_shapes.zir" \
    "$work/record-ir/record_operations.zir" \
    "$work/record-ir/record_application.zir"
cmp "$work/record.zib" "$work/record-from-ir.zib"
test "$("$ziran" run "$work/record-from-ir.zib")" = 42
cat > "$work/modes.zi" <<'EOF'
#module "modes"
Mode :: enum {
    Off = -1
    On
    Later = On + 4
}
Settings :: struct {
    mode: Mode
    number: i32
}
Bump :: (settings: Settings) -> Settings #export {
    out: Settings = settings
    out.number += 2
    return out
}
EOF
cat > "$work/mode_app.zi" <<'EOF'
#module "mode_app"
#import "modes"
Answer :: () -> i32 #export {
    if Off != -1 || On != 0 || Later != 4 { return 0 }
    settings: Settings = (Settings){.mode = (Mode)Later, .number = 40}
    updated: Settings = Bump(settings)
    if settings.number != 40 || updated.number != 42 { return 0 }
    if updated.mode != (Mode)Later { return 0 }
    return updated.number
}
EOF
"$ziran" bundle --root "$work" --entry mode_app:Answer \
    -o "$work/modes.zib" "$work/modes.zi" "$work/mode_app.zi"
test "$("$ziran" run "$work/modes.zib")" = 42
"$ziran" ir --root "$work" -o "$work/modes-ir" \
    "$work/modes.zi" "$work/mode_app.zi"
"$ziran" bundle --root "$work" --entry mode_app:Answer \
    -o "$work/modes-from-ir.zib" \
    "$work/modes-ir/modes.zir" "$work/modes-ir/mode_app.zir"
cmp "$work/modes.zib" "$work/modes-from-ir.zib"
test "$("$ziran" run "$work/modes-from-ir.zib")" = 42
cat > "$work/direct_enum.zi" <<'EOF'
#module "direct_enum"
Step :: enum {
    First = 3
    Second
    Last = Second + 4
}
Answer :: () -> i32 #export {
    return Last
}
EOF
"$ziran" bundle --root "$work" --entry direct_enum:Answer \
    -o "$work/direct-enum.zib" "$work/direct_enum.zi"
test "$("$ziran" run "$work/direct-enum.zib")" = 8
cat > "$work/unsigned_bits.zi" <<'EOF'
#module "unsigned_bits"
Answer :: () -> i32 #export {
    value: u32 = (u32)0x80000000
    if (u32)1 << (u32)31 != value { return 0 }
    if value >> (u32)31 != (u32)1 { return 0 }
    value |= (u32)3
    if value != (u32)0x80000003 { return 0 }
    value &= ~(u32)1
    if value != (u32)0x80000002 { return 0 }
    value ^= (u32)2
    if value != (u32)0x80000000 { return 0 }
    value >>= (u32)31
    byte: u8 = (u8)258
    if byte != (u8)2 { return 0 }
    negative: i32 = -8
    if negative >> 2 != -2 { return 0 }
    return (i32)(value + (u32)41)
}
EOF
"$ziran" bundle --root "$work" --entry unsigned_bits:Answer \
    -o "$work/unsigned-bits.zib" "$work/unsigned_bits.zi"
test "$("$ziran" run "$work/unsigned-bits.zib")" = 42
"$ziran" ir --root "$work" -o "$work/unsigned-bits-ir" \
    "$work/unsigned_bits.zi"
"$ziran" bundle --root "$work" --entry unsigned_bits:Answer \
    -o "$work/unsigned-bits-from-ir.zib" \
    "$work/unsigned-bits-ir/unsigned_bits.zir"
cmp "$work/unsigned-bits.zib" "$work/unsigned-bits-from-ir.zib"
test "$("$ziran" run "$work/unsigned-bits-from-ir.zib")" = 42
"$ziran" build --target=c --strict --root "$work" \
    -o "$work/unsigned-bits-c" "$work/unsigned_bits.zi"
cat > "$work/unsigned-bits-c/main.c" <<'EOF'
#include "unsigned_bits.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/unsigned-bits-c" \
    "$work/unsigned-bits-c/unsigned_bits.c" \
    "$work/unsigned-bits-c/main.c" -o "$work/unsigned-bits-c/app"
"$work/unsigned-bits-c/app"
"$ziran" build --target=cpp --strict --root "$work" \
    -o "$work/unsigned-bits-cpp" "$work/unsigned_bits.zi"
cat > "$work/unsigned-bits-cpp/main.cpp" <<'EOF'
#include "unsigned_bits.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/unsigned-bits-cpp" \
    "$work/unsigned-bits-cpp/unsigned_bits.cpp" \
    "$work/unsigned-bits-cpp/main.cpp" -o "$work/unsigned-bits-cpp/app"
"$work/unsigned-bits-cpp/app"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/unsigned-bits-go" "$work/unsigned_bits.zi"
cat > "$work/unsigned-bits-go/main.go" <<'EOF'
package main
func main() { if UnsignedBits_Answer() != 42 { panic("wrong bits result") } }
EOF
GO111MODULE=off go run "$work/unsigned-bits-go/unsigned_bits.go" \
    "$work/unsigned-bits-go/main.go"
cat > "$work/u64_boundary.zi" <<'EOF'
#module "u64_boundary"
Answer :: () -> i32 #export {
    high: u64 = (u64)0x8000000000000000
    maximum: u64 = (u64)0xffffffffffffffff
    if high <= (u64)0x7fffffffffffffff { return 0 }
    if maximum <= high { return 0 }
    if maximum + (u64)1 != (u64)0 { return 0 }
    if high >> (u64)63 != (u64)1 { return 0 }
    if (u64)1 << (u64)63 != high { return 0 }
    if (maximum & high) != high { return 0 }
    if (maximum ^ high) != (u64)0x7fffffffffffffff { return 0 }
    value: u64 = maximum
    value -= (u64)41
    if value / (u64)2 != (u64)9223372036854775787 { return 0 }
    if (u32)maximum != (u32)0xffffffff { return 0 }
    return 42
}
EOF
"$ziran" ir --root "$work" -o "$work/u64-ir" "$work/u64_boundary.zi"
"$ziran" bundle --root "$work" --entry u64_boundary:Answer \
    -o "$work/u64.zib" "$work/u64_boundary.zi"
"$ziran" bundle --root "$work" --entry u64_boundary:Answer \
    -o "$work/u64-ir.zib" "$work/u64-ir/u64_boundary.zir"
cmp "$work/u64.zib" "$work/u64-ir.zib"
test "$("$ziran" run "$work/u64.zib")" = 42
test "$("$ziran" run "$work/u64-ir.zib")" = 42
for input in source ir; do
    if test "$input" = source; then
        extension=zi
        input_dir=$work
    else
        extension=zir
        input_dir=$work/u64-ir
    fi
    "$ziran" build --target=c --strict --root "$work" \
        -o "$work/u64-c-$input" "$input_dir/u64_boundary.$extension"
    cat > "$work/u64-c-$input/main.c" <<'C'
#include "u64_boundary.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
C
    ${CC:-cc} -Iinclude -I"$work/u64-c-$input" \
        "$work/u64-c-$input/u64_boundary.c" \
        "$work/u64-c-$input/main.c" -o "$work/u64-c-$input/app"
    "$work/u64-c-$input/app"
    "$ziran" build --target=cpp --strict --root "$work" \
        -o "$work/u64-cpp-$input" "$input_dir/u64_boundary.$extension"
    cat > "$work/u64-cpp-$input/main.cpp" <<'CPP'
#include "u64_boundary.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
CPP
    ${CXX:-c++} -Iinclude -I"$work/u64-cpp-$input" \
        "$work/u64-cpp-$input/u64_boundary.cpp" \
        "$work/u64-cpp-$input/main.cpp" -o "$work/u64-cpp-$input/app"
    "$work/u64-cpp-$input/app"
    "$ziran" build --target=go --strict --pkg main --root "$work" \
        -o "$work/u64-go-$input" "$input_dir/u64_boundary.$extension"
    cat > "$work/u64-go-$input/main.go" <<'GO'
package main
func main() { if U64Boundary_Answer() != 42 { panic("wrong u64 result") } }
GO
    GO111MODULE=off go run "$work/u64-go-$input/u64_boundary.go" \
        "$work/u64-go-$input/main.go"
done
python3 - "$work/flow-ir/flow.zir" "$work/invalid-flow.zir" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data.count(b'return 42') == 1
Path(sys.argv[2]).write_bytes(data.replace(b'return 42', b'return xx', 1))
PY
for target in c cpp go; do
    if "$ziran" build "--target=$target" --strict --root "$work" \
        -o "$work/invalid-$target" "$work/invalid-flow.zir" \
        2> "$work/invalid-$target.err"; then
        echo "invalid saved IR unexpectedly passed $target checking" >&2
        exit 1
    fi
    grep -Eq 'unknown|undeclared|unresolved' "$work/invalid-$target.err"
done
if "$ziran" bundle --root "$work" --entry flow:Answer \
    -o "$work/invalid-flow.zib" "$work/invalid-flow.zir" \
    2> "$work/invalid-bundle.err"; then
    echo 'invalid saved IR unexpectedly bundled' >&2
    exit 1
fi
grep -Eq 'unknown|undeclared|unresolved' "$work/invalid-bundle.err"
python3 - "$work/flow.zib" "$work/invalid-embedded.zib" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data.count(b'return 42') == 1
Path(sys.argv[2]).write_bytes(data.replace(b'return 42', b'return xx', 1))
PY
if "$ziran" run "$work/invalid-embedded.zib" \
    2> "$work/invalid-embedded.err"; then
    echo 'bundle with invalid semantic IR unexpectedly ran' >&2
    exit 1
fi
grep -Fq 'embedded ZIR failed semantic checking' "$work/invalid-embedded.err"
python3 - "$work/flow-ir/flow.zir" "$work/inconsistent-flow.zir" <<'PY'
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
assert data.count(b'42') == 2
position = data.rfind(b'42')
assert position > data.index(b'return 42') + len(b'return 42')
data[position:position + 2] = b'43'
Path(sys.argv[2]).write_bytes(data)
PY
for target in c cpp go; do
    if "$ziran" build "--target=$target" --strict --root "$work" \
        -o "$work/inconsistent-$target" "$work/inconsistent-flow.zir" \
        2> "$work/inconsistent-$target.err"; then
        echo "inconsistent saved IR unexpectedly passed $target checking" >&2
        exit 1
    fi
    grep -Fq 'saved IR does not match the checked program' \
        "$work/inconsistent-$target.err"
done
if "$ziran" bundle --root "$work" --entry flow:Answer \
    -o "$work/inconsistent-flow.zib" "$work/inconsistent-flow.zir" \
    2> "$work/inconsistent-bundle.err"; then
    echo 'inconsistent saved IR unexpectedly bundled' >&2
    exit 1
fi
grep -Fq 'saved IR does not match the checked program' \
    "$work/inconsistent-bundle.err"
python3 - "$work/flow.zib" "$work/inconsistent-embedded.zib" <<'PY'
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
assert data.count(b'42') == 2
data[data.rfind(b'42'):data.rfind(b'42') + 2] = b'43'
Path(sys.argv[2]).write_bytes(data)
PY
if "$ziran" run "$work/inconsistent-embedded.zib" \
    2> "$work/inconsistent-embedded.err"; then
    echo 'bundle with inconsistent IR unexpectedly ran' >&2
    exit 1
fi
grep -Fq 'saved IR does not match the checked program' \
    "$work/inconsistent-embedded.err"
python3 - "$work/app.zib" "$work/bad-bundle.zib" "$work/old-bundle.zib" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data[:8] == b'ZIB\0\x01\0\0\0', data[:8]
Path(sys.argv[2]).write_bytes(data[:17])
old = bytearray(data)
old[4] = 2
Path(sys.argv[3]).write_bytes(old)
PY
if "$ziran" run "$work/bad-bundle.zib" 2> "$work/bad-bundle.err"; then
    echo 'truncated bundle unexpectedly ran' >&2
    exit 1
fi
grep -Fq 'invalid or truncated bundle' "$work/bad-bundle.err"
if "$ziran" run "$work/old-bundle.zib" 2> "$work/old-bundle.err"; then
    echo 'unsupported bundle version unexpectedly ran' >&2
    exit 1
fi
grep -Fq 'unsupported ZIB version' "$work/old-bundle.err"
cat > "$work/unsupported-bundle.zi" <<'EOF'
#module "unsupported_bundle"
Answer :: () -> i32 #export {
    value: string = "hello"
    unused value
    return 42
}
EOF
if "$ziran" bundle --root "$work" --entry unsupported_bundle:Answer \
    -o "$work/unsupported-bundle.zib" "$work/unsupported-bundle.zi" \
    2> "$work/unsupported-bundle.err"; then
    echo 'unsupported portable data type unexpectedly bundled' >&2
    exit 1
fi
grep -Fq 'outside the portable subset' "$work/unsupported-bundle.err"
test ! -e "$work/unsupported-bundle.zib"

cat > "$work/blocklib.zi" <<'EOF'
#module "blocklib"
Props :: struct {
    value: i32
}
Button :: (props: Props) -> i32 #export {
    return props.value + 1
}
EOF
cat > "$work/blockapp.zi" <<'EOF'
#module "blockapp"
#import "blocklib"
Answer :: () -> i32 #export {
    Button: {
        value = 41
    }
    return 42
}
EOF
"$ziran" build --target=c --strict --root "$work" -o "$work/blocks" \
    "$work/blocklib.zi" "$work/blockapp.zi"
grep -Eq 'Button\(value_[0-9]+\)' "$work/blocks/blockapp.c"
cat > "$work/blocks/main.c" <<'EOF'
#include "blockapp.h"
int main(void) { return Answer() == 42 ? 0 : 1; }
EOF
${CC:-cc} -Iinclude -I"$work/blocks" "$work/blocks/blocklib.c" \
    "$work/blocks/blockapp.c" "$work/blocks/main.c" -o "$work/blocks/app"
"$work/blocks/app"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/blocks-go" "$work/blocklib.zi" "$work/blockapp.zi"
grep -Fq 'Blocklib_Button(' "$work/blocks-go/blockapp.go"
if grep -Fq 'github.com/waozixyz/kryon/go/kryon' \
    "$work/blocks-go/blocklib.go" "$work/blocks-go/blockapp.go"; then
    echo 'ordinary Go record call pulled in the legacy Kryon runtime' >&2
    exit 1
fi
cat > "$work/blocks-go/main.go" <<'EOF'
package main
func main() { if Blockapp_Answer() != 42 { panic("wrong result") } }
EOF
GO111MODULE=off go run "$work/blocks-go/blocklib.go" \
    "$work/blocks-go/blockapp.go" "$work/blocks-go/main.go"

"$ziran" ir --root "$work" -o "$work/blocks-ir" \
    "$work/blocklib.zi" "$work/blockapp.zi"
"$ziran" build --target=c --strict --root "$work" -o "$work/blocks-from-ir" \
    "$work/blocks-ir/blocklib.zir" "$work/blocks-ir/blockapp.zir"
cp "$work/blocks/main.c" "$work/blocks-from-ir/main.c"
${CC:-cc} -Iinclude -I"$work/blocks-from-ir" \
    "$work/blocks-from-ir/blocklib.c" "$work/blocks-from-ir/blockapp.c" \
    "$work/blocks-from-ir/main.c" -o "$work/blocks-from-ir/app"
"$work/blocks-from-ir/app"
"$ziran" build --target=go --strict --pkg main --root "$work" \
    -o "$work/blocks-go-ir" "$work/blocks-ir/blocklib.zir" \
    "$work/blocks-ir/blockapp.zir"
cp "$work/blocks-go/main.go" "$work/blocks-go-ir/main.go"
GO111MODULE=off go run "$work/blocks-go-ir/blocklib.go" \
    "$work/blocks-go-ir/blockapp.go" "$work/blocks-go-ir/main.go"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/blocks-cpp" \
    "$work/blocklib.zi" "$work/blockapp.zi"
cat > "$work/blocks-cpp/main.cpp" <<'EOF'
#include "blockapp.hpp"
int main() { return Answer() == 42 ? 0 : 1; }
EOF
${CXX:-c++} -Iinclude -I"$work/blocks-cpp" \
    "$work/blocks-cpp/blocklib.cpp" "$work/blocks-cpp/blockapp.cpp" \
    "$work/blocks-cpp/main.cpp" -o "$work/blocks-cpp/app"
"$work/blocks-cpp/app"
"$ziran" build --target=cpp --strict --root "$work" -o "$work/blocks-cpp-ir" \
    "$work/blocks-ir/blocklib.zir" "$work/blocks-ir/blockapp.zir"
cp "$work/blocks-cpp/main.cpp" "$work/blocks-cpp-ir/main.cpp"
${CXX:-c++} -Iinclude -I"$work/blocks-cpp-ir" \
    "$work/blocks-cpp-ir/blocklib.cpp" "$work/blocks-cpp-ir/blockapp.cpp" \
    "$work/blocks-cpp-ir/main.cpp" -o "$work/blocks-cpp-ir/app"
"$work/blocks-cpp-ir/app"

python3 - "$work/blocks-ir/blockapp.zir" "$work/corrupt.zir" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_bytes(data[:17])
PY
if "$ziran" build --target=c --root "$work" -o "$work/corrupt-out" \
    "$work/corrupt.zir" 2> "$work/corrupt.err"; then
    echo 'truncated IR unexpectedly passed' >&2
    exit 1
fi
grep -Eq 'truncated|invalid' "$work/corrupt.err"
python3 - "$work/blocks-ir/blockapp.zir" "$work/version.zir" <<'PY'
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
data[4] = 1
Path(sys.argv[2]).write_bytes(data)
PY
if "$ziran" build --target=c --root "$work" -o "$work/version-out" \
    "$work/version.zir" 2> "$work/version.err"; then
    echo 'unsupported IR version unexpectedly passed' >&2
    exit 1
fi
grep -Fq 'unsupported ZIR version' "$work/version.err"

cat > "$work/bad.zi" <<'EOF'
#module "bad"
#assert 0, "expected failure"
EOF
if "$ziran" check --diagnostics=json --root "$work" "$work/bad.zi" \
    2> "$work/diagnostic.json"; then
    echo 'invalid source unexpectedly passed' >&2
    exit 1
fi
python3 -m json.tool "$work/diagnostic.json" > /dev/null

cat > "$work/ui_mode.zi" <<'EOF'
Screen :: () #ui {
}
EOF
if "$ziran" check --root "$work" "$work/ui_mode.zi" \
    2> "$work/ui_mode.err"; then
    echo '#ui unexpectedly passed in Ziran' >&2
    exit 1
fi
grep -Fq '#ui is not a Ziran modifier' "$work/ui_mode.err"

cat > "$work/instance_mode.zi" <<'EOF'
#module "instance_mode"
State :: struct {
    value: i32
}
Main :: () {
    state: State #instance(7)
}
EOF
if "$ziran" check --root "$work" "$work/instance_mode.zi" \
    2> "$work/instance_mode.err"; then
    echo '#instance unexpectedly passed in Ziran' >&2
    exit 1
fi
grep -Fq '#instance is not a Ziran modifier' "$work/instance_mode.err"

cat > "$work/unknown_block.zi" <<'EOF'
#module "unknown_block"
Main :: () {
    Missing: {
        value = 41
    }
}
EOF
if "$ziran" check --root "$work" "$work/unknown_block.zi" \
    2> "$work/unknown_block.err"; then
    echo 'unknown block call unexpectedly passed' >&2
    exit 1
fi
grep -Fq 'unknown block-call declaration: Missing' "$work/unknown_block.err"
