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
"$ziran" ir --root "$work" -o "$work/ir" "$work/hello.zi"
"$ziran" ir --root "$work" -o "$work/ir-again" "$work/hello.zi"
cmp "$work/ir/hello.zir" "$work/ir-again/hello.zir"
test -s "$work/ir/hello.zir"
python3 - "$work/ir/hello.zir" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data[:8] == b'ZIR\0\x03\0\0\0', data[:8]
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
grep -Fq 'outside the portable scalar subset' "$work/unsupported-bundle.err"
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
