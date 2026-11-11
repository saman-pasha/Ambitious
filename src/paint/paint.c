/* src/paint/paint.c -- a laid-out page drawn, and written as a PNG (paint.h).

   The order is CSS 2.1's (Appendix E) for what layout makes: the canvas, its
   colour the root element's background (or the body's, propagated); then each
   box in tree order -- its background over its border box, its borders, then
   its lines (inline elements' backgrounds, text, decorations, list markers) --
   and its child boxes. Glyphs come from font.c at their size and quarter-pixel
   position, cached. An image is drawn from the bytes the loader fetched for its
   element (image.c decodes PNG), sampled bilinearly into its content box; one
   that cannot be decoded is a grey placeholder. The PNG is written with this file's own deflate: each row
   filtered (None, Sub or Up, whichever sums smallest), LZ77 over a hash of three
   bytes, fixed Huffman codes. */
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

/* ---- the canvas ---- */

static void pt_fill(amb_canvas *c, double x0, double y0, double x1, double y1, uint32_t rgba) {
    uint32_t a = rgba & 0xff;
    if (a == 0) return;
    int ix0 = (int)floor(x0 + 0.5), iy0 = (int)floor(y0 + 0.5), ix1 = (int)floor(x1 + 0.5), iy1 = (int)floor(y1 + 0.5);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > (int)c->w) ix1 = (int)c->w;
    if (iy1 > (int)c->h) iy1 = (int)c->h;
    uint32_t r = rgba >> 24, g = rgba >> 16 & 0xff, b = rgba >> 8 & 0xff;
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            size_t i = ((size_t)y * c->w + (size_t)x) * 3;
            if (a == 255) { c->px[i] = (uint8_t)r; c->px[i + 1] = (uint8_t)g; c->px[i + 2] = (uint8_t)b; continue; }
            c->px[i] = (uint8_t)((r * a + c->px[i] * (255 - a)) / 255);
            c->px[i + 1] = (uint8_t)((g * a + c->px[i + 1] * (255 - a)) / 255);
            c->px[i + 2] = (uint8_t)((b * a + c->px[i + 2] * (255 - a)) / 255);
        }
}

/* ---- glyphs, cached by face, size, code point, quarter pixel and slant ---- */

typedef struct pt_glyph { uint64_t key; uint32_t off; int32_t w, h, ox, oy; } pt_glyph;

static uint64_t pt_key(int face, double size, uint32_t cp, int quarter, int italic) {
    uint64_t sz = (uint64_t)(size * 16 + 0.5) & 0xffff;
    return (uint64_t)face << 61 | (uint64_t)(italic & 1) << 60 | (uint64_t)(quarter & 3) << 58 | sz << 40 | (uint64_t)(cp & 0x1fffff);
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
                    int face, double size, int italic, uint32_t rgba) {
    uint32_t ca = rgba & 0xff, r = rgba >> 24, g = rgba >> 16 & 0xff, b = rgba >> 8 & 0xff;
    int by = (int)floor(baseline + 0.5);
    size_t i = 0;
    while (i < n) {
        uint32_t cp = amb_utf8_next(s, n, &i);
        double fx = floor(x);
        int quarter = (int)((x - fx) * 4);
        uint32_t e = pt_glyph_of(f, entries, table, masks, acc, mask, face, size, cp, quarter, italic);
        pt_glyph gl = at(*entries, e);
        for (int yy = 0; yy < gl.h; yy++) {
            int Y = by + gl.oy + yy;
            if (Y < 0 || Y >= (int)c->h) continue;
            for (int xx = 0; xx < gl.w; xx++) {
                int X = (int)fx + gl.ox + xx;
                if (X < 0 || X >= (int)c->w) continue;
                uint32_t m = at(*masks, gl.off + (size_t)yy * gl.w + xx);
                if (!m) continue;
                uint32_t a = m * ca / 255;
                size_t p = ((size_t)Y * c->w + (size_t)X) * 3;
                c->px[p] = (uint8_t)((r * a + c->px[p] * (255 - a)) / 255);
                c->px[p + 1] = (uint8_t)((g * a + c->px[p + 1] * (255 - a)) / 255);
                c->px[p + 2] = (uint8_t)((b * a + c->px[p + 2] * (255 - a)) / 255);
            }
        }
        x += amb_advance(f, face, cp, size);
    }
}

/* ---- the boxes ---- */

/* an image's RGBA pixels, w x h, drawn into the rectangle (x0, y0, dw, dh), sampled
   bilinearly and blended by their alpha */
static void pt_image(amb_canvas *c, const vector<uint8_t> *rgba, uint32_t w, uint32_t h, double x0, double y0, double dw, double dh) {
    if (dw <= 0 || dh <= 0 || w == 0 || h == 0) return;
    int ix0 = (int)floor(x0 + 0.5), iy0 = (int)floor(y0 + 0.5), ix1 = (int)floor(x0 + dw + 0.5), iy1 = (int)floor(y0 + dh + 0.5);
    for (int y = iy0 < 0 ? 0 : iy0; y < iy1 && y < (int)c->h; y++)
        for (int x = ix0 < 0 ? 0 : ix0; x < ix1 && x < (int)c->w; x++) {
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

/* the image the loader fetched for an element, decoded into rgba: 1 when it was */
static int pt_image_of(const net_loader *nt, uint32_t el, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    if (!nt) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++) {
        if (!nt->resource[r].live || nt->resource[r].elem.index != el || nt->resource[r].kind != NET_KIND_IMAGE) continue;
        if (nt->resource[r].outcome != NET_OUTCOME_OK) return 0;
        amb_span b = nt->resource[r].body;
        vector<uint8_t> bytes;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
        return amb_png_decode(data(bytes), len(bytes), rgba, w, h);
    }
    return 0;
}

static void pt_box(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, uint32_t skip_bg) {
    double bx = t->box[b].x - t->box[b].pl - t->box[b].bl, by = t->box[b].y - t->box[b].pt - t->box[b].bt;
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    if (b != skip_bg) pt_fill(c, bx, by, bx + bw, by + bh, t->box[b].background);
    if (t->box[b].kind == LAYOUT_BOX_KIND_REPLACED) {
        vector<uint8_t> rgba;
        uint32_t iw, ih;
        if (pt_image_of(nt, t->box[b].node.index, &rgba, &iw, &ih)) pt_image(c, &rgba, iw, ih, t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h);
        else if ((t->box[b].background & 0xff) == 0 && t->box[b].bt + t->box[b].bl == 0)
            pt_fill(c, t->box[b].x, t->box[b].y, t->box[b].x + t->box[b].w, t->box[b].y + t->box[b].h, 0xe8e8e8ff);
    }
    uint32_t bc = t->box[b].border_color;
    if (t->box[b].bt > 0) pt_fill(c, bx, by, bx + bw, by + t->box[b].bt, bc);
    if (t->box[b].bb > 0) pt_fill(c, bx, by + bh - t->box[b].bb, bx + bw, by + bh, bc);
    if (t->box[b].bl > 0) pt_fill(c, bx, by, bx + t->box[b].bl, by + bh, bc);
    if (t->box[b].br > 0) pt_fill(c, bx + bw - t->box[b].br, by, bx + bw, by + bh, bc);
}

static void pt_lines(const layout_tree *t, uint32_t b, amb_canvas *c, const amb_fonts *f, vector<pt_glyph> *entries, vector<uint32_t> *table,
                     vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next)
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            layout_frag_kind k = t->frag[fr].kind;
            if (k == LAYOUT_FRAG_KIND_INLINE) {
                pt_fill(c, t->frag[fr].x, t->frag[fr].y, t->frag[fr].x + t->frag[fr].w, t->frag[fr].y + t->frag[fr].h, t->frag[fr].background);
                continue;
            }
            if (k != LAYOUT_FRAG_KIND_TEXT && k != LAYOUT_FRAG_KIND_MARKER) continue;
            amb_span sp = t->frag[fr].text;
            double size = t->frag[fr].size, base = t->frag[fr].baseline;
            uint32_t color = t->frag[fr].color;
            pt_text(c, f, entries, table, masks, acc, mask, text_ptr(t, sp), sp.len, t->frag[fr].x, base,
                    (int)t->frag[fr].face, size, t->frag[fr].italic, color);
            uint8_t deco = t->frag[fr].decoration;
            double th = floor(size / 16 + 0.5);
            if (th < 1) th = 1;
            double x0 = t->frag[fr].x, x1 = x0 + t->frag[fr].w;
            if (deco & 1) { double y = floor(base + size * 0.09 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
            if (deco & 2) { double y = floor(base - size * 0.93 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
            if (deco & 4) { double y = floor(base - size * 0.3 + 0.5); pt_fill(c, x0, y, x1, y + th, color); }
        }
}

static void pt_tree(const layout_tree *t, const net_loader *nt, uint32_t b, amb_canvas *c, uint32_t skip_bg, const amb_fonts *f,
                    vector<pt_glyph> *entries, vector<uint32_t> *table, vector<uint8_t> *masks, vector<float> *acc, vector<uint8_t> *mask) {
    if (t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) pt_box(t, nt, b, c, skip_bg);
    pt_lines(t, b, c, f, entries, table, masks, acc, mask);
    for (uint32_t ch = t->box[b].first_child; ch; ch = t->box[ch].next) pt_tree(t, nt, ch, c, skip_bg, f, entries, table, masks, acc, mask);
}

void amb_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c) {
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    uint32_t vp = 0;
    for (uint32_t b = 1; b < t->box_len && !vp; b++) if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    double w = vp ? t->box[vp].w : 800, h = vp ? t->box[vp].h : 600;
    if (h > 16384) h = 16384;
    c->w = (uint32_t)ceil(w);
    c->h = (uint32_t)ceil(h);
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
    pt_fill(c, 0, 0, c->w, c->h, 0xffffffff);
    pt_fill(c, 0, 0, c->w, c->h, bg);
    vector<pt_glyph> entries;
    vector<uint32_t> table;
    vector<uint8_t> masks, mask;
    vector<float> acc;
    resize(&table, 1024);
    if (vp) pt_tree(t, nt, vp, c, skip, &f, &entries, &table, &masks, &acc, &mask);
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
