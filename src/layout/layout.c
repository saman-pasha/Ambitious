/* src/layout/layout.c -- the layout module (contracts/layout.pl): the box tree of
   a styled document, laid out; and the module's generated implementation.

   Boxes. The root element's box is the viewport box's child. An element whose
   display is block, list-item, table (or flex, grid, flow-root, laid out as
   block) makes a block-level box. An inline element makes no box: its text and
   its inline children are laid out in its block container's lines, its margins,
   borders and paddings as space at its two ends, and its background drawn
   behind its part of each line. An inline element that holds a block is
   flattened into its container. inline-block, img, input, button, textarea and
   select make atomic inline boxes. A container with both block-level and
   inline-level children wraps each run of the inline ones that shows anything
   in an anonymous block.

   Block layout (CSS 2.1, 10.3.3 and 8.3.1). The width fills the containing
   block, or is as declared, with auto margins taking what is left (centring when
   both are auto). Vertical margins collapse: between siblings; through a parent
   that has no border or padding on that side (and is no new formatting context:
   the root, a cell, an inline-block); and through an empty box.

   Inline layout. White space is collapsed or kept as white-space says. Lines
   break at spaces (and around atomic inlines), never inside a word, a word too
   wide for its line overflowing it. A line's height comes from its strut and its
   pieces' line heights about one baseline; it is aligned left, right, centre or
   justified.

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
   it. border-spacing is 2px; a cell's content sits in the middle of its row.

   What is not done: floats, positioning, flex and grid as such (their
   containers are blocks), vertical-align, min/max width and height,
   box-sizing, overflow, a percentage height, rowspan, writing modes and
   bidirectional text. Coordinates are relative to the parent box's content box
   while the tree is laid out, and made page coordinates at the end. */
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

/* the run's context, as parameters: a struct may hold no borrow. sx: each node's style
   (0 for none); sc: each style as layout reads it, made once a run */
#define LCTX layout_tree *t, const dom_document *d, const css_style *st, const net_loader *nt, const amb_fonts *f, const vector<uint32_t> *sx, \
    const vector<lay_style> *sc, const vector<lay_gen> *gx
#define LARGS t, d, st, nt, f, sx, sc, gx

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
    int face, italic, align, ws, deco, list, border_box, tt;
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
    lay_word_of(st, cv[CSS_PROP_FONT_FAMILY], w);
    int family = lay_w(w, "sans-serif") ? 0 : lay_w(w, "monospace") ? 2 : lay_w(w, "serif") ? 1 : -1;
    int wf;
    if (family < 0 && lay_web_face(st, webface, w, weight, s->italic, &wf, &s->font)) {
        s->face = wf;
        /* an italic face is slanted already: no slant made for it */
        for (uint32_t fi = 1; fi < st->face_len && fi < len(*webface); fi++)
            if (at(*webface, fi) == wf && st->face[fi].italic) s->italic = 0;
    } else s->face = amb_face_for(family < 0 ? 1 : family, bold);
    double gap;
    amb_face_metrics(f, s->face, s->fs, &s->asc, &s->desc, &gap);
    c = cv[CSS_PROP_LINE_HEIGHT];
    if (st->cval[c].unit == CSS_UNIT_NUMBER) s->lh = st->cval[c].num * s->fs;
    else if (st->cval[c].unit == CSS_UNIT_PX) s->lh = st->cval[c].num;
    else s->lh = s->asc + s->desc + gap;
    lay_word_of(st, cv[CSS_PROP_TEXT_ALIGN], w);
    s->align = lay_w(w, "right") || lay_w(w, "end") ? AL_RIGHT : lay_w(w, "center") ? AL_CENTER :
               lay_w(w, "justify") ? AL_JUSTIFY : AL_LEFT;
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

static int lay_is_named(const dom_document *d, uint32_t x, const char *name) {
    return !(x & LAY_GEN) && d->node[x].kind == DOM_NODE_KIND_ELEMENT && names_is(d, d->node[x].name, name, strlen(name));
}
/* an element drawn by its own content rules: an image, a form control, an embedding */
static int lay_replaced(const dom_document *d, uint32_t x) {
    return lay_is_named(d, x, "img") || lay_is_named(d, x, "input") || lay_is_named(d, x, "textarea") ||
           lay_is_named(d, x, "select") || lay_is_named(d, x, "iframe") || lay_is_named(d, x, "video") ||
           lay_is_named(d, x, "canvas") || lay_is_named(d, x, "embed") || lay_is_named(d, x, "object") ||
           lay_is_named(d, x, "svg");
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

/* a container's children as segments: a block-level element, or a run of inline-level nodes */
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
    if (dsp == D_CONTENTS || (dsp == D_INLINE && lay_holds_block(LARGS, c))) { lay_collect(LARGS, c, segs, items); return; }
    if (lay_block_level(dsp)) {
        lay_seg s;
        s.node = c;
        s.first = 0;
        s.n = 0;
        s.block = 1;
        push_back(segs, s);
        return;
    }
    lay_item(segs, items, c);
}
/* a container's children: its ::before first, its ::after last; a pseudo-element's, its text */
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items) {
    if (parent & LAY_GEN) { lay_item(segs, items, parent | LAY_GTEXT); return; }
    uint32_t fl = parent < len(*sx) ? at(*sx, parent) : 0;
    if (fl & LAY_HAS_BEFORE) {
        int64_t g = lay_gen_find(gx, parent, 1);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
    for (uint32_t c = d->node[parent].first_child; c; c = d->node[c].next_sibling) {
        int kind = d->node[c].kind;
        if (kind == DOM_NODE_KIND_TEXT) { lay_item(segs, items, c); continue; }
        if (kind != DOM_NODE_KIND_ELEMENT) continue;
        lay_collect_el(LARGS, c, segs, items);
    }
    if (fl & LAY_HAS_AFTER) {
        int64_t g = lay_gen_find(gx, parent, 2);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
}

/* ---- pieces: an inline run as words, spaces, breaks, atomic boxes and inline edges ---- */

enum { P_WORD, P_SPACE, P_BREAK, P_ATOMIC, P_OPEN, P_CLOSE };
typedef struct lay_piece {
    uint8_t kind, face, italic, deco, brk, keep;   /* brk: a space a line may break at; keep: a kept (pre) space */
    uint8_t tt;                                    /* text-transform */
    uint32_t node, elem;                           /* the text node, or the element; and the text's element */
    uint32_t font;                                 /* a web font's resource, 0 for a built-in face */
    double ls, wsp;                                /* letter-spacing, word-spacing */
    uint32_t off, len;                             /* a word's (or a kept space's) bytes of its text node */
    double w, size, lh, asc, desc;
    uint32_t color, bg;
    double aw, ah;                                 /* an atomic box's content width (and a replaced one's height) */
} lay_piece;

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
        else if (lay_is_named(d, el, "canvas") || lay_is_named(d, el, "svg")) { dw = 300; dh = 150; }
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

/* the code points of UTF-8 text: letter-spacing's count */
static size_t lay_cps(const char *s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) k++;
    return k;
}

/* the pieces of text tx[0, n): a text node's (node), or a pseudo-element's content (node
   LAY_GEN | LAY_GTEXT and its index), in the style s of its element pe */
static void lay_chars(LCTX, const lay_style *s, uint32_t node, uint32_t pe, const char *tx, size_t n, int deco, vector<lay_piece> *ps, int *sp) {
    size_t i = 0;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, s);
    p.node = node;
    p.elem = pe;
    p.deco = (uint8_t)deco;
    /* a space: its advance, with letter-spacing and word-spacing */
    double space = amb_advance(f, s->face, ' ', s->fs) + s->ls + s->wsp;
    int collapse = s->ws == WS_NORMAL || s->ws == WS_NOWRAP || s->ws == WS_PRE_LINE;
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
        while (j < n && !(tx[j] == ' ' || tx[j] == '\t' || tx[j] == '\r' || tx[j] == '\f' || tx[j] == '\n')) j++;
        p.kind = P_WORD;
        p.off = (uint32_t)i;
        p.len = (uint32_t)(j - i);
        if (s->tt) {
            vector<char> tw;
            lay_transform(tx + i, j - i, s->tt, &tw);
            p.w = amb_text_width(f, s->face, s->fs, data(tw), len(tw));
        } else p.w = amb_text_width(f, s->face, s->fs, tx + i, j - i);
        p.w += s->ls * (double)lay_cps(tx + i, j - i);
        p.brk = 0;
        p.keep = 0;
        push_back(ps, p);
        *sp = 0;
        i = j;
    }
}

static void lay_text_pieces(LCTX, uint32_t node, int deco, vector<lay_piece> *ps, int *sp) {
    lay_style s;
    if (node & LAY_GEN) {
        /* a pseudo-element's content, in its style */
        uint32_t pe = node & ~LAY_GTEXT;
        lay_style_of(LARGS, pe, &s);
        vector<char> gt;
        amb_span g = at(*gx, node & LAY_GIDX).text;
        append_range(gt, text_ptr(t, g), g.len);
        lay_chars(LARGS, &s, node, pe, data(gt), len(gt), deco, ps, sp);
        return;
    }
    /* a text node's, in its parent's style (read in place) */
    uint32_t pe = d->node[node].parent;
    uint32_t si = pe < len(*sx) ? at(*sx, pe) & LAY_SMASK : 0;
    amb_span span = d->node[node].data;
    lay_chars(LARGS, data(*sc) + si, node, pe, text_ptr(d, span), span.len, deco, ps, sp);
}

/* the pieces of an inline-level node: text, an inline element (its edges around its
   children's pieces), a forced break, an atomic box */
static void lay_node_pieces(LCTX, uint32_t node, int deco, double cw, vector<lay_piece> *ps, int *sp) {
    if (node & LAY_GTEXT) { lay_text_pieces(LARGS, node, deco, ps, sp); return; }
    if (!(node & LAY_GEN)) {
        if (d->node[node].kind == DOM_NODE_KIND_TEXT) { lay_text_pieces(LARGS, node, deco, ps, sp); return; }
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
        p.aw = w;
        p.ah = h;
        p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw) + w + lay_res(st, s.p[1], cw) + s.b[1] + lay_res(st, s.m[1], cw);
        push_back(ps, p);
        *sp = 0;
        return;
    }
    int inner = deco | s.deco;
    p.kind = P_OPEN;
    p.bg = s.bg;
    p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw);
    push_back(ps, p);
    if (node & LAY_GEN) lay_node_pieces(LARGS, node | LAY_GTEXT, inner, cw, ps, sp);
    else {
        /* its children, after its ::before and before its ::after */
        uint32_t fl = node < len(*sx) ? at(*sx, node) : 0;
        if (fl & LAY_HAS_BEFORE) {
            int64_t g = lay_gen_find(gx, node, 1);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp);
        }
        for (uint32_t c = d->node[node].first_child; c; c = d->node[c].next_sibling) lay_node_pieces(LARGS, c, inner, cw, ps, sp);
        if (fl & LAY_HAS_AFTER) {
            int64_t g = lay_gen_find(gx, node, 2);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp);
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
    for (uint32_t k = first; k < first + n; k++) lay_node_pieces(LARGS, at(*items, k), s.deco, cw, ps, &sp);
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

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    double edges = lay_edges_px(st, &s, cw);
    *mn = 0;
    *mx = 0;
    if (s.width.unit == LAY_PX) {
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
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        double a, b;
        if (sg.block) lay_intrinsic(LARGS, sg.node, cw, &a, &b);
        else {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, cw, &ps);
            lay_run_intrinsic(&ps, &a, &b);
        }
        if (a > *mn) *mn = a;
        if (b > *mx) *mx = b;
    }
    *mn += edges;
    *mx += edges;
}

/* ---- the store's records ---- */

static uint32_t lay_new_box(LCTX, uint32_t parent, uint32_t el, layout_box_kind kind) {
    layout_box_h h = box_new(t);
    t->box[h.index].kind = kind;
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
static void lay_block_width(layout_tree *t, const css_style *st, uint32_t b, const lay_style *s, double cw) {
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
    t->box[b].x = bx + t->box[b].bl + t->box[b].pl;
    t->box[b].y = by + t->box[b].bt + t->box[b].pt;
}

/* ---- margins as they collapse: the largest positive one and the most negative ---- */

typedef struct lay_mc { double pos, neg; } lay_mc;
static lay_mc lay_mc_of(double m) { lay_mc c; c.pos = m > 0 ? m : 0; c.neg = m < 0 ? m : 0; return c; }
static lay_mc lay_mc_add(lay_mc a, lay_mc b) { a.pos = a.pos > b.pos ? a.pos : b.pos; a.neg = a.neg < b.neg ? a.neg : b.neg; return a; }
static double lay_mc_v(lay_mc a) { return a.pos + a.neg; }

/* ---- lines ---- */

/* the vertical extent of a piece about its baseline */
static void lay_piece_vert(const lay_piece *p, double *above, double *below) {
    double hl = (p->lh - (p->asc + p->desc)) / 2;
    *above = p->asc + hl;
    *below = p->desc + hl;
}

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, lay_mc *mt, lay_mc *mb, int *empty);
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, lay_mc *et, lay_mc *eb, int *empty);

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
    int replaced = lay_replaced(d, el);
    uint32_t b = lay_new_box(LARGS, parent, el, replaced ? LAYOUT_BOX_KIND_REPLACED : LAYOUT_BOX_KIND_INLINE_BLOCK);
    lay_edges(t, st, b, &s, cw);
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
    lay_flow_children(LARGS, b, el, 1, s.height.unit == LAY_AUTO, &et, &eb, &empty);
    if (s.height.unit == LAY_PX) t->box[b].h = s.height.v;
    return b;
}

/* the baseline of a laid-out atomic box, from its margin box's top: its last line's,
   or its bottom margin edge */
static double lay_atomic_baseline(const layout_tree *t, uint32_t b) {
    double top = t->box[b].mt + t->box[b].bt + t->box[b].pt;
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
        if (p.node & LAY_GEN) {
            amb_span g = at(*gx, p.node & LAY_GIDX).text;
            lay_piece_bytes(text_ptr(t, g), g.len, p.kind == P_WORD, p.tt, p.off, p.len, out);
            continue;
        }
        amb_span sp = d->node[p.node].data;
        const char *tx = text_ptr(d, sp);
        if (p.kind == P_WORD && p.tt && p.off + p.len <= sp.len) { lay_transform(tx + p.off, p.len, p.tt, out); continue; }
        for (uint32_t i = p.off; i < p.off + p.len && i < sp.len; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);
    }
}

/* one line of pieces [a, e) in box b at y, w wide; returns its height. open: the inline
   elements open at its start, which it leaves as those open at its end */
static double lay_line(LCTX, uint32_t b, const lay_style *bs, const vector<lay_piece> *ps, size_t a, size_t e, double w, double y,
                       int justify_ok, double indent, vector<uint32_t> *open) {
    /* trailing collapsible spaces hang: they take no width */
    size_t end = e;
    while (end > a && at(*ps, end - 1).kind == P_SPACE && !at(*ps, end - 1).keep) end--;
    /* the pieces' x and the line's vertical extent */
    vector<double> xs;
    vector<uint32_t> atoms;
    double x = indent, above, below, hl = (bs->lh - (bs->asc + bs->desc)) / 2;
    double max_above = bs->asc + hl, max_below = bs->desc + hl;
    int spaces = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        push_back(xs, x);
        if (p.kind == P_SPACE && !p.keep) spaces++;
        if (p.kind == P_WORD || (p.kind == P_SPACE && p.keep)) {
            lay_piece_vert(&p, &above, &below);
            if (above > max_above) max_above = above;
            if (below > max_below) max_below = below;
        }
        if (p.kind == P_ATOMIC) {
            uint32_t ab = lay_atomic(LARGS, b, &p, w);
            push_back(atoms, ab);
            double mh = t->box[ab].mt + lay_border_height(t, ab) + t->box[ab].mb;
            double base = lay_atomic_baseline(t, ab);
            if (base > max_above) max_above = base;
            if (mh - base > max_below) max_below = mh - base;
        }
        x += p.w;
    }
    double line_w = x, free = w - line_w, shift = 0, extra = 0;
    if (bs->align == AL_RIGHT && free > 0) shift = free;
    else if (bs->align == AL_CENTER && free > 0) shift = free / 2;
    else if (bs->align == AL_JUSTIFY && justify_ok && free > 0 && spaces > 0) extra = free / spaces;
    double baseline = max_above, height = max_above + max_below;
    layout_line_h lh = line_new(t);
    uint32_t ln = lh.index;
    t->line[ln].x = 0;
    t->line[ln].y = y;
    t->line[ln].w = w;
    t->line[ln].h = height;
    t->line[ln].baseline = baseline;
    lines_append(t, b, ln);
    /* the inline elements' backgrounds, under the text: each open one from where it is
       opened (or the line's start) to where it is closed (or the line's end) */
    vector<double> starts;           /* for each open element, where it opens on this line (-1: before the line) */
    for (size_t k = 0; k < len(*open); k++) push_back(starts, -1.0);
    double gap = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        double px = at(xs, k - a) + shift + gap;
        if (p.kind == P_SPACE && !p.keep) gap += extra;
        if (p.kind == P_OPEN) { push_back(open, p.node); push_back(starts, px); }
        if (p.kind == P_CLOSE && len(*open)) {
            /* its background, from its start on this line (or the line's start) to here */
            double from = back(starts);
            uint32_t elx = back(*open);
            pop_back(open);
            pop_back(&starts);
            lay_style es;
            lay_style_of(LARGS, elx, &es);
            if ((es.bg & 0xff) != 0) {
                double x0 = from < 0 ? 0 : from;
                layout_frag_h fh = frag_new(t);
                uint32_t fr = fh.index;
                t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
                t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
                t->frag[fr].x = x0 + (from < 0 ? 0 : lay_res(st, es.m[3], w));
                t->frag[fr].w = px + p.w - lay_res(st, es.m[1], w) - t->frag[fr].x;
                t->frag[fr].y = baseline - es.asc - lay_res(st, es.p[0], w) - es.b[0];
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
        double from = at(starts, k);
        double x0 = from < 0 ? 0 : from + lay_res(st, es.m[3], w);
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
        t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
        t->frag[fr].x = x0;
        t->frag[fr].w = line_w + shift + gap - x0;
        t->frag[fr].y = baseline - es.asc - lay_res(st, es.p[0], w) - es.b[0];
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
            uint32_t ab = at(atoms, atom);
            atom++;
            double base = lay_atomic_baseline(t, ab);
            lay_place(t, ab, px + t->box[ab].ml, y + baseline - base + t->box[ab].mt);
            layout_frag_h fh = frag_new(t);
            uint32_t fr = fh.index;
            t->frag[fr].kind = LAYOUT_FRAG_KIND_ATOMIC;
            t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
            t->frag[fr].atomic = ab;
            t->frag[fr].x = px;
            t->frag[fr].y = baseline - base;
            t->frag[fr].w = p.w;
            t->frag[fr].h = t->box[ab].mt + lay_border_height(t, ab) + t->box[ab].mb;
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
        while (r < end && extra == 0) {
            lay_piece q = at(*ps, r);
            if ((q.kind != P_WORD && q.kind != P_SPACE) || q.node != p.node) break;
            if (q.size != p.size || q.color != p.color || q.face != p.face || q.font != p.font || q.deco != p.deco) break;
            rw += q.w;
            r++;
        }
        while (r > k + 1 && at(*ps, r - 1).kind == P_SPACE && !at(*ps, r - 1).keep) { r--; rw -= at(*ps, r).w; }
        vector<char> txt;
        lay_piece_text(t, d, gx, ps, k, r, &txt);
        amb_span sp = text_add(t, data(txt), len(txt));
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        /* a pseudo-element's content: generated, its element's */
        t->frag[fr].kind = (p.node & LAY_GEN) ? LAYOUT_FRAG_KIND_GENERATED : LAYOUT_FRAG_KIND_TEXT;
        t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
        t->frag[fr].text = sp;
        t->frag[fr].x = px;
        t->frag[fr].y = baseline - p.asc;
        t->frag[fr].w = rw;
        t->frag[fr].h = p.asc + p.desc;
        t->frag[fr].baseline = baseline;
        t->frag[fr].face = p.face;
        if (p.font) t->frag[fr].font = resource_handle(nt, p.font);
        t->frag[fr].spacing = p.ls;
        t->frag[fr].word_spacing = p.wsp;
        t->frag[fr].size = p.size;
        t->frag[fr].italic = p.italic;
        t->frag[fr].color = p.color;
        t->frag[fr].decoration = p.deco;
        frags_append(t, ln, fr);
        k = r;
    }
    return height;
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

/* ::first-line: the pieces in the style of the pseudo-element fl where they have their
   block's (bs) -- what they inherit -- measured again */
static void lay_first_line_pieces(LCTX, vector<lay_piece> *ps, const lay_style *bs, const lay_style *fl) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.color == bs->color) p.color = fl->color;
        if (p.face != bs->face || p.font != bs->font || p.size != bs->fs || p.ls != bs->ls || p.wsp != bs->wsp) { (*ps)[k] = p; continue; }
        double old_space = amb_advance(f, p.face, ' ', p.size) + p.ls + p.wsp;
        p.face = (uint8_t)fl->face;
        p.font = fl->font;
        p.italic = (uint8_t)fl->italic;
        p.size = fl->fs;
        p.lh = fl->lh;
        p.asc = fl->asc;
        p.desc = fl->desc;
        p.ls = fl->ls;
        p.wsp = fl->wsp;
        if (fl->tt && p.kind == P_WORD) p.tt = (uint8_t)fl->tt;
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
static double lay_lines(LCTX, uint32_t b, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double w) {
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, container, items, first, n, w, &ps);
    if (!lay_run_shows(&ps)) return 0;
    lay_style bs;
    lay_style_of(LARGS, container, &bs);
    /* the block's ::first-letter and ::first-line, in its first run: the first line laid
       out in the pseudo-element's style (its strut too), the rest as they were */
    lay_style fl, bs1 = bs;
    vector<lay_piece> rest;
    int has_fl = 0;
    if (first == 0 && !(container & LAY_GEN) && container < len(*sx) && (at(*sx, container) & LAY_HAS_OTHER)) {
        int64_t gl = lay_gen_find(gx, container, CSS_PSEUDO_FIRST_LETTER);
        if (gl >= 0) lay_first_letter(LARGS, &ps, (uint32_t)gl, w);
        if (lay_pseudo_style(LARGS, container, CSS_PSEUDO_FIRST_LINE, &fl)) {
            has_fl = 1;
            append_range(rest, data(ps), len(ps));
            lay_first_line_pieces(LARGS, &ps, &bs, &fl);
            bs1.lh = fl.lh;
            bs1.asc = fl.asc;
            bs1.desc = fl.desc;
        }
    }
    vector<uint32_t> open;
    size_t i = 0, np = len(ps);
    double y = 0;
    /* text-indent: the first line of the block's first run, from its start edge */
    double indent = first == 0 ? lay_res(st, bs.indent, w) : 0;
    while (i < np) {
        while (i < np && at(ps, i).kind == P_SPACE && !at(ps, i).keep) i++;
        if (i >= np) break;
        size_t start = i, j = i, end = i;
        double x = indent;
        int forced = 0;
        while (j < np) {
            if (at(ps, j).kind == P_BREAK) { end = j; j++; forced = 1; break; }
            /* the spaces before the next chunk, and the chunk */
            size_t k = j;
            double sw = 0;
            int breakable = 0;
            while (k < np && at(ps, k).kind == P_SPACE) { sw += at(ps, k).w; if (at(ps, k).brk) breakable = 1; k++; }
            if (k >= np || at(ps, k).kind == P_BREAK) { end = k; j = k; continue; }
            size_t c = k;
            double cw = 0;
            int atomic_edge = at(ps, k).kind == P_ATOMIC || (k > start && at(ps, k - 1).kind == P_ATOMIC);
            while (c < np && at(ps, c).kind != P_SPACE && at(ps, c).kind != P_BREAK) {
                if (c > k && (at(ps, c).kind == P_ATOMIC || at(ps, c - 1).kind == P_ATOMIC)) break;
                cw += at(ps, c).w;
                c++;
            }
            int can_break = (breakable || atomic_edge) && end > start;
            if (can_break && x + sw + cw > w + 0.01) { j = k; break; }
            x += sw + cw;
            end = c;
            j = c;
        }
        if (j >= np && !forced) end = np;
        int last = j >= np || forced;
        if (has_fl) y += lay_line(LARGS, b, &bs1, &ps, start, end, w, y, !last, indent, &open);
        else y += lay_line(LARGS, b, &bs, &ps, start, end, w, y, !last, indent, &open);
        indent = 0;
        i = j;
        if (has_fl) {
            /* past the first line: the pieces as their own style has them */
            for (size_t q = i; q < np; q++) ps[q] = at(rest, q);
            has_fl = 0;
        }
        if (forced) {
            /* a break: the next line begins after it, its leading spaces kept if pre */
            continue;
        }
    }
    /* a run that ends in a forced break holds an empty last line in pre text */
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

typedef struct lay_cell { uint32_t el, row, col, span; double mn, mx; } lay_cell;

/* the table's rows (in its row groups too) and their cells, each cell's column and span */
static void lay_table_cells(LCTX, uint32_t table, vector<uint32_t> *rows, vector<lay_cell> *cells, vector<uint32_t> *captions, uint32_t *ncols) {
    *ncols = 0;
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
                lay_cell lc;
                lc.el = cell;
                lc.row = ri;
                lc.col = col;
                long span = lay_attr_int(d, cell, "colspan", 1);
                lc.span = (uint32_t)(span < 1 ? 1 : span > 100 ? 100 : span);
                lc.mn = 0;
                lc.mx = 0;
                col += lc.span;
                push_back(cells, lc);
            }
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
            /* a spanning cell: what its columns lack, shared out evenly */
            double smin = LAY_SPACING * (c.span - 1), smax = smin;
            for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) { smin += (*cmin)[i]; smax += (*cmax)[i]; }
            for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) {
                if (mn > smin) (*cmin)[i] += (mn - smin) / c.span;
                if (mx > smax) (*cmax)[i] += (mx - smax) / c.span;
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
    /* captions above, then each row: its cells laid out in their columns, the row as tall
       as its tallest cell, each cell's content in its middle */
    double y = 0;
    for (size_t k = 0; k < len(captions); k++) {
        lay_mc cmt, cmb;
        int empty;
        uint32_t cb = lay_block(LARGS, b, at(captions, k), t->box[b].w, 0, 0, 0, 0, &cmt, &cmb, &empty);
        lay_place(t, cb, t->box[b].ml * 0 + t->box[cb].ml, y + lay_mc_v(cmt));
        y += lay_mc_v(cmt) + lay_border_height(t, cb) + lay_mc_v(cmb);
    }
    y += LAY_SPACING;
    size_t ci = 0;
    for (size_t r = 0; r < len(rows); r++) {
        uint32_t row_el = at(rows, r);
        uint32_t rb = lay_new_box(LARGS, b, row_el, LAYOUT_BOX_KIND_ROW);
        lay_style rs;
        lay_style_of(LARGS, row_el, &rs);
        t->box[rb].background = rs.bg;
        t->box[rb].w = t->box[b].w;
        t->box[rb].how = LAYOUT_WIDTH_HOW_FILL;
        t->box[rb].avail = t->box[b].w;
        double row_h = rs.height.unit == LAY_PX ? rs.height.v : 0;
        size_t c0 = ci;
        while (ci < len(cells) && at(cells, ci).row == r) {
            lay_cell c = at(cells, ci);
            double x = LAY_SPACING;
            for (uint32_t i = 0; i < c.col && i < ncols; i++) x += at(widths, i) + LAY_SPACING;
            double span_w = LAY_SPACING * (c.span - 1);
            for (uint32_t i = c.col; i < c.col + c.span && i < ncols; i++) span_w += at(widths, i);
            lay_style cs;
            lay_style_of(LARGS, c.el, &cs);
            uint32_t cb = lay_new_box(LARGS, rb, c.el, LAYOUT_BOX_KIND_CELL);
            lay_edges(t, st, cb, &cs, t->box[b].w);
            t->box[cb].mt = t->box[cb].mr = t->box[cb].mb = t->box[cb].ml = 0;
            t->box[cb].w = span_w - t->box[cb].bl - t->box[cb].br - t->box[cb].pl - t->box[cb].pr;
            if (t->box[cb].w < 0) t->box[cb].w = 0;
            t->box[cb].how = LAYOUT_WIDTH_HOW_COLUMN;
            t->box[cb].avail = span_w;
            lay_mc et, eb;
            int empty;
            lay_flow_children(LARGS, cb, c.el, 1, 1, &et, &eb, &empty);
            if (cs.height.unit == LAY_PX && cs.height.v > t->box[cb].h) t->box[cb].h = cs.height.v;
            lay_place(t, cb, x, 0);
            double bh = lay_border_height(t, cb);
            if (bh > row_h) row_h = bh;
            ci++;
        }
        for (size_t k = c0; k < ci; k++) {
            /* each cell as tall as the row, its content in the middle */
            uint32_t cb = 0;
            uint32_t cell_el = at(cells, k).el;
            for (uint32_t x = t->box[rb].first_child; x; x = t->box[x].next) if (t->box[x].node.index == cell_el) cb = x;
            if (!cb) continue;
            double inner = row_h - t->box[cb].bt - t->box[cb].pt - t->box[cb].pb - t->box[cb].bb;
            double dy = (inner - t->box[cb].h) / 2;
            if (dy > 0) lay_shift_content(t, cb, dy);
            t->box[cb].h = inner;
        }
        t->box[rb].h = row_h;
        t->box[rb].x = 0;
        t->box[rb].y = y;
        y += row_h + LAY_SPACING;
    }
    t->box[b].h = s->height.unit == LAY_PX && s->height.v > y ? s->height.v : y;
}

/* ---- block layout ---- */

/* the children of box b (element el's), as blocks or as lines, in b's content box of
   width t->box[b].w; sets b's content height. et and eb: the margins that collapsed
   through b's top and bottom; empty: b holds nothing that takes height */
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, lay_mc *et, lay_mc *eb, int *empty) {
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
        if (len(segs)) h = lay_lines(LARGS, b, el, &items, at(segs, 0).first, at(segs, 0).n, w);
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
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        lay_mc cmt, cmb;
        int cempty;
        uint32_t cb;
        if (sg.block) cb = lay_block(LARGS, b, sg.node, w, 0, 0, 0, 0, &cmt, &cmb, &cempty);
        else {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, w, &ps);
            if (!lay_run_shows(&ps)) continue;
            cb = lay_block(LARGS, b, 0, w, &items, sg.first, sg.n, el, &cmt, &cmb, &cempty);
        }
        if (cempty) {
            /* an empty box: its margins collapse through it, into one */
            lay_mc both = lay_mc_add(cmt, cmb);
            if (first && through_top) *et = lay_mc_add(*et, both);
            else pending = lay_mc_add(pending, both);
            lay_place(t, cb, t->box[cb].ml, cursor);
            continue;
        }
        double top;
        if (first && through_top) { *et = lay_mc_add(*et, cmt); top = 0; }
        else top = cursor + lay_mc_v(lay_mc_add(pending, cmt));
        lay_place(t, cb, t->box[cb].ml, top);
        cursor = top + lay_border_height(t, cb);
        pending = cmb;
        first = 0;
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

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, lay_mc *mt, lay_mc *mb, int *empty) {
    if (!el) {
        /* an anonymous block: the inline run, as lines; no margins of its own */
        uint32_t b = lay_new_box(LARGS, parent, 0, LAYOUT_BOX_KIND_ANONYMOUS);
        t->box[b].w = cw;
        t->box[b].how = LAYOUT_WIDTH_HOW_FILL;
        t->box[b].avail = cw;
        t->box[b].h = lay_lines(LARGS, b, style_el, items, first, n, cw);
        mt->pos = mt->neg = mb->pos = mb->neg = 0;
        *empty = !t->box[b].first_line;
        return b;
    }
    lay_style s;
    lay_style_of(LARGS, el, &s);
    layout_box_kind kind = s.display == D_LIST_ITEM ? LAYOUT_BOX_KIND_LIST_ITEM : s.display == D_TABLE ? LAYOUT_BOX_KIND_TABLE :
                           LAYOUT_BOX_KIND_BLOCK;
    uint32_t b = lay_new_box(LARGS, parent, el, kind);
    lay_edges(t, st, b, &s, cw);
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    if (s.display == D_TABLE) {
        lay_table(LARGS, b, el, &s, cw);
        *mt = lay_mc_of(t->box[b].mt);
        *mb = lay_mc_of(t->box[b].mb);
        *empty = 0;
        return b;
    }
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
    lay_block_width(t, st, b, &s, cw);
    if (content_sized && t->box[b].how == LAYOUT_WIDTH_HOW_DECLARED) t->box[b].how = LAYOUT_WIDTH_HOW_SHRINK_TO_FIT;
    int root = !(el & LAY_GEN) && d->node[el].parent == 1;
    int bfc = root || s.bfc || s.display == D_CELL || s.display == D_CAPTION;
    lay_mc et, eb;
    lay_flow_children(LARGS, b, el, bfc, s.height.unit == LAY_AUTO, &et, &eb, empty);
    if (s.height.unit == LAY_PX) {
        t->box[b].h = s.height.v;
        eb.pos = eb.neg = 0;
        *empty = 0;
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
    resize(styles, len(style_first));
    for (size_t k = 0; k < len(style_first); k++) {
        lay_style ls;
        lay_style_make(style, &fonts, &webface, at(style_first, k), &ls);
        styles[k] = ls;
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
    uint32_t vp = lay_new_box(t, doc, style, net, &fonts, &node_style, &styles, &gens, 0, 0, LAYOUT_BOX_KIND_VIEWPORT);
    t->box[vp].w = width;
    t->box[vp].h = height;
    t->box[vp].how = LAYOUT_WIDTH_HOW_VIEWPORT;
    t->box[vp].avail = width;
    t->box[vp].background = 0xffffffff;
    if (!root) { t->revision++; return LAYOUT_NO_ROOT; }
    if (lay_display_of(t, doc, style, net, &fonts, &node_style, &styles, &gens, root) != D_NONE) {
        lay_mc mt, mb;
        int empty;
        uint32_t rb = lay_block(t, doc, style, net, &fonts, &node_style, &styles, &gens, vp, root, width, 0, 0, 0, 0, &mt, &mb, &empty);
        lay_place(t, rb, t->box[rb].ml, t->box[rb].mt);
        double bottom = t->box[rb].mt + lay_border_height(t, rb) + t->box[rb].mb;
        if (bottom > height) t->box[vp].h = bottom;
    }
    lay_absolute(t, vp, 0, 0);
    t->revision++;
    return LAYOUT_OK;
}
