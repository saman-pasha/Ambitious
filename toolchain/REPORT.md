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
9. **Without `--target`, cicilang compiles for the host CPU and every feature
   it has** (`module/ccl_llvm.c`: `LLVMGetHostCPUName()`,
   `LLVMGetHostCPUFeatures()`). On an AVX-512 machine, a `uint32_t` turned into
   a `double` becomes `vcvtusi2sd`. Valgrind 3 cannot run it: the CSS test
   stopped with `SIGILL` in `css_value_text`. A binary built this way also may
   not run on a machine without those features.

   Named, the target takes the arch's generic CPU, with no extra features and
   the host's own headers and linker (`ccl_cross_target/2` is false for the
   host's own triple). `toolchain/env.sh` therefore names this machine
   (`AMB_TARGET`, from `uname`: `x86_64-linux-gnu` here). Every build passes
   `AMB_CICILANG_FLAGS`, which is `--no-kb --target=$AMB_TARGET -I include`.
10. **`T x = {0};` fails to lower when `T`'s first member is an array of
    structs.** cicilang emits `sext i32 0 to [6 x %struct.amb_face]`, which
    LLVM refuses (`invalid cast opcode`). A struct that begins with a scalar
    or a vector lowers fine. `src/paint/font.h` therefore puts `amb_fonts`'s
    vectors first.

    Two more things learnt on the way, which are rules rather than bugs:
    - The ownership qualifier `own` is a keyword, so a parameter or variable
      named `own` misparses. The checker then reports `a borrow consumed` or a
      syntax error some lines on.
    - A struct holding ccllib vectors is not autodropped. Its owner drops each
      vector by name under `defer`; a function given a pointer to the struct
      may not consume its fields.
11. **`struct addrinfo` cannot be lowered from `<netdb.h>`**
    (`not lowered yet: struct(addrinfo)`). glibc declares it only under
    `__USE_XOPEN2K`, which cicilang's reading of the system headers never
    sets, even with `_POSIX_C_SOURCE` defined. `sockaddr_in` and the rest of
    the sockets API lower fine. `src/net/net.c` declares the struct (glibc's
    LP64 layout), `getaddrinfo`, and `freeaddrinfo` with an `own` parameter,
    so the checker sees the list consumed. Name lookup and connections then
    work, and are valgrind clean.
12. **`read_file_to_codes/2` refuses a file over 16 MiB**, by failing with no
    error (`lib/files.cicili`: `st_size > 16777216`). A large page's snapshot
    (21 MB, 408,681 facts) made the reasoner exit 1 with nothing said.
    `prolog/amb/reasoner.pl` now reads a snapshot a line at a time through
    `library(stream)`, where `open/3` lives in cocolog. It also says when a
    command fails.
13. **cocolog keys nothing inside a compound** (COMPAT.md: no deep index; "the
    inner key as an argument of its own"). Every snapshot fact is keyed by a
    handle, `n(Doc, Index, Gen)`, so a lookup by handle walked every fact of
    its predicate. Over 100,000 facts that was 0.385 ms against 0.014 ms for an
    integer key, and the reasoning cost grew as the square of the page.

    The reasoner now keeps each fact predicate with handle arguments as
    `'$amb_F'(K1, ..., Kk, Args...)`, each Ki a handle's integer index. F
    itself becomes one clause that works out the keys from the handles it is
    called with. Snapshots, rules, queries, proofs and answers are unchanged.
    On the 21 MB snapshot, `advise` fell from 77 s to 11.7 s, all of it now the
    snapshot's reading. One trap on the way: building that clause with
    `findall/3` copies the head's variables, which unlinks the keys.
14. **A conditional whose arms are a char array and a string literal does not
    lower:** `invalid cast opcode for cast from 'ptr' to '[8 x i8]'` (a
    `ptrtoint` to the array's type). Two string literals of different lengths
    lower fine. The reduction:

    ```c
    int main(int argc, char **argv) {
        (void)argv;
        char buf[8] = "abc";
        const char *p = argc > 1 ? buf : "none";
        return p[0];
    }
    ```

    Ambitious writes `if`/`else` there, or passes the table to a function.
15. **A pointer a function returns into a static table is refused as "plain
    pointer not consumed"** when it is kept in a local, though nothing owns
    it: `error: plain pointer not consumed: 'p' in return(int(0))`.

    ```c
    static const int table[4] = { 1, 2, 3, 4 };
    static const int *row(int i) { return table + i; }
    int main(void) { const int *p = row(2); return *p; }
    ```

    Ambitious returns an index instead, or passes the table as a parameter.
    The same holds of a local pointer to a row of a static table, though no
    function returns it (12k's WebP decoder met it; it indexes the table):

    ```c
    static const unsigned char table[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int main(int argc, char **argv) { (void)argv; const unsigned char *row = table[argc & 1]; return row[0]; }
    ```

    reports `error: plain pointer not consumed: 'row' in return(index(id(row),int(0)))`.
16. **The checker accepts a pointer into a vector's element passed beside the
    vector to a function that grows it**, which then writes through a pointer
    into the freed buffer. A borrow of `data(v)` is refused after a call that
    may grow `v`; an element field's address is not tracked:

    ```c
    #include <vector.pl>
    typedef struct item { int a, b; } item;
    static void grow(vector<item> *v, int *out) {
        for (int i = 0; i < 1000; i++) { item x = { i, i }; push_back(v, x); }
        *out = 7;
    }
    int main(void) {
        vector<item> v;
        item first = { 0, 0 };
        push_back(v, first);
        grow(&v, &v[0].b);
        return v[0].b;
    }
    ```
17. **`-g` crashes the compiler (a segmentation fault) on a static local
    inside a nested block**; the same function without `-g`, and a file without
    such a local with it, compile:

    ```c
    int f(int n) {
        if (n > 0) {
            static int count = 0;
            count += n;
            return count;
        }
        return 0;
    }
    int main(void) { return f(1) - 1; }
    ```
18. **A unit that includes `<openssl/ssl.h>` takes about 64 s to read**, most
    of a build of `src/net/net.c`, against a few seconds for a unit of the same
    size without it. Ambitious keeps OpenSSL to that one unit.
19. **A call to a function with no declaration in scope, inside an operand of
    `?:`, is reported without the callee's name**: `uint32_t e = a ? nowhere(a,
    2) : 0;` gives `error: not lowered yet: type(unknown) (function uses)`, where
    the same call as a plain initializer gives `not lowered yet:
    undeclared(nowhere)`. The work-around is to declare the function first (12g's
    package met it); the message should name the undeclared callee wherever the
    call stands.
20. **A local struct with a `vector` member is not zero-initialised, where a
    `vector` local is**: `vector<int> v; push_back(v, k);` starts empty, but
    with `typedef struct holder { vector<int> items; int n; } holder;` the
    local `holder h; push_back(h.items, k);` reads whatever the stack held: it
    aborts in `push_back`, or appends to an earlier call's elements (12g's Intl
    package met it as every locale taking the first locale's date formats).
    The work-around is `memset(&h, 0, sizeof h)` and dropping the members by
    hand. A struct with an owning member should start zeroed as an owning
    local does, or the checker should refuse its use before it is
    initialised:

    ```c
    #include <stdio.h>
    #include <string.h>
    #include <vector.pl>
    typedef struct holder { vector<int> items; int n; } holder;
    static void scribble(void) {
        volatile unsigned char junk[256];
        for (int i = 0; i < 256; i++) junk[i] = (unsigned char)(0xA5 + i);
        (void)junk[0];
    }
    static int in_struct(int k) {
        holder h;
        push_back(h.items, k);
        int n = (int)len(h.items);
        vector_int_drop(&h.items);
        return n;
    }
    int main(void) { scribble(); printf("%d\n", in_struct(1)); return 0; }
    ```
