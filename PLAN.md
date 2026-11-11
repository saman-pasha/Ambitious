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

*As built:* the facts are `css_matched/2`, `css_won/3`, `css_computed/4` and
`css_rgba/3`, beside the sheets, rules and declarations. `css_explain/3`
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

### Phase 8: JavaScript integration

- Integrate an embeddable engine (QuickJS, MIT) as a native library.
- Host objects and bindings in cicilang C: document, element, text,
  events, timers.
- Event-loop integration. Engine internals stay behind a module boundary,
  and only selected metadata reaches the reasoner.

**Gate:** a basic interactive page runs, with DOM interoperability tests.

### Phase 9: Accessibility and developer tools

- `src/a11y/`: an accessibility tree with roles and names (from content,
  `alt`, `aria-label`, `aria-labelledby`, `<label for>`).
- Rules for missing names, broken references, inconsistent roles and
  heading order.
- `ambitious inspect`: DOM, style, layout and resources, plus `--query`
  and `--why`.

**Gate:** diagnostics on fixture pages, each naming its nodes and its rule.

### Phase 10: ZiguratIP persistence

- A profile is a cocolog `--embed` store holding history, bookmarks,
  sessions, permissions and preferences, with retention and deletion
  rules.
- Private browsing uses the memory store.
- The profile store is isolated from page snapshots.

**Gate:** knowledge written by one process is read by the next; a private
session writes nothing; deletion removes what it names.

### Phase 11: Compatibility, security, performance

- More standards and interoperability tests.
- A security audit of the boundaries: the snapshot quoting, the request
  validation, capabilities.
- Profiles of the hot paths, and incremental snapshots.
- A measure of reasoning overhead against plain browsing.

**Gate:** measured numbers and documented limitations.

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

---

## 9. Status

| Phase | State |
|---|---|
| 0 Toolchain | **done**: `toolchain/`; `check.sh` GREEN; report with 10 findings (the 9th: builds name their target, so the code runs under valgrind and on other machines) |
| 1 Contract | **done**: `docs/contract.md`, `contracts/dom.pl`, the validator; contract gate GREEN (32 refusals) |
| 2 Macro integration | **done**: `include/amb.pl` and `prolog/amb/gen_c.pl` make the DOM's C from its contract; gen gate (goldens) and exec gate GREEN, valgrind clean |
| 3 DOM vertical slice | **done, the first milestone**: each §6.3 criterion has a passing test (the table in Phase 3); `bin/ambitious loop` runs the whole loop; `make test` GREEN |
| 4 HTML parsing | **done, first slice**: the WHATWG tokenizer (every state, the 2,231 named references) and tree builder (the insertion modes, adoption agency, foster parenting, fragments) over `contracts/html.pl`. html5lib tree construction: **1,374 of 1,721 cases run pass (79.8%)**; every failure is foreign content (218), template contents (109) or the 2025 select rules (20). Parse errors are facts |
| 5 CSS | **done, first slice**: `contracts/css.pl` and `src/css/css.c`. It covers sheets (comments, strings, `@media` against an 800x600 viewport), Selectors 4 (attribute operators, the structural pseudo-classes, `:nth-*()`, `:not`, `:is`, `:where`) with their specificity, and the cascade (origin, importance, the style attribute, specificity, order). It also covers inheritance, the global keywords, presentational hints, and 26 computed properties with the shorthands that set them. Each computed value is a fact with its winning declaration. `css_explain/3` says why, `css_lost/4` names the cascade step at which each loser lost, and contrast and unused rules are diagnostics. `tests/exec/css_cascade` (120 cases, valgrind clean) and `tests/integ/style` |
| 6 Layout and rendering | **done, first slice**: `contracts/layout.pl`, `src/layout/layout.c`, `src/paint/`. Block layout covers CSS 2.1 widths, auto margins, max-width, box-sizing, and margins collapsed between siblings, through parents and through empty boxes. Inline layout covers the white-space modes, line breaking, baselines, left/right/centre/justify and inline backgrounds. Also: inline-blocks shrunk to fit, replaced elements, tables in automatic layout (colspan, cells centred), and list markers. Text is set in six DejaVu faces from outlines (`tools/gen-font.py`), rasterized by exact area coverage, and drawn into a PNG by the painter's own deflate. `ambitious render`. Every box has a width explanation that reaches the CSS rule behind a declared width; facts answer hit-testing and overflow. `tests/exec/layout_boxes` (valgrind clean) and `tests/integ/render`, which pins three pages' pixel checksums; `tests/golden/render/` holds their PNGs |
| 7 Networking | planned |
| 8 JavaScript | planned |
| 9 Accessibility and devtools | planned |
| 10 Persistence | planned |
| 11 Hardening | planned |

## 10. Risks

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
