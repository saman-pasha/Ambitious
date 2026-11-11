%% prolog/amb/rules/net.pl -- what the network module's facts mean (contracts/net.pl).
%%
%% A page is a document fetched for itself; the resources it needs are those its
%% elements asked for, and theirs in turn. An origin is a scheme, a host and a
%% port; an opaque one (a file: or data: URL's) is null, and same as nothing.
%% No clause here cuts: amb_why/2 proves these rules clause by clause.
:- dynamic net_resource/7, net_final_url/2, net_origin/2, net_asked_by/3, net_hop/3, net_image_size/3.

net_page(R) :- net_resource(R, _, document, _, _, _, _), \+ net_asked_by(R, _, _).

%% a resource the page needs: asked for by it, or by what it needs
net_depends(P, R) :- net_asked_by(R, P, _).
net_depends(P, R) :- net_asked_by(Q, P, _), net_depends(Q, R).

%% a resource from another origin than the page that asked for it; an opaque
%% origin (a data: or file: URL's) is neither the same nor another
net_cross_origin(P, R) :-
    net_asked_by(R, P, _), net_origin(P, OP), net_origin(R, OR),
    OP \== null, OR \== null, OP \== OR.

%% how a fetch that did not end well ended: its outcome, and its status if HTTP's
net_failed(R, Why) :-
    net_resource(R, _, _, O, S, _, _), O \== ok,
    (   O == http_error -> Why = http_error(S) ; Why = O ).

%% the URLs a resource was redirected through
net_redirects(R, Urls) :- net_resource(R, _, _, _, _, _, _), findall(U-S, net_hop(R, U, S), Urls), Urls \== [].

%% ---- what the reasoner says of what a page loaded --------------------------------

net_diag(failed, R, Text) :-
    net_failed(R, Why), net_resource(R, U, K, _, _, _, _),
    (   net_asked_by(R, _, E), E \== none, dom_element(E, Name) -> true ; Name = page ),
    format(atom(Text), 'the ~w ~w (asked for by <~w>) failed: ~w', [K, U, Name, Why]).
net_diag(cross_origin, R, Text) :-
    net_cross_origin(P, R), net_resource(R, U, K, ok, _, _, _), net_origin(R, O), net_origin(P, OP),
    format(atom(Text), 'the ~w ~w comes from ~w, not the page''s ~w', [K, U, O, OP]).
net_diag(redirected, R, Text) :-
    net_redirects(R, Hops), net_resource(R, U, _, _, _, _, _), net_final_url(R, F), length(Hops, N),
    format(atom(Text), '~w was redirected ~d time(s), to ~w', [U, N, F]).
