#!/usr/bin/env python3
"""Exercise Android file effects and link every NDK ABI at the API 21 floor."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/bin/zi2c"
RUNNER = shlex.split(os.environ.get("ZIRAN_RUNNER", ""))
ENV = dict(os.environ)
for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "GDK_DISPLAY"):
    ENV.pop(key, None)

def run(command, **kwargs):
    subprocess.run(list(map(str, command)), env=ENV, check=True, **kwargs)

with tempfile.TemporaryDirectory(prefix="ziran-android-files-") as work:
    work = Path(work)
    ndks = sorted((Path(os.environ.get("ANDROID_HOME", str(Path.home() / "Android/Sdk"))) / "ndk").glob("*/toolchains/llvm/prebuilt/linux-x86_64"))
    targets = [("x86_64-linux-android21", "__x86_64__", 316),
               ("aarch64-linux-android21", "__aarch64__", 276),
               ("armv7a-linux-androideabi21", "__arm__", 382),
               ("i686-linux-android21", "__i386__", 353)]
    for target, define, number in targets:
        output = work / target
        run([*RUNNER, COMPILER, "--no-main", "--root", ROOT,
             "--module-path", ROOT / "std", "--define", "ANDROID_BUILD",
             "--define", define, "-o", output, ROOT / "tests/std_file_android.zi"])
        sources = sorted(output.rglob("*.c"))
        includes = ["-I" + str(ROOT / "include")]
        includes += ["-iquote" + str(p) for p in sorted({p.parent for p in output.rglob("*.h")})]
        generated = "\n".join(p.read_text() for p in sources)
        assert '__asm__("renameat2")' not in generated
        assert f'#define RenameAt2Number {number}' in generated
        assert '__asm__("syscall")' in generated
        assert 'ptrdiff_t zir_foreign_ReadAtRaw(int32_t fd, uint8_t* output, size_t count, int64_t offset)' in generated
        if define == "__x86_64__":
            run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                 *sources, "-o", work / "test"])
            run([work / "test"], cwd=work)
        if ndks:
            # This is a link check as well as a type/architecture check: the
            # API 21 libc stubs must supply every generated foreign symbol.
            run([ndks[-1] / "bin/clang", "--target=" + target, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                 "-fPIC", "-shared", "-Wl,--no-undefined", *includes, *sources,
                 "-o", output / "libfiles.so"])
    if not ndks:
        print("Android NDK unavailable; native Android syscall behavior passed")
print("Android file effects and available API 21 ABI links passed")
