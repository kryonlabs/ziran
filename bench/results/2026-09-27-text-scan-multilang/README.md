# Multilanguage UTF-8 scan matrix

This run measures a fixed `aé中🙂` workload (10 UTF-8 bytes, 4 Unicode scalar
values) from Ziran source and saved IR to C, C++, Go, and portable `.zib`,
with handwritten C, C++, Go, Rust, Java, JavaScript, and Python comparisons.
It contains **206 validated samples across 50 phase/case
combinations**. Every runtime sample returned `148921 * rounds`; generated C,
C++, and Go were identical for source and saved IR, and the two `.zib`
bundles were byte-identical.

At two million rounds, median fresh-process times were: Rust 5.401 ms,
handwritten C 10.253 ms, handwritten C++ 10.234 ms, handwritten Go 13.339 ms,
generated C 62.416–62.593 ms, generated C++ 62.438–62.569 ms, Java 33.663 ms,
JavaScript 48.036 ms, generated Go 301.714–303.533 ms, and Python 562.911 ms.
The explicit Ziran/C/C++ decoder validates multibyte structure on every
scalar, while Go, Rust, Java, JavaScript, and Python use their native
codepoint iterators. These whole-process numbers are not a general language
ranking.

The run used commit `eb5fe437a23eb3bc55b1de43776b9b6be0e95bbb`. The working
tree still contained unrelated site/logo edits, which are recorded by
`metadata.json`; the compiler, standard library, include files, Makefile, and
harness hashes are also recorded. Raw commands, timings, tool versions,
fixture hashes, artifact hashes, and caveats are retained alongside this
summary.

Reproduce it with:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY python3 bench/text_scan.py
```
