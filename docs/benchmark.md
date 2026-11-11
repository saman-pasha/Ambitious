# Benchmark: Ambitious against Chromium

*The performance gate of PLAN.md §11. `make bench` reruns the page rows. `QJS=path/to/qjsrun python3 tools/bench/js/run.py` reruns the JavaScript rows.*

Ambitious and the reference engines run on the same box, on the same pages:
- **The pages.** The four test pages, and `tools/gen-big-page.py` at 20 sections (medium.html) and at 200 (large.html).
- **The reference.** Chromium 1243, Playwright's headless shell, in an 800x600 viewport at scale 1.
- **Chromium's stages** are the durations of its trace events: ParseHTML; then UpdateLayoutTree and ParseAuthorStyleSheet; Layout; and PrePaint, Paint and Layerize, with the raster threads' RasterTask.
- **Ambitious's stages** are `ambitious bench`'s timers.
- **The two engines are built differently.** Chromium uses several threads and processes. Ambitious uses one thread, built at -O2 for the generic x86-64 CPU.

## The latest run: 64 of 64

The pages now include `scripted.html` (`tools/gen-scripted-page.py 2000`): its script builds a list of 2,000 items, and a click at its button puts every item on sale. The page adds rows for its scripts, as a stage, and for a click handled and drawn again.

Measured by `python3 tools/bench/compare.py 5` on Intel(R) Xeon(R) Processor @ 2.10GHz (4 CPUs), Linux-6.18.44-fc-v378-x86_64-with-glibc2.39.

| page | bytes | DOM nodes (Ambitious) | elements (Chromium) |
|---|---:|---:|---:|
| news.html | 466 | 50 | 22 |
| layout.html | 2,257 | 101 | 44 |
| style.html | 1,511 | 60 | 25 |
| a11y.html | 1,666 | 123 | 56 |
| medium.html | 28,451 | 1,632 | 746 |
| large.html | 248,919 | 16,212 | 7,406 |
| scripted.html | 1,734 | 10,029 | 6,011 |

### The stages (ms)

Ambitious's timers against the durations of Chromium's trace events for the same work.

| page | stage | Ambitious | Chromium | faster |
|---|---|---:|---:|---|
| news.html | scripts | 0.00 | 7.51 | **Ambitious** (infx) |
| news.html | parse | 0.07 | 0.40 | **Ambitious** (5.8x) |
| news.html | style | 0.27 | 0.45 | **Ambitious** (1.7x) |
| news.html | layout | 1.20 | 1.47 | **Ambitious** (1.2x) |
| news.html | paint and raster | 1.89 | 3.52 | **Ambitious** (1.9x) |
| layout.html | scripts | 0.00 | 7.10 | **Ambitious** (infx) |
| layout.html | parse | 0.11 | 0.50 | **Ambitious** (4.5x) |
| layout.html | style | 0.32 | 0.47 | **Ambitious** (1.5x) |
| layout.html | layout | 1.28 | 1.61 | **Ambitious** (1.3x) |
| layout.html | paint and raster | 2.46 | 5.78 | **Ambitious** (2.3x) |
| style.html | scripts | 0.00 | 8.44 | **Ambitious** (infx) |
| style.html | parse | 0.07 | 0.40 | **Ambitious** (5.7x) |
| style.html | style | 0.31 | 0.57 | **Ambitious** (1.8x) |
| style.html | layout | 1.21 | 2.00 | **Ambitious** (1.7x) |
| style.html | paint and raster | 2.11 | 5.09 | **Ambitious** (2.4x) |
| a11y.html | scripts | 0.00 | 6.82 | **Ambitious** (infx) |
| a11y.html | parse | 0.11 | 0.62 | **Ambitious** (5.6x) |
| a11y.html | style | 0.33 | 1.20 | **Ambitious** (3.6x) |
| a11y.html | layout | 1.26 | 1.93 | **Ambitious** (1.5x) |
| a11y.html | paint and raster | 2.40 | 10.38 | **Ambitious** (4.3x) |
| medium.html | scripts | 0.00 | 8.54 | **Ambitious** (infx) |
| medium.html | parse | 0.73 | 1.18 | **Ambitious** (1.6x) |
| medium.html | style | 1.41 | 2.10 | **Ambitious** (1.5x) |
| medium.html | layout | 3.07 | 18.39 | **Ambitious** (6.0x) |
| medium.html | paint and raster | 29.83 | 48.26 | **Ambitious** (1.6x) |
| large.html | scripts | 0.03 | 10.73 | **Ambitious** (357.6x) |
| large.html | parse | 5.63 | 8.72 | **Ambitious** (1.5x) |
| large.html | style | 3.65 | 17.16 | **Ambitious** (4.7x) |
| large.html | layout | 15.12 | 202.17 | **Ambitious** (13.4x) |
| large.html | paint and raster | 38.34 | 603.02 | **Ambitious** (15.7x) |
| scripted.html | scripts | 5.08 | 15.94 | **Ambitious** (3.1x) |
| scripted.html | parse | 0.06 | 8.03 | **Ambitious** (133.8x) |
| scripted.html | style | 1.79 | 16.08 | **Ambitious** (9.0x) |
| scripted.html | layout | 9.74 | 119.69 | **Ambitious** (12.3x) |
| scripted.html | paint and raster | 36.73 | 252.94 | **Ambitious** (6.9x) |

### Whole loads (ms)

*Warm, laid out*: the page fetched, parsed, styled and laid out, in a process already running. *Warm, to PNG*: that, then drawn and encoded as a PNG. *Cold*: a process started, the page drawn into a PNG file, the process ended.

| page | measure | Ambitious | Chromium | faster |
|---|---|---:|---:|---|
| news.html | warm, laid out | 1.6 | 17.2 | **Ambitious** (10.8x) |
| news.html | warm, to PNG | 17.7 | 56.7 | **Ambitious** (3.2x) |
| news.html | cold, to PNG | 52.2 | 195.7 | **Ambitious** (3.7x) |
| layout.html | warm, laid out | 1.8 | 14.7 | **Ambitious** (8.4x) |
| layout.html | warm, to PNG | 21.5 | 72.6 | **Ambitious** (3.4x) |
| layout.html | cold, to PNG | 42.4 | 214.7 | **Ambitious** (5.1x) |
| style.html | warm, laid out | 1.6 | 17.7 | **Ambitious** (10.9x) |
| style.html | warm, to PNG | 18.3 | 71.2 | **Ambitious** (3.9x) |
| style.html | cold, to PNG | 39.1 | 231.2 | **Ambitious** (5.9x) |
| a11y.html | warm, laid out | 1.8 | 15.3 | **Ambitious** (8.7x) |
| a11y.html | warm, to PNG | 21.2 | 91.5 | **Ambitious** (4.3x) |
| a11y.html | cold, to PNG | 45.0 | 233.7 | **Ambitious** (5.2x) |
| medium.html | warm, laid out | 5.3 | 21.8 | **Ambitious** (4.1x) |
| medium.html | warm, to PNG | 346.6 | 441.1 | **Ambitious** (1.3x) |
| medium.html | cold, to PNG | 393.3 | 613.5 | **Ambitious** (1.6x) |
| large.html | warm, laid out | 24.9 | 99.2 | **Ambitious** (4.0x) |
| large.html | warm, to PNG | 493.2 | 5055.9 | **Ambitious** (10.3x) |
| large.html | cold, to PNG | 614.0 | 5297.4 | **Ambitious** (8.6x) |
| scripted.html | warm, laid out | 16.7 | 111.0 | **Ambitious** (6.6x) |
| scripted.html | warm, to PNG | 505.1 | 2037.6 | **Ambitious** (4.0x) |
| scripted.html | cold, to PNG | 560.9 | 2198.8 | **Ambitious** (3.9x) |

### Diagnostics (ms)

Ambitious: the snapshot exported, then the reasoner's `advise` (every module's diagnostics, a cocolog process of its own). Chromium: axe-core 4.14.0 injected into the loaded page, then `axe.run()`.

| page | Ambitious | its findings | Chromium + axe-core | its violations (nodes) | faster |
|---|---:|---|---:|---|---|
| news.html | 40.9 | | 104.0 | 4 (9) | **Ambitious** (2.5x) |
| layout.html | 45.8 | | 127.2 | 4 (5) | **Ambitious** (2.8x) |
| style.html | 27.9 | | 129.0 | 3 (12) | **Ambitious** (4.6x) |
| a11y.html | 35.4 | | 141.3 | 6 (8) | **Ambitious** (4.0x) |
| medium.html | 155.2 | | 365.8 | 2 (22) | **Ambitious** (2.4x) |
| large.html | 1034.9 | | 2282.9 | 2 (202) | **Ambitious** (2.2x) |
| scripted.html | 670.7 | | 1606.8 | 2 (4) | **Ambitious** (2.4x) |

### A click, handled and drawn again (ms)

The page loaded and drawn, then a user's click at the element. Ambitious: the click's listeners (`ambitious --click SELECTOR bench`), then the page styled, laid out and its viewport painted again. Chromium: the traced input events' dispatch, the listeners run in it, then the frame it draws: style, layout, paint and raster.

| page | click at | Ambitious | its click, cascade, layout, viewport | Chromium | its dispatch, style, layout, paint and raster | faster |
|---|---|---:|---|---:|---|---|
| scripted.html | `#sale` | 19.4 | 3.7, 2.0, 11.1, 2.7 | 57.6 | 8.6, 4.8, 13.8, 30.5 | **Ambitious** (3.0x) |

**Ambitious is faster in 64 of 64 comparisons.**

### What changed between the runs

PLAN.md §11 has the causes, each measured with callgrind before it was changed. In short:

| row lost at the first run | first run | latest run | what changed |
|---|---:|---:|---|
| style, large.html | 131.8 ms (Chromium 15.9) | 12.7 ms, 4.5 ms in `ambitious bench` | Each selector is split once. Rules are bucketed by their subject's id, class, attribute, type or `:is()` alternatives. An ancestor Bloom filter rejects chains early. Elements share computed styles: the same matched rules under parents of one style give one style, recorded once. Value texts are written once each |
| style, medium.html | 12.0 ms (Chromium 2.2) | under 1 ms | the same; the shared styles halved what was left |
| warm, laid out, large.html | 172.6 ms (Chromium 88.5) | 37.8 ms | the cascade, and layout reading each node's values in one pass |
| diagnostics, large.html | 11.5 s (axe-core 2.1 s) | about 1.1 s | The reasoner reads terms from the stream and checks each with one clause compiled for its predicate. The snapshot carries each element's style, each style's declared values once, and the initial values; the rules derive the rest. 408,681 facts became 147,006 |
| diagnostics, medium.html | 950 ms (axe-core 415 ms) | 171 ms | the same |
| paint and raster, news.html | 4.0 ms (Chromium 3.0) | 2.0 ms | opaque fills are written a row at a time |

The scripted page's rows were first measured with Phase 8b. They lost none of them, but a click came out even: 60.9 ms against Chromium's 60.8. The paint after the click drew the whole page, 44,000 pixels tall, where Chromium rasterizes the tiles on screen. The painter now draws a window of the page (`amb_paint_view`), so the click is drawn again as a screen shows it: 2.7 ms of paint where the whole page takes 37.

Every gate of `make test` stayed green through each change. The pixels of every page are unchanged, and so is every answer the reasoner gives. The only exceptions are the proofs, which now show the derived values.

### JavaScript: 8 of 8 against QuickJS

`tools/bench/js/suite.js` on the four engines, median ms of 9 runs (`QJS=path/to/qjsrun python3 tools/bench/js/run.py 9`). Ambitious is its own engine (`src/js/`, PLAN.md Phase 8), run by `ambitious js`; QuickJS is the engine Phase 8 had planned to embed.

| workload | V8 (node v22.23.4) | V8 --jitless | QuickJS 2021-03-27 | Ambitious | against QuickJS |
|---|---:|---:|---:|---:|---|
| loop | 42.3 | 144.2 | 119.1 | 56.4 | **Ambitious** (2.1x) |
| fib | 1.8 | 6.6 | 6.5 | 4.4 | **Ambitious** (1.5x) |
| objects | 41.6 | 71.2 | 128.9 | 53.2 | **Ambitious** (2.4x) |
| classes | 18.5 | 61.5 | 94.5 | 60.7 | **Ambitious** (1.6x) |
| closures | 3.1 | 44.2 | 52.3 | 40.9 | **Ambitious** (1.3x) |
| strings | 89.2 | 87.5 | 330.6 | 206.2 | **Ambitious** (1.6x) |
| arrays | 10.5 | 14.3 | 24.0 | 19.4 | **Ambitious** (1.2x) |
| json | 5.0 | 4.9 | 18.7 | 9.9 | **Ambitious** (1.9x) |

Checksums: all engines agree. Ambitious is also faster than V8's own interpreter (`--jitless`) on loop, fib, objects, classes and closures, and slower on strings, arrays and json, whose time is in the builtins. V8's JIT is faster than every interpreter on every row; those rows are reported, not targeted (PLAN.md §11).

The engine's first correct run lost fib (22.5 ms), closures (146), arrays (41.8) and loop (275). Each loss was profiled with callgrind and changed the interpreter:

| lost | measured cause | the engine now |
|---|---|---|
| fib, closures | about 480 instructions a call; 9 to 14 of every instruction's dispatch, the exception check after each, and a comparison's jump decoded from operands | A comparison and its jump are one instruction, one per relation, and a local compared with an integer is one more. Arithmetic with an integer or a second local is one instruction. A plain call pushes no `this` (CALL0), and the closure's frame is built in place, by a flag set when the closure was made. `return c ? a : b` returns from each branch. Simple instructions skip the exception check. The switch covers every byte, so its jump has no range check |
| loop | the remainder through `fmod` and `trunc`, library calls on the generic CPU; the loop's own jump | integers' remainder by integer division, the sign the dividend's; loops test at their foot, one jump an iteration |
| arrays | a callback from a native took about 330 instructions to enter; `a.push`'s cache never filled for an array; each array growth pushed its slots one at a time | a closure called from a native gets its frame made in `js_call`, read through borrows; an array's named properties are cached like any object's; slots are allocated in one resize |

`tests/js/` holds the engine's own gate: scripts whose every line is checked against V8's output, the slow paths of each fast instruction among them.

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
