%% tests/contract/run.pl -- the contract gate.
%%
%%   cocolog -s tests/contract/run.pl            (make test-contract)
%%
%% 1. every contract in contracts/ validates with no diagnostic;
%% 2. a minimal contract validates with none;
%% 3. each case below is that minimal contract with ONE rule broken, and the
%%    validator must refuse it with a message holding the case's expected text.
%%
%% A case's declarations are asserted as tc_N_contract/1 (the default --local
%% store: memory only), so the validator reads them as it reads a file's.
:- ensure_loaded(library(amb/contract)).

amb_tc_min([
    module('$M', '0.1.0', 'a minimal contract'),
    status([ok, stale_handle, bad]),
    enum(colour, [red, green]),
    record(item, [field(hue, enum(colour)), field(next, ref(item)), field(up, ref(box_rec)), field(n, u32)]),
    record(box_rec, [field(first, ref(item)), field(kind, enum(colour))]),
    handle(item, i),
    handle(box_rec, b),
    store(box, [table(item), table(box_rec), pool(text), atoms(names), log(changed)]),
    owns(box, item, store),
    owns(box, box_rec, store),
    owns(box_rec, item, list(items)),
    list(items, box_rec, item, [first(first), next(next), up(up)]),
    operation(paint, [param(it, handle(item)), param(c, enum(colour))], status,
              [pre(live(it), stale_handle)], [sets(item, hue)], [mutates, requestable]),
    invariant(linked, 'items are listed by their box', list_consistent(items)),
    event(changed, [field(op, enum(op)), field(target, ref(item))]),
    fact('$M_item'(handle, field(hue)), table(item), true)
]).

%% case(Name, Edit, ExpectedText): Edit turns the minimal list into the bad one
amb_tc_case(no_module,        drop(module(_, _, _)),                          'no module declaration').
amb_tc_case(module_twice,     add(module('$M', '0.2.0', x)),                  'declared twice').
amb_tc_case(wrong_module,     swap(module(_, _, _), module(other, '0.1.0', x)), 'is named other').
amb_tc_case(no_status,        drop(status(_)),                                'declares no status codes').
amb_tc_case(status_not_ok,    swap(status(_), status([bad, ok])),             'must be ok').
amb_tc_case(status_dup,       swap(status(_), status([ok, bad, bad])),        'listed twice').
amb_tc_case(unknown_decl,     add(colour(red)),                               'unknown declaration').
amb_tc_case(enum_none,        swap(enum(colour, _), enum(colour, [red, none])), 'none is the empty ref').
amb_tc_case(field_type,       swap(record(item, _), record(item, [field(hue, enum(shade))])), 'not declared or not a field type').
amb_tc_case(field_reserved,   add(record(extra, [field(gen, u32)])),          'the table''s own').
amb_tc_case(record_untabled,  add(record(stray, [field(n, u32)])),            'has no table in the store').
amb_tc_case(two_stores,       add(store(crate, [table(item)])),               'a module has one store').
amb_tc_case(unowned,          drop(owns(box, box_rec, store)),                'the store owns no box_rec').
amb_tc_case(owns_no_list,     add(owns(item, box_rec, list(items))),          'no list items from item').
amb_tc_case(owner_cycle,      [swap(record(item, _), record(item, [field(hue, enum(colour)), field(next, ref(item)),
                                                                   field(up, ref(box_rec)), field(n, u32),
                                                                   field(firstbox, ref(box_rec))])),
                               swap(record(box_rec, _), record(box_rec, [field(first, ref(item)), field(kind, enum(colour)),
                                                                         field(bnext, ref(box_rec)), field(bup, ref(item))])),
                               add(list(boxes, item, box_rec, [first(firstbox), next(bnext), up(bup)])),
                               add(owns(item, box_rec, list(boxes)))],     'ownership has a cycle').
amb_tc_case(handle_twice,     add(handle(box_rec, i)),                        'the handle of item too').
amb_tc_case(list_link,        swap(list(items, _, _, _), list(items, box_rec, item, [first(next), next(next), up(up)])), 'not a ref field of the right record').
amb_tc_case(list_needs_up,    swap(list(items, _, _, _), list(items, box_rec, item, [first(first), next(next)])), 'needs its up link').
amb_tc_case(pre_status,       swap(operation(paint, _, _, _, _, _), operation(paint, [param(it, handle(item))], status, [pre(live(it), nonesuch)], [], [mutates])), 'unknown status').
amb_tc_case(pre_param,        swap(operation(paint, _, _, _, _, _), operation(paint, [param(it, handle(item))], status, [pre(live(other), bad)], [], [mutates])), 'does not fit its parameters').
amb_tc_case(requestable_res,  swap(operation(paint, _, _, _, _, _), operation(paint, [param(it, handle(item))], handle(item), [], [], [requestable])), 'answers a status').
amb_tc_case(option_unknown,   swap(operation(paint, _, _, _, _, _), operation(paint, [], status, [], [], [fast])), 'is not mutates').
amb_tc_case(invariant_check,  swap(invariant(linked, _, _), invariant(linked, 'x', list_consistent(nolist))), 'no generated check').
amb_tc_case(acyclic_not_tree, swap(invariant(linked, _, _), invariant(linked, 'x', acyclic(items))), 'no generated check').
amb_tc_case(event_unlogged,   add(event(other, [field(n, u32)])),             'no store part logs the event other').
amb_tc_case(fact_prefix,      swap(fact(_, _, _), fact(item_of(handle), table(item), true)), 'does not begin with').
amb_tc_case(fact_arg,         swap(fact(_, _, _), fact('$M_item'(field(colourless)), table(item), true)), 'is not the handle').
amb_tc_case(fact_where,       swap(fact(_, _, _), fact('$M_item'(handle), table(item), field(hue) == blue)), 'is not true or a comparison').
amb_tc_case(derived_no_rules, add(derived('$M_thing'/1, 'a thing')),          'needs the module''s rules').
amb_tc_case(query_unknown,    add(query(q, '$M_nonesuch'(_), [], 'q')),       'no fact or derived predicate').
amb_tc_case(generate_unknown, add(generate([c_types, rockets])),             'not one the generator makes').
amb_tc_case(test_kind,        add(test(fuzz, 'tests/fuzz')),                  'test kind fuzz is unknown').

%% ---- the runner -------------------------------------------------------------

main :-
    amb_tc_real(R1),
    amb_tc_min_ok(R2),
    findall(N, amb_tc_case(N, _, _), Names),
    foldl(amb_tc_run, Names, 0, R3),
    length(Names, K),
    Bad is R1 + R2 + R3,
    (   Bad =:= 0
    ->  format('GREEN: contract (the real contracts, the minimal one, ~d refusals)~n', [K])
    ;   format('RED: contract, ~d failure(s)~n', [Bad]), halt(1)
    ).

amb_tc_real(Bad) :-
    amb_contract_root(Root),
    atom_concat(Root, '/contracts', Dir),
    directory_files(Dir, Fs0), msort(Fs0, Fs),
    findall(M, ( member(F, Fs), file_name_extension(M, pl, F), M \== '' ), Ms),
    foldl(amb_tc_real_one, Ms, 0, Bad).

amb_tc_real_one(M, B0, B) :-
    amb_validate(M, Ds),
    (   Ds == []
    ->  format('ok   contracts/~w.pl validates~n', [M]), B = B0
    ;   format('FAIL contracts/~w.pl: ~q~n', [M, Ds]), B is B0 + 1
    ).

%% the minimal contract under a module name of its own
amb_tc_install(Name, Decls, M) :-
    atom_concat(tc_, Name, M),
    atom_concat(M, '_contract', P),
    retractall(amb_tc_dummy), PI = P/1,
    ( current_predicate(PI) -> abolish(PI) ; true ),
    dynamic(PI),
    forall(member(D0, Decls), ( amb_tc_subst(D0, M, D), T =.. [P, D], assertz(T) )).

%% '$M' in a declaration is the case's module name; '$M_x' its prefixed x
amb_tc_subst(V, _, V) :- var(V), !.
amb_tc_subst('$M', M, M) :- !.
amb_tc_subst(A, M, B) :- atom(A), atom_concat('$M_', Rest, A), !, atomic_list_concat([M, '_', Rest], B).
amb_tc_subst(T, M, T2) :- compound(T), !, T =.. [F0|As], amb_tc_subst(F0, M, F), maplist([X, Y]>>amb_tc_subst(X, M, Y), As, Bs), T2 =.. [F|Bs].
amb_tc_subst(T, _, T).

:- dynamic amb_tc_dummy/0.

amb_tc_min_ok(Bad) :-
    amb_tc_min(Decls),
    amb_tc_install(min, Decls, M),
    amb_validate(M, Ds),
    (   Ds == []
    ->  format('ok   the minimal contract validates~n', []), Bad = 0
    ;   format('FAIL the minimal contract: ~q~n', [Ds]), Bad = 1
    ).

amb_tc_run(Name, B0, B) :-
    amb_tc_case(Name, Edit, Want),
    amb_tc_min(Min),
    amb_tc_edit(Edit, Min, Decls),
    amb_tc_install(Name, Decls, M),
    amb_validate(M, Ds),
    (   member(diag(_, Msg), Ds), sub_atom(Msg, _, _, _, Want)
    ->  format('ok   ~w refused: ~w~n', [Name, Want]), B = B0
    ;   format('FAIL ~w: wanted a refusal holding "~w", got ~q~n', [Name, Want, Ds]), B is B0 + 1
    ).

amb_tc_edit([], L, L) :- !.
amb_tc_edit([E|Es], L0, L) :- !, amb_tc_edit(E, L0, L1), amb_tc_edit(Es, L1, L).
amb_tc_edit(drop(T), L0, L) :- exclude(subsumes_term(T), L0, L).
amb_tc_edit(add(D), L0, L) :- append(L0, [D], L).
amb_tc_edit(swap(T, D), L0, L) :- amb_tc_swap(L0, T, D, L).

amb_tc_swap([], _, _, []).
amb_tc_swap([X|Xs], T, D, [Y|Ys]) :-
    ( subsumes_term(T, X) -> Y = D ; Y = X ),
    amb_tc_swap(Xs, T, D, Ys).
