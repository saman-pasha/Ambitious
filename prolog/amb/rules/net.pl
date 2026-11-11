%% prolog/amb/rules/net.pl -- what the network module's facts mean (contracts/net.pl).
%%
%% A page is a document fetched for itself; the resources it needs are those its
%% elements asked for, and theirs in turn. An origin is a scheme, a host and a
%% port; an opaque one (a file: or data: URL's) is null, and same as nothing.
%% No clause here cuts: amb_why/2 proves these rules clause by clause.
:- dynamic net_resource/7, net_final_url/2, net_origin/2, net_asked_by/3, net_hop/3, net_image_size/3,
           net_failure/2, net_tls/4, net_proxy/2, net_cross_site/1, net_cookie/5, net_cookie_flags/5,
           net_cookie_expires/2, net_cookie_set_by/2, net_cookie_refused/2, net_cookie_sent/2, net_cache/4.

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

%% a resource got over TLS, its server verified, and the TLS version
net_secure(R, V) :- net_tls(R, V, _, _).

%% a part a page served over TLS got over http: instead, mixed content
net_mixed_content(P, R) :-
    net_asked_by(R, P, _), net_tls(P, _, _, _), net_final_url(R, U), sub_atom(U, 0, _, _, 'http:').

%% the names of the cookies sent for a resource, in the order they were sent
net_cookies_sent(R, Names) :-
    net_resource(R, _, _, _, _, _, _),
    findall(N, ( net_cookie_sent(R, C), net_cookie(C, N, _, _, _) ), Names), Names \== [].

%% a cookie that went with a request for another site than the page's: a third party's
net_third_party_cookie(C, R) :- net_cookie_sent(R, C), net_cross_site(R).

%% a resource the HTTP cache answered: fresh, with no request, or revalidated by a 304
net_from_cache(R, How) :- net_cache(R, How, _, _), ( How == fresh ; How == revalidated ).

%% a refusal in words
net_refusal_words(malformed, 'it is malformed').
net_refusal_words(too_large, 'its name and value pass 4096 bytes').
net_refusal_words(secure_over_http, 'it is Secure but came over http:').
net_refusal_words(domain_mismatch, 'its Domain does not hold the host').
net_refusal_words(public_suffix, 'its Domain is a public suffix').
net_refusal_words(prefix, 'its name''s prefix asks for what it lacks').
net_refusal_words(cross_site, 'it came from another site''s response and is not SameSite=None').
net_refusal_words(none_insecure, 'it is SameSite=None but not Secure').
net_refusal_words(overwrites_secure, 'it would take the place of a Secure cookie over http:').
net_refusal_words(too_many, 'the jar holds 3000 cookies').

%% the outcomes that do not say why by themselves: their diagnostic adds the reason
%% (net_failure/2 has every failure's)
net_says_why(malformed).
net_says_why(tls_handshake_failed).
net_says_why(tls_verify_failed).
net_says_why(proxy_refused).
net_says_why(decoding_failed).

%% ---- what the reasoner says of what a page loaded --------------------------------

net_diag(failed, R, Text) :-
    net_failed(R, Why), net_resource(R, U, K, O, _, _, _),
    (   net_asked_by(R, _, E), E \== none, dom_element(E, Name) -> true ; Name = page ),
    (   net_says_why(O), net_failure(R, Reason), Reason \== '' -> format(atom(Text), 'the ~w ~w (asked for by <~w>) failed: ~w (~w)', [K, U, Name, Why, Reason])
    ;   format(atom(Text), 'the ~w ~w (asked for by <~w>) failed: ~w', [K, U, Name, Why])
    ).
net_diag(cross_origin, R, Text) :-
    net_cross_origin(P, R), net_resource(R, U, K, ok, _, _, _), net_origin(R, O), net_origin(P, OP),
    format(atom(Text), 'the ~w ~w comes from ~w, not the page''s ~w', [K, U, O, OP]).
net_diag(redirected, R, Text) :-
    net_redirects(R, Hops), net_resource(R, U, _, _, _, _, _), net_final_url(R, F), length(Hops, N),
    format(atom(Text), '~w was redirected ~d time(s), to ~w', [U, N, F]).
net_diag(mixed_content, R, Text) :-
    net_mixed_content(P, R), net_resource(R, U, K, _, _, _, _), net_final_url(P, PU),
    format(atom(Text), 'the ~w ~w came over http: to a page served over TLS (~w)', [K, U, PU]).
net_diag(cookie_refused, C, Text) :-
    net_cookie_refused(C, Why), net_cookie(C, N, D, _, _), net_cookie_set_by(C, R), net_resource(R, U, _, _, _, _, _),
    net_refusal_words(Why, W),
    format(atom(Text), 'the cookie ~q of ~w, set by ~w, was refused: ~w', [N, D, U, W]).
net_diag(no_lifetime, R, Text) :-
    net_cache(R, How, _, 0), ( How == stored ; How == revalidated ), net_resource(R, U, K, _, _, _, _),
    format(atom(Text), 'the ~w ~w has no freshness lifetime: each load asks the server for it again', [K, U]).
net_diag(third_party_cookie, C, Text) :-
    net_third_party_cookie(C, R), net_cookie(C, N, D, _, _), net_resource(R, U, _, _, _, _, _),
    format(atom(Text), 'the cookie ~q of ~w went with ~w, another site''s', [N, D, U]).
