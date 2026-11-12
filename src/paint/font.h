/* src/paint/font.h -- the fonts text is measured and drawn with: the built-in faces of
   font_data.h (tools/gen-font.py: TrueType subsets of the Liberation and DejaVu fonts)
   and a page's web fonts (@font-face: TrueType, OpenType with TrueType outlines, WOFF 1),
   each read by one TrueType reader into an amb_fonts, whose owner drops its vectors
   (AMB_FONTS_DROP). Layout asks it for advances, kerning and metrics; paint for each
   glyph's coverage at a size.

   The built-in faces are the ones Chromium draws with on a Linux machine whose fonts are
   these: Liberation Serif for serif (the default), Liberation Sans for sans-serif and
   Liberation Mono for Courier New (fontconfig's metric aliases: Times New Roman and
   Times, Arial and Helvetica, Courier New and Courier), DejaVu Sans Mono for monospace,
   and each DejaVu face for its own name; each in its four styles, the italic ones its
   family's italic face (Liberation's italics, DejaVu Sans Mono's obliques), measured by
   their own advances and drawn as they are. DejaVu Sans and DejaVu Serif have no italic
   face here: their italic is the upright face slanted as Skia slants it (a skew of a
   quarter of the height, the advances the upright's), as Chromium draws it.

   A code point a built-in face has no glyph for is drawn by the face Chromium falls back
   to there (Blink's order, measured face by face, tests/pages/italic.html and
   families.html): the face of the family itself; then Liberation Serif in the style
   asked for (the user's standard font); then, for a bold or italic style, the family's
   regular face (its glyph made bold or slanted); then DejaVu Sans, which fontconfig's
   order for no family puts first of the built-in faces (Chromium takes Inter, before it,
   where that is installed: stars, checks and some arrows). U+2011 is drawn as U+2010 by
   a face that has only that (as HarfBuzz draws it), U+00A0 as a space.

   A glyph's outline is TrueType's (glyf): contours of points on and off the curve,
   quadratic, a composite glyph's components placed by their transforms.
   amb_glyph_mask draws one: its contours flattened into lines whose signed area each
   pixel accumulates (exact coverage, non-zero fill), at any size, at a fraction of a
   pixel across, upright or, for an italic style a face is not italic in, slanted. */
#ifndef AMB_FONT_H
#define AMB_FONT_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the built-in faces, a family's four styles together (its regular, bold, italic, bold
   italic: the family's first plus 1 for bold, plus 2 for italic): Liberation Serif
   (serif, the default), Liberation Sans (sans-serif), Liberation Mono (Courier New),
   DejaVu Sans, DejaVu Serif, DejaVu Sans Mono (monospace). DejaVu Sans's and DejaVu
   Serif's italic styles are their upright faces, slanted. */
enum { AMB_FACE_SERIF, AMB_FACE_SERIF_BOLD, AMB_FACE_SERIF_ITALIC, AMB_FACE_SERIF_BOLD_ITALIC,
       AMB_FACE_SANS, AMB_FACE_SANS_BOLD, AMB_FACE_SANS_ITALIC, AMB_FACE_SANS_BOLD_ITALIC,
       AMB_FACE_COURIER, AMB_FACE_COURIER_BOLD, AMB_FACE_COURIER_ITALIC, AMB_FACE_COURIER_BOLD_ITALIC,
       AMB_FACE_DV_SANS, AMB_FACE_DV_SANS_BOLD, AMB_FACE_DV_SANS_ITALIC, AMB_FACE_DV_SANS_BOLD_ITALIC,
       AMB_FACE_DV_SERIF, AMB_FACE_DV_SERIF_BOLD, AMB_FACE_DV_SERIF_ITALIC, AMB_FACE_DV_SERIF_BOLD_ITALIC,
       AMB_FACE_MONO, AMB_FACE_MONO_BOLD, AMB_FACE_MONO_ITALIC, AMB_FACE_MONO_BOLD_ITALIC,
       AMB_NFACES };
/* the built-in faces, then a page's web fonts, at most so many (an enum's constant: a
   #define in an array's bound is not lowered in a struct of vectors) */
enum { AMB_MAXFACES = 48 };

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
    uint8_t slope;                            /* its outlines are italic or oblique: no slant is made for them */
    uint8_t style;                            /* a built-in face: the style it stands for, 1 bold | 2 italic */
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

/* the built-in faces into f, which is emptied first (read once a process) */
void amb_fonts_load(amb_fonts *f);

/* a web font's bytes (TrueType, OpenType with TrueType outlines, WOFF) read into f as a
   face: its index, or -1 when they are no font this reads */
int amb_fonts_add(amb_fonts *f, const uint8_t *p, size_t n);

/* the built-in face a family the cascade computed names (src/css: serif, sans-serif,
   monospace, couriernew, dejavusans, dejavuserif, dejavusansmono), in a style (bold, and
   italic, which oblique is here): -1 for another family (a web font's) */
int amb_face_named(const char *family, int bold, int italic);

/* a face's x-height, as a share of its size: a built-in face's from its font (OS/2's
   sxHeight; DejaVu's, which has none, its x's height), a web font's taken as half */
double amb_x_height(int face);

/* a face's ascent, descent (positive, below the baseline) and line gap at a size, in
   px, each rounded to a whole px as Chromium rounds them (its line height is their sum) */
void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap);

/* the index of the glyph of cp in f->glyphs, and the face whose it is: the face's, a
   fallback face's (built-in faces fall back as Chromium's do), or the face's
   replacement glyph */
uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp, int *drawn_by);

/* the advance of cp at a size, in px (its fallback face's when the face has no glyph
   for it); the kerning between two code points, which a face's own glyphs have between
   them (none between a glyph of the face and a fallback face's) */
double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size);
double amb_kern(const amb_fonts *f, int face, uint32_t left, uint32_t right, double size);

/* the width of UTF-8 text at a size, in px: its advances and its pairs' kerning, as
   amb_advance and amb_kern have them */
double amb_text_width(const amb_fonts *f, int face, double size, const char *s, size_t n);

/* the faces other than face that draw some of UTF-8 text's code points (its fallbacks),
   a mask: bit k for built-in face k */
uint32_t amb_text_fallbacks(const amb_fonts *f, int face, const char *s, size_t n);

/* the next code point of UTF-8 text from *i (a malformed byte reads as U+FFFD) */
uint32_t amb_utf8_next(const char *s, size_t n, size_t *i);

/* the coverage of the glyph of cp at a size (its fallback face's when the face has none),
   its pen at x = dx (0 <= dx < 1) on the baseline: a w x h mask, 0..255, whose top-left pixel is at (ox, oy) from the pen.
   Italic (asked for by the text's style, or by the face's own), it is slanted as Skia
   slants a face with no italic of its own -- x moved by a quarter of the height above
   the baseline -- unless the face that draws it is italic itself. acc is the
   rasterizer's scratch. 0 when the glyph draws nothing (a space). */
int amb_glyph_mask(const amb_fonts *f, int face, uint32_t cp, double size, double dx, int italic,
                   vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy);

#endif
