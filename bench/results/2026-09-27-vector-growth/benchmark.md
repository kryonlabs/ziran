# Collection-growth benchmark

Fresh-process wall time; validated output for every sample. Not a language ranking.

| Phase | Case | Median ms | Range ms | Samples |
|---|---|---:|---:|---:|
| cached go build | hand Go | 85.392 | 64.094–128.711 | 3 |
| cached go build | ziran go (saved) | 93.564 | 75.821–117.937 | 3 |
| cached go build | ziran go (source) | 97.762 | 71.952–114.191 | 3 |
| comparison compile | hand C | 20.502 | 20.395–20.762 | 3 |
| comparison compile | hand C++ | 88.080 | 83.052–98.275 | 3 |
| downstream compile | ziran c (saved) | 41.641 | 41.430–44.578 | 3 |
| downstream compile | ziran c (source) | 39.382 | 39.056–40.378 | 3 |
| downstream compile | ziran cpp (saved) | 49.882 | 49.834–51.118 | 3 |
| downstream compile | ziran cpp (source) | 51.374 | 48.659–53.162 | 3 |
| run n=2000000 | hand C | 2.401 | 2.185–2.592 | 5 |
| run n=2000000 | hand C++ | 3.454 | 2.666–3.842 | 5 |
| run n=2000000 | hand Go | 7.939 | 6.881–8.366 | 5 |
| run n=2000000 | ziran c (saved) | 3.291 | 3.224–3.831 | 5 |
| run n=2000000 | ziran c (source) | 3.349 | 3.258–3.577 | 5 |
| run n=2000000 | ziran cpp (saved) | 3.584 | 3.229–3.830 | 5 |
| run n=2000000 | ziran cpp (source) | 3.388 | 3.052–3.597 | 5 |
| run n=2000000 | ziran go (saved) | 21.382 | 20.686–23.254 | 5 |
| run n=2000000 | ziran go (source) | 21.950 | 20.303–22.643 | 5 |
| run n=4000 | hand C | 0.287 | 0.225–0.459 | 5 |
| run n=4000 | hand C++ | 0.584 | 0.460–0.779 | 5 |
| run n=4000 | hand Go | 0.626 | 0.569–0.870 | 5 |
| run n=4000 | ziran VM (saved) | 5.673 | 4.993–6.630 | 5 |
| run n=4000 | ziran VM (source) | 5.127 | 4.949–5.223 | 5 |
| run n=4000 | ziran c (saved) | 0.328 | 0.217–0.517 | 5 |
| run n=4000 | ziran c (source) | 0.253 | 0.247–0.414 | 5 |
| run n=4000 | ziran cpp (saved) | 0.254 | 0.203–0.555 | 5 |
| run n=4000 | ziran cpp (source) | 0.268 | 0.211–0.428 | 5 |
| run n=4000 | ziran go (saved) | 0.544 | 0.519–0.619 | 5 |
| run n=4000 | ziran go (source) | 0.647 | 0.496–0.765 | 5 |
| ziran | saved IR check | 0.437 | 0.419–0.450 | 3 |
| ziran | saved to c | 0.744 | 0.721–0.895 | 3 |
| ziran | saved to cpp | 0.784 | 0.740–0.993 | 3 |
| ziran | saved to go | 0.985 | 0.950–1.037 | 3 |
| ziran | saved to zib | 0.834 | 0.677–0.975 | 3 |
| ziran | source check | 0.729 | 0.669–0.881 | 3 |
| ziran | source to c | 0.854 | 0.852–0.885 | 3 |
| ziran | source to cpp | 1.092 | 1.050–1.145 | 3 |
| ziran | source to go | 1.013 | 0.951–1.503 | 3 |
| ziran | source to saved IR | 0.720 | 0.702–0.823 | 3 |
| ziran | source to zib | 1.194 | 1.091–1.355 | 3 |

Expected checksums: {"4000": 295970000, "2000000": 73999985000000}

Generated source equality: {"c": true, "cpp": true, "go": true}

Source and saved-IR bundles are byte-identical.

- Fresh processes include startup, loading, printing, and shutdown.
- Go build uses a dedicated warm cache; it is not a forced recompilation.
- C uses a doubling array, C++ uses std::vector, Go uses append.
- No CPU isolation; medians are not regression thresholds.
- VM large run omitted because its instruction budget is too small.
- RSS from wait4 includes launcher accounting and is omitted from the table.
