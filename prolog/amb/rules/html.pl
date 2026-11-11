%% prolog/amb/rules/html.pl -- what the HTML module's facts mean (contracts/html.pl).
:- dynamic html_error/5.

html_error_count(Code, N) :-
    setof(C, E^L^Co^D^html_error(E, C, L, Co, D), Codes), member(Code, Codes),
    aggregate_all(count, html_error(_, Code, _, _, _), N).

html_conforming :- \+ html_error(_, _, _, _, _).

html_diag(parse_error, E, Text) :-
    html_error(E, Code, Line, Col, Detail),
    (   Detail == '' -> format(atom(Text), '~w at ~w:~w', [Code, Line, Col])
    ;   format(atom(Text), '~w at ~w:~w: ~w', [Code, Line, Col, Detail]) ).
