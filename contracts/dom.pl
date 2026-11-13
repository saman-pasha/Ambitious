%% contracts/dom.pl -- the DOM module's contract (docs/contract.md is the format).
%%
%% One predicate, dom_contract/1, one fact a declaration, in the sections of the
%% architecture's §3: TYPES, OWNERSHIP, OPERATIONS, INVARIANTS, EVENTS,
%% REASONING, GENERATION, VALIDATION. The generator reads it at C read time
%% (include/amb.pl) and the tool reads it for the reasoning schema (tools/amb):
%% this file is the one source of truth for both.

dom_contract(module(dom, '0.1.0', 'Document Object Model: documents, elements in their namespaces, text, comments, attributes, template contents')).

%% ---- TYPES ------------------------------------------------------------------

%% what an operation answers: ok, or why it refused; ok is first
dom_contract(status([ok, stale_handle, wrong_kind, has_parent, not_a_child, would_cycle,
                     invalid_name, not_found, out_of_memory, has_shadow])).

%% a fragment is a template's contents: a tree of its own, no node's child,
%% which only its template holds (DOM 4.7, HTML 4.12.3)
dom_contract(enum(node_kind, [document, doctype, element, text, comment, fragment])).

%% an element's namespace (Infra's HTML, SVG and MathML namespaces)
dom_contract(enum(namespace, [html, svg, mathml])).
%% a shadow root's mode: whether its host's shadowRoot shows it
dom_contract(enum(shadow_mode, [open, closed])).
%% how a shadow root's slots find their nodes (DOM 4.2.2.3): by the slot attribute's name,
%% or as a script assigned them (slotAssignment manual, HTMLSlotElement's assign())
dom_contract(enum(slot_assignment, [named, manual])).
%% an attribute's namespace: null for no namespace, or one of the three the
%% HTML parser gives attributes of foreign elements (HTML 13.2.6.1)
dom_contract(enum(attr_namespace, [null, xlink, xml, xmlns])).
%% a document's mode (DOM 4.5): no-quirks, or the quirks or limited-quirks mode
%% the HTML parser sets from its DOCTYPE (HTML 13.2.6.4.1), which CSS, layout and
%% the selectors read (the Quirks Mode Standard)
dom_contract(enum(document_mode, [no_quirks, quirks, limited_quirks])).

%% a node: one record of the node table. Links are refs within the store,
%% never pointers; the record's slot, liveness and generation are the table's.
%% The fields every walk of the tree reads come first, a doctype's last.
dom_contract(record(node, [
    field(kind,         enum(node_kind)),
    field(name,         atom(names)),      % an element's local name, a doctype's name
    field(ns,           enum(namespace)),  % an element's namespace
    field(parent,       ref(node)),
    field(first_child,  ref(node)),
    field(last_child,   ref(node)),
    field(prev_sibling, ref(node)),
    field(next_sibling, ref(node)),
    field(first_attr,   ref(attr)),
    field(data,         span(text)),       % a text or comment node's data
    field(content,      ref(node)),        % an HTML template's contents, a fragment
    field(host,         ref(node)),        % a fragment's template, or a shadow root's host
    field(public_id,    span(text)),       % a doctype's public identifier ...
    field(system_id,    span(text)),       % ... and its system identifier
    field(doctype_ids,  u8),               % which of the two the doctype has: 1 public, 2 system
    %% an input's dirty checkedness flag (1) and, while it is set, its checkedness (2): HTML
    %% 4.10.5.4 as Chromium keeps it (src/html/forms.c), every change but its checked
    %% attribute's setting the flag; without the flag the attribute's presence is the
    %% checkedness. Beside doctype_ids, where it costs a node no room
    field(checked,      u8),
    field(mode,         enum(document_mode)),  % the document node's mode
    %% THE FLAT TREE (DOM 4.2.2, CSS Scoping 1, 2.2), which the cascade, layout and
    %% accessibility walk (src/dom/dom.h): a host's shadow root; the slot a node is
    %% assigned to, a slot's assigned nodes in order (found again by flatten when the
    %% document has changed since: flat, the document node's, is the revision they were
    %% found at, plus one); the slot a script assigned a node to, a slot's manually
    %% assigned nodes in the order assign() gave them
    field(shadow,         ref(node)),
    field(slot,           ref(node)),
    field(assigned_first, ref(node)),
    field(assigned_next,  ref(node)),
    field(manual,         ref(node)),
    field(manual_first,   ref(node)),
    field(manual_next,    ref(node)),
    field(flat,           u64)
])).

%% an attribute's name is its qualified name: an attribute in a namespace keeps
%% its prefix there ('xlink:href'), as the HTML serialization writes it
dom_contract(record(attr, [
    field(owner, ref(node)),
    field(name,  atom(names)),
    field(ns,    enum(attr_namespace)),
    field(value, span(text)),
    field(next,  ref(attr))
])).

%% what a change was, beyond the log's entry, kept once a reader asks for it
%% (keep_details; src/script/observe.c's MutationObserver records say it): a child's
%% insertion or removal, its siblings there (after an insertion, before a removal); an
%% attribute's change, its name's atom, its namespace and the value it had (had: 0 for
%% an attribute added); a node's data change, the data it had
dom_contract(record(detail, [
    field(prev, u32),
    field(next, u32),
    field(name, u32),
    field(ns,   u32),
    field(old,  span(text)),
    field(had,  bool)
])).

%% a shadow root (DOM 4.8): a fragment, the root of a tree of its own, attached to its host
%% element (the fragment's host link names it too, and the host's shadow link the root):
%% its mode, how its slots are assigned, whether focusing its host delegates the focus to
%% it, whether a clone of its host clones it, whether its host's getHTML() may write it,
%% and whether the HTML parser made it from a template (shadowrootmode)
dom_contract(record(shadow, [
    field(host, ref(node)),
    field(root, ref(node)),
    field(mode, enum(shadow_mode)),
    field(assignment,      enum(slot_assignment)),
    field(delegates_focus, bool),
    field(clonable,        bool),
    field(serializable,    bool),
    field(declarative,     bool)
])).

%% public handles: {doc, index, generation}; in Prolog n(Doc, Index, Gen)
dom_contract(handle(node, n)).
dom_contract(handle(attr, a)).
dom_contract(handle(detail, md)).
dom_contract(handle(shadow, ms)).

%% ---- OWNERSHIP --------------------------------------------------------------

%% the store: one per document. It owns every record, its text and its names.
dom_contract(store(document, [table(node), table(attr), pool(text), atoms(names), log(mutation), table(detail), table(shadow)])).
dom_contract(owns(document, shadow, store)).
dom_contract(owns(document, detail, store)).

dom_contract(owns(document, node, store)).
dom_contract(owns(document, attr, store)).
dom_contract(owns(node, node, list(children))).       % destroying a node destroys its children
dom_contract(owns(node, attr, list(attributes))).     % ... and its attributes
%% (and a template its contents: dom_destroy follows the content link too)
dom_contract(tie(handle(node), document)).            % a handle means something in its own document only
dom_contract(tie(handle(attr), document)).

%% the intrusive lists the links make, from which traversal and the
%% structural invariants are generated
dom_contract(list(children,   node, node, [first(first_child), last(last_child), next(next_sibling),
                                           prev(prev_sibling), up(parent)])).
dom_contract(list(attributes, node, attr, [first(first_attr), next(next), up(owner)])).
%% a slot's assigned nodes (the flat tree's, which flatten finds) and the nodes a script
%% assigned it (assign_slot): lists of nodes no slot owns
dom_contract(list(assigned, node, node, [first(assigned_first), next(assigned_next), up(slot)])).
dom_contract(list(manual, node, node, [first(manual_first), next(manual_next), up(manual)])).

%% the document node: the store's root, made with the store
dom_contract(root(node, kind, document)).

%% ---- OPERATIONS -------------------------------------------------------------
%% operation(Name, Params, Result, Preconditions, Effects, Options). The store
%% is every operation's implicit first parameter. A precondition names the
%% status its failure answers. `requestable' lets the reasoner propose it.

%% an element in the HTML namespace; a template is made with its contents
dom_contract(operation(create_element, [param(name, text)], handle(node),
    [pre(valid_name(name), invalid_name)],
    [creates(node)], [mutates])).
%% an element in a namespace (the HTML parser's foreign elements)
dom_contract(operation(create_element_ns, [param(ns, enum(namespace)), param(name, text)], handle(node),
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
%% the child may not be the parent's ancestor through a template either (the
%% DOM's host-including inclusive ancestor): dom_append_child checks that link
%% after the generated checks, and refuses with would_cycle
dom_contract(operation(append_child, [param(parent, handle(node)), param(child, handle(node))], status,
    [pre(live(parent), stale_handle), pre(live(child), stale_handle),
     pre(kind_in(parent, [document, element, fragment]), wrong_kind),
     pre(kind_in(child, [doctype, element, text, comment]), wrong_kind),
     pre(unset(child, parent), has_parent),
     pre(not_inclusive_ancestor(child, parent, children), would_cycle)],
    [links(children, parent, child)], [mutates, requestable])).
dom_contract(operation(insert_before, [param(parent, handle(node)), param(child, handle(node)),
                                       param(before, handle(node))], status,
    [pre(live(parent), stale_handle), pre(live(child), stale_handle), pre(live(before), stale_handle),
     pre(kind_in(parent, [document, element, fragment]), wrong_kind),
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
%% an attribute in a namespace, by its qualified name (the HTML parser's
%% adjusted foreign attributes: 'xlink:href' in the XLink namespace)
dom_contract(operation(set_attribute_ns, [param(element, handle(node)), param(ns, enum(attr_namespace)),
                                          param(name, text), param(value, text)], status,
    [pre(live(element), stale_handle), pre(kind_in(element, [element]), wrong_kind),
     pre(valid_name(name), invalid_name)],
    [sets(attributes, element, name)], [mutates])).
dom_contract(operation(remove_attribute, [param(element, handle(node)), param(name, text)], status,
    [pre(live(element), stale_handle), pre(kind_in(element, [element]), wrong_kind)],
    [unsets(attributes, element, name)], [mutates, requestable])).
dom_contract(operation(set_data, [param(node, handle(node)), param(data, text)], status,
    [pre(live(node), stale_handle), pre(kind_in(node, [text, comment]), wrong_kind)],
    [sets(node, data)], [mutates, requestable])).
%% a shadow root attached to an element (the script module, or the HTML parser for a
%% template's shadowrootmode, checks it may have one): the root, a fragment whose host is
%% the element, with its options; destroying the element destroys its tree
dom_contract(operation(attach_shadow, [param(host, handle(node)), param(mode, enum(shadow_mode)),
                                       param(assignment, enum(slot_assignment)), param(delegates_focus, bool),
                                       param(clonable, bool), param(serializable, bool), param(declarative, bool)], handle(node),
    [pre(live(host), stale_handle), pre(kind_in(host, [element]), wrong_kind), pre(unset(host, shadow), has_shadow)],
    [creates(node)], [mutates])).
%% the flat tree's assignment found again (DOM 4.2.2.3: each slot's assigned nodes, by
%% name or as a script assigned them), when the document has changed since it was last
%% found: what a walk of the flat tree reads (src/dom/dom.h). It changes no node of the
%% tree, so it logs nothing
dom_contract(operation(flatten, [], status, [], [], [])).
%% a node appended to a slot's manually assigned nodes, taken from the slot it was
%% assigned to before (HTMLSlotElement's assign(), DOM 4.2.2.6), and a slot's manually
%% assigned nodes emptied: a shadow root of manual slot assignment's slots take them
dom_contract(operation(assign_slot, [param(slot, handle(node)), param(node, handle(node))], status,
    [pre(live(slot), stale_handle), pre(live(node), stale_handle), pre(kind_in(slot, [element]), wrong_kind),
     pre(kind_in(node, [element, text]), wrong_kind)],
    [], [mutates])).
dom_contract(operation(clear_slot, [param(slot, handle(node))], status,
    [pre(live(slot), stale_handle), pre(kind_in(slot, [element]), wrong_kind)],
    [], [mutates])).
%% the details of each change kept from now on (its log entry's detail), for a reader
%% that needs them; the changes before are not
dom_contract(operation(keep_details, [], status, [], [], [])).
%% the document's mode, as the HTML parser decides it from the DOCTYPE (a document is
%% made in no-quirks mode)
dom_contract(operation(set_mode, [param(mode, enum(document_mode))], status,
    [], [sets(node, mode)], [mutates])).
%% an input's checked field (its checkedness and dirty checkedness flag), as HTML's form
%% controls set it (src/html/forms.c): a change logged, so that what reads the state (the
%% cascade's :checked, a script's view) sees it; none when the field is already that
dom_contract(operation(set_checked, [param(element, handle(node)), param(state, u8)], status,
    [pre(live(element), stale_handle), pre(kind_in(element, [element]), wrong_kind)],
    [sets(node, checked)], [mutates])).
%% the HTML serialization of a subtree (the document node: the whole document;
%% a template: its contents)
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
dom_contract(invariant(only_containers, 'Only document, element and fragment nodes have children',
    owner_kinds(children, kind, [document, element, fragment]))).
dom_contract(invariant(attr_links, 'Each attribute is listed once, by its live owner',
    list_consistent(attributes))).
dom_contract(invariant(attr_owners, 'Only elements have attributes',
    owner_kinds(attributes, kind, [element]))).
dom_contract(invariant(live_links, 'Every link of a live record names a live record',
    refs_live)).
dom_contract(invariant(assigned_links, 'A node is assigned to one slot at most, and listed once by it',
    list_consistent(assigned))).
dom_contract(invariant(manual_links, 'A node a script assigned is listed once, by its slot',
    list_consistent(manual))).

%% ---- EVENTS -----------------------------------------------------------------
%% each successful mutating operation logs one, with the revision it made, and its
%% details' record once they are kept (keep_details; none before)

dom_contract(event(mutation, [field(op, enum(op)), field(target, ref(node)), field(other, u32),
                              field(detail, ref(detail)), field(revision, u64)])).

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
%% an element's namespace, when it is not HTML's; a template's contents
dom_contract(fact(dom_namespace(handle, field(ns)), table(node), (field(kind) == element, field(ns) \== html))).
dom_contract(fact(dom_template_content(handle, field(content)), table(node), field(content) \== none)).
%% a shadow root: its host, the root (a fragment, its tree's own), the mode; its slots'
%% assignment, and whether it delegates focus, is clonable, serializable and declarative
dom_contract(fact(dom_shadow_root(field(host), field(root), field(mode)), table(shadow), true)).
dom_contract(fact(dom_shadow_options(field(root), field(assignment), field(delegates_focus), field(clonable), field(serializable),
                                     field(declarative)), table(shadow), true)).
%% the flat tree: the slot a node is assigned to, and the slot a script assigned it to
%% (manual assignment, which holds while the node is its host's child)
dom_contract(fact(dom_assigned_slot(handle, field(slot)), table(node), field(slot) \== none)).
dom_contract(fact(dom_manual_slot(handle, field(manual)), table(node), field(manual) \== none)).
%% a slot's assigned nodes in their order: the first, and each one's next
dom_contract(fact(dom_slot_first(handle, field(assigned_first)), table(node), field(assigned_first) \== none)).
dom_contract(fact(dom_slot_next(handle, field(assigned_next)), table(node), field(assigned_next) \== none)).
%% an attribute's namespace, when it has one (its name in dom_attr/3 has the prefix)
dom_contract(fact(dom_attr_namespace(field(owner), field(name), field(ns)), table(attr), field(ns) \== null)).
%% the document's mode, when it is not no-quirks: quirks or limited_quirks
dom_contract(fact(dom_mode(handle, field(mode)), table(node), (field(kind) == document, field(mode) \== no_quirks))).

%% what the module's rules derive from those facts (prolog/amb/rules/dom.pl)
dom_contract(rules('rules/dom')).
dom_contract(derived(dom_root/1, 'the document node')).
dom_contract(derived(dom_document_mode/2, 'the document''s mode: no_quirks, quirks or limited_quirks')).
dom_contract(derived(dom_html_element/2, 'N is an element of the HTML namespace named E')).
dom_contract(derived(dom_in_template/2, 'N is in the contents of the template T')).
dom_contract(derived(dom_child/2, 'P is the parent of C')).
dom_contract(derived(dom_children/2, 'the children of P, in order')).
dom_contract(derived(dom_ancestor/2, 'A is an ancestor of D')).
dom_contract(derived(dom_descendant/2, 'D is a descendant of A')).
dom_contract(derived(dom_inside/2, 'N is inside the subtree of A (A itself included)')).
dom_contract(derived(dom_path/2, 'the parent chain from the root to N')).
dom_contract(derived(dom_text_content/2, 'the concatenated text of a subtree')).
dom_contract(derived(dom_accessible_name/2, 'a control''s accessible name, from aria-label, alt, title or content')).
dom_contract(derived(dom_missing_attribute/2, 'an HTML element lacks an attribute it requires')).
dom_contract(derived(dom_removal_effect/2, 'what removing N takes with it')).
dom_contract(derived(dom_flat_parent/2, 'P is the parent of N in the flat tree: its slot, a shadow root''s host, or its parent')).
dom_contract(derived(dom_flat_child/2, 'C is a child of P in the flat tree')).
dom_contract(derived(dom_flat_children/2, 'the children of P in the flat tree, in order')).
dom_contract(derived(dom_shadow_tree_of/2, 'N is in the shadow tree whose root is R')).

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
