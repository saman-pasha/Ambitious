/* src/net/net.c -- the network module (contracts/net.pl): URLs, fetches, what a
   page loads; and the module's generated implementation.

   URLs (WHATWG URL, a subset). A scheme, then for the special schemes (http,
   https, file, ws, wss, ftp) an authority -- user info dropped, the host as the
   host parser makes it, a default port dropped -- and a path whose dot segments
   are removed and whose backslashes are slashes; a query; a fragment, which no
   fetch sends. The host: an IPv6 literal in brackets, written back compressed;
   else percent-decoded, made ASCII, refused for a forbidden code point, and an
   IPv4 address in any of its forms (0x hex, 0 octal, fewer than four parts) when
   its last label is a number. Made ASCII is IDNA in part: UTF-8 decoded, each code
   point lowercased as UTS #46 maps capitals (Unicode's lowercase mappings,
   src/net/lower.h), ASCII's fullwidth forms and the ideographic full stops
   mapped, and each label past ASCII written xn-- and its Punycode (RFC 3492); the
   rest of the IDNA mapping table, NFC and the bidi and joiner rules are not done.
   Another scheme's URL is opaque (data:, mailto:). A reference resolves against
   its base as scheme-relative, path-absolute, query-only, fragment-only or
   path-relative. No percent-encoding sets beyond what a file: path decodes.

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
   (fetched, never run), each URL fetched once. An img with a srcset takes the
   candidate HTML's image source selection gives for an 800x600 viewport at a
   device pixel ratio of 1: srcset parsed as HTML says, a width descriptor's
   density by the length of the first entry of sizes whose media condition holds
   (100vw when none; px, em, vw ... and calc() of them), src a 1x candidate when
   no candidate is 1x and none has a width, and of the densities sorted the first
   at 1 or past it, else the greatest, as Blink picks; the image's size is then
   its header's divided by its density. Not done: <picture> and its <source>s. A
   page served over http: or https: may not load a file: URL: blocked, here in
   C; nor any kind of resource its profile denies its site. An image's size is
   read from its header (PNG, GIF, JPEG); its pixels are not decoded here.

   Cookies (RFC 6265bis) are kept in a jar, in the file net_keep names (a
   profile's) or for the run alone; the HTTP cache (RFC 9111) in the directory it
   names; gzip and deflate are decoded over the painter's inflate. Each section
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
#include <netinet/in.h>
#include <vector.pl>
#include <amb.pl>
#include "url.h"
#include "lower.h"
#include "../paint/image.h"

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

static int net_host_parse(const char *s, size_t n, char *out, size_t cap);

/* the authority and the rest of a special URL, after its "//": user info dropped,
   the host as the host parser makes it, the port (the scheme's default dropped) */
static int net_parse_authority(const char *s, net_url *u) {
    size_t e = 0;
    while (s[e] && s[e] != '/' && s[e] != '\\' && s[e] != '?' && s[e] != '#') e++;
    /* user info, dropped */
    size_t a = 0;
    for (size_t i = 0; i < e; i++) if (s[i] == '@') a = i + 1;
    /* the host ends at the first colon outside an IPv6 literal's brackets */
    size_t colon = e;
    int inside = 0;
    for (size_t i = a; i < e && colon == e; i++) {
        if (s[i] == '[') inside = 1;
        else if (s[i] == ']') inside = 0;
        else if (s[i] == ':' && !inside) colon = i;
    }
    u->host[0] = 0;
    if (colon > a && !net_host_parse(s + a, colon - a, u->host, sizeof u->host)) return 0;
    if (strcmp(u->scheme, "file") == 0 && strcmp(u->host, "localhost") == 0) u->host[0] = 0;
    size_t hn = strlen(u->host);
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

/* an IPv6 address serialized as WHATWG URL does, in brackets: lowercase hex pieces,
   the first longest run of two or more zero pieces written "::" */
static void net_ipv6_text(const uint16_t a[8], char *out, size_t cap) {
    int best = -1, bestlen = 1;
    for (int i = 0; i < 8;) {
        if (a[i] != 0) { i++; continue; }
        int j = i;
        while (j < 8 && a[j] == 0) j++;
        if (j - i > bestlen) { best = i; bestlen = j - i; }
        i = j;
    }
    size_t k = 0;
    int ignore0 = 0;
    if (cap < 42) { if (cap) out[0] = 0; return; }
    out[k++] = '[';
    for (int i = 0; i < 8; i++) {
        if (ignore0 && a[i] == 0) continue;
        ignore0 = 0;
        if (best == i) {
            out[k++] = ':';
            if (i == 0) out[k++] = ':';
            ignore0 = 1;
            continue;
        }
        k += (size_t)snprintf(out + k, cap - k, "%x", a[i]);
        if (i != 7) out[k++] = ':';
    }
    out[k++] = ']';
    out[k] = 0;
}

/* an IPv4 address's part (WHATWG URL): 0x and hex, a leading 0 and octal, else
   decimal; -1 when it is none, and past 2^32 kept just past it */
static int64_t net_ipv4_number(const char *s, size_t n) {
    if (n == 0) return -1;
    int radix = 10;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; n -= 2; radix = 16; }
    else if (n >= 2 && s[0] == '0') { s++; n--; radix = 8; }
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int d = net_hexval(s[i]);
        if (d < 0 || d >= radix) return -1;
        v = v * radix + d;
        if (v > 0x100000000ll) v = 0x100000000ll;
    }
    return v;
}

/* the host's labels, a last empty one dropped: where each begins and ends */
static int net_labels(const char *s, size_t n, size_t *b, size_t *e, int max) {
    int k = 0;
    size_t a = 0;
    for (;;) {
        size_t c = a;
        while (c < n && s[c] != '.') c++;
        if (k == max) return -1;
        b[k] = a;
        e[k] = c;
        k++;
        if (c >= n) break;
        a = c + 1;
    }
    if (k > 1 && b[k - 1] == e[k - 1]) k--;
    return k;
}

/* WHATWG URL's ends-in-a-number: the last label all digits, or 0x and hex */
static int net_ends_in_number(const char *s, size_t n) {
    size_t b[130], e[130];
    int k = net_labels(s, n, b, e, 130);
    if (k <= 0) return k < 0;
    const char *t = s + b[k - 1];
    size_t tn = e[k - 1] - b[k - 1];
    if (tn == 0) return 0;
    int digits = 1;
    for (size_t i = 0; i < tn; i++) if (t[i] < '0' || t[i] > '9') digits = 0;
    if (digits) return 1;
    if (tn >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        for (size_t i = 2; i < tn; i++) if (net_hexval(t[i]) < 0) return 0;
        return 1;
    }
    return 0;
}

/* WHATWG URL's IPv4 parser: one to four parts, each but the last a byte, the last
   filling what the others leave */
static int net_ipv4_parse(const char *s, size_t n, uint32_t *out) {
    size_t b[8], e[8];
    int k = net_labels(s, n, b, e, 8);
    if (k <= 0 || k > 4) return 0;
    int64_t v[4];
    for (int i = 0; i < k; i++) {
        v[i] = net_ipv4_number(s + b[i], e[i] - b[i]);
        if (v[i] < 0) return 0;
        if (i < k - 1 && v[i] > 255) return 0;
    }
    if (v[k - 1] >= (int64_t)1 << (8 * (5 - k))) return 0;
    uint64_t a = (uint64_t)v[k - 1];
    for (int i = 0; i < k - 1; i++) a += (uint64_t)v[i] << (8 * (3 - i));
    *out = (uint32_t)a;
    return 1;
}

/* ---- IDNA: a host's labels made ASCII ---- */

/* a code point's small letter, as UTS #46 maps a capital: Unicode's simple
   lowercase mapping, by the runs of src/net/lower.h */
static uint32_t net_lower_cp(uint32_t c) {
    int lo = 0, hi = (int)(sizeof net_lower_runs / sizeof net_lower_runs[0]) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (c < net_lower_runs[mid].first) hi = mid - 1;
        else if (c > net_lower_runs[mid].last) lo = mid + 1;
        else if (net_lower_runs[mid].alternating && (c - net_lower_runs[mid].first) % 2) return c;
        else return (uint32_t)((int32_t)c + net_lower_runs[mid].delta);
    }
    return c;
}

/* UTF-8 decoded into code points; 0 for what is ill-formed (an overlong form, a
   surrogate, past U+10FFFF, a sequence cut short) or past cap code points */
static int net_utf8(const char *s, size_t n, uint32_t *cps, size_t cap, size_t *count) {
    size_t i = 0, k = 0;
    while (i < n) {
        uint8_t b = (uint8_t)s[i];
        uint32_t c;
        int more;
        if (b < 0x80) { c = b; more = 0; }
        else if (b >= 0xC2 && b <= 0xDF) { c = b & 0x1F; more = 1; }
        else if (b >= 0xE0 && b <= 0xEF) { c = b & 0x0F; more = 2; }
        else if (b >= 0xF0 && b <= 0xF4) { c = b & 0x07; more = 3; }
        else return 0;
        for (int j = 1; j <= more; j++) {
            if (i + (size_t)j >= n) return 0;
            uint8_t x = (uint8_t)s[i + (size_t)j];
            if ((x & 0xC0) != 0x80) return 0;
            c = c << 6 | (x & 0x3F);
        }
        if ((more == 2 && c < 0x800) || (more == 3 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return 0;
        if (k >= cap) return 0;
        cps[k++] = c;
        i += 1 + (size_t)more;
    }
    *count = k;
    return 1;
}

/* RFC 3492's bias adaptation */
static uint32_t net_puny_adapt(uint32_t delta, uint32_t points, int first) {
    delta = first ? delta / 700 : delta / 2;
    delta += delta / points;
    uint32_t k = 0;
    while (delta > ((36 - 1) * 26) / 2) { delta /= 36 - 1; k += 36; }
    return k + (36 * delta) / (delta + 38);
}

/* a label's code points in Punycode (RFC 3492): its ASCII ones in order, a "-"
   when there are any, then the others as deltas in base 36; 0 on an overflow */
static int net_punycode(const uint32_t *cp, size_t n, char *out, size_t cap) {
    size_t k = 0, basic = 0;
    for (size_t i = 0; i < n; i++)
        if (cp[i] < 0x80) {
            if (k + 1 >= cap) return 0;
            out[k++] = (char)cp[i];
            basic++;
        }
    if (basic) {
        if (k + 1 >= cap) return 0;
        out[k++] = '-';
    }
    uint32_t next = 0x80, delta = 0, bias = 72;
    size_t h = basic;
    while (h < n) {
        uint32_t m = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; i++) if (cp[i] >= next && cp[i] < m) m = cp[i];
        if ((uint64_t)(m - next) * (h + 1) + delta > 0xFFFFFFFFu) return 0;
        delta += (m - next) * (uint32_t)(h + 1);
        next = m;
        for (size_t i = 0; i < n; i++) {
            if (cp[i] < next && ++delta == 0) return 0;
            if (cp[i] != next) continue;
            uint32_t q = delta;
            for (uint32_t t36 = 36;; t36 += 36) {
                uint32_t t = t36 <= bias ? 1 : t36 >= bias + 26 ? 26 : t36 - bias;
                if (q < t) break;
                uint32_t d = t + (q - t) % (36 - t);
                if (k + 1 >= cap) return 0;
                out[k++] = (char)(d < 26 ? 'a' + d : '0' + d - 26);
                q = (q - t) / (36 - t);
            }
            if (k + 1 >= cap) return 0;
            out[k++] = (char)(q < 26 ? 'a' + q : '0' + q - 26);
            bias = net_puny_adapt(delta, (uint32_t)h + 1, h == basic);
            delta = 0;
            h++;
        }
        delta++;
        next++;
    }
    out[k] = 0;
    return 1;
}

/* a domain made ASCII as UTS #46 makes a URL's host, in part: its UTF-8 decoded;
   each code point lowercased (U+0130 to i and U+0307, as Unicode lowercases it),
   ASCII's fullwidth forms made ASCII and the ideographic full stops "."; then
   each label holding a code point past ASCII written xn-- and its Punycode. Not
   done: the rest of the IDNA mapping table (compatibility forms, ignored code
   points), NFC, the bidi and joiner rules, refusing what the table disallows, and
   checking an xn-- label's Punycode */
static int net_domain_ascii(const char *s, size_t n, char *out, size_t cap) {
    int ascii = 1;
    for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) ascii = 0;
    if (ascii) {
        if (n >= cap) return 0;
        for (size_t i = 0; i < n; i++) out[i] = net_lower(s[i]);
        out[n] = 0;
        return 1;
    }
    uint32_t cps[300], m[320];
    size_t nc = 0, nm = 0;
    if (!net_utf8(s, n, cps, 300, &nc)) return 0;
    for (size_t i = 0; i < nc; i++) {
        uint32_t c = cps[i];
        if (nm + 2 > 320) return 0;
        if (c == 0x3002 || c == 0xFF0E || c == 0xFF61) m[nm++] = '.';
        else if (c == 0x130) { m[nm++] = 'i'; m[nm++] = 0x307; }
        else if (c >= 0xFF01 && c <= 0xFF5E) m[nm++] = net_lower_cp(c - 0xFEE0);
        else m[nm++] = net_lower_cp(c);
    }
    size_t k = 0, a = 0;
    while (a <= nm) {
        size_t b = a;
        while (b < nm && m[b] != '.') b++;
        int wide = 0;
        for (size_t i = a; i < b; i++) if (m[i] >= 0x80) wide = 1;
        if (wide) {
            char p[256];
            if (!net_punycode(m + a, b - a, p, sizeof p)) return 0;
            int w = snprintf(out + k, cap - k, "xn--%s", p);
            if (w < 0 || k + (size_t)w >= cap) return 0;
            k += (size_t)w;
        } else {
            for (size_t i = a; i < b; i++) {
                if (k + 1 >= cap) return 0;
                out[k++] = (char)m[i];
            }
        }
        if (b < nm) {
            if (k + 1 >= cap) return 0;
            out[k++] = '.';
        }
        a = b + 1;
    }
    out[k] = 0;
    return 1;
}

/* a special URL's host, as WHATWG URL's host parser reads one: an IPv6 literal in
   brackets, written back compressed; else percent-decoded, made ASCII, holding no
   forbidden code point, and an IPv4 address in any of its forms when its last label
   is a number. The host as the URL is serialized with it */
static int net_host_parse(const char *s, size_t n, char *out, size_t cap) {
    if (n && s[0] == '[') {
        uint16_t a[8];
        if (n < 2 || s[n - 1] != ']' || !net_ipv6_parse(s + 1, n - 2, a)) return 0;
        net_ipv6_text(a, out, cap);
        return 1;
    }
    char d[1024], ascii[256];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (k + 1 >= sizeof d) return 0;
        if (s[i] == '%' && i + 2 < n && net_hexval(s[i + 1]) >= 0 && net_hexval(s[i + 2]) >= 0) {
            d[k++] = (char)(net_hexval(s[i + 1]) * 16 + net_hexval(s[i + 2]));
            i += 2;
        } else d[k++] = s[i];
        if (d[k - 1] == 0) return 0;
    }
    d[k] = 0;
    if (!net_domain_ascii(d, k, ascii, sizeof ascii) || !ascii[0]) return 0;
    for (size_t i = 0; ascii[i]; i++) {
        unsigned char c = (unsigned char)ascii[i];
        if (c <= 0x20 || c == 0x7f || strchr("#%/:<>?@[\\]^|", c)) return 0;
    }
    size_t an = strlen(ascii);
    if (net_ends_in_number(ascii, an)) {
        uint32_t v;
        if (!net_ipv4_parse(ascii, an, &v)) return 0;
        snprintf(out, cap, "%u.%u.%u.%u", v >> 24, v >> 16 & 255, v >> 8 & 255, v & 255);
        return 1;
    }
    if (an >= cap) return 0;
    snprintf(out, cap, "%s", ascii);
    return 1;
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
    if (strcmp(e, "txt") == 0) return "text/plain";
    return "application/octet-stream";
}

/* an image's size from its header (PNG, GIF, JPEG, WebP): the painter's reading, which
   turns a JPEG by its EXIF orientation, so that layout gives it the box it is drawn in */
static void net_image_size_of(const uint8_t *b, size_t n, uint32_t *w, uint32_t *h) {
    *w = 0;
    *h = 0;
    if (!amb_image_size(b, n, w, h)) { *w = 0; *h = 0; }
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
    if (!t->ctx) t->ctx = SSL_CTX_new(TLS_client_method());
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
    n = snprintf(line, sizeof line, "User-Agent: Ambitious/0.1\r\nAccept: */*\r\nAccept-Encoding: gzip, deflate\r\n");
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
static int net_same_site(const net_url *u, const net_url *pu) {
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

/* ---- content codings: gzip and deflate, over the painter's inflate ---- */

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

/* a body decoded from the codings its Content-Encoding lists, the last applied
   undone first; identity is none. The codings into coding; decoding_failed and why
   for one not read here (br, compress, zstd: none of them asked for) or a body
   that does not decode */
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
    for (int c = nn - 1; c >= 0; c--) {
        vector<uint8_t> in, out;
        resize(in, len(*body));
        if (len(in)) memcpy(data(in), data(*body), len(in));
        int ok;
        if (strcmp(names[c], "gzip") == 0 || strcmp(names[c], "x-gzip") == 0) ok = net_gunzip(data(in), len(in), &out, why, wcap);
        else if (strcmp(names[c], "deflate") == 0) ok = net_inflate_body(data(in), len(in), &out, why, wcap);
        else {
            snprintf(why, wcap, "the content coding %s is not one read here", names[c]);
            return NET_OUTCOME_DECODING_FAILED;
        }
        if (!ok) return len(out) >= NET_MAX_BODY ? NET_OUTCOME_TOO_LARGE : NET_OUTCOME_DECODING_FAILED;
        clear(*body);
        resize(body, len(out));
        if (len(out)) memcpy(data(*body), data(out), len(out));
    }
    return NET_OUTCOME_OK;
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
    char text[8192], type[256], origin[512], charset[64];
    type[0] = 0;
    charset[0] = 0;
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
    net_cache_use cache = NET_CACHE_USE_UNUSED;
    int64_t cache_age = 0, cache_life = 0;
    char coding[200];
    int coded = 0;
    uint32_t encoded = 0;
    coding[0] = 0;
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
        /* the cache: a fresh copy answers, and no request is made; a stale one with a
           validator asks the server whether it still holds */
        char here[8192], cpath[1300];
        net_url_text(&u, here, sizeof here);
        vector<char> c_head, c_body;
        int c_status = 0;
        int64_t c_rtime = 0, now = net_now(), c_age = 0, c_life = 0;
        int caching = net_cache_path(l, here, cpath, sizeof cpath);
        int cached = caching && net_cache_read(cpath, here, &c_status, &c_rtime, &c_head, &c_body);
        if (cached) {
            c_age = net_initial_age(data(c_head), len(c_head), c_rtime) + (now > c_rtime ? now - c_rtime : 0);
            c_life = net_lifetime(data(c_head), len(c_head), c_rtime);
            if (c_life > c_age) {
                append_range(body, data(c_body), len(c_body));
                status = c_status;
                net_type_of_head(data(c_head), len(c_head), type, sizeof type, charset, sizeof charset);
                cache = NET_CACHE_USE_FRESH;
                cache_age = c_age;
                cache_life = c_life;
                break;
            }
        }
        /* a top-level fetch is a navigation, same-site with itself; a page's part is
           same-site when its site is the page's */
        int same = !page || net_same_site(&u, page);
        vector<char> head, extra;
        net_cookie_header(l, &u, same, r, &extra);
        if (cached) {
            char v[1024], line[1100];
            if (net_header(data(c_head), len(c_head), "etag", v, sizeof v)) {
                int n = snprintf(line, sizeof line, "If-None-Match: %s\r\n", v);
                if (n > 0 && (size_t)n < sizeof line) append_range(extra, line, (size_t)n);
            }
            if (net_header(data(c_head), len(c_head), "last-modified", v, sizeof v)) {
                int n = snprintf(line, sizeof line, "If-Modified-Since: %s\r\n", v);
                if (n > 0 && (size_t)n < sizeof line) append_range(extra, line, (size_t)n);
            }
        }
        out = net_http(&u, t, data(extra), len(extra), &status, &head, &body, &rt);
        if (out != NET_OUTCOME_OK) { status = 0; break; }
        net_cookies_from(l, &u, data(head), len(head), same, r);
        if (status == 304 && cached) {
            /* revalidated: the stored response answers, its fields brought up to date by the 304's (4.3.4) */
            net_cache_write(cpath, here, c_status, now, data(head), len(head), data(c_head), len(c_head), data(c_body), len(c_body));
            clear(body);
            append_range(body, data(c_body), len(c_body));
            net_type_of_head(data(c_head), len(c_head), type, sizeof type, charset, sizeof charset);
            cache = NET_CACHE_USE_REVALIDATED;
            cache_age = c_age;
            cache_life = c_life;
            break;
        }
        net_type_of_head(data(head), len(head), type, sizeof type, charset, sizeof charset);
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
        /* the body out of its content codings */
        if (len(body)) {
            size_t sent = len(body);
            out = net_decode_body(data(head), len(head), &body, coding, sizeof coding, rt.reason, sizeof rt.reason);
            if (out != NET_OUTCOME_OK) break;
            if (coding[0]) { coded = 1; encoded = (uint32_t)sent; }
        }
        if (status >= 400) out = NET_OUTCOME_HTTP_ERROR;
        /* kept for later when it may be, and when a lifetime or a validator makes it of use */
        if (caching) {
            int64_t life = net_lifetime(data(head), len(head), now);
            char v[1024];
            int validator = net_header(data(head), len(head), "etag", v, sizeof v) || net_header(data(head), len(head), "last-modified", v, sizeof v);
            if (net_storable(status, data(head), len(head)) && (life > 0 || validator)) {
                net_cache_write(cpath, here, status, now, data(head), len(head), "", 0, data(body), len(body));
                cache = NET_CACHE_USE_STORED;
                cache_age = net_initial_age(data(head), len(head), now);
                cache_life = life;
            } else {
                if (cached) unlink(cpath);
                cache = NET_CACHE_USE_NOT_STORED;
            }
        }
        break;
    }
    net_url_text(&u, text, sizeof text);
    l->resource[r].final_url = net_text(l, text);
    net_origin_text(&u, origin, sizeof origin);
    l->resource[r].origin = net_text(l, origin);
    l->resource[r].outcome = out;
    l->resource[r].status = (uint32_t)status;
    l->resource[r].type = net_text(l, type);
    l->resource[r].charset = net_text(l, charset);
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
    l->resource[r].cross_site = (uint8_t)(page && !net_same_site(&u, page));
    l->resource[r].cache = cache;
    l->resource[r].cache_age = cache_age;
    l->resource[r].cache_lifetime = cache_life;
    l->resource[r].coded = (uint8_t)coded;
    if (coded) {
        l->resource[r].coding = net_text(l, coding);
        l->resource[r].encoded_size = encoded;
    }
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
    net_jar_write(l);
    l->revision++;
    return resource_handle(l, r);
}

/* ---- srcset and sizes: the image source an img takes (HTML 4.8.4.3) ---- */

static uint32_t net_attr(const dom_document *d, uint32_t x, const char *name);

/* the viewport images are chosen for, the cascade's (800 x 600), at a device pixel
   ratio of 1; a font-relative length in sizes is of the initial 16px */
#define NET_VIEW_W 800.0
#define NET_VIEW_H 600.0
#define NET_EM 16.0

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
    double per[15] = { 1, NET_EM, NET_EM, NET_EM / 2, NET_EM / 2, NET_VIEW_W / 100, NET_VIEW_H / 100, NET_VIEW_H / 100, NET_VIEW_W / 100,
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

/* one media feature in its parentheses: width, height and their min- and max-,
   in a range too ((width >= 600px), (400px < width <= 700px)), orientation,
   resolution (1dppx, 96dpi), prefers-color-scheme (light), prefers-reduced-motion
   (no-preference); 0 for a feature not known */
static int net_media_feature(const char *s, size_t n) {
    while (n && net_ascii_ws(s[n - 1])) n--;
    while (n && net_ascii_ws(s[0])) { s++; n--; }
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
                double have = k < 3 ? NET_VIEW_W : NET_VIEW_H;
                if (px < -1e299) return 0;
                return fcmp[k] == 0 ? have == px : fcmp[k] > 0 ? have >= px : have <= px;
            }
        if (net_ci_is(s, a, "orientation")) return net_ci_is(v, vn, "landscape");
        if (net_ci_is(s, a, "prefers-color-scheme")) return net_ci_is(v, vn, "light");
        if (net_ci_is(s, a, "prefers-reduced-motion")) return net_ci_is(v, vn, "no-preference");
        if (net_ci_is(s, a, "resolution") || net_ci_is(s, a, "min-resolution") || net_ci_is(s, a, "max-resolution")) {
            double d = -1;
            if (vn > 4 && net_ci_is(v + vn - 4, 4, "dppx")) net_html_float(v, vn - 4, &d);
            else if (vn > 1 && net_ci_is(v + vn - 1, 1, "x")) net_html_float(v, vn - 1, &d);
            else if (vn > 3 && net_ci_is(v + vn - 3, 3, "dpi")) { if (net_html_float(v, vn - 3, &d)) d /= 96; }
            if (d < 0) return 0;
            return net_ci_prefix(s, a, "min-") ? 1 >= d : net_ci_prefix(s, a, "max-") ? 1 <= d : d == 1;
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
        if (net_ci_is(t + b, i - b, "width")) { have = NET_VIEW_W; named = 1; }
        else if (net_ci_is(t + b, i - b, "height")) { have = NET_VIEW_H; named = 1; }
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
    return NET_VIEW_W;
}

/* the img's choice: its srcset's candidates, each density found (a width's by the
   source size of sizes), its src a 1x one when no candidate is 1x and none has a
   width; sorted by density, the first of equal ones kept; then as Blink picks at
   a device pixel ratio of 1, the first density at 1 or past it, else the greatest.
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
    double source = NET_VIEW_W;
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
    int w = total - 1;
    for (int i = 0; i < total; i++) if (density[order[i]] >= 1) { w = i; break; }
    while (w > 0 && density[order[w - 1]] == density[order[w]]) w--;
    int pick = order[w];
    if (pick == k) snprintf(url, cap, "%s", srcurl);
    else snprintf(url, cap, "%.*s", (int)c[pick].url_len, text + c[pick].url);
    pk->density = density[pick];
    pk->source_size = source;
    pk->candidates = total;
    snprintf(pk->desc, sizeof pk->desc, "%s", pick == k ? "" : c[pick].desc);
    return 1;
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
        char ref[8192], abs[8192];
        net_pick pk;
        int picked = 0;
        uint32_t fetched = 0;
        if (net_is(doc, x, "link") && net_stylesheet_rel(doc, x)) { kind = NET_KIND_STYLESHEET; a = net_attr(doc, x, "href"); }
        else if (net_is(doc, x, "img")) {
            kind = NET_KIND_IMAGE;
            a = net_attr(doc, x, "src");
            /* an img with a srcset takes the candidate chosen for the viewport */
            uint32_t ss = net_attr(doc, x, "srcset");
            if (ss && doc->attr[ss].value.len) picked = net_srcset_choose(doc, x, ss, a, ref, sizeof ref, &pk);
        } else if (net_is(doc, x, "script")) { kind = NET_KIND_SCRIPT; a = net_attr(doc, x, "src"); }
        if (picked || (a && doc->attr[a].value.len > 0)) {
            if (!picked) {
                amb_span v = doc->attr[a].value;
                snprintf(ref, sizeof ref, "%.*s", (int)v.len, text_ptr(doc, v));
            }
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
                fetched = r;
            } else if (!net_resolve(&base, ref, &u)) {
                fetched = net_get(l, ref, kind, pg, eh, &base, &t);
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
                    fetched = h.index;
                    /* the first's choice is not this one's: the size again from the header */
                    l->resource[fetched].srcset = 0;
                    l->resource[fetched].descriptor = net_text(l, "");
                    if (kind == NET_KIND_IMAGE && copy.outcome == NET_OUTCOME_OK && copy.body.len) {
                        uint32_t w = 0, hh = 0;
                        net_image_size_of((const uint8_t *)bytes_ptr(l, copy.body), copy.body.len, &w, &hh);
                        l->resource[fetched].width = w;
                        l->resource[fetched].height = hh;
                    }
                } else fetched = net_get(l, abs, kind, pg, eh, &base, &t);
            }
            if (picked && fetched) net_srcset_size(l, fetched, &pk);
        } else if (kind == NET_KIND_IMAGE && a) {
            /* an img whose src is empty: its request is broken at once (HTML's update the
               image data, step 9), and Chromium draws it broken; recorded, not fetched */
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
            l->resource[r].reason = net_text(l, "the src attribute is empty");
        }
        if (doc->node[x].first_child) { x = doc->node[x].first_child; continue; }
        while (x && !doc->node[x].next_sibling) { x = doc->node[x].parent; if (x == 1) x = 0; }
        if (x) x = doc->node[x].next_sibling;
    }
    net_jar_write(l);
    l->revision++;
    return NET_OK;
}
