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
   body (base64 or percent-encoded); http: is HTTP/1.1 over a socket -- GET with
   Connection: close, the response's Content-Length or chunked body or the
   bytes until close, at most five redirects, ten seconds a read, 32 MB a body.
   https: and the rest are unsupported_scheme. What a page asks for: <link
   rel=stylesheet href>, <img src>, <script src> (fetched, never run), each
   URL fetched once. A page served over http: or https: may not load a file:
   URL: blocked, here in C; nor any kind of resource its profile denies its site. An image's size is read from its header (PNG, GIF,
   JPEG); its pixels are not decoded. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
   finding 11). The layout is the LP64 one, as on x86-64 and arm64 Linux. */
typedef struct net_addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct net_addrinfo *ai_next;
} net_addrinfo;
int getaddrinfo(const char *node, const char *service, const net_addrinfo *hints, net_addrinfo **res);
void freeaddrinfo(own net_addrinfo *res);

#define NET_MAX_BODY (32u << 20)
#define NET_MAX_REDIRECTS 5

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

/* ---- bodies: base64, percent-decoding ---- */

static int net_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = net_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
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

/* ---- the three schemes ---- */

static net_outcome net_get_file(const net_url *u, vector<char> *body, char *type, size_t tcap) {
    vector<char> path;
    net_percent_decode(u->path, strlen(u->path), &path);
    push_back(path, 0);
    own FILE *f = fopen(data(path), "rb");
    if (!f) return NET_OUTCOME_NO_SUCH_FILE;
    defer(f) { fclose(f); }
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) {
        append_range(body, buf, k);
        if (len(*body) > NET_MAX_BODY) return NET_OUTCOME_TOO_LARGE;
    }
    if (ferror(f)) return NET_OUTCOME_NO_SUCH_FILE;
    snprintf(type, tcap, "%s", net_type_of_name(u->path));
    return NET_OUTCOME_OK;
}

static net_outcome net_get_data(const net_url *u, vector<char> *body, char *type, size_t tcap) {
    const char *s = u->path;
    size_t n = strlen(s), comma = 0;
    while (comma < n && s[comma] != ',') comma++;
    if (comma >= n) return NET_OUTCOME_MALFORMED;
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

static int net_dial(const char *host, int port) {
    net_addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    own net_addrinfo *res = 0;
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) return -2;
    defer(res) { freeaddrinfo(res); }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) return -1;
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { close(fd); return -1; }
    return fd;
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
                while (b > a && (head[b - 1] == '\r' || head[b - 1] == ' ')) b--;
                snprintf(out, cap, "%.*s", (int)(b - a), head + a);
                return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

/* one HTTP exchange: GET the URL; its status, head and body (chunks joined) */
static net_outcome net_http_once(const net_url *u, int *status, vector<char> *head, vector<char> *body) {
    int port = u->port >= 0 ? u->port : 80;
    int fd = net_dial(u->host, port);
    if (fd == -2) return NET_OUTCOME_UNREACHABLE;
    if (fd < 0) return NET_OUTCOME_REFUSED;
    char req[8192], host[300];
    if (u->port >= 0) snprintf(host, sizeof host, "%s:%d", u->host, u->port);
    else snprintf(host, sizeof host, "%s", u->host);
    int rn = snprintf(req, sizeof req, "GET %s%s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Ambitious/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                      u->path, u->has_query ? "?" : "", u->query, host);
    if (rn < 0 || (size_t)rn >= sizeof req || send(fd, req, (size_t)rn, 0) != rn) { close(fd); return NET_OUTCOME_REFUSED; }
    vector<char> all;
    char buf[16384];
    ssize_t k;
    while ((k = recv(fd, buf, sizeof buf, 0)) > 0) {
        append_range(all, buf, (size_t)k);
        if (len(all) > NET_MAX_BODY + 65536) { close(fd); return NET_OUTCOME_TOO_LARGE; }
    }
    close(fd);
    /* the head: the status line and the headers, to the blank line */
    size_t n = len(all), he = 0;
    while (he + 3 < n && !(at(all, he) == '\r' && at(all, he + 1) == '\n' && at(all, he + 2) == '\r' && at(all, he + 3) == '\n')) he++;
    if (he + 3 >= n || n < 12 || memcmp(data(all), "HTTP/1.", 7) != 0) return NET_OUTCOME_MALFORMED;
    append_range(head, data(all), he + 2);
    *status = (at(all, 9) - '0') * 100 + (at(all, 10) - '0') * 10 + (at(all, 11) - '0');
    size_t b = he + 4;
    char v[64];
    if (net_header(data(*head), len(*head), "transfer-encoding", v, sizeof v) && strstr(v, "chunked")) {
        /* chunks: a hex size, its bytes, to the empty one */
        while (b < n) {
            size_t e = b;
            long size = 0;
            int any = 0;
            while (e < n && net_hexval(at(all, e)) >= 0) { size = size * 16 + net_hexval(at(all, e)); e++; any = 1; if (size > (long)NET_MAX_BODY) return NET_OUTCOME_TOO_LARGE; }
            if (!any) return NET_OUTCOME_MALFORMED;
            while (e < n && at(all, e) != '\n') e++;
            e++;
            if (size == 0) break;
            if (e + (size_t)size > n) return NET_OUTCOME_MALFORMED;
            append_range(body, data(all) + e, (size_t)size);
            b = e + (size_t)size;
            if (b < n && at(all, b) == '\r') b++;
            if (b < n && at(all, b) == '\n') b++;
        }
        return NET_OUTCOME_OK;
    }
    size_t take = n - b;
    if (net_header(data(*head), len(*head), "content-length", v, sizeof v)) {
        long cl = atol(v);
        if (cl >= 0 && (size_t)cl < take) take = (size_t)cl;
    }
    append_range(body, data(all) + b, take);
    return NET_OUTCOME_OK;
}

/* ---- the loader's records ---- */

static amb_span net_text(net_loader *l, const char *s) { return text_add(l, s, strlen(s)); }

/* a fetch, its redirects followed, recorded: the record's index */
static uint32_t net_get(net_loader *l, const char *url, net_kind kind, uint32_t from, dom_node_h elem, const net_url *page) {
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
        return r;
    }
    vector<char> body;
    net_outcome out = NET_OUTCOME_OK;
    int status = 200;
    for (int hop = 0;; hop++) {
        /* a page served over the network may not read the machine's files */
        if (page && strcmp(u.scheme, "file") == 0 && strcmp(page->scheme, "file") != 0 && !page->opaque) { out = NET_OUTCOME_BLOCKED; status = 0; break; }
        clear(body);
        if (strcmp(u.scheme, "file") == 0) { out = net_get_file(&u, &body, type, sizeof type); status = out == NET_OUTCOME_OK ? 200 : 404; break; }
        if (strcmp(u.scheme, "data") == 0) { out = net_get_data(&u, &body, type, sizeof type); status = out == NET_OUTCOME_OK ? 200 : 0; break; }
        if (strcmp(u.scheme, "http") != 0) { out = NET_OUTCOME_UNSUPPORTED_SCHEME; status = 0; break; }
        vector<char> head;
        out = net_http_once(&u, &status, &head, &body);
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
            if (hop >= NET_MAX_REDIRECTS) { out = NET_OUTCOME_TOO_MANY_REDIRECTS; break; }
            net_url next;
            if (!net_resolve(&u, loc, &next)) { out = NET_OUTCOME_BAD_URL; break; }
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
    uint32_t r = net_get(l, u, kind, 0, none, 0);
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
            } else if (!net_resolve(&base, ref, &u)) {
                uint32_t r = net_get(l, ref, kind, pg, eh, &base);
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
                } else net_get(l, abs, kind, pg, eh, &base);
            }
        }
        if (doc->node[x].first_child) { x = doc->node[x].first_child; continue; }
        while (x && !doc->node[x].next_sibling) { x = doc->node[x].parent; if (x == 1) x = 0; }
        if (x) x = doc->node[x].next_sibling;
    }
    l->revision++;
    return NET_OK;
}
