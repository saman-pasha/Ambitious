%% prolog/amb/rules/css.pl -- what the CSS module's facts mean (contracts/css.pl).
%%
%% The cascade's order, as CSS Cascade 5 has it for the origins the module
%% knows: origin and importance first (normal user agent < normal author <
%% animations < important author < important user agent), then the encapsulation
%% context (a shadow tree's rules against its host's tree's, by their sheets' depths,
%% css_sheet_tree/3: the outer winning for normal declarations, the inner for important
%% ones), the style attribute's
%% declarations above the rules' within each (element-attached style), then the cascade layer
%% (@layer: in the layers' order, the unlayered rules last; for !important the
%% order reversed), then specificity, then scope proximity (@scope: a nearer scoping
%% root over a farther, a scoped rule over an unscoped one), then the order of appearance. The C side (src/css/css.c, css_rank) sorts the same
%% way; css_lost/4 says, of each declaration that lost, at which of these steps.
%%
%% No clause here cuts: amb_why/2 proves these rules clause by clause, and an
%% if-then-else says what a cut would.
:- dynamic css_sheet/2, css_sheet_tree/3, css_rule/5, css_decl/5, css_matched/2, css_style_of/2, css_style_declared/4, css_style_won/3,
   css_style_rgba/3, css_initial/2, css_initial_rgba/2, css_style_custom/5, css_decl_custom/2, css_rule_layer/2, css_layer/3,
   css_imported/3, css_font_face/5, css_font_face_font/2,
   css_pseudo_matched/3, css_pseudo_style_of/3, css_state/2, css_keyframes/3, css_keyframe/3, css_registered/5,
   css_scope/3, css_match_proximity/3, css_container_unmet/2, css_container_query/3, css_rule_container/2, css_counter_style/3.

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
%% every value was a fact, from a snapshot with a sixth as many (PLAN.md 11). A mask's
%% longhand left at its initial value under a mask-image of more layers than one is that
%% value an item for each layer, as Chromium computes it (mask-size: auto, auto).
%% The elements are those the cascade styled (css_style_of/2): a template's
%% contents are no part of the document's tree, and have no style, and neither has a
%% node of no flat tree (a host's child no slot takes). An element inherits from its
%% parent in the flat tree (dom_flat_parent/2): a slotted one from its slot, a shadow
%% root's from its host.
css_computed(N, P, V, How) :-
    css_declared(N, P, V, How).
css_computed(N, P, V, How) :-
    css_style_of(N, _), css_initial(P, _), \+ css_declared(N, P, _, _),
    css_rules_default(N, P, V, How).

css_rules_default(N, P, V, How) :-
    (   css_rules_inherited(P), dom_flat_parent(N, Par), css_style_of(Par, _)
    ->  css_computed(Par, P, V, _), How = inherited
    ;   css_rules_currentcolor(P)
    ->  css_computed(N, color, V, _), How = initial
    ;   css_rules_border_style(P, S), css_computed(N, S, Style, _), ( Style == none ; Style == hidden )
    ->  V = '0px', How = initial
    ;   css_rules_mask_list(P), css_declared(N, mask_image, I, _), css_rules_layers(I, K), K > 1
    ->  css_initial(P, V0), css_rules_items(V0, K, V), How = initial
    ;   css_initial(P, V), How = initial
    ).

css_rgba(N, P, C) :-
    css_declared_rgba(N, P, C).
css_rgba(N, P, C) :-
    css_rules_colour(P), css_style_of(N, _), \+ css_declared(N, P, _, _),
    css_rules_default_rgba(N, P, C).

css_rules_default_rgba(N, P, C) :-
    (   css_rules_inherited(P), dom_flat_parent(N, Par), css_style_of(Par, _)
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
css_rules_inherited(fill).
css_rules_inherited(fill_opacity).
css_rules_inherited(fill_rule).
css_rules_inherited(stroke).
css_rules_inherited(stroke_width).
css_rules_inherited(stroke_linecap).
css_rules_inherited(stroke_linejoin).
css_rules_inherited(stroke_miterlimit).
css_rules_inherited(stroke_dasharray).
css_rules_inherited(stroke_dashoffset).
css_rules_inherited(stroke_opacity).
css_rules_inherited(clip_rule).
css_rules_inherited(text_anchor).
css_rules_inherited(marker_start).
css_rules_inherited(marker_mid).
css_rules_inherited(marker_end).
css_rules_inherited(paint_order).
css_rules_inherited(dominant_baseline).
css_rules_inherited(color_interpolation_filters).
css_rules_inherited(color_interpolation).
css_rules_inherited(image_rendering).
css_rules_colour(color).
css_rules_colour(background_color).
css_rules_colour(stop_color).
css_rules_colour(flood_color).
css_rules_colour(lighting_color).
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
css_rules_mask_list(mask_mode).
css_rules_mask_list(mask_repeat).
css_rules_mask_list(mask_position).
css_rules_mask_list(mask_size).
css_rules_mask_list(mask_origin).
css_rules_mask_list(mask_clip).
css_rules_mask_list(mask_composite).

%% a list's items (its commas outside brackets and quotes, a backslash's character passed
%% over), and an item K times
css_rules_layers(I, K) :- atom_codes(I, Cs), css_rules_commas(Cs, 0, 0, 1, K).
css_rules_commas([], _, _, K, K).
css_rules_commas([92, _|Cs], D, Q, K0, K) :- !, css_rules_commas(Cs, D, Q, K0, K).
css_rules_commas([C|Cs], D, Q, K0, K) :- Q =\= 0, !, ( C =:= Q -> Q1 = 0 ; Q1 = Q ), css_rules_commas(Cs, D, Q1, K0, K).
css_rules_commas([C|Cs], D, _, K0, K) :- ( C =:= 34 ; C =:= 39 ), !, css_rules_commas(Cs, D, C, K0, K).
css_rules_commas([40|Cs], D, _, K0, K) :- !, D1 is D + 1, css_rules_commas(Cs, D1, 0, K0, K).
css_rules_commas([41|Cs], D, _, K0, K) :- !, D1 is D - 1, css_rules_commas(Cs, D1, 0, K0, K).
css_rules_commas([44|Cs], 0, _, K0, K) :- !, K1 is K0 + 1, css_rules_commas(Cs, 0, 0, K1, K).
css_rules_commas([_|Cs], D, Q, K0, K) :- css_rules_commas(Cs, D, Q, K0, K).
css_rules_items(V, 1, V) :- !.
css_rules_items(V0, K, V) :- K1 is K - 1, css_rules_items(V0, K1, V1), atomic_list_concat([V0, ', ', V1], V).

%% CUSTOM PROPERTIES. An element's are its style's own (css_style_custom/5, var()
%% replaced; an unregistered one's initial is no value), and its parent's of every other
%% name: a custom property is inherited, but one registered (@property) not to inherit,
%% which has its initial value where the element gives it none, as one registered has at
%% the root (CSS Properties and Values 1)
css_custom(N, Name, V) :-
    css_style_of(N, S), css_style_custom(S, Name, V, How, _), ( How == initial -> V \== '' ; true ).
css_custom(N, Name, V) :-
    dom_flat_parent(N, P), dom_element(P, _), css_custom(P, Name, V),
    \+ ( css_style_of(N, S), css_style_custom(S, Name, _, _, _) ),
    \+ css_rules_registered(Name, _, false, _).
css_custom(N, Name, V) :-
    css_style_of(N, S), css_rules_registered(Name, _, Inherits, V),
    \+ css_style_custom(S, Name, _, _, _),
    (   Inherits == true -> \+ ( dom_flat_parent(N, P), dom_element(P, _) ) ; true ).

%% a custom property's registration that holds: the last of its name's
css_rules_registered(Name, Syntax, Inherits, Initial) :-
    css_registered(R, Name, Syntax, Inherits, Initial),
    \+ ( css_registered(R2, Name, _, _, _), R2 @> R ).

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
    (   css_keyframe(R, _, _) -> Layer = 4 ; css_rules_layer(O, Imp, Layer) ),
    css_rules_layer_rank(R, Imp, LayerRank).

%% a declaration's encapsulation context (Cascade 5, 6.1): its origin and importance
%% without the style attribute's place (the context is ranked between them), and its
%% sheet's depth, the outer winning a normal declaration's, the inner an important one's
css_rules_context(D, ctx(OI, K, Depth)) :-
    css_decl(D, R, _, _, Imp), css_rule(R, S, _, _, _), css_origin_of(D, O),
    (   css_keyframe(R, _, _) -> OI = 3 ; css_rules_oi(O, Imp, OI) ),
    (   css_sheet_tree(S, _, Depth0) -> Depth = Depth0 ; Depth = 0 ),
    (   Imp == true -> K = Depth ; K is 0 - Depth ).
css_rules_oi(user_agent, false, 1).
css_rules_oi(author, false, 2).
css_rules_oi(inline, false, 2).
css_rules_oi(author, true, 4).
css_rules_oi(inline, true, 4).
css_rules_oi(user_agent, true, 5).
css_rules_context_name(D, Name) :- ( D =:= 0 -> Name = document ; Name = shadow_tree(depth(D)) ).

%% a rule's cascade layer's place: its layer's rank, the unlayered after every layer
%% (8191, as src/css/css.c has it); for !important, the order reversed
css_rules_layer_rank(R, Imp, LR) :-
    (   css_rule_layer(R, L), css_layer(L, _, Rank) -> Base = Rank ; Base = 8191 ),
    (   Imp == true -> LR is 8191 - Base ; LR = Base ).

%% the cascade layer a declaration is in: its name, or unlayered
css_layer_of(D, Name) :-
    css_decl(D, R, _, _, _),
    (   css_rule_layer(R, L), css_layer(L, N, _) -> Name = N ; Name = unlayered ).

%% (an animation's keyframe is 4: animations above every normal declaration and
%% under the important ones)
css_rules_layer(user_agent, false, 1).
css_rules_layer(author, false, 2).
css_rules_layer(inline, false, 3).
css_rules_layer(author, true, 5).
css_rules_layer(inline, true, 6).
css_rules_layer(user_agent, true, 7).

css_rules_layer_name(1, 'user agent').
css_rules_layer_name(2, author).
css_rules_layer_name(3, 'style attribute').
css_rules_layer_name(4, animation).
css_rules_layer_name(5, 'important author').
css_rules_layer_name(6, 'important style attribute').
css_rules_layer_name(7, 'important user agent').

%% a specificity, a * 65536 + b * 256 + c, as Selectors writes it
css_specificity(S, spec(A, B, C)) :- A is S >> 16, B is (S >> 8) /\ 255, C is S /\ 255.

%% a declaration of an element's matching rules that lost the property, and why:
%% the step of the cascade at which the winner was ahead
css_lost(N, P, D2, Why) :-
    css_won(N, P, D),
    css_matched(N, R2), css_decl(D2, R2, P, _, _), D2 \== D,
    css_rules_rank(D, rank(L1, LR1, S1, O1)), css_rules_rank(D2, rank(L2, LR2, S2, O2)),
    css_rules_context(D, ctx(OI1, K1, De1)), css_rules_context(D2, ctx(OI2, K2, De2)),
    css_decl(D, R1, _, _, _), css_rules_nearness(N, R1, X1), css_rules_nearness(N, R2, X2),
    (   OI2 =:= OI1, K2 < K1 -> css_rules_context_name(De2, C2), css_rules_context_name(De1, C1), Why = context(lost(C2), won(C1))
    ;   L2 < L1 -> css_rules_layer_name(L2, W2), css_rules_layer_name(L1, W1), Why = origin_or_importance(lost(W2), won(W1))
    ;   LR2 < LR1 -> css_layer_of(D2, N2), css_layer_of(D, N1), Why = layer(lost(N2), won(N1))
    ;   S2 < S1 -> css_specificity(S2, T2), css_specificity(S1, T1), Why = specificity(T2, lower_than(T1))
    ;   X2 < X1 -> css_rules_scope_name(X2, W2), css_rules_scope_name(X1, W1), Why = scope_proximity(lost(W2), won(W1))
    ;   O2 < O1 -> Why = order(O2, earlier_than(O1))
    ;   Why = earlier_in_the_same_rule
    ).

%% a matched rule's nearness to an element by its @scope (Cascade 6, 6.1), after
%% specificity: 256 less its scoping root's proximity (generations above the element,
%% plus one) for a scoped rule, 0 for an unscoped one; and as said
css_rules_nearness(N, R, X) :- ( css_match_proximity(N, R, P) -> X is 256 - P ; X = 0 ).
css_rules_scope_name(X, W) :- ( X =:= 0 -> W = unscoped ; G is 255 - X, W = scoped(generations(G)) ).

%% why an element has its value of a property: the declaration that won (as
%% written, its rule and where that rule is), or the animation that gave it at
%% the page's rest time (its @keyframes and the keyframe's place), or the parent it
%% inherited from, or the property's initial value; for the last two, the
%% declaration that asked for it (inherit, initial, unset) if one did
css_explain(N, P, declared(Value, written(Text), by(Selector), in(Where))) :-
    css_computed(N, P, Value, declared), css_won(N, P, D),
    css_decl(D, R, _, Text, Imp), css_rule(R, _, Selector, _, _), css_origin_of(D, Origin),
    (   Imp == true -> Where = important(Origin) ; Where = Origin ).
css_explain(N, P, animated(Value, by(Name, at(Offset)), written(Text))) :-
    css_computed(N, P, Value, animated), css_won(N, P, D),
    css_decl(D, R, _, Text, _), css_keyframe(R, K, Offset), css_keyframes(K, Name, _).
css_explain(N, P, inherited(Value, from(Parent), Because)) :-
    css_computed(N, P, Value, inherited), dom_flat_parent(N, Parent),
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
    ;   dom_flat_parent(N, P), dom_element(P, _)
    ->  css_background_of(P, C, From)
    ;   layout_box(V, _, viewport, _, _, _, _), layout_background(V, C1)
    ->  C = C1, From = canvas
    ;   C = 0xffffffff, From = canvas
    ).

%% an element that is drawn: neither it nor an ancestor has display none
css_rendered(N) :-
    css_computed(N, display, D, _), D \== none,
    (   dom_flat_parent(N, P), dom_element(P, _) -> css_rendered(P) ; true ).

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
    once(( dom_flat_child(N, T), dom_text(T, S), atom_codes(S, Cs), member(C, Cs), C > 32 )).

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
    css_rule(R, S, Selector, _, _), css_sheet(S, author), \+ css_keyframe(R, _, _), \+ css_matched(_, R), \+ css_pseudo_matched(_, _, R),
    \+ css_container_unmet(_, R),
    (   css_state(_, _) -> State = 'in the state the page was put in' ; State = 'at rest: no hover, focus or other state' ),
    format(atom(Text), 'the rule ~w matches no element (~w)', [Selector, State]).
%% a rule in an @container whose selector matches but whose condition holds for none of the elements it matches
css_diag(container_unmet, R, Text) :-
    css_rule(R, _, Selector, _, _), css_rule_container(R, Q), css_container_unmet(_, R), \+ css_matched(_, R), \+ css_pseudo_matched(_, _, R),
    css_container_query(Q, Query, _),
    format(atom(Text), 'the rule ~w matches, but its @container ~w holds for none of the elements it matches', [Selector, Query]).
