# Collection-growth benchmark

Fresh-process wall time; validated output for every sample. Not a language ranking.

| Phase | Case | Median ms | Range ms | Samples |
|---|---|---:|---:|---:|
| cached go build | hand Go | 68.668 | 35.013–71.412 | 3 |
| cached go build | ziran go (saved) | 109.549 | 77.479–123.141 | 3 |
| cached go build | ziran go (source) | 67.344 | 62.767–124.380 | 3 |
| comparison compile | hand C | 21.045 | 20.580–22.191 | 3 |
| comparison compile | hand C++ | 89.919 | 88.730–91.624 | 3 |
| comparison compile | hand Java | 190.464 | 188.305–197.246 | 3 |
| comparison compile | hand Rust | 152.251 | 151.828–154.558 | 3 |
| downstream compile | ziran c (saved) | 44.360 | 43.306–44.678 | 3 |
| downstream compile | ziran c (source) | 42.850 | 42.765–44.429 | 3 |
| downstream compile | ziran cpp (saved) | 53.790 | 53.662–56.672 | 3 |
| downstream compile | ziran cpp (source) | 53.991 | 52.135–56.825 | 3 |
| run n=2000000 | JavaScript | 66.241 | 65.686–71.812 | 5 |
| run n=2000000 | Python | 149.008 | 143.839–158.322 | 5 |
| run n=2000000 | hand C | 2.411 | 1.737–2.552 | 5 |
| run n=2000000 | hand C++ | 3.139 | 2.910–4.258 | 5 |
| run n=2000000 | hand Go | 7.638 | 7.194–10.483 | 5 |
| run n=2000000 | hand Java | 32.083 | 22.377–35.255 | 5 |
| run n=2000000 | hand Rust | 2.292 | 1.819–2.571 | 5 |
| run n=2000000 | ziran c (saved) | 3.586 | 3.449–3.828 | 5 |
| run n=2000000 | ziran c (source) | 3.190 | 2.896–3.885 | 5 |
| run n=2000000 | ziran cpp (saved) | 3.489 | 3.203–3.625 | 5 |
| run n=2000000 | ziran cpp (source) | 3.321 | 3.303–4.204 | 5 |
| run n=2000000 | ziran go (saved) | 9.639 | 9.210–11.275 | 5 |
| run n=2000000 | ziran go (source) | 9.764 | 9.432–9.972 | 5 |
| run n=4000 | JavaScript | 12.532 | 11.780–13.965 | 5 |
| run n=4000 | Python | 7.062 | 6.365–7.782 | 5 |
| run n=4000 | hand C | 0.321 | 0.238–0.399 | 5 |
| run n=4000 | hand C++ | 0.639 | 0.502–1.216 | 5 |
| run n=4000 | hand Go | 0.575 | 0.484–0.690 | 5 |
| run n=4000 | hand Java | 14.803 | 14.228–16.013 | 5 |
| run n=4000 | hand Rust | 0.343 | 0.304–0.623 | 5 |
| run n=4000 | ziran VM (saved) | 4.983 | 4.892–5.239 | 5 |
| run n=4000 | ziran VM (source) | 5.221 | 4.894–5.292 | 5 |
| run n=4000 | ziran c (saved) | 0.319 | 0.285–0.505 | 5 |
| run n=4000 | ziran c (source) | 0.283 | 0.224–0.344 | 5 |
| run n=4000 | ziran cpp (saved) | 0.280 | 0.235–0.333 | 5 |
| run n=4000 | ziran cpp (source) | 0.326 | 0.227–0.464 | 5 |
| run n=4000 | ziran go (saved) | 0.574 | 0.557–0.652 | 5 |
| run n=4000 | ziran go (source) | 0.552 | 0.519–0.628 | 5 |
| ziran | saved IR check | 0.434 | 0.422–0.439 | 3 |
| ziran | saved to c | 0.814 | 0.766–0.912 | 3 |
| ziran | saved to cpp | 0.684 | 0.670–0.721 | 3 |
| ziran | saved to go | 0.838 | 0.799–0.874 | 3 |
| ziran | saved to zib | 0.784 | 0.777–0.788 | 3 |
| ziran | source check | 0.673 | 0.655–0.699 | 3 |
| ziran | source to c | 1.090 | 0.930–1.304 | 3 |
| ziran | source to cpp | 1.252 | 1.081–1.403 | 3 |
| ziran | source to go | 0.978 | 0.852–1.041 | 3 |
| ziran | source to saved IR | 0.763 | 0.724–0.913 | 3 |
| ziran | source to zib | 1.124 | 1.030–1.174 | 3 |

Expected checksums: {"4000": 295970000, "2000000": 73999985000000}

Generated source equality: {"c": true, "cpp": true, "go": true}

Source and saved-IR bundles are byte-identical.

Unavailable comparisons: none

- Fresh processes include startup, loading, printing, and shutdown.
- Go build uses a dedicated warm cache; it is not a forced recompilation.
- C and Java use doubling arrays; C++, Go, Rust, JavaScript, and Python use growable collections.
- JavaScript uses BigInt for the sum to preserve exact checksums.
- No CPU isolation; medians are not regression thresholds.
- VM large run omitted because its instruction budget is too small.
- RSS from wait4 includes launcher accounting and is omitted from the table.
