# Integer-kernel smoke baseline

Process wall time includes startup. Warm-cache medians; not a language ranking.

| Phase | Case | Median ms | Range ms | Samples |
|---|---|---:|---:|---:|
| cached go build | hand Go | 92.236 | 82.160–95.786 | 3 |
| cached go build | ziran go (saved) | 82.711 | 73.209–118.519 | 3 |
| cached go build | ziran go (source) | 78.777 | 58.190–118.957 | 3 |
| comparison compile | hand C | 18.986 | 18.309–20.529 | 3 |
| comparison compile | hand C++ | 28.418 | 27.578–29.424 | 3 |
| comparison compile | hand Java | 204.754 | 204.107–211.849 | 3 |
| comparison compile | hand Rust | 141.114 | 136.238–147.802 | 3 |
| downstream compile | ziran c (saved) | 33.351 | 33.106–35.957 | 3 |
| downstream compile | ziran c (source) | 33.237 | 33.127–35.048 | 3 |
| downstream compile | ziran cpp (saved) | 45.114 | 45.076–47.426 | 3 |
| downstream compile | ziran cpp (source) | 47.973 | 46.861–48.215 | 3 |
| run n=20000 | Java | 16.981 | 16.171–17.336 | 5 |
| run n=20000 | JavaScript | 13.528 | 13.281–14.349 | 5 |
| run n=20000 | Python | 8.485 | 8.248–8.841 | 5 |
| run n=20000 | hand C | 0.280 | 0.269–0.325 | 5 |
| run n=20000 | hand C++ | 0.345 | 0.256–0.436 | 5 |
| run n=20000 | hand Go | 0.705 | 0.541–0.817 | 5 |
| run n=20000 | hand Rust | 0.439 | 0.362–0.627 | 5 |
| run n=20000 | ziran VM (saved) | 9.618 | 9.366–10.140 | 5 |
| run n=20000 | ziran VM (source) | 9.449 | 9.284–9.810 | 5 |
| run n=20000 | ziran c (saved) | 0.462 | 0.323–0.620 | 5 |
| run n=20000 | ziran c (source) | 0.329 | 0.272–0.549 | 5 |
| run n=20000 | ziran cpp (saved) | 0.480 | 0.331–0.697 | 5 |
| run n=20000 | ziran cpp (source) | 0.299 | 0.259–0.406 | 5 |
| run n=20000 | ziran go (saved) | 0.784 | 0.577–0.868 | 5 |
| run n=20000 | ziran go (source) | 0.791 | 0.739–1.053 | 5 |
| run n=2000000 | Java | 23.180 | 22.042–33.053 | 5 |
| run n=2000000 | JavaScript | 44.405 | 43.823–49.438 | 5 |
| run n=2000000 | Python | 181.452 | 180.360–184.302 | 5 |
| run n=2000000 | hand C | 5.408 | 5.371–5.429 | 5 |
| run n=2000000 | hand C++ | 5.392 | 5.212–6.209 | 5 |
| run n=2000000 | hand Go | 5.914 | 5.656–6.094 | 5 |
| run n=2000000 | hand Rust | 5.266 | 5.135–5.682 | 5 |
| run n=2000000 | ziran c (saved) | 5.428 | 5.329–5.887 | 5 |
| run n=2000000 | ziran c (source) | 5.606 | 5.268–6.214 | 5 |
| run n=2000000 | ziran cpp (saved) | 5.399 | 5.245–5.741 | 5 |
| run n=2000000 | ziran cpp (source) | 5.281 | 5.238–5.796 | 5 |
| run n=2000000 | ziran go (saved) | 14.557 | 14.316–14.848 | 5 |
| run n=2000000 | ziran go (source) | 14.737 | 14.244–18.846 | 5 |
| ziran | saved IR check | 0.379 | 0.367–0.385 | 3 |
| ziran | saved to c | 0.647 | 0.633–0.710 | 3 |
| ziran | saved to cpp | 0.650 | 0.583–0.654 | 3 |
| ziran | saved to go | 0.772 | 0.602–0.882 | 3 |
| ziran | saved to zib | 0.514 | 0.495–0.665 | 3 |
| ziran | source check | 0.639 | 0.590–0.858 | 3 |
| ziran | source to c | 0.890 | 0.769–1.065 | 3 |
| ziran | source to cpp | 0.868 | 0.832–1.084 | 3 |
| ziran | source to go | 0.984 | 0.965–1.009 | 3 |
| ziran | source to saved IR | 0.653 | 0.633–0.720 | 3 |
| ziran | source to zib | 0.972 | 0.870–1.214 | 3 |

Checksums: {"20000": 1799947492, "2000000": 2108566504}

Generated source identity: {"c": true, "cpp": true, "go": true}

Source/saved bundles are byte-identical. All runtime samples matched the independent modular-series oracle.

- Smoke baseline of one integer modular recurrence, not a language ranking.
- Every run is a fresh process; timings include startup, loading, printing and shutdown.
- Java and Node are not steady-state warmed JIT measurements; only filesystem/toolchain caches are warm.
- C/C++ use -O2 without LTO or march=native; Rust uses opt-level=2; Go uses defaults.
- Python arbitrary precision and JavaScript exact integer-valued Number arithmetic differ in representation.
- All products stay below 2^53 and signed 64-bit range, so results are exactly comparable.
- Zi check/emit are whole invocations, not disaggregated internal compiler phases.
- Saved IR is checked again; source-to-IR cost is separate, not included in saved-IR emit.
- Go uses a dedicated warmed build cache, including the unchanged program itself: these are cached go build invocations, not forced recompiles comparable to GCC.
- VM small input only: 1,000,000-step execution limit prevents the large workload.
- No CPU pinning/frequency isolation; background system activity may affect samples.
- Raw wait4 RSS can include the inherited Python launcher high-water floor; it is launch accounting, not comparable executable peak memory. RSS is omitted from the displayed table.
- Bend, Zig, Clang, and GPU execution are outside this smoke matrix; none are approximated.
