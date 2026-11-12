/* tests/exec/net_dict.c -- Compression Dictionary Transport's parts (RFC 9842): SHA-256
   (src/net/dict.c), Use-As-Dictionary read, a dictionary's match held to its URL and to the
   URLs it is offered for, and the decoders over a raw dictionary: the painter's Brotli
   (src/paint/brotli.c, dcb) and the zstd decoder (src/net/zstd.c, dcz).

   SHA-256's known answers (FIPS 180-2's: "abc", the two-block message, a million a's; and no
   bytes). The fields and patterns are the cases measured in Chromium (tests/integ/net-h2.sh's
   dictionary section holds Chromium to the same over the network): a field that is no
   structured field, a match that is no string, a type other than raw, an id too long, an
   empty one; a pattern of a path, with `*', with a search, with a named group, with a regular
   expression, of the dictionary's origin written whole and of another origin, relative.

   Each vector of tests/exec/dict/ (tools/gen-dict-vectors.py made them, by libbrotlienc 1.1.0
   and libzstd 1.5.5 over their dictionaries, and printed these lines' lengths and CRC-32s,
   the originals') decoded over its dictionary; without it, refused where it reaches into
   the dictionary; a bound one byte short refused. Then every vector broken at random --
   bytes set, the stream cut short -- each decoded or refused over its dictionary, nothing
   read outside it (valgrind). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/net/dict.h"
#include "../../src/paint/brotli.h"
#include "../../src/net/zstd.h"

static int read_file(const char *path, vector<uint8_t> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xffffffffu;
}

static void sha_line(const char *name, const uint8_t *p, size_t n) {
    uint8_t h[32];
    net_sha256(p, n, h);
    printf("sha256 %s: ", name);
    for (int i = 0; i < 32; i++) printf("%02x", h[i]);
    printf("\n");
}

static void field_line(const char *v) {
    net_dict_field f;
    int ok = net_dict_field_read(v, strlen(v), &f);
    printf("field %s: ", v);
    if (!ok) {
        printf("none\n");
        return;
    }
    printf("match %s", f.match);
    if (f.has_dest) {
        /* each destination quoted, a space between */
        printf(", match-dest (");
        int start = 1;
        for (size_t i = 0; f.dest[i]; i++) {
            if (start) putchar('"');
            start = 0;
            if (f.dest[i] != '\n') {
                putchar(f.dest[i]);
                continue;
            }
            putchar('"');
            if (f.dest[i + 1]) putchar(' ');
            start = 1;
        }
        printf(")");
    }
    if (f.id[0]) printf(", id %s", f.id);
    printf("\n");
}

static const char *const base = "https://a.localhost:4443/d/dict.js";

static void pattern_line(const char *pattern, const char *url) {
    printf("pattern %s: %s", pattern, net_dict_pattern_ok(pattern, base) ? "kept" : "not kept");
    if (url) printf("; %s %s", url, net_dict_pattern_matches(pattern, base, url) ? "matches" : "does not match");
    printf("\n");
}

static const char *const vectors[] = { "edited-small-q11.br", "edited-small-q5.br", "edited-small-q9-w10.br", "edited-small-q19.zst", "edited-small-q3.zst",
                                       "edited-small-q9-w10.zst", "unrelated-small-q11.br", "unrelated-small-q5.br", "unrelated-small-q9-w10.br",
                                       "unrelated-small-q19.zst", "unrelated-small-q3.zst", "unrelated-small-q9-w10.zst", "again-small-q11.br",
                                       "again-small-q5.br", "again-small-q9-w10.br", "again-small-q19.zst", "again-small-q3.zst",
                                       "again-small-q9-w10.zst", "empty-small-q11.br", "empty-small-q5.br", "empty-small-q9-w10.br",
                                       "empty-small-q19.zst", "empty-small-q3.zst", "empty-small-q9-w10.zst", "long-large-q11.br",
                                       "long-large-q5.br", "long-large-q9-w10.br", "long-large-q19.zst", "long-large-q3.zst",
                                       "long-large-q9-w10.zst" };

/* one vector decoded over d (dn bytes; none when dn is 0) into out, at most cap: 1 or 0 */
static int decode(const char *name, const vector<uint8_t> *in, const uint8_t *d, size_t dn, vector<uint8_t> *out, size_t cap) {
    clear(*out);
    size_t n = strlen(name);
    int cut = 0;
    if (n > 3 && strcmp(name + n - 3, ".br") == 0) return amb_brotli_prefix(data(*in), len(*in), d, dn, out, cap, &cut);
    char why[300];
    return net_zstd_dict(data(*in), len(*in), d, dn, out, cap, why, sizeof why, 0) == 1;
}

int main(void) {
    sha_line("of no bytes", (const uint8_t *)"", 0);
    sha_line("of abc", (const uint8_t *)"abc", 3);
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha_line("of the two-block message", (const uint8_t *)two, strlen(two));
    vector<uint8_t> million;
    resize(million, 1000000);
    memset(data(million), 'a', len(million));
    sha_line("of a million a's", data(million), len(million));

    field_line("match=\"/d/a/*\"");
    field_line("match=\"/d/e/*\", match-dest=(\"script\" \"style\")");
    field_line("match=\"/d/k/*\", match-dest=(\"\")");
    field_line("match=\"/d/f/*\", id=\"dict-f\"");
    field_line("match=\"/d/ie/*\", id=\"\"");
    field_line("match=\"/d/h/*\", type=other");
    field_line("match=\"/d/r/*\", type=raw");
    field_line("match=/d/bad/*");
    field_line("id=\"x\"");
    field_line("match=\"/d/x/*\", match-dest=\"script\"");
    field_line("match=\"/d/x/*\", match-dest=()");
    field_line("match=\"/d/x/*\";p=1, extra=?1, n=-1.5");
    field_line("match=\"/d/x/*\",");
    field_line("match=\"/d/\\\"q\\\\/*\"");

    pattern_line("/d/a/*", "https://a.localhost:4443/d/a/x.txt");
    pattern_line("/d/a/*", "https://a.localhost:4443/d/a/x.txt?q=1");
    pattern_line("/d/a/*", "https://a.localhost:4443/d/b/x.txt");
    pattern_line("/d/a/*", "https://b.localhost:4443/d/a/x.txt");
    pattern_line("/d/m/*.txt", "https://a.localhost:4443/d/m/x.txt");
    pattern_line("/d/m/*.txt", "https://a.localhost:4443/d/m/x.js");
    pattern_line("/d/n/*?*", "https://a.localhost:4443/d/n/x?q=1");
    pattern_line("/d/n/*?*", "https://a.localhost:4443/d/n/x");
    pattern_line("/d/s/*?v=1", "https://a.localhost:4443/d/s/x?v=1");
    pattern_line("/d/s/*?v=1", "https://a.localhost:4443/d/s/x?v=2");
    pattern_line("/d/o/:id", "https://a.localhost:4443/d/o/123");
    pattern_line("/d/o/:id", "https://a.localhost:4443/d/o/1/2");
    pattern_line("/d/l/(\\d+)", 0);
    pattern_line("/d/g/{x}", 0);
    pattern_line("/d/o/:id?", 0);
    pattern_line("https://a.localhost:4443/d/ab/*", "https://a.localhost:4443/d/ab/x.txt");
    pattern_line("https://b.localhost:4443/d/j/*", 0);
    pattern_line("x/*", "https://a.localhost:4443/d/x/y.txt");
    pattern_line("x/*", "https://a.localhost:4443/x/y.txt");
    pattern_line("?q=*", "https://a.localhost:4443/d/dict.js?q=7");
    pattern_line("/d/h/*#*", "https://a.localhost:4443/d/h/x");
    pattern_line("/d/h/*#top", 0);
    pattern_line("", 0);

    vector<uint8_t> small, large;
    if (!read_file("tests/exec/dict/small.dict", &small) || !read_file("tests/exec/dict/large.dict", &large)) {
        printf("the dictionaries are missing\n");
        return 1;
    }
    vector<uint8_t> in, out;
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
        char path[300];
        snprintf(path, sizeof path, "tests/exec/dict/%s", vectors[v]);
        clear(in);
        if (!read_file(path, &in)) {
            printf("%s: missing\n", vectors[v]);
            continue;
        }
        int big = strstr(vectors[v], "large") != 0;
        const uint8_t *d = big ? data(large) : data(small);
        size_t dn = big ? len(large) : len(small);
        if (!decode(vectors[v], &in, d, dn, &out, (size_t)1 << 24)) printf("%s: refused\n", vectors[v]);
        else printf("%s: %zu bytes, crc32 %08x\n", vectors[v], len(out), crc32_of(data(out), len(out)));
        size_t want = len(out);
        int alone = decode(vectors[v], &in, 0, 0, &out, (size_t)1 << 24);
        printf("  without its dictionary: %s\n", alone ? "decoded" : "refused");
        if (want) printf("  with a bound one byte short: %s\n", decode(vectors[v], &in, d, dn, &out, want - 1) ? "decoded" : "refused");
    }

    /* every vector broken at random, decoded or refused over its dictionary */
    uint32_t seed = 12345, decoded = 0, refused = 0;
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
        char path[300];
        snprintf(path, sizeof path, "tests/exec/dict/%s", vectors[v]);
        vector<uint8_t> orig;
        if (!read_file(path, &orig) || len(orig) == 0) continue;
        int big = strstr(vectors[v], "large") != 0;
        const uint8_t *d = big ? data(large) : data(small);
        size_t dn = big ? len(large) : len(small);
        for (int t = 0; t < 40; t++) {
            clear(in);
            append_range(in, data(orig), len(orig));
            seed = seed * 1103515245u + 12345u;
            uint32_t kind = seed >> 16 & 3;
            if (kind == 3) resize(in, (size_t)(seed >> 8) % len(in));
            else
                for (int k = 0; k <= (int)kind; k++) {
                    seed = seed * 1103515245u + 12345u;
                    in[(size_t)(seed >> 8) % len(in)] = (uint8_t)(seed >> 3);
                }
            if (decode(vectors[v], &in, d, dn, &out, (size_t)1 << 22)) decoded++;
            else refused++;
        }
    }
    printf("broken at random: %u decoded, %u refused, of %u\n", decoded, refused, decoded + refused);
    return 0;
}
