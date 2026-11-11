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
    custom])).
%% a value: a keyword, a length in px (em and rem as declared), a percentage, a colour
%% (RGBA), auto, a number; a calc() that is num px plus pct percent, or one that is
%% no such sum (min(), max(), clamp() over percentages), kept as its text with every
%% length made px; a value kept as its text (lists, functions: gradients, shadows,
%% transforms, track lists, content); a declared value with var() in it
css_contract(enum(unit, [keyword, px, em, percent, color, auto, number, rem, calc, expr, text, var])).
css_contract(enum(how, [declared, inherited, initial])).

css_contract(record(sheet, [field(origin, enum(origin)), field(first_rule, ref(rule)), field(last_rule, ref(rule))])).
css_contract(record(rule, [
    field(sheet,      ref(sheet)),
    field(next,       ref(rule)),
    field(selector,   span(text)),
    field(spec,       u32),            % specificity: a * 65536 + b * 256 + c
    field(order,      u32),            % order of appearance, over every sheet
    field(first_decl, ref(decl)),
    field(last_decl,  ref(decl))
])).
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
%% a rule that matched an element
css_contract(record(match, [field(node, xhandle(dom, node)), field(rule, ref(rule))])).
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
    field(color,  u32),
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

%% ---- OWNERSHIP --------------------------------------------------------------

css_contract(store(style, [table(sheet), table(rule), table(decl), table(match), table(cval), table(estyle), table(initval), pool(text), atoms(names)])).
css_contract(owns(style, sheet, store)).
css_contract(owns(style, rule, store)).
css_contract(owns(style, decl, store)).
css_contract(owns(style, match, store)).
css_contract(owns(style, cval, store)).
css_contract(owns(style, initval, store)).
css_contract(owns(style, estyle, store)).
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

%% ---- INVARIANTS -------------------------------------------------------------

css_contract(invariant(rule_links, 'Each rule is listed once, by its sheet', list_consistent(rules))).
css_contract(invariant(decl_links, 'Each declaration is listed once, by its rule', list_consistent(decls))).
css_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

css_contract(fact(css_sheet(handle, field(origin)), table(sheet), true)).
css_contract(fact(css_rule(handle, field(sheet), field(selector), field(spec), field(order)), table(rule), true)).
css_contract(fact(css_decl(handle, field(rule), field(prop), field(text), field(important)), table(decl), true)).
css_contract(fact(css_matched(field(node), field(rule)), table(match), true)).
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
css_contract(derived(css_explain/3, 'why an element has its computed value of a property')).
css_contract(derived(css_specificity/2, 'a specificity as Selectors writes it, spec(A, B, C)')).
css_contract(derived(css_background_of/3, 'the background an element''s text is drawn on, and whose it is')).
css_contract(derived(css_rendered/1, 'an element that is drawn: no display none on it or above it')).
css_contract(derived(css_contrast/2, 'the WCAG 2 contrast ratio of an element''s text with its background')).
css_contract(derived(css_custom/3, 'an element''s computed value of a custom property, its own or inherited')).

css_contract(query(colours, (dom_element(N, E), css_computed(N, color, C, H)), [E, C, H], 'every element''s colour and where it came from')).
css_contract(query(overridden, (dom_element(N, E), css_lost(N, P, D, Why)), [E, P, D, Why], 'every declaration that lost, and why')).

css_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
css_contract(test(exec, 'tests/css')).
