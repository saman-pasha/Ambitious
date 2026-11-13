# Performance

*Phase 11: the engine's stages measured, and the reasoner's cost against them.*

`ambitious bench PAGE [N]` times each stage of loading a page over N runs. It
then times the reasoner, a cocolog process of its own, over the page's
snapshot: `check` loads and validates it; `advise` also proves every module's
diagnostics.

The measurements below were taken on the session's container: four cores of an
Intel Xeon at 2.10 GHz, the units built by cicilang at `-O2` for the generic
x86-64 CPU. `tools/gen-big-page.py 200` makes the large page: 200 sections of
headings, paragraphs, links, lists and tables, under 80 rules.

| stage (ms, mean of 5) | news.html | layout.html | the large page |
|---|---:|---:|---:|
| the page's bytes, nodes, boxes | 466, 50, 17 | 2,257, 101, 32 | 248,919, 16,212, 5,404 |
| fetch the page | 0.03 | 0.03 | 0.46 |
| parse | 0.06 | 0.10 | 6.77 |
| fetch its resources | 0.02 | 0.00 | 0.23 |
| cascade | 0.30 | 0.32 | 4.51 |
| layout | 1.30 | 1.24 | 17.26 |
| accessibility tree | 0.02 | 0.02 | 1.75 |
| paint | 2.44 | 2.56 | 48.15 |
| encode the PNG | 14.45 | 15.43 | 432.69 |
| export the snapshot | 0.88 | 1.21 | 74.52 |
| **the engine, in all** | **19.5** | **20.9** | **586.3** |
| the snapshot | 44 KB, 957 facts | 85 KB, 1,779 facts | 7.0 MB, 147,006 facts |
| **reasoner: check** | **49** | **37** | **635** |
| **reasoner: advise** | **34** | **34** | **1,206** |

`docs/benchmark.md` sets these against Chromium on the same box (PLAN.md §11).
Ambitious is faster at every stage it shares with Chromium, on every page.

## What reasoning costs against plain browsing

For a page of ordinary size, reasoning over it costs one to two times what
loading, laying out and drawing it costs. That is 35 to 50 ms, most of it
cocolog starting. The price is paid only when the reasoner is asked: `load`,
`style` and `render` export nothing and start no reasoner.

At 147,000 facts, the large page's snapshot is read in about 0.6 s, at 4.3 µs
a fact, and every module's diagnostics are proved in about 0.4 s. axe-core
takes 2.1 s over the same page in Chromium.

## What changed, and what it bought

| change | before | after |
|---|---|---|
| units built at `-O2` (env.sh); pixels unchanged, checked by the render gate | 2,512 ms (the large page) | 837 ms |
| the reasoner keeps handle-keyed facts by their integer keys (REPORT, finding 13) | `advise` 77 s; `low_contrast` alone 55 s | `advise` 11 s |
| the snapshot read a line at a time (REPORT, finding 12) | refused past 16 MiB, exit 1 with nothing said | 21 MB read |
| **the benchmark's changes (PLAN.md §11):** | | |
| selectors split once; rules bucketed by their subject's id, class, attribute, type or `:is()` alternatives; an ancestor Bloom filter | cascade 131.8 ms (the large page) | 12.0 ms |
| computed styles shared by elements with the same matched rules under parents of one style | 12.0 ms; 214,774 value records | 4.5 ms; one record per element and 29 per style |
| layout reads a node's values in one pass | layout 38.7 ms | 17.3 ms |
| the snapshot read a term at a time, each fact checked by a clause made for its predicate | 27 µs a fact | 4.3 µs a fact |
| the snapshot carries each element's style and each style's declared values; the rules derive inherited and initial values | 408,681 facts, 21.4 MB | 147,006 facts, 7.0 MB |
| `low_contrast` asks the cheap tests first | its diagnostics 0.45 s | 0.25 s |
| opaque fills written a row at a time | paint 4.0 ms (news.html) | 2.4 ms |
| the collector runs for pages, between their tasks, its roots the script host's values (Phase 12n): a page whose timer makes 100 rounds of 10,000 records of garbage (`tools/gen-garbage-page.py`), its scripts stage (`ambitious bench`, 5 runs) and its peak memory (one `ambitious script`, its maximum resident set) | 417 to 469 ms; 371 MB | 331 to 358 ms, 12 collections of 6 ms each; 38 MB |
| the same, on the benchmark's scripted page (2,000 items, `tools/gen-scripted-page.py`), which never makes the 32 MB a first collection waits for: its scripts stage, its click (`--click '#sale'`), its peak memory | 5.3 to 6.9 ms, 3.9 to 4.5 ms; 15.4 MB | 5.4 to 6.6 ms, 4.2 ms; 15.7 MB |
| connections kept, and a page's parts carried at once (Phase 12q): `ambitious load` of a page of thirty parts over https from a local node server, `tests/net/site/h2/page.html` (`tools/bench/net-load.sh 20`: the median of 20 loads, each a process, the binaries' runs interleaved) | 92 ms: a connection and a TLS handshake a request, one after another (31 a load) | 16 ms over HTTP/2 (one connection, the parts concurrent streams); 25 ms over HTTP/1.1 kept alive (six connections) |
| `src/net/net.c` built without struct copies by assignment, which the optimizer takes minutes and gigabytes over (REPORT, finding 31) | the unit before 12q, one URL of 25 KB copied so: 44 s and 1.2 GB to build | 7 s and 250 MB with that copy a `memcpy`; 12q's unit, twice the size: 10 s and 285 MB |
| the user agent's sheet kept in its tables again (Phase 12u): its shorthands expanded, it had passed the 768 declarations they held, so every cascade parsed it anew (a page's frames run two each) | cascade 0.46 ms (`tests/pages/frames.html`, `ambitious bench`, 20 runs) | 0.29 ms; outputs unchanged (`make test`) |
| a page's frames (Phase 12u, `src/shell/frame.c`): `tests/pages/frames.html`, 13 frames, `ambitious bench` (20 runs), against main's build, whose iframes are empty boxes | the engine in all 16.6 ms | 23.9 ms: the frames' stage 6.8 ms (each frame made, parsed, laid out for its scroll bars and again at rest, and drawn), and 1.6 ms more paint, their drawings composed into the page's: about 0.65 ms a frame |
| a frame whose root (or body) has an opaque background drawn once, not over black and over white: `tests/pages/frames-deep.html`, 15 frames, 12 deep | its frames' stage 39.9 ms | 19.1 ms (the sheet kept too) |
| a box drawn onto black and onto white (a filter's, an SVG effect's, a 3D plane's; Phase 12w) on a white canvas filled at once, what it drew found a row at a time, each row compared whole first; a clip-path's pass over the rows its context changed alone; a 3D plane's drawing kept as its rectangle (`ambitious bench`, the medians of five rounds of ten runs, against main's build) | paint: `tests/pages/filters.html` 69 ms, `clippath.html` 47 ms, `planes3d.html` 254 ms (its planes drawn whole) | 54 ms, 14 ms, 20.5 ms (ordered pixel by pixel); every page's pixels as they were |
| text measured and drawn as Chromium shapes it (Phase 12v): each run of text through GSUB and GPOS (`src/paint/shape.c`), its glyphs kept for paint; `ambitious bench` (10 runs) against the build before 12v, the pages of `docs/benchmark.md` | layout 0.26 ms (news.html), 0.32 (layout.html), 1.69 (medium.html), 14.97 (the large page), 7.68 (scripted.html) | 0.36, 0.64, 5.14, 53.3 and 17.1 ms, paint unchanged; Chromium's layout of the same pages 1.47, 1.61, 18.39, 202.17 and 119.69 ms. Each run shaped once a layout (its pieces' key hashed), a lookup passed over when its digest meets none of the run's glyphs (HarfBuzz's set digests), a font's GDEF classes read once: the large page's layout was 62 ms before them |
| SVG's filter primitives drawn (Phase 12ab): each over an 800 x 600 element, paint (`ambitious bench`, the medians of three rounds of five runs; the element unfiltered 19 ms). A filter converts its images between sRGB and linearRGB, a power a channel, which was most of its cost; now a table of every eight-bit colour at every alpha, made once a process | feGaussianBlur 143 ms; `tests/pages/svg-filter.html`'s paint 34.9 ms | feGaussianBlur 90 ms, feTurbulence (4 octaves) 130, feMorphology 80, feConvolveMatrix (5 x 5) 75, feDisplacementMap over a turbulence 99, feTile 60, feImage 36, feSpecularLighting 109; svg-filter.html 26.4 ms; every page's pixels as they were (186 renders, byte for byte) |
| SVG text shaped (Phase 12ab), and the cascade's two properties more a style (callgrind's instructions, `ambitious render`, against main's build) | layout of `svg-text.html` 21.3M, of `svg-textpath.html` 33.5M; news.html's cascade 4.564M | 23.7M and 34.5M; 4.573M, its layout and paint unchanged |
| the cascade, layout and accessibility over the flat tree (Phase 12z): one walk of the DOM's flat tree every module takes (`src/dom/dom.h`, inline), a sheet's tree and depth ranked as its encapsulation context, the scoping's checks made in a document with shadow roots alone, the reasoner's flat parent the DOM's parent in one without; callgrind's instructions (`ambitious render`) and `ambitious bench` (the medians of five rounds of ten runs, the two binaries in turn, on a shared machine) against main's build | the large page: cascade 55.5M instructions, 6.4 ms; layout 488.9M, 60.3 ms; accessibility tree 10.7M, 1.1 ms; `advise` 1,024 ms. The cascade of news.html 4.51M, layout.html 5.64M, style.html 4.88M | cascade 58.1M (+4.7%: the walk's calls, the featureless host's test in each compound, each declaration's context), 6.6 ms; layout 485.9M, 59.9 ms; accessibility tree 11.2M (+4.9%), 1.2 ms; `advise` 1,069 ms (+4%: the flat tree's parent asked under each inherited value). news.html 4.51M, layout.html 5.63M, style.html 4.85M (within 0.4%); a page of eight components (`tests/pages/shadow-layout.html`): its slots' nodes found in 76K instructions, its cascade 8.1M |
| writing modes (Phase 12aa), on horizontal pages: the box record's new field and the fragment record's two in their padding, a face's vertical metrics kept beside its record, an upright run's glyphs set apart after its shaping, a vertical run measured and packed in loops of its own (`ambitious bench`, the medians of five rounds of ten runs, against the build before 12aa, the pages of `docs/benchmark.md`; callgrind, the large page) | layout 0.54 ms (news.html), 0.80 (layout.html), 6.61 (medium.html), 67.5 (the large page), 21.1 (scripted.html); paint 0.91, 1.38, 24.0, 43.3 and 40.9 ms; the large page's layout 485.7 million instructions | 0.45, 0.78, 6.47, 68.1 and 21.3 ms; paint 0.85, 1.43, 24.6, 43.6 and 41.0 ms: within the runs' noise. 487.8 million instructions (+0.4%; paint +0.3%, the cascade +0.3%), where the same work with the fields appended and the vertical paths in the horizontal ones' loops had cost 502.7 million (+3.5%) |
| the collector inside a running script, at its loops' turns, the host's own call the only C below (`src/js/gc.c`, `js_gc_live`): a script whose loop makes 20 million objects in one task (`ambitious js`, its time and maximum resident set; node 22: 0.11 s and about 50 MB); `tests/js/gcloop.js`, loops of garbage among kept closures, maps, generators, promises and a WeakRef's target | 5.65 s, one collection at its end; 2,792 MB; the page past 4 GB | 4.91 s, 76 collections; 44 MB; the page 1.35 s, 24 collections, 82 MB |
| a rope's piece and a rope written out (`src/js/vm.c`, `js_str_sub`, `js_str_flatten`): `s += c` then `s.slice(-3)`, 80,000 turns (`ambitious js`; node 22: 0.29 s, V8 copying the whole at a byte a char each turn) | 10.3 s: the rope written out a unit at a time each turn | 0.087 s: a short piece read from the rope's leaves, the rope left as it is; a whole one written out in one growth of the units |

## Hot paths, and what would make them faster

- **Encoding the PNG** is now three quarters of the engine's time on every page.
  The painter's own deflate uses fixed codes, one LZ77 candidate per hash and no
  lazy matching, at about 12 ns a byte. It still beats Chromium's full-page
  screenshot (`docs/benchmark.md`). A filter chosen per row and dynamic codes
  would cut both the time and the file.
- **The snapshot** could shrink further: deltas between revisions, from the DOM's
  mutation log (`diff` already compares two snapshots), and a reasoner kept
  running between them.
- **Shaping** is now most of layout's time on a page of text: the large page's
  160,573 code points cost about 1,700 instructions each (callgrind), a third of
  it GPOS's kerning (each glyph's coverage and pair set searched) and almost half
  the passes before the lookups (segments, fallback, normalization, each a copy of
  the run). HarfBuzz's per-lookup caches of coverage and class values, and one pass
  from code points to glyphs, would cut both. Blink keeps a node's shape results
  until its text or style changes; runs kept across layouts would spare a relayout
  (a click's) most of its shaping.

## Robustness

`tests/exec/fuzz_pipeline` pushes 60 deterministic documents of tag soup, broken
CSS and random bytes through the whole pipeline. That is parse, cascade,
layout, accessibility, paint and every module's export, with every module's
invariants checked after each document. The exec gate runs it under valgrind:
no invalid access, no leak, no broken invariant. A larger run of 120 documents
(2.6 MB, 63,000 nodes) ran clean outside the gate.
