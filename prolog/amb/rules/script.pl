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
   script_window_listener/2, script_capture/2, script_timer/4, script_timer_of/2, script_forced_layout/2.

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

%% a script named by its URL, or by its number on the page (its run's index)
script_rules_where(R, Where) :- script_url(R, U), format(atom(Where), '~w', [U]).
script_rules_where(R, Where) :- \+ script_url(R, _), R = sr(_, I, _), script_run(R, N, Source, _), dom_element(N, E),
    (   Source == attribute -> format(atom(Where), '#~d, an on* attribute of a <~w>', [I, E])
    ;   format(atom(Where), '#~d (inline)', [I]) ).
