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
%% the properties the module computes; a shorthand is expanded into these
css_contract(enum(prop, [display, color, background_color, font_size, font_weight, font_style, text_align,
    line_height, text_decoration, white_space, margin_top, margin_right, margin_bottom, margin_left,
    padding_top, padding_right, padding_bottom, padding_left, border_top_width, border_right_width,
    border_bottom_width, border_left_width, border_color, width, height, list_style_type,
    font_family,                       % the generic family a font-family list resolves to
    box_sizing, max_width])).
%% a value: a keyword, a length in px, a percentage, a colour (RGBA), auto, a number
css_contract(enum(unit, [keyword, px, em, percent, color, auto, number])).
%% where a computed value came from
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
    field(text,      span(text))       % the value as written
])).
%% a rule that matched an element
css_contract(record(match, [field(node, xhandle(dom, node)), field(rule, ref(rule))])).
%% an element's computed value of a property
css_contract(record(cval, [
    field(node,   xhandle(dom, node)),
    field(prop,   enum(prop)),
    field(unit,   enum(unit)),
    field(num,    f64),
    field(word,   atom(names)),
    field(color,  u32),
    field(text,   span(text)),         % the computed value, as CSS writes it
    field(how,    enum(how)),
    field(winner, ref(decl))           % the declaration that won the cascade, if one applied
])).                                   % (its value may be inherit, initial or unset)

css_contract(handle(sheet, sh)).
css_contract(handle(rule, ru)).
css_contract(handle(decl, de)).
css_contract(handle(match, ma)).
css_contract(handle(cval, cv)).

%% ---- OWNERSHIP --------------------------------------------------------------

css_contract(store(style, [table(sheet), table(rule), table(decl), table(match), table(cval), pool(text), atoms(names)])).
css_contract(owns(style, sheet, store)).
css_contract(owns(style, rule, store)).
css_contract(owns(style, decl, store)).
css_contract(owns(style, match, store)).
css_contract(owns(style, cval, store)).
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
css_contract(fact(css_computed(field(node), field(prop), field(text), field(how)), table(cval), true)).
css_contract(fact(css_won(field(node), field(prop), field(winner)), table(cval), field(winner) \== none)).
%% a computed colour as a number, RGBA (alpha in the low byte): what contrast is reckoned from
css_contract(fact(css_rgba(field(node), field(prop), field(color)), table(cval), field(unit) == color)).

css_contract(rules('rules/css')).
css_contract(derived(css_origin_of/2, 'the origin of a declaration')).
css_contract(derived(css_lost/4, 'a matching declaration that lost the cascade for a property, and at which step')).
css_contract(derived(css_explain/3, 'why an element has its computed value of a property')).
css_contract(derived(css_specificity/2, 'a specificity as Selectors writes it, spec(A, B, C)')).
css_contract(derived(css_background_of/3, 'the background an element''s text is drawn on, and whose it is')).
css_contract(derived(css_rendered/1, 'an element that is drawn: no display none on it or above it')).
css_contract(derived(css_contrast/2, 'the WCAG 2 contrast ratio of an element''s text with its background')).

css_contract(query(colours, (dom_element(N, E), css_computed(N, color, C, H)), [E, C, H], 'every element''s colour and where it came from')).
css_contract(query(overridden, (dom_element(N, E), css_lost(N, P, D, Why)), [E, P, D, Why], 'every declaration that lost, and why')).

css_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
css_contract(test(exec, 'tests/css')).
