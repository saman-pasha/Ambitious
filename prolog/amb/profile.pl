%% prolog/amb/profile.pl -- the profile: what the browser keeps between runs.
%%
%% A profile is a cocolog store: --embed DIR keeps it in ZiguratIP's MVCC store
%% in DIR, from one process to the next; --local keeps it in memory, gone when
%% the process ends -- a private session. It holds six kinds of fact, and the
%% keeper writes no other:
%%
%%   amb_profile_visit(Url, Title, Time)          a page loaded, when (seconds)
%%   amb_profile_bookmark(Url, Title, Time)        a page kept, when
%%   amb_profile_permission(Site, Kind, Decision)  may a site's pages load a kind of
%%                                                 resource: allow or deny
%%   amb_profile_pref(Key, Value)                  a preference
%%   amb_profile_session(Name, Order, Url)         a saved session's pages
%%   amb_profile_retention(Days)                   visits older are deleted
%%
%% A site is a page's origin (scheme://host[:port]), or for a page whose origin
%% is opaque its scheme (file, data). The profile is never in a page's snapshot:
%% the reasoner, a process of its own over its own store, cannot see it, and the
%% profile sees no page but what the engine records. Retention is applied each
%% time the profile is opened; forgetting removes what it names and nothing else.
%% Its predicates are amb_profile_* (facts and rules) and amb_pk_* (the keeper's).
:- dynamic amb_profile_visit/3, amb_profile_bookmark/3, amb_profile_permission/3, amb_profile_pref/2,
           amb_profile_session/3, amb_profile_retention/1.

%% ---- what the profile knows, derived ---------------------------------------------

amb_profile_visits(Url, N) :- setof(U, T^W^amb_profile_visit(U, T, W), Us), member(Url, Us), aggregate_all(count, amb_profile_visit(Url, _, _), N).
amb_profile_most_visited(Url, N) :-
    findall(N0-U0, amb_profile_visits(U0, N0), Ps), msort(Ps, S), reverse(S, [N-Url|_]).
amb_profile_last_visit(Url, Time) :- amb_profile_visits(Url, _), aggregate_all(max(T), amb_profile_visit(Url, _, T), Time).
%% may the site load the kind: its permission, or allow
amb_profile_may(Site, Kind, D) :- ( amb_profile_permission(Site, Kind, D0) -> D = D0 ; D = allow ).

%% ---- the keeper's commands -------------------------------------------------------

amb_pk_kind(stylesheet).
amb_pk_kind(image).
amb_pk_kind(script).

amb_pk_now(T) :- ( getenv('AMB_NOW', A), atom_number(A, T0) -> T = T0 ; get_time(F), T is integer(F) ).

%% the profile opened: visits older than the retention deleted
amb_pk_open :-
    (   amb_profile_retention(Days)
    ->  amb_pk_now(Now), Limit is Now - Days * 86400,
        forall(( amb_profile_visit(U, T, W), W < Limit ), retract(amb_profile_visit(U, T, W)))
    ;   true ).

amb_pk([visit, Url, Title, Time]) :- !, atom_number(Time, T), assertz(amb_profile_visit(Url, Title, T)).
amb_pk([bookmark, Url, Title]) :- !,
    amb_pk_now(T), retractall(amb_profile_bookmark(Url, _, _)), assertz(amb_profile_bookmark(Url, Title, T)),
    format('bookmarked ~w~n', [Url]).
amb_pk([unbookmark, Url]) :- !, retractall(amb_profile_bookmark(Url, _, _)), format('unbookmarked ~w~n', [Url]).
amb_pk([permit, Site, Kind, D]) :- !,
    (   amb_pk_kind(Kind), memberchk(D, [allow, deny]) -> true
    ;   throw(error(amb_profile(bad_permission(Kind, D)), _)) ),
    retractall(amb_profile_permission(Site, Kind, _)), assertz(amb_profile_permission(Site, Kind, D)),
    format('~w: ~w ~w~n', [Site, D, Kind]).
amb_pk([prefer, Key, Value]) :- !, retractall(amb_profile_pref(Key, _)), assertz(amb_profile_pref(Key, Value)).
amb_pk([save_session, Name|Urls]) :- !,
    retractall(amb_profile_session(Name, _, _)),
    forall(nth1(I, Urls, U), assertz(amb_profile_session(Name, I, U))),
    length(Urls, N), format('session ~w: ~d page(s)~n', [Name, N]).
amb_pk([session, Name]) :- !, forall(amb_profile_session(Name, _, U), format('~w~n', [U])).
amb_pk([retain, Days]) :- !,
    atom_number(Days, D), retractall(amb_profile_retention(_)), assertz(amb_profile_retention(D)),
    amb_pk_open, format('visits are kept ~d day(s)~n', [D]).
amb_pk([forget, Url]) :- !,
    aggregate_all(count, amb_profile_visit(Url, _, _), V), aggregate_all(count, amb_profile_bookmark(Url, _, _), B),
    retractall(amb_profile_visit(Url, _, _)), retractall(amb_profile_bookmark(Url, _, _)),
    format('forgot ~w: ~d visit(s), ~d bookmark(s)~n', [Url, V, B]).
amb_pk([forget_site, Site]) :- !,
    findall(U-T-W, ( amb_profile_visit(U, T, W), sub_atom(U, 0, _, _, Site) ), Vs),
    forall(member(U-T-W, Vs), retract(amb_profile_visit(U, T, W))),
    findall(U-T-W, ( amb_profile_bookmark(U, T, W), sub_atom(U, 0, _, _, Site) ), Bs),
    forall(member(U-T-W, Bs), retract(amb_profile_bookmark(U, T, W))),
    aggregate_all(count, amb_profile_permission(Site, _, _), P), retractall(amb_profile_permission(Site, _, _)),
    length(Vs, NV), length(Bs, NB),
    format('forgot ~w: ~d visit(s), ~d bookmark(s), ~d permission(s)~n', [Site, NV, NB, P]).
amb_pk([forget_since, Time]) :- !,
    atom_number(Time, T0),
    findall(U-T-W, ( amb_profile_visit(U, T, W), W >= T0 ), Vs),
    forall(member(U-T-W, Vs), retract(amb_profile_visit(U, T, W))),
    length(Vs, N), format('forgot ~d visit(s) since ~w~n', [N, T0]).
amb_pk([history]) :- !,
    findall(W-U-T, amb_profile_visit(U, T, W), Vs), msort(Vs, S), reverse(S, R),
    forall(member(W-U-T, R), format('~w  ~w  ~w~n', [W, U, T])).
amb_pk([bookmarks]) :- !, forall(amb_profile_bookmark(U, T, _), format('~w  ~w~n', [U, T])).
amb_pk([permissions]) :- !, forall(amb_profile_permission(S, K, D), format('~w  ~w  ~w~n', [S, K, D])).
%% the kinds a site may not load, one a line: what the engine asks before it fetches
amb_pk([denied, Site]) :- !, forall(( amb_pk_kind(K), amb_profile_may(Site, K, deny) ), format('~w~n', [K])).
amb_pk([stats]) :- !,
    forall(member(P/A, [amb_profile_visit/3, amb_profile_bookmark/3, amb_profile_permission/3, amb_profile_pref/2,
                        amb_profile_session/3, amb_profile_retention/1]),
           ( functor(H, P, A), aggregate_all(count, H, N), format('~w ~d~n', [P, N]) )).
amb_pk([query, Text]) :- !,
    catch(read_term_from_atom(Text, G, [variable_names(Vs)]), E, throw(error(amb_profile(bad_goal(Text, E)), _))),
    (   amb_pk_reads_only(G) -> true ; throw(error(amb_profile(not_read_only(Text)), _)) ),
    findall(Vs, G, As),
    (   As == [] -> format('no~n', [])
    ;   forall(member(A, As), amb_pk_print(A)) ).
amb_pk(Args) :- throw(error(amb_profile(unknown_command(Args)), _)).

amb_pk_print([]) :- !, format('yes~n', []).
amb_pk_print(Vs) :- findall(S, ( member(N=V, Vs), format(atom(S), '~w = ~q', [N, V]) ), Ss), atomic_list_concat(Ss, ', ', L), format('~w~n', [L]).

%% a query may read the profile and nothing else: its goals are the profile's
%% facts and rules, and tests that change nothing
amb_pk_reads_only(G) :- var(G), !, fail.
amb_pk_reads_only((A, B)) :- !, amb_pk_reads_only(A), amb_pk_reads_only(B).
amb_pk_reads_only((A ; B)) :- !, amb_pk_reads_only(A), amb_pk_reads_only(B).
amb_pk_reads_only((A -> B)) :- !, amb_pk_reads_only(A), amb_pk_reads_only(B).
amb_pk_reads_only(\+ A) :- !, amb_pk_reads_only(A).
amb_pk_reads_only(findall(_, A, _)) :- !, amb_pk_reads_only(A).
amb_pk_reads_only(aggregate_all(_, A, _)) :- !, amb_pk_reads_only(A).
amb_pk_reads_only(G) :- functor(G, F, _), atom_concat(amb_profile_, _, F), !.
amb_pk_reads_only(G) :- functor(G, F, A), memberchk(F/A, [member/2, length/2, msort/2, sort/2, nth1/3, (=)/2, (==)/2, (\==)/2, (<)/2, (>)/2,
    (=<)/2, (>=)/2, (is)/2, sub_atom/5, atom_length/2, atom_concat/3, true/0]).
