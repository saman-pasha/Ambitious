%% prolog/amb/rules/a11y.pl -- what the accessibility module's facts mean (contracts/a11y.pl).
%%
%% A reader meets the page through its accessibility tree: each node's role says
%% what it is, its name what it is called. Some roles cannot be used without a
%% name (WAI-ARIA 1.2: a link, a button, an image, a form field, a heading); an
%% ID reference that names no element is lost; a heading more than one level
%% below the one before it leaves a hole in the outline; an interactive node
%% inside another cannot be told apart from it; an id on two elements makes every
%% reference to it ambiguous. No clause here cuts: amb_why/2 proves them clause
%% by clause.
:- dynamic a11y_node/5, a11y_parent/2, a11y_explicit_role/1, a11y_heading/2, a11y_focusable/1, a11y_ref/4, a11y_bad_role/2.

a11y_of(N, A) :- a11y_node(A, N, _, _, _).

%% the roles a reader cannot use without a name
a11y_rules_needs_name(link).
a11y_rules_needs_name(button).
a11y_rules_needs_name(img).
a11y_rules_needs_name(checkbox).
a11y_rules_needs_name(radio).
a11y_rules_needs_name(textbox).
a11y_rules_needs_name(combobox).
a11y_rules_needs_name(listbox).
a11y_rules_needs_name(slider).
a11y_rules_needs_name(heading).
a11y_rules_needs_name(menuitem).
a11y_rules_needs_name(tab).
a11y_rules_needs_name(dialog).

a11y_missing_name(A, Role) :- a11y_node(A, _, Role, '', _), a11y_rules_needs_name(Role).

a11y_broken_ref(N, Attr, Id) :- a11y_ref(N, Attr, Id, none).

%% the headings in document order (the tree is built in it), each with its level
a11y_outline(A, Level) :- a11y_heading(A, Level).

%% a heading more than one level below the heading before it
a11y_heading_jump(A, From, To) :-
    findall(H-L, a11y_heading(H, L), Hs),
    a11y_rules_next(Hs, A, From, To), To > From + 1.

a11y_rules_next([_-F, A-T|_], A, F, T).
a11y_rules_next([_|Rest], A, F, T) :- a11y_rules_next(Rest, A, F, T).

a11y_rules_interactive(link).
a11y_rules_interactive(button).
a11y_rules_interactive(checkbox).
a11y_rules_interactive(radio).
a11y_rules_interactive(textbox).
a11y_rules_interactive(combobox).
a11y_rules_interactive(listbox).
a11y_rules_interactive(slider).
a11y_rules_interactive(menuitem).
a11y_rules_interactive(tab).

a11y_rules_ancestor(P, A) :- a11y_parent(A, P).
a11y_rules_ancestor(P, A) :- a11y_parent(A, Q), a11y_rules_ancestor(P, Q).

%% an interactive node inside another
a11y_nested_interactive(Outer, Inner) :-
    a11y_node(Inner, _, R1, _, _), a11y_rules_interactive(R1),
    a11y_rules_ancestor(Outer, Inner), a11y_node(Outer, _, R2, _, _), a11y_rules_interactive(R2).

%% two elements with one id
a11y_duplicate_id(Id, N1, N2) :- dom_attr(N1, id, Id), dom_attr(N2, id, Id), N1 @< N2.

%% why a node has its name: where the name came from, and what it was
a11y_explain_name(A, Why) :- a11y_node(A, N, _, Name, From), a11y_rules_name_why(From, N, Name, Why).

a11y_rules_name_why(nothing, _, _, 'nothing names it').
a11y_rules_name_why(aria_labelledby, N, Name, labelled_by(Ids, Name)) :- findall(Id, a11y_ref(N, aria_labelledby, Id, _), Ids).
a11y_rules_name_why(aria_label, _, Name, aria_label(Name)).
a11y_rules_name_why(alt, _, Name, alt(Name)).
a11y_rules_name_why(label, N, Name, label(By, Name)) :-
    (   a11y_ref(L, label_for, _, N) -> By = L ; By = 'the label around it' ).
a11y_rules_name_why(caption, _, Name, caption(Name)).
a11y_rules_name_why(legend, _, Name, legend(Name)).
a11y_rules_name_why(content, _, Name, its_content(Name)).
a11y_rules_name_why(title, _, Name, title(Name)).
a11y_rules_name_why(placeholder, _, Name, placeholder(Name)).
a11y_rules_name_why(value, _, Name, value(Name)).

%% ---- what the reasoner says of a page's accessibility -----------------------------

a11y_diag(missing_name, A, Text) :-
    a11y_missing_name(A, Role), a11y_node(A, N, _, _, _), dom_element(N, E),
    format(atom(Text), 'the ~w <~w> has no accessible name, which its role needs', [Role, E]).
a11y_diag(broken_ref, N, Text) :-
    a11y_broken_ref(N, Attr, Id), dom_element(N, E),
    format(atom(Text), '<~w> ~w names id ~q, which no element has', [E, Attr, Id]).
a11y_diag(heading_jump, A, Text) :-
    a11y_heading_jump(A, F, T), a11y_node(A, _, _, Name, _), Skipped is T - F - 1,
    format(atom(Text), 'the level ~d heading ~q follows a level ~d one: ~d level(s) skipped', [T, Name, F, Skipped]).
a11y_diag(nested_interactive, Inner, Text) :-
    a11y_nested_interactive(Outer, Inner), a11y_node(Inner, _, R1, N1, _), a11y_node(Outer, _, R2, N2, _),
    format(atom(Text), 'the ~w ~q is inside the ~w ~q: a reader cannot tell them apart', [R1, N1, R2, N2]).
a11y_diag(duplicate_id, N2, Text) :-
    a11y_duplicate_id(Id, _, N2),
    format(atom(Text), 'id ~q is on two elements: a reference to it finds the first', [Id]).
a11y_diag(unknown_role, N, Text) :-
    a11y_bad_role(N, V), dom_element(N, E),
    format(atom(Text), '<~w role=~q>: no such role, so the element keeps its own', [E, V]).
