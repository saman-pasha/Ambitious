/* src/layout/linebreak.c -- line breaking as Chromium breaks lines (linebreak.h), measured against Chromium 153
   and its ICU 78 at each step.

   ICU's line rules (line_normal.txt and its siblings, UAX #14 of Unicode 17 as ICU writes it) are run as UAX #14
   states them, position by position, over each code point's class (LB1: AI, SG and XX as AL, SA as CM when a
   mark and AL when not, CJ as ID, or as NS under strict), its marks absorbed (LB9, a ZWJ too; one after a space,
   a break or a ZW, or at the text's start, AL: LB10), with ICU's readings where they and the annex part: LB20a's
   hyphen kept with the letter after it wherever a boundary came before it (ICU's "^"), and its LB25 the regular
   expression of numbers. The tailorings ICU's rules make: line (strict) CJ a nonstarter; line_loose the
   iteration marks no nonstarters, and a break between inseparables; line_normal_cj 301C and 30A0 no
   nonstarters, U+201C opening and U+201D closing punctuation; line_loose_cj those of both, the centred
   punctuation (203C, 2047 to 2049, 30FB, FF1A, FF1B, FF65) and FF01 and FF1F of no class, the postfixes and
   prefixes of an East Asian width (POX, PRX: a POX may prefix a number, a PRX end one, neither more), a break
   between an ideograph and a hyphen of HH. A character a tailoring takes out of a class is of none (ZZ: what
   ICU's sets leave out matches only "any" and the sets that are a complement).

   Blink (text_break_iterator.cc, LazyLineBreakIterator::NextBreakablePosition) takes ICU's boundaries but where
   it decides first: never before a space and always after a run of them; between two characters of Latin-1, its
   fast table (character_property_data_generator.cc: each pair's boundary as ICU's "en" rules give it with
   nothing about them, then ASCII's rows and columns by hand -- a break before ( < [ { after most characters,
   after - and ?, none else), - before a digit breakable after a letter or a digit alone; word-break: break-all's
   table of classes over it (and line-break: loose's break before U+2010 and U+2013), keep-all's rule that no
   letters or numbers break but a complex script's; soft hyphens no boundary under hyphens: none. A position is
   taken on its own when a line is broken (IsBreakable, PreviousBreakOpportunity: break-all's class before it
   that of the code point before it), or by a scan from the last break when the min-content width is found
   (HandleTextForFastMinContent: the class the last but marks; ICU's text begun again at each break, so that a
   rule of the start of text applies there). A break is never taken inside a grapheme cluster (UAX #29's, as
   ICU's char.brk: GB9c's conjuncts, GB11's emoji sequences), whose glyphs are shaped as one; and under
   line-break: anywhere, or overflow-wrap's fallback, every grapheme boundary is a break.

   Not done: the dictionaries ICU breaks Thai, Lao, Khmer and Myanmar by (SA text breaks at its spaces alone). */
#include <stdint.h>
#include <string.h>
#include <vector.pl>
#include "linebreak.h"
#include "linebreak_data.h"
#include "../paint/shape.h"

/* the breaks as the rules and Blink give them, not moved to their clusters' ends (amb_lb_test_raw, for the
   tests against ICU) */
static int lb_raw;

/* the Line_Break classes in ICU's ULineBreak order, then those of the tailorings and the start of text */
enum { LB_XX, LB_AI, LB_AL, LB_B2, LB_BA, LB_BB, LB_BK, LB_CB, LB_CL, LB_CM, LB_CR, LB_EX, LB_GL, LB_HY, LB_ID, LB_IN, LB_IS,
       LB_LF, LB_NS, LB_NU, LB_OP, LB_PO, LB_PR, LB_QU, LB_SA, LB_SG, LB_SP, LB_SY, LB_ZW, LB_NL, LB_WJ, LB_H2, LB_H3, LB_JL,
       LB_JT, LB_JV, LB_CP, LB_CJ, LB_HL, LB_RI, LB_EB, LB_EM, LB_ZWJ, LB_AK, LB_AP, LB_AS, LB_VF, LB_VI, LB_HH, LB_COUNT,
       LB_ZZ = 60, LB_POX, LB_PRX, LB_SOT };

/* a code point's flags as the rules read them: East Asian (F, W, H), an initial or a final quotation mark (QU
   of Pi, of Pf), an unassigned Extended_Pictographic (LB30b), U+25CC (LB28a's dotted circle) */
enum { F_EA = 1, F_PI = 2, F_PF = 4, F_EPCN = 8, F_DC = 16 };
/* what a run of spaces after a character keeps of it: ZW SP* (LB8), OP SP* (LB14), LB15a's quotation mark and
   the spaces after it, (CL | CP) SP* (LB16), B2 SP* (LB17) */
enum { RUN_ZW = 1, RUN_OP = 2, RUN_PI = 4, RUN_CL = 8, RUN_B2 = 16 };

static uint32_t lb_latin[256];
static int lb_latin_ready;

/* the run of linebreak_data.h's table cp is in: its properties */
static uint32_t lb_props_search(uint32_t cp) {
    size_t lo = 0, hi = sizeof amb_lbd_start / sizeof amb_lbd_start[0];
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (amb_lbd_start[mid] <= cp) lo = mid;
        else hi = mid;
    }
    return amb_lbd_value[lo];
}
uint32_t amb_lb_props(uint32_t cp) {
    if (cp < 256) {
        if (!lb_latin_ready) {
            for (uint32_t c = 0; c < 256; c++) lb_latin[c] = lb_props_search(c);
            lb_latin_ready = 1;
        }
        return lb_latin[cp];
    }
    if (cp > 0x10FFFF) return lb_props_search(0xFFFD);
    return lb_props_search(cp);
}
static int lb_class_of(uint32_t p) { return (int)(p & AMB_LBP_CLASS); }
static int lb_gc_of(uint32_t p) { return (int)(p >> 8 & 7); }
static int lb_gcb_of(uint32_t p) { return (int)(p >> 12 & 15); }
static int lb_incb_of(uint32_t p) { return (int)(p >> 16 & 3); }

/* a code point's class under rules r: LB1's resolutions and the tailorings */
static int lb_resolve(uint32_t cp, uint32_t p, int r) {
    int c = lb_class_of(p);
    if (c == LB_AI || c == LB_SG || c == LB_XX) c = LB_AL;
    else if (c == LB_SA) c = (lb_gc_of(p) == 3 || lb_gc_of(p) == 4) ? LB_CM : LB_AL;
    else if (c == LB_CJ) c = r == AMB_LBR_STRICT ? LB_NS : LB_ID;
    if (r == AMB_LBR_LOOSE || r == AMB_LBR_LOOSE_CJ) {
        if (cp == 0x3005 || cp == 0x303B || cp == 0x309D || cp == 0x309E || cp == 0x30FD || cp == 0x30FE) return LB_ZZ;
    }
    if (r == AMB_LBR_NORMAL_CJ || r == AMB_LBR_LOOSE_CJ) {
        if (cp == 0x301C || cp == 0x30A0) return LB_ZZ;
        if (cp == 0x201C) return LB_OP;
        if (cp == 0x201D) return LB_CL;
    }
    if (r == AMB_LBR_LOOSE_CJ) {
        if (cp == 0x203C || cp == 0x2047 || cp == 0x2048 || cp == 0x2049 || cp == 0x30FB || cp == 0xFF1A || cp == 0xFF1B || cp == 0xFF65 ||
            cp == 0xFF01 || cp == 0xFF1F)
            return LB_ZZ;
        if (cp == 0xB0 || cp == 0x2030 || cp == 0x2032 || cp == 0x2033 || cp == 0x2035 || cp == 0x2103 || cp == 0x2109 || cp == 0xFE6A ||
            cp == 0xFF05 || cp == 0xFFE0)
            return LB_POX;
        if (cp == 0xA4 || cp == 0xB1 || cp == 0x20AC || cp == 0x2116 || cp == 0xFE69 || cp == 0xFF04 || cp == 0xFFE1 || cp == 0xFFE5 ||
            cp == 0xFFE6)
            return LB_PRX;
    }
    return c;
}
static uint8_t lb_flags_of(uint32_t cp, uint32_t p, int cls) {
    uint8_t f = 0;
    if (p & AMB_LBP_EA) f |= F_EA;
    if (cls == LB_QU && lb_gc_of(p) == 6) f |= F_PI;
    if (cls == LB_QU && lb_gc_of(p) == 7) f |= F_PF;
    if ((p & AMB_LBP_EP) && (p & AMB_LBP_CN)) f |= F_EPCN;
    if (cp == 0x25CC) f |= F_DC;
    return f;
}

/* a paragraph's text under one set of rules: each code point's class (cls), its flags (fl), whether it is a mark
   absorbed by the character before it (att: LB9), and the next code point after it that is not (nx: n at the
   end) */
static void lb_prepare(const vector<uint32_t> *cps, const vector<uint32_t> *props, int r, vector<uint8_t> *cls, vector<uint8_t> *fl,
                       vector<uint8_t> *att, vector<uint32_t> *nx) {
    size_t n = len(*cps);
    clear(*cls);
    clear(*fl);
    clear(*att);
    clear(*nx);
    resize(cls, n);
    resize(fl, n);
    resize(att, n);
    resize(nx, n);
    for (size_t k = 0; k < n; k++) {
        uint32_t cp = at(*cps, k), p = at(*props, k);
        int c = lb_resolve(cp, p, r);
        (*cls)[k] = (uint8_t)c;
        (*fl)[k] = lb_flags_of(cp, p, c);
        int a = 0;
        if ((c == LB_CM || c == LB_ZWJ) && k > 0) {
            int b = at(*cls, k - 1);
            a = !(b == LB_BK || b == LB_CR || b == LB_LF || b == LB_NL || b == LB_SP || b == LB_ZW);
        }
        (*att)[k] = (uint8_t)a;
    }
    uint32_t next = (uint32_t)n;
    for (size_t k = n; k > 0; k--) {
        (*nx)[k - 1] = next;
        if (!at(*att, k - 1)) next = (uint32_t)(k - 1);
    }
}

/* the rules' state, going through the text: the class of the last character not absorbed (L, AL for a mark
   that is not) and of the one before it (L2), their flags and runs of spaces, the regional indicators ending at
   the last, LB25's number before it (1 NU (SY | IS)*, 2 NU (SY | IS)* (CL | CP)), whether a boundary came before
   the last (LB20a), whether the code point just before is a ZWJ (LB8a); so the text's start (ICU's text begins
   at a line's start in Blink) */
typedef struct lb_eng {
    int L, L2, ri, so;
    uint8_t lf, l2f, run, l2run, num, brk_last, zwj;
} lb_eng;

static void lb_eng_start(lb_eng *e, size_t so) {
    memset(e, 0, sizeof *e);
    e->L = LB_SOT;
    e->L2 = LB_SOT;
    e->brk_last = 1;
    e->so = (int)so;
}

/* the class and flags of code point k as the rules see it when it is not absorbed: a mark (a ZWJ) AL, as U+0041 */
static int lb_eff(const vector<uint8_t> *cls, size_t k) {
    int c = at(*cls, k);
    return c == LB_CM || c == LB_ZWJ ? LB_AL : c;
}
static uint8_t lb_effl(const vector<uint8_t> *cls, const vector<uint8_t> *fl, size_t k) {
    int c = at(*cls, k);
    return c == LB_CM || c == LB_ZWJ ? 0 : at(*fl, k);
}

/* code point k taken into the state, brk the boundary before it */
static void lb_eng_take(lb_eng *e, const vector<uint8_t> *cls, const vector<uint8_t> *fl, const vector<uint8_t> *att, size_t k, int brk) {
    e->zwj = at(*cls, k) == LB_ZWJ;
    if (at(*att, k) && (int)k > e->so) return;
    int E = lb_eff(cls, k);
    uint8_t f = lb_effl(cls, fl, k), run = 0, sp = E == LB_SP;
    if (E == LB_ZW || (sp && (e->run & RUN_ZW))) run |= RUN_ZW;
    if (E == LB_OP || (sp && (e->run & RUN_OP))) run |= RUN_OP;
    if ((E == LB_QU && (f & F_PI) &&
         (e->L == LB_SOT || e->L == LB_BK || e->L == LB_CR || e->L == LB_LF || e->L == LB_NL || e->L == LB_OP || e->L == LB_QU ||
          e->L == LB_GL || e->L == LB_SP || e->L == LB_ZW)) ||
        (sp && (e->run & RUN_PI)))
        run |= RUN_PI;
    if (E == LB_CL || E == LB_CP || (sp && (e->run & RUN_CL))) run |= RUN_CL;
    if (E == LB_B2 || (sp && (e->run & RUN_B2))) run |= RUN_B2;
    e->ri = E == LB_RI ? e->ri + 1 : 0;
    if (E == LB_NU) e->num = 1;
    else if ((E == LB_SY || E == LB_IS) && e->num == 1) e->num = 1;
    else if ((E == LB_CL || E == LB_CP) && e->num == 1) e->num = 2;
    else e->num = 0;
    e->brk_last = (uint8_t)(brk || (int)k == e->so);
    e->L2 = e->L;
    e->l2f = e->lf;
    e->l2run = e->run;
    e->L = E;
    e->lf = f;
    e->run = run;
}

static int lb_x28a(int c, uint8_t f) { return c == LB_AK || c == LB_AS || (c == LB_AL && (f & F_DC)); }

/* the rules' boundary before code point i (> the text's start), its state that of the code points before it */
static int lb_eng_at(const lb_eng *e, const vector<uint8_t> *cls, const vector<uint8_t> *fl, const vector<uint8_t> *att,
                     const vector<uint32_t> *nx, int r, size_t i) {
    size_t n = len(*cls);
    int rr = at(*cls, i), L = e->L;
    /* LB4, LB5 */
    if (L == LB_BK || L == LB_LF || L == LB_NL) return 1;
    if (L == LB_CR) return rr != LB_LF;
    /* LB6, LB7 */
    if (rr == LB_BK || rr == LB_CR || rr == LB_LF || rr == LB_NL || rr == LB_SP || rr == LB_ZW) return 0;
    /* LB8, LB8a, LB9 */
    if (e->run & RUN_ZW) return 1;
    if (e->zwj) return 0;
    if (at(*att, i)) return 0;
    int R = lb_eff(cls, i);
    uint8_t rf = lb_effl(cls, fl, i);
    size_t j = at(*nx, i);              /* the next character the rules see */
    int rn = j < n ? lb_eff(cls, j) : LB_SOT;
    /* LB11, LB12, LB12a, LB13 */
    if (R == LB_WJ || L == LB_WJ) return 0;
    if (L == LB_GL) return 0;
    if (R == LB_GL && L != LB_SP && L != LB_BA && L != LB_HY && L != LB_HH) return 0;
    if (R == LB_CL || R == LB_CP || R == LB_EX || R == LB_SY) return 0;
    /* LB14, LB15a */
    if (e->run & (RUN_OP | RUN_PI)) return 0;
    /* LB15b */
    if (R == LB_QU && (rf & F_PF)) {
        if (j >= n) return 0;
        int c = at(*cls, j);
        if (c == LB_SP || c == LB_GL || c == LB_WJ || c == LB_CL || c == LB_QU || c == LB_CP || c == LB_EX || c == LB_IS || c == LB_SY ||
            c == LB_BK || c == LB_CR || c == LB_LF || c == LB_NL || c == LB_ZW)
            return 0;
    }
    /* LB15c, LB15d */
    if (L == LB_SP && R == LB_IS && rn == LB_NU) return 1;
    if (R == LB_IS) return 0;
    /* LB16, LB17, LB18 */
    if ((e->run & RUN_CL) && R == LB_NS) return 0;
    if ((e->run & RUN_B2) && R == LB_B2) return 0;
    if (L == LB_SP) return 1;
    /* LB19, LB19a */
    if (R == LB_QU && !(rf & F_PI)) return 0;
    if (L == LB_QU && !(e->lf & F_PF)) return 0;
    if (R == LB_QU) {
        if (!(e->lf & F_EA)) return 0;
        if (j >= n || !(lb_effl(cls, fl, j) & F_EA)) return 0;
    }
    if (L == LB_QU) {
        if (!(rf & F_EA)) return 0;
        if (e->L2 == LB_SOT || !(e->l2f & F_EA)) return 0;
    }
    /* LB20 */
    if (R == LB_CB || L == LB_CB) return 1;
    /* LB20a, as ICU has it: a hyphen a boundary came before (or a GL, a CB with its joiner, a space after OP or
       LB15a's quotation mark) kept with the letter after it */
    if ((L == LB_HY || L == LB_HH) && (R == LB_AL || R == LB_HL) &&
        (e->brk_last || e->L2 == LB_GL || e->L2 == LB_CB || (e->L2 == LB_SP && (e->l2run & (RUN_OP | RUN_PI)))))
        return 0;
    /* LB21 (line_loose_cj: an ideograph breakable before HH) */
    if (R == LB_BA || R == LB_HY || R == LB_NS) return 0;
    if (R == LB_HH && !(r == AMB_LBR_LOOSE_CJ && L == LB_ID)) return 0;
    if (L == LB_BB) return 0;
    /* LB21a, LB21b */
    if ((L == LB_HY || L == LB_HH) && e->L2 == LB_HL && R != LB_HL) return 0;
    if (L == LB_SY && R == LB_HL) return 0;
    /* LB22 (line_loose: inseparables breakable between them) */
    if (R == LB_IN && !((r == AMB_LBR_LOOSE || r == AMB_LBR_LOOSE_CJ) && L == LB_IN)) return 0;
    /* LB23, LB23a, LB24 */
    if ((L == LB_AL || L == LB_HL) && R == LB_NU) return 0;
    if (L == LB_NU && (R == LB_AL || R == LB_HL)) return 0;
    if (L == LB_PR && (R == LB_ID || R == LB_EB || R == LB_EM)) return 0;
    if ((L == LB_ID || L == LB_EB || L == LB_EM) && R == LB_PO) return 0;
    if ((L == LB_PR || L == LB_PO || L == LB_POX) && (R == LB_AL || R == LB_HL)) return 0;
    if ((L == LB_AL || L == LB_HL) && (R == LB_PR || R == LB_PO || R == LB_POX)) return 0;
    /* LB25: (PR | PO)? (OP | HY)? IS? NU (NU | SY | IS)* (CL | CP)? (PR | PO)? kept whole (line_loose_cj: POX a
       prefix alone, PRX a postfix alone) */
    if (e->num == 1 && (R == LB_NU || R == LB_PO || R == LB_PR || R == LB_PRX)) return 0;
    if (e->num == 2 && (R == LB_PO || R == LB_PR || R == LB_PRX)) return 0;
    if (L == LB_PR || L == LB_PO || L == LB_POX) {
        if (R == LB_NU) return 0;
        if (R == LB_OP || R == LB_HY) {
            if (rn == LB_NU) return 0;
            if (rn == LB_IS && j < n) {
                size_t j2 = at(*nx, j);
                if (j2 < n && lb_eff(cls, j2) == LB_NU) return 0;
            }
        }
    }
    if ((L == LB_HY || L == LB_IS) && R == LB_NU) return 0;
    /* LB26, LB27 */
    if (L == LB_JL && (R == LB_JL || R == LB_JV || R == LB_H2 || R == LB_H3)) return 0;
    if ((L == LB_JV || L == LB_H2) && (R == LB_JV || R == LB_JT)) return 0;
    if ((L == LB_JT || L == LB_H3) && R == LB_JT) return 0;
    if ((L == LB_JL || L == LB_JV || L == LB_JT || L == LB_H2 || L == LB_H3) && R == LB_PO) return 0;
    if (L == LB_PR && (R == LB_JL || R == LB_JV || R == LB_JT || R == LB_H2 || R == LB_H3)) return 0;
    /* LB28, LB28a */
    if ((L == LB_AL || L == LB_HL) && (R == LB_AL || R == LB_HL)) return 0;
    int xl = lb_x28a(L, e->lf), xr = lb_x28a(R, rf);
    if (L == LB_AP && xr) return 0;
    if (xl && (R == LB_VF || R == LB_VI)) return 0;
    if (L == LB_VI && lb_x28a(e->L2, e->l2f) && (R == LB_AK || (R == LB_AL && (rf & F_DC)))) return 0;
    if (xl && xr && rn == LB_VF) return 0;
    /* LB29, LB30 */
    if (L == LB_IS && (R == LB_AL || R == LB_HL)) return 0;
    if ((L == LB_AL || L == LB_HL || L == LB_NU) && R == LB_OP && !(rf & F_EA)) return 0;
    if (L == LB_CP && !(e->lf & F_EA) && (R == LB_AL || R == LB_HL || R == LB_NU)) return 0;
    /* LB30a, LB30b */
    if (L == LB_RI && R == LB_RI && (e->ri & 1)) return 0;
    if (R == LB_EM && (L == LB_EB || (e->lf & F_EPCN))) return 0;
    /* LB31 */
    return 1;
}

/* ICU's boundaries of the text under rules r, out[k] before cps[k] (the text begun at 0) */
static void lb_icu_run(const vector<uint8_t> *cls, const vector<uint8_t> *fl, const vector<uint8_t> *att, const vector<uint32_t> *nx, int r,
                       vector<uint8_t> *out) {
    size_t n = len(*cls);
    clear(*out);
    resize(out, n + 1);
    if (!n) return;
    lb_eng e;
    lb_eng_start(&e, 0);
    lb_eng_take(&e, cls, fl, att, 0, 1);
    for (size_t i = 1; i < n; i++) {
        int b = lb_eng_at(&e, cls, fl, att, nx, r, i);
        (*out)[i] = (uint8_t)b;
        lb_eng_take(&e, cls, fl, att, i, b);
    }
}

void amb_lb_icu(const vector<uint32_t> *cps, int rules, vector<uint8_t> *out) {
    vector<uint32_t> props;
    for (size_t k = 0; k < len(*cps); k++) push_back(props, amb_lb_props(at(*cps, k)));
    vector<uint8_t> cls, fl, att;
    vector<uint32_t> nx;
    lb_prepare(cps, &props, rules, &cls, &fl, &att, &nx);
    lb_icu_run(&cls, &fl, &att, &nx, rules, out);
}

/* ---- grapheme clusters (UAX #29, Unicode 17) ---- */

enum { GCB_OTHER, GCB_CR, GCB_LF, GCB_CONTROL, GCB_EXTEND, GCB_ZWJ, GCB_RI, GCB_PREPEND, GCB_SPACINGMARK, GCB_L, GCB_V, GCB_T, GCB_LV, GCB_LVT };
enum { INCB_NONE, INCB_LINKER, INCB_CONSONANT, INCB_EXTEND };

/* gb[k] 1 where a grapheme cluster boundary comes before cps[k] (k from 1 to n - 1; 0 and n are ends) */
static void lb_graphemes(const vector<uint32_t> *props, vector<uint8_t> *gb) {
    size_t n = len(*props);
    clear(*gb);
    resize(gb, n + 1);
    if (!n) return;
    (*gb)[0] = 1;
    (*gb)[n] = 1;
    int ri = 0, ep = 0, cons = 0;   /* RIs ending here; ExtPict Extend* (2: and a ZWJ); GB9c: 1 a consonant then
                                       extends and linkers, 2 with a linker among them */
    uint32_t p0 = at(*props, 0);
    int g0 = lb_gcb_of(p0);
    ri = g0 == GCB_RI;
    ep = (p0 & AMB_LBP_EP) ? 1 : 0;
    cons = lb_incb_of(p0) == INCB_CONSONANT;
    for (size_t k = 1; k < n; k++) {
        uint32_t p = at(*props, k), q = at(*props, k - 1);
        int a = lb_gcb_of(q), b = lb_gcb_of(p), brk = 1;
        int ib = lb_incb_of(p);
        if (a == GCB_CR && b == GCB_LF) brk = 0;                                                         /* GB3 */
        else if (a == GCB_CR || a == GCB_LF || a == GCB_CONTROL) brk = 1;                               /* GB4 */
        else if (b == GCB_CR || b == GCB_LF || b == GCB_CONTROL) brk = 1;                               /* GB5 */
        else if (a == GCB_L && (b == GCB_L || b == GCB_V || b == GCB_LV || b == GCB_LVT)) brk = 0;      /* GB6 */
        else if ((a == GCB_LV || a == GCB_V) && (b == GCB_V || b == GCB_T)) brk = 0;                     /* GB7 */
        else if ((a == GCB_LVT || a == GCB_T) && b == GCB_T) brk = 0;                                    /* GB8 */
        else if (b == GCB_EXTEND || b == GCB_ZWJ || b == GCB_SPACINGMARK) brk = 0;                       /* GB9, GB9a */
        else if (a == GCB_PREPEND) brk = 0;                                                               /* GB9b */
        else if (cons == 2 && ib == INCB_CONSONANT) brk = 0;                                              /* GB9c */
        else if (ep == 2 && (p & AMB_LBP_EP)) brk = 0;                                                    /* GB11 */
        else if (a == GCB_RI && b == GCB_RI && (ri & 1)) brk = 0;                                         /* GB12, GB13 */
        (*gb)[k] = (uint8_t)brk;
        /* the states after k */
        ri = b == GCB_RI ? ri + 1 : 0;
        if (p & AMB_LBP_EP) ep = 1;
        else if (ep == 1 && b == GCB_EXTEND) ep = 1;
        else if (ep == 1 && b == GCB_ZWJ) ep = 2;
        else ep = 0;
        if (ib == INCB_CONSONANT) cons = 1;
        else if (cons && ib == INCB_LINKER) cons = 2;
        else if (cons && ib == INCB_EXTEND) cons = cons;
        else cons = 0;
    }
}

/* ---- Blink's layer ---- */

/* Blink's kBreakAllLineBreakClassTable (text_break_iterator.cc, its 49 classes of ICU 78): bit 0x80 >> (c % 8) of
   byte c / 8 of row l set where break-all breaks between a character of class l and one of class c */
static const uint8_t lb_break_all[49][7] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 }, { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x12, 0x00, 0x02, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x16, 0x00, 0x02, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x10, 0x00, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x68, 0x04, 0x16, 0x00, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x68, 0x04, 0x12, 0x80, 0x02, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x68, 0x04, 0x1A, 0x80, 0x02, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

/* Blink's fast table: between two characters of '!' to U+00FF, a break where ICU's "en" rules (line_normal) give
   one between them alone, then ASCII's rows and columns as LineBreakData::FillAscii sets them; bit b % 8 of byte
   b / 8 of row a (both less '!'). ASCII's pairs are set by hand, made once; a pair with a character past ASCII is
   asked of the rules the first time it is met (lb_fast_known) */
enum { FAST_MIN = 0x21, FAST_MAX = 0xFF, FAST_N = 0xFF - 0x21 + 1, FAST_ROW = (0xFF - 0x21 + 1 + 7) / 8 };
static uint8_t lb_fast[223][28];
static uint8_t lb_fast_known[223][28];
static int lb_fast_ready;
static void lb_fast_set(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1, int v) {
    for (uint32_t a = a0; a <= a1; a++)
        for (uint32_t b = b0; b <= b1; b++) {
            uint32_t x = b - FAST_MIN;
            if (v) lb_fast[a - FAST_MIN][x / 8] |= (uint8_t)(1u << (x % 8));
            else lb_fast[a - FAST_MIN][x / 8] &= (uint8_t)~(1u << (x % 8));
            lb_fast_known[a - FAST_MIN][x / 8] |= (uint8_t)(1u << (x % 8));
        }
}
/* the pair a, b alone under line_normal, as ICU's "en" instance answers LineBreakData's question */
static int lb_fast_icu(uint32_t a, uint32_t b) {
    vector<uint8_t> cls, fl, att;
    vector<uint32_t> nx;
    resize(cls, 2);
    resize(fl, 2);
    resize(att, 2);
    resize(nx, 2);
    uint32_t pa = amb_lb_props(a), pb = amb_lb_props(b);
    int ca = lb_resolve(a, pa, AMB_LBR_NORMAL), cb = lb_resolve(b, pb, AMB_LBR_NORMAL);
    cls[0] = (uint8_t)ca;
    cls[1] = (uint8_t)cb;
    fl[0] = lb_flags_of(a, pa, ca);
    fl[1] = lb_flags_of(b, pb, cb);
    att[1] = (uint8_t)((cb == LB_CM || cb == LB_ZWJ) && !(ca == LB_BK || ca == LB_CR || ca == LB_LF || ca == LB_NL || ca == LB_SP || ca == LB_ZW));
    nx[0] = at(att, 1) ? 2 : 1;
    nx[1] = 2;
    lb_eng e;
    lb_eng_start(&e, 0);
    lb_eng_take(&e, &cls, &fl, &att, 0, 1);
    return lb_eng_at(&e, &cls, &fl, &att, &nx, AMB_LBR_NORMAL, 1);
}
static int lb_fast_get(uint32_t a, uint32_t b) {
    uint32_t x = b - FAST_MIN;
    if (!(lb_fast_known[a - FAST_MIN][x / 8] & (1u << (x % 8)))) lb_fast_set(a, a, b, b, lb_fast_icu(a, b));
    return (lb_fast[a - FAST_MIN][x / 8] >> (x % 8)) & 1;
}
static void lb_fast_make(void) {
    if (lb_fast_ready) return;
    lb_fast_ready = 1;
    memset(lb_fast, 0, sizeof lb_fast);
    memset(lb_fast_known, 0, sizeof lb_fast_known);
    lb_fast_set('!', 0x7F, '!', 0x7F, 0);
    lb_fast_set('!', 0x7F, '(', '(', 1);
    lb_fast_set('!', 0x7F, '<', '<', 1);
    lb_fast_set('!', 0x7F, '[', '[', 1);
    lb_fast_set('!', 0x7F, '{', '{', 1);
    lb_fast_set('-', '-', '!', 0x7F, 1);
    lb_fast_set('?', '?', '!', 0x7F, 1);
    lb_fast_set('-', '-', '$', '$', 0);
    lb_fast_set('!', 0x7F, '!', '!', 0);
    lb_fast_set('?', '?', '"', '"', 0);
    lb_fast_set('?', '?', '\'', '\'', 0);
    lb_fast_set('!', 0x7F, ')', ')', 0);
    lb_fast_set('!', 0x7F, ',', ',', 0);
    lb_fast_set('!', 0x7F, '.', '.', 0);
    lb_fast_set('!', 0x7F, '/', '/', 0);
    lb_fast_set('-', '-', '0', '9', 0);
    lb_fast_set('!', 0x7F, ':', ':', 0);
    lb_fast_set('!', 0x7F, ';', ';', 0);
    lb_fast_set('!', 0x7F, '?', '?', 0);
    lb_fast_set('!', 0x7F, ']', ']', 0);
    lb_fast_set('!', 0x7F, '}', '}', 0);
    lb_fast_set('$', '$', '!', 0x7F, 0);
    lb_fast_set('\'', '\'', '!', 0x7F, 0);
    lb_fast_set('(', '(', '!', 0x7F, 0);
    lb_fast_set('/', '/', '!', 0x7F, 0);
    lb_fast_set('0', '9', '!', 0x7F, 0);
    lb_fast_set('<', '<', '!', 0x7F, 0);
    lb_fast_set('@', '@', '!', 0x7F, 0);
    lb_fast_set('A', 'Z', '!', 0x7F, 0);
    lb_fast_set('[', '[', '!', 0x7F, 0);
    lb_fast_set('^', '`', '!', 0x7F, 0);
    lb_fast_set('a', 'z', '!', 0x7F, 0);
    lb_fast_set('{', '{', '!', 0x7F, 0);
    lb_fast_set(0x7F, 0x7F, '!', 0x7F, 0);
}

static int lb_space(uint32_t c) { return c == ' ' || c == '\t' || c == '\n'; }
static int lb_alnum(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* ShouldBreakFast: 0 no break, 1 a break, 2 ICU decides */
static int lb_fast_break(uint32_t last_last, uint32_t last, uint32_t ch, int noshy) {
    if (last < FAST_MIN || ch < FAST_MIN) return 0;
    if (last == '-') {
        if (ch <= 0x7F) {
            if (ch >= '0' && ch <= '9') return lb_alnum(last_last) ? 1 : 0;
        } else return 2;
    }
    if (last <= FAST_MAX && ch <= FAST_MAX) {
        if (!lb_fast_get(last, ch)) return 0;
        if (noshy && last == 0xAD) return 0;
        return 1;
    }
    return 2;
}

/* break-all's class of a code point (LineBreakPropertyValue: '+' AL, as IE tailors it) */
static int lb_ba_class(uint32_t cp, uint32_t p) { return cp == '+' ? LB_AL : lb_class_of(p); }
static int lb_ba_break(int last, int c, uint32_t ch, int loose) {
    if (c < 0 || c >= 49 || last < 0 || last >= 49) return 0;
    if (!(lb_break_all[last][c / 8] & (0x80 >> (c % 8)))) return 0;
    if (c == LB_BA && ch != 0x7C && !loose) return 0;
    return 1;
}
/* ShouldKeepAfterKeepAll: a letter or number (or a mark after one), not of a complex script, before one; of
   UTF-16 units, so a code point past the BMP is neither */
static int lb_ln(uint32_t cp, uint32_t p) { return cp <= 0xFFFF && (lb_gc_of(p) == 1 || lb_gc_of(p) == 2) && lb_class_of(p) != LB_SA; }
static int lb_mark(uint32_t cp, uint32_t p) { return cp <= 0xFFFF && lb_gc_of(p) >= 3 && lb_gc_of(p) <= 5; }
static int lb_keep_all(uint32_t ll, uint32_t llp, uint32_t l, uint32_t lp, uint32_t ch, uint32_t chp) {
    int pre = lb_mark(l, lp) ? lb_ln(ll, llp) : lb_ln(l, lp);
    return pre && lb_ln(ch, chp);
}


/* Blink's NextBreakablePosition at position i (before cps[i]) for a style of type type: a space never broken
   before and always after; a grapheme boundary for kBreakCharacter; the fast table; break-all's classes (last_lb
   the class before, which a scan keeps over marks: *last_lb takes the class at i unless i breaks or is a mark; on
   its own, at a code point past the BMP, break-all is not asked: Blink skips a lead surrogate) or keep-all; else
   ICU's boundary icu_b. so: the start of the rules' text (the context before it unseen) */
static int lb_blink_at(const vector<uint32_t> *cps, const vector<uint32_t> *props, const vector<uint8_t> *gb, size_t i, size_t so,
                       const amb_lbstyle *st, int type, int scan, int *last_lb, int icu_b) {
    if (!st->wrap) return 0;
    uint32_t ch = at(*cps, i), last = at(*cps, i - 1), ll = i > so + 1 ? at(*cps, i - 2) : 0;
    if (lb_space(ch)) return 0;
    if (lb_space(last)) return 1;
    if (type == AMB_LBT_BREAK_CHAR) return at(*gb, i);
    int fr = lb_fast_break(ll, last, ch, !st->shy);
    if (fr == 1) return 1;
    if (type == AMB_LBT_BREAK_ALL && (scan || ch <= 0xFFFF)) {
        int c = lb_ba_class(ch, at(*props, i));
        if (st->loose && (ch == 0x2010 || ch == 0x2013) && (*last_lb == LB_NU || *last_lb == LB_AL || *last_lb == LB_SA || *last_lb == LB_ID))
            return 1;
        if (lb_ba_break(*last_lb, c, ch, st->loose)) return 1;
        if (c != LB_CM) *last_lb = c;
    } else if (type == AMB_LBT_KEEP_ALL &&
               lb_keep_all(ll, i > so + 1 ? at(*props, i - 2) : 0, last, at(*props, i - 1), ch, at(*props, i)))
        return 0;
    if (fr == 0) return 0;
    return icu_b && !(!st->shy && last == 0xAD);
}

void amb_line_breaks(const vector<uint32_t> *cps, const vector<uint8_t> *sty, const vector<amb_lbstyle> *styles, vector<uint8_t> *out) {
    size_t n = len(*cps);
    clear(*out);
    resize(out, n + 1);
    if (n < 2) return;
    lb_fast_make();
    vector<uint32_t> props;
    reserve(props, n);
    /* Latin-1 alone, but a hyphen before a letter past ASCII (the fast table's one question for ICU): no rules run */
    int latin = 1;
    for (size_t k = 0; k < n; k++) {
        uint32_t c = at(*cps, k);
        push_back(props, amb_lb_props(c));
        if (c > 0xFF || (k > 0 && c > 0x7F && at(*cps, k - 1) == '-')) latin = 0;
    }
    vector<uint8_t> gb;
    if (latin) {
        /* every position a grapheme boundary but CR LF */
        resize(gb, n + 1);
        for (size_t k = 0; k <= n; k++) gb[k] = (uint8_t)!(k > 0 && k < n && at(*cps, k - 1) == '\r' && at(*cps, k) == '\n');
    } else lb_graphemes(&props, &gb);
    for (size_t k = 1; k < n; k++)
        if (at(gb, k)) (*out)[k] |= AMB_BRK_GRAPHEME;
    /* AMB_BRK_LINE: each position on its own, ICU's boundaries over the whole text, for each set of rules the
       styles use */
    uint8_t used = 0;
    for (size_t k = 0; k < len(*styles); k++) used |= (uint8_t)(1u << at(*styles, k).rules);
    vector<uint8_t> cls, fl, att, icu;
    vector<uint32_t> nx;
    int prepared = -1;                  /* the rules cls, fl, att and nx are of */
    for (int r = 0; r < 5; r++) {
        if (!(used >> r & 1)) continue;
        if (!latin) {
            lb_prepare(cps, &props, r, &cls, &fl, &att, &nx);
            prepared = r;
            lb_icu_run(&cls, &fl, &att, &nx, r, &icu);
        }
        for (size_t i = 1; i < n; i++) {
            amb_lbstyle st = at(*styles, at(*sty, i - 1));
            if (st.rules != r) continue;
            int last_lb = lb_ba_class(at(*cps, i - 1), at(props, i - 1));
            if (lb_blink_at(cps, &props, &gb, i, 0, &st, st.type, 0, &last_lb, latin ? 0 : at(icu, i))) (*out)[i] |= AMB_BRK_LINE;
        }
    }
    /* AMB_BRK_MIN: a scan from each break to the next, the rules begun again at each (Blink's start offset); a
       change of rules begins them again too */
    size_t so = 0;
    lb_eng e;
    lb_eng_start(&e, 0);
    int er = -1, last_lb = 0;
    for (size_t i = 1; i < n; i++) {
        amb_lbstyle st = at(*styles, at(*sty, i - 1));
        if (st.rules != er) {
            er = st.rules;
            if (!latin) {
                if (prepared != er) lb_prepare(cps, &props, er, &cls, &fl, &att, &nx);
                prepared = er;
                lb_eng_start(&e, so);
                lb_eng_take(&e, &cls, &fl, &att, so, 1);
                for (size_t k = so + 1; k < i; k++) lb_eng_take(&e, &cls, &fl, &att, k, lb_eng_at(&e, &cls, &fl, &att, &nx, er, k));
            }
            last_lb = lb_ba_class(at(*cps, i - 1), at(props, i - 1));
        }
        int icu_b = latin ? 0 : lb_eng_at(&e, &cls, &fl, &att, &nx, er, i);
        if (lb_blink_at(cps, &props, &gb, i, so, &st, st.mintype, 1, &last_lb, icu_b)) {
            (*out)[i] |= AMB_BRK_MIN;
            /* the rules begun again here, the text's start */
            so = i;
            if (!latin) {
                lb_eng_start(&e, so);
                lb_eng_take(&e, &cls, &fl, &att, so, 1);
            }
            last_lb = lb_ba_class(at(*cps, i), at(props, i));
        } else if (!latin) lb_eng_take(&e, &cls, &fl, &att, i, icu_b);
    }
    /* a break inside a cluster HarfBuzz shapes as one (a mark, a joiner and what it joins, an emoji modifier, a
       regional indicator's pair: hb_form_clusters, font.c's amb_shape) is taken at the cluster's end, where
       Blink's line, holding the cluster's glyphs, ends */
    if (lb_raw || latin) return;
    vector<uint8_t> cont;
    resize(cont, n + 1);
    for (size_t k = 1; k < n; k++) {
        uint32_t cp = at(*cps, k), prev = at(*cps, k - 1);
        int c = (amb_uprops_of(cp, prev) & AMB_U_CONT) != 0;
        /* a regional indicator after one that begins its cluster: their pair */
        if (!c && cp >= 0x1F1E6 && cp <= 0x1F1FF && prev >= 0x1F1E6 && prev <= 0x1F1FF && !at(cont, k - 1)) c = 1;
        cont[k] = (uint8_t)c;
    }
    for (size_t k = 1; k < n; k++) {
        if (!at(cont, k)) continue;
        uint8_t moved = (uint8_t)(at(*out, k) & (AMB_BRK_LINE | AMB_BRK_MIN));
        if (!moved) continue;
        (*out)[k] = (uint8_t)(at(*out, k) & ~(AMB_BRK_LINE | AMB_BRK_MIN));
        size_t j = k + 1;
        while (j < n && at(cont, j)) j++;
        if (j < n) (*out)[j] |= moved;
    }
}

void amb_lb_test_raw(int on) { lb_raw = on; }

void amb_lb_graphemes(const vector<uint32_t> *cps, vector<uint8_t> *out) {
    vector<uint32_t> props;
    for (size_t k = 0; k < len(*cps); k++) push_back(props, amb_lb_props(at(*cps, k)));
    lb_graphemes(&props, out);
}
