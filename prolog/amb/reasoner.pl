%% prolog/amb/reasoner.pl -- the reasoner: cocolog over a snapshot of actual browser state.
%% (the library; prolog/amb/reason.pl is the command over it)
%%
%%   cocolog -s prolog/amb/reason.pl -- check SNAPSHOT            is it a valid snapshot?
%%   cocolog -s prolog/amb/reason.pl -- query SNAPSHOT GOAL       every answer, a line each
%%   cocolog -s prolog/amb/reason.pl -- why SNAPSHOT GOAL         each answer with its proof
%%   cocolog -s prolog/amb/reason.pl -- advise SNAPSHOT           diagnostics and requests
%%   cocolog -s prolog/amb/reason.pl -- diff OLD NEW              the facts that changed
%%   cocolog -s prolog/amb/reason.pl -- serve NONCE               a reasoner kept for a session
%%
%% A SNAPSHOT IS DATA, NEVER CODE. It is read a term at a time with read_term/3,
%% never consulted, so no directive in it runs. Its first
%% term must be amb_snapshot(M, Doc, Rev, Fingerprint) with M's schema loaded
%% (build/schema/M.pl) and Fingerprint that schema's, and every other line a fact
%% the schema declares, each argument of its declared shape (a handle of the
%% snapshot's own document, a value of the enum, an atom, an integer). Anything
%% else refuses the whole snapshot, by line. Page text arrives as quoted atoms:
%% the engine's writer escapes it, and here it is only ever data.
%%
%% THE REASONER PROPOSES, THE ENGINE DECIDES. advise prints amb_request(Doc, Rev,
%% Op) lines: the engine runs one only through its own dispatcher, which holds it
%% to the contract (requestable, the revision of the batch, live handles, the
%% preconditions). Nothing here can touch the engine's memory.
%%
%% Every predicate here is amb_reason_* (or amb_why/2, amb_snapshot_load/2).
:- ensure_loaded(library(amb/contract)).
:- use_module(library(stream)).
:- dynamic('$amb_take'/5).
:- dynamic('$amb_conv'/3).

amb_reason([check, Snap]) :- !,
    amb_snapshot_load(Snap, snap(_, _, _, N)),
    amb_reason_check(N).
amb_reason([query, Snap, GoalText]) :- !,
    amb_snapshot_load(Snap, _),
    amb_reason_query(GoalText).
amb_reason([why, Snap, GoalText]) :- !,
    amb_snapshot_load(Snap, _),
    amb_reason_why(GoalText).
amb_reason([advise, Snap]) :- !,
    amb_snapshot_load(Snap, _),
    amb_reason_advise.
amb_reason([diff, Old, New]) :- !,
    amb_snapshot_facts(Old, [snap(M, Doc, R1, _)|_], F1),
    amb_snapshot_facts(New, [snap(M2, Doc2, R2, _)|_], F2),
    (   M == M2, Doc == Doc2 -> true
    ;   throw(error(amb_snapshot(diff_mismatch(M, Doc, M2, Doc2)), _)) ),
    amb_reason_print_diff(snap(M, Doc, R1, _), snap(M2, Doc2, R2, _), F1, F2).
amb_reason([serve, Nonce]) :- !, amb_reason_serve(Nonce).
amb_reason(Args) :-
    format(user_error, 'reason: unknown command ~q~nusage: check S | query S GOAL | why S GOAL | advise S | diff OLD NEW | serve NONCE~n', [Args]),
    halt(2).

%% ---- what each command prints, over the snapshot held -----------------------------

amb_reason_check(N) :-
    nb_getval(amb_snapshot, Sections),
    forall(member(snap(M, Doc, Rev, _), Sections), format('ok: ~w snapshot of document ~w at revision ~w~n', [M, Doc, Rev])),
    length(Sections, K),
    format('ok: ~d section(s), ~d facts~n', [K, N]).

amb_reason_query(GoalText) :-
    amb_reason_goal(GoalText, Goal, Vs),
    findall(Vs, call(Goal), Answers),
    (   Answers == []
    ->  format('no~n', [])
    ;   forall(member(A, Answers), amb_reason_print_bindings(A))
    ).

amb_reason_why(GoalText) :-
    amb_reason_goal(GoalText, Goal, _),
    findall(Goal-P, amb_why(Goal, P), Ps),
    (   Ps == []
    ->  copy_term(Goal, G2), numbervars(G2, 0, _),
        format('no: ~W cannot be proved from the snapshot~n', [G2, [quoted(true), numbervars(true)]])
    ;   forall(member(_-P, Ps), ( amb_reason_print_proof(P, 0), format('~n', []) ))
    ).

amb_reason_advise :-
    nb_getval(amb_snapshot, Sections),
    forall(member(snap(M, Doc, Rev, _), Sections), amb_reason_advise(M, Doc, Rev)).

%% the facts of one snapshot against another's, each list as the facts were read:
%% what the second lacks, then what it adds, each in the standard order
amb_reason_print_diff(snap(M, Doc, R1, _), snap(_, _, R2, _), F1, F2) :-
    msort(F1, S1), msort(F2, S2),
    ord_subtract(S2, S1, Added), ord_subtract(S1, S2, Removed),
    format('%% ~w document ~w, revision ~w to ~w~n', [M, Doc, R1, R2]),
    forall(member(F, Removed), format('- ~q.~n', [F])),
    forall(member(F, Added), format('+ ~q.~n', [F])).

%% a section's diagnostics and its requests, at its own document and revision
amb_reason_advise(M, Doc, Rev) :-
    atom_concat(M, '_diag', DiagP), atom_concat(M, '_advice', AdvP),
    DG =.. [DiagP, K, S, T],
    findall(K-S-T, catch(DG, error(existence_error(_, _), _), fail), Diags),
    forall(member(K1-S1-T1, Diags), format('amb_diag(~q, ~q, ~q).~n', [K1, S1, T1])),
    AG =.. [AdvP, Op, Why],
    findall(Op-Why, catch(AG, error(existence_error(_, _), _), fail), Advice),
    forall(member(Op1-Why1, Advice),
           ( format('%% ~w~n', [Why1]), format('amb_request(~w, ~w, ~q).~n', [Doc, Rev, Op1]) )).

amb_reason_goal(Text, Goal, Vs) :-
    catch(read_term_from_atom(Text, Goal, [variable_names(Vs)]), E,
          throw(error(amb_snapshot(bad_goal(Text, E)), _))).

amb_reason_print_bindings([]) :- !, format('yes~n', []).
amb_reason_print_bindings(Vs) :-
    findall(A, ( member(N=V, Vs), format(atom(A), '~w = ~q', [N, V]) ), As),
    atomic_list_concat(As, ', ', Line),
    format('~w~n', [Line]).

amb_reason_error(error(amb_snapshot(Why), _)) :- !,
    format(user_error, 'reason: refused: ~q~n', [Why]).
amb_reason_error(E) :- format(user_error, 'reason: ~q~n', [E]).

%% ---- loading a snapshot -----------------------------------------------------------

%% the snapshot's facts asserted, each section's module's rules loaded. A snapshot
%% is one or more sections, each a header amb_snapshot(M, Doc, Rev, FP) and M's
%% facts: a page's DOM and its parse, say. Snap is the first section, its count
%% the snapshot's whole. Each fact is stored as it is read (PLAN.md section 11).
amb_snapshot_load(File, snap(M, Doc, Rev, N)) :-
    amb_reason_read_snapshot(File, store, Sections, _, N),
    amb_reason_hold(Sections, N),
    Sections = [snap(M, Doc, Rev, _)|_].

%% the sections read held, their count of facts kept, each module's rules loaded
amb_reason_hold(Sections, N) :-
    nb_setval(amb_snapshot, Sections),
    nb_setval(amb_reason_count, N),
    forall(member(snap(SM2, _, _, _), Sections), amb_reason_rules(SM2)).

%% THE FACTS ARE KEPT KEYED BY THEIR HANDLES' INDEXES. cocolog indexes a call by an
%% argument's own value and keys nothing inside a compound (toolchain/REPORT.md,
%% finding 13), so a fact whose arguments are handles -- n(Doc, Index, Gen) -- was
%% found by walking every fact of its predicate. A fact predicate F/A with handles
%% at positions P1..Pk is therefore stored as '$amb_F'(K1, ..., Kk, A1, ..., An), Ki
%% the integer index of the handle at Pi (0 for none), and F/A itself is one clause
%% that works out the keys of the handles it is called with and calls the store. A
%% rule, a query, a proof and a printed answer see F/A as before.
amb_reason_store_decl(F, A, Shapes) :-
    findall(P, ( nth1(P, Shapes, S), amb_reason_keyed_shape(S) ), Ps),
    functor(H, F, A), amb_reason_empty(F, A),
    retractall('$amb_conv'(F, _, _)),
    (   Ps == [] -> nb_setval(F, plain), assertz('$amb_conv'(F, X, X))
    ;   length(Ps, K), atom_concat('$amb_', F, SF), SA is K + A,
        amb_reason_empty(SF, SA),
        nb_setval(F, keyed(SF, Ps)),
        H =.. [F|Vs], amb_reason_pick(Ps, Vs, KPs),         % not findall/3: it would copy the head's variables
        amb_reason_key_goals(KPs, Keys, Goals),
        append(Keys, Vs, SArgs), Stored =.. [SF|SArgs],
        amb_reason_conj(Goals, Stored, Body),
        assertz((H :- Body)),
        %% and the fact as it is stored, from the fact as it is read: its keys worked out
        amb_reason_conj(Goals, true, KeyBody),
        assertz(('$amb_conv'(F, H, Stored) :- KeyBody))
    ).

%% F/A, an empty dynamic predicate. abolish/1 and not retractall/1: cocolog's
%% retractall/1 takes some 16 us a clause, abolish/1 drops them all at once, and a
%% session's snapshot replaces predicates of thousands of clauses
amb_reason_empty(F, A) :- abolish(F/A), dynamic(F/A).

amb_reason_keyed_shape(handle(_)).
amb_reason_keyed_shape(handle_or_none(_)).
amb_reason_keyed_shape(xhandle(_, _)).

%% the head's variables at the keyed positions, themselves
amb_reason_pick([], _, []).
amb_reason_pick([P|Ps], Vs, [V-P|Rest]) :- nth1(P, Vs, V), amb_reason_pick(Ps, Vs, Rest).

amb_reason_key_goals([], [], []).
amb_reason_key_goals([V-_|Rest], [K|Ks], [amb_reason_key(V, K)|Gs]) :- amb_reason_key_goals(Rest, Ks, Gs).

amb_reason_conj([], G, G).
amb_reason_conj([A|As], G, (A, B)) :- amb_reason_conj(As, G, B).

%% a handle's key: its index; none's 0; unbound, unbound
amb_reason_key(V, K) :-
    (   var(V) -> true
    ;   V == none -> K = 0
    ;   compound(V), arg(2, V, I), integer(I) -> K = I
    ;   K = -1
    ).

amb_reason_store(T) :-
    functor(T, F, _),
    (   nb_getval(F, keyed(SF, Ps))
    ->  T =.. [_|Args], findall(K, ( member(P, Ps), nth1(P, Args, V), amb_reason_key(V, K) ), Keys),
        append(Keys, Args, SArgs), S =.. [SF|SArgs], assertz(S)
    ;   assertz(T)
    ).

%% the snapshot read and validated: its sections and its facts, in their order
amb_snapshot_facts(File, Sections, Facts) :-
    amb_reason_read_snapshot(File, collect, Sections, Facts, _).

amb_reason_header(amb_snapshot(M, Doc, Rev, FP)) :- atom(M), integer(Doc), integer(Rev), atom(FP).

%% THE READER. The file is read a term at a time with read_term/3, never consulted,
%% so no directive in it runs and only data is read. Each term is checked as it
%% comes: ground; the first a header; a header's schema loaded and its fingerprint
%% the schema's; every other term a fact its section's schema declares, each
%% argument of its declared shape. Anything else refuses the whole snapshot. The
%% declarations are indexed by predicate with their shapes resolved once, and in
%% mode store each fact is stored as it is read; in mode collect they are returned.
amb_reason_read_snapshot(File, Mode, Sections, Facts, N) :-
    (   exists_file(File) -> true ; throw(error(amb_snapshot(no_file(File)), _)) ),
    open(File, read, S),
    catch(amb_reason_read_all(S, File, end_of_file, Mode, Sections, Facts, N), E, ( close(S), throw(E) )),
    close(S).

%% the snapshot on the stream S up to the term End: end_of_file in a file, amb_end on a
%% session's standard input, where a snapshot is one request among others
amb_reason_read_all(S, File, End, Mode, Sections, Facts, N) :-
    amb_reason_next(S, T),
    (   T == End -> nb_setval(amb_reason_at_end, true), throw(error(amb_snapshot(empty(File)), _)) ; true ),
    (   amb_reason_header(T) -> true ; throw(error(amb_snapshot(no_header(T)), _)) ),
    nb_setval(amb_reason_open, []),
    amb_reason_section(T, Mode, Sec, Snap),
    amb_reason_terms(S, End, Mode, Sec, [Snap], Sections, Facts, 0, N).

amb_reason_terms(S, End, Mode, Sec, Acc, Sections, Facts, N0, N) :-
    amb_reason_next(S, T),
    (   T == End
    ->  nb_setval(amb_reason_at_end, true), reverse(Acc, Sections), Facts = [], N = N0
    ;   T == end_of_file
    ->  nb_setval(amb_reason_at_end, true), throw(error(amb_snapshot(truncated), _))
    ;   T = amb_snapshot(_, _, _, _)
    ->  (   amb_reason_header(T) -> true ; throw(error(amb_snapshot(no_header(T)), _)) ),
        amb_reason_section(T, Mode, Sec1, Snap),
        amb_reason_terms(S, End, Mode, Sec1, [Snap|Acc], Sections, Facts, N0, N)
    ;   amb_reason_fact(S, T, Sec, Mode, Facts, Rest),
        N1 is N0 + 1,
        amb_reason_terms(S, End, Mode, Sec, Acc, Sections, Rest, N1, N)
    ).

%% the next term, ground, or end_of_file. A read that ran out of memory says so: it
%% is no syntax error
amb_reason_next(S, T) :-
    catch(read_term(S, T, []), E,
          (   E = error(resource_error(_), _) -> throw(E)
          ;   line_count(S, L), throw(error(amb_snapshot(syntax(line(L), E)), _)) )),
    (   T == end_of_file -> true
    ;   ground(T) -> true
    ;   line_count(S, L2), throw(error(amb_snapshot(not_ground(line(L2), T)), _))
    ).

%% a section opened: its schema loaded, its fingerprint the schema's; the first time
%% its module is met, its facts' declarations indexed and, in mode store, its fact
%% predicates made ready to store into
amb_reason_section(amb_snapshot(M, Doc, Rev, FP), Mode, sec(M, Doc), snap(M, Doc, Rev, FP)) :-
    amb_reason_schema(M),
    amb_reason_schema_decl(M, module(M, _, SchemaFP)),
    (   FP == SchemaFP -> true
    ;   throw(error(amb_snapshot(fingerprint(M, snapshot(FP), schema(SchemaFP))), _)) ),
    nb_getval(amb_reason_open, Open),
    (   memberchk(M, Open) -> true
    ;   nb_setval(amb_reason_open, [M|Open]),
        amb_reason_schema_facts(M, Decls),
        forall(member(fact(F/A, Shapes), Decls),
               ( maplist(amb_reason_cshape(M), Shapes, CS),
                 retractall('$amb_take'(F, M, _, _, _)),
                 amb_reason_take_clause(M, F, A, Shapes, CS, Clause),
                 assertz(Clause),
                 ( Mode == store -> amb_reason_store_decl(F, A, Shapes) ; true ) ))
    ).

%% THE CHECK OF A FACT, MADE ONCE A PREDICATE: one clause, '$amb_take'(F, M, Fact,
%% Doc, Stored), whose head takes the fact apart and whose body tests each argument
%% against its declared shape (a handle of the section's document, a value of the
%% enum, an atom, an integer) and works out the stored form's keys, as
%% amb_reason_store_decl/3 keeps the fact
amb_reason_take_clause(M, F, A, Shapes, CS, ('$amb_take'(F, M, T, Doc, St) :- Body)) :-
    functor(T, F, A), T =.. [_|Xs],
    amb_reason_take_goals(Xs, CS, Doc, Goals, Keys),
    (   Keys == [] -> St = T
    ;   atom_concat('$amb_', F, SF), append(Keys, Xs, SArgs), St =.. [SF|SArgs]
    ),
    length(Shapes, A),
    amb_reason_conj(Goals, true, Body).

amb_reason_take_goals([], [], _, [], []).
amb_reason_take_goals([X|Xs], [S|Ss], Doc, Goals, Keys) :-
    amb_reason_take_goal(S, X, Doc, G, K),
    amb_reason_take_goals(Xs, Ss, Doc, Gs, Ks),
    Goals = [G|Gs],
    ( var(K) -> Keys = [K|Ks] ; Keys = Ks ).

%% an argument's test, and its key when the argument is a handle
amb_reason_take_goal(h(Fn), X, Doc, (X = P, D == Doc, integer(I), I > 0, integer(G), G >= 0), I) :- !, P =.. [Fn, D, I, G].
amb_reason_take_goal(hn(Fn), X, Doc, ( X == none -> K = 0 ; X = P, D == Doc, integer(I), I > 0, integer(G), G >= 0, K = I ), K) :- !,
    P =.. [Fn, D, I, G].
amb_reason_take_goal(x(Fn), X, _, ( X == none -> K = 0 ; X = P, integer(D), integer(I), integer(G), K = I ), K) :- !, P =.. [Fn, D, I, G].
amb_reason_take_goal(e(Vs), X, _, (atom(X), memberchk(X, Vs)), none) :- !.
amb_reason_take_goal(atom, X, _, atom(X), none) :- !.
amb_reason_take_goal(integer, X, _, integer(X), none) :- !.
amb_reason_take_goal(number, X, _, number(X), none).

%% a declared shape, resolved: a handle's functor, an enum's values
amb_reason_cshape(M, handle(R), h(F)) :- !, amb_reason_schema_decl(M, handle(R, F)).
amb_reason_cshape(M, handle_or_none(R), hn(F)) :- !, amb_reason_schema_decl(M, handle(R, F)).
amb_reason_cshape(_, xhandle(M2, R), x(F)) :- !, amb_reason_schema(M2), amb_reason_schema_decl(M2, handle(R, F)).
amb_reason_cshape(M, enum(E), e(Vs)) :- !, amb_reason_schema_decl(M, enum(E, Vs)).
amb_reason_cshape(_, enum_values(Vs), e(Vs)) :- !.
amb_reason_cshape(_, S, S).

%% a fact of the section, checked, then stored or kept
amb_reason_fact(S, T, sec(M, Doc), Mode, Facts, Rest) :-
    (   compound(T), functor(T, F, _), '$amb_take'(F, M, T, Doc, St)
    ->  true
    ;   line_count(S, L), throw(error(amb_snapshot(undeclared_or_malformed(line(L), T)), _))
    ),
    (   Mode == store -> assertz(St), Facts = Rest
    ;   Facts = [T|Rest]
    ).


%% the schema of M: build/schema/M.pl, made by `make schema' from the contract
amb_reason_schema(M) :-
    atom_concat(M, '_schema', P),
    (   current_predicate(P/1) -> true
    ;   amb_contract_root(Root),
        atomic_list_concat([Root, '/build/schema/', M, '.pl'], File),
        (   exists_file(File) -> ensure_loaded(File)
        ;   throw(error(amb_snapshot(no_schema(M, File)), _)) )
    ).

amb_reason_schema_decl(M, D) :- atom_concat(M, '_schema', P), G =.. [P, D], call(G).

amb_reason_schema_facts(M, Decls) :- findall(fact(PI, S), amb_reason_schema_decl(M, fact(PI, S)), Decls).

amb_reason_rules(M) :-
    (   amb_reason_schema_decl(M, rules(R))
    ->  atomic_list_concat(['amb/', R], Lib), atom_to_term(Lib, LibT, _),
        ensure_loaded(library(LibT))
    ;   true
    ).

%% ---- the session: one reasoner for many requests ----------------------------------
%%
%% `serve NONCE' keeps the reasoner for a session (src/rt/amb_proc.c is the engine's
%% side). Each request is a term on the standard input. Its answer is what the command
%% of its name prints, then the line amb_done(NONCE, Status): Status 0 answered, 1
%% failed, 2 refused, with the reason on the standard error as a command gives it. The
%% nonce is the engine's, so no text an answer prints can end the answer early.
%%
%%   amb_load.  SNAPSHOT  amb_end.     a whole snapshot, in place of the one held
%%   amb_delta.  DELTA  amb_end.       what changed since the snapshot held
%%   amb_delta(changes).  DELTA  amb_end.   the same, the facts it changes kept for `changes'
%%       each answered held(N): the facts held now, which the engine counts too
%%   check.  query(Goal).  why(Goal).  advise.
%%                                     as the commands of those names print them
%%   changes.                          the facts the last delta changed, as diff prints them
%%   quit.                             (or the end of the input)
%%
%% A DELTA KEEPS EACH PREDICATE'S FACTS IN THE ORDER A LOAD OF THE WHOLE SNAPSHOT GIVES,
%% so every answer is the one a fresh reasoner gives over the whole snapshot. A delta
%% is each section's header, in the order held (its revision moves on), and after a
%% header the predicates of its module that changed, each in one of two ways:
%%
%%   amb_edit(F).     then -(Fact) and +(Fact): a -(Fact) retracts the fact equal to it,
%%                    a +(Fact) is added after the facts held. The engine edits only
%%                    where that keeps the order: every fact added comes after every
%%                    fact kept, and no fact retracted has a twin
%%   amb_rebuild(F).  then amb_keep(Start, Count) and +(Fact), in the order of the new
%%                    facts: a run of Count facts held from Start (the first is 0), and
%%                    the facts added between the runs. The facts held and kept by no
%%                    run are retracted, and F's clauses are made again in that order
%%
%% Every fact of a delta is checked as a snapshot's is. A delta that does not fit the
%% snapshot held (a header out of place, a fact retracted that is not held, a run past
%% the facts held) is refused whole: the reasoner then holds nothing, and the engine
%% sends the whole snapshot.

amb_reason_serve(Nonce) :-
    nb_setval(amb_snapshot, []),
    nb_setval(amb_reason_count, 0),
    nb_setval(amb_reason_changes, none),
    repeat,
    catch(read_term(user_input, Req0, []), E, Req0 = amb_unreadable(E)),
    (   Req0 == end_of_file -> !
    ;   Req0 == quit -> !, amb_reason_done(Nonce, 0)
    ;   amb_reason_answer(Req0, St), amb_reason_done(Nonce, St), fail
    ).

amb_reason_done(Nonce, St) :- format('amb_done(~w, ~w).~n', [Nonce, St]), flush_output.

%% a request answered: 0, or 2 refused (its reason said), or 1 failed
amb_reason_answer(Req, St) :-
    (   catch(amb_reason_serve_one(Req), E, ( amb_reason_error(E), St0 = 2 ))
    ->  ( var(St0) -> St = 0 ; St = St0 )
    ;   format(user_error, 'reason: ~q failed~n', [Req]), St = 1
    ).

amb_reason_serve_one(amb_load) :- !, amb_reason_load_from(user_input, N), format('held(~d).~n', [N]).
amb_reason_serve_one(amb_delta) :- !, amb_reason_delta_from(user_input, false, N), format('held(~d).~n', [N]).
amb_reason_serve_one(amb_delta(changes)) :- !, amb_reason_delta_from(user_input, true, N), format('held(~d).~n', [N]).
amb_reason_serve_one(check) :- !, amb_reason_held, nb_getval(amb_reason_count, N), amb_reason_check(N).
amb_reason_serve_one(query(G)) :- !, amb_reason_held, amb_reason_text(G), amb_reason_query(G).
amb_reason_serve_one(why(G)) :- !, amb_reason_held, amb_reason_text(G), amb_reason_why(G).
amb_reason_serve_one(advise) :- !, amb_reason_held, amb_reason_advise.
amb_reason_serve_one(changes) :- !, amb_reason_changes.
amb_reason_serve_one(amb_unreadable(E)) :- !, throw(error(amb_snapshot(unreadable_request(E)), _)).
amb_reason_serve_one(R) :- throw(error(amb_snapshot(unknown_request(R)), _)).

amb_reason_held :-
    nb_getval(amb_snapshot, S),
    (   S == [] -> throw(error(amb_snapshot(nothing_held), _)) ; true ).

amb_reason_text(G) :- ( atom(G) -> true ; throw(error(amb_snapshot(bad_goal(G, not_an_atom)), _)) ).

%% the facts the last delta changed, as diff prints them
amb_reason_changes :-
    nb_getval(amb_reason_changes, C),
    (   C = changes(H0, H1, Removed, Added) -> amb_reason_print_diff(H0, H1, Removed, Added)
    ;   throw(error(amb_snapshot(no_delta), _)) ).

%% after a refusal: nothing held, and the request's terms on S read up to its amb_end
amb_reason_forget(S) :-
    nb_setval(amb_snapshot, []),
    nb_setval(amb_reason_changes, none),
    (   nb_current(amb_reason_at_end, true) -> true
    ;   repeat,
        catch(read_term(S, T, []), _, T = unreadable),
        ( T == amb_end ; T == end_of_file ), !
    ).

%% a whole snapshot read from the stream S up to amb_end, held in place of the one held:
%% N its facts
amb_reason_load_from(S, N) :-
    nb_setval(amb_snapshot, []),
    nb_setval(amb_reason_changes, none),
    nb_setval(amb_reason_at_end, false),
    catch(amb_reason_read_all(S, session, amb_end, store, Sections, _, N), E,
          ( amb_reason_forget(S), throw(E) )),
    amb_reason_hold(Sections, N).

%% a delta read from the stream S up to amb_end and applied to the snapshot held: N the
%% facts held after it. With Track, the facts it retracted and added are kept for
%% `changes'; without, only counted, which spares a copy of each
amb_reason_delta_from(S, Track, N) :-
    amb_reason_held,
    nb_getval(amb_snapshot, Held),
    nb_setval(amb_reason_at_end, false),
    nb_setval(amb_reason_changes, none),
    (   Track == true -> Z = [] ; Z = 0 ),
    catch(( amb_reason_next(S, T), amb_reason_dt(S, T, Held, none, none, [], Z, Z, New, Removed, Added) ), E,
          ( amb_reason_forget(S), throw(E) )),
    nb_getval(amb_reason_count, N0),
    amb_reason_noted(Removed, R), amb_reason_noted(Added, A),
    N is N0 - R + A,
    nb_setval(amb_snapshot, New),
    nb_setval(amb_reason_count, N),
    (   Track == true
    ->  Held = [H0|_], New = [H1|_], nb_setval(amb_reason_changes, changes(H0, H1, Removed, Added))
    ;   true ).

%% a fact retracted or added, noted: kept in the list, or counted
amb_reason_note(L0, Fact, L) :- ( integer(L0) -> L is L0 + 1 ; L = [Fact|L0] ).
amb_reason_noted(L, N) :- ( integer(L) -> N = L ; length(L, N) ).

%% amb_reason_dt(S, T, Held, Sec, Block, NewAcc, Rm0, Ad0, New, Rm, Ad): the delta's term T
%% and the terms after it on S. Held are the sections held whose headers are still to
%% come, Sec the section the edits are of, Block the predicate they change; New the
%% sections' new headers, Rm and Ad the facts retracted and added
amb_reason_dt(_, amb_end, Held, _, Block, NewAcc, Rm0, Ad0, New, Rm, Ad) :- !,
    nb_setval(amb_reason_at_end, true),
    (   Held == [] -> true ; throw(error(amb_snapshot(bad_delta(sections_missing(Held))), _)) ),
    amb_reason_block_end(Block, Rm0, Rm),
    Ad = Ad0,
    reverse(NewAcc, New).
amb_reason_dt(_, end_of_file, _, _, _, _, _, _, _, _, _) :- !,
    nb_setval(amb_reason_at_end, true),
    throw(error(amb_snapshot(truncated), _)).
amb_reason_dt(S, amb_snapshot(M, Doc, Rev, FP), Held, _, Block, NewAcc, Rm0, Ad0, New, Rm, Ad) :- !,
    amb_reason_block_end(Block, Rm0, Rm1),
    (   Held = [snap(M, Doc, _, _)|Held1], amb_reason_header(amb_snapshot(M, Doc, Rev, FP)) -> true
    ;   throw(error(amb_snapshot(bad_delta(section(M, Doc))), _)) ),
    amb_reason_schema_decl(M, module(M, _, SFP)),
    (   FP == SFP -> true ; throw(error(amb_snapshot(fingerprint(M, snapshot(FP), schema(SFP))), _)) ),
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held1, sec(M, Doc), none, [snap(M, Doc, Rev, FP)|NewAcc], Rm1, Ad0, New, Rm, Ad).
amb_reason_dt(S, amb_edit(F), Held, Sec, Block, NewAcc, Rm0, Ad0, New, Rm, Ad) :- !,
    amb_reason_block_end(Block, Rm0, Rm1),
    amb_reason_block_pred(Sec, F, A),
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held, Sec, edit(F, A), NewAcc, Rm1, Ad0, New, Rm, Ad).
amb_reason_dt(S, amb_rebuild(F), Held, Sec, Block, NewAcc, Rm0, Ad0, New, Rm, Ad) :- !,
    amb_reason_block_end(Block, Rm0, Rm1),
    amb_reason_block_pred(Sec, F, A),
    amb_reason_stored(F, A, SF, SA),
    functor(SH, SF, SA),
    findall(SH, SH, Old),
    amb_reason_empty(SF, SA),
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held, Sec, rebuild(F, A, Old, 0), NewAcc, Rm1, Ad0, New, Rm, Ad).
amb_reason_dt(S, -(Fact), Held, Sec, edit(F, A), NewAcc, Rm0, Ad0, New, Rm, Ad) :- !,
    amb_reason_delta_take(Sec, F, Fact, St),
    (   retract(St) -> true ; throw(error(amb_snapshot(bad_delta(not_held(Fact))), _)) ),
    amb_reason_note(Rm0, Fact, Rm1),
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held, Sec, edit(F, A), NewAcc, Rm1, Ad0, New, Rm, Ad).
amb_reason_dt(S, +(Fact), Held, Sec, Block, NewAcc, Rm0, Ad0, New, Rm, Ad) :-
    ( Block = edit(F, _) ; Block = rebuild(F, _, _, _) ), !,
    amb_reason_delta_take(Sec, F, Fact, St),
    assertz(St),
    amb_reason_note(Ad0, Fact, Ad1),
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held, Sec, Block, NewAcc, Rm0, Ad1, New, Rm, Ad).
amb_reason_dt(S, amb_keep(K, C), Held, Sec, rebuild(F, A, Old, Cur), NewAcc, Rm0, Ad0, New, Rm, Ad) :-
    integer(K), integer(C), K >= Cur, C >= 1, !,
    Skip is K - Cur,
    amb_reason_drop(Skip, F, A, Old, Old1, Rm0, Rm1),
    amb_reason_keep(C, Old1, Old2),
    Cur1 is K + C,
    amb_reason_next(S, T1),
    amb_reason_dt(S, T1, Held, Sec, rebuild(F, A, Old2, Cur1), NewAcc, Rm1, Ad0, New, Rm, Ad).
amb_reason_dt(_, T, _, _, _, _, _, _, _, _, _) :- throw(error(amb_snapshot(bad_delta(T)), _)).

%% the predicate a block changes: a fact its section's module declares
amb_reason_block_pred(sec(M, _), F, A) :-
    atom(F), amb_reason_schema_decl(M, fact(F/A, _)), !.
amb_reason_block_pred(Sec, F, _) :- throw(error(amb_snapshot(bad_delta(not_a_fact(Sec, F))), _)).

%% a block ended: the facts a rebuild kept no run of, retracted
amb_reason_block_end(rebuild(F, A, Old, _), Rm0, Rm) :- !, amb_reason_drop_all(F, A, Old, Rm0, Rm).
amb_reason_block_end(_, Rm, Rm).

%% a fact of the delta, checked as a snapshot's: of the block's predicate, its section's
%% shapes; and the form it is stored in
amb_reason_delta_take(sec(M, Doc), F, Fact, St) :-
    (   compound(Fact), functor(Fact, F, _), '$amb_take'(F, M, Fact, Doc, St) -> true
    ;   throw(error(amb_snapshot(bad_delta(malformed(Fact))), _)) ).

%% the predicate F/A's facts are stored in: itself, or '$amb_F' with the keys first
amb_reason_stored(F, A, SF, SA) :-
    (   nb_getval(F, keyed(SF, Ps)) -> length(Ps, K), SA is K + A ; SF = F, SA = A ).

%% N stored facts dropped from the list, each noted as a fact retracted
amb_reason_drop(0, _, _, L, L, Rm, Rm) :- !.
amb_reason_drop(N, _, _, L, L1, Rm0, Rm) :- integer(Rm0), !,
    (   length(Pre, N), append(Pre, L1, L) -> Rm is Rm0 + N
    ;   throw(error(amb_snapshot(bad_delta(run_past_the_facts_held)), _)) ).
amb_reason_drop(N, F, A, [St|L], L1, Rm0, Rm) :- !,
    amb_reason_unstore(F, A, St, Fact),
    N1 is N - 1,
    amb_reason_drop(N1, F, A, L, L1, [Fact|Rm0], Rm).
amb_reason_drop(_, _, _, [], _, _, _) :- throw(error(amb_snapshot(bad_delta(run_past_the_facts_held)), _)).

%% the rest of the stored facts, dropped
amb_reason_drop_all(_, _, L, Rm0, Rm) :- integer(Rm0), !, length(L, K), Rm is Rm0 + K.
amb_reason_drop_all(_, _, [], Rm, Rm).
amb_reason_drop_all(F, A, [St|L], Rm0, Rm) :-
    amb_reason_unstore(F, A, St, Fact),
    amb_reason_drop_all(F, A, L, [Fact|Rm0], Rm).

%% N stored facts of the list kept: stored again, in order
amb_reason_keep(0, L, L) :- !.
amb_reason_keep(N, [St|L], L1) :- !, assertz(St), N1 is N - 1, amb_reason_keep(N1, L, L1).
amb_reason_keep(_, [], _) :- throw(error(amb_snapshot(bad_delta(run_past_the_facts_held)), _)).

%% a stored fact as the fact it stands for: its keys taken off
amb_reason_unstore(F, A, St, Fact) :-
    (   nb_getval(F, keyed(_, Ps))
    ->  length(Ps, K), St =.. [_|Args], length(Keys, K), append(Keys, FArgs, Args), length(FArgs, A),
        Fact =.. [F|FArgs]
    ;   Fact = St ).

%% ---- explanations -----------------------------------------------------------------
%% amb_why(Goal, Proof): Goal proved from the snapshot, with the proof that shows why.
%% A fact of the snapshot is a leaf, fact(G); a derived predicate is
%% by(G, BodyProof), from the clause that proved it; a test the system answers is
%% builtin(G); \+ G proved by G's failure is not(G).

amb_why(G, P) :- amb_reason_prove(G, P).

amb_reason_prove(G, _) :- var(G), !, throw(error(instantiation_error, amb_why/2)).
amb_reason_prove(true, true) :- !.
amb_reason_prove((A, B), and(PA, PB)) :- !, amb_reason_prove(A, PA), amb_reason_prove(B, PB).
amb_reason_prove((C -> T ; E), P) :- !,
    (   amb_reason_prove(C, PC) -> amb_reason_prove(T, PT), P = and(PC, PT) ; amb_reason_prove(E, P) ).
amb_reason_prove((A ; B), P) :- !, ( amb_reason_prove(A, P) ; amb_reason_prove(B, P) ).
amb_reason_prove(\+ G, not(G)) :- !, \+ amb_reason_prove(G, _).
amb_reason_prove(once(G), P) :- !, once(amb_reason_prove(G, P)).
amb_reason_prove(G, fact(G)) :- amb_reason_is_fact(G), !, call(G).
amb_reason_prove(G, by(G, P)) :- amb_reason_is_rule(G), !, clause(G, Body), amb_reason_prove(Body, P).
amb_reason_prove(G, builtin(G)) :- call(G).

amb_reason_is_fact(G) :-
    nb_current(amb_snapshot, Sections), member(snap(M, _, _, _), Sections),
    functor(G, F, A), amb_reason_schema_decl(M, fact(F/A, _)), !.

%% a predicate of a module's rules: the module's prefix, and clauses to read
amb_reason_is_rule(G) :-
    nb_current(amb_snapshot, Sections), member(snap(M, _, _, _), Sections),
    functor(G, F, _), atom_concat(M, '_', Pre), atom_concat(Pre, _, F),
    catch(clause(G, _), _, fail), !.

%% the proof as an indented tree: a derived goal, then what it rests on
amb_reason_print_proof(true, _) :- !.
amb_reason_print_proof(and(A, B), D) :- !, amb_reason_print_proof(A, D), amb_reason_print_proof(B, D).
amb_reason_print_proof(fact(G), D) :- !, amb_reason_indent(D), amb_reason_show(G), format('   [fact]~n', []).
amb_reason_print_proof(builtin(G), D) :- !, amb_reason_indent(D), amb_reason_show(G), format('   [test]~n', []).
amb_reason_print_proof(not(G), D) :- !, amb_reason_indent(D), format('not ', []), amb_reason_show(G), format('   [no proof]~n', []).
amb_reason_print_proof(by(G, true), D) :- !, amb_reason_indent(D), amb_reason_show(G), format('   [rule]~n', []).
amb_reason_print_proof(by(G, P), D) :- !,
    amb_reason_indent(D), amb_reason_show(G), format('   because~n', []),
    D1 is D + 1, amb_reason_print_proof(P, D1).

%% a goal as a proof shows it: its variables named A, B, ... in order, the same each run
amb_reason_show(G) :- copy_term(G, G2), numbervars(G2, 0, _), format('~W', [G2, [quoted(true), numbervars(true)]]).

amb_reason_indent(D) :- forall(between(1, D, _), format('  ', [])).
