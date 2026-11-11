/* src/net/net.c -- the network module (contracts/net.pl): URLs, fetches, what a
   page loads; and the module's generated implementation.

   URLs (WHATWG URL, a subset). A scheme, then for the special schemes (http,
   https, file, ws, wss, ftp) an authority -- user info dropped, the host
   lowercased, a default port dropped -- and a path whose dot segments are
   removed and whose backslashes are slashes; a query; a fragment, which no fetch
   sends. Another scheme's URL is opaque (data:, mailto:). A reference resolves
   against its base as scheme-relative, path-absolute, query-only,
   fragment-only or path-relative. No IDNA, no IPv6 literals, no
   percent-encoding sets beyond what a file: path decodes.

   Fetching. file: reads the file (its type from its name); data: decodes its
   body (base64 or percent-encoded); http: and https: are HTTP/1.1 over a socket
   -- GET with Connection: close, interim 1xx answers passed over, the response's
   Content-Length or chunked body or the bytes until close (a body shorter than
   its length, or chunks without their last, is malformed), at most five
   redirects, ten seconds a read, 32 MB a body. Each address a name has is tried
   in turn. https: is TLS 1.2 or 1.3 through the system's OpenSSL, the one part a
   library gives: SNI (a host name's, never an address's), ALPN offering
   http/1.1, the server's chain verified against SSL_CERT_FILE or else the
   system's store (/etc/ssl/certs/ca-certificates.crt), and its certificate
   against the URL's host, by name or by address. A verification that fails and
   a handshake that fails are outcomes of their own, each with the library's
   reason. The TLS context, which holds the store, is made at an operation's
   first https: URL and shared by its connections. A proxy comes from the
   environment: https_proxy (or HTTPS_PROXY) for https:, through a CONNECT
   tunnel; http_proxy for http:, its requests in absolute form (HTTP_PROXY is
   not read: a CGI program's request can set it); either may carry user info,
   sent as Basic Proxy-Authorization. no_proxy (or NO_PROXY) names the hosts
   reached directly: `*', a domain with its subdomains (a leading `.' or `*.'
   changes nothing), an address or a CIDR block for an address, each with an
   optional port. A proxy that will not open a tunnel is proxy_refused, with its
   status line. Every fetch that fails has its reason in words.

   What a page asks for: <link rel=stylesheet href>, <img src>, <script src>
   (fetched, never run), each URL fetched once. A page served over http: or
   https: may not load a file: URL: blocked, here in C; nor any kind of resource
   its profile denies its site. An image's size is read from its header (PNG,
   GIF, JPEG); its pixels are not decoded here. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <vector.pl>
#include <amb.pl>
#include "url.h"

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
#define NET_SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define NET_SSL_CTRL_SET_MIN_PROTO_VERSION 123
#define NET_TLS1_2_VERSION 0x0303
#define NET_SSL_VERIFY_PEER 1

#define NET_MAX_BODY (32u << 20)
#define NET_MAX_REDIRECTS 5
/* the system's store of certificate authorities, when SSL_CERT_FILE names none */
#define NET_CA_FILE "/etc/ssl/certs/ca-certificates.crt"

/* ---- URLs ---- */

typedef struct net_url {
    char scheme[16];
    char host[256];
    int port;                 /* -1: none written */
    char path[4096];          /* an opaque URL's whole text after the scheme */
    char query[2048];
    int has_query, special, opaque;
} net_url;

static char net_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int net_special(const char *s) {
    return strcmp(s, "http") == 0 || strcmp(s, "https") == 0 || strcmp(s, "file") == 0 || strcmp(s, "ws") == 0 ||
           strcmp(s, "wss") == 0 || strcmp(s, "ftp") == 0;
}
static int net_default_port(const char *s) {
    if (strcmp(s, "http") == 0 || strcmp(s, "ws") == 0) return 80;
    if (strcmp(s, "https") == 0 || strcmp(s, "wss") == 0) return 443;
    if (strcmp(s, "ftp") == 0) return 21;
    return -1;
}

/* the input as the parser reads it: no leading or trailing control or space, no tab or newline */
static size_t net_clean(const char *s, char *out, size_t cap) {
    size_t n = strlen(s), a = 0, b = n, k = 0;
    while (a < n && (unsigned char)s[a] <= 0x20) a++;
    while (b > a && (unsigned char)s[b - 1] <= 0x20) b--;
    for (size_t i = a; i < b && k + 1 < cap; i++) if (s[i] != '\t' && s[i] != '\n' && s[i] != '\r') out[k++] = s[i];
    out[k] = 0;
    return k;
}

/* the scheme's length, when the text begins with one and its colon */
static size_t net_scheme_len(const char *s) {
    size_t i = 0;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
    while (s[i] && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
                    s[i] == '+' || s[i] == '-' || s[i] == '.')) i++;
    return s[i] == ':' ? i : 0;
}

/* RFC 3986, 5.2.4: a path's dot segments removed, as the RFC writes the steps */
static void net_pop_segment(char *out, size_t *k) {
    while (*k > 0 && out[*k - 1] != '/') (*k)--;
    if (*k > 0) (*k)--;
}
static void net_remove_dots(char *path) {
    char in[4096], out[4096];
    snprintf(in, sizeof in, "%s", path);
    size_t i = 0, n = strlen(in), k = 0;
    while (i < n) {
        if (strncmp(in + i, "../", 3) == 0) { i += 3; continue; }                           /* A */
        if (strncmp(in + i, "./", 2) == 0) { i += 2; continue; }
        if (strncmp(in + i, "/./", 3) == 0) { i += 2; continue; }                           /* B */
        if (i + 2 == n && strncmp(in + i, "/.", 2) == 0) { in[i + 1] = 0; n = i + 1; continue; }
        if (strncmp(in + i, "/../", 4) == 0) { i += 3; net_pop_segment(out, &k); continue; } /* C */
        if (i + 3 == n && strncmp(in + i, "/..", 3) == 0) { in[i + 1] = 0; n = i + 1; net_pop_segment(out, &k); continue; }
        if ((i + 1 == n && in[i] == '.') || (i + 2 == n && in[i] == '.' && in[i + 1] == '.')) break;   /* D */
        if (in[i] == '/' && k + 1 < sizeof out) out[k++] = in[i++];                          /* E */
        while (i < n && in[i] != '/' && k + 1 < sizeof out) out[k++] = in[i++];
    }
    out[k] = 0;
    snprintf(path, 4096, "%s", out);
}

/* the authority and the rest of a special URL, after its "//" */
static int net_parse_authority(const char *s, net_url *u) {
    size_t e = 0;
    while (s[e] && s[e] != '/' && s[e] != '\\' && s[e] != '?' && s[e] != '#') e++;
    /* user info, dropped */
    size_t a = 0;
    for (size_t i = 0; i < e; i++) if (s[i] == '@') a = i + 1;
    size_t colon = e;
    for (size_t i = a; i < e; i++) if (s[i] == ':') colon = i;
    size_t hn = colon - a;
    if (hn >= sizeof u->host) return 0;
    for (size_t i = 0; i < hn; i++) u->host[i] = net_lower(s[a + i]);
    u->host[hn] = 0;
    u->port = -1;
    if (colon < e) {
        long p = 0;
        if (colon + 1 == e) p = -1;
        for (size_t i = colon + 1; i < e; i++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            p = p * 10 + (s[i] - '0');
            if (p > 65535) return 0;
        }
        u->port = (int)p;
    }
    if (u->port == net_default_port(u->scheme)) u->port = -1;
    if (hn == 0 && strcmp(u->scheme, "file") != 0) return 0;
    return (int)e;
}

/* the path, query and fragment from s: the path its dots removed */
static void net_parse_tail(const char *s, net_url *u) {
    size_t i = 0, k = 0;
    while (s[i] && s[i] != '?' && s[i] != '#' && k + 1 < sizeof u->path) {
        u->path[k++] = s[i] == '\\' && u->special ? '/' : s[i];
        i++;
    }
    u->path[k] = 0;
    if (u->special && u->path[0] != '/') {
        char t[4096];
        snprintf(t, sizeof t, "/%s", u->path);
        snprintf(u->path, sizeof u->path, "%s", t);
    }
    if (u->special) net_remove_dots(u->path);
    u->has_query = 0;
    u->query[0] = 0;
    if (s[i] == '?') {
        size_t j = i + 1, q = 0;
        while (s[j] && s[j] != '#' && q + 1 < sizeof u->query) u->query[q++] = s[j++];
        u->query[q] = 0;
        u->has_query = 1;
    }
}

static int net_parse_abs(const char *in, net_url *u) {
    char s[8192];
    net_clean(in, s, sizeof s);
    memset(u, 0, sizeof *u);
    size_t sl = net_scheme_len(s);
    if (sl == 0 || sl >= sizeof u->scheme) return 0;
    for (size_t i = 0; i < sl; i++) u->scheme[i] = net_lower(s[i]);
    u->scheme[sl] = 0;
    u->port = -1;
    u->special = net_special(u->scheme);
    const char *rest = s + sl + 1;
    if (!u->special) {
        /* opaque: everything up to the fragment */
        size_t k = 0;
        while (rest[k] && rest[k] != '#' && k + 1 < sizeof u->path) { u->path[k] = rest[k]; k++; }
        u->path[k] = 0;
        u->opaque = 1;
        return 1;
    }
    if ((rest[0] == '/' || rest[0] == '\\') && (rest[1] == '/' || rest[1] == '\\')) {
        int e = net_parse_authority(rest + 2, u);
        if (!e && strcmp(u->scheme, "file") != 0) return 0;
        net_parse_tail(rest + 2 + e, u);
        return 1;
    }
    if (strcmp(u->scheme, "file") == 0) { net_parse_tail(rest, u); return 1; }
    return 0;
}

static size_t net_url_text(const net_url *u, char *out, size_t cap) {
    int n;
    if (u->opaque) n = snprintf(out, cap, "%s:%s", u->scheme, u->path);
    else if (u->port >= 0) n = snprintf(out, cap, "%s://%s:%d%s%s%s", u->scheme, u->host, u->port, u->path, u->has_query ? "?" : "", u->query);
    else n = snprintf(out, cap, "%s://%s%s%s%s", u->scheme, u->host, u->path, u->has_query ? "?" : "", u->query);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

static size_t net_origin_text(const net_url *u, char *out, size_t cap) {
    int n;
    if (u->opaque || strcmp(u->scheme, "file") == 0) n = snprintf(out, cap, "null");
    else if (u->port >= 0) n = snprintf(out, cap, "%s://%s:%d", u->scheme, u->host, u->port);
    else n = snprintf(out, cap, "%s://%s", u->scheme, u->host);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* a reference resolved against a base */
static int net_resolve(const net_url *base, const char *in, net_url *u) {
    char s[8192];
    net_clean(in, s, sizeof s);
    if (net_scheme_len(s)) return net_parse_abs(s, u);
    if (base->opaque) return 0;
    *u = *base;
    if ((s[0] == '/' || s[0] == '\\') && (s[1] == '/' || s[1] == '\\')) {
        char t[8192];
        snprintf(t, sizeof t, "%s:%s", base->scheme, s);
        return net_parse_abs(t, u);
    }
    if (s[0] == '/' || (s[0] == '\\' && base->special)) { net_parse_tail(s, u); return 1; }
    if (s[0] == '?') { net_parse_tail(s, u); snprintf(u->path, sizeof u->path, "%s", base->path); return 1; }
    if (s[0] == '#' || s[0] == 0) return 1;
    /* a path relative to the base's directory */
    char t[8192];
    size_t d = strlen(base->path);
    while (d > 0 && base->path[d - 1] != '/') d--;
    snprintf(t, sizeof t, "%.*s%s", (int)d, base->path, s);
    net_parse_tail(t, u);
    return 1;
}

int net_url_resolve(const char *base, const char *ref, char *out, size_t cap) {
    net_url b, u;
    if (!net_parse_abs(base, &b) || !net_resolve(&b, ref, &u)) return 0;
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
    if (strcmp(e, "svg") == 0) return "image/svg+xml";
    if (strcmp(e, "txt") == 0) return "text/plain";
    return "application/octet-stream";
}

/* an image's size from its header: PNG, GIF, JPEG */
static void net_image_size(const vector<char> *b, uint32_t *w, uint32_t *h) {
    size_t n = len(*b);
    *w = 0;
    *h = 0;
    if (n >= 24 && (uint8_t)at(*b, 0) == 0x89 && at(*b, 1) == 'P' && at(*b, 2) == 'N' && at(*b, 3) == 'G') {
        *w = (uint32_t)(uint8_t)at(*b, 16) << 24 | (uint32_t)(uint8_t)at(*b, 17) << 16 | (uint32_t)(uint8_t)at(*b, 18) << 8 | (uint8_t)at(*b, 19);
        *h = (uint32_t)(uint8_t)at(*b, 20) << 24 | (uint32_t)(uint8_t)at(*b, 21) << 16 | (uint32_t)(uint8_t)at(*b, 22) << 8 | (uint8_t)at(*b, 23);
        return;
    }
    if (n >= 10 && at(*b, 0) == 'G' && at(*b, 1) == 'I' && at(*b, 2) == 'F') {
        *w = (uint32_t)(uint8_t)at(*b, 6) | (uint32_t)(uint8_t)at(*b, 7) << 8;
        *h = (uint32_t)(uint8_t)at(*b, 8) | (uint32_t)(uint8_t)at(*b, 9) << 8;
        return;
    }
    if (n >= 4 && (uint8_t)at(*b, 0) == 0xFF && (uint8_t)at(*b, 1) == 0xD8) {
        /* the markers to the first start of frame */
        size_t i = 2;
        while (i + 9 < n) {
            if ((uint8_t)at(*b, i) != 0xFF) { i++; continue; }
            uint8_t m = (uint8_t)at(*b, i + 1);
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { i++; continue; }
            size_t seg = (size_t)(uint8_t)at(*b, i + 2) << 8 | (uint8_t)at(*b, i + 3);
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                *h = (uint32_t)(uint8_t)at(*b, i + 5) << 8 | (uint8_t)at(*b, i + 6);
                *w = (uint32_t)(uint8_t)at(*b, i + 7) << 8 | (uint8_t)at(*b, i + 8);
                return;
            }
            i += 2 + seg;
        }
    }
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

static net_outcome net_get_data(const net_url *u, vector<char> *body, char *type, size_t tcap, char *why, size_t wcap) {
    const char *s = u->path;
    size_t n = strlen(s), comma = 0;
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

/* ---- connections: a socket, a proxy's tunnel, TLS ---- */

/* a TCP connection to host (bare) and port, each of its addresses tried in turn:
   the socket, or -2 when the name has no address and -1 when none answered, why in words */
static int net_dial(const char *host, int port, char *why, size_t cap) {
    net_addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    own net_addrinfo *res = 0;
    int g = getaddrinfo(host, ps, &hints, &res);
    if (g != 0 || !res) { snprintf(why, cap, "%s: %s", host, gai_strerror(g)); freeaddrinfo(res); return -2; }
    defer(res) { freeaddrinfo(res); }
    int err = 0;
    for (const net_addrinfo *a = res; a; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) { err = errno; continue; }
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) return fd;
        err = errno;
        close(fd);
    }
    snprintf(why, cap, "%s port %d: %s", host, port, strerror(err));
    return -1;
}

/* every byte of p sent, over TLS when there is a session */
static int net_send_all(int fd, SSL *ssl, const char *p, size_t n) {
    size_t done = 0;
    while (done < n) {
        size_t step = n - done > 65536 ? 65536 : n - done;
        ssize_t k = ssl ? (ssize_t)SSL_write(ssl, p + done, (int)step) : send(fd, p + done, step, MSG_NOSIGNAL);
        if (k <= 0) return 0;
        done += (size_t)k;
    }
    return 1;
}

static ssize_t net_recv(int fd, SSL *ssl, char *buf, size_t n) {
    if (ssl) return SSL_read(ssl, buf, (int)n);
    return recv(fd, buf, n, 0);
}

/* the TLS client an operation's connections share: its context, made at the
   operation's first https: URL, holds the store of certificate authorities */
typedef struct net_tls { own SSL_CTX *ctx; int tried, ok; char why[300]; } net_tls;

static int net_tls_ready(net_tls *t) {
    if (t->tried) return t->ok;
    t->tried = 1;
    const char *env = getenv("SSL_CERT_FILE");
    char ca[1024];
    snprintf(ca, sizeof ca, "%s", env && *env ? env : NET_CA_FILE);
    SSL_CTX_free(t->ctx);                   /* none yet: this is the first try */
    t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx) { snprintf(t->why, sizeof t->why, "no TLS context could be made"); return 0; }
    SSL_CTX_ctrl(t->ctx, NET_SSL_CTRL_SET_MIN_PROTO_VERSION, NET_TLS1_2_VERSION, 0);
    if (SSL_CTX_load_verify_locations(t->ctx, ca, 0) != 1) {
        snprintf(t->why, sizeof t->why, "no certificate authority could be read from %s", ca);
        return 0;
    }
    SSL_CTX_set_verify(t->ctx, NET_SSL_VERIFY_PEER, 0);
    static const unsigned char alpn[9] = { 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    SSL_CTX_set_alpn_protos(t->ctx, alpn, 9);
    t->ok = 1;
    return 1;
}

/* what an exchange went over, recorded on its resource; and why it failed */
typedef struct net_route {
    int tls;
    char version[32], cipher[96], alpn[32];
    int proxied;
    char proxy[300];
    char reason[600];
} net_route;

/* the session over fd: SNI and the name or address to verify from the bare host,
   the handshake, the server's chain checked by it. OK, or how it failed and why */
static net_outcome net_tls_start(SSL *ssl, int fd, const char *host, net_route *rt) {
    char name[256];
    snprintf(name, sizeof name, "%s", host);
    SSL_set_fd(ssl, fd);
    if (net_is_address(name)) X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), name);
    else {
        SSL_ctrl(ssl, NET_SSL_CTRL_SET_TLSEXT_HOSTNAME, 0, name);
        SSL_set1_host(ssl, name);
    }
    ERR_clear_error();
    if (SSL_connect(ssl) != 1) {
        long v = SSL_get_verify_result(ssl);
        if (v != 0) {
            snprintf(rt->reason, sizeof rt->reason, "%s", X509_verify_cert_error_string(v));
            return NET_OUTCOME_TLS_VERIFY_FAILED;
        }
        unsigned long e = ERR_get_error();
        if (e) {
            /* error:CODE:LIBRARY:FUNCTION:REASON -- the reason */
            char buf[256];
            ERR_error_string_n(e, buf, sizeof buf);
            const char *r = buf;
            for (int c = 0; c < 4 && strchr(r, ':'); c++) r = strchr(r, ':') + 1;
            snprintf(rt->reason, sizeof rt->reason, "%s", r);
        } else snprintf(rt->reason, sizeof rt->reason, "the server closed the connection in the handshake");
        return NET_OUTCOME_TLS_HANDSHAKE_FAILED;
    }
    rt->tls = 1;
    snprintf(rt->version, sizeof rt->version, "%s", SSL_get_version(ssl));
    const SSL_CIPHER *c = SSL_get_current_cipher(ssl);
    snprintf(rt->cipher, sizeof rt->cipher, "%s", SSL_CIPHER_get_name(c));
    const unsigned char *ap = 0;
    unsigned int an = 0;
    SSL_get0_alpn_selected(ssl, &ap, &an);
    if (ap && an < sizeof rt->alpn) { memcpy(rt->alpn, ap, an); rt->alpn[an] = 0; }
    return NET_OUTCOME_OK;
}

/* a proxy asked for a tunnel to target (host:port): its answer's head read to the
   blank line, a byte at a time so that nothing of the server's is taken; OK on a
   2xx, else proxy_refused with the proxy's status line */
static net_outcome net_tunnel(int fd, const char *target, const char *auth, char *why, size_t cap) {
    char req[1400];
    int n = snprintf(req, sizeof req, "CONNECT %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Ambitious/0.1\r\n%s%s%s\r\n", target, target,
                     auth[0] ? "Proxy-Authorization: Basic " : "", auth, auth[0] ? "\r\n" : "");
    if (n < 0 || (size_t)n >= sizeof req || !net_send_all(fd, 0, req, (size_t)n)) {
        snprintf(why, cap, "the proxy closed the connection");
        return NET_OUTCOME_PROXY_REFUSED;
    }
    char head[8192];
    size_t k = 0;
    while (k + 1 < sizeof head) {
        char c;
        if (recv(fd, &c, 1, 0) != 1) break;
        head[k++] = c;
        if (k >= 4 && memcmp(head + k - 4, "\r\n\r\n", 4) == 0) break;
    }
    head[k] = 0;
    size_t le = 0;
    while (le < k && head[le] != '\r' && head[le] != '\n') le++;
    if (k < 12 || strncmp(head, "HTTP/1.", 7) != 0) {
        snprintf(why, cap, "the proxy gave no HTTP answer to CONNECT");
        return NET_OUTCOME_PROXY_REFUSED;
    }
    if (head[9] != '2') {
        snprintf(why, cap, "the proxy answered %.*s", (int)le, head);
        return NET_OUTCOME_PROXY_REFUSED;
    }
    return NET_OUTCOME_OK;
}

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

/* whether a response's bytes so far are the whole of it, by its framing */
static int net_complete(const char *p, size_t n) {
    size_t end = 0, a = net_final_head(p, n, &end);
    if (!a) return 0;
    a--;
    int st = net_status_of(p + a, end - a);
    if (st == 204 || st == 304) return 1;
    char v[64];
    if (net_header(p + a, end - a, "transfer-encoding", v, sizeof v)) {
        /* chunks: checked only once the bytes end as a last chunk does */
        if (n < 5 || memcmp(p + n - 5, "0\r\n\r\n", 5) != 0) return 0;
        return net_chunks(p + end, n - end, 0) == 1;
    }
    if (net_header(p + a, end - a, "content-length", v, sizeof v)) return n - end >= (size_t)atoll(v);
    return 0;
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

/* one HTTP exchange: GET the URL, directly or through the proxy the environment
   names, over TLS for https:; extra holds further header lines, each ending in
   CRLF. Its status, head and body (chunks joined); what it went over in rt */
static net_outcome net_exchange(const net_url *u, net_tls *t, const char *extra, size_t extra_n, int *status, vector<char> *head,
                                vector<char> *body, net_route *rt) {
    int https = strcmp(u->scheme, "https") == 0;
    char host[256], authority[300];
    net_bare_host(u->host, host, sizeof host);
    int port = u->port >= 0 ? u->port : https ? 443 : 80;
    if (u->port >= 0) snprintf(authority, sizeof authority, "%s:%d", u->host, u->port);
    else snprintf(authority, sizeof authority, "%s", u->host);
    net_proxy_t px;
    int via = net_proxy_for(u, &px, rt->reason, sizeof rt->reason);
    if (via < 0) return NET_OUTCOME_PROXY_REFUSED;
    rt->proxied = via;
    if (via) snprintf(rt->proxy, sizeof rt->proxy, "%s", px.origin);
    int fd = via ? net_dial(px.host, px.port, rt->reason, sizeof rt->reason) : net_dial(host, port, rt->reason, sizeof rt->reason);
    if (fd == -2) return NET_OUTCOME_UNREACHABLE;
    if (fd < 0) return NET_OUTCOME_REFUSED;
    own SSL *ssl = 0;
    defer(ssl) { SSL_free(ssl); }
    if (https) {
        net_outcome o = NET_OUTCOME_OK;
        if (via) {
            char target[300];
            snprintf(target, sizeof target, "%s:%d", u->host, port);
            o = net_tunnel(fd, target, px.auth, rt->reason, sizeof rt->reason);
        }
        if (o == NET_OUTCOME_OK && !net_tls_ready(t)) {
            snprintf(rt->reason, sizeof rt->reason, "%s", t->why);
            o = NET_OUTCOME_TLS_VERIFY_FAILED;
        }
        if (o == NET_OUTCOME_OK) {
            ssl = SSL_new(t->ctx);
            if (!ssl) { snprintf(rt->reason, sizeof rt->reason, "no TLS session could be made"); o = NET_OUTCOME_TLS_HANDSHAKE_FAILED; }
            else o = net_tls_start(ssl, fd, host, rt);
        }
        if (o != NET_OUTCOME_OK) { close(fd); return o; }
    }
    /* the request: through a proxy, an http: request names its whole URL */
    vector<char> req;
    char line[8000], whole[340];
    /* (no ternary over the char arrays: cicilang lowers one as a cast to the array, toolchain/REPORT.md) */
    whole[0] = 0;
    if (via && !https) snprintf(whole, sizeof whole, "%s://%s", u->scheme, authority);
    int n = snprintf(line, sizeof line, "GET %s%s%s%s HTTP/1.1\r\nHost: %s\r\n", whole, u->path, u->has_query ? "?" : "", u->query, authority);
    if (n > 0 && (size_t)n < sizeof line) append_range(req, line, (size_t)n);
    n = snprintf(line, sizeof line, "User-Agent: Ambitious/0.1\r\nAccept: */*\r\n");
    append_range(req, line, (size_t)n);
    if (via && !https && px.auth[0]) {
        n = snprintf(line, sizeof line, "Proxy-Authorization: Basic %s\r\n", px.auth);
        append_range(req, line, (size_t)n);
    }
    append_range(req, extra, extra_n);
    n = snprintf(line, sizeof line, "Connection: close\r\n\r\n");
    append_range(req, line, (size_t)n);
    if (!net_send_all(fd, ssl, data(req), len(req))) {
        snprintf(rt->reason, sizeof rt->reason, "the connection closed before the request was sent");
        close(fd);
        return NET_OUTCOME_REFUSED;
    }
    /* the response: to its framing's end, or to the connection's close */
    vector<char> all;
    char buf[16384];
    for (;;) {
        ssize_t k = net_recv(fd, ssl, buf, sizeof buf);
        if (k <= 0) break;
        append_range(all, buf, (size_t)k);
        if (len(all) > NET_MAX_BODY + 65536) {
            snprintf(rt->reason, sizeof rt->reason, "the response passed 32 MB");
            close(fd);
            return NET_OUTCOME_TOO_LARGE;
        }
        if (net_complete(data(all), len(all))) break;
    }
    if (ssl) SSL_shutdown(ssl);
    close(fd);
    return net_parse_response(data(all), len(all), status, head, body, rt->reason, sizeof rt->reason);
}

/* an exchange with SIGPIPE ignored: a write to a connection the server has closed
   then fails, where it would end the process (OpenSSL writes with write(2)) */
static net_outcome net_http(const net_url *u, net_tls *t, const char *extra, size_t extra_n, int *status, vector<char> *head,
                            vector<char> *body, net_route *rt) {
    struct sigaction ign, old;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);
    net_outcome o = net_exchange(u, t, extra, extra_n, status, head, body, rt);
    sigaction(SIGPIPE, &old, 0);
    return o;
}

/* ---- the loader's records ---- */

static amb_span net_text(net_loader *l, const char *s) { return text_add(l, s, strlen(s)); }

/* a fetch, its redirects followed, recorded: the record's index */
static uint32_t net_get(net_loader *l, const char *url, net_kind kind, uint32_t from, dom_node_h elem, const net_url *page, net_tls *t) {
    net_resource_h h = resource_new(l);
    uint32_t r = h.index;
    l->resource[r].kind = kind;
    l->resource[r].from = from;
    l->resource[r].elem = elem;
    l->resource[r].url = net_text(l, url);
    l->resource[r].final_url = l->resource[r].url;
    net_url u;
    char text[8192], type[256], origin[512];
    type[0] = 0;
    if (!net_parse_abs(url, &u)) {
        l->resource[r].outcome = NET_OUTCOME_BAD_URL;
        l->resource[r].origin = net_text(l, "null");
        l->resource[r].reason = net_text(l, "not a URL");
        return r;
    }
    vector<char> body;
    net_outcome out = NET_OUTCOME_OK;
    int status = 200;
    net_route rt;
    memset(&rt, 0, sizeof rt);
    for (int hop = 0;; hop++) {
        memset(&rt, 0, sizeof rt);
        /* a page served over the network may not read the machine's files */
        if (page && strcmp(u.scheme, "file") == 0 && strcmp(page->scheme, "file") != 0 && !page->opaque) {
            out = NET_OUTCOME_BLOCKED;
            status = 0;
            snprintf(rt.reason, sizeof rt.reason, "a page served over %s: may not read a file: URL", page->scheme);
            break;
        }
        clear(body);
        if (strcmp(u.scheme, "file") == 0) { out = net_get_file(&u, &body, type, sizeof type, rt.reason, sizeof rt.reason); status = out == NET_OUTCOME_OK ? 200 : 404; break; }
        if (strcmp(u.scheme, "data") == 0) { out = net_get_data(&u, &body, type, sizeof type, rt.reason, sizeof rt.reason); status = out == NET_OUTCOME_OK ? 200 : 0; break; }
        if (strcmp(u.scheme, "http") != 0 && strcmp(u.scheme, "https") != 0) {
            out = NET_OUTCOME_UNSUPPORTED_SCHEME;
            status = 0;
            snprintf(rt.reason, sizeof rt.reason, "%s: is not a scheme fetched here", u.scheme);
            break;
        }
        vector<char> head;
        out = net_http(&u, t, "", 0, &status, &head, &body, &rt);
        if (out != NET_OUTCOME_OK) { status = 0; break; }
        char ct[256];
        if (net_header(data(head), len(head), "content-type", ct, sizeof ct)) {
            size_t k = 0;
            while (ct[k] && ct[k] != ';' && ct[k] != ' ' && k + 1 < sizeof type) { type[k] = net_lower(ct[k]); k++; }
            type[k] = 0;
        }
        char loc[4096];
        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
            net_header(data(head), len(head), "location", loc, sizeof loc)) {
            if (hop >= NET_MAX_REDIRECTS) {
                out = NET_OUTCOME_TOO_MANY_REDIRECTS;
                snprintf(rt.reason, sizeof rt.reason, "more than %d redirects", NET_MAX_REDIRECTS);
                break;
            }
            net_url next;
            if (!net_resolve(&u, loc, &next)) { out = NET_OUTCOME_BAD_URL; snprintf(rt.reason, sizeof rt.reason, "a redirect to what is not a URL"); break; }
            net_url_text(&next, text, sizeof text);
            net_hop_h hp = hop_new(l);
            l->hop[hp.index].resource = r;
            l->hop[hp.index].url = net_text(l, text);
            l->hop[hp.index].status = (uint32_t)status;
            u = next;
            continue;
        }
        if (status >= 400) out = NET_OUTCOME_HTTP_ERROR;
        break;
    }
    net_url_text(&u, text, sizeof text);
    l->resource[r].final_url = net_text(l, text);
    net_origin_text(&u, origin, sizeof origin);
    l->resource[r].origin = net_text(l, origin);
    l->resource[r].outcome = out;
    l->resource[r].status = (uint32_t)status;
    l->resource[r].type = net_text(l, type);
    l->resource[r].size = (uint32_t)len(body);
    if (out != NET_OUTCOME_OK) l->resource[r].reason = net_text(l, rt.reason);
    l->resource[r].tls = (uint8_t)rt.tls;
    if (rt.tls) {
        l->resource[r].tls_version = net_text(l, rt.version);
        l->resource[r].tls_cipher = net_text(l, rt.cipher);
        l->resource[r].alpn = net_text(l, rt.alpn);
    }
    l->resource[r].proxied = (uint8_t)rt.proxied;
    if (rt.proxied) l->resource[r].proxy = net_text(l, rt.proxy);
    if (out == NET_OUTCOME_OK || out == NET_OUTCOME_HTTP_ERROR) l->resource[r].body = bytes_add(l, data(body), len(body));
    if (kind == NET_KIND_IMAGE && out == NET_OUTCOME_OK) {
        uint32_t w, hh;
        net_image_size(&body, &w, &hh);
        l->resource[r].width = w;
        l->resource[r].height = hh;
    }
    return r;
}

net_resource_h net_fetch(net_loader *l, const char *url, size_t url_len, net_kind kind) {
    char u[8192];
    snprintf(u, sizeof u, "%.*s", (int)url_len, url);
    dom_node_h none = { 0, 0, 0 };
    net_tls t = {0};
    defer(t) { SSL_CTX_free(t.ctx); }
    uint32_t r = net_get(l, u, kind, 0, none, 0, &t);
    l->revision++;
    return resource_handle(l, r);
}

/* ---- what a page asks for ---- */

static int net_is(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && names_is(d, d->node[x].name, name, strlen(name));
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

net_status net_fetch_subresources(net_loader *l, dom_document *doc, net_resource_h page, uint32_t denied) {
    uint32_t pg = page.index;
    char base_text[8192];
    amb_span fs = l->resource[pg].final_url;
    snprintf(base_text, sizeof base_text, "%.*s", (int)fs.len, text_ptr(l, fs));
    net_url base;
    if (!net_parse_abs(base_text, &base)) return NET_FAILED;
    net_tls t = {0};
    defer(t) { SSL_CTX_free(t.ctx); }
    /* the document's elements in tree order */
    uint32_t x = doc->node_len > 1 ? doc->node[1].first_child : 0;
    while (x) {
        net_kind kind = NET_KIND_OTHER;
        uint32_t a = 0;
        if (net_is(doc, x, "link") && net_stylesheet_rel(doc, x)) { kind = NET_KIND_STYLESHEET; a = net_attr(doc, x, "href"); }
        else if (net_is(doc, x, "img")) { kind = NET_KIND_IMAGE; a = net_attr(doc, x, "src"); }
        else if (net_is(doc, x, "script")) { kind = NET_KIND_SCRIPT; a = net_attr(doc, x, "src"); }
        if (a && doc->attr[a].value.len > 0) {
            char ref[8192], abs[8192];
            amb_span v = doc->attr[a].value;
            snprintf(ref, sizeof ref, "%.*s", (int)v.len, text_ptr(doc, v));
            net_url u;
            dom_node_h eh = node_handle(doc, x);
            if (denied >> kind & 1) {
                /* the profile denies the site this kind: recorded, not fetched */
                net_resource_h h = resource_new(l);
                uint32_t r = h.index;
                l->resource[r].kind = kind;
                l->resource[r].from = pg;
                l->resource[r].elem = eh;
                if (net_resolve(&base, ref, &u)) net_url_text(&u, abs, sizeof abs);
                else snprintf(abs, sizeof abs, "%s", ref);
                l->resource[r].url = net_text(l, abs);
                l->resource[r].final_url = l->resource[r].url;
                l->resource[r].outcome = NET_OUTCOME_BLOCKED;
                l->resource[r].origin = net_text(l, "null");
                l->resource[r].type = net_text(l, "");
                char why[64];
                snprintf(why, sizeof why, "the profile denies the site's %ss", net_kind_name(kind));
                l->resource[r].reason = net_text(l, why);
            } else if (!net_resolve(&base, ref, &u)) {
                uint32_t r = net_get(l, ref, kind, pg, eh, &base, &t);
                (void)r;
            } else {
                net_url_text(&u, abs, sizeof abs);
                /* each URL fetched once: a second element asking for it shares the first's body */
                uint32_t same = 0;
                for (uint32_t r = 1; r < l->resource_len && !same; r++)
                    if (l->resource[r].live && r != pg && l->resource[r].from == pg && l->resource[r].url.len == strlen(abs) &&
                        memcmp(text_ptr(l, l->resource[r].url), abs, strlen(abs)) == 0) same = r;
                if (same) {
                    net_resource_h h = resource_new(l);
                    net_resource copy = l->resource[same];
                    uint32_t gen = l->resource[h.index].gen;
                    l->resource[h.index] = copy;
                    l->resource[h.index].gen = gen;
                    l->resource[h.index].live = 1;
                    l->resource[h.index].elem = eh;
                    l->resource[h.index].kind = kind;
                } else net_get(l, abs, kind, pg, eh, &base, &t);
            }
        }
        if (doc->node[x].first_child) { x = doc->node[x].first_child; continue; }
        while (x && !doc->node[x].next_sibling) { x = doc->node[x].parent; if (x == 1) x = 0; }
        if (x) x = doc->node[x].next_sibling;
    }
    l->revision++;
    return NET_OK;
}
