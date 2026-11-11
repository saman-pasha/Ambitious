# Ambitious

A Reasoning Web Browser: a browser whose modules are written in
[cicilang](https://github.com/saman-pasha/cicilang)'s Safe C with ccllib, whose
module contracts are [cocolog](https://github.com/saman-pasha/cocolog) terms
turned into C by cocolog macros, and whose live state is reasoned over by
cocolog.

- [`reasoning_native_browser_plan.md`](reasoning_native_browser_plan.md) is the
  architecture.
- [`PLAN.md`](PLAN.md) is the implementation plan and its status.
- [`docs/contract.md`](docs/contract.md) is the module contract.
- [`toolchain/`](toolchain/) is what it is built with: the pins, the bootstrap
  and the Phase 0 report.

## What runs

Three modules, each declared by a contract and generated into C from it:

- **DOM** (`contracts/dom.pl`, `src/dom/`): the first milestone, the vertical
  slice. The contract declares types, ownership, operations and their
  preconditions, invariants, events, and the facts it exposes to reasoning.
  At C read time, `include/amb.pl`'s macros turn it into C:
  - the types and the store type, which is autodropped;
  - handles checked by generation;
  - the precondition checks and the intrusive lists;
  - the invariant checks;
  - the fact exporter and the request dispatcher.

  The hand-written operations stand on that interface.
- **HTML** (`contracts/html.pl`, `src/html/`): the WHATWG tokenizer and tree
  builder. Bytes become a DOM, and parse errors become facts. It passes 1,374
  of the 1,721 html5lib tree-construction cases it runs.
- **CSS** (`contracts/css.pl`, `src/css/`): the user agent's sheet, `<style>`,
  style attributes and presentational hints, Selectors 4 and the cascade.
  Each element's computed values are facts that keep their winning
  declaration.
- **Layout** (`contracts/layout.pl`, `src/layout/`): block, inline, table and
  list layout. Every box keeps how its width was found. **Paint**
  (`src/paint/`) draws the boxes and text, set in DejaVu outlines, into a PNG.

The engine exports a snapshot of a page: the document, its parse and its
style. The reasoner (`prolog/amb/reason.pl`, cocolog, a process of its own)
validates the snapshot against the schemas generated from the same contracts.
It then answers queries, explains its answers with proofs, and proposes
operations. The engine runs a proposed operation only through its own
dispatcher, held to the contract.

```console
$ bin/ambitious loop
...
6. the first batch again, against the document that moved on: 0 applied, 3 refused
GREEN: the loop, 0 failure(s)
$ bin/ambitious query tests/pages/style.html "dom_element(N, p), css_explain(N, color, X)"
N = n(1,18,0), X = declared('rgb(128, 0, 128)',written(purple),by('#intro'),in(important(author)))
N = n(1,21,0), X = declared('rgb(0, 128, 128)',written(teal),by('p.lead'),in(author))
...
$ bin/ambitious query tests/pages/style.html "dom_attr(N, id, intro), css_lost(N, color, D, Why)"
N = n(1,18,0), D = de(1,226,0), Why = origin_or_importance(lost('style attribute'),won('important author'))
...
$ bin/ambitious render tests/pages/layout.html layout.png
rendered 800x601: 32 boxes, 27 lines, 66 text fragments; pixels fnv1a64:c21c5ac365972c5e
$ bin/ambitious query tests/pages/layout.html "dom_attr(N, class, wrap), layout_box_of(N, B), layout_explain_width(B, W)"
N = n(1,26,0), B = bx(1,5,0), W = declared(600,declared('600px',written('600px'),by('.wrap'),in(author)))
$ bin/ambitious advise tests/pages/style.html
amb_diag(low_contrast, n(1,42,0), '<p> text rgb(200, 200, 200) on rgb(253, 253, 248): contrast 1.64:1, below 4.5:1').
amb_diag(unused_rule, ru(1,149,0), 'the rule .ghost matches no element (at rest: no hover, focus or other state)').
```

## Build and test

```sh
sh toolchain/bootstrap.sh       # the toolchain, once (see toolchain/README.md)
make                            # the modules and build/ambitious
make test                       # the gates: contract gen exec safe reason html integ
bin/ambitious loop              # the DOM vertical slice end to end
bin/ambitious parse PAGE        # an HTML file parsed: its tree and parse errors
bin/ambitious style PAGE        # its elements' computed styles
bin/ambitious render PAGE PNG   # laid out in an 800x600 viewport and drawn
bin/ambitious query PAGE GOAL   # the reasoner over the page (also: why, advise)
tools/amb doc css               # a contract's reference
```

Set `AMB_DEPS` to the directory that holds the four checkouts (cicili,
ZiguratIP, cocolog, cicilang). The default is `~/Projects/GitHub`.

## Layout

| path | what |
|---|---|
| `contracts/` | module contracts, one `M_contract/1` predicate each |
| `include/amb.pl`, `amb.pl.h` | the macros C units include, and the C they call |
| `prolog/amb/` | the contract validator, the generators, the reasoner, each module's rules |
| `src/` | the engine in cicilang C: `rt/` the runtime, `dom/`, `html/`, `css/`, `layout/`, `paint/`, `shell/` |
| `tests/` | `contract/ gen/ golden/ exec/ safe/ reason/ html/ integ/ pages/`, run by `make test` |
| `tools/amb` | the contract tool: validate, schema, doc, ctext, fingerprint |
| `tools/gen-font.py` | `src/paint/font_data.h`, the DejaVu outlines (with the fonts' notice) |
