%% prolog/amb/profile_main.pl -- the profile keeper's command (prolog/amb/profile.pl is the library):
%%
%%   cocolog --embed DIR -s prolog/amb/profile_main.pl -- COMMAND ARGS...   a profile kept in DIR
%%   cocolog --local -s prolog/amb/profile_main.pl -- COMMAND ARGS...       a private one: memory only
%%
%% The commands: visit URL TITLE TIME, bookmark URL TITLE, unbookmark URL, permit SITE KIND
%% allow|deny, prefer KEY VALUE, save_session NAME URL..., session NAME, retain DAYS,
%% forget URL, forget_site SITE, forget_since TIME, history, bookmarks, permissions,
%% denied SITE, stats, query GOAL. Exit 0 done, 2 refused.
:- ensure_loaded(library(amb/profile)).

main :-
    current_prolog_flag(argv, [_|Args]),
    catch(( amb_pk_open, amb_pk(Args) ), E, ( amb_pk_error(E), halt(2) )).

amb_pk_error(error(amb_profile(Why), _)) :- !, format(user_error, 'profile: refused: ~q~n', [Why]).
amb_pk_error(E) :- format(user_error, 'profile: ~q~n', [E]).
