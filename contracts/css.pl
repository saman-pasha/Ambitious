%% contracts/css.pl -- the CSS module's contract (docs/contract.md is the format).
%%
%% Style sheets parsed (CSS Syntax 3's shapes; the rules and their declarations),
%% selectors matched against the DOM, the cascade run (origin and importance, the
%% style attribute over rules, specificity, order of appearance; inheritance;
%% initial values) and each element's computed values kept with their
%% provenance: the declaration that won, and whether the value is the declared
%% one, inherited or initial. The reasoner explains a value from these facts:
%% which rule won, and at which step of the cascade each other one lost.
%% src/css/css.c says what of CSS the module reads, and what it simplifies.

css_contract(module(css, '0.1.0', 'CSS: style sheets, selectors, the cascade and computed values with their provenance')).

%% ---- TYPES ------------------------------------------------------------------

css_contract(status([ok, stale_handle, wrong_kind])).

css_contract(enum(origin, [user_agent, author, inline])).
%% the properties the module computes, longhands each (a shorthand is expanded into
%% them); custom is every custom property (--name), its name the value's word
css_contract(enum(prop, [display, color, background_color, font_size, font_weight, font_style, text_align,
    line_height, text_decoration, white_space, margin_top, margin_right, margin_bottom, margin_left,
    padding_top, padding_right, padding_bottom, padding_left,
    border_top_width, border_right_width, border_bottom_width, border_left_width,
    border_top_style, border_right_style, border_bottom_style, border_left_style,
    border_top_color, border_right_color, border_bottom_color, border_left_color,
    border_top_left_radius, border_top_right_radius, border_bottom_right_radius, border_bottom_left_radius,
    width, height, min_width, min_height, max_width, max_height,
    list_style_type, list_style_position,
    font_family,                       % the family a font-family list resolves to: a generic one, or a web font's
    box_sizing, position, top, right, bottom, left, float, clear, z_index, overflow_x, overflow_y,
    vertical_align, opacity, visibility,
    flex_direction, flex_wrap, flex_grow, flex_shrink, flex_basis, order,
    justify_content, justify_items, justify_self, align_items, align_self, align_content, row_gap, column_gap,
    grid_template_columns, grid_template_rows, grid_auto_columns, grid_auto_rows, grid_auto_flow,
    grid_column_start, grid_column_end, grid_row_start, grid_row_end,
    background_image, background_position, background_size, background_repeat,
    box_shadow, text_shadow, transform, transform_origin,
    outline_width, outline_style, outline_color, outline_offset,
    letter_spacing, word_spacing, text_indent, text_transform, direction, unicode_bidi, writing_mode, hyphens,
    content, counter_reset, counter_increment, quotes,
    color_scheme,                      % its colour field the Canvas colour of the scheme it is used in
    background_clip, background_origin,
    filter, backdrop_filter,           % their functions as text (lengths px, colours computed)
    perspective, perspective_origin, transform_style, backface_visibility,
    translate, rotate, scale,          % the individual transforms, before transform's functions
    animation_name, animation_duration, animation_timing_function, animation_delay,
    animation_iteration_count, animation_direction, animation_fill_mode, animation_play_state,
    transition_property, transition_duration, transition_timing_function, transition_delay,
    transition_behavior,               % each a list, as text, one item a comma
    grid_template_areas,               % the named areas, kept as their strings
    webkit_text_fill_color,            % -webkit-text-fill-color: the colour glyphs are filled with
    text_overflow,                     % clip, or ellipsis where a line overflows a box that clips it
    webkit_line_clamp,                 % -webkit-line-clamp: none, or the lines a vertical -webkit-box keeps
    webkit_box_orient,                 % -webkit-box-orient: horizontal (inline-axis) or vertical (block-axis)
    %% SVG's (SVG 2, 13 and 14; CSS Masking 1): a paint (fill, stroke) as text -- none,
    %% currentcolor (a keyword, each element's own colour), a colour, url(#id) and its
    %% fallback; a dash array as text; clip-path's none or url(#id)
    fill, fill_opacity, fill_rule, stroke, stroke_width, stroke_linecap, stroke_linejoin, stroke_miterlimit,
    stroke_dasharray, stroke_dashoffset, stroke_opacity, stop_color, stop_opacity, clip_path, clip_rule, text_anchor,
    appearance,                        % none, auto or a control's own (CSS UI 4): whether the theme draws it
    object_fit,                        % fill, contain, cover, none or scale-down (CSS Images 3, 5.5)
    object_position,                   % each axis a percentage, px, or a calc() of both, the percentage first
    %% CSS Conditional 5's query containers: container-type (normal, size, inline-size, scroll-state, as a
    %% keyword or kept as text) and container-name (none, or its names kept as text)
    container_type, container_name,
    custom])).
%% a value: a keyword, a length in px (em and rem as declared), a percentage, a colour
%% (RGBA), auto, a number; a calc() that is num px plus pct percent, or one that is
%% no such sum (min(), max(), clamp() over percentages), kept as its text with every
%% length made px; a value kept as its text (lists, functions: gradients, shadows,
%% transforms, track lists, content); a declared value with var() in it
css_contract(enum(unit, [keyword, px, em, percent, color, auto, number, rem, calc, expr, text, var])).
%% how a computed value came: a declaration gave it, the parent's, the initial one, or an
%% animation at the page's rest time (its winner the keyframe's declaration)
css_contract(enum(how, [declared, inherited, initial, animated])).
%% what a rule's subject is: the element, or one of its pseudo-elements
css_contract(enum(pseudo, [element, before, after, first_line, first_letter, marker, placeholder])).
%% a state of the page the pseudo-classes of state read (:hover, :focus, :focus-visible,
%% :active, :target): set by a command or a script (css_set_state), :target by the URL's fragment
css_contract(enum(state_kind, [hover, focus, focus_visible, active, target])).

css_contract(record(sheet, [field(origin, enum(origin)), field(first_rule, ref(rule)), field(last_rule, ref(rule)),
    field(url,    span(text)),          % where it was fetched from: a linked or imported sheet's
    field(parent, ref(sheet))           % the sheet whose @import brought it
])).
css_contract(record(rule, [
    field(sheet,      ref(sheet)),
    field(next,       ref(rule)),
    field(selector,   span(text)),
    field(spec,       u32),            % specificity: a * 65536 + b * 256 + c
    field(order,      u32),            % order of appearance, over every sheet
    field(first_decl, ref(decl)),
    field(last_decl,  ref(decl)),
    field(layer,      ref(layer)),      % its cascade layer (@layer); none: unlayered
    field(keyframes,  ref(keyframes)),  % a keyframe's: its @keyframes (it matches no element); none for a style rule
    field(offset,     f64),             % a keyframe's place in its animation, 0 to 1
    field(scoped,     bool),            % in an @scope: its selector matched below a scoping root
    field(scope,      span(text)),      % the scoping roots' selector list (empty: the style element's parent)
    field(scope_end,  span(text)),      % the scoping limits' (empty: none)
    field(scope_host, xhandle(dom, node)),  % the root of an @scope without roots' selectors
    field(container,  ref(cquery))      % the innermost @container it is in (none: in none); it applies where each one's condition holds
])).
%% an @container rule (CSS Conditional 5, 6.1): its prelude as written (its queries, each a container's name and a
%% condition; white space one space), the @container it is in, its sheet. Its condition is answered for each element by
%% the container it asks (the nearest of the type and the name it needs), once layout knows that container's size
css_contract(record(cquery, [field(text, span(text)), field(parent, ref(cquery)), field(sheet, ref(sheet))])).
%% a counter style a sheet defined (@counter-style, CSS Counter Styles 3): its name, its system as written (symbolic
%% when it gives none), its descriptors as written (`name: value;' each), its sheet. The predefined styles (decimal,
%% the alphabets, the CJK and Indic numbers, ...) are the module's own and are not records
css_contract(record(cstyle, [field(name, span(text)), field(system, span(text)), field(text, span(text)), field(sheet, ref(sheet))])).
%% a custom property a sheet registered (@property, CSS Properties and Values 1): its name,
%% its syntax (as written, its quotes gone), whether it inherits, its initial value (computed)
css_contract(record(regprop, [field(name, atom(names)), field(syntax, span(text)), field(inherits, bool),
    field(initial, span(text)), field(sheet, ref(sheet))])).
%% an @keyframes rule: its name, its sheet, its cascade layer; its keyframes are rules
css_contract(record(keyframes, [field(name, span(text)), field(sheet, ref(sheet)), field(layer, ref(layer))])).
%% a cascade layer (Cascade 5): its full name (a.b for b in a; an anonymous one's is
%% written <anonymous N>), the layer it is in, and its place in the layers' order
%% (each layer's own rules after its sublayers'; the unlayered rules after every layer)
css_contract(record(layer, [field(name, span(text)), field(parent, ref(layer)), field(rank, u32)])).
%% a web font a sheet declared (@font-face): its family, the source read, its weight and
%% style, and the resource the loader fetched for it
css_contract(record(face, [field(family, span(text)), field(url, span(text)), field(weight, u32), field(italic, bool),
    field(font, xhandle(net, resource))])).
css_contract(record(decl, [
    field(rule,      ref(rule)),
    field(next,      ref(decl)),
    field(prop,      enum(prop)),
    field(important, bool),
    field(unit,      enum(unit)),
    field(num,       f64),
    field(word,      atom(names)),
    field(color,     u32),
    field(text,      span(text))       % the value as written (a custom property's name is its word)
])).
%% a rule that matched an element (or, pseudo, one of its pseudo-elements)
css_contract(record(match, [field(node, xhandle(dom, node)), field(rule, ref(rule)), field(pseudo, enum(pseudo)),
    field(proximity, u32),              % a scoped rule's: its scoping root's generations above the node, plus one; 0 unscoped
    field(unmet, bool)                  % its selector matched, but its @container's condition does not hold for the node
])).
%% a pseudo-element's computed style: its element, which one, its style (whose parent is
%% the element's), the first of its values
css_contract(record(pstyle, [field(node, xhandle(dom, node)), field(pseudo, enum(pseudo)), field(style, u32), field(first, ref(cval))])).
%% a state the page is in: an element hovered, focused, active, or the URL's target
css_contract(record(state, [field(kind, enum(state_kind)), field(node, xhandle(dom, node))])).
%% A COMPUTED STYLE is a style's values, one record a property, in a run, then its
%% custom properties (inherited and declared); elements whose matched rules are the
%% same, under parents of the same style, share one (PLAN.md 11)
css_contract(record(cval, [
    field(style,  u32),                % the style the value is of, a number from 1
    field(prop,   enum(prop)),
    field(unit,   enum(unit)),
    field(num,    f64),
    field(pct,    f64),                % a calc()'s percentage, beside num px
    field(word,   atom(names)),        % a keyword; a custom property's name
    field(color,  u32),                % a colour, RGBA; color-scheme's: the Canvas of the scheme it is used in
    field(text,   span(text)),         % the computed value, as CSS writes it
    field(how,    enum(how)),
    field(winner, ref(decl))           % the declaration that won the cascade, if one applied
])).                                   % (its value may be inherit, initial or unset)
%% an element's computed style: which style, and the first of its values
css_contract(record(estyle, [
    field(node,   xhandle(dom, node)),
    field(style,  u32),
    field(first,  ref(cval))
])).
%% a property's initial value, as computed (border-color's is the element's colour)
css_contract(record(initval, [
    field(prop,   enum(prop)),
    field(unit,   enum(unit)),
    field(color,  u32),
    field(text,   span(text))
])).

css_contract(handle(sheet, sh)).
css_contract(handle(rule, ru)).
css_contract(handle(decl, de)).
css_contract(handle(match, ma)).
css_contract(handle(cval, cv)).
css_contract(handle(initval, iv)).
css_contract(handle(estyle, es)).
css_contract(handle(layer, ly)).
css_contract(handle(pstyle, ps)).
css_contract(handle(state, st)).
css_contract(handle(face, ff)).
css_contract(handle(keyframes, kf)).
css_contract(handle(regprop, rg)).
css_contract(handle(cquery, cq)).
css_contract(handle(cstyle, cs)).

%% ---- OWNERSHIP --------------------------------------------------------------

css_contract(store(style, [table(sheet), table(rule), table(decl), table(match), table(cval), table(estyle), table(initval), table(layer),
    table(face), table(pstyle), table(state), table(keyframes), table(regprop), table(cquery), table(cstyle), pool(text), atoms(names)])).
css_contract(owns(style, sheet, store)).
css_contract(owns(style, rule, store)).
css_contract(owns(style, decl, store)).
css_contract(owns(style, match, store)).
css_contract(owns(style, cval, store)).
css_contract(owns(style, initval, store)).
css_contract(owns(style, estyle, store)).
css_contract(owns(style, layer, store)).
css_contract(owns(style, face, store)).
css_contract(owns(style, keyframes, store)).
css_contract(owns(style, regprop, store)).
css_contract(owns(style, cquery, store)).
css_contract(owns(style, cstyle, store)).
css_contract(owns(style, pstyle, store)).
css_contract(owns(style, state, store)).
css_contract(owns(sheet, rule, list(rules))).
css_contract(owns(rule, decl, list(decls))).
css_contract(list(rules, sheet, rule, [first(first_rule), last(last_rule), next(next), up(sheet)])).
css_contract(list(decls, rule, decl, [first(first_decl), last(last_decl), next(next), up(rule)])).

%% ---- OPERATIONS -------------------------------------------------------------

%% a style sheet's text parsed into rules and declarations, of an origin
css_contract(operation(parse_sheet, [param(origin, enum(origin)), param(src, text)], status, [], [creates(sheet)], [mutates])).
%% the user agent's sheet, the document's style elements, the sheets its links
%% loaded (from the network module's loader, when there is one) and its style
%% attributes, matched and cascaded: every element's computed values
css_contract(operation(cascade, [param(doc, store(dom)), param(net, store(net))], status, [], [creates(cval)], [mutates])).
%% a state of the page set before the cascade: the element (by its node's index) that is
%% hovered, focused, active or the target; the pseudo-classes of state read it
css_contract(operation(set_state, [param(doc, store(dom)), param(kind, enum(state_kind)), param(node, u32)], status, [], [creates(state)],
    [mutates])).

%% ---- INVARIANTS -------------------------------------------------------------

css_contract(invariant(rule_links, 'Each rule is listed once, by its sheet', list_consistent(rules))).
css_contract(invariant(decl_links, 'Each declaration is listed once, by its rule', list_consistent(decls))).
css_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

css_contract(fact(css_sheet(handle, field(origin)), table(sheet), true)).
%% a sheet an @import brought, the sheet it is in, and where it was fetched from
css_contract(fact(css_imported(handle, field(parent), field(url)), table(sheet), field(parent) \== none)).
css_contract(fact(css_rule(handle, field(sheet), field(selector), field(spec), field(order)), table(rule), true)).
css_contract(fact(css_rule_layer(handle, field(layer)), table(rule), field(layer) \== none)).
%% @keyframes and their keyframes: each keyframe a rule of its own, with its place
css_contract(fact(css_keyframes(handle, field(name), field(sheet)), table(keyframes), true)).
css_contract(fact(css_keyframe(handle, field(keyframes), field(offset)), table(rule), field(keyframes) \== none)).
css_contract(fact(css_registered(handle, field(name), field(syntax), field(inherits), field(initial)), table(regprop), true)).
css_contract(fact(css_layer(handle, field(name), field(rank)), table(layer), true)).
css_contract(fact(css_font_face(handle, field(family), field(url), field(weight), field(italic)), table(face), true)).
css_contract(fact(css_font_face_font(handle, field(font)), table(face), true)).
css_contract(fact(css_decl(handle, field(rule), field(prop), field(text), field(important)), table(decl), true)).
css_contract(fact(css_matched(field(node), field(rule)), table(match), (field(pseudo) == element, field(unmet) == false))).
%% a scoped rule (@scope, Cascade 6): its roots' and limits' selectors; a match of one, its
%% scoping root's distance (generations, plus one), which ranks it over a farther one
css_contract(fact(css_scope(handle, field(scope), field(scope_end)), table(rule), field(scoped) == true)).
css_contract(fact(css_match_proximity(field(node), field(rule), field(proximity)), table(match), field(proximity) \== 0)).
css_contract(fact(css_pseudo_matched(field(node), field(pseudo), field(rule)), table(match), (field(pseudo) \== element, field(unmet) == false))).
%% a rule whose selector matched an element (or its pseudo-element) but whose @container's condition does not hold for it
css_contract(fact(css_container_unmet(field(node), field(rule)), table(match), field(unmet) == true)).
%% @container rules and the rules in them; the counter styles a sheet defined
css_contract(fact(css_container_query(handle, field(text), field(parent)), table(cquery), true)).
css_contract(fact(css_rule_container(handle, field(container)), table(rule), field(container) \== none)).
css_contract(fact(css_counter_style(handle, field(name), field(system)), table(cstyle), true)).
%% a pseudo-element's style when it is generated (a ::before or an ::after whose content is none has its style, no first)
css_contract(fact(css_pseudo_style_of(field(node), field(pseudo), field(style)), table(pstyle), field(first) \== none)).
css_contract(fact(css_state(field(kind), field(node)), table(state), true)).
%% EACH ELEMENT'S STYLE; OF EACH STYLE, THE VALUES A DECLARATION GAVE; and each
%% property's initial value. Every other computed value is the parent element's or
%% the initial one, which the rules derive (css_computed/4, css_rgba/3), so the
%% snapshot need not carry it (PLAN.md 11)
css_contract(fact(css_style_of(field(node), field(style)), table(estyle), true)).
css_contract(fact(css_style_declared(field(style), field(prop), field(text), field(how)), table(cval),
    (field(winner) \== none, field(prop) \== custom))).
css_contract(fact(css_style_won(field(style), field(prop), field(winner)), table(cval), (field(winner) \== none, field(prop) \== custom))).
%% a custom property a style's own declaration gave, var() substituted: its value, and
%% initial when it was invalid at computed-value time (a cycle, a var() with no value)
css_contract(fact(css_style_custom(field(style), field(word), field(text), field(how), field(winner)), table(cval),
    (field(prop) == custom, field(winner) \== none))).
css_contract(fact(css_decl_custom(handle, field(word)), table(decl), field(prop) == custom)).
%% a computed colour as a number, RGBA (alpha in the low byte): what contrast is reckoned from
css_contract(fact(css_style_rgba(field(style), field(prop), field(color)), table(cval), (field(unit) == color, field(winner) \== none))).
css_contract(fact(css_initial(field(prop), field(text)), table(initval), true)).
css_contract(fact(css_initial_rgba(field(prop), field(color)), table(initval), field(unit) == color)).

css_contract(rules('rules/css')).
css_contract(derived(css_declared/4, 'an element''s computed value of a property that a declaration gave, and how')).
css_contract(derived(css_won/3, 'the declaration that won the cascade for an element''s property')).
css_contract(derived(css_computed/4, 'an element''s computed value of a property, as CSS writes it, and whether it was declared, inherited or initial')).
css_contract(derived(css_rgba/3, 'an element''s computed colour of a property as a number, RGBA')).
css_contract(derived(css_origin_of/2, 'the origin of a declaration')).
css_contract(derived(css_lost/4, 'a matching declaration that lost the cascade for a property, and at which step')).
css_contract(derived(css_layer_of/2, 'the cascade layer of a declaration, its name; unlayered when it is in none')).
css_contract(derived(css_explain/3, 'why an element has its computed value of a property')).
css_contract(derived(css_specificity/2, 'a specificity as Selectors writes it, spec(A, B, C)')).
css_contract(derived(css_background_of/3, 'the background an element''s text is drawn on, and whose it is')).
css_contract(derived(css_rendered/1, 'an element that is drawn: no display none on it or above it')).
css_contract(derived(css_contrast/2, 'the WCAG 2 contrast ratio of an element''s text with its background')).
css_contract(derived(css_custom/3, 'an element''s computed value of a custom property, its own or inherited')).
css_contract(derived(css_pseudo_computed/5, 'a pseudo-element''s computed value of a property, and how: declared, or its element''s')).

css_contract(query(colours, (dom_element(N, E), css_computed(N, color, C, H)), [E, C, H], 'every element''s colour and where it came from')).
css_contract(query(overridden, (dom_element(N, E), css_lost(N, P, D, Why)), [E, P, D, Why], 'every declaration that lost, and why')).

css_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
css_contract(test(exec, 'tests/css')).
