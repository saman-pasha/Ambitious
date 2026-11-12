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

%% the document's encoding (src/html/encoding.c). A page that declares none -- no byte
%% order mark, no charset from its server, no meta or XML declaration -- and whose
%% bytes are not all ASCII was read in a guessed encoding (UTF-8 for a file that is, else
%% the likeliest by its letters, else windows-1252): its text depends on it
:- dynamic html_encoding/2, html_decoding/2.

html_diag(undeclared_encoding, Name, Text) :-
    html_encoding(Name, Source),
    memberchk(Source-How, [default-'the default', detected-'as its bytes suggest']),
    html_decoding(false, _),
    format(atom(Text), 'the page declares no encoding and is not ASCII: it was decoded as ~w, ~w (<meta charset> declares one)', [Name, How]).
%% bytes the encoding's decoder found malformed, each sequence now a U+FFFD
html_diag(malformed_bytes, Name, Text) :-
    html_encoding(Name, _), Name \== replacement,
    html_decoding(_, N), N > 0,
    format(atom(Text), '~w byte sequence(s) are not ~w: each became U+FFFD', [N, Name]).
%% a label the Encoding standard maps to replacement (ISO-2022-KR, HZ-GB-2312 ...):
%% the whole document decodes to one U+FFFD
html_diag(replaced_encoding, replacement, 'the page''s encoding is one the Encoding standard replaces: its text is a single U+FFFD') :-
    html_encoding(replacement, _), html_decoding(_, N), N > 0.
