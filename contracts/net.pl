%% contracts/net.pl -- the network module's contract (docs/contract.md is the format).
%%
%% URLs parsed and resolved (a WHATWG subset), resources fetched over file:,
%% data:, http: and https: (HTTP/1.1: Content-Length, chunked, redirects within a
%% bound; TLS 1.2 and 1.3 through the system's OpenSSL, the certificate chain and
%% the host name verified; a proxy from the environment), and what a page loads --
%% its stylesheets, images and scripts -- each with the element and the resource
%% that asked for it, its origin, its redirects, how its fetch ended and why. Same-
%% origin decisions are made here in C (a page served over http: may not load a
%% file: URL); the reasoner sees the dependency graph, the origins and what each
%% fetch rode on, and says what failed and what crossed origins. src/net/net.c says
%% what of URLs and HTTP the module does.

net_contract(module(net, '0.2.0', 'Networking: URLs resolved, resources fetched over file:, data:, http: and https:, what a page loaded, why, and from where')).

%% ---- TYPES ------------------------------------------------------------------

net_contract(status([ok, stale_handle, wrong_kind, failed])).

net_contract(enum(kind, [document, stylesheet, image, script, other])).
%% what the HTTP cache (RFC 9111) did for a resource: unused (no cache, or not
%% http:), stored (got over the network and kept), fresh (answered from the cache,
%% no request made), revalidated (its stale copy confirmed by a 304), not_stored (got
%% over the network, and not kept: no-store, a Vary, a status other than 200, or
%% neither a lifetime nor a validator)
net_contract(enum(cache_use, [unused, stored, fresh, revalidated, not_stored])).
%% how a fetch ended: tls_handshake_failed is a TLS session that could not be set
%% up, tls_verify_failed a server whose certificate chain or name did not verify,
%% proxy_refused a proxy that would not open the tunnel, decoding_failed a body in a
%% content coding not read here or that did not decode
net_contract(enum(outcome, [ok, http_error, no_such_file, refused, unreachable, too_many_redirects, unsupported_scheme,
    bad_url, blocked, too_large, malformed, tls_handshake_failed, tls_verify_failed, proxy_refused, decoding_failed])).

net_contract(record(resource, [
    field(url,       span(text)),            % the absolute URL asked for
    field(final_url, span(text)),            % where it was got, after its redirects
    field(kind,      enum(kind)),
    field(outcome,   enum(outcome)),
    field(status,    u32),                   % HTTP's status; 200 for a file: or data: resource read
    field(type,      span(text)),            % its media type, as served or by its file name
    field(size,      u32),                   % its body's bytes
    field(body,      span(bytes)),
    field(origin,    span(text)),            % its origin, serialized: null for an opaque one
    field(from,      ref(resource)),         % the page that asked for it
    field(elem,      xhandle(dom, node)),    % the element that asked for it
    field(width,     u32),                   % an image's size, from its header (divided by its density, for one from a srcset)
    field(height,    u32),
    field(reason,    span(text)),            % why its fetch did not end well, in words: the server's, the TLS library's, the proxy's, the system's
    field(tls,       bool),                  % its last exchange went over TLS, its server verified
    field(tls_version, span(text)),          % TLSv1.2 or TLSv1.3
    field(tls_cipher, span(text)),
    field(alpn,      span(text)),            % the protocol the server chose by ALPN, or empty
    field(proxied,   bool),                  % its last exchange went through a proxy
    field(proxy,     span(text)),            % that proxy's origin
    field(cross_site, bool),                 % asked for by a page of another site (scheme and registrable domain)
    field(cache,     enum(cache_use)),       % what the HTTP cache did for it
    field(cache_age, i64),                   % the cached response's age then, in seconds
    field(cache_lifetime, i64),              % and its freshness lifetime
    field(coded,     bool),                  % its body came in a content coding, decoded
    field(coding,    span(text)),            % that coding, as Content-Encoding named it: gzip, deflate
    field(encoded_size, u32),                % the body's bytes as they came
    field(srcset,    bool),                  % an image chosen from its element's srcset
    field(descriptor, span(text)),           % the candidate's descriptor as written (2x, 800w), or empty for 1x and src
    field(density,   f64),                   % its pixel density, for a device pixel ratio of 1
    field(source_size, f64),                 % the sizes attribute's length in the viewport, px (100vw by default)
    field(candidates, u32)                   % how many candidates there were, src among them when it was one
])).
%% a redirect taken on the way to a resource
net_contract(record(hop, [field(resource, ref(resource)), field(url, span(text)), field(status, u32)])).

%% a cookie's SameSite: unset (taken as Lax), lax, strict, or no_restriction for
%% SameSite=None (none is the empty ref's word)
net_contract(enum(same_site, [unset, lax, strict, no_restriction])).
%% what became of a cookie: kept in the jar; replaced by a newer one of its name,
%% domain and path; expired, or deleted by its server; refused when it was set
net_contract(enum(cookie_state, [kept, replaced, expired, refused])).
%% why a Set-Cookie was refused (RFC 6265bis 5.6, 5.7): malformed, too_large (name
%% and value past 4096 bytes), secure_over_http (Secure from a response not over
%% TLS), domain_mismatch (a Domain the host is not in), public_suffix (a Domain of
%% a public suffix), prefix (a __Secure- or __Host- name without what the prefix
%% asks), cross_site (SameSite other than None from a response to another site's
%% page), none_insecure (SameSite=None without Secure), overwrites_secure (over
%% http:, a name a Secure cookie of the domain holds), too_many (past 3000)
net_contract(enum(refusal, [accepted, malformed, too_large, secure_over_http, domain_mismatch, public_suffix, prefix,
    cross_site, none_insecure, overwrites_secure, too_many])).

%% a cookie of the jar (RFC 6265bis): those kept in the jar's file between runs,
%% and those the page's responses set or tried to
net_contract(record(cookie, [
    field(name,       span(text)),
    field(value,      span(text)),           % never a fact: a value may be a secret
    field(domain,     span(text)),           % the host it came from, or its Domain attribute's
    field(path,       span(text)),
    field(host_only,  bool),                 % sent to its domain alone, not to its subdomains
    field(secure,     bool),                 % sent over TLS alone
    field(http_only,  bool),                 % hidden from scripts
    field(same_site,  enum(same_site)),
    field(persistent, bool),                 % it has an expiry, and is kept in the jar's file
    field(expires,    i64),                  % that expiry, seconds since the epoch, at most 400 days on
    field(created,    i64),
    field(state,      enum(cookie_state)),
    field(refusal,    enum(refusal)),
    field(set_by,     ref(resource)),        % the response that set it in this run; none for one from the jar's file
    field(seen,       bool)                  % set, or sent, while the page loaded: its facts are the snapshot's
])).
%% a cookie sent with a request for a resource
net_contract(record(sent, [field(resource, ref(resource)), field(cookie, ref(cookie))])).
%% where the loader keeps what outlives a run: the cookie jar's file and the
%% cache's directory, either empty for none; whether the jar changed since read
net_contract(record(keeping, [field(jar, span(text)), field(cache, span(text)), field(dirty, bool)])).

net_contract(handle(resource, rs)).
net_contract(handle(hop, hp)).
net_contract(handle(cookie, ck)).
net_contract(handle(sent, sn)).
net_contract(handle(keeping, kp)).

%% ---- OWNERSHIP --------------------------------------------------------------

net_contract(store(loader, [table(resource), table(hop), table(cookie), table(sent), table(keeping), pool(text), pool(bytes)])).
net_contract(owns(loader, resource, store)).
net_contract(owns(loader, hop, store)).
net_contract(owns(loader, cookie, store)).
net_contract(owns(loader, sent, store)).
net_contract(owns(loader, keeping, store)).

%% ---- OPERATIONS -------------------------------------------------------------

%% where the jar's file and the cache's directory are (a profile's), each empty for
%% none: cookies then last the run, and nothing is cached. The jar's cookies are read,
%% and every fetch after keeps it up to date
net_contract(operation(keep, [param(jar, text), param(cache, text)], status, [], [], [mutates])).
%% a resource fetched: its record, whatever the outcome
net_contract(operation(fetch, [param(url, text), param(kind, enum(kind))], handle(resource), [], [creates(resource)], [mutates])).
%% the stylesheets, images and scripts the page's document asks for, fetched; the
%% kinds denied (bit k for the kind k of enum(kind): the profile's permissions for
%% the page's site) are blocked, never fetched
net_contract(operation(fetch_subresources, [param(doc, store(dom)), param(page, handle(resource)), param(denied, u32)], status,
    [pre(live(page), stale_handle)], [creates(resource)], [mutates])).

%% ---- INVARIANTS -------------------------------------------------------------

net_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

net_contract(fact(net_resource(handle, field(url), field(kind), field(outcome), field(status), field(type), field(size)), table(resource), true)).
net_contract(fact(net_final_url(handle, field(final_url)), table(resource), true)).
net_contract(fact(net_origin(handle, field(origin)), table(resource), true)).
net_contract(fact(net_asked_by(handle, field(from), field(elem)), table(resource), field(from) \== none)).
net_contract(fact(net_hop(field(resource), field(url), field(status)), table(hop), true)).
net_contract(fact(net_image_size(handle, field(width), field(height)), table(resource), field(kind) == image)).
net_contract(fact(net_failure(handle, field(reason)), table(resource), field(outcome) \== ok)).
net_contract(fact(net_tls(handle, field(tls_version), field(tls_cipher), field(alpn)), table(resource), field(tls) == true)).
net_contract(fact(net_proxy(handle, field(proxy)), table(resource), field(proxied) == true)).
net_contract(fact(net_cross_site(handle), table(resource), field(cross_site) == true)).
net_contract(fact(net_cache(handle, field(cache), field(cache_age), field(cache_lifetime)), table(resource), field(cache) \== unused)).
net_contract(fact(net_coding(handle, field(coding), field(encoded_size)), table(resource), field(coded) == true)).
net_contract(fact(net_srcset(handle, field(descriptor), field(density), field(source_size), field(candidates)), table(resource), field(srcset) == true)).
%% the page's cookies, those its responses set or tried to and those sent for it:
%% names, domains, paths and flags, never a value
net_contract(fact(net_cookie(handle, field(name), field(domain), field(path), field(state)), table(cookie), field(seen) == true)).
net_contract(fact(net_cookie_flags(handle, field(host_only), field(secure), field(http_only), field(same_site)), table(cookie), field(seen) == true)).
net_contract(fact(net_cookie_expires(handle, field(expires)), table(cookie), (field(seen) == true, field(persistent) == true))).
net_contract(fact(net_cookie_set_by(handle, field(set_by)), table(cookie), field(set_by) \== none)).
net_contract(fact(net_cookie_refused(handle, field(refusal)), table(cookie), field(state) == refused)).
net_contract(fact(net_cookie_sent(field(resource), field(cookie)), table(sent), true)).

net_contract(rules('rules/net')).
net_contract(derived(net_page/1, 'a document fetched as a page')).
net_contract(derived(net_depends/2, 'a resource the page needs, directly or through another')).
net_contract(derived(net_cross_origin/2, 'a resource from another origin than the page that asked for it, neither opaque')).
net_contract(derived(net_failed/2, 'a resource whose fetch did not end well, and how it ended')).
net_contract(derived(net_redirects/2, 'the URLs a resource was redirected through, in order')).
net_contract(derived(net_secure/2, 'a resource got over TLS from a verified server, and the TLS version')).
net_contract(derived(net_mixed_content/2, 'a part that a page served over TLS got over http:')).
net_contract(derived(net_cookies_sent/2, 'the names of the cookies sent for a resource, in the order sent')).
net_contract(derived(net_third_party_cookie/2, 'a cookie sent for a resource of another site than the page''s')).
net_contract(derived(net_from_cache/2, 'a resource the cache answered, fresh or revalidated')).
net_contract(derived(net_low_density/2, 'an image chosen from a srcset at a density below the screen''s, and that density')).

net_contract(query(loaded, (net_resource(R, U, K, O, S, _, _)), [R, U, K, O, S], 'every resource, what it is and how its fetch ended')).

net_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
net_contract(test(integ, 'tests/integ/net')).
