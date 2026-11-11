/* tests/exec/net_url.c -- URL resolution and origins (src/net/url.h): RFC 3986's
   examples (5.4.1, 5.4.2) against its base, WHATWG's special cases, origins */
#include <stdio.h>
#include <string.h>
#include "../../src/net/url.h"

static const char *const refs[][2] = {
    { "g:h", "g:h" }, { "g", "http://a/b/c/g" }, { "./g", "http://a/b/c/g" }, { "g/", "http://a/b/c/g/" },
    { "/g", "http://a/g" }, { "//g", "http://g/" }, { "?y", "http://a/b/c/d;p?y" }, { "g?y", "http://a/b/c/g?y" },
    { "#s", "http://a/b/c/d;p?q" }, { "g#s", "http://a/b/c/g" }, { "g?y#s", "http://a/b/c/g?y" }, { ";x", "http://a/b/c/;x" },
    { "g;x", "http://a/b/c/g;x" }, { "", "http://a/b/c/d;p?q" }, { ".", "http://a/b/c/" }, { "./", "http://a/b/c/" },
    { "..", "http://a/b/" }, { "../", "http://a/b/" }, { "../g", "http://a/b/g" }, { "../..", "http://a/" },
    { "../../", "http://a/" }, { "../../g", "http://a/g" },
    { "../../../g", "http://a/g" }, { "../../../../g", "http://a/g" }, { "/./g", "http://a/g" }, { "/../g", "http://a/g" },
    { "g.", "http://a/b/c/g." }, { ".g", "http://a/b/c/.g" }, { "g..", "http://a/b/c/g.." }, { "..g", "http://a/b/c/..g" },
    { "./../g", "http://a/b/g" }, { "./g/.", "http://a/b/c/g/" }, { "g/./h", "http://a/b/c/g/h" }, { "g/../h", "http://a/b/c/h" },
    { "g;x=1/./y", "http://a/b/c/g;x=1/y" }, { "g;x=1/../y", "http://a/b/c/y" },
    { "  HTTP://Example.COM:80/x\\y ", "http://example.com/x/y" }, { "http://u:p@h:8080/", "http://h:8080/" },
    { "file:///tmp/a b.html", "file:///tmp/a b.html" }, { "data:text/css,p{}", "data:text/css,p{}" },
};

static const char *const origins[][2] = {
    { "http://a/b/c/d;p?q", "http://a" }, { "http://Example.com:8080/x", "http://example.com:8080" },
    { "https://h:443/", "https://h" }, { "file:///tmp/x.html", "null" }, { "data:text/plain,hi", "null" },
};

int main(void) {
    int bad = 0, n = 0;
    char out[8192];
    for (size_t i = 0; i < sizeof refs / sizeof refs[0]; i++, n++) {
        if (!net_url_resolve("http://a/b/c/d;p?q", refs[i][0], out, sizeof out)) snprintf(out, sizeof out, "(not a URL)");
        int ok = strcmp(out, refs[i][1]) == 0;
        bad += !ok;
        printf("%s '%s' -> %s%s%s\n", ok ? "ok  " : "FAIL", refs[i][0], out, ok ? "" : ", not ", ok ? "" : refs[i][1]);
    }
    for (size_t i = 0; i < sizeof origins / sizeof origins[0]; i++, n++) {
        if (!net_url_origin(origins[i][0], out, sizeof out)) snprintf(out, sizeof out, "(not a URL)");
        int ok = strcmp(out, origins[i][1]) == 0;
        bad += !ok;
        printf("%s origin of %s: %s%s%s\n", ok ? "ok  " : "FAIL", origins[i][0], out, ok ? "" : ", not ", ok ? "" : origins[i][1]);
    }
    printf("%d of %d cases\n", n - bad, n);
    return bad ? 1 : 0;
}
