/* tests/exec/net_hpack.c -- HPACK (src/net/hpack.c), HTTP/2's header compression.

   RFC 7541's Appendix C, every example: the integers of C.1 encoded and decoded; the
   fields of C.2 (with indexing, without, never indexed, indexed); the requests of C.3 and
   C.4 and the responses of C.5 and C.6 (a table of 256 bytes, evictions), each block
   decoded -- its fields and the dynamic table after it, as the RFC prints them -- and the
   same lists encoded again, to the RFC's own bytes (its encoder Huffman codes a string as
   long so as raw, where Chromium's codes it raw). Then Chromium's own header blocks,
   eight requests of one connection as its encoder wrote them (tests/exec/hpack/
   chromium.txt, logged by a TLS front to a node h2 server, cookie crumbs among them):
   each encoded here by Chromium's policy to the same bytes, and decoded. Then blocks
   that are no HPACK, each refused with its reason; a table under pressure, and a name
   whose entry its own insertion evicts (4.4); a list past its bound, its table kept in
   step; lists made at random, encoded under changing table sizes and decoded to
   themselves, both tables the same size after each; and blocks broken at random, each
   decoded or refused, nothing read outside it (valgrind). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/net/hpack.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
/* hex, spaces aside, as bytes */
static void unhex(const char *s, vector<uint8_t> *out) {
    int hi = -1;
    for (size_t i = 0; s[i]; i++) {
        int v = hexval(s[i]);
        if (v < 0) continue;
        if (hi < 0) hi = v;
        else {
            push_back(out, (uint8_t)(hi * 16 + v));
            hi = -1;
        }
    }
}
/* bytes as the RFC prints them, in groups of two */
static void hexline(const char *label, const uint8_t *p, size_t n) {
    printf("%s", label);
    for (size_t i = 0; i < n; i++) printf("%s%02x", i % 2 == 0 ? " " : "", p[i]);
    printf("\n");
}

static void add(vector<hpack_field> *fs, vector<uint8_t> *b, const char *name, const char *value, uint32_t never) {
    hpack_field f = { (uint32_t)len(*b), (uint32_t)strlen(name), 0, (uint32_t)strlen(value), never };
    append_range(b, name, strlen(name));
    f.value = (uint32_t)len(*b);
    append_range(b, value, strlen(value));
    push_back(fs, f);
}

static void show_fields(const vector<hpack_field> *fs, const vector<uint8_t> *b) {
    for (size_t i = 0; i < len(*fs); i++) {
        hpack_field f = (*fs)[i];
        printf("  %.*s: %.*s%s\n", (int)f.nlen, (const char *)data(*b) + f.name, (int)f.vlen, (const char *)data(*b) + f.value,
               f.never ? " (never indexed)" : "");
    }
}

/* the dynamic table, newest first, as the RFC prints it */
static void show_table(const hpack_table *t) {
    size_t n = len(t->ents);
    for (size_t k = n; k > t->first; k--) {
        hpack_entry e = t->ents[k - 1];
        printf("  [%3zu] (s = %3u) %.*s: %.*s\n", n - k + 1, e.nlen + e.vlen + 32, (int)e.nlen, (const char *)data(t->bytes) + e.off,
               (int)e.vlen, (const char *)data(t->bytes) + e.off + e.nlen);
    }
    printf("  Table size: %u\n", t->size);
}

/* an example: its list (names and values in pairs, ending at a null name), its block as
   the RFC gives it; decoded by dec, encoded by enc */
static void example(const char *title, const char *const *nv, const char *hex, hpack_table *dec, hpack_table *enc, int policy, int huffman) {
    vector<uint8_t> want, out, fb, lb;
    vector<hpack_field> fs, ls;
    unhex(hex, &want);
    printf("%s\n", title);
    char why[200];
    int r = hpack_decode(dec, data(want), len(want), &fs, &fb, 65536, why, sizeof why);
    if (r != HPACK_OK) printf("  decoding failed: %s\n", why);
    show_fields(&fs, &fb);
    show_table(dec);
    for (size_t i = 0; nv[i]; i += 2) add(&ls, &lb, nv[i], nv[i + 1], nv[i][0] == 'p' && strcmp(nv[i], "password") == 0);
    hpack_encode(enc, data(ls), len(ls), data(lb), policy, huffman, &out);
    if (len(out) == len(want) && memcmp(data(out), data(want), len(out)) == 0) printf("  encoded again: the RFC's %zu bytes\n", len(out));
    else hexline("  encoded again, NOT the RFC's:", data(out), len(out));
}

/* C.3 to C.6's lists */
static const char *const req1[] = { ":method", "GET", ":scheme", "http", ":path", "/", ":authority", "www.example.com", 0 };
static const char *const req2[] = { ":method", "GET", ":scheme", "http", ":path", "/", ":authority", "www.example.com", "cache-control",
                                    "no-cache", 0 };
static const char *const req3[] = { ":method", "GET", ":scheme", "https", ":path", "/index.html", ":authority", "www.example.com",
                                    "custom-key", "custom-value", 0 };
static const char *const resp1[] = { ":status", "302", "cache-control", "private", "date", "Mon, 21 Oct 2013 20:13:21 GMT", "location",
                                     "https://www.example.com", 0 };
static const char *const resp2[] = { ":status", "307", "cache-control", "private", "date", "Mon, 21 Oct 2013 20:13:21 GMT", "location",
                                     "https://www.example.com", 0 };
static const char *const resp3[] = { ":status", "200", "cache-control", "private", "date", "Mon, 21 Oct 2013 20:13:22 GMT", "location",
                                     "https://www.example.com", "content-encoding", "gzip", "set-cookie",
                                     "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1", 0 };

/* a pair of tables, a decoder's and an encoder's, of a limit */
static void sequence(const char *title, uint32_t limit, int huffman, const char *const *const *lists, const char *const *hexes, int n) {
    hpack_table dec, enc;
    memset(&dec, 0, sizeof dec);
    memset(&enc, 0, sizeof enc);
    defer(dec) { HPACK_TABLE_DROP(dec); }
    defer(enc) { HPACK_TABLE_DROP(enc); }
    hpack_init(&dec, limit);
    hpack_init(&enc, limit);
    printf("== %s\n", title);
    for (int i = 0; i < n; i++) {
        char t[64];
        snprintf(t, sizeof t, "-- block %d", i + 1);
        example(t, lists[i], hexes[i], &dec, &enc, HPACK_INDEX_ALL, huffman);
    }
}

/* one block decoded by a fresh table, what came of it said */
static void refuse(const char *label, const char *hex, uint32_t limit) {
    hpack_table t;
    memset(&t, 0, sizeof t);
    defer(t) { HPACK_TABLE_DROP(t); }
    hpack_init(&t, limit);
    vector<uint8_t> in, b;
    vector<hpack_field> fs;
    unhex(hex, &in);
    char why[200];
    why[0] = 0;
    int r = hpack_decode(&t, data(in), len(in), &fs, &b, 65536, why, sizeof why);
    printf("%s: %s%s%s\n", label, r == HPACK_OK ? "decoded" : r == HPACK_BAD ? "refused" : "too large", why[0] ? ", " : "", why);
}

/* a literal with incremental indexing, its name new and raw, its value count bytes of ch */
static void lit(vector<uint8_t> *in, const char *name, char ch, int count) {
    push_back(in, (uint8_t)0x40);
    hpack_int_encode((uint32_t)strlen(name), 7, 0, in);
    append_range(in, name, strlen(name));
    hpack_int_encode((uint32_t)count, 7, 0, in);
    for (int i = 0; i < count; i++) push_back(in, (uint8_t)ch);
}

/* what a decoding came to: "decoded", or its reason (no ?: over the array: toolchain/REPORT.md,
   finding 14) */
static void say(const char *label, int r, const char *why) {
    if (r == HPACK_OK) printf("%s: decoded\n", label);
    else printf("%s: %s\n", label, why);
}

static uint32_t rnd(uint32_t *x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

/* Chromium's blocks (tests/exec/hpack/chromium.txt): each list encoded by Chromium's policy
   into the same bytes, and the block decoded into the same list */
static void chromium_blocks(void) {
    vector<char> text;
    {
        own FILE *f = fopen("tests/exec/hpack/chromium.txt", "rb");
        if (!f) {
            printf("tests/exec/hpack/chromium.txt: missing\n");
            return;
        }
        defer(f) { fclose(f); }
        char buf[4096];
        size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) append_range(text, buf, k);
    }
    push_back(text, 0);
    hpack_table enc, dec;
    memset(&enc, 0, sizeof enc);
    memset(&dec, 0, sizeof dec);
    defer(enc) { HPACK_TABLE_DROP(enc); }
    defer(dec) { HPACK_TABLE_DROP(dec); }
    hpack_init(&enc, 4096);
    hpack_init(&dec, 4096);
    vector<hpack_field> ls;
    vector<uint8_t> lb;
    unsigned same_enc = 0, same_dec = 0, blocks = 0, stream = 0;
    size_t total = 0, i = 0, n = len(text) - 1;
    char line[2048];
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        snprintf(line, sizeof line, "%.*s", (int)(e - i), data(text) + i);
        i = e + 1;
        if (strncmp(line, "stream ", 7) == 0) {
            stream = (unsigned)atoi(line + 7);
            clear(ls);
            clear(lb);
        } else if (strncmp(line, "block ", 6) == 0) {
            vector<uint8_t> want, out, fb;
            vector<hpack_field> fs;
            unhex(line + 6, &want);
            hpack_encode(&enc, data(ls), len(ls), data(lb), HPACK_CHROMIUM, 1, &out);
            int se = len(out) == len(want) && memcmp(data(out), data(want), len(out)) == 0;
            char why[200];
            int r = hpack_decode(&dec, data(want), len(want), &fs, &fb, 262144, why, sizeof why);
            int sd = r == HPACK_OK && len(fs) == len(ls) && len(fb) == len(lb) && memcmp(data(fb), data(lb), len(fb)) == 0;
            printf("stream %u: %zu fields, Chromium's block %zu bytes; ours %s, its decoding %s\n", stream, len(ls), len(want),
                   se ? "the same" : "DIFFERENT", sd ? "the list" : "NOT the list");
            if (!se) {
                hexline("  Chromium's:", data(want), len(want));
                hexline("  ours:      ", data(out), len(out));
            }
            same_enc += (unsigned)se;
            same_dec += (unsigned)sd;
            blocks++;
            total += len(want);
        } else {
            /* name: value, the name perhaps a pseudo-header's */
            char *c = strstr(line + 1, ": ");
            if (!c) continue;
            *c = 0;
            add(&ls, &lb, line, c + 2, 0);
        }
    }
    printf("%u of %u blocks encoded as Chromium encoded them, %u decoded to their lists (%zu bytes); the encoder's table %u bytes, %zu entries\n",
           same_enc, blocks, same_dec, total, enc.size, len(enc.ents) - enc.first);
}

int main(void) {
    printf("== C.1 integers\n");
    {
        vector<uint8_t> a, b, c;
        hpack_int_encode(10, 5, 0, &a);
        hpack_int_encode(1337, 5, 0, &b);
        hpack_int_encode(42, 8, 0, &c);
        hexline("C.1.1 10, a 5-bit prefix:", data(a), len(a));
        hexline("C.1.2 1337, a 5-bit prefix:", data(b), len(b));
        hexline("C.1.3 42 at an octet's start:", data(c), len(c));
        size_t at = 0;
        uint32_t v1 = 0, v2 = 0, v3 = 0;
        hpack_int_decode(data(a), len(a), &at, 5, &v1);
        at = 0;
        hpack_int_decode(data(b), len(b), &at, 5, &v2);
        at = 0;
        hpack_int_decode(data(c), len(c), &at, 8, &v3);
        printf("decoded again: %u %u %u\n", v1, v2, v3);
    }
    printf("== C.2 header fields, each into an empty table\n");
    {
        static const char *const c21[] = { "custom-key", "custom-header", 0 };
        static const char *const c22[] = { ":path", "/sample/path", 0 };
        static const char *const c23[] = { "password", "secret", 0 };
        static const char *const c24[] = { ":method", "GET", 0 };
        static const char *const hx[4] = { "400a 6375 7374 6f6d 2d6b 6579 0d63 7573 746f 6d2d 6865 6164 6572", "040c 2f73 616d 706c 652f 7061 7468",
                                           "1008 7061 7373 776f 7264 0673 6563 7265 74", "82" };
        static const char *const titles[4] = { "-- C.2.1 a literal with indexing", "-- C.2.2 a literal without indexing (Chromium's policy)",
                                               "-- C.2.3 a literal never indexed", "-- C.2.4 an indexed field" };
        for (int i = 0; i < 4; i++) {
            hpack_table dec, enc;
            memset(&dec, 0, sizeof dec);
            memset(&enc, 0, sizeof enc);
            defer(dec) { HPACK_TABLE_DROP(dec); }
            defer(enc) { HPACK_TABLE_DROP(enc); }
            hpack_init(&dec, 4096);
            hpack_init(&enc, 4096);
            const char *const *nv = c21;
            if (i == 1) nv = c22;
            else if (i == 2) nv = c23;
            else if (i == 3) nv = c24;
            example(titles[i], nv, hx[i], &dec, &enc, i == 1 ? HPACK_CHROMIUM : HPACK_INDEX_ALL, 0);
        }
    }
    {
        const char *const *const reqs[3] = { req1, req2, req3 };
        const char *const *const resps[3] = { resp1, resp2, resp3 };
        static const char *const c3[3] = { "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d", "8286 84be 5808 6e6f 2d63 6163 6865",
                                           "8287 85bf 400a 6375 7374 6f6d 2d6b 6579 0c63 7573 746f 6d2d 7661 6c75 65" };
        static const char *const c4[3] = { "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff", "8286 84be 5886 a8eb 1064 9cbf",
                                           "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925 a849 e95b b8e8 b4bf" };
        static const char *const c5[3] = {
            "4803 3330 3258 0770 7269 7661 7465 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32 3120 474d 546e 1768 "
            "7474 7073 3a2f 2f77 7777 2e65 7861 6d70 6c65 2e63 6f6d",
            "4803 3330 37c1 c0bf",
            "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32 3220 474d 54c0 5a04 677a 6970 7738 666f 6f3d 4153 "
            "444a 4b48 514b 425a 584f 5157 454f 5049 5541 5851 5745 4f49 553b 206d 6178 2d61 6765 3d33 3630 303b 2076 6572 7369 6f6e 3d31" };
        static const char *const c6[3] = {
            "4882 6402 5885 aec3 771a 4b61 96d0 7abe 9410 54d4 44a8 2005 9504 0b81 66e0 82a6 2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8 "
            "e9ae 82ae 43d3",
            "4883 640e ffc1 c0bf",
            "88c1 6196 d07a be94 1054 d444 a820 0595 040b 8166 e084 a62d 1bff c05a 839b d9ab 77ad 94e7 821d d7f2 e6c7 b335 dfdf cd5b "
            "3960 d5af 2708 7f36 72c1 ab27 0fb5 291f 9587 3160 65c0 03ed 4ee5 b106 3d50 07" };
        sequence("C.3 requests without Huffman coding", 4096, 0, reqs, c3, 3);
        sequence("C.4 requests with Huffman coding", 4096, 2, reqs, c4, 3);
        sequence("C.5 responses without Huffman coding (SETTINGS_HEADER_TABLE_SIZE 256)", 256, 0, resps, c5, 3);
        sequence("C.6 responses with Huffman coding (SETTINGS_HEADER_TABLE_SIZE 256)", 256, 2, resps, c6, 3);
    }

    printf("== Chromium's blocks, one connection's (tests/exec/hpack/chromium.txt)\n");
    chromium_blocks();

    printf("== blocks that are no HPACK\n");
    refuse("index 0", "80", 4096);
    refuse("index 62, the dynamic table empty", "be", 4096);
    refuse("an integer past 2^28", "ff ff ff ff ff 0f", 4096);
    refuse("an integer cut short", "ff 80", 4096);
    refuse("a size update after a field", "82 3f e1 1f", 4096);
    refuse("a size update past the limit (4096 allowed, 4097 asked)", "3f e2 1f", 4096);
    refuse("two size updates at the start (0, then 4096)", "20 3f e1 1f 82", 4096);
    refuse("a string longer than the block", "40 05 61 62", 4096);
    refuse("a Huffman string with EOS in it", "40 01 61 84 ff ff ff ff", 4096);
    refuse("a Huffman string padded with 11 bits", "40 01 61 82 1f ff", 4096);
    refuse("a Huffman string padded with a 0 bit", "40 01 61 81 1e", 4096);
    refuse("a Huffman string padded rightly", "40 01 61 81 1f", 4096);
    refuse("a literal never indexed", "10 01 78 01 79", 4096);

    printf("== the dynamic table under pressure (a limit of 100)\n");
    {
        hpack_table t;
        memset(&t, 0, sizeof t);
        defer(t) { HPACK_TABLE_DROP(t); }
        hpack_init(&t, 100);
        char why[200];
        vector<uint8_t> in, b;
        vector<hpack_field> fs;
        /* a: 1 (34), bb: 22 (36), then ccc: 333 (38) evicts a */
        unhex("40 01 61 01 31 40 02 62 62 02 32 32 40 03 63 63 63 03 33 33 33", &in);
        int r = hpack_decode(&t, data(in), len(in), &fs, &b, 65536, why, sizeof why);
        say("three literals indexed", r, why);
        show_table(&t);
        /* a literal named by entry 63 (bb), whose own insertion (38 + 3 + 32 bytes) evicts bb */
        clear(in);
        clear(fs);
        clear(b);
        unhex("7f 00 08 78 78 78 78 78 78 78 78", &in);
        r = hpack_decode(&t, data(in), len(in), &fs, &b, 65536, why, sizeof why);
        say("a literal named by entry 63, which its insertion evicts", r, why);
        show_fields(&fs, &b);
        show_table(&t);
        /* an entry larger than the table empties it */
        clear(in);
        clear(fs);
        clear(b);
        lit(&in, "big", 'a', 70);
        r = hpack_decode(&t, data(in), len(in), &fs, &b, 65536, why, sizeof why);
        say("an entry of 105 bytes, larger than the table, which it empties", r, why);
        show_table(&t);
        clear(in);
        clear(fs);
        clear(b);
        unhex("3f 25 40 01 61 01 31", &in);
        r = hpack_decode(&t, data(in), len(in), &fs, &b, 65536, why, sizeof why);
        say("a size update to 68, then a: 1", r, why);
        show_table(&t);
    }

    printf("== a list past its bound (100 bytes), its table kept in step\n");
    {
        hpack_table t;
        memset(&t, 0, sizeof t);
        defer(t) { HPACK_TABLE_DROP(t); }
        hpack_init(&t, 4096);
        char why[200];
        vector<uint8_t> in, b;
        vector<hpack_field> fs;
        /* x-a: 40 a's, indexed (75 bytes); then x-b: 40 b's, indexed: past 100 */
        lit(&in, "x-a", 'a', 40);
        lit(&in, "x-b", 'b', 40);
        int r = hpack_decode(&t, data(in), len(in), &fs, &b, 100, why, sizeof why);
        if (r != HPACK_TOO_LARGE) printf("NOT too large\n");
        printf("%s; %zu field kept; the table holds %zu entries\n", why, len(fs), len(t.ents) - t.first);
        clear(in);
        clear(fs);
        clear(b);
        unhex("be", &in);
        r = hpack_decode(&t, data(in), len(in), &fs, &b, 100, why, sizeof why);
        printf("the next block's index 62, the entry the last one added:\n");
        show_fields(&fs, &b);
    }

    printf("== lists made at random, encoded and decoded, the tables' sizes changing\n");
    {
        hpack_table enc, dec;
        memset(&enc, 0, sizeof enc);
        memset(&dec, 0, sizeof dec);
        defer(enc) { HPACK_TABLE_DROP(enc); }
        defer(dec) { HPACK_TABLE_DROP(dec); }
        hpack_init(&enc, 4096);
        hpack_init(&dec, 65536);
        static const char *const names[12] = { ":method", ":path", ":authority", "accept", "cookie", "x-trace", "user-agent", "referer",
                                               "content-type", "x-long-header-name-here", "set-cookie", "cache-control" };
        uint32_t x = 2463534242u;
        unsigned ok = 0, blocks = 0, updates = 0;
        size_t bytes = 0;
        for (int round = 0; round < 3000; round++) {
            if (rnd(&x) % 50 == 0) {
                static const uint32_t sizes[6] = { 0, 64, 256, 4096, 16384, 65536 };
                hpack_set_limit(&enc, sizes[rnd(&x) % 6]);
                updates++;
            }
            vector<hpack_field> ls, fs;
            vector<uint8_t> lb, fb, out;
            int nf = 1 + (int)(rnd(&x) % 12);
            for (int k = 0; k < nf; k++) {
                char v[300];
                int vl = (int)(rnd(&x) % 120);
                for (int j = 0; j < vl; j++) v[j] = (char)(rnd(&x) % 4 ? 'a' + rnd(&x) % 26 : (char)(32 + rnd(&x) % 95));
                v[vl] = 0;
                if (rnd(&x) % 3 == 0) snprintf(v, sizeof v, "value-%u", rnd(&x) % 8);
                add(&ls, &lb, names[rnd(&x) % 12], v, rnd(&x) % 40 == 0);
            }
            hpack_encode(&enc, data(ls), len(ls), data(lb), round % 2 ? HPACK_CHROMIUM : HPACK_INDEX_ALL, round % 3 != 0, &out);
            char why[200];
            int r = hpack_decode(&dec, data(out), len(out), &fs, &fb, 1 << 20, why, sizeof why);
            int same = r == HPACK_OK && len(fs) == len(ls) && len(fb) == len(lb) && memcmp(data(fb), data(lb), len(fb)) == 0 &&
                       enc.size == dec.size && enc.max == dec.max;
            for (size_t k = 0; same && k < len(fs); k++) same = fs[k].never == ls[k].never;
            ok += (unsigned)same;
            blocks++;
            bytes += len(out);
        }
        printf("%u of %u blocks (%zu bytes, %u table size changes) decoded to their lists, both tables the same size after each\n", ok,
               blocks, bytes, updates);
    }

    printf("== blocks broken at random, each decoded or refused\n");
    {
        uint32_t x = 7;
        unsigned decoded = 0, refused = 0, too = 0;
        static const char *const seeds[4] = { "82 86 84 41 8c f1 e3 c2 e5 f2 3a 6b a0 ab 90 f4 ff", "48 82 64 02 58 85 ae c3 77 1a 4b 61 96 d0 7a be",
                                              "88 c1 61 96 d0 7a be 94 10 54 d4 44 a8 20 05 95 04 0b 81 66 e0 84 a6 2d 1b ff c0 5a 83 9b",
                                              "3f e1 1f 40 03 78 2d 61 03 61 61 61 10 01 78 01 79 be bf" };
        for (int round = 0; round < 20000; round++) {
            hpack_table t;
            memset(&t, 0, sizeof t);
            defer(t) { HPACK_TABLE_DROP(t); }
            hpack_init(&t, 4096);
            vector<uint8_t> in, b;
            vector<hpack_field> fs;
            if (round % 4 == 3) {
                int n = (int)(rnd(&x) % 64);
                for (int k = 0; k < n; k++) push_back(in, (uint8_t)rnd(&x));
            } else {
                unhex(seeds[rnd(&x) % 4], &in);
                int k = 1 + (int)(rnd(&x) % 4);
                for (int j = 0; j < k && len(in); j++) {
                    size_t p = rnd(&x) % len(in);
                    if (rnd(&x) % 2) in[p] = (uint8_t)rnd(&x);
                    else in[p] ^= (uint8_t)(1u << (rnd(&x) % 8));
                }
                if (rnd(&x) % 5 == 0) resize(in, rnd(&x) % (len(in) + 1));
            }
            char why[200];
            int r = hpack_decode(&t, data(in), len(in), &fs, &b, 200, why, sizeof why);
            if (r == HPACK_OK) decoded++;
            else if (r == HPACK_BAD) refused++;
            else too++;
        }
        printf("20000 blocks: %u decoded, %u refused, %u past their bound\n", decoded, refused, too);
    }
    return 0;
}
