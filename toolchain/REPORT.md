# Phase 0 — toolchain baseline and compatibility report

Verified on 2026-11-11 in a fresh Ubuntu 24.04.5 container (x86_64, 4 CPUs,
15 GB). Every result below comes from a command run on that box. Nothing is
taken from a README.

## The baseline

| | commit | version | built by |
|---|---|---|---|
| Cicili | `3ba2ca4` | 3.3.0 | SBCL 2.2.9.debian, from the checkout |
| ZiguratIP | `2a65f81` | 1.0.2 | `make MODE=Release` — 14 libraries, `parsi`, `parsic`, `ziguratip`, the Parsi objects |
| cocolog | `d4b5564` | 3.62.0 | `make`, `make schema`, `make modules` — 17 modules |
| cicilang | `2c72459` | 0.284 | `module/build.sh`, `module/build-llvm.sh` |
| LLVM | — | 18.1.3 | Ubuntu's `llvm-18-dev`, embedded through llvm-c |
| clang | — | 18.1.3 | Ubuntu's, for the neighbours' native builds and the link |

`toolchain/pins` holds the full commits. `sh toolchain/check.sh` holds a box
to them and was GREEN at the end of this run:

```text
ok       cicili     .../cicili at 3ba2ca4
ok       ziguratip  .../ziguratip at 2a65f81
ok       cocolog    .../cocolog at d4b5564
ok       cicilang   .../cicilang at 2c72459
ok       cocolog    cocolog 3.62.0
ok       cocolog    answers 42
ok       cicilang   cicilang 0.284 (cocolog 3.62.0; LLVM 18.1.3)
ok       cicilang   builds and runs a ccllib program (--no-kb)
ok       contracts  the DOM contract validates
GREEN: the toolchain
```

## How it was built

`toolchain/bootstrap.sh` performs the same steps as cocolog's
`install/install-linux.sh`, with one difference: it downloads no Quicklisp.
Cicili's four Lisp systems come from these sources:

| system | from |
|---|---|
| `cl-ppcre` | Debian's `cl-ppcre` package |
| `sha1`, `base64` | cocolog's own shims (`colab/lisp/`) |
| `str` | `toolchain/lisp/str`, a shim of the ten cl-str functions that Cicili 3.3.0 calls |

The `str` shim reproduces cl-str's documented behaviour, including the
details Cicili's output depends on:

- the literal (non-regex) `replace-all` and `replace-first`, and their
  `:regex t` form;
- `split`'s kept trailing empty strings;
- `substring`'s clamping.

The shim's first version had no `:regex` keyword. MVCCS's transpile failed
on `(str:replace-all "[^/]+?/\\.\\./" "" … :regex t)`, so the keyword was
added and the build re-run.

## cicilang's own gates (the C half)

The gates were run with `CICILI` and `COCOLOG` set, over the user's knowledge
base, one after another:

| gate | result | note |
|---|---|---|
| `test/toolchain.sh` | RED | 1 missing (libc++ 18), 2 differing (cocolog 3.62.0 against cicilang's pin of 3.37.0). Neither matters for Ambitious; see the findings. |
| `test/flow.sh` | RED (1 s) | "the check of a predicate in two files was not" written: finding 1 |
| `test/reader.sh` | **GREEN** (3 s) | two fixtures SKIP: `cimath.c` and `numpy_example.c` are not in Cicili's checkout |
| `test/compile.sh` | **GREEN** (32 s) | every C run fixture, and every refusal of the safe part |
| `test/driver.sh` | RED (9 failures) | each failure is C++ (no libc++, finding 3; `-g` of C++ forms) or finding 1, including the `-E` checks, whose text carries finding 1's load errors |
| `test/objects.sh` | **GREEN** (5 s) | |
| `proof/run.sh` | **GREEN** | LLVM IR → native binary → exit 42 |
| `test/cross.sh` | not run | needs qemu-aarch64 and a cross gcc |
| `test/libcxx.sh`, `test/cpp.sh` | not run | C++: libc++ is not installed, and Ambitious does not use cicilang++ (the owner's directive) |
| `test/ccllib.sh` | not run | its three runs need cicilang++ and clang++ over libc++ |

cocolog's own suite (`make test`) was not run: its SKILL.md asks the owner
first. It is the next step of this phase on the owner's word.

## What Ambitious stands on, proven

These probes are not committed; they are listed so a later toolchain can be
re-checked against the same points.

| point | result |
|---|---|
| A ccllib program (`vector<int>`, `push_back`, `:=`, `println`'s holes) | builds and runs |
| A build in memory (`--no-kb`) | 0.57–0.61 s for a small unit with a macro file |
| A file-scope macro call `m(dom);` | is handed `id(dom)`; its C text's items land at file scope |
| `typedef store<tag> name;` inside a macro's C text | makes the type macro's instance; the typedef's objects are autodropped |
| A type macro's function, called by its short name | made at its first call (asked first with its arity, `rec_at2`, then `rec_at`) |
| A pointer from a generated `… tie s` accessor, used after a call that may grow the store | refused: `use of a borrow after a call that may replace its owner: 'p' in borrowed_from('d.rec')` |

## Findings

Each finding is for its repository's owner. Ambitious edits none of the
neighbours.

1. **cocolog 3.62.0 reads `public` as a prefix operator** (as swipl does).
   Two clauses of cicilang 0.284 no longer load: `library/ccl_cpp_class.pl:154`
   and `library/ccl_cpp_layout.pl:25`, each `A \== public, …`
   ("Operand expected, unquoted comma or bar found").
   - Both are C++ access control, so a C build is unaffected.
   - Under `-E` the two load errors reach the output.
   - The flow gate stops on them.
   - The cure is to write `(public)`, as cocolog's SKILL.md §2 says.
2. **cicilang's `test/toolchain.pins` names cocolog 3.37.0 (`74183ae`).**
   Its owner's rule takes cocolog's latest master, so the pin is behind by
   design until the chain runs over 3.62.0. Ambitious pins the cocolog it was
   verified on (3.62.0).
3. **No libc++ on the box.** The C++ gates and the C++ halves of the driver
   gate cannot run. Ambitious is C with ccllib, by the owner's directive, so
   it needs none.
4. **One `--embed` knowledge base spun.** A build over `~/.cicilang/KB` ran at
   100% CPU for ten minutes, holding the store's lock, after a sequence of
   builds whose macro file was edited between them. Every later build over
   that store spun the same way.
   - Removing the store (cicilang's documented repair) cured it.
   - From a fresh store, the same sequences (a macro file changed between
     builds; a build the safe part refuses; a macro that writes a file) did
     not reproduce it.
   - Ambitious builds with `--no-kb`. It is fast enough at 0.6 s a unit,
     hermetic, and it leaves the user's store alone.
5. **Cicili 3.3.0 calls `str:replace-all` with `:regex t`**, a keyword of
   newer cl-str. A box that gets `str` from an old Quicklisp dist fails in
   MVCCS's transpile, the way the shim's first version did.
6. **A macro file found through a relative `-I` directory brings no
   `NAME.pl.h`** (cicilang 0.284).
   - `cicilang -I include -c src/dom/dom.c` loaded `include/amb.pl`, but not
     `include/amb.pl.h` beside it. The generated text's `free` was then
     `undeclared(free)`, and with `<stdlib.h>` included by hand,
     `typedef(amb_span)` was not lowered.
   - With `-I $PWD/include` the header comes with the macro file, and the
     unit builds.
   - Ambitious's Makefile passes the include directory by its absolute path.
7. **The README's `FILE *f = fopen(...); if (f == NULL) return -1;` is
   refused** (`plain pointer not consumed: 'f' in return(...)`). The null test
   refines an owner but not a plain pointer that holds fresh memory.
   `own FILE *f = fopen(...)` passes, with `!f` or `f == NULL`, so Ambitious
   declares its streams as owners.
8. **`continue` inside a `switch` inside a loop, from blocks of different
   depths, stops the check with no line named:** `not lowered yet:
   phase(check) (function file)`. `test/why.sh` shows `ck_merge_frames/3`
   given frame stacks of different lengths at the loop's join. The 15-line
   reduction:

   ```c
   int f(int n) {
       int s = 0;
       while (n > 0) {
           switch (n % 3) {
           case 0:
               if (n > 5) { s++; n--; continue; }
               s += 2; n--;
               continue;
           default:
               n--;
           }
       }
       return s;
   }
   ```

   Ambitious's tree builder writes such a dispatch as an `if`/`else if` chain
   instead.
