/* src/layout/linebreak.h -- line breaking as Chromium breaks lines (linebreak.c), for src/layout/layout.c: the break
   opportunities of the text of a paragraph as the LazyLineBreakIterator of Blink finds them over the ICU 78 line
   rules Chromium carries (UAX #14 with the tailorings of ICU: line_normal by default, line for strict, line_loose,
   and the _cj rules of Chinese and Japanese), its fast table for Latin-1, the break-all and keep-all tables of
   word-break, and the grapheme clusters of UAX #29 for line-break: anywhere and overflow-wrap; the properties of
   each code point from the UCD of Unicode 17 (linebreak_data.h, tools/gen-linebreak.py). (No apostrophe in a
   comment of this header: toolchain/REPORT.md, finding 30.) */
#ifndef AMB_LINEBREAK_H
#define AMB_LINEBREAK_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the fields of the properties of a code point (amb_lb_props): its Line_Break class (the low 6 bits, in the
   ULineBreak order of ICU), East_Asian_Width F, W or H (64), Extended_Pictographic (128), its general category
   as line breaking reads it (3 bits from bit 8: 1 a letter, 2 a number, 3 Mn, 4 Mc, 5 Me, 6 Pi, 7 Pf),
   unassigned (2048), its Grapheme_Cluster_Break (4 bits from bit 12) and Indic_Conjunct_Break (2 bits from
   bit 16) */
enum { AMB_LBP_CLASS = 63, AMB_LBP_EA = 64, AMB_LBP_EP = 128, AMB_LBP_GC = 1792, AMB_LBP_CN = 2048 };
/* two of the classes (OP, QU) and two of the categories (Pi, Pf, the field AMB_LBP_GC shifted right 8) */
enum { AMB_LBC_OP = 20, AMB_LBC_QU = 23, AMB_LBG_PI = 6, AMB_LBG_PF = 7 };
uint32_t amb_lb_props(uint32_t cp);

/* the ICU rules a style breaks by (the locale of Blink with its lb keyword): line_normal (the default of
   Chromium, and of any language without line-break), line (strict), line_loose, line_normal_cj and
   line_loose_cj (Chinese and Japanese, normal and loose) */
enum { AMB_LBR_NORMAL = 0, AMB_LBR_STRICT = 1, AMB_LBR_LOOSE = 2, AMB_LBR_NORMAL_CJ = 3, AMB_LBR_LOOSE_CJ = 4 };
/* word-break as the line break type of Blink: normal, break-all, keep-all, and a break at every grapheme
   boundary (line-break: anywhere; overflow-wrap, and word-break: break-word, when a line overflows) */
enum { AMB_LBT_NORMAL = 0, AMB_LBT_BREAK_ALL = 1, AMB_LBT_KEEP_ALL = 2, AMB_LBT_BREAK_CHAR = 3 };

/* the line breaking of a style: its type (AMB_LBT_*) and the type its min-content width is found by (a break
   at every grapheme for overflow-wrap: anywhere and word-break: break-word, whose emergency breaks lower it),
   its rules (AMB_LBR_*), line-break: loose (the breaks of break-all before hyphens), soft hyphens taken
   (hyphens other than none), its lines wrapped (white-space) */
typedef struct amb_lbstyle { uint8_t type, mintype, rules, loose, shy, wrap; } amb_lbstyle;

/* the flags of a position: a break opportunity as Blink finds one breaking a line (IsBreakable,
   PreviousBreakOpportunity: each position on its own), as it finds them for the min-content width (a scan from
   each break to the next, the text of the rules begun again at each: HandleTextForFastMinContent), a grapheme
   cluster boundary */
enum { AMB_BRK_LINE = 1, AMB_BRK_MIN = 2, AMB_BRK_GRAPHEME = 4 };

/* the break opportunities of the text of a paragraph, cps[0, n): sty[k] the index in styles of the style of
   the piece cps[k] is in; out[k] (k from 0 to n) the flags of the position before cps[k], a position between
   two code points taking the style of the one before it. A space (U+0020, a tab, a line feed) is no
   opportunity before it and always one after it */
void amb_line_breaks(const vector<uint32_t> *cps, const vector<uint8_t> *sty, const vector<amb_lbstyle> *styles, vector<uint8_t> *out);

/* the ICU boundaries alone of cps under rules r (out[k] 1 where one is before cps[k]), for the tests */
void amb_lb_icu(const vector<uint32_t> *cps, int rules, vector<uint8_t> *out);
/* for the tests: amb_line_breaks with a break inside a cluster left where the rules put it (on 1), or moved to
   the cluster end (0, as layout takes it) */
void amb_lb_test_raw(int on);
/* the grapheme cluster boundaries of cps (UAX #29): out[k] 1 where one comes before cps[k] (0 and n ends) */
void amb_lb_graphemes(const vector<uint32_t> *cps, vector<uint8_t> *out);
#endif
