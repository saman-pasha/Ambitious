/* src/net/net.c -- the network module (contracts/net.pl): URLs, fetches, what a
   page loads; and the module's generated implementation.

   URLs are the URL standard's: src/net/url.c parses and serializes them (its own
   header says what of the standard it does), and a reference resolves against
   its base as its parser resolves one. The loader keeps a URL serialized without
   its fragment, which no fetch sends, and without its credentials, which no record
   keeps; a URL of another scheme than the special ones has an opaque origin (data:,
   mailto:). A URL whose serialization passes 8 KB, or its host 255 bytes, is not
   fetched: it is a bad URL here.

   Fetching. file: reads the file its path names, percent-decoded (its type from
   its name); data: decodes its body, the URL past "data:" without its fragment
   (base64 or percent-encoded). http: and https: are HTTP/1.1, and HTTP/2 over TLS
   when the server's ALPN chooses it (src/net/h2.c, its header blocks by
   src/net/hpack.c): interim 1xx answers passed over, at most five redirects (from
   one protocol to the other as they come), 32 MB a body; a connect waited for four
   minutes, a proxy's tunnel and a TLS handshake 30 seconds each, an answer as long as
   it takes, as Chromium waits (AMB_NET_TIMEOUT, in ms, caps every wait). The
   request is Chromium's: its fields in its order and case (lowercase over HTTP/2,
   the Cookie in crumbs), client hints and fetch metadata to a potentially
   trustworthy URL alone (https:, a loopback host), the Referer of the default
   policy, the Accept of each kind (but the image formats the painter cannot draw),
   Accept-Encoding gzip, deflate, br and zstd to a potentially trustworthy URL,
   gzip and deflate to any other, and over HTTP/2 its priority (RFC 9218's field
   and the HEADERS' dependency tree). A response over HTTP/1.1 ends by its
   Content-Length, its last chunk and trailers, or the connection's close (a body
   shorter than its length, or chunks without their last, is malformed).

   Connections are kept, as Chromium's pool keeps them, for the process: an idle
   connection to the same origin (through the same proxy) carries the next request
   there, HTTP/1.1's kept alive, up to six to an origin at once; an HTTP/2
   connection is one per origin, its requests concurrent streams within the
   server's SETTINGS_MAX_CONCURRENT_STREAMS (100 until it says), and one made
   directly also carries another host that resolves to its address and port and that
   its certificate covers (Chromium's pooling by IP; a 421 asked again on the host's
   own connection). TLS sessions are kept and resumed as Chromium's session cache
   keeps them (below). A page's parts are
   fetched together: each made as the page orders them, its Cookie the jar's then,
   the exchanges carried at once over sockets that do not wait (poll), the answers
   taken in the page's order. A request on a reused connection that closes before
   a byte of its answer comes goes once again, on a new one; a stream refused
   (REFUSED_STREAM) goes again, and the streams a GOAWAY says were not processed
   go again on another connection, twice at most as Chromium's do (its
   kMaxRetryAttempts). Each address a name has is tried in turn. https:
   is TLS 1.2 or 1.3 through the system's OpenSSL, the one part a library gives:
   SNI (a host name's, never an address's), ALPN offering h2 and http/1.1 as
   Chromium does (the server's choice decides), the server's chain verified
   against SSL_CERT_FILE or else the system's store
   (/etc/ssl/certs/ca-certificates.crt), and its certificate against the URL's
   host, by name or by address. A verification that fails and a handshake that
   fails are outcomes of their own, each with the library's reason. The TLS
   context, which holds the store, is made at an operation's first https: URL. A
   proxy comes from the environment: https_proxy (or HTTPS_PROXY) for https:,
   through a CONNECT tunnel (HTTP/2 inside it as over any TLS); http_proxy for
   http:, its requests in absolute form (HTTP_PROXY is not read: a CGI program's
   request can set it); either may carry user info, sent as Basic
   Proxy-Authorization. no_proxy (or NO_PROXY) names the hosts reached directly:
   `*', a domain with its subdomains (a leading `.' or `*.' changes nothing), an
   address or a CIDR block for an address, each with an optional port. A proxy
   that will not open a tunnel is proxy_refused, with its status line. Every fetch
   that fails has its reason in words.

   What a page asks for: <link rel=stylesheet href>, <img src>, <script src>
   (fetched, never run), an SVG <image href> (or xlink:href), each URL fetched once,
   its query written in the page's encoding (net_set_encoding: the URL standard's
   encoding argument). An img with a srcset takes the candidate HTML's image source
   selection gives in the environment the shell sets (net_set_env: the profile's
   viewport, the device pixel ratio its zoom makes, its font size, colour scheme and
   motion; an 800x600 viewport at 1, 16px, light, by default): srcset parsed as HTML
   says, a width descriptor's density by the length of the first entry of sizes
   whose media condition holds (100vw when none; px, em, vw ... and calc() of them),
   src a 1x candidate when no candidate is 1x and none has a width, and of the
   densities sorted the first at the device pixel ratio or past it, else the
   greatest, as Blink picks (measured at ratios of 1, 2 and 2.5); the image's size is
   then its header's divided by its density, floored as naturalWidth is (layout
   takes it unrounded). An img in a <picture> first takes the
   first <source> before it, among the picture's children, whose srcset has a
   candidate, whose type (its MIME type's essence, in any case; empty for any) is
   one the painter draws (PNG, GIF, JPEG, WebP, SVG: not AVIF or BMP, which
   Chromium takes) and whose media query list holds (all and screen, not print;
   and, or, not, only; width, height, orientation and resolution, ranges, the
   colour scheme and reduced motion, a mouse's hover and fine pointer, a feature's
   name alone), by the source's own sizes; the source is a fact, and its width and
   height are the img's (the cascade's dimension attribute source). Else its own
   srcset and src, as above. A
   page served over http: or https: may not load a file: URL: blocked, here in
   C; nor any kind of resource its profile denies its site. An image's size is
   read from its header (PNG, GIF, JPEG); its pixels are not decoded here.

   A page's frames (Phase 12u): an iframe's src is fetched as a document by the shell
   (src/shell/frame.c, through net_fetch: its redirects followed, asked for by the iframe, of
   the page), once the page is laid out and its iframes' boxes are known (a lazy one's reach
   is the viewport's and 4000px), and each frame's own parts by a loader of its own; the
   page's frames, at any depth, are this store's frame records, which the shell fills.

   Cookies (RFC 6265bis) are kept in a jar, in the file net_keep names (a
   profile's) or for the run alone; the HTTP cache (RFC 9111) in the directory it
   names; gzip and deflate are decoded over the painter's inflate, br over its
   Brotli decoder (src/paint/brotli.c), zstd by src/net/zstd.c, dcb and dcz over a
   dictionary kept (Compression Dictionary Transport, RFC 9842). Each section
   below says what of it is done. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include <vector.pl>
#include <amb.pl>
#include "url.h"
#include "../paint/image.h"
#include "../paint/brotli.h"
#include "zstd.h"
#include "h2.h"
#include "dict.h"

amb_interface(dom);
amb_implementation(net);

/* glibc's struct addrinfo: <netdb.h> declares it only under a feature macro that
   cicilang's reading of the system headers does not set (toolchain/REPORT.md,
   finding 11), and gai_strerror with it. The layout is the LP64 one, as on x86-64
   and arm64 Linux. */
typedef struct net_addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct net_addrinfo *ai_next;
} net_addrinfo;
int getaddrinfo(const char *node, const char *service, const net_addrinfo *hints, net_addrinfo **res);
void freeaddrinfo(own net_addrinfo *res);
const char *gai_strerror(int code);

/* OpenSSL 3's libssl and libcrypto: the types opaque, and the calls the module
   makes declared here rather than read from <openssl/ssl.h>, whose reading adds
   a minute to every build of this unit (toolchain/REPORT.md). Each free takes an
   own pointer, so the checker sees every context and session consumed; a result
   that lives in its argument is tied to it. The constants are macros there,
   numbers here. */
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;
typedef struct ssl_cipher_st SSL_CIPHER;
typedef struct x509_verify_param_st X509_VERIFY_PARAM;
typedef struct x509_store_ctx_st X509_STORE_CTX;
const SSL_METHOD *TLS_client_method(void);
SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
void SSL_CTX_free(own SSL_CTX *ctx);
long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, char *parg);
int SSL_CTX_load_verify_locations(SSL_CTX *ctx, const char *file, const char *dir);
void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, int (*cb)(int, X509_STORE_CTX *));
int SSL_CTX_set_alpn_protos(SSL_CTX *ctx, const unsigned char *protos, unsigned int len);
SSL *SSL_new(SSL_CTX *ctx);
void SSL_free(own SSL *ssl);
int SSL_set_fd(SSL *ssl, int fd);
long SSL_ctrl(SSL *ssl, int cmd, long larg, char *parg);
int SSL_set1_host(SSL *ssl, const char *host);
X509_VERIFY_PARAM *SSL_get0_param(SSL *ssl) tie ssl;
int X509_VERIFY_PARAM_set1_ip_asc(X509_VERIFY_PARAM *param, const char *ip);
int SSL_connect(SSL *ssl);
int SSL_read(SSL *ssl, char *buf, int n);
int SSL_peek(SSL *ssl, char *buf, int n);
int SSL_write(SSL *ssl, const char *buf, int n);
int SSL_shutdown(SSL *ssl);
long SSL_get_verify_result(const SSL *ssl);
const char *X509_verify_cert_error_string(long n);
const char *SSL_get_version(const SSL *ssl) tie ssl;
const SSL_CIPHER *SSL_get_current_cipher(const SSL *ssl) tie ssl;
const char *SSL_CIPHER_get_name(const SSL_CIPHER *c) tie c;
void SSL_get0_alpn_selected(const SSL *ssl, const unsigned char **data, unsigned int *len);
unsigned long ERR_get_error(void);
void ERR_error_string_n(unsigned long e, char *buf, size_t len);
void ERR_clear_error(void);
int SSL_get_error(const SSL *ssl, int ret);
uint64_t SSL_CTX_set_options(SSL_CTX *ctx, uint64_t op);
/* the sessions a client keeps to resume (Chromium's SSLClientSessionCache), and the peer's
   certificate a connection pooled by IP is held to */
typedef struct ssl_session_st SSL_SESSION;
typedef struct x509_st X509;
void SSL_CTX_sess_set_new_cb(SSL_CTX *ctx, int (*cb)(SSL *, own SSL_SESSION *));
int SSL_set_session(SSL *ssl, SSL_SESSION *session);
void SSL_SESSION_free(own SSL_SESSION *session);
int SSL_session_reused(const SSL *s);
long SSL_SESSION_get_time(const SSL_SESSION *s);
long SSL_SESSION_get_timeout(const SSL_SESSION *s);
int SSL_SESSION_get_protocol_version(const SSL_SESSION *s);
X509 *SSL_get1_peer_certificate(const SSL *s);
int X509_check_host(X509 *x, const char *chk, size_t chklen, unsigned int flags, char **peername);
int X509_check_ip_asc(X509 *x, const char *address, unsigned int flags);
void X509_free(own X509 *a);
void SSL_set_hostflags(SSL *s, unsigned int flags);
void SSL_set_shutdown(SSL *ssl, int mode);
#define NET_SSL_SENT_SHUTDOWN 1
#define NET_SSL_CTRL_SET_SESS_CACHE_MODE 44
#define NET_SSL_SESS_CACHE_CLIENT 1
#define NET_SSL_SESS_CACHE_NO_INTERNAL_STORE 0x200
#define NET_X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS 4
#define NET_X509_CHECK_FLAG_NEVER_CHECK_SUBJECT 0x20
#define NET_TLS1_3_VERSION 0x0304
#define NET_SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define NET_SSL_CTRL_SET_MIN_PROTO_VERSION 123
#define NET_TLS1_2_VERSION 0x0303
#define NET_SSL_VERIFY_PEER 1
#define NET_SSL_CTRL_MODE 33
#define NET_SSL_MODE_PARTIAL_WRITE 1
#define NET_SSL_MODE_MOVING_BUFFER 2
#define NET_SSL_OP_IGNORE_UNEXPECTED_EOF ((uint64_t)1 << 7)
#define NET_SSL_ERROR_WANT_READ 2
#define NET_SSL_ERROR_WANT_WRITE 3
#define NET_SSL_ERROR_ZERO_RETURN 6

#define NET_MAX_BODY (32u << 20)
#define NET_MAX_REDIRECTS 5
/* the system's store of certificate authorities, when SSL_CERT_FILE names none */
#define NET_CA_FILE "/etc/ssl/certs/ca-certificates.crt"

/* ---- URLs, as src/net/url.c parses them ---- */

/* a URL as the loader reads it: its serialization and its components' places
   (url.h's parts), and the components it fetches by copied out. opaque is a URL of
   a scheme that is not special (data:, mailto:, about:), whose origin is opaque */
typedef struct net_url {
    char href[8192];
    url_parts p;
    char scheme[64];
    char host[256];           /* the host serialized: an IPv6 address in its brackets; empty for none */
    int port;                 /* -1: none, or the scheme's default */
    char path[8192];          /* the path serialized, or the opaque path */
    char query[8192];
    int has_query, special, opaque;
} net_url;

static char net_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int net_default_port(const char *s) {
    if (strcmp(s, "http") == 0 || strcmp(s, "ws") == 0) return 80;
    if (strcmp(s, "https") == 0 || strcmp(s, "wss") == 0) return 443;
    if (strcmp(s, "ftp") == 0) return 21;
    return -1;
}

/* the scheme's length, when the text begins with one and its colon (a proxy's setting may lack one) */
static size_t net_scheme_len(const char *s) {
    size_t i = 0;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
    while (s[i] && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
                    s[i] == '+' || s[i] == '-' || s[i] == '.')) i++;
    return s[i] == ':' ? i : 0;
}

/* a parsed URL's components copied into u: 0 when one does not fit */
static int net_url_fill(const char *href, const url_parts *p, net_url *u) {
    if (p->len >= sizeof u->href || p->scheme_end >= sizeof u->scheme || p->host_end - p->host_start >= sizeof u->host) return 0;
    memcpy(u->href, href, p->len);
    u->href[p->len] = 0;
    u->p = *p;
    memcpy(u->scheme, href, p->scheme_end);
    u->scheme[p->scheme_end] = 0;
    memcpy(u->host, href + p->host_start, p->host_end - p->host_start);
    u->host[p->host_end - p->host_start] = 0;
    u->port = p->port;
    memcpy(u->path, href + p->path_start, p->path_end - p->path_start);
    u->path[p->path_end - p->path_start] = 0;
    u->has_query = p->query_start != URL_NONE;
    u->query[0] = 0;
    if (u->has_query) {
        memcpy(u->query, href + p->query_start, p->query_end - p->query_start);
        u->query[p->query_end - p->query_start] = 0;
    }
    u->special = p->special != 0;
    u->opaque = !u->special;
    return 1;
}

/* an absolute URL parsed */
static int net_parse_abs(const char *in, net_url *u) {
    char out[8192];
    url_parts p;
    return url_parse(in, strlen(in), 0, 0, out, sizeof out, &p) && net_url_fill(out, &p, u);
}

/* a reference resolved against a base */
static int net_resolve(const net_url *base, const char *in, net_url *u) {
    char out[8192];
    url_parts p;
    return url_parse(in, strlen(in), base->href, base->p.len, out, sizeof out, &p) && net_url_fill(out, &p, u);
}

/* the encoding of the document whose URLs are parsed now (net_set_encoding), -1 for UTF-8 */
static int net_doc_enc = -1;
void net_set_encoding(int enc) { net_doc_enc = enc; }
int net_encoding(void) { return net_doc_enc; }

/* a reference resolved against a base, as a document's element names it: its query in the
   document's encoding (the URL standard's encoding argument) */
static int net_resolve_enc(const net_url *base, const char *in, int enc, net_url *u) {
    char out[8192];
    url_parts p;
    return url_parse_enc(in, strlen(in), base->href, base->p.len, enc, out, sizeof out, &p) && net_url_fill(out, &p, u);
}

/* a URL as the loader keeps it: serialized without its fragment, which no fetch
   sends, and without its credentials, which no record keeps */
static size_t net_url_text(const net_url *u, char *out, size_t cap) {
    uint32_t end = u->p.fragment_start != URL_NONE ? u->p.fragment_start - 1 : u->p.len;
    int n;
    if (u->p.host_kind != URL_HOST_NULL)
        n = snprintf(out, cap, "%.*s://%.*s", (int)u->p.scheme_end, u->href, (int)(end - u->p.host_start), u->href + u->p.host_start);
    else n = snprintf(out, cap, "%.*s", (int)end, u->href);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* its origin, serialized (4.7): null for an opaque one */
static size_t net_origin_text(const net_url *u, char *out, size_t cap) { return url_attr(u->href, &u->p, URL_ORIGIN, out, cap); }

int net_url_resolve(const char *base, const char *ref, char *out, size_t cap) {
    net_url b, u;
    if (!net_parse_abs(base, &b) || !net_resolve(&b, ref, &u)) return 0;
    net_url_text(&u, out, cap);
    return 1;
}

int net_url_resolve_enc(const char *base, const char *ref, int enc, char *out, size_t cap) {
    net_url b, u;
    if (!net_parse_abs(base, &b) || !net_resolve_enc(&b, ref, enc, &u)) return 0;
    net_url_text(&u, out, cap);
    return 1;
}

int net_url_origin(const char *url, char *out, size_t cap) {
    net_url u;
    if (!net_parse_abs(url, &u)) return 0;
    net_origin_text(&u, out, cap);
    return 1;
}

/* ---- addresses: IPv4 and IPv6 literals ---- */

static int net_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = net_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* a dotted quad, four decimal parts of at most 255, into v */
static int net_ipv4_dotted(const char *s, size_t n, uint32_t *v) {
    uint32_t a = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        size_t b = i;
        uint32_t x = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9' && i - b < 3) x = x * 10 + (uint32_t)(s[i++] - '0');
        if (i == b || x > 255) return 0;
        a = a << 8 | x;
        parts++;
        if (parts < 4) {
            if (i >= n || s[i] != '.') return 0;
            i++;
        }
    }
    if (i != n) return 0;
    *v = a;
    return 1;
}

/* WHATWG URL's IPv6 parser: eight pieces, one `::' standing for a run of zeros,
   the last two pieces perhaps a dotted quad */
static int net_ipv6_parse(const char *s, size_t n, uint16_t a[8]) {
    memset(a, 0, 8 * sizeof a[0]);
    int piece = 0, compress = -1;
    size_t p = 0;
    if (p < n && s[p] == ':') {
        if (p + 1 >= n || s[p + 1] != ':') return 0;
        p += 2;
        piece++;
        compress = piece;
    }
    while (p < n) {
        if (piece == 8) return 0;
        if (s[p] == ':') {
            if (compress >= 0) return 0;
            p++;
            piece++;
            compress = piece;
            continue;
        }
        uint32_t value = 0;
        int length = 0;
        while (length < 4 && p < n && net_hexval(s[p]) >= 0) { value = value * 16 + (uint32_t)net_hexval(s[p]); p++; length++; }
        if (p < n && s[p] == '.') {
            if (length == 0) return 0;
            p -= (size_t)length;
            if (piece > 6) return 0;
            int seen = 0;
            while (p < n) {
                int v4 = -1;
                if (seen > 0) {
                    if (s[p] == '.' && seen < 4) p++;
                    else return 0;
                }
                if (p >= n || s[p] < '0' || s[p] > '9') return 0;
                while (p < n && s[p] >= '0' && s[p] <= '9') {
                    int d = s[p] - '0';
                    if (v4 < 0) v4 = d;
                    else if (v4 == 0) return 0;
                    else v4 = v4 * 10 + d;
                    if (v4 > 255) return 0;
                    p++;
                }
                a[piece] = (uint16_t)(a[piece] * 0x100 + v4);
                seen++;
                if (seen == 2 || seen == 4) piece++;
            }
            if (seen != 4) return 0;
            break;
        }
        if (p < n && s[p] == ':') {
            p++;
            if (p >= n) return 0;
        } else if (p < n) return 0;
        a[piece] = (uint16_t)value;
        piece++;
    }
    if (compress >= 0) {
        int swaps = piece - compress;
        piece = 7;
        while (piece != 0 && swaps > 0) {
            uint16_t t = a[piece];
            a[piece] = a[compress + swaps - 1];
            a[compress + swaps - 1] = t;
            piece--;
            swaps--;
        }
    } else if (piece != 8) return 0;
    return 1;
}

/* a host as a socket names it: an IPv6 literal without its brackets */
static void net_bare_host(const char *host, char *out, size_t cap) {
    size_t n = strlen(host);
    if (n >= 2 && host[0] == '[' && host[n - 1] == ']') snprintf(out, cap, "%.*s", (int)(n - 2), host + 1);
    else snprintf(out, cap, "%s", host);
}

/* a bare host that is an address: a dotted quad, or IPv6 */
static int net_is_address(const char *h) {
    uint32_t v4;
    uint16_t v6[8];
    size_t n = strlen(h);
    return net_ipv4_dotted(h, n, &v4) || (strchr(h, ':') && net_ipv6_parse(h, n, v6));
}

/* ---- proxies ---- */

/* a proxy a request goes through: where it is, its credentials as Basic, its origin */
typedef struct net_proxy_t {
    char host[256];
    int port;
    char auth[700];           /* base64 of user:password, or empty */
    char origin[300];         /* http://host:port, no credentials */
} net_proxy_t;

static void net_base64_encode(const char *p, size_t n, char *out, size_t cap) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < n && k + 5 < cap; i += 3) {
        uint32_t v = (uint32_t)(uint8_t)p[i] << 16;
        if (i + 1 < n) v |= (uint32_t)(uint8_t)p[i + 1] << 8;
        if (i + 2 < n) v |= (uint8_t)p[i + 2];
        out[k++] = tab[v >> 18 & 63];
        out[k++] = tab[v >> 12 & 63];
        out[k++] = i + 1 < n ? tab[v >> 6 & 63] : '=';
        out[k++] = i + 2 < n ? tab[v & 63] : '=';
    }
    out[k] = 0;
}

/* an address block matched: an address of the same family whose first bits are the block's */
static int net_in_block(const char *host, const char *blk, size_t en) {
    size_t slash = en;
    for (size_t i = 0; i < en; i++) if (blk[i] == '/') slash = i;
    int bits = -1;
    if (slash < en) {
        bits = 0;
        if (slash + 1 >= en) return 0;
        for (size_t i = slash + 1; i < en; i++) {
            if (blk[i] < '0' || blk[i] > '9') return 0;
            bits = bits * 10 + (blk[i] - '0');
            if (bits > 128) return 0;
        }
    }
    uint32_t h4, e4;
    uint16_t h6[8], e6[8];
    size_t hn = strlen(host);
    if (net_ipv4_dotted(host, hn, &h4) && net_ipv4_dotted(blk, slash, &e4)) {
        if (bits < 0) bits = 32;
        if (bits > 32) return 0;
        uint32_t mask = bits == 0 ? 0 : 0xffffffffu << (32 - bits);
        return (h4 & mask) == (e4 & mask);
    }
    if (strchr(host, ':') && net_ipv6_parse(host, hn, h6) && net_ipv6_parse(blk, slash, e6)) {
        if (bits < 0) bits = 128;
        for (int i = 0; i < 8; i++) {
            int b = bits - 16 * i;
            if (b <= 0) break;
            uint16_t mask = b >= 16 ? 0xffff : (uint16_t)(0xffff << (16 - b));
            if ((h6[i] & mask) != (e6[i] & mask)) return 0;
        }
        return 1;
    }
    return 0;
}

/* one no_proxy entry against a bare, lowercase host and its port */
static int net_no_proxy_entry(const char *host, int port, const char *e, size_t en) {
    while (en && (e[0] == ' ' || e[0] == '\t')) { e++; en--; }
    while (en && (e[en - 1] == ' ' || e[en - 1] == '\t')) en--;
    if (en == 0) return 0;
    if (en == 1 && e[0] == '*') return 1;
    char t[300];
    if (en >= sizeof t) return 0;
    for (size_t i = 0; i < en; i++) t[i] = net_lower(e[i]);
    t[en] = 0;
    /* its port: [v6]:port, name:port or v4:port; an unbracketed v6 entry has none */
    int want = -1;
    size_t tn = en, colons = 0;
    for (size_t i = 0; i < tn; i++) if (t[i] == ':') colons++;
    if (t[0] == '[') {
        size_t close = 0;
        while (close < tn && t[close] != ']') close++;
        if (close >= tn) return 0;
        if (close + 1 < tn && t[close + 1] == ':') want = atoi(t + close + 2);
        memmove(t, t + 1, close - 1);
        t[close - 1] = 0;
        tn = close - 1;
    } else if (colons == 1) {
        size_t c = 0;
        while (t[c] != ':') c++;
        want = atoi(t + c + 1);
        t[c] = 0;
        tn = c;
    }
    if (want >= 0 && want != port) return 0;
    if (strchr(t, '/') || net_is_address(host)) return net_in_block(host, t, tn);
    /* a domain: itself and its subdomains, a final dot of either name aside */
    const char *d = t;
    if (d[0] == '*' && d[1] == '.') d += 2;
    else if (d[0] == '.') d++;
    size_t dn = strlen(d), hn = strlen(host);
    if (dn && d[dn - 1] == '.') dn--;
    if (hn && host[hn - 1] == '.') hn--;
    if (dn == 0) return 0;
    if (hn == dn) return strncmp(host, d, dn) == 0;
    return hn > dn && host[hn - dn - 1] == '.' && strncmp(host + hn - dn, d, dn) == 0;
}

/* whether no_proxy (or NO_PROXY) says to reach the host directly */
static int net_no_proxy(const char *host, int port) {
    const char *v = getenv("no_proxy");
    if (!v || !*v) v = getenv("NO_PROXY");
    if (!v || !*v) return 0;
    size_t n = strlen(v), a = 0;
    while (a <= n) {
        size_t b = a;
        while (b < n && v[b] != ',') b++;
        if (net_no_proxy_entry(host, port, v + a, b - a)) return 1;
        a = b + 1;
    }
    return 0;
}

/* the proxy a URL is fetched through: 1 and px filled; 0 for none; -1 for a proxy
   of a kind not spoken here, its words in why */
static int net_proxy_for(const net_url *u, net_proxy_t *px, char *why, size_t cap) {
    const char *v = 0;
    if (strcmp(u->scheme, "https") == 0) {
        v = getenv("https_proxy");
        if (!v || !*v) v = getenv("HTTPS_PROXY");
    } else if (strcmp(u->scheme, "http") == 0) v = getenv("http_proxy");
    if (!v || !*v) return 0;
    char host[256];
    net_bare_host(u->host, host, sizeof host);
    int port = u->port >= 0 ? u->port : net_default_port(u->scheme);
    if (net_no_proxy(host, port)) return 0;
    char text[1200];
    if (net_scheme_len(v)) snprintf(text, sizeof text, "%s", v);
    else snprintf(text, sizeof text, "http://%s", v);
    net_url pu;
    if (!net_parse_abs(text, &pu) || pu.opaque || strcmp(pu.scheme, "http") != 0) {
        snprintf(why, cap, "the proxy %s is not an http: proxy, the only kind spoken here", v);
        return -1;
    }
    memset(px, 0, sizeof *px);
    net_bare_host(pu.host, px->host, sizeof px->host);
    px->port = pu.port >= 0 ? pu.port : 80;
    snprintf(px->origin, sizeof px->origin, "http://%s:%d", pu.host, px->port);
    /* user info: what lies between the "//" and the last "@" of the authority */
    const char *a = strstr(text, "//");
    if (a) {
        a += 2;
        size_t e = 0, at = 0;
        while (a[e] && a[e] != '/' && a[e] != '?' && a[e] != '#') { if (a[e] == '@') at = e + 1; e++; }
        if (at) {
            vector<char> cred;
            for (size_t i = 0; i + 1 < at; i++) {
                if (a[i] == '%' && i + 2 < at - 1 && net_hexval(a[i + 1]) >= 0 && net_hexval(a[i + 2]) >= 0) {
                    push_back(cred, (char)(net_hexval(a[i + 1]) * 16 + net_hexval(a[i + 2])));
                    i += 2;
                } else push_back(cred, a[i]);
            }
            net_base64_encode(data(cred), len(cred), px->auth, sizeof px->auth);
        }
    }
    return 1;
}

int net_url_proxy(const char *url, char *out, size_t cap) {
    net_url u;
    net_proxy_t px;
    char why[300];
    out[0] = 0;
    if (!net_parse_abs(url, &u)) return 0;
    int via = net_proxy_for(&u, &px, why, sizeof why);
    if (via > 0) snprintf(out, cap, "%s", px.origin);
    else if (via < 0) snprintf(out, cap, "refused: %s", why);
    return via > 0;
}

/* ---- bodies: base64, percent-decoding ---- */

static void net_percent_decode(const char *s, size_t n, vector<char> *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int a = net_hexval(s[i + 1]), b = net_hexval(s[i + 2]);
            if (a >= 0 && b >= 0) { push_back(out, (char)(a * 16 + b)); i += 2; continue; }
        }
        push_back(out, s[i]);
    }
}
static int net_b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
static void net_base64_decode(const char *s, size_t n, vector<char> *out) {
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = net_b64(s[i]);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; push_back(out, (char)(acc >> bits & 0xff)); }
    }
}

/* a media type from a file's name */
static const char *net_type_of_name(const char *path) {
    size_t n = strlen(path), d = n;
    while (d > 0 && path[d - 1] != '.' && path[d - 1] != '/') d--;
    const char *ext = d > 0 && path[d - 1] == '.' ? path + d : "";
    char e[16];
    size_t k = 0;
    for (; ext[k] && k + 1 < sizeof e; k++) e[k] = net_lower(ext[k]);
    e[k] = 0;
    if (strcmp(e, "html") == 0 || strcmp(e, "htm") == 0) return "text/html";
    if (strcmp(e, "css") == 0) return "text/css";
    if (strcmp(e, "js") == 0 || strcmp(e, "mjs") == 0) return "text/javascript";
    if (strcmp(e, "png") == 0) return "image/png";
    if (strcmp(e, "gif") == 0) return "image/gif";
    if (strcmp(e, "jpg") == 0 || strcmp(e, "jpeg") == 0) return "image/jpeg";
    if (strcmp(e, "webp") == 0) return "image/webp";
    if (strcmp(e, "svg") == 0) return "image/svg+xml";
    if (strcmp(e, "bmp") == 0) return "image/bmp";
    if (strcmp(e, "ico") == 0) return "image/x-icon";
    if (strcmp(e, "txt") == 0) return "text/plain";
    return "application/octet-stream";
}

/* an image's size from its header (PNG, GIF, JPEG, WebP): the painter's reading, which
   turns a JPEG by its EXIF orientation, so that layout gives it the box it is drawn in; an
   SVG document's natural size from its root svg element (src/paint/svg.c), none when it
   has a ratio alone or nothing */
static void net_image_size_of(const uint8_t *b, size_t n, uint32_t *w, uint32_t *h) {
    *w = 0;
    *h = 0;
    if (amb_image_size(b, n, w, h)) return;
    *w = 0;
    *h = 0;
    double sw = 0, sh = 0, sr = 0;
    if (amb_svg_size(b, n, &sw, &sh, &sr) && sw > 0 && sh > 0) { *w = (uint32_t)(sw + 0.5); *h = (uint32_t)(sh + 0.5); }
}
static void net_image_size(const vector<char> *b, uint32_t *w, uint32_t *h) {
    if (len(*b) == 0) { *w = *h = 0; return; }
    net_image_size_of((const uint8_t *)data(*b), len(*b), w, h);
}

/* ---- file: and data: ---- */

static net_outcome net_get_file(const net_url *u, vector<char> *body, char *type, size_t tcap, char *why, size_t wcap) {
    vector<char> path;
    net_percent_decode(u->path, strlen(u->path), &path);
    push_back(path, 0);
    own FILE *f = fopen(data(path), "rb");
    if (!f) { snprintf(why, wcap, "%s", strerror(errno)); return NET_OUTCOME_NO_SUCH_FILE; }
    defer(f) { fclose(f); }
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) {
        append_range(body, buf, k);
        if (len(*body) > NET_MAX_BODY) { snprintf(why, wcap, "the file passed 32 MB"); return NET_OUTCOME_TOO_LARGE; }
    }
    if (ferror(f)) { snprintf(why, wcap, "the file could not be read"); return NET_OUTCOME_NO_SUCH_FILE; }
    snprintf(type, tcap, "%s", net_type_of_name(u->path));
    return NET_OUTCOME_OK;
}

/* a data: URL's body (Fetch's data: URL processor): the URL serialized without its
   fragment, past "data:" -- its path and its query */
static net_outcome net_get_data(const net_url *u, vector<char> *body, char *type, size_t tcap, char *why, size_t wcap) {
    uint32_t end = u->p.fragment_start != URL_NONE ? u->p.fragment_start - 1 : u->p.len;
    const char *s = u->href + u->p.scheme_end + 1;
    size_t n = end - u->p.scheme_end - 1, comma = 0;
    while (comma < n && s[comma] != ',') comma++;
    if (comma >= n) { snprintf(why, wcap, "a data: URL without its comma"); return NET_OUTCOME_MALFORMED; }
    int b64 = comma >= 7 && strncmp(s + comma - 7, ";base64", 7) == 0;
    size_t tn = b64 ? comma - 7 : comma;
    if (tn == 0) snprintf(type, tcap, "text/plain");
    else snprintf(type, tcap, "%.*s", (int)tn, s);
    vector<char> raw;
    net_percent_decode(s + comma + 1, n - comma - 1, &raw);
    if (b64) net_base64_decode(data(raw), len(raw), body);
    else append_range(body, data(raw), len(raw));
    return NET_OUTCOME_OK;
}

/* ---- TLS's context, and what an exchange went over ---- */

/* the TLS client every connection is made by, one for the process as Chromium's is for its
   network context: its context, made at the first https: URL, holds the store of certificate
   authorities and hands each new session (a TLS 1.3 ticket, a TLS 1.2 session) to the cache
   that resumes it (net_session_new, below); the pool frees it as the process ends */
typedef struct net_tls { own SSL_CTX *ctx; int tried, ok; char why[300]; } net_tls;
static net_tls net_tls_proc;
static int net_session_new(SSL *ssl, own SSL_SESSION *sess);
static void net_pool_kept_once(void);

static int net_tls_ready(net_tls *t) {
    if (t->tried) return t->ok;
    t->tried = 1;
    net_pool_kept_once();
    const char *env = getenv("SSL_CERT_FILE");
    char ca[1024];
    snprintf(ca, sizeof ca, "%s", env && *env ? env : NET_CA_FILE);
    if (!t->ctx) t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx) { snprintf(t->why, sizeof t->why, "no TLS context could be made"); return 0; }
    /* a client's session cache, kept here and not in the library's: each new session handed
       over (TLS 1.3's after each ticket, TLS 1.2's at its handshake's end) */
    SSL_CTX_ctrl(t->ctx, NET_SSL_CTRL_SET_SESS_CACHE_MODE, NET_SSL_SESS_CACHE_CLIENT | NET_SSL_SESS_CACHE_NO_INTERNAL_STORE, 0);
    SSL_CTX_sess_set_new_cb(t->ctx, net_session_new);
    SSL_CTX_ctrl(t->ctx, NET_SSL_CTRL_SET_MIN_PROTO_VERSION, NET_TLS1_2_VERSION, 0);
    if (SSL_CTX_load_verify_locations(t->ctx, ca, 0) != 1) {
        snprintf(t->why, sizeof t->why, "no certificate authority could be read from %s", ca);
        return 0;
    }
    SSL_CTX_set_verify(t->ctx, NET_SSL_VERIFY_PEER, 0);
    /* ALPN as Chromium offers it: h2, then http/1.1 */
    static const unsigned char alpn[12] = { 2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    SSL_CTX_set_alpn_protos(t->ctx, alpn, 12);
    /* a write the socket takes in part goes on from where it stopped; a server that closes
       without close_notify ends what it sent, as a browser takes it */
    SSL_CTX_ctrl(t->ctx, NET_SSL_CTRL_MODE, NET_SSL_MODE_PARTIAL_WRITE | NET_SSL_MODE_MOVING_BUFFER, 0);
    SSL_CTX_set_options(t->ctx, NET_SSL_OP_IGNORE_UNEXPECTED_EOF);
    t->ok = 1;
    return 1;
}

/* what an exchange went over, recorded on its resource; and why it failed */
typedef struct net_route {
    int tls, resumed;
    char version[32], cipher[96], alpn[32];
    int proxied;
    char proxy[300];
    char reason[600];
} net_route;

/* ---- HTTP/1.1 ---- */

/* where a head ends: just past its blank line, or 0 while it has not */
static size_t net_head_end(const char *p, size_t n, size_t from) {
    for (size_t i = from; i + 1 < n; i++) {
        if (p[i] != '\n') continue;
        if (p[i + 1] == '\n') return i + 2;
        if (i + 2 < n && p[i + 1] == '\r' && p[i + 2] == '\n') return i + 3;
    }
    return 0;
}

/* the status of the head at p, or -1 when it begins with no HTTP/1.x status line */
static int net_status_of(const char *p, size_t n) {
    if (n < 12 || memcmp(p, "HTTP/1.", 7) != 0 || p[8] != ' ') return -1;
    for (int i = 9; i < 12; i++) if (p[i] < '0' || p[i] > '9') return -1;
    return (p[9] - '0') * 100 + (p[10] - '0') * 10 + (p[11] - '0');
}

/* a header's value, its name matched without case, from the response head */
static int net_header(const char *head, size_t hn, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name), i = 0;
    while (i < hn) {
        size_t e = i;
        while (e < hn && head[e] != '\n') e++;
        if (e - i > nl && head[i + nl] == ':') {
            int same = 1;
            for (size_t k = 0; k < nl && same; k++) if (net_lower(head[i + k]) != name[k]) same = 0;
            if (same) {
                size_t a = i + nl + 1, b = e;
                while (a < b && (head[a] == ' ' || head[a] == '\t')) a++;
                while (b > a && (head[b - 1] == '\r' || head[b - 1] == ' ' || head[b - 1] == '\t')) b--;
                snprintf(out, cap, "%.*s", (int)(b - a), head + a);
                return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

/* a response's media type, from its Content-Type when it has one: lowercased, its
   parameters dropped but its charset's, which goes into charset (unquoted) for the
   document's encoding to be sniffed from (src/html/encoding.c) */
static void net_type_of_head(const char *head, size_t hn, char *type, size_t cap, char *charset, size_t ccap) {
    char ct[256];
    if (!net_header(head, hn, "content-type", ct, sizeof ct)) return;
    size_t k = 0;
    while (ct[k] && ct[k] != ';' && ct[k] != ' ' && k + 1 < cap) { type[k] = net_lower(ct[k]); k++; }
    type[k] = 0;
    /* the parameters: name=value pairs between semicolons, the first charset's taken */
    size_t i = k;
    while (ct[i]) {
        while (ct[i] && ct[i] != ';') i++;
        if (!ct[i]) break;
        i++;
        while (ct[i] == ' ' || ct[i] == '\t') i++;
        size_t ns = i;
        while (ct[i] && ct[i] != '=' && ct[i] != ';') i++;
        int is_cs = i - ns == 7 && net_lower(ct[ns]) == 'c' && net_lower(ct[ns + 1]) == 'h' && net_lower(ct[ns + 2]) == 'a' &&
                    net_lower(ct[ns + 3]) == 'r' && net_lower(ct[ns + 4]) == 's' && net_lower(ct[ns + 5]) == 'e' && net_lower(ct[ns + 6]) == 't';
        if (ct[i] != '=') continue;
        i++;
        size_t vs = i, ve;
        if (ct[i] == '"') { vs = ++i; while (ct[i] && ct[i] != '"') i++; ve = i; if (ct[i]) i++; }
        else { while (ct[i] && ct[i] != ';' && ct[i] != ' ') i++; ve = i; }
        if (is_cs && charset && !charset[0]) {
            size_t m = 0;
            for (size_t j = vs; j < ve && m + 1 < ccap; j++) charset[m++] = ct[j];
            charset[m] = 0;
        }
    }
}

/* where the final head begins: past every interim 1xx head (not 101). The
   head's end in *end; 0 while no final head has arrived */
static size_t net_final_head(const char *p, size_t n, size_t *end) {
    size_t a = 0;
    for (;;) {
        size_t e = net_head_end(p, n, a);
        if (!e) return 0;
        int st = net_status_of(p + a, e - a);
        if (st >= 100 && st < 200 && st != 101) { a = e; continue; }
        *end = e;
        return a + 1;
    }
}

/* a chunked body from p: -1 malformed, 0 cut short, 1 whole; its data into body
   when body is given */
static int net_chunks(const char *p, size_t n, vector<char> *body) {
    size_t b = 0;
    for (;;) {
        size_t e = b;
        uint64_t size = 0;
        int any = 0;
        while (e < n && net_hexval(p[e]) >= 0) {
            size = size * 16 + (uint64_t)net_hexval(p[e]);
            e++;
            any = 1;
            if (size > NET_MAX_BODY) return -1;
        }
        if (e >= n) return 0;
        if (!any) return -1;
        while (e < n && p[e] != '\n') e++;        /* chunk extensions */
        if (e >= n) return 0;
        e++;
        if (size == 0) return 1;                  /* trailers, if any, are not read */
        if (e + size > n) return 0;
        if (body) append_range(body, p + e, (size_t)size);
        b = e + (size_t)size;
        if (b < n && p[b] == '\r') b++;
        if (b < n && p[b] == '\n') b++;
        else if (b >= n) return 0;
    }
}

/* a response's bytes parsed: interim heads passed over; the status; the head (its
   status line and headers) into head; the body, framed, into body; the status
   line's reason phrase into why */
static net_outcome net_parse_response(const char *p, size_t n, int *status, vector<char> *head, vector<char> *body, char *why, size_t cap) {
    size_t end = 0, a = net_final_head(p, n, &end);
    if (!a) {
        snprintf(why, cap, n ? "the response's head did not end" : "the server sent nothing");
        return NET_OUTCOME_MALFORMED;
    }
    a--;
    int st = net_status_of(p + a, end - a);
    if (st < 0) { snprintf(why, cap, "the response has no HTTP/1.x status line"); return NET_OUTCOME_MALFORMED; }
    *status = st;
    append_range(head, p + a, end - a);
    size_t le = a + 12;
    while (le < end && p[le] != '\r' && p[le] != '\n') le++;
    size_t ra = a + 12;
    while (ra < le && p[ra] == ' ') ra++;
    snprintf(why, cap, "%d%s%.*s", st, ra < le ? " " : "", (int)(le - ra), p + ra);
    if (st == 204 || st == 304) return NET_OUTCOME_OK;
    char v[64];
    if (net_header(p + a, end - a, "transfer-encoding", v, sizeof v) && strstr(v, "chunked")) {
        int c = net_chunks(p + end, n - end, body);
        if (c < 0) { snprintf(why, cap, "a chunk's size is not a number"); return NET_OUTCOME_MALFORMED; }
        if (c == 0) { snprintf(why, cap, "the chunks ended before the last one"); return NET_OUTCOME_MALFORMED; }
        return NET_OUTCOME_OK;
    }
    size_t take = n - end;
    if (net_header(p + a, end - a, "content-length", v, sizeof v)) {
        long long cl = atoll(v);
        if (cl < 0) { snprintf(why, cap, "the Content-Length is not a length"); return NET_OUTCOME_MALFORMED; }
        if ((unsigned long long)cl > take) { snprintf(why, cap, "the body ended %llu bytes before its Content-Length", (unsigned long long)cl - take); return NET_OUTCOME_MALFORMED; }
        take = (size_t)cl;
    }
    append_range(body, p + end, take);
    return NET_OUTCOME_OK;
}

/* ---- the clock and dates ---- */

/* the session's clock: AMB_NOW (for tests), or the time; seconds since the epoch */
static int64_t net_now(void) {
    const char *n = getenv("AMB_NOW");
    return n && *n ? (int64_t)atoll(n) : (int64_t)time(0);
}

/* the days from 1970-01-01 to a date of the proleptic Gregorian calendar */
static int64_t net_days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int net_cookie_delim(unsigned char c) {
    return c == 0x09 || (c >= 0x20 && c <= 0x2f) || (c >= 0x3b && c <= 0x40) || (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e);
}

/* 1*maxd DIGIT, then nothing or a non-digit: the number, its digits in *k; -1 when not */
static int net_date_digits(const char *t, size_t n, int mind, int maxd, size_t *k) {
    size_t i = 0;
    int v = 0;
    while (i < n && t[i] >= '0' && t[i] <= '9') { if ((int)i >= maxd) return -1; v = v * 10 + (t[i] - '0'); i++; }
    if ((int)i < mind) return -1;
    *k = i;
    return v;
}

/* a date as RFC 6265bis (5.1.1) reads a cookie's, and as HTTP's dates are read:
   tokens between delimiters, the first time, day, month and year found, each once;
   two-digit years 70-99 the 1900s, 0-69 the 2000s */
static int net_parse_date(const char *s, size_t n, int64_t *out) {
    static const char *const months[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    int have_time = 0, have_day = 0, have_month = 0, have_year = 0;
    int hour = 0, minute = 0, second = 0, day = 0, month = 0, year = 0;
    size_t i = 0;
    while (i < n) {
        while (i < n && net_cookie_delim((unsigned char)s[i])) i++;
        size_t b = i;
        while (i < n && !net_cookie_delim((unsigned char)s[i])) i++;
        const char *t = s + b;
        size_t tn = i - b, k = 0;
        if (tn == 0) continue;
        if (!have_time) {
            int h = net_date_digits(t, tn, 1, 2, &k);
            if (h >= 0 && k < tn && t[k] == ':') {
                size_t k2 = 0, k3 = 0;
                int mi = net_date_digits(t + k + 1, tn - k - 1, 1, 2, &k2);
                if (mi >= 0 && k + 1 + k2 < tn && t[k + 1 + k2] == ':') {
                    int se = net_date_digits(t + k + 2 + k2, tn - k - 2 - k2, 1, 2, &k3);
                    if (se >= 0) { have_time = 1; hour = h; minute = mi; second = se; continue; }
                }
            }
        }
        if (!have_day) {
            int d = net_date_digits(t, tn, 1, 2, &k);
            if (d >= 0) { have_day = 1; day = d; continue; }
        }
        if (!have_month && tn >= 3) {
            int found = 0;
            for (int m = 0; m < 12 && !found; m++)
                if (net_lower(t[0]) == months[m][0] && net_lower(t[1]) == months[m][1] && net_lower(t[2]) == months[m][2]) { month = m + 1; found = 1; }
            if (found) { have_month = 1; continue; }
        }
        if (!have_year) {
            int y = net_date_digits(t, tn, 2, 4, &k);
            if (y >= 0) { have_year = 1; year = y; continue; }
        }
    }
    if (have_year && year >= 70 && year <= 99) year += 1900;
    else if (have_year && year >= 0 && year <= 69) year += 2000;
    if (!have_time || !have_day || !have_month || !have_year) return 0;
    if (day < 1 || day > 31 || year < 1601 || hour > 23 || minute > 59 || second > 59) return 0;
    static const int mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (day > mdays[month - 1] || (month == 2 && day == 29 && !leap)) return 0;
    *out = net_days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
    return 1;
}

int net_url_date(const char *text, long long *t) {
    int64_t v = 0;
    if (!net_parse_date(text, strlen(text), &v)) return 0;
    *t = (long long)v;
    return 1;
}

/* ---- sites: a scheme and a registrable domain ---- */

/* the public suffixes of two labels that the site of a name takes three labels
   under: the most used of the Public Suffix List, which is not bundled */
static const char *const net_suffixes2[] = {
    "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "net.uk", "ltd.uk", "plc.uk", "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp",
    "com.au", "net.au", "org.au", "edu.au", "gov.au", "co.nz", "net.nz", "org.nz", "com.br", "com.cn", "net.cn", "org.cn",
    "gov.cn", "co.in", "net.in", "org.in", "co.kr", "or.kr", "com.mx", "com.tr", "com.tw", "co.za", "com.sg", "com.hk",
    "github.io", "gitlab.io", "blogspot.com", "herokuapp.com", "appspot.com", "cloudfront.net", "azurewebsites.net",
    "pages.dev", "netlify.app", "vercel.app", "web.app", "firebaseapp.com",
};

/* a domain that is a public suffix: one label, or one of the list's two */
static int net_public_suffix(const char *d) {
    if (!strchr(d, '.')) return 1;
    for (size_t i = 0; i < sizeof net_suffixes2 / sizeof net_suffixes2[0]; i++) if (strcmp(d, net_suffixes2[i]) == 0) return 1;
    return 0;
}

/* a URL's site, the scheme and the registrable domain (an address, or one label,
   for itself), serialized: https://example.com; null for an opaque URL */
static void net_site_of(const net_url *u, char *out, size_t cap) {
    if (u->opaque || strcmp(u->scheme, "file") == 0) { snprintf(out, cap, "null"); return; }
    char host[256];
    net_bare_host(u->host, host, sizeof host);
    size_t hn = strlen(host);
    if (hn && host[hn - 1] == '.') host[--hn] = 0;
    if (net_is_address(host)) { snprintf(out, cap, "%s://%s", u->scheme, u->host); return; }
    /* the last two labels, or three under a two-label suffix */
    size_t dots[3] = { 0, 0, 0 };
    int nd = 0;
    for (size_t i = hn; i > 0 && nd < 3; i--) if (host[i - 1] == '.') dots[nd++] = i - 1;
    size_t from = 0;
    if (nd >= 2) {
        from = dots[1] + 1;
        if (net_public_suffix(host + from)) from = nd >= 3 ? dots[2] + 1 : 0;
    }
    snprintf(out, cap, "%s://%s", u->scheme, host + from);
}

/* whether a request for u, from a page at pu, is same-site (5.2): their sites the same */
static int net_is_same_site(const net_url *u, const net_url *pu) {
    char a[300], b[300];
    net_site_of(u, a, sizeof a);
    net_site_of(pu, b, sizeof b);
    return strcmp(a, "null") != 0 && strcmp(a, b) == 0;
}

int net_url_site(const char *url, char *out, size_t cap) {
    net_url u;
    if (!net_parse_abs(url, &u)) return 0;
    net_site_of(&u, out, cap);
    return 1;
}

/* ---- cookies (RFC 6265bis) ---- */

#define NET_COOKIE_MAX 3000
#define NET_COOKIE_AGE_LIMIT (400 * 86400)

/* 5.1.3: a host in a domain: the same, or a name ending in "." and the domain */
static int net_domain_match(const char *host, const char *dom) {
    size_t hn = strlen(host), dn = strlen(dom);
    if (hn == dn) return strcmp(host, dom) == 0;
    if (dn == 0 || hn < dn + 1 || host[hn - dn - 1] != '.' || strcmp(host + hn - dn, dom) != 0) return 0;
    char bare[256];
    net_bare_host(host, bare, sizeof bare);
    return !net_is_address(bare);
}

/* 5.1.4: a request's path in a cookie's path */
static int net_path_match(const char *req, const char *cp, size_t cn) {
    size_t rn = strlen(req);
    if (rn < cn || strncmp(req, cp, cn) != 0) return 0;
    return rn == cn || (cn > 0 && cp[cn - 1] == '/') || req[cn] == '/';
}

/* 5.1.4: the default path, the request's path to its last "/" */
static void net_default_path(const char *path, char *out, size_t cap) {
    size_t n = strlen(path), last = 0;
    for (size_t i = 0; i < n; i++) if (path[i] == '/') last = i;
    if (n == 0 || path[0] != '/' || last == 0) snprintf(out, cap, "/");
    else snprintf(out, cap, "%.*s", (int)last, path);
}

static int net_ci_prefix(const char *s, size_t n, const char *p) {
    size_t pn = strlen(p);
    if (n < pn) return 0;
    for (size_t i = 0; i < pn; i++) if (net_lower(s[i]) != net_lower(p[i])) return 0;
    return 1;
}
static int net_ci_is(const char *s, size_t n, const char *w) { return strlen(w) == n && net_ci_prefix(s, n, w); }

static int net_wsp(char c) { return c == ' ' || c == '\t'; }

/* the cookie's text field as a C string */
static void net_span_text(const net_loader *l, amb_span sp, char *out, size_t cap) {
    if (sp.len == 0) { out[0] = 0; return; }
    snprintf(out, cap, "%.*s", (int)sp.len, text_ptr(l, sp));
}

/* the jar's live cookies, past their expiry, expired */
static void net_cookies_expire(net_loader *l, int64_t now) {
    for (uint32_t c = 1; c < l->cookie_len; c++) {
        if (!l->cookie[c].live || l->cookie[c].state != NET_COOKIE_STATE_KEPT || !l->cookie[c].persistent) continue;
        if (l->cookie[c].expires <= now) {
            l->cookie[c].state = NET_COOKIE_STATE_EXPIRED;
            if (l->keeping_len > 1) l->keeping[1].dirty = 1;
        }
    }
}

/* a Set-Cookie value from the response for u, stored in the jar or refused, as the
   storage model says (5.6, 5.7). The request was same-site with its page, or a
   top-level navigation, when same is 1; r the resource whose response it is */
static void net_cookie_set(net_loader *l, const net_url *u, const char *s, size_t n, int same, uint32_t r) {
    int64_t now = net_now();
    char host[256];
    snprintf(host, sizeof host, "%s", u->host);
    int secure = strcmp(u->scheme, "https") == 0 || strcmp(u->scheme, "wss") == 0;
    net_refusal why = NET_REFUSAL_ACCEPTED;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) why = NET_REFUSAL_MALFORMED;
    }
    /* the name and value */
    size_t semi = 0;
    while (semi < n && s[semi] != ';') semi++;
    size_t eq = 0;
    while (eq < semi && s[eq] != '=') eq++;
    size_t na = 0, nb = 0, va = 0, vb = semi;
    if (eq < semi) { nb = eq; va = eq + 1; }
    while (na < nb && net_wsp(s[na])) na++;
    while (nb > na && net_wsp(s[nb - 1])) nb--;
    while (va < vb && net_wsp(s[va])) va++;
    while (vb > va && net_wsp(s[vb - 1])) vb--;
    if (why == NET_REFUSAL_ACCEPTED && nb == na && vb == va) why = NET_REFUSAL_MALFORMED;
    if (why == NET_REFUSAL_ACCEPTED && (nb - na) + (vb - va) > 4096) why = NET_REFUSAL_TOO_LARGE;
    /* the attributes, each the last of its name */
    int has_max_age = 0, has_expires = 0, has_domain = 0, has_path = 0, csecure = 0, http_only = 0;
    int64_t max_age = 0, expires = 0;
    net_same_site ss = NET_SAME_SITE_UNSET;
    char domain[256], path[1100];
    domain[0] = 0;
    net_default_path(u->path, path, sizeof path);
    size_t i = semi;
    while (i < n) {
        i++;
        size_t e = i;
        while (e < n && s[e] != ';') e++;
        size_t q = i;
        while (q < e && s[q] != '=') q++;
        size_t an = i, ab = q, aa = q < e ? q + 1 : e, ae = e;
        while (an < ab && net_wsp(s[an])) an++;
        while (ab > an && net_wsp(s[ab - 1])) ab--;
        while (aa < ae && net_wsp(s[aa])) aa++;
        while (ae > aa && net_wsp(s[ae - 1])) ae--;
        const char *nm = s + an, *v = s + aa;
        size_t nl = ab - an, vl = ae - aa;
        if (vl <= 1024) {
            int64_t t = 0;
            if (net_ci_is(nm, nl, "expires")) {
                if (net_parse_date(v, vl, &t)) { has_expires = 1; expires = t; }
            } else if (net_ci_is(nm, nl, "max-age")) {
                int ok = vl > 0 && ((v[0] >= '0' && v[0] <= '9') || v[0] == '-');
                for (size_t k = 1; k < vl && ok; k++) if (v[k] < '0' || v[k] > '9') ok = 0;
                if (ok && vl == 1 && v[0] == '-') ok = 0;
                if (ok) {
                    int64_t d = 0;
                    for (size_t k = v[0] == '-' ? 1 : 0; k < vl; k++) { d = d * 10 + (v[k] - '0'); if (d > (int64_t)1 << 40) d = (int64_t)1 << 40; }
                    if (v[0] == '-') d = -d;
                    has_max_age = 1;
                    max_age = d <= 0 ? 0 : now + d;
                }
            } else if (net_ci_is(nm, nl, "domain")) {
                if (vl > 0) {
                    size_t k = v[0] == '.' ? 1 : 0, o = 0;
                    for (; k < vl && o + 1 < sizeof domain; k++) domain[o++] = net_lower(v[k]);
                    domain[o] = 0;
                    has_domain = 1;
                }
            } else if (net_ci_is(nm, nl, "path")) {
                has_path = 1;
                if (vl > 0 && v[0] == '/') snprintf(path, sizeof path, "%.*s", (int)vl, v);
                else net_default_path(u->path, path, sizeof path);
            } else if (net_ci_is(nm, nl, "secure")) csecure = 1;
            else if (net_ci_is(nm, nl, "httponly")) http_only = 1;
            else if (net_ci_is(nm, nl, "samesite")) {
                if (net_ci_is(v, vl, "strict")) ss = NET_SAME_SITE_STRICT;
                else if (net_ci_is(v, vl, "lax")) ss = NET_SAME_SITE_LAX;
                else if (net_ci_is(v, vl, "none")) ss = NET_SAME_SITE_NO_RESTRICTION;
                else ss = NET_SAME_SITE_UNSET;
            }
        }
        i = e;
    }
    int persistent = has_max_age || has_expires;
    int64_t exp = has_max_age ? max_age : expires;
    if (persistent && exp > now + NET_COOKIE_AGE_LIMIT) exp = now + NET_COOKIE_AGE_LIMIT;
    /* the domain: a public suffix only as the host itself; else the host must be in it */
    int host_only = 1;
    char dom[256];
    snprintf(dom, sizeof dom, "%s", host);
    if (has_domain && why == NET_REFUSAL_ACCEPTED) {
        for (size_t k = 0; domain[k]; k++) if ((unsigned char)domain[k] >= 0x80) why = NET_REFUSAL_MALFORMED;
        if (why != NET_REFUSAL_ACCEPTED) {}
        else if (net_public_suffix(domain) && strcmp(domain, host) != 0) why = NET_REFUSAL_PUBLIC_SUFFIX;
        else if (net_public_suffix(domain)) {}
        else if (!net_domain_match(host, domain)) why = NET_REFUSAL_DOMAIN_MISMATCH;
        else { host_only = 0; snprintf(dom, sizeof dom, "%s", domain); }
    }
    if (why == NET_REFUSAL_ACCEPTED && csecure && !secure) why = NET_REFUSAL_SECURE_OVER_HTTP;
    if (why == NET_REFUSAL_ACCEPTED && !csecure && !secure) {
        /* leave Secure cookies alone: none of their name over http: in their domain and path */
        for (uint32_t c = 1; c < l->cookie_len && why == NET_REFUSAL_ACCEPTED; c++) {
            if (!l->cookie[c].live || l->cookie[c].state != NET_COOKIE_STATE_KEPT || !l->cookie[c].secure) continue;
            if (!text_eq(l, l->cookie[c].name, s + na, nb - na)) continue;
            char cd[256], cp[1100];
            net_span_text(l, l->cookie[c].domain, cd, sizeof cd);
            net_span_text(l, l->cookie[c].path, cp, sizeof cp);
            if ((net_domain_match(cd, dom) || net_domain_match(dom, cd)) && net_path_match(path, cp, strlen(cp))) why = NET_REFUSAL_OVERWRITES_SECURE;
        }
    }
    if (why == NET_REFUSAL_ACCEPTED && ss != NET_SAME_SITE_NO_RESTRICTION && !same) why = NET_REFUSAL_CROSS_SITE;
    if (why == NET_REFUSAL_ACCEPTED && ss == NET_SAME_SITE_NO_RESTRICTION && !csecure) why = NET_REFUSAL_NONE_INSECURE;
    if (why == NET_REFUSAL_ACCEPTED && net_ci_prefix(s + na, nb - na, "__Secure-") && !csecure) why = NET_REFUSAL_PREFIX;
    if (why == NET_REFUSAL_ACCEPTED && net_ci_prefix(s + na, nb - na, "__Host-") && (!csecure || !host_only || !has_path || strcmp(path, "/") != 0))
        why = NET_REFUSAL_PREFIX;
    if (why == NET_REFUSAL_ACCEPTED && nb == na && (net_ci_prefix(s + va, vb - va, "__Secure-") || net_ci_prefix(s + va, vb - va, "__Host-")))
        why = NET_REFUSAL_PREFIX;
    if (why == NET_REFUSAL_ACCEPTED) {
        size_t kept = 0;
        for (uint32_t c = 1; c < l->cookie_len; c++) if (l->cookie[c].live && l->cookie[c].state == NET_COOKIE_STATE_KEPT) kept++;
        if (kept >= NET_COOKIE_MAX) why = NET_REFUSAL_TOO_MANY;
    }
    /* the record, kept or refused */
    int64_t created = now;
    if (why == NET_REFUSAL_ACCEPTED)
        for (uint32_t c = 1; c < l->cookie_len; c++) {
            if (!l->cookie[c].live || l->cookie[c].state != NET_COOKIE_STATE_KEPT || l->cookie[c].host_only != host_only) continue;
            if (!text_eq(l, l->cookie[c].name, s + na, nb - na) || !text_eq(l, l->cookie[c].domain, dom, strlen(dom)) ||
                !text_eq(l, l->cookie[c].path, path, strlen(path))) continue;
            created = l->cookie[c].created;
            l->cookie[c].state = NET_COOKIE_STATE_REPLACED;
            l->cookie[c].seen = 1;
        }
    char name[4200], value[4200];
    snprintf(name, sizeof name, "%.*s", (int)(nb - na), s + na);
    snprintf(value, sizeof value, "%.*s", (int)(vb - va), s + va);
    net_cookie_h h = cookie_new(l);
    uint32_t c = h.index;
    l->cookie[c].name = text_add(l, name, strlen(name));
    l->cookie[c].value = text_add(l, value, strlen(value));
    l->cookie[c].domain = text_add(l, dom, strlen(dom));
    l->cookie[c].path = text_add(l, path, strlen(path));
    l->cookie[c].host_only = (uint8_t)host_only;
    l->cookie[c].secure = (uint8_t)csecure;
    l->cookie[c].http_only = (uint8_t)http_only;
    l->cookie[c].same_site = ss;
    l->cookie[c].persistent = (uint8_t)persistent;
    l->cookie[c].expires = persistent ? exp : 0;
    l->cookie[c].created = created;
    l->cookie[c].refusal = why;
    l->cookie[c].set_by = r;
    l->cookie[c].seen = 1;
    if (why != NET_REFUSAL_ACCEPTED) l->cookie[c].state = NET_COOKIE_STATE_REFUSED;
    else if (persistent && exp <= now) l->cookie[c].state = NET_COOKIE_STATE_EXPIRED;
    else l->cookie[c].state = NET_COOKIE_STATE_KEPT;
    if (why == NET_REFUSAL_ACCEPTED && l->keeping_len > 1) l->keeping[1].dirty = 1;
}

/* each Set-Cookie line of a response's head, stored or refused */
static void net_cookies_from(net_loader *l, const net_url *u, const char *head, size_t hn, int same, uint32_t r) {
    size_t i = 0;
    while (i < hn) {
        size_t e = i;
        while (e < hn && head[e] != '\n') e++;
        if (e - i > 11 && net_ci_prefix(head + i, e - i, "set-cookie:")) {
            size_t a = i + 11, b = e;
            while (a < b && net_wsp(head[a])) a++;
            while (b > a && (head[b - 1] == '\r' || net_wsp(head[b - 1]))) b--;
            net_cookie_set(l, u, head + a, b - a, same, r);
        }
        i = e + 1;
    }
}

/* the Cookie header for a request for u (5.8.3): the jar's cookies for its host and
   path, Secure ones over TLS alone, SameSite ones on a same-site request (a
   top-level navigation is one); longest paths first, then the oldest; each sent
   recorded for resource r. Its header line into out, when there is a cookie */
static void net_cookie_header(net_loader *l, const net_url *u, int same, uint32_t r, vector<char> *out) {
    int64_t now = net_now();
    net_cookies_expire(l, now);
    char host[256];
    snprintf(host, sizeof host, "%s", u->host);
    int secure = strcmp(u->scheme, "https") == 0 || strcmp(u->scheme, "wss") == 0;
    vector<uint32_t> pick;
    for (uint32_t c = 1; c < l->cookie_len; c++) {
        if (!l->cookie[c].live || l->cookie[c].state != NET_COOKIE_STATE_KEPT) continue;
        char cd[256], cp[1100];
        net_span_text(l, l->cookie[c].domain, cd, sizeof cd);
        net_span_text(l, l->cookie[c].path, cp, sizeof cp);
        if (l->cookie[c].host_only ? strcmp(host, cd) != 0 : !net_domain_match(host, cd)) continue;
        if (!net_path_match(u->path, cp, strlen(cp))) continue;
        if (l->cookie[c].secure && !secure) continue;
        if (l->cookie[c].same_site != NET_SAME_SITE_NO_RESTRICTION && !same) continue;
        push_back(pick, c);
    }
    if (len(pick) == 0) return;
    /* longest path first, then the oldest, then the first made (an insertion sort: a request's few) */
    for (size_t a = 1; a < len(pick); a++) {
        uint32_t x = pick[a];
        size_t b = a;
        while (b > 0) {
            uint32_t y = pick[b - 1];
            uint32_t px = l->cookie[x].path.len, py = l->cookie[y].path.len;
            int before = px > py || (px == py && (l->cookie[x].created < l->cookie[y].created || (l->cookie[x].created == l->cookie[y].created && x < y)));
            if (!before) break;
            pick[b] = y;
            b--;
        }
        pick[b] = x;
    }
    append_range(out, "Cookie: ", 8);
    for (size_t k = 0; k < len(pick); k++) {
        uint32_t c = pick[k];
        if (k) append_range(out, "; ", 2);
        amb_span nm = l->cookie[c].name, v = l->cookie[c].value;
        if (nm.len) { append_range(out, text_ptr(l, nm), nm.len); push_back(out, '='); }
        if (v.len) append_range(out, text_ptr(l, v), v.len);
        l->cookie[c].seen = 1;
        int had = 0;
        for (uint32_t q = 1; q < l->sent_len && !had; q++) if (l->sent[q].live && l->sent[q].resource == r && l->sent[q].cookie == c) had = 1;
        if (!had) {
            net_sent_h sh = sent_new(l);
            l->sent[sh.index].resource = r;
            l->sent[sh.index].cookie = c;
        }
    }
    append_range(out, "\r\n", 2);
}

/* ---- the jar's file: a cookie a line, its fields between tabs ---- */

/* a field written with %, tab, CR and LF as %XX */
static void net_jar_put(FILE *f, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '%' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r') fprintf(f, "%%%02X", (unsigned)(unsigned char)p[i]);
        else fputc(p[i], f);
    }
}

/* the jar's persistent cookies, written to its file through a new file renamed over it */
static void net_jar_write(net_loader *l) {
    if (l->keeping_len < 2 || l->keeping[1].jar.len == 0 || !l->keeping[1].dirty) return;
    char path[1100], tmp[1200], dir[1100];
    net_span_text(l, l->keeping[1].jar, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = 0; mkdir(dir, 0700); }
    int64_t now = net_now();
    {
        /* readable by its owner alone: a cookie's value may be a secret */
        mode_t mask = umask(077);
        own FILE *f = fopen(tmp, "w");
        umask(mask);
        if (!f) return;
        defer(f) { fclose(f); }
        fprintf(f, "# Ambitious's cookie jar: a cookie a line -- domain, host-only, path, secure, http-only, same-site, expires, created, name, value\n");
        for (uint32_t c = 1; c < l->cookie_len; c++) {
            if (!l->cookie[c].live || l->cookie[c].state != NET_COOKIE_STATE_KEPT || !l->cookie[c].persistent || l->cookie[c].expires <= now) continue;
            amb_span d = l->cookie[c].domain, p = l->cookie[c].path, nm = l->cookie[c].name, v = l->cookie[c].value;
            if (d.len) net_jar_put(f, text_ptr(l, d), d.len);
            fprintf(f, "\t%d\t", l->cookie[c].host_only);
            if (p.len) net_jar_put(f, text_ptr(l, p), p.len);
            fprintf(f, "\t%d\t%d\t%s\t%lld\t%lld\t", l->cookie[c].secure, l->cookie[c].http_only, net_same_site_name(l->cookie[c].same_site),
                    (long long)l->cookie[c].expires, (long long)l->cookie[c].created);
            if (nm.len) net_jar_put(f, text_ptr(l, nm), nm.len);
            fputc('\t', f);
            if (v.len) net_jar_put(f, text_ptr(l, v), v.len);
            fputc('\n', f);
        }
    }
    if (rename(tmp, path) == 0) l->keeping[1].dirty = 0;
}

/* a field of a line read, its %XX undone */
static void net_jar_field(const char *p, size_t n, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        if (p[i] == '%' && i + 2 < n && net_hexval(p[i + 1]) >= 0 && net_hexval(p[i + 2]) >= 0) {
            out[k++] = (char)(net_hexval(p[i + 1]) * 16 + net_hexval(p[i + 2]));
            i += 2;
        } else out[k++] = p[i];
    }
    out[k] = 0;
}

/* the jar's file read: each cookie not yet expired, kept, unseen until sent */
static void net_jar_read(net_loader *l, const char *path) {
    vector<char> text;
    {
        own FILE *f = fopen(path, "rb");
        if (!f) return;
        defer(f) { fclose(f); }
        char buf[8192];
        size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) append_range(text, buf, k);
    }
    int64_t now = net_now();
    size_t i = 0, n = len(text);
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        size_t f[11], nf = 0;
        f[nf++] = i;
        for (size_t k = i; k < e && nf < 11; k++) if (text[k] == '\t') f[nf++] = k + 1;
        if (e > i && text[i] != '#' && nf == 10) {
            char dom[256], path[1100], ss[32], exps[32], cr[32], name[4200], value[4200], ho[8], sec[8], ho2[8];
            const char *t = data(text);
            net_jar_field(t + f[0], f[1] - 1 - f[0], dom, sizeof dom);
            net_jar_field(t + f[1], f[2] - 1 - f[1], ho, sizeof ho);
            net_jar_field(t + f[2], f[3] - 1 - f[2], path, sizeof path);
            net_jar_field(t + f[3], f[4] - 1 - f[3], sec, sizeof sec);
            net_jar_field(t + f[4], f[5] - 1 - f[4], ho2, sizeof ho2);
            net_jar_field(t + f[5], f[6] - 1 - f[5], ss, sizeof ss);
            net_jar_field(t + f[6], f[7] - 1 - f[6], exps, sizeof exps);
            net_jar_field(t + f[7], f[8] - 1 - f[7], cr, sizeof cr);
            net_jar_field(t + f[8], f[9] - 1 - f[8], name, sizeof name);
            net_jar_field(t + f[9], e - f[9], value, sizeof value);
            int64_t expires = atoll(exps);
            if (expires > now && dom[0] && path[0] == '/') {
                net_same_site s = NET_SAME_SITE_UNSET;
                if (strcmp(ss, "lax") == 0) s = NET_SAME_SITE_LAX;
                else if (strcmp(ss, "strict") == 0) s = NET_SAME_SITE_STRICT;
                else if (strcmp(ss, "no_restriction") == 0) s = NET_SAME_SITE_NO_RESTRICTION;
                net_cookie_h h = cookie_new(l);
                uint32_t c = h.index;
                l->cookie[c].name = text_add(l, name, strlen(name));
                l->cookie[c].value = text_add(l, value, strlen(value));
                l->cookie[c].domain = text_add(l, dom, strlen(dom));
                l->cookie[c].path = text_add(l, path, strlen(path));
                l->cookie[c].host_only = (uint8_t)(ho[0] == '1');
                l->cookie[c].secure = (uint8_t)(sec[0] == '1');
                l->cookie[c].http_only = (uint8_t)(ho2[0] == '1');
                l->cookie[c].same_site = s;
                l->cookie[c].persistent = 1;
                l->cookie[c].expires = expires;
                l->cookie[c].created = atoll(cr);
                l->cookie[c].state = NET_COOKIE_STATE_KEPT;
                l->cookie[c].refusal = NET_REFUSAL_ACCEPTED;
            }
        }
        i = e + 1;
    }
}

net_status net_keep(net_loader *l, const char *jar, size_t jar_len, const char *cache, size_t cache_len) {
    if (l->keeping_len < 2) keeping_new(l);
    l->keeping[1].jar = text_add(l, jar, jar_len);
    l->keeping[1].cache = text_add(l, cache, cache_len);
    l->keeping[1].dirty = 0;
    if (jar_len) {
        char path[1100];
        snprintf(path, sizeof path, "%.*s", (int)jar_len, jar);
        net_jar_read(l, path);
    }
    l->revision++;
    return NET_OK;
}

/* ---- the HTTP cache (RFC 9111): an entry a file in the cache's directory ---- */

/* the fields an entry keeps of its response's head, and that a 304 updates */
static const char *const net_cache_fields[] = { "content-type", "cache-control", "expires", "date", "last-modified", "etag", "age" };

/* every value a head has for a field, joined by ", " as a list's are */
static int net_header_all(const char *head, size_t hn, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name), i = 0, k = 0;
    int any = 0;
    out[0] = 0;
    while (i < hn) {
        size_t e = i;
        while (e < hn && head[e] != '\n') e++;
        if (e - i > nl && head[i + nl] == ':' && net_ci_prefix(head + i, e - i, name)) {
            size_t a = i + nl + 1, b = e;
            while (a < b && net_wsp(head[a])) a++;
            while (b > a && (head[b - 1] == '\r' || net_wsp(head[b - 1]))) b--;
            int n = snprintf(out + k, cap - k, "%s%.*s", any ? ", " : "", (int)(b - a), head + a);
            if (n > 0 && k + (size_t)n < cap) k += (size_t)n;
            any = 1;
        }
        i = e + 1;
    }
    return any;
}

/* a Cache-Control directive of the head: 1 when it is there, its value (a number,
   bare or quoted) in *v, or -1 for none */
static int net_cache_directive(const char *head, size_t hn, const char *dir, int64_t *v) {
    char cc[1024];
    *v = -1;
    if (!net_header_all(head, hn, "cache-control", cc, sizeof cc)) return 0;
    size_t n = strlen(cc), a = 0, dn = strlen(dir);
    while (a < n) {
        size_t b = a;
        while (b < n && cc[b] != ',') b++;
        size_t x = a, y = b;
        while (x < y && net_wsp(cc[x])) x++;
        while (y > x && net_wsp(cc[y - 1])) y--;
        if (y - x >= dn && net_ci_prefix(cc + x, y - x, dir) && (y - x == dn || cc[x + dn] == '=' || net_wsp(cc[x + dn]))) {
            size_t q = x + dn;
            while (q < y && (net_wsp(cc[q]) || cc[q] == '=' || cc[q] == '"')) q++;
            if (q < y && cc[q] >= '0' && cc[q] <= '9') *v = atoll(cc + q);
            return 1;
        }
        a = b + 1;
    }
    return 0;
}

/* a response's freshness lifetime (4.2.1): none under no-cache; its max-age; else its
   Expires less its Date (an Expires that is no date has passed); else a tenth of the
   time since its Last-Modified, at most a week (4.2.2); else none */
static int64_t net_lifetime(const char *head, size_t hn, int64_t rtime) {
    int64_t v, t;
    if (net_cache_directive(head, hn, "no-cache", &v)) return 0;
    if (net_cache_directive(head, hn, "max-age", &v)) return v < 0 ? 0 : v;
    char e[256];
    int64_t date = rtime;
    if (net_header(head, hn, "date", e, sizeof e) && net_parse_date(e, strlen(e), &t)) date = t;
    if (net_header(head, hn, "expires", e, sizeof e)) {
        if (!net_parse_date(e, strlen(e), &t)) return 0;
        return t > date ? t - date : 0;
    }
    if (net_header(head, hn, "last-modified", e, sizeof e) && net_parse_date(e, strlen(e), &t) && t < date) {
        int64_t h = (date - t) / 10;
        return h > 7 * 86400 ? 7 * 86400 : h;
    }
    return 0;
}

/* the age a response came with (4.2.3): the more of its apparent age (its Date's
   distance before it was got) and its Age */
static int64_t net_initial_age(const char *head, size_t hn, int64_t rtime) {
    char e[256];
    int64_t t, age = 0;
    if (net_header(head, hn, "date", e, sizeof e) && net_parse_date(e, strlen(e), &t) && rtime > t) age = rtime - t;
    if (net_header(head, hn, "age", e, sizeof e)) {
        int64_t a = atoll(e);
        if (a > age) age = a;
    }
    return age;
}

/* whether a response may be kept (3): a 200 not marked no-store whose Vary names
   nothing a request of ours varies in (Accept-Encoding is the same in every one) */
static int net_storable(int status, const char *head, size_t hn) {
    int64_t v;
    char vary[512];
    if (status != 200 || net_cache_directive(head, hn, "no-store", &v)) return 0;
    if (net_header_all(head, hn, "vary", vary, sizeof vary)) {
        size_t n = strlen(vary), a = 0;
        while (a < n) {
            size_t b = a;
            while (b < n && vary[b] != ',') b++;
            size_t x = a, y = b;
            while (x < y && net_wsp(vary[x])) x++;
            while (y > x && net_wsp(vary[y - 1])) y--;
            if (y > x && !net_ci_is(vary + x, y - x, "accept-encoding")) return 0;
            a = b + 1;
        }
    }
    return 1;
}

/* an entry's file: the cache's directory and the FNV-1a hash of the URL; 0 when
   there is no cache */
static int net_cache_path(const net_loader *l, const char *url, char *out, size_t cap) {
    if (l->keeping_len < 2 || l->keeping[1].cache.len == 0) return 0;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; url[i]; i++) {
        h ^= (uint8_t)url[i];
        h *= 0x100000001b3ull;
    }
    char dir[1100];
    net_span_text(l, l->keeping[1].cache, dir, sizeof dir);
    snprintf(out, cap, "%s/%016llx", dir, (unsigned long long)h);
    return 1;
}

/* an entry read: its status, when it was got, its head (a field a line) and its
   body; 0 when there is none for the URL */
static int net_cache_read(const char *path, const char *url, int *status, int64_t *rtime, vector<char> *head, vector<char> *body) {
    vector<char> all;
    {
        own FILE *f = fopen(path, "rb");
        if (!f) return 0;
        defer(f) { fclose(f); }
        char buf[16384];
        size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) {
            append_range(all, buf, k);
            if (len(all) > NET_MAX_BODY + 65536) return 0;
        }
    }
    size_t n = len(all);
    const char *p = data(all);
    size_t end = net_head_end(p, n, 0);
    if (!end || n < 18 || memcmp(p, "AMBITIOUS-CACHE 1\n", 18) != 0) return 0;
    char u[8192], v[64];
    if (!net_header(p, end, "url", u, sizeof u) || strcmp(u, url) != 0) return 0;
    if (!net_header(p, end, "status", v, sizeof v)) return 0;
    *status = atoi(v);
    if (!net_header(p, end, "response-time", v, sizeof v)) return 0;
    *rtime = atoll(v);
    append_range(head, p, end);
    append_range(body, p + end, n - end);
    return 1;
}

/* an entry written, through a new file renamed over the old: the kept fields of
   the head first, else of the head under it (a 304's over the stored response's) */
static void net_cache_write(const char *path, const char *url, int status, int64_t rtime, const char *head, size_t hn, const char *under,
                            size_t un, const char *body, size_t bn) {
    char tmp[1300], dir[1200];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        char up[1200];
        snprintf(up, sizeof up, "%s", dir);
        char *s2 = strrchr(up, '/');
        if (s2 && s2 != up) { *s2 = 0; mkdir(up, 0700); }
        mkdir(dir, 0700);
    }
    {
        /* readable by its owner alone: a private response is the user's */
        mode_t mask = umask(077);
        own FILE *f = fopen(tmp, "wb");
        umask(mask);
        if (!f) return;
        defer(f) { fclose(f); }
        fprintf(f, "AMBITIOUS-CACHE 1\nurl: %s\nstatus: %d\nresponse-time: %lld\n", url, status, (long long)rtime);
        for (size_t k = 0; k < sizeof net_cache_fields / sizeof net_cache_fields[0]; k++) {
            char v[2048];
            if (net_header_all(head, hn, net_cache_fields[k], v, sizeof v) || net_header_all(under, un, net_cache_fields[k], v, sizeof v))
                fprintf(f, "%s: %s\n", net_cache_fields[k], v);
        }
        fputc('\n', f);
        if (bn) fwrite(body, 1, bn, f);
    }
    rename(tmp, path);
}

/* ---- content codings: gzip and deflate over the painter's inflate, br over its Brotli decoder, zstd ---- */

static uint32_t net_crc_tab[256];

/* CRC-32 (ISO 3309, gzip's), by a table made at the first call */
static uint32_t net_crc32(const uint8_t *p, size_t n) {
    if (!net_crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            net_crc_tab[i] = c;
        }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = net_crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

/* Adler-32 (RFC 1950, zlib's), its sums reduced every 5552 bytes */
static uint32_t net_adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i = 0;
    while (i < n) {
        size_t e = n - i > 5552 ? i + 5552 : n;
        for (; i < e; i++) { a += p[i]; b += a; }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

static uint32_t net_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t net_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* a gzip body (RFC 1952): each member's header passed over, its deflate data
   inflated, its CRC-32 and size checked; a member may follow another. 0 and why
   when it does not decode */
static int net_gunzip(const uint8_t *p, size_t n, vector<uint8_t> *out, char *why, size_t cap) {
    size_t i = 0;
    int members = 0;
    while (i < n) {
        if (n - i < 18 || p[i] != 0x1f || p[i + 1] != 0x8b || p[i + 2] != 8) {
            if (members) break;      /* what follows the last member is not read, as zlib's gunzip does not */
            snprintf(why, cap, "the gzip body does not begin as gzip does");
            return 0;
        }
        uint8_t flg = p[i + 3];
        size_t k = i + 10;
        if ((flg & 4) && k + 2 <= n) k += 2 + ((size_t)p[k] | (size_t)p[k + 1] << 8);
        if (flg & 8) { while (k < n && p[k]) k++; k++; }
        if (flg & 16) { while (k < n && p[k]) k++; k++; }
        if (flg & 2) k += 2;
        if (k >= n) { snprintf(why, cap, "the gzip body ends in its header"); return 0; }
        size_t start = len(*out);
        if (!amb_inflate_raw(p, n, &k, out, NET_MAX_BODY)) {
            snprintf(why, cap, len(*out) >= NET_MAX_BODY ? "the decoded body passed 32 MB" : "the gzip body's deflate data do not inflate");
            return 0;
        }
        if (k + 8 > n) { snprintf(why, cap, "the gzip body ends before its CRC-32 and size"); return 0; }
        size_t got = len(*out) - start;
        if (net_crc32(data(*out) + start, got) != net_le32(p + k)) { snprintf(why, cap, "the gzip body's CRC-32 is not its data's"); return 0; }
        if ((uint32_t)got != net_le32(p + k + 4)) { snprintf(why, cap, "the gzip body's size is not its data's"); return 0; }
        i = k + 8;
        members++;
    }
    return 1;
}

/* a deflate body: a zlib stream (RFC 1950), as HTTP's deflate is, its Adler-32
   checked; or, as browsers take it, raw deflate data with no zlib header */
static int net_inflate_body(const uint8_t *p, size_t n, vector<uint8_t> *out, char *why, size_t cap) {
    int zlib = n >= 2 && (p[0] & 0x0f) == 8 && (p[0] >> 4) <= 7 && ((unsigned)p[0] << 8 | p[1]) % 31 == 0 && !(p[1] & 0x20);
    size_t k = zlib ? 2 : 0;
    if (!amb_inflate_raw(p, n, &k, out, NET_MAX_BODY)) {
        snprintf(why, cap, len(*out) >= NET_MAX_BODY ? "the decoded body passed 32 MB" : "the deflate body does not inflate");
        return 0;
    }
    if (zlib) {
        if (k + 4 > n) { snprintf(why, cap, "the deflate body ends before its Adler-32"); return 0; }
        if (net_adler32(data(*out), len(*out)) != net_be32(p + k)) { snprintf(why, cap, "the deflate body's Adler-32 is not its data's"); return 0; }
    }
    return 1;
}

/* a body decoded from the codings its Content-Encoding lists, the last applied undone
   first; identity is none. gzip and deflate over the painter's inflate, br over its Brotli
   decoder (src/paint/brotli.c), zstd by src/net/zstd.c. The codings into coding;
   decoding_failed and why for a body that does not decode; too_large for one that decodes
   past 32 MB. One coding not read here (compress) and the body is taken as it came */
static net_outcome net_decode_body(const char *head, size_t hn, vector<char> *body, char *coding, size_t ccap, char *why, size_t wcap) {
    char ce[256], names[8][24];
    int nn = 0;
    coding[0] = 0;
    if (!net_header_all(head, hn, "content-encoding", ce, sizeof ce)) return NET_OUTCOME_OK;
    size_t n = strlen(ce), a = 0, k = 0;
    while (a < n && nn < 8) {
        size_t b = a;
        while (b < n && ce[b] != ',') b++;
        size_t x = a, y = b;
        while (x < y && net_wsp(ce[x])) x++;
        while (y > x && net_wsp(ce[y - 1])) y--;
        if (y > x && y - x < sizeof names[0] && !net_ci_is(ce + x, y - x, "identity")) {
            for (size_t c = 0; c < y - x; c++) names[nn][c] = net_lower(ce[x + c]);
            names[nn][y - x] = 0;
            int w = snprintf(coding + k, ccap - k, "%s%s", nn ? ", " : "", names[nn]);
            if (w > 0 && k + (size_t)w < ccap) k += (size_t)w;
            nn++;
        }
        a = b + 1;
    }
    /* a coding not read here leaves the body as it came, every coding undone (Chromium's
       URLRequestHttpJob passes the raw body on: measured, compress, and dcb or dcz with no
       dictionary offered) */
    for (int c = 0; c < nn; c++)
        if (strcmp(names[c], "gzip") != 0 && strcmp(names[c], "x-gzip") != 0 && strcmp(names[c], "deflate") != 0 && strcmp(names[c], "br") != 0 &&
            strcmp(names[c], "zstd") != 0) {
            coding[0] = 0;
            return NET_OUTCOME_OK;
        }
    for (int c = nn - 1; c >= 0; c--) {
        vector<uint8_t> in, out;
        resize(in, len(*body));
        if (len(in)) memcpy(data(in), data(*body), len(in));
        int ok = 0, too = 0;
        if (strcmp(names[c], "gzip") == 0 || strcmp(names[c], "x-gzip") == 0) {
            ok = net_gunzip(data(in), len(in), &out, why, wcap);
            too = !ok && len(out) >= NET_MAX_BODY;
        } else if (strcmp(names[c], "deflate") == 0) {
            ok = net_inflate_body(data(in), len(in), &out, why, wcap);
            too = !ok && len(out) >= NET_MAX_BODY;
        } else if (strcmp(names[c], "br") == 0) {
            ok = amb_brotli(data(in), len(in), &out, NET_MAX_BODY + 1);
            too = !ok && len(out) > NET_MAX_BODY;
            if (too) snprintf(why, wcap, "the decoded body passed 32 MB");
            else if (!ok) snprintf(why, wcap, "the br body is not a whole Brotli stream (RFC 7932)");
        } else if (strcmp(names[c], "zstd") == 0) {
            int z = net_zstd(data(in), len(in), &out, NET_MAX_BODY, why, wcap, 0);
            ok = z == 1;
            too = z < 0;
            if (too) snprintf(why, wcap, "the decoded body passed 32 MB");
        }
        if (!ok) return too ? NET_OUTCOME_TOO_LARGE : NET_OUTCOME_DECODING_FAILED;
        clear(*body);
        resize(body, len(out));
        if (len(out)) memcpy(data(*body), data(out), len(out));
    }
    return NET_OUTCOME_OK;
}

/* ---- the loader's records ---- */

static amb_span net_text(net_loader *l, const char *s) { return text_add(l, s, strlen(s)); }

/* ---- requests, as Chromium writes them ---- */

/* a URL potentially trustworthy (Secure Contexts 3.1): https: or wss:, a loopback host
   (localhost or under it, 127.0.0.0/8, ::1), or file:. Chromium asks such a URL's server
   for br and zstd as well as gzip and deflate, and sends it client hints and fetch
   metadata; others it sends neither, and asks for gzip and deflate alone (measured) */
static int net_trustworthy(const net_url *u) {
    if (strcmp(u->scheme, "https") == 0 || strcmp(u->scheme, "wss") == 0 || strcmp(u->scheme, "file") == 0) return 1;
    char host[256];
    net_bare_host(u->host, host, sizeof host);
    size_t n = strlen(host);
    if (n && host[n - 1] == '.') host[--n] = 0;
    for (size_t i = 0; i < n; i++) host[i] = net_lower(host[i]);
    if (strcmp(host, "localhost") == 0 || (n > 10 && strcmp(host + n - 10, ".localhost") == 0)) return 1;
    uint32_t v4;
    if (net_ipv4_dotted(host, n, &v4)) return (v4 >> 24) == 127;
    uint16_t v6[8];
    if (strchr(host, ':') && net_ipv6_parse(host, n, v6)) {
        for (int i = 0; i < 7; i++)
            if (v6[i]) return 0;
        return v6[7] == 1;
    }
    return 0;
}

/* ---- compression dictionaries (RFC 9842), kept and offered as Chromium keeps and offers them ----

   A response is kept as a dictionary when it carries Use-As-Dictionary (src/net/dict.c reads
   it: a match, a match-dest, an id, the type raw) from a potentially trustworthy URL (https:,
   a loopback host: measured over http://localhost), when it is readable by its page (of the
   page's origin, or asked for in CORS mode -- a crossorigin element -- and allowed by its
   Access-Control-Allow-Origin; measured: a no-cors script of another origin keeps none, a
   crossorigin one does), when its match is a pattern of its own origin with no regular
   expression, and when it is fresh: its freshness lifetime (max-age, Expires, or the
   heuristic of its Last-Modified) more than its age, no no-store, no-cache or Pragma:
   no-cache (measured: none of max-age=0, no-cache, no-store keeps one; a heuristic lifetime
   does; a 404 does; a gzipped body is kept decoded). It lives until that lifetime ends. A
   dictionary of the same origin, match and match-dest replaces the one before it.

   A request offers one when its URL is potentially trustworthy, when it is not a no-cors
   request of another origin than its page's (measured: an img of another origin offers none,
   one with crossorigin does), of the dictionaries of its URL's origin that have not expired,
   whose match-dest is none or holds the request's destination (document, style, image,
   script, or "" for a script's fetch: measured), and whose pattern matches its URL: the one
   of the longest match, the latest of equal ones (measured). It is sent as
   Available-Dictionary (its SHA-256, a byte sequence), with Dictionary-ID when it has an id,
   and Accept-Encoding offers dcb and dcz after the others.

   A response whose Content-Encoding is dcb or dcz alone, to a request that offered a
   dictionary, begins with its magic number and the dictionary's SHA-256 (another is
   ERR_UNEXPECTED_CONTENT_DICTIONARY_HEADER, measured: here decoding_failed), then a Brotli
   stream (src/paint/brotli.c) or a Zstandard frame (src/net/zstd.c) over the dictionary as a
   raw one. Without a dictionary offered, dcb and dcz are codings not read, and the body is
   taken as it came (measured), as every coding not read is.

   The dictionaries are the process's, as Chromium's are its profile's: kept in the cache's
   directory too when there is one (a profile's), an index (dictionaries) and a file of each
   dictionary's bytes, read at the first fetch that keeps one, so that a later run offers
   them. */
static int net_has_token(const char *v, const char *tok);
#define NET_DICTS 64
typedef struct net_dictionary {
    int live;
    char url[2048], origin[600];
    net_dict_field f;
    uint8_t hash[32];
    char sf[64];                 /* Available-Dictionary's value: :base64: */
    int64_t expires;
    uint64_t order;
    vector<uint8_t> bytes;
} net_dictionary;
static net_dictionary net_dicts[NET_DICTS];
static uint64_t net_dict_clock;
/* the cache directory whose index was read (once a process) */
static char net_dict_dir[1100];
static int net_dict_dir_read;

/* the request's destination, as match-dest names one */
static const char *net_dict_dest(net_kind kind) {
    if (kind == NET_KIND_DOCUMENT) return "document";
    if (kind == NET_KIND_STYLESHEET) return "style";
    if (kind == NET_KIND_IMAGE) return "image";
    if (kind == NET_KIND_SCRIPT) return "script";
    return "";
}

/* a dictionary's place freed */
static void net_dict_forget(int k) {
    vector_uint8_t_drop(&net_dicts[k].bytes);
    memset(&net_dicts[k], 0, sizeof net_dicts[k]);
}

/* a dictionary's SHA-256 as hex, and as Available-Dictionary writes it */
static void net_dict_hex(const uint8_t *h, char *out) {
    static const char x[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = x[h[i] >> 4];
        out[2 * i + 1] = x[h[i] & 15];
    }
    out[64] = 0;
}
static void net_dict_sf(net_dictionary *d) {
    char b64[64];
    net_base64_encode((const char *)d->hash, 32, b64, sizeof b64);
    snprintf(d->sf, sizeof d->sf, ":%s:", b64);
}

/* a dictionary's field written again, as Use-As-Dictionary would carry it (the index's form) */
static void net_dict_field_write(const net_dict_field *f, char *out, size_t cap) {
    vector<char> o;
    append_range(o, "match=\"", 7);
    for (size_t i = 0; f->match[i]; i++) {
        if (f->match[i] == '"' || f->match[i] == '\\') push_back(o, '\\');
        push_back(o, f->match[i]);
    }
    push_back(o, '"');
    if (f->has_dest) {
        append_range(o, ", match-dest=(", 14);
        size_t a = 0, n = strlen(f->dest);
        int first = 1;
        while (a < n) {
            size_t b = a;
            while (b < n && f->dest[b] != '\n') b++;
            if (!first) push_back(o, ' ');
            first = 0;
            push_back(o, '"');
            for (size_t i = a; i < b; i++) {
                if (f->dest[i] == '"' || f->dest[i] == '\\') push_back(o, '\\');
                push_back(o, f->dest[i]);
            }
            push_back(o, '"');
            a = b + 1;
        }
        push_back(o, ')');
    }
    if (f->id[0]) {
        append_range(o, ", id=\"", 6);
        for (size_t i = 0; f->id[i]; i++) {
            if (f->id[i] == '"' || f->id[i] == '\\') push_back(o, '\\');
            push_back(o, f->id[i]);
        }
        push_back(o, '"');
    }
    snprintf(out, cap, "%.*s", (int)len(o), data(o));
}

/* the cache's directory made, and the one it is in, as the cache makes them (its owner's alone) */
static void net_dict_dir_make(const char *dir) {
    char up[1200];
    snprintf(up, sizeof up, "%s", dir);
    size_t n = strlen(up);
    while (n && up[n - 1] != '/') n--;
    if (n > 1) {
        up[n - 1] = 0;
        mkdir(up, 0700);
    }
    mkdir(dir, 0700);
}

/* the cache directory's index written again: a line a dictionary (its hash, expiry, order, URL
   and field), each dictionary's bytes in a file of its hash; its owner's alone to read */
static void net_dict_index_write(const char *dir) {
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%s/dictionaries", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    mode_t mask = umask(077);
    own FILE *f = fopen(tmp, "w");
    umask(mask);
    if (!f) return;
    for (int k = 0; k < NET_DICTS; k++) {
        if (!net_dicts[k].live) continue;
        char hex[65], field[5000];
        net_dict_hex(net_dicts[k].hash, hex);
        net_dict_field_write(&net_dicts[k].f, field, sizeof field);
        fprintf(f, "%s\t%lld\t%llu\t%s\t%s\n", hex, (long long)net_dicts[k].expires, (unsigned long long)net_dicts[k].order, net_dicts[k].url,
                field);
    }
    fclose(f);
    rename(tmp, path);
}

/* the dictionaries the cache directory keeps read, once a process: those not expired, whose
   bytes are there and are their hash's */
static void net_dict_index_read(const net_loader *l) {
    if (l->keeping_len < 2 || l->keeping[1].cache.len == 0) return;
    char dir[1100];
    net_span_text(l, l->keeping[1].cache, dir, sizeof dir);
    if (net_dict_dir_read && strcmp(dir, net_dict_dir) == 0) return;
    net_dict_dir_read = 1;
    snprintf(net_dict_dir, sizeof net_dict_dir, "%s", dir);
    char path[1200];
    snprintf(path, sizeof path, "%s/dictionaries", dir);
    vector<char> all;
    {
        own FILE *f = fopen(path, "r");
        if (!f) return;
        defer(f) { fclose(f); }
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) append_range(all, buf, got);
    }
    int64_t now = net_now();
    size_t i = 0, n = len(all);
    while (i < n) {
        size_t e = i;
        while (e < n && all[e] != '\n') e++;
        char line[8000];
        snprintf(line, sizeof line, "%.*s", (int)(e - i), data(all) + i);
        i = e + 1;
        /* its five fields, at the tabs: the hash, the expiry, the order, the URL, the field */
        size_t ln = strlen(line), at[5], c = 1;
        at[0] = 0;
        for (size_t q = 0; q < ln && c < 5; q++)
            if (line[q] == '\t') {
                line[q] = 0;
                at[c++] = q + 1;
            }
        if (c < 5) continue;
        int64_t expires = atoll(line + at[1]);
        if (expires <= now || strlen(line) != 64 || strlen(line + at[3]) >= sizeof net_dicts[0].url) continue;
        net_dict_field fd;
        if (!net_dict_field_read(line + at[4], strlen(line + at[4]), &fd)) continue;
        int k = -1;
        for (int q = 0; q < NET_DICTS && k < 0; q++) if (!net_dicts[q].live) k = q;
        if (k < 0) break;
        char bpath[1300];
        snprintf(bpath, sizeof bpath, "%s/dict-%s", dir, line);
        vector<uint8_t> bytes;
        {
            own FILE *bf = fopen(bpath, "rb");
            if (!bf) continue;
            defer(bf) { fclose(bf); }
            uint8_t buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof buf, bf)) > 0 && len(bytes) <= NET_MAX_BODY) append_range(bytes, buf, got);
        }
        uint8_t h[32];
        char hex[65];
        net_sha256(data(bytes), len(bytes), h);
        net_dict_hex(h, hex);
        if (strcmp(hex, line) != 0) continue;
        net_dictionary *d = &net_dicts[k];
        d->live = 1;
        snprintf(d->url, sizeof d->url, "%s", line + at[3]);
        net_url_origin(d->url, d->origin, sizeof d->origin);
        memcpy(&d->f, &fd, sizeof fd);
        memcpy(d->hash, h, 32);
        net_dict_sf(d);
        d->expires = expires;
        d->order = (uint64_t)atoll(line + at[2]);
        if (d->order > net_dict_clock) net_dict_clock = d->order;
        append_range(d->bytes, data(bytes), len(bytes));
    }
}

/* a response kept as a dictionary when it may be (above): its index then, or -1. page is the
   page that asked (none for a navigation); cors whether it was asked in CORS mode */
static int net_dict_keep(const net_loader *l, const net_url *u, const net_url *page, int cors, const char *head, size_t hn, const char *body,
                         size_t bn, int64_t now) {
    char v[4096];
    if (!net_header_all(head, hn, "use-as-dictionary", v, sizeof v) || !net_trustworthy(u) || bn > NET_MAX_BODY) return -1;
    char url[2048], origin[600], porigin[600];
    if (net_url_text(u, url, sizeof url) >= sizeof url - 1) return -1;
    net_origin_text(u, origin, sizeof origin);
    if (page) {
        net_origin_text(page, porigin, sizeof porigin);
        if (strcmp(origin, porigin) != 0) {
            /* of another origin: readable only when asked in CORS mode and allowed */
            char acao[1024];
            if (!cors || !net_header_all(head, hn, "access-control-allow-origin", acao, sizeof acao) ||
                (strcmp(acao, "*") != 0 && strcmp(acao, porigin) != 0))
                return -1;
        }
    }
    int64_t dv;
    char pragma[256];
    if (net_cache_directive(head, hn, "no-store", &dv) || net_cache_directive(head, hn, "no-cache", &dv)) return -1;
    if (net_header_all(head, hn, "pragma", pragma, sizeof pragma) && net_has_token(pragma, "no-cache")) return -1;
    int64_t life = net_lifetime(head, hn, now) - net_initial_age(head, hn, now);
    if (life <= 0) return -1;
    net_dict_field f;
    if (!net_dict_field_read(v, strlen(v), &f) || !net_dict_pattern_ok(f.match, url)) return -1;
    net_dict_index_read(l);
    /* its place: the one of the same origin, match and match-dest, a free one, or the oldest's */
    int k = -1, free_at = -1, oldest = -1;
    for (int q = 0; q < NET_DICTS; q++) {
        if (!net_dicts[q].live) {
            if (free_at < 0) free_at = q;
            continue;
        }
        if (k < 0 && strcmp(net_dicts[q].origin, origin) == 0 && strcmp(net_dicts[q].f.match, f.match) == 0 &&
            net_dicts[q].f.has_dest == f.has_dest && strcmp(net_dicts[q].f.dest, f.dest) == 0)
            k = q;
        if (oldest < 0 || net_dicts[q].order < net_dicts[oldest].order) oldest = q;
    }
    if (k < 0) k = free_at >= 0 ? free_at : oldest;
    net_dict_forget(k);
    net_dictionary *d = &net_dicts[k];
    d->live = 1;
    snprintf(d->url, sizeof d->url, "%s", url);
    snprintf(d->origin, sizeof d->origin, "%s", origin);
    memcpy(&d->f, &f, sizeof f);
    append_range(d->bytes, (const uint8_t *)body, bn);
    net_sha256(data(d->bytes), len(d->bytes), d->hash);
    net_dict_sf(d);
    d->expires = now + life;
    d->order = ++net_dict_clock;
    if (l->keeping_len >= 2 && l->keeping[1].cache.len) {
        char dir[1100], hex[65], bpath[1300];
        net_span_text(l, l->keeping[1].cache, dir, sizeof dir);
        net_dict_hex(d->hash, hex);
        snprintf(bpath, sizeof bpath, "%s/dict-%s", dir, hex);
        net_dict_dir_make(dir);
        mode_t mask = umask(077);
        own FILE *bf = fopen(bpath, "wb");
        umask(mask);
        if (bf) {
            fwrite(data(d->bytes), 1, len(d->bytes), bf);
            fclose(bf);
        }
        net_dict_index_write(dir);
    }
    return k;
}

/* the dictionary a request offers (above): its index, or -1. page is the page that asked
   (none for a navigation), cors whether it is asked in CORS mode */
static int net_dict_offer(const net_loader *l, const net_url *u, const net_url *page, int cors, net_kind kind, int64_t now) {
    if (!net_trustworthy(u)) return -1;
    char url[2048], origin[600], porigin[600];
    if (net_url_text(u, url, sizeof url) >= sizeof url - 1) return -1;
    net_origin_text(u, origin, sizeof origin);
    if (page && !cors) {
        net_origin_text(page, porigin, sizeof porigin);
        if (strcmp(origin, porigin) != 0) return -1;
    }
    net_dict_index_read(l);
    const char *dest = net_dict_dest(kind);
    int best = -1;
    for (int k = 0; k < NET_DICTS; k++) {
        const net_dictionary *d = &net_dicts[k];
        if (!d->live || d->expires <= now || strcmp(d->origin, origin) != 0) continue;
        if (d->f.has_dest) {
            int in = 0;
            size_t a = 0, n = strlen(d->f.dest), dn = strlen(dest);
            while (a < n && !in) {
                size_t b = a;
                while (b < n && d->f.dest[b] != '\n') b++;
                in = b - a == dn && strncmp(d->f.dest + a, dest, dn) == 0;
                a = b + 1;
            }
            if (!in) continue;
        }
        if (!net_dict_pattern_matches(d->f.match, d->url, url)) continue;
        if (best < 0 || strlen(d->f.match) > strlen(net_dicts[best].f.match) ||
            (strlen(d->f.match) == strlen(net_dicts[best].f.match) && d->order > net_dicts[best].order))
            best = k;
    }
    return best;
}

/* a dcb or dcz body (Content-Encoding that alone) over the dictionary its request offered: its
   header (the magic number, the dictionary's SHA-256) checked, then decoded. ok, or
   decoding_failed (too_large past 32 MB) with why */
static net_outcome net_dict_decode(const char *coding, const uint8_t *hash, vector<char> *body, char *why, size_t wcap) {
    static const uint8_t dcb[4] = { 0xff, 0x44, 0x43, 0x42 }, dcz[8] = { 0x5e, 0x2a, 0x4d, 0x18, 0x20, 0x00, 0x00, 0x00 };
    int brotli = strcmp(coding, "dcb") == 0;
    size_t magic = brotli ? 4 : 8;
    int k = -1;
    for (int q = 0; q < NET_DICTS && k < 0; q++)
        if (net_dicts[q].live && memcmp(net_dicts[q].hash, hash, 32) == 0) k = q;
    if (k < 0) {
        snprintf(why, wcap, "the %s body's dictionary is no longer kept", coding);
        return NET_OUTCOME_DECODING_FAILED;
    }
    if (len(*body) < magic + 32 || memcmp(data(*body), brotli ? dcb : dcz, magic) != 0 || memcmp(data(*body) + magic, hash, 32) != 0) {
        snprintf(why, wcap, "the %s body does not begin with its dictionary's header (Chromium's ERR_UNEXPECTED_CONTENT_DICTIONARY_HEADER)", coding);
        return NET_OUTCOME_DECODING_FAILED;
    }
    vector<uint8_t> out;
    const uint8_t *in = (const uint8_t *)data(*body) + magic + 32;
    size_t n = len(*body) - magic - 32;
    int ok, too;
    if (brotli) {
        /* a stream cut short is what decoded before the cut (Chromium's decoder hands it on) */
        int cut = 0;
        ok = amb_brotli_prefix(in, n, data(net_dicts[k].bytes), len(net_dicts[k].bytes), &out, NET_MAX_BODY + 1, &cut);
        if (!ok && cut && len(out) <= NET_MAX_BODY) ok = 1;
        too = !ok && len(out) > NET_MAX_BODY;
        if (too) snprintf(why, wcap, "the decoded body passed 32 MB");
        else if (!ok) snprintf(why, wcap, "the dcb body is not a whole Brotli stream over its dictionary");
    } else {
        int z = net_zstd_dict(in, n, data(net_dicts[k].bytes), len(net_dicts[k].bytes), &out, NET_MAX_BODY, why, wcap, 0);
        ok = z == 1;
        too = z < 0;
        if (too) snprintf(why, wcap, "the decoded body passed 32 MB");
    }
    if (!ok) return too ? NET_OUTCOME_TOO_LARGE : NET_OUTCOME_DECODING_FAILED;
    clear(*body);
    resize(body, len(out));
    if (len(out)) memcpy(data(*body), data(out), len(out));
    return NET_OUTCOME_OK;
}

/* the platform sec-ch-ua-platform names, as Chromium names it */
static void net_platform(char *out, size_t cap) {
    struct utsname un;
    if (uname(&un) != 0) snprintf(out, cap, "Unknown");
    else if (strcmp(un.sysname, "Darwin") == 0) snprintf(out, cap, "macOS");
    else snprintf(out, cap, "%s", un.sysname);
}

/* the Referer of a request for u from the page at page, by the default referrer policy,
   strict-origin-when-cross-origin: the page's URL to its own origin; its origin alone to
   another; none from a page over TLS to a URL that is not potentially trustworthy, nor
   from a file: or opaque page */
static void net_referrer(const net_url *u, const net_url *page, char *out, size_t cap) {
    out[0] = 0;
    if (!page || page->opaque || strcmp(page->scheme, "file") == 0) return;
    if (strcmp(page->scheme, "https") == 0 && !net_trustworthy(u)) return;
    char a[512], b[512];
    net_origin_text(u, a, sizeof a);
    net_origin_text(page, b, sizeof b);
    if (strcmp(a, b) == 0) net_url_text(page, out, cap);
    else snprintf(out, cap, "%s/", b);
}

/* how a request's origin stands to its page's, as Sec-Fetch-Site says it: none (no page),
   same-origin, same-site, cross-site */
static const char *const net_sites[4] = { "none", "same-origin", "same-site", "cross-site" };
static int net_fetch_site(const net_url *u, const net_url *page) {
    if (!page) return 0;
    char a[512], b[512];
    net_origin_text(u, a, sizeof a);
    net_origin_text(page, b, sizeof b);
    if (strcmp(a, b) == 0 && strcmp(a, "null") != 0) return 1;
    return net_is_same_site(u, page) ? 2 : 3;
}

static void net_line(vector<char> *o, const char *name, const char *value) {
    append_range(o, name, strlen(name));
    append_range(o, ": ", 2);
    append_range(o, value, strlen(value));
    append_range(o, "\r\n", 2);
}
/* the lines of s (n bytes, each ending in CRLF) named name (want 1), or all the others
   (want 0) */
static void net_lines_named(vector<char> *o, const char *s, size_t n, const char *name, int want) {
    size_t i = 0, nl = strlen(name);
    while (i < n) {
        size_t e = i;
        while (e < n && s[e] != '\n') e++;
        if (e < n) e++;
        int is = e - i > nl && s[i + nl] == ':' && net_ci_prefix(s + i, e - i, name);
        if (is == want) append_range(o, s + i, e - i);
        i = e;
    }
}
static int net_has_line(const char *s, size_t n, const char *name) {
    size_t i = 0, nl = strlen(name);
    while (i < n) {
        size_t e = i;
        while (e < n && s[e] != '\n') e++;
        if (e - i > nl && s[i + nl] == ':' && net_ci_prefix(s + i, e - i, name)) return 1;
        i = e + 1;
    }
    return 0;
}

/* ---- a fetch, step by step ---- */

/* a fetch's state, from its record made to its record filled: J_HOP, its next URL to be
   asked for; J_NET, an exchange of it on the way; J_DONE */
enum { J_HOP, J_NET, J_DONE };
/* an exchange's: waiting for a connection, carried by one, answered (or failed) */
enum { X_WAIT, X_ON, X_DONE };

/* a fetch of a batch (net_batch): what Fetch's main fetch keeps from hop to hop, and its
   exchange while one is under way. Text that grows with a page (a request, a head, a cached
   entry, a body) is in the batch's arena, by place */
typedef struct net_job {
    uint32_t r;                    /* its resource */
    net_kind kind;
    int nav;                       /* a navigation: a document asked for at the top */
    int state, hop;
    net_url u;                     /* where it is now */
    char meth[16];
    size_t sent, sent_n;           /* the request's body, in the arena */
    size_t hdrs, hdrs_n;           /* the caller's header lines, in the arena */
    uint32_t priority, incremental;   /* Chromium's priority for it: 0 (highest) to 4; RFC 9218's i */
    int same;                      /* same-site with its page, or a navigation */
    net_outcome out;
    int status;
    net_route rt;
    net_cache_use cache;
    int64_t cache_age, cache_life, now;
    int caching, cached, c_status;
    int64_t c_rtime, c_age, c_life;
    size_t c_head, c_head_n, c_body, c_body_n;   /* the cached entry, in the arena */
    char here[8192], cpath[1300];  /* its URL as the cache keys it, and its entry's file */
    char type[256], charset[64], coding[200];
    int coded;
    uint32_t encoded;
    size_t body, body_n;           /* its body, decoded, in the arena */
    int recorded;                  /* its record filled already (a URL that is no URL) */
    /* the exchange: the connection it needs (its key, where it goes, the proxy, a tunnel's
       target) */
    int x_state, x_https, x_via, x_port, x_head_only;
    char x_key[700], x_host[256], x_authority[300], x_tunnel[300];
    net_proxy_t x_px;
    size_t x_req, x_req_n;         /* the HTTP/1.1 request, whole, in the arena */
    size_t x_lines, x_lines_n;     /* its header lines after Connection's, for HTTP/2 */
    size_t x_target, x_target_n;   /* its request target, for :path */
    int x_conn, x_retries, x_fresh, x_got, x_reused;
    uint32_t x_stream, x_number;
    size_t x_body_sent;
    int x_status;
    net_outcome x_out;
    size_t x_head, x_head_n;       /* the response's head, a status line and fields, in the arena */
    int32_t x_first, x_last;       /* its body's pieces */
    uint64_t x_bytes;
    int x_h2;
    /* the addresses its host (or its proxy) resolved to, looked up once; whether it rides a
       connection made for another host (pooling by IP), whether it may (not after a 421),
       whether it was asked again for a 421 */
    int x_resolved, x_naddr, x_family[8];
    struct sockaddr_storage x_addr[8];
    socklen_t x_alen[8];
    int x_pooled, x_nopool, x_421;
    /* asked in CORS mode (a crossorigin element); the dictionary it offered (Available-
       Dictionary's value, its id, its SHA-256), and whether its response was kept as one */
    int cors, dict, dict_kept;
    char dict_sf[64], dict_id[1040], kept_sf[64], kept_match[2048], kept_id[1040];
    uint8_t dict_hash[32];
} net_job;

/* a piece of a body that came, in the arena; the next piece of the same exchange's */
typedef struct net_piece { size_t off, len; int32_t next; } net_piece;

/* ---- connections, kept for reuse ---- */

#define NET_POOL 64
#define NET_PER_ORIGIN 6
/* Chromium's kMaxRetryAttempts: a stream refused, or left unprocessed by a GOAWAY, goes
   again twice at most */
#define NET_RETRIES 2
#define NET_IDLE_MS 60000
/* how long a connection may wait, by what it waits for, as Chromium's: its TCP connect four
   minutes (TransportConnectJob's 240 s, from its source: no address here leaves a connect
   unanswered to measure it by), a proxy's tunnel and a TLS handshake 30 s each (measured: a
   proxy that never answers CONNECT and a server that never answers the ClientHello, each
   ERR_TIMED_OUT after 30.0 s), an answer for ever (measured: a page answered after 150 s, an
   HTTP/2 stream after 40 s, a body stalled for 15 s, each waited out and loaded) */
#define NET_CONNECT_MS 240000
#define NET_TUNNEL_MS 30000
#define NET_HANDSHAKE_MS 30000

enum { NC_FREE, NC_CONNECT, NC_TUNNEL, NC_HANDSHAKE, NC_READY };

/* a connection of the pool, kept between fetches for its origin (and the proxy it goes
   through: its key) to carry more: its socket and protocol; the addresses to dial in
   turn, and the one it reached (peer); a tunnel's target and credentials; the port of its
   TLS session's server (through a tunnel, the target's); what it goes over (TLS, ALPN, the
   proxy); when its present wait began (its connect, tunnel or handshake) and the last
   byte's time; its number in the run and the requests it carried; over HTTP/1.1 the job it
   carries; its bytes both ways; over HTTP/2 its frames' state */
typedef struct net_conn {
    int state, fd, h2, tls, family[8], naddr, at, peer, err, want_write, reusable, busy;
    char key[700], host[256], target[300], auth[700];
    int port, tls_port;
    struct sockaddr_storage addr[8];
    socklen_t alen[8];
    net_route rt;
    int64_t began, touched;
    uint32_t number, served;
    vector<uint8_t> in, out;
    h2_conn h;
} net_conn;

static net_conn net_pool[NET_POOL];
static own SSL *net_pool_ssl[NET_POOL];
static uint32_t net_conns_made;
static int net_pool_kept;

static int64_t net_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* a slot's connection closed and the slot freed. Its TLS connection is marked shut down
   first, the flag alone (no close_notify goes): OpenSSL takes a session freed without it for a
   bad one and makes it, the very object the session cache keeps, never resumable */
static void net_conn_close(int i) {
    if (net_pool_ssl[i]) {
        SSL_set_shutdown(net_pool_ssl[i], NET_SSL_SENT_SHUTDOWN);
        SSL_free(net_pool_ssl[i]);
    }
    net_pool_ssl[i] = 0;
    if (net_pool[i].state != NC_FREE && net_pool[i].fd >= 0) close(net_pool[i].fd);
    vector_uint8_t_drop(&net_pool[i].in);
    vector_uint8_t_drop(&net_pool[i].out);
    H2_CONN_DROP(net_pool[i].h);
    memset(&net_pool[i], 0, sizeof net_pool[i]);
    net_pool[i].fd = -1;
    net_pool[i].busy = -1;
    net_pool[i].peer = -1;
    net_pool[i].state = NC_FREE;
}

/* ---- TLS sessions, kept to be resumed ----

   As Chromium's SSLClientSessionCache keeps them: by the server's host and port (the
   origin's, through a tunnel too), two sessions a server at most, the newest first. A TLS
   1.3 session (a ticket) is used once: taken from the cache when a handshake offers it, the
   one after it moving up; a TLS 1.2 one (a session ID, or a ticket) is offered again and
   again, a newer one replacing it. One past its lifetime is offered no more. Measured
   against node's TLS servers (tests/net/h2serve.js), which send two tickets after a full TLS
   1.3 handshake and none after a resumed one: of connections made one after another, the
   second and third resume and the fourth does not; of five made at once after the first,
   two resume; over TLS 1.2 every one after the first resumes, by ticket or by session ID.
   The sessions live as long as the process, as Chromium's live as long as its network
   context, and are never written down. */
#define NET_SESSIONS 64
typedef struct net_sess_key { char key[300]; int live; uint64_t used; } net_sess_key;
static net_sess_key net_sess_keys[NET_SESSIONS];
/* each key's two sessions, the newest first. An element is only ever moved out by passing
   it to an own parameter, which empties it: one copied out by assignment would stay in it,
   and be freed (by free(), not SSL_SESSION_free) when it is next written (toolchain/REPORT.md,
   finding 38) */
static own SSL_SESSION *net_sess[NET_SESSIONS][2];
static uint64_t net_sess_clock;

static own SSL_SESSION *net_sess_take(own SSL_SESSION *s) { return s; }
static int net_sess_single(const SSL_SESSION *s) { return SSL_SESSION_get_protocol_version(s) >= NET_TLS1_3_VERSION; }
static int net_sess_expired(const SSL_SESSION *s, long now) {
    long t = SSL_SESSION_get_time(s);
    return now < t || now >= t + SSL_SESSION_get_timeout(s);
}
/* a key's two sessions freed, the key forgotten */
static void net_sess_forget(int e) {
    if (net_sess[e][0]) SSL_SESSION_free(net_sess[e][0]);
    if (net_sess[e][1]) SSL_SESSION_free(net_sess[e][1]);
    net_sess_keys[e].live = 0;
    net_sess_keys[e].key[0] = 0;
}
/* the entry of a key: -1 when it has none (and make is 0); a new one made in a free place,
   or in the least recently used one's */
static int net_sess_find(const char *key, int make) {
    int free_at = -1, oldest = -1;
    for (int e = 0; e < NET_SESSIONS; e++) {
        if (net_sess_keys[e].live && strcmp(net_sess_keys[e].key, key) == 0) {
            net_sess_keys[e].used = ++net_sess_clock;
            return e;
        }
        if (!net_sess_keys[e].live && free_at < 0) free_at = e;
        if (net_sess_keys[e].live && (oldest < 0 || net_sess_keys[e].used < net_sess_keys[oldest].used)) oldest = e;
    }
    if (!make) return -1;
    int e = free_at >= 0 ? free_at : oldest;
    if (net_sess_keys[e].live) net_sess_forget(e);
    snprintf(net_sess_keys[e].key, sizeof net_sess_keys[e].key, "%s", key);
    net_sess_keys[e].live = 1;
    net_sess_keys[e].used = ++net_sess_clock;
    return e;
}
/* a new session kept (Chromium's Entry::Push): a single-use newest moves to second place,
   another is replaced */
static void net_sess_push(int e, own SSL_SESSION *s) {
    if (net_sess[e][0] && net_sess_single(net_sess[e][0])) {
        if (net_sess[e][1]) SSL_SESSION_free(net_sess[e][1]);
        net_sess[e][1] = net_sess_take(net_sess[e][0]);
    } else if (net_sess[e][0]) SSL_SESSION_free(net_sess[e][0]);
    net_sess[e][0] = s;
}
/* the key's newest session offered by the handshake to come (Chromium's Lookup: Entry::Pop,
   then what has expired dropped): taken from the cache if it is single-use */
static void net_sess_offer(SSL *ssl, int e) {
    long now = (long)time(0);
    if (!net_sess[e][0]) return;
    if (net_sess_single(net_sess[e][0])) {
        own SSL_SESSION *s = net_sess_take(net_sess[e][0]);
        net_sess[e][0] = net_sess_take(net_sess[e][1]);
        if (!net_sess_expired(s, now)) SSL_set_session(ssl, s);
        SSL_SESSION_free(s);
    } else if (!net_sess_expired(net_sess[e][0], now)) SSL_set_session(ssl, net_sess[e][0]);
    if (!net_sess[e][0] || net_sess_expired(net_sess[e][0], now)) net_sess_forget(e);
    else if (net_sess[e][1] && net_sess_expired(net_sess[e][1], now)) SSL_SESSION_free(net_sess[e][1]);
}
/* the key of a connection's TLS sessions: its server's host and port */
static void net_sess_key_of(int i, char *out, size_t cap) { snprintf(out, cap, "%s:%d", net_pool[i].host, net_pool[i].tls_port); }

/* OpenSSL's hand-over of a new session of one of the pool's connections: kept, by that
   connection's key */
static int net_session_new(SSL *ssl, own SSL_SESSION *sess) {
    int i = -1;
    for (int k = 0; k < NET_POOL && i < 0; k++)
        if (net_pool_ssl[k] && net_pool_ssl[k] == ssl) i = k;
    if (i < 0) {
        SSL_SESSION_free(sess);
        return 1;
    }
    char key[300];
    net_sess_key_of(i, key, sizeof key);
    net_sess_push(net_sess_find(key, 1), sess);
    return 1;
}

static void net_dict_forget(int k);
/* the pool's connections closed as the process ends, the sessions, the TLS context and the
   dictionaries freed */
static void net_pool_end(void) {
    for (int i = 0; i < NET_POOL; i++)
        if (net_pool[i].state != NC_FREE) net_conn_close(i);
    for (int k = 0; k < NET_DICTS; k++) net_dict_forget(k);
    for (int e = 0; e < NET_SESSIONS; e++) net_sess_forget(e);
    if (net_tls_proc.ctx) SSL_CTX_free(net_tls_proc.ctx);
}
static void net_pool_kept_once(void) {
    if (net_pool_kept) return;
    atexit(net_pool_end);
    net_pool_kept = 1;
    for (int i = 0; i < NET_POOL; i++)
        if (net_pool[i].state == NC_FREE) net_pool[i].peer = -1;
}

/* an idle connection still open: no byte waits on it (over HTTP/2 frames may; over TLS the
   records after the handshake, a session's tickets, are read first, as Chromium's peek
   reads them), its server has not closed it, it has not idled past a minute */
static int net_conn_alive(int i) {
    if (net_ms() - net_pool[i].touched > NET_IDLE_MS) return 0;
    char b;
    ssize_t k = recv(net_pool[i].fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (k == 0) return 0;
    if (k > 0 && net_pool[i].h2) return 1;
    if (k > 0 && !net_pool_ssl[i]) return 0;
    if (k > 0) {
        int r = SSL_peek(net_pool_ssl[i], &b, 1);
        return r <= 0 && SSL_get_error(net_pool_ssl[i], r) == NET_SSL_ERROR_WANT_READ;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

/* the next of a connection's addresses dialled, without waiting: 0 when none is left */
static int net_dial_next(int i) {
    net_conn *c = &net_pool[i];
    while (c->at < c->naddr) {
        int k = c->at++;
        int fd = socket(c->family[k], SOCK_STREAM, 0);
        if (fd < 0) {
            c->err = errno;
            continue;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (connect(fd, (struct sockaddr *)&c->addr[k], c->alen[k]) == 0 || errno == EINPROGRESS) {
            c->fd = fd;
            return 1;
        }
        c->err = errno;
        close(fd);
    }
    c->fd = -1;
    return 0;
}

/* a free slot: the least recently used idle connection closed when none is free; -1 when
   every connection is busy */
static int net_slot(void) {
    int oldest = -1;
    for (int i = 0; i < NET_POOL; i++) {
        if (net_pool[i].state == NC_FREE) return i;
        if (net_pool[i].state == NC_READY && net_pool[i].busy < 0 && (!net_pool[i].h2 || !h2_open_streams(&net_pool[i].h)) &&
            (oldest < 0 || net_pool[i].touched < net_pool[oldest].touched))
            oldest = i;
    }
    if (oldest >= 0) net_conn_close(oldest);
    return oldest;
}

/* localhost, or a name under it: names Chromium resolves itself, to the loopback addresses
   (net::ResolveLocalHostname), never asking DNS; its final dot and its case aside */
static int net_localhost_name(const char *h) {
    char t[256];
    size_t n = strlen(h);
    if (n >= sizeof t) return 0;
    for (size_t i = 0; i < n; i++) t[i] = net_lower(h[i]);
    t[n] = 0;
    if (n && t[n - 1] == '.') t[--n] = 0;
    return strcmp(t, "localhost") == 0 || (n > 10 && strcmp(t + n - 10, ".localhost") == 0);
}

/* a name's (or an address's) addresses at a port, added to the job's: by getaddrinfo, or
   numerically for an address. 0 when it does not resolve, its words in the job's reason */
static int net_job_lookup(net_job *j, const char *host, int port, int numeric) {
    net_addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = numeric ? 4 : 0;          /* AI_NUMERICHOST */
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    own net_addrinfo *res = 0;
    int g = getaddrinfo(host, ps, &hints, &res);
    if (g != 0 || !res) {
        snprintf(j->rt.reason, sizeof j->rt.reason, "%s: %s", host, gai_strerror(g));
        freeaddrinfo(res);
        return 0;
    }
    defer(res) { freeaddrinfo(res); }
    for (const net_addrinfo *a = res; a && j->x_naddr < 8; a = a->ai_next) {
        if ((size_t)a->ai_addrlen > sizeof j->x_addr[0]) continue;
        memcpy(&j->x_addr[j->x_naddr], a->ai_addr, a->ai_addrlen);
        j->x_alen[j->x_naddr] = a->ai_addrlen;
        j->x_family[j->x_naddr] = a->ai_family;
        j->x_naddr++;
    }
    return 1;
}

/* the addresses a job's connection would be made to, its host's (or its proxy's), looked up
   once: localhost and the names under it the loopback addresses, ::1 then 127.0.0.1, as
   Chromium has them; another name by getaddrinfo. 0 when there are none, the job's outcome
   and reason set */
static int net_job_resolve(net_job *j) {
    if (j->x_resolved) return j->x_naddr > 0;
    j->x_resolved = 1;
    j->x_naddr = 0;
    char dhost[256];
    int dport = j->x_port;
    snprintf(dhost, sizeof dhost, "%s", j->x_host);
    if (j->x_via) {
        snprintf(dhost, sizeof dhost, "%s", j->x_px.host);
        dport = j->x_px.port;
    }
    int ok = net_localhost_name(dhost) ? net_job_lookup(j, "::1", dport, 1) + net_job_lookup(j, "127.0.0.1", dport, 1) > 0
                                       : net_job_lookup(j, dhost, dport, 0);
    if (!ok || !j->x_naddr) {
        j->x_naddr = 0;
        j->x_out = NET_OUTCOME_UNREACHABLE;
        return 0;
    }
    return 1;
}

/* a new connection begun for the job's key: the job's addresses (its host's, or its
   proxy's), the first dialled, each address tried in turn. The slot; or -1, the job's
   outcome and reason set */
static int net_conn_open(net_job *j) {
    if (!net_job_resolve(j)) return -1;
    int dport = j->x_via ? j->x_px.port : j->x_port;
    int i = net_slot();
    if (i < 0) {
        snprintf(j->rt.reason, sizeof j->rt.reason, "no connection could be made: all %d are busy", NET_POOL);
        j->x_out = NET_OUTCOME_REFUSED;
        return -1;
    }
    net_pool_kept_once();
    net_conn *c = &net_pool[i];
    c->fd = -1;
    c->busy = -1;
    c->peer = -1;
    for (int k = 0; k < j->x_naddr; k++) {
        memcpy(&c->addr[c->naddr], &j->x_addr[k], j->x_alen[k]);
        c->alen[c->naddr] = j->x_alen[k];
        c->family[c->naddr] = j->x_family[k];
        c->naddr++;
    }
    snprintf(c->key, sizeof c->key, "%s", j->x_key);
    snprintf(c->host, sizeof c->host, "%s", j->x_host);
    c->port = dport;
    c->tls_port = j->x_port;
    c->tls = j->x_https;
    c->rt.proxied = j->x_via;
    if (j->x_via) snprintf(c->rt.proxy, sizeof c->rt.proxy, "%s", j->x_px.origin);
    if (j->x_via && j->x_https) {
        snprintf(c->target, sizeof c->target, "%s", j->x_tunnel);
        snprintf(c->auth, sizeof c->auth, "%s", j->x_px.auth);
    }
    c->number = ++net_conns_made;
    c->touched = net_ms();
    c->began = c->touched;
    c->state = NC_CONNECT;
    if (!net_dial_next(i)) {
        snprintf(j->rt.reason, sizeof j->rt.reason, "%s port %d: %s", j->x_via ? j->x_px.host : j->x_host, dport, strerror(c->err));
        j->x_out = NET_OUTCOME_REFUSED;
        net_conn_close(i);
        return -1;
    }
    return i;
}

/* ---- TLS, without waiting ---- */

/* the session begun over the connection's socket: SNI and the name or address to verify
   from the bare host (net_tls_ready made the context). 0 when no session could be made */
static int net_tls_begin(int i, net_tls *t) {
    if (!net_tls_ready(t)) {
        snprintf(net_pool[i].rt.reason, sizeof net_pool[i].rt.reason, "%s", t->why);
        return 0;
    }
    net_pool_ssl[i] = SSL_new(t->ctx);
    if (!net_pool_ssl[i]) {
        snprintf(net_pool[i].rt.reason, sizeof net_pool[i].rt.reason, "no TLS session could be made");
        return 0;
    }
    char name[256];
    snprintf(name, sizeof name, "%s", net_pool[i].host);
    SSL_set_fd(net_pool_ssl[i], net_pool[i].fd);
    if (net_is_address(name)) X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(net_pool_ssl[i]), name);
    else {
        SSL_ctrl(net_pool_ssl[i], NET_SSL_CTRL_SET_TLSEXT_HOSTNAME, 0, name);
        /* the name held to the certificate's DNS names alone, a wildcard a whole first label,
           as Chromium matches one (its subject's common name is never read) */
        SSL_set_hostflags(net_pool_ssl[i], NET_X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS | NET_X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
        SSL_set1_host(net_pool_ssl[i], name);
    }
    /* the session this server's last handshake left, offered for resumption */
    char key[300];
    net_sess_key_of(i, key, sizeof key);
    int e = net_sess_find(key, 0);
    if (e >= 0) net_sess_offer(net_pool_ssl[i], e);
    return 1;
}

/* the handshake taken a step: 1 when it is done (its version, cipher and ALPN's choice
   kept), 0 while it waits, -1 when it failed (its outcome in *o, its reason the library's) */
static int net_tls_step(int i, net_outcome *o) {
    net_conn *c = &net_pool[i];
    ERR_clear_error();
    int r = SSL_connect(net_pool_ssl[i]);
    if (r == 1) {
        c->rt.tls = 1;
        c->rt.resumed = SSL_session_reused(net_pool_ssl[i]) == 1;
        snprintf(c->rt.version, sizeof c->rt.version, "%s", SSL_get_version(net_pool_ssl[i]));
        /* the cipher's name read at once: a pointer tied to a session of the pool's array,
           kept in a local, is taken for one never consumed (toolchain/REPORT.md, finding 29) */
        snprintf(c->rt.cipher, sizeof c->rt.cipher, "%s", SSL_CIPHER_get_name(SSL_get_current_cipher(net_pool_ssl[i])));
        const unsigned char *ap = 0;
        unsigned int an = 0;
        SSL_get0_alpn_selected(net_pool_ssl[i], &ap, &an);
        c->rt.alpn[0] = 0;
        if (ap && an < sizeof c->rt.alpn) {
            memcpy(c->rt.alpn, ap, an);
            c->rt.alpn[an] = 0;
        }
        c->h2 = strcmp(c->rt.alpn, "h2") == 0;
        c->want_write = 0;
        return 1;
    }
    int e = SSL_get_error(net_pool_ssl[i], r);
    if (e == NET_SSL_ERROR_WANT_READ || e == NET_SSL_ERROR_WANT_WRITE) {
        c->want_write = e == NET_SSL_ERROR_WANT_WRITE;
        return 0;
    }
    long v = SSL_get_verify_result(net_pool_ssl[i]);
    if (v != 0) {
        snprintf(c->rt.reason, sizeof c->rt.reason, "%s", X509_verify_cert_error_string(v));
        *o = NET_OUTCOME_TLS_VERIFY_FAILED;
        return -1;
    }
    unsigned long err = ERR_get_error();
    if (err) {
        /* error:CODE:LIBRARY:FUNCTION:REASON -- the reason */
        char buf[256];
        ERR_error_string_n(err, buf, sizeof buf);
        const char *rs = buf;
        for (int k = 0; k < 4 && strchr(rs, ':'); k++) rs = strchr(rs, ':') + 1;
        snprintf(c->rt.reason, sizeof c->rt.reason, "%s", rs);
    } else snprintf(c->rt.reason, sizeof c->rt.reason, "the server closed the connection in the handshake");
    *o = NET_OUTCOME_TLS_HANDSHAKE_FAILED;
    return -1;
}

/* ---- bytes both ways ---- */

/* the connection's out written, as much as the socket takes: 1 all of it, 0 to wait, -1
   when the connection failed */
static int net_conn_write(int i) {
    net_conn *c = &net_pool[i];
    while (len(c->out)) {
        size_t n = len(c->out) > 65536 ? 65536 : len(c->out);
        ssize_t k;
        if (net_pool_ssl[i]) {
            ERR_clear_error();
            int r = SSL_write(net_pool_ssl[i], (const char *)data(c->out), (int)n);
            if (r <= 0) {
                int e = SSL_get_error(net_pool_ssl[i], r);
                c->want_write = e == NET_SSL_ERROR_WANT_WRITE;
                if (e == NET_SSL_ERROR_WANT_WRITE || e == NET_SSL_ERROR_WANT_READ) return 0;
                return -1;
            }
            k = r;
        } else {
            k = send(c->fd, data(c->out), n, MSG_NOSIGNAL);
            if (k < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    c->want_write = 1;
                    return 0;
                }
                return -1;
            }
        }
        size_t left = len(c->out) - (size_t)k;
        memmove(data(c->out), data(c->out) + k, left);
        resize(c->out, left);
    }
    c->want_write = 0;
    return 1;
}

/* what waits on the connection read into its in: 1 (some came, or none waited), 0 when the
   server closed it, -1 when it failed */
static int net_conn_read(int i) {
    net_conn *c = &net_pool[i];
    char buf[65536];
    for (;;) {
        if (net_pool_ssl[i]) {
            ERR_clear_error();
            int r = SSL_read(net_pool_ssl[i], buf, (int)sizeof buf);
            if (r > 0) {
                append_range(c->in, buf, (size_t)r);
                c->touched = net_ms();
                continue;
            }
            int e = SSL_get_error(net_pool_ssl[i], r);
            if (e == NET_SSL_ERROR_WANT_READ) return 1;
            if (e == NET_SSL_ERROR_WANT_WRITE) {
                c->want_write = 1;
                return 1;
            }
            if (e == NET_SSL_ERROR_ZERO_RETURN) return 0;
            return -1;
        }
        ssize_t k = recv(c->fd, buf, sizeof buf, 0);
        if (k > 0) {
            append_range(c->in, buf, (size_t)k);
            c->touched = net_ms();
            continue;
        }
        if (k == 0) return 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
        return -1;
    }
}

/* ---- HTTP/1.1 over a connection kept alive ---- */

/* where a chunked body ends in p: past its last chunk and its trailers' blank line; 0
   while it has not come, -1 when it is malformed */
static long long net_chunks_end(const char *p, size_t n) {
    size_t b = 0;
    for (;;) {
        size_t e = b;
        uint64_t size = 0;
        int any = 0;
        while (e < n && net_hexval(p[e]) >= 0) {
            size = size * 16 + (uint64_t)net_hexval(p[e]);
            e++;
            any = 1;
            if (size > NET_MAX_BODY) return -1;
        }
        if (e >= n) return 0;
        if (!any) return -1;
        while (e < n && p[e] != '\n') e++;
        if (e >= n) return 0;
        e++;
        if (size == 0)
            for (;;) {
                size_t l = e;
                while (l < n && p[l] != '\n') l++;
                if (l >= n) return 0;
                if (l == e || (l == e + 1 && p[e] == '\r')) return (long long)(l + 1);
                e = l + 1;
            }
        if (e + size > n) return 0;
        b = e + (size_t)size;
        if (b < n && p[b] == '\r') b++;
        if (b < n && p[b] == '\n') b++;
        else if (b >= n) return 0;
    }
}

/* a token in a list header's value, its case aside */
static int net_has_token(const char *v, const char *tok) {
    size_t n = strlen(v), t = strlen(tok), a = 0;
    while (a < n) {
        size_t b = a;
        while (b < n && v[b] != ',') b++;
        size_t x = a, y = b;
        while (x < y && net_wsp(v[x])) x++;
        while (y > x && net_wsp(v[y - 1])) y--;
        if (y - x == t && net_ci_prefix(v + x, t, tok)) return 1;
        a = b + 1;
    }
    return 0;
}

/* where an HTTP/1.1 response ends in p (n bytes), by its framing: its length, or 0 while
   it has not all come; *keep whether the connection may carry another request after it (an
   HTTP/1.1 response not marked close, an HTTP/1.0 one marked keep-alive, its body not run
   to the connection's close) */
static size_t net_h1_end(const char *p, size_t n, int head_only, int *keep) {
    size_t end = 0, a = net_final_head(p, n, &end);
    *keep = 0;
    if (!a) return 0;
    a--;
    int st = net_status_of(p + a, end - a);
    if (st < 0) return 0;
    char v[256];
    int has_conn = net_header(p + a, end - a, "connection", v, sizeof v);
    if (memcmp(p + a, "HTTP/1.1", 8) == 0) *keep = !(has_conn && net_has_token(v, "close"));
    else *keep = has_conn && net_has_token(v, "keep-alive");
    if (head_only || st == 204 || st == 304) return end;
    if (net_header(p + a, end - a, "transfer-encoding", v, sizeof v)) {
        if (!strstr(v, "chunked")) {
            *keep = 0;
            return 0;
        }
        long long r = net_chunks_end(p + end, n - end);
        if (r < 0) {
            *keep = 0;
            return n;
        }
        if (r == 0) return 0;
        return end + (size_t)r;
    }
    if (net_header(p + a, end - a, "content-length", v, sizeof v)) {
        long long cl = atoll(v);
        if (cl < 0) {
            *keep = 0;
            return n;
        }
        if ((unsigned long long)(n - end) >= (unsigned long long)cl) return end + (size_t)cl;
        return 0;
    }
    *keep = 0;
    return 0;
}

/* a body's bytes kept as the next piece of the job's body */
static void net_piece_add(net_job *j, vector<net_piece> *pieces, vector<char> *arena, const char *p, size_t n) {
    net_piece pc = { len(*arena), n, -1 };
    append_range(arena, p, n);
    int32_t at = (int32_t)len(*pieces);
    push_back(pieces, pc);
    if (j->x_last >= 0) (*pieces)[j->x_last].next = at;
    else j->x_first = at;
    j->x_last = at;
    j->x_bytes += n;
}

/* what the connection goes over, copied to the job it carries */
static void net_route_of(const net_conn *c, net_job *j) {
    j->rt.tls = c->rt.tls;
    j->rt.resumed = c->rt.resumed;
    snprintf(j->rt.version, sizeof j->rt.version, "%s", c->rt.version);
    snprintf(j->rt.cipher, sizeof j->rt.cipher, "%s", c->rt.cipher);
    snprintf(j->rt.alpn, sizeof j->rt.alpn, "%s", c->rt.alpn);
    j->rt.proxied = c->rt.proxied;
    snprintf(j->rt.proxy, sizeof j->rt.proxy, "%s", c->rt.proxy);
    j->x_number = c->number;
    j->x_h2 = c->h2;
}

/* an exchange failed: its outcome and its words */
static void net_job_fail(net_job *j, net_outcome o, const char *why) {
    j->x_out = o;
    snprintf(j->rt.reason, sizeof j->rt.reason, "%s", why);
    j->x_state = X_DONE;
}

/* the response of the HTTP/1.1 connection's job, read as far as it has come: when whole,
   parsed (net_parse_response) into the job, the connection idle again when it may carry
   another; closed, the connection closed under it. A request on a connection used before
   that closed before a byte of its answer came goes once again, on a new connection */
static void net_h1_step(int i, vector<net_job> *jobs, vector<char> *arena, vector<net_piece> *pieces, int closed) {
    net_conn *c = &net_pool[i];
    int k = c->busy;
    if (k < 0) {
        if (closed) net_conn_close(i);
        return;
    }
    net_job *j = &(*jobs)[k];
    if (len(c->in)) j->x_got = 1;
    int keep = 0;
    size_t end = net_h1_end((const char *)data(c->in), len(c->in), j->x_head_only, &keep);
    if (!end && !closed) {
        if (len(c->in) > NET_MAX_BODY + 65536) {
            net_job_fail(j, NET_OUTCOME_TOO_LARGE, "the response passed 32 MB");
            net_conn_close(i);
        }
        return;
    }
    if (!end) {
        if (!j->x_got && j->x_reused && j->x_retries == 0) {
            j->x_retries++;
            j->x_fresh = 1;
            j->x_state = X_WAIT;
            j->x_conn = -1;
            net_conn_close(i);
            return;
        }
        end = len(c->in);
        keep = 0;
    }
    vector<char> head, body;
    int status = 0;
    j->x_out = net_parse_response((const char *)data(c->in), end, &status, &head, &body, j->rt.reason, sizeof j->rt.reason);
    j->x_status = status;
    if (status == 421 && !j->x_421 && j->x_out == NET_OUTCOME_OK) {
        /* asked once again, its connection not kept (Chromium closes it to resend) */
        net_job_misdirected(j);
        c->busy = -1;
        net_conn_close(i);
        return;
    }
    j->x_head = len(*arena);
    j->x_head_n = len(head);
    append_range(arena, data(head), len(head));
    if (len(body)) net_piece_add(j, pieces, arena, data(body), len(body));
    j->x_state = X_DONE;
    c->served++;
    c->busy = -1;
    c->touched = net_ms();
    if (!keep || closed || end < len(c->in)) net_conn_close(i);
    else clear(c->in);
}

/* ---- HTTP/2 ---- */

/* HTTP's reason phrases (RFC 9110, 15), for a status an HTTP/2 response gives alone */
static const int net_phrase_code[47] = { 200, 201, 202, 203, 204, 205, 206, 300, 301, 302, 303, 304, 305, 307, 308, 400, 401, 402, 403,
                                         404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 421, 422, 425, 426, 428,
                                         429, 431, 451, 500, 501, 502, 503, 504, 505 };
static const char *const net_phrase[47] = { "OK", "Created", "Accepted", "Non-Authoritative Information", "No Content", "Reset Content",
                                            "Partial Content", "Multiple Choices", "Moved Permanently", "Found", "See Other", "Not Modified",
                                            "Use Proxy", "Temporary Redirect", "Permanent Redirect", "Bad Request", "Unauthorized",
                                            "Payment Required", "Forbidden", "Not Found", "Method Not Allowed", "Not Acceptable",
                                            "Proxy Authentication Required", "Request Timeout", "Conflict", "Gone", "Length Required",
                                            "Precondition Failed", "Content Too Large", "URI Too Long", "Unsupported Media Type",
                                            "Range Not Satisfiable", "Expectation Failed", "Misdirected Request", "Unprocessable Content",
                                            "Too Early", "Upgrade Required", "Precondition Required", "Too Many Requests",
                                            "Request Header Fields Too Large", "Unavailable For Legal Reasons", "Internal Server Error",
                                            "Not Implemented", "Bad Gateway", "Service Unavailable", "Gateway Timeout",
                                            "HTTP Version Not Supported" };

/* a status and its reason phrase, as an HTTP/1.1 status line carries them */
static void net_status_words(int status, char *out, size_t cap) {
    for (size_t k = 0; k < 47; k++)
        if (net_phrase_code[k] == status) {
            snprintf(out, cap, "%d %s", status, net_phrase[k]);
            return;
        }
    snprintf(out, cap, "%d", status);
}

/* a job's request as HTTP/2 fields (8.3.1): the pseudo-header fields as Chromium orders
   them, then its header lines but the connection's own, lowercased, its Cookie in crumbs
   (as Chromium's encoder splits one), then priority (RFC 9218) */
static void net_h2_fields(const net_job *j, const vector<char> *arena, vector<hpack_field> *fs, vector<uint8_t> *fb) {
    static const char *const pseudo[4] = { ":method", ":authority", ":scheme", ":path" };
    for (int k = 0; k < 4; k++) {
        hpack_field f = { (uint32_t)len(*fb), (uint32_t)strlen(pseudo[k]), 0, 0, 0 };
        append_range(fb, pseudo[k], strlen(pseudo[k]));
        f.value = (uint32_t)len(*fb);
        if (k == 0) append_range(fb, j->meth, strlen(j->meth));
        else if (k == 1) append_range(fb, j->x_authority, strlen(j->x_authority));
        else if (k == 2) append_range(fb, "https", 5);
        else append_range(fb, data(*arena) + j->x_target, j->x_target_n);
        f.vlen = (uint32_t)(len(*fb) - f.value);
        push_back(fs, f);
    }
    const char *s = data(*arena) + j->x_lines;
    size_t n = j->x_lines_n, i = 0;
    static const char *const skip[7] = { "host", "connection", "proxy-connection", "keep-alive", "transfer-encoding", "upgrade",
                                         "proxy-authorization" };
    while (i < n) {
        size_t e = i;
        while (e < n && s[e] != '\n') e++;
        size_t c = i;
        while (c < e && s[c] != ':') c++;
        size_t va = c + 1, vb = e;
        while (va < vb && net_wsp(s[va])) va++;
        while (vb > va && (s[vb - 1] == '\r' || net_wsp(s[vb - 1]))) vb--;
        int drop = c >= e;
        for (int k = 0; k < 7 && !drop; k++) drop = net_ci_is(s + i, c - i, skip[k]);
        if (!drop) {
            int cookie = net_ci_is(s + i, c - i, "cookie");
            size_t a = va;
            for (;;) {
                size_t b = vb;
                if (cookie) {
                    b = a;
                    while (b < vb && s[b] != ';') b++;
                }
                hpack_field f = { (uint32_t)len(*fb), (uint32_t)(c - i), 0, 0, 0 };
                for (size_t q = i; q < c; q++) push_back(fb, (uint8_t)net_lower(s[q]));
                f.value = (uint32_t)len(*fb);
                append_range(fb, s + a, b - a);
                f.vlen = (uint32_t)(b - a);
                push_back(fs, f);
                if (b >= vb) break;
                a = b + 1;
                if (a < vb && s[a] == ' ') a++;
            }
        }
        i = e + 1;
    }
    /* priority: its urgency and incremental flag, none when both are RFC 9218's defaults */
    char pv[16];
    pv[0] = 0;
    if (j->priority != 3) snprintf(pv, sizeof pv, "u=%u%s", j->priority, j->incremental ? ", i" : "");
    else if (j->incremental) snprintf(pv, sizeof pv, "i");
    if (pv[0]) {
        hpack_field f = { (uint32_t)len(*fb), 8, 0, (uint32_t)strlen(pv), 0 };
        append_range(fb, "priority", 8);
        f.value = (uint32_t)len(*fb);
        append_range(fb, pv, strlen(pv));
        push_back(fs, f);
    }
}

/* the job carried by stream id of the HTTP/2 connection i, or -1 */
static int net_job_of_stream(const vector<net_job> *jobs, int i, uint32_t id) {
    for (size_t k = 0; k < len(*jobs); k++)
        if ((*jobs)[k].state == J_NET && (*jobs)[k].x_state == X_ON && (*jobs)[k].x_conn == i && (*jobs)[k].x_stream == id) return (int)k;
    return -1;
}

/* a job's request body sent on its stream as far as the windows allow */
static void net_h2_body(int i, net_job *j, const vector<char> *arena) {
    if (j->x_body_sent >= j->sent_n || j->x_state != X_ON) return;
    j->x_body_sent += h2_data(&net_pool[i].h, j->x_stream, (const uint8_t *)data(*arena) + j->sent + j->x_body_sent, j->sent_n - j->x_body_sent,
                              1, &net_pool[i].out);
}

/* a job answered 421 Misdirected Request asked once again, as Chromium asks it
   (HttpNetworkTransaction's retry with IP pooling disabled): on a connection of its own
   host, made for it if there is none; a second 421 is its answer (measured: a request on a
   connection pooled by IP is asked again on a new connection to its host; one on its own
   host's connection, again on that connection) */
static void net_job_misdirected(net_job *j) {
    j->x_421 = 1;
    j->x_nopool = 1;
    j->x_state = X_WAIT;
    j->x_conn = -1;
    j->x_stream = 0;
    j->x_body_sent = 0;
    j->x_first = -1;
    j->x_last = -1;
    j->x_bytes = 0;
    j->x_got = 0;
    j->x_status = 0;
    j->x_head = 0;
    j->x_head_n = 0;
    j->x_pooled = 0;
}

/* a job on a connection that can carry it no more: again on another, up to limit times,
   when no byte of its answer came (a request the server did not process); else failed */
static void net_job_again(net_job *j, int limit, net_outcome o, const char *why) {
    if (!j->x_got && j->x_retries < limit) {
        j->x_retries++;
        j->x_state = X_WAIT;
        j->x_conn = -1;
        j->x_stream = 0;
        j->x_body_sent = 0;
        j->x_first = -1;
        j->x_last = -1;
        j->x_bytes = 0;
        return;
    }
    net_job_fail(j, o, why);
}

/* the HTTP/2 connection's frames read as far as they have come, each to its stream's job: a
   head, data, the stream's end; a reset (REFUSED_STREAM goes again); GOAWAY (the streams it
   did not process go again, on another connection), each twice at most; a window opened
   (bodies go on); an error, or the connection closed (its jobs that got nothing go again
   once, the others fail) */
static void net_h2_step(int i, vector<net_job> *jobs, vector<char> *arena, vector<net_piece> *pieces, int closed) {
    net_conn *c = &net_pool[i];
    size_t at = 0;
    int dead = 0;
    for (;;) {
        h2_event ev;
        vector<hpack_field> fs;
        vector<uint8_t> fb;
        size_t used = 0;
        if (!h2_read(&c->h, data(c->in) + at, len(c->in) - at, &used, &ev, &fs, &fb, &c->out)) break;
        size_t base = at;
        at += used;
        int k = -1;
        if (ev.stream) k = net_job_of_stream(jobs, i, ev.stream);
        if (ev.kind == H2_EV_HEADERS && k >= 0 && ev.status == 421 && !(*jobs)[k].x_421) {
            if (!ev.end_stream) h2_reset(&c->h, ev.stream, H2_CANCEL, &c->out);
            net_job_misdirected(&(*jobs)[k]);
        } else if (ev.kind == H2_EV_HEADERS && k >= 0) {
            net_job *j = &(*jobs)[k];
            j->x_got = 1;
            if (ev.status) {
                char line[64];
                j->x_status = (int)ev.status;
                j->x_head = len(*arena);
                int ln = snprintf(line, sizeof line, "HTTP/2 %u\r\n", ev.status);
                append_range(arena, line, (size_t)ln);
                for (size_t q = 0; q < len(fs); q++) {
                    append_range(arena, (const char *)data(fb) + fs[q].name, fs[q].nlen);
                    append_range(arena, ": ", 2);
                    append_range(arena, (const char *)data(fb) + fs[q].value, fs[q].vlen);
                    append_range(arena, "\r\n", 2);
                }
                append_range(arena, "\r\n", 2);
                j->x_head_n = len(*arena) - j->x_head;
                net_status_words((int)ev.status, j->rt.reason, sizeof j->rt.reason);
            }
            if (ev.end_stream) {
                j->x_out = NET_OUTCOME_OK;
                j->x_state = X_DONE;
            }
        } else if (ev.kind == H2_EV_DATA && k >= 0) {
            net_job *j = &(*jobs)[k];
            j->x_got = 1;
            net_piece_add(j, pieces, arena, (const char *)data(c->in) + base + ev.off, ev.len);
            if (j->x_bytes > NET_MAX_BODY) {
                h2_reset(&c->h, ev.stream, H2_CANCEL, &c->out);
                net_job_fail(j, NET_OUTCOME_TOO_LARGE, "the response passed 32 MB");
            } else if (ev.end_stream) {
                j->x_out = NET_OUTCOME_OK;
                j->x_state = X_DONE;
            }
        } else if (ev.kind == H2_EV_RESET && k >= 0) {
            net_job_again(&(*jobs)[k], ev.code == H2_REFUSED_STREAM ? NET_RETRIES : 0, NET_OUTCOME_RESET, ev.why);
        } else if (ev.kind == H2_EV_GOAWAY) {
            for (size_t q = 0; q < len(*jobs); q++) {
                net_job *j = &(*jobs)[q];
                if (j->state == J_NET && j->x_state == X_ON && j->x_conn == i && j->x_stream > ev.last) {
                    j->x_got = 0;
                    net_job_again(j, NET_RETRIES, NET_OUTCOME_RESET, ev.why);
                }
            }
        } else if (ev.kind == H2_EV_WINDOW) {
            for (size_t q = 0; q < len(*jobs); q++)
                if ((*jobs)[q].state == J_NET && (*jobs)[q].x_conn == i) net_h2_body(i, &(*jobs)[q], arena);
        } else if (ev.kind == H2_EV_ERROR) {
            for (size_t q = 0; q < len(*jobs); q++) {
                net_job *j = &(*jobs)[q];
                if (j->state == J_NET && j->x_state == X_ON && j->x_conn == i) net_job_again(j, 1, NET_OUTCOME_RESET, ev.why);
            }
            dead = 1;
            break;
        }
    }
    size_t left = len(c->in) - at;
    memmove(data(c->in), data(c->in) + at, left);
    resize(c->in, left);
    if (dead) net_conn_write(i);
    /* a connection gone away carries nothing more once its streams are done */
    if (!closed && !dead && c->h.goaway && h2_open_streams(&c->h) == 0) {
        net_conn_write(i);
        dead = 1;
    }
    if (closed || dead) {
        for (size_t q = 0; q < len(*jobs); q++) {
            net_job *j = &(*jobs)[q];
            if (j->state == J_NET && j->x_state == X_ON && j->x_conn == i)
                net_job_again(j, 1, NET_OUTCOME_RESET, "the server closed the connection before the response ended");
        }
        net_conn_close(i);
    }
}

/* ---- the connections' loop ---- */

/* a connection that could not be made: the job it was made for failed (with the proxy it
   went through), the slot freed */
static void net_conn_fail(int i, vector<net_job> *jobs, net_outcome o) {
    net_conn *c = &net_pool[i];
    if (c->busy >= 0) {
        net_job *j = &(*jobs)[c->busy];
        j->rt.proxied = c->rt.proxied;
        snprintf(j->rt.proxy, sizeof j->rt.proxy, "%s", c->rt.proxy);
        net_job_fail(j, o, c->rt.reason);
    }
    net_conn_close(i);
}

/* a request sent on a ready connection: HTTP/1.1's whole, or a new HTTP/2 stream of it */
static void net_attach(int i, int k, vector<net_job> *jobs, vector<char> *arena) {
    net_conn *c = &net_pool[i];
    net_job *j = &(*jobs)[k];
    j->x_conn = i;
    j->x_state = X_ON;
    j->x_got = 0;
    j->x_fresh = 0;
    j->x_pooled = 0;
    if (c->h2) {
        vector<hpack_field> fs;
        vector<uint8_t> fb;
        net_h2_fields(j, arena, &fs, &fb);
        j->x_reused = c->h.next_id > 1;
        j->x_stream = h2_request(&c->h, data(fs), len(fs), data(fb), j->sent_n == 0, j->priority, &c->out);
        j->x_body_sent = 0;
        net_h2_body(i, j, arena);
    } else {
        j->x_reused = c->served > 0;
        c->busy = k;
        append_range(c->out, data(*arena) + j->x_req, j->x_req_n);
    }
    net_route_of(c, j);
    c->touched = net_ms();
}

/* ---- HTTP/2's pooling by IP ---- */

/* two socket addresses the same: their family, address and port */
static int net_same_address(const struct sockaddr_storage *a, socklen_t an, const struct sockaddr_storage *b, socklen_t bn) {
    if (a->ss_family != b->ss_family || an != bn) return 0;
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
        return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a, *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port && memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
    return 0;
}

/* the TLS connection's certificate (verified in its handshake) covers the host: a DNS name
   of it, a wildcard a whole first label, or an address of it, as Chromium's VerifyNameMatch
   holds a name to a certificate */
static int net_cert_covers(int i, const char *host) {
    if (!net_pool_ssl[i]) return 0;
    own X509 *x = SSL_get1_peer_certificate(net_pool_ssl[i]);
    if (!x) return 0;
    int ok;
    if (net_is_address(host)) ok = X509_check_ip_asc(x, host, 0) == 1;
    else ok = X509_check_host(x, host, strlen(host), NET_X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS | NET_X509_CHECK_FLAG_NEVER_CHECK_SUBJECT, 0) == 1;
    X509_free(x);
    return ok;
}

/* an HTTP/2 connection made for another host that the job may ride (Chromium's pooling by
   IP, SpdySessionPool's aliases): made directly, with no proxy between (through one, the
   address is the proxy's), its session up and not going away, to an address and port the
   job's host resolves to, its certificate covering the job's host. Measured against node's
   servers: a host of the certificate at the connection's address and port rides it, a name
   or an address alike; one the certificate does not cover, one at another address or
   another port, and every one through a proxy each get a connection of their own. -1 when
   none */
static int net_pool_by_ip(const net_job *j) {
    for (int i = 0; i < NET_POOL; i++) {
        const net_conn *c = &net_pool[i];
        if (c->state != NC_READY || !c->h2 || !c->tls || c->rt.proxied || c->h.goaway || c->h.dead || c->peer < 0) continue;
        int same = 0;
        for (int a = 0; a < j->x_naddr && !same; a++) same = net_same_address(&j->x_addr[a], j->x_alen[a], &c->addr[c->peer], c->alen[c->peer]);
        if (same && net_cert_covers(i, j->x_host)) return i;
    }
    return -1;
}

/* the jobs waiting for a connection given one, in order: an HTTP/2 connection of their key
   with room for a stream; else an idle HTTP/1.1 one still open; else, when the key has no
   connection at all, an HTTP/2 one made for another host that it may ride (pooling by IP,
   not after a 421 Misdirected Request); else a new one, while fewer than six are open for
   the key (one only while a TLS connection's protocol is unknown: an HTTP/2 one would carry
   them all), and none is being made for the job already (as Chromium's pool starts one
   connect job a request) */
static void net_assign(vector<net_job> *jobs, vector<char> *arena) {
    for (size_t k = 0; k < len(*jobs); k++) {
        if ((*jobs)[k].state != J_NET || (*jobs)[k].x_state != X_WAIT) continue;
        int found = -1, pending = 0, count = 0, waiting_room = 0, known_h1 = 0, making = 0;
        for (int i = 0; i < NET_POOL && found < 0; i++) {
            net_conn *c = &net_pool[i];
            if (c->state == NC_FREE || strcmp(c->key, (*jobs)[k].x_key) != 0) continue;
            if (c->state != NC_READY) {
                count++;
                pending = 1;
                if (c->busy == (int)k) making = 1;
                continue;
            }
            if (c->h2) {
                if (h2_can_open(&c->h)) found = i;
                else if (!c->h.goaway && !c->h.dead) {
                    count++;
                    waiting_room = 1;
                }
                continue;
            }
            count++;
            known_h1 = 1;
            if (c->busy >= 0 || (*jobs)[k].x_fresh) continue;
            if (!net_conn_alive(i)) {
                net_conn_close(i);
                count--;
                continue;
            }
            found = i;
        }
        if (found >= 0) {
            net_attach(found, (int)k, jobs, arena);
            continue;
        }
        if (count == 0 && (*jobs)[k].x_https && !(*jobs)[k].x_via && !(*jobs)[k].x_nopool) {
            if (!net_job_resolve(&(*jobs)[k])) {
                (*jobs)[k].x_state = X_DONE;
                continue;
            }
            int p = net_pool_by_ip(&(*jobs)[k]);
            if (p >= 0) {
                /* it rides that connection, or waits for room on it */
                if (h2_can_open(&net_pool[p].h)) {
                    net_attach(p, (int)k, jobs, arena);
                    (*jobs)[k].x_pooled = 1;
                }
                continue;
            }
        }
        if (making || waiting_room || (pending && (*jobs)[k].x_https && !known_h1) || count >= NET_PER_ORIGIN) continue;
        int i = net_conn_open(&(*jobs)[k]);
        if (i < 0) {
            (*jobs)[k].x_state = X_DONE;
            continue;
        }
        net_pool[i].busy = (int)k;
    }
}

/* a connection whose setup is done made ready: HTTP/2 begun (its waiting jobs become
   streams at the next assignment), or HTTP/1.1's request sent for the job it was made for */
static void net_conn_ready(int i, vector<net_job> *jobs, vector<char> *arena) {
    net_conn *c = &net_pool[i];
    c->state = NC_READY;
    c->touched = net_ms();
    int k = c->busy;
    c->busy = -1;
    if (c->h2) {
        h2_start(&c->h, &c->out);
        return;
    }
    if (k >= 0 && (*jobs)[k].x_state == X_WAIT) net_attach(i, k, jobs, arena);
}

/* a connection taken a step on what poll said of its socket */
static void net_conn_step(int i, vector<net_job> *jobs, vector<char> *arena, vector<net_piece> *pieces, net_tls *t) {
    net_conn *c = &net_pool[i];
    if (c->state == NC_CONNECT) {
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) {
            close(c->fd);
            c->err = err;
            if (!net_dial_next(i)) {
                if (c->rt.proxied) snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy port %d: %s", c->port, strerror(err));
                else snprintf(c->rt.reason, sizeof c->rt.reason, "%s port %d: %s", c->host, c->port, strerror(err));
                net_conn_fail(i, jobs, NET_OUTCOME_REFUSED);
            }
            return;
        }
        c->peer = c->at - 1;
        c->touched = net_ms();
        c->began = c->touched;
        if (c->target[0]) {
            char req[1400];
            int n = snprintf(req, sizeof req, "CONNECT %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Ambitious/0.1\r\n%s%s%s\r\n", c->target, c->target,
                             c->auth[0] ? "Proxy-Authorization: Basic " : "", c->auth, c->auth[0] ? "\r\n" : "");
            if (n > 0 && (size_t)n < sizeof req) append_range(c->out, req, (size_t)n);
            c->state = NC_TUNNEL;
        } else if (c->tls) {
            if (!net_tls_begin(i, t)) {
                net_conn_fail(i, jobs, NET_OUTCOME_TLS_HANDSHAKE_FAILED);
                return;
            }
            c->state = NC_HANDSHAKE;
        } else {
            net_conn_ready(i, jobs, arena);
            net_conn_write(i);
            return;
        }
    }
    if (c->state == NC_TUNNEL) {
        int w = net_conn_write(i), r = net_conn_read(i);
        size_t n = len(c->in), end = net_head_end((const char *)data(c->in), n, 0);
        if (w < 0 || (!end && r <= 0)) {
            if (n) snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy gave no HTTP answer to CONNECT");
            else snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy closed the connection");
            net_conn_fail(i, jobs, NET_OUTCOME_PROXY_REFUSED);
            return;
        }
        if (!end) return;
        /* its status line copied out: a pointer into the pool's buffers is not kept */
        size_t le = 0;
        while (le < end && le < 299 && c->in[le] != '\r' && c->in[le] != '\n') le++;
        char line[300];
        snprintf(line, sizeof line, "%.*s", (int)le, (const char *)data(c->in));
        if (end < 12 || strncmp(line, "HTTP/1.", 7) != 0 || line[9] != '2') {
            if (end >= 12 && strncmp(line, "HTTP/1.", 7) == 0) snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy answered %s", line);
            else snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy gave no HTTP answer to CONNECT");
            net_conn_fail(i, jobs, NET_OUTCOME_PROXY_REFUSED);
            return;
        }
        clear(c->in);
        if (!net_tls_begin(i, t)) {
            net_conn_fail(i, jobs, NET_OUTCOME_TLS_HANDSHAKE_FAILED);
            return;
        }
        c->state = NC_HANDSHAKE;
        c->began = net_ms();
    }
    if (c->state == NC_HANDSHAKE) {
        net_outcome o = NET_OUTCOME_OK;
        int r = net_tls_step(i, &o);
        if (r < 0) {
            net_conn_fail(i, jobs, o);
            return;
        }
        c->touched = net_ms();
        if (r > 0) {
            net_conn_ready(i, jobs, arena);
            net_conn_write(i);
        }
        return;
    }
    if (c->state != NC_READY) return;
    int w = net_conn_write(i);
    int r = net_conn_read(i);
    if (c->h2) net_h2_step(i, jobs, arena, pieces, w < 0 || r <= 0);
    else net_h1_step(i, jobs, arena, pieces, w < 0 || r <= 0);
    if (net_pool[i].state == NC_READY && len(net_pool[i].out)) net_conn_write(i);
}

/* whether a connection has work: its setup, a job on it, bytes to send */
static int net_conn_working(int i, const vector<net_job> *jobs) {
    const net_conn *c = &net_pool[i];
    if (c->state == NC_FREE) return 0;
    if (c->state != NC_READY || len(c->out) || c->busy >= 0) return 1;
    for (size_t k = 0; k < len(*jobs); k++)
        if ((*jobs)[k].state == J_NET && (*jobs)[k].x_state == X_ON && (*jobs)[k].x_conn == i) return 1;
    return 0;
}

/* AMB_NET_TIMEOUT: the longest any of a connection's waits may last, in ms, for tests that
   would not wait Chromium's; 0 (unset) for none */
static int64_t net_wait_cap(void) {
    const char *v = getenv("AMB_NET_TIMEOUT");
    long long ms = v && *v ? atoll(v) : 0;
    return ms > 0 ? (int64_t)ms : 0;
}

/* how long a connection's present wait may last: its connect's, tunnel's or handshake's
   (from when it began) Chromium's, an answer's (from the last byte) for ever; each within
   AMB_NET_TIMEOUT. -1 for ever */
static int64_t net_conn_limit(int i) {
    int s = net_pool[i].state;
    int64_t cap = net_wait_cap(), d = s == NC_CONNECT ? NET_CONNECT_MS : s == NC_TUNNEL ? NET_TUNNEL_MS : s == NC_HANDSHAKE ? NET_HANDSHAKE_MS : -1;
    if (cap && (d < 0 || cap < d)) d = cap;
    return d;
}
/* when that wait gives out, by the clock of net_ms; -1 never */
static int64_t net_conn_deadline(int i) {
    int64_t d = net_conn_limit(i);
    if (d < 0) return -1;
    return (net_pool[i].state == NC_READY ? net_pool[i].touched : net_pool[i].began) + d;
}

/* a wait's length in words */
static void net_wait_words(int64_t ms, char *out, size_t cap) {
    if (ms % 1000 == 0) snprintf(out, cap, "%lld second%s", (long long)(ms / 1000), ms == 1000 ? "" : "s");
    else snprintf(out, cap, "%.1f seconds", (double)ms / 1000);
}

/* a connection whose wait gave out: its jobs failed, it closed */
static void net_conn_timeout(int i, vector<net_job> *jobs) {
    net_conn *c = &net_pool[i];
    char w[48];
    net_wait_words(net_conn_limit(i), w, sizeof w);
    if (c->state != NC_READY) {
        net_outcome o = NET_OUTCOME_REFUSED;
        if (c->state == NC_HANDSHAKE) {
            o = NET_OUTCOME_TLS_HANDSHAKE_FAILED;
            snprintf(c->rt.reason, sizeof c->rt.reason, "%s port %d: no TLS handshake in %s", c->host, c->tls_port, w);
        } else if (c->state == NC_TUNNEL) {
            o = NET_OUTCOME_PROXY_REFUSED;
            snprintf(c->rt.reason, sizeof c->rt.reason, "the proxy did not answer CONNECT in %s", w);
        } else snprintf(c->rt.reason, sizeof c->rt.reason, "%s port %d: no connection in %s", c->host, c->port, w);
        net_conn_fail(i, jobs, o);
        return;
    }
    char why[120];
    for (size_t k = 0; k < len(*jobs); k++) {
        net_job *j = &(*jobs)[k];
        if (j->state == J_NET && j->x_state == X_ON && j->x_conn == i) {
            snprintf(why, sizeof why, j->x_got ? "the response stopped for %s" : "the server sent nothing for %s", w);
            net_job_fail(j, NET_OUTCOME_MALFORMED, why);
        }
    }
    net_conn_close(i);
}

/* the batch's exchanges carried until each is answered or failed: the connections polled,
   each taken a step when its socket is ready, given up when its wait gave out */
static void net_run(vector<net_job> *jobs, vector<char> *arena, vector<net_piece> *pieces, net_tls *t) {
    for (;;) {
        net_assign(jobs, arena);
        int live = 0;
        for (size_t k = 0; k < len(*jobs) && !live; k++) live = (*jobs)[k].state == J_NET && (*jobs)[k].x_state != X_DONE;
        if (!live) return;
        struct pollfd fds[NET_POOL];
        int which[NET_POOL], nf = 0;
        int64_t now = net_ms(), wait = -1;
        for (int i = 0; i < NET_POOL; i++) {
            if (!net_conn_working(i, jobs)) continue;
            net_conn *c = &net_pool[i];
            fds[nf].fd = c->fd;
            fds[nf].events = POLLIN;
            if (c->state == NC_CONNECT || len(c->out) || c->want_write) fds[nf].events |= POLLOUT;
            fds[nf].revents = 0;
            which[nf++] = i;
            int64_t dl = net_conn_deadline(i);
            if (dl >= 0 && (wait < 0 || dl - now < wait)) wait = dl - now < 0 ? 0 : dl - now;
        }
        if (nf == 0) {
            /* jobs that wait for a connection no one can make (every slot busy) */
            for (size_t k = 0; k < len(*jobs); k++)
                if ((*jobs)[k].state == J_NET && (*jobs)[k].x_state == X_WAIT) net_job_fail(&(*jobs)[k], NET_OUTCOME_REFUSED, "no connection could carry it");
            return;
        }
        /* no deadline: poll waits until a socket is ready, as long as that takes */
        if (wait > 2000000000) wait = 2000000000;
        poll(fds, (nfds_t)nf, (int)wait);
        now = net_ms();
        for (int f = 0; f < nf; f++) {
            int i = which[f];
            if (net_pool[i].state == NC_FREE) continue;
            int64_t dl = net_conn_deadline(i);
            if (fds[f].revents) net_conn_step(i, jobs, arena, pieces, t);
            else if (dl >= 0 && now >= dl) net_conn_timeout(i, jobs);
        }
    }
}

/* ---- the request a fetch sends ---- */

/* what each kind of request accepts: Chromium's values, but for the image formats the
   painter cannot draw (AVIF, SVG) and signed exchanges, which are not offered */
static const char *const net_accepts[5] = { "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
                                            "text/css,*/*;q=0.1", "image/webp,image/apng,image/*,*/*;q=0.8", "*/*", "*/*" };
static const char *const net_dests[5] = { "document", "style", "image", "script", "empty" };

/* the job's request, as Chromium writes one over HTTP/1.1 (its fields' order and case,
   measured against a server that logs them: tests/net/h2.chromium): Host and Connection
   (Proxy-Connection to an HTTP proxy), then the fields of a navigation (a GET, or a form's
   POST with its Content-Length, Cache-Control and Origin) or of a part or a script's
   request (its own fields after Connection, its Content-Type where Chromium puts it), the
   client hints and fetch metadata to a potentially trustworthy URL alone, the Referer the
   default policy gives, Accept-Encoding (br and zstd to a potentially trustworthy URL
   alone), the Cookie, then the body. Into the arena, its places in the job: the whole, the
   lines after Connection's (HTTP/2's fields), the target (its :path) */
static void net_request_build(net_job *j, const net_url *page, const char *cookie, size_t cookie_n, const char *valid, size_t valid_n,
                              vector<char> *arena) {
    const net_url *u = &j->u;
    int trust = net_trustworthy(u), site = net_fetch_site(u, page), kind = (int)j->kind;
    int post = strcmp(j->meth, "GET") != 0 && strcmp(j->meth, "HEAD") != 0;
    if (kind > 4) kind = 4;
    vector<char> ex, req;
    append_range(ex, data(*arena) + j->hdrs, j->hdrs_n);
    char referrer[8400], plat[80], origin[512], v[64];
    net_referrer(u, page, referrer, sizeof referrer);
    char platform[64];
    net_platform(platform, sizeof platform);
    snprintf(plat, sizeof plat, "\"%s\"", platform);
    snprintf(origin, sizeof origin, "null");
    if (page) net_origin_text(page, origin, sizeof origin);
    /* the request line: through an HTTP proxy, an http: request names its whole URL */
    char whole[340];
    whole[0] = 0;
    if (j->x_via && !j->x_https) snprintf(whole, sizeof whole, "%s://%s", u->scheme, j->x_authority);
    append_range(req, j->meth, strlen(j->meth));
    push_back(req, ' ');
    size_t target = len(req);
    append_range(req, whole, strlen(whole));
    size_t path = len(req);
    append_range(req, u->path, strlen(u->path));
    if (u->has_query) {
        push_back(req, '?');
        append_range(req, u->query, strlen(u->query));
    }
    size_t target_end = len(req);
    append_range(req, " HTTP/1.1\r\n", 11);
    net_line(&req, "Host", j->x_authority);
    if (j->x_via && !j->x_https) net_line(&req, "Proxy-Connection", "keep-alive");
    else net_line(&req, "Connection", "keep-alive");
    size_t lines = len(req);
    int accept = !net_has_line(data(ex), len(ex), "accept");
    if (j->sent_n || post) {
        snprintf(v, sizeof v, "%zu", j->sent_n);
        net_line(&req, "Content-Length", v);
    }
    if (j->nav) {
        if (post) net_line(&req, "Cache-Control", "max-age=0");
        if (trust) {
            net_line(&req, "sec-ch-ua", "\"Ambitious\";v=\"0\"");
            net_line(&req, "sec-ch-ua-mobile", "?0");
            net_line(&req, "sec-ch-ua-platform", plat);
        }
        net_line(&req, "Upgrade-Insecure-Requests", "1");
        net_lines_named(&req, data(ex), len(ex), "content-type", 1);
        net_line(&req, "User-Agent", "Ambitious/0.1");
        if (post) net_line(&req, "Origin", origin);
        if (accept) net_line(&req, "Accept", net_accepts[0]);
        append_range(req, valid, valid_n);
        if (trust) {
            net_line(&req, "Sec-Fetch-Site", net_sites[site]);
            net_line(&req, "Sec-Fetch-Mode", "navigate");
            if (!page) net_line(&req, "Sec-Fetch-User", "?1");
            net_line(&req, "Sec-Fetch-Dest", "document");
        }
    } else {
        net_lines_named(&req, data(ex), len(ex), "content-type", 0);
        if (trust) net_line(&req, "sec-ch-ua-platform", plat);
        net_line(&req, "User-Agent", "Ambitious/0.1");
        if (trust) net_line(&req, "sec-ch-ua", "\"Ambitious\";v=\"0\"");
        net_lines_named(&req, data(ex), len(ex), "content-type", 1);
        if (trust) net_line(&req, "sec-ch-ua-mobile", "?0");
        if (accept) net_line(&req, "Accept", net_accepts[kind]);
        append_range(req, valid, valid_n);
        if (post) net_line(&req, "Origin", origin);
        if (trust) {
            net_line(&req, "Sec-Fetch-Site", net_sites[site]);
            net_line(&req, "Sec-Fetch-Mode", j->kind == NET_KIND_OTHER ? "cors" : "no-cors");
            net_line(&req, "Sec-Fetch-Dest", net_dests[kind]);
        }
    }
    if (referrer[0]) net_line(&req, "Referer", referrer);
    /* with a dictionary offered, dcb and dcz too, and the dictionary's name and id after the
       Cookie (Chromium's SharedDictionaryNetworkTransaction adds them below the job that sets
       it; measured over HTTP/2: after accept-encoding, before priority) */
    net_line(&req, "Accept-Encoding", !trust ? "gzip, deflate" : j->dict ? "gzip, deflate, br, zstd, dcb, dcz" : "gzip, deflate, br, zstd");
    append_range(req, cookie, cookie_n);
    if (j->dict) {
        net_line(&req, "Available-Dictionary", j->dict_sf);
        if (j->dict_id[0]) {
            char idv[1100];
            vector<char> q;
            push_back(q, '"');
            for (size_t i = 0; j->dict_id[i]; i++) {
                if (j->dict_id[i] == '"' || j->dict_id[i] == '\\') push_back(q, '\\');
                push_back(q, j->dict_id[i]);
            }
            push_back(q, '"');
            snprintf(idv, sizeof idv, "%.*s", (int)len(q), data(q));
            net_line(&req, "Dictionary-ID", idv);
        }
    }
    if (j->nav) net_lines_named(&req, data(ex), len(ex), "content-type", 0);
    if (j->x_via && !j->x_https && j->x_px.auth[0]) {
        char pa[800];
        snprintf(pa, sizeof pa, "Basic %s", j->x_px.auth);
        net_line(&req, "Proxy-Authorization", pa);
    }
    size_t lines_end = len(req);
    append_range(req, "\r\n", 2);
    /* the body after the head, copied out of the arena first: appending grows it */
    append_range(req, data(*arena) + j->sent, j->sent_n);
    size_t base = len(*arena);
    append_range(arena, data(req), len(req));
    j->x_req = base;
    j->x_req_n = len(req);
    j->x_lines = base + lines;
    j->x_lines_n = lines_end - lines;
    j->x_target = base + path;
    j->x_target_n = target_end - path;
    (void)target;
}

/* ---- a fetch's hops ---- */

/* its next hop begun: a file: URL a page served over the network may not read refused; a
   file: or data: URL read; a scheme not fetched here refused; a fresh copy in the cache
   taken, no request made -- each the fetch's end. Else its request made (its Cookie, a
   stale copy's validators) for its exchange: 1 */
static int net_job_prepare(net_loader *l, net_job *j, const net_url *page, vector<char> *arena) {
    memset(&j->rt, 0, sizeof j->rt);
    j->body_n = 0;
    j->state = J_DONE;
    j->x_number = 0;
    j->x_stream = 0;
    j->x_reused = 0;
    j->x_h2 = 0;
    const net_url *u = &j->u;
    if (page && strcmp(u->scheme, "file") == 0 && strcmp(page->scheme, "file") != 0 && !page->opaque) {
        j->out = NET_OUTCOME_BLOCKED;
        j->status = 0;
        snprintf(j->rt.reason, sizeof j->rt.reason, "a page served over %s: may not read a file: URL", page->scheme);
        return 0;
    }
    if (strcmp(u->scheme, "file") == 0 || strcmp(u->scheme, "data") == 0) {
        vector<char> body;
        int file = strcmp(u->scheme, "file") == 0;
        if (file) j->out = net_get_file(u, &body, j->type, sizeof j->type, j->rt.reason, sizeof j->rt.reason);
        else j->out = net_get_data(u, &body, j->type, sizeof j->type, j->rt.reason, sizeof j->rt.reason);
        if (file) j->status = j->out == NET_OUTCOME_OK ? 200 : 404;
        else j->status = j->out == NET_OUTCOME_OK ? 200 : 0;
        j->body = len(*arena);
        j->body_n = len(body);
        append_range(arena, data(body), len(body));
        return 0;
    }
    if (strcmp(u->scheme, "http") != 0 && strcmp(u->scheme, "https") != 0) {
        j->out = NET_OUTCOME_UNSUPPORTED_SCHEME;
        j->status = 0;
        snprintf(j->rt.reason, sizeof j->rt.reason, "%s: is not a scheme fetched here", u->scheme);
        return 0;
    }
    /* the cache: a fresh copy answers, and no request is made; a stale one with a
       validator asks the server whether it still holds */
    net_url_text(u, j->here, sizeof j->here);
    j->now = net_now();
    vector<char> c_head, c_body;
    j->caching = strcmp(j->meth, "GET") == 0 && net_cache_path(l, j->here, j->cpath, sizeof j->cpath);
    j->cached = j->caching && net_cache_read(j->cpath, j->here, &j->c_status, &j->c_rtime, &c_head, &c_body);
    if (j->cached) {
        j->c_head = len(*arena);
        j->c_head_n = len(c_head);
        append_range(arena, data(c_head), len(c_head));
        j->c_body = len(*arena);
        j->c_body_n = len(c_body);
        append_range(arena, data(c_body), len(c_body));
        j->c_age = net_initial_age(data(c_head), len(c_head), j->c_rtime) + (j->now > j->c_rtime ? j->now - j->c_rtime : 0);
        j->c_life = net_lifetime(data(c_head), len(c_head), j->c_rtime);
        if (j->c_life > j->c_age) {
            j->body = j->c_body;
            j->body_n = j->c_body_n;
            j->status = j->c_status;
            j->out = NET_OUTCOME_OK;
            net_type_of_head(data(c_head), len(c_head), j->type, sizeof j->type, j->charset, sizeof j->charset);
            j->cache = NET_CACHE_USE_FRESH;
            j->cache_age = j->c_age;
            j->cache_life = j->c_life;
            return 0;
        }
    }
    /* a navigation is same-site with itself; a page's part is when its site is the page's */
    j->same = !page || net_is_same_site(u, page);
    /* the compression dictionary it offers, if one is kept for it */
    j->dict = 0;
    j->dict_kept = 0;
    int dk = net_dict_offer(l, u, page, j->cors, j->kind, j->now);
    if (dk >= 0) {
        j->dict = 1;
        memcpy(j->dict_hash, net_dicts[dk].hash, 32);
        snprintf(j->dict_sf, sizeof j->dict_sf, "%s", net_dicts[dk].sf);
        snprintf(j->dict_id, sizeof j->dict_id, "%s", net_dicts[dk].f.id);
    }
    vector<char> cookie, valid;
    net_cookie_header(l, u, j->same, j->r, &cookie);
    if (j->cached) {
        char vv[1024], line[1100];
        if (net_header(data(c_head), len(c_head), "etag", vv, sizeof vv)) {
            int n = snprintf(line, sizeof line, "If-None-Match: %s\r\n", vv);
            if (n > 0 && (size_t)n < sizeof line) append_range(valid, line, (size_t)n);
        }
        if (net_header(data(c_head), len(c_head), "last-modified", vv, sizeof vv)) {
            int n = snprintf(line, sizeof line, "If-Modified-Since: %s\r\n", vv);
            if (n > 0 && (size_t)n < sizeof line) append_range(valid, line, (size_t)n);
        }
    }
    /* where it goes: its host and port, through the proxy the environment names */
    j->x_https = strcmp(u->scheme, "https") == 0;
    net_bare_host(u->host, j->x_host, sizeof j->x_host);
    j->x_port = u->port >= 0 ? u->port : j->x_https ? 443 : 80;
    if (u->port >= 0) snprintf(j->x_authority, sizeof j->x_authority, "%s:%d", u->host, u->port);
    else snprintf(j->x_authority, sizeof j->x_authority, "%s", u->host);
    snprintf(j->x_tunnel, sizeof j->x_tunnel, "%s:%d", u->host, j->x_port);
    int via = net_proxy_for(u, &j->x_px, j->rt.reason, sizeof j->rt.reason);
    if (via < 0) {
        j->out = NET_OUTCOME_PROXY_REFUSED;
        j->status = 0;
        return 0;
    }
    j->x_via = via;
    snprintf(j->x_key, sizeof j->x_key, "%s://%s:%d", u->scheme, u->host, j->x_port);
    if (via) {
        size_t kn = strlen(j->x_key);
        snprintf(j->x_key + kn, sizeof j->x_key - kn, " via %s %s", j->x_px.origin, j->x_px.auth);
        j->rt.proxied = 1;
        snprintf(j->rt.proxy, sizeof j->rt.proxy, "%s", j->x_px.origin);
    }
    j->x_head_only = strcmp(j->meth, "HEAD") == 0;
    net_request_build(j, page, data(cookie), len(cookie), data(valid), len(valid), arena);
    j->x_state = X_WAIT;
    j->x_conn = -1;
    j->x_resolved = 0;
    j->x_naddr = 0;
    j->x_pooled = 0;
    j->x_nopool = 0;
    j->x_421 = 0;
    j->x_retries = 0;
    j->x_fresh = 0;
    j->x_got = 0;
    j->x_body_sent = 0;
    j->x_status = 0;
    j->x_out = NET_OUTCOME_OK;
    j->x_head = 0;
    j->x_head_n = 0;
    j->x_first = -1;
    j->x_last = -1;
    j->x_bytes = 0;
    j->state = J_NET;
    return 1;
}

/* its exchange answered: the cookies its response set; a 304 that revalidates the stale
   copy; a redirect (Fetch 4.4: a 303, or a 301 or 302 of a POST, followed by a GET without
   the body), the next hop; else its body out of its content codings, an HTTP error over 400,
   and kept by the cache when it may be and a lifetime or a validator makes it of use */
static void net_job_answer(net_loader *l, net_job *j, const net_url *page, vector<char> *arena, const vector<net_piece> *pieces) {
    j->state = J_DONE;
    j->out = j->x_out;
    j->status = j->x_status;
    if (j->out != NET_OUTCOME_OK) {
        j->status = 0;
        return;
    }
    vector<char> head, body;
    append_range(head, data(*arena) + j->x_head, j->x_head_n);
    for (int32_t p = j->x_first; p >= 0; p = (*pieces)[p].next) append_range(body, data(*arena) + (*pieces)[p].off, (*pieces)[p].len);
    net_cookies_from(l, &j->u, data(head), len(head), j->same, j->r);
    if (j->status == 304 && j->cached) {
        vector<char> ch, cb;
        append_range(ch, data(*arena) + j->c_head, j->c_head_n);
        append_range(cb, data(*arena) + j->c_body, j->c_body_n);
        net_cache_write(j->cpath, j->here, j->c_status, j->now, data(head), len(head), data(ch), len(ch), data(cb), len(cb));
        j->body = j->c_body;
        j->body_n = j->c_body_n;
        net_type_of_head(data(ch), len(ch), j->type, sizeof j->type, j->charset, sizeof j->charset);
        j->cache = NET_CACHE_USE_REVALIDATED;
        j->cache_age = j->c_age;
        j->cache_life = j->c_life;
        return;
    }
    net_type_of_head(data(head), len(head), j->type, sizeof j->type, j->charset, sizeof j->charset);
    char loc[4096];
    int st = j->status;
    if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) && net_header(data(head), len(head), "location", loc, sizeof loc)) {
        if (j->hop >= NET_MAX_REDIRECTS) {
            j->out = NET_OUTCOME_TOO_MANY_REDIRECTS;
            snprintf(j->rt.reason, sizeof j->rt.reason, "more than %d redirects", NET_MAX_REDIRECTS);
            return;
        }
        net_url next;
        if (!net_resolve(&j->u, loc, &next)) {
            j->out = NET_OUTCOME_BAD_URL;
            snprintf(j->rt.reason, sizeof j->rt.reason, "a redirect to what is not a URL");
            return;
        }
        char text[8192];
        net_url_text(&next, text, sizeof text);
        net_hop_h hp = hop_new(l);
        l->hop[hp.index].resource = j->r;
        l->hop[hp.index].url = net_text(l, text);
        l->hop[hp.index].status = (uint32_t)st;
        memcpy(&j->u, &next, sizeof next);   /* not j->u = next: toolchain/REPORT.md, finding 31 */
        if (st == 303 || ((st == 301 || st == 302) && strcmp(j->meth, "POST") == 0)) {
            snprintf(j->meth, sizeof j->meth, "GET");
            j->sent_n = 0;
        }
        j->hop++;
        j->state = J_HOP;
        return;
    }
    if (len(body)) {
        size_t came = len(body);
        /* dcb or dcz alone, to a request that offered a dictionary: decoded over it */
        char ce[256];
        int dcx = j->dict && net_header_all(data(head), len(head), "content-encoding", ce, sizeof ce) && (net_ci_is(ce, strlen(ce), "dcb") || net_ci_is(ce, strlen(ce), "dcz"));
        if (dcx) {
            snprintf(j->coding, sizeof j->coding, "%s", net_lower(ce[2]) == 'b' ? "dcb" : "dcz");
            j->out = net_dict_decode(j->coding, j->dict_hash, &body, j->rt.reason, sizeof j->rt.reason);
        } else j->out = net_decode_body(data(head), len(head), &body, j->coding, sizeof j->coding, j->rt.reason, sizeof j->rt.reason);
        if (j->out == NET_OUTCOME_OK && j->coding[0]) {
            j->coded = 1;
            j->encoded = (uint32_t)came;
        }
    }
    /* kept as a compression dictionary, when it may be */
    int kept = j->out == NET_OUTCOME_OK ? net_dict_keep(l, &j->u, page, j->cors, data(head), len(head), data(body), len(body), j->now) : -1;
    if (kept >= 0) {
        j->dict_kept = 1;
        snprintf(j->kept_sf, sizeof j->kept_sf, "%s", net_dicts[kept].sf);
        snprintf(j->kept_match, sizeof j->kept_match, "%s", net_dicts[kept].f.match);
        snprintf(j->kept_id, sizeof j->kept_id, "%s", net_dicts[kept].f.id);
    }
    j->body = len(*arena);
    j->body_n = len(body);
    append_range(arena, data(body), len(body));
    if (j->out != NET_OUTCOME_OK) return;
    if (j->status >= 400) j->out = NET_OUTCOME_HTTP_ERROR;
    if (j->caching) {
        int64_t life = net_lifetime(data(head), len(head), j->now);
        char vv[1024];
        int validator = net_header(data(head), len(head), "etag", vv, sizeof vv) || net_header(data(head), len(head), "last-modified", vv, sizeof vv);
        if (net_storable(j->status, data(head), len(head)) && (life > 0 || validator)) {
            net_cache_write(j->cpath, j->here, j->status, j->now, data(head), len(head), "", 0, data(body), len(body));
            j->cache = NET_CACHE_USE_STORED;
            j->cache_age = net_initial_age(data(head), len(head), j->now);
            j->cache_life = life;
        } else {
            if (j->cached) unlink(j->cpath);
            j->cache = NET_CACHE_USE_NOT_STORED;
        }
    }
}

/* its record filled from where it ended */
static void net_job_record(net_loader *l, net_job *j, const net_url *page, const vector<char> *arena) {
    uint32_t r = j->r;
    char text[8192], origin[512];
    net_url_text(&j->u, text, sizeof text);
    l->resource[r].final_url = net_text(l, text);
    net_origin_text(&j->u, origin, sizeof origin);
    l->resource[r].origin = net_text(l, origin);
    l->resource[r].outcome = j->out;
    l->resource[r].status = (uint32_t)j->status;
    l->resource[r].type = net_text(l, j->type);
    l->resource[r].charset = net_text(l, j->charset);
    l->resource[r].size = (uint32_t)j->body_n;
    if (j->out != NET_OUTCOME_OK) l->resource[r].reason = net_text(l, j->rt.reason);
    l->resource[r].tls = (uint8_t)j->rt.tls;
    if (j->rt.tls) {
        l->resource[r].tls_version = net_text(l, j->rt.version);
        l->resource[r].tls_cipher = net_text(l, j->rt.cipher);
        l->resource[r].alpn = net_text(l, j->rt.alpn);
    }
    l->resource[r].proxied = (uint8_t)j->rt.proxied;
    if (j->rt.proxied) l->resource[r].proxy = net_text(l, j->rt.proxy);
    l->resource[r].cross_site = (uint8_t)(page && !net_is_same_site(&j->u, page));
    l->resource[r].cache = j->cache;
    l->resource[r].cache_age = j->cache_age;
    l->resource[r].cache_lifetime = j->cache_life;
    l->resource[r].coded = (uint8_t)j->coded;
    if (j->coded) {
        l->resource[r].coding = net_text(l, j->coding);
        l->resource[r].encoded_size = j->encoded;
    }
    if (j->x_number) {
        l->resource[r].protocol = j->x_h2 ? NET_PROTOCOL_H2 : NET_PROTOCOL_HTTP1;
        l->resource[r].connection = j->x_number;
        l->resource[r].stream = j->x_h2 ? j->x_stream : 0;
        l->resource[r].reused = (uint8_t)j->x_reused;
        l->resource[r].pooled = (uint8_t)j->x_pooled;
    }
    l->resource[r].tls_resumed = (uint8_t)(j->rt.tls && j->rt.resumed);
    if (j->dict && j->x_number) {
        l->resource[r].dict_sent = 1;
        l->resource[r].dict_offered = net_text(l, j->dict_sf);
    }
    if (j->dict_kept) {
        l->resource[r].dict_kept = 1;
        l->resource[r].dict_hash = net_text(l, j->kept_sf);
        l->resource[r].dict_match = net_text(l, j->kept_match);
        l->resource[r].dict_id = net_text(l, j->kept_id);
    }
    if (j->out == NET_OUTCOME_OK || j->out == NET_OUTCOME_HTTP_ERROR) l->resource[r].body = bytes_add(l, data(*arena) + j->body, j->body_n);
    if (j->kind == NET_KIND_IMAGE && j->out == NET_OUTCOME_OK) {
        uint32_t w = 0, hh = 0;
        if (j->body_n) net_image_size_of((const uint8_t *)data(*arena) + j->body, j->body_n, &w, &hh);
        l->resource[r].width = w;
        l->resource[r].height = hh;
    }
}

/* a fetch begun, its record made: its URL parsed (one that is not a URL recorded so at
   once), the caller's body and header lines kept in the arena, its priority Chromium's for
   its kind (a part's priority may be set after). The job is made in its place, zeroed by
   resize: a net_job passed by value (push_back) is a copy of 45 KB that the optimizer
   takes minutes and gigabytes over (toolchain/REPORT.md, finding 31) */
static void net_job_new(net_loader *l, vector<net_job> *jobs, vector<char> *arena, const char *url, net_kind kind, uint32_t from, dom_node_h elem,
                        const char *method, const char *sent, size_t sent_n, const char *hdrs, size_t hdrs_n, int nav) {
    size_t k = len(*jobs);
    resize(jobs, k + 1);
    net_job *j = &(*jobs)[k];
    net_resource_h h = resource_new(l);
    j->r = h.index;
    l->resource[j->r].kind = kind;
    l->resource[j->r].from = from;
    l->resource[j->r].elem = elem;
    l->resource[j->r].url = net_text(l, url);
    l->resource[j->r].final_url = l->resource[j->r].url;
    j->kind = kind;
    j->nav = nav;
    snprintf(j->meth, sizeof j->meth, "%s", method);
    j->sent = len(*arena);
    j->sent_n = sent_n;
    append_range(arena, sent, sent_n);
    j->hdrs = len(*arena);
    j->hdrs_n = hdrs_n;
    append_range(arena, hdrs, hdrs_n);
    j->x_conn = -1;
    j->x_first = -1;
    j->x_last = -1;
    static const uint32_t urgency[5] = { 0, 0, 3, 1, 1 }, incr[5] = { 1, 0, 1, 0, 1 };
    uint32_t kk = (uint32_t)kind < 5 ? (uint32_t)kind : 4;
    j->priority = urgency[kk];
    j->incremental = incr[kk];
    j->state = J_HOP;
    if (!net_parse_abs(url, &j->u)) {
        l->resource[j->r].outcome = NET_OUTCOME_BAD_URL;
        l->resource[j->r].origin = net_text(l, "null");
        l->resource[j->r].reason = net_text(l, "not a URL");
        j->state = J_DONE;
        j->recorded = 1;
    }
}

/* a batch of fetches carried together: each round, every fetch whose next hop is to be
   made begun (in order, so its Cookie and its record are made as a page orders them), the
   exchanges carried at once over the pool's connections, then each answered in order; then
   each recorded. A write to a connection the server has closed fails, where it would end
   the process: SIGPIPE is ignored meanwhile (OpenSSL writes with write(2)) */
static void net_batch(net_loader *l, vector<net_job> *jobs, const net_url *page, net_tls *t, vector<char> *arena) {
    struct sigaction ign, old;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);
    vector<net_piece> pieces;
    for (;;) {
        int any = 0;
        for (size_t k = 0; k < len(*jobs); k++)
            if ((*jobs)[k].state == J_HOP && net_job_prepare(l, &(*jobs)[k], page, arena)) any = 1;
        if (!any) break;
        net_run(jobs, arena, &pieces, t);
        for (size_t k = 0; k < len(*jobs); k++)
            if ((*jobs)[k].state == J_NET) net_job_answer(l, &(*jobs)[k], page, arena, &pieces);
    }
    for (size_t k = 0; k < len(*jobs); k++)
        if (!(*jobs)[k].recorded) net_job_record(l, &(*jobs)[k], page, arena);
    sigaction(SIGPIPE, &old, 0);
}

/* one fetch, by a method of its own (a script's request, a form's submission): its body
   and header lines (each ending in CRLF) sent with it. A request other than a GET is never
   answered from the cache nor kept in it */
static uint32_t net_get_by(net_loader *l, const char *url, net_kind kind, uint32_t from, dom_node_h elem, const net_url *page, net_tls *t,
                           const char *method, const char *sent, size_t sent_n, const char *hdrs, size_t hdrs_n) {
    vector<net_job> jobs;
    vector<char> arena;
    net_job_new(l, &jobs, &arena, url, kind, from, elem, method, sent, sent_n, hdrs, hdrs_n, kind == NET_KIND_DOCUMENT);
    net_batch(l, &jobs, page, t, &arena);
    return jobs[0].r;
}

static uint32_t net_get(net_loader *l, const char *url, net_kind kind, uint32_t from, dom_node_h elem, const net_url *page, net_tls *t) {
    return net_get_by(l, url, kind, from, elem, page, t, "GET", "", 0, "", 0);
}

net_resource_h net_fetch_request(net_loader *l, const char *url, size_t url_len, net_kind kind, const char *method, const char *sent,
                                 size_t sent_n, const char *hdrs, size_t hdrs_n, uint32_t from) {
    char u[8192], pu[8192];
    snprintf(u, sizeof u, "%.*s", (int)url_len, url);
    dom_node_h none = { 0, 0, 0 };
    /* the page that asked: its URL for the same-site rules and for what it may read */
    net_url page;
    int has_page = 0;
    if (from && from < l->resource_len && l->resource[from].live) {
        amb_span fu = l->resource[from].final_url;
        snprintf(pu, sizeof pu, "%.*s", (int)fu.len, text_ptr(l, fu));
        has_page = net_parse_abs(pu, &page);
    }
    uint32_t r = net_get_by(l, u, kind, has_page ? from : 0, none, has_page ? &page : 0, &net_tls_proc, method, sent, sent_n, hdrs, hdrs_n);
    net_jar_write(l);
    l->revision++;
    return resource_handle(l, r);
}

net_resource_h net_fetch(net_loader *l, const char *url, size_t url_len, net_kind kind) {
    char u[8192];
    snprintf(u, sizeof u, "%.*s", (int)url_len, url);
    dom_node_h none = { 0, 0, 0 };
    uint32_t r = net_get(l, u, kind, 0, none, 0, &net_tls_proc);
    net_jar_write(l);
    l->revision++;
    return resource_handle(l, r);
}

/* ---- srcset and sizes: the image source an img takes (HTML 4.8.4.3) ---- */

static uint32_t net_attr(const dom_document *d, uint32_t x, const char *name);
static int net_is(const dom_document *d, uint32_t x, const char *name);

/* the environment images are chosen in (net_set_env): the viewport, the cascade's (800 x 600 by
   default, the window over the profile's zoom), its device pixel ratio (the zoom), the initial
   font size a font-relative length of sizes is of (the reader's), the colour scheme and reduced
   motion the profile prefers */
static double net_view_w = 800, net_view_h = 600, net_dpr = 1, net_em = 16;
static int net_dark, net_reduce;
void net_set_env(double width, double height, double dpr, double font_px, int dark, int reduced_motion) {
    net_view_w = width > 0 ? width : 800;
    net_view_h = height > 0 ? height : 600;
    net_dpr = dpr > 0 ? dpr : 1;
    net_em = font_px > 0 ? font_px : 16;
    net_dark = dark;
    net_reduce = reduced_motion;
}

static int net_ascii_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

/* a candidate of a srcset: its URL's place in the attribute, and its width or density */
typedef struct net_candidate {
    size_t url, url_len;
    int has_w, has_x;
    double w, x;
    char desc[32];
} net_candidate;

/* HTML's valid non-negative integer, and valid floating-point number */
static int net_html_int(const char *s, size_t n, double *v) {
    if (n == 0 || n > 15) return 0;
    double x = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        x = x * 10 + (s[i] - '0');
    }
    *v = x;
    return 1;
}
static int net_html_float(const char *s, size_t n, double *v) {
    size_t i = 0, digits = 0;
    if (i < n && s[i] == '-') i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    if (i < n && s[i] == '.') {
        i++;
        size_t f = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; f++; }
        if (!f) return 0;
        digits += f;
    }
    if (!digits) return 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) i++;
        size_t e = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; e++; }
        if (!e) return 0;
    }
    if (i != n) return 0;
    char t[64];
    snprintf(t, sizeof t, "%.*s", (int)n, s);
    *v = strtod(t, 0);
    return 1;
}

/* a candidate's descriptors (each between from and to in d): a width (Nw, N > 0),
   a density (Nx, N >= 0), a height (Nh, only beside a width); 0 when they are
   none of these, or more than one of a kind */
static int net_descriptors(const char *d, size_t n, net_candidate *c) {
    size_t i = 0;
    int has_h = 0;
    c->has_w = c->has_x = 0;
    c->desc[0] = 0;
    while (i < n) {
        while (i < n && net_ascii_ws(d[i])) i++;
        size_t b = i;
        while (i < n && !net_ascii_ws(d[i])) i++;
        if (i == b) break;
        const char *t = d + b;
        size_t tn = i - b;
        char last = t[tn - 1];
        double v;
        if (last == 'w') {
            if (c->has_w || c->has_x || !net_html_int(t, tn - 1, &v) || v <= 0) return 0;
            c->has_w = 1;
            c->w = v;
        } else if (last == 'x') {
            if (c->has_w || c->has_x || has_h || !net_html_float(t, tn - 1, &v) || v < 0) return 0;
            c->has_x = 1;
            c->x = v;
        } else if (last == 'h') {
            if (has_h || c->has_x || !net_html_int(t, tn - 1, &v) || v <= 0) return 0;
            has_h = 1;
        } else return 0;
        if (last != 'h') snprintf(c->desc, sizeof c->desc, "%.*s", (int)tn, t);
    }
    if (has_h && !c->has_w) return 0;
    return 1;
}

/* HTML's "parse a srcset attribute": candidates split at commas, a URL then its
   descriptors (parentheses keep a comma within); one whose descriptors are wrong is
   dropped. How many there are, at most cap */
static int net_srcset_parse(const char *s, size_t n, net_candidate *out, int cap) {
    size_t p = 0;
    int k = 0;
    while (k < cap) {
        while (p < n && (net_ascii_ws(s[p]) || s[p] == ',')) p++;
        if (p >= n) break;
        size_t ua = p;
        while (p < n && !net_ascii_ws(s[p])) p++;
        size_t ue = p;
        net_candidate c;
        memset(&c, 0, sizeof c);
        size_t da = p, de = p;
        if (ue > ua && s[ue - 1] == ',') {
            while (ue > ua && s[ue - 1] == ',') ue--;
        } else {
            /* the descriptors, to a comma outside parentheses or the end */
            int paren = 0;
            while (p < n && net_ascii_ws(s[p])) p++;
            da = p;
            while (p < n && (paren || s[p] != ',')) {
                if (s[p] == '(') paren = 1;
                else if (s[p] == ')') paren = 0;
                p++;
            }
            de = p;
            if (p < n) p++;
        }
        if (ue == ua) continue;
        c.url = ua;
        c.url_len = ue - ua;
        if (net_descriptors(s + da, de - da, &c)) out[k++] = c;
    }
    return k;
}

/* a <length> of sizes in px: px, em and rem (16px), ex and ch (half an em), vw, vh,
   vmin, vmax, in, cm, mm, q, pt, pc, a unitless 0; -1 when it is none */
static double net_length(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == '-' || s[i] == '+')) i++;
    double v;
    if (!i || !net_html_float(s + (s[0] == '+' ? 1 : 0), i - (s[0] == '+' ? 1 : 0), &v)) return -1e300;
    const char *u = s + i;
    size_t un = n - i;
    static const char *const units[] = { "px", "em", "rem", "ex", "ch", "vw", "vh", "vmin", "vmax", "in", "cm", "mm", "q", "pt", "pc" };
    double vmin = net_view_w < net_view_h ? net_view_w : net_view_h, vmax = net_view_w < net_view_h ? net_view_h : net_view_w;
    double per[15] = { 1, net_em, net_em, net_em / 2, net_em / 2, net_view_w / 100, net_view_h / 100, vmin / 100, vmax / 100,
                       96, 96 / 2.54, 96 / 25.4, 96 / 101.6, 96.0 / 72, 16 };
    if (un == 0) return v == 0 ? 0 : -1e300;
    for (int k = 0; k < 15; k++) if (net_ci_is(u, un, units[k])) return v * per[k];
    return -1e300;
}

/* calc()'s sum: terms of lengths and numbers joined by + and -, each a product of
   factors joined by * and /; a parenthesis or a nested calc() a factor. *dim says
   whether the value is a length; -1e300 when it does not parse */
static double net_calc_sum(const char *s, size_t n, size_t *p, int *dim, int depth);
static double net_calc_factor(const char *s, size_t n, size_t *p, int *dim, int depth) {
    while (*p < n && net_ascii_ws(s[*p])) (*p)++;
    if (depth > 16) return -1e300;
    if (*p + 5 <= n && net_ci_prefix(s + *p, n - *p, "calc(")) *p += 4;
    if (*p < n && s[*p] == '(') {
        (*p)++;
        double v = net_calc_sum(s, n, p, dim, depth + 1);
        while (*p < n && net_ascii_ws(s[*p])) (*p)++;
        if (*p >= n || s[*p] != ')') return -1e300;
        (*p)++;
        return v;
    }
    size_t b = *p;
    while (*p < n && ((s[*p] >= '0' && s[*p] <= '9') || s[*p] == '.' || ((s[*p] == '-' || s[*p] == '+') && *p == b))) (*p)++;
    while (*p < n && ((s[*p] >= 'a' && s[*p] <= 'z') || (s[*p] >= 'A' && s[*p] <= 'Z') || s[*p] == '%')) (*p)++;
    if (*p == b) return -1e300;
    size_t e = *p, d = b;
    while (d < e && ((s[d] >= '0' && s[d] <= '9') || s[d] == '.' || s[d] == '-' || s[d] == '+')) d++;
    if (d == e) {
        double v;
        if (!net_html_float(s + b + (s[b] == '+'), e - b - (s[b] == '+'), &v)) return -1e300;
        *dim = 0;
        return v;
    }
    *dim = 1;
    return net_length(s + b, e - b);
}
static double net_calc_product(const char *s, size_t n, size_t *p, int *dim, int depth) {
    double v = net_calc_factor(s, n, p, dim, depth);
    for (;;) {
        if (v <= -1e299) return -1e300;
        size_t q = *p;
        while (q < n && net_ascii_ws(s[q])) q++;
        if (q >= n || (s[q] != '*' && s[q] != '/')) return v;
        *p = q;
        char op = s[(*p)++];
        int d2 = 0;
        double w = net_calc_factor(s, n, p, &d2, depth);
        if (w <= -1e299 || (*dim && d2) || (op == '/' && (d2 || w == 0))) return -1e300;
        v = op == '*' ? v * w : v / w;
        *dim = *dim || d2;
    }
}
static double net_calc_sum(const char *s, size_t n, size_t *p, int *dim, int depth) {
    double v = net_calc_product(s, n, p, dim, depth);
    for (;;) {
        if (v <= -1e299) return -1e300;
        size_t q = *p;
        while (q < n && net_ascii_ws(s[q])) q++;
        if (q >= n || (s[q] != '+' && s[q] != '-') || q == *p || q + 1 >= n || !net_ascii_ws(s[q + 1])) return v;
        char op = s[q];
        *p = q + 1;
        int d2 = 0;
        double w = net_calc_product(s, n, p, &d2, depth);
        if (w <= -1e299 || d2 != *dim) return -1e300;
        v = op == '+' ? v + w : v - w;
    }
}

/* a source size: a length, or calc() of lengths; -1 when it is none (a negative one too) */
static double net_source_size(const char *s, size_t n) {
    while (n && net_ascii_ws(s[n - 1])) n--;
    while (n && net_ascii_ws(s[0])) { s++; n--; }
    double v;
    if (n > 5 && net_ci_prefix(s, n, "calc(")) {
        size_t p = 0;
        int dim = 0;
        v = net_calc_factor(s, n, &p, &dim, 0);
        while (p < n && net_ascii_ws(s[p])) p++;
        if (p != n || (!dim && v != 0)) return -1;
    } else v = net_length(s, n);
    return v < 0 ? -1 : v;
}

/* the features a lone name holds of here, in a boolean context (Media Queries 4, 2.4.1):
   those whose value is other than 0 or none, as Chromium's headless shell answers them
   (a mouse's hover and fine pointer, colour in sRGB; prefers-color-scheme in either
   scheme, measured); and what the others are, with a value: the keyword each holds for */
static const char *const net_mq_on[] = { "width", "height", "aspect-ratio", "color", "color-gamut", "hover", "any-hover", "pointer",
                                         "any-pointer", "orientation", "resolution", "update", "display-mode", "prefers-color-scheme" };
static const char *const net_mq_name[] = { "hover", "any-hover", "pointer", "any-pointer", "color-gamut", "prefers-contrast",
                                           "forced-colors", "update", "display-mode", "dynamic-range", "prefers-reduced-transparency",
                                           "inverted-colors" };
static const char *const net_mq_value[] = { "hover", "hover", "fine", "fine", "srgb", "no-preference", "none", "fast", "browser",
                                            "standard", "no-preference", "none" };

/* one media feature in its parentheses: width, height and their min- and max-,
   in a range too ((width >= 600px), (400px < width <= 700px)), orientation,
   resolution (1dppx, 96dpi), prefers-color-scheme and prefers-reduced-motion, of the
   environment net_set_env sets, the pointer's and the screen's (net_mq_name), and a
   name alone; 0 for a feature not known */
static int net_media_feature(const char *s, size_t n) {
    while (n && net_ascii_ws(s[n - 1])) n--;
    while (n && net_ascii_ws(s[0])) { s++; n--; }
    size_t id = 0;
    while (id < n && ((s[id] >= 'a' && s[id] <= 'z') || (s[id] >= 'A' && s[id] <= 'Z') || s[id] == '-')) id++;
    if (id == n && n > 0) {
        for (size_t k = 0; k < sizeof net_mq_on / sizeof net_mq_on[0]; k++) if (net_ci_is(s, n, net_mq_on[k])) return 1;
        if (net_ci_is(s, n, "prefers-reduced-motion")) return net_reduce;   /* reduce (measured) */
        return 0;   /* monochrome, grid, a name not known */
    }
    size_t colon = 0;
    while (colon < n && s[colon] != ':') colon++;
    if (colon < n) {
        size_t a = colon;
        while (a && net_ascii_ws(s[a - 1])) a--;
        const char *v = s + colon + 1;
        size_t vn = n - colon - 1;
        while (vn && net_ascii_ws(v[0])) { v++; vn--; }
        double px = net_length(v, vn);
        static const char *const fname[6] = { "width", "min-width", "max-width", "height", "min-height", "max-height" };
        static const int fcmp[6] = { 0, 1, -1, 0, 1, -1 };
        for (int k = 0; k < 6; k++)
            if (net_ci_is(s, a, fname[k])) {
                double have = k < 3 ? net_view_w : net_view_h;
                if (px < -1e299) return 0;
                return fcmp[k] == 0 ? have == px : fcmp[k] > 0 ? have >= px : have <= px;
            }
        if (net_ci_is(s, a, "orientation")) return net_ci_is(v, vn, net_view_h > net_view_w ? "portrait" : "landscape");
        if (net_ci_is(s, a, "prefers-color-scheme")) return net_ci_is(v, vn, net_dark ? "dark" : "light");
        if (net_ci_is(s, a, "prefers-reduced-motion")) return net_ci_is(v, vn, net_reduce ? "reduce" : "no-preference");
        for (size_t k = 0; k < sizeof net_mq_name / sizeof net_mq_name[0]; k++)
            if (net_ci_is(s, a, net_mq_name[k])) return net_ci_is(v, vn, net_mq_value[k]);
        if (net_ci_is(s, a, "resolution") || net_ci_is(s, a, "min-resolution") || net_ci_is(s, a, "max-resolution")) {
            double d = -1;
            if (vn > 4 && net_ci_is(v + vn - 4, 4, "dppx")) net_html_float(v, vn - 4, &d);
            else if (vn > 1 && net_ci_is(v + vn - 1, 1, "x")) net_html_float(v, vn - 1, &d);
            else if (vn > 3 && net_ci_is(v + vn - 3, 3, "dpi")) { if (net_html_float(v, vn - 3, &d)) d /= 96; }
            if (d < 0) return 0;
            return net_ci_prefix(s, a, "min-") ? net_dpr >= d : net_ci_prefix(s, a, "max-") ? net_dpr <= d : net_dpr == d;
        }
        return 0;
    }
    /* a range: a value, a comparison and width or height, either way round, perhaps twice */
    const char *t = s;
    size_t i = 0;
    double lo = -1e300, hi = 1e300, have = -1;
    int lo_eq = 1, hi_eq = 1, ok = 1, named = 0;
    double last = -1e300;
    char lastop[3] = "";
    while (i < n && ok) {
        while (i < n && net_ascii_ws(t[i])) i++;
        size_t b = i;
        if (t[i] == '<' || t[i] == '>' || t[i] == '=') {
            while (i < n && (t[i] == '<' || t[i] == '>' || t[i] == '=')) i++;
            snprintf(lastop, sizeof lastop, "%.*s", (int)(i - b > 2 ? 2 : i - b), t + b);
            continue;
        }
        while (i < n && !net_ascii_ws(t[i]) && t[i] != '<' && t[i] != '>' && t[i] != '=') i++;
        if (net_ci_is(t + b, i - b, "width")) { have = net_view_w; named = 1; }
        else if (net_ci_is(t + b, i - b, "height")) { have = net_view_h; named = 1; }
        else {
            double v = net_length(t + b, i - b);
            if (v < -1e299) return 0;
            /* the value before the name bounds it from below with < or <=, from above with > or >= */
            if (!named) {
                if (lastop[0] == 0) last = v;
                else return 0;
            } else {
                if (strcmp(lastop, ">=") == 0 || strcmp(lastop, ">") == 0) { lo = v; lo_eq = lastop[1] == '='; }
                else if (strcmp(lastop, "<=") == 0 || strcmp(lastop, "<") == 0) { hi = v; hi_eq = lastop[1] == '='; }
                else if (strcmp(lastop, "=") == 0) { lo = hi = v; }
                else return 0;
            }
            lastop[0] = 0;
            continue;
        }
        if (last > -1e299) {
            if (strcmp(lastop, "<=") == 0 || strcmp(lastop, "<") == 0) { lo = last; lo_eq = lastop[1] == '='; }
            else if (strcmp(lastop, ">=") == 0 || strcmp(lastop, ">") == 0) { hi = last; hi_eq = lastop[1] == '='; }
            else if (strcmp(lastop, "=") == 0) { lo = hi = last; }
            else return 0;
        }
        lastop[0] = 0;
    }
    if (!named) return 0;
    if (lo > -1e299 && (have < lo || (!lo_eq && have == lo))) return 0;
    if (hi < 1e299 && (have > hi || (!hi_eq && have == hi))) return 0;
    return 1;
}

/* a <media-condition>: `not' a parenthesized one, or parenthesized ones joined by
   `and' alone or by `or' alone, each a feature or a condition in parentheses */
static int net_media_condition(const char *s, size_t n, int depth) {
    size_t i = 0;
    while (i < n && net_ascii_ws(s[i])) i++;
    if (depth > 8) return 0;
    int neg = 0;
    if (i + 3 < n && net_ci_prefix(s + i, n - i, "not") && net_ascii_ws(s[i + 3])) { neg = 1; i += 4; }
    int result = -1, op = 0;   /* op: 1 and, 2 or */
    while (i < n) {
        while (i < n && net_ascii_ws(s[i])) i++;
        if (i >= n) break;
        if (s[i] != '(') {
            size_t b = i;
            while (i < n && !net_ascii_ws(s[i]) && s[i] != '(') i++;
            int w = net_ci_is(s + b, i - b, "and") ? 1 : net_ci_is(s + b, i - b, "or") ? 2 : 0;
            if (!w || (op && op != w) || result < 0) return 0;
            op = w;
            continue;
        }
        size_t b = ++i;
        int d = 1;
        while (i < n && d) { if (s[i] == '(') d++; else if (s[i] == ')') d--; i++; }
        if (d) return 0;
        const char *in = s + b;
        size_t inn = i - b - 1, q = 0;
        while (q < inn && net_ascii_ws(in[q])) q++;
        int v = q < inn && (in[q] == '(' || (inn - q > 4 && net_ci_prefix(in + q, inn - q, "not") && net_ascii_ws(in[q + 3])))
                    ? net_media_condition(in, inn, depth + 1) : net_media_feature(in, inn);
        if (result < 0) result = v;
        else if (op == 1) result = result && v;
        else if (op == 2) result = result || v;
        else return 0;
    }
    if (result < 0) return 0;
    return neg ? !result : result;
}

/* the next white-space-delimited word of s from *i, up to a parenthesis: its start, and
   *i past it */
static size_t net_word(const char *s, size_t n, size_t *i) {
    while (*i < n && net_ascii_ws(s[*i])) (*i)++;
    size_t b = *i;
    while (*i < n && !net_ascii_ws(s[*i]) && s[*i] != '(') (*i)++;
    return b;
}

/* a <media-query> (Media Queries 4, 3): a media condition; or `not' or `only', a media
   type and perhaps `and' a condition. Of the types, all and screen hold here; print,
   the deprecated ones (tv, handheld ...) and what is none do not */
static int net_media_query(const char *s, size_t n) {
    size_t i = 0;
    size_t b = net_word(s, n, &i);
    if (b >= n) return 0;
    if (s[b] == '(') return net_media_condition(s, n, 0);
    int neg = 0;
    if (net_ci_is(s + b, i - b, "not") || net_ci_is(s + b, i - b, "only")) {
        int is_not = net_ci_is(s + b, i - b, "not");
        size_t j = i;
        while (j < n && net_ascii_ws(s[j])) j++;
        if (j < n && s[j] == '(') return is_not ? net_media_condition(s, n, 0) : 0;
        neg = is_not;
        b = net_word(s, n, &i);
    }
    int holds = net_ci_is(s + b, i - b, "all") || net_ci_is(s + b, i - b, "screen");
    size_t a = net_word(s, n, &i);
    if (a < n) {
        if (!net_ci_is(s + a, i - a, "and")) return 0;
        holds = holds && net_media_condition(s + i, n - i, 0);
    }
    return neg ? !holds : holds;
}

/* a <media-query-list>, as a <source>'s media is: empty, or one of its queries (split
   at commas outside parentheses) holds */
static int net_media_list(const char *s, size_t n) {
    size_t a = 0;
    while (a < n && net_ascii_ws(s[a])) a++;
    if (a >= n) return 1;
    a = 0;
    while (a <= n) {
        size_t b = a;
        int d = 0;
        while (b < n && (d || s[b] != ',')) {
            if (s[b] == '(') d++;
            else if (s[b] == ')' && d) d--;
            b++;
        }
        if (net_media_query(s + a, b - a)) return 1;
        a = b + 1;
    }
    return 0;
}

/* HTML's "parse a sizes attribute": the first entry whose media condition holds, or
   that has none, gives its length; 100vw when none does */
static double net_sizes(const char *s, size_t n) {
    size_t a = 0;
    while (a < n) {
        size_t b = a;
        int d = 0;
        while (b < n && (d || s[b] != ',')) { if (s[b] == '(') d++; else if (s[b] == ')' && d) d--; b++; }
        size_t x = a, y = b;
        while (x < y && net_ascii_ws(s[x])) x++;
        while (y > x && net_ascii_ws(s[y - 1])) y--;
        /* its last component: a function to its parenthesis, or a token */
        size_t z = y;
        if (z > x && s[z - 1] == ')') {
            int dd = 0;
            while (z > x) {
                z--;
                if (s[z] == ')') dd++;
                else if (s[z] == '(' && --dd == 0) break;
            }
            while (z > x && ((s[z - 1] >= 'a' && s[z - 1] <= 'z') || (s[z - 1] >= 'A' && s[z - 1] <= 'Z') || s[z - 1] == '-')) z--;
            if (z > x && !net_ascii_ws(s[z - 1])) z = y;    /* a parenthesized condition, no length */
        } else
            while (z > x && !net_ascii_ws(s[z - 1])) z--;
        double v = z < y ? net_source_size(s + z, y - z) : -1;
        if (v >= 0) {
            size_t c = z;
            while (c > x && net_ascii_ws(s[c - 1])) c--;
            if (c == x || net_media_condition(s + x, c - x, 0)) return v;
        }
        a = b + 1;
    }
    return net_view_w;
}

/* the img's choice: its srcset's candidates, each density found (a width's by the
   source size of sizes), its src a 1x one when no candidate is 1x and none has a
   width; sorted by density, the first of equal ones kept; then as Blink picks at
   the environment's device pixel ratio (net_set_env).
   The URL into url; 0 when the img has no srcset, or no candidate in it */
typedef struct net_pick { double density, source_size; int candidates; char desc[32]; } net_pick;

static int net_srcset_choose(const dom_document *d, uint32_t x, uint32_t srcset, uint32_t src, char *url, size_t cap, net_pick *pk) {
    char text[8192], sizes[2048];
    amb_span v = d->attr[srcset].value;
    snprintf(text, sizeof text, "%.*s", (int)v.len, text_ptr(d, v));
    net_candidate c[64];
    int k = net_srcset_parse(text, strlen(text), c, 63);
    if (k == 0) return 0;
    uint32_t sz = net_attr(d, x, "sizes");
    double source = net_view_w;
    int any_w = 0, any_1x = 0;
    for (int i = 0; i < k; i++) if (c[i].has_w) any_w = 1;
    if (any_w && sz) {
        amb_span sv = d->attr[sz].value;
        snprintf(sizes, sizeof sizes, "%.*s", (int)sv.len, text_ptr(d, sv));
        source = net_sizes(sizes, strlen(sizes));
    }
    double density[65];
    for (int i = 0; i < k; i++) {
        density[i] = c[i].has_w ? (source > 0 ? c[i].w / source : 1e9) : c[i].has_x ? c[i].x : 1;
        if (density[i] == 1) any_1x = 1;
    }
    int with_src = 0;
    char srcurl[4096];
    srcurl[0] = 0;
    if (src && d->attr[src].value.len && !any_1x && !any_w) {
        amb_span sv = d->attr[src].value;
        snprintf(srcurl, sizeof srcurl, "%.*s", (int)sv.len, text_ptr(d, sv));
        density[k] = 1;
        memset(&c[k], 0, sizeof c[k]);
        with_src = 1;
    }
    int total = k + with_src;
    /* sorted by density, stably: an insertion sort of indices */
    int order[65];
    for (int i = 0; i < total; i++) {
        int j = i;
        while (j > 0 && density[order[j - 1]] > density[i]) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    /* the densities each once, the first of equal ones; then, as Chromium picks at the device
       pixel ratio, the first density at it or past it, else the greatest (measured at ratios of
       2 and 2.5: of 1x, 1.5x and 3x the 3x, of 1x and 5x the 5x, of 1x, 2.3x and 3x at 2.5 the
       3x -- no density nearer below is taken, none is capped) */
    int uniq[65], nu = 0;
    for (int i = 0; i < total; i++)
        if (nu == 0 || density[order[i]] != density[uniq[nu - 1]]) uniq[nu++] = order[i];
    int w = nu - 1;
    for (int i = 0; i < nu; i++)
        if (density[uniq[i]] >= net_dpr) {
            w = i;
            break;
        }
    int pick = uniq[w];
    if (pick == k) snprintf(url, cap, "%s", srcurl);
    else snprintf(url, cap, "%.*s", (int)c[pick].url_len, text + c[pick].url);
    pk->density = density[pick];
    pk->source_size = source;
    pk->candidates = total;
    snprintf(pk->desc, sizeof pk->desc, "%s", pick == k ? "" : c[pick].desc);
    return 1;
}

/* the image types the painter decodes, by their MIME types and the names Chromium also
   takes for them: PNG (an APNG's first frame), GIF, JPEG, WebP, BMP and ICO, and SVG, which
   it draws (src/paint/svg.c) -- measured: image/bmp, image/x-icon and image/vnd.microsoft.icon
   taken, image/x-ms-bmp not. AVIF, which Chromium decodes, is not among them */
static const char *const net_image_types[] = { "image/png", "image/x-png", "image/apng", "image/gif", "image/jpeg", "image/jpg",
                                               "image/pjpeg", "image/webp", "image/svg+xml", "image/bmp", "image/x-icon",
                                               "image/vnd.microsoft.icon" };

/* a <source>'s type, as Blink reads one (its MIME type's essence: white space and
   parameters aside, in any case), one the painter decodes; an empty type is any */
static int net_image_type(const char *s, size_t n) {
    while (n && net_ascii_ws(s[0])) { s++; n--; }
    size_t e = 0;
    while (e < n && s[e] != ';') e++;
    while (e && net_ascii_ws(s[e - 1])) e--;
    if (e == 0) return 1;
    for (size_t k = 0; k < sizeof net_image_types / sizeof net_image_types[0]; k++) if (net_ci_is(s, e, net_image_types[k])) return 1;
    return 0;
}

/* the image an img in a picture takes (HTML's "update the source set", 4.8.4.3): of
   the picture's children before the img, the first source with a srcset of some
   candidate, a type the painter decodes or none, and a media query list that holds
   (in the environment net_set_env sets) or none; its candidate as net_srcset_choose picks
   one, by the source's own sizes. That source, or 0 when none is taken and the img's
   own srcset and src give its image */
static uint32_t net_picture_choose(const dom_document *d, uint32_t picture, uint32_t img, char *url, size_t cap, net_pick *pk) {
    char text[2048];
    for (uint32_t c = d->node[picture].first_child; c && c != img; c = d->node[c].next_sibling) {
        if (!net_is(d, c, "source")) continue;
        uint32_t ss = net_attr(d, c, "srcset");
        if (!ss || !d->attr[ss].value.len) continue;
        uint32_t ty = net_attr(d, c, "type");
        if (ty) {
            amb_span v = d->attr[ty].value;
            snprintf(text, sizeof text, "%.*s", (int)v.len, text_ptr(d, v));
            if (!net_image_type(text, strlen(text))) continue;
        }
        uint32_t md = net_attr(d, c, "media");
        if (md) {
            amb_span v = d->attr[md].value;
            snprintf(text, sizeof text, "%.*s", (int)v.len, text_ptr(d, v));
            if (!net_media_list(text, strlen(text))) continue;
        }
        if (net_srcset_choose(d, c, ss, 0, url, cap, pk)) return c;
    }
    return 0;
}

/* an img's size as a srcset choice gives it: its header's size divided by its density */
static void net_srcset_size(net_loader *l, uint32_t r, const net_pick *pk) {
    l->resource[r].srcset = 1;
    l->resource[r].descriptor = net_text(l, pk->desc);
    l->resource[r].density = pk->density;
    l->resource[r].source_size = pk->source_size;
    l->resource[r].candidates = (uint32_t)pk->candidates;
    if (l->resource[r].outcome != NET_OUTCOME_OK || pk->density <= 0) return;
    amb_span b = l->resource[r].body;
    uint32_t w = 0, h = 0;
    if (b.len) net_image_size_of((const uint8_t *)bytes_ptr(l, b), b.len, &w, &h);
    l->resource[r].width = (uint32_t)(w / pk->density + 1e-6);
    l->resource[r].height = (uint32_t)(h / pk->density + 1e-6);
}

/* ---- what a page asks for ---- */

/* the HTML element of that name (an SVG or MathML element of the name is none) */
static int net_is(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t net_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
/* a link's rel holds the token stylesheet */
static int net_stylesheet_rel(const dom_document *d, uint32_t x) {
    uint32_t a = net_attr(d, x, "rel");
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        size_t b = i;
        while (i < v.len && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        if (i - b == 10) {
            int same = 1;
            for (size_t k = 0; k < 10 && same; k++) if (net_lower(p[b + k]) != "stylesheet"[k]) same = 0;
            if (same) return 1;
        }
    }
    return 0;
}

/* a part of a page as its element asked for it: its record, the record it shares (a URL
   asked for twice), the srcset's or the picture's choice that sizes it */
typedef struct net_part {
    uint32_t r, dup, source;
    int picked;
    net_pick pk;
    net_kind kind;
    dom_node_h elem;
} net_part;

/* the node after x in shadow-including tree order (DOM 4.2.2): a host's shadow tree (a declarative one the
   parser made) right after it, before its children; 0 at the document's end */
static uint32_t net_next_part(const dom_document *d, uint32_t x) {
    uint32_t r = d->node[x].shadow;
    if (r && d->node[r].first_child) return d->node[r].first_child;
    if (d->node[x].first_child) return d->node[x].first_child;
    for (uint32_t y = x, steps = 0; y && y != 1 && steps <= d->node_len; steps++) {
        if (d->node[y].next_sibling) return d->node[y].next_sibling;
        uint32_t p = d->node[y].parent;
        if (!p) {
            uint32_t h = d->node[y].kind == DOM_NODE_KIND_FRAGMENT ? d->node[y].host : 0;
            if (!h || d->node[h].shadow != y) return 0;
            if (d->node[h].first_child) return d->node[h].first_child;
            p = h;
        }
        y = p;
    }
    return 0;
}

net_status net_fetch_subresources(net_loader *l, dom_document *doc, net_resource_h page, uint32_t denied) {
    uint32_t pg = page.index;
    char base_text[8192];
    amb_span fs = l->resource[pg].final_url;
    snprintf(base_text, sizeof base_text, "%.*s", (int)fs.len, text_ptr(l, fs));
    net_url base;
    if (!net_parse_abs(base_text, &base)) return NET_FAILED;
    /* the page's parts, in tree order, each its record made as it is met: a fetch of the
       batch, a part the profile denies, an img whose src is empty, or a URL asked for twice
       (its record filled from the first's after) */
    vector<net_job> jobs;
    vector<char> arena;
    vector<net_part> parts;
    uint32_t images = 0;
    uint32_t x = doc->node_len > 1 ? doc->node[1].first_child : 0;
    while (x) {
        net_kind kind = NET_KIND_OTHER;
        uint32_t a = 0;
        char ref[8192], abs[8192];
        net_part pt;
        memset(&pt, 0, sizeof pt);
        uint32_t srcset = 0;
        int in_picture = 0;
        if (net_is(doc, x, "link") && net_stylesheet_rel(doc, x)) { kind = NET_KIND_STYLESHEET; a = net_attr(doc, x, "href"); }
        else if (net_is(doc, x, "img")) {
            kind = NET_KIND_IMAGE;
            a = net_attr(doc, x, "src");
            /* an img in a picture takes the first of its sources that holds; else, and
               outside a picture, an img with a srcset the candidate chosen for the viewport */
            uint32_t pa = doc->node[x].parent;
            if (pa && net_is(doc, pa, "picture")) {
                pt.source = net_picture_choose(doc, pa, x, ref, sizeof ref, &pt.pk);
                in_picture = 1;
            }
            pt.picked = pt.source != 0;
            uint32_t ss = net_attr(doc, x, "srcset");
            srcset = ss;
            if (!pt.picked && ss && doc->attr[ss].value.len) pt.picked = net_srcset_choose(doc, x, ss, a, ref, sizeof ref, &pt.pk);
        } else if (net_is(doc, x, "script")) { kind = NET_KIND_SCRIPT; a = net_attr(doc, x, "src"); }
        else if (doc->node[x].kind == DOM_NODE_KIND_ELEMENT && doc->node[x].ns == DOM_NAMESPACE_SVG &&
                 (names_is(doc, doc->node[x].name, "image", 5) || names_is(doc, doc->node[x].name, "feImage", 7))) {
            /* an SVG image element's picture (SVG 2, 12.3), or an feImage's (Filter Effects 1,
               15.17): its href, else its xlink:href; none when it is empty (no request is made) or,
               an feImage's, a reference to an element of the document (#id) */
            kind = NET_KIND_IMAGE;
            a = net_attr(doc, x, "href");
            if (!a) a = net_attr(doc, x, "xlink:href");
            if (a && doc->attr[a].value.len == 0) a = 0;
            if (a && names_is(doc, doc->node[x].name, "feImage", 7) && text_ptr(doc, doc->attr[a].value)[0] == '#') a = 0;
        }
        if (pt.picked || (a && doc->attr[a].value.len > 0)) {
            if (!pt.picked) {
                amb_span v = doc->attr[a].value;
                snprintf(ref, sizeof ref, "%.*s", (int)v.len, text_ptr(doc, v));
            }
            net_url u;
            dom_node_h eh = node_handle(doc, x);
            pt.kind = kind;
            pt.elem = eh;
            if (denied >> kind & 1) {
                /* the profile denies the site this kind: recorded, not fetched */
                net_resource_h h = resource_new(l);
                uint32_t r = h.index;
                l->resource[r].kind = kind;
                l->resource[r].from = pg;
                l->resource[r].elem = eh;
                if (net_resolve_enc(&base, ref, net_doc_enc, &u)) net_url_text(&u, abs, sizeof abs);
                else snprintf(abs, sizeof abs, "%s", ref);
                l->resource[r].url = net_text(l, abs);
                l->resource[r].final_url = l->resource[r].url;
                l->resource[r].outcome = NET_OUTCOME_BLOCKED;
                l->resource[r].origin = net_text(l, "null");
                l->resource[r].type = net_text(l, "");
                char why[64];
                snprintf(why, sizeof why, "the profile denies the site's %ss", net_kind_name(kind));
                l->resource[r].reason = net_text(l, why);
                pt.r = r;
            } else if (!net_resolve_enc(&base, ref, net_doc_enc, &u)) {
                net_job_new(l, &jobs, &arena, ref, kind, pg, eh, "GET", "", 0, "", 0, 0);
                pt.r = jobs[len(jobs) - 1].r;
            } else {
                net_url_text(&u, abs, sizeof abs);
                /* each URL fetched once: a second element asking for it shares the first's body */
                uint32_t same = 0;
                for (uint32_t r = 1; r < l->resource_len && !same; r++)
                    if (l->resource[r].live && r != pg && l->resource[r].from == pg && l->resource[r].url.len == strlen(abs) &&
                        memcmp(text_ptr(l, l->resource[r].url), abs, strlen(abs)) == 0) same = r;
                if (same) {
                    net_resource_h h = resource_new(l);
                    pt.r = h.index;
                    pt.dup = same;
                } else {
                    net_job_new(l, &jobs, &arena, abs, kind, pg, eh, "GET", "", 0, "", 0, 0);
                    pt.r = jobs[len(jobs) - 1].r;
                    /* Chromium's priorities: the first five images LOW, later ones LOWEST,
                       both incremental; a script async or deferred LOWEST, else MEDIUM, or
                       LOW after an image (Blink's ComputeLoadPriority: its preload scanner
                       meets a script after an image fetch as one at the bottom of the page) */
                    size_t jl = len(jobs) - 1;
                    /* a crossorigin element asks in CORS mode (its response readable, a
                       dictionary of another origin then offered and kept) */
                    jobs[jl].cors = net_attr(doc, x, "crossorigin") != 0;
                    if (kind == NET_KIND_IMAGE) jobs[jl].priority = ++images <= 5 ? 2 : 3;
                    if (kind == NET_KIND_SCRIPT) {
                        if (net_attr(doc, x, "async") || net_attr(doc, x, "defer")) jobs[jl].priority = 3;
                        else jobs[jl].priority = images ? 2 : 1;
                    }
                }
            }
            push_back(parts, pt);
        } else if (kind == NET_KIND_IMAGE && (a || srcset || in_picture)) {
            /* an img whose src is empty, or which uses srcset or picture (a srcset attribute, a
               picture parent) with no source chosen and no src: its request is broken at once
               (HTML's update the image data, steps 9 and 14: no source selected), and Chromium
               draws it broken; recorded, not fetched. With none of these it has no request, and
               is drawn as one with no image to load */
            net_resource_h h = resource_new(l);
            uint32_t r = h.index;
            l->resource[r].kind = kind;
            l->resource[r].from = pg;
            l->resource[r].elem = node_handle(doc, x);
            l->resource[r].url = net_text(l, "");
            l->resource[r].final_url = l->resource[r].url;
            l->resource[r].outcome = NET_OUTCOME_BAD_URL;
            l->resource[r].origin = net_text(l, "null");
            l->resource[r].type = net_text(l, "");
            l->resource[r].reason = net_text(l, a ? "the src attribute is empty" : srcset ? "the srcset attribute names no image" : "no source of its picture holds");
        }
        x = net_next_part(doc, x);
    }
    net_batch(l, &jobs, &base, &net_tls_proc, &arena);
    /* then, in order, each part asked for twice given the first's record, and each image's
       choice of a srcset or a picture's source applied */
    for (size_t k = 0; k < len(parts); k++) {
        net_part pt = parts[k];
        uint32_t fetched = pt.r;
        if (pt.dup) {
            net_resource copy = l->resource[pt.dup];
            uint32_t gen = l->resource[fetched].gen;
            l->resource[fetched] = copy;
            l->resource[fetched].gen = gen;
            l->resource[fetched].live = 1;
            l->resource[fetched].elem = pt.elem;
            l->resource[fetched].kind = pt.kind;
            /* the first's choice is not this one's: the size again from the header */
            l->resource[fetched].srcset = 0;
            l->resource[fetched].descriptor = net_text(l, "");
            dom_node_h no_source = { 0, 0, 0 };
            l->resource[fetched].picture = 0;
            l->resource[fetched].source = no_source;
            if (pt.kind == NET_KIND_IMAGE && copy.outcome == NET_OUTCOME_OK && copy.body.len) {
                uint32_t w = 0, hh = 0;
                net_image_size_of((const uint8_t *)bytes_ptr(l, copy.body), copy.body.len, &w, &hh);
                l->resource[fetched].width = w;
                l->resource[fetched].height = hh;
            }
        }
        if (pt.picked && fetched) net_srcset_size(l, fetched, &pt.pk);
        if (pt.source && fetched) {
            l->resource[fetched].picture = 1;
            l->resource[fetched].source = node_handle(doc, pt.source);
        }
    }
    net_jar_write(l);
    l->revision++;
    return NET_OK;
}
