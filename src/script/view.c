/* src/script/view.c -- what a page's scripts read of its rendering (CSSOM 9, CSSOM
   View 6 and 7): getComputedStyle's values, and the boxes getBoundingClientRect,
   getClientRects, offset* and client* read.

   ON DEMAND. The cascade and the layout run when a script asks and the DOM changed
   since they last ran -- a style sheet is the DOM's too: its element's text, a style
   attribute -- never on every read. A run's results are taken into the host (each
   style's computed values, every box, the lines' fragments) and its stores dropped at
   once; the reads after it are answered from the host until the DOM's revision moves.
   The page's own cascade and layout, after its scripts, are the command's
   (src/shell/main.c), as before.

   RESOLVED VALUES (CSSOM 9.3). width and height are the used ones, in px, of an element
   that has a box of its own (a block, an atomic inline, a table, a cell), its border box
   under box-sizing: border-box; an inline element's and an element not rendered's are
   the computed ones (auto). Margins and paddings are the used ones of a box; line-height
   is normal or the used px. Every other property is the computed value as the CSS module
   writes it, which is CSSOM's serialization: rgb() and rgba() colours, lengths in px.
   A property the cascade does not compute reads as the empty string, as does every
   property of an element in no rendered document.

   GEOMETRY. A box's border box, in page px: the viewport does not scroll, so page and
   viewport coordinates are one. An inline element's client rects are its part of each
   line: the union of its fragments' (an inline background's, an atomic inline's) and its
   descendants' text on that line, a rect a line. What is approximated: an inline
   element's horizontal padding and border are not in its rects, and its rects' height is
   its text's, the font's ascent and descent. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(script);
#include "../css/css.h"
#include "../js/js.h"
#include "script.h"

/* the cascade's property of a CSS name (background-color): its number, or -1. The CSS
   module names a property by its enum's word (background_color), each border side's
   colour its own */
static int sv_prop(const char *name, size_t n) {
    char pn[64];
    for (int p = 0; p < 1000; p++) {
        snprintf(pn, sizeof pn, "%s", css_prop_name((css_prop)p));
        if (pn[0] == '?') return -1;
        if (strlen(pn) != n) continue;
        int same = 1;
        for (size_t i = 0; i < n && same; i++) if ((pn[i] == '_' ? '-' : pn[i]) != name[i]) same = 0;
        if (same) return p;
    }
    return -1;
}

/* a font-family list as CSSOM serializes it (CSS Fonts 4, 6.1): its families, a name of
   several words in double quotes, joined by ", ". The text is a font-family declaration's
   or a font shorthand's, whose list begins after its size (and line height) */
static int sv_space(char c) { return c == ' ' || c == '\t' || c == '\n'; }
static void sv_family(const char *s, size_t n, vector<char> *out) {
    clear(*out);
    static const char *const kw[] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "smaller", "larger" };
    size_t i = 0, k = 0;
    while (k < n) {
        while (k < n && sv_space(s[k])) k++;
        size_t b = k;
        if (k >= n || s[k] == '"' || s[k] == '\'') break;
        while (k < n && !sv_space(s[k]) && s[k] != ',') k++;
        if (k < n && s[k] == ',') break;
        int size = (s[b] >= '0' && s[b] <= '9') || s[b] == '.';
        for (size_t q = 0; q < sizeof kw / sizeof kw[0]; q++) if (k - b == strlen(kw[q]) && memcmp(s + b, kw[q], k - b) == 0) size = 1;
        if (size) i = k;
    }
    while (i < n) {
        while (i < n && (sv_space(s[i]) || s[i] == ',')) i++;
        if (i >= n) break;
        vector<char> fam;
        int quoted = 0;
        if (s[i] == '"' || s[i] == '\'') {
            char q = s[i++];
            while (i < n && s[i] != q) push_back(fam, s[i++]);
            if (i < n) i++;
            quoted = 1;
            while (i < n && s[i] != ',') i++;
        } else {
            size_t b = i;
            while (i < n && s[i] != ',') i++;
            size_t e = i;
            while (e > b && sv_space(s[e - 1])) e--;
            /* a name not quoted: its words, one space between each */
            for (size_t c = b; c < e; c++) {
                if (sv_space(s[c])) { if (len(fam) && fam[len(fam) - 1] != ' ') { push_back(fam, ' '); quoted = 1; } continue; }
                push_back(fam, s[c]);
            }
        }
        if (!len(fam)) continue;
        if (len(*out)) append_range(*out, ", ", 2);
        if (quoted) push_back(*out, '"');
        append_range(*out, data(fam), len(fam));
        if (quoted) push_back(*out, '"');
    }
}

void script_view_update(struct js_host *h) {
    dom_document *d = h->d;
    if (h->view_ok && h->view_rev == dom_revision(d)) return;
    css_style st;
    layout_tree lt;
    css_init(&st, 1);
    layout_init(&lt, 1);
    css_cascade(&st, d, h->nt);
    layout_run(&lt, d, &st, h->nt, h->view_w, h->view_h);
    /* each element's style, each style's values once */
    clear(h->vstyle);
    resize(h->vstyle, (size_t)d->node_len, 0u);
    clear(h->vvals);
    clear(h->vtext);
    vector<uint32_t> run_of;
    for (uint32_t e = 1; e < st.estyle_len; e++) {
        if (!st.estyle[e].live) continue;
        uint32_t x = st.estyle[e].node.index, sid = st.estyle[e].style, first = st.estyle[e].first;
        if (x >= d->node_len) continue;
        if (sid >= len(run_of)) resize(run_of, (size_t)sid + 1, 0u);
        if (!run_of[sid]) {
            run_of[sid] = (uint32_t)len(h->vvals) + 1;
            for (uint32_t c = first; c < st.cval_len && st.cval[c].style == sid; c++) {
                sh_vval v;
                memset(&v, 0, sizeof v);
                v.prop = (uint32_t)st.cval[c].prop;
                v.unit = (uint32_t)st.cval[c].unit;
                v.how = (uint32_t)st.cval[c].how;
                v.num = st.cval[c].num;
                amb_span t = st.cval[c].text;
                v.off = (uint32_t)len(h->vtext);
                uint32_t w = st.cval[c].winner;
                if (st.cval[c].prop == CSS_PROP_FONT_FAMILY && st.cval[c].how == CSS_HOW_DECLARED && w) {
                    /* the family list as declared, which the cascade resolves to its generic family */
                    vector<char> fam, txt;
                    amb_span dt = st.decl[w].text;
                    append_range(txt, text_ptr(&st, dt), dt.len);
                    sv_family(data(txt), len(txt), &fam);
                    append_range(h->vtext, data(fam), len(fam));
                    v.len = (uint32_t)len(fam);
                } else {
                    append_range(h->vtext, text_ptr(&st, t), t.len);
                    v.len = t.len;
                }
                push_back(h->vvals, v);
            }
            sh_vval end;
            memset(&end, 0, sizeof end);
            end.prop = ~0u;
            push_back(h->vvals, end);
        }
        h->vstyle[x] = run_of[sid];
    }
    /* every element's box (its first), and the lines' fragments in their order */
    clear(h->vbox);
    resize(h->vbox, (size_t)d->node_len, 0u);
    clear(h->vboxes);
    clear(h->vfrags);
    for (uint32_t b = 1; b < lt.box_len; b++) {
        if (!lt.box[b].live || lt.box[b].kind == LAYOUT_BOX_KIND_VIEWPORT || lt.box[b].kind == LAYOUT_BOX_KIND_INLINE) continue;
        uint32_t x = lt.box[b].node.index;
        if (!x || x >= d->node_len || h->vbox[x]) continue;
        sh_vbox v;
        v.node = x;
        v.kind = (uint32_t)lt.box[b].kind;
        v.x = lt.box[b].x; v.y = lt.box[b].y; v.w = lt.box[b].w; v.h = lt.box[b].h;
        v.m[0] = lt.box[b].mt; v.m[1] = lt.box[b].mr; v.m[2] = lt.box[b].mb; v.m[3] = lt.box[b].ml;
        v.b[0] = lt.box[b].bt; v.b[1] = lt.box[b].br; v.b[2] = lt.box[b].bb; v.b[3] = lt.box[b].bl;
        v.p[0] = lt.box[b].pt; v.p[1] = lt.box[b].pr; v.p[2] = lt.box[b].pb; v.p[3] = lt.box[b].pl;
        push_back(h->vboxes, v);
        h->vbox[x] = (uint32_t)len(h->vboxes);
    }
    for (uint32_t f = 1; f < lt.frag_len; f++) {
        if (!lt.frag[f].live || lt.frag[f].kind == LAYOUT_FRAG_KIND_MARKER) continue;
        sh_vfrag v;
        v.node = lt.frag[f].node.index;
        v.line = lt.frag[f].line;
        v.x = lt.frag[f].x; v.y = lt.frag[f].y; v.w = lt.frag[f].w; v.h = lt.frag[f].h;
        push_back(h->vfrags, v);
    }
    h->view_rev = dom_revision(d);
    h->view_ok = 1;
    h->view_runs++;
}

/* the element x's computed value of the property p: its index in vvals, or ~0 */
static uint32_t sv_val(const struct js_host *h, uint32_t x, uint32_t p) {
    if (x >= len(h->vstyle) || !h->vstyle[x]) return ~0u;
    for (uint32_t i = h->vstyle[x] - 1; i < len(h->vvals) && h->vvals[i].prop != ~0u; i++) if (h->vvals[i].prop == p) return i;
    return ~0u;
}
static int sv_val_is(const struct js_host *h, uint32_t i, const char *w) {
    size_t n = strlen(w);
    return i != ~0u && h->vvals[i].len == n && memcmp(data(h->vtext) + h->vvals[i].off, w, n) == 0;
}

int script_view_fresh(struct js_host *h) { return h->view_ok && h->view_rev == dom_revision(h->d); }

int script_view_rendered(struct js_host *h, uint32_t x) {
    script_view_update(h);
    const dom_document *d = h->d;
    for (uint32_t y = x; y && y != 1; y = d->node[y].parent) {
        uint32_t v = sv_val(h, y, CSS_PROP_DISPLAY);
        if (v == ~0u || sv_val_is(h, v, "none")) return 0;
    }
    return x && x < len(h->vstyle) && h->vstyle[x];
}

/* the element's box of its own, in vboxes plus 1: 0 for an inline element or one not
   rendered */
static uint32_t sv_box(struct js_host *h, uint32_t x) {
    script_view_update(h);
    return x < len(h->vbox) ? h->vbox[x] : 0;
}

/* a length as a layout unit holds it: a whole number of 1/64 px, toward zero, as Blink
   keeps every length (so 6% of 784px is 47.03125px there), the layout's own rounding
   noise absorbed */
static double sv_lu(double v) {
    if (v >= 0) return floor(v * 64 + 1e-6) / 64;
    return -floor(-v * 64 + 1e-6) / 64;
}
/* a number as CSSOM writes a px value: six significant digits, no -0 */
static void sv_px(vector<char> *out, double v) {
    char buf[64];
    v = sv_lu(v);
    if (v == 0) v = 0;
    int n = snprintf(buf, sizeof buf, "%.6gpx", v);
    if (n > 0) append_range(*out, buf, (size_t)n);
}

int script_view_style(struct js_host *h, uint32_t x, const char *name, size_t n, vector<char> *out) {
    clear(*out);
    script_view_update(h);
    int p = sv_prop(name, n);
    if (p < 0) return 0;
    if (p > CSS_PROP_CUSTOM) {
        /* a flow-relative property (CSS Logical 1): the physical one the element's writing mode and direction make it */
        uint32_t wi = sv_val(h, x, CSS_PROP_WRITING_MODE), di = sv_val(h, x, CSS_PROP_DIRECTION);
        int wm = sv_val_is(h, wi, "vertical-rl") ? CSS_WM_VRL : sv_val_is(h, wi, "vertical-lr") ? CSS_WM_VLR :
                 sv_val_is(h, wi, "sideways-rl") ? CSS_WM_SRL : sv_val_is(h, wi, "sideways-lr") ? CSS_WM_SLR : CSS_WM_HTB;
        p = css_logical_physical(p, wm, sv_val_is(h, di, "rtl"));
    }
    uint32_t i = sv_val(h, x, (uint32_t)p);
    if (i == ~0u) return 1;
    uint32_t b = sv_box(h, x);
    int rendered = b && script_view_rendered(h, x);
    sh_vval v = h->vvals[i];
    if (rendered) {
        sh_vbox bx = h->vboxes[b - 1];
        int border = sv_val_is(h, sv_val(h, x, CSS_PROP_BOX_SIZING), "border-box");
        if (p == CSS_PROP_WIDTH) { sv_px(out, bx.w + (border ? bx.p[1] + bx.p[3] + bx.b[1] + bx.b[3] : 0)); return 1; }
        if (p == CSS_PROP_HEIGHT) { sv_px(out, bx.h + (border ? bx.p[0] + bx.p[2] + bx.b[0] + bx.b[2] : 0)); return 1; }
        /* a margin or a padding: the used one where the computed one is auto or a
           percentage, as Blink resolves them; else the computed length */
        static const int margins[4] = { CSS_PROP_MARGIN_TOP, CSS_PROP_MARGIN_RIGHT, CSS_PROP_MARGIN_BOTTOM, CSS_PROP_MARGIN_LEFT };
        static const int paddings[4] = { CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_RIGHT, CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT };
        int used = v.unit == (uint32_t)CSS_UNIT_AUTO || v.unit == (uint32_t)CSS_UNIT_PERCENT;
        for (int k = 0; k < 4 && used; k++) {
            if (p == margins[k]) { sv_px(out, bx.m[k]); return 1; }
            if (p == paddings[k]) { sv_px(out, bx.p[k]); return 1; }
        }
    }
    if (p == CSS_PROP_FONT_FAMILY) {
        /* the list the nearest element that declared one declared; else the default's name */
        const dom_document *d = h->d;
        uint32_t y = x;
        while (y && v.how == (uint32_t)CSS_HOW_INHERITED) {
            y = d->node[y].parent;
            uint32_t j = sv_val(h, y, (uint32_t)p);
            if (j == ~0u) break;
            v = h->vvals[j];
        }
        if (v.how != (uint32_t)CSS_HOW_DECLARED) { append_range(*out, "\"Times New Roman\"", 17); return 1; }
    }
    if (p == CSS_PROP_LINE_HEIGHT && v.unit == (uint32_t)CSS_UNIT_NUMBER) {
        uint32_t fs = sv_val(h, x, CSS_PROP_FONT_SIZE);
        sv_px(out, v.num * (fs != ~0u ? h->vvals[fs].num : 16));
        return 1;
    }
    append_range(*out, data(h->vtext) + v.off, v.len);
    return 1;
}

int script_view_border_box(struct js_host *h, uint32_t x, double *r) {
    uint32_t b = sv_box(h, x);
    if (!b || !script_view_rendered(h, x)) return 0;
    sh_vbox v = h->vboxes[b - 1];
    r[0] = sv_lu(v.x - v.p[3] - v.b[3]);
    r[1] = sv_lu(v.y - v.p[0] - v.b[0]);
    r[2] = sv_lu(v.w + v.p[1] + v.p[3] + v.b[1] + v.b[3]);
    r[3] = sv_lu(v.h + v.p[0] + v.p[2] + v.b[0] + v.b[2]);
    return 1;
}

int script_view_borders(struct js_host *h, uint32_t x, double *r) {
    uint32_t b = sv_box(h, x);
    if (!b || !script_view_rendered(h, x)) return 0;
    sh_vbox v = h->vboxes[b - 1];
    for (int k = 0; k < 4; k++) r[k] = sv_lu(v.b[k]);
    return 1;
}

uint32_t script_view_rects(struct js_host *h, uint32_t x, double *r, uint32_t max) {
    if (!script_view_rendered(h, x)) return 0;
    if (max && script_view_border_box(h, x, r)) return 1;
    if (sv_box(h, x)) return 1;
    /* an inline element: its part of each line, in the lines' order */
    const dom_document *d = h->d;
    uint32_t n = 0, line = 0;
    for (size_t i = 0; i < len(h->vfrags); i++) {
        sh_vfrag f = h->vfrags[i];
        uint32_t c = f.node;
        while (c && c != x) c = d->node[c].parent;
        if (c != x) continue;
        if (n && f.line == line) {
            if (n > max) continue;
            size_t q = 4 * (size_t)(n - 1);
            double x1 = r[q] + r[q + 2], y1 = r[q + 1] + r[q + 3];
            if (f.x < r[q]) r[q] = f.x;
            if (f.y < r[q + 1]) r[q + 1] = f.y;
            if (f.x + f.w > x1) x1 = f.x + f.w;
            if (f.y + f.h > y1) y1 = f.y + f.h;
            r[q + 2] = x1 - r[q];
            r[q + 3] = y1 - r[q + 1];
            continue;
        }
        n++;
        line = f.line;
        if (n > max) continue;
        r[4 * (n - 1)] = f.x;
        r[4 * (n - 1) + 1] = f.y;
        r[4 * (n - 1) + 2] = f.w;
        r[4 * (n - 1) + 3] = f.h;
    }
    for (uint32_t i = 0; i < 4 * (n < max ? n : max); i++) r[i] = sv_lu(r[i]);
    return n;
}

uint32_t script_view_runs(const struct js_host *h) { return h->view_runs; }

uint32_t script_view_props(void) {
    char pn[64];
    uint32_t p = 0;
    for (; p < 1000; p++) {
        snprintf(pn, sizeof pn, "%s", css_prop_name((css_prop)p));
        if (pn[0] == '?') break;
    }
    return p;
}
