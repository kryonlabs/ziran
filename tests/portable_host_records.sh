#!/bin/sh
set -eu

ziran=$1
host_test=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/shapes.zi" <<'EOF'
Tone :: enum {
    ToneNormal = 0
    ToneAccent = 1
}
Point :: struct {
    x: s32
    y: s32
}
Packet :: struct {
    point: Point
    label: string
    tone: Tone
}
EOF
cat > "$work/record_host.zi" <<'EOF'
host_api :: #system_library "host_api";
#import "shapes"
TransformHost :: (packet: Packet) -> Packet #foreign host_api;
#program_export
Answer :: () -> s32 {
    packet: Packet
    packet.point.x = 40
    packet.point.y = 1
    packet.label = "in"
    packet.tone = cast(Tone)ToneNormal
    transformed: Packet = TransformHost(packet)
    if transformed.point.x != 41 || transformed.point.y != 1 ||
       transformed.label != "ok" ||
       transformed.tone != cast(Tone)ToneAccent { return 0 }
    return 42
}
EOF

"$ziran" ir --root "$work" -o "$work/ir" "$work/record_host.zi"
"$ziran" bundle --root "$work" --entry record_host:Answer \
    -o "$work/source.zib" "$work/record_host.zi"
"$ziran" bundle --root "$work/ir" --entry record_host:Answer \
    -o "$work/saved.zib" "$work/ir/record_host.zir"
cmp "$work/source.zib" "$work/saved.zib"
"$host_test" "$work/source.zib"
"$host_test" "$work/saved.zib"

for input in source saved; do
    if test "$input" = source; then
        module=$work/record_host.zi
        root=$work
    else
        module=$work/ir/record_host.zir
        root=$work/ir
    fi
    for target in c cpp go; do
        output=$work/$target-$input
        "$ziran" build --target="$target" --strict --root "$root" \
            -o "$output" "$module"
        if test "$target" = c; then
            cat > "$output/main.c" <<'C'
#include "record_host.h"
Packet TransformHost(Packet packet) {
    packet.point.x += 1;
    packet.label = StringView("ok", 2);
    packet.tone = ToneAccent;
    return packet;
}
int main(void) { return Answer() == 42 ? 0 : 1; }
C
            "${CC:-cc}" -std=c11 -I"$output" -I"$(dirname "$ziran")/../../include" \
                "$output"/*.c -o "$output/app"
            "$output/app"
        elif test "$target" = cpp; then
            cat > "$output/main.cpp" <<'CPP'
#include "record_host.hpp"
extern "C" Packet TransformHost(Packet packet) {
    packet.point.x += 1;
    packet.label = StringView("ok", 2);
    packet.tone = ToneAccent;
    return packet;
}
int main() { return Answer() == 42 ? 0 : 1; }
CPP
            "${CXX:-c++}" -std=c++17 -I"$output" \
                -I"$(dirname "$ziran")/../../include" \
                "$output"/*.cpp -o "$output/app"
            "$output/app"
        else
            cat > "$output/main.go" <<'GO'
package ziran
type testHost struct{}
func (testHost) TransformHost(packet Packet) Packet {
    packet.Point.X++
    packet.Label = "ok"
    packet.Tone = ToneAccent
    return packet
}
func init() { SetRecordHostHost(testHost{}) }
GO
            cat > "$output/record_host_test.go" <<'GO'
package ziran
import "testing"
func TestRecordHost(t *testing.T) {
    if RecordHost_Answer() != 42 { t.Fatal("record host result") }
}
GO
            GO111MODULE=off go test "$output"/*.go
        fi
    done
done
