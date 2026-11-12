/* tests/exec/fonts.c -- the TrueType reader (src/paint/font.c): the built-in faces, and
   the fixture pages' web fonts read as a page's are (a TrueType file and a WOFF), each
   measured -- metrics rounded as Chromium rounds them, advances and kerning -- and a
   glyph drawn; the families that name built-in faces; the code points a face has no
   glyph for, drawn by its fallbacks; and bytes that are no font, refused. Every width
   here is the one tools/gen-font.py's own reader gives from the Liberation and DejaVu
   files, kern table and all; the built-in faces' are Chromium's on a machine with those
   fonts within 0.01 px (tools/bench: 'AVAVAV To. Wa' at 16px is 111.4531 px in
   sans-serif, 110.4688 in bold serif, 125.2344 in monospace, 124.8281 in Courier New,
   116.9844 in DejaVu Sans; U+2011 as wide as U+2010, two Hebrew letters in monospace
   17.3125, U+25B8 in serif 8.0313), but for the stars and checks it takes from Inter
   where that is installed (U+2605 16.7188 px there). */
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
    const char *s = "AVAVAV To. Wa";
    printf("%s: face %d, %u code points, %u kerning pairs; at 16px ascent %g descent %g gap %g; '%s' %.4f px (unkerned %.4f)\n", what,
           face, f->face[face].count, f->face[face].kcount, asc, desc, gap, s, amb_text_width(f, face, 16, s, strlen(s)),
           amb_advance(f, face, 'A', 16) * 3 + amb_advance(f, face, 'V', 16) * 3 + amb_advance(f, face, ' ', 16) * 2 +
           amb_advance(f, face, 'T', 16) + amb_advance(f, face, 'o', 16) + amb_advance(f, face, '.', 16) + amb_advance(f, face, 'W', 16) +
           amb_advance(f, face, 'a', 16));
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
    /* the families that name built-in faces, and one that names none */
    static const char *const fams[9] = { "serif", "sans-serif", "monospace", "couriernew", "dejavusans", "dejavuserif",
                                         "dejavusansmono", "liberationsans", "georgia" };
    for (int k = 0; k < 9; k++) printf("%s: face %d, bold %d\n", fams[k], amb_face_named(fams[k], 0), amb_face_named(fams[k], 1));
    /* what a face has no glyph for, drawn by its fallbacks: each code point's face, its
       advance at 16px, and the kerning beside a letter of the face's own */
    static const uint32_t cps[6] = { 0x2605, 0x2011, 0x05D0, 0x25B8, 0x4E00, 0x2010 };
    static const int in[6] = { AMB_FACE_SANS, AMB_FACE_SANS, AMB_FACE_MONO, AMB_FACE_SERIF, AMB_FACE_SANS, AMB_FACE_SANS };
    for (int k = 0; k < 6; k++) {
        int by;
        uint32_t g = amb_glyph_of(&f, in[k], cps[k], &by);
        printf("U+%04X in face %d: drawn by face %d (its U+%04X), %.4f px; kerning after 'A' %.4f\n", cps[k], in[k], by,
               at(f.glyphs, g).cp, amb_advance(&f, in[k], cps[k], 16), amb_kern(&f, in[k], 'A', cps[k], 16));
    }
    printf("'A\u2605A' in face %d: %.4f px\n", AMB_FACE_SANS, amb_text_width(&f, AMB_FACE_SANS, 16, "A\xe2\x98\x85A", 5));
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
    /* what is no font: refused, and the faces read before are as they were */
    vector<uint8_t> junk;
    append_range(junk, data(ttf), 100);
    printf("a truncated font: %d; text: %d; an empty file: %d\n", amb_fonts_add(&f, data(junk), len(junk)),
           amb_fonts_add(&f, (const uint8_t *)"hello, world, no font", 21), amb_fonts_add(&f, data(junk), 0));
    printf("%u faces\n", f.nfaces);
    return 0;
}
