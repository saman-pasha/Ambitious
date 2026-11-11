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

| stage (ms, mean) | news.html | layout.html | the large page |
|---|---:|---:|---:|
| the page's bytes, nodes, boxes | 466, 50, 17 | 2,257, 101, 32 | 248,919, 16,212, 5,404 |
| fetch the page | 0.02 | 0.03 | 0.31 |
| parse | 0.05 | 0.08 | 5.49 |
| fetch its resources | 0.01 | 0.00 | 0.15 |
| cascade | 0.40 | 0.71 | 113.52 |
| layout | 1.16 | 1.34 | 33.03 |
| accessibility tree | 0.01 | 0.02 | 3.55 |
| paint | 2.90 | 4.24 | 75.84 |
| encode the PNG | 11.98 | 15.68 | 455.19 |
| export the snapshot | 0.94 | 1.48 | 149.75 |
| **the engine, in all** | **17.5** | **23.6** | **836.8** |
| the snapshot | 77 KB, 1,561 facts | 152 KB, 3,019 facts | 21.4 MB, 408,681 facts |
| **reasoner: check** | **43** | **80** | **10,882** |
| **reasoner: advise** | **64** | **87** | **10,939** |

## What reasoning costs against plain browsing

For a page of ordinary size, reasoning over it costs two to four times what
loading, laying out and drawing it costs: 40 to 90 ms, most of it cocolog
starting and reading the snapshot. That price is paid only when the reasoner is
asked: `load`, `style` and `render` export nothing and start no reasoner.

At 400,000 facts, reading the snapshot costs about 27 µs a fact, and it is the
whole of the reasoner's cost: proving every module's diagnostics takes 0.5 s of
the 11. The fix is to send less, which the next section lists.

## What Phase 11 changed

| change | before | after |
|---|---|---|
| units built at `-O2` (env.sh); pixels unchanged, checked by the render gate | 2,512 ms (the large page) | 837 ms |
| the reasoner keeps handle-keyed facts by their integer keys (REPORT, finding 13) | `advise` 77 s; `low_contrast` alone 55 s | `advise` 11 s; every module's diagnostics 0.5 s |
| the snapshot read a line at a time (REPORT, finding 12) | refused past 16 MiB, exit 1 with nothing said | 21 MB read |
| `check` reads the snapshot once | twice: 21 s | 10.9 s |

## Hot paths, and what would make them faster

- **Encoding the PNG** is the largest of the engine's stages. The painter's own
  deflate is fixed codes, one LZ77 candidate per hash and no lazy matching, at
  about 12 ns a byte. Dynamic codes and a hash chain would cut both the time
  and the file.
- **The cascade** matches every rule against every element, reading each
  selector anew. Bucketing the rules by their rightmost compound's id, class or
  type (as browsers do) skips most of the pairs.
- **The snapshot.** Each element's 29 computed values are a third of the facts.
  The next steps are to export only the values that differ from the parent's or
  the initial ones, and to send deltas between revisions from the DOM's
  mutation log (`diff` already compares two snapshots).

## Robustness

`tests/exec/fuzz_pipeline` pushes 60 deterministic documents of tag soup, broken
CSS and random bytes through the whole pipeline. That is parse, cascade,
layout, accessibility, paint and every module's export, with every module's
invariants checked after each document. The exec gate runs it under valgrind:
no invalid access, no leak, no broken invariant. A larger run of 120 documents
(2.6 MB, 63,000 nodes) ran clean outside the gate.
