%% prolog/amb/tool.pl -- the contract tool, run by tools/amb:
%%
%%   cocolog -s prolog/amb/tool.pl -- validate M ...    each contract checked; exit 1 on a refusal
%%   cocolog -s prolog/amb/tool.pl -- schema M          the reasoning schema of M, on stdout
%%   cocolog -s prolog/amb/tool.pl -- doc M             the contract's reference, Markdown, on stdout
%%   cocolog -s prolog/amb/tool.pl -- fingerprint M     the contract's fingerprint
%%
%% Every predicate here is amb_tool_*.
:- ensure_loaded(library(amb/contract)).
:- ensure_loaded(library(amb/gen_schema)).

main :-
    current_prolog_flag(argv, Argv),
    amb_tool_args(Argv, Args),
    catch(amb_tool(Args), E, ( amb_tool_error(E), halt(2) )).

%% the arguments after the program's own: cocolog gives [Exe|Args]
amb_tool_args([_|Args], Args) :- !.
amb_tool_args([], []).

amb_tool([validate|Ms]) :- !,
    Ms \== [],
    foldl(amb_tool_validate, Ms, 0, Bad),
    ( Bad =:= 0 -> true ; halt(1) ).
amb_tool([schema, M]) :- !, amb_schema_text(M, Text), format('~s', [Text]).
amb_tool([doc, M]) :- !, amb_doc_text(M, Text), format('~s', [Text]).
amb_tool([fingerprint, M]) :- !, amb_contract_fingerprint(M, H), format('~w~n', [H]).
amb_tool(Args) :-
    format(user_error, 'amb: unknown command ~q~nusage: amb validate M... | schema M | doc M | fingerprint M~n', [Args]),
    halt(2).

amb_tool_validate(M, Bad0, Bad) :-
    amb_contract_file(M, File),
    amb_validate(M, Diags),
    (   Diags == []
    ->  amb_contract_terms(M, Ds), length(Ds, N),
        format('~w: ok (~d declarations)~n', [File, N]), Bad = Bad0
    ;   forall(member(diag(D, Msg), Diags),
               format('~w: error: ~w~n    in: ~q~n', [File, Msg, D])),
        length(Diags, K), format('~w: ~d error(s)~n', [File, K]),
        Bad is Bad0 + 1
    ).

amb_tool_error(error(amb_contract(no_contract(M, File)), _)) :- !,
    format(user_error, 'amb: no contract for ~w (looked for ~w)~n', [M, File]).
amb_tool_error(E) :- format(user_error, 'amb: ~q~n', [E]).
