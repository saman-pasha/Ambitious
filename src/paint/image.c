/* src/paint/image.c -- images decoded (image.h): PNG over this file's own inflate,
   GIF over its own LZW, JPEG over its own Huffman decoder and inverse DCT.

   inflate (RFC 1950 and 1951): zlib's header and Adler-32, deflate's stored,
   fixed and dynamic blocks, decoded as zlib's puff.c does -- a canonical Huffman
   code read a bit at a time against its counts per length. PNG: the chunks
   (IHDR, PLTE, tRNS, IDAT, IEND), every colour type and bit depth, the five
   filters, Adam7 interlacing; each pixel made RGBA8 (16-bit samples by their
   high byte, tRNS's key or palette alpha applied).

   GIF (87a and 89a): the logical screen and its global colour table, a frame's
   local table, the graphic control extension's transparent index, LZW with its
   code size growing to 12 bits, clear codes and a full table kept, interlaced
   rows; the first frame composed onto a transparent canvas of the screen's size,
   the frames counted. An index past its table is opaque black, no table at all a
   grey ramp, as PIL has them.

   JPEG (ITU T.81): baseline, extended and progressive Huffman-coded frames of
   8-bit samples, 1, 3 or 4 components at any sampling factors (1 to 4), restart
   intervals. Progressive scans are spectral selection and successive
   approximation, DC and AC, first and refining, their coefficients kept until the
   end. The rest is libjpeg's, as PIL decodes, so a decoded pixel is PIL's: its
   integer inverse DCT (jidctint.c, the islow method), its fancy upsampling of
   2:1 chroma (a triangle filter, jdsample.c) and box upsampling otherwise, its
   colour spaces (JFIF or Adobe's transform, else the component ids: YCbCr, RGB,
   CMYK, YCCK) and its YCbCr conversion. CMYK is made RGB as PIL makes it, taking
   the samples as Adobe's inverted ones. The standard Huffman tables stand in for
   ones a frame never defines (Motion JPEG), as libjpeg-turbo has them. Data that
   ends or breaks inside a scan leaves the rest of that restart interval grey,
   and decoding resumes at the next restart marker. EXIF's orientation (1 to 8)
   is applied last.

   Every image has at most 2^24 pixels; a larger one is refused from its header. */
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
static int amb_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap) {
    if (n < 6 || (in[0] & 0x0f) != 8 || ((unsigned)in[0] << 8 | in[1]) % 31 != 0 || (in[1] & 0x20)) return 0;
    inf_bits b;
    memset(&b, 0, sizeof b);
    b.pos = 2;
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
    if (!channels || W == 0 || H == 0 || (uint64_t)W * H > AMB_IMAGE_MAX_PIXELS || interlace > 1) return 0;
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

/* ---- GIF ---- */

/* the data sub-blocks from i skipped; the position after their terminator (*cut when
   the data ends first) */
static size_t gif_skip(const uint8_t *p, size_t n, size_t i, int *cut) {
    while (i < n) {
        size_t k = p[i++];
        if (k == 0) return i;
        if (k > n - i) break;
        i += k;
    }
    *cut = 1;
    return n;
}

/* the data sub-blocks from i appended to out; as gif_skip */
static size_t gif_gather(const uint8_t *p, size_t n, size_t i, vector<uint8_t> *out, int *cut) {
    while (i < n) {
        size_t k = p[i++];
        if (k == 0) return i;
        if (k > n - i) {
            append_range(out, p + i, n - i);
            break;
        }
        append_range(out, p + i, k);
        i += k;
    }
    *cut = 1;
    return n;
}

/* LZW codes (least significant bit first, min code size mcs) decoded into out, at
   most total indices; how many it made. A string is written backwards from its last
   index, along its prefixes. *bad: a code past the table, which ends the stream. */
static size_t gif_lzw(const uint8_t *d, size_t dn, int mcs, uint8_t *out, size_t total, int *bad) {
    uint16_t prefix[4096], length[4096];
    uint8_t suffix[4096], first[4096];
    int clr = 1 << mcs, eoi = clr + 1;
    for (int c = 0; c < clr; c++) {
        prefix[c] = 0;
        length[c] = 1;
        suffix[c] = (uint8_t)c;
        first[c] = (uint8_t)c;
    }
    int nxt = clr + 2, bits = mcs + 1, prev = -1;
    uint32_t acc = 0;
    int cnt = 0;
    size_t pos = 0, op = 0;
    while (op < total) {
        while (cnt < bits && pos < dn) {
            acc |= (uint32_t)d[pos++] << cnt;
            cnt += 8;
        }
        if (cnt < bits) break;
        int code = (int)(acc & ((1u << bits) - 1));
        acc >>= bits;
        cnt -= bits;
        if (code == clr) {
            nxt = clr + 2;
            bits = mcs + 1;
            prev = -1;
            continue;
        }
        if (code == eoi) break;
        if (prev < 0) {
            /* the first code after a clear: a literal */
            if (code > clr) { *bad = 1; break; }
            out[op++] = (uint8_t)code;
            prev = code;
            continue;
        }
        if (code > nxt) { *bad = 1; break; }
        if (nxt < 4096) {
            /* the new string: the previous one and the first index of this one (of itself, when it is the new one) */
            prefix[nxt] = (uint16_t)prev;
            first[nxt] = first[prev];
            suffix[nxt] = code == nxt ? first[prev] : first[code];
            length[nxt] = (uint16_t)(length[prev] + 1);
            nxt++;
            if (nxt == (1 << bits) && bits < 12) bits++;
        }
        size_t stop = op + length[code], q = stop;
        int c = code;
        while (q > op) {
            q--;
            if (q < total) out[q] = suffix[c];
            c = prefix[c];
        }
        op = stop;
        prev = code;
    }
    return op < total ? op : total;
}

/* the row of a frame of h rows that its r-th coded row is: interlaced, every 8th from
   0, every 8th from 4, every 4th from 2, every 2nd from 1 */
static uint32_t gif_row(uint32_t r, uint32_t h, int interlaced) {
    if (!interlaced) return r;
    uint32_t n1 = (h + 7) / 8, n2 = (h + 3) / 8, n3 = (h + 1) / 4;
    if (r < n1) return r * 8;
    r -= n1;
    if (r < n2) return r * 8 + 4;
    r -= n2;
    if (r < n3) return r * 4 + 2;
    return (r - n3) * 2 + 1;
}

int amb_gif_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info) {
    memset(info, 0, sizeof *info);
    if (n < 13 || p[0] != 'G' || p[1] != 'I' || p[2] != 'F' || p[3] != '8' || (p[4] != '7' && p[4] != '9') || p[5] != 'a') return 0;
    uint32_t W = p[6] | (uint32_t)p[7] << 8, H = p[8] | (uint32_t)p[9] << 8;
    if (W == 0 || H == 0 || (uint64_t)W * H > AMB_IMAGE_MAX_PIXELS) return 0;
    uint8_t gct[768], lct[768];
    size_t ngct = 0, nlct = 0, i = 13;
    if (p[10] & 0x80) {
        ngct = (size_t)2 << (p[10] & 7);
        if (3 * ngct > n - i) return 0;
        memcpy(gct, p + i, 3 * ngct);
        i += 3 * ngct;
    }
    int trans = -1, ended = 0, broken = 0, drawn = 0;
    uint32_t frames = 0;
    vector<uint8_t> lzw, idx;
    while (i < n) {
        int blk = p[i++];
        if (blk == 0x3B) { ended = 1; break; }
        if (blk == 0x21) {
            /* an extension: the graphic control's transparent index is for the image that follows it */
            if (i >= n) break;
            int label = p[i++];
            if (label == 0xF9 && i < n && p[i] >= 4 && 5 <= n - i) trans = (p[i + 1] & 1) ? p[i + 4] : -1;
            int cut = 0;
            i = gif_skip(p, n, i, &cut);
            if (cut) break;
            continue;
        }
        if (blk != 0x2C || 9 > n - i) break;
        uint32_t fx = p[i] | (uint32_t)p[i + 1] << 8, fy = p[i + 2] | (uint32_t)p[i + 3] << 8;
        uint32_t fw = p[i + 4] | (uint32_t)p[i + 5] << 8, fh = p[i + 6] | (uint32_t)p[i + 7] << 8;
        int flags = p[i + 8];
        i += 9;
        size_t nl = 0;
        if (flags & 0x80) {
            nl = (size_t)2 << (flags & 7);
            if (3 * nl > n - i) break;
            if (frames == 0) {
                memcpy(lct, p + i, 3 * nl);
                nlct = nl;
            }
            i += 3 * nl;
        }
        if (i >= n) break;
        int mcs = p[i++];
        frames++;
        int cut = 0;
        if (frames > 1) {
            i = gif_skip(p, n, i, &cut);
            trans = -1;
            if (cut) break;
            continue;
        }
        /* a code size of 2 to 8, as GIF89a has them (at 1 the first free code would
           already fill its width, and the width would never grow) */
        if (mcs < 2 || mcs > 8 || (uint64_t)fw * fh > AMB_IMAGE_MAX_PIXELS) return 0;
        i = gif_gather(p, n, i, &lzw, &cut);
        size_t total = (size_t)fw * fh;
        clear(idx);
        resize(&idx, total + 1);
        int bad = 0;
        size_t got = gif_lzw(data(lzw), len(lzw), mcs, data(idx), total, &bad);
        if (!drawn) {
            clear(*rgba);
            resize(rgba, (size_t)W * H * 4);
            drawn = 1;
        }
        /* the frame onto the canvas, clipped to it: each index its table's colour (the
           table chosen by an if: cicilang lowers a conditional of two arrays as a cast) */
        const uint8_t *pal = gct;
        size_t npal = ngct;
        if (nlct) {
            pal = lct;
            npal = nlct;
        }
        const uint8_t *ix = data(idx);
        uint8_t *o = data(*rgba);
        for (uint32_t r = 0; r < fh; r++) {
            uint32_t y = fy + gif_row(r, fh, flags & 0x40);
            if ((size_t)r * fw >= got) break;
            if (y >= H) continue;
            for (uint32_t x = 0; x < fw && fx + x < W; x++) {
                size_t k = (size_t)r * fw + x;
                if (k >= got) break;
                int v = ix[k];
                if (v == trans) continue;
                size_t d = ((size_t)y * W + fx + x) * 4;
                if (npal == 0) { o[d] = (uint8_t)v; o[d + 1] = (uint8_t)v; o[d + 2] = (uint8_t)v; }
                else if ((size_t)v < npal) { o[d] = pal[3 * v]; o[d + 1] = pal[3 * v + 1]; o[d + 2] = pal[3 * v + 2]; }
                else { o[d] = 0; o[d + 1] = 0; o[d + 2] = 0; }
                o[d + 3] = 255;
            }
        }
        if (got < total || bad) broken = 1;
        trans = -1;
        if (cut) break;
    }
    if (!frames) return 0;
    if (!drawn) {
        clear(*rgba);
        resize(rgba, (size_t)W * H * 4);
    }
    info->w = W;
    info->h = H;
    info->frames = frames;
    info->orientation = 1;
    info->partial = !ended || broken;
    return 1;
}

/* ---- JPEG ---- */

/* the natural (row by row) index of the k-th coefficient in zigzag order */
static const uint8_t jpg_zz[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
    14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

/* the standard Huffman tables (T.81 K.3): DC luminance, DC chrominance, AC luminance,
   AC chrominance; libjpeg-turbo's stand-ins for tables 0 and 1 when a frame defines none */
static const uint8_t jpg_std_bits[4][16] = { { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 }, { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
                                             { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d }, { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 } };
static const uint8_t jpg_std_dc[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t jpg_std_ac_luma[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1,
    0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
    0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa };
static const uint8_t jpg_std_ac_chroma[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42,
    0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19,
    0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa };

/* a Huffman table: by the next 9 bits, a code's length (0 when it is longer) and its
   symbol; for the longer codes, by length, the largest code and the offset from a code
   to its symbol's index */
typedef struct jpg_huff {
    int32_t defined;
    int32_t maxcode[18];
    int32_t valoff[18];
    uint8_t look_len[512];
    uint8_t look_val[512];
    uint8_t vals[256];
} jpg_huff;

/* a component: its id, sampling factors, quantization table, and the current scan's DC
   and AC tables; its DC predictor; its blocks in the MCU grid (bw x bh) and those that
   hold the image (nbx x nby, what a scan of it alone codes); its samples (cw x ch);
   where its coefficients and its plane start; its quantization, natural order, copied
   at its first scan */
typedef struct jpg_comp {
    int32_t id, h, v, tq, td, ta, pred, latched;
    uint32_t bw, bh, nbx, nby, cw, ch;
    size_t coff, poff;
    uint16_t q[64];
} jpg_comp;

/* the bits of entropy-coded data, most significant first in acc: hit once a marker or
   the data's end is reached (its code in marker, 0 for the end), after which zeros are
   read, fake counting them -- a scan that reads more bits than it has, reads into
   those */
typedef struct jpg_bits { size_t pos; uint64_t acc; int32_t cnt, hit, marker, fake; } jpg_bits;

enum { JPG_GREY, JPG_YCC, JPG_RGB, JPG_CMYK, JPG_YCCK };

/* a decoding: the frame (its size, MCU grid, components and their largest sampling
   factors), the markers read (restart interval, JFIF, Adobe's transform, EXIF's
   orientation), the scan being read (its components, band, approximation, EOB run and
   the restart marker it expects next), the tables, and libjpeg's ranges and colour
   tables */
typedef struct jpg {
    uint32_t w, h, mcux, mcuy;
    int32_t nc, progressive, hmax, vmax, restart, jfif, adobe, transform, orientation, partial, scans;
    int32_t ns, ss, se, ah, al, eobrun, rst;
    int32_t sc[4];
    int32_t qdef[4];
    jpg_comp comp[4];
    uint16_t qt[4][64];
    int32_t crr[256], cbb[256], crg[256], cbg[256];
    uint8_t range[1024], limit[1024];
    jpg_huff huff[8];
} jpg;

/* the canonical code of the counts (by length 1..16) and symbols: 0 when it is none --
   a length oversubscribed, a code of all ones (libjpeg's test), a DC symbol past 15 */
static int jpg_build(jpg_huff *h, const uint8_t *counts, const uint8_t *vals, int nv, int dc) {
    memset(h, 0, sizeof *h);
    int32_t code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        int c = counts[l - 1];
        h->valoff[l] = k - code;
        h->maxcode[l] = c ? code + c - 1 : -1;
        for (int i = 0; i < c; i++) {
            if (code >= (1 << l)) return 0;
            if (l <= 9) {
                int base = code << (9 - l), span = 1 << (9 - l);
                for (int s = 0; s < span; s++) {
                    h->look_len[base + s] = (uint8_t)l;
                    h->look_val[base + s] = vals[k];
                }
            }
            code++;
            k++;
        }
        if (code >= (1 << l)) return 0;
        code <<= 1;
    }
    for (int i = 0; i < nv; i++) {
        if (dc && vals[i] > 15) return 0;
        h->vals[i] = vals[i];
    }
    h->defined = 1;
    return 1;
}

/* acc filled to more than 56 bits: a stuffed FF 00 is FF; FF before a marker is the
   data's end, and so is the end of the bytes */
static void jpg_fill(const uint8_t *p, size_t n, jpg_bits *b) {
    while (b->cnt <= 56) {
        uint32_t c = 0;
        if (!b->hit) {
            if (b->pos >= n) {
                b->hit = 1;
                b->marker = 0;
            } else if (p[b->pos] != 0xFF) c = p[b->pos++];
            else {
                size_t q = b->pos + 1;
                while (q < n && p[q] == 0xFF) q++;
                if (q < n && p[q] == 0) {
                    c = 0xFF;
                    b->pos = q + 1;
                } else {
                    b->hit = 1;
                    b->marker = q < n ? p[q] : 0;
                    b->pos = q < n ? q + 1 : n;
                }
            }
        }
        if (b->hit) b->fake += 8;
        b->acc |= (uint64_t)c << (56 - b->cnt);
        b->cnt += 8;
    }
}

/* the next s bits (1 <= s <= 16) */
static int jpg_get(const uint8_t *p, size_t n, jpg_bits *b, int s) {
    if (b->cnt < s) jpg_fill(p, n, b);
    int v = (int)(b->acc >> (64 - s));
    b->acc <<= s;
    b->cnt -= s;
    return v;
}

/* s bits read as a signed magnitude (T.81 F.12) */
static int jpg_extend(int v, int s) { return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

/* the next symbol of table h; -1 when the bits are no code of it */
static int jpg_symbol(const uint8_t *p, size_t n, jpg_bits *b, const jpg_huff *h) {
    if (b->cnt < 16) jpg_fill(p, n, b);
    uint32_t look = (uint32_t)(b->acc >> 55);
    int l = h->look_len[look];
    if (l) {
        b->acc <<= l;
        b->cnt -= l;
        return h->look_val[look];
    }
    uint32_t code = (uint32_t)(b->acc >> 48);
    for (l = 10; l <= 16; l++) {
        int32_t c = (int32_t)(code >> (16 - l));
        if (c <= h->maxcode[l]) {
            b->acc <<= l;
            b->cnt -= l;
            return h->vals[(h->valoff[l] + c) & 255];
        }
    }
    return -1;
}

/* the islow inverse DCT of libjpeg (jidctint.c), so a sample is the one PIL decodes:
   13 bits of fraction, 2 more kept between the passes, a column or row with no AC term
   short-cut; each sample the range table's, which adds 128 and limits to 0..255 an
   index masked to 10 bits. ac: whether the block has an AC term at all. */
static void jpg_idct(const int16_t *in, const uint16_t *q, int ac, const uint8_t *range, uint8_t *out, size_t stride) {
    if (!ac) {
        uint8_t v = range[(int)(((int64_t)in[0] * q[0] * 4 + 16) >> 5) & 1023];
        for (int r = 0; r < 8; r++) memset(out + (size_t)r * stride, v, 8);
        return;
    }
    int32_t ws[64];
    for (int c = 0; c < 8; c++) {
        if (in[8 + c] == 0 && in[16 + c] == 0 && in[24 + c] == 0 && in[32 + c] == 0 && in[40 + c] == 0 && in[48 + c] == 0 && in[56 + c] == 0) {
            int32_t dc = (int32_t)((int64_t)in[c] * q[c] * 4);
            for (int r = 0; r < 8; r++) ws[r * 8 + c] = dc;
            continue;
        }
        int64_t z2 = (int64_t)in[16 + c] * q[16 + c], z3 = (int64_t)in[48 + c] * q[48 + c];
        int64_t z1 = (z2 + z3) * 4433;
        int64_t t2 = z1 - z3 * 15137, t3 = z1 + z2 * 6270;
        z2 = (int64_t)in[c] * q[c];
        z3 = (int64_t)in[32 + c] * q[32 + c];
        int64_t t0 = (z2 + z3) * 8192, t1 = (z2 - z3) * 8192;
        int64_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        t0 = (int64_t)in[56 + c] * q[56 + c];
        t1 = (int64_t)in[40 + c] * q[40 + c];
        t2 = (int64_t)in[24 + c] * q[24 + c];
        t3 = (int64_t)in[8 + c] * q[8 + c];
        z1 = t0 + t3;
        z2 = t1 + t2;
        z3 = t0 + t2;
        int64_t z4 = t1 + t3, z5 = (z3 + z4) * 9633;
        t0 *= 2446;
        t1 *= 16819;
        t2 *= 25172;
        t3 *= 12299;
        z1 *= -7373;
        z2 *= -20995;
        z3 = z3 * -16069 + z5;
        z4 = z4 * -3196 + z5;
        t0 += z1 + z3;
        t1 += z2 + z4;
        t2 += z2 + z3;
        t3 += z1 + z4;
        ws[c] = (int32_t)((t10 + t3 + 1024) >> 11);
        ws[56 + c] = (int32_t)((t10 - t3 + 1024) >> 11);
        ws[8 + c] = (int32_t)((t11 + t2 + 1024) >> 11);
        ws[48 + c] = (int32_t)((t11 - t2 + 1024) >> 11);
        ws[16 + c] = (int32_t)((t12 + t1 + 1024) >> 11);
        ws[40 + c] = (int32_t)((t12 - t1 + 1024) >> 11);
        ws[24 + c] = (int32_t)((t13 + t0 + 1024) >> 11);
        ws[32 + c] = (int32_t)((t13 - t0 + 1024) >> 11);
    }
    for (int r = 0; r < 8; r++) {
        const int32_t *w = ws + r * 8;
        uint8_t *o = out + (size_t)r * stride;
        if (w[1] == 0 && w[2] == 0 && w[3] == 0 && w[4] == 0 && w[5] == 0 && w[6] == 0 && w[7] == 0) {
            memset(o, range[(int)(((int64_t)w[0] + 16) >> 5) & 1023], 8);
            continue;
        }
        int64_t z2 = w[2], z3 = w[6];
        int64_t z1 = (z2 + z3) * 4433;
        int64_t t2 = z1 - z3 * 15137, t3 = z1 + z2 * 6270;
        int64_t t0 = ((int64_t)w[0] + w[4]) * 8192, t1 = ((int64_t)w[0] - w[4]) * 8192;
        int64_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        t0 = w[7];
        t1 = w[5];
        t2 = w[3];
        t3 = w[1];
        z1 = t0 + t3;
        z2 = t1 + t2;
        z3 = t0 + t2;
        int64_t z4 = t1 + t3, z5 = (z3 + z4) * 9633;
        t0 *= 2446;
        t1 *= 16819;
        t2 *= 25172;
        t3 *= 12299;
        z1 *= -7373;
        z2 *= -20995;
        z3 = z3 * -16069 + z5;
        z4 = z4 * -3196 + z5;
        t0 += z1 + z3;
        t1 += z2 + z4;
        t2 += z2 + z3;
        t3 += z1 + z4;
        o[0] = range[(int)((t10 + t3 + 131072) >> 18) & 1023];
        o[7] = range[(int)((t10 - t3 + 131072) >> 18) & 1023];
        o[1] = range[(int)((t11 + t2 + 131072) >> 18) & 1023];
        o[6] = range[(int)((t11 - t2 + 131072) >> 18) & 1023];
        o[2] = range[(int)((t12 + t1 + 131072) >> 18) & 1023];
        o[5] = range[(int)((t12 - t1 + 131072) >> 18) & 1023];
        o[3] = range[(int)((t13 + t0 + 131072) >> 18) & 1023];
        o[4] = range[(int)((t13 - t0 + 131072) >> 18) & 1023];
    }
}

/* ---- JPEG: the entropy-coded scans ---- */

/* a sequential block's coefficients into blk (zeroed): its DC by the predictor; the
   zigzag index of its last coefficient (0 when it has only the DC), -1 when the bits
   are no code or run past the block */
static int jpg_block(const uint8_t *p, size_t n, jpg_bits *b, const jpg_huff *dc, const jpg_huff *ac, int32_t *pred, int16_t *blk) {
    int t = jpg_symbol(p, n, b, dc);
    if (t < 0) return -1;
    int diff = t ? jpg_extend(jpg_get(p, n, b, t), t) : 0;
    *pred = (int32_t)((uint32_t)*pred + (uint32_t)diff);
    blk[0] = (int16_t)*pred;
    int k = 1, last = 0;
    while (k < 64) {
        int rs = jpg_symbol(p, n, b, ac);
        if (rs < 0) return -1;
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15) break;
            k += 16;
        } else {
            k += r;
            if (k > 63) return -1;
            blk[jpg_zz[k]] = (int16_t)jpg_extend(jpg_get(p, n, b, s), s);
            last = k;
            k++;
        }
    }
    return last;
}

/* a progressive AC scan's first pass over a block: the band ss..se, each coefficient
   shifted up al bits; a run of bands with nothing left in them counted in *eob */
static int jpg_ac_first(const uint8_t *p, size_t n, jpg_bits *b, const jpg_huff *ac, int32_t *eob, int ss, int se, int al, int16_t *cf) {
    if (*eob > 0) {
        (*eob)--;
        return 0;
    }
    for (int k = ss; k <= se; k++) {
        int rs = jpg_symbol(p, n, b, ac);
        if (rs < 0) return -1;
        int r = rs >> 4, s = rs & 15;
        if (s) {
            k += r;
            if (k > 63) return -1;
            cf[jpg_zz[k]] = (int16_t)(uint32_t)((uint32_t)jpg_extend(jpg_get(p, n, b, s), s) << al);
        } else if (r == 15) k += 15;
        else {
            *eob = (1 << r) - 1;
            if (r) *eob += jpg_get(p, n, b, r);
            break;
        }
    }
    return 0;
}

/* a refining AC pass (libjpeg's decode_mcu_AC_refine): a bit more of each coefficient
   already nonzero, and new coefficients of +-2^al placed among the zero ones */
static int jpg_ac_refine(const uint8_t *p, size_t n, jpg_bits *b, const jpg_huff *ac, int32_t *eob, int ss, int se, int al, int16_t *cf) {
    int p1 = 1 << al, m1 = -(1 << al), k = ss;
    if (*eob == 0) {
        for (; k <= se; k++) {
            int rs = jpg_symbol(p, n, b, ac);
            if (rs < 0) return -1;
            int r = rs >> 4, s = rs & 15;
            if (s) s = jpg_get(p, n, b, 1) ? p1 : m1;
            else if (r != 15) {
                *eob = 1 << r;
                if (r) *eob += jpg_get(p, n, b, r);
                break;
            }
            /* over the nonzero coefficients, refining each, and r zero ones */
            while (k <= se) {
                int z = jpg_zz[k];
                if (cf[z] != 0) {
                    if (jpg_get(p, n, b, 1) && (cf[z] & p1) == 0) cf[z] = (int16_t)(cf[z] >= 0 ? cf[z] + p1 : cf[z] + m1);
                } else if (--r < 0) break;
                k++;
            }
            if (s && k <= se) cf[jpg_zz[k]] = (int16_t)s;
        }
    }
    if (*eob > 0) {
        /* the band's remaining nonzero coefficients refined */
        for (; k <= se; k++) {
            int z = jpg_zz[k];
            if (cf[z] != 0 && jpg_get(p, n, b, 1) && (cf[z] & p1) == 0) cf[z] = (int16_t)(cf[z] >= 0 ? cf[z] + p1 : cf[z] + m1);
        }
        (*eob)--;
    }
    return 0;
}

/* block (bx, by) of component ci: a sequential one decoded into the scratch block and
   at once turned into samples in its plane (one outside the image is decoded only);
   a progressive one's coefficients brought on by this scan. -1 when its bits broke. */
static int jpg_unit(jpg *j, int ci, const uint8_t *p, size_t n, jpg_bits *b, uint8_t *planes, int16_t *coefs, uint32_t bx, uint32_t by) {
    if (!j->progressive) {
        memset(coefs, 0, 64 * sizeof(int16_t));
        int last = jpg_block(p, n, b, &j->huff[j->comp[ci].td], &j->huff[4 + j->comp[ci].ta], &j->comp[ci].pred, coefs);
        if (last < 0) return -1;
        if (bx < j->comp[ci].nbx && by < j->comp[ci].nby) {
            size_t stride = (size_t)j->comp[ci].bw * 8;
            jpg_idct(coefs, j->comp[ci].q, last > 0, j->range, planes + j->comp[ci].poff + (size_t)by * 8 * stride + (size_t)bx * 8, stride);
        }
        return 0;
    }
    int16_t *cf = coefs + j->comp[ci].coff + ((size_t)by * j->comp[ci].bw + bx) * 64;
    if (j->ss == 0) {
        if (j->ah == 0) {
            int t = jpg_symbol(p, n, b, &j->huff[j->comp[ci].td]);
            if (t < 0) return -1;
            int diff = t ? jpg_extend(jpg_get(p, n, b, t), t) : 0;
            j->comp[ci].pred = (int32_t)((uint32_t)j->comp[ci].pred + (uint32_t)diff);
            cf[0] = (int16_t)(uint32_t)((uint32_t)j->comp[ci].pred << j->al);
        } else if (jpg_get(p, n, b, 1)) cf[0] = (int16_t)(cf[0] | (1 << j->al));
        return 0;
    }
    if (j->ah == 0) return jpg_ac_first(p, n, b, &j->huff[4 + j->comp[ci].ta], &j->eobrun, j->ss, j->se, j->al, cf);
    return jpg_ac_refine(p, n, b, &j->huff[4 + j->comp[ci].ta], &j->eobrun, j->ss, j->se, j->al, cf);
}

/* the marker that ends the entropy-coded data at b, read to past the bytes before it */
static void jpg_to_marker(const uint8_t *p, size_t n, jpg_bits *b) {
    if (b->hit) return;
    b->acc = 0;
    b->cnt = 0;
    jpg_fill(p, n, b);
    if (b->hit) return;
    for (size_t q = b->pos; q + 1 < n; q++)
        if (p[q] == 0xFF && p[q + 1] != 0 && p[q + 1] != 0xFF) {
            b->hit = 1;
            b->marker = p[q + 1];
            b->pos = q + 2;
            return;
        }
    b->hit = 1;
    b->marker = 0;
    b->pos = n;
}

/* a restart interval's end, resynchronized as libjpeg does (jpeg_resync_to_restart): the
   marker RSTn expected next is read, and the bits, the predictors and the EOB run begin
   again. A marker one or two restarts on is left for later and this interval left empty,
   as is every interval once a marker ends the scan; one a restart or two back is passed
   over for the next marker; any other restart is taken. 1 when the interval that follows
   can be decoded. */
static int jpg_restart(jpg *j, const uint8_t *p, size_t n, jpg_bits *b) {
    for (;;) {
        jpg_to_marker(p, n, b);
        int mk = b->marker, d = (mk - 0xD0 - j->rst) & 7;
        if (mk < 0xD0 || mk > 0xD7 || d == 1 || d == 2) {
            j->rst = (j->rst + 1) & 7;
            j->partial = 1;
            return 0;
        }
        if (d == 0) break;
        j->partial = 1;
        if (d < 6) break;
        b->hit = 0;
    }
    b->acc = 0;
    b->cnt = 0;
    b->hit = 0;
    b->marker = 0;
    b->fake = 0;
    for (int s = 0; s < j->ns; s++) j->comp[j->sc[s]].pred = 0;
    j->eobrun = 0;
    j->rst = (j->rst + 1) & 7;
    return 1;
}

/* a scan, MCU by MCU: interleaved, each component's h x v blocks; alone, its blocks that
   hold the image. Where the bits run out or break, the rest of the restart interval is
   left as it is (grey, or its coefficients so far) and marked partial. */
static void jpg_scan(jpg *j, const uint8_t *p, size_t n, jpg_bits *b, uint8_t *planes, int16_t *coefs) {
    uint32_t across = j->ns == 1 ? j->comp[j->sc[0]].nbx : j->mcux;
    uint32_t total = j->ns == 1 ? across * j->comp[j->sc[0]].nby : j->mcux * j->mcuy;
    uint32_t togo = (uint32_t)j->restart;
    int dead = 0;
    j->eobrun = 0;
    j->rst = 0;
    for (int s = 0; s < j->ns; s++) j->comp[j->sc[s]].pred = 0;
    for (uint32_t m = 0; m < total; m++) {
        if (j->restart) {
            if (togo == 0) {
                dead = !jpg_restart(j, p, n, b);
                togo = (uint32_t)j->restart;
            }
            togo--;
        }
        if (dead) {
            j->partial = 1;
            continue;
        }
        uint32_t mx = m % across, my = m / across;
        for (int s = 0; s < j->ns && !dead; s++) {
            int ci = j->sc[s];
            int nh = j->ns == 1 ? 1 : j->comp[ci].h, nv = j->ns == 1 ? 1 : j->comp[ci].v;
            for (int v = 0; v < nv && !dead; v++)
                for (int u = 0; u < nh && !dead; u++) {
                    uint32_t bx = j->ns == 1 ? mx : mx * (uint32_t)j->comp[ci].h + (uint32_t)u;
                    uint32_t by = j->ns == 1 ? my : my * (uint32_t)j->comp[ci].v + (uint32_t)v;
                    if (jpg_unit(j, ci, p, n, b, planes, coefs, bx, by) < 0) dead = 1;
                }
        }
        if (dead || b->cnt < b->fake) {
            dead = 1;
            j->partial = 1;
        }
    }
    jpg_to_marker(p, n, b);
}

/* ---- JPEG: the segments ---- */

/* the frame header (SOF0, 1 or 2): the size, the components and their sampling, the MCU
   grid, and where each component's coefficients and plane lie; 0 when it is not one
   this reads, or the image is past the cap */
static int jpg_sof(jpg *j, const uint8_t *d, size_t dl) {
    if (dl < 6 || d[0] != 8) return 0;
    j->h = (uint32_t)d[1] << 8 | d[2];
    j->w = (uint32_t)d[3] << 8 | d[4];
    j->nc = d[5];
    if (j->w == 0 || j->h == 0 || (uint64_t)j->w * j->h > AMB_IMAGE_MAX_PIXELS) return 0;
    if ((j->nc != 1 && j->nc != 3 && j->nc != 4) || dl < 6 + 3 * (size_t)j->nc) return 0;
    j->hmax = 1;
    j->vmax = 1;
    for (int c = 0; c < j->nc; c++) {
        const uint8_t *e = d + 6 + 3 * c;
        j->comp[c].id = e[0];
        j->comp[c].h = e[1] >> 4;
        j->comp[c].v = e[1] & 15;
        j->comp[c].tq = e[2];
        if (j->comp[c].h < 1 || j->comp[c].h > 4 || j->comp[c].v < 1 || j->comp[c].v > 4 || j->comp[c].tq > 3) return 0;
        if (j->comp[c].h > j->hmax) j->hmax = j->comp[c].h;
        if (j->comp[c].v > j->vmax) j->vmax = j->comp[c].v;
    }
    j->mcux = (j->w + 8 * (uint32_t)j->hmax - 1) / (8 * (uint32_t)j->hmax);
    j->mcuy = (j->h + 8 * (uint32_t)j->vmax - 1) / (8 * (uint32_t)j->vmax);
    size_t coff = 0, poff = 0;
    for (int c = 0; c < j->nc; c++) {
        jpg_comp *k = &j->comp[c];
        k->bw = j->mcux * (uint32_t)k->h;
        k->bh = j->mcuy * (uint32_t)k->v;
        k->cw = (j->w * (uint32_t)k->h + (uint32_t)j->hmax - 1) / (uint32_t)j->hmax;
        k->ch = (j->h * (uint32_t)k->v + (uint32_t)j->vmax - 1) / (uint32_t)j->vmax;
        k->nbx = (k->cw + 7) / 8;
        k->nby = (k->ch + 7) / 8;
        k->coff = coff;
        k->poff = poff;
        coff += (size_t)k->bw * k->bh * 64;
        poff += (size_t)k->bw * k->bh * 64;
    }
    return 1;
}

/* a table of standard codes made for slot 0 or 1 of DC (0-3) or AC (4-7) when the file
   left it undefined; 0 when the slot has none */
static int jpg_table(jpg *j, int slot) {
    if (j->huff[slot].defined) return 1;
    int t = slot & 3;
    if (t > 1) return 0;
    if (slot < 4) return jpg_build(&j->huff[slot], jpg_std_bits[t], jpg_std_dc, 12, 1);
    if (t) return jpg_build(&j->huff[slot], jpg_std_bits[3], jpg_std_ac_chroma, 162, 0);
    return jpg_build(&j->huff[slot], jpg_std_bits[2], jpg_std_ac_luma, 162, 0);
}

/* the scan header: its components (each the first frame component of its id not yet in
   the scan) and their tables, its band and its approximation, checked as libjpeg checks
   them; each component's quantization copied at its first scan. 0 when it is not one. */
static int jpg_sos(jpg *j, const uint8_t *d, size_t dl) {
    if (dl < 1) return 0;
    int ns = d[0], blocks = 0;
    if (ns < 1 || ns > 4 || dl < 4 + 2 * (size_t)ns) return 0;
    for (int s = 0; s < ns; s++) {
        int ci = -1;
        for (int c = 0; c < j->nc && ci < 0; c++) {
            int used = 0;
            for (int k = 0; k < s; k++)
                if (j->sc[k] == c) used = 1;
            if (j->comp[c].id == d[1 + 2 * s] && !used) ci = c;
        }
        if (ci < 0) return 0;
        j->sc[s] = ci;
        j->comp[ci].td = d[2 + 2 * s] >> 4;
        j->comp[ci].ta = d[2 + 2 * s] & 15;
        if (j->comp[ci].td > 3 || j->comp[ci].ta > 3) return 0;
        blocks += j->comp[ci].h * j->comp[ci].v;
    }
    j->ns = ns;
    if (ns > 1 && blocks > 10) return 0;
    j->ss = d[1 + 2 * ns];
    j->se = d[2 + 2 * ns];
    j->ah = d[3 + 2 * ns] >> 4;
    j->al = d[3 + 2 * ns] & 15;
    if (!j->progressive) {
        j->ss = 0;
        j->se = 63;
        j->ah = 0;
        j->al = 0;
    } else if (j->ss > j->se || j->se > 63 || (j->ss == 0 && j->se != 0) || (j->ss > 0 && ns != 1) || (j->ah != 0 && j->ah != j->al + 1) ||
               j->al > 13)
        return 0;
    for (int s = 0; s < ns; s++) {
        int ci = j->sc[s];
        if ((!j->progressive || (j->ss == 0 && j->ah == 0)) && !jpg_table(j, j->comp[ci].td)) return 0;
        if ((!j->progressive || j->ss > 0) && !jpg_table(j, 4 + j->comp[ci].ta)) return 0;
        if (!j->comp[ci].latched) {
            if (!j->qdef[j->comp[ci].tq]) return 0;
            memcpy(j->comp[ci].q, j->qt[j->comp[ci].tq], sizeof j->comp[ci].q);
            j->comp[ci].latched = 1;
        }
    }
    return 1;
}

/* DQT: tables of 8- or 16-bit steps, zigzag order, kept in natural order */
static int jpg_dqt(jpg *j, const uint8_t *d, size_t dl) {
    size_t k = 0;
    while (k < dl) {
        int pq = d[k] >> 4, tq = d[k] & 15;
        size_t need = 1 + 64 * (size_t)(pq + 1);
        if (pq > 1 || tq > 3 || need > dl - k) return 0;
        for (int i = 0; i < 64; i++) j->qt[tq][jpg_zz[i]] = pq ? (uint16_t)(d[k + 1 + 2 * i] << 8 | d[k + 2 + 2 * i]) : d[k + 1 + i];
        j->qdef[tq] = 1;
        k += need;
    }
    return 1;
}

/* DHT: tables of counts by length and their symbols */
static int jpg_dht(jpg *j, const uint8_t *d, size_t dl) {
    size_t k = 0;
    while (k < dl) {
        if (17 > dl - k) return 0;
        int tc = d[k] >> 4, th = d[k] & 15, nv = 0;
        if (tc > 1 || th > 3) return 0;
        for (int l = 1; l <= 16; l++) nv += d[k + l];
        if (nv > 256 || (size_t)nv > dl - k - 17) return 0;
        if (!jpg_build(&j->huff[tc * 4 + th], d + k + 1, d + k + 17, nv, tc == 0)) return 0;
        k += 17 + (size_t)nv;
    }
    return 1;
}

static uint32_t jpg_u16(const uint8_t *t, int le) { return le ? (uint32_t)t[0] | (uint32_t)t[1] << 8 : (uint32_t)t[0] << 8 | t[1]; }
static uint32_t jpg_u32(const uint8_t *t, int le) { return le ? jpg_u16(t, 1) | jpg_u16(t + 2, 1) << 16 : jpg_u16(t, 0) << 16 | jpg_u16(t + 2, 0); }

/* the orientation (1 to 8) in the first IFD of an Exif segment's TIFF structure; 1 when
   it has none */
static int jpg_orientation(const uint8_t *t, size_t tn) {
    if (tn < 8 || !((t[0] == 'I' && t[1] == 'I') || (t[0] == 'M' && t[1] == 'M'))) return 1;
    int le = t[0] == 'I';
    if (jpg_u16(t + 2, le) != 42) return 1;
    uint32_t ifd = jpg_u32(t + 4, le);
    if (ifd > tn - 2) return 1;
    uint32_t count = jpg_u16(t + ifd, le);
    for (uint32_t e = 0; e < count; e++) {
        size_t off = (size_t)ifd + 2 + (size_t)e * 12;
        if (off + 12 > tn) break;
        if (jpg_u16(t + off, le) != 0x0112) continue;
        uint32_t type = jpg_u16(t + off + 2, le), v = 0;
        if (type == 3) v = jpg_u16(t + off + 8, le);
        else if (type == 4) v = jpg_u32(t + off + 8, le);
        return v >= 1 && v <= 8 ? (int)v : 1;
    }
    return 1;
}

/* a table or application segment read into j: DHT, DQT, DRI, JFIF's APP0, the first
   Exif APP1, Adobe's APP14; 0 when it is malformed */
static int jpg_segment(jpg *j, int mk, const uint8_t *d, size_t dl, int *exif) {
    if (mk == 0xC4) return jpg_dht(j, d, dl);
    if (mk == 0xDB) return jpg_dqt(j, d, dl);
    if (mk == 0xDD) {
        if (dl < 2) return 0;
        j->restart = d[0] << 8 | d[1];
        return 1;
    }
    if (mk == 0xE0 && dl >= 14 && memcmp(d, "JFIF", 5) == 0) j->jfif = 1;
    if (mk == 0xE1 && !*exif && dl >= 6 && memcmp(d, "Exif\0", 6) == 0) {
        j->orientation = jpg_orientation(d + 6, dl - 6);
        *exif = 1;
    }
    if (mk == 0xEE && dl >= 12 && memcmp(d, "Adobe", 5) == 0) {
        j->adobe = 1;
        j->transform = d[11];
    }
    return 1;
}

/* ---- JPEG: samples to pixels ---- */

/* the progressive coefficients, all scans read, turned into samples */
static void jpg_finish(const jpg *j, const int16_t *coefs, uint8_t *planes) {
    for (int ci = 0; ci < j->nc; ci++) {
        size_t stride = (size_t)j->comp[ci].bw * 8;
        for (uint32_t by = 0; by < j->comp[ci].nby; by++)
            for (uint32_t bx = 0; bx < j->comp[ci].nbx; bx++) {
                const int16_t *cf = coefs + j->comp[ci].coff + ((size_t)by * j->comp[ci].bw + bx) * 64;
                int ac = 0;
                for (int k = 1; k < 64; k++) ac |= cf[k];
                jpg_idct(cf, j->comp[ci].q, ac != 0, j->range, planes + j->comp[ci].poff + (size_t)by * 8 * stride + (size_t)bx * 8, stride);
            }
    }
}

/* the colour space, as libjpeg tells it: JFIF is YCbCr; Adobe's transform 0 is RGB or
   CMYK, any other YCbCr or YCCK; else three components are RGB by the ids R, G, B and
   YCbCr otherwise, four CMYK */
static int jpg_space(const jpg *j) {
    if (j->nc == 1) return JPG_GREY;
    if (j->nc == 4) return j->adobe && j->transform != 0 ? JPG_YCCK : JPG_CMYK;
    if (j->jfif) return JPG_YCC;
    if (j->adobe) return j->transform == 0 ? JPG_RGB : JPG_YCC;
    if (j->comp[0].id == 82 && j->comp[1].id == 71 && j->comp[2].id == 66) return JPG_RGB;
    return JPG_YCC;
}

/* row y of component ci's samples, upsampled to the image's width into out, as libjpeg
   upsamples (jdsample.c): its fancy filters where it uses them -- 2:1 across, 2:1 down,
   or both, of a component more than 2 samples wide (2:1 down alone, of any), each new
   sample 3/4 of its nearer source and 1/4 of the further, the edges repeated -- box
   replication at another whole ratio, and at a fractional one, which libjpeg refuses,
   the nearest sample */
static void jpg_row(const jpg *j, int ci, uint32_t y, const uint8_t *planes, uint8_t *out) {
    size_t stride = (size_t)j->comp[ci].bw * 8;
    const uint8_t *pl = planes + j->comp[ci].poff;
    uint32_t W = j->w, cw = j->comp[ci].cw, ch = j->comp[ci].ch, h = (uint32_t)j->comp[ci].h, v = (uint32_t)j->comp[ci].v;
    uint32_t rx = (uint32_t)j->hmax / h, ry = (uint32_t)j->vmax / v;
    if (rx * h != (uint32_t)j->hmax || ry * v != (uint32_t)j->vmax) {
        uint32_t sy = (uint32_t)((uint64_t)y * v / (uint32_t)j->vmax);
        const uint8_t *in = pl + (size_t)(sy < ch ? sy : ch - 1) * stride;
        for (uint32_t x = 0; x < W; x++) {
            uint32_t sx = (uint32_t)((uint64_t)x * h / (uint32_t)j->hmax);
            out[x] = in[sx < cw ? sx : cw - 1];
        }
        return;
    }
    if (ry == 2 && (rx == 1 || (rx == 2 && cw > 2))) {
        uint32_t cy = y >> 1, ny = (y & 1) ? (cy + 1 < ch ? cy + 1 : cy) : (cy > 0 ? cy - 1 : 0);
        const uint8_t *r0 = pl + (size_t)cy * stride, *r1 = pl + (size_t)ny * stride;
        if (rx == 1) {
            int bias = (y & 1) ? 2 : 1;
            for (uint32_t x = 0; x < W; x++) out[x] = (uint8_t)((r0[x] * 3 + r1[x] + bias) >> 2);
            return;
        }
        int prev = r0[0] * 3 + r1[0], cur = prev;
        for (uint32_t x = 0; x < cw; x++) {
            int nx = x + 1 < cw ? r0[x + 1] * 3 + r1[x + 1] : cur;
            out[2 * x] = (uint8_t)((cur * 3 + prev + 8) >> 4);
            out[2 * x + 1] = (uint8_t)((cur * 3 + nx + 7) >> 4);
            prev = cur;
            cur = nx;
        }
        return;
    }
    const uint8_t *in = pl + (size_t)(y / ry) * stride;
    if (rx == 1) memcpy(out, in, W);
    else if (rx == 2 && ry == 1 && cw > 2) {
        for (uint32_t x = 0; x < cw; x++) {
            int c3 = in[x] * 3, l = in[x > 0 ? x - 1 : 0], r = in[x + 1 < cw ? x + 1 : x];
            out[2 * x] = (uint8_t)((c3 + l + 1) >> 2);
            out[2 * x + 1] = (uint8_t)((c3 + r + 2) >> 2);
        }
    } else
        for (uint32_t x = 0; x < cw; x++) memset(out + (size_t)x * rx, in[x], rx);
}

static uint8_t jpg_clamp(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* a * b / 255 as PIL rounds it (MULDIV255) */
static int jpg_mul255(int a, int b) {
    int t = a * b + 128;
    return (t + (t >> 8)) >> 8;
}

/* libjpeg's tables of the YCbCr conversion (jdcolor.c, 16 bits of fraction), by
   sample: Cr's part of red and Cb's of blue, rounded; Cr's and Cb's parts of green,
   summed and then rounded; and the range limit, a value at index v + 384 */
static void jpg_tables(jpg *j) {
    for (int k = 0; k < 256; k++) {
        int x = k - 128;
        j->crr[k] = (91881 * x + 32768) >> 16;
        j->cbb[k] = (116130 * x + 32768) >> 16;
        j->crg[k] = -46802 * x;
        j->cbg[k] = -22554 * x + 32768;
    }
    for (int k = 0; k < 1024; k++) j->limit[k] = jpg_clamp(k - 384);
}

/* a row of the components' samples (rows rw apart in rb) made RGBA pixels at o[dst],
   step bytes apart: YCbCr by libjpeg's tables; CMYK and YCCK as PIL takes them,
   inverted (Adobe's), each channel (255 - C)(255 - K) */
static void jpg_color(const jpg *j, int space, const uint8_t *rb, size_t rw, uint32_t W, uint8_t *o, ptrdiff_t dst, ptrdiff_t step) {
    const uint8_t *c0 = rb, *c1 = rb + rw, *c2 = rb + 2 * rw, *c3 = rb + 3 * rw;
    const int32_t *crr = j->crr, *cbb = j->cbb, *crg = j->crg, *cbg = j->cbg;
    const uint8_t *lim = j->limit + 384;
    if (space == JPG_GREY) {
        for (uint32_t x = 0; x < W; x++, dst += step) {
            o[dst] = c0[x];
            o[dst + 1] = c0[x];
            o[dst + 2] = c0[x];
            o[dst + 3] = 255;
        }
    } else if (space == JPG_RGB) {
        for (uint32_t x = 0; x < W; x++, dst += step) {
            o[dst] = c0[x];
            o[dst + 1] = c1[x];
            o[dst + 2] = c2[x];
            o[dst + 3] = 255;
        }
    } else if (space == JPG_YCC) {
        for (uint32_t x = 0; x < W; x++, dst += step) {
            int y = c0[x], cb = c1[x], cr = c2[x];
            o[dst] = lim[y + crr[cr]];
            o[dst + 1] = lim[y + ((cbg[cb] + crg[cr]) >> 16)];
            o[dst + 2] = lim[y + cbb[cb]];
            o[dst + 3] = 255;
        }
    } else {
        for (uint32_t x = 0; x < W; x++, dst += step) {
            int r = 255 - c0[x], g = 255 - c1[x], bl = 255 - c2[x], k = c3[x];
            if (space == JPG_YCCK) {
                /* YCC made RGB, which is the inverse of what libjpeg hands on as CMY */
                int y = c0[x], cb = c1[x], cr = c2[x];
                r = lim[y + crr[cr]];
                g = lim[y + ((cbg[cb] + crg[cr]) >> 16)];
                bl = lim[y + cbb[cb]];
            }
            o[dst] = (uint8_t)(k - jpg_mul255(r, k));
            o[dst + 1] = (uint8_t)(k - jpg_mul255(g, k));
            o[dst + 2] = (uint8_t)(k - jpg_mul255(bl, k));
            o[dst + 3] = 255;
        }
    }
}

/* where decoded row y's first pixel goes in the oriented image, and the step to the
   next: EXIF's 2 to 8 are a mirror, a half turn, a flip, a transpose, a quarter turn
   clockwise, a transverse, a quarter turn counter-clockwise (the last four swap the
   width and height) */
static void jpg_place(int o, uint32_t W, uint32_t H, uint32_t y, ptrdiff_t *dst, ptrdiff_t *step) {
    ptrdiff_t w = (ptrdiff_t)W, h = (ptrdiff_t)H, r = (ptrdiff_t)y;
    *dst = r * w * 4;
    *step = 4;
    if (o == 2) { *dst = (r * w + w - 1) * 4; *step = -4; }
    else if (o == 3) { *dst = ((h - 1 - r) * w + w - 1) * 4; *step = -4; }
    else if (o == 4) *dst = (h - 1 - r) * w * 4;
    else if (o == 5) { *dst = r * 4; *step = h * 4; }
    else if (o == 6) { *dst = (h - 1 - r) * 4; *step = h * 4; }
    else if (o == 7) { *dst = ((w - 1) * h + h - 1 - r) * 4; *step = -h * 4; }
    else if (o == 8) { *dst = ((w - 1) * h + r) * 4; *step = -h * 4; }
}

int amb_jpeg_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info) {
    memset(info, 0, sizeof *info);
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 0;
    jpg j;
    memset(&j, 0, sizeof j);
    j.orientation = 1;
    for (int k = 0; k < 1024; k++) j.range[k] = (uint8_t)(k < 128 ? k + 128 : k < 512 ? 255 : k < 896 ? 0 : k - 896);
    vector<uint8_t> planes;
    vector<int16_t> coef;
    jpg_bits b;
    memset(&b, 0, sizeof b);
    size_t i = 2;
    int m = 0, frame = 0, ended = 0, exif = 0;
    for (;;) {
        if (!m) {
            /* the next marker: past any bytes that are none, any 0xFF fill */
            while (i < n && p[i] != 0xFF) i++;
            while (i < n && p[i] == 0xFF) i++;
            if (i >= n) break;
            m = p[i++];
            if (m == 0) continue;
        }
        int mk = m;
        m = 0;
        if (mk == 0xD9) {
            ended = 1;
            break;
        }
        if (mk == 0xD8 || mk == 0x01 || (mk >= 0xD0 && mk <= 0xD7)) continue;
        if (2 > n - i) break;
        size_t sl = (size_t)p[i] << 8 | p[i + 1];
        if (sl < 2 || sl > n - i) break;
        const uint8_t *d = p + i + 2;
        size_t dl = sl - 2;
        i += sl;
        if (mk == 0xDA) {
            if (!frame || !jpg_sos(&j, d, dl)) break;
            memset(&b, 0, sizeof b);
            b.pos = i;
            jpg_scan(&j, p, n, &b, data(planes), data(coef));
            j.scans++;
            i = b.pos;
            m = b.marker;
            if (!m) break;
            continue;
        }
        if (mk >= 0xC0 && mk <= 0xCF && mk != 0xC4 && mk != 0xC8 && mk != 0xCC) {
            /* a frame: baseline, extended or progressive Huffman; the others are not read */
            if (frame) break;
            if (mk > 0xC2) return 0;
            j.progressive = mk == 0xC2;
            if (!jpg_sof(&j, d, dl)) return 0;
            size_t blocks = (size_t)j.comp[j.nc - 1].coff / 64 + (size_t)j.comp[j.nc - 1].bw * j.comp[j.nc - 1].bh;
            resize(&planes, blocks * 64);
            memset(data(planes), 128, blocks * 64);
            resize(&coef, j.progressive ? blocks * 64 : 64);
            frame = 1;
            continue;
        }
        if (!jpg_segment(&j, mk, d, dl, &exif)) {
            if (j.scans) break;
            return 0;
        }
    }
    if (!frame || !j.scans) return 0;
    if (!ended) j.partial = 1;
    if (j.progressive) jpg_finish(&j, data(coef), data(planes));
    /* each row: its components upsampled, made colours, placed by the orientation */
    uint32_t W = j.w, H = j.h;
    int o = j.orientation;
    clear(*rgba);
    resize(rgba, (size_t)W * H * 4);
    vector<uint8_t> rows;
    size_t rw = (size_t)W + 16;
    resize(&rows, 4 * rw);
    int space = jpg_space(&j);
    const uint8_t *pl = data(planes);
    uint8_t *rb = data(rows);
    uint8_t *out = data(*rgba);
    jpg_tables(&j);
    for (uint32_t y = 0; y < H; y++) {
        for (int c = 0; c < j.nc; c++) jpg_row(&j, c, y, pl, rb + (size_t)c * rw);
        ptrdiff_t dst, step;
        jpg_place(o, W, H, y, &dst, &step);
        jpg_color(&j, space, rb, rw, W, out, dst, step);
    }
    info->w = o >= 5 ? H : W;
    info->h = o >= 5 ? W : H;
    info->frames = 1;
    info->orientation = (uint32_t)o;
    info->partial = j.partial;
    return 1;
}

/* ---- any of them ---- */

int amb_image_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    *w = 0;
    *h = 0;
    if (n >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') return amb_png_decode(p, n, rgba, w, h);
    amb_image_info info;
    int ok = 0;
    if (n >= 6 && p[0] == 'G' && p[1] == 'I' && p[2] == 'F') ok = amb_gif_decode(p, n, rgba, &info);
    else if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) ok = amb_jpeg_decode(p, n, rgba, &info);
    if (ok) {
        *w = info.w;
        *h = info.h;
    }
    return ok;
}

int amb_image_size(const uint8_t *p, size_t n, uint32_t *w, uint32_t *h) {
    *w = 0;
    *h = 0;
    if (n >= 24 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') {
        *w = png_be32(p + 16);
        *h = png_be32(p + 20);
        return 1;
    }
    if (n >= 10 && p[0] == 'G' && p[1] == 'I' && p[2] == 'F') {
        *w = p[6] | (uint32_t)p[7] << 8;
        *h = p[8] | (uint32_t)p[9] << 8;
        return 1;
    }
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 0;
    /* a JPEG's markers to its first scan: the frame's size, and the first Exif segment's orientation */
    size_t i = 2;
    int orient = 1, exif = 0;
    uint32_t W = 0, H = 0;
    while (i < n) {
        while (i < n && p[i] != 0xFF) i++;
        while (i < n && p[i] == 0xFF) i++;
        if (i >= n) break;
        int mk = p[i++];
        if (mk == 0 || mk == 0xD8 || mk == 0x01 || (mk >= 0xD0 && mk <= 0xD7)) continue;
        if (mk == 0xD9 || mk == 0xDA || 2 > n - i) break;
        size_t sl = (size_t)p[i] << 8 | p[i + 1];
        if (sl < 2 || sl > n - i) break;
        const uint8_t *d = p + i + 2;
        size_t dl = sl - 2;
        if (mk == 0xE1 && !exif && dl >= 6 && memcmp(d, "Exif\0", 6) == 0) {
            orient = jpg_orientation(d + 6, dl - 6);
            exif = 1;
        }
        if (!W && mk >= 0xC0 && mk <= 0xCF && mk != 0xC4 && mk != 0xC8 && mk != 0xCC && dl >= 5) {
            H = (uint32_t)d[1] << 8 | d[2];
            W = (uint32_t)d[3] << 8 | d[4];
        }
        i += sl;
    }
    if (!W || !H) return 0;
    *w = orient >= 5 ? H : W;
    *h = orient >= 5 ? W : H;
    return 1;
}
