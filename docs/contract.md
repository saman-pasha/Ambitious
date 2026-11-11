# The module contract

A **contract** is the one source of truth for a browser module. It states the
module's types, ownership, operations, invariants, events and the facts it
exposes to reasoning. It is read in two places:

- **by cicilang, at C read time.** `include/amb.pl`'s macros turn it into C
  declarations, a store type, checked accessors, precondition checks,
  invariant checks, a fact exporter and a request dispatcher;
- **by `tools/amb`, in cocolog.** The tool validates it and writes the
  reasoning schema (`build/schema/M.pl`) and its reference
  (`build/doc/M.md`).

Both read the same file, so the C interface and the reasoning schema can't
drift apart. A fingerprint of the contract's text, compiled into the C side
and stamped on every snapshot, makes any drift detectable at run time.

## The file

`contracts/M.pl` holds ground facts of **one predicate, `M_contract/1`**, one
declaration each, in any order. The validator keeps the order for the
fingerprint. One predicate per module is deliberate: cocolog has one
namespace, and two files that defined clauses of a shared predicate would
replace each other's clauses when loaded side by side.

```prolog
dom_contract(module(dom, '0.1.0', 'Document Object Model ...')).
dom_contract(status([ok, stale_handle, wrong_kind, ...])).
dom_contract(enum(node_kind, [document, doctype, element, text, comment])).
...
```

`sh tools/amb validate M` checks a contract. Every rule below is a check the
validator makes; `tests/contract/run.pl` breaks each one on purpose and
expects its refusal.

## Types

| declaration | meaning |
|---|---|
| `module(M, Version, Summary)` | exactly once; `M` is the file's name; `Version` an atom |
| `status(Codes)` | exactly once; the outcomes of operations, distinct atoms, `ok` first. The generator appends `bad_request`, `not_requestable` and `stale_revision` for the request path |
| `enum(E, Values)` | distinct atoms; `none` is reserved for the empty ref |
| `record(R, Fields)` | `Fields` is a list of `field(Name, Type)` with distinct names; `gen`, `live`, `id` and `free_next` are the table's own |
| `handle(R, Functor)` | a public handle for R: `{doc, index, gen}` in C, `Functor(Doc, Index, Gen)` in Prolog; one per record, functors distinct |

Field types:

| type | C | what it is |
|---|---|---|
| `u8 u16 u32 u64 i32 i64` | `uint8_t` … `int64_t` | integers |
| `f64` | `double` | |
| `bool` | `uint8_t` | `true`/`false` in facts |
| `enum(E)` | `M_E` | a declared enum |
| `ref(R)` | `uint32_t` | a record of R in the same store, by slot; 0 is `none` |
| `atom(A)` | `uint32_t` | an interned name in the store's `atoms(A)`; 0 is none |
| `span(P)` | `amb_span` | `{off, len}` in the store's byte `pool(P)` |

## Ownership

| declaration | meaning |
|---|---|
| `store(S, Parts)` | exactly one: the module's state, a C type with one drop. Each part is `table(R)` (one per record, every record has one), `pool(P)`, `atoms(A)` or `log(Event)` |
| `owns(Owner, Owned, How)` | `How` is `store` (only the store owns so; it must own every record) or `list(L)` (a record owns the items of its list L). Ownership through lists has no cycle, except a record over records of its own kind (a tree) |
| `tie(handle(R), S)` | a handle of R means something only in its own store |
| `list(L, Owner, Item, Links)` | an intrusive list: `first(F)` and `up(F)` are required, `last(F)` and `prev(F)` optional, `next(F)` required. Each link names a `ref` field of the right record. Traversal, link and unlink, and the structural invariants are generated from it |
| `root(R, Field, Value)` | R's first record is made with the store, with `Field = Value` |

## Operations

```prolog
operation(Name, [param(P, Type) ...], Result, [pre(Cond, Status) ...], Effects, Options)
```

- **Parameter types:** `text` (a byte string: `const char *P, size_t P_len`
  in C), `handle(R)`, `enum(E)`, a scalar.
- **Result:** `status`, `void`, `handle(R)` (the none handle on failure), or
  a scalar.
- **The store** is every operation's implicit first parameter.
- **Options:**
  - `mutates`: the operation bumps the revision and logs an event;
  - `readonly`;
  - `requestable`: the reasoner may propose it. Such an operation answers a
    status, and every parameter can be written in a request.

Each precondition names the status its failure answers:

| condition | holds when |
|---|---|
| `live(P)` | the handle P is live: its store, slot and generation |
| `kind_in(P, Values)` | P's `kind` field is one of the values |
| `unset(P, F)` / `set(P, F)` | P's ref field F is none / is not |
| `child_of(C, P, L)` | C is an item of P's list L |
| `not_inclusive_ancestor(A, B, L)` | A is neither B nor, over the tree list L, an ancestor of B |
| `valid_name(T)` | the text parameter T is a valid name (non-empty, no white space, no `<>"'/=`) |

The generator emits `M_check_Op(store, params…)`, which tests the
conditions: every handle's `live` first, the rest in order. The hand-written
operation calls it first, and the request dispatcher calls the same check.
`Effects` (`creates(R)`, `links(L, P, C)`, `unlinks(L, C)`, `destroys(L, N)`,
`sets(…)`, `unsets(…)`) document the operation for the reasoner's
consequence rules and for the reference.

## Invariants

`invariant(Name, Text, Check)` names a check that holds after every
operation, whether it succeeded or was refused. The checks are generated:

| check | tests |
|---|---|
| `root(R, F, V)` | slot 1 of R is live, has `F = V`, and is no list's item |
| `list_consistent(L)` | walking each owner's list visits only items whose `up` is the owner, `prev` mirrors `next`, `last` is the last; every item whose `up` is set is visited exactly once. So an item has at most one owner |
| `acyclic(L)` | over a tree list, no record is its own ancestor |
| `owner_kinds(L, F, Values)` | every owner with items has `F` among the values |
| `refs_live` | every ref of every live record is none or names a live record |

## Events

`event(E, Fields)` is logged by `log(E)` in the store. The field type
`enum(op)` is the generated enum of the module's operations.

## Reasoning

```prolog
fact(Head, table(R), Where)
```

This declares one fact predicate of the snapshot. `Head`'s functor begins
with `M_`, and its arguments say where each value comes from:

| argument | value in the fact |
|---|---|
| `handle` | the record's own handle term, `n(Doc, Index, Gen)` |
| `field(F)` | the field, written by its type: an enum's value, an atom's or a span's text as a quoted atom, a ref's handle term or `none`, a number |
| `doc`, `rev` | the document id, the revision |

`Where` is `true`, `field(F) == V`, `field(F) \== V`, or a conjunction.
`V` is a value of F's enum, `none` for a ref, or an integer.

| declaration | meaning |
|---|---|
| `rules(File)` | the module's rules, `prolog/amb/File.pl` |
| `derived(Name/Arity, Text)` | a predicate the rules define; prefixed, and no fact's name |
| `query(Name, Goal, Vars, Text)` | a named query, which calls only the module's facts and derived predicates (or another module's) |

## Generation and validation

`generate(Outputs)` lists what the generator makes. Its known outputs are
`c_types c_store c_accessors c_checks c_exporter c_invariants c_dispatch
schema doc`.

`test(Kind, Path)` points at a suite: `contract gen exec safe reason integ`.

## Snapshots and requests

A snapshot is text, a fact per line:

```prolog
amb_snapshot(dom, 1, 7, 'fnv1a32:7bbcd05e:6270').
dom_node(n(1,1,0), document).
dom_element(n(1,2,0), html).
dom_attr(n(1,5,0), 'aria-label', 'Close').
```

Every string from a page is a quoted atom, escaped. The reasoner reads a
snapshot term by term with `read_term`, never `consult`. It accepts only the
facts its schema declares, with arguments of their declared shapes, under a
fingerprint equal to its schema's. Anything else refuses the snapshot.

A request is a line the reasoner prints:

```prolog
amb_request(1, 7, set_attribute(n(1,9,0), alt, '')).
```

The engine reads it with its own term reader and runs it only when:

- the operation is `requestable`;
- the document is still at revision 7 when its batch begins (a batch is
  derived from one snapshot);
- every handle is live with its generation;
- the generated precondition check passes.

It answers with the status, and with the new revision on success.
