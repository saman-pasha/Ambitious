%% prolog/amb/rules/layout.pl -- what the layout module's facts mean (contracts/layout.pl).
%%
%% A box's width is explained by how layout found it (layout_width/4): filling
%% its containing block less its margins, borders and paddings; a length or a
%% percentage its style declared -- and then the CSS module says which rule
%% declared it (css_explain/3); shrunk to fit its content; its table column's.
%% No clause here cuts: amb_why/2 proves these rules clause by clause.
:- dynamic layout_box/7, layout_parent/2, layout_margin/5, layout_border/5, layout_padding/5, layout_width/4,
           layout_line/6, layout_text/8, layout_frag/8.

%% the box an element generated
layout_box_of(N, B) :- layout_box(B, N, K, _, _, _, _), N \== none, K \== viewport.

%% a box's border box
layout_border_box(B, X, Y, W, H) :-
    layout_box(B, _, _, CX, CY, CW, CH), layout_border(B, BT, BR, BB, BL), layout_padding(B, PT, PR, PB, PL),
    X is CX - PL - BL, Y is CY - PT - BT, W is CW + PL + PR + BL + BR, H is CH + PT + PB + BT + BB.

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
