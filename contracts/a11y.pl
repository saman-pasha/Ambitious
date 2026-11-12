%% contracts/a11y.pl -- the accessibility module's contract (docs/contract.md is the format).
%%
%% The accessibility tree of a styled document: each element that a reader of
%% the page meets -- by its role (the element's own, from HTML-AAM, or its role
%% attribute) -- with its accessible name and description and where each came
%% from, its states and properties (WAI-ARIA 1.2 and HTML-AAM: checked, selected,
%% expanded, pressed, disabled, required, ...), the values of a range, its level
%% and its position in its set, and the live region it belongs to. What is not
%% rendered (display none) or is hidden (hidden, aria-hidden) is not in the tree:
%% each hidden subtree is kept by its root, with what inside it still takes focus
%% or would announce its changes. An element that is no node (its role generic or
%% presentation) but carries aria-* attributes is kept with its role, so that the
%% rules can judge the attributes. Each ID reference an element makes
%% (aria-labelledby, aria-describedby, aria-controls, a label's for, ...) is kept
%% with the element it names, or none. The reasoner says what is missing or
%% broken, naming the node and the rule.

a11y_contract(module(a11y, '0.2.0', 'Accessibility: the accessibility tree of a styled document: roles, names and descriptions and where each came from, states and properties, values of ranges, positions and live regions')).

%% ---- TYPES ------------------------------------------------------------------

a11y_contract(status([ok, stale_handle, wrong_kind])).

%% the roles: WAI-ARIA 1.2's, and document, generic and presentation as HTML-AAM uses them
a11y_contract(enum(role, [document, generic, presentation, link, button, checkbox, radio, textbox, combobox, listbox, option, slider,
    img, heading, paragraph, list, listitem, table, row, cell, columnheader, rowheader, navigation, main, banner, contentinfo,
    complementary, region, form, search, article, dialog, figure, group, separator, term, definition, blockquote, code,
    progressbar, menu, menuitem, tab, tablist, tabpanel, alert, status,
    alertdialog, application, caption, deletion, directory, emphasis, feed, grid, gridcell, insertion, log, marquee, math,
    menubar, menuitemcheckbox, menuitemradio, meter, note, radiogroup, rowgroup, scrollbar, searchbox, spinbutton, strong,
    subscript, superscript, switch, time, timer, toolbar, tooltip, tree, treegrid, treeitem, unknown])).
%% where an accessible name came from
a11y_contract(enum(name_from, [nothing, aria_labelledby, aria_label, alt, label, caption, legend, content, title, placeholder, value,
    label_attribute])).
%% where an accessible description came from
a11y_contract(enum(desc_from, [nothing, aria_describedby, aria_description, title, caption])).
%% the attributes that name other elements by their ids
a11y_contract(enum(ref_attr, [aria_labelledby, aria_describedby, aria_controls, label_for, aria_owns, aria_activedescendant,
    aria_details, aria_errormessage, aria_flowto])).
%% the states and properties whose values are tokens
a11y_contract(enum(state, [checked, selected, expanded, pressed, disabled, required, readonly, invalid, current, busy, haspopup,
    orientation, multiselectable, modal, autocomplete, multiline])).
%% their values (aria-autocomplete's none, aria-haspopup's false and the like are no state at all)
a11y_contract(enum(token, [true, false, mixed, page, step, location, date, time, grammar, spelling, menu, listbox, tree, grid,
    dialog, horizontal, vertical, inline, list, both])).
%% where a state came from: the element's aria-* attribute, its own HTML (an attribute,
%% or its native state), an ancestor's (a disabled fieldset, optgroup or select, an
%% aria-disabled container), its role's default, or computed (a select's default option)
a11y_contract(enum(state_from, [aria, html, inherited, implicit, computed])).
%% where a range's value came from: aria-valuenow, the element's own (an input, progress
%% or meter), or the role's default
a11y_contract(enum(value_from, [aria, html, default])).
%% a live region's politeness; not_live for a node that is no live region
a11y_contract(enum(politeness, [not_live, off, polite, assertive])).
%% why a hidden subtree is not in the tree
a11y_contract(enum(hidden_how, [display_none, hidden_attribute, aria_hidden])).
%% what a hidden subtree holds that a reader should know of
a11y_contract(enum(inside_what, [focusable, live_region])).
%% a node's properties whose values are texts
a11y_contract(enum(text_prop, [description, value, placeholder, valuetext])).
%% the flags of a form control's validity state (HTML's constraint validation), of a page
%% no one has edited: its value missing, of the wrong type, off its pattern, under its
%% min, over its max, off its step
a11y_contract(enum(validity, [value_missing, type_mismatch, pattern_mismatch, range_underflow, range_overflow, step_mismatch])).

%% A NODE is kept small, as every element a reader meets is one: what few nodes have -- a
%% description and the other texts, a range's values, a live region's settings -- is a
%% record of its own, made with its node
a11y_contract(record(anode, [
    field(node,      xhandle(dom, node)),
    field(role,      enum(role)),
    field(stated,    bool),                 % its role came from a role attribute
    field(name,      span(text)),
    field(name_from, enum(name_from)),
    field(parent,    ref(anode)),
    field(level,     u32),                  % a heading's level, a list item's, a tree item's, a row's in a tree grid; 0 none
    field(focusable, bool),
    field(posinset,  i32),                  % its position in its set, from 1; 0 when it is in none
    field(setsize,   i32),                  % its set's size; -1 when unknown
    field(region,    ref(anode))            % the live region the node is inside, or none
])).
%% a text a node has: its description (and where it came from), a field's value or a combobox's
%% chosen option, its placeholder, a range's valuetext
a11y_contract(record(atext, [
    field(node,      ref(anode)),
    field(prop,      enum(text_prop)),
    field(text,      span(text)),
    field(desc_from, enum(desc_from))       % a description's source; nothing for the other texts
])).
%% a range's value, least and greatest values, each when it has one, and where the value came from
a11y_contract(record(arange, [
    field(node,      ref(anode)),
    field(valuenow,  f64),
    field(valuemin,  f64),
    field(valuemax,  f64),
    field(has_now,   bool),
    field(has_min,   bool),
    field(has_max,   bool),
    field(now_from,  enum(value_from))
])).
%% a live region: its politeness, whether it is read whole, the changes it tells of
a11y_contract(record(alive, [
    field(node,       ref(anode)),
    field(politeness, enum(politeness)),
    field(atomic,     bool),
    field(relevant,   span(text))
])).
%% a state or property of a node whose value is a token, where it came from, and the
%% element that gave it (the node's own, or the ancestor it inherited it from)
a11y_contract(record(astate, [
    field(node,  ref(anode)),
    field(state, enum(state)),
    field(value, enum(token)),
    field(from,  enum(state_from)),
    field(by,    xhandle(dom, node))
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
%% the root of a subtree left out of the tree because it is hidden, and why
a11y_contract(record(ahidden, [field(node, xhandle(dom, node)), field(how, enum(hidden_how))])).
%% an element inside a hidden subtree that takes focus (inside aria-hidden content) or
%% is a live region, with the subtree's root
a11y_contract(record(ahinside, [field(node, xhandle(dom, node)), field(root, xhandle(dom, node)), field(what, enum(inside_what))])).
%% an element that is no node of the tree, by its role, and has aria-* attributes
a11y_contract(record(aplain, [field(node, xhandle(dom, node)), field(role, enum(role))])).
%% a flag of a form control's validity state that holds. Its invalid state is true for
%% any of them, as Chromium tells it, but a text field's missing value (no one has typed)
a11y_contract(record(avalid, [field(node, ref(anode)), field(flag, enum(validity))])).

a11y_contract(handle(anode, ax)).
a11y_contract(handle(atext, axt)).
a11y_contract(handle(arange, axr)).
a11y_contract(handle(alive, axl)).
a11y_contract(handle(astate, axs)).
a11y_contract(handle(aref, ar)).
a11y_contract(handle(badrole, br)).
a11y_contract(handle(ahidden, axh)).
a11y_contract(handle(ahinside, axi)).
a11y_contract(handle(aplain, axp)).
a11y_contract(handle(avalid, axv)).

%% ---- OWNERSHIP --------------------------------------------------------------

a11y_contract(store(tree, [table(anode), table(atext), table(arange), table(alive), table(astate), table(aref), table(badrole),
    table(ahidden), table(ahinside), table(aplain), table(avalid), pool(text)])).
a11y_contract(owns(tree, anode, store)).
a11y_contract(owns(tree, atext, store)).
a11y_contract(owns(tree, arange, store)).
a11y_contract(owns(tree, alive, store)).
a11y_contract(owns(tree, astate, store)).
a11y_contract(owns(tree, aref, store)).
a11y_contract(owns(tree, badrole, store)).
a11y_contract(owns(tree, ahidden, store)).
a11y_contract(owns(tree, ahinside, store)).
a11y_contract(owns(tree, aplain, store)).
a11y_contract(owns(tree, avalid, store)).

%% ---- OPERATIONS -------------------------------------------------------------

a11y_contract(operation(build, [param(doc, store(dom)), param(style, store(css))], status, [], [creates(anode)], [mutates])).
%% the tree as a developer reads it: a node a line, indented by its depth, with its
%% role, element id, name and description and where each came from, its states and
%% properties, its values and its live region
a11y_contract(operation(print_tree, [param(doc, store(dom)), param(out, stream)], status, [], [], [readonly])).

%% ---- INVARIANTS -------------------------------------------------------------

a11y_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

a11y_contract(fact(a11y_node(handle, field(node), field(role), field(name), field(name_from)), table(anode), true)).
a11y_contract(fact(a11y_parent(handle, field(parent)), table(anode), field(parent) \== none)).
a11y_contract(fact(a11y_explicit_role(handle), table(anode), field(stated) == true)).
a11y_contract(fact(a11y_heading(handle, field(level)), table(anode), field(role) == heading)).
a11y_contract(fact(a11y_focusable(handle), table(anode), field(focusable) == true)).
a11y_contract(fact(a11y_level(handle, field(level)), table(anode), field(level) \== 0)).
a11y_contract(fact(a11y_position(handle, field(posinset), field(setsize)), table(anode), field(posinset) \== 0)).
a11y_contract(fact(a11y_live_region(handle, field(region)), table(anode), field(region) \== none)).
a11y_contract(fact(a11y_description(field(node), field(text), field(desc_from)), table(atext), field(prop) == description)).
a11y_contract(fact(a11y_value(field(node), field(text)), table(atext), field(prop) == value)).
a11y_contract(fact(a11y_placeholder(field(node), field(text)), table(atext), field(prop) == placeholder)).
a11y_contract(fact(a11y_valuetext(field(node), field(text)), table(atext), field(prop) == valuetext)).
a11y_contract(fact(a11y_valuenow(field(node), field(valuenow), field(now_from)), table(arange), field(has_now) == true)).
a11y_contract(fact(a11y_valuemin(field(node), field(valuemin)), table(arange), field(has_min) == true)).
a11y_contract(fact(a11y_valuemax(field(node), field(valuemax)), table(arange), field(has_max) == true)).
a11y_contract(fact(a11y_live(field(node), field(politeness), field(atomic), field(relevant)), table(alive), true)).
a11y_contract(fact(a11y_state(field(node), field(state), field(value)), table(astate), true)).
a11y_contract(fact(a11y_state_from(field(node), field(state), field(from), field(by)), table(astate), true)).
a11y_contract(fact(a11y_ref(field(from), field(attr), field(idref), field(target)), table(aref), true)).
a11y_contract(fact(a11y_bad_role(field(node), field(value)), table(badrole), true)).
a11y_contract(fact(a11y_hidden(field(node), field(how)), table(ahidden), true)).
a11y_contract(fact(a11y_hidden_focusable(field(node), field(root)), table(ahinside), field(what) == focusable)).
a11y_contract(fact(a11y_hidden_live(field(node), field(root)), table(ahinside), field(what) == live_region)).
a11y_contract(fact(a11y_no_node(field(node), field(role)), table(aplain), true)).
a11y_contract(fact(a11y_validity(field(node), field(flag)), table(avalid), true)).

a11y_contract(rules('rules/a11y')).
a11y_contract(derived(a11y_of/2, 'the accessibility node of an element')).
a11y_contract(derived(a11y_by_id/2, 'the accessibility node of the element with an id')).
a11y_contract(derived(a11y_role_of/2, 'an element''s role, whether or not it is a node of the tree')).
a11y_contract(derived(a11y_missing_name/2, 'a node whose role needs a name, without one')).
a11y_contract(derived(a11y_broken_ref/3, 'an ID reference to no element')).
a11y_contract(derived(a11y_heading_jump/3, 'a heading more than one level below the heading before it')).
a11y_contract(derived(a11y_nested_interactive/2, 'an interactive node inside another')).
a11y_contract(derived(a11y_duplicate_id/3, 'two elements with one id, which every reference to it confuses')).
a11y_contract(derived(a11y_explain_name/2, 'why a node has its accessible name')).
a11y_contract(derived(a11y_explain_description/2, 'why a node has its accessible description')).
a11y_contract(derived(a11y_explain_state/3, 'why a node has a state, and from which element')).
a11y_contract(derived(a11y_outline/2, 'the headings in document order, with their levels')).
a11y_contract(derived(a11y_unsupported_attr/3, 'an aria-* state or property on an element whose role does not support it')).
a11y_contract(derived(a11y_prohibited_attr/3, 'aria-label or aria-labelledby on an element whose role cannot be named')).
a11y_contract(derived(a11y_unknown_attr/2, 'an aria-* attribute WAI-ARIA does not define')).
a11y_contract(derived(a11y_bad_value/3, 'an aria-* attribute whose value is none of the values it takes')).
a11y_contract(derived(a11y_range_missing/2, 'a range widget without the value its role needs')).
a11y_contract(derived(a11y_range_outside/4, 'a range widget whose aria-valuenow lies outside its least and greatest values')).
a11y_contract(derived(a11y_position_outside/3, 'a node whose position lies outside its set')).
a11y_contract(derived(a11y_live_inside/2, 'a node inside a live region, and the region''s politeness')).

a11y_contract(query(tree, (a11y_node(A, _, R, Nm, F)), [A, R, Nm, F], 'every node of the accessibility tree, its role and name')).
a11y_contract(query(states, (a11y_state(A, S, V)), [A, S, V], 'every state of every node, with its value')).

a11y_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
a11y_contract(test(integ, 'tests/integ/a11y')).
