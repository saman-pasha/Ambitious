/* tests/exec/brotli.c -- the Brotli decoder (src/paint/brotli.c) and the WOFF2 reader
   (src/paint/font.c).

   Each stream of tests/exec/brotli/ decoded: Brotli's own test vectors (google/brotli's
   tests/testdata, under its MIT license: src/paint/brotli_dict.h has the notice) -- its
   edge cases (empty streams written every way, a byte, runs, a distance of 65,536) and
   alice29.txt at quality 11 (the dictionary, its transforms, context modelling, block
   switches) -- and tests/pages/fonts/oblique.ttf compressed by Brotli 1.1.0 at qualities
   0, 5 and 11 (a window of 1,008 bytes): each one's length and CRC-32 are its original's
   (the .expect's lines, written from the originals). A cap one byte short refuses a
   stream. Then streams broken at random -- bytes set, runs of them, the stream cut short
   -- each decoded or refused, nothing read outside them (valgrind).

   The fixture pages' WOFF2 fonts (tests/pages/woff2.html) read as a page's are: each
   table of the sfnt made from them, its length and CRC-32 -- every one but glyf and loca
   the bytes fontTools 4.53.1 decompresses (the hmtx made again its metrics, which
   fontTools compiles anew); glyf and loca, made again as Google's decoder makes them,
   glyph by glyph fontTools' -- and each ASCII glyph drawn as the font it was made from
   draws it, each string as wide. Then WOFF2 files broken at random, read or refused. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/paint/brotli.h"
#include "../../src/paint/font.h"

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
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return c ^ 0xffffffffu;
}

static uint32_t be(const uint8_t *p, size_t i) { return (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3]; }

/* src broken from seed (a few bytes set at random, or a run of them, or the stream cut
   short) and decoded: 1 when it was, 0 when refused */
static int broken_stream(const vector<uint8_t> *src, uint32_t seed, size_t *bytes) {
    vector<uint8_t> b, out;
    append_range(b, data(*src), len(*src));
    if (!len(b)) return 0;
    uint32_t x = seed * 2654435761u + 1;
    if (seed % 7 == 0) {
        x = x * 1103515245u + 12345u;
        resize(&b, (x >> 8) % len(b));
    } else {
        int n = 1 + (int)(seed % 6);
        for (int k = 0; k < n; k++) {
            x = x * 1103515245u + 12345u;
            size_t p = (x >> 8) % len(b);
            x = x * 1103515245u + 12345u;
            if (seed % 5 == 0)
                for (size_t j = 0; j < 1 + (x >> 16) % 32 && p + j < len(b); j++) b[p + j] = (uint8_t)(x >> (j % 24));
            else b[p] ^= (uint8_t)(1u << ((x >> 16) % 8));
        }
    }
    int ok = amb_brotli(data(b), len(b), &out, (size_t)1 << 22);
    *bytes += len(out);
    return ok;
}

/* a WOFF2 file broken from seed, read into fonts of their own and its glyphs drawn: 1
   when it was read */
static int broken_woff2(const vector<uint8_t> *src, uint32_t seed, unsigned long *glyphs) {
    vector<uint8_t> b;
    append_range(b, data(*src), len(*src));
    uint32_t x = seed * 2654435761u + 3;
    int n = 1 + (int)(seed % 5);
    for (int k = 0; k < n; k++) {
        x = x * 1103515245u + 12345u;
        /* the header and directory more often than their share */
        size_t p = seed % 3 == 0 ? (x >> 8) % (len(b) < 120 ? len(b) : 120) : (x >> 8) % len(b);
        x = x * 1103515245u + 12345u;
        if (seed % 4 == 0)
            for (size_t j = 0; j < 1 + (x >> 16) % 16 && p + j < len(b); j++) b[p + j] = (uint8_t)(x >> (j % 24));
        else b[p] ^= (uint8_t)(1u << ((x >> 16) % 8));
    }
    amb_fonts g = {0};
    defer(g) { AMB_FONTS_DROP(g); }
    amb_fonts_load(&g);
    int face = amb_fonts_add(&g, data(b), len(b));
    if (face < 0) return 0;
    vector<float> acc;
    vector<uint8_t> mask;
    for (uint32_t c = 0x21; c < 0x7F; c++) {
        int w, h, ox, oy;
        if (amb_glyph_mask(&g, face, c, 16, 0, 0, &acc, &mask, &w, &h, &ox, &oy)) (*glyphs)++;
    }
    return 1;
}

/* the largest difference of two masks (each w x h, its top left at ox, oy) */
static int mask_diff(const vector<uint8_t> *m1, int w1, int h1, int x1, int y1, const vector<uint8_t> *m2, int w2, int h2, int x2, int y2) {
    if (w1 != w2 || h1 != h2 || x1 != x2 || y1 != y2) return 256;
    int worst = 0;
    for (size_t i = 0; i < len(*m1); i++) {
        int d = (int)at(*m1, i) - (int)at(*m2, i);
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

int main(void) {
    static const char *const vectors[38] = {
        "10x10y.br", "64x.br", "alice29.txt.br", "backward65536.br", "cp1251-utf16le.br", "cp852-utf8.br", "empty-00.br",
        "empty-01.br", "empty-02.br", "empty-03.br", "empty-04.br", "empty-05.br", "empty-06.br", "empty-07.br", "empty-08.br",
        "empty-09.br", "empty-10.br", "empty-11.br", "empty-12.br", "empty-13.br", "empty-14.br", "empty-15.br", "empty-16.br",
        "empty.br", "monkey.br", "oblique-q0-w10.br", "oblique-q11-w10.br", "oblique-q5-w18.br", "quickfox.br",
        "quickfox_repeated.br", "ukkonooa.br", "x-00.br", "x-01.br", "x-02.br", "x-03.br", "x.br", "xyzzy.br", "zeros.br" };
    for (int k = 0; k < 38; k++) {
        char path[256];
        snprintf(path, sizeof path, "tests/exec/brotli/%s", vectors[k]);
        vector<uint8_t> z, out;
        if (!read_file(path, &z)) { printf("%s: missing\n", vectors[k]); return 1; }
        int ok = amb_brotli(data(z), len(z), &out, (size_t)1 << 26);
        printf("%s: %zu bytes, crc32 %08x%s\n", vectors[k], len(out), crc32_of(data(out), len(out)), ok ? "" : " (refused)");
    }
    {
        vector<uint8_t> z, out;
        read_file("tests/exec/brotli/alice29.txt.br", &z);
        printf("alice29.txt.br with a cap of 152,088 bytes: %s\n", amb_brotli(data(z), len(z), &out, 152088) ? "decoded" : "refused");
    }
    /* broken streams */
    static const char *const brokenv[5] = { "alice29.txt.br", "oblique-q11-w10.br", "oblique-q0-w10.br", "monkey.br", "quickfox_repeated.br" };
    for (int k = 0; k < 5; k++) {
        char path[256];
        snprintf(path, sizeof path, "tests/exec/brotli/%s", brokenv[k]);
        vector<uint8_t> z;
        read_file(path, &z);
        unsigned long ok = 0;
        size_t bytes = 0;
        for (uint32_t seed = 1; seed <= 200; seed++) ok += (unsigned long)broken_stream(&z, seed, &bytes);
        printf("200 broken copies of %s: %lu decoded, %lu refused (%zu bytes out)\n", brokenv[k], ok, 200 - ok, bytes);
    }
    /* the WOFF2 fonts, against the fonts they were made from */
    static const char *const woff2[4] = { "serif-bold.woff2", "oblique-hmtx.woff2", "oblique-null.woff2", "sans-cff.woff2" };
    static const char *const orig[4] = { "serif-bold.woff", "oblique.ttf", "oblique.ttf", "sans-cff.otf" };
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    vector<float> acc;
    for (int k = 0; k < 4; k++) {
        char path[256];
        vector<uint8_t> w, o;
        snprintf(path, sizeof path, "tests/pages/fonts/%s", woff2[k]);
        read_file(path, &w);
        snprintf(path, sizeof path, "tests/pages/fonts/%s", orig[k]);
        read_file(path, &o);
        int a = amb_fonts_add(&f, data(w), len(w)), b = amb_fonts_add(&f, data(o), len(o));
        amb_face fc;
        if (!amb_face_record(&f, a, &fc)) { printf("%s: refused\n", woff2[k]); continue; }
        const uint8_t *s = data(f.bytes) + fc.base;
        uint32_t nt = (uint32_t)s[4] << 8 | s[5];
        printf("%s: face %d, an sfnt of %u bytes, %u tables:\n", woff2[k], a, fc.size, nt);
        for (uint32_t i = 0; i < nt; i++) {
            size_t r = 12 + 16 * (size_t)i;
            uint32_t off = be(s, r + 8), ln = be(s, r + 12);
            printf("  %c%c%c%c %u bytes, crc32 %08x\n", s[r], s[r + 1], s[r + 2], s[r + 3], ln, off + ln <= fc.size ? crc32_of(s + off, ln) : 0);
        }
        int worst = 0, drawn = 0;
        for (uint32_t c = 0x21; c < 0x7F; c++) {
            vector<uint8_t> m1, m2;
            int w1, h1, x1, y1, w2, h2, x2, y2;
            int d1 = amb_glyph_mask(&f, a, c, 20, 0.25, 0, &acc, &m1, &w1, &h1, &x1, &y1);
            int d2 = amb_glyph_mask(&f, b, c, 20, 0.25, 0, &acc, &m2, &w2, &h2, &x2, &y2);
            if (d1 != d2) { worst = 256; continue; }
            if (!d1) continue;
            drawn++;
            int d = mask_diff(&m1, w1, h1, x1, y1, &m2, w2, h2, x2, y2);
            if (d > worst) worst = d;
        }
        const char *text = "Sphinx of black quartz, judge my vow. AVAVAV To. Wa";
        printf("  %d glyphs drawn as %s draws them, within %d of 255; '%s' %.4f px (%s: %.4f)\n", drawn, orig[k], worst, text,
               amb_text_width(&f, a, 16, text, strlen(text)), orig[k], amb_text_width(&f, b, 16, text, strlen(text)));
    }
    /* broken WOFF2 files */
    for (int k = 0; k < 4; k++) {
        char path[256];
        vector<uint8_t> w;
        snprintf(path, sizeof path, "tests/pages/fonts/%s", woff2[k]);
        read_file(path, &w);
        unsigned long read = 0, glyphs = 0;
        for (uint32_t seed = 1; seed <= 150; seed++) read += (unsigned long)broken_woff2(&w, seed, &glyphs);
        printf("150 broken copies of %s: %lu read, %lu refused, %lu glyphs drawn\n", woff2[k], read, 150 - read, glyphs);
    }
    return 0;
}
