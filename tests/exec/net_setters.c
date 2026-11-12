/* tests/exec/net_setters.c -- web-platform-tests' setters_tests.json through the URL API's
   setters as src/net/url.c has them (url_set: the basic URL parser with its state
   overrides), every case as WPT's url-setters test runs it: the href parsed, the
   attribute set to the new value, then each attribute the case expects read back. A
   line a case, each attribute's totals and the whole's. There is no URL API in the
   script module to run them through, so they run here, at the parser. The data is
   tests/net/wpt/setters_tests.json (tests/net/wpt/LICENSE says whence). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "net_json.h"
#include "../../src/net/url.h"

static const char *const attrs[] = { "href", "protocol", "username", "password", "host", "hostname", "port", "pathname", "search",
                                     "hash", "origin" };
static const int which[] = { URL_HREF, URL_PROTOCOL, URL_USERNAME, URL_PASSWORD, URL_HOST, URL_HOSTNAME, URL_PORT, URL_PATHNAME,
                             URL_SEARCH, URL_HASH, URL_ORIGIN };

/* the attribute's place in attrs, or -1 */
static int attr_of(const char *s, size_t n) {
    for (int k = 0; k < 11; k++) if (strlen(attrs[k]) == n && memcmp(attrs[k], s, n) == 0) return k;
    return -1;
}

static char peek(size_t p) { return p < json_file_len ? json_file[p] : 0; }

/* a value passed over: a string, an array, an object, a number or a literal */
static int skip_value(size_t *p, int depth) {
    json_ws(p);
    char c = peek(*p);
    if (depth > 32) return 0;
    if (c == '"') {
        size_t keep = json_pool_len;
        int ok = json_string(p);
        json_pool_len = keep;
        return ok;
    }
    if (c == '[' || c == '{') {
        char close = c == '[' ? ']' : '}';
        (*p)++;
        for (;;) {
            json_ws(p);
            char d = peek(*p);
            if (d == 0) return 0;
            if (d == close) {
                (*p)++;
                return 1;
            }
            if (d == ',') {
                (*p)++;
                continue;
            }
            if (c == '{') {
                size_t keep = json_pool_len;
                if (!json_string(p)) return 0;
                json_pool_len = keep;
                json_ws(p);
                if (peek(*p) != ':') return 0;
                (*p)++;
            }
            if (!skip_value(p, depth + 1)) return 0;
        }
    }
    while (*p < json_file_len && peek(*p) != ',' && peek(*p) != ']' && peek(*p) != '}') (*p)++;
    return 1;
}

int main(void) {
    if (!json_read_file("tests/net/wpt/setters_tests.json")) {
        printf("no tests/net/wpt/setters_tests.json\n");
        return 1;
    }
    size_t p = 0;
    int all = 0, all_passed = 0;
    vector<char> why;
    char h0[8192], h1[8192], got[8192];
    json_ws(&p);
    if (peek(p) != '{') return 1;
    p++;
    for (;;) {
        json_ws(&p);
        if (peek(p) == ',') { p++; json_ws(&p); }
        if (peek(p) != '"') break;
        json_pool_len = 0;
        if (!json_string(&p)) return 1;
        char attr[32];
        snprintf(attr, sizeof attr, "%.*s", (int)json_pool_len, json_pool);
        json_ws(&p);
        if (peek(p) != ':') return 1;
        p++;
        int a = attr_of(attr, strlen(attr));
        json_ws(&p);
        if (a < 0 || peek(p) != '[') {
            if (!skip_value(&p, 0)) return 1;
            continue;
        }
        p++;
        int total = 0, passed = 0;
        for (;;) {
            json_ws(&p);
            if (peek(p) == ',') { p++; json_ws(&p); }
            if (peek(p) == ']') { p++; break; }
            if (peek(p) != '{') return 1;
            p++;
            /* the case: its href, new value, and the attributes expected */
            json_pool_len = 0;
            size_t ha = 0, hn = 0, va = 0, vn = 0, ek[16], ekn[16], ev[16], evn[16];
            int ne = 0;
            for (;;) {
                json_ws(&p);
                if (peek(p) == ',') { p++; json_ws(&p); }
                if (peek(p) == '}') { p++; break; }
                size_t ka = json_pool_len;
                if (!json_string(&p)) return 1;
                size_t kn = json_pool_len - ka;
                json_ws(&p);
                if (peek(p) != ':') return 1;
                p++;
                json_ws(&p);
                int is_href = kn == 4 && memcmp(json_pool + ka, "href", 4) == 0;
                int is_value = kn == 9 && memcmp(json_pool + ka, "new_value", 9) == 0;
                int is_expected = kn == 8 && memcmp(json_pool + ka, "expected", 8) == 0;
                if ((is_href || is_value) && peek(p) == '"') {
                    size_t sa = json_pool_len;
                    if (!json_string(&p)) return 1;
                    if (is_href) { ha = sa; hn = json_pool_len - sa; }
                    else { va = sa; vn = json_pool_len - sa; }
                } else if (is_expected && peek(p) == '{') {
                    p++;
                    for (;;) {
                        json_ws(&p);
                        if (peek(p) == ',') { p++; json_ws(&p); }
                        if (peek(p) == '}') { p++; break; }
                        if (ne == 16) return 1;
                        ek[ne] = json_pool_len;
                        if (!json_string(&p)) return 1;
                        ekn[ne] = json_pool_len - ek[ne];
                        json_ws(&p);
                        if (peek(p) != ':') return 1;
                        p++;
                        json_ws(&p);
                        ev[ne] = json_pool_len;
                        if (!json_string(&p)) return 1;
                        evn[ne] = json_pool_len - ev[ne];
                        ne++;
                    }
                } else if (!skip_value(&p, 0)) return 1;
            }
            total++;
            clear(why);
            url_parts p0, p1;
            if (!url_parse(json_pool + ha, hn, 0, 0, h0, sizeof h0, &p0)) append_range(why, " the href does not parse", 24);
            else {
                if (!url_set(h0, &p0, which[a], json_pool + va, vn, h1, sizeof h1, &p1)) {
                    /* the setter refused the value: the URL as it was */
                    memcpy(h1, h0, strlen(h0) + 1);
                    p1 = p0;
                }
                for (int e = 0; e < ne; e++) {
                    int k = attr_of(json_pool + ek[e], ekn[e]);
                    if (k < 0) continue;
                    size_t gn = url_attr(h1, &p1, which[k], got, sizeof got);
                    if (gn == evn[e] && memcmp(got, json_pool + ev[e], gn) == 0) continue;
                    char t[64];
                    int tn = snprintf(t, sizeof t, "%s%s ", len(why) ? "; " : " ", attrs[k]);
                    append_range(why, t, (size_t)tn);
                    append_range(why, got, gn);
                    append_range(why, " not ", 5);
                    append_range(why, json_pool + ev[e], evn[e]);
                }
            }
            int good = len(why) == 0;
            passed += good;
            printf("%s <", good ? "ok  " : "FAIL");
            json_print(json_pool + ha, hn);
            printf(">.%s = <", attrs[a]);
            json_print(json_pool + va, vn);
            printf(">");
            if (!good) {
                printf(":");
                json_print(data(why), len(why));
            }
            printf("\n");
        }
        printf("%s: %d of %d cases pass\n", attrs[a], passed, total);
        all += total;
        all_passed += passed;
    }
    printf("setters: %d of %d cases pass\n", all_passed, all);
    return 0;
}
