# UTF-8 scan benchmark

Fresh-process wall time; validated output for every sample. Not a language ranking.

| Phase | Case | Median ms | Range ms | Samples |
|---|---|---:|---:|---:|
| cached go build | hand Go | 100.914 | 72.113–119.295 | 3 |
| cached go build | ziran go (saved) | 68.330 | 54.631–87.149 | 3 |
| cached go build | ziran go (source) | 81.653 | 80.634–91.277 | 3 |
| comparison compile | hand C | 23.438 | 23.293–24.872 | 3 |
| comparison compile | hand C++ | 32.453 | 32.004–32.921 | 3 |
| comparison compile | hand Java | 176.296 | 173.229–192.365 | 3 |
| comparison compile | hand Rust | 137.078 | 137.010–138.157 | 3 |
| downstream compile | ziran c (saved) | 60.797 | 60.545–61.844 | 3 |
| downstream compile | ziran c (source) | 61.982 | 60.136–63.500 | 3 |
| downstream compile | ziran cpp (saved) | 78.388 | 78.192–79.637 | 3 |
| downstream compile | ziran cpp (source) | 77.818 | 76.690–79.891 | 3 |
| run rounds=1000 | JavaScript | 12.812 | 12.633–13.590 | 5 |
| run rounds=1000 | Python | 7.223 | 6.605–9.402 | 5 |
| run rounds=1000 | hand C | 0.444 | 0.252–0.454 | 5 |
| run rounds=1000 | hand C++ | 0.283 | 0.272–0.409 | 5 |
| run rounds=1000 | hand Go | 0.563 | 0.469–0.871 | 5 |
| run rounds=1000 | hand Java | 15.640 | 15.399–17.637 | 5 |
| run rounds=1000 | hand Rust | 0.380 | 0.331–0.438 | 5 |
| run rounds=1000 | ziran VM (saved) | 34.148 | 33.964–35.805 | 5 |
| run rounds=1000 | ziran VM (source) | 34.452 | 33.480–35.227 | 5 |
| run rounds=1000 | ziran c (saved) | 0.324 | 0.257–0.534 | 5 |
| run rounds=1000 | ziran c (source) | 0.384 | 0.348–0.418 | 5 |
| run rounds=1000 | ziran cpp (saved) | 0.373 | 0.295–0.390 | 5 |
| run rounds=1000 | ziran cpp (source) | 0.334 | 0.251–0.438 | 5 |
| run rounds=1000 | ziran go (saved) | 0.798 | 0.656–0.817 | 5 |
| run rounds=1000 | ziran go (source) | 0.775 | 0.679–0.827 | 5 |
| run rounds=2000000 | JavaScript | 48.036 | 45.294–49.539 | 5 |
| run rounds=2000000 | Python | 562.911 | 532.142–582.338 | 5 |
| run rounds=2000000 | hand C | 10.253 | 9.806–10.379 | 5 |
| run rounds=2000000 | hand C++ | 10.234 | 9.664–10.788 | 5 |
| run rounds=2000000 | hand Go | 13.339 | 12.380–13.698 | 5 |
| run rounds=2000000 | hand Java | 33.663 | 31.636–37.438 | 5 |
| run rounds=2000000 | hand Rust | 5.401 | 4.910–6.489 | 5 |
| run rounds=2000000 | ziran c (saved) | 62.593 | 61.743–68.088 | 5 |
| run rounds=2000000 | ziran c (source) | 62.416 | 61.971–63.407 | 5 |
| run rounds=2000000 | ziran cpp (saved) | 62.569 | 61.084–63.450 | 5 |
| run rounds=2000000 | ziran cpp (source) | 62.438 | 61.478–62.963 | 5 |
| run rounds=2000000 | ziran go (saved) | 301.714 | 289.545–308.042 | 5 |
| run rounds=2000000 | ziran go (source) | 303.533 | 299.399–310.638 | 5 |
| ziran | saved IR check | 0.977 | 0.971–0.987 | 3 |
| ziran | saved to c | 2.593 | 2.163–4.319 | 3 |
| ziran | saved to cpp | 2.131 | 1.993–2.435 | 3 |
| ziran | saved to go | 2.588 | 2.035–2.952 | 3 |
| ziran | saved to zib | 2.362 | 2.079–2.918 | 3 |
| ziran | source check | 2.399 | 2.004–2.440 | 3 |
| ziran | source to c | 3.722 | 3.459–3.897 | 3 |
| ziran | source to cpp | 3.848 | 3.826–3.935 | 3 |
| ziran | source to go | 3.742 | 2.828–4.061 | 3 |
| ziran | source to saved IR | 2.368 | 2.060–2.370 | 3 |
| ziran | source to zib | 3.382 | 2.923–3.507 | 3 |

Expected checksums: {"1000": 148921000, "2000000": 297842000000}

Generated source equality: {"c": true, "cpp": true, "go": true}

Source and saved-IR bundles are byte-identical.

Unavailable comparisons: none

- Fresh processes include startup, loading, printing, and shutdown.
- Go build uses a dedicated warm cache; it is not a forced recompilation.
- Ziran, C, and C++ use explicit UTF-8 decoders; Go, Rust, Java, JavaScript, and Python use their native codepoint iteration.
- No CPU isolation; medians are not regression thresholds.
- VM large run omitted because its instruction budget is too small.
- RSS from wait4 includes launcher accounting and is omitted from the table.
