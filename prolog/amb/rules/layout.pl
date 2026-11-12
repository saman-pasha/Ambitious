%% prolog/amb/rules/layout.pl -- what the layout module's facts mean (contracts/layout.pl).
%%
%% A box's width is explained by how layout found it (layout_width/4): filling
%% its containing block less its margins, borders and paddings; a length or a
%% percentage its style declared -- and then the CSS module says which rule
%% declared it (css_explain/3); shrunk to fit its content; its table column's.
%% No clause here cuts: amb_why/2 proves these rules clause by clause.
:- dynamic layout_box/7, layout_parent/2, layout_margin/5, layout_border/5, layout_padding/5, layout_width/4,
           layout_line/6, layout_text/8, layout_frag/8, layout_pseudo/2, layout_generated/3, layout_positioned/5,
           layout_stacking/2, layout_float/2, layout_clips/1, layout_background/2.

%% the box an element generated (its pseudo-elements' are theirs)
layout_box_of(N, B) :- layout_box(B, N, K, _, _, _, _), N \== none, K \== viewport, \+ layout_pseudo(B, _).

%% the box an element's ::before (1) or ::after (2) generated
layout_pseudo_box_of(N, P, B) :-
    layout_pseudo(B, K), layout_box(B, N, _, _, _, _, _),
    (   K =:= 1 -> P = before ; P = after ).

%% an inline element's box, which is no box of the tree's: the fragments it makes (its
%% background's, its generated content's) and its text's, together
layout_inline_box(N, X, Y, W, H) :-
    dom_element(N, _), \+ layout_box_of(N, _),
    findall(r(X0, Y0, X1, Y1),
            ( (   dom_parent(T, N), layout_text(_, _, T, _, X0, Y0, W0, H0)
              ;   layout_frag(_, _, K, N, X0, Y0, W0, H0), K \== atomic
              ),
              X1 is X0 + W0, Y1 is Y0 + H0 ), Rs),
    Rs \== [],
    aggregate_all(min(A), member(r(A, _, _, _), Rs), X), aggregate_all(min(B), member(r(_, B, _, _), Rs), Y),
    aggregate_all(max(C), member(r(_, _, C, _), Rs), XE), aggregate_all(max(D), member(r(_, _, _, D), Rs), YE),
    W is XE - X, H is YE - Y.

%% a box's border box
layout_border_box(B, X, Y, W, H) :-
    layout_box(B, _, _, CX, CY, CW, CH), layout_border(B, BT, BR, BB, BL), layout_padding(B, PT, PR, PB, PL),
    X is CX - PL - BL, Y is CY - PT - BT, W is CW + PL + PR + BL + BR, H is CH + PT + PB + BT + BB.

%% why a box is where it is: the flow put it there; it floats to a side; it is offset
%% from where the flow put it (relative), or placed in its containing block (absolute:
%% the nearest positioned ancestor's, or the viewport's) or the viewport (fixed); sticky
%% at rest, where the flow put it
layout_explain_position(B, Why) :-
    layout_box(B, _, K, _, _, _, _), K \== viewport,
    (   layout_positioned(B, P, CB, DX, DY)
    ->  layout_rules_position(P, CB, DX, DY, Why)
    ;   layout_float(B, Side)
    ->  Why = floated(Side)
    ;   Why = flow
    ).

layout_rules_position(relative, _, DX, DY, offset(DX, DY)).
layout_rules_position(sticky, _, _, _, sticky(at_rest)).
layout_rules_position(fixed, _, _, _, fixed(viewport)).
layout_rules_position(absolute, CB, _, _, absolute(in(E))) :- layout_rules_named(CB, E).

%% the order boxes are painted in (CSS 2.1 Appendix E, as paint has it): those with a
%% negative z-index first, then the flow in the tree's order, then the positioned ones
%% with z-index auto or 0, then those with a positive z-index -- a key that sorts so
layout_paint_order(B, key(Layer, Z, B)) :-
    layout_box(B, _, _, _, _, _, _),
    (   layout_stacking(B, Z0)
    ->  Z = Z0, ( Z0 < 0 -> Layer = 0 ; Z0 =:= 0 -> Layer = 2 ; Layer = 3 )
    ;   layout_positioned(B, _, _, _, _)
    ->  Z = 0, Layer = 2
    ;   Z = 0, Layer = 1
    ).

%% why a box is as wide as it is
layout_explain_width(B, Why) :-
    layout_width(B, W, How, Avail),
    layout_rules_width(How, B, W, Avail, Why).

layout_rules_width(viewport, _, W, _, viewport(W)).
layout_rules_width(fill, B, W, Avail, fills(W, of(Avail), less(margins(ML, MR), borders(BL, BR), paddings(PL, PR)))) :-
    layout_margin(B, _, MR, _, ML), layout_border(B, _, BR, _, BL), layout_padding(B, _, PR, _, PL).
layout_rules_width(declared, B, W, _, declared(W, Because)) :- layout_rules_css(B, width, Because).
layout_rules_width(percent, B, W, Avail, percent(W, of(Avail), Because)) :- layout_rules_css(B, width, Because).
layout_rules_width(shrink_to_fit, _, W, Avail, shrink_to_fit(W, within(Avail))).
layout_rules_width(column, _, W, Avail, column(W, of(Avail))).
layout_rules_width(intrinsic, _, W, _, intrinsic(W)).
layout_rules_width(max_width, B, W, Avail, capped(W, of(Avail), Because)) :- layout_rules_css(B, max_width, Because).
layout_rules_width(min_width, B, W, Avail, raised(W, of(Avail), Because)) :- layout_rules_css(B, min_width, Because).
layout_rules_width(flex, _, W, Avail, flexed(W, within(Avail))).
layout_rules_width(grid, _, W, Avail, grid_area(W, within(Avail))).
layout_rules_width(positioned, _, W, Avail, offsets(W, of(Avail))).

%% the CSS behind a declared width (or max-width): what the CSS module says of it
layout_rules_css(B, P, Because) :-
    layout_box(B, N, _, _, _, _, _),
    (   catch(css_explain(N, P, E), _, fail) -> Because = E ; Because = 'its element' ).

%% the deepest element whose border box holds the point (X, Y)
layout_at(X, Y, N) :-
    findall(D-N0, ( layout_box_of(N0, B), layout_border_box(B, BX, BY, BW, BH),
                    X >= BX, X < BX + BW, Y >= BY, Y < BY + BH, layout_rules_depth(B, D) ), L),
    L \== [], max_member(_-N, L).

layout_rules_depth(B, D) :- ( layout_parent(B, P) -> layout_rules_depth(P, D0), D is D0 + 1 ; D = 0 ).

%% a box whose border box goes past its container's content box on the right
layout_overflows(B, N, By) :-
    layout_box_of(N, B), layout_parent(B, P), layout_box(P, _, _, PX, _, PW, _),
    layout_border_box(B, X, _, W, _),
    By is X + W - (PX + PW), By > 0.5.

%% a box whose lines' text runs past its content box, and by how much at most
layout_text_overflow(B, By) :-
    layout_box(B, _, _, BX, _, BW, _),
    aggregate_all(max(E), ( layout_line(L, B, _, _, _, _), layout_text(_, L, _, _, X, _, W, _), E is X + W - (BX + BW) ), By),
    By > 0.5.

%% the element a box stands for: its own, or for an anonymous box its parent's
layout_rules_named(B, E) :-
    layout_box(B, N, _, _, _, _, _),
    (   N \== none -> dom_element(N, E)
    ;   layout_parent(B, P) -> layout_rules_named(P, E)
    ;   E = viewport
    ).

%% ---- what the reasoner says of a page's layout -----------------------------------

%% the outermost box that overflows: its own container does not
layout_diag(overflow, B, Text) :-
    layout_overflows(B, N, By), dom_element(N, E),
    layout_parent(B, P), \+ layout_overflows(P, _, _), layout_box(P, _, _, _, _, PW, _),
    format(atom(Text), '<~w> overflows its container by ~1fpx (the container is ~1fpx wide)', [E, By, PW]).
layout_diag(text_overflow, B, Text) :-
    layout_text_overflow(B, By), layout_box(B, _, _, _, _, BW, _), layout_rules_named(B, E),
    format(atom(Text), 'text in <~w> runs ~1fpx past its ~1fpx content box', [E, By, BW]).
