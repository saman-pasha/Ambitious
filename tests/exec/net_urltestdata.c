/* tests/exec/net_urltestdata.c -- web-platform-tests' urltestdata.json through the
   URL standard's parser (src/net/url.c), every case as WPT's url-constructor and
   url-origin tests run it: the input parsed against its base (none when the base
   is null; a base that fails to parse fails the case), then failure, or else the
   href, protocol, username, password, host, hostname, port, pathname, search and
   hash the URL API's getters answer, the origin where the case names one, and
   searchParams where it names that (the query parsed and serialized again as
   application/x-www-form-urlencoded). A line a case, then the totals. The data is
   tests/net/wpt/urltestdata.json (tests/net/wpt/LICENSE says whence). */
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

int main(void) {
    if (!json_read_file("tests/net/wpt/urltestdata.json")) {
        printf("no tests/net/wpt/urltestdata.json\n");
        return 1;
    }
    size_t at = 0;
    int total = 0, passed = 0;
    char href[8192], got[8192];
    vector<char> why;
    for (;;) {
        json_obj o;
        memset(&o, 0, sizeof o);
        int k = json_next(&at, &o);
        if (k == 0) break;
        if (k == 2) continue;
        size_t ia, in, ba, bn, fa, fn;
        if (json_get(&o, "input", &ia, &in) != 's') continue;
        char bk = json_get(&o, "base", &ba, &bn);
        int want_failure = json_get(&o, "failure", &fa, &fn) == 't';
        total++;
        url_parts p;
        int ok = 0;
        if (bk == 's') ok = url_parse(json_pool + ia, in, json_pool + ba, bn, href, sizeof href, &p);
        else ok = url_parse(json_pool + ia, in, 0, 0, href, sizeof href, &p);
        clear(why);
        if (want_failure) {
            if (ok) {
                append_range(why, " parsed as ", 11);
                append_range(why, href, strlen(href));
                append_range(why, ", not failure", 13);
            }
        } else if (!ok) append_range(why, " failed to parse", 16);
        else {
            for (int a = 0; a < 11; a++) {
                size_t va, vn;
                if (json_get(&o, attrs[a], &va, &vn) != 's') continue;
                size_t gn = url_attr(href, &p, which[a], got, sizeof got);
                if (gn == vn && memcmp(got, json_pool + va, vn) == 0) continue;
                char t[64];
                int tn = snprintf(t, sizeof t, "%s%s ", len(why) ? "; " : " ", attrs[a]);
                append_range(why, t, (size_t)tn);
                append_range(why, got, gn);
                append_range(why, " not ", 5);
                append_range(why, json_pool + va, vn);
            }
            /* searchParams: the query parsed and serialized again as
               application/x-www-form-urlencoded (URLSearchParams' toString()) */
            size_t sa, sn;
            if (json_get(&o, "searchParams", &sa, &sn) == 's') {
                size_t gn = 0;
                if (p.query_start != URL_NONE) gn = url_form_reserialize(href + p.query_start, p.query_end - p.query_start, got, sizeof got);
                if (gn != sn || memcmp(got, json_pool + sa, sn) != 0) {
                    append_range(why, " searchParams ", 14);
                    append_range(why, got, gn);
                    append_range(why, " not ", 5);
                    append_range(why, json_pool + sa, sn);
                }
            }
        }
        int good = len(why) == 0;
        passed += good;
        printf("%s <", good ? "ok  " : "FAIL");
        json_print(json_pool + ia, in);
        if (bk == 's') {
            printf("> against <");
            json_print(json_pool + ba, bn);
        }
        printf(">");
        if (!good) {
            printf(":");
            json_print(data(why), len(why));
        }
        printf("\n");
    }
    printf("urltestdata: %d of %d cases pass\n", passed, total);
    return 0;
}
