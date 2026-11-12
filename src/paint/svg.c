/* src/paint/svg.c -- SVG drawn (svg.h): an svg element's subtree made a scene, the scene
   drawn, and an SVG document's bytes read, made a scene and drawn as an image.

   THE SCENE (amb_svg_scene). The svg element's subtree is walked with each element's
   computed values (the cascade's: presentation attributes, style attributes, the page's
   sheets). Its inherited SVG properties are taken from the parent the walk came from, so
   that a use element's tree inherits from the use (SVG 2, 5.6). Drawn: g, a, switch (its
   first child), nested svg (a viewport: x, y, width, height, viewBox, preserveAspectRatio,
   clipped unless its overflow is visible), use (href or xlink:href, x and y; a symbol or an
   svg as a viewport its width and height make), rect (rx, ry), circle, ellipse, line,
   polyline, polygon, path (every command; an arc made cubics, its centre found as SVG 2's
   appendix B finds it, a quarter turn a cubic at most). Never drawn of themselves: defs,
   symbol, clipPath, the gradients, stop, style, title, desc, metadata, and what is not
   known. Transforms: the transform attribute (matrix, translate, scale, rotate about a
   centre, skewX, skewY), a declared CSS transform over it (translate, scale, rotate, skew,
   matrix: px and angles), transform-origin 0 0 as Chromium's sheet has it for SVG
   elements. Paints: none, a colour, currentcolor (each element's own colour). Strokes:
   stroke-width (a percentage of the viewport's normalized diagonal), linecap, linejoin
   (miter-clip and arcs drawn as miter), miterlimit, dasharray and dashoffset. Opacity: a
   group of its own.
   Paints also: url(#id) of a linearGradient or radialGradient (gradientUnits,
   gradientTransform, spreadMethod, its stops' offsets, stop-color and stop-opacity, every
   attribute and the stops inherited through href) and a fallback. Clip-path: url(#id) of a
   clipPath (clipPathUnits, its transform, its shapes' and uses' clip-rule, a clip-path on
   the clipPath itself), a group of its own.
   Text: text and tspan (x, y, dx, dy lists, text-anchor by chunks, font-size, -family,
   -weight, -style, letter- and word-spacing, fill), its glyphs placed by the built-in
   faces' advances and kerning.

   THE DRAWING (amb_svg_draw). Every coordinate is mapped to the device's px, an svg's
   content placed at whole CSS px as Chromium places it. Curves are flattened at the
   device's px (0.05 px at most from the curve, a point just inside each end, so that a
   join there meets the curve's own tangent, as Skia's does). A stroke is made in user
   space, as Skia makes one: the union of its segments' quads, its joins (a miter within its
   limit, else a bevel; round) and its caps (butt, round, square; a zero-length subpath's
   upright), each piece turned to wind one way so that the non-zero rule unites them. Its
   dashes are cut first, where SkDashPath cuts them over SkContourMeasure's chords (each
   curve halved while it bends more than half a device px from its chord), a closed
   subpath's last dash joined to its first. A stroke no wider than a device px is drawn as
   Skia draws a hairline: a px wide, its alpha its width. A shape's coverage is found over
   sixteen rows a pixel, each span's ends exact across it, under its fill rule, then
   painted (a gradient's colour found at each pixel's centre, its stops interpolated
   unpremultiplied, as Blink asks Skia for SVG's) and blended over what its layer holds. A
   group with an opacity or a clip is drawn in a layer of its own and blended down, its
   clip's shapes' coverage (their union) a mask: a clip-path's times its bounding box's,
   as Blink clips by both and Skia multiplies the two (a rectangle along the axes clips as
   itself alone).
   Glyphs are drawn from font.c's masks at their device size, placed as the page's text is
   (a quarter of a px across, a whole px down); a rotated or skewed run is drawn upright.

   Not done: SVG documents as images (this package's next step); filters, masks, markers, patterns, images inside SVG, foreignObject,
   SMIL animation, textPath, a text's stroke, its rotate, textLength and baseline
   properties, paint-order, vector-effect, a CSS clip-path's basic shapes, and fonts other
   than the built-in faces in SVG text. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "font.h"

amb_interface(dom);
amb_interface(css);
amb_interface(layout);
amb_interface(net);

#include "paint.h"
#include "image.h"
#include "svg.h"
#include "../css/css.h"

/* ---- matrices: x' = a x + c y + e, y' = b x + d y + f ---- */

typedef struct sv_mat { double a, b, c, d, e, f; } sv_mat;

static sv_mat sv_make(double a, double b, double c, double d, double e, double f) {
    sv_mat m;
    m.a = a; m.b = b; m.c = c; m.d = d; m.e = e; m.f = f;
    return m;
}
static sv_mat sv_id(void) { return sv_make(1, 0, 0, 1, 0, 0); }
/* p after q: q applied first */
static sv_mat sv_mul(sv_mat p, sv_mat q) {
    return sv_make(p.a * q.a + p.c * q.b, p.b * q.a + p.d * q.b, p.a * q.c + p.c * q.d, p.b * q.c + p.d * q.d, p.a * q.e + p.c * q.f + p.e,
                   p.b * q.e + p.d * q.f + p.f);
}
static int sv_inv(sv_mat m, sv_mat *out) {
    double det = m.a * m.d - m.b * m.c;
    if (fabs(det) < 1e-18) return 0;
    double k = 1 / det;
    *out = sv_make(m.d * k, -m.b * k, -m.c * k, m.a * k, (m.c * m.f - m.d * m.e) * k, (m.b * m.e - m.a * m.f) * k);
    return 1;
}
static double sv_ax(sv_mat m, double x, double y) { return m.a * x + m.c * y + m.e; }
static double sv_ay(sv_mat m, double x, double y) { return m.b * x + m.d * y + m.f; }
/* how far the matrix stretches a length at most */
static double sv_scale(sv_mat m) {
    double p = m.a * m.a + m.b * m.b, q = m.c * m.c + m.d * m.d, r = m.a * m.c + m.b * m.d;
    double mid = (p + q) / 2, dif = sqrt((p - q) * (p - q) / 4 + r * r);
    return sqrt(mid + dif);
}

/* ---- numbers, as SVG's attributes and path data write them ---- */

static int sv_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static size_t sv_skip_ws(const char *s, size_t n, size_t i) {
    while (i < n && sv_ws(s[i])) i++;
    return i;
}
/* white space, perhaps a comma, white space */
static size_t sv_skip_sep(const char *s, size_t n, size_t i) {
    i = sv_skip_ws(s, n, i);
    if (i < n && s[i] == ',') i = sv_skip_ws(s, n, i + 1);
    return i;
}
/* a number from s[*i]: its sign, digits, point and exponent (an e not followed by digits is
   no exponent); 1 and *i past it */
static int sv_number(const char *s, size_t n, size_t *i, double *out) {
    size_t p = *i;
    int neg = 0, digits = 0;
    if (p < n && (s[p] == '+' || s[p] == '-')) { neg = s[p] == '-'; p++; }
    double v = 0;
    while (p < n && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); p++; digits++; }
    if (p < n && s[p] == '.') {
        p++;
        double k = 0.1;
        while (p < n && s[p] >= '0' && s[p] <= '9') { v += (s[p] - '0') * k; k *= 0.1; p++; digits++; }
    }
    if (!digits) return 0;
    if (p < n && (s[p] == 'e' || s[p] == 'E')) {
        size_t q = p + 1;
        int eneg = 0;
        if (q < n && (s[q] == '+' || s[q] == '-')) { eneg = s[q] == '-'; q++; }
        if (q < n && s[q] >= '0' && s[q] <= '9') {
            int ex = 0;
            while (q < n && s[q] >= '0' && s[q] <= '9') { if (ex < 400) ex = ex * 10 + (s[q] - '0'); q++; }
            v *= pow(10.0, eneg ? -ex : ex);
            p = q;
        }
    }
    /* as a float holds it, at most: past that, none (Chromium's numbers are floats) */
    if (!(v <= 3.4e38)) return 0;
    *out = neg ? -v : v;
    *i = p;
    return 1;
}
/* a path's flag: 0 or 1, a character alone */
static int sv_flag(const char *s, size_t n, size_t *i, int *out) {
    if (*i < n && (s[*i] == '0' || s[*i] == '1')) { *out = s[*i] == '1'; (*i)++; return 1; }
    return 0;
}

/* ---- path data (SVG 2, 9.3): read into absolute moves, lines, curves and closes ----
   A curve is a cubic (C), a quadratic (Q), or a cubic that stands for an elliptical arc's
   piece (K): drawn as a cubic, measured as Skia measures the conic it draws an arc with */

enum { SV_M = 'M', SV_L = 'L', SV_C = 'C', SV_Q = 'Q', SV_K = 'K', SV_Z = 'Z' };

static void sv_put2(vector<double> *out, int cmd, double x, double y) {
    push_back(out, (double)cmd);
    push_back(out, x);
    push_back(out, y);
}
static void sv_put_c(vector<double> *out, int cmd, double x1, double y1, double x2, double y2, double x, double y) {
    push_back(out, (double)cmd);
    push_back(out, x1);
    push_back(out, y1);
    push_back(out, x2);
    push_back(out, y2);
    push_back(out, x);
    push_back(out, y);
}
static void sv_put_cubic(vector<double> *out, double x1, double y1, double x2, double y2, double x, double y) {
    sv_put_c(out, SV_C, x1, y1, x2, y2, x, y);
}
static void sv_put_quad(vector<double> *out, double x1, double y1, double x, double y) {
    push_back(out, (double)SV_Q);
    push_back(out, x1);
    push_back(out, y1);
    push_back(out, x);
    push_back(out, y);
}
/* the values a command takes after its letter */
static int sv_args(int c) { return c == SV_C || c == SV_K ? 6 : c == SV_Q ? 4 : c == SV_Z ? 0 : 2; }
/* the curve command at p[i], from (x0, y0), as a cubic's four points (a quadratic raised) */
static void sv_curve_at(const vector<double> *p, size_t i, double x0, double y0, double *q) {
    q[0] = x0;
    q[1] = y0;
    if ((int)at(*p, i) == SV_Q) {
        double qx = at(*p, i + 1), qy = at(*p, i + 2), x = at(*p, i + 3), y = at(*p, i + 4);
        q[2] = x0 + 2.0 / 3 * (qx - x0);
        q[3] = y0 + 2.0 / 3 * (qy - y0);
        q[4] = x + 2.0 / 3 * (qx - x);
        q[5] = y + 2.0 / 3 * (qy - y);
        q[6] = x;
        q[7] = y;
        return;
    }
    for (int k = 0; k < 6; k++) q[2 + k] = at(*p, i + 1 + (size_t)k);
}

/* an elliptical arc from (x1, y1) to (x2, y2) as cubics (SVG 2, B.2.4's centre found, then
   a cubic a quarter turn at most) */
static void sv_arc(vector<double> *out, double x1, double y1, double rx, double ry, double rot, int large, int sweep, double x2, double y2) {
    if (x1 == x2 && y1 == y2) return;
    rx = fabs(rx);
    ry = fabs(ry);
    if (rx == 0 || ry == 0) { sv_put2(out, SV_L, x2, y2); return; }
    double phi = rot * M_PI / 180, cp = cos(phi), sp = sin(phi);
    double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    double lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lam > 1) { double s = sqrt(lam); rx *= s; ry *= s; }
    double num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, den = rx * rx * yp * yp + ry * ry * xp * xp;
    double coef = den > 0 && num > 0 ? sqrt(num / den) : 0;
    if (large == sweep) coef = -coef;
    double cxp = coef * rx * yp / ry, cyp = -coef * ry * xp / rx;
    double cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry, vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    double t1 = atan2(uy, ux);
    double dt = atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && dt > 0) dt -= 2 * M_PI;
    else if (sweep && dt < 0) dt += 2 * M_PI;
    double fsegs = ceil(fabs(dt) / (M_PI / 2) - 1e-9);
    int segs = fsegs >= 1 && fsegs <= 4 ? (int)fsegs : 1;
    double d = dt / segs, k = 4.0 / 3 * tan(d / 4);
    double a = t1;
    for (int i = 0; i < segs; i++) {
        double b = a + d;
        double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b);
        double p0x = cx + rx * ca * cp - ry * sa * sp, p0y = cy + rx * ca * sp + ry * sa * cp;
        double d0x = -rx * sa * cp - ry * ca * sp, d0y = -rx * sa * sp + ry * ca * cp;
        double p1x = cx + rx * cb * cp - ry * sb * sp, p1y = cy + rx * cb * sp + ry * sb * cp;
        double d1x = -rx * sb * cp - ry * cb * sp, d1y = -rx * sb * sp + ry * cb * cp;
        if (i == segs - 1) { p1x = x2; p1y = y2; }
        sv_put_c(out, SV_K, p0x + k * d0x, p0y + k * d0y, p1x - k * d1x, p1y - k * d1y, p1x, p1y);
        a = b;
    }
}

size_t amb_svg_path(const char *s, size_t n, vector<double> *out) {
    size_t i = sv_skip_ws(s, n, 0), count = 0;
    double cx = 0, cy = 0, sx = 0, sy = 0, qx = 0, qy = 0, kx = 0, ky = 0;   /* current, subpath start, last Q and C controls */
    char cmd = 0, prev = 0;
    int open = 0, closed = 0;   /* a subpath begun; a close just made, which the next command starts from */
    while (i < n) {
        char c = s[i];
        int letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (letter) {
            if (!strchr("MmLlHhVvCcSsQqTtAaZz", c)) break;
            cmd = c;
            i = sv_skip_ws(s, n, i + 1);
        } else {
            /* a command repeated: its letter left out (a move's further pairs are lines) */
            if (!cmd || cmd == 'Z' || cmd == 'z') break;
            if (cmd == 'M') cmd = 'L';
            else if (cmd == 'm') cmd = 'l';
        }
        if (!open && cmd != 'M' && cmd != 'm') break;   /* path data begins with a move */
        int rel = cmd >= 'a';
        char up = (char)(rel ? cmd - 32 : cmd);
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double a[7];
        int need = up == 'M' || up == 'L' || up == 'T' ? 2 : up == 'H' || up == 'V' ? 1 : up == 'C' ? 6 : up == 'S' || up == 'Q' ? 4 : up == 'A' ? 7 : 0;
        int ok = 1;
        size_t j = i;
        for (int k = 0; k < need && ok; k++) {
            if (k > 0) j = sv_skip_sep(s, n, j);
            if (up == 'A' && (k == 3 || k == 4)) {
                int fl;
                ok = sv_flag(s, n, &j, &fl);
                a[k] = fl;
            } else ok = sv_number(s, n, &j, &a[k]);
        }
        if (!ok) break;
        i = sv_skip_sep(s, n, j);
        if (up != 'M' && up != 'Z' && closed) { sv_put2(out, SV_M, sx, sy); }
        closed = 0;
        if (up == 'M') {
            cx = a[0] + ox; cy = a[1] + oy;
            sx = cx; sy = cy;
            sv_put2(out, SV_M, cx, cy);
            open = 1;
            prev = 'M';
        } else if (up == 'L' || up == 'H' || up == 'V') {
            if (up == 'L') { cx = a[0] + ox; cy = a[1] + oy; }
            else if (up == 'H') cx = a[0] + ox;
            else cy = a[0] + oy;
            sv_put2(out, SV_L, cx, cy);
            prev = 'L';
        } else if (up == 'C' || up == 'S') {
            double x1, y1, x2, y2, x, y;
            if (up == 'C') { x1 = a[0] + ox; y1 = a[1] + oy; x2 = a[2] + ox; y2 = a[3] + oy; x = a[4] + ox; y = a[5] + oy; }
            else {
                x1 = prev == 'C' ? 2 * cx - kx : cx;
                y1 = prev == 'C' ? 2 * cy - ky : cy;
                x2 = a[0] + ox; y2 = a[1] + oy; x = a[2] + ox; y = a[3] + oy;
            }
            sv_put_cubic(out, x1, y1, x2, y2, x, y);
            kx = x2; ky = y2;
            cx = x; cy = y;
            prev = 'C';
        } else if (up == 'Q' || up == 'T') {
            double x1, y1, x, y;
            if (up == 'Q') { x1 = a[0] + ox; y1 = a[1] + oy; x = a[2] + ox; y = a[3] + oy; }
            else {
                x1 = prev == 'Q' ? 2 * cx - qx : cx;
                y1 = prev == 'Q' ? 2 * cy - qy : cy;
                x = a[0] + ox; y = a[1] + oy;
            }
            sv_put_quad(out, x1, y1, x, y);
            qx = x1; qy = y1;
            cx = x; cy = y;
            prev = 'Q';
        } else if (up == 'A') {
            double x = a[5] + ox, y = a[6] + oy;
            sv_arc(out, cx, cy, a[0], a[1], a[2], (int)a[3], (int)a[4], x, y);
            cx = x; cy = y;
            prev = 'A';
        } else {
            push_back(out, (double)SV_Z);
            cx = sx; cy = sy;
            closed = 1;
            prev = 'Z';
        }
        count++;
    }
    return count;
}

/* ---- the scene's text: numbers, words, matrices, paths ---- */

static void sv_w(vector<char> *out, const char *s) { append_range(out, s, strlen(s)); }
static void sv_n(vector<char> *out, double v) {
    char buf[40];
    if (fabs(v) < 1e-12 || v != v) v = 0;
    int k = snprintf(buf, sizeof buf, " %.10g", v);
    if (k > 0) append_range(out, buf, (size_t)k);
}
static void sv_hex(vector<char> *out, uint32_t v) {
    char buf[16];
    int k = snprintf(buf, sizeof buf, " %08x", v);
    if (k > 0) append_range(out, buf, (size_t)k);
}
static void sv_m(vector<char> *out, sv_mat m) {
    sv_n(out, m.a); sv_n(out, m.b); sv_n(out, m.c); sv_n(out, m.d); sv_n(out, m.e); sv_n(out, m.f);
}
static void sv_path_out(vector<char> *out, const vector<double> *p) {
    size_t i = 0, n = len(*p);
    while (i < n) {
        int c = (int)at(*p, i);
        int k = sv_args(c);
        sv_w(out, c == SV_M ? " M" : c == SV_L ? " L" : c == SV_C ? " C" : c == SV_Q ? " Q" : c == SV_K ? " K" : " Z");
        for (int q = 1; q <= k; q++) sv_n(out, at(*p, i + (size_t)q));
        i += (size_t)k + 1;
    }
}

/* ---- the scene made: the subtree walked, each element's values from the cascade ---- */

/* the inherited properties a walk carries: for each, the computed value (st's cval) that
   gives it -- the element's own when a declaration gave it, else the one its parent in the
   walk had (a use's tree inherits from the use) */
enum { SI_FILL, SI_FILL_OPACITY, SI_FILL_RULE, SI_STROKE, SI_STROKE_WIDTH, SI_LINECAP, SI_LINEJOIN, SI_MITER, SI_DASHARRAY, SI_DASHOFFSET,
       SI_STROKE_OPACITY, SI_CLIP_RULE, SI_ANCHOR, SI_COLOR, SI_VISIBILITY, SI_FONT_SIZE, SI_FONT_WEIGHT, SI_FONT_STYLE, SI_FONT_FAMILY,
       SI_LETTER_SPACING, SI_WORD_SPACING, SI_N };
static const int sv_iprops[SI_N] = { CSS_PROP_FILL, CSS_PROP_FILL_OPACITY, CSS_PROP_FILL_RULE, CSS_PROP_STROKE, CSS_PROP_STROKE_WIDTH,
                                     CSS_PROP_STROKE_LINECAP, CSS_PROP_STROKE_LINEJOIN, CSS_PROP_STROKE_MITERLIMIT, CSS_PROP_STROKE_DASHARRAY,
                                     CSS_PROP_STROKE_DASHOFFSET, CSS_PROP_STROKE_OPACITY, CSS_PROP_CLIP_RULE, CSS_PROP_TEXT_ANCHOR,
                                     CSS_PROP_COLOR, CSS_PROP_VISIBILITY, CSS_PROP_FONT_SIZE, CSS_PROP_FONT_WEIGHT, CSS_PROP_FONT_STYLE,
                                     CSS_PROP_FONT_FAMILY, CSS_PROP_LETTER_SPACING, CSS_PROP_WORD_SPACING };

/* where a walk is: the nearest viewport's size (percentages), and the inherited values */
typedef struct sv_walk { double vw, vh; uint32_t src[SI_N]; } sv_walk;

/* the scene's context: the document, its style, each node's first value, the fonts, the
   scene's text, the gradients' count so far, the uses being expanded (no use is expanded
   inside itself) and the elements visited so far */
#define SB const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, vector<char> *out, uint32_t *ids, \
           vector<uint32_t> *uses, uint32_t *work
#define SA d, st, firsts, f, out, ids, uses, work
/* the elements a scene visits at most, uses' copies counted (a use of uses of uses ... stops) */
enum { SV_WORK = 100000 };

static uint32_t sv_first(const vector<uint32_t> *firsts, uint32_t x) { return x < len(*firsts) ? at(*firsts, x) : 0; }

static int sv_word_is(const css_style *st, uint32_t cv, const char *w) {
    uint32_t a = st->cval[cv].word;
    return a && strcmp(names_ptr(st, a), w) == 0;
}

/* the walk below the element x, from its parent's */
static void sv_inherit(const css_style *st, const vector<uint32_t> *firsts, uint32_t x, const sv_walk *pw, sv_walk *w) {
    *w = *pw;
    uint32_t c0 = sv_first(firsts, x);
    if (!c0) return;
    for (int k = 0; k < SI_N; k++) {
        uint32_t cv = c0 + (uint32_t)sv_iprops[k];
        css_how how = st->cval[cv].how;
        if (how == CSS_HOW_DECLARED || how == CSS_HOW_ANIMATED || (how == CSS_HOW_INITIAL && st->cval[cv].winner)) w->src[k] = cv;
    }
}

static int sv_is(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t sv_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t q = d->node[x].first_attr; q; q = d->attr[q].next) if (d->attr[q].name == a) return q;
    return 0;
}
/* an attribute's value copied into v; 0 when the element has none */
static int sv_attr_text(const dom_document *d, uint32_t x, const char *name, vector<char> *v) {
    uint32_t q = sv_attr(d, x, name);
    clear(*v);
    if (!q) return 0;
    append_range(v, text_ptr(d, d->attr[q].value), d->attr[q].value.len);
    return 1;
}
/* the element whose id is id[0, n) (the first in the document's order), or 0 */
static uint32_t sv_by_id(const dom_document *d, const char *id, size_t n) {
    uint32_t ida = names_find(d, "id", 2);
    if (!ida || n == 0) return 0;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT) continue;
        for (uint32_t q = d->node[x].first_attr; q; q = d->attr[q].next)
            if (d->attr[q].name == ida && d->attr[q].value.len == n && memcmp(text_ptr(d, d->attr[q].value), id, n) == 0) return x;
    }
    return 0;
}
/* the element an IRI names (#id, white space about it): 0 for none */
static uint32_t sv_iri(const dom_document *d, const char *s, size_t n) {
    size_t a = sv_skip_ws(s, n, 0), b = n;
    while (b > a && sv_ws(s[b - 1])) b--;
    if (b > a + 1 && (s[a] == '"' || s[a] == '\'') && s[b - 1] == s[a]) { a++; b--; }
    if (b <= a + 1 || s[a] != '#') return 0;
    return sv_by_id(d, s + a + 1, b - a - 1);
}
/* the element a use's or a gradient's href names (SVG 2's href over xlink:href) */
static uint32_t sv_href(const dom_document *d, uint32_t x) {
    vector<char> v;
    if (!sv_attr_text(d, x, "href", &v) && !sv_attr_text(d, x, "xlink:href", &v)) return 0;
    return sv_iri(d, data(v), len(v));
}
/* the element a url() at s names, and where the url() ends */
static uint32_t sv_url(const dom_document *d, const char *s, size_t n, size_t *end) {
    size_t i = sv_skip_ws(s, n, 0);
    *end = i;
    if (i + 4 > n || memcmp(s + i, "url(", 4) != 0) return 0;
    size_t j = i + 4;
    while (j < n && s[j] != ')') j++;
    if (j >= n) return 0;
    *end = j + 1;
    return sv_iri(d, s + i + 4, j - i - 4);
}

/* a length an attribute writes, in user units: a number, px or another absolute unit, em
   and ex of the font size, a percentage of basis; *ok 0 when it is none */
static double sv_length(const char *s, size_t n, double basis, double fs, int *ok) {
    size_t i = sv_skip_ws(s, n, 0);
    double v;
    *ok = 0;
    if (!sv_number(s, n, &i, &v)) return 0;
    size_t u = i;
    while (u < n && !sv_ws(s[u])) u++;
    if (sv_skip_ws(s, n, u) != n) return 0;
    size_t un = u - i;
    const char *q = s + i;
    double k = 0;
    if (un == 0) k = 1;
    else if (un == 1 && q[0] == '%') k = basis / 100;
    else if (un == 2) {
        if (memcmp(q, "px", 2) == 0) k = 1;
        else if (memcmp(q, "in", 2) == 0) k = 96;
        else if (memcmp(q, "cm", 2) == 0) k = 96 / 2.54;
        else if (memcmp(q, "mm", 2) == 0) k = 96 / 25.4;
        else if (memcmp(q, "pt", 2) == 0) k = 4.0 / 3;
        else if (memcmp(q, "pc", 2) == 0) k = 16;
        else if (memcmp(q, "em", 2) == 0) k = fs;
        else if (memcmp(q, "ex", 2) == 0) k = fs / 2;
    }
    if (k == 0) return 0;
    *ok = 1;
    return v * k;
}
static double sv_len_attr(const dom_document *d, uint32_t x, const char *name, double basis, double fs, double dflt, int *given) {
    vector<char> v;
    *given = 0;
    if (!sv_attr_text(d, x, name, &v)) return dflt;
    int ok;
    double r = sv_length(data(v), len(v), basis, fs, &ok);
    if (!ok) return dflt;
    *given = 1;
    return r;
}
/* a number an attribute writes, or dflt */
static double sv_num_attr(const dom_document *d, uint32_t x, const char *name, double dflt) {
    vector<char> v;
    if (!sv_attr_text(d, x, name, &v)) return dflt;
    size_t i = sv_skip_ws(data(v), len(v), 0);
    double r;
    if (!sv_number(data(v), len(v), &i, &r)) return dflt;
    return r;
}

/* the font size a walk is at, px */
static double sv_fs(const css_style *st, const sv_walk *w) {
    double fs = st->cval[w->src[SI_FONT_SIZE]].num;
    return fs > 0 ? fs : 16;
}
static double sv_diag(const sv_walk *w) { return sqrt((w->vw * w->vw + w->vh * w->vh) / 2); }

/* a computed length (stroke-width's, stroke-dashoffset's): px, a number of user units, a
   percentage of basis */
static double sv_css_len(const css_style *st, uint32_t cv, double basis) {
    css_unit u = st->cval[cv].unit;
    double v = st->cval[cv].num;
    if (u == CSS_UNIT_PERCENT) return v * basis / 100;
    if (u == CSS_UNIT_CALC) return v + st->cval[cv].pct * basis / 100;
    return v;
}

/* ---- transforms ---- */

/* an angle CSS writes, in degrees (deg, rad, grad, turn; a bare number degrees) */
static int sv_angle(const char *s, size_t n, size_t *i, double *deg) {
    if (!sv_number(s, n, i, deg)) return 0;
    if (*i + 3 <= n && memcmp(s + *i, "deg", 3) == 0) *i += 3;
    else if (*i + 4 <= n && memcmp(s + *i, "grad", 4) == 0) { *deg *= 0.9; *i += 4; }
    else if (*i + 3 <= n && memcmp(s + *i, "rad", 3) == 0) { *deg *= 180 / M_PI; *i += 3; }
    else if (*i + 4 <= n && memcmp(s + *i, "turn", 4) == 0) { *deg *= 360; *i += 4; }
    return 1;
}
/* a transform list: SVG's attribute (css 0: numbers, rotate's centre) or CSS's (css 1: px
   lengths and angles); 1 and its matrix, or 0 when it does not parse (then none) */
static int sv_transform_list(const char *s, size_t n, int css, sv_mat *out) {
    sv_mat m = sv_id();
    size_t i = sv_skip_ws(s, n, 0);
    int any = 0;
    while (i < n) {
        size_t b = i;
        while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (css && s[i] == '3'))) i++;
        size_t nl = i - b;
        const char *nm = s + b;
        i = sv_skip_ws(s, n, i);
        if (nl == 0 || i >= n || s[i] != '(') return 0;
        i = sv_skip_ws(s, n, i + 1);
        double a[6];
        int k = 0;
        int angle = css && ((nl == 6 && memcmp(nm, "rotate", 6) == 0) || (nl >= 4 && memcmp(nm, "skew", 4) == 0));
        while (i < n && s[i] != ')' && k < 6) {
            int ok = angle ? sv_angle(s, n, &i, &a[k]) : sv_number(s, n, &i, &a[k]);
            if (!ok) return 0;
            if (css && i + 2 <= n && memcmp(s + i, "px", 2) == 0) i += 2;
            k++;
            i = sv_skip_sep(s, n, i);
        }
        if (i >= n || s[i] != ')') return 0;
        i = sv_skip_sep(s, n, i + 1);
        sv_mat t;
        if (nl == 6 && memcmp(nm, "matrix", 6) == 0 && k == 6) t = sv_make(a[0], a[1], a[2], a[3], a[4], a[5]);
        else if (nl == 9 && memcmp(nm, "translate", 9) == 0 && (k == 1 || k == 2)) t = sv_make(1, 0, 0, 1, a[0], k == 2 ? a[1] : 0);
        else if (css && nl == 10 && memcmp(nm, "translateX", 10) == 0 && k == 1) t = sv_make(1, 0, 0, 1, a[0], 0);
        else if (css && nl == 10 && memcmp(nm, "translateY", 10) == 0 && k == 1) t = sv_make(1, 0, 0, 1, 0, a[0]);
        else if (nl == 5 && memcmp(nm, "scale", 5) == 0 && (k == 1 || k == 2)) t = sv_make(a[0], 0, 0, k == 2 ? a[1] : a[0], 0, 0);
        else if (css && nl == 6 && memcmp(nm, "scaleX", 6) == 0 && k == 1) t = sv_make(a[0], 0, 0, 1, 0, 0);
        else if (css && nl == 6 && memcmp(nm, "scaleY", 6) == 0 && k == 1) t = sv_make(1, 0, 0, a[0], 0, 0);
        else if (nl == 6 && memcmp(nm, "rotate", 6) == 0 && (k == 1 || (!css && k == 3))) {
            double r = a[0] * M_PI / 180, cr = cos(r), sr = sin(r);
            t = sv_make(cr, sr, -sr, cr, 0, 0);
            if (k == 3) t = sv_mul(sv_make(1, 0, 0, 1, a[1], a[2]), sv_mul(t, sv_make(1, 0, 0, 1, -a[1], -a[2])));
        } else if (nl == 5 && memcmp(nm, "skewX", 5) == 0 && k == 1) t = sv_make(1, 0, tan(a[0] * M_PI / 180), 1, 0, 0);
        else if (nl == 5 && memcmp(nm, "skewY", 5) == 0 && k == 1) t = sv_make(1, tan(a[0] * M_PI / 180), 0, 1, 0, 0);
        else if (css && nl == 4 && memcmp(nm, "skew", 4) == 0 && (k == 1 || k == 2))
            t = sv_make(1, k == 2 ? tan(a[1] * M_PI / 180) : 0, tan(a[0] * M_PI / 180), 1, 0, 0);
        else return 0;
        m = sv_mul(m, t);
        any = 1;
    }
    *out = m;
    return any || n == 0 || sv_skip_ws(s, n, 0) == n;
}
/* an element's transform: a declared CSS transform (about its transform-origin, 0 0 unless
   declared, as Chromium's sheet has it for SVG elements), else its transform attribute */
static sv_mat sv_transform_of(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, uint32_t x, const sv_walk *w) {
    sv_mat m = sv_id();
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && st->cval[c0 + CSS_PROP_TRANSFORM].how == CSS_HOW_DECLARED && st->cval[c0 + CSS_PROP_TRANSFORM].winner) {
        if (st->cval[c0 + CSS_PROP_TRANSFORM].unit != CSS_UNIT_TEXT) return m;
        amb_span sp = st->cval[c0 + CSS_PROP_TRANSFORM].text;
        vector<char> t;
        append_range(t, text_ptr(st, sp), sp.len);
        if (!sv_transform_list(data(t), len(t), 1, &m)) return sv_id();
        uint32_t oc = c0 + CSS_PROP_TRANSFORM_ORIGIN;
        if (st->cval[oc].how == CSS_HOW_DECLARED && st->cval[oc].unit == CSS_UNIT_TEXT) {
            amb_span op = st->cval[oc].text;
            vector<char> o;
            append_range(o, text_ptr(st, op), op.len);
            double ox = 0, oy = 0;
            size_t i = sv_skip_ws(data(o), len(o), 0);
            int ok;
            size_t e = i;
            while (e < len(o) && !sv_ws(at(o, e))) e++;
            ox = sv_length(data(o) + i, e - i, w->vw, 16, &ok);
            i = sv_skip_ws(data(o), len(o), e);
            e = i;
            while (e < len(o) && !sv_ws(at(o, e))) e++;
            if (e > i) oy = sv_length(data(o) + i, e - i, w->vh, 16, &ok);
            m = sv_mul(sv_make(1, 0, 0, 1, ox, oy), sv_mul(m, sv_make(1, 0, 0, 1, -ox, -oy)));
        }
        return m;
    }
    vector<char> v;
    if (sv_attr_text(d, x, "transform", &v) && !sv_transform_list(data(v), len(v), 0, &m)) return sv_id();
    return m;
}

/* preserveAspectRatio (SVG 2, 8.8) mapping the box vb (x, y, w, h) into the viewport vp */
static sv_mat sv_viewbox(const dom_document *d, uint32_t x, const double *vb, const double *vp) {
    vector<char> v;
    int ax = 1, ay = 1, slice = 0, none = 0;   /* 0 min, 1 mid, 2 max */
    if (sv_attr_text(d, x, "preserveAspectRatio", &v)) {
        size_t i = sv_skip_ws(data(v), len(v), 0), n = len(v);
        if (i + 5 <= n && memcmp(data(v) + i, "defer", 5) == 0) i = sv_skip_ws(data(v), n, i + 5);
        if (i + 4 <= n && memcmp(data(v) + i, "none", 4) == 0) { none = 1; i += 4; }
        else if (i + 8 <= n && data(v)[i] == 'x' && data(v)[i + 4] == 'Y') {
            const char *q = data(v) + i;
            ax = memcmp(q + 1, "Min", 3) == 0 ? 0 : memcmp(q + 1, "Mid", 3) == 0 ? 1 : 2;
            ay = memcmp(q + 5, "Min", 3) == 0 ? 0 : memcmp(q + 5, "Mid", 3) == 0 ? 1 : 2;
            i += 8;
        }
        i = sv_skip_ws(data(v), n, i);
        if (i + 5 <= n && memcmp(data(v) + i, "slice", 5) == 0) slice = 1;
    }
    double sx = vp[2] / vb[2], sy = vp[3] / vb[3];
    if (!none) {
        double s = slice ? fmax(sx, sy) : fmin(sx, sy);
        sx = s;
        sy = s;
    }
    double tx = vp[0] - vb[0] * sx, ty = vp[1] - vb[1] * sy;
    if (!none) {
        double rw = vp[2] - vb[2] * sx, rh = vp[3] - vb[3] * sy;
        tx += ax == 1 ? rw / 2 : ax == 2 ? rw : 0;
        ty += ay == 1 ? rh / 2 : ay == 2 ? rh : 0;
    }
    return sv_make(sx, 0, 0, sy, tx, ty);
}
/* a viewBox: 1 and its four numbers (a width and height above 0) */
static int sv_viewbox_of(const dom_document *d, uint32_t x, double *vb) {
    vector<char> v;
    if (!sv_attr_text(d, x, "viewBox", &v)) return 0;
    size_t i = sv_skip_ws(data(v), len(v), 0);
    for (int k = 0; k < 4; k++) {
        if (k) i = sv_skip_sep(data(v), len(v), i);
        if (!sv_number(data(v), len(v), &i, &vb[k])) return 0;
    }
    return vb[2] > 0 && vb[3] > 0;
}

/* ---- shapes ---- */

/* a cubic's coordinate's extremes in (0, 1), its bounds widened to them */
static void sv_cubic_bounds(double p0, double p1, double p2, double p3, double *lo, double *hi) {
    double a = -p0 + 3 * p1 - 3 * p2 + p3, b = 2 * (p0 - 2 * p1 + p2), c = p1 - p0;
    double ts[2];
    int nt = 0;
    if (fabs(a) < 1e-12) { if (fabs(b) > 1e-12) ts[nt++] = -c / b; }
    else {
        double disc = b * b - 4 * a * c;
        if (disc >= 0) { double q = sqrt(disc); ts[nt++] = (-b + q) / (2 * a); ts[nt++] = (-b - q) / (2 * a); }
    }
    for (int k = 0; k < nt; k++) {
        double t = ts[k];
        if (t <= 0 || t >= 1) continue;
        double u = 1 - t, v = u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
        if (v < *lo) *lo = v;
        if (v > *hi) *hi = v;
    }
}
/* a path's tight bounds (x0, y0, x1, y1) under m: 0 when it has no point */
static int sv_path_bounds(const vector<double> *p, sv_mat m, double *bb) {
    size_t i = 0, n = len(*p);
    double cx = 0, cy = 0, sx = 0, sy = 0;
    int any = 0;
    bb[0] = bb[1] = 1e300;
    bb[2] = bb[3] = -1e300;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_Z) { cx = sx; cy = sy; i++; continue; }
        double pts[8];
        int np = 0;
        if (c == SV_M || c == SV_L) {
            double x = at(*p, i + 1), y = at(*p, i + 2);
            pts[0] = sv_ax(m, x, y);
            pts[1] = sv_ay(m, x, y);
            np = 1;
            if (c == SV_M) { sx = x; sy = y; }
            cx = x; cy = y;
            i += 3;
        } else {
            double q[8];
            sv_curve_at(p, i, cx, cy, q);
            for (int k = 0; k < 4; k++) { pts[2 * k] = sv_ax(m, q[2 * k], q[2 * k + 1]); pts[2 * k + 1] = sv_ay(m, q[2 * k], q[2 * k + 1]); }
            double lox = fmin(pts[0], pts[6]), hix = fmax(pts[0], pts[6]), loy = fmin(pts[1], pts[7]), hiy = fmax(pts[1], pts[7]);
            sv_cubic_bounds(pts[0], pts[2], pts[4], pts[6], &lox, &hix);
            sv_cubic_bounds(pts[1], pts[3], pts[5], pts[7], &loy, &hiy);
            if (lox < bb[0]) bb[0] = lox;
            if (hix > bb[2]) bb[2] = hix;
            if (loy < bb[1]) bb[1] = loy;
            if (hiy > bb[3]) bb[3] = hiy;
            any = 1;
            cx = q[6]; cy = q[7];
            i += (size_t)sv_args(c) + 1;
            continue;
        }
        for (int k = 0; k < np; k++) {
            if (pts[2 * k] < bb[0]) bb[0] = pts[2 * k];
            if (pts[2 * k] > bb[2]) bb[2] = pts[2 * k];
            if (pts[2 * k + 1] < bb[1]) bb[1] = pts[2 * k + 1];
            if (pts[2 * k + 1] > bb[3]) bb[3] = pts[2 * k + 1];
            any = 1;
        }
    }
    return any;
}

/* a quarter ellipse's cubic from (x0, y0) to (x1, y1), its corner at (kx, ky) */
static void sv_corner(vector<double> *p, double x0, double y0, double kx, double ky, double x1, double y1) {
    const double k = 0.55228474983079;
    sv_put_c(p, SV_K, x0 + (kx - x0) * k, y0 + (ky - y0) * k, x1 + (kx - x1) * k, y1 + (ky - y1) * k, x1, y1);
}
/* an ellipse as Skia adds an oval: from its right, clockwise */
static void sv_ellipse_path(vector<double> *p, double cx, double cy, double rx, double ry) {
    sv_put2(p, SV_M, cx + rx, cy);
    sv_corner(p, cx + rx, cy, cx + rx, cy + ry, cx, cy + ry);
    sv_corner(p, cx, cy + ry, cx - rx, cy + ry, cx - rx, cy);
    sv_corner(p, cx - rx, cy, cx - rx, cy - ry, cx, cy - ry);
    sv_corner(p, cx, cy - ry, cx + rx, cy - ry, cx + rx, cy);
    push_back(p, (double)SV_Z);
}
/* a list of points (polyline, polygon): pairs of numbers, read up to an error; an odd
   number's last dropped */
static void sv_points(const char *s, size_t n, int close, vector<double> *p) {
    size_t i = sv_skip_ws(s, n, 0);
    int k = 0;
    while (i < n) {
        double x, y;
        if (!sv_number(s, n, &i, &x)) break;
        i = sv_skip_sep(s, n, i);
        if (!sv_number(s, n, &i, &y)) break;
        i = sv_skip_sep(s, n, i);
        sv_put2(p, k ? SV_L : SV_M, x, y);
        k++;
    }
    if (close && k) push_back(p, (double)SV_Z);
}

/* a shape's geometry as a path (SVG 2, 10's equivalent paths, as Blink makes them): 1, or 0
   when it draws nothing (a width, height or radius not above 0) */
static int sv_geometry(const dom_document *d, const css_style *st, uint32_t x, const sv_walk *w, vector<double> *p) {
    double fs = sv_fs(st, w), dg = sv_diag(w);
    int g1, g2;
    vector<char> v;
    if (sv_is(d, x, "rect")) {
        double rx0 = sv_len_attr(d, x, "x", w->vw, fs, 0, &g1), ry0 = sv_len_attr(d, x, "y", w->vh, fs, 0, &g1);
        double rw = sv_len_attr(d, x, "width", w->vw, fs, 0, &g1), rh = sv_len_attr(d, x, "height", w->vh, fs, 0, &g2);
        if (rw <= 0 || rh <= 0) return 0;
        double rx = sv_len_attr(d, x, "rx", w->vw, fs, 0, &g1), ry = sv_len_attr(d, x, "ry", w->vh, fs, 0, &g2);
        if (g1 && rx < 0) g1 = 0;
        if (g2 && ry < 0) g2 = 0;
        if (g1 && !g2) ry = rx;
        else if (g2 && !g1) rx = ry;
        else if (!g1 && !g2) rx = ry = 0;
        if (rx > rw / 2) rx = rw / 2;
        if (ry > rh / 2) ry = rh / 2;
        if (rx <= 0 || ry <= 0) {
            sv_put2(p, SV_M, rx0, ry0);
            sv_put2(p, SV_L, rx0 + rw, ry0);
            sv_put2(p, SV_L, rx0 + rw, ry0 + rh);
            sv_put2(p, SV_L, rx0, ry0 + rh);
            push_back(p, (double)SV_Z);
            return 1;
        }
        /* as Skia adds a round rect: from its left side's lower end, clockwise */
        double x1 = rx0 + rw, y1 = ry0 + rh;
        sv_put2(p, SV_M, rx0, y1 - ry);
        sv_put2(p, SV_L, rx0, ry0 + ry);
        sv_corner(p, rx0, ry0 + ry, rx0, ry0, rx0 + rx, ry0);
        sv_put2(p, SV_L, x1 - rx, ry0);
        sv_corner(p, x1 - rx, ry0, x1, ry0, x1, ry0 + ry);
        sv_put2(p, SV_L, x1, y1 - ry);
        sv_corner(p, x1, y1 - ry, x1, y1, x1 - rx, y1);
        sv_put2(p, SV_L, rx0 + rx, y1);
        sv_corner(p, rx0 + rx, y1, rx0, y1, rx0, y1 - ry);
        push_back(p, (double)SV_Z);
        return 1;
    }
    if (sv_is(d, x, "circle")) {
        double cx = sv_len_attr(d, x, "cx", w->vw, fs, 0, &g1), cy = sv_len_attr(d, x, "cy", w->vh, fs, 0, &g1);
        double r = sv_len_attr(d, x, "r", dg, fs, 0, &g1);
        if (r <= 0) return 0;
        sv_ellipse_path(p, cx, cy, r, r);
        return 1;
    }
    if (sv_is(d, x, "ellipse")) {
        double cx = sv_len_attr(d, x, "cx", w->vw, fs, 0, &g1), cy = sv_len_attr(d, x, "cy", w->vh, fs, 0, &g1);
        double rx = sv_len_attr(d, x, "rx", w->vw, fs, 0, &g1), ry = sv_len_attr(d, x, "ry", w->vh, fs, 0, &g2);
        if (g1 && !g2) ry = rx;
        else if (g2 && !g1) rx = ry;
        if (rx <= 0 || ry <= 0) return 0;
        sv_ellipse_path(p, cx, cy, rx, ry);
        return 1;
    }
    if (sv_is(d, x, "line")) {
        double x1 = sv_len_attr(d, x, "x1", w->vw, fs, 0, &g1), y1 = sv_len_attr(d, x, "y1", w->vh, fs, 0, &g1);
        double x2 = sv_len_attr(d, x, "x2", w->vw, fs, 0, &g1), y2 = sv_len_attr(d, x, "y2", w->vh, fs, 0, &g1);
        sv_put2(p, SV_M, x1, y1);
        sv_put2(p, SV_L, x2, y2);
        return 1;
    }
    if (sv_is(d, x, "polyline") || sv_is(d, x, "polygon")) {
        if (!sv_attr_text(d, x, "points", &v)) return 0;
        sv_points(data(v), len(v), sv_is(d, x, "polygon"), p);
        return len(*p) > 0;
    }
    if (sv_is(d, x, "path")) {
        if (!sv_attr_text(d, x, "d", &v)) return 0;
        amb_svg_path(data(v), len(v), p);
        return len(*p) > 0;
    }
    return 0;
}

/* ---- paints ---- */

/* a paint (fill's or stroke's): none, a colour, or a gradient (its element) with a
   fallback (none or a colour, fb_kind 0 none) */
enum { SV_PAINT_NONE, SV_PAINT_COLOR, SV_PAINT_URL };
typedef struct sv_paint { int kind; uint32_t rgba, ref; int fb_kind; uint32_t fb; } sv_paint;

/* s[0, n) is the word w, its case aside */
static int sv_eq_ci(const char *s, size_t n, const char *w) {
    if (strlen(w) != n) return 0;
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = w[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return 0;
    }
    return 1;
}
/* a colour CSS writes, or currentcolor's (cur) */
static int sv_colour(const char *s, size_t n, uint32_t cur, uint32_t *rgba) {
    size_t a = sv_skip_ws(s, n, 0), b = n;
    while (b > a && sv_ws(s[b - 1])) b--;
    if (sv_eq_ci(s + a, b - a, "currentcolor")) { *rgba = cur; return 1; }
    return css_color_parse(s + a, b - a, rgba);
}
static void sv_paint_of(const dom_document *d, const css_style *st, uint32_t cv, uint32_t cur, sv_paint *p) {
    memset(p, 0, sizeof *p);
    css_unit u = st->cval[cv].unit;
    if (u == CSS_UNIT_KEYWORD) {
        if (sv_word_is(st, cv, "currentcolor")) { p->kind = SV_PAINT_COLOR; p->rgba = cur; }
        return;
    }
    if (u == CSS_UNIT_COLOR) { p->kind = SV_PAINT_COLOR; p->rgba = st->cval[cv].color; return; }
    if (u != CSS_UNIT_TEXT) return;
    amb_span sp = st->cval[cv].text;
    vector<char> t;
    append_range(t, text_ptr(st, sp), sp.len);
    size_t end = 0;
    uint32_t ref = sv_url(d, data(t), len(t), &end);
    if (end > 0 && len(t) >= 4 && memcmp(data(t) + sv_skip_ws(data(t), len(t), 0), "url(", 4) == 0) {
        p->kind = SV_PAINT_URL;
        p->ref = ref;
        size_t i = sv_skip_ws(data(t), len(t), end);
        if (i < len(t)) {
            if (len(t) - i >= 4 && memcmp(data(t) + i, "none", 4) == 0) p->fb_kind = SV_PAINT_NONE;
            else if (sv_colour(data(t) + i, len(t) - i, cur, &p->fb)) p->fb_kind = SV_PAINT_COLOR;
        }
        return;
    }
    if (sv_colour(data(t), len(t), cur, &p->rgba)) p->kind = SV_PAINT_COLOR;
}

/* a gradient element and those its href names, at most 16 deep without a cycle: chain[0..n) */
static int sv_chain(const dom_document *d, uint32_t g, uint32_t *chain) {
    int n = 0;
    while (g && n < 16) {
        if (!sv_is(d, g, "linearGradient") && !sv_is(d, g, "radialGradient")) break;
        for (int k = 0; k < n; k++) if (chain[k] == g) return n;
        chain[n++] = g;
        g = sv_href(d, g);
    }
    return n;
}
static uint32_t sv_chain_attr(const dom_document *d, const uint32_t *chain, int n, const char *name) {
    for (int k = 0; k < n; k++) if (sv_attr(d, chain[k], name)) return chain[k];
    return 0;
}
/* a gradient's coordinate: in its bounding box a number (a percentage a fraction), else a
   length of the viewport */
static double sv_gcoord(const dom_document *d, const uint32_t *chain, int n, const char *name, int bbox, double basis, double fs,
                        double dflt_frac) {
    uint32_t g = sv_chain_attr(d, chain, n, name);
    int given;
    if (!g) return bbox ? dflt_frac : dflt_frac * basis;
    double v = sv_len_attr(d, g, name, bbox ? 1 : basis, fs, bbox ? dflt_frac : dflt_frac * basis, &given);
    return v;
}

/* the paint p resolved for a shape whose bounds (in its user space) are bb: a gradient's
   line written into the scene (its number in *gid), a colour (*rgba), or none: 2, 1, 0 */
static int sv_resolve(SB, const sv_paint *p, const double *bb, const sv_walk *w, uint32_t *gid, uint32_t *rgba) {
    if (p->kind == SV_PAINT_NONE) return 0;
    if (p->kind == SV_PAINT_COLOR) { *rgba = p->rgba; return 1; }
    uint32_t chain[16];
    int n = sv_chain(d, p->ref, chain);
    if (n == 0) {
        if (p->fb_kind == SV_PAINT_COLOR) { *rgba = p->fb; return 1; }
        return 0;
    }
    int radial = sv_is(d, chain[0], "radialGradient");
    double fs = sv_fs(st, w), dg = sv_diag(w);
    vector<char> v;
    uint32_t ug = sv_chain_attr(d, chain, n, "gradientUnits");
    int bbox = 1;
    if (ug && sv_attr_text(d, ug, "gradientUnits", &v) && len(v) == 14 && memcmp(data(v), "userSpaceOnUse", 14) == 0) bbox = 0;
    sv_mat gt = sv_id();
    uint32_t tg = sv_chain_attr(d, chain, n, "gradientTransform");
    if (tg && sv_attr_text(d, tg, "gradientTransform", &v) && !sv_transform_list(data(v), len(v), 0, &gt)) gt = sv_id();
    int spread = 0;
    uint32_t sg = sv_chain_attr(d, chain, n, "spreadMethod");
    if (sg && sv_attr_text(d, sg, "spreadMethod", &v)) {
        if (len(v) == 7 && memcmp(data(v), "reflect", 7) == 0) spread = 1;
        else if (len(v) == 6 && memcmp(data(v), "repeat", 6) == 0) spread = 2;
    }
    /* the stops: those of the first in the chain that has any */
    uint32_t holder = 0;
    for (int k = 0; k < n && !holder; k++)
        for (uint32_t c = d->node[chain[k]].first_child; c && !holder; c = d->node[c].next_sibling) if (sv_is(d, c, "stop")) holder = chain[k];
    if (!holder) return 0;
    vector<double> stops;
    double last = 0;
    for (uint32_t c = d->node[holder].first_child; c; c = d->node[c].next_sibling) {
        if (!sv_is(d, c, "stop")) continue;
        double off = 0;
        if (sv_attr_text(d, c, "offset", &v)) {
            size_t i = sv_skip_ws(data(v), len(v), 0);
            if (sv_number(data(v), len(v), &i, &off) && i < len(v) && at(v, i) == '%') off /= 100;
        }
        off = off < 0 ? 0 : off > 1 ? 1 : off;
        if (off < last) off = last;
        last = off;
        uint32_t c0 = sv_first(firsts, c);
        uint32_t col = c0 ? st->cval[c0 + CSS_PROP_STOP_COLOR].color : 0x000000ff;
        double op = c0 ? st->cval[c0 + CSS_PROP_STOP_OPACITY].num : 1;
        uint32_t a = (uint32_t)floor((col & 0xff) * (op < 0 ? 0 : op > 1 ? 1 : op) + 0.5);
        push_back(stops, off);
        push_back(stops, (double)((col & 0xffffff00u) | a));
    }
    size_t ns = len(stops) / 2;
    if (ns == 1) { *rgba = (uint32_t)at(stops, 1); return 1; }
    sv_mat gm = gt;
    if (bbox) {
        double bw = bb[2] - bb[0], bh = bb[3] - bb[1];
        if (bw <= 0 || bh <= 0) return 0;
        gm = sv_mul(sv_make(bw, 0, 0, bh, bb[0], bb[1]), gt);
    }
    uint32_t id = ++*ids;
    sv_w(out, radial ? "r" : "l");
    sv_n(out, id);
    sv_m(out, gm);
    if (radial) {
        double cx = sv_gcoord(d, chain, n, "cx", bbox, w->vw, fs, 0.5), cy = sv_gcoord(d, chain, n, "cy", bbox, w->vh, fs, 0.5);
        double r = sv_gcoord(d, chain, n, "r", bbox, dg, fs, 0.5);
        double fx = sv_chain_attr(d, chain, n, "fx") ? sv_gcoord(d, chain, n, "fx", bbox, w->vw, fs, 0.5) : cx;
        double fy = sv_chain_attr(d, chain, n, "fy") ? sv_gcoord(d, chain, n, "fy", bbox, w->vh, fs, 0.5) : cy;
        double fr = sv_gcoord(d, chain, n, "fr", bbox, dg, fs, 0);
        if (r < 0 || fr < 0) return 0;
        sv_n(out, cx); sv_n(out, cy); sv_n(out, r); sv_n(out, fx); sv_n(out, fy); sv_n(out, fr);
    } else {
        sv_n(out, sv_gcoord(d, chain, n, "x1", bbox, w->vw, fs, 0));
        sv_n(out, sv_gcoord(d, chain, n, "y1", bbox, w->vh, fs, 0));
        sv_n(out, sv_gcoord(d, chain, n, "x2", bbox, w->vw, fs, 1));
        sv_n(out, sv_gcoord(d, chain, n, "y2", bbox, w->vh, fs, 0));
    }
    sv_n(out, spread);
    sv_n(out, (double)ns);
    for (size_t k = 0; k < ns; k++) { sv_n(out, at(stops, 2 * k)); sv_hex(out, (uint32_t)at(stops, 2 * k + 1)); }
    sv_w(out, "\n");
    *gid = id;
    return 2;
}
/* a resolved paint's words in a path's line */
static void sv_paint_words(vector<char> *line, int kind, uint32_t gid, uint32_t rgba) {
    if (kind == 2) { sv_w(line, " u"); sv_n(line, gid); }
    else if (kind == 1) { sv_w(line, " c"); sv_hex(line, rgba); }
    else sv_w(line, " n");
}

/* ---- elements ---- */

static void sv_node(SB, uint32_t x, const sv_walk *pw, sv_mat m, int depth);

/* the bounds an element's content covers under m (with its own transform when withself is 1, as
   a child's are; without, its user space's: an objectBoundingBox's): a shape's geometry's,
   a group's or a use's children's together; 0 when none */
static int sv_bounds(SB, uint32_t x, const sv_walk *pw, sv_mat m, double *bb, int depth, int withself) {
    if (depth > 32 || ++*work > SV_WORK || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_SVG) return 0;
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none")) return 0;
    sv_walk w;
    sv_inherit(st, firsts, x, pw, &w);
    sv_mat mm = withself ? sv_mul(m, sv_transform_of(d, st, firsts, x, &w)) : m;
    vector<double> p;
    if (sv_geometry(d, st, x, &w, &p)) return sv_path_bounds(&p, mm, bb);
    int any = 0;
    if (sv_is(d, x, "g") || sv_is(d, x, "a") || sv_is(d, x, "switch") || sv_is(d, x, "use")) {
        bb[0] = bb[1] = 1e300;
        bb[2] = bb[3] = -1e300;
        uint32_t ref = sv_is(d, x, "use") ? sv_href(d, x) : 0;
        if (sv_is(d, x, "use")) {
            int g;
            double ux = sv_len_attr(d, x, "x", w.vw, sv_fs(st, &w), 0, &g), uy = sv_len_attr(d, x, "y", w.vh, sv_fs(st, &w), 0, &g);
            mm = sv_mul(mm, sv_make(1, 0, 0, 1, ux, uy));
        }
        for (uint32_t c = ref ? ref : d->node[x].first_child; c; c = ref ? 0 : d->node[c].next_sibling) {
            double cb[4];
            if (!sv_bounds(SA, c, &w, mm, cb, depth + 1, 1)) continue;
            bb[0] = fmin(bb[0], cb[0]);
            bb[1] = fmin(bb[1], cb[1]);
            bb[2] = fmax(bb[2], cb[2]);
            bb[3] = fmax(bb[3], cb[3]);
            any = 1;
        }
    }
    return any;
}

/* a clipPath's shapes, each a line of its own into lines under m (the referencing element's
   user space, in a viewport vw x vh; bb its bounds there, for objectBoundingBox): their
   count */
static int sv_clip_shapes(SB, uint32_t cp, const double *bb, int hasbb, sv_mat m, double vw, double vh, vector<char> *lines) {
    sv_walk root;
    memset(&root, 0, sizeof root);
    uint32_t c0 = sv_first(firsts, cp);
    if (!c0) return 0;
    for (int k = 0; k < SI_N; k++) root.src[k] = c0 + (uint32_t)sv_iprops[k];
    root.vw = vw;
    root.vh = vh;
    sv_mat cm = sv_mul(m, sv_transform_of(d, st, firsts, cp, &root));
    vector<char> v;
    if (sv_attr_text(d, cp, "clipPathUnits", &v) && len(v) == 17 && memcmp(data(v), "objectBoundingBox", 17) == 0) {
        if (!hasbb || bb[2] - bb[0] <= 0 || bb[3] - bb[1] <= 0) return 0;
        cm = sv_mul(cm, sv_make(bb[2] - bb[0], 0, 0, bb[3] - bb[1], bb[0], bb[1]));
    }
    int count = 0;
    for (uint32_t c = d->node[cp].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || d->node[c].ns != DOM_NAMESPACE_SVG) continue;
        uint32_t s0 = sv_first(firsts, c);
        if (s0 && sv_word_is(st, s0 + CSS_PROP_DISPLAY, "none")) continue;
        sv_walk w;
        sv_inherit(st, firsts, c, &root, &w);
        sv_mat sm = sv_mul(cm, sv_transform_of(d, st, firsts, c, &w));
        uint32_t shape = c;
        if (sv_is(d, c, "use")) {
            shape = sv_href(d, c);
            if (!shape) continue;
            int g;
            double ux = sv_len_attr(d, c, "x", w.vw, sv_fs(st, &w), 0, &g), uy = sv_len_attr(d, c, "y", w.vh, sv_fs(st, &w), 0, &g);
            sm = sv_mul(sm, sv_make(1, 0, 0, 1, ux, uy));
            sv_walk uw = w;
            sv_inherit(st, firsts, shape, &uw, &w);
            sm = sv_mul(sm, sv_transform_of(d, st, firsts, shape, &w));
        }
        if (sv_word_is(st, w.src[SI_VISIBILITY], "hidden") || sv_word_is(st, w.src[SI_VISIBILITY], "collapse")) continue;
        vector<double> p;
        if (!sv_geometry(d, st, shape, &w, &p)) continue;
        sv_w(lines, "k");
        sv_m(lines, sm);
        sv_n(lines, sv_word_is(st, w.src[SI_CLIP_RULE], "evenodd"));
        sv_path_out(lines, &p);
        sv_w(lines, "\n");
        count++;
    }
    return count;
}

/* the clipPath a clip-path value (cv) names: 0 for none, or one that is no clipPath */
static uint32_t sv_clip_of(const dom_document *d, const css_style *st, uint32_t cv) {
    if (st->cval[cv].unit != CSS_UNIT_TEXT) return 0;
    amb_span sp = st->cval[cv].text;
    vector<char> t;
    append_range(t, text_ptr(st, sp), sp.len);
    size_t end;
    uint32_t cp = sv_url(d, data(t), len(t), &end);
    return cp && sv_is(d, cp, "clipPath") ? cp : 0;
}

/* a group's lines, its opacity and its clip (cp a clipPath, 0 for none), each line followed
   by its clip's shapes: a clipPath's own clip-path a group about it. How many were written
   (the caller ends each) */
static int sv_group_open(SB, uint32_t x, uint32_t cp, double op, const sv_walk *pw, const sv_walk *w, sv_mat mm, int depth) {
    if (op >= 1 && !cp) return 0;
    double bb[4] = { 0, 0, 0, 0 };
    int hb = 0, groups = 0;
    vector<char> v;
    if (cp && sv_attr_text(d, cp, "clipPathUnits", &v)) hb = sv_bounds(SA, x, pw, sv_id(), bb, depth, 0);
    uint32_t c0 = cp ? sv_first(firsts, cp) : 0;
    uint32_t outer = c0 ? sv_clip_of(d, st, c0 + CSS_PROP_CLIP_PATH) : 0;
    if (outer && outer != cp) {
        vector<char> ol;
        int ok = sv_clip_shapes(SA, outer, bb, hb, mm, w->vw, w->vh, &ol);
        sv_w(out, "g 1 2");
        sv_n(out, ok);
        sv_w(out, "\n");
        append_range(out, data(ol), len(ol));
        groups++;
    }
    vector<char> lines;
    int k = cp ? sv_clip_shapes(SA, cp, bb, hb, mm, w->vw, w->vh, &lines) : 0;
    sv_w(out, "g");
    sv_n(out, op);
    sv_n(out, cp ? 2 : 0);
    sv_n(out, k);
    sv_w(out, "\n");
    append_range(out, data(lines), len(lines));
    return groups + 1;
}

/* a shape's line: its matrix, its paints, its stroke, its path */
static void sv_shape(SB, uint32_t x, const sv_walk *w, sv_mat m) {
    vector<double> p;
    if (!sv_geometry(d, st, x, w, &p)) return;
    if (sv_word_is(st, w->src[SI_VISIBILITY], "hidden") || sv_word_is(st, w->src[SI_VISIBILITY], "collapse")) return;
    double bb[4];
    if (!sv_path_bounds(&p, sv_id(), bb)) return;
    uint32_t cur = st->cval[w->src[SI_COLOR]].color;
    sv_paint fp, spt;
    sv_paint_of(d, st, w->src[SI_FILL], cur, &fp);
    sv_paint_of(d, st, w->src[SI_STROKE], cur, &spt);
    uint32_t fg = 0, frgba = 0, sg = 0, srgba = 0;
    int fk = sv_is(d, x, "line") ? 0 : sv_resolve(SA, &fp, bb, w, &fg, &frgba);
    int sk = sv_resolve(SA, &spt, bb, w, &sg, &srgba);
    double sw = sv_css_len(st, w->src[SI_STROKE_WIDTH], sv_diag(w));
    if (sw <= 0) sk = 0;
    if (!fk && !sk) return;
    vector<char> line;
    sv_w(&line, "p");
    sv_m(&line, m);
    sv_paint_words(&line, fk, fg, frgba);
    sv_n(&line, st->cval[w->src[SI_FILL_OPACITY]].num);
    sv_n(&line, sv_word_is(st, w->src[SI_FILL_RULE], "evenodd"));
    sv_paint_words(&line, sk, sg, srgba);
    sv_n(&line, st->cval[w->src[SI_STROKE_OPACITY]].num);
    sv_n(&line, sw);
    uint32_t lc = w->src[SI_LINECAP], lj = w->src[SI_LINEJOIN];
    sv_n(&line, sv_word_is(st, lc, "round") ? 1 : sv_word_is(st, lc, "square") ? 2 : 0);
    sv_n(&line, sv_word_is(st, lj, "round") ? 1 : sv_word_is(st, lj, "bevel") ? 2 : 0);
    double ml = st->cval[w->src[SI_MITER]].num;
    sv_n(&line, ml >= 1 ? ml : 4);
    sv_n(&line, sv_css_len(st, w->src[SI_DASHOFFSET], sv_diag(w)));
    /* the dashes: none, or the list (a percentage of the diagonal), its count first */
    vector<double> dash;
    uint32_t dc = w->src[SI_DASHARRAY];
    if (st->cval[dc].unit == CSS_UNIT_TEXT) {
        amb_span sp = st->cval[dc].text;
        vector<char> t;
        append_range(t, text_ptr(st, sp), sp.len);
        size_t i = 0;
        while (i < len(t)) {
            i = sv_skip_sep(data(t), len(t), i);
            if (i >= len(t)) break;
            double v;
            if (!sv_number(data(t), len(t), &i, &v)) { clear(dash); break; }
            if (i < len(t) && at(t, i) == '%') { v = v * sv_diag(w) / 100; i++; }
            else if (i + 1 < len(t) && at(t, i) == 'p' && at(t, i + 1) == 'x') i += 2;
            push_back(dash, v);
        }
    }
    sv_n(&line, (double)len(dash));
    for (size_t k = 0; k < len(dash); k++) sv_n(&line, at(dash, k));
    sv_path_out(&line, &p);
    sv_w(&line, "\n");
    append_range(out, data(line), len(line));
}

/* a viewport (a nested svg, or a use's symbol or svg): its rectangle in its parent's user
   space, its viewBox mapped into it, its clip unless its overflow is visible; its children
   walked from the walk w (its own) */
static void sv_viewport(SB, uint32_t x, const sv_walk *w, sv_mat m, double vx, double vy, double vw, double vh, int depth) {
    if (vw <= 0 || vh <= 0) return;
    double vb[4], vp[4] = { vx, vy, vw, vh };
    sv_walk cw = *w;
    sv_mat cm = sv_mul(m, sv_make(1, 0, 0, 1, vx, vy));
    cw.vw = vw;
    cw.vh = vh;
    if (sv_viewbox_of(d, x, vb)) {
        cm = sv_mul(m, sv_viewbox(d, x, vb, vp));
        cw.vw = vb[2];
        cw.vh = vb[3];
    }
    uint32_t c0 = sv_first(firsts, x);
    int clip = !(c0 && st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    if (clip) {
        sv_w(out, "g 1 1 1\nk");
        sv_m(out, m);
        sv_w(out, " 0 M");
        sv_n(out, vx); sv_n(out, vy);
        sv_w(out, " L"); sv_n(out, vx + vw); sv_n(out, vy);
        sv_w(out, " L"); sv_n(out, vx + vw); sv_n(out, vy + vh);
        sv_w(out, " L"); sv_n(out, vx); sv_n(out, vy + vh);
        sv_w(out, " Z\n");
    }
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) sv_node(SA, c, &cw, cm, depth + 1);
    if (clip) sv_w(out, "e\n");
}

/* a use: the element its href names drawn as if it were the use's child, moved by x and y
   (a symbol or an svg a viewport of the use's width and height) */
static void sv_use(SB, uint32_t x, const sv_walk *w, sv_mat m, int depth) {
    uint32_t ref = sv_href(d, x);
    if (!ref || len(*uses) >= 16) return;
    for (size_t k = 0; k < len(*uses); k++) if (at(*uses, k) == ref) return;
    for (uint32_t a = d->node[x].parent; a; a = d->node[a].parent) if (a == ref) return;
    double fs = sv_fs(st, w);
    int g;
    double ux = sv_len_attr(d, x, "x", w->vw, fs, 0, &g), uy = sv_len_attr(d, x, "y", w->vh, fs, 0, &g);
    sv_mat um = sv_mul(m, sv_make(1, 0, 0, 1, ux, uy));
    push_back(uses, ref);
    if (sv_is(d, ref, "symbol") || sv_is(d, ref, "svg")) {
        uint32_t c0 = sv_first(firsts, ref);
        if (!(c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none"))) {
            sv_walk rw;
            sv_inherit(st, firsts, ref, w, &rw);
            int gw, gh;
            double vw = sv_len_attr(d, x, "width", w->vw, fs, 0, &gw), vh = sv_len_attr(d, x, "height", w->vh, fs, 0, &gh);
            if (!gw) vw = sv_is(d, ref, "svg") ? sv_len_attr(d, ref, "width", w->vw, fs, w->vw, &gw) : w->vw;
            if (!gh) vh = sv_is(d, ref, "svg") ? sv_len_attr(d, ref, "height", w->vh, fs, w->vh, &gh) : w->vh;
            sv_viewport(SA, ref, &rw, um, 0, 0, vw, vh, depth + 1);
        }
    } else sv_node(SA, ref, w, um, depth + 1);
    resize(uses, len(*uses) - 1);
}

static void sv_text(SB, uint32_t x, const sv_walk *w, sv_mat m);

static void sv_node(SB, uint32_t x, const sv_walk *pw, sv_mat m, int depth) {
    if (depth > 64 || ++*work > SV_WORK || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_SVG) return;
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none")) return;
    int shape = sv_is(d, x, "rect") || sv_is(d, x, "circle") || sv_is(d, x, "ellipse") || sv_is(d, x, "line") || sv_is(d, x, "polyline") ||
                sv_is(d, x, "polygon") || sv_is(d, x, "path");
    int group = sv_is(d, x, "g") || sv_is(d, x, "a") || sv_is(d, x, "switch");
    int nested = sv_is(d, x, "svg"), use = sv_is(d, x, "use"), text = sv_is(d, x, "text");
    if (!shape && !group && !nested && !use && !text) return;
    sv_walk w;
    sv_inherit(st, firsts, x, pw, &w);
    sv_mat mm = sv_mul(m, sv_transform_of(d, st, firsts, x, &w));
    double op = c0 ? st->cval[c0 + CSS_PROP_OPACITY].num : 1;
    if (op <= 0) return;
    uint32_t cp = c0 ? sv_clip_of(d, st, c0 + CSS_PROP_CLIP_PATH) : 0;
    int grouped = sv_group_open(SA, x, cp, op, pw, &w, mm, depth);
    if (shape) sv_shape(SA, x, &w, mm);
    else if (group) {
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
            if (sv_is(d, x, "switch") && d->node[c].kind == DOM_NODE_KIND_ELEMENT && d->node[c].ns == DOM_NAMESPACE_SVG) {
                sv_node(SA, c, &w, mm, depth + 1);
                break;
            }
            sv_node(SA, c, &w, mm, depth + 1);
        }
    } else if (nested) {
        double fs = sv_fs(st, &w);
        int g;
        double vx = sv_len_attr(d, x, "x", pw->vw, fs, 0, &g), vy = sv_len_attr(d, x, "y", pw->vh, fs, 0, &g);
        double vw = sv_len_attr(d, x, "width", pw->vw, fs, pw->vw, &g), vh = sv_len_attr(d, x, "height", pw->vh, fs, pw->vh, &g);
        sv_viewport(SA, x, &w, mm, vx, vy, vw, vh, depth);
    } else if (use) sv_use(SA, x, &w, mm, depth);
    else sv_text(SA, x, &w, mm);
    for (int k = 0; k < grouped; k++) sv_w(out, "e\n");
}

/* ---- text (SVG 2, 11): its glyphs placed ---- */

/* a text's characters, each with what places and draws it: its code point; the element it is
   in (its style's); its x, y, dx, dy as the lists set them (set: bit k for each of the four) */
typedef struct sv_char { uint32_t cp, el, set; double x, y, dx, dy; } sv_char;

/* the characters of x's text nodes and tspans into cs, white space collapsed as CSS's normal
   collapses it (a run of spaces, tabs and newlines one space, none at the text's start);
   each element's own walk into ws (its index in els, with its style) */
static void sv_text_chars(const dom_document *d, uint32_t x, vector<sv_char> *cs, vector<uint32_t> *els, int *space, int depth) {
    if (depth > 32) return;
    push_back(els, x);
    uint32_t ei = (uint32_t)len(*els) - 1;
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) {
            amb_span sp = d->node[c].data;
            vector<char> t;
            append_range(t, text_ptr(d, sp), sp.len);
            size_t i = 0;
            while (i < len(t)) {
                uint32_t cp = amb_utf8_next(data(t), len(t), &i);
                if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
                    if (*space) continue;
                    *space = 1;
                    cp = ' ';
                } else *space = 0;
                sv_char ch;
                ch.cp = cp;
                ch.el = ei;
                ch.set = 0;
                ch.x = ch.y = ch.dx = ch.dy = 0;
                push_back(cs, ch);
            }
        } else if (sv_is(d, c, "tspan") || sv_is(d, c, "a")) sv_text_chars(d, c, cs, els, space, depth + 1);
    }
}

/* an element's x, y, dx or dy list set on its characters from the first, k (0 x, 1 y, 2 dx,
   3 dy), for those of its own and its descendants' (the descendants' set after) */
static void sv_text_list(const dom_document *d, uint32_t x, const char *name, int k, uint32_t ei, const vector<uint32_t> *els,
                         vector<sv_char> *cs, double basis, double fs) {
    vector<char> v;
    if (!sv_attr_text(d, x, name, &v)) return;
    /* the element's characters: those whose element is it or below it */
    size_t i = 0, ci = 0, n = len(*cs);
    while (ci < n) {
        uint32_t e = at(*cs, ci).el;
        int mine = 0;
        for (uint32_t a = at(*els, e); a; a = d->node[a].parent) if (a == x) { mine = 1; break; }
        if (mine) break;
        ci++;
    }
    (void)ei;
    while (i < len(v) && ci < n) {
        i = sv_skip_sep(data(v), len(v), i);
        if (i >= len(v)) break;
        size_t b = i;
        while (i < len(v) && !sv_ws(at(v, i)) && at(v, i) != ',') i++;
        int ok;
        double val = sv_length(data(v) + b, i - b, basis, fs, &ok);
        if (!ok) break;
        uint32_t e = at(*cs, ci).el;
        int mine = 0;
        for (uint32_t a = at(*els, e); a; a = d->node[a].parent) if (a == x) { mine = 1; break; }
        if (!mine) break;
        sv_char ch = at(*cs, ci);
        if (k == 0) ch.x = val;
        else if (k == 1) ch.y = val;
        else if (k == 2) ch.dx = val;
        else ch.dy = val;
        ch.set |= 1u << k;
        (*cs)[ci] = ch;
        ci++;
    }
}

/* the built-in face a walk's font names: its family and weight */
static int sv_face(const css_style *st, const sv_walk *w) {
    uint32_t fc = w->src[SI_FONT_FAMILY];
    const char *fam = st->cval[fc].word ? names_ptr(st, st->cval[fc].word) : "serif";
    int bold = st->cval[w->src[SI_FONT_WEIGHT]].num >= 600;
    int face = amb_face_named(fam, bold);
    return face < 0 ? (bold ? AMB_FACE_SERIF_BOLD : AMB_FACE_SERIF) : face;
}

/* a text element's glyphs: its characters placed by their lists and advances, each chunk
   (from an absolutely placed character) moved by its text-anchor; runs of one style each a
   line: its matrix, face, size, slant, paint, opacity, then each glyph's code point and
   place */
static void sv_text(SB, uint32_t x, const sv_walk *w, sv_mat m) {
    vector<sv_char> cs;
    vector<uint32_t> els;
    int space = 1;
    sv_text_chars(d, x, &cs, &els, &space, 0);
    while (len(cs) && at(cs, len(cs) - 1).cp == ' ') resize(cs, len(cs) - 1);
    if (!len(cs)) return;
    /* each element's walk, its parent's first */
    size_t ne = len(els);
    vector<sv_walk> ws;
    for (size_t k = 0; k < ne; k++) {
        uint32_t e = at(els, k);
        sv_walk pw = *w, cw;
        if (k > 0) {
            for (size_t q = 0; q < k; q++) if (at(els, q) == d->node[e].parent) pw = at(ws, q);
            sv_inherit(st, firsts, e, &pw, &cw);
        } else cw = *w;
        push_back(ws, cw);
    }
    /* the lists: the text's, then each tspan's over its own characters */
    for (size_t k = 0; k < ne; k++) {
        uint32_t e = at(els, k);
        sv_walk ew = at(ws, k);
        double fs = sv_fs(st, &ew);
        sv_text_list(d, e, "x", 0, (uint32_t)k, &els, &cs, w->vw, fs);
        sv_text_list(d, e, "y", 1, (uint32_t)k, &els, &cs, w->vh, fs);
        sv_text_list(d, e, "dx", 2, (uint32_t)k, &els, &cs, w->vw, fs);
        sv_text_list(d, e, "dy", 3, (uint32_t)k, &els, &cs, w->vh, fs);
    }
    /* the pen: each character's place, then each chunk moved by its anchor */
    size_t n = len(cs);
    vector<double> gx, gy;
    resize(gx, n);
    resize(gy, n);
    double px = 0, py = 0;
    size_t chunk = 0;
    for (size_t i = 0; i <= n; i++) {
        int starts = i == n || (i > 0 && (at(cs, i).set & 3));
        if (starts && i > 0) {
            /* the chunk before ends: its anchor (its first character's element's) */
            sv_walk aw = at(ws, at(cs, chunk).el);
            int an = sv_word_is(st, aw.src[SI_ANCHOR], "middle") ? 1 : sv_word_is(st, aw.src[SI_ANCHOR], "end") ? 2 : 0;
            if (an) {
                double x0 = at(gx, chunk), x1 = px, shift = an == 1 ? (x1 - x0) / 2 : x1 - x0;
                for (size_t q = chunk; q < i; q++) gx[q] = at(gx, q) - shift;
            }
            chunk = i;
        }
        if (i == n) break;
        sv_char ch = at(cs, i);
        if (ch.set & 1) px = ch.x;
        if (ch.set & 2) py = ch.y;
        if (ch.set & 4) px += ch.dx;
        if (ch.set & 8) py += ch.dy;
        gx[i] = px;
        gy[i] = py;
        sv_walk cwv = at(ws, ch.el);
        const sv_walk *cw = &cwv;
        int face = sv_face(st, cw);
        double fs = sv_fs(st, cw);
        px += amb_advance(f, face, ch.cp, fs);
        if (i + 1 < n && at(cs, i + 1).el == ch.el && !(at(cs, i + 1).set & 1)) px += amb_kern(f, face, ch.cp, at(cs, i + 1).cp, fs);
        uint32_t ls = cw->src[SI_LETTER_SPACING];
        if (st->cval[ls].unit == CSS_UNIT_PX) px += st->cval[ls].num;
        uint32_t wsp = cw->src[SI_WORD_SPACING];
        if (ch.cp == ' ' && st->cval[wsp].unit == CSS_UNIT_PX) px += st->cval[wsp].num;
    }
    /* runs of one element's characters, each a line */
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && at(cs, j).el == at(cs, i).el) j++;
        sv_walk cwv = at(ws, at(cs, i).el);
        const sv_walk *cw = &cwv;
        if (sv_word_is(st, cw->src[SI_VISIBILITY], "hidden") || sv_word_is(st, cw->src[SI_VISIBILITY], "collapse")) { i = j; continue; }
        int face = sv_face(st, cw);
        double fs = sv_fs(st, cw);
        double asc, desc, gap;
        amb_face_metrics(f, face, fs, &asc, &desc, &gap);
        double bb[4] = { 1e300, 1e300, -1e300, -1e300 };
        for (size_t q = i; q < j; q++) {
            double adv = amb_advance(f, face, at(cs, q).cp, fs);
            bb[0] = fmin(bb[0], at(gx, q));
            bb[2] = fmax(bb[2], at(gx, q) + adv);
            bb[1] = fmin(bb[1], at(gy, q) - asc);
            bb[3] = fmax(bb[3], at(gy, q) + desc);
        }
        uint32_t cur = st->cval[cw->src[SI_COLOR]].color;
        sv_paint fp;
        sv_paint_of(d, st, cw->src[SI_FILL], cur, &fp);
        uint32_t fg = 0, frgba = 0;
        int fk = sv_resolve(SA, &fp, bb, cw, &fg, &frgba);
        if (fk) {
            uint32_t sc = cw->src[SI_FONT_STYLE];
            int italic = sv_word_is(st, sc, "italic") || sv_word_is(st, sc, "oblique");
            vector<char> line;
            sv_w(&line, "t");
            sv_m(&line, m);
            sv_n(&line, face);
            sv_n(&line, fs);
            sv_n(&line, italic);
            sv_paint_words(&line, fk, fg, frgba);
            sv_n(&line, st->cval[cw->src[SI_FILL_OPACITY]].num);
            size_t count = 0;
            for (size_t q = i; q < j; q++) if (at(cs, q).cp != ' ') count++;
            sv_n(&line, (double)count);
            for (size_t q = i; q < j; q++) {
                if (at(cs, q).cp == ' ') continue;
                sv_n(&line, at(cs, q).cp);
                sv_n(&line, at(gx, q));
                sv_n(&line, at(gy, q));
            }
            sv_w(&line, "\n");
            append_range(out, data(line), len(line));
        }
        i = j;
    }
}

void amb_svg_scene(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t el, double vw,
                   double vh, vector<char> *out) {
    if (vw <= 0 || vh <= 0 || el >= d->node_len || !sv_is(d, el, "svg")) return;
    uint32_t c0 = sv_first(firsts, el);
    if (!c0) return;
    sv_walk w;
    memset(&w, 0, sizeof w);
    for (int k = 0; k < SI_N; k++) w.src[k] = c0 + (uint32_t)sv_iprops[k];
    w.vw = vw;
    w.vh = vh;
    sv_mat m = sv_id();
    double vb[4], vp[4] = { 0, 0, vw, vh };
    if (sv_viewbox_of(d, el, vb)) {
        m = sv_viewbox(d, el, vb, vp);
        w.vw = vb[2];
        w.vh = vb[3];
    }
    int clip = !(st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    vector<char> body;
    uint32_t ids = 0, work = 0;
    vector<uint32_t> uses;
    for (uint32_t c = d->node[el].first_child; c; c = d->node[c].next_sibling) sv_node(d, st, firsts, f, &body, &ids, &uses, &work, c, &w, m, 0);
    if (!len(body)) return;
    sv_w(out, "v");
    sv_n(out, vw);
    sv_n(out, vh);
    sv_n(out, clip);
    sv_w(out, "\n");
    append_range(out, data(body), len(body));
}

/* ---- the drawing: the scene read ---- */

/* the scene's next word from *i (on its line when line is 1): its place */
static int sv_tok(const char *s, size_t n, size_t *i, int line, size_t *a, size_t *k) {
    size_t p = *i;
    while (p < n && (s[p] == ' ' || (!line && s[p] == '\n'))) p++;
    if (p >= n || s[p] == '\n') { *i = p; return 0; }
    size_t b = p;
    while (p < n && s[p] != ' ' && s[p] != '\n') p++;
    *a = b;
    *k = p - b;
    *i = p;
    return 1;
}
static double sv_rnum(const char *s, size_t n, size_t *i) {
    size_t a, k, j;
    double v = 0;
    if (!sv_tok(s, n, i, 1, &a, &k)) return 0;
    j = a;
    if (!sv_number(s, a + k, &j, &v)) return 0;
    return v;
}
static uint32_t sv_rhex(const char *s, size_t n, size_t *i) {
    size_t a, k;
    uint32_t v = 0;
    if (!sv_tok(s, n, i, 1, &a, &k)) return 0;
    for (size_t j = a; j < a + k; j++) {
        char c = s[j];
        v = v * 16 + (uint32_t)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
    }
    return v;
}
static sv_mat sv_rmat(const char *s, size_t n, size_t *i) {
    double v[6];
    for (int k = 0; k < 6; k++) v[k] = sv_rnum(s, n, i);
    return sv_make(v[0], v[1], v[2], v[3], v[4], v[5]);
}
/* the rest of the line's path: its commands into p */
static void sv_rpath(const char *s, size_t n, size_t *i, vector<double> *p) {
    clear(*p);
    size_t a, k;
    while (sv_tok(s, n, i, 1, &a, &k)) {
        char c = s[a];
        if (c == 'M' || c == 'L') { double x = sv_rnum(s, n, i), y = sv_rnum(s, n, i); sv_put2(p, c, x, y); }
        else if (c == 'C' || c == 'K') {
            double v[6];
            for (int q = 0; q < 6; q++) v[q] = sv_rnum(s, n, i);
            sv_put_c(p, c, v[0], v[1], v[2], v[3], v[4], v[5]);
        } else if (c == 'Q') {
            double v[4];
            for (int q = 0; q < 4; q++) v[q] = sv_rnum(s, n, i);
            sv_put_quad(p, v[0], v[1], v[2], v[3]);
        } else if (c == 'Z') push_back(p, (double)SV_Z);
    }
}
static void sv_skip_line(const char *s, size_t n, size_t *i) {
    while (*i < n && s[*i] != '\n') (*i)++;
}

/* ---- curves flattened, strokes outlined ---- */

/* the path's subpaths under m, flattened (each curve's points within tol of it): their
   points into pts (x, y), each one's corner flag (1 a point the path names, 0 a curve's
   own) and subs: each subpath's first point, its count, and its flags (1 closed, 2 drawn:
   a command after its move) */
static void sv_flatten(const vector<double> *p, sv_mat m, double tol, vector<double> *pts, vector<uint8_t> *corner, vector<uint32_t> *subs) {
    clear(*pts);
    clear(*corner);
    clear(*subs);
    size_t i = 0, n = len(*p);
    uint32_t start = 0, flags = 0;
    int open = 0;
    double cx = 0, cy = 0;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_M) {
            if (open) { push_back(subs, start); push_back(subs, (uint32_t)(len(*pts) / 2) - start); push_back(subs, flags); }
            cx = at(*p, i + 1);
            cy = at(*p, i + 2);
            start = (uint32_t)(len(*pts) / 2);
            push_back(pts, sv_ax(m, cx, cy));
            push_back(pts, sv_ay(m, cx, cy));
            push_back(corner, (uint8_t)1);
            open = 1;
            flags = 0;
            i += 3;
        } else if (c == SV_L) {
            cx = at(*p, i + 1);
            cy = at(*p, i + 2);
            if (open) {
                push_back(pts, sv_ax(m, cx, cy));
                push_back(pts, sv_ay(m, cx, cy));
                push_back(corner, (uint8_t)1);
                flags |= 2;
            }
            i += 3;
        } else if (c == SV_C || c == SV_K || c == SV_Q) {
            double q[8];
            sv_curve_at(p, i, cx, cy, q);
            double z[8];
            for (int k = 0; k < 4; k++) { z[2 * k] = sv_ax(m, q[2 * k], q[2 * k + 1]); z[2 * k + 1] = sv_ay(m, q[2 * k], q[2 * k + 1]); }
            double ddx = fmax(fabs(z[0] - 2 * z[2] + z[4]), fabs(z[2] - 2 * z[4] + z[6]));
            double ddy = fmax(fabs(z[1] - 2 * z[3] + z[5]), fabs(z[3] - 2 * z[5] + z[7]));
            double dd = sqrt(ddx * ddx + ddy * ddy);
            double fsegs = ceil(sqrt(0.75 * dd / tol));
            int segs = fsegs >= 1 && fsegs <= 1000 ? (int)fsegs : fsegs > 1000 ? 1000 : 1;
            if (open) {
                /* a point just inside each end too, so that the curve's first and last chords
                   run along its tangents there: a join at its ends is then the one Skia makes
                   from the curve's own tangents (a miter's limit is met or not as there) */
                for (int k = 0; k <= segs + 1; k++) {
                    double t = k == 0 ? 1e-4 : k == segs + 1 ? 1 : k == segs ? 1 - 1e-4 : (double)k / segs, u = 1 - t;
                    double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
                    push_back(pts, b0 * z[0] + b1 * z[2] + b2 * z[4] + b3 * z[6]);
                    push_back(pts, b0 * z[1] + b1 * z[3] + b2 * z[5] + b3 * z[7]);
                    push_back(corner, (uint8_t)(k == segs + 1));
                }
                flags |= 2;
            }
            cx = q[6];
            cy = q[7];
            i += (size_t)sv_args(c) + 1;
        } else {
            if (open) {
                push_back(subs, start);
                push_back(subs, (uint32_t)(len(*pts) / 2) - start);
                push_back(subs, flags | 3);
            }
            open = 0;
            i++;
        }
    }
    if (open) { push_back(subs, start); push_back(subs, (uint32_t)(len(*pts) / 2) - start); push_back(subs, flags); }
}

/* a polygon of k points (q) added to edges under m, wound one way: its area made positive,
   so that the non-zero rule unites the pieces of a stroke */
static void sv_poly(const double *q, int k, sv_mat m, vector<double> *edges) {
    double area = 0;
    for (int j = 0; j < k; j++) {
        int l = j + 1 == k ? 0 : j + 1;
        area += q[2 * j] * q[2 * l + 1] - q[2 * l] * q[2 * j + 1];
    }
    for (int j = 0; j < k; j++) {
        int l = j + 1 == k ? 0 : j + 1;
        int a0 = area >= 0 ? j : l, a1 = area >= 0 ? l : j;
        double x0 = sv_ax(m, q[2 * a0], q[2 * a0 + 1]), y0 = sv_ay(m, q[2 * a0], q[2 * a0 + 1]);
        double x1 = sv_ax(m, q[2 * a1], q[2 * a1 + 1]), y1 = sv_ay(m, q[2 * a1], q[2 * a1 + 1]);
        if (y0 == y1) continue;
        push_back(edges, x0);
        push_back(edges, y0);
        push_back(edges, x1);
        push_back(edges, y1);
    }
}
/* a disc of radius r about (cx, cy), its sides within 0.05 of the device's px of its circle
   (rd: its radius there) */
static void sv_disc(double cx, double cy, double r, double rd, sv_mat m, vector<double> *edges, vector<double> *tmp) {
    double fk = rd > 0.05 ? ceil(M_PI / acos(1 - 0.05 / rd)) : 8;
    int k = fk >= 8 && fk <= 720 ? (int)fk : fk > 720 ? 720 : 8;
    clear(*tmp);
    for (int j = 0; j < k; j++) {
        double t = 2 * M_PI * j / k;
        push_back(tmp, cx + r * cos(t));
        push_back(tmp, cy + r * sin(t));
    }
    sv_poly(data(*tmp), k, m, edges);
}
static void sv_quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, sv_mat m, vector<double> *edges) {
    double q[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
    sv_poly(q, 4, m, edges);
}

/* the stroke's style: half its width, cap (0 butt, 1 round, 2 square), join (0 miter, 1
   round, 2 bevel), miter limit, and the device's px a unit (for round pieces) */
typedef struct sv_pen { double hw, miter, rd; int cap, join; } sv_pen;

/* a polyline's stroke (k points at q, their corner flags at cf; closed or open; (tx, ty) the
   direction of one that is a point alone) as pieces into edges under m: each segment's
   quad, each joint's join (the path's own at its corners, a smooth one inside a curve), the
   caps of an open one */
static void sv_stroke_line(const double *q, const uint8_t *cf, int k, int closed, double tx, double ty, const sv_pen *pen, sv_mat m,
                           vector<double> *edges, vector<double> *tmp, vector<double> *cl, vector<uint8_t> *ccf) {
    clear(*cl);
    clear(*ccf);
    for (int j = 0; j < k; j++) {
        double x = q[2 * j], y = q[2 * j + 1];
        size_t c = len(*cl);
        if (c >= 2 && fabs(at(*cl, c - 2) - x) < 1e-9 && fabs(at(*cl, c - 1) - y) < 1e-9) {
            if (cf[j]) (*ccf)[c / 2 - 1] = 1;
            continue;
        }
        push_back(cl, x);
        push_back(cl, y);
        push_back(ccf, cf[j]);
    }
    int np = (int)(len(*cl) / 2);
    if (closed && np > 1 && fabs(at(*cl, 0) - at(*cl, 2 * np - 2)) < 1e-9 && fabs(at(*cl, 1) - at(*cl, 2 * np - 1)) < 1e-9) {
        np--;
        (*ccf)[0] = 1;
    }
    if (np <= 0) return;
    double hw = pen->hw;
    if (np == 1) {
        double px = at(*cl, 0), py = at(*cl, 1);
        if (pen->cap == 1) sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp);
        else if (pen->cap == 2) {
            double l = sqrt(tx * tx + ty * ty);
            double ux = l > 0 ? tx / l : 1, uy = l > 0 ? ty / l : 0;
            double nx = -uy * hw, ny = ux * hw;
            sv_quad(px - ux * hw + nx, py - uy * hw + ny, px + ux * hw + nx, py + uy * hw + ny, px + ux * hw - nx, py + uy * hw - ny,
                    px - ux * hw - nx, py - uy * hw - ny, m, edges);
        }
        return;
    }
    int segs = closed ? np : np - 1;
    for (int j = 0; j < segs; j++) {
        int l = j + 1 == np ? 0 : j + 1;
        double x0 = at(*cl, 2 * j), y0 = at(*cl, 2 * j + 1), x1 = at(*cl, 2 * l), y1 = at(*cl, 2 * l + 1);
        double dx = x1 - x0, dy = y1 - y0, len0 = sqrt(dx * dx + dy * dy);
        double nx = -dy / len0 * hw, ny = dx / len0 * hw;
        sv_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, m, edges);
    }
    /* the joins */
    for (int j = closed ? 0 : 1; j < (closed ? np : np - 1); j++) {
        int pj = j == 0 ? np - 1 : j - 1, nj = j + 1 == np ? 0 : j + 1;
        double px = at(*cl, 2 * j), py = at(*cl, 2 * j + 1);
        double ax = px - at(*cl, 2 * pj), ay = py - at(*cl, 2 * pj + 1), bx = at(*cl, 2 * nj) - px, by = at(*cl, 2 * nj + 1) - py;
        double la = sqrt(ax * ax + ay * ay), lb = sqrt(bx * bx + by * by);
        ax /= la; ay /= la; bx /= lb; by /= lb;
        double cross = ax * by - ay * bx, dot = ax * bx + ay * by;
        if (fabs(cross) < 1e-9 && dot > 0) continue;
        int smooth = !at(*ccf, j);
        int jt = smooth ? (dot < 0.5 ? 1 : 0) : pen->join;
        if (jt == 1) { sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp); continue; }
        double sg = cross > 0 ? -1 : 1;
        double n1x = -ay * hw * sg, n1y = ax * hw * sg, n2x = -by * hw * sg, n2y = bx * hw * sg;
        if (jt == 0 && dot > -1 + 1e-9) {
            double ratio = sqrt(2 / (1 + dot));
            if (smooth || ratio <= pen->miter) {
                double tipx = px + (n1x + n2x) / (1 + dot), tipy = py + (n1y + n2y) / (1 + dot);
                sv_quad(px, py, px + n1x, py + n1y, tipx, tipy, px + n2x, py + n2y, m, edges);
                continue;
            }
        }
        double t3[6] = { px, py, px + n1x, py + n1y, px + n2x, py + n2y };
        sv_poly(t3, 3, m, edges);
    }
    /* the caps */
    if (!closed && pen->cap) {
        for (int e = 0; e < 2; e++) {
            int j = e ? np - 1 : 0, o = e ? np - 2 : 1;
            double px = at(*cl, 2 * j), py = at(*cl, 2 * j + 1);
            double ux = px - at(*cl, 2 * o), uy = py - at(*cl, 2 * o + 1), l = sqrt(ux * ux + uy * uy);
            ux /= l;
            uy /= l;
            if (pen->cap == 1) sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp);
            else {
                double nx = -uy * hw, ny = ux * hw;
                sv_quad(px + nx, py + ny, px + nx + ux * hw, py + ny + uy * hw, px - nx + ux * hw, py - ny + uy * hw, px - nx, py - ny, m, edges);
            }
        }
    }
}

/* ---- dashes as Skia cuts them: SkDashPath over SkContourMeasure ----
   A contour is measured by its curves' chords: each curve halved (in t, kept as Skia keeps
   it in 30 bits) while it bends more than the tolerance (half a device px) from its chord --
   a cubic by its control points against its chord's thirds, a quadratic by its control
   point, an arc's piece (Skia's conic) by its middle -- and a distance along the contour
   is a t interpolated within its chord. A dash is then the curves between two such
   places, cut exactly. So a dash on a curve sits where Skia puts it, a little short of
   where the curve's true length would. */

enum { SV_TMAX = 0x3fffffff };
/* a contour's measured piece: how far along the contour it ends, its curve (its record's
   place in the curves: its kind, then four points), and the curve's t there, of SV_TMAX */
typedef struct sv_mseg { double dist; uint32_t ci, t; } sv_mseg;

static void sv_bez_pt(const double *q, int kind, double t, double *x, double *y) {
    double u = 1 - t;
    if (kind == SV_L) { *x = q[0] + (q[2] - q[0]) * t; *y = q[1] + (q[3] - q[1]) * t; return; }
    if (kind == SV_Q) {
        *x = u * u * q[0] + 2 * u * t * q[2] + t * t * q[4];
        *y = u * u * q[1] + 2 * u * t * q[3] + t * t * q[5];
        return;
    }
    double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    *x = b0 * q[0] + b1 * q[2] + b2 * q[4] + b3 * q[6];
    *y = b0 * q[1] + b1 * q[3] + b2 * q[5] + b3 * q[7];
}
/* a cubic (or a quadratic, its three points) cut at t: its two parts' points into a and b */
static void sv_cut(const double *q, int quad, double t, double *a, double *b) {
    for (int c = 0; c < 2; c++) {
        if (quad) {
            double p01 = q[c] + (q[2 + c] - q[c]) * t, p12 = q[2 + c] + (q[4 + c] - q[2 + c]) * t, m = p01 + (p12 - p01) * t;
            a[c] = q[c]; a[2 + c] = p01; a[4 + c] = m;
            b[c] = m; b[2 + c] = p12; b[4 + c] = q[4 + c];
            a[6 + c] = b[6 + c] = 0;
        } else {
            double p01 = q[c] + (q[2 + c] - q[c]) * t, p12 = q[2 + c] + (q[4 + c] - q[2 + c]) * t, p23 = q[4 + c] + (q[6 + c] - q[4 + c]) * t;
            double p012 = p01 + (p12 - p01) * t, p123 = p12 + (p23 - p12) * t, m = p012 + (p123 - p012) * t;
            a[c] = q[c]; a[2 + c] = p01; a[4 + c] = p012; a[6 + c] = m;
            b[c] = m; b[2 + c] = p123; b[4 + c] = p23; b[6 + c] = q[6 + c];
        }
    }
}
static double sv_cheap(double ax, double ay, double bx, double by) { return fmax(fabs(ax - bx), fabs(ay - by)); }
static void sv_mseg_add(vector<sv_mseg> *ms, double *dist, double d, uint32_t ci, int t) {
    double prev = *dist;
    *dist += d;
    if (*dist > prev) {
        sv_mseg m;
        m.dist = *dist;
        m.ci = ci;
        m.t = (uint32_t)t;
        push_back(ms, m);
    }
}
/* a cubic's or a quadratic's pieces (q: the piece from mint to maxt), halved as Skia halves them */
static void sv_meas_bez(const double *q, int quad, double *dist, int mint, int maxt, uint32_t ci, double tol, vector<sv_mseg> *ms) {
    int curvy;
    if (quad) curvy = sv_cheap(q[2] / 2, q[3] / 2, (q[0] + q[4]) / 4, (q[1] + q[5]) / 4) > tol;
    else curvy = sv_cheap(q[2], q[3], q[0] + (q[6] - q[0]) / 3, q[1] + (q[7] - q[1]) / 3) > tol ||
                 sv_cheap(q[4], q[5], q[0] + (q[6] - q[0]) * 2 / 3, q[1] + (q[7] - q[1]) * 2 / 3) > tol;
    if (((maxt - mint) >> 10) && curvy) {
        double a[8], b[8];
        int half = (mint + maxt) >> 1;
        sv_cut(q, quad, 0.5, a, b);
        sv_meas_bez(a, quad, dist, mint, half, ci, tol, ms);
        sv_meas_bez(b, quad, dist, half, maxt, ci, tol, ms);
        return;
    }
    sv_mseg_add(ms, dist, quad ? hypot(q[4] - q[0], q[5] - q[1]) : hypot(q[6] - q[0], q[7] - q[1]), ci, maxt);
}
/* an arc's piece (its cubic q, whole), halved as Skia halves a conic: by the curve's middle
   between the two points it has at mint and maxt */
static void sv_meas_arc(const double *q, double *dist, int mint, double mx, double my, int maxt, double nx, double ny, uint32_t ci,
                        double tol, vector<sv_mseg> *ms) {
    int half = (mint + maxt) >> 1;
    double hx, hy;
    sv_bez_pt(q, SV_C, (double)half / SV_TMAX, &hx, &hy);
    if (((maxt - mint) >> 10) && sv_cheap(hx, hy, (mx + nx) / 2, (my + ny) / 2) > tol) {
        sv_meas_arc(q, dist, mint, mx, my, half, hx, hy, ci, tol, ms);
        sv_meas_arc(q, dist, half, hx, hy, maxt, nx, ny, ci, tol, ms);
        return;
    }
    sv_mseg_add(ms, dist, hypot(nx - mx, ny - my), ci, maxt);
}

/* the place a distance along a contour (its pieces ms[m0, m1)) falls: its piece, and the
   curve's t there */
static size_t sv_dist_at(const vector<sv_mseg> *ms, size_t m0, size_t m1, double dist, double *t) {
    size_t lo = m0, hi = m1 - 1;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (at(*ms, mid).dist < dist) lo = mid + 1;
        else hi = mid;
    }
    sv_mseg sg = at(*ms, lo);
    double t0 = 0, d0 = 0;
    if (lo > m0) {
        d0 = at(*ms, lo - 1).dist;
        if (at(*ms, lo - 1).ci == sg.ci) t0 = (double)at(*ms, lo - 1).t / SV_TMAX;
    }
    double t1 = (double)sg.t / SV_TMAX;
    *t = sg.dist > d0 ? t0 + (t1 - t0) * (dist - d0) / (sg.dist - d0) : t1;
    return lo;
}

/* a curve (cv at ci: its kind, then its points) from t0 to t1 appended to out as a command */
static void sv_seg_to(const vector<double> *cv, uint32_t ci, double t0, double t1, vector<double> *out) {
    int kind = (int)at(*cv, ci);
    double q[8];
    for (int k = 0; k < 8; k++) q[k] = at(*cv, ci + 1 + (size_t)k);
    if (t0 == t1) {
        /* a dash of no length: a line of none, which the stroke caps */
        size_t n = len(*out);
        if (n >= 2) sv_put2(out, SV_L, at(*out, n - 2), at(*out, n - 1));
        return;
    }
    if (kind == SV_L) {
        double x, y;
        sv_bez_pt(q, SV_L, t1, &x, &y);
        sv_put2(out, SV_L, x, y);
        return;
    }
    int quad = kind == SV_Q;
    /* the piece from 0 to t1, then of it the part from t0 / t1 (de Casteljau's) */
    double a[8], b[8];
    if (t1 < 1) {
        sv_cut(q, quad, t1, a, b);
        for (int k = 0; k < 8; k++) q[k] = a[k];
    }
    if (t0 > 0 && t1 > 0) {
        sv_cut(q, quad, t0 / t1, a, b);
        for (int k = 0; k < 8; k++) q[k] = b[k];
    }
    if (quad) sv_put_quad(out, q[2], q[3], q[4], q[5]);
    else sv_put_c(out, kind, q[2], q[3], q[4], q[5], q[6], q[7]);
}
/* a contour's part from distance d0 to d1 appended to out, a move first when move is 1 */
static void sv_get_segment(const vector<double> *cv, const vector<sv_mseg> *ms, size_t m0, size_t m1, double d0, double d1, int move,
                           vector<double> *out) {
    double length = at(*ms, m1 - 1).dist;
    if (d0 < 0) d0 = 0;
    if (d1 > length) d1 = length;
    if (!(d0 <= d1)) return;
    double t0, t1;
    size_t s0 = sv_dist_at(ms, m0, m1, d0, &t0), s1 = sv_dist_at(ms, m0, m1, d1, &t1);
    uint32_t c0 = at(*ms, s0).ci, c1 = at(*ms, s1).ci;
    if (move) {
        double x, y, q[8];
        for (int k = 0; k < 8; k++) q[k] = at(*cv, c0 + 1 + (size_t)k);
        sv_bez_pt(q, (int)at(*cv, c0), t0, &x, &y);
        sv_put2(out, SV_M, x, y);
    }
    if (c0 == c1) { sv_seg_to(cv, c0, t0, t1, out); return; }
    sv_seg_to(cv, c0, t0, 1, out);
    for (uint32_t c = c0 + 9; c < c1; c += 9) sv_seg_to(cv, c, 0, 1, out);
    sv_seg_to(cv, c1, 0, t1, out);
}

/* the path p dashed (each subpath a contour of its own; nd lengths, an odd count's repeated;
   the pattern begun offset into it), its dashes as subpaths into out; tol the measure's.
   0 when the lengths sum to nothing (the path is not dashed) */
static int sv_dash_path(const vector<double> *p, const double *dash, int nd, double offset, double tol, vector<double> *out, vector<double> *cv,
                        vector<sv_mseg> *ms) {
    clear(*out);
    int count = nd % 2 ? 2 * nd : nd;
    double total = 0;
    for (int k = 0; k < count; k++) total += dash[k % nd];
    if (!(total > 0)) return 0;
    /* the phase, and the dash it begins in, as SkDashPath finds them */
    double phase = offset;
    if (phase < 0) {
        phase = -phase;
        if (phase > total) phase = fmod(phase, total);
        phase = total - phase;
        if (phase == total) phase = 0;
    } else if (phase >= total) phase = fmod(phase, total);
    int first = 0;
    double first_len = dash[0];
    for (int k = 0; k < count; k++) {
        double gap = dash[k % nd];
        if (phase > gap || (phase == gap && gap > 0)) phase -= gap;
        else { first = k; first_len = gap - phase; break; }
    }
    size_t i = 0, n = len(*p);
    long budget = 100000;
    while (i < n) {
        /* a contour: its curves and their measure */
        clear(*cv);
        clear(*ms);
        double sx = 0, sy = 0, cx = 0, cy = 0, dist = 0;
        int closed = 0;
        if ((int)at(*p, i) == SV_M) { sx = cx = at(*p, i + 1); sy = cy = at(*p, i + 2); i += 3; }
        while (i < n && (int)at(*p, i) != SV_M) {
            int c = (int)at(*p, i);
            if (c == SV_Z) { closed = 1; i++; break; }
            double q[8];
            uint32_t ci = (uint32_t)len(*cv);
            q[0] = cx;
            q[1] = cy;
            for (int k = 2; k < 8; k++) q[k] = 0;
            int k = sv_args(c);
            for (int j = 0; j < k; j++) q[2 + j] = at(*p, i + 1 + (size_t)j);
            push_back(cv, (double)c);
            for (int j = 0; j < 8; j++) push_back(cv, q[j]);
            if (c == SV_L) sv_mseg_add(ms, &dist, hypot(q[2] - q[0], q[3] - q[1]), ci, SV_TMAX);
            else if (c == SV_Q) sv_meas_bez(q, 1, &dist, 0, SV_TMAX, ci, tol, ms);
            else if (c == SV_K) sv_meas_arc(q, &dist, 0, q[0], q[1], SV_TMAX, q[6], q[7], ci, tol, ms);
            else sv_meas_bez(q, 0, &dist, 0, SV_TMAX, ci, tol, ms);
            cx = at(*p, i + (size_t)k - 1);
            cy = at(*p, i + (size_t)k);
            i += (size_t)k + 1;
        }
        if (closed) {
            uint32_t ci = (uint32_t)len(*cv);
            double q[8] = { cx, cy, sx, sy, 0, 0, 0, 0 };
            push_back(cv, (double)SV_L);
            for (int k = 0; k < 8; k++) push_back(cv, q[k]);
            sv_mseg_add(ms, &dist, hypot(sx - cx, sy - cy), ci, SV_TMAX);
        }
        size_t m1 = len(*ms);
        if (!m1 || !(dist < 1e9)) continue;
        double length = dist, d = 0, dl = first_len;
        int index = first, skip = closed, added = 0;
        while (d < length && budget-- > 0) {
            added = 0;
            if (index % 2 == 0 && !skip) {
                added = 1;
                sv_get_segment(cv, ms, 0, m1, d, d + dl, 1, out);
            }
            d += dl;
            skip = 0;
            index = index + 1 == count ? 0 : index + 1;
            dl = dash[index % nd];
        }
        /* a closed contour's first dash, skipped, joined to its last when that reaches it */
        if (closed && first % 2 == 0 && first_len >= 0) sv_get_segment(cv, ms, 0, m1, 0, first_len, !added, out);
    }
    return 1;
}

/* ---- coverage: sixteen rows a pixel, each span's ends exact across it ---- */

enum { SV_SUB = 16 };

/* the coverage of what edges bound (x0 y0 x1 y1 each, the layer's px) under the non-zero
   rule or the even-odd, in the layer's w x h: the rectangle it reaches into b (x0, y0, x1,
   y1), each of its pixels' coverage into cov (b's width a row); 0 when it reaches none */
static int sv_cover(vector<double> *edges, int evenodd, int w, int h, int *b, vector<float> *cov, vector<double> *xs,
                    vector<uint32_t> *order, vector<uint32_t> *act, vector<float> *acc) {
    /* edges past any canvas (or not numbers at all) clamped far outside it, their slopes kept
       finite */
    for (size_t q = 0; q < len(*edges); q++) {
        double v = at(*edges, q);
        if (!(v > -1e7)) (*edges)[q] = -1e7;
        else if (v > 1e7) (*edges)[q] = 1e7;
    }
    size_t ne = len(*edges) / 4;
    if (!ne) return 0;
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    for (size_t e = 0; e < ne; e++) {
        double x0 = at(*edges, 4 * e), y0 = at(*edges, 4 * e + 1), x1 = at(*edges, 4 * e + 2), y1 = at(*edges, 4 * e + 3);
        mnx = fmin(mnx, fmin(x0, x1));
        mxx = fmax(mxx, fmax(x0, x1));
        mny = fmin(mny, fmin(y0, y1));
        mxy = fmax(mxy, fmax(y0, y1));
    }
    if (!(mnx < 1e299) || mxx < 0 || mxy < 0 || mnx > w || mny > h) return 0;
    int bx0 = mnx < 0 ? 0 : (int)floor(mnx), by0 = mny < 0 ? 0 : (int)floor(mny);
    int bx1 = mxx >= w ? w : (int)ceil(mxx) + 1, by1 = mxy >= h ? h : (int)ceil(mxy);
    if (bx1 > w) bx1 = w;
    if (by1 > h) by1 = h;
    if (bx1 <= bx0 || by1 <= by0) return 0;
    b[0] = bx0; b[1] = by0; b[2] = bx1; b[3] = by1;
    int bw = bx1 - bx0, bh = by1 - by0;
    clear(*cov);
    resize(cov, (size_t)bw * (size_t)bh);
    /* the edges by the row each begins in (above the box: its first) */
    clear(*order);
    resize(order, (size_t)bh + 1 + ne);
    for (size_t e = 0; e < ne; e++) {
        double y0 = fmin(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3));
        int r = y0 < by0 ? 0 : (int)floor(y0) - by0;
        if (r >= bh) continue;
        (*order)[(size_t)r + 1]++;
    }
    for (int r = 0; r < bh; r++) (*order)[(size_t)r + 1] += at(*order, (size_t)r);
    for (size_t e = 0; e < ne; e++) {
        double y0 = fmin(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3));
        int r = y0 < by0 ? 0 : (int)floor(y0) - by0;
        if (r >= bh) continue;
        uint32_t slot = at(*order, (size_t)r);
        (*order)[(size_t)r]++;
        (*order)[(size_t)bh + 1 + slot] = (uint32_t)e;
    }
    /* order[r] is now where row r's edges end; they begin where row r-1's end */
    clear(*act);
    clear(*acc);
    resize(acc, 2 * ((size_t)bw + 2));
    size_t base = (size_t)bh + 1;
    float wt = 1.0f / SV_SUB;
    for (int r = 0; r < bh; r++) {
        int y = by0 + r;
        uint32_t from = r == 0 ? 0 : at(*order, (size_t)r - 1), to = at(*order, (size_t)r);
        for (uint32_t s = from; s < to; s++) push_back(act, at(*order, base + s));
        /* those that end above this row leave */
        size_t keep = 0;
        for (size_t q = 0; q < len(*act); q++) {
            uint32_t e = at(*act, q);
            if (fmax(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3)) > y) (*act)[keep++] = e;
        }
        resize(act, keep);
        if (!keep) continue;
        for (size_t q = 0; q < len(*acc); q++) (*acc)[q] = 0;
        for (int sub = 0; sub < SV_SUB; sub++) {
            double sy = y + (sub + 0.5) / SV_SUB;
            clear(*xs);
            for (size_t q = 0; q < keep; q++) {
                uint32_t e = at(*act, q);
                double x0 = at(*edges, 4 * e), y0 = at(*edges, 4 * e + 1), x1 = at(*edges, 4 * e + 2), y1 = at(*edges, 4 * e + 3);
                double lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
                if (sy < lo || sy >= hi) continue;
                double xx = x0 + (sy - y0) * (x1 - x0) / (y1 - y0);
                /* in order by x: the crossings kept sorted as they come */
                push_back(xs, xx);
                push_back(xs, y1 > y0 ? 1.0 : -1.0);
                size_t m = len(*xs) / 2 - 1;
                while (m > 0 && at(*xs, 2 * m - 2) > xx) {
                    (*xs)[2 * m] = at(*xs, 2 * m - 2);
                    (*xs)[2 * m + 1] = at(*xs, 2 * m - 1);
                    m--;
                }
                (*xs)[2 * m] = xx;
                (*xs)[2 * m + 1] = y1 > y0 ? 1.0 : -1.0;
            }
            size_t nx = len(*xs) / 2;
            int wind = 0;
            for (size_t q = 0; q + 1 < nx; q++) {
                wind += (int)at(*xs, 2 * q + 1);
                int in = evenodd ? (wind & 1) : wind != 0;
                if (!in) continue;
                double fa = at(*xs, 2 * q) - bx0, fb = at(*xs, 2 * q + 2) - bx0;
                if (fa < 0) fa = 0;
                if (fb > bw) fb = bw;
                if (fb <= fa) continue;
                int ia = (int)floor(fa), ib = (int)floor(fb);
                if (ia == ib) (*acc)[(size_t)ia] += (float)(fb - fa) * wt;
                else {
                    (*acc)[(size_t)ia] += (float)(ia + 1 - fa) * wt;
                    (*acc)[(size_t)bw + 2 + (size_t)ia + 1] += wt;
                    (*acc)[(size_t)bw + 2 + (size_t)ib] -= wt;
                    (*acc)[(size_t)ib] += (float)(fb - ib) * wt;
                }
            }
        }
        float run = 0;
        size_t row = (size_t)r * (size_t)bw;
        for (int x = 0; x < bw; x++) {
            run += at(*acc, (size_t)bw + 2 + (size_t)x);
            float v = at(*acc, (size_t)x) + run;
            (*cov)[row + (size_t)x] = v > 1 ? 1 : v < 0 ? 0 : v;
        }
    }
    return 1;
}

/* ---- paints drawn ---- */

/* what a shape is painted with: none, a colour (premultiplied, its opacity in), or a
   gradient (its record in the gradients, the matrix from the layer's px to its space, its
   opacity) */
typedef struct sv_ink { int kind; float r, g, b, a; size_t goff; sv_mat inv; } sv_ink;

/* a gradient's colour at t (its record at o: its kind, matrix, geometry, spread, stops), premultiplied */
static void sv_stop_at(const vector<double> *gr, size_t o, double t, float *rgba) {
    int kind = (int)at(*gr, o);
    size_t sp = o + 7 + (kind ? 6 : 4);
    int spread = (int)at(*gr, sp), ns = (int)at(*gr, sp + 1);
    size_t s0 = sp + 2;
    if (spread == 1) { t = fmod(t, 2); if (t < 0) t += 2; if (t > 1) t = 2 - t; }
    else if (spread == 2) t = t - floor(t);
    else t = t < 0 ? 0 : t > 1 ? 1 : t;
    int k = 0;
    while (k + 1 < ns && at(*gr, s0 + 2 * ((size_t)k + 1)) <= t) k++;
    uint32_t c0 = (uint32_t)at(*gr, s0 + 2 * (size_t)k + 1), c1 = c0;
    double f = 0;
    if (k + 1 < ns && t > at(*gr, s0 + 2 * (size_t)k)) {
        double a = at(*gr, s0 + 2 * (size_t)k), b = at(*gr, s0 + 2 * ((size_t)k + 1));
        c1 = (uint32_t)at(*gr, s0 + 2 * ((size_t)k + 1) + 1);
        f = b > a ? (t - a) / (b - a) : 1;
    } else if (t < at(*gr, s0)) c1 = c0;
    double ch[4];
    for (int q = 0; q < 4; q++) {
        double u = (double)(c0 >> (24 - 8 * q) & 0xff), v = (double)(c1 >> (24 - 8 * q) & 0xff);
        ch[q] = (u + (v - u) * f) / 255;
    }
    rgba[0] = (float)(ch[0] * ch[3]);
    rgba[1] = (float)(ch[1] * ch[3]);
    rgba[2] = (float)(ch[2] * ch[3]);
    rgba[3] = (float)ch[3];
}
/* a gradient's colour at the layer's point (px, py), premultiplied: 0 where it paints nothing */
static int sv_grad_at(const vector<double> *gr, const sv_ink *ink, double px, double py, float *rgba) {
    size_t o = ink->goff;
    int kind = (int)at(*gr, o);
    double gx = sv_ax(ink->inv, px, py), gy = sv_ay(ink->inv, px, py), t;
    size_t g = o + 7;
    if (!kind) {
        double x1 = at(*gr, g), y1 = at(*gr, g + 1), dx = at(*gr, g + 2) - x1, dy = at(*gr, g + 3) - y1, l2 = dx * dx + dy * dy;
        t = l2 > 0 ? ((gx - x1) * dx + (gy - y1) * dy) / l2 : 1;
    } else {
        double cx = at(*gr, g), cy = at(*gr, g + 1), r = at(*gr, g + 2), fx = at(*gr, g + 3), fy = at(*gr, g + 4), fr = at(*gr, g + 5);
        if (r <= 0) t = 1;
        else {
            /* the two-point conical gradient from the focal circle to the end one: the
               greatest t whose circle passes through the point */
            double cdx = cx - fx, cdy = cy - fy, dr = r - fr, pdx = gx - fx, pdy = gy - fy;
            double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + fr * dr, c = pdx * pdx + pdy * pdy - fr * fr;
            if (fabs(a) < 1e-12) {
                if (fabs(b) < 1e-12) return 0;
                t = c / (2 * b);
                if (fr + t * dr < 0) return 0;
            } else {
                double disc = b * b - a * c;
                if (disc < 0) return 0;
                double sq = sqrt(disc), t1 = (b + sq) / a, t2 = (b - sq) / a;
                double hi = fmax(t1, t2), lo = fmin(t1, t2);
                if (fr + hi * dr >= 0) t = hi;
                else if (fr + lo * dr >= 0) t = lo;
                else return 0;
            }
        }
    }
    sv_stop_at(gr, o, t, rgba);
    for (int q = 0; q < 4; q++) rgba[q] *= ink->a;
    return 1;
}

/* the coverage cov over b painted into the layer at loff (w px a row) with ink */
static void sv_paint_cov(vector<float> *lay, size_t loff, int w, const vector<float> *cov, const int *b, const sv_ink *ink, const vector<double> *gr) {
    int bw = b[2] - b[0];
    for (int y = b[1]; y < b[3]; y++)
        for (int x = b[0]; x < b[2]; x++) {
            float cv = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)(x - b[0]));
            if (cv <= 0) continue;
            float c[4] = { ink->r, ink->g, ink->b, ink->a };
            if (ink->kind == 2 && !sv_grad_at(gr, ink, x + 0.5, y + 0.5, c)) continue;
            size_t q = loff + ((size_t)y * (size_t)w + (size_t)x) * 4;
            float a = c[3] * cv;
            if (a <= 0) continue;
            float keep = 1 - a;
            (*lay)[q] = c[0] * cv + at(*lay, q) * keep;
            (*lay)[q + 1] = c[1] * cv + at(*lay, q + 1) * keep;
            (*lay)[q + 2] = c[2] * cv + at(*lay, q + 2) * keep;
            (*lay)[q + 3] = a + at(*lay, q + 3) * keep;
        }
}

/* ---- the drawing's state: its layers, masks, gradients and scratch ---- */

/* where the drawing is: the layer's size and its place in the canvas's px, and the matrix
   from the viewport's CSS px to its px */
typedef struct sv_dev { int w, h, x0, y0; sv_mat dev; } sv_dev;

#define RC const sv_dev *dv, vector<float> *lay, vector<double> *lop, vector<float> *masks, vector<int32_t> *lmk, vector<double> *gr, \
           vector<uint32_t> *gix, vector<double> *path, vector<double> *edges, vector<float> *cov, vector<double> *pts, vector<uint8_t> *corner, \
           vector<uint32_t> *subs, vector<double> *dp, vector<sv_mseg> *dcf, vector<double> *ds, vector<double> *tmp, vector<double> *cl, \
           vector<uint8_t> *ccf, vector<double> *xs, vector<uint32_t> *order, vector<uint32_t> *act, vector<float> *acc
#define RA dv, lay, lop, masks, lmk, gr, gix, path, edges, cov, pts, corner, subs, dp, dcf, ds, tmp, cl, ccf, xs, order, act, acc

static size_t sv_top(RC) { return len(*lay) - (size_t)dv->w * (size_t)dv->h * 4; }

/* a paint's words read: n, c RRGGBBAA, u N; with its opacity, under the path's matrix m */
static void sv_rink(const char *s, size_t n, size_t *i, RC, sv_mat m, sv_ink *ink) {
    memset(ink, 0, sizeof *ink);
    size_t a, k;
    if (!sv_tok(s, n, i, 1, &a, &k)) return;
    if (s[a] == 'c') {
        uint32_t c = sv_rhex(s, n, i);
        ink->kind = 1;
        float al = (float)(c & 0xff) / 255;
        ink->r = (float)(c >> 24) / 255 * al;
        ink->g = (float)(c >> 16 & 0xff) / 255 * al;
        ink->b = (float)(c >> 8 & 0xff) / 255 * al;
        ink->a = al;
    } else if (s[a] == 'u') {
        uint32_t id = (uint32_t)sv_rnum(s, n, i);
        if (id < len(*gix) && at(*gix, id)) {
            size_t o = at(*gix, id) - 1;
            sv_mat gm = sv_make(at(*gr, o + 1), at(*gr, o + 2), at(*gr, o + 3), at(*gr, o + 4), at(*gr, o + 5), at(*gr, o + 6));
            if (sv_inv(sv_mul(dv->dev, sv_mul(m, gm)), &ink->inv)) {
                ink->kind = 2;
                ink->goff = o;
                ink->a = 1;
            }
        }
    }
}
static void sv_ink_opacity(sv_ink *ink, double op) {
    float o = (float)(op < 0 ? 0 : op > 1 ? 1 : op);
    ink->r *= o; ink->g *= o; ink->b *= o; ink->a *= o;
}

/* the path's fill: its subpaths closed, under the full matrix */
static void sv_fill_edges(RC, sv_mat full) {
    sv_flatten(path, full, 0.05, pts, corner, subs);
    clear(*edges);
    for (size_t s = 0; s + 2 < len(*subs); s += 3) {
        uint32_t b = at(*subs, s), c = at(*subs, s + 1);
        for (uint32_t j = 0; j < c; j++) {
            uint32_t l = j + 1 == c ? 0 : j + 1;
            double x0 = at(*pts, 2 * (b + j)), y0 = at(*pts, 2 * (b + j) + 1), x1 = at(*pts, 2 * (b + l)), y1 = at(*pts, 2 * (b + l) + 1);
            if (y0 == y1) continue;
            push_back(edges, x0);
            push_back(edges, y0);
            push_back(edges, x1);
            push_back(edges, y1);
        }
    }
}

/* the path's stroke as pieces under full (its user space's matrix): dashed first, as Skia
   cuts dashes (its measure's tolerance half a device px), then each subpath outlined in
   user space; a stroke no wider than a device px outlined a px wide in the device's, its
   alpha its width (*alpha), as Skia draws a hairline */
static void sv_stroke_edges(RC, sv_mat full, const sv_pen *pen0, const double *dash, int nd, double offset, double *alpha) {
    sv_pen pen = *pen0;
    double w = 2 * pen.hw;
    double c0 = sqrt(full.a * full.a + full.b * full.b), c1 = sqrt(full.c * full.c + full.d * full.d);
    double l0 = c0 * w, l1 = c1 * w, rs = fmax(c0, c1);
    double scale = sv_scale(full);
    int hair = l0 <= 1 && l1 <= 1;
    *alpha = hair ? (l0 + l1) / 2 : 1;
    int dashed = nd > 0 && sv_dash_path(path, dash, nd, offset, 0.5 / (rs > 0 ? rs : 1), dp, ds, dcf);
    sv_flatten(dashed ? dp : path, hair ? full : sv_id(), hair ? 0.05 : 0.05 / (scale > 0 ? scale : 1), pts, corner, subs);
    clear(*edges);
    sv_mat lm = hair ? sv_id() : full;
    pen.rd = hair ? 1 : scale;
    if (hair) pen.hw = 0.5;
    for (size_t s = 0; s + 2 < len(*subs); s += 3) {
        uint32_t b = at(*subs, s), c = at(*subs, s + 1), fl = at(*subs, s + 2);
        if (!(fl & 2)) continue;
        /* a subpath of no length capped upright, as Skia caps one */
        sv_stroke_line(data(*pts) + 2 * b, data(*corner) + b, (int)c, (fl & 1) != 0, 1, 0, &pen, lm, edges, tmp, cl, ccf);
    }
}

/* a path's line: its matrix, fill (paint, opacity, rule), stroke (paint, opacity, width,
   cap, join, miter limit, dash offset, dashes), path */
static void sv_draw_path(const char *s, size_t n, size_t *i, RC) {
    sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
    sv_ink fi, si;
    sv_rink(s, n, i, RA, m, &fi);
    sv_ink_opacity(&fi, sv_rnum(s, n, i));
    int evenodd = (int)sv_rnum(s, n, i);
    sv_rink(s, n, i, RA, m, &si);
    sv_ink_opacity(&si, sv_rnum(s, n, i));
    sv_pen pen;
    pen.hw = sv_rnum(s, n, i) / 2;
    pen.cap = (int)sv_rnum(s, n, i);
    pen.join = (int)sv_rnum(s, n, i);
    pen.miter = sv_rnum(s, n, i);
    pen.rd = 1;
    double offset = sv_rnum(s, n, i);
    int nd = (int)sv_rnum(s, n, i);
    double dash[64];
    int kd = 0, bad = 0;
    for (int k = 0; k < nd; k++) {
        double v = sv_rnum(s, n, i);
        if (v < 0) bad = 1;
        if (kd < 64) dash[kd++] = v;
    }
    if (bad) kd = 0;
    sv_rpath(s, n, i, path);
    size_t top = sv_top(RA);
    int b[4];
    if (fi.kind) {
        sv_fill_edges(RA, full);
        if (sv_cover(edges, evenodd, dv->w, dv->h, b, cov, xs, order, act, acc)) sv_paint_cov(lay, top, dv->w, cov, b, &fi, gr);
    }
    if (si.kind && pen.hw > 0) {
        double alpha = 1;
        sv_stroke_edges(RA, full, &pen, dash, kd, offset, &alpha);
        sv_ink_opacity(&si, alpha);
        if (sv_cover(edges, 0, dv->w, dv->h, b, cov, xs, order, act, acc)) sv_paint_cov(lay, top, dv->w, cov, b, &si, gr);
    }
}

/* a gradient's line kept: kind, its matrix, its geometry, spread and stops */
static void sv_read_grad(const char *s, size_t n, size_t *i, int radial, RC) {
    uint32_t id = (uint32_t)sv_rnum(s, n, i);
    if (id == 0 || id > 100000) { sv_skip_line(s, n, i); return; }
    if (len(*gix) <= id) resize(gix, (size_t)id + 1);
    (*gix)[id] = (uint32_t)len(*gr) + 1;
    push_back(gr, (double)radial);
    for (int k = 0; k < 6 + (radial ? 6 : 4) + 1; k++) push_back(gr, sv_rnum(s, n, i));
    int ns = (int)sv_rnum(s, n, i);
    if (ns < 0 || ns > 4096) ns = 0;
    push_back(gr, (double)ns);
    for (int k = 0; k < ns; k++) {
        push_back(gr, sv_rnum(s, n, i));
        push_back(gr, (double)sv_rhex(s, n, i));
    }
    sv_skip_line(s, n, i);
}

/* the flattened path (pts, subs) is one rectangle along the axes, as SkPath::isRect finds one */
static int sv_is_rect(const vector<double> *pts, const vector<uint32_t> *subs) {
    if (len(*subs) != 3) return 0;
    uint32_t b = at(*subs, 0), c = at(*subs, 1);
    if (c == 5 && at(*pts, 2 * b) == at(*pts, 2 * (b + 4)) && at(*pts, 2 * b + 1) == at(*pts, 2 * (b + 4) + 1)) c = 4;
    if (c != 4) return 0;
    double x[4], y[4];
    for (uint32_t j = 0; j < 4; j++) { x[j] = at(*pts, 2 * (b + j)); y[j] = at(*pts, 2 * (b + j) + 1); }
    int a = x[0] == x[1] && y[1] == y[2] && x[2] == x[3] && y[3] == y[0];
    int h = y[0] == y[1] && x[1] == x[2] && y[2] == y[3] && x[3] == x[0];
    return (a || h) && x[0] != x[2] && y[0] != y[2];
}

/* a group's line: a layer of its own, its clip's shapes (their union) its mask; a
   clip-path's (hasclip 2) times its bounding box's own coverage, as Blink clips one: a
   rectangle about it, then its path, each anti-aliased, Skia multiplying the two (a path
   that is a rectangle along the axes a rectangle's clip alone) */
static void sv_push(const char *s, size_t n, size_t *i, RC) {
    double op = sv_rnum(s, n, i);
    int hasclip = (int)sv_rnum(s, n, i), nk = (int)sv_rnum(s, n, i);
    sv_skip_line(s, n, i);
    size_t px = (size_t)dv->w * (size_t)dv->h;
    int32_t mk = -1;
    if (hasclip) {
        mk = (int32_t)(len(*masks) / (px ? px : 1));
        resize(masks, len(*masks) + px);
    }
    size_t mo = mk >= 0 ? (size_t)mk * px : 0;
    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    int rect = 0;
    for (int k = 0; k < nk; k++) {
        size_t a, kl;
        if (!sv_tok(s, n, i, 0, &a, &kl) || s[a] != 'k') { sv_skip_line(s, n, i); continue; }
        sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
        int evenodd = (int)sv_rnum(s, n, i);
        sv_rpath(s, n, i, path);
        int b[4];
        sv_fill_edges(RA, full);
        rect = nk == 1 && sv_is_rect(pts, subs);
        for (size_t q = 0; q + 3 < len(*edges); q += 2) {
            bx0 = fmin(bx0, at(*edges, q));
            bx1 = fmax(bx1, at(*edges, q));
            by0 = fmin(by0, at(*edges, q + 1));
            by1 = fmax(by1, at(*edges, q + 1));
        }
        if (mk >= 0 && sv_cover(edges, evenodd, dv->w, dv->h, b, cov, xs, order, act, acc)) {
            int bw = b[2] - b[0];
            for (int y = b[1]; y < b[3]; y++)
                for (int x = b[0]; x < b[2]; x++) {
                    float v = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)(x - b[0]));
                    size_t q = mo + (size_t)y * (size_t)dv->w + (size_t)x;
                    if (v > at(*masks, q)) (*masks)[q] = v;
                }
        }
    }
    if (hasclip == 2 && !rect && mk >= 0 && bx1 > bx0 && by1 > by0)
        for (int y = 0; y < dv->h; y++) {
            double cy = fmin(y + 1, by1) - fmax(y, by0);
            for (int x = 0; x < dv->w; x++) {
                size_t q = mo + (size_t)y * (size_t)dv->w + (size_t)x;
                if (at(*masks, q) <= 0) continue;
                double cx = fmin(x + 1, bx1) - fmax(x, bx0);
                double k = (cx < 0 ? 0 : cx > 1 ? 1 : cx) * (cy < 0 ? 0 : cy > 1 ? 1 : cy);
                (*masks)[q] = (float)(at(*masks, q) * k);
            }
        }
    resize(lay, len(*lay) + px * 4);
    push_back(lop, op);
    push_back(lmk, mk);
}
/* a group's end: its layer blended into the one under it, by its opacity and mask */
static void sv_pop(RC) {
    size_t px = (size_t)dv->w * (size_t)dv->h;
    if (len(*lop) == 0 || len(*lay) < 2 * px * 4) return;
    double op = at(*lop, len(*lop) - 1);
    int32_t mk = at(*lmk, len(*lmk) - 1);
    size_t top = len(*lay) - px * 4, under = top - px * 4, mo = mk >= 0 ? (size_t)mk * px : 0;
    float o = (float)(op < 0 ? 0 : op > 1 ? 1 : op);
    for (size_t p = 0; p < px; p++) {
        float k = mk >= 0 ? o * at(*masks, mo + p) : o;
        size_t q = top + 4 * p, u = under + 4 * p;
        float a = at(*lay, q + 3) * k;
        if (a <= 0) continue;
        float keep = 1 - a;
        (*lay)[u] = at(*lay, q) * k + at(*lay, u) * keep;
        (*lay)[u + 1] = at(*lay, q + 1) * k + at(*lay, u + 1) * keep;
        (*lay)[u + 2] = at(*lay, q + 2) * k + at(*lay, u + 2) * keep;
        (*lay)[u + 3] = a + at(*lay, u + 3) * keep;
    }
    resize(lay, top);
    resize(lop, len(*lop) - 1);
    resize(lmk, len(*lmk) - 1);
    if (mk >= 0) resize(masks, mo);
}

static void sv_draw_glyphs(const char *s, size_t n, size_t *i, RC, const amb_fonts *f);

/* a scene's commands from s[i] drawn into a layer of w x h px (premultiplied RGBA,
   transparent to begin with; into layer), dev mapping the viewport's CSS px into it */
static void sv_render(const char *s, size_t n, size_t i, int w, int h, sv_mat dev, vector<float> *layer) {
    size_t a, k;
    sv_dev dvv;
    dvv.w = w;
    dvv.h = h;
    dvv.x0 = 0;
    dvv.y0 = 0;
    dvv.dev = dev;
    const sv_dev *dv = &dvv;
    vector<float> layv, masksv, covv, accv;
    vector<double> lopv, grv, pathv, edgesv, ptsv, dpv, dsv, tmpv, clv, xsv;
    vector<int32_t> lmkv;
    vector<uint32_t> gixv, subsv, orderv, actv;
    vector<uint8_t> cornerv, ccfv;
    vector<sv_mseg> dcfv;
    resize(layv, (size_t)dvv.w * (size_t)dvv.h * 4);
    amb_fonts fonts = {0};
    defer(fonts) { AMB_FONTS_DROP(fonts); }
    int fonts_loaded = 0;
    while (sv_tok(s, n, &i, 0, &a, &k)) {
        char cmd = s[a];
        if (cmd == 'p' && k == 1)
            sv_draw_path(s, n, &i, dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv, &dsv,
                         &tmpv, &clv, &ccfv, &xsv, &orderv, &actv, &accv);
        else if ((cmd == 'l' || cmd == 'r') && k == 1)
            sv_read_grad(s, n, &i, cmd == 'r', dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv,
                         &dcfv, &dsv, &tmpv, &clv, &ccfv, &xsv, &orderv, &actv, &accv);
        else if (cmd == 'g' && k == 1)
            sv_push(s, n, &i, dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv, &dsv,
                    &tmpv, &clv, &ccfv, &xsv, &orderv, &actv, &accv);
        else if (cmd == 'e' && k == 1)
            sv_pop(dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv, &dsv, &tmpv, &clv,
                   &ccfv, &xsv, &orderv, &actv, &accv);
        else if (cmd == 't' && k == 1) {
            if (!fonts_loaded) { amb_fonts_load(&fonts); fonts_loaded = 1; }
            sv_draw_glyphs(s, n, &i, dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv,
                           &dsv, &tmpv, &clv, &ccfv, &xsv, &orderv, &actv, &accv, &fonts);
        }
        sv_skip_line(s, n, &i);
    }
    /* groups left open end here */
    while (len(lopv))
        sv_pop(dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv, &dsv, &tmpv, &clv,
               &ccfv, &xsv, &orderv, &actv, &accv);
    clear(*layer);
    append_range(layer, data(layv), len(layv));
}

/* a scene's first line: its viewport's size and whether it clips to it; where its
   commands begin */
static int sv_head(const char *s, size_t n, double *vw, double *vh, int *clip, size_t *i) {
    size_t a, k;
    *i = 0;
    if (!sv_tok(s, n, i, 0, &a, &k) || k != 1 || s[a] != 'v') return 0;
    *vw = sv_rnum(s, n, i);
    *vh = sv_rnum(s, n, i);
    *clip = (int)sv_rnum(s, n, i);
    sv_skip_line(s, n, i);
    return *vw > 0 && *vh > 0;
}

void amb_svg_draw(amb_canvas *c, const char *s, size_t n, double x, double y, double sx, double sy) {
    size_t i;
    double vw, vh;
    int clip;
    if (!sv_head(s, n, &vw, &vh, &clip, &i)) return;
    /* the canvas's clip, its px */
    int kx0 = (int)floor(c->cx0 + 0.5), kx1 = (int)floor(c->cx1 + 0.5), ky0 = (int)floor(c->cy0 - c->oy + 0.5), ky1 = (int)floor(c->cy1 - c->oy + 0.5);
    if (kx0 < 0) kx0 = 0;
    if (ky0 < 0) ky0 = 0;
    if (kx1 > (int)c->w) kx1 = (int)c->w;
    if (ky1 > (int)c->h) ky1 = (int)c->h;
    /* the viewport's place rounded to whole CSS px, as Chromium places an svg's content (then
       scaled to the device's px, which a zoom may leave between two) */
    double z = c->zoom > 0 ? c->zoom : 1;
    double X0 = floor(x / z + 0.5) * z, Y0 = floor(y / z + 0.5) * z - c->oy;
    if (!(fabs(X0) < 1e8 && fabs(Y0) < 1e8 && sx > 0 && sy > 0 && sx < 1e4 && sy < 1e4)) return;
    int vx0 = kx0, vy0 = ky0, vx1 = kx1, vy1 = ky1;
    if (clip) {
        double ex = fmin(fmax(X0 + vw * sx, -1e8), 1e8), ey = fmin(fmax(Y0 + vh * sy, -1e8), 1e8);
        int ax0 = (int)floor(X0 + 1e-6), ay0 = (int)floor(Y0 + 1e-6), ax1 = (int)ceil(ex - 1e-6), ay1 = (int)ceil(ey - 1e-6);
        if (ax0 > vx0) vx0 = ax0;
        if (ay0 > vy0) vy0 = ay0;
        if (ax1 < vx1) vx1 = ax1;
        if (ay1 < vy1) vy1 = ay1;
    }
    if (vx1 <= vx0 || vy1 <= vy0 || (double)(vx1 - vx0) * (vy1 - vy0) > 3e7) return;
    int lw = vx1 - vx0, lh = vy1 - vy0;
    vector<float> layv;
    sv_render(s, n, i, lw, lh, sv_make(sx, 0, 0, sy, X0 - vx0, Y0 - vy0), &layv);
    /* the layer blended onto the canvas, its viewport's clip anti-aliased where a zoom leaves
       its edges between two px */
    double ex = X0 + vw * sx, ey = Y0 + vh * sy;
    for (int yy = 0; yy < lh; yy++) {
        double py = vy0 + yy, cy = clip ? fmin(py + 1, ey) - fmax(py, Y0) : 1;
        if (cy <= 0) continue;
        for (int xx = 0; xx < lw; xx++) {
            size_t q = ((size_t)yy * (size_t)lw + (size_t)xx) * 4;
            float al = at(layv, q + 3);
            if (al <= 0) continue;
            double px = vx0 + xx, cx = clip ? fmin(px + 1, ex) - fmax(px, X0) : 1;
            if (cx <= 0) continue;
            double k = (cx > 1 ? 1 : cx) * (cy > 1 ? 1 : cy);
            size_t p = ((size_t)(vy0 + yy) * c->w + (size_t)(vx0 + xx)) * 3;
            for (int ch = 0; ch < 3; ch++) {
                double v = at(layv, q + (size_t)ch) * 255 * k + c->px[p + (size_t)ch] * (1 - al * k);
                c->px[p + (size_t)ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
        }
    }
}

/* a run of glyphs' line: its matrix, face, size, slant, paint and opacity, then each glyph's
   code point and place */
static void sv_draw_glyphs(const char *s, size_t n, size_t *i, RC, const amb_fonts *f) {
    sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
    int face = (int)sv_rnum(s, n, i);
    double size = sv_rnum(s, n, i);
    int italic = (int)sv_rnum(s, n, i);
    sv_ink ink;
    sv_rink(s, n, i, RA, m, &ink);
    sv_ink_opacity(&ink, sv_rnum(s, n, i));
    int count = (int)sv_rnum(s, n, i);
    if (face < 0 || face >= (int)f->nfaces) face = AMB_FACE_SERIF;
    /* the device's size: the matrix's scale (a rotated or skewed run drawn upright, at its
       area's scale) */
    double dsize = size * sqrt(fabs(full.a * full.d - full.b * full.c));
    if (!ink.kind || dsize <= 0 || dsize > 2000) return;
    size_t top = sv_top(RA);
    vector<float> facc;
    vector<uint8_t> mask;
    for (int g = 0; g < count; g++) {
        uint32_t cp = (uint32_t)sv_rnum(s, n, i);
        double gx = sv_rnum(s, n, i), gy = sv_rnum(s, n, i);
        double X = sv_ax(full, gx, gy), Y = sv_ay(full, gx, gy);
        if (!(fabs(X) < 1e7 && fabs(Y) < 1e7)) continue;
        /* as the page's text is set: its pen at a quarter of a px across, its baseline at a whole px */
        double fx = floor(X);
        int quarter = (int)((X - fx) * 4);
        int by = (int)floor(Y + 0.5);
        int w, h, ox, oy;
        if (!amb_glyph_mask(f, face, cp, dsize, quarter / 4.0, italic, &facc, &mask, &w, &h, &ox, &oy)) continue;
        for (int yy = 0; yy < h; yy++) {
            int py = by + oy + yy;
            if (py < 0 || py >= dv->h) continue;
            for (int xx = 0; xx < w; xx++) {
                int px = (int)fx + ox + xx;
                if (px < 0 || px >= dv->w) continue;
                uint8_t mv = at(mask, (size_t)yy * (size_t)w + (size_t)xx);
                if (!mv) continue;
                float cv = mv / 255.0f;
                float c[4] = { ink.r, ink.g, ink.b, ink.a };
                if (ink.kind == 2 && !sv_grad_at(gr, &ink, px + 0.5, py + 0.5, c)) continue;
                size_t q = top + ((size_t)py * (size_t)dv->w + (size_t)px) * 4;
                float a = c[3] * cv, keep = 1 - a;
                (*lay)[q] = c[0] * cv + at(*lay, q) * keep;
                (*lay)[q + 1] = c[1] * cv + at(*lay, q + 1) * keep;
                (*lay)[q + 2] = c[2] * cv + at(*lay, q + 2) * keep;
                (*lay)[q + 3] = a + at(*lay, q + 3) * keep;
            }
        }
    }
}

void amb_svg_coverage(const char *scene, size_t n, uint32_t w, uint32_t h, vector<uint8_t> *out) {
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    c.w = w;
    c.h = h;
    c.oy = 0;
    c.cx0 = 0;
    c.cy0 = 0;
    c.cx1 = w;
    c.cy1 = h;
    c.zoom = 1;
    c.background = 0xffffffff;
    resize(c.px, (size_t)w * h * 3);
    for (size_t k = 0; k < len(c.px); k++) c.px[k] = 255;
    amb_svg_draw(&c, scene, n, 0, 0, 1, 1);
    clear(*out);
    for (size_t k = 0; k < (size_t)w * h; k++) push_back(out, (uint8_t)(255 - c.px[3 * k]));
}

/* ---- SVG documents as images: this package's last step ---- */

int amb_svg_size(const uint8_t *bytes, size_t n, double *w, double *h, double *ratio) {
    (void)bytes;
    (void)n;
    *w = *h = *ratio = 0;
    return 0;
}
int amb_svg_size_of(const net_loader *nt, uint32_t r, double *w, double *h, double *ratio) {
    (void)nt;
    (void)r;
    *w = *h = *ratio = 0;
    return 0;
}
int amb_svg_image(amb_canvas *c, const uint8_t *p, size_t n, double x, double y, double w, double h) {
    (void)c; (void)p; (void)n; (void)x; (void)y; (void)w; (void)h;
    return 0;
}
int amb_svg_image_of(amb_canvas *c, const net_loader *nt, uint32_t el, double x, double y, double w, double h) {
    (void)c; (void)nt; (void)el; (void)x; (void)y; (void)w; (void)h;
    return 0;
}
int amb_svg_raster_of(const net_loader *nt, uint32_t r, uint32_t w, uint32_t h, vector<uint8_t> *rgba) {
    (void)nt; (void)r; (void)w; (void)h; (void)rgba;
    return 0;
}
