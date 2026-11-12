/* src/net/zstd.c -- Zstandard (RFC 8878) decoded (zstd.h): HTTP's zstd content coding.

   Content is frames, each its magic number, a header and blocks; a skippable frame is
   passed over. A frame's header gives its window, its content's size and whether a
   checksum ends it. One that names a dictionary is refused, as Chromium refuses it (it
   holds none for the zstd coding), and so is a window past 8 MB (Chromium's
   ZSTD_d_windowLogMax of 23, RFC 9659's bound for HTTP). A block is raw, RLE or
   compressed. A compressed block is its literals, then its sequences:
   - the literals raw, RLE, or Huffman coded in one stream or four, their tree described
     by weights written directly or FSE coded, or the frame's last tree again (treeless);
   - each sequence a run of literals and a match, its literal length, offset and match
     length FSE coded by a table predefined, of one symbol (RLE), described in the block,
     or the last block's (repeat), the three states interleaved in one backward bit
     stream; an offset of 1 to 3 is one of the three last ones (repeat offsets).
   The content's XXH64 is checked when the frame carries its low 32 bits.

   The data is checked as libzstd 1.5's decoder checks it: a table description whose
   probabilities do not fill its table, a Huffman tree that is not complete, a stream not
   read to its last bit (or read past it), literals past those decoded, a match before the
   frame's start or past its window, a block past its maximum (the window, at most
   128 KB), a reserved bit or block type, and a size or checksum that is not the
   content's each refuse the whole content. Nothing outside the input is read, nothing
   past cap written.

   With a raw dictionary (net_zstd_dict: RFC 9842's dcz coding, as Chromium loads one,
   ZSTD_dct_rawContent), every frame's content follows the dictionary's bytes: a match may
   reach back into them, past the frame's start (and then on into the content), as far as the
   dictionary's first byte, whatever the window; the repeat offsets begin as without one. A
   frame naming a dictionary ID is still refused (the raw dictionary has none, as libzstd
   refuses it), and the window may then be as large as 1.25 times the dictionary, rounded up
   to a power of two, from 8 MB to 128 MB (Chromium's ZSTD_d_windowLogMax, clamp(ceil(log2(size
   * 1.25)), 23, 27)). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "zstd.h"

#define ZS_MAGIC 0xFD2FB528u
#define ZS_BLOCK_MAX (128u << 10)
#define ZS_WINDOW_MAX (8u << 20)

/* why a content was refused: an index of zs_why */
enum { ZS_OK, ZS_E_MAGIC, ZS_E_SHORT, ZS_E_RESERVED, ZS_E_DICT, ZS_E_WINDOW, ZS_E_BLOCK_TYPE, ZS_E_BLOCK_SIZE, ZS_E_LITERALS,
       ZS_E_HUFFMAN, ZS_E_TREELESS, ZS_E_SEQ_HEADER, ZS_E_FSE, ZS_E_REPEAT_TABLE, ZS_E_SEQUENCES, ZS_E_OFFSET, ZS_E_SIZE,
       ZS_E_CHECKSUM, ZS_E_CAP, ZS_E_TRAILING, ZS_E_EMPTY };
static const char *const zs_why[] = {
    "",
    "the zstd body does not begin as a zstd frame does",
    "the zstd body ends inside a frame",
    "a zstd frame's reserved bit is set",
    "a zstd frame names a dictionary",
    "a zstd frame's window passes 8 MB",
    "a zstd block is of the reserved type",
    "a zstd block passes its frame's block size",
    "a zstd block's literals section is not one",
    "a zstd block's Huffman tree is not one",
    "a zstd block reuses a Huffman tree its frame has not had",
    "a zstd block's sequences section header is not one",
    "a zstd block's FSE table is not one",
    "a zstd block repeats an FSE table its frame has not had",
    "a zstd block's sequences do not decode",
    "a zstd sequence's match reaches past what its frame has decoded",
    "the zstd frame's content size is not its content's",
    "the zstd frame's checksum is not its content's",
    "the decoded body passed its bound",
    "what follows a zstd frame is no frame",
    "the zstd body is empty",
};

static uint32_t zs_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t zs_highbit(uint32_t x) {
    uint32_t b = 31;
    while (b && !(x >> b & 1)) b--;
    return b;
}

/* ---- XXH64 ---- */

#define ZX_P1 0x9E3779B185EBCA87ull
#define ZX_P2 0xC2B2AE3D27D4EB4Full
#define ZX_P3 0x165667B19E3779F9ull
#define ZX_P4 0x85EBCA77C2B2AE63ull
#define ZX_P5 0x27D4EB2F165667C5ull

static uint64_t zx_rotl(uint64_t x, int r) { return x << r | x >> (64 - r); }
static uint64_t zx_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static uint64_t zx_round(uint64_t acc, uint64_t lane) {
    acc += lane * ZX_P2;
    acc = zx_rotl(acc, 31);
    return acc * ZX_P1;
}
static uint64_t zx_merge(uint64_t acc, uint64_t v) {
    acc ^= zx_round(0, v);
    return acc * ZX_P1 + ZX_P4;
}

uint64_t net_xxh64(const uint8_t *p, size_t n, uint64_t seed) {
    size_t i = 0;
    uint64_t h;
    if (n >= 32) {
        uint64_t v1 = seed + ZX_P1 + ZX_P2, v2 = seed + ZX_P2, v3 = seed, v4 = seed - ZX_P1;
        for (; i + 32 <= n; i += 32) {
            v1 = zx_round(v1, zx_le64(p + i));
            v2 = zx_round(v2, zx_le64(p + i + 8));
            v3 = zx_round(v3, zx_le64(p + i + 16));
            v4 = zx_round(v4, zx_le64(p + i + 24));
        }
        h = zx_rotl(v1, 1) + zx_rotl(v2, 7) + zx_rotl(v3, 12) + zx_rotl(v4, 18);
        h = zx_merge(h, v1);
        h = zx_merge(h, v2);
        h = zx_merge(h, v3);
        h = zx_merge(h, v4);
    } else h = seed + ZX_P5;
    h += (uint64_t)n;
    for (; i + 8 <= n; i += 8) {
        h ^= zx_round(0, zx_le64(p + i));
        h = zx_rotl(h, 27) * ZX_P1 + ZX_P4;
    }
    if (i + 4 <= n) {
        h ^= (uint64_t)zs_le32(p + i) * ZX_P1;
        h = zx_rotl(h, 23) * ZX_P2 + ZX_P3;
        i += 4;
    }
    for (; i < n; i++) {
        h ^= (uint64_t)p[i] * ZX_P5;
        h = zx_rotl(h, 11) * ZX_P1;
    }
    h ^= h >> 33;
    h *= ZX_P2;
    h ^= h >> 29;
    h *= ZX_P3;
    h ^= h >> 32;
    return h;
}

/* ---- bit streams ---- */

/* a forward bit stream, as an FSE table's description is: bytes [start, end) of the input,
   its first bit the lowest of its first byte; bit, the bits read; bad, a read past its end */
typedef struct zs_fwd { size_t start, end, bit; int bad; } zs_fwd;

/* the next k bits (k at most 24), not taken: 0 past the end */
static uint32_t zs_fpeek(const uint8_t *in, const zs_fwd *f, uint32_t k) {
    size_t total = (f->end - f->start) * 8;
    uint32_t v = 0;
    for (uint32_t i = 0; i < k; i++) {
        size_t b = f->bit + i;
        if (b < total && (in[f->start + (b >> 3)] >> (b & 7) & 1)) v |= 1u << i;
    }
    return v;
}
static void zs_fskip(zs_fwd *f, uint32_t k) {
    f->bit += k;
    if (f->bit > (f->end - f->start) * 8) f->bad = 1;
}
static uint32_t zs_fget(const uint8_t *in, zs_fwd *f, uint32_t k) {
    uint32_t v = zs_fpeek(in, f, k);
    zs_fskip(f, k);
    return v;
}

/* a backward bit stream, as Huffman's and FSE's are: bytes [start, ...) of the input, read
   from its last bit down, the last byte's highest 1 its end mark; pos, the bits not read
   (bit 0 the lowest of its first byte); over, the bits read past its start, as zeros */
typedef struct zs_back { size_t start, pos, over; } zs_back;

/* the stream of bytes [start, end): 0 when it has no end mark */
static int zs_binit(const uint8_t *in, size_t start, size_t end, zs_back *b) {
    if (end <= start || in[end - 1] == 0) return 0;
    b->start = start;
    b->pos = (end - start - 1) * 8 + zs_highbit(in[end - 1]);
    b->over = 0;
    return 1;
}
/* the k bits (k at most 56) of the stream from its bit p up, as a number */
static uint64_t zs_bits_at(const uint8_t *in, size_t start, size_t p, uint32_t k) {
    size_t b0 = p >> 3, b1 = (p + k - 1) >> 3;
    uint64_t w = 0;
    for (size_t i = b1 + 1; i-- > b0;) w = w << 8 | in[start + i];
    return w >> (p & 7) & (((uint64_t)1 << k) - 1);
}
/* the next k bits, the first read the highest */
static uint64_t zs_bread(const uint8_t *in, zs_back *b, uint32_t k) {
    if (k == 0) return 0;
    if (b->pos >= k) {
        b->pos -= k;
        return zs_bits_at(in, b->start, b->pos, k);
    }
    uint64_t v = 0;
    if (b->pos) v = zs_bits_at(in, b->start, 0, (uint32_t)b->pos) << (k - b->pos);
    b->over += k - b->pos;
    b->pos = 0;
    return v;
}
static uint32_t zs_bpeek(const uint8_t *in, const zs_back *b, uint32_t k) {
    if (b->pos >= k) return (uint32_t)zs_bits_at(in, b->start, b->pos - k, k);
    if (!b->pos) return 0;
    return (uint32_t)(zs_bits_at(in, b->start, 0, (uint32_t)b->pos) << (k - b->pos));
}
static void zs_bskip(zs_back *b, uint32_t k) {
    if (b->pos >= k) b->pos -= k;
    else {
        b->over += k - b->pos;
        b->pos = 0;
    }
}

/* ---- FSE ---- */

/* an FSE decoding table: each state's symbol, the bits read for the next state and the
   base they are added to; its accuracy log; ok once it holds a table */
typedef struct zs_fse { uint8_t sym[512]; uint8_t nb[512]; uint16_t base[512]; uint32_t log; int ok; } zs_fse;

/* the predefined distributions (RFC 8878, 3.1.1.3.2.2): literal lengths and match lengths
   of accuracy 6, offsets of 5; -1 a probability below one */
static const int16_t zs_ll_default[36] = { 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1,
                                           1, 1, -1, -1, -1, -1 };
static const int16_t zs_ml_default[53] = { 1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1 };
static const int16_t zs_of_default[29] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1 };

/* the literal lengths' and match lengths' codes: each one's baseline and extra bits */
static const uint32_t zs_ll_base[36] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 40, 48,
                                         64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
static const uint8_t zs_ll_bits[36] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10,
                                        11, 12, 13, 14, 15, 16 };
static const uint32_t zs_ml_base[53] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
                                         27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515,
                                         1027, 2051, 4099, 8195, 16387, 32771, 65539 };
static const uint8_t zs_ml_bits[53] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

/* an FSE table's description from in[at, end) (RFC 8878, 4.1.1): its accuracy log, at most
   max_log, and each symbol's normalized count up to max_sym (-1: below one); how many
   symbols in *nsym, the bytes it took in *used. 0 when it is no description */
static int zs_ncount(const uint8_t *in, size_t at, size_t end, uint32_t max_sym, uint32_t max_log, int16_t *norm, uint32_t *nsym,
                     uint32_t *log, size_t *used) {
    zs_fwd f = { at, end, 0, 0 };
    if (at >= end) return 0;
    uint32_t al = zs_fget(in, &f, 4) + 5;
    if (al > max_log) return 0;
    int32_t remaining = (1 << al) + 1, threshold = 1 << al;
    uint32_t nbits = al + 1, s = 0;
    int prev0 = 0;
    while (remaining > 1 && s <= max_sym) {
        if (prev0) {
            /* zeros: a 2-bit count of them, another after each 3 */
            uint32_t n0 = s;
            for (;;) {
                uint32_t r = zs_fget(in, &f, 2);
                n0 += r;
                if (r < 3 || f.bad) break;
            }
            if (f.bad || n0 > max_sym) return 0;
            while (s < n0) norm[s++] = 0;
        }
        int32_t max = (2 * threshold - 1) - remaining, count;
        int32_t low = (int32_t)zs_fpeek(in, &f, nbits - 1);
        if (low < max) {
            count = low;
            zs_fskip(&f, nbits - 1);
        } else {
            count = (int32_t)zs_fpeek(in, &f, nbits);
            if (count >= threshold) count -= max;
            zs_fskip(&f, nbits);
        }
        count--;
        if (count >= 0) remaining -= count;
        else remaining += count;
        norm[s++] = (int16_t)count;
        prev0 = count == 0;
        if (remaining < threshold) {
            if (remaining <= 1) break;
            nbits = zs_highbit((uint32_t)remaining) + 1;
            threshold = 1 << (nbits - 1);
        }
    }
    if (remaining != 1 || f.bad || s > max_sym + 1) return 0;
    *nsym = s;
    *log = al;
    *used = (f.bit + 7) / 8;
    return 1;
}

/* the decoding table of a distribution (RFC 8878, 4.1.1): below-one symbols at the table's
   end, the others spread by the step, then each state's bits and base by its symbol's
   order. 0 when the spread does not close */
static int zs_fse_build(zs_fse *t, const int16_t *norm, uint32_t nsym, uint32_t log) {
    uint32_t size = 1u << log, high = size - 1;
    uint16_t next[256];
    t->ok = 0;
    for (uint32_t s = 0; s < nsym; s++) {
        if (norm[s] == -1) {
            t->sym[high] = (uint8_t)s;
            if (high) high--;
            next[s] = 1;
        } else next[s] = (uint16_t)norm[s];
    }
    uint32_t step = (size >> 1) + (size >> 3) + 3, mask = size - 1, pos = 0;
    for (uint32_t s = 0; s < nsym; s++)
        for (int32_t i = 0; i < norm[s]; i++) {
            t->sym[pos] = (uint8_t)s;
            do pos = (pos + step) & mask;
            while (pos > high);
        }
    if (pos != 0) return 0;
    for (uint32_t u = 0; u < size; u++) {
        uint32_t s = t->sym[u], x = next[s]++;
        uint32_t nb = log - zs_highbit(x);
        t->nb[u] = (uint8_t)nb;
        t->base[u] = (uint16_t)((x << nb) - size);
    }
    t->log = log;
    t->ok = 1;
    return 1;
}

/* ---- Huffman ---- */

/* a Huffman decoding table: by the next log bits, the symbol and the bits its code takes */
typedef struct zs_huf { uint8_t sym[4096]; uint8_t nb[4096]; uint32_t log; int ok; } zs_huf;

/* a Huffman tree's description from in[at, end) (RFC 8878, 4.2.1): its weights written
   directly, four bits each, or FSE coded with two states; the last weight is implied, the
   total a power of two. The table built from them, the bytes it took in *used */
static int zs_huf_read(const uint8_t *in, size_t at, size_t end, zs_huf *h, size_t *used, net_zstd_stats *st) {
    uint8_t w[256];
    uint32_t nw = 0;
    h->ok = 0;
    if (at >= end) return 0;
    uint32_t hb = in[at];
    if (hb >= 128) {
        nw = hb - 127;
        size_t bytes = (nw + 1) / 2;
        if (at + 1 + bytes > end) return 0;
        for (uint32_t i = 0; i < nw; i++) {
            uint8_t b = in[at + 1 + i / 2];
            if (i & 1) w[i] = (uint8_t)(b & 15);
            else w[i] = (uint8_t)(b >> 4);
        }
        *used = 1 + bytes;
        if (st) st->weights_direct++;
    } else {
        if (hb == 0 || at + 1 + hb > end) return 0;
        int16_t norm[256];
        uint32_t nsym = 0, log = 0;
        size_t u = 0;
        if (!zs_ncount(in, at + 1, at + 1 + hb, 255, 6, norm, &nsym, &log, &u)) return 0;
        zs_fse ft;
        if (!zs_fse_build(&ft, norm, nsym, log)) return 0;
        zs_back b;
        if (!zs_binit(in, at + 1 + u, at + 1 + hb, &b)) return 0;
        uint32_t s1 = (uint32_t)zs_bread(in, &b, log), s2 = (uint32_t)zs_bread(in, &b, log);
        for (;;) {
            if (nw > 253) return 0;
            w[nw++] = ft.sym[s1];
            s1 = ft.base[s1] + (uint32_t)zs_bread(in, &b, ft.nb[s1]);
            if (b.over) {
                w[nw++] = ft.sym[s2];
                break;
            }
            if (nw > 253) return 0;
            w[nw++] = ft.sym[s2];
            s2 = ft.base[s2] + (uint32_t)zs_bread(in, &b, ft.nb[s2]);
            if (b.over) {
                w[nw++] = ft.sym[s1];
                break;
            }
        }
        *used = 1 + hb;
        if (st) st->weights_fse++;
    }
    uint32_t rank[13], total = 0;
    memset(rank, 0, sizeof rank);
    for (uint32_t i = 0; i < nw; i++) {
        if (w[i] > 12) return 0;
        rank[w[i]]++;
        total += (1u << w[i]) >> 1;
    }
    if (total == 0) return 0;
    uint32_t log = zs_highbit(total) + 1;
    if (log > 12) return 0;
    uint32_t rest = (1u << log) - total, rb = zs_highbit(rest);
    if ((1u << rb) != rest) return 0;
    w[nw] = (uint8_t)(rb + 1);
    rank[rb + 1]++;
    if (rank[1] < 2 || (rank[1] & 1)) return 0;
    /* each weight's codes in order, the lowest weight (the longest codes) first */
    uint32_t start[13], cur = 0;
    for (uint32_t k = 1; k <= log; k++) {
        start[k] = cur;
        cur += rank[k] << (k - 1);
    }
    if (cur != 1u << log) return 0;
    for (uint32_t s = 0; s <= nw; s++) {
        uint32_t k = w[s];
        if (!k) continue;
        uint32_t span = 1u << (k - 1);
        for (uint32_t j = 0; j < span; j++) {
            h->sym[start[k] + j] = (uint8_t)s;
            h->nb[start[k] + j] = (uint8_t)(log + 1 - k);
        }
        start[k] += span;
    }
    h->log = log;
    h->ok = 1;
    return 1;
}

/* count literals of the Huffman stream in[a, b), into out: 1 when the stream holds them to
   its last bit */
static int zs_huf_stream(const uint8_t *in, size_t a, size_t b, const zs_huf *h, uint8_t *out, size_t count) {
    zs_back r;
    if (!zs_binit(in, a, b, &r)) return 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t v = zs_bpeek(in, &r, h->log);
        out[i] = h->sym[v];
        zs_bskip(&r, h->nb[v]);
        if (r.over) return 0;
    }
    return r.pos == 0;
}

/* ---- blocks ---- */

/* what a frame keeps from block to block: its last Huffman tree and FSE tables, its window,
   its blocks' bound, where its content begins in the output, its repeat offsets */
typedef struct zs_frame {
    zs_huf huf;
    zs_fse ll, of, ml;
    uint64_t window;
    size_t block_max, start;
    uint64_t rep[3];
} zs_frame;

/* a compressed block's literals section from in[at, end) (RFC 8878, 3.1.1.3.1): its
   literals into lit, the bytes it took in *used. 0, or why not (zs_why) */
static int zs_literals(const uint8_t *in, size_t at, size_t end, zs_frame *fr, vector<uint8_t> *lit, size_t *used, net_zstd_stats *st) {
    size_t n = end - at;
    clear(*lit);
    if (n < 2) return ZS_E_LITERALS;
    uint32_t b0 = in[at], type = b0 & 3, sf = (b0 >> 2) & 3;
    if (type == 0 || type == 1) {
        size_t hs, size;
        if (sf == 0 || sf == 2) {
            hs = 1;
            size = b0 >> 3;
        } else if (sf == 1) {
            hs = 2;
            size = (b0 >> 4) | (uint32_t)in[at + 1] << 4;
        } else {
            if (n < 3) return ZS_E_LITERALS;
            hs = 3;
            size = (b0 >> 4) | (uint32_t)in[at + 1] << 4 | (uint32_t)in[at + 2] << 12;
        }
        if (size > fr->block_max) return ZS_E_LITERALS;
        if (type == 0) {
            if (hs + size > n) return ZS_E_LITERALS;
            append_range(lit, in + at + hs, size);
            *used = hs + size;
            if (st) st->lit_raw++;
        } else {
            if (hs + 1 > n) return ZS_E_LITERALS;
            resize(lit, size);
            if (size) memset(data(*lit), in[at + hs], size);
            *used = hs + 1;
            if (st) st->lit_rle++;
        }
        return ZS_OK;
    }
    if (n < 5) return ZS_E_LITERALS;
    uint32_t lhc = zs_le32(in + at);
    size_t hs, size, csize;
    int one = 0;
    if (sf == 0 || sf == 1) {
        one = sf == 0;
        hs = 3;
        size = (lhc >> 4) & 0x3ff;
        csize = (lhc >> 14) & 0x3ff;
    } else if (sf == 2) {
        hs = 4;
        size = (lhc >> 4) & 0x3fff;
        csize = lhc >> 18;
    } else {
        hs = 5;
        size = (lhc >> 4) & 0x3ffff;
        csize = (lhc >> 22) + ((size_t)in[at + 4] << 10);
    }
    if (size > fr->block_max || (!one && size < 6) || hs + csize > n) return ZS_E_LITERALS;
    size_t s = at + hs, e = at + hs + csize;
    if (type == 2) {
        size_t u = 0;
        if (!zs_huf_read(in, s, e, &fr->huf, &u, st) || u >= csize) return ZS_E_HUFFMAN;
        s += u;
        if (st) st->lit_huffman++;
    } else {
        if (!fr->huf.ok) return ZS_E_TREELESS;
        if (st) st->lit_treeless++;
    }
    resize(lit, size);
    if (one) {
        if (!zs_huf_stream(in, s, e, &fr->huf, data(*lit), size)) return ZS_E_LITERALS;
        if (st) st->lit_one_stream++;
    } else {
        /* four streams: their jump table, three sizes; each a quarter (rounded up) of the
           literals, the fourth the rest */
        if (e - s < 10) return ZS_E_LITERALS;
        size_t l1 = (size_t)in[s] | (size_t)in[s + 1] << 8, l2 = (size_t)in[s + 2] | (size_t)in[s + 3] << 8,
               l3 = (size_t)in[s + 4] | (size_t)in[s + 5] << 8;
        size_t total = e - s - 6, seg = (size + 3) / 4;
        if (l1 + l2 + l3 > total || 3 * seg > size) return ZS_E_LITERALS;
        size_t p1 = s + 6, p2 = p1 + l1, p3 = p2 + l2, p4 = p3 + l3;
        if (!zs_huf_stream(in, p1, p2, &fr->huf, data(*lit), seg) || !zs_huf_stream(in, p2, p3, &fr->huf, data(*lit) + seg, seg) ||
            !zs_huf_stream(in, p3, p4, &fr->huf, data(*lit) + 2 * seg, seg) ||
            !zs_huf_stream(in, p4, e, &fr->huf, data(*lit) + 3 * seg, size - 3 * seg))
            return ZS_E_LITERALS;
        if (st) st->lit_four_streams++;
    }
    *used = hs + csize;
    return ZS_OK;
}

/* a sequences table by its mode (RFC 8878, 3.1.1.3.2.1): predefined, RLE (one symbol), FSE
   compressed (its description at *at), or the last one again; kind 0 literal lengths, 1
   offsets, 2 match lengths */
static int zs_table(const uint8_t *in, size_t *at, size_t end, uint32_t mode, zs_fse *t, uint32_t max_sym, uint32_t max_log,
                    const int16_t *def, uint32_t ndef, uint32_t def_log, net_zstd_stats *st, int kind) {
    if (st) st->seq_mode[kind][mode]++;
    if (mode == 0) return zs_fse_build(t, def, ndef, def_log) ? ZS_OK : ZS_E_FSE;
    if (mode == 1) {
        if (*at >= end || in[*at] > max_sym) return ZS_E_FSE;
        t->sym[0] = in[(*at)++];
        t->nb[0] = 0;
        t->base[0] = 0;
        t->log = 0;
        t->ok = 1;
        return ZS_OK;
    }
    if (mode == 2) {
        int16_t norm[256];
        uint32_t nsym = 0, log = 0;
        size_t u = 0;
        t->ok = 0;
        if (!zs_ncount(in, *at, end, max_sym, max_log, norm, &nsym, &log, &u)) return ZS_E_FSE;
        *at += u;
        return zs_fse_build(t, norm, nsym, log) ? ZS_OK : ZS_E_FSE;
    }
    return t->ok ? ZS_OK : ZS_E_REPEAT_TABLE;
}

/* the literals lit[from, ...) and a match of ml bytes off back appended to out, as a
   sequence executes (RFC 8878, 3.1.2.5) */
static int zs_put(const vector<uint8_t> *lit, size_t from, size_t ll, uint64_t off, size_t ml, zs_frame *fr, size_t block_start,
                  vector<uint8_t> *out, size_t cap, const uint8_t *dict, size_t dn) {
    size_t o = len(*out);
    if (o - block_start + ll + ml > fr->block_max) return ZS_E_BLOCK_SIZE;
    if (o + ll + ml > cap) return ZS_E_CAP;
    if (ml && (off > o - fr->start + ll + dn || (!dn && off > fr->window))) return ZS_E_OFFSET;
    resize(out, o + ll + ml);
    uint8_t *p = data(*out);
    if (ll) memcpy(p + o, data(*lit) + from, ll);
    size_t d = o + ll;
    if (off > d - fr->start) {
        /* back into the dictionary, then on into the frame's content */
        for (size_t i = 0; i < ml; i++) {
            uint64_t at = (uint64_t)(d + i - fr->start);
            p[d + i] = at >= off ? p[fr->start + (size_t)(at - off)] : dict[dn - (size_t)(off - at)];
        }
        return ZS_OK;
    }
    size_t src = d - (size_t)off;
    if (off >= ml) {
        if (ml) memcpy(p + d, p + src, ml);
    } else
        for (size_t i = 0; i < ml; i++) p[d + i] = p[src + i];
    return ZS_OK;
}

/* a compressed block's sequences section from in[at, end) (RFC 8878, 3.1.1.3.2), each
   sequence executed over the block's literals; then the literals left */
static int zs_sequences(const uint8_t *in, size_t at, size_t end, zs_frame *fr, const vector<uint8_t> *lit, vector<uint8_t> *out,
                        size_t cap, net_zstd_stats *st, const uint8_t *dict, size_t dn) {
    size_t nl = len(*lit), lp = 0, block_start = len(*out);
    if (at >= end) return ZS_E_SEQ_HEADER;
    uint32_t nseq = in[at++];
    if (nseq > 127) {
        if (nseq == 255) {
            if (at + 2 > end) return ZS_E_SEQ_HEADER;
            nseq = ((uint32_t)in[at] | (uint32_t)in[at + 1] << 8) + 0x7f00;
            at += 2;
        } else {
            if (at >= end) return ZS_E_SEQ_HEADER;
            nseq = ((nseq - 128) << 8) + in[at++];
        }
    }
    if (nseq == 0) {
        if (at != end) return ZS_E_SEQ_HEADER;
    } else {
        if (at >= end) return ZS_E_SEQ_HEADER;
        uint32_t modes = in[at++];
        if (modes & 3) return ZS_E_SEQ_HEADER;
        int r = zs_table(in, &at, end, modes >> 6, &fr->ll, 35, 9, zs_ll_default, 36, 6, st, 0);
        if (!r) r = zs_table(in, &at, end, (modes >> 4) & 3, &fr->of, 31, 8, zs_of_default, 29, 5, st, 1);
        if (!r) r = zs_table(in, &at, end, (modes >> 2) & 3, &fr->ml, 52, 9, zs_ml_default, 53, 6, st, 2);
        if (r) return r;
        zs_back b;
        if (!zs_binit(in, at, end, &b)) return ZS_E_SEQUENCES;
        uint32_t sl = (uint32_t)zs_bread(in, &b, fr->ll.log), so = (uint32_t)zs_bread(in, &b, fr->of.log),
                 sm = (uint32_t)zs_bread(in, &b, fr->ml.log);
        for (uint32_t k = 0; k < nseq; k++) {
            uint32_t llc = fr->ll.sym[sl], ofc = fr->of.sym[so], mlc = fr->ml.sym[sm];
            /* the extra bits: the offset's, the match length's, the literal length's */
            uint64_t ofv = ((uint64_t)1 << ofc) + zs_bread(in, &b, ofc);
            size_t ml = zs_ml_base[mlc] + (size_t)zs_bread(in, &b, zs_ml_bits[mlc]);
            size_t ll = zs_ll_base[llc] + (size_t)zs_bread(in, &b, zs_ll_bits[llc]);
            uint64_t off;
            if (ofv > 3) {
                off = ofv - 3;
                fr->rep[2] = fr->rep[1];
                fr->rep[1] = fr->rep[0];
                fr->rep[0] = off;
            } else {
                /* a repeat offset: the first, second or third last; with no literals before
                   it, the second, third, or the first less one */
                uint32_t idx = (uint32_t)ofv + (ll == 0);
                if (idx == 1) off = fr->rep[0];
                else if (idx == 2) {
                    off = fr->rep[1];
                    fr->rep[1] = fr->rep[0];
                    fr->rep[0] = off;
                } else {
                    if (idx == 3) off = fr->rep[2];
                    else off = fr->rep[0] - 1;
                    fr->rep[2] = fr->rep[1];
                    fr->rep[1] = fr->rep[0];
                    fr->rep[0] = off;
                }
                if (off == 0) return ZS_E_OFFSET;
                if (st) st->repeat_offsets++;
            }
            /* the states: literal lengths', match lengths', offsets', but after the last */
            if (k + 1 < nseq) {
                sl = fr->ll.base[sl] + (uint32_t)zs_bread(in, &b, fr->ll.nb[sl]);
                sm = fr->ml.base[sm] + (uint32_t)zs_bread(in, &b, fr->ml.nb[sm]);
                so = fr->of.base[so] + (uint32_t)zs_bread(in, &b, fr->of.nb[so]);
            }
            if (b.over) return ZS_E_SEQUENCES;
            if (ll > nl - lp) return ZS_E_SEQUENCES;
            r = zs_put(lit, lp, ll, off, ml, fr, block_start, out, cap, dict, dn);
            if (r) return r;
            lp += ll;
        }
        if (b.pos || b.over) return ZS_E_SEQUENCES;
        if (st) st->sequences += nseq;
    }
    return zs_put(lit, lp, nl - lp, 0, 0, fr, block_start, out, cap, dict, dn);
}

/* the frame at in[*at] (its magic number checked), its blocks decoded into out after the
   raw dictionary rd (rn bytes; none when 0), its window at most wmax; *at past it. The
   dictionary it names and its window in *dict and *window */
static int zs_frame_decode(const uint8_t *in, size_t n, size_t *at, zs_frame *fr, vector<uint8_t> *lit, vector<uint8_t> *out,
                           size_t cap, net_zstd_stats *st, uint32_t *dict, uint64_t *window, const uint8_t *rd, size_t rn, uint64_t wmax) {
    size_t p = *at + 4;
    if (p >= n) return ZS_E_SHORT;
    uint32_t fhd = in[p++];
    if (fhd & 8) return ZS_E_RESERVED;
    uint32_t dflag = fhd & 3, csum = fhd >> 2 & 1, single = fhd >> 5 & 1, fcsf = fhd >> 6;
    uint64_t w = 0;
    if (!single) {
        if (p >= n) return ZS_E_SHORT;
        uint32_t wd = in[p++];
        w = (uint64_t)1 << ((wd >> 3) + 10);
        w += (w >> 3) * (wd & 7);
    }
    size_t dn = dflag;
    if (dflag == 3) dn = 4;
    if (p + dn > n) return ZS_E_SHORT;
    uint32_t did = 0;
    for (size_t i = 0; i < dn; i++) did |= (uint32_t)in[p + i] << (8 * i);
    p += dn;
    size_t fn = single;
    if (fcsf == 1) fn = 2;
    else if (fcsf == 2) fn = 4;
    else if (fcsf == 3) fn = 8;
    if (p + fn > n) return ZS_E_SHORT;
    uint64_t fcs = 0;
    for (size_t i = 0; i < fn; i++) fcs |= (uint64_t)in[p + i] << (8 * i);
    if (fn == 2) fcs += 256;
    p += fn;
    if (single) w = fcs;
    *dict = did;
    *window = w;
    if (did) return ZS_E_DICT;
    if (w > wmax) return ZS_E_WINDOW;
    if (w < 1024) w = 1024;
    fr->window = w;
    fr->block_max = w < ZS_BLOCK_MAX ? (size_t)w : ZS_BLOCK_MAX;
    fr->start = len(*out);
    fr->rep[0] = 1;
    fr->rep[1] = 4;
    fr->rep[2] = 8;
    fr->huf.ok = 0;
    fr->ll.ok = 0;
    fr->of.ok = 0;
    fr->ml.ok = 0;
    if (st) st->frames++;
    for (;;) {
        if (p + 3 > n) return ZS_E_SHORT;
        uint32_t bh = (uint32_t)in[p] | (uint32_t)in[p + 1] << 8 | (uint32_t)in[p + 2] << 16;
        p += 3;
        uint32_t last = bh & 1, type = bh >> 1 & 3;
        size_t bsize = bh >> 3;
        if (type == 3) return ZS_E_BLOCK_TYPE;
        if (type == 1) {
            if (p + 1 > n) return ZS_E_SHORT;
            if (bsize > fr->block_max) return ZS_E_BLOCK_SIZE;
            if (len(*out) + bsize > cap) return ZS_E_CAP;
            size_t o = len(*out);
            resize(out, o + bsize);
            if (bsize) memset(data(*out) + o, in[p], bsize);
            p++;
            if (st) st->rle_blocks++;
        } else {
            if (bsize > n - p) return ZS_E_SHORT;
            if (bsize > fr->block_max) return ZS_E_BLOCK_SIZE;
            if (type == 0) {
                if (len(*out) + bsize > cap) return ZS_E_CAP;
                append_range(out, in + p, bsize);
                if (st) st->raw_blocks++;
            } else {
                size_t lu = 0;
                int r = zs_literals(in, p, p + bsize, fr, lit, &lu, st);
                if (!r) r = zs_sequences(in, p + lu, p + bsize, fr, lit, out, cap, st, rd, rn);
                if (r) return r;
                if (st) st->compressed_blocks++;
            }
            p += bsize;
        }
        if (last) break;
    }
    size_t got = len(*out) - fr->start;
    if ((single || fcsf) && got != fcs) return ZS_E_SIZE;
    if (csum) {
        if (p + 4 > n) return ZS_E_SHORT;
        if ((uint32_t)net_xxh64(data(*out) + fr->start, got, 0) != zs_le32(in + p)) return ZS_E_CHECKSUM;
        p += 4;
        if (st) st->checksums++;
    }
    *at = p;
    return ZS_OK;
}

int net_zstd(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap, char *why, size_t wcap, net_zstd_stats *stats) {
    return net_zstd_dict(in, n, 0, 0, out, cap, why, wcap, stats);
}

int net_zstd_dict(const uint8_t *in, size_t n, const uint8_t *dict, size_t dn, vector<uint8_t> *out, size_t cap, char *why, size_t wcap,
                  net_zstd_stats *stats) {
    /* the window's bound: 8 MB, or with a dictionary 1.25 times it, a power of two, to 128 MB */
    uint64_t wmax = ZS_WINDOW_MAX;
    if (dn) {
        uint64_t want = (uint64_t)dn + dn / 4 + (dn % 4 != 0);
        uint32_t lg = 0;
        while (lg < 63 && ((uint64_t)1 << lg) < want) lg++;
        if (lg < 23) lg = 23;
        if (lg > 27) lg = 27;
        wmax = (uint64_t)1 << lg;
    }
    zs_frame fr;
    memset(&fr, 0, sizeof fr);
    vector<uint8_t> lit;
    size_t at = 0;
    int r = ZS_OK;
    if (n == 0) r = ZS_E_EMPTY;
    while (!r && at < n) {
        if (n - at < 4) {
            r = at ? ZS_E_TRAILING : ZS_E_SHORT;
            break;
        }
        uint32_t magic = zs_le32(in + at);
        if ((magic & 0xfffffff0u) == 0x184d2a50u) {
            /* a skippable frame: its size, and that many bytes passed over */
            if (n - at < 8 || n - at - 8 < zs_le32(in + at + 4)) {
                r = ZS_E_SHORT;
                break;
            }
            at += 8 + (size_t)zs_le32(in + at + 4);
            if (stats) stats->skippable++;
            continue;
        }
        if (magic != ZS_MAGIC) {
            r = at ? ZS_E_TRAILING : ZS_E_MAGIC;
            break;
        }
        uint32_t did = 0;
        uint64_t window = 0;
        r = zs_frame_decode(in, n, &at, &fr, &lit, out, cap, stats, &did, &window, dict, dn, wmax);
        if (r == ZS_E_DICT) {
            snprintf(why, wcap, "the zstd frame needs dictionary %u, and none is used here (nor in Chromium)", did);
            return 0;
        }
        if (r == ZS_E_WINDOW) {
            if (dn) snprintf(why, wcap, "the zstd frame's window is %llu bytes, past the %llu its dictionary allows", (unsigned long long)window,
                             (unsigned long long)wmax);
            else snprintf(why, wcap, "the zstd frame's window is %llu bytes, past the 8 MB HTTP's zstd allows", (unsigned long long)window);
            return 0;
        }
    }
    if (r == ZS_E_CAP) {
        snprintf(why, wcap, "the decoded body passed %zu bytes", cap);
        return -1;
    }
    if (r) {
        snprintf(why, wcap, "%s", zs_why[r]);
        return 0;
    }
    return 1;
}
