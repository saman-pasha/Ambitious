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

The first milestone is complete: the DOM vertical slice.

- `contracts/dom.pl` declares the DOM: types, ownership, operations and their
  preconditions, invariants, events, and the facts it exposes to reasoning.
- At C read time, `include/amb.pl`'s macros turn it into C:
  - the types and the store type, which is autodropped;
  - handles checked by generation;
  - the precondition checks and the intrusive lists;
  - the invariant checks;
  - the fact exporter and the request dispatcher.

  The hand-written operations in `src/dom/dom.c` stand on that interface.
- The engine exports a snapshot of the live document. The reasoner
  (`prolog/amb/reason.pl`, cocolog, a process of its own) validates the
  snapshot against the schema generated from the same contract, then answers
  queries, explains its answers with proofs, and proposes operations.
- The engine runs a proposed operation only through its own dispatcher, held
  to the contract.

```console
$ bin/ambitious loop
1. the document, at revision 36, 17 nodes: ...
2. the snapshot, checked by the reasoner against the schema:
cross-check: descendants of 17 nodes, the reasoner's and the C traversal's: agree
3. the reasoner's advice, from the snapshot alone:
   amb_request(1, 36, set_attribute(n(1,7,0),alt,'The Ambitious logo')).
4. the requests, validated and applied by the engine as one batch: 3 applied, 0 refused
5. what changed, the reasoner's diff of the two snapshots: ...
6. the first batch again, against the document that moved on: 0 applied, 3 refused
GREEN: the loop, 0 failure(s)
```

## Build and test

```sh
sh toolchain/bootstrap.sh       # the toolchain, once (see toolchain/README.md)
make                            # the modules and build/ambitious
make test                       # the gates: contract gen exec safe reason integ
bin/ambitious loop              # the DOM vertical slice end to end
tools/amb doc dom               # the DOM contract's reference
```

Set `AMB_DEPS` to the directory that holds the four checkouts (cicili,
ZiguratIP, cocolog, cicilang). The default is `~/Projects/GitHub`.

## Layout

| path | what |
|---|---|
| `contracts/` | module contracts, one `M_contract/1` predicate each |
| `include/amb.pl`, `amb.pl.h` | the macros C units include, and the C they call |
| `prolog/amb/` | the contract validator, the generators, the reasoner, each module's rules |
| `src/` | the engine in cicilang C: `rt/` the runtime, `dom/`, `shell/` |
| `tests/` | `contract/ gen/ golden/ exec/ safe/ reason/ integ/`, run by `make test` |
| `tools/amb` | the contract tool: validate, schema, doc, ctext, fingerprint |
