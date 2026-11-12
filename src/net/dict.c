/* src/net/dict.c -- Compression Dictionary Transport (RFC 9842): what of a dictionary the
   loader (src/net/net.c) asks, as Chromium answers it (dict.h).

   SHA-256 (FIPS 180-4) names a dictionary: Available-Dictionary carries its digest, and a dcb
   or dcz body begins with it. Use-As-Dictionary is a structured field dictionary (RFC 9651),
   read whole: its members' keys, their items (integers, decimals, strings, tokens, byte
   sequences, booleans, dates, display strings) and inner lists, each with its parameters; a
   field that does not parse is none (measured: match=/d/* as a token keeps no dictionary).
   Of its members, match must be a string, match-dest an inner list of strings, id a string of
   at most 1024 characters, type the token raw (measured: type=other keeps none; an empty id
   sends no Dictionary-ID; match-dest ("") is the empty destination of fetch(), ("empty") none).

   The match is a URL pattern's constructor string (the URL Pattern standard), its base the
   dictionary's URL: a path alone (relative to the base's when it does not begin with `/'),
   `?' and a search alone, or a URL written whole, whose scheme, host and port must be the
   dictionary's own (measured: a pattern of another origin keeps none). Components the pattern
   does not name are anything (a path's pattern then matches every query). Its syntax as far
   as it is read here: a character, one a backslash escapes, `*' (any characters, or none), a
   named group `:name' (one character or more; in a path none of them `/', measured: `/d/o/:id'
   matches /d/o/123, not /d/o/1/2), and `*?' (`*' again). A regular expression (a group in
   parentheses: measured, its dictionary not kept), `{', and a modifier after a named group
   (`?', `+', `*') make a pattern not kept here, where Chromium keeps the last two kinds; a
   pattern's characters are not canonicalized (a space in a pattern is not `%20'). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dict.h"
#include "url.h"

/* ---- SHA-256 ---- */

static const uint32_t net_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static uint32_t net_rotr(uint32_t x, uint32_t k) { return x >> k | x << (32 - k); }

/* one 64-byte block of the message at b[at] taken into the state h */
static void net_sha_block(uint32_t *h, const uint8_t *b, size_t at) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++)
        w[t] = (uint32_t)b[at + 4 * t] << 24 | (uint32_t)b[at + 4 * t + 1] << 16 | (uint32_t)b[at + 4 * t + 2] << 8 | b[at + 4 * t + 3];
    for (int t = 16; t < 64; t++) {
        uint32_t s0 = net_rotr(w[t - 15], 7) ^ net_rotr(w[t - 15], 18) ^ w[t - 15] >> 3;
        uint32_t s1 = net_rotr(w[t - 2], 17) ^ net_rotr(w[t - 2], 19) ^ w[t - 2] >> 10;
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
        uint32_t t1 = hh + (net_rotr(e, 6) ^ net_rotr(e, 11) ^ net_rotr(e, 25)) + ((e & f) ^ (~e & g)) + net_sha_k[t] + w[t];
        uint32_t t2 = (net_rotr(a, 2) ^ net_rotr(a, 13) ^ net_rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = bb;
        bb = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += bb;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void net_sha256(const uint8_t *p, size_t n, uint8_t *out) {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) net_sha_block(h, p, i);
    /* the rest, a 1 bit, zeros, and the length in bits: one block or two */
    uint8_t last[128];
    memset(last, 0, sizeof last);
    size_t r = n - i;
    if (r) memcpy(last, p + i, r);
    last[r] = 0x80;
    size_t total = r + 9 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) last[total - 1 - (size_t)k] = (uint8_t)(bits >> (8 * k));
    net_sha_block(h, last, 0);
    if (total == 128) net_sha_block(h, last, 64);
    for (int k = 0; k < 8; k++) {
        out[4 * k] = (uint8_t)(h[k] >> 24);
        out[4 * k + 1] = (uint8_t)(h[k] >> 16);
        out[4 * k + 2] = (uint8_t)(h[k] >> 8);
        out[4 * k + 3] = (uint8_t)h[k];
    }
}

/* ---- structured fields (RFC 9651, 4.2), as far as a dictionary's need them ---- */

enum { SF_NONE, SF_INTEGER, SF_DECIMAL, SF_STRING, SF_TOKEN, SF_BYTES, SF_BOOLEAN, SF_DATE, SF_DISPLAY, SF_INNER };

static int net_sf_lcalpha(char c) { return c >= 'a' && c <= 'z'; }
static int net_sf_digit(char c) { return c >= '0' && c <= '9'; }
static int net_sf_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
/* a tchar of RFC 9110 */
static int net_sf_tchar(char c) { return net_sf_alpha(c) || net_sf_digit(c) || (c && strchr("!#$%&'*+-.^_`|~", c)); }
static void net_sf_sp(const char *v, size_t n, size_t *at) { while (*at < n && v[*at] == ' ') (*at)++; }
static void net_sf_ows(const char *v, size_t n, size_t *at) { while (*at < n && (v[*at] == ' ' || v[*at] == '\t')) (*at)++; }

/* a key, into out (cut at cap): 0 when there is none */
static int net_sf_key(const char *v, size_t n, size_t *at, char *out, size_t cap) {
    if (*at >= n || !(net_sf_lcalpha(v[*at]) || v[*at] == '*')) return 0;
    size_t k = 0;
    while (*at < n && (net_sf_lcalpha(v[*at]) || net_sf_digit(v[*at]) || v[*at] == '_' || v[*at] == '-' || v[*at] == '.' || v[*at] == '*')) {
        if (k + 1 < cap) out[k++] = v[*at];
        (*at)++;
    }
    out[k] = 0;
    return 1;
}

/* a bare item: its type (SF_...), a string's or a token's text into out (cut at cap, its
   whole length in *len); SF_NONE when it is none */
static int net_sf_bare(const char *v, size_t n, size_t *at, char *out, size_t cap, size_t *len) {
    size_t k = 0;
    out[0] = 0;
    *len = 0;
    if (*at >= n) return SF_NONE;
    char c = v[*at];
    if (c == '-' || net_sf_digit(c)) {
        /* an integer (at most 15 digits), or a decimal (at most 12 before its point, 3 after) */
        if (c == '-') (*at)++;
        size_t d = 0;
        while (*at < n && net_sf_digit(v[*at])) { (*at)++; d++; }
        if (!d) return SF_NONE;
        if (*at < n && v[*at] == '.') {
            if (d > 12) return SF_NONE;
            (*at)++;
            size_t f = 0;
            while (*at < n && net_sf_digit(v[*at])) { (*at)++; f++; }
            if (f < 1 || f > 3) return SF_NONE;
            return SF_DECIMAL;
        }
        return d <= 15 ? SF_INTEGER : SF_NONE;
    }
    if (c == '"') {
        (*at)++;
        while (*at < n) {
            char x = v[*at];
            if (x == '\\') {
                if (*at + 1 >= n || (v[*at + 1] != '"' && v[*at + 1] != '\\')) return SF_NONE;
                x = v[*at + 1];
                (*at)++;
            } else if (x == '"') {
                (*at)++;
                out[k < cap ? k : cap - 1] = 0;
                return SF_STRING;
            } else if (x < 0x20 || x > 0x7e) return SF_NONE;
            if (k + 1 < cap) out[k++] = x;
            out[k] = 0;
            (*len)++;
            (*at)++;
        }
        return SF_NONE;
    }
    if (net_sf_alpha(c) || c == '*') {
        while (*at < n && (net_sf_tchar(v[*at]) || v[*at] == ':' || v[*at] == '/')) {
            if (k + 1 < cap) out[k++] = v[*at];
            (*len)++;
            (*at)++;
        }
        out[k] = 0;
        return SF_TOKEN;
    }
    if (c == ':') {
        (*at)++;
        while (*at < n && v[*at] != ':') {
            char x = v[*at];
            if (!(net_sf_alpha(x) || net_sf_digit(x) || x == '+' || x == '/' || x == '=')) return SF_NONE;
            (*at)++;
        }
        if (*at >= n) return SF_NONE;
        (*at)++;
        return SF_BYTES;
    }
    if (c == '?') {
        if (*at + 1 >= n || (v[*at + 1] != '0' && v[*at + 1] != '1')) return SF_NONE;
        *at += 2;
        return SF_BOOLEAN;
    }
    if (c == '@') {
        (*at)++;
        return net_sf_bare(v, n, at, out, cap, len) == SF_INTEGER ? SF_DATE : SF_NONE;
    }
    if (c == '%' && *at + 1 < n && v[*at + 1] == '"') {
        *at += 2;
        while (*at < n && v[*at] != '"') {
            if (v[*at] < 0x20 || v[*at] > 0x7e) return SF_NONE;
            (*at)++;
        }
        if (*at >= n) return SF_NONE;
        (*at)++;
        return SF_DISPLAY;
    }
    return SF_NONE;
}

/* an item's or an inner list's parameters: 1, or 0 when they do not parse */
static int net_sf_params(const char *v, size_t n, size_t *at) {
    char key[64], val[8];
    size_t vl;
    while (*at < n && v[*at] == ';') {
        (*at)++;
        net_sf_sp(v, n, at);
        if (!net_sf_key(v, n, at, key, sizeof key)) return 0;
        if (*at < n && v[*at] == '=') {
            (*at)++;
            if (net_sf_bare(v, n, at, val, sizeof val, &vl) == SF_NONE) return 0;
        }
    }
    return 1;
}

int net_dict_field_read(const char *v, size_t n, net_dict_field *f) {
    memset(f, 0, sizeof *f);
    int has_match = 0, bad = 0;
    char key[64], text[2048];
    size_t at = 0;
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
    net_sf_sp(v, n, &at);
    if (at >= n) return 0;
    for (;;) {
        if (!net_sf_key(v, n, &at, key, sizeof key)) return 0;
        int type = SF_BOOLEAN;
        size_t tl = 0;
        text[0] = 0;
        char dest[256];
        size_t dk = 0;
        int dest_strings = 1;
        dest[0] = 0;
        if (at < n && v[at] == '=') {
            at++;
            if (at < n && v[at] == '(') {
                /* an inner list: its items, each a string for match-dest */
                at++;
                type = SF_INNER;
                for (;;) {
                    net_sf_sp(v, n, &at);
                    if (at < n && v[at] == ')') { at++; break; }
                    char item[256];
                    size_t il;
                    int it = net_sf_bare(v, n, &at, item, sizeof item, &il);
                    if (it == SF_NONE || !net_sf_params(v, n, &at)) return 0;
                    if (it != SF_STRING) dest_strings = 0;
                    else if (dk + il + 2 < sizeof dest) {
                        memcpy(dest + dk, item, il);
                        dk += il;
                        dest[dk++] = '\n';
                        dest[dk] = 0;
                    }
                    if (at >= n || (v[at] != ' ' && v[at] != ')')) return 0;
                }
            } else {
                type = net_sf_bare(v, n, &at, text, sizeof text, &tl);
                if (type == SF_NONE) return 0;
            }
        }
        if (!net_sf_params(v, n, &at)) return 0;
        /* the members a dictionary reads, the last of a key the one that counts */
        if (strcmp(key, "match") == 0) {
            has_match = type == SF_STRING && tl > 0 && tl < sizeof f->match;
            if (has_match) snprintf(f->match, sizeof f->match, "%s", text);
        } else if (strcmp(key, "match-dest") == 0) {
            f->has_dest = 1;
            bad = bad || type != SF_INNER || !dest_strings;
            snprintf(f->dest, sizeof f->dest, "%s", dest);
        } else if (strcmp(key, "id") == 0) {
            bad = bad || type != SF_STRING || tl > 1024;
            if (type == SF_STRING && tl <= 1024) snprintf(f->id, sizeof f->id, "%s", text);
        } else if (strcmp(key, "type") == 0)
            bad = bad || type != SF_TOKEN || strcmp(text, "raw") != 0;
        net_sf_ows(v, n, &at);
        if (at >= n) break;
        if (v[at] != ',') return 0;
        at++;
        net_sf_ows(v, n, &at);
        if (at >= n) return 0;
    }
    return has_match && !bad;
}

/* ---- URL patterns ---- */

/* a pattern's piece: a character, anything (`*'), or a named group */
enum { PT_CHAR, PT_ANY, PT_GROUP };
#define NET_PAT_MAX 512

/* a component's pattern compiled into kinds and characters: their count, or -1 when it is
   one not read here (a regular expression, `{', a modifier after a group) */
static int net_pat_compile(const char *s, size_t n, uint8_t *kind, char *ch) {
    int k = 0;
    size_t i = 0;
    while (i < n) {
        if (k >= NET_PAT_MAX) return -1;
        char c = s[i];
        if (c == '\\') {
            if (i + 1 >= n) return -1;
            kind[k] = PT_CHAR;
            ch[k++] = s[i + 1];
            i += 2;
        } else if (c == '*') {
            kind[k] = PT_ANY;
            ch[k++] = 0;
            i++;
            if (i < n && s[i] == '?') i++;           /* `*?': anything, or nothing, as `*' is */
        } else if (c == ':') {
            size_t j = i + 1;
            while (j < n && (net_sf_alpha(s[j]) || net_sf_digit(s[j]) || s[j] == '_' || s[j] == '$')) j++;
            if (j == i + 1) return -1;
            if (j < n && (s[j] == '?' || s[j] == '+' || s[j] == '*' || s[j] == '(')) return -1;
            kind[k] = PT_GROUP;
            ch[k++] = 0;
            i = j;
        } else if (c == '(' || c == ')' || c == '{' || c == '}') return -1;
        else {
            kind[k] = PT_CHAR;
            ch[k++] = c;
            i++;
        }
    }
    return k;
}

/* text t[0, tn) matched by the compiled pattern, every piece in turn over the positions it
   may reach (a group's characters none of them delim, when delim is not 0) */
static int net_pat_run(const uint8_t *kind, const char *ch, int k, const char *t, size_t tn, char delim) {
    if (tn > 16384) return 0;
    uint8_t cur[16385], next[16385];
    memset(cur, 0, tn + 1);
    cur[0] = 1;
    for (int p = 0; p < k; p++) {
        memset(next, 0, tn + 1);
        if (kind[p] == PT_CHAR) {
            for (size_t j = 0; j < tn; j++)
                if (cur[j] && t[j] == ch[p]) next[j + 1] = 1;
        } else if (kind[p] == PT_ANY) {
            int seen = 0;
            for (size_t j = 0; j <= tn; j++) {
                seen = seen || cur[j];
                next[j] = (uint8_t)seen;
            }
        } else {
            int open = 0;
            for (size_t j = 1; j <= tn; j++) {
                if (delim && t[j - 1] == delim) open = 0;
                else open = open || cur[j - 1];
                next[j] = (uint8_t)open;
            }
        }
        memcpy(cur, next, tn + 1);
    }
    return cur[tn];
}

/* a pattern split into its pathname and search patterns (`*' for a search it does not name),
   its base the dictionary's URL: 1, or 0 when it is no pattern kept here */
static int net_pat_split(const char *pat, const char *dict_url, char *path, size_t pcap, char *search, size_t scap) {
    char base[8192], origin[600], bpath[8192];
    url_parts bp;
    if (!url_parse(dict_url, strlen(dict_url), 0, 0, base, sizeof base, &bp)) return 0;
    url_attr(base, &bp, URL_ORIGIN, origin, sizeof origin);
    url_attr(base, &bp, URL_PATHNAME, bpath, sizeof bpath);
    size_t n = strlen(pat), s = 0;
    /* a pattern written with its scheme: its scheme, host and port the dictionary's own */
    while (s < n && (net_sf_alpha(pat[s]) || (s && (net_sf_digit(pat[s]) || pat[s] == '+' || pat[s] == '-' || pat[s] == '.')))) s++;
    char rest[4200];
    if (s && s < n && pat[s] == ':') {
        if (s + 2 >= n || pat[s + 1] != '/' || pat[s + 2] != '/') return 0;
        size_t a = s + 3;
        while (a < n && pat[a] != '/' && pat[a] != '?' && pat[a] != '#') a++;
        char whole[700], canon[700];
        if (a > sizeof whole - 2) return 0;
        snprintf(whole, sizeof whole, "%.*s/", (int)a, pat);
        for (size_t q = 0; q < a; q++)
            if (pat[q] == '*' || pat[q] == '(' || pat[q] == '{' || (q > s && pat[q] == ':' && q + 1 < a && !net_sf_digit(pat[q + 1]))) return 0;
        url_parts op;
        char ob[800];
        if (!url_parse(whole, strlen(whole), 0, 0, ob, sizeof ob, &op)) return 0;
        url_attr(ob, &op, URL_ORIGIN, canon, sizeof canon);
        if (strcmp(canon, origin) != 0) return 0;
        if (a < n) snprintf(rest, sizeof rest, "%s", pat + a);
        else snprintf(rest, sizeof rest, "/");
    } else if (pat[0] == '/') snprintf(rest, sizeof rest, "%s", pat);
    else if (pat[0] == '?') {
        /* a search alone: the dictionary's own path */
        size_t k = 0;
        for (size_t q = 0; bpath[q] && k + 3 < sizeof rest; q++) {
            if (strchr("\\*:(){}?+", bpath[q])) rest[k++] = '\\';
            rest[k++] = bpath[q];
        }
        rest[k] = 0;
        snprintf(rest + k, sizeof rest - k, "%s", pat);
    } else {
        /* a relative path: the dictionary's path to its last `/', then the pattern */
        size_t last = 0;
        for (size_t q = 0; bpath[q]; q++) if (bpath[q] == '/') last = q + 1;
        size_t k = 0;
        for (size_t q = 0; q < last && k + 3 < sizeof rest; q++) {
            if (strchr("\\*:(){}?+", bpath[q])) rest[k++] = '\\';
            rest[k++] = bpath[q];
        }
        rest[k] = 0;
        snprintf(rest + k, sizeof rest - k, "%s", pat);
    }
    /* the pathname to its search's `?' (not one a `*' or a group's end makes a modifier), the
       search to its hash's `#' */
    size_t rn = strlen(rest), q = 0;
    int named = 0;      /* within a named group's name, whose `?' would be a modifier */
    while (q < rn) {
        if (rest[q] == '\\') { q += 2; named = 0; continue; }
        if (rest[q] == '?' && !named && !(q > 0 && (rest[q - 1] == '*' || rest[q - 1] == ')' || rest[q - 1] == '}'))) break;
        if (rest[q] == '#') break;
        if (rest[q] == ':') named = 1;
        else if (!(net_sf_alpha(rest[q]) || net_sf_digit(rest[q]) || rest[q] == '_' || rest[q] == '$')) named = 0;
        q++;
    }
    if (q > rn) q = rn;
    if (q + 1 > pcap) return 0;
    snprintf(path, pcap, "%.*s", (int)q, rest);
    if (q < rn && rest[q] == '?') {
        size_t e = q + 1;
        while (e < rn && rest[e] != '#') { if (rest[e] == '\\') e++; e++; }
        if (e > rn) e = rn;
        snprintf(search, scap, "%.*s", (int)(e - q - 1), rest + q + 1);
        q = e;
    } else snprintf(search, scap, "*");
    /* a hash's pattern: a request has no fragment, so one that cannot be empty never matches */
    if (q < rn && rest[q] == '#') {
        uint8_t kd[NET_PAT_MAX];
        char cc[NET_PAT_MAX];
        int k = net_pat_compile(rest + q + 1, rn - q - 1, kd, cc);
        if (k < 0) return 0;
        if (!net_pat_run(kd, cc, k, "", 0, 0)) return 0;
    }
    return 1;
}

int net_dict_pattern_ok(const char *pattern, const char *dict_url) {
    char path[4200], search[4200];
    uint8_t kd[NET_PAT_MAX];
    char cc[NET_PAT_MAX];
    if (!pattern[0] || !net_pat_split(pattern, dict_url, path, sizeof path, search, sizeof search)) return 0;
    return net_pat_compile(path, strlen(path), kd, cc) >= 0 && net_pat_compile(search, strlen(search), kd, cc) >= 0;
}

int net_dict_pattern_matches(const char *pattern, const char *dict_url, const char *url) {
    char path[4200], search[4200], ub[8192], uo[600], dbuf[8192], dorig[600], upath[8192], uquery[8192];
    uint8_t kd[NET_PAT_MAX];
    char cc[NET_PAT_MAX];
    url_parts up, dp;
    if (!net_pat_split(pattern, dict_url, path, sizeof path, search, sizeof search)) return 0;
    if (!url_parse(url, strlen(url), 0, 0, ub, sizeof ub, &up) || !url_parse(dict_url, strlen(dict_url), 0, 0, dbuf, sizeof dbuf, &dp)) return 0;
    url_attr(ub, &up, URL_ORIGIN, uo, sizeof uo);
    url_attr(dbuf, &dp, URL_ORIGIN, dorig, sizeof dorig);
    if (strcmp(uo, dorig) != 0 || strcmp(uo, "null") == 0) return 0;
    url_attr(ub, &up, URL_PATHNAME, upath, sizeof upath);
    uquery[0] = 0;
    if (up.query_start != URL_NONE) snprintf(uquery, sizeof uquery, "%.*s", (int)(up.query_end - up.query_start), ub + up.query_start);
    int k = net_pat_compile(path, strlen(path), kd, cc);
    if (k < 0 || !net_pat_run(kd, cc, k, upath, strlen(upath), '/')) return 0;
    k = net_pat_compile(search, strlen(search), kd, cc);
    return k >= 0 && net_pat_run(kd, cc, k, uquery, strlen(uquery), 0);
}
