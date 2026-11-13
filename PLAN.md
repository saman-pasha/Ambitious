# Ambitious — Implementation Plan

This is the working plan for building the browser described in
[`reasoning_native_browser_plan.md`](reasoning_native_browser_plan.md) (the
*architecture*). The architecture says what the browser is. This plan says
how it is built: on which facilities of the toolchain, in what order, how
each phase is proven, and which of the architecture's open questions
(its §18) each decision answers.

The plan is a living document. A phase that lands updates its row in §9.
A decision that changes is edited where it stands, and the reason is noted.

---

## 1. What the plan stands on

### 1.1 The toolchain, as it is

| Part | Role in Ambitious | What it provides that the plan depends on |
|---|---|---|
| **cicilang** (0.301) | Compiler for every browser module | Safe Modern C (C17/C23) lowered to LLVM IR; the ownership check (`own`, `move`, borrows, `x tie y`); `defer`; `:=`; macros written in cocolog (`#include "m.pl"`, `#cocolog … #end`); **type macros** (`vector<int>`), **C text with holes** (`ccl_c_items/3`), functions made at their first call (`fn_N/3`), calls found by the first argument's type, **autodrop** |
| **ccllib** | The standard library of that C | `vector`, `array`, `deque`, `list`, `forward_list`, `stack`, `queue`, `priority_queue`, `set`/`multiset`, `map`/`multimap`, the four `unordered_*`. Each is a cocolog macro file. |
| **cocolog** (3.62.3) | Reasoning, contracts, generation | A Prolog in the SWI-Prolog 9.0.4 dialect, with one namespace. It has an MVCC store embedded over ZiguratIP (`--embed DIR`), `library(process)`, `library(html)`/`xml`/`json`, and the flow libraries for charting and debugging Prolog. |
| **ZiguratIP** (1.0.2) | Durable store | MVCC storage engine. It is reached only as cocolog's store, as cicilang reaches it. |
| **Cicili** (3.3.0) | Builds the toolchain | Transpiles cocolog, ZiguratIP's MVCCS and cicilang's native modules. No browser module is written in it. |
| **LLVM** (18) | Back end | Embedded in cicilang through llvm-c. |

**The owner's directive for this repository:** browser modules are written
in cicilang's C with ccllib. `cicilang++` and libc++ are not used.

### 1.2 Constraints that shape every design below

1. **A ccllib element holds no pointer** (CCLLIB.md's open decision 2).
   Therefore every browser structure is an *index arena of plain records*,
   and all text lives in byte pools addressed by `(offset, length)`. This is
   the architecture's §2.5 (stable identity) arrived at from the other side.
2. **Instance types do not nest, and a plain struct's instance-typed member
   is not dropped.** Therefore each module's state is one *store*. Its type
   is made by Ambitious's own type macro, generated from the module's
   contract: one struct of own buffers with one drop. Autodrop then applies
   to it like to any ccllib object.
3. **cocolog has one namespace, and every predicate of an included macro
   file is a macro.** Therefore all Prolog that Ambitious defines is
   prefixed: `amb_` for the reasoner and tools, `ambm_` inside the
   generator. The included macro file holds only its entry points, and the
   generator is loaded by a directive, which makes none of its predicates a
   macro. No generated C name equals a macro's name.
4. **The safe part refuses any pointer without an ownership path.**
   Therefore there are no back pointers and no stored borrows. A borrow of a
   store's buffer ends at the next call that may replace the buffer (the
   check finds iterator invalidation at compile time). Hence handles
   everywhere, and borrows that live for a statement or two.
5. **A C text is not preprocessed.** `NULL`, `stdout`, `EOF` and `errno` are
   no names inside generated text, so generated code writes `0` and calls
   runtime helpers instead.
6. **ccllib has no `string` yet.** Ambitious keeps its text in the stores'
   byte pools and its spans, and moves to ccllib's `string` when it lands.

---

## 2. Decisions: the architecture's open questions answered

| # | Question (§18) | Decision |
|---|---|---|
| 1 | Syntax and expansion model of module contracts | A contract is a cocolog file `contracts/<module>.pl` of ground `amb_*` facts (§4). A C unit uses it through the macro file `include/amb.pl`: `amb_interface(dom);` at file scope expands to the module's types and prototypes; `amb_implementation(dom);` (only in the module's own unit) adds the exporters, the invariant checks and the operation table. `store<dom_tag>` is a type macro, so a store is an instance with a drop. |
| 2 | What Cocolog generates directly, what goes through an intermediate format | **At C read time, by cicilang macros:** enums and their name tables, records, handles, the store type and its drop, accessors that check handles, fact exporters, operation prototypes, invariant checks and the request dispatcher. **By the cocolog tool `tools/amb`:** the reasoning schema (`build/schema/<m>.pl`), the contract's Markdown reference and the contract-test expectations. The tool's outputs are deterministic text, and golden copies live in `tests/golden/`. |
| 3 | How versions and generated artifacts are tied to contracts | Each contract has a *fingerprint*: a hash of its canonical text. It is compiled into C (`<m>_contract_hash`), written into the schema (`amb_contract_hash(M, H)`) and stamped on every snapshot. The reasoner refuses a snapshot whose fingerprint differs from the schema it loaded. |
| 4 | Full snapshots, incremental updates, or both | Both. Full snapshots come first (Phase 3), as one fact per line at `(doc, revision)`. Incremental deltas (`+Fact` / `-Fact` between two revisions, from the mutation event log) come in Phase 11, for when snapshot cost becomes measurable. |
| 5 | Identity of revisions and snapshots | A document id (u32) and a revision (u64) that every successful mutating operation increments. A node handle is `{doc, index, generation}`, written in Prolog as `n(Doc, Index, Gen)`. A snapshot is `(Module, Doc, Revision, Fingerprint)`. |
| 6 | How reasoning-requested operations are authorized | The reasoner only *proposes*: it emits `amb_request(Doc, Rev, Op)`. The browser parses it with its own small term reader. An op is accepted only if (a) the contract marks it `may_request`, (b) the revision is the document's current one, (c) every handle is live with a matching generation, and (d) the generated precondition check passes. It then runs through the same C function that every caller uses, and is logged as an event. Anything else is refused with a reason. Cocolog never touches browser memory: the boundary is a process boundary. |
| 7 | Which parts of ZiguratIP are durable truth | **Durable:** history, bookmarks, sessions and tabs, permissions, preferences. **Rebuildable:** search indexes, semantic summaries, resource and cache metadata. **Never stored:** handles, pointers, script heaps. A profile is a cocolog `--embed` store directory. Private browsing uses the memory store, so nothing is written. |
| 8 | Minimum platform and standards target | Linux x86-64 (Ubuntu 24.04) first, headless, rendering to PNG. HTML: the WHATWG tokenizer and the tree-construction modes listed in Phase 4. CSS: a CSS 2.1 core with Selectors 3's simple selectors and combinators. A window, GPU work and other platforms come later. |

Two decisions the architecture implies but does not ask:

- **The reasoning boundary is a process boundary.** The engine binary and
  the cocolog reasoner exchange text: snapshots out, requests back. This
  enforces §2.6 ("Cocolog should not mutate internal browser memory
  directly") and §14 (capabilities) by construction, and it keeps the hot
  paths free of the reasoner.
- **Page content is data, never code.** Every string from a page reaches
  Prolog as a quoted, escaped atom. The reasoner loads a snapshot with
  `read_term`, never `consult`, and accepts only the fact predicates its
  schema declares. A directive, an undeclared predicate or a malformed line
  rejects the whole snapshot.

---

## 3. Architecture as built

```text
                 contracts/dom.pl  (the one source of truth)
                  │                                  │
   cicilang reads src/dom/dom.c                 tools/amb (cocolog)
   #include <amb.pl>                                 │
   amb_implementation(dom);                          ▼
     │ generator (prolog/amb/gen_c.pl)         build/schema/dom.pl
     ▼                                         fact signatures, fingerprint,
   C: enums+names · records · handles ·        rules' interface, docs
      store<dom_tag> + drop · checked accessors ·    │
      fact exporter · invariant checks ·             │
      op prototypes + request dispatcher             │
     │                                               │
     ▼   + hand-written operations                   ▼
   ambitious (engine binary) ── snapshot ──▶ prolog/amb/reason.pl
     ▲    facts @ (doc, rev, fingerprint)     + rules/dom.pl … (derived facts,
     │                                          diagnostics, explanations)
     └──── amb_request(Doc, Rev, Op) ◀─────────┘
           validated (capability · revision · generation · precondition)
           → executed → new revision → event

   durable knowledge: reasoner over a cocolog --embed store (ZiguratIP MVCCS)
```

The three roles stay as the architecture names them:

- **Execution:** cicilang C, the hot paths.
- **Contracts and generation:** cocolog macros, at read time.
- **Reasoning:** cocolog, over snapshots of actual state.

---

## 4. The module contract

The full specification is `docs/contract.md`, written in Phase 1. In short,
a contract is a set of ground facts in one file, every one carrying its
module's name:

| Section (architecture §3) | Facts |
|---|---|
| module | `amb_module(M, Version, Summary)` |
| `TYPES` | `amb_enum(M, Name, [Value…])`, `amb_record(M, Name, [field(F, Type)…])`, `amb_handle(M, Name, Record)` |
| `OWNERSHIP` | `amb_store(M, Store, [table(Record), pool(Name), atoms(Name)…])`, `amb_owns(M, Owner, Owned, How)`, `amb_ties(M, Ref, Store)` |
| `OPERATIONS` | `amb_operation(M, Op, [param(P, Type)…], Result, [pre(Cond)…], [effect(E)…], [may_request])` |
| `INVARIANTS` | `amb_invariant(M, Name, Text, Check)` |
| `EVENTS` | `amb_event(M, Name, [field(F, Type)…])` |
| `REASONING` | `amb_fact(M, Pred/Arity, Table, [ArgSource…], Where)`, `amb_rule_file(M, File)`, `amb_query(M, Name, Goal)` |
| `GENERATION` | `amb_generate(M, [Output…])` |
| `VALIDATION` | `amb_test(M, Name, Kind)` |

The validator (`prolog/amb/contract.pl`) checks:

- every reference resolves;
- names are unique and prefixed;
- ownership has no cycle;
- each fact's arguments map to declared fields;
- each operation's types exist;
- each invariant and precondition names only what is declared.

Its refusals cite the contract file and the fact.

---

## 5. Runtime model

- **Stores.** One per module instance, for example one DOM store per
  document. A store owns all of its records. Records link to each other
  only by handle (`parent`, `first_child`, `next_sibling`, …), never by
  pointer.
- **Handles.** `{doc, index, generation}`. Freeing a record bumps its
  generation, so a stale handle is detected on every access: an accessor
  answers "not live" instead of reading the reused slot.
- **Borrows.** An accessor that returns a pointer is `tie`d to the store.
  cicilang refuses such a borrow if it is used after a call that may replace
  the store's buffers.
- **Revisions and events.** Each successful mutating operation increments
  the document's revision and appends an event record (op, handles, old and
  new values). A failed operation changes nothing and returns a status code.
- **Snapshots.** `amb_snapshot(M, Doc, Rev, Fingerprint).` followed by one
  fact per line.
- **Requests.** Each is a line `amb_request(Doc, Rev, Op).`, accepted or
  refused as §2 decides. The answer names the outcome: `ok(NewRev)`,
  `refused(stale_revision)`, `refused(stale_handle)`,
  `refused(not_requestable)`, or `refused(precondition(P))`.
- **Diagnostics and explanations.** The reasoner answers
  `amb_diag(Kind, Subject, Text)`, and `amb_why(Goal, Proof)` gives the
  proof tree of a derived fact, built from the facts it rests on. This is
  "explain why a node is inside a subtree" (§6.2) and "why a declaration
  won" (§8).

---

## 6. Repository layout

```text
PLAN.md                         this plan
reasoning_native_browser_plan.md   the architecture
Makefile                        build, test, gates (sources toolchain/env.sh)
toolchain/                      Phase 0: pins, bootstrap.sh, env.sh, check.sh, REPORT.md,
                                lisp/str (the shim that lets Cicili build from distro packages)
contracts/                      module contracts: dom.pl, html.pl, css.pl, layout.pl, net.pl, …
prolog/amb/                     the cocolog side, found through COCOLOG_LIBRARY
  contract.pl                   reading and validating contracts
  gen_c.pl                      the C texts the macros emit
  gen_schema.pl                 the reasoning schema and the contract's reference
  reason.pl                     the reasoner: snapshots, queries, explanations, requests
  rules/                        derived rules, one file per module
include/amb.pl, amb.pl.h        the macro file C units include, and the C it needs
src/                            cicilang C
  rt/                           runtime: text output, term writing and reading, process spawn
  dom/ html/ css/ layout/ paint/ net/ a11y/ shell/
tools/amb                       the contract tool: validate, schema, doc, fingerprint
tests/                          contract/ gen/ golden/ exec/ safe/ reason/ integ/ html/ css/ render/
docs/                           contract.md, and one note per module
```

---

## 7. Phases

The order is the architecture's §15. Each phase names its deliverable and
the gate that proves it. A phase is done when its gate is green in
`make test`, not before.

### Phase 0: Freeze and verify the toolchain

- `toolchain/pins`: exact commits and versions of cicili, ZiguratIP,
  cocolog and cicilang, plus LLVM, clang, SBCL and the OS.
- `toolchain/bootstrap.sh`: builds the toolchain from scratch, from
  distribution packages, with no Quicklisp download: Debian's `cl-ppcre`,
  cocolog's `sha1`/`base64` shims, and `toolchain/lisp/str`.
- `toolchain/check.sh`: each tool's version against the pins; cocolog
  answers; cicilang builds and runs a C program and a ccllib program.
- `toolchain/REPORT.md`: the compatibility report, with the gates of
  cicilang and cocolog that were run here and their results.

**Gate:** `check.sh` is green, and the report names every gate run and every
gate skipped.

### Phase 1: The module contract

- `docs/contract.md`: syntax and semantics.
- `prolog/amb/contract.pl`: the validator.
- `contracts/dom.pl`: the first contract.
- `tests/contract/`: one bad contract per rule, each with its expected
  refusal.

**Gate:** the validator accepts every contract in `contracts/` and refuses
each bad contract with its expected message.

### Phase 2: Cocolog macro integration

- `include/amb.pl`: `amb_interface/1`, `amb_implementation/1`, and the
  type macro `store<…>` with `fn_store/3`.
- `prolog/amb/gen_c.pl` and `gen_schema.pl`.
- `tools/amb`.
- Consistency is enforced three ways. A generated prototype makes the
  compiler check each hand-written operation's signature. The generated
  operation table makes the link fail on a missing operation. The
  fingerprint ties the C side to the schema.

**Gate:** a sample module (`tests/gen/`) compiles with cicilang over a sample
contract, and its generated accessors, exporter and dispatcher run. The
generated C text and the schema match `tests/golden/`.

### Phase 3: DOM proof of concept (the first milestone)

- `src/dom/`: Document, Element, Text, Comment and Attribute; create,
  append, insert-before, remove, destroy, set and remove attribute, set
  text; traversal; serialization; events; revisions.
- `prolog/amb/reason.pl` and `rules/dom.pl`: ancestry, accessible names,
  missing required attributes, the consequences of a removal, fact diffs
  between revisions, and `amb_why/2`.
- A full loop in the `ambitious` binary: mutate → snapshot → reason →
  request → validate → apply → new revision.

**Gate:** one test per acceptance criterion of the architecture's §6.3.

| §6.3 criterion | Test |
|---|---|
| 1. contract parsed and validated | `tests/contract`: dom accepted |
| 2. macros generate the interface and schema | `tests/gen` and goldens |
| 3. Cicilang compiles and runs the DOM | `tests/exec/dom_*` |
| 4. ownership and lifetime errors rejected | `tests/safe/*.c`, each refused by cicilang with its expected error |
| 5. mutations preserve invariants | randomized mutation runs, invariants checked after every op |
| 6. snapshot exported | `tests/reason`: the snapshot is valid against the schema |
| 7. queries answer the actual state | queries compared with the C side's own traversal |
| 8. reasoning requests a controlled op | `tests/integ`: advise, apply, re-snapshot |
| 9. stale identities and old snapshots | requests at an old revision and on freed handles, refused |

### Phase 4: HTML parsing

- `src/html/`: the WHATWG tokenizer (data, tag, attribute, comment, DOCTYPE
  states; RCDATA, RAWTEXT and script data; character references) and tree
  construction:
  - insertion modes initial, before html, before head, in head, after head,
    in body, text, in table, in row/cell, after body and after after body;
  - implied elements and implied end tags;
  - foster parenting;
  - the adoption agency algorithm;
  - the list of active formatting elements;
  - fragment parsing;
  - serialization.
- Parse errors become facts with their positions. The parser speaks to the
  DOM through a tree-sink interface, so its output contract is independent
  of DOM storage (architecture §7). cocolog's `library(html)` is evaluated
  and kept as the reference *serializer* in tests. It is not a parser of
  real-world HTML.

**Gate:** the fixture suite, plus a recorded pass rate on html5lib's
tree-construction tests where they can be fetched.

### Phase 5: CSS

- `src/css/`:
  - a tokenizer (CSS Syntax 3) and a parser for rules, declarations and
    `@media` (basic);
  - values: lengths in px, em and %, and colours;
  - selectors (type, universal, class, id, attribute, descendant, child,
    compound) with specificity;
  - the cascade (origin, importance, specificity, order), inheritance, and
    computed style for a core property set, including the `style`
    attribute.
- Explanations: `css_matched/3`, `css_won/3`, and `css_lost/4` with its
  reason. Dependencies from a computed value to its rule and stylesheet.

**Gate:** computed styles of the fixture pages, and an explanation for each
win and loss.

*As built:* the facts are `css_matched/2`, each element's computed style
(`css_style_of/2`), each style's declared values (`css_style_declared/4`,
`css_style_won/3`, `css_style_rgba/3`) and the initial values
(`css_initial/2`), beside the sheets, rules and declarations. The rules derive
`css_computed/4`, `css_won/3` and `css_rgba/3` for every element and property
(§11 says why). `css_explain/3`
gives the winning declaration as written, its selector and its origin, or the
parent a value was inherited from, or the initial value and why. `css_lost/4`
names the step a loser lost at: origin or importance, specificity (as
`spec(A, B, C)`) or order. Beyond the plan, the module has Selectors 4's
functional pseudo-classes, media queries, the HTML presentational hints, and
two diagnostics: WCAG contrast and unused rules. What it simplifies is listed
at the top of `src/css/css.c`; chiefly, border-style is folded into the border
shorthands.

### Phase 6: Layout and rendering

- `src/layout/`: a box tree from the DOM and computed style (display block,
  inline and none), the box model, block layout, and inline layout with
  line breaking.
- `src/paint/`: a paint list (rectangles, borders, glyph runs), a software
  rasterizer and a PNG writer. Fonts come through a native interface: a
  bitmap font first, a platform font library later.
- Facts: `layout_box/6` and `layout_line/…`, and an explanation for each
  width ("why is this box 300px wide").

**Gate:** fixture pages render to PNG, matching golden checksums, and their
layout facts answer queries.

*As built:* the facts are `layout_box/7`, `layout_parent/2`, the margin,
border and padding facts, `layout_width/4` (how a width was found, and from
what), `layout_line/6`, `layout_text/8` and `layout_frag/8`. The rules are:
- `layout_explain_width/2`, which says why a box is as wide as it is (a
  declared width goes to `css_explain/3`, across modules);
- `layout_at/3`, for hit-testing;
- `layout_overflows/3` and `layout_text_overflow/2`, which are also
  diagnostics.

Fonts are vector outlines rather than a bitmap font: six DejaVu faces (sans,
serif, mono; regular and bold, with a synthetic oblique), so every computed
size draws crisply. Not done: floats, positioning, flex and grid as such,
vertical-align, min/max-height, rowspan, bidirectional text. Each is listed at
the top of `src/layout/layout.c`.

### Phase 7: Networking and navigation

- `src/net/`:
  - URL parsing and resolution (a WHATWG subset);
  - `file:`, `data:` and `http:` (HTTP/1.1 over sockets: content-length,
    chunked, redirects with a bound);
  - resource loading for stylesheets and images;
  - origins, with same-origin decisions made in C.
- Facts: resources, dependencies, redirects and origins.

**Gate:** a multi-resource page served by a local server loads, and its
dependency graph is queryable.

*As built:* `ambitious load URL` lists what a page loaded. Every page command
takes a URL or a path, and the page's snapshot gains the loader's section:
- `net_resource/7`, `net_final_url/2`, `net_origin/2`, `net_asked_by/3`,
  `net_hop/3` and `net_image_size/3`;
- the rules `net_depends/2`, `net_cross_origin/2` (opaque origins are neither
  same nor cross), `net_failed/2` and `net_redirects/2`;
- diagnostics for failures, cross-origin resources and redirects.

Not done: https (no TLS), cookies, caching, `@import`, `srcset`, iframes, and
decoding GIF or JPEG pixels (their sizes are read).

### Phase 8: JavaScript integration

- A JavaScript engine behind the `script` module's boundary.
- Host objects and bindings in cicilang C: document, element, text,
  events, timers.
- Event-loop integration. Engine internals stay behind the boundary, and
  only selected metadata reaches the reasoner.

**Gate:** a basic interactive page runs, with DOM interoperability tests,
and the JavaScript rows of the benchmark (§11) are met.

*Revised by the benchmark (§11).* The design had been QuickJS as a native
library. The benchmark put QuickJS and V8's interpreter (V8 `--jitless`)
within 1–2x of each other, with V8's JIT ahead of both. Embedding QuickJS
would therefore buy an interpreter-class engine, at the price of a
50,000-line engine outside the checker. Its host callbacks would also reach
the page through a `void *`, which the checker refuses. This phase builds the
same class of engine in cicilang C with ccllib, designed for speed from the
start (`src/js/`):
- **Values** are NaN-boxed 64-bit words. A double is itself. Undefined, null,
  booleans, and an object's or a string's heap index live in the NaN space.
  A value holds no pointer, so to the checker it is an integer.
- **Compilation.** Source becomes tokens, then a compact AST (an index
  arena). Scopes are resolved, so the compiler knows which variables a closure
  captures. Then stack bytecode is emitted, with a cache slot for each
  property access and global name.
- **Objects** have shapes: a shape is a node of a transition tree (its
  parent, the key it adds, that key's slot). An object is its shape and its
  slots, so `o.x` at a cached site is one comparison and one load. Arrays are
  dense vectors of values.
- **Strings** are UTF-16 in pools. Names are interned atoms. `+` makes a rope,
  flattened when it is read.
- **Calls** are frames on one value stack. Only captured variables live in
  heap cells.
- **Memory** is the page's arena: freed with the page, under a cap. A script
  past the cap ends with a RangeError. A collector is the first follow-on,
  for pages that run long (Phase 12g's, which runs between a page's tasks).
- **Determinism.** Time is virtual: timers fire in order of their virtual due
  times, and `Date.now()` and `performance.now()` read the virtual clock.
  Every script, handler and timer runs under a step budget.
- **The module boundary.** A `script` module with a contract covers scripts
  (source, origin, outcome), console messages, errors, event listeners and
  timers. Its facts are `js_script/4`, `js_console/3`, `js_error/3`,
  `js_listener/3` and `js_timer/4`. No JavaScript object ever reaches a
  snapshot.
- **The DOM's bindings.** A wrapper keeps a node's handle (document, index,
  generation) in its slots, never a pointer. Every DOM call from script is
  one of the DOM module's operations, so the contract's preconditions,
  invariants and mutation log hold for script as they do for the reasoner. A
  stale handle throws. `querySelector` runs the CSS module's compiled
  matcher.
- **The event loop.** Classic scripts run in document order after the parse
  (no `document.write`, no parser blocking). Then come DOMContentLoaded and
  load, then timers in virtual time. `ambitious click PAGE SELECTOR`
  dispatches a click.
- **Reasoning.** The DOM's mutation log says what script changed. The script
  facts say what ran, what failed and what listens. The new rules find a click
  listener on an element that takes no focus (accessibility), uncaught errors,
  and timers that never settle.
- **Gate.** A counter button and a list built by script, both run and
  re-snapshotted. Interoperability tests for `getElementById`,
  `querySelector`, `textContent`, `setAttribute`, `appendChild`, `classList`
  and events. On the benchmark: `tools/bench/js/suite.js` faster than QuickJS
  on each workload, and the scripted pages faster than Chromium.

*As built, 8a: the engine* (`src/js/`: `vm.c`, `compile.c`, `builtins.c`;
`ambitious js FILE`):
- **The language.** ES2020's statements and expressions: classes with fields,
  accessors, static members and `super`; destructuring, spread and rest;
  default parameters; template literals, tagged ones included; optional
  chaining and nullish coalescing; labels; for-in and for-of. Strictness comes
  from a directive prologue and is inherited by nested functions; class code
  is strict. Not read, each a SyntaxError that names it: generators, async
  functions, modules, `with`, BigInt, direct eval. A regular expression
  literal makes a RegExp whose methods throw.
- **The interpreter**, shaped by the benchmark (§11, `docs/benchmark.md`):
  - a comparison and its jump are one instruction, one opcode per relation;
  - a local's own instructions compare it with an integer and jump, add,
    subtract, multiply or take the remainder with an integer or a second
    local, or return it;
  - a plain call pushes no `this` (CALL0), and a closure's frame is built in
    place, by a flag set when the closure was made;
  - simple instructions go straight to the next one, and only an instruction
    that can throw passes the exception check;
  - the switch covers every byte, so its jump has no range check;
  - loops test at their foot;
  - inline caches cover arrays' named properties.
- **The gate.** `make test-js` runs `tests/js/*.js`, checking each line
  against V8's output (`node NAME.js > NAME.expect`), the slow paths of each
  fast instruction among them. The suite: faster than QuickJS on 8 of 8
  workloads (§11).

*As built, 8b: the page's scripts* (`contracts/script.pl`, `src/script/`):
- **The module is `script`**, and its facts are `script_*`. A contract's facts
  begin with its module's name, and `js_` is the engine's C namespace.
  - What ran: `script_run/4` (the element, the source, how it ended) and
    `script_url/2`.
  - What it wrote and threw: `script_console/3` and `script_error/3`, the
    latter with where the error was thrown (a script, a listener, a timer).
  - What listens and what is set: `script_listener/3`,
    `script_window_listener/2`, `script_capture/2`, `script_timer/4` and
    `script_timer_of/2`.

  The rules derive `script_failed/2`, `script_uncaught/3`,
  `script_click_unfocusable/2`, `script_unsettled/2` and `script_listens/3`.
  `advise` reports four things: a click listener on an element a keyboard cannot
  reach, an error nothing caught, a script that did not run to its end, and a
  timer that keeps the page from coming to rest.
- **The host.** The engine reaches the page through `vm->host`, a struct of
  typed pointers to the page's stores. The safe part stores a borrow only
  where it can follow it. So a command fills the host where it declares the
  stores (`SCRIPT_HOST`), never in a function that is given them.
- **The bindings.**
  - A node's wrapper is a host object holding the node's index and generation,
    one wrapper a node. A destroyed node's wrapper is stale, and using it
    throws.
  - The tree: `parentNode` to `childNodes`; `appendChild`, `insertBefore`,
    `removeChild`, `replaceChild`; `remove`, `before`, `after`, `append`,
    `prepend`, `replaceWith`; `contains`, `cloneNode`.
  - Attributes: get, set, remove, has and toggle, and the reflected ones (`id`,
    `className`, `href` ...).
  - Content: `textContent` and `innerHTML`. `innerHTML` is parsed by the HTML
    module as a fragment and copied in through DOM operations. `outerHTML`
    serializes.
  - `classList`; `style`, the style attribute's declarations; `dataset`.
  - Queries: `querySelector(All)`, `matches` and `closest` run the CSS module's
    own matcher. Also `getElementById`, `getElementsByTagName` and
    `getElementsByClassName`, and `create{Element,TextNode,Comment}`.
  - Events: `addEventListener` with capture and once, `removeEventListener`,
    `dispatchEvent`, `click()`, `Event`, `CustomEvent`, `preventDefault`,
    `stopPropagation`. An `on*` attribute or property is a handler.
  - `window`: the timers, `requestAnimationFrame`, `location` and
    `navigator`.

  What differs from a browser:
  - node lists are arrays, read once, not live;
  - a checkbox's checked state is its attribute, so the cascade's `:checked`
    and the reasoner see a click's change (a field's typed value is kept apart
    from its attribute, as in a browser);
  - `getComputedStyle` reads the inline style, and `getBoundingClientRect` is
    empty, because the scripts run before the cascade and layout;
  - there are no Promises and no `fetch`, and an inserted script does not run.
- **The event loop** is as designed: the scripts in document order, then
  `DOMContentLoaded` and `load`, then the timers in virtual time up to a
  horizon of 10 s.
  - A click runs the listeners on its path, capturing then bubbling.
  - A checkbox's or radio's state changes before the click, and its `input`
    and `change` follow. A label's control is clicked after the label.
  - `--click SELECTOR` adds clicks after the scripts to any page command.
    `ambitious script PAGE` and `ambitious click PAGE SELECTOR` print what the
    scripts did.
- **At rest.** Before the snapshot, each timer's state is recorded, and the
  nodes no tree holds are destroyed.
- **The gate**, `tests/integ/script.sh`:
  - the two DOM interop pages' consoles must be Chromium's own, 41 and 20
    lines with their uncaught errors (`tests/pages/*.chromium`, written by
    `tools/bench/console.js`);
  - the counter, clicked twice and queried again;
  - the list a script builds and a timer trims;
  - the diagnostics.

  The benchmark's scripted page is in §11.

### Phase 9: Accessibility and developer tools

- `src/a11y/`: an accessibility tree with roles and names (from content,
  `alt`, `aria-label`, `aria-labelledby`, `<label for>`).
- Rules for missing names, broken references, inconsistent roles and
  heading order.
- `ambitious inspect`: DOM, style, layout and resources, plus `--query`
  and `--why`.

**Gate:** diagnostics on fixture pages, each naming its nodes and its rule.

*As built:* the facts are `a11y_node/5`, `a11y_parent/2`,
`a11y_explicit_role/1`, `a11y_heading/2`, `a11y_focusable/1`, `a11y_ref/4`
and `a11y_bad_role/2`. The rules add `a11y_missing_name/2`,
`a11y_broken_ref/3`, `a11y_heading_jump/3`, `a11y_nested_interactive/2`,
`a11y_duplicate_id/3`, `a11y_explain_name/2` and `a11y_outline/2`. Proofs
now name their variables A, B, ... and show a bodiless clause as `[rule]`,
so a proof prints the same each run. Not done: aria-describedby
descriptions, states and properties (checked, expanded), live regions, and
accname's recursion through embedded controls.

### Phase 10: ZiguratIP persistence

- A profile is a cocolog `--embed` store holding history, bookmarks,
  sessions, permissions and preferences, with retention and deletion
  rules.
- Private browsing uses the memory store.
- The profile store is isolated from page snapshots.

**Gate:** knowledge written by one process is read by the next; a private
session writes nothing; deletion removes what it names.

*As built:* the profile is a keeper process, as the reasoner is, over its own
store. The page snapshots stay in the reasoner's memory store, so the two never
meet; the test shows the reasoner answering `no` about a profile fact. The
engine asks the keeper only two things, a site's denied kinds and a visit to
record. The commands are `history`, `bookmarks`, `bookmark`, `forget` (`URL`,
`--site`, `--since`), `permit SITE KIND allow|deny`, `retain DAYS` and
`profile GOAL`. AMB_NOW fixes the clock for tests. Not done: sessions restored
into tabs (there are no tabs), and preferences that change rendering.

### Phase 11: Compatibility, security, performance

- More standards and interoperability tests.
- A security audit of the boundaries: the snapshot quoting, the request
  validation, capabilities.
- Profiles of the hot paths, and incremental snapshots.
- A measure of reasoning overhead against plain browsing.

**Gate:** measured numbers and documented limitations.

*As built:*
- **Numbers.** `docs/performance.md` has every stage, measured by `ambitious
  bench` on three pages. For a page of ordinary size the reasoner costs two to
  four times what loading and drawing it costs, about 40 to 90 ms. At 400,000
  facts, reading the snapshot is nearly all of it.
- **Audit.** `docs/security.md` covers the five boundaries (snapshots,
  requests, network, profile, memory and processes), each with its test, and
  the four gaps the audit closed.
- **Limitations.** `docs/limitations.md`, module by module.
- **Not done.** Incremental snapshots (deltas from the mutation log), rule
  bucketing in the cascade, and a faster deflate. Each is named in
  `docs/performance.md` with what it would save.

### Phase 12: The limitations closed

`docs/limitations.md` lists, module by module, what Phase 11 left undone. This
phase does it. Every package keeps `make test` GREEN and adds the tests that
prove its work. When the benchmark's pages change, §11's rule holds: a lost
row changes the implementation. Each package owns its files, so packages can
be built side by side and merged without touching each other's.

- **12a Network** (`src/net/`, `contracts/net.pl`, `tests/net/`):
  - `https:` through the system's OpenSSL: TLS 1.2 and 1.3, SNI, the
    certificate chain and host name verified, a CONNECT proxy from
    `https_proxy`. TLS is the one part a library gives: a TLS of the
    browser's own would be its least trustworthy code. Decoders stay in the
    checked language;
  - cookies (RFC 6265bis: Domain, Path, Secure, HttpOnly, SameSite, expiry),
    kept in the profile's directory when there is one;
  - an HTTP cache (Cache-Control, Expires, ETag and Last-Modified
    revalidation);
  - gzip and deflate content codings, over the painter's own inflate;
  - IPv6 literals; IDNA's punycode; `srcset`'s candidate chosen.
  - Facts for cookies and the cache's answers.

  Then (12m): the URL standard's basic URL parser whole (`src/net/url.c`:
  its state machine, percent-encode sets, IPv4 and IPv6 hosts, the
  serializer, the URL API's getters and setters, origins), IDNA as UTS #46
  has it (the mapping table, NFC, the validity, joiner and bidi rules),
  application/x-www-form-urlencoded, and `<picture>`'s sources
  (`net_picture_source/2`). Gates: WPT's urltestdata (904/904), toascii
  (87/87), IdnaTestV2 (2675/2675) and setters (278/278); a picture page's
  images against Chromium's (22/22).

  Then (12q): HTTP/2 over TLS, offered by ALPN beside http/1.1 as Chromium
  offers it (`src/net/h2.c`: RFC 9113's frames, flow control both ways, PING,
  RST_STREAM, GOAWAY and the streams it left unprocessed asked again, padding,
  Chromium's SETTINGS, window and priority tree; HPACK whole, `src/net/hpack.c`,
  encoding by Chromium's policy); a page's parts the concurrent streams of one
  connection an origin; HTTP/1.1 connections kept alive and reused, six to an
  origin at most, a request on a reused one the server closed asked once again
  on a new one; br (the painter's Brotli decoder, fuzzed against libbrotli and
  unchanged) and zstd (`src/net/zstd.c`, RFC 8878) codings, asked for as Chromium
  asks; each request's fields Chromium's, in its order and case. Gates: RFC
  7541's examples and Chromium's own header blocks (`tests/exec/net_hpack`),
  zstd against libzstd's frames and 11,700 broken ones (`tests/exec/net_zstd`),
  frames and conversations broken at random (`tests/exec/net_h2`), and a page of
  thirty parts over one connection, its header blocks Chromium's field by
  field, with GOAWAY, resets, a server that refuses h2, redirects across the
  protocols, a proxy's tunnel, the cache and cookies, coded and broken bodies
  and HTTP/1.1 kept alive (`tests/integ/net-h2.sh`). The page loads in 16 ms
  over HTTP/2 and 25 ms over HTTP/1.1 kept alive, from 92 ms
  (`docs/performance.md`). HTTP/3 waits for a TLS library with QUIC
  (`docs/limitations.md`).
- **12b Encodings** (`src/html/encoding.c`, generated tables): the WHATWG
  Encoding standard's decoders. That is UTF-8, UTF-16LE and BE, the
  single-byte encodings, and Shift_JIS, EUC-JP, ISO-2022-JP, GBK and
  gb18030, Big5 and EUC-KR. The encoding is sniffed from the BOM, the HTTP
  charset, then a meta prescan, else windows-1252. A document is transcoded
  to UTF-8 before the tokenizer, and its encoding is a fact. Then:
  - a meta past the first 1024 bytes counts while the document is in its
    head section, as Blink's look for one reads on (the tree builder's
    restart in its encoding, 13.2.3.4);
  - an encoding nothing declares is guessed from the bytes as Chromium's
    detector guesses one: Japanese, Korean and Chinese by their commonest
    characters, sixteen single-byte encodings by the likelihood, in bits, of
    their letters in the languages each is written in, a misread word
    counting against one, else windows-1252.

  Gate: Chromium's encoding and text for 45 pages
  (`tests/integ/encoding.sh`).
- **12c The tree builder** (`src/html/tree.c`, `contracts/dom.pl`):
  - foreign content: SVG and MathML elements in their namespaces, with the
    adjusted names and the integration points;
  - template contents, a fragment of the template's own;
  - the 2025 select rules.

  Gate: html5lib's foreign, template and select cases pass.
- **12d Images** (`src/paint/image.c`): JPEG (baseline and progressive, the
  chroma subsamplings, restart intervals) and GIF (LZW, transparency,
  interlace, the first frame) decoded and drawn. Gate: fixtures checked
  against an independent decoder. Then (12k): every GIF frame composed as
  Chromium composes it (disposal, transparency, delays, loop counts), and
  WebP (`src/paint/webp.c`: lossless, lossy with fancy upsampling, alpha,
  the extended format, animation), an animation drawn at the frame it
  shows once the page is at rest; held to Chromium's frames (WebCodecs'
  ImageDecoder) and Pillow's pixels, and fuzzed.
- **12e Accessibility** (`src/a11y/`, `contracts/a11y.pl`):
  - descriptions (aria-describedby, title);
  - states and properties (checked, selected, expanded, pressed, disabled,
    required, invalid, current, values of ranges, ...);
  - live regions;
  - accname's recursion through embedded controls;
  - visibility from the cascade, `::before` and `::after` content (and its
    alternative text) in names, HTML's constraint validation
    (`a11y_validity/2`; a pattern read by the script engine's regular
    expressions), aria-owns reordering the tree, inherited presentation:
    `tests/pages/a11y-more.html`'s tree is Chromium's.

  With their facts and the rules that find faults in them.
- **12f Rendering** (`src/css/`, `src/layout/`, `src/paint/paint.c`):
  - CSS:
    - the properties behind floats, positioning, flex and grid, plus
      vertical-align, overflow, z-index, min/max sizes, opacity, visibility,
      transforms, border styles and colours a side, backgrounds and shadows;
    - `calc()` and custom properties, and rem against the root's font size;
    - `@supports`, `@layer`, `@import` and `@font-face`;
    - the pseudo-classes of state (from the event model), `::before` and
      `::after`, `:has()`;
    - `color-scheme`, a page's `<meta name=color-scheme>`, the system
      colours and `light-dark()` in the scheme each element is used in, the
      user agent's links, marks and form controls in them, and the canvas the
      root's scheme gives (Chromium's values for each scheme).
    - `@keyframes` and the animations' and transitions' properties, each
      animation evaluated at the page's rest time (Web Animations' phases and
      iterations, CSS Easing's functions, a keyframe's own easing, implicit
      keyframes, values interpolated by their kinds), its value over every
      declaration but the important ones and explained as the animation's;
      held to Chromium's values and drawing with each animation at 10 s
      (`tests/integ/cssref.sh`, `tests/integ/paintref.sh`).
    - `@property`: a registered custom property's initial value, its
      inheritance or not, a value its syntax refuses taken as unset, its value
      computed by its syntax (lengths px, colours, angles, numbers, lists), and
      registered ones interpolated in animations (unregistered ones discrete).
    - `@scope`: rules matched below their nearest scoping root (its roots'
      selectors, or a style element's parent) and above its limits, `:scope`
      and `&` as Chromium ranks them, and scope proximity in the cascade
      between specificity and order, which the reasons name.
    - the `-webkit-` names Chromium keeps as aliases of standard properties,
      and `-webkit-text-fill-color`, an inherited colour whose currentcolor
      each element takes from its own `color` (the word kept through
      inheritance, CSS Color 4), with which glyphs and decorations are filled.
  - Layout: floats; positioning (relative, absolute, fixed, sticky); flex;
    grid; vertical-align; min/max-height and percentage heights; rowspan;
    writing modes and bidirectional text; hyphenation; kerning. Then (12j):
    grid's named lines and areas (`grid-template-areas`), column and dense
    auto-flow, and Grid 1's track sizing (11.4-11.8) with spanning items;
    flex's first and last baseline alignment and a flex container's
    baseline; sticky boxes placed as at rest by Chromium's rules; an inline
    element's fragments as a containing block; UAX #9 bidi by characters
    (embeddings, overrides, isolates, brackets, `dir=auto`, `bdi`); each
    held to Chromium's boxes (`tests/integ/boxes.sh`).
  - Paint: border styles, background images, box and text shadows, opacity,
    transforms, clipping; outer and inset shadows, elliptical and
    percentage radii, `background-clip` and `background-origin` (`text`: a
    block's background drawn through the coverage of its text's glyphs); `filter`
    and `backdrop-filter` (Blink's colour matrices and transfer functions in
    sRGB, blur, drop-shadow, a backdrop's blur mirrored at its edges); 3D
    transforms (4x4 matrices, a parent's perspective, each plane drawn
    through its homography, backface-visibility as Chromium tests it, the
    planes of a preserve-3d space drawn far to near) and the individual
    transforms; a zoomed page drawn at the device's px from a copy of its
    box tree with every length scaled, its text set at the larger size; dashed and
    dotted borders laid round rounded corners as Blink lays them (its dash
    ratios, its path's start, Skia's arc lengths), and the corners where
    unlike sides meet split along their diagonals (Blink's miters), each
    held to Chromium's screenshot of a fixture page region by region
    (`tests/integ/paintref.sh`); the fonts a page asks for (TrueType parsed at
    load); the built-in faces Chromium draws with on Linux (subsets of
    Liberation Serif, Sans and Mono for serif, sans-serif and their
    fontconfig aliases, of DejaVu for monospace and by name), each code point
    a face lacks drawn by the face Chromium falls back to, with Chromium's
    line metrics for fallback glyphs, its leading and its monospace default
    size (13px). Then (12l), text:
    - Liberation's italic faces and DejaVu Sans Mono's obliques, and
      Chromium's slant where a family has none.
    - Built-in faces that carry Latin, Greek, Cyrillic and the symbol
      blocks, 1,419 to 2,444 code points each, packed in arrays of short
      literals (toolchain finding 28).
    - What they lack is drawn from the machine's fonts in fontconfig's order,
      as Chromium falls back: Inter, WenQuanYi, Loma, FreeSans and FreeSerif,
      Unifont, then Noto Color Emoji for emoji, which is measured but not
      drawn.
    - CFF and CFF2 outlines, for system fonts and web fonts, read as
      Chromium's FreeType and font sanitizer read them.
    - WOFF2 web fonts, through a Brotli decoder of their own
      (`src/paint/brotli.c`).
    - Each held to Chromium's boxes and drawing on its own fixture pages
      (italic, coverage, symbols, scripts, emoji, cff, woff2).
- **12g JavaScript** (`src/js/`):
  - Promises and the microtask queue; async functions and `await`;
    generators;
  - Symbol and the iteration protocol;
  - a regular-expression engine, and the String methods over it;
  - Proxy and Reflect; typed arrays; weak collections; BigInt; `Intl`
    (Locale, NumberFormat, DateTimeFormat, PluralRules, ListFormat,
    RelativeTimeFormat, DisplayNames, Segmenter, Collator; the locale-
    sensitive methods of Number, BigInt, String, Date and Array) over CLDR's
    data for 29 languages, each test's output node's line for line;
  - modules; `with`; direct `eval`; and in pages, `<script type=module>`
    and import maps (each module run as HTML orders it, fetched as Chromium
    would: CORS, the strict MIME check, its console lines), held to
    Chromium's consoles (`tests/integ/script-modules.sh`);
  - a garbage collector, run at task boundaries, where no native holds an
    object unrooted: for `ambitious js`, and for pages, the script host's
    values among its roots (each unit's, `src/script/script.h`), every
    scripted page's console and facts held unchanged with a collection at
    every boundary, and memcheck clean so (`tests/integ/script-gc.sh`,
    `tests/exec/script_gc`). Then inside a running script too, at its loops'
    turns (every jump back), when the host's own call into the engine is the
    only C code below (`vm->depth` at the host's `gc_live_depth`), the running
    microtask and the WeakRefs' kept targets (AddToKeptObjects, let go at the
    checkpoint's end) among the roots: a loop of 20 million objects peaks at
    44 MB, not 2.8 GB (`tests/js/gcloop.js`; the stress gate collects every
    256 allocations at the loops' turns as well).

  Then (12p): `Function.prototype.toString` gives a function's source text,
  the exact slice ECMA-262 2019 has (functions, arrows, methods, getters and
  setters, classes, async functions and generators, `Function`'s and `eval`'s
  code; a native's `function name() { [native code] }`, a bound function's
  `function () { [native code] }`); a TypeError for calling, constructing,
  iterating, destructuring or reading through what is no function or object
  names the expression as V8's call printer writes it (each error site's span
  kept with the code, the span parsed again when it throws: member chains,
  calls, computed keys, `this`, optional chains, `super`), else the value in
  V8's words; and an array's or an arguments object's elements keep
  attributes of their own (`defineProperty`, `freeze`, `seal`,
  `preventExtensions`, accessors, a non-configurable element stopping
  `length`, a read-only `length` stopping appends), each built-in that writes
  elements taking the standard's steps, which throw where V8 throws. Gates:
  `tests/js/tostring.js`, `errors.js` and `array-attrs.js`, node's output line
  for line.
- **12h Scripts** (`src/script/`):
  - live node lists;
  - `getComputedStyle` and `getBoundingClientRect` over a style and layout
    run on demand;
  - `fetch` and XMLHttpRequest through the loader; inserted scripts;
    `document.write`;
  - keyboard, focus and pointer events; forms' submission;
  - `MutationObserver` from the DOM's mutation log;
  - custom elements and shadow DOM;
  - the window's history, storage, screen and `matchMedia`.

  Done as planned (src/script/view.c, fetch.c, input.c, observe.c,
  custom.c, window.c): each fixture's console held to Chromium's line for
  line; a user's actions (`--click`, `--dblclick`, `--focus`, `--type`,
  `--key`) applied at load; rules over forms sent insecurely, observers
  never disconnected, custom-element names never defined and credentials
  written to storage; storage kept in the profile, its values never in
  the facts.
- **12i Reasoning and the profile**:
  - a reasoner process kept for a session, which takes snapshot deltas from
    the mutation log;
  - the reasoner sandboxed: resource limits, no network, a syscall filter;
  - tabs, and sessions restored into them;
  - preferences that change rendering: base font size, colour scheme,
    reduced motion, zoom.
- **12o SVG** (`src/paint/svg.c`, `src/paint/svg.h`; SVG's properties in
  `contracts/css.pl`, the box's `svg` scene in `contracts/layout.pl`):
  - the cascade computes SVG's properties (fill and stroke, their opacities,
    the stroke's width, caps, joins, miter limit, dashes and offset, the fill
    and clip rules, stop-color and -opacity, clip-path, text-anchor), reads
    presentation attributes as author rules of specificity 0 (a bare number a
    length's px), and the outermost svg's width and height as its box's;
  - layout sizes an svg as Chromium does (its natural size and its viewBox's
    ratio; inline, a block or positioned) and makes its subtree a scene in
    user space: every shape and path command (arcs too), transforms (the
    attribute's, a declared CSS one's), viewBoxes and preserveAspectRatio,
    nested svgs, use and symbol (a use's tree inheriting from it), gradients
    and clips resolved (objectBoundingBox through the element's box), text
    and tspan placed (x, y, dx, dy lists, text-anchor by chunks);
  - paint draws a scene as Skia does: curves flattened at the device's px,
    strokes made outlines (joins with their tangents' miter limit, caps, a
    hairline's alpha), dashes cut where SkDashPath cuts them over
    SkContourMeasure's chords, coverage over sixteen rows a pixel under either
    fill rule, groups with an opacity or a clip drawn in layers (a clip-path's
    mask times its bounding box's, as Blink's two clips multiply), the svg's
    content at whole CSS px;
  - SVG documents as images (img, CSS backgrounds, picture's sources): read
    by a small XML parser into a document of their own, cascaded with their
    own style elements, drawn at their natural size scaled into the box (a
    viewBox alone fitted into it), their natural size the loader's fact.

  Gate: `tests/pages/svg.html`, `svg-paint.html`, `svg-text.html`,
  `svg-image.html` and, zoomed 1.5, `svg-zoom.html`, each held to Chromium's
  screenshot region by region (`tests/integ/paintref.sh`: 37, 40, 9, 11 and
  9 regions agree); `tests/exec/svg_render` (path data, coverage, natural
  sizes, scenes, hostile documents; valgrind clean).
- **12r Quirks mode** (`src/html/tree.c`, `contracts/dom.pl`; the quirks in
  `src/css/css.c` and `src/layout/layout.c`, a broken image's in
  `src/paint/paint.c`; `document.compatMode` in `src/script/input.c`, the
  body's client size in `src/script/script.c`):
  - the document's mode decided by the tree builder as HTML 13.2.6.4.1 has it
    (the public and system identifier lists; limited-quirks for XHTML 1.0's
    transitional and frameset DOCTYPEs and HTML 4.01's with a system
    identifier), kept on the document node (`dom_set_mode`), a fact
    (`dom_mode/2`, `dom_document_mode/2`), a fragment parsed in its document's
    mode, and `document.compatMode`;
  - the Quirks Mode Standard's quirks as Blink applies them, each measured in
    Chromium first: hashless colours and unitless lengths for the properties
    that take them (never through `border`, `font`, `background`, `inset` or a
    function); class and id selectors without ASCII case (the rules' keys and
    `getElementsByClassName` too); `:hover` and `:active` alone holding of
    links alone; Chromium's quirks sheet
    (a table resets the text's properties and takes the body's colour, a text
    field and a textarea are sized by their border box, a form's bottom margin,
    a floated image's 3px); the line height quirk, in limited-quirks mode too (a
    block's strut on a line with its own text or on a list item's lines, an
    inline box's on a line with its text or an edge's border or padding, a
    forced break's on a line of nothing else); Blink's quirky margins (the user
    agent's of a p, a heading, a list, a blockquote, a pre) left out at the
    start and the end of the body and of a table cell; the html element and the
    body filling the viewport, their margins as they collapse taken off, and a
    percentage height taken through the blocks of an auto height up to them; a
    cell's height its border box's; a nowrap cell of a length's width at least
    that wide; no wrap around an img in an auto-width cell's min-content; an
    empty table's height none; a broken image of a declared size a box of it
    (one dimension the other's too), its alt text laid out and drawn in it as
    Blink's fallback has it; the body's `clientWidth` and `clientHeight` the
    viewport's, the root element's its own (CSSOM View's quirk);
  - in every mode, as the quirks' pages need them: a cell's `nowrap` and an
    image's `align` left or right (a float) read as presentational hints.

  Gate: Chromium's mode for 107 document starts (`tests/exec/quirks_mode`,
  `tools/bench/doctypes.js`; the four whose system identifier is empty kept as
  the standard has them); `tests/pages/quirks-*.html`, quirks-mode pages and one
  limited-quirks page, held to Chromium's boxes (337 of 337), regions (35 of 35)
  and computed values (156 of 156, `:hover` measured with the mouse over the
  page: `tests/pages/NAME.hover`); the mode's facts (`tests/integ/quirks.sh`);
  every standards-mode page unmoved.
- **12s Container queries and counter styles** (`src/css/css.c`,
  `src/css/css.h`, `contracts/css.pl`, `prolog/amb/rules/css.pl`; the
  containers' layout, their restyle and the list markers in
  `src/layout/layout.c`), each measured in Chromium first:
  - `container-type` (normal, size, inline-size; scroll-state read and kept,
    never answered), `container-name` and the `container` shorthand, computed
    as Chromium serializes them. A size or inline-size container has that
    containment (its content counts for nothing in its inline size; a size
    container's auto height is as if it held nothing), makes a formatting
    context of its own, and its counters and quotes are its subtree's (style
    containment);
  - `@container [name] <condition>`: width, height, inline-size, block-size,
    aspect-ratio and orientation, in plain (`min-`, `max-`), boolean and range
    syntax (either way round, a value between two); `and`, `or`, `not`,
    parentheses, a list of queries; `@container` nested, inside `@media` and
    around it; `style()` over custom properties. A query asks the nearest
    ancestor of its name whose type has the axes its features need (an element
    itself, for its pseudo-elements); none, or one not sized, is unknown, which
    is false. The matches whose condition fails are kept, unmet
    (`css_container_unmet/2`, `css_container_query/3`, `css_rule_container/2`;
    an unused rule is not one of them, a `container_unmet` diagnostic names
    them);
  - the units cqw, cqh, cqi, cqb, cqmin, cqmax, against the nearest container
    of each axis, the small viewport's without one;
  - `@counter-style` and its descriptors: system (cyclic, numeric, alphabetic,
    symbolic, additive, fixed and its first value, extends), symbols,
    additive-symbols, negative, prefix, suffix, range, pad, fallback (a cycle
    of them ends at decimal), speak-as read and ignored; Chromium's limits
    kept (120 symbols a representation, a pad of 120 at most, its integers'
    range). Every style Chromium predefines (62 names: its UA sheet's and the
    algorithms it writes in C++, the CJK longhands, Hebrew, Armenian, Ethiopic
    numeric), for `list-style-type` (a name or a string) and `counter()` and
    `counters()`.
    A marker of text is the style's representation with its prefix and suffix,
    outside ending at the content box as Chromium places it, its right-to-left
    letters in order; disc, circle, square, the disclosure triangles and the
    styles extending them are shapes, as before.

  The cycle between style and layout is broken as Chromium breaks it, by
  interleaving: a size query needs the container's size, which needs the
  styles. The cascade cannot know a size, so it answers what it can (style
  queries; size queries unknown, which is false; container units the small
  viewport's), keeps every @container rule's match with its answer, and keeps
  its working state for the store (`css_cq`: each node's matches and records,
  the style table and its sharing). Layout, at each container whose content
  box it has sized (its width; a size container's height too, definite or as
  if empty) and before it lays out the children, calls
  `css_container_restyle`: the subtree's matched lists are rebuilt from the
  kept matches, their conditions asked again, the styles made through the
  same shared table (a style that reads container units keyed by their bases
  too), and layout's tables take the changed nodes' and pseudo-elements'
  styles and its generated content again, from the counters it entered with.
  No subtree is laid out twice for it, and a page without `@container` or
  container units does nothing more. Rejected: laying the page out, then
  cascading each container's subtree again and laying out again until nothing
  changes. That lays each container's subtree out twice or more, so a page of
  containers costs twice as much, and a cycle needs a cap.

  Cost (callgrind's instructions, `ambitious bench`, before and after):
  without containers, the cascade from -1.6% (a11y) to +1.8% (medium,
  scripted), mostly the two new properties every style holds (155 values a
  style, from 153), and layout from -0.5% (news) to +1.4% (scripted). On a
  page of 200 cards, each a container whose rules lay it out, size its title
  and generate a badge (`cards-cq`), the restyles are 3.3M instructions, 8.6%
  of its layout (16.5K a container: matched lists and conditions 1.2M, styles
  made 0.7M, generated content walked again 1.1M), and its cascade 14% more
  than the same page's with no container and its narrow rules written plainly
  (`cards-plain`: 12.3M against 10.8M); its cascade and layout together
  (50.8M) are less than the plain page's (55.2M), the containment sparing the
  grid its items' contents. Wall time, ms, the cascade and layout, before
  and after (the medians of seven rounds of ten runs, the two binaries in
  turn, on a shared machine): news 0.36 and 0.28, then 0.37 and 0.27;
  layout.html 0.51 and 0.39, then 0.50 and 0.40; style 0.42 and 0.19, then
  0.43 and 0.19; a11y 0.55 and 0.31, then 0.57 and 0.28; medium 1.69 and
  1.70, then 1.65 and 1.64; large 4.86 and 16.52, then 5.18 and 16.54;
  scripted 2.20 and 6.96, then 2.01 and 6.81; cards-cq 0.82 and 2.86 (its
  @container rules skipped), then 1.04 and 2.94; cards-plain 0.86 and 2.96,
  then 0.86 and 3.01.

  Gates: `tests/pages/container.html` (boxes, 49 of 49), `container-paint.html`
  (regions, 37 of 37), `container-values.html` (computed values, 125 of 125),
  `counter-styles.html` (boxes, 126 of 126), `counter-markers.html` (regions,
  48 of 48), `counter-values.html` (computed values, 27 of 27);
  `tests/exec/counter_styles` (the 62 styles from 0 to 123456789 and below 0,
  and the test's own 33 rules, invalid ones among them, with names in other
  cases and unknown ones, as `counter()` writes them and as markers: 4,716
  lines, Chromium's line for line), `tests/exec/container_queries` (the
  restyle case by case; valgrind clean); layout.html's markers as Chromium
  draws them (8.8% of their region's pixels differed; none do), and the
  ordered lists' of quirks-margins.html and a11y-states.html (66 and 60
  pixels of their regions differed; 4 and 0 do).
- **12t SVG, the rest of it** (`src/paint/svg.c`, `src/paint/svg.h`; a glyph's
  outline in `src/paint/font.c`; SVG's new properties in `contracts/css.pl` and
  `src/css/css.c`; an SVG image element's picture fetched by `src/net/net.c` and drawn
  through the loader paint passes, `amb_svg_draw_with`), each measured in Chromium
  first, Blink's and Skia's sources deciding where the specs leave room:
  - patterns as a fill, stroke or text's paint (patternUnits, patternContentUnits,
    viewBox and preserveAspectRatio, patternTransform, href inheritance of attributes and
    content): each tile drawn as Skia rasterizes a picture shader's, at its matrix's
    scale (reckoned in floats) rounded up to whole px, then sampled bilinearly, repeated;
  - markers (marker-start, -mid, -end and the marker shorthand; markerUnits, refX and
    refY, orient's auto, auto-start-reverse and angles, viewBox, markerWidth and
    markerHeight, overflow) at the places Blink's SVGMarkerDataBuilder finds, a closed
    subpath's ends bisecting its closing segment and its first, the viewport's clip not
    anti-aliased; paint-order between fill, stroke and markers;
  - masks (maskUnits, maskContentUnits, mask-type, CSS's `mask: url()`): the content's
    Rec. 709 luminance (or alpha) in eight bits, its region a clip Skia rounds to whole px;
  - image elements: raster pictures (from the mipmap level Chromium's decode cache picks,
    sampled bilinearly) and SVG ones (laid out in the viewport, a document without a
    viewBox stretched there from its natural size), preserveAspectRatio, CSS's width and
    height, data: URLs;
  - filters (filterUnits, primitiveUnits, regions and subregions rounded out, in, in2 and
    result, SourceGraphic and SourceAlpha, color-interpolation-filters): feGaussianBlur as
    Skia's three box passes (SVG's windows, summed whole and divided once), feOffset,
    feFlood, feComposite (arithmetic too), feMerge, feColorMatrix, feBlend (every mode),
    feDropShadow and feComponentTransfer, each result kept in eight bits and converted
    between linearRGB and sRGB as Skia's colour filters convert it; CSS's filter
    functions on an SVG element made their shorthand primitives; a text's and an
    image's bounding boxes for objectBoundingBox units;
  - vector-effect: non-scaling-stroke; the geometry properties from CSS (r, cx, cy, rx,
    ry, x, y, width, height, `d: path()`); switch's conditions; a text's stroke, its
    rotate lists, textLength (spacing and spacingAndGlyphs), textPath (href,
    startOffset, its anchor, glyphs off its ends dropped), dominant-baseline, and runs
    turned, skewed or stretched drawn as their outlines; strokes no wider than a device
    px drawn as Skia's anti-aliased hairlines.

  Gate: `tests/pages/svg-pattern.html`, `svg-marker.html`, `svg-mask.html`,
  `svg-filter.html`, `svg-embed.html`, `svg-textpath.html`, `svg-props.html` and
  `svg-boxes.html`, each held to Chromium's screenshot region by region
  (`tests/integ/paintref.sh`: 34, 29, 33, 42, 14, 19, 17 and 23 regions agree), and
  `svg-zoom2.html`, each kind drawn again at a zoom of 1.5 (17 of 17); 12o's pages
  unmoved (37, 40, 9, 11 and 9);
  `tests/exec/svg_render` (scenes of each, a pattern's tile, a mask, a filter and a
  hairline drawn, documents of patterns, masks and markers of themselves and of filters
  and text past every bound; valgrind clean).
- **12u Frames** (`src/shell/frame.c`, `src/shell/frame.h`, `src/script/frames.c`,
  `contracts/net.pl`; the iframe's box: its user agent's border in `src/css/css.c`, its drawing
  in `src/paint/paint.c`'s replaced content, a 3D border of currentcolor in `src/layout/layout.c`;
  an iframe srcdoc document's mode in `src/html/tree.c`; the run split and the hooks in
  `src/script/script.c`, the pipeline in `src/shell/main.c`):
  - each iframe's document: its srcdoc (about:srcdoc, the parent's base URL, UTF-8, never in
    quirks mode by its DOCTYPE), else its src fetched by the page's loader as a document (its
    own encoding sniffed, its own mode; an HTTP error's body shown, with Chromium's console
    line; a failed fetch Chromium's error page, which draws nothing and fires no load), else
    about:blank (quirks mode); a page framing its own URL let do it once, not twice over
    (Chromium's self-reference rule); `loading=lazy` over http: and https: beyond 4000px of the
    viewport left unloaded at rest (Chromium's distance, measured; file: and srcdoc never lazy);
    its parts fetched by a loader of its own, at any depth, up to 32 frames a page;
  - styled and laid out in its iframe's content box as its viewport (its own initial
    containing block, media queries and viewport units), the classic scroll bars its
    overflowing content makes taking their 15px from it and laid out again until they hold
    (none for scrolling=no or a root that clips), and drawn into the content box, clipped:
    twice, over black and over white, so that paint lays it over the page with what it left
    transparent showing through (a frame's canvas is transparent unless its root's colour
    scheme is not its iframe's); the scroll bars as Chromium's headless shell draws them with
    its scroll bars shown (measured: track, arrows, a thumb of round ends 3px in, its length
    the track's less 6px by the share shown, 17px at least, the dark scheme's colours);
  - the iframe's own box: the user agent's `border: 2px inset` (an inset, outset, groove or
    ridge border of currentcolor drawn from #eeeeee, as Blink draws it: the hr's too),
    frameborder as Blink's ToInt() reads it, width and height;
  - each document its own VM, realm and window (src/script/frames.c): parent, top, frames,
    length, the frames by index and by name, frameElement, name, contentWindow and
    contentDocument; one realm's objects seen in another through a membrane of the engine's
    proxies whose traps run in the owner's VM; a document of another origin (a file:'s
    own, a sandbox's without allow-same-origin, another host's) offering only HTML's
    cross-origin properties, else Chromium's SecurityError; postMessage with a structured
    clone and its MessageEvent (data, origin, source), a target origin checked as the message
    is dispatched; sandboxed scripts refused with Chromium's console line, and noscript drawn;
    the order Chromium runs them in (the page's parse, its DOMContentLoaded, each frame's
    scripts, load at its window and at its iframe, the page's load, then every frame's timers
    and tasks as one event loop in virtual time);
  - the frames as facts (`net_frame/6`, `net_frame_parent/2`, `net_frame_fetch/2`,
    `net_frame_viewport/5`, `net_frame_sandbox/3`, `net_frame_quirks/1`), the rules over them
    (`net_frame_of/2`, `net_frame_failed/2`, `net_frame_depth/2`, `net_frame_scrolls/2`), and
    `advise` naming a frame that failed, was blocked or is lazy, and a sandbox that keeps a
    frame's scripts off.

  Gate: `tests/pages/frames*.html`, each held to Chromium: the pages' boxes (22 of 22), their
  regions (22 of 22, the frames' scroll bars shown: `tools/bench/shot.js --scrollbars`), each
  frame's boxes in its own viewport (53 of 53, `tools/bench/frameboxes.js`), and the consoles of
  scripts across frames and realms, read from a file and served over HTTP from two origins, line
  for line (22, 9 and 11 lines); the facts and the diagnostics (`tests/integ/frames.sh`);
  `tests/exec/frames` runs the pages' frames through the pipeline under valgrind.
- **12x Layout's rest** (`src/layout/layout.c`; a flex or grid container's last baseline in `contracts/layout.pl`, a
  fieldset's `min-width: min-content` and justify-items' and -self's first and last baseline in `src/css/css.c`), each
  measured in Chromium first, where it and the specs part Chromium deciding:
  - list markers: an inside marker begins the list item's first line, or, when its first in-flow box is a block, takes
    a line of its own before it (Chromium's inline marker box in an anonymous block of its own: docs/limitations.md had
    Chromium setting it in that block's first line, which measured it does not); an outside one by the first line
    however deep, a nested list's too, or a flex or grid container's first baseline, a line in a box that clips (or a
    container's baseline) setting it in a line of the list item's own, unclipped; with no line, at the list item's top,
    the list item at least that line's height (an empty `<li>`);
  - a fieldset's legend wider than its content box: the fieldset's min- and max-content widths take the legend's, the
    user agent's `min-width: min-content` holds a fieldset of a declared width as wide, and with `min-width: 0` the
    legend overflows the border box from its start (right to left, from the right), the top border left out behind it;
  - a flex container's last baseline (8.5: its last line's last-baseline items', else its last item's) for an item
    aligned on its last baseline, and both baselines in the order the lines and items are drawn (a reversed direction
    or wrap-reverse), as Chromium finds them; an inline-block over a flex or grid container takes that container's
    first baseline unless its last is its last-baseline items' (measured);
  - grid: baseline alignment (align-items and -self baseline and last baseline: a row's group sharing a baseline at
    the row's start, or its end, each item's contribution to its rows with the shift; justify-items and -self's
    fallbacks start and end) and a grid container's own baselines (10.1); its own min- and max-content widths from its
    items placed as its layout places them and its columns sized by the track sizing algorithm under a min-content and
    a max-content constraint (11.5's spans in increasing order, 11.5.4's flexible tracks, 11.7.1's size of an fr), as
    Chromium runs it -- its items' contributions taken as in a layout, not the spec's limited ones; an item's minimum
    contribution as Chromium has it (a width of its own, a min-width, a scroll container, the tracks' fixed maximums),
    min-width and max-width holding its contributions and its stretched width;
  - a sticky inline element at rest, unscrolled: its fragments' union its box, the block whose lines hold it its
    containing block, its fragments, text, atomic boxes and strips moved;
  - the quirks' layout: an inline-block's, a float's met in a line, an inline image's and a block-level image's
    percentage heights taken of their block's definite height, through the blocks of an auto height up to the body in
    quirks mode (an inline-block's min-height and max-height too, in every mode); a list item whose first child is a block-level ul or ol has its
    outside marker on a line of its own (Blink's NeedsOccupyWholeLine, quirks mode alone); a body that fills keeps its
    last child's bottom margin through its bottom under a min-height; and in every mode the body's overflow is the
    viewport's when the root's is visible, so a body that clips makes no formatting context and clips nothing.

  Gate: `tests/pages/markers-nested.html` (boxes 82 of 82, from 4; regions 82 of 82, from 56), `legend-wide.html` (37
  of 37, from 17; regions 37 of 37, from 18), `flex-lastbaseline.html` (126 of 126, from 79), `grid-sizing.html` (490 of
  490, from 249: twenty track lists, each as a float, an inline-grid, `width: min-content` and `max-content` and a block,
  items spanning one to three tracks of text and fixed widths; `tools/gen-grid-sizing.py`), `grid-baseline.html` (97 of
  97, from 63), `sticky-inline.html` (33 of 33, from 22; regions 33 of 33, from 3), `quirks-heights2.html` (27 of 27,
  from 9), `quirks-lists.html` (92 of 92, from 0; regions 92 of 92, from 80), `quirks-bodyclip.html` (5 of 5, from 1;
  regions 5 of 5, from 4), `quirks-bodymin.html` (4 of 4, from 3) and `body-overflow.html` (5 of 5, from 0; regions 5
  of 5, from 2), each held to Chromium's boxes (`tests/integ/boxes.sh`) and drawing (`tests/integ/paintref.sh`); every
  other page's boxes and regions unmoved; `lists.png` and `geometry.png` of `tests/golden/render/` moved towards
  Chromium's drawing (an empty list item's marker).
- **12w SVG effects on HTML boxes, paint's rest** (`src/paint/paint.c`, `src/paint/svg.c`,
  `src/paint/svg.h`; a box's effects and a cloned or clipped inline element's boxes in
  `src/layout/layout.c` and `contracts/layout.pl`: the box fields fx and clone, the fragment field
  clip; `box-decoration-break` and CSS Masking's longhands in `src/css/css.c` and
  `contracts/css.pl`, their initial lists in `prolog/amb/rules/css.pl`), each measured in Chromium
  first:
  - SVG's filters, clipPaths and masks on an HTML box (`filter`, `clip-path` and `mask` by `url()`):
    layout writes a box's effects for its border box as svg.c writes a scene's groups
    (`amb_svg_effects`): its user space from the border box's top left, a CSS px a unit, the border
    box its bounding box (userSpaceOnUse regions, clips and masks measured landing from the border
    box's corner, a border included). Paint draws the box on black and on white into a layer as far
    as its filters reach and through them (`amb_svg_fx`): the filter's list in its order, a run of
    functions as before and a url()'s filter between them, then the clip, then the mask. A url()
    naming no filter, and an empty filter, are passed over (`url(#missing) blur(2px)` blurs). A mask
    naming none masks the box away. A filter's user space has its origin at whole CSS px (the
    layer's: a box at 20.5 x 20.25 filters from 21 x 20, zoomed 1.5 from 31.5 x 30). A clip on a
    box is its shapes' union alone, not held to their bounding box as an SVG element's is (an
    ellipse's top row half covered, not a quarter). On SVG elements a filter's list may mix url()s
    and functions too, each stage a group inside the next's, a run of functions given the region of
    the stage before it;
  - `box-decoration-break: clone` (and its -webkit- name): each fragment of an inline box wrapped in
    every edge. A line the box continues on starts with its start edges, as text-indent would, and
    the fragment there ends with its end edges past the content. Chromium's line break makes no room
    for them (a fragment measured 28.8px past a 240px line). Each fragment's backgrounds, borders,
    radii and shadows are its own (a gradient restarts, an image is placed in each). Inline fragments
    are now drawn in the order their elements open, ancestors first: a nested span's background had
    been hidden under its parent's where both closed on one line;
  - a background that repeats within its painting area is one pattern (Skia's image shader, Blink's
    DrawImageTiled): each pixel sampled bilinearly across its tiles' edges (10px gradient tiles at a
    3.5px offset meet in a pixel half of each), a gradient's tile rasterized as a picture shader's
    (its size rounded up to whole px), a band along an axis it does not repeat along snapped to whole
    px, an image drawn smaller from the deepest mip level still at least the tile's size (Chromium's
    decode cache: a 96 x 32 logo tiled at 24 x 24 from its own pixels, not from the 24 x 8 level);
  - a basic shape or geometry box in an inline element's `clip-path`: its reference box the element's
    first fragment's border box, as Chromium's is (inset(0 0 50% 0) on a span over two lines keeps
    the top half of its first line and nothing of its second); every fragment it holds (its own, a
    nested element's, its text) drawn together and held to the shape, as a block's stacking context
    is (a clip box made once for the element, which each fragment names);
  - CSS Masking's mask layers: `mask` the shorthand of `mask-image`, `-position`, `-size`, `-repeat`,
    `-origin`, `-clip`, `-composite` and `-mode`, lists with their -webkit- aliases, computed as
    Chromium serializes them (an item for each layer of mask-image, a longhand left at its initial
    value too, positions as object-position's, two repeats one keyword, one size's height auto, a
    mask element's URL quoted). A gradient layer is drawn as a background layer is (the same code,
    `pt_layer_one`), its alpha or its luminance the mask; a mask element's layer its group's
    coverage; anything else transparent (a picture is not fetched: Chromium's CORS fetch fails on a
    file: page too); the layers composited from the lowest, each by its operator (source-over,
    source-out, source-in, xor);
  - the planes of a preserve-3d space ordered pixel by pixel, as Chromium's compositor splits them
    with a BSP tree: each plane drawn onto black and onto white (the rectangle it drew kept), each
    pixel the planes reach laid from those it shows, the farthest first, by their depths there (z
    over w at the point the inverse homography finds); a space whose drawings would pass 60 MB drawn
    plane by plane, by their middles' depth. An untransformed box that preserves 3D is now seen
    through its parent's perspective, as its children's planes are in Chromium.

  Gates against Chromium: `tests/pages/svgfx-html.html` (regions, 61 of 61) and, zoomed 1.5 and
  between px, `svgfx-zoom.html` (22 of 22); `clone.html` (boxes 25 of 25, regions 25 of 25) and
  `clonevals.html` (computed values, 12 of 12); `bgseams.html` (regions, 16 of 16); `clipinline.html`
  (boxes 28 of 28, regions 28 of 28); `mask-html.html` (regions, 27 of 27), `mask-zoom.html` (10 of
  10) and `maskvals.html` (computed values, 71 of 71); `planes3d.html` (regions, 10 of 10). Every
  earlier page's boxes, regions and values are as they were; the golden renders that moved
  (inline-cb, paint, fracimg, fracimg-zoom, bgtext, animpaint, svgfx-zoom, transforms) moved nearer
  Chromium's screenshots; the new pages render valgrind clean.

  Cost (callgrind's instructions, `ambitious render`, before and after): the cascade 1.5% more
  (news 4.42M to 4.49M, layout.html 5.53M to 5.62M, style 4.78M to 4.86M), the eight new
  properties every style holds (182 values a style, from 174); layout and paint within 0.1%
  (news's layout 14.33M and its paint 5.87M, both). Wall time, ms, paint (`ambitious bench`, the
  medians of five rounds of ten runs, the two binaries in turn, on a shared machine): the pages
  whose effects are now drawn, each box drawn onto black and onto white on canvases of the page's
  size, about 1.3 ms an effect: `svgfx-html` 7.6, then 45 to 49; `mask-html` 10.2, then 33 to 36;
  `clipinline` 0.8, then 6.8. The pages that draw less now: `bgseams` 17.2, then 5.9 to 7.1 (a
  pattern sampled, not tile by tile); `planes3d` 254 to 260, then 20.5 (a plane's rectangle kept,
  not its canvas). On the way, the black and white canvases are filled at once and what a box drew
  is found a row at a time, each compared whole first, and a clip-path's pass reads only the rows
  its context changed, for every box that draws so: `filters` 69, then 54; `clippath` 47, then 14;
  every page's pixels as they were (163 renders, byte for byte).
- **12v Text shaping** (`src/paint/shape.c`, `src/paint/shape.h` and `src/paint/shape_data.h`,
  which `tools/gen-shape.pl` writes from perl's Unicode tables; the shaper's entry and font
  fallback in `src/paint/font.c`, its glyph runs in `src/paint/font.h`; the built-in faces'
  layout tables by `tools/gen-font.py`'s OpenType subsetter in `src/paint/font_data.h`; runs
  measured and each text fragment's glyphs in `src/layout/layout.c` and `contracts/layout.pl`;
  the glyphs drawn in `src/paint/paint.c`), measured in Chromium first, HarfBuzz's and Blink's
  sources deciding where it left room:
  - text shaped as HarfBuzz shapes it for Chromium: the normalizer (decomposed and composed by
    the face's cmap, marks ordered by their classes), the plan of each font, script and
    direction (its features in the shaper's stages: ccmp, locl, the Arabic joining forms,
    rlig, calt, clig, liga, rclt; kern, mark, mkmk, curs, dist, abvm, blwm), GSUB's eight
    lookup types and GPOS's nine, GDEF's classes and the lookup flags, the kern table where a
    run's script has no GPOS kern, Arabic joining from the context about a run, Hebrew's and
    Thai's own steps; 553 strings in 12 of the machine's fonts and a cursive one of the tests'
    own shaped as HarfBuzz 15 shapes them, glyph for glyph (`tests/exec/shaping`), a mark
    taking its base's offset across the line as it is attached (a cursive chain's, if that
    came first), as HarfBuzz 15 has it;
  - the built-in faces keep their GDEF, GSUB and GPOS, cut to their glyphs and those their
    substitutions reach; `--web ... layout`, `gpos` and `curs` write web fonts with them (the
    last with a cursive attachment of its own, which no font on the machine has);
  - font fallback as Blink's: a grapheme drawn whole by one face, the family's faces first,
    then the system font for the first code point of the first hole left (a right-to-left
    run's holes in visual order: its last first), each taking what it draws; a line's height
    from the fonts its runs used;
  - layout measures each run of pieces as Blink's ShapeText shapes it (one font, across an
    inline element's edge unless a margin, border or padding on that side or its
    vertical-align breaks it; one direction; in the paragraph's text as context): a pair's
    kerning across an edge on its first glyph, a ligature across one drawn by its first
    letter's element, letter-spacing turning ligatures off, the kerning before the space a
    line ends at dropped; each item of a line (a text node's run) placed at its width
    rounded up to a 64th of a px, as Blink places its LayoutUnits;
  - each text fragment keeps its run's glyphs (`frag.glyphs`: glyph, face, pen), which paint
    draws from a cache keyed by face and glyph at Skia's quarter pixel; a fragment with none
    (a placeholder's, a marker's, text an ellipsis cut) is shaped by paint.

  Gate: `tests/pages/shaping.html`, `shaping-arabic.html` (with `fonts/cursive.ttf`) and
  `shaping-kern.html` (with `fonts/serif-gpos.ttf`, kerned by GPOS alone), each held to
  Chromium: their boxes (33, 21 and 15 of them) and their regions pixel by pixel (33, 21 and
  15); every page's boxes (1,849 of 1,849) and regions (1,170 of 1,170) within the gates; 31
  pages' golden drawings moved, 25 nearer Chromium's screenshot and 6 by a quarter pixel
  where other differences hold them off it; `tests/exec/shaping` (valgrind clean);
  `tests/exec/layout_boxes`, each text fragment's glyphs against its text shaped alone in its
  direction, a right-to-left paragraph before left-to-right ones of as many pieces. Its cost
  (`docs/performance.md`): the large page's layout 15.0 ms before, 53.3 ms after, against
  Chromium's 202.2 ms; paint unchanged.
- **12y Network's rest** (`src/net/net.c`, `src/net/dict.c` and `dict.h`, `src/net/zstd.c` and `src/paint/brotli.c`
  over a dictionary, `contracts/net.pl` 0.5.0; the Encoding standard's encoders in `src/html/encoding.c` and
  `src/html/encode.h`, the URL parser's encoding argument in `src/net/url.c`, the sites that resolve a URL in
  `src/css/css.c`, `src/layout/layout.c`, `src/script/` and `src/shell/`, a sheet's encoding in `contracts/css.pl`),
  each measured in Chromium first:
  - HTTP/2's pooling by IP (SpdySessionPool's aliases): a connection made directly carries another host that resolves
    to its address and port and that its certificate covers, a name or an address alike, none through a proxy; a 421
    asked again on the host's own connection, a second its answer; localhost and `*.localhost` resolved as Chromium
    resolves them (::1, then 127.0.0.1);
  - TLS sessions resumed as SSLClientSessionCache keeps them: by host and port, two a server, a TLS 1.3 ticket used
    once, a TLS 1.2 session again and again; a connection closed with its shutdown sent, so that OpenSSL keeps its
    session good;
  - the waits Chromium's: a connect four minutes (its source's), a proxy's tunnel and a TLS handshake 30 s each, an
    answer as long as it takes (measured at 150 s), each capped by `AMB_NET_TIMEOUT` for tests;
  - Compression Dictionary Transport (RFC 9842): Use-As-Dictionary read as RFC 9651's structured field, a response
    kept where Chromium keeps one (potentially trustworthy, readable by its page, a pattern of its own origin, fresh),
    the longest match offered as Available-Dictionary and Dictionary-ID with dcb and dcz, a body checked against the
    dictionary's SHA-256 and decoded over it, the dictionaries kept in the profile for later runs (the system's
    libbrotli 1.1.0 has a dictionary API and libzstd no headers: the decoders are the module's own);
  - a URL's query in its page's legacy encoding (the URL standard's encoding argument): an element's, a background's,
    a frame's, an XMLHttpRequest's, a navigation's, a form's action and window.open's in the document's, fetch()'s
    and new URL's in UTF-8, a fetched sheet's in its own (its charset, @charset, its importing sheet's), the sheet now
    decoded from it, an imported sheet fetched once;
  - `<picture>`: the source an img takes gives it its width and height (Blink's
    CollectExtraStyleForPresentationAttribute);
  - images chosen in the profile's viewport, at the device pixel ratio its zoom makes (the first density at it or
    past it, else the greatest: measured at 1, 2 and 2.5), its font size for sizes' em, its scheme and reduced motion
    for a source's media; a density-corrected size laid out unrounded.

  Gate: `tests/integ/net-h2.sh`: 12 connections over 2 visits, connection by connection (`tests/net/pool.chromium`);
  12 hosts' resumed counts (`tests/net/res.chromium`); 17 requests' dictionary fields and the pages' scripts' results
  (`tests/net/dict.chromium`); an answer after 11 s waited for, a handshake given up after 30 s, each cut short by
  `AMB_NET_TIMEOUT`; `tests/exec/net_dict` (SHA-256's known answers, the field and the patterns as measured, 30 vectors
  of `tools/gen-dict-vectors.py` decoded, 1,200 broken ones decoded or refused, valgrind clean); `tests/integ/net.sh`:
  36 requests of two legacy pages, target by target (`tests/net/query.chromium`); `tests/integ/picture.sh`: 9 of 9
  sources' dimensions (`tests/net/picture-dims.chromium`), 15 of 15 images under each of five profiles
  (`tests/net/env.chromium`).
- **12z Shadow trees rendered** (the flat tree in `src/dom/dom.c`, `src/dom/dom.h` and
  `contracts/dom.pl`: a host's root, each slot's assigned and manually assigned nodes, a shadow
  root's options; a sheet's tree and depth in `src/css/css.c` and `contracts/css.pl`; the flat
  tree walked in `src/layout/layout.c` and `src/a11y/a11y.c`; declarative shadow roots in
  `src/html/tree.c`, their parts fetched in `src/net/net.c`; references by id in
  `src/paint/svg.c`; slots, `slotchange` and `composedPath()` in `src/script/custom.c`;
  inheritance and the context's step in `prolog/amb/rules/`), each measured in Chromium first,
  the specs deciding where it left room:
  - the flat tree (DOM 4.2.2, CSS Scoping 1, 2.2): the DOM finds each slot's assigned nodes
    (named: a host's element and text children by their slot attribute, each to the first slot
    of its name in its tree's order; manual: those `assign()` gave, in its order, while they are
    the host's children) when a walker asks after a change (`flatten`, its revision kept on the
    document node), and keeps them as lists on the nodes; one walk (`dom_flat_first`, `_next`,
    `_parent`, `_walk`) serves every module. The cascade styles the flat tree's elements alone,
    each under its parent there (a slotted one under its slot, a shadow root's under its host;
    a host's child no slot takes has no style, as Chromium's `getComputedStyle` gives none), an
    item blockified by the box it is laid out in past a `display: contents` element (a flex
    host's slotted children are its items: a style keeps whether its children's box is a flex
    or grid container); the user agent's sheet makes a slot `display: contents`; layout lays a
    host's shadow tree out in its place and a slot's nodes (else its fallback) where it stands;
    accessibility walks it, its names from content too; the reasoner inherits through it
    (`dom_flat_parent/2` and `dom_flat_child/2` over the facts `dom_assigned_slot/2`,
    `dom_slot_first/2` and `dom_slot_next/2`);
  - style scoping: a shadow tree's style elements and links make sheets of its tree, whose rules
    match its elements alone, a combinator going up from the root's elements to the featureless
    host (`:host`, `:host()` and `:host-context()` alone match it: neither `*`, `.k` nor
    `:not(:host)`, and nothing is above it); `:host()` and `:host-context()` (the host or an
    ancestor in the flat tree, as Chromium walks them) a pseudo-class and their argument's
    specificity; `::slotted()` an element assigned to one of the tree's slots, through a slot
    assigned to a slot, a pseudo-element and its argument's; `::part()` an element of a host's
    tree by its part names or that a deeper one's `exportparts` gives them, `:host::part()` a
    tree's own parts; pseudo-elements after each. A sheet's encapsulation context, its tree's
    depth, ranks after origin and importance and before the style attribute (Cascade 5, 6.1):
    the outer winning a normal declaration (a `::part()` rule over the part's own style
    attribute), the inner an important one (`:host { color: red !important }` over the host's
    important style attribute); a tree's elements' hints and style attributes in sheets of its
    depth; `css_lost/4` names the step (`context(lost(shadow_tree(depth(1))), won(document))`);
    an SVG's `url(#id)` or `href` (an HTML box's `clip-path`, `mask` and `filter` too) names the
    element of that id in the tree of the element whose value or attribute it is, as Chromium
    looks (a `::part()` rule of the document's in the part's tree), never in a template's
    contents;
  - declarative shadow DOM (HTML 13.2.6.4.4): a template whose `shadowrootmode` is open or
    closed, in any case, in a document parsed whole, attaches its parent's shadow root when the
    parent may host one and hosts none, with `shadowrootdelegatesfocus`, `shadowrootclonable` and
    `shadowrootserializable`; the template is pushed alone, the root its contents, and is never in
    the document; a second one, one of a mode no one knows and `innerHTML`'s are templates as any
    other; the loader fetches a page's parts in shadow-including order, a declarative tree's
    links and images among them;
  - scripts: `attachShadow()`'s `delegatesFocus`, `slotAssignment` (a value it does not know
    refused as Chromium refuses it), `clonable` (a host's clone, deep or not, clones its root) and
    `serializable`, read back; `assign()` and its refusal of a value no Element or Text;
    `slotchange`: each change of a slot's assigned nodes, looked for at the end of each call into
    the host, signals the slot once (a node's slot attribute changed, its old slot before its
    new), its event fired after the observers' callbacks; `composedPath()` hiding a closed tree's
    nodes from a listener outside it (DOM 2.9), a nested open tree's too; `appendChild` and
    `insertBefore` into a shadow root (refused before).

  Gate, each page held to Chromium: `tests/pages/shadowvals.html` (computed values, 78 of 78,
  from 9: `tools/bench/cssref.js --shadow`, which reads the open trees' elements too),
  `shadow-layout.html` (boxes 45 of 45, from 0; regions 32 of 32, from 0:
  `tools/bench/boxes.js --shadow`), `shadow-dsd.html` (boxes 29 of 29, from 0; regions 19 of 19,
  from 0), `shadow-svg.html` (regions 19 of 19, from 10; five of them the document's
  definitions', of no size), `shadow-slots.html` (its console, 54 lines, Chromium's line for
  line, from 1 of them: the first script stopped at a declarative root's absence) and
  `a11y-shadow.html` (its accessibility tree, 12 lines, from 3; `tools/bench/axtree.js` reading
  the shadow trees' ids);
  the reasoner's flat tree and a root's options (`tests/integ/script-custom.sh`); every other
  page's boxes, regions, values, consoles and trees as they were; the new pages render, and their
  scripts run, valgrind clean. Its cost (`docs/performance.md`; callgrind, and `ambitious bench`
  against main's build): the large page's cascade 4.7% more instructions (55.5M to 58.1M; 6.4 ms
  and 6.6 ms), its accessibility tree 4.9% (10.7M to 11.2M), its layout none (488.9M to 485.9M),
  the smaller pages' cascades within 0.4%; the reasoner's `advise` over the large page 4%
  (1,024 ms to 1,069 ms), the flat tree's parent asked first as the DOM's in a document without
  shadow roots.
- **12aa Writing modes** (`src/layout/layout.c`: each box laid out in its writing mode's frame; the box field wm and
  the fragment fields orient and vref in `contracts/layout.pl`; writing-mode, text-orientation,
  text-combine-upright and CSS Logical 1's flow-relative properties in `src/css/css.c`, `src/css/css.h` and
  `contracts/css.pl`, their computed values in `prolog/amb/rules/css.pl`; vertical runs shaped in
  `src/paint/font.c` and `src/paint/shape.c`, UAX #50's Vertical_Orientation in `src/paint/shape_data.h` from
  `tools/gen-shape.pl`; vertical lines drawn in `src/paint/paint.c`), each measured in Chromium first:
  - `writing-mode` (horizontal-tb, vertical-rl, vertical-lr, sideways-rl, sideways-lr; SVG 1.1's tb, tb-rl, lr,
    lr-tb, rl and rl-tb and `-webkit-writing-mode` computed as Chromium computes them): a box is laid out in its
    mode's frame, as a horizontal box is with its axes turned -- its style's edges and sizes read turned (a vertical
    box's height its inline size, its right margin vertical-rl's block-start one), its boxes, lines and fragments
    turned back once laid out -- so that its lines stack along the block axis (right to left, or left to right),
    each a column of text; percentages are taken of the right axis, margins collapse along the block axis, a float's
    left and right are a vertical line's top and bottom; vertical-lr's lines, whose over side is their right, have
    what they hold mirrored in them. A box whose writing mode is not its parent's makes a formatting context of its
    own (3.1): laid out in its own frame, then set in its parent's, its edges turned. An orthogonal one's available
    inline size is its parent's definite block size, else the viewport's (7.3, as Chromium has it), a percentage of
    its inline size taken of that too; its auto inline size fits its content in it; its auto margins along its
    parent's inline axis share what is left; its min- and max-content contributions to its parent are its block size
    laid out; an inline-block of the other axis is laid out before its line, its baseline its margin box's bottom (in
    a vertical line, whose baseline is the central one, its middle), an inline element of another mode an inline-block
    (Blink's StyleAdjuster); a positioned box's offsets are read in its containing block's frame. The root's writing
    mode -- its body's when it has one (8: the principal writing mode, propagated as Chromium does) -- is the initial
    containing block's: a vertical-rl page set from the viewport's right edge, its inline size the viewport's height.
    The user agent's sheet's margins and paddings are flow-relative, as Chromium's html.css has them;
  - text in vertical lines: their dominant baseline the central one (a font's height halved, the descent's half
    floored, as Blink's FontMetrics has it), the alphabetic one under the sideways modes; text-orientation (mixed,
    upright, sideways; sideways-right and vertical-right Chromium's other names): in mixed each grapheme upright or
    turned as its first code point's Vertical_Orientation has it (UAX #50: U, Tu and Tr upright; perl's Unicode::UCD
    carries the property, which tools/gen-shape.pl writes into shape_data.h). Upright runs are shaped top to bottom as
    HarfBuzz shapes them for Blink: the common features and vert (found under any script), or, for a font with no vert,
    Unicode's vertical presentation forms the font has (hb_vert_char_for: WenQuanYi Zen Hei's brackets); each glyph
    advancing by its vmtx advance (WenQuanYi's ideographs by 0, as in Chromium; a mark by none, as HarfBuzz has it), or
    by the font's height where the font has none, from its vertical origin (VORG's; vmtx's top side bearing above the
    glyph's top, that top rounded up to a whole px as Blink's glyph bounds are; or the ascent); turned runs are shaped
    as a horizontal line's;
  - `text-combine-upright: all` in vertical-rl and vertical-lr (whatever the text's orientation; not in the sideways
    modes, as Chromium has it): each text node one piece an em down its line, its white space at its ends dropped, no
    break between it and the letters about it, its run shaped alone as a horizontal one, its box where its text would
    be (the font's height across the line); paint sets its text across the line, centred, or squeezed across into 1.1em
    (an em under an underline or overline) when it is wider, as Blink's LayoutTextCombine does (its scale_x; the
    machine's fonts have none of the width variants it tries first), on the baseline Blink's line of an em gives it,
    the line's decorations running along it;
  - paint draws a vertical line's turned glyphs a quarter turned (clockwise; sideways-lr's anticlockwise) on its
    alphabetic baseline, Skia's subpixel axis along the line; upright ones centred on its central baseline (half the
    font's unrounded height below the line's over edge, Blink's FixedAscent); each line's text set from its first text
    fragment's start (or that of the decorated inline box it is in) snapped to a whole pixel, as Chromium sets it; its
    text-shadows, a turned glyph's offset turned with it, an upright or combined one's as it is (measured); an
    underline and an overline outside the text's under and over edges, a sideways line's turned with its glyphs; an
    inline box's decorations along the line;
  - CSS Logical 1: the flow-relative margins, paddings, borders (their widths, styles and colours; the shorthands of an
    axis and of a side), insets, sizes (inline-size, block-size, their minimums and maximums) and corners' radii, and
    Chromium's old `-webkit-` names of them, kept as declared and taken by the cascade for the physical property the
    element's writing mode and direction make each, in its place in the cascade's order among that property's
    declarations (the writing mode and direction the element inherits, or, when it sets its own, those); float and
    clear's inline-start and inline-end by their containing block's direction; getComputedStyle and the reasoner's
    css_computed/4 give a flow-relative property its physical one's value.

  Gate: `tests/pages/wm-blocks.html` (boxes 30 of 30, from 1 before; regions 30 of 30, from 3), `wm-root.html` (12 of
  12, from 0; regions 12 of 12, from 0), `wm-text.html` (15 of 15, from 0; regions 15 of 15, from 8),
  `wm-logical.html` (42 of 42, from 0), `wm-flex.html` (flex, grid and a table in vertical modes: 30 of 30, from 0;
  regions 30 of 30, from 0), `wm-ortho.html` (32 of 32, from 0; regions 32 of 32, from 2), `wm-combine.html` (combined
  text: 29 of 29, from 0, and 16 with writing modes but not combined text; regions 29 of 29, from 2 and 14),
  `wm-shadow.html` (text shadows in vertical lines: 9 of 9, from 0; regions 9 of 9, from 2, and 0 with writing modes
  but no vertical shadows) and `wmvals.html` (computed values 126 of 126, from 25), each held to Chromium; every
  earlier page's boxes, regions and values as they were, and its drawing byte for byte (180 renders); the new pages
  render valgrind clean, their leaks checked. Its cost (`docs/performance.md`): on horizontal pages, none past the
  timings' noise (the large page's layout 67.5 ms before, 68.1 after; news.html's 0.54 and 0.45) -- by callgrind, the
  large page's layout 485.7 million instructions before, 487.8 after (+0.4%), its paint +0.3% and its cascade +0.3%:
  the box and fragment records no bigger (the new fields in their padding), a horizontal run shaped, measured and
  packed on its old paths (a vertical run in loops of its own), an upright run's glyphs set apart after its shaping.
- **12ab SVG's rest** (`src/paint/svg.c`; `lighting-color` and `color-interpolation` in `contracts/css.pl`,
  `src/css/css.c` and `prolog/amb/rules/css.pl`; an feImage's picture fetched by `src/net/net.c`), each measured in
  Chromium first, Skia's and Blink's sources deciding where the measurements left room:
  - the filter primitives Skia draws for Chromium: feTurbulence as SkPerlinNoiseShader makes it (the spec's lattice
    and gradients, kept in sixteen bits; each device px's centre mapped into the filter's user space and half a unit
    on; a seed truncated, the stitched tile the subregion's size from the space's origin, a negative frequency 0),
    turbulence and fractalNoise matched value for value; feMorphology (radii mapped to device px and rounded, a
    negative one none, outside the image transparent); feConvolveMatrix (the kernel turned about its target, the
    divisor, the bias added to premultiplied colours or, preserving alpha, to unpremultiplied ones, the edge modes
    about the subregion, an invalid order, kernel, target or divisor transparent); feDisplacementMap (its map
    unpremultiplied, the scale horizontal alone under objectBoundingBox as Blink takes it, the nearest px);
    feTile (the px inside its input's subregion, from where they are; feTile's own subregion the region, as Blink
    resets it); feImage (an element drawn as a use draws it, moved to the subregion's corner as Blink moves it; a
    picture the loader fetches, a PNG or an SVG document placed by preserveAspectRatio from its natural size, in
    sRGB); feDiffuseLighting and feSpecularLighting with fePointLight, feSpotLight (its cone's edge smoothed as
    Skia's) and feDistantLight, surfaceScale, the constants, specularExponent and lighting-color (a property now),
    Sobel's kernels with the bounds' edge px repeated and each px lit at its centre, as Skia's lighting filter;
  - a mask's content painted in linearRGB element by element where its `color-interpolation` (inherited, a property
    now) says so, as Chromium paints it (measured: an element's own value counts, the mask's inherited);
  - textPath's `path` attribute (over its href; its href a path element's alone, another shape drawing nothing,
    as Blink's LayoutSVGTextPath takes it), and a path's `pathLength` scaling a stroke's dashes and a textPath's
    startOffset (not its percentages; 0 a solid stroke, a negative one none);
  - SVG text shaped (`src/paint/shape.c`): runs split where Blink splits SVG text's items (an absolutely placed
    character, a textPath's edge) and at a change of face, size, letter-spacing or direction; ligatures, kerning
    across tspans, marks and Arabic's joining; a cluster's glyphs drawn by the character it begins at, its later
    characters (a ligature's, marks) with no place of their own (their dx moving the pen, their rotate none);
    textLength spread between typographic characters; a right-to-left script's run set in its visual order;
  - an `<image>` whose picture fails to load or decode drawn as Blink's broken image (its own 14 x 16 px picture,
    measured), by preserveAspectRatio as a raster is (none for an auto width or height, or under
    preserveAspectRatio none); an SVG document drawn as an image draws its image elements' and feImages' data:
    URLs (rasters and SVG documents) and fetches nothing else, not even a broken image;
  - measured and left as Chromium has them: textPath's side, method and spacing, a filter's href and
    feGaussianBlur's edgeMode, none of which Chromium reads; `vector-effect`'s values but none and
    non-scaling-stroke, which its parser refuses, now refused by the cascade too.

  Gate: `tests/pages/svg-fe.html` (75 regions), `svg-light.html` (35), `svg-rest.html` (53), `svg-shaped.html`
  (18), `svg-broken.html` (17, with `svg-inner.svg` and `svg-inner2.svg` drawn as images) and, zoomed 1.5,
  `svgfe-zoom.html` (26), each held to Chromium's screenshot region by region (`tests/integ/paintref.sh`): all
  agree, from 41, 22, 36, 4, 4 and 15; every earlier page's regions as they were (SVG text's pages too, now
  shaped), and no earlier golden render moved; `svg-restvals.html`'s computed values of lighting-color,
  color-interpolation and vector-effect (19 of 19, from 3; `tests/integ/cssref.sh`); `tests/exec/svg_render`
  (the primitives' scenes and drawings, a page of every new attribute, and a hostile document of them past every
  bound; valgrind clean).

  Cost (`docs/performance.md`): a page without them as before (news.html's render 213.94M instructions, then
  213.95M: the cascade 0.2% more for the two properties every style holds); SVG text's scenes shaped,
  svg-text.html's layout 21.3M instructions, then 23.7M, svg-textpath.html's 33.5M, then 34.5M. Each primitive
  over an 800 x 600 element, paint (`ambitious bench`, the medians of three rounds of five runs): feTurbulence
  (4 octaves) 130 ms, feMorphology 80, feConvolveMatrix (5 x 5) 75, feDisplacementMap over a turbulence 99,
  feTile 60, feImage 36, feSpecularLighting 109, against feGaussianBlur's 90 and the element unfiltered 19. The
  colour conversions every filter makes (sRGB to linearRGB and back, a power a channel) are now looked up in a
  table of every eight-bit colour at every alpha, made once a process: feGaussianBlur's element was 143 ms,
  svg-filter.html's paint 34.9 ms, then 26.4; every page's pixels as they were (186 renders, byte for byte).
- **The toolchain's findings** are cicilang's and cocolog's to fix, upstream;
  their workarounds stay until then.

---

## 8. Testing and the gates

`make test` runs these, in order, and stops at the first red:

1. **contract**: the validator over `contracts/` and `tests/contract/bad_*`.
2. **gen**: generated outputs against `tests/golden/`.
3. **exec**: cicilang-built test programs; each prints lines compared with
   `.expect`.
4. **safe**: programs cicilang must refuse; each `.expect` holds the
   expected diagnostic.
5. **reason**: snapshots → queries → expected answers.
6. **integ**: full loops through the `ambitious` binary.
7. **Later phases' suites** (html, css, render, net), each added with its
   phase.

Every cocolog run in the gates has a time cap and a memory cap, as
cicilang's own gates do. A cicilang diagnostic is matched by its stable part
(the error kind and the name), not by the whole line.

`make bench` is the performance gate, outside `make test` (§11). It compares
Ambitious with Chromium page by page, and it compares the JavaScript suite
with V8 and QuickJS. A performance change is done when every row it targets is
won.

---

## 9. Status

| Phase | State |
|---|---|
| 0 Toolchain | **done**: `toolchain/`; `check.sh` GREEN; report with 13 findings (the 9th: builds name their target, so the code runs under valgrind and on other machines) |
| 1 Contract | **done**: `docs/contract.md`, `contracts/dom.pl`, the validator; contract gate GREEN (32 refusals) |
| 2 Macro integration | **done**: `include/amb.pl` and `prolog/amb/gen_c.pl` make the DOM's C from its contract; gen gate (goldens) and exec gate GREEN, valgrind clean |
| 3 DOM vertical slice | **done, the first milestone**: each §6.3 criterion has a passing test (the table in Phase 3); `bin/ambitious loop` runs the whole loop; `make test` GREEN |
| 4 HTML parsing | **done, first slice**: the WHATWG tokenizer (every state, the 2,231 named references) and tree builder (the insertion modes, adoption agency, foster parenting, fragments) over `contracts/html.pl`. html5lib tree construction: **1,374 of 1,721 cases run pass (79.8%)**; every failure is foreign content (218), template contents (109) or the 2025 select rules (20). Parse errors are facts |
| 5 CSS | **done, first slice**: `contracts/css.pl` and `src/css/css.c`. It covers sheets (comments, strings, `@media` against an 800x600 viewport), Selectors 4 (attribute operators, the structural pseudo-classes, `:nth-*()`, `:not`, `:is`, `:where`) with their specificity, and the cascade (origin, importance, the style attribute, specificity, order). It also covers inheritance, the global keywords, presentational hints, and 26 computed properties with the shorthands that set them. Each computed value is a fact with its winning declaration. `css_explain/3` says why, `css_lost/4` names the cascade step at which each loser lost, and contrast and unused rules are diagnostics. `tests/exec/css_cascade` (120 cases, valgrind clean) and `tests/integ/style` |
| 6 Layout and rendering | **done, first slice**: `contracts/layout.pl`, `src/layout/layout.c`, `src/paint/`. Block layout covers CSS 2.1 widths, auto margins, max-width, box-sizing, and margins collapsed between siblings, through parents and through empty boxes. Inline layout covers the white-space modes, line breaking, baselines, left/right/centre/justify and inline backgrounds. Also: inline-blocks shrunk to fit, replaced elements, tables in automatic layout (colspan, cells centred), and list markers. Text is set in six DejaVu faces from outlines (`tools/gen-font.py`), rasterized by exact area coverage, and drawn into a PNG by the painter's own deflate. `ambitious render`. Every box has a width explanation that reaches the CSS rule behind a declared width; facts answer hit-testing and overflow. `tests/exec/layout_boxes` (valgrind clean) and `tests/integ/render`, which pins three pages' pixel checksums; `tests/golden/render/` holds their PNGs |
| 7 Networking | **done, first slice**: `contracts/net.pl` and `src/net/net.c`. URLs follow a WHATWG subset: `tests/exec/net_url` resolves all of RFC 3986's §5.4 examples. Fetching covers `file:`, `data:` and HTTP/1.1 (Content-Length, chunked, redirects bounded at five, relative Location). A page's stylesheets, images and scripts are fetched once each, with the element and page that asked; a page served over `http:` may not read `file:` (decided in C). Linked sheets join the cascade in document order. Images take their intrinsic size from their headers (PNG, GIF, JPEG), and PNGs are decoded and drawn (`src/paint/image.c`, its own inflate; `tests/exec/png_decode`: 19 fixtures whose pixels are known independently). Facts cover resources, origins, who asked and redirects; the rules give the dependency graph, cross-origin resources and failures. `tests/integ/net` serves a page of many parts from a local server: the gate |
| 8 JavaScript | **built** (redesigned by the benchmark, §11): an engine of Ambitious's own in cicilang (NaN-boxed values, shapes with inline caches, stack bytecode) in place of embedded QuickJS; `tests/js` checks it line by line against V8, and the suite is faster than QuickJS on 8 of 8 workloads. The `script` module runs a page's scripts, events and timers through DOM bindings whose every change is a DOM operation; its facts and rules find uncaught errors, unsettled timers and click listeners a keyboard cannot reach. The interop page's console is Chromium's line for line; the scripted page wins its rows against Chromium |
| 9 Accessibility and devtools | **done, first slice**: `contracts/a11y.pl` and `src/a11y/a11y.c` build the accessibility tree: HTML-AAM roles, role attributes (unknown ones recorded), and accessible names with their source (aria-labelledby, aria-label, alt, label, caption, legend, content, value, title, placeholder). Hidden content is left out, and every ID reference is kept with its target. The rules find missing names, broken references, heading jumps, nested interactive controls, duplicate ids and unknown roles, and explain each name. `ambitious inspect PAGE [--query|--why GOAL]` shows the page, its resources, the tree and every module's diagnostics. `tests/integ/a11y` is the gate: a fixture with every kind of fault, each found with its node and its rule |
| 10 Persistence | **done, first slice**: `prolog/amb/profile.pl` keeps the profile in a cocolog store. `--profile DIR` (or AMB_PROFILE) uses `--embed DIR`, ZiguratIP's MVCC store, from one run to the next; `--private` uses `--local`, memory for the run alone; with neither, nothing is kept. The keeper writes six kinds of fact (visits, bookmarks, permissions, preferences, sessions, retention) from named commands. A profile query is checked read-only before it runs. Retention is applied at each opening; forgetting removes what it names (a URL, a site, a time on). A site's permissions reach C as the kinds the loader may not fetch. `tests/integ/profile` is the gate: written by one process and read by the next, a private session leaving the profile's files byte-identical, forgetting, retention, permissions, and the reasoner unable to see the profile |
| 11 Hardening | **done, first pass**: `docs/performance.md`, `docs/security.md` and `docs/limitations.md` are the gate's measured numbers and documented limitations. `ambitious bench` times each stage and the reasoner. Units are built at `-O2` (2.9x faster, pixels unchanged). The reasoner keys handle facts by integer (on 21 MB, `advise` went from 77 s to 11 s) and streams snapshots past cocolog's 16 MiB read. The audit fixed three crashes, now `tests/exec/hostile`: 100,000-deep markup, 20,000-deep `:is()`, and backtracking descendant chains. `tests/exec/fuzz_pipeline`: 60 hostile documents through the whole pipeline under valgrind |
| 12 The limitations | **in progress**: the packages 12a-12y merged, each with its own files and gate |
| Benchmark | **48 of 48 rows won against Chromium 1243** (§11, `docs/benchmark.md`; the first run won 38). Each lost row changed the implementation, as §11 says: compiled and bucketed selectors, shared computed styles, a streamed snapshot of a third the size, opaque fills by rows. Now **64 of 64**, the scripted page's scripts and its click among them; the JavaScript suite 8 of 8 against QuickJS |

## 10. Risks

- **Speed against a production engine.** §11 holds Ambitious to winning the
  benchmark. A row it loses changes the implementation before more is built
  on it, and a row it cannot win, such as a JIT's, is named rather than
  hidden.
- **The compiler on a large program.** cicilang is proven on fixtures and
  ccllib, not yet on a codebase of this size. Mitigations: small units, a
  compile after each step, and each defect cut to a reduction and recorded
  as a finding for cicilang's owner. Ambitious never edits its neighbours.
- **The check's strictness.** It is met by design (handles, arenas,
  short-lived borrows) rather than worked around.
- **Build time.** Units stay small, and cicilang's IR cache serves
  unchanged ones.
- **No ccllib string.** Byte pools and spans until the string module lands.
- **JavaScript.** A large independent effort, kept behind its boundary in
  Phase 8.

---

## 11. Performance against a reference engine

**The rule.** Ambitious is measured against a production engine on the same
box, and it must win. A row of the comparison that Ambitious loses changes the
implementation, here in the plan, before more is built on it. `make bench`
(`tools/bench/compare.py`) runs the comparison. `docs/benchmark.md` holds its
latest run. The comparison is not part of `make test`, because it needs the
reference engine and takes minutes. It is the gate of every performance
change.

**The reference.** Chromium 1243 (Playwright's headless shell), on the same
pages, in an 800x600 viewport at scale 1:
- **Its stages** are the durations of its own trace events: ParseHTML; then
  UpdateLayoutTree and ParseAuthorStyleSheet; Layout; and PrePaint, Paint and
  Layerize, with the raster threads' RasterTask.
- **Its whole loads** are timed from outside. *Warm*: one browser, each load
  starting from `about:blank`, through the load event and a forced layout,
  then a full-page PNG. *Cold*: a browser launched for the page.
- **Diagnostics**: axe-core 4.14.0, injected into the loaded page and run.
- **JavaScript**: `tools/bench/js/suite.js` on V8 (node 22), on V8
  `--jitless`, and on QuickJS 2021-03-27, built from its source with clang -O2.
  QuickJS is the engine Phase 8 had planned to embed.

**The first run.** Ambitious was faster in 38 of 48 rows:
- It won parse on every page (1.2–6.9x).
- It won layout on 5 of the 6 pages (6.2x at 16,000 nodes).
- It won paint and raster on 5 of 6 (7.8x at 16,000 nodes).
- It won every whole load but one, warm and cold (cold to PNG, 6–8x on small
  pages).
- It won diagnostics on the four small pages (1.2–2.1x).

It lost these:
1. Style on 5 of the 6 pages: 1.3x on news.html, up to 8.3x at 16,000 nodes
   (131.8 ms against 15.9 ms).
2. Warm, laid out, at 16,000 nodes: 172.6 ms against 88.5 ms. This follows
   from loss 1.
3. Diagnostics at scale: 950 ms against 415 ms at 1,600 nodes, and 11.5 s
   against 2.1 s at 16,000 nodes.
4. Paint and raster on news.html: 4.0 ms against 3.0 ms.
5. JavaScript, which Ambitious did not have.

**What each loss changed:**

| lost | measured cause | the implementation now |
|---|---|---|
| style | callgrind, large page: selector text split and matched anew for every rule against every element is 48% of the run (`css_split` 23%, `css_parts_match` 25%); each computed value's text written by `snprintf`, 7% | **Compiled selectors.** Each selector is compiled once per sheet into compounds: type, id and classes as interned atoms; attribute tests; pseudo-classes with their arguments parsed; combinators right to left. **Rule buckets.** Rules are bucketed by their rightmost compound's id, else a class, else the type, else universal, so an element meets only its own buckets' rules. **An ancestor filter.** A Bloom filter of the ancestors' ids, classes and types rejects descendant and child chains before the exact walk. Values' texts get a formatter of their own. Target: under Chromium's style time on every page |
| warm, laid out | the cascade, as above; layout's `lay_style_of` searching all computed values, 11% | the cascade as above; each node's computed values found by an index |
| diagnostics | the reasoner reads at 27 µs a fact. It holds each line as a string and parses it again, finds its declaration by a list scan, checks its shapes through `=..` and `call`, and holds every line and fact in lists. cocolog itself reads and asserts at 3.8 µs a fact with `read_term/3` | **A streamed snapshot.** `read_term/3` reads the file. The data is still never consulted: no directive runs, and only the shapes of declared facts are accepted. Each fact is checked against a declaration indexed by its predicate and stored as it is read. Target: under axe-core's time on every page |
| paint on a small page | opaque fills written a pixel at a time through checked indexing (`pt_fill`, 11% of the run) | opaque fills written a row at a time; the canvas cleared in one fill |
| JavaScript | not built | Phase 8 as revised: an engine of Ambitious's own, in cicilang, built to be measured (Phase 8) |

**The latest run** (`docs/benchmark.md`): Ambitious is faster in **48 of 48
rows**. The cascade had first been made 3x faster by splitting each selector
once, bucketing the rules and adding the ancestor filter. That still lost to
Chromium at 16,000 nodes, so two further changes followed:
- **Shared computed styles.** An element's values depend only on its matched
  rules and its parent's values, as in Blink's matched-properties cache. So
  elements with the same matched rules under parents of one style share one
  style, recorded once.
- **The CSS facts follow the styles.** The snapshot carries `css_style_of/2`,
  each style's declared values (`css_style_declared/4`, `css_style_won/3`,
  `css_style_rgba/3`) and the initial values (`css_initial/2`). The rules
  derive `css_computed/4`, `css_rgba/3`, `css_declared/4` and `css_won/3`,
  which answer as the facts did.

The results on the 16,000-node page:
- style: 131.8 ms to 4.5 ms, against Chromium's 15.1;
- the snapshot: 408,681 facts to 147,006;
- diagnostics: 11.5 s to 1.1 s, against axe-core's 2.1 s.

**The JavaScript rows.** V8's JIT runs the suite's loops, calls and closures
3–10x faster than either interpreter. V8 without its JIT and QuickJS are
within 1–2x of each other. No interpreter wins a JIT's rows. A JIT needs
memory that is written and then executed, which the safe part cannot express,
so those rows are reported, not targeted. The rows Phase 8 must win are
QuickJS's, the engine it replaces, and Chromium's on scripted pages: a list
built by script, and a click handled and drawn again.

**The JavaScript rows, Phase 8a** (`docs/benchmark.md`). The engine's first
correct run lost four of QuickJS's rows: loop 275 ms against 117, fib 22.5
against 6.4, closures 146 against 51, and arrays 41.8 against 24.2. Callgrind
put each loss in the interpreter:

| lost | measured cause | the engine now |
|---|---|---|
| fib, closures | about 480 instructions a call. Each instruction's dispatch cost 9 to 14 of them, plus the exception check after it, and a comparison's jump was decoded from its operands | a comparison and its jump in one instruction, one per relation; a local's own compare-and-jump, arithmetic and return; calls without `this` (CALL0); a closure's frame built in place; simple instructions skip the exception check; a switch with no range check |
| loop | the remainder computed by `fmod` and `trunc`, which are library calls on the generic CPU; the loop's own jump | integers' remainder by integer division; loops test at their foot |
| arrays | a native's callback took about 330 instructions to enter; `a.push`'s cache never filled for an array | callbacks enter through `js_call`'s own frame, read through borrows; arrays' named properties cached |

The latest run is faster than QuickJS on **8 of 8** workloads: 1.2x on arrays
and 1.3x on closures, up to 2.4x on objects. It is also faster than V8's
interpreter on loop, fib, objects, classes and closures.

**The scripted page, Phase 8b.** `tools/gen-scripted-page.py` writes a page whose
script builds a list of 2,000 items, and whose button's click puts every item on
sale. Two rows were added to the comparison:
- **the scripts, as a stage**: Ambitious's scripts stage against Chromium's
  traced `EvaluateScript` and `FunctionCall`;
- **a click handled and drawn again**: Ambitious's click, cascade, layout and
  viewport paint against Chromium's traced dispatch and the frame it draws.

At the first measure the click came out even, 60.9 ms against 60.8. Paint
redrew the whole page, 44,000 pixels tall, where Chromium rasterizes the tiles
on screen. The painter now draws a window of the page (`amb_paint_view`), the
viewport a click is seen in. The latest run (`docs/benchmark.md`) wins **64 of
64** comparisons. On the scripted page:
- the scripts: 5.1 ms against 15.9;
- the click, drawn again: 19.4 ms against 57.6;
- warm, laid out: 16.7 ms against 111.0.
