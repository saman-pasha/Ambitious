%% contracts/layout.pl -- the layout module's contract (docs/contract.md is the format).
%%
%% The box tree made from the DOM and its computed style, and laid out: block
%% layout (widths found as CSS 2.1 10.3 finds them, vertical margins collapsed),
%% inline layout (white space processed, lines broken, aligned), atomic inlines
%% (inline-blocks, replaced elements, form controls) shrunk to fit, tables in
%% their automatic layout, list markers. Every box keeps how its width was found
%% and from what, so the reasoner can say why a box is as wide as it is.
%% src/layout/layout.c says what of CSS's layout the module does, and what not.

layout_contract(module(layout, '0.1.0', 'Layout: the box tree of a styled document, laid out; each box''s geometry and why it has its width')).

%% ---- TYPES ------------------------------------------------------------------

layout_contract(status([ok, stale_handle, wrong_kind, no_root])).

%% the viewport's box; a block container (an element's, an anonymous one, a list
%% item's); an atomic inline (an inline-block, a replaced element, a form control);
%% a table, its rows and its cells
layout_contract(enum(box_kind, [viewport, block, anonymous, list_item, inline_block, replaced, table, row, cell])).
%% how a box's width was found: the viewport's; filling its containing block; a
%% length, or a percentage, its style declared; shrunk to fit its content; its
%% table column's; its content's own (a replaced element's, a form control's);
%% its max-width, which held it narrower than any of those
layout_contract(enum(width_how, [viewport, fill, declared, percent, shrink_to_fit, column, intrinsic, max_width])).
%% what a fragment of a line is: a run of text; an atomic inline's box; a list
%% item's marker; the part of an inline element on the line (its background)
layout_contract(enum(frag_kind, [text, atomic, marker, inline])).
layout_contract(enum(face, [sans, sans_bold, serif, serif_bold, mono, mono_bold])).

layout_contract(record(box, [
    field(node,         xhandle(dom, node)),    % the element; none for the viewport and an anonymous box
    field(kind,         enum(box_kind)),
    field(parent,       ref(box)),
    field(next,         ref(box)),
    field(first_child,  ref(box)),
    field(last_child,   ref(box)),
    field(first_line,   ref(line)),
    field(last_line,    ref(line)),
    field(x, f64), field(y, f64), field(w, f64), field(h, f64),          % the content box, in page px
    field(mt, f64), field(mr, f64), field(mb, f64), field(ml, f64),     % its used margins
    field(bt, f64), field(br, f64), field(bb, f64), field(bl, f64),     % borders
    field(pt, f64), field(pr, f64), field(pb, f64), field(pl, f64),     % paddings
    field(how,          enum(width_how)),
    field(avail,        f64),                   % the width its width was found from
    field(background,   u32),                   % RGBA, behind its padding box
    field(border_color, u32)
])).
layout_contract(record(line, [
    field(box,        ref(box)),
    field(next,       ref(line)),
    field(first_frag, ref(frag)),
    field(last_frag,  ref(frag)),
    field(x, f64), field(y, f64), field(w, f64), field(h, f64),          % the line box, in page px
    field(baseline,   f64)                      % its baseline's y, in page px
])).
layout_contract(record(frag, [
    field(line,       ref(line)),
    field(next,       ref(frag)),
    field(kind,       enum(frag_kind)),
    field(node,       xhandle(dom, node)),      % the text node; the atomic, inline or list item element
    field(atomic,     ref(box)),                % an atomic fragment's box
    field(text,       span(text)),              % a text or marker fragment's text, its white space as laid out
    field(x, f64), field(y, f64), field(w, f64), field(h, f64),          % in page px
    field(baseline,   f64),
    field(face,       enum(face)),
    field(size,       f64),
    field(italic,     bool),
    field(color,      u32),
    field(background, u32),                     % an inline fragment's
    field(decoration, u8)                       % underline 1, overline 2, line-through 4
])).

layout_contract(handle(box, bx)).
layout_contract(handle(line, ln)).
layout_contract(handle(frag, fr)).

%% ---- OWNERSHIP --------------------------------------------------------------

layout_contract(store(tree, [table(box), table(line), table(frag), pool(text)])).
layout_contract(owns(tree, box, store)).
layout_contract(owns(tree, line, store)).
layout_contract(owns(tree, frag, store)).
layout_contract(owns(box, box, list(children))).
layout_contract(owns(box, line, list(lines))).
layout_contract(owns(line, frag, list(frags))).
layout_contract(list(children, box, box, [first(first_child), last(last_child), next(next), up(parent)])).
layout_contract(list(lines, box, line, [first(first_line), last(last_line), next(next), up(box)])).
layout_contract(list(frags, line, frag, [first(first_frag), last(last_frag), next(next), up(line)])).

%% ---- OPERATIONS -------------------------------------------------------------

%% the document's box tree, laid out in a viewport of width x height px
%% (images' sizes from the network module's loader, when there is one)
layout_contract(operation(run, [param(doc, store(dom)), param(style, store(css)), param(net, store(net)), param(width, f64),
    param(height, f64)], status, [], [creates(box)], [mutates])).

%% ---- INVARIANTS -------------------------------------------------------------

layout_contract(invariant(child_links, 'Each box is listed once, by its parent', list_consistent(children))).
layout_contract(invariant(line_links, 'Each line is listed once, by its box', list_consistent(lines))).
layout_contract(invariant(frag_links, 'Each fragment is listed once, by its line', list_consistent(frags))).
layout_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

layout_contract(fact(layout_box(handle, field(node), field(kind), field(x), field(y), field(w), field(h)), table(box), true)).
layout_contract(fact(layout_parent(handle, field(parent)), table(box), field(parent) \== none)).
layout_contract(fact(layout_margin(handle, field(mt), field(mr), field(mb), field(ml)), table(box), true)).
layout_contract(fact(layout_border(handle, field(bt), field(br), field(bb), field(bl)), table(box), true)).
layout_contract(fact(layout_padding(handle, field(pt), field(pr), field(pb), field(pl)), table(box), true)).
layout_contract(fact(layout_width(handle, field(w), field(how), field(avail)), table(box), true)).
layout_contract(fact(layout_line(handle, field(box), field(x), field(y), field(w), field(h)), table(line), true)).
layout_contract(fact(layout_text(handle, field(line), field(node), field(text), field(x), field(y), field(w), field(h)), table(frag),
    field(kind) == text)).
layout_contract(fact(layout_frag(handle, field(line), field(kind), field(node), field(x), field(y), field(w), field(h)), table(frag),
    field(kind) \== text)).

layout_contract(rules('rules/layout')).
layout_contract(derived(layout_box_of/2, 'the box an element generated')).
layout_contract(derived(layout_border_box/5, 'a box''s border box: X, Y, W, H')).
layout_contract(derived(layout_explain_width/2, 'why a box is as wide as it is')).
layout_contract(derived(layout_at/3, 'the deepest element whose box holds a point')).
layout_contract(derived(layout_overflows/3, 'a box wider than its container, and by how much')).
layout_contract(derived(layout_text_overflow/2, 'a box whose lines'' text runs past its content box, and by how much')).

layout_contract(query(widths, (layout_box_of(N, B), dom_element(N, E), layout_explain_width(B, Why)), [E, Why],
    'every element''s box''s width, and why')).

layout_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
layout_contract(test(exec, 'tests/exec/layout')).
