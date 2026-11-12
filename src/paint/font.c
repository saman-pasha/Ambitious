/* src/paint/font.c -- the fonts (font.h): TrueType read, measured, and its glyphs
   drawn as coverage masks.

   One reader serves every face. The built-in faces' twenty fonts are TrueType subsets
   of the Liberation and DejaVu fonts (font_data.h, tools/gen-font.py), packed (LZ77 in
   LZ4's byte-aligned tokens, unpacked at a few instructions a byte) and base64. Each is
   decoded, unpacked and read the first time a face of it is asked for,
   once a process, into tables every amb_fonts shares (the static faces: freed when the
   process exits); DejaVu Sans's and DejaVu Serif's italic styles are their upright
   fonts again, which amb_glyph_mask slants. Each face falls back for a code point it
   has no glyph for as Chromium's does (font.h). A page's web font is read
   when it is added: its sfnt's tables found (a WOFF 1 file's rebuilt first), its
   code points from cmap (format 4 or 12), advances from hmtx, metrics from head,
   hhea and OS/2, kerning pairs from kern (format 0); its outlines (glyf, through
   loca) are read when a glyph is drawn. OpenType with CFF outlines is no font this
   reads.

   Metrics are rounded as Chromium rounds them (SimpleFontData): the ascent, the
   descent and the line gap each to a whole px, a line's normal height their sum.
   Widths are the advances and the kerning pairs of each two code points, summed
   in font units and scaled once. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include "font.h"
#include "font_data.h"

/* ---- reading big-endian bytes, every read inside the face's n bytes ---- */

static uint32_t amb_u16(const uint8_t *p, size_t n, size_t off) { return off + 2 <= n ? (uint32_t)p[off] << 8 | p[off + 1] : 0; }
static int32_t amb_s16(const uint8_t *p, size_t n, size_t off) { return (int32_t)(int16_t)amb_u16(p, n, off); }
static uint32_t amb_u32(const uint8_t *p, size_t n, size_t off) {
    return off + 4 <= n ? (uint32_t)p[off] << 24 | (uint32_t)p[off + 1] << 16 | (uint32_t)p[off + 2] << 8 | p[off + 3] : 0;
}
static int amb_tag(const uint8_t *p, size_t n, size_t off, const char *t) {
    return off + 4 <= n && p[off] == (uint8_t)t[0] && p[off + 1] == (uint8_t)t[1] && p[off + 2] == (uint8_t)t[2] && p[off + 3] == (uint8_t)t[3];
}

/* ---- the static faces: read once a process, shared by every amb_fonts ---- */

/* their fonts' bytes, code points and kerning pairs, each face's from its record's first
   and kfirst, as a web font's are in its amb_fonts; the records, by face; each built-in
   font's state: 0 not read yet, 1 read */
static vector<uint8_t> amb_sb;
static vector<amb_glyph> amb_sg;
static vector<amb_kern> amb_sk;
static amb_face amb_sf[AMB_NFACES];
static uint8_t amb_font_state[AMB_FONT_FACES];
static int amb_s_kept;

/* the static faces' tables dropped when the process exits */
static void amb_static_drop(void) {
    vector_uint8_t_drop(&amb_sb);
    vector_amb_glyph_drop(&amb_sg);
    vector_amb_kern_drop(&amb_sk);
}

/* each base64 character's six bits; 64 for none (the padding) */
static uint8_t amb_b64[256];

/* a built-in font's base64 (its pieces of amb_font_b64, from first to last) decoded into
   out's n bytes: 1, or 0 when they are not n bytes */
static int amb_unbase64(uint32_t first, uint32_t last, uint8_t *out, size_t n) {
    if (!amb_b64['B']) {
        for (int c = 0; c < 256; c++) amb_b64[c] = 64;
        for (int c = 0; c < 26; c++) { amb_b64['A' + c] = (uint8_t)c; amb_b64['a' + c] = (uint8_t)(26 + c); }
        for (int c = 0; c < 10; c++) amb_b64['0' + c] = (uint8_t)(52 + c);
        amb_b64['+'] = 62;
        amb_b64['/'] = 63;
    }
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (uint32_t p = first; p < last; p++) {
        size_t i = 0;
        /* four characters, three bytes, while the piece has four more that are base64 */
        for (;;) {
            uint32_t a = amb_b64[(uint8_t)amb_font_b64[p][i]], b = a < 64 ? amb_b64[(uint8_t)amb_font_b64[p][i + 1]] : 64;
            uint32_t c = b < 64 ? amb_b64[(uint8_t)amb_font_b64[p][i + 2]] : 64, d = c < 64 ? amb_b64[(uint8_t)amb_font_b64[p][i + 3]] : 64;
            if (d == 64 || bits || n - o < 3) break;
            uint32_t v = a << 18 | b << 12 | c << 6 | d;
            out[o] = (uint8_t)(v >> 16);
            out[o + 1] = (uint8_t)(v >> 8);
            out[o + 2] = (uint8_t)v;
            o += 3;
            i += 4;
        }
        /* the rest, a character at a time */
        for (; amb_font_b64[p][i]; i++) {
            uint32_t v = amb_b64[(uint8_t)amb_font_b64[p][i]];
            if (v == 64) continue;
            acc = acc << 6 | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (o == n) return 0;
                out[o++] = (uint8_t)(acc >> bits);
            }
        }
    }
    return o == n;
}

/* bytes tools/gen-font.py's lz_pack packed (LZ77 in LZ4's tokens) unpacked into out's n
   bytes: 1, or 0 when they are not lz_pack's of n bytes */
static int amb_unlz(const uint8_t *z, size_t zn, uint8_t *out, size_t n) {
    size_t i = 0, o = 0;
    while (i < zn) {
        uint32_t tok = z[i++];
        size_t ll = tok >> 4;
        if (ll == 15) {
            while (i < zn && z[i] == 255) { ll += 255; i++; }
            if (i == zn) return 0;
            ll += z[i++];
        }
        if (ll > zn - i || ll > n - o) return 0;
        memcpy(out + o, z + i, ll);
        o += ll;
        i += ll;
        if (i == zn) break;
        if (zn - i < 2) return 0;
        size_t off = (size_t)z[i] | (size_t)z[i + 1] << 8, ml = (tok & 15) + 4;
        i += 2;
        if ((tok & 15) == 15) {
            while (i < zn && z[i] == 255) { ml += 255; i++; }
            if (i == zn) return 0;
            ml += z[i++];
        }
        if (off == 0 || off > o || ml > n - o) return 0;
        if (off >= ml) {
            memcpy(out + o, out + o - off, ml);
            o += ml;
        } else
            for (size_t k = 0; k < ml; k++, o++) out[o] = out[o - off];
    }
    return o == n;
}

/* ---- an sfnt read as a face ---- */

typedef struct amb_tables { uint32_t cmap, head, hhea, hmtx, maxp, loca, glyf, glyf_len, kern, kern_len, os2, os2_len; } amb_tables;

static int amb_sfnt_tables(const uint8_t *p, size_t n, amb_tables *t) {
    memset(t, 0, sizeof *t);
    uint32_t v = amb_u32(p, n, 0);
    if (v != 0x00010000 && !amb_tag(p, n, 0, "true")) return 0;      /* 'OTTO' (CFF outlines) is not read */
    uint32_t nt = amb_u16(p, n, 4);
    for (uint32_t i = 0; i < nt; i++) {
        size_t r = 12 + 16 * (size_t)i;
        uint32_t off = amb_u32(p, n, r + 8), len = amb_u32(p, n, r + 12);
        if ((size_t)off + len > n) return 0;
        if (amb_tag(p, n, r, "cmap")) t->cmap = off;
        else if (amb_tag(p, n, r, "head")) t->head = off;
        else if (amb_tag(p, n, r, "hhea")) t->hhea = off;
        else if (amb_tag(p, n, r, "hmtx")) t->hmtx = off;
        else if (amb_tag(p, n, r, "maxp")) t->maxp = off;
        else if (amb_tag(p, n, r, "loca")) t->loca = off;
        else if (amb_tag(p, n, r, "glyf")) { t->glyf = off; t->glyf_len = len; }
        else if (amb_tag(p, n, r, "kern")) { t->kern = off; t->kern_len = len; }
        else if (amb_tag(p, n, r, "OS/2")) { t->os2 = off; t->os2_len = len; }
    }
    return t->cmap && t->head && t->hhea && t->hmtx && t->maxp && t->loca && t->glyf;
}

/* a code point's glyph made one of the face's, its advance from hmtx */
static void amb_map(vector<amb_glyph> *gl, const uint8_t *p, size_t n, const amb_tables *t, uint32_t nhm, uint32_t nglyphs,
                    uint32_t cp, uint32_t gid) {
    if (gid == 0 || gid >= nglyphs) return;
    amb_glyph g;
    g.cp = cp;
    g.gid = gid;
    g.advance = (uint16_t)amb_u16(p, n, t->hmtx + 4 * (size_t)(gid < nhm ? gid : nhm - 1));
    g.kcount = 0;
    g.kfirst = 0;
    push_back(gl, g);
}

/* the sfnt p (n bytes, at base in its pool) read as the face fc, its code points and
   kerning pairs pushed to gl and kn: 1, or 0 when it is no font this reads */
static int amb_sfnt_face(const uint8_t *p, size_t n, uint32_t base, int builtin, vector<amb_glyph> *gl, vector<amb_kern> *kn, amb_face *fc) {
    amb_tables t;
    if (!amb_sfnt_tables(p, n, &t)) return 0;
    memset(fc, 0, sizeof *fc);
    fc->upem = amb_u16(p, n, t.head + 18);
    if (fc->upem < 16 || fc->upem > 16384) return 0;
    fc->loca_long = (uint8_t)(amb_s16(p, n, t.head + 50) == 1);
    fc->nglyphs = amb_u16(p, n, t.maxp + 4);
    uint32_t nhm = amb_u16(p, n, t.hhea + 34);
    if (nhm == 0 || fc->nglyphs == 0 || (size_t)t.hmtx + 4 * (size_t)nhm > n) return 0;
    fc->asc = amb_s16(p, n, t.hhea + 4);
    fc->desc = -amb_s16(p, n, t.hhea + 6);
    fc->gap = amb_s16(p, n, t.hhea + 8);
    if (t.os2 && t.os2_len >= 78) {
        /* OS/2's typographic metrics when the font asks for them (USE_TYPO_METRICS), its
           Windows ones when hhea gives none */
        if (amb_u16(p, n, t.os2 + 62) & 0x80) {
            fc->asc = amb_s16(p, n, t.os2 + 68);
            fc->desc = -amb_s16(p, n, t.os2 + 70);
            fc->gap = amb_s16(p, n, t.os2 + 72);
        } else if (fc->asc == 0 && fc->desc == 0) {
            fc->asc = (int32_t)amb_u16(p, n, t.os2 + 74);
            fc->desc = (int32_t)amb_u16(p, n, t.os2 + 76);
            fc->gap = 0;
        }
    }
    if (fc->gap < 0) fc->gap = 0;
    fc->base = base;
    fc->size = (uint32_t)n;
    fc->builtin = (uint8_t)builtin;
    fc->loca = t.loca;
    fc->glyf = t.glyf;
    fc->glyf_size = t.glyf_len;
    /* cmap: a Unicode subtable, format 12 over format 4 */
    uint32_t nsub = amb_u16(p, n, t.cmap + 2), sub = 0, fmt = 0;
    for (uint32_t i = 0; i < nsub; i++) {
        size_t r = t.cmap + 4 + 8 * (size_t)i;
        uint32_t plat = amb_u16(p, n, r), enc = amb_u16(p, n, r + 2), off = t.cmap + amb_u32(p, n, r + 4);
        uint32_t f = amb_u16(p, n, off);
        int unicode = plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
        if (unicode && f == 12 && fmt != 12) { sub = off; fmt = 12; }
        else if (unicode && f == 4 && !fmt) { sub = off; fmt = 4; }
    }
    if (!fmt) return 0;
    fc->first = (uint32_t)len(*gl);
    if (fmt == 4) {
        uint32_t segs = amb_u16(p, n, sub + 6) / 2;
        size_t ends = sub + 14, starts = ends + 2 * (size_t)segs + 2, deltas = starts + 2 * (size_t)segs, ros = deltas + 2 * (size_t)segs;
        for (uint32_t s = 0; s < segs && len(*gl) - fc->first < 70000; s++) {
            uint32_t end = amb_u16(p, n, ends + 2 * (size_t)s), start = amb_u16(p, n, starts + 2 * (size_t)s);
            uint32_t delta = amb_u16(p, n, deltas + 2 * (size_t)s), ro = amb_u16(p, n, ros + 2 * (size_t)s);
            for (uint32_t c = start; c <= end && c != 0xFFFF; c++) {
                uint32_t g;
                if (ro == 0) g = (c + delta) & 0xFFFF;
                else {
                    g = amb_u16(p, n, ros + 2 * (size_t)s + ro + 2 * (size_t)(c - start));
                    if (g) g = (g + delta) & 0xFFFF;
                }
                amb_map(gl, p, n, &t, nhm, fc->nglyphs, c, g);
            }
        }
    } else {
        uint32_t groups = amb_u32(p, n, sub + 12);
        for (uint32_t k = 0; k < groups && len(*gl) - fc->first < 70000; k++) {
            size_t r = sub + 16 + 12 * (size_t)k;
            uint32_t start = amb_u32(p, n, r), end = amb_u32(p, n, r + 4), g0 = amb_u32(p, n, r + 8);
            if (end < start || end - start > 70000 || end > 0x10FFFF) break;
            for (uint32_t c = start; c <= end; c++) amb_map(gl, p, n, &t, nhm, fc->nglyphs, c, g0 + (c - start));
        }
    }
    fc->count = (uint32_t)len(*gl) - fc->first;
    /* the notdef glyph, after every code point: the replacement glyph when U+FFFD is none */
    {
        amb_glyph nd;
        nd.cp = 0x110000;
        nd.gid = 0;
        nd.advance = (uint16_t)amb_u16(p, n, t.hmtx);
        nd.kcount = 0;
        nd.kfirst = 0;
        push_back(gl, nd);
    }
    fc->fallback = (uint32_t)len(*gl) - 1;
    /* the code points in order (cmap's are): each found by halves; U+FFFD's the fallback */
    for (uint32_t k = 1; k < fc->count; k++)
        if (at(*gl, fc->first + k).cp <= at(*gl, fc->first + k - 1).cp) { fc->count = k; break; }
    for (uint32_t k = 0; k < 128; k++) fc->ascii[k] = 0xffff;
    for (uint32_t k = 0; k < fc->count; k++) {
        uint32_t c = at(*gl, fc->first + k).cp;
        if (c < 128) fc->ascii[c] = (uint16_t)k;
        if (c == 0xFFFD) fc->fallback = fc->first + k;
    }
    /* kern: its format 0 subtables' horizontal pairs, in order */
    fc->kfirst = (uint32_t)len(*kn);
    if (t.kern && amb_u16(p, n, t.kern) == 0) {
        uint32_t ntab = amb_u16(p, n, t.kern + 2);
        size_t r = t.kern + 4;
        for (uint32_t i = 0; i < ntab && r + 6 <= (size_t)t.kern + t.kern_len; i++) {
            uint32_t sublen = amb_u16(p, n, r + 2), cov = amb_u16(p, n, r + 4);
            if ((cov >> 8) == 0 && (cov & 1) && !(cov & 4)) {
                uint32_t np = amb_u16(p, n, r + 6);
                uint32_t last = 0;
                for (uint32_t k = 0; k < np; k++) {
                    size_t q = r + 14 + 6 * (size_t)k;
                    if (q + 6 > n) break;
                    uint32_t pair = amb_u16(p, n, q) << 16 | amb_u16(p, n, q + 2);
                    if (pair < last) break;                         /* out of order: read no further */
                    last = pair;
                    amb_kern kk;
                    kk.pair = pair;
                    kk.value = amb_s16(p, n, q + 4);
                    push_back(kn, kk);
                }
            }
            if (sublen < 6) break;
            r += sublen;
        }
    }
    fc->kcount = (uint32_t)len(*kn) - fc->kfirst;
    /* each code point's pairs as the left glyph */
    for (uint32_t k = 0; k < fc->count && fc->kcount; k++) {
        uint32_t gid = at(*gl, fc->first + k).gid, lo = 0, hi = fc->kcount;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if ((at(*kn, fc->kfirst + mid).pair >> 16) < gid) lo = mid + 1; else hi = mid;
        }
        uint32_t c = 0;
        while (lo + c < fc->kcount && (at(*kn, fc->kfirst + lo + c).pair >> 16) == gid && c < 0xffff) c++;
        (*gl)[fc->first + k].kfirst = lo;
        (*gl)[fc->first + k].kcount = (uint16_t)c;
    }
    return 1;
}

/* ---- WOFF 1: its tables made an sfnt again ---- */

#include "image.h"

/* a WOFF table's zlib data inflated, through image.c's inflate (which the loader's
   content codings share) */
static int amb_woff_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap) {
    return amb_inflate(in, n, out, cap);
}

static void amb_be16(vector<uint8_t> *o, uint32_t v) { push_back(o, (uint8_t)(v >> 8)); push_back(o, (uint8_t)v); }
static void amb_be32(vector<uint8_t> *o, uint32_t v) { amb_be16(o, v >> 16); amb_be16(o, v & 0xffff); }

static int amb_woff(const uint8_t *p, size_t n, vector<uint8_t> *sf) {
    uint32_t nt = amb_u16(p, n, 12);
    if (nt == 0 || nt > 64 || 44 + 20 * (size_t)nt > n) return 0;
    amb_be32(sf, amb_u32(p, n, 4));
    amb_be16(sf, nt);
    amb_be16(sf, 0); amb_be16(sf, 0); amb_be16(sf, 0);
    size_t dir = len(*sf);
    for (uint32_t i = 0; i < nt; i++) for (int k = 0; k < 16; k++) push_back(sf, 0);
    for (uint32_t i = 0; i < nt; i++) {
        size_t r = 44 + 20 * (size_t)i;
        uint32_t off = amb_u32(p, n, r + 4), clen = amb_u32(p, n, r + 8), olen = amb_u32(p, n, r + 12);
        if ((size_t)off + clen > n || olen > (1u << 24)) return 0;
        while (len(*sf) % 4) push_back(sf, 0);
        uint32_t at_off = (uint32_t)len(*sf);
        if (clen == olen) append_range(sf, p + off, clen);
        else {
            vector<uint8_t> out;
            if (!amb_woff_inflate(p + off, clen, &out, olen) || len(out) != olen) return 0;
            append_range(sf, data(out), len(out));
        }
        size_t e = dir + 16 * (size_t)i;
        for (int k = 0; k < 4; k++) (*sf)[e + k] = p[r + k];
        uint32_t cs = amb_u32(p, n, r + 16);
        for (int k = 0; k < 4; k++) (*sf)[e + 4 + k] = (uint8_t)(cs >> (24 - 8 * k));
        for (int k = 0; k < 4; k++) (*sf)[e + 8 + k] = (uint8_t)(at_off >> (24 - 8 * k));
        for (int k = 0; k < 4; k++) (*sf)[e + 12 + k] = (uint8_t)(olen >> (24 - 8 * k));
    }
    return 1;
}

/* ---- loading ---- */

/* each built-in face's font in font_data.h (a family's four styles, but DejaVu Sans's and
   DejaVu Serif's italics, which are their upright fonts) */
static const uint8_t amb_font_of[AMB_NFACES] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 14, 15, 16, 17, 18, 19 };

/* the built-in font k decoded, inflated and read into the static faces, and each face of
   it given its record: the style it stands for, and whether its font is italic itself.
   Its data is the program's own, so a font that cannot be read is a broken build. */
static void amb_font_read(int k) {
    if (AMB_FONT_FACES != 20) abort();
    if (!amb_s_kept) {
        atexit(amb_static_drop);
        amb_s_kept = 1;
    }
    /* the font decoded, then unpacked into the static bytes' end */
    vector<uint8_t> z;
    resize(&z, amb_font_packed[k]);
    size_t base = len(amb_sb), n = amb_font_size[k];
    resize(&amb_sb, base + n);
    if (!amb_unbase64(amb_font_piece[k], amb_font_piece[k + 1], data(z), len(z)) || !amb_unlz(data(z), len(z), data(amb_sb) + base, n))
        abort();
    amb_face fc;
    if (!amb_sfnt_face(data(amb_sb) + base, n, (uint32_t)base, 1, &amb_sg, &amb_sk, &fc)) abort();
    for (int face = 0; face < AMB_NFACES; face++) {
        if (amb_font_of[face] != k) continue;
        amb_sf[face] = fc;
        amb_sf[face].style = (uint8_t)(face & 3);
        amb_sf[face].slope = (uint8_t)((face & 2) && amb_font_of[face] != amb_font_of[face & ~2]);
    }
    amb_font_state[k] = 1;
}

/* a face that can be read: a built-in one (read on its first use), or a web font added */
static int amb_ready(const amb_fonts *f, int face) {
    if (face < 0) return 0;
    if (face < AMB_NFACES) {
        if (!amb_font_state[amb_font_of[face]]) amb_font_read(amb_font_of[face]);
        return 1;
    }
    return (uint32_t)face < f->nfaces;
}

void amb_fonts_load(amb_fonts *f) {
    clear(f->glyphs);
    clear(f->kerns);
    clear(f->bytes);
    memset(f->face, 0, sizeof f->face);
    f->nfaces = AMB_NFACES;
}

int amb_fonts_add(amb_fonts *f, const uint8_t *p, size_t n) {
    if (f->nfaces >= AMB_MAXFACES || n < 12 || n > (64u << 20)) return -1;
    vector<uint8_t> sf;
    if (amb_tag(p, n, 0, "wOFF")) { if (!amb_woff(p, n, &sf)) return -1; }
    else append_range(sf, p, n);
    uint32_t base = (uint32_t)len(f->bytes);
    amb_face fc;
    if (!amb_sfnt_face(data(sf), len(sf), base, 0, &f->glyphs, &f->kerns, &fc)) return -1;
    append_range(f->bytes, data(sf), len(sf));
    f->face[f->nfaces] = fc;
    f->nfaces++;
    return (int)f->nfaces - 1;
}

int amb_face_named(const char *family, int bold, int italic) {
    static const char *const names[6] = { "serif", "sans-serif", "couriernew", "dejavusans", "dejavuserif", "monospace" };
    int style = (bold ? 1 : 0) + (italic ? 2 : 0);
    for (int k = 0; k < 6; k++)
        if (strcmp(family, names[k]) == 0) return 4 * k + style;
    if (strcmp(family, "dejavusansmono") == 0) return AMB_FACE_MONO + style;
    return -1;
}

double amb_x_height(int face) {
    /* each family's (its styles share their x-heights) */
    static const double xh[6] = { 0.4590, 0.5283, 0.5283, 0.547, 0.519, 0.545 };
    return face >= 0 && face < AMB_NFACES ? xh[face / 4] : 0.5;
}

/* ---- a face's record and tables: a static face's, or a web font's in its amb_fonts ---- */

static void amb_rec(const amb_fonts *f, int face, amb_face *out) {
    if (face < AMB_NFACES) *out = amb_sf[face];
    else *out = f->face[face];
}
int amb_face_record(const amb_fonts *f, int face, amb_face *out) {
    memset(out, 0, sizeof *out);
    if (!amb_ready(f, face)) return 0;
    amb_rec(f, face, out);
    return 1;
}
static uint32_t amb_upem(const amb_fonts *f, int face) { return face < AMB_NFACES ? amb_sf[face].upem : f->face[face].upem; }
static uint32_t amb_first(const amb_fonts *f, int face) { return face < AMB_NFACES ? amb_sf[face].first : f->face[face].first; }
static uint32_t amb_count(const amb_fonts *f, int face) { return face < AMB_NFACES ? amb_sf[face].count : f->face[face].count; }
static uint32_t amb_kfirst(const amb_fonts *f, int face) { return face < AMB_NFACES ? amb_sf[face].kfirst : f->face[face].kfirst; }
static uint32_t amb_notdef(const amb_fonts *f, int face) { return face < AMB_NFACES ? amb_sf[face].fallback : f->face[face].fallback; }
static uint32_t amb_ascii(const amb_fonts *f, int face, uint32_t c) { return face < AMB_NFACES ? amb_sf[face].ascii[c] : f->face[face].ascii[c]; }
/* the i-th code point of the faces of face's kind (its glyph, advance and pairs) */
static amb_glyph amb_gl(const amb_fonts *f, int face, uint32_t i) {
    if (face < AMB_NFACES) return amb_sg[i];
    return f->glyphs[i];
}
static amb_kern amb_kn(const amb_fonts *f, int face, uint32_t i) {
    if (face < AMB_NFACES) return amb_sk[i];
    return f->kerns[i];
}

/* ---- measuring ---- */

static double amb_round(double x) { return x < 0 ? -(double)(long)(-x + 0.5) : (double)(long)(x + 0.5); }

void amb_face_metrics(const amb_fonts *f, int face, double size, double *asc, double *desc, double *gap) {
    amb_face fc;
    *asc = *desc = *gap = 0;
    if (!amb_ready(f, face)) return;
    amb_rec(f, face, &fc);
    double s = size / fc.upem;
    *asc = amb_round(fc.asc * s);
    *desc = amb_round(fc.desc * s);
    *gap = amb_round(fc.gap * s);
}

/* the glyph of cp among the face's own (its index among its kind's code points into *g):
   1, or 0 when the face has none -- U+00A0 drawn as a space and U+2011 as U+2010 when
   the face has no glyph of its own for them (HarfBuzz's fallbacks) */
static int amb_own_glyph(const amb_fonts *f, int face, uint32_t cp, uint32_t *g) {
    if (!amb_ready(f, face)) return 0;
    if (cp < 128) {
        uint32_t k = amb_ascii(f, face, cp);
        if (k == 0xffff) return 0;
        *g = amb_first(f, face) + k;
        return 1;
    }
    uint32_t lo = amb_first(f, face), hi = lo + amb_count(f, face);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t c = amb_gl(f, face, mid).cp;
        if (c == cp) { *g = mid; return 1; }
        if (c < cp) lo = mid + 1; else hi = mid;
    }
    if (cp == 0xA0) return amb_own_glyph(f, face, 0x20, g);
    if (cp == 0x2011) return amb_own_glyph(f, face, 0x2010, g);
    return 0;
}

/* the faces a face falls back to, in Blink's order, as Chromium's do on a machine with
   these fonts (font.h): Liberation Serif in the style the face stands for (the standard
   font, after the family), the family's regular face for a bold or an italic style, and
   DejaVu Sans; a web font's, Liberation Serif and DejaVu Sans. The k-th, or -1 */
static int amb_fallback(int face, int k) {
    int style = face < AMB_NFACES ? (face & 3) : 0;
    if (k == 0) return AMB_FACE_SERIF + style;
    if (k == 1) return style && face < AMB_NFACES ? (face & ~3) : -1;
    if (k == 2) return AMB_FACE_DV_SANS;
    return -1;
}

/* the glyph that draws cp (its index among its face's kind's code points), and the face
   whose it is: the face's, a fallback face's, or the face's replacement glyph */
static uint32_t amb_glyph_at(const amb_fonts *f, int face, uint32_t cp, int *drawn_by) {
    uint32_t g;
    *drawn_by = face;
    if (amb_own_glyph(f, face, cp, &g)) return g;
    for (int k = 0; k < 3; k++) {
        int fc = amb_fallback(face, k);
        if (fc < 0 || fc == face) continue;
        if (amb_own_glyph(f, fc, cp, &g)) { *drawn_by = fc; return g; }
    }
    /* none has it: the replacement glyph, U+FFFD's from the face or its fallbacks */
    if (cp != 0xFFFD) {
        g = amb_glyph_at(f, face, 0xFFFD, drawn_by);
        if (amb_gl(f, *drawn_by, g).cp == 0xFFFD) return g;
    }
    *drawn_by = face;
    return amb_ready(f, face) ? amb_notdef(f, face) : 0;
}

uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp, int *drawn_by) {
    uint32_t g = amb_glyph_at(f, face, cp, drawn_by);
    if (!amb_ready(f, *drawn_by)) return 0x110000;
    return amb_gl(f, *drawn_by, g).cp;
}

/* the kerning of the glyphs a then b (indexes among the face's kind's code points), in
   font units */
static int32_t amb_kern_units(const amb_fonts *f, int face, uint32_t a, uint32_t b) {
    amb_glyph ga = amb_gl(f, face, a);
    uint32_t kc = ga.kcount;
    if (!kc) return 0;
    uint32_t k0 = amb_kfirst(f, face) + ga.kfirst, right = amb_gl(f, face, b).gid;
    uint32_t lo = 0, hi = kc;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        amb_kern kk = amb_kn(f, face, k0 + mid);
        uint32_t r = kk.pair & 0xffff;
        if (r == right) return kk.value;
        if (r < right) lo = mid + 1; else hi = mid;
    }
    return 0;
}

double amb_advance(const amb_fonts *f, int face, uint32_t cp, double size) {
    int by;
    uint32_t g = amb_glyph_at(f, face, cp, &by);
    if (!amb_ready(f, by)) return 0;
    return amb_gl(f, by, g).advance * size / amb_upem(f, by);
}

double amb_kern(const amb_fonts *f, int face, uint32_t left, uint32_t right, double size) {
    int lb, rb;
    uint32_t a = amb_glyph_at(f, face, left, &lb), b = amb_glyph_at(f, face, right, &rb);
    if (lb != face || rb != face || !amb_ready(f, face)) return 0;
    return amb_kern_units(f, face, a, b) * size / amb_upem(f, face);
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
    int64_t units = 0;
    double other = 0;                           /* the glyphs fallback faces draw, in px */
    uint32_t prev = 0xffffffffu;
    size_t i = 0;
    if (!amb_ready(f, face)) return 0;
    while (i < n) {
        uint32_t cp = (uint8_t)s[i] < 0x80 ? (uint32_t)(uint8_t)s[i++] : amb_utf8_next(s, n, &i);
        int by;
        uint32_t g = amb_glyph_at(f, face, cp, &by);
        if (by != face) {
            if (amb_ready(f, by)) other += amb_gl(f, by, g).advance * size / amb_upem(f, by);
            prev = 0xffffffffu;
            continue;
        }
        units += amb_gl(f, face, g).advance;
        if (prev != 0xffffffffu) units += amb_kern_units(f, face, prev, g);
        prev = g;
    }
    return (double)units * size / amb_upem(f, face) + other;
}

uint32_t amb_text_fallbacks(const amb_fonts *f, int face, const char *s, size_t n) {
    uint32_t mask = 0;
    size_t i = 0;
    while (i < n) {
        if ((uint8_t)s[i] < 0x80) { i++; continue; }        /* every built-in face has ASCII's */
        uint32_t cp = amb_utf8_next(s, n, &i);
        int by;
        amb_glyph_at(f, face, cp, &by);
        if (by != face && by < AMB_NFACES) mask |= 1u << by;
    }
    return mask;
}

/* ---- outlines: a glyph's points from glyf ---- */

/* the glyph gid of a face (its bytes p, n) into pts, each point (x, y) made
   (a x + c y + e, b x + d y + g) for a composite's component */
static void amb_outline(const uint8_t *p, size_t n, const amb_face *fc, uint32_t gid, int depth, double a, double b, double c, double d,
                        double e, double g, vector<amb_pt> *pts) {
    if (gid >= fc->nglyphs || depth > 8 || len(*pts) > 60000) return;
    uint32_t o0, o1;
    if (fc->loca_long) { o0 = amb_u32(p, n, fc->loca + 4 * (size_t)gid); o1 = amb_u32(p, n, fc->loca + 4 * (size_t)gid + 4); }
    else { o0 = 2 * amb_u16(p, n, fc->loca + 2 * (size_t)gid); o1 = 2 * amb_u16(p, n, fc->loca + 2 * (size_t)gid + 2); }
    if (o1 <= o0 || o1 > fc->glyf_size) return;
    size_t q = fc->glyf + (size_t)o0, qe = fc->glyf + (size_t)o1;
    if (qe > n) return;
    int32_t nc = amb_s16(p, n, q);
    if (nc >= 0) {
        size_t ends = q + 10;
        uint32_t npts = nc ? amb_u16(p, n, ends + 2 * (size_t)(nc - 1)) + 1 : 0;
        if (npts > 20000) return;
        size_t ins = ends + 2 * (size_t)nc, fl = ins + 2 + amb_u16(p, n, ins);
        /* the flags (with their repeats), then the xs, then the ys */
        vector<uint8_t> flags;
        while (len(flags) < npts && fl < qe) {
            uint8_t f = p[fl++];
            push_back(flags, f);
            if (f & 8) {
                uint32_t r = fl < qe ? p[fl++] : 0;
                for (uint32_t k = 0; k < r && len(flags) < npts; k++) push_back(flags, f);
            }
        }
        if (len(flags) < npts) return;
        vector<int32_t> xs;
        int32_t v = 0;
        size_t xi = fl;
        for (uint32_t k = 0; k < npts; k++) {
            uint8_t f = at(flags, k);
            if (f & 2) { if (xi >= qe) return; v += (f & 16) ? p[xi] : -(int32_t)p[xi]; xi++; }
            else if (!(f & 16)) { v += amb_s16(p, n, xi); xi += 2; }
            push_back(xs, v);
        }
        v = 0;
        size_t yi = xi;
        uint32_t cont = 0, end = amb_u16(p, n, ends);
        for (uint32_t k = 0; k < npts; k++) {
            uint8_t f = at(flags, k);
            if (f & 4) { if (yi >= qe) return; v += (f & 32) ? p[yi] : -(int32_t)p[yi]; yi++; }
            else if (!(f & 32)) { v += amb_s16(p, n, yi); yi += 2; }
            double X = at(xs, k), Y = v;
            amb_pt pt;
            pt.x = (int16_t)amb_round(a * X + c * Y + e);
            pt.y = (int16_t)amb_round(b * X + d * Y + g);
            pt.on = (uint8_t)(f & 1);
            pt.end = (uint8_t)(k == end);
            if (k == end && cont + 1 < (uint32_t)nc) { cont++; end = amb_u16(p, n, ends + 2 * (size_t)cont); }
            push_back(pts, pt);
        }
        return;
    }
    /* a composite: each component, moved and scaled */
    size_t r = q + 10;
    for (int more = 1; more && r + 4 <= qe;) {
        uint32_t fl = amb_u16(p, n, r), cg = amb_u16(p, n, r + 2);
        r += 4;
        double dx, dy;
        if (fl & 1) { dx = amb_s16(p, n, r); dy = amb_s16(p, n, r + 2); r += 4; }
        else { dx = (int8_t)p[r]; dy = (int8_t)p[r + 1]; r += 2; }
        if (!(fl & 2)) { dx = 0; dy = 0; }                      /* placed by points: not read */
        double ca = 1, cb = 0, cc = 0, cd = 1;
        if (fl & 8) { ca = cd = amb_s16(p, n, r) / 16384.0; r += 2; }
        else if (fl & 0x40) { ca = amb_s16(p, n, r) / 16384.0; cd = amb_s16(p, n, r + 2) / 16384.0; r += 4; }
        else if (fl & 0x80) {
            ca = amb_s16(p, n, r) / 16384.0; cb = amb_s16(p, n, r + 2) / 16384.0;
            cc = amb_s16(p, n, r + 4) / 16384.0; cd = amb_s16(p, n, r + 6) / 16384.0;
            r += 8;
        }
        /* the component's transform, then the composite's own */
        amb_outline(p, n, fc, cg, depth + 1, a * ca + c * cb, b * ca + d * cb, a * cc + c * cd, b * cc + d * cd,
                    a * dx + c * dy + e, b * dx + d * dy + g, pts);
        more = (fl & 0x20) != 0;
    }
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
    *w = 0;
    *h = 0;
    *ox = 0;
    *oy = 0;
    int by;
    uint32_t gi = amb_glyph_at(f, face, cp, &by);
    if (!amb_ready(f, by)) return 0;
    amb_glyph g = amb_gl(f, by, gi);
    amb_face fc;
    amb_rec(f, by, &fc);
    vector<amb_pt> pts;
    if (by < AMB_NFACES) amb_outline(data(amb_sb) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    else amb_outline(data(f->bytes) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    uint32_t npts = (uint32_t)len(pts);
    if (npts == 0) return 0;
    /* italic asked for, of a face whose outlines are upright: Skia's skew, a quarter of
       the height (Chromium's synthetic italic) */
    int asked = italic || (face < AMB_NFACES && (amb_sf[face].style & 2));
    double s = size / fc.upem, slant = asked && !fc.slope ? 0.25 : 0;
    /* the box of the points, in px from the pen (y down) */
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (uint32_t k = 0; k < npts; k++) {
        amb_pt p = at(pts, k);
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
    while (c0 < npts) {
        uint32_t c1 = c0;
        while (c1 + 1 < npts && !at(pts, c1).end) c1++;
        uint32_t n = c1 - c0 + 1, k0 = 0;
        int found = 0;
        for (uint32_t k = 0; k < n && !found; k++) if (at(pts, c0 + k).on) { k0 = k; found = 1; }
        double sx, sy, cx, cy, qx = 0, qy = 0, px, py;
        int ctrl = 0;
        if (found) {
            amb_xf(at(pts, c0 + k0), s, slant, ax, ay, &sx, &sy);
            cx = sx;
            cy = sy;
            for (uint32_t j = 1; j <= n; j++) {
                amb_pt p = at(pts, c0 + (k0 + j) % n);
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
            amb_xf(at(pts, c0), s, slant, ax, ay, &px, &py);
            amb_xf(at(pts, c1), s, slant, ax, ay, &lx, &ly);
            sx = (px + lx) * 0.5;
            sy = (py + ly) * 0.5;
            cx = sx;
            cy = sy;
            for (uint32_t j = 0; j < n; j++) {
                amb_xf(at(pts, c0 + j), s, slant, ax, ay, &px, &py);
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
