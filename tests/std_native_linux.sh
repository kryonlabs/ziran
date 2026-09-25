#!/bin/sh
set -eu

ziran=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/std_native_linux.zi" <<'ZI'
#import "binary_linux"
#import "byte_text_linux"
#import "date_time_linux"
#import "file_linux"
#import "process_capture_linux"

#program_export
main :: () -> s32 {
    bytes: [16]u8
    if !WriteU64LE(bytes[:], 0, 123456789) ||
        !WriteF64LE(bytes[:], 8, 456.25) { return 1 }
    if ReadU64LE(bytes[:], 0) != 123456789 ||
        ReadF64LE(bytes[:], 8) != 456.25 { return 2 }
    if !CreateDirectory("cache") { return 9 }
    file: FileHandle = OpenReplace("cache/record.bin")
    if !file.valid || WriteAt(file, bytes[:], 0) != 16 ||
        !CloseFile(file) { return 3 }
    file = OpenRead("cache/record.bin")
    if !file.valid || FileSize(file) != 16 { return 4 }
    again: [16]u8
    if ReadAt(file, again[:], 0) != 16 || !CloseFile(file) ||
        ReadU64LE(again[:], 0) != 123456789 ||
        ReadF64LE(again[:], 8) != 456.25 { return 5 }
    letters: [3]u8 = .[88, 77, 82]
    if TextFromBytes(letters[:]) != "XMR" { return 6 }
    args: [2]string = .["printf", "ok"]
    output: [16]u8
    captured: CaptureResult = CaptureOutput(args[:], output[:])
    if captured.error || captured.truncated || captured.code != 0 ||
        captured.length != 2 || TextFromBytes(output[0:2]) != "ok" {
        return 7
    }
    if UnixNow() < 1700000000 { return 8 }
    return 0
}
ZI

"$ziran" build --target=c --entry std_native_linux:main \
    --root "$work" --module-path "$root/std" \
    -o "$work/c" "$work/std_native_linux.zi"
"${CC:-cc}" -std=c99 -pedantic-errors -I"$root/include" -I"$work/c" \
    "$work/c"/*.c -lm -o "$work/program"
(cd "$work" && ./program)
