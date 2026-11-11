/* src/paint/font.h -- the fonts text is measured and drawn with: the six DejaVu
   faces of font_data.h (tools/gen-font.py), decoded into one amb_fonts, whose
   owner drops its two vectors (AMB_FONTS_DROP). Layout asks it for advances and
   metrics; paint for each glyph's coverage at a size.

   A glyph's outline is TrueType's: contours of points on and off the curve,
   quadratic. amb_glyph_mask draws one: its contours flattened into lines whose
   signed area each pixel accumulates (exact coverage, non-zero fill), at any
   size, at a fraction of a pixel across, upright or slanted for italic. */
#ifndef AMB_FONT_H
#define AMB_FONT_H
#include <stdint.h>
#include <vector.pl>

enum { AMB_FACE_SANS, AMB_FACE_SANS_BOLD, AMB_FACE_SERIF, AMB_FACE_SERIF_BOLD, AMB_FACE_MONO, AMB_FACE_MONO_BOLD, AMB_NFACES };

typedef struct amb_glyph { uint32_t cp; uint32_t first; uint16_t npts; uint16_t advance; } amb_glyph;
typedef struct amb_pt { int16_t x, y; uint8_t on, end; } amb_pt;      /* end: the last point of its contour */
typedef struct amb_face { uint32_t upem; int32_t asc, desc, gap; uint32_t first, count, fallback; } amb_face;

/* the vectors first: `amb_fonts f = {0};' is then cicilang's to lower (an array of
   structs first, it is not: toolchain/REPORT.md, finding 10) */
typedef struct amb_fonts {
    vector<amb_glyph> glyphs;
    vector<amb_pt> pts;
    amb_face face[AMB_NFACES];
} amb_fonts;

/* the owner of an amb_fonts drops its vectors: a struct's are not autodropped */
#define AMB_FONTS_DROP(f) vector_amb_glyph_drop(&(f).glyphs); vector_amb_pt_drop(&(f).pts)

/* the six faces decoded into f, which is zeroed first */
void amb_fonts_load(amb_fonts *f);

/* the face for a family (0 sans-serif, 1 serif, 2 monospace) and a weight */
int amb_face_for(int family, int bold);

/* a face's ascent, descent (positive, below the baseline) and line gap at a size, in px */
void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap);

/* the index of the glyph of cp in f->glyphs: the face's, or its replacement glyph */
uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp);

/* the advance of cp, and the width of UTF-8 text, at a size, in px */
double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size);
double amb_text_width(const amb_fonts *f, int face, double size, const char *s, size_t n);

/* the next code point of UTF-8 text from *i (a malformed byte reads as U+FFFD) */
uint32_t amb_utf8_next(const char *s, size_t n, size_t *i);

/* the coverage of the glyph of cp at a size, its pen at x = dx (0 <= dx < 1) on the
   baseline: a w x h mask, 0..255, whose top-left pixel is at (ox, oy) from the pen.
   acc is the rasterizer's scratch. 0 when the glyph draws nothing (a space). */
int amb_glyph_mask(const amb_fonts *f, int face, uint32_t cp, double size, double dx, int italic,
                   vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy);

#endif
