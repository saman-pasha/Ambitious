%% contracts/net.pl -- the network module's contract (docs/contract.md is the format).
%%
%% URLs parsed and resolved (a WHATWG subset), resources fetched over file:,
%% data: and http: (HTTP/1.1: Content-Length, chunked, redirects within a
%% bound), and what a page loads -- its stylesheets, images and scripts -- each
%% with the element and the resource that asked for it, its origin, its
%% redirects and how its fetch ended. Same-origin decisions are made here in C
%% (a page served over http: may not load a file: URL); the reasoner sees the
%% dependency graph and the origins, and says what failed and what crossed
%% origins. src/net/net.c says what of URLs and HTTP the module does.

net_contract(module(net, '0.1.0', 'Networking: URLs resolved, resources fetched over file:, data: and http:, what a page loaded, why, and from where')).

%% ---- TYPES ------------------------------------------------------------------

net_contract(status([ok, stale_handle, wrong_kind, failed])).

net_contract(enum(kind, [document, stylesheet, image, script, other])).
%% how a fetch ended
net_contract(enum(outcome, [ok, http_error, no_such_file, refused, unreachable, too_many_redirects, unsupported_scheme,
    bad_url, blocked, too_large, malformed])).

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
    field(width,     u32),                   % an image's size, from its header
    field(height,    u32)
])).
%% a redirect taken on the way to a resource
net_contract(record(hop, [field(resource, ref(resource)), field(url, span(text)), field(status, u32)])).

net_contract(handle(resource, rs)).
net_contract(handle(hop, hp)).

%% ---- OWNERSHIP --------------------------------------------------------------

net_contract(store(loader, [table(resource), table(hop), pool(text), pool(bytes)])).
net_contract(owns(loader, resource, store)).
net_contract(owns(loader, hop, store)).

%% ---- OPERATIONS -------------------------------------------------------------

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

net_contract(rules('rules/net')).
net_contract(derived(net_page/1, 'a document fetched as a page')).
net_contract(derived(net_depends/2, 'a resource the page needs, directly or through another')).
net_contract(derived(net_cross_origin/2, 'a resource from another origin than the page that asked for it, neither opaque')).
net_contract(derived(net_failed/2, 'a resource whose fetch did not end well, and how it ended')).
net_contract(derived(net_redirects/2, 'the URLs a resource was redirected through, in order')).

net_contract(query(loaded, (net_resource(R, U, K, O, S, _, _)), [R, U, K, O, S], 'every resource, what it is and how its fetch ended')).

net_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
net_contract(test(integ, 'tests/integ/net')).
