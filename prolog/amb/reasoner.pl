%% prolog/amb/reasoner.pl -- the reasoner: cocolog over a snapshot of actual browser state.
%% (the library; prolog/amb/reason.pl is the command over it)
%%
%%   cocolog -s prolog/amb/reason.pl -- check SNAPSHOT            is it a valid snapshot?
%%   cocolog -s prolog/amb/reason.pl -- query SNAPSHOT GOAL       every answer, a line each
%%   cocolog -s prolog/amb/reason.pl -- why SNAPSHOT GOAL         each answer with its proof
%%   cocolog -s prolog/amb/reason.pl -- advise SNAPSHOT           diagnostics and requests
%%   cocolog -s prolog/amb/reason.pl -- diff OLD NEW              the facts that changed
%%
%% A SNAPSHOT IS DATA, NEVER CODE. It is read a line at a time with
%% read_term_from_atom/3, never consulted, so no directive in it runs. Its first
%% line must be amb_snapshot(M, Doc, Rev, Fingerprint) with M's schema loaded
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

amb_reason([check, Snap]) :- !,
    amb_snapshot_load(Snap, snap(_, _, _, N)),
    nb_getval(amb_snapshot, Sections),
    forall(member(snap(M, Doc, Rev, _), Sections), format('ok: ~w snapshot of document ~w at revision ~w~n', [M, Doc, Rev])),
    length(Sections, K),
    format('ok: ~d section(s), ~d facts~n', [K, N]).
amb_reason([query, Snap, GoalText]) :- !,
    amb_snapshot_load(Snap, _),
    amb_reason_goal(GoalText, Goal, Vs),
    findall(Vs, call(Goal), Answers),
    (   Answers == []
    ->  format('no~n', [])
    ;   forall(member(A, Answers), amb_reason_print_bindings(A))
    ).
amb_reason([why, Snap, GoalText]) :- !,
    amb_snapshot_load(Snap, _),
    amb_reason_goal(GoalText, Goal, _),
    findall(Goal-P, amb_why(Goal, P), Ps),
    (   Ps == []
    ->  copy_term(Goal, G2), numbervars(G2, 0, _),
        format('no: ~W cannot be proved from the snapshot~n', [G2, [quoted(true), numbervars(true)]])
    ;   forall(member(_-P, Ps), ( amb_reason_print_proof(P, 0), format('~n', []) ))
    ).
amb_reason([advise, Snap]) :- !,
    amb_snapshot_load(Snap, _),
    nb_getval(amb_snapshot, Sections),
    forall(member(snap(M, Doc, Rev, _), Sections), amb_reason_advise(M, Doc, Rev)).
amb_reason([diff, Old, New]) :- !,
    amb_snapshot_facts(Old, [snap(M, Doc, R1, _)|_], F1),
    amb_snapshot_facts(New, [snap(M2, Doc2, R2, _)|_], F2),
    (   M == M2, Doc == Doc2 -> true
    ;   throw(error(amb_snapshot(diff_mismatch(M, Doc, M2, Doc2)), _)) ),
    msort(F1, S1), msort(F2, S2),
    ord_subtract(S2, S1, Added), ord_subtract(S1, S2, Removed),
    format('%% ~w document ~w, revision ~w to ~w~n', [M, Doc, R1, R2]),
    forall(member(F, Removed), format('- ~q.~n', [F])),
    forall(member(F, Added), format('+ ~q.~n', [F])).
amb_reason(Args) :-
    format(user_error, 'reason: unknown command ~q~nusage: check S | query S GOAL | why S GOAL | advise S | diff OLD NEW~n', [Args]),
    halt(2).

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
%% the snapshot's whole
amb_snapshot_load(File, snap(M, Doc, Rev, N)) :-
    amb_snapshot_facts(File, Sections, Facts),
    Sections = [snap(M, Doc, Rev, _)|_],
    forall(member(snap(SM, _, _, _), Sections),
           ( amb_reason_schema_facts(SM, Decls),
             forall(member(fact(F/A, Shapes), Decls), amb_reason_store_decl(F, A, Shapes)) )),
    forall(member(Fact, Facts), amb_reason_store(Fact)),
    length(Facts, N),
    nb_setval(amb_snapshot, Sections),
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
    functor(H, F, A), retractall(H), dynamic(F/A),
    (   Ps == [] -> nb_setval(F, plain)
    ;   length(Ps, K), atom_concat('$amb_', F, SF), SA is K + A,
        functor(SH, SF, SA), retractall(SH), dynamic(SF/SA),
        nb_setval(F, keyed(SF, Ps)),
        H =.. [F|Vs], amb_reason_pick(Ps, Vs, KPs),         % not findall/3: it would copy the head's variables
        amb_reason_key_goals(KPs, Keys, Goals),
        append(Keys, Vs, SArgs), Stored =.. [SF|SArgs],
        amb_reason_conj(Goals, Stored, Body),
        assertz((H :- Body))
    ).

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
    (   exists_file(File) -> true ; throw(error(amb_snapshot(no_file(File)), _)) ),
    amb_reason_lines(File, Lines0),
    exclude(amb_reason_blank, Lines0, Lines),
    (   Lines = [First|_] -> true ; throw(error(amb_snapshot(empty(File)), _)) ),
    amb_reason_term(First, 1, Head),
    (   amb_reason_header(Head) -> true ; throw(error(amb_snapshot(no_header(Head)), _)) ),
    amb_reason_sections(Lines, 1, none, Sections, Facts).

amb_reason_header(amb_snapshot(M, Doc, Rev, FP)) :- atom(M), integer(Doc), integer(Rev), atom(FP).

amb_reason_sections([], _, _, [], []).
amb_reason_sections([L|Ls], No, Cur, Sections, Facts) :-
    amb_reason_term(L, No, T),
    No1 is No + 1,
    (   T = amb_snapshot(_, _, _, _)
    ->  (   amb_reason_header(T) -> true ; throw(error(amb_snapshot(no_header(T)), _)) ),
        T = amb_snapshot(M, Doc, Rev, FP),
        amb_reason_schema(M),
        amb_reason_schema_decl(M, module(M, _, SchemaFP)),
        (   FP == SchemaFP -> true
        ;   throw(error(amb_snapshot(fingerprint(M, snapshot(FP), schema(SchemaFP))), _)) ),
        amb_reason_schema_facts(M, Decls),
        Sections = [snap(M, Doc, Rev, FP)|Ss],
        amb_reason_sections(Ls, No1, sec(M, Doc, Decls), Ss, Facts)
    ;   Cur = sec(M, Doc, Decls),
        (   amb_reason_fact_ok(T, Doc, M, Decls) -> true
        ;   throw(error(amb_snapshot(undeclared_or_malformed(line(No), T)), _)) ),
        Facts = [T|Fs],
        amb_reason_sections(Ls, No1, Cur, Sections, Fs)
    ).

amb_reason_blank(S) :- string_length(S, 0).

%% the file's lines, read from a stream a line at a time: cocolog's
%% read_file_to_codes/2 refuses a file past 16 MiB (toolchain/REPORT.md, finding 12)
amb_reason_lines(File, Lines) :-
    open(File, read, S),
    amb_reason_read_lines(S, Lines),
    close(S).
amb_reason_read_lines(S, Lines) :-
    read_line_to_codes(S, C),
    (   C == end_of_file -> Lines = []
    ;   ( append(C0, [0'\r], C) -> true ; C0 = C ),
        string_codes(L, C0), Lines = [L|Rest], amb_reason_read_lines(S, Rest)
    ).

amb_reason_term(Line, No, T) :-
    catch(read_term_from_atom(Line, T, []), _,
          throw(error(amb_snapshot(syntax(line(No), Line)), _))),
    (   ground(T) -> true ; throw(error(amb_snapshot(not_ground(line(No), T)), _)) ).

amb_reason_fact_ok(T, Doc, M, Decls) :-
    compound(T), functor(T, F, A),
    memberchk(fact(F/A, Shapes), Decls),
    T =.. [_|Args],
    amb_reason_shapes(Args, Shapes, Doc, M).

amb_reason_shapes([], [], _, _).
amb_reason_shapes([A|As], [S|Ss], Doc, M) :- amb_reason_shape(S, A, Doc, M), amb_reason_shapes(As, Ss, Doc, M).

amb_reason_shape(handle(R), H, Doc, M) :- amb_reason_schema_decl(M, handle(R, F)), H =.. [F, Doc, I, G], integer(I), I > 0, integer(G), G >= 0.
amb_reason_shape(handle_or_none(R), H, Doc, M) :- ( H == none -> true ; amb_reason_shape(handle(R), H, Doc, M) ).
amb_reason_shape(xhandle(M2, R), H, _, _) :-
    ( H == none -> true ; amb_reason_schema(M2), amb_reason_schema_decl(M2, handle(R, F)), H =.. [F, D, I, G], integer(D), integer(I), integer(G) ).
amb_reason_shape(enum(E), V, _, M) :- atom(V), amb_reason_schema_decl(M, enum(E, Vs)), memberchk(V, Vs).
amb_reason_shape(enum_values(Vs), V, _, _) :- atom(V), memberchk(V, Vs).
amb_reason_shape(atom, V, _, _) :- atom(V).
amb_reason_shape(integer, V, _, _) :- integer(V).
amb_reason_shape(number, V, _, _) :- number(V).

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
