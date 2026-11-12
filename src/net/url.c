/* src/net/url.c -- URLs as the URL standard parses them (url.spec.whatwg.org, the
   living standard of 19 October 2026): the basic URL parser's state machine (4.4),
   the host parser (3.5), the serializers (3.6, 4.5), the URL API's getters (6.1) and
   origins (4.7). Its tests are web-platform-tests' urltestdata.json, every case
   (tests/exec/net_urltestdata.c).

   The parser reads bytes of UTF-8: every code point it treats apart is ASCII, and
   every percent-encode set holds each code point past U+007E, so a code point past
   ASCII is percent-encoded byte by byte wherever one is. The input loses its
   leading and trailing C0 controls and spaces and every tab and newline; then the
   states: a scheme, special (ftp, file, http, https, ws, wss, with their default
   ports) or not; for a special one an authority after its slashes or backslashes,
   or the base's (relative, relative slash); user info, each code point
   percent-encoded in the userinfo set and an `@' seen twice written %40; a host; a
   port of at most 65535, its scheme's default dropped; a path of segments, each in
   the path percent-encode set, `.' and `..' (and %2e for a dot) applied, a
   backslash a slash in a special URL; the file: scheme's own states (an empty host,
   localhost made empty, Windows drive letters: `c|' made `c:', a drive letter
   never popped and kept from a file: base); a non-special URL's opaque path, in
   the C0 control set, a space before `?' or `#' written %20; a query in the query
   set, or for a special URL the special-query set; a fragment in the fragment set.
   The serializer writes `/.' before a path that begins with an empty segment when
   the host is null.

   A host: an IPv6 address in brackets (eight pieces, `::' a run of zeros, the last
   two perhaps a dotted quad; written back lowercase, the first longest run of two
   or more zero pieces compressed); for a non-special URL an opaque host, refused
   for a forbidden host code point and percent-encoded in the C0 control set; else
   percent-decoded, decoded from UTF-8, made ASCII by the domain parser (3.3) and
   refused for an empty result or a forbidden domain code point, then an IPv4
   address when its last label is a number (one to four parts, each decimal, 0x hex
   or 0 octal, the last filling what the others leave).

   The domain parser: an ASCII domain lowercased, as the standard has it for one
   whatever IDNA would say of it; a domain past ASCII decoded and each code point
   lowercased as UTS #46 maps capitals (Unicode's lowercase mappings, src/net/lower.h),
   ASCII's fullwidth forms and the ideographic full stops mapped, and each label past
   ASCII written xn-- and its Punycode (RFC 3492). Not done: the rest of IDNA's
   mapping table, NFC, and the validity, bidi and joiner rules.

   The query is percent-encoded from UTF-8: a document in a legacy encoding does not
   encode its URLs' queries in its own (the standard's encoding argument). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector.pl>
#include "url.h"
#include "lower.h"

#define URL_EOF (-1)

/* the parser's states */
enum { URL_ST_NONE, URL_ST_SCHEME_START, URL_ST_SCHEME, URL_ST_NO_SCHEME, URL_ST_SPECIAL_RELATIVE_OR_AUTHORITY,
       URL_ST_PATH_OR_AUTHORITY, URL_ST_RELATIVE, URL_ST_RELATIVE_SLASH, URL_ST_SPECIAL_AUTHORITY_SLASHES,
       URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES, URL_ST_AUTHORITY, URL_ST_HOST, URL_ST_HOSTNAME, URL_ST_PORT, URL_ST_FILE,
       URL_ST_FILE_SLASH, URL_ST_FILE_HOST, URL_ST_PATH_START, URL_ST_PATH, URL_ST_OPAQUE_PATH, URL_ST_QUERY, URL_ST_FRAGMENT };

/* the percent-encode sets (1.3) */
enum { URL_SET_C0, URL_SET_FRAGMENT, URL_SET_QUERY, URL_SET_SPECIAL_QUERY, URL_SET_PATH, URL_SET_USERINFO, URL_SET_COMPONENT,
       URL_SET_FORM };

/* a URL record's parts that are not text: what its host is, its port, whether its
   query and fragment are null, whether its path is opaque, its special scheme */
typedef struct url_rec {
    int host_kind;
    int32_t port;
    int query_null, fragment_null, opaque, special;
} url_rec;

static int url_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int url_digit(int c) { return c >= '0' && c <= '9'; }
static int url_hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static char url_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* the special schemes: 1 ftp, 2 file, 3 http, 4 https, 5 ws, 6 wss; 0 for another */
static int url_special_of(const char *s, size_t n) {
    if (n == 3 && memcmp(s, "ftp", 3) == 0) return 1;
    if (n == 4 && memcmp(s, "file", 4) == 0) return 2;
    if (n == 4 && memcmp(s, "http", 4) == 0) return 3;
    if (n == 5 && memcmp(s, "https", 5) == 0) return 4;
    if (n == 2 && memcmp(s, "ws", 2) == 0) return 5;
    if (n == 3 && memcmp(s, "wss", 3) == 0) return 6;
    return 0;
}
static int32_t url_default_port(int special) {
    if (special == 1) return 21;
    if (special == 3 || special == 5) return 80;
    if (special == 4 || special == 6) return 443;
    return -1;
}

/* whether the byte c is in the percent-encode set: every byte past U+007E and every C0
   control is in each */
static int url_in_set(int set, int c) {
    if (c < 0x20 || c > 0x7E) return 1;
    if (set == URL_SET_C0) return 0;
    if (set == URL_SET_FRAGMENT) return c == ' ' || c == '"' || c == '<' || c == '>' || c == '`';
    int q = c == ' ' || c == '"' || c == '#' || c == '<' || c == '>';
    if (set == URL_SET_QUERY) return q;
    if (set == URL_SET_SPECIAL_QUERY) return q || c == '\'';
    int pth = q || c == '?' || c == '^' || c == '`' || c == '{' || c == '}';
    if (set == URL_SET_PATH) return pth;
    int u = pth || c == '/' || c == ':' || c == ';' || c == '=' || c == '@' || (c >= '[' && c <= ']') || c == '|';
    if (set == URL_SET_USERINFO) return u;
    int co = u || (c >= '$' && c <= '&') || c == '+' || c == ',';
    if (set == URL_SET_COMPONENT) return co;
    return co || c == '!' || (c >= '\'' && c <= ')') || c == '~';
}

static const char url_hex[] = "0123456789ABCDEF";

/* the byte c appended to out, percent-encoded when the set holds it */
static void url_pct(vector<char> *out, int c, int set) {
    if (!url_in_set(set, c)) { push_back(out, (char)c); return; }
    push_back(out, '%');
    push_back(out, url_hex[(c >> 4) & 15]);
    push_back(out, url_hex[c & 15]);
}

static void url_put(vector<char> *out, const char *s, size_t n) { append_range(out, s, n); }
static void url_puts(vector<char> *out, const char *s) { append_range(out, s, strlen(s)); }
static int url_eq(const vector<char> *v, const char *s) {
    size_t n = strlen(s);
    return len(*v) == n && (n == 0 || memcmp(data(*v), s, n) == 0);
}

/* ---- paths ---- */

/* a Windows drive letter: an ASCII letter and `:' or `|'; normalized, `:' alone */
static int url_wdl(const char *s, size_t n) { return n == 2 && url_alpha((unsigned char)s[0]) && (s[1] == ':' || s[1] == '|'); }
static int url_wdl_normalized(const char *s, size_t n) { return n == 2 && url_alpha((unsigned char)s[0]) && s[1] == ':'; }
/* a string that starts with a Windows drive letter: its first two code points one,
   then the end or one of / \ ? # */
static int url_starts_wdl(const char *s, size_t n) {
    if (n < 2 || !url_wdl(s, 2)) return 0;
    return n == 2 || s[2] == '/' || s[2] == '\\' || s[2] == '?' || s[2] == '#';
}

/* a single-dot segment: "." or %2e; a double-dot one: "..", .%2e, %2e. or %2e%2e */
static int url_dot1(const char *s, size_t n) {
    return (n == 1 && s[0] == '.') || (n == 3 && s[0] == '%' && s[1] == '2' && (s[2] == 'e' || s[2] == 'E'));
}
static int url_dot2(const char *s, size_t n) {
    if (n == 2) return s[0] == '.' && s[1] == '.';
    if (n == 4) return (s[0] == '.' && url_dot1(s + 1, 3)) || (url_dot1(s, 3) && s[3] == '.');
    if (n == 6) return url_dot1(s, 3) && url_dot1(s + 3, 3);
    return 0;
}

/* the path is kept as its serialization, each segment after a `/': how many segments */
static size_t url_segs(const vector<char> *path) {
    size_t k = 0, n = len(*path);
    const char *d = data(*path);
    for (size_t i = 0; i < n; i++) if (d[i] == '/') k++;
    return k;
}

/* shorten a path (4.2): its last segment removed, unless it is a file: URL's one
   normalized drive letter */
static void url_shorten(vector<char> *path, int special) {
    size_t n = len(*path);
    if (n == 0) return;
    if (special == 2 && url_segs(path) == 1 && url_wdl_normalized(data(*path) + 1, n - 1)) return;
    size_t i = n;
    while (i > 0 && data(*path)[i - 1] != '/') i--;
    resize(path, i > 0 ? i - 1 : 0);
}

/* ---- hosts: IPv4 and IPv6 ---- */

/* an IPv4 address's part (3.5's IPv4 number parser): 0x and hex, a leading 0 and
   octal, else decimal; -1 for none; past 2^32 kept at 2^32 */
static int64_t url_ipv4_number(const char *s, size_t n) {
    if (n == 0) return -1;
    int radix = 10;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; n -= 2; radix = 16; }
    else if (n >= 2 && s[0] == '0') { s++; n--; radix = 8; }
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int d = url_hexval((unsigned char)s[i]);
        if (d < 0 || d >= radix) return -1;
        v = v * radix + d;
        if (v > 0x100000000ll) v = 0x100000000ll;
    }
    return v;
}

/* the ends-in-a-number checker: the last label, a last empty one dropped, all digits
   or an IPv4 number */
static int url_ends_in_number(const char *s, size_t n) {
    if (n && s[n - 1] == '.') n--;
    size_t b = n;
    while (b > 0 && s[b - 1] != '.') b--;
    if (b == n) return 0;
    int digits = 1;
    for (size_t i = b; i < n; i++) if (!url_digit((unsigned char)s[i])) digits = 0;
    if (digits) return 1;
    return url_ipv4_number(s + b, n - b) >= 0;
}

/* the IPv4 parser: one to four parts, a last empty one dropped, each but the last a
   byte, the last filling what the others leave */
static int url_ipv4_parse(const char *s, size_t n, uint32_t *out) {
    size_t b[5], e[5];
    int k = 0;
    size_t a = 0;
    for (;;) {
        size_t c = a;
        while (c < n && s[c] != '.') c++;
        if (k == 5) return 0;
        b[k] = a;
        e[k] = c;
        k++;
        if (c >= n) break;
        a = c + 1;
    }
    if (k > 1 && b[k - 1] == e[k - 1]) k--;
    if (k > 4) return 0;
    int64_t v[4];
    for (int i = 0; i < k; i++) {
        v[i] = url_ipv4_number(s + b[i], e[i] - b[i]);
        if (v[i] < 0) return 0;
    }
    for (int i = 0; i < k - 1; i++) if (v[i] > 255) return 0;
    if (v[k - 1] >= (int64_t)1 << (8 * (5 - k))) return 0;
    uint64_t r = (uint64_t)v[k - 1];
    for (int i = 0; i < k - 1; i++) r += (uint64_t)v[i] << (8 * (3 - i));
    *out = (uint32_t)r;
    return 1;
}

/* the IPv6 parser: eight pieces, one `::' standing for a run of zeros, the last two
   pieces perhaps a dotted quad */
static int url_ipv6_parse(const char *s, size_t n, uint16_t *a) {
    for (int i = 0; i < 8; i++) a[i] = 0;
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
        while (length < 4 && p < n && url_hexval((unsigned char)s[p]) >= 0) {
            value = value * 16 + (uint32_t)url_hexval((unsigned char)s[p]);
            p++;
            length++;
        }
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
                if (p >= n || !url_digit((unsigned char)s[p])) return 0;
                while (p < n && url_digit((unsigned char)s[p])) {
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

/* the IPv6 serializer, in brackets: lowercase hex pieces, the first longest run of
   two or more zero pieces written "::" */
static void url_ipv6_text(const uint16_t *a, vector<char> *out) {
    int best = -1, bestlen = 1;
    for (int i = 0; i < 8;) {
        if (a[i] != 0) { i++; continue; }
        int j = i;
        while (j < 8 && a[j] == 0) j++;
        if (j - i > bestlen) { best = i; bestlen = j - i; }
        i = j;
    }
    push_back(out, '[');
    int ignore0 = 0;
    for (int i = 0; i < 8; i++) {
        if (ignore0 && a[i] == 0) continue;
        ignore0 = 0;
        if (best == i) {
            url_puts(out, i == 0 ? "::" : ":");
            ignore0 = 1;
            continue;
        }
        char t[8];
        int k = snprintf(t, sizeof t, "%x", a[i]);
        url_put(out, t, (size_t)k);
        if (i != 7) push_back(out, ':');
    }
    push_back(out, ']');
}

/* ---- domains made ASCII ---- */

/* a code point's small letter, as UTS #46 maps a capital: Unicode's simple
   lowercase mapping, by the runs of src/net/lower.h */
static uint32_t url_lower_cp(uint32_t c) {
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
   surrogate, past U+10FFFF, a sequence cut short), which the standard's decoding
   makes U+FFFD and the domain parser then refuses */
static int url_utf8(const char *s, size_t n, vector<uint32_t> *cps) {
    size_t i = 0;
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
        push_back(cps, c);
        i += 1 + (size_t)more;
    }
    return 1;
}

/* RFC 3492's bias adaptation */
static uint32_t url_puny_adapt(uint32_t delta, uint32_t points, int first) {
    delta = first ? delta / 700 : delta / 2;
    delta += delta / points;
    uint32_t k = 0;
    while (delta > ((36 - 1) * 26) / 2) { delta /= 36 - 1; k += 36; }
    return k + (36 * delta) / (delta + 38);
}

/* a label's code points cp[a, b) in Punycode (RFC 3492) onto out: its ASCII ones in
   order, a "-" when there are any, then the others as deltas in base 36; 0 on an
   overflow */
static int url_punycode(const vector<uint32_t> *cps, size_t a, size_t b, vector<char> *out) {
    const uint32_t *cp = data(*cps);
    size_t n = b - a, basic = 0;
    for (size_t i = a; i < b; i++)
        if (cp[i] < 0x80) { push_back(out, (char)cp[i]); basic++; }
    if (basic) push_back(out, '-');
    uint32_t next = 0x80, delta = 0, bias = 72;
    size_t h = basic;
    while (h < n) {
        uint32_t m = 0xFFFFFFFFu;
        for (size_t i = a; i < b; i++) if (cp[i] >= next && cp[i] < m) m = cp[i];
        if ((uint64_t)(m - next) * (h + 1) + delta > 0xFFFFFFFFu) return 0;
        delta += (m - next) * (uint32_t)(h + 1);
        next = m;
        for (size_t i = a; i < b; i++) {
            if (cp[i] < next && ++delta == 0) return 0;
            if (cp[i] != next) continue;
            uint32_t q = delta;
            for (uint32_t t36 = 36;; t36 += 36) {
                uint32_t t = t36 <= bias ? 1 : t36 >= bias + 26 ? 26 : t36 - bias;
                if (q < t) break;
                uint32_t d = t + (q - t) % (36 - t);
                push_back(out, (char)(d < 26 ? 'a' + d : '0' + d - 26));
                q = (q - t) / (36 - t);
            }
            push_back(out, (char)(q < 26 ? 'a' + q : '0' + q - 26));
            bias = url_puny_adapt(delta, (uint32_t)h + 1, h == basic);
            delta = 0;
            h++;
        }
        delta++;
        next++;
    }
    return 1;
}

/* a domain past ASCII made ASCII as UTS #46 makes a URL's host, in part: its UTF-8
   decoded; each code point lowercased (U+0130 to i and U+0307, as Unicode lowercases
   it), ASCII's fullwidth forms made ASCII and the ideographic full stops "."; then
   each label holding a code point past ASCII written xn-- and its Punycode */
static int url_domain_to_ascii(const char *s, size_t n, vector<char> *out) {
    vector<uint32_t> cps, m;
    if (!url_utf8(s, n, &cps)) return 0;
    for (size_t i = 0; i < len(cps); i++) {
        uint32_t c = cps[i];
        if (c == 0x3002 || c == 0xFF0E || c == 0xFF61) push_back(m, (uint32_t)'.');
        else if (c == 0x130) { push_back(m, (uint32_t)'i'); push_back(m, 0x307u); }
        else if (c >= 0xFF01 && c <= 0xFF5E) push_back(m, url_lower_cp(c - 0xFEE0));
        else push_back(m, url_lower_cp(c));
    }
    size_t nm = len(m), a = 0;
    while (a <= nm) {
        size_t b = a;
        while (b < nm && m[b] != '.') b++;
        int wide = 0;
        for (size_t i = a; i < b; i++) if (m[i] >= 0x80) wide = 1;
        if (wide) {
            url_puts(out, "xn--");
            if (!url_punycode(&m, a, b, out)) return 0;
        } else
            for (size_t i = a; i < b; i++) push_back(out, (char)m[i]);
        if (b < nm) push_back(out, '.');
        a = b + 1;
    }
    return 1;
}

/* a forbidden host code point (3.2), and a forbidden domain code point: those, the
   C0 controls, `%' and DEL */
static int url_forbidden_host(int c) {
    return c == 0 || c == '\t' || c == '\n' || c == '\r' || c == ' ' || c == '#' || c == '/' || c == ':' || c == '<' || c == '>' ||
           c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' || c == '^' || c == '|';
}
static int url_forbidden_domain(int c) { return url_forbidden_host(c) || (c >= 0 && c <= 0x1F) || c == '%' || c == 0x7F; }

/* the domain parser (3.3), beStrict false: an ASCII domain lowercased, another made
   ASCII; refused when empty or holding a forbidden domain code point */
static int url_domain_parse(const char *s, size_t n, vector<char> *out) {
    clear(out);
    int ascii = 1;
    for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) ascii = 0;
    if (ascii) for (size_t i = 0; i < n; i++) push_back(out, url_lower(s[i]));
    else if (!url_domain_to_ascii(s, n, out)) return 0;
    size_t k = len(*out);
    if (k == 0) return 0;
    for (size_t i = 0; i < k; i++) if (url_forbidden_domain((uint8_t)data(*out)[i])) return 0;
    return 1;
}

/* the host parser (3.5): s[0, n) as a host into out, serialized, its kind in *kind;
   opaque for a URL that is not special. 0 for failure */
static int url_host_parse(const char *s, size_t n, int opaque, vector<char> *out, int *kind) {
    clear(out);
    if (n && s[0] == '[') {
        uint16_t a[8];
        if (n < 2 || s[n - 1] != ']' || !url_ipv6_parse(s + 1, n - 2, a)) return 0;
        url_ipv6_text(a, out);
        *kind = URL_HOST_IPV6;
        return 1;
    }
    if (opaque) {
        for (size_t i = 0; i < n; i++) if (url_forbidden_host((uint8_t)s[i])) return 0;
        for (size_t i = 0; i < n; i++) url_pct(out, (uint8_t)s[i], URL_SET_C0);
        *kind = n ? URL_HOST_OPAQUE : URL_HOST_EMPTY;
        return 1;
    }
    /* percent-decoded */
    vector<char> d, ascii;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && url_hexval((uint8_t)s[i + 1]) >= 0 && url_hexval((uint8_t)s[i + 2]) >= 0) {
            push_back(d, (char)(url_hexval((uint8_t)s[i + 1]) * 16 + url_hexval((uint8_t)s[i + 2])));
            i += 2;
        } else push_back(d, s[i]);
    }
    if (!url_domain_parse(data(d), len(d), &ascii)) return 0;
    size_t an = len(ascii);
    if (url_ends_in_number(data(ascii), an)) {
        uint32_t v;
        if (!url_ipv4_parse(data(ascii), an, &v)) return 0;
        char t[20];
        int k = snprintf(t, sizeof t, "%u.%u.%u.%u", v >> 24, v >> 16 & 255, v >> 8 & 255, v & 255);
        url_put(out, t, (size_t)k);
        *kind = URL_HOST_IPV4;
        return 1;
    }
    append_range(out, data(ascii), an);
    *kind = URL_HOST_DOMAIN;
    return 1;
}

/* ---- the basic URL parser (4.4) ---- */

/* the base's component [a, b) of its serialization copied into v */
static void url_take(vector<char> *v, const char *bh, uint32_t a, uint32_t b) {
    clear(v);
    if (b > a) append_range(v, bh + a, b - a);
}

/* in[0, n) parsed into the record (scheme ... fragment, and r), against the base bh
   with its parts bp (none when bh is 0), from the state override (URL_ST_NONE for
   none): 1, or 0 for failure. With a state override the record is a URL's own, and
   a return with 1 may have changed nothing, as the standard's plain return does */
static int url_basic(const char *in, size_t n, const char *bh, const url_parts *bp, vector<char> *scheme, vector<char> *user,
                     vector<char> *pass, vector<char> *host, vector<char> *path, vector<char> *query, vector<char> *frag,
                     url_rec *r, int override) {
    vector<char> buf;
    int state = override ? override : URL_ST_SCHEME_START;
    int at_seen = 0, brackets = 0, pass_seen = 0;
    long p = 0, nn = (long)n;
    for (;;) {
        int c = p < nn ? (int)(uint8_t)in[p] : URL_EOF;
        int special = r->special;
        if (state == URL_ST_SCHEME_START) {
            if (url_alpha(c)) { push_back(buf, url_lower((char)c)); state = URL_ST_SCHEME; }
            else if (!override) { state = URL_ST_NO_SCHEME; p--; }
            else return 0;
        } else if (state == URL_ST_SCHEME) {
            if (url_alpha(c) || url_digit(c) || c == '+' || c == '-' || c == '.') push_back(buf, url_lower((char)c));
            else if (c == ':') {
                int sp = url_special_of(data(buf), len(buf));
                if (override) {
                    if (r->special && !sp) return 1;
                    if (!r->special && sp) return 1;
                    if ((len(*user) || len(*pass) || r->port >= 0) && sp == 2) return 1;
                    if (r->special == 2 && r->host_kind == URL_HOST_EMPTY) return 1;
                }
                clear(scheme);
                append_range(scheme, data(buf), len(buf));
                r->special = sp;
                if (override) {
                    if (r->port == url_default_port(sp)) r->port = -1;
                    return 1;
                }
                clear(buf);
                int same_base = bh && sp && bp->special == sp;
                if (sp == 2) state = URL_ST_FILE;
                else if (same_base) state = URL_ST_SPECIAL_RELATIVE_OR_AUTHORITY;
                else if (sp) state = URL_ST_SPECIAL_AUTHORITY_SLASHES;
                else if (p + 1 < nn && in[p + 1] == '/') { state = URL_ST_PATH_OR_AUTHORITY; p++; }
                else { clear(path); r->opaque = 1; state = URL_ST_OPAQUE_PATH; }
            } else if (!override) { clear(buf); state = URL_ST_NO_SCHEME; p = -1; }
            else return 0;
        } else if (state == URL_ST_NO_SCHEME) {
            if (!bh || (bp->opaque && c != '#')) return 0;
            if (bp->opaque) {
                url_take(scheme, bh, 0, bp->scheme_end);
                r->special = bp->special;
                url_take(path, bh, bp->path_start, bp->path_end);
                r->opaque = 1;
                r->query_null = bp->query_start == URL_NONE;
                if (!r->query_null) url_take(query, bh, bp->query_start, bp->query_end);
                clear(frag);
                r->fragment_null = 0;
                state = URL_ST_FRAGMENT;
            } else if (bp->special != 2) { state = URL_ST_RELATIVE; p--; }
            else { state = URL_ST_FILE; p--; }
        } else if (state == URL_ST_SPECIAL_RELATIVE_OR_AUTHORITY) {
            if (c == '/' && p + 1 < nn && in[p + 1] == '/') { state = URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES; p++; }
            else { state = URL_ST_RELATIVE; p--; }
        } else if (state == URL_ST_PATH_OR_AUTHORITY) {
            if (c == '/') state = URL_ST_AUTHORITY;
            else { state = URL_ST_PATH; p--; }
        } else if (state == URL_ST_RELATIVE) {
            url_take(scheme, bh, 0, bp->scheme_end);
            r->special = bp->special;
            special = r->special;
            if (c == '/' || (special && c == '\\')) state = URL_ST_RELATIVE_SLASH;
            else {
                url_take(user, bh, bp->user_start, bp->user_end);
                url_take(pass, bh, bp->pass_start, bp->pass_end);
                url_take(host, bh, bp->host_start, bp->host_end);
                r->host_kind = bp->host_kind;
                r->port = bp->port;
                url_take(path, bh, bp->path_start, bp->path_end);
                r->query_null = bp->query_start == URL_NONE;
                if (!r->query_null) url_take(query, bh, bp->query_start, bp->query_end);
                else clear(query);
                if (c == '?') { clear(query); r->query_null = 0; state = URL_ST_QUERY; }
                else if (c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
                else if (c != URL_EOF) {
                    clear(query);
                    r->query_null = 1;
                    url_shorten(path, special);
                    state = URL_ST_PATH;
                    p--;
                }
            }
        } else if (state == URL_ST_RELATIVE_SLASH) {
            if (special && (c == '/' || c == '\\')) state = URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES;
            else if (c == '/') state = URL_ST_AUTHORITY;
            else {
                url_take(user, bh, bp->user_start, bp->user_end);
                url_take(pass, bh, bp->pass_start, bp->pass_end);
                url_take(host, bh, bp->host_start, bp->host_end);
                r->host_kind = bp->host_kind;
                r->port = bp->port;
                state = URL_ST_PATH;
                p--;
            }
        } else if (state == URL_ST_SPECIAL_AUTHORITY_SLASHES) {
            if (c == '/' && p + 1 < nn && in[p + 1] == '/') { state = URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES; p++; }
            else { state = URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES; p--; }
        } else if (state == URL_ST_SPECIAL_AUTHORITY_IGNORE_SLASHES) {
            if (c != '/' && c != '\\') { state = URL_ST_AUTHORITY; p--; }
        } else if (state == URL_ST_AUTHORITY) {
            if (c == '@') {
                if (at_seen) insert_range(buf, 0, "%40", 3);
                at_seen = 1;
                size_t bn = len(buf);
                for (size_t i = 0; i < bn; i++) {
                    int b = (uint8_t)buf[i];
                    if (b == ':' && !pass_seen) pass_seen = 1;
                    else if (pass_seen) url_pct(pass, b, URL_SET_USERINFO);
                    else url_pct(user, b, URL_SET_USERINFO);
                }
                clear(buf);
            } else if (c == URL_EOF || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                if (at_seen && len(buf) == 0) return 0;
                p -= (long)len(buf) + 1;
                clear(buf);
                state = URL_ST_HOST;
            } else push_back(buf, (char)c);
        } else if (state == URL_ST_HOST || state == URL_ST_HOSTNAME) {
            if (override && special == 2) { p--; state = URL_ST_FILE_HOST; }
            else if (c == ':' && !brackets) {
                if (len(buf) == 0) return 0;
                if (override == URL_ST_HOSTNAME) return 0;
                int kind = 0;
                vector<char> h;
                if (!url_host_parse(data(buf), len(buf), !special, &h, &kind)) return 0;
                clear(host);
                append_range(host, data(h), len(h));
                r->host_kind = kind;
                clear(buf);
                state = URL_ST_PORT;
            } else if (c == URL_EOF || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                p--;
                if (special && len(buf) == 0) return 0;
                if (override && len(buf) == 0 && (len(*user) || len(*pass) || r->port >= 0)) return 0;
                int kind = 0;
                vector<char> h;
                if (!url_host_parse(data(buf), len(buf), !special, &h, &kind)) return 0;
                clear(host);
                append_range(host, data(h), len(h));
                r->host_kind = kind;
                clear(buf);
                state = URL_ST_PATH_START;
                if (override) return 1;
            } else {
                if (c == '[') brackets = 1;
                if (c == ']') brackets = 0;
                push_back(buf, (char)c);
            }
        } else if (state == URL_ST_PORT) {
            if (url_digit(c)) push_back(buf, (char)c);
            else if (c == URL_EOF || c == '/' || c == '?' || c == '#' || (special && c == '\\') || override) {
                if (len(buf)) {
                    long port = 0;
                    for (size_t i = 0; i < len(buf); i++) {
                        port = port * 10 + (buf[i] - '0');
                        if (port > 65535) return 0;
                    }
                    r->port = port == url_default_port(special) ? -1 : (int32_t)port;
                    clear(buf);
                    if (override) return 1;
                }
                if (override) return 0;
                state = URL_ST_PATH_START;
                p--;
            } else return 0;
        } else if (state == URL_ST_FILE) {
            clear(scheme);
            url_puts(scheme, "file");
            r->special = 2;
            special = 2;
            clear(host);
            r->host_kind = URL_HOST_EMPTY;
            if (c == '/' || c == '\\') state = URL_ST_FILE_SLASH;
            else if (bh && bp->special == 2) {
                url_take(host, bh, bp->host_start, bp->host_end);
                r->host_kind = bp->host_kind;
                url_take(path, bh, bp->path_start, bp->path_end);
                r->query_null = bp->query_start == URL_NONE;
                if (!r->query_null) url_take(query, bh, bp->query_start, bp->query_end);
                else clear(query);
                if (c == '?') { clear(query); r->query_null = 0; state = URL_ST_QUERY; }
                else if (c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
                else if (c != URL_EOF) {
                    clear(query);
                    r->query_null = 1;
                    if (!url_starts_wdl(in + p, (size_t)(nn - p))) url_shorten(path, special);
                    else clear(path);
                    state = URL_ST_PATH;
                    p--;
                }
            } else { state = URL_ST_PATH; p--; }
        } else if (state == URL_ST_FILE_SLASH) {
            if (c == '/' || c == '\\') state = URL_ST_FILE_HOST;
            else {
                if (bh && bp->special == 2) {
                    url_take(host, bh, bp->host_start, bp->host_end);
                    r->host_kind = bp->host_kind;
                    /* the base's drive letter, kept: its path's first segment */
                    uint32_t s0 = bp->path_start + 1, e0 = s0;
                    while (e0 < bp->path_end && bh[e0] != '/') e0++;
                    if (!url_starts_wdl(in + p, (size_t)(nn - p)) && bp->path_end > bp->path_start &&
                        url_wdl_normalized(bh + s0, e0 - s0)) {
                        push_back(path, '/');
                        append_range(path, bh + s0, e0 - s0);
                    }
                }
                state = URL_ST_PATH;
                p--;
            }
        } else if (state == URL_ST_FILE_HOST) {
            if (c == URL_EOF || c == '/' || c == '\\' || c == '?' || c == '#') {
                p--;
                if (!override && url_wdl(data(buf), len(buf))) state = URL_ST_PATH;
                else if (len(buf) == 0) {
                    clear(host);
                    r->host_kind = URL_HOST_EMPTY;
                    if (override) return 1;
                    state = URL_ST_PATH_START;
                } else {
                    int kind = 0;
                    vector<char> h;
                    if (!url_host_parse(data(buf), len(buf), !special, &h, &kind)) return 0;
                    if (url_eq(&h, "localhost")) { clear(h); kind = URL_HOST_EMPTY; }
                    clear(host);
                    append_range(host, data(h), len(h));
                    r->host_kind = kind;
                    if (override) return 1;
                    clear(buf);
                    state = URL_ST_PATH_START;
                }
            } else push_back(buf, (char)c);
        } else if (state == URL_ST_PATH_START) {
            if (special) {
                state = URL_ST_PATH;
                if (c != '/' && c != '\\') p--;
            } else if (!override && c == '?') { clear(query); r->query_null = 0; state = URL_ST_QUERY; }
            else if (!override && c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
            else if (c != URL_EOF) {
                state = URL_ST_PATH;
                if (c != '/') p--;
            } else if (override && r->host_kind == URL_HOST_NULL) push_back(path, '/');
        } else if (state == URL_ST_PATH) {
            int slash = c == '/' || (special && c == '\\');
            if (c == URL_EOF || slash || (!override && (c == '?' || c == '#'))) {
                size_t bn = len(buf);
                if (url_dot2(data(buf), bn)) {
                    url_shorten(path, special);
                    if (!slash) push_back(path, '/');
                } else if (url_dot1(data(buf), bn)) {
                    if (!slash) push_back(path, '/');
                } else {
                    if (special == 2 && len(*path) == 0 && url_wdl(data(buf), bn)) buf[1] = ':';
                    push_back(path, '/');
                    append_range(path, data(buf), bn);
                }
                clear(buf);
                if (c == '?') { clear(query); r->query_null = 0; state = URL_ST_QUERY; }
                if (c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
            } else url_pct(&buf, c, URL_SET_PATH);
        } else if (state == URL_ST_OPAQUE_PATH) {
            if (c == '?') { clear(query); r->query_null = 0; state = URL_ST_QUERY; }
            else if (c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
            else if (c == ' ') {
                if (p + 1 < nn && (in[p + 1] == '?' || in[p + 1] == '#')) url_puts(path, "%20");
                else push_back(path, ' ');
            } else if (c != URL_EOF) url_pct(path, c, URL_SET_C0);
        } else if (state == URL_ST_QUERY) {
            if ((!override && c == '#') || c == URL_EOF) {
                int set = special ? URL_SET_SPECIAL_QUERY : URL_SET_QUERY;
                for (size_t i = 0; i < len(buf); i++) url_pct(query, (uint8_t)buf[i], set);
                clear(buf);
                if (c == '#') { clear(frag); r->fragment_null = 0; state = URL_ST_FRAGMENT; }
            } else push_back(buf, (char)c);
        } else if (state == URL_ST_FRAGMENT) {
            if (c != URL_EOF) url_pct(frag, c, URL_SET_FRAGMENT);
        }
        if (p >= nn) break;
        p++;
    }
    return 1;
}

/* the URL serializer (4.5): the record into o, and its components' places into *p */
static void url_serialize(const vector<char> *scheme, const vector<char> *user, const vector<char> *pass, const vector<char> *host,
                          const vector<char> *path, const vector<char> *query, const vector<char> *frag, const url_rec *r,
                          vector<char> *o, url_parts *p) {
    clear(o);
    memset(p, 0, sizeof *p);
    append_range(o, data(*scheme), len(*scheme));
    p->scheme_end = (uint32_t)len(*o);
    push_back(o, ':');
    p->port = -1;
    if (r->host_kind != URL_HOST_NULL) {
        url_puts(o, "//");
        if (len(*user) || len(*pass)) {
            p->user_start = (uint32_t)len(*o);
            append_range(o, data(*user), len(*user));
            p->user_end = (uint32_t)len(*o);
            if (len(*pass)) {
                push_back(o, ':');
                p->pass_start = (uint32_t)len(*o);
                append_range(o, data(*pass), len(*pass));
                p->pass_end = (uint32_t)len(*o);
            }
            push_back(o, '@');
        }
        p->host_start = (uint32_t)len(*o);
        append_range(o, data(*host), len(*host));
        p->host_end = (uint32_t)len(*o);
        if (r->port >= 0) {
            char t[16];
            int k = snprintf(t, sizeof t, ":%d", (int)r->port);
            url_put(o, t, (size_t)k);
            p->port = r->port;
        }
    } else if (!r->opaque && len(*path) >= 2 && data(*path)[0] == '/' && data(*path)[1] == '/') url_puts(o, "/.");
    p->path_start = (uint32_t)len(*o);
    append_range(o, data(*path), len(*path));
    p->path_end = (uint32_t)len(*o);
    p->query_start = p->query_end = URL_NONE;
    if (!r->query_null) {
        push_back(o, '?');
        p->query_start = (uint32_t)len(*o);
        append_range(o, data(*query), len(*query));
        p->query_end = (uint32_t)len(*o);
    }
    p->fragment_start = URL_NONE;
    if (!r->fragment_null) {
        push_back(o, '#');
        p->fragment_start = (uint32_t)len(*o);
        append_range(o, data(*frag), len(*frag));
    }
    p->len = (uint32_t)len(*o);
    p->host_kind = (uint8_t)r->host_kind;
    p->special = (uint8_t)r->special;
    p->opaque = (uint8_t)r->opaque;
}

/* the input as the parser reads it: no leading or trailing C0 control or space, no
   tab or newline */
static void url_clean(const char *s, size_t n, vector<char> *out) {
    size_t a = 0, b = n;
    while (a < n && (uint8_t)s[a] <= 0x20) a++;
    while (b > a && (uint8_t)s[b - 1] <= 0x20) b--;
    for (size_t i = a; i < b; i++) if (s[i] != '\t' && s[i] != '\n' && s[i] != '\r') push_back(out, s[i]);
}

/* in parsed against an already parsed base (bh 0 for none), serialized into o */
static int url_parse_into(const char *in, size_t n, const char *bh, const url_parts *bp, vector<char> *o, url_parts *p) {
    vector<char> s, scheme, user, pass, host, path, query, frag;
    url_clean(in, n, &s);
    url_rec r;
    memset(&r, 0, sizeof r);
    r.host_kind = URL_HOST_NULL;
    r.port = -1;
    r.query_null = r.fragment_null = 1;
    if (!url_basic(data(s), len(s), bh, bp, &scheme, &user, &pass, &host, &path, &query, &frag, &r, URL_ST_NONE)) return 0;
    url_serialize(&scheme, &user, &pass, &host, &path, &query, &frag, &r, o, p);
    return 1;
}

int url_parse(const char *in, size_t n, const char *base, size_t bn, char *out, size_t cap, url_parts *p) {
    vector<char> bt, o;
    url_parts bp;
    int ok = 0;
    if (!base) ok = url_parse_into(in, n, 0, 0, &o, p);
    else if (url_parse_into(base, bn, 0, 0, &bt, &bp)) ok = url_parse_into(in, n, data(bt), &bp, &o, p);
    if (!ok || len(o) + 1 > cap) {
        if (cap) out[0] = 0;
        return 0;
    }
    memcpy(out, data(o), len(o));
    out[len(o)] = 0;
    return 1;
}

/* ---- the getters (6.1) and the origin (4.7) ---- */

static size_t url_out(char *out, size_t cap, const char *s, size_t n) {
    if (!cap) return 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(out, s, n);
    out[n] = 0;
    return n;
}

size_t url_attr(const char *href, const url_parts *p, int which, char *out, size_t cap) {
    char t[300];
    if (which == URL_HREF) return url_out(out, cap, href, p->len);
    if (which == URL_PROTOCOL) return url_out(out, cap, href, p->scheme_end + 1);
    if (which == URL_USERNAME) return url_out(out, cap, href + p->user_start, p->user_end - p->user_start);
    if (which == URL_PASSWORD) return url_out(out, cap, href + p->pass_start, p->pass_end - p->pass_start);
    if (which == URL_HOSTNAME) return url_out(out, cap, href + p->host_start, p->host_end - p->host_start);
    if (which == URL_HOST) {
        if (p->host_kind == URL_HOST_NULL) return url_out(out, cap, "", 0);
        size_t hn = p->host_end - p->host_start;
        if (p->port < 0) return url_out(out, cap, href + p->host_start, hn);
        /* the host and its port, as the serialization has them */
        return url_out(out, cap, href + p->host_start, hn + (size_t)snprintf(t, sizeof t, ":%d", (int)p->port));
    }
    if (which == URL_PORT) {
        if (p->port < 0) return url_out(out, cap, "", 0);
        int k = snprintf(t, sizeof t, "%d", (int)p->port);
        return url_out(out, cap, t, (size_t)k);
    }
    if (which == URL_PATHNAME) return url_out(out, cap, href + p->path_start, p->path_end - p->path_start);
    if (which == URL_SEARCH) {
        if (p->query_start == URL_NONE || p->query_end == p->query_start) return url_out(out, cap, "", 0);
        return url_out(out, cap, href + p->query_start - 1, p->query_end - p->query_start + 1);
    }
    if (which == URL_HASH) {
        if (p->fragment_start == URL_NONE || p->fragment_start == p->len) return url_out(out, cap, "", 0);
        return url_out(out, cap, href + p->fragment_start - 1, p->len - p->fragment_start + 1);
    }
    if (which == URL_ORIGIN) {
        /* a tuple origin for a special URL but file:; a blob: URL's is its path's when
           that is an http:, https: or file: URL; every other an opaque one */
        if (p->special && p->special != 2) {
            size_t e = p->port >= 0 ? p->host_end + (size_t)snprintf(t, sizeof t, ":%d", (int)p->port) : p->host_end;
            vector<char> o;
            append_range(o, href, p->scheme_end);
            url_puts(&o, "://");
            append_range(o, href + p->host_start, e - p->host_start);
            return url_out(out, cap, data(o), len(o));
        }
        if (p->scheme_end == 4 && memcmp(href, "blob", 4) == 0) {
            vector<char> u;
            url_parts q;
            if (url_parse_into(href + p->path_start, p->path_end - p->path_start, 0, 0, &u, &q) && (q.special == 3 || q.special == 4))
                return url_attr(data(u), &q, URL_ORIGIN, out, cap);
        }
        return url_out(out, cap, "null", 4);
    }
    return url_out(out, cap, "", 0);
}
