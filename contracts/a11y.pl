%% contracts/a11y.pl -- the accessibility module's contract (docs/contract.md is the format).
%%
%% The accessibility tree of a styled document: each element that a reader of
%% the page meets -- by its role (the element's own, from HTML-AAM, or its role
%% attribute) -- with its accessible name and where the name came from
%% (aria-labelledby, aria-label, alt, a label, a caption or legend, its content,
%% title, placeholder), its heading level, whether it takes focus. What is not
%% rendered (display none) or is hidden (hidden, aria-hidden) is not in the tree.
%% Each ID reference an element makes (aria-labelledby, aria-describedby,
%% aria-controls, a label's for) is kept with the element it names, or none.
%% The reasoner says what is missing or broken, naming the node and the rule.

a11y_contract(module(a11y, '0.1.0', 'Accessibility: the accessibility tree of a styled document, its roles and names and where each name came from')).

%% ---- TYPES ------------------------------------------------------------------

a11y_contract(status([ok, stale_handle, wrong_kind])).

a11y_contract(enum(role, [document, generic, presentation, link, button, checkbox, radio, textbox, combobox, listbox, option, slider,
    img, heading, paragraph, list, listitem, table, row, cell, columnheader, rowheader, navigation, main, banner, contentinfo,
    complementary, region, form, search, article, dialog, figure, group, separator, term, definition, blockquote, code,
    progressbar, menu, menuitem, tab, tablist, tabpanel, alert, status, unknown])).
%% where an accessible name came from
a11y_contract(enum(name_from, [nothing, aria_labelledby, aria_label, alt, label, caption, legend, content, title, placeholder, value])).
%% the attributes that name other elements by their ids
a11y_contract(enum(ref_attr, [aria_labelledby, aria_describedby, aria_controls, label_for])).

a11y_contract(record(anode, [
    field(node,      xhandle(dom, node)),
    field(role,      enum(role)),
    field(stated,    bool),                 % its role came from a role attribute
    field(name,      span(text)),
    field(name_from, enum(name_from)),
    field(parent,    ref(anode)),
    field(level,     u32),                  % a heading's level
    field(focusable, bool)
])).
%% an ID reference an element makes: the id it names and the element with that id
a11y_contract(record(aref, [
    field(from,   xhandle(dom, node)),
    field(attr,   enum(ref_attr)),
    field(idref,  span(text)),
    field(target, xhandle(dom, node))       % none when no element has the id
])).
%% an element's role attribute whose value is no role
a11y_contract(record(badrole, [field(node, xhandle(dom, node)), field(value, span(text))])).

a11y_contract(handle(anode, ax)).
a11y_contract(handle(aref, ar)).
a11y_contract(handle(badrole, br)).

%% ---- OWNERSHIP --------------------------------------------------------------

a11y_contract(store(tree, [table(anode), table(aref), table(badrole), pool(text)])).
a11y_contract(owns(tree, anode, store)).
a11y_contract(owns(tree, aref, store)).
a11y_contract(owns(tree, badrole, store)).

%% ---- OPERATIONS -------------------------------------------------------------

a11y_contract(operation(build, [param(doc, store(dom)), param(style, store(css))], status, [], [creates(anode)], [mutates])).

%% ---- INVARIANTS -------------------------------------------------------------

a11y_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

a11y_contract(fact(a11y_node(handle, field(node), field(role), field(name), field(name_from)), table(anode), true)).
a11y_contract(fact(a11y_parent(handle, field(parent)), table(anode), field(parent) \== none)).
a11y_contract(fact(a11y_explicit_role(handle), table(anode), field(stated) == true)).
a11y_contract(fact(a11y_heading(handle, field(level)), table(anode), field(role) == heading)).
a11y_contract(fact(a11y_focusable(handle), table(anode), field(focusable) == true)).
a11y_contract(fact(a11y_ref(field(from), field(attr), field(idref), field(target)), table(aref), true)).
a11y_contract(fact(a11y_bad_role(field(node), field(value)), table(badrole), true)).

a11y_contract(rules('rules/a11y')).
a11y_contract(derived(a11y_of/2, 'the accessibility node of an element')).
a11y_contract(derived(a11y_missing_name/2, 'a node whose role needs a name, without one')).
a11y_contract(derived(a11y_broken_ref/3, 'an ID reference to no element')).
a11y_contract(derived(a11y_heading_jump/3, 'a heading more than one level below the heading before it')).
a11y_contract(derived(a11y_nested_interactive/2, 'an interactive node inside another')).
a11y_contract(derived(a11y_duplicate_id/3, 'two elements with one id, which every reference to it confuses')).
a11y_contract(derived(a11y_explain_name/2, 'why a node has its accessible name')).
a11y_contract(derived(a11y_outline/2, 'the headings in document order, with their levels')).

a11y_contract(query(tree, (a11y_node(A, _, R, Nm, F)), [A, R, Nm, F], 'every node of the accessibility tree, its role and name')).

a11y_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
a11y_contract(test(integ, 'tests/integ/a11y')).
