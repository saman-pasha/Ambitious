%% prolog/amb/rules/dom.pl -- what the DOM's facts mean.
%%
%% contracts/dom.pl declares every predicate defined here (derived/2) and the
%% facts they stand on (fact/3), which a snapshot of the live document gives.
%% The reasoner (prolog/amb/reason.pl) loads this file over a snapshot; nothing
%% here reads or changes the browser: a change is proposed by dom_advice/2 and
%% made, or refused, by the engine.
%%
%% Argument order: the subject first. dom_ancestor(A, D) is "A is an ancestor
%% of D", dom_descendant(D, A) "D is a descendant of A", dom_inside(N, A) "N is
%% in A's subtree". Helpers are dom_rules_*.
%%
%% An element is HTML's unless dom_namespace/2 names its namespace (svg,
%% mathml); the rules that know elements by their HTML names ask for HTML
%% elements. A template's contents are a fragment, a tree of their own: no
%% child of the template, so neither in the document's subtree nor in the
%% template's, and dom_in_template/2 says whose contents they are.

:- dynamic dom_node/2, dom_element/2, dom_text/2, dom_comment/2, dom_doctype/2,
           dom_parent/2, dom_next_sibling/2, dom_first_child/2, dom_attr/3,
           dom_namespace/2, dom_template_content/2, dom_attr_namespace/3, dom_mode/2,
           dom_shadow_root/3, dom_shadow_options/6, dom_assigned_slot/2, dom_manual_slot/2, dom_slot_first/2, dom_slot_next/2.

dom_root(N) :- dom_node(N, document).

%% the document's mode: quirks or limited_quirks when dom_mode/2 says so (the HTML
%% parser's, from its DOCTYPE), else no_quirks
dom_document_mode(D, M) :- dom_root(D), ( dom_mode(D, M0) -> M = M0 ; M = no_quirks ).

dom_html_element(N, E) :- dom_element(N, E), \+ dom_namespace(N, _).

%% N is in the contents of the template T: under its fragment, at any depth
dom_in_template(N, T) :- dom_template_content(T, F), dom_ancestor(F, N).

dom_child(P, C) :- dom_parent(C, P).

dom_children(P, Cs) :-
    (   dom_first_child(P, F) -> dom_rules_chain(F, Cs) ; Cs = [] ).

dom_rules_chain(N, [N|Ns]) :-
    (   dom_next_sibling(N, M) -> dom_rules_chain(M, Ns) ; Ns = [] ).

dom_ancestor(A, D) :- dom_parent(D, A).
dom_ancestor(A, D) :- dom_parent(D, P), dom_ancestor(A, P).

dom_descendant(D, A) :- dom_ancestor(A, D).

dom_inside(N, A) :- dom_node(A, _), N = A.
dom_inside(N, A) :- dom_ancestor(A, N).

%% THE FLAT TREE (DOM 4.2.2, CSS Scoping 1, 2.2), which the cascade, layout and
%% accessibility walk: a shadow host's children there are its shadow root's, a slot's
%% its assigned nodes in their order (dom_slot_first/2, dom_slot_next/2) or, with none
%% assigned it, its own; a host's child no slot takes, and a slot's own children while
%% nodes are assigned it, are in no flat tree. Without shadow roots it is the DOM's tree,
%% which is asked first (the inheritance of every value walks it)
dom_flat_parent(N, P) :-
    (   dom_shadow_root(_, _, _) -> dom_rules_flat_parent(N, P) ; dom_parent(N, P) ).

dom_rules_flat_parent(N, P) :- dom_assigned_slot(N, P).
dom_rules_flat_parent(N, P) :- dom_parent(N, Q), \+ dom_assigned_slot(N, _), dom_rules_flat_up(Q, P).

dom_rules_flat_up(Q, H) :- dom_shadow_root(H, Q, _).
dom_rules_flat_up(Q, Q) :- \+ dom_shadow_root(_, Q, _), \+ dom_shadow_root(Q, _, _), \+ dom_slot_first(Q, _).

dom_flat_child(P, C) :-
    (   dom_shadow_root(_, _, _) -> dom_rules_flat_child(P, C) ; dom_parent(C, P) ).

dom_rules_flat_child(P, C) :- dom_shadow_root(P, R, _), dom_parent(C, R).
dom_rules_flat_child(P, C) :- \+ dom_shadow_root(P, _, _), dom_slot_first(P, F), dom_rules_slot_member(F, C).
dom_rules_flat_child(P, C) :- \+ dom_shadow_root(P, _, _), \+ dom_slot_first(P, _), dom_parent(C, P).

dom_rules_slot_member(N, N).
dom_rules_slot_member(N, C) :- dom_slot_next(N, M), dom_rules_slot_member(M, C).

dom_flat_children(P, Cs) :-
    (   dom_shadow_root(P, R, _) -> dom_children(R, Cs)
    ;   dom_slot_first(P, F) -> dom_rules_slot_chain(F, Cs)
    ;   dom_children(P, Cs)
    ).
dom_rules_slot_chain(N, [N|Ns]) :-
    (   dom_slot_next(N, M) -> dom_rules_slot_chain(M, Ns) ; Ns = [] ).

%% N is in the shadow tree whose root is R (at any depth, a nested tree's not)
dom_shadow_tree_of(N, R) :- dom_shadow_root(_, R, _), dom_ancestor(R, N).

%% the parent chain from the root down to N, N last
dom_path(N, Path) :- dom_node(N, _), dom_rules_up(N, [N], Path).

dom_rules_up(N, Acc, Path) :-
    (   dom_parent(N, P) -> dom_rules_up(P, [P|Acc], Path) ; Path = Acc ).

%% the text of a subtree in document order
dom_text_content(N, Text) :-
    dom_node(N, _),
    dom_rules_texts(N, Ts, []),
    atomic_list_concat(Ts, Text).

dom_rules_texts(N, [T|R], R) :- dom_text(N, T), !.
dom_rules_texts(N, Ts, R) :- dom_children(N, Cs), dom_rules_texts_list(Cs, Ts, R).

dom_rules_texts_list([], R, R).
dom_rules_texts_list([C|Cs], Ts, R) :- dom_rules_texts(C, Ts, R1), dom_rules_texts_list(Cs, R1, R).

%% an element's accessible name, after the first of the sources that gives one
%% (a reduction of WAI-ARIA's accessible name computation, enough for buttons,
%% links, images and form controls; library(a11y) of Phase 9 replaces it)
dom_accessible_name(N, Name) :- dom_element(N, _), once(dom_rules_name(N, Name)).

dom_rules_name(N, Name) :- dom_attr(N, 'aria-label', L), dom_rules_squash(L, Name), Name \== ''.
dom_rules_name(N, Name) :- dom_html_element(N, img), dom_attr(N, alt, A), dom_rules_squash(A, Name).
dom_rules_name(N, Name) :-
    dom_html_element(N, input), dom_attr(N, type, T), memberchk(T, [submit, button, reset]),
    dom_attr(N, value, V), dom_rules_squash(V, Name), Name \== ''.
dom_rules_name(N, Name) :-
    dom_html_element(N, E), dom_rules_named_by_content(E),
    dom_text_content(N, T), dom_rules_squash(T, Name), Name \== ''.
dom_rules_name(N, Name) :- dom_attr(N, title, T), dom_rules_squash(T, Name), Name \== ''.

dom_rules_named_by_content(E) :-
    memberchk(E, [button, a, h1, h2, h3, h4, h5, h6, label, legend, caption, summary, option, th, td, li]).

%% whitespace runs to one space, none at the ends
dom_rules_squash(A, S) :-
    split_string(A, " \t\n\r\f", " \t\n\r\f", Parts),
    exclude(==(""), Parts, Words),
    atomic_list_concat(Words, ' ', S).

%% an HTML element that lacks an attribute it requires
dom_missing_attribute(N, A) :-
    dom_html_element(N, E), dom_rules_required(E, A), \+ dom_attr(N, A, _).

dom_rules_required(img, alt).
dom_rules_required(img, src).
dom_rules_required(area, alt).
dom_rules_required(iframe, title).
dom_rules_required(html, lang).

%% what removing N takes with it: its subtree, and the labels whose control goes
dom_removal_effect(N, removes(D)) :- dom_inside(D, N).
dom_removal_effect(N, breaks_label(L)) :-
    dom_inside(X, N), dom_attr(X, id, Id),
    dom_attr(L, for, Id), \+ dom_inside(L, N).

%% ---- diagnostics and advice -------------------------------------------------
%% dom_diag(Kind, Subject, Text): something to tell; dom_advice(Request, Reason):
%% an operation of the contract's requestable ones, with why. The reasoner prints
%% the first as amb_diag/3 and the second as amb_request/3 at the snapshot's
%% revision.

dom_diag(missing_attribute, N, Text) :-
    dom_missing_attribute(N, A), dom_element(N, E),
    format(atom(Text), '<~w> lacks the ~w attribute', [E, A]).
dom_diag(unnamed_control, N, Text) :-
    dom_html_element(N, E), memberchk(E, [button, a]), \+ dom_accessible_name(N, _),
    format(atom(Text), 'the <~w> has no accessible name', [E]).
dom_diag(empty_text, N, 'an empty text node') :- dom_text(N, '').

dom_advice(set_attribute(N, alt, Alt), Reason) :-
    dom_html_element(N, img), \+ dom_attr(N, alt, _),
    (   dom_attr(N, title, T), T \== ''
    ->  Alt = T, Reason = 'the image has no alt; its title says what it shows'
    ;   Alt = '', Reason = 'the image has no alt and nothing names it: mark it decorative'
    ).
dom_advice(remove_child(P, N), 'an empty text node carries nothing') :-
    dom_text(N, ''), dom_parent(N, P).
