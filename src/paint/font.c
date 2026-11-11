/* src/paint/font.c -- the fonts (font.h): font_data.h's faces decoded, measured,
   and their glyphs drawn as coverage masks. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include "font.h"
#include "font_data.h"

/* ---- decoding ---- */

static int amb_b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static void amb_unbase64(const char *s, vector<uint8_t> *out) {
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; s[i]; i++) {
        int v = amb_b64(s[i]);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            push_back(out, (uint8_t)(acc >> bits & 0xff));
        }
    }
}

static uint32_t amb_varint(const vector<uint8_t> *b, size_t *i) {
    uint32_t n = 0;
    int shift = 0;
    while (*i < len(*b)) {
        uint8_t c = at(*b, *i);
        (*i)++;
        n |= (uint32_t)(c & 0x7f) << shift;
        if (!(c & 0x80)) break;
        shift += 7;
        if (shift > 28) break;
    }
    return n;
}
static int32_t amb_unzig(uint32_t v) { return (v & 1) ? -(int32_t)(v >> 1) - 1 : (int32_t)(v >> 1); }

static void amb_face_load(amb_fonts *f, int k, const char *b64) {
    vector<uint8_t> b;
    amb_unbase64(b64, &b);
    if (len(b) < 5 || at(b, 0) != 'A' || at(b, 1) != 'M' || at(b, 2) != 'B' || at(b, 3) != 'F') abort();
    size_t i = 5;
    uint32_t upem = amb_varint(&b, &i);
    int32_t asc = amb_unzig(amb_varint(&b, &i)), desc = amb_unzig(amb_varint(&b, &i));
    int32_t gap = (int32_t)amb_varint(&b, &i);
    uint32_t count = amb_varint(&b, &i);
    uint32_t first = (uint32_t)len(f->glyphs), cp = 0, fallback = first;
    for (uint32_t g = 0; g < count; g++) {
        cp += amb_varint(&b, &i);
        uint32_t adv = amb_varint(&b, &i), ncont = amb_varint(&b, &i);
        amb_glyph gl;
        gl.cp = cp;
        gl.first = (uint32_t)len(f->pts);
        gl.advance = (uint16_t)adv;
        gl.npts = 0;
        int32_t x = 0, y = 0;
        for (uint32_t c = 0; c < ncont; c++) {
            uint32_t np = amb_varint(&b, &i);
            for (uint32_t p = 0; p < np; p++) {
                uint32_t vx = amb_varint(&b, &i);
                x += amb_unzig(vx >> 1);
                y += amb_unzig(amb_varint(&b, &i));
                amb_pt pt;
                pt.x = (int16_t)x;
                pt.y = (int16_t)y;
                pt.on = (uint8_t)(vx & 1);
                pt.end = (uint8_t)(p + 1 == np);
                push_back(f->pts, pt);
                gl.npts++;
            }
        }
        if (cp == 0xFFFD) fallback = (uint32_t)len(f->glyphs);
        push_back(f->glyphs, gl);
    }
    amb_face fc;
    fc.upem = upem ? upem : 2048;
    fc.asc = asc;
    fc.desc = -desc;
    fc.gap = gap;
    fc.first = first;
    fc.count = count;
    fc.fallback = fallback;
    f->face[k] = fc;
}

void amb_fonts_load(amb_fonts *f) {
    memset(f->face, 0, sizeof f->face);
    clear(f->glyphs);
    clear(f->pts);
    amb_face_load(f, AMB_FACE_SANS, amb_font_sans);
    amb_face_load(f, AMB_FACE_SANS_BOLD, amb_font_sans_bold);
    amb_face_load(f, AMB_FACE_SERIF, amb_font_serif);
    amb_face_load(f, AMB_FACE_SERIF_BOLD, amb_font_serif_bold);
    amb_face_load(f, AMB_FACE_MONO, amb_font_mono);
    amb_face_load(f, AMB_FACE_MONO_BOLD, amb_font_mono_bold);
}

int amb_face_for(int family, int bold) {
    if (family == 0) return bold ? AMB_FACE_SANS_BOLD : AMB_FACE_SANS;
    if (family == 2) return bold ? AMB_FACE_MONO_BOLD : AMB_FACE_MONO;
    return bold ? AMB_FACE_SERIF_BOLD : AMB_FACE_SERIF;
}

/* ---- measuring ---- */

void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap) {
    double s = size / f->face[face].upem;
    *asc = f->face[face].asc * s;
    *desc = f->face[face].desc * s;
    *gap = f->face[face].gap * s;
}

uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp) {
    uint32_t lo = f->face[face].first, hi = lo + f->face[face].count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t c = at(f->glyphs, mid).cp;
        if (c == cp) return mid;
        if (c < cp) lo = mid + 1; else hi = mid;
    }
    if (cp == 0xA0) return amb_glyph_of(f, face, 0x20);
    return f->face[face].fallback;
}

double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size) {
    return at(f->glyphs, amb_glyph_of(f, face, cp)).advance * size / f->face[face].upem;
}

uint32_t amb_utf8_next(const char *s, size_t n, size_t *i) {
    uint8_t c = (uint8_t)s[*i];
    uint32_t cp;
    size_t k;
    if (c < 0x80) { (*i)++; return c; }
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; k = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; k = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; k = 3; }
    else { (*i)++; return 0xFFFD; }
    if (*i + k >= n) { (*i)++; return 0xFFFD; }
    for (size_t j = 1; j <= k; j++) {
        uint8_t d = (uint8_t)s[*i + j];
        if ((d & 0xC0) != 0x80) { (*i)++; return 0xFFFD; }
        cp = cp << 6 | (d & 0x3F);
    }
    *i += k + 1;
    return cp;
}

double amb_text_width(const amb_fonts *f, int face, double size, const char *s, size_t n) {
    double w = 0;
    size_t i = 0;
    while (i < n) w += amb_advance(f, face, amb_utf8_next(s, n, &i), size);
    return w;
}

/* ---- drawing: the signed area of each line, accumulated per pixel (as font-rs does) ---- */

static void amb_acc_line(vector<float> *acc, int W, int H, double x0, double y0, double x1, double y1) {
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

static void amb_xf(amb_pt p, double s, double slant, double ax, double ay, double *x, double *y) {
    *x = (p.x + slant * p.y) * s + ax;
    *y = -p.y * s + ay;
}

static void amb_acc_quad(vector<float> *acc, int W, int H, double x0, double y0, double cx, double cy, double x1, double y1) {
    double devx = x0 - 2 * cx + x1, devy = y0 - 2 * cy + y1;
    double devsq = devx * devx + devy * devy;
    if (devsq < 0.333) { amb_acc_line(acc, W, H, x0, y0, x1, y1); return; }
    int n = 1 + (int)floor(sqrt(sqrt(3.0 * devsq)));
    if (n > 32) n = 32;
    double px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, mt = 1 - t;
        double qx = mt * mt * x0 + 2 * mt * t * cx + t * t * x1;
        double qy = mt * mt * y0 + 2 * mt * t * cy + t * t * y1;
        amb_acc_line(acc, W, H, px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

int amb_glyph_mask(const amb_fonts *f, int face, uint32_t cp, double size, double dx, int italic,
                   vector<float> *acc, vector<uint8_t> *mask, int *w, int *h, int *ox, int *oy) {
    uint32_t gi = amb_glyph_of(f, face, cp);
    amb_glyph g = at(f->glyphs, gi);
    *w = 0;
    *h = 0;
    *ox = 0;
    *oy = 0;
    if (g.npts == 0) return 0;
    double s = size / f->face[face].upem, slant = italic ? 0.2 : 0;
    /* the box of the points, in px from the pen (y down) */
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (uint32_t k = 0; k < g.npts; k++) {
        amb_pt p = at(f->pts, g.first + k);
        double X = (p.x + slant * p.y) * s + dx, Y = -p.y * s;
        if (X < minx) minx = X;
        if (X > maxx) maxx = X;
        if (Y < miny) miny = Y;
        if (Y > maxy) maxy = Y;
    }
    int x0 = (int)floor(minx), y0 = (int)floor(miny);
    int gw = (int)ceil(maxx) - x0 + 1, gh = (int)ceil(maxy) - y0 + 1;
    if (gw <= 0 || gh <= 0 || gw > 4096 || gh > 4096) return 0;
    int W = gw + 2;
    clear(*acc);
    resize(acc, (size_t)W * (size_t)gh);
    /* each contour: from an on-curve start (or, every point off the curve, the middle
       of its last and first), its lines and quadratic curves, back to the start */
    double ax = dx - x0, ay = (double)-y0;
    uint32_t c0 = 0;
    while (c0 < g.npts) {
        uint32_t c1 = c0;
        while (c1 + 1 < g.npts && !at(f->pts, g.first + c1).end) c1++;
        uint32_t n = c1 - c0 + 1, k0 = 0;
        int found = 0;
        for (uint32_t k = 0; k < n && !found; k++) if (at(f->pts, g.first + c0 + k).on) { k0 = k; found = 1; }
        double sx, sy, cx, cy, qx = 0, qy = 0, px, py;
        int ctrl = 0;
        if (found) {
            amb_xf(at(f->pts, g.first + c0 + k0), s, slant, ax, ay, &sx, &sy);
            cx = sx;
            cy = sy;
            for (uint32_t j = 1; j <= n; j++) {
                amb_pt p = at(f->pts, g.first + c0 + (k0 + j) % n);
                amb_xf(p, s, slant, ax, ay, &px, &py);
                if (p.on) {
                    if (ctrl) amb_acc_quad(acc, W, gh, cx, cy, qx, qy, px, py);
                    else amb_acc_line(acc, W, gh, cx, cy, px, py);
                    cx = px;
                    cy = py;
                    ctrl = 0;
                } else {
                    if (ctrl) {
                        double mx = (qx + px) * 0.5, my = (qy + py) * 0.5;
                        amb_acc_quad(acc, W, gh, cx, cy, qx, qy, mx, my);
                        cx = mx;
                        cy = my;
                    }
                    qx = px;
                    qy = py;
                    ctrl = 1;
                }
            }
        } else {
            double lx, ly;
            amb_xf(at(f->pts, g.first + c0), s, slant, ax, ay, &px, &py);
            amb_xf(at(f->pts, g.first + c1), s, slant, ax, ay, &lx, &ly);
            sx = (px + lx) * 0.5;
            sy = (py + ly) * 0.5;
            cx = sx;
            cy = sy;
            for (uint32_t j = 0; j < n; j++) {
                amb_xf(at(f->pts, g.first + c0 + j), s, slant, ax, ay, &px, &py);
                if (ctrl) {
                    double mx = (qx + px) * 0.5, my = (qy + py) * 0.5;
                    amb_acc_quad(acc, W, gh, cx, cy, qx, qy, mx, my);
                    cx = mx;
                    cy = my;
                }
                qx = px;
                qy = py;
                ctrl = 1;
            }
            amb_acc_quad(acc, W, gh, cx, cy, qx, qy, sx, sy);
        }
        c0 = c1 + 1;
    }
    clear(*mask);
    resize(mask, (size_t)gw * (size_t)gh);
    for (int y = 0; y < gh; y++) {
        double sum = 0;
        for (int x = 0; x < gw; x++) {
            sum += (*acc)[(size_t)y * W + x];
            double a = fabs(sum);
            if (a > 1) a = 1;
            (*mask)[(size_t)y * gw + x] = (uint8_t)(a * 255.0 + 0.5);
        }
    }
    *w = gw;
    *h = gh;
    *ox = x0;
    *oy = y0;
    return 1;
}
