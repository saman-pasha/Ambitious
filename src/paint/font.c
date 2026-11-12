/* src/paint/font.c -- the fonts (font.h): TrueType and OpenType read, measured, and
   their glyphs drawn as coverage masks.

   One reader serves every face. The built-in faces' twenty fonts are TrueType subsets
   of the Liberation and DejaVu fonts (font_data.h, tools/gen-font.py), packed (LZ77 in
   LZ4's byte-aligned tokens, unpacked at a few instructions a byte) and base64. Each is
   decoded, unpacked and read the first time a face of it is asked for,
   once a process, into tables every amb_fonts shares (the static faces: freed when the
   process exits); DejaVu Sans's and DejaVu Serif's italic styles are their upright
   fonts again, which amb_glyph_mask slants. Each face falls back for a code point it
   has no glyph for as Chromium's does (font.h): to the system faces among others,
   font_data.h's list of the machine's font files, each looked at when the fallback
   first reaches it -- its cmap read alone, into runs of the code points it has -- and
   read whole, as a static face, when it has one a page needs (a collection's font found
   by its index; a missing file passed over), once a process. A page's web font is read
   when it is added: its sfnt's tables found (a WOFF file's rebuilt first: WOFF 1's tables
   inflated; WOFF2's decompressed by brotli.c, its transformed glyf, loca and hmtx made
   again as Google's decoder, which Chromium runs, makes them), its
   code points from cmap (format 4 or 12), advances from hmtx, metrics from head,
   hhea and OS/2, kerning pairs from kern (format 0, not GPOS's; none for a web font
   with CFF outlines, whose kern table Chromium's sanitizer drops); its outlines are read
   when a glyph is drawn: TrueType's (glyf, through loca), or CFF's and CFF2's Type 2
   charstrings (amb_cs_run: subroutines, the flex operators, div, blend at the default
   instance, a CID-keyed font's Private DICT by FDSelect; hints skipped; the other
   arithmetic operators taken as unknown ones, as Chromium's FreeType takes them; seac's
   accented characters not composed), their cubic curves flattened as the quadratic ones
   are. Colour bitmaps (CBLC, CBDT) are measured in their strike's pixels, and not drawn.

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
   and kfirst, as a web font's are in its amb_fonts; the records, by face (the built-in
   faces, then the system faces read); each built-in font's state: 0 not read yet, 1 read;
   the system fonts' cmaps, as runs of code points (each one's first and last) */
static vector<uint8_t> amb_sb;
static vector<amb_glyph> amb_sg;
static vector<amb_kern> amb_sk;
static vector<uint32_t> amb_cov;
static amb_face amb_sf[AMB_WEBFIRST];
static uint8_t amb_font_state[AMB_FONT_FACES];
static int amb_s_kept;

/* the static faces' tables dropped when the process exits */
static void amb_static_drop(void) {
    vector_uint8_t_drop(&amb_sb);
    vector_amb_glyph_drop(&amb_sg);
    vector_amb_kern_drop(&amb_sk);
    vector_uint32_t_drop(&amb_cov);
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

typedef struct amb_tables {
    uint32_t cmap, head, hhea, hmtx, maxp, loca, glyf, glyf_len, kern, kern_len, os2, os2_len;
    uint32_t cff, cff_len, cff2, cff2_len, cblc, cblc_len, cbdt, cbdt_len;
} amb_tables;

/* an sfnt's tables, from its directory at dir (a collection's font's; 0 for a font alone):
   1 when it has those a face is read from -- cmap, head, hhea, hmtx, maxp, and outlines
   (TrueType's, CFF's or CFF2's) or colour bitmaps */
static int amb_sfnt_tables(const uint8_t *p, size_t n, size_t dir, amb_tables *t) {
    memset(t, 0, sizeof *t);
    uint32_t v = amb_u32(p, n, dir);
    if (v != 0x00010000 && !amb_tag(p, n, dir, "true") && !amb_tag(p, n, dir, "OTTO")) return 0;
    uint32_t nt = amb_u16(p, n, dir + 4);
    for (uint32_t i = 0; i < nt; i++) {
        size_t r = dir + 12 + 16 * (size_t)i;
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
        else if (amb_tag(p, n, r, "CFF ")) { t->cff = off; t->cff_len = len; }
        else if (amb_tag(p, n, r, "CFF2")) { t->cff2 = off; t->cff2_len = len; }
        else if (amb_tag(p, n, r, "CBLC")) { t->cblc = off; t->cblc_len = len; }
        else if (amb_tag(p, n, r, "CBDT")) { t->cbdt = off; t->cbdt_len = len; }
    }
    int outlines = (t->loca && t->glyf) || t->cff || t->cff2 || (t->cblc && t->cbdt);
    return t->cmap && t->head && t->hhea && t->hmtx && t->maxp && outlines;
}

/* a colour bitmap font's strike (CBLC's BitmapSize record): its largest, by ppem; 0 for none */
static size_t amb_cblc_strike(const uint8_t *p, size_t n, const amb_tables *t) {
    uint32_t sizes = amb_u32(p, n, t->cblc + 4);
    size_t best = 0;
    for (uint32_t i = 0; i < sizes && i < 64; i++) {
        size_t r = t->cblc + 8 + 48 * (size_t)i;
        if (r + 48 > (size_t)t->cblc + t->cblc_len || r + 48 > n) break;
        if (!best || p[r + 45] > p[best + 45]) best = r;
    }
    return best;
}

/* a glyph's advance in its strike's pixels, from its image's metrics (CBDT's formats 17
   and 18 through CBLC's index formats 1 and 3; index formats 2 and 5 hold them
   themselves): -1 when the strike has no image for it */
static int32_t amb_cbdt_advance(const uint8_t *p, size_t n, const amb_tables *t, size_t strike, uint32_t gid) {
    size_t arr = t->cblc + amb_u32(p, n, strike);
    uint32_t subs = amb_u32(p, n, strike + 8);
    for (uint32_t i = 0; i < subs && i < 4096; i++) {
        size_t e = arr + 8 * (size_t)i;
        uint32_t first = amb_u16(p, n, e), last = amb_u16(p, n, e + 2);
        if (gid < first || gid > last) continue;
        size_t st = arr + amb_u32(p, n, e + 4);
        uint32_t ifmt = amb_u16(p, n, st), gfmt = amb_u16(p, n, st + 2), idata = amb_u32(p, n, st + 4);
        size_t img;
        if (ifmt == 1) img = idata + amb_u32(p, n, st + 8 + 4 * (size_t)(gid - first));
        else if (ifmt == 3) img = idata + amb_u16(p, n, st + 8 + 2 * (size_t)(gid - first));
        else if ((ifmt == 2 || ifmt == 5) && st + 17 <= n) return p[st + 16];       /* bigGlyphMetrics' horiAdvance */
        else return -1;
        img += t->cbdt;
        if ((gfmt == 17 || gfmt == 18) && img + 5 <= n) return p[img + 4];
        return -1;
    }
    return -1;
}

/* a code point's glyph made one of the face's, its advance from hmtx -- or, for colour
   bitmaps (strike, the strike's record), from its image's metrics, in the strike's pixels */
static void amb_map(vector<amb_glyph> *gl, const uint8_t *p, size_t n, const amb_tables *t, uint32_t nhm, uint32_t nglyphs,
                    size_t strike, uint32_t cp, uint32_t gid) {
    if (gid == 0 || gid >= nglyphs) return;
    amb_glyph g;
    g.cp = cp;
    g.gid = gid;
    if (strike) {
        int32_t adv = amb_cbdt_advance(p, n, t, strike, gid);
        if (adv < 0) return;
        g.advance = (uint16_t)adv;
    } else g.advance = (uint16_t)amb_u16(p, n, t->hmtx + 4 * (size_t)(gid < nhm ? gid : nhm - 1));
    g.kcount = 0;
    g.kfirst = 0;
    push_back(gl, g);
}

/* a cmap's Unicode subtable (format 12 over format 4) at its offset: its format into
   *fmt, or 0 when it has none */
static uint32_t amb_cmap_sub(const uint8_t *p, size_t n, size_t cmap, uint32_t *fmt) {
    uint32_t nsub = amb_u16(p, n, cmap + 2), sub = 0;
    *fmt = 0;
    for (uint32_t i = 0; i < nsub; i++) {
        size_t r = cmap + 4 + 8 * (size_t)i;
        uint32_t plat = amb_u16(p, n, r), enc = amb_u16(p, n, r + 2), off = (uint32_t)cmap + amb_u32(p, n, r + 4);
        uint32_t f = amb_u16(p, n, off);
        int unicode = plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
        if (unicode && f == 12 && *fmt != 12) { sub = off; *fmt = 12; }
        else if (unicode && f == 4 && !*fmt) { sub = off; *fmt = 4; }
    }
    return sub;
}

/* a walk through a cmap subtable's code points and their glyphs (amb_cmap_next): where
   it is (a segment or group, and its next code point), how many it has given */
typedef struct amb_cwalk { uint32_t seg, c, given; int started; } amb_cwalk;

/* the next code point and its glyph (0 for none) of the subtable sub of format fmt: 1, or
   0 when there are no more (at most 70,000) */
static int amb_cmap_next(const uint8_t *p, size_t n, uint32_t sub, uint32_t fmt, amb_cwalk *w, uint32_t *cp, uint32_t *gid) {
    if (w->given >= 70000) return 0;
    if (fmt == 4) {
        uint32_t segs = amb_u16(p, n, sub + 6) / 2;
        size_t ends = sub + 14, starts = ends + 2 * (size_t)segs + 2, deltas = starts + 2 * (size_t)segs, ros = deltas + 2 * (size_t)segs;
        while (w->seg < segs) {
            uint32_t s = w->seg, end = amb_u16(p, n, ends + 2 * (size_t)s), start = amb_u16(p, n, starts + 2 * (size_t)s);
            if (!w->started) { w->c = start; w->started = 1; }
            if (w->c > end || w->c == 0xFFFF || w->c < start) { w->seg++; w->started = 0; continue; }
            uint32_t c = w->c++, delta = amb_u16(p, n, deltas + 2 * (size_t)s), ro = amb_u16(p, n, ros + 2 * (size_t)s), g;
            if (ro == 0) g = (c + delta) & 0xFFFF;
            else {
                g = amb_u16(p, n, ros + 2 * (size_t)s + ro + 2 * (size_t)(c - start));
                if (g) g = (g + delta) & 0xFFFF;
            }
            *cp = c;
            *gid = g;
            w->given++;
            return 1;
        }
        return 0;
    }
    uint32_t groups = amb_u32(p, n, sub + 12);
    while (w->seg < groups) {
        size_t r = sub + 16 + 12 * (size_t)w->seg;
        uint32_t start = amb_u32(p, n, r), end = amb_u32(p, n, r + 4), g0 = amb_u32(p, n, r + 8);
        if (end < start || end - start > 70000 || end > 0x10FFFF) return 0;
        if (!w->started) { w->c = start; w->started = 1; }
        if (w->c > end) { w->seg++; w->started = 0; continue; }
        *cp = w->c;
        *gid = g0 + (w->c - start);
        w->c++;
        w->given++;
        return 1;
    }
    return 0;
}

/* the sfnt p (n bytes, at base in its pool; its table directory at dir) read as the face
   fc, its code points and kerning pairs pushed to gl and kn: 1, or 0 when it is no font
   this reads */
static int amb_sfnt_face(const uint8_t *p, size_t n, size_t dir, uint32_t base, int builtin, vector<amb_glyph> *gl, vector<amb_kern> *kn,
                         amb_face *fc) {
    amb_tables t;
    if (!amb_sfnt_tables(p, n, dir, &t)) return 0;
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
    fc->xmin = amb_s16(p, n, t.head + 36);
    fc->xmax = amb_s16(p, n, t.head + 40);
    if (t.os2 && t.os2_len >= 4) fc->avgw = amb_s16(p, n, t.os2 + 2);
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
    /* a font of the program's own or the machine's: italic as its head says (a web
       font's style is its @font-face rule's, which layout reads) */
    if (builtin) fc->slope = (uint8_t)((amb_u16(p, n, t.head + 44) & 2) != 0);
    fc->loca = t.loca;
    fc->glyf = t.glyf;
    fc->glyf_size = t.glyf_len;
    fc->outlines = AMB_GLYF;
    size_t strike = 0;
    if (!(t.loca && t.glyf) && (t.cff2 || t.cff)) {
        /* CFF's or CFF2's charstrings (amb_cff_outline reads the table a glyph at a time),
           in units of head's em, as FreeType scales them (a FontMatrix's scale aside) */
        fc->outlines = t.cff2 ? AMB_CFF2 : AMB_CFF;
        fc->loca = 0;
        fc->glyf = t.cff2 ? t.cff2 : t.cff;
        fc->glyf_size = t.cff2 ? t.cff2_len : t.cff_len;
    } else if (!(t.loca && t.glyf)) {
        /* colour bitmaps alone: measured in the pixels of its largest strike, which Skia
           scales to the size -- its advances and its line's ascent and descent the
           strike's (Chromium's emoji) */
        strike = amb_cblc_strike(p, n, &t);
        if (!strike || p[strike + 45] == 0) return 0;
        fc->outlines = AMB_BITMAPS;
        fc->upem = p[strike + 45];
        fc->asc = (int8_t)p[strike + 16];
        fc->desc = -(int8_t)p[strike + 17];
        fc->gap = 0;
        fc->loca = t.cblc;
        fc->glyf = t.cbdt;
        fc->glyf_size = t.cbdt_len;
    }
    /* cmap: a Unicode subtable, format 12 over format 4 */
    uint32_t fmt;
    uint32_t sub = amb_cmap_sub(p, n, t.cmap, &fmt);
    if (!fmt) return 0;
    fc->first = (uint32_t)len(*gl);
    amb_cwalk w;
    memset(&w, 0, sizeof w);
    uint32_t c, g;
    while (amb_cmap_next(p, n, sub, fmt, &w, &c, &g)) amb_map(gl, p, n, &t, nhm, fc->nglyphs, strike, c, g);
    fc->count = (uint32_t)len(*gl) - fc->first;
    /* the notdef glyph, after every code point: what a code point no face has is drawn
       with (Chromium draws the first font's) */
    {
        amb_glyph nd;
        nd.cp = 0x110000;
        nd.gid = 0;
        nd.advance = (uint16_t)(strike ? amb_u16(p, n, t.hmtx) * fc->upem / amb_u16(p, n, t.head + 18) : amb_u16(p, n, t.hmtx));
        nd.kcount = 0;
        nd.kfirst = 0;
        push_back(gl, nd);
    }
    fc->fallback = (uint32_t)len(*gl) - 1;
    /* the code points in order (cmap's are): each found by halves */
    for (uint32_t k = 1; k < fc->count; k++)
        if (at(*gl, fc->first + k).cp <= at(*gl, fc->first + k - 1).cp) { fc->count = k; break; }
    for (uint32_t k = 0; k < 128; k++) fc->ascii[k] = 0xffff;
    for (uint32_t k = 0; k < fc->count; k++) {
        uint32_t cp = at(*gl, fc->first + k).cp;
        if (cp < 128) fc->ascii[cp] = (uint16_t)k;
    }
    /* kern: its format 0 subtables' horizontal pairs, in order -- but a web font's with CFF
       outlines, whose kern table Chromium's sanitizer (OTS) drops: Chromium draws it
       unkerned */
    fc->kfirst = (uint32_t)len(*kn);
    int sanitized_away = !builtin && fc->outlines != AMB_GLYF;
    if (t.kern && amb_u16(p, n, t.kern) == 0 && !sanitized_away) {
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

/* ---- WOFF2: its tables decompressed (Brotli: brotli.c) and made an sfnt again ---- */

#include "brotli.h"

/* the tags a WOFF2 table directory names by their index (63: a tag of its own follows) */
static const char amb_woff2_tags[] = "cmapheadhheahmtxmaxpnameOS/2postcvt fpgmglyflocaprepCFF VORGEBDTEBLCgasphdmxkernLTSHPCLTVDMXvheavmtxBASEGDEFGPOSGSUBEBSCJSTFMATHCBDTCBLCCOLRCPALSVG sbixacntavarbdatblocbslncvarfdscfeatfmtxfvargvarhstyjustlcarmortmorxopbdproptrakZapfSilfGlatGlocFeatSill";

/* a UIntBase128 at *q (one to five bytes, seven bits each, no leading zero, below 2^32) */
static int amb_base128(const uint8_t *p, size_t n, size_t *q, uint32_t *v) {
    uint32_t r = 0;
    for (int i = 0; i < 5; i++) {
        if (*q >= n) return 0;
        uint32_t c = p[(*q)++];
        if ((i == 0 && c == 0x80) || (r & 0xFE000000)) return 0;
        r = r << 7 | (c & 0x7f);
        if (!(c & 0x80)) {
            *v = r;
            return 1;
        }
    }
    return 0;
}

/* a 255UInt16 at *q, before end */
static int amb_u255(const uint8_t *p, size_t end, size_t *q, uint32_t *v) {
    if (*q >= end) return 0;
    uint32_t c = p[(*q)++];
    if (c == 253) {
        if (*q + 2 > end) return 0;
        *v = (uint32_t)p[*q] << 8 | p[*q + 1];
        *q += 2;
    } else if (c == 254 || c == 255) {
        if (*q >= end) return 0;
        *v = p[(*q)++] + (c == 254 ? 506u : 253u);
    } else *v = c;
    return 1;
}

/* the transformed glyf table (t, tn bytes; WOFF2 5.1) made glyf and loca again as Google's
   decoder makes them (which Chromium's sanitizer runs): each glyph from its streams -- its
   contours' point counts, its points' flags and their coordinates' triplets, its
   instructions, a composite's components, its box (given, or its points') -- written as
   TrueType writes it (flags repeated, coordinates short where they fit) and padded to four
   bytes; loca in the index format given. Each glyph's xMin into xmin (for hmtx). 1, or 0
   when the streams do not hold the glyphs */
static int amb_woff2_glyf(const uint8_t *t, size_t tn, vector<uint8_t> *glyf, vector<uint8_t> *loca, vector<int16_t> *xmin,
                          uint32_t *nglyphs, uint32_t *ifmt) {
    if (tn < 36) return 0;
    uint32_t opt = amb_u16(t, tn, 2), ng = amb_u16(t, tn, 4);
    *nglyphs = ng;
    *ifmt = amb_u16(t, tn, 6);
    if (*ifmt > 1) return 0;
    /* the streams: contour counts, point counts, flags, glyphs (triplets, instruction
       lengths), composites, boxes (a bitmap of the glyphs that have one, then theirs),
       instructions; and the bitmap of glyphs whose contours overlap */
    size_t c[7], e[7], sp = 36;
    for (int k = 0; k < 7; k++) {
        size_t sz = amb_u32(t, tn, 8 + 4 * (size_t)k);
        if (sz > tn - sp) return 0;
        c[k] = sp;
        e[k] = sp + sz;
        sp += sz;
    }
    size_t overlap = 0, bbmap = c[5];
    if (opt & 1) {
        if (((size_t)ng + 7) / 8 > tn - sp) return 0;
        overlap = sp;
    }
    size_t bbsize = (((size_t)ng + 31) >> 5) << 2;
    if (bbsize > e[5] - c[5]) return 0;
    c[5] += bbsize;
    clear(*glyf);
    clear(*loca);
    clear(*xmin);
    vector<uint32_t> offs;
    vector<int32_t> xs, ys;
    vector<uint8_t> on, gb;
    for (uint32_t i = 0; i < ng; i++) {
        if (c[0] + 2 > e[0]) return 0;
        uint32_t nc = amb_u16(t, tn, c[0]);
        c[0] += 2;
        int have_bbox = (t[bbmap + (i >> 3)] & (0x80 >> (i & 7))) != 0;
        int16_t x0 = 0;
        clear(gb);
        if (nc == 0xFFFF) {
            /* a composite: its components as they are, its box, its instructions */
            if (!have_bbox) return 0;
            size_t cs = c[4];
            uint32_t flags = 0x20, instr = 0, ilen = 0;
            while (flags & 0x20) {
                if (cs + 2 > e[4]) return 0;
                flags = amb_u16(t, tn, cs);
                cs += 2;
                instr |= flags & 0x100;
                size_t arg = 2 + ((flags & 1) ? 4 : 2) + ((flags & 8) ? 2 : (flags & 0x40) ? 4 : (flags & 0x80) ? 8 : 0);
                if (arg > e[4] - cs) return 0;
                cs += arg;
            }
            if (instr && !amb_u255(t, e[3], &c[3], &ilen)) return 0;
            if (c[5] + 8 > e[5] || ilen > e[6] - c[6]) return 0;
            amb_be16(&gb, 0xFFFF);
            append_range(gb, t + c[5], 8);
            x0 = (int16_t)amb_u16(t, tn, c[5]);
            c[5] += 8;
            append_range(gb, t + c[4], cs - c[4]);
            c[4] = cs;
            if (instr) {
                amb_be16(&gb, ilen);
                append_range(gb, t + c[6], ilen);
                c[6] += ilen;
            }
        } else if (nc > 0) {
            /* a simple glyph: its contours' ends, its points from their triplets */
            clear(offs);
            uint32_t total = 0, k;
            for (k = 0; k < nc; k++) {
                uint32_t np;
                if (!amb_u255(t, e[1], &c[1], &np)) return 0;
                total += np;
                if (total >= (1u << 16)) return 0;
                push_back(offs, total);
            }
            if (total > e[2] - c[2]) return 0;
            clear(xs);
            clear(ys);
            clear(on);
            int32_t x = 0, y = 0;
            for (k = 0; k < total; k++) {
                uint32_t f = t[c[2] + k], b = f & 0x7f;
                size_t nb = b < 84 ? 1 : b < 120 ? 2 : b < 124 ? 3 : 4;
                if (nb > e[3] - c[3]) return 0;
                size_t q = c[3];
                int32_t dx, dy;
                if (b < 10) {
                    dx = 0;
                    dy = (int32_t)(((b & 14) << 7) + t[q]);
                } else if (b < 20) {
                    dx = (int32_t)((((b - 10) & 14) << 7) + t[q]);
                    dy = 0;
                } else if (b < 84) {
                    uint32_t b0 = b - 20, b1 = t[q];
                    dx = (int32_t)(1 + (b0 & 0x30) + (b1 >> 4));
                    dy = (int32_t)(1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f));
                } else if (b < 120) {
                    uint32_t b0 = b - 84;
                    dx = (int32_t)(1 + ((b0 / 12) << 8) + t[q]);
                    dy = (int32_t)(1 + (((b0 % 12) >> 2) << 8) + t[q + 1]);
                } else if (b < 124) {
                    uint32_t b2 = t[q + 1];
                    dx = (int32_t)(((uint32_t)t[q] << 4) + (b2 >> 4));
                    dy = (int32_t)(((b2 & 0x0f) << 8) + t[q + 2]);
                } else {
                    dx = (int32_t)((uint32_t)t[q] << 8 | t[q + 1]);
                    dy = (int32_t)((uint32_t)t[q + 2] << 8 | t[q + 3]);
                }
                /* the sign of each: the flag's low bit for x, its next for y (its low bit
                   for a y alone) */
                if (!(b & 1)) dx = -dx;
                if (!((b < 10 ? b : b >> 1) & 1)) dy = -dy;
                c[3] += nb;
                x += dx;
                y += dy;
                push_back(xs, x);
                push_back(ys, y);
                push_back(on, (uint8_t)!(f >> 7));
            }
            c[2] += total;
            uint32_t ilen;
            if (!amb_u255(t, e[3], &c[3], &ilen) || ilen > e[6] - c[6]) return 0;
            /* its box: given, or its points' */
            int32_t bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            if (have_bbox) {
                if (c[5] + 8 > e[5]) return 0;
                bx0 = (int16_t)amb_u16(t, tn, c[5]);
                by0 = (int16_t)amb_u16(t, tn, c[5] + 2);
                bx1 = (int16_t)amb_u16(t, tn, c[5] + 4);
                by1 = (int16_t)amb_u16(t, tn, c[5] + 6);
                c[5] += 8;
            } else if (total) {
                bx0 = bx1 = at(xs, 0);
                by0 = by1 = at(ys, 0);
                for (k = 1; k < total; k++) {
                    int32_t px = at(xs, k), py = at(ys, k);
                    if (px < bx0) bx0 = px;
                    if (px > bx1) bx1 = px;
                    if (py < by0) by0 = py;
                    if (py > by1) by1 = py;
                }
            }
            x0 = (int16_t)bx0;
            amb_be16(&gb, nc);
            amb_be16(&gb, (uint32_t)bx0 & 0xffff);
            amb_be16(&gb, (uint32_t)by0 & 0xffff);
            amb_be16(&gb, (uint32_t)bx1 & 0xffff);
            amb_be16(&gb, (uint32_t)by1 & 0xffff);
            for (k = 0; k < nc; k++) amb_be16(&gb, at(offs, k) - 1);
            amb_be16(&gb, ilen);
            append_range(gb, t + c[6], ilen);
            c[6] += ilen;
            /* the flags (a run of one repeated, with its count), then x's, then y's */
            int overlapbit = overlap && (t[overlap + (i >> 3)] & (0x80 >> (i & 7)));
            int last = -1;
            uint32_t repeat = 0;
            int32_t lx = 0, ly = 0;
            for (k = 0; k < total; k++) {
                int32_t dx = at(xs, k) - lx, dy = at(ys, k) - ly;
                int fl = at(on, k) ? 1 : 0;
                if (overlapbit && k == 0) fl |= 0x40;
                if (dx == 0) fl |= 0x10;
                else if (dx > -256 && dx < 256) fl |= 0x02 | (dx > 0 ? 0x10 : 0);
                if (dy == 0) fl |= 0x20;
                else if (dy > -256 && dy < 256) fl |= 0x04 | (dy > 0 ? 0x20 : 0);
                if (fl == last && repeat != 255) {
                    gb[len(gb) - 1 - (repeat ? 1 : 0)] |= 0x08;
                    if (repeat) gb[len(gb) - 1] = (uint8_t)(repeat + 1);
                    else push_back(gb, 1);
                    repeat++;
                } else {
                    push_back(gb, (uint8_t)fl);
                    repeat = 0;
                }
                last = fl;
                lx = at(xs, k);
                ly = at(ys, k);
            }
            for (int axis = 0; axis < 2; axis++) {
                int32_t prev = 0;
                for (k = 0; k < total; k++) {
                    int32_t v = axis ? at(ys, k) : at(xs, k), d = v - prev;
                    if (d != 0 && d > -256 && d < 256) push_back(gb, (uint8_t)(d < 0 ? -d : d));
                    else if (d != 0) amb_be16(&gb, (uint32_t)d & 0xffff);
                    prev = v;
                }
            }
        } else if (have_bbox) return 0;                  /* an empty glyph has no box */
        push_back(*xmin, x0);
        uint32_t here = (uint32_t)len(*glyf);
        if (*ifmt == 0) amb_be16(loca, here >> 1);
        else amb_be32(loca, here);
        append_range(*glyf, data(gb), len(gb));
        while (len(*glyf) % 4) push_back(*glyf, 0);
    }
    uint32_t end = (uint32_t)len(*glyf);
    if (*ifmt == 0 && end >= 0x20000) return 0;
    if (*ifmt == 0) amb_be16(loca, end >> 1);
    else amb_be32(loca, end);
    return 1;
}

/* the transformed hmtx table (t, tn bytes; WOFF2 5.4) made hmtx again: its advances, and
   its left side bearings, given or (as its flags say) each glyph's xMin */
static int amb_woff2_hmtx(const uint8_t *t, size_t tn, uint32_t ng, uint32_t nhm, const vector<int16_t> *xmin, vector<uint8_t> *out) {
    if (tn < 1 || nhm < 1 || nhm > ng || len(*xmin) != ng) return 0;
    uint32_t fl = t[0];
    if ((fl & 0xFC) || (fl & 3) == 0) return 0;
    size_t lsb_at = 1 + 2 * (size_t)nhm;
    if (lsb_at > tn) return 0;
    clear(*out);
    for (uint32_t i = 0; i < ng; i++) {
        if (i < nhm) amb_be16(out, amb_u16(t, tn, 1 + 2 * (size_t)i));
        int given = i < nhm ? !(fl & 1) : !(fl & 2);
        if (given) {
            if (lsb_at + 2 > tn) return 0;
            amb_be16(out, amb_u16(t, tn, lsb_at));
            lsb_at += 2;
        } else amb_be16(out, (uint32_t)(uint16_t)at(*xmin, i));
    }
    return 1;
}

/* a WOFF2 font (WOFF2 3-5) made an sfnt: its directory read, its tables decompressed, glyf
   and loca and hmtx made again where they were transformed. A collection is not read */
static int amb_woff2(const uint8_t *p, size_t n, vector<uint8_t> *sf) {
    if (n < 48 || amb_u32(p, n, 8) != n || amb_tag(p, n, 4, "ttcf")) return 0;
    uint32_t nt = amb_u16(p, n, 12), comp = amb_u32(p, n, 20);
    if (nt == 0 || nt > 256) return 0;
    char tags[256 * 4];
    uint32_t olen[256], tlen[256], xf[256];
    size_t start[256], pos = 48, total = 0;
    for (uint32_t i = 0; i < nt; i++) {
        if (pos >= n) return 0;
        uint32_t fl = p[pos++], idx = fl & 63, ver = fl >> 6;
        if (idx == 63) {
            if (pos + 4 > n) return 0;
            for (int k = 0; k < 4; k++) tags[4 * i + k] = (char)p[pos + k];
            pos += 4;
        } else
            for (int k = 0; k < 4; k++) tags[4 * i + k] = amb_woff2_tags[4 * idx + k];
        if (!amb_base128(p, n, &pos, &olen[i])) return 0;
        int glyf_or_loca = memcmp(tags + 4 * i, "glyf", 4) == 0 || memcmp(tags + 4 * i, "loca", 4) == 0;
        /* transformed: glyf and loca by version 0 (3 is none), hmtx by version 1 */
        if (glyf_or_loca) xf[i] = ver == 0;
        else xf[i] = ver != 0;
        if ((glyf_or_loca && ver != 0 && ver != 3) || (!glyf_or_loca && ver != 0 && !(ver == 1 && memcmp(tags + 4 * i, "hmtx", 4) == 0)))
            return 0;
        tlen[i] = olen[i];
        if (xf[i] && !amb_base128(p, n, &pos, &tlen[i])) return 0;
        if (xf[i] && memcmp(tags + 4 * i, "loca", 4) == 0 && tlen[i] != 0) return 0;
        start[i] = total;
        total += tlen[i];
        if (total > (64u << 20)) return 0;
    }
    if (comp > n - pos) return 0;
    vector<uint8_t> raw;
    if (!amb_brotli(p + pos, comp, &raw, total) || len(raw) != total) return 0;
    /* glyf and loca made again (both transformed, or neither), then hmtx */
    int gi = -1, li = -1, hi = -1, hhi = -1, mi = -1;
    for (uint32_t i = 0; i < nt; i++) {
        if (memcmp(tags + 4 * i, "glyf", 4) == 0) gi = (int)i;
        else if (memcmp(tags + 4 * i, "loca", 4) == 0) li = (int)i;
        else if (memcmp(tags + 4 * i, "hmtx", 4) == 0) hi = (int)i;
        else if (memcmp(tags + 4 * i, "hhea", 4) == 0) hhi = (int)i;
        else if (memcmp(tags + 4 * i, "maxp", 4) == 0) mi = (int)i;
    }
    vector<uint8_t> glyf, loca, hmtx;
    vector<int16_t> xmin;
    int gx = gi >= 0 && xf[gi], lx = li >= 0 && xf[li], hx = hi >= 0 && xf[hi];
    if (gx != lx) return 0;
    uint32_t ng = 0, ifmt = 0;
    if (gx) {
        if (!amb_woff2_glyf(data(raw) + start[gi], tlen[gi], &glyf, &loca, &xmin, &ng, &ifmt)) return 0;
        if (len(loca) != olen[li]) return 0;
    }
    if (hx) {
        if (!gx || hhi < 0 || mi < 0 || tlen[hhi] < 36) return 0;
        uint32_t nhm = amb_u16(data(raw) + start[hhi], tlen[hhi], 34);
        if (!amb_woff2_hmtx(data(raw) + start[hi], tlen[hi], ng, nhm, &xmin, &hmtx)) return 0;
    }
    /* the sfnt: its directory in its tags' order, each table's bytes padded to four */
    uint32_t order[256];
    for (uint32_t i = 0; i < nt; i++) order[i] = i;
    for (uint32_t i = 1; i < nt; i++)
        for (uint32_t j = i; j > 0 && memcmp(tags + 4 * order[j - 1], tags + 4 * order[j], 4) > 0; j--) {
            uint32_t tmp = order[j];
            order[j] = order[j - 1];
            order[j - 1] = tmp;
        }
    clear(*sf);
    uint32_t sr = 1, es = 0;
    while (sr * 2 <= nt) { sr *= 2; es++; }
    amb_be32(sf, amb_u32(p, n, 4));
    amb_be16(sf, nt);
    amb_be16(sf, 16 * sr);
    amb_be16(sf, es);
    amb_be16(sf, 16 * nt - 16 * sr);
    size_t dir = len(*sf);
    for (uint32_t i = 0; i < 16 * nt; i++) push_back(*sf, 0);
    for (uint32_t k = 0; k < nt; k++) {
        uint32_t i = order[k];
        size_t off = len(*sf);
        if ((int)i == gi && gx) append_range(*sf, data(glyf), len(glyf));
        else if ((int)i == li && lx) append_range(*sf, data(loca), len(loca));
        else if ((int)i == hi && hx) append_range(*sf, data(hmtx), len(hmtx));
        else append_range(*sf, data(raw) + start[i], tlen[i]);
        size_t tl = len(*sf) - off;
        while (len(*sf) % 4) push_back(*sf, 0);
        uint32_t sum = 0;
        for (size_t q = off; q < off + tl; q += 4)
            sum += (uint32_t)at(*sf, q) << 24 | (uint32_t)(q + 1 < len(*sf) ? at(*sf, q + 1) : 0) << 16 |
                   (uint32_t)(q + 2 < len(*sf) ? at(*sf, q + 2) : 0) << 8 | (q + 3 < len(*sf) ? at(*sf, q + 3) : 0);
        size_t r = dir + 16 * (size_t)k;
        for (int b = 0; b < 4; b++) {
            (*sf)[r + b] = (uint8_t)tags[4 * i + b];
            (*sf)[r + 4 + b] = (uint8_t)(sum >> (24 - 8 * b));
            (*sf)[r + 8 + b] = (uint8_t)(off >> (24 - 8 * b));
            (*sf)[r + 12 + b] = (uint8_t)(tl >> (24 - 8 * b));
        }
    }
    return 1;
}

/* ---- loading ---- */

/* each built-in face's font in font_data.h (a family's four styles, but DejaVu Sans's and
   DejaVu Serif's italics, which are their upright fonts) */
static const uint8_t amb_font_of[AMB_NBUILTIN] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 14, 15, 16, 17, 18, 19 };

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
    if (!amb_sfnt_face(data(amb_sb) + base, n, 0, (uint32_t)base, 1, &amb_sg, &amb_sk, &fc)) abort();
    /* the widths of its file, not of the subset (which has no OS/2, and its head bounds its own glyphs) */
    fc.avgw = amb_font_wide[k][0];
    fc.xmin = amb_font_wide[k][1];
    fc.xmax = amb_font_wide[k][2];
    for (int face = 0; face < AMB_NBUILTIN; face++) {
        if (amb_font_of[face] != k) continue;
        amb_sf[face] = fc;
        amb_sf[face].style = (uint8_t)(face & 3);
        amb_sf[face].slope = (uint8_t)((face & 2) && amb_font_of[face] != amb_font_of[face & ~2]);
    }
    amb_font_state[k] = 1;
}

/* ---- the system faces: Chromium's fallback fonts on this machine, read from their files ---- */

/* each of font_data.h's system fonts (fontconfig's order): its state -- 0 not looked at,
   1 its cmap read, 2 read as a face, 3 none to read (no file, no font this reads, no
   room); its face once read; the runs of code points its cmap has glyphs for, in amb_cov
   (first and last of each) from its first run, so many */
static uint8_t amb_sys_state[AMB_SYS_FONTS];
static int amb_sys_face[AMB_SYS_FONTS];
static uint32_t amb_sys_run[AMB_SYS_FONTS], amb_sys_nruns[AMB_SYS_FONTS];
/* the system faces read so far; each one's built-in face of the same font (-1 none) */
static int amb_sys_read;
static int8_t amb_sys_same_face[AMB_NSYSTEM];

/* bytes of a file from off, n of them (all to its end when n is 0), appended to out: how
   many, or -1 when it cannot be read */
static long amb_file_read(const char *path, size_t off, size_t n, vector<uint8_t> *out) {
    own FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    defer(fp) { fclose(fp); }
    if (fseek(fp, (long)off, SEEK_SET) != 0) return -1;
    uint8_t buf[16384];
    size_t got = 0, k;
    while (n == 0 || got < n) {
        size_t want = n == 0 || n - got > sizeof buf ? sizeof buf : n - got;
        k = fread(buf, 1, want, fp);
        if (k == 0) break;
        append_range(out, buf, k);
        got += k;
        if (got > ((size_t)256 << 20)) return -1;
    }
    return (long)got;
}

/* where a font's table directory is in its file's first bytes h (a collection's index-th
   font's): its offset, or -1 */
static long amb_font_dir(const uint8_t *h, size_t n, uint32_t index) {
    if (!amb_tag(h, n, 0, "ttcf")) return index == 0 ? 0 : -1;
    if (index >= amb_u32(h, n, 8)) return -1;
    return (long)amb_u32(h, n, 12 + 4 * (size_t)index);
}

/* system font s looked at: its table directory found, its cmap read into runs of the code
   points it has glyphs for (cheaper than reading a collection of CJK fonts whole) */
static void amb_sys_probe(int s) {
    amb_sys_state[s] = 3;
    if (!amb_s_kept) {
        atexit(amb_static_drop);
        amb_s_kept = 1;
    }
    vector<uint8_t> h;
    if (amb_file_read(amb_sys_path[s], 0, 4096, &h) < 12) return;
    long dir = amb_font_dir(data(h), len(h), amb_sys_ttc[s]);
    if (dir < 0) return;
    uint32_t nt = amb_u16(data(h), len(h), (size_t)dir + 4);
    if ((size_t)dir + 12 + 16 * (size_t)nt > len(h)) {
        clear(h);
        if (amb_file_read(amb_sys_path[s], 0, (size_t)dir + 12 + 16 * (size_t)nt, &h) < 12) return;
    }
    uint32_t cmap = 0, cmap_len = 0;
    for (uint32_t i = 0; i < nt; i++) {
        size_t r = (size_t)dir + 12 + 16 * (size_t)i;
        if (amb_tag(data(h), len(h), r, "cmap")) { cmap = amb_u32(data(h), len(h), r + 8); cmap_len = amb_u32(data(h), len(h), r + 12); }
    }
    if (!cmap || cmap_len > (16u << 20)) return;
    vector<uint8_t> cm;
    if (amb_file_read(amb_sys_path[s], cmap, cmap_len, &cm) != (long)cmap_len) return;
    uint32_t fmt;
    uint32_t sub = amb_cmap_sub(data(cm), len(cm), 0, &fmt);
    if (!fmt) return;
    amb_sys_run[s] = (uint32_t)len(amb_cov) / 2;
    amb_cwalk w;
    memset(&w, 0, sizeof w);
    uint32_t c, g;
    while (amb_cmap_next(data(cm), len(cm), sub, fmt, &w, &c, &g)) {
        if (!g) continue;
        size_t k = len(amb_cov);
        if (k > 2 * amb_sys_run[s] && at(amb_cov, k - 1) + 1 == c) amb_cov[k - 1] = c;
        else {
            push_back(amb_cov, c);
            push_back(amb_cov, c);
        }
    }
    amb_sys_nruns[s] = (uint32_t)len(amb_cov) / 2 - amb_sys_run[s];
    amb_sys_state[s] = 1;
}

/* whether system font s has a glyph for cp, by its cmap */
static int amb_sys_has(int s, uint32_t cp) {
    if (amb_sys_state[s] == 0) amb_sys_probe(s);
    if (amb_sys_state[s] == 3) return 0;
    uint32_t lo = amb_sys_run[s], hi = lo + amb_sys_nruns[s];
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (cp < at(amb_cov, 2 * (size_t)mid)) hi = mid;
        else if (cp > at(amb_cov, 2 * (size_t)mid + 1)) lo = mid + 1;
        else return 1;
    }
    return 0;
}

/* system font s read as a face: its file into the static bytes, its tables into the static
   faces'. Its face, or -1 (no font this reads, or no room) */
static int amb_sys_load(int s) {
    if (amb_sys_state[s] == 2) return amb_sys_face[s];
    if (amb_sys_state[s] != 1 || amb_sys_read >= AMB_NSYSTEM) return -1;
    amb_sys_state[s] = 3;
    size_t base = len(amb_sb);
    long n = amb_file_read(amb_sys_path[s], 0, 0, &amb_sb);
    long dir = n >= 12 ? amb_font_dir(data(amb_sb) + base, (size_t)n, amb_sys_ttc[s]) : -1;
    amb_face fc;
    if (dir < 0 || !amb_sfnt_face(data(amb_sb) + base, (size_t)n, (size_t)dir, (uint32_t)base, 1, &amb_sg, &amb_sk, &fc)) {
        resize(&amb_sb, base);
        return -1;
    }
    int face = AMB_NBUILTIN + amb_sys_read;
    amb_sf[face] = fc;
    amb_sys_same_face[amb_sys_read] = amb_sys_same[s];
    amb_sys_read++;
    amb_sys_face[s] = face;
    amb_sys_state[s] = 2;
    return face;
}

/* a face that can be read: a built-in one (read on its first use), a system face read, or
   a web font added */
static int amb_ready(const amb_fonts *f, int face) {
    if (face < 0) return 0;
    if (face < AMB_NBUILTIN) {
        if (!amb_font_state[amb_font_of[face]]) amb_font_read(amb_font_of[face]);
        return 1;
    }
    if (face < AMB_WEBFIRST) return face < AMB_NBUILTIN + amb_sys_read;
    return (uint32_t)face < f->nfaces;
}

void amb_fonts_load(amb_fonts *f) {
    clear(f->glyphs);
    clear(f->kerns);
    clear(f->bytes);
    memset(f->face, 0, sizeof f->face);
    f->nfaces = AMB_WEBFIRST;
}

int amb_fonts_add(amb_fonts *f, const uint8_t *p, size_t n) {
    if (f->nfaces >= AMB_WEBFIRST + AMB_MAXWEB || n < 12 || n > (64u << 20)) return -1;
    vector<uint8_t> sf;
    if (amb_tag(p, n, 0, "wOFF")) { if (!amb_woff(p, n, &sf)) return -1; }
    else if (amb_tag(p, n, 0, "wOF2")) { if (!amb_woff2(p, n, &sf)) return -1; }
    else append_range(sf, p, n);
    uint32_t base = (uint32_t)len(f->bytes);
    amb_face fc;
    if (!amb_sfnt_face(data(sf), len(sf), 0, base, 0, &f->glyphs, &f->kerns, &fc)) return -1;
    append_range(f->bytes, data(sf), len(sf));
    f->face[f->nfaces - AMB_WEBFIRST] = fc;
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
    return face >= 0 && face < AMB_NBUILTIN ? xh[face / 4] : 0.5;
}

/* ---- a face's record and tables: a static face's, or a web font's in its amb_fonts ---- */

static void amb_rec(const amb_fonts *f, int face, amb_face *out) {
    if (face < AMB_WEBFIRST) *out = amb_sf[face];
    else *out = f->face[face - AMB_WEBFIRST];
}
int amb_face_record(const amb_fonts *f, int face, amb_face *out) {
    memset(out, 0, sizeof *out);
    if (!amb_ready(f, face)) return 0;
    amb_rec(f, face, out);
    return 1;
}
static uint32_t amb_upem(const amb_fonts *f, int face) { return face < AMB_WEBFIRST ? amb_sf[face].upem : f->face[face - AMB_WEBFIRST].upem; }
static uint32_t amb_first(const amb_fonts *f, int face) { return face < AMB_WEBFIRST ? amb_sf[face].first : f->face[face - AMB_WEBFIRST].first; }
static uint32_t amb_count(const amb_fonts *f, int face) { return face < AMB_WEBFIRST ? amb_sf[face].count : f->face[face - AMB_WEBFIRST].count; }
static uint32_t amb_kfirst(const amb_fonts *f, int face) { return face < AMB_WEBFIRST ? amb_sf[face].kfirst : f->face[face - AMB_WEBFIRST].kfirst; }
static uint32_t amb_notdef(const amb_fonts *f, int face) { return face < AMB_WEBFIRST ? amb_sf[face].fallback : f->face[face - AMB_WEBFIRST].fallback; }
static uint32_t amb_ascii(const amb_fonts *f, int face, uint32_t c) { return face < AMB_WEBFIRST ? amb_sf[face].ascii[c] : f->face[face - AMB_WEBFIRST].ascii[c]; }
/* the i-th code point of the faces of face's kind (its glyph, advance and pairs) */
static amb_glyph amb_gl(const amb_fonts *f, int face, uint32_t i) {
    if (face < AMB_WEBFIRST) return amb_sg[i];
    return f->glyphs[i];
}
static amb_kern amb_kn(const amb_fonts *f, int face, uint32_t i) {
    if (face < AMB_WEBFIRST) return amb_sk[i];
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

void amb_face_widths(const amb_fonts *f, int face, double size, double *avg, double *maxw) {
    amb_face fc;
    *avg = *maxw = 0;
    if (!amb_ready(f, face)) return;
    amb_rec(f, face, &fc);
    double s = size / fc.upem;
    *avg = fc.avgw > 0 ? fc.avgw * s : 0;
    *maxw = amb_round((fc.xmax - fc.xmin) * s);
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

/* a code point in font_data.h's ranges (first, last, ... n of them) */
static int amb_in_ranges(const uint32_t *r, uint32_t n, uint32_t cp) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (cp < r[2 * mid]) hi = mid;
        else if (cp > r[2 * mid + 1]) lo = mid + 1;
        else return 1;
    }
    return 0;
}

/* a default-ignorable code point (U+200D, U+FE0F, ...): drawn as nothing, no advance, as
   HarfBuzz hides it */
static int amb_ignorable(uint32_t cp) { return cp >= 0xAD && amb_in_ranges(amb_ignore, AMB_IGNORE_RANGES, cp); }

/* the glyph of cp in system font s (as the built-in face it is, when it is one that has
   it): 1, its index and face into *g and *by; 0 when the font has none or cannot be read */
static int amb_sys_glyph(const amb_fonts *f, int s, uint32_t cp, uint32_t *g, int *by) {
    int same = amb_sys_same[s];
    if (same >= 0 && amb_own_glyph(f, same, cp, g)) { *by = same; return 1; }
    if (!amb_sys_has(s, cp)) return 0;
    int face = amb_sys_load(s);
    if (face < 0 || !amb_own_glyph(f, face, cp, g)) return 0;
    *by = face;
    return 1;
}

/* the glyph of cp in a face -- a built-in face's from its subset or, when that has none,
   from its font's file (Chromium draws every code point its font has with it): 1, its
   index and face into *g and *by */
static int amb_face_glyph(const amb_fonts *f, int face, uint32_t cp, uint32_t *g, int *by) {
    if (amb_own_glyph(f, face, cp, g)) { *by = face; return 1; }
    if (face < 0 || face >= AMB_NBUILTIN) return 0;
    int s = amb_font_file[amb_font_of[face]];
    if (!amb_sys_has(s, cp)) return 0;
    int sf = amb_sys_load(s);
    if (sf < 0 || !amb_own_glyph(f, sf, cp, g)) return 0;
    *by = sf;
    return 1;
}

/* the glyph that draws cp (its index among its face's kind's code points), and the face
   whose it is, in Blink's order (measured, font.h): the face's own; Liberation Serif in the
   style the face stands for (the standard font); for an emoji, the emoji font; for a bold
   or italic style, the family's regular face; the system fallback's fonts in fontconfig's
   order (but for a private-use code point, which Blink falls back for to none); else the
   face's notdef */
static uint32_t amb_glyph_at(const amb_fonts *f, int face, uint32_t cp, int *drawn_by) {
    uint32_t g;
    *drawn_by = face;
    if (amb_face_glyph(f, face, cp, &g, drawn_by)) return g;
    int style = face < AMB_NBUILTIN ? (face & 3) : 0, fc = AMB_FACE_SERIF + style;
    if (fc != face && amb_face_glyph(f, fc, cp, &g, drawn_by)) return g;
    int emoji = amb_in_ranges(amb_emoji, AMB_EMOJI_RANGES, cp);
    if (emoji && amb_sys_glyph(f, AMB_SYS_EMOJI, cp, &g, drawn_by)) return g;
    fc = face & ~3;
    if (style && !emoji && fc != face && amb_face_glyph(f, fc, cp, &g, drawn_by)) return g;
    if (!((cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000))
        for (int k = 0; k < AMB_SYS_ORDER; k++)
            if (amb_sys_glyph(f, amb_sys_order[k], cp, &g, drawn_by)) return g;
    *drawn_by = face;
    return amb_ready(f, face) ? amb_notdef(f, face) : 0;
}

uint32_t amb_glyph_of(const amb_fonts *f, int face, uint32_t cp, int *drawn_by) {
    *drawn_by = face;
    if (amb_ignorable(cp)) return 0;
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
    if (amb_ignorable(cp)) return 0;
    uint32_t g = amb_glyph_at(f, face, cp, &by);
    if (!amb_ready(f, by)) return 0;
    return amb_gl(f, by, g).advance * size / amb_upem(f, by);
}

double amb_kerning(const amb_fonts *f, int face, uint32_t left, uint32_t right, double size) {
    int lb, rb;
    if (amb_ignorable(left) || amb_ignorable(right)) return 0;
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
        if (cp >= 0xAD && amb_ignorable(cp)) { prev = 0xffffffffu; continue; }
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
        if (amb_ignorable(cp)) continue;
        int by;
        amb_glyph_at(f, face, cp, &by);
        /* a system face of a built-in face's font: that face's line metrics */
        if (by >= AMB_NBUILTIN && by < AMB_WEBFIRST && amb_sys_same_face[by - AMB_NBUILTIN] >= 0) by = amb_sys_same_face[by - AMB_NBUILTIN];
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

/* ---- outlines: a glyph's points from CFF's or CFF2's Type 2 charstrings ---- */

/* an INDEX of a CFF table (p, its n bytes) at off: its count, its offsets' size, where its
   offsets and where its data begin (CFF2's count is 4 bytes, CFF's 2). 0 when it is none */
static int amb_cff_index(const uint8_t *p, size_t n, size_t off, int cff2, uint32_t *count, uint32_t *osz, size_t *offs, size_t *dat) {
    size_t h = off + (cff2 ? 4 : 2);
    if (h > n) return 0;
    *count = cff2 ? amb_u32(p, n, off) : amb_u16(p, n, off);
    *osz = 0;
    *offs = h;
    *dat = h;
    if (*count == 0) return 1;
    if (h >= n || *count >= 0x1000000) return 0;
    *osz = p[h];
    if (*osz < 1 || *osz > 4) return 0;
    *offs = h + 1;
    *dat = *offs + (size_t)(*count + 1) * *osz - 1;
    return *dat <= n;
}

/* an offset of osz bytes at off */
static uint32_t amb_cff_off(const uint8_t *p, size_t n, size_t off, uint32_t osz) {
    uint32_t v = 0;
    for (uint32_t k = 0; k < osz; k++) v = v << 8 | (off + k < n ? p[off + k] : 0);
    return v;
}

/* the i-th item of the INDEX at off: its bytes [*a, *b). 0 when it has none */
static int amb_cff_item(const uint8_t *p, size_t n, size_t off, int cff2, uint32_t i, size_t *a, size_t *b) {
    uint32_t count, osz;
    size_t offs, dat;
    if (!amb_cff_index(p, n, off, cff2, &count, &osz, &offs, &dat) || i >= count) return 0;
    *a = dat + amb_cff_off(p, n, offs + (size_t)i * osz, osz);
    *b = dat + amb_cff_off(p, n, offs + (size_t)(i + 1) * osz, osz);
    return *a <= *b && *b <= n;
}

/* where the INDEX at off ends */
static size_t amb_cff_index_end(const uint8_t *p, size_t n, size_t off, int cff2) {
    uint32_t count, osz;
    size_t offs, dat;
    if (!amb_cff_index(p, n, off, cff2, &count, &osz, &offs, &dat)) return n;
    if (count == 0) return dat;
    return dat + amb_cff_off(p, n, offs + (size_t)count * osz, osz);
}

/* the operands (at most 4, into v) of operator op (12 x as 1200 + x) in a DICT, its bytes
   [a, b): how many, or -1 when the DICT has no op. A real's value is not needed here: 0 */
static int amb_cff_dict(const uint8_t *p, size_t n, size_t a, size_t b, int op, double *v) {
    double st[48];
    int sp = 0;
    size_t i = a;
    if (b > n) b = n;
    while (i < b) {
        uint32_t c = p[i];
        if (c <= 27 || c == 31) {                  /* an operator (CFF2's vsindex, blend, vstore among them) */
            int o = (int)c;
            i++;
            if (c == 12) { o = 1200 + (i < b ? p[i] : 0); i++; }
            if (o == op) {
                int k = sp < 4 ? sp : 4;
                for (int j = 0; j < k; j++) v[j] = st[sp - k + j];
                return k;
            }
            sp = 0;
            continue;
        }
        double x;
        if (c >= 32 && c <= 246) { x = (int)c - 139; i++; }
        else if (c >= 247 && c <= 250) { if (i + 1 >= b) return -1; x = ((int)c - 247) * 256 + p[i + 1] + 108; i += 2; }
        else if (c >= 251 && c <= 254) { if (i + 1 >= b) return -1; x = -((int)c - 251) * 256 - p[i + 1] - 108; i += 2; }
        else if (c == 28) { if (i + 2 >= b) return -1; x = (int16_t)(p[i + 1] << 8 | p[i + 2]); i += 3; }
        else if (c == 29) { if (i + 4 >= b) return -1; x = (int32_t)amb_u32(p, n, i + 1); i += 5; }
        else if (c == 30) {
            x = 0;
            i++;
            while (i < b) {
                uint32_t d = p[i++];
                if ((d & 0x0f) == 0x0f || (d >> 4) == 0x0f) break;
            }
        } else return -1;
        if (sp < 48) st[sp++] = x;
    }
    return -1;
}

/* what a glyph's charstring needs of its table: whether it is CFF2, its CharStrings and
   Global Subrs INDEXes, its local Subrs INDEX (0 none: a CID-keyed font's are its Font
   DICT's, by FDSelect), CFF2's variation store and the Private DICT's vsindex */
typedef struct amb_cffg {
    int cff2;
    size_t cs, gsubrs, lsubrs, vstore;
    uint32_t vsindex;
} amb_cffg;

/* a CFF or CFF2 table (p, its n bytes) read for glyph gid: 1, or 0 when it is none this
   reads */
static int amb_cff_open(const uint8_t *p, size_t n, int cff2, uint32_t gid, amb_cffg *g) {
    memset(g, 0, sizeof *g);
    g->cff2 = cff2;
    if (n < 5) return 0;
    size_t hdr = p[2], top_a, top_b, gs;
    double v[4];
    if (cff2) {
        if (p[0] != 2) return 0;
        top_a = hdr;
        top_b = hdr + amb_u16(p, n, 3);
        gs = top_b;
    } else {
        if (p[0] != 1) return 0;
        size_t tops = amb_cff_index_end(p, n, hdr, 0);
        if (!amb_cff_item(p, n, tops, 0, 0, &top_a, &top_b)) return 0;
        gs = amb_cff_index_end(p, n, amb_cff_index_end(p, n, tops, 0), 0);
    }
    if (top_b > n || gs >= n) return 0;
    g->gsubrs = gs;
    if (amb_cff_dict(p, n, top_a, top_b, 17, v) != 1 || v[0] <= 0 || v[0] >= n) return 0;
    g->cs = (size_t)v[0];
    if (cff2 && amb_cff_dict(p, n, top_a, top_b, 24, v) == 1 && v[0] > 0 && v[0] < n) g->vstore = (size_t)v[0];
    /* the Private DICT: the Top DICT's, or that of the glyph's Font DICT (FDSelect's) */
    size_t pa = 0, pb = 0;
    if (amb_cff_dict(p, n, top_a, top_b, 1236, v) == 1 && v[0] > 0 && v[0] < n) {
        size_t fdarray = (size_t)v[0];
        uint32_t fd = 0;
        if (amb_cff_dict(p, n, top_a, top_b, 1237, v) == 1 && v[0] > 0 && v[0] < n) {
            size_t fs = (size_t)v[0];
            uint32_t fmt = p[fs];
            if (fmt == 0) fd = fs + 1 + gid < n ? p[fs + 1 + gid] : 0;
            else if (fmt == 3 || fmt == 4) {
                /* ranges: each its first glyph and its Font DICT, then a sentinel */
                uint32_t w = fmt == 3 ? 3 : 6;
                uint32_t nr = fmt == 3 ? amb_u16(p, n, fs + 1) : amb_u32(p, n, fs + 1);
                size_t r = fs + (fmt == 3 ? 3 : 5);
                for (uint32_t k = 0; k < nr && k < 65536; k++) {
                    size_t e = r + (size_t)k * w;
                    uint32_t first = fmt == 3 ? amb_u16(p, n, e) : amb_u32(p, n, e);
                    uint32_t next = fmt == 3 ? amb_u16(p, n, e + w) : amb_u32(p, n, e + w);
                    if (gid >= first && gid < next) { fd = fmt == 3 ? (e + 2 < n ? p[e + 2] : 0) : amb_u16(p, n, e + 4); break; }
                }
            }
        }
        size_t fa, fb;
        if (!amb_cff_item(p, n, fdarray, cff2, fd, &fa, &fb)) return 0;
        if (amb_cff_dict(p, n, fa, fb, 18, v) == 2) { pb = (size_t)v[0]; pa = (size_t)v[1]; }
    } else if (amb_cff_dict(p, n, top_a, top_b, 18, v) == 2) { pb = (size_t)v[0]; pa = (size_t)v[1]; }
    if (pa && pa < n && pb <= n - pa) {
        pb += pa;
        if (amb_cff_dict(p, n, pa, pb, 19, v) == 1 && v[0] > 0 && pa + (size_t)v[0] < n) g->lsubrs = pa + (size_t)v[0];
        if (cff2 && amb_cff_dict(p, n, pa, pb, 22, v) == 1 && v[0] >= 0) g->vsindex = (uint32_t)v[0];
    }
    return 1;
}

/* a subroutine INDEX's bias (by its count, as Type 2 has it) */
static int32_t amb_cff_bias(const uint8_t *p, size_t n, size_t off, int cff2) {
    uint32_t count, osz;
    size_t offs, dat;
    if (!off || !amb_cff_index(p, n, off, cff2, &count, &osz, &offs, &dat)) return 0;
    return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

/* how many regions CFF2's blend takes deltas for: its variation store's data vsindex's */
static uint32_t amb_cff_regions(const uint8_t *p, size_t n, const amb_cffg *g, uint32_t vsindex) {
    if (!g->vstore) return 0;
    size_t vs = g->vstore + 2;                      /* the store, past its length */
    if (vsindex >= amb_u16(p, n, vs + 6)) return 0;
    size_t d = vs + amb_u32(p, n, vs + 8 + 4 * (size_t)vsindex);
    return amb_u16(p, n, d + 4);
}

/* a Type 2 charstring's interpreter: its operand stack, the pen, the stems declared (for
   hintmask's bytes), whether the width was read, a contour open, endchar met, CFF2's
   vsindex */
typedef struct amb_cs {
    double st[520];
    int sp;
    double x, y;
    int nstems, width, open, done;
    uint32_t vsindex;
} amb_cs;

/* a point of the outline at (x, y), in font units (on: 1 on the curve, 2 a cubic's
   control) */
static void amb_cs_pt(vector<amb_pt> *pts, double x, double y, uint8_t on) {
    if (len(*pts) > 60000) return;
    amb_pt q;
    q.x = (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : amb_round(x));
    q.y = (int16_t)(y < -32768 ? -32768 : y > 32767 ? 32767 : amb_round(y));
    q.on = on;
    q.end = 0;
    push_back(pts, q);
}

/* the open contour closed: its last point its end */
static void amb_cs_close(amb_cs *s, vector<amb_pt> *pts) {
    if (s->open && len(*pts)) (*pts)[len(*pts) - 1].end = 1;
    s->open = 0;
}

static void amb_cs_moveto(amb_cs *s, vector<amb_pt> *pts, double dx, double dy) {
    amb_cs_close(s, pts);
    s->x += dx;
    s->y += dy;
    amb_cs_pt(pts, s->x, s->y, 1);
    s->open = 1;
}

static void amb_cs_lineto(amb_cs *s, vector<amb_pt> *pts, double dx, double dy) {
    if (!s->open) amb_cs_moveto(s, pts, 0, 0);
    s->x += dx;
    s->y += dy;
    amb_cs_pt(pts, s->x, s->y, 1);
}

static void amb_cs_curveto(amb_cs *s, vector<amb_pt> *pts, double dx1, double dy1, double dx2, double dy2, double dx3, double dy3) {
    if (!s->open) amb_cs_moveto(s, pts, 0, 0);
    double x1 = s->x + dx1, y1 = s->y + dy1, x2 = x1 + dx2, y2 = y1 + dy2;
    s->x = x2 + dx3;
    s->y = y2 + dy3;
    amb_cs_pt(pts, x1, y1, 2);
    amb_cs_pt(pts, x2, y2, 2);
    amb_cs_pt(pts, s->x, s->y, 1);
}

/* the first stack-clearing operator met: how many of its operands (extra: 1 or 0) are the
   glyph's width, which CFF puts first when they are one more than the operator takes
   (CFF2 has none; hmtx gives the advance) */
static int amb_cs_width(amb_cs *s, const amb_cffg *g, int extra) {
    if (s->width || g->cff2) return 0;
    s->width = 1;
    return extra;
}

/* a charstring (its bytes [a, b)) run on s, its outline's points into pts; depth, how
   many subroutines deep it is. 0 when it is broken */
static int amb_cs_run(const uint8_t *p, size_t n, amb_cs *s, const amb_cffg *g, size_t a, size_t b, int depth, vector<amb_pt> *pts) {
    size_t i = a;
    if (depth > 10 || b > n) return 0;
    while (i < b && !s->done) {
        uint32_t c = p[i];
        if (c >= 32 || c == 28) {
            double x;
            if (c == 28) { if (i + 2 >= b) return 0; x = (int16_t)(p[i + 1] << 8 | p[i + 2]); i += 3; }
            else if (c <= 246) { x = (int)c - 139; i++; }
            else if (c <= 250) { if (i + 1 >= b) return 0; x = ((int)c - 247) * 256 + p[i + 1] + 108; i += 2; }
            else if (c <= 254) { if (i + 1 >= b) return 0; x = -((int)c - 251) * 256 - p[i + 1] - 108; i += 2; }
            else { if (i + 4 >= b) return 0; x = (int32_t)amb_u32(p, n, i + 1) / 65536.0; i += 5; }
            if (s->sp < 513) s->st[s->sp++] = x;
            continue;
        }
        i++;
        int k = 0, sp = s->sp;
        if (c == 1 || c == 3 || c == 18 || c == 23) {                 /* hstem vstem hstemhm vstemhm */
            k = amb_cs_width(s, g, sp % 2);
            s->nstems += (sp - k) / 2;
            s->sp = 0;
        } else if (c == 19 || c == 20) {                              /* hintmask cntrmask: vstems, then the mask */
            k = amb_cs_width(s, g, sp % 2);
            s->nstems += (sp - k) / 2;
            s->sp = 0;
            i += (size_t)(s->nstems + 7) / 8;
        } else if (c == 21) {                                         /* rmoveto */
            k = amb_cs_width(s, g, sp > 2);
            if (sp - k >= 2) amb_cs_moveto(s, pts, s->st[k], s->st[k + 1]);
            s->sp = 0;
        } else if (c == 22 || c == 4) {                               /* hmoveto vmoveto */
            k = amb_cs_width(s, g, sp > 1);
            if (sp - k >= 1) amb_cs_moveto(s, pts, c == 22 ? s->st[k] : 0, c == 22 ? 0 : s->st[k]);
            s->sp = 0;
        } else if (c == 5) {                                          /* rlineto */
            for (; k + 1 < sp; k += 2) amb_cs_lineto(s, pts, s->st[k], s->st[k + 1]);
            s->sp = 0;
        } else if (c == 6 || c == 7) {                                /* hlineto vlineto, alternating */
            int h = c == 6;
            for (; k < sp; k++, h = !h) amb_cs_lineto(s, pts, h ? s->st[k] : 0, h ? 0 : s->st[k]);
            s->sp = 0;
        } else if (c == 8) {                                          /* rrcurveto */
            for (; k + 5 < sp; k += 6) amb_cs_curveto(s, pts, s->st[k], s->st[k + 1], s->st[k + 2], s->st[k + 3], s->st[k + 4], s->st[k + 5]);
            s->sp = 0;
        } else if (c == 24) {                                         /* rcurveline */
            for (; k + 5 < sp - 2; k += 6) amb_cs_curveto(s, pts, s->st[k], s->st[k + 1], s->st[k + 2], s->st[k + 3], s->st[k + 4], s->st[k + 5]);
            if (k + 1 < sp) amb_cs_lineto(s, pts, s->st[k], s->st[k + 1]);
            s->sp = 0;
        } else if (c == 25) {                                         /* rlinecurve */
            for (; k + 1 < sp - 6; k += 2) amb_cs_lineto(s, pts, s->st[k], s->st[k + 1]);
            if (k + 5 < sp) amb_cs_curveto(s, pts, s->st[k], s->st[k + 1], s->st[k + 2], s->st[k + 3], s->st[k + 4], s->st[k + 5]);
            s->sp = 0;
        } else if (c == 26) {                                         /* vvcurveto */
            double dx1 = 0;
            if (sp % 4 == 1) { dx1 = s->st[0]; k = 1; }
            for (; k + 3 < sp; k += 4) { amb_cs_curveto(s, pts, dx1, s->st[k], s->st[k + 1], s->st[k + 2], 0, s->st[k + 3]); dx1 = 0; }
            s->sp = 0;
        } else if (c == 27) {                                         /* hhcurveto */
            double dy1 = 0;
            if (sp % 4 == 1) { dy1 = s->st[0]; k = 1; }
            for (; k + 3 < sp; k += 4) { amb_cs_curveto(s, pts, s->st[k], dy1, s->st[k + 1], s->st[k + 2], s->st[k + 3], 0); dy1 = 0; }
            s->sp = 0;
        } else if (c == 30 || c == 31) {                              /* vhcurveto hvcurveto, alternating */
            int h = c == 31;
            for (; k + 3 < sp; k += 4, h = !h) {
                double last = sp - k == 5 ? s->st[k + 4] : 0;
                if (h) amb_cs_curveto(s, pts, s->st[k], 0, s->st[k + 1], s->st[k + 2], last, s->st[k + 3]);
                else amb_cs_curveto(s, pts, 0, s->st[k], s->st[k + 1], s->st[k + 2], s->st[k + 3], last);
            }
            s->sp = 0;
        } else if (c == 10 || c == 29) {                              /* callsubr callgsubr */
            if (sp < 1) return 0;
            size_t idx = c == 10 ? g->lsubrs : g->gsubrs;
            double num = s->st[sp - 1] + amb_cff_bias(p, n, idx, g->cff2);
            s->sp--;
            size_t sa, sb;
            if (!idx || num < 0 || num > 0xffffff || !amb_cff_item(p, n, idx, g->cff2, (uint32_t)num, &sa, &sb)) return 0;
            if (!amb_cs_run(p, n, s, g, sa, sb, depth + 1, pts)) return 0;
        } else if (c == 11) {                                         /* return */
            return 1;
        } else if (c == 14) {                                         /* endchar (its accented-character form, seac, not read) */
            amb_cs_width(s, g, 0);
            amb_cs_close(s, pts);
            s->done = 1;
            return 1;
        } else if (c == 15) {                                         /* vsindex (CFF2) */
            if (sp >= 1) s->vsindex = (uint32_t)s->st[sp - 1];
            s->sp = 0;
        } else if (c == 16) {                                         /* blend (CFF2): its defaults kept, the default instance's */
            if (sp < 1) return 0;
            double nb = s->st[sp - 1];
            double used = nb + nb * amb_cff_regions(p, n, g, s->vsindex);
            if (nb < 0 || used > sp - 1) return 0;
            s->sp = sp - 1 - (int)used + (int)nb;
        } else if (c == 12) {                                         /* escape: the flexes, arithmetic */
            if (i >= b) return 0;
            uint32_t e = p[i++];
            if (e == 35 && sp >= 13) {                                /* flex: two curves (drawn as curves, as FreeType does) */
                amb_cs_curveto(s, pts, s->st[0], s->st[1], s->st[2], s->st[3], s->st[4], s->st[5]);
                amb_cs_curveto(s, pts, s->st[6], s->st[7], s->st[8], s->st[9], s->st[10], s->st[11]);
                s->sp = 0;
            } else if (e == 34 && sp >= 7) {                          /* hflex */
                double y0 = s->y;
                amb_cs_curveto(s, pts, s->st[0], 0, s->st[1], s->st[2], s->st[3], 0);
                amb_cs_curveto(s, pts, s->st[4], 0, s->st[5], y0 - s->y, s->st[6], 0);
                s->sp = 0;
            } else if (e == 36 && sp >= 9) {                          /* hflex1 */
                double y0 = s->y;
                amb_cs_curveto(s, pts, s->st[0], s->st[1], s->st[2], s->st[3], s->st[4], 0);
                amb_cs_curveto(s, pts, s->st[5], 0, s->st[6], s->st[7], s->st[8], y0 - (s->y + s->st[7]));
                s->sp = 0;
            } else if (e == 37 && sp >= 11) {                         /* flex1 */
                double dx = s->st[0] + s->st[2] + s->st[4] + s->st[6] + s->st[8], dy = s->st[1] + s->st[3] + s->st[5] + s->st[7] + s->st[9];
                int across = fabs(dx) > fabs(dy);
                amb_cs_curveto(s, pts, s->st[0], s->st[1], s->st[2], s->st[3], s->st[4], s->st[5]);
                amb_cs_curveto(s, pts, s->st[6], s->st[7], s->st[8], s->st[9], across ? s->st[10] : -dx, across ? -dy : s->st[10]);
                s->sp = 0;
            } else if (e == 12 && sp >= 2) {                          /* div */
                s->st[sp - 2] = s->st[sp - 1] != 0 ? s->st[sp - 2] / s->st[sp - 1] : 0;
                s->sp--;
            } else s->sp = 0;                                         /* the other arithmetic operators (and, add, */
                                                                      /* ifelse, roll, ...) Chromium's FreeType runs */
                                                                      /* as unknown ones (measured): the stack cleared */
        } else s->sp = 0;                                             /* reserved: the stack cleared */
    }
    return 1;
}

/* glyph gid of the CFF (or, cff2, CFF2) table at off in a face's bytes (p, n), its len
   bytes: its outline's contours into pts (their points on the curve, on 1, and a cubic's
   controls, on 2), in font units. Its hints are skipped, as TrueType's instructions are */
static void amb_cff_outline(const uint8_t *p, size_t n, size_t off, size_t len, int cff2, uint32_t gid, vector<amb_pt> *pts) {
    if (off > n || len > n - off) return;
    amb_cffg g;
    if (!amb_cff_open(p + off, len, cff2, gid, &g)) return;
    size_t a, b;
    if (!amb_cff_item(p + off, len, g.cs, cff2, gid, &a, &b)) return;
    amb_cs s;
    memset(&s, 0, sizeof s);
    s.vsindex = g.vsindex;
    amb_cs_run(p + off, len, &s, &g, a, b, 0, pts);
    amb_cs_close(&s, pts);
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

/* a cubic curve (CFF's) flattened as the quadratic ones are, into lines no further than
   about a seventh of a pixel from it: its error after n even steps is at most 3/4 of its
   larger second difference over n squared */
static void amb_acc_cubic(vector<float> *acc, int W, int H, double x0, double y0, double x1, double y1, double x2, double y2,
                          double x3, double y3) {
    double ax = x0 - 2 * x1 + x2, ay = y0 - 2 * y1 + y2, bx = x1 - 2 * x2 + x3, by = y1 - 2 * y2 + y3;
    double devsq = fmax(ax * ax + ay * ay, bx * bx + by * by);
    if (27.0 * devsq < 1) { amb_acc_line(acc, W, H, x0, y0, x3, y3); return; }
    int n = 1 + (int)floor(sqrt(sqrt(27.0 * devsq)));
    if (n > 64) n = 64;
    double px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, mt = 1 - t;
        double a = mt * mt * mt, b = 3 * mt * mt * t, c = 3 * mt * t * t, d = t * t * t;
        double qx = a * x0 + b * x1 + c * x2 + d * x3, qy = a * y0 + b * y1 + c * y2 + d * y3;
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
    if (amb_ignorable(cp)) return 0;
    uint32_t gi = amb_glyph_at(f, face, cp, &by);
    if (!amb_ready(f, by)) return 0;
    amb_glyph g = amb_gl(f, by, gi);
    amb_face fc;
    amb_rec(f, by, &fc);
    if (fc.outlines == AMB_BITMAPS) return 0;                /* colour bitmaps: not drawn */
    vector<amb_pt> pts;
    if (fc.outlines != AMB_GLYF) {
        if (by < AMB_WEBFIRST) amb_cff_outline(data(amb_sb) + fc.base, fc.size, fc.glyf, fc.glyf_size, fc.outlines == AMB_CFF2, g.gid, &pts);
        else amb_cff_outline(data(f->bytes) + fc.base, fc.size, fc.glyf, fc.glyf_size, fc.outlines == AMB_CFF2, g.gid, &pts);
    } else if (by < AMB_WEBFIRST) amb_outline(data(amb_sb) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    else amb_outline(data(f->bytes) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    uint32_t npts = (uint32_t)len(pts);
    if (npts == 0) return 0;
    /* italic asked for, of a face whose outlines are upright: Skia's skew, a quarter of
       the height (Chromium's synthetic italic) */
    int asked = italic || (face < AMB_NBUILTIN && (amb_sf[face].style & 2));
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
       of its last and first), its lines and curves -- quadratic, or cubic (CFF's: two
       controls, on 2, then the point they lead to) -- back to the start */
    double ax = dx - x0, ay = (double)-y0;
    uint32_t c0 = 0;
    while (c0 < npts) {
        uint32_t c1 = c0;
        while (c1 + 1 < npts && !at(pts, c1).end) c1++;
        uint32_t n = c1 - c0 + 1, k0 = 0;
        int found = 0;
        for (uint32_t k = 0; k < n && !found; k++) if (at(pts, c0 + k).on == 1) { k0 = k; found = 1; }
        double sx, sy, cx, cy, qx = 0, qy = 0, rx = 0, ry = 0, px, py;
        int ctrl = 0, cubic = 0;
        if (found) {
            amb_xf(at(pts, c0 + k0), s, slant, ax, ay, &sx, &sy);
            cx = sx;
            cy = sy;
            for (uint32_t j = 1; j <= n; j++) {
                amb_pt p = at(pts, c0 + (k0 + j) % n);
                amb_xf(p, s, slant, ax, ay, &px, &py);
                if (p.on == 2) {
                    if (cubic) { rx = px; ry = py; }
                    else { qx = px; qy = py; rx = px; ry = py; }
                    cubic = 1;
                } else if (p.on) {
                    if (cubic) amb_acc_cubic(acc, W, gh, cx, cy, qx, qy, rx, ry, px, py);
                    else if (ctrl) amb_acc_quad(acc, W, gh, cx, cy, qx, qy, px, py);
                    else amb_acc_line(acc, W, gh, cx, cy, px, py);
                    cx = px;
                    cy = py;
                    ctrl = 0;
                    cubic = 0;
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

/* a path command into out: its letter as a number, then its points */
static void amb_path_cmd(vector<double> *out, char c, int n, const double *v) {
    push_back(out, (double)c);
    for (int k = 0; k < n; k++) push_back(out, v[k]);
}

int amb_glyph_path(const amb_fonts *f, int face, uint32_t cp, double size, int italic, vector<double> *out) {
    int by;
    if (amb_ignorable(cp)) return 0;
    uint32_t gi = amb_glyph_at(f, face, cp, &by);
    if (!amb_ready(f, by)) return 0;
    amb_glyph g = amb_gl(f, by, gi);
    amb_face fc;
    amb_rec(f, by, &fc);
    if (fc.outlines == AMB_BITMAPS) return 0;
    vector<amb_pt> pts;
    if (fc.outlines != AMB_GLYF) {
        if (by < AMB_WEBFIRST) amb_cff_outline(data(amb_sb) + fc.base, fc.size, fc.glyf, fc.glyf_size, fc.outlines == AMB_CFF2, g.gid, &pts);
        else amb_cff_outline(data(f->bytes) + fc.base, fc.size, fc.glyf, fc.glyf_size, fc.outlines == AMB_CFF2, g.gid, &pts);
    } else if (by < AMB_WEBFIRST) amb_outline(data(amb_sb) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    else amb_outline(data(f->bytes) + fc.base, fc.size, &fc, g.gid, 0, 1, 0, 0, 1, 0, 0, &pts);
    uint32_t npts = (uint32_t)len(pts);
    if (npts == 0) return 0;
    int asked = italic || (face < AMB_NBUILTIN && (amb_sf[face].style & 2));
    double s = size / fc.upem, slant = asked && !fc.slope ? 0.25 : 0;
    /* each contour as amb_glyph_mask walks it: lines, quadratics (an implied point between two
       controls) and cubics (CFF's controls on 2), back to its start */
    uint32_t c0 = 0;
    while (c0 < npts) {
        uint32_t c1 = c0;
        while (c1 + 1 < npts && !at(pts, c1).end) c1++;
        uint32_t n = c1 - c0 + 1, k0 = 0;
        int found = 0;
        for (uint32_t k = 0; k < n && !found; k++) if (at(pts, c0 + k).on == 1) { k0 = k; found = 1; }
        double v[6], px, py, qx = 0, qy = 0, rx = 0, ry = 0;
        int ctrl = 0, cubic = 0;
        if (found) {
            amb_xf(at(pts, c0 + k0), s, slant, 0, 0, &v[0], &v[1]);
            amb_path_cmd(out, 'M', 2, v);
            for (uint32_t j = 1; j <= n; j++) {
                amb_pt p = at(pts, c0 + (k0 + j) % n);
                amb_xf(p, s, slant, 0, 0, &px, &py);
                if (p.on == 2) {
                    if (cubic) { rx = px; ry = py; }
                    else { qx = px; qy = py; rx = px; ry = py; }
                    cubic = 1;
                } else if (p.on) {
                    if (cubic) { v[0] = qx; v[1] = qy; v[2] = rx; v[3] = ry; v[4] = px; v[5] = py; amb_path_cmd(out, 'C', 6, v); }
                    else if (ctrl) { v[0] = qx; v[1] = qy; v[2] = px; v[3] = py; amb_path_cmd(out, 'Q', 4, v); }
                    else { v[0] = px; v[1] = py; amb_path_cmd(out, 'L', 2, v); }
                    ctrl = 0;
                    cubic = 0;
                } else {
                    if (ctrl) { v[0] = qx; v[1] = qy; v[2] = (qx + px) * 0.5; v[3] = (qy + py) * 0.5; amb_path_cmd(out, 'Q', 4, v); }
                    qx = px;
                    qy = py;
                    ctrl = 1;
                }
            }
        } else {
            double lx, ly, sx, sy;
            amb_xf(at(pts, c0), s, slant, 0, 0, &px, &py);
            amb_xf(at(pts, c1), s, slant, 0, 0, &lx, &ly);
            sx = (px + lx) * 0.5;
            sy = (py + ly) * 0.5;
            v[0] = sx;
            v[1] = sy;
            amb_path_cmd(out, 'M', 2, v);
            for (uint32_t j = 0; j < n; j++) {
                amb_xf(at(pts, c0 + j), s, slant, 0, 0, &px, &py);
                if (ctrl) { v[0] = qx; v[1] = qy; v[2] = (qx + px) * 0.5; v[3] = (qy + py) * 0.5; amb_path_cmd(out, 'Q', 4, v); }
                qx = px;
                qy = py;
                ctrl = 1;
            }
            v[0] = qx; v[1] = qy; v[2] = sx; v[3] = sy;
            amb_path_cmd(out, 'Q', 4, v);
        }
        amb_path_cmd(out, 'Z', 0, v);
        c0 = c1 + 1;
    }
    return 1;
}
