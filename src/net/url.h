/* src/net/url.h -- URLs: the URL standard's parser and serializer (src/net/url.c),
   and URLs as the network module's loader keeps them (src/net/net.c).

   url_parse is the URL standard's basic URL parser (url.spec.whatwg.org, 4.4) and
   its serializer (4.5): the URL in out, NUL-terminated, and where each of its
   components lies in it in *p. url_attr reads a component as the URL API's getters
   do (6.1), url_origin the URL's origin (4.7), serialized.

   The loader's own uses: a reference resolved against a base, an origin, a site,
   the proxy a URL is fetched through, a date. Each writes a NUL-terminated string
   into out and answers 1, or 0 when the URL is not one. */
#ifndef AMB_URL_H
#define AMB_URL_H
#include <stddef.h>
#include <stdint.h>

/* no such component: a null query or fragment */
#define URL_NONE 0xFFFFFFFFu

/* what a URL's host is (3.1): none (null), the empty host, a domain, an IPv4 or IPv6
   address, an opaque host */
enum { URL_HOST_NULL, URL_HOST_EMPTY, URL_HOST_DOMAIN, URL_HOST_IPV4, URL_HOST_IPV6, URL_HOST_OPAQUE };

/* a parsed URL's components, as offsets into its serialization: each [start, end).
   A username or password that is empty has an empty span; the host's span is the
   host serialized (an IPv6 address in its brackets), empty for a null host; the
   path's span is the path serialized (past a "/." the serializer adds before a path
   that begins with an empty segment when the host is null), or the opaque path */
typedef struct url_parts {
    uint32_t len;                          /* the serialization's length */
    uint32_t scheme_end;                   /* the scheme is [0, scheme_end); its ':' follows */
    uint32_t user_start, user_end, pass_start, pass_end;
    uint32_t host_start, host_end;
    int32_t port;                          /* -1 for null */
    uint32_t path_start, path_end;
    uint32_t query_start, query_end;       /* past its '?'; query_start URL_NONE for a null query */
    uint32_t fragment_start;               /* past its '#', to the end; URL_NONE for a null fragment */
    uint8_t host_kind;                     /* URL_HOST_... */
    uint8_t special;                       /* 0, or the special scheme: 1 ftp, 2 file, 3 http, 4 https, 5 ws, 6 wss */
    uint8_t opaque;                        /* the path is opaque */
} url_parts;

/* the URL standard's basic URL parser: in (n bytes of UTF-8) against base (bn
   bytes, a URL the parser reads first; none when base is 0), serialized into out
   with its components' places in *p. 1, or 0 for failure: the input is no URL, the
   base is none, or the serialization does not fit in cap */
int url_parse(const char *in, size_t n, const char *base, size_t bn, char *out, size_t cap, url_parts *p);

/* the URL API's getters (6.1), by name: what each answers for the URL href, parsed
   into *p, written into out (NUL-terminated, cut at cap); its length */
enum { URL_HREF, URL_PROTOCOL, URL_USERNAME, URL_PASSWORD, URL_HOST, URL_HOSTNAME, URL_PORT, URL_PATHNAME, URL_SEARCH, URL_HASH,
       URL_ORIGIN };
size_t url_attr(const char *href, const url_parts *p, int which, char *out, size_t cap);

/* the URL standard's domain to Unicode (3.3), for showing a host: UTS #46's ToUnicode
   of the domain (n bytes), its xn-- labels decoded, into out (NUL-terminated, cut at
   cap); 1, or 0 when ToUnicode records an error, out then the domain as it is */
int url_domain_to_unicode(const char *domain, size_t n, char *out, size_t cap);

/* a URL as a person is shown it (4.8): serialized, its host, when a domain, through
   domain to Unicode; what is no URL (a file's path) as it is. Into out, NUL-terminated
   and cut at cap; its length */
size_t url_display(const char *in, size_t n, char *out, size_t cap);

/* ---- the loader's URLs (src/net/net.c) ---- */

/* ref resolved against base (an absolute URL), serialized as the loader keeps a URL:
   without its fragment, which no fetch sends, and without its credentials, which
   no record keeps */
int net_url_resolve(const char *base, const char *ref, char *out, size_t cap);

/* the origin of an absolute URL, serialized: scheme://host[:port], or null for an opaque one */
int net_url_origin(const char *url, char *out, size_t cap);

/* the proxy the environment names for the URL (https_proxy or HTTPS_PROXY for
   https:, http_proxy for http:, unless no_proxy or NO_PROXY names its host): 1 and
   the proxy's origin in out; 0 and out empty for none, or out `refused: WHY' for a
   proxy of a kind not spoken */
int net_url_proxy(const char *url, char *out, size_t cap);

/* a URL's site, its scheme and registrable domain, as cookies' same-site rules
   compare them: https://example.com; null for an opaque URL */
int net_url_site(const char *url, char *out, size_t cap);

/* a date as a cookie's Expires and HTTP's dates are read (RFC 6265bis 5.1.1): 1 and
   its seconds since the epoch in *t, or 0 when it is no date */
int net_url_date(const char *text, long long *t);

#endif
