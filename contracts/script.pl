%% contracts/script.pl -- the script module's contract (docs/contract.md is the format).
%%
%% What a page's scripts did, as the engine (src/js/) ran them through the
%% bindings (src/script/): each script that ran or did not, and how it ended;
%% what it wrote to the console; the errors nothing caught, and where they were
%% thrown; the event listeners it added, on which element, for which event; the
%% timers it set, their delays, and whether they settled. A script's values never
%% reach the snapshot: a listener is its target and its event's type, a timer
%% its delay and its state. What the scripts changed in the page is the DOM's
%% own record (contracts/dom.pl), every change one of its operations.

script_contract(module(script, '0.1.0', 'Scripts: what a page''s scripts ran, wrote, threw, listened for and scheduled')).

%% ---- TYPES ------------------------------------------------------------------

script_contract(status([ok, stale_handle, wrong_kind, failed])).

%% where a script's text came from: a <script>'s content, its src, an on* attribute
script_contract(enum(source, [inline, external, attribute])).
%% how it ended: stopped is past its step budget; not_run a type that is not a
%% classic script (a module, a template's data); not_fetched an src that failed
script_contract(enum(outcome, [ran, threw, syntax_error, stopped, not_fetched, not_run])).
script_contract(enum(level, [log, info, warn, error, debug])).
%% where an uncaught error was thrown: a script's own run, a listener, a timer, a
%% microtask (a promise's reaction, an async function resumed; or a rejection no
%% handler took)
script_contract(enum(phase, [script, event, timer, promise])).
%% what a listener listens on: an element (or the document), or the window
script_contract(enum(listen_on, [node, window])).
%% a timer at the end: it fired (a timeout), it was cleared, or it would fire again
script_contract(enum(timer_state, [fired, cleared, pending])).

script_contract(record(run, [
    field(elem,    xhandle(dom, node)),     % the <script> element, or the element of the on* attribute
    field(source,  enum(source)),
    field(url,     span(text)),             % an external script's URL, as resolved
    field(outcome, enum(outcome))
])).
script_contract(record(console, [
    field(run,   ref(run)),                 % the script whose code wrote it (its listener's or timer's)
    field(level, enum(level)),
    field(text,  span(text))
])).
script_contract(record(error, [
    field(run,     ref(run)),
    field(phase,   enum(phase)),
    field(message, span(text))
])).
script_contract(record(listener, [
    field(on,      enum(listen_on)),
    field(target,  xhandle(dom, node)),     % the element or document; the document for the window
    field(type,    span(text)),             % the event's type: click, load ...
    field(run,     ref(run)),               % the script that added it
    field(capture, bool)
])).
script_contract(record(timer, [
    field(run,     ref(run)),               % the script that set it
    field(delay,   u32),                    % ms of virtual time
    field(repeats, bool),                   % setInterval's
    field(fired,   u32),                    % how many times it ran
    field(state,   enum(timer_state))
])).

script_contract(handle(run, sr)).
script_contract(handle(console, sc)).
script_contract(handle(error, se)).
script_contract(handle(listener, sl)).
script_contract(handle(timer, st)).

%% ---- OWNERSHIP --------------------------------------------------------------

script_contract(store(runtime, [table(run), table(console), table(error), table(listener), table(timer), pool(text)])).
script_contract(owns(runtime, run, store)).
script_contract(owns(runtime, console, store)).
script_contract(owns(runtime, error, store)).
script_contract(owns(runtime, listener, store)).
script_contract(owns(runtime, timer, store)).

%% ---- INVARIANTS -------------------------------------------------------------

script_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

script_contract(fact(script_run(handle, field(elem), field(source), field(outcome)), table(run), true)).
script_contract(fact(script_url(handle, field(url)), table(run), field(source) == external)).
script_contract(fact(script_console(field(run), field(level), field(text)), table(console), true)).
script_contract(fact(script_error(field(run), field(phase), field(message)), table(error), true)).
script_contract(fact(script_listener(field(target), field(type), field(run)), table(listener), field(on) == node)).
script_contract(fact(script_window_listener(field(type), field(run)), table(listener), field(on) == window)).
script_contract(fact(script_capture(field(target), field(type)), table(listener), field(capture) == true)).
script_contract(fact(script_timer(handle, field(delay), field(repeats), field(state)), table(timer), true)).
script_contract(fact(script_timer_of(handle, field(run)), table(timer), true)).

script_contract(rules('rules/script')).
script_contract(derived(script_failed/2, 'a script that did not run to its end, and how it ended')).
script_contract(derived(script_uncaught/3, 'an error no code caught: the script, where it was thrown, its message')).
script_contract(derived(script_click_unfocusable/2, 'an element with a click listener that a keyboard cannot reach, and its name')).
script_contract(derived(script_unsettled/2, 'a timer still set when the page came to rest, and the script that set it')).
script_contract(derived(script_listens/3, 'an element, an event it is listened for on, and the script whose listener')).

script_contract(query(errors, script_uncaught(R, P, M), [R, P, M], 'every error no code caught')).
script_contract(query(listeners, script_listens(N, T, R), [N, T, R], 'every element listened on, for what, by which script')).

%% ---- GENERATION -------------------------------------------------------------

script_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, c_dispatch, schema, doc])).

%% ---- VALIDATION -------------------------------------------------------------

script_contract(test(contract, 'tests/contract')).
script_contract(test(integ, 'tests/integ')).
