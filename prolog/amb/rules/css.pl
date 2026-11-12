%% prolog/amb/rules/css.pl -- what the CSS module's facts mean (contracts/css.pl).
%%
%% The cascade's order, as CSS Cascade 5 has it for the origins the module
%% knows: origin and importance first (normal user agent < normal author <
%% important author < important user agent), the style attribute's declarations
%% above the rules' within each (element-attached style), then the cascade layer
%% (@layer: in the layers' order, the unlayered rules last; for !important the
%% order reversed), then specificity, then the order of appearance. The C side (src/css/css.c, css_rank) sorts the same
%% way; css_lost/4 says, of each declaration that lost, at which of these steps.
%%
%% No clause here cuts: amb_why/2 proves these rules clause by clause, and an
%% if-then-else says what a cut would.
:- dynamic css_sheet/2, css_rule/5, css_decl/5, css_matched/2, css_style_of/2, css_style_declared/4, css_style_won/3,
   css_style_rgba/3, css_initial/2, css_initial_rgba/2, css_style_custom/5, css_decl_custom/2, css_rule_layer/2, css_layer/3,
   css_imported/3, css_font_face/5, css_font_face_font/2,
   css_pseudo_matched/3, css_pseudo_style_of/3, css_state/2.

%% STYLES. Elements whose matched rules are the same, under parents of one style,
%% share a computed style (src/css/css.c): the snapshot says each element's style,
%% and each style's values once
css_declared(N, P, V, How) :- css_style_of(N, S), css_style_declared(S, P, V, How).
css_won(N, P, D) :- css_style_of(N, S), css_style_won(S, P, D).
css_declared_rgba(N, P, C) :- css_style_of(N, S), css_style_rgba(S, P, C).

%% COMPUTED VALUES. The snapshot carries the values a declaration gave
%% (css_declared/4, css_declared_rgba/3) and each property's initial value
%% (css_initial/2, css_initial_rgba/2). Every other value is what the cascade made
%% it (src/css/css.c, css_element): an inherited property's is the parent
%% element's, and any other's the initial value: a border's colour and the
%% outline's being the element's own colour, and a border's width 0 when its
%% style is none or hidden. So css_computed/4 and css_rgba/3 answer as they did when
%% every value was a fact, from a snapshot with a sixth as many (PLAN.md 11).
%% The elements are those the cascade styled (css_style_of/2): a template's
%% contents are no part of the document's tree, and have no style.
css_computed(N, P, V, How) :-
    css_declared(N, P, V, How).
css_computed(N, P, V, How) :-
    css_style_of(N, _), css_initial(P, _), \+ css_declared(N, P, _, _),
    css_rules_default(N, P, V, How).

css_rules_default(N, P, V, How) :-
    (   css_rules_inherited(P), dom_parent(N, Par), css_style_of(Par, _)
    ->  css_computed(Par, P, V, _), How = inherited
    ;   css_rules_currentcolor(P)
    ->  css_computed(N, color, V, _), How = initial
    ;   css_rules_border_style(P, S), css_computed(N, S, Style, _), ( Style == none ; Style == hidden )
    ->  V = '0px', How = initial
    ;   css_initial(P, V), How = initial
    ).

css_rgba(N, P, C) :-
    css_declared_rgba(N, P, C).
css_rgba(N, P, C) :-
    css_rules_colour(P), css_style_of(N, _), \+ css_declared(N, P, _, _),
    css_rules_default_rgba(N, P, C).

css_rules_default_rgba(N, P, C) :-
    (   css_rules_inherited(P), dom_parent(N, Par), css_style_of(Par, _)
    ->  css_rgba(Par, P, C)
    ;   css_rules_currentcolor(P)
    ->  css_rgba(N, color, C)
    ;   css_initial_rgba(P, C)
    ).

%% the properties inherited (of those the module computes: src/css/css.c's table), the
%% colours, those whose initial value is currentcolor, and each border width's style
css_rules_inherited(color).
css_rules_inherited(font_size).
css_rules_inherited(font_weight).
css_rules_inherited(font_style).
css_rules_inherited(text_align).
css_rules_inherited(line_height).
css_rules_inherited(white_space).
css_rules_inherited(list_style_type).
css_rules_inherited(list_style_position).
css_rules_inherited(font_family).
css_rules_inherited(visibility).
css_rules_inherited(text_shadow).
css_rules_inherited(letter_spacing).
css_rules_inherited(word_spacing).
css_rules_inherited(text_indent).
css_rules_inherited(text_transform).
css_rules_inherited(direction).
css_rules_inherited(writing_mode).
css_rules_inherited(hyphens).
css_rules_inherited(quotes).
css_rules_colour(color).
css_rules_colour(background_color).
css_rules_colour(P) :- css_rules_currentcolor(P).
css_rules_currentcolor(border_top_color).
css_rules_currentcolor(border_right_color).
css_rules_currentcolor(border_bottom_color).
css_rules_currentcolor(border_left_color).
css_rules_currentcolor(outline_color).
css_rules_border_style(border_top_width, border_top_style).
css_rules_border_style(border_right_width, border_right_style).
css_rules_border_style(border_bottom_width, border_bottom_style).
css_rules_border_style(border_left_width, border_left_style).

%% CUSTOM PROPERTIES. An element's are its style's own (css_style_custom/5, var()
%% replaced), and its parent's of every other name: a custom property is inherited
css_custom(N, Name, V) :-
    css_style_of(N, S), css_style_custom(S, Name, V, How, _), How \== initial.
css_custom(N, Name, V) :-
    dom_parent(N, P), dom_element(P, _), css_custom(P, Name, V),
    \+ ( css_style_of(N, S), css_style_custom(S, Name, _, _, _) ).

%% PSEUDO-ELEMENTS. A pseudo-element's style (css_pseudo_style_of/3: a ::before or an
%% ::after whose content is something, and the others that rules matched) is a style
%% like an element's, under its element's: a value a declaration gave, or else its
%% element's (an inherited property's), the element's own colour (currentcolor's), a
%% border's width 0 under the style none, or the initial value
css_pseudo_computed(N, Ps, P, V, How) :-
    css_pseudo_style_of(N, Ps, S), css_initial(P, _),
    (   css_style_declared(S, P, V0, How0)
    ->  V = V0, How = How0
    ;   css_rules_inherited(P)
    ->  css_computed(N, P, V, _), How = inherited
    ;   css_rules_currentcolor(P)
    ->  css_pseudo_computed(N, Ps, color, V, _), How = initial
    ;   css_rules_border_style(P, Sp), css_pseudo_computed(N, Ps, Sp, Style, _), ( Style == none ; Style == hidden )
    ->  V = '0px', How = initial
    ;   css_initial(P, V), How = initial
    ).

css_origin_of(D, O) :- css_decl(D, R, _, _, _), css_rule(R, S, _, _, _), css_sheet(S, O).

%% a declaration's place in the cascade: a term that sorts as the cascade does
css_rules_rank(D, rank(Layer, LayerRank, Spec, Order)) :-
    css_decl(D, R, _, _, Imp), css_rule(R, _, _, Spec, Order), css_origin_of(D, O),
    css_rules_layer(O, Imp, Layer), css_rules_layer_rank(R, Imp, LayerRank).

%% a rule's cascade layer's place: its layer's rank, the unlayered after every layer
%% (8191, as src/css/css.c has it); for !important, the order reversed
css_rules_layer_rank(R, Imp, LR) :-
    (   css_rule_layer(R, L), css_layer(L, _, Rank) -> Base = Rank ; Base = 8191 ),
    (   Imp == true -> LR is 8191 - Base ; LR = Base ).

%% the cascade layer a declaration is in: its name, or unlayered
css_layer_of(D, Name) :-
    css_decl(D, R, _, _, _),
    (   css_rule_layer(R, L), css_layer(L, N, _) -> Name = N ; Name = unlayered ).

css_rules_layer(user_agent, false, 1).
css_rules_layer(author, false, 2).
css_rules_layer(inline, false, 3).
css_rules_layer(author, true, 4).
css_rules_layer(inline, true, 5).
css_rules_layer(user_agent, true, 6).

css_rules_layer_name(1, 'user agent').
css_rules_layer_name(2, author).
css_rules_layer_name(3, 'style attribute').
css_rules_layer_name(4, 'important author').
css_rules_layer_name(5, 'important style attribute').
css_rules_layer_name(6, 'important user agent').

%% a specificity, a * 65536 + b * 256 + c, as Selectors writes it
css_specificity(S, spec(A, B, C)) :- A is S >> 16, B is (S >> 8) /\ 255, C is S /\ 255.

%% a declaration of an element's matching rules that lost the property, and why:
%% the step of the cascade at which the winner was ahead
css_lost(N, P, D2, Why) :-
    css_won(N, P, D),
    css_matched(N, R2), css_decl(D2, R2, P, _, _), D2 \== D,
    css_rules_rank(D, rank(L1, LR1, S1, O1)), css_rules_rank(D2, rank(L2, LR2, S2, O2)),
    (   L2 < L1 -> css_rules_layer_name(L2, W2), css_rules_layer_name(L1, W1), Why = origin_or_importance(lost(W2), won(W1))
    ;   LR2 < LR1 -> css_layer_of(D2, N2), css_layer_of(D, N1), Why = layer(lost(N2), won(N1))
    ;   S2 < S1 -> css_specificity(S2, T2), css_specificity(S1, T1), Why = specificity(T2, lower_than(T1))
    ;   O2 < O1 -> Why = order(O2, earlier_than(O1))
    ;   Why = earlier_in_the_same_rule
    ).

%% why an element has its value of a property: the declaration that won (as
%% written, its rule and where that rule is), or the parent it inherited from, or
%% the property's initial value; for the last two, the declaration that asked
%% for it (inherit, initial, unset) if one did
css_explain(N, P, declared(Value, written(Text), by(Selector), in(Where))) :-
    css_computed(N, P, Value, declared), css_won(N, P, D),
    css_decl(D, R, _, Text, Imp), css_rule(R, _, Selector, _, _), css_origin_of(D, Origin),
    (   Imp == true -> Where = important(Origin) ; Where = Origin ).
css_explain(N, P, inherited(Value, from(Parent), Because)) :-
    css_computed(N, P, Value, inherited), dom_parent(N, Parent),
    css_rules_because(N, P, 'the property is inherited', Because).
css_explain(N, P, initial(Value, Because)) :-
    css_computed(N, P, Value, initial),
    css_rules_because(N, P, 'no declaration applies', Because).

css_rules_because(N, P, Otherwise, Because) :-
    (   css_won(N, P, D)
    ->  css_decl(D, R, _, Text, _), css_rule(R, _, Selector, _, _), Because = by(Selector, written(Text))
    ;   Because = Otherwise
    ).

%% the background an element's text is drawn on: its own, or the nearest
%% ancestor's that is not transparent, or the canvas's -- the viewport's
%% background, the Canvas of the root's color scheme; white where layout did not run
css_background_of(N, C, From) :-
    (   css_rgba(N, background_color, C0), C0 /\ 255 =\= 0
    ->  C = C0, From = N
    ;   dom_parent(N, P), dom_element(P, _)
    ->  css_background_of(P, C, From)
    ;   layout_box(V, _, viewport, _, _, _, _), layout_background(V, C1)
    ->  C = C1, From = canvas
    ;   C = 0xffffffff, From = canvas
    ).

%% an element that is drawn: neither it nor an ancestor has display none
css_rendered(N) :-
    css_computed(N, display, D, _), D \== none,
    (   dom_parent(N, P), dom_element(P, _) -> css_rendered(P) ; true ).

%% WCAG 2's contrast ratio of an element's text with its background
css_contrast(N, Ratio) :-
    css_rgba(N, color, Fg), css_background_of(N, Bg, _),
    css_rules_luminance(Fg, L1), css_rules_luminance(Bg, L2),
    Hi is max(L1, L2), Lo is min(L1, L2),
    Ratio is (Hi + 0.05) / (Lo + 0.05).

css_rules_luminance(C, L) :-
    R is (C >> 24) /\ 255, G is (C >> 16) /\ 255, B is (C >> 8) /\ 255,
    css_rules_linear(R, LR), css_rules_linear(G, LG), css_rules_linear(B, LB),
    L is 0.2126 * LR + 0.7152 * LG + 0.0722 * LB.
css_rules_linear(V, L) :-
    S is V / 255,
    (   S =< 0.04045 -> L is S / 12.92 ; L is ((S + 0.055) / 1.055) ** 2.4 ).

%% the contrast an element's text needs: 3:1 for large text (24px, or 18.66px
%% bold), 4.5:1 for the rest (WCAG 2, 1.4.3)
css_rules_needed(N, Min) :-
    css_computed(N, font_size, FS, _), atom_concat(X, px, FS), atom_number(X, Size),
    css_computed(N, font_weight, FW, _), atom_number(FW, Weight),
    (   ( Size >= 24 ; Size >= 18.66, Weight >= 700 ) -> Min = 3 ; Min = 4.5 ).

%% an element with text of its own that shows: a child text that is not all white space
css_rules_has_text(N) :-
    once(( dom_parent(T, N), dom_text(T, S), atom_codes(S, Cs), member(C, Cs), C > 32 )).

%% ---- what the reasoner says of a page's style ------------------------------------

%% (the cheap tests first: a ratio of 4.5 or more is below no threshold, so most
%% elements stop at it before their rendering and their text's size are asked)
css_diag(low_contrast, N, Text) :-
    dom_element(N, E), css_rules_has_text(N),
    css_contrast(N, Ratio), Ratio < 4.5, css_rendered(N), css_rules_needed(N, Min), Ratio < Min,
    css_computed(N, color, Fg, _), css_background_of(N, _, From),
    (   From == canvas -> Bg = 'the white canvas' ; css_computed(From, background_color, Bg, _) ),
    format(atom(Text), '<~w> text ~w on ~w: contrast ~2f:1, below ~w:1', [E, Fg, Bg, Ratio, Min]).
css_diag(unused_rule, R, Text) :-
    css_rule(R, S, Selector, _, _), css_sheet(S, author), \+ css_matched(_, R), \+ css_pseudo_matched(_, _, R),
    (   css_state(_, _) -> State = 'in the state the page was put in' ; State = 'at rest: no hover, focus or other state' ),
    format(atom(Text), 'the rule ~w matches no element (~w)', [Selector, State]).
