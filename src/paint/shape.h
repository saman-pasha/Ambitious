/* src/paint/shape.h -- OpenType layout as HarfBuzz applies it when Chromium shapes text
   (shape.c), for src/paint/font.c's amb_shape: the character properties a shaper reads
   (shape_data.h, tools/gen-shape.pl), and a run's glyphs substituted (GSUB) and positioned
   (GPOS, or the kern table where the run's script's features have none) by the features
   HarfBuzz applies for Chromium, in the stages its default, Arabic and Hebrew shapers give
   them. */
#ifndef AMB_SHAPE_H
#define AMB_SHAPE_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* a glyph of a run being shaped: HarfBuzz's glyph info and position in one. Its glyph, the
   code point it came from (a ligature's its first's), the text's byte its cluster begins at,
   the features that may apply to it (AMB_M_*); its advance and offset from the pen, in font
   units, y up; its class (AMB_P_*: GDEF's, or a code point's own; its mark attachment class
   << 8), its ligature properties (the ligature's id << 5, AMB_LIG_BASE on the ligature itself,
   a mark's component or a ligature's count of them), the code point's properties (AMB_U_*)
   and combining class as HarfBuzz modifies it, its Arabic joining form (0 isol .. 6 init, 7
   none), how it is attached (1 as a mark, 2 cursively) and to the glyph how far from it */
typedef struct amb_otg {
    uint32_t gid, cp, cluster, mask;
    int32_t ax, dx, dy;
    uint16_t props, uprops;
    uint8_t lig, ccc, action, attach;
    int32_t chain;
} amb_otg;

/* the features' masks: the global ones', each Arabic joining form's (isol, fina, fin2, fin3,
   medi, med2, init), rtlm's (a mirrorable character the font has no mirror for) */
enum { AMB_M_GLOBAL = 1, AMB_M_ISOL = 2, AMB_M_RTLM = 256 };
/* a glyph's classes (HarfBuzz's glyph props) */
enum { AMB_P_BASE = 2, AMB_P_LIGATURE = 4, AMB_P_MARK = 8, AMB_P_SUBSTITUTED = 16, AMB_P_LIGATED = 32, AMB_P_MULTIPLIED = 64 };
enum { AMB_LIG_BASE = 16 };
/* a code point's properties: a mark (Mn, Mc, Me); Mn itself; default ignorable; ZWNJ; ZWJ;
   hidden (CGJ, Mongolian free variation selectors, tags); a continuation of the cluster
   before it (a mark, ZWJ, an emoji modifier, ...) */
enum { AMB_U_MARK = 1, AMB_U_MN = 2, AMB_U_IGNORABLE = 4, AMB_U_ZWNJ = 8, AMB_U_ZWJ = 16, AMB_U_HIDDEN = 32, AMB_U_CONT = 64 };

/* a face's tables, where each is in its font's bytes (0: none), its glyphs' count, hmtx's
   advances' count, and an id of the font's own (its table directory's checksums), by which
   its plans are kept */
typedef struct amb_otf { uint32_t gsub, gpos, gdef, kern, kern_len, hmtx, nhm, nglyphs, id; } amb_otf;

/* how a run is shaped: right to left (the run's direction asked for), its buffer reversed to
   the script's own direction (HarfBuzz's hb_ensure_native_direction: the buffer then right to
   left when RTL alone is not), ligatures off (letter-spacing: liga, clig and calt, as Blink
   turns them off), kerning off */
enum { AMB_SH_RTL = 1, AMB_SH_BACKWARD = 2, AMB_SH_NOLIGA = 4, AMB_SH_NOKERN = 8 };

/* what a run's plan says of it (amb_ot_plan_info): the Arabic shaper takes it; the Hebrew
   one; GPOS has a mark feature for it (HarfBuzz composes Hebrew's presentation forms only
   without one); a code point that has no glyph of its own may be decomposed */
enum { AMB_PI_ARABIC = 1, AMB_PI_HEBREW = 2, AMB_PI_GPOS_MARK = 4 };

/* the character properties: the combining class as HarfBuzz modifies it; a mark's kind (1
   Mn, 2 Mc, 3 Me; 0 none); the canonical decomposition of one level (1, its parts into *a and
   *b, *b 0 for a singleton; 0 none) and the primary composite of two (0 none); the mirrored
   glyph (the code point itself for none); the script (its index in amb_sh_tags, AMB_SC_COMMON,
   AMB_SC_INHERITED, 0 unknown), its OpenType tag, and whether it is written right to left;
   HarfBuzz's joining type (0 U, 1 L, 2 R, 3 D or C, 4 Alaph, 5 Dalath and Rish, 7 T) */
enum { AMB_SC_COMMON = 254, AMB_SC_INHERITED = 255 };
uint32_t amb_ccc_of(uint32_t cp);
int amb_mark_of(uint32_t cp);
int amb_decompose(uint32_t cp, uint32_t *a, uint32_t *b);
uint32_t amb_compose(uint32_t a, uint32_t b);
uint32_t amb_mirror_of(uint32_t cp);
int amb_script_of(uint32_t cp);
uint32_t amb_script_tag(int script);
int amb_script_rtl(int script);
int amb_joining_of(uint32_t cp);
/* a code point's AMB_U_* properties (its continuation by the code point before it: emoji ZWJ sequences) */
uint32_t amb_uprops_of(uint32_t cp, uint32_t prev);

/* a run's plan's AMB_PI_* facts */
int amb_ot_plan_info(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags);

/* a run (its glyphs' gid, cp, cluster and mask set) substituted: each glyph's classes, the
   Arabic shaper's joining forms (pre, the npre code points before the run, the nearest first;
   post, the npost after it), then GSUB's lookups, stage by stage */
void amb_ot_substitute(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags, const uint32_t *pre, size_t npre,
                       const uint32_t *post, size_t npost, vector<amb_otg> *buf);
/* a substituted run (each glyph's advance in ax) positioned: GPOS's lookups, or the kern
   table's pairs; marks' advances zeroed by their class, default ignorables' advances zeroed,
   attachments' offsets carried to the glyphs attached */
void amb_ot_position(const uint8_t *p, size_t n, const amb_otf *t, int script, int flags, vector<amb_otg> *buf);

/* the kerning HarfBuzz's kern feature gives the glyphs a then b in a face (GPOS's pair lookups
   or the kern table's pair, as its plan for the script decides), in font units */
int32_t amb_ot_pair_kern(const uint8_t *p, size_t n, const amb_otf *t, int script, uint32_t a, uint32_t b);
#endif
