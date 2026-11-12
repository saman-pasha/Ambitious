%% prolog/amb/reason.pl -- the reasoner's command (prolog/amb/reasoner.pl is the library):
%%
%%   cocolog -s prolog/amb/reason.pl -- check SNAPSHOT            is it a valid snapshot?
%%   cocolog -s prolog/amb/reason.pl -- query SNAPSHOT GOAL       every answer, a line each
%%   cocolog -s prolog/amb/reason.pl -- why SNAPSHOT GOAL         each answer with its proof
%%   cocolog -s prolog/amb/reason.pl -- advise SNAPSHOT           diagnostics and requests
%%   cocolog -s prolog/amb/reason.pl -- diff OLD NEW              the facts that changed
%%   cocolog -s prolog/amb/reason.pl -- serve NONCE               a reasoner kept for a session:
%%                                       requests on its standard input, answers on its output
%%
%% Exit 0 answered, 2 refused (a snapshot that is not valid, a goal that does not read).
:- ensure_loaded(library(amb/reasoner)).

main :-
    current_prolog_flag(argv, [_|Args]),
    (   catch(amb_reason(Args), E, ( amb_reason_error(E), halt(2) )) -> true
    ;   format(user_error, 'reason: ~q failed~n', [Args]), halt(1) ).
