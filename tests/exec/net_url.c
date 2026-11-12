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
    { "file:///tmp/a b.html", "file:///tmp/a%20b.html" }, { "data:text/css,p{}", "data:text/css,p{}" },
};

/* hosts as WHATWG URL's host parser makes them, against the same base: IPv6
   literals compressed, IPv4 in its four forms, percent-decoding and the forbidden
   code points. Each answer is node's (new URL(ref, base).href, node 22) */
static const char *const hosts[][2] = {
    { "http://[::1]:8080/x", "http://[::1]:8080/x" }, { "http://[0:0:0:0:0:0:0:1]/", "http://[::1]/" },
    { "http://[2001:DB8::1]/", "http://[2001:db8::1]/" }, { "http://[2001:db8:0:0:1:0:0:1]/", "http://[2001:db8::1:0:0:1]/" },
    { "http://[::ffff:192.168.0.1]/", "http://[::ffff:c0a8:1]/" }, { "http://[1:0:0:2::3:0]/", "http://[1::2:0:0:3:0]/" },
    { "http://[::]/", "http://[::]/" }, { "http://[1::]/", "http://[1::]/" }, { "http://[::1:2:3:4:5:6:7]/", "http://[0:1:2:3:4:5:6:7]/" },
    { "http://[::127.0.0.1]/", "http://[::7f00:1]/" }, { "https://[::1]:443/", "https://[::1]/" }, { "//[::1]:81/a", "http://[::1]:81/a" },
    { "file://[::1]/x", "file://[::1]/x" }, { "file://localhost/x", "file:///x" },
    { "http://[:1]/", "(not a URL)" }, { "http://[1:2:3:4:5:6:7:8:9]/", "(not a URL)" }, { "http://[::1/", "(not a URL)" },
    { "http://[::1]x/", "(not a URL)" }, { "http://[1::2::3]/", "(not a URL)" }, { "http://[::1.2.3]/", "(not a URL)" },
    { "ws://[fe80::1%25eth0]/", "(not a URL)" },
    { "http://0x7f.1/", "http://127.0.0.1/" }, { "http://127.1/", "http://127.0.0.1/" }, { "http://0300.0250.0.1/", "http://192.168.0.1/" },
    { "http://4294967295/", "http://255.255.255.255/" }, { "http://0x/", "http://0.0.0.0/" }, { "http://1.2.3.4./", "http://1.2.3.4/" },
    { "http://4294967296/", "(not a URL)" }, { "http://1.2.3.256/", "(not a URL)" }, { "http://09.1/", "(not a URL)" },
    { "http://foo.09/", "(not a URL)" },
    { "http://ex%41mple.com/", "http://example.com/" }, { "http://a b/", "(not a URL)" }, { "http://a<b/", "(not a URL)" },
    { "http://a%2Fb/", "(not a URL)" }, { "http://h:1:2/", "(not a URL)" },
    /* IDNA: each label past ASCII lowercased as UTS #46 maps capitals, then xn-- and its Punycode (RFC 3492) */
    { "http://Bücher.example/", "http://xn--bcher-kva.example/" }, { "http://ÄÖÜ.de/", "http://xn--4ca0bs.de/" },
    { "http://münchen.de/", "http://xn--mnchen-3ya.de/" }, { "http://例え.テスト/", "http://xn--r8jz45g.xn--zckzah/" },
    { "http://ПРИМЕР.рф/", "http://xn--e1afmkfd.xn--p1ai/" }, { "http://faß.de/", "http://xn--fa-hia.de/" },
    { "http://☃.net/", "http://xn--n3h.net/" }, { "http://ΕΛΛΆΔΑ.gr/", "http://xn--hxakic4aa.gr/" },
    { "http://Ελληνικά.δοκιμή/", "http://xn--hxargifdar.xn--jxalpdlp/" }, { "http://ÀÉÎÕÜ.fr/", "http://xn--0casp2a9a.fr/" },
    { "http://ŁÓDŹ.pl/", "http://xn--d-uga0v4h.pl/" }, { "http://ĞÜŞİÖÇ.tr/", "http://xn--i-6fa8bwa8ovo32h.tr/" },
    { "http://Ἀθῆναι.gr/", "http://xn--mxaneo7006a87a.gr/" }, { "http://ꙀꙂ.test/", "http://xn--nw8ae.test/" },
    { "http://𐐀.test/", "http://xn--hj8c.test/" }, { "http://中文.example.com/", "http://xn--fiq228c.example.com/" },
    { "http://मराठी.भारत/", "http://xn--d2b1ag0dl.xn--h2brj9c/" }, { "http://a.b.ü/", "http://a.b.xn--tda/" },
    { "http://例え。テスト/", "http://xn--r8jz45g.xn--zckzah/" }, { "http://ＥＸＡＭＰＬＥ.com/", "http://example.com/" },
    { "http://b%C3%BCcher.example/", "http://xn--bcher-kva.example/" }, { "http://xn--bcher-kva.example/", "http://xn--bcher-kva.example/" },
    { "http://%FF.example/", "(not a URL)" },
};

static const char *const origins[][2] = {
    { "http://a/b/c/d;p?q", "http://a" }, { "http://Example.com:8080/x", "http://example.com:8080" },
    { "https://h:443/", "https://h" }, { "file:///tmp/x.html", "null" }, { "data:text/plain,hi", "null" },
    { "http://[::1]:8080/", "http://[::1]:8080" },
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
    { "http://p.test:3128", 0, 0, 0, "::1", "https://[::1]:9/", "" },
    { "http://p.test:3128", 0, 0, 0, "[::1]:9", "https://[::1]:9/", "" },
    { "http://p.test:3128", 0, 0, 0, "fe80::/10", "https://[fe80::1]:9/", "" },
    { "http://p.test:3128", 0, 0, 0, "fe80::/10", "https://[fec0::1]:9/", "http://p.test:3128" },
    { "socks5://p.test:1080", 0, 0, 0, 0, "https://a.test/", "refused: the proxy socks5://p.test:1080 is not an http: proxy, the only kind spoken here" },
    { "http://p.test:3128", 0, 0, 0, 0, "http://a.test/", "" },
    { 0, 0, "http://h.test:8080", 0, 0, "http://a.test/", "http://h.test:8080" },
    { 0, 0, 0, "http://h.test:8080", 0, "http://a.test/", "" },
    { 0, 0, "http://h.test:8080", 0, "a.test", "http://a.test/", "" },
    { "http://p.test:3128", 0, 0, 0, 0, "file:///etc/hostname", "" },
};

/* dates as a cookie's Expires is read (RFC 6265bis 5.1.1), HTTP's three forms among
   them: the seconds since the epoch, as Python's calendar.timegm gives them, or
   "none" for what is no date (a day the month lacks, a year before 1601, hour 25) */
static const char *const dates[][2] = {
    { "Wed, 01 Jan 2031 00:00:00 GMT", "1924992000" }, { "Sun, 06 Nov 1994 08:49:37 GMT", "784111777" },
    { "Sunday, 06-Nov-94 08:49:37 GMT", "784111777" }, { "Sun Nov  6 08:49:37 1994", "784111777" },
    { "Thu, 01 Jan 1970 00:00:00 GMT", "0" }, { "Thu, 01-Jan-70 00:00:01 GMT", "1" }, { "09 Jun 2021 10:18:14", "1623233894" },
    { "Thu, 29 Feb 2024 12:00:00 GMT", "1709208000" }, { "31-Dec-69 23:59:59", "3155759999" }, { "1 jan 1601 0:0:0", "-11644473600" },
    { "Tue, 29 Feb 2023 12:00:00 GMT", "none" }, { "Fri, 31 Feb 2023 00:00:00 GMT", "none" }, { "Mon, 01 Jan 1600 00:00:00 GMT", "none" },
    { "Mon, 01 Jan 2024 25:00:00 GMT", "none" }, { "2021-06-09T10:18:14Z", "none" }, { "tomorrow", "none" },
};

/* sites, as cookies' SameSite compares them: the scheme and the registrable domain,
   two labels or three under a public suffix of two (co.uk, github.io: the Public
   Suffix List's) */
static const char *const sites[][2] = {
    { "https://www.example.com/x", "https://example.com" }, { "http://a.b.example.co.uk/", "http://example.co.uk" },
    { "https://foo.github.io/", "https://foo.github.io" }, { "http://localhost:8080/", "http://localhost" },
    { "http://127.0.0.1:9/", "http://127.0.0.1" }, { "https://Example.COM./", "https://example.com" },
    { "http://co.uk/", "http://co.uk" }, { "file:///x", "null" }, { "data:text/plain,x", "null" },
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
    for (size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++, n++) {
        if (!net_url_resolve("http://a/b/c/d;p?q", hosts[i][0], out, sizeof out)) snprintf(out, sizeof out, "(not a URL)");
        int ok = strcmp(out, hosts[i][1]) == 0;
        bad += !ok;
        printf("%s '%s' -> %s%s%s\n", ok ? "ok  " : "FAIL", hosts[i][0], out, ok ? "" : ", not ", ok ? "" : hosts[i][1]);
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
    for (size_t i = 0; i < sizeof dates / sizeof dates[0]; i++, n++) {
        long long t = 0;
        if (net_url_date(dates[i][0], &t)) snprintf(out, sizeof out, "%lld", t);
        else snprintf(out, sizeof out, "none");
        int ok = strcmp(out, dates[i][1]) == 0;
        bad += !ok;
        printf("%s date '%s': %s%s%s\n", ok ? "ok  " : "FAIL", dates[i][0], out, ok ? "" : ", not ", ok ? "" : dates[i][1]);
    }
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++, n++) {
        if (!net_url_site(sites[i][0], out, sizeof out)) snprintf(out, sizeof out, "(not a URL)");
        int ok = strcmp(out, sites[i][1]) == 0;
        bad += !ok;
        printf("%s site of %s: %s%s%s\n", ok ? "ok  " : "FAIL", sites[i][0], out, ok ? "" : ", not ", ok ? "" : sites[i][1]);
    }
    printf("%d of %d cases\n", n - bad, n);
    return bad ? 1 : 0;
}
