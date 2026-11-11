/* tests/exec/net_url.c -- URL resolution and origins (src/net/url.h): RFC 3986's
   examples (5.4.1, 5.4.2) against its base, WHATWG's special cases, origins; and
   the proxy the environment names for a URL */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

/* the proxy for a URL: https_proxy, HTTPS_PROXY, http_proxy, HTTP_PROXY and no_proxy as
   given (0: unset), the URL, and the proxy taken ("" for none). Where curl 8.5 reads a
   no_proxy entry, its choice is the same (asked with a proxy that answers nothing);
   an entry's port and its `*.' follow Go's net/http, which curl does not read */
static const char *const proxies[][7] = {
    { "http://127.0.0.1:3128", 0, 0, 0, 0, "https://a.test/", "http://127.0.0.1:3128" },
    { "127.0.0.1:3128", 0, 0, 0, 0, "https://a.test/", "http://127.0.0.1:3128" },
    { 0, "http://P.test:8080", 0, 0, 0, "https://a.test/x", "http://p.test:8080" },
    { "http://u:p@proxy.test:8080/", 0, 0, 0, 0, "https://a.test/", "http://proxy.test:8080" },
    { "http://p.test", 0, 0, 0, 0, "https://a.test/", "http://p.test:80" },
    { "http://p.test:3128", 0, 0, 0, "*", "https://a.test/", "" },
    { "http://p.test:3128", 0, 0, 0, "example.test", "https://example.test/", "" },
    { "http://p.test:3128", 0, 0, 0, "example.test", "https://www.example.test/", "" },
    { "http://p.test:3128", 0, 0, 0, "example.test", "https://notexample.test/", "http://p.test:3128" },
    { "http://p.test:3128", 0, 0, 0, ".example.test", "https://example.test/", "" },
    { "http://p.test:3128", 0, 0, 0, ".example.test", "https://www.example.test/", "" },
    { "http://p.test:3128", 0, 0, 0, "*.example.test", "https://a.b.example.test/", "" },
    { "http://p.test:3128", 0, 0, 0, "EXAMPLE.test", "https://www.Example.TEST/", "" },
    { "http://p.test:3128", 0, 0, 0, "a.test", "https://a.test./", "" },
    { "http://p.test:3128", 0, 0, 0, "localhost,127.0.0.1", "https://localhost:9/", "" },
    { "http://p.test:3128", 0, 0, 0, "127.0.0.0/8", "https://127.1.2.3:9/", "" },
    { "http://p.test:3128", 0, 0, 0, "127.0.0.0/8", "https://128.0.0.1:9/", "http://p.test:3128" },
    { "http://p.test:3128", 0, 0, 0, "10.0.0.0/8, 192.168.0.0/16", "https://192.168.4.5:8443/", "" },
    { "http://p.test:3128", 0, 0, 0, "example.test:8443", "https://example.test:8443/", "" },
    { "http://p.test:3128", 0, 0, 0, "example.test:8443", "https://example.test/", "http://p.test:3128" },
    { "socks5://p.test:1080", 0, 0, 0, 0, "https://a.test/", "refused: the proxy socks5://p.test:1080 is not an http: proxy, the only kind spoken here" },
    { "http://p.test:3128", 0, 0, 0, 0, "http://a.test/", "" },
    { 0, 0, "http://h.test:8080", 0, 0, "http://a.test/", "http://h.test:8080" },
    { 0, 0, 0, "http://h.test:8080", 0, "http://a.test/", "" },
    { 0, 0, "http://h.test:8080", 0, "a.test", "http://a.test/", "" },
    { "http://p.test:3128", 0, 0, 0, 0, "file:///etc/hostname", "" },
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
    static const char *const vars[5] = { "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY", "no_proxy" };
    unsetenv("NO_PROXY");
    for (size_t i = 0; i < sizeof proxies / sizeof proxies[0]; i++, n++) {
        for (int v = 0; v < 5; v++) {
            if (proxies[i][v]) setenv(vars[v], proxies[i][v], 1);
            else unsetenv(vars[v]);
        }
        net_url_proxy(proxies[i][5], out, sizeof out);
        int ok = strcmp(out, proxies[i][6]) == 0;
        bad += !ok;
        printf("%s proxy for %s with no_proxy '%s': %s%s%s\n", ok ? "ok  " : "FAIL", proxies[i][5], proxies[i][4] ? proxies[i][4] : "",
               out[0] ? (const char *)out : "none", ok ? "" : ", not ", ok ? "" : proxies[i][6]);
    }
    printf("%d of %d cases\n", n - bad, n);
    return bad ? 1 : 0;
}
