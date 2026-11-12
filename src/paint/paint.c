/* src/paint/paint.c -- a laid-out page drawn, and written as a PNG (paint.h).

   The order is CSS 2.1's (Appendix E), simplified: the canvas, its colour the
   root element's background (or the body's, propagated); then each stacking
   context -- the root, and each positioned box -- paints its own box, the
   positioned boxes in it with a negative z-index, its flow (each box in tree
   order: its shadows, its background, its borders, its lines -- inline
   elements' backgrounds, text and its shadows, decorations, list markers --
   its child boxes, its outline), then those with z-index auto or 0 in tree
   order and those with a positive one by z-index. A box that clips its overflow
   clips what is in it to its padding box.

   A box: its outer box-shadows (offset, spread, blurred: three box blurs, a
   Gaussian's), its background colour and layers (url() images, linear and
   radial gradients; background-size, -position and -repeat; the last layer
   first) in its border box, rounded by its corners' radii; its borders, solid,
   dotted, dashed, double, groove, ridge, inset or outset (Chromium's darker
   colour for the shaded sides), round where its corners are. A box with an
   opacity is drawn and blended with what was under it; one with a transform
   (translate, scale, rotate, skew, matrix, about its transform-origin) is drawn
   on black and on white, which give its colours and their alpha, and mapped
   onto the canvas, sampled bilinearly. Glyphs come from font.c at their size and
   quarter-pixel position, cached. An image is drawn from the bytes the loader
   fetched for its element (image.c decodes PNG), sampled bilinearly into its
   content box; one that cannot be decoded is a grey placeholder. The PNG is
   written with this file's own deflate: each row filtered (None, Sub or Up,
   whichever sums smallest), LZ77 over a hash of three bytes, fixed Huffman
   codes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "font.h"

amb_interface(dom);
amb_interface(layout);
amb_interface(net);

#include "paint.h"
#include "image.h"
#include "../css/css.h"
#include "../layout/layout.h"

/* ---- the canvas ---- */

/* the clip as the canvas's pixels: [x0, x1) x [y0, y1) */
static void pt_clip_px(const amb_canvas *c, int *x0, int *y0, int *x1, int *y1) {
    *x0 = (int)floor(c->cx0 + 0.5);
    *x1 = (int)floor(c->cx1 + 0.5);
    *y0 = (int)floor(c->cy0 - c->oy + 0.5);
    *y1 = (int)floor(c->cy1 - c->oy + 0.5);
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > (int)c->w) *x1 = (int)c->w;
    if (*y1 > (int)c->h) *y1 = (int)c->h;
}

static void pt_fill(amb_canvas *c, double x0, double y0, double x1, double y1, uint32_t rgba) {
    uint32_t a = rgba & 0xff;
    if (a == 0) return;
    y0 -= c->oy;
    y1 -= c->oy;
    int ix0 = (int)floor(x0 + 0.5), iy0 = (int)floor(y0 + 0.5), ix1 = (int)floor(x1 + 0.5), iy1 = (int)floor(y1 + 0.5);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    if (ix0 < kx0) ix0 = kx0;
    if (iy0 < ky0) iy0 = ky0;
    if (ix1 > kx1) ix1 = kx1;
    if (iy1 > ky1) iy1 = ky1;
    uint32_t r = rgba >> 24, g = rgba >> 16 & 0xff, b = rgba >> 8 & 0xff;
    if (ix1 <= ix0 || iy1 <= iy0) return;
    if (a == 255) {
        /* opaque: its first row written a pixel at a time, every other row a copy of it (PLAN.md 11) */
        size_t first = ((size_t)iy0 * c->w + (size_t)ix0) * 3, rowb = (size_t)(ix1 - ix0) * 3;
        for (size_t i = first; i < first + rowb; i += 3) { c->px[i] = (uint8_t)r; c->px[i + 1] = (uint8_t)g; c->px[i + 2] = (uint8_t)b; }
        uint8_t *base = data(c->px);
        for (int y = iy0 + 1; y < iy1; y++) memcpy(base + ((size_t)y * c->w + (size_t)ix0) * 3, base + first, rowb);
        return;
    }
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            c->px[i] = (uint8_t)((r * a + c->px[i] * (255 - a)) / 255);
            c->px[i + 1] = (uint8_t)((g * a + c->px[i + 1] * (255 - a)) / 255);
            c->px[i + 2] = (uint8_t)((b * a + c->px[i + 2] * (255 - a)) / 255);
        }
}

/* ---- glyphs, cached by face, size, code point, quarter pixel and slant ---- */

typedef struct pt_glyph { uint64_t key; uint32_t off; int32_t w, h, ox, oy; } pt_glyph;

static uint64_t pt_key(int face, double size, uint32_t cp, int quarter, int italic) {
    uint64_t sz = (uint64_t)(size * 16 + 0.5) & 0xffff;
    return (uint64_t)(face & 31) << 59 | (uint64_t)(italic & 1) << 58 | (uint64_t)(quarter & 3) << 56 | sz << 40 | (uint64_t)(cp & 0x1fffff);
}

static uint32_t pt_slot(const vector<uint32_t> *table, uint64_t key) {
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(h >> 40) & (uint32_t)(len(*table) - 1);
}

/* the cached glyph's index in entries, drawn on its first use */
static uint32_t pt_glyph_of(const amb_fonts *f, vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks,
                            vector<float> *acc, vector<uint8_t> *mask, int face, double size, uint32_t cp, int quarter, int italic) {
    uint64_t key = pt_key(face, size, cp, quarter, italic);
    uint32_t i = pt_slot(table, key);
    while (at(*table, i)) {
        uint32_t e = at(*table, i) - 1;
        if (at(*entries, e).key == key) return e;
        i = (i + 1) & (uint32_t)(len(*table) - 1);
    }
    pt_glyph g;
    int w, h, ox, oy;
    memset(&g, 0, sizeof g);
    g.key = key;
    g.off = (uint32_t)len(*masks);
    if (amb_glyph_mask(f, face, cp, size, quarter / 4.0, italic, acc, mask, &w, &h, &ox, &oy)) {
        append_range(masks, data(*mask), len(*mask));
        g.w = w;
        g.h = h;
        g.ox = ox;
        g.oy = oy;
    }
    uint32_t e = (uint32_t)len(*entries);
    push_back(entries, g);
    (*table)[i] = e + 1;
    if (2 * len(*entries) > len(*table)) {
        /* twice the slots, every entry placed again */
        size_t n = 2 * len(*table);
        clear(*table);
        resize(table, n);
        for (uint32_t k = 0; k < (uint32_t)len(*entries); k++) {
            uint32_t s = pt_slot(table, at(*entries, k).key);
            while (at(*table, s)) s = (s + 1) & (uint32_t)(n - 1);
            (*table)[s] = k + 1;
        }
    }
    return e;
}

static void pt_text(amb_canvas *c, const amb_fonts *f, vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks,
                    vector<float> *acc, vector<uint8_t> *mask, const char *s, size_t n, double x, double baseline,
                    int face, double size, int italic, double ls, double wsp, uint32_t rgba) {
    uint32_t ca = rgba & 0xff, r = rgba >> 24, g = rgba >> 16 & 0xff, b = rgba >> 8 & 0xff;
    int by = (int)floor(baseline - c->oy + 0.5);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    size_t i = 0;
    uint32_t prev = 0;
    while (i < n) {
        uint32_t cp = amb_utf8_next(s, n, &i);
        if (prev) x += amb_kern(f, face, prev, cp, size);       /* the pair's kerning, as layout measured it */
        prev = cp;
        double fx = floor(x);
        int quarter = (int)((x - fx) * 4);
        uint32_t e = pt_glyph_of(f, entries, table, masks, acc, mask, face, size, cp, quarter, italic);
        pt_glyph gl = at(*entries, e);
        for (int yy = 0; yy < gl.h; yy++) {
            int Y = by + gl.oy + yy;
            if (Y < ky0 || Y >= ky1) continue;
            for (int xx = 0; xx < gl.w; xx++) {
                int X = (int)fx + gl.ox + xx;
                if (X < kx0 || X >= kx1) continue;
                uint32_t m = at(*masks, gl.off + (size_t)yy * gl.w + xx);
                if (!m) continue;
                uint32_t a = m * ca / 255;
                size_t p = ((size_t)Y * c->w + (size_t)X) * 3;
                c->px[p] = (uint8_t)((r * a + c->px[p] * (255 - a)) / 255);
                c->px[p + 1] = (uint8_t)((g * a + c->px[p + 1] * (255 - a)) / 255);
                c->px[p + 2] = (uint8_t)((b * a + c->px[p + 2] * (255 - a)) / 255);
            }
        }
        x += amb_advance(f, face, cp, size) + ls + (cp == ' ' ? wsp : 0);
    }
}

/* ---- the boxes ---- */

/* an image's RGBA pixels, w x h, drawn into the rectangle (x0, y0, dw, dh), sampled
   bilinearly and blended by their alpha */
static void pt_image(amb_canvas *c, const vector<uint8_t> *rgba, uint32_t w, uint32_t h, double x0, double y0, double dw, double dh) {
    if (dw <= 0 || dh <= 0 || w == 0 || h == 0) return;
    y0 -= c->oy;
    int ix0 = (int)floor(x0 + 0.5), iy0 = (int)floor(y0 + 0.5), ix1 = (int)floor(x0 + dw + 0.5), iy1 = (int)floor(y0 + dh + 0.5);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int y = iy0 < ky0 ? ky0 : iy0; y < iy1 && y < ky1; y++)
        for (int x = ix0 < kx0 ? kx0 : ix0; x < ix1 && x < kx1; x++) {
            double sx = (x + 0.5 - x0) * w / dw - 0.5, sy = (y + 0.5 - y0) * h / dh - 0.5;
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            uint32_t xa = (uint32_t)sx, ya = (uint32_t)sy, xb = xa + 1 < w ? xa + 1 : xa, yb = ya + 1 < h ? ya + 1 : ya;
            if (xa >= w) xa = xb = w - 1;
            if (ya >= h) ya = yb = h - 1;
            double fx = sx - xa, fy = sy - ya, px[4];
            for (int k = 0; k < 4; k++) {
                double top = at(*rgba, ((size_t)ya * w + xa) * 4 + k) * (1 - fx) + at(*rgba, ((size_t)ya * w + xb) * 4 + k) * fx;
                double bot = at(*rgba, ((size_t)yb * w + xa) * 4 + k) * (1 - fx) + at(*rgba, ((size_t)yb * w + xb) * 4 + k) * fx;
                px[k] = top * (1 - fy) + bot * fy;
            }
            double a = px[3] / 255;
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            for (int k = 0; k < 3; k++) c->px[i + k] = (uint8_t)(px[k] * a + c->px[i + k] * (1 - a) + 0.5);
        }
}

/* a pixel blended with a colour at coverage a (0..1) */
static void pt_blend(amb_canvas *c, int x, int y, uint32_t rgba, double cover) {
    double a = (rgba & 0xff) / 255.0 * cover;
    if (a <= 0) return;
    if (a > 1) a = 1;
    size_t i = ((size_t)y * c->w + (size_t)x) * 3;
    c->px[i] = (uint8_t)((rgba >> 24) * a + c->px[i] * (1 - a) + 0.5);
    c->px[i + 1] = (uint8_t)((rgba >> 16 & 0xff) * a + c->px[i + 1] * (1 - a) + 0.5);
    c->px[i + 2] = (uint8_t)((rgba >> 8 & 0xff) * a + c->px[i + 2] * (1 - a) + 0.5);
}

/* how much of the pixel at (px, py) (its centre, page coordinates) the rounded rectangle
   (x0, y0)-(x1, y1) with its corners' radii r (top left, top right, bottom right, bottom
   left) covers: 0 to 1, its edges and arcs anti-aliased */
static double pt_rr_cover(double px, double py, double x0, double y0, double x1, double y1, const double *r) {
    double e = px - x0 + 0.5, cov = e < 1 ? (e > 0 ? e : 0) : 1;
    double e2 = x1 - px + 0.5;
    if (e2 < cov) cov = e2 < 0 ? 0 : e2;
    double e3 = py - y0 + 0.5;
    if (e3 < cov) cov = e3 < 0 ? 0 : e3;
    double e4 = y1 - py + 0.5;
    if (e4 < cov) cov = e4 < 0 ? 0 : e4;
    if (cov <= 0) return 0;
    double cx = 0, cy = 0, rr = 0;
    if (r[0] > 0 && px < x0 + r[0] && py < y0 + r[0]) { cx = x0 + r[0]; cy = y0 + r[0]; rr = r[0]; }
    else if (r[1] > 0 && px > x1 - r[1] && py < y0 + r[1]) { cx = x1 - r[1]; cy = y0 + r[1]; rr = r[1]; }
    else if (r[2] > 0 && px > x1 - r[2] && py > y1 - r[2]) { cx = x1 - r[2]; cy = y1 - r[2]; rr = r[2]; }
    else if (r[3] > 0 && px < x0 + r[3] && py > y1 - r[3]) { cx = x0 + r[3]; cy = y1 - r[3]; rr = r[3]; }
    if (rr > 0) {
        double dd = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        double k = rr - dd + 0.5;
        if (k < cov) cov = k < 0 ? 0 : k;
    }
    return cov > 1 ? 1 : cov;
}

/* radii that fit their box: scaled down together when two of a side's sum more than it (CSS Backgrounds 5.5) */
static void pt_fit_radii(double w, double h, double *r) {
    double f = 1;
    if (r[0] + r[1] > w && r[0] + r[1] > 0) f = fmin(f, w / (r[0] + r[1]));
    if (r[3] + r[2] > w && r[3] + r[2] > 0) f = fmin(f, w / (r[3] + r[2]));
    if (r[0] + r[3] > h && r[0] + r[3] > 0) f = fmin(f, h / (r[0] + r[3]));
    if (r[1] + r[2] > h && r[1] + r[2] > 0) f = fmin(f, h / (r[1] + r[2]));
    for (int k = 0; k < 4; k++) r[k] = r[k] * f > 0 ? r[k] * f : 0;
}

/* a rounded rectangle filled (the clip's part of it) */
static void pt_rrect(amb_canvas *c, double x0, double y0, double x1, double y1, const double *r, uint32_t rgba) {
    if ((rgba & 0xff) == 0 || x1 <= x0 || y1 <= y0) return;
    if (r[0] <= 0 && r[1] <= 0 && r[2] <= 0 && r[3] <= 0) { pt_fill(c, x0, y0, x1, y1, rgba); return; }
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int ya = (int)floor(y0 - c->oy), yb = (int)ceil(y1 - c->oy), xa = (int)floor(x0), xb = (int)ceil(x1);
    for (int y = ya < ky0 ? ky0 : ya; y < yb && y < ky1; y++)
        for (int x = xa < kx0 ? kx0 : xa; x < xb && x < kx1; x++) {
            double cov = pt_rr_cover(x + 0.5, y + 0.5 + c->oy, x0, y0, x1, y1, r);
            if (cov > 0) pt_blend(c, x, y, rgba, cov);
        }
}

/* Chromium's darker and lighter colours (Color::Dark, Color::Light): a colour's value moved
   a third down or up, its hue kept */
static uint32_t pt_dark(uint32_t rgba) {
    if ((rgba & 0xffffff00) == 0xffffff00) return 0xababab00 | (rgba & 0xff);
    double r = (rgba >> 24) / 255.0, g = (rgba >> 16 & 0xff) / 255.0, b = (rgba >> 8 & 0xff) / 255.0;
    double v = fmax(r, fmax(g, b)), m = v > 0 ? fmax(0, (v - 0.33) / v) : 0;
    uint32_t R = (uint32_t)(r * m * 255.999), G = (uint32_t)(g * m * 255.999), B = (uint32_t)(b * m * 255.999);
    return R << 24 | G << 16 | B << 8 | (rgba & 0xff);
}
static uint32_t pt_light(uint32_t rgba) {
    double r = (rgba >> 24) / 255.0, g = (rgba >> 16 & 0xff) / 255.0, b = (rgba >> 8 & 0xff) / 255.0;
    double v = fmax(r, fmax(g, b));
    if (v <= 0) return 0x54545400 | (rgba & 0xff);
    double m = fmin(1.0, v + 0.33) / v;
    uint32_t R = (uint32_t)fmin(255, r * m * 255.999), G = (uint32_t)fmin(255, g * m * 255.999), B = (uint32_t)fmin(255, b * m * 255.999);
    return R << 24 | G << 16 | B << 8 | (rgba & 0xff);
}
static double pt_diff2(uint32_t a, uint32_t b) {
    double d0 = (double)(a >> 24) - (b >> 24), d1 = (double)(a >> 16 & 0xff) - (b >> 16 & 0xff), d2 = (double)(a >> 8 & 0xff) - (b >> 8 & 0xff);
    return d0 * d0 + d1 * d1 + d2 * d2;
}
/* inset's colour for a side (outset's the other way): the top and left darkened, the
   bottom and right lightened -- a colour near black not darkened, one near white not
   lightened (Blink's CalculateBorderStyleColor) */
static uint32_t pt_inset(uint32_t col, int side, int inset) {
    int darken = ((side == 0 || side == 3) == (inset != 0));
    if (darken) return pt_diff2(col, 0x000000ff) > pt_diff2(0x202020ff, 0x000000ff) ? pt_dark(col) : col;
    return pt_diff2(col, 0xffffffff) > pt_diff2(0xebebebff, 0xffffffff) ? pt_light(col) : col;
}

/* the colour of a part of a border of style bs: its side (0 top, 1 right, 2 bottom, 3
   left), and whether it is the outer half (groove: the outer half inset's, the inner
   outset's; ridge the other way) */
static uint32_t pt_side_colour(uint32_t col, int bs, int side, int outer) {
    if (bs == LAY_BS_INSET) return pt_inset(col, side, 1);
    if (bs == LAY_BS_OUTSET) return pt_inset(col, side, 0);
    if (bs == LAY_BS_GROOVE) return pt_inset(col, side, outer);
    if (bs == LAY_BS_RIDGE) return pt_inset(col, side, !outer);
    return col;
}

/* the gap between dashes (or dots) of length dash along len0, so that a whole number of
   them begin and end it, the nearest to gap (Blink's SelectBestDashGap); their count */
static double pt_dash_gap(double len0, double dash, double gap, long *n) {
    double mn = floor((len0 + gap) / (dash + gap)), mx = mn + 1;
    double g0 = mn > 1 ? (len0 - mn * dash) / (mn - 1) : 0, g1 = (len0 - mx * dash) / (mx - 1);
    if (g1 <= 0 || fabs(g0 - gap) < fabs(g1 - gap)) { *n = (long)mn; return g0; }
    *n = (long)mx;
    return g1;
}

/* one side of a border, w thick, along the rectangle's edge from (x0, y0) to (x1, y1) (its
   inside), drawn in its style: solid, double (two lines of a third), dotted (round dots),
   dashed (dashes three times as long as it is thick), the shaded halves of groove and ridge */
static void pt_side(amb_canvas *c, double x0, double y0, double x1, double y1, int side, int bs, uint32_t col) {
    double w = (side == 0 || side == 2) ? y1 - y0 : x1 - x0, len0 = (side == 0 || side == 2) ? x1 - x0 : y1 - y0;
    if (w <= 0 || len0 <= 0 || bs <= LAY_BS_HIDDEN) return;
    int horiz = side == 0 || side == 2;
    if (bs == LAY_BS_DOUBLE && w >= 3) {
        double t3 = floor(w / 3 + 0.5), gap = w - 2 * t3;
        if (horiz) { pt_fill(c, x0, y0, x1, y0 + t3, col); pt_fill(c, x0, y0 + t3 + gap, x1, y1, col); }
        else { pt_fill(c, x0, y0, x0 + t3, y1, col); pt_fill(c, x0 + t3 + gap, y0, x1, y1, col); }
        return;
    }
    if (bs == LAY_BS_GROOVE || bs == LAY_BS_RIDGE) {
        double hw = w / 2;
        int out_first = side == 0 || side == 3;
        uint32_t c0 = pt_side_colour(col, bs, side, out_first), c1 = pt_side_colour(col, bs, side, !out_first);
        if (horiz) { pt_fill(c, x0, y0, x1, y0 + hw, c0); pt_fill(c, x0, y0 + hw, x1, y1, c1); }
        else { pt_fill(c, x0, y0, x0 + hw, y1, c0); pt_fill(c, x0 + hw, y0, x1, y1, c1); }
        return;
    }
    if (bs == LAY_BS_DOTTED || bs == LAY_BS_DASHED) {
        double dash = bs == LAY_BS_DOTTED ? w : 3 * w;
        long n = 1;
        double gap = pt_dash_gap(len0, dash, dash, &n);
        if (n < 1) { n = 1; gap = 0; }
        for (long k = 0; k < n; k++) {
            double a = (horiz ? x0 : y0) + k * (dash + gap), z = a + dash;
            if (bs == LAY_BS_DOTTED && w >= 3) {
                double rr[4] = { w / 2, w / 2, w / 2, w / 2 };
                if (horiz) pt_rrect(c, a, y0, z, y1, rr, col); else pt_rrect(c, x0, a, x1, z, rr, col);
            } else if (horiz) pt_fill(c, a, y0, z, y1, col);
            else pt_fill(c, x0, a, x1, z, col);
        }
        return;
    }
    pt_fill(c, x0, y0, x1, y1, pt_side_colour(col, bs, side, 1));
}

/* ---- backgrounds: gradients and images, a layer at a time ---- */

/* the next comma-separated part of s[*i, n) at its top level (not in parentheses) */
static int pt_layer(const char *s, size_t n, size_t *i, size_t *a, size_t *len0) {
    size_t p = *i;
    while (p < n && (s[p] == ' ' || s[p] == ',')) p++;
    if (p >= n) return 0;
    size_t b = p;
    int depth = 0;
    while (p < n && (depth > 0 || s[p] != ',')) {
        if (s[p] == '(') depth++;
        else if (s[p] == ')') depth--;
        p++;
    }
    size_t e = p;
    while (e > b && s[e - 1] == ' ') e--;
    *a = b;
    *len0 = e - b;
    *i = p;
    return 1;
}

/* the k-th layer of a list (the last one again past its end) */
static int pt_nth(const char *s, size_t n, size_t k, size_t *a, size_t *len0) {
    size_t i = 0, q = 0, la = 0, ll = 0;
    int any = 0;
    while (pt_layer(s, n, &i, a, len0)) {
        la = *a;
        ll = *len0;
        any = 1;
        if (q++ == k) return 1;
    }
    *a = la;
    *len0 = ll;
    return any;
}

static int pt_is(const char *s, size_t n, const char *w) { return n == strlen(w) && memcmp(s, w, n) == 0; }

/* a gradient's colour stops: colours (premultiplied in v) and their places along it (0..1) */
typedef struct pt_stop { double pos, r, g, b, a; } pt_stop;

static void pt_stops(const char *s, size_t n, size_t from, double len0, vector<pt_stop> *out) {
    size_t i = from, a, ln;
    while (pt_layer(s, n, &i, &a, &ln)) {
        /* a colour, then its place (a percentage or a length), or a second place */
        size_t j = 0, pa, pl;
        uint32_t rgba = 0;
        double pos = -1, pos2 = -1;
        int have = 0;
        while (css_next_part(s + a, ln, &j, &pa, &pl)) {
            uint32_t cc;
            const char *t = s + a + pa;
            if (!have && css_color_parse(t, pl, &cc)) { rgba = cc; have = 1; continue; }
            double v;
            if (css_length_eval(t, pl, len0, &v)) { if (pos < 0) pos = len0 > 0 ? v / len0 : 0; else pos2 = len0 > 0 ? v / len0 : 0; }
        }
        if (!have) continue;
        pt_stop st0;
        double al = (rgba & 0xff) / 255.0;
        st0.a = al;
        st0.r = (rgba >> 24) / 255.0 * al;
        st0.g = (rgba >> 16 & 0xff) / 255.0 * al;
        st0.b = (rgba >> 8 & 0xff) / 255.0 * al;
        st0.pos = pos;
        push_back(out, st0);
        if (pos2 >= 0) { st0.pos = pos2; push_back(out, st0); }
    }
    /* places not given: the first 0, the last 1, the rest evenly between; none before the one before */
    size_t m = len(*out);
    if (!m) return;
    if (at(*out, 0).pos < 0) (*out)[0].pos = 0;
    if (at(*out, m - 1).pos < 0) (*out)[m - 1].pos = 1;
    double last = at(*out, 0).pos;
    for (size_t k = 1; k < m; k++) {
        if (at(*out, k).pos >= 0) { if (at(*out, k).pos < last) (*out)[k].pos = last; last = at(*out, k).pos; continue; }
        size_t e = k;
        while (e < m && at(*out, e).pos < 0) e++;
        double p0 = at(*out, k - 1).pos, p1 = at(*out, e).pos;
        for (size_t q = k; q < e; q++) (*out)[q].pos = p0 + (p1 - p0) * (double)(q - k + 1) / (double)(e - k + 1);
        k = e - 1;
    }
}

/* the colour at place t along the stops (premultiplied in; RGBA out) */
static uint32_t pt_stop_at(const vector<pt_stop> *sv, double t) {
    size_t m = len(*sv);
    pt_stop a = at(*sv, 0), b = a;
    if (t <= a.pos) b = a;
    else if (t >= at(*sv, m - 1).pos) { a = at(*sv, m - 1); b = a; }
    else for (size_t k = 1; k < m; k++) if (t <= at(*sv, k).pos) { a = at(*sv, k - 1); b = at(*sv, k); break; }
    double f = b.pos > a.pos ? (t - a.pos) / (b.pos - a.pos) : 0;
    double al = a.a + (b.a - a.a) * f;
    if (al <= 0) return 0;
    double r = (a.r + (b.r - a.r) * f) / al, g = (a.g + (b.g - a.g) * f) / al, bl = (a.b + (b.b - a.b) * f) / al;
    return (uint32_t)(fmin(r, 1) * 255 + 0.5) << 24 | (uint32_t)(fmin(g, 1) * 255 + 0.5) << 16 | (uint32_t)(fmin(bl, 1) * 255 + 0.5) << 8 |
           (uint32_t)(al * 255 + 0.5);
}

/* a linear or radial gradient (s[0, n), its function call) over the tile (x, y, w, h),
   drawn where the box's rounded border box (bx..., r) and the clip let it */
static void pt_gradient(amb_canvas *c, const char *s, size_t n, double x, double y, double w, double h, double bx0, double by0, double bx1,
                        double by1, const double *rad) {
    int radial = n > 15 && memcmp(s, "radial-gradient", 15) == 0;
    int linear = n > 15 && memcmp(s, "linear-gradient", 15) == 0;
    int rep = n > 25 && memcmp(s, "repeating-", 10) == 0;
    if (rep) { radial = memcmp(s + 10, "radial", 6) == 0; linear = !radial; }
    if (!radial && !linear) return;
    size_t open = 0;
    while (open < n && s[open] != '(') open++;
    if (open >= n || s[n - 1] != ')') return;
    const char *args = s + open + 1;
    size_t an = n - open - 2;
    /* the first argument: a direction (an angle, to a side) or a shape and place, unless it is a colour stop */
    size_t i = 0, a0, l0;
    double angle = 180, cx = 0.5, cy = 0.5;
    int circle = 0, first_is_stop = 1;
    if (pt_layer(args, an, &i, &a0, &l0)) {
        const char *f0 = args + a0;
        uint32_t tmp;
        size_t j = 0, pa, pl;
        int colour = css_next_part(f0, l0, &j, &pa, &pl) && css_color_parse(f0 + pa, pl, &tmp);
        if (!colour) {
            first_is_stop = 0;
            if (linear) {
                double v = strtod(f0, 0);
                if (l0 > 3 && f0[0] == 't' && f0[1] == 'o' && f0[2] == ' ') {
                    int top = 0, bottom = 0, left = 0, right = 0;
                    j = 3;
                    while (css_next_part(f0, l0, &j, &pa, &pl)) {
                        if (pt_is(f0 + pa, pl, "top")) top = 1;
                        else if (pt_is(f0 + pa, pl, "bottom")) bottom = 1;
                        else if (pt_is(f0 + pa, pl, "left")) left = 1;
                        else if (pt_is(f0 + pa, pl, "right")) right = 1;
                    }
                    if ((top || bottom) && (left || right)) {
                        /* to a corner: the angle whose line runs through the other two corners */
                        double ang = atan2(w, h) * 180 / M_PI;
                        angle = top ? (right ? ang : 360 - ang) : (right ? 180 - ang : 180 + ang);
                    } else angle = top ? 0 : right ? 90 : bottom ? 180 : left ? 270 : 180;
                } else if (l0 > 3 && memcmp(f0 + l0 - 3, "deg", 3) == 0) angle = v;
                else if (l0 > 4 && memcmp(f0 + l0 - 4, "turn", 4) == 0) angle = v * 360;
                else if (l0 > 3 && memcmp(f0 + l0 - 3, "rad", 3) == 0 && memcmp(f0 + l0 - 4, "grad", 4) != 0) angle = v * 180 / M_PI;
                else if (l0 > 4 && memcmp(f0 + l0 - 4, "grad", 4) == 0) angle = v * 0.9;
            } else {
                j = 0;
                int at_pos = 0, axis = 0;
                while (css_next_part(f0, l0, &j, &pa, &pl)) {
                    const char *t = f0 + pa;
                    if (pt_is(t, pl, "circle")) circle = 1;
                    else if (pt_is(t, pl, "at")) at_pos = 1;
                    else if (at_pos) {
                        double v;
                        double pv = pt_is(t, pl, "left") || pt_is(t, pl, "top") ? 0 : pt_is(t, pl, "right") || pt_is(t, pl, "bottom") ? 1 :
                                    pt_is(t, pl, "center") ? 0.5 : -1;
                        if (pv < 0 && css_length_eval(t, pl, axis == 0 ? w : h, &v)) pv = v / (axis == 0 ? w : h);
                        if (pv >= 0) {
                            if (axis == 0 && (pt_is(t, pl, "top") || pt_is(t, pl, "bottom"))) cy = pv;
                            else if (axis == 0) cx = pv;
                            else cy = pv;
                            axis++;
                        }
                    }
                }
            }
        }
    }
    double rad_a = angle * M_PI / 180, dxv = sin(rad_a), dyv = -cos(rad_a);
    double glen = fabs(w * dxv) + fabs(h * dyv);
    /* a radial one's size: farthest-corner */
    double px0 = cx * w, py0 = cy * h;
    double rx = fmax(px0, w - px0), ry = fmax(py0, h - py0);
    if (circle) { rx = sqrt(rx * rx + ry * ry); ry = rx; }
    else { rx *= M_SQRT2; ry *= M_SQRT2; }
    vector<pt_stop> sv;
    pt_stops(args, an, first_is_stop ? 0 : i, linear ? glen : rx, &sv);
    if (!len(sv)) return;
    double period = len(sv) > 1 ? at(sv, len(sv) - 1).pos - at(sv, 0).pos : 0;
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int ya = (int)floor(fmax(y, by0) - c->oy), yb = (int)ceil(fmin(y + h, by1) - c->oy);
    int xa = (int)floor(fmax(x, bx0)), xb = (int)ceil(fmin(x + w, bx1));
    for (int py = ya < ky0 ? ky0 : ya; py < yb && py < ky1; py++)
        for (int pxx = xa < kx0 ? kx0 : xa; pxx < xb && pxx < kx1; pxx++) {
            double X = pxx + 0.5 - x, Y = py + 0.5 + c->oy - y;
            if (X < 0 || Y < 0 || X > w || Y > h) continue;
            double tt;
            if (linear) tt = glen > 0 ? ((X - w / 2) * dxv + (Y - h / 2) * dyv) / glen + 0.5 : 0;
            else { double ux = (X - px0) / (rx > 0 ? rx : 1), uy = (Y - py0) / (ry > 0 ? ry : 1); tt = sqrt(ux * ux + uy * uy); }
            if (rep && period > 0) tt = at(sv, 0).pos + fmod(tt - at(sv, 0).pos + 1000 * period, period);
            double cov = pt_rr_cover(pxx + 0.5, py + 0.5 + c->oy, bx0, by0, bx1, by1, rad);
            if (cov > 0) pt_blend(c, pxx, py, pt_stop_at(&sv, tt), cov);
        }
}

/* one value of a pair (background-size's, -position's): a length or percentage of basis */
static int pt_len(const char *s, size_t n, double basis, double *v) { return css_length_eval(s, n, basis, v); }

/* a background layer's tile: its size (bs: background-size's value for it; iw, ih the
   image's own size, 0 for a gradient) in the positioning area aw x ah */
static void pt_tile_size(const char *bs, size_t bn, double iw, double ih, double aw, double ah, double *tw, double *th) {
    *tw = iw > 0 ? iw : aw;
    *th = ih > 0 ? ih : ah;
    if (bn == 0) return;
    if (pt_is(bs, bn, "cover") || pt_is(bs, bn, "contain")) {
        if (iw <= 0 || ih <= 0) { *tw = aw; *th = ah; return; }
        double sx = aw / iw, sy = ah / ih, sc = pt_is(bs, bn, "cover") ? fmax(sx, sy) : fmin(sx, sy);
        *tw = iw * sc;
        *th = ih * sc;
        return;
    }
    size_t j = 0, pa, pl;
    double v[2] = { -1, -1 };
    int k = 0;
    while (k < 2 && css_next_part(bs, bn, &j, &pa, &pl)) {
        double x;
        if (!pt_is(bs + pa, pl, "auto") && pt_len(bs + pa, pl, k == 0 ? aw : ah, &x)) v[k] = x;
        k++;
    }
    if (v[0] >= 0 && v[1] >= 0) { *tw = v[0]; *th = v[1]; }
    else if (v[0] >= 0) { *tw = v[0]; *th = iw > 0 && ih > 0 ? v[0] * ih / iw : ah; }
    else if (v[1] >= 0) { *th = v[1]; *tw = iw > 0 && ih > 0 ? v[1] * iw / ih : aw; }
}

/* a background layer's place: background-position's value bp for it, in the positioning
   area aw x ah for a tile tw x th: the tile's offset */
static void pt_tile_pos(const char *bp, size_t bn, double aw, double ah, double tw, double th, double *ox, double *oy) {
    *ox = 0;
    *oy = 0;
    size_t j = 0, pa, pl;
    int k = 0;
    double v[2] = { 0, 0 };
    int axis[2] = { 0, 1 };
    while (k < 2 && css_next_part(bp, bn, &j, &pa, &pl)) {
        const char *t = bp + pa;
        double f = -1;
        if (pt_is(t, pl, "left")) { f = 0; axis[k] = 0; }
        else if (pt_is(t, pl, "right")) { f = 1; axis[k] = 0; }
        else if (pt_is(t, pl, "top")) { f = 0; axis[k] = 1; }
        else if (pt_is(t, pl, "bottom")) { f = 1; axis[k] = 1; }
        else if (pt_is(t, pl, "center")) { f = 0.5; axis[k] = k; }
        if (f >= 0) v[k] = f * ((axis[k] == 0 ? aw - tw : ah - th));
        else {
            double x;
            int ax = k;
            if (pl > 0 && t[pl - 1] == '%') {
                x = strtod(t, 0) / 100 * (ax == 0 ? aw - tw : ah - th);
            } else if (!pt_len(t, pl, ax == 0 ? aw : ah, &x)) x = 0;
            v[k] = x;
            axis[k] = ax;
        }
        k++;
    }
    if (k == 1) { v[1] = 0.5 * (ah - th); axis[1] = 1; if (axis[0] == 1) { v[1] = v[0]; v[0] = 0.5 * (aw - tw); axis[0] = 0; } }
    for (int q = 0; q < 2; q++) { if (axis[q] == 0) *ox = v[q]; else *oy = v[q]; }
}

/* ---- shadows: a shape's alpha, blurred ---- */

/* three box blurs of a radius close to sigma's, across and down (a Gaussian's likeness) */
static void pt_blur(vector<float> *m, int w, int h, double sigma) {
    if (sigma < 0.5) return;
    int rr = (int)floor(sigma * 0.94 + 0.5);
    if (rr < 1) rr = 1;
    vector<float> tmp;
    resize(&tmp, (size_t)(w > h ? w : h));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            double sum = 0;
            for (int x = -rr; x <= rr; x++) if (x >= 0 && x < w) sum += at(*m, (size_t)y * w + x);
            for (int x = 0; x < w; x++) {
                tmp[(size_t)x] = (float)(sum / (2 * rr + 1));
                int out = x - rr, in = x + rr + 1;
                if (out >= 0) sum -= at(*m, (size_t)y * w + out);
                if (in < w) sum += at(*m, (size_t)y * w + in);
            }
            for (int x = 0; x < w; x++) (*m)[(size_t)y * w + x] = at(tmp, (size_t)x);
        }
        for (int x = 0; x < w; x++) {
            double sum = 0;
            for (int y = -rr; y <= rr; y++) if (y >= 0 && y < h) sum += at(*m, (size_t)y * w + x);
            for (int y = 0; y < h; y++) {
                tmp[(size_t)y] = (float)(sum / (2 * rr + 1));
                int out = y - rr, in = y + rr + 1;
                if (out >= 0) sum -= at(*m, (size_t)out * w + x);
                if (in < h) sum += at(*m, (size_t)in * w + x);
            }
            for (int y = 0; y < h; y++) (*m)[(size_t)y * w + x] = at(tmp, (size_t)y);
        }
    }
}

/* a box's outer shadows (box-shadow's value s): each, last first, its rounded border box
   offset and spread, blurred, outside the box only */
static void pt_box_shadows(amb_canvas *c, const char *s, size_t n, double x0, double y0, double x1, double y1, const double *rad) {
    vector<size_t> starts;
    size_t i = 0, a, ln;
    while (pt_layer(s, n, &i, &a, &ln)) { push_back(starts, a); push_back(starts, ln); }
    for (size_t q = len(starts); q >= 2; q -= 2) {
        const char *t = s + at(starts, q - 2);
        size_t tl = at(starts, q - 1), j = 0, pa, pl;
        double v[4] = { 0, 0, 0, 0 };
        int nv = 0, inset = 0;
        uint32_t col = 0x000000ff;
        while (css_next_part(t, tl, &j, &pa, &pl)) {
            uint32_t cc;
            double x;
            if (pt_is(t + pa, pl, "inset")) inset = 1;
            else if (nv < 4 && css_length_eval(t + pa, pl, 0, &x)) v[nv++] = x;
            else if (css_color_parse(t + pa, pl, &cc)) col = cc;
        }
        if (inset || nv < 2 || (col & 0xff) == 0) continue;
        double blur = v[2] > 0 ? v[2] : 0, spread = v[3];
        double sx0 = x0 + v[0] - spread, sy0 = y0 + v[1] - spread, sx1 = x1 + v[0] + spread, sy1 = y1 + v[1] + spread;
        double sr[4];
        for (int k = 0; k < 4; k++) sr[k] = rad[k] > 0 ? fmax(0, rad[k] + spread) : 0;
        int margin = (int)ceil(blur) + 2;
        int mx0 = (int)floor(sx0) - margin, my0 = (int)floor(sy0) - margin;
        int mw = (int)ceil(sx1) + margin - mx0, mh = (int)ceil(sy1) + margin - my0;
        if (mw <= 0 || mh <= 0 || (double)mw * mh > 4e7) continue;
        vector<float> m;
        resize(&m, (size_t)mw * mh);
        for (int y = 0; y < mh; y++)
            for (int x = 0; x < mw; x++) m[(size_t)y * mw + x] = (float)pt_rr_cover(mx0 + x + 0.5, my0 + y + 0.5, sx0, sy0, sx1, sy1, sr);
        pt_blur(&m, mw, mh, blur / 2);
        int kx0, ky0, kx1, ky1;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        for (int y = 0; y < mh; y++) {
            int Y = (int)floor(my0 + y - c->oy + 0.5);
            if (Y < ky0 || Y >= ky1) continue;
            for (int x = 0; x < mw; x++) {
                int X = mx0 + x;
                if (X < kx0 || X >= kx1) continue;
                double cov = at(m, (size_t)y * mw + x);
                if (cov <= 0) continue;
                double inside = pt_rr_cover(X + 0.5, my0 + y + 0.5, x0, y0, x1, y1, rad);
                pt_blend(c, X, Y, col, cov * (1 - inside));
            }
        }
    }
}

/* the image the loader fetched for an element, decoded into rgba: 1 when it was */
static int pt_image_of(const net_loader *nt, uint32_t el, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    if (!nt) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++) {
        if (!nt->resource[r].live || nt->resource[r].elem.index != el || nt->resource[r].kind != NET_KIND_IMAGE) continue;
        if (nt->resource[r].outcome != NET_OUTCOME_OK) return 0;
        amb_span b = nt->resource[r].body;
        vector<uint8_t> bytes;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
        return amb_image_decode(data(bytes), len(bytes), rgba, w, h);
    }
    return 0;
}

/* an image a box's background names (resource r), decoded once a paint: its pixels' place
   in the cache's pixels, its size; 0 when it cannot be */
static int pt_cached_image(const net_loader *nt, uint32_t r, vector<uint32_t> *icache, vector<uint8_t> *ipx, size_t *off, uint32_t *w, uint32_t *h) {
    for (size_t k = 0; k + 4 < len(*icache) + 1 && k + 3 < len(*icache); k += 4)
        if (at(*icache, k) == r) {
            *off = at(*icache, k + 1);
            *w = at(*icache, k + 2);
            *h = at(*icache, k + 3);
            return *w > 0;
        }
    *w = 0;
    *h = 0;
    *off = len(*ipx);
    if (nt && r < nt->resource_len && nt->resource[r].outcome == NET_OUTCOME_OK) {
        vector<uint8_t> bytes, rgba;
        amb_span bd = nt->resource[r].body;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, bd), bd.len);
        if (amb_png_decode(data(bytes), len(bytes), &rgba, w, h)) append_range(ipx, data(rgba), len(rgba));
        else { *w = 0; *h = 0; }
    }
    push_back(icache, r);
    push_back(icache, (uint32_t)*off);
    push_back(icache, *w);
    push_back(icache, *h);
    return *w > 0;
}

/* a box's background layers (background-image's value, last first): each tile placed by
   background-size, -position and -repeat in its padding box, drawn in its rounded border box */
static void pt_layers(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, vector<uint32_t> *icache, vector<uint8_t> *ipx,
                      const double *rad) {
    vector<char> img, pos, siz, rep;
    amb_span sp = t->box[b].bg_image;
    append_range(img, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_pos;
    append_range(pos, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_size;
    append_range(siz, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_repeat;
    append_range(rep, text_ptr(t, sp), sp.len);
    double bx0 = t->box[b].x - t->box[b].pl - t->box[b].bl, by0 = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bx1 = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br, by1 = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb;
    double ax = t->box[b].x - t->box[b].pl, ay = t->box[b].y - t->box[b].pt;
    double aw = t->box[b].w + t->box[b].pl + t->box[b].pr, ah = t->box[b].h + t->box[b].pt + t->box[b].pb;
    vector<size_t> layers;
    size_t i = 0, a, ln;
    while (pt_layer(data(img), len(img), &i, &a, &ln)) { push_back(layers, a); push_back(layers, ln); }
    size_t nl = len(layers) / 2;
    for (size_t q = nl; q >= 1; q--) {
        const char *li = data(img) + at(layers, 2 * q - 2);
        size_t ll = at(layers, 2 * q - 1);
        size_t pa = 0, pl = 0, sa = 0, sl = 0, ra = 0, rl = 0;
        if (len(pos)) pt_nth(data(pos), len(pos), q - 1, &pa, &pl);
        if (len(siz)) pt_nth(data(siz), len(siz), q - 1, &sa, &sl);
        if (len(rep)) pt_nth(data(rep), len(rep), q - 1, &ra, &rl);
        const char *rp = data(rep) + ra;
        int rx = 1, ry = 1;
        if (rl && pt_is(rp, rl, "no-repeat")) { rx = 0; ry = 0; }
        else if (rl && pt_is(rp, rl, "repeat-x")) ry = 0;
        else if (rl && pt_is(rp, rl, "repeat-y")) rx = 0;
        else if (rl && memcmp(rp, "no-repeat", rl < 9 ? rl : 9) == 0) { rx = 0; ry = 0; }
        int is_url = ll > 4 && memcmp(li, "url(", 4) == 0;
        size_t off = 0;
        uint32_t iw = 0, ih = 0;
        if (is_url && !(t->box[b].bg_res.index && pt_cached_image(nt, t->box[b].bg_res.index, icache, ipx, &off, &iw, &ih))) continue;
        double tw, th, ox, oy;
        pt_tile_size(data(siz) + sa, sl, iw, ih, aw, ah, &tw, &th);
        if (tw < 0.5 || th < 0.5) continue;
        pt_tile_pos(data(pos) + pa, pl, aw, ah, tw, th, &ox, &oy);
        double x0 = ax + ox, y0 = ay + oy;
        /* the tiles over the painting area: from the first that reaches it */
        double sx = x0, sy = y0;
        if (rx) while (sx > bx0) sx -= tw;
        if (ry) while (sy > by0) sy -= th;
        double save[4] = { c->cx0, c->cy0, c->cx1, c->cy1 };
        c->cx0 = fmax(c->cx0, bx0);
        c->cy0 = fmax(c->cy0, by0);
        c->cx1 = fmin(c->cx1, bx1);
        c->cy1 = fmin(c->cy1, by1);
        for (double ty = sy; ty < by1 && (ry || ty == sy); ty += th) {
            if (ty + th < c->oy || ty > c->oy + c->h) { if (!ry) break; continue; }
            for (double tx = sx; tx < bx1 && (rx || tx == sx); tx += tw) {
                if (is_url) {
                    vector<uint8_t> tile;
                    append_range(tile, data(*ipx) + off, (size_t)iw * ih * 4);
                    pt_image(c, &tile, iw, ih, tx, ty, tw, th);
                } else pt_gradient(c, li, ll, tx, ty, tw, th, bx0, by0, bx1, by1, rad);
                if (!rx) break;
            }
            if (!ry) break;
        }
        c->cx0 = save[0];
        c->cy0 = save[1];
        c->cx1 = save[2];
        c->cy1 = save[3];
    }
}

static void pt_box(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, uint32_t skip_bg, vector<uint32_t> *icache,
                   vector<uint8_t> *ipx) {
    double bx = t->box[b].x - t->box[b].pl - t->box[b].bl, by = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double rad[4] = { t->box[b].rtl, t->box[b].rtr, t->box[b].rbr, t->box[b].rbl };
    pt_fit_radii(bw, bh, rad);
    int round = rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0;
    if (t->box[b].shadows.len) {
        vector<char> sh;
        amb_span sp = t->box[b].shadows;
        append_range(sh, text_ptr(t, sp), sp.len);
        pt_box_shadows(c, data(sh), len(sh), bx, by, bx + bw, by + bh, rad);
    }
    if (b != skip_bg) pt_rrect(c, bx, by, bx + bw, by + bh, rad, t->box[b].background);
    if (t->box[b].bg_image.len) pt_layers(t, nt, b, c, icache, ipx, rad);
    if (t->box[b].kind == LAYOUT_BOX_KIND_REPLACED) {
        vector<uint8_t> rgba;
        uint32_t iw, ih;
        if (pt_image_of(nt, t->box[b].node.index, &rgba, &iw, &ih)) pt_image(c, &rgba, iw, ih, t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h);
        else if ((t->box[b].background & 0xff) == 0 && t->box[b].bt + t->box[b].bl == 0)
            pt_fill(c, t->box[b].x, t->box[b].y, t->box[b].x + t->box[b].w, t->box[b].y + t->box[b].h, 0xe8e8e8ff);
    }
    double bt = t->box[b].bt, br = t->box[b].br, bb = t->box[b].bb, bl = t->box[b].bl;
    int st0 = t->box[b].bst, st1 = t->box[b].bsr, st2 = t->box[b].bsb, st3 = t->box[b].bsl;
    int plain = (st0 == LAY_BS_SOLID || st0 <= LAY_BS_HIDDEN) && (st1 == LAY_BS_SOLID || st1 <= LAY_BS_HIDDEN) &&
                (st2 == LAY_BS_SOLID || st2 <= LAY_BS_HIDDEN) && (st3 == LAY_BS_SOLID || st3 <= LAY_BS_HIDDEN);
    if (round && (bt > 0 || br > 0 || bb > 0 || bl > 0)) {
        /* a rounded border: the ring between the border box and the padding box, each
           pixel its side's colour (the sides meeting along the corners' diagonals) */
        double ir[4] = { fmax(0, rad[0] - fmax(bt, bl)), fmax(0, rad[1] - fmax(bt, br)), fmax(0, rad[2] - fmax(bb, br)), fmax(0, rad[3] - fmax(bb, bl)) };
        double ix0 = bx + bl, iy0 = by + bt, ix1 = bx + bw - br, iy1 = by + bh - bb;
        int kx0, ky0, kx1, ky1;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        int ya = (int)floor(by - c->oy), yb = (int)ceil(by + bh - c->oy), xa = (int)floor(bx), xb = (int)ceil(bx + bw);
        for (int y = ya < ky0 ? ky0 : ya; y < yb && y < ky1; y++)
            for (int x = xa < kx0 ? kx0 : xa; x < xb && x < kx1; x++) {
                double px = x + 0.5, py = y + 0.5 + c->oy;
                double cov = pt_rr_cover(px, py, bx, by, bx + bw, by + bh, rad) - pt_rr_cover(px, py, ix0, iy0, ix1, iy1, ir);
                if (cov <= 0) continue;
                double dt = (py - by) / (bt > 0 ? bt : 1e-9), db = (by + bh - py) / (bb > 0 ? bb : 1e-9);
                double dl = (px - bx) / (bl > 0 ? bl : 1e-9), dr = (bx + bw - px) / (br > 0 ? br : 1e-9);
                int side = 0;
                double m = dt;
                if (dr < m) { m = dr; side = 1; }
                if (db < m) { m = db; side = 2; }
                if (dl < m) { side = 3; }
                uint32_t colv = side == 0 ? t->box[b].bct : side == 1 ? t->box[b].bcr : side == 2 ? t->box[b].bcb : t->box[b].bcl;
                int sty = side == 0 ? st0 : side == 1 ? st1 : side == 2 ? st2 : st3;
                if (sty <= LAY_BS_HIDDEN) continue;
                pt_blend(c, x, y, pt_side_colour(colv, sty, side, 1), cov);
            }
    } else if (plain) {
        if (bt > 0) pt_fill(c, bx, by, bx + bw, by + bt, t->box[b].bct);
        if (bb > 0) pt_fill(c, bx, by + bh - bb, bx + bw, by + bh, t->box[b].bcb);
        if (bl > 0) pt_fill(c, bx, by, bx + bl, by + bh, t->box[b].bcl);
        if (br > 0) pt_fill(c, bx + bw - br, by, bx + bw, by + bh, t->box[b].bcr);
    } else {
        /* the styled sides: top and bottom across, left and right between them */
        if (bt > 0) pt_side(c, bx, by, bx + bw, by + bt, 0, st0, t->box[b].bct);
        if (bb > 0) pt_side(c, bx, by + bh - bb, bx + bw, by + bh, 2, st2, t->box[b].bcb);
        if (bl > 0) pt_side(c, bx, by + bt, bx + bl, by + bh - bb, 3, st3, t->box[b].bcl);
        if (br > 0) pt_side(c, bx + bw - br, by + bt, bx + bw, by + bh - bb, 1, st1, t->box[b].bcr);
    }
}

/* a box's outline: outside its border box by its offset, in its style */
static void pt_outline(const layout_tree *t, uint32_t b, amb_canvas *c) {
    double w = t->box[b].ow;
    if (w <= 0 || (t->box[b].ocolor & 0xff) == 0) return;
    double o = t->box[b].ooffset;
    double x0 = t->box[b].x - t->box[b].pl - t->box[b].bl - o - w, y0 = t->box[b].y - t->box[b].pt - t->box[b].bt - o - w;
    double x1 = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br + o + w, y1 = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb + o + w;
    int sty = t->box[b].ostyle;
    uint32_t col = t->box[b].ocolor;
    pt_side(c, x0, y0, x1, y0 + w, 0, sty, col);
    pt_side(c, x0, y1 - w, x1, y1, 2, sty, col);
    pt_side(c, x0, y0 + w, x0 + w, y1 - w, 3, sty, col);
    pt_side(c, x1 - w, y0 + w, x1, y1 - w, 1, sty, col);
}

/* the face a fragment is drawn in: its built-in one, or its web font's as paint read it
   (webmap: each web font's resource, then its face) */
static int pt_face_of(const layout_tree *t, uint32_t fr, const vector<uint32_t> *webmap) {
    uint32_t r = t->frag[fr].font.index;
    if (!r) return t->frag[fr].face < AMB_NFACES ? t->frag[fr].face : AMB_FACE_SERIF;
    for (size_t k = 0; k + 1 < len(*webmap); k += 2) if (at(*webmap, k) == r) return (int)at(*webmap, k + 1);
    return AMB_FACE_SERIF;
}

/* a text fragment's shadows (text-shadow's value), last first: its text offset in the
   shadow's colour, blurred through a canvas of its own when the shadow has a blur */
static void pt_text_shadows(const layout_tree *t, uint32_t fr, amb_canvas *c, const amb_fonts *f, const vector<uint32_t> *webmap,
                            vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask) {
    vector<char> sh, txt;
    amb_span sp = t->frag[fr].shadows;
    append_range(sh, text_ptr(t, sp), sp.len);
    sp = t->frag[fr].text;
    append_range(txt, text_ptr(t, sp), sp.len);
    vector<size_t> starts;
    size_t i = 0, a, ln;
    while (pt_layer(data(sh), len(sh), &i, &a, &ln)) { push_back(starts, a); push_back(starts, ln); }
    for (size_t q = len(starts); q >= 2; q -= 2) {
        const char *s0 = data(sh) + at(starts, q - 2);
        size_t sl = at(starts, q - 1), j = 0, pa, pl;
        double v[3] = { 0, 0, 0 };
        int nv = 0;
        uint32_t col = t->frag[fr].color;
        while (css_next_part(s0, sl, &j, &pa, &pl)) {
            uint32_t cc;
            double x;
            if (nv < 3 && css_length_eval(s0 + pa, pl, 0, &x)) v[nv++] = x;
            else if (css_color_parse(s0 + pa, pl, &cc)) col = cc;
        }
        if (nv < 2 || (col & 0xff) == 0) continue;
        double x = t->frag[fr].x + v[0], base = t->frag[fr].baseline + v[1], blur = v[2];
        if (blur <= 0) {
            pt_text(c, f, entries, table, masks, acc, mask, data(txt), len(txt), x, base, pt_face_of(t, fr, webmap), t->frag[fr].size,
                    t->frag[fr].italic, t->frag[fr].spacing, t->frag[fr].word_spacing, col);
            continue;
        }
        /* blurred: the text drawn white on black over its box and the blur's reach, that
           coverage blurred, the colour laid by it */
        int margin = (int)ceil(blur) + 2;
        int mx0 = (int)floor(x) - margin, my0 = (int)floor(t->frag[fr].y + v[1]) - margin;
        int mw = (int)ceil(t->frag[fr].w) + 2 * margin + 4, mh = (int)ceil(t->frag[fr].h) + 2 * margin + 4;
        if (mw <= 0 || mh <= 0 || (double)mw * mh > 4e6) continue;
        amb_canvas m;
        memset(&m, 0, sizeof m);
        defer(m) { AMB_CANVAS_DROP(m); }
        m.w = (uint32_t)mw;
        m.h = (uint32_t)mh;
        m.oy = my0;
        m.cx0 = 0;
        m.cy0 = my0;
        m.cx1 = mw;
        m.cy1 = my0 + mh;
        resize(&m.px, (size_t)mw * mh * 3);
        pt_text(&m, f, entries, table, masks, acc, mask, data(txt), len(txt), x - mx0, base, pt_face_of(t, fr, webmap), t->frag[fr].size,
                t->frag[fr].italic, t->frag[fr].spacing, t->frag[fr].word_spacing, 0xffffffff);
        vector<float> cov;
        resize(&cov, (size_t)mw * mh);
        for (size_t k = 0; k < (size_t)mw * mh; k++) cov[k] = at(m.px, k * 3) / 255.0f;
        pt_blur(&cov, mw, mh, blur / 2);
        int kx0, ky0, kx1, ky1;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        for (int yy = 0; yy < mh; yy++) {
            int Y = (int)floor(my0 + yy - c->oy + 0.5);
            if (Y < ky0 || Y >= ky1) continue;
            for (int xx = 0; xx < mw; xx++) {
                int X = mx0 + xx;
                if (X < kx0 || X >= kx1) continue;
                double cv = at(cov, (size_t)yy * mw + xx);
                if (cv > 0) pt_blend(c, X, Y, col, cv);
            }
        }
    }
}

static void pt_lines(const layout_tree *t, uint32_t b, amb_canvas *c, const amb_fonts *f, const vector<uint32_t> *webmap, vector<pt_glyph> *entries, vector<uint32_t> *table,
                     vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next)
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            layout_frag_kind k = t->frag[fr].kind;
            if (k == LAYOUT_FRAG_KIND_INLINE) {
                pt_fill(c, t->frag[fr].x, t->frag[fr].y, t->frag[fr].x + t->frag[fr].w, t->frag[fr].y + t->frag[fr].h, t->frag[fr].background);
                continue;
            }
            if (k != LAYOUT_FRAG_KIND_TEXT && k != LAYOUT_FRAG_KIND_MARKER && k != LAYOUT_FRAG_KIND_GENERATED) continue;
            /* a fragment outside the canvas's window: nothing of it to draw */
            if (t->frag[fr].y > c->oy + c->h || t->frag[fr].y + t->frag[fr].h < c->oy) continue;
            amb_span sp = t->frag[fr].text;
            double size = t->frag[fr].size, base = t->frag[fr].baseline;
            uint32_t color = t->frag[fr].color;
            if (t->frag[fr].shadows.len) pt_text_shadows(t, fr, c, f, webmap, entries, table, masks, acc, mask);
            pt_text(c, f, entries, table, masks, acc, mask, text_ptr(t, sp), sp.len, t->frag[fr].x, base,
                    pt_face_of(t, fr, webmap), size, t->frag[fr].italic, t->frag[fr].spacing, t->frag[fr].word_spacing, color);
            uint8_t deco = t->frag[fr].decoration;
            double th = floor(size / 16 + 0.5);
            if (th < 1) th = 1;
            double x0 = t->frag[fr].x, x1 = x0 + t->frag[fr].w;
            if (deco & 1) { double y = floor(base + size * 0.09 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
            if (deco & 2) { double y = floor(base - size * 0.93 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
            if (deco & 4) { double y = floor(base - size * 0.3 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
        }
}

/* the painter's context, as parameters (a struct may hold no borrow) */
#define PCTX const layout_tree *t, const net_loader *nt, amb_canvas *c, uint32_t skip_bg, const amb_fonts *f, const vector<uint32_t> *webmap, \
    vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask, vector<uint32_t> *icache, \
    vector<uint8_t> *ipx
#define PARGS t, nt, c, skip_bg, f, webmap, entries, table, masks, acc, mask, icache, ipx
#define PARGS_C(cv) t, nt, (cv), skip_bg, f, webmap, entries, table, masks, acc, mask, icache, ipx

static int pt_positioned(const layout_tree *t, uint32_t b) {
    return t->box[b].position != LAYOUT_POSITION_STATIC;
}
/* a box that makes a stacking context of its own: a z-index, an opacity, a transform */
static int pt_context(const layout_tree *t, uint32_t b) {
    return !t->box[b].z_auto || t->box[b].opacity < 1 || t->box[b].transform.len;
}

static void pt_flow(PCTX, uint32_t b, vector<uint32_t> *later, int with_box);
static void pt_ctx(PCTX, uint32_t b);
static void pt_effects_plain(PCTX, uint32_t b, int ctx);
static void pt_order(PCTX, const vector<uint32_t> *list, int neg);

/* a transform's matrix (a b c d e f: x' = a x + c y + e, y' = b x + d y + f) from its
   functions, about (ox, oy); percentages of the box's w and h */
static void pt_matrix(const char *s, size_t n, double w, double h, double ox, double oy, double *m) {
    double a = 1, b = 0, cc = 0, d = 1, e = 0, f = 0;
    size_t i = 0, pa, pl;
    while (css_next_part(s, n, &i, &pa, &pl)) {
        const char *fn = s + pa;
        size_t open = 0;
        while (open < pl && fn[open] != '(') open++;
        if (open >= pl || fn[pl - 1] != ')') continue;
        double v[6] = { 0, 0, 0, 0, 0, 0 };
        int nv = 0;
        size_t j = 0, xa, xl;
        const char *args = fn + open + 1;
        size_t al = pl - open - 2;
        while (nv < 6 && css_next_part(args, al, &j, &xa, &xl)) {
            const char *t0 = args + xa;
            double x = strtod(t0, 0);
            if (xl > 3 && memcmp(t0 + xl - 3, "deg", 3) == 0) x = x * M_PI / 180;
            else if (xl > 4 && memcmp(t0 + xl - 4, "turn", 4) == 0) x = x * 2 * M_PI;
            else if (xl > 4 && memcmp(t0 + xl - 4, "grad", 4) == 0) x = x * M_PI / 200;
            else if (xl > 0 && t0[xl - 1] == '%') x = x / 100 * (nv == 0 ? w : h);
            v[nv++] = x;
        }
        double na = 1, nb = 0, nc = 0, nd = 1, ne = 0, nf = 0;
        if (pt_is(fn, open, "translate")) { ne = v[0]; nf = nv > 1 ? v[1] : 0; }
        else if (pt_is(fn, open, "translateX")) ne = v[0];
        else if (pt_is(fn, open, "translateY")) nf = nv > 0 ? (fn[pl - 2] == '%' ? v[0] * h / w : v[0]) : 0;
        else if (pt_is(fn, open, "scale")) { na = v[0]; nd = nv > 1 ? v[1] : v[0]; }
        else if (pt_is(fn, open, "scaleX")) na = v[0];
        else if (pt_is(fn, open, "scaleY")) nd = v[0];
        else if (pt_is(fn, open, "rotate")) { na = cos(v[0]); nb = sin(v[0]); nc = -sin(v[0]); nd = cos(v[0]); }
        else if (pt_is(fn, open, "skew")) { nc = tan(v[0]); nb = nv > 1 ? tan(v[1]) : 0; }
        else if (pt_is(fn, open, "skewX")) nc = tan(v[0]);
        else if (pt_is(fn, open, "skewY")) nb = tan(v[0]);
        else if (pt_is(fn, open, "matrix") && nv == 6) { na = v[0]; nb = v[1]; nc = v[2]; nd = v[3]; ne = v[4]; nf = v[5]; }
        else continue;
        /* this one after those before it: m = m x n */
        double ra = a * na + cc * nb, rb = b * na + d * nb, rc = a * nc + cc * nd, rd = b * nc + d * nd;
        double re = a * ne + cc * nf + e, rf = b * ne + d * nf + f;
        a = ra; b = rb; cc = rc; d = rd; e = re; f = rf;
    }
    /* about the origin: T(o) M T(-o) */
    m[0] = a; m[1] = b; m[2] = cc; m[3] = d;
    m[4] = e + ox - a * ox - cc * oy;
    m[5] = f + oy - b * ox - d * oy;
}

/* a box (with its content) drawn by its effects: through a canvas of its own when it has
   a transform or an opacity, clipped when it clips its overflow; paint draws it */
static void pt_effects(PCTX, uint32_t b, int ctx) {
    double bx0 = t->box[b].x - t->box[b].pl - t->box[b].bl, by0 = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    if (t->box[b].transform.len) {
        vector<char> tv, ov;
        amb_span sp = t->box[b].transform;
        append_range(tv, text_ptr(t, sp), sp.len);
        sp = t->box[b].origin;
        append_range(ov, text_ptr(t, sp), sp.len);
        double ox = bw / 2, oy = bh / 2;
        if (len(ov)) {
            double px, py;
            pt_tile_pos(data(ov), len(ov), bw, bh, 0, 0, &px, &py);
            ox = px;
            oy = py;
        }
        double m[6];
        pt_matrix(data(tv), len(tv), bw, bh, bx0 + ox, by0 + oy, m);
        double det = m[0] * m[3] - m[1] * m[2];
        if (fabs(det) < 1e-9) return;
        /* the box drawn on black and on white (its alpha their difference), as it is */
        amb_canvas k0, k1;
        memset(&k0, 0, sizeof k0);
        memset(&k1, 0, sizeof k1);
        defer(k0) { AMB_CANVAS_DROP(k0); }
        defer(k1) { AMB_CANVAS_DROP(k1); }
        k0.w = k1.w = c->w;
        k0.h = k1.h = c->h;
        k0.oy = k1.oy = c->oy;
        k0.cx0 = k1.cx0 = 0;
        k0.cy0 = k1.cy0 = c->oy;
        k0.cx1 = k1.cx1 = c->w;
        k0.cy1 = k1.cy1 = c->oy + c->h;
        resize(&k0.px, (size_t)c->w * c->h * 3);
        resize(&k1.px, (size_t)c->w * c->h * 3);
        for (size_t q = 0; q < len(k1.px); q++) k1.px[q] = 255;
        pt_effects_plain(PARGS_C(&k0), b, ctx);
        pt_effects_plain(PARGS_C(&k1), b, ctx);
        /* mapped: each pixel of the canvas sampled from where the inverse takes it */
        double ia = m[3] / det, ib = -m[1] / det, ic = -m[2] / det, id = m[0] / det;
        double ie = -(ia * m[4] + ic * m[5]), iff = -(ib * m[4] + id * m[5]);
        /* the transformed box's bounds */
        double xs[4] = { bx0, bx0 + bw, bx0, bx0 + bw }, ys[4] = { by0, by0, by0 + bh, by0 + bh };
        double lo_x = 1e18, lo_y = 1e18, hi_x = -1e18, hi_y = -1e18;
        for (int q = 0; q < 4; q++) {
            double X = m[0] * xs[q] + m[2] * ys[q] + m[4], Y = m[1] * xs[q] + m[3] * ys[q] + m[5];
            lo_x = fmin(lo_x, X); hi_x = fmax(hi_x, X); lo_y = fmin(lo_y, Y); hi_y = fmax(hi_y, Y);
        }
        /* its overflow too: the whole canvas, which is mapped */
        int kx0, ky0, kx1, ky1;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        (void)lo_x;
        (void)hi_x;
        (void)lo_y;
        (void)hi_y;
        for (int y = ky0; y < ky1; y++)
            for (int x = kx0; x < kx1; x++) {
                double X = x + 0.5, Y = y + 0.5 + c->oy;
                double sx = ia * X + ic * Y + ie - 0.5, sy = ib * X + id * Y + iff - 0.5 - c->oy;
                if (sx < -1 || sy < -1 || sx > (double)c->w || sy > (double)c->h) continue;
                int x0 = (int)floor(sx), y0 = (int)floor(sy);
                double fx = sx - x0, fy = sy - y0, col[3] = { 0, 0, 0 }, al = 0;
                for (int q = 0; q < 4; q++) {
                    int qx = x0 + (q & 1), qy = y0 + (q >> 1);
                    double wgt = ((q & 1) ? fx : 1 - fx) * ((q >> 1) ? fy : 1 - fy);
                    if (qx < 0 || qy < 0 || qx >= (int)c->w || qy >= (int)c->h || wgt <= 0) continue;
                    size_t pi = ((size_t)qy * c->w + (size_t)qx) * 3;
                    double a0 = 1 - (at(k1.px, pi) - at(k0.px, pi)) / 255.0;
                    for (int ch = 0; ch < 3; ch++) col[ch] += at(k0.px, pi + ch) * wgt;
                    al += a0 * wgt;
                }
                if (al <= 0.002) continue;
                size_t di = ((size_t)y * c->w + (size_t)x) * 3;
                for (int ch = 0; ch < 3; ch++) {
                    double v = col[ch] + c->px[di + ch] * (1 - al);
                    c->px[di + ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
                }
            }
        return;
    }
    pt_effects_plain(PARGS, b, ctx);
}

/* a box drawn with its opacity and clip (no transform): through a copy of what was under
   it when it has an opacity, blended with it */
static void pt_effects_plain(PCTX, uint32_t b, int ctx) {
    double op = t->box[b].opacity;
    if (op <= 0) return;
    vector<uint8_t> under;
    if (op < 1) append_range(under, data(c->px), len(c->px));
    if (ctx) pt_ctx(PARGS, b);
    else {
        vector<uint32_t> later;
        pt_flow(PARGS, b, &later, 1);
        for (size_t q = 0; q < len(later); q++) pt_ctx(PARGS, at(later, q));
    }
    if (op < 1)
        for (size_t q = 0; q < len(c->px); q++) {
            int o = at(under, q), n0 = c->px[q];
            if (o != n0) c->px[q] = (uint8_t)floor(o + (n0 - o) * op + 0.5);
        }
}

/* positioned boxes drawn in stacking order: by z-index (auto as 0), the tree's order among
   equals; those below zero (neg 1) or the rest */
static void pt_order(PCTX, const vector<uint32_t> *list, int neg) {
    vector<uint32_t> ord;
    for (size_t q = 0; q < len(*list); q++) {
        uint32_t x = at(*list, q);
        if ((t->box[x].z < 0) != (neg != 0)) continue;
        push_back(ord, x);
        size_t k = len(ord) - 1;
        while (k > 0 && t->box[at(ord, k - 1)].z > t->box[x].z) { ord[k] = at(ord, k - 1); k--; }
        ord[k] = x;
    }
    for (size_t q = 0; q < len(ord); q++) {
        uint32_t x = at(ord, q);
        if (pt_context(t, x)) pt_effects(PARGS, x, 1);
        else {
            /* positioned, z-index auto: drawn as a context, but what is positioned in it is its parent context's */
            vector<uint32_t> its;
            pt_flow(PARGS, x, &its, 1);
        }
    }
}

/* a box in the flow: its box (with_box), its lines and its children in tree order, clipped to
   its padding box when it clips its overflow; the positioned boxes in it kept in later for
   its stacking context (those under a clipping box drawn there, clipped) */
static void pt_flow(PCTX, uint32_t b, vector<uint32_t> *later, int with_box) {
    if (with_box && t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) pt_box(t, nt, b, c, skip_bg, icache, ipx);
    double save[4] = { c->cx0, c->cy0, c->cx1, c->cy1 };
    int clips = t->box[b].clip != 0;
    if (clips) {
        c->cx0 = fmax(c->cx0, t->box[b].x - t->box[b].pl);
        c->cy0 = fmax(c->cy0, t->box[b].y - t->box[b].pt);
        c->cx1 = fmin(c->cx1, t->box[b].x + t->box[b].w + t->box[b].pr);
        c->cy1 = fmin(c->cy1, t->box[b].y + t->box[b].h + t->box[b].pb);
    }
    pt_lines(t, b, c, f, webmap, entries, table, masks, acc, mask);
    vector<uint32_t> inner;
    for (uint32_t ch = t->box[b].first_child; ch; ch = t->box[ch].next) {
        if (pt_positioned(t, ch)) {
            if (clips) push_back(inner, ch);
            else push_back(later, ch);
            continue;
        }
        if (t->box[ch].opacity < 1 || t->box[ch].transform.len) { pt_effects(PARGS, ch, 0); continue; }
        if (clips) pt_flow(PARGS, ch, &inner, 1);
        else pt_flow(PARGS, ch, later, 1);
    }
    if (len(inner)) {
        pt_order(PARGS, &inner, 1);
        pt_order(PARGS, &inner, 0);
    }
    c->cx0 = save[0];
    c->cy0 = save[1];
    c->cx1 = save[2];
    c->cy1 = save[3];
    if (with_box) pt_outline(t, b, c);
}

/* the positioned boxes in b's flow: those its stacking context draws */
static void pt_collect(const layout_tree *t, uint32_t b, vector<uint32_t> *later) {
    for (uint32_t ch = t->box[b].first_child; ch; ch = t->box[ch].next) {
        if (pt_positioned(t, ch)) {
            push_back(later, ch);
            /* one of z-index auto: what is positioned in it is this context's too */
            if (!pt_context(t, ch) && !t->box[ch].clip) pt_collect(t, ch, later);
            continue;
        }
        if (t->box[ch].clip || pt_context(t, ch)) continue;
        pt_collect(t, ch, later);
    }
}

/* a stacking context: its own box; the positioned boxes in its flow with a negative
   z-index; its flow; then the rest of them, by z-index */
static void pt_ctx(PCTX, uint32_t b) {
    if (t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) pt_box(t, nt, b, c, skip_bg, icache, ipx);
    vector<uint32_t> later, seen;
    pt_collect(t, b, &later);
    pt_order(PARGS, &later, 1);
    pt_flow(PARGS, b, &seen, 0);
    pt_order(PARGS, &later, 0);
    pt_outline(t, b, c);
}

/* the page drawn onto c, its window from page y oy down at most maxh px (the page's
   own height when shorter) */
static void pt_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c, double oy, double maxh) {
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    /* the web fonts the fragments are set in, each read once */
    vector<uint32_t> webmap;
    for (uint32_t fr = 1; nt && fr < t->frag_len; fr++) {
        uint32_t r = t->frag[fr].font.index;
        if (!t->frag[fr].live || !r || r >= nt->resource_len) continue;
        int known = 0;
        for (size_t k = 0; k + 1 < len(webmap) && !known; k += 2) if (at(webmap, k) == r) known = 1;
        if (known) continue;
        vector<uint8_t> bytes;
        amb_span body = nt->resource[r].body;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, body), body.len);
        int face = amb_fonts_add(&f, data(bytes), len(bytes));
        push_back(webmap, r);
        push_back(webmap, (uint32_t)(face < 0 ? AMB_FACE_SERIF : face));
    }
    uint32_t vp = 0;
    for (uint32_t b = 1; b < t->box_len && !vp; b++) if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    double w = vp ? t->box[vp].w : 800, h = vp ? t->box[vp].h : 600;
    if (oy < 0) oy = 0;
    h -= oy;
    if (h > maxh) h = maxh;
    if (h < 1) h = 1;
    c->oy = oy;
    c->w = (uint32_t)ceil(w);
    c->h = (uint32_t)ceil(h);
    c->cx0 = 0;
    c->cy0 = oy;
    c->cx1 = c->w;
    c->cy1 = oy + c->h;
    clear(c->px);
    resize(&c->px, (size_t)c->w * c->h * 3);
    /* the canvas's background: the root element's, or the body's if the root's is
       transparent; that box then does not draw it again */
    uint32_t bg = 0xffffffff, skip = 0;
    uint32_t root = vp ? t->box[vp].first_child : 0;
    if (root && (t->box[root].background & 0xff)) { bg = t->box[root].background; skip = root; }
    else if (root) {
        for (uint32_t ch = t->box[root].first_child; ch; ch = t->box[ch].next)
            if (t->box[ch].kind == LAYOUT_BOX_KIND_BLOCK && (t->box[ch].background & 0xff)) { bg = t->box[ch].background; skip = ch; break; }
    }
    c->background = bg | 0xff;
    if ((bg & 0xff) != 0xff) pt_fill(c, 0, 0, c->w, c->h, 0xffffffff);
    pt_fill(c, 0, 0, c->w, c->h, bg);
    vector<pt_glyph> entries;
    vector<uint32_t> table;
    vector<uint8_t> masks, mask;
    vector<float> acc;
    vector<uint32_t> icache;
    vector<uint8_t> ipx;
    resize(&table, 1024);
    if (vp) pt_ctx(t, nt, c, skip, &f, &webmap, &entries, &table, &masks, &acc, &mask, &icache, &ipx, vp);
}

void amb_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c) { pt_paint(t, nt, c, 0, 16384); }

void amb_paint_view(const layout_tree *t, const net_loader *nt, amb_canvas *c, double y, double h) { pt_paint(t, nt, c, y, h); }

void amb_canvas_zoom(const amb_canvas *src, amb_canvas *dst, double z) {
    if (z <= 0) z = 1;
    uint32_t w = (uint32_t)(src->w * z + 0.5), h = (uint32_t)(src->h * z + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    dst->w = w;
    dst->h = h;
    dst->background = src->background;
    dst->oy = src->oy * z;
    dst->cx0 = src->cx0 * z;
    dst->cy0 = src->cy0 * z;
    dst->cx1 = src->cx1 * z;
    dst->cy1 = src->cy1 * z;
    clear(dst->px);
    resize(&dst->px, (size_t)w * h * 3);
    const uint8_t *sp = data(src->px);
    uint8_t *dp = data(dst->px);
    uint32_t sw = src->w, sh = src->h;
    for (uint32_t y = 0; y < h; y++) {
        /* each device pixel's centre in the source's pixels, between the four it falls among */
        double fy = (y + 0.5) / z - 0.5;
        if (fy < 0) fy = 0;
        uint32_t y0 = (uint32_t)fy, y1 = y0 + 1 < sh ? y0 + 1 : y0;
        double ty = fy - y0;
        for (uint32_t x = 0; x < w; x++) {
            double fx = (x + 0.5) / z - 0.5;
            if (fx < 0) fx = 0;
            uint32_t x0 = (uint32_t)fx, x1 = x0 + 1 < sw ? x0 + 1 : x0;
            if (x0 >= sw) x0 = x1 = sw - 1;
            double tx = fx - x0;
            for (int k = 0; k < 3; k++) {
                double a = sp[((size_t)y0 * sw + x0) * 3 + k], b = sp[((size_t)y0 * sw + x1) * 3 + k];
                double cc = sp[((size_t)y1 * sw + x0) * 3 + k], d = sp[((size_t)y1 * sw + x1) * 3 + k];
                double v = (a + (b - a) * tx) * (1 - ty) + (cc + (d - cc) * tx) * ty;
                dp[((size_t)y * w + x) * 3 + k] = (uint8_t)(v + 0.5);
            }
        }
    }
}

uint64_t amb_canvas_hash(const amb_canvas *c) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len(c->px); i++) { h ^= at(c->px, i); h *= 1099511628211ull; }
    return h;
}

/* ---- PNG ---- */

static uint32_t pt_crc(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

static void pt_be32(vector<uint8_t> *o, uint32_t v) {
    push_back(o, (uint8_t)(v >> 24));
    push_back(o, (uint8_t)(v >> 16));
    push_back(o, (uint8_t)(v >> 8));
    push_back(o, (uint8_t)v);
}

static int pt_chunk(FILE *out, const char *type, const vector<uint8_t> *body) {
    vector<uint8_t> head;
    pt_be32(&head, (uint32_t)len(*body));
    for (int i = 0; i < 4; i++) push_back(head, (uint8_t)type[i]);
    uint32_t crc = pt_crc(0, data(head) + 4, 4);
    crc = pt_crc(crc, data(*body), len(*body));
    vector<uint8_t> tail;
    pt_be32(&tail, crc);
    return fwrite(data(head), 1, len(head), out) == len(head) && fwrite(data(*body), 1, len(*body), out) == len(*body) &&
           fwrite(data(tail), 1, 4, out) == 4;
}

/* bits, least significant first, as deflate writes them */
static void pt_bits(vector<uint8_t> *o, uint32_t *acc, int *n, uint32_t v, int k) {
    *acc |= v << *n;
    *n += k;
    while (*n >= 8) {
        push_back(o, (uint8_t)(*acc & 0xff));
        *acc >>= 8;
        *n -= 8;
    }
}
static uint32_t pt_rev(uint32_t v, int k) {
    uint32_t r = 0;
    for (int i = 0; i < k; i++) { r = r << 1 | (v & 1); v >>= 1; }
    return r;
}
/* a literal/length symbol in the fixed code (RFC 1951, 3.2.6) */
static void pt_sym(vector<uint8_t> *o, uint32_t *acc, int *n, int s) {
    if (s < 144) pt_bits(o, acc, n, pt_rev((uint32_t)(0x30 + s), 8), 8);
    else if (s < 256) pt_bits(o, acc, n, pt_rev((uint32_t)(0x190 + s - 144), 9), 9);
    else if (s < 280) pt_bits(o, acc, n, pt_rev((uint32_t)(s - 256), 7), 7);
    else pt_bits(o, acc, n, pt_rev((uint32_t)(0xC0 + s - 280), 8), 8);
}

static const uint16_t pt_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t pt_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t pt_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                       4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t pt_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* a zlib stream of the bytes: one block of fixed codes, LZ77 over a hash of three bytes */
static void pt_deflate(const vector<uint8_t> *in, vector<uint8_t> *o) {
    uint32_t acc = 0;
    int nb = 0;
    push_back(o, 0x78);
    push_back(o, 0x01);
    pt_bits(o, &acc, &nb, 1, 1);
    pt_bits(o, &acc, &nb, 1, 2);
    vector<uint32_t> head;
    resize(&head, 1 << 15);
    size_t n = len(*in), i = 0;
    while (i < n) {
        if (i + 3 <= n) {
            uint32_t hsh = ((uint32_t)at(*in, i) << 10 ^ (uint32_t)at(*in, i + 1) << 5 ^ (uint32_t)at(*in, i + 2)) & 0x7fff;
            uint32_t cand = at(head, hsh);
            head[hsh] = (uint32_t)i + 1;
            if (cand && i - (cand - 1) <= 32768 && i - (cand - 1) > 0) {
                size_t c = cand - 1, l = 0;
                while (l < 258 && i + l < n && at(*in, c + l) == at(*in, i + l)) l++;
                if (l >= 3) {
                    int lc = 28;
                    while (pt_lbase[lc] > l) lc--;
                    pt_sym(o, &acc, &nb, 257 + lc);
                    pt_bits(o, &acc, &nb, (uint32_t)(l - pt_lbase[lc]), pt_lext[lc]);
                    size_t dist = i - c;
                    int dc = 29;
                    while (pt_dbase[dc] > dist) dc--;
                    pt_bits(o, &acc, &nb, pt_rev((uint32_t)dc, 5), 5);
                    pt_bits(o, &acc, &nb, (uint32_t)(dist - pt_dbase[dc]), pt_dext[dc]);
                    i += l;
                    continue;
                }
            }
        }
        pt_sym(o, &acc, &nb, at(*in, i));
        i++;
    }
    pt_sym(o, &acc, &nb, 256);
    if (nb > 0) push_back(o, (uint8_t)(acc & 0xff));
    /* Adler-32 of the bytes */
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < n; k++) { a = (a + at(*in, k)) % 65521; b = (b + a) % 65521; }
    pt_be32(o, b << 16 | a);
}

int amb_png_write(const amb_canvas *c, FILE *out) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (fwrite(sig, 1, 8, out) != 8) return 0;
    vector<uint8_t> ihdr;
    pt_be32(&ihdr, c->w);
    pt_be32(&ihdr, c->h);
    push_back(ihdr, 8);
    push_back(ihdr, 2);
    push_back(ihdr, 0);
    push_back(ihdr, 0);
    push_back(ihdr, 0);
    if (!pt_chunk(out, "IHDR", &ihdr)) return 0;
    /* each row filtered: None, Sub or Up, whichever leaves the smallest sum */
    size_t row = (size_t)c->w * 3;
    vector<uint8_t> raw;
    for (uint32_t y = 0; y < c->h; y++) {
        size_t base = (size_t)y * row;
        uint64_t sums[3] = { 0, 0, 0 };
        for (size_t x = 0; x < row; x++) {
            int v = at(c->px, base + x), left = x >= 3 ? at(c->px, base + x - 3) : 0, up = y ? at(c->px, base + x - row) : 0;
            sums[0] += (uint64_t)(v < 128 ? v : 256 - v);
            int s1 = (v - left) & 0xff, s2 = (v - up) & 0xff;
            sums[1] += (uint64_t)(s1 < 128 ? s1 : 256 - s1);
            sums[2] += (uint64_t)(s2 < 128 ? s2 : 256 - s2);
        }
        int ft = sums[1] < sums[0] ? 1 : 0;
        if (sums[2] < sums[ft]) ft = 2;
        push_back(raw, (uint8_t)ft);
        for (size_t x = 0; x < row; x++) {
            int v = at(c->px, base + x), left = x >= 3 ? at(c->px, base + x - 3) : 0, up = y ? at(c->px, base + x - row) : 0;
            push_back(raw, (uint8_t)(ft == 0 ? v : ft == 1 ? (v - left) & 0xff : (v - up) & 0xff));
        }
    }
    vector<uint8_t> z;
    pt_deflate(&raw, &z);
    if (!pt_chunk(out, "IDAT", &z)) return 0;
    vector<uint8_t> none;
    return pt_chunk(out, "IEND", &none);
}
