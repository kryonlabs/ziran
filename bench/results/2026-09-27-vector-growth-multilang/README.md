# Provisional multilanguage vector-growth matrix

This run extends the vector-growth workload to handwritten C, C++, Go, Rust,
Java, JavaScript, and Python, alongside Ziran source and saved-IR output to C,
C++, Go, and portable `.zib`. It contains **206 validated samples across 50
phase/case combinations**. Every runtime sample matched the closed-form
checksum; generated source and saved-IR output remained identical, and the
source and saved-IR bundles were byte-identical.

At two million elements, median fresh-process times were: handwritten C 2.411
ms, Rust 2.292 ms, C++ 3.139 ms, generated C 3.190–3.586 ms, generated C++
3.321–3.489 ms, handwritten Go 7.638 ms, generated Go 9.639–9.764 ms, Java
32.083 ms, JavaScript 66.241 ms, and Python 149.008 ms. These numbers include
process startup and are not a general language ranking.

This was a provisional run over a dirty working tree that included unrelated
in-progress compiler edits. `metadata.json` records the exact HEAD, status,
source hashes, tool hashes, tool versions, commands, fixture hashes, and raw
samples needed to identify the measured tree. Re-run the committed harness on
a settled tree before treating these medians as a regression baseline.
