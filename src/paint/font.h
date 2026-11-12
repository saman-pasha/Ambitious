/* src/paint/font.h -- the fonts text is measured and drawn with: the six built-in
   DejaVu faces of font_data.h (tools/gen-font.py: TrueType subsets) and a page's web
   fonts (@font-face: TrueType, OpenType with TrueType outlines, WOFF 1), each read by
   one TrueType reader into an amb_fonts, whose owner drops its vectors
   (AMB_FONTS_DROP). Layout asks it for advances, kerning and metrics; paint for each
   glyph's coverage at a size.

   A glyph's outline is TrueType's (glyf): contours of points on and off the curve,
   quadratic, a composite glyph's components placed by their transforms.
   amb_glyph_mask draws one: its contours flattened into lines whose signed area each
   pixel accumulates (exact coverage, non-zero fill), at any size, at a fraction of a
   pixel across, upright or slanted for italic. */
#ifndef AMB_FONT_H
#define AMB_FONT_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

enum { AMB_FACE_SANS, AMB_FACE_SANS_BOLD, AMB_FACE_SERIF, AMB_FACE_SERIF_BOLD, AMB_FACE_MONO, AMB_FACE_MONO_BOLD, AMB_NFACES };
/* the built-in six, then a page's web fonts, at most so many (an enum's constant: a
   #define in an array's bound is not lowered in a struct of vectors) */
enum { AMB_MAXFACES = 24 };

/* a face's code point: its glyph, advance, and its kerning pairs as the left glyph
   (kfirst, kcount in kerns, from its face's first) */
typedef struct amb_glyph { uint32_t cp, gid; uint16_t advance, kcount; uint32_t kfirst; } amb_glyph;
typedef struct amb_kern { uint32_t pair; int32_t value; } amb_kern;     /* left gid << 16 | right gid, in order */
typedef struct amb_pt { int16_t x, y; uint8_t on, end; } amb_pt;        /* end: the last point of its contour */
typedef struct amb_face {
    uint32_t upem; int32_t asc, desc, gap;
    uint32_t first, count, fallback;          /* its code points in glyphs, in order; the replacement glyph's */
    uint32_t kfirst, kcount;                  /* its kerning pairs in kerns */
    uint32_t base, size;                      /* its font's bytes, in bytes or (built-in) in the built-in pool */
    uint32_t loca, glyf, glyf_size, nglyphs;  /* where its outlines are, from base */
    uint8_t loca_long, builtin;
    uint16_t ascii[128];                      /* each ASCII code point's index in glyphs from first; 0xffff none */
} amb_face;

/* the vectors first: `amb_fonts f = {0};' is then cicilang's to lower (an array of
   structs first, it is not: toolchain/REPORT.md, finding 10) */
typedef struct amb_fonts {
    vector<amb_glyph> glyphs;
    vector<amb_kern> kerns;
    vector<uint8_t> bytes;                    /* the web fonts' bytes, each an sfnt */
    uint32_t nfaces;
    amb_face face[AMB_MAXFACES];
} amb_fonts;

/* the owner of an amb_fonts drops its vectors: a struct's are not autodropped */
#define AMB_FONTS_DROP(f) vector_amb_glyph_drop(&(f).glyphs); vector_amb_kern_drop(&(f).kerns); vector_uint8_t_drop(&(f).bytes)

/* the six built-in faces into f, which is emptied first (read once a process) */
void amb_fonts_load(amb_fonts *f);

/* a web font's bytes (TrueType, OpenType with TrueType outlines, WOFF) read into f as a
   face: its index, or -1 when they are no font this reads */
int amb_fonts_add(amb_fonts *f, const uint8_t *p, size_t n);

/* the face for a family (0 sans-serif, 1 serif, 2 monospace) and a weight */
int amb_face_for(int family, int bold);

/* a face's ascent, descent (positive, below the baseline) and line gap at a size, in
   px, each rounded to a whole px as Chromium rounds them (its line height is their sum) */
void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap);

/* the index of the glyph of cp in f->glyphs: the face's, or its replacement glyph */
uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp);

/* the advance of cp at a size, in px; the kerning between two code points */
double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size);
double amb_kern(const amb_fonts *f, int face, uint32_t left, uint32_t right, double size);

/* the width of UTF-8 text at a size, in px: its advances and its pairs' kerning */
double amb_text_width(const amb_fonts *f, int face, double size, const char *s, size_t n);

/* the next code point of UTF-8 text from *i (a malformed byte reads as U+FFFD) */
uint32_t amb_utf8_next(const char *s, size_t n, size_t *i);

/* the coverage of the glyph of cp at a size, its pen at x = dx (0 <= dx < 1) on the
   baseline: a w x h mask, 0..255, whose top-left pixel is at (ox, oy) from the pen.
   acc and pts are the rasterizer's scratch. 0 when the glyph draws nothing (a space). */
int amb_glyph_mask(const amb_fonts *f, int face, uint32_t cp, double size, double dx, int italic,
                   vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy);

#endif
