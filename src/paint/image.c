/* src/paint/image.c -- images decoded (image.h): PNG, over this file's own inflate.

   inflate (RFC 1950 and 1951): zlib's header and Adler-32, deflate's stored,
   fixed and dynamic blocks, decoded as zlib's puff.c does -- a canonical Huffman
   code read a bit at a time against its counts per length. PNG: the chunks
   (IHDR, PLTE, tRNS, IDAT, IEND), every colour type and bit depth, the five
   filters, Adam7 interlacing; each pixel made RGBA8 (16-bit samples by their
   high byte, tRNS's key or palette alpha applied). At most 2^24 pixels. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "image.h"

/* ---- inflate ---- */

typedef struct inf_bits { size_t pos; uint32_t buf; int cnt; int bad; } inf_bits;
typedef struct inf_huff { uint16_t count[16]; uint16_t symbol[320]; } inf_huff;

static int inf_need(const uint8_t *in, size_t n, inf_bits *b, int k) {
    uint32_t v = b->buf;
    while (b->cnt < k) {
        if (b->pos >= n) { b->bad = 1; return 0; }
        v |= (uint32_t)in[b->pos++] << b->cnt;
        b->cnt += 8;
    }
    b->buf = v >> k;
    b->cnt -= k;
    return (int)(v & ((1u << k) - 1));
}

/* the canonical code of the given lengths; 0 when they are no code */
static int inf_build(inf_huff *h, const uint16_t *lengths, int n) {
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++) h->count[lengths[s]]++;
    if (h->count[0] == n) return 1;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return 0;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int s = 0; s < n; s++) if (lengths[s]) h->symbol[offs[lengths[s]]++] = (uint16_t)s;
    return 1;
}

static int inf_decode(const uint8_t *in, size_t n, inf_bits *b, const inf_huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= inf_need(in, n, b, 1);
        if (b->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t inf_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint16_t inf_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t inf_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                        4097, 6145, 8193, 12289, 16385, 24577 };
static const uint16_t inf_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* a block's symbols, by its two codes, into out */
static int inf_codes(const uint8_t *in, size_t n, inf_bits *b, const inf_huff *lc, const inf_huff *dc, vector<uint8_t> *out, size_t cap) {
    for (;;) {
        int sym = inf_decode(in, n, b, lc);
        if (sym < 0) return 0;
        if (sym < 256) {
            if (len(*out) >= cap) return 0;
            push_back(out, (uint8_t)sym);
            continue;
        }
        if (sym == 256) return 1;
        sym -= 257;
        if (sym >= 29) return 0;
        size_t length = inf_lbase[sym] + (size_t)inf_need(in, n, b, inf_lext[sym]);
        int ds = inf_decode(in, n, b, dc);
        if (ds < 0 || ds >= 30 || b->bad) return 0;
        size_t dist = inf_dbase[ds] + (size_t)inf_need(in, n, b, inf_dext[ds]);
        if (b->bad || dist > len(*out) || len(*out) + length > cap) return 0;
        size_t from = len(*out) - dist;
        for (size_t k = 0; k < length; k++) push_back(out, at(*out, from + k));
    }
}

/* a zlib stream inflated into out, at most cap bytes; 0 when it is not one */
int amb_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap) {
    if (n < 6 || (in[0] & 0x0f) != 8 || ((unsigned)in[0] << 8 | in[1]) % 31 != 0 || (in[1] & 0x20)) return 0;
    size_t pos = 2;
    return amb_inflate_raw(in, n, &pos, out, cap);
}

/* a raw deflate stream from in[*pos] inflated into out, at most cap bytes, *pos then
   just past its last block (HTTP's content codings read their trailers there); 0
   when it is not one */
int amb_inflate_raw(const uint8_t *in, size_t n, size_t *pos, vector<uint8_t> *out, size_t cap) {
    inf_bits b;
    memset(&b, 0, sizeof b);
    b.pos = *pos;
    inf_huff lc, dc;
    uint16_t lengths[320];
    int last;
    do {
        last = inf_need(in, n, &b, 1);
        int type = inf_need(in, n, &b, 2);
        if (b.bad) return 0;
        if (type == 0) {
            /* stored: to a byte, its length and the length's complement, its bytes */
            b.buf = 0;
            b.cnt = 0;
            if (b.pos + 4 > n) return 0;
            size_t ln = in[b.pos] | (size_t)in[b.pos + 1] << 8, nl = in[b.pos + 2] | (size_t)in[b.pos + 3] << 8;
            b.pos += 4;
            if (ln != (~nl & 0xffff) || b.pos + ln > n || len(*out) + ln > cap) return 0;
            append_range(out, in + b.pos, ln);
            b.pos += ln;
        } else if (type == 1) {
            int s = 0;
            for (; s < 144; s++) lengths[s] = 8;
            for (; s < 256; s++) lengths[s] = 9;
            for (; s < 280; s++) lengths[s] = 7;
            for (; s < 288; s++) lengths[s] = 8;
            inf_build(&lc, lengths, 288);
            for (s = 0; s < 30; s++) lengths[s] = 5;
            inf_build(&dc, lengths, 30);
            if (!inf_codes(in, n, &b, &lc, &dc, out, cap)) return 0;
        } else if (type == 2) {
            static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int nlen = inf_need(in, n, &b, 5) + 257, ndist = inf_need(in, n, &b, 5) + 1, ncode = inf_need(in, n, &b, 4) + 4;
            if (b.bad || nlen > 286 || ndist > 30) return 0;
            memset(lengths, 0, sizeof lengths);
            for (int k = 0; k < ncode; k++) lengths[order[k]] = (uint16_t)inf_need(in, n, &b, 3);
            inf_huff cl;
            if (b.bad || !inf_build(&cl, lengths, 19)) return 0;
            int k = 0;
            memset(lengths, 0, sizeof lengths);
            while (k < nlen + ndist) {
                int sym = inf_decode(in, n, &b, &cl);
                if (sym < 0) return 0;
                if (sym < 16) { lengths[k++] = (uint16_t)sym; continue; }
                uint16_t v = 0;
                int rep;
                if (sym == 16) { if (k == 0) return 0; v = lengths[k - 1]; rep = 3 + inf_need(in, n, &b, 2); }
                else if (sym == 17) rep = 3 + inf_need(in, n, &b, 3);
                else rep = 11 + inf_need(in, n, &b, 7);
                if (b.bad || k + rep > nlen + ndist) return 0;
                while (rep--) lengths[k++] = v;
            }
            if (lengths[256] == 0) return 0;
            if (!inf_build(&lc, lengths, nlen) || !inf_build(&dc, lengths + nlen, ndist)) return 0;
            if (!inf_codes(in, n, &b, &lc, &dc, out, cap)) return 0;
        } else return 0;
    } while (!last);
    *pos = b.pos;
    return 1;
}

/* ---- PNG ---- */

static uint32_t png_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int png_paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* the filtered rows of a w x h (sub)image at raw[off..], unfiltered in place; its bytes */
static size_t png_unfilter(vector<uint8_t> *raw, size_t off, uint32_t w, uint32_t h, int bits_pp, int *ok) {
    size_t row = ((size_t)w * bits_pp + 7) / 8, bpp = bits_pp >= 8 ? (size_t)bits_pp / 8 : 1;
    if (w == 0 || h == 0) return 0;
    if (off + (row + 1) * h > len(*raw)) { *ok = 0; return 0; }
    for (uint32_t y = 0; y < h; y++) {
        size_t r = off + y * (row + 1), prev = off + (y - 1) * (row + 1);
        int ft = at(*raw, r);
        for (size_t x = 0; x < row; x++) {
            int a = x >= bpp ? at(*raw, r + 1 + x - bpp) : 0;
            int b = y ? at(*raw, prev + 1 + x) : 0;
            int c = y && x >= bpp ? at(*raw, prev + 1 + x - bpp) : 0;
            int v = at(*raw, r + 1 + x), add;
            if (ft == 0) add = 0;
            else if (ft == 1) add = a;
            else if (ft == 2) add = b;
            else if (ft == 3) add = (a + b) / 2;
            else if (ft == 4) add = png_paeth(a, b, c);
            else { *ok = 0; return 0; }
            (*raw)[r + 1 + x] = (uint8_t)(v + add);
        }
    }
    return (row + 1) * h;
}

/* a sample of the unfiltered row r (bytes at raw[r + 1 ..]) */
static int png_sample(const vector<uint8_t> *raw, size_t r, uint32_t i, int depth) {
    if (depth == 8) return at(*raw, r + 1 + i);
    if (depth == 16) return at(*raw, r + 1 + 2 * (size_t)i);
    size_t bit = (size_t)i * depth;
    int byte = at(*raw, r + 1 + bit / 8), shift = 8 - depth - (int)(bit % 8);
    return byte >> shift & ((1 << depth) - 1);
}
static int png_sample16(const vector<uint8_t> *raw, size_t r, uint32_t i) {
    return at(*raw, r + 1 + 2 * (size_t)i) << 8 | at(*raw, r + 2 + 2 * (size_t)i);
}

int amb_png_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    *w = 0;
    *h = 0;
    if (n < 33 || memcmp(p, sig, 8) != 0) return 0;
    size_t i = 8;
    uint32_t W = 0, H = 0;
    int depth = 0, ctype = 0, interlace = 0, have_trns = 0;
    uint8_t pal[256][4];
    int npal = 0;
    int key[3] = { -1, -1, -1 };
    memset(pal, 255, sizeof pal);
    vector<uint8_t> z;
    while (i + 12 <= n) {
        uint32_t ln = png_be32(p + i);
        const uint8_t *type = p + i + 4;
        if (ln > n - i - 12) return 0;
        const uint8_t *d = p + i + 8;
        if (memcmp(type, "IHDR", 4) == 0 && ln >= 13) {
            W = png_be32(d);
            H = png_be32(d + 4);
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            npal = (int)(ln / 3);
            if (npal > 256) npal = 256;
            for (int k = 0; k < npal; k++) { pal[k][0] = d[3 * k]; pal[k][1] = d[3 * k + 1]; pal[k][2] = d[3 * k + 2]; }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            have_trns = 1;
            if (ctype == 3) for (uint32_t k = 0; k < ln && k < 256; k++) pal[k][3] = d[k];
            else if (ctype == 0 && ln >= 2) key[0] = d[0] << 8 | d[1];
            else if (ctype == 2 && ln >= 6) { key[0] = d[0] << 8 | d[1]; key[1] = d[2] << 8 | d[3]; key[2] = d[4] << 8 | d[5]; }
        } else if (memcmp(type, "IDAT", 4) == 0) append_range(z, d, ln);
        else if (memcmp(type, "IEND", 4) == 0) break;
        i += 12 + ln;
    }
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels || W == 0 || H == 0 || (uint64_t)W * H > (1u << 24) || interlace > 1) return 0;
    if (!(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) return 0;
    if ((ctype == 2 || ctype == 4 || ctype == 6) && depth < 8) return 0;
    if (ctype == 3 && depth == 16) return 0;
    int bits_pp = channels * depth;
    vector<uint8_t> raw;
    size_t cap = (((size_t)W * bits_pp + 7) / 8 + 1) * H + 7 * (H + 8);
    if (!amb_inflate(data(z), len(z), &raw, cap + 64)) return 0;
    clear(*rgba);
    resize(rgba, (size_t)W * H * 4);
    /* the passes: the whole image, or Adam7's seven */
    static const int px0[7] = { 0, 4, 0, 2, 0, 1, 0 }, py0[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const int pdx[7] = { 8, 8, 4, 4, 2, 2, 1 }, pdy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = interlace ? 7 : 1;
    size_t off = 0;
    int ok = 1;
    for (int ps = 0; ps < passes && ok; ps++) {
        uint32_t sx0 = interlace ? (uint32_t)px0[ps] : 0, sy0 = interlace ? (uint32_t)py0[ps] : 0;
        uint32_t sdx = interlace ? (uint32_t)pdx[ps] : 1, sdy = interlace ? (uint32_t)pdy[ps] : 1;
        uint32_t pw = W > sx0 ? (W - sx0 + sdx - 1) / sdx : 0, ph = H > sy0 ? (H - sy0 + sdy - 1) / sdy : 0;
        if (pw == 0 || ph == 0) continue;
        size_t used = png_unfilter(&raw, off, pw, ph, bits_pp, &ok);
        if (!ok) return 0;
        size_t row = ((size_t)pw * bits_pp + 7) / 8 + 1;
        for (uint32_t y = 0; y < ph; y++) {
            size_t r = off + y * row;
            for (uint32_t x = 0; x < pw; x++) {
                int R, G, B, A = 255;
                uint32_t k = x * (uint32_t)channels;
                if (ctype == 3) {
                    int idx = png_sample(&raw, r, x, depth);
                    if (idx >= npal) { R = G = B = 0; }
                    else { R = pal[idx][0]; G = pal[idx][1]; B = pal[idx][2]; A = pal[idx][3]; }
                } else if (ctype == 0 || ctype == 4) {
                    int v = depth == 16 ? png_sample16(&raw, r, k) : png_sample(&raw, r, k, depth);
                    if (have_trns && ctype == 0 && v == key[0]) A = 0;
                    if (depth < 8) v = v * 255 / ((1 << depth) - 1);
                    else if (depth == 16) v >>= 8;
                    R = G = B = v;
                    if (ctype == 4) A = png_sample(&raw, r, k + 1, depth);
                } else {
                    if (depth == 16) {
                        int r16 = png_sample16(&raw, r, k), g16 = png_sample16(&raw, r, k + 1), b16 = png_sample16(&raw, r, k + 2);
                        if (have_trns && ctype == 2 && r16 == key[0] && g16 == key[1] && b16 == key[2]) A = 0;
                        R = r16 >> 8; G = g16 >> 8; B = b16 >> 8;
                        if (ctype == 6) A = png_sample16(&raw, r, k + 3) >> 8;
                    } else {
                        R = png_sample(&raw, r, k, 8); G = png_sample(&raw, r, k + 1, 8); B = png_sample(&raw, r, k + 2, 8);
                        if (have_trns && ctype == 2 && R == key[0] && G == key[1] && B == key[2]) A = 0;
                        if (ctype == 6) A = png_sample(&raw, r, k + 3, 8);
                    }
                }
                size_t o = ((size_t)(sy0 + y * sdy) * W + (sx0 + x * sdx)) * 4;
                (*rgba)[o] = (uint8_t)R;
                (*rgba)[o + 1] = (uint8_t)G;
                (*rgba)[o + 2] = (uint8_t)B;
                (*rgba)[o + 3] = (uint8_t)A;
            }
        }
        off += used;
    }
    *w = W;
    *h = H;
    return 1;
}
