%% prolog/amb/rules/script.pl -- what the script module's facts mean (contracts/script.pl).
%%
%% A page's scripts ran before it came to rest: each script's outcome, what it
%% wrote and threw, the listeners it added and the timers it set are facts of
%% the snapshot. A script that did not run to its end leaves the page as far as
%% it got; an error nothing caught is lost to the page's own code; a click
%% listener on an element that takes no focus answers a mouse alone, never a
%% keyboard (WCAG 2.1.1); a timer still set when the page came to rest keeps it
%% from settling. No clause here cuts: amb_why/2 proves them clause by clause.

:- dynamic script_run/4, script_url/2, script_console/3, script_error/3, script_listener/3,
   script_window_listener/2, script_capture/2, script_timer/4, script_timer_of/2, script_forced_layout/2,
   script_inserted/2, script_request/5, script_request_by/2, script_request_status/2, script_request_resource/2,
   script_request_cross/1, script_write/4.

script_failed(R, How) :- script_run(R, _, _, How), How \== ran, How \== not_run.

script_uncaught(R, Phase, Message) :- script_error(R, Phase, Message).

script_listens(N, Type, R) :- script_listener(N, Type, R).

%% the elements a keyboard reaches by themselves (HTML's focusable areas), or by a tabindex
script_rules_focusable(N) :- dom_attr(N, tabindex, _).
script_rules_focusable(N) :- dom_attr(N, contenteditable, V), V \== false.
script_rules_focusable(N) :- dom_element(N, E), script_rules_native(E, N).
script_rules_native(button, _).
script_rules_native(select, _).
script_rules_native(textarea, _).
script_rules_native(summary, _).
script_rules_native(input, N) :- \+ dom_attr(N, type, hidden).
script_rules_native(a, N) :- dom_attr(N, href, _).
script_rules_native(area, N) :- dom_attr(N, href, _).
script_rules_native(iframe, _).
script_rules_native(audio, N) :- dom_attr(N, controls, _).
script_rules_native(video, N) :- dom_attr(N, controls, _).

%% a listener on the page's own containers is a delegate for what is inside it, not a control
script_rules_container(html).
script_rules_container(body).
script_rules_container(main).

script_click_unfocusable(N, Name) :-
    script_listener(N, click, _), dom_element(N, E),
    \+ script_rules_container(E), \+ script_rules_focusable(N),
    script_rules_name(N, Name).
script_rules_name(N, T) :- script_rules_short_text(N, T).
script_rules_name(N, Name) :- \+ script_rules_short_text(N, _), dom_element(N, E), format(atom(Name), '<~w>', [E]).
script_rules_short_text(N, T) :- dom_text_content(N, T), atom_length(T, L), L > 0, L =< 40.

script_unsettled(T, R) :- script_timer(T, _, _, pending), script_timer_of(T, R).

%% a request the browser would not let a script make or read: another origin's answer
%% without CORS's Access-Control-Allow-Origin, a file: URL
script_request_blocked(Q, U) :- script_request(Q, _, _, U, blocked).
%% one that had no good answer: no response at all, or an HTTP error's
script_request_failed(Q, U, How) :- script_request(Q, _, _, U, How), memberchk(How, [failed, http_error]).
%% a request over http: from a page served over https: (mixed content)
script_insecure_request(Q, U) :-
    script_request(Q, _, _, U, _), sub_atom(U, 0, _, _, 'http:'),
    net_page(P), net_final_url(P, PU), sub_atom(PU, 0, _, _, 'https:').
%% document.write after the page loaded opens a new document: the page is gone
script_write_replaced(R) :- script_write(_, R, after_load, _).
%% an asynchronously loaded script may not write into the document that is being parsed
script_write_ignored(R) :- script_write(_, R, ignored, _).

%% a read of style or geometry after a change to the DOM runs the cascade and the layout
%% again (a forced, synchronous layout): a script that does it many times interleaves its
%% reads with its writes, where reading first and writing after would run them once
script_layout_thrash(R, N) :- script_forced_layout(R, N), N >= 10.

%% ---- what the reasoner says of a page's scripts ------------------------------------

script_diag(script_failed, R, Text) :-
    script_failed(R, How), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w did not run to its end: ~w', [Where, How]).
script_diag(uncaught_error, R, Text) :-
    script_uncaught(R, Phase, Message), script_rules_where(R, Where),
    format(atom(Text), 'an error no code caught, thrown in a ~w of the script ~w: ~w', [Phase, Where, Message]).
script_diag(click_not_focusable, N, Text) :-
    script_click_unfocusable(N, Name), dom_element(N, E),
    format(atom(Text), 'the <~w> ~q has a click listener but takes no focus: a keyboard cannot use it (a button, or tabindex and a key listener)', [E, Name]).
script_diag(unsettled_timer, T, Text) :-
    script_unsettled(T, R), script_timer(T, Delay, Repeats, _), script_rules_where(R, Where),
    (   Repeats == true
    ->  format(atom(Text), 'an interval of ~d ms set by the script ~w is never cleared: the page never comes to rest', [Delay, Where])
    ;   format(atom(Text), 'a timeout of ~d ms set by the script ~w had not fired when the page came to rest', [Delay, Where])
    ).

script_diag(layout_thrash, R, Text) :-
    script_layout_thrash(R, N), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w made the cascade and the layout run ~d times, each read of style or geometry after a change to the page: read first, then write', [Where, N]).

script_diag(request_blocked, Q, Text) :-
    script_request_blocked(Q, U), script_request_by(Q, R), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w asked for ~w, which was blocked: another origin answers a script only with CORS''s Access-Control-Allow-Origin, and a page may not read a file: URL', [Where, U]).
script_diag(request_failed, Q, Text) :-
    script_request_failed(Q, U, How), script_request_by(Q, R), script_rules_where(R, Where),
    (   script_request_status(Q, S) -> true ; S = none ),
    format(atom(Text), 'the script ~w asked for ~w and had no good answer: ~w (status ~w)', [Where, U, How, S]).
script_diag(insecure_request, Q, Text) :-
    script_insecure_request(Q, U), script_request_by(Q, R), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w asked for ~w over http: from a page served over https: (mixed content)', [Where, U]).
script_diag(write_replaced, R, Text) :-
    script_write_replaced(R), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w called document.write after the page loaded: a new document replaced the whole page', [Where]).
script_diag(write_ignored, R, Text) :-
    script_write_ignored(R), script_rules_where(R, Where),
    format(atom(Text), 'the script ~w was loaded asynchronously, so its document.write was ignored', [Where]).

%% a script named by its URL, or by its number on the page (its run's index)
script_rules_where(R, Where) :- script_url(R, U), format(atom(Where), '~w', [U]).
script_rules_where(R, Where) :- \+ script_url(R, _), R = sr(_, I, _), script_run(R, N, Source, _),
    (   Source == attribute, dom_element(N, E) -> format(atom(Where), '#~d, an on* attribute of a <~w>', [I, E])
    ;   format(atom(Where), '#~d (inline)', [I]) ).
