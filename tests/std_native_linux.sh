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
    text_bytes: [4100]u8
    index: s32 = 0
    while index < 4100 {
        text_bytes[index] = cast(u8)(65 + index % 26)
        index += 1
    }
    file = OpenReplace("cache/text.bin")
    if !file.valid ||
        WriteTextAt(file, TextFromBytes(text_bytes[:]), 0) != 4100 ||
        !CloseFile(file) { return 17 }
    file = OpenRead("cache/text.bin")
    text_again: [4100]u8
    if !file.valid || FileSize(file) != 4100 ||
        ReadAt(file, text_again[:], 0) != 4100 ||
        !CloseFile(file) { return 18 }
    index = 0
    while index < 4100 {
        if text_again[index] != text_bytes[index] { return 19 }
        index += 1
    }
    if !PathExists("cache/record.bin") ||
        DirectoryExists("cache/record.bin") ||
        !DirectoryExists("cache") ||
        PathExists("cache/moved.bin") { return 10 }
    if CreateDirectory("cache/record.bin") ||
        !CreatePrivateDirectory("private") ||
        !DirectoryExists("private") { return 12 }
    if !RenamePath("cache/record.bin", "cache/moved.bin") ||
        PathExists("cache/record.bin") ||
        !PathExists("cache/moved.bin") ||
        !RemoveFile("cache/moved.bin") ||
        PathExists("cache/moved.bin") { return 11 }
    file = OpenReplace("cache/old.bin")
    if !file.valid || !CloseFile(file) { return 13 }
    file = OpenReplace("cache/new.bin")
    if !file.valid || !CloseFile(file) { return 14 }
    if RenameNoReplace("cache/old.bin", "cache/new.bin") ||
        !PathExists("cache/old.bin") ||
        !PathExists("cache/new.bin") { return 15 }
    if !RemoveFile("cache/new.bin") ||
        !RenameNoReplace("cache/old.bin", "cache/new.bin") ||
        PathExists("cache/old.bin") ||
        !PathExists("cache/new.bin") { return 16 }
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
    "$work/c"/*.c "$root/build/libziran-runtime.a" -lm -o "$work/program"
(cd "$work" && ./program)
test "$(stat -c %a "$work/private")" = "700"
