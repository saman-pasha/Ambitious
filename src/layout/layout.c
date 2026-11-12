/* src/layout/layout.c -- the layout module (contracts/layout.pl): the box tree of
   a styled document, laid out; and the module's generated implementation.

   Boxes. The root element's box is the viewport box's child. An element whose
   display is block, list-item, table, flex, grid or flow-root makes a
   block-level box. An inline element makes no box: its text and
   its inline children are laid out in its block container's lines, its margins,
   borders and paddings as space at its two ends, and its background drawn
   behind its part of each line. An inline element that holds a block is
   flattened into its container. inline-block, img, input, button, textarea and
   select make atomic inline boxes. A container with both block-level and
   inline-level children wraps each run of the inline ones that shows anything
   in an anonymous block.

   Block layout (CSS 2.1, 10.3.3 and 8.3.1). The width fills the containing
   block, or is as declared (min-width and max-width holding it), with auto
   margins taking what is left (centring when both are auto). The height is the
   content's, or as declared (a percentage of a containing block's definite
   height; the root's is the viewport's), min-height and max-height holding it.
   Vertical margins collapse: between siblings; through a parent that has no
   border or padding on that side (and is no new formatting context: the root, a
   cell, an inline-block, a float, a positioned box, a flex or grid item or
   container, an overflow other than visible); and through an empty box.

   Floats (9.5). A float is laid out shrunk to fit (unless its width is
   declared) and placed as high as the flow has reached -- a float between
   blocks at the next one's top, a float met in a line at the line's top when it
   fits beside what the line holds, else below the line -- and no higher than an
   earlier float, to its side as far as the floats there leave room, lower where
   they leave too little. Lines are shortened beside the floats of their block
   formatting context (moved down when not even their first word fits); a block
   that makes a formatting context of its own is narrowed beside them; clear
   moves a block below them. A formatting context's root holds its floats in its
   auto height. Its float list is in its own coordinates; the offset of each
   block's content box in it is found as its margins will collapse, as far as
   can be seen before the block is laid out.

   Positioning (9.3, 9.4.3, 10.3.7, 10.6.4). A relatively positioned box is laid
   out where the flow puts it and moved by its offsets (an inline element's
   fragments are). An absolutely positioned or fixed element leaves the flow:
   the flow keeps its static position, and after it each is laid out in its
   containing block -- the padding box of its nearest positioned ancestor's box,
   or the viewport (800 x 600) for fixed or for none -- its width from its
   offsets or shrunk to fit, auto offsets at the static position, and its box is
   that block's last child. A sticky box is where the flow puts it (as at rest,
   unscrolled). z-index and overflow are kept for paint.

   Flex containers (Flexbox 1, 9): the items in order (order, then the tree's),
   their flex base sizes (flex-basis, a declared size, or the content's), min and
   max main sizes (an auto min the content's min-content), lines when they wrap,
   the flexible lengths resolved by freezing the items clamped, the items laid
   out at their main sizes, the lines' cross sizes (stretched, align-content),
   justify-content and auto margins along the main axis, align-items and
   align-self across. Grid containers (Grid 1, 11, simplified): the track lists
   read (px, %, fr, auto, min-content, max-content, minmax(), fit-content(),
   repeat() with a count or auto-fill), the items placed by their lines and spans
   and the rest automatically row by row, the columns sized from the items that
   span one (then to their growth limits, the fr tracks sharing the rest, auto
   tracks stretched), the items laid out in their columns, the rows from their
   heights; justify-self, align-self, justify-content and align-content.

   Inline layout. White space is collapsed or kept as white-space says. Lines
   break at spaces (and around atomic inlines), never inside a word, a word too
   wide for its line overflowing it. A line's height comes from its strut and its
   pieces' line heights about one baseline, each piece shifted by its
   vertical-align as Chromium shifts it (sub, super, text-top, text-bottom,
   middle, a length, a percentage of the line height; top and bottom against
   the line box); it is aligned left, right, centre or justified (start and end
   as the direction has them). A word too wide for what is left of its line is
   broken by a hyphen (hyphens: manual at its soft hyphens, which are otherwise
   not drawn; auto there, else at the points Liang's algorithm finds with TeX's
   American English patterns, tools/gen-hyph.py, two letters at least each
   side), at the last point whose part fits. Bidirectional text (UAX #9, by
   words): a word of Hebrew or Arabic letters is right to left, a word of others
   left to right, spaces and punctuation their neighbours' direction when those
   agree, else the paragraph's (direction); the line's words are placed in the
   order rule L2 gives, a right-to-left word's letters drawn reversed.

   Pseudo-elements. An element's ::before and ::after are its first and last
   children, boxes as their display has them (inline ones in the lines), their
   content's text made in tree order before layout: strings, attr(), counter()
   and counters() (counter-reset and counter-increment, each counter's scope
   its element's, its descendants' and its later siblings', CSS 2.1 12.4.1),
   and the quotes (open-quote, close-quote: the quotes property's, or English's
   for auto). ::first-letter is its block's first letter (the punctuation before
   it too) in its own style; ::first-line the first line's pieces in its style
   where they have their block's, the line broken in it; ::marker the list
   marker's style; ::placeholder an empty field's placeholder text.

   Tables, in the automatic layout. Each column's minimum and maximum widths come
   from its cells' content (colspan shared out), a declared cell width fixing
   them. The table fills its declared width, or shrinks to its columns'
   maximums, and the columns share the width as the minimums and maximums have
   it, a spanning cell's excess shared as its columns' widths are. A rowspan
   holds its columns in the rows below; what its rows lack is shared as their
   heights are. border-spacing is 2px; a cell's content sits where its
   vertical-align puts it (its row's or row group's when it has none, the middle
   when none has).

   What is not done: writing modes other than horizontal-tb. Coordinates are
   relative to the parent box's content box while the tree is laid out, and made
   page coordinates at the end. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "../paint/font.h"

amb_interface(dom);
amb_interface(css);
amb_interface(net);
amb_implementation(layout);
#include "../css/css.h"
#include "layout.h"
#include "hyph_data.h"
#include "../net/url.h"

/* the run's context, as parameters: a struct may hold no borrow. sx: each node's style
   (0 for none); sc: each style as layout reads it, made once a run */
#define LCTX layout_tree *t, const dom_document *d, const css_style *st, const net_loader *nt, const amb_fonts *f, const vector<uint32_t> *sx, \
    const vector<lay_style> *sc, const vector<lay_gen> *gx, const vector<lay_ext> *xc, vector<lay_abs> *ab, vector<lay_flt> *fl
#define LARGS t, d, st, nt, f, sx, sc, gx, xc, ab, fl
/* the context with another block formatting context's floats */
#define LARGS_FL(x) t, d, st, nt, f, sx, sc, gx, xc, ab, (x)

#define LAY_SPACING 2.0

/* ---- the computed style of an element, as layout reads it ---- */

/* a length: px, a percentage, auto, px plus a percentage (a calc()), a calc() whose
   text (the value cv's) is reckoned against the percentages' basis; or a size its content
   gives (min-content, max-content, fit-content) */
enum { LAY_PX, LAY_PCT, LAY_AUTO, LAY_CALC, LAY_EXPR, LAY_MINC, LAY_MAXC, LAY_FITC };
typedef struct lay_len { int unit; double v, pct; uint32_t cv; } lay_len;

enum { D_NONE, D_INLINE, D_BLOCK, D_LIST_ITEM, D_INLINE_BLOCK, D_TABLE, D_ROW_GROUP, D_ROW, D_CELL, D_CAPTION, D_CONTENTS };
enum { WS_NORMAL, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE };
enum { AL_LEFT, AL_RIGHT, AL_CENTER, AL_JUSTIFY };

/* text-transform */
enum { TT_NONE, TT_UPPER, TT_LOWER, TT_CAPITALIZE };

typedef struct lay_style {
    int display, bfc;
    uint32_t color, bg, bc[4];
    double fs, lh, asc, desc;
    int face, italic, align, ws, deco, list, border_box, tt, rtl;
    int lhn;                        /* line-height is normal */
    uint32_t font;                  /* a web font's resource (the loader's), 0 for a built-in face */
    double ls, wsp;                 /* letter-spacing, word-spacing: px */
    uint8_t bs[4];
    lay_len m[4], p[4], width, height, maxw, indent;
    double b[4];
} lay_style;

/* ---- pseudo-elements, as layout reads them ---- */

/* a pseudo-element of an element (its css_pseudo: ::before 1, ::after 2, ::first-line,
   ::first-letter, ::marker, ::placeholder), its style, and a ::before's or an ::after's
   content as text, in the tree's pool */
typedef struct lay_gen { uint32_t el, pseudo, style; amb_span text; } lay_gen;

/* an item of a run that is no node: a ::before or an ::after (LAY_GEN and its lay_gen's
   index), or its text, the pseudo-element's one child (LAY_GEN | LAY_GTEXT) */
#define LAY_GEN 0x80000000u
#define LAY_GTEXT 0x40000000u
#define LAY_GIDX 0x3fffffffu
/* a node's entry in sx: its style, and whether the element has a ::before, an ::after,
   or another pseudo-element of its own */
#define LAY_HAS_BEFORE 0x80000000u
#define LAY_HAS_AFTER 0x40000000u
#define LAY_HAS_OTHER 0x20000000u
#define LAY_SMASK 0x1fffffffu

/* the index in gx (sorted by element, then pseudo-element) of el's pseudo-element pk, or -1 */
static int64_t lay_gen_find(const vector<lay_gen> *gx, uint32_t el, uint32_t pk) {
    size_t lo = 0, hi = len(*gx);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        lay_gen g = at(*gx, mid);
        if (g.el < el || (g.el == el && g.pseudo < pk)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < len(*gx) && at(*gx, lo).el == el && at(*gx, lo).pseudo == pk) return (int64_t)lo;
    return -1;
}
/* the element a run's item stands for: a pseudo-element's, or the node itself */
static uint32_t lay_dom(const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).el;
    return x;
}

/* ---- what layout reads of a style besides lay_style: positioning, floats, overflow,
   vertical-align, min and max sizes, flex and grid; read in place, by the style's index ---- */

enum { POS_STATIC, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED, POS_STICKY };
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { VA_BASELINE, VA_SUB, VA_SUPER, VA_TEXT_TOP, VA_TEXT_BOTTOM, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_LEN };
enum { LX_FLOW, LX_FLEX, LX_GRID };
/* the alignment keywords: justify-content, align-items, align-self, align-content, justify-items, justify-self */
enum { AJ_NORMAL, AJ_START, AJ_END, AJ_CENTER, AJ_STRETCH, AJ_BETWEEN, AJ_AROUND, AJ_EVENLY, AJ_BASELINE, AJ_AUTO };

typedef struct lay_ext {
    uint8_t pos, flt, clr, clip, va, rtl, hyph, lx, frow, frev, fwrap, jc, ai, as, ac, ji, js, zauto, ostyle;
    int32_t z, order;
    lay_len off[4];                     /* top, right, bottom, left */
    lay_len minw, minh, maxh, hraw, valen, basis, rgap, cgap;   /* hraw: height as declared, a percentage kept */
    double grow, shrink;
    uint32_t gtc, gtr, gac, gar, gflow, gcs, gce, grs, gre;    /* the grid's values, kept as text: their cvals */
    /* what paint reads: the corners' radii (a percentage of the border box's width), the
       opacity, the cvals of the values kept as text (none: 0), the outline, the image */
    lay_len rad[4];
    double opacity, ow, ooffset;
    uint32_t ocolor, shadows, bgimg, bgpos, bgsize, bgrepeat, xform, xorigin, tshadow, bgres, bgclip, bgorigin;
    int paints;                         /* any of these other than its initial value */
} lay_ext;

/* an absolutely positioned element the flow met: its static position, in the content box of box */
typedef struct lay_abs { uint32_t el, box; double x, y; } lay_abs;
/* a float's margin box, in its block formatting context's coordinates, and its side */
typedef struct lay_flt { double x, y, w, h; int side; } lay_flt;
/* what an inline element passes to its content: its baseline's shift (vertical-align), a
   line-relative alignment (top, bottom), its relative offset */
typedef struct lay_inh { double shift, rx, ry; int va; } lay_inh;
/* how a block-level box is laid out: its margin box's left and top in its block formatting
   context, its containing block's definite height (-1 when it has none), a border-box width
   it is given (-1 for none), and whether an auto width shrinks to fit */
typedef struct lay_bk { double ox, oy, ch, fw; int shrink, item; } lay_bk;

/* the style's index of a run's item: a pseudo-element's, or the node's */
static uint32_t lay_si(const vector<uint32_t> *sx, const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).style;
    return x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0;
}
#define LAY_X(x) at(*xc, lay_si(sx, gx, (x)))

static int lay_content_sized(lay_len l) { return l.unit == LAY_MINC || l.unit == LAY_MAXC || l.unit == LAY_FITC; }
static double lay_res(const css_style *st, lay_len l, double base) {
    if (l.unit == LAY_PCT) return base * l.v / 100;
    if (l.unit == LAY_AUTO || lay_content_sized(l)) return 0;
    if (l.unit == LAY_CALC) return l.v + base * l.pct / 100;
    if (l.unit == LAY_EXPR) {
        double x;
        amb_span sp = st->cval[l.cv].text;
        if (css_calc_eval(text_ptr(st, sp), sp.len, base, &x)) return x;
        return 0;
    }
    return l.v;
}
static lay_len lay_len_at(const css_style *st, uint32_t c) {
    lay_len l;
    l.unit = LAY_PX;
    l.v = 0;
    l.pct = 0;
    l.cv = c;
    if (!c) return l;
    css_unit u = st->cval[c].unit;
    if (u == CSS_UNIT_KEYWORD) {
        uint32_t w = st->cval[c].word;
        l.unit = names_is(st, w, "min-content", 11) ? LAY_MINC : names_is(st, w, "max-content", 11) ? LAY_MAXC :
                 names_is(st, w, "fit-content", 11) ? LAY_FITC : LAY_AUTO;
    } else if (u == CSS_UNIT_AUTO) l.unit = LAY_AUTO;
    else if (u == CSS_UNIT_PERCENT) { l.unit = LAY_PCT; l.v = st->cval[c].num; }
    else if (u == CSS_UNIT_CALC) { l.unit = LAY_CALC; l.v = st->cval[c].num; l.pct = st->cval[c].pct; }
    else if (u == CSS_UNIT_EXPR) l.unit = LAY_EXPR;
    else l.v = st->cval[c].num;
    return l;
}

/* the keyword of the value c, copied into w (32 bytes; "" for no keyword) */
static void lay_word_of(const css_style *st, uint32_t c, char *w) {
    w[0] = 0;
    if (!c || st->cval[c].unit != CSS_UNIT_KEYWORD) return;
    const char *p = names_ptr(st, st->cval[c].word);
    size_t i = 0;
    while (p[i] && i < 31) { w[i] = p[i]; i++; }
    w[i] = 0;
}
static int lay_w(const char *w, const char *k) { return strcmp(w, k) == 0; }

static int lay_border_style(const char *w) {
    static const char *const names[10] = { "none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset", "outset" };
    for (int i = 0; i < 10; i++) if (lay_w(w, names[i])) return i;
    return LAY_BS_NONE;
}

/* the web font for a family (lower case, no spaces, as the cascade resolved it) at a
   weight and style: of the faces @font-face gave the family, the one of the style asked
   for if there is one, then of the nearest weight. webface: each face record's index in
   f (-1 when it was not read). 0 when the family has none; else its face, its resource */
static int lay_web_face(const css_style *st, const vector<int32_t> *webface, const char *fam, double weight, int italic, int *face,
                        uint32_t *res) {
    int best = 0;
    double bscore = 1e9;
    for (uint32_t fi = 1; fi < st->face_len && fi < len(*webface); fi++) {
        if (at(*webface, fi) < 0) continue;
        amb_span sp = st->face[fi].family;
        const char *t = text_ptr(st, sp);
        size_t k = 0, i = 0;
        int same = 1;
        for (; i < sp.len && same; i++) {
            char c = t[i];
            if (c == ' ' || c == '\t') continue;
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (fam[k] != c) same = 0;
            else k++;
        }
        if (!same || fam[k]) continue;
        double score = (st->face[fi].italic != (uint8_t)italic) * 10000.0 + (weight > st->face[fi].weight ? weight - st->face[fi].weight :
                       st->face[fi].weight - weight);
        if (score < bscore) { bscore = score; best = (int)fi; }
    }
    if (!best) return 0;
    *face = at(*webface, (uint32_t)best);
    *res = st->face[best].font.index;
    return 1;
}

/* a style as layout reads it, from its computed values: a run from c0 in enum(prop)'s
   order (src/css/css.c) */
static void lay_style_make(const css_style *st, const amb_fonts *f, const vector<int32_t> *webface, uint32_t c0, lay_style *s) {
    memset(s, 0, sizeof *s);
    s->color = 0x000000ff;
    s->fs = 16;
    s->face = AMB_FACE_SERIF;
    s->width.unit = LAY_AUTO;
    s->height.unit = LAY_AUTO;
    s->maxw.unit = LAY_AUTO;
    if (!c0) { s->display = D_NONE; return; }
    uint32_t cv[CSS_NPROPS];
    for (uint32_t k = 0; k < CSS_NPROPS; k++) cv[k] = c0 + k;
    char w[32];
    uint32_t c = cv[CSS_PROP_DISPLAY];
    lay_word_of(st, c, w);
    if (lay_w(w, "none") || lay_w(w, "table-column") || lay_w(w, "table-column-group")) s->display = D_NONE;
    else if (lay_w(w, "inline")) s->display = D_INLINE;
    else if (lay_w(w, "list-item")) s->display = D_LIST_ITEM;
    else if (lay_w(w, "inline-block") || lay_w(w, "inline-flex") || lay_w(w, "inline-grid") || lay_w(w, "inline-table"))
        s->display = D_INLINE_BLOCK;
    else if (lay_w(w, "table")) s->display = D_TABLE;
    else if (lay_w(w, "table-row-group") || lay_w(w, "table-header-group") || lay_w(w, "table-footer-group"))
        s->display = D_ROW_GROUP;
    else if (lay_w(w, "table-row")) s->display = D_ROW;
    else if (lay_w(w, "table-cell")) s->display = D_CELL;
    else if (lay_w(w, "table-caption")) s->display = D_CAPTION;
    else if (lay_w(w, "contents")) s->display = D_CONTENTS;
    else {
        s->display = D_BLOCK;
        s->bfc = lay_w(w, "flow-root") || lay_w(w, "flex") || lay_w(w, "grid");
    }
    s->color = st->cval[cv[CSS_PROP_COLOR]].color;
    s->bg = st->cval[cv[CSS_PROP_BACKGROUND_COLOR]].color;
    s->fs = st->cval[cv[CSS_PROP_FONT_SIZE]].num;
    double weight = st->cval[cv[CSS_PROP_FONT_WEIGHT]].num;
    int bold = weight >= 600;
    lay_word_of(st, cv[CSS_PROP_FONT_STYLE], w);
    s->italic = lay_w(w, "italic") || lay_w(w, "oblique");
    /* the family the cascade resolved: a web font the page declared under it (over a
       built-in face of the name), else the built-in face it names, else the default */
    lay_word_of(st, cv[CSS_PROP_FONT_FAMILY], w);
    int generic = lay_w(w, "serif") || lay_w(w, "sans-serif") || lay_w(w, "monospace");
    int builtin = amb_face_named(w, bold), wf;
    if (!generic && lay_web_face(st, webface, w, weight, s->italic, &wf, &s->font)) {
        s->face = wf;
        /* an italic face is slanted already: no slant made for it */
        for (uint32_t fi = 1; fi < st->face_len && fi < len(*webface); fi++)
            if (at(*webface, fi) == wf && st->face[fi].italic) s->italic = 0;
    } else s->face = builtin >= 0 ? builtin : bold ? AMB_FACE_SERIF_BOLD : AMB_FACE_SERIF;
    double gap;
    amb_face_metrics(f, s->face, s->fs, &s->asc, &s->desc, &gap);
    c = cv[CSS_PROP_LINE_HEIGHT];
    if (st->cval[c].unit == CSS_UNIT_NUMBER) s->lh = st->cval[c].num * s->fs;
    else if (st->cval[c].unit == CSS_UNIT_PX) s->lh = st->cval[c].num;
    else { s->lh = s->asc + s->desc + gap; s->lhn = 1; }
    lay_word_of(st, cv[CSS_PROP_DIRECTION], w);
    s->rtl = lay_w(w, "rtl");
    /* text-align: start and end as the direction has them */
    lay_word_of(st, cv[CSS_PROP_TEXT_ALIGN], w);
    s->align = lay_w(w, "right") ? AL_RIGHT : lay_w(w, "left") ? AL_LEFT : lay_w(w, "center") ? AL_CENTER :
               lay_w(w, "justify") ? AL_JUSTIFY : lay_w(w, "end") ? (s->rtl ? AL_LEFT : AL_RIGHT) : (s->rtl ? AL_RIGHT : AL_LEFT);
    lay_word_of(st, cv[CSS_PROP_WHITE_SPACE], w);
    s->ws = lay_w(w, "pre") ? WS_PRE : lay_w(w, "nowrap") ? WS_NOWRAP : lay_w(w, "pre-wrap") || lay_w(w, "break-spaces") ? WS_PRE_WRAP :
            lay_w(w, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
    lay_word_of(st, cv[CSS_PROP_TEXT_TRANSFORM], w);
    s->tt = lay_w(w, "uppercase") ? TT_UPPER : lay_w(w, "lowercase") ? TT_LOWER : lay_w(w, "capitalize") ? TT_CAPITALIZE : TT_NONE;
    c = cv[CSS_PROP_LETTER_SPACING];
    s->ls = st->cval[c].unit == CSS_UNIT_PX ? st->cval[c].num : 0;
    c = cv[CSS_PROP_WORD_SPACING];
    s->wsp = st->cval[c].unit == CSS_UNIT_PX ? st->cval[c].num : 0;
    s->indent = lay_len_at(st, cv[CSS_PROP_TEXT_INDENT]);
    lay_word_of(st, cv[CSS_PROP_TEXT_DECORATION], w);
    s->deco = lay_w(w, "underline") ? 1 : lay_w(w, "overline") ? 2 : lay_w(w, "line-through") ? 4 : 0;
    lay_word_of(st, cv[CSS_PROP_LIST_STYLE_TYPE], w);
    s->list = lay_w(w, "none") ? 0 : lay_w(w, "circle") ? 2 : lay_w(w, "square") ? 3 :
              lay_w(w, "decimal") || lay_w(w, "decimal-leading-zero") ? 4 :
              lay_w(w, "lower-alpha") || lay_w(w, "lower-latin") || lay_w(w, "lower-greek") ? 5 :
              lay_w(w, "upper-alpha") || lay_w(w, "upper-latin") ? 6 : lay_w(w, "lower-roman") ? 7 :
              lay_w(w, "upper-roman") ? 8 : 1;
    for (int k = 0; k < 4; k++) {
        s->m[k] = lay_len_at(st, cv[CSS_PROP_MARGIN_TOP + k]);
        s->p[k] = lay_len_at(st, cv[CSS_PROP_PADDING_TOP + k]);
        s->b[k] = st->cval[cv[CSS_PROP_BORDER_TOP_WIDTH + k]].num;
        s->bc[k] = st->cval[cv[CSS_PROP_BORDER_TOP_COLOR + k]].color;
        lay_word_of(st, cv[CSS_PROP_BORDER_TOP_STYLE + k], w);
        s->bs[k] = (uint8_t)lay_border_style(w);
    }
    s->width = lay_len_at(st, cv[CSS_PROP_WIDTH]);
    s->height = lay_len_at(st, cv[CSS_PROP_HEIGHT]);
    lay_word_of(st, cv[CSS_PROP_BOX_SIZING], w);
    s->border_box = lay_w(w, "border-box");
    c = cv[CSS_PROP_MAX_WIDTH];
    if (st->cval[c].unit != CSS_UNIT_KEYWORD) s->maxw = lay_len_at(st, c);
    if (s->border_box) {
        /* a declared height is its border box's: the content's is what is left */
        double vedges = s->b[0] + s->b[2] + (s->p[0].unit == LAY_PX ? s->p[0].v : 0) + (s->p[2].unit == LAY_PX ? s->p[2].v : 0);
        if (s->height.unit == LAY_PX) { s->height.v -= vedges; if (s->height.v < 0) s->height.v = 0; }
    }
    if (s->height.unit != LAY_PX && s->height.unit != LAY_AUTO) s->height.unit = LAY_AUTO;   /* against no definite height */
}

static int lay_align_of(const char *w) {
    if (lay_w(w, "flex-start") || lay_w(w, "start") || lay_w(w, "self-start") || lay_w(w, "left")) return AJ_START;
    if (lay_w(w, "flex-end") || lay_w(w, "end") || lay_w(w, "self-end") || lay_w(w, "right")) return AJ_END;
    if (lay_w(w, "center")) return AJ_CENTER;
    if (lay_w(w, "stretch")) return AJ_STRETCH;
    if (lay_w(w, "space-between")) return AJ_BETWEEN;
    if (lay_w(w, "space-around")) return AJ_AROUND;
    if (lay_w(w, "space-evenly")) return AJ_EVENLY;
    if (lay_w(w, "baseline")) return AJ_BASELINE;
    if (lay_w(w, "auto")) return AJ_AUTO;
    return AJ_NORMAL;
}

/* what layout reads of a style besides lay_style, from its computed values (a run from c0) */
static void lay_ext_make(const css_style *st, uint32_t c0, lay_ext *e) {
    memset(e, 0, sizeof *e);
    e->zauto = 1;
    e->shrink = 1;
    e->basis.unit = LAY_AUTO;
    e->minw.unit = LAY_AUTO;
    e->minh.unit = LAY_AUTO;
    e->maxh.unit = LAY_AUTO;
    e->hraw.unit = LAY_AUTO;
    for (int k = 0; k < 4; k++) e->off[k].unit = LAY_AUTO;
    if (!c0) return;
    char w[32];
    lay_word_of(st, c0 + CSS_PROP_POSITION, w);
    e->pos = lay_w(w, "relative") ? POS_RELATIVE : lay_w(w, "absolute") ? POS_ABSOLUTE : lay_w(w, "fixed") ? POS_FIXED :
             lay_w(w, "sticky") ? POS_STICKY : POS_STATIC;
    for (int k = 0; k < 4; k++) e->off[k] = lay_len_at(st, c0 + CSS_PROP_TOP + k);
    lay_word_of(st, c0 + CSS_PROP_FLOAT, w);
    e->flt = lay_w(w, "left") || lay_w(w, "inline-start") ? FL_LEFT : lay_w(w, "right") || lay_w(w, "inline-end") ? FL_RIGHT : FL_NONE;
    lay_word_of(st, c0 + CSS_PROP_CLEAR, w);
    e->clr = lay_w(w, "left") || lay_w(w, "inline-start") ? 1 : lay_w(w, "right") || lay_w(w, "inline-end") ? 2 : lay_w(w, "both") ? 3 : 0;
    uint32_t c = c0 + CSS_PROP_Z_INDEX;
    if (st->cval[c].unit == CSS_UNIT_NUMBER) { e->z = (int32_t)st->cval[c].num; e->zauto = 0; }
    for (int k = 0; k < 2; k++) {
        lay_word_of(st, c0 + CSS_PROP_OVERFLOW_X + k, w);
        if (lay_w(w, "hidden") || lay_w(w, "scroll") || lay_w(w, "auto")) e->clip = 1;
        else if (lay_w(w, "clip") && !e->clip) e->clip = 2;
    }
    c = c0 + CSS_PROP_VERTICAL_ALIGN;
    lay_word_of(st, c, w);
    if (st->cval[c].unit == CSS_UNIT_KEYWORD)
        e->va = lay_w(w, "sub") ? VA_SUB : lay_w(w, "super") ? VA_SUPER : lay_w(w, "text-top") ? VA_TEXT_TOP :
                lay_w(w, "text-bottom") ? VA_TEXT_BOTTOM : lay_w(w, "middle") ? VA_MIDDLE : lay_w(w, "top") ? VA_TOP :
                lay_w(w, "bottom") ? VA_BOTTOM : VA_BASELINE;
    else { e->va = VA_LEN; e->valen = lay_len_at(st, c); }
    lay_word_of(st, c0 + CSS_PROP_DIRECTION, w);
    e->rtl = lay_w(w, "rtl");
    lay_word_of(st, c0 + CSS_PROP_HYPHENS, w);
    e->hyph = lay_w(w, "auto") ? 2 : lay_w(w, "manual") ? 1 : 0;
    lay_word_of(st, c0 + CSS_PROP_DISPLAY, w);
    e->lx = lay_w(w, "flex") || lay_w(w, "inline-flex") ? LX_FLEX : lay_w(w, "grid") || lay_w(w, "inline-grid") ? LX_GRID : LX_FLOW;
    lay_word_of(st, c0 + CSS_PROP_FLEX_DIRECTION, w);
    e->frow = !(lay_w(w, "column") || lay_w(w, "column-reverse"));
    e->frev = lay_w(w, "row-reverse") || lay_w(w, "column-reverse");
    lay_word_of(st, c0 + CSS_PROP_FLEX_WRAP, w);
    e->fwrap = lay_w(w, "wrap") ? 1 : lay_w(w, "wrap-reverse") ? 2 : 0;
    e->grow = st->cval[c0 + CSS_PROP_FLEX_GROW].num;
    e->shrink = st->cval[c0 + CSS_PROP_FLEX_SHRINK].num;
    c = c0 + CSS_PROP_FLEX_BASIS;
    lay_word_of(st, c, w);
    e->basis = lay_len_at(st, c);
    if (lay_w(w, "content")) e->basis.unit = LAY_MAXC;
    e->order = (int32_t)st->cval[c0 + CSS_PROP_ORDER].num;
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_CONTENT, w);
    e->jc = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_ITEMS, w);
    e->ai = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_SELF, w);
    e->as = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_CONTENT, w);
    e->ac = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_ITEMS, w);
    e->ji = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_SELF, w);
    e->js = (uint8_t)lay_align_of(w);
    for (int k = 0; k < 2; k++) {
        c = c0 + (k == 0 ? CSS_PROP_ROW_GAP : CSS_PROP_COLUMN_GAP);
        lay_len g = lay_len_at(st, c);
        if (st->cval[c].unit == CSS_UNIT_KEYWORD) { g.unit = LAY_PX; g.v = 0; }
        if (k == 0) e->rgap = g; else e->cgap = g;
    }
    e->minw = lay_len_at(st, c0 + CSS_PROP_MIN_WIDTH);
    e->minh = lay_len_at(st, c0 + CSS_PROP_MIN_HEIGHT);
    c = c0 + CSS_PROP_MAX_HEIGHT;
    if (st->cval[c].unit != CSS_UNIT_KEYWORD) e->maxh = lay_len_at(st, c);
    e->hraw = lay_len_at(st, c0 + CSS_PROP_HEIGHT);
    e->gtc = c0 + CSS_PROP_GRID_TEMPLATE_COLUMNS;
    e->gtr = c0 + CSS_PROP_GRID_TEMPLATE_ROWS;
    e->gac = c0 + CSS_PROP_GRID_AUTO_COLUMNS;
    e->gar = c0 + CSS_PROP_GRID_AUTO_ROWS;
    e->gflow = c0 + CSS_PROP_GRID_AUTO_FLOW;
    e->gcs = c0 + CSS_PROP_GRID_COLUMN_START;
    e->gce = c0 + CSS_PROP_GRID_COLUMN_END;
    e->grs = c0 + CSS_PROP_GRID_ROW_START;
    e->gre = c0 + CSS_PROP_GRID_ROW_END;
    /* paint's: values that are text are kept when they are not their initial ones */
    for (int k = 0; k < 4; k++) {
        e->rad[k] = lay_len_at(st, c0 + CSS_PROP_BORDER_TOP_LEFT_RADIUS + k);
        if (e->rad[k].unit != LAY_PX || e->rad[k].v != 0 || st->cval[e->rad[k].cv].unit == CSS_UNIT_TEXT) e->paints = 1;
    }
    e->opacity = st->cval[c0 + CSS_PROP_OPACITY].num;
    if (st->cval[c0 + CSS_PROP_OPACITY].unit != CSS_UNIT_NUMBER) e->opacity = 1;
    if (e->opacity < 1) e->paints = 1;
    lay_word_of(st, c0 + CSS_PROP_OUTLINE_STYLE, w);
    e->ostyle = (uint8_t)(lay_w(w, "auto") ? LAY_BS_SOLID : lay_border_style(w));
    e->ow = e->ostyle > LAY_BS_HIDDEN ? st->cval[c0 + CSS_PROP_OUTLINE_WIDTH].num : 0;
    e->ocolor = st->cval[c0 + CSS_PROP_OUTLINE_COLOR].color;
    e->ooffset = st->cval[c0 + CSS_PROP_OUTLINE_OFFSET].num;
    if (e->ow > 0) e->paints = 1;
    static const int text_props[10] = { CSS_PROP_BOX_SHADOW, CSS_PROP_BACKGROUND_IMAGE, CSS_PROP_BACKGROUND_POSITION, CSS_PROP_BACKGROUND_SIZE,
                                        CSS_PROP_BACKGROUND_REPEAT, CSS_PROP_TRANSFORM, CSS_PROP_TRANSFORM_ORIGIN, CSS_PROP_TEXT_SHADOW,
                                        CSS_PROP_BACKGROUND_CLIP, CSS_PROP_BACKGROUND_ORIGIN };
    uint32_t got[10];
    for (int k = 0; k < 10; k++) {
        uint32_t c = c0 + (uint32_t)text_props[k];
        lay_word_of(st, c, w);
        int initial = st->cval[c].how == CSS_HOW_INITIAL || lay_w(w, "none");
        got[k] = initial ? 0 : c;
    }
    e->shadows = got[0];
    e->bgimg = got[1];
    e->bgpos = got[2];
    e->bgsize = got[3];
    e->bgrepeat = got[4];
    e->xform = got[5];
    e->xorigin = got[6];
    e->tshadow = got[7];
    e->bgclip = got[8];
    e->bgorigin = got[9];
    if (e->shadows || e->bgimg || e->xform || e->tshadow || e->bgclip || e->bgorigin) e->paints = 1;
}

static void lay_style_of(LCTX, uint32_t x, lay_style *s) {
    if (x & LAY_GEN) { *s = at(*sc, at(*gx, x & LAY_GIDX).style); return; }
    uint32_t si = x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0;
    *s = at(*sc, si);
}
/* the style of el's pseudo-element pk (::first-line, ::first-letter, ::marker,
   ::placeholder) into s: 1, or 0 when it has none */
static int lay_pseudo_style(LCTX, uint32_t el, uint32_t pk, lay_style *s) {
    if ((el & LAY_GEN) || el >= len(*sx) || !(at(*sx, el) & LAY_HAS_OTHER)) return 0;
    int64_t g = lay_gen_find(gx, el, pk);
    if (g < 0) return 0;
    *s = at(*sc, at(*gx, (size_t)g).style);
    return 1;
}

/* ---- the DOM, as layout walks it ---- */

/* the HTML element of that name (an SVG or MathML element of the name is none) */
static int lay_is_named(const dom_document *d, uint32_t x, const char *name) {
    return !(x & LAY_GEN) && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, name, strlen(name));
}
/* an SVG document fragment: its svg element, in the SVG namespace */
static int lay_is_svg(const dom_document *d, uint32_t x) {
    return !(x & LAY_GEN) && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, "svg", 3);
}
/* an element drawn by its own content rules: an image, a form control, an embedding */
static int lay_replaced(const dom_document *d, uint32_t x) {
    return lay_is_named(d, x, "img") || lay_is_named(d, x, "input") || lay_is_named(d, x, "textarea") ||
           lay_is_named(d, x, "select") || lay_is_named(d, x, "iframe") || lay_is_named(d, x, "video") ||
           lay_is_named(d, x, "canvas") || lay_is_named(d, x, "embed") || lay_is_named(d, x, "object") ||
           lay_is_svg(d, x);
}
static uint32_t lay_attr(const dom_document *d, uint32_t x, const char *name) {
    if (x & LAY_GEN) return 0;
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
static long lay_attr_int(const dom_document *d, uint32_t x, const char *name, long dflt) {
    uint32_t a = lay_attr(d, x, name);
    if (!a) return dflt;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    long n = 0;
    int any = 0;
    for (size_t i = 0; i < v.len && p[i] >= '0' && p[i] <= '9' && n < 100000; i++) { n = n * 10 + (p[i] - '0'); any = 1; }
    return any ? n : dflt;
}

static int lay_display_of(LCTX, uint32_t x) {
    if (x & LAY_GTEXT) return D_INLINE;
    if (x & LAY_GEN) {
        int gd = at(*sc, at(*gx, x & LAY_GIDX).style).display;
        return gd == D_CONTENTS ? D_INLINE : gd;
    }
    if (d->node[x].kind == DOM_NODE_KIND_TEXT) return D_INLINE;
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return D_NONE;
    int dsp = at(*sc, x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0).display;
    if (dsp == D_INLINE && lay_replaced(d, x)) return D_INLINE_BLOCK;
    if (lay_is_named(d, x, "input")) {
        uint32_t a = lay_attr(d, x, "type");
        if (a && d->attr[a].value.len == 6 && memcmp(text_ptr(d, d->attr[a].value), "hidden", 6) == 0) return D_NONE;
    }
    return dsp;
}
static int lay_block_level(int dsp) {
    return dsp == D_BLOCK || dsp == D_LIST_ITEM || dsp == D_TABLE || dsp == D_ROW_GROUP || dsp == D_ROW || dsp == D_CELL || dsp == D_CAPTION;
}
/* an inline element with a block-level box inside it */
static int lay_holds_block(LCTX, uint32_t x) {
    if (x & LAY_GEN) return 0;                  /* a pseudo-element holds its text alone */
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_NONE) continue;
        if (lay_block_level(dsp)) return 1;
        if ((dsp == D_INLINE || dsp == D_CONTENTS) && d->node[c].kind == DOM_NODE_KIND_ELEMENT && lay_holds_block(LARGS, c)) return 1;
    }
    return 0;
}

/* a container's children as segments: a run of inline-level nodes, a block-level element,
   an absolutely positioned one (out of the flow), a float */
enum { SEG_RUN, SEG_BLOCK, SEG_ABS, SEG_FLOAT };
typedef struct lay_seg { uint32_t node; uint32_t first, n; uint8_t block; } lay_seg;

static void lay_item(vector<lay_seg> *segs, vector<uint32_t> *items, uint32_t node) {
    if (len(*segs) == 0 || back(*segs).block) {
        lay_seg s;
        s.node = 0;
        s.first = (uint32_t)len(*items);
        s.n = 0;
        s.block = 0;
        push_back(segs, s);
    }
    push_back(items, node);
    (*segs)[len(*segs) - 1].n++;
}
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items);

/* an element child (or a pseudo-element) of a container: a block-level box, its children
   in its place (display: contents, or an inline element that holds a block), or an item */
static void lay_collect_el(LCTX, uint32_t c, vector<lay_seg> *segs, vector<uint32_t> *items) {
    int dsp = lay_display_of(LARGS, c);
    if (dsp == D_NONE) return;
    uint32_t si = lay_si(sx, gx, c);
    int pos = at(*xc, si).pos, flt = at(*xc, si).flt;
    if (pos == POS_ABSOLUTE || pos == POS_FIXED || (flt && pos != POS_ABSOLUTE && pos != POS_FIXED)) {
        /* out of the flow, or floated: in a run of inline content, a piece of it (its static
           position, or where the float is placed from); else a segment of its own */
        if (len(*segs) && back(*segs).block == SEG_RUN) { lay_item(segs, items, c); return; }
        lay_seg s;
        s.node = c;
        s.first = 0;
        s.n = 0;
        s.block = (pos == POS_ABSOLUTE || pos == POS_FIXED) ? SEG_ABS : SEG_FLOAT;
        push_back(segs, s);
        return;
    }
    if (dsp == D_CONTENTS || (dsp == D_INLINE && lay_holds_block(LARGS, c))) { lay_collect(LARGS, c, segs, items); return; }
    if (lay_block_level(dsp)) {
        lay_seg s;
        s.node = c;
        s.first = 0;
        s.n = 0;
        s.block = SEG_BLOCK;
        push_back(segs, s);
        return;
    }
    lay_item(segs, items, c);
}/* a container's children: its ::before first, its ::after last; a pseudo-element's, its text */
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items) {
    if (parent & LAY_GEN) { lay_item(segs, items, parent | LAY_GTEXT); return; }
    uint32_t fl0 = parent < len(*sx) ? at(*sx, parent) : 0;
    if (fl0 & LAY_HAS_BEFORE) {
        int64_t g = lay_gen_find(gx, parent, 1);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
    for (uint32_t c = d->node[parent].first_child; c; c = d->node[c].next_sibling) {
        int kind = d->node[c].kind;
        if (kind == DOM_NODE_KIND_TEXT) { lay_item(segs, items, c); continue; }
        if (kind != DOM_NODE_KIND_ELEMENT) continue;
        lay_collect_el(LARGS, c, segs, items);
    }
    if (fl0 & LAY_HAS_AFTER) {
        int64_t g = lay_gen_find(gx, parent, 2);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
}

/* ---- pieces: an inline run as words, spaces, breaks, atomic boxes and inline edges ---- */

enum { P_WORD, P_SPACE, P_BREAK, P_ATOMIC, P_OPEN, P_CLOSE, P_ABS, P_FLOAT };
typedef struct lay_piece {
    uint8_t kind, face, italic, deco, brk, keep;   /* brk: a space a line may break at; keep: a kept (pre) space */
    uint8_t tt;                                    /* text-transform */
    uint8_t va;                                    /* VA_TOP, VA_BOTTOM: aligned with the line box; an atomic's own */
    uint8_t hy;                                    /* a word broken by a hyphen: the hyphen after it */
    uint8_t rtl;                                   /* a word of right-to-left letters (Hebrew, Arabic) */
    uint8_t soft;                                  /* a word with soft hyphens in it */
    uint8_t fb;                                    /* a word some of whose code points fallback faces draw */
    uint32_t node, elem;                           /* the text node, or the element; and the text's element */
    uint32_t font;                                 /* a web font's resource, 0 for a built-in face */
    double ls, wsp;                                /* letter-spacing, word-spacing */
    uint32_t off, len;                             /* a word's (or a kept space's) bytes of its text node */
    double w, size, lh, asc, desc;
    uint32_t color, bg;
    float aw, ah;                                  /* an atomic box's content width (and a replaced one's height) */
    float fa, fd;                                  /* those faces' lines above and below its baseline, under line-height
                                                      normal (fb) */
    float shift, rx, ry;                           /* its baseline's shift (vertical-align), its relative offset */
} lay_piece;

/* the leading a line height leaves about a font's ascent and descent, split as Chromium
   splits it (CalculateLeadingSpace): above, half of it floored to a whole px; below, the
   rest */
static void lay_leading(double lh, double asc, double desc, double *above, double *below) {
    double lead = lh - (asc + desc), up = floor(lead / 2);
    *above = asc + up;
    *below = desc + (lead - up);
}

static void lay_piece_style(lay_piece *p, const lay_style *s) {
    p->face = (uint8_t)s->face;
    p->font = s->font;
    p->ls = s->ls;
    p->wsp = s->wsp;
    p->tt = (uint8_t)s->tt;
    p->italic = (uint8_t)s->italic;
    p->size = s->fs;
    p->lh = s->lh;
    p->asc = s->asc;
    p->desc = s->desc;
    p->color = s->color;
}

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);

/* the content box of an atomic inline: a replaced element's size, or an inline-block's
   width shrunk to fit within avail */
static void lay_atomic_size(LCTX, uint32_t el, const lay_style *s, double cw, double *w, double *h) {
    *h = -1;
    if (lay_replaced(d, el)) {
        double dw = 0, dh = 0;
        if (lay_is_named(d, el, "img") && nt) {
            /* its size, as the loader read it from the image's header; one dimension
               declared, the other kept in proportion */
            for (uint32_t r = 1; r < nt->resource_len; r++)
                if (nt->resource[r].live && nt->resource[r].elem.index == el && nt->resource[r].kind == NET_KIND_IMAGE) {
                    dw = nt->resource[r].width;
                    dh = nt->resource[r].height;
                }
            if (dw > 0 && dh > 0) {
                if (s->width.unit != LAY_AUTO && s->height.unit == LAY_AUTO) { *w = lay_res(st, s->width, cw); *h = *w * dh / dw; return; }
                if (s->width.unit == LAY_AUTO && s->height.unit != LAY_AUTO) { *h = s->height.v; *w = *h * dw / dh; return; }
            }
        }
        if (lay_is_named(d, el, "input")) {
            uint32_t a = lay_attr(d, el, "type");
            amb_span v = a ? d->attr[a].value : d->attr[0].value;
            const char *tp = a ? text_ptr(d, v) : "";
            size_t tn = a ? v.len : 0;
            if ((tn == 8 && memcmp(tp, "checkbox", 8) == 0) || (tn == 5 && memcmp(tp, "radio", 5) == 0)) { dw = 13; dh = 13; }
            else { dw = (double)lay_attr_int(d, el, "size", 20) * s->fs * 0.5; dh = s->lh; }
        } else if (lay_is_named(d, el, "textarea")) {
            dw = (double)lay_attr_int(d, el, "cols", 20) * s->fs * 0.55;
            dh = (double)lay_attr_int(d, el, "rows", 2) * s->lh;
        } else if (lay_is_named(d, el, "select")) { dw = 8 * s->fs; dh = s->lh; }
        else if (lay_is_named(d, el, "iframe") || lay_is_named(d, el, "video") || lay_is_named(d, el, "embed") || lay_is_named(d, el, "object")) { dw = 300; dh = 150; }
        else if (lay_is_named(d, el, "canvas") || lay_is_svg(d, el)) { dw = 300; dh = 150; }
        *w = s->width.unit == LAY_AUTO ? dw : lay_res(st, s->width, cw);
        *h = s->height.unit == LAY_AUTO ? dh : s->height.v;
        return;
    }
    double box_edges = s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw);
    if (s->width.unit != LAY_AUTO && !lay_content_sized(s->width)) {
        *w = lay_res(st, s->width, cw) - (s->border_box ? box_edges : 0);
        if (*w < 0) *w = 0;
        return;
    }
    double mn, mx;
    lay_intrinsic(LARGS, el, cw, &mn, &mx);
    double edges = lay_res(st, s->m[1], cw) + lay_res(st, s->m[3], cw) + box_edges;
    mn -= edges;
    mx -= edges;
    double avail = cw - edges;
    double v = avail > mn ? avail : mn;
    *w = s->width.unit == LAY_MINC ? mn : s->width.unit == LAY_MAXC ? mx : v < mx ? v : mx;
    if (*w < 0) *w = 0;
}

/* the text-transform tt of s[0, n) into out (a word's: capitalize makes its first letter
   upper case): ASCII's and Latin-1's letters, each the length it was */
static void lay_transform(const char *s, size_t n, int tt, vector<char> *out) {
    int first = 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int up = tt == TT_UPPER || (tt == TT_CAPITALIZE && first);
        if (c < 0x80) {
            if (up && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
            else if (tt == TT_LOWER && c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) first = 0;
            push_back(out, (char)c);
            continue;
        }
        if (c == 0xC3 && i + 1 < n) {
            unsigned char e = (unsigned char)s[i + 1];
            if (up && e >= 0xA0 && e <= 0xBE && e != 0xB7) e = (unsigned char)(e - 0x20);
            else if (tt == TT_LOWER && e >= 0x80 && e <= 0x9E && e != 0x97) e = (unsigned char)(e + 0x20);
            push_back(out, (char)c);
            push_back(out, (char)e);
            i++;
            first = 0;
            continue;
        }
        push_back(out, (char)c);
        if ((c & 0xC0) != 0x80) first = 0;
    }
}

/* text without its soft hyphens (U+00AD) */
static void lay_strip_soft(vector<char> *tw) {
    size_t o = 0;
    for (size_t q = 0; q < len(*tw); q++) {
        if ((unsigned char)at(*tw, q) == 0xC2 && q + 1 < len(*tw) && (unsigned char)at(*tw, q + 1) == 0xAD) { q++; continue; }
        (*tw)[o++] = at(*tw, q);
    }
    resize(tw, o);
}

/* the code points of UTF-8 text: letter-spacing's count */
static size_t lay_cps(const char *s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) k++;
    return k;
}

/* the pieces of text tx[0, n): a text node's (node), or a pseudo-element's content (node
   LAY_GEN | LAY_GTEXT and its index), in the style s of its element pe */
static void lay_chars(LCTX, const lay_style *s, uint32_t node, uint32_t pe, const char *tx, size_t n, int deco, vector<lay_piece> *ps, int *sp,
                      const lay_inh *ih) {
    size_t i = 0;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, s);
    p.node = node;
    p.elem = pe;
    p.deco = (uint8_t)deco;
    p.shift = (float)ih->shift;
    p.rx = (float)ih->rx;
    p.ry = (float)ih->ry;
    p.va = (uint8_t)ih->va;
    /* a space: its advance, with letter-spacing and word-spacing */
    double space = amb_advance(f, s->face, ' ', s->fs) + s->ls + s->wsp;
    int collapse = s->ws == WS_NORMAL || s->ws == WS_NOWRAP || s->ws == WS_PRE_LINE;
    size_t start = len(*ps);
    while (i < n) {
        char c = tx[i];
        int white = c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\n';
        if (c == '\n' && s->ws != WS_NORMAL && s->ws != WS_NOWRAP) {
            /* a kept newline: a forced break, and in pre-line the spaces before it go */
            if (s->ws == WS_PRE_LINE && len(*ps) && back(*ps).kind == P_SPACE && !back(*ps).keep) pop_back(ps);
            p.kind = P_BREAK;
            p.w = 0;
            push_back(ps, p);
            *sp = collapse;
            i++;
            continue;
        }
        if (white && collapse) {
            if (!*sp) {
                p.kind = P_SPACE;
                p.w = space;
                p.brk = (uint8_t)(s->ws != WS_NOWRAP);
                p.keep = 0;
                p.off = (uint32_t)i;
                p.len = 1;
                push_back(ps, p);
                *sp = 1;
            }
            i++;
            continue;
        }
        if (white) {
            /* kept spaces (pre, pre-wrap): a run of them, a tab to the next stop of eight */
            size_t j = i;
            double w = 0;
            while (j < n && (tx[j] == ' ' || tx[j] == '\t' || tx[j] == '\r' || tx[j] == '\f')) { w += tx[j] == '\t' ? 8 * space : space; j++; }
            p.kind = P_SPACE;
            p.w = w;
            p.brk = (uint8_t)(s->ws == WS_PRE_WRAP);
            p.keep = 1;
            p.off = (uint32_t)i;
            p.len = (uint32_t)(j - i);
            push_back(ps, p);
            *sp = 0;
            i = j;
            continue;
        }
        size_t j = i;
        int soft = 0, rtl = 0, hi = 0;
        while (j < n && !(tx[j] == ' ' || tx[j] == '\t' || tx[j] == '\r' || tx[j] == '\f' || tx[j] == '\n')) {
            unsigned char c0 = (unsigned char)tx[j];
            if (c0 >= 0x80) hi = 1;
            if (c0 >= 0xC2) {
                if (c0 == 0xC2 && j + 1 < n && (unsigned char)tx[j + 1] == 0xAD) soft = 1;
                else if (c0 >= 0xD6 && c0 <= 0xDB) rtl = 1;      /* U+0580 to U+06FF: Hebrew, Arabic */
            }
            j++;
        }
        p.kind = P_WORD;
        p.off = (uint32_t)i;
        p.len = (uint32_t)(j - i);
        p.rtl = (uint8_t)rtl;
        p.soft = (uint8_t)soft;
        if (s->tt || soft) {
            /* text-transform's letters; a soft hyphen, no width where it does not break */
            vector<char> tw;
            if (s->tt) lay_transform(tx + i, j - i, s->tt, &tw);
            else append_range(tw, tx + i, j - i);
            if (soft) lay_strip_soft(&tw);
            p.w = amb_text_width(f, s->face, s->fs, data(tw), len(tw)) + s->ls * (double)lay_cps(data(tw), len(tw));
        } else p.w = amb_text_width(f, s->face, s->fs, tx + i, j - i) + s->ls * (double)lay_cps(tx + i, j - i);
        p.brk = 0;
        p.keep = 0;
        /* the lines of the faces that draw what the word's face has no glyph for */
        if (hi && s->lhn) {
            uint32_t m = amb_text_fallbacks(f, s->face, tx + i, j - i);
            for (int k = 0; m && k < AMB_NFACES; k++) {
                if (!(m >> k & 1)) continue;
                double fa, fd, fg, up, dn;
                amb_face_metrics(f, k, s->fs, &fa, &fd, &fg);
                lay_leading(fa + fd + fg, fa, fd, &up, &dn);
                if (!p.fb || up > p.fa) p.fa = (float)up;
                if (!p.fb || dn > p.fd) p.fd = (float)dn;
                p.fb = 1;
            }
        }
        push_back(ps, p);
        p.rtl = 0;
        p.soft = 0;
        p.fb = 0;
        *sp = 0;
        i = j;
    }
    /* each space's kerning with the letters beside it in this text, which paint draws a
       run with (Chromium shapes the run whole: the Liberation faces kern a space before
       A, T, W and Y, and after them) */
    if (s->tt) return;
    for (size_t k = start; k < len(*ps); k++) {
        lay_piece q = at(*ps, k);
        if (q.kind != P_SPACE) continue;
        double kk = 0;
        if (k > start && at(*ps, k - 1).kind == P_WORD) {
            lay_piece w = at(*ps, k - 1);
            size_t e = w.off + w.len;
            while (e > w.off + 1 && ((unsigned char)tx[e - 1] & 0xC0) == 0x80) e--;
            size_t z = e - 1;
            kk += amb_kern(f, s->face, amb_utf8_next(tx, w.off + w.len, &z), ' ', s->fs);
        }
        if (k + 1 < len(*ps) && at(*ps, k + 1).kind == P_WORD) {
            size_t z = at(*ps, k + 1).off;
            kk += amb_kern(f, s->face, ' ', amb_utf8_next(tx, n, &z), s->fs);
        }
        if (kk != 0) { q.w += kk; (*ps)[k] = q; }
    }
}

static void lay_text_pieces(LCTX, uint32_t node, int deco, vector<lay_piece> *ps, int *sp, const lay_inh *ih) {
    lay_style s;
    if (node & LAY_GEN) {
        /* a pseudo-element's content, in its style */
        uint32_t pe = node & ~LAY_GTEXT;
        lay_style_of(LARGS, pe, &s);
        vector<char> gt;
        amb_span g = at(*gx, node & LAY_GIDX).text;
        append_range(gt, text_ptr(t, g), g.len);
        lay_chars(LARGS, &s, node, pe, data(gt), len(gt), deco, ps, sp, ih);
        return;
    }
    /* a text node's, in its parent's style (read in place) */
    uint32_t pe = d->node[node].parent;
    uint32_t si = pe < len(*sx) ? at(*sx, pe) & LAY_SMASK : 0;
    amb_span span = d->node[node].data;
    lay_chars(LARGS, data(*sc) + si, node, pe, text_ptr(d, span), span.len, deco, ps, sp, ih);
}
/* the pieces of an inline-level node: text, an inline element (its edges around its
   children's pieces), a forced break, an atomic box */
/* the x-height of a style's font, in px (src/paint/font.h) */
static double lay_xh(const lay_style *s) { return s->fs * amb_x_height(s->face); }

/* the parent of a run's item: an element's parent, or a pseudo-element's element */
static uint32_t lay_parent_of(const dom_document *d, const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).el;
    return d->node[x].parent;
}

/* a relatively positioned element's offset (CSS 2.1 9.4.3): left (or right's opposite),
   top (or bottom's), against its containing block's width cw and height ch (-1: none) */
static void lay_rel_offset(const css_style *st, const lay_ext *e, double cw, double ch, double *dx, double *dy) {
    *dx = 0;
    *dy = 0;
    if (e->pos != POS_RELATIVE) return;
    if (e->off[3].unit != LAY_AUTO) *dx = lay_res(st, e->off[3], cw);
    else if (e->off[1].unit != LAY_AUTO) *dx = -lay_res(st, e->off[1], cw);
    if (e->off[0].unit != LAY_AUTO && (e->off[0].unit == LAY_PX || ch >= 0)) *dy = lay_res(st, e->off[0], ch);
    else if (e->off[2].unit != LAY_AUTO && (e->off[2].unit == LAY_PX || ch >= 0)) *dy = -lay_res(st, e->off[2], ch);
}

/* an inline element's vertical-align and relative offset, added to what its parent passes
   (in): the baseline's shift against its parent's font, as Chromium has it */
static void lay_inline_shift(LCTX, uint32_t el, const lay_style *es, double cw, lay_inh *in) {
    int va = LAY_X(el).va;
    if (va != VA_BASELINE) {
        lay_style ps;
        lay_style_of(LARGS, lay_parent_of(d, gx, el), &ps);
        double sh = 0;
        if (va == VA_SUB) sh = ps.fs / 5 + 1;
        else if (va == VA_SUPER) sh = -(ps.fs / 3 + 1);
        else if (va == VA_TEXT_TOP) sh = es->asc - ps.asc;
        else if (va == VA_TEXT_BOTTOM) sh = ps.desc - es->desc;
        else if (va == VA_MIDDLE) sh = (es->asc - es->desc) / 2 - lay_xh(&ps) / 2;
        else if (va == VA_LEN) sh = -lay_res(st, LAY_X(el).valen, es->lh);
        else in->va = va;
        in->shift += sh;
    }
    if (LAY_X(el).pos == POS_RELATIVE) {
        double dx, dy;
        lay_ext e = LAY_X(el);
        lay_rel_offset(st, &e, cw, -1, &dx, &dy);
        in->rx += dx;
        in->ry += dy;
    }
}

static void lay_node_pieces(LCTX, uint32_t node, int deco, double cw, vector<lay_piece> *ps, int *sp, const lay_inh *ih) {
    if (node & LAY_GTEXT) { lay_text_pieces(LARGS, node, deco, ps, sp, ih); return; }
    if (!(node & LAY_GEN)) {
        if (d->node[node].kind == DOM_NODE_KIND_TEXT) { lay_text_pieces(LARGS, node, deco, ps, sp, ih); return; }
        if (d->node[node].kind != DOM_NODE_KIND_ELEMENT) return;
    }
    lay_style s;
    lay_style_of(LARGS, node, &s);
    int dsp = lay_display_of(LARGS, node);
    if (dsp == D_NONE) return;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, &s);
    p.node = node;
    p.elem = node;
    p.shift = (float)ih->shift;
    p.rx = (float)ih->rx;
    p.ry = (float)ih->ry;
    p.va = (uint8_t)ih->va;
    int pos = LAY_X(node).pos;
    if (pos == POS_ABSOLUTE || pos == POS_FIXED || LAY_X(node).flt) {
        /* out of the flow: its static position, or the float placed where the line is */
        p.kind = (pos == POS_ABSOLUTE || pos == POS_FIXED) ? P_ABS : P_FLOAT;
        push_back(ps, p);
        return;
    }
    if (lay_is_named(d, node, "br")) {
        if (len(*ps) && back(*ps).kind == P_SPACE && !back(*ps).keep) pop_back(ps);
        p.kind = P_BREAK;
        push_back(ps, p);
        *sp = 1;
        return;
    }
    if (dsp == D_INLINE_BLOCK || lay_block_level(dsp)) {
        double w, h;
        lay_atomic_size(LARGS, node, &s, cw, &w, &h);
        p.kind = P_ATOMIC;
        p.aw = (float)w;
        p.ah = (float)h;
        p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw) + w + lay_res(st, s.p[1], cw) + s.b[1] + lay_res(st, s.m[1], cw);
        /* its own vertical-align: the box's, against its parent's font */
        int va = LAY_X(node).va;
        if (va == VA_SUB || va == VA_SUPER) {
            lay_style pst;
            lay_style_of(LARGS, lay_parent_of(d, gx, node), &pst);
            if (va == VA_SUB) p.shift += (float)(pst.fs / 5 + 1);
            else p.shift -= (float)(pst.fs / 3 + 1);
        } else if (va == VA_LEN) p.shift -= (float)lay_res(st, LAY_X(node).valen, s.lh);
        else if (va != VA_BASELINE) p.va = (uint8_t)va;
        push_back(ps, p);
        *sp = 0;
        return;
    }
    int inner = deco | s.deco;
    lay_inh in = *ih;
    lay_inline_shift(LARGS, node, &s, cw, &in);
    p.shift = (float)in.shift;
    p.rx = (float)in.rx;
    p.ry = (float)in.ry;
    p.va = (uint8_t)in.va;
    p.kind = P_OPEN;
    p.bg = s.bg;
    p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw);
    push_back(ps, p);
    if (node & LAY_GEN) lay_node_pieces(LARGS, node | LAY_GTEXT, inner, cw, ps, sp, &in);
    else {
        /* its children, after its ::before and before its ::after */
        uint32_t fl0 = node < len(*sx) ? at(*sx, node) : 0;
        if (fl0 & LAY_HAS_BEFORE) {
            int64_t g = lay_gen_find(gx, node, 1);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp, &in);
        }
        for (uint32_t c = d->node[node].first_child; c; c = d->node[c].next_sibling) lay_node_pieces(LARGS, c, inner, cw, ps, sp, &in);
        if (fl0 & LAY_HAS_AFTER) {
            int64_t g = lay_gen_find(gx, node, 2);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp, &in);
        }
    }
    p.kind = P_CLOSE;
    p.w = lay_res(st, s.p[1], cw) + s.b[1] + lay_res(st, s.m[1], cw);
    push_back(ps, p);
}
static void lay_run_pieces(LCTX, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double cw, vector<lay_piece> *ps) {
    lay_style s;
    lay_style_of(LARGS, container, &s);
    int sp = 1;
    lay_inh ih;
    memset(&ih, 0, sizeof ih);
    for (uint32_t k = first; k < first + n; k++) lay_node_pieces(LARGS, at(*items, k), s.deco, cw, ps, &sp, &ih);
}
/* a run shows something: a word, a kept space, an atomic box, a break, or an inline
   element's non-zero edge */
static int lay_run_shows(const vector<lay_piece> *ps) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_WORD || p.kind == P_ATOMIC || p.kind == P_BREAK || (p.kind == P_SPACE && p.keep)) return 1;
        if ((p.kind == P_OPEN || p.kind == P_CLOSE) && p.w > 0) return 1;
    }
    return 0;
}

/* ---- intrinsic widths: an element's margin box at its min- and max-content ---- */

static void lay_run_intrinsic(const vector<lay_piece> *ps, double *mn, double *mx) {
    double line = 0, chunk = 0, pend = 0;
    *mn = 0;
    *mx = 0;
    int start = 1;
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_BREAK) {
            if (line > *mx) *mx = line;
            if (chunk > *mn) *mn = chunk;
            line = 0; chunk = 0; pend = 0; start = 1;
            continue;
        }
        if (p.kind == P_SPACE) {
            if (p.brk || !p.keep) { if (chunk > *mn) *mn = chunk; chunk = 0; }
            if (p.brk) { if (!start) pend += p.w; }
            else { chunk += p.w; line += pend + p.w; pend = 0; }
            continue;
        }
        if (p.kind == P_ATOMIC) { if (chunk > *mn) *mn = chunk; chunk = 0; }
        line += pend + p.w;
        pend = 0;
        chunk += p.w;
        start = 0;
        if (p.kind == P_ATOMIC) { if (chunk > *mn) *mn = chunk; chunk = 0; }
    }
    if (line > *mx) *mx = line;
    if (chunk > *mn) *mn = chunk;
}

static double lay_edges_px(const css_style *st, const lay_style *s, double cw) {
    return (s->m[1].unit == LAY_AUTO ? 0 : lay_res(st, s->m[1], cw)) + (s->m[3].unit == LAY_AUTO ? 0 : lay_res(st, s->m[3], cw)) +
           s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw);
}

static void lay_table_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);

/* el's margin box at its min- and max-content widths; its declared width's when it has one
   and own_width, else its content's */
static void lay_intrinsic_of(LCTX, uint32_t el, double cw, double *mn, double *mx, int own_width) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    double edges = lay_edges_px(st, &s, cw);
    *mn = 0;
    *mx = 0;
    if (s.width.unit == LAY_PX && own_width) {
        double box_edges = s.b[1] + s.b[3] + lay_res(st, s.p[1], cw) + lay_res(st, s.p[3], cw);
        *mn = *mx = s.width.v + edges - (s.border_box ? box_edges : 0);
        return;
    }
    if (lay_replaced(d, el)) {
        double w, h;
        lay_atomic_size(LARGS, el, &s, cw, &w, &h);
        *mn = *mx = w + edges;
        return;
    }
    if (s.display == D_TABLE) { lay_table_intrinsic(LARGS, el, cw, mn, mx); *mn += edges; *mx += edges; return; }
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    /* a row flex container's items side by side (wrapping: its widest one at least) */
    int side = LAY_X(el).lx == LX_FLEX && LAY_X(el).frow;
    int wraps = LAY_X(el).fwrap != 0;
    double gap = side ? lay_res(st, LAY_X(el).cgap, cw) : 0;
    int nitems = 0;
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        double a, b;
        if (sg.block == SEG_ABS) continue;
        if (sg.block) lay_intrinsic(LARGS, sg.node, cw, &a, &b);
        else {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, cw, &ps);
            lay_run_intrinsic(&ps, &a, &b);
        }
        if (side) {
            if (nitems++) { *mx += gap; if (!wraps) *mn += gap; }
            *mx += b;
            if (wraps) { if (a > *mn) *mn = a; }
            else *mn += a;
            continue;
        }
        if (a > *mn) *mn = a;
        if (b > *mx) *mx = b;
    }
    *mn += edges;
    *mx += edges;
}

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) { lay_intrinsic_of(LARGS, el, cw, mn, mx, 1); }

/* ---- the store's records ---- */

static uint32_t lay_new_box(LCTX, uint32_t parent, uint32_t el, layout_box_kind kind) {
    layout_box_h h = box_new(t);
    t->box[h.index].kind = kind;
    t->box[h.index].opacity = 1;
    if (el & LAY_GEN) {
        /* a pseudo-element's box: its element's, and which pseudo-element */
        lay_gen g = at(*gx, el & LAY_GIDX);
        t->box[h.index].node = node_handle(d, g.el);
        t->box[h.index].pseudo = (uint8_t)g.pseudo;
    } else if (el) t->box[h.index].node = node_handle(d, el);
    if (parent) children_append(t, parent, h.index);
    return h.index;
}

/* a box's margins (vertical ones, and horizontal ones that are not auto), borders,
   paddings and colours, against its containing block's width cw */
static void lay_edges(layout_tree *t, const css_style *st, uint32_t b, const lay_style *s, double cw) {
    t->box[b].mt = lay_res(st, s->m[0], cw);
    t->box[b].mb = lay_res(st, s->m[2], cw);
    t->box[b].ml = lay_res(st, s->m[3], cw);
    t->box[b].mr = lay_res(st, s->m[1], cw);
    t->box[b].bt = s->b[0];
    t->box[b].br = s->b[1];
    t->box[b].bb = s->b[2];
    t->box[b].bl = s->b[3];
    t->box[b].pt = lay_res(st, s->p[0], cw);
    t->box[b].pr = lay_res(st, s->p[1], cw);
    t->box[b].pb = lay_res(st, s->p[2], cw);
    t->box[b].pl = lay_res(st, s->p[3], cw);
    t->box[b].background = s->bg;
    t->box[b].bct = s->bc[0];
    t->box[b].bcr = s->bc[1];
    t->box[b].bcb = s->bc[2];
    t->box[b].bcl = s->bc[3];
    t->box[b].bst = s->bs[0];
    t->box[b].bsr = s->bs[1];
    t->box[b].bsb = s->bs[2];
    t->box[b].bsl = s->bs[3];
    t->box[b].avail = cw;
}

/* a block-level box's width and horizontal margins (CSS 2.1, 10.3.3): a declared
   width is the border box's under box-sizing: border-box; max-width holds it narrower */
static void lay_block_width(layout_tree *t, const css_style *st, uint32_t b, const lay_style *s, double cw, const lay_len *minw) {
    double edges = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    int mla = s->m[3].unit == LAY_AUTO, mra = s->m[1].unit == LAY_AUTO;
    double ml = mla ? 0 : t->box[b].ml, mr = mra ? 0 : t->box[b].mr, w;
    int declared = s->width.unit != LAY_AUTO;
    if (!declared) {
        w = cw - ml - mr - edges;
        t->box[b].how = LAYOUT_WIDTH_HOW_FILL;
    } else {
        w = lay_res(st, s->width, cw) - (s->border_box ? edges : 0);
        t->box[b].how = s->width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
    }
    if (w < 0) w = 0;
    if (s->maxw.unit != LAY_AUTO) {
        double mw = lay_res(st, s->maxw, cw) - (s->border_box ? edges : 0);
        if (mw < 0) mw = 0;
        if (w > mw) { w = mw; declared = 1; t->box[b].how = LAYOUT_WIDTH_HOW_MAX_WIDTH; }
    }
    if (minw && minw->unit != LAY_AUTO && !lay_content_sized(*minw)) {
        double mn = lay_res(st, *minw, cw) - (s->border_box ? edges : 0);
        if (w < mn) { w = mn; declared = 1; t->box[b].how = LAYOUT_WIDTH_HOW_MIN_WIDTH; }
    }
    if (declared) {
        double rest = cw - w - edges;
        if (mla && mra) { if (rest > 0) { ml = rest / 2; mr = rest / 2; } else mr = rest; }
        else if (mla) ml = rest - mr;
        else mr = rest - ml;
    }
    t->box[b].w = w;
    t->box[b].ml = ml;
    t->box[b].mr = mr;
}

static double lay_border_height(const layout_tree *t, uint32_t b) {
    return t->box[b].bt + t->box[b].pt + t->box[b].h + t->box[b].pb + t->box[b].bb;
}

/* a child placed in its parent's content box, its border box's top-left at (bx, by) */
static void lay_place(layout_tree *t, uint32_t b, double bx, double by) {
    /* a relatively positioned box: its offset too */
    t->box[b].x = bx + t->box[b].bl + t->box[b].pl + t->box[b].dx;
    t->box[b].y = by + t->box[b].bt + t->box[b].pt + t->box[b].dy;
}
/* ---- margins as they collapse: the largest positive one and the most negative ---- */

typedef struct lay_mc { double pos, neg; } lay_mc;
static lay_mc lay_mc_of(double m) { lay_mc c; c.pos = m > 0 ? m : 0; c.neg = m < 0 ? m : 0; return c; }
static lay_mc lay_mc_add(lay_mc a, lay_mc b) { a.pos = a.pos > b.pos ? a.pos : b.pos; a.neg = a.neg < b.neg ? a.neg : b.neg; return a; }
static double lay_mc_v(lay_mc a) { return a.pos + a.neg; }

/* ---- lines ---- */

/* the vertical extent of a piece about its baseline: its font's line, and under
   line-height normal its fallback faces' (Chromium's AccumulateUsedFonts) */
static void lay_piece_vert(const lay_piece *p, double *above, double *below) {
    lay_leading(p->lh, p->asc, p->desc, above, below);
    if (!p->fb) return;
    if (p->fa > *above) *above = p->fa;
    if (p->fd > *below) *below = p->fd;
}

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty);
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, lay_mc *et, lay_mc *eb,
                              int *empty);
static void lay_box_style(LCTX, uint32_t b, uint32_t el, double cw, double ch);
static void lay_box_paint(LCTX, uint32_t b, uint32_t si);
static amb_span lay_kept(layout_tree *t, const css_style *st, uint32_t c);
static void lay_flex(LCTX, uint32_t b, uint32_t el, double defh);
static void lay_grid(LCTX, uint32_t b, uint32_t el, double defh);

/* an empty text field's placeholder (its attribute's text, on one line, white space
   collapsed) in its content box, in the style of its ::placeholder: a line of one
   generated fragment, in the middle of an input, at the top of a textarea */
static void lay_placeholder(LCTX, uint32_t b, uint32_t el, const lay_style *es) {
    uint32_t a = lay_attr(d, el, "placeholder");
    if (!a || d->attr[a].value.len == 0) return;
    int input = lay_is_named(d, el, "input");
    if (input) {
        uint32_t ty = lay_attr(d, el, "type");
        if (ty) {
            static const char *const text_types[8] = { "text", "search", "email", "url", "tel", "password", "number", "" };
            amb_span tv = d->attr[ty].value;
            int ok = 0;
            for (int k = 0; k < 8 && !ok; k++)
                if (tv.len == strlen(text_types[k]) && memcmp(text_ptr(d, tv), text_types[k], tv.len) == 0) ok = 1;
            if (!ok) return;
        }
        uint32_t v = lay_attr(d, el, "value");
        if (v && d->attr[v].value.len) return;
    } else if (!lay_is_named(d, el, "textarea") || d->node[el].first_child) return;
    lay_style ps = *es;
    lay_pseudo_style(LARGS, el, CSS_PSEUDO_PLACEHOLDER, &ps);
    vector<char> txt;
    amb_span v = d->attr[a].value;
    const char *src = text_ptr(d, v);
    int sp = 1;
    for (size_t i = 0; i < v.len; i++) {
        char c = src[i];
        int white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        if (white) { if (!sp) push_back(txt, ' '); sp = 1; continue; }
        push_back(txt, c);
        sp = 0;
    }
    while (len(txt) && back(txt) == ' ') pop_back(&txt);
    if (!len(txt)) return;
    double tw = amb_text_width(f, ps.face, ps.fs, data(txt), len(txt)) + ps.ls * (double)lay_cps(data(txt), len(txt));
    double h = t->box[b].h, lh = input ? h : ps.lh;
    double base = (lh - (ps.asc + ps.desc)) / 2 + ps.asc;
    layout_line_h lnh = line_new(t);
    uint32_t ln = lnh.index;
    t->line[ln].x = 0;
    t->line[ln].y = 0;
    t->line[ln].w = t->box[b].w;
    t->line[ln].h = lh;
    t->line[ln].baseline = base;
    lines_append(t, b, ln);
    amb_span tsp = text_add(t, data(txt), len(txt));
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    t->frag[fr].kind = LAYOUT_FRAG_KIND_GENERATED;
    t->frag[fr].node = node_handle(d, el);
    t->frag[fr].text = tsp;
    t->frag[fr].x = 0;
    t->frag[fr].y = base - ps.asc;
    t->frag[fr].w = tw;
    t->frag[fr].h = ps.asc + ps.desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (uint8_t)ps.face;
    if (ps.font) t->frag[fr].font = resource_handle(nt, ps.font);
    t->frag[fr].size = ps.fs;
    t->frag[fr].spacing = ps.ls;
    t->frag[fr].word_spacing = ps.wsp;
    t->frag[fr].italic = ps.italic;
    t->frag[fr].color = ps.color;
    frags_append(t, ln, fr);
}

/* an atomic inline's box, laid out: a replaced element's content, or an inline-block's
   children as a new formatting context */
static uint32_t lay_atomic(LCTX, uint32_t parent, const lay_piece *p, double cw) {
    uint32_t el = p->node;
    lay_style s;
    lay_style_of(LARGS, el, &s);
    int replaced = lay_replaced(d, el), alx = LAY_X(el).lx;
    uint32_t b = lay_new_box(LARGS, parent, el, replaced ? LAYOUT_BOX_KIND_REPLACED : alx == LX_FLEX ? LAYOUT_BOX_KIND_FLEX :
                                                alx == LX_GRID ? LAYOUT_BOX_KIND_GRID : LAYOUT_BOX_KIND_INLINE_BLOCK);
    lay_edges(t, st, b, &s, cw);
    lay_box_style(LARGS, b, el, cw, -1);
    if (s.m[3].unit == LAY_AUTO) t->box[b].ml = 0;
    if (s.m[1].unit == LAY_AUTO) t->box[b].mr = 0;
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    t->box[b].w = p->aw;
    t->box[b].how = replaced || s.width.unit == LAY_AUTO ? (replaced ? LAYOUT_WIDTH_HOW_INTRINSIC : LAYOUT_WIDTH_HOW_SHRINK_TO_FIT)
                                                         : (s.width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED);
    if (replaced) {
        t->box[b].h = p->ah > 0 ? p->ah : 0;
        lay_placeholder(LARGS, b, el, &s);
        return b;
    }
    lay_mc et, eb;
    int empty;
    double defh = s.height.unit == LAY_PX ? s.height.v : -1;
    int lx = LAY_X(el).lx;
    if (lx == LX_FLEX) lay_flex(LARGS, b, el, defh);
    else if (lx == LX_GRID) lay_grid(LARGS, b, el, defh);
    else lay_flow_children(LARGS, b, el, 1, defh < 0, 0, 0, defh, &et, &eb, &empty);
    if (defh >= 0) t->box[b].h = defh;
    return b;
}
/* the baseline of a laid-out atomic box, from its margin box's top: its last line's,
   or its bottom margin edge */
/* the first baseline in b's box subtree, from b's content box's top: its first line's, or
   its first child's that has one */
static int lay_first_baseline(const layout_tree *t, uint32_t b, double *y, int depth) {
    if (t->box[b].first_line) { *y = t->line[t->box[b].first_line].y + t->line[t->box[b].first_line].baseline; return 1; }
    if (depth > 32) return 0;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        double cy;
        if (t->box[c].position == LAYOUT_POSITION_ABSOLUTE || t->box[c].position == LAYOUT_POSITION_FIXED) continue;
        if (lay_first_baseline(t, c, &cy, depth + 1)) { *y = t->box[c].y + cy; return 1; }
    }
    return 0;
}

/* an atomic box's baseline, from its margin box's top: an inline-block's last line's, a
   flex or grid container's first item's; else its bottom margin edge */
static double lay_atomic_baseline(const layout_tree *t, uint32_t b) {
    double top = t->box[b].mt + t->box[b].bt + t->box[b].pt;
    if (t->box[b].kind == LAYOUT_BOX_KIND_FLEX || t->box[b].kind == LAYOUT_BOX_KIND_GRID) {
        double y;
        if (lay_first_baseline(t, b, &y, 0)) return top + y;
    }
    uint32_t ln = t->box[b].last_line;
    if (ln) return top + t->line[ln].y + t->line[ln].baseline;
    return t->box[b].mt + lay_border_height(t, b) + t->box[b].mb;
}

/* a word's (or kept spaces') bytes tx[off, off + ln) of its text tx[0, n), as laid out
   (text-transform tt for a word), into out */
static void lay_piece_bytes(const char *tx, size_t n, int word, int tt, uint32_t off, uint32_t ln, vector<char> *out) {
    if (off >= n) return;
    if (off + ln > n) ln = (uint32_t)(n - off);
    if (word && tt) { lay_transform(tx + off, ln, tt, out); return; }
    for (uint32_t i = off; i < off + ln; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);     /* a tab: a space as laid out */
}
/* the text of pieces [a, b) of one text node (or a pseudo-element's content), white space
   as laid out, into out */
static void lay_piece_text(const layout_tree *t, const dom_document *d, const vector<lay_gen> *gx, const vector<lay_piece> *ps, size_t a, size_t b,
                           vector<char> *out) {
    for (size_t k = a; k < b; k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_SPACE && !p.keep) { push_back(out, ' '); continue; }
        if (p.kind != P_WORD && p.kind != P_SPACE) continue;
        size_t from = len(*out);
        if (p.node & LAY_GEN) {
            amb_span g = at(*gx, p.node & LAY_GIDX).text;
            lay_piece_bytes(text_ptr(t, g), g.len, p.kind == P_WORD, p.tt, p.off, p.len, out);
        } else {
            amb_span sp = d->node[p.node].data;
            const char *tx = text_ptr(d, sp);
            if (p.kind == P_WORD && p.tt && p.off + p.len <= sp.len) lay_transform(tx + p.off, p.len, p.tt, out);
            else for (uint32_t i = p.off; i < p.off + p.len && i < sp.len; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);
        }
        if (p.kind != P_WORD || !(p.hy || p.rtl || p.soft)) continue;
        if (p.hy || p.rtl) {
            /* the word's own bytes: soft hyphens out, the hyphen it is broken by, its letters
               in the order they are drawn (right to left: reversed) */
            vector<char> wd;
            for (size_t q = from; q < len(*out); q++) push_back(wd, at(*out, q));
            resize(out, from);
            lay_strip_soft(&wd);
            if (p.hy) push_back(wd, '-');
            if (p.rtl) {
                size_t q = len(wd);
                while (q > 0) {
                    size_t b0 = q - 1;
                    while (b0 > 0 && ((unsigned char)at(wd, b0) & 0xC0) == 0x80) b0--;
                    for (size_t z = b0; z < q; z++) push_back(out, at(wd, z));
                    q = b0;
                }
            } else append_range(out, data(wd), len(wd));
        } else {
            /* soft hyphens where the word is not broken: none drawn */
            vector<char> wd;
            for (size_t q = from; q < len(*out); q++) push_back(wd, at(*out, q));
            resize(out, from);
            lay_strip_soft(&wd);
            append_range(out, data(wd), len(wd));
        }
    }
}

/* one line of pieces [a, e) in box b at y, w wide; returns its height. open: the inline
   elements open at its start, which it leaves as those open at its end */
static double lay_line(LCTX, uint32_t b, const lay_style *bs, const vector<lay_piece> *ps, size_t a, size_t e, double x0, double w, double y,
                       int justify_ok, double indent, vector<uint32_t> *open) {
    /* trailing collapsible spaces hang: they take no width */
    size_t end = e;
    while (end > a && at(*ps, end - 1).kind == P_SPACE && !at(*ps, end - 1).keep) end--;
    /* the pieces' x and the line's vertical extent: each piece about the baseline, shifted
       by its vertical-align; top- and bottom-aligned ones against the line box after */
    vector<double> xs;
    vector<uint32_t> atoms;
    vector<double> abase;            /* each atomic box's baseline, from its margin box's top, as its vertical-align has it */
    double x = indent, above, below, max_above, max_below, top_h = 0, bottom_h = 0;
    lay_leading(bs->lh, bs->asc, bs->desc, &max_above, &max_below);
    int spaces = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        push_back(xs, x);
        if (p.kind == P_SPACE && !p.keep) spaces++;
        if (p.kind == P_WORD || (p.kind == P_SPACE && p.keep)) {
            lay_piece_vert(&p, &above, &below);
            if (p.va == VA_TOP) { if (above + below > top_h) top_h = above + below; }
            else if (p.va == VA_BOTTOM) { if (above + below > bottom_h) bottom_h = above + below; }
            else {
                if (above - p.shift > max_above) max_above = above - p.shift;
                if (below + p.shift > max_below) max_below = below + p.shift;
            }
        }
        if (p.kind == P_ATOMIC) {
            uint32_t ab0 = lay_atomic(LARGS, b, &p, w);
            push_back(atoms, ab0);
            double mh = t->box[ab0].mt + lay_border_height(t, ab0) + t->box[ab0].mb;
            double base = lay_atomic_baseline(t, ab0);
            if (p.va == VA_MIDDLE || p.va == VA_TEXT_TOP || p.va == VA_TEXT_BOTTOM) {
                /* against its parent's font */
                lay_style pst;
                lay_style_of(LARGS, lay_parent_of(d, gx, p.node), &pst);
                if (p.va == VA_MIDDLE) base = mh / 2 + lay_xh(&pst) / 2;
                else if (p.va == VA_TEXT_TOP) base = pst.asc;
                else base = mh - pst.desc;
            }
            push_back(abase, base);
            if (p.va == VA_TOP) { if (mh > top_h) top_h = mh; }
            else if (p.va == VA_BOTTOM) { if (mh > bottom_h) bottom_h = mh; }
            else {
                if (base - p.shift > max_above) max_above = base - p.shift;
                if (mh - base + p.shift > max_below) max_below = mh - base + p.shift;
            }
        }
        x += p.w;
    }
    /* bidirectional text (UAX #9, by words): each piece's level -- a right-to-left word's 1,
       a left-to-right one's 0 (2 in a right-to-left paragraph), the others' (spaces,
       punctuation, boxes) their neighbours' when those agree, else the paragraph's, the
       line's trailing spaces the paragraph's -- and the pieces placed in the order L2
       gives them, each run of a level and above reversed, from the highest down */
    int reordered = 0;
    if (bs->rtl && end > a) {
        size_t m = end - a;
        vector<int> lv;
        for (size_t k = a; k < end; k++) {
            lay_piece p = at(*ps, k);
            int v = -1;
            if (p.kind == P_WORD) v = p.rtl ? 1 : (bs->rtl & 1 ? 2 : 0);
            push_back(lv, v);
        }
        for (size_t q = 0; q < m;) {
            if (at(lv, q) >= 0) { q++; continue; }
            size_t e2 = q;
            while (e2 < m && at(lv, e2) < 0) e2++;
            int para = bs->rtl & 1;
            int before = q > 0 ? at(lv, q - 1) % 2 : para, after = e2 < m ? at(lv, e2) % 2 : para;
            int v = before == after ? (before ? 1 : (para ? 2 : 0)) : para;
            if (e2 == m) v = para;
            for (size_t z = q; z < e2; z++) lv[z] = v;
            q = e2;
        }
        int maxl = 0;
        for (size_t q = 0; q < m; q++) if (at(lv, q) > maxl) maxl = at(lv, q);
        vector<uint32_t> ord;
        for (size_t q = 0; q < m; q++) push_back(ord, (uint32_t)q);
        for (int l = maxl; l >= 1; l--)
            for (size_t q = 0; q < m;) {
                if (at(lv, at(ord, q)) < l) { q++; continue; }
                size_t e2 = q;
                while (e2 < m && at(lv, at(ord, e2)) >= l) e2++;
                for (size_t i0 = q, i1 = e2 - 1; i0 < i1; i0++, i1--) { uint32_t tmp = at(ord, i0); ord[i0] = at(ord, i1); ord[i1] = tmp; }
                q = e2;
            }
        double vx = indent;
        for (size_t q = 0; q < m; q++) { uint32_t kk = at(ord, q); xs[kk] = vx; vx += at(*ps, a + kk).w; }
        reordered = maxl > 0;
    }
    /* a top-aligned piece taller than the rest grows the line downward, a bottom-aligned one upward */
    if (top_h > max_above + max_below) max_below = top_h - max_above;
    if (bottom_h > max_above + max_below) max_above = bottom_h - max_below;
    double line_w = x, free = w - line_w, shift = 0, extra = 0;
    if (bs->align == AL_RIGHT && free > 0) shift = free;
    else if (bs->align == AL_CENTER && free > 0) shift = free / 2;
    else if (bs->align == AL_JUSTIFY && justify_ok && free > 0 && spaces > 0) extra = free / spaces;
    double baseline = max_above, height = max_above + max_below;
    layout_line_h lh = line_new(t);
    uint32_t ln = lh.index;
    t->line[ln].x = x0;
    t->line[ln].y = y;
    t->line[ln].w = w;
    t->line[ln].h = height;
    t->line[ln].baseline = baseline;
    lines_append(t, b, ln);
    /* the inline elements' backgrounds, under the text: each open one from where it is
       opened (or the line's start) to where it is closed (or the line's end) */
    vector<double> starts;           /* for each open element, where it opens on this line (-1: before the line), and its baseline's shift */
    for (size_t k = 0; k < len(*open); k++) { push_back(starts, -1.0); push_back(starts, 0.0); }
    double gap = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        double px = at(xs, k - a) + shift + gap;
        if (p.kind == P_SPACE && !p.keep) gap += extra;
        if (p.kind == P_ABS) {
            /* an absolutely positioned element in the line: its static position */
            lay_abs sa;
            sa.el = p.node;
            sa.box = b;
            sa.x = x0 + px;
            sa.y = y;
            push_back(ab, sa);
        }
        if (p.kind == P_OPEN) { push_back(open, p.node); push_back(starts, px); push_back(starts, (double)p.shift); }
        if (p.kind == P_CLOSE && len(*open)) {
            /* its background, from its start on this line (or the line's start) to here */
            pop_back(&starts);
            double from = back(starts);
            uint32_t elx = back(*open);
            pop_back(open);
            pop_back(&starts);
            lay_style es;
            lay_style_of(LARGS, elx, &es);
            if ((es.bg & 0xff) != 0) {
                double x0b = from < 0 ? 0 : from;
                layout_frag_h fh = frag_new(t);
                uint32_t fr = fh.index;
                t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
                t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
                t->frag[fr].x = x0b + (from < 0 ? 0 : lay_res(st, es.m[3], w)) + p.rx;
                t->frag[fr].w = px + p.w - lay_res(st, es.m[1], w) + p.rx - t->frag[fr].x;
                t->frag[fr].y = baseline + p.shift - es.asc - lay_res(st, es.p[0], w) - es.b[0] + p.ry;
                t->frag[fr].h = es.asc + es.desc + lay_res(st, es.p[0], w) + lay_res(st, es.p[2], w) + es.b[0] + es.b[2];
                t->frag[fr].baseline = baseline;
                t->frag[fr].background = es.bg;
                frags_append(t, ln, fr);
            }
        }
    }
    /* the inline elements still open at the line's end: their backgrounds to the end */
    for (size_t k = 0; k < len(*open); k++) {
        uint32_t elx = at(*open, k);
        lay_style es;
        lay_style_of(LARGS, elx, &es);
        if ((es.bg & 0xff) == 0) continue;
        double from = at(starts, 2 * k);
        double x0b = from < 0 ? 0 : from + lay_res(st, es.m[3], w);
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
        t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
        t->frag[fr].x = x0b;
        t->frag[fr].w = line_w + shift + gap - x0b;
        t->frag[fr].y = baseline + at(starts, 2 * k + 1) - es.asc - lay_res(st, es.p[0], w) - es.b[0];
        t->frag[fr].h = es.asc + es.desc + lay_res(st, es.p[0], w) + lay_res(st, es.p[2], w) + es.b[0] + es.b[2];
        t->frag[fr].baseline = baseline;
        t->frag[fr].background = es.bg;
        frags_append(t, ln, fr);
    }
    /* the text runs (a text node's words and spaces, as one fragment unless justified)
       and the atomic boxes */
    size_t k = a, atom = 0;
    gap = 0;
    while (k < end) {
        lay_piece p = at(*ps, k);
        double px = at(xs, k - a) + shift + gap;
        if (p.kind == P_ATOMIC) {
            uint32_t ab0 = at(atoms, atom);
            double base = at(abase, atom);
            atom++;
            double mh = t->box[ab0].mt + lay_border_height(t, ab0) + t->box[ab0].mb;
            double top = baseline - base + p.shift;
            if (p.va == VA_TOP) top = 0;
            else if (p.va == VA_BOTTOM) top = height - mh;
            lay_place(t, ab0, x0 + px + p.rx + t->box[ab0].ml, y + top + p.ry + t->box[ab0].mt);
            layout_frag_h fh = frag_new(t);
            uint32_t fr = fh.index;
            t->frag[fr].kind = LAYOUT_FRAG_KIND_ATOMIC;
            t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
            t->frag[fr].atomic = ab0;
            t->frag[fr].x = px + p.rx;
            t->frag[fr].y = top + p.ry;
            t->frag[fr].w = p.w;
            t->frag[fr].h = mh;
            t->frag[fr].baseline = baseline;
            frags_append(t, ln, fr);
            k++;
            continue;
        }
        if (p.kind != P_WORD && !(p.kind == P_SPACE && p.keep)) {
            if (p.kind == P_SPACE) gap += extra;
            k++;
            continue;
        }
        /* a run: this text node's words and spaces that follow, on this line, in one style */
        size_t r = k + 1;
        double rw = p.w;
        while (r < end && extra == 0 && !reordered) {
            lay_piece q = at(*ps, r);
            if ((q.kind != P_WORD && q.kind != P_SPACE) || q.node != p.node) break;
            if (q.size != p.size || q.color != p.color || q.face != p.face || q.font != p.font || q.deco != p.deco) break;
            if (q.shift != p.shift || q.va != p.va || q.rx != p.rx || q.ry != p.ry) break;
            rw += q.w;
            r++;
        }
        while (r > k + 1 && at(*ps, r - 1).kind == P_SPACE && !at(*ps, r - 1).keep) { r--; rw -= at(*ps, r).w; }
        vector<char> txt;
        lay_piece_text(t, d, gx, ps, k, r, &txt);
        amb_span sp = text_add(t, data(txt), len(txt));
        /* its baseline: the line's, shifted; a top- or bottom-aligned run's against the line box */
        double pb = baseline + p.shift;
        if (p.va == VA_TOP || p.va == VA_BOTTOM) {
            lay_piece_vert(&p, &above, &below);
            pb = p.va == VA_TOP ? above : height - below;
        }
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        /* a pseudo-element's content: generated, its element's */
        t->frag[fr].kind = (p.node & LAY_GEN) ? LAYOUT_FRAG_KIND_GENERATED : LAYOUT_FRAG_KIND_TEXT;
        t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
        t->frag[fr].text = sp;
        t->frag[fr].x = px + p.rx;
        t->frag[fr].y = pb - p.asc + p.ry;
        t->frag[fr].w = rw;
        t->frag[fr].h = p.asc + p.desc;
        t->frag[fr].baseline = pb + p.ry;
        t->frag[fr].face = p.face;
        if (p.font) t->frag[fr].font = resource_handle(nt, p.font);
        t->frag[fr].spacing = p.ls;
        t->frag[fr].word_spacing = p.wsp;
        t->frag[fr].size = p.size;
        t->frag[fr].italic = p.italic;
        t->frag[fr].color = p.color;
        t->frag[fr].decoration = p.deco;
        if (p.elem) {
            uint32_t ts = LAY_X(p.elem).tshadow;
            if (ts) t->frag[fr].shadows = lay_kept(t, st, ts);
        }
        frags_append(t, ln, fr);
        k = r;
    }
    return height;
}

/* ---- floats (CSS 2.1 9.5) ---- */

/* the left and right edges a band [y, y + h) of the block formatting context leaves a
   content box at ox, w wide, beside the floats in it; next: the nearest bottom of a float
   in the band (-1 when none is) */
static void lay_band(const vector<lay_flt> *fl, double ox, double w, double y, double h, double *l, double *r, double *next) {
    *l = 0;
    *r = w;
    *next = -1;
    for (size_t k = 0; k < len(*fl); k++) {
        lay_flt q = at(*fl, k);
        if (q.h <= 0 || q.y >= y + h || q.y + q.h <= y) continue;
        if (q.side == FL_LEFT) { if (q.x + q.w - ox > *l) *l = q.x + q.w - ox; }
        else if (q.x - ox < *r) *r = q.x - ox;
        if (*next < 0 || q.y + q.h < *next) *next = q.y + q.h;
    }
    if (*r < *l) *r = *l;
}

/* the lowest bottom of the floats on the sides clr clears (1 left, 2 right, 3 both) */
static double lay_clear_y(const vector<lay_flt> *fl, int clr) {
    double y = 0;
    for (size_t k = 0; k < len(*fl); k++) {
        lay_flt q = at(*fl, k);
        if (((clr & 1) && q.side == FL_LEFT) || ((clr & 2) && q.side == FL_RIGHT)) if (q.y + q.h > y) y = q.y + q.h;
    }
    return y;
}

/* a float's box, laid out as a child of b in b's content box cw wide: its width shrunk
   to fit unless declared, its content a formatting context of its own */
static uint32_t lay_float_box(LCTX, uint32_t b, uint32_t el, double cw, double ch) {
    lay_bk k;
    k.ox = 0;
    k.oy = 0;
    k.ch = ch;
    k.fw = -1;
    k.shrink = 1;
    k.item = 0;
    lay_mc mt, mb;
    int empty;
    uint32_t fb = lay_block(LARGS, b, el, cw, 0, 0, 0, 0, &k, &mt, &mb, &empty);
    t->box[fb].float_side = LAY_X(el).flt == FL_RIGHT ? LAYOUT_FLOAT_SIDE_RIGHT : LAYOUT_FLOAT_SIDE_LEFT;
    return fb;
}

/* a float placed (CSS 2.1 9.5.1): as high as y (in b's content box, at (ox, oy) in the
   block formatting context) and no higher than an earlier float, past what it clears, as
   far to its side as the floats there leave room, lower where they leave too little; its
   margin box kept in fl */
static void lay_float_place(LCTX, uint32_t fb, uint32_t el, double cw, double ox, double oy, double y) {
    int side = t->box[fb].float_side == LAYOUT_FLOAT_SIDE_RIGHT ? FL_RIGHT : FL_LEFT;
    double mw = t->box[fb].ml + t->box[fb].bl + t->box[fb].pl + t->box[fb].w + t->box[fb].pr + t->box[fb].br + t->box[fb].mr;
    double mh = t->box[fb].mt + lay_border_height(t, fb) + t->box[fb].mb;
    double fy = oy + y;
    for (size_t k = 0; k < len(*fl); k++) if (at(*fl, k).y > fy) fy = at(*fl, k).y;
    int clr = LAY_X(el).clr;
    if (clr) { double cy = lay_clear_y(fl, clr); if (cy > fy) fy = cy; }
    double fx = 0;
    for (int guard = 0; guard < 200; guard++) {
        double l, r, next;
        lay_band(fl, ox, cw, fy, mh > 0 ? mh : 0.01, &l, &r, &next);
        if (r - l >= mw - 0.01 || next < 0) { fx = side == FL_LEFT ? l : r - mw; break; }
        fy = next;
    }
    lay_flt q;
    q.x = ox + fx;
    q.y = fy;
    q.w = mw;
    q.h = mh;
    q.side = side;
    push_back(fl, q);
    lay_place(t, fb, fx + t->box[fb].ml, fy - oy + t->box[fb].mt);
}

/* the out-of-flow pieces of a run that shows nothing, where it is (y in b's content box):
   the positioned elements' static positions, the floats placed */
static void lay_run_oof(LCTX, uint32_t b, const vector<lay_piece> *ps, double w, double ox, double oy, double y) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_ABS) {
            lay_abs sa;
            sa.el = p.node;
            sa.box = b;
            sa.x = 0;
            sa.y = y;
            push_back(ab, sa);
        } else if (p.kind == P_FLOAT) {
            uint32_t fb = lay_float_box(LARGS, b, p.node, w, -1);
            lay_float_place(LARGS, fb, p.node, w, ox, oy, y);
        }
    }
}

/* the width of the first chunk of pieces from i: what a line must hold to begin */
static double lay_first_chunk(const vector<lay_piece> *ps, size_t i) {
    double w = 0;
    size_t c = i;
    while (c < len(*ps) && at(*ps, c).kind != P_SPACE && at(*ps, c).kind != P_BREAK) {
        if (c > i && (at(*ps, c).kind == P_ATOMIC || at(*ps, c - 1).kind == P_ATOMIC)) break;
        w += at(*ps, c).w;
        c++;
    }
    return w;
}
/* ---- hyphenation (Liang's algorithm over TeX's American English patterns) ---- */

/* the patterns, found by their letters' hash: each slot a pattern's offset in
   amb_hyph_patterns, plus one (0 an empty slot); made once a process */
#define LAY_HYPH_SLOTS 16384
static uint32_t lay_hyph_slot[LAY_HYPH_SLOTS];
static int lay_hyph_ready;

static uint32_t lay_hyph_hash(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h & (LAY_HYPH_SLOTS - 1);
}
static int lay_digit(char c) { return c >= '0' && c <= '9'; }
static void lay_hyph_load(void) {
    if (lay_hyph_ready) return;
    lay_hyph_ready = 1;
    size_t i = 0, n = sizeof amb_hyph_patterns - 1;
    while (i < n) {
        size_t b = i, e = i;
        while (i < n && amb_hyph_patterns[i] != ' ') i++;
        while (e < i && !lay_digit(amb_hyph_patterns[e])) e++;
        uint32_t h = lay_hyph_hash(amb_hyph_patterns + b, e - b);
        while (lay_hyph_slot[h]) h = (h + 1) & (LAY_HYPH_SLOTS - 1);
        lay_hyph_slot[h] = (uint32_t)b + 1;
        i++;
    }
}

/* the places a word of lower-case letters w[0, n) may be broken at (CSS hyphens: auto):
   ok[i] is 1 where a hyphen may come before letter i -- an exception's hyphens, or the odd
   values Liang's patterns give, two letters at least on each side */
static void lay_hyph_points(const char *w, size_t n, uint8_t *ok) {
    memset(ok, 0, n + 1);
    if (n < 5 || n > 60) return;
    /* an exception: its hyphens as written */
    size_t i = 0, en = sizeof amb_hyph_exceptions - 1;
    while (i < en) {
        size_t b = i;
        while (i < en && amb_hyph_exceptions[i] != ' ') i++;
        size_t k = 0, q = b;
        int same = 1;
        for (; q < i && same; q++) {
            if (amb_hyph_exceptions[q] == '-') continue;
            if (k >= n || amb_hyph_exceptions[q] != w[k]) same = 0;
            else k++;
        }
        if (same && k == n) {
            k = 0;
            for (q = b; q < i; q++) { if (amb_hyph_exceptions[q] == '-') ok[k] = 1; else k++; }
            return;
        }
        i++;
    }
    lay_hyph_load();
    char s0[64];
    s0[0] = '.';
    memcpy(s0 + 1, w, n);
    s0[n + 1] = '.';
    size_t m = n + 2;
    uint8_t vals[64];
    memset(vals, 0, sizeof vals);
    for (size_t a = 0; a < m; a++)
        for (size_t k = 1; k <= AMB_HYPH_LONGEST && a + k <= m; k++) {
            uint32_t h = lay_hyph_hash(s0 + a, k);
            while (lay_hyph_slot[h]) {
                uint32_t off = lay_hyph_slot[h] - 1;
                if (memcmp(amb_hyph_patterns + off, s0 + a, k) == 0 && lay_digit(amb_hyph_patterns[off + k])) {
                    for (size_t q = 0; q <= k; q++) {
                        uint8_t v = (uint8_t)(amb_hyph_patterns[off + k + q] - '0');
                        if (v > vals[a + q]) vals[a + q] = v;
                    }
                    break;
                }
                h = (h + 1) & (LAY_HYPH_SLOTS - 1);
            }
        }
    /* vals[j]: the value before s0[j]; letter i of w is s0[i + 1] */
    for (size_t q = 2; q + 2 <= n; q++) ok[q] = (uint8_t)(vals[q + 1] & 1);
}

/* where a word piece p can be broken by a hyphen, the part before it and the hyphen
   fitting in room: its last soft hyphen that fits (hyph 1 or 2), else (hyph 2) the last of
   Liang's points; the bytes its part keeps (*keep), where its rest begins (*rest) and the
   part's width (*pw); 0 when it has no such place */
static int lay_hyph_split(LCTX, const vector<lay_piece> *ps, size_t k, int hyph, double room, uint32_t *keep, uint32_t *rest, double *pw) {
    lay_piece p = at(*ps, k);
    vector<char> raw;
    if (p.node & LAY_GEN) { amb_span g = at(*gx, p.node & LAY_GIDX).text; if (p.off + p.len <= g.len) append_range(raw, text_ptr(t, g) + p.off, p.len); }
    else { amb_span sp = d->node[p.node].data; if (p.off + p.len <= sp.len) append_range(raw, text_ptr(d, sp) + p.off, p.len); }
    if (len(raw) != p.len || p.len < 2) return 0;
    double hw = amb_text_width(f, p.face, p.size, "-", 1) + p.ls;
    /* the candidates: soft hyphens, else Liang's points in the word's run of letters */
    vector<uint32_t> cuts;           /* each: the part's bytes, then the rest's start */
    for (uint32_t q = 1; q + 1 < p.len; q++)
        if ((unsigned char)at(raw, q) == 0xC2 && (unsigned char)at(raw, q + 1) == 0xAD) { push_back(cuts, q); push_back(cuts, q + 2); }
    if (!len(cuts) && hyph == 2) {
        uint32_t a = 0, z = p.len;
        while (a < z && !((at(raw, a) >= 'a' && at(raw, a) <= 'z') || (at(raw, a) >= 'A' && at(raw, a) <= 'Z'))) a++;
        while (z > a && !((at(raw, z - 1) >= 'a' && at(raw, z - 1) <= 'z') || (at(raw, z - 1) >= 'A' && at(raw, z - 1) <= 'Z'))) z--;
        char lw[64];
        int letters = z - a < 61;
        for (uint32_t q = a; q < z && letters; q++) {
            char c = at(raw, q);
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c < 'a' || c > 'z') letters = 0;
            else lw[q - a] = c;
        }
        if (letters && z > a) {
            uint8_t ok[64];
            lay_hyph_points(lw, z - a, ok);
            for (uint32_t q = 1; q < z - a; q++) if (ok[q]) { push_back(cuts, a + q); push_back(cuts, a + q); }
        }
    }
    /* the last that fits */
    for (size_t c = len(cuts); c >= 2; c -= 2) {
        uint32_t kb = at(cuts, c - 2);
        vector<char> part;
        if (p.tt) lay_transform(data(raw), kb, p.tt, &part);
        else append_range(part, data(raw), kb);
        lay_strip_soft(&part);
        double w = amb_text_width(f, p.face, p.size, data(part), len(part)) + p.ls * (double)lay_cps(data(part), len(part)) + hw;
        if (w <= room + 0.01) { *keep = kb; *rest = at(cuts, c - 1); *pw = w; return 1; }
    }
    return 0;
}

/* a word piece broken at a hyphenation point: its part (keep bytes, pw wide, the hyphen
   after it) and its rest, the next piece */
static void lay_hyph_apply(LCTX, vector<lay_piece> *ps, size_t k, uint32_t keep, uint32_t rest, double pw) {
    lay_piece p = at(*ps, k), q = p;
    p.len = keep;
    p.w = pw;
    p.hy = 1;
    q.off = p.off + rest;
    q.len = at(*ps, k).len - rest;
    /* the rest's width: its own letters */
    vector<char> raw;
    if (q.node & LAY_GEN) { amb_span g = at(*gx, q.node & LAY_GIDX).text; append_range(raw, text_ptr(t, g) + q.off, q.len); }
    else { amb_span sp = d->node[q.node].data; append_range(raw, text_ptr(d, sp) + q.off, q.len); }
    vector<char> tw;
    if (q.tt) lay_transform(data(raw), len(raw), q.tt, &tw);
    else append_range(tw, data(raw), len(raw));
    lay_strip_soft(&tw);
    q.w = amb_text_width(f, q.face, q.size, data(tw), len(tw)) + q.ls * (double)lay_cps(data(tw), len(tw));
    (*ps)[k] = p;
    push_back(ps, q);
    for (size_t z = len(*ps) - 1; z > k + 1; z--) (*ps)[z] = at(*ps, z - 1);
    (*ps)[k + 1] = q;
}

/* the end of the first letter of text s[0, n): the punctuation before it and the letter
   (a code point of UTF-8); 0 when there is none */
static size_t lay_letter_end(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        int punct = c == '"' || c == '\'' || c == '(' || c == '[' || c == '{' || c == '!' || c == '?' || c == '*' || c == '#' || c == '%' ||
                    c == '&' || c == '@' || c == '/' || c == '-' || c == '.' || c == ',' || c == ':' || c == ';';
        /* the quotes and guillemets: U+00AB, U+00BB, U+2018 to U+201F */
        int upunct = (c == 0xC2 && i + 1 < n && ((unsigned char)s[i + 1] == 0xAB || (unsigned char)s[i + 1] == 0xBB)) ||
                     (c == 0xE2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] >= 0x98 && (unsigned char)s[i + 2] <= 0x9F);
        if (punct) { i++; continue; }
        if (upunct) { i += c == 0xC2 ? 2 : 3; continue; }
        size_t e = i + 1;
        while (e < n && ((unsigned char)s[e] & 0xC0) == 0x80) e++;
        return e;
    }
    return 0;
}

/* ::first-letter: the run's first letter (with the punctuation before it), a piece of its
   own in the pseudo-element's style g (the gen index gi), its box's edges about it */
static void lay_first_letter(LCTX, vector<lay_piece> *ps, uint32_t gi, double cw) {
    size_t k = 0;
    while (k < len(*ps) && at(*ps, k).kind != P_WORD) {
        int kd = at(*ps, k).kind;
        if (kd == P_ATOMIC || kd == P_BREAK || (kd == P_SPACE && at(*ps, k).keep)) return;
        k++;
    }
    if (k >= len(*ps)) return;
    lay_style g;
    lay_style_of(LARGS, LAY_GEN | gi, &g);
    lay_piece p = at(*ps, k);
    vector<char> wt;
    lay_piece_text(t, d, gx, ps, k, k + 1, &wt);
    size_t cut = lay_letter_end(data(wt), len(wt));
    if (cut == 0 || cut > p.len) return;
    lay_piece a = p, r = p;
    lay_piece_style(&a, &g);
    a.len = (uint32_t)cut;
    a.w = amb_text_width(f, a.face, a.size, data(wt), cut) + a.ls * (double)lay_cps(data(wt), cut);
    r.off = p.off + (uint32_t)cut;
    r.len = p.len - (uint32_t)cut;
    r.w = amb_text_width(f, r.face, r.size, data(wt) + cut, len(wt) - cut) + r.ls * (double)lay_cps(data(wt) + cut, len(wt) - cut);
    vector<lay_piece> out;
    for (size_t q = 0; q < k; q++) push_back(out, at(*ps, q));
    lay_piece o;
    memset(&o, 0, sizeof o);
    lay_piece_style(&o, &g);
    o.node = LAY_GEN | gi;
    o.elem = LAY_GEN | gi;
    o.kind = P_OPEN;
    o.bg = g.bg;
    o.w = lay_res(st, g.m[3], cw) + g.b[3] + lay_res(st, g.p[3], cw);
    push_back(out, o);
    push_back(out, a);
    o.kind = P_CLOSE;
    o.w = lay_res(st, g.p[1], cw) + g.b[1] + lay_res(st, g.m[1], cw);
    push_back(out, o);
    if (r.len > 0) push_back(out, r);
    for (size_t q = k + 1; q < len(*ps); q++) push_back(out, at(*ps, q));
    clear(ps);
    append_range(ps, data(out), len(out));
}

/* ::first-line: the pieces in the style of the pseudo-element fls where they have their
   block's (bs) -- what they inherit -- measured again */
static void lay_first_line_pieces(LCTX, vector<lay_piece> *ps, const lay_style *bs, const lay_style *fls) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.color == bs->color) p.color = fls->color;
        if (p.face != bs->face || p.font != bs->font || p.size != bs->fs || p.ls != bs->ls || p.wsp != bs->wsp) { (*ps)[k] = p; continue; }
        double old_space = amb_advance(f, p.face, ' ', p.size) + p.ls + p.wsp;
        p.face = (uint8_t)fls->face;
        p.font = fls->font;
        p.italic = (uint8_t)fls->italic;
        p.size = fls->fs;
        p.lh = fls->lh;
        p.asc = fls->asc;
        p.desc = fls->desc;
        p.ls = fls->ls;
        p.wsp = fls->wsp;
        if (fls->tt && p.kind == P_WORD) p.tt = (uint8_t)fls->tt;
        double space = amb_advance(f, p.face, ' ', p.size) + p.ls + p.wsp;
        if (p.kind == P_WORD) {
            vector<char> wt;
            (*ps)[k] = p;
            lay_piece_text(t, d, gx, ps, k, k + 1, &wt);
            p.w = amb_text_width(f, p.face, p.size, data(wt), len(wt)) + p.ls * (double)lay_cps(data(wt), len(wt));
        } else if (p.kind == P_SPACE) p.w = old_space > 0 ? p.w * space / old_space : space;
        (*ps)[k] = p;
    }
}

/* the lines of the inline run [first, first + n) of items in box b, w wide; returns their height */
static double lay_lines(LCTX, uint32_t b, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double w, double ox,
                        double oy) {
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, container, items, first, n, w, &ps);
    if (!lay_run_shows(&ps)) {
        lay_run_oof(LARGS, b, &ps, w, ox, oy, 0);
        return 0;
    }
    lay_style bs;
    lay_style_of(LARGS, container, &bs);
    /* the block's ::first-letter and ::first-line, in its first run: the first line laid
       out in the pseudo-element's style (its strut too), the rest as they were */
    lay_style fls, bs1 = bs;
    vector<lay_piece> rest;
    int has_fl = 0;
    if (first == 0 && !(container & LAY_GEN) && container < len(*sx) && (at(*sx, container) & LAY_HAS_OTHER)) {
        int64_t gl = lay_gen_find(gx, container, CSS_PSEUDO_FIRST_LETTER);
        if (gl >= 0) lay_first_letter(LARGS, &ps, (uint32_t)gl, w);
        if (lay_pseudo_style(LARGS, container, CSS_PSEUDO_FIRST_LINE, &fls)) {
            has_fl = 1;
            append_range(rest, data(ps), len(ps));
            lay_first_line_pieces(LARGS, &ps, &bs, &fls);
            bs1.lh = fls.lh;
            bs1.asc = fls.asc;
            bs1.desc = fls.desc;
        }
    }
    /* bidirectional text to reorder: the paragraph's direction (1), right-to-left words (2) */
    for (size_t q = 0; q < len(ps) && !(bs.rtl & 2); q++) if (at(ps, q).rtl) { bs.rtl |= 2; bs1.rtl |= 2; }
    vector<uint32_t> open;
    vector<uint32_t> later;           /* floats too wide for the line they are met on: placed below it */
    size_t i = 0, np = len(ps);
    double y = 0;
    /* text-indent: the first line of the block's first run, from its start edge */
    double indent = first == 0 ? lay_res(st, bs.indent, w) : 0;
    while (i < np) {
        while (i < np && at(ps, i).kind == P_SPACE && !at(ps, i).keep) i++;
        if (i >= np) break;
        /* the line's band beside the floats, lower when its first chunk does not fit */
        double lx = 0, lw = w;
        if (len(*fl)) {
            double need = indent + lay_first_chunk(&ps, i);
            for (int guard = 0; guard < 200; guard++) {
                double l, r, next;
                lay_band(fl, ox, w, oy + y, bs.lh, &l, &r, &next);
                lx = l;
                lw = r - l;
                if (lw >= need - 0.01 || next < 0) break;
                y = next - oy;
            }
        }
        size_t start = i, j = i, end = i;
        double x = indent;
        int forced = 0;
        while (j < np) {
            if (at(ps, j).kind == P_BREAK) { end = j; j++; forced = 1; break; }
            if (at(ps, j).kind == P_FLOAT) {
                /* a float met in the line: placed at its top if it fits beside what the line
                   holds, the line then narrowed; else below the line */
                uint32_t fel = at(ps, j).node;
                uint32_t fb = lay_float_box(LARGS, b, fel, w, -1);
                double mw = t->box[fb].ml + t->box[fb].bl + t->box[fb].pl + t->box[fb].w + t->box[fb].pr + t->box[fb].br + t->box[fb].mr;
                if (mw <= lw - x + 0.01 || x <= indent) {
                    lay_float_place(LARGS, fb, fel, w, ox, oy, y);
                    double l, r, next;
                    lay_band(fl, ox, w, oy + y, bs.lh, &l, &r, &next);
                    lx = l;
                    lw = r - l;
                } else {
                    push_back(later, fb);
                    push_back(later, fel);
                }
                end = j + 1;
                j++;
                continue;
            }
            /* the spaces before the next chunk, and the chunk */
            size_t k = j;
            double sw = 0;
            int breakable = 0;
            while (k < np && at(ps, k).kind == P_SPACE) { sw += at(ps, k).w; if (at(ps, k).brk) breakable = 1; k++; }
            if (k >= np || at(ps, k).kind == P_BREAK) { end = k; j = k; continue; }
            size_t c = k;
            double cw = 0;
            int atomic_edge = at(ps, k).kind == P_ATOMIC || (k > start && at(ps, k - 1).kind == P_ATOMIC);
            while (c < np && at(ps, c).kind != P_SPACE && at(ps, c).kind != P_BREAK && at(ps, c).kind != P_FLOAT) {
                if (c > k && (at(ps, c).kind == P_ATOMIC || at(ps, c - 1).kind == P_ATOMIC)) break;
                cw += at(ps, c).w;
                c++;
            }
            int can_break = (breakable || atomic_edge) && end > start;
            if (!has_fl && x + sw + cw > lw + 0.01 && c == k + 1 && at(ps, k).kind == P_WORD) {
                /* a word too wide for what is left: broken by a hyphen where it may be */
                int hyph = LAY_X(at(ps, k).elem).hyph;
                if (hyph == 1 && !at(ps, k).soft) hyph = 0;
                uint32_t keep, rst;
                double pw;
                if (hyph && lay_hyph_split(LARGS, &ps, k, hyph, lw - x - sw, &keep, &rst, &pw)) {
                    lay_hyph_apply(LARGS, &ps, k, keep, rst, pw);
                    np = len(ps);
                    x += sw + pw;
                    end = k + 1;
                    j = k + 1;
                    break;
                }
            }
            if (can_break && x + sw + cw > lw + 0.01) { j = k; break; }
            x += sw + cw;
            end = c;
            j = c;
        }
        if (j >= np && !forced) end = np;
        int last = j >= np || forced;
        if (has_fl) y += lay_line(LARGS, b, &bs1, &ps, start, end, lx, lw, y, !last, indent, &open);
        else y += lay_line(LARGS, b, &bs, &ps, start, end, lx, lw, y, !last, indent, &open);
        indent = 0;
        i = j;
        /* the floats that waited for the line's end */
        for (size_t q = 0; q + 1 < len(later); q += 2) lay_float_place(LARGS, at(later, q), at(later, q + 1), w, ox, oy, y);
        clear(later);
        if (has_fl) {
            /* past the first line: the pieces as their own style has them */
            for (size_t q = i; q < np; q++) ps[q] = at(rest, q);
            has_fl = 0;
        }
    }
    return y;
}
/* ---- list markers ---- */

static size_t lay_roman(long n, int upper, char *out, size_t cap) {
    static const long vals[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const syms[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    size_t k = 0;
    if (n <= 0 || n >= 4000) return (size_t)snprintf(out, cap, "%ld", n);
    for (int i = 0; i < 13; i++)
        while (n >= vals[i] && k + 3 < cap) {
            for (size_t j = 0; syms[i][j]; j++) out[k++] = upper ? (char)(syms[i][j] - 32) : syms[i][j];
            n -= vals[i];
        }
    out[k] = 0;
    return k;
}

/* the first line in b's box subtree, and its box */
static uint32_t lay_first_line(const layout_tree *t, uint32_t b, uint32_t *owner) {
    if (t->box[b].first_line) { *owner = b; return t->box[b].first_line; }
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        if (t->box[c].kind == LAYOUT_BOX_KIND_INLINE_BLOCK || t->box[c].kind == LAYOUT_BOX_KIND_REPLACED) continue;
        uint32_t ln = lay_first_line(t, c, owner);
        if (ln) return ln;
    }
    return 0;
}

static void lay_marker(LCTX, uint32_t b, uint32_t el, const lay_style *ls) {
    if (ls->list == 0 || (el & LAY_GEN)) return;
    /* the marker's style: its ::marker's, or the list item's */
    lay_style sm = *ls;
    lay_pseudo_style(LARGS, el, CSS_PSEUDO_MARKER, &sm);
    sm.list = ls->list;
    uint32_t owner = 0, ln = lay_first_line(t, b, &owner);
    if (!ln) return;
    char txt[32];
    size_t tn;
    uint32_t cp = 0;
    if (sm.list == 1) cp = 0x2022;
    else if (sm.list == 2) cp = 0x25E6;
    else if (sm.list == 3) cp = 0x25AA;
    if (cp) {
        txt[0] = (char)(0xE0 | cp >> 12);
        txt[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        txt[2] = (char)(0x80 | (cp & 0x3F));
        txt[3] = 0;
        tn = 3;
    } else {
        /* its ordinal: its place among its parent's list items, from the list's start */
        uint32_t parent = d->node[el].parent;
        long ord = lay_attr_int(d, parent, "start", 1);
        for (uint32_t c = d->node[parent].first_child; c && c != el; c = d->node[c].next_sibling)
            if (lay_display_of(LARGS, c) == D_LIST_ITEM) ord++;
        ord = lay_attr_int(d, el, "value", ord);
        char num[24];
        if (sm.list == 4) snprintf(num, sizeof num, "%ld", ord);
        else if (sm.list == 5 || sm.list == 6) {
            long v = ord;
            char rev[16];
            size_t k = 0;
            while (v > 0 && k < sizeof rev) { v--; rev[k++] = (char)((sm.list == 5 ? 'a' : 'A') + v % 26); v /= 26; }
            for (size_t i = 0; i < k; i++) num[i] = rev[k - 1 - i];
            num[k] = 0;
        } else lay_roman(ord, sm.list == 8, num, sizeof num);
        tn = (size_t)snprintf(txt, sizeof txt, "%s.", num);
    }
    /* outside the content, its right end half an em before the list item's content box */
    double mw = amb_text_width(f, sm.face, sm.fs, txt, tn);
    double off_x = 0;
    for (uint32_t x = owner; x && x != b; x = t->box[x].parent) off_x += t->box[x].x;
    amb_span sp = text_add(t, txt, tn);
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    double base = t->line[ln].baseline;
    t->frag[fr].kind = LAYOUT_FRAG_KIND_MARKER;
    t->frag[fr].node = node_handle(d, el);
    t->frag[fr].text = sp;
    t->frag[fr].x = -off_x - t->line[ln].x - mw - sm.fs * 0.5;
    t->frag[fr].y = base - sm.asc;
    t->frag[fr].w = mw;
    t->frag[fr].h = sm.asc + sm.desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (uint8_t)sm.face;
    if (sm.font) t->frag[fr].font = resource_handle(nt, sm.font);
    t->frag[fr].size = sm.fs;
    t->frag[fr].color = sm.color;
    frags_append(t, ln, fr);
}

/* ---- tables ---- */

typedef struct lay_cell { uint32_t el, row, col, span, rspan, box; double mn, mx, h; } lay_cell;

/* the table's rows (in its row groups too) and their cells, each cell's column and span */
static void lay_table_cells(LCTX, uint32_t table, vector<uint32_t> *rows, vector<lay_cell> *cells, vector<uint32_t> *captions, uint32_t *ncols) {
    *ncols = 0;
    vector<uint32_t> busy;            /* each column: the rows a rowspan above still holds it for */
    for (uint32_t c = d->node[table].first_child; c; c = d->node[c].next_sibling) {
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_CAPTION) { push_back(captions, c); continue; }
        uint32_t g0 = dsp == D_ROW_GROUP ? d->node[c].first_child : c;
        for (uint32_t r = g0; r; r = dsp == D_ROW_GROUP ? d->node[r].next_sibling : 0) {
            if (lay_display_of(LARGS, r) != D_ROW) continue;
            uint32_t col = 0, ri = (uint32_t)len(*rows);
            push_back(rows, r);
            for (uint32_t cell = d->node[r].first_child; cell; cell = d->node[cell].next_sibling) {
                int cd = lay_display_of(LARGS, cell);
                if (cd != D_CELL) continue;
                while (col < len(busy) && at(busy, col) > 0) col++;
                lay_cell lc;
                memset(&lc, 0, sizeof lc);
                lc.el = cell;
                lc.row = ri;
                lc.col = col;
                long span = lay_attr_int(d, cell, "colspan", 1);
                lc.span = (uint32_t)(span < 1 ? 1 : span > 100 ? 100 : span);
                long rspan = lay_attr_int(d, cell, "rowspan", 1);
                lc.rspan = (uint32_t)(rspan < 1 ? 1 : rspan > 1000 ? 1000 : rspan);
                if (lc.rspan > 1) {
                    if (len(busy) < col + lc.span) resize(&busy, col + lc.span);
                    for (uint32_t q = col; q < col + lc.span; q++) if (at(busy, q) < lc.rspan) busy[q] = lc.rspan;
                }
                col += lc.span;
                push_back(cells, lc);
            }
            for (size_t q = 0; q < len(busy); q++) if (at(busy, q) > 0) busy[q] = at(busy, q) - 1;
            if (col > *ncols) *ncols = col;
        }
    }
}
/* the columns' minimum and maximum widths, from their cells' */
static void lay_columns(LCTX, vector<lay_cell> *cells, uint32_t ncols, double cw, vector<double> *cmin, vector<double> *cmax) {
    resize(cmin, ncols);
    resize(cmax, ncols);
    for (int pass = 0; pass < 2; pass++)
        for (size_t k = 0; k < len(*cells); k++) {
            lay_cell c = at(*cells, k);
            if ((pass == 0) != (c.span == 1)) continue;
            double mn, mx;
            lay_intrinsic(LARGS, c.el, cw, &mn, &mx);
            (*cells)[k].mn = mn;
            (*cells)[k].mx = mx;
            if (c.span == 1) {
                if (mn > (*cmin)[c.col]) (*cmin)[c.col] = mn;
                if (mx > (*cmax)[c.col]) (*cmax)[c.col] = mx;
                continue;
            }
            /* a spanning cell: what its columns lack, shared out as their widths are (evenly
               among columns of none) */
            double smin = LAY_SPACING * (c.span - 1), smax = smin, wmin = 0, wmax = 0;
            for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) {
                smin += (*cmin)[i];
                smax += (*cmax)[i];
                wmin += (*cmin)[i];
                wmax += (*cmax)[i];
            }
            double lmin = mn - smin, lmax = mx - smax;
            for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) {
                if (lmin > 0) (*cmin)[i] += wmin > 0 ? lmin * (*cmin)[i] / wmin : lmin / c.span;
                if (lmax > 0) (*cmax)[i] += wmax > 0 ? lmax * (*cmax)[i] / wmax : lmax / c.span;
            }
        }
    for (uint32_t i = 0; i < ncols; i++) if ((*cmax)[i] < (*cmin)[i]) (*cmax)[i] = (*cmin)[i];
}

static void lay_table_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) {
    vector<uint32_t> rows, captions;
    vector<lay_cell> cells;
    vector<double> cmin, cmax;
    uint32_t ncols;
    lay_table_cells(LARGS, el, &rows, &cells, &captions, &ncols);
    lay_columns(LARGS, &cells, ncols, cw, &cmin, &cmax);
    *mn = LAY_SPACING * (ncols + 1);
    *mx = *mn;
    for (uint32_t i = 0; i < ncols; i++) { *mn += at(cmin, i); *mx += at(cmax, i); }
}

/* shift a laid-out box's content down by dy: its lines and child boxes */
static void lay_shift_content(layout_tree *t, uint32_t b, double dy) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) t->line[ln].y += dy;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) t->box[c].y += dy;
}

static void lay_table(LCTX, uint32_t b, uint32_t el, const lay_style *s, double cw) {
    vector<uint32_t> rows, captions;
    vector<lay_cell> cells;
    vector<double> cmin, cmax, widths;
    uint32_t ncols;
    lay_table_cells(LARGS, el, &rows, &cells, &captions, &ncols);
    lay_columns(LARGS, &cells, ncols, cw, &cmin, &cmax);
    double smin = 0, smax = 0;
    for (uint32_t i = 0; i < ncols; i++) { smin += at(cmin, i); smax += at(cmax, i); }
    double edges = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    double spacing = LAY_SPACING * (ncols + 1);
    /* the table's width: as declared (its border box), or shrunk to its columns */
    double ml = s->m[3].unit == LAY_AUTO ? 0 : t->box[b].ml, mr = s->m[1].unit == LAY_AUTO ? 0 : t->box[b].mr;
    double bw;
    if (s->width.unit != LAY_AUTO) {
        bw = lay_res(st, s->width, cw);
        t->box[b].how = s->width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
    } else {
        double avail = cw - ml - mr;
        bw = smax + spacing + edges;
        if (bw > avail) bw = avail;
        t->box[b].how = LAYOUT_WIDTH_HOW_SHRINK_TO_FIT;
    }
    if (bw < smin + spacing + edges) bw = smin + spacing + edges;
    double colw = bw - spacing - edges;
    resize(&widths, ncols);
    for (uint32_t i = 0; i < ncols; i++) {
        double v;
        if (smax <= colw) v = at(cmax, i) + (smax > 0 ? (colw - smax) * at(cmax, i) / smax : (colw - smax) / ncols);
        else if (smin >= colw) v = at(cmin, i);
        else v = at(cmin, i) + (smax > smin ? (colw - smin) * (at(cmax, i) - at(cmin, i)) / (smax - smin) : 0);
        widths[i] = v;
    }
    t->box[b].w = bw - edges;
    double rest = cw - bw;
    if (s->m[3].unit == LAY_AUTO && s->m[1].unit == LAY_AUTO && rest > 0) { ml = rest / 2; mr = rest / 2; }
    else if (s->m[3].unit == LAY_AUTO) ml = rest - mr;
    else mr = rest - ml;
    t->box[b].ml = ml;
    t->box[b].mr = mr;
    /* captions above */
    double y = 0;
    for (size_t k = 0; k < len(captions); k++) {
        lay_mc cmt, cmb;
        int empty;
        lay_bk bk;
        bk.ox = 0;
        bk.oy = 0;
        bk.ch = -1;
        bk.fw = -1;
        bk.shrink = 0;
        bk.item = 0;
    bk.item = 0;
        uint32_t cb = lay_block(LARGS, b, at(captions, k), t->box[b].w, 0, 0, 0, 0, &bk, &cmt, &cmb, &empty);
        lay_place(t, cb, t->box[cb].ml, y + lay_mc_v(cmt));
        y += lay_mc_v(cmt) + lay_border_height(t, cb) + lay_mc_v(cmb);
    }
    y += LAY_SPACING;
    /* the rows' boxes, and each cell laid out in its columns */
    size_t nrows = len(rows);
    vector<uint32_t> rboxes;
    vector<double> rh;
    for (size_t r = 0; r < nrows; r++) {
        uint32_t row_el = at(rows, r);
        uint32_t rb = lay_new_box(LARGS, b, row_el, LAYOUT_BOX_KIND_ROW);
        lay_style rs;
        lay_style_of(LARGS, row_el, &rs);
        t->box[rb].background = rs.bg;
        t->box[rb].w = t->box[b].w;
        t->box[rb].how = LAYOUT_WIDTH_HOW_FILL;
        t->box[rb].avail = t->box[b].w;
        push_back(rboxes, rb);
        push_back(rh, rs.height.unit == LAY_PX ? rs.height.v : 0.0);
    }
    for (size_t ci = 0; ci < len(cells); ci++) {
        lay_cell c = at(cells, ci);
        double x = LAY_SPACING;
        for (uint32_t i = 0; i < c.col && i < ncols; i++) x += at(widths, i) + LAY_SPACING;
        double span_w = LAY_SPACING * (c.span - 1);
        for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) span_w += at(widths, i);
        lay_style cs;
        lay_style_of(LARGS, c.el, &cs);
        uint32_t cb = lay_new_box(LARGS, at(rboxes, c.row), c.el, LAYOUT_BOX_KIND_CELL);
        lay_edges(t, st, cb, &cs, t->box[b].w);
        lay_box_style(LARGS, cb, c.el, t->box[b].w, -1);
        t->box[cb].mt = t->box[cb].mr = t->box[cb].mb = t->box[cb].ml = 0;
        t->box[cb].w = span_w - t->box[cb].bl - t->box[cb].br - t->box[cb].pl - t->box[cb].pr;
        if (t->box[cb].w < 0) t->box[cb].w = 0;
        t->box[cb].how = LAYOUT_WIDTH_HOW_COLUMN;
        t->box[cb].avail = span_w;
        lay_mc et, eb;
        int empty;
        lay_flow_children(LARGS, cb, c.el, 1, 1, 0, 0, -1, &et, &eb, &empty);
        if (cs.height.unit == LAY_PX && cs.height.v > t->box[cb].h) t->box[cb].h = cs.height.v;
        lay_place(t, cb, x, 0);
        double bh = lay_border_height(t, cb);
        cells[ci].box = cb;
        cells[ci].h = bh;
        if (c.rspan == 1 && bh > at(rh, c.row)) rh[c.row] = bh;
    }
    /* a cell its rowspan holds: what its rows lack, shared out as their heights are
       (evenly among rows of none) */
    for (size_t ci = 0; ci < len(cells); ci++) {
        lay_cell c = at(cells, ci);
        if (c.rspan < 2) continue;
        size_t lastr = c.row + c.rspan - 1 < nrows ? c.row + c.rspan - 1 : nrows - 1;
        double sum = LAY_SPACING * (double)(lastr - c.row), rows_h = 0;
        for (size_t r = c.row; r <= lastr; r++) { sum += at(rh, r); rows_h += at(rh, r); }
        if (c.h <= sum) continue;
        double lack = c.h - sum;
        for (size_t r = c.row; r <= lastr; r++)
            rh[r] = at(rh, r) + (rows_h > 0 ? lack * at(rh, r) / rows_h : lack / (double)(lastr - c.row + 1));
    }
    /* the rows placed; each cell as tall as its rows, its content where its vertical-align
       puts it: the cell's, else its row's, else its row group's (as the user agent's sheet
       has them inherit), else the middle; the top for baseline */
    vector<double> ry;
    for (size_t r = 0; r < nrows; r++) {
        uint32_t rb = at(rboxes, r);
        t->box[rb].h = at(rh, r);
        t->box[rb].x = 0;
        t->box[rb].y = y;
        push_back(ry, y);
        y += at(rh, r) + LAY_SPACING;
    }
    for (size_t ci = 0; ci < len(cells); ci++) {
        lay_cell c = at(cells, ci);
        uint32_t cb = c.box;
        size_t lastr = c.row + c.rspan - 1 < nrows ? c.row + c.rspan - 1 : nrows - 1;
        double full = at(ry, lastr) + at(rh, lastr) - at(ry, c.row);
        double inner = full - t->box[cb].bt - t->box[cb].pt - t->box[cb].pb - t->box[cb].bb;
        int va = LAY_X(c.el).va;
        uint32_t row_el = at(rows, c.row);
        if (va == VA_BASELINE) va = LAY_X(row_el).va;
        if (va == VA_BASELINE && lay_display_of(LARGS, d->node[row_el].parent) == D_ROW_GROUP) va = LAY_X(d->node[row_el].parent).va;
        if (va == VA_BASELINE) va = VA_MIDDLE;
        double dy = va == VA_BOTTOM ? inner - t->box[cb].h : va == VA_TOP ? 0 : (inner - t->box[cb].h) / 2;
        if (dy > 0) lay_shift_content(t, cb, dy);
        t->box[cb].h = inner;
    }
    t->box[b].h = s->height.unit == LAY_PX && s->height.v > y ? s->height.v : y;
}
/* ---- block layout ---- */

/* the children of box b (element el's), as blocks or as lines, in b's content box of
   width t->box[b].w; sets b's content height. et and eb: the margins that collapsed
   through b's top and bottom; empty: b holds nothing that takes height */
/* the margins that collapse through el's top, as far as can be seen before it is laid
   out: its own, and (no border or padding above it, no formatting context of its own)
   its first in-flow child's -- where floats are placed from, beside its lines */
static lay_mc lay_peek_mt(LCTX, uint32_t el, double cw, int depth) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    lay_mc m = lay_mc_of(s.m[0].unit == LAY_AUTO ? 0 : lay_res(st, s.m[0], cw));
    if (depth > 6 || s.b[0] > 0 || lay_res(st, s.p[0], cw) > 0 || s.bfc || (s.display != D_BLOCK && s.display != D_LIST_ITEM)) return m;
    if (LAY_X(el).clip == 1 || LAY_X(el).lx || LAY_X(el).flt || lay_replaced(d, el)) return m;
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_ABS || sg.block == SEG_FLOAT) continue;
        if (sg.block == SEG_BLOCK) return lay_mc_add(m, lay_peek_mt(LARGS, sg.node, cw, depth + 1));
        return m;
    }
    return m;
}

/* a block-level element that makes a formatting context of its own: it goes beside the
   floats, not under them */
static int lay_makes_bfc(LCTX, uint32_t el) {
    if (lay_replaced(d, el)) return 1;
    uint32_t si = lay_si(sx, gx, el);
    return at(*sc, si).bfc || at(*sc, si).display == D_TABLE || at(*xc, si).clip == 1 || at(*xc, si).lx != LX_FLOW;
}

/* the children of box b (element el's), as blocks or as lines, in b's content box of
   width t->box[b].w, which is at (ox, oy) in the block formatting context; sets b's
   content height. ch: b's definite height (-1: none). et and eb: the margins that
   collapsed through b's top and bottom; empty: b holds nothing that takes height */
static void lay_flow(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, lay_mc *et, lay_mc *eb, int *empty) {
    double w = t->box[b].w;
    et->pos = et->neg = eb->pos = eb->neg = 0;
    *empty = 0;
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    int any_block = 0;
    for (size_t k = 0; k < len(segs); k++) if (at(segs, k).block) any_block = 1;
    if (!any_block) {
        /* a container of inline content only: its lines */
        double h = 0;
        if (len(segs)) h = lay_lines(LARGS, b, el, &items, at(segs, 0).first, at(segs, 0).n, w, ox, oy);
        t->box[b].h = h;
        *empty = !t->box[b].first_line;
        return;
    }
    int through_top = !bfc && t->box[b].bt == 0 && t->box[b].pt == 0;
    int through_bottom = !bfc && height_auto && t->box[b].bb == 0 && t->box[b].pb == 0;
    double cursor = 0;
    lay_mc pending;
    pending.pos = pending.neg = 0;
    int first = 1;
    vector<uint32_t> waiting;          /* the floats met before the next in-flow box: placed at its top */
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_FLOAT) { push_back(waiting, sg.node); continue; }
        if (sg.block == SEG_ABS) {
            /* out of the flow: its static position, where its top margin edge would be */
            lay_abs sa;
            sa.el = sg.node;
            sa.box = b;
            sa.x = 0;
            sa.y = cursor + lay_mc_v(pending);
            push_back(ab, sa);
            continue;
        }
        if (sg.block == SEG_RUN) {
            vector<lay_piece> rps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, w, &rps);
            if (!lay_run_shows(&rps)) {
                lay_run_oof(LARGS, b, &rps, w, ox, oy, cursor + lay_mc_v(pending));
                continue;
            }
        }
        /* where the child's top will be, margins collapsed as far as can be seen before it is
           laid out (through its first children when floats are about) */
        lay_mc cm0;
        cm0.pos = cm0.neg = 0;
        if (sg.block == SEG_BLOCK) {
            if (len(*fl) || len(waiting)) cm0 = lay_peek_mt(LARGS, sg.node, w, 0);
            else {
                lay_style cs;
                lay_style_of(LARGS, sg.node, &cs);
                cm0 = lay_mc_of(cs.m[0].unit == LAY_AUTO ? 0 : lay_res(st, cs.m[0], w));
            }
        }
        double est = (first && through_top) ? 0 : cursor + lay_mc_v(lay_mc_add(pending, cm0));
        /* the floats that waited: placed at the child's top */
        for (size_t q = 0; q < len(waiting); q++) {
            uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
            lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, est);
        }
        clear(waiting);
        /* clearance: below the floats it clears (those just placed too) */
        double clear_to = -1;
        int clr = sg.block == SEG_BLOCK ? LAY_X(sg.node).clr : 0;
        if (clr && len(*fl)) {
            double cy = lay_clear_y(fl, clr) - oy;
            if (cy > est) { est = cy; clear_to = cy; }
        }
        /* a child that makes a formatting context of its own goes beside the floats, narrowed */
        double cx = 0, ccw = w;
        if (sg.block == SEG_BLOCK && len(*fl) && lay_makes_bfc(LARGS, sg.node)) {
            double l, r, next;
            lay_band(fl, ox, w, oy + est, 1, &l, &r, &next);
            cx = l;
            ccw = r - l;
        }
        size_t nf0 = len(*fl);
        lay_mc cmt, cmb;
        int cempty;
        uint32_t cb;
        lay_bk bk;
        bk.ox = ox + cx;
        bk.oy = oy + est;
        bk.ch = ch;
        bk.fw = -1;
        bk.shrink = 0;
        bk.item = 0;
    bk.item = 0;
        if (sg.block == SEG_BLOCK) cb = lay_block(LARGS, b, sg.node, ccw, 0, 0, 0, 0, &bk, &cmt, &cmb, &cempty);
        else cb = lay_block(LARGS, b, 0, w, &items, sg.first, sg.n, el, &bk, &cmt, &cmb, &cempty);
        if (cempty && clear_to < 0) {
            /* an empty box: its margins collapse through it, into one; its top border
               edge where it would be with a bottom border (CSS 2.1 8.3.1) */
            lay_mc both = lay_mc_add(cmt, cmb);
            double at_y = (first && through_top) ? 0 : cursor + lay_mc_v(lay_mc_add(pending, cmt));
            if (first && through_top) *et = lay_mc_add(*et, both);
            else pending = lay_mc_add(pending, both);
            lay_place(t, cb, cx + t->box[cb].ml, at_y);
            if (at_y != est) for (size_t q = nf0; q < len(*fl); q++) (*fl)[q].y = at(*fl, q).y + at_y - est;
            continue;
        }
        double top;
        if (first && through_top && clear_to < 0) { *et = lay_mc_add(*et, cmt); top = 0; }
        else top = cursor + lay_mc_v(lay_mc_add(pending, cmt));
        if (clear_to > top) top = clear_to;
        lay_place(t, cb, cx + t->box[cb].ml, top);
        /* the floats the child placed, where it really is */
        if (top != est) for (size_t q = nf0; q < len(*fl); q++) (*fl)[q].y = at(*fl, q).y + top - est;
        cursor = top + lay_border_height(t, cb);
        pending = cmb;
        first = 0;
    }
    /* floats with no in-flow box after them: after the last */
    for (size_t q = 0; q < len(waiting); q++) {
        uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
        lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, cursor + lay_mc_v(pending));
    }
    if (first) {
        /* every child was empty: b is too, unless it is a new formatting context */
        t->box[b].h = 0;
        *empty = !bfc;
        if (*empty) *et = lay_mc_add(*et, pending);
        return;
    }
    if (through_bottom) { *eb = pending; t->box[b].h = cursor; }
    else t->box[b].h = cursor + lay_mc_v(pending);
}

/* b's children: in the flow b's formatting context is (b's own when bfc: its floats its
   own, its auto height holding them) */
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, lay_mc *et, lay_mc *eb,
                              int *empty) {
    if (!bfc) { lay_flow(LARGS, b, el, 0, height_auto, ox, oy, ch, et, eb, empty); return; }
    vector<lay_flt> bfl;
    lay_flow(LARGS_FL(&bfl), b, el, 1, height_auto, 0, 0, ch, et, eb, empty);
    if (len(bfl)) {
        double bottom = 0;
        for (size_t k = 0; k < len(bfl); k++) if (at(bfl, k).y + at(bfl, k).h > bottom) bottom = at(bfl, k).y + at(bfl, k).h;
        if (height_auto && bottom > t->box[b].h) t->box[b].h = bottom;
        *empty = 0;
    }
}
/* a value kept as text, its cval c's, into the tree's pool: the span paint reads (empty for 0) */
static amb_span lay_kept(layout_tree *t, const css_style *st, uint32_t c) {
    amb_span none;
    memset(&none, 0, sizeof none);
    if (!c) return none;
    vector<char> v;
    amb_span sp = st->cval[c].text;
    append_range(v, text_ptr(st, sp), sp.len);
    return text_add(t, data(v), len(v));
}

/* one radius of text: a percentage as its negative (layout_run takes it of the box's
   final border box: lay_radii), else px */
static double lay_radius_part(const char *s, size_t n, double basis) {
    double x;
    if (n > 1 && s[n - 1] == '%') {
        double p;
        if (css_length_eval(s, n, 100, &p) && p > 0) return -p;
        return 0;
    }
    if (!css_length_eval(s, n, basis, &x) || x < 0) return 0;
    return x;
}
/* a corner's radii: one length (both), or the two of a value kept as text (horizontal,
   vertical). A percentage, of the border box's width across and of its height down, is
   kept as its negative until the box's size is final (lay_radii) */
static void lay_radius(const css_style *st, lay_len l, double bw, double bh, double *h, double *v) {
    if (l.cv && st->cval[l.cv].unit == CSS_UNIT_TEXT) {
        vector<char> tx;
        amb_span sp = st->cval[l.cv].text;
        append_range(tx, text_ptr(st, sp), sp.len);
        size_t i = 0, a0 = 0, n0 = 0, a1 = 0, n1 = 0;
        *h = 0;
        *v = 0;
        if (!css_next_part(data(tx), len(tx), &i, &a0, &n0) || !css_next_part(data(tx), len(tx), &i, &a1, &n1)) return;
        *h = lay_radius_part(data(tx) + a0, n0, bw);
        *v = lay_radius_part(data(tx) + a1, n1, bh);
        return;
    }
    if (l.unit == LAY_PCT) { *h = l.v > 0 ? -l.v : 0; *v = *h; return; }
    *h = lay_res(st, l, bw);
    *v = l.unit == LAY_CALC || l.unit == LAY_EXPR ? lay_res(st, l, bh) : *h;
}

/* the radii kept as percentages taken of each box's final border box */
static void lay_radii(layout_tree *t) {
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live) continue;
        layout_box x = t->box[b];
        if (x.rtl >= 0 && x.rtr >= 0 && x.rbr >= 0 && x.rbl >= 0 && x.rtlv >= 0 && x.rtrv >= 0 && x.rbrv >= 0 && x.rblv >= 0) continue;
        double bw = x.w + x.pl + x.pr + x.bl + x.br, bh = x.h + x.pt + x.pb + x.bt + x.bb;
        if (x.rtl < 0) t->box[b].rtl = -x.rtl * bw / 100;
        if (x.rtr < 0) t->box[b].rtr = -x.rtr * bw / 100;
        if (x.rbr < 0) t->box[b].rbr = -x.rbr * bw / 100;
        if (x.rbl < 0) t->box[b].rbl = -x.rbl * bw / 100;
        if (x.rtlv < 0) t->box[b].rtlv = -x.rtlv * bh / 100;
        if (x.rtrv < 0) t->box[b].rtrv = -x.rtrv * bh / 100;
        if (x.rbrv < 0) t->box[b].rbrv = -x.rbrv * bh / 100;
        if (x.rblv < 0) t->box[b].rblv = -x.rblv * bh / 100;
    }
}

/* what paint reads of a box's style: its radii, opacity, shadows, backgrounds (the image
   layout_run fetched), transform and outline */
static void lay_box_paint(LCTX, uint32_t b, uint32_t si) {
    lay_ext e = at(*xc, si);
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double rh, rv;
    lay_radius(st, e.rad[0], bw, bh, &rh, &rv);
    t->box[b].rtl = rh;
    t->box[b].rtlv = rv;
    lay_radius(st, e.rad[1], bw, bh, &rh, &rv);
    t->box[b].rtr = rh;
    t->box[b].rtrv = rv;
    lay_radius(st, e.rad[2], bw, bh, &rh, &rv);
    t->box[b].rbr = rh;
    t->box[b].rbrv = rv;
    lay_radius(st, e.rad[3], bw, bh, &rh, &rv);
    t->box[b].rbl = rh;
    t->box[b].rblv = rv;
    t->box[b].opacity = e.opacity;
    t->box[b].shadows = lay_kept(t, st, e.shadows);
    t->box[b].bg_image = lay_kept(t, st, e.bgimg);
    t->box[b].bg_pos = lay_kept(t, st, e.bgpos);
    t->box[b].bg_size = lay_kept(t, st, e.bgsize);
    t->box[b].bg_repeat = lay_kept(t, st, e.bgrepeat);
    t->box[b].bg_clip = lay_kept(t, st, e.bgclip);
    t->box[b].bg_origin = lay_kept(t, st, e.bgorigin);
    t->box[b].transform = lay_kept(t, st, e.xform);
    t->box[b].origin = lay_kept(t, st, e.xorigin);
    if (e.bgres) t->box[b].bg_res = resource_handle(nt, e.bgres);
    t->box[b].ow = e.ow;
    t->box[b].ostyle = e.ostyle;
    t->box[b].ocolor = e.ocolor;
    t->box[b].ooffset = e.ooffset;
}

/* what a box's style says of it besides its geometry: how it is positioned (a relative
   one's offset), its stacking and its clipping */
static void lay_box_style(LCTX, uint32_t b, uint32_t el, double cw, double ch) {
    uint32_t si = lay_si(sx, gx, el);
    int pos = at(*xc, si).pos;
    t->box[b].position = pos == POS_RELATIVE ? LAYOUT_POSITION_RELATIVE : pos == POS_ABSOLUTE ? LAYOUT_POSITION_ABSOLUTE :
                         pos == POS_FIXED ? LAYOUT_POSITION_FIXED : pos == POS_STICKY ? LAYOUT_POSITION_STICKY : LAYOUT_POSITION_STATIC;
    t->box[b].z_auto = pos == POS_STATIC || at(*xc, si).zauto;
    t->box[b].z = t->box[b].z_auto ? 0 : at(*xc, si).z;
    t->box[b].clip = at(*xc, si).clip;
    if (at(*xc, si).paints) lay_box_paint(LARGS, b, si);
    if (pos == POS_RELATIVE) {
        double dx, dy;
        lay_ext e = at(*xc, si);
        lay_rel_offset(st, &e, cw, ch, &dx, &dy);
        t->box[b].dx = dx;
        t->box[b].dy = dy;
    }
}

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty) {
    if (!el) {
        /* an anonymous block: the inline run, as lines; no margins of its own */
        uint32_t b = lay_new_box(LARGS, parent, 0, LAYOUT_BOX_KIND_ANONYMOUS);
        t->box[b].w = k->fw >= 0 ? k->fw : cw;
        t->box[b].how = k->fw >= 0 ? LAYOUT_WIDTH_HOW_FLEX : LAYOUT_WIDTH_HOW_FILL;
        t->box[b].avail = cw;
        t->box[b].h = lay_lines(LARGS, b, style_el, items, first, n, t->box[b].w, k->ox, k->oy);
        mt->pos = mt->neg = mb->pos = mb->neg = 0;
        *empty = !t->box[b].first_line;
        return b;
    }
    lay_style s;
    lay_style_of(LARGS, el, &s);
    uint32_t si = lay_si(sx, gx, el);
    int lx = at(*xc, si).lx;
    layout_box_kind kind = s.display == D_LIST_ITEM ? LAYOUT_BOX_KIND_LIST_ITEM : s.display == D_TABLE ? LAYOUT_BOX_KIND_TABLE :
                           lx == LX_FLEX ? LAYOUT_BOX_KIND_FLEX : lx == LX_GRID ? LAYOUT_BOX_KIND_GRID : LAYOUT_BOX_KIND_BLOCK;
    uint32_t b = lay_new_box(LARGS, parent, el, kind);
    lay_edges(t, st, b, &s, cw);
    lay_box_style(LARGS, b, el, cw, k->ch);
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    if (s.display == D_TABLE) {
        lay_table(LARGS, b, el, &s, cw);
        *mt = lay_mc_of(t->box[b].mt);
        *mb = lay_mc_of(t->box[b].mb);
        *empty = 0;
        return b;
    }
    double edges_h = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    if (k->fw >= 0) {
        /* a width it is given (a flex item's, a grid item's, a positioned box's): its border box's */
        t->box[b].w = k->fw - edges_h > 0 ? k->fw - edges_h : 0;
        t->box[b].how = LAYOUT_WIDTH_HOW_FLEX;
    } else {
        if (k->shrink && s.width.unit == LAY_AUTO) s.width.unit = LAY_FITC;
        int content_sized = lay_content_sized(s.width);
        if (content_sized) {
            double mn, mx;
            lay_intrinsic(LARGS, el, cw, &mn, &mx);
            double edges = lay_edges_px(st, &s, cw), box = s.b[1] + s.b[3] + lay_res(st, s.p[1], cw) + lay_res(st, s.p[3], cw);
            double avail = cw - edges, v = avail > mn - edges ? avail : mn - edges;
            double w = s.width.unit == LAY_MINC ? mn - edges : s.width.unit == LAY_MAXC ? mx - edges : v < mx - edges ? v : mx - edges;
            s.width.unit = LAY_PX;
            s.width.v = (w < 0 ? 0 : w) + (s.border_box ? box : 0);
        }
        lay_len minw = at(*xc, si).minw;
        lay_block_width(t, st, b, &s, cw, &minw);
        if (content_sized && t->box[b].how == LAYOUT_WIDTH_HOW_DECLARED) t->box[b].how = LAYOUT_WIDTH_HOW_SHRINK_TO_FIT;
    }
    if (k->fw >= 0 || k->shrink || k->item || at(*xc, si).flt) {
        /* a float's, a positioned box's, an item's: its margins as they are, auto ones 0 */
        t->box[b].ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw);
        t->box[b].mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw);
    }
    int root = !(el & LAY_GEN) && d->node[el].parent == 1;
    int pos = at(*xc, si).pos;
    int bfc = root || s.bfc || s.display == D_CELL || s.display == D_CAPTION || at(*xc, si).clip == 1 || lx != LX_FLOW ||
              at(*xc, si).flt || pos == POS_ABSOLUTE || pos == POS_FIXED || k->item;
    /* its definite height, if it has one: declared in px, or a percentage of a definite
       containing block's (the root's: the viewport's) */
    double vedges = t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb;
    double defh = s.height.unit == LAY_PX ? s.height.v : -1;
    lay_len hraw = at(*xc, si).hraw;
    if (defh < 0 && hraw.unit != LAY_AUTO && !lay_content_sized(hraw) && k->ch >= 0) {
        defh = lay_res(st, hraw, k->ch) - (s.border_box ? vedges : 0);
        if (defh < 0) defh = 0;
    }
    double minh = 0, maxh = -1;
    lay_len mnl = at(*xc, si).minh, mxl = at(*xc, si).maxh;
    if (mnl.unit != LAY_AUTO && !lay_content_sized(mnl) && (mnl.unit == LAY_PX || k->ch >= 0)) minh = lay_res(st, mnl, k->ch) - (s.border_box ? vedges : 0);
    if (mxl.unit != LAY_AUTO && !lay_content_sized(mxl) && (mxl.unit == LAY_PX || k->ch >= 0)) maxh = lay_res(st, mxl, k->ch) - (s.border_box ? vedges : 0);
    if (defh >= 0 && maxh >= 0 && defh > maxh) defh = maxh;
    if (defh >= 0 && defh < minh) defh = minh;
    lay_mc et, eb;
    double cox = k->ox + t->box[b].ml + t->box[b].bl + t->box[b].pl, coy = k->oy + t->box[b].bt + t->box[b].pt;
    if (lx == LX_FLEX) { lay_flex(LARGS, b, el, defh); et.pos = et.neg = eb.pos = eb.neg = 0; *empty = 0; }
    else if (lx == LX_GRID) { lay_grid(LARGS, b, el, defh); et.pos = et.neg = eb.pos = eb.neg = 0; *empty = 0; }
    else lay_flow_children(LARGS, b, el, bfc, defh < 0, cox, coy, defh, &et, &eb, empty);
    if (defh >= 0) {
        t->box[b].h = defh;
        eb.pos = eb.neg = 0;
        *empty = 0;
    } else {
        if (maxh >= 0 && t->box[b].h > maxh) t->box[b].h = maxh;
        if (t->box[b].h < minh) { t->box[b].h = minh; eb.pos = eb.neg = 0; *empty = 0; }
    }
    if (t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb > 0) *empty = 0;
    *mt = lay_mc_add(lay_mc_of(t->box[b].mt), et);
    *mb = lay_mc_add(lay_mc_of(t->box[b].mb), eb);
    if (s.display == D_LIST_ITEM) lay_marker(LARGS, b, el, &s);
    return b;
}
/* ---- generated content: ::before's and ::after's text (CSS 2.1, 12) ---- */

/* a counter in scope as the walk has it: its name (hashed) and its value */
typedef struct lay_ctr { uint64_t name; long v; } lay_ctr;

static uint64_t lay_hash(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    return h;
}
static int lay_is_word(const char *s, size_t n, const char *w) { return n == strlen(w) && memcmp(s, w, n) == 0; }
/* s[0, n) is a call of the function fn: its arguments' text from *a, *an bytes */
static int lay_call(const char *s, size_t n, const char *fn, size_t *a, size_t *an) {
    size_t k = strlen(fn);
    if (n < k + 2 || memcmp(s, fn, k) != 0 || s[k] != '(' || s[n - 1] != ')') return 0;
    *a = k + 1;
    *an = n - k - 2;
    return 1;
}
/* the next comma-separated argument of s[*i, n), its white space trimmed */
static int lay_arg(const char *s, size_t n, size_t *i, size_t *a, size_t *an) {
    size_t p = *i;
    while (p < n && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n')) p++;
    if (p >= n) return 0;
    size_t b = p;
    char q = 0;
    while (p < n && (q || s[p] != ',')) {
        if (q && s[p] == '\\') p++;
        else if (q && s[p] == q) q = 0;
        else if (!q && (s[p] == '"' || s[p] == '\'')) q = s[p];
        p++;
    }
    size_t e = p < n ? p : n;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n')) e--;
    *a = b;
    *an = e - b;
    *i = p < n ? p + 1 : n;
    return 1;
}
/* a CSS string's text (s[0, n), its quotes included), its escapes made characters, into out */
static void lay_unquote(const char *s, size_t n, vector<char> *out) {
    if (n < 2) return;
    for (size_t i = 1; i + 1 < n; i++) {
        if (s[i] != '\\') { push_back(out, s[i]); continue; }
        i++;
        if (i + 1 >= n) break;
        if (s[i] == '\n') continue;
        uint32_t cp = 0;
        size_t k = 0;
        while (k < 6 && i + 1 < n) {
            char c = s[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) break;
            cp = cp * 16 + (uint32_t)v;
            i++;
            k++;
        }
        if (k == 0) { push_back(out, s[i]); continue; }
        if (i + 1 < n && s[i] == ' ') i++;
        i--;
        if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
        if (cp < 0x80) push_back(out, (char)cp);
        else if (cp < 0x800) { push_back(out, (char)(0xC0 | cp >> 6)); push_back(out, (char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            push_back(out, (char)(0xE0 | cp >> 12));
            push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (cp & 0x3F)));
        } else {
            push_back(out, (char)(0xF0 | cp >> 18));
            push_back(out, (char)(0x80 | (cp >> 12 & 0x3F)));
            push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (cp & 0x3F)));
        }
    }
}
/* v as the counter style named s[0, n) writes it (decimal for one not known), into out */
static void lay_counter_style(long v, const char *s, size_t n, vector<char> *out) {
    char num[48];
    size_t k = 0;
    if (lay_is_word(s, n, "none")) return;
    if (lay_is_word(s, n, "disc")) { append_range(out, "\xe2\x80\xa2", 3); return; }
    if (lay_is_word(s, n, "circle")) { append_range(out, "\xe2\x97\xa6", 3); return; }
    if (lay_is_word(s, n, "square")) { append_range(out, "\xe2\x96\xaa", 3); return; }
    int upper = lay_is_word(s, n, "upper-roman") || lay_is_word(s, n, "upper-alpha") || lay_is_word(s, n, "upper-latin");
    if ((lay_is_word(s, n, "lower-roman") || lay_is_word(s, n, "upper-roman")) && v > 0 && v < 4000) k = lay_roman(v, upper, num, sizeof num);
    else if ((lay_is_word(s, n, "lower-alpha") || lay_is_word(s, n, "lower-latin") || lay_is_word(s, n, "upper-alpha") ||
              lay_is_word(s, n, "upper-latin")) && v > 0) {
        char rev[16];
        size_t r = 0;
        long x = v;
        while (x > 0 && r < sizeof rev) { x--; rev[r++] = (char)((upper ? 'A' : 'a') + x % 26); x /= 26; }
        for (size_t i = 0; i < r; i++) num[i] = rev[r - 1 - i];
        k = r;
    } else if (lay_is_word(s, n, "decimal-leading-zero") && v >= 0 && v < 10) k = (size_t)snprintf(num, sizeof num, "0%ld", v);
    else k = (size_t)snprintf(num, sizeof num, "%ld", v);
    if (k > sizeof num - 1) k = sizeof num - 1;
    append_range(out, num, k);
}
/* the quote of the quotes property's value qc at depth (an open one, or a close one) into
   out: auto's are English's, none's none */
static void lay_quote(const css_style *st, uint32_t qc, int depth, int close, vector<char> *out) {
    if (st->cval[qc].unit != CSS_UNIT_TEXT) {
        if (names_is(st, st->cval[qc].word, "none", 4)) return;
        if (depth == 0 && close) append_range(out, "\xe2\x80\x9d", 3);
        else if (depth == 0) append_range(out, "\xe2\x80\x9c", 3);
        else if (close) append_range(out, "\xe2\x80\x99", 3);
        else append_range(out, "\xe2\x80\x98", 3);
        return;
    }
    vector<char> src;
    amb_span sp = st->cval[qc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n, k = 0, pa = 0, pn = 0, total = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) total++;
    size_t pairs = total / 2;
    if (pairs == 0) return;
    size_t want = 2 * ((size_t)depth < pairs ? (size_t)depth : pairs - 1) + (close ? 1 : 0);
    i = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        if (k == want) { pa = a; pn = n; }
        k++;
    }
    if (pn) lay_unquote(data(src) + pa, pn, out);
}

/* an element's counter-reset and counter-increment (its values from c0): a reset makes a
   counter of this level (from mark in ctr), or sets one an earlier sibling made; an
   increment adds to the innermost counter of its name, or makes one */
static void lay_counters(const css_style *st, uint32_t c0, vector<lay_ctr> *ctr, size_t mark) {
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = c0 + (pass == 0 ? CSS_PROP_COUNTER_RESET : CSS_PROP_COUNTER_INCREMENT);
        if (st->cval[c].unit != CSS_UNIT_TEXT) continue;
        vector<char> src;
        amb_span sp = st->cval[c].text;
        append_range(src, text_ptr(st, sp), sp.len);
        size_t i = 0, a, n;
        uint64_t name = 0;
        int have = 0;
        while (css_next_part(data(src), len(src), &i, &a, &n)) {
            if (!have) { name = lay_hash(data(src) + a, n); have = 1; continue; }
            have = 0;
            long v = 0;
            int neg = 0;
            for (size_t q = a; q < a + n; q++) {
                char ch = data(src)[q];
                if (ch == '-') neg = 1;
                else if (ch >= '0' && ch <= '9' && v < 100000000) v = v * 10 + (ch - '0');
            }
            if (neg) v = -v;
            size_t q = len(*ctr), lo = pass == 0 ? mark : 0;
            int found = 0;
            while (q > lo && !found) {
                q--;
                if (at(*ctr, q).name == name) {
                    if (pass == 0) (*ctr)[q].v = v;
                    else (*ctr)[q].v += v;
                    found = 1;
                }
            }
            if (!found) {
                lay_ctr nc;
                nc.name = name;
                nc.v = v;
                push_back(ctr, nc);
            }
        }
    }
}

/* the text content's value (cc) gives element el's pseudo-element (its quotes qc), the
   counters ctr in scope, the quotes' depth *depth: strings, attr(), counter(), counters(),
   the quotes; an image's url() gives none */
static void lay_content(const dom_document *d, const css_style *st, uint32_t el, uint32_t cc, uint32_t qc, const vector<lay_ctr> *ctr, int *depth,
                        vector<char> *out) {
    if (st->cval[cc].unit != CSS_UNIT_TEXT) return;
    vector<char> src;
    amb_span sp = st->cval[cc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n, fa, fn;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        const char *p = data(src) + a;
        if (p[0] == '"' || p[0] == '\'') { lay_unquote(p, n, out); continue; }
        if (lay_is_word(p, n, "open-quote")) { lay_quote(st, qc, *depth, 0, out); (*depth)++; continue; }
        if (lay_is_word(p, n, "close-quote")) { if (*depth > 0) { (*depth)--; lay_quote(st, qc, *depth, 1, out); } continue; }
        if (lay_is_word(p, n, "no-open-quote")) { (*depth)++; continue; }
        if (lay_is_word(p, n, "no-close-quote")) { if (*depth > 0) (*depth)--; continue; }
        if (lay_call(p, n, "attr", &fa, &fn)) {
            size_t j = 0, xa, xn;
            if (!lay_arg(p + fa, fn, &j, &xa, &xn)) continue;
            uint32_t nm = names_find(d, p + fa + xa, xn);
            if (!nm) continue;
            for (uint32_t r = d->node[el].first_attr; r; r = d->attr[r].next)
                if (d->attr[r].name == nm) { amb_span v = d->attr[r].value; append_range(out, text_ptr(d, v), v.len); }
            continue;
        }
        int one = lay_call(p, n, "counter", &fa, &fn);
        int many = !one && lay_call(p, n, "counters", &fa, &fn);
        if (one || many) {
            size_t j = 0, na, nn, sa = 0, sn = 0, ya = 0, yn = 0;
            if (!lay_arg(p + fa, fn, &j, &na, &nn)) continue;
            if (many && !lay_arg(p + fa, fn, &j, &sa, &sn)) continue;
            int styled = lay_arg(p + fa, fn, &j, &ya, &yn);
            uint64_t name = lay_hash(p + fa + na, nn);
            vector<char> sty;
            if (styled) append_range(sty, p + fa + ya, yn);
            else append_range(sty, "decimal", 7);
            int any = 0;
            for (size_t q = 0; q < len(*ctr); q++) {
                if (at(*ctr, q).name != name) continue;
                if (!many) continue;
                if (any) lay_unquote(p + fa + sa, sn, out);
                lay_counter_style(at(*ctr, q).v, data(sty), len(sty), out);
                any = 1;
            }
            if (!many) {
                long v = 0;
                for (size_t q = len(*ctr); q > 0; q--) if (at(*ctr, q - 1).name == name) { v = at(*ctr, q - 1).v; break; }
                lay_counter_style(v, data(sty), len(sty), out);
            } else if (!any) lay_counter_style(0, data(sty), len(sty), out);
            continue;
        }
    }
}

/* a pseudo-element's counters and its text, as the walk reaches it */
static void lay_gen_content(layout_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sf, const vector<lay_style> *sc,
                            vector<lay_gen> *gx, uint32_t x, uint32_t pk, vector<lay_ctr> *ctr, size_t mark, int *depth) {
    int64_t g = lay_gen_find(gx, x, pk);
    if (g < 0) return;
    uint32_t si = at(*gx, (size_t)g).style;
    if (at(*sc, si).display == D_NONE) return;
    uint32_t c0 = at(*sf, si);
    lay_counters(st, c0, ctr, mark);
    vector<char> out;
    lay_content(d, st, x, c0 + CSS_PROP_CONTENT, c0 + CSS_PROP_QUOTES, ctr, depth, &out);
    amb_span sp = text_add(t, data(out), len(out));
    (*gx)[(size_t)g].text = sp;
}

/* the walk that gives each ::before and ::after its text, in tree order, which counters
   and quotes follow: x's ::before, its children (each displayed one's counters, then its
   own walk), its ::after; the counters x's children made go out of scope at its end */
static void lay_gen_walk(layout_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sx, const vector<uint32_t> *sf,
                         const vector<lay_style> *sc, vector<lay_gen> *gx, uint32_t x, vector<lay_ctr> *ctr, int *depth, int level) {
    size_t mark = len(*ctr);
    uint32_t fl = x < len(*sx) ? at(*sx, x) : 0;
    if (fl & LAY_HAS_BEFORE) lay_gen_content(t, d, st, sf, sc, gx, x, CSS_PSEUDO_BEFORE, ctr, mark, depth);
    if (level < 1024)
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
            if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || c >= len(*sx)) continue;
            uint32_t si = at(*sx, c) & LAY_SMASK;
            if (!si || at(*sc, si).display == D_NONE) continue;
            lay_counters(st, at(*sf, si), ctr, mark);
            lay_gen_walk(t, d, st, sx, sf, sc, gx, c, ctr, depth, level + 1);
        }
    if (fl & LAY_HAS_AFTER) lay_gen_content(t, d, st, sf, sc, gx, x, CSS_PSEUDO_AFTER, ctr, mark, depth);
    resize(ctr, mark);
}

/* ---- flex containers (CSS Flexbox 1, 9) ---- */

/* a flex item: an element (or a pseudo-element), or an anonymous one of a run of text
   (el 0: the run items[first, first + n)); its order and its place; its flex base size,
   hypothetical main size and min and max main sizes, its final main size and its cross
   size (border boxes); its margins along each axis; its box */
typedef struct lay_fi {
    uint32_t el, first, n, idx, box;
    int32_t order;
    double base, hypo, mn, mx, size, cross, grow, shrink, mm0, mm1, cm0, cm1;
    int frozen, auto_m0, auto_m1, auto_c0, auto_c1, align, cross_auto;
} lay_fi;

/* the items of a flex or grid container el, in order (order, then the tree's); its
   positioned children met, their static position its content box's corner */
static void lay_items(LCTX, uint32_t b, uint32_t el, vector<lay_fi> *its, vector<uint32_t> *items) {
    vector<lay_seg> segs;
    lay_collect(LARGS, el, &segs, items);
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_ABS) {
            lay_abs sa;
            sa.el = sg.node;
            sa.box = b;
            sa.x = 0;
            sa.y = 0;
            push_back(ab, sa);
            continue;
        }
        lay_fi it;
        memset(&it, 0, sizeof it);
        it.idx = (uint32_t)k;
        if (sg.block == SEG_RUN) {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, items, sg.first, sg.n, t->box[b].w, &ps);
            if (!lay_run_shows(&ps)) { lay_run_oof(LARGS, b, &ps, t->box[b].w, 0, 0, 0); continue; }
            it.first = sg.first;
            it.n = sg.n;
            it.shrink = 1;
        } else {
            it.el = sg.node;
            it.order = LAY_X(sg.node).order;
            it.grow = LAY_X(sg.node).grow;
            it.shrink = LAY_X(sg.node).shrink;
        }
        /* in order: order, then the tree's */
        push_back(its, it);
        size_t q = len(*its) - 1;
        while (q > 0 && at(*its, q - 1).order > it.order) { (*its)[q] = at(*its, q - 1); q--; }
        (*its)[q] = it;
    }
}

/* an item's max- and min-content widths (its margin box's): an anonymous one's text's */
static void lay_item_intrinsic(LCTX, uint32_t el, const lay_fi *it, const vector<uint32_t> *items, double cw, double *mn, double *mx) {
    if (it->el) { lay_intrinsic(LARGS, it->el, cw, mn, mx); return; }
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, el, items, it->first, it->n, cw, &ps);
    lay_run_intrinsic(&ps, mn, mx);
}

/* an item laid out as a child of b: fw its border box's width (-1: shrunk to fit in cw),
   ch the container's definite height; its box */
static uint32_t lay_item_box(LCTX, uint32_t b, uint32_t el, const lay_fi *it, const vector<uint32_t> *items, double cw, double fw, double ch) {
    lay_bk bk;
    bk.ox = 0;
    bk.oy = 0;
    bk.ch = ch;
    bk.fw = fw;
    bk.shrink = fw < 0;
    bk.item = 1;
    lay_mc m1, m2;
    int empty;
    vector<lay_flt> nf;
    if (it->el) return lay_block(LARGS_FL(&nf), b, it->el, cw, 0, 0, 0, 0, &bk, &m1, &m2, &empty);
    if (fw < 0) {
        double mn, mx;
        lay_item_intrinsic(LARGS, el, it, items, cw, &mn, &mx);
        bk.fw = mx < cw ? mx : cw;
        if (bk.fw < mn) bk.fw = mn;
    }
    return lay_block(LARGS_FL(&nf), b, 0, cw, items, it->first, it->n, el, &bk, &m1, &m2, &empty);
}

static double lay_bw(const layout_tree *t, uint32_t b) { return t->box[b].bl + t->box[b].pl + t->box[b].w + t->box[b].pr + t->box[b].br; }

/* where free space puts the first of n things and the space between them, by a
   justify-content or align-content keyword */
static void lay_distribute(int how, double free, size_t n, double *start, double *between) {
    *start = 0;
    *between = 0;
    if (how == AJ_END) *start = free;
    else if (how == AJ_CENTER) *start = free / 2;
    else if (how == AJ_BETWEEN && n > 1 && free > 0) *between = free / (double)(n - 1);
    else if (how == AJ_AROUND && n > 0 && free > 0) { *between = free / (double)n; *start = *between / 2; }
    else if (how == AJ_EVENLY && n > 0 && free > 0) { *between = free / (double)(n + 1); *start = *between; }
    else if ((how == AJ_BETWEEN || how == AJ_AROUND || how == AJ_EVENLY) && free < 0 && how != AJ_BETWEEN) *start = free / 2;
}

/* a flex container's items laid out (CSS Flexbox 1, 9): their base sizes, lines, the
   flexible lengths resolved, their cross sizes, justify-content and the alignments; b's
   content height when defh (its definite one) is -1 */
static void lay_flex(LCTX, uint32_t b, uint32_t el, double defh) {
    lay_ext e = LAY_X(el);
    double W = t->box[b].w;
    int row = e.frow;
    vector<lay_fi> its;
    vector<uint32_t> items;
    lay_items(LARGS, b, el, &its, &items);
    double main_avail = row ? W : defh;
    double gap_main = lay_res(st, row ? e.cgap : e.rgap, row ? W : (defh >= 0 ? defh : 0));
    double gap_cross = lay_res(st, row ? e.rgap : e.cgap, row ? (defh >= 0 ? defh : 0) : W);
    size_t ni = len(its);
    /* each item's base size, its min and max, its hypothetical main size */
    for (size_t q = 0; q < ni; q++) {
        lay_fi it = at(its, q);
        lay_style is;
        if (it.el) lay_style_of(LARGS, it.el, &is);
        else { memset(&is, 0, sizeof is); is.width.unit = LAY_AUTO; is.height.unit = LAY_AUTO; is.maxw.unit = LAY_AUTO; for (int k = 0; k < 4; k++) { is.m[k].unit = LAY_PX; is.p[k].unit = LAY_PX; } }
        lay_ext ie;
        if (it.el) ie = LAY_X(it.el);
        else lay_ext_make(st, 0, &ie);
        /* margins: main axis start and end, cross start and end */
        int m0 = row ? 3 : 0, m1 = row ? 1 : 2, c0 = row ? 0 : 3, c1 = row ? 2 : 1;
        it.auto_m0 = is.m[m0].unit == LAY_AUTO;
        it.auto_m1 = is.m[m1].unit == LAY_AUTO;
        it.auto_c0 = is.m[c0].unit == LAY_AUTO;
        it.auto_c1 = is.m[c1].unit == LAY_AUTO;
        it.mm0 = it.auto_m0 ? 0 : lay_res(st, is.m[m0], W);
        it.mm1 = it.auto_m1 ? 0 : lay_res(st, is.m[m1], W);
        it.cm0 = it.auto_c0 ? 0 : lay_res(st, is.m[c0], W);
        it.cm1 = it.auto_c1 ? 0 : lay_res(st, is.m[c1], W);
        double eh = is.b[1] + is.b[3] + lay_res(st, is.p[1], W) + lay_res(st, is.p[3], W);
        double ev = is.b[0] + is.b[2] + lay_res(st, is.p[0], W) + lay_res(st, is.p[2], W);
        int al = ie.as == AJ_AUTO || ie.as == AJ_NORMAL ? e.ai : ie.as;
        it.align = al == AJ_NORMAL ? AJ_STRETCH : al;
        it.cross_auto = row ? (is.height.unit == LAY_AUTO && ie.hraw.unit == LAY_AUTO) : is.width.unit == LAY_AUTO;
        double mn, mx;
        double base = -1;
        lay_len bs = ie.basis;
        if (bs.unit != LAY_AUTO && !lay_content_sized(bs) && (bs.unit == LAY_PX || main_avail >= 0))
            base = lay_res(st, bs, main_avail) + (is.border_box ? 0 : (row ? eh : ev));
        else if (bs.unit == LAY_AUTO && row && is.width.unit != LAY_AUTO && !lay_content_sized(is.width))
            base = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
        else if (bs.unit == LAY_AUTO && !row && is.height.unit == LAY_PX)
            base = is.height.v + ev;
        double content_min = 0;
        if (row) {
            lay_item_intrinsic(LARGS, el, &it, &items, W, &mn, &mx);
            if (base < 0) base = mx - it.mm0 - it.mm1;
            content_min = mn - it.mm0 - it.mm1;
            if (it.el && is.width.unit == LAY_PX) {
                /* the content's min-content width, not its declared width's */
                double cmn, cmx;
                lay_intrinsic_of(LARGS, it.el, W, &cmn, &cmx, 0);
                content_min = cmn - it.mm0 - it.mm1;
            }
        } else {
            /* a column's item: laid out now at its cross size, its height its content's */
            double fw = -1;
            if (it.align == AJ_STRETCH && it.cross_auto && !it.auto_c0 && !it.auto_c1) fw = W - it.cm0 - it.cm1;
            else if (!it.cross_auto && it.el) fw = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
            it.box = lay_item_box(LARGS, b, el, &it, &items, W - it.cm0 - it.cm1, fw, defh);
            double content_h = lay_border_height(t, it.box);
            if (base < 0) base = content_h;
            content_min = content_h;
            it.cross = lay_bw(t, it.box);
        }
        /* min and max main sizes; an auto min the content's (overflow visible) */
        lay_len mnl = row ? ie.minw : ie.minh, mxl = row ? is.maxw : ie.maxh;
        double pad = row ? eh : ev;
        if (mnl.unit == LAY_AUTO) it.mn = ie.clip ? 0 : (content_min < base || bs.unit != LAY_AUTO ? content_min : base);
        else it.mn = lay_res(st, mnl, row ? W : (defh >= 0 ? defh : 0)) + (is.border_box ? 0 : pad);
        if (mnl.unit == LAY_AUTO && it.el && row && is.width.unit != LAY_AUTO && !lay_content_sized(is.width)) {
            double spec = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
            if (spec < it.mn) it.mn = spec;
        }
        it.mx = 1e9;
        if (mxl.unit != LAY_AUTO && !lay_content_sized(mxl) && (mxl.unit == LAY_PX || main_avail >= 0))
            it.mx = lay_res(st, mxl, main_avail) + (is.border_box ? 0 : pad);
        if (it.mn > it.mx) it.mx = it.mn;
        it.base = base;
        it.hypo = base < it.mn ? it.mn : base > it.mx ? it.mx : base;
        its[q] = it;
    }
    /* the lines: one, or as many as the main size holds with wrap */
    vector<uint32_t> lstart;          /* each line's first item, then the end */
    push_back(lstart, 0);
    if (e.fwrap && main_avail >= 0) {
        double used = 0;
        for (size_t q = 0; q < ni; q++) {
            double o = at(its, q).hypo + at(its, q).mm0 + at(its, q).mm1;
            if (q > back(lstart) && used + gap_main + o > main_avail + 0.01) { push_back(lstart, (uint32_t)q); used = o; }
            else used += (q > back(lstart) ? gap_main : 0) + o;
        }
    }
    push_back(lstart, (uint32_t)ni);
    size_t nl = len(lstart) - 1;
    /* each line's flexible lengths (9.7) */
    for (size_t l = 0; l < nl; l++) {
        size_t a = at(lstart, l), z = at(lstart, l + 1);
        double outer = 0;
        for (size_t q = a; q < z; q++) outer += at(its, q).hypo + at(its, q).mm0 + at(its, q).mm1 + (q > a ? gap_main : 0);
        if (main_avail < 0) {
            for (size_t q = a; q < z; q++) its[q].size = at(its, q).hypo;
            continue;
        }
        int grow = outer < main_avail;
        for (size_t q = a; q < z; q++) {
            lay_fi it = at(its, q);
            it.frozen = (grow ? it.grow <= 0 : it.shrink <= 0) || (grow && it.base > it.hypo) || (!grow && it.base < it.hypo);
            it.size = it.hypo;
            its[q] = it;
        }
        double initial_free = 0;
        int first_round = 1;
        for (int round = 0; round < 64; round++) {
            double used = 0, factors = 0;
            int open_items = 0;
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                used += (it.frozen ? it.size : it.base) + it.mm0 + it.mm1 + (q > a ? gap_main : 0);
                if (!it.frozen) { open_items = 1; factors += grow ? it.grow : it.shrink * it.base; }
            }
            if (!open_items) break;
            double free = main_avail - used;
            if (first_round) { initial_free = free; first_round = 0; }
            if (grow) {
                double sum_grow = 0;
                for (size_t q = a; q < z; q++) if (!at(its, q).frozen) sum_grow += at(its, q).grow;
                if (sum_grow < 1 && initial_free * sum_grow < free) free = initial_free * sum_grow;
            }
            double violation = 0;
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                if (it.frozen) continue;
                double target = it.base;
                if (factors > 0) target = it.base + free * (grow ? it.grow : it.shrink * it.base) / factors;
                double clamped = target < it.mn ? it.mn : target > it.mx ? it.mx : target;
                if (clamped < 0) clamped = 0;
                violation += clamped - target;
                it.size = clamped;
                its[q] = it;
            }
            /* freeze: all when nothing was clamped; else those clamped the way the total went */
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                if (it.frozen) continue;
                double target = it.base;
                if (factors > 0) target = it.base + free * (grow ? it.grow : it.shrink * it.base) / factors;
                if (violation == 0 || (violation > 0 && it.size > target) || (violation < 0 && it.size < target)) it.frozen = 1;
                its[q] = it;
            }
        }
    }
    /* the items laid out at their main sizes (a row's: their widths); cross sizes */
    for (size_t q = 0; q < ni; q++) {
        lay_fi it = at(its, q);
        if (row) {
            it.box = lay_item_box(LARGS, b, el, &it, &items, W, it.size, defh);
            it.cross = lay_border_height(t, it.box);
        } else {
            /* a column's item: its height the main size it was given */
            double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
            t->box[it.box].h = it.size - ve > 0 ? it.size - ve : 0;
        }
        its[q] = it;
    }
    /* each line's cross size: its tallest item's (the container's, a single line's when definite) */
    vector<double> lcross, lpos;
    double cross_total = 0;
    double cross_avail = row ? defh : W;
    for (size_t l = 0; l < nl; l++) {
        double c = 0;
        for (size_t q = at(lstart, l); q < at(lstart, l + 1); q++) {
            double o = at(its, q).cross + at(its, q).cm0 + at(its, q).cm1;
            if (o > c) c = o;
        }
        if (nl == 1 && !e.fwrap && cross_avail >= 0) c = cross_avail;
        push_back(lcross, c);
        cross_total += c + (l > 0 ? gap_cross : 0);
    }
    /* align-content: the lines in the container's cross size, when it has one */
    double lstart_off = 0, lbetween = 0;
    if (cross_avail >= 0 && (e.fwrap || nl > 1)) {
        double free = cross_avail - cross_total;
        int ac = e.ac == AJ_NORMAL ? AJ_STRETCH : e.ac;
        if (ac == AJ_STRETCH && free > 0) {
            for (size_t l = 0; l < nl; l++) lcross[l] = at(lcross, l) + free / (double)nl;
            cross_total = cross_avail;
        } else lay_distribute(ac, free, nl, &lstart_off, &lbetween);
    }
    double cy = lstart_off;
    for (size_t l = 0; l < nl; l++) {
        push_back(lpos, cy);
        cy += at(lcross, l) + gap_cross + lbetween;
    }
    if (e.fwrap == 2) {
        /* wrap-reverse: the lines from the cross end */
        double total = cy - gap_cross - lbetween;
        double span = cross_avail >= 0 ? cross_avail : total;
        for (size_t l = 0; l < nl; l++) lpos[l] = span - at(lpos, l) - at(lcross, l);
    }
    /* main positions: auto margins, else justify-content; cross positions: align-self */
    double main_size = main_avail;
    if (main_size < 0) {
        main_size = 0;
        for (size_t q = 0; q < ni; q++) main_size += at(its, q).size + at(its, q).mm0 + at(its, q).mm1 + (q > 0 ? gap_main : 0);
    }
    for (size_t l = 0; l < nl; l++) {
        size_t a = at(lstart, l), z = at(lstart, l + 1);
        double used = 0;
        int autos = 0;
        for (size_t q = a; q < z; q++) {
            used += at(its, q).size + at(its, q).mm0 + at(its, q).mm1 + (q > a ? gap_main : 0);
            autos += at(its, q).auto_m0 + at(its, q).auto_m1;
        }
        double free = main_size - used, start = 0, between = 0, per_auto = 0;
        if (autos && free > 0) per_auto = free / autos;
        else {
            int jc = e.jc;
            if (jc == AJ_NORMAL || jc == AJ_STRETCH) jc = AJ_START;
            lay_distribute(jc, free, z - a, &start, &between);
        }
        double mpos = start;
        for (size_t q = a; q < z; q++) {
            lay_fi it = at(its, q);
            double m0 = it.mm0 + (it.auto_m0 ? per_auto : 0), m1 = it.mm1 + (it.auto_m1 ? per_auto : 0);
            double mp = mpos + m0;
            mpos += m0 + it.size + m1 + gap_main + between;
            if (e.frev) mp = main_size - mp - it.size;
            /* the cross axis */
            double lc = at(lcross, l);
            double c0 = it.cm0, c1 = it.cm1;
            if (it.align == AJ_STRETCH && it.cross_auto && !it.auto_c0 && !it.auto_c1 && row) {
                /* stretched: as tall as the line */
                double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
                double hh = lc - c0 - c1 - ve;
                t->box[it.box].h = hh > 0 ? hh : 0;
                it.cross = lc - c0 - c1;
            }
            double cfree = lc - it.cross - c0 - c1, cp = c0;
            if (it.auto_c0 && it.auto_c1) cp = c0 + cfree / 2;
            else if (it.auto_c0) cp = c0 + cfree;
            else if (it.auto_c1) cp = c0;
            else if (it.align == AJ_END) cp = c0 + cfree;
            else if (it.align == AJ_CENTER) cp = c0 + cfree / 2;
            cp += at(lpos, l);
            if (row) lay_place(t, it.box, mp, cp);
            else lay_place(t, it.box, cp, mp);
            its[q] = it;
        }
    }
    if (defh < 0) t->box[b].h = row ? (cy > 0 ? cy - gap_cross - lbetween : 0) : main_size;
    if (row && defh < 0 && t->box[b].h < 0) t->box[b].h = 0;
}

/* ---- grid containers (CSS Grid 1, 11, simplified) ---- */

/* a track's sizing: its min and its max (fixed px, auto, min-content, max-content, fr,
   fit-content's limit), its base size and growth limit, its place */
enum { TK_FIXED, TK_AUTO, TK_MINC, TK_MAXC, TK_FR, TK_FIT };
typedef struct lay_track { int mink, maxk; double minv, maxv, base, limit, pos; } lay_track;

/* one track size as written (px, %, fr, auto, min-content, max-content): its kind and value */
static int lay_track_one(const char *s, size_t n, double avail, int *kind, double *v) {
    *v = 0;
    if (lay_is_word(s, n, "auto")) { *kind = TK_AUTO; return 1; }
    if (lay_is_word(s, n, "min-content")) { *kind = TK_MINC; return 1; }
    if (lay_is_word(s, n, "max-content")) { *kind = TK_MAXC; return 1; }
    char buf[64];
    size_t k = n < sizeof buf - 1 ? n : sizeof buf - 1;
    memcpy(buf, s, k);
    buf[k] = 0;
    char *end = buf;
    double x = strtod(buf, &end);
    if (end == buf) return 0;
    if (strcmp(end, "fr") == 0) { *kind = TK_FR; *v = x; return 1; }
    if (strcmp(end, "%") == 0) {
        if (avail < 0) { *kind = TK_AUTO; return 1; }
        *kind = TK_FIXED;
        *v = avail * x / 100;
        return 1;
    }
    *kind = TK_FIXED;
    *v = x;
    return 1;
}

/* a track list (grid-template-columns' or -rows' computed text) into out: repeat() (a
   count, auto-fill, auto-fit), minmax(), fit-content(); line names passed over */
static void lay_tracks(const char *s, size_t n, double avail, double gap, vector<lay_track> *out, int depth) {
    size_t i = 0, a, ln, fa, fn;
    while (css_next_part(s, n, &i, &a, &ln)) {
        const char *p = s + a;
        if (p[0] == '[') continue;
        lay_track tk;
        memset(&tk, 0, sizeof tk);
        if (lay_call(p, ln, "repeat", &fa, &fn) && depth < 2) {
            size_t j = 0, ca, cn, la, lnn;
            if (!lay_arg(p + fa, fn, &j, &ca, &cn) || !lay_arg(p + fa, fn, &j, &la, &lnn)) continue;
            /* the rest of the arguments: the tracks repeated */
            lnn = fn - la;
            vector<lay_track> one;
            lay_tracks(p + fa + la, lnn, avail, gap, &one, depth + 1);
            if (!len(one)) continue;
            long count = 0;
            if (lay_is_word(p + fa + ca, cn, "auto-fill") || lay_is_word(p + fa + ca, cn, "auto-fit")) {
                double each = 0;
                for (size_t q = 0; q < len(one); q++) {
                    lay_track o = at(one, q);
                    double v = o.maxk == TK_FIXED ? o.maxv : o.mink == TK_FIXED ? o.minv : 0;
                    if (o.mink == TK_FIXED && o.minv > v) v = o.minv;
                    each += v + gap;
                }
                count = each > 0 && avail >= 0 ? (long)((avail + gap) / each) : 1;
                if (count < 1) count = 1;
            } else {
                for (size_t q = 0; q < cn; q++) if (p[fa + ca + q] >= '0' && p[fa + ca + q] <= '9') count = count * 10 + (p[fa + ca + q] - '0');
            }
            if (count > 1000) count = 1000;
            for (long c = 0; c < count; c++) for (size_t q = 0; q < len(one); q++) push_back(out, at(one, q));
            continue;
        }
        if (lay_call(p, ln, "minmax", &fa, &fn)) {
            size_t j = 0, xa, xn, ya, yn;
            if (!lay_arg(p + fa, fn, &j, &xa, &xn) || !lay_arg(p + fa, fn, &j, &ya, &yn)) continue;
            if (!lay_track_one(p + fa + xa, xn, avail, &tk.mink, &tk.minv) || !lay_track_one(p + fa + ya, yn, avail, &tk.maxk, &tk.maxv)) continue;
            if (tk.mink == TK_FR) tk.mink = TK_AUTO;
            push_back(out, tk);
            continue;
        }
        if (lay_call(p, ln, "fit-content", &fa, &fn)) {
            int kk;
            double v;
            if (!lay_track_one(p + fa, fn, avail, &kk, &v)) continue;
            tk.mink = TK_AUTO;
            tk.maxk = TK_FIT;
            tk.maxv = v;
            push_back(out, tk);
            continue;
        }
        int kk;
        double v;
        if (!lay_track_one(p, ln, avail, &kk, &v)) continue;
        tk.mink = kk == TK_FR ? TK_AUTO : kk;
        tk.minv = v;
        tk.maxk = kk;
        tk.maxv = v;
        push_back(out, tk);
    }
}

/* a grid line as written (grid-column-start's computed text): a number (from the end when
   negative), span n, or auto (0) */
static void lay_grid_line(const css_style *st, uint32_t c, long *line, long *span) {
    *line = 0;
    *span = 0;
    vector<char> src;
    amb_span sp = st->cval[c].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n;
    int is_span = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        if (lay_is_word(data(src) + a, n, "span")) { is_span = 1; continue; }
        long v = 0;
        int neg = 0, digits = 0;
        for (size_t q = a; q < a + n; q++) {
            char ch = data(src)[q];
            if (ch == '-') neg = 1;
            else if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); digits = 1; }
        }
        if (!digits) continue;
        if (is_span) *span = v;
        else *line = neg ? -v : v;
    }
    if (is_span && *span < 1) *span = 1;
}

/* an item's area: its first column and span, its first row and span */
typedef struct lay_ga { uint32_t c0, cs, r0, rs; } lay_ga;

/* the tracks' sizes (11.4-11.7, simplified): their base sizes from the content of the
   items that span one track, then up to their growth limits in the free space, the fr
   tracks sharing what is left, the auto ones stretched when nothing else takes it */
static void lay_size_tracks(vector<lay_track> *tr, const vector<double> *cmin, const vector<double> *cmax, double avail, double gap, int stretch) {
    size_t n = len(*tr);
    double total = gap * (double)(n > 0 ? n - 1 : 0), frs = 0;
    for (size_t q = 0; q < n; q++) {
        lay_track tk = at(*tr, q);
        double mn = at(*cmin, q), mx = at(*cmax, q);
        tk.base = tk.mink == TK_FIXED ? tk.minv : tk.mink == TK_MAXC ? mx : tk.mink == TK_MINC || tk.mink == TK_AUTO ? mn : 0;
        tk.limit = tk.maxk == TK_FIXED ? tk.maxv : tk.maxk == TK_MINC ? mn : tk.maxk == TK_FIT ? (mx < tk.maxv ? mx : tk.maxv) : tk.maxk == TK_FR ? tk.base : mx;
        if (tk.maxk == TK_FIT && tk.limit < mn) tk.limit = mn;
        if (tk.limit < tk.base) tk.limit = tk.base;
        if (tk.maxk == TK_FR) frs += tk.maxv;
        total += tk.base;
        (*tr)[q] = tk;
    }
    if (avail < 0) {
        /* no definite size: each track at its limit, an fr at its share of the largest */
        for (size_t q = 0; q < n; q++) (*tr)[q].base = at(*tr, q).maxk == TK_FR ? at(*tr, q).base : at(*tr, q).limit;
        return;
    }
    /* the free space to the growth limits, shared evenly */
    for (int round = 0; round < 32; round++) {
        double free = avail - total;
        int growing = 0;
        for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk != TK_FR && at(*tr, q).limit > at(*tr, q).base + 0.001) growing++;
        if (free <= 0.001 || !growing) break;
        double each = free / growing;
        for (size_t q = 0; q < n; q++) {
            lay_track tk = at(*tr, q);
            if (tk.maxk == TK_FR || tk.limit <= tk.base + 0.001) continue;
            double add = tk.limit - tk.base < each ? tk.limit - tk.base : each;
            tk.base += add;
            total += add;
            (*tr)[q] = tk;
        }
    }
    /* the fr tracks: what is left, by their factors; one whose base is more keeps it */
    if (frs > 0) {
        double fixed = gap * (double)(n > 0 ? n - 1 : 0);
        for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk != TK_FR) fixed += at(*tr, q).base;
        vector<uint8_t> out_of;
        resize(&out_of, n);
        for (int round = 0; round < 16; round++) {
            double left = avail - fixed, f = 0;
            for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_FR && !at(out_of, q)) f += at(*tr, q).maxv;
            if (f <= 0) break;
            double unit = left / (f < 1 ? 1 : f);
            int changed = 0;
            for (size_t q = 0; q < n; q++) {
                if (at(*tr, q).maxk != TK_FR || at(out_of, q)) continue;
                if (at(*tr, q).base > unit * at(*tr, q).maxv) { out_of[q] = 1; fixed += at(*tr, q).base; changed = 1; }
            }
            if (changed) continue;
            for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_FR && !at(out_of, q)) (*tr)[q].base = unit * at(*tr, q).maxv > 0 ? unit * at(*tr, q).maxv : 0;
            break;
        }
        return;
    }
    /* the auto tracks stretched over what is left */
    if (stretch) {
        double free = avail - total;
        int autos = 0;
        for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_AUTO) autos++;
        if (free > 0 && autos)
            for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_AUTO) (*tr)[q].base = at(*tr, q).base + free / autos;
    }
}

/* a grid container's items laid out: placed in its tracks (explicitly, then automatically
   row by row), the columns sized, the items laid out in them, the rows sized; b's content
   height when defh (its definite one) is -1 */
static void lay_grid(LCTX, uint32_t b, uint32_t el, double defh) {
    lay_ext e = LAY_X(el);
    double W = t->box[b].w;
    vector<lay_fi> its;
    vector<uint32_t> items;
    lay_items(LARGS, b, el, &its, &items);
    size_t ni = len(its);
    double cgap = lay_res(st, e.cgap, W), rgap = lay_res(st, e.rgap, defh >= 0 ? defh : 0);
    vector<lay_track> cols, rows, acol, arow;
    vector<char> src;
    amb_span sp = st->cval[e.gtc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), W, cgap, &cols, 0);
    clear(src);
    sp = st->cval[e.gtr].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), defh, rgap, &rows, 0);
    clear(src);
    sp = st->cval[e.gac].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), W, cgap, &acol, 0);
    clear(src);
    sp = st->cval[e.gar].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), defh, rgap, &arow, 0);
    lay_track auto_tk;
    memset(&auto_tk, 0, sizeof auto_tk);
    auto_tk.mink = TK_AUTO;
    auto_tk.maxk = TK_AUTO;
    lay_track col_tk = len(acol) ? at(acol, 0) : auto_tk, row_tk = len(arow) ? at(arow, 0) : auto_tk;
    size_t ncols = len(cols) ? len(cols) : 1, nrows_x = len(rows);
    if (!len(cols)) push_back(cols, col_tk);
    /* the items' areas: lines as written (from the explicit grid's end when negative) */
    vector<lay_ga> areas;
    for (size_t q = 0; q < ni; q++) {
        lay_ga g;
        memset(&g, 0, sizeof g);
        g.cs = 1;
        g.rs = 1;
        uint32_t ie = at(its, q).el;
        long cl0 = 0, cs0 = 0, cl1 = 0, cs1 = 0, rl0 = 0, rs0 = 0, rl1 = 0, rs1 = 0;
        if (ie) {
            lay_grid_line(st, LAY_X(ie).gcs, &cl0, &cs0);
            lay_grid_line(st, LAY_X(ie).gce, &cl1, &cs1);
            lay_grid_line(st, LAY_X(ie).grs, &rl0, &rs0);
            lay_grid_line(st, LAY_X(ie).gre, &rl1, &rs1);
        }
        if (cl0 < 0) cl0 = (long)ncols + 2 + cl0;
        if (cl1 < 0) cl1 = (long)ncols + 2 + cl1;
        if (rl0 < 0) rl0 = (long)nrows_x + 2 + rl0;
        if (rl1 < 0) rl1 = (long)nrows_x + 2 + rl1;
        if (cl0 > 0 && cl1 > cl0) g.cs = (uint32_t)(cl1 - cl0);
        else if (cs0 > 0) g.cs = (uint32_t)cs0;
        else if (cs1 > 0) g.cs = (uint32_t)cs1;
        if (rl0 > 0 && rl1 > rl0) g.rs = (uint32_t)(rl1 - rl0);
        else if (rs0 > 0) g.rs = (uint32_t)rs0;
        else if (rs1 > 0) g.rs = (uint32_t)rs1;
        if (cl0 <= 0 && cl1 > 0) cl0 = cl1 - (long)g.cs;
        if (rl0 <= 0 && rl1 > 0) rl0 = rl1 - (long)g.rs;
        g.c0 = cl0 > 0 ? (uint32_t)(cl0 - 1) : 0xffffffffu;
        g.r0 = rl0 > 0 ? (uint32_t)(rl0 - 1) : 0xffffffffu;
        if (g.cs > 1000) g.cs = 1000;
        if (g.rs > 1000) g.rs = 1000;
        push_back(areas, g);
        if (g.c0 != 0xffffffffu && g.c0 + g.cs > ncols) ncols = g.c0 + g.cs;
        if (g.cs > ncols) ncols = g.cs;
    }
    while (len(cols) < ncols) push_back(cols, col_tk);
    /* placed: the items whose row is given first, then the rest in the order of a cursor
       moving along each row (sparse) */
    vector<uint8_t> used;              /* rows x ncols */
    size_t nrows = nrows_x;
    resize(&used, nrows * ncols);
    for (int pass = 0; pass < 2; pass++) {
        size_t cr = 0, cc = 0;
        for (size_t q = 0; q < ni; q++) {
            lay_ga g = at(areas, q);
            if ((pass == 0) != (g.r0 != 0xffffffffu)) continue;
            if (g.r0 != 0xffffffffu && g.c0 == 0xffffffffu) {
                /* its row given: the first columns free in it */
                size_t c = 0;
                for (; c + g.cs <= ncols; c++) {
                    int free_here = 1;
                    for (size_t rr = g.r0; rr < g.r0 + g.rs && free_here; rr++)
                        for (size_t k = c; k < c + g.cs && free_here; k++) if (rr < nrows && at(used, rr * ncols + k)) free_here = 0;
                    if (free_here) break;
                }
                g.c0 = (uint32_t)(c + g.cs <= ncols ? c : 0);
            } else if (g.r0 == 0xffffffffu) {
                /* the cursor's next area that is free */
                for (;;) {
                    if (g.c0 != 0xffffffffu) { if (cc > g.c0) cr++; cc = g.c0; }
                    if (cc + g.cs > ncols) { cc = 0; cr++; continue; }
                    int free_here = 1;
                    for (size_t rr = cr; rr < cr + g.rs && free_here; rr++)
                        for (size_t k = cc; k < cc + g.cs && free_here; k++) if (rr < nrows && at(used, rr * ncols + k)) free_here = 0;
                    if (free_here) break;
                    if (g.c0 != 0xffffffffu) { cr++; continue; }
                    cc++;
                }
                g.r0 = (uint32_t)cr;
                g.c0 = (uint32_t)cc;
                cc += g.cs;
            }
            if (g.r0 + g.rs > nrows) { nrows = g.r0 + g.rs; resize(&used, nrows * ncols); }
            for (size_t rr = g.r0; rr < g.r0 + g.rs; rr++) for (size_t k = g.c0; k < g.c0 + g.cs && k < ncols; k++) used[rr * ncols + k] = 1;
            areas[q] = g;
        }
    }
    while (len(rows) < nrows) push_back(rows, row_tk);
    /* the columns: from the content of the items that span one */
    vector<double> cmin, cmax;
    resize(&cmin, ncols);
    resize(&cmax, ncols);
    for (size_t q = 0; q < ni; q++) {
        lay_ga g = at(areas, q);
        if (g.cs != 1 || g.c0 >= ncols) continue;
        lay_track tk = at(cols, g.c0);
        if (tk.mink == TK_FIXED && (tk.maxk == TK_FIXED)) continue;
        double mn, mx;
        lay_fi it = at(its, q);
        lay_item_intrinsic(LARGS, el, &it, &items, W, &mn, &mx);
        if (mn > at(cmin, g.c0)) cmin[g.c0] = mn;
        if (mx > at(cmax, g.c0)) cmax[g.c0] = mx;
    }
    int jc = e.jc;
    lay_size_tracks(&cols, &cmin, &cmax, W, cgap, jc == AJ_NORMAL || jc == AJ_STRETCH);
    double cols_total = cgap * (double)(ncols - 1);
    for (size_t c = 0; c < ncols; c++) cols_total += at(cols, c).base;
    double cstart = 0, cbetween = 0;
    if (jc != AJ_NORMAL && jc != AJ_STRETCH) lay_distribute(jc, W - cols_total, ncols, &cstart, &cbetween);
    double x = cstart;
    for (size_t c = 0; c < ncols; c++) { cols[c].pos = x; x += at(cols, c).base + cgap + cbetween; }
    /* the items laid out in their columns: stretched, or shrunk to fit and placed by
       justify-self */
    for (size_t q = 0; q < ni; q++) {
        lay_ga g = at(areas, q);
        lay_fi it = at(its, q);
        double aw = cgap * (double)(g.cs - 1);
        for (size_t c = g.c0; c < g.c0 + g.cs && c < ncols; c++) aw += at(cols, c).base;
        lay_style is;
        double ml = 0, mr = 0;
        int js = AJ_STRETCH, wauto = 1;
        if (it.el) {
            lay_style_of(LARGS, it.el, &is);
            ml = is.m[3].unit == LAY_AUTO ? 0 : lay_res(st, is.m[3], W);
            mr = is.m[1].unit == LAY_AUTO ? 0 : lay_res(st, is.m[1], W);
            js = LAY_X(it.el).js == AJ_AUTO || LAY_X(it.el).js == AJ_NORMAL ? e.ji : LAY_X(it.el).js;
            if (js == AJ_NORMAL) js = lay_replaced(d, it.el) ? AJ_START : AJ_STRETCH;
            wauto = is.width.unit == LAY_AUTO;
        }
        double fw = js == AJ_STRETCH && wauto ? aw - ml - mr : -1;
        it.box = lay_item_box(LARGS, b, el, &it, &items, aw - ml - mr, fw, -1);
        t->box[it.box].how = LAYOUT_WIDTH_HOW_GRID;
        double bw = lay_bw(t, it.box);
        double xoff = ml;
        if (js == AJ_END) xoff = aw - mr - bw;
        else if (js == AJ_CENTER) xoff = ml + (aw - ml - mr - bw) / 2;
        it.mm0 = xoff;
        it.cross = t->box[it.box].mt + lay_border_height(t, it.box) + t->box[it.box].mb;
        its[q] = it;
    }
    /* the rows: from the heights of the items that span one */
    vector<double> rmin;
    resize(&rmin, nrows);
    for (size_t q = 0; q < ni; q++) {
        lay_ga g = at(areas, q);
        if (g.rs != 1 || g.r0 >= nrows) continue;
        if (at(its, q).cross > at(rmin, g.r0)) rmin[g.r0] = at(its, q).cross;
    }
    int ac = e.ac;
    lay_size_tracks(&rows, &rmin, &rmin, defh, rgap, defh >= 0 && (ac == AJ_NORMAL || ac == AJ_STRETCH));
    /* an item that spans rows: what they lack, to the last of them */
    for (size_t q = 0; q < ni; q++) {
        lay_ga g = at(areas, q);
        if (g.rs < 2 || g.r0 + g.rs > nrows) continue;
        double sum = rgap * (double)(g.rs - 1);
        for (size_t rr = g.r0; rr < g.r0 + g.rs; rr++) sum += at(rows, rr).base;
        if (at(its, q).cross > sum) rows[g.r0 + g.rs - 1].base = at(rows, g.r0 + g.rs - 1).base + at(its, q).cross - sum;
    }
    double rows_total = nrows ? rgap * (double)(nrows - 1) : 0;
    for (size_t rr = 0; rr < nrows; rr++) rows_total += at(rows, rr).base;
    double rstart = 0, rbetween = 0;
    if (defh >= 0 && ac != AJ_NORMAL && ac != AJ_STRETCH) lay_distribute(ac, defh - rows_total, nrows, &rstart, &rbetween);
    double y = rstart;
    for (size_t rr = 0; rr < nrows; rr++) { rows[rr].pos = y; y += at(rows, rr).base + rgap + rbetween; }
    /* each item in its area: align-self (stretched as tall as its rows, by default) */
    for (size_t q = 0; q < ni; q++) {
        lay_ga g = at(areas, q);
        lay_fi it = at(its, q);
        if (g.r0 >= nrows || g.c0 >= ncols) continue;
        double ah = rgap * (double)(g.rs - 1);
        for (size_t rr = g.r0; rr < g.r0 + g.rs && rr < nrows; rr++) ah += at(rows, rr).base;
        int as = AJ_STRETCH, hauto = 1;
        if (it.el) {
            lay_style is;
            lay_style_of(LARGS, it.el, &is);
            as = LAY_X(it.el).as == AJ_AUTO || LAY_X(it.el).as == AJ_NORMAL ? e.ai : LAY_X(it.el).as;
            if (as == AJ_NORMAL) as = lay_replaced(d, it.el) ? AJ_START : AJ_STRETCH;
            hauto = is.height.unit == LAY_AUTO && LAY_X(it.el).hraw.unit == LAY_AUTO;
        }
        double mt = t->box[it.box].mt, mb = t->box[it.box].mb;
        if (as == AJ_STRETCH && hauto) {
            double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
            double hh = ah - mt - mb - ve;
            if (hh > t->box[it.box].h) t->box[it.box].h = hh;
        }
        double bh = lay_border_height(t, it.box), yoff = mt;
        if (as == AJ_END) yoff = ah - mb - bh;
        else if (as == AJ_CENTER) yoff = mt + (ah - mt - mb - bh) / 2;
        lay_place(t, it.box, at(cols, g.c0).pos + it.mm0, at(rows, g.r0).pos + yoff);
    }
    if (defh < 0) t->box[b].h = rows_total > 0 ? rows_total : 0;
}

/* ---- absolutely positioned boxes (CSS 2.1 10.3.7, 10.6.4), after the flow ---- */

/* each element the flow met out of it, laid out in its containing block -- the nearest
   positioned ancestor's padding box, the viewport's for fixed or for none -- by its
   offsets, or its static position where they are auto; its box that block's last child.
   Those inside it are met as it is laid out, and laid out in turn */
static void lay_positioned(LCTX, uint32_t vp, double vw, double vh) {
    for (size_t q = 0; q < len(*ab); q++) {
        lay_abs a = at(*ab, q);
        uint32_t el = a.el;
        lay_style s;
        lay_style_of(LARGS, el, &s);
        uint32_t si = lay_si(sx, gx, el);
        int fixed = at(*xc, si).pos == POS_FIXED;
        uint32_t cb = vp;
        if (!fixed)
            for (uint32_t x = a.box; x && x != vp; x = t->box[x].parent)
                if (t->box[x].position != LAYOUT_POSITION_STATIC) { cb = x; break; }
        /* the static position in cb's content box */
        double sx0 = a.x, sy0 = a.y;
        for (uint32_t x = a.box; x && x != cb && x != vp; x = t->box[x].parent) { sx0 += t->box[x].x; sy0 += t->box[x].y; }
        /* cb's padding box, in its content box */
        double px0 = 0, py0 = 0, cbw = vw, cbh = vh;
        if (cb != vp) {
            px0 = -t->box[cb].pl;
            py0 = -t->box[cb].pt;
            cbw = t->box[cb].w + t->box[cb].pl + t->box[cb].pr;
            cbh = t->box[cb].h + t->box[cb].pt + t->box[cb].pb;
        }
        lay_len o0 = at(*xc, si).off[0], o1 = at(*xc, si).off[1], o2 = at(*xc, si).off[2], o3 = at(*xc, si).off[3];
        int la = o3.unit == LAY_AUTO, ra = o1.unit == LAY_AUTO, ta = o0.unit == LAY_AUTO, ba = o2.unit == LAY_AUTO;
        double L = la ? 0 : lay_res(st, o3, cbw), R = ra ? 0 : lay_res(st, o1, cbw);
        double T = ta ? 0 : lay_res(st, o0, cbh), B = ba ? 0 : lay_res(st, o2, cbh);
        double ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cbw), mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cbw);
        double eh = s.b[1] + s.b[3] + lay_res(st, s.p[1], cbw) + lay_res(st, s.p[3], cbw);
        double fw = -1;
        int wdecl = s.width.unit != LAY_AUTO && !lay_content_sized(s.width);
        if (wdecl) fw = lay_res(st, s.width, cbw) + (s.border_box ? 0 : eh);
        else if (!la && !ra) fw = cbw - L - R - ml - mr;
        if (la && ra) { L = sx0 - px0; la = 0; }
        lay_bk bk;
        bk.ox = 0;
        bk.oy = 0;
        bk.ch = cbh;
        bk.fw = fw >= 0 ? (fw > 0 ? fw : 0) : -1;
        bk.shrink = fw < 0;
        bk.item = 0;
        double avail = cbw - (la ? 0 : L) - (ra ? 0 : R);
        lay_mc m1, m2;
        int empty;
        uint32_t bx = lay_block(LARGS, cb, el, fw >= 0 ? cbw : avail, 0, 0, 0, 0, &bk, &m1, &m2, &empty);
        if (fw >= 0 && !wdecl) t->box[bx].how = LAYOUT_WIDTH_HOW_POSITIONED;
        else if (wdecl) t->box[bx].how = s.width.unit == LAY_PX ? LAYOUT_WIDTH_HOW_DECLARED : LAYOUT_WIDTH_HOW_PERCENT;
        t->box[bx].cb = cb == vp ? vp : cb;
        t->box[bx].avail = cbw;
        double bw = t->box[bx].w + eh;
        if (la) L = cbw - R - ml - mr - bw;
        if (!ra && wdecl && s.m[3].unit == LAY_AUTO && s.m[1].unit == LAY_AUTO) {
            /* auto margins with both offsets and the width: the box in the middle */
            double rest = cbw - L - R - bw;
            if (rest > 0) { ml = rest / 2; mr = rest / 2; }
        }
        t->box[bx].ml = ml;
        t->box[bx].mr = mr;
        /* its height: from both offsets when it is auto, else its content's */
        double mtv = t->box[bx].mt, mbv = t->box[bx].mb;
        double ve = t->box[bx].bt + t->box[bx].bb + t->box[bx].pt + t->box[bx].pb;
        if (s.height.unit == LAY_AUTO && at(*xc, si).hraw.unit == LAY_AUTO && !ta && !ba) {
            double h = cbh - T - B - mtv - mbv - ve;
            t->box[bx].h = h > 0 ? h : 0;
        }
        double bh = lay_border_height(t, bx);
        if (ta && ba) T = sy0 - py0;
        else if (ta) T = cbh - B - mtv - mbv - bh;
        lay_place(t, bx, px0 + L + ml, py0 + T + mtv);
    }
}

/* the image a background-image's first url() names (its cval c), fetched through the
   loader (once a URL: a resource fetched before is the one): its resource, 0 for none */
static uint32_t lay_fetch_image(const css_style *st, net_loader *net, uint32_t c) {
    vector<char> v;
    amb_span sp = st->cval[c].text;
    append_range(v, text_ptr(st, sp), sp.len);
    push_back(v, 0);
    const char *u = strstr(data(v), "url(");
    if (!u) return 0;
    size_t i = (size_t)(u - data(v)) + 4, n = len(v) - 1;
    while (i < n && (data(v)[i] == ' ' || data(v)[i] == '"' || data(v)[i] == '\'')) i++;
    size_t e = i;
    while (e < n && data(v)[e] != ')' && data(v)[e] != '"' && data(v)[e] != '\'') e++;
    if (e == i || e - i > 2000) return 0;
    char ref[2048];
    memcpy(ref, data(v) + i, e - i);
    ref[e - i] = 0;
    /* the base: the declaration's sheet's URL, or the page's */
    char base[2048];
    base[0] = 0;
    uint32_t dcl = st->cval[c].winner;
    if (dcl && dcl < st->decl_len) {
        uint32_t ru = st->decl[dcl].rule;
        uint32_t sh = ru && ru < st->rule_len ? st->rule[ru].sheet : 0;
        if (sh && sh < st->sheet_len && st->sheet[sh].url.len && st->sheet[sh].url.len < sizeof base) {
            amb_span su = st->sheet[sh].url;
            memcpy(base, text_ptr(st, su), su.len);
            base[su.len] = 0;
        }
    }
    for (uint32_t r = 1; r < net->resource_len && !base[0]; r++)
        if (net->resource[r].live && net->resource[r].kind == NET_KIND_DOCUMENT) {
            amb_span fu = net->resource[r].final_url.len ? net->resource[r].final_url : net->resource[r].url;
            if (fu.len < sizeof base) { memcpy(base, text_ptr(net, fu), fu.len); base[fu.len] = 0; }
        }
    char abs_url[4096];
    if (!base[0] || !net_url_resolve(base, ref, abs_url, sizeof abs_url)) return 0;
    size_t al = strlen(abs_url);
    for (uint32_t r = 1; r < net->resource_len; r++)
        if (net->resource[r].live && net->resource[r].kind == NET_KIND_IMAGE && net->resource[r].url.len == al &&
            memcmp(text_ptr(net, net->resource[r].url), abs_url, al) == 0) return r;
    net_resource_h h = net_fetch(net, abs_url, al, NET_KIND_IMAGE);
    return h.index;
}

/* ---- page coordinates ---- */

static void lay_absolute(layout_tree *t, uint32_t b, double ox, double oy) {
    t->box[b].x += ox;
    t->box[b].y += oy;
    double cx = t->box[b].x, cy = t->box[b].y;
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) {
        t->line[ln].x += cx;
        t->line[ln].y += cy;
        double lx = t->line[ln].x, ly = t->line[ln].y;
        t->line[ln].baseline += ly;
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            t->frag[fr].x += lx;
            t->frag[fr].y += ly;
            t->frag[fr].baseline += ly;
        }
    }
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) lay_absolute(t, c, cx, cy);
}

layout_status layout_run(layout_tree *t, dom_document *doc, css_style *style, net_loader *net, double width, double height) {
    amb_fonts fonts = {0};
    defer(fonts) { AMB_FONTS_DROP(fonts); }
    amb_fonts_load(&fonts);
    /* each node's style, and each style's first value; then each style as layout reads it */
    vector<uint32_t> node_style;
    resize(node_style, doc->node_len);
    vector<uint32_t> style_first;
    resize(style_first, 1);
    for (uint32_t e = 1; e < style->estyle_len; e++) {
        uint32_t x = style->estyle[e].node.index, sid = style->estyle[e].style;
        if (x < doc->node_len && !at(node_style, x)) node_style[x] = sid;
        if (sid >= len(style_first)) resize(style_first, (size_t)sid + 1);
        style_first[sid] = style->estyle[e].first;
    }
    /* the pseudo-elements, each element's together (by element, then pseudo-element), and
       their styles; whether an element has any, in its node_style */
    vector<lay_gen> gens;
    if (style->pstyle_len > 1) {
        vector<uint32_t> head;
        resize(head, doc->node_len);
        for (uint32_t q = style->pstyle_len - 1; q >= 1; q--) {
            uint32_t x = style->pstyle[q].node.index;
            if (style->pstyle[q].live && x < doc->node_len) head[x] = q;
        }
        for (uint32_t x = 1; x < doc->node_len; x++) {
            for (uint32_t q = at(head, x); q && q < style->pstyle_len && style->pstyle[q].node.index == x; q++) {
                lay_gen g;
                memset(&g, 0, sizeof g);
                g.el = x;
                g.pseudo = (uint32_t)style->pstyle[q].pseudo;
                g.style = style->pstyle[q].style;
                push_back(gens, g);
                if (g.pseudo == CSS_PSEUDO_BEFORE) node_style[x] |= LAY_HAS_BEFORE;
                else if (g.pseudo == CSS_PSEUDO_AFTER) node_style[x] |= LAY_HAS_AFTER;
                else node_style[x] |= LAY_HAS_OTHER;
                if (g.style >= len(style_first)) resize(style_first, (size_t)g.style + 1);
                style_first[g.style] = style->pstyle[q].first;
            }
        }
    }
    /* the web fonts @font-face gave, each read once: each face record's face in fonts */
    vector<int32_t> webface;
    resize(webface, style->face_len > 0 ? style->face_len : 1);
    for (uint32_t fi = 1; fi < style->face_len; fi++) {
        webface[fi] = -1;
        uint32_t r = style->face[fi].font.index;
        if (!net || !r || r >= net->resource_len) continue;
        for (uint32_t q = 1; q < fi && at(webface, fi) < 0; q++)
            if (style->face[q].font.index == r) webface[fi] = at(webface, q);
        if (at(webface, fi) >= 0) continue;
        vector<uint8_t> bytes;
        amb_span body = net->resource[r].body;
        append_range(bytes, (const uint8_t *)bytes_ptr(net, body), body.len);
        webface[fi] = amb_fonts_add(&fonts, data(bytes), len(bytes));
    }
    vector<lay_style> styles;
    vector<lay_ext> exts;
    resize(styles, len(style_first));
    resize(exts, len(style_first));
    for (size_t k = 0; k < len(style_first); k++) {
        lay_style ls;
        lay_style_make(style, &fonts, &webface, at(style_first, k), &ls);
        styles[k] = ls;
        lay_ext le;
        lay_ext_make(style, at(style_first, k), &le);
        exts[k] = le;
    }
    vector<lay_abs> abs;
    vector<lay_flt> flts;
    /* the images the styles' backgrounds name: each url() (the first of its layers)
       resolved against the sheet its declaration is in (the page for one of its own) and
       fetched once */
    for (size_t k = 0; k < len(exts) && net; k++) {
        uint32_t c = at(exts, k).bgimg;
        if (!c) continue;
        uint32_t res = lay_fetch_image(style, net, c);
        exts[k].bgres = res;
    }
    /* the text of each ::before and ::after, in tree order */
    if (len(gens) && doc->node_len > 1) {
        vector<lay_ctr> ctr;
        int depth = 0;
        lay_gen_walk(t, doc, style, &node_style, &style_first, &styles, &gens, 1, &ctr, &depth, 0);
    }
    /* the root element: the document's element child */
    uint32_t root = 0;
    for (uint32_t c = doc->node_len > 1 ? doc->node[1].first_child : 0; c; c = doc->node[c].next_sibling)
        if (doc->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    uint32_t vp = lay_new_box(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, 0, 0, LAYOUT_BOX_KIND_VIEWPORT);
    t->box[vp].w = width;
    t->box[vp].h = height;
    t->box[vp].how = LAYOUT_WIDTH_HOW_VIEWPORT;
    t->box[vp].avail = width;
    /* the canvas: the Canvas of the scheme the root is used in (CSS Color Adjust 1, the
       cascade's color-scheme value's colour), white without a root */
    uint32_t rsid = root ? at(node_style, root) & LAY_SMASK : 0;
    uint32_t rc0 = rsid && rsid < len(style_first) ? at(style_first, rsid) : 0;
    uint32_t canvas = rc0 ? style->cval[rc0 + CSS_PROP_COLOR_SCHEME].color : 0;
    t->box[vp].background = (canvas & 0xff) == 0xff ? canvas : 0xffffffff;
    if (!root) { t->revision++; return LAYOUT_NO_ROOT; }
    if (lay_display_of(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, root) != D_NONE) {
        lay_mc mt, mb;
        int empty;
        /* the root: its containing block the initial one, the viewport's size */
        lay_bk bk;
        bk.ox = 0;
        bk.oy = 0;
        bk.ch = height;
        bk.fw = -1;
        bk.shrink = 0;
        bk.item = 0;
    bk.item = 0;
        uint32_t rb = lay_block(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, vp, root, width, 0, 0, 0, 0, &bk, &mt,
                                &mb, &empty);
        lay_place(t, rb, t->box[rb].ml, t->box[rb].mt);
        double bottom = t->box[rb].mt + lay_border_height(t, rb) + t->box[rb].mb;
        /* the floats the root holds */
        for (size_t k = 0; k < len(flts); k++) if (at(flts, k).y + at(flts, k).h > bottom) bottom = at(flts, k).y + at(flts, k).h;
        lay_positioned(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, vp, width, height);
        if (bottom > height) t->box[vp].h = bottom;
    }
    lay_absolute(t, vp, 0, 0);
    lay_radii(t);
    t->revision++;
    return LAYOUT_OK;
}
