# Cross-target smoke benchmark

From the repository root, build the current toolchain and run:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY make -j4 all
env -u DISPLAY -u WAYLAND_DISPLAY python3 bench/smoke.py
```

The script creates a new directory under `build/benchmarks/` with `metadata.json`,
`samples.jsonl`, `summary.json`, and `benchmark.md`. Pass `--out PATH` to select
a new output directory. `--compile-repetitions` and `--run-repetitions` adjust
the measured sample counts; each case also has one discarded warmup. The run
requires GCC, G++, Go and the Ziran tools. Rust, Java and Node comparisons are
included when their toolchains are present, and absences are recorded.

One integer recurrence is built from source and saved IR to C, C++, Go, and
portable `.zib`. The harness checks that generated source and bundles match,
and validates every runtime result against an independent modular-series
oracle before reporting times. It separately measures Ziran check/emission,
downstream native compilation, and whole-process execution. Runtime cases are
shuffled with a fixed seed between sample rounds. The portable VM runs only
the small input because its instruction budget excludes the large one.

These numbers are a smoke baseline, not a language ranking. They include
process startup; Go build measurements use a warm build cache and are labeled
accordingly. JIT languages are launched fresh rather than timed at warmed
steady state. The output records compiler revisions, source and tool hashes,
tool versions, commands, fixture hashes, raw timings, and limitations so that
a comparison can be checked against the exact experiment. A broader suite
still needs arrays, records, generics, collections, text processing, larger
module graphs, and real applications.

The first repeated baseline, including raw samples and machine metadata, is
in [`results/2026-09-27`](results/2026-09-27/benchmark.md). It used three
measured compile samples and five measured runtime samples per case on one
machine; its medians are not regression thresholds.

For a second workload that grows a `Vec(s32)` and then scans it, run:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY python3 bench/vector_growth.py
```

This measures source and saved-IR checks and emissions to C, C++, Go, and
portable `.zib`, downstream native builds, and whole-process execution.
Handwritten C, C++, and Go versions provide algorithm-level comparisons. The
checksum uses a closed-form arithmetic oracle, and every runtime sample must
match it. The portable VM runs the small input only. Raw commands, samples,
source and tool hashes, and caveats are written alongside the report. These
workloads remain a smoke and collection baseline; they do not cover text,
records, generics, larger module graphs, or real applications.

The [vector-growth baseline](results/2026-09-27-vector-growth/benchmark.md)
contains three measured compilation and five measured execution samples per
case on one machine. The generated C and C++ runs were close to their
handwritten comparisons for the large input; generated Go took about 22 ms
versus 8 ms for handwritten Go. This identifies a workload for investigating
vector lowering, not a general performance ranking.
