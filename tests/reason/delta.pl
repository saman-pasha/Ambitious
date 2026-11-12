%% tests/reason/delta.pl -- the reasoner kept for a session takes deltas (Phase 12i, 1).
%% Each delta here is written by hand, with the snapshot it leads to: a delta applied
%% to the snapshot before it must leave the reasoner holding, predicate by predicate
%% and in order, the facts a fresh load of the snapshot after it gives. The deltas edit
%% (a fact retracted, one appended), rebuild (runs kept around facts put between them,
%% a fact with a twin dropped), empty a predicate and fill one that was empty; the
%% changes are what diff says of the two snapshots; and each kind of bad delta is
%% refused whole, the reasoner then holding nothing and its input read to the delta's
%% end. Prints a line a check; delta.expect is what it must print.
:- ensure_loaded(library(amb/reasoner)).
:- use_module(library(stream)).

main :-
    amb_contract_root(Root),
    atom_concat(Root, '/tests/reason/demo.snapshot', Demo),
    read_file_to_codes(Demo, Codes),
    split_string(Codes, "\n", "", [Head0|_]),
    atom_string(Head, Head0),
    %% the header with its revision put at R
    nb_setval(amb_td_head, Head),
    amb_td_snap(1, s0, S0),
    amb_td_snap(2, s1, S1),
    amb_td_snap(3, s2, S2),
    amb_td_snap(4, s3, S3),
    amb_td_step('an edit: a fact retracted, one appended', S0, 2, d1, S1),
    amb_td_step('a rebuild: runs kept, facts put between them, a twin dropped', S1, 3, d2, S2),
    amb_td_changes(S1, 3, d2, S2),
    amb_td_step('a predicate emptied, and one that was empty filled', S2, 4, d3, S3),
    forall(amb_td_bad(Name, Lines), amb_td_refused(Name, S3, Lines)),
    format('done~n', []).

%% the facts of each snapshot, as their lines; s0 holds a twin
amb_td_facts(s0, ['dom_node(n(1,1,0),document).', 'dom_node(n(1,2,0),element).', 'dom_node(n(1,3,0),element).',
                  'dom_element(n(1,2,0),html).', 'dom_element(n(1,3,0),p).',
                  'dom_parent(n(1,2,0),n(1,1,0)).', 'dom_parent(n(1,3,0),n(1,2,0)).',
                  'dom_attr(n(1,3,0),class,a).', 'dom_attr(n(1,3,0),class,a).', 'dom_attr(n(1,2,0),lang,en).']).
amb_td_facts(s1, ['dom_node(n(1,1,0),document).', 'dom_node(n(1,2,0),element).', 'dom_node(n(1,3,0),element).',
                  'dom_element(n(1,2,0),html).', 'dom_element(n(1,3,0),p).',
                  'dom_parent(n(1,2,0),n(1,1,0)).', 'dom_parent(n(1,3,0),n(1,2,0)).',
                  'dom_attr(n(1,3,0),class,a).', 'dom_attr(n(1,3,0),class,a).', 'dom_attr(n(1,3,0),id,x).']).
amb_td_facts(s2, ['dom_node(n(1,1,0),document).', 'dom_node(n(1,2,0),element).', 'dom_node(n(1,3,0),element).',
                  'dom_node(n(1,4,0),element).',
                  'dom_element(n(1,4,0),em).', 'dom_element(n(1,2,0),html).', 'dom_element(n(1,3,0),p).',
                  'dom_parent(n(1,2,0),n(1,1,0)).', 'dom_parent(n(1,3,0),n(1,2,0)).',
                  'dom_attr(n(1,3,0),class,a).', 'dom_attr(n(1,3,0),title,\'t\').', 'dom_attr(n(1,3,0),id,x).']).
amb_td_facts(s3, ['dom_node(n(1,1,0),document).', 'dom_node(n(1,2,0),element).', 'dom_node(n(1,3,0),element).',
                  'dom_node(n(1,4,0),element).', 'dom_node(n(1,5,0),text).',
                  'dom_element(n(1,4,0),em).', 'dom_element(n(1,2,0),html).', 'dom_element(n(1,3,0),p).',
                  'dom_text(n(1,5,0),\'hi\').',
                  'dom_parent(n(1,2,0),n(1,1,0)).', 'dom_parent(n(1,3,0),n(1,2,0)).']).

%% each delta's terms after its header
amb_td_delta(d1, ['amb_edit(dom_attr).', '-(dom_attr(n(1,2,0),lang,en)).', '+(dom_attr(n(1,3,0),id,x)).']).
amb_td_delta(d2, ['amb_edit(dom_node).', '+(dom_node(n(1,4,0),element)).',
                  'amb_rebuild(dom_element).', '+(dom_element(n(1,4,0),em)).', 'amb_keep(0,2).',
                  'amb_rebuild(dom_attr).', 'amb_keep(0,1).', '+(dom_attr(n(1,3,0),title,\'t\')).', 'amb_keep(2,1).']).
amb_td_delta(d3, ['amb_edit(dom_node).', '+(dom_node(n(1,5,0),text)).',
                  'amb_edit(dom_text).', '+(dom_text(n(1,5,0),\'hi\')).',
                  'amb_rebuild(dom_attr).']).

%% the header at revision R: the demo snapshot's module, document and fingerprint
amb_td_header(R, H) :-
    nb_getval(amb_td_head, Head),
    term_to_atom(amb_snapshot(M, D, _, FP), Head),
    format(atom(H), '~q.', [amb_snapshot(M, D, R, FP)]).

%% a snapshot's file
amb_td_snap(R, Name, File) :-
    amb_td_header(R, H), amb_td_facts(Name, Fs),
    atomic_list_concat([H|Fs], '\n', Text),
    amb_td_write(Text, File).

amb_td_write(Text, File) :-
    tmp_file(delta, File), atom_codes(Text, Cs), append(Cs, [0'\n], Cs1), write_file_from_codes(File, Cs1).

%% a delta's file, its header at revision R; then a term past its end, to show where
%% a reader stops
amb_td_delta_file(R, Lines, File) :-
    amb_td_header(R, H),
    append([H|Lines], ['amb_end.', 'amb_td_after.'], All),
    atomic_list_concat(All, '\n', Text),
    amb_td_write(Text, File).

%% the facts held, predicate by predicate, each predicate's in the order held
amb_td_held(Facts) :-
    findall(F/A-L, ( amb_reason_schema_decl(dom, fact(F/A, _)), functor(G, F, A), findall(G, G, L) ), Facts).

%% the delta applied, its changes kept (Track true, as for diff) or only counted
amb_td_apply(R, Name, Track, Res) :-
    amb_td_delta(Name, Lines), amb_td_delta_file(R, Lines, File),
    open(File, read, S),
    catch(( amb_reason_delta_from(S, Track, N), Res = held(N) ), error(amb_snapshot(Why), _), Res = refused(Why)),
    close(S).

%% the snapshot Before loaded, the delta applied, its facts held to a fresh load of After's
amb_td_step(What, Before, R, Delta, After) :-
    amb_snapshot_load(Before, _),
    amb_td_apply(R, Delta, false, Res),
    amb_td_held(Got),
    nb_getval(amb_reason_count, NGot),
    amb_snapshot_load(After, snap(_, _, RAfter, NWant)),
    amb_td_held(Want),
    (   Res = held(_), Got == Want, NGot == NWant
    ->  format('~w: the facts held are a fresh load''s, in order (~d facts, revision ~w)~n', [What, NGot, RAfter])
    ;   format('~w: DIFFERENT (~q)~n  held:  ~q~n  fresh: ~q~n', [What, Res, Got, Want])
    ).

%% the changes a delta made, kept as it is applied, as diff says them of the two snapshots
amb_td_changes(Before, R, Delta, After) :-
    amb_snapshot_load(Before, _),
    amb_td_apply(R, Delta, true, _),
    with_output_to(atom(Got), amb_reason_changes),
    with_output_to(atom(Want), amb_reason([diff, Before, After])),
    (   Got == Want -> atom_length(Got, L), format('its changes, as diff says them: the same ~d characters~n', [L])
    ;   format('its changes DIFFER from diff:~n~w~nagainst~n~w', [Got, Want])
    ).

%% each bad delta, its terms after the header (the first the header itself where named)
amb_td_bad(not_held, ['amb_edit(dom_attr).', '-(dom_attr(n(1,9,0),lang,en)).']).
amb_td_bad(run_past_the_end, ['amb_rebuild(dom_node).', 'amb_keep(0,9).']).
amb_td_bad(run_backwards, ['amb_rebuild(dom_node).', 'amb_keep(2,1).', 'amb_keep(0,1).']).
amb_td_bad(other_document, ['amb_edit(dom_node).', '+(dom_node(n(2,9,0),element)).']).
amb_td_bad(other_predicate, ['amb_edit(dom_attr).', '+(dom_node(n(1,9,0),element)).']).
amb_td_bad(undeclared, ['amb_edit(dom_secret).', '+(dom_secret(x)).']).
amb_td_bad(no_block, ['+(dom_node(n(1,9,0),element)).']).
amb_td_bad(directive, [':- halt.']).
amb_td_bad(other_section, ['amb_snapshot(css, 1, 0, \'fnv1a32:0:0\').']).

amb_td_refused(Name, Snap, Lines) :-
    amb_snapshot_load(Snap, _),
    amb_td_header(5, H), append([H|Lines], ['amb_end.', 'amb_td_after.'], All),
    atomic_list_concat(All, '\n', Text), amb_td_write(Text, File),
    open(File, read, S),
    catch(( amb_reason_delta_from(S, false, N), Res = held(N) ), error(amb_snapshot(Why), _), Res = refused(Why)),
    read_term(S, Next, []),
    close(S),
    catch(( amb_reason_held, Held = holds ), error(amb_snapshot(W2), _), Held = W2),
    (   Res = refused(Why2)
    ->  ( Why2 = bad_delta(B) -> functor(B, K, _) ; functor(Why2, K, _) ),
        format('refused ~w: ~w; then ~w, the next term read ~w~n', [Name, K, Held, Next])
    ;   format('ACCEPTED ~w: it must be refused~n', [Name])
    ).
