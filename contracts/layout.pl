%% contracts/layout.pl -- the layout module's contract (docs/contract.md is the format).
%%
%% The box tree made from the DOM and its computed style, and laid out: block
%% layout (widths found as CSS 2.1 10.3 finds them, vertical margins collapsed),
%% floats and clearance, positioned boxes (relative, absolute, fixed; sticky as
%% at rest), flex and grid containers, inline layout (white space processed,
%% lines broken around floats, aligned, vertical-align, bidirectional text
%% reordered), atomic inlines (inline-blocks, replaced elements, form controls)
%% shrunk to fit, tables in their automatic layout (rowspan too), list markers.
%% Every box keeps how its width was found and from what, so the reasoner can say
%% why a box is as wide as it is; and how it is positioned, floated, stacked and
%% clipped, which paint reads.
%% src/layout/layout.c says what of CSS's layout the module does, and what not.

layout_contract(module(layout, '0.1.0', 'Layout: the box tree of a styled document, laid out; each box''s geometry and why it has its width')).

%% ---- TYPES ------------------------------------------------------------------

layout_contract(status([ok, stale_handle, wrong_kind, no_root])).

%% the viewport's box; a block container (an element's, an anonymous one, a list
%% item's); an atomic inline (an inline-block, a replaced element, a form control);
%% a table, its rows and its cells; a flex container, a grid container; an inline
%% element's decorations, a box a fragment (its piece) draws them from: the element
%% as one unbroken strip, its fragments laid end to end, placed so that the
%% fragment's part of it lies on the fragment (box-decoration-break: slice), in no
%% box's children
layout_contract(enum(box_kind, [viewport, block, anonymous, list_item, inline_block, replaced, table, row, cell, flex, grid, inline])).
%% a form control the theme draws (Chromium's NativeThemeBase), in place of its CSS background and border; css: none, CSS draws it
layout_contract(enum(theme, [css, field, area, button, menulist, listbox, checkbox, radio, range, colorwell])).
%% how a box's width was found: the viewport's; filling its containing block; a
%% length, or a percentage, its style declared; shrunk to fit its content; its
%% table column's; its content's own (a replaced element's, a form control's);
%% its max-width, which held it narrower than any of those; its min-width, which
%% held it wider; its flex line's share (a flex item's); its grid area's; its
%% containing block's less its offsets (an absolutely positioned box's)
layout_contract(enum(width_how, [viewport, fill, declared, percent, shrink_to_fit, column, intrinsic, max_width, min_width, flex, grid,
    positioned])).
%% how a box is positioned (CSS 2.1 9.3): in the flow; offset from there; taken
%% out of it, in its containing block (absolute) or the viewport (fixed); sticky
%% (at rest: where the flow put it, moved inward when an offset it has asks)
layout_contract(enum(position, [static, relative, absolute, fixed, sticky])).
%% the side a box floats to
layout_contract(enum(float_side, [unfloated, left, right])).
%% how a grid item was placed (Grid 1, 8): it is no grid item; automatically; by its
%% lines (a number or a name, on either axis); in a named area (grid-area: NAME)
layout_contract(enum(grid_by, [ungridded, auto, lines, area])).
%% what a fragment of a line is: a run of text; an atomic inline's box; a list
%% item's marker; the part of an inline element on the line (its background)
layout_contract(enum(frag_kind, [text, atomic, marker, inline, generated])).

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
    field(bct, u32), field(bcr, u32), field(bcb, u32), field(bcl, u32),  % border colours, RGBA, a side each
    field(bst, u8), field(bsr, u8), field(bsb, u8), field(bsl, u8),      % border styles (src/layout/layout.h)
    field(pseudo, u8),                          % 1 its element's ::before, 2 ::after; 0 the element's own
    field(position,     enum(position)),
    field(cb,           ref(box)),              % an absolutely positioned box's containing block (the viewport's for fixed)
    %% an absolutely positioned box whose containing block is an inline element's: its
    %% first and last fragments' padding boxes make it (CSS 2.1, 10.1); cb is then the box
    %% the first one's line is in
    field(cb_inline,    bool),
    field(cb_node,      xhandle(dom, node)),
    field(float_side,   enum(float_side)),
    field(z,            i32),                   % z-index, a positioned box's (0 for auto)
    field(z_auto,       bool),                  % z-index is auto: the box makes no stacking context of its own
    field(clip,         u8),                    % overflow: 1 its content clipped to its padding box (hidden, clip, scroll, auto)
    field(dx, f64), field(dy, f64),             % a relatively positioned box's offset (a sticky one's at rest), in its position already
    %% what paint reads of its style: the corners' radii (horizontal, then vertical), its
    %% opacity, its shadows, backgrounds, transform, filters and outline as CSS computed them
    field(rtl, f64), field(rtr, f64), field(rbr, f64), field(rbl, f64),
    field(rtlv, f64), field(rtrv, f64), field(rbrv, f64), field(rblv, f64),
    field(opacity,      f64),
    field(shadows,      span(text)),            % box-shadow
    field(bg_image,     span(text)),            % background-image: its layers (url(), gradients)
    field(bg_res,       xhandle(net, resource)),% the image its first url() names, fetched; none for none
    field(bg_pos,       span(text)), field(bg_size, span(text)), field(bg_repeat, span(text)),
    field(bg_clip,      span(text)), field(bg_origin, span(text)),   % background-clip, -origin: a box a layer
    field(transform,    span(text)), field(origin, span(text)),
    field(filter,       span(text)), field(backdrop, span(text)),   % filter, backdrop-filter: their functions
    field(translate, span(text)), field(rotate, span(text)), field(scale, span(text)),  % the individual transforms
    field(persp, span(text)), field(porigin, span(text)),       % perspective, perspective-origin: what its children are seen through
    field(p3d, u8), field(backface, u8),        % transform-style: preserve-3d; backface-visibility: hidden
    %% an svg element's drawing (src/paint/svg.h): its content's shapes, paints, groups, clips
    %% and glyphs, each in its viewport's CSS px, which paint draws in its content box
    field(svg, span(text)),
    %% the theme's drawing of a form control (css: its author styled its background or border, or its appearance is
    %% none): what it is; a check box's or radio button's checked state; a range's value, 0 to 1 along its track; a
    %% colour well's colour, a drop-down's arrow's
    field(theme, enum(theme)), field(checked, bool), field(fraction, f64), field(swatch, u32),
    %% an img with no image to load (no src, no srcset, no source of its picture chosen): with alt text, its
    %% fallback, a broken image's icon before the text; without, its content box outlined in light grey as
    %% Chromium's ImagePainter outlines it, when more than 2px each way
    field(unsourced, bool),
    field(ow, f64), field(ostyle, u8), field(ocolor, u32), field(ooffset, f64),  % outline: width, style, colour, offset
    %% a grid item's area: its first row and column lines (the explicit grid's first line
    %% is 1, an implicit one before it 0 or less), the tracks it spans, how it was placed,
    %% and the area's name when it was placed in one
    field(grid_row, i32), field(grid_col, i32), field(grid_rows, u32), field(grid_cols, u32),
    field(grid_by,      enum(grid_by)),
    field(grid_area,    span(text)),
    %% a flex container's first baseline, from its content box's top, which its items give
    %% it (Flexbox 1, 8.5), when it has one
    field(baseline, f64), field(has_baseline, bool),
    %% a fieldset's rendered legend's box (HTML 15.3.13), set on its top border, whose
    %% border box the top border is not drawn behind; none
    field(legend, ref(box))
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
    field(face,       u8),                      % its font: one of the six built-in faces (src/paint/font.h), or a web font's
    field(font,       xhandle(net, resource)),  % the web font's resource (@font-face), the loader's; none for a built-in face
    field(size,       f64),
    field(spacing,    f64),                     % letter-spacing, px after each character
    field(word_spacing, f64),                   % word-spacing, px after each space
    field(italic,     bool),
    field(color,      u32),
    field(fill,       u32),                     % -webkit-text-fill-color: the colour its glyphs are filled with (color's unless set)
    field(background, u32),                     % an inline fragment's
    field(piece,      ref(box)),                % an inline fragment's box of decorations (borders, rounded corners, images, shadows); none
    field(decoration, u8),                      % underline 1, overline 2, line-through 4
    field(shadows,    span(text))               % text-shadow
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
layout_contract(fact(layout_pseudo(handle, field(pseudo)), table(box), field(pseudo) \== 0)).
%% a positioned box: how, its containing block (an absolute or fixed one's), its offset (a relative one's)
layout_contract(fact(layout_positioned(handle, field(position), field(cb), field(dx), field(dy)), table(box), field(position) \== static)).
%% the inline element whose fragments make an absolutely positioned box's containing block
layout_contract(fact(layout_inline_cb(handle, field(cb_node)), table(box), field(cb_inline) == true)).
layout_contract(fact(layout_stacking(handle, field(z)), table(box), field(z_auto) == false)).
layout_contract(fact(layout_float(handle, field(float_side)), table(box), field(float_side) \== unfloated)).
%% a grid item's area: its first row line and column line, the rows and columns it spans, how it was placed
layout_contract(fact(layout_grid_item(handle, field(grid_row), field(grid_col), field(grid_rows), field(grid_cols), field(grid_by)), table(box),
    field(grid_by) \== ungridded)).
layout_contract(fact(layout_grid_area(handle, field(grid_area)), table(box), field(grid_by) == area)).
layout_contract(fact(layout_clips(handle), table(box), field(clip) \== 0)).
%% a box's background colour, RGBA, where it has one; the viewport's is the canvas's: the
%% Canvas of the scheme the root is used in (CSS Color Adjust 1)
layout_contract(fact(layout_background(handle, field(background)), table(box), field(background) \== 0)).
layout_contract(fact(layout_margin(handle, field(mt), field(mr), field(mb), field(ml)), table(box), true)).
layout_contract(fact(layout_border(handle, field(bt), field(br), field(bb), field(bl)), table(box), true)).
layout_contract(fact(layout_padding(handle, field(pt), field(pr), field(pb), field(pl)), table(box), true)).
layout_contract(fact(layout_width(handle, field(w), field(how), field(avail)), table(box), true)).
layout_contract(fact(layout_line(handle, field(box), field(x), field(y), field(w), field(h)), table(line), true)).
layout_contract(fact(layout_text(handle, field(line), field(node), field(text), field(x), field(y), field(w), field(h)), table(frag),
    field(kind) == text)).
layout_contract(fact(layout_frag(handle, field(line), field(kind), field(node), field(x), field(y), field(w), field(h)), table(frag),
    field(kind) \== text)).
%% the text a pseudo-element gave (::before's and ::after's content, a field's placeholder)
layout_contract(fact(layout_generated(handle, field(node), field(text)), table(frag), field(kind) == generated)).

layout_contract(rules('rules/layout')).
layout_contract(derived(layout_box_of/2, 'the box an element generated')).
layout_contract(derived(layout_inline_box/5, 'an inline element''s box, from its fragments and its text''s: X, Y, W, H')).
layout_contract(derived(layout_pseudo_box_of/3, 'the box an element''s ::before or ::after generated')).
layout_contract(derived(layout_explain_position/2, 'why a box is where it is: the flow, a float, an offset, its containing block')).
layout_contract(derived(layout_paint_order/2, 'the order boxes are painted in: stacking contexts by z-index, then the flow')).
layout_contract(derived(layout_border_box/5, 'a box''s border box: X, Y, W, H')).
layout_contract(derived(layout_explain_width/2, 'why a box is as wide as it is')).
layout_contract(derived(layout_at/3, 'the deepest element whose box holds a point')).
layout_contract(derived(layout_overflows/3, 'a box wider than its container, and by how much')).
layout_contract(derived(layout_text_overflow/2, 'a box whose lines'' text runs past its content box, and by how much')).

layout_contract(query(widths, (layout_box_of(N, B), dom_element(N, E), layout_explain_width(B, Why)), [E, Why],
    'every element''s box''s width, and why')).

layout_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
layout_contract(test(exec, 'tests/exec/layout')).
