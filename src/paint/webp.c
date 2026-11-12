/* src/paint/webp.c -- WebP decoded (image.h), as libwebp decodes it for Chromium and Pillow.

   The container (RIFF): a simple file's one VP8 or VP8L chunk; an extended file's VP8X
   canvas, its still image (an ALPH chunk before VP8 when the alpha flag is set) or its
   animation (ANIM's loop count, ANMF frames each placed, blended or not and disposed or
   not); ICCP, EXIF, XMP and unknown chunks passed over. A file is checked as libwebp's
   demuxer checks it: chunk sizes inside the RIFF, ANIM before ANMF, VP8L never after ALPH,
   no flag but the five, a still image the canvas's size, each frame inside the canvas.

   VP8L (lossless, RFC 9649): the transforms (predictor, cross-colour, subtract-green,
   colour-indexing with packed pixels), the colour cache, the entropy image of prefix-code
   groups, LZ77 copies with the distance plane codes; prefix codes built as libwebp builds
   them (a table of 8 bits and second-level tables), refused when over- or under-
   subscribed. ALPH: raw or VP8L-coded alpha, unfiltered (horizontal, vertical, gradient).

   VP8 (lossy key frames, RFC 6386): the boolean decoder, segments, the token partitions,
   intra prediction of 16x16, 4x4 and chroma blocks from a 32-byte-stride workspace (127
   above the frame, 129 left of it), the inverse WHT and DCT, the loop filter in its simple
   and normal forms with its sharpness, deltas and per-segment strength; then YUV 4:2:0 to
   RGB as libwebp converts it -- its "fancy" upsampling (9-3-3-1 sixteenths of the four
   nearest chroma samples) and its 14-bit fixed-point coefficients. Chromium's pixels are
   these (measured on a canvas and a screenshot: identical to Pillow's, unlike libwebp's
   point upsampling).

   An animation's frames are composed as libwebp's animation decoder composes them (what
   Pillow returns): a frame's pixels written over its rectangle and, when it blends, each
   not opaque blended over what was there in libwebp's integer arithmetic; a frame
   disposed to the background clears its rectangle to transparent (Chromium ignores
   ANIM's colour too). Every refusal -- a malformed or cut stream -- leaves the image
   undrawn, as Chromium leaves it, though the size its header gives is still laid out. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "image.h"

static uint32_t wp_le16(const uint8_t *q) { return (uint32_t)q[0] | (uint32_t)q[1] << 8; }
static uint32_t wp_le24(const uint8_t *q) { return wp_le16(q) | (uint32_t)q[2] << 16; }
static uint32_t wp_le32(const uint8_t *q) { return wp_le24(q) | (uint32_t)q[3] << 24; }

/* ---- VP8L: the lossless bitstream ---- */

#define WL_ROOT 8                   /* bits of a prefix code's first-level table */
#define WL_ALPHABET 2328            /* the largest alphabet: 256 + 24 + a cache of 2048 */
#define WL_MAX_TABLE (1u << 24)     /* table entries of one image's codes, at most */

/* the bits of a stream q[0, n), least significant first: acc holds cnt of them from
   bit used on; bits past the end read as zeros, and eos is set once more are taken than
   the stream holds (libwebp counts it as 8 bytes at least) */
typedef struct wl_br { size_t pos, n; uint64_t acc, used, limit; int32_t cnt, eos; } wl_br;

static void wl_begin(wl_br *b, size_t n) {
    memset(b, 0, sizeof *b);
    b->n = n;
    b->limit = 8 * (uint64_t)(n > 8 ? n : 8);
}

static void wl_fill(const uint8_t *q, wl_br *b) {
    while (b->cnt <= 56) {
        uint64_t c = 0;
        if (b->pos < b->n) c = q[b->pos++];
        b->acc |= c << b->cnt;
        b->cnt += 8;
    }
}

/* the next 32 bits, at least, not taken */
static uint32_t wl_peek(const uint8_t *q, wl_br *b) {
    if (b->cnt < 32) wl_fill(q, b);
    return (uint32_t)b->acc;
}

static void wl_skip(wl_br *b, int k) {
    b->acc >>= k;
    b->cnt -= k;
    b->used += (uint64_t)k;
    if (b->used > b->limit) b->eos = 1;
}

/* the next k bits (0 to 24) */
static uint32_t wl_bits(const uint8_t *q, wl_br *b, int k) {
    uint32_t v = wl_peek(q, b) & (uint32_t)((1ull << k) - 1);
    wl_skip(b, k);
    return v;
}

static int wl_sub_size(int size, int bits) { return (size + (1 << bits) - 1) >> bits; }

/* the key after key of a code of len bits, its bits reversed (libwebp's GetNextKey) */
static uint32_t wl_next_key(uint32_t key, int len) {
    uint32_t step = 1u << (len - 1);
    while (key & step) step >>= 1;
    return step ? (key & (step - 1)) + step : key;
}

static void wl_replicate(uint32_t *t, int step, int end, uint32_t code) {
    do {
        end -= step;
        t[end] = code;
    } while (end > 0);
}

/* the bits of the second-level table a code of len bits begins */
static int wl_table_bits(const int32_t *count, int len, int root) {
    int left = 1 << (len - root);
    while (len < 15) {
        left -= count[len];
        if (left <= 0) break;
        len++;
        left <<= 1;
    }
    return len - root;
}

/* the table of the canonical code of lengths[0, n), as libwebp builds it
   (BuildHuffmanTable): its size in entries, and when fill its entries in t -- each
   bits << 16 | symbol, or for a code longer than the root, at its first root bits, the
   second-level table's bits << 16 | its distance on. 0 when the lengths are no code: a
   length past 15, none at all, too many of a length, or a code over- or under-
   subscribed (one symbol alone is a code of no bits). */
static int wl_huff(const int32_t *lengths, int n, int root, int fill, uint32_t *t, uint16_t *sorted) {
    int32_t count[16], offset[16];
    memset(count, 0, sizeof count);
    memset(offset, 0, sizeof offset);
    for (int s = 0; s < n; s++) {
        if (lengths[s] < 0 || lengths[s] > 15) return 0;
        count[lengths[s]]++;
    }
    if (count[0] == n) return 0;
    for (int l = 1; l < 15; l++) {
        if (count[l] > (1 << l)) return 0;
        offset[l + 1] = offset[l] + count[l];
    }
    for (int s = 0; s < n; s++)
        if (lengths[s] > 0) {
            if (fill) sorted[offset[lengths[s]]] = (uint16_t)s;
            offset[lengths[s]]++;
        }
    int total = 1 << root;
    if (offset[15] == 1) {
        if (fill) wl_replicate(t, 1, total, sorted[0]);
        return total;
    }
    uint32_t low = 0xffffffffu, mask = (uint32_t)total - 1, key = 0;
    int nodes = 1, open = 1, table_size = 1 << root, sym = 0, tpos = 0, len, step;
    for (len = 1, step = 2; len <= root; len++, step <<= 1) {
        open <<= 1;
        nodes += open;
        open -= count[len];
        if (open < 0) return 0;
        if (!fill) continue;
        for (; count[len] > 0; count[len]--) {
            wl_replicate(t + key, step, table_size, (uint32_t)len << 16 | sorted[sym++]);
            key = wl_next_key(key, len);
        }
    }
    for (len = root + 1, step = 2; len <= 15; len++, step <<= 1) {
        open <<= 1;
        nodes += open;
        open -= count[len];
        if (open < 0) return 0;
        for (; count[len] > 0; count[len]--) {
            if ((key & mask) != low) {
                tpos += table_size;
                int table_bits = wl_table_bits(count, len, root);
                table_size = 1 << table_bits;
                total += table_size;
                low = key & mask;
                if (fill) t[low] = (uint32_t)(table_bits + root) << 16 | (uint32_t)(tpos - (int)low);
            }
            if (fill) wl_replicate(t + tpos + (key >> root), step, table_size, (uint32_t)(len - root) << 16 | sorted[sym++]);
            key = wl_next_key(key, len);
        }
    }
    if (nodes != 2 * offset[15] - 1) return 0;
    return total;
}

/* the next symbol of the code whose table starts at t */
static int wl_symbol(const uint8_t *q, wl_br *b, const uint32_t *t) {
    uint32_t val = wl_peek(q, b), e = t[val & 0xff];
    int nb = (int)(e >> 16);
    if (nb > WL_ROOT) {
        wl_skip(b, WL_ROOT);
        uint32_t v2 = wl_peek(q, b);
        e = t[(val & 0xff) + (e & 0xffff) + (v2 & ((1u << (nb - WL_ROOT)) - 1))];
    }
    wl_skip(b, (int)(e >> 16));
    return (int)(e & 0xffff);
}

static const uint8_t wl_cl_order[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* a normal code's lengths, coded by the code-length code of the lengths cl: literal
   lengths, 16 (the last non-zero length again, 3 to 6 times), 17 and 18 (3 to 10 and 11
   to 138 zeros); as many symbols as the stream says, if it says */
static int wl_lengths(const uint8_t *q, wl_br *b, const int32_t *cl, int n, int32_t *lengths) {
    uint32_t t[128];
    uint16_t sorted[19];
    if (!wl_huff(cl, 19, 7, 0, t, sorted)) return 0;
    wl_huff(cl, 19, 7, 1, t, sorted);
    int max_symbol = n;
    if (wl_bits(q, b, 1)) {
        int nbits = 2 + 2 * (int)wl_bits(q, b, 3);
        max_symbol = 2 + (int)wl_bits(q, b, nbits);
        if (max_symbol > n) return 0;
    }
    int sym = 0, prev = 8;
    while (sym < n) {
        if (max_symbol-- == 0) break;
        uint32_t e = t[wl_peek(q, b) & 127];
        wl_skip(b, (int)(e >> 16));
        int c = (int)(e & 0xffff);
        if (c < 16) {
            lengths[sym++] = c;
            if (c) prev = c;
            continue;
        }
        int repeat = c == 16 ? 3 + (int)wl_bits(q, b, 2) : c == 17 ? 3 + (int)wl_bits(q, b, 3) : 11 + (int)wl_bits(q, b, 7);
        if (sym + repeat > n) return 0;
        int v = c == 16 ? prev : 0;
        while (repeat-- > 0) lengths[sym++] = v;
    }
    return 1;
}

/* a prefix code of the alphabet read: simple (one or two symbols of one bit, or one of
   none) or normal; its table appended to tabs at *at when keep (lengths: scratch of
   WL_ALPHABET); 0 when it is malformed or the tables pass their cap */
static int wl_code(const uint8_t *q, wl_br *b, int alphabet, int32_t *lengths, vector<uint32_t> *tabs, uint32_t *at, int keep) {
    memset(lengths, 0, (size_t)alphabet * sizeof(int32_t));
    if (wl_bits(q, b, 1)) {
        int two = (int)wl_bits(q, b, 1);
        int first8 = (int)wl_bits(q, b, 1);
        lengths[wl_bits(q, b, first8 ? 8 : 1)] = 1;
        if (two) lengths[wl_bits(q, b, 8)] = 1;
    } else {
        int32_t cl[19];
        memset(cl, 0, sizeof cl);
        int num = (int)wl_bits(q, b, 4) + 4;
        for (int i = 0; i < num; i++) cl[wl_cl_order[i]] = (int32_t)wl_bits(q, b, 3);
        if (!wl_lengths(q, b, cl, alphabet, lengths)) return 0;
    }
    if (b->eos) return 0;
    uint16_t sorted[WL_ALPHABET];
    int size = wl_huff(lengths, alphabet, WL_ROOT, 0, data(*tabs), sorted);
    if (!size) return 0;
    if (!keep) return 1;
    size_t base = len(*tabs);
    if (base + (size_t)size > WL_MAX_TABLE) return 0;
    resize(tabs, base + (size_t)size);
    wl_huff(lengths, alphabet, WL_ROOT, 1, data(*tabs) + base, sorted);
    *at = (uint32_t)base;
    return 1;
}

/* a group's five codes: green with the lengths and the cache, red, blue, alpha, distance */
typedef struct wl_group { uint32_t tree[5]; } wl_group;

static const uint16_t wl_alphabet[5] = { 280, 256, 256, 256, 40 };

static int wl_sub(const uint8_t *q, wl_br *b, int xsize, int ysize, vector<uint32_t> *out);

/* an image's prefix codes: the entropy image naming each block's group (allow_meta), then
   each group's codes, a group the image never names read and checked but not kept; the
   entropy image is left as the kept groups' indices, *meta_bits 0 when there is none */
static int wl_codes(const uint8_t *q, wl_br *b, int xsize, int ysize, int cache_bits, int allow_meta, vector<uint32_t> *tabs,
                    vector<wl_group> *groups, vector<uint32_t> *meta, int *meta_bits) {
    int num = 1;
    *meta_bits = 0;
    clear(*meta);
    if (allow_meta && wl_bits(q, b, 1)) {
        int prec = (int)wl_bits(q, b, 3) + 2;
        if (!wl_sub(q, b, wl_sub_size(xsize, prec), wl_sub_size(ysize, prec), meta)) return 0;
        *meta_bits = prec;
        for (size_t i = 0; i < len(*meta); i++) {
            uint32_t g = (at(*meta, i) >> 8) & 0xffff;
            (*meta)[i] = g;
            if ((int)g >= num) num = (int)g + 1;
        }
    }
    if (b->eos) return 0;
    vector<int32_t> map;
    resize(&map, (size_t)num);
    int used = 0;
    if (*meta_bits) {
        for (int g = 0; g < num; g++) map[g] = -1;
        for (size_t i = 0; i < len(*meta); i++) {
            uint32_t g = at(*meta, i);
            if (at(map, g) < 0) map[g] = used++;
            (*meta)[i] = (uint32_t)at(map, g);
        }
    } else used = 1;
    vector<int32_t> lengths;
    resize(&lengths, WL_ALPHABET);
    clear(*groups);
    resize(groups, (size_t)used);
    for (int g = 0; g < num; g++) {
        wl_group gr;
        memset(&gr, 0, sizeof gr);
        int keep = at(map, g) >= 0;
        for (int j = 0; j < 5; j++) {
            int alphabet = wl_alphabet[j] + (j == 0 && cache_bits ? 1 << cache_bits : 0);
            if (!wl_code(q, b, alphabet, data(lengths), tabs, &gr.tree[j], keep)) return 0;
        }
        if (keep) (*groups)[at(map, g)] = gr;
    }
    return 1;
}

/* a copy's length or distance from its prefix symbol and extra bits */
static int wl_copy(const uint8_t *q, wl_br *b, int sym) {
    if (sym < 4) return sym + 1;
    int extra = (sym - 2) >> 1, offset = (2 + (sym & 1)) << extra;
    return offset + (int)wl_bits(q, b, extra) + 1;
}

/* the 120 distance codes nearest a pixel: dy << 4 | (8 - dx) */
static const uint8_t wl_plane_codes[120] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a, 0x26, 0x2a, 0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a,
    0x25, 0x2b, 0x48, 0x04, 0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b, 0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45, 0x4b, 0x34, 0x3c, 0x03,
    0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d, 0x44, 0x4c, 0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b, 0x32, 0x3e, 0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f,
    0x64, 0x6c, 0x42, 0x4e, 0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b, 0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e, 0x00, 0x74, 0x7c, 0x41,
    0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d, 0x51, 0x5f, 0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70 };

static int wl_plane(int xsize, int code) {
    if (code > 120) return code - 120;
    int c = wl_plane_codes[code - 1], dist = (c >> 4) * xsize + 8 - (c & 0xf);
    return dist >= 1 ? dist : 1;
}

/* the entropy-coded pixels of a width x height image into px: literals, copies and cache
   hits, each block's group from the entropy image; 0 when a code is malformed, a copy
   reaches outside the image, or the stream ends first */
static int wl_data(const uint8_t *q, wl_br *b, uint32_t *px, int width, int height, int cache_bits, const vector<uint32_t> *tabs,
                   const vector<wl_group> *groups, const vector<uint32_t> *meta, int meta_bits) {
    const uint32_t *t = data(*tabs);
    const wl_group *gs = data(*groups);
    const uint32_t *mp = data(*meta);
    size_t total = (size_t)width * (size_t)height, pos = 0;
    vector<uint32_t> cache;
    if (cache_bits) resize(&cache, (size_t)1 << cache_bits);
    uint32_t *cc = data(cache);
    int shift = 32 - cache_bits, ncache = cache_bits ? 1 << cache_bits : 0;
    int meta_x = meta_bits ? wl_sub_size(width, meta_bits) : 0, mask = meta_bits ? (1 << meta_bits) - 1 : -1;
    int col = 0, row = 0;
    uint32_t gi = 0;
    while (pos < total) {
        if ((col & mask) == 0) gi = meta_bits ? mp[(size_t)meta_x * (size_t)(row >> meta_bits) + (size_t)(col >> meta_bits)] : 0;
        int code = wl_symbol(q, b, t + gs[gi].tree[0]);
        if (b->eos) return 0;
        if (code < 256) {
            int red = wl_symbol(q, b, t + gs[gi].tree[1]);
            int blue = wl_symbol(q, b, t + gs[gi].tree[2]);
            int alpha = wl_symbol(q, b, t + gs[gi].tree[3]);
            if (b->eos) return 0;
            px[pos] = (uint32_t)alpha << 24 | (uint32_t)red << 16 | (uint32_t)code << 8 | (uint32_t)blue;
        } else if (code < 280) {
            int length = wl_copy(q, b, code - 256);
            int dsym = wl_symbol(q, b, t + gs[gi].tree[4]);
            int dist = wl_plane(width, wl_copy(q, b, dsym));
            if (b->eos || pos < (size_t)dist || total - pos < (size_t)length) return 0;
            for (int k = 0; k < length; k++) {
                px[pos] = px[pos - (size_t)dist];
                if (cache_bits) cc[(px[pos] * 0x1e35a7bdu) >> shift] = px[pos];
                pos++;
            }
            col += length;
            while (col >= width) {
                col -= width;
                row++;
            }
            if (col & mask) gi = meta_bits ? mp[(size_t)meta_x * (size_t)(row >> meta_bits) + (size_t)(col >> meta_bits)] : 0;
            continue;
        } else if (code - 280 < ncache) {
            px[pos] = cc[code - 280];
        } else return 0;
        if (cache_bits) cc[(px[pos] * 0x1e35a7bdu) >> shift] = px[pos];
        pos++;
        if (++col >= width) {
            col = 0;
            row++;
        }
    }
    return !b->eos;
}

/* a sub-image (an entropy image, a transform's data): its cache and codes, then its
   pixels into out */
static int wl_sub(const uint8_t *q, wl_br *b, int xsize, int ysize, vector<uint32_t> *out) {
    int cache_bits = 0, meta_bits = 0;
    if (wl_bits(q, b, 1)) {
        cache_bits = (int)wl_bits(q, b, 4);
        if (cache_bits < 1 || cache_bits > 11) return 0;
    }
    vector<uint32_t> tabs, meta;
    vector<wl_group> groups;
    if (!wl_codes(q, b, xsize, ysize, cache_bits, 0, &tabs, &groups, &meta, &meta_bits)) return 0;
    clear(*out);
    resize(out, (size_t)xsize * (size_t)ysize);
    return wl_data(q, b, data(*out), xsize, ysize, cache_bits, &tabs, &groups, &meta, 0);
}

/* ---- VP8L: the transforms undone ---- */

static uint32_t wl_add(uint32_t a, uint32_t b) {
    uint32_t ag = (a & 0xff00ff00u) + (b & 0xff00ff00u), rb = (a & 0x00ff00ffu) + (b & 0x00ff00ffu);
    return (ag & 0xff00ff00u) | (rb & 0x00ff00ffu);
}
static uint32_t wl_avg2(uint32_t a, uint32_t b) { return (((a ^ b) & 0xfefefefeu) >> 1) + (a & b); }
static uint32_t wl_clip255(uint32_t a) { return a < 256 ? a : ~a >> 24; }
static int wl_sub3(int a, int b, int c) { return abs(b - c) - abs(a - c); }

static uint32_t wl_select(uint32_t a, uint32_t b, uint32_t c) {
    int d = wl_sub3((int)(a >> 24), (int)(b >> 24), (int)(c >> 24)) + wl_sub3((int)(a >> 16 & 0xff), (int)(b >> 16 & 0xff), (int)(c >> 16 & 0xff)) +
            wl_sub3((int)(a >> 8 & 0xff), (int)(b >> 8 & 0xff), (int)(c >> 8 & 0xff)) + wl_sub3((int)(a & 0xff), (int)(b & 0xff), (int)(c & 0xff));
    return d <= 0 ? a : b;
}

static uint32_t wl_full(uint32_t c0, uint32_t c1, uint32_t c2) {
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) out |= wl_clip255((uint32_t)((int)(c0 >> s & 0xff) + (int)(c1 >> s & 0xff) - (int)(c2 >> s & 0xff))) << s;
    return out;
}

static uint32_t wl_half(uint32_t c0, uint32_t c1, uint32_t c2) {
    uint32_t ave = wl_avg2(c0, c1), out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (int)(ave >> s & 0xff), b = (int)(c2 >> s & 0xff);
        out |= wl_clip255((uint32_t)(a + (a - b) / 2)) << s;
    }
    return out;
}

/* the predictor of mode m from the left pixel and those above (top[-1], top[0], top[1]) */
static uint32_t wl_predict(int m, uint32_t left, const uint32_t *top) {
    switch (m) {
    case 1: return left;
    case 2: return top[0];
    case 3: return top[1];
    case 4: return top[-1];
    case 5: return wl_avg2(wl_avg2(left, top[1]), top[0]);
    case 6: return wl_avg2(left, top[-1]);
    case 7: return wl_avg2(left, top[0]);
    case 8: return wl_avg2(top[-1], top[0]);
    case 9: return wl_avg2(top[0], top[1]);
    case 10: return wl_avg2(wl_avg2(left, top[-1]), wl_avg2(top[0], top[1]));
    case 11: return wl_select(top[0], left, top[-1]);
    case 12: return wl_full(left, top[0], top[-1]);
    case 13: return wl_half(left, top[0], top[-1]);
    default: return 0xff000000u;
    }
}

/* the predictor transform undone in place: the first row from the left (its first pixel
   from black), the first column from above, the rest by each block's mode; the pixel
   above and to the right of a row's last is the row's first, as the stream has it */
static void wl_predictor(uint32_t *px, int width, int height, int bits, const uint32_t *modes) {
    int tiles = wl_sub_size(width, bits);
    px[0] = wl_add(px[0], 0xff000000u);
    for (int x = 1; x < width; x++) px[x] = wl_add(px[x], px[x - 1]);
    for (int y = 1; y < height; y++) {
        uint32_t *row = px + (size_t)y * (size_t)width;
        const uint32_t *m = modes + (size_t)(y >> bits) * (size_t)tiles;
        row[0] = wl_add(row[0], row[-width]);
        for (int x = 1; x < width; x++) row[x] = wl_add(row[x], wl_predict((int)(m[x >> bits] >> 8 & 0xf), row[x - 1], row + x - width));
    }
}

/* the cross-colour transform undone: red from green, blue from green and red, by each
   block's multipliers */
static void wl_cross(uint32_t *px, int width, int height, int bits, const uint32_t *codes) {
    int tiles = wl_sub_size(width, bits);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            uint32_t code = codes[(size_t)(y >> bits) * (size_t)tiles + (size_t)(x >> bits)];
            int g2r = (int8_t)(uint8_t)code, g2b = (int8_t)(uint8_t)(code >> 8), r2b = (int8_t)(uint8_t)(code >> 16);
            size_t i = (size_t)y * (size_t)width + (size_t)x;
            uint32_t argb = px[i];
            int green = (int8_t)(uint8_t)(argb >> 8);
            int red = (int)(argb >> 16 & 0xff), blue = (int)(argb & 0xff);
            red = (red + ((g2r * green) >> 5)) & 0xff;
            blue = (blue + ((g2b * green) >> 5) + ((r2b * (int8_t)(uint8_t)red) >> 5)) & 0xff;
            px[i] = (argb & 0xff00ff00u) | (uint32_t)red << 16 | (uint32_t)blue;
        }
}

/* the colour-indexing transform undone: each index (packed 2, 4 or 8 to a pixel when the
   palette is small) its colour, into out of the full width */
static void wl_unindex(const uint32_t *in, uint32_t *out, int width, int height, int bits, const uint32_t *map) {
    if (bits == 0) {
        for (size_t i = 0; i < (size_t)width * (size_t)height; i++) out[i] = map[in[i] >> 8 & 0xff];
        return;
    }
    int in_w = wl_sub_size(width, bits), bpp = 8 >> bits, cmask = (1 << bits) - 1;
    uint32_t bmask = (1u << bpp) - 1;
    for (int y = 0; y < height; y++) {
        const uint32_t *src = in + (size_t)y * (size_t)in_w;
        uint32_t *dst = out + (size_t)y * (size_t)width;
        uint32_t packed = 0;
        int k = 0;
        for (int x = 0; x < width; x++) {
            if ((x & cmask) == 0) packed = src[k++] >> 8 & 0xff;
            dst[x] = map[packed & bmask];
            packed >>= bpp;
        }
    }
}

/* a transform read: its type, the width it was read at, its block bits, where its data
   lies in the transforms' data */
typedef struct wl_xform { int32_t type, bits, xsize; size_t off; } wl_xform;

/* an image stream (a VP8L image's, or an ALPH chunk's): its transforms (each type once),
   cache and codes, pixels, and the transforms undone, into argb, width x height */
static int wl_image(const uint8_t *q, wl_br *b, int width, int height, vector<uint32_t> *argb) {
    wl_xform xf[4];
    memset(xf, 0, sizeof xf);
    int nx = 0, seen = 0, xsize = width;
    vector<uint32_t> tdata, sub;
    while (wl_bits(q, b, 1)) {
        if (b->eos) return 0;
        int type = (int)wl_bits(q, b, 2);
        if (seen & (1 << type)) return 0;
        seen |= 1 << type;
        xf[nx].type = type;
        xf[nx].xsize = xsize;
        xf[nx].off = len(tdata);
        if (type == 0 || type == 1) {
            int bits = (int)wl_bits(q, b, 3) + 2;
            if (!wl_sub(q, b, wl_sub_size(xsize, bits), wl_sub_size(height, bits), &sub)) return 0;
            xf[nx].bits = bits;
            append_range(tdata, data(sub), len(sub));
        } else if (type == 3) {
            int num = (int)wl_bits(q, b, 8) + 1;
            int bits = num > 16 ? 0 : num > 4 ? 1 : num > 2 ? 2 : 3;
            if (!wl_sub(q, b, num, 1, &sub)) return 0;
            /* the palette, coded as differences, summed; the indices past it black */
            size_t base = len(tdata);
            resize(&tdata, base + ((size_t)1 << (8 >> bits)));
            tdata[base] = at(sub, 0);
            for (int k = 1; k < num; k++) tdata[base + (size_t)k] = wl_add(at(sub, (size_t)k), at(tdata, base + (size_t)k - 1));
            xf[nx].bits = bits;
            xsize = wl_sub_size(xsize, bits);
        }
        nx++;
    }
    int cache_bits = 0, meta_bits = 0;
    if (wl_bits(q, b, 1)) {
        cache_bits = (int)wl_bits(q, b, 4);
        if (cache_bits < 1 || cache_bits > 11) return 0;
    }
    vector<uint32_t> tabs, meta;
    vector<wl_group> groups;
    if (!wl_codes(q, b, xsize, height, cache_bits, 1, &tabs, &groups, &meta, &meta_bits)) return 0;
    clear(*argb);
    resize(argb, (size_t)xsize * (size_t)height);
    if (!wl_data(q, b, data(*argb), xsize, height, cache_bits, &tabs, &groups, &meta, meta_bits)) return 0;
    for (int k = nx - 1; k >= 0; k--) {
        const uint32_t *td = data(tdata) + xf[k].off;
        int w = xf[k].xsize;
        if (xf[k].type == 0) wl_predictor(data(*argb), w, height, xf[k].bits, td);
        else if (xf[k].type == 1) wl_cross(data(*argb), w, height, xf[k].bits, td);
        else if (xf[k].type == 2) {
            uint32_t *px = data(*argb);
            for (size_t i = 0; i < len(*argb); i++) {
                uint32_t g = px[i] >> 8 & 0xff, rb = (px[i] & 0x00ff00ffu) + (g << 16 | g);
                px[i] = (px[i] & 0xff00ff00u) | (rb & 0x00ff00ffu);
            }
        } else {
            vector<uint32_t> full;
            resize(&full, (size_t)w * (size_t)height);
            wl_unindex(data(*argb), data(full), w, height, xf[k].bits, data(tdata) + xf[k].off);
            clear(*argb);
            append_range(argb, data(full), len(full));
        }
    }
    return 1;
}

/* a VP8L stream's header: its signature, size, alpha hint and version 0 */
static int wl_header(const uint8_t *q, size_t n, uint32_t *w, uint32_t *h) {
    if (n < 5 || q[0] != 0x2f || (q[4] >> 5) != 0) return 0;
    uint32_t v = wp_le32(q + 1);
    *w = (v & 0x3fff) + 1;
    *h = (v >> 14 & 0x3fff) + 1;
    return 1;
}

/* a VP8L stream q[0, n) decoded into RGBA8 rows; 0 when it is malformed or cut short */
static int wl_decode(const uint8_t *q, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    if (!wl_header(q, n, w, h) || (uint64_t)*w * *h > AMB_IMAGE_MAX_PIXELS) return 0;
    wl_br b;
    wl_begin(&b, n);
    wl_bits(q, &b, 8);
    wl_bits(q, &b, 16);
    wl_bits(q, &b, 16);
    vector<uint32_t> argb;
    if (!wl_image(q, &b, (int)*w, (int)*h, &argb)) return 0;
    clear(*rgba);
    resize(rgba, len(argb) * 4);
    uint8_t *o = data(*rgba);
    for (size_t i = 0; i < len(argb); i++) {
        uint32_t c = at(argb, i);
        o[4 * i] = (uint8_t)(c >> 16);
        o[4 * i + 1] = (uint8_t)(c >> 8);
        o[4 * i + 2] = (uint8_t)c;
        o[4 * i + 3] = (uint8_t)(c >> 24);
    }
    return 1;
}

/* ---- VP8: the boolean decoder ---- */

/* a partition p[pos, end): value holds the bits read, the window's 8 at bit bits; range
   is the interval's size less one. Past the end it reads one zero byte and sets eof, a
   stream's error once its macroblock or row is done (libwebp's VP8BitReader) */
typedef struct v8_br { size_t pos, end; uint32_t value; int32_t range, bits, eof; } v8_br;

static void v8_load(const uint8_t *p, v8_br *b) {
    if (b->pos < b->end) {
        b->bits += 8;
        b->value = b->value << 8 | p[b->pos++];
    } else if (!b->eof) {
        b->value <<= 8;
        b->bits += 8;
        b->eof = 1;
    } else b->bits = 0;
}

static void v8_begin(const uint8_t *p, v8_br *b, size_t start, size_t size) {
    b->pos = start;
    b->end = start + size;
    b->value = 0;
    b->range = 254;
    b->bits = -8;
    b->eof = 0;
    v8_load(p, b);
}

static int v8_bit(const uint8_t *p, v8_br *b, int prob) {
    uint32_t range = (uint32_t)b->range;
    if (b->bits < 0) v8_load(p, b);
    int pos = b->bits;
    uint32_t split = (range * (uint32_t)prob) >> 8, value = b->value >> pos;
    int bit = value > split;
    if (bit) {
        range -= split;
        b->value -= (split + 1) << pos;
    } else range = split + 1;
    int shift = 0;
    while ((range << shift) < 128) shift++;
    b->bits -= shift;
    b->range = (int32_t)(range << shift) - 1;
    return bit;
}

static uint32_t v8_value(const uint8_t *p, v8_br *b, int bits) {
    uint32_t v = 0;
    while (bits-- > 0) v |= (uint32_t)v8_bit(p, b, 0x80) << bits;
    return v;
}

static int32_t v8_signed(const uint8_t *p, v8_br *b, int bits) {
    int32_t v = (int32_t)v8_value(p, b, bits);
    return v8_bit(p, b, 0x80) ? -v : v;
}

/* the coefficients' probabilities before a frame's updates (RFC 6386 13.5), by plane
   type, band and context; the probabilities that a frame updates each (13.4); the
   4x4 modes' probabilities in a key frame by the modes above and to the left (11.5);
   the quantizers' steps by index (14.1) */
static const uint8_t v8_proba0[4][8][3][11] = { { { { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 } },
        { { 253, 136, 254, 255, 228, 219, 128, 128, 128, 128, 128 },
            { 189, 129, 242, 255, 227, 213, 255, 219, 128, 128, 128 },
            { 106, 126, 227, 252, 214, 209, 255, 255, 128, 128, 128 } },
        { { 1, 98, 248, 255, 236, 226, 255, 255, 128, 128, 128 },
            { 181, 133, 238, 254, 221, 234, 255, 154, 128, 128, 128 },
            { 78, 134, 202, 247, 198, 180, 255, 219, 128, 128, 128 } },
        { { 1, 185, 249, 255, 243, 255, 128, 128, 128, 128, 128 },
            { 184, 150, 247, 255, 236, 224, 128, 128, 128, 128, 128 },
            { 77, 110, 216, 255, 236, 230, 128, 128, 128, 128, 128 } },
        { { 1, 101, 251, 255, 241, 255, 128, 128, 128, 128, 128 },
            { 170, 139, 241, 252, 236, 209, 255, 255, 128, 128, 128 },
            { 37, 116, 196, 243, 228, 255, 255, 255, 128, 128, 128 } },
        { { 1, 204, 254, 255, 245, 255, 128, 128, 128, 128, 128 },
            { 207, 160, 250, 255, 238, 128, 128, 128, 128, 128, 128 },
            { 102, 103, 231, 255, 211, 171, 128, 128, 128, 128, 128 } },
        { { 1, 152, 252, 255, 240, 255, 128, 128, 128, 128, 128 },
            { 177, 135, 243, 255, 234, 225, 128, 128, 128, 128, 128 },
            { 80, 129, 211, 255, 194, 224, 128, 128, 128, 128, 128 } },
        { { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 246, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 255, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 } } },
    { { { 198, 35, 237, 223, 193, 187, 162, 160, 145, 155, 62 },
            { 131, 45, 198, 221, 172, 176, 220, 157, 252, 221, 1 },
            { 68, 47, 146, 208, 149, 167, 221, 162, 255, 223, 128 } },
        { { 1, 149, 241, 255, 221, 224, 255, 255, 128, 128, 128 },
            { 184, 141, 234, 253, 222, 220, 255, 199, 128, 128, 128 },
            { 81, 99, 181, 242, 176, 190, 249, 202, 255, 255, 128 } },
        { { 1, 129, 232, 253, 214, 197, 242, 196, 255, 255, 128 },
            { 99, 121, 210, 250, 201, 198, 255, 202, 128, 128, 128 },
            { 23, 91, 163, 242, 170, 187, 247, 210, 255, 255, 128 } },
        { { 1, 200, 246, 255, 234, 255, 128, 128, 128, 128, 128 },
            { 109, 178, 241, 255, 231, 245, 255, 255, 128, 128, 128 },
            { 44, 130, 201, 253, 205, 192, 255, 255, 128, 128, 128 } },
        { { 1, 132, 239, 251, 219, 209, 255, 165, 128, 128, 128 },
            { 94, 136, 225, 251, 218, 190, 255, 255, 128, 128, 128 },
            { 22, 100, 174, 245, 186, 161, 255, 199, 128, 128, 128 } },
        { { 1, 182, 249, 255, 232, 235, 128, 128, 128, 128, 128 },
            { 124, 143, 241, 255, 227, 234, 128, 128, 128, 128, 128 },
            { 35, 77, 181, 251, 193, 211, 255, 205, 128, 128, 128 } },
        { { 1, 157, 247, 255, 236, 231, 255, 255, 128, 128, 128 },
            { 121, 141, 235, 255, 225, 227, 255, 255, 128, 128, 128 },
            { 45, 99, 188, 251, 195, 217, 255, 224, 128, 128, 128 } },
        { { 1, 1, 251, 255, 213, 255, 128, 128, 128, 128, 128 },
            { 203, 1, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
            { 137, 1, 177, 255, 224, 255, 128, 128, 128, 128, 128 } } },
    { { { 253, 9, 248, 251, 207, 208, 255, 192, 128, 128, 128 },
            { 175, 13, 224, 243, 193, 185, 249, 198, 255, 255, 128 },
            { 73, 17, 171, 221, 161, 179, 236, 167, 255, 234, 128 } },
        { { 1, 95, 247, 253, 212, 183, 255, 255, 128, 128, 128 },
            { 239, 90, 244, 250, 211, 209, 255, 255, 128, 128, 128 },
            { 155, 77, 195, 248, 188, 195, 255, 255, 128, 128, 128 } },
        { { 1, 24, 239, 251, 218, 219, 255, 205, 128, 128, 128 },
            { 201, 51, 219, 255, 196, 186, 128, 128, 128, 128, 128 },
            { 69, 46, 190, 239, 201, 218, 255, 228, 128, 128, 128 } },
        { { 1, 191, 251, 255, 255, 128, 128, 128, 128, 128, 128 },
            { 223, 165, 249, 255, 213, 255, 128, 128, 128, 128, 128 },
            { 141, 124, 248, 255, 255, 128, 128, 128, 128, 128, 128 } },
        { { 1, 16, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
            { 190, 36, 230, 255, 236, 255, 128, 128, 128, 128, 128 },
            { 149, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 } },
        { { 1, 226, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 247, 192, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 240, 128, 255, 128, 128, 128, 128, 128, 128, 128, 128 } },
        { { 1, 134, 252, 255, 255, 128, 128, 128, 128, 128, 128 },
            { 213, 62, 250, 255, 255, 128, 128, 128, 128, 128, 128 },
            { 55, 93, 255, 128, 128, 128, 128, 128, 128, 128, 128 } },
        { { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 } } },
    { { { 202, 24, 213, 235, 186, 191, 220, 160, 240, 175, 255 },
            { 126, 38, 182, 232, 169, 184, 228, 174, 255, 187, 128 },
            { 61, 46, 138, 219, 151, 178, 240, 170, 255, 216, 128 } },
        { { 1, 112, 230, 250, 199, 191, 247, 159, 255, 255, 128 },
            { 166, 109, 228, 252, 211, 215, 255, 174, 128, 128, 128 },
            { 39, 77, 162, 232, 172, 180, 245, 178, 255, 255, 128 } },
        { { 1, 52, 220, 246, 198, 199, 249, 220, 255, 255, 128 },
            { 124, 74, 191, 243, 183, 193, 250, 221, 255, 255, 128 },
            { 24, 71, 130, 219, 154, 170, 243, 182, 255, 255, 128 } },
        { { 1, 182, 225, 249, 219, 240, 255, 224, 128, 128, 128 },
            { 149, 150, 226, 252, 216, 205, 255, 171, 128, 128, 128 },
            { 28, 108, 170, 242, 183, 194, 254, 223, 255, 255, 128 } },
        { { 1, 81, 230, 252, 204, 203, 255, 192, 128, 128, 128 },
            { 123, 102, 209, 247, 188, 196, 255, 233, 128, 128, 128 },
            { 20, 95, 153, 243, 164, 173, 255, 203, 128, 128, 128 } },
        { { 1, 222, 248, 255, 216, 213, 128, 128, 128, 128, 128 },
            { 168, 175, 246, 252, 235, 205, 255, 255, 128, 128, 128 },
            { 47, 116, 215, 255, 211, 212, 255, 255, 128, 128, 128 } },
        { { 1, 121, 236, 253, 212, 214, 255, 255, 128, 128, 128 },
            { 141, 84, 213, 252, 201, 202, 255, 219, 128, 128, 128 },
            { 42, 80, 160, 240, 162, 185, 255, 205, 128, 128, 128 } },
        { { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 244, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
            { 238, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 } } } };
static const uint8_t v8_update[4][8][3][11] = { { { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 176, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 223, 241, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 249, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 244, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 234, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 246, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 239, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 251, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 251, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 253, 255, 254, 255, 255, 255, 255, 255, 255 },
            { 250, 255, 254, 255, 254, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
    { { { 217, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 225, 252, 241, 253, 255, 255, 254, 255, 255, 255, 255 },
            { 234, 250, 241, 250, 253, 255, 253, 254, 255, 255, 255 } },
        { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 223, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 238, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 249, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 247, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
    { { { 186, 251, 250, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 234, 251, 244, 254, 255, 255, 255, 255, 255, 255, 255 },
            { 251, 251, 243, 253, 254, 255, 254, 255, 255, 255, 255 } },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 236, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 251, 253, 253, 254, 254, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
    { { { 248, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 250, 254, 252, 254, 255, 255, 255, 255, 255, 255, 255 },
            { 248, 254, 249, 253, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 246, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 252, 254, 251, 254, 254, 255, 255, 255, 255, 255, 255 } },
        { { 255, 254, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 248, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 253, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 245, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 253, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 251, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 252, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 252, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 249, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } } };
static const uint8_t v8_bmodes[10][10][9] = { { { 231, 120, 48, 89, 115, 113, 120, 152, 112 },
        { 152, 179, 64, 126, 170, 118, 46, 70, 95 },
        { 175, 69, 143, 80, 85, 82, 72, 155, 103 },
        { 56, 58, 10, 171, 218, 189, 17, 13, 152 },
        { 114, 26, 17, 163, 44, 195, 21, 10, 173 },
        { 121, 24, 80, 195, 26, 62, 44, 64, 85 },
        { 144, 71, 10, 38, 171, 213, 144, 34, 26 },
        { 170, 46, 55, 19, 136, 160, 33, 206, 71 },
        { 63, 20, 8, 114, 114, 208, 12, 9, 226 },
        { 81, 40, 11, 96, 182, 84, 29, 16, 36 } },
    { { 134, 183, 89, 137, 98, 101, 106, 165, 148 },
        { 72, 187, 100, 130, 157, 111, 32, 75, 80 },
        { 66, 102, 167, 99, 74, 62, 40, 234, 128 },
        { 41, 53, 9, 178, 241, 141, 26, 8, 107 },
        { 74, 43, 26, 146, 73, 166, 49, 23, 157 },
        { 65, 38, 105, 160, 51, 52, 31, 115, 128 },
        { 104, 79, 12, 27, 217, 255, 87, 17, 7 },
        { 87, 68, 71, 44, 114, 51, 15, 186, 23 },
        { 47, 41, 14, 110, 182, 183, 21, 17, 194 },
        { 66, 45, 25, 102, 197, 189, 23, 18, 22 } },
    { { 88, 88, 147, 150, 42, 46, 45, 196, 205 },
        { 43, 97, 183, 117, 85, 38, 35, 179, 61 },
        { 39, 53, 200, 87, 26, 21, 43, 232, 171 },
        { 56, 34, 51, 104, 114, 102, 29, 93, 77 },
        { 39, 28, 85, 171, 58, 165, 90, 98, 64 },
        { 34, 22, 116, 206, 23, 34, 43, 166, 73 },
        { 107, 54, 32, 26, 51, 1, 81, 43, 31 },
        { 68, 25, 106, 22, 64, 171, 36, 225, 114 },
        { 34, 19, 21, 102, 132, 188, 16, 76, 124 },
        { 62, 18, 78, 95, 85, 57, 50, 48, 51 } },
    { { 193, 101, 35, 159, 215, 111, 89, 46, 111 },
        { 60, 148, 31, 172, 219, 228, 21, 18, 111 },
        { 112, 113, 77, 85, 179, 255, 38, 120, 114 },
        { 40, 42, 1, 196, 245, 209, 10, 25, 109 },
        { 88, 43, 29, 140, 166, 213, 37, 43, 154 },
        { 61, 63, 30, 155, 67, 45, 68, 1, 209 },
        { 100, 80, 8, 43, 154, 1, 51, 26, 71 },
        { 142, 78, 78, 16, 255, 128, 34, 197, 171 },
        { 41, 40, 5, 102, 211, 183, 4, 1, 221 },
        { 51, 50, 17, 168, 209, 192, 23, 25, 82 } },
    { { 138, 31, 36, 171, 27, 166, 38, 44, 229 },
        { 67, 87, 58, 169, 82, 115, 26, 59, 179 },
        { 63, 59, 90, 180, 59, 166, 93, 73, 154 },
        { 40, 40, 21, 116, 143, 209, 34, 39, 175 },
        { 47, 15, 16, 183, 34, 223, 49, 45, 183 },
        { 46, 17, 33, 183, 6, 98, 15, 32, 183 },
        { 57, 46, 22, 24, 128, 1, 54, 17, 37 },
        { 65, 32, 73, 115, 28, 128, 23, 128, 205 },
        { 40, 3, 9, 115, 51, 192, 18, 6, 223 },
        { 87, 37, 9, 115, 59, 77, 64, 21, 47 } },
    { { 104, 55, 44, 218, 9, 54, 53, 130, 226 },
        { 64, 90, 70, 205, 40, 41, 23, 26, 57 },
        { 54, 57, 112, 184, 5, 41, 38, 166, 213 },
        { 30, 34, 26, 133, 152, 116, 10, 32, 134 },
        { 39, 19, 53, 221, 26, 114, 32, 73, 255 },
        { 31, 9, 65, 234, 2, 15, 1, 118, 73 },
        { 75, 32, 12, 51, 192, 255, 160, 43, 51 },
        { 88, 31, 35, 67, 102, 85, 55, 186, 85 },
        { 56, 21, 23, 111, 59, 205, 45, 37, 192 },
        { 55, 38, 70, 124, 73, 102, 1, 34, 98 } },
    { { 125, 98, 42, 88, 104, 85, 117, 175, 82 },
        { 95, 84, 53, 89, 128, 100, 113, 101, 45 },
        { 75, 79, 123, 47, 51, 128, 81, 171, 1 },
        { 57, 17, 5, 71, 102, 57, 53, 41, 49 },
        { 38, 33, 13, 121, 57, 73, 26, 1, 85 },
        { 41, 10, 67, 138, 77, 110, 90, 47, 114 },
        { 115, 21, 2, 10, 102, 255, 166, 23, 6 },
        { 101, 29, 16, 10, 85, 128, 101, 196, 26 },
        { 57, 18, 10, 102, 102, 213, 34, 20, 43 },
        { 117, 20, 15, 36, 163, 128, 68, 1, 26 } },
    { { 102, 61, 71, 37, 34, 53, 31, 243, 192 },
        { 69, 60, 71, 38, 73, 119, 28, 222, 37 },
        { 68, 45, 128, 34, 1, 47, 11, 245, 171 },
        { 62, 17, 19, 70, 146, 85, 55, 62, 70 },
        { 37, 43, 37, 154, 100, 163, 85, 160, 1 },
        { 63, 9, 92, 136, 28, 64, 32, 201, 85 },
        { 75, 15, 9, 9, 64, 255, 184, 119, 16 },
        { 86, 6, 28, 5, 64, 255, 25, 248, 1 },
        { 56, 8, 17, 132, 137, 255, 55, 116, 128 },
        { 58, 15, 20, 82, 135, 57, 26, 121, 40 } },
    { { 164, 50, 31, 137, 154, 133, 25, 35, 218 },
        { 51, 103, 44, 131, 131, 123, 31, 6, 158 },
        { 86, 40, 64, 135, 148, 224, 45, 183, 128 },
        { 22, 26, 17, 131, 240, 154, 14, 1, 209 },
        { 45, 16, 21, 91, 64, 222, 7, 1, 197 },
        { 56, 21, 39, 155, 60, 138, 23, 102, 213 },
        { 83, 12, 13, 54, 192, 255, 68, 47, 28 },
        { 85, 26, 85, 85, 128, 128, 32, 146, 171 },
        { 18, 11, 7, 63, 144, 171, 4, 4, 246 },
        { 35, 27, 10, 146, 174, 171, 12, 26, 128 } },
    { { 190, 80, 35, 99, 180, 80, 126, 54, 45 },
        { 85, 126, 47, 87, 176, 51, 41, 20, 32 },
        { 101, 75, 128, 139, 118, 146, 116, 128, 85 },
        { 56, 41, 15, 176, 236, 85, 37, 9, 62 },
        { 71, 30, 17, 119, 118, 255, 17, 18, 138 },
        { 101, 38, 60, 138, 55, 70, 43, 26, 142 },
        { 146, 36, 19, 30, 171, 255, 97, 27, 20 },
        { 138, 45, 61, 62, 219, 1, 81, 188, 64 },
        { 32, 41, 20, 117, 151, 142, 20, 21, 163 },
        { 112, 19, 12, 61, 195, 128, 48, 4, 24 } } };
static const uint8_t v8_dc_table[128] = {
    4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 17,
    18, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 25, 25, 26, 27, 28,
    29, 30, 31, 32, 33, 34, 35, 36, 37, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
    59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
    75, 76, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    91, 93, 95, 96, 98, 100, 101, 102, 104, 106, 108, 110, 112, 114, 116, 118,
    122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 143, 145, 148, 151, 154, 157 };
static const uint16_t v8_ac_table[128] = {
    4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    52, 53, 54, 55, 56, 57, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76,
    78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108,
    110, 112, 114, 116, 119, 122, 125, 128, 131, 134, 137, 140, 143, 146, 149, 152,
    155, 158, 161, 164, 167, 170, 173, 177, 181, 185, 189, 193, 197, 201, 205, 209,
    213, 217, 221, 225, 229, 234, 239, 245, 249, 254, 259, 264, 269, 274, 279, 284 };

/* the 4x4 modes' tree: an entry above 0 the next node, else a mode, negated (DC, TM, VE,
   HE, RD, VR, LD, VL, HD, HU) */
static const int8_t v8_bmodes_tree[18] = { 0, 1, -1, 2, -2, 3, 4, 6, -3, 5, -4, -5, -6, 7, -7, 8, -8, -9 };

static const uint8_t v8_zigzag[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };
static const uint8_t v8_bands[17] = { 0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0 };
static const uint8_t v8_cat[4][12] = { { 173, 148, 140, 0 }, { 176, 155, 140, 135, 0 }, { 180, 157, 141, 134, 130, 0 },
                                       { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 } };

/* a key frame's state: its size and macroblocks, its segments, loop filter and token
   partitions, its quantizers by segment, the coefficients' probabilities, and the loop
   filter's limit, interior limit and high-edge-variance threshold by segment and by
   whether a macroblock is 4x4 */
typedef struct v8 {
    uint32_t w, h, mb_w, mb_h;
    int32_t use_segment, update_map, absolute, quantizer[4], strength[4], seg_proba[3];
    int32_t simple, level, sharpness, use_delta, ref_delta[4], mode_delta[4], filter_type, parts;
    int32_t y1_dc[4], y1_ac[4], y2_dc[4], y2_ac[4], uv_dc[4], uv_ac[4];
    int32_t use_skip, skip_p;
    int32_t f_limit[4][2], f_ilevel[4][2], f_hev[4][2];
    uint8_t proba[4][8][3][11];
    v8_br br;
    v8_br part[8];
} v8;

static int v8_clip(int v, int m) { return v < 0 ? 0 : v > m ? m : v; }

/* the frame header and the first partition's global part (RFC 6386 9.2 to 9.11, 13.4),
   the token partitions found, the loop filter's strengths worked out (15.1); 0 when it
   is no key frame libwebp decodes */
static int v8_header(const uint8_t *p, size_t n, v8 *d) {
    memset(d, 0, sizeof *d);
    if (n < 10) return 0;
    uint32_t tag = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16, part0 = tag >> 5;
    if ((tag & 1) || ((tag >> 1) & 7) > 3 || !((tag >> 4) & 1)) return 0;
    if (p[3] != 0x9d || p[4] != 0x01 || p[5] != 0x2a) return 0;
    d->w = wp_le16(p + 6) & 0x3fff;
    d->h = wp_le16(p + 8) & 0x3fff;
    if (!d->w || !d->h || part0 > n - 10) return 0;
    d->mb_w = (d->w + 15) >> 4;
    d->mb_h = (d->h + 15) >> 4;
    v8_begin(p, &d->br, 10, part0);
    v8_value(p, &d->br, 2);   /* the colour space and the clamping type, which libwebp ignores */
    d->absolute = 1;
    d->seg_proba[0] = d->seg_proba[1] = d->seg_proba[2] = 255;
    d->use_segment = (int32_t)v8_value(p, &d->br, 1);
    if (d->use_segment) {
        d->update_map = (int32_t)v8_value(p, &d->br, 1);
        if (v8_value(p, &d->br, 1)) {
            d->absolute = (int32_t)v8_value(p, &d->br, 1);
            for (int s = 0; s < 4; s++) d->quantizer[s] = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 7) : 0;
            for (int s = 0; s < 4; s++) d->strength[s] = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 6) : 0;
        }
        if (d->update_map)
            for (int s = 0; s < 3; s++) d->seg_proba[s] = v8_value(p, &d->br, 1) ? (int32_t)v8_value(p, &d->br, 8) : 255;
    }
    if (d->br.eof) return 0;
    d->simple = (int32_t)v8_value(p, &d->br, 1);
    d->level = (int32_t)v8_value(p, &d->br, 6);
    d->sharpness = (int32_t)v8_value(p, &d->br, 3);
    d->use_delta = (int32_t)v8_value(p, &d->br, 1);
    if (d->use_delta && v8_value(p, &d->br, 1)) {
        for (int i = 0; i < 4; i++)
            if (v8_value(p, &d->br, 1)) d->ref_delta[i] = v8_signed(p, &d->br, 6);
        for (int i = 0; i < 4; i++)
            if (v8_value(p, &d->br, 1)) d->mode_delta[i] = v8_signed(p, &d->br, 6);
    }
    d->filter_type = d->level == 0 ? 0 : d->simple ? 1 : 2;
    if (d->br.eof) return 0;
    /* the token partitions: each one's size but the last's, 3 bytes, then each */
    d->parts = 1 << v8_value(p, &d->br, 2);
    size_t start = 10 + part0, size = n - start, last = (size_t)d->parts - 1;
    if (size < 3 * last) return 0;
    size_t pstart = start + 3 * last, left = size - 3 * last;
    for (size_t k = 0; k < last; k++) {
        size_t ps = (size_t)p[start + 3 * k] | (size_t)p[start + 3 * k + 1] << 8 | (size_t)p[start + 3 * k + 2] << 16;
        if (ps > left) ps = left;
        v8_begin(p, &d->part[k], pstart, ps);
        pstart += ps;
        left -= ps;
    }
    v8_begin(p, &d->part[last], pstart, left);
    if (pstart >= n) return 0;
    /* the quantizers by segment (9.6, 14.1) */
    int q0 = (int)v8_value(p, &d->br, 7);
    int dqy1_dc = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 4) : 0;
    int dqy2_dc = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 4) : 0;
    int dqy2_ac = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 4) : 0;
    int dquv_dc = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 4) : 0;
    int dquv_ac = v8_value(p, &d->br, 1) ? v8_signed(p, &d->br, 4) : 0;
    for (int s = 0; s < 4; s++) {
        int q = q0;
        if (d->use_segment) q = d->quantizer[s] + (d->absolute ? 0 : q0);
        d->y1_dc[s] = v8_dc_table[v8_clip(q + dqy1_dc, 127)];
        d->y1_ac[s] = v8_ac_table[v8_clip(q, 127)];
        d->y2_dc[s] = v8_dc_table[v8_clip(q + dqy2_dc, 127)] * 2;
        d->y2_ac[s] = (v8_ac_table[v8_clip(q + dqy2_ac, 127)] * 101581) >> 16;
        if (d->y2_ac[s] < 8) d->y2_ac[s] = 8;
        d->uv_dc[s] = v8_dc_table[v8_clip(q + dquv_dc, 117)];
        d->uv_ac[s] = v8_ac_table[v8_clip(q + dquv_ac, 127)];
    }
    v8_value(p, &d->br, 1);   /* whether to keep the probabilities: one key frame, so ignored */
    for (int t = 0; t < 4; t++)
        for (int b = 0; b < 8; b++)
            for (int c = 0; c < 3; c++)
                for (int k = 0; k < 11; k++)
                    d->proba[t][b][c][k] = v8_bit(p, &d->br, v8_update[t][b][c][k]) ? (uint8_t)v8_value(p, &d->br, 8) : v8_proba0[t][b][c][k];
    d->use_skip = (int32_t)v8_value(p, &d->br, 1);
    if (d->use_skip) d->skip_p = (int32_t)v8_value(p, &d->br, 8);
    for (int s = 0; s < 4; s++) {
        int base = d->use_segment ? d->strength[s] + (d->absolute ? 0 : d->level) : d->level;
        for (int i4 = 0; i4 <= 1; i4++) {
            int level = base;
            if (d->use_delta) level += d->ref_delta[0] + (i4 ? d->mode_delta[0] : 0);
            level = v8_clip(level, 63);
            if (level > 0) {
                int ilevel = level;
                if (d->sharpness > 0) {
                    ilevel >>= d->sharpness > 4 ? 2 : 1;
                    if (ilevel > 9 - d->sharpness) ilevel = 9 - d->sharpness;
                }
                if (ilevel < 1) ilevel = 1;
                d->f_ilevel[s][i4] = ilevel;
                d->f_limit[s][i4] = 2 * level + ilevel;
                d->f_hev[s][i4] = level >= 40 ? 2 : level >= 15 ? 1 : 0;
            }
        }
    }
    return 1;
}

/* ---- VP8: a macroblock's modes and coefficients ---- */

/* a macroblock's modes: its segment, whether it has no coefficients, whether it is 4x4,
   its 16x16 mode or 4x4 modes, its chroma mode */
typedef struct v8_mb { int32_t segment, skip, i4x4, uvmode; uint8_t modes[16]; } v8_mb;

/* the modes of the macroblock in column x (11.2 to 11.4): top the modes above (4 a
   macroblock column), left those to the left; a 16x16 mode stands for its 4x4 context */
static void v8_modes(const uint8_t *p, v8 *d, uint8_t *top, uint8_t *left, v8_mb *m) {
    v8_br *br = &d->br;
    m->segment = 0;
    if (d->update_map) m->segment = !v8_bit(p, br, d->seg_proba[0]) ? v8_bit(p, br, d->seg_proba[1]) : v8_bit(p, br, d->seg_proba[2]) + 2;
    m->skip = d->use_skip ? v8_bit(p, br, d->skip_p) : 0;
    m->i4x4 = !v8_bit(p, br, 145);
    if (!m->i4x4) {
        int ymode = v8_bit(p, br, 156) ? (v8_bit(p, br, 128) ? 1 : 3) : (v8_bit(p, br, 163) ? 2 : 0);
        m->modes[0] = (uint8_t)ymode;
        memset(top, ymode, 4);
        memset(left, ymode, 4);
    } else
        for (int y = 0; y < 4; y++) {
            int ymode = left[y];
            for (int x = 0; x < 4; x++) {
                int above = top[x];
                int i = v8_bmodes_tree[v8_bit(p, br, v8_bmodes[above][ymode][0])];
                while (i > 0) i = v8_bmodes_tree[2 * i + v8_bit(p, br, v8_bmodes[above][ymode][i])];
                ymode = -i;
                top[x] = (uint8_t)ymode;
            }
            memcpy(m->modes + 4 * y, top, 4);
            left[y] = (uint8_t)ymode;
        }
    m->uvmode = !v8_bit(p, br, 142) ? 0 : !v8_bit(p, br, 114) ? 2 : v8_bit(p, br, 183) ? 1 : 3;
}

static int16_t v8_i16(int v) { return (int16_t)(uint16_t)(uint32_t)v; }

/* a coefficient's magnitude past 1 (13.2): 2 to 4 by the tree, then the categories'
   extra bits */
static int v8_large(const uint8_t *p, v8_br *b, const uint8_t *pr) {
    if (!v8_bit(p, b, pr[3])) {
        if (!v8_bit(p, b, pr[4])) return 2;
        return 3 + v8_bit(p, b, pr[5]);
    }
    if (!v8_bit(p, b, pr[6])) {
        if (!v8_bit(p, b, pr[7])) return 5 + v8_bit(p, b, 159);
        int v = 7 + 2 * v8_bit(p, b, 165);
        return v + v8_bit(p, b, 145);
    }
    int bit1 = v8_bit(p, b, pr[8]);
    int bit0 = v8_bit(p, b, pr[9 + bit1]);
    int cat = 2 * bit1 + bit0, v = 0;
    for (int k = 0; v8_cat[cat][k]; k++) v += v + v8_bit(p, b, v8_cat[cat][k]);
    return v + 3 + (8 << cat);
}

/* a block's tokens from position n (13), each dequantized (dq0 the DC's step, dq1 the
   rest's) into out in natural order; the position after its last non-zero one */
static int v8_coeffs(const uint8_t *p, v8_br *b, const v8 *d, int type, int ctx, int dq0, int dq1, int n, int16_t *out) {
    const uint8_t *pr = d->proba[type][v8_bands[n]][ctx];
    for (; n < 16; n++) {
        if (!v8_bit(p, b, pr[0])) return n;
        while (!v8_bit(p, b, pr[1])) {
            if (++n == 16) return 16;
            pr = d->proba[type][v8_bands[n]][0];
        }
        int v;
        if (!v8_bit(p, b, pr[2])) {
            v = 1;
            pr = d->proba[type][v8_bands[n + 1]][1];
        } else {
            v = v8_large(p, b, pr);
            pr = d->proba[type][v8_bands[n + 1]][2];
        }
        if (v8_bit(p, b, 0x80)) v = -v;
        out[v8_zigzag[n]] = v8_i16(v * (n > 0 ? dq1 : dq0));
    }
    return 16;
}

/* the inverse Walsh-Hadamard transform of the second-order block into each luma
   block's DC (14.3) */
static void v8_wht(const int16_t *in, int16_t *out) {
    int tmp[16];
    for (int i = 0; i < 4; i++) {
        int a0 = in[i] + in[12 + i], a1 = in[4 + i] + in[8 + i], a2 = in[4 + i] - in[8 + i], a3 = in[i] - in[12 + i];
        tmp[i] = a0 + a1;
        tmp[8 + i] = a0 - a1;
        tmp[4 + i] = a3 + a2;
        tmp[12 + i] = a3 - a2;
    }
    for (int i = 0; i < 4; i++) {
        int dc = tmp[i * 4] + 3, a0 = dc + tmp[3 + i * 4], a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
        int a2 = tmp[1 + i * 4] - tmp[2 + i * 4], a3 = dc - tmp[3 + i * 4];
        out[64 * i] = v8_i16((a0 + a1) >> 3);
        out[64 * i + 16] = v8_i16((a3 + a2) >> 3);
        out[64 * i + 32] = v8_i16((a0 - a1) >> 3);
        out[64 * i + 48] = v8_i16((a3 - a2) >> 3);
    }
}

/* a block's 2-bit transform code: 3 a full transform, 2 its DC and first two AC terms,
   1 its DC alone, 0 none */
static uint32_t v8_nz_code(uint32_t codes, int nz, int dc_nz) { return codes << 2 | (uint32_t)(nz > 3 ? 3 : nz > 1 ? 2 : dc_nz); }

/* a macroblock's coefficients (libwebp's ParseResiduals) into coeffs (16 luma blocks,
   4 U, 4 V), the non-zero contexts above (*nz_top, *dc_top) and to the left brought on;
   *nz_y and *nz_uv the blocks' transform codes. 1 when every block is empty */
static int v8_residuals(const uint8_t *p, v8_br *tok, const v8 *d, const v8_mb *m, uint8_t *nz_top, uint8_t *dc_top, uint8_t *nz_left,
                        uint8_t *dc_left, int16_t *coeffs, uint32_t *nz_y, uint32_t *nz_uv) {
    int seg = m->segment, first, ac_type;
    int16_t *dst = coeffs;
    memset(coeffs, 0, 384 * sizeof(int16_t));
    if (!m->i4x4) {
        int16_t dc[16];
        memset(dc, 0, sizeof dc);
        int nz = v8_coeffs(p, tok, d, 1, *dc_top + *dc_left, d->y2_dc[seg], d->y2_ac[seg], 0, dc);
        *dc_top = *dc_left = (uint8_t)(nz > 0);
        if (nz > 1) v8_wht(dc, dst);
        else
            for (int i = 0; i < 256; i += 16) dst[i] = v8_i16((dc[0] + 3) >> 3);
        first = 1;
        ac_type = 0;
    } else {
        first = 0;
        ac_type = 3;
    }
    uint8_t tnz = (uint8_t)(*nz_top & 0x0f), lnz = (uint8_t)(*nz_left & 0x0f);
    uint32_t ny = 0, nuv = 0;
    for (int y = 0; y < 4; y++) {
        int l = lnz & 1;
        uint32_t codes = 0;
        for (int x = 0; x < 4; x++) {
            int nz = v8_coeffs(p, tok, d, ac_type, l + (tnz & 1), d->y1_dc[seg], d->y1_ac[seg], first, dst);
            l = nz > first;
            tnz = (uint8_t)(tnz >> 1 | l << 7);
            codes = v8_nz_code(codes, nz, dst[0] != 0);
            dst += 16;
        }
        tnz >>= 4;
        lnz = (uint8_t)(lnz >> 1 | l << 7);
        ny = ny << 8 | codes;
    }
    uint32_t out_t = tnz, out_l = (uint32_t)(lnz >> 4);
    for (int ch = 0; ch < 4; ch += 2) {
        uint32_t codes = 0;
        tnz = (uint8_t)(*nz_top >> (4 + ch));
        lnz = (uint8_t)(*nz_left >> (4 + ch));
        for (int y = 0; y < 2; y++) {
            int l = lnz & 1;
            for (int x = 0; x < 2; x++) {
                int nz = v8_coeffs(p, tok, d, 2, l + (tnz & 1), d->uv_dc[seg], d->uv_ac[seg], 0, dst);
                l = nz > 0;
                tnz = (uint8_t)(tnz >> 1 | l << 3);
                codes = v8_nz_code(codes, nz, dst[0] != 0);
                dst += 16;
            }
            tnz >>= 2;
            lnz = (uint8_t)(lnz >> 1 | l << 5);
        }
        nuv |= codes << (4 * ch);
        out_t |= (uint32_t)(tnz << 4) << ch;
        out_l |= (uint32_t)(lnz & 0xf0) << ch;
    }
    *nz_top = (uint8_t)out_t;
    *nz_left = (uint8_t)out_l;
    *nz_y = ny;
    *nz_uv = nuv;
    return !(ny | nuv);
}
/* ---- VP8: prediction and the inverse transforms (12, 14.4) ---- */

/* a macroblock is rebuilt in a workspace of 32-byte rows, as libwebp's: luma at row 1,
   column 8, chroma U and V below it side by side, each with the row above and the column
   to the left of it */
#define V8_BPS 32
#define V8_Y 40
#define V8_U 584
#define V8_V 600

static uint8_t v8_clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void v8_fill(uint8_t *dst, int size, int v) {
    for (int j = 0; j < size; j++) memset(dst + j * V8_BPS, v, (size_t)size);
}

/* TrueMotion: each sample its left one plus its top one less the corner */
static void v8_tm(uint8_t *dst, int size) {
    const uint8_t *top = dst - V8_BPS;
    int corner = top[-1];
    for (int y = 0; y < size; y++) {
        int l = dst[y * V8_BPS - 1];
        for (int x = 0; x < size; x++) dst[y * V8_BPS + x] = v8_clip8(top[x] + l - corner);
    }
}

/* a 16x16 luma block or an 8x8 chroma one (size), by mode: DC, TM, vertical,
   horizontal, and DC with no row above, no column to the left, or neither */
static void v8_predict(uint8_t *dst, int size, int mode) {
    int shift = size == 16 ? 4 : 3, dc = 0;
    if (mode == 1) v8_tm(dst, size);
    else if (mode == 2)
        for (int j = 0; j < size; j++) memcpy(dst + j * V8_BPS, dst - V8_BPS, (size_t)size);
    else if (mode == 3)
        for (int j = 0; j < size; j++) memset(dst + j * V8_BPS, dst[j * V8_BPS - 1], (size_t)size);
    else if (mode == 6) v8_fill(dst, size, 0x80);
    else {
        if (mode != 5)
            for (int j = 0; j < size; j++) dc += dst[j * V8_BPS - 1];
        if (mode != 4)
            for (int i = 0; i < size; i++) dc += dst[i - V8_BPS];
        if (mode == 0) v8_fill(dst, size, (dc + size) >> (shift + 1));
        else v8_fill(dst, size, (dc + (size >> 1)) >> shift);
    }
}

static int v8_avg3(int a, int b, int c) { return (a + 2 * b + c + 2) >> 2; }
static int v8_avg2(int a, int b) { return (a + b + 1) >> 1; }

static void v8_put(uint8_t *dst, int x, int y, int v) { dst[x + y * V8_BPS] = (uint8_t)v; }

/* a 4x4 luma block by its mode (12.3): DC, TM, VE, HE, RD, VR, LD, VL, HD, HU */
static void v8_predict4(uint8_t *dst, int mode) {
    const int B = V8_BPS;
    int I = dst[-1], J = dst[-1 + B], K = dst[-1 + 2 * B], L = dst[-1 + 3 * B], X = dst[-1 - B];
    int A = dst[-B], Bt = dst[1 - B], C = dst[2 - B], D = dst[3 - B], E = dst[4 - B], F = dst[5 - B], G = dst[6 - B], H = dst[7 - B];
    switch (mode) {
    case 0: {
        int dc = 4;
        for (int i = 0; i < 4; i++) dc += dst[i - B] + dst[-1 + i * B];
        for (int i = 0; i < 4; i++) memset(dst + i * B, dc >> 3, 4);
        break;
    }
    case 1: v8_tm(dst, 4); break;
    case 2: {
        int v0 = v8_avg3(X, A, Bt), v1 = v8_avg3(A, Bt, C), v2 = v8_avg3(Bt, C, D), v3 = v8_avg3(C, D, E);
        for (int i = 0; i < 4; i++) {
            v8_put(dst, 0, i, v0);
            v8_put(dst, 1, i, v1);
            v8_put(dst, 2, i, v2);
            v8_put(dst, 3, i, v3);
        }
        break;
    }
    case 3:
        memset(dst, v8_avg3(X, I, J), 4);
        memset(dst + B, v8_avg3(I, J, K), 4);
        memset(dst + 2 * B, v8_avg3(J, K, L), 4);
        memset(dst + 3 * B, v8_avg3(K, L, L), 4);
        break;
    case 4:
        v8_put(dst, 0, 3, v8_avg3(J, K, L));
        v8_put(dst, 1, 3, v8_avg3(I, J, K)); v8_put(dst, 0, 2, v8_avg3(I, J, K));
        v8_put(dst, 2, 3, v8_avg3(X, I, J)); v8_put(dst, 1, 2, v8_avg3(X, I, J)); v8_put(dst, 0, 1, v8_avg3(X, I, J));
        v8_put(dst, 3, 3, v8_avg3(A, X, I)); v8_put(dst, 2, 2, v8_avg3(A, X, I));
        v8_put(dst, 1, 1, v8_avg3(A, X, I)); v8_put(dst, 0, 0, v8_avg3(A, X, I));
        v8_put(dst, 3, 2, v8_avg3(Bt, A, X)); v8_put(dst, 2, 1, v8_avg3(Bt, A, X)); v8_put(dst, 1, 0, v8_avg3(Bt, A, X));
        v8_put(dst, 3, 1, v8_avg3(C, Bt, A)); v8_put(dst, 2, 0, v8_avg3(C, Bt, A));
        v8_put(dst, 3, 0, v8_avg3(D, C, Bt));
        break;
    case 5:
        v8_put(dst, 0, 0, v8_avg2(X, A)); v8_put(dst, 1, 2, v8_avg2(X, A));
        v8_put(dst, 1, 0, v8_avg2(A, Bt)); v8_put(dst, 2, 2, v8_avg2(A, Bt));
        v8_put(dst, 2, 0, v8_avg2(Bt, C)); v8_put(dst, 3, 2, v8_avg2(Bt, C));
        v8_put(dst, 3, 0, v8_avg2(C, D));
        v8_put(dst, 0, 3, v8_avg3(K, J, I));
        v8_put(dst, 0, 2, v8_avg3(J, I, X));
        v8_put(dst, 0, 1, v8_avg3(I, X, A)); v8_put(dst, 1, 3, v8_avg3(I, X, A));
        v8_put(dst, 1, 1, v8_avg3(X, A, Bt)); v8_put(dst, 2, 3, v8_avg3(X, A, Bt));
        v8_put(dst, 2, 1, v8_avg3(A, Bt, C)); v8_put(dst, 3, 3, v8_avg3(A, Bt, C));
        v8_put(dst, 3, 1, v8_avg3(Bt, C, D));
        break;
    case 6:
        v8_put(dst, 0, 0, v8_avg3(A, Bt, C));
        v8_put(dst, 1, 0, v8_avg3(Bt, C, D)); v8_put(dst, 0, 1, v8_avg3(Bt, C, D));
        v8_put(dst, 2, 0, v8_avg3(C, D, E)); v8_put(dst, 1, 1, v8_avg3(C, D, E)); v8_put(dst, 0, 2, v8_avg3(C, D, E));
        v8_put(dst, 3, 0, v8_avg3(D, E, F)); v8_put(dst, 2, 1, v8_avg3(D, E, F));
        v8_put(dst, 1, 2, v8_avg3(D, E, F)); v8_put(dst, 0, 3, v8_avg3(D, E, F));
        v8_put(dst, 3, 1, v8_avg3(E, F, G)); v8_put(dst, 2, 2, v8_avg3(E, F, G)); v8_put(dst, 1, 3, v8_avg3(E, F, G));
        v8_put(dst, 3, 2, v8_avg3(F, G, H)); v8_put(dst, 2, 3, v8_avg3(F, G, H));
        v8_put(dst, 3, 3, v8_avg3(G, H, H));
        break;
    case 7:
        v8_put(dst, 0, 0, v8_avg2(A, Bt));
        v8_put(dst, 1, 0, v8_avg2(Bt, C)); v8_put(dst, 0, 2, v8_avg2(Bt, C));
        v8_put(dst, 2, 0, v8_avg2(C, D)); v8_put(dst, 1, 2, v8_avg2(C, D));
        v8_put(dst, 3, 0, v8_avg2(D, E)); v8_put(dst, 2, 2, v8_avg2(D, E));
        v8_put(dst, 0, 1, v8_avg3(A, Bt, C));
        v8_put(dst, 1, 1, v8_avg3(Bt, C, D)); v8_put(dst, 0, 3, v8_avg3(Bt, C, D));
        v8_put(dst, 2, 1, v8_avg3(C, D, E)); v8_put(dst, 1, 3, v8_avg3(C, D, E));
        v8_put(dst, 3, 1, v8_avg3(D, E, F)); v8_put(dst, 2, 3, v8_avg3(D, E, F));
        v8_put(dst, 3, 2, v8_avg3(E, F, G));
        v8_put(dst, 3, 3, v8_avg3(F, G, H));
        break;
    case 8:
        v8_put(dst, 0, 0, v8_avg2(I, X)); v8_put(dst, 2, 1, v8_avg2(I, X));
        v8_put(dst, 0, 1, v8_avg2(J, I)); v8_put(dst, 2, 2, v8_avg2(J, I));
        v8_put(dst, 0, 2, v8_avg2(K, J)); v8_put(dst, 2, 3, v8_avg2(K, J));
        v8_put(dst, 0, 3, v8_avg2(L, K));
        v8_put(dst, 3, 0, v8_avg3(A, Bt, C));
        v8_put(dst, 2, 0, v8_avg3(X, A, Bt));
        v8_put(dst, 1, 0, v8_avg3(I, X, A)); v8_put(dst, 3, 1, v8_avg3(I, X, A));
        v8_put(dst, 1, 1, v8_avg3(J, I, X)); v8_put(dst, 3, 2, v8_avg3(J, I, X));
        v8_put(dst, 1, 2, v8_avg3(K, J, I)); v8_put(dst, 3, 3, v8_avg3(K, J, I));
        v8_put(dst, 1, 3, v8_avg3(L, K, J));
        break;
    default:
        v8_put(dst, 0, 0, v8_avg2(I, J));
        v8_put(dst, 2, 0, v8_avg2(J, K)); v8_put(dst, 0, 1, v8_avg2(J, K));
        v8_put(dst, 2, 1, v8_avg2(K, L)); v8_put(dst, 0, 2, v8_avg2(K, L));
        v8_put(dst, 1, 0, v8_avg3(I, J, K));
        v8_put(dst, 3, 0, v8_avg3(J, K, L)); v8_put(dst, 1, 1, v8_avg3(J, K, L));
        v8_put(dst, 3, 1, v8_avg3(K, L, L)); v8_put(dst, 1, 2, v8_avg3(K, L, L));
        v8_put(dst, 3, 2, L); v8_put(dst, 2, 2, L); v8_put(dst, 0, 3, L); v8_put(dst, 1, 3, L); v8_put(dst, 2, 3, L); v8_put(dst, 3, 3, L);
        break;
    }
}

static int v8_mul1(int a) { return ((a * 20091) >> 16) + a; }
static int v8_mul2(int a) { return (a * 35468) >> 16; }

static void v8_add(uint8_t *dst, int x, int y, int v) { dst[x + y * V8_BPS] = v8_clip8(dst[x + y * V8_BPS] + (v >> 3)); }

/* the inverse DCT of a 4x4 block added to its prediction */
static void v8_idct(const int16_t *in, uint8_t *dst) {
    int c[16];
    for (int i = 0; i < 4; i++) {
        int a = in[i] + in[8 + i], b = in[i] - in[8 + i];
        int cc = v8_mul2(in[4 + i]) - v8_mul1(in[12 + i]), dd = v8_mul1(in[4 + i]) + v8_mul2(in[12 + i]);
        c[4 * i] = a + dd;
        c[4 * i + 1] = b + cc;
        c[4 * i + 2] = b - cc;
        c[4 * i + 3] = a - dd;
    }
    for (int i = 0; i < 4; i++) {
        int dc = c[i] + 4, a = dc + c[8 + i], b = dc - c[8 + i];
        int cc = v8_mul2(c[4 + i]) - v8_mul1(c[12 + i]), dd = v8_mul1(c[4 + i]) + v8_mul2(c[12 + i]);
        v8_add(dst, 0, i, a + dd);
        v8_add(dst, 1, i, b + cc);
        v8_add(dst, 2, i, b - cc);
        v8_add(dst, 3, i, a - dd);
    }
}

/* the inverse DCT of a block whose only terms are its DC and in[1] and in[4] */
static void v8_idct_ac3(const int16_t *in, uint8_t *dst) {
    int a = in[0] + 4, c4 = v8_mul2(in[4]), d4 = v8_mul1(in[4]), c1 = v8_mul2(in[1]), d1 = v8_mul1(in[1]);
    int rows[4] = { a + d4, a + c4, a - c4, a - d4 };
    for (int y = 0; y < 4; y++) {
        v8_add(dst, 0, y, rows[y] + d1);
        v8_add(dst, 1, y, rows[y] + c1);
        v8_add(dst, 2, y, rows[y] - c1);
        v8_add(dst, 3, y, rows[y] - d1);
    }
}

static void v8_idct_dc(const int16_t *in, uint8_t *dst) {
    int dc = in[0] + 4;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) v8_add(dst, x, y, dc);
}

/* a luma block's residue by its 2-bit code */
static void v8_transform(uint32_t code, const int16_t *in, uint8_t *dst) {
    if (code == 3) v8_idct(in, dst);
    else if (code == 2) v8_idct_ac3(in, dst);
    else if (code == 1) v8_idct_dc(in, dst);
}

/* a chroma plane's four blocks: all transformed when any has an AC term, else each DC */
static void v8_transform_uv(uint32_t codes, const int16_t *in, uint8_t *dst) {
    if (!(codes & 0xff)) return;
    for (int k = 0; k < 4; k++) {
        uint8_t *d = dst + (k >> 1) * 4 * V8_BPS + (k & 1) * 4;
        if (codes & 0xaa) v8_idct(in + 16 * k, d);
        else if (in[16 * k]) v8_idct_dc(in + 16 * k, d);
    }
}

/* the DC mode of a 16x16 or chroma block at the frame's edge: without its top row, its
   left column, or both */
static int v8_check_mode(uint32_t mb_x, uint32_t mb_y, int mode) {
    if (mode != 0) return mode;
    if (mb_x == 0) return mb_y == 0 ? 6 : 5;
    return mb_y == 0 ? 4 : 0;
}

/* a macroblock rebuilt in the workspace (libwebp's ReconstructRow): the left samples
   brought over from the last one, the top ones from the row above (top_*, by column),
   then each block predicted and its residue added; the top samples kept for the row
   below, and the samples copied to the planes */
static void v8_rebuild(uint8_t *ws, const v8_mb *m, const int16_t *coeffs, uint32_t nzy, uint32_t nzuv, uint32_t mb_x, uint32_t mb_y,
                       uint32_t mb_w, uint32_t mb_h, uint8_t *top_y, uint8_t *top_u, uint8_t *top_v, uint8_t *Y, uint8_t *U, uint8_t *V) {
    uint8_t *yd = ws + V8_Y, *ud = ws + V8_U, *vd = ws + V8_V;
    if (mb_x > 0) {
        for (int j = -1; j < 16; j++) memcpy(yd + j * V8_BPS - 4, yd + j * V8_BPS + 12, 4);
        for (int j = -1; j < 8; j++) {
            memcpy(ud + j * V8_BPS - 4, ud + j * V8_BPS + 4, 4);
            memcpy(vd + j * V8_BPS - 4, vd + j * V8_BPS + 4, 4);
        }
    }
    if (mb_y > 0) {
        memcpy(yd - V8_BPS, top_y + 16 * mb_x, 16);
        memcpy(ud - V8_BPS, top_u + 8 * mb_x, 8);
        memcpy(vd - V8_BPS, top_v + 8 * mb_x, 8);
    }
    if (m->i4x4) {
        uint8_t *tr = yd - V8_BPS + 16;
        if (mb_y > 0) {
            if (mb_x >= mb_w - 1) memset(tr, top_y[16 * mb_x + 15], 4);
            else memcpy(tr, top_y + 16 * (mb_x + 1), 4);
        }
        for (int k = 1; k < 4; k++) memcpy(tr + 4 * k * V8_BPS, tr, 4);
        for (int n = 0; n < 16; n++, nzy <<= 2) {
            uint8_t *dst = yd + (n & 3) * 4 + (n >> 2) * 4 * V8_BPS;
            v8_predict4(dst, m->modes[n]);
            v8_transform(nzy >> 30, coeffs + 16 * n, dst);
        }
    } else {
        v8_predict(yd, 16, v8_check_mode(mb_x, mb_y, m->modes[0]));
        if (nzy)
            for (int n = 0; n < 16; n++, nzy <<= 2) v8_transform(nzy >> 30, coeffs + 16 * n, yd + (n & 3) * 4 + (n >> 2) * 4 * V8_BPS);
    }
    int uvmode = v8_check_mode(mb_x, mb_y, m->uvmode);
    v8_predict(ud, 8, uvmode);
    v8_predict(vd, 8, uvmode);
    v8_transform_uv(nzuv, coeffs + 256, ud);
    v8_transform_uv(nzuv >> 8, coeffs + 320, vd);
    if (mb_y < mb_h - 1) {
        memcpy(top_y + 16 * mb_x, yd + 15 * V8_BPS, 16);
        memcpy(top_u + 8 * mb_x, ud + 7 * V8_BPS, 8);
        memcpy(top_v + 8 * mb_x, vd + 7 * V8_BPS, 8);
    }
    size_t ys = 16 * (size_t)mb_w, uvs = 8 * (size_t)mb_w;
    for (int j = 0; j < 16; j++) memcpy(Y + (16 * (size_t)mb_y + (size_t)j) * ys + 16 * (size_t)mb_x, yd + j * V8_BPS, 16);
    for (int j = 0; j < 8; j++) {
        memcpy(U + (8 * (size_t)mb_y + (size_t)j) * uvs + 8 * (size_t)mb_x, ud + j * V8_BPS, 8);
        memcpy(V + (8 * (size_t)mb_y + (size_t)j) * uvs + 8 * (size_t)mb_x, vd + j * V8_BPS, 8);
    }
}

/* ---- VP8: the loop filter (15) ---- */

static int v8_abs(int v) { return v < 0 ? -v : v; }
static int v8_sclip1(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }
static int v8_sclip2(int v) { return v < -16 ? -16 : v > 15 ? 15 : v; }

/* the edge between p[-step] and p[0]: 4 samples read, the 2 nearest it changed */
static void v8_filter2(uint8_t *p, int step) {
    int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
    int a = 3 * (q0 - p0) + v8_sclip1(p1 - q1);
    int a1 = v8_sclip2((a + 4) >> 3), a2 = v8_sclip2((a + 3) >> 3);
    p[-step] = v8_clip8(p0 + a2);
    p[0] = v8_clip8(q0 - a1);
}

/* the 4 nearest changed (an interior edge's normal filter) */
static void v8_filter4(uint8_t *p, int step) {
    int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
    int a = 3 * (q0 - p0);
    int a1 = v8_sclip2((a + 4) >> 3), a2 = v8_sclip2((a + 3) >> 3), a3 = (a1 + 1) >> 1;
    p[-2 * step] = v8_clip8(p1 + a3);
    p[-step] = v8_clip8(p0 + a2);
    p[0] = v8_clip8(q0 - a1);
    p[step] = v8_clip8(q1 - a3);
}

/* the 6 nearest changed (a macroblock edge's normal filter) */
static void v8_filter6(uint8_t *p, int step) {
    int p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step], q2 = p[2 * step];
    int a = v8_sclip1(3 * (q0 - p0) + v8_sclip1(p1 - q1));
    int a1 = (27 * a + 63) >> 7, a2 = (18 * a + 63) >> 7, a3 = (9 * a + 63) >> 7;
    p[-3 * step] = v8_clip8(p2 + a3);
    p[-2 * step] = v8_clip8(p1 + a2);
    p[-step] = v8_clip8(p0 + a1);
    p[0] = v8_clip8(q0 - a1);
    p[step] = v8_clip8(q1 - a2);
    p[2 * step] = v8_clip8(q2 - a3);
}

static int v8_hev(const uint8_t *p, int step, int thresh) {
    return v8_abs(p[-2 * step] - p[-step]) > thresh || v8_abs(p[step] - p[0]) > thresh;
}

static int v8_needs(const uint8_t *p, int step, int t) { return 4 * v8_abs(p[-step] - p[0]) + v8_abs(p[-2 * step] - p[step]) <= t; }

static int v8_needs2(const uint8_t *p, int step, int t, int it) {
    int p3 = p[-4 * step], p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step], q2 = p[2 * step], q3 = p[3 * step];
    if (4 * v8_abs(p0 - q0) + v8_abs(p1 - q1) > t) return 0;
    return v8_abs(p3 - p2) <= it && v8_abs(p2 - p1) <= it && v8_abs(p1 - p0) <= it && v8_abs(q3 - q2) <= it && v8_abs(q2 - q1) <= it &&
           v8_abs(q1 - q0) <= it;
}

/* the simple filter along an edge of 16 samples: hstep across the edge, vstep along it */
static void v8_simple(uint8_t *p, int hstep, int vstep, int thresh) {
    int t2 = 2 * thresh + 1;
    for (int i = 0; i < 16; i++, p += vstep)
        if (v8_needs(p, hstep, t2)) v8_filter2(p, hstep);
}

/* the normal filter along an edge of size samples: a macroblock edge's (6 changed), or an
   interior one's (4) */
static void v8_normal(uint8_t *p, int hstep, int vstep, int size, int thresh, int ithresh, int hev, int edge) {
    int t2 = 2 * thresh + 1;
    for (int i = 0; i < size; i++, p += vstep) {
        if (!v8_needs2(p, hstep, t2, ithresh)) continue;
        if (v8_hev(p, hstep, hev)) v8_filter2(p, hstep);
        else if (edge) v8_filter6(p, hstep);
        else v8_filter4(p, hstep);
    }
}

/* a macroblock's edges filtered (libwebp's DoFilter): its left edge, its interior
   vertical edges, its top edge, its interior horizontal ones; luma alone for the simple
   filter, chroma too for the normal one */
static void v8_filter_mb(const v8 *d, const uint8_t *fi, uint32_t mb_x, uint32_t mb_y, uint8_t *Y, uint8_t *U, uint8_t *V) {
    int limit = fi[0], ilevel = fi[1], inner = fi[2], hev = fi[3];
    if (limit == 0) return;
    int ys = 16 * (int)d->mb_w, uvs = 8 * (int)d->mb_w;
    uint8_t *y = Y + (16 * (size_t)mb_y) * (size_t)ys + 16 * (size_t)mb_x;
    uint8_t *u = U + (8 * (size_t)mb_y) * (size_t)uvs + 8 * (size_t)mb_x;
    uint8_t *v = V + (8 * (size_t)mb_y) * (size_t)uvs + 8 * (size_t)mb_x;
    if (d->filter_type == 1) {
        if (mb_x > 0) v8_simple(y, 1, ys, limit + 4);
        if (inner)
            for (int k = 1; k < 4; k++) v8_simple(y + 4 * k, 1, ys, limit);
        if (mb_y > 0) v8_simple(y, ys, 1, limit + 4);
        if (inner)
            for (int k = 1; k < 4; k++) v8_simple(y + 4 * k * ys, ys, 1, limit);
        return;
    }
    if (mb_x > 0) {
        v8_normal(y, 1, ys, 16, limit + 4, ilevel, hev, 1);
        v8_normal(u, 1, uvs, 8, limit + 4, ilevel, hev, 1);
        v8_normal(v, 1, uvs, 8, limit + 4, ilevel, hev, 1);
    }
    if (inner) {
        for (int k = 1; k < 4; k++) v8_normal(y + 4 * k, 1, ys, 16, limit, ilevel, hev, 0);
        v8_normal(u + 4, 1, uvs, 8, limit, ilevel, hev, 0);
        v8_normal(v + 4, 1, uvs, 8, limit, ilevel, hev, 0);
    }
    if (mb_y > 0) {
        v8_normal(y, ys, 1, 16, limit + 4, ilevel, hev, 1);
        v8_normal(u, uvs, 1, 8, limit + 4, ilevel, hev, 1);
        v8_normal(v, uvs, 1, 8, limit + 4, ilevel, hev, 1);
    }
    if (inner) {
        for (int k = 1; k < 4; k++) v8_normal(y + 4 * k * ys, ys, 1, 16, limit, ilevel, hev, 0);
        v8_normal(u + 4 * uvs, uvs, 1, 8, limit, ilevel, hev, 0);
        v8_normal(v + 4 * uvs, uvs, 1, 8, limit, ilevel, hev, 0);
    }
}

/* ---- VP8: YUV 4:2:0 to RGB, as libwebp converts it ---- */

static int v8_mulhi(int v, int c) { return (v * c) >> 8; }
static uint8_t v8_clip_yuv(int v) { return (uint8_t)((v & ~16383) == 0 ? v >> 6 : v < 0 ? 0 : 255); }

/* a sample's colour (libwebp's VP8YuvToRgb, 14-bit fixed point) */
static void v8_rgb(int y, int u, int v, uint8_t *o) {
    o[0] = v8_clip_yuv(v8_mulhi(y, 19077) + v8_mulhi(v, 26149) - 14234);
    o[1] = v8_clip_yuv(v8_mulhi(y, 19077) - v8_mulhi(u, 6419) - v8_mulhi(v, 13320) + 8708);
    o[2] = v8_clip_yuv(v8_mulhi(y, 19077) + v8_mulhi(u, 33050) - 17685);
}

/* a pair of rows of len pixels, libwebp's fancy upsampling (UpsampleRgbaLinePair): the
   chroma rows nearer and farther, each output sample's chroma 9/16, 3/16, 3/16, 1/16 of
   the four nearest (the edges repeated), U and V side by side in one word; the bottom
   row too when bottom */
static void v8_upsample(const uint8_t *top_y, const uint8_t *bot_y, int bottom, const uint8_t *top_u, const uint8_t *top_v, const uint8_t *cur_u,
                        const uint8_t *cur_v, uint8_t *top_dst, uint8_t *bot_dst, int len) {
    int last_pair = (len - 1) >> 1;
    uint32_t tl = (uint32_t)top_u[0] | (uint32_t)top_v[0] << 16, l = (uint32_t)cur_u[0] | (uint32_t)cur_v[0] << 16;
    uint32_t uv = (3 * tl + l + 0x00020002u) >> 2;
    v8_rgb(top_y[0], (int)(uv & 0xff), (int)(uv >> 16), top_dst);
    if (bottom) {
        uv = (3 * l + tl + 0x00020002u) >> 2;
        v8_rgb(bot_y[0], (int)(uv & 0xff), (int)(uv >> 16), bot_dst);
    }
    for (int x = 1; x <= last_pair; x++) {
        uint32_t t = (uint32_t)top_u[x] | (uint32_t)top_v[x] << 16, c = (uint32_t)cur_u[x] | (uint32_t)cur_v[x] << 16;
        uint32_t avg = tl + t + l + c + 0x00080008u, d12 = (avg + 2 * (t + l)) >> 3, d03 = (avg + 2 * (tl + c)) >> 3;
        uint32_t a = (d12 + tl) >> 1, b = (d03 + t) >> 1;
        v8_rgb(top_y[2 * x - 1], (int)(a & 0xff), (int)(a >> 16), top_dst + 4 * (2 * x - 1));
        v8_rgb(top_y[2 * x], (int)(b & 0xff), (int)(b >> 16), top_dst + 8 * x);
        if (bottom) {
            a = (d03 + l) >> 1;
            b = (d12 + c) >> 1;
            v8_rgb(bot_y[2 * x - 1], (int)(a & 0xff), (int)(a >> 16), bot_dst + 4 * (2 * x - 1));
            v8_rgb(bot_y[2 * x], (int)(b & 0xff), (int)(b >> 16), bot_dst + 8 * x);
        }
        tl = t;
        l = c;
    }
    if (!(len & 1)) {
        uv = (3 * tl + l + 0x00020002u) >> 2;
        v8_rgb(top_y[len - 1], (int)(uv & 0xff), (int)(uv >> 16), top_dst + 4 * (len - 1));
        if (bottom) {
            uv = (3 * l + tl + 0x00020002u) >> 2;
            v8_rgb(bot_y[len - 1], (int)(uv & 0xff), (int)(uv >> 16), bot_dst + 4 * (len - 1));
        }
    }
}

/* the planes to RGBA rows, paired as libwebp pairs them: the first row alone (its chroma
   row its own neighbour), then rows 2k-1 and 2k from chroma rows k-1 and k, and an even
   height's last row alone */
static void v8_emit(const uint8_t *Y, const uint8_t *U, const uint8_t *V, size_t ys, size_t uvs, uint32_t w, uint32_t h, uint8_t *out) {
    size_t row = 4 * (size_t)w;
    v8_upsample(Y, Y, 0, U, V, U, V, out, out, (int)w);
    uint32_t y = 1;
    for (; y + 1 < h; y += 2) {
        size_t k = (y + 1) / 2;
        v8_upsample(Y + y * ys, Y + (y + 1) * ys, 1, U + (k - 1) * uvs, V + (k - 1) * uvs, U + k * uvs, V + k * uvs, out + y * row,
                    out + (y + 1) * row, (int)w);
    }
    if (h > 1 && !(h & 1)) {
        size_t k = h / 2 - 1;
        const uint8_t *ly = Y + (h - 1) * ys, *lu = U + k * uvs, *lv = V + k * uvs;
        v8_upsample(ly, ly, 0, lu, lv, lu, lv, out + (h - 1) * row, out + (h - 1) * row, (int)w);
    }
}

/* a VP8 key frame p[0, n) decoded into RGBA8 rows, its alpha from alpha (w x h, or
   empty: opaque); 0 when it is malformed or cut short */
static int v8_decode(const uint8_t *p, size_t n, const vector<uint8_t> *alpha, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h) {
    v8 d;
    if (!v8_header(p, n, &d) || (uint64_t)d.w * d.h > AMB_IMAGE_MAX_PIXELS) return 0;
    if (len(*alpha) && len(*alpha) != (size_t)d.w * d.h) return 0;
    *w = d.w;
    *h = d.h;
    size_t mbw = d.mb_w, mbh = d.mb_h, ys = 16 * mbw, uvs = 8 * mbw;
    vector<uint8_t> Y, U, V, intra_t, nz_t, dc_t, top_y, top_u, top_v, finfo;
    resize(&Y, ys * 16 * mbh);
    resize(&U, uvs * 8 * mbh);
    resize(&V, uvs * 8 * mbh);
    resize(&intra_t, 4 * mbw);
    resize(&nz_t, mbw);
    resize(&dc_t, mbw);
    resize(&top_y, ys);
    resize(&top_u, uvs);
    resize(&top_v, uvs);
    resize(&finfo, 4 * mbw * mbh);
    uint8_t ws[832];
    int16_t coeffs[384];
    memset(ws, 0, sizeof ws);
    memset(coeffs, 0, sizeof coeffs);
    for (uint32_t mb_y = 0; mb_y < d.mb_h; mb_y++) {
        uint32_t part = mb_y & (uint32_t)(d.parts - 1);
        uint8_t intra_l[4] = { 0, 0, 0, 0 }, nz_l = 0, dc_l = 0;
        for (int j = 0; j < 16; j++) ws[V8_Y + j * V8_BPS - 1] = 129;
        for (int j = 0; j < 8; j++) {
            ws[V8_U + j * V8_BPS - 1] = 129;
            ws[V8_V + j * V8_BPS - 1] = 129;
        }
        if (mb_y > 0) {
            ws[V8_Y - 1 - V8_BPS] = 129;
            ws[V8_U - 1 - V8_BPS] = 129;
            ws[V8_V - 1 - V8_BPS] = 129;
        } else {
            memset(ws + V8_Y - V8_BPS - 1, 127, 21);
            memset(ws + V8_U - V8_BPS - 1, 127, 9);
            memset(ws + V8_V - V8_BPS - 1, 127, 9);
        }
        for (uint32_t mb_x = 0; mb_x < d.mb_w; mb_x++) {
            v8_mb m;
            memset(&m, 0, sizeof m);
            v8_modes(p, &d, data(intra_t) + 4 * (size_t)mb_x, intra_l, &m);
            uint32_t nzy = 0, nzuv = 0;
            int skip = m.skip;
            if (!skip) skip = v8_residuals(p, &d.part[part], &d, &m, data(nz_t) + mb_x, data(dc_t) + mb_x, &nz_l, &dc_l, coeffs, &nzy, &nzuv);
            else {
                nz_l = 0;
                nz_t[mb_x] = 0;
                if (!m.i4x4) {
                    dc_l = 0;
                    dc_t[mb_x] = 0;
                }
            }
            if (d.part[part].eof) return 0;
            if (d.filter_type) {
                uint8_t *fi = data(finfo) + 4 * ((size_t)mb_y * mbw + mb_x);
                fi[0] = (uint8_t)d.f_limit[m.segment][m.i4x4];
                fi[1] = (uint8_t)d.f_ilevel[m.segment][m.i4x4];
                fi[2] = (uint8_t)(m.i4x4 || !skip);
                fi[3] = (uint8_t)d.f_hev[m.segment][m.i4x4];
            }
            v8_rebuild(ws, &m, coeffs, nzy, nzuv, mb_x, mb_y, d.mb_w, d.mb_h, data(top_y), data(top_u), data(top_v), data(Y), data(U), data(V));
        }
        if (d.br.eof) return 0;
    }
    if (d.filter_type)
        for (uint32_t mb_y = 0; mb_y < d.mb_h; mb_y++)
            for (uint32_t mb_x = 0; mb_x < d.mb_w; mb_x++)
                v8_filter_mb(&d, data(finfo) + 4 * ((size_t)mb_y * mbw + mb_x), mb_x, mb_y, data(Y), data(U), data(V));
    clear(*rgba);
    resize(rgba, (size_t)d.w * d.h * 4);
    uint8_t *o = data(*rgba);
    v8_emit(data(Y), data(U), data(V), ys, uvs, d.w, d.h, o);
    for (size_t i = 0; i < (size_t)d.w * d.h; i++) o[4 * i + 3] = len(*alpha) ? at(*alpha, i) : 255;
    return 1;
}

/* ---- ALPH: the alpha plane ---- */

/* a row of a w-wide alpha plane unfiltered in place (libwebp's WebPUnfilters):
   horizontal from the left (the first sample from the one above, or 0 on the first row),
   vertical from above, gradient from left + above - corner clipped; the first row of any
   filter is horizontal */
static void al_unfilter(int filter, int first, const uint8_t *prev, uint8_t *row, uint32_t w) {
    if (filter == 1 || first) {
        uint8_t pred = first ? 0 : prev[0];
        for (uint32_t x = 0; x < w; x++) {
            row[x] = (uint8_t)(pred + row[x]);
            pred = row[x];
        }
    } else if (filter == 2) {
        for (uint32_t x = 0; x < w; x++) row[x] = (uint8_t)(prev[x] + row[x]);
    } else {
        int top = prev[0], corner = top, left = top;
        for (uint32_t x = 0; x < w; x++) {
            top = prev[x];
            left = (uint8_t)(row[x] + v8_clip8(left + top - corner));
            corner = top;
            row[x] = (uint8_t)left;
        }
    }
}

/* an ALPH chunk's payload q[0, m) for a w x h image: its header byte (method, filter,
   level reduction, which needs nothing undone), its plane raw or VP8L-coded in the
   green of an image stream, unfiltered; 0 when it is malformed */
static int al_decode(const uint8_t *q, size_t m, uint32_t w, uint32_t h, vector<uint8_t> *a) {
    if (m <= 1) return 0;
    int method = q[0] & 3, filter = q[0] >> 2 & 3;
    if (method > 1 || (q[0] >> 4 & 3) > 1 || (q[0] >> 6)) return 0;
    size_t np = (size_t)w * h;
    clear(*a);
    resize(a, np);
    uint8_t *o = data(*a);
    if (method == 0) {
        if (m - 1 < np) return 0;
        memcpy(o, q + 1, np);
    } else {
        vector<uint32_t> argb;
        wl_br b;
        wl_begin(&b, m - 1);
        if (!wl_image(q + 1, &b, (int)w, (int)h, &argb)) return 0;
        for (size_t i = 0; i < np; i++) o[i] = (uint8_t)(at(argb, i) >> 8);
    }
    if (filter)
        for (uint32_t y = 0; y < h; y++) al_unfilter(filter, y == 0, o + (y ? (size_t)(y - 1) * w : 0), o + (size_t)y * w, w);
    return 1;
}

/* ---- the container ---- */

#define WP_MAX_PAYLOAD 0xfffffff6u
#define WP_FLAGS 0x3eu

/* a frame: where it lies on the canvas (its size its bitstream's), its duration, whether
   it blends and whether it is disposed to the background, its image chunk (VP8L or
   VP8: where its payload starts, its declared size, how much of it, padded, the data
   holds), its ALPH chunk's payload (alpha_len 0: none), whether it has alpha, and whether
   the data holds all of it */
typedef struct wp_frame {
    uint32_t x, y, w, h, duration;
    int32_t blend, dispose, lossless, has_alpha, complete;
    size_t img, img_size, img_len, alpha, alpha_len;
} wp_frame;

/* a file: its canvas, its loop count (ANIM's, 0 for ever), its VP8X flags, whether it is
   an animation, and whether its data ends before its RIFF chunk */
typedef struct wp_file { uint32_t cw, ch, loops, flags; int32_t animated, partial; } wp_file;

/* a VP8 or VP8L chunk's header, the available part q[0, have) of a payload of size
   bytes, checked as WebPGetFeatures checks it: its size, whether a VP8L one says it has
   alpha; 0 when it is none, -1 when the data ends first */
static int wp_features(const uint8_t *q, size_t have, uint32_t size, int lossless, uint32_t *w, uint32_t *h, int *alpha) {
    *alpha = 0;
    if (lossless) {
        if (have < 5) return -1;
        if (!wl_header(q, have, w, h)) return 0;
        *alpha = (int)(wp_le32(q + 1) >> 28 & 1);
        return 1;
    }
    if (have < 10) return -1;
    uint32_t tag = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16;
    if (q[3] != 0x9d || q[4] != 0x01 || q[5] != 0x2a) return 0;
    if ((tag & 1) || ((tag >> 1) & 7) > 3 || !((tag >> 4) & 1) || (tag >> 5) >= size) return 0;
    *w = wp_le16(q + 6) & 0x3fff;
    *h = wp_le16(q + 8) & 0x3fff;
    return *w && *h;
}

/* a frame's chunks from i (libwebp's StoreFrame): an ALPH and a VP8 or VP8L (never
   after an ALPH), each the first of its kind, anything else ending them; the data ends
   at n, the RIFF chunk at rend. The position after them; *bad when a chunk is malformed
   or passes the RIFF chunk */
static size_t wp_store(const uint8_t *p, size_t n, size_t rend, size_t i, wp_frame *f, int *bad) {
    int alpha = 0, image = 0;
    while (n - i >= 8) {
        uint32_t size = wp_le32(p + i + 4);
        size_t padded = (size_t)size + (size & 1), avail = n - i - 8;
        if (size > WP_MAX_PAYLOAD || padded > rend - i - 8) {
            *bad = 1;
            return i;
        }
        size_t have = padded < avail ? padded : avail;
        int complete = padded <= avail;
        if (memcmp(p + i, "ALPH", 4) == 0) {
            if (alpha) break;
            alpha = 1;
            f->alpha = i + 8;
            f->alpha_len = size;
            f->has_alpha = 1;
        } else if (memcmp(p + i, "VP8L", 4) == 0 || memcmp(p + i, "VP8 ", 4) == 0) {
            int lossless = p[i + 3] == 'L', a = 0;
            if (lossless && alpha) {
                *bad = 1;
                return i;
            }
            if (image) break;
            int ok = wp_features(p + i + 8, have, size, lossless, &f->w, &f->h, &a);
            if (ok == 0 || (ok < 0 && complete)) {
                *bad = 1;
                return i;
            }
            if (ok < 0) return n;
            image = 1;
            f->img = i + 8;
            f->img_size = size;
            f->img_len = have;
            f->lossless = lossless;
            f->has_alpha |= a;
            f->complete = complete;
        } else break;
        i += 8 + have;
        if (!complete || i == rend) break;
    }
    return i;
}

/* a WebP's chunks read and checked as libwebp's demuxer reads and checks them (a simple
   file's image, or VP8X's canvas and its still image or ANIM and ANMF frames): its
   frames into frames. 0 when it is no WebP or its chunks are malformed; a file cut
   short is read as far as it goes and marked partial, 0 when not even its canvas is
   known */
static int wp_parse(const uint8_t *p, size_t n, wp_file *f, vector<wp_frame> *frames) {
    memset(f, 0, sizeof *f);
    clear(*frames);
    if (n < 20 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WEBP", 4) != 0) return 0;
    uint32_t riff = wp_le32(p + 4);
    if (riff < 8 || riff > WP_MAX_PAYLOAD) return 0;
    size_t rend = (size_t)riff + 8;
    if (n > rend) n = rend;
    f->partial = n < rend;
    f->loops = 1;
    int bad = 0;
    wp_frame fr;
    memset(&fr, 0, sizeof fr);
    if (memcmp(p + 12, "VP8X", 4) != 0) {
        if (memcmp(p + 12, "VP8 ", 4) != 0 && memcmp(p + 12, "VP8L", 4) != 0) return 0;
        wp_store(p, n, rend, 12, &fr, &bad);
        if (bad || !fr.w) return 0;
        /* no VP8X, so no alpha flag: an ALPH chunk is passed over */
        fr.alpha_len = 0;
        fr.has_alpha = fr.lossless && fr.has_alpha;
        f->cw = fr.w;
        f->ch = fr.h;
        push_back(frames, fr);
        return 1;
    }
    uint32_t vsize = wp_le32(p + 16);
    if (vsize > WP_MAX_PAYLOAD || vsize < 10) return 0;
    size_t vpad = (size_t)vsize + (vsize & 1);
    if (vpad > rend - 20 || n - 20 < vpad) return 0;
    f->flags = p[20];
    f->cw = 1 + wp_le24(p + 24);
    f->ch = 1 + wp_le24(p + 27);
    f->animated = (f->flags & 2) != 0;
    size_t i = 20 + vpad;
    int anims = 0;
    while (i < rend) {
        if (n - i < 8) {
            f->partial = 1;
            break;
        }
        uint32_t size = wp_le32(p + i + 4);
        if (size > WP_MAX_PAYLOAD) return 0;
        size_t padded = (size_t)size + (size & 1), avail = n - i - 8;
        if (padded > rend - i - 8) return 0;
        if (memcmp(p + i, "VP8X", 4) == 0) return 0;
        if (memcmp(p + i, "ALPH", 4) == 0 || memcmp(p + i, "VP8 ", 4) == 0 || memcmp(p + i, "VP8L", 4) == 0) {
            if (anims || f->animated || len(*frames)) return 0;
            memset(&fr, 0, sizeof fr);
            i = wp_store(p, n, rend, i, &fr, &bad);
            if (bad) return 0;
            if (!(f->flags & 0x10)) {
                fr.alpha_len = 0;
                fr.has_alpha = fr.lossless && fr.has_alpha;
            }
            if (!fr.w) {
                if (n < rend) {
                    f->partial = 1;
                    break;
                }
                return 0;
            }
            if (fr.alpha_len && fr.alpha > fr.img) return 0;
            push_back(frames, fr);
            if (!fr.complete) break;
            continue;
        }
        if (memcmp(p + i, "ANIM", 4) == 0) {
            if (padded < 6) return 0;
            if (avail < padded) {
                f->partial = 1;
                break;
            }
            if (!anims) f->loops = wp_le16(p + i + 12);
            anims++;
        } else if (memcmp(p + i, "ANMF", 4) == 0) {
            if (!anims || padded < 16) return 0;
            if (avail < padded) {
                f->partial = 1;
                break;
            }
            memset(&fr, 0, sizeof fr);
            fr.x = 2 * wp_le24(p + i + 8);
            fr.y = 2 * wp_le24(p + i + 11);
            if ((uint64_t)(1 + wp_le24(p + i + 14)) * (1 + wp_le24(p + i + 17)) >= (1ull << 32)) return 0;
            fr.duration = wp_le24(p + i + 20);
            fr.dispose = p[i + 23] & 1;
            fr.blend = !(p[i + 23] & 2);
            size_t start = i + 24, end = wp_store(p, n, rend, start, &fr, &bad);
            if (bad || end - start > padded - 16) return 0;
            if (f->animated && (fr.w || fr.alpha_len)) {
                if (!fr.w || (fr.alpha_len && fr.alpha > fr.img)) return 0;
                push_back(frames, fr);
            }
            i = end;
            continue;
        }
        if (avail < padded) {
            f->partial = 1;
            break;
        }
        i += 8 + padded;
    }
    if ((f->flags & ~WP_FLAGS) || (!f->partial && len(*frames) == 0)) return 0;
    for (size_t k = 0; k < len(*frames); k++) {
        wp_frame g = at(*frames, k);
        if (!g.complete) {
            if (!f->partial || k + 1 < len(*frames)) return 0;
            continue;
        }
        if (f->animated) {
            if ((uint64_t)g.x + g.w > f->cw || (uint64_t)g.y + g.h > f->ch) return 0;
        } else if (g.x || g.y || g.w != f->cw || g.h != f->ch) return 0;
    }
    return 1;
}

/* a frame's own pixels, RGBA8, its size its bitstream's; 0 when they cannot be decoded */
static int wp_pixels(const uint8_t *p, const wp_frame *fr, vector<uint8_t> *rgba) {
    uint32_t w = 0, h = 0;
    if (!fr->complete) return 0;
    if (fr->lossless) {
        if (!wl_decode(p + fr->img, fr->img_len, rgba, &w, &h)) return 0;
    } else {
        vector<uint8_t> alpha;
        if (fr->alpha_len && !al_decode(p + fr->alpha, fr->alpha_len, fr->w, fr->h, &alpha)) return 0;
        if (!v8_decode(p + fr->img, fr->img_len, &alpha, rgba, &w, &h)) return 0;
    }
    return w == fr->w && h == fr->h;
}

/* src blended over dst, neither premultiplied, in libwebp's integer arithmetic
   (BlendPixelNonPremult): a source of alpha 0 leaves dst */
static void wp_blend(uint8_t *src, const uint8_t *dst) {
    uint32_t sa = src[3];
    if (sa == 0) {
        memcpy(src, dst, 4);
        return;
    }
    uint32_t da = (uint8_t)((dst[3] * (256 - sa)) >> 8), ba = (uint8_t)(sa + da), scale = (1u << 24) / ba;
    for (int c = 0; c < 3; c++) src[c] = (uint8_t)(((uint32_t)src[c] * sa + (uint32_t)dst[c] * da) * scale >> 24);
    src[3] = (uint8_t)ba;
}

static int wp_full(const wp_frame *fr, const wp_file *f) { return fr->w == f->cw && fr->h == f->ch; }

/* the canvas after frame upto of an animation, as libwebp's animation decoder composes it:
   the last frame disposed of (its rectangle cleared, when it says so); a key frame -- the
   first, one that covers the canvas and does not blend or has no alpha, or one after a
   frame disposed of that covered the canvas or was itself key -- drawn over a clear
   canvas; any other frame's pixels written over its rectangle and, when it blends, each
   not opaque blended over what was there, except where the disposed frame left it clear.
   The frames' areas summed are capped: past AMB_IMAGE_MAX_FRAME_WORK, the frames before
   are drawn. *drawn the frame drawn last; 0 when a frame cannot be decoded */
static int wp_compose(const uint8_t *p, const wp_file *f, const vector<wp_frame> *frames, uint32_t upto, vector<uint8_t> *canvas, uint32_t *drawn) {
    size_t cw = f->cw;
    clear(*canvas);
    resize(canvas, cw * f->ch * 4);
    vector<uint8_t> px, under;
    int prev_key = 0;
    uint64_t work = 0;
    *drawn = upto;
    for (uint32_t k = 0; k <= upto; k++) {
        wp_frame fr = at(*frames, k);
        work += (uint64_t)fr.w * fr.h;
        if (work > AMB_IMAGE_MAX_FRAME_WORK) {
            *drawn = k ? k - 1 : 0;
            return 1;
        }
        uint8_t *cv = data(*canvas);
        int key = k == 0 || ((!fr.has_alpha || !fr.blend) && wp_full(&fr, f));
        if (k > 0) {
            wp_frame pv = at(*frames, k - 1);
            if (pv.dispose) {
                for (uint32_t y = 0; y < pv.h; y++) memset(cv + (((size_t)pv.y + y) * cw + pv.x) * 4, 0, (size_t)pv.w * 4);
                if (wp_full(&pv, f) || prev_key) key = 1;
            }
        }
        if (!wp_pixels(p, &fr, &px)) return 0;
        cv = data(*canvas);
        int blend = !key && fr.blend;
        if (blend) {
            clear(under);
            for (uint32_t y = 0; y < fr.h; y++) append_range(under, cv + (((size_t)fr.y + y) * cw + fr.x) * 4, (size_t)fr.w * 4);
        }
        for (uint32_t y = 0; y < fr.h; y++) memcpy(cv + (((size_t)fr.y + y) * cw + fr.x) * 4, data(px) + (size_t)y * fr.w * 4, (size_t)fr.w * 4);
        if (blend) {
            wp_frame pv = at(*frames, k - 1);
            const uint8_t *un = data(under);
            for (uint32_t y = 0; y < fr.h; y++) {
                uint32_t cy = fr.y + y;
                for (uint32_t x = 0; x < fr.w; x++) {
                    uint32_t cx = fr.x + x;
                    if (pv.dispose && cy >= pv.y && cy < pv.y + pv.h && cx >= pv.x && cx < pv.x + pv.w) continue;
                    uint8_t *s = cv + ((size_t)cy * cw + cx) * 4;
                    if (s[3] != 255) wp_blend(s, un + ((size_t)y * fr.w + x) * 4);
                }
            }
        }
        prev_key = key;
    }
    return 1;
}

int amb_webp_size(const uint8_t *p, size_t n, uint32_t *w, uint32_t *h) {
    wp_file f;
    vector<wp_frame> frames;
    *w = 0;
    *h = 0;
    if (!wp_parse(p, n, &f, &frames)) return 0;
    *w = f.cw;
    *h = f.ch;
    return 1;
}

int amb_webp_frame(const uint8_t *p, size_t n, uint32_t index, vector<uint8_t> *rgba, amb_image_info *info) {
    memset(info, 0, sizeof *info);
    wp_file f;
    vector<wp_frame> frames;
    if (!wp_parse(p, n, &f, &frames) || f.partial || len(frames) == 0) return 0;
    if ((uint64_t)f.cw * f.ch > AMB_IMAGE_MAX_PIXELS) return 0;
    uint32_t count = (uint32_t)len(frames);
    if (index == AMB_IMAGE_AT_REST) {
        vector<uint32_t> ms;
        for (uint32_t k = 0; k < count; k++) push_back(ms, at(frames, k).duration);
        index = amb_image_frame_at(data(ms), count, f.animated ? f.loops : 1, AMB_IMAGE_REST_MS);
    }
    if (index >= count) return 0;
    uint32_t drawn = index;
    if (!f.animated) {
        wp_frame fr = at(frames, 0);
        if (!wp_pixels(p, &fr, rgba)) return 0;
    } else if (!wp_compose(p, &f, &frames, index, rgba, &drawn)) return 0;
    info->w = f.cw;
    info->h = f.ch;
    info->frames = count;
    info->orientation = 1;
    info->loops = f.animated ? f.loops : 1;
    info->frame = drawn;
    info->partial = drawn != index;
    return 1;
}
