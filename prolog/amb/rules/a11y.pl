%% prolog/amb/rules/a11y.pl -- what the accessibility module's facts mean (contracts/a11y.pl).
%%
%% A reader meets the page through its accessibility tree: each node's role says
%% what it is, its name what it is called, its description what more there is to
%% know, its states what it is doing. Some roles cannot be used without a name
%% (WAI-ARIA 1.2: a link, a button, an image, a form field, a heading); an ID
%% reference that names no element is lost; a heading more than one level below
%% the one before it leaves a hole in the outline; an interactive node inside
%% another cannot be told apart from it; an id on two elements makes every
%% reference to it ambiguous. An aria-* attribute a role does not support, or a
%% value outside the attribute's values, is not passed on as the author meant; a
%% range widget without its value is read a default; what takes focus inside
%% aria-hidden content is reached by a keyboard but never announced; a live region
%% inside hidden content announces nothing; a position outside its set misleads.
%% No clause here cuts: amb_why/2 proves them clause by clause.
:- dynamic a11y_node/5, a11y_parent/2, a11y_explicit_role/1, a11y_heading/2, a11y_focusable/1, a11y_ref/4, a11y_bad_role/2,
   a11y_description/3, a11y_level/2, a11y_position/3, a11y_value/2, a11y_placeholder/2, a11y_valuenow/3, a11y_valuemin/2,
   a11y_valuemax/2, a11y_valuetext/2, a11y_live/4, a11y_live_region/2, a11y_state/3, a11y_state_from/4, a11y_hidden/2,
   a11y_hidden_focusable/2, a11y_hidden_live/2, a11y_no_node/2.

a11y_of(N, A) :- a11y_node(A, N, _, _, _).

%% the node of the element whose id is Id
a11y_by_id(Id, A) :- dom_attr(N, id, Id), a11y_node(A, N, _, _, _).

%% an element's role: its node's, or the role an element that is no node was kept with
a11y_role_of(N, Role) :- a11y_node(_, N, Role, _, _).
a11y_role_of(N, Role) :- a11y_no_node(N, Role).

%% the roles a reader cannot use without a name
a11y_rules_needs_name(link).
a11y_rules_needs_name(button).
a11y_rules_needs_name(img).
a11y_rules_needs_name(checkbox).
a11y_rules_needs_name(radio).
a11y_rules_needs_name(textbox).
a11y_rules_needs_name(searchbox).
a11y_rules_needs_name(combobox).
a11y_rules_needs_name(listbox).
a11y_rules_needs_name(slider).
a11y_rules_needs_name(spinbutton).
a11y_rules_needs_name(switch).
a11y_rules_needs_name(heading).
a11y_rules_needs_name(menuitem).
a11y_rules_needs_name(menuitemcheckbox).
a11y_rules_needs_name(menuitemradio).
a11y_rules_needs_name(tab).
a11y_rules_needs_name(treeitem).
a11y_rules_needs_name(tree).
a11y_rules_needs_name(treegrid).
a11y_rules_needs_name(radiogroup).
a11y_rules_needs_name(meter).
a11y_rules_needs_name(progressbar).
a11y_rules_needs_name(dialog).
a11y_rules_needs_name(alertdialog).

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
a11y_rules_name_why(label_attribute, _, Name, label_attribute(Name)).

%% why a node has its description
a11y_explain_description(A, Why) :- a11y_description(A, Text, From), a11y_node(A, N, _, _, _), a11y_rules_desc_why(From, N, Text, Why).

a11y_rules_desc_why(aria_describedby, N, Text, described_by(Ids, Text)) :- findall(Id, a11y_ref(N, aria_describedby, Id, _), Ids).
a11y_rules_desc_why(aria_description, _, Text, aria_description(Text)).
a11y_rules_desc_why(title, _, Text, title(Text)).
a11y_rules_desc_why(caption, _, Text, caption(Text)).

%% why a node has a state: its aria-* attribute, its HTML (an attribute, or a native state), an
%% ancestor it inherited the state from, its role's default, or the select's choice of a default option
a11y_explain_state(A, State, Why) :-
    a11y_state(A, State, V), a11y_state_from(A, State, From, By), a11y_rules_state_why(From, A, State, V, By, Why).

a11y_rules_state_why(aria, _, State, V, _, attribute(Attr, V)) :- atom_concat('aria-', State, Attr).
a11y_rules_state_why(html, _, State, V, _, html(Source, V)) :- a11y_rules_html_source(State, Source).
a11y_rules_state_why(inherited, _, _, V, By, inherited(V, By, E)) :- dom_element(By, E).
a11y_rules_state_why(implicit, A, _, V, _, role_default(Role, V)) :- a11y_node(A, _, Role, _, _).
a11y_rules_state_why(computed, _, _, V, _, chosen_by_default(V)).

%% the HTML a state comes from
a11y_rules_html_source(checked, 'the checked attribute').
a11y_rules_html_source(selected, 'the selected attribute').
a11y_rules_html_source(expanded, 'its details element''s open attribute').
a11y_rules_html_source(disabled, 'the disabled attribute').
a11y_rules_html_source(required, 'the required attribute').
a11y_rules_html_source(readonly, 'the readonly attribute').
a11y_rules_html_source(multiselectable, 'the multiple attribute').
a11y_rules_html_source(autocomplete, 'the list attribute').
a11y_rules_html_source(multiline, 'a textarea, or an editing host').

%% a node inside a live region, and the region's politeness
a11y_live_inside(A, Politeness) :- a11y_live_region(A, R), a11y_live(R, Politeness, _, _).

%% ---- WAI-ARIA 1.2's attributes and what each role supports ----------------------

%% each aria-* attribute, with the kind of its value
a11y_rules_attr('aria-activedescendant', idref).
a11y_rules_attr('aria-atomic', token([true, false])).
a11y_rules_attr('aria-autocomplete', token([inline, list, both, none])).
a11y_rules_attr('aria-braillelabel', text).
a11y_rules_attr('aria-brailleroledescription', text).
a11y_rules_attr('aria-busy', token([true, false])).
a11y_rules_attr('aria-checked', token([true, false, mixed, undefined])).
a11y_rules_attr('aria-colcount', integer(-1)).
a11y_rules_attr('aria-colindex', integer(1)).
a11y_rules_attr('aria-colindextext', text).
a11y_rules_attr('aria-colspan', integer(1)).
a11y_rules_attr('aria-controls', idrefs).
a11y_rules_attr('aria-current', token([page, step, location, date, time, true, false])).
a11y_rules_attr('aria-describedby', idrefs).
a11y_rules_attr('aria-description', text).
a11y_rules_attr('aria-details', idrefs).
a11y_rules_attr('aria-disabled', token([true, false])).
a11y_rules_attr('aria-dropeffect', tokens([copy, execute, link, move, none, popup])).
a11y_rules_attr('aria-errormessage', idrefs).
a11y_rules_attr('aria-expanded', token([true, false, undefined])).
a11y_rules_attr('aria-flowto', idrefs).
a11y_rules_attr('aria-grabbed', token([true, false, undefined])).
a11y_rules_attr('aria-haspopup', token([false, true, menu, listbox, tree, grid, dialog])).
a11y_rules_attr('aria-hidden', token([true, false, undefined])).
a11y_rules_attr('aria-invalid', token([grammar, false, spelling, true])).
a11y_rules_attr('aria-keyshortcuts', text).
a11y_rules_attr('aria-label', text).
a11y_rules_attr('aria-labelledby', idrefs).
a11y_rules_attr('aria-level', integer(1)).
a11y_rules_attr('aria-live', token([assertive, off, polite])).
a11y_rules_attr('aria-modal', token([true, false])).
a11y_rules_attr('aria-multiline', token([true, false])).
a11y_rules_attr('aria-multiselectable', token([true, false])).
a11y_rules_attr('aria-orientation', token([horizontal, vertical, undefined])).
a11y_rules_attr('aria-owns', idrefs).
a11y_rules_attr('aria-placeholder', text).
a11y_rules_attr('aria-posinset', integer(1)).
a11y_rules_attr('aria-pressed', token([true, false, mixed, undefined])).
a11y_rules_attr('aria-readonly', token([true, false])).
a11y_rules_attr('aria-relevant', tokens([additions, all, removals, text])).
a11y_rules_attr('aria-required', token([true, false])).
a11y_rules_attr('aria-roledescription', text).
a11y_rules_attr('aria-rowcount', integer(-1)).
a11y_rules_attr('aria-rowindex', integer(1)).
a11y_rules_attr('aria-rowindextext', text).
a11y_rules_attr('aria-rowspan', integer(0)).
a11y_rules_attr('aria-selected', token([true, false, undefined])).
a11y_rules_attr('aria-setsize', integer(-1)).
a11y_rules_attr('aria-sort', token([ascending, descending, none, other])).
a11y_rules_attr('aria-valuemax', number).
a11y_rules_attr('aria-valuemin', number).
a11y_rules_attr('aria-valuenow', number).
a11y_rules_attr('aria-valuetext', text).

%% the global states and properties, which every role supports (those deprecated as global in 1.2 among them)
a11y_rules_global('aria-atomic').
a11y_rules_global('aria-braillelabel').
a11y_rules_global('aria-brailleroledescription').
a11y_rules_global('aria-busy').
a11y_rules_global('aria-controls').
a11y_rules_global('aria-current').
a11y_rules_global('aria-describedby').
a11y_rules_global('aria-description').
a11y_rules_global('aria-details').
a11y_rules_global('aria-disabled').
a11y_rules_global('aria-dropeffect').
a11y_rules_global('aria-errormessage').
a11y_rules_global('aria-flowto').
a11y_rules_global('aria-grabbed').
a11y_rules_global('aria-haspopup').
a11y_rules_global('aria-hidden').
a11y_rules_global('aria-invalid').
a11y_rules_global('aria-keyshortcuts').
a11y_rules_global('aria-label').
a11y_rules_global('aria-labelledby').
a11y_rules_global('aria-live').
a11y_rules_global('aria-owns').
a11y_rules_global('aria-relevant').
a11y_rules_global('aria-roledescription').

%% the states and properties each role supports beyond the global ones (WAI-ARIA 1.2's characteristics
%% tables, those a role inherits included)
a11y_rules_supports(Role, Attr) :- a11y_rules_role_attrs(Role, Attrs), memberchk(Attr, Attrs).

a11y_rules_role_attrs(alertdialog, ['aria-modal']).
a11y_rules_role_attrs(application, ['aria-activedescendant', 'aria-expanded']).
a11y_rules_role_attrs(article, ['aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(button, ['aria-expanded', 'aria-pressed']).
a11y_rules_role_attrs(cell, ['aria-colindex', 'aria-colspan', 'aria-rowindex', 'aria-rowspan']).
a11y_rules_role_attrs(checkbox, ['aria-checked', 'aria-expanded', 'aria-readonly', 'aria-required']).
a11y_rules_role_attrs(columnheader, ['aria-sort', 'aria-colindex', 'aria-colspan', 'aria-rowindex', 'aria-rowspan', 'aria-expanded',
                                     'aria-readonly', 'aria-required', 'aria-selected']).
a11y_rules_role_attrs(combobox, ['aria-activedescendant', 'aria-autocomplete', 'aria-expanded', 'aria-readonly', 'aria-required']).
a11y_rules_role_attrs(dialog, ['aria-modal']).
a11y_rules_role_attrs(document, ['aria-expanded']).
a11y_rules_role_attrs(grid, ['aria-activedescendant', 'aria-colcount', 'aria-multiselectable', 'aria-readonly', 'aria-rowcount']).
a11y_rules_role_attrs(gridcell, ['aria-colindex', 'aria-colspan', 'aria-rowindex', 'aria-rowspan', 'aria-expanded', 'aria-readonly',
                                 'aria-required', 'aria-selected']).
a11y_rules_role_attrs(group, ['aria-activedescendant', 'aria-expanded']).
a11y_rules_role_attrs(heading, ['aria-level']).
a11y_rules_role_attrs(link, ['aria-expanded']).
a11y_rules_role_attrs(listbox, ['aria-activedescendant', 'aria-expanded', 'aria-multiselectable', 'aria-orientation', 'aria-readonly',
                                'aria-required']).
a11y_rules_role_attrs(listitem, ['aria-level', 'aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(menu, ['aria-activedescendant', 'aria-orientation']).
a11y_rules_role_attrs(menubar, ['aria-activedescendant', 'aria-orientation']).
a11y_rules_role_attrs(menuitem, ['aria-expanded', 'aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(menuitemcheckbox, ['aria-checked', 'aria-expanded', 'aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(menuitemradio, ['aria-checked', 'aria-expanded', 'aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(meter, ['aria-valuemax', 'aria-valuemin', 'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(option, ['aria-checked', 'aria-posinset', 'aria-selected', 'aria-setsize']).
a11y_rules_role_attrs(progressbar, ['aria-valuemax', 'aria-valuemin', 'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(radio, ['aria-checked', 'aria-posinset', 'aria-setsize']).
a11y_rules_role_attrs(radiogroup, ['aria-activedescendant', 'aria-expanded', 'aria-orientation', 'aria-readonly', 'aria-required']).
a11y_rules_role_attrs(row, ['aria-activedescendant', 'aria-colindex', 'aria-expanded', 'aria-level', 'aria-posinset', 'aria-rowindex',
                            'aria-selected', 'aria-setsize']).
a11y_rules_role_attrs(rowheader, ['aria-sort', 'aria-colindex', 'aria-colspan', 'aria-rowindex', 'aria-rowspan', 'aria-expanded',
                                  'aria-readonly', 'aria-required', 'aria-selected']).
a11y_rules_role_attrs(scrollbar, ['aria-orientation', 'aria-valuemax', 'aria-valuemin', 'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(searchbox, ['aria-activedescendant', 'aria-autocomplete', 'aria-multiline', 'aria-placeholder', 'aria-readonly',
                                  'aria-required']).
a11y_rules_role_attrs(separator, ['aria-orientation', 'aria-valuemax', 'aria-valuemin', 'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(slider, ['aria-orientation', 'aria-readonly', 'aria-valuemax', 'aria-valuemin', 'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(spinbutton, ['aria-activedescendant', 'aria-readonly', 'aria-required', 'aria-valuemax', 'aria-valuemin',
                                   'aria-valuenow', 'aria-valuetext']).
a11y_rules_role_attrs(switch, ['aria-checked', 'aria-expanded', 'aria-readonly', 'aria-required']).
a11y_rules_role_attrs(tab, ['aria-expanded', 'aria-posinset', 'aria-selected', 'aria-setsize']).
a11y_rules_role_attrs(table, ['aria-colcount', 'aria-rowcount']).
a11y_rules_role_attrs(tablist, ['aria-activedescendant', 'aria-multiselectable', 'aria-orientation']).
a11y_rules_role_attrs(textbox, ['aria-activedescendant', 'aria-autocomplete', 'aria-multiline', 'aria-placeholder', 'aria-readonly',
                                'aria-required']).
a11y_rules_role_attrs(toolbar, ['aria-activedescendant', 'aria-orientation']).
a11y_rules_role_attrs(tree, ['aria-activedescendant', 'aria-multiselectable', 'aria-orientation', 'aria-required']).
a11y_rules_role_attrs(treegrid, ['aria-activedescendant', 'aria-colcount', 'aria-multiselectable', 'aria-orientation', 'aria-readonly',
                                 'aria-required', 'aria-rowcount']).
a11y_rules_role_attrs(treeitem, ['aria-checked', 'aria-expanded', 'aria-level', 'aria-posinset', 'aria-selected', 'aria-setsize']).

%% the roles that take no name: aria-label and aria-labelledby on them name nothing (WAI-ARIA 1.2)
a11y_rules_unnamed(caption).
a11y_rules_unnamed(code).
a11y_rules_unnamed(deletion).
a11y_rules_unnamed(emphasis).
a11y_rules_unnamed(generic).
a11y_rules_unnamed(insertion).
a11y_rules_unnamed(paragraph).
a11y_rules_unnamed(presentation).
a11y_rules_unnamed(strong).
a11y_rules_unnamed(subscript).
a11y_rules_unnamed(superscript).

%% an aria-* state or property its element's role does not support
a11y_unsupported_attr(N, Attr, Role) :-
    dom_attr(N, Attr, _), a11y_rules_attr(Attr, _), \+ a11y_rules_global(Attr),
    a11y_role_of(N, Role), \+ a11y_rules_supports(Role, Attr).

%% a name given to a role that takes none
a11y_prohibited_attr(N, Attr, Role) :-
    dom_attr(N, Attr, _), a11y_rules_naming(Attr), a11y_role_of(N, Role), a11y_rules_unnamed(Role).
a11y_rules_naming('aria-label').
a11y_rules_naming('aria-labelledby').

%% an aria-* attribute WAI-ARIA does not define, on an element of the tree
a11y_unknown_attr(N, Attr) :-
    dom_attr(N, Attr, _), atom_concat('aria-', _, Attr), \+ a11y_rules_attr(Attr, _), a11y_role_of(N, _).

%% an aria-* attribute whose value is none of those it takes, on an element of the tree. Values are
%% compared without case, trimmed; an empty value is as none
a11y_bad_value(N, Attr, Value) :-
    dom_attr(N, Attr, Value), a11y_rules_attr(Attr, Kind), \+ a11y_rules_valid(Kind, Value), a11y_role_of(N, _).

a11y_rules_valid(_, V) :- a11y_rules_norm(V, '').
a11y_rules_valid(text, _).
a11y_rules_valid(idref, _).
a11y_rules_valid(idrefs, _).
a11y_rules_valid(token(Ts), V) :- a11y_rules_norm(V, T), memberchk(T, Ts).
a11y_rules_valid(tokens(Ts), V) :-
    a11y_rules_norm(V, T), atomic_list_concat(Ws0, ' ', T), exclude(==(''), Ws0, Ws), forall(member(W, Ws), memberchk(W, Ts)).
a11y_rules_valid(integer(Least), V) :- a11y_rules_integer(V, I), I >= Least.
a11y_rules_valid(number, V) :- a11y_rules_number(V, _).

%% a value trimmed and in lower case
a11y_rules_norm(V, T) :-
    downcase_atom(V, L), split_string(L, "", " \t\n\r\f", [S]), atom_string(T, S).

%% an integer written in decimal, a sign allowed
a11y_rules_integer(V, I) :-
    a11y_rules_norm(V, T), atom_chars(T, Cs), a11y_rules_unsigned(Cs, Ds), Ds \== [],
    forall(member(C, Ds), a11y_rules_digit(C)), atom_chars(A, Ds), atom_number(A, U),
    (   Cs = ['-'|_] -> I is -U ; I = U ).
a11y_rules_unsigned(['-'|Ds], Ds).
a11y_rules_unsigned(['+'|Ds], Ds).
a11y_rules_unsigned([C|Ds], [C|Ds]) :- C \== '-', C \== '+'.
a11y_rules_digit(C) :- memberchk(C, ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']).

%% a number written in decimal, as aria-valuenow takes one (and as the engine reads it)
a11y_rules_number(V, X) :-
    a11y_rules_norm(V, T), T \== '', atom_chars(T, Cs),
    forall(member(C, Cs), ( a11y_rules_digit(C) ; memberchk(C, ['.', '-', '+', e]) )),
    a11y_rules_decimal(Cs, Ds), atom_chars(A, Ds), catch(atom_number(A, X), _, fail), number(X).

%% a decimal as Prolog reads one: no plus sign, a 0 before a leading point and after a trailing one
a11y_rules_decimal(Cs, Ds) :-
    (   Cs = ['+'|R] -> true ; R = Cs ),
    (   R = ['-'|R1] -> Sign = ['-'] ; Sign = [], R1 = R ),
    (   R1 = ['.'|_] -> R2 = ['0'|R1] ; R2 = R1 ),
    (   append(F, ['.'], R2) -> append(F, ['.', '0'], R3) ; R3 = R2 ),
    append(Sign, R3, Ds).

%% what kind of value an attribute takes, as a diagnostic says it
a11y_rules_kind_text(token(Ts), Text) :- atomic_list_concat(Ts, ', ', L), format(atom(Text), 'one of ~w', [L]).
a11y_rules_kind_text(tokens(Ts), Text) :- atomic_list_concat(Ts, ', ', L), format(atom(Text), 'a list of ~w', [L]).
a11y_rules_kind_text(integer(Least), Text) :- format(atom(Text), 'an integer of ~w or more', [Least]).
a11y_rules_kind_text(number, 'a number').

%% ---- ranges and positions -----------------------------------------------------

%% a range widget whose role needs aria-valuenow, read a default for want of it
a11y_range_missing(A, Role) :- a11y_valuenow(A, _, default), a11y_node(A, _, Role, _, _), a11y_rules_needs_value(Role).
a11y_rules_needs_value(slider).
a11y_rules_needs_value(scrollbar).
a11y_rules_needs_value(meter).
a11y_rules_needs_value(separator).

%% a range widget whose aria-valuenow, as written, lies outside its least and greatest values
a11y_range_outside(A, Now, Min, Max) :-
    a11y_valuenow(A, _, aria), a11y_node(A, N, _, _, _), dom_attr(N, 'aria-valuenow', V), a11y_rules_number(V, Now),
    a11y_valuemin(A, Min), a11y_valuemax(A, Max), \+ ( Now >= Min, Now =< Max ).

%% a node whose position lies outside its set: before the first, past the set's size, or a size below -1 (unknown)
a11y_position_outside(A, Pos, Size) :- a11y_position(A, Pos, Size), \+ a11y_rules_in_set(Pos, Size).
a11y_rules_in_set(Pos, Size) :- Pos >= 1, Size >= -1, ( Size =:= -1 -> true ; Pos =< Size ).

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
a11y_diag(unsupported_attribute, N, Text) :-
    a11y_unsupported_attr(N, Attr, Role), dom_element(N, E),
    format(atom(Text), 'the ~w <~w> has ~w, which its role does not support: a reader is not told it', [Role, E, Attr]).
a11y_diag(prohibited_attribute, N, Text) :-
    a11y_prohibited_attr(N, Attr, Role), dom_element(N, E),
    format(atom(Text), 'the ~w <~w> has ~w, but its role takes no name: a reader is not told it', [Role, E, Attr]).
a11y_diag(unknown_attribute, N, Text) :-
    a11y_unknown_attr(N, Attr), dom_element(N, E),
    format(atom(Text), '<~w> has ~w, which WAI-ARIA does not define', [E, Attr]).
a11y_diag(bad_value, N, Text) :-
    a11y_bad_value(N, Attr, V), dom_element(N, E), a11y_rules_attr(Attr, Kind), a11y_rules_kind_text(Kind, K),
    a11y_rules_read_as(Attr, As),
    format(atom(Text), '<~w ~w=~q>: the value is not ~w, so it counts as ~w', [E, Attr, V, K, As]).

%% how a value outside an attribute's values is read: as true for aria-current and aria-invalid
%% (WAI-ARIA 1.2), as unset for every other
a11y_rules_read_as(Attr, true) :- memberchk(Attr, ['aria-current', 'aria-invalid']).
a11y_rules_read_as(Attr, unset) :- \+ memberchk(Attr, ['aria-current', 'aria-invalid']).
a11y_diag(range_missing_value, A, Text) :-
    a11y_range_missing(A, Role), a11y_node(A, N, _, _, _), dom_element(N, E), a11y_valuenow(A, Now, _),
    format(atom(Text), 'the ~w <~w> has no aria-valuenow, which its role needs: a reader is told ~w, a default', [Role, E, Now]).
a11y_diag(range_out_of_bounds, A, Text) :-
    a11y_range_outside(A, Now, Min, Max), a11y_node(A, N, Role, _, _), dom_element(N, E),
    format(atom(Text), 'the ~w <~w> says its value is ~w, outside its range of ~w to ~w', [Role, E, Now, Min, Max]).
a11y_diag(hidden_focusable, N, Text) :-
    a11y_hidden_focusable(N, Root), dom_element(N, E), dom_element(Root, RE),
    format(atom(Text), 'the <~w> takes focus inside aria-hidden content (<~w>): a keyboard reaches what a reader is not told of', [E, RE]).
a11y_diag(hidden_live_region, N, Text) :-
    a11y_hidden_live(N, Root), a11y_hidden(Root, How), dom_element(N, E), dom_element(Root, RE),
    format(atom(Text), 'the live region <~w> is inside hidden content (<~w>, ~w): what changes in it is not announced', [E, RE, How]).
a11y_diag(position_out_of_range, A, Text) :-
    a11y_position_outside(A, Pos, Size), a11y_node(A, N, Role, _, _), dom_element(N, E),
    format(atom(Text), 'the ~w <~w> is told it is at position ~w of a set of ~w', [Role, E, Pos, Size]).
