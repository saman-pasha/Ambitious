%% prolog/amb/profile.pl -- the profile: what the browser keeps between runs.
%%
%% A profile is a cocolog store: --embed DIR keeps it in ZiguratIP's MVCC store
%% in DIR, from one process to the next; --local keeps it in memory, gone when
%% the process ends -- a private session. It holds these kinds of fact, and the
%% keeper writes no other:
%%
%%   amb_profile_visit(Url, Title, Time)          a page loaded, when (seconds)
%%   amb_profile_bookmark(Url, Title, Time)        a page kept, when
%%   amb_profile_permission(Site, Kind, Decision)  may a site's pages load a kind of
%%                                                 resource: allow or deny
%%   amb_profile_pref(Key, Value)                  a preference. Four change how pages
%%                                                 are drawn: font_size (px, 6 to 72),
%%                                                 color_scheme (light, dark),
%%                                                 reduced_motion (no_preference, reduce)
%%                                                 and zoom (0.25 to 5); the shell hands
%%                                                 them to the stages (src/rt/amb_prefs.h)
%%   amb_profile_session(Name, Order, Url)         a saved list of pages
%%   amb_profile_retention(Days)                   visits older are deleted
%%
%% and a session's tabs, as `ambitious loop' saves them when it ends (a private
%% session saves none) and restores them with --restore:
%%
%%   amb_profile_saved(Session, Time)              a session saved, when
%%   amb_profile_tab(Session, Tab, Order, At)      a tab: its place among the tabs (1, 2,
%%                                                 ...) and the page of its history it shows
%%   amb_profile_entry(Session, Tab, I, Url, Title)   the I-th page of the tab's history
%%   amb_profile_current(Session, Tab)             the tab that was current
%%   amb_profile_loaded(Session, Tab, I, Outcome, Time)  how the I-th page loaded when the
%%                                                 tab last showed it: ok, or failed(Why)
%%
%% A site is a page's origin (scheme://host[:port]), or for a page whose origin
%% is opaque its scheme (file, data). The profile is never in a page's snapshot:
%% the reasoner, a process of its own over its own store, cannot see it, and the
%% profile sees no page but what the engine records. Retention is applied each
%% time the profile is opened; forgetting removes what it names and nothing else,
%% the saved tabs' pages among it. Its predicates are amb_profile_* (facts and rules)
%% and amb_pk_* (the keeper's).
:- use_module(library(stream)).
:- dynamic amb_profile_visit/3, amb_profile_bookmark/3, amb_profile_permission/3, amb_profile_pref/2,
           amb_profile_session/3, amb_profile_retention/1,
           amb_profile_saved/2, amb_profile_tab/4, amb_profile_entry/5, amb_profile_current/2, amb_profile_loaded/5.

%% ---- what the profile knows, derived ---------------------------------------------

amb_profile_visits(Url, N) :- setof(U, T^W^amb_profile_visit(U, T, W), Us), member(Url, Us), aggregate_all(count, amb_profile_visit(Url, _, _), N).
amb_profile_most_visited(Url, N) :-
    findall(N0-U0, amb_profile_visits(U0, N0), Ps), msort(Ps, S), reverse(S, [N-Url|_]).
amb_profile_last_visit(Url, Time) :- amb_profile_visits(Url, _), aggregate_all(max(T), amb_profile_visit(Url, _, T), Time).
%% may the site load the kind: its permission, or allow
amb_profile_may(Site, Kind, D) :- ( amb_profile_permission(Site, Kind, D0) -> D = D0 ; D = allow ).

%% the page a saved tab shows
amb_profile_tab_url(S, Tab, Url) :- amb_profile_tab(S, Tab, _, At), amb_profile_entry(S, Tab, At, Url, _).
%% a saved session's tabs' pages, in their order
amb_profile_session_pages(S, Urls) :-
    amb_profile_saved(S, _),
    findall(O-U, ( amb_profile_tab(S, T, O, _), amb_profile_tab_url(S, T, U) ), Ps), msort(Ps, Ss),
    findall(U, member(_-U, Ss), Urls).

%% the preferences that change how pages are drawn: each one's default, and the test
%% its value must pass
amb_profile_rendering_pref(font_size, 16).
amb_profile_rendering_pref(color_scheme, light).
amb_profile_rendering_pref(reduced_motion, no_preference).
amb_profile_rendering_pref(zoom, 1).
amb_profile_pref_valid(font_size, V) :- number(V), V >= 6, V =< 72.
amb_profile_pref_valid(color_scheme, V) :- memberchk(V, [light, dark]).
amb_profile_pref_valid(reduced_motion, V) :- memberchk(V, [no_preference, reduce]).
amb_profile_pref_valid(zoom, V) :- number(V), V >= 0.25, V =< 5.
%% a rendering preference as pages are drawn with it: the reader's, when it is valid,
%% else its default
amb_profile_rendering(K, V) :-
    amb_profile_rendering_pref(K, D),
    (   amb_profile_pref(K, V0), amb_profile_pref_valid(K, V0) -> V = V0 ; V = D ).
%% the size, in device px, text set at the base font size is drawn at
amb_profile_text_px(Px) :- amb_profile_rendering(font_size, F), amb_profile_rendering(zoom, Z), Px is F * Z.

%% WHAT IS WRONG WITH THE PREFERENCES:
%% a rendering preference whose value is none it takes (kept from before it was checked)
amb_profile_fault(bad_pref, pref(K), Text) :-
    amb_profile_rendering_pref(K, D), amb_profile_pref(K, V), \+ amb_profile_pref_valid(K, V),
    format(atom(Text), 'the preference ~w is ~q, which it cannot be: pages are drawn with ~w', [K, V, D]).
%% text drawn smaller than 9 px, which the base font size and the zoom make together
amb_profile_fault(small_text, prefs, Text) :-
    amb_profile_text_px(Px), Px < 9,
    amb_profile_rendering(font_size, F), amb_profile_rendering(zoom, Z),
    format(atom(Text), 'text at the base font size is drawn ~w px high (font_size ~w at zoom ~w), under 9 px', [Px, F, Z]).

%% WHAT IS WRONG WITH A SAVED SESSION, as amb_profile_fault(Kind, Subject, Text):
%% a tab restored to a page that failed to load when the tab showed it again
amb_profile_fault(restore_failed, tab(S, Tab), Text) :-
    amb_profile_tab(S, Tab, O, At), amb_profile_entry(S, Tab, At, Url, _),
    amb_profile_loaded(S, Tab, At, failed(Why), _),
    format(atom(Text), 'tab ~w shows ~w, which failed to load when it was restored: ~w', [O, Url, Why]).
%% a page of a tab's history, not the one it shows, that failed to load when shown
amb_profile_fault(history_failed, entry(S, Tab, I), Text) :-
    amb_profile_tab(S, Tab, O, At), amb_profile_loaded(S, Tab, I, failed(Why), _), I \== At,
    amb_profile_entry(S, Tab, I, Url, _),
    format(atom(Text), 'page ~w of tab ~w''s history, ~w, failed to load: ~w', [I, O, Url, Why]).
%% a tab that shows no page of its history
amb_profile_fault(shown_outside, tab(S, Tab), Text) :-
    amb_profile_tab(S, Tab, O, At), \+ amb_profile_entry(S, Tab, At, _, _),
    format(atom(Text), 'tab ~w shows page ~w of a history without it', [O, At]).
%% a session whose current tab is none of its tabs
amb_profile_fault(no_current, session(S), Text) :-
    amb_profile_saved(S, _), amb_profile_tab(S, _, _, _),
    \+ ( amb_profile_current(S, T), amb_profile_tab(S, T, _, _) ),
    format(atom(Text), 'session ~w has no current tab', [S]).
%% two tabs of a session that show one page
amb_profile_fault(twin_tabs, tabs(S, T1, T2), Text) :-
    amb_profile_tab(S, T1, O1, _), amb_profile_tab(S, T2, O2, _), O1 < O2,
    amb_profile_tab_url(S, T1, U), amb_profile_tab_url(S, T2, U),
    format(atom(Text), 'tabs ~w and ~w both show ~w', [O1, O2, U]).

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
%% a preference kept; a rendering one only with a value it takes, a number as a number
amb_pk([prefer, Key, Value]) :- !,
    (   amb_profile_rendering_pref(Key, _)
    ->  ( atom_number(Value, N) -> V = N ; V = Value ),
        (   amb_profile_pref_valid(Key, V) -> true ; throw(error(amb_profile(bad_pref(Key, Value)), _)) )
    ;   V = Value ),
    retractall(amb_profile_pref(Key, _)), assertz(amb_profile_pref(Key, V)),
    format('~w: ~w~n', [Key, V]).
%% the rendering preferences, each as pages are drawn with it, a line each for the
%% engine (src/rt/amb_prefs.h reads them)
amb_pk([prefs]) :- !, forall(amb_profile_rendering_pref(K, _), ( amb_profile_rendering(K, V), format('pref(~w, ~w).~n', [K, V]) )).
amb_pk([save_session, Name|Urls]) :- !,
    retractall(amb_profile_session(Name, _, _)),
    forall(nth1(I, Urls, U), assertz(amb_profile_session(Name, I, U))),
    length(Urls, N), format('session ~w: ~d page(s)~n', [Name, N]).
amb_pk([session, Name]) :- !, forall(amb_profile_session(Name, _, U), format('~w~n', [U])).
%% a session's tabs, from the file the engine wrote: its terms read as data, never
%% consulted, each of a declared shape, in place of what the session held
amb_pk([save_tabs, Name, File, Time]) :- !,
    atom_number(Time, T),
    amb_pk_read_tabs(File, Terms),
    amb_pk_drop_session(Name),
    assertz(amb_profile_saved(Name, T)),
    forall(member(tab(Id, O, At), Terms), assertz(amb_profile_tab(Name, Id, O, At))),
    forall(member(entry(Id, I, U, Ti), Terms), assertz(amb_profile_entry(Name, Id, I, U, Ti))),
    forall(member(current(Id), Terms), assertz(amb_profile_current(Name, Id))),
    forall(member(loaded(Id, I, Out), Terms), assertz(amb_profile_loaded(Name, Id, I, Out, T))),
    aggregate_all(count, member(tab(_, _, _), Terms), N),
    format('session ~w saved: ~d tab(s)~n', [Name, N]).
%% a saved session's tabs as terms, the engine's to read: tab(Id, At) in their order,
%% each followed by its entry(Id, I, 'Url', 'Title') and loaded(Id, I, ok | 'Why'); then
%% current(Id). Every atom is quoted, as the engine's reader (amb_lex_atom) takes one
amb_pk([tabs, Name]) :- !,
    findall(O-Id-At, amb_profile_tab(Name, Id, O, At), Ts0), msort(Ts0, Ts),
    forall(member(_-Id-At, Ts),
           ( format('tab(~d,~d).~n', [Id, At]),
             findall(I-U-Ti, amb_profile_entry(Name, Id, I, U, Ti), Es0), msort(Es0, Es),
             forall(member(I-U-Ti, Es),
                    ( format('entry(~d,~d,', [Id, I]), amb_pk_put_atom(U), format(',', []), amb_pk_put_atom(Ti), format(').~n', []) )),
             forall(amb_profile_loaded(Name, Id, I2, Out, _),
                    ( Out = failed(W) -> format('loaded(~d,~d,', [Id, I2]), amb_pk_put_atom(W), format(').~n', [])
                    ; format('loaded(~d,~d,ok).~n', [Id, I2]) )) )),
    forall(amb_profile_current(Name, C), format('current(~d).~n', [C])).
amb_pk([sessions]) :- !,
    forall(amb_profile_saved(S, T),
           ( aggregate_all(count, amb_profile_tab(S, _, _, _), N), format('~w  ~w  ~d tab(s)~n', [S, T, N]) )).
amb_pk([faults]) :- !, forall(amb_profile_fault(K, S, T), format('amb_profile_fault(~q, ~q, ~q).~n', [K, S, T])).
amb_pk([retain, Days]) :- !,
    atom_number(Days, D), retractall(amb_profile_retention(_)), assertz(amb_profile_retention(D)),
    amb_pk_open, format('visits are kept ~d day(s)~n', [D]).
amb_pk([forget, Url]) :- !,
    aggregate_all(count, amb_profile_visit(Url, _, _), V), aggregate_all(count, amb_profile_bookmark(Url, _, _), B),
    retractall(amb_profile_visit(Url, _, _)), retractall(amb_profile_bookmark(Url, _, _)),
    amb_pk_forget_pages(==(Url), P),
    format('forgot ~w: ~d visit(s), ~d bookmark(s), ~d page(s) of saved tabs~n', [Url, V, B, P]).
amb_pk([forget_site, Site]) :- !,
    findall(U-T-W, ( amb_profile_visit(U, T, W), sub_atom(U, 0, _, _, Site) ), Vs),
    forall(member(U-T-W, Vs), retract(amb_profile_visit(U, T, W))),
    findall(U-T-W, ( amb_profile_bookmark(U, T, W), sub_atom(U, 0, _, _, Site) ), Bs),
    forall(member(U-T-W, Bs), retract(amb_profile_bookmark(U, T, W))),
    aggregate_all(count, amb_profile_permission(Site, _, _), P), retractall(amb_profile_permission(Site, _, _)),
    amb_pk_forget_pages(amb_pk_of_site(Site), TP),
    length(Vs, NV), length(Bs, NB),
    format('forgot ~w: ~d visit(s), ~d bookmark(s), ~d permission(s), ~d page(s) of saved tabs~n', [Site, NV, NB, P, TP]).
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
                        amb_profile_session/3, amb_profile_retention/1, amb_profile_saved/2, amb_profile_tab/4,
                        amb_profile_entry/5, amb_profile_current/2, amb_profile_loaded/5]),
           ( functor(H, P, A), aggregate_all(count, H, N), format('~w ~d~n', [P, N]) )).
amb_pk([query, Text]) :- !,
    catch(read_term_from_atom(Text, G, [variable_names(Vs)]), E, throw(error(amb_profile(bad_goal(Text, E)), _))),
    (   amb_pk_reads_only(G) -> true ; throw(error(amb_profile(not_read_only(Text)), _)) ),
    findall(Vs, G, As),
    (   As == [] -> format('no~n', [])
    ;   forall(member(A, As), amb_pk_print(A)) ).
amb_pk(Args) :- throw(error(amb_profile(unknown_command(Args)), _)).

%% the tabs' file: tab(Id, Order, At), entry(Id, I, Url, Title), current(Id) and
%% loaded(Id, I, ok | failed(Why)), each term ground and of its shape; anything else
%% refuses the whole file
amb_pk_read_tabs(File, Terms) :-
    open(File, read, S),
    catch(amb_pk_tab_terms(S, Terms), E, ( close(S), throw(E) )),
    close(S).
amb_pk_tab_terms(S, Terms) :-
    read_term(S, T, []),
    (   T == end_of_file -> Terms = []
    ;   amb_pk_tab_term(T) -> Terms = [T|Rest], amb_pk_tab_terms(S, Rest)
    ;   throw(error(amb_profile(bad_tab_term(T)), _)) ).
amb_pk_tab_term(tab(Id, O, At)) :- integer(Id), integer(O), integer(At).
amb_pk_tab_term(entry(Id, I, U, Ti)) :- integer(Id), integer(I), atom(U), atom(Ti).
amb_pk_tab_term(current(Id)) :- integer(Id).
amb_pk_tab_term(loaded(Id, I, ok)) :- integer(Id), integer(I).
amb_pk_tab_term(loaded(Id, I, failed(W))) :- integer(Id), integer(I), atom(W).

%% a session's tabs gone
amb_pk_drop_session(S) :-
    retractall(amb_profile_saved(S, _)), retractall(amb_profile_tab(S, _, _, _)), retractall(amb_profile_entry(S, _, _, _, _)),
    retractall(amb_profile_current(S, _)), retractall(amb_profile_loaded(S, _, _, _, _)).

amb_pk_of_site(Site, Url) :- sub_atom(Url, 0, _, _, Site).

%% an atom written quoted, whatever it holds: ' and \ escaped, the control characters
%% by name or in hex, the rest as it is
amb_pk_put_atom(A) :- atom_codes(A, Cs), format('\'', []), forall(member(C, Cs), amb_pk_put_code(C)), format('\'', []).
amb_pk_put_code(0'\\) :- !, format('\\\\', []).
amb_pk_put_code(0'\') :- !, format('\\\'', []).
amb_pk_put_code(10) :- !, format('\\n', []).
amb_pk_put_code(9) :- !, format('\\t', []).
amb_pk_put_code(13) :- !, format('\\r', []).
amb_pk_put_code(C) :- C < 32, !, format('\\x~16r\\', [C]).
amb_pk_put_code(C) :- format('~c', [C]).

%% the saved tabs' pages whose URL passes Test, forgotten: each tab's history numbered
%% again from 1, the page it shows the nearest kept one before (or the first), and a tab
%% left with none closed; N the pages forgotten
amb_pk_forget_pages(Test, N) :-
    findall(S-T-I, ( amb_profile_entry(S, T, I, U, _), call(Test, U) ), Gone),
    length(Gone, N),
    forall(member(S-T-I, Gone), ( retract(amb_profile_entry(S, T, I, _, _)), retractall(amb_profile_loaded(S, T, I, _, _)) )),
    findall(S-T, member(S-T-_, Gone), Ts0), sort(Ts0, Ts),
    forall(member(S-T, Ts), amb_pk_renumber(S, T)).
amb_pk_renumber(S, T) :-
    findall(I-U-Ti, amb_profile_entry(S, T, I, U, Ti), Es0), msort(Es0, Es),
    (   Es == []
    ->  retractall(amb_profile_tab(S, T, _, _)), retractall(amb_profile_current(S, T)), retractall(amb_profile_loaded(S, T, _, _, _))
    ;   retract(amb_profile_tab(S, T, O, At)),
        findall(I, member(I-_-_, Es), Is),
        (   findall(K, ( nth1(K, Is, I), I =< At ), Ks), Ks \== [] -> last(Ks, At1) ; At1 = 1 ),
        findall(K-Out-W, ( amb_profile_loaded(S, T, I, Out, W), nth1(K, Is, I) ), Ls),
        retractall(amb_profile_entry(S, T, _, _, _)), retractall(amb_profile_loaded(S, T, _, _, _)),
        forall(nth1(K, Es, _-U-Ti), assertz(amb_profile_entry(S, T, K, U, Ti))),
        forall(member(K-Out-W, Ls), assertz(amb_profile_loaded(S, T, K, Out, W))),
        assertz(amb_profile_tab(S, T, O, At1))
    ).

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
