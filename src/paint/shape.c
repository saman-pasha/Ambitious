/* src/paint/shape.c -- OpenType layout as HarfBuzz applies it when Chromium shapes text
   (shape.h), for src/paint/font.c's amb_shape.

   The character properties are shape_data.h's (tools/gen-shape.pl, perl's Unicode::UCD).
   A run's plan is what hb-ot-map builds: the script's tag looked for in GSUB's and GPOS's
   scripts (then DFLT, dflt and latn), its default language system, and in it the features
   HarfBuzz applies for Chromium -- rvrn; ltra and ltrm, or rtla and rtlm; the common ones
   (abvm, blwm, ccmp, locl, mark, mkmk, rlig) and the horizontal ones (calt, clig, curs,
   dist, kern, liga, rclt), Blink turning liga, clig and calt off under letter-spacing and
   kern off under font-kerning: none -- in the stages the default shaper gives them (rvrn,
   then the rest), or the Arabic shaper (ccmp and locl; isol, fina, fin2, fin3, medi, med2,
   init each its own; rlig; rclt and calt; mset, clig, liga), each feature's lookups sorted by
   their index and merged within a stage, each with its features' masks and whether they
   skip joiners on their own (ccmp, locl, rlig, rclt and calt take ZWJ as it is in Arabic,
   mark and mkmk both joiners). Plans are kept by font, script and flags. The kern table is
   applied, after GPOS, where the run's script's GPOS features have no kern (Liberation's
   Hebrew and DFLT), as HarfBuzz decides; the Hebrew shaper's GPOS only where the font has a
   hebr script.

   Arabic joining is HarfBuzz's state machine (U, L, R, D and C, Syriac's Alaph and
   Dalath-Rish groups; transparent T passed over), the code points before and after the run
   its context. GSUB's lookups -- single, multiple, alternate (its first), ligature,
   contextual and chained contextual in their three formats, reverse chained -- and GPOS's --
   single, pair in both formats, cursive, mark to base, to ligature and to mark, contextual --
   are applied as hb-ot-layout-gsubgpos applies them: each glyph's class from GDEF, else
   synthesized (a nonspacing mark a mark, the rest bases); the lookup flags (ignore bases,
   ligatures, marks, a mark attachment class, a mark filtering set); HarfBuzz's skipping
   iterator (a default ignorable passed over in matching unless it is a joiner the feature
   takes as it is; context matched under any mask, input under the lookup's); ligatures'
   ids and components, the marks between their components kept with them for mark-to-
   ligature attachment and mark-to-mark within one component; a contextual lookup's own
   lookups applied at their positions, the buffer moved to each; GSUB through an output
   buffer, GPOS in place, a reverse chained lookup backward in place. Then marks' advances
   are zeroed by their class (late, as the default, Arabic and Hebrew shapers do), default
   ignorables' advances and offsets, and attachments' offsets carried to the glyphs attached
   (a mark's less the advances between it and its base; a cursive chain's across).

   Not done: FeatureVariations, a language's own system (the default one is taken, as for a
   page with no lang), 'rand' (alternates are their first), Device tables (read by HarfBuzz
   only at a pixel size, which Chromium gives no font), the automatic fractions, Arabic's
   fallback shaping (presentation forms for a font with no GSUB of its own), the fallback
   mark positioning of a font with no GPOS, and the shapers but these three (Indic, USE,
   Thai, Khmer, Myanmar and Hangul text takes the default one's). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "shape.h"
#include "shape_data.h"

#define OT_TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
enum { OT_MAXCTX = 64, OT_NEST = 64 };

/* ---- reading big-endian bytes, every read inside the font's n bytes ---- */

static uint32_t ot_u16(const uint8_t *p, size_t n, size_t off) { return off + 2 <= n ? (uint32_t)p[off] << 8 | p[off + 1] : 0; }
static int32_t ot_s16(const uint8_t *p, size_t n, size_t off) { return (int32_t)(int16_t)ot_u16(p, n, off); }
static uint32_t ot_u32(const uint8_t *p, size_t n, size_t off) {
    return off + 4 <= n ? (uint32_t)p[off] << 24 | (uint32_t)p[off + 1] << 16 | (uint32_t)p[off + 2] << 8 | p[off + 3] : 0;
}
/* the table an Offset16 at base + field names: base + it, or 0 for a null one */
static size_t ot_at(const uint8_t *p, size_t n, size_t base, size_t field) {
    uint32_t o = ot_u16(p, n, base + field);
    return o ? base + o : 0;
}

/* ---- the character properties ---- */

/* the value of cp in a table of ranges (first, last | value << 24), 0 when it is in none */
static uint32_t ot_range_value(const uint32_t *t, size_t pairs, uint32_t cp) {
    size_t lo = 0, hi = pairs;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < t[2 * mid]) hi = mid;
        else if (cp > (t[2 * mid + 1] & 0xFFFFFF)) lo = mid + 1;
        else return t[2 * mid + 1] >> 24;
    }
    return 0;
}

uint32_t amb_ccc_of(uint32_t cp) {
    if (cp < 0x300) return 0;
    size_t lo = 0, hi = AMB_SH_CCC;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t c = amb_sh_ccc[mid] >> 8;
        if (c == cp) return amb_sh_ccc[mid] & 0xff;
        if (c < cp) lo = mid + 1; else hi = mid;
    }
    return 0;
}

int amb_mark_of(uint32_t cp) { return cp < 0x300 ? 0 : (int)ot_range_value(amb_sh_mark, AMB_SH_MARK / 2, cp); }

int amb_decompose(uint32_t cp, uint32_t *a, uint32_t *b) {
    *a = cp;
    *b = 0;
    if (cp < 0xC0) return 0;
    if (cp >= 0xAC00 && cp < 0xAC00 + 11172) {
        uint32_t s = cp - 0xAC00, tr = s % 28;
        if (tr) { *a = cp - tr; *b = 0x11A7 + tr; }
        else { *a = 0x1100 + s / 588; *b = 0x1161 + s % 588 / 28; }
        return 1;
    }
    size_t lo = 0, hi = AMB_SH_DECOMP / 3;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t c = amb_sh_decomp[3 * mid];
        if (c == cp) {
            *a = amb_sh_decomp[3 * mid + 1];
            *b = amb_sh_decomp[3 * mid + 2];
            return 1;
        }
        if (c < cp) lo = mid + 1; else hi = mid;
    }
    return 0;
}

uint32_t amb_compose(uint32_t a, uint32_t b) {
    if (a >= 0x1100 && a < 0x1113 && b >= 0x1161 && b < 0x1176) return 0xAC00 + ((a - 0x1100) * 21 + b - 0x1161) * 28;
    if (a >= 0xAC00 && a < 0xAC00 + 11172 && (a - 0xAC00) % 28 == 0 && b > 0x11A7 && b < 0x11C3) return a + b - 0x11A7;
    size_t lo = 0, hi = AMB_SH_COMP / 3;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t x = amb_sh_comp[3 * mid], y = amb_sh_comp[3 * mid + 1];
        if (x == a && y == b) return amb_sh_comp[3 * mid + 2];
        if (x < a || (x == a && y < b)) lo = mid + 1; else hi = mid;
    }
    return 0;
}

uint32_t amb_mirror_of(uint32_t cp) {
    size_t lo = 0, hi = AMB_SH_MIRROR / 2;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t c = amb_sh_mirror[2 * mid];
        if (c == cp) return amb_sh_mirror[2 * mid + 1];
        if (c < cp) lo = mid + 1; else hi = mid;
    }
    return cp;
}

int amb_script_of(uint32_t cp) {
    size_t lo = 0, hi = AMB_SH_SCRIPT;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if ((amb_sh_script[mid] >> 8) <= cp) lo = mid; else hi = mid;
    }
    return (int)(amb_sh_script[lo] & 255);
}

uint32_t amb_script_tag(int script) {
    if (script <= 0 || script >= AMB_SH_TAGS) return 0;
    size_t k = 4 * (size_t)script;
    return OT_TAG(amb_sh_tags[k], amb_sh_tags[k + 1], amb_sh_tags[k + 2], amb_sh_tags[k + 3]);
}

/* a tag among those of a list (its tags one after another) */
static int ot_tag_in(uint32_t tag, const char *list) {
    for (size_t k = 0; list[k] && list[k + 1] && list[k + 2] && list[k + 3]; k += 4)
        if (OT_TAG(list[k], list[k + 1], list[k + 2], list[k + 3]) == tag) return 1;
    return 0;
}

int amb_script_rtl(int script) {
    /* hb_script_get_horizontal_direction's right-to-left scripts */
    return ot_tag_in(amb_script_tag(script), "arabhebrsyrcthaacprtkharphnxnko lydiavstarmiphliprtisarborkhsamrmandmercmeromanimendnbatnarbpalmphlphatrhungadlmrohgsogosogdelymchrsyeziougr");
}

int amb_joining_of(uint32_t cp) {
    if (cp < 0x600) return 0;
    if (cp == 0x0710) return 4;
    if (cp == 0x0715 || cp == 0x0716 || cp == 0x072A || cp == 0x072F) return 5;
    /* the table's: 1 R, 2 D, 3 C, 4 L, 5 T */
    uint32_t j = ot_range_value(amb_sh_joining, AMB_SH_JOINING / 2, cp);
    if (j == 1) return 2;
    if (j == 2 || j == 3) return 3;
    if (j == 4) return 1;
    if (j == 5) return 7;
    return 0;
}

uint32_t amb_uprops_of(uint32_t cp, uint32_t prev) {
    uint32_t u = 0;
    if (cp < 0x80) return 0;
    int mk = amb_mark_of(cp);
    if (mk) u |= AMB_U_MARK | AMB_U_CONT | (mk == 1 ? AMB_U_MN : 0);
    /* Default_Ignorable_Code_Point but the Hangul fillers, which HarfBuzz draws (font.c's amb_ignore) */
    if (cp == 0xAD || cp == 0x34F || cp == 0x61C || (cp >= 0x17B4 && cp <= 0x17B5) ||
        (cp >= 0x180B && cp <= 0x180F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF || (cp >= 0xFFF0 && cp <= 0xFFF8) || (cp >= 0x1BCA0 && cp <= 0x1BCA3) ||
        (cp >= 0x1D173 && cp <= 0x1D17A) || (cp >= 0xE0000 && cp <= 0xE0FFF)) {
        u |= AMB_U_IGNORABLE;
        if (cp == 0x200C) u |= AMB_U_ZWNJ;
        else if (cp == 0x200D) u |= AMB_U_ZWJ | AMB_U_CONT;
        else if ((cp >= 0x180B && cp <= 0x180D) || cp == 0x180F || (cp >= 0xE0020 && cp <= 0xE007F) || cp == 0x34F) u |= AMB_U_HIDDEN;
    }
    /* the continuations that are not marks (hb_set_unicode_props): emoji modifiers, a ZWJ's
       next pictograph, the tags and the halfwidth katakana sound marks */
    if ((cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xFF9E && cp <= 0xFF9F) || (cp >= 0xE0020 && cp <= 0xE007F)) u |= AMB_U_CONT;
    if (prev == 0x200D && cp >= 0x2000) u |= AMB_U_CONT;
    return u;
}

/* ---- Coverage and ClassDef tables ---- */

/* the coverage index of a glyph in the Coverage table at off: -1 when it is not covered */
static int ot_coverage(const uint8_t *p, size_t n, size_t off, uint32_t g) {
    if (!off) return -1;
    uint32_t fmt = ot_u16(p, n, off), cnt = ot_u16(p, n, off + 2);
    uint32_t lo = 0, hi = cnt;
    if (fmt == 1) {
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2, x = ot_u16(p, n, off + 4 + 2 * (size_t)mid);
            if (x == g) return (int)mid;
            if (x < g) lo = mid + 1; else hi = mid;
        }
        return -1;
    }
    if (fmt == 2) {
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            size_t r = off + 4 + 6 * (size_t)mid;
            uint32_t a = ot_u16(p, n, r), b = ot_u16(p, n, r + 2);
            if (g < a) hi = mid;
            else if (g > b) lo = mid + 1;
            else return (int)(ot_u16(p, n, r + 4) + g - a);
        }
    }
    return -1;
}

/* the class of a glyph in the ClassDef table at off (0 for none, and for a null offset) */
static uint32_t ot_class(const uint8_t *p, size_t n, size_t off, uint32_t g) {
    if (!off) return 0;
    uint32_t fmt = ot_u16(p, n, off);
    if (fmt == 1) {
        uint32_t start = ot_u16(p, n, off + 2), cnt = ot_u16(p, n, off + 4);
        return g >= start && g - start < cnt ? ot_u16(p, n, off + 6 + 2 * (size_t)(g - start)) : 0;
    }
    if (fmt == 2) {
        uint32_t lo = 0, hi = ot_u16(p, n, off + 2);
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            size_t r = off + 4 + 6 * (size_t)mid;
            uint32_t a = ot_u16(p, n, r), b = ot_u16(p, n, r + 2);
            if (g < a) hi = mid;
            else if (g > b) lo = mid + 1;
            else return ot_u16(p, n, r + 4);
        }
    }
    return 0;
}

/* ---- GDEF ---- */

/* a glyph's classes by GDEF (its glyph class, a mark's attachment class << 8): 0 for none */
static uint32_t ot_gdef_props(const uint8_t *p, size_t n, size_t gdef, uint32_t g) {
    uint32_t cls = ot_class(p, n, ot_at(p, n, gdef, 4), g);
    if (cls == 1) return AMB_P_BASE;
    if (cls == 2) return AMB_P_LIGATURE;
    if (cls == 3) return AMB_P_MARK | ot_class(p, n, ot_at(p, n, gdef, 10), g) << 8;
    return 0;
}

/* whether GDEF's mark glyph set k covers a glyph */
static int ot_mark_set(const uint8_t *p, size_t n, size_t gdef, uint32_t k, uint32_t g) {
    if (!gdef || ot_u16(p, n, gdef + 2) < 2) return 0;
    size_t ms = ot_at(p, n, gdef, 12);
    if (!ms || k >= ot_u16(p, n, ms + 2)) return 0;
    uint32_t off = ot_u32(p, n, ms + 4 + 4 * (size_t)k);
    return off && ot_coverage(p, n, ms + off, g) >= 0;
}

/* ---- the plan ---- */

/* a lookup a plan applies: the features' masks, its index, its table (0 GSUB, 1 GPOS), its
   stage, whether its features skip ZWJ and ZWNJ on their own */
typedef struct ot_lookup { uint32_t mask; uint16_t index; uint8_t table, stage, zwj, zwnj; } ot_lookup;
/* a plan: for a font (its id), a script, the flags; its lookups in ot_lks from first (GSUB's,
   then GPOS's); what HarfBuzz decides of it */
typedef struct ot_plan {
    uint32_t id, script, flags, first, nsub, npos;
    uint8_t apply_gpos, apply_kern, info, used;
} ot_plan;
/* a feature a plan looks for: its tag, mask, GSUB stage, joiners skipped */
typedef struct ot_want { uint32_t tag, mask; uint8_t stage, zwj, zwnj; } ot_want;

static vector<ot_lookup> ot_lks;
static vector<ot_plan> ot_plans;
static int ot_kept;

static void ot_static_drop(void) {
    vector_ot_lookup_drop(&ot_lks);
    vector_ot_plan_drop(&ot_plans);
}

/* the script table of GSUB or GPOS (at tb) for tag, else DFLT, dflt, latn: its offset (0 none),
   the tag chosen into *chosen */
static size_t ot_script(const uint8_t *p, size_t n, size_t tb, uint32_t tag, uint32_t *chosen) {
    *chosen = 0;
    size_t sl = ot_at(p, n, tb, 4);
    if (!sl) return 0;
    uint32_t cnt = ot_u16(p, n, sl);
    uint32_t tries[4] = { tag, OT_TAG('D', 'F', 'L', 'T'), OT_TAG('d', 'f', 'l', 't'), OT_TAG('l', 'a', 't', 'n') };
    for (int k = 0; k < 4; k++) {
        if (!tries[k]) continue;
        for (uint32_t i = 0; i < cnt; i++)
            if (ot_u32(p, n, sl + 2 + 6 * (size_t)i) == tries[k]) {
                *chosen = tries[k];
                return ot_at(p, n, sl, 6 + 6 * (size_t)i);
            }
    }
    return 0;
}

/* a script's language system: its 'dflt' one, else its default (0 none) */
static size_t ot_langsys(const uint8_t *p, size_t n, size_t sc) {
    if (!sc) return 0;
    uint32_t cnt = ot_u16(p, n, sc + 2);
    for (uint32_t i = 0; i < cnt; i++)
        if (ot_u32(p, n, sc + 4 + 6 * (size_t)i) == OT_TAG('d', 'f', 'l', 't')) return ot_at(p, n, sc, 8 + 6 * (size_t)i);
    return ot_at(p, n, sc, 0);
}

/* the Feature table of a language system whose tag is tag: its offset, 0 for none */
static size_t ot_feature(const uint8_t *p, size_t n, size_t tb, size_t ls, uint32_t tag) {
    if (!ls) return 0;
    size_t fl = ot_at(p, n, tb, 6);
    uint32_t nf = ot_u16(p, n, fl), cnt = ot_u16(p, n, ls + 4);
    for (uint32_t k = 0; k < cnt; k++) {
        uint32_t fi = ot_u16(p, n, ls + 6 + 2 * (size_t)k);
        if (fi < nf && ot_u32(p, n, fl + 2 + 6 * (size_t)fi) == tag) return ot_at(p, n, fl, 6 + 6 * (size_t)fi);
    }
    return 0;
}

/* the shaper a script's run takes: 1 the Arabic one (Arabic, or a joining script the font has
   a script of its own for), 2 the Hebrew one, 0 the default one */
static int ot_shaper(uint32_t tag, uint32_t chosen_gsub) {
    if (tag == OT_TAG('a', 'r', 'a', 'b')) return 1;
    if (ot_tag_in(tag, "syrcnko mongphagmandmaniphlpadlmrohgsogdougrchrs") && chosen_gsub && chosen_gsub != OT_TAG('D', 'F', 'L', 'T')) return 1;
    if (tag == OT_TAG('h', 'e', 'b', 'r')) return 2;
    return 0;
}

static void ot_want_add(ot_want *w, int *nw, uint32_t tag, uint32_t mask, int stage, int zwj, int zwnj) {
    if (*nw >= 48) return;
    w[*nw].tag = tag;
    w[*nw].mask = mask;
    w[*nw].stage = (uint8_t)stage;
    w[*nw].zwj = (uint8_t)zwj;
    w[*nw].zwnj = (uint8_t)zwnj;
    (*nw)++;
}

/* a font's plan for a script and flags: its index in ot_plans, made the first time */
static uint32_t ot_plan_of(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags) {
    uint32_t key = (uint32_t)flags & (AMB_SH_RTL | AMB_SH_NOLIGA | AMB_SH_NOKERN);
    for (uint32_t k = 0; k < (uint32_t)len(ot_plans); k++) {
        ot_plan q = at(ot_plans, k);
        if (q.id == t->id && q.script == (uint32_t)script && q.flags == key) return k;
    }
    if (!ot_kept) {
        atexit(ot_static_drop);
        ot_kept = 1;
    }
    if (len(ot_plans) >= 512) {
        clear(ot_plans);
        clear(ot_lks);
    }
    ot_plan pl;
    memset(&pl, 0, sizeof pl);
    pl.id = t->id;
    pl.script = (uint32_t)script;
    pl.flags = key;
    pl.first = (uint32_t)len(ot_lks);
    uint32_t tag = amb_script_tag(script), csub = 0, cpos = 0;
    size_t lsub = t->gsub ? ot_langsys(p, n, ot_script(p, n, t->gsub, tag, &csub)) : 0;
    size_t lpos = t->gpos ? ot_langsys(p, n, ot_script(p, n, t->gpos, tag, &cpos)) : 0;
    int shaper = ot_shaper(tag, csub), rtl = (flags & AMB_SH_RTL) != 0;
    int liga = !(flags & AMB_SH_NOLIGA), kern = !(flags & AMB_SH_NOKERN);
    ot_want w[48];
    int nw = 0;
    ot_want_add(w, &nw, OT_TAG('r', 'v', 'r', 'n'), AMB_M_GLOBAL, 0, 1, 1);
    if (rtl) {
        ot_want_add(w, &nw, OT_TAG('r', 't', 'l', 'a'), AMB_M_GLOBAL, 1, 1, 1);
        ot_want_add(w, &nw, OT_TAG('r', 't', 'l', 'm'), AMB_M_RTLM, 1, 1, 1);
    } else {
        ot_want_add(w, &nw, OT_TAG('l', 't', 'r', 'a'), AMB_M_GLOBAL, 1, 1, 1);
        ot_want_add(w, &nw, OT_TAG('l', 't', 'r', 'm'), AMB_M_GLOBAL, 1, 1, 1);
    }
    if (shaper == 1) {
        /* the Arabic shaper's stages (collect_features_arabic) */
        static const char forms[] = "isolfinafin2fin3medimed2init";
        ot_want_add(w, &nw, OT_TAG('c', 'c', 'm', 'p'), AMB_M_GLOBAL, 2, 0, 1);
        ot_want_add(w, &nw, OT_TAG('l', 'o', 'c', 'l'), AMB_M_GLOBAL, 2, 0, 1);
        for (int k = 0; k < 7; k++)
            ot_want_add(w, &nw, OT_TAG(forms[4 * k], forms[4 * k + 1], forms[4 * k + 2], forms[4 * k + 3]), (uint32_t)AMB_M_ISOL << k, 3 + k, 1, 1);
        ot_want_add(w, &nw, OT_TAG('r', 'l', 'i', 'g'), AMB_M_GLOBAL, 11, 0, 1);
        ot_want_add(w, &nw, OT_TAG('r', 'c', 'l', 't'), AMB_M_GLOBAL, 12, 0, 1);
        if (liga) ot_want_add(w, &nw, OT_TAG('c', 'a', 'l', 't'), AMB_M_GLOBAL, 12, 0, 1);
        ot_want_add(w, &nw, OT_TAG('m', 's', 'e', 't'), AMB_M_GLOBAL, 13, 1, 1);
        if (liga) {
            ot_want_add(w, &nw, OT_TAG('c', 'l', 'i', 'g'), AMB_M_GLOBAL, 13, 1, 1);
            ot_want_add(w, &nw, OT_TAG('l', 'i', 'g', 'a'), AMB_M_GLOBAL, 13, 1, 1);
        }
    } else {
        ot_want_add(w, &nw, OT_TAG('c', 'c', 'm', 'p'), AMB_M_GLOBAL, 1, 1, 1);
        ot_want_add(w, &nw, OT_TAG('l', 'o', 'c', 'l'), AMB_M_GLOBAL, 1, 1, 1);
        ot_want_add(w, &nw, OT_TAG('r', 'l', 'i', 'g'), AMB_M_GLOBAL, 1, 1, 1);
        ot_want_add(w, &nw, OT_TAG('r', 'c', 'l', 't'), AMB_M_GLOBAL, 1, 1, 1);
        if (liga) {
            ot_want_add(w, &nw, OT_TAG('c', 'a', 'l', 't'), AMB_M_GLOBAL, 1, 1, 1);
            ot_want_add(w, &nw, OT_TAG('c', 'l', 'i', 'g'), AMB_M_GLOBAL, 1, 1, 1);
            ot_want_add(w, &nw, OT_TAG('l', 'i', 'g', 'a'), AMB_M_GLOBAL, 1, 1, 1);
        }
    }
    ot_want_add(w, &nw, OT_TAG('a', 'b', 'v', 'm'), AMB_M_GLOBAL, 1, 1, 1);
    ot_want_add(w, &nw, OT_TAG('b', 'l', 'w', 'm'), AMB_M_GLOBAL, 1, 1, 1);
    ot_want_add(w, &nw, OT_TAG('m', 'a', 'r', 'k'), AMB_M_GLOBAL, 1, 0, 0);
    ot_want_add(w, &nw, OT_TAG('m', 'k', 'm', 'k'), AMB_M_GLOBAL, 1, 0, 0);
    ot_want_add(w, &nw, OT_TAG('c', 'u', 'r', 's'), AMB_M_GLOBAL, 1, 1, 1);
    ot_want_add(w, &nw, OT_TAG('d', 'i', 's', 't'), AMB_M_GLOBAL, 1, 1, 1);
    if (kern) ot_want_add(w, &nw, OT_TAG('k', 'e', 'r', 'n'), AMB_M_GLOBAL, 1, 1, 1);
    /* the lookups each finds in GSUB, then in GPOS (whose stages are all one) */
    int has_kern = 0, has_mark = 0;
    for (int tb = 0; tb < 2; tb++) {
        size_t table = tb ? t->gpos : t->gsub, ls = tb ? lpos : lsub;
        if (!table || !ls) continue;
        uint32_t start = (uint32_t)len(ot_lks);
        size_t ll = ot_at(p, n, table, 8);
        uint32_t nl = ot_u16(p, n, ll);
        uint32_t req = ot_u16(p, n, ls + 2);
        for (int k = -1; k < nw; k++) {
            size_t fe;
            ot_lookup lk;
            memset(&lk, 0, sizeof lk);
            lk.table = (uint8_t)tb;
            if (k < 0) {
                /* the language system's required feature, at its first stage */
                size_t fl = ot_at(p, n, table, 6);
                if (req == 0xFFFF || req >= ot_u16(p, n, fl)) continue;
                fe = ot_at(p, n, fl, 6 + 6 * (size_t)req);
                lk.mask = AMB_M_GLOBAL;
                lk.zwj = 1;
                lk.zwnj = 1;
            } else {
                fe = ot_feature(p, n, table, ls, w[k].tag);
                if (!fe) continue;
                if (tb == 1 && w[k].tag == OT_TAG('k', 'e', 'r', 'n')) has_kern = 1;
                if (w[k].tag == OT_TAG('m', 'a', 'r', 'k')) has_mark = 1;
                lk.mask = w[k].mask;
                lk.stage = tb ? 0 : w[k].stage;
                lk.zwj = w[k].zwj;
                lk.zwnj = w[k].zwnj;
            }
            uint32_t cnt = ot_u16(p, n, fe + 2);
            for (uint32_t j = 0; j < cnt; j++) {
                uint32_t li = ot_u16(p, n, fe + 4 + 2 * (size_t)j);
                if (li >= nl) continue;
                lk.index = (uint16_t)li;
                push_back(ot_lks, lk);
            }
        }
        /* sorted by stage, then index; one lookup of a stage merged: its masks together, its
           joiners skipped only when every feature of it skips them */
        uint32_t end = (uint32_t)len(ot_lks);
        for (uint32_t i = start + 1; i < end; i++) {
            ot_lookup x = at(ot_lks, i);
            uint32_t j = i;
            while (j > start && (at(ot_lks, j - 1).stage > x.stage || (at(ot_lks, j - 1).stage == x.stage && at(ot_lks, j - 1).index > x.index))) {
                ot_lks[j] = at(ot_lks, j - 1);
                j--;
            }
            ot_lks[j] = x;
        }
        uint32_t o = start;
        for (uint32_t i = start; i < end; i++) {
            ot_lookup x = at(ot_lks, i);
            if (o > start && at(ot_lks, o - 1).stage == x.stage && at(ot_lks, o - 1).index == x.index) {
                ot_lookup y = at(ot_lks, o - 1);
                y.mask |= x.mask;
                y.zwj &= x.zwj;
                y.zwnj &= x.zwnj;
                ot_lks[o - 1] = y;
                continue;
            }
            ot_lks[o++] = x;
        }
        resize(ot_lks, o);
        if (tb) pl.npos = o - start;
        else pl.nsub = o - start;
    }
    /* who positions: GPOS, unless the Hebrew shaper's font has no hebr script in it; then the
       kern table, unless GPOS applies a kern feature */
    pl.apply_gpos = (uint8_t)(t->gpos && !(shaper == 2 && cpos != OT_TAG('h', 'e', 'b', 'r')));
    if (!pl.apply_gpos) pl.npos = 0;
    pl.apply_kern = (uint8_t)(t->kern && kern && (!has_kern || !pl.apply_gpos));
    pl.info = (uint8_t)((shaper == 1 ? AMB_PI_ARABIC : 0) | (shaper == 2 ? AMB_PI_HEBREW : 0) | (has_mark ? AMB_PI_GPOS_MARK : 0));
    push_back(ot_plans, pl);
    return (uint32_t)len(ot_plans) - 1;
}

int amb_ot_plan_info(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags) {
    return at(ot_plans, ot_plan_of(p, n, t, script, flags)).info;
}

/* ---- applying lookups: the context ---- */

/* what applying a lookup knows: the font's GDEF and the table's LookupList, the lookup's mask
   and properties (its flag | its mark filtering set << 16), whether its features skip ZWJ and
   ZWNJ, the table (0 GSUB, 1 GPOS), whether GSUB's output buffer is in use, the buffer's
   direction (1 right to left), nesting left, the last ligature id given */
typedef struct ot_ctx {
    size_t gdef, lookups;
    uint32_t mask, props;
    uint8_t zwj, zwnj, table, hout, rtl;
    int nest;
    uint32_t serial;
} ot_ctx;

/* the buffer: in, the glyphs from *idx on still to be taken (those before it taken); out,
   GSUB's output so far */
#define OTX const uint8_t *p, size_t n, ot_ctx *c, vector<amb_otg> *in, vector<amb_otg> *out, size_t *idx
#define OTA p, n, c, in, out, idx

/* a glyph's properties match a lookup's (HarfBuzz's check_glyph_property) */
static int ot_props_ok(const uint8_t *p, size_t n, const ot_ctx *c, uint32_t props, amb_otg g) {
    uint32_t gp = g.props;
    if (gp & props & 0x0E) return 0;
    if (gp & AMB_P_MARK) {
        if (props & 0x10) return ot_mark_set(p, n, c->gdef, props >> 16, g.gid);
        if (props & 0xFF00) return (props & 0xFF00) == (gp & 0xFF00);
    }
    return 1;
}

/* a default ignorable, not substituted */
static int ot_ignorable(amb_otg g) { return (g.uprops & AMB_U_IGNORABLE) && !(g.props & AMB_P_SUBSTITUTED); }

/* the skipping iterator's may_skip: 1 skipped, 2 maybe skipped (an ignorable), 0 not */
static int ot_may_skip(const uint8_t *p, size_t n, const ot_ctx *c, uint32_t props, int context, amb_otg g) {
    if (!ot_props_ok(p, n, c, props, g)) return 1;
    int izwnj = c->table == 1 || (context && c->zwnj), izwj = context || c->zwj, ihid = c->table == 1;
    if (ot_ignorable(g) && (izwnj || !(g.uprops & AMB_U_ZWNJ)) && (izwj || !(g.uprops & AMB_U_ZWJ)) && (ihid || !(g.uprops & AMB_U_HIDDEN)))
        return 2;
    return 0;
}

/* a sequence to match: glyphs (kind 1), classes of the ClassDef at base (2), or Coverages whose
   offsets are from base (3), each a uint16 of the array at arr */
typedef struct ot_seq { int kind; size_t arr, base; } ot_seq;

static ot_seq ot_seq_of(int kind, size_t arr, size_t base) {
    ot_seq s;
    s.kind = kind;
    s.arr = arr;
    s.base = base;
    return s;
}

static int ot_seq_match(const uint8_t *p, size_t n, ot_seq s, uint32_t k, uint32_t g) {
    uint32_t v = ot_u16(p, n, s.arr + 2 * (size_t)k);
    if (s.kind == 1) return g == v;
    if (s.kind == 2) return ot_class(p, n, s.base, g) == v;
    return v && ot_coverage(p, n, s.base + v, g) >= 0;
}

/* one step of the iterator over a glyph: 1 matched, 0 not (a mismatch, or a glyph that may not be
   skipped), 2 skipped */
static int ot_step(const uint8_t *p, size_t n, const ot_ctx *c, uint32_t props, uint32_t mask, int context, ot_seq s, uint32_t k, amb_otg g) {
    int skip = ot_may_skip(p, n, c, props, context, g);
    if (skip == 1) return 2;
    int m = !(g.mask & mask) ? 0 : s.kind ? ot_seq_match(p, n, s, k, g.gid) : 2;
    if (m == 1 || (m == 2 && skip == 0)) return 1;
    if (skip == 0) return 0;
    return 2;
}

/* the iterator forward in the input from *pos: the next glyph matching item k of s (none: any
   glyph not skipped) into *pos, 1; 0 when there is none */
static int ot_next(OTX, size_t *pos, int context, ot_seq s, uint32_t k) {
    (void)out;
    (void)idx;
    uint32_t mask = context ? 0xFFFFFFFFu : c->mask;
    size_t i = *pos;
    while (i + 1 < len(*in)) {
        i++;
        int r = ot_step(p, n, c, c->props, mask, context, s, k, at(*in, i));
        if (r == 2) continue;
        *pos = i;
        return r;
    }
    return 0;
}

/* the iterator backward from *pos (exclusive) over what is before the input (GSUB's output, or
   the input itself in place), under props */
static int ot_prev(OTX, size_t *pos, int context, ot_seq s, uint32_t k, uint32_t props) {
    (void)idx;
    uint32_t mask = context ? 0xFFFFFFFFu : c->mask;
    size_t i = *pos;
    while (i > 0) {
        i--;
        amb_otg g = c->hout ? at(*out, i) : at(*in, i);
        int r = ot_step(p, n, c, props, mask, context, s, k, g);
        if (r == 2) continue;
        *pos = i;
        return r;
    }
    return 0;
}

/* how many glyphs precede the input's: GSUB's output, else the input's own */
static size_t ot_back_len(const ot_ctx *c, const vector<amb_otg> *out, size_t idx) { return c->hout ? len(*out) : idx; }

/* ---- the buffer ---- */

static void ot_next_glyph(OTX) {
    if (c->hout) push_back(out, at(*in, *idx));
    (*idx)++;
}

/* a glyph's cluster, and those of the glyphs in [start, end) of the input, their least
   (HarfBuzz's merge_clusters, its out-buffer continuation too) */
static void ot_merge_clusters(OTX, size_t start, size_t end) {
    if (end - start < 2 || end > len(*in)) return;
    uint32_t cl = at(*in, start).cluster;
    for (size_t i = start + 1; i < end; i++) if (at(*in, i).cluster < cl) cl = at(*in, i).cluster;
    if (cl != at(*in, end - 1).cluster)
        while (end < len(*in) && at(*in, end - 1).cluster == at(*in, end).cluster) end++;
    if (cl != at(*in, start).cluster)
        while (*idx < start && at(*in, start - 1).cluster == at(*in, start).cluster) start--;
    if (*idx == start && at(*in, start).cluster != cl && c->hout) {
        uint32_t old = at(*in, start).cluster;
        for (size_t i = len(*out); i > 0 && at(*out, i - 1).cluster == old; i--) (*out)[i - 1].cluster = cl;
    }
    for (size_t i = start; i < end; i++) (*in)[i].cluster = cl;
}

/* the current glyph given glyph g's classes (HarfBuzz's _set_glyph_class): substituted, and
   ligated or multiplied as it is made; GDEF's class of g, else the guess */
static void ot_set_class(OTX, uint32_t g, uint32_t guess, int ligature, int component) {
    (void)out;
    amb_otg cur = at(*in, *idx);
    uint32_t props = cur.props | AMB_P_SUBSTITUTED;
    if (ligature) props = (props | AMB_P_LIGATED) & ~(uint32_t)AMB_P_MULTIPLIED;
    if (component) props |= AMB_P_MULTIPLIED;
    if (c->gdef && ot_at(p, n, c->gdef, 4)) props = (props & 0x70) | ot_gdef_props(p, n, c->gdef, g);
    else if (guess) props = (props & 0x70) | guess;
    cur.props = (uint16_t)props;
    (*in)[*idx] = cur;
}

/* the current glyph replaced by g (to the output), the input past it */
static void ot_replace(OTX, uint32_t g) {
    ot_set_class(OTA, g, 0, 0, 0);
    amb_otg cur = at(*in, *idx);
    cur.gid = g;
    if (c->hout) push_back(out, cur);
    else (*in)[*idx] = cur;
    (*idx)++;
}

/* the current glyph deleted, its cluster merged with a neighbour's (HarfBuzz's delete_glyph) */
static void ot_delete(OTX) {
    uint32_t cl = at(*in, *idx).cluster;
    int survives = (*idx + 1 < len(*in) && at(*in, *idx + 1).cluster == cl) || (c->hout && len(*out) && back(*out).cluster == cl);
    if (!survives) {
        if (c->hout && len(*out)) {
            if (cl < back(*out).cluster) {
                uint32_t old = back(*out).cluster;
                for (size_t i = len(*out); i > 0 && at(*out, i - 1).cluster == old; i--) (*out)[i - 1].cluster = cl;
            }
        } else if (*idx + 1 < len(*in)) ot_merge_clusters(OTA, *idx, *idx + 2);
    }
    (*idx)++;
}

/* the buffer moved to the place i of out and in together (HarfBuzz's move_to): glyphs carried
   from in to out, or back */
static void ot_move_to(OTX, size_t i) {
    if (!c->hout) { *idx = i; return; }
    size_t ol = len(*out);
    if (ol < i) {
        size_t cnt = i - ol;
        for (size_t k = 0; k < cnt && *idx < len(*in); k++) {
            push_back(out, at(*in, *idx));
            (*idx)++;
        }
    } else if (ol > i) {
        size_t cnt = ol - i;
        if (*idx < cnt) {
            /* room made before the input for what goes back */
            amb_otg blank;
            memset(&blank, 0, sizeof blank);
            size_t more = cnt - *idx;
            insert4(in, 0, more, blank);
            *idx += more;
        }
        *idx -= cnt;
        for (size_t k = 0; k < cnt; k++) (*in)[*idx + k] = at(*out, i + k);
        resize(out, i);
    }
}

/* ---- matching an input sequence, its lookahead and backtrack ---- */

static uint32_t ot_lig_id(amb_otg g) { return (uint32_t)g.lig >> 5; }
static uint32_t ot_lig_comp(amb_otg g) { return (g.lig & AMB_LIG_BASE) ? 0 : (uint32_t)g.lig & 15; }
static uint32_t ot_lig_ncomp(amb_otg g) { return (g.props & AMB_P_LIGATURE) && (g.lig & AMB_LIG_BASE) ? (uint32_t)g.lig & 15 : 1; }

/* the input sequence from the current glyph: count glyphs in all, the first the current one,
   the others matching s's items 0.. (HarfBuzz's match_input, its ligature components' rules):
   their places into pos, where the match ends into *end, the ligature components into *total */
static int ot_match_input(OTX, uint32_t count, ot_seq s, size_t *pos, size_t *end, uint32_t *total) {
    if (count > OT_MAXCTX) return 0;
    amb_otg cur = at(*in, *idx);
    uint32_t first_id = ot_lig_id(cur), first_comp = ot_lig_comp(cur), tot = 0;
    int ligbase = 0;                                   /* 0 not looked at, 1 may not skip, 2 may skip */
    size_t q = *idx;
    for (uint32_t i = 1; i < count; i++) {
        if (ot_next(OTA, &q, 0, s, i - 1) != 1) return 0;
        pos[i] = q;
        amb_otg g = at(*in, q);
        uint32_t this_id = ot_lig_id(g), this_comp = ot_lig_comp(g);
        if (first_id && first_comp) {
            if (first_id != this_id || first_comp != this_comp) {
                if (!ligbase) {
                    int found = 0;
                    size_t j = c->hout ? len(*out) : *idx;
                    while (j && ot_lig_id(c->hout ? at(*out, j - 1) : at(*in, j - 1)) == first_id) {
                        if (ot_lig_comp(c->hout ? at(*out, j - 1) : at(*in, j - 1)) == 0) { j--; found = 1; break; }
                        j--;
                    }
                    ligbase = found && ot_may_skip(p, n, c, c->props, 0, c->hout ? at(*out, j) : at(*in, j)) == 1 ? 2 : 1;
                }
                if (ligbase == 1) return 0;
            }
        } else if (this_id && this_comp && this_id != first_id) return 0;
        tot += ot_lig_ncomp(g);
    }
    *end = q + 1;
    *total = tot + ot_lig_ncomp(cur);
    pos[0] = *idx;
    return 1;
}

/* the lookahead after the input that ends at end: count glyphs matching s */
static int ot_match_ahead(OTX, uint32_t count, ot_seq s, size_t end) {
    size_t q = end - 1;
    for (uint32_t i = 0; i < count; i++)
        if (ot_next(OTA, &q, 1, s, i) != 1) return 0;
    return 1;
}

/* the backtrack before the current glyph: count glyphs matching s, the nearest first */
static int ot_match_back(OTX, uint32_t count, ot_seq s) {
    size_t q = ot_back_len(c, out, *idx);
    for (uint32_t i = 0; i < count; i++)
        if (ot_prev(OTA, &q, 1, s, i, c->props) != 1) return 0;
    return 1;
}

/* ---- GSUB ---- */

/* the matched input ligated into g (HarfBuzz's ligate_input): its clusters merged; a base and
   marks a base, marks alone a mark ligature, else a ligature of a new id whose components the
   marks between them keep */
static void ot_ligate(OTX, uint32_t count, size_t *pos, size_t end, uint32_t g, uint32_t total) {
    ot_merge_clusters(OTA, *idx, end);
    int is_base = (at(*in, pos[0]).props & AMB_P_BASE) != 0, is_mark = (at(*in, pos[0]).props & AMB_P_MARK) != 0;
    for (uint32_t i = 1; i < count; i++)
        if (!(at(*in, pos[i]).props & AMB_P_MARK)) { is_base = 0; is_mark = 0; break; }
    int is_lig = !is_base && !is_mark;
    uint32_t klass = is_lig ? AMB_P_LIGATURE : 0, lig_id = 0;
    if (is_lig) {
        c->serial = c->serial % 7 + 1;
        lig_id = c->serial;
    }
    amb_otg cur = at(*in, *idx);
    uint32_t last_id = ot_lig_id(cur), last_n = ot_lig_ncomp(cur), so_far = last_n;
    if (is_lig) {
        cur.lig = (uint8_t)(lig_id << 5 | AMB_LIG_BASE | (total & 15));
        if (cur.uprops & AMB_U_MN) cur.uprops = (uint16_t)(cur.uprops & ~(uint32_t)(AMB_U_MN | AMB_U_MARK | AMB_U_CONT));
        (*in)[*idx] = cur;
    }
    ot_set_class(OTA, g, klass, 1, 0);
    cur = at(*in, *idx);
    cur.gid = g;
    push_back(out, cur);
    (*idx)++;
    for (uint32_t i = 1; i < count; i++) {
        while (*idx < pos[i] && *idx < len(*in)) {
            if (is_lig) {
                amb_otg m = at(*in, *idx);
                uint32_t this_comp = ot_lig_comp(m);
                if (this_comp == 0) this_comp = last_n;
                uint32_t nc = so_far - last_n + (this_comp < last_n ? this_comp : last_n);
                m.lig = (uint8_t)(lig_id << 5 | (nc & 15));
                (*in)[*idx] = m;
            }
            ot_next_glyph(OTA);
        }
        if (*idx >= len(*in)) break;
        last_id = ot_lig_id(at(*in, *idx));
        last_n = ot_lig_ncomp(at(*in, *idx));
        so_far += last_n;
        (*idx)++;
    }
    if (!is_mark && last_id)
        for (size_t i = *idx; i < len(*in); i++) {
            amb_otg m = at(*in, i);
            if (ot_lig_id(m) != last_id) break;
            uint32_t this_comp = ot_lig_comp(m);
            if (!this_comp) break;
            uint32_t nc = so_far - last_n + (this_comp < last_n ? this_comp : last_n);
            m.lig = (uint8_t)(lig_id << 5 | (nc & 15));
            (*in)[i] = m;
        }
}

static int ot_apply_sub(OTX, size_t st, uint32_t type);
static int ot_recurse(OTX, uint32_t li);

/* a contextual rule's lookups applied at the matched input's places (HarfBuzz's apply_lookup):
   the buffer moved to each in turn, the places after it moved by what it added or took away */
static void ot_apply_records(OTX, uint32_t count, size_t *pos, size_t recs, uint32_t nrec, size_t match_end) {
    size_t bl = ot_back_len(c, out, *idx);
    long end = (long)bl + (long)match_end - (long)*idx;
    long delta0 = (long)bl - (long)*idx;
    for (uint32_t j = 0; j < count; j++) pos[j] = (size_t)((long)pos[j] + delta0);
    for (uint32_t r = 0; r < nrec; r++) {
        uint32_t si = ot_u16(p, n, recs + 4 * (size_t)r), li = ot_u16(p, n, recs + 4 * (size_t)r + 2);
        if (si >= count) continue;
        size_t orig_len = ot_back_len(c, out, *idx) + (len(*in) - *idx);
        if (pos[si] >= orig_len) continue;
        ot_move_to(OTA, pos[si]);
        if (!ot_recurse(OTA, li)) continue;
        size_t new_len = ot_back_len(c, out, *idx) + (len(*in) - *idx);
        long delta = (long)new_len - (long)orig_len;
        if (!delta) continue;
        end += delta;
        if (end < (long)pos[si]) {
            delta += (long)pos[si] - end;
            end = (long)pos[si];
        }
        uint32_t next = si + 1;
        if (delta > 0) {
            if ((long)count + delta > OT_MAXCTX) break;
        } else {
            if (delta < (long)next - (long)count) delta = (long)next - (long)count;
            next = (uint32_t)((long)next - delta);
        }
        /* the places after next shifted by delta, those made filled in after the current one */
        if (delta > 0) for (long k = (long)count - 1; k >= (long)next; k--) pos[k + delta] = pos[k];
        else for (uint32_t k = next; k < count; k++) pos[(long)k + delta] = pos[k];
        next = (uint32_t)((long)next + delta);
        count = (uint32_t)((long)count + delta);
        for (uint32_t k = si + 1; k < next; k++) pos[k] = pos[k - 1] + 1;
        for (; next < count; next++) pos[next] = (size_t)((long)pos[next] + delta);
    }
    ot_move_to(OTA, (size_t)(end < 0 ? 0 : end));
}

/* a (chained) contextual rule at rule: its backtrack, input and lookahead of kind (1 glyphs, 2
   classes of bcd, icd, lcd); applied when they match. 1 when it applied */
static int ot_rule(OTX, size_t rule, int chain, int kind, size_t bcd, size_t icd, size_t lcd) {
    uint32_t nb = 0, ni, nl = 0, nr;
    size_t b = 0, i, l = 0, recs;
    if (chain) {
        nb = ot_u16(p, n, rule);
        b = rule + 2;
        ni = ot_u16(p, n, b + 2 * (size_t)nb);
        i = b + 2 * (size_t)nb + 2;
        uint32_t ni1 = ni ? ni - 1 : 0;
        nl = ot_u16(p, n, i + 2 * (size_t)ni1);
        l = i + 2 * (size_t)ni1 + 2;
        nr = ot_u16(p, n, l + 2 * (size_t)nl);
        recs = l + 2 * (size_t)nl + 2;
    } else {
        ni = ot_u16(p, n, rule);
        nr = ot_u16(p, n, rule + 2);
        i = rule + 4;
        recs = i + 2 * (size_t)(ni ? ni - 1 : 0);
    }
    if (!ni) return 0;
    size_t pos[OT_MAXCTX], end;
    uint32_t total;
    if (!ot_match_input(OTA, ni, ot_seq_of(kind, i, icd), pos, &end, &total)) return 0;
    if (nl && !ot_match_ahead(OTA, nl, ot_seq_of(kind, l, lcd), end)) return 0;
    if (nb && !ot_match_back(OTA, nb, ot_seq_of(kind, b, bcd))) return 0;
    ot_apply_records(OTA, ni, pos, recs, nr, end);
    return 1;
}

/* a contextual or chained contextual subtable (GSUB 5 and 6, GPOS 7 and 8) at the current glyph */
static int ot_context(OTX, size_t st, int chain) {
    uint32_t fmt = ot_u16(p, n, st), g = at(*in, *idx).gid;
    if (fmt == 1 || fmt == 2) {
        int ci = ot_coverage(p, n, ot_at(p, n, st, 2), g);
        if (ci < 0) return 0;
        size_t bcd = 0, icd = 0, lcd = 0, arr;
        uint32_t nsets, which = (uint32_t)ci;
        if (fmt == 1) {
            nsets = ot_u16(p, n, st + 4);
            arr = st + 6;
        } else if (chain) {
            bcd = ot_at(p, n, st, 4);
            icd = ot_at(p, n, st, 6);
            lcd = ot_at(p, n, st, 8);
            nsets = ot_u16(p, n, st + 10);
            arr = st + 12;
            which = ot_class(p, n, icd, g);
        } else {
            icd = ot_at(p, n, st, 4);
            nsets = ot_u16(p, n, st + 6);
            arr = st + 8;
            which = ot_class(p, n, icd, g);
        }
        if (which >= nsets) return 0;
        size_t set = ot_at(p, n, st, arr - st + 2 * (size_t)which);
        if (!set) return 0;
        uint32_t nrules = ot_u16(p, n, set);
        for (uint32_t r = 0; r < nrules; r++) {
            size_t rule = ot_at(p, n, set, 2 + 2 * (size_t)r);
            if (rule && ot_rule(OTA, rule, chain, fmt == 1 ? 1 : 2, bcd, icd, lcd)) return 1;
        }
        return 0;
    }
    if (fmt != 3) return 0;
    uint32_t nb = 0, ni, nl = 0, nr;
    size_t b = 0, i, l = 0, recs;
    if (chain) {
        nb = ot_u16(p, n, st + 2);
        b = st + 4;
        ni = ot_u16(p, n, b + 2 * (size_t)nb);
        i = b + 2 * (size_t)nb + 2;
        nl = ot_u16(p, n, i + 2 * (size_t)ni);
        l = i + 2 * (size_t)ni + 2;
        nr = ot_u16(p, n, l + 2 * (size_t)nl);
        recs = l + 2 * (size_t)nl + 2;
    } else {
        ni = ot_u16(p, n, st + 2);
        nr = ot_u16(p, n, st + 4);
        i = st + 6;
        recs = i + 2 * (size_t)ni;
    }
    if (!ni || ot_coverage(p, n, ot_at(p, n, st, i - st), g) < 0) return 0;
    size_t pos[OT_MAXCTX], end;
    uint32_t total;
    if (!ot_match_input(OTA, ni, ot_seq_of(3, i + 2, st), pos, &end, &total)) return 0;
    if (nl && !ot_match_ahead(OTA, nl, ot_seq_of(3, l, st), end)) return 0;
    if (nb && !ot_match_back(OTA, nb, ot_seq_of(3, b, st))) return 0;
    ot_apply_records(OTA, ni, pos, recs, nr, end);
    return 1;
}

/* a GSUB subtable of a type at the current glyph: 1 when it applied */
static int ot_gsub(OTX, size_t st, uint32_t type) {
    uint32_t fmt = ot_u16(p, n, st), g = at(*in, *idx).gid;
    if (type == 5 || type == 6) return ot_context(OTA, st, type == 6);
    if (type == 7) {
        uint32_t inner = ot_u16(p, n, st + 2);
        return inner != 7 && fmt == 1 ? ot_gsub(OTA, st + ot_u32(p, n, st + 4), inner) : 0;
    }
    int ci = ot_coverage(p, n, ot_at(p, n, st, 2), g);
    if (ci < 0) return 0;
    if (type == 1) {
        uint32_t s;
        if (fmt == 1) s = (g + (uint32_t)ot_s16(p, n, st + 4)) & 0xFFFF;
        else if (fmt == 2 && (uint32_t)ci < ot_u16(p, n, st + 4)) s = ot_u16(p, n, st + 6 + 2 * (size_t)ci);
        else return 0;
        ot_replace(OTA, s);
        return 1;
    }
    if (type == 2 || type == 3) {
        if ((uint32_t)ci >= ot_u16(p, n, st + 4)) return 0;
        size_t sq = ot_at(p, n, st, 6 + 2 * (size_t)ci);
        uint32_t cnt = ot_u16(p, n, sq);
        if (type == 3) {
            if (!cnt) return 0;
            ot_replace(OTA, ot_u16(p, n, sq + 2));
            return 1;
        }
        if (cnt == 1) { ot_replace(OTA, ot_u16(p, n, sq + 2)); return 1; }
        if (cnt == 0) { ot_delete(OTA); return 1; }
        amb_otg cur = at(*in, *idx);
        uint32_t klass = (cur.props & AMB_P_LIGATURE) ? AMB_P_BASE : 0, lig_id = ot_lig_id(cur);
        for (uint32_t k = 0; k < cnt; k++) {
            uint32_t s = ot_u16(p, n, sq + 2 + 2 * (size_t)k);
            if (!lig_id) {
                amb_otg x = at(*in, *idx);
                x.lig = (uint8_t)(k & 15);
                (*in)[*idx] = x;
            }
            ot_set_class(OTA, s, klass, 0, 1);
            amb_otg o = at(*in, *idx);
            o.gid = s;
            push_back(out, o);
        }
        (*idx)++;
        return 1;
    }
    if (type == 4) {
        if ((uint32_t)ci >= ot_u16(p, n, st + 4)) return 0;
        size_t set = ot_at(p, n, st, 6 + 2 * (size_t)ci);
        uint32_t nlig = ot_u16(p, n, set);
        for (uint32_t k = 0; k < nlig; k++) {
            size_t lg = ot_at(p, n, set, 2 + 2 * (size_t)k);
            uint32_t glyph = ot_u16(p, n, lg), cnt = ot_u16(p, n, lg + 2);
            if (!cnt) continue;
            if (cnt == 1) { ot_replace(OTA, glyph); return 1; }
            size_t pos[OT_MAXCTX], end;
            uint32_t total;
            if (!ot_match_input(OTA, cnt, ot_seq_of(1, lg + 4, 0), pos, &end, &total)) continue;
            ot_ligate(OTA, cnt, pos, end, glyph, total);
            return 1;
        }
        return 0;
    }
    if (type == 8) {
        /* reverse chained single: in place, its backtrack and lookahead by coverages */
        if (c->nest != OT_NEST) return 0;
        uint32_t nb = ot_u16(p, n, st + 4);
        size_t b = st + 6;
        uint32_t nl = ot_u16(p, n, b + 2 * (size_t)nb);
        size_t l = b + 2 * (size_t)nb + 2;
        uint32_t ns = ot_u16(p, n, l + 2 * (size_t)nl);
        size_t subs = l + 2 * (size_t)nl + 2;
        if ((uint32_t)ci >= ns) return 0;
        if (nb && !ot_match_back(OTA, nb, ot_seq_of(3, b, st))) return 0;
        if (nl && !ot_match_ahead(OTA, nl, ot_seq_of(3, l, st), *idx + 1)) return 0;
        uint32_t s = ot_u16(p, n, subs + 2 * (size_t)ci);
        ot_set_class(OTA, s, 0, 0, 0);
        amb_otg cur = at(*in, *idx);
        cur.gid = s;
        (*in)[*idx] = cur;
        return 1;
    }
    return 0;
}

/* ---- GPOS ---- */

/* a ValueRecord of format fmt at vr added to glyph i's position (its Device tables passed over) */
static void ot_value(const uint8_t *p, size_t n, size_t vr, uint32_t fmt, vector<amb_otg> *in, size_t i) {
    amb_otg g = at(*in, i);
    size_t q = vr;
    if (fmt & 1) { g.dx += ot_s16(p, n, q); q += 2; }
    if (fmt & 2) { g.dy += ot_s16(p, n, q); q += 2; }
    if (fmt & 4) { g.ax += ot_s16(p, n, q); q += 2; }
    (*in)[i] = g;
}

static uint32_t ot_vsize(uint32_t fmt) {
    uint32_t k = 0;
    for (int b = 0; b < 8; b++) if (fmt & (1u << b)) k += 2;
    return k;
}

/* an Anchor's x and y: 1, or 0 for a null offset */
static int ot_anchor(const uint8_t *p, size_t n, size_t a, int32_t *x, int32_t *y) {
    if (!a) return 0;
    *x = ot_s16(p, n, a + 2);
    *y = ot_s16(p, n, a + 4);
    return 1;
}

/* the mark at the current glyph attached to glyph j by the mark record mi of the MarkArray at ma
   and the anchor of the record at row for its class (offsets from rb): 1 when the anchor is there */
static int ot_mark_attach(OTX, size_t ma, uint32_t mi, size_t rb, size_t row, uint32_t nclass, size_t j) {
    (void)out;
    uint32_t cls = ot_u16(p, n, ma + 2 + 4 * (size_t)mi);
    if (cls >= nclass) return 0;
    size_t base_anchor = ot_at(p, n, rb, row - rb + 2 * (size_t)cls);
    size_t mark_anchor = ot_at(p, n, ma, 4 + 4 * (size_t)mi);
    int32_t bx, by, mx, my;
    if (!ot_anchor(p, n, base_anchor, &bx, &by) || !ot_anchor(p, n, mark_anchor, &mx, &my)) return 0;
    amb_otg g = at(*in, *idx);
    g.dx = bx - mx;
    g.dy = by - my;
    g.attach = 1;
    g.chain = (int32_t)j - (int32_t)*idx;
    (*in)[*idx] = g;
    (*idx)++;
    return 1;
}

/* a MarkBasePos's base may be this glyph: not one of a multiple substitution's sequence but its
   first (HarfBuzz's accept) */
static int ot_accept_base(const vector<amb_otg> *in, size_t i) {
    amb_otg g = at(*in, i);
    if (!(g.props & AMB_P_MULTIPLIED) || ot_lig_comp(g) == 0 || i == 0) return 1;
    amb_otg h = at(*in, i - 1);
    return (h.props & AMB_P_MARK) || !(h.props & AMB_P_MULTIPLIED) || ot_lig_id(g) != ot_lig_id(h) || ot_lig_comp(g) != ot_lig_comp(h) + 1;
}

/* a cursive chain's offsets turned the other way from glyph i, to its new parent (HarfBuzz's
   reverse_cursive_minor_offset) */
static void ot_reverse_cursive(vector<amb_otg> *in, size_t i, size_t parent, int depth) {
    amb_otg g = at(*in, i);
    if (!g.chain || g.attach != 2 || depth > 64) return;
    int32_t chain = g.chain;
    g.chain = 0;
    (*in)[i] = g;
    long jj = (long)i + chain;
    if (jj < 0 || (size_t)jj >= len(*in) || (size_t)jj == parent) return;
    size_t j = (size_t)jj;
    ot_reverse_cursive(in, j, parent, depth + 1);
    amb_otg h = at(*in, j);
    h.dy = -at(*in, i).dy;
    h.chain = -chain;
    h.attach = 2;
    (*in)[j] = h;
}

/* a GPOS subtable of a type at the current glyph: 1 when it applied */
static int ot_gpos(OTX, size_t st, uint32_t type) {
    uint32_t fmt = ot_u16(p, n, st), g = at(*in, *idx).gid;
    if (type == 7 || type == 8) return ot_context(OTA, st, type == 8);
    if (type == 9) {
        uint32_t inner = ot_u16(p, n, st + 2);
        return inner != 9 && fmt == 1 ? ot_gpos(OTA, st + ot_u32(p, n, st + 4), inner) : 0;
    }
    int ci = ot_coverage(p, n, ot_at(p, n, st, 2), g);
    if (ci < 0) return 0;
    if (type == 1) {
        uint32_t vf = ot_u16(p, n, st + 4);
        if (fmt == 1) ot_value(p, n, st + 6, vf, in, *idx);
        else if (fmt == 2 && (uint32_t)ci < ot_u16(p, n, st + 6)) ot_value(p, n, st + 8 + (size_t)ci * ot_vsize(vf), vf, in, *idx);
        else return 0;
        (*idx)++;
        return 1;
    }
    if (type == 2) {
        size_t q = *idx;
        if (ot_next(OTA, &q, 0, ot_seq_of(0, 0, 0), 0) != 1) return 0;
        uint32_t vf1 = ot_u16(p, n, st + 4), vf2 = ot_u16(p, n, st + 6), s1 = ot_vsize(vf1), s2 = ot_vsize(vf2);
        uint32_t g2 = at(*in, q).gid;
        if (fmt == 1) {
            if ((uint32_t)ci >= ot_u16(p, n, st + 8)) return 0;
            size_t set = ot_at(p, n, st, 10 + 2 * (size_t)ci);
            uint32_t cnt = ot_u16(p, n, set), lo = 0, hi = cnt, rs = 2 + s1 + s2;
            size_t rec = 0;
            while (lo < hi) {
                uint32_t mid = (lo + hi) / 2;
                size_t r = set + 2 + (size_t)mid * rs;
                uint32_t x = ot_u16(p, n, r);
                if (x == g2) { rec = r; break; }
                if (x < g2) lo = mid + 1; else hi = mid;
            }
            if (!rec) return 0;
            ot_value(p, n, rec + 2, vf1, in, *idx);
            ot_value(p, n, rec + 2 + s1, vf2, in, q);
        } else if (fmt == 2) {
            uint32_t k1 = ot_class(p, n, ot_at(p, n, st, 8), g), k2 = ot_class(p, n, ot_at(p, n, st, 10), g2);
            uint32_t c1 = ot_u16(p, n, st + 12), c2 = ot_u16(p, n, st + 14);
            if (k1 >= c1 || k2 >= c2) return 0;
            size_t v = st + 16 + ((size_t)k1 * c2 + k2) * (s1 + s2);
            if (s1) ot_value(p, n, v, vf1, in, *idx);
            if (s2) ot_value(p, n, v + s1, vf2, in, q);
        } else return 0;
        *idx = s2 ? q + 1 : q;
        return 1;
    }
    if (type == 3) {
        uint32_t cnt = ot_u16(p, n, st + 4);
        if ((uint32_t)ci >= cnt) return 0;
        size_t entry = ot_at(p, n, st, 6 + 4 * (size_t)ci);
        if (!entry) return 0;
        size_t q = *idx;
        if (ot_prev(OTA, &q, 0, ot_seq_of(0, 0, 0), 0, c->props) != 1) return 0;
        int pi = ot_coverage(p, n, ot_at(p, n, st, 2), at(*in, q).gid);
        if (pi < 0 || (uint32_t)pi >= cnt) return 0;
        size_t exit_a = ot_at(p, n, st, 8 + 4 * (size_t)pi);
        if (!exit_a) return 0;
        int32_t ex, ey, nx, ny;
        if (!ot_anchor(p, n, exit_a, &ex, &ey) || !ot_anchor(p, n, entry, &nx, &ny)) return 0;
        size_t i = q, j = *idx;
        amb_otg gi = at(*in, i), gj = at(*in, j);
        if (!c->rtl) {
            gi.ax = ex + gi.dx;
            int32_t d = nx + gj.dx;
            gj.ax -= d;
            gj.dx -= d;
        } else {
            int32_t d = ex + gi.dx;
            gi.ax -= d;
            gi.dx -= d;
            gj.ax = nx + gj.dx;
        }
        (*in)[i] = gi;
        (*in)[j] = gj;
        size_t child = i, parent = j;
        int32_t yo = ny - ey;
        if (!(c->props & 1)) {
            child = j;
            parent = i;
            yo = -yo;
        }
        ot_reverse_cursive(in, child, parent, 0);
        amb_otg ch = at(*in, child);
        ch.attach = 2;
        ch.chain = (int32_t)parent - (int32_t)child;
        ch.dy = yo;
        (*in)[child] = ch;
        amb_otg pa = at(*in, parent);
        if (pa.chain == -ch.chain) {
            pa.chain = 0;
            pa.dy = 0;
            (*in)[parent] = pa;
        }
        (*idx)++;
        return 1;
    }
    if (type == 4 || type == 5 || type == 6) {
        uint32_t nclass = ot_u16(p, n, st + 6);
        size_t ma = ot_at(p, n, st, 8), ba = ot_at(p, n, st, 10), bcov = ot_at(p, n, st, 4);
        if ((uint32_t)ci >= ot_u16(p, n, ma)) return 0;
        if (type == 6) {
            /* mark to mark: the nearest mark before, of the same base or ligature component */
            size_t q = *idx;
            if (ot_prev(OTA, &q, 0, ot_seq_of(0, 0, 0), 0, c->props & ~(uint32_t)0x0E) != 1) return 0;
            amb_otg m2 = at(*in, q), m1 = at(*in, *idx);
            if (!(m2.props & AMB_P_MARK)) return 0;
            uint32_t id1 = ot_lig_id(m1), id2 = ot_lig_id(m2), comp1 = ot_lig_comp(m1), comp2 = ot_lig_comp(m2);
            int good = id1 == id2 ? (id1 == 0 || comp1 == comp2) : ((id1 > 0 && !comp1) || (id2 > 0 && !comp2));
            if (!good) return 0;
            int bi = ot_coverage(p, n, bcov, m2.gid);
            if (bi < 0 || (uint32_t)bi >= ot_u16(p, n, ba)) return 0;
            return ot_mark_attach(OTA, ma, (uint32_t)ci, ba, ba + 2 + 2 * (size_t)bi * nclass, nclass, q);
        }
        /* to a base or a ligature: the nearest glyph before that is not a mark */
        uint32_t saved = c->props;
        c->props = 8;
        size_t j = *idx;
        int found = 0;
        while (j > 0) {
            amb_otg b = at(*in, j - 1);
            int r = ot_step(p, n, c, c->props, c->mask, 0, ot_seq_of(0, 0, 0), 0, b);
            if (r == 1 && type == 4 && !ot_accept_base(in, j - 1) && ot_coverage(p, n, bcov, b.gid) < 0) r = 2;
            if (r == 0) break;
            if (r == 1) { found = 1; break; }
            j--;
        }
        c->props = saved;
        if (!found) return 0;
        size_t bidx = j - 1;
        amb_otg base = at(*in, bidx);
        int bi = ot_coverage(p, n, bcov, base.gid);
        if (bi < 0 || (uint32_t)bi >= ot_u16(p, n, ba)) return 0;
        if (type == 4) return ot_mark_attach(OTA, ma, (uint32_t)ci, ba, ba + 2 + 2 * (size_t)bi * nclass, nclass, bidx);
        size_t la = ot_at(p, n, ba, 2 + 2 * (size_t)bi);
        uint32_t ncomp = ot_u16(p, n, la);
        if (!ncomp) return 0;
        amb_otg mk = at(*in, *idx);
        uint32_t lig_id = ot_lig_id(base), mark_id = ot_lig_id(mk), mark_comp = ot_lig_comp(mk), comp;
        if (lig_id && lig_id == mark_id && mark_comp > 0) comp = (mark_comp < ncomp ? mark_comp : ncomp) - 1;
        else comp = ncomp - 1;
        return ot_mark_attach(OTA, ma, (uint32_t)ci, la, la + 2 + 2 * (size_t)comp * nclass, nclass, bidx);
    }
    return 0;
}

/* ---- lookups ---- */

/* a lookup's table: its offset (0 for none) */
static size_t ot_lookup_at(const uint8_t *p, size_t n, const ot_ctx *c, uint32_t li) {
    if (!c->lookups || li >= ot_u16(p, n, c->lookups)) return 0;
    return ot_at(p, n, c->lookups, 2 + 2 * (size_t)li);
}

/* a lookup's properties: its flag, its mark filtering set << 16 */
static uint32_t ot_lookup_props(const uint8_t *p, size_t n, size_t lk) {
    uint32_t flag = ot_u16(p, n, lk + 2), props = flag;
    if (flag & 0x10) props |= ot_u16(p, n, lk + 6 + 2 * (size_t)ot_u16(p, n, lk + 4)) << 16;
    return props;
}

/* the lookup's subtables tried at the current glyph, the first that applies taken */
static int ot_lookup_apply(OTX, size_t lk) {
    uint32_t type = ot_u16(p, n, lk), cnt = ot_u16(p, n, lk + 4);
    for (uint32_t k = 0; k < cnt; k++) {
        size_t st = ot_at(p, n, lk, 6 + 2 * (size_t)k);
        if (!st) continue;
        if (c->table ? ot_gpos(OTA, st, type) : ot_gsub(OTA, st, type)) return 1;
    }
    return 0;
}

/* a lookup a contextual rule names, applied at the current glyph (HarfBuzz's recurse) */
static int ot_recurse(OTX, uint32_t li) {
    if (c->nest <= 0) return 0;
    size_t lk = ot_lookup_at(p, n, c, li);
    if (!lk || *idx >= len(*in)) return 0;
    uint32_t saved = c->props;
    c->props = ot_lookup_props(p, n, lk);
    c->nest--;
    int r = ot_lookup_apply(OTA, lk);
    c->nest++;
    c->props = saved;
    return r;
}

/* whether a lookup is a reverse chained single substitution (through an extension) */
static int ot_lookup_reverse(const uint8_t *p, size_t n, size_t lk) {
    uint32_t type = ot_u16(p, n, lk);
    if (type == 7 && ot_u16(p, n, lk + 4)) {
        size_t st = ot_at(p, n, lk, 6);
        type = st ? ot_u16(p, n, st + 2) : 0;
    }
    return type == 8;
}

/* a lookup applied over the buffer: forward through an output buffer (GSUB), in place (GPOS),
   backward in place (a reverse chained substitution) */
static void ot_lookup_string(const uint8_t *p, size_t n, ot_ctx *c, vector<amb_otg> *buf, size_t lk) {
    vector<amb_otg> out;
    size_t idx = 0;
    if (!len(*buf) || !c->mask) return;
    c->props = ot_lookup_props(p, n, lk);
    c->nest = OT_NEST;
    if (c->table == 0 && ot_lookup_reverse(p, n, lk)) {
        c->hout = 0;
        size_t i = len(*buf);
        while (i > 0) {
            i--;
            idx = i;
            amb_otg g = at(*buf, i);
            if ((g.mask & c->mask) && ot_props_ok(p, n, c, c->props, g)) ot_lookup_apply(p, n, c, buf, &out, &idx, lk);
        }
        return;
    }
    c->hout = c->table == 0;
    while (idx < len(*buf)) {
        amb_otg g = at(*buf, idx);
        int applied = 0;
        if ((g.mask & c->mask) && ot_props_ok(p, n, c, c->props, g)) applied = ot_lookup_apply(p, n, c, buf, &out, &idx, lk);
        if (!applied) ot_next_glyph(p, n, c, buf, &out, &idx);
    }
    if (c->hout) swap(buf, &out);
    c->hout = 0;
}

/* ---- the run ---- */

/* HarfBuzz's Arabic state machine: each state's entry for each joining type (U, L, R, D,
   Alaph, Dalath-Rish): the previous glyph's form, this one's, the next state (7: none) */
static const uint8_t ot_arabic_states[7][6][3] = {
    { { 7, 7, 0 }, { 7, 0, 2 }, { 7, 0, 1 }, { 7, 0, 2 }, { 7, 0, 1 }, { 7, 0, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 7, 0, 1 }, { 7, 0, 2 }, { 7, 2, 5 }, { 7, 0, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 6, 1, 1 }, { 6, 1, 3 }, { 6, 1, 4 }, { 6, 1, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 4, 1, 1 }, { 4, 1, 3 }, { 4, 1, 4 }, { 4, 1, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 5, 0, 1 }, { 5, 0, 2 }, { 5, 2, 5 }, { 5, 0, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 0, 0, 1 }, { 0, 0, 2 }, { 0, 2, 5 }, { 0, 0, 6 } },
    { { 7, 7, 0 }, { 7, 0, 2 }, { 7, 0, 1 }, { 7, 0, 2 }, { 7, 3, 5 }, { 7, 0, 6 } },
};

/* the joining forms of an Arabic run (its glyphs' actions, then their masks), the code points
   around it its context */
static void ot_arabic_joining(vector<amb_otg> *buf, const uint32_t *pre, size_t npre, const uint32_t *post, size_t npost) {
    size_t cnt = len(*buf), prev = (size_t)-1;
    uint32_t state = 0;
    for (size_t i = 0; i < npre; i++) {
        int jt = amb_joining_of(pre[i]);
        if (jt == 7) continue;
        state = ot_arabic_states[state][jt][2];
        break;
    }
    for (size_t i = 0; i < cnt; i++) {
        amb_otg g = at(*buf, i);
        int jt = amb_joining_of(g.cp);
        if (jt == 7) {
            g.action = 7;
            (*buf)[i] = g;
            continue;
        }
        if (jt > 5) jt = 0;
        uint8_t pa = ot_arabic_states[state][jt][0], ca = ot_arabic_states[state][jt][1];
        if (pa != 7 && prev != (size_t)-1) {
            amb_otg h = at(*buf, prev);
            h.action = pa;
            (*buf)[prev] = h;
        }
        g.action = ca;
        (*buf)[i] = g;
        prev = i;
        state = ot_arabic_states[state][jt][2];
    }
    for (size_t i = 0; i < npost; i++) {
        int jt = amb_joining_of(post[i]);
        if (jt == 7) continue;
        if (jt > 5) jt = 0;
        uint8_t pa = ot_arabic_states[state][jt][0];
        if (pa != 7 && prev != (size_t)-1) {
            amb_otg h = at(*buf, prev);
            h.action = pa;
            (*buf)[prev] = h;
        }
        break;
    }
    for (size_t i = 0; i < cnt; i++) {
        amb_otg g = at(*buf, i);
        if (g.action < 7) g.mask |= (uint32_t)AMB_M_ISOL << g.action;
        (*buf)[i] = g;
    }
}

void amb_ot_substitute(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags, const uint32_t *pre, size_t npre,
                       const uint32_t *post, size_t npost, vector<amb_otg> *buf) {
    uint32_t pi = ot_plan_of(p, n, t, script, flags);
    ot_plan pl = at(ot_plans, pi);
    int classes = t->gdef && ot_at(p, n, t->gdef, 4);
    for (size_t i = 0; i < len(*buf); i++) {
        amb_otg g = at(*buf, i);
        g.ccc = (uint8_t)amb_ccc_of(g.cp);
        g.uprops = (uint16_t)amb_uprops_of(g.cp, i ? at(*buf, i - 1).cp : 0);
        if (classes) g.props = (uint16_t)ot_gdef_props(p, n, t->gdef, g.gid);
        else g.props = (g.uprops & AMB_U_MN) && !(g.uprops & AMB_U_IGNORABLE) ? AMB_P_MARK : AMB_P_BASE;
        g.lig = 0;
        g.action = 7;
        g.attach = 0;
        g.chain = 0;
        (*buf)[i] = g;
    }
    if (pl.info & AMB_PI_ARABIC) ot_arabic_joining(buf, pre, npre, post, npost);
    ot_ctx c;
    memset(&c, 0, sizeof c);
    c.gdef = t->gdef;
    c.lookups = t->gsub ? ot_at(p, n, t->gsub, 8) : 0;
    c.table = 0;
    c.rtl = (flags & AMB_SH_BACKWARD) != 0;
    for (uint32_t k = 0; k < pl.nsub; k++) {
        ot_lookup lk = at(ot_lks, pl.first + k);
        size_t lt = ot_lookup_at(p, n, &c, lk.index);
        if (!lt) continue;
        c.mask = lk.mask;
        c.zwj = lk.zwj;
        c.zwnj = lk.zwnj;
        ot_lookup_string(p, n, &c, buf, lt);
    }
}

/* the kern table's pairs over the run (HarfBuzz's kern machine: marks passed over; a pair's
   value half to the first glyph's advance, half to the second's advance and offset) */
static void ot_kern_table(const uint8_t *p, size_t n, const amb_otf *t, vector<amb_otg> *buf) {
    if (t->kern + 4 > n || ot_u16(p, n, t->kern) != 0) return;
    uint32_t ntab = ot_u16(p, n, t->kern + 2);
    size_t st = t->kern + 4, end = (size_t)t->kern + t->kern_len;
    ot_ctx c;
    memset(&c, 0, sizeof c);
    c.table = 1;
    c.mask = AMB_M_GLOBAL;
    c.props = 8;
    vector<amb_otg> out;
    for (uint32_t s = 0; s < ntab && st + 6 <= end; s++) {
        uint32_t sublen = ot_u16(p, n, st + 2), cov = ot_u16(p, n, st + 4);
        if ((cov >> 8) == 0 && (cov & 1) && !(cov & 4)) {
            uint32_t np = ot_u16(p, n, st + 6);
            for (size_t i = 0; i < len(*buf);) {
                size_t idx = i, q = i;
                if (!(at(*buf, i).mask & AMB_M_GLOBAL) || ot_next(p, n, &c, buf, &out, &idx, &q, 0, ot_seq_of(0, 0, 0), 0) != 1) { i++; continue; }
                uint32_t pair = at(*buf, i).gid << 16 | at(*buf, q).gid, lo = 0, hi = np;
                int32_t kv = 0;
                while (lo < hi) {
                    uint32_t mid = (lo + hi) / 2;
                    size_t r = st + 14 + 6 * (size_t)mid;
                    uint32_t x = ot_u32(p, n, r);
                    if (x == pair) { kv = ot_s16(p, n, r + 4); break; }
                    if (x < pair) lo = mid + 1; else hi = mid;
                }
                if (kv) {
                    int32_t k1 = kv >> 1, k2 = kv - k1;
                    amb_otg a = at(*buf, i), b = at(*buf, q);
                    a.ax += k1;
                    (*buf)[i] = a;
                    b = at(*buf, q);
                    b.ax += k2;
                    b.dx += k2;
                    (*buf)[q] = b;
                }
                i = q;
            }
        }
        if (sublen < 6) break;
        st += sublen;
    }
}

/* the attachment offsets carried from the glyph a glyph is attached to (HarfBuzz's
   propagate_attachment_offsets): a mark's the base's and less the advances between them (more,
   right to left), a cursive glyph's the parent's across */
static void ot_propagate(vector<amb_otg> *buf, size_t i, int rtl, int depth) {
    amb_otg g = at(*buf, i);
    if (!g.chain || depth > 64) return;
    int32_t chain = g.chain;
    int type = g.attach;
    g.chain = 0;
    (*buf)[i] = g;
    long jj = (long)i + chain;
    if (jj < 0 || (size_t)jj >= len(*buf)) return;
    size_t j = (size_t)jj;
    ot_propagate(buf, j, rtl, depth + 1);
    g = at(*buf, i);
    amb_otg b = at(*buf, j);
    if (type == 2) g.dy += b.dy;
    else {
        g.dx += b.dx;
        g.dy += b.dy;
        if (j < i) {
            if (!rtl) for (size_t k = j; k < i; k++) g.dx -= at(*buf, k).ax;
            else for (size_t k = j + 1; k < i + 1; k++) g.dx += at(*buf, k).ax;
        }
    }
    (*buf)[i] = g;
}

void amb_ot_position(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags, vector<amb_otg> *buf) {
    uint32_t pi = ot_plan_of(p, n, t, script, flags);
    ot_plan pl = at(ot_plans, pi);
    for (size_t i = 0; i < len(*buf); i++) {
        amb_otg g = at(*buf, i);
        g.attach = 0;
        g.chain = 0;
        (*buf)[i] = g;
    }
    ot_ctx c;
    memset(&c, 0, sizeof c);
    c.gdef = t->gdef;
    c.lookups = t->gpos ? ot_at(p, n, t->gpos, 8) : 0;
    c.table = 1;
    c.rtl = (flags & AMB_SH_BACKWARD) != 0;
    for (uint32_t k = 0; k < pl.npos; k++) {
        ot_lookup lk = at(ot_lks, pl.first + pl.nsub + k);
        size_t lt = ot_lookup_at(p, n, &c, lk.index);
        if (!lt) continue;
        c.mask = lk.mask;
        c.zwj = lk.zwj;
        c.zwnj = lk.zwnj;
        ot_lookup_string(p, n, &c, buf, lt);
    }
    if (pl.apply_kern) ot_kern_table(p, n, t, buf);
    /* marks' advances zeroed by their class (with no GPOS, left to right, a mark moved back by
       its advance, over the glyph before it); default ignorables' advances and offsets */
    int adjust = !pl.apply_gpos && !c.rtl;
    for (size_t i = 0; i < len(*buf); i++) {
        amb_otg g = at(*buf, i);
        if (g.props & AMB_P_MARK) {
            if (adjust) g.dx -= g.ax;
            g.ax = 0;
        }
        if (ot_ignorable(g)) {
            g.ax = 0;
            g.dx = 0;
            g.dy = 0;
        }
        (*buf)[i] = g;
    }
    for (size_t i = 0; i < len(*buf); i++) ot_propagate(buf, i, c.rtl, 0);
}

int32_t amb_ot_pair_kern(const uint8_t *p, size_t n, const amb_otf *t, int script, uint32_t a, uint32_t b) {
    vector<amb_otg> buf;
    amb_otg g;
    memset(&g, 0, sizeof g);
    g.mask = AMB_M_GLOBAL;
    g.props = AMB_P_BASE;
    g.action = 7;
    int classes = t->gdef && ot_at(p, n, t->gdef, 4);
    g.gid = a;
    if (classes) g.props = (uint16_t)ot_gdef_props(p, n, t->gdef, a);
    push_back(buf, g);
    g.gid = b;
    g.cluster = 1;
    if (classes) g.props = (uint16_t)ot_gdef_props(p, n, t->gdef, b);
    push_back(buf, g);
    amb_ot_position(p, n, t, script, 0, &buf);
    return at(buf, 0).ax + at(buf, 1).ax;
}
