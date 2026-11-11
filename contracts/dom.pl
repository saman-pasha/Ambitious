%% contracts/dom.pl -- the DOM module's contract (docs/contract.md is the format).
%%
%% One predicate, dom_contract/1, one fact a declaration, in the sections of the
%% architecture's §3: TYPES, OWNERSHIP, OPERATIONS, INVARIANTS, EVENTS,
%% REASONING, GENERATION, VALIDATION. The generator reads it at C read time
%% (include/amb.pl) and the tool reads it for the reasoning schema (tools/amb):
%% this file is the one source of truth for both.

dom_contract(module(dom, '0.1.0', 'Document Object Model: documents, elements, text, comments, attributes')).

%% ---- TYPES ------------------------------------------------------------------

%% what an operation answers: ok, or why it refused; ok is first
dom_contract(status([ok, stale_handle, wrong_kind, has_parent, not_a_child, would_cycle,
                     invalid_name, not_found, out_of_memory])).

dom_contract(enum(node_kind, [document, doctype, element, text, comment])).

%% a node: one record of the node table. Links are refs within the store,
%% never pointers; the record's slot, liveness and generation are the table's.
dom_contract(record(node, [
    field(kind,         enum(node_kind)),
    field(name,         atom(names)),      % an element's local name, a doctype's name
    field(data,         span(text)),       % a text or comment node's data
    field(public_id,    span(text)),       % a doctype's public identifier ...
    field(system_id,    span(text)),       % ... and its system identifier
    field(doctype_ids,  u8),               % which of the two the doctype has: 1 public, 2 system
    field(parent,       ref(node)),
    field(first_child,  ref(node)),
    field(last_child,   ref(node)),
    field(prev_sibling, ref(node)),
    field(next_sibling, ref(node)),
    field(first_attr,   ref(attr))
])).

dom_contract(record(attr, [
    field(owner, ref(node)),
    field(name,  atom(names)),
    field(value, span(text)),
    field(next,  ref(attr))
])).

%% public handles: {doc, index, generation}; in Prolog n(Doc, Index, Gen)
dom_contract(handle(node, n)).
dom_contract(handle(attr, a)).

%% ---- OWNERSHIP --------------------------------------------------------------

%% the store: one per document. It owns every record, its text and its names.
dom_contract(store(document, [table(node), table(attr), pool(text), atoms(names), log(mutation)])).

dom_contract(owns(document, node, store)).
dom_contract(owns(document, attr, store)).
dom_contract(owns(node, node, list(children))).       % destroying a node destroys its children
dom_contract(owns(node, attr, list(attributes))).     % ... and its attributes
dom_contract(tie(handle(node), document)).            % a handle means something in its own document only
dom_contract(tie(handle(attr), document)).

%% the intrusive lists the links make, from which traversal and the
%% structural invariants are generated
dom_contract(list(children,   node, node, [first(first_child), last(last_child), next(next_sibling),
                                           prev(prev_sibling), up(parent)])).
dom_contract(list(attributes, node, attr, [first(first_attr), next(next), up(owner)])).

%% the document node: the store's root, made with the store
dom_contract(root(node, kind, document)).

%% ---- OPERATIONS -------------------------------------------------------------
%% operation(Name, Params, Result, Preconditions, Effects, Options). The store
%% is every operation's implicit first parameter. A precondition names the
%% status its failure answers. `requestable' lets the reasoner propose it.

dom_contract(operation(create_element, [param(name, text)], handle(node),
    [pre(valid_name(name), invalid_name)],
    [creates(node)], [mutates])).
dom_contract(operation(create_text, [param(data, text)], handle(node),
    [], [creates(node)], [mutates])).
dom_contract(operation(create_comment, [param(data, text)], handle(node),
    [], [creates(node)], [mutates])).
dom_contract(operation(create_doctype, [param(name, text), param(public_id, text), param(system_id, text),
                                        param(ids, u8)], handle(node),
    [],
    [creates(node)], [mutates])).
dom_contract(operation(append_child, [param(parent, handle(node)), param(child, handle(node))], status,
    [pre(live(parent), stale_handle), pre(live(child), stale_handle),
     pre(kind_in(parent, [document, element]), wrong_kind),
     pre(kind_in(child, [doctype, element, text, comment]), wrong_kind),
     pre(unset(child, parent), has_parent),
     pre(not_inclusive_ancestor(child, parent, children), would_cycle)],
    [links(children, parent, child)], [mutates, requestable])).
dom_contract(operation(insert_before, [param(parent, handle(node)), param(child, handle(node)),
                                       param(before, handle(node))], status,
    [pre(live(parent), stale_handle), pre(live(child), stale_handle), pre(live(before), stale_handle),
     pre(kind_in(parent, [document, element]), wrong_kind),
     pre(kind_in(child, [doctype, element, text, comment]), wrong_kind),
     pre(unset(child, parent), has_parent),
     pre(child_of(before, parent, children), not_a_child),
     pre(not_inclusive_ancestor(child, parent, children), would_cycle)],
    [links(children, parent, child)], [mutates, requestable])).
dom_contract(operation(remove_child, [param(parent, handle(node)), param(child, handle(node))], status,
    [pre(live(parent), stale_handle), pre(live(child), stale_handle),
     pre(child_of(child, parent, children), not_a_child)],
    [unlinks(children, child)], [mutates, requestable])).
dom_contract(operation(destroy, [param(node, handle(node))], status,
    [pre(live(node), stale_handle), pre(unset(node, parent), has_parent),
     pre(kind_in(node, [doctype, element, text, comment]), wrong_kind)],
    [destroys(children, node)], [mutates])).
dom_contract(operation(set_attribute, [param(element, handle(node)), param(name, text), param(value, text)], status,
    [pre(live(element), stale_handle), pre(kind_in(element, [element]), wrong_kind),
     pre(valid_name(name), invalid_name)],
    [sets(attributes, element, name)], [mutates, requestable])).
dom_contract(operation(remove_attribute, [param(element, handle(node)), param(name, text)], status,
    [pre(live(element), stale_handle), pre(kind_in(element, [element]), wrong_kind)],
    [unsets(attributes, element, name)], [mutates, requestable])).
dom_contract(operation(set_data, [param(node, handle(node)), param(data, text)], status,
    [pre(live(node), stale_handle), pre(kind_in(node, [text, comment]), wrong_kind)],
    [sets(node, data)], [mutates, requestable])).
%% the HTML serialization of a subtree (the document node: the whole document)
dom_contract(operation(serialize, [param(node, handle(node)), param(out, stream)], status,
    [pre(live(node), stale_handle)],
    [], [readonly])).

%% ---- INVARIANTS -------------------------------------------------------------
%% invariant(Name, Text, Check): Check is one of the generated checks
%% (docs/contract.md); each holds after every operation, ok or refused.

dom_contract(invariant(root_document, 'The document node is the first record, live, with no parent',
    root(node, kind, document))).
dom_contract(invariant(tree_links, 'Parent, child and sibling links agree, so a node has at most one parent',
    list_consistent(children))).
dom_contract(invariant(acyclic, 'No node is its own ancestor: the tree has no ownership cycle',
    acyclic(children))).
dom_contract(invariant(only_containers, 'Only document and element nodes have children',
    owner_kinds(children, kind, [document, element]))).
dom_contract(invariant(attr_links, 'Each attribute is listed once, by its live owner',
    list_consistent(attributes))).
dom_contract(invariant(attr_owners, 'Only elements have attributes',
    owner_kinds(attributes, kind, [element]))).
dom_contract(invariant(live_links, 'Every link of a live record names a live record',
    refs_live)).

%% ---- EVENTS -----------------------------------------------------------------
%% each successful mutating operation logs one, with the revision it made

dom_contract(event(mutation, [field(op, enum(op)), field(target, ref(node)), field(other, u32),
                              field(revision, u64)])).

%% ---- REASONING --------------------------------------------------------------
%% fact(Head, Source, Where): Head's arguments say where each value comes from
%% (handle: the record's own handle; field(F): its field, written by type).

dom_contract(fact(dom_node(handle, field(kind)), table(node), true)).
dom_contract(fact(dom_element(handle, field(name)), table(node), field(kind) == element)).
dom_contract(fact(dom_text(handle, field(data)), table(node), field(kind) == text)).
dom_contract(fact(dom_comment(handle, field(data)), table(node), field(kind) == comment)).
dom_contract(fact(dom_doctype(handle, field(name)), table(node), field(kind) == doctype)).
dom_contract(fact(dom_parent(handle, field(parent)), table(node), field(parent) \== none)).
dom_contract(fact(dom_next_sibling(handle, field(next_sibling)), table(node), field(next_sibling) \== none)).
dom_contract(fact(dom_first_child(handle, field(first_child)), table(node), field(first_child) \== none)).
dom_contract(fact(dom_attr(field(owner), field(name), field(value)), table(attr), true)).

%% what the module's rules derive from those facts (prolog/amb/rules/dom.pl)
dom_contract(rules('rules/dom')).
dom_contract(derived(dom_root/1, 'the document node')).
dom_contract(derived(dom_child/2, 'P is the parent of C')).
dom_contract(derived(dom_children/2, 'the children of P, in order')).
dom_contract(derived(dom_ancestor/2, 'A is an ancestor of D')).
dom_contract(derived(dom_descendant/2, 'D is a descendant of A')).
dom_contract(derived(dom_inside/2, 'N is inside the subtree of A (A itself included)')).
dom_contract(derived(dom_path/2, 'the parent chain from the root to N')).
dom_contract(derived(dom_text_content/2, 'the concatenated text of a subtree')).
dom_contract(derived(dom_accessible_name/2, 'a control''s accessible name, from aria-label, alt, title or content')).
dom_contract(derived(dom_missing_attribute/2, 'an element lacks an attribute it requires')).
dom_contract(derived(dom_removal_effect/2, 'what removing N takes with it')).

dom_contract(query(buttons, (dom_element(N, button), dom_accessible_name(N, Name)), [N, Name],
    'every button and its accessible name')).
dom_contract(query(missing, dom_missing_attribute(N, Attr), [N, Attr],
    'elements that lack a required attribute')).

%% ---- GENERATION -------------------------------------------------------------

dom_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, c_dispatch,
                       schema, doc])).

%% ---- VALIDATION -------------------------------------------------------------

dom_contract(test(contract, 'tests/contract')).
dom_contract(test(exec, 'tests/exec')).
dom_contract(test(safe, 'tests/safe')).
dom_contract(test(reason, 'tests/reason')).
dom_contract(test(integ, 'tests/integ')).
