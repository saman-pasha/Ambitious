/* tests/exec/net_idna.c -- IDNA (UTS #46) as the URL standard's host parser runs it
   (src/net/url.c), through web-platform-tests' toascii.json and IdnaTestV2.json, every
   case as WPT's toascii and IdnaTestV2 tests run it: https://INPUT/x parsed, then
   failure where the case's output is null, or else its host and hostname the output,
   its pathname /x and its href https://OUTPUT/x (IdnaTestV2's empty input skipped, as
   WPT skips it). A line a case, and each file's totals. Then domain to Unicode, as a
   browser shows a host: a few domains each way. The data is tests/net/wpt/
   (tests/net/wpt/LICENSE says whence). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "net_json.h"
#include "../../src/net/url.h"

/* one file's cases: how many passed of how many */
static void run(const char *path, const char *name, int skip_empty) {
    if (!json_read_file(path)) {
        printf("no %s\n", path);
        return;
    }
    size_t at = 0;
    int total = 0, passed = 0;
    vector<char> in, want;
    char href[8192], got[8192];
    for (;;) {
        json_obj o;
        memset(&o, 0, sizeof o);
        int k = json_next(&at, &o);
        if (k == 0) break;
        if (k == 2) continue;
        size_t ia, inn, oa, on;
        if (json_get(&o, "input", &ia, &inn) != 's') continue;
        if (skip_empty && inn == 0) continue;
        char ok_kind = json_get(&o, "output", &oa, &on);
        total++;
        clear(in);
        append_range(in, "https://", 8);
        append_range(in, json_pool + ia, inn);
        append_range(in, "/x", 2);
        url_parts p;
        int parsed = url_parse(data(in), len(in), 0, 0, href, sizeof href, &p);
        int good = 0;
        if (ok_kind != 's') good = !parsed;
        else if (parsed) {
            clear(want);
            append_range(want, "https://", 8);
            append_range(want, json_pool + oa, on);
            append_range(want, "/x", 2);
            size_t hn = url_attr(href, &p, URL_HOST, got, sizeof got);
            good = hn == on && memcmp(got, json_pool + oa, on) == 0;
            hn = url_attr(href, &p, URL_HOSTNAME, got, sizeof got);
            good = good && hn == on && memcmp(got, json_pool + oa, on) == 0;
            hn = url_attr(href, &p, URL_PATHNAME, got, sizeof got);
            good = good && hn == 2 && memcmp(got, "/x", 2) == 0;
            good = good && strlen(href) == len(want) && memcmp(href, data(want), len(want)) == 0;
        }
        passed += good;
        printf("%s %s <", good ? "ok  " : "FAIL", name);
        json_print(json_pool + ia, inn);
        printf("> -> ");
        if (parsed) json_print(href, strlen(href));
        else printf("failure");
        if (!good) {
            printf(", not ");
            if (ok_kind == 's') json_print(json_pool + oa, on);
            else printf("failure");
        }
        printf("\n");
    }
    printf("%s: %d of %d cases pass\n", name, passed, total);
}

/* domain to Unicode: the domain, and what a browser shows of it */
static const char *const unicode[][2] = {
    { "xn--bcher-kva.example", "b\xC3\xBC" "cher.example" }, { "example.com", "example.com" },
    { "xn--fa-hia.de", "fa\xC3\x9F.de" }, { "xn--r8jz45g.xn--zckzah", "\xE4\xBE\x8B\xE3\x81\x88.\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88" },
    { "xn--mgbh0fb.xn--kgbechtv", "\xD9\x85\xD8\xAB\xD8\xA7\xD9\x84.\xD8\xA5\xD8\xAE\xD8\xAA\xD8\xA8\xD8\xA7\xD8\xB1" },
    /* errors give the domain back: Punycode that is none, a label of ASCII alone, a capital */
    { "xn--a.example", "xn--a.example" }, { "xn--abc-.example", "xn--abc-.example" }, { "xn--8i7caa", "xn--8i7caa" },
};

/* a URL as a person is shown it: serialized, its domain in Unicode, no username or
   password; a host of another kind as it is; what is no URL as it is */
static const char *const shown[][2] = {
    { "https://xn--bcher-kva.example/x?y#z", "https://b\xC3\xBC" "cher.example/x?y#z" }, { "HTTP://EXAMPLE.com:8080", "http://example.com:8080/" },
    { "http://user:pw@xn--fa-hia.de/", "http://fa\xC3\x9F.de/" }, { "http://[::1]:81/", "http://[::1]:81/" },
    { "ftp://u@10.0.0.1/f", "ftp://10.0.0.1/f" },
    { "file:///tmp/a b.html", "file:///tmp/a%20b.html" }, { "tests/pages/news.html", "tests/pages/news.html" },
    { "http://xn--8i7caa/", "http://xn--8i7caa/" },
};

int main(void) {
    run("tests/net/wpt/toascii.json", "toascii", 0);
    run("tests/net/wpt/IdnaTestV2.json", "IdnaTestV2", 1);
    int passed = 0, total = 0;
    char out[512];
    for (size_t i = 0; i < sizeof unicode / sizeof unicode[0]; i++, total++) {
        url_domain_to_unicode(unicode[i][0], strlen(unicode[i][0]), out, sizeof out);
        int good = strcmp(out, unicode[i][1]) == 0;
        passed += good;
        printf("%s to Unicode <%s> -> ", good ? "ok  " : "FAIL", unicode[i][0]);
        json_print(out, strlen(out));
        printf("\n");
    }
    for (size_t i = 0; i < sizeof shown / sizeof shown[0]; i++, total++) {
        url_display(shown[i][0], strlen(shown[i][0]), out, sizeof out);
        int good = strcmp(out, shown[i][1]) == 0;
        passed += good;
        printf("%s shown <%s> as ", good ? "ok  " : "FAIL", shown[i][0]);
        json_print(out, strlen(out));
        printf("\n");
    }
    printf("domain to Unicode: %d of %d cases pass\n", passed, total);
    return 0;
}
