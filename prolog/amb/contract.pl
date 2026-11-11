%% prolog/amb/contract.pl -- reading and validating Ambitious module contracts.
%%
%% A contract is contracts/M.pl, one predicate M_contract/1, one fact a
%% declaration (docs/contract.md). This file reads it and checks it; it ASSERTS
%% NOTHING, because the generator loads it inside cicilang's compile, whose
%% knowledge base is the user's persistent store: every answer is computed from
%% the contract's own clauses each time it is asked.
%%
%% cocolog has one namespace: the public predicates here are amb_contract_*,
%% amb_c/2 and amb_validate/2, the helpers ambc_*.
%%
%%   amb_contract_root(-Dir)        the Ambitious checkout ($AMB_ROOT, or found
%%                                  from this file's place on COCOLOG_LIBRARY)
%%   amb_contract_file(+M, -File)   contracts/M.pl
%%   amb_contract_load(+M)          the contract's clauses loaded (once a process)
%%   amb_c(+M, ?Decl)               a declaration of M's contract, in its order
%%   amb_contract_terms(+M, -Ds)    all of them
%%   amb_validate(+M, -Diags)       diag(Decl, Message) for each rule broken; [] is valid

amb_contract_root(Root) :-
    (   getenv('AMB_ROOT', R), R \== ''
    ->  Root = R
    ;   absolute_file_name(library(amb/contract), F, [file_type(prolog), access(read)]),
        file_directory_name(F, AmbDir),           % .../prolog/amb
        file_directory_name(AmbDir, PrologDir),   % .../prolog
        file_directory_name(PrologDir, Root)
    ).

amb_contract_file(M, File) :-
    amb_contract_root(Root),
    atomic_list_concat([Root, '/contracts/', M, '.pl'], File).

amb_contract_pred(M, P) :- atom_concat(M, '_contract', P).

amb_contract_load(M) :-
    amb_contract_pred(M, P),
    (   current_predicate(P/1)
    ->  true
    ;   amb_contract_file(M, File),
        (   exists_file(File)
        ->  ensure_loaded(File)
        ;   throw(error(amb_contract(no_contract(M, File)), _))
        )
    ).

amb_c(M, Decl) :-
    amb_contract_load(M),
    amb_contract_pred(M, P),
    G =.. [P, Decl],
    call(G).

amb_contract_terms(M, Ds) :- findall(D, amb_c(M, D), Ds).

%% ---- validation -------------------------------------------------------------

amb_validate(M, Diags) :-
    amb_contract_terms(M, Ds),
    findall(D, ambc_check(M, Ds, D), Diags0),
    ambc_dedup(Diags0, Diags).

ambc_dedup([], []).
ambc_dedup([D|Ds], [D|Rest]) :- ambc_delete_all(Ds, D, Ds1), ambc_dedup(Ds1, Rest).
ambc_delete_all([], _, []).
ambc_delete_all([X|Xs], D, Out) :-
    ( X == D -> Out = Rest ; Out = [X|Rest] ),
    ambc_delete_all(Xs, D, Rest).

%% diag(Decl, Message): Message is an atom
ambc_diag(Decl, Fmt, Args, diag(Decl, Msg)) :- format(atom(Msg), Fmt, Args).

%% the kinds of declaration a contract may hold
ambc_decl_kind(module(_, _, _)).
ambc_decl_kind(status(_)).
ambc_decl_kind(enum(_, _)).
ambc_decl_kind(record(_, _)).
ambc_decl_kind(handle(_, _)).
ambc_decl_kind(store(_, _)).
ambc_decl_kind(owns(_, _, _)).
ambc_decl_kind(tie(_, _)).
ambc_decl_kind(list(_, _, _, _)).
ambc_decl_kind(root(_, _, _)).
ambc_decl_kind(operation(_, _, _, _, _, _)).
ambc_decl_kind(invariant(_, _, _)).
ambc_decl_kind(event(_, _)).
ambc_decl_kind(fact(_, _, _)).
ambc_decl_kind(rules(_)).
ambc_decl_kind(derived(_, _)).
ambc_decl_kind(query(_, _, _, _)).
ambc_decl_kind(generate(_)).
ambc_decl_kind(test(_, _)).

%% ambc_check(+M, +Decls, -Diag): one broken rule. Each clause is one rule.

%% a declaration of no known kind
ambc_check(_, Ds, Diag) :-
    member(D, Ds), \+ ( ambc_decl_kind(K), subsumes_term(K, D) ),
    ambc_diag(D, 'unknown declaration', [], Diag).

%% the module: once, named as the file, with a version
ambc_check(M, Ds, Diag) :-
    findall(X, ( member(X, Ds), X = module(_, _, _) ), Ms),
    (   Ms = []
    ->  ambc_diag(module(M, '?', '?'), 'the contract has no module declaration', [], Diag)
    ;   Ms = [_, _|_]
    ->  Ms = [_, Second|_], ambc_diag(Second, 'the module is declared twice', [], Diag)
    ;   Ms = [module(N, V, _)],
        (   N \== M -> ambc_diag(module(N, V, '...'), 'the module is named ~w in the file of ~w', [N, M], Diag)
        ;   \+ atom(V) -> ambc_diag(module(N, V, '...'), 'the version must be an atom', [], Diag)
        )
    ).

%% the status codes: once, a list of distinct atoms, ok first
ambc_check(_, Ds, Diag) :-
    findall(L, member(status(L), Ds), Ls),
    (   Ls = []
    ->  ambc_diag(status('?'), 'the contract declares no status codes', [], Diag)
    ;   Ls = [_, L2|_]
    ->  ambc_diag(status(L2), 'the status codes are declared twice', [], Diag)
    ;   Ls = [L],
        (   \+ ambc_atom_list(L) -> ambc_diag(status(L), 'the status codes must be a list of atoms', [], Diag)
        ;   L \= [ok|_] -> ambc_diag(status(L), 'the first status code must be ok', [], Diag)
        ;   ambc_dup(L, X) -> ambc_diag(status(L), 'the status code ~w is listed twice', [X], Diag)
        )
    ).

%% names of one kind are unique
ambc_check(_, Ds, Diag) :-
    member(Kind-Template-Name, [enum-enum(N1, _)-N1, record-record(N2, _)-N2, operation-operation(N3, _, _, _, _, _)-N3,
                                invariant-invariant(N4, _, _)-N4, list-list(N5, _, _, _)-N5, event-event(N6, _)-N6,
                                query-query(N7, _, _, _)-N7]),
    findall(Name-Template, member(Template, Ds), Pairs),
    ambc_pairs_dup(Pairs, Dup, Decl),
    ambc_diag(Decl, 'the ~w ~w is declared twice', [Kind, Dup], Diag).

%% an enum lists distinct atoms
ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = enum(_, Vs),
    (   \+ ambc_atom_list(Vs) -> ambc_diag(D, 'an enum''s values must be a list of atoms', [], Diag)
    ;   Vs == [] -> ambc_diag(D, 'an enum needs a value', [], Diag)
    ;   ambc_dup(Vs, X) -> ambc_diag(D, 'the value ~w is listed twice', [X], Diag)
    ;   member(none, Vs) -> ambc_diag(D, 'none is the empty ref''s word and no enum value', [], Diag)
    ).

%% a record's fields: distinct names, known types
ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = record(_, Fs),
    (   \+ is_list(Fs) -> ambc_diag(D, 'a record''s fields must be a list', [], Diag)
    ;   member(F, Fs), F \= field(_, _) -> ambc_diag(D, 'a field is field(Name, Type), not ~q', [F], Diag)
    ;   findall(N, member(field(N, _), Fs), Ns), ambc_dup(Ns, X) -> ambc_diag(D, 'the field ~w is declared twice', [X], Diag)
    ;   member(field(N, T), Fs), \+ ambc_field_type(M, Ds, T)
    ->  ambc_diag(D, 'the field ~w has the type ~q, which is not declared or not a field type', [N, T], Diag)
    ;   member(field(N, _), Fs), ambc_reserved_field(N)
    ->  ambc_diag(D, 'the field name ~w is the table''s own', [N], Diag)
    ).

%% a handle names a record and a functor no other handle has
ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = handle(R, F),
    (   \+ member(record(R, _), Ds) -> ambc_diag(D, 'the handle names no record ~w', [R], Diag)
    ;   \+ atom(F) -> ambc_diag(D, 'a handle''s functor must be an atom', [], Diag)
    ;   member(handle(R2, F), Ds), R2 \== R -> ambc_diag(D, 'the functor ~w is the handle of ~w too', [F, R2], Diag)
    ;   member(handle(R, F2), Ds), F2 \== F -> ambc_diag(D, 'the record ~w has two handles', [R], Diag)
    ).

%% the store: exactly one; every record in one table; the parts known
ambc_check(_, Ds, Diag) :-
    findall(S, ( member(S, Ds), S = store(_, _) ), Ss),
    (   Ss = [] -> ambc_diag(store('?', '?'), 'the contract declares no store', [], Diag)
    ;   Ss = [_, S2|_] -> ambc_diag(S2, 'a module has one store', [], Diag)
    ).
ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = store(_, Parts),
    (   member(P, Parts), \+ ambc_store_part(Ds, P)
    ->  ambc_diag(D, 'the store part ~q is not a table of a record, a pool, an atom table or a log of an event', [P], Diag)
    ;   member(record(R, _), Ds), \+ member(table(R), Parts)
    ->  ambc_diag(D, 'the record ~w has no table in the store', [R], Diag)
    ;   findall(N, ( member(P, Parts), arg(1, P, N) ), Ns), ambc_dup(Ns, X)
    ->  ambc_diag(D, 'the store part ~w is declared twice', [X], Diag)
    ).

%% ownership: the owner is the store or a record, the owned a record, the way known
ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = owns(O, X, How),
    (   \+ ( member(store(O, _), Ds) ; member(record(O, _), Ds) )
    ->  ambc_diag(D, 'the owner ~w is neither the store nor a record', [O], Diag)
    ;   \+ member(record(X, _), Ds) -> ambc_diag(D, 'the owned ~w is not a record', [X], Diag)
    ;   How == store, \+ member(store(O, _), Ds) -> ambc_diag(D, 'only the store owns by store', [], Diag)
    ;   How = list(L), \+ member(list(L, O, X, _), Ds)
    ->  ambc_diag(D, 'no list ~w from ~w to ~w is declared', [L, O, X], Diag)
    ;   How \== store, How \= list(_) -> ambc_diag(D, 'a way of owning is store or list(L), not ~q', [How], Diag)
    ).
%% every record is owned by the store
ambc_check(_, Ds, Diag) :-
    member(store(S, _), Ds), member(record(R, _), Ds), \+ member(owns(S, R, store), Ds),
    ambc_diag(owns(S, R, store), 'the store owns no ~w: the record must be the store''s', [R], Diag).
%% owning by lists has no cycle but a tree's own (a record owning records of its kind)
ambc_check(_, Ds, Diag) :-
    member(owns(A, B, list(_)), Ds), A \== B, ambc_owns_path(Ds, B, A, [B]),
    ambc_diag(owns(A, B, list('...')), 'ownership has a cycle through ~w and ~w', [A, B], Diag).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = tie(H, S),
    (   H = handle(R), member(handle(R, _), Ds) -> ( member(store(S, _), Ds) -> fail ; ambc_diag(D, 'the tie names no store ~w', [S], Diag) )
    ;   ambc_diag(D, 'a tie is tie(handle(Record), Store), over a declared handle', [], Diag)
    ).

%% a list's links are fields of the right records and types
ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = list(_, O, I, Links),
    (   \+ member(record(O, _), Ds) -> ambc_diag(D, 'the list''s owner ~w is not a record', [O], Diag)
    ;   \+ member(record(I, _), Ds) -> ambc_diag(D, 'the list''s item ~w is not a record', [I], Diag)
    ;   \+ is_list(Links) -> ambc_diag(D, 'a list''s links must be a list', [], Diag)
    ;   member(Need, [first, next, up]), \+ ( member(L, Links), functor(L, Need, 1) )
    ->  ambc_diag(D, 'a list needs its ~w link', [Need], Diag)
    ;   member(L, Links), \+ ambc_link_ok(Ds, O, I, L)
    ->  ambc_diag(D, 'the link ~q is not a ref field of the right record', [L], Diag)
    ).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = root(R, F, V),
    (   \+ ambc_field(Ds, R, F, enum(E)) -> ambc_diag(D, 'the root''s field ~w is no enum field of ~w', [F, R], Diag)
    ;   \+ ( member(enum(E, Vs), Ds), memberchk(V, Vs) ) -> ambc_diag(D, '~w is no value of the enum ~w', [V, E], Diag)
    ).

%% operations: parameters, result, preconditions, options
ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = operation(_, Ps, Res, Pres, Effs, Opts),
    (   \+ is_list(Ps) -> ambc_diag(D, 'an operation''s parameters must be a list', [], Diag)
    ;   member(P, Ps), P \= param(_, _) -> ambc_diag(D, 'a parameter is param(Name, Type), not ~q', [P], Diag)
    ;   findall(N, member(param(N, _), Ps), Ns), ambc_dup(Ns, X) -> ambc_diag(D, 'the parameter ~w is named twice', [X], Diag)
    ;   member(param(N, T), Ps), \+ ambc_param_type(M, Ds, T)
    ->  ambc_diag(D, 'the parameter ~w has the type ~q, which is no parameter type', [N, T], Diag)
    ;   \+ ambc_result_type(Ds, Res) -> ambc_diag(D, 'the result ~q is no result type', [Res], Diag)
    ;   \+ is_list(Pres) -> ambc_diag(D, 'an operation''s preconditions must be a list', [], Diag)
    ;   member(Pre, Pres), \+ ambc_pre_ok(Ds, Ps, Pre)
    ->  ambc_diag(D, 'the precondition ~q is malformed, names an unknown status, or does not fit its parameters', [Pre], Diag)
    ;   \+ is_list(Effs) -> ambc_diag(D, 'an operation''s effects must be a list', [], Diag)
    ;   \+ ambc_atom_list(Opts) -> ambc_diag(D, 'an operation''s options must be a list of atoms', [], Diag)
    ;   member(O, Opts), \+ memberchk(O, [mutates, requestable, readonly])
    ->  ambc_diag(D, 'the option ~w is not mutates, requestable or readonly', [O], Diag)
    ;   memberchk(requestable, Opts), Res \== status
    ->  ambc_diag(D, 'a requestable operation answers a status', [], Diag)
    ;   memberchk(requestable, Opts), member(param(N, T), Ps), \+ ambc_requestable_type(T)
    ->  ambc_diag(D, 'a requestable operation''s parameter ~w cannot come from a request (~q)', [N, T], Diag)
    ;   memberchk(mutates, Opts), memberchk(readonly, Opts)
    ->  ambc_diag(D, 'an operation is not both mutates and readonly', [], Diag)
    ).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = invariant(_, Text, Check),
    (   \+ atom(Text) -> ambc_diag(D, 'an invariant''s text must be an atom', [], Diag)
    ;   \+ ambc_inv_ok(Ds, Check) -> ambc_diag(D, 'the check ~q is no generated check over declared things', [Check], Diag)
    ).

ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = event(N, Fs),
    (   \+ is_list(Fs) -> ambc_diag(D, 'an event''s fields must be a list', [], Diag)
    ;   member(field(F, T), Fs), \+ ambc_event_type(M, Ds, T)
    ->  ambc_diag(D, 'the event field ~w has the type ~q, which is not declared', [F, T], Diag)
    ;   \+ ( member(store(_, Parts), Ds), memberchk(log(N), Parts) )
    ->  ambc_diag(D, 'no store part logs the event ~w', [N], Diag)
    ).

%% facts: the module's prefix, a known table, arguments that are its own
ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = fact(Head, Src, Where),
    (   \+ compound(Head) -> ambc_diag(D, 'a fact''s head must be a compound', [], Diag)
    ;   functor(Head, F, _), \+ ambc_prefixed(M, F) -> ambc_diag(D, 'the fact ~w does not begin with ~w_', [F, M], Diag)
    ;   Src \= table(_) -> ambc_diag(D, 'a fact''s source is table(Record)', [], Diag)
    ;   Src = table(R), \+ member(record(R, _), Ds) -> ambc_diag(D, 'the fact''s table ~w is no record', [R], Diag)
    ;   Src = table(R), Head =.. [_|As], member(A, As), \+ ambc_fact_arg(Ds, R, A)
    ->  ambc_diag(D, 'the argument ~q is not the handle, the doc, the revision or a field of ~w', [A, R], Diag)
    ;   Src = table(R), \+ ambc_where_ok(Ds, R, Where)
    ->  ambc_diag(D, 'the condition ~q is not true or a comparison of a field of ~w with its value', [Where, R], Diag)
    ).
ambc_check(_, Ds, Diag) :-
    findall(F/A-fact(H, S, W), ( member(fact(H, S, W), Ds), compound(H), functor(H, F, A) ), Pairs),
    ambc_pairs_dup(Pairs, Dup, Decl),
    ambc_diag(Decl, 'the fact ~w is declared twice', [Dup], Diag).

ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = derived(PI, Text),
    (   PI \= _/_ -> ambc_diag(D, 'a derived predicate is Name/Arity', [], Diag)
    ;   PI = F/_, \+ ambc_prefixed(M, F) -> ambc_diag(D, 'the derived predicate ~w does not begin with ~w_', [F, M], Diag)
    ;   PI = F/A, member(fact(H, _, _), Ds), compound(H), functor(H, F, A)
    ->  ambc_diag(D, '~w is a fact and a derived predicate', [PI], Diag)
    ;   \+ atom(Text) -> ambc_diag(D, 'a derived predicate''s text must be an atom', [], Diag)
    ;   \+ member(rules(_), Ds) -> ambc_diag(D, 'a derived predicate needs the module''s rules(File)', [], Diag)
    ).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = rules(File),
    amb_contract_root(Root),
    atomic_list_concat([Root, '/prolog/amb/', File, '.pl'], Path),
    \+ exists_file(Path),
    ambc_diag(D, 'the rules file ~w does not exist', [Path], Diag).

ambc_check(M, Ds, Diag) :-
    member(D, Ds), D = query(_, Goal, Vars, Text),
    (   \+ is_list(Vars) -> ambc_diag(D, 'a query''s answer variables must be a list', [], Diag)
    ;   \+ atom(Text) -> ambc_diag(D, 'a query''s text must be an atom', [], Diag)
    ;   ambc_goal_pred(Goal, G), functor(G, F, A), \+ ambc_known_pred(M, Ds, F/A)
    ->  ambc_diag(D, 'the query calls ~w/~w, which is no fact or derived predicate of the module', [F, A], Diag)
    ).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = generate(Outs),
    member(O, Outs), \+ memberchk(O, [c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants,
                                      c_dispatch, schema, doc]),
    ambc_diag(D, 'the output ~w is not one the generator makes', [O], Diag).

ambc_check(_, Ds, Diag) :-
    member(D, Ds), D = test(Kind, _),
    \+ memberchk(Kind, [contract, gen, exec, safe, reason, integ]),
    ambc_diag(D, 'the test kind ~w is unknown', [Kind], Diag).

%% ---- the rules' vocabulary --------------------------------------------------

ambc_atom_list(L) :- is_list(L), forall(member(X, L), atom(X)).

ambc_dup(L, X) :- append(_, [X|Rest], L), memberchk(X, Rest), !.

ambc_pairs_dup(Pairs, Dup, Decl) :-
    append(_, [Dup-_|Rest], Pairs), member(Dup-Decl, Rest), !.

ambc_prefixed(M, F) :- atom(F), atom_concat(M, '_', P), atom_concat(P, Rest, F), Rest \== ''.

%% the table's own names, which no field may take
ambc_reserved_field(gen).
ambc_reserved_field(live).
ambc_reserved_field(id).
ambc_reserved_field(free_next).

ambc_scalar(u8). ambc_scalar(u16). ambc_scalar(u32). ambc_scalar(u64).
ambc_scalar(i32). ambc_scalar(i64). ambc_scalar(f64). ambc_scalar(bool).

ambc_field_type(_, _, T) :- ambc_scalar(T), !.
ambc_field_type(_, Ds, enum(E)) :- !, memberchk(enum(E, _), Ds).
ambc_field_type(_, Ds, ref(R)) :- !, memberchk(record(R, _), Ds).
ambc_field_type(_, Ds, atom(A)) :- !, member(store(_, Ps), Ds), memberchk(atoms(A), Ps).
ambc_field_type(_, Ds, span(P)) :- !, member(store(_, Ps), Ds), memberchk(pool(P), Ps).

ambc_event_type(_, _, enum(op)) :- !.        % the operations' own enum, generated
ambc_event_type(M, Ds, T) :- ambc_field_type(M, Ds, T).

ambc_param_type(_, _, text) :- !.
ambc_param_type(_, _, T) :- ambc_scalar(T), !.
ambc_param_type(_, Ds, enum(E)) :- !, memberchk(enum(E, _), Ds).
ambc_param_type(_, Ds, handle(R)) :- !, memberchk(handle(R, _), Ds).

ambc_requestable_type(text).
ambc_requestable_type(handle(_)).
ambc_requestable_type(enum(_)).
ambc_requestable_type(T) :- ambc_scalar(T).

ambc_result_type(_, status).
ambc_result_type(_, void).
ambc_result_type(Ds, handle(R)) :- memberchk(handle(R, _), Ds).
ambc_result_type(_, T) :- ambc_scalar(T).

ambc_store_part(Ds, table(R)) :- memberchk(record(R, _), Ds).
ambc_store_part(_, pool(P)) :- atom(P).
ambc_store_part(_, atoms(A)) :- atom(A).
ambc_store_part(Ds, log(E)) :- memberchk(event(E, _), Ds).

ambc_field(Ds, R, F, T) :- member(record(R, Fs), Ds), memberchk(field(F, T), Fs).

ambc_link_ok(Ds, O, I, first(F)) :- ambc_field(Ds, O, F, ref(I)).
ambc_link_ok(Ds, O, I, last(F)) :- ambc_field(Ds, O, F, ref(I)).
ambc_link_ok(Ds, _, I, next(F)) :- ambc_field(Ds, I, F, ref(I)).
ambc_link_ok(Ds, _, I, prev(F)) :- ambc_field(Ds, I, F, ref(I)).
ambc_link_ok(Ds, O, I, up(F)) :- ambc_field(Ds, I, F, ref(O)).

ambc_owns_path(_, X, X, _) :- !.
ambc_owns_path(Ds, From, To, Seen) :-
    member(owns(From, Next, list(_)), Ds), Next \== From, \+ memberchk(Next, Seen),
    ambc_owns_path(Ds, Next, To, [Next|Seen]).

%% a precondition: pre(Cond, Status), Cond over the operation's parameters
ambc_pre_ok(Ds, Ps, pre(C, S)) :-
    member(status(Codes), Ds), memberchk(S, Codes),
    ambc_cond_ok(Ds, Ps, C).

ambc_cond_ok(Ds, Ps, live(P)) :- ambc_handle_param(Ds, Ps, P, _).
ambc_cond_ok(Ds, Ps, kind_in(P, Vs)) :-
    ambc_handle_param(Ds, Ps, P, R), ambc_field(Ds, R, kind, enum(E)),
    member(enum(E, All), Ds), ambc_atom_list(Vs), forall(member(V, Vs), memberchk(V, All)).
ambc_cond_ok(Ds, Ps, unset(P, F)) :- ambc_handle_param(Ds, Ps, P, R), ambc_field(Ds, R, F, ref(_)).
ambc_cond_ok(Ds, Ps, set(P, F)) :- ambc_handle_param(Ds, Ps, P, R), ambc_field(Ds, R, F, ref(_)).
ambc_cond_ok(Ds, Ps, child_of(C, P, L)) :-
    member(list(L, O, I, _), Ds), ambc_handle_param(Ds, Ps, C, I), ambc_handle_param(Ds, Ps, P, O).
ambc_cond_ok(Ds, Ps, not_inclusive_ancestor(A, B, L)) :-
    member(list(L, R, R, _), Ds), ambc_handle_param(Ds, Ps, A, R), ambc_handle_param(Ds, Ps, B, R).
ambc_cond_ok(_, Ps, valid_name(T)) :- memberchk(param(T, text), Ps).

ambc_handle_param(_, Ps, P, R) :- memberchk(param(P, handle(R)), Ps).

%% an invariant's check
ambc_inv_ok(Ds, root(R, F, V)) :- memberchk(root(R, F, V), Ds).
ambc_inv_ok(Ds, list_consistent(L)) :- memberchk(list(L, _, _, _), Ds).
ambc_inv_ok(Ds, acyclic(L)) :- member(list(L, R, R, _), Ds).
ambc_inv_ok(Ds, owner_kinds(L, F, Vs)) :-
    member(list(L, O, _, _), Ds), ambc_field(Ds, O, F, enum(E)),
    member(enum(E, All), Ds), ambc_atom_list(Vs), forall(member(V, Vs), memberchk(V, All)).
ambc_inv_ok(_, refs_live).

%% a fact's argument, and its condition
ambc_fact_arg(Ds, R, handle) :- memberchk(handle(R, _), Ds).
ambc_fact_arg(_, _, doc).
ambc_fact_arg(_, _, rev).
ambc_fact_arg(Ds, R, field(F)) :- ambc_field(Ds, R, F, T), ambc_fact_arg_type(Ds, T).
ambc_fact_arg_type(Ds, ref(R2)) :- !, memberchk(handle(R2, _), Ds).
ambc_fact_arg_type(_, _).

ambc_where_ok(_, _, true) :- !.
ambc_where_ok(Ds, R, (A, B)) :- !, ambc_where_ok(Ds, R, A), ambc_where_ok(Ds, R, B).
ambc_where_ok(Ds, R, field(F) == V) :- !, ambc_where_value(Ds, R, F, V).
ambc_where_ok(Ds, R, field(F) \== V) :- !, ambc_where_value(Ds, R, F, V).
ambc_where_value(Ds, R, F, V) :-
    ambc_field(Ds, R, F, T),
    (   T = enum(E) -> member(enum(E, Vs), Ds), memberchk(V, Vs)
    ;   T = ref(_) -> V == none
    ;   T == bool -> memberchk(V, [true, false])
    ;   ambc_scalar(T) -> integer(V)
    ).

%% the predicates a query's goal calls
ambc_goal_pred(G, _) :- var(G), !, fail.
ambc_goal_pred((A, B), P) :- !, ( ambc_goal_pred(A, P) ; ambc_goal_pred(B, P) ).
ambc_goal_pred((A ; B), P) :- !, ( ambc_goal_pred(A, P) ; ambc_goal_pred(B, P) ).
ambc_goal_pred(\+ A, P) :- !, ambc_goal_pred(A, P).
ambc_goal_pred(G, G) :- callable(G).

ambc_known_pred(M, Ds, F/A) :-
    (   member(fact(H, _, _), Ds), compound(H), functor(H, F, A)
    ;   memberchk(derived(F/A, _), Ds)
    ;   \+ ambc_prefixed(M, F)          % another module's or the system's: not this check's to judge
    ), !.
