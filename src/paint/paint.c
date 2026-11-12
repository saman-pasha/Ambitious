/* src/paint/paint.c -- a laid-out page drawn, and written as a PNG (paint.h).

   The order is CSS 2.1's (Appendix E), simplified: the canvas, its colour the
   root element's background (or the body's, propagated); then each stacking
   context -- the root, and each positioned box -- paints its own box, the
   positioned boxes in it with a negative z-index, its flow (each box in tree
   order: its shadows, its background, its borders, its lines -- inline
   elements' backgrounds (an element with borders, rounded corners, images
   or shadows drawn from its box of them, the element as one strip sliced
   at its line breaks), text and its shadows, decorations, list markers --
   its child boxes, its outline), then those with z-index auto or 0 in tree
   order and those with a positive one by z-index. A box that clips its overflow
   clips what is in it to its padding box.

   A box: its outer box-shadows (offset, spread, blurred: three box blurs, a
   Gaussian's), its background colour and layers (url() images, linear and
   radial gradients; background-size, -position and -repeat; the last layer
   first) in its border box, rounded by its corners' radii (a fieldset's from
   its legend's middle, its top border left out behind the legend); its borders, solid,
   dotted, dashed, double, groove, ridge, inset or outset (Chromium's darker
   colour for the shaded sides), round where its corners are. A box with an
   opacity is drawn and blended with what was under it; one with a transform
   (translate, scale, rotate, skew, matrix, about its transform-origin) is drawn
   on black and on white, which give its colours and their alpha, and mapped
   onto the canvas, sampled bilinearly. Glyphs come from font.c at their size and
   quarter-pixel position, cached. An image is drawn from the bytes the loader
   fetched for its element (image.c decodes PNG), sampled bilinearly into its
   content box; one whose load failed is Chromium's fallback (its 16px icon,
   before its alt text or in a box of its own size). The PNG is
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
amb_interface(css);
amb_interface(layout);
amb_interface(net);

#include "paint.h"
#include "image.h"
#include "svg.h"
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
    return (uint64_t)(face & 127) << 57 | (uint64_t)(italic & 1) << 56 | (uint64_t)(quarter & 3) << 54 | sz << 38 | (uint64_t)(cp & 0x1fffff);
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
        /* Skia's subpixel position: an eighth of a px added, then the quarter below it (SkGlyphPositionRoundingSpec) */
        double fx = floor(x + 0.125);
        int quarter = (int)((x + 0.125 - fx) * 4);
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

/* a text's glyphs' coverage as pt_text draws it, into the mask m (mw px wide and mh rows,
   its first row the page's y my0): each pixel the most any glyph covers of it */
static void pt_text_mask(vector<uint8_t> *m, int mw, double my0, int mh, const amb_fonts *f, vector<pt_glyph> *entries, vector<uint32_t> *table,
                         vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask, const char *s, size_t n, double x, double baseline,
                         int face, double size, int italic, double ls, double wsp) {
    int by = (int)floor(baseline - my0 + 0.5);
    size_t i = 0;
    uint32_t prev = 0;
    while (i < n) {
        uint32_t cp = amb_utf8_next(s, n, &i);
        if (prev) x += amb_kern(f, face, prev, cp, size);
        prev = cp;
        /* Skia's subpixel position: an eighth of a px added, then the quarter below it (SkGlyphPositionRoundingSpec) */
        double fx = floor(x + 0.125);
        int quarter = (int)((x + 0.125 - fx) * 4);
        uint32_t e = pt_glyph_of(f, entries, table, masks, acc, mask, face, size, cp, quarter, italic);
        pt_glyph gl = at(*entries, e);
        for (int yy = 0; yy < gl.h; yy++) {
            int Y = by + gl.oy + yy;
            if (Y < 0 || Y >= mh) continue;
            for (int xx = 0; xx < gl.w; xx++) {
                int X = (int)fx + gl.ox + xx;
                if (X < 0 || X >= mw) continue;
                uint8_t v = at(*masks, gl.off + (size_t)yy * gl.w + xx);
                size_t q = (size_t)Y * (size_t)mw + (size_t)X;
                if (v > at(*m, q)) (*m)[q] = v;
            }
        }
        x += amb_advance(f, face, cp, size) + ls + (cp == ' ' ? wsp : 0);
    }
}

/* ---- the boxes ---- */

/* an image's RGBA pixels, w x h, drawn into the rectangle (x0, y0, dw, dh), sampled
   bilinearly and blended by their alpha */
/* the next mip level of a premultiplied image (Skia's SkMipmap): half its size each way, floored, at least 1 px; each
   pixel two taps of the level above it each way (1 1), three where that size is odd (1 2 1), one where it is 1;
   summed and shifted down, truncating, as Skia's downsamplers do */
static void pt_mip_level(const vector<uint8_t> *src, uint32_t w, uint32_t h, vector<uint8_t> *dst, uint32_t *ow, uint32_t *oh) {
    uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    int tx = w == 1 ? 1 : (w & 1) ? 3 : 2, ty = h == 1 ? 1 : (h & 1) ? 3 : 2;
    int shift = (tx == 3 ? 2 : tx == 2 ? 1 : 0) + (ty == 3 ? 2 : ty == 2 ? 1 : 0);
    clear(*dst);
    resize(*dst, (size_t)nw * nh * 4);
    for (uint32_t y = 0; y < nh; y++)
        for (uint32_t x = 0; x < nw; x++)
            for (int k = 0; k < 4; k++) {
                uint32_t acc = 0;
                for (int j = 0; j < ty; j++)
                    for (int i = 0; i < tx; i++) {
                        uint32_t sx = w == 1 ? 0 : 2 * x + (uint32_t)i, sy = h == 1 ? 0 : 2 * y + (uint32_t)j;
                        uint32_t wx = tx == 3 && i == 1 ? 2 : 1, wy = ty == 3 && j == 1 ? 2 : 1;
                        acc += wx * wy * at(*src, ((size_t)sy * w + sx) * 4 + (size_t)k);
                    }
                (*dst)[((size_t)y * nw + x) * 4 + (size_t)k] = (uint8_t)(acc >> shift);
            }
    *ow = nw;
    *oh = nh;
}

/* an image drawn into (x0, y0, dw, dh), bilinear (its edges clamped); with aa, its edge pixels covered by as much of
   them as the rectangle covers, as Skia draws an image at a fractional place (an img's under a zoom); without, from
   the nearest whole pixels (a repeated background's tiles, which meet) */
static void pt_image_ex(amb_canvas *c, const vector<uint8_t> *rgba, uint32_t w, uint32_t h, double x0, double y0, double dw, double dh, int aa) {
    if (dw <= 0 || dh <= 0 || w == 0 || h == 0) return;
    double s = fmin(dw / w, dh / h);
    if (s < 0.5) {
        /* drawn smaller than half its size: from a mip level, as Skia's medium quality draws (Chromium's for an image),
           the level the nearest to the inverse scale's log2 less Skia's half-level sharpening, its smaller axis's:
           floor(log2(1 / s)); built from the premultiplied pixels, drawn bilinear from there */
        int lv = (int)floor(-log2(s));
        vector<uint8_t> cur, nxt;
        resize(cur, len(*rgba));
        for (size_t q = 0; q + 3 < len(*rgba); q += 4) {
            uint32_t a = at(*rgba, q + 3);
            for (int k = 0; k < 3; k++) cur[q + (size_t)k] = (uint8_t)((at(*rgba, q + (size_t)k) * a + 127) / 255);
            cur[q + 3] = (uint8_t)a;
        }
        uint32_t lw = w, lh = h;
        for (int n = 0; n < lv && (lw > 1 || lh > 1); n++) {
            uint32_t nw2, nh2;
            pt_mip_level(&cur, lw, lh, &nxt, &nw2, &nh2);
            clear(cur);
            append_range(cur, data(nxt), len(nxt));
            lw = nw2;
            lh = nh2;
        }
        for (size_t q = 0; q + 3 < len(cur); q += 4) {
            uint32_t a = at(cur, q + 3);
            for (int k = 0; k < 3; k++) cur[q + (size_t)k] = a ? (uint8_t)(fmin(255, at(cur, q + (size_t)k) * 255.0 / a + 0.5)) : 0;
        }
        if (fmin(dw / lw, dh / lh) >= 0.5) { pt_image_ex(c, &cur, lw, lh, x0, y0, dw, dh, aa); return; }
    }
    y0 -= c->oy;
    int ix0 = (int)floor(x0 + 0.5), iy0 = (int)floor(y0 + 0.5), ix1 = (int)floor(x0 + dw + 0.5), iy1 = (int)floor(y0 + dh + 0.5);
    if (aa) {
        ix0 = (int)floor(x0);
        iy0 = (int)floor(y0);
        ix1 = (int)ceil(x0 + dw);
        iy1 = (int)ceil(y0 + dh);
    }
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int y = iy0 < ky0 ? ky0 : iy0; y < iy1 && y < ky1; y++)
        for (int x = ix0 < kx0 ? kx0 : ix0; x < ix1 && x < kx1; x++) {
            double cov = 1;
            if (aa) cov = fmax(0, fmin(x + 1.0, x0 + dw) - fmax((double)x, x0)) * fmax(0, fmin(y + 1.0, y0 + dh) - fmax((double)y, y0));
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
            double a = px[3] / 255 * cov;
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            for (int k = 0; k < 3; k++) c->px[i + k] = (uint8_t)(px[k] * a + c->px[i + k] * (1 - a) + 0.5);
        }
}
static void pt_image(amb_canvas *c, const vector<uint8_t> *rgba, uint32_t w, uint32_t h, double x0, double y0, double dw, double dh) {
    pt_image_ex(c, rgba, w, h, x0, y0, dw, dh, 0);
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
   (x0, y0)-(x1, y1) with its corners' radii r covers: 0 to 1, its edges and arcs
   anti-aliased. r: the horizontal radii (top left, top right, bottom right, bottom left),
   then the vertical ones; a corner is round when both of its radii are */
static double pt_rr_cover(double px, double py, double x0, double y0, double x1, double y1, const double *r) {
    double e = px - x0 + 0.5, cov = e < 1 ? (e > 0 ? e : 0) : 1;
    double e2 = x1 - px + 0.5;
    if (e2 < cov) cov = e2 < 0 ? 0 : e2;
    double e3 = py - y0 + 0.5;
    if (e3 < cov) cov = e3 < 0 ? 0 : e3;
    double e4 = y1 - py + 0.5;
    if (e4 < cov) cov = e4 < 0 ? 0 : e4;
    if (cov <= 0) return 0;
    double cx = 0, cy = 0, rx = 0, ry = 0;
    if (r[0] > 0 && r[4] > 0 && px < x0 + r[0] && py < y0 + r[4]) { cx = x0 + r[0]; cy = y0 + r[4]; rx = r[0]; ry = r[4]; }
    else if (r[1] > 0 && r[5] > 0 && px > x1 - r[1] && py < y0 + r[5]) { cx = x1 - r[1]; cy = y0 + r[5]; rx = r[1]; ry = r[5]; }
    else if (r[2] > 0 && r[6] > 0 && px > x1 - r[2] && py > y1 - r[6]) { cx = x1 - r[2]; cy = y1 - r[6]; rx = r[2]; ry = r[6]; }
    else if (r[3] > 0 && r[7] > 0 && px < x0 + r[3] && py > y1 - r[7]) { cx = x0 + r[3]; cy = y1 - r[7]; rx = r[3]; ry = r[7]; }
    if (rx > 0) {
        /* the distance to the arc, as the ellipse's level over its gradient: a circle's
           radius less the distance to its centre */
        double dx = (px - cx) / rx, dy = (py - cy) / ry, d = sqrt(dx * dx + dy * dy);
        double gx = dx / rx, gy = dy / ry, g = sqrt(gx * gx + gy * gy);
        double k = g > 0 ? (1 - d) * d / g + 0.5 : 1;
        if (k < cov) cov = k < 0 ? 0 : k;
    }
    return cov > 1 ? 1 : cov;
}

/* radii that fit their box: the eight scaled down together when two along a side sum more
   than it (CSS Backgrounds 5.5): the horizontal ones along the top and bottom, the vertical
   ones along the left and right */
static void pt_fit_radii(double w, double h, double *r) {
    double f = 1;
    if (r[0] + r[1] > w && r[0] + r[1] > 0) f = fmin(f, w / (r[0] + r[1]));
    if (r[3] + r[2] > w && r[3] + r[2] > 0) f = fmin(f, w / (r[3] + r[2]));
    if (r[4] + r[7] > h && r[4] + r[7] > 0) f = fmin(f, h / (r[4] + r[7]));
    if (r[5] + r[6] > h && r[5] + r[6] > 0) f = fmin(f, h / (r[5] + r[6]));
    for (int k = 0; k < 8; k++) r[k] = r[k] * f > 0 ? r[k] * f : 0;
}
/* whether any corner of r is round */
static int pt_round(const double *r) {
    for (int k = 0; k < 4; k++) if (r[k] > 0 && r[k + 4] > 0) return 1;
    return 0;
}

/* a rounded rectangle filled (the clip's part of it) */
static void pt_rrect(amb_canvas *c, double x0, double y0, double x1, double y1, const double *r, uint32_t rgba) {
    if ((rgba & 0xff) == 0 || x1 <= x0 || y1 <= y0) return;
    if (!pt_round(r)) { pt_fill(c, x0, y0, x1, y1, rgba); return; }
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
/* a colour's relative luminance (sRGB's), as Blink weighs it */
static double pt_luminance(uint32_t rgba) {
    double ch[3] = { (rgba >> 24) / 255.0, (rgba >> 16 & 0xff) / 255.0, (rgba >> 8 & 0xff) / 255.0 };
    for (int k = 0; k < 3; k++) ch[k] = ch[k] <= 0.04045 ? ch[k] / 12.92 : pow((ch[k] + 0.055) / 1.055, 2.4);
    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}
/* inset's colour for a side (outset's the other way), as Blink's CalculateBorderStyleColor picks it: the top and left
   its dark side, the bottom and right its light one. A colour as dark as #202020 or darker is lightened on both (its
   dark side once, its light side twice); one lighter than #ebebeb is darkened on its dark side and kept on its light;
   any other darkened and lightened (measured in Chromium on twelve colours) */
static uint32_t pt_inset(uint32_t col, int side, int inset) {
    int darken = ((side == 0 || side == 3) == (inset != 0));
    double lum = pt_luminance(col);
    if (lum <= 0.014443844 + 1e-9) return darken ? pt_light(col) : pt_light(pt_light(col));
    if (darken) return pt_dark(col);
    return lum > 0.83077 + 1e-6 ? col : pt_light(col);
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

/* ---- polygons: each edge's signed area accumulated per pixel along its rows, as
   src/paint/font.c draws glyphs (font-rs's method), then summed across each row ---- */

static void pt_acc_line(vector<float> *acc, int W, int H, double x0, double y0, double x1, double y1) {
    if (y0 == y1) return;
    double dir = 1;
    if (y0 > y1) {
        double t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dir = -1;
    }
    double dxdy = (x1 - x0) / (y1 - y0);
    double x = x0;
    if (y0 < 0) { x -= y0 * dxdy; y0 = 0; }
    if (y1 > H) y1 = H;
    if (y0 >= y1) return;
    int ys = (int)floor(y0), ye = (int)ceil(y1);
    for (int y = ys; y < ye; y++) {
        double dy = fmin((double)(y + 1), y1) - fmax((double)y, y0);
        double xnext = x + dxdy * dy;
        double d = dy * dir;
        double xa = x < xnext ? x : xnext, xb = x < xnext ? xnext : x;
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        if (xa > W - 2) xa = W - 2;
        if (xb > W - 2) xb = W - 2;
        double xaf = floor(xa), xbc = ceil(xb);
        int xai = (int)xaf, xbi = (int)xbc;
        size_t row = (size_t)y * (size_t)W;
        if (xbi <= xai + 1) {
            double xmf = 0.5 * (xa + xb) - xaf;
            (*acc)[row + xai] += (float)(d - d * xmf);
            (*acc)[row + xai + 1] += (float)(d * xmf);
        } else {
            double s = 1.0 / (xb - xa);
            double x0f = xa - xaf;
            double a0 = 0.5 * s * (1.0 - x0f) * (1.0 - x0f);
            double x1f = xb - xbc + 1.0;
            double am = 0.5 * s * x1f * x1f;
            (*acc)[row + xai] += (float)(d * a0);
            if (xbi == xai + 2) (*acc)[row + xai + 1] += (float)(d * (1.0 - a0 - am));
            else {
                double a1 = s * (1.5 - x0f);
                (*acc)[row + xai + 1] += (float)(d * (a1 - a0));
                for (int xi = xai + 2; xi < xbi - 1; xi++) (*acc)[row + xi] += (float)(d * s);
                double a2 = a1 + (xbi - xai - 3) * s;
                (*acc)[row + xbi - 1] += (float)(d * (1.0 - a2 - am));
            }
            (*acc)[row + xbi] += (float)(d * am);
        }
        x = xnext;
    }
}

/* a closed polygon of n points (page coordinates) filled in rgba, non-zero, anti-aliased;
   acc its scratch */
static void pt_poly(amb_canvas *c, const vector<double> *xs, const vector<double> *ys, uint32_t rgba, vector<float> *acc) {
    size_t n = len(*xs);
    if (n < 3 || (rgba & 0xff) == 0) return;
    double minx = at(*xs, 0), maxx = minx, miny = at(*ys, 0), maxy = miny;
    for (size_t k = 1; k < n; k++) {
        minx = fmin(minx, at(*xs, k)); maxx = fmax(maxx, at(*xs, k));
        miny = fmin(miny, at(*ys, k)); maxy = fmax(maxy, at(*ys, k));
    }
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int ox = (int)floor(minx), oy = (int)floor(miny - c->oy);
    int W = (int)ceil(maxx) - ox + 2, H = (int)ceil(maxy - c->oy) - oy + 1;
    if (W <= 2 || H <= 0 || (double)W * H > 4e7) return;
    clear(*acc);
    resize(acc, (size_t)W * (size_t)H);
    for (size_t k = 0; k < n; k++) {
        size_t j = k + 1 == n ? 0 : k + 1;
        pt_acc_line(acc, W, H, at(*xs, k) - ox, at(*ys, k) - c->oy - oy, at(*xs, j) - ox, at(*ys, j) - c->oy - oy);
    }
    for (int y = 0; y < H; y++) {
        int Y = oy + y;
        if (Y < ky0 || Y >= ky1) continue;
        double sum = 0;
        for (int x = 0; x < W; x++) {
            sum += at(*acc, (size_t)y * W + x);
            int X = ox + x;
            if (X < kx0 || X >= kx1) continue;
            double a = fabs(sum);
            if (a > 0.002) pt_blend(c, X, Y, rgba, a > 1 ? 1 : a);
        }
    }
}

/* how much of the pixel at (px, py) (its centre) the rectangle (x0, y0)-(x1, y1) covers: 0 to 1 */
static double pt_sq_cover(double px, double py, double x0, double y0, double x1, double y1) {
    double cov = fmin(fmin(px - x0 + 0.5, x1 - px + 0.5), fmin(py - y0 + 0.5, y1 - py + 0.5));
    return cov <= 0 ? 0 : cov > 1 ? 1 : cov;
}

/* a corner of a border, the square (x0, y0)-(x1, y1) where a horizontal and a vertical
   side meet, split along its diagonal from the box's outer corner (corner 0 top left, 1
   top right, 2 bottom right, 3 bottom left): the horizontal side's share of the pixel at
   (px, py), 0 to 1, anti-aliased (the signed distance to the diagonal, positive on the
   horizontal side's part) */
static double pt_miter_share(double px, double py, double x0, double y0, double x1, double y1, int corner) {
    double w = x1 - x0, h = y1 - y0, dl = sqrt(w * w + h * h);
    double u = corner == 0 || corner == 3 ? px - x0 : x1 - px, v = corner < 2 ? py - y0 : y1 - py;
    double fh = 0.5 + (u * h - v * w) / dl;
    return fh < 0 ? 0 : fh > 1 ? 1 : fh;
}

/* a corner of a solid border: one colour when its sides' agree, else split along its
   diagonal (pt_miter_share), the horizontal side's colour (ch) on its part, the vertical
   side's (cv) on the other */
static void pt_corner(amb_canvas *c, double x0, double y0, double x1, double y1, int corner, uint32_t ch, uint32_t cv) {
    if (x1 <= x0 || y1 <= y0) return;
    if (ch == cv) { pt_fill(c, x0, y0, x1, y1, ch); return; }
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int ya = (int)floor(y0 - c->oy), yb = (int)ceil(y1 - c->oy), xa = (int)floor(x0), xb = (int)ceil(x1);
    for (int y = ya < ky0 ? ky0 : ya; y < yb && y < ky1; y++)
        for (int x = xa < kx0 ? kx0 : xa; x < xb && x < kx1; x++) {
            double px = x + 0.5, py = y + 0.5 + c->oy;
            double cov = pt_sq_cover(px, py, x0, y0, x1, y1);
            if (cov <= 0) continue;
            double fh = pt_miter_share(px, py, x0, y0, x1, y1, corner);
            if (fh > 0) pt_blend(c, x, y, ch, cov * fh);
            if (fh < 1) pt_blend(c, x, y, cv, cov * (1 - fh));
        }
}

/* one side of a border, w thick, along the rectangle's edge from (x0, y0) to (x1, y1) (its
   inside), drawn in its style: solid, double (two lines of a third), dotted (round dots),
   dashed, the shaded halves of groove and ridge. Dashes and dots are laid along the side's
   whole length from corner to corner, f0 to f1 along it (Blink's: a dash twice as long as
   the side is thick and a gap as long, three times and twice under 3px; the gap adjusted
   so that a dash begins and ends the side), and drawn where the side is */
static void pt_side(amb_canvas *c, double x0, double y0, double x1, double y1, int side, int bs, uint32_t col, double f0, double f1) {
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
        double full = f1 - f0, lo = horiz ? x0 : y0, hi = horiz ? x1 : y1;
        double dash = bs == LAY_BS_DOTTED ? w : w >= 3 ? 2 * w : 3 * w;
        double gap0 = bs == LAY_BS_DOTTED ? w : w >= 3 ? w : 2 * w;
        long n = 1;
        double gap = 0;
        if (bs == LAY_BS_DASHED && full <= 2 * dash) { n = 1; dash = full; }            /* no room: solid */
        else if (bs == LAY_BS_DASHED && full <= 2 * dash + gap0) {
            double m = full / (2 * dash + gap0);                                          /* two, in proportion */
            dash *= m;
            gap = gap0 * m;
            n = 2;
        } else {
            gap = pt_dash_gap(full, dash, gap0, &n);
            if (n < 1) { n = 1; gap = 0; }
        }
        for (long k = 0; k < n; k++) {
            double a = f0 + k * (dash + gap), z = a + dash;
            if (z <= lo || a >= hi) continue;
            if (bs == LAY_BS_DOTTED && w >= 3) {
                double rr[8] = { w / 2, w / 2, w / 2, w / 2, w / 2, w / 2, w / 2, w / 2 };
                if (horiz) pt_rrect(c, a, y0, z, y1, rr, col); else pt_rrect(c, x0, a, x1, z, rr, col);
            } else {
                if (a < lo) a = lo;
                if (z > hi) z = hi;
                if (horiz) pt_fill(c, a, y0, z, y1, col);
                else pt_fill(c, x0, a, x1, z, col);
            }
        }
        return;
    }
    pt_fill(c, x0, y0, x1, y1, pt_side_colour(col, bs, side, 1));
}

/* whether the corner where sides s and a of a border meet is split along its diagonal,
   each side drawn on its own part of it, rather than drawn by one of them: both are drawn
   and they are unlike, in colour or in style; or both are double, groove or ridge (their
   lines mitred); or both inset or outset where their shades differ, at the top right and
   bottom left (Blink's ColorsMatchAtCorner and BorderStylesRequireMiter) */
static int pt_unlike(const double *ws, const int *sts, const uint32_t *cols, int s, int a) {
    if (ws[s] <= 0 || ws[a] <= 0 || sts[s] <= LAY_BS_HIDDEN || sts[a] <= LAY_BS_HIDDEN) return 0;
    if (cols[s] != cols[a] || sts[s] != sts[a]) return 1;
    int st = sts[s];
    if (st == LAY_BS_DOUBLE || st == LAY_BS_GROOVE || st == LAY_BS_RIDGE) return 1;
    if (st == LAY_BS_INSET || st == LAY_BS_OUTSET) {
        int f = 1 << s | 1 << a;
        return f == (1 << 0 | 1 << 1) || f == (1 << 2 | 1 << 3);
    }
    return 0;
}

/* whether side s's part of a corner it splits with side a is anti-aliased: when their
   colours differ there (theirs, or an inset, outset, groove or ridge side's shades at the
   top right and bottom left); else its edge is hard, a pixel its own when its centre is
   (Blink's soft and hard miters) */
static int pt_soft(const int *sts, const uint32_t *cols, int s, int a) {
    if (cols[s] != cols[a]) return 1;
    int st = sts[s];
    if (st != LAY_BS_INSET && st != LAY_BS_OUTSET && st != LAY_BS_GROOVE && st != LAY_BS_RIDGE) return 0;
    int f = 1 << s | 1 << a;
    return f == (1 << 0 | 1 << 1) || f == (1 << 2 | 1 << 3);
}

/* a side drawn by pt_side (its rectangle x0, y0 - x1, y1, its dashes laid from f0 to f1)
   on its own part only of the nk corners it splits with its neighbours: corner ks[q] (0
   top left to 3 bottom left), whose square is sq[4 ks[q]] to sq[4 ks[q] + 3], split along
   its diagonal (Blink clips the side to its polygon, ClipBorderSidePolygon), its edge
   anti-aliased when soft[q]. The side is drawn onto black and onto white, its colour and
   cover read from the two, and laid on the canvas through its part of each corner */
static void pt_side_split(amb_canvas *c, double x0, double y0, double x1, double y1, int side, int bs, uint32_t col,
                          double f0, double f1, const double *sq, const int *ks, const int *soft, int nk) {
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int ax = (int)floor(x0), ay = (int)floor(y0 - c->oy), zx = (int)ceil(x1), zy = (int)ceil(y1 - c->oy);
    if (ax < kx0) ax = kx0;
    if (ay < ky0) ay = ky0;
    if (zx > kx1) zx = kx1;
    if (zy > ky1) zy = ky1;
    if (zx <= ax || zy <= ay) return;
    amb_canvas k0, k1;
    memset(&k0, 0, sizeof k0);
    memset(&k1, 0, sizeof k1);
    defer(k0) { AMB_CANVAS_DROP(k0); }
    defer(k1) { AMB_CANVAS_DROP(k1); }
    k0.w = k1.w = (uint32_t)(zx - ax);
    k0.h = k1.h = (uint32_t)(zy - ay);
    k0.oy = k1.oy = c->oy + ay;
    k0.zoom = k1.zoom = c->zoom;
    k0.cx0 = k1.cx0 = 0;
    k0.cx1 = k1.cx1 = k0.w;
    k0.cy0 = k1.cy0 = k0.oy;
    k0.cy1 = k1.cy1 = k0.oy + k0.h;
    resize(&k0.px, (size_t)k0.w * k0.h * 3);
    resize(&k1.px, (size_t)k1.w * k1.h * 3);
    for (size_t q = 0; q < len(k1.px); q++) k1.px[q] = 255;
    /* the small canvases' columns start at ax */
    int horiz = side == 0 || side == 2;
    pt_side(&k0, x0 - ax, y0, x1 - ax, y1, side, bs, col, horiz ? f0 - ax : f0, horiz ? f1 - ax : f1);
    pt_side(&k1, x0 - ax, y0, x1 - ax, y1, side, bs, col, horiz ? f0 - ax : f0, horiz ? f1 - ax : f1);
    for (int y = 0; y < (int)k0.h; y++)
        for (int x = 0; x < (int)k0.w; x++) {
            size_t i = ((size_t)y * k0.w + (size_t)x) * 3;
            double a = 1 - (at(k1.px, i) - at(k0.px, i)) / 255.0;
            if (a <= 0.002) continue;
            double px = ax + x + 0.5, py = c->oy + ay + y + 0.5, m = 1;
            for (int q = 0; q < nk; q++) {
                const double *s = sq + 4 * ks[q];
                double cov = pt_sq_cover(px, py, s[0], s[1], s[2], s[3]);
                if (cov <= 0) continue;
                double fh = pt_miter_share(px, py, s[0], s[1], s[2], s[3], ks[q]), own = horiz ? fh : 1 - fh;
                if (soft[q]) m *= 1 - cov * (1 - own);
                else if (cov >= 0.5 && own < 0.5) m = 0;
            }
            if (m <= 0) continue;
            size_t di = ((size_t)(ay + y) * c->w + (size_t)(ax + x)) * 3;
            for (int ch = 0; ch < 3; ch++) {
                double v = at(k0.px, i + ch) * m + c->px[di + ch] * (1 - a * m);
                c->px[di + ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
        }
}

/* the four sides of a square-cornered border (its box bx, by, bw by bh; the sides' widths,
   styles and colours top, right, bottom, left), each in its style and from corner to
   corner: a corner its two sides share (pt_unlike) drawn by the top or bottom one, a
   corner they split drawn by each on its own part of it. A side's dashes are laid along
   its whole length either way */
static void pt_styled_sides(amb_canvas *c, double bx, double by, double bw, double bh, const double *ws, const int *sts,
                            const uint32_t *cols) {
    /* corner k (0 top left, 1 top right, 2 bottom right, 3 bottom left): its square, its
       horizontal side and its vertical one; a side's corners at its start and its end */
    double sq[16] = { bx, by, bx + ws[3], by + ws[0],
                      bx + bw - ws[1], by, bx + bw, by + ws[0],
                      bx + bw - ws[1], by + bh - ws[2], bx + bw, by + bh,
                      bx, by + bh - ws[2], bx + ws[3], by + bh };
    static const int hs[4] = { 0, 0, 2, 2 }, vs[4] = { 3, 1, 1, 3 }, c0[4] = { 0, 1, 3, 0 }, c1[4] = { 1, 2, 2, 3 };
    int split[4];
    for (int k = 0; k < 4; k++) split[k] = pt_unlike(ws, sts, cols, hs[k], vs[k]);
    for (int s = 0; s < 4; s++) {
        if (ws[s] <= 0 || sts[s] <= LAY_BS_HIDDEN) continue;
        double x0, y0, x1, y1, f0, f1;
        if (s == 0 || s == 2) {
            x0 = bx;
            x1 = bx + bw;
            y0 = s == 0 ? by : by + bh - ws[2];
            y1 = y0 + ws[s];
            f0 = bx;
            f1 = bx + bw;
        } else {
            x0 = s == 3 ? bx : bx + bw - ws[1];
            x1 = x0 + ws[s];
            y0 = split[c0[s]] ? by : by + ws[0];
            y1 = split[c1[s]] ? by + bh : by + bh - ws[2];
            f0 = by;
            f1 = by + bh;
        }
        int ks[2], soft[2], nk = 0;
        for (int e = 0; e < 2; e++) {
            int k = e ? c1[s] : c0[s];
            if (!split[k]) continue;
            ks[nk] = k;
            soft[nk++] = pt_soft(sts, cols, s, s == hs[k] ? vs[k] : hs[k]);
        }
        if (nk) pt_side_split(c, x0, y0, x1, y1, s, sts[s], cols[s], f0, f1, sq, ks, soft, nk);
        else pt_side(c, x0, y0, x1, y1, s, sts[s], cols[s], f0, f1);
    }
}

/* the dash pattern of a path len0 long in style bs, w thick, closed or not (Blink's
   SetupPaintDashPathEffect and SelectBestDashGap, its ratios as Chromium draws them: a dash
   twice as long as the line is thick and a gap as long, three times and twice under 3px):
   the dash (a dot's: 0, its round cap the dot), the period from one dash's start to the
   next's, their count */
static void pt_dashes(int bs, double w, double len0, int closed, double *dash, double *period, long *n) {
    double d = bs == LAY_BS_DOTTED ? w : w >= 3 ? 2 * w : 3 * w;
    double g = bs == LAY_BS_DOTTED ? w : w >= 3 ? w : 2 * w;
    *n = 1;
    if (bs == LAY_BS_DASHED && len0 <= 2 * d) { *dash = len0; *period = len0; return; }
    if (bs == LAY_BS_DASHED && len0 <= 2 * d + g) {
        double m = len0 / (2 * d + g);
        *dash = d * m;
        *period = (d + g) * m;
        *n = 2;
        return;
    }
    double avail = closed ? len0 : len0 + g;
    double mn = floor(avail / (d + g)), mx = mn + 1;
    double gmn = closed ? mn : mn - 1, gmx = closed ? mx : mx - 1;
    double g0 = gmn > 0 ? (len0 - mn * d) / gmn : 0, g1 = gmx > 0 ? (len0 - mx * d) / gmx : 0;
    double gap = g1 <= 0 || fabs(g0 - g) < fabs(g1 - g) ? g0 : g1;
    *n = (long)(gap == g0 ? mn : mx);
    if (*n < 1) { *n = 1; gap = 0; }
    *dash = bs == LAY_BS_DOTTED ? 0 : d;
    *period = d + gap;
}

/* the point at length s along a polyline (cum: each point's length from the first) and the
   unit normal there, to its left */
static void pt_along(const vector<double> *px, const vector<double> *py, const vector<double> *cum, double s, double *x, double *y,
                     double *nx, double *ny) {
    size_t n = len(*px), k = 1;
    while (k + 1 < n && at(*cum, k) < s) k++;
    double x0 = at(*px, k - 1), y0 = at(*py, k - 1), x1 = at(*px, k), y1 = at(*py, k);
    double sl = at(*cum, k) - at(*cum, k - 1), f = sl > 0 ? (s - at(*cum, k - 1)) / sl : 0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    *x = x0 + (x1 - x0) * f;
    *y = y0 + (y1 - y0) * f;
    double dx = x1 - x0, dy = y1 - y0, dl = sqrt(dx * dx + dy * dy);
    *nx = dl > 0 ? -dy / dl : 0;
    *ny = dl > 0 ? dx / dl : 0;
}

/* the length Skia's path measure gives an arc of an ellipse (radii rx, ry, angles a to b):
   cut in halves until each half's middle lies within half a px (in x and in y) of its
   chord's, the chords' lengths summed -- shorter than the arc, as the dashes Chromium lays
   along it are */
static double pt_skia_arc(double rx, double ry, double a, double b, int depth) {
    double xa = rx * cos(a), ya = ry * sin(a), xb = rx * cos(b), yb = ry * sin(b);
    double m = (a + b) / 2, xm = rx * cos(m), ym = ry * sin(m);
    if (depth < 10 && (fabs(xm - (xa + xb) / 2) > 0.5 || fabs(ym - (ya + yb) / 2) > 0.5))
        return pt_skia_arc(rx, ry, a, m, depth + 1) + pt_skia_arc(rx, ry, m, b, depth + 1);
    return sqrt((xb - xa) * (xb - xa) + (yb - ya) * (yb - ya));
}

/* a quarter of an ellipse into a polyline: centre (cx, cy), radii (rx, ry), from angle t0
   on, sixteen segments, each point's length along the line (cum) the arc's as Skia
   measures it, shared out by angle */
static void pt_arc(vector<double> *px, vector<double> *py, vector<double> *cum, double cx, double cy, double rx, double ry, double t0) {
    double arc = pt_skia_arc(rx, ry, t0, t0 + 0.5 * M_PI, 0), s0 = back(*cum);
    for (int i = 1; i <= 16; i++) {
        double th = t0 + 0.5 * M_PI * i / 16.0;
        push_back(px, cx + rx * cos(th));
        push_back(py, cy + ry * sin(th));
        push_back(cum, s0 + arc * i / 16.0);
    }
}
/* a straight segment to (x, y), its length along the line */
static void pt_line_to(vector<double> *px, vector<double> *py, vector<double> *cum, double x, double y) {
    double dx = x - back(*px), dy = y - back(*py);
    push_back(px, x);
    push_back(py, y);
    push_back(cum, back(*cum) + sqrt(dx * dx + dy * dy));
}

/* a polyline (px, py; cum each point's length along it) in dashes or dots, w thick, in col:
   the pattern laid along its length from its start (closed: a closed path's pattern); a
   dash the band w wide about its stretch, cut square, a dot a circle w across at its
   period's start (Blink's dots: dashes of no length with round caps) */
static void pt_dash_path(amb_canvas *c, const vector<double> *px, const vector<double> *py, const vector<double> *cum, int bs, double w,
                         uint32_t col, int closed, vector<float> *acc) {
    double total = back(*cum);
    if (total <= 0) return;
    double dash, period;
    long n;
    pt_dashes(bs, w, total, closed, &dash, &period, &n);
    vector<double> xs, ys, ss, rx, ry;
    for (long q = 0; q < n; q++) {
        double a = q * period, z = a + dash;
        if (z > total) z = total;
        clear(xs);
        clear(ys);
        double x, y, nx, ny;
        if (bs == LAY_BS_DOTTED) {
            pt_along(px, py, cum, a, &x, &y, &nx, &ny);
            for (int k = 0; k < 24; k++) {
                double th = 2 * M_PI * k / 24;
                push_back(xs, x + w / 2 * cos(th));
                push_back(ys, y + w / 2 * sin(th));
            }
        } else {
            /* the stretch's points (its ends, the line's points between), the band's left
               edge along them forward, its right edge back */
            clear(ss);
            clear(rx);
            clear(ry);
            push_back(ss, a);
            for (size_t i = 1; i + 1 < len(*cum); i++) if (at(*cum, i) > a && at(*cum, i) < z) push_back(ss, at(*cum, i));
            push_back(ss, z);
            for (size_t k = 0; k < len(ss); k++) {
                pt_along(px, py, cum, at(ss, k), &x, &y, &nx, &ny);
                push_back(xs, x + nx * w / 2);
                push_back(ys, y + ny * w / 2);
                push_back(rx, x - nx * w / 2);
                push_back(ry, y - ny * w / 2);
            }
            for (size_t k = len(rx); k-- > 0;) { push_back(xs, at(rx, k)); push_back(ys, at(ry, k)); }
        }
        pt_poly(c, &xs, &ys, col, acc);
    }
}

/* a rounded border in dashes or dots (Blink's DrawDashedOrDottedBoxSideFromPath), its
   centre line's rectangle cl (x0 y0 x1 y1) and radii cr. All four sides alike: the whole
   centre line stroked as one closed path, as Blink's Path::AddRoundedRect makes it -- from
   the left end of the top edge, clockwise. A side alone (side 0 to 3): its part of the
   line, from the middle of the corner it starts at to the middle of the one it ends at */
static void pt_round_side(amb_canvas *c, int side, int bs, uint32_t col, double w, const double *cl, const double *cr, vector<float> *acc) {
    double cx[4] = { cl[0] + cr[0], cl[2] - cr[1], cl[2] - cr[2], cl[0] + cr[3] };
    double cy[4] = { cl[1] + cr[4], cl[1] + cr[5], cl[3] - cr[6], cl[3] - cr[7] };
    vector<double> px, py, cum;
    if (side < 0) {
        push_back(px, cl[0] + cr[0]);
        push_back(py, cl[1]);
        push_back(cum, 0.0);
        pt_line_to(&px, &py, &cum, cl[2] - cr[1], cl[1]);
        pt_arc(&px, &py, &cum, cx[1], cy[1], cr[1], cr[5], 1.5 * M_PI);
        pt_line_to(&px, &py, &cum, cl[2], cl[3] - cr[6]);
        pt_arc(&px, &py, &cum, cx[2], cy[2], cr[2], cr[6], 0);
        pt_line_to(&px, &py, &cum, cl[0] + cr[3], cl[3]);
        pt_arc(&px, &py, &cum, cx[3], cy[3], cr[3], cr[7], 0.5 * M_PI);
        pt_line_to(&px, &py, &cum, cl[0], cl[1] + cr[4]);
        pt_arc(&px, &py, &cum, cx[0], cy[0], cr[0], cr[4], M_PI);
        pt_dash_path(c, &px, &py, &cum, bs, w, col, 1, acc);
        return;
    }
    int ca = side, cb = (side + 1) % 4;
    double ta = (1.25 + 0.5 * side) * M_PI, tm = ta + 0.25 * M_PI;
    for (int i = 0; i <= 16; i++) {
        int k = i <= 8 ? ca : cb;
        double th = i <= 8 ? ta + (tm - ta) * i / 8.0 : tm + 0.25 * M_PI * (i - 8) / 8.0;
        push_back(px, cx[k] + cr[k] * cos(th));
        push_back(py, cy[k] + cr[k + 4] * sin(th));
        if (i == 8) {
            push_back(px, cx[cb] + cr[cb] * cos(tm));
            push_back(py, cy[cb] + cr[cb + 4] * sin(tm));
        }
    }
    push_back(cum, 0.0);
    for (size_t k = 1; k < len(px); k++) {
        double dx = at(px, k) - at(px, k - 1), dy = at(py, k) - at(py, k - 1);
        push_back(cum, at(cum, k - 1) + sqrt(dx * dx + dy * dy));
    }
    pt_dash_path(c, &px, &py, &cum, bs, w, col, 0, acc);
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

/* the colour at place t along the stops (premultiplied in; RGBA out). At a stop's place
   the stretch that starts there is taken, as Skia takes it: a hard stop's later colour */
static uint32_t pt_stop_at(const vector<pt_stop> *sv, double t) {
    size_t m = len(*sv);
    pt_stop a = at(*sv, 0), b = a;
    if (t < a.pos) b = a;
    else if (t >= at(*sv, m - 1).pos) { a = at(*sv, m - 1); b = a; }
    else for (size_t k = 1; k < m; k++) if (t < at(*sv, k).pos) { a = at(*sv, k - 1); b = at(*sv, k); break; }
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

/* three box blurs of a radius close to sigma's, across and down (a Gaussian's likeness),
   of the w by h plane of m from off */
static void pt_blur_at(vector<float> *m, size_t off, int w, int h, double sigma) {
    if (sigma < 0.5) return;
    int rr = (int)floor(sigma * 0.94 + 0.5);
    if (rr < 1) rr = 1;
    vector<float> tmp;
    resize(&tmp, (size_t)(w > h ? w : h));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            double sum = 0;
            for (int x = -rr; x <= rr; x++) if (x >= 0 && x < w) sum += at(*m, off + (size_t)y * w + x);
            for (int x = 0; x < w; x++) {
                tmp[(size_t)x] = (float)(sum / (2 * rr + 1));
                int out = x - rr, in = x + rr + 1;
                if (out >= 0) sum -= at(*m, off + (size_t)y * w + out);
                if (in < w) sum += at(*m, off + (size_t)y * w + in);
            }
            for (int x = 0; x < w; x++) (*m)[off + (size_t)y * w + x] = at(tmp, (size_t)x);
        }
        for (int x = 0; x < w; x++) {
            double sum = 0;
            for (int y = -rr; y <= rr; y++) if (y >= 0 && y < h) sum += at(*m, off + (size_t)y * w + x);
            for (int y = 0; y < h; y++) {
                tmp[(size_t)y] = (float)(sum / (2 * rr + 1));
                int out = y - rr, in = y + rr + 1;
                if (out >= 0) sum -= at(*m, off + (size_t)out * w + x);
                if (in < h) sum += at(*m, off + (size_t)in * w + x);
            }
            for (int y = 0; y < h; y++) (*m)[off + (size_t)y * w + x] = at(tmp, (size_t)y);
        }
    }
}
static void pt_blur(vector<float> *m, int w, int h, double sigma) { pt_blur_at(m, 0, w, h, sigma); }

/* ---- filters (Filter Effects 1): a drawing through filter's functions ---- */

enum { PF_BLUR, PF_BRIGHTNESS, PF_CONTRAST, PF_GRAYSCALE, PF_HUE, PF_INVERT, PF_OPACITY, PF_SATURATE, PF_SEPIA, PF_DROP };
/* a function: its kind; its amount (a number, a percentage's fraction; hue-rotate's
   angle in degrees); a blur's deviation (a drop shadow's too); a shadow's offset and
   colour */
typedef struct pt_fop { int kind; double a, x, y, blur; uint32_t col; } pt_fop;

static int pt_is_ci(const char *s, size_t n, const char *w) {
    if (n != strlen(w)) return 0;
    for (size_t i = 0; i < n; i++) if ((s[i] | 0x20) != w[i] && s[i] != w[i]) return 0;
    return 1;
}

/* the functions of a filter's computed value s into ops, in turn; 0 when one names an SVG
   filter by url(), which is not drawn (the whole value then ignored, as the standard
   has one that names no filter) */
static int pt_filter_ops(const char *s, size_t n, vector<pt_fop> *ops) {
    size_t i = 0, pa, pl;
    while (css_next_part(s, n, &i, &pa, &pl)) {
        const char *fn = s + pa;
        size_t open = 0;
        while (open < pl && fn[open] != '(') open++;
        if (open >= pl || fn[pl - 1] != ')') continue;
        const char *args = fn + open + 1;
        size_t al = pl - open - 2, j = 0, xa, xl;
        pt_fop op;
        memset(&op, 0, sizeof op);
        op.a = 1;
        if (pt_is_ci(fn, open, "url")) return 0;
        if (pt_is_ci(fn, open, "drop-shadow")) {
            op.kind = PF_DROP;
            op.col = 0x000000ff;
            double v[3] = { 0, 0, 0 };
            int nv = 0;
            while (css_next_part(args, al, &j, &xa, &xl)) {
                uint32_t cc;
                double x;
                if (nv < 3 && css_length_eval(args + xa, xl, 0, &x)) v[nv++] = x;
                else if (css_color_parse(args + xa, xl, &cc)) op.col = cc;
            }
            op.x = v[0];
            op.y = v[1];
            op.blur = v[2] > 0 ? v[2] : 0;
            push_back(*ops, op);
            continue;
        }
        /* the one argument: a number, a percentage, a length (px) or an angle */
        int have = css_next_part(args, al, &j, &xa, &xl);
        double x = have ? strtod(args + xa, 0) : 0;
        if (have && xl > 0) {
            const char *t0 = args + xa;
            if (t0[xl - 1] == '%') x /= 100;
            else if (xl > 4 && pt_is_ci(t0 + xl - 4, 4, "turn")) x *= 360;
            else if (xl > 4 && pt_is_ci(t0 + xl - 4, 4, "grad")) x *= 0.9;
            else if (xl > 3 && pt_is_ci(t0 + xl - 3, 3, "rad")) x *= 180 / M_PI;
        }
        if (pt_is_ci(fn, open, "blur")) { op.kind = PF_BLUR; op.blur = have ? x : 0; }
        else if (pt_is_ci(fn, open, "hue-rotate")) { op.kind = PF_HUE; op.a = have ? x : 0; }
        else {
            static const char *const names[7] = { "brightness", "contrast", "grayscale", "invert", "opacity", "saturate", "sepia" };
            static const int kinds[7] = { PF_BRIGHTNESS, PF_CONTRAST, PF_GRAYSCALE, PF_INVERT, PF_OPACITY, PF_SATURATE, PF_SEPIA };
            int k = 0;
            while (k < 7 && !pt_is_ci(fn, open, names[k])) k++;
            if (k == 7) continue;
            op.kind = kinds[k];
            op.a = have ? x : 1;
            /* the four whose amount is a share, at most all of it */
            if ((op.kind == PF_GRAYSCALE || op.kind == PF_INVERT || op.kind == PF_OPACITY || op.kind == PF_SEPIA) && op.a > 1) op.a = 1;
        }
        push_back(*ops, op);
    }
    return 1;
}

/* the w by h pixels px (four planes: red, green, blue, alpha, the colours premultiplied,
   each 0 to 1) through the functions ops in turn, in sRGB, as Chromium applies them: the
   colour ones Blink's matrices and transfer functions on the colours unpremultiplied,
   each clamped; blur() a Gaussian of its deviation; drop-shadow() the alpha moved by its
   offset, blurred, in its colour, under the rest. A drop shadow's third length is its
   blur's deviation (as the standard has it, and Chromium draws it: not half of it, as a
   box-shadow's is) */
static void pt_filter_apply(vector<float> *px, int w, int h, const vector<pt_fop> *ops) {
    size_t np = (size_t)w * h;
    for (size_t k = 0; k < len(*ops); k++) {
        pt_fop op = at(*ops, k);
        if (op.kind == PF_BLUR) {
            for (size_t ch = 0; ch < 4; ch++) pt_blur_at(px, ch * np, w, h, op.blur);
            continue;
        }
        if (op.kind == PF_DROP) {
            vector<float> sh;
            resize(&sh, np);
            int dx = (int)floor(op.x + 0.5), dy = (int)floor(op.y + 0.5);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int sx = x - dx, sy = y - dy;
                    sh[(size_t)y * w + x] = sx >= 0 && sy >= 0 && sx < w && sy < h ? at(*px, 3 * np + (size_t)sy * w + sx) : 0.0f;
                }
            pt_blur_at(&sh, 0, w, h, op.blur);
            double ca = (op.col & 0xff) / 255.0, cr = (op.col >> 24) / 255.0, cg = (op.col >> 16 & 0xff) / 255.0, cb = (op.col >> 8 & 0xff) / 255.0;
            for (size_t q = 0; q < np; q++) {
                double s = at(sh, q) * ca, a = at(*px, 3 * np + q), under = s * (1 - a);
                if (under <= 0) continue;
                (*px)[q] = (float)(at(*px, q) + cr * under);
                (*px)[np + q] = (float)(at(*px, np + q) + cg * under);
                (*px)[2 * np + q] = (float)(at(*px, 2 * np + q) + cb * under);
                (*px)[3 * np + q] = (float)(a + under);
            }
            continue;
        }
        if (op.kind == PF_OPACITY) {
            for (size_t q = 0; q < 4 * np; q++) (*px)[q] = (float)(at(*px, q) * op.a);
            continue;
        }
        /* a colour matrix, rows red, green, blue: three factors and an offset each */
        double m[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        double a = op.a, o = 1 - a;
        if (op.kind == PF_BRIGHTNESS) { m[0] = m[5] = m[10] = a; }
        else if (op.kind == PF_CONTRAST) { m[0] = m[5] = m[10] = a; m[3] = m[7] = m[11] = 0.5 - 0.5 * a; }
        else if (op.kind == PF_INVERT) { m[0] = m[5] = m[10] = 1 - 2 * a; m[3] = m[7] = m[11] = a; }
        else if (op.kind == PF_GRAYSCALE) {
            double g[12] = { 0.2126 + 0.7874 * o, 0.7152 - 0.7152 * o, 0.0722 - 0.0722 * o, 0,
                             0.2126 - 0.2126 * o, 0.7152 + 0.2848 * o, 0.0722 - 0.0722 * o, 0,
                             0.2126 - 0.2126 * o, 0.7152 - 0.7152 * o, 0.0722 + 0.9278 * o, 0 };
            memcpy(m, g, sizeof m);
        } else if (op.kind == PF_SEPIA) {
            double g[12] = { 0.393 + 0.607 * o, 0.769 - 0.769 * o, 0.189 - 0.189 * o, 0,
                             0.349 - 0.349 * o, 0.686 + 0.314 * o, 0.168 - 0.168 * o, 0,
                             0.272 - 0.272 * o, 0.534 - 0.534 * o, 0.131 + 0.869 * o, 0 };
            memcpy(m, g, sizeof m);
        } else if (op.kind == PF_SATURATE) {
            double g[12] = { 0.213 + 0.787 * a, 0.715 - 0.715 * a, 0.072 - 0.072 * a, 0,
                             0.213 - 0.213 * a, 0.715 + 0.285 * a, 0.072 - 0.072 * a, 0,
                             0.213 - 0.213 * a, 0.715 - 0.715 * a, 0.072 + 0.928 * a, 0 };
            memcpy(m, g, sizeof m);
        } else if (op.kind == PF_HUE) {
            double cs = cos(a * M_PI / 180), sn = sin(a * M_PI / 180);
            double g[12] = { 0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928, 0,
                             0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283, 0,
                             0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072, 0 };
            memcpy(m, g, sizeof m);
        }
        for (size_t q = 0; q < np; q++) {
            double al = at(*px, 3 * np + q);
            if (al <= 0) continue;
            double r = at(*px, q) / al, g = at(*px, np + q) / al, b = at(*px, 2 * np + q) / al;
            for (int row = 0; row < 3; row++) {
                double v = m[4 * row] * r + m[4 * row + 1] * g + m[4 * row + 2] * b + m[4 * row + 3];
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                (*px)[(size_t)row * np + q] = (float)(v * al);
            }
        }
    }
}

/* how far the functions ops reach past what they are given, in px */
static int pt_filter_reach(const vector<pt_fop> *ops) {
    double r = 0;
    for (size_t k = 0; k < len(*ops); k++) {
        pt_fop op = at(*ops, k);
        if (op.kind == PF_BLUR) r += 3 * op.blur + 2;
        else if (op.kind == PF_DROP) r += fabs(op.x) + fabs(op.y) + 3 * op.blur + 2;
    }
    return (int)ceil(r);
}

/* a box's shadows (box-shadow's value s), each, last first. The outer ones (inset 0): its
   rounded border box offset and spread, blurred, outside the box only. The inset ones
   (inset 1, drawn over the background): inside the padding box (bw: the border's widths,
   top right bottom left; prad the padding box's radii), all of it but a hole -- the padding
   box shrunk by the spread and moved by the offset -- blurred */
static void pt_box_shadows(amb_canvas *c, const char *s, size_t n, double x0, double y0, double x1, double y1, const double *rad,
                           int want_inset, const double *bw, const double *prad) {
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
        if (inset != want_inset || nv < 2 || (col & 0xff) == 0) continue;
        double blur = v[2] > 0 ? v[2] : 0, spread = v[3];
        if (inset) {
            double px0 = x0 + bw[3], py0 = y0 + bw[0], px1 = x1 - bw[1], py1 = y1 - bw[2];
            if (px1 <= px0 || py1 <= py0) continue;
            double hx0 = px0 + v[0] + spread, hy0 = py0 + v[1] + spread, hx1 = px1 + v[0] - spread, hy1 = py1 + v[1] - spread;
            double hr[8];
            for (int k = 0; k < 8; k++) hr[k] = prad[k] > 0 ? fmax(0, prad[k] - spread) : 0;
            int margin = (int)ceil(blur) + 2;
            int mx0 = (int)floor(px0) - margin, my0 = (int)floor(py0) - margin;
            int mw = (int)ceil(px1) + margin - mx0, mh = (int)ceil(py1) + margin - my0;
            if (mw <= 0 || mh <= 0 || (double)mw * mh > 4e7) continue;
            vector<float> m;
            resize(&m, (size_t)mw * mh);
            int hole = hx1 > hx0 && hy1 > hy0;
            for (int y = 0; y < mh; y++)
                for (int x = 0; x < mw; x++)
                    m[(size_t)y * mw + x] = (float)(hole ? 1 - pt_rr_cover(mx0 + x + 0.5, my0 + y + 0.5, hx0, hy0, hx1, hy1, hr) : 1);
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
                    double inside = pt_rr_cover(X + 0.5, my0 + y + 0.5, px0, py0, px1, py1, prad);
                    if (inside > 0) pt_blend(c, X, Y, col, cov * inside);
                }
            }
            continue;
        }
        double sx0 = x0 + v[0] - spread, sy0 = y0 + v[1] - spread, sx1 = x1 + v[0] + spread, sy1 = y1 + v[1] + spread;
        double sr[8];
        for (int k = 0; k < 8; k++) sr[k] = rad[k] > 0 ? fmax(0, rad[k] + spread) : 0;
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

/* the broken-image icon Chromium draws (its IDR_BROKENIMAGE resource, BSD-licensed, as
   its screenshots show it at one device px a CSS px), each pixel 0xRRGGBBAA */
static const uint32_t pt_broken_icon[256] = {
    0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff,
    0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0x9c9c9c84, 0x00000006, 0x00000000, 0x00000000, 0x00000000,
    0xa2a2a3ff, 0xdee1e7ff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff,
    0xf1f6feff, 0xf1f6fdff, 0xb8babcff, 0x9c9c9cff, 0x9e9e9ea3, 0x00000007, 0x00000000, 0x00000000,
    0xa1a1a2ff, 0xd3dae6ff, 0xdde8fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff,
    0xdae6fbff, 0xdee9faff, 0xb1b5bbff, 0xcacacaff, 0xdbdbdcff, 0x9f9fa0c1, 0x00000008, 0x00000000,
    0xa0a0a0ff, 0xcdd6e5ff, 0xd3e2faff, 0xcedef9ff, 0xe0eafbff, 0xffffffff, 0xf8fafeff, 0xcedef9ff,
    0xcedef9ff, 0xd5e1f8ff, 0xaeb2baff, 0xcacacaff, 0xffffffff, 0xe7e7e7ff, 0x9f9f9fe0, 0x0000000a,
    0x9e9e9fff, 0xcbd5e5ff, 0xd2e1f9ff, 0xe6eefbff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xd3e1f8ff, 0xd4e1f8ff, 0xabafb7ff, 0x919191ff, 0x919191ff, 0x919191ff, 0x929292ff, 0x9e9e9fff,
    0x9d9d9eff, 0xcbd5e5ff, 0xf1f6feff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xd1dff8ff, 0xcddbf4ff, 0xdce6f8ff, 0xe9f0fbff, 0xeef3fcff, 0xeff4fcff, 0xe8ebf2ff, 0x9d9d9eff,
    0x9b9b9cff, 0xcbd4e4ff, 0xcfdef8ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff,
    0xc9daf5ff, 0xcbdbf5ff, 0xcfdef6ff, 0xd3e1f7ff, 0xd5e2f7ff, 0xd8e4f8ff, 0xdae1f0ff, 0x9b9b9cff,
    0x9a9a9bff, 0xc9d3e3ff, 0xcdddf7ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff,
    0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xcadbf6ff, 0xd1dcefff, 0x9a9a9bff,
    0x989899ff, 0xc8d2e3ff, 0xccdcf6ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff,
    0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc9daf5ff, 0xd8e6fcdf, 0x00000000,
    0x969697ff, 0xc7d1e2ff, 0xcadaf6ff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc2d4f2ff, 0xbfd3edff, 0xb1ced4ff,
    0xb3ced8ff, 0xbfd2ecff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc3d4f3bf, 0x00000000, 0x00000000,
    0x959596ff, 0xc6d1e2ff, 0xc8d8f4ff, 0xc0d1f0ff, 0xb3ccdaff, 0x8cbe97ff, 0x56a938ff, 0x58ae39ff,
    0x58ae39ff, 0x67b155ff, 0x98c2aaff, 0xb9cee4ff, 0xc0d0f19f, 0x00000000, 0x00000000, 0x00000000,
    0x939394ff, 0xc5cfe2ff, 0xc7d7f4ff, 0xa7c8c8ff, 0x7bb979ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff,
    0x58ae39ff, 0x58ae39ff, 0x5bae3eff, 0x5caf427f, 0x00000000, 0x00000000, 0x9494941f, 0x939394ff,
    0x929293ff, 0xc5cfe1ff, 0xb2d0cfff, 0x7ab977ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff,
    0x58ae39ff, 0x58ae39ff, 0x59ae385f, 0x00000000, 0x00000000, 0xd7e7ff3f, 0xcdd9eeff, 0x929293ff,
    0x909092ff, 0xb9c9d1ff, 0x86c082ff, 0x5baf3fff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff,
    0x58ae39ff, 0x59ae393f, 0x00000000, 0x00000000, 0x6eb4635f, 0x87bf88ff, 0xcbd8edff, 0x909092ff,
    0x8f8f90ff, 0x81b279ff, 0x65b44eff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff,
    0x5aad3a1f, 0x00000000, 0x00000000, 0x58af387f, 0x58ae39ff, 0x65b34dff, 0x8dbe8cff, 0x8d908eff,
    0x868687ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x828284df, 0x00000000,
    0x00000000, 0x00000000, 0x8282849f, 0x838384ff, 0x838384ff, 0x838384ff, 0x838484ff, 0x8a8b8bff,
};

/* an image whose load failed, in its content box at x, y: as Chromium's fallback draws
   one with no alt text (its shadow tree's container: a 1px silver border, 1px padding,
   the 16px icon inside, overflow hidden), when the box is 18 CSS px or more each way;
   else nothing. An image whose bytes were fetched but do not decode draws nothing, as in
   Chromium */
/* a broken image's 16px icon, at (x, y) */
static void pt_broken_icon_at(amb_canvas *c, double x, double y) {
    double z = c->zoom > 0 ? c->zoom : 1;
    vector<uint8_t> px;
    for (int k = 0; k < 256; k++) {
        uint32_t v = pt_broken_icon[k];
        push_back(px, (uint8_t)(v >> 24));
        push_back(px, (uint8_t)(v >> 16));
        push_back(px, (uint8_t)(v >> 8));
        push_back(px, (uint8_t)v);
    }
    pt_image(c, &px, 16, 16, x, y, 16 * z, 16 * z);
}
static void pt_broken(amb_canvas *c, double x, double y, double w, double h) {
    double z = c->zoom > 0 ? c->zoom : 1;
    /* the icon's own box (no size declared, no alt): the icon alone, as Chromium draws it */
    if (w >= 16 * z - 0.01 && h >= 16 * z - 0.01 && (w < 18 * z - 0.01 || h < 18 * z - 0.01)) { pt_broken_icon_at(c, x, y); return; }
    if (w < 18 * z - 0.01 || h < 18 * z - 0.01) return;
    uint32_t edge = 0xc0c0c0ff;
    pt_fill(c, x, y, x + w, y + z, edge);
    pt_fill(c, x, y + h - z, x + w, y + h, edge);
    pt_fill(c, x, y + z, x + z, y + h - z, edge);
    pt_fill(c, x + w - z, y + z, x + w, y + h - z, edge);
    vector<uint8_t> px;
    for (int k = 0; k < 256; k++) {
        uint32_t v = pt_broken_icon[k];
        push_back(px, (uint8_t)(v >> 24));
        push_back(px, (uint8_t)(v >> 16));
        push_back(px, (uint8_t)(v >> 8));
        push_back(px, (uint8_t)v);
    }
    double cx0 = c->cx0, cy0 = c->cy0, cx1 = c->cx1, cy1 = c->cy1;
    if (c->cx0 < x + z) c->cx0 = x + z;
    if (c->cy0 < y + z) c->cy0 = y + z;
    if (c->cx1 > x + w - z) c->cx1 = x + w - z;
    if (c->cy1 > y + h - z) c->cy1 = y + h - z;
    pt_image(c, &px, 16, 16, x + 2 * z, y + 2 * z, 16 * z, 16 * z);
    c->cx0 = cx0;
    c->cy0 = cy0;
    c->cx1 = cx1;
    c->cy1 = cy1;
}

/* an img with no image to load and no alt text, in its content box at x, y: Chromium's ImagePainter outlines the
   box, snapped to whole CSS px, 1 CSS px wide inside it in light grey (Color::kLightGray), when it is more than
   2 CSS px each way; nothing else (measured zoomed too: the snapping, the width and the 2px in CSS px) */
static void pt_unsourced(amb_canvas *c, double x, double y, double w, double h) {
    double z = c->zoom > 0 ? c->zoom : 1;
    if (w / z <= 2 || h / z <= 2) return;
    double x0 = floor(x / z + 0.5) * z, y0 = floor(y / z + 0.5) * z, x1 = floor((x + w) / z + 0.5) * z, y1 = floor((y + h) / z + 0.5) * z;
    if (x1 - x0 < z || y1 - y0 < z) return;
    uint32_t grey = 0xc0c0c0ff;
    pt_fill(c, x0, y0, x1, y0 + z, grey);
    if (y1 - y0 > z) pt_fill(c, x0, y1 - z, x1, y1, grey);
    if (y1 - y0 > 2 * z) {
        pt_fill(c, x0, y0 + z, x0 + z, y1 - z, grey);
        if (x1 - x0 > z) pt_fill(c, x1 - z, y0 + z, x1, y1 - z, grey);
    }
}

/* the pixels [x0, x1) x [y0, y1) of the canvas as they are, before replaced content is drawn over them */
static void pt_keep(const amb_canvas *c, int x0, int y0, int x1, int y1, vector<uint8_t> *out) {
    clear(*out);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            push_back(*out, c->px[i]);
            push_back(*out, c->px[i + 1]);
            push_back(*out, c->px[i + 2]);
        }
}
/* what was drawn over the pixels pt_keep kept, held to the rounded rectangle (x0, y0)-(x1, y1) with radii r (page
   coordinates): each pixel moved from what it was by as much of it as the rectangle covers, as an anti-aliased
   clip lets a drawing through */
static void pt_round_clip(amb_canvas *c, int kx0, int ky0, int kx1, int ky1, const vector<uint8_t> *was, double x0, double y0, double x1,
                          double y1, const double *r) {
    size_t k = 0;
    for (int y = ky0; y < ky1; y++)
        for (int x = kx0; x < kx1; x++, k += 3) {
            double cov = pt_rr_cover(x + 0.5, y + 0.5 + c->oy, x0, y0, x1, y1, r);
            if (cov >= 1) continue;
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            for (int ch = 0; ch < 3; ch++) {
                double v0 = at(*was, k + (size_t)ch);
                c->px[i + (size_t)ch] = (uint8_t)(v0 + (c->px[i + (size_t)ch] - v0) * cov + 0.5);
            }
        }
}

/* the image the loader fetched for an element, decoded into rgba: 1 when it was; -1 when
   its load failed; 0 when there is none or its bytes are no image this decodes */
static int pt_image_of(const net_loader *nt, uint32_t el, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h, double *nw, double *nh) {
    *nw = *nh = 0;
    if (!nt) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++) {
        if (!nt->resource[r].live || nt->resource[r].elem.index != el || nt->resource[r].kind != NET_KIND_IMAGE) continue;
        if (nt->resource[r].outcome != NET_OUTCOME_OK) return -1;
        amb_span b = nt->resource[r].body;
        vector<uint8_t> bytes;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
        int got = amb_image_decode(data(bytes), len(bytes), rgba, w, h);
        /* its natural size in CSS px: the loader's (a srcset density's share), else its pixels */
        *nw = nt->resource[r].width > 0 ? nt->resource[r].width : *w;
        *nh = nt->resource[r].height > 0 ? nt->resource[r].height : *h;
        return got;
    }
    return 0;
}

/* one axis of object-position's text (a percentage, px, or a calc() of both) as px: its percentage of room (the
   box less the image, below 0 when the image is the larger), its px at the zoom z */
static double pt_opos_axis(const char *s, size_t n, double room, double z, double dflt) {
    double v0, v100;
    if (!css_length_eval(s, n, 0, &v0) || !css_length_eval(s, n, 100, &v100)) return dflt;
    return v0 * z + (v100 - v0) * room / 100;
}
/* a replaced element's image in its content box, as object-fit and object-position place it (CSS Images 3, 5.5):
   its natural size nw x nh (CSS px) filling the box (fill), fitted in it (contain), covering it (cover), kept (none),
   or the smaller of none and contain (scale-down); at object-position. Snapped to whole pixels and clipped to the
   box, as Blink's ImagePainter draws it (a destination larger than the box drawn from the part of the source the
   box shows) */
static void pt_fit_image(amb_canvas *c, const layout_tree *t, uint32_t b, const vector<uint8_t> *rgba, uint32_t iw, uint32_t ih, double nw,
                         double nh) {
    double x = t->box[b].x, y = t->box[b].y, w = t->box[b].w, h = t->box[b].h;
    int fit = t->box[b].ofit;
    if (fit == 0 || nw <= 0 || nh <= 0) {
        /* filling its box: the box snapped to whole pixels, the image mapped across what it covers (Blink's
           ImagePainter: ToPixelSnappedRect), so an image at a fractional x is drawn crisp */
        double zs = c->zoom > 0 ? c->zoom : 1;       /* in CSS px, then scaled, as Chromium snaps under a zoom */
        double sx0 = floor(x / zs + 0.5) * zs, sy0 = floor(y / zs + 0.5) * zs, sx1 = floor((x + w) / zs + 0.5) * zs, sy1 = floor((y + h) / zs + 0.5) * zs;
        pt_image_ex(c, rgba, iw, ih, sx0, sy0, sx1 - sx0, sy1 - sy0, 1);
        return;
    }
    double z = c->zoom > 0 ? c->zoom : 1, dw = nw * z, dh = nh * z;
    if (fit == 1 || fit == 2 || (fit == 4 && (dw > w || dh > h))) {
        double s = fit == 2 ? fmax(w / dw, h / dh) : fmin(w / dw, h / dh);
        dw *= s;
        dh *= s;
    }
    double ox = (w - dw) / 2, oy = (h - dh) / 2;
    if (t->box[b].opos.len) {
        vector<char> op;
        append_range(op, text_ptr(t, t->box[b].opos), t->box[b].opos.len);
        size_t st[2], ln[2], i = 0, k = 0, a, n;
        while (k < 2 && css_next_part(data(op), len(op), &i, &a, &n)) { st[k] = a; ln[k] = n; k++; }
        if (k == 2) {
            ox = pt_opos_axis(data(op) + st[0], ln[0], w - dw, z, ox);
            oy = pt_opos_axis(data(op) + st[1], ln[1], h - dh, z, oy);
        }
    }
    double dx0 = floor((x + ox) / z + 0.5) * z, dy0 = floor((y + oy) / z + 0.5) * z, dx1 = floor((x + ox + dw) / z + 0.5) * z,
           dy1 = floor((y + oy + dh) / z + 0.5) * z;
    double cx0 = c->cx0, cy0 = c->cy0, cx1 = c->cx1, cy1 = c->cy1;
    double bx0 = floor(x / z + 0.5) * z, by0 = floor(y / z + 0.5) * z, bx1 = floor((x + w) / z + 0.5) * z, by1 = floor((y + h) / z + 0.5) * z;
    if (c->cx0 < bx0) c->cx0 = bx0;
    if (c->cy0 < by0) c->cy0 = by0;
    if (c->cx1 > bx1) c->cx1 = bx1;
    if (c->cy1 > by1) c->cy1 = by1;
    pt_image_ex(c, rgba, iw, ih, dx0, dy0, dx1 - dx0, dy1 - dy0, 1);
    c->cx0 = cx0;
    c->cy0 = cy0;
    c->cx1 = cx1;
    c->cy1 = cy1;
}

/* an image a box's background names (resource r), decoded once a paint (any format the
   painter reads, an animation at the frame it shows at rest): its pixels' place in the
   cache's pixels, its size; 0 when it cannot be */
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
        if (amb_image_decode(data(bytes), len(bytes), &rgba, w, h)) append_range(ipx, data(rgba), len(rgba));
        else { *w = 0; *h = 0; }
    }
    push_back(icache, r);
    push_back(icache, (uint32_t)*off);
    push_back(icache, *w);
    push_back(icache, *h);
    return *w > 0;
}

/* a box's background layers (background-image's value, last first): each tile placed by
   background-size, -position and -repeat in its padding box, drawn in its rounded border box;
   those clipped to the text alone (text_only), or the others */
/* a box of b's a keyword names -- border-box, padding-box, content-box; another its border
   box -- into a (x0, y0, x1, y1), and its corners' radii from the border box's rad, each
   less what lies between (CSS Backgrounds 5.2) into r */
/* how far a fieldset's border box is drawn below its top: to the middle of a legend taller than its top border, as
   Chromium draws it; 0 for any other box */
static double pt_legend_shift(const layout_tree *t, uint32_t b) {
    uint32_t lg = t->box[b].legend;
    if (!lg) return 0;
    double lmh = t->box[lg].mt + t->box[lg].bt + t->box[lg].pt + t->box[lg].h + t->box[lg].pb + t->box[lg].bb + t->box[lg].mb;
    return lmh > t->box[b].bt ? (lmh - t->box[b].bt) / 2 : 0;
}

static void pt_area(const layout_tree *t, uint32_t b, const char *w, size_t wn, const double *rad, double *a, double *r) {
    layout_box x = t->box[b];
    double dl = 0, dt = 0, dr = 0, db = 0;
    if (pt_is(w, wn, "padding-box")) { dl = x.bl; dt = x.bt; dr = x.br; db = x.bb; }
    else if (pt_is(w, wn, "content-box")) { dl = x.bl + x.pl; dt = x.bt + x.pt; dr = x.br + x.pr; db = x.bb + x.pb; }
    a[0] = x.x - x.pl - x.bl + dl;
    a[1] = x.y - x.pt - x.bt + dt + (dt < x.bt + x.pt ? pt_legend_shift(t, b) : 0);
    a[2] = x.x + x.w + x.pr + x.br - dr;
    a[3] = x.y + x.h + x.pb + x.bb - db;
    r[0] = fmax(0, rad[0] - dl); r[1] = fmax(0, rad[1] - dr); r[2] = fmax(0, rad[2] - dr); r[3] = fmax(0, rad[3] - dl);
    r[4] = fmax(0, rad[4] - dt); r[5] = fmax(0, rad[5] - dt); r[6] = fmax(0, rad[6] - db); r[7] = fmax(0, rad[7] - db);
}

static void pt_layers(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, vector<uint32_t> *icache, vector<uint8_t> *ipx,
                      const double *rad, int text_only) {
    vector<char> img, pos, siz, rep, org, clp;
    amb_span sp = t->box[b].bg_image;
    append_range(img, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_origin;
    append_range(org, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_clip;
    append_range(clp, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_pos;
    append_range(pos, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_size;
    append_range(siz, text_ptr(t, sp), sp.len);
    sp = t->box[b].bg_repeat;
    append_range(rep, text_ptr(t, sp), sp.len);
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
        /* the layer's painting area (background-clip's box) and positioning area
           (background-origin's: the padding box unless it says otherwise) */
        size_t oa = 0, ol = 0, ca = 0, cl = 0;
        if (len(org)) pt_nth(data(org), len(org), q - 1, &oa, &ol);
        if (len(clp)) pt_nth(data(clp), len(clp), q - 1, &ca, &cl);
        /* a layer clipped to the text is drawn by pt_text_bg (text_only), over the border box */
        int to_text = cl && pt_is(data(clp) + ca, cl, "text");
        if (to_text != (text_only != 0)) continue;
        double area[4], ar[8], clip[4], cr[8];
        if (ol) pt_area(t, b, data(org) + oa, ol, rad, area, ar);
        else pt_area(t, b, "padding-box", 11, rad, area, ar);
        if (cl && !to_text) pt_area(t, b, data(clp) + ca, cl, rad, clip, cr);
        else pt_area(t, b, "border-box", 10, rad, clip, cr);
        double bx0 = clip[0], by0 = clip[1], bx1 = clip[2], by1 = clip[3];
        double ax = area[0], ay = area[1], aw = area[2] - area[0], ah = area[3] - area[1];
        /* the positioning area moved with the border box's origin snapped to whole pixels, its offsets kept (Blink's
           BackgroundImageGeometry: a tile at round(box) + offset, resampled there; measured on seven boxes) */
        {
            double ox0 = t->box[b].x - t->box[b].pl - t->box[b].bl, oy0 = t->box[b].y - t->box[b].pt - t->box[b].bt;
            double zz = c->zoom > 0 ? c->zoom : 1;     /* snapped in CSS px, as Chromium snaps under a zoom */
            ax += floor(ox0 / zz + 0.5) * zz - ox0;
            ay += floor(oy0 / zz + 0.5) * zz - oy0;
        }
        const char *rp = data(rep) + ra;
        int rx = 1, ry = 1;
        if (rl && pt_is(rp, rl, "no-repeat")) { rx = 0; ry = 0; }
        else if (rl && pt_is(rp, rl, "repeat-x")) ry = 0;
        else if (rl && pt_is(rp, rl, "repeat-y")) rx = 0;
        else if (rl && memcmp(rp, "no-repeat", rl < 9 ? rl : 9) == 0) { rx = 0; ry = 0; }
        int is_url = ll > 4 && memcmp(li, "url(", 4) == 0;
        size_t off = 0;
        uint32_t iw = 0, ih = 0;
        /* an SVG image (src/paint/svg.h): its natural size its tile's (a ratio alone as
           contain has it, neither the area), drawn at its tile's size */
        double snw = 0, snh = 0, sr = 0;
        int svg = is_url && t->box[b].bg_res.index && amb_svg_size_of(nt, t->box[b].bg_res.index, &snw, &snh, &sr);
        if (svg) {
            double z = c->zoom > 0 ? c->zoom : 1, cw = aw, ch = ah;
            if (snw > 0 && snh > 0) { cw = snw * z; ch = snh * z; }
            else if (sr > 0 && ah > 0) { if (aw / ah > sr) cw = ah * sr; else ch = aw / sr; }
            iw = (uint32_t)floor(cw + 0.5);
            ih = (uint32_t)floor(ch + 0.5);
        } else if (is_url && !(t->box[b].bg_res.index && pt_cached_image(nt, t->box[b].bg_res.index, icache, ipx, &off, &iw, &ih))) continue;
        double tw, th, ox, oy;
        /* a raster image's natural size in device px: its pixels at the zoom (an SVG's is so already) */
        double zt = !svg && c->zoom > 0 ? c->zoom : 1;
        pt_tile_size(data(siz) + sa, sl, iw * zt, ih * zt, aw, ah, &tw, &th);
        if (tw < 0.5 || th < 0.5) continue;
        vector<uint8_t> spx;
        uint32_t spw = (uint32_t)floor(tw + 0.5), sph = (uint32_t)floor(th + 0.5);
        if (svg && (spw < 1 || sph < 1 || !amb_svg_raster_of(nt, t->box[b].bg_res.index, spw, sph, &spx))) continue;
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
                if (svg) pt_image(c, &spx, spw, sph, tx, ty, tw, th);
                else if (is_url) {
                    vector<uint8_t> tile;
                    append_range(tile, data(*ipx) + off, (size_t)iw * ih * 4);
                    pt_image(c, &tile, iw, ih, tx, ty, tw, th);
                } else pt_gradient(c, li, ll, tx, ty, tw, th, bx0, by0, bx1, by1, cr);
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

/* ---- the theme: form controls drawn as Chromium's NativeThemeBase draws them (light scheme) ---- */

static void pt_hair_rrect(amb_canvas *c, double l, double t, double r, double b, double rad, uint32_t col);

/* an anti-aliased rounded rectangle (x0, y0)-(x1, y1), corners of radius r, filled; with ring set, only the band
   between it and the same rectangle inset by 1px (its radius r - 1): Skia's fill, and its 1px stroke along a path
   inset half a px */
static void pt_theme_rr(amb_canvas *c, double x0, double y0, double x1, double y1, double r, uint32_t col, int ring) {
    double ro[8] = { r, r, r, r, r, r, r, r }, ri[8];
    double rr = r - 1 > 0 ? r - 1 : 0;
    for (int k = 0; k < 8; k++) ri[k] = rr;
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int Y = (int)floor(y0); Y < (int)ceil(y1); Y++) {
        int yy = (int)(Y - c->oy);
        if (yy < ky0 || yy >= ky1) continue;
        for (int X = (int)floor(x0); X < (int)ceil(x1); X++) {
            if (X < kx0 || X >= kx1) continue;
            double cv = pt_rr_cover(X + 0.5, Y + 0.5, x0, y0, x1, y1, ro);
            if (ring) cv -= pt_rr_cover(X + 0.5, Y + 0.5, x0 + 1, y0 + 1, x1 - 1, y1 - 1, ri);
            if (cv > 0.002) pt_blend(c, X, yy, col, cv > 1 ? 1 : cv);
        }
    }
}
/* a disc of radius r at (cx, cy) filled, each pixel's coverage from 16 by 16 samples */
static void pt_theme_disc(amb_canvas *c, double cx, double cy, double r, uint32_t col) {
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int Y = (int)floor(cy - r); Y < (int)ceil(cy + r); Y++) {
        int yy = (int)(Y - c->oy);
        if (yy < ky0 || yy >= ky1) continue;
        for (int X = (int)floor(cx - r); X < (int)ceil(cx + r); X++) {
            if (X < kx0 || X >= kx1) continue;
            int in = 0;
            for (int sy = 0; sy < 16; sy++)
                for (int sx = 0; sx < 16; sx++) {
                    double dx = X + (sx + 0.5) / 16 - cx, dy = Y + (sy + 0.5) / 16 - cy;
                    if (dx * dx + dy * dy <= r * r) in++;
                }
            if (in) pt_blend(c, X, yy, col, in / 256.0);
        }
    }
}
/* a polyline of n points stroked w wide, butt ends and mitred joints, anti-aliased: its outline filled */
static void pt_theme_stroke(amb_canvas *c, const double *px, const double *py, int n, double w, uint32_t col, vector<float> *acc) {
    vector<double> xs, ys;
    double h = w / 2;
    for (int side = 0; side < 2; side++) {
        for (int k = 0; k < n; k++) {
            int i = side ? n - 1 - k : k;
            double nx = 0, ny = 0;
            /* the normals of the segments meeting at i, their mitre */
            double ax = 0, ay = 0, bx = 0, by = 0;
            if (i > 0) { double dx = px[i] - px[i - 1], dy = py[i] - py[i - 1], l = sqrt(dx * dx + dy * dy); ax = -dy / l; ay = dx / l; }
            if (i < n - 1) { double dx = px[i + 1] - px[i], dy = py[i + 1] - py[i], l = sqrt(dx * dx + dy * dy); bx = -dy / l; by = dx / l; }
            if (i == 0) { nx = bx; ny = by; }
            else if (i == n - 1) { nx = ax; ny = ay; }
            else {
                double mx = ax + bx, my = ay + by, ml = sqrt(mx * mx + my * my), dot = ml > 0 ? (mx * ax + my * ay) / ml : 1;
                nx = ml > 0 ? mx / ml / dot : ax;
                ny = ml > 0 ? my / ml / dot : ay;
            }
            double s = side ? -h : h;
            push_back(xs, px[i] + nx * s);
            push_back(ys, py[i] + ny * s);
        }
    }
    pt_poly(c, &xs, &ys, col, acc);
}
/* a text field's, textarea's, list box's or drop-down's frame: its rectangle less its last row and column filled,
   and a 1px hairline along the four edges of that rectangle, its corner pixels left out (a rounded rectangle of
   radius 2, drawn without anti-aliasing: NativeThemeBase::PaintTextField) */
static void pt_theme_field(amb_canvas *c, int x0, int y0, int x1, int y1, uint32_t fill, uint32_t line) {
    pt_fill(c, x0, y0, x1 - 1, y1 - 1, fill);
    pt_fill(c, x0 + 1, y0, x1 - 1, y0 + 1, line);
    pt_fill(c, x0 + 1, y1 - 1, x1 - 1, y1, line);
    pt_fill(c, x0, y0 + 1, x0 + 1, y1 - 1, line);
    pt_fill(c, x1 - 1, y0 + 1, x1, y1 - 1, line);
}
/* the form control b as the theme draws it, its border box (x, y, w, h) in page px */
static void pt_theme(const layout_tree *t, uint32_t b, amb_canvas *c, double x, double y, double w, double h, vector<float> *acc) {
    /* disabled (Chromium's lighten layer, then each part in its disabled colour; measured over a white and a black
       page, each part's colour and alpha solved from the two): the border #767676 at 30% over an interior of #f5f5f5
       at 68%, the accent #767676 at 30% too, a check mark white at 60% (0x99), a range's value and thumb #cbcbcb */
    int dis = t->box[b].tstate & 1;
    const uint32_t line = dis ? 0x7676764d : 0x767676ff, accent = dis ? 0x7676764d : 0x0075ffff;
    const uint32_t inner = 0xf5f5f5ad;
    uint32_t fill = t->box[b].background & 0xff ? t->box[b].background : 0xffffffff;
    int X0 = (int)floor(x + 0.5), Y0 = (int)floor(y + 0.5), X1 = (int)floor(x + w + 0.5), Y1 = (int)floor(y + h + 0.5);
    layout_theme th = t->box[b].theme;
    if (th == LAYOUT_THEME_FIELD || th == LAYOUT_THEME_AREA || th == LAYOUT_THEME_LISTBOX) {
        if (th == LAYOUT_THEME_LISTBOX && dis) pt_theme_field(c, X0, Y0, X1, Y1, 0xffffffb3, 0x76767638);
        else pt_theme_field(c, X0, Y0, X1, Y1, dis ? inner : fill, line);
        if (th == LAYOUT_THEME_AREA) {
            /* the resizer: two diagonals in the bottom right corner */
            uint32_t rz = dis ? 0x6666664d : 0x666666ff;
            for (int k = 0; k < 7; k++) pt_fill(c, X1 - 4 - k, Y1 - 10 + k, X1 - 3 - k, Y1 - 9 + k, rz);
            for (int k = 0; k < 3; k++) pt_fill(c, X1 - 4 - k, Y1 - 6 + k, X1 - 3 - k, Y1 - 5 + k, rz);
        }
    } else if (th == LAYOUT_THEME_MENULIST) {
        /* a drop-down: a field's frame on the field's white, and its arrow, an 8 by 4 chevron stroked 2px wide in the
           text's colour, 12px before the inner right edge, at the box's middle (ThemePainterDefault::SetupMenuListArrow);
           disabled, the frame fainter and the arrow at 70% */
        pt_theme_field(c, X0, Y0, X1, Y1, dis ? 0xf4f4f478 : 0xffffffff, dis ? 0x76767638 : line);
        double ax = X1 - floor(t->box[b].br) - 12, ay = Y0 + floor((Y1 - Y0) / 2);
        double px[3] = { ax, ax + 4, ax + 8 }, py[3] = { ay - 2, ay + 2, ay - 2 };
        uint32_t arrow = t->box[b].swatch & 0xff ? t->box[b].swatch : 0x000000ff;
        if (dis) arrow = (arrow & 0xffffff00) | (uint32_t)((arrow & 0xff) * 0.7 + 0.5);
        pt_theme_stroke(c, px, py, 3, 2, arrow, acc);
    } else if (th == LAYOUT_THEME_BUTTON || th == LAYOUT_THEME_COLORWELL) {
        /* a button: its background as a rounded rectangle of radius 2, its border 1px inside it (PaintButton);
           disabled, #d8d8d8 at 44% and its border #c0c0c0 at 68% */
        if (dis) fill = 0xd8d8d870;
        if (X1 - X0 < 5 || Y1 - Y0 < 5) { pt_fill(c, X0, Y0, X1, Y1, fill); return; }
        pt_theme_rr(c, X0, Y0, X1, Y1, 2, fill, 0);
        pt_hair_rrect(c, X0 + 0.5, Y0 + 0.5, X1 - 0.5, Y1 - 0.5, 2, dis ? 0xc0c0c0ad : line);
        if (th == LAYOUT_THEME_COLORWELL) {
            /* the swatch: the content box less 4px above and below and 2px aside, a 1px #777777 border round the colour */
            double cx0 = x + t->box[b].bl + t->box[b].pl + 2, cy0 = y + t->box[b].bt + t->box[b].pt + 4;
            double cx1 = x + w - t->box[b].br - t->box[b].pr - 2, cy1 = y + h - t->box[b].bb - t->box[b].pb - 4;
            if (cx1 - cx0 >= 2 && cy1 - cy0 >= 2) {
                pt_fill(c, cx0, cy0, cx1, cy1, 0x777777ff);
                pt_fill(c, cx0 + 1, cy0 + 1, cx1 - 1, cy1 - 1, t->box[b].swatch);
            }
        }
    } else if (th == LAYOUT_THEME_CHECKBOX || th == LAYOUT_THEME_RADIO) {
        /* the largest square in the box, a rounded square (radius 2) or a circle: unchecked, white with a 1px border;
           a check box checked, the accent filled and a white check stroked across it; a radio button checked, its
           border the accent and a dot of it inside (PaintCheckbox, PaintRadio) */
        double s = w < h ? w : h, sx = x + (w - s) / 2, sy = y + (h - s) / 2, r = th == LAYOUT_THEME_RADIO ? s / 2 : 2;
        int checked = t->box[b].checked;
        if (s <= 2) { pt_fill(c, sx, sy, sx + s, sy + s, line); return; }
        if (dis) pt_theme_rr(c, sx, sy, sx + s, sy + s, r, inner, 0);
        if (!dis || (th == LAYOUT_THEME_CHECKBOX && checked))
            pt_theme_rr(c, sx, sy, sx + s, sy + s, r, th == LAYOUT_THEME_CHECKBOX && checked ? accent : 0xffffffff, 0);
        if (th == LAYOUT_THEME_CHECKBOX && checked) {
            double px[3] = { sx + s * 0.2, sx + s * 0.4, sx + s - s * 0.2 }, py[3] = { sy + s / 2, sy + s / 2 + s * 0.2, sy + s * 0.2 };
            pt_theme_stroke(c, px, py, 3, s * 0.16, dis ? 0xffffff99 : 0xffffffff, acc);
            return;
        }
        pt_hair_rrect(c, sx + 0.5, sy + 0.5, sx + s - 0.5, sy + s - 0.5, th == LAYOUT_THEME_RADIO ? (s - 1) / 2 : 2, checked && !dis ? accent : line);
        if (th == LAYOUT_THEME_RADIO && checked) {
            /* the dot: as Chromium's draws, its radius 0.28 of the box and 0.36px (fitted to its pixels: 4px in 13,
               7.1 in 24) */
            pt_theme_disc(c, sx + s / 2, sy + s / 2, s * 0.28 + 0.36, accent);
        }
    } else if (th == LAYOUT_THEME_RANGE) {
        /* a range: its 8px track across the box's middle, #efefef and the accent up to the thumb, a 1px #767676 border
           at half its alpha, its corners rounded; the thumb a 16px circle of the accent, its centre from 8px in to 8px
           before the end along the value (PaintSliderTrack, PaintSliderThumb) */
        double ty0 = y + (h - 8) / 2, ty1 = ty0 + 8, tx0 = x + 1, tx1 = x + w - 1, cxm = x + 8 + floor(t->box[b].fraction * (w - 16) + 0.5);
        /* disabled: the track #eeeeee at 30%, its value and the thumb #cbcbcb */
        uint32_t value = dis ? 0xcbcbcbff : accent;
        pt_theme_rr(c, tx0, ty0, tx1, ty1, 4, dis ? 0xeeeeee4d : 0xefefefff, 0);
        double save = c->cx1;
        c->cx1 = fmin(c->cx1, cxm);
        pt_theme_rr(c, tx0, ty0, tx1, ty1, 4, value, 0);
        c->cx1 = save;
        pt_hair_rrect(c, tx0 + 0.5, ty0 + 0.5, tx1 - 0.5, ty1 - 0.5, 4, dis ? 0x7676764d : 0x76767680);
        pt_theme_disc(c, cxm, y + h / 2, 7.4, value);    /* as Chromium's 16px thumb draws (fitted: 7.4px) */
    }
}

static void pt_box(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, uint32_t skip_bg, vector<uint32_t> *icache,
                   vector<uint8_t> *ipx) {
    double bx = t->box[b].x - t->box[b].pl - t->box[b].bl, by = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double lsh = pt_legend_shift(t, b);
    by += lsh;
    bh -= lsh;
    double rad[8] = { t->box[b].rtl, t->box[b].rtr, t->box[b].rbr, t->box[b].rbl,
                      t->box[b].rtlv, t->box[b].rtrv, t->box[b].rbrv, t->box[b].rblv };
    pt_fit_radii(bw, bh, rad);
    int round = pt_round(rad);
    double bt = t->box[b].bt, br = t->box[b].br, bb = t->box[b].bb, bl = t->box[b].bl;
    double bws[4] = { bt, br, bb, bl };
    /* the padding box's radii: each less the border beside it (CSS Backgrounds 5.2) */
    double prad[8] = { fmax(0, rad[0] - bl), fmax(0, rad[1] - br), fmax(0, rad[2] - br), fmax(0, rad[3] - bl),
                       fmax(0, rad[4] - bt), fmax(0, rad[5] - bt), fmax(0, rad[6] - bb), fmax(0, rad[7] - bb) };
    vector<char> sh;
    if (t->box[b].shadows.len) {
        amb_span sp = t->box[b].shadows;
        append_range(sh, text_ptr(t, sp), sp.len);
        pt_box_shadows(c, data(sh), len(sh), bx, by, bx + bw, by + bh, rad, 0, bws, prad);
    }
    /* a form control the theme draws: its drawing in place of its background and border (its shadows kept) */
    int themed = t->box[b].theme != LAYOUT_THEME_CSS;
    if (themed) {
        vector<float> tacc;
        pt_theme(t, b, c, bx, by, bw, bh, &tacc);
    }
    if (b != skip_bg && (t->box[b].background & 0xff) && !themed) {
        /* the background colour, clipped by the bottom layer's background-clip (CSS
           Backgrounds 3.7): the last of its list */
        double ca[4], cr[8];
        size_t cs = 0, cl = 0;
        vector<char> clp;
        amb_span cp = t->box[b].bg_clip;
        append_range(clp, text_ptr(t, cp), cp.len);
        if (len(clp)) pt_nth(data(clp), len(clp), 1u << 30, &cs, &cl);
        if (!(cl && pt_is(data(clp) + cs, cl, "text"))) {
            if (cl) pt_area(t, b, data(clp) + cs, cl, rad, ca, cr);
            else pt_area(t, b, "border-box", 10, rad, ca, cr);
            pt_rrect(c, ca[0], ca[1], ca[2], ca[3], cr, t->box[b].background);
        }
    }
    if (t->box[b].bg_image.len && !themed) pt_layers(t, nt, b, c, icache, ipx, rad, 0);
    if (len(sh)) pt_box_shadows(c, data(sh), len(sh), bx, by, bx + bw, by + bh, rad, 1, bws, prad);
    if (themed) bt = br = bb = bl = 0;      /* its border the theme's */
    /* replaced content with rounded corners: held to its content box, rounded as the border box is less the border
       and padding (Blink's ReplacedPainter, the rounded inner rect for content), anti-aliased */
    double ka[4] = { 0, 0, 0, 0 }, kr[8];
    vector<uint8_t> kept;
    int kx0 = 0, ky0 = 0, kx1 = 0, ky1 = 0, kround = t->box[b].kind == LAYOUT_BOX_KIND_REPLACED && round;
    double scx0 = c->cx0, scy0 = c->cy0, scx1 = c->cx1, scy1 = c->cy1;
    if (kround) {
        pt_area(t, b, "content-box", 11, rad, ka, kr);
        if (c->cx0 < floor(ka[0])) c->cx0 = floor(ka[0]);
        if (c->cy0 < floor(ka[1] - c->oy) + c->oy) c->cy0 = floor(ka[1] - c->oy) + c->oy;
        if (c->cx1 > ceil(ka[2])) c->cx1 = ceil(ka[2]);
        if (c->cy1 > ceil(ka[3] - c->oy) + c->oy) c->cy1 = ceil(ka[3] - c->oy) + c->oy;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        if (kx1 > kx0 && ky1 > ky0) pt_keep(c, kx0, ky0, kx1, ky1, &kept);
        else kround = 0;
    }
    if (t->box[b].kind == LAYOUT_BOX_KIND_REPLACED && t->box[b].svg.len) {
        /* an svg element: the drawing layout made of its content (src/paint/svg.h) */
        vector<char> scene;
        append_range(scene, text_ptr(t, t->box[b].svg), t->box[b].svg.len);
        double z = c->zoom > 0 ? c->zoom : 1;
        amb_svg_draw(c, data(scene), len(scene), t->box[b].x, t->box[b].y, z, z);
    } else if (t->box[b].kind == LAYOUT_BOX_KIND_REPLACED) {
        vector<uint8_t> rgba;
        uint32_t iw, ih;
        double inw, inh;
        int got = pt_image_of(nt, t->box[b].node.index, &rgba, &iw, &ih, &inw, &inh);
        if (got > 0) pt_fit_image(c, t, b, &rgba, iw, ih, inw, inh);
        /* its icon before its alt text; a box of its own (no alt text, or quirks mode's of a declared size, its baseline
           its content box's bottom: has_baseline) with its border and icon */
        else if ((got < 0 || t->box[b].unsourced) && t->box[b].first_line && !t->box[b].has_baseline)
            pt_broken_icon_at(c, floor(t->box[b].x + 0.5), floor(t->box[b].y + 0.5));
        else if (got < 0) pt_broken(c, t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h);
        else if (t->box[b].unsourced) pt_unsourced(c, t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h);
        else amb_svg_image_of(c, nt, t->box[b].node.index, t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h);
    }
    if (kround) pt_round_clip(c, kx0, ky0, kx1, ky1, &kept, ka[0], ka[1], ka[2], ka[3], kr);
    c->cx0 = scx0;
    c->cy0 = scy0;
    c->cx1 = scx1;
    c->cy1 = scy1;
    /* a fieldset's top border not behind its legend's border box: the band kept as the background left it */
    vector<uint8_t> gap;
    int gx0 = 0, gx1 = 0, gy0 = 0, gy1 = 0;
    if (t->box[b].legend && bt > 0) {
        uint32_t lg = t->box[b].legend;
        int kx0, ky0, kx1, ky1;
        pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
        gx0 = (int)floor(t->box[lg].x - t->box[lg].pl - t->box[lg].bl + 0.5);
        gx1 = (int)floor(t->box[lg].x + t->box[lg].w + t->box[lg].pr + t->box[lg].br + 0.5);
        gy0 = (int)floor(by - c->oy);
        gy1 = (int)ceil(by + bt - c->oy);
        if (gx0 < kx0) gx0 = kx0;
        if (gx1 > kx1) gx1 = kx1;
        if (gy0 < ky0) gy0 = ky0;
        if (gy1 > ky1) gy1 = ky1;
        for (int y = gy0; y < gy1 && gx1 > gx0; y++) append_range(gap, data(c->px) + ((size_t)y * c->w + (size_t)gx0) * 3, (size_t)(gx1 - gx0) * 3);
    }
    int st0 = t->box[b].bst, st1 = t->box[b].bsr, st2 = t->box[b].bsb, st3 = t->box[b].bsl;
    int plain = (st0 == LAY_BS_SOLID || st0 <= LAY_BS_HIDDEN) && (st1 == LAY_BS_SOLID || st1 <= LAY_BS_HIDDEN) &&
                (st2 == LAY_BS_SOLID || st2 <= LAY_BS_HIDDEN) && (st3 == LAY_BS_SOLID || st3 <= LAY_BS_HIDDEN);
    if (round && (bt > 0 || br > 0 || bb > 0 || bl > 0)) {
        /* a rounded border: the ring between the border box and the padding box, each
           pixel its side's colour (the sides meeting along the corners' diagonals) */
        const double *ir = prad;
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
                if (sty <= LAY_BS_HIDDEN || sty == LAY_BS_DASHED || sty == LAY_BS_DOTTED) continue;
                pt_blend(c, x, y, pt_side_colour(colv, sty, side, 1), cov);
            }
        /* the dashed and dotted sides, along the border's centre line: the four as one
           path when they are alike */
        double ws[4] = { bt, br, bb, bl };
        int sts[4] = { st0, st1, st2, st3 };
        uint32_t cols[4] = { t->box[b].bct, t->box[b].bcr, t->box[b].bcb, t->box[b].bcl };
        vector<float> pacc;
        int alike = (sts[0] == LAY_BS_DASHED || sts[0] == LAY_BS_DOTTED) && ws[0] > 0;
        for (int s = 1; s < 4 && alike; s++) if (ws[s] != ws[0] || sts[s] != sts[0] || cols[s] != cols[0]) alike = 0;
        if (alike) {
            double hw = ws[0] / 2;
            double clr[4] = { bx + hw, by + hw, bx + bw - hw, by + bh - hw };
            double crr[8];
            for (int k = 0; k < 8; k++) crr[k] = fmax(0, rad[k] - hw);
            pt_round_side(c, -1, sts[0], cols[0], ws[0], clr, crr, &pacc);
        }
        for (int s = 0; s < 4 && !alike; s++) {
            if (ws[s] <= 0 || (sts[s] != LAY_BS_DASHED && sts[s] != LAY_BS_DOTTED)) continue;
            double hw = ws[s] / 2;
            double clr[4] = { bx + hw, by + hw, bx + bw - hw, by + bh - hw };
            double crr[8];
            for (int k = 0; k < 8; k++) crr[k] = fmax(0, rad[k] - hw);
            pt_round_side(c, s, sts[s], cols[s], ws[s], clr, crr, &pacc);
        }
    } else if (plain) {
        /* the sides between the corners, each corner drawn once where two meet */
        uint32_t ct = t->box[b].bct, cr = t->box[b].bcr, cb = t->box[b].bcb, cl = t->box[b].bcl;
        if (bt > 0) pt_fill(c, bx + bl, by, bx + bw - br, by + bt, ct);
        if (bb > 0) pt_fill(c, bx + bl, by + bh - bb, bx + bw - br, by + bh, cb);
        if (bl > 0) pt_fill(c, bx, by + bt, bx + bl, by + bh - bb, cl);
        if (br > 0) pt_fill(c, bx + bw - br, by + bt, bx + bw, by + bh - bb, cr);
        pt_corner(c, bx, by, bx + bl, by + bt, 0, ct, cl);
        pt_corner(c, bx + bw - br, by, bx + bw, by + bt, 1, ct, cr);
        pt_corner(c, bx + bw - br, by + bh - bb, bx + bw, by + bh, 2, cb, cr);
        pt_corner(c, bx, by + bh - bb, bx + bl, by + bh, 3, cb, cl);
    } else {
        double ws[4] = { bt, br, bb, bl };
        int sts[4] = { st0, st1, st2, st3 };
        uint32_t cols[4] = { t->box[b].bct, t->box[b].bcr, t->box[b].bcb, t->box[b].bcl };
        pt_styled_sides(c, bx, by, bw, bh, ws, sts, cols);
    }
    if (len(gap)) {
        size_t row = (size_t)(gx1 - gx0) * 3, q = 0;
        for (int y = gy0; y < gy1; y++, q += row) memcpy(data(c->px) + ((size_t)y * c->w + (size_t)gx0) * 3, data(gap) + q, row);
    }
}

/* a box's outline: outside its border box by its offset, in its style */
static void pt_outline(const layout_tree *t, uint32_t b, amb_canvas *c) {
    double w = t->box[b].ow;
    if (w <= 0 || (t->box[b].ocolor & 0xff) == 0) return;
    double o = t->box[b].ooffset;
    double x0 = t->box[b].x - t->box[b].pl - t->box[b].bl - o - w, y0 = t->box[b].y - t->box[b].pt - t->box[b].bt - o - w;
    double x1 = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br + o + w, y1 = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb + o + w;
    /* drawn as a border is, its four sides alike */
    double ws[4] = { w, w, w, w };
    int sts[4] = { t->box[b].ostyle, t->box[b].ostyle, t->box[b].ostyle, t->box[b].ostyle };
    uint32_t cols[4] = { t->box[b].ocolor, t->box[b].ocolor, t->box[b].ocolor, t->box[b].ocolor };
    pt_styled_sides(c, x0, y0, x1 - x0, y1 - y0, ws, sts, cols);
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
        m.zoom = c->zoom;
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

/* a disclosure marker (a details element's summary's, list-style-type disclosure-closed or -open), drawn as Chromium
   draws it: a triangle in a square of 0.66em whose bottom is on the baseline, pointing to the end (an equilateral
   one, from the square's start) or down (centred in it); 1 when the fragment is one */
static int pt_disclosure(const layout_tree *t, uint32_t fr, amb_canvas *c, vector<float> *acc) {
    amb_span sp = t->frag[fr].text;
    if (sp.len != 3) return 0;
    const char *s = text_ptr(t, sp);
    if ((uint8_t)s[0] != 0xE2 || (uint8_t)s[1] != 0x96 || ((uint8_t)s[2] != 0xB8 && (uint8_t)s[2] != 0xBE)) return 0;
    int open = (uint8_t)s[2] == 0xBE;
    double sz = floor(0.66 * t->frag[fr].size * 64) / 64, x0 = t->frag[fr].x, y0 = t->frag[fr].baseline - sz;
    vector<double> xs, ys;
    if (open) {
        double inset = sz * (1 - 0.8660254) / 2;
        push_back(xs, x0); push_back(ys, y0 + inset);
        push_back(xs, x0 + sz); push_back(ys, y0 + inset);
        push_back(xs, x0 + sz / 2); push_back(ys, y0 + sz - inset);
    } else {
        push_back(xs, x0); push_back(ys, y0);
        push_back(xs, x0 + sz * 0.8660254); push_back(ys, y0 + sz / 2);
        push_back(xs, x0); push_back(ys, y0 + sz);
    }
    pt_poly(c, &xs, &ys, t->frag[fr].fill, acc);
    return 1;
}

/* an inline fragment's decorations: its box of them (the element as one strip, CSS Backgrounds 3's slice) drawn
   clipped to the fragment where the strip breaks -- not at the strip's own ends, where its shadows reach out */
static void pt_inline_piece(const layout_tree *t, const net_loader *nt, uint32_t fr, amb_canvas *c, vector<uint32_t> *icache, vector<uint8_t> *ipx) {
    uint32_t b = t->frag[fr].piece;
    double s0 = t->box[b].x - t->box[b].pl - t->box[b].bl, s1 = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br;
    double f0 = floor(t->frag[fr].x + 0.5), f1 = floor(t->frag[fr].x + t->frag[fr].w + 0.5);
    double save[4] = { c->cx0, c->cy0, c->cx1, c->cy1 };
    if (f0 > s0 + 0.01) c->cx0 = fmax(c->cx0, f0);
    if (f1 < s1 - 0.01) c->cx1 = fmin(c->cx1, f1);
    if (c->cx1 > c->cx0) pt_box(t, nt, b, c, 0, icache, ipx);
    c->cx0 = save[0];
    c->cy0 = save[1];
    c->cx1 = save[2];
    c->cy1 = save[3];
}

/* ---- Skia's anti-aliased hairline (SkScan_Antihair.cpp), for a circle marker's 1px stroke, which Skia draws as a
   hairline (SkDrawTreatAsHairline) ---- */

/* one pixel blended at coverage a (0..255), a page's y */
static void pt_hair_blit(amb_canvas *c, int x, int y, uint32_t col, int a) {
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int yy = (int)(y - c->oy);
    if (a <= 0 || x < kx0 || x >= kx1 || yy < ky0 || yy >= ky1) return;
    pt_blend(c, x, yy, col, a / 255.0);
}
/* a line from (x0, y0) to (x1, y1) in 26.6 fixed point (do_anti_hairline): stepped along its major axis a pixel at a
   time, the minor axis's position in 16.16 split between the two pixels it falls between, its end pixels' coverage
   scaled by how much of them the line spans */
static void pt_hair_line(amb_canvas *c, int x0, int y0, int x1, int y1, uint32_t col) {
    int horiz = abs(x1 - x0) > abs(y1 - y0);
    if (!horiz) { int s = x0; x0 = y0; y0 = s; s = x1; x1 = y1; y1 = s; }   /* the major axis as x from here */
    if (x0 > x1) { int s = x0; x0 = x1; x1 = s; s = y0; y0 = y1; y1 = s; }
    int istart = x0 >> 6, istop = (x1 + 63) >> 6;
    long fstart = (long)y0 << 10, slope = 0;
    if (y0 != y1) {
        slope = (long)(((int64_t)(y1 - y0) << 16) / (x1 - x0));
        fstart += (slope * (32 - (x0 & 63)) + 32) >> 6;
    }
    int scale_start, scale_stop;
    if (istop - istart == 1) { scale_start = x1 - x0; scale_stop = 0; }
    else { scale_start = 64 - (x0 & 63); scale_stop = x1 & 63; }
    long fy = fstart;
    for (int i = istart; i < istop; i++) {
        int mod = i == istart ? scale_start : (i == istop - 1 && scale_stop > 0) ? scale_stop : 64;
        long f = fy + 32768;
        int lower = (int)(f >> 16), a = (int)((f >> 8) & 0xFF);
        int a0 = (255 - a) * mod >> 6, a1 = a * mod >> 6;
        if (horiz) { pt_hair_blit(c, i, lower - 1, col, a0); pt_hair_blit(c, i, lower, col, a1); }
        else { pt_hair_blit(c, lower - 1, i, col, a0); pt_hair_blit(c, lower, i, col, a1); }
        fy += slope;
    }
}
/* a quadratic (x[3], y[3]) cut into 2^level lines (hair_quad), at Skia's level for it: the control point's distance
   from its chord's middle, whole px, until under a px (compute_quad_level) */
static void pt_hair_quad(amb_canvas *c, const double *x, const double *y, uint32_t col) {
    double dx = fabs((x[0] + x[2]) / 2 - x[1]), dy = fabs((y[0] + y[2]) / 2 - y[1]);
    uint32_t ix = (uint32_t)ceil(dx), iy = (uint32_t)ceil(dy), d = ix > iy ? ix + (iy >> 1) : iy + (ix >> 1);
    int bits = 0;
    while (bits < 32 && (d >> bits)) bits++;
    int level = (33 - (32 - bits)) >> 1;
    if (level > 4) level = 4;
    int n = 1 << level;
    double ax = x[2] - 2 * x[1] + x[0], ay = y[2] - 2 * y[1] + y[0], bx = 2 * (x[1] - x[0]), by = 2 * (y[1] - y[0]);
    double px = x[0], py = y[0];
    for (int i = 1; i <= n; i++) {
        double tt = (double)i / n, qx = i == n ? x[2] : (ax * tt + bx) * tt + x[0], qy = i == n ? y[2] : (ay * tt + by) * tt + y[0];
        pt_hair_line(c, (int)(px * 64), (int)(py * 64), (int)(qx * 64), (int)(qy * 64), col);
        px = qx;
        py = qy;
    }
}
/* a conic (x[3], y[3], weight w) as quadratics within a quarter px of it (SkConic::computeQuadPOW2, chop): halved
   pow2 times, each half's weight sqrt((1 + w) / 2) */
static void pt_hair_conic(amb_canvas *c, const double *x, const double *y, double w, int pow2, uint32_t col) {
    if (pow2 < 0) {
        double a = w - 1, k = a / (4 * (2 + a)), ex = k * (x[0] - 2 * x[1] + x[2]), ey = k * (y[0] - 2 * y[1] + y[2]);
        double err = sqrt(ex * ex + ey * ey);
        for (pow2 = 0; pow2 < 5 && err > 0.25; pow2++) err *= 0.25;
    }
    if (pow2 == 0) { pt_hair_quad(c, x, y, col); return; }
    double sc = 1 / (1 + w), nw = sqrt(0.5 + w * 0.5), wx = w * x[1], wy = w * y[1];
    double mx = (x[0] + 2 * wx + x[2]) * sc * 0.5, my = (y[0] + 2 * wy + y[2]) * sc * 0.5;
    double l[3] = { x[0], (x[0] + wx) * sc, mx }, lt[3] = { y[0], (y[0] + wy) * sc, my };
    double r[3] = { mx, (wx + x[2]) * sc, x[2] }, rt[3] = { my, (wy + y[2]) * sc, y[2] };
    pt_hair_conic(c, l, lt, nw, pow2 - 1, col);
    pt_hair_conic(c, r, rt, nw, pow2 - 1, col);
}
/* an oval's hairline as Skia draws one: SkPath::Oval's four conics from its right middle, clockwise (each a quarter,
   its control point the bounds' corner, weight sqrt(2)/2) */
static void pt_hair_oval_from(amb_canvas *c, double l, double t, double r, double b, int start, uint32_t col) {
    double cx = (l + r) / 2, cy = (t + b) / 2;
    const double ox[4] = { cx, r, cx, l }, oy[4] = { t, cy, b, cy }, rx[4] = { l, r, r, l }, ry[4] = { t, t, b, b };
    for (int i = 0; i < 4; i++) {
        int from = (start + i) % 4, to = (start + 1 + i) % 4;
        double x[3] = { ox[from], rx[to], ox[to] }, y[3] = { oy[from], ry[to], oy[to] };
        pt_hair_conic(c, x, y, 0.70710678118654752, -1, col);
    }
}
static void pt_hair_oval(amb_canvas *c, double l, double t, double r, double b, uint32_t col) { pt_hair_oval_from(c, l, t, r, b, 1, col); }
/* a rounded rectangle's outline (its corners' radius rad) as Skia's hairline draws a 1px stroke of one: SkPath::addRRect's
   path from its left edge's bottom end, clockwise, each side a line and each corner a conic (a circle's its oval's,
   from its left middle) */
static void pt_hair_rrect(amb_canvas *c, double l, double t, double r, double b, double rad, uint32_t col) {
    if (rad * 2 >= r - l && rad * 2 >= b - t) { pt_hair_oval_from(c, l, t, r, b, 3, col); return; }
    if (rad > (r - l) / 2) rad = (r - l) / 2;
    if (rad > (b - t) / 2) rad = (b - t) / 2;
    /* the eight points where the sides meet the corners (Skia's RRect point iterator), from the left edge's bottom */
    const double px[8] = { l + rad, r - rad, r, r, r - rad, l + rad, l, l }, py[8] = { t, t, t + rad, b - rad, b, b, b - rad, t + rad };
    const double cxs[4] = { l, r, r, l }, cys[4] = { t, t, b, b };
    int at6 = 6;
    for (int i = 0; i < 4; i++) {
        int a = (at6 + 2 * i) % 8, m = (a + 1) % 8, e = (a + 2) % 8;
        pt_hair_line(c, (int)(px[a] * 64), (int)(py[a] * 64), (int)(px[m] * 64), (int)(py[m] * 64), col);
        int corner = (i + 0) % 4;     /* the corner after the side: top left, top right, bottom right, bottom left */
        double x[3] = { px[m], cxs[corner], px[e] }, y[3] = { py[m], cys[corner], py[e] };
        if (rad > 0) pt_hair_conic(c, x, y, 0.70710678118654752, -1, col);
    }
}

/* a disc's, circle's or square's marker, drawn as Chromium draws it (ListMarkerPainter::PaintSymbol): in a square of
   (2/3 of its whole ascent + 1) / 2 px, 1px into its marker box and half its ascent's last third below the ascent's
   top, snapped to pixels; a disc an ellipse filled, a circle one stroked 1px wide (a hairline, as Skia draws a 1px
   stroke), a square the square filled; 1 when the fragment is one. The fragment's top is its whole ascent above the
   baseline (layout's lay_marker) */
static int pt_symbol(const layout_tree *t, uint32_t fr, amb_canvas *c) {
    amb_span sp = t->frag[fr].text;
    if (sp.len != 3) return 0;
    const uint8_t *s = (const uint8_t *)text_ptr(t, sp);
    int kind = s[0] == 0xE2 && s[1] == 0x80 && s[2] == 0xA2 ? 1 : s[0] == 0xE2 && s[1] == 0x97 && s[2] == 0xA6 ? 2 :
               s[0] == 0xE2 && s[1] == 0x96 && s[2] == 0xAA ? 3 : 0;
    if (!kind) return 0;
    int ai = (int)floor(t->frag[fr].baseline - t->frag[fr].y + 0.5), off = ai * 2 / 3, bw = (off + 1) / 2;
    if (bw <= 0) return 1;
    double x0 = floor(t->frag[fr].x + 1 + 0.5), y0 = floor(t->frag[fr].baseline - ai + 0.5) + 3 * (ai - off) / 2;
    uint32_t col = t->frag[fr].fill;
    if (kind == 3) { pt_fill(c, x0, y0, x0 + bw, y0 + bw, col); return 1; }
    if (kind == 2) { pt_hair_oval(c, x0, y0, x0 + bw, y0 + bw, col); return 1; }
    /* a disc: the ellipse's coverage of each pixel, from 16 by 16 samples */
    double cx = x0 + bw / 2.0, cy = y0 + bw / 2.0, r = bw / 2.0;
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int Y = (int)floor(cy - r); Y < (int)ceil(cy + r); Y++) {
        int yy = (int)(Y - c->oy);
        if (yy < ky0 || yy >= ky1) continue;
        for (int X = (int)floor(cx - r); X < (int)ceil(cx + r); X++) {
            if (X < kx0 || X >= kx1) continue;
            int in = 0;
            for (int sy = 0; sy < 16; sy++)
                for (int sx = 0; sx < 16; sx++) {
                    double dx = X + (sx + 0.5) / 16 - cx, dy = Y + (sy + 0.5) / 16 - cy;
                    if (dx * dx + dy * dy <= r * r) in++;
                }
            if (in) pt_blend(c, X, yy, col, in / 256.0);
        }
    }
    return 1;
}

static void pt_lines(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, const amb_fonts *f, const vector<uint32_t> *webmap,
                     vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask,
                     vector<uint32_t> *icache, vector<uint8_t> *ipx) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next)
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            layout_frag_kind k = t->frag[fr].kind;
            if (k == LAYOUT_FRAG_KIND_INLINE) {
                if (t->frag[fr].piece) pt_inline_piece(t, nt, fr, c, icache, ipx);
                else pt_fill(c, t->frag[fr].x, t->frag[fr].y, t->frag[fr].x + t->frag[fr].w, t->frag[fr].y + t->frag[fr].h, t->frag[fr].background);
                continue;
            }
            if (k != LAYOUT_FRAG_KIND_TEXT && k != LAYOUT_FRAG_KIND_MARKER && k != LAYOUT_FRAG_KIND_GENERATED) continue;
            if (k == LAYOUT_FRAG_KIND_MARKER && (pt_disclosure(t, fr, c, acc) || pt_symbol(t, fr, c))) continue;
            /* a fragment outside the canvas's window: nothing of it to draw */
            if (t->frag[fr].y > c->oy + c->h || t->frag[fr].y + t->frag[fr].h < c->oy) continue;
            amb_span sp = t->frag[fr].text;
            double size = t->frag[fr].size, base = t->frag[fr].baseline;
            if (t->frag[fr].shadows.len) pt_text_shadows(t, fr, c, f, webmap, entries, table, masks, acc, mask);
            pt_text(c, f, entries, table, masks, acc, mask, text_ptr(t, sp), sp.len, t->frag[fr].x, base,
                    pt_face_of(t, fr, webmap), size, t->frag[fr].italic, t->frag[fr].spacing, t->frag[fr].word_spacing, t->frag[fr].fill);
            /* the decorations in the text's fill, as Blink resolves their currentcolor */
            uint8_t deco = t->frag[fr].decoration;
            double th = floor(size / 16 + 0.5);
            if (th < 1) th = 1;
            double x0 = t->frag[fr].x, x1 = x0 + t->frag[fr].w;
            uint32_t dc = t->frag[fr].fill;
            if (deco & 1) { double y = floor(base + size * 0.09 + 0.5); pt_fill(c, x0, y, x1, y + th, dc); }
            if (deco & 2) { double y = floor(base - size * 0.93 + 0.5); pt_fill(c, x0, y, x1, y + th, dc); }
            if (deco & 4) { double y = floor(base - size * 0.3 + 0.5); pt_fill(c, x0, y, x1, y + th, dc); }
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

/* the box's background clipped to its text (background-clip: text, CSS Backgrounds 4):
   the layers so clipped, and its colour when its bottom layer is, drawn over a copy of
   what lies under its border box, then laid onto the canvas through the coverage of the
   glyphs of its text and of its in-flow and floated descendants' (not a positioned
   one's). Drawn after the box's borders, which glyphs seldom reach */
static void pt_text_bg(PCTX, uint32_t b) {
    if (!t->box[b].bg_clip.len) return;
    vector<char> clp;
    amb_span cs = t->box[b].bg_clip;
    append_range(clp, text_ptr(t, cs), cs.len);
    int any = 0;
    for (size_t i = 0; i + 4 <= len(clp); i++)
        if (memcmp(data(clp) + i, "text", 4) == 0) any = 1;
    if (!any) return;
    double bx = t->box[b].x - t->box[b].pl - t->box[b].bl, by = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double rad[8] = { t->box[b].rtl, t->box[b].rtr, t->box[b].rbr, t->box[b].rbl,
                      t->box[b].rtlv, t->box[b].rtrv, t->box[b].rbrv, t->box[b].rblv };
    pt_fit_radii(bw, bh, rad);
    /* the canvas's rows the border box covers */
    double y0 = fmax(by, c->oy), y1 = fmin(by + bh, c->oy + c->h);
    if (y1 <= y0) return;
    int r0 = (int)floor(y0 - c->oy), r1 = (int)ceil(y1 - c->oy);
    if (r1 > (int)c->h) r1 = (int)c->h;
    int kh = r1 - r0;
    if (kh <= 0 || c->w == 0) return;
    amb_canvas k;
    memset(&k, 0, sizeof k);
    defer(k) { AMB_CANVAS_DROP(k); }
    k.w = c->w;
    k.h = (uint32_t)kh;
    k.oy = c->oy + r0;
    k.zoom = c->zoom;
    k.cx0 = c->cx0;
    k.cx1 = c->cx1;
    k.cy0 = fmax(c->cy0, k.oy);
    k.cy1 = fmin(c->cy1, k.oy + kh);
    append_range(k.px, data(c->px) + (size_t)r0 * c->w * 3, (size_t)kh * c->w * 3);
    /* the colour, when the bottom layer (the last of the list) is clipped to the text */
    size_t ba = 0, bl = 0;
    pt_nth(data(clp), len(clp), 1u << 30, &ba, &bl);
    if ((t->box[b].background & 0xff) && bl && pt_is(data(clp) + ba, bl, "text")) pt_rrect(&k, bx, by, bx + bw, by + bh, rad, t->box[b].background);
    if (t->box[b].bg_image.len) pt_layers(t, nt, b, &k, icache, ipx, rad, 1);
    /* the mask: the glyphs of the text the box holds */
    vector<uint8_t> m;
    resize(&m, (size_t)c->w * (size_t)kh);
    for (uint32_t fr = 1; fr < t->frag_len; fr++) {
        layout_frag_kind kd = t->frag[fr].kind;
        if (kd != LAYOUT_FRAG_KIND_TEXT && kd != LAYOUT_FRAG_KIND_GENERATED) continue;
        if (t->frag[fr].y > k.oy + kh || t->frag[fr].y + t->frag[fr].h < k.oy) continue;
        uint32_t x = t->line[t->frag[fr].line].box;
        int in = 0;
        while (x) {
            if (x == b) { in = 1; break; }
            if (t->box[x].position == LAYOUT_POSITION_ABSOLUTE || t->box[x].position == LAYOUT_POSITION_FIXED) break;
            x = t->box[x].parent;
        }
        if (!in) continue;
        amb_span sp = t->frag[fr].text;
        pt_text_mask(&m, (int)c->w, k.oy, kh, f, entries, table, masks, acc, mask, text_ptr(t, sp), sp.len, t->frag[fr].x, t->frag[fr].baseline,
                     pt_face_of(t, fr, webmap), t->frag[fr].size, t->frag[fr].italic, t->frag[fr].spacing, t->frag[fr].word_spacing);
    }
    /* laid on through it */
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int y = 0; y < kh; y++) {
        int Y = r0 + y;
        if (Y < ky0 || Y >= ky1) continue;
        for (int X = kx0; X < kx1; X++) {
            uint32_t a = at(m, (size_t)y * c->w + (size_t)X);
            if (!a) continue;
            size_t p = ((size_t)Y * c->w + (size_t)X) * 3, q = ((size_t)y * c->w + (size_t)X) * 3;
            for (int ch = 0; ch < 3; ch++) c->px[p + ch] = (uint8_t)((at(k.px, q + ch) * a + c->px[p + ch] * (255 - a) + 127) / 255);
        }
    }
}
/* a box transformed: by its transform's functions or its individual transforms */
static int pt_transformed(const layout_tree *t, uint32_t b) {
    return t->box[b].transform.len || t->box[b].translate.len || t->box[b].rotate.len || t->box[b].scale.len;
}
/* a box whose children share its 3D space (transform-style: preserve-3d), unless a
   property that groups its content flattens it (an opacity, a filter, a clip) */
static int pt_p3d(const layout_tree *t, uint32_t b) {
    return t->box[b].p3d && t->box[b].opacity >= 1 && !t->box[b].filter.len && !t->box[b].clip && !t->box[b].cpath.len;
}
/* a box drawn as a plane of its parent's 3D rendering context: its parent preserves 3D
   and it is transformed or preserves 3D itself */
static int pt_in_3d(const layout_tree *t, uint32_t b) {
    uint32_t p = t->box[b].parent;
    return p && pt_p3d(t, p) && (pt_transformed(t, b) || pt_p3d(t, b));
}
/* a box that makes a stacking context of its own: a z-index, an opacity, a transform, a
   filter, a perspective, a 3D space of its own */
static int pt_context(const layout_tree *t, uint32_t b) {
    return !t->box[b].z_auto || t->box[b].opacity < 1 || pt_transformed(t, b) || t->box[b].filter.len || t->box[b].backdrop.len ||
           t->box[b].persp.len || t->box[b].p3d || t->box[b].cpath.len;
}

static void pt_flow(PCTX, uint32_t b, vector<uint32_t> *later, int with_box);
static void pt_ctx(PCTX, uint32_t b);
static void pt_effects_plain(PCTX, uint32_t b, int ctx);
static void pt_effects_draw(PCTX, uint32_t b, int ctx);
static void pt_order(PCTX, const vector<uint32_t> *list, int neg);

/* ---- transforms (CSS Transforms 1 and 2): 4x4 matrices, row by row,
   (x', y', z', w') = M (x, y, z, 1) ---- */

static void pt_m4_id(double *m) {
    for (int k = 0; k < 16; k++) m[k] = k % 5 == 0 ? 1 : 0;
}
/* out = a b (b applied first) */
static void pt_m4_mul(const double *a, const double *b, double *out) {
    double r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[4 * i + k] * b[4 * k + j];
            r[4 * i + j] = s;
        }
    for (int k = 0; k < 16; k++) out[k] = r[k];
}
/* m = m T(x, y, z) */
static void pt_m4_translate(double *m, double x, double y, double z) {
    double f[16];
    pt_m4_id(f);
    f[3] = x;
    f[7] = y;
    f[11] = z;
    pt_m4_mul(m, f, m);
}
/* the rotation by a radians about the axis (x, y, z) (CSS Transforms 2, rotate3d()) */
static void pt_m4_rotation(double x, double y, double z, double a, double *f) {
    pt_m4_id(f);
    double l = sqrt(x * x + y * y + z * z);
    if (l <= 0) return;
    x /= l;
    y /= l;
    z /= l;
    double sc = sin(a / 2) * cos(a / 2), sq = sin(a / 2) * sin(a / 2);
    f[0] = 1 - 2 * (y * y + z * z) * sq;
    f[1] = 2 * (x * y * sq - z * sc);
    f[2] = 2 * (x * z * sq + y * sc);
    f[4] = 2 * (x * y * sq + z * sc);
    f[5] = 1 - 2 * (x * x + z * z) * sq;
    f[6] = 2 * (y * z * sq - x * sc);
    f[8] = 2 * (x * z * sq - y * sc);
    f[9] = 2 * (y * z * sq + x * sc);
    f[10] = 1 - 2 * (x * x + y * y) * sq;
}

/* an argument of a transform function: an angle in radians, a percentage of base (of
   the box's width or height; a scale's of 1), a length in px or a number */
static double pt_targ(const char *t0, size_t xl, double base) {
    double x = strtod(t0, 0);
    if (xl > 3 && pt_is_ci(t0 + xl - 3, 3, "deg")) return x * M_PI / 180;
    if (xl > 4 && pt_is_ci(t0 + xl - 4, 4, "turn")) return x * 2 * M_PI;
    if (xl > 4 && pt_is_ci(t0 + xl - 4, 4, "grad")) return x * M_PI / 200;
    if (xl > 3 && pt_is_ci(t0 + xl - 3, 3, "rad")) return x;
    if (xl > 0 && t0[xl - 1] == '%') return x / 100 * base;
    return x;
}

/* a transform's functions s in turn into m (m = m F1 F2 ...); percentages of the box's
   w and h */
static void pt_functions(const char *s, size_t n, double w, double h, double *m) {
    size_t i = 0, pa, pl;
    while (css_next_part(s, n, &i, &pa, &pl)) {
        const char *fn = s + pa;
        size_t open = 0;
        while (open < pl && fn[open] != '(') open++;
        if (open >= pl || fn[pl - 1] != ')') continue;
        const char *args = fn + open + 1;
        size_t al = pl - open - 2, j = 0, xa, xl;
        int scale = open >= 5 && pt_is_ci(fn, 5, "scale");
        double v[16];
        int nv = 0;
        while (nv < 16 && css_next_part(args, al, &j, &xa, &xl)) {
            /* a translation's percentages: its first of the width, its second of the height */
            double base = scale ? 1 : (nv == 0 && !pt_is_ci(fn, open, "translateY")) ? w : h;
            v[nv++] = pt_targ(args + xa, xl, base);
        }
        double f[16];
        pt_m4_id(f);
        if (pt_is_ci(fn, open, "translate")) { f[3] = nv > 0 ? v[0] : 0; f[7] = nv > 1 ? v[1] : 0; }
        else if (pt_is_ci(fn, open, "translateX")) f[3] = nv > 0 ? v[0] : 0;
        else if (pt_is_ci(fn, open, "translateY")) f[7] = nv > 0 ? v[0] : 0;
        else if (pt_is_ci(fn, open, "translateZ")) f[11] = nv > 0 ? v[0] : 0;
        else if (pt_is_ci(fn, open, "translate3d") && nv == 3) { f[3] = v[0]; f[7] = v[1]; f[11] = v[2]; }
        else if (pt_is_ci(fn, open, "scale") && nv > 0) { f[0] = v[0]; f[5] = nv > 1 ? v[1] : v[0]; }
        else if (pt_is_ci(fn, open, "scaleX") && nv > 0) f[0] = v[0];
        else if (pt_is_ci(fn, open, "scaleY") && nv > 0) f[5] = v[0];
        else if (pt_is_ci(fn, open, "scaleZ") && nv > 0) f[10] = v[0];
        else if (pt_is_ci(fn, open, "scale3d") && nv == 3) { f[0] = v[0]; f[5] = v[1]; f[10] = v[2]; }
        else if ((pt_is_ci(fn, open, "rotate") || pt_is_ci(fn, open, "rotateZ")) && nv > 0) pt_m4_rotation(0, 0, 1, v[0], f);
        else if (pt_is_ci(fn, open, "rotateX") && nv > 0) pt_m4_rotation(1, 0, 0, v[0], f);
        else if (pt_is_ci(fn, open, "rotateY") && nv > 0) pt_m4_rotation(0, 1, 0, v[0], f);
        else if (pt_is_ci(fn, open, "rotate3d") && nv == 4) pt_m4_rotation(v[0], v[1], v[2], v[3], f);
        else if (pt_is_ci(fn, open, "skew") && nv > 0) { f[1] = tan(v[0]); f[4] = nv > 1 ? tan(v[1]) : 0; }
        else if (pt_is_ci(fn, open, "skewX") && nv > 0) f[1] = tan(v[0]);
        else if (pt_is_ci(fn, open, "skewY") && nv > 0) f[4] = tan(v[0]);
        else if (pt_is_ci(fn, open, "matrix") && nv == 6) { f[0] = v[0]; f[4] = v[1]; f[1] = v[2]; f[5] = v[3]; f[3] = v[4]; f[7] = v[5]; }
        else if (pt_is_ci(fn, open, "matrix3d") && nv == 16) {
            /* its values column by column */
            for (int r = 0; r < 4; r++)
                for (int k = 0; k < 4; k++) f[4 * r + k] = v[4 * k + r];
        } else if (pt_is_ci(fn, open, "perspective") && nv > 0) f[14] = -1 / (v[0] < 1 ? 1 : v[0]);
        else continue;
        pt_m4_mul(m, f, m);
    }
}

/* the individual transforms (CSS Transforms 2, 5) into m, translate, rotate, then scale */
static void pt_individual(const layout_tree *t, uint32_t b, double w, double h, double *m) {
    double v[4];
    int nv;
    size_t j, xa, xl;
    vector<char> tx;
    amb_span sp = t->box[b].translate;
    append_range(tx, text_ptr(t, sp), sp.len);
    for (nv = 0, j = 0; nv < 3 && css_next_part(data(tx), len(tx), &j, &xa, &xl); nv++) v[nv] = pt_targ(data(tx) + xa, xl, nv == 0 ? w : h);
    if (nv) pt_m4_translate(m, v[0], nv > 1 ? v[1] : 0, nv > 2 ? v[2] : 0);
    clear(tx);
    sp = t->box[b].rotate;
    append_range(tx, text_ptr(t, sp), sp.len);
    double ax = 0, ay = 0, az = 1, ang = 0;
    int have = 0;
    for (nv = 0, j = 0; css_next_part(data(tx), len(tx), &j, &xa, &xl);) {
        const char *p = data(tx) + xa;
        if (pt_is_ci(p, xl, "x")) { ax = 1; ay = 0; az = 0; }
        else if (pt_is_ci(p, xl, "y")) { ax = 0; ay = 1; az = 0; }
        else if (pt_is_ci(p, xl, "z")) { ax = 0; ay = 0; az = 1; }
        else if (xl > 0 && (p[xl - 1] | 0x20) >= 'a' && (p[xl - 1] | 0x20) <= 'z') { ang = pt_targ(p, xl, 0); have = 1; }
        else if (nv < 3) v[nv++] = strtod(p, 0);
    }
    if (nv == 3) { ax = v[0]; ay = v[1]; az = v[2]; }
    if (have) {
        double f[16];
        pt_m4_rotation(ax, ay, az, ang, f);
        pt_m4_mul(m, f, m);
    }
    clear(tx);
    sp = t->box[b].scale;
    append_range(tx, text_ptr(t, sp), sp.len);
    for (nv = 0, j = 0; nv < 3 && css_next_part(data(tx), len(tx), &j, &xa, &xl); nv++) v[nv] = pt_targ(data(tx) + xa, xl, 1);
    if (nv) {
        double f[16];
        pt_m4_id(f);
        f[0] = v[0];
        f[5] = nv > 1 ? v[1] : v[0];
        f[10] = nv > 2 ? v[2] : 1;
        pt_m4_mul(m, f, m);
    }
}

/* a box's border box */
static void pt_border_box(const layout_tree *t, uint32_t b, double *x, double *y, double *w, double *h) {
    *x = t->box[b].x - t->box[b].pl - t->box[b].bl;
    *y = t->box[b].y - t->box[b].pt - t->box[b].bt;
    *w = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    *h = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
}

/* an origin's text (transform-origin's, perspective-origin's) over a box w by h: its x and
   y (the middle when it has none), and its z */
static void pt_origin(const layout_tree *t, amb_span sp, double w, double h, double *ox, double *oy, double *oz) {
    *ox = w / 2;
    *oy = h / 2;
    *oz = 0;
    if (!sp.len) return;
    vector<char> ov;
    append_range(ov, text_ptr(t, sp), sp.len);
    pt_tile_pos(data(ov), len(ov), w, h, 0, 0, ox, oy);
    size_t j = 0, xa, xl;
    int k = 0;
    while (css_next_part(data(ov), len(ov), &j, &xa, &xl))
        if (++k == 3) *oz = strtod(data(ov) + xa, 0);
}

/* a box's transform in page coordinates into m: about its transform-origin, its individual
   transforms and then its transform's functions; seen through its parent's perspective
   about the parent's perspective-origin, when it has one (CSS Transforms 2, 6). 0 when
   the box is not transformed (m the identity) */
static int pt_box_matrix(const layout_tree *t, uint32_t b, double *m) {
    pt_m4_id(m);
    if (!pt_transformed(t, b)) return 0;
    double bx, by, bw, bh, ox, oy, oz;
    pt_border_box(t, b, &bx, &by, &bw, &bh);
    uint32_t p = t->box[b].parent;
    if (p && t->box[p].persp.len) {
        vector<char> pv;
        amb_span sp = t->box[p].persp;
        append_range(pv, text_ptr(t, sp), sp.len);
        double d = strtod(data(pv), 0), px, py, pw, ph, qx, qy, qz;
        if (d < 1) d = 1;
        pt_border_box(t, p, &px, &py, &pw, &ph);
        pt_origin(t, t->box[p].porigin, pw, ph, &qx, &qy, &qz);
        pt_m4_translate(m, px + qx, py + qy, 0);
        double f[16];
        pt_m4_id(f);
        f[14] = -1 / d;
        pt_m4_mul(m, f, m);
        pt_m4_translate(m, -(px + qx), -(py + qy), 0);
    }
    pt_origin(t, t->box[b].origin, bw, bh, &ox, &oy, &oz);
    pt_m4_translate(m, bx + ox, by + oy, oz);
    pt_individual(t, b, bw, bh, m);
    if (t->box[b].transform.len) {
        vector<char> tv;
        amb_span sp = t->box[b].transform;
        append_range(tv, text_ptr(t, sp), sp.len);
        pt_functions(data(tv), len(tv), bw, bh, m);
    }
    pt_m4_translate(m, -(bx + ox), -(by + oy), -oz);
    return 1;
}

/* a box (with its content) drawn through its matrix m onto its plane: drawn onto black
   and onto white as it is (its alpha their difference), then each pixel of the canvas
   sampled from where the plane's homography (m flattened: its plane's z is 0) takes it
   back, where the plane is in front of the viewer. Not drawn when its back faces the
   viewer and its backface-visibility is hidden (Chromium's test: the minor of m without
   z against m's determinant) */
static void pt_plane_draw(PCTX, uint32_t b, int ctx, const double *m) {
    double hm[9] = { m[0], m[1], m[3], m[4], m[5], m[7], m[12], m[13], m[15] };
    double dh = hm[0] * (hm[4] * hm[8] - hm[5] * hm[7]) - hm[1] * (hm[3] * hm[8] - hm[5] * hm[6]) + hm[2] * (hm[3] * hm[7] - hm[4] * hm[6]);
    if (fabs(dh) < 1e-12) return;
    if (t->box[b].backface) {
        /* m's determinant, by its first row's cofactors */
        double dm = 0;
        for (int k = 0; k < 4; k++) {
            double s3[9];
            int q = 0;
            for (int r = 1; r < 4; r++)
                for (int cc = 0; cc < 4; cc++)
                    if (cc != k) s3[q++] = m[4 * r + cc];
            double d3 = s3[0] * (s3[4] * s3[8] - s3[5] * s3[7]) - s3[1] * (s3[3] * s3[8] - s3[5] * s3[6]) + s3[2] * (s3[3] * s3[7] - s3[4] * s3[6]);
            dm += (k % 2 ? -1 : 1) * m[k] * d3;
        }
        if (dh * dm < 0) return;
    }
    /* the inverse homography: the adjugate (its scale does not matter) */
    double iv[9] = { hm[4] * hm[8] - hm[5] * hm[7], hm[2] * hm[7] - hm[1] * hm[8], hm[1] * hm[5] - hm[2] * hm[4],
                     hm[5] * hm[6] - hm[3] * hm[8], hm[0] * hm[8] - hm[2] * hm[6], hm[2] * hm[3] - hm[0] * hm[5],
                     hm[3] * hm[7] - hm[4] * hm[6], hm[1] * hm[6] - hm[0] * hm[7], hm[0] * hm[4] - hm[1] * hm[3] };
    amb_canvas k0, k1;
    memset(&k0, 0, sizeof k0);
    memset(&k1, 0, sizeof k1);
    defer(k0) { AMB_CANVAS_DROP(k0); }
    defer(k1) { AMB_CANVAS_DROP(k1); }
    k0.w = k1.w = c->w;
    k0.h = k1.h = c->h;
    k0.oy = k1.oy = c->oy;
    k0.zoom = k1.zoom = c->zoom;
    k0.cx0 = k1.cx0 = 0;
    k0.cy0 = k1.cy0 = c->oy;
    k0.cx1 = k1.cx1 = c->w;
    k0.cy1 = k1.cy1 = c->oy + c->h;
    resize(&k0.px, (size_t)c->w * c->h * 3);
    resize(&k1.px, (size_t)c->w * c->h * 3);
    for (size_t q = 0; q < len(k1.px); q++) k1.px[q] = 255;
    pt_effects_plain(PARGS_C(&k0), b, ctx);
    pt_effects_plain(PARGS_C(&k1), b, ctx);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int y = ky0; y < ky1; y++)
        for (int x = kx0; x < kx1; x++) {
            double X = x + 0.5, Y = y + 0.5 + c->oy;
            double sw = iv[6] * X + iv[7] * Y + iv[8];
            /* the sign of the plane's w where it is seen: behind the viewer when not that of det */
            if (sw * dh <= 0) continue;
            double sx = (iv[0] * X + iv[1] * Y + iv[2]) / sw - 0.5, sy = (iv[3] * X + iv[4] * Y + iv[5]) / sw - 0.5 - c->oy;
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
}

/* a plane of a 3D rendering context: its box, its matrix, its depth (its middle's z) */
typedef struct pt_plane { uint32_t box; double m[16]; double z; } pt_plane;

/* box b, whose matrix is m, and the planes of the 3D space it shares with its children
   (when it preserves 3D: each child transformed or preserving 3D, by m and its own
   matrix), into out */
static void pt_planes(const layout_tree *t, uint32_t b, const double *m, vector<pt_plane> *out) {
    pt_plane p;
    memset(&p, 0, sizeof p);
    p.box = b;
    for (int k = 0; k < 16; k++) p.m[k] = m[k];
    double bx, by, bw, bh;
    pt_border_box(t, b, &bx, &by, &bw, &bh);
    p.z = m[8] * (bx + bw / 2) + m[9] * (by + bh / 2) + m[11];
    push_back(*out, p);
    if (!pt_p3d(t, b)) return;
    for (uint32_t ch = t->box[b].first_child; ch; ch = t->box[ch].next) {
        if (!pt_in_3d(t, ch)) continue;
        double mc[16], mm[16];
        pt_box_matrix(t, ch, mc);
        pt_m4_mul(m, mc, mm);
        pt_planes(t, ch, mm, out);
    }
}

/* a box (with its content) drawn by its effects: in the 3D space it makes for its children
   when it preserves 3D, each of its planes drawn by its matrix, those farther first (by
   their middles' depth); else through its matrix when it is transformed; else by its
   opacity, filters and clip */
static void pt_effects(PCTX, uint32_t b, int ctx) {
    double m[16];
    int moved = pt_box_matrix(t, b, m);
    if (pt_p3d(t, b)) {
        vector<pt_plane> planes;
        pt_planes(t, b, m, &planes);
        for (size_t k = 1; k < len(planes); k++) {
            pt_plane x = at(planes, k);
            size_t q = k;
            while (q > 0 && at(planes, q - 1).z > x.z) { planes[q] = at(planes, q - 1); q--; }
            planes[q] = x;
        }
        for (size_t k = 0; k < len(planes); k++) {
            pt_plane x = at(planes, k);
            if (x.box == b && !moved) pt_effects_plain(PARGS, b, ctx);
            else pt_plane_draw(PARGS, x.box, x.box == b ? ctx : 1, x.m);
        }
        return;
    }
    if (moved) { pt_plane_draw(PARGS, b, ctx, m); return; }
    pt_effects_plain(PARGS, b, ctx);
}

/* a box drawn as it is, with its content: as the stacking context it makes (ctx), else as
   a box in the flow, and the positioned boxes in it */
static void pt_effects_draw(PCTX, uint32_t b, int ctx) {
    if (ctx) pt_ctx(PARGS, b);
    else {
        vector<uint32_t> later;
        pt_flow(PARGS, b, &later, 1);
        for (size_t q = 0; q < len(later); q++) pt_ctx(PARGS, at(later, q));
    }
}

/* a filter's value (a box's span sp) as its functions; none when it has none or names an
   SVG filter */
static void pt_filter_of(const layout_tree *t, amb_span sp, vector<pt_fop> *ops) {
    if (!sp.len) return;
    vector<char> fv;
    append_range(fv, text_ptr(t, sp), sp.len);
    if (!pt_filter_ops(data(fv), len(fv), ops)) clear(*ops);
}

/* a box and its content drawn through its filter's functions: drawn onto black and onto
   white (its colours premultiplied and its cover read from the two), the functions
   applied over what was drawn and as far as they reach, and the result laid on the
   canvas within its clip */
static void pt_filtered(PCTX, uint32_t b, int ctx, const vector<pt_fop> *ops) {
    amb_canvas k0, k1;
    memset(&k0, 0, sizeof k0);
    memset(&k1, 0, sizeof k1);
    defer(k0) { AMB_CANVAS_DROP(k0); }
    defer(k1) { AMB_CANVAS_DROP(k1); }
    k0.w = k1.w = c->w;
    k0.h = k1.h = c->h;
    k0.oy = k1.oy = c->oy;
    k0.zoom = k1.zoom = c->zoom;
    k0.cx0 = k1.cx0 = 0;
    k0.cy0 = k1.cy0 = c->oy;
    k0.cx1 = k1.cx1 = c->w;
    k0.cy1 = k1.cy1 = c->oy + c->h;
    resize(&k0.px, (size_t)c->w * c->h * 3);
    resize(&k1.px, (size_t)c->w * c->h * 3);
    for (size_t q = 0; q < len(k1.px); q++) k1.px[q] = 255;
    pt_effects_draw(PARGS_C(&k0), b, ctx);
    pt_effects_draw(PARGS_C(&k1), b, ctx);
    /* what was drawn: a pixel not black on black or not white on white */
    int x0 = (int)c->w, y0 = (int)c->h, x1 = -1, y1 = -1;
    for (int y = 0; y < (int)c->h; y++)
        for (int x = 0; x < (int)c->w; x++) {
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            if (at(k0.px, i) | at(k0.px, i + 1) | at(k0.px, i + 2) || (at(k1.px, i) & at(k1.px, i + 1) & at(k1.px, i + 2)) != 255) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    if (x1 < 0) return;
    int reach = pt_filter_reach(ops);
    x0 = x0 - reach < 0 ? 0 : x0 - reach;
    y0 = y0 - reach < 0 ? 0 : y0 - reach;
    x1 = x1 + reach + 1 > (int)c->w ? (int)c->w : x1 + reach + 1;
    y1 = y1 + reach + 1 > (int)c->h ? (int)c->h : y1 + reach + 1;
    int w = x1 - x0, h = y1 - y0;
    if ((double)w * h > 4e7) return;
    size_t np = (size_t)w * h;
    vector<float> px;
    resize(&px, 4 * np);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = ((size_t)(y0 + y) * c->w + (size_t)(x0 + x)) * 3, q = (size_t)y * w + x;
            double d = (at(k1.px, i) - at(k0.px, i) + at(k1.px, i + 1) - at(k0.px, i + 1) + at(k1.px, i + 2) - at(k0.px, i + 2)) / 765.0;
            px[q] = at(k0.px, i) / 255.0f;
            px[np + q] = at(k0.px, i + 1) / 255.0f;
            px[2 * np + q] = at(k0.px, i + 2) / 255.0f;
            px[3 * np + q] = (float)(d < 0 ? 1 : d > 1 ? 0 : 1 - d);
        }
    pt_filter_apply(&px, w, h, ops);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    for (int y = y0 < ky0 ? ky0 : y0; y < y1 && y < ky1; y++)
        for (int x = x0 < kx0 ? kx0 : x0; x < x1 && x < kx1; x++) {
            size_t q = (size_t)(y - y0) * w + (size_t)(x - x0), di = ((size_t)y * c->w + (size_t)x) * 3;
            double al = at(px, 3 * np + q);
            if (al <= 0.001) continue;
            if (al > 1) al = 1;
            for (int ch = 0; ch < 3; ch++) {
                double v = at(px, (size_t)ch * np + q) * 255 + c->px[di + ch] * (1 - al);
                c->px[di + ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
        }
}

/* backdrop-filter: what the canvas holds under a box's border box, through its functions,
   laid back within the box as its corners round it, before the box is drawn over it. A
   blur reaches past the box's edges by mirroring what is within them (Chromium blurs a
   backdrop with its edges mirrored) */
static void pt_backdrop(const layout_tree *t, uint32_t b, amb_canvas *c) {
    vector<pt_fop> ops;
    pt_filter_of(t, t->box[b].backdrop, &ops);
    if (!len(ops)) return;
    double bx = t->box[b].x - t->box[b].pl - t->box[b].bl, by = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double rad[8] = { t->box[b].rtl, t->box[b].rtr, t->box[b].rbr, t->box[b].rbl,
                      t->box[b].rtlv, t->box[b].rtrv, t->box[b].rbrv, t->box[b].rblv };
    pt_fit_radii(bw, bh, rad);
    int kx0, ky0, kx1, ky1;
    pt_clip_px(c, &kx0, &ky0, &kx1, &ky1);
    int x0 = (int)floor(bx), y0 = (int)floor(by - c->oy), x1 = (int)ceil(bx + bw), y1 = (int)ceil(by + bh - c->oy);
    if (x0 < kx0) x0 = kx0;
    if (y0 < ky0) y0 = ky0;
    if (x1 > kx1) x1 = kx1;
    if (y1 > ky1) y1 = ky1;
    int iw = x1 - x0, ih = y1 - y0;
    if (iw <= 0 || ih <= 0) return;
    int m = pt_filter_reach(&ops), w = iw + 2 * m, h = ih + 2 * m;
    if ((double)w * h > 4e7) return;
    size_t np = (size_t)w * h;
    vector<float> px;
    resize(&px, 4 * np);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            /* mirrored about the region's edges */
            int sx = x - m, sy = y - m;
            while (sx < 0 || sx >= iw) sx = sx < 0 ? -sx - 1 : 2 * iw - sx - 1;
            while (sy < 0 || sy >= ih) sy = sy < 0 ? -sy - 1 : 2 * ih - sy - 1;
            size_t i = ((size_t)(y0 + sy) * c->w + (size_t)(x0 + sx)) * 3, q = (size_t)y * w + x;
            px[q] = c->px[i] / 255.0f;
            px[np + q] = c->px[i + 1] / 255.0f;
            px[2 * np + q] = c->px[i + 2] / 255.0f;
            px[3 * np + q] = 1;
        }
    pt_filter_apply(&px, w, h, &ops);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double cov = pt_rr_cover(x + 0.5, y + 0.5 + c->oy, bx, by, bx + bw, by + bh, rad);
            if (cov <= 0) continue;
            size_t q = (size_t)(y - y0 + m) * w + (size_t)(x - x0 + m), di = ((size_t)y * c->w + (size_t)x) * 3;
            double al = at(px, 3 * np + q);
            for (int ch = 0; ch < 3; ch++) {
                /* the backdrop is opaque: what the functions leave transparent shows white */
                double f = at(px, (size_t)ch * np + q) * 255 + 255 * (1 - (al > 1 ? 1 : al));
                double v = f * cov + c->px[di + ch] * (1 - cov);
                c->px[di + ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
        }
}

/* ---- clip-path's basic shapes (CSS Masking 1, 5.1; CSS Shapes 1, 3) ---- */

/* the reference box of a clip-path (named by w, wn; the border box when none is named) in page coordinates: x0 y0 x1
   y1 into r, its corners' radii (the border box's, less the border and padding inside it, more the margin outside)
   into rad */
static void pt_ref_box(const layout_tree *t, uint32_t b, const char *w, size_t wn, double *r, double *rad) {
    layout_box x = t->box[b];
    double brad[8] = { x.rtl, x.rtr, x.rbr, x.rbl, x.rtlv, x.rtrv, x.rbrv, x.rblv };
    double bw = x.w + x.pl + x.pr + x.bl + x.br, bh = x.h + x.pt + x.pb + x.bt + x.bb;
    pt_fit_radii(bw, bh, brad);
    if (pt_is(w, wn, "margin-box")) {
        r[0] = x.x - x.pl - x.bl - x.ml;
        r[1] = x.y - x.pt - x.bt - x.mt;
        r[2] = x.x + x.w + x.pr + x.br + x.mr;
        r[3] = x.y + x.h + x.pb + x.bb + x.mb;
        double grow[4] = { x.ml, x.mr, x.mr, x.ml };
        double growv[4] = { x.mt, x.mt, x.mb, x.mb };
        for (int k = 0; k < 4; k++) {
            rad[k] = brad[k] > 0 ? brad[k] + grow[k] : 0;
            rad[k + 4] = brad[k + 4] > 0 ? brad[k + 4] + growv[k] : 0;
        }
        return;
    }
    pt_area(t, b, pt_is(w, wn, "padding-box") ? "padding-box" : pt_is(w, wn, "content-box") ? "content-box" : "border-box",
            pt_is(w, wn, "padding-box") ? 11 : pt_is(w, wn, "content-box") ? 11 : 10, brad, r, rad);
}

/* a part of a clip-path's text (a <length-percentage> made px, a percentage, or a calc() of both) as device px: its
   percentage of basis, its px at the zoom z */
static double pt_clip_lp(const char *s, size_t n, double basis, double z) { return pt_opos_axis(s, n, basis, z, 0); }

/* the text's parts a space or a comma apart (functions kept whole), as css_next_part finds them */
static size_t pt_clip_parts(const char *s, size_t n, size_t *st, size_t *ln, size_t max) {
    size_t i = 0, k = 0, a, l;
    while (k < max && css_next_part(s, n, &i, &a, &l)) { st[k] = a; ln[k] = l; k++; }
    return k;
}

/* the shape clip-path's text names for box b (its reference box ref): a rounded rectangle (1: rr x0 y0 x1 y1, radii
   rad), a path (2: its commands, page coordinates, its rule), or none at all (0: nothing is drawn) */
static int pt_clip_shape(const layout_tree *t, uint32_t b, double z, double *rr, double *rad, vector<double> *path, int *evenodd) {
    vector<char> cp;
    append_range(cp, text_ptr(t, t->box[b].cpath), t->box[b].cpath.len);
    const char *s = data(cp);
    size_t n = len(cp), open = 0;
    while (open < n && s[open] != '(' && s[open] != ' ') open++;
    size_t close = open, depth = 0;
    int fn = open < n && s[open] == '(';
    if (fn) {
        for (close = open; close < n; close++) {
            if (s[close] == '"') { close++; while (close < n && s[close] != '"') close++; continue; }
            if (s[close] == '(') depth++;
            else if (s[close] == ')' && --depth == 0) break;
        }
        if (close >= n) return 0;
    }
    /* the reference box: the word after the shape, or the whole text */
    size_t bs = fn ? close + 1 : 0;
    while (bs < n && s[bs] == ' ') bs++;
    double ref[4], brad[8];
    pt_ref_box(t, b, s + bs, n - bs, ref, brad);
    double w = ref[2] - ref[0], h = ref[3] - ref[1];
    for (int k = 0; k < 8; k++) rad[k] = 0;
    if (!fn) {
        /* a box alone: its edges, its corners rounded as the box's are */
        for (int k = 0; k < 4; k++) rr[k] = ref[k];
        for (int k = 0; k < 8; k++) rad[k] = brad[k];
        return 1;
    }
    const char *a = s + open + 1;
    size_t an = close - open - 1, st[16], ln[16];
    if (pt_is(s, open, "inset")) {
        size_t k = pt_clip_parts(a, an, st, ln, 16), r = k;
        for (size_t j = 0; j < k; j++) if (pt_is(a + st[j], ln[j], "round")) { r = j; break; }
        if (r == 0 || r > 4) return 0;
        double o[4];
        for (size_t j = 0; j < 4; j++) {
            size_t q = r == 1 ? 0 : r == 2 ? j % 2 : r == 3 ? (j == 3 ? 1 : j) : j;
            o[j] = pt_clip_lp(a + st[q], ln[q], j % 2 ? w : h, z);
        }
        rr[0] = ref[0] + o[3];
        rr[1] = ref[1] + o[0];
        rr[2] = fmax(rr[0], ref[2] - o[1]);
        rr[3] = fmax(rr[1], ref[3] - o[2]);
        if (r < k) {
            /* the radii: border-radius's lists, their percentages of the reference box's size */
            size_t slash = k;
            for (size_t j = r + 1; j < k; j++) if (pt_is(a + st[j], ln[j], "/")) { slash = j; break; }
            for (int side = 0; side < 2; side++) {
                size_t from = side == 0 ? r + 1 : slash + 1, to = side == 0 ? slash : k;
                if (side == 1 && slash == k) { for (int q = 0; q < 4; q++) rad[4 + q] = rad[q]; break; }
                size_t m = to - from;
                if (m == 0 || m > 4) return 0;
                for (int q = 0; q < 4; q++) {
                    size_t pick = m == 1 ? 0 : m == 2 ? (size_t)(q % 2) : m == 3 ? (q == 3 ? 1 : (size_t)q) : (size_t)q;
                    rad[side * 4 + q] = fmax(0, pt_clip_lp(a + st[from + pick], ln[from + pick], side == 0 ? w : h, z));
                }
            }
            pt_fit_radii(rr[2] - rr[0], rr[3] - rr[1], rad);
        }
        return 1;
    }
    if (pt_is(s, open, "circle") || pt_is(s, open, "ellipse")) {
        int ellipse = pt_is(s, open, "ellipse");
        size_t k = pt_clip_parts(a, an, st, ln, 16), at_i = k;
        for (size_t j = 0; j < k; j++) if (pt_is(a + st[j], ln[j], "at")) { at_i = j; break; }
        double cx = ref[0] + w / 2, cy = ref[1] + h / 2;
        if (at_i + 2 < k) {
            cx = ref[0] + pt_clip_lp(a + st[at_i + 1], ln[at_i + 1], w, z);
            cy = ref[1] + pt_clip_lp(a + st[at_i + 2], ln[at_i + 2], h, z);
        }
        double dl = fabs(cx - ref[0]), dr = fabs(ref[2] - cx), dt = fabs(cy - ref[1]), db = fabs(ref[3] - cy), rx, ry;
        if (!ellipse) {
            int far = at_i >= 1 && pt_is(a + st[0], ln[0], "farthest-side"), lp = at_i >= 1 && !far && !pt_is(a + st[0], ln[0], "closest-side");
            rx = far ? fmax(fmax(dl, dr), fmax(dt, db)) : fmin(fmin(dl, dr), fmin(dt, db));
            if (lp) rx = fmax(0, pt_clip_lp(a + st[0], ln[0], sqrt((w * w + h * h) / 2), z));
            ry = rx;
        } else {
            rx = fmin(dl, dr);
            ry = fmin(dt, db);
            if (at_i >= 2) {
                if (pt_is(a + st[0], ln[0], "farthest-side")) rx = fmax(dl, dr);
                else if (!pt_is(a + st[0], ln[0], "closest-side")) rx = fmax(0, pt_clip_lp(a + st[0], ln[0], w, z));
                if (pt_is(a + st[1], ln[1], "farthest-side")) ry = fmax(dt, db);
                else if (!pt_is(a + st[1], ln[1], "closest-side")) ry = fmax(0, pt_clip_lp(a + st[1], ln[1], h, z));
            }
        }
        if (rx <= 0 || ry <= 0) return 0;
        rr[0] = cx - rx;
        rr[1] = cy - ry;
        rr[2] = cx + rx;
        rr[3] = cy + ry;
        for (int q = 0; q < 4; q++) { rad[q] = rx; rad[q + 4] = ry; }
        return 1;
    }
    if (pt_is(s, open, "polygon")) {
        size_t i = 0;
        int first = 1;
        clear(*path);
        while (i < an) {
            size_t e = i;
            while (e < an && a[e] != ',') { if (a[e] == '(') { int dp = 0; for (; e < an; e++) { if (a[e] == '(') dp++; else if (a[e] == ')' && --dp == 0) break; } } e++; }
            if (e > an) e = an;
            size_t pk = pt_clip_parts(a + i, e - i, st, ln, 3);
            if (pk == 1 && pt_is(a + i + st[0], ln[0], "evenodd")) *evenodd = 1;
            else if (pk == 2) {
                push_back(*path, first ? 'M' : 'L');
                push_back(*path, ref[0] + pt_clip_lp(a + i + st[0], ln[0], w, z));
                push_back(*path, ref[1] + pt_clip_lp(a + i + st[1], ln[1], h, z));
                first = 0;
            }
            i = e + 1;
        }
        if (first) return 0;
        push_back(*path, 'Z');
        return 2;
    }
    if (pt_is(s, open, "path")) {
        size_t q0 = 0;
        while (q0 < an && a[q0] != '"') q0++;
        size_t q1 = q0 + 1;
        while (q1 < an && a[q1] != '"') q1++;
        if (q1 >= an) return 0;
        for (size_t j = 0; j < q0; j++) if (a[j] == 'e') { *evenodd = 1; break; }   /* evenodd, before the string */
        vector<double> raw;
        clear(*path);
        if (!amb_svg_path(a + q0 + 1, q1 - q0 - 1, &raw)) return 0;
        /* its coordinates CSS px from the reference box's top left */
        for (size_t j = 0; j < len(raw);) {
            int c = (int)at(raw, j);
            push_back(*path, at(raw, j));
            j++;
            int pts = c == 'M' || c == 'L' ? 1 : c == 'C' ? 3 : 0;
            for (int q = 0; q < pts && j + 1 < len(raw); q++, j += 2) {
                push_back(*path, ref[0] + at(raw, j) * z);
                push_back(*path, ref[1] + at(raw, j + 1) * z);
            }
        }
        return 2;
    }
    return 0;
}

/* a box's stacking context drawn over what was under it (before), held to its clip-path's shape: each pixel moved
   from what it was by as much of it as the shape covers, anti-aliased */
static void pt_clip_path(const layout_tree *t, uint32_t b, amb_canvas *c, const vector<uint8_t> *before) {
    double z = c->zoom > 0 ? c->zoom : 1, rr[4] = { 0, 0, 0, 0 }, rad[8];
    vector<double> path;
    int evenodd = 0;
    int kind = pt_clip_shape(t, b, z, rr, rad, &path, &evenodd);
    int bx[4] = { 0, 0, 0, 0 };
    vector<float> cov;
    int got = 0;
    if (kind == 2) {
        double m[6] = { 1, 0, 0, 1, 0, -c->oy };
        got = amb_path_coverage(&path, m, evenodd, (int)c->w, (int)c->h, bx, &cov);
    }
    for (uint32_t y = 0; y < c->h; y++)
        for (uint32_t x = 0; x < c->w; x++) {
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            if (c->px[i] == at(*before, i) && c->px[i + 1] == at(*before, i + 1) && c->px[i + 2] == at(*before, i + 2)) continue;
            double cv = 0;
            if (kind == 1) cv = pt_rr_cover(x + 0.5, y + 0.5 + c->oy, rr[0], rr[1], rr[2], rr[3], rad);
            else if (kind == 2 && got && (int)x >= bx[0] && (int)x < bx[2] && (int)y >= bx[1] && (int)y < bx[3])
                cv = at(cov, (size_t)((int)y - bx[1]) * (size_t)(bx[2] - bx[0]) + (size_t)((int)x - bx[0]));
            if (cv >= 1) continue;
            for (int ch = 0; ch < 3; ch++) {
                double v0 = at(*before, i + (size_t)ch);
                c->px[i + (size_t)ch] = (uint8_t)(v0 + (c->px[i + (size_t)ch] - v0) * cv + 0.5);
            }
        }
}

/* a box drawn with its opacity, backdrop and filters: through a copy of what was under it
   when it has an opacity, blended with it */
static void pt_effects_plain(PCTX, uint32_t b, int ctx) {
    double op = t->box[b].opacity;
    if (op <= 0) return;
    vector<uint8_t> under, unclipped;
    if (op < 1) append_range(under, data(c->px), len(c->px));
    if (t->box[b].cpath.len) append_range(unclipped, data(c->px), len(c->px));
    if (t->box[b].backdrop.len) pt_backdrop(t, b, c);
    vector<pt_fop> ops;
    pt_filter_of(t, t->box[b].filter, &ops);
    if (len(ops)) pt_filtered(PARGS, b, ctx, &ops);
    else pt_effects_draw(PARGS, b, ctx);
    if (op < 1)
        for (size_t q = 0; q < len(c->px); q++) {
            int o = at(under, q), n0 = c->px[q];
            if (o != n0) c->px[q] = (uint8_t)floor(o + (n0 - o) * op + 0.5);
        }
    if (t->box[b].cpath.len) pt_clip_path(t, b, c, &unclipped);
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
        if (pt_in_3d(t, x)) continue;
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
    if (with_box && t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) {
        pt_box(t, nt, b, c, skip_bg, icache, ipx);
        pt_text_bg(PARGS, b);
    }
    double save[4] = { c->cx0, c->cy0, c->cx1, c->cy1 };
    int clips = t->box[b].clip != 0;
    if (clips) {
        c->cx0 = fmax(c->cx0, t->box[b].x - t->box[b].pl);
        c->cy0 = fmax(c->cy0, t->box[b].y - t->box[b].pt);
        c->cx1 = fmin(c->cx1, t->box[b].x + t->box[b].w + t->box[b].pr);
        c->cy1 = fmin(c->cy1, t->box[b].y + t->box[b].h + t->box[b].pb);
    }
    pt_lines(t, nt, b, c, f, webmap, entries, table, masks, acc, mask, icache, ipx);
    vector<uint32_t> inner;
    for (uint32_t ch = t->box[b].first_child; ch; ch = t->box[ch].next) {
        if (pt_positioned(t, ch)) {
            if (clips) push_back(inner, ch);
            else push_back(later, ch);
            continue;
        }
        if (pt_in_3d(t, ch)) continue;
        if (t->box[ch].opacity < 1 || pt_transformed(t, ch) || t->box[ch].filter.len || t->box[ch].backdrop.len || t->box[ch].p3d ||
            t->box[ch].cpath.len) {
            pt_effects(PARGS, ch, 0);
            continue;
        }
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
    if (t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) {
        pt_box(t, nt, b, c, skip_bg, icache, ipx);
        pt_text_bg(PARGS, b);
    }
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
       transparent (that box then does not draw it again), over the viewport's: the Canvas
       of the root's color scheme */
    uint32_t canvas = vp && (t->box[vp].background & 0xff) == 0xff ? t->box[vp].background : 0xffffffff;
    uint32_t bg = canvas, skip = 0;
    uint32_t root = vp ? t->box[vp].first_child : 0;
    if (root && (t->box[root].background & 0xff)) { bg = t->box[root].background; skip = root; }
    else if (root) {
        for (uint32_t ch = t->box[root].first_child; ch; ch = t->box[ch].next)
            if (t->box[ch].kind == LAYOUT_BOX_KIND_BLOCK && (t->box[ch].background & 0xff)) { bg = t->box[ch].background; skip = ch; break; }
    }
    c->background = bg | 0xff;
    if ((bg & 0xff) != 0xff) pt_fill(c, 0, 0, c->w, c->h, canvas);
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

/* ---- zoom: the page drawn at the device's px, its text set at the larger size ---- */

static int pt_digit(char c) { return c >= '0' && c <= '9'; }
static int pt_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int pt_alnum(char c) { return pt_digit(c) || pt_alpha(c); }

/* the text s with each px length scaled by z (url()s and strings as they are), into out */
static void pt_scale_px(const char *s, size_t n, double z, vector<char> *out) {
    size_t i = 0;
    while (i < n) {
        char ch = s[i];
        int start = i == 0 || !(pt_alnum(s[i - 1]) || s[i - 1] == '-' || s[i - 1] == '_' || s[i - 1] == '#' || s[i - 1] == '.');
        if (ch == '"' || ch == '\'') {
            size_t e = i + 1;
            while (e < n && s[e] != ch) e += s[e] == '\\' ? 2 : 1;
            if (e > n) e = n;
            size_t stop = e < n ? e + 1 : n;
            append_range(*out, s + i, stop - i);
            i = stop;
            continue;
        }
        if (start && i + 4 <= n && pt_is_ci(s + i, 4, "url(")) {
            size_t e = i + 4;
            while (e < n && s[e] != ')') e++;
            size_t stop = e < n ? e + 1 : n;
            append_range(*out, s + i, stop - i);
            i = stop;
            continue;
        }
        if (start && (pt_digit(ch) || ((ch == '.' || ch == '-' || ch == '+') && i + 1 < n &&
                                                     (pt_digit(s[i + 1]) || s[i + 1] == '.')))) {
            /* a number: its sign, digits, point and exponent; then its unit */
            size_t e = i + (ch == '-' || ch == '+');
            while (e < n && (pt_digit(s[e]) || s[e] == '.')) e++;
            if (e + 1 < n && (s[e] == 'e' || s[e] == 'E') && (pt_digit(s[e + 1]) ||
                                                           ((s[e + 1] == '-' || s[e + 1] == '+') && e + 2 < n && pt_digit(s[e + 2])))) {
                e += 2;
                while (e < n && pt_digit(s[e])) e++;
            }
            size_t u = e;
            while (u < n && pt_alpha(s[u])) u++;
            if (u - e == 2 && pt_is_ci(s + e, 2, "px") && e - i < 40) {
                char num[48], buf[48];
                memcpy(num, s + i, e - i);
                num[e - i] = 0;
                int bn = snprintf(buf, sizeof buf, "%gpx", strtod(num, 0) * z);
                if (bn > 0) append_range(*out, buf, (size_t)bn);
            } else append_range(*out, s + i, u - i);
            i = u;
            continue;
        }
        push_back(*out, ch);
        i++;
    }
}

/* a value of zt's (its span sp) with its px lengths scaled by z: a new span when that
   changed it */
static amb_span pt_zoom_span(layout_tree *zt, amb_span sp, double z) {
    if (!sp.len) return sp;
    vector<char> src, out;
    append_range(src, text_ptr(zt, sp), sp.len);
    pt_scale_px(data(src), len(src), z, &out);
    if (len(out) == len(src) && memcmp(data(out), data(src), len(src)) == 0) return sp;
    return text_add(zt, data(out), len(out));
}

/* the tree t copied into zt (empty) with every length paint reads scaled by z: each box's
   and line's geometry, a box's radii and outline, a fragment's place, font size and
   spacing, and the px lengths in the values paint reads as text. The records keep their
   indices and the text its offsets (the pool copied whole after its first byte) */
static void pt_zoom_tree(const layout_tree *t, double z, layout_tree *zt) {
    if (t->text_len > 1) {
        amb_span all = { 1, (uint32_t)(t->text_len - 1) };
        text_add(zt, text_ptr(t, all), all.len);
    }
    for (uint32_t i = 1; i < t->box_len; i++) {
        box_new(zt);
        layout_box *b = box_slot(zt, i);
        *b = t->box[i];
        b->x *= z; b->y *= z; b->w *= z; b->h *= z;
        b->mt *= z; b->mr *= z; b->mb *= z; b->ml *= z;
        b->bt *= z; b->br *= z; b->bb *= z; b->bl *= z;
        b->pt *= z; b->pr *= z; b->pb *= z; b->pl *= z;
        b->avail *= z; b->dx *= z; b->dy *= z;
        b->rtl *= z; b->rtr *= z; b->rbr *= z; b->rbl *= z;
        b->rtlv *= z; b->rtrv *= z; b->rbrv *= z; b->rblv *= z;
        b->ow *= z; b->ooffset *= z;
    }
    for (uint32_t i = 1; i < t->box_len; i++) {
        amb_span sp[11] = { t->box[i].shadows, t->box[i].bg_image, t->box[i].bg_pos, t->box[i].bg_size, t->box[i].transform, t->box[i].origin,
                            t->box[i].filter, t->box[i].backdrop, t->box[i].translate, t->box[i].persp, t->box[i].porigin };
        for (int k = 0; k < 11; k++) sp[k] = pt_zoom_span(zt, sp[k], z);
        layout_box *b = box_slot(zt, i);
        b->shadows = sp[0];
        b->bg_image = sp[1];
        b->bg_pos = sp[2];
        b->bg_size = sp[3];
        b->transform = sp[4];
        b->origin = sp[5];
        b->filter = sp[6];
        b->backdrop = sp[7];
        b->translate = sp[8];
        b->persp = sp[9];
        b->porigin = sp[10];
    }
    for (uint32_t i = 1; i < t->line_len; i++) {
        line_new(zt);
        layout_line *l = line_slot(zt, i);
        *l = t->line[i];
        l->x *= z;
        l->y *= z;
        l->w *= z;
        l->h *= z;
        l->baseline *= z;
    }
    for (uint32_t i = 1; i < t->frag_len; i++) {
        frag_new(zt);
        amb_span sh = pt_zoom_span(zt, t->frag[i].shadows, z);
        layout_frag *f = frag_slot(zt, i);
        *f = t->frag[i];
        f->x *= z;
        f->y *= z;
        f->w *= z;
        f->h *= z;
        f->baseline *= z;
        f->size *= z;
        f->spacing *= z;
        f->word_spacing *= z;
        f->shadows = sh;
    }
}

/* the page zoomed, z device px a CSS px, drawn onto c: its tree copied with its lengths
   scaled and drawn as it is, its text set at the larger size as Chromium sets a zoomed
   page's; its window from device px y down h (h 0: the whole page, at most 16384 CSS px) */
void amb_paint_zoom(const layout_tree *t, const net_loader *nt, amb_canvas *c, double z, double y, double h) {
    if (z <= 0) z = 1;
    layout_tree zt;
    layout_init(&zt, t->id);
    pt_zoom_tree(t, z, &zt);
    c->zoom = z;
    pt_paint(&zt, nt, c, y, h > 0 ? h : 16384 * z);
}

void amb_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c) {
    c->zoom = 1;
    pt_paint(t, nt, c, 0, 16384);
}

void amb_paint_view(const layout_tree *t, const net_loader *nt, amb_canvas *c, double y, double h) {
    c->zoom = 1;
    pt_paint(t, nt, c, y, h);
}

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
