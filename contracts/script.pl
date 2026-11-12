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
%% how a script came into the document: the parser's, inserted by a script (an element
%% made and connected), or written by document.write
script_contract(enum(insertion, [parsed, inserted, written])).
%% what asked the network for a resource: fetch(), an XMLHttpRequest, an inserted
%% script's src
script_contract(enum(api, [fetch, xhr, script])).
%% how a request ended: a response (ok; http_error, its status 400 or more; opaque, a
%% no-cors response a script cannot read), none (failed: refused, unreachable, a scheme
%% not fetched; blocked: another origin's without CORS, or a file: the page may not
%% read), aborted by the script, or pending when the page came to rest
script_contract(enum(req_outcome, [ok, http_error, opaque, failed, blocked, aborted, pending])).
%% when document.write wrote: while its script ran from the parse (the text parsed at
%% the script's place), after the page loaded (a new document: the old one gone), or
%% ignored (an asynchronously loaded script's)
script_contract(enum(write_when, [parsing, after_load, ignored])).
%% what navigated the page: a form's submission, a link followed, location set (assign,
%% replace, href), a reload
script_contract(enum(nav_cause, [form, link, location, reload])).

script_contract(record(run, [
    field(elem,    xhandle(dom, node)),     % the <script> element, or the element of the on* attribute
    field(source,  enum(source)),
    field(url,     span(text)),             % an external script's URL, as resolved
    field(outcome, enum(outcome)),
    field(layouts, u32),                    % the cascades and layouts its code's reads forced (getComputedStyle, a box's size)
    field(insert,  enum(insertion))
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

%% what a script asked the network for, and how it ended
script_contract(record(request, [
    field(run,      ref(run)),              % the script whose code asked
    field(api,      enum(api)),
    field(method,   span(text)),
    field(url,      span(text)),            % as resolved against the page
    field(resource, xhandle(net, resource)),% the loader's record of it; none when it was not fetched
    field(status,   u32),                   % the response's HTTP status, 0 for none
    field(outcome,  enum(req_outcome)),
    field(cross,    bool)                   % to another origin than the page's
])).
%% a call of document.write or writeln: its script, when, how many bytes it wrote
script_contract(record(write, [
    field(run,   ref(run)),
    field(when,  enum(write_when)),
    field(bytes, u32)
])).

%% a navigation the page made: the request through the loader that leaves it
script_contract(record(navigation, [
    field(run,       ref(run)),             % the script whose code navigated; none for a user's own click
    field(cause,     enum(nav_cause)),
    field(method,    span(text)),
    field(url,       span(text)),           % where to, resolved (a GET form's without its query of fields, whose values are not kept)
    field(form,      xhandle(dom, node)),   % the form submitted, none for another cause
    field(submitter, xhandle(dom, node)),   % the button that submitted it, none for submit()
    field(target,    span(text)),           % the browsing context named (_blank ...), empty for the page's own
    field(resource,  xhandle(net, resource)),  % the loader's fetch of it; none when it was not fetched here
    field(status,    u32),
    field(fields,    u32)                   % the form data set's entries
])).
%% a field a form's submission sent: its name and its control, never its value
script_contract(record(form_field, [
    field(navigation, ref(navigation)),
    field(name,       span(text)),
    field(control,    xhandle(dom, node))
])).

script_contract(handle(run, sr)).
script_contract(handle(console, sc)).
script_contract(handle(error, se)).
script_contract(handle(listener, sl)).
script_contract(handle(timer, st)).
script_contract(handle(request, sq)).
script_contract(handle(write, sw)).
script_contract(handle(navigation, sn)).
script_contract(handle(form_field, sf)).

%% ---- OWNERSHIP --------------------------------------------------------------

script_contract(store(runtime, [table(run), table(console), table(error), table(listener), table(timer), table(request), table(write),
    table(navigation), table(form_field), pool(text)])).
script_contract(owns(runtime, run, store)).
script_contract(owns(runtime, console, store)).
script_contract(owns(runtime, error, store)).
script_contract(owns(runtime, listener, store)).
script_contract(owns(runtime, timer, store)).
script_contract(owns(runtime, request, store)).
script_contract(owns(runtime, write, store)).
script_contract(owns(runtime, navigation, store)).
script_contract(owns(runtime, form_field, store)).

%% ---- INVARIANTS -------------------------------------------------------------

script_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

script_contract(fact(script_run(handle, field(elem), field(source), field(outcome)), table(run), true)).
script_contract(fact(script_url(handle, field(url)), table(run), field(source) == external)).
%% a script whose reads of the page's style or geometry made the cascade and the layout
%% run, and how many times: each read after a change to the DOM runs them anew
script_contract(fact(script_forced_layout(handle, field(layouts)), table(run), field(layouts) \== 0)).
%% a script a script inserted or document.write wrote
script_contract(fact(script_inserted(handle, field(insert)), table(run), field(insert) \== parsed)).
%% what each script asked the network for: what asked, by which method, for what URL,
%% how it ended; who asked, the status, the loader's record, whether it crossed origins
script_contract(fact(script_request(handle, field(api), field(method), field(url), field(outcome)), table(request), true)).
script_contract(fact(script_request_by(handle, field(run)), table(request), true)).
script_contract(fact(script_request_status(handle, field(status)), table(request), field(status) \== 0)).
script_contract(fact(script_request_resource(handle, field(resource)), table(request), true)).
script_contract(fact(script_request_cross(handle), table(request), field(cross) == true)).
script_contract(fact(script_write(handle, field(run), field(when), field(bytes)), table(write), true)).
%% each navigation: what made it, by which method, to where; who, the form and its
%% submitter, the context it was for, the loader's fetch, its status; the fields sent
script_contract(fact(script_navigation(handle, field(cause), field(method), field(url)), table(navigation), true)).
script_contract(fact(script_navigation_by(handle, field(run)), table(navigation), true)).
script_contract(fact(script_navigation_form(handle, field(form), field(submitter)), table(navigation), field(cause) == form)).
script_contract(fact(script_navigation_target(handle, field(target)), table(navigation), true)).
script_contract(fact(script_navigation_resource(handle, field(resource)), table(navigation), true)).
script_contract(fact(script_navigation_status(handle, field(status)), table(navigation), field(status) \== 0)).
script_contract(fact(script_form_field(field(navigation), field(name), field(control)), table(form_field), true)).
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
script_contract(derived(script_request_blocked/2, 'a request a script made that the browser blocked, and its URL: another origin''s without CORS, or a file: URL')).
script_contract(derived(script_request_failed/3, 'a request a script made that had no good answer, its URL and how it ended')).
script_contract(derived(script_insecure_request/2, 'a request over http: from a page served over https:, and its URL')).
script_contract(derived(script_write_replaced/1, 'a script whose document.write after the page loaded replaced the whole document')).
script_contract(derived(script_write_ignored/1, 'a script loaded asynchronously whose document.write was ignored')).
script_contract(derived(script_form_cross_origin_http/2, 'a form submitted to another origin over http:, and where to')).
script_contract(derived(script_form_password_http/2, 'a form submitted over http: with a password field, and the field')).
script_contract(derived(script_form_password_get/2, 'a form whose GET put a password field in its URL, and the field')).
script_contract(derived(script_key_unfocusable/2, 'an element with a key listener that neither it nor anything inside it can take focus for, and its name')).
script_contract(derived(script_layout_thrash/2, 'a script whose reads of style or geometry, each after a change, made the cascade and the layout run many times, and how many')).

script_contract(query(errors, script_uncaught(R, P, M), [R, P, M], 'every error no code caught')).
script_contract(query(listeners, script_listens(N, T, R), [N, T, R], 'every element listened on, for what, by which script')).
script_contract(query(requests, script_request(Q, A, M, U, O), [Q, A, M, U, O], 'every request the scripts made, and how it ended')).
script_contract(query(navigations, script_navigation(N, C, M, U), [N, C, M, U], 'every navigation the page made: why, how and where to')).

%% ---- GENERATION -------------------------------------------------------------

script_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, c_dispatch, schema, doc])).

%% ---- VALIDATION -------------------------------------------------------------

script_contract(test(contract, 'tests/contract')).
script_contract(test(integ, 'tests/integ')).
