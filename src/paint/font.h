/* src/paint/font.h -- the fonts text is measured and drawn with: the built-in faces of
   font_data.h (tools/gen-font.py: TrueType subsets of the Liberation and DejaVu fonts),
   the system faces (the machine's fonts Chromium falls back to, read from their files
   when a page first needs them) and a page's web fonts (@font-face: TrueType, OpenType
   with TrueType, CFF or CFF2 outlines, WOFF, WOFF2), each read by one sfnt reader into an
   amb_fonts, whose owner drops its vectors (AMB_FONTS_DROP). Layout asks it for advances,
   kerning and metrics; paint for each glyph's coverage at a size.

   The built-in faces are the ones Chromium draws with on a Linux machine whose fonts are
   these: Liberation Serif for serif (the default), Liberation Sans for sans-serif and
   Liberation Mono for Courier New (fontconfig's metric aliases: Times New Roman and
   Times, Arial and Helvetica, Courier New and Courier), DejaVu Sans Mono for monospace,
   and each DejaVu face for its own name; each in its four styles, the italic ones its
   family's italic face (Liberation's italics, DejaVu Sans Mono's obliques), measured by
   their own advances and drawn as they are. DejaVu Sans and DejaVu Serif have no italic
   face here: their italic is the upright face slanted as Skia slants it (a skew of a
   quarter of the height, the advances the upright's), as Chromium draws it.

   Each carries what its font has of these blocks: Latin-1, Latin Extended-A and B, the
   spacing accents, Greek and Coptic, Cyrillic and its supplement, Hebrew's letters,
   Latin Extended Additional (Vietnamese), general punctuation, currency, letterlike
   symbols, arrows, mathematical operators, box drawing, block elements, geometric shapes,
   miscellaneous symbols and dingbats; and a code point it has no glyph for but has the
   canonical decomposition of, as HarfBuzz draws it (its parts, each mark where the
   font's GPOS puts it: tools/gen-font.py).

   A code point a face has no glyph for is drawn by the font Chromium falls back to here
   (Blink's order, measured code point by code point in each family and style:
   tests/pages/italic.html, coverage.html, symbols.html, scripts.html, emoji.html and
   families.html): the family's own font -- a built-in face's subset, else that font's file
   (IPA, polytonic Greek); then Liberation Serif in the style asked for (the user's
   standard font); for an emoji (Emoji_Presentation, Emoji_Modifier_Base), the font
   fontconfig gives emoji (Noto Color Emoji: measured by its colour bitmaps' advances);
   for a bold or italic style, the family's regular font (its glyph made bold or
   slanted); then the system fallback's fonts in fontconfig's order for no family
   (gfx::GetFallbackFontForChar: Inter, DejaVu Sans, WenQuanYi Zen Hei for CJK, kana and
   hangul, Loma, IPAGothic, FreeSans for Devanagari, FreeSerif for Ethiopic, ...), the
   first whose cmap has it, for any code point but a private-use one; else the face's
   notdef. A system font whose file is missing, or that this cannot read, is passed over.
   U+2011 is drawn as U+2010 by a face that has only that (as HarfBuzz draws it), U+00A0
   as a space, and a default-ignorable code point as nothing.

   A glyph's outline is TrueType's (glyf): contours of points on and off the curve,
   quadratic, a composite glyph's components placed by their transforms; or CFF's or
   CFF2's (Inter, Loma, Unifont, web fonts): Type 2 charstrings, run into contours of
   cubic curves (their controls' points on 2). amb_glyph_mask draws one: its contours
   flattened into lines whose signed area each pixel accumulates (exact coverage,
   non-zero fill), at any size, at a fraction of a pixel across, upright or, for an
   italic style a face is not italic in, slanted, or turned a quarter (a vertical line's
   sideways glyph); amb_glyph_path gives its contours as
   path commands, for SVG text drawn turned or stroked (src/paint/svg.c). A face of colour
   bitmaps (CBLC and CBDT: Noto Color Emoji) is measured, not drawn. */
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
       AMB_NBUILTIN };
/* then the system faces: the fonts Chromium falls back to here (font_data.h's list,
   fontconfig's order), each read from its file when a page first needs it and a face from
   then on, numbered in the order they are read, at most AMB_NSYSTEM of them. The faces a
   fallback mask names (amb_text_fallbacks), whose line metrics layout reads, are the
   built-in ones and the first eight system faces read: AMB_NFACES. Then a page's web
   fonts, at most AMB_MAXWEB, from AMB_WEBFIRST on. (Enums' constants: a #define in an
   array's bound is not lowered in a struct of vectors.) */
enum { AMB_NSYSTEM = 24, AMB_NFACES = 32, AMB_WEBFIRST = AMB_NBUILTIN + AMB_NSYSTEM, AMB_MAXWEB = 24 };

/* a face's code point: its glyph, advance, and its kerning pairs as the left glyph
   (kfirst, kcount in kerns, from its face's first) */
typedef struct amb_glyph { uint32_t cp, gid; uint16_t advance, kcount; uint32_t kfirst; } amb_glyph;
typedef struct amb_kern { uint32_t pair; int32_t value; } amb_kern;     /* left gid << 16 | right gid, in order */
typedef struct amb_pt { int16_t x, y; uint8_t on, end; } amb_pt;        /* on: 1 on the curve, 0 a quadratic's control, 2 a cubic's; end: the last point of its contour */
/* a face's outlines: TrueType's quadratic contours (glyf), CFF's or CFF2's cubic ones,
   or colour bitmaps (CBLC and CBDT: its advances theirs, its glyphs drawn as nothing) */
enum { AMB_GLYF, AMB_CFF, AMB_CFF2, AMB_BITMAPS };
typedef struct amb_face {
    uint32_t upem; int32_t asc, desc, gap;
    int32_t avgw, xmin, xmax;                 /* OS/2's average character width (0: none), head's horizontal bounds */
    uint32_t first, count, fallback;          /* its code points in glyphs, in order; its notdef glyph's */
    uint32_t kfirst, kcount;                  /* its kerning pairs in kerns */
    uint32_t base, size;                      /* its font's bytes, in bytes */
    uint32_t loca, glyf, glyf_size, nglyphs;  /* where its outlines are, from base (CBLC and CBDT for bitmaps) */
    uint8_t loca_long, builtin;
    uint8_t outlines;                         /* AMB_GLYF, AMB_CFF, AMB_CFF2, or AMB_BITMAPS (colour bitmaps) */
    uint8_t slope;                            /* its outlines are italic or oblique: no slant is made for them */
    uint8_t style;                            /* a built-in face: the style it stands for, 1 bold | 2 italic */
    uint16_t ascii[128];                      /* each ASCII code point's index in glyphs from first; 0xffff none */
    /* its layout tables (GSUB, GPOS, GDEF), its advances (hmtx, so many) and kern table, from
       base; a colour bitmap font's strike (CBLC's record); an id of its font's own (its table
       directory's checksums), by which shaping keeps its plans */
    uint32_t gsub, gpos, gdef, hmtx, nhm, kern, kern_len, strike, id;
} amb_face;

/* a face's vertical metrics (vmtx, so many of its long records; VORG), from its font's base: an upright glyph's
   advance and origin; 0 none. Kept beside its record, not in it, so that a record is no bigger for them */
typedef struct amb_vmet { uint32_t vmtx, nvm, vorg; } amb_vmet;

/* a page's fonts: the built-in and system faces (whose tables font.c reads once a
   process, when a face is first asked for, and every amb_fonts shares), then the web
   fonts added to it, whose tables are its own: their code points (glyphs), kerning pairs
   (kerns), bytes, and each one's record (face[k - AMB_WEBFIRST] for face k). The vectors
   first: `amb_fonts f = {0};' is then cicilang's to lower (an array of structs first, it is
   not: toolchain/REPORT.md, finding 10) */
typedef struct amb_fonts {
    vector<amb_glyph> glyphs;
    vector<amb_kern> kerns;
    vector<uint8_t> bytes;                    /* the web fonts' bytes, each an sfnt */
    uint32_t nfaces;                          /* the next web font's face */
    amb_face face[AMB_MAXWEB];
    amb_vmet vmet[AMB_MAXWEB];                /* each one's vertical metrics */
} amb_fonts;

/* the owner of an amb_fonts drops its vectors: a struct's are not autodropped */
#define AMB_FONTS_DROP(f) vector_amb_glyph_drop(&(f).glyphs); vector_amb_kern_drop(&(f).kerns); vector_uint8_t_drop(&(f).bytes)

/* f made the built-in and system faces alone (its web fonts dropped); their fonts are
   read when a face is first asked for, once a process */
void amb_fonts_load(amb_fonts *f);

/* a web font's bytes (TrueType, OpenType with TrueType, CFF or CFF2 outlines, WOFF,
   WOFF2 but a collection) read into f as a face: its index, or -1 when they are no font
   this reads */
int amb_fonts_add(amb_fonts *f, const uint8_t *p, size_t n);

/* the built-in face a family the cascade computed names (src/css: serif, sans-serif,
   monospace, couriernew, dejavusans, dejavuserif, dejavusansmono), in a style (bold, and
   italic, which oblique is here): -1 for another family (a web font's) */
int amb_face_named(const char *family, int bold, int italic);

/* a face's record into *out (a built-in face's font read first): 1, or 0 for no face */
int amb_face_record(const amb_fonts *f, int face, amb_face *out);

/* a face's x-height, as a share of its size: a built-in face's from its font (OS/2's
   sxHeight; DejaVu's, which has none, its x's height), a web font's taken as half */
double amb_x_height(int face);

/* a face's ascent, descent (positive, below the baseline) and line gap at a size, in
   px, each rounded to a whole px as Chromium rounds them (its line height is their sum) */
void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap);
/* the widths a text control is sized by (Blink's text field and textarea), the face at size: its average
   character's, OS/2's, unrounded (0 when it has none), and its glyphs' horizontal extent, head's xMax - xMin,
   rounded (Skia's fXMax - fXMin, Blink's MaxCharWidth). A built-in face's are its font file's */
void amb_face_widths(const amb_fonts *f, int face, double size, double *avg, double *maxw);

/* the code point whose glyph draws cp, and the face whose glyph it is: cp in the face
   or a fallback face (faces fall back as Chromium's do), the stand-in HarfBuzz takes for
   it (U+0020 for U+00A0, U+2010 for U+2011), 0x110000 for the face's notdef, or 0 for a
   default-ignorable code point, which is drawn as nothing */
uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp, int *drawn_by);

/* the advance of cp at a size, in px (its fallback face's when the face has no glyph
   for it); the kerning between two code points, which a face's own glyphs have between
   them (none between a glyph of the face and a fallback face's) */
double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size);
double amb_kerning(const amb_fonts *f, int face, uint32_t left, uint32_t right, double size);

/* the width of UTF-8 text at a size, in px: its glyphs' advances as amb_shape gives them (left
   to right, ligatures and kerning on) */
double amb_text_width(const amb_fonts *f, int face, double size, const char *s, size_t n);

/* a glyph a run is drawn with (amb_shape): its glyph in the face that draws it, the byte of the
   text its cluster begins at, its advance and offset from the pen (y up), in that face's units
   (amb_face_scale makes them px), and its advance before positioning (what kerning added is
   the difference); AMB_SHAPED_HIDDEN for a default ignorable, which draws nothing */
typedef struct amb_shaped { uint32_t gid, cluster; int32_t ax, dx, dy, base; uint16_t face, flags; } amb_shaped;
/* a glyph set upright in a vertical line (AMB_SHAPED_UPRIGHT): its ax its advance down the line, its dx its vertical
   origin (from the top of its advance to its baseline) and its dy half its horizontal advance (its pen's x the central
   baseline less that), each in its face's units -- but for a face with no vmtx (AMB_SHAPED_VFALLBACK), whose advance
   is its height and whose origin its ascent at the size, each rounded (Blink's OpenTypeVerticalData); a mark's
   advance none (AMB_SHAPED_VZERO: HarfBuzz zeroes a mark's advance down the line too): amb_shaped_advance and
   amb_shaped_pen give them in px */
enum { AMB_SHAPED_HIDDEN = 1, AMB_SHAPED_UPRIGHT = 2, AMB_SHAPED_VFALLBACK = 4, AMB_SHAPED_VZERO = 8 };
/* how a run is shaped: right to left (its bidi level odd; the glyphs then given in visual
   order), ligatures off (liga, clig and calt: letter-spacing, as Blink has it), kerning off; in a
   vertical line, each grapheme upright as its first code point's Vertical_Orientation (UAX #50) has
   it, U, Tu and Tr (text-orientation mixed, Blink's OrientationIterator), or every one (upright):
   those runs shaped top to bottom (vert, the vertical metrics), the rest as a horizontal line's */
enum { AMB_SHAPE_RTL = 1, AMB_SHAPE_NOLIGA = 4, AMB_SHAPE_NOKERN = 8, AMB_SHAPE_VMIXED = 16, AMB_SHAPE_VUPRIGHT = 32 };
/* a shaped glyph's advance at a size, in px; its pen's offset (along the line, and across it down) */
double amb_shaped_advance(const amb_fonts *f, const amb_shaped *g, double size);
void amb_shaped_pen(const amb_fonts *f, const amb_shaped *g, double size, double *dx, double *dy);

/* UTF-8 text s[from, to) shaped in a face as Chromium shapes it (Blink's run segmenter and
   font fallback, HarfBuzz's normalizer and its OpenType shapers: src/paint/shape.c), the
   text before from and after to its context (Arabic joining across an element's edge): its
   glyphs appended to out, left to right. Each grapheme cluster is drawn by one face: the
   first of the face's fallbacks (font.h) that has every code point of it (or its canonical
   decomposition); a system font found for the first code point the face's own fallbacks
   leave undrawn (Blink's hint) draws every other one it can. The faces' runs are shaped by
   their GSUB and GPOS (or kern table) for their scripts */
void amb_shape(const amb_fonts *f, int face, const char *s, size_t n, size_t from, size_t to, int flags, vector<amb_shaped> *out);

/* a laid-out run's glyph (a layout fragment's, packed in its glyphs): its glyph, the face that
   draws it (AMB_RGLYPH_OWN: the fragment's own, a web font's number being paint's to give),
   its AMB_SHAPED_* flags, its pen's x from the fragment's start and its y from the baseline
   (down), in px */
typedef struct amb_rglyph { uint32_t gid; uint16_t face, flags; float y; uint32_t pad; double x; } amb_rglyph;
/* a vertical line's glyph drawn upright (its AMB_SHAPED_UPRIGHT): its x then its pen's place down the line (its top there
   plus its vertical origin: the glyph's baseline), its y its pen's from the line's central baseline across it */
enum { AMB_RGLYPH_OWN = 0xFFFF };

/* a face's units at a size, in px (size over its units per em); its units per em (0: no face) */
double amb_face_scale(const amb_fonts *f, int face, double size);
double amb_face_upem(const amb_fonts *f, int face);

/* whether a face's text is italic by the face (a built-in italic style), which slants the
   glyphs a face with no italic outlines draws */
int amb_face_italic(int face);

/* the faces other than face that draw some of UTF-8 text's code points (its fallbacks),
   whose line metrics layout takes for its line: a mask, bit k for face k below AMB_NFACES
   (a system face of a built-in face's font as that face) */
uint32_t amb_text_fallbacks(const amb_fonts *f, int face, const char *s, size_t n);
/* the face whose line metrics a face's glyphs bring a line: a system face of a built-in face's
   font that face, any other itself */
int amb_metrics_face(int face);

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
/* the same of glyph gid of the face that draws it (amb_shape's), slanted when slant asks and
   the face's outlines are upright */
int amb_glyph_mask_id(const amb_fonts *f, int face, uint32_t gid, double size, double dx, int slant,
                      vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy);

/* the same, the outline turned a quarter about the pen (rot 1 clockwise: a vertical line's sideways glyph, its top to
   the right; 3 anticlockwise, sideways-lr's), its pen at y = dy (0 <= dy < 1): Skia's subpixel axis is then the
   vertical one */
int amb_glyph_mask_rot(const amb_fonts *f, int face, uint32_t gid, double size, double dy, int rot,
                       vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy);

/* the outline of the glyph of cp at a size (its fallback face's when the face has none), its
   pen at the origin on the baseline, y down, slanted as amb_glyph_mask slants it: path
   commands appended to out, each its letter ('M', 'L', 'Q', 'C', 'Z') as a number, then its
   points (src/paint/svg.c's, which fills and strokes them under any matrix). 0 when it
   draws nothing */
int amb_glyph_path(const amb_fonts *f, int face, uint32_t cp, double size, int italic, vector<double> *out);
/* the same of glyph gid of the face that draws it */
int amb_glyph_path_id(const amb_fonts *f, int face, uint32_t gid, double size, int slant, vector<double> *out);

#endif
