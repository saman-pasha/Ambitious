# Benchmark: Ambitious against Chromium

*The performance gate of PLAN.md §11. `make bench` reruns the page rows. `QJS=path/to/qjsrun python3 tools/bench/js/run.py` reruns the JavaScript rows.*

Ambitious and the reference engines run on the same box, on the same pages:
- **The pages.** The four test pages, and `tools/gen-big-page.py` at 20 sections (medium.html) and at 200 (large.html).
- **The reference.** Chromium 1243, Playwright's headless shell, in an 800x600 viewport at scale 1.
- **Chromium's stages** are the durations of its trace events: ParseHTML; then UpdateLayoutTree and ParseAuthorStyleSheet; Layout; and PrePaint, Paint and Layerize, with the raster threads' RasterTask.
- **Ambitious's stages** are `ambitious bench`'s timers.
- **The two engines are built differently.** Chromium uses several threads and processes. Ambitious uses one thread, built at -O2 for the generic x86-64 CPU.

## The first run (before the changes of PLAN.md §11)

Measured by `python3 tools/bench/compare.py 5` on Intel(R) Xeon(R) Processor @ 2.10GHz (4 CPUs), Linux-6.18.44-fc-v378-x86_64-with-glibc2.39.

| page | bytes | DOM nodes (Ambitious) | elements (Chromium) |
|---|---:|---:|---:|
| news.html | 466 | 50 | 22 |
| layout.html | 2,257 | 101 | 44 |
| style.html | 1,511 | 60 | 25 |
| a11y.html | 1,666 | 123 | 56 |
| medium.html | 28,451 | 1,632 | 746 |
| large.html | 248,919 | 16,212 | 7,406 |

### The stages (ms)

Ambitious's timers against the durations of Chromium's trace events for the same work.

| page | stage | Ambitious | Chromium | faster |
|---|---|---:|---:|---|
| news.html | parse | 0.07 | 0.38 | **Ambitious** (5.5x) |
| news.html | style | 0.64 | 0.49 | Chromium (1.3x) |
| news.html | layout | 1.48 | 1.42 | Chromium (1.0x) |
| news.html | paint and raster | 4.04 | 3.04 | Chromium (1.3x) |
| layout.html | parse | 0.09 | 0.46 | **Ambitious** (5.2x) |
| layout.html | style | 0.83 | 0.46 | Chromium (1.8x) |
| layout.html | layout | 1.44 | 1.87 | **Ambitious** (1.3x) |
| layout.html | paint and raster | 4.75 | 7.40 | **Ambitious** (1.6x) |
| style.html | parse | 0.07 | 0.41 | **Ambitious** (5.9x) |
| style.html | style | 1.10 | 0.62 | Chromium (1.8x) |
| style.html | layout | 1.22 | 1.95 | **Ambitious** (1.6x) |
| style.html | paint and raster | 3.49 | 6.43 | **Ambitious** (1.8x) |
| a11y.html | parse | 0.10 | 0.69 | **Ambitious** (6.9x) |
| a11y.html | style | 0.98 | 1.24 | **Ambitious** (1.3x) |
| a11y.html | layout | 1.64 | 1.94 | **Ambitious** (1.2x) |
| a11y.html | paint and raster | 4.47 | 12.17 | **Ambitious** (2.7x) |
| medium.html | parse | 0.59 | 1.37 | **Ambitious** (2.3x) |
| medium.html | style | 12.01 | 2.16 | Chromium (5.6x) |
| medium.html | layout | 4.25 | 19.52 | **Ambitious** (4.6x) |
| medium.html | paint and raster | 48.05 | 57.37 | **Ambitious** (1.2x) |
| large.html | parse | 6.74 | 8.33 | **Ambitious** (1.2x) |
| large.html | style | 131.76 | 15.91 | Chromium (8.3x) |
| large.html | layout | 33.57 | 208.78 | **Ambitious** (6.2x) |
| large.html | paint and raster | 77.42 | 602.30 | **Ambitious** (7.8x) |

### Whole loads (ms)

*Warm, laid out*: the page fetched, parsed, styled and laid out, in a process already running. *Warm, to PNG*: that, then drawn and encoded as a PNG. *Cold*: a process started, the page drawn into a PNG file, the process ended.

| page | measure | Ambitious | Chromium | faster |
|---|---|---:|---:|---|
| news.html | warm, laid out | 2.2 | 13.6 | **Ambitious** (6.0x) |
| news.html | warm, to PNG | 22.3 | 59.2 | **Ambitious** (2.7x) |
| news.html | cold, to PNG | 26.8 | 183.1 | **Ambitious** (6.8x) |
| layout.html | warm, laid out | 2.5 | 15.7 | **Ambitious** (6.3x) |
| layout.html | warm, to PNG | 23.3 | 73.6 | **Ambitious** (3.2x) |
| layout.html | cold, to PNG | 30.8 | 218.6 | **Ambitious** (7.1x) |
| style.html | warm, laid out | 2.5 | 17.4 | **Ambitious** (6.9x) |
| style.html | warm, to PNG | 20.5 | 73.3 | **Ambitious** (3.6x) |
| style.html | cold, to PNG | 27.2 | 225.9 | **Ambitious** (8.3x) |
| a11y.html | warm, laid out | 2.8 | 29.7 | **Ambitious** (10.4x) |
| a11y.html | warm, to PNG | 24.8 | 106.6 | **Ambitious** (4.3x) |
| a11y.html | cold, to PNG | 42.8 | 258.9 | **Ambitious** (6.0x) |
| medium.html | warm, laid out | 16.9 | 28.2 | **Ambitious** (1.7x) |
| medium.html | warm, to PNG | 380.5 | 460.7 | **Ambitious** (1.2x) |
| medium.html | cold, to PNG | 422.8 | 640.7 | **Ambitious** (1.5x) |
| large.html | warm, laid out | 172.6 | 88.5 | Chromium (2.0x) |
| large.html | warm, to PNG | 696.5 | 5651.9 | **Ambitious** (8.1x) |
| large.html | cold, to PNG | 764.1 | 6345.5 | **Ambitious** (8.3x) |

### Diagnostics (ms)

Ambitious: the snapshot exported, then the reasoner's `advise` (every module's diagnostics, a cocolog process of its own). Chromium: axe-core 4.14.0 injected into the loaded page, then `axe.run()`.

| page | Ambitious | its findings | Chromium + axe-core | its violations (nodes) | faster |
|---|---:|---|---:|---|---|
| news.html | 51.7 | | 106.6 | 4 (9) | **Ambitious** (2.1x) |
| layout.html | 109.4 | | 127.6 | 4 (5) | **Ambitious** (1.2x) |
| style.html | 73.7 | | 114.2 | 3 (12) | **Ambitious** (1.6x) |
| a11y.html | 94.2 | | 154.2 | 6 (8) | **Ambitious** (1.6x) |
| medium.html | 949.6 | | 414.5 | 2 (22) | Chromium (2.3x) |
| large.html | 11501.6 | | 2071.7 | 2 (202) | Chromium (5.6x) |

**Ambitious is faster in 38 of 48 comparisons.**

### JavaScript

Ambitious had no engine at this run. These are the reference engines' times for `tools/bench/js/suite.js`, median ms of 5 runs:

| workload | V8 (node v22.23.4) | V8 --jitless | QuickJS 2021-03-27 |
|---|---:|---:|---:|
| loop | 43.4 | 138.2 | 117.0 |
| fib | 1.6 | 6.4 | 6.4 |
| objects | 43.2 | 67.4 | 124.0 |
| classes | 20.3 | 60.8 | 90.2 |
| closures | 3.2 | 44.4 | 50.9 |
| strings | 92.6 | 87.3 | 328.4 |
| arrays | 10.6 | 14.2 | 24.2 |
| json | 5.3 | 4.8 | 18.4 |

Median ms of 5 runs. Checksums: all engines agree.
