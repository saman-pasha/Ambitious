%% contracts/net.pl -- the network module's contract (docs/contract.md is the format).
%%
%% URLs parsed, resolved and serialized as the URL standard has them (src/net/url.c:
%% its state machine, its percent-encode sets, IPv4 and IPv6 hosts, opaque hosts and
%% paths, and IDNA as UTS #46 makes a domain ASCII: the mapping table, NFC, the
%% validity, joiner and bidi rules, Punycode both ways; the API's setters, and
%% application/x-www-form-urlencoded), resources fetched over file:, data:, http: and https:
%% (HTTP/1.1 over connections kept alive, and HTTP/2 over TLS when the server's ALPN
%% chooses it: Content-Length, chunked, redirects within a bound, gzip, deflate, br and
%% zstd; TLS 1.2 and 1.3 through the system's OpenSSL, the certificate chain and the host
%% name verified; a proxy from the environment), and what a page loads -- its
%% stylesheets, images (from a picture's source, or a srcset, where there is one) and scripts -- each with
%% the element and the resource that asked for it, its origin, its redirects, how
%% its fetch ended and why. The loader keeps a cookie jar (RFC 6265bis) and an HTTP
%% cache (RFC 9111), in a profile's directory when there is one. Same-origin and
%% same-site decisions are made here in C (a page served over http: may not load a
%% file: URL; SameSite cookies stay with their site); the reasoner sees the
%% dependency graph, the origins, what each fetch rode on (its TLS, its protocol, the
%% connection and stream that carried it), the page's cookies (never
%% a value) and what the cache did, and says what failed, what crossed origins and
%% which cookies were refused. The page's frames are records here too (Phase 12u): each
%% iframe's document, where it came from and what became of it, which src/shell/frame.c
%% makes as it loads them. src/net/net.c says what of each the module does.

net_contract(module(net, '0.5.0', 'Networking: URLs resolved, resources fetched over file:, data:, http: and https:, what a page loaded, why, and from where; its frames')).

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
%% content coding not read here or that did not decode, reset an HTTP/2 stream the
%% server reset or a connection that closed or went away before its response ended
net_contract(enum(outcome, [ok, http_error, no_such_file, refused, unreachable, too_many_redirects, unsupported_scheme,
    bad_url, blocked, too_large, malformed, tls_handshake_failed, tls_verify_failed, proxy_refused, decoding_failed, reset])).
%% what a resource's last request went over: unsent (none went over the network: a file:
%% or data: URL, the cache's fresh copy, a connection that could not be made), HTTP/1.1,
%% or HTTP/2
net_contract(enum(protocol, [unsent, http1, h2])).

net_contract(record(resource, [
    field(url,       span(text)),            % the absolute URL asked for
    field(final_url, span(text)),            % where it was got, after its redirects
    field(kind,      enum(kind)),
    field(outcome,   enum(outcome)),
    field(status,    u32),                   % HTTP's status; 200 for a file: or data: resource read
    field(type,      span(text)),            % its media type, as served or by its file name
    field(charset,   span(text)),            % the charset its Content-Type named, or empty
    field(size,      u32),                   % its body's bytes
    field(body,      span(bytes)),
    field(origin,    span(text)),            % its origin, serialized: null for an opaque one
    field(from,      ref(resource)),         % the page that asked for it
    field(elem,      xhandle(dom, node)),    % the element that asked for it
    field(width,     u32),                   % an image's size, from its header (divided by its density and floored, as naturalWidth is, for one from a srcset)
    field(height,    u32),
    field(reason,    span(text)),            % why its fetch did not end well, in words: the server's, the TLS library's, the proxy's, the system's
    field(tls,       bool),                  % its last exchange went over TLS, its server verified
    field(tls_version, span(text)),          % TLSv1.2 or TLSv1.3
    field(tls_cipher, span(text)),
    field(alpn,      span(text)),            % the protocol the server chose by ALPN (h2, http/1.1), or empty
    field(proxied,   bool),                  % its last exchange went through a proxy
    field(proxy,     span(text)),            % that proxy's origin
    field(cross_site, bool),                 % asked for by a page of another site (scheme and registrable domain)
    field(cache,     enum(cache_use)),       % what the HTTP cache did for it
    field(cache_age, i64),                   % the cached response's age then, in seconds
    field(cache_lifetime, i64),              % and its freshness lifetime
    field(coded,     bool),                  % its body came in a content coding, decoded
    field(coding,    span(text)),            % that coding, as Content-Encoding named it: gzip, deflate, br, zstd
    field(encoded_size, u32),                % the body's bytes as they came
    field(srcset,    bool),                  % an image chosen from its element's srcset
    field(descriptor, span(text)),           % the candidate's descriptor as written (2x, 800w), or empty for 1x and src
    field(density,   f64),                   % its candidate's pixel density, chosen at the environment's device pixel ratio (net_set_env)
    field(source_size, f64),                 % the sizes attribute's length in the viewport, px (100vw by default)
    field(candidates, u32),                  % how many candidates there were, src among them when it was one
    field(picture,   bool),                  % an image chosen from a <source> of its img's <picture>
    field(source,    xhandle(dom, node)),    % that <source>
    field(protocol,  enum(protocol)),        % what its last request went over
    field(connection, u32),                  % the connection that carried it, numbered from 1 in the run
    field(stream,    u32),                   % its HTTP/2 stream; 0 over HTTP/1.1
    field(reused,    bool),                  % that connection had carried a request before it
    field(pooled,    bool),                  % that connection is an HTTP/2 one made for another host at the same address, whose certificate covers this one's (pooling by IP)
    field(tls_resumed, bool),                % its TLS session was resumed from an earlier connection's to its server (a ticket, a session ID)
    field(dict_sent, bool),                  % its last request offered a compression dictionary (RFC 9842)
    field(dict_offered, span(text)),         % that dictionary, as Available-Dictionary named it (its SHA-256, :base64:)
    field(dict_kept, bool),                  % its response was kept as a compression dictionary (its Use-As-Dictionary)
    field(dict_hash, span(text)),            % that dictionary's name, as a request offering it names it
    field(dict_match, span(text)),           % its match, the URL pattern of the URLs it is offered for
    field(dict_id, span(text))               % its id, or empty
])).
%% a redirect taken on the way to a resource
net_contract(record(hop, [field(resource, ref(resource)), field(url, span(text)), field(status, u32)])).

%% a frame's document came from: its iframe's srcdoc (about:srcdoc, its parent's base URL),
%% its src fetched, or none of them (about:blank)
net_contract(enum(frame_source, [srcdoc, src, blank])).
%% what became of a frame (Phase 12u): its document loaded; its src's fetch failed (Chromium's
%% error page, which draws nothing); blocked (a page that frames its own URL twice over, or
%% past the frames a page has here); lazy (loading=lazy and out of the viewport's reach: not
%% loaded, as Chromium leaves it at rest)
net_contract(enum(frame_outcome, [loaded, failed, blocked, lazy])).
%% a frame of the page (an iframe's document), at any depth: its iframe element (a node of
%% its parent document); its own document's number and its parent's (the page's is 1); its
%% URL; where its document came from and what became of it; the fetch of its src, in the
%% page's loader (none for srcdoc and about:blank); its viewport (the iframe's content box,
%% less any scroll bar its content made: classic ones, 15px); its sandbox (an iframe's
%% sandbox attribute), whether its scripts may run there, and whether its origin is its
%% parent's; its document's mode
net_contract(record(frame, [
    field(iframe,    xhandle(dom, node)),
    field(doc,       u32),
    field(parent,    u32),
    field(url,       span(text)),
    field(source,    enum(frame_source)),
    field(outcome,   enum(frame_outcome)),
    field(resource,  ref(resource)),
    field(width,     f64),
    field(height,    f64),
    field(vbar,      bool),
    field(hbar,      bool),
    field(sandboxed, bool),
    field(scripts,   bool),
    field(same_origin, bool),
    field(quirks,    bool)
])).

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
net_contract(handle(frame, fm)).

%% ---- OWNERSHIP --------------------------------------------------------------

net_contract(store(loader, [table(resource), table(hop), table(cookie), table(sent), table(keeping), table(frame), pool(text), pool(bytes)])).
net_contract(owns(loader, frame, store)).
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
%% the protocol a resource's request went over, the connection (by its number in the run)
%% and HTTP/2 stream that carried it, and whether the connection had carried one before
net_contract(fact(net_http(handle, field(protocol), field(connection), field(stream), field(reused)), table(resource),
    field(protocol) \== unsent)).
%% a resource carried by an HTTP/2 connection made for another host (pooling by IP: the same
%% address and port, the connection's certificate covering this one's host, no proxy between)
net_contract(fact(net_pooled(handle), table(resource), field(pooled) == true)).
%% a resource whose TLS session was resumed from one an earlier connection to its server left
net_contract(fact(net_tls_resumed(handle), table(resource), field(tls_resumed) == true)).
%% a response kept as a compression dictionary (RFC 9842): its name (its SHA-256 as
%% Available-Dictionary writes it), its match, its id; and the dictionary a request offered
net_contract(fact(net_dictionary(handle, field(dict_hash), field(dict_match), field(dict_id)), table(resource), field(dict_kept) == true)).
net_contract(fact(net_dictionary_offered(handle, field(dict_offered)), table(resource), field(dict_sent) == true)).
net_contract(fact(net_proxy(handle, field(proxy)), table(resource), field(proxied) == true)).
net_contract(fact(net_cross_site(handle), table(resource), field(cross_site) == true)).
net_contract(fact(net_cache(handle, field(cache), field(cache_age), field(cache_lifetime)), table(resource), field(cache) \== unused)).
net_contract(fact(net_coding(handle, field(coding), field(encoded_size)), table(resource), field(coded) == true)).
net_contract(fact(net_srcset(handle, field(descriptor), field(density), field(source_size), field(candidates)), table(resource), field(srcset) == true)).
%% the <source> of a <picture> whose srcset an img took its image from (its media held,
%% its type is one the painter decodes)
net_contract(fact(net_picture_source(handle, field(source)), table(resource), field(picture) == true)).
%% the page's cookies, those its responses set or tried to and those sent for it:
%% names, domains, paths and flags, never a value
net_contract(fact(net_cookie(handle, field(name), field(domain), field(path), field(state)), table(cookie), field(seen) == true)).
net_contract(fact(net_cookie_flags(handle, field(host_only), field(secure), field(http_only), field(same_site)), table(cookie), field(seen) == true)).
net_contract(fact(net_cookie_expires(handle, field(expires)), table(cookie), (field(seen) == true, field(persistent) == true))).
net_contract(fact(net_cookie_set_by(handle, field(set_by)), table(cookie), field(set_by) \== none)).
net_contract(fact(net_cookie_refused(handle, field(refusal)), table(cookie), field(state) == refused)).
net_contract(fact(net_cookie_sent(field(resource), field(cookie)), table(sent), true)).
%% the page's frames: each with its iframe, its document's number, its URL, where its
%% document came from and what became of it; its parent document's number; the fetch of its
%% src; its viewport and the scroll bars drawn in it; its sandbox; its document's mode
net_contract(fact(net_frame(handle, field(iframe), field(doc), field(url), field(source), field(outcome)), table(frame), true)).
net_contract(fact(net_frame_parent(handle, field(parent)), table(frame), true)).
net_contract(fact(net_frame_fetch(handle, field(resource)), table(frame), field(resource) \== none)).
net_contract(fact(net_frame_viewport(handle, field(width), field(height), field(vbar), field(hbar)), table(frame), field(outcome) == loaded)).
net_contract(fact(net_frame_sandbox(handle, field(scripts), field(same_origin)), table(frame), field(sandboxed) == true)).
net_contract(fact(net_frame_quirks(handle), table(frame), field(quirks) == true)).

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
net_contract(derived(net_dictionary_from/2, 'a resource whose request offered a compression dictionary, and the resource of the page that kept it')).
net_contract(derived(net_frame_of/2, 'the frame an iframe element holds')).
net_contract(derived(net_frame_failed/2, 'a frame whose document did not load, and why: its fetch''s failure, blocked or lazy')).
net_contract(derived(net_frame_depth/2, 'how deep a frame is: 1 for one of the page''s own iframes')).
net_contract(derived(net_frame_scrolls/2, 'a frame whose content overflows its viewport, and the scroll bars drawn in it')).

net_contract(query(loaded, (net_resource(R, U, K, O, S, _, _)), [R, U, K, O, S], 'every resource, what it is and how its fetch ended')).
net_contract(query(cookies, (net_cookie(C, N, D, P, S)), [C, N, D, P, S], 'the page''s cookies: name, domain, path, and what became of each')).
net_contract(query(cached, (net_from_cache(R, How)), [R, How], 'every resource the HTTP cache answered, and how')).
net_contract(query(frames, (net_frame(F, I, D, U, S, O)), [F, I, D, U, S, O], 'every frame: its iframe, its document, its URL, where it came from, what became of it')).

net_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
net_contract(test(integ, 'tests/integ/net')).
