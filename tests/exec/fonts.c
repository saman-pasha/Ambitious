/* tests/exec/fonts.c -- the font reader (src/paint/font.c): the built-in faces, and
   the fixture pages' web fonts read as a page's are (a TrueType file and a WOFF; CFF,
   CID-keyed CFF, CFF2 and CFF in WOFF), each measured -- metrics rounded as Chromium
   rounds them, advances and kerning -- and a glyph drawn; the families that name
   built-in faces; the code points a face has no glyph for, drawn by its fallbacks; fonts
   whose CFF tables are broken, read and drawn without a read outside them; and bytes that
   are no font, refused. Every width
   here is the one tools/gen-font.py's own reader gives from the Liberation and DejaVu
   files, kern table and all; the built-in faces' are Chromium's on a machine with those
   fonts within 0.01 px (tools/bench: 'AVAVAV To. Wa' at 16px is 111.4531 px in
   sans-serif, 110.4688 in bold serif, 125.2344 in monospace, 124.8281 in Courier New,
   116.9844 in DejaVu Sans; U+2011 as wide as U+2010, two Hebrew letters in monospace
   17.3125, U+25B8 in serif 8.0313), but for the stars and checks it takes from Inter
   where that is installed (U+2605 16.7188 px there). The italic faces are measured by
   their own advances ('Hamburgefonstiv AVAVAV To. Wa' at 16px: 215.0938 px in italic
   serif, 238.6719 in italic sans-serif, tests/pages/italic.chromium); DejaVu Sans's
   italic, which has no face of its own, by its upright one's, its glyphs slanted. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
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

static void show(const amb_fonts *f, int face, const char *what) {
    double asc, desc, gap;
    amb_face_metrics(f, face, 16, &asc, &desc, &gap);
    amb_face fc;
    amb_face_record(f, face, &fc);
    const char *s = "AVAVAV To. Wa";
    printf("%s: face %d, %u code points, %u kerning pairs; at 16px ascent %g descent %g gap %g; '%s' %.4f px (unkerned %.4f)\n", what,
           face, fc.count, fc.kcount, asc, desc, gap, s, amb_text_width(f, face, 16, s, strlen(s)),
           amb_advance(f, face, 'A', 16) * 3 + amb_advance(f, face, 'V', 16) * 3 + amb_advance(f, face, ' ', 16) * 2 +
           amb_advance(f, face, 'T', 16) + amb_advance(f, face, 'o', 16) + amb_advance(f, face, '.', 16) + amb_advance(f, face, 'W', 16) +
           amb_advance(f, face, 'a', 16));
}

/* the largest difference between two masks of a glyph (each w x h, its top left at ox,
   oy), over the box of both */
static int mask_diff(const vector<uint8_t> *m1, int w1, int h1, int x1, int y1, const vector<uint8_t> *m2, int w2, int h2, int x2, int y2) {
    int lx = x1 < x2 ? x1 : x2, ly = y1 < y2 ? y1 : y2;
    int hx = x1 + w1 > x2 + w2 ? x1 + w1 : x2 + w2, hy = y1 + h1 > y2 + h2 ? y1 + h1 : y2 + h2, worst = 0;
    for (int y = ly; y < hy; y++)
        for (int x = lx; x < hx; x++) {
            int a = x >= x1 && x < x1 + w1 && y >= y1 && y < y1 + h1 ? at(*m1, (size_t)(y - y1) * w1 + (x - x1)) : 0;
            int b = x >= x2 && x < x2 + w2 && y >= y2 && y < y2 + h2 ? at(*m2, (size_t)(y - y2) * w2 + (x - x2)) : 0;
            if ((a > b ? a - b : b - a) > worst) worst = a > b ? a - b : b - a;
        }
    return worst;
}

static uint32_t be32(const vector<uint8_t> *b, size_t i) {
    return (uint32_t)at(*b, i) << 24 | (uint32_t)at(*b, i + 1) << 16 | (uint32_t)at(*b, i + 2) << 8 | at(*b, i + 3);
}

/* a font's bytes with its table tag broken (from seed: a few bytes set at random, or a run
   of them), read into fonts of their own, and each ASCII glyph drawn: *read and *glyphs
   count those that were */
static void mutate_and_draw(const vector<uint8_t> *src, const char *tag, uint32_t seed, unsigned long *read, unsigned long *glyphs) {
    vector<uint8_t> b;
    append_range(b, data(*src), len(*src));
    uint32_t nt = (uint32_t)at(b, 4) << 8 | at(b, 5), off = 0, ln = 0;
    for (uint32_t i = 0; i < nt; i++)
        if (memcmp(data(b) + 12 + 16 * (size_t)i, tag, 4) == 0) { off = be32(&b, 12 + 16 * (size_t)i + 8); ln = be32(&b, 12 + 16 * (size_t)i + 12); }
    if (!ln || (size_t)off + ln > len(b)) return;
    uint32_t x = seed * 2654435761u;
    int n = 1 + (int)(seed % 8);
    for (int k = 0; k < n; k++) {
        x = x * 1103515245u + 12345u;
        size_t p = off + (x >> 8) % ln;
        x = x * 1103515245u + 12345u;
        if (seed % 5 == 0)
            for (size_t j = 0; j < 1 + (x >> 16) % 64 && p + j < (size_t)off + ln; j++) b[p + j] = (uint8_t)(x >> (j % 24));
        else b[p] = (uint8_t)(x >> 16);
    }
    amb_fonts g = {0};
    defer(g) { AMB_FONTS_DROP(g); }
    amb_fonts_load(&g);
    int face = amb_fonts_add(&g, data(b), len(b));
    if (face < 0) return;
    (*read)++;
    vector<float> acc;
    vector<uint8_t> mask;
    for (uint32_t c = 0x21; c < 0x7F; c++) {
        int w, h, ox, oy;
        if (amb_glyph_mask(&g, face, c, 16, 0, 0, &acc, &mask, &w, &h, &ox, &oy)) (*glyphs)++;
    }
    int w, h, ox, oy;
    for (uint32_t c = 0xE000; c <= 0xE004; c++)
        if (amb_glyph_mask(&g, face, c, 16, 0, 0, &acc, &mask, &w, &h, &ox, &oy)) (*glyphs)++;
}

int main(void) {
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    show(&f, AMB_FACE_SANS, "built-in sans");
    show(&f, AMB_FACE_SERIF_BOLD, "built-in serif bold");
    show(&f, AMB_FACE_MONO, "built-in mono");
    show(&f, AMB_FACE_COURIER, "built-in courier");
    show(&f, AMB_FACE_DV_SANS, "built-in dejavu sans");
    show(&f, AMB_FACE_SERIF_ITALIC, "built-in serif italic");
    show(&f, AMB_FACE_SANS_BOLD_ITALIC, "built-in sans bold italic");
    show(&f, AMB_FACE_MONO_ITALIC, "built-in mono italic (oblique)");
    show(&f, AMB_FACE_DV_SANS_ITALIC, "built-in dejavu sans italic (slanted)");
    printf("'Hamburgefonstiv AVAVAV To. Wa' at 16px: italic serif %.4f px, italic sans %.4f px\n",
           amb_text_width(&f, AMB_FACE_SERIF_ITALIC, 16, "Hamburgefonstiv AVAVAV To. Wa", 29),
           amb_text_width(&f, AMB_FACE_SANS_ITALIC, 16, "Hamburgefonstiv AVAVAV To. Wa", 29));
    /* the families that name built-in faces, and one that names none: each style's face */
    static const char *const fams[9] = { "serif", "sans-serif", "monospace", "couriernew", "dejavusans", "dejavuserif",
                                         "dejavusansmono", "liberationsans", "georgia" };
    for (int k = 0; k < 9; k++)
        printf("%s: face %d, bold %d, italic %d, bold italic %d\n", fams[k], amb_face_named(fams[k], 0, 0), amb_face_named(fams[k], 1, 0),
               amb_face_named(fams[k], 0, 1), amb_face_named(fams[k], 1, 1));
    /* what a face has no glyph for, drawn by its fallbacks: each code point's face, its
       advance at 16px, and the kerning beside a letter of the face's own */
    static const uint32_t cps[6] = { 0x2605, 0x2011, 0x05D0, 0x25B8, 0x4E00, 0x2010 };
    static const int in[6] = { AMB_FACE_SANS, AMB_FACE_SANS, AMB_FACE_MONO, AMB_FACE_SERIF, AMB_FACE_SANS, AMB_FACE_SANS };
    for (int k = 0; k < 6; k++) {
        int by;
        uint32_t g = amb_glyph_of(&f, in[k], cps[k], &by);
        printf("U+%04X in face %d: drawn by face %d (its U+%04X), %.4f px; kerning after 'A' %.4f\n", cps[k], in[k], by,
               g, amb_advance(&f, in[k], cps[k], 16), amb_kern(&f, in[k], 'A', cps[k], 16));
    }
    /* (the escape ends before the A: "\x85A" would be one escape) */
    printf("'A\u2605A' in face %d: %.4f px\n", AMB_FACE_SANS, amb_text_width(&f, AMB_FACE_SANS, 16, "A\xe2\x98\x85" "A", 5));
    /* the blocks the built-in faces carry (tests/pages/coverage.html and symbols.html hold
       them to Chromium): words of Latin Extended-A, Vietnamese, Greek and Cyrillic in a
       few faces (DejaVu Sans Mono draws the Vietnamese letters with two marks it has no
       glyph for from their parts, as HarfBuzz does), and which face draws a symbol of each
       block in serif and in monospace */
    static const char *const words[5] = { "Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144", "Ti\u1ebfng Vi\u1ec7t: \u1ea9m th\u1ef1c, ph\u1edf b\u00f2", "\u039e\u03b5\u03c3\u03ba\u03b5\u03c0\u03ac\u03b6\u03c9 \u03c4\u03b7\u03bd \u03c8\u03c5\u03c7\u03bf\u03c6\u03b8\u03cc\u03c1\u03b1",
                                          "\u0421\u044a\u0435\u0448\u044c \u0436\u0435 \u0435\u0449\u0451 \u044d\u0442\u0438\u0445 \u043c\u044f\u0433\u043a\u0438\u0445 \u0431\u0443\u043b\u043e\u043a", "\u0490\u0430\u043d\u043e\u043a, \u0457\u0436\u0430\u043a, \u0454\u043d\u043e\u0442, \u0452\u0430\u043a" };
    static const int wface[4] = { AMB_FACE_SERIF, AMB_FACE_SANS_BOLD, AMB_FACE_MONO, AMB_FACE_COURIER_ITALIC };
    for (int k = 0; k < 5; k++)
        printf("'%s' at 16px: %.4f, %.4f, %.4f, %.4f px in faces %d, %d, %d, %d\n", words[k],
               amb_text_width(&f, wface[0], 16, words[k], strlen(words[k])), amb_text_width(&f, wface[1], 16, words[k], strlen(words[k])),
               amb_text_width(&f, wface[2], 16, words[k], strlen(words[k])), amb_text_width(&f, wface[3], 16, words[k], strlen(words[k])),
               wface[0], wface[1], wface[2], wface[3]);
    static const uint32_t syms[22] = { 0x2030, 0x2056, 0x20AC, 0x20BF, 0x2116, 0x2102, 0x212A, 0x2190, 0x21C4, 0x2211, 0x2200, 0x2249,
                                       0x2500, 0x2501, 0x2588, 0x25B2, 0x25C8, 0x263A, 0x2602, 0x2702, 0x0514, 0x1EDF };
    for (int k = 0; k < 22; k++) {
        int bs, bm;
        uint32_t gs = amb_glyph_of(&f, AMB_FACE_SERIF, syms[k], &bs), gm = amb_glyph_of(&f, AMB_FACE_MONO, syms[k], &bm);
        printf("U+%04X: in serif by face %d (U+%04X), %.4f px; in monospace by face %d (U+%04X), %.4f px\n", syms[k], bs, gs,
               amb_advance(&f, AMB_FACE_SERIF, syms[k], 16), bm, gm, amb_advance(&f, AMB_FACE_MONO, syms[k], 16));
    }
    /* the system fallback (font_data.h's fonts, in fontconfig's order, read from their
       files): what no built-in face has, drawn by the font Chromium draws it with here
       (tests/pages/scripts.html) -- CJK ideographs, kana and hangul by WenQuanYi Zen Hei,
       Arabic, Armenian and Georgian by DejaVu Sans's file, Devanagari by FreeSans, Ethiopic by
       FreeSerif, an emoji by Noto Color Emoji (by its strike's advance: 19.9633 px), a skin
       tone alone by FreeSerif; IPA by Liberation Serif's own file, Armenian in monospace by
       DejaVu Sans Mono's; a private-use code point by the face's notdef (12.4453 px in
       serif); a default-ignorable one by nothing. Each face's font by its size, and its line
       at 16px */
    static const uint32_t far[15] = { 0x4E00, 0x3042, 0xAC00, 0x0627, 0x0531, 0x10D0, 0x0915, 0x1200, 0x1F600, 0x1F3FB,
                                      0x0250, 0x0531, 0xE000, 0x200D, 0xFE0F };
    static const int farface[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, AMB_FACE_MONO, 0, 0, 0 };
    for (int k = 0; k < 15; k++) {
        int by;
        uint32_t g = amb_glyph_of(&f, farface[k], far[k], &by);
        amb_face fc;
        amb_face_record(&f, by, &fc);
        double asc, desc, gap;
        amb_face_metrics(&f, by, 16, &asc, &desc, &gap);
        printf("U+%04X in face %d: drawn by face %d (U+%04X; a font of %u bytes, %u units an em; at 16px %g + %g + %g), %.4f px\n", far[k],
               farface[k], by, g, fc.size, fc.upem, asc, desc, gap, amb_advance(&f, farface[k], far[k], 16));
    }
    printf("'a‍b' %.4f px, 'ab' %.4f px\n", amb_text_width(&f, 0, 16, "a\xe2\x80\x8d" "b", 5), amb_text_width(&f, 0, 16, "ab", 2));
    vector<uint8_t> ttf, woff;
    if (!read_file("tests/pages/fonts/oblique.ttf", &ttf) || !read_file("tests/pages/fonts/serif-bold.woff", &woff)) { printf("no fixture fonts\n"); return 1; }
    int a = amb_fonts_add(&f, data(ttf), len(ttf));
    int b = amb_fonts_add(&f, data(woff), len(woff));
    show(&f, a, "web oblique (TrueType)");
    show(&f, b, "web serif bold (WOFF)");
    /* a glyph drawn from each, its mask's size and the sum of its coverage */
    vector<float> acc;
    vector<uint8_t> mask;
    int faces[3] = { AMB_FACE_SANS, a, b };
    for (int k = 0; k < 3; k++) {
        int w, h, ox, oy;
        unsigned long sum = 0;
        if (amb_glyph_mask(&f, faces[k], 'g', 20, 0, 0, &acc, &mask, &w, &h, &ox, &oy))
            for (size_t i = 0; i < len(mask); i++) sum += at(mask, i);
        printf("'g' at 20px in face %d: %dx%d at %d,%d, coverage %lu\n", faces[k], w, h, ox, oy, sum);
    }
    /* an 'l' at 40px: upright; DejaVu Sans's italic, slanted (a quarter of its height to
       the right at its top, its coverage the same); the italic serif face, drawn as it is
       (asked for italic or not); a web font asked for italic, slanted */
    static const char *const lwhat[6] = { "dejavu sans", "dejavu sans italic", "dejavu sans, italic asked", "serif italic",
                                          "serif italic, italic asked", "web serif bold, italic asked" };
    static const int litalic[6] = { 0, 0, 1, 0, 1, 1 };
    int lface[6] = { AMB_FACE_DV_SANS, AMB_FACE_DV_SANS_ITALIC, AMB_FACE_DV_SANS, AMB_FACE_SERIF_ITALIC, AMB_FACE_SERIF_ITALIC, b };
    for (int k = 0; k < 6; k++) {
        int w, h, ox, oy;
        unsigned long sum = 0;
        if (amb_glyph_mask(&f, lface[k], 'l', 40, 0, litalic[k], &acc, &mask, &w, &h, &ox, &oy))
            for (size_t i = 0; i < len(mask); i++) sum += at(mask, i);
        printf("'l' at 40px, %s: %dx%d at %d,%d, coverage %lu\n", lwhat[k], w, h, ox, oy, sum);
    }
    /* CFF outlines (Type 2 charstrings): the machine's fonts Chromium falls back to that have
       them -- Loma for Thai, Inter, Unifont and Unifont Upper (CID-keyed) -- and the
       fixture pages' web fonts (tools/gen-font.py --web: DejaVu's glyphs as CFF, CID-keyed
       CFF, CFF2 and CFF in WOFF), whose glyphs are DejaVu's quadratic curves made cubic
       exactly: drawn as the built-in faces draw them, to a few levels of 255 at an edge
       (each curve flattened its own way). A web font with CFF outlines is unkerned (its kern
       table is one Chromium's sanitizer drops). */
    static const uint32_t cffcp[4] = { 0x0E01, 0x0376, 0x0560, 0x1F321 };
    for (int k = 0; k < 4; k++) {
        int by, w, h, ox, oy;
        uint32_t g = amb_glyph_of(&f, 0, cffcp[k], &by);
        amb_face fc;
        amb_face_record(&f, by, &fc);
        unsigned long sum = 0;
        if (amb_glyph_mask(&f, 0, cffcp[k], 32, 0, 0, &acc, &mask, &w, &h, &ox, &oy))
            for (size_t i = 0; i < len(mask); i++) sum += at(mask, i);
        printf("U+%04X in face 0: drawn by face %d (U+%04X; outlines %d, a font of %u bytes, %u units an em), %.4f px; at 32px %dx%d at %d,%d, coverage %lu\n",
               cffcp[k], by, g, fc.outlines, fc.size, fc.upem, amb_advance(&f, 0, cffcp[k], 16), w, h, ox, oy, sum);
    }
    static const char *const cffpath[4] = { "tests/pages/fonts/sans-cff.otf", "tests/pages/fonts/sans-cid.otf", "tests/pages/fonts/sans-cff2.otf",
                                            "tests/pages/fonts/serif-cff.woff" };
    static const char *const cffwhat[4] = { "web CFF", "web CID-keyed CFF", "web CFF2", "web CFF in WOFF" };
    int cface[4];
    for (int k = 0; k < 4; k++) {
        vector<uint8_t> bytes;
        if (!read_file(cffpath[k], &bytes)) { printf("no fixture font %s\n", cffpath[k]); return 1; }
        cface[k] = amb_fonts_add(&f, data(bytes), len(bytes));
        show(&f, cface[k], cffwhat[k]);
        int same = k == 3 ? AMB_FACE_DV_SERIF : AMB_FACE_DV_SANS, worst = 0, drawn = 0;
        const char *gs = "gQ&@eS%8";
        for (int j = 0; gs[j]; j++)
            for (int sz = 0; sz < 2; sz++) {
                double size = sz ? 47.5 : 20, dx = sz ? 0.25 : 0;
                vector<uint8_t> m1;
                int w1, h1, x1, y1, w2, h2, x2, y2;
                int d1 = amb_glyph_mask(&f, cface[k], (uint32_t)gs[j], size, dx, 0, &acc, &m1, &w1, &h1, &x1, &y1);
                int d2 = amb_glyph_mask(&f, same, (uint32_t)gs[j], size, dx, 0, &acc, &mask, &w2, &h2, &x2, &y2);
                if (!d1 || !d2) { printf("  '%c' at %gpx not drawn\n", gs[j], size); continue; }
                drawn++;
                int d = mask_diff(&m1, w1, h1, x1, y1, &mask, w2, h2, x2, y2);
                if (d > worst) worst = d;
            }
        printf("  %d glyphs drawn as the built-in face draws them, within %d of 255\n", drawn, worst);
    }
    /* the made glyphs (U+E000 on): two curves that hflex, hflex1, flex1 and flex draw, each
       closed by lines; frames whose numbers div works out, a bar whose height add would
       (Chromium's FreeType takes add for an unknown operator: the bar has no area), and one
       after it -- the same in CFF, CID-keyed CFF and CFF2 (which has no arithmetic glyph) */
    for (uint32_t cp = 0xE000; cp <= 0xE004; cp++) {
        printf("U+%04X at 32px:", cp);
        for (int k = 0; k < 3; k++) {
            int w, h, ox, oy, by;
            unsigned long sum = 0;
            int notdef = amb_glyph_of(&f, cface[k], cp, &by) == 0x110000;
            if (amb_glyph_mask(&f, cface[k], cp, 32, 0, 0, &acc, &mask, &w, &h, &ox, &oy))
                for (size_t i = 0; i < len(mask); i++) sum += at(mask, i);
            printf(" %s %s%dx%d at %d,%d, coverage %lu;", cffwhat[k] + 4, notdef ? "its notdef " : "", w, h, ox, oy, sum);
        }
        printf("\n");
    }
    /* fonts whose CFF or CFF2 table is broken at random (a few bytes, or a run of them):
       each read or refused, its glyphs drawn or not, and nothing read outside the font
       (valgrind) */
    static const char *const brokenwhat[3] = { "CFF", "CID-keyed CFF", "CFF2" };
    static const char *const brokentag[3] = { "CFF ", "CFF ", "CFF2" };
    static const uint32_t brokenmany[3] = { 300, 150, 150 };
    for (int k = 0; k < 3; k++) {
        vector<uint8_t> bytes;
        read_file(cffpath[k], &bytes);
        unsigned long read = 0, glyphs = 0;
        for (uint32_t seed = 1; seed <= brokenmany[k]; seed++) mutate_and_draw(&bytes, brokentag[k], seed, &read, &glyphs);
        printf("%u broken %s fonts: %lu read, %lu glyphs drawn\n", brokenmany[k], brokenwhat[k], read, glyphs);
    }
    /* what is no font: refused, and the faces read before are as they were */
    vector<uint8_t> junk;
    append_range(junk, data(ttf), 100);
    printf("a truncated font: %d; text: %d; an empty file: %d\n", amb_fonts_add(&f, data(junk), len(junk)),
           amb_fonts_add(&f, (const uint8_t *)"hello, world, no font", 21), amb_fonts_add(&f, data(junk), 0));
    printf("%u faces\n", f.nfaces);
    return 0;
}
