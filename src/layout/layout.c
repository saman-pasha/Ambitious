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

/* the run's context, as parameters: a struct may hold no borrow */
#define LCTX layout_tree *t, const dom_document *d, const css_style *st, const net_loader *nt, const amb_fonts *f, const vector<uint32_t> *sx
#define LARGS t, d, st, nt, f, sx

#define LAY_SPACING 2.0

/* ---- the computed style of an element, as layout reads it ---- */

enum { LAY_PX, LAY_PCT, LAY_AUTO };
typedef struct lay_len { int unit; double v; } lay_len;

enum { D_NONE, D_INLINE, D_BLOCK, D_LIST_ITEM, D_INLINE_BLOCK, D_TABLE, D_ROW_GROUP, D_ROW, D_CELL, D_CAPTION, D_CONTENTS };
enum { WS_NORMAL, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE };
enum { AL_LEFT, AL_RIGHT, AL_CENTER, AL_JUSTIFY };

typedef struct lay_style {
    int display, bfc;
    uint32_t color, bg, bcolor;
    double fs, lh, asc, desc;
    int face, italic, align, ws, deco, list, border_box;
    lay_len m[4], p[4], width, height, maxw;
    double b[4];
} lay_style;

static uint32_t lay_cv(const css_style *st, const vector<uint32_t> *sx, uint32_t x, css_prop p) {
    if (x >= len(*sx)) return 0;
    uint32_t c = at(*sx, x);
    if (!c) return 0;
    for (uint32_t k = c; k < st->cval_len && st->cval[k].node.index == x; k++) if (st->cval[k].prop == p) return k;
    return 0;
}
static int lay_is(const css_style *st, uint32_t c, const char *w) {
    return c && st->cval[c].unit == CSS_UNIT_KEYWORD && names_is(st, st->cval[c].word, w, strlen(w));
}
static lay_len lay_len_at(const css_style *st, uint32_t c) {
    lay_len l;
    l.unit = LAY_PX;
    l.v = 0;
    if (!c) return l;
    if (st->cval[c].unit == CSS_UNIT_AUTO) l.unit = LAY_AUTO;
    else if (st->cval[c].unit == CSS_UNIT_PERCENT) { l.unit = LAY_PCT; l.v = st->cval[c].num; }
    else l.v = st->cval[c].num;
    return l;
}
static double lay_res(lay_len l, double base) { return l.unit == LAY_PCT ? base * l.v / 100 : l.unit == LAY_AUTO ? 0 : l.v; }

static void lay_style_of(LCTX, uint32_t x, lay_style *s) {
    memset(s, 0, sizeof *s);
    s->color = 0x000000ff;
    s->fs = 16;
    s->face = AMB_FACE_SERIF;
    s->width.unit = LAY_AUTO;
    s->height.unit = LAY_AUTO;
    uint32_t c = lay_cv(st, sx, x, CSS_PROP_DISPLAY);
    if (!c) { s->display = D_NONE; return; }
    if (lay_is(st, c, "none") || lay_is(st, c, "table-column") || lay_is(st, c, "table-column-group")) s->display = D_NONE;
    else if (lay_is(st, c, "inline")) s->display = D_INLINE;
    else if (lay_is(st, c, "list-item")) s->display = D_LIST_ITEM;
    else if (lay_is(st, c, "inline-block") || lay_is(st, c, "inline-flex") || lay_is(st, c, "inline-grid") || lay_is(st, c, "inline-table"))
        s->display = D_INLINE_BLOCK;
    else if (lay_is(st, c, "table")) s->display = D_TABLE;
    else if (lay_is(st, c, "table-row-group") || lay_is(st, c, "table-header-group") || lay_is(st, c, "table-footer-group"))
        s->display = D_ROW_GROUP;
    else if (lay_is(st, c, "table-row")) s->display = D_ROW;
    else if (lay_is(st, c, "table-cell")) s->display = D_CELL;
    else if (lay_is(st, c, "table-caption")) s->display = D_CAPTION;
    else if (lay_is(st, c, "contents")) s->display = D_CONTENTS;
    else {
        s->display = D_BLOCK;
        s->bfc = lay_is(st, c, "flow-root") || lay_is(st, c, "flex") || lay_is(st, c, "grid");
    }
    c = lay_cv(st, sx, x, CSS_PROP_COLOR);
    if (c) s->color = st->cval[c].color;
    c = lay_cv(st, sx, x, CSS_PROP_BACKGROUND_COLOR);
    if (c) s->bg = st->cval[c].color;
    c = lay_cv(st, sx, x, CSS_PROP_BORDER_COLOR);
    s->bcolor = c ? st->cval[c].color : s->color;
    c = lay_cv(st, sx, x, CSS_PROP_FONT_SIZE);
    if (c) s->fs = st->cval[c].num;
    c = lay_cv(st, sx, x, CSS_PROP_FONT_WEIGHT);
    int bold = c && st->cval[c].num >= 600;
    c = lay_cv(st, sx, x, CSS_PROP_FONT_FAMILY);
    int family = lay_is(st, c, "sans-serif") ? 0 : lay_is(st, c, "monospace") ? 2 : 1;
    s->face = amb_face_for(family, bold);
    c = lay_cv(st, sx, x, CSS_PROP_FONT_STYLE);
    s->italic = lay_is(st, c, "italic") || lay_is(st, c, "oblique");
    double gap;
    amb_face_metrics(f, s->face, s->fs, &s->asc, &s->desc, &gap);
    c = lay_cv(st, sx, x, CSS_PROP_LINE_HEIGHT);
    if (c && st->cval[c].unit == CSS_UNIT_NUMBER) s->lh = st->cval[c].num * s->fs;
    else if (c && st->cval[c].unit == CSS_UNIT_PX) s->lh = st->cval[c].num;
    else s->lh = s->asc + s->desc + gap;
    c = lay_cv(st, sx, x, CSS_PROP_TEXT_ALIGN);
    s->align = lay_is(st, c, "right") || lay_is(st, c, "end") ? AL_RIGHT : lay_is(st, c, "center") ? AL_CENTER :
               lay_is(st, c, "justify") ? AL_JUSTIFY : AL_LEFT;
    c = lay_cv(st, sx, x, CSS_PROP_WHITE_SPACE);
    s->ws = lay_is(st, c, "pre") ? WS_PRE : lay_is(st, c, "nowrap") ? WS_NOWRAP : lay_is(st, c, "pre-wrap") || lay_is(st, c, "break-spaces") ? WS_PRE_WRAP :
            lay_is(st, c, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
    c = lay_cv(st, sx, x, CSS_PROP_TEXT_DECORATION);
    s->deco = lay_is(st, c, "underline") ? 1 : lay_is(st, c, "overline") ? 2 : lay_is(st, c, "line-through") ? 4 : 0;
    c = lay_cv(st, sx, x, CSS_PROP_LIST_STYLE_TYPE);
    s->list = lay_is(st, c, "none") ? 0 : lay_is(st, c, "circle") ? 2 : lay_is(st, c, "square") ? 3 :
              lay_is(st, c, "decimal") || lay_is(st, c, "decimal-leading-zero") ? 4 :
              lay_is(st, c, "lower-alpha") || lay_is(st, c, "lower-latin") || lay_is(st, c, "lower-greek") ? 5 :
              lay_is(st, c, "upper-alpha") || lay_is(st, c, "upper-latin") ? 6 : lay_is(st, c, "lower-roman") ? 7 :
              lay_is(st, c, "upper-roman") ? 8 : 1;
    for (int k = 0; k < 4; k++) {
        s->m[k] = lay_len_at(st, lay_cv(st, sx, x, (css_prop)(CSS_PROP_MARGIN_TOP + k)));
        s->p[k] = lay_len_at(st, lay_cv(st, sx, x, (css_prop)(CSS_PROP_PADDING_TOP + k)));
        c = lay_cv(st, sx, x, (css_prop)(CSS_PROP_BORDER_TOP_WIDTH + k));
        s->b[k] = c ? st->cval[c].num : 0;
        if (s->b[k] > 0 && s->b[k] < 1) s->b[k] = 1;         /* a border shows at least a pixel */
    }
    s->width = lay_len_at(st, lay_cv(st, sx, x, CSS_PROP_WIDTH));
    s->height = lay_len_at(st, lay_cv(st, sx, x, CSS_PROP_HEIGHT));
    s->border_box = lay_is(st, lay_cv(st, sx, x, CSS_PROP_BOX_SIZING), "border-box");
    c = lay_cv(st, sx, x, CSS_PROP_MAX_WIDTH);
    s->maxw.unit = LAY_AUTO;
    if (c && !lay_is(st, c, "none")) s->maxw = lay_len_at(st, c);
    if (s->border_box) {
        /* a declared height is its border box's: the content's is what is left */
        double vedges = s->b[0] + s->b[2] + (s->p[0].unit == LAY_PX ? s->p[0].v : 0) + (s->p[2].unit == LAY_PX ? s->p[2].v : 0);
        if (s->height.unit == LAY_PX) { s->height.v -= vedges; if (s->height.v < 0) s->height.v = 0; }
    }
    if (s->height.unit == LAY_PCT) s->height.unit = LAY_AUTO;   /* against no definite height */
}

/* ---- the DOM, as layout walks it ---- */

static int lay_is_named(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && names_is(d, d->node[x].name, name, strlen(name));
}
/* an element drawn by its own content rules: an image, a form control, an embedding */
static int lay_replaced(const dom_document *d, uint32_t x) {
    return lay_is_named(d, x, "img") || lay_is_named(d, x, "input") || lay_is_named(d, x, "textarea") ||
           lay_is_named(d, x, "select") || lay_is_named(d, x, "iframe") || lay_is_named(d, x, "video") ||
           lay_is_named(d, x, "canvas") || lay_is_named(d, x, "embed") || lay_is_named(d, x, "object") ||
           lay_is_named(d, x, "svg");
}
static uint32_t lay_attr(const dom_document *d, uint32_t x, const char *name) {
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
    if (d->node[x].kind == DOM_NODE_KIND_TEXT) return D_INLINE;
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return D_NONE;
    lay_style s;
    lay_style_of(LARGS, x, &s);
    if (s.display == D_INLINE && lay_replaced(d, x)) return D_INLINE_BLOCK;
    if (lay_is_named(d, x, "input")) {
        uint32_t a = lay_attr(d, x, "type");
        if (a && d->attr[a].value.len == 6 && memcmp(text_ptr(d, d->attr[a].value), "hidden", 6) == 0) return D_NONE;
    }
    return s.display;
}
static int lay_block_level(int dsp) {
    return dsp == D_BLOCK || dsp == D_LIST_ITEM || dsp == D_TABLE || dsp == D_ROW_GROUP || dsp == D_ROW || dsp == D_CELL || dsp == D_CAPTION;
}
/* an inline element with a block-level box inside it */
static int lay_holds_block(LCTX, uint32_t x) {
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
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items) {
    for (uint32_t c = d->node[parent].first_child; c; c = d->node[c].next_sibling) {
        int kind = d->node[c].kind;
        if (kind == DOM_NODE_KIND_TEXT) { lay_item(segs, items, c); continue; }
        if (kind != DOM_NODE_KIND_ELEMENT) continue;
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_NONE) continue;
        if (dsp == D_CONTENTS || (dsp == D_INLINE && lay_holds_block(LARGS, c))) { lay_collect(LARGS, c, segs, items); continue; }
        if (lay_block_level(dsp)) {
            lay_seg s;
            s.node = c;
            s.first = 0;
            s.n = 0;
            s.block = 1;
            push_back(segs, s);
            continue;
        }
        lay_item(segs, items, c);
    }
}

/* ---- pieces: an inline run as words, spaces, breaks, atomic boxes and inline edges ---- */

enum { P_WORD, P_SPACE, P_BREAK, P_ATOMIC, P_OPEN, P_CLOSE };
typedef struct lay_piece {
    uint8_t kind, face, italic, deco, brk, keep;   /* brk: a space a line may break at; keep: a kept (pre) space */
    uint32_t node, elem;                           /* the text node, or the element; and the text's element */
    uint32_t off, len;                             /* a word's (or a kept space's) bytes of its text node */
    double w, size, lh, asc, desc;
    uint32_t color, bg;
    double aw, ah;                                 /* an atomic box's content width (and a replaced one's height) */
} lay_piece;

static void lay_piece_style(lay_piece *p, const lay_style *s) {
    p->face = (uint8_t)s->face;
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
                if (s->width.unit != LAY_AUTO && s->height.unit == LAY_AUTO) { *w = lay_res(s->width, cw); *h = *w * dh / dw; return; }
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
        *w = s->width.unit == LAY_AUTO ? dw : lay_res(s->width, cw);
        *h = s->height.unit == LAY_AUTO ? dh : s->height.v;
        return;
    }
    double box_edges = s->b[1] + s->b[3] + lay_res(s->p[1], cw) + lay_res(s->p[3], cw);
    if (s->width.unit != LAY_AUTO) {
        *w = lay_res(s->width, cw) - (s->border_box ? box_edges : 0);
        if (*w < 0) *w = 0;
        return;
    }
    double mn, mx;
    lay_intrinsic(LARGS, el, cw, &mn, &mx);
    double edges = lay_res(s->m[1], cw) + lay_res(s->m[3], cw) + box_edges;
    mn -= edges;
    mx -= edges;
    double avail = cw - edges;
    double v = avail > mn ? avail : mn;
    *w = v < mx ? v : mx;
    if (*w < 0) *w = 0;
}

static void lay_text_pieces(LCTX, uint32_t node, int deco, vector<lay_piece> *ps, int *sp) {
    uint32_t pe = d->node[node].parent;
    lay_style s;
    lay_style_of(LARGS, pe, &s);
    amb_span span = d->node[node].data;
    const char *tx = text_ptr(d, span);
    size_t n = span.len, i = 0;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, &s);
    p.node = node;
    p.elem = pe;
    p.deco = (uint8_t)deco;
    double space = amb_advance(f, s.face, ' ', s.fs);
    int collapse = s.ws == WS_NORMAL || s.ws == WS_NOWRAP || s.ws == WS_PRE_LINE;
    while (i < n) {
        char c = tx[i];
        int white = c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\n';
        if (c == '\n' && s.ws != WS_NORMAL && s.ws != WS_NOWRAP) {
            /* a kept newline: a forced break, and in pre-line the spaces before it go */
            if (s.ws == WS_PRE_LINE && len(*ps) && back(*ps).kind == P_SPACE && !back(*ps).keep) pop_back(ps);
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
                p.brk = (uint8_t)(s.ws != WS_NOWRAP);
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
            p.brk = (uint8_t)(s.ws == WS_PRE_WRAP);
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
        p.w = amb_text_width(f, s.face, s.fs, tx + i, j - i);
        p.brk = 0;
        p.keep = 0;
        push_back(ps, p);
        *sp = 0;
        i = j;
    }
}

/* the pieces of an inline-level node: text, an inline element (its edges around its
   children's pieces), a forced break, an atomic box */
static void lay_node_pieces(LCTX, uint32_t node, int deco, double cw, vector<lay_piece> *ps, int *sp) {
    if (d->node[node].kind == DOM_NODE_KIND_TEXT) { lay_text_pieces(LARGS, node, deco, ps, sp); return; }
    if (d->node[node].kind != DOM_NODE_KIND_ELEMENT) return;
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
        p.w = lay_res(s.m[3], cw) + s.b[3] + lay_res(s.p[3], cw) + w + lay_res(s.p[1], cw) + s.b[1] + lay_res(s.m[1], cw);
        push_back(ps, p);
        *sp = 0;
        return;
    }
    int inner = deco | s.deco;
    p.kind = P_OPEN;
    p.bg = s.bg;
    p.w = lay_res(s.m[3], cw) + s.b[3] + lay_res(s.p[3], cw);
    push_back(ps, p);
    for (uint32_t c = d->node[node].first_child; c; c = d->node[c].next_sibling) lay_node_pieces(LARGS, c, inner, cw, ps, sp);
    p.kind = P_CLOSE;
    p.w = lay_res(s.p[1], cw) + s.b[1] + lay_res(s.m[1], cw);
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

static double lay_edges_px(const lay_style *s, double cw) {
    return (s->m[1].unit == LAY_AUTO ? 0 : lay_res(s->m[1], cw)) + (s->m[3].unit == LAY_AUTO ? 0 : lay_res(s->m[3], cw)) +
           s->b[1] + s->b[3] + lay_res(s->p[1], cw) + lay_res(s->p[3], cw);
}

static void lay_table_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    double edges = lay_edges_px(&s, cw);
    *mn = 0;
    *mx = 0;
    if (s.width.unit == LAY_PX) {
        double box_edges = s.b[1] + s.b[3] + lay_res(s.p[1], cw) + lay_res(s.p[3], cw);
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
    if (el) t->box[h.index].node = node_handle(d, el);
    if (parent) children_append(t, parent, h.index);
    return h.index;
}

/* a box's margins (vertical ones, and horizontal ones that are not auto), borders,
   paddings and colours, against its containing block's width cw */
static void lay_edges(layout_tree *t, uint32_t b, const lay_style *s, double cw) {
    t->box[b].mt = lay_res(s->m[0], cw);
    t->box[b].mb = lay_res(s->m[2], cw);
    t->box[b].ml = lay_res(s->m[3], cw);
    t->box[b].mr = lay_res(s->m[1], cw);
    t->box[b].bt = s->b[0];
    t->box[b].br = s->b[1];
    t->box[b].bb = s->b[2];
    t->box[b].bl = s->b[3];
    t->box[b].pt = lay_res(s->p[0], cw);
    t->box[b].pr = lay_res(s->p[1], cw);
    t->box[b].pb = lay_res(s->p[2], cw);
    t->box[b].pl = lay_res(s->p[3], cw);
    t->box[b].background = s->bg;
    t->box[b].border_color = s->bcolor;
    t->box[b].avail = cw;
}

/* a block-level box's width and horizontal margins (CSS 2.1, 10.3.3): a declared
   width is the border box's under box-sizing: border-box; max-width holds it narrower */
static void lay_block_width(layout_tree *t, uint32_t b, const lay_style *s, double cw) {
    double edges = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    int mla = s->m[3].unit == LAY_AUTO, mra = s->m[1].unit == LAY_AUTO;
    double ml = mla ? 0 : t->box[b].ml, mr = mra ? 0 : t->box[b].mr, w;
    int declared = s->width.unit != LAY_AUTO;
    if (!declared) {
        w = cw - ml - mr - edges;
        t->box[b].how = LAYOUT_WIDTH_HOW_FILL;
    } else {
        w = lay_res(s->width, cw) - (s->border_box ? edges : 0);
        t->box[b].how = s->width.unit == LAY_PCT ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
    }
    if (w < 0) w = 0;
    if (s->maxw.unit != LAY_AUTO) {
        double mw = lay_res(s->maxw, cw) - (s->border_box ? edges : 0);
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

/* an atomic inline's box, laid out: a replaced element's content, or an inline-block's
   children as a new formatting context */
static uint32_t lay_atomic(LCTX, uint32_t parent, const lay_piece *p, double cw) {
    uint32_t el = p->node;
    lay_style s;
    lay_style_of(LARGS, el, &s);
    int replaced = lay_replaced(d, el);
    uint32_t b = lay_new_box(LARGS, parent, el, replaced ? LAYOUT_BOX_KIND_REPLACED : LAYOUT_BOX_KIND_INLINE_BLOCK);
    lay_edges(t, b, &s, cw);
    if (s.m[3].unit == LAY_AUTO) t->box[b].ml = 0;
    if (s.m[1].unit == LAY_AUTO) t->box[b].mr = 0;
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    t->box[b].w = p->aw;
    t->box[b].how = replaced || s.width.unit == LAY_AUTO ? (replaced ? LAYOUT_WIDTH_HOW_INTRINSIC : LAYOUT_WIDTH_HOW_SHRINK_TO_FIT)
                                                         : (s.width.unit == LAY_PCT ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED);
    if (replaced) { t->box[b].h = p->ah > 0 ? p->ah : 0; return b; }
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

/* the text of pieces [a, b) of one text node, white space as laid out, into out */
static void lay_piece_text(const dom_document *d, const vector<lay_piece> *ps, size_t a, size_t b, vector<char> *out) {
    for (size_t k = a; k < b; k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_SPACE && !p.keep) { push_back(out, ' '); continue; }
        if (p.kind != P_WORD && p.kind != P_SPACE) continue;
        amb_span sp = d->node[p.node].data;
        const char *tx = text_ptr(d, sp);
        for (uint32_t i = p.off; i < p.off + p.len && i < sp.len; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);
    }
}

/* one line of pieces [a, e) in box b at y, w wide; returns its height. open: the inline
   elements open at its start, which it leaves as those open at its end */
static double lay_line(LCTX, uint32_t b, const lay_style *bs, const vector<lay_piece> *ps, size_t a, size_t e, double w, double y,
                       int justify_ok, vector<uint32_t> *open) {
    /* trailing collapsible spaces hang: they take no width */
    size_t end = e;
    while (end > a && at(*ps, end - 1).kind == P_SPACE && !at(*ps, end - 1).keep) end--;
    /* the pieces' x and the line's vertical extent */
    vector<double> xs;
    vector<uint32_t> atoms;
    double x = 0, above, below, hl = (bs->lh - (bs->asc + bs->desc)) / 2;
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
                t->frag[fr].node = node_handle(d, elx);
                t->frag[fr].x = x0 + (from < 0 ? 0 : lay_res(es.m[3], w));
                t->frag[fr].w = px + p.w - lay_res(es.m[1], w) - t->frag[fr].x;
                t->frag[fr].y = baseline - es.asc - lay_res(es.p[0], w) - es.b[0];
                t->frag[fr].h = es.asc + es.desc + lay_res(es.p[0], w) + lay_res(es.p[2], w) + es.b[0] + es.b[2];
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
        double x0 = from < 0 ? 0 : from + lay_res(es.m[3], w);
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
        t->frag[fr].node = node_handle(d, elx);
        t->frag[fr].x = x0;
        t->frag[fr].w = line_w + shift + gap - x0;
        t->frag[fr].y = baseline - es.asc - lay_res(es.p[0], w) - es.b[0];
        t->frag[fr].h = es.asc + es.desc + lay_res(es.p[0], w) + lay_res(es.p[2], w) + es.b[0] + es.b[2];
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
            t->frag[fr].node = node_handle(d, p.node);
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
        /* a run: this text node's words and spaces that follow, on this line */
        size_t r = k + 1;
        double rw = p.w;
        while (r < end && extra == 0) {
            lay_piece q = at(*ps, r);
            if ((q.kind != P_WORD && q.kind != P_SPACE) || q.node != p.node) break;
            rw += q.w;
            r++;
        }
        while (r > k + 1 && at(*ps, r - 1).kind == P_SPACE && !at(*ps, r - 1).keep) { r--; rw -= at(*ps, r).w; }
        vector<char> txt;
        lay_piece_text(d, ps, k, r, &txt);
        amb_span sp = text_add(t, data(txt), len(txt));
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_TEXT;
        t->frag[fr].node = node_handle(d, p.node);
        t->frag[fr].text = sp;
        t->frag[fr].x = px;
        t->frag[fr].y = baseline - p.asc;
        t->frag[fr].w = rw;
        t->frag[fr].h = p.asc + p.desc;
        t->frag[fr].baseline = baseline;
        t->frag[fr].face = (layout_face)p.face;
        t->frag[fr].size = p.size;
        t->frag[fr].italic = p.italic;
        t->frag[fr].color = p.color;
        t->frag[fr].decoration = p.deco;
        frags_append(t, ln, fr);
        k = r;
    }
    return height;
}

/* the lines of the inline run [first, first + n) of items in box b, w wide; returns their height */
static double lay_lines(LCTX, uint32_t b, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double w) {
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, container, items, first, n, w, &ps);
    if (!lay_run_shows(&ps)) return 0;
    lay_style bs;
    lay_style_of(LARGS, container, &bs);
    vector<uint32_t> open;
    size_t i = 0, np = len(ps);
    double y = 0;
    while (i < np) {
        while (i < np && at(ps, i).kind == P_SPACE && !at(ps, i).keep) i++;
        if (i >= np) break;
        size_t start = i, j = i, end = i;
        double x = 0;
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
        y += lay_line(LARGS, b, &bs, &ps, start, end, w, y, !last, &open);
        i = j;
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

static void lay_marker(LCTX, uint32_t b, uint32_t el, const lay_style *s) {
    if (s->list == 0) return;
    uint32_t owner = 0, ln = lay_first_line(t, b, &owner);
    if (!ln) return;
    char txt[32];
    size_t tn;
    uint32_t cp = 0;
    if (s->list == 1) cp = 0x2022;
    else if (s->list == 2) cp = 0x25E6;
    else if (s->list == 3) cp = 0x25AA;
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
        if (s->list == 4) snprintf(num, sizeof num, "%ld", ord);
        else if (s->list == 5 || s->list == 6) {
            long v = ord;
            char rev[16];
            size_t k = 0;
            while (v > 0 && k < sizeof rev) { v--; rev[k++] = (char)((s->list == 5 ? 'a' : 'A') + v % 26); v /= 26; }
            for (size_t i = 0; i < k; i++) num[i] = rev[k - 1 - i];
            num[k] = 0;
        } else lay_roman(ord, s->list == 8, num, sizeof num);
        tn = (size_t)snprintf(txt, sizeof txt, "%s.", num);
    }
    /* outside the content, its right end half an em before the list item's content box */
    double mw = amb_text_width(f, s->face, s->fs, txt, tn);
    double off_x = 0;
    for (uint32_t x = owner; x && x != b; x = t->box[x].parent) off_x += t->box[x].x;
    amb_span sp = text_add(t, txt, tn);
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    double base = t->line[ln].baseline;
    t->frag[fr].kind = LAYOUT_FRAG_KIND_MARKER;
    t->frag[fr].node = node_handle(d, el);
    t->frag[fr].text = sp;
    t->frag[fr].x = -off_x - t->line[ln].x - mw - s->fs * 0.5;
    t->frag[fr].y = base - s->asc;
    t->frag[fr].w = mw;
    t->frag[fr].h = s->asc + s->desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (layout_face)s->face;
    t->frag[fr].size = s->fs;
    t->frag[fr].color = s->color;
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
        bw = lay_res(s->width, cw);
        t->box[b].how = s->width.unit == LAY_PCT ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
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
        t->box[rb].border_color = rs.bcolor;
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
            lay_edges(t, cb, &cs, t->box[b].w);
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
    lay_edges(t, b, &s, cw);
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    if (s.display == D_TABLE) {
        lay_table(LARGS, b, el, &s, cw);
        *mt = lay_mc_of(t->box[b].mt);
        *mb = lay_mc_of(t->box[b].mb);
        *empty = 0;
        return b;
    }
    lay_block_width(t, b, &s, cw);
    int root = d->node[el].parent == 1;
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
    vector<uint32_t> first_cval;
    resize(first_cval, doc->node_len);
    for (uint32_t c = 1; c < style->cval_len; c++) {
        uint32_t x = style->cval[c].node.index;
        if (x < doc->node_len && !at(first_cval, x)) first_cval[x] = c;
    }
    /* the root element: the document's element child */
    uint32_t root = 0;
    for (uint32_t c = doc->node_len > 1 ? doc->node[1].first_child : 0; c; c = doc->node[c].next_sibling)
        if (doc->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    uint32_t vp = lay_new_box(t, doc, style, net, &fonts, &first_cval, 0, 0, LAYOUT_BOX_KIND_VIEWPORT);
    t->box[vp].w = width;
    t->box[vp].h = height;
    t->box[vp].how = LAYOUT_WIDTH_HOW_VIEWPORT;
    t->box[vp].avail = width;
    t->box[vp].background = 0xffffffff;
    if (!root) { t->revision++; return LAYOUT_NO_ROOT; }
    if (lay_display_of(t, doc, style, net, &fonts, &first_cval, root) != D_NONE) {
        lay_mc mt, mb;
        int empty;
        uint32_t rb = lay_block(t, doc, style, net, &fonts, &first_cval, vp, root, width, 0, 0, 0, 0, &mt, &mb, &empty);
        lay_place(t, rb, t->box[rb].ml, t->box[rb].mt);
        double bottom = t->box[rb].mt + lay_border_height(t, rb) + t->box[rb].mb;
        if (bottom > height) t->box[vp].h = bottom;
    }
    lay_absolute(t, vp, 0, 0);
    t->revision++;
    return LAYOUT_OK;
}
