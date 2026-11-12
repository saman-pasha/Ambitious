/* src/paint/brotli.c -- Brotli (RFC 7932) decoded (brotli.h): a WOFF2 font's tables, whole,
   into a vector.

   A stream is its window's size, then meta-blocks: each uncompressed (its bytes as they
   are), metadata (skipped) or compressed. A compressed one holds, for each of its three
   kinds of symbols -- literals, commands and distances -- its block types and their
   lengths, then the literals' context modes, the context maps from a literal's or a
   distance's context to its prefix code, and the prefix codes themselves (simple, of up
   to four symbols, or complex: their code lengths prefix-coded in turn, with runs); then
   commands, each some literals and a copy: from a distance back, or, past the window, a
   word of the static dictionary transformed (brotli_dict.h: the RFC's dictionary, its
   transforms and its context tables, tools/gen-brotli.py). A prefix code is canonical and
   decoded over its lengths' counts, a bit at a time (as zlib's puff decodes deflate's).

   The stream is checked as the reference decoder checks it (c/dec/decode.c): a code whose
   lengths do not fill its space, a repeated symbol, a length past its meta-block, a
   distance before the start or a word or transform the dictionary has not, padding that
   is not 0, a nibble or a byte more than a length needs, and a read past the input each
   fail the whole stream; nothing outside the input is read, nothing past cap written. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "brotli.h"
#include "brotli_dict.h"

/* ---- the input, a bit at a time, its first bit the lowest ---- */

/* the input's size, the next bit's place (in bits from its start), whether a read went
   past its end */
typedef struct amb_bits { size_t n, at; int bad; } amb_bits;

/* the next k bits (at most 24) as a number, the first of them its lowest bit; 0 past the
   end (bad then set) */
static uint32_t amb_bget(const uint8_t *in, amb_bits *b, uint32_t k) {
    if (k == 0) return 0;
    if (b->at + k > 8 * b->n) {
        b->bad = 1;
        b->at = 8 * b->n;
        return 0;
    }
    size_t byte = b->at >> 3;
    uint64_t w = 0;
    for (uint32_t i = 0; i < 5 && byte + i < b->n; i++) w |= (uint64_t)in[byte + i] << (8 * i);
    uint32_t v = (uint32_t)(w >> (b->at & 7)) & (((uint32_t)1 << k) - 1);
    b->at += k;
    return v;
}

/* the next 15 bits, as many as there are (0 past the end), not taken */
static uint32_t amb_bpeek(const uint8_t *in, const amb_bits *b) {
    size_t byte = b->at >> 3;
    uint32_t w = 0;
    for (uint32_t i = 0; i < 3 && byte + i < b->n; i++) w |= (uint32_t)in[byte + i] << (8 * i);
    return (w >> (b->at & 7)) & 0x7fff;
}

/* k bits taken (past the end: bad) */
static void amb_bskip(amb_bits *b, uint32_t k) {
    if (b->at + k > 8 * b->n) {
        b->bad = 1;
        b->at = 8 * b->n;
    } else b->at += k;
}

/* to the next byte's start: the bits passed must all be 0 */
static void amb_balign(const uint8_t *in, amb_bits *b) {
    uint32_t k = (uint32_t)((8 - (b->at & 7)) & 7);
    if (amb_bget(in, b, k) != 0) b->bad = 1;
}

/* a number of 0 to 255 in 1, 4 or up to 11 bits (section 9.2: NBLTYPES, NTREES) */
static uint32_t amb_bvar8(const uint8_t *in, amb_bits *b) {
    if (!amb_bget(in, b, 1)) return 0;
    uint32_t k = amb_bget(in, b, 3);
    if (k == 0) return 1;
    return ((uint32_t)1 << k) + amb_bget(in, b, k);
}

/* ---- prefix codes ---- */

/* a canonical prefix code: where its symbols are in the pool (sorted by their codes'
   lengths, then by symbol), how many there are, and how many of each length; a code of
   one symbol takes no bits */
typedef struct amb_hcode { uint32_t first, nsym; uint16_t count[16]; } amb_hcode;

/* a code of the one symbol s pushed to codes */
static void amb_hone(uint32_t s, vector<amb_hcode> *codes, vector<uint16_t> *pool) {
    amb_hcode c;
    memset(&c, 0, sizeof c);
    c.first = (uint32_t)len(*pool);
    c.nsym = 1;
    push_back(pool, (uint16_t)s);
    push_back(codes, c);
}

/* the code of the symbols' lengths (lens: 0 for none, of an alphabet of n) pushed to
   codes */
static void amb_hmake(const uint8_t *lens, uint32_t n, vector<amb_hcode> *codes, vector<uint16_t> *pool) {
    amb_hcode c;
    memset(&c, 0, sizeof c);
    c.first = (uint32_t)len(*pool);
    uint32_t offs[16];
    for (uint32_t s = 0; s < n; s++)
        if (lens[s]) c.count[lens[s]]++;
    uint32_t total = 0;
    offs[0] = 0;
    for (uint32_t l = 1; l < 16; l++) {
        offs[l] = total;
        total += c.count[l];
    }
    c.nsym = total;
    resize(pool, c.first + total);
    for (uint32_t s = 0; s < n; s++)
        if (lens[s]) (*pool)[c.first + offs[lens[s]]++] = (uint16_t)s;
    push_back(codes, c);
}

/* the next symbol of code k: its code's bits read, the first of them its highest */
static uint32_t amb_hsym(const uint8_t *in, amb_bits *b, const vector<amb_hcode> *codes, uint32_t k, const vector<uint16_t> *pool) {
    amb_hcode c = at(*codes, k);
    if (c.nsym == 1) return at(*pool, c.first);
    uint32_t peek = amb_bpeek(in, b);
    int32_t code = 0, first = 0, index = 0;
    for (uint32_t l = 1; l < 16; l++) {
        code |= (int32_t)(peek >> (l - 1) & 1);
        int32_t count = c.count[l];
        if (code - first < count) {
            amb_bskip(b, l);
            return at(*pool, c.first + (uint32_t)(index + code - first));
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    b->bad = 1;
    return 0;
}

/* a prefix code of an alphabet of n symbols (at most 704) read, simple or complex (section
   3.4, 3.5), and pushed to codes: 1, or 0 when it is none the format allows */
static int amb_hread(const uint8_t *in, amb_bits *b, uint32_t n, vector<amb_hcode> *codes, vector<uint16_t> *pool) {
    uint8_t lens[704];
    if (n < 1 || n > 704) return 0;
    memset(lens, 0, sizeof lens);
    uint32_t hskip = amb_bget(in, b, 2);
    if (hskip == 1) {
        /* simple: up to four symbols of ALPHABET_BITS each, their lengths the shape's */
        uint32_t nsym = amb_bget(in, b, 2) + 1, nbits = 0, s[4];
        while (((uint32_t)1 << nbits) < n) nbits++;
        for (uint32_t k = 0; k < nsym; k++) {
            s[k] = amb_bget(in, b, nbits);
            if (s[k] >= n) return 0;
            for (uint32_t j = 0; j < k; j++)
                if (s[j] == s[k]) return 0;
        }
        if (b->bad) return 0;
        if (nsym == 1) {
            amb_hone(s[0], codes, pool);
            return 1;
        }
        if (nsym == 2) { lens[s[0]] = 1; lens[s[1]] = 1; }
        else if (nsym == 3) { lens[s[0]] = 1; lens[s[1]] = 2; lens[s[2]] = 2; }
        else if (!amb_bget(in, b, 1)) { lens[s[0]] = 2; lens[s[1]] = 2; lens[s[2]] = 2; lens[s[3]] = 2; }
        else { lens[s[0]] = 1; lens[s[1]] = 2; lens[s[2]] = 3; lens[s[3]] = 3; }
        amb_hmake(lens, n, codes, pool);
        return !b->bad;
    }
    /* complex: the code lengths' code (its own lengths in a fixed code, from the HSKIP-th
       of the order), till its space is filled */
    static const uint8_t order[18] = { 1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const uint8_t plen[16] = { 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4 };
    static const uint8_t pval[16] = { 0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5 };
    uint8_t cl[18];
    memset(cl, 0, sizeof cl);
    uint32_t space = 32, num = 0, one = 0;
    for (uint32_t i = hskip; i < 18; i++) {
        uint32_t ix = amb_bpeek(in, b) & 15;
        amb_bskip(b, plen[ix]);
        uint32_t v = pval[ix];
        cl[order[i]] = (uint8_t)v;
        if (v) {
            space -= 32u >> v;
            num++;
            one = order[i];
            if (space - 1u >= 32u) break;                 /* filled, or past it */
        }
    }
    if (b->bad || !(num == 1 || space == 0)) return 0;
    vector<amb_hcode> clc;
    vector<uint16_t> clp;
    if (num == 1) amb_hone(one, &clc, &clp);
    else amb_hmake(cl, 18, &clc, &clp);
    /* the symbols' lengths: 0 to 15, or runs -- 16 of the last length not 0, 17 of 0s --
       a run after a run of its kind extending it (section 3.5) */
    uint32_t sym = 0, prev = 8, repeat = 0, rlen = 0;
    int32_t room = 32768;
    while (sym < n && room > 0) {
        uint32_t c = amb_hsym(in, b, &clc, 0, &clp);
        if (b->bad) return 0;
        if (c < 16) {
            repeat = 0;
            if (c) {
                lens[sym] = (uint8_t)c;
                prev = c;
                room -= 32768 >> c;
            }
            sym++;
            continue;
        }
        uint32_t extra = c == 16 ? 2 : 3, nl = c == 16 ? prev : 0, delta = amb_bget(in, b, extra);
        if (rlen != nl) {
            repeat = 0;
            rlen = nl;
        }
        uint32_t old = repeat;
        if (repeat > 0) {
            repeat -= 2;
            repeat <<= extra;
        }
        repeat += delta + 3;
        delta = repeat - old;
        if (sym + delta > n) return 0;
        for (uint32_t k = 0; k < delta; k++) lens[sym++] = (uint8_t)rlen;
        if (rlen) room -= (int32_t)(delta << (15 - rlen));
    }
    if (room != 0 || b->bad) return 0;
    amb_hmake(lens, n, codes, pool);
    return 1;
}

/* a context map of size entries (section 7.3): how many codes it names, and each
   context's, run-length coded and, at its end, perhaps moved to front. 1, or 0 */
static int amb_cmap(const uint8_t *in, amb_bits *b, uint32_t size, vector<uint8_t> *map, uint32_t *ntrees) {
    *ntrees = amb_bvar8(in, b) + 1;
    clear(*map);
    resize(map, size);
    for (uint32_t i = 0; i < size; i++) (*map)[i] = 0;
    if (*ntrees < 2) return !b->bad;
    uint32_t rlemax = 0;
    if (amb_bget(in, b, 1)) rlemax = amb_bget(in, b, 4) + 1;
    vector<amb_hcode> c;
    vector<uint16_t> p;
    if (!amb_hread(in, b, *ntrees + rlemax, &c, &p)) return 0;
    uint32_t i = 0;
    while (i < size) {
        uint32_t code = amb_hsym(in, b, &c, 0, &p);
        if (b->bad) return 0;
        if (code == 0) {
            (*map)[i++] = 0;
            continue;
        }
        if (code > rlemax) {
            (*map)[i++] = (uint8_t)(code - rlemax);
            continue;
        }
        uint32_t reps = ((uint32_t)1 << code) + amb_bget(in, b, code);
        if (reps > size - i) return 0;
        for (uint32_t k = 0; k < reps; k++) (*map)[i++] = 0;
    }
    if (amb_bget(in, b, 1)) {
        /* the inverse move-to-front transform */
        uint8_t mtf[256];
        for (uint32_t k = 0; k < 256; k++) mtf[k] = (uint8_t)k;
        for (uint32_t k = 0; k < size; k++) {
            uint32_t idx = at(*map, k);
            uint8_t v = mtf[idx];
            (*map)[k] = v;
            for (uint32_t j = idx; j > 0; j--) mtf[j] = mtf[j - 1];
            mtf[0] = v;
        }
    }
    return !b->bad;
}

/* ---- the static dictionary ---- */

/* its words, decoded from brotli_dict.h's base64 when a stream first needs them, once a
   process (dropped when it exits) */
static vector<uint8_t> amb_bdict;
static int amb_bdict_kept;

static void amb_bdict_drop(void) { vector_uint8_t_drop(&amb_bdict); }

static int amb_bdict_ready(void) {
    if (amb_bdict_kept) return len(amb_bdict) == AMB_BROTLI_DICT_SIZE;
    amb_bdict_kept = 1;
    atexit(amb_bdict_drop);
    uint8_t six[256];
    for (int c = 0; c < 256; c++) six[c] = 64;
    for (int c = 0; c < 26; c++) { six['A' + c] = (uint8_t)c; six['a' + c] = (uint8_t)(26 + c); }
    for (int c = 0; c < 10; c++) six['0' + c] = (uint8_t)(52 + c);
    six['+'] = 62;
    six['/'] = 63;
    uint32_t acc = 0;
    int bits = 0;
    for (uint32_t p = 0; p < AMB_BROTLI_DICT_PIECES; p++)
        for (size_t i = 0; amb_brotli_dict[p][i]; i++) {
            uint32_t v = six[(uint8_t)amb_brotli_dict[p][i]];
            if (v == 64) continue;
            acc = acc << 6 | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                push_back(amb_bdict, (uint8_t)(acc >> bits));
            }
        }
    return len(amb_bdict) == AMB_BROTLI_DICT_SIZE;
}

/* a byte's upper case as the reference makes it, at dst[i] (UTF-8's, simplified): how
   many bytes it took */
static uint32_t amb_bupper(uint8_t *dst, size_t i) {
    if (dst[i] < 0xC0) {
        if (dst[i] >= 'a' && dst[i] <= 'z') dst[i] ^= 32;
        return 1;
    }
    if (dst[i] < 0xE0) {
        dst[i + 1] ^= 32;
        return 2;
    }
    dst[i + 2] ^= 5;
    return 3;
}

/* the dictionary's word of length n at w, transformed by transform t (Appendix B: its
   prefix, its word less some first or last bytes, perhaps upper-cased, its suffix) into
   dst (at least 64 bytes): its length */
static size_t amb_btransform(uint8_t *dst, size_t w, int32_t n, uint32_t t) {
    uint32_t po = amb_brotli_transform[5 * t], pl = amb_brotli_transform[5 * t + 1], op = amb_brotli_transform[5 * t + 2];
    uint32_t so = amb_brotli_transform[5 * t + 3], sl = amb_brotli_transform[5 * t + 4];
    size_t idx = 0;
    for (uint32_t k = 0; k < pl; k++) dst[idx++] = amb_brotli_affix[po + k];
    int32_t skip = 0;
    if (op <= 9) n -= (int32_t)op;
    else if (op >= 12 && op <= 20) {
        skip = (int32_t)op - 11;
        n -= skip;
    }
    for (int32_t i = 0; i < n; i++) dst[idx++] = at(amb_bdict, w + (size_t)(skip + i));
    if (op == 10 && n > 0) amb_bupper(dst, idx - (size_t)n);
    else if (op == 11) {
        size_t u = idx - (size_t)(n > 0 ? n : 0);
        while (n > 0) {
            uint32_t step = amb_bupper(dst, u);
            u += step;
            n -= (int32_t)step;
        }
    }
    for (uint32_t k = 0; k < sl; k++) dst[idx++] = amb_brotli_affix[so + k];
    return idx;
}

/* ---- meta-blocks ---- */

/* the distances codes 4 to 15 take from the last or the one before: less or more by */
static const int8_t amb_bnear[6] = { -1, 1, -2, 2, -3, 3 };

/* category i's block switch (section 6): its next block type -- 0 the one before the
   last, 1 the last's next, else the symbol less 2 -- and its next block's length */
static void amb_bswitch(const uint8_t *in, amb_bits *b, const vector<amb_hcode> *codes, const vector<uint16_t> *pool, uint32_t i,
                        const uint32_t *nbl, const uint32_t *ttree, const uint32_t *ctree, uint32_t *brb, uint32_t *btype, uint32_t *blen) {
    if (nbl[i] < 2) {                                   /* one type: its block never ends in a meta-block */
        b->bad = 1;
        return;
    }
    uint32_t t = amb_hsym(in, b, codes, ttree[i], pool);
    if (t == 1) t = brb[2 * i + 1] + 1;
    else if (t == 0) t = brb[2 * i];
    else t -= 2;
    if (t >= nbl[i]) t -= nbl[i];
    brb[2 * i] = brb[2 * i + 1];
    brb[2 * i + 1] = t;
    btype[i] = t;
    uint32_t s = amb_hsym(in, b, codes, ctree[i], pool);
    blen[i] = s < 26 ? amb_brotli_block_base[s] + amb_bget(in, b, amb_brotli_block_extra[s]) : 0;
}

/* a compressed meta-block of mlen bytes, its header from its block types on: its bytes
   appended to out (the stream's from start, its last four distances in last). 1, or 0 */
static int amb_bmeta(const uint8_t *in, amb_bits *b, vector<uint8_t> *out, size_t start, size_t mlen, size_t cap, size_t maxback,
                     int64_t *last) {
    vector<amb_hcode> codes;
    vector<uint16_t> pool;
    uint32_t nbl[3], ttree[3], ctree[3], brb[6], btype[3], blen[3];
    for (uint32_t i = 0; i < 3; i++) {
        nbl[i] = amb_bvar8(in, b) + 1;
        brb[2 * i] = 1;
        brb[2 * i + 1] = 0;
        btype[i] = 0;
        ttree[i] = ctree[i] = 0;
        blen[i] = (uint32_t)1 << 24;
        if (nbl[i] >= 2) {
            ttree[i] = (uint32_t)len(codes);
            if (!amb_hread(in, b, nbl[i] + 2, &codes, &pool)) return 0;
            ctree[i] = (uint32_t)len(codes);
            if (!amb_hread(in, b, 26, &codes, &pool)) return 0;
            uint32_t s = amb_hsym(in, b, &codes, ctree[i], &pool);
            blen[i] = amb_brotli_block_base[s] + amb_bget(in, b, amb_brotli_block_extra[s]);
        }
    }
    uint32_t pd = amb_bget(in, b, 6), npostfix = pd & 3, ndirect = (pd >> 2) << npostfix;
    vector<uint8_t> modes, cmapl, cmapd;
    for (uint32_t k = 0; k < nbl[0]; k++) push_back(modes, (uint8_t)amb_bget(in, b, 2));
    uint32_t ntreesl, ntreesd;
    if (b->bad || !amb_cmap(in, b, nbl[0] << 6, &cmapl, &ntreesl) || !amb_cmap(in, b, nbl[2] << 2, &cmapd, &ntreesd)) return 0;
    /* the codes: the literals', the commands', the distances' */
    uint32_t lit0 = (uint32_t)len(codes), cmd0, dist0, dalpha = 16 + ndirect + (48u << npostfix);
    for (uint32_t k = 0; k < ntreesl; k++)
        if (!amb_hread(in, b, 256, &codes, &pool)) return 0;
    cmd0 = (uint32_t)len(codes);
    for (uint32_t k = 0; k < nbl[1]; k++)
        if (!amb_hread(in, b, 704, &codes, &pool)) return 0;
    dist0 = (uint32_t)len(codes);
    for (uint32_t k = 0; k < ntreesd; k++)
        if (!amb_hread(in, b, dalpha, &codes, &pool)) return 0;
    /* the commands, till the meta-block's bytes are all there */
    size_t end = len(*out) + mlen;
    if (end - start > cap) return 0;
    while (len(*out) < end) {
        if (blen[1] == 0) amb_bswitch(in, b, &codes, &pool, 1, nbl, ttree, ctree, brb, btype, blen);
        blen[1]--;
        uint32_t cmd = amb_hsym(in, b, &codes, cmd0 + btype[1], &pool);
        if (b->bad || cmd >= 704) return 0;
        uint32_t icode = amb_brotli_ins_cell[cmd >> 6] + (cmd >> 3 & 7), ccode = amb_brotli_copy_cell[cmd >> 6] + (cmd & 7);
        size_t insert = amb_brotli_ins_base[icode] + amb_bget(in, b, amb_brotli_ins_extra[icode]);
        size_t copy = amb_brotli_copy_base[ccode] + amb_bget(in, b, amb_brotli_copy_extra[ccode]);
        if (insert > end - len(*out)) return 0;
        uint32_t mode = at(modes, btype[0]);
        for (size_t k = 0; k < insert; k++) {
            if (blen[0] == 0) {
                amb_bswitch(in, b, &codes, &pool, 0, nbl, ttree, ctree, brb, btype, blen);
                mode = at(modes, btype[0]);
            }
            blen[0]--;
            size_t pos = len(*out) - start;
            uint32_t p1 = pos >= 1 ? at(*out, len(*out) - 1) : 0, p2 = pos >= 2 ? at(*out, len(*out) - 2) : 0;
            uint32_t ctx = amb_brotli_context[512 * mode + p1] | amb_brotli_context[512 * mode + 256 + p2];
            uint32_t tree = at(cmapl, (btype[0] << 6) + ctx);
            if (tree >= ntreesl) return 0;
            push_back(*out, (uint8_t)amb_hsym(in, b, &codes, lit0 + tree, &pool));
            if (b->bad) return 0;
        }
        if (len(*out) == end) break;                     /* the meta-block ends with the literals */
        /* the distance: the last one (the first two cells' commands), or read */
        int64_t d;
        uint32_t dc = 0;
        if (cmd < 128) d = last[0];
        else {
            if (blen[2] == 0) amb_bswitch(in, b, &codes, &pool, 2, nbl, ttree, ctree, brb, btype, blen);
            blen[2]--;
            uint32_t tree = at(cmapd, (btype[2] << 2) + (copy > 4 ? 3 : (uint32_t)copy - 2));
            if (tree >= ntreesd) return 0;
            dc = amb_hsym(in, b, &codes, dist0 + tree, &pool);
            if (dc < 4) d = last[dc];
            else if (dc < 10) d = last[0] + amb_bnear[dc - 4];
            else if (dc < 16) d = last[1] + amb_bnear[dc - 10];
            else if (dc < 16 + ndirect) d = dc - 15;
            else {
                uint32_t x = dc - ndirect - 16, nbits = 1 + (x >> (npostfix + 1));
                int64_t offset = ((int64_t)(2 + (x >> npostfix & 1)) << nbits) - 4;
                d = ((offset + amb_bget(in, b, nbits)) << npostfix) + (x & ((1u << npostfix) - 1)) + ndirect + 1;
            }
        }
        if (b->bad || d <= 0) return 0;
        size_t pos = len(*out) - start, maxdist = pos < maxback ? pos : maxback;
        if ((uint64_t)d > maxdist) {
            /* past the window: a word of the dictionary, transformed */
            if (d > 0x7FFFFFFC || copy < 4 || copy > 24 || !amb_brotli_word_bits[copy] || !amb_bdict_ready()) return 0;
            uint64_t id = (uint64_t)d - maxdist - 1;
            uint32_t wbits = amb_brotli_word_bits[copy];
            uint64_t t = id >> wbits;
            if (t >= 121) return 0;
            uint8_t word[64];
            size_t wn = amb_btransform(word, amb_brotli_word_at[copy] + (size_t)(id & ((1u << wbits) - 1)) * copy, (int32_t)copy, (uint32_t)t);
            if (wn == 0 && d <= 120) return 0;
            if (wn > end - len(*out)) return 0;
            append_range(*out, word, wn);
        } else {
            if (copy > end - len(*out)) return 0;
            /* the last distances: this one first, but the last one again (code 0) */
            if (dc != 0) {
                last[3] = last[2];
                last[2] = last[1];
                last[1] = last[0];
                last[0] = d;
            }
            size_t from = len(*out) - (size_t)d;
            for (size_t k = 0; k < copy; k++) push_back(*out, at(*out, from + k));
        }
    }
    return !b->bad;
}

int amb_brotli(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap) {
    amb_bits b;
    b.n = n;
    b.at = 0;
    b.bad = 0;
    size_t start = len(*out);
    /* the window: 16, or 17 to 24, or 10 to 15 bits (section 9.1) */
    uint32_t wbits = 16;
    if (amb_bget(in, &b, 1)) {
        uint32_t k = amb_bget(in, &b, 3);
        if (k) wbits = 17 + k;
        else {
            k = amb_bget(in, &b, 3);
            if (k == 1) return 0;
            wbits = k ? 8 + k : 17;
        }
    }
    size_t maxback = ((size_t)1 << wbits) - 16;
    int64_t last[4] = { 4, 11, 15, 16 };
    for (;;) {
        if (b.bad) return 0;
        uint32_t islast = amb_bget(in, &b, 1);
        if (islast && amb_bget(in, &b, 1)) break;        /* ISLASTEMPTY */
        uint32_t nib = amb_bget(in, &b, 2);
        if (nib == 3) {
            /* metadata: its length in up to three bytes, skipped */
            if (amb_bget(in, &b, 1)) return 0;
            uint32_t nbytes = amb_bget(in, &b, 2);
            size_t skip = 0;
            for (uint32_t i = 0; i < nbytes; i++) {
                uint32_t v = amb_bget(in, &b, 8);
                if (i + 1 == nbytes && nbytes > 1 && v == 0) return 0;
                skip |= (size_t)v << (8 * i);
            }
            if (nbytes) skip++;
            amb_balign(in, &b);
            if (b.bad || skip > n - b.at / 8) return 0;
            b.at += 8 * skip;
            if (islast) break;
            continue;
        }
        size_t mlen = 0;
        for (uint32_t i = 0; i < nib + 4; i++) {
            uint32_t v = amb_bget(in, &b, 4);
            if (i + 1 == nib + 4 && nib > 0 && v == 0) return 0;
            mlen |= (size_t)v << (4 * i);
        }
        mlen++;
        if (len(*out) - start + mlen > cap) return 0;
        if (!islast && amb_bget(in, &b, 1)) {
            /* uncompressed: its bytes as they are, from the next byte on */
            amb_balign(in, &b);
            if (b.bad || mlen > n - b.at / 8) return 0;
            append_range(*out, in + b.at / 8, mlen);
            b.at += 8 * mlen;
        } else if (!amb_bmeta(in, &b, out, start, mlen, cap, maxback, last)) return 0;
        if (islast) break;
    }
    amb_balign(in, &b);
    return !b.bad;
}
