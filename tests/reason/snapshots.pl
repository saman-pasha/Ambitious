%% tests/reason/snapshots.pl -- the reasoning gate (the architecture's §6.3,
%% criteria 6 and 7): a snapshot the engine wrote (tests/reason/demo.snapshot,
%% `ambitious snapshot`) is loaded and asked, and each kind of bad snapshot is
%% refused whole. Prints a line a check; snapshots.expect is what it must print.
:- ensure_loaded(library(amb/reasoner)).

main :-
    amb_contract_root(Root),
    atom_concat(Root, '/tests/reason/demo.snapshot', Demo),
    amb_snapshot_load(Demo, snap(M, Doc, Rev, N)),
    format('loaded: ~w snapshot of document ~w at revision ~w, ~d facts~n', [M, Doc, Rev, N]),
    amb_rt_answers('the images', N1^dom_element(N1, img)),
    amb_rt_answers('the body''s children, in order', Cs^( dom_element(B, body), dom_children(B, Cs) )),
    amb_rt_answers('the button''s accessible name', Nm^( dom_element(Bt, button), dom_accessible_name(Bt, Nm) )),
    amb_rt_answers('what lacks a required attribute', (E-A)^( dom_missing_attribute(X, A), dom_element(X, E) )),
    amb_rt_answers('the title''s text', T^( dom_element(Ti, title), dom_text_content(Ti, T) )),
    aggregate_all(count, ( dom_element(B2, body), dom_removal_effect(B2, removes(_)) ), K),
    format('removing the body removes ~d nodes~n', [K]),
    once(( dom_element(Ttl, title), dom_text(Txt, 'Ambitious'), dom_element(Html, html) )),
    findall(P, amb_why(dom_inside(Txt, Html), P), [Proof|_]),
    amb_rt_leaves(Proof, Leaves),
    length(Leaves, NL),
    (   forall(member(L, Leaves), L = fact(_)) -> What = 'every leaf a fact of the snapshot' ; What = 'NOT every leaf a fact' ),
    format('why the title''s text is inside html: a proof of ~d leaves, ~w~n', [NL, What]),
    Ttl = Ttl,
    read_file_to_codes(Demo, Codes),
    split_string(Codes, "\n", "", [Head|Body0]),
    exclude(==(""), Body0, Body),
    forall(amb_rt_bad(Name, Head, Body, Lines), amb_rt_refusal(Name, Lines)),
    amb_rt_data(Head),
    format('done~n', []).

amb_rt_answers(What, Template^Goal) :-
    findall(Template, Goal, As0), msort(As0, As),
    format('~w: ~q~n', [What, As]).

%% the leaves of a proof
amb_rt_leaves(and(A, B), L) :- !, amb_rt_leaves(A, LA), amb_rt_leaves(B, LB), append(LA, LB, L).
amb_rt_leaves(by(_, P), L) :- !, amb_rt_leaves(P, L).
amb_rt_leaves(true, []) :- !.
amb_rt_leaves(P, [P]).

%% each bad snapshot: the demo's lines with one fault
amb_rt_bad(fingerprint, _, Body, ['amb_snapshot(dom, 1, 36, \'fnv1a32:00000000:1\').'|Body]).
amb_rt_bad(directive, Head, Body, [Head, ':- halt.'|Body]).
amb_rt_bad(undeclared_fact, Head, Body, [Head, 'dom_secret(x).'|Body]).
amb_rt_bad(other_document, Head, Body, [Head, 'dom_node(n(2,1,0),document).'|Body]).
amb_rt_bad(undeclared_value, Head, Body, [Head, 'dom_node(n(1,1,0),banana).'|Body]).
amb_rt_bad(not_ground, Head, Body, [Head, 'dom_element(n(1,X,0),p).'|Body]).
amb_rt_bad(syntax, Head, Body, [Head, 'dom_element(n(1,2,0), .'|Body]).
amb_rt_bad(wrong_handle, Head, Body, [Head, 'dom_element(a(1,2,0),p).'|Body]).
amb_rt_bad(no_header, _, Body, Body).

amb_rt_refusal(Name, Lines) :-
    tmp_file(snap, F),
    atomic_list_concat(Lines, '\n', Text), atom_codes(Text, Cs),
    write_file_from_codes(F, Cs),
    catch(( amb_snapshot_facts(F, _, _), R = accepted ), error(amb_snapshot(Why), _), R = refused(Why)),
    delete_file(F),
    (   R = refused(Why2) -> functor(Why2, Kind, _), format('refused ~w: ~w~n', [Name, Kind])
    ;   format('ACCEPTED ~w: it must be refused~n', [Name])
    ).

%% page text that looks like code stays text: an escaped quote, then what would
%% be a directive and another fact, all inside one quoted atom
amb_rt_data(Head) :-
    tmp_file(snap, F),
    atomic_list_concat([Head, 'dom_text(n(1,2,0),\'x\\\'). halt. dom_secret(\\\'y\').', 'dom_node(n(1,2,0),text).'], '\n', Text),
    atom_codes(Text, Cs), write_file_from_codes(F, Cs),
    catch(( amb_snapshot_facts(F, _, Facts), R = Facts ), error(amb_snapshot(Why), _), R = refused(Why)),
    delete_file(F),
    (   R = [dom_text(_, T)|_] -> atom_length(T, L), format('text that looks like code is one atom of ~d characters, and nothing ran~n', [L])
    ;   format('UNEXPECTED: ~q~n', [R])
    ).
