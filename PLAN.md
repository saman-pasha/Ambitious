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
| **cicilang** (0.284) | Compiler for every browser module | Safe Modern C (C17/C23) lowered to LLVM IR; the ownership check (`own`, `move`, borrows, `x tie y`); `defer`; `:=`; macros written in cocolog (`#include "m.pl"`, `#cocolog … #end`); **type macros** (`vector<int>`), **C text with holes** (`ccl_c_items/3`), functions made at their first call (`fn_N/3`), calls found by the first argument's type, **autodrop** |
| **ccllib** | The standard library of that C | `vector`, `array`, `deque`, `list`, `forward_list`, `stack`, `queue`, `priority_queue`, `set`/`multiset`, `map`/`multimap`, the four `unordered_*`. Each is a cocolog macro file. |
| **cocolog** (3.62.0) | Reasoning, contracts, generation | A Prolog in the SWI-Prolog 9.0.4 dialect, with one namespace. It has an MVCC store embedded over ZiguratIP (`--embed DIR`), `library(process)`, `library(html)`/`xml`/`json`, and the flow libraries for charting and debugging Prolog. |
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
    `tests/exec/script_gc`).
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
  `src/paint/paint.c`; `document.compatMode` in `src/script/input.c`):
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
    Blink's fallback has it;
  - in every mode, as the quirks' pages need them: a cell's `nowrap` and an
    image's `align` left or right (a float) read as presentational hints.

  Gate: Chromium's mode for 107 document starts (`tests/exec/quirks_mode`,
  `tools/bench/doctypes.js`; the four whose system identifier is empty kept as
  the standard has them); `tests/pages/quirks-*.html`, quirks-mode pages and one
  limited-quirks page, held to Chromium's boxes (337 of 337), regions (35 of 35)
  and computed values (150 of 150, `:hover` measured with the mouse over the
  page: `tests/pages/NAME.hover`); the mode's facts (`tests/integ/quirks.sh`);
  every standards-mode page unmoved.
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
| 12 The limitations | **in progress**: the packages 12a-12o, each with its own files and gate |
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
