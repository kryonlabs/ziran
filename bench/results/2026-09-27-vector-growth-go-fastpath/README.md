# Go arithmetic fast-path comparison

On 2026-09-27, a paired run of the 2,000,000-element vector-growth workload
measured **25.682 ms before** and **12.900 ms after** (0.502× elapsed time)
for generated Go. Each median uses 20 shuffled, fresh-process samples, with
two warmups per binary. Every sample returned the independent checksum
`73999985000000`. The [raw samples and binary hashes](paired_samples.json)
include the host description and random seed. The Go toolchain was
`go1.24.4 linux/amd64`.

The [generated source before](before.go) and [after](after.go), with only
trailing blank lines removed from these copies, differ in the added small
arithmetic helpers and replacement of general numeric-helper
calls for wrapping addition and multiplication in this workload. The source
and saved-IR routes generated identical Go files in each full harness run.
The original [baseline](../2026-09-27-vector-growth/benchmark.md) records
the pre-change compiler and workload hashes. The same
[`bench/vector_growth.py`](../../vector_growth.py) harness produced both
binaries. The paired comparison can be repeated with
[`bench/vector_growth_ab.py`](../../vector_growth_ab.py) and paths to the
two built executables.

These are whole-process timings on a shared machine without CPU isolation or
frequency control. They establish a focused improvement for this workload,
not a general speed ranking. The full harness run after the change was noisy;
the paired run controls run order and uses more execution samples.
