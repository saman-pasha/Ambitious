/* src/css/cssom_decl.c -- CSSOM's declarations (CSSOM 6.6) as Chromium's property sets keep
   them, each measured in Chromium first: a declaration list read, its values checked and
   written as Chromium writes a specified value, its shorthands kept as their longhands and
   written back as Chromium's StylePropertySerializer does.

   VALUES. A longhand's value is read by its grammar here (colours, lengths and
   percentages, numbers, keywords, times, lists of them, the font family list) or, for a
   property with none here, taken as it is; either way written as Chromium writes it: a
   keyword in lower case (a custom ident as written), a number in six significant digits, a
   unitless zero a length's 0px, a hex or functional sRGB colour rgb() or rgba() (alpha the
   shortest decimal its byte comes back from), a named colour its name, a string in double
   quotes, url() a string's, a calc() of like terms one term (percentages first, then the
   units' order), a comma ", ". A value with var() is kept as written; a custom property's
   value as written, trimmed.

   SHORTHANDS. Read into their longhands in Chromium's order, the ones a value leaves out
   given what Chromium gives them (initial, implicit, or the initial value); a CSS-wide
   keyword, each longhand that keyword; a value with var(), each longhand pending, the
   shorthand's text kept. Written back where every longhand is there of one importance, by
   each shorthand's own rule (the four sides' compaction, a border side's initial values left
   out, background's and font's and transition's and animation's layers ...); else as its
   longhands. A shorthand this has no reader for keeps its value whole, as a pending one does.

   A LIST. Read as Chromium's parser keeps a block: for each property its last declaration,
   the important ones after the others; set, a longhand replaced where it is or appended;
   removed, a shorthand's longhands all. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include "cssom.h"
#include "cssom_int.h"
#include "cssom_props.h"

static char od_low(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static void od_cat(vector<char> *out, const char *s) { append_range(*out, s, strlen(s)); }
/* a name copied into a buffer of cap bytes, cut to fit (a table's name, kept apart from it) */
static void od_copyz(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}
static int od_is(const vector<char> *v, const char *w) { return css_omx_eq(data(*v), len(*v), w, strlen(w)); }
static uint32_t od_prop(const char *name) { int p = css_om_prop(name, strlen(name)); return p < 0 ? 0xFFFFu : (uint32_t)p; }

/* a few texts kept together (an array of vectors cannot be had): each a span of one pool, its
   length 0 when it is not given; and a number beside each. A local one is zeroed first and
   dropped by its defer */
enum { OD_TS_N = 128 };
typedef struct od_ts { vector<char> pool; uint32_t off[OD_TS_N], ln[OD_TS_N], fl[OD_TS_N]; } od_ts;
static void od_ts_set(od_ts *s, int i, const char *t, size_t n) {
    s->off[i] = (uint32_t)len(s->pool);
    append_range(s->pool, t, n);
    s->ln[i] = (uint32_t)n;
}
static void od_ts_setv(od_ts *s, int i, const vector<char> *v) { od_ts_set(s, i, data(*v), len(*v)); }
static void od_ts_setz(od_ts *s, int i, const char *t) { od_ts_set(s, i, t, strlen(t)); }
static int od_ts_is(const od_ts *s, int i, const char *w) { return css_omx_eq(data(s->pool) + s->off[i], s->ln[i], w, strlen(w)); }
static int od_ts_eq(const od_ts *s, int i, int j) { return css_omx_eq(data(s->pool) + s->off[i], s->ln[i], data(s->pool) + s->off[j], s->ln[j]); }
static void od_ts_out(const od_ts *s, int i, vector<char> *out) { append_range(*out, data(s->pool) + s->off[i], s->ln[i]); }

/* ---- keywords ---- */

/* the keywords Chromium writes in lower case wherever a value has no grammar here: an ident
   among them is lowered, any other kept as written (a custom ident) */
static const char *const od_keywords =
    " auto none normal inherit initial unset revert revert-layer default all both left right top bottom center middle start end "
    "self-start self-end flex-start flex-end space-between space-around space-evenly stretch baseline first last safe unsafe legacy "
    "block inline inline-block flex inline-flex grid inline-grid table inline-table table-row table-cell table-column table-caption "
    "table-row-group table-header-group table-footer-group table-column-group list-item contents flow flow-root ruby run-in math "
    "hidden visible clip scroll overlay collapse static relative absolute fixed sticky solid dashed dotted double groove ridge inset "
    "outset thin medium thick transparent currentcolor min-content max-content fit-content repeat no-repeat repeat-x repeat-y space "
    "round cover contain padding-box border-box content-box text fill-box stroke-box view-box local row column row-reverse "
    "column-reverse wrap nowrap wrap-reverse dense span auto-fill auto-fit subgrid masonry minmax infinite forwards backwards "
    "alternate alternate-reverse reverse running paused linear ease ease-in ease-out ease-in-out step-start step-end jump-start "
    "jump-end jump-none jump-both italic oblique bold bolder lighter condensed expanded semi-condensed semi-expanded extra-condensed "
    "extra-expanded ultra-condensed ultra-expanded small-caps all-small-caps petite-caps all-petite-caps unicase titling-caps "
    "underline overline line-through blink wavy from-font pointer default crosshair move text wait help progress not-allowed "
    "grab grabbing zoom-in zoom-out cell copy alias context-menu vertical-text no-drop all-scroll col-resize row-resize n-resize "
    "e-resize s-resize w-resize ne-resize nw-resize se-resize sw-resize ew-resize ns-resize nesw-resize nwse-resize uppercase "
    "lowercase capitalize full-width full-size-kana break-word break-all keep-all anywhere manual pre pre-wrap pre-line "
    "break-spaces preserve preserve-breaks balance pretty stable ltr rtl isolate bidi-override isolate-override plaintext embed "
    "horizontal-tb vertical-rl vertical-lr sideways-rl sideways-lr mixed upright sideways inside outside disc circle square decimal "
    "decimal-leading-zero lower-roman upper-roman lower-alpha upper-alpha lower-latin upper-latin lower-greek disclosure-open "
    "disclosure-closed open-quote close-quote no-open-quote no-close-quote counter counters attr url linear-gradient "
    "radial-gradient conic-gradient repeating-linear-gradient repeating-radial-gradient repeating-conic-gradient image-set "
    "cross-fade element paint multiply screen darken lighten color-dodge color-burn hard-light soft-light difference exclusion "
    "hue saturation color luminosity plus-lighter pixelated crisp-edges smooth optimizespeed optimizequality geometricprecision "
    "optimizelegibility translate translatex translatey translatez translate3d scale scalex scaley scalez scale3d rotate rotatex "
    "rotatey rotatez rotate3d skew skewx skewy matrix matrix3d perspective blur brightness contrast drop-shadow grayscale "
    "hue-rotate invert opacity saturate sepia calc min max clamp var env rgb rgba hsl hsla hwb lab lch oklab oklch color-mix "
    "light-dark steps cubic-bezier circle ellipse closest-side closest-corner farthest-side farthest-corner at polygon path "
    "inset xywh rect shape evenodd nonzero butt square miter bevel stroke markers fill size inline-size layout style paint strict "
    "content scroll-position contents transform proximity mandatory x y block inline both always avoid avoid-page avoid-column "
    "page column recto verso horizontal vertical zoom manipulation pan-x pan-y pan-left pan-right pan-up pan-down pinch-zoom "
    "economy exact isolation pixelated sub super text-top text-bottom match-parent justify justify-all tb-rl -webkit-left "
    "-webkit-right -webkit-center -webkit-box -webkit-inline-box -webkit-fill-available -webkit-min-content -webkit-max-content "
    "-webkit-fit-content smaller larger xx-small x-small small large x-large xx-large xxx-large serif sans-serif monospace "
    "cursive fantasy system-ui ui-serif ui-sans-serif ui-monospace ui-rounded emoji fangsong caption icon menu message-box "
    "small-caption status-bar hanging each-line on off allow-discrete discrete separate sticky optional swap fallback block "
    "canvas canvastext linktext visitedtext activetext buttonface buttontext buttonborder field fieldtext highlight "
    "highlighttext selecteditem selecteditemtext mark marktext graytext accentcolor accentcolortext black white red green blue "
    "a5 a4 a3 b5 b4 jis-b5 jis-b4 letter legal ledger portrait landscape upright rotate-left rotate-right ";

static int od_keyword(const char *s, size_t n) {
    char buf[64];
    if (n == 0 || n + 2 >= sizeof buf) return 0;
    buf[0] = ' ';
    for (size_t i = 0; i < n; i++) buf[i + 1] = od_low(s[i]);
    buf[n + 1] = ' ';
    buf[n + 2] = 0;
    return strstr(od_keywords, buf) != 0;
}
static int od_wide(const char *s, size_t n) {
    return css_omx_ieq(s, n, "initial") || css_omx_ieq(s, n, "inherit") || css_omx_ieq(s, n, "unset") || css_omx_ieq(s, n, "revert") ||
           css_omx_ieq(s, n, "revert-layer");
}

/* ---- colours (CSS Color 4) ---- */

static const char *const od_named =
    " aliceblue antiquewhite aqua aquamarine azure beige bisque black blanchedalmond blue blueviolet brown burlywood cadetblue "
    "chartreuse chocolate coral cornflowerblue cornsilk crimson cyan darkblue darkcyan darkgoldenrod darkgray darkgreen darkgrey "
    "darkkhaki darkmagenta darkolivegreen darkorange darkorchid darkred darksalmon darkseagreen darkslateblue darkslategray "
    "darkslategrey darkturquoise darkviolet deeppink deepskyblue dimgray dimgrey dodgerblue firebrick floralwhite forestgreen "
    "fuchsia gainsboro ghostwhite gold goldenrod gray green greenyellow grey honeydew hotpink indianred indigo ivory khaki "
    "lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral lightcyan lightgoldenrodyellow lightgray lightgreen "
    "lightgrey lightpink lightsalmon lightseagreen lightskyblue lightslategray lightslategrey lightsteelblue lightyellow lime "
    "limegreen linen magenta maroon mediumaquamarine mediumblue mediumorchid mediumpurple mediumseagreen mediumslateblue "
    "mediumspringgreen mediumturquoise mediumvioletred midnightblue mintcream mistyrose moccasin navajowhite navy oldlace olive "
    "olivedrab orange orangered orchid palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff peru pink plum "
    "powderblue purple rebeccapurple red rosybrown royalblue saddlebrown salmon sandybrown seagreen seashell sienna silver skyblue "
    "slateblue slategray slategrey snow springgreen steelblue tan teal thistle tomato turquoise violet wheat white whitesmoke "
    "yellow yellowgreen transparent currentcolor canvas canvastext linktext visitedtext activetext buttonface buttontext "
    "buttonborder field fieldtext highlight highlighttext selecteditem selecteditemtext mark marktext graytext accentcolor "
    "accentcolortext activeborder activecaption appworkspace background buttonhighlight buttonshadow captiontext inactiveborder "
    "inactivecaption inactivecaptiontext infobackground infotext menu menutext scrollbar threeddarkshadow threedface "
    "threedhighlight threedlightshadow threedshadow window windowframe windowtext -webkit-link -webkit-activelink "
    "-webkit-focus-ring-color ";
static int od_named_color(const char *s, size_t n) {
    char buf[64];
    if (n == 0 || n + 2 >= sizeof buf) return 0;
    buf[0] = ' ';
    for (size_t i = 0; i < n; i++) buf[i + 1] = od_low(s[i]);
    buf[n + 1] = ' ';
    buf[n + 2] = 0;
    return strstr(od_named, buf) != 0;
}
/* an rgb colour as Chromium writes a legacy one: rgb(r, g, b) or rgba(r, g, b, a), a the
   shortest decimal of two or three places that comes back to its byte */
static void od_rgb_text(double r, double g, double b, double a, vector<char> *out) {
    int R = (int)floor(r + 0.5), G = (int)floor(g + 0.5), B = (int)floor(b + 0.5);
    if (R < 0) R = 0;
    if (R > 255) R = 255;
    if (G < 0) G = 0;
    if (G > 255) G = 255;
    if (B < 0) B = 0;
    if (B > 255) B = 255;
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    char buf[96];
    int alpha = (int)floor(a * 255 + 0.5);
    if (alpha >= 255) {
        int k = snprintf(buf, sizeof buf, "rgb(%d, %d, %d)", R, G, B);
        append_range(*out, buf, (size_t)k);
        return;
    }
    double two = floor(alpha / 255.0 * 100 + 0.5) / 100;
    double av = (int)floor(two * 255 + 0.5) == alpha ? two : floor(alpha / 255.0 * 1000 + 0.5) / 1000;
    int k = snprintf(buf, sizeof buf, "rgba(%d, %d, %d, ", R, G, B);
    append_range(*out, buf, (size_t)k);
    css_omx_num(av, out);
    push_back(*out, ')');
}
static double od_hue2rgb(double m1, double m2, double h) {
    if (h < 0) h += 1;
    if (h > 1) h -= 1;
    if (h * 6 < 1) return m1 + (m2 - m1) * h * 6;
    if (h * 2 < 1) return m2;
    if (h * 3 < 2) return m1 + (m2 - m1) * (2.0 / 3 - h) * 6;
    return m1;
}
/* a hue's degrees from a number or an angle token */
static int od_hue(const char *p, const css_omt *t, double *deg) {
    if (t->t == OT_NUM) { *deg = t->v; return 1; }
    if (t->t != OT_DIM) return 0;
    vector<char> u, lu;
    css_omt_unit(p, t, &u);
    css_omx_lower(data(u), len(u), &lu);
    if (od_is(&lu, "deg")) *deg = t->v;
    else if (od_is(&lu, "rad")) *deg = t->v * 180 / 3.14159265358979323846;
    else if (od_is(&lu, "grad")) *deg = t->v * 0.9;
    else if (od_is(&lu, "turn")) *deg = t->v * 360;
    else return 0;
    return 1;
}
/* a colour [a, b): 1 and its text, 0 when it is none */
static int od_color(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return 0;
    css_omt t = (*tk)[a];
    if (t.t == OT_IDENT && a + 1 == b) {
        vector<char> v;
        css_omt_text(p, &t, &v);
        if (!od_named_color(data(v), len(v))) return 0;
        css_omx_lower(data(v), len(v), out);
        return 1;
    }
    if (t.t == OT_HASH && a + 1 == b) {
        vector<char> h;
        css_omt_text(p, &t, &h);
        size_t n = len(h);
        if (n != 3 && n != 4 && n != 6 && n != 8) return 0;
        int c[8];
        for (size_t i = 0; i < n; i++) {
            char x = h[i];
            if (!((x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F'))) return 0;
            c[i] = x >= '0' && x <= '9' ? x - '0' : (x | 32) - 'a' + 10;
        }
        int r, g, bl, al = 255;
        if (n <= 4) {
            r = c[0] * 17; g = c[1] * 17; bl = c[2] * 17;
            if (n == 4) al = c[3] * 17;
        } else {
            r = c[0] * 16 + c[1]; g = c[2] * 16 + c[3]; bl = c[4] * 16 + c[5];
            if (n == 8) al = c[6] * 16 + c[7];
        }
        od_rgb_text(r, g, bl, al / 255.0, out);
        return 1;
    }
    if (t.t != OT_FUNC || t.m + 1 != b) {
        return 0;
    }
    vector<char> fn, lf;
    css_omt_text(p, &t, &fn);
    css_omx_lower(data(fn), len(fn), &lf);
    int rgb = od_is(&lf, "rgb") || od_is(&lf, "rgba"), hsl = od_is(&lf, "hsl") || od_is(&lf, "hsla"), hwb = od_is(&lf, "hwb");
    if (!rgb && !hsl && !hwb) {
        /* the other colour functions: their own syntax, as written but white space and numbers (lab's
           lightness a number: Chromium's) */
        static const char *const others[] = { "lab", "lch", "oklab", "oklch", "color", "color-mix", "light-dark", "-webkit-gradient", 0 };
        int ok = 0;
        for (int k = 0; others[k]; k++) if (od_is(&lf, others[k])) ok = 1;
        if (!ok) return 0;
        if (od_is(&lf, "lab") || od_is(&lf, "lch")) {
            /* L a b [/ alpha]: a percentage of lightness its number */
            vector<char> v;
            append_range(v, data(lf), len(lf));
            push_back(v, '(');
            int k = 0;
            for (uint32_t i = a + 1; i < t.m; i++) {
                css_omt q = (*tk)[i];
                if (q.t == OT_WS) continue;
                if (k++) push_back(v, ' ');
                if (q.t == OT_PCT && k == 1) css_omx_num(q.v, &v);
                else if (q.t == OT_PCT && od_is(&lf, "lab") && k <= 3) css_omx_num(q.v * 1.25, &v);
                else css_omx_tokens(p, tk, i, i + 1, 1, &v);
            }
            push_back(v, ')');
            append_range(*out, data(v), len(v));
            return 1;
        }
        css_omx_tokens(p, tk, a, b, 1, out);
        return 1;
    }
    /* the components: three, then an alpha after a slash or a fourth comma */
    css_omt comp[4];
    int nc = 0, commas = 0, slash = 0;
    for (uint32_t i = a + 1; i < t.m; i++) {
        css_omt q = (*tk)[i];
        if (q.t == OT_WS) continue;
        if (q.t == OT_COMMA) { commas++; continue; }
        if (q.t == OT_DELIM && p[q.s] == '/') { if (nc != 3) return 0; slash = 1; continue; }
        if (nc >= 4) return 0;
        if (q.t == OT_IDENT && css_omt_is(p, &q, "none")) { q.t = OT_NUM; q.v = 0; }
        if (q.t != OT_NUM && q.t != OT_PCT && q.t != OT_DIM) return 0;
        comp[nc++] = q;
    }
    if (nc < 3) return 0;
    if (commas && !((commas == 2 && nc == 3) || (commas == 3 && nc == 4))) return 0;
    if (slash && nc != 4) return 0;
    if (!commas && !slash && nc == 4) return 0;
    double alpha = 1;
    if (nc == 4) {
        if (comp[3].t == OT_PCT) alpha = comp[3].v / 100;
        else if (comp[3].t == OT_NUM) alpha = comp[3].v;
        else return 0;
    }
    if (rgb) {
        double ch[3];
        int pct = comp[0].t == OT_PCT;
        for (int i = 0; i < 3; i++) {
            if (comp[i].t == OT_DIM) return 0;
            if (commas && (comp[i].t == OT_PCT) != pct) return 0;
            ch[i] = comp[i].t == OT_PCT ? comp[i].v * 255 / 100 : comp[i].v;
        }
        od_rgb_text(ch[0], ch[1], ch[2], alpha, out);
        return 1;
    }
    double h;
    if (!od_hue(p, &comp[0], &h)) return 0;
    if (comp[1].t == OT_DIM || comp[2].t == OT_DIM) return 0;
    if (commas && (comp[1].t != OT_PCT || comp[2].t != OT_PCT)) return 0;
    double s = comp[1].v / 100, l = comp[2].v / 100;
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    if (l < 0) l = 0;
    if (l > 1) l = 1;
    h = fmod(h, 360);
    if (h < 0) h += 360;
    h /= 360;
    double r, g, bl;
    if (hsl) {
        double m2 = l <= 0.5 ? l * (s + 1) : l + s - l * s, m1 = l * 2 - m2;
        r = od_hue2rgb(m1, m2, h + 1.0 / 3);
        g = od_hue2rgb(m1, m2, h);
        bl = od_hue2rgb(m1, m2, h - 1.0 / 3);
    } else {
        /* hwb: whiteness s, blackness l */
        double w = s, k = l;
        if (w + k >= 1) { r = g = bl = w / (w + k); }
        else {
            r = od_hue2rgb(0, 1, h + 1.0 / 3) * (1 - w - k) + w;
            g = od_hue2rgb(0, 1, h) * (1 - w - k) + w;
            bl = od_hue2rgb(0, 1, h - 1.0 / 3) * (1 - w - k) + w;
        }
    }
    od_rgb_text(r * 255, g * 255, bl * 255, alpha, out);
    return 1;
}

/* ---- lengths, numbers and calc() ---- */

static const char *const od_len_units = " px em rem ex rex ch rch ic ric cap rcap lh rlh vw vh vi vb vmin vmax svw svh svi svb svmin svmax lvw lvh "
                                         "lvi lvb lvmin lvmax dvw dvh dvi dvb dvmin dvmax cqw cqh cqi cqb cqmin cqmax cm mm q in pt pc ";
static int od_unit_in(const char *units, const vector<char> *u) {
    char buf[32];
    if (!len(*u) || len(*u) + 2 >= sizeof buf) return 0;
    buf[0] = ' ';
    for (size_t i = 0; i < len(*u); i++) buf[i + 1] = od_low((*u)[i]);
    buf[len(*u) + 1] = ' ';
    buf[len(*u) + 2] = 0;
    return strstr(units, buf) != 0;
}
/* a calc term: a coefficient of a unit (an empty unit a number's, % a percentage's) */
typedef struct od_term { double v; char unit[12]; } od_term;
/* the units' kinds a sum may hold: what a value takes (OD_K_*) */
enum { OD_K_LEN = 1, OD_K_PCT = 2, OD_K_NUM = 4, OD_K_TIME = 8, OD_K_ANGLE = 16, OD_K_RES = 32 };
static int od_unit_kind(const char *u) {
    if (!u[0]) return OD_K_NUM;
    if (!strcmp(u, "%")) return OD_K_PCT;
    char buf[16];
    snprintf(buf, sizeof buf, " %s ", u);
    if (strstr(od_len_units, buf)) return OD_K_LEN;
    if (!strcmp(u, "s") || !strcmp(u, "ms")) return OD_K_TIME;
    if (!strcmp(u, "deg") || !strcmp(u, "rad") || !strcmp(u, "grad") || !strcmp(u, "turn")) return OD_K_ANGLE;
    if (!strcmp(u, "dpi") || !strcmp(u, "dpcm") || !strcmp(u, "dppx") || !strcmp(u, "x")) return OD_K_RES;
    return 0;
}
static int od_sum(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, vector<od_term> *out, int depth);
/* a sum's terms combined: like units added, zero ones kept only as needed */
static void od_add_term(vector<od_term> *s, double v, const char *unit) {
    for (size_t k = 0; k < len(*s); k++)
        if (!strcmp((*s)[k].unit, unit)) { (*s)[k].v += v; return; }
    od_term t;
    memset(&t, 0, sizeof t);
    t.v = v;
    snprintf(t.unit, sizeof t.unit, "%s", unit);
    push_back(*s, t);
}
/* a calc() value [*i, b): a product of factors: a number, a dimension, a percentage, a
   parenthesized sum, a nested calc() (min(), max() and clamp() made one term when theirs agree) */
static int od_factor(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, vector<od_term> *out, int depth) {
    *i = css_omt_skip(tk, *i, b);
    if (*i >= b || depth > 32) return 0;
    css_omt t = (*tk)[*i];
    clear(*out);
    if (t.t == OT_NUM) { od_add_term(out, t.v, ""); (*i)++; return 1; }
    if (t.t == OT_PCT) { od_add_term(out, t.v, "%"); (*i)++; return 1; }
    if (t.t == OT_DIM) {
        vector<char> u, lu;
        css_omt_unit(p, &t, &u);
        css_omx_lower(data(u), len(u), &lu);
        push_back(lu, 0);
        if (!od_unit_kind(data(lu))) return 0;
        od_add_term(out, t.v, data(lu));
        (*i)++;
        return 1;
    }
    if (t.t == OT_LPAREN || (t.t == OT_FUNC && css_omt_is(p, &t, "calc"))) {
        uint32_t e = t.m;
        if (e >= b) return 0;
        uint32_t k = *i + 1;
        if (!od_sum(p, tk, &k, e, out, depth + 1)) return 0;
        if (css_omt_skip(tk, k, e) != e) return 0;
        *i = e + 1;
        return 1;
    }
    if (t.t == OT_FUNC && (css_omt_is(p, &t, "min") || css_omt_is(p, &t, "max") || css_omt_is(p, &t, "clamp"))) {
        uint32_t e = t.m;
        if (e >= b) return 0;
        /* each argument one term of one unit, all of the same: resolved; else not reduced here */
        int is_min = css_omt_is(p, &t, "min"), is_clamp = css_omt_is(p, &t, "clamp");
        vector<od_term> arg;
        double vals[16];
        int na = 0;
        char unit[12];
        unit[0] = 0;
        uint32_t k = *i + 1;
        for (;;) {
            if (!od_sum(p, tk, &k, e, &arg, depth + 1)) return 0;
            if (len(arg) != 1 || na >= 16) return 0;
            if (na == 0) snprintf(unit, sizeof unit, "%s", arg[0].unit);
            else if (strcmp(unit, arg[0].unit)) return 0;
            vals[na++] = arg[0].v;
            k = css_omt_skip(tk, k, e);
            if (k >= e) break;
            if ((*tk)[k].t != OT_COMMA) return 0;
            k++;
        }
        double r;
        if (is_clamp) {
            if (na != 3) return 0;
            r = vals[1] < vals[0] ? vals[0] : vals[1];
            if (r > vals[2]) r = vals[2];
            if (vals[0] > vals[2]) r = vals[0];
        } else {
            r = vals[0];
            for (int q = 1; q < na; q++) r = is_min ? (vals[q] < r ? vals[q] : r) : (vals[q] > r ? vals[q] : r);
        }
        od_add_term(out, r, unit);
        *i = e + 1;
        return 1;
    }
    return 0;
}
static int od_product(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, vector<od_term> *out, int depth) {
    if (!od_factor(p, tk, i, b, out, depth)) return 0;
    for (;;) {
        uint32_t k = css_omt_skip(tk, *i, b);
        if (k >= b || (*tk)[k].t != OT_DELIM || (p[(*tk)[k].s] != '*' && p[(*tk)[k].s] != '/')) return 1;
        char op = p[(*tk)[k].s];
        k++;
        vector<od_term> f;
        if (!od_factor(p, tk, &k, b, &f, depth)) return 0;
        *i = k;
        /* one side a number */
        int fn = len(f) == 1 && !f[0].unit[0], on = len(*out) == 1 && !(*out)[0].unit[0];
        if (op == '/') {
            if (!fn || f[0].v == 0) return 0;
            for (size_t q = 0; q < len(*out); q++) (*out)[q].v /= f[0].v;
            continue;
        }
        if (fn) { for (size_t q = 0; q < len(*out); q++) (*out)[q].v *= f[0].v; continue; }
        if (on) {
            double c = (*out)[0].v;
            clear(*out);
            for (size_t q = 0; q < len(f); q++) od_add_term(out, f[q].v * c, f[q].unit);
            continue;
        }
        return 0;
    }
}
static int od_sum(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, vector<od_term> *out, int depth) {
    clear(*out);
    vector<od_term> t;
    if (!od_product(p, tk, i, b, &t, depth)) return 0;
    for (size_t q = 0; q < len(t); q++) od_add_term(out, t[q].v, t[q].unit);
    for (;;) {
        /* + and - need white space about them (CSS Values 4, 10.1) */
        uint32_t k = *i;
        if (k >= b || (*tk)[k].t != OT_WS) return 1;
        k = css_omt_skip(tk, k, b);
        if (k >= b) { *i = k; return 1; }
        css_omt o = (*tk)[k];
        if (o.t != OT_DELIM || (p[o.s] != '+' && p[o.s] != '-')) return 1;
        if (k + 1 >= b || (*tk)[k + 1].t != OT_WS) return 0;
        double sign = p[o.s] == '-' ? -1 : 1;
        k++;
        vector<od_term> r;
        if (!od_product(p, tk, &k, b, &r, depth)) return 0;
        for (size_t q = 0; q < len(r); q++) od_add_term(out, sign * r[q].v, r[q].unit);
        *i = k;
    }
}
/* a sum's text as Chromium writes calc()'s: a number first, then a percentage, then the
   dimensions in their units' order; "calc(" before it when it is to be one */
static void od_sum_text(vector<od_term> *s, vector<char> *out) {
    /* sorted: number, %, then units by name */
    for (size_t a = 1; a < len(*s); a++)
        for (size_t z = a; z > 0; z--) {
            od_term x = (*s)[z - 1], y = (*s)[z];
            int kx = !x.unit[0] ? 0 : !strcmp(x.unit, "%") ? 1 : 2, ky = !y.unit[0] ? 0 : !strcmp(y.unit, "%") ? 1 : 2;
            int swap_them = kx > ky || (kx == ky && kx == 2 && strcmp(x.unit, y.unit) > 0);
            if (!swap_them) break;
            (*s)[z - 1] = y;
            (*s)[z] = x;
        }
    /* zero terms dropped while another remains */
    vector<od_term> kept;
    for (size_t q = 0; q < len(*s); q++) if ((*s)[q].v != 0) push_back(kept, (*s)[q]);
    if (!len(kept) && len(*s)) push_back(kept, (*s)[len(*s) - 1]);
    for (size_t q = 0; q < len(kept); q++) {
        double v = kept[q].v;
        if (q) {
            if (v < 0) { append_range(*out, " - ", 3); v = -v; }
            else append_range(*out, " + ", 3);
        }
        css_omx_num(v, out);
        od_cat(out, kept[q].unit);
    }
}
/* a calc(), min(), max() or clamp() [a, b) whose terms are of the kinds allowed: 1 and its text */
static int od_calc(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int kinds, vector<char> *out) {
    css_omt t = (*tk)[a];
    if (t.t != OT_FUNC || t.m + 1 != b) return 0;
    vector<od_term> s;
    uint32_t i = a;
    if (css_omt_is(p, &t, "calc")) {
        i = a + 1;
        if (!od_sum(p, tk, &i, t.m, &s, 0) || css_omt_skip(tk, i, t.m) != t.m) return 0;
    } else if (css_omt_is(p, &t, "min") || css_omt_is(p, &t, "max") || css_omt_is(p, &t, "clamp")) {
        uint32_t k = a;
        if (!od_factor(p, tk, &k, b, &s, 0)) {
            /* arguments that do not reduce: each written as a sum of its own */
            vector<char> fn, lf;
            css_omt_text(p, &t, &fn);
            css_omx_lower(data(fn), len(fn), &lf);
            vector<char> v;
            append_range(v, data(lf), len(lf));
            push_back(v, '(');
            uint32_t q = a + 1;
            int n = 0;
            while (q < t.m) {
                vector<od_term> arg;
                if (!od_sum(p, tk, &q, t.m, &arg, 0)) return 0;
                for (size_t z = 0; z < len(arg); z++) {
                    char u[16];
                    snprintf(u, sizeof u, "%s", arg[z].unit);
                    if (!(od_unit_kind(u) & kinds)) return 0;
                }
                if (n++) append_range(v, ", ", 2);
                if (len(arg) > 1) od_cat(&v, "calc(");
                od_sum_text(&arg, &v);
                if (len(arg) > 1) push_back(v, ')');
                q = css_omt_skip(tk, q, t.m);
                if (q >= t.m) break;
                if ((*tk)[q].t != OT_COMMA) return 0;
                q++;
            }
            push_back(v, ')');
            append_range(*out, data(v), len(v));
            return 1;
        }
    } else return 0;
    for (size_t z = 0; z < len(s); z++) {
        char u[16];
        snprintf(u, sizeof u, "%s", s[z].unit);
        if (!(od_unit_kind(u) & kinds)) return 0;
    }
    od_cat(out, "calc(");
    od_sum_text(&s, out);
    push_back(*out, ')');
    return 1;
}

/* a single token's (or calc()'s) value of the kinds given: 1 and its text. nonneg: a negative
   one refused; zero_len: a unitless zero a length's 0px */
static int od_numeric(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int kinds, int nonneg, vector<char> *out) {
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return 0;
    css_omt t = (*tk)[a];
    if (t.t == OT_FUNC) return od_calc(p, tk, a, b, kinds, out);
    if (a + 1 != b) return 0;
    if (nonneg && t.v < 0 && (t.t == OT_NUM || t.t == OT_PCT || t.t == OT_DIM)) return 0;
    if (t.t == OT_NUM) {
        if (kinds & OD_K_NUM) { css_omx_num(t.v, out); return 1; }
        if ((kinds & OD_K_LEN) && t.v == 0) { od_cat(out, "0px"); return 1; }
        return 0;
    }
    if (t.t == OT_PCT) {
        if (!(kinds & OD_K_PCT)) return 0;
        css_omx_num(t.v, out);
        push_back(*out, '%');
        return 1;
    }
    if (t.t == OT_DIM) {
        vector<char> u, lu;
        css_omt_unit(p, &t, &u);
        css_omx_lower(data(u), len(u), &lu);
        push_back(lu, 0);
        if (!(od_unit_kind(data(lu)) & kinds)) return 0;
        css_omx_num(t.v, out);
        od_cat(out, data(lu));
        return 1;
    }
    return 0;
}

/* ---- longhands' grammars ---- */

/* a longhand's grammar: a single value of the kinds given and the keywords given (a
   space-separated list), a list of them (OD_LIST, a comma apart), or one of the readers
   below */
enum { OD_G_ANY = 0, OD_G_ONE, OD_G_LIST, OD_G_COLOR, OD_G_FAMILY, OD_G_DISPLAY, OD_G_OPACITY, OD_G_WEIGHT, OD_G_SHADOW, OD_G_RATIO,
       OD_G_RADIUS, OD_G_REPEAT, OD_G_EASING, OD_G_LINE, OD_G_CUSTOM_IDENTS, OD_G_IMAGES, OD_G_STYLE_FONT, OD_G_ANIM_NAME, OD_G_SIZE_BG,
       OD_G_DECO_LINE };
typedef struct od_gram { const char *name; int g, kinds, nonneg; const char *kw; } od_gram;
static const od_gram od_grams[] = {
    { "color", OD_G_COLOR, 0, 0, 0 }, { "background-color", OD_G_COLOR, 0, 0, 0 }, { "border-top-color", OD_G_COLOR, 0, 0, 0 },
    { "border-right-color", OD_G_COLOR, 0, 0, 0 }, { "border-bottom-color", OD_G_COLOR, 0, 0, 0 }, { "border-left-color", OD_G_COLOR, 0, 0, 0 },
    { "border-block-start-color", OD_G_COLOR, 0, 0, 0 }, { "border-block-end-color", OD_G_COLOR, 0, 0, 0 },
    { "border-inline-start-color", OD_G_COLOR, 0, 0, 0 }, { "border-inline-end-color", OD_G_COLOR, 0, 0, 0 },
    { "outline-color", OD_G_COLOR, 0, 0, 0 }, { "text-decoration-color", OD_G_COLOR, 0, 0, 0 }, { "column-rule-color", OD_G_COLOR, 0, 0, 0 },
    { "caret-color", OD_G_COLOR, 0, 0, "auto" }, { "accent-color", OD_G_COLOR, 0, 0, "auto" }, { "stop-color", OD_G_COLOR, 0, 0, 0 },
    { "flood-color", OD_G_COLOR, 0, 0, 0 }, { "lighting-color", OD_G_COLOR, 0, 0, 0 }, { "text-emphasis-color", OD_G_COLOR, 0, 0, 0 },
    { "-webkit-text-fill-color", OD_G_COLOR, 0, 0, 0 }, { "-webkit-text-stroke-color", OD_G_COLOR, 0, 0, 0 },
    { "-webkit-tap-highlight-color", OD_G_COLOR, 0, 0, 0 },
    { "width", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "height", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "inline-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available" },
    { "block-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available" },
    { "min-width", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "min-height", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "max-width", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "none min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "max-height", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "none min-content max-content fit-content stretch -webkit-fill-available -webkit-min-content -webkit-max-content -webkit-fit-content" },
    { "min-inline-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content" },
    { "min-block-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto min-content max-content fit-content" },
    { "max-inline-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "none min-content max-content fit-content" },
    { "max-block-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "none min-content max-content fit-content" },
    { "margin-top", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "margin-right", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "margin-bottom", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "margin-left", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "margin-block-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "margin-block-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "margin-inline-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "margin-inline-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "padding-top", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 }, { "padding-right", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 },
    { "padding-bottom", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 }, { "padding-left", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 },
    { "padding-block-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 }, { "padding-block-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 },
    { "padding-inline-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 }, { "padding-inline-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, 0 },
    { "top", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "right", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "bottom", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "left", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "inset-block-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "inset-block-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "inset-inline-start", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" }, { "inset-inline-end", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "scroll-margin-top", OD_G_ONE, OD_K_LEN, 0, 0 }, { "scroll-margin-right", OD_G_ONE, OD_K_LEN, 0, 0 },
    { "scroll-margin-bottom", OD_G_ONE, OD_K_LEN, 0, 0 }, { "scroll-margin-left", OD_G_ONE, OD_K_LEN, 0, 0 },
    { "scroll-padding-top", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto" }, { "scroll-padding-right", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto" },
    { "scroll-padding-bottom", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto" }, { "scroll-padding-left", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto" },
    { "border-top-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" }, { "border-right-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" },
    { "border-bottom-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" }, { "border-left-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" },
    { "border-block-start-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" }, { "border-block-end-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" },
    { "border-inline-start-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" }, { "border-inline-end-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" },
    { "outline-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" }, { "column-rule-width", OD_G_ONE, OD_K_LEN, 1, "thin medium thick" },
    { "border-top-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-right-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-bottom-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-left-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-block-start-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-block-end-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-inline-start-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "border-inline-end-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "column-rule-style", OD_G_ONE, 0, 0, "none hidden dotted dashed solid double groove ridge inset outset" },
    { "outline-style", OD_G_ONE, 0, 0, "auto none dotted dashed solid double groove ridge inset outset" },
    { "outline-offset", OD_G_ONE, OD_K_LEN, 0, 0 },
    { "-webkit-border-horizontal-spacing", OD_G_ONE, OD_K_LEN, 1, 0 }, { "-webkit-border-vertical-spacing", OD_G_ONE, OD_K_LEN, 1, 0 },
    { "border-top-left-radius", OD_G_RADIUS, 0, 0, 0 }, { "border-top-right-radius", OD_G_RADIUS, 0, 0, 0 },
    { "border-bottom-right-radius", OD_G_RADIUS, 0, 0, 0 }, { "border-bottom-left-radius", OD_G_RADIUS, 0, 0, 0 },
    { "border-start-start-radius", OD_G_RADIUS, 0, 0, 0 }, { "border-start-end-radius", OD_G_RADIUS, 0, 0, 0 },
    { "border-end-start-radius", OD_G_RADIUS, 0, 0, 0 }, { "border-end-end-radius", OD_G_RADIUS, 0, 0, 0 },
    { "display", OD_G_DISPLAY, 0, 0, 0 },
    { "position", OD_G_ONE, 0, 0, "static relative absolute fixed sticky" },
    { "float", OD_G_ONE, 0, 0, "none left right inline-start inline-end" },
    { "clear", OD_G_ONE, 0, 0, "none left right both inline-start inline-end" },
    { "visibility", OD_G_ONE, 0, 0, "visible hidden collapse" },
    { "overflow-x", OD_G_ONE, 0, 0, "visible hidden clip scroll auto overlay" },
    { "overflow-y", OD_G_ONE, 0, 0, "visible hidden clip scroll auto overlay" },
    { "box-sizing", OD_G_ONE, 0, 0, "content-box border-box" },
    { "z-index", OD_G_ONE, OD_K_NUM, 0, "auto" },
    { "order", OD_G_ONE, OD_K_NUM, 0, 0 },
    { "opacity", OD_G_OPACITY, 0, 0, 0 }, { "fill-opacity", OD_G_OPACITY, 0, 0, 0 }, { "stroke-opacity", OD_G_OPACITY, 0, 0, 0 },
    { "stop-opacity", OD_G_OPACITY, 0, 0, 0 }, { "flood-opacity", OD_G_OPACITY, 0, 0, 0 },
    { "line-height", OD_G_ONE, OD_K_LEN | OD_K_PCT | OD_K_NUM, 1, "normal" },
    { "font-size", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "xx-small x-small small medium large x-large xx-large xxx-large larger smaller math" },
    { "font-weight", OD_G_WEIGHT, 0, 0, 0 },
    { "font-style", OD_G_STYLE_FONT, 0, 0, 0 },
    { "font-family", OD_G_FAMILY, 0, 0, 0 },
    { "font-stretch", OD_G_ONE, OD_K_PCT, 1, "normal ultra-condensed extra-condensed condensed semi-condensed semi-expanded expanded extra-expanded ultra-expanded" },
    { "font-variant-caps", OD_G_ONE, 0, 0, "normal small-caps all-small-caps petite-caps all-petite-caps unicase titling-caps" },
    { "font-kerning", OD_G_ONE, 0, 0, "auto normal none" },
    { "text-align", OD_G_ONE, 0, 0, "start end left right center justify match-parent -webkit-left -webkit-right -webkit-center -webkit-auto -internal-center" },
    { "text-align-last", OD_G_ONE, 0, 0, "auto start end left right center justify match-parent" },
    { "text-transform", OD_G_ONE, 0, 0, "none capitalize uppercase lowercase full-width full-size-kana math-auto" },
    { "text-indent", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, 0 },
    { "letter-spacing", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "normal" }, { "word-spacing", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "normal" },
    { "white-space-collapse", OD_G_ONE, 0, 0, "collapse preserve preserve-breaks break-spaces preserve-spaces" },
    { "text-wrap-mode", OD_G_ONE, 0, 0, "wrap nowrap" }, { "text-wrap-style", OD_G_ONE, 0, 0, "auto balance pretty stable" },
    { "vertical-align", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "baseline sub super text-top text-bottom middle top bottom -webkit-baseline-middle" },
    { "list-style-position", OD_G_ONE, 0, 0, "inside outside" },
    { "list-style-image", OD_G_IMAGES, 0, 0, 0 }, { "background-image", OD_G_IMAGES, 0, 0, 0 }, { "mask-image", OD_G_IMAGES, 0, 0, 0 },
    { "border-image-source", OD_G_IMAGES, 0, 0, 0 },
    { "background-repeat", OD_G_REPEAT, 0, 0, 0 },
    { "background-attachment", OD_G_LIST, 0, 0, "scroll fixed local" },
    { "background-origin", OD_G_LIST, 0, 0, "border-box padding-box content-box" },
    { "background-clip", OD_G_LIST, 0, 0, "border-box padding-box content-box text" },
    { "background-size", OD_G_SIZE_BG, 0, 0, 0 },
    { "flex-grow", OD_G_ONE, OD_K_NUM, 1, 0 }, { "flex-shrink", OD_G_ONE, OD_K_NUM, 1, 0 },
    { "flex-basis", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "auto content min-content max-content fit-content" },
    { "flex-direction", OD_G_ONE, 0, 0, "row row-reverse column column-reverse" },
    { "flex-wrap", OD_G_ONE, 0, 0, "nowrap wrap wrap-reverse" },
    { "row-gap", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "normal" }, { "column-gap", OD_G_ONE, OD_K_LEN | OD_K_PCT, 1, "normal" },
    { "grid-row-start", OD_G_LINE, 0, 0, 0 }, { "grid-row-end", OD_G_LINE, 0, 0, 0 }, { "grid-column-start", OD_G_LINE, 0, 0, 0 },
    { "grid-column-end", OD_G_LINE, 0, 0, 0 },
    { "grid-auto-flow", OD_G_ANY, 0, 0, 0 },
    { "transition-duration", OD_G_LIST, OD_K_TIME, 1, 0 }, { "transition-delay", OD_G_LIST, OD_K_TIME, 0, 0 },
    { "transition-timing-function", OD_G_EASING, 0, 0, 0 }, { "animation-timing-function", OD_G_EASING, 0, 0, 0 },
    { "transition-behavior", OD_G_LIST, 0, 0, "normal allow-discrete" },
    { "transition-property", OD_G_CUSTOM_IDENTS, 0, 0, "none all" },
    { "animation-duration", OD_G_LIST, OD_K_TIME, 1, "auto" }, { "animation-delay", OD_G_LIST, OD_K_TIME, 0, 0 },
    { "animation-iteration-count", OD_G_LIST, OD_K_NUM, 1, "infinite" },
    { "animation-direction", OD_G_LIST, 0, 0, "normal reverse alternate alternate-reverse" },
    { "animation-fill-mode", OD_G_LIST, 0, 0, "none forwards backwards both" },
    { "animation-play-state", OD_G_LIST, 0, 0, "running paused" },
    { "animation-name", OD_G_ANIM_NAME, 0, 0, 0 },
    { "box-shadow", OD_G_SHADOW, 0, 0, 0 }, { "text-shadow", OD_G_SHADOW, 0, 0, 0 },
    { "aspect-ratio", OD_G_RATIO, 0, 0, 0 },
    { "object-fit", OD_G_ONE, 0, 0, "fill contain cover none scale-down" },
    { "cursor", OD_G_ANY, 0, 0, 0 },
    { "pointer-events", OD_G_ONE, 0, 0, "auto none visiblepainted visiblefill visiblestroke visible painted fill stroke all bounding-box" },
    { "user-select", OD_G_ONE, 0, 0, "auto none text all contain" },
    { "appearance", OD_G_ANY, 0, 0, 0 },
    { "tab-size", OD_G_ONE, OD_K_LEN | OD_K_NUM, 1, 0 },
    { "container-name", OD_G_CUSTOM_IDENTS, 0, 0, "none" },
    { "container-type", OD_G_ANY, 0, 0, 0 },
    { "text-decoration-line", OD_G_DECO_LINE, 0, 0, 0 },
    { "text-decoration-style", OD_G_ONE, 0, 0, "solid double dotted dashed wavy" },
    { "text-decoration-thickness", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto from-font" },
    { "text-underline-offset", OD_G_ONE, OD_K_LEN | OD_K_PCT, 0, "auto" },
    { "overflow-wrap", OD_G_ONE, 0, 0, "normal break-word anywhere" },
    { "word-break", OD_G_ONE, 0, 0, "normal break-all keep-all break-word auto-phrase" },
    { "hyphens", OD_G_ONE, 0, 0, "none manual auto" },
    { "direction", OD_G_ONE, 0, 0, "ltr rtl" },
    { "unicode-bidi", OD_G_ONE, 0, 0, "normal embed isolate bidi-override isolate-override plaintext" },
    { "writing-mode", OD_G_ONE, 0, 0, "horizontal-tb vertical-rl vertical-lr sideways-rl sideways-lr lr lr-tb rl rl-tb tb tb-rl" },
    { "text-overflow", OD_G_ONE, 0, 0, "clip ellipsis" },
    { "table-layout", OD_G_ONE, 0, 0, "auto fixed" }, { "border-collapse", OD_G_ONE, 0, 0, "separate collapse" },
    { "caption-side", OD_G_ONE, 0, 0, "top bottom" }, { "empty-cells", OD_G_ONE, 0, 0, "show hide" },
    { "isolation", OD_G_ONE, 0, 0, "auto isolate" },
    { "resize", OD_G_ONE, 0, 0, "none both horizontal vertical block inline" },
    { "will-change", OD_G_CUSTOM_IDENTS, 0, 0, "auto" },
    { "counter-reset", OD_G_ANY, 0, 0, 0 }, { "counter-increment", OD_G_ANY, 0, 0, 0 }, { "counter-set", OD_G_ANY, 0, 0, 0 },
    { "list-style-type", OD_G_ANY, 0, 0, 0 },
    { "quotes", OD_G_ANY, 0, 0, 0 },
    { "content", OD_G_ANY, 0, 0, 0 },
    { "-webkit-line-clamp", OD_G_ONE, OD_K_NUM, 1, "none" },
    { "contain", OD_G_ANY, 0, 0, 0 },
    { "transform", OD_G_ANY, 0, 0, 0 },
    { 0, 0, 0, 0, 0 } };

static int od_gram_of(uint32_t prop) {
    char n[64];
    od_copyz(n, sizeof n, css_om_prop_name(prop));
    for (int k = 0; od_grams[k].name; k++) if (!strcmp(od_grams[k].name, n)) return k;
    return -1;
}
static int od_word_in(const char *list, const char *s, size_t n) {
    if (!list) return 0;
    size_t i = 0, ln = strlen(list);
    while (i < ln) {
        size_t e = i;
        while (e < ln && list[e] != ' ') e++;
        if (e - i == n) {
            int same = 1;
            for (size_t k = 0; k < n && same; k++) if (od_low(s[k]) != list[i + k]) same = 0;
            if (same) return 1;
        }
        i = e + 1;
    }
    return 0;
}
static void od_generic(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out);
/* one component value [a, b) by a grammar of kinds and keywords: 1 and its text */
static int od_one(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int kinds, int nonneg, const char *kw, vector<char> *out) {
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return 0;
    css_omt t = (*tk)[a];
    if (t.t == OT_IDENT && a + 1 == b) {
        vector<char> v;
        css_omt_text(p, &t, &v);
        if (!od_word_in(kw, data(v), len(v))) return 0;
        css_omx_lower(data(v), len(v), out);
        return 1;
    }
    if (t.t == OT_FUNC && kw && css_omt_is(p, &t, "fit-content") && od_word_in(kw, "fit-content", 11) && t.m + 1 == b) {
        od_cat(out, "fit-content(");
        if (!od_numeric(p, tk, a + 1, t.m, OD_K_LEN | OD_K_PCT, 1, out)) return 0;
        push_back(*out, ')');
        return 1;
    }
    /* anchor positioning's functions where a length is (CSS Anchor Positioning 1), as written */
    if (t.t == OT_FUNC && (kinds & OD_K_LEN) && t.m + 1 == b &&
        (css_omt_is(p, &t, "anchor") || css_omt_is(p, &t, "anchor-size") || css_omt_is(p, &t, "calc-size"))) {
        od_generic(p, tk, a, b, out);
        return 1;
    }
    if (!kinds) return 0;
    return od_numeric(p, tk, a, b, kinds, nonneg, out);
}
/* a comma-separated list, each item by one's grammar */
static int od_list(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int kinds, int nonneg, const char *kw, vector<char> *out) {
    uint32_t i = a;
    int n = 0;
    for (;;) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) {
            css_omt q = (*tk)[e];
            if ((q.t == OT_FUNC || q.t == OT_LPAREN) && q.m < b) e = q.m;
            e++;
        }
        if (n++) append_range(*out, ", ", 2);
        if (!od_one(p, tk, i, e, kinds, nonneg, kw, out)) return 0;
        if (e >= b) return 1;
        i = e + 1;
    }
}
/* the items of a comma-separated list [a, b): their starts and ends (at most max) */
static int od_items(const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t *st, uint32_t *en, int max) {
    uint32_t i = a;
    int n = 0;
    for (;;) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) {
            css_omt q = (*tk)[e];
            if ((q.t == OT_FUNC || q.t == OT_LPAREN || q.t == OT_LBRACK) && q.m < b) e = q.m;
            e++;
        }
        if (n >= max) return -1;
        st[n] = i;
        en[n] = e;
        n++;
        if (e >= b) return n;
        i = e + 1;
    }
}
/* the space-separated components of [a, b): their places (functions and blocks whole) */
static int od_parts(const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t *st, uint32_t *en, int max) {
    int n = 0;
    uint32_t i = css_omt_skip(tk, a, b);
    while (i < b) {
        css_omt t = (*tk)[i];
        uint32_t e = i + 1;
        if ((t.t == OT_FUNC || t.t == OT_LPAREN || t.t == OT_LBRACK || t.t == OT_LBRACE) && t.m < b) e = t.m + 1;
        if (n >= max) return -1;
        st[n] = i;
        en[n] = e;
        n++;
        i = css_omt_skip(tk, e, b);
    }
    return n;
}
/* a value with no grammar here, its tokens written (keywords lowered, others as written) */
static void od_generic(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out);
static void od_generic_rec(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    int space = 0;
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS) { space = 1; continue; }
        if (t.t == OT_COMMA) { append_range(*out, ", ", 2); space = 0; continue; }
        if (space && len(*out) && (*out)[len(*out) - 1] != ' ' && (*out)[len(*out) - 1] != '(' && (*out)[len(*out) - 1] != '[') push_back(*out, ' ');
        space = 0;
        vector<char> v;
        if (t.t == OT_IDENT) {
            css_omt_text(p, &t, &v);
            if (od_keyword(data(v), len(v))) css_omx_lower(data(v), len(v), out);
            else css_omx_ident(data(v), len(v), out);
            continue;
        }
        if (t.t == OT_HASH) {
            vector<char> c;
            if (od_color(p, tk, i, i + 1, &c)) { append_range(*out, data(c), len(c)); continue; }
        }
        if (t.t == OT_FUNC) {
            vector<char> c;
            if (od_color(p, tk, i, t.m + 1 <= b ? t.m + 1 : b, &c)) { append_range(*out, data(c), len(c)); i = t.m < b ? t.m : b; continue; }
            clear(c);
            if (od_calc(p, tk, i, t.m + 1 <= b ? t.m + 1 : b, OD_K_LEN | OD_K_PCT | OD_K_NUM | OD_K_TIME | OD_K_ANGLE | OD_K_RES, &c)) {
                append_range(*out, data(c), len(c));
                i = t.m < b ? t.m : b;
                continue;
            }
            if (css_omt_is(p, &t, "var") || css_omt_is(p, &t, "env")) {
                css_omx_raw(p, tk, i, (t.m < b ? t.m : b) + 1 <= b ? (t.m < b ? t.m : b) + 1 : b, 0, out);
                i = t.m < b ? t.m : b;
                continue;
            }
            css_omt_text(p, &t, &v);
            css_omx_lower(data(v), len(v), out);
            push_back(*out, '(');
            uint32_t e = t.m < b ? t.m : b;
            od_generic_rec(p, tk, i + 1, e, out);
            while (len(*out) && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
            push_back(*out, ')');
            i = e;
            continue;
        }
        if (t.t == OT_LPAREN || t.t == OT_LBRACK) {
            push_back(*out, t.t == OT_LPAREN ? '(' : '[');
            uint32_t e = t.m < b ? t.m : b;
            od_generic_rec(p, tk, i + 1, e, out);
            while (len(*out) && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
            push_back(*out, t.t == OT_LPAREN ? ')' : ']');
            i = e;
            continue;
        }
        css_omx_tokens(p, tk, i, i + 1, 1, out);
    }
}
static void od_generic(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    size_t start = len(*out);
    od_generic_rec(p, tk, a, b, out);
    while (len(*out) > start && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
}
/* whether [a, b) holds a var() or env() at any depth */
static int od_has_var(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b) {
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_FUNC && (css_omt_is(p, &t, "var") || css_omt_is(p, &t, "env") || css_omt_is(p, &t, "attr"))) return 1;
    }
    return 0;
}
/* a font family list: names a comma apart, each a string or idents, as Chromium writes them
   (an identifier as it is, a generic family's keyword lowered, any other name a string) */
static const char *const od_generics = "serif sans-serif monospace cursive fantasy system-ui math emoji fangsong ui-serif ui-sans-serif ui-monospace ui-rounded -webkit-body -webkit-standard -webkit-pictograph";
static int od_ident_ok(const char *s, size_t n) {
    /* a valid identifier: what CSS.escape would leave alone, no escape needed */
    vector<char> e;
    css_omx_ident(s, n, &e);
    return css_omx_eq(data(e), len(e), s, n) && n > 0;
}
static int od_family(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[64], en[64];
    int n = od_items(tk, a, b, st, en, 64);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t i = css_omt_skip(tk, st[k], en[k]), e = en[k];
        while (e > i && (*tk)[e - 1].t == OT_WS) e--;
        if (i >= e) return 0;
        if (k) append_range(*out, ", ", 2);
        css_omt t = (*tk)[i];
        if (t.t == OT_STR) {
            if (i + 1 != e) return 0;
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (od_ident_ok(data(v), len(v)) && !od_word_in(od_generics, data(v), len(v)) && !od_wide(data(v), len(v)) && !css_omx_ieq(data(v), len(v), "default"))
                append_range(*out, data(v), len(v));
            else css_omx_string(data(v), len(v), out);
            continue;
        }
        vector<char> name;
        int words = 0;
        for (uint32_t q = i; q < e; q++) {
            css_omt w = (*tk)[q];
            if (w.t == OT_WS) continue;
            if (w.t != OT_IDENT) return 0;
            vector<char> v;
            css_omt_text(p, &w, &v);
            if (words == 0 && i + 1 == e && od_word_in(od_generics, data(v), len(v))) { css_omx_lower(data(v), len(v), out); words = -1; break; }
            if (od_wide(data(v), len(v)) || css_omx_ieq(data(v), len(v), "default")) return 0;
            if (words++) push_back(name, ' ');
            append_range(name, data(v), len(v));
        }
        if (words < 0) continue;
        if (words == 1) css_omx_ident(data(name), len(name), out);
        else css_omx_string(data(name), len(name), out);
    }
    return 1;
}
/* display (CSS Display 3): one keyword, or the outer and inner pair Chromium writes as one */
static int od_display(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[4], en[4];
    int n = od_parts(tk, a, b, st, en, 4);
    if (n <= 0 || n > 3) return 0;
    char w[3][32];
    for (int k = 0; k < n; k++) {
        if (en[k] != st[k] + 1 || (*tk)[st[k]].t != OT_IDENT) return 0;
        css_omt t = (*tk)[st[k]];
        vector<char> v;
        css_omt_text(p, &t, &v);
        if (len(v) >= sizeof w[0]) return 0;
        for (size_t i = 0; i < len(v); i++) w[k][i] = od_low(v[i]);
        w[k][len(v)] = 0;
    }
    static const char *const one = "none contents block inline inline-block flex inline-flex grid inline-grid table inline-table table-row table-cell "
                                   "table-column table-caption table-row-group table-header-group table-footer-group table-column-group list-item "
                                   "flow-root ruby ruby-text math inline-math block-math -webkit-box -webkit-inline-box run-in flow masonry inline-masonry";
    if (n == 1) {
        if (!od_word_in(one, w[0], strlen(w[0]))) return 0;
        if (!strcmp(w[0], "flow")) od_cat(out, "block");
        else od_cat(out, w[0]);
        return 1;
    }
    /* outer and inner (and list-item): their places among the words, -1 none given */
    int outer = -1, inner = -1, li = 0;
    for (int k = 0; k < n; k++) {
        if (!strcmp(w[k], "block") || !strcmp(w[k], "inline") || !strcmp(w[k], "run-in")) { if (outer >= 0) return 0; outer = k; }
        else if (!strcmp(w[k], "flow") || !strcmp(w[k], "flow-root") || !strcmp(w[k], "flex") || !strcmp(w[k], "grid") || !strcmp(w[k], "table") ||
                 !strcmp(w[k], "ruby") || !strcmp(w[k], "math")) {
            if (inner >= 0) return 0;
            inner = k;
        } else if (!strcmp(w[k], "list-item")) { if (li) return 0; li = 1; }
        else return 0;
    }
    char o[32], in[32];
    snprintf(o, sizeof o, "%s", outer >= 0 ? w[outer] : "block");
    snprintf(in, sizeof in, "%s", inner >= 0 ? w[inner] : "flow");
    if (li) {
        if (strcmp(in, "flow") && strcmp(in, "flow-root")) return 0;
        if (!strcmp(o, "block") && !strcmp(in, "flow")) { od_cat(out, "list-item"); return 1; }
        od_cat(out, o);
        push_back(*out, ' ');
        od_cat(out, in);
        od_cat(out, " list-item");
        return 1;
    }
    if (!strcmp(o, "block")) {
        od_cat(out, !strcmp(in, "flow") ? "block" : in);
        return 1;
    }
    if (!strcmp(o, "inline")) {
        if (!strcmp(in, "flow")) od_cat(out, "inline");
        else if (!strcmp(in, "flow-root")) od_cat(out, "inline-block");
        else if (!strcmp(in, "flex")) od_cat(out, "inline-flex");
        else if (!strcmp(in, "grid")) od_cat(out, "inline-grid");
        else if (!strcmp(in, "table")) od_cat(out, "inline-table");
        else if (!strcmp(in, "math")) od_cat(out, "math");
        else od_cat(out, in);
        return 1;
    }
    od_cat(out, o);
    push_back(*out, ' ');
    od_cat(out, in);
    return 1;
}
/* a shadow list (box-shadow, text-shadow): each shadow its colour, then its lengths, then inset,
   as Chromium writes them */
static int od_shadow(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int box, vector<char> *out) {
    uint32_t i0 = css_omt_skip(tk, a, b);
    if (i0 < b && (*tk)[i0].t == OT_IDENT && css_omt_is(p, &(*tk)[i0], "none") && css_omt_skip(tk, i0 + 1, b) == b) { od_cat(out, "none"); return 1; }
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t ps[8], pe[8];
        int np = od_parts(tk, st[k], en[k], ps, pe, 8);
        if (np <= 0) return 0;
        vector<char> color, lens;
        int nl = 0, inset = 0, lens_done = 0;
        for (int q = 0; q < np; q++) {
            css_omt t = (*tk)[ps[q]];
            if (box && t.t == OT_IDENT && css_omt_is(p, &t, "inset") && pe[q] == ps[q] + 1) { if (inset) return 0; inset = 1; if (nl) lens_done = 1; continue; }
            vector<char> c;
            if (od_color(p, tk, ps[q], pe[q], &c)) { if (len(color)) return 0; append_range(color, data(c), len(c)); if (nl) lens_done = 1; continue; }
            vector<char> l;
            if (lens_done) return 0;
            if (!od_numeric(p, tk, ps[q], pe[q], OD_K_LEN, nl == 2, &l)) return 0;
            if (nl >= (box ? 4 : 3)) return 0;
            if (nl) push_back(lens, ' ');
            append_range(lens, data(l), len(l));
            nl++;
        }
        if (nl < 2) return 0;
        if (k) append_range(*out, ", ", 2);
        if (len(color)) { append_range(*out, data(color), len(color)); push_back(*out, ' '); }
        append_range(*out, data(lens), len(lens));
        if (inset) od_cat(out, " inset");
    }
    return 1;
}
/* the easing functions: keywords, cubic-bezier(), steps() (its default end left out), linear() */
static int od_easing(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t i = css_omt_skip(tk, st[k], en[k]), e = en[k];
        while (e > i && (*tk)[e - 1].t == OT_WS) e--;
        if (i >= e) return 0;
        if (k) append_range(*out, ", ", 2);
        css_omt t = (*tk)[i];
        if (t.t == OT_IDENT && i + 1 == e) {
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (!od_word_in("linear ease ease-in ease-out ease-in-out step-start step-end", data(v), len(v))) return 0;
            css_omx_lower(data(v), len(v), out);
            continue;
        }
        if (t.t != OT_FUNC || t.m + 1 != e) return 0;
        if (css_omt_is(p, &t, "steps")) {
            uint32_t q = css_omt_skip(tk, i + 1, t.m);
            if (q >= t.m || (*tk)[q].t != OT_NUM || !((*tk)[q].f & OTF_INT) || (*tk)[q].v < 1) return 0;
            double steps = (*tk)[q].v;
            q = css_omt_skip(tk, q + 1, t.m);
            vector<char> pos;
            if (q < t.m) {
                if ((*tk)[q].t != OT_COMMA) return 0;
                q = css_omt_skip(tk, q + 1, t.m);
                if (q >= t.m || (*tk)[q].t != OT_IDENT) return 0;
                css_omt w = (*tk)[q];
                vector<char> v;
                css_omt_text(p, &w, &v);
                css_omx_lower(data(v), len(v), &pos);
                if (!od_word_in("start end jump-start jump-end jump-none jump-both", data(pos), len(pos))) return 0;
                if (css_omt_skip(tk, q + 1, t.m) != t.m) return 0;
            }
            od_cat(out, "steps(");
            css_omx_num(steps, out);
            if (len(pos) && !od_is(&pos, "end") && !od_is(&pos, "jump-end")) { append_range(*out, ", ", 2); append_range(*out, data(pos), len(pos)); }
            push_back(*out, ')');
            continue;
        }
        if (css_omt_is(p, &t, "cubic-bezier") || css_omt_is(p, &t, "linear")) { od_generic(p, tk, i, e, out); continue; }
        return 0;
    }
    return 1;
}
/* a grid line (grid-row-start ...): auto, an integer, a name, span and either */
static int od_line(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[4], en[4];
    int n = od_parts(tk, a, b, st, en, 4);
    if (n <= 0 || n > 3) return 0;
    int span = 0, has_int = 0, has_name = 0;
    double iv = 0;
    vector<char> name;
    for (int k = 0; k < n; k++) {
        if (en[k] != st[k] + 1) return 0;
        css_omt t = (*tk)[st[k]];
        if (t.t == OT_IDENT) {
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (css_omx_ieq(data(v), len(v), "auto")) { if (n != 1) return 0; od_cat(out, "auto"); return 1; }
            if (css_omx_ieq(data(v), len(v), "span")) { if (span) return 0; span = 1; continue; }
            if (has_name || od_wide(data(v), len(v))) return 0;
            has_name = 1;
            append_range(name, data(v), len(v));
            continue;
        }
        if (t.t == OT_NUM && (t.f & OTF_INT)) { if (has_int || t.v == 0) return 0; has_int = 1; iv = t.v; continue; }
        return 0;
    }
    if (span && has_int && iv < 0) return 0;
    if (!has_int && !has_name) return 0;
    int k = 0;
    if (span) { od_cat(out, "span"); k++; }
    if (has_int) { if (k++) push_back(*out, ' '); css_omx_num(iv, out); }
    if (has_name) { if (k++) push_back(*out, ' '); css_omx_ident(data(name), len(name), out); }
    return 1;
}
/* idents a space or a comma apart (transition-property's a comma, container-name's a space): a
   keyword of the grammar lowered, a custom ident as written */
static int od_idents(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, const char *kw, vector<char> *out) {
    int n = 0;
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS) continue;
        if (t.t == OT_COMMA) { append_range(*out, ", ", 2); n = 0; continue; }
        if (t.t != OT_IDENT) return 0;
        vector<char> v;
        css_omt_text(p, &t, &v);
        if (n++) push_back(*out, ' ');
        if (od_word_in(kw, data(v), len(v)) || od_keyword(data(v), len(v))) css_omx_lower(data(v), len(v), out);
        else css_omx_ident(data(v), len(v), out);
    }
    return len(*out) > 0;
}
/* images: none, url(), the gradients and the other image functions, a comma apart */
static int od_images(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t i = css_omt_skip(tk, st[k], en[k]), e = en[k];
        while (e > i && (*tk)[e - 1].t == OT_WS) e--;
        if (i >= e) return 0;
        if (k) append_range(*out, ", ", 2);
        css_omt t = (*tk)[i];
        if (t.t == OT_IDENT && i + 1 == e && css_omt_is(p, &t, "none")) { od_cat(out, "none"); continue; }
        if (t.t == OT_URL && i + 1 == e) {
            vector<char> v;
            css_omt_text(p, &t, &v);
            od_cat(out, "url(");
            css_omx_string(data(v), len(v), out);
            push_back(*out, ')');
            continue;
        }
        if (t.t == OT_FUNC && t.m + 1 == e) {
            static const char *const fns = "url linear-gradient radial-gradient conic-gradient repeating-linear-gradient repeating-radial-gradient "
                                           "repeating-conic-gradient image-set -webkit-image-set cross-fade -webkit-cross-fade element paint "
                                           "-webkit-gradient -webkit-linear-gradient -webkit-radial-gradient -webkit-repeating-linear-gradient "
                                           "-webkit-repeating-radial-gradient light-dark";
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (!od_word_in(fns, data(v), len(v))) return 0;
            if (css_omt_is(p, &t, "url")) {
                uint32_t q = css_omt_skip(tk, i + 1, t.m);
                if (q >= t.m || (*tk)[q].t != OT_STR) return 0;
                css_omt s = (*tk)[q];
                vector<char> u;
                css_omt_text(p, &s, &u);
                od_cat(out, "url(");
                css_omx_string(data(u), len(u), out);
                push_back(*out, ')');
                continue;
            }
            od_generic(p, tk, i, e, out);
            continue;
        }
        return 0;
    }
    return 1;
}
/* background-repeat: each layer's one or two keywords, as Chromium writes them (repeat no-repeat
   repeat-x, and a pair of one keyword that keyword) */
static int od_repeat(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t ps[3], pe[3];
        int np = od_parts(tk, st[k], en[k], ps, pe, 3);
        if (np <= 0 || np > 2) return 0;
        char w[2][16];
        for (int q = 0; q < np; q++) {
            if (pe[q] != ps[q] + 1 || (*tk)[ps[q]].t != OT_IDENT) return 0;
            css_omt t = (*tk)[ps[q]];
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (len(v) >= sizeof w[0]) return 0;
            for (size_t i = 0; i < len(v); i++) w[q][i] = od_low(v[i]);
            w[q][len(v)] = 0;
        }
        if (k) append_range(*out, ", ", 2);
        if (np == 1) {
            if (!od_word_in("repeat no-repeat repeat-x repeat-y space round", w[0], strlen(w[0]))) return 0;
            od_cat(out, w[0]);
            continue;
        }
        for (int q = 0; q < 2; q++) if (!od_word_in("repeat no-repeat space round", w[q], strlen(w[q]))) return 0;
        if (!strcmp(w[0], w[1])) od_cat(out, w[0]);
        else if (!strcmp(w[0], "repeat") && !strcmp(w[1], "no-repeat")) od_cat(out, "repeat-x");
        else if (!strcmp(w[0], "no-repeat") && !strcmp(w[1], "repeat")) od_cat(out, "repeat-y");
        else { od_cat(out, w[0]); push_back(*out, ' '); od_cat(out, w[1]); }
    }
    return 1;
}
/* background-size: cover, contain, or one or two of a length, a percentage and auto */
static int od_bgsize(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t ps[3], pe[3];
        int np = od_parts(tk, st[k], en[k], ps, pe, 3);
        if (np <= 0 || np > 2) return 0;
        if (k) append_range(*out, ", ", 2);
        if (np == 1 && (*tk)[ps[0]].t == OT_IDENT && (css_omt_is(p, &(*tk)[ps[0]], "cover") || css_omt_is(p, &(*tk)[ps[0]], "contain"))) {
            css_omx_tokens(p, tk, ps[0], pe[0], 1, out);
            continue;
        }
        for (int q = 0; q < np; q++) {
            if (q) push_back(*out, ' ');
            if (!od_one(p, tk, ps[q], pe[q], OD_K_LEN | OD_K_PCT, 1, "auto", out)) return 0;
        }
    }
    return 1;
}
/* a radius: one or two lengths or percentages (two the same one) */
static int od_radius(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t ps[3], pe[3];
    int np = od_parts(tk, a, b, ps, pe, 3);
    if (np <= 0 || np > 2) return 0;
    vector<char> v0, v1;
    if (!od_numeric(p, tk, ps[0], pe[0], OD_K_LEN | OD_K_PCT, 1, &v0)) return 0;
    if (np == 2 && !od_numeric(p, tk, ps[1], pe[1], OD_K_LEN | OD_K_PCT, 1, &v1)) return 0;
    append_range(*out, data(v0), len(v0));
    if (np == 2 && !css_omx_eq(data(v0), len(v0), data(v1), len(v1))) { push_back(*out, ' '); append_range(*out, data(v1), len(v1)); }
    return 1;
}

int css_omv_value(uint32_t prop, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return 0;
    /* a CSS-wide keyword alone */
    if (a + 1 == b && (*tk)[a].t == OT_IDENT) {
        css_omt t = (*tk)[a];
        vector<char> v;
        css_omt_text(p, &t, &v);
        if (od_wide(data(v), len(v))) { css_omx_lower(data(v), len(v), out); return 1; }
    }
    if (od_has_var(p, tk, a, b)) { css_omx_raw(p, tk, a, b, 1, out); return 1; }
    int g = od_gram_of(prop);
    if (g < 0) { od_generic(p, tk, a, b, out); return 1; }
    od_gram gr = od_grams[g];
    switch (gr.g) {
    case OD_G_ONE: return od_one(p, tk, a, b, gr.kinds, gr.nonneg, gr.kw, out);
    case OD_G_LIST: return od_list(p, tk, a, b, gr.kinds, gr.nonneg, gr.kw, out);
    case OD_G_COLOR: {
        if (gr.kw && (*tk)[a].t == OT_IDENT && a + 1 == b && od_word_in(gr.kw, p + (*tk)[a].s, (*tk)[a].e - (*tk)[a].s)) {
            css_omx_lower(p + (*tk)[a].s, (*tk)[a].e - (*tk)[a].s, out);
            return 1;
        }
        return od_color(p, tk, a, b, out);
    }
    case OD_G_FAMILY: return od_family(p, tk, a, b, out);
    case OD_G_DISPLAY: return od_display(p, tk, a, b, out);
    case OD_G_OPACITY: {
        css_omt t = (*tk)[a];
        if (a + 1 == b && t.t == OT_PCT) { css_omx_num(t.v / 100, out); return 1; }
        return od_numeric(p, tk, a, b, OD_K_NUM, 0, out);
    }
    case OD_G_WEIGHT: {
        css_omt t = (*tk)[a];
        if (a + 1 == b && t.t == OT_NUM) {
            if (t.v < 1 || t.v > 1000) return 0;
            css_omx_num(t.v, out);
            return 1;
        }
        return od_one(p, tk, a, b, 0, 0, "normal bold bolder lighter", out);
    }
    case OD_G_STYLE_FONT: {
        uint32_t ps[3], pe[3];
        int np = od_parts(tk, a, b, ps, pe, 3);
        if (np == 1) return od_one(p, tk, a, b, 0, 0, "normal italic oblique", out);
        if (np != 2 || !((*tk)[ps[0]].t == OT_IDENT && css_omt_is(p, &(*tk)[ps[0]], "oblique"))) return 0;
        od_cat(out, "oblique ");
        return od_numeric(p, tk, ps[1], pe[1], OD_K_ANGLE, 0, out);
    }
    case OD_G_SHADOW: return od_shadow(p, tk, a, b, !strcmp(gr.name, "box-shadow"), out);
    case OD_G_RATIO: {
        /* auto || <number> [/ <number>]: one number n is n / 1 */
        uint32_t ps[6], pe[6];
        int np = od_parts(tk, a, b, ps, pe, 6);
        int has_auto = 0;
        vector<char> r;
        int k = 0;
        while (k < np) {
            css_omt t = (*tk)[ps[k]];
            if (t.t == OT_IDENT && css_omt_is(p, &t, "auto")) { if (has_auto) return 0; has_auto = 1; k++; continue; }
            if (len(r)) return 0;
            vector<char> x, y;
            if (!od_numeric(p, tk, ps[k], pe[k], OD_K_NUM, 1, &x)) return 0;
            k++;
            if (k < np && (*tk)[ps[k]].t == OT_DELIM && p[(*tk)[ps[k]].s] == '/') {
                if (k + 1 >= np || !od_numeric(p, tk, ps[k + 1], pe[k + 1], OD_K_NUM, 1, &y)) return 0;
                k += 2;
            } else od_cat(&y, "1");
            append_range(r, data(x), len(x));
            append_range(r, " / ", 3);
            append_range(r, data(y), len(y));
        }
        if (has_auto) { od_cat(out, "auto"); if (len(r)) push_back(*out, ' '); }
        append_range(*out, data(r), len(r));
        return has_auto || len(r);
    }
    case OD_G_RADIUS: return od_radius(p, tk, a, b, out);
    case OD_G_REPEAT: return od_repeat(p, tk, a, b, out);
    case OD_G_EASING: return od_easing(p, tk, a, b, out);
    case OD_G_LINE: return od_line(p, tk, a, b, out);
    case OD_G_CUSTOM_IDENTS: return od_idents(p, tk, a, b, gr.kw, out);
    case OD_G_IMAGES: return od_images(p, tk, a, b, out);
    case OD_G_SIZE_BG: return od_bgsize(p, tk, a, b, out);
    case OD_G_ANIM_NAME: {
        uint32_t st[32], en[32];
        int n = od_items(tk, a, b, st, en, 32);
        if (n <= 0) return 0;
        for (int k = 0; k < n; k++) {
            uint32_t i = css_omt_skip(tk, st[k], en[k]), e = en[k];
            while (e > i && (*tk)[e - 1].t == OT_WS) e--;
            if (i + 1 != e) return 0;
            if (k) append_range(*out, ", ", 2);
            css_omt t = (*tk)[i];
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (t.t == OT_STR) css_omx_string(data(v), len(v), out);
            else if (t.t == OT_IDENT) {
                if (css_omx_ieq(data(v), len(v), "none")) od_cat(out, "none");
                else css_omx_ident(data(v), len(v), out);
            } else return 0;
        }
        return 1;
    }
    case OD_G_DECO_LINE: {
        uint32_t ps[5], pe[5];
        int np = od_parts(tk, a, b, ps, pe, 5);
        if (np <= 0) return 0;
        if (np == 1 && (*tk)[ps[0]].t == OT_IDENT && (css_omt_is(p, &(*tk)[ps[0]], "none") || css_omt_is(p, &(*tk)[ps[0]], "spelling-error") ||
                                                     css_omt_is(p, &(*tk)[ps[0]], "grammar-error"))) {
            css_omx_tokens(p, tk, ps[0], pe[0], 1, out);
            return 1;
        }
        int seen[4] = { 0, 0, 0, 0 };
        static const char *const lines[] = { "underline", "overline", "line-through", "blink" };
        for (int q = 0; q < np; q++) {
            if (pe[q] != ps[q] + 1 || (*tk)[ps[q]].t != OT_IDENT) return 0;
            int found = -1;
            for (int z = 0; z < 4; z++) if (css_omt_is(p, &(*tk)[ps[q]], lines[z])) found = z;
            if (found < 0 || seen[found]) return 0;
            seen[found] = 1;
        }
        int k = 0;
        for (int z = 0; z < 4; z++) if (seen[z]) { if (k++) push_back(*out, ' '); od_cat(out, lines[z]); }
        return 1;
    }
    default: od_generic(p, tk, a, b, out); return 1;
    }
}

/* ---- descriptors (the at-rules' own) ---- */

static const char *const od_desc_font_face[] = { "font-family", "src", "unicode-range", "font-weight", "font-style", "font-stretch", "font-display",
    "font-feature-settings", "font-variation-settings", "ascent-override", "descent-override", "line-gap-override", "size-adjust",
    "font-named-instance", 0 };
static const char *const od_desc_property[] = { "syntax", "inherits", "initial-value", 0 };
static const char *const od_desc_counter[] = { "system", "symbols", "additive-symbols", "negative", "prefix", "suffix", "pad", "range",
    "fallback", "speak-as", 0 };
static const char *const od_desc_palette[] = { "font-family", "base-palette", "override-colors", 0 };
static const char *const od_desc_view[] = { "navigation", "types", 0 };
static const char *const od_desc_page[] = { "size", "page-orientation", "marks", "bleed", 0 };
/* the descriptors' names after the properties' numbers, block kind by block kind */
enum { OD_D_FONT_FACE = 0, OD_D_PROPERTY = 20, OD_D_COUNTER = 30, OD_D_PALETTE = 45, OD_D_VIEW = 50, OD_D_PAGE = 55, OD_D_END = 60 };
static const char *const *od_desc_list(uint32_t kind, uint32_t *base) {
    switch (kind) {
    case CSS_OM_B_FONT_FACE: *base = OD_D_FONT_FACE; return od_desc_font_face;
    case CSS_OM_B_PROPERTY: *base = OD_D_PROPERTY; return od_desc_property;
    case CSS_OM_B_COUNTER: *base = OD_D_COUNTER; return od_desc_counter;
    case CSS_OM_B_PALETTE: *base = OD_D_PALETTE; return od_desc_palette;
    case CSS_OM_B_VIEW: *base = OD_D_VIEW; return od_desc_view;
    case CSS_OM_B_PAGE: *base = OD_D_PAGE; return od_desc_page;
    default: *base = 0; return 0;
    }
}
int css_om_descriptor(uint32_t kind, const char *name, size_t n) {
    uint32_t base;
    const char *const *list = od_desc_list(kind, &base);
    if (!list) return -1;
    for (int k = 0; list[k]; k++) if (css_omx_ieq(name, n, list[k])) return (int)(CSS_OM_NPROPS + base + (uint32_t)k);
    return -1;
}
/* a descriptor's name by its number */
static const char *od_desc_name(uint32_t p) {
    if (p < CSS_OM_NPROPS || p >= CSS_OM_NPROPS + OD_D_END) return "";
    uint32_t d = p - CSS_OM_NPROPS;
    static const uint32_t kinds[] = { CSS_OM_B_FONT_FACE, CSS_OM_B_PROPERTY, CSS_OM_B_COUNTER, CSS_OM_B_PALETTE, CSS_OM_B_VIEW, CSS_OM_B_PAGE };
    for (int k = 5; k >= 0; k--) {
        uint32_t base;
        const char *const *list = od_desc_list(kinds[k], &base);
        if (d < base) continue;
        for (int q = 0; list[q]; q++) if (base + (uint32_t)q == d) return list[q];
        return "";
    }
    return "";
}
const char *css_om_desc_name(uint32_t p) { return od_desc_name(p); }
static int od_hexdig(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* @font-face's src: its sources a comma apart, local() and format() of an ident given a string,
   as Chromium writes them */
static int od_src(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t st[32], en[32];
    int n = od_items(tk, a, b, st, en, 32);
    if (n <= 0) return 0;
    for (int k = 0; k < n; k++) {
        uint32_t ps[6], pe[6];
        int np = od_parts(tk, st[k], en[k], ps, pe, 6);
        if (np <= 0) return 0;
        if (k) append_range(*out, ", ", 2);
        for (int q = 0; q < np; q++) {
            css_omt t = (*tk)[ps[q]];
            if (q) push_back(*out, ' ');
            if (t.t == OT_URL) {
                vector<char> v;
                css_omt_text(p, &t, &v);
                od_cat(out, "url(");
                css_omx_string(data(v), len(v), out);
                push_back(*out, ')');
                continue;
            }
            if (t.t == OT_FUNC && (css_omt_is(p, &t, "local") || css_omt_is(p, &t, "format") || css_omt_is(p, &t, "tech") || css_omt_is(p, &t, "url"))) {
                vector<char> fn, lf;
                css_omt_text(p, &t, &fn);
                css_omx_lower(data(fn), len(fn), &lf);
                append_range(*out, data(lf), len(lf));
                push_back(*out, '(');
                if (css_omt_is(p, &t, "tech")) css_omx_tokens(p, tk, ps[q] + 1, t.m, 1, out);
                else {
                    /* idents (a family's words, a format's name) a string, a string as it is */
                    vector<char> words;
                    int nw = 0, str = 0;
                    for (uint32_t i = ps[q] + 1; i < t.m; i++) {
                        css_omt w = (*tk)[i];
                        if (w.t == OT_WS) continue;
                        vector<char> v;
                        css_omt_text(p, &w, &v);
                        if (w.t == OT_STR) str = 1;
                        else if (w.t != OT_IDENT) return 0;
                        if (nw++) push_back(words, ' ');
                        append_range(words, data(v), len(v));
                    }
                    if (!nw || (str && nw > 1)) return 0;
                    css_omx_string(data(words), len(words), out);
                }
                push_back(*out, ')');
                continue;
            }
            return 0;
        }
    }
    return 1;
}
/* a unicode-range's ranges as Chromium writes them (U+ and hex in upper case, no leading zero) */
static int od_unicode_range(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    /* read from the text itself: the tokenizer splits U+0000-00FF oddly */
    a = css_omt_skip(tk, a, b);
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return 0;
    size_t s = (*tk)[a].s, e = (*tk)[b - 1].e;
    size_t i = s;
    int n = 0;
    while (i < e) {
        while (i < e && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        if (i + 1 >= e || (p[i] != 'u' && p[i] != 'U') || p[i + 1] != '+') return 0;
        i += 2;
        unsigned long lo = 0, hi = 0;
        int digits = 0, q = 0;
        while (i < e && digits < 6 && (od_hexdig(p[i]) || p[i] == '?')) {
            int v = p[i] == '?' ? 0 : (p[i] >= '0' && p[i] <= '9') ? p[i] - '0' : (p[i] | 32) - 'a' + 10;
            int vh = p[i] == '?' ? 15 : v;
            if (p[i] == '?') q = 1;
            else if (q) return 0;
            lo = lo * 16 + (unsigned long)v;
            hi = hi * 16 + (unsigned long)vh;
            digits++;
            i++;
        }
        if (!digits) return 0;
        if (!q && i < e && p[i] == '-') {
            i++;
            hi = 0;
            int d2 = 0;
            while (i < e && d2 < 6 && od_hexdig(p[i])) { hi = hi * 16 + (unsigned long)((p[i] >= '0' && p[i] <= '9') ? p[i] - '0' : (p[i] | 32) - 'a' + 10); d2++; i++; }
            if (!d2) return 0;
        }
        char buf[40];
        int k = lo == hi ? snprintf(buf, sizeof buf, "U+%lX", lo) : snprintf(buf, sizeof buf, "U+%lX-%lX", lo, hi);
        if (n++) append_range(*out, ", ", 2);
        append_range(*out, buf, (size_t)k);
        while (i < e && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        if (i < e) { if (p[i] != ',') return 0; i++; }
    }
    return n > 0;
}
/* a descriptor's value: 1 and its text */
static int od_desc_value(uint32_t kind, uint32_t prop, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    char dn[64];
    od_copyz(dn, sizeof dn, od_desc_name(prop));
    if (!strcmp(dn, "font-family") && kind == CSS_OM_B_FONT_FACE) {
        /* one family: a string or idents */
        uint32_t st[2], en[2];
        if (od_items(tk, a, b, st, en, 2) != 1) return 0;
        return od_family(p, tk, a, b, out);
    }
    if (!strcmp(dn, "unicode-range")) return od_unicode_range(p, tk, a, b, out);
    if (!strcmp(dn, "syntax")) {
        uint32_t i = css_omt_skip(tk, a, b);
        if (i >= b || (*tk)[i].t != OT_STR || css_omt_skip(tk, i + 1, b) != b) return 0;
        css_omt t = (*tk)[i];
        vector<char> v;
        css_omt_text(p, &t, &v);
        css_omx_string(data(v), len(v), out);
        return 1;
    }
    if (!strcmp(dn, "inherits")) return od_one(p, tk, a, b, 0, 0, "true false", out);
    if (!strcmp(dn, "initial-value")) { css_omx_raw(p, tk, a, b, 1, out); return 1; }
    if (kind == CSS_OM_B_COUNTER && (!strcmp(dn, "symbols") || !strcmp(dn, "prefix") || !strcmp(dn, "suffix") || !strcmp(dn, "negative") ||
                                     !strcmp(dn, "additive-symbols") || !strcmp(dn, "pad") || !strcmp(dn, "fallback") || !strcmp(dn, "system"))) {
        css_omx_tokens(p, tk, a, b, 0, out);
        /* keywords of the system lowered */
        return 1;
    }
    if (!strcmp(dn, "src")) return od_src(p, tk, a, b, out);
    od_generic(p, tk, a, b, out);
    return 1;
}

/* ---- declaration lists ---- */

/* a declaration's longhand values made, before they are filed in a list: a longhand, its flags,
   its text in the made list's own (off, n); pending: the shorthand it waits for */
typedef struct od_lv { uint32_t prop, flags, sh, off, n, spare; } od_lv;
static void od_push(vector<od_lv> *ls, vector<char> *txt, uint32_t prop, uint32_t flags, uint32_t sh, const char *v, size_t n) {
    od_lv x;
    memset(&x, 0, sizeof x);
    x.prop = prop;
    x.flags = flags;
    x.sh = sh;
    x.off = (uint32_t)len(*txt);
    x.n = (uint32_t)n;
    append_range(*txt, v, n);
    push_back(*ls, x);
}
static void od_pushz(vector<od_lv> *ls, vector<char> *txt, const char *name, uint32_t flags, const char *v) {
    od_push(ls, txt, od_prop(name), flags, 0, v, strlen(v));
}
static void od_pushv(vector<od_lv> *ls, vector<char> *txt, const char *name, uint32_t flags, const vector<char> *v) {
    od_push(ls, txt, od_prop(name), flags, 0, data(*v), len(*v));
}

static int od_expand(uint32_t sh, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<od_lv> *ls, vector<char> *txt);

/* a declaration [a, b) (its name first) of a block of kind: its longhands appended to ls; 0 when it
   is no declaration (an unknown name, a value the property does not take) */
static int od_decl(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t kind, vector<od_lv> *ls, vector<char> *txt,
                   vector<char> *cname) {
    uint32_t i = css_omt_skip(tk, a, b);
    if (i >= b || (*tk)[i].t != OT_IDENT) return 0;
    css_omt nt = (*tk)[i];
    vector<char> name;
    css_omt_text(p, &nt, &name);
    uint32_t c = css_omt_skip(tk, i + 1, b);
    if (c >= b || (*tk)[c].t != OT_COLON) return 0;
    uint32_t va = c + 1, vb = b;
    while (vb > va && (*tk)[vb - 1].t == OT_WS) vb--;
    /* !important at the value's end */
    uint32_t flags = 0;
    if (vb > va && (*tk)[vb - 1].t == OT_IDENT && css_omt_is(p, &(*tk)[vb - 1], "important")) {
        uint32_t k = vb - 1;
        while (k > va && (*tk)[k - 1].t == OT_WS) k--;
        if (k > va && (*tk)[k - 1].t == OT_DELIM && p[(*tk)[k - 1].s] == '!') {
            flags |= CSS_OM_IMPORTANT;
            vb = k - 1;
            while (vb > va && (*tk)[vb - 1].t == OT_WS) vb--;
        }
    }
    /* a stray ! is no value */
    for (uint32_t k = va; k < vb; k++) if ((*tk)[k].t == OT_DELIM && p[(*tk)[k].s] == '!') return 0;
    if (kind == CSS_OM_B_KEYFRAME && (flags & CSS_OM_IMPORTANT)) return 0;
    if (len(name) >= 2 && name[0] == '-' && name[1] == '-') {
        if (kind != CSS_OM_B_STYLE && kind != CSS_OM_B_KEYFRAME && kind != CSS_OM_B_PAGE && kind != CSS_OM_B_TRY) return 0;
        /* a custom property: its value as written, trimmed (an empty one is one) */
        vector<char> v;
        uint32_t k = va;
        while (k < vb && (*tk)[k].t == OT_WS) k++;
        css_omx_raw(p, tk, k, vb, 1, &v);
        clear(*cname);
        append_range(*cname, data(name), len(name));
        od_push(ls, txt, CSS_OM_CUSTOM, flags, 0, data(v), len(v));
        return 1;
    }
    if (css_omt_skip(tk, va, vb) >= vb) return 0;
    if (kind != CSS_OM_B_STYLE && kind != CSS_OM_B_KEYFRAME && kind != CSS_OM_B_TRY) {
        int d = css_om_descriptor(kind, data(name), len(name));
        if (d >= 0) {
            vector<char> v;
            if (!od_desc_value(kind, (uint32_t)d, p, tk, va, vb, &v)) return 0;
            od_push(ls, txt, (uint32_t)d, flags, 0, data(v), len(v));
            return 1;
        }
        if (kind != CSS_OM_B_PAGE) return 0;
    }
    int pr = css_om_prop(data(name), len(name));
    if (pr < 0 || pr == CSS_OM_CUSTOM) return 0;
    uint32_t prop = css_om_unalias((uint32_t)pr);
    if (kind == CSS_OM_B_KEYFRAME) {
        /* animation's own are not animatable */
        char pn[64];
        od_copyz(pn, sizeof pn, css_om_prop_name(prop));
        if (!strncmp(pn, "animation", 9)) return 0;
    }
    if (css_om_is_shorthand(prop)) {
        size_t n0 = len(*ls);
        uint32_t nl = css_om_longhands(prop);
        /* a CSS-wide keyword: each longhand it, in the shorthand's order */
        uint32_t k0 = css_omt_skip(tk, va, vb);
        if (k0 + 1 == vb && (*tk)[k0].t == OT_IDENT) {
            css_omt t = (*tk)[k0];
            vector<char> v, lv;
            css_omt_text(p, &t, &v);
            if (od_wide(data(v), len(v))) {
                css_omx_lower(data(v), len(v), &lv);
                for (uint32_t q = 0; q < nl; q++) od_push(ls, txt, css_om_longhand(prop, q), flags, 0, data(lv), len(lv));
                return 1;
            }
        }
        if (od_has_var(p, tk, va, vb)) {
            vector<char> v;
            css_omx_raw(p, tk, va, vb, 1, &v);
            for (uint32_t q = 0; q < nl; q++) od_push(ls, txt, css_om_longhand(prop, q), flags | CSS_OM_PENDING, prop, data(v), len(v));
            return 1;
        }
        int r = od_expand(prop, p, tk, va, vb, ls, txt);
        if (r < 0) {
            /* a shorthand with no reader here: its value whole, its longhands pending on it */
            vector<char> v;
            od_generic(p, tk, va, vb, &v);
            for (uint32_t q = 0; q < nl; q++) od_push(ls, txt, css_om_longhand(prop, q), flags | CSS_OM_PENDING, prop, data(v), len(v));
            return 1;
        }
        if (!r) { resize(*ls, n0); return 0; }
        for (size_t q = n0; q < len(*ls); q++) (*ls)[q].flags |= flags;
        return 1;
    }
    vector<char> v;
    if (!css_omv_value(prop, p, tk, va, vb, &v)) return 0;
    od_push(ls, txt, prop, flags, 0, data(v), len(v));
    return 1;
}

/* the declarations made filed into dl as Chromium's parser files a block's: for each property its
   last, the important ones after the others (a custom property by its name) */
static void od_file(css_om_dl *dl, const vector<od_lv> *ls, const vector<char> *txt, const vector<uint32_t> *names) {
    size_t n = len(*ls);
    vector<uint8_t> keep;
    resize(keep, n, (uint8_t)0);
    vector<uint8_t> seen;
    resize(seen, (size_t)CSS_OM_NPROPS + OD_D_END + 1, (uint8_t)0);
    for (int pass = 0; pass < 2; pass++) {
        int important = pass == 0;
        for (size_t i = n; i-- > 0;) {
            od_lv x = (*ls)[i];
            if (((x.flags & CSS_OM_IMPORTANT) != 0) != important) continue;
            if (x.prop == CSS_OM_CUSTOM) {
                /* a custom property by its name */
                uint32_t no = (*names)[2 * i], nn = (*names)[2 * i + 1];
                int dup = 0;
                for (size_t j = i + 1; j < n && !dup; j++) {
                    if (!keep[j] || (*ls)[j].prop != CSS_OM_CUSTOM) continue;
                    if ((*names)[2 * j + 1] == nn && memcmp(data(*txt) + (*names)[2 * j], data(*txt) + no, nn) == 0) dup = 1;
                }
                if (!dup) keep[i] = 1;
                continue;
            }
            if (x.prop >= len(seen) || seen[x.prop]) continue;
            seen[x.prop] = 1;
            keep[i] = 1;
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < n; i++) {
            od_lv x = (*ls)[i];
            if (!keep[i] || ((x.flags & CSS_OM_IMPORTANT) != 0) != (pass == 1)) continue;
            css_om_decl d;
            memset(&d, 0, sizeof d);
            d.prop = x.prop;
            d.flags = x.flags;
            d.sh = x.sh;
            d.voff = (uint32_t)len(dl->t);
            d.vlen = x.n;
            append_range(dl->t, data(*txt) + x.off, x.n);
            if (x.prop == CSS_OM_CUSTOM) {
                d.noff = (uint32_t)len(dl->t);
                d.nlen = (*names)[2 * i + 1];
                append_range(dl->t, data(*txt) + (*names)[2 * i], (*names)[2 * i + 1]);
            }
            push_back(dl->d, d);
        }
    }
}

void css_om_decls_tokens(css_om_dl *dl, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t kind) {
    vector<od_lv> ls;
    vector<char> txt;
    vector<uint32_t> names;   /* each made value's custom name (off, n in txt), 0 0 for none */
    uint32_t i = a;
    while (i < b) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_SEMI) {
            css_omt q = (*tk)[e];
            if ((q.t == OT_FUNC || q.t == OT_LPAREN || q.t == OT_LBRACK || q.t == OT_LBRACE) && q.m < b) e = q.m;
            e++;
        }
        size_t n0 = len(ls);
        vector<char> cname;
        od_decl(p, tk, i, e, kind, &ls, &txt, &cname);
        for (size_t k = n0; k < len(ls); k++) {
            if (ls[k].prop == CSS_OM_CUSTOM) {
                push_back(names, (uint32_t)len(txt));
                push_back(names, (uint32_t)len(cname));
                append_range(txt, data(cname), len(cname));
            } else { push_back(names, 0u); push_back(names, 0u); }
        }
        i = e + 1;
    }
    clear(dl->d);
    clear(dl->t);
    od_file(dl, &ls, &txt, &names);
}
void css_om_parse_decls(css_om_dl *dl, const char *t, size_t n, uint32_t kind) {
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    css_om_decls_tokens(dl, t, &tk, 0, (uint32_t)len(tk) - 1, kind);
}

/* ---- blocks in the model ---- */

void css_om_block_load(const css_om *m, uint32_t b, css_om_dl *dl) {
    clear(dl->d);
    clear(dl->t);
    if (!b || b >= len(m->blocks)) return;
    css_om_block bl = m->blocks[b];
    size_t tn = 0;
    for (uint32_t k = 0; k < bl.n; k++) tn += m->decls[bl.first + k].vlen + m->decls[bl.first + k].nlen;
    reserve(dl->d, bl.n + 2);
    reserve(dl->t, tn + 64);
    for (uint32_t k = 0; k < bl.n; k++) {
        css_om_decl d = m->decls[bl.first + k];
        uint32_t vo = (uint32_t)len(dl->t);
        css_om_span(m, d.voff, d.vlen, &dl->t);
        uint32_t no = (uint32_t)len(dl->t);
        css_om_span(m, d.noff, d.nlen, &dl->t);
        d.voff = vo;
        d.noff = no;
        push_back(dl->d, d);
    }
}
void css_om_block_store(css_om *m, uint32_t b, const css_om_dl *dl) {
    if (!b || b >= len(m->blocks)) return;
    uint32_t first = (uint32_t)len(m->decls);
    for (size_t k = 0; k < len(dl->d); k++) {
        css_om_decl d = dl->d[k];
        d.voff = css_om_put(m, data(dl->t) + d.voff, d.vlen);
        d.noff = d.nlen ? css_om_put(m, data(dl->t) + d.noff, d.nlen) : 0;
        push_back(m->decls, d);
    }
    m->blocks[b].first = first;
    m->blocks[b].n = (uint32_t)len(dl->d);
    m->rev++;
}
uint32_t css_om_block_new(css_om *m, uint32_t kind, uint32_t rule, const css_om_dl *dl) {
    if (!len(m->blocks)) {
        css_om_block z;
        memset(&z, 0, sizeof z);
        push_back(m->blocks, z);
    }
    css_om_block bl;
    memset(&bl, 0, sizeof bl);
    bl.kind = kind;
    bl.rule = rule;
    push_back(m->blocks, bl);
    uint32_t b = (uint32_t)len(m->blocks) - 1;
    css_om_block_store(m, b, dl);
    return b;
}

/* ---- the list's own operations ---- */

/* the declaration of a property in dl (its place plus one, 0 none); a custom one by its name */
static uint32_t od_find(const css_om_dl *dl, uint32_t prop, const char *name, size_t n) {
    for (size_t k = 0; k < len(dl->d); k++) {
        css_om_decl d = dl->d[k];
        if (d.prop != prop) continue;
        if (prop == CSS_OM_CUSTOM && !css_omx_eq(data(dl->t) + d.noff, d.nlen, name, n)) continue;
        return (uint32_t)k + 1;
    }
    return 0;
}
/* a name's property in a block of kind: a longhand's, a shorthand's, a descriptor's; -1 none */
static int od_name_prop(uint32_t kind, const char *name, size_t n) {
    if (n >= 2 && name[0] == '-' && name[1] == '-') return CSS_OM_CUSTOM;
    if (kind != CSS_OM_B_STYLE && kind != CSS_OM_B_KEYFRAME && kind != CSS_OM_B_TRY) {
        int d = css_om_descriptor(kind, name, n);
        if (d >= 0) return d;
        if (kind != CSS_OM_B_PAGE) return -1;
    }
    int p = css_om_prop(name, n);
    if (p < 0) return -1;
    return (int)css_om_unalias((uint32_t)p);
}

static int od_shorthand_text(const css_om_dl *dl, uint32_t sh, vector<char> *out);

void css_om_get(const css_om_dl *dl, uint32_t kind, const char *name, size_t n, vector<char> *out) {
    int p = od_name_prop(kind, name, n);
    if (p < 0) return;
    if (css_om_is_shorthand((uint32_t)p)) { od_shorthand_text(dl, (uint32_t)p, out); return; }
    uint32_t k = od_find(dl, (uint32_t)p, name, n);
    if (!k) return;
    css_om_decl d = dl->d[k - 1];
    if (d.flags & CSS_OM_PENDING) return;
    append_range(*out, data(dl->t) + d.voff, d.vlen);
}
int css_om_important(const css_om_dl *dl, uint32_t kind, const char *name, size_t n) {
    int p = od_name_prop(kind, name, n);
    if (p < 0) return 0;
    if (css_om_is_shorthand((uint32_t)p)) {
        uint32_t nl = css_om_longhands((uint32_t)p);
        if (!nl) return 0;
        for (uint32_t q = 0; q < nl; q++) {
            uint32_t k = od_find(dl, css_om_longhand((uint32_t)p, q), "", 0);
            if (!k || !(dl->d[k - 1].flags & CSS_OM_IMPORTANT)) return 0;
        }
        return 1;
    }
    uint32_t k = od_find(dl, (uint32_t)p, name, n);
    return k && (dl->d[k - 1].flags & CSS_OM_IMPORTANT) != 0;
}
/* a declaration removed from dl */
static void od_remove_at(css_om_dl *dl, uint32_t k) {
    size_t n = len(dl->d);
    for (size_t q = k; q + 1 < n; q++) dl->d[q] = dl->d[q + 1];
    resize(dl->d, n - 1);
}
int css_om_unset(css_om_dl *dl, uint32_t kind, const char *name, size_t n, vector<char> *old) {
    clear(*old);
    int p = od_name_prop(kind, name, n);
    if (p < 0) return 0;
    if (css_om_is_shorthand((uint32_t)p)) {
        int any = 0;
        uint32_t nl = css_om_longhands((uint32_t)p);
        for (uint32_t q = 0; q < nl; q++) {
            uint32_t k = od_find(dl, css_om_longhand((uint32_t)p, q), "", 0);
            if (k) { od_remove_at(dl, k - 1); any = 1; }
        }
        return any;
    }
    uint32_t k = od_find(dl, (uint32_t)p, name, n);
    if (!k) return 0;
    css_om_decl d = dl->d[k - 1];
    if (!(d.flags & CSS_OM_PENDING)) append_range(*old, data(dl->t) + d.voff, d.vlen);
    od_remove_at(dl, k - 1);
    return 1;
}
int css_om_set(css_om_dl *dl, uint32_t kind, const char *name, size_t n, const char *v, size_t vn, int important) {
    int p = od_name_prop(kind, name, n);
    if (p < 0) return 0;
    /* an empty value removes */
    size_t z = 0;
    while (z < vn && (v[z] == ' ' || v[z] == '\t' || v[z] == '\n' || v[z] == '\r' || v[z] == '\f')) z++;
    if (z >= vn) {
        vector<char> old;
        return css_om_unset(dl, kind, name, n, &old);
    }
    /* the declaration's text read as a block's one declaration */
    vector<char> text;
    reserve(text, n + vn + 2);
    append_range(text, name, n);
    push_back(text, ':');
    append_range(text, v, vn);
    vector<css_omt> tk;
    css_omt_tokens(data(text), len(text), &tk);
    uint32_t b = (uint32_t)len(tk) - 1;
    /* a ; or a !important in the value makes it no value (Chromium's) */
    for (uint32_t k = 0; k < b; k++) {
        if (tk[k].t == OT_SEMI) return 0;
        if (tk[k].t == OT_DELIM && data(text)[tk[k].s] == '!') return 0;
    }
    vector<od_lv> ls;
    vector<char> txt, cname;
    if (!od_decl(data(text), &tk, 0, b, kind, &ls, &txt, &cname)) return 0;
    for (size_t q = 0; q < len(ls); q++) {
        od_lv x = ls[q];
        css_om_decl d;
        memset(&d, 0, sizeof d);
        d.prop = x.prop;
        d.flags = (x.flags & ~(uint32_t)CSS_OM_IMPORTANT) | (important ? CSS_OM_IMPORTANT : 0);
        d.sh = x.sh;
        d.voff = (uint32_t)len(dl->t);
        d.vlen = x.n;
        append_range(dl->t, data(txt) + x.off, x.n);
        if (x.prop == CSS_OM_CUSTOM) {
            d.noff = (uint32_t)len(dl->t);
            d.nlen = (uint32_t)len(cname);
            append_range(dl->t, data(cname), len(cname));
        }
        uint32_t k = od_find(dl, x.prop, data(cname), len(cname));
        if (k) dl->d[k - 1] = d;
        else push_back(dl->d, d);
    }
    return 1;
}
uint32_t css_om_decl_count(const css_om_dl *dl) { return (uint32_t)len(dl->d); }
void css_om_decl_name(const css_om_dl *dl, uint32_t i, vector<char> *out) {
    if (i >= len(dl->d)) return;
    css_om_decl d = dl->d[i];
    if (d.prop == CSS_OM_CUSTOM) { append_range(*out, data(dl->t) + d.noff, d.nlen); return; }
    if (d.prop >= CSS_OM_NPROPS) od_cat(out, od_desc_name(d.prop));
    else od_cat(out, css_om_prop_name(d.prop));
}

/* ---- shorthands: reading ---- */

/* a longhand's value [a, b) by its own grammar, into v */
static int od_lh(const char *name, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *v) {
    clear(*v);
    return css_omv_value(od_prop(name), p, tk, a, b, v);
}
/* a longhand's value read into the text set's item i: 1 when the property takes it (a CSS-wide
   keyword not) */
static int od_lh_ts(const char *name, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, od_ts *s, int i) {
    vector<char> v;
    if (!od_lh(name, p, tk, a, b, &v) || od_wide(data(v), len(v))) return 0;
    od_ts_setv(s, i, &v);
    return 1;
}
static void od_push_ts(vector<od_lv> *ls, vector<char> *txt, const char *name, uint32_t flags, const od_ts *s, int i) {
    od_push(ls, txt, od_prop(name), flags, 0, data(s->pool) + s->off[i], s->ln[i]);
}
/* the four sides from one to four values (top right bottom left) */
static int od_four(const char *const *names, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<od_lv> *ls, vector<char> *txt) {
    uint32_t st[5], en[5];
    int n = od_parts(tk, a, b, st, en, 5);
    if (n <= 0 || n > 4) return 0;
    od_ts s;
    memset(&s, 0, sizeof s);
    defer(s) { vector_char_drop(&s.pool); }
    for (int k = 0; k < n; k++) if (!od_lh_ts(names[0], p, tk, st[k], en[k], &s, k)) return 0;
    static const int side[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
    for (int q = 0; q < 4; q++) od_push_ts(ls, txt, names[q], 0, &s, side[n - 1][q]);
    return 1;
}
/* two values (the second the first's when missing) */
static int od_two(const char *n0, const char *n1, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<od_lv> *ls, vector<char> *txt) {
    uint32_t st[3], en[3];
    int n = od_parts(tk, a, b, st, en, 3);
    if (n <= 0 || n > 2) return 0;
    vector<char> v0, v1;
    if (!od_lh(n0, p, tk, st[0], en[0], &v0) || od_wide(data(v0), len(v0))) return 0;
    if (n == 2) { if (!od_lh(n1, p, tk, st[1], en[1], &v1) || od_wide(data(v1), len(v1))) return 0; }
    else append_range(v1, data(v0), len(v0));
    od_pushv(ls, txt, n0, 0, &v0);
    od_pushv(ls, txt, n1, 0, &v1);
    return 1;
}
/* a border side's width, style and colour in any order (each once): items 0, 1, 2 of s (their
   length 0: not given) */
static int od_wsc(const char *wname, const char *sname, const char *cname, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, od_ts *s) {
    uint32_t st[4], en[4];
    int n = od_parts(tk, a, b, st, en, 4);
    if (n <= 0 || n > 3) return 0;
    s->ln[0] = s->ln[1] = s->ln[2] = 0;
    for (int k = 0; k < n; k++) {
        if (!s->ln[0] && od_lh_ts(wname, p, tk, st[k], en[k], s, 0)) continue;
        if (!s->ln[1] && od_lh_ts(sname, p, tk, st[k], en[k], s, 1)) continue;
        if (!s->ln[2] && od_lh_ts(cname, p, tk, st[k], en[k], s, 2)) continue;
        return 0;
    }
    return 1;
}
/* a grid line's default for the end it leaves out: its start when that is a name, else auto */
static void od_line_default(const char *start, size_t n, vector<char> *out) {
    clear(*out);
    /* a single custom ident: no space in it, not a number, not auto, not span */
    int name = n > 0;
    for (size_t i = 0; i < n && name; i++) if (start[i] == ' ') name = 0;
    if (name && ((start[0] >= '0' && start[0] <= '9') || (start[0] == '-' && n > 1 && start[1] >= '0' && start[1] <= '9'))) name = 0;
    if (name && css_omx_eq(start, n, "auto", 4)) name = 0;
    if (name) append_range(*out, start, n);
    else od_cat(out, "auto");
}
static void od_line_default_ts(const od_ts *s, int i, vector<char> *out) { od_line_default(data(s->pool) + s->off[i], s->ln[i], out); }

/* background's layers (CSS Backgrounds 3, 3.10): each layer's image, position (x and y), size,
   repeat, attachment, origin and clip into the text set at layer * 8 + its place (length 0: not
   given), the colour (the last layer's) at item OD_TS_N - 1; its count, 0 when the value is none */
static int od_bg_layers(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int pos_only, od_ts *s) {
    uint32_t st[15], en[15];
    int nl = od_items(tk, a, b, st, en, 15);
    if (nl <= 0) return 0;
    od_ts pp;
    memset(&pp, 0, sizeof pp);
    defer(pp) { vector_char_drop(&pp.pool); }
    for (int layer = 0; layer < nl; layer++) {
        uint32_t ps[24], pe[24];
        int np = od_parts(tk, st[layer], en[layer], ps, pe, 24);
        if (np <= 0) return 0;
        int base = layer * 8;
        int k = 0;
        while (k < np) {
            css_omt t = (*tk)[ps[k]];
            vector<char> x;
            int is_pos = (t.t == OT_IDENT && (css_omt_is(p, &t, "left") || css_omt_is(p, &t, "right") || css_omt_is(p, &t, "top") || css_omt_is(p, &t, "bottom") ||
                                              css_omt_is(p, &t, "center"))) || t.t == OT_DIM || t.t == OT_PCT || (t.t == OT_NUM && t.v == 0) ||
                         (t.t == OT_FUNC && (css_omt_is(p, &t, "calc") || css_omt_is(p, &t, "min") || css_omt_is(p, &t, "max") || css_omt_is(p, &t, "clamp")));
            if (is_pos && !s->ln[base + 1]) {
                /* the position's parts: up to four keywords and lengths, before a slash */
                clear(pp.pool);
                int q = k, npp = 0, kinds[4];
                while (q < np && npp < 4) {
                    css_omt u = (*tk)[ps[q]];
                    int kw = u.t == OT_IDENT && (css_omt_is(p, &u, "left") || css_omt_is(p, &u, "right") || css_omt_is(p, &u, "top") || css_omt_is(p, &u, "bottom") ||
                                                 css_omt_is(p, &u, "center"));
                    vector<char> lv;
                    if (kw) css_omx_tokens(p, tk, ps[q], pe[q], 1, &lv);
                    else if (!od_numeric(p, tk, ps[q], pe[q], OD_K_LEN | OD_K_PCT, 0, &lv)) break;
                    od_ts_setv(&pp, npp, &lv);
                    kinds[npp] = kw;
                    npp++;
                    q++;
                }
                if (!npp) return 0;
                /* x and y from the parts (3.6: one to four values) */
                vector<char> px, py;
                if (npp == 1) {
                    if (od_ts_is(&pp, 0, "top") || od_ts_is(&pp, 0, "bottom")) { od_cat(&px, "center"); od_ts_out(&pp, 0, &py); }
                    else { od_ts_out(&pp, 0, &px); od_cat(&py, "center"); }
                } else if (npp == 2) {
                    int swapped = od_ts_is(&pp, 0, "top") || od_ts_is(&pp, 0, "bottom") || od_ts_is(&pp, 1, "left") || od_ts_is(&pp, 1, "right");
                    if (swapped && (!kinds[0] || !kinds[1])) return 0;
                    od_ts_out(&pp, swapped ? 1 : 0, &px);
                    od_ts_out(&pp, swapped ? 0 : 1, &py);
                } else {
                    /* keywords, each with its offset after it */
                    int i2 = 0;
                    while (i2 < npp) {
                        if (!kinds[i2]) return 0;
                        int isy = od_ts_is(&pp, i2, "top") || od_ts_is(&pp, i2, "bottom");
                        vector<char> *dst = isy ? &py : &px;
                        if (len(*dst)) return 0;
                        od_ts_out(&pp, i2, dst);
                        if (i2 + 1 < npp && !kinds[i2 + 1]) { push_back(*dst, ' '); od_ts_out(&pp, i2 + 1, dst); i2 += 2; }
                        else i2++;
                    }
                    if (!len(px)) od_cat(&px, "center");
                    if (!len(py)) od_cat(&py, "center");
                }
                od_ts_setv(s, base + 1, &px);
                od_ts_setv(s, base + 2, &py);
                k = q;
                /* a size after a slash */
                if (k < np && (*tk)[ps[k]].t == OT_DELIM && p[(*tk)[ps[k]].s] == '/') {
                    if (pos_only) return 0;
                    k++;
                    int q2 = k;
                    vector<char> size;
                    while (q2 < np && q2 - k < 2) {
                        vector<char> sv;
                        css_omt u = (*tk)[ps[q2]];
                        if (u.t == OT_IDENT && (css_omt_is(p, &u, "cover") || css_omt_is(p, &u, "contain"))) {
                            if (q2 != k) break;
                            css_omx_tokens(p, tk, ps[q2], pe[q2], 1, &size);
                            q2++;
                            break;
                        }
                        if (!od_one(p, tk, ps[q2], pe[q2], OD_K_LEN | OD_K_PCT, 1, "auto", &sv)) break;
                        if (len(size)) push_back(size, ' ');
                        append_range(size, data(sv), len(sv));
                        q2++;
                    }
                    if (q2 == k) return 0;
                    od_ts_setv(s, base + 3, &size);
                    k = q2;
                }
                continue;
            }
            if (pos_only) return 0;
            if (!s->ln[base + 0] && od_images(p, tk, ps[k], pe[k], &x)) { od_ts_setv(s, base + 0, &x); k++; continue; }
            clear(x);
            if (!s->ln[base + 4] && t.t == OT_IDENT) {
                /* one or two repeat keywords */
                if (k + 1 < np && (*tk)[ps[k + 1]].t == OT_IDENT && !css_omt_is(p, &t, "repeat-x") && !css_omt_is(p, &t, "repeat-y") &&
                    od_repeat(p, tk, ps[k], pe[k + 1], &x)) {
                    od_ts_setv(s, base + 4, &x);
                    k += 2;
                    continue;
                }
                clear(x);
                if (od_repeat(p, tk, ps[k], pe[k], &x)) { od_ts_setv(s, base + 4, &x); k++; continue; }
            }
            clear(x);
            if (!s->ln[base + 5] && od_list(p, tk, ps[k], pe[k], 0, 0, "scroll fixed local", &x)) { od_ts_setv(s, base + 5, &x); k++; continue; }
            clear(x);
            if (od_list(p, tk, ps[k], pe[k], 0, 0, "border-box padding-box content-box", &x)) {
                if (!s->ln[base + 6]) { od_ts_setv(s, base + 6, &x); k++; continue; }
                if (!s->ln[base + 7]) { od_ts_setv(s, base + 7, &x); k++; continue; }
                return 0;
            }
            clear(x);
            if (!s->ln[base + 7] && od_list(p, tk, ps[k], pe[k], 0, 0, "text", &x)) { od_ts_setv(s, base + 7, &x); k++; continue; }
            clear(x);
            if (layer == nl - 1 && !s->ln[OD_TS_N - 1] && od_color(p, tk, ps[k], pe[k], &x)) { od_ts_setv(s, OD_TS_N - 1, &x); k++; continue; }
            return 0;
        }
        /* one box is the origin and the clip */
        if (s->ln[base + 6] && !s->ln[base + 7]) {
            vector<char> o;
            od_ts_out(s, base + 6, &o);
            od_ts_setv(s, base + 7, &o);
        }
    }
    return nl;
}

static int od_expand(uint32_t sh, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<od_lv> *ls, vector<char> *txt) {
    char nm[64];
    od_copyz(nm, sizeof nm, css_om_prop_name(sh));
    od_ts s;
    memset(&s, 0, sizeof s);
    defer(s) { vector_char_drop(&s.pool); }
    static const char *const margin[] = { "margin-top", "margin-right", "margin-bottom", "margin-left" };
    static const char *const padding[] = { "padding-top", "padding-right", "padding-bottom", "padding-left" };
    static const char *const inset[] = { "top", "right", "bottom", "left" };
    static const char *const bw[] = { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" };
    static const char *const bs[] = { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" };
    static const char *const bc[] = { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" };
    static const char *const smargin[] = { "scroll-margin-top", "scroll-margin-right", "scroll-margin-bottom", "scroll-margin-left" };
    static const char *const spadding[] = { "scroll-padding-top", "scroll-padding-right", "scroll-padding-bottom", "scroll-padding-left" };
    if (!strcmp(nm, "border-image")) {
        /* source || slice [/ width [/ outset]] || repeat; each left out its initial value */
        uint32_t ps[16], pe[16];
        int np = od_parts(tk, a, b, ps, pe, 16);
        if (np <= 0) return 0;
        int k = 0;
        while (k < np) {
            css_omt t = (*tk)[ps[k]];
            vector<char> x;
            if (!s.ln[0] && od_images(p, tk, ps[k], pe[k], &x)) { od_ts_setv(&s, 0, &x); k++; continue; }
            if (!s.ln[4] && t.t == OT_IDENT && od_word_in("stretch repeat round space", p + t.s, t.e - t.s)) {
                vector<char> r;
                css_omx_tokens(p, tk, ps[k], pe[k], 1, &r);
                if (k + 1 < np && (*tk)[ps[k + 1]].t == OT_IDENT && od_word_in("stretch repeat round space", p + (*tk)[ps[k + 1]].s, (*tk)[ps[k + 1]].e - (*tk)[ps[k + 1]].s)) {
                    vector<char> r2;
                    css_omx_tokens(p, tk, ps[k + 1], pe[k + 1], 1, &r2);
                    if (!css_omx_eq(data(r), len(r), data(r2), len(r2))) { push_back(r, ' '); append_range(r, data(r2), len(r2)); }
                    k++;
                }
                od_ts_setv(&s, 4, &r);
                k++;
                continue;
            }
            if (!s.ln[1] && (t.t == OT_NUM || t.t == OT_PCT)) {
                /* the slice's one to four numbers or percentages and fill, then / width, / outset */
                vector<char> sl;
                int q = k, nv = 0;
                while (q < np && ((*tk)[ps[q]].t == OT_NUM || (*tk)[ps[q]].t == OT_PCT || ((*tk)[ps[q]].t == OT_IDENT && css_omt_is(p, &(*tk)[ps[q]], "fill")))) {
                    if (len(sl)) push_back(sl, ' ');
                    css_omx_tokens(p, tk, ps[q], pe[q], 1, &sl);
                    q++;
                    nv++;
                }
                if (nv > 5) return 0;
                od_ts_setv(&s, 1, &sl);
                k = q;
                for (int part = 2; part <= 3 && k < np && (*tk)[ps[k]].t == OT_DELIM && p[(*tk)[ps[k]].s] == '/'; part++) {
                    k++;
                    vector<char> w;
                    int q2 = k;
                    while (q2 < np && !((*tk)[ps[q2]].t == OT_DELIM && p[(*tk)[ps[q2]].s] == '/') && q2 - k < 4) {
                        vector<char> one;
                        css_omt u = (*tk)[ps[q2]];
                        if (u.t == OT_IDENT && css_omt_is(p, &u, "auto")) od_cat(&one, "auto");
                        else if (!od_numeric(p, tk, ps[q2], pe[q2], OD_K_LEN | OD_K_PCT | OD_K_NUM, 1, &one)) break;
                        if (len(w)) push_back(w, ' ');
                        append_range(w, data(one), len(one));
                        q2++;
                    }
                    if (q2 == k) return 0;
                    od_ts_setv(&s, part, &w);
                    k = q2;
                }
                continue;
            }
            return 0;
        }
        static const char *const names[] = { "border-image-source", "border-image-slice", "border-image-width", "border-image-outset", "border-image-repeat" };
        static const char *const init[] = { "none", "100%", "1", "0", "stretch" };
        for (int q = 0; q < 5; q++) {
            if (s.ln[q]) od_push_ts(ls, txt, names[q], 0, &s, q);
            else od_pushz(ls, txt, names[q], 0, init[q]);
        }
        return 1;
    }
    if (!strcmp(nm, "font-variant")) {
        /* normal, none, or each keyword its longhand's */
        uint32_t ps[16], pe[16];
        int np = od_parts(tk, a, b, ps, pe, 16);
        if (np <= 0) return 0;
        static const char *const names[] = { "font-variant-ligatures", "font-variant-caps", "font-variant-alternates", "font-variant-numeric",
                                             "font-variant-east-asian", "font-variant-position", "font-variant-emoji" };
        static const char *const kws[] = {
            "common-ligatures no-common-ligatures discretionary-ligatures no-discretionary-ligatures historical-ligatures no-historical-ligatures contextual no-contextual",
            "small-caps all-small-caps petite-caps all-petite-caps unicase titling-caps", "historical-forms",
            "lining-nums oldstyle-nums proportional-nums tabular-nums diagonal-fractions stacked-fractions ordinal slashed-zero",
            "jis78 jis83 jis90 jis04 simplified traditional full-width proportional-width ruby", "sub super", "text emoji unicode" };
        int single = np == 1 && (*tk)[ps[0]].t == OT_IDENT && (css_omt_is(p, &(*tk)[ps[0]], "normal") || css_omt_is(p, &(*tk)[ps[0]], "none"));
        if (single) {
            int none = css_omt_is(p, &(*tk)[ps[0]], "none");
            static const int order_normal[] = { 0, 1, 3, 4, 2, 5, 6 };
            for (int q = 0; q < 7; q++) od_pushz(ls, txt, names[order_normal[q]], 0, (none && q == 0) ? "none" : "normal");
            return 1;
        }
        for (int k = 0; k < np; k++) {
            css_omt t = (*tk)[ps[k]];
            if (t.t != OT_IDENT || pe[k] != ps[k] + 1) return 0;
            int placed = 0;
            for (int q = 0; q < 7 && !placed; q++) {
                if (!od_word_in(kws[q], p + t.s, t.e - t.s)) continue;
                vector<char> w;
                if (s.ln[q]) { od_ts_out(&s, q, &w); push_back(w, ' '); }
                css_omx_tokens(p, tk, ps[k], pe[k], 1, &w);
                od_ts_setv(&s, q, &w);
                placed = 1;
            }
            if (!placed) return 0;
        }
        /* in the order Chromium reads them */
        static const int order[] = { 0, 3, 4, 1, 2, 5, 6 };
        for (int q = 0; q < 7; q++) {
            int i = order[q];
            if (s.ln[i]) od_push_ts(ls, txt, names[i], 0, &s, i);
            else od_pushz(ls, txt, names[i], 0, "normal");
        }
        return 1;
    }
    if (!strcmp(nm, "font-synthesis")) {
        uint32_t ps[4], pe[4];
        int np = od_parts(tk, a, b, ps, pe, 4);
        if (np <= 0 || np > 3) return 0;
        int w = 0, st0 = 0, sc = 0;
        if (!(np == 1 && (*tk)[ps[0]].t == OT_IDENT && css_omt_is(p, &(*tk)[ps[0]], "none")))
            for (int k = 0; k < np; k++) {
                css_omt t = (*tk)[ps[k]];
                if (t.t != OT_IDENT) return 0;
                if (css_omt_is(p, &t, "weight") && !w) w = 1;
                else if (css_omt_is(p, &t, "style") && !st0) st0 = 1;
                else if (css_omt_is(p, &t, "small-caps") && !sc) sc = 1;
                else return 0;
            }
        od_pushz(ls, txt, "font-synthesis-weight", 0, w ? "auto" : "none");
        od_pushz(ls, txt, "font-synthesis-style", 0, st0 ? "auto" : "none");
        od_pushz(ls, txt, "font-synthesis-small-caps", 0, sc ? "auto" : "none");
        return 1;
    }
    if (!strcmp(nm, "contain-intrinsic-size")) {
        /* one or two of [auto] (none | <length>) */
        uint32_t ps[5], pe[5];
        int np = od_parts(tk, a, b, ps, pe, 5);
        int k = 0, nv = 0;
        while (k < np) {
            vector<char> v;
            if ((*tk)[ps[k]].t == OT_IDENT && css_omt_is(p, &(*tk)[ps[k]], "auto")) {
                od_cat(&v, "auto ");
                k++;
                if (k >= np) return 0;
            }
            vector<char> one;
            if ((*tk)[ps[k]].t == OT_IDENT && css_omt_is(p, &(*tk)[ps[k]], "none")) od_cat(&one, "none");
            else if (!od_numeric(p, tk, ps[k], pe[k], OD_K_LEN, 1, &one)) return 0;
            append_range(v, data(one), len(one));
            if (nv >= 2) return 0;
            od_ts_setv(&s, nv++, &v);
            k++;
        }
        if (!nv) return 0;
        if (nv == 1) { vector<char> v; od_ts_out(&s, 0, &v); od_ts_setv(&s, 1, &v); }
        od_push_ts(ls, txt, "contain-intrinsic-width", 0, &s, 0);
        od_push_ts(ls, txt, "contain-intrinsic-height", 0, &s, 1);
        return 1;
    }
    if (!strcmp(nm, "margin")) return od_four(margin, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "padding")) return od_four(padding, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "inset")) return od_four(inset, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "border-width")) return od_four(bw, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "border-style")) return od_four(bs, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "border-color")) return od_four(bc, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "scroll-margin")) return od_four(smargin, p, tk, a, b, ls, txt);
    if (!strcmp(nm, "scroll-padding")) return od_four(spadding, p, tk, a, b, ls, txt);
    /* the pairs */
    static const char *const pairs[][3] = {
        { "margin-block", "margin-block-start", "margin-block-end" }, { "margin-inline", "margin-inline-start", "margin-inline-end" },
        { "padding-block", "padding-block-start", "padding-block-end" }, { "padding-inline", "padding-inline-start", "padding-inline-end" },
        { "inset-block", "inset-block-start", "inset-block-end" }, { "inset-inline", "inset-inline-start", "inset-inline-end" },
        { "scroll-margin-block", "scroll-margin-block-start", "scroll-margin-block-end" },
        { "scroll-margin-inline", "scroll-margin-inline-start", "scroll-margin-inline-end" },
        { "scroll-padding-block", "scroll-padding-block-start", "scroll-padding-block-end" },
        { "scroll-padding-inline", "scroll-padding-inline-start", "scroll-padding-inline-end" },
        { "border-block-width", "border-block-start-width", "border-block-end-width" },
        { "border-block-style", "border-block-start-style", "border-block-end-style" },
        { "border-block-color", "border-block-start-color", "border-block-end-color" },
        { "border-inline-width", "border-inline-start-width", "border-inline-end-width" },
        { "border-inline-style", "border-inline-start-style", "border-inline-end-style" },
        { "border-inline-color", "border-inline-start-color", "border-inline-end-color" },
        { "overflow", "overflow-x", "overflow-y" }, { "gap", "row-gap", "column-gap" },
        { "place-content", "align-content", "justify-content" }, { "place-items", "align-items", "justify-items" },
        { "place-self", "align-self", "justify-self" }, { "overscroll-behavior", "overscroll-behavior-x", "overscroll-behavior-y" },
        { "contain-intrinsic-size", "contain-intrinsic-width", "contain-intrinsic-height" },
        { "border-spacing", "-webkit-border-horizontal-spacing", "-webkit-border-vertical-spacing" }, { 0, 0, 0 } };
    for (int k = 0; pairs[k][0]; k++) if (!strcmp(nm, pairs[k][0])) return od_two(pairs[k][1], pairs[k][2], p, tk, a, b, ls, txt);
    if (!strcmp(nm, "border-radius")) {
        uint32_t slash = b;
        for (uint32_t i = a; i < b; i++) if ((*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '/') { slash = i; break; }
        uint32_t hs[5], he[5], vs[5], ve[5];
        int nh = od_parts(tk, a, slash, hs, he, 5), nv = slash < b ? od_parts(tk, slash + 1, b, vs, ve, 5) : 0;
        if (nh <= 0 || nh > 4 || (slash < b && (nv <= 0 || nv > 4))) return 0;
        /* items 0-3 the horizontal radii, 4-7 the vertical */
        for (int k = 0; k < nh; k++) {
            vector<char> x;
            if (!od_numeric(p, tk, hs[k], he[k], OD_K_LEN | OD_K_PCT, 1, &x)) return 0;
            od_ts_setv(&s, k, &x);
        }
        for (int k = 0; k < nv; k++) {
            vector<char> x;
            if (!od_numeric(p, tk, vs[k], ve[k], OD_K_LEN | OD_K_PCT, 1, &x)) return 0;
            od_ts_setv(&s, 4 + k, &x);
        }
        static const int side[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
        static const char *const corners[] = { "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius" };
        for (int c = 0; c < 4; c++) {
            vector<char> one;
            int hx = side[nh - 1][c];
            od_ts_out(&s, hx, &one);
            if (nv) {
                int vx = 4 + side[nv - 1][c];
                if (!od_ts_eq(&s, hx, vx)) { push_back(one, ' '); od_ts_out(&s, vx, &one); }
            }
            od_pushv(ls, txt, corners[c], 0, &one);
        }
        return 1;
    }
    /* the border sides: width, style, colour; the ones left out their initial values */
    static const char *const sides[][4] = { { "border-top", "border-top-width", "border-top-style", "border-top-color" },
                                            { "border-right", "border-right-width", "border-right-style", "border-right-color" },
                                            { "border-bottom", "border-bottom-width", "border-bottom-style", "border-bottom-color" },
                                            { "border-left", "border-left-width", "border-left-style", "border-left-color" },
                                            { "border-block-start", "border-block-start-width", "border-block-start-style", "border-block-start-color" },
                                            { "border-block-end", "border-block-end-width", "border-block-end-style", "border-block-end-color" },
                                            { "border-inline-start", "border-inline-start-width", "border-inline-start-style", "border-inline-start-color" },
                                            { "border-inline-end", "border-inline-end-width", "border-inline-end-style", "border-inline-end-color" },
                                            { "column-rule", "column-rule-width", "column-rule-style", "column-rule-color" }, { 0, 0, 0, 0 } };
    for (int k = 0; sides[k][0]; k++) {
        if (strcmp(nm, sides[k][0])) continue;
        if (!od_wsc(sides[k][1], sides[k][2], sides[k][3], p, tk, a, b, &s)) return 0;
        int logical = k >= 4 && k < 8;
        static const char *const init[] = { "medium", "none", "currentcolor" };
        for (int q = 0; q < 3; q++) {
            if (s.ln[q]) od_push_ts(ls, txt, sides[k][q + 1], 0, &s, q);
            else od_pushz(ls, txt, sides[k][q + 1], CSS_OM_IMPLICIT, logical ? "initial" : init[q]);
        }
        return 1;
    }
    if (!strcmp(nm, "border-block") || !strcmp(nm, "border-inline")) {
        int blk = nm[7] == 'b';
        if (!od_wsc("border-top-width", "border-top-style", "border-top-color", p, tk, a, b, &s)) return 0;
        const char *pre = blk ? "border-block-" : "border-inline-";
        static const char *const what[] = { "width", "style", "color" };
        static const char *const dflt[] = { "medium", "none", "currentcolor" };
        for (int q = 0; q < 3; q++) {
            char n1[48], n2[48];
            snprintf(n1, sizeof n1, "%sstart-%s", pre, what[q]);
            snprintf(n2, sizeof n2, "%send-%s", pre, what[q]);
            if (s.ln[q]) { od_push_ts(ls, txt, n1, 0, &s, q); od_push_ts(ls, txt, n2, 0, &s, q); }
            else { od_pushz(ls, txt, n1, CSS_OM_IMPLICIT, dflt[q]); od_pushz(ls, txt, n2, CSS_OM_IMPLICIT, dflt[q]); }
        }
        return 1;
    }
    if (!strcmp(nm, "border")) {
        if (!od_wsc("border-top-width", "border-top-style", "border-top-color", p, tk, a, b, &s)) return 0;
        if (!s.ln[0]) od_ts_setz(&s, 0, "medium");
        if (!s.ln[1]) od_ts_setz(&s, 1, "none");
        if (!s.ln[2]) od_ts_setz(&s, 2, "currentcolor");
        for (int q = 0; q < 4; q++) od_push_ts(ls, txt, bw[q], 0, &s, 0);
        for (int q = 0; q < 4; q++) od_push_ts(ls, txt, bs[q], 0, &s, 1);
        for (int q = 0; q < 4; q++) od_push_ts(ls, txt, bc[q], 0, &s, 2);
        od_pushz(ls, txt, "border-image-source", CSS_OM_IMPLICIT, "none");
        od_pushz(ls, txt, "border-image-slice", CSS_OM_IMPLICIT, "100%");
        od_pushz(ls, txt, "border-image-width", CSS_OM_IMPLICIT, "1");
        od_pushz(ls, txt, "border-image-outset", CSS_OM_IMPLICIT, "0");
        od_pushz(ls, txt, "border-image-repeat", CSS_OM_IMPLICIT, "stretch");
        return 1;
    }
    if (!strcmp(nm, "outline")) {
        if (!od_wsc("outline-width", "outline-style", "outline-color", p, tk, a, b, &s)) return 0;
        static const char *const names[] = { "outline-color", "outline-style", "outline-width" };
        static const int at[] = { 2, 1, 0 };
        for (int q = 0; q < 3; q++) {
            if (s.ln[at[q]]) od_push_ts(ls, txt, names[q], 0, &s, at[q]);
            else od_pushz(ls, txt, names[q], CSS_OM_IMPLICIT, "initial");
        }
        return 1;
    }
    if (!strcmp(nm, "flex")) {
        uint32_t st[4], en[4];
        int n = od_parts(tk, a, b, st, en, 4);
        if (n <= 0 || n > 3) return 0;
        if (n == 1 && (*tk)[st[0]].t == OT_IDENT && css_omt_is(p, &(*tk)[st[0]], "none")) {
            od_pushz(ls, txt, "flex-grow", 0, "0"); od_pushz(ls, txt, "flex-shrink", 0, "0"); od_pushz(ls, txt, "flex-basis", 0, "auto");
            return 1;
        }
        if (n == 1 && (*tk)[st[0]].t == OT_IDENT && css_omt_is(p, &(*tk)[st[0]], "auto")) {
            od_pushz(ls, txt, "flex-grow", 0, "1"); od_pushz(ls, txt, "flex-shrink", 0, "1"); od_pushz(ls, txt, "flex-basis", 0, "auto");
            return 1;
        }
        /* items 0 grow, 1 shrink, 2 basis: the numbers a grow and a shrink, the first two of them
           together; a third unitless zero the basis */
        int nn = 0, after_basis = 0;
        for (int k = 0; k < n; k++) {
            css_omt t = (*tk)[st[k]];
            if (t.t == OT_NUM && en[k] == st[k] + 1) {
                if (t.v < 0) return 0;
                vector<char> x;
                if (nn == 0 && !after_basis) { css_omx_num(t.v, &x); od_ts_setv(&s, 0, &x); nn = 1; continue; }
                if (nn == 1 && !s.ln[1] && (!s.ln[2] || 1)) {
                    if (s.ln[2] && after_basis) return 0;
                    if (!s.ln[2] || !after_basis) { css_omx_num(t.v, &x); od_ts_setv(&s, 1, &x); nn = 2; continue; }
                }
                if (nn == 2 && t.v == 0 && !s.ln[2]) { od_ts_setz(&s, 2, "0px"); continue; }
                return 0;
            }
            if (s.ln[2]) return 0;
            if (nn == 1 && k > 1) return 0;
            if (!od_lh_ts("flex-basis", p, tk, st[k], en[k], &s, 2)) return 0;
            if (nn) { if (k + 1 < n) return 0; }
            else after_basis = 1;
        }
        if (!s.ln[0]) od_ts_setz(&s, 0, "1");
        if (!s.ln[1]) od_ts_setz(&s, 1, "1");
        if (!s.ln[2]) od_ts_setz(&s, 2, "0%");
        od_push_ts(ls, txt, "flex-grow", 0, &s, 0);
        od_push_ts(ls, txt, "flex-shrink", 0, &s, 1);
        od_push_ts(ls, txt, "flex-basis", 0, &s, 2);
        return 1;
    }
    if (!strcmp(nm, "flex-flow")) {
        uint32_t st[3], en[3];
        int n = od_parts(tk, a, b, st, en, 3);
        if (n <= 0 || n > 2) return 0;
        for (int k = 0; k < n; k++) {
            if (!s.ln[0] && od_lh_ts("flex-direction", p, tk, st[k], en[k], &s, 0)) continue;
            if (!s.ln[1] && od_lh_ts("flex-wrap", p, tk, st[k], en[k], &s, 1)) continue;
            return 0;
        }
        if (!s.ln[0]) od_ts_setz(&s, 0, "row");
        if (!s.ln[1]) od_ts_setz(&s, 1, "nowrap");
        od_push_ts(ls, txt, "flex-direction", 0, &s, 0);
        od_push_ts(ls, txt, "flex-wrap", 0, &s, 1);
        return 1;
    }
    if (!strcmp(nm, "list-style")) {
        /* items 0 position, 1 image, 2 type */
        uint32_t st[4], en[4];
        int n = od_parts(tk, a, b, st, en, 4);
        if (n <= 0 || n > 3) return 0;
        int nones = 0;
        for (int k = 0; k < n; k++) {
            css_omt t = (*tk)[st[k]];
            if (t.t == OT_IDENT && en[k] == st[k] + 1 && css_omt_is(p, &t, "none")) { nones++; continue; }
            if (!s.ln[0] && od_lh_ts("list-style-position", p, tk, st[k], en[k], &s, 0)) continue;
            if (!s.ln[1] && t.t != OT_IDENT && od_lh_ts("list-style-image", p, tk, st[k], en[k], &s, 1)) continue;
            if (!s.ln[2] && (t.t == OT_IDENT || t.t == OT_STR) && en[k] == st[k] + 1) {
                vector<char> w, ty;
                css_omt_text(p, &t, &w);
                if (od_wide(data(w), len(w))) return 0;
                if (t.t == OT_STR) css_omx_string(data(w), len(w), &ty);
                else if (od_keyword(data(w), len(w))) css_omx_lower(data(w), len(w), &ty);
                else css_omx_ident(data(w), len(w), &ty);
                od_ts_setv(&s, 2, &ty);
                continue;
            }
            return 0;
        }
        /* none: the type's when it is not given, else the image's */
        if (nones > 2) return 0;
        if (nones == 2) { if (s.ln[1] || s.ln[2]) return 0; od_ts_setz(&s, 1, "none"); od_ts_setz(&s, 2, "none"); }
        else if (nones == 1) {
            if (!s.ln[2]) od_ts_setz(&s, 2, "none");
            else if (!s.ln[1]) od_ts_setz(&s, 1, "none");
            else return 0;
        }
        static const char *const names[] = { "list-style-position", "list-style-image", "list-style-type" };
        for (int q = 0; q < 3; q++) {
            if (s.ln[q]) od_push_ts(ls, txt, names[q], 0, &s, q);
            else od_pushz(ls, txt, names[q], CSS_OM_IMPLICIT, "initial");
        }
        return 1;
    }
    if (!strcmp(nm, "text-decoration")) {
        /* items 0 line, 1 thickness, 2 style, 3 colour */
        uint32_t st[8], en[8];
        int n = od_parts(tk, a, b, st, en, 8);
        if (n <= 0) return 0;
        int k = 0;
        while (k < n) {
            css_omt t = (*tk)[st[k]];
            int is_line = t.t == OT_IDENT && (css_omt_is(p, &t, "underline") || css_omt_is(p, &t, "overline") || css_omt_is(p, &t, "line-through") ||
                                              css_omt_is(p, &t, "blink") || css_omt_is(p, &t, "none"));
            if (!s.ln[0] && is_line) {
                /* the line keywords run together */
                int q = k;
                while (q < n && (*tk)[st[q]].t == OT_IDENT && (css_omt_is(p, &(*tk)[st[q]], "underline") || css_omt_is(p, &(*tk)[st[q]], "overline") ||
                                                               css_omt_is(p, &(*tk)[st[q]], "line-through") || css_omt_is(p, &(*tk)[st[q]], "blink") ||
                                                               css_omt_is(p, &(*tk)[st[q]], "none"))) q++;
                if (!od_lh_ts("text-decoration-line", p, tk, st[k], en[q - 1], &s, 0)) return 0;
                k = q;
                continue;
            }
            if (!s.ln[2] && od_lh_ts("text-decoration-style", p, tk, st[k], en[k], &s, 2)) { k++; continue; }
            if (!s.ln[1] && od_lh_ts("text-decoration-thickness", p, tk, st[k], en[k], &s, 1)) { k++; continue; }
            if (!s.ln[3] && od_lh_ts("text-decoration-color", p, tk, st[k], en[k], &s, 3)) { k++; continue; }
            return 0;
        }
        static const char *const names[] = { "text-decoration-line", "text-decoration-thickness", "text-decoration-style", "text-decoration-color" };
        for (int q = 0; q < 4; q++) {
            if (s.ln[q]) od_push_ts(ls, txt, names[q], 0, &s, q);
            else od_pushz(ls, txt, names[q], CSS_OM_IMPLICIT, "initial");
        }
        return 1;
    }
    if (!strcmp(nm, "white-space")) {
        uint32_t st[3], en[3];
        int n = od_parts(tk, a, b, st, en, 3);
        if (n <= 0 || n > 2) return 0;
        if (n == 1 && (*tk)[st[0]].t == OT_IDENT) {
            static const char *const kw[][3] = { { "normal", "collapse", "wrap" }, { "pre", "preserve", "nowrap" }, { "nowrap", "collapse", "nowrap" },
                                                 { "pre-wrap", "preserve", "wrap" }, { "break-spaces", "break-spaces", "wrap" },
                                                 { "pre-line", "preserve-breaks", "wrap" }, { 0, 0, 0 } };
            for (int q = 0; kw[q][0]; q++)
                if (css_omt_is(p, &(*tk)[st[0]], kw[q][0])) {
                    od_pushz(ls, txt, "white-space-collapse", 0, kw[q][1]);
                    od_pushz(ls, txt, "text-wrap-mode", 0, kw[q][2]);
                    return 1;
                }
        }
        for (int k = 0; k < n; k++) {
            if (!s.ln[0] && od_lh_ts("white-space-collapse", p, tk, st[k], en[k], &s, 0)) continue;
            if (!s.ln[1] && od_lh_ts("text-wrap-mode", p, tk, st[k], en[k], &s, 1)) continue;
            return 0;
        }
        if (!s.ln[0]) od_ts_setz(&s, 0, "collapse");
        if (!s.ln[1]) od_ts_setz(&s, 1, "wrap");
        od_push_ts(ls, txt, "white-space-collapse", 0, &s, 0);
        od_push_ts(ls, txt, "text-wrap-mode", 0, &s, 1);
        return 1;
    }
    if (!strcmp(nm, "text-wrap")) {
        uint32_t st[3], en[3];
        int n = od_parts(tk, a, b, st, en, 3);
        if (n <= 0 || n > 2) return 0;
        for (int k = 0; k < n; k++) {
            if (!s.ln[0] && od_lh_ts("text-wrap-mode", p, tk, st[k], en[k], &s, 0)) continue;
            if (!s.ln[1] && od_lh_ts("text-wrap-style", p, tk, st[k], en[k], &s, 1)) continue;
            return 0;
        }
        if (s.ln[0]) od_push_ts(ls, txt, "text-wrap-mode", 0, &s, 0); else od_pushz(ls, txt, "text-wrap-mode", CSS_OM_IMPLICIT, "initial");
        if (s.ln[1]) od_push_ts(ls, txt, "text-wrap-style", 0, &s, 1); else od_pushz(ls, txt, "text-wrap-style", CSS_OM_IMPLICIT, "initial");
        return 1;
    }
    if (!strcmp(nm, "container")) {
        uint32_t slash = b;
        for (uint32_t i = a; i < b; i++) if ((*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '/') { slash = i; break; }
        vector<char> name, type;
        if (!od_idents(p, tk, a, slash, "none", &name)) return 0;
        for (size_t q = 0; q < len(name); q++) if (name[q] == ',') return 0;
        if (slash < b) { if (!od_lh("container-type", p, tk, slash + 1, b, &type)) return 0; }
        else od_cat(&type, "normal");
        od_pushv(ls, txt, "container-name", 0, &name);
        od_pushv(ls, txt, "container-type", 0, &type);
        return 1;
    }
    if (!strcmp(nm, "columns")) {
        uint32_t st[3], en[3];
        int n = od_parts(tk, a, b, st, en, 3);
        if (n <= 0 || n > 2) return 0;
        for (int k = 0; k < n; k++) {
            css_omt t = (*tk)[st[k]];
            if (t.t == OT_IDENT && en[k] == st[k] + 1 && css_omt_is(p, &t, "auto")) continue;
            if (t.t == OT_NUM && en[k] == st[k] + 1 && !s.ln[1]) {
                if (!(t.f & OTF_INT) || t.v < 1) return 0;
                vector<char> x;
                css_omx_num(t.v, &x);
                od_ts_setv(&s, 1, &x);
                continue;
            }
            vector<char> x;
            if (!s.ln[0] && od_numeric(p, tk, st[k], en[k], OD_K_LEN, 1, &x)) { od_ts_setv(&s, 0, &x); continue; }
            return 0;
        }
        if (!s.ln[0]) od_ts_setz(&s, 0, "auto");
        if (!s.ln[1]) od_ts_setz(&s, 1, "auto");
        od_push_ts(ls, txt, "column-width", 0, &s, 0);
        od_push_ts(ls, txt, "column-count", 0, &s, 1);
        od_pushz(ls, txt, "column-height", CSS_OM_IMPLICIT, "auto");
        od_pushz(ls, txt, "column-wrap", CSS_OM_IMPLICIT, "auto");
        return 1;
    }
    if (!strcmp(nm, "grid-row") || !strcmp(nm, "grid-column")) {
        int row = nm[5] == 'r';
        uint32_t slash = b;
        for (uint32_t i = a; i < b; i++) if ((*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '/') { slash = i; break; }
        vector<char> st0, en0;
        if (!od_line(p, tk, a, slash, &st0)) return 0;
        if (slash < b) { if (!od_line(p, tk, slash + 1, b, &en0)) return 0; }
        else od_line_default(data(st0), len(st0), &en0);
        od_pushv(ls, txt, row ? "grid-row-start" : "grid-column-start", 0, &st0);
        od_pushv(ls, txt, row ? "grid-row-end" : "grid-column-end", 0, &en0);
        return 1;
    }
    if (!strcmp(nm, "grid-area")) {
        uint32_t cut[5];
        int nc = 0;
        cut[nc++] = a;
        for (uint32_t i = a; i < b; i++) if ((*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '/') { if (nc >= 4) return 0; cut[nc++] = i + 1; }
        for (int k = 0; k < nc; k++) {
            uint32_t e = k + 1 < nc ? cut[k + 1] - 1 : b;
            vector<char> x;
            if (!od_line(p, tk, cut[k], e, &x)) return 0;
            od_ts_setv(&s, k, &x);
        }
        vector<char> d;
        if (nc < 2) { od_line_default_ts(&s, 0, &d); od_ts_setv(&s, 1, &d); }
        if (nc < 3) { od_line_default_ts(&s, 0, &d); od_ts_setv(&s, 2, &d); }
        if (nc < 4) { od_line_default_ts(&s, 1, &d); od_ts_setv(&s, 3, &d); }
        od_push_ts(ls, txt, "grid-row-start", 0, &s, 0);
        od_push_ts(ls, txt, "grid-column-start", 0, &s, 1);
        od_push_ts(ls, txt, "grid-row-end", 0, &s, 2);
        od_push_ts(ls, txt, "grid-column-end", 0, &s, 3);
        return 1;
    }
    if (!strcmp(nm, "marker")) {
        vector<char> v;
        if (!od_images(p, tk, a, b, &v)) {
            clear(v);
            od_generic(p, tk, a, b, &v);
        }
        od_pushv(ls, txt, "marker-start", 0, &v);
        od_pushv(ls, txt, "marker-mid", 0, &v);
        od_pushv(ls, txt, "marker-end", 0, &v);
        return 1;
    }
    if (!strcmp(nm, "text-emphasis")) {
        uint32_t st[4], en[4];
        int n = od_parts(tk, a, b, st, en, 4);
        if (n <= 0 || n > 3) return 0;
        vector<char> style, color;
        for (int k = 0; k < n; k++) {
            vector<char> c;
            if (!len(color) && od_color(p, tk, st[k], en[k], &c)) { append_range(color, data(c), len(c)); continue; }
            if (len(style)) push_back(style, ' ');
            css_omx_tokens(p, tk, st[k], en[k], 1, &style);
        }
        if (len(style)) od_pushv(ls, txt, "text-emphasis-style", 0, &style); else od_pushz(ls, txt, "text-emphasis-style", CSS_OM_IMPLICIT, "initial");
        if (len(color)) od_pushv(ls, txt, "text-emphasis-color", 0, &color); else od_pushz(ls, txt, "text-emphasis-color", CSS_OM_IMPLICIT, "initial");
        return 1;
    }
    if (!strcmp(nm, "background") || !strcmp(nm, "background-position")) {
        int pos_only = nm[10] == '-';
        int nl = od_bg_layers(p, tk, a, b, pos_only, &s);
        if (!nl) return 0;
        static const char *const names[] = { "background-image", "background-position-x", "background-position-y", "background-size", "background-repeat",
                                             "background-attachment", "background-origin", "background-clip", "background-color" };
        int lo = pos_only ? 1 : 0, hi = pos_only ? 3 : 8;
        for (int q = lo; q < hi; q++) {
            vector<char> list;
            int given = 0;
            for (int layer = 0; layer < nl; layer++) {
                if (layer) append_range(list, ", ", 2);
                int i = layer * 8 + q;
                if (s.ln[i]) { od_ts_out(&s, i, &list); given = 1; }
                else {
                    if (pos_only) return 0;
                    od_cat(&list, "initial");
                }
            }
            od_pushv(ls, txt, names[q], given ? 0 : CSS_OM_IMPLICIT, &list);
        }
        if (!pos_only) {
            if (s.ln[OD_TS_N - 1]) od_push_ts(ls, txt, "background-color", 0, &s, OD_TS_N - 1);
            else od_pushz(ls, txt, "background-color", CSS_OM_IMPLICIT, "initial");
        }
        return 1;
    }
    if (!strcmp(nm, "font")) {
        uint32_t ps[24], pe[24];
        int np = od_parts(tk, a, b, ps, pe, 24);
        if (np <= 0) return 0;
        /* a system font: every longhand of it, pending on it */
        if (np == 1 && (*tk)[ps[0]].t == OT_IDENT) {
            static const char *const sys = "caption icon menu message-box small-caption status-bar";
            css_omt t = (*tk)[ps[0]];
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (od_word_in(sys, data(v), len(v))) {
                vector<char> lv;
                css_omx_lower(data(v), len(v), &lv);
                uint32_t nl = css_om_longhands(sh);
                for (uint32_t q = 0; q < nl; q++) od_push(ls, txt, css_om_longhand(sh, q), CSS_OM_PENDING, sh, data(lv), len(lv));
                return 1;
            }
        }
        /* items 0 style, 1 variant-caps, 2 weight, 3 stretch, 4 size, 5 line-height, 6 family */
        int k = 0, normals = 0;
        while (k < np) {
            css_omt t = (*tk)[ps[k]];
            vector<char> x;
            if (t.t == OT_IDENT && pe[k] == ps[k] + 1 && css_omt_is(p, &t, "normal")) { normals++; k++; continue; }
            if (!s.ln[0] && t.t == OT_IDENT && (css_omt_is(p, &t, "italic") || css_omt_is(p, &t, "oblique"))) {
                css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                od_ts_setv(&s, 0, &x);
                k++;
                continue;
            }
            if (!s.ln[1] && t.t == OT_IDENT && css_omt_is(p, &t, "small-caps")) { od_ts_setz(&s, 1, "small-caps"); k++; continue; }
            if (!s.ln[2] && ((t.t == OT_IDENT && (css_omt_is(p, &t, "bold") || css_omt_is(p, &t, "bolder") || css_omt_is(p, &t, "lighter"))) ||
                             (t.t == OT_NUM && t.v >= 1 && t.v <= 1000))) {
                css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                od_ts_setv(&s, 2, &x);
                k++;
                continue;
            }
            if (!s.ln[3] && t.t == OT_IDENT && od_word_in("ultra-condensed extra-condensed condensed semi-condensed semi-expanded expanded extra-expanded ultra-expanded",
                                                          p + t.s, t.e - t.s)) {
                css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                od_ts_setv(&s, 3, &x);
                k++;
                continue;
            }
            break;
        }
        if (normals + (s.ln[0] > 0) + (s.ln[1] > 0) + (s.ln[2] > 0) + (s.ln[3] > 0) > 4) return 0;
        if (k >= np || !od_lh_ts("font-size", p, tk, ps[k], pe[k], &s, 4)) return 0;
        k++;
        if (k < np && (*tk)[ps[k]].t == OT_DELIM && p[(*tk)[ps[k]].s] == '/') {
            if (k + 1 >= np || !od_lh_ts("line-height", p, tk, ps[k + 1], pe[k + 1], &s, 5)) return 0;
            k += 2;
        }
        vector<char> fam;
        if (k >= np || !od_family(p, tk, ps[k], b, &fam)) return 0;
        od_ts_setv(&s, 6, &fam);
        static const int dflt_at[] = { 0, 1, 2, 3, 5 };
        for (int q = 0; q < 5; q++) if (!s.ln[dflt_at[q]]) od_ts_setz(&s, dflt_at[q], "normal");
        od_push_ts(ls, txt, "font-style", 0, &s, 0);
        od_push_ts(ls, txt, "font-variant-caps", 0, &s, 1);
        static const char *const resets[][2] = { { "font-variant-ligatures", "normal" }, { "font-variant-numeric", "normal" },
                                                 { "font-variant-east-asian", "normal" }, { "font-variant-alternates", "normal" },
                                                 { "font-size-adjust", "none" }, { "font-language-override", "normal" }, { "font-kerning", "auto" },
                                                 { "font-optical-sizing", "auto" }, { "font-feature-settings", "normal" },
                                                 { "font-variation-settings", "normal" }, { "font-variant-position", "normal" },
                                                 { "font-variant-emoji", "normal" }, { 0, 0 } };
        for (int q = 0; resets[q][0]; q++) od_pushz(ls, txt, resets[q][0], CSS_OM_IMPLICIT, resets[q][1]);
        od_push_ts(ls, txt, "font-weight", 0, &s, 2);
        od_push_ts(ls, txt, "font-stretch", 0, &s, 3);
        od_push_ts(ls, txt, "font-size", 0, &s, 4);
        od_push_ts(ls, txt, "line-height", 0, &s, 5);
        od_push_ts(ls, txt, "font-family", 0, &s, 6);
        return 1;
    }
    if (!strcmp(nm, "transition")) {
        /* each item's behavior, duration, timing, delay and property at item * 5 + its place */
        uint32_t st[25], en[25];
        int n = od_items(tk, a, b, st, en, 25);
        if (n <= 0) return 0;
        for (int item = 0; item < n; item++) {
            uint32_t ps[8], pe[8];
            int np = od_parts(tk, st[item], en[item], ps, pe, 8);
            if (np <= 0) return 0;
            int base = item * 5, times = 0;
            for (int k = 0; k < np; k++) {
                css_omt t = (*tk)[ps[k]];
                vector<char> x;
                if (t.t == OT_DIM && od_numeric(p, tk, ps[k], pe[k], OD_K_TIME, 0, &x)) {
                    if (times == 0) { if (x[0] == '-') return 0; od_ts_setv(&s, base + 1, &x); }
                    else if (times == 1) od_ts_setv(&s, base + 3, &x);
                    else return 0;
                    times++;
                    continue;
                }
                clear(x);
                if (!s.ln[base + 2] && od_easing(p, tk, ps[k], pe[k], &x)) { od_ts_setv(&s, base + 2, &x); continue; }
                clear(x);
                if (!s.ln[base] && t.t == OT_IDENT && (css_omt_is(p, &t, "normal") || css_omt_is(p, &t, "allow-discrete"))) {
                    css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                    od_ts_setv(&s, base, &x);
                    continue;
                }
                if (!s.ln[base + 4] && t.t == OT_IDENT) {
                    vector<char> w;
                    css_omt_text(p, &t, &w);
                    if (od_wide(data(w), len(w))) return 0;
                    if (css_omx_ieq(data(w), len(w), "none") && n > 1) return 0;
                    css_omx_lower(data(w), len(w), &x);
                    od_ts_setv(&s, base + 4, &x);
                    continue;
                }
                return 0;
            }
        }
        static const char *const dflt[] = { "normal", "0s", "ease", "0s", "all" };
        static const char *const names[] = { "transition-behavior", "transition-duration", "transition-timing-function", "transition-delay", "transition-property" };
        for (int q = 0; q < 5; q++) {
            vector<char> list;
            for (int item = 0; item < n; item++) {
                if (item) append_range(list, ", ", 2);
                if (s.ln[item * 5 + q]) od_ts_out(&s, item * 5 + q, &list);
                else od_cat(&list, dflt[q]);
            }
            od_pushv(ls, txt, names[q], 0, &list);
        }
        return 1;
    }
    if (!strcmp(nm, "animation")) {
        /* each item's duration, timing, delay, iteration, direction, fill, play-state and name at
           item * 8 + its place */
        uint32_t st[15], en[15];
        int n = od_items(tk, a, b, st, en, 15);
        if (n <= 0) return 0;
        for (int item = 0; item < n; item++) {
            uint32_t ps[10], pe[10];
            int np = od_parts(tk, st[item], en[item], ps, pe, 10);
            if (np <= 0) return 0;
            int base = item * 8, times = 0;
            for (int k = 0; k < np; k++) {
                css_omt t = (*tk)[ps[k]];
                vector<char> x;
                if (t.t == OT_DIM && od_numeric(p, tk, ps[k], pe[k], OD_K_TIME, 0, &x)) {
                    if (times == 0) { if (x[0] == '-') return 0; od_ts_setv(&s, base, &x); }
                    else if (times == 1) od_ts_setv(&s, base + 2, &x);
                    else return 0;
                    times++;
                    continue;
                }
                clear(x);
                if (!s.ln[base + 1] && od_easing(p, tk, ps[k], pe[k], &x)) { od_ts_setv(&s, base + 1, &x); continue; }
                clear(x);
                if (!s.ln[base + 3] && ((t.t == OT_NUM && t.v >= 0) || (t.t == OT_IDENT && css_omt_is(p, &t, "infinite")))) {
                    css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                    od_ts_setv(&s, base + 3, &x);
                    continue;
                }
                static const char *const groups[] = { "normal reverse alternate alternate-reverse", "none forwards backwards both", "running paused" };
                int placed = 0;
                for (int g = 0; g < 3 && !placed; g++) {
                    if (s.ln[base + 4 + g] || t.t != OT_IDENT || !od_word_in(groups[g], p + t.s, t.e - t.s)) continue;
                    css_omx_tokens(p, tk, ps[k], pe[k], 1, &x);
                    od_ts_setv(&s, base + 4 + g, &x);
                    placed = 1;
                }
                if (placed) continue;
                if (!s.ln[base + 7] && (t.t == OT_IDENT || t.t == OT_STR)) {
                    vector<char> w;
                    css_omt_text(p, &t, &w);
                    if (t.t == OT_IDENT && od_wide(data(w), len(w))) return 0;
                    if (t.t == OT_STR) css_omx_string(data(w), len(w), &x);
                    else if (css_omx_ieq(data(w), len(w), "none")) od_cat(&x, "none");
                    else css_omx_ident(data(w), len(w), &x);
                    od_ts_setv(&s, base + 7, &x);
                    continue;
                }
                return 0;
            }
        }
        static const char *const dflt[] = { "auto", "ease", "0s", "1", "normal", "none", "running", "none" };
        static const char *const names[] = { "animation-duration", "animation-timing-function", "animation-delay", "animation-iteration-count",
                                             "animation-direction", "animation-fill-mode", "animation-play-state", "animation-name" };
        for (int q = 0; q < 8; q++) {
            vector<char> list;
            for (int item = 0; item < n; item++) {
                if (item) append_range(list, ", ", 2);
                if (s.ln[item * 8 + q]) od_ts_out(&s, item * 8 + q, &list);
                else od_cat(&list, dflt[q]);
            }
            od_pushv(ls, txt, names[q], 0, &list);
        }
        /* the timeline and its range, as many as the names */
        vector<char> tl, rs, re;
        for (int item = 0; item < n; item++) {
            if (item) { append_range(tl, ", ", 2); append_range(rs, ", ", 2); append_range(re, ", ", 2); }
            od_cat(&tl, "auto");
            od_cat(&rs, "normal");
            od_cat(&re, "normal");
        }
        od_pushv(ls, txt, "animation-timeline", CSS_OM_IMPLICIT, &tl);
        od_pushv(ls, txt, "animation-range-start", CSS_OM_IMPLICIT, &rs);
        od_pushv(ls, txt, "animation-range-end", CSS_OM_IMPLICIT, &re);
        return 1;
    }
    return -1;
}

/* ---- shorthands: writing (Chromium's StylePropertySerializer) ---- */

/* a longhand's declaration in dl read into the text set's item i (its flags beside it): 0 when dl
   has none */
static int od_get(const css_om_dl *dl, const char *name, od_ts *s, int i) {
    uint32_t k = od_find(dl, od_prop(name), "", 0);
    s->ln[i] = 0;
    s->fl[i] = 0;
    if (!k) return 0;
    css_om_decl d = dl->d[k - 1];
    od_ts_set(s, i, data(dl->t) + d.voff, d.vlen);
    s->fl[i] = d.flags;
    return 1;
}
/* four sides' values (items i to i + 3) as one to four (CSS Backgrounds 3's compaction) */
static void od_four_text(const od_ts *s, int i, vector<char> *out) {
    int tb = od_ts_eq(s, i, i + 2), rl = od_ts_eq(s, i + 1, i + 3);
    int all = tb && rl && od_ts_eq(s, i, i + 1);
    od_ts_out(s, i, out);
    if (all) return;
    push_back(*out, ' ');
    od_ts_out(s, i + 1, out);
    if (tb && rl) return;
    push_back(*out, ' ');
    od_ts_out(s, i + 2, out);
    if (rl) return;
    push_back(*out, ' ');
    od_ts_out(s, i + 3, out);
}
/* a list value's comma-separated items: their starts and lengths in its text, at most max */
static int od_split(const char *s, size_t n, uint32_t *st, uint32_t *ln, int max) {
    int k = 0, depth = 0;
    size_t b0 = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && s[i] == '(') depth++;
        else if (i < n && s[i] == ')') depth--;
        if (i == n || (s[i] == ',' && depth == 0)) {
            if (k >= max) return -1;
            size_t a = b0, e = i;
            while (a < e && s[a] == ' ') a++;
            while (e > a && s[e - 1] == ' ') e--;
            st[k] = (uint32_t)a;
            ln[k] = (uint32_t)(e - a);
            k++;
            b0 = i + 1;
        }
    }
    return k;
}
/* layered longhands' values (each a list) read into the text set, item q * layers + layer, as
   many layers as each has (0 when their counts differ) */
static int od_layers(const css_om_dl *dl, const char *const *names, int nn, od_ts *s, int maxl) {
    int layers = -1;
    for (int q = 0; q < nn; q++) {
        uint32_t k = od_find(dl, od_prop(names[q]), "", 0);
        if (!k) return 0;
        css_om_decl d = dl->d[k - 1];
        uint32_t st[16], ln[16];
        int c = od_split(data(dl->t) + d.voff, d.vlen, st, ln, 16);
        if (c <= 0 || c > maxl || (layers >= 0 && c != layers)) return 0;
        layers = c;
        for (int l = 0; l < c; l++) od_ts_set(s, q * maxl + l, data(dl->t) + d.voff + st[l], ln[l]);
    }
    return layers;
}

static int od_shorthand_text(const css_om_dl *dl, uint32_t sh, vector<char> *out) {
    uint32_t nl = css_om_longhands(sh);
    if (!nl || nl > 32) return 0;
    /* every longhand there, of one importance */
    int imp = -1, pending = 0, all_pending = 1, wide = 0, same_wide = 1;
    uint32_t k0 = 0;
    for (uint32_t q = 0; q < nl; q++) {
        uint32_t k = od_find(dl, css_om_longhand(sh, q), "", 0);
        if (!k) return 0;
        css_om_decl d = dl->d[k - 1];
        int i = (d.flags & CSS_OM_IMPORTANT) != 0;
        if (imp >= 0 && i != imp) return 0;
        imp = i;
        if (d.flags & CSS_OM_PENDING) { pending = 1; if (d.sh != sh) all_pending = 0; }
        else all_pending = 0;
        int w = !(d.flags & CSS_OM_IMPLICIT) && od_wide(data(dl->t) + d.voff, d.vlen);
        if (q == 0) { k0 = k; wide = w; }
        else {
            css_om_decl d0 = dl->d[k0 - 1];
            if (!css_omx_eq(data(dl->t) + d.voff, d.vlen, data(dl->t) + d0.voff, d0.vlen) || w != wide) same_wide = 0;
        }
        if (w && !wide) wide = 2;
    }
    css_om_decl first = dl->d[k0 - 1];
    if (pending) {
        if (!all_pending) return 0;
        append_range(*out, data(dl->t) + first.voff, first.vlen);
        return 1;
    }
    /* every longhand a CSS-wide initial, an implicit one among them (background's position of
       background: red): initial, as Chromium's CommonShorthandChecks finds them equivalent */
    int all_initial = 1;
    for (uint32_t q = 0; q < nl && all_initial; q++) {
        css_om_decl d = dl->d[od_find(dl, css_om_longhand(sh, q), "", 0) - 1];
        if (!css_omx_ieq(data(dl->t) + d.voff, d.vlen, "initial")) all_initial = 0;
    }
    if (all_initial) { od_cat(out, "initial"); return 1; }
    if (wide == 1) {
        if (!same_wide) return 0;
        append_range(*out, data(dl->t) + first.voff, first.vlen);
        return 1;
    }
    /* a CSS-wide keyword among others' values, or a value with var() in it (not yet a value of
       the shorthand's grammar): no shorthand */
    for (uint32_t q = 0; q < nl; q++) {
        uint32_t k = od_find(dl, css_om_longhand(sh, q), "", 0);
        css_om_decl d = dl->d[k - 1];
        if (!(d.flags & CSS_OM_IMPLICIT) && od_wide(data(dl->t) + d.voff, d.vlen)) return 0;
        const char *v = data(dl->t) + d.voff;
        for (uint32_t i = 0; i + 4 <= d.vlen; i++)
            if ((od_low(v[i]) == 'v' && od_low(v[i + 1]) == 'a' && od_low(v[i + 2]) == 'r' && v[i + 3] == '(') ||
                (od_low(v[i]) == 'e' && od_low(v[i + 1]) == 'n' && od_low(v[i + 2]) == 'v' && v[i + 3] == '('))
                return 0;
    }
    char nm[64];
    od_copyz(nm, sizeof nm, css_om_prop_name(sh));
    od_ts s;
    memset(&s, 0, sizeof s);
    defer(s) { vector_char_drop(&s.pool); }
    if (!strcmp(nm, "border-image")) {
        static const char *const names[] = { "border-image-source", "border-image-slice", "border-image-width", "border-image-outset", "border-image-repeat" };
        static const char *const init[] = { "none", "100%", "1", "0", "stretch" };
        int all_init = 1;
        for (int q = 0; q < 5; q++) {
            od_get(dl, names[q], &s, q);
            if (!od_ts_is(&s, q, init[q])) all_init = 0;
        }
        if (all_init) { od_cat(out, "none"); return 1; }
        od_ts_out(&s, 0, out);
        push_back(*out, ' ');
        od_ts_out(&s, 1, out);
        append_range(*out, " / ", 3);
        od_ts_out(&s, 2, out);
        append_range(*out, " / ", 3);
        od_ts_out(&s, 3, out);
        push_back(*out, ' ');
        od_ts_out(&s, 4, out);
        return 1;
    }
    if (!strcmp(nm, "font-variant")) {
        /* the longhands' values but normal, in Chromium's order; none when the ligatures' alone is none */
        static const char *const names[] = { "font-variant-ligatures", "font-variant-caps", "font-variant-alternates", "font-variant-numeric",
                                             "font-variant-east-asian", "font-variant-position", "font-variant-emoji" };
        int n = 0;
        for (int q = 0; q < 7; q++) od_get(dl, names[q], &s, q);
        if (od_ts_is(&s, 0, "none")) {
            for (int q = 1; q < 7; q++) if (!od_ts_is(&s, q, "normal")) return 0;
            od_cat(out, "none");
            return 1;
        }
        for (int q = 0; q < 7; q++) {
            if (od_ts_is(&s, q, "normal")) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        if (!n) od_cat(out, "normal");
        return 1;
    }
    if (!strcmp(nm, "font-synthesis")) {
        static const char *const names[] = { "font-synthesis-weight", "font-synthesis-style", "font-synthesis-small-caps" };
        static const char *const words[] = { "weight", "style", "small-caps" };
        int n = 0;
        for (int q = 0; q < 3; q++) {
            od_get(dl, names[q], &s, q);
            if (!od_ts_is(&s, q, "auto")) continue;
            if (n++) push_back(*out, ' ');
            od_cat(out, words[q]);
        }
        if (!n) od_cat(out, "none");
        return 1;
    }
    static const char *const four[][5] = {
        { "margin", "margin-top", "margin-right", "margin-bottom", "margin-left" },
        { "padding", "padding-top", "padding-right", "padding-bottom", "padding-left" },
        { "inset", "top", "right", "bottom", "left" },
        { "border-width", "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" },
        { "border-style", "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" },
        { "border-color", "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" },
        { "scroll-margin", "scroll-margin-top", "scroll-margin-right", "scroll-margin-bottom", "scroll-margin-left" },
        { "scroll-padding", "scroll-padding-top", "scroll-padding-right", "scroll-padding-bottom", "scroll-padding-left" }, { 0, 0, 0, 0, 0 } };
    for (int k = 0; four[k][0]; k++) {
        if (strcmp(nm, four[k][0])) continue;
        for (int q = 0; q < 4; q++) od_get(dl, four[k][q + 1], &s, q);
        od_four_text(&s, 0, out);
        return 1;
    }
    static const char *const pairs[][3] = {
        { "margin-block", "margin-block-start", "margin-block-end" }, { "margin-inline", "margin-inline-start", "margin-inline-end" },
        { "padding-block", "padding-block-start", "padding-block-end" }, { "padding-inline", "padding-inline-start", "padding-inline-end" },
        { "inset-block", "inset-block-start", "inset-block-end" }, { "inset-inline", "inset-inline-start", "inset-inline-end" },
        { "scroll-margin-block", "scroll-margin-block-start", "scroll-margin-block-end" },
        { "scroll-margin-inline", "scroll-margin-inline-start", "scroll-margin-inline-end" },
        { "scroll-padding-block", "scroll-padding-block-start", "scroll-padding-block-end" },
        { "scroll-padding-inline", "scroll-padding-inline-start", "scroll-padding-inline-end" },
        { "border-block-width", "border-block-start-width", "border-block-end-width" },
        { "border-block-style", "border-block-start-style", "border-block-end-style" },
        { "border-block-color", "border-block-start-color", "border-block-end-color" },
        { "border-inline-width", "border-inline-start-width", "border-inline-end-width" },
        { "border-inline-style", "border-inline-start-style", "border-inline-end-style" },
        { "border-inline-color", "border-inline-start-color", "border-inline-end-color" },
        { "overflow", "overflow-x", "overflow-y" }, { "gap", "row-gap", "column-gap" },
        { "place-content", "align-content", "justify-content" }, { "place-items", "align-items", "justify-items" },
        { "place-self", "align-self", "justify-self" }, { "overscroll-behavior", "overscroll-behavior-x", "overscroll-behavior-y" },
        { "contain-intrinsic-size", "contain-intrinsic-width", "contain-intrinsic-height" },
        { "border-spacing", "-webkit-border-horizontal-spacing", "-webkit-border-vertical-spacing" }, { 0, 0, 0 } };
    for (int k = 0; pairs[k][0]; k++) {
        if (strcmp(nm, pairs[k][0])) continue;
        od_get(dl, pairs[k][1], &s, 0);
        od_get(dl, pairs[k][2], &s, 1);
        od_ts_out(&s, 0, out);
        if (!od_ts_eq(&s, 0, 1)) { push_back(*out, ' '); od_ts_out(&s, 1, out); }
        return 1;
    }
    if (!strcmp(nm, "border-radius")) {
        /* items 0-3 the corners' horizontal radii, 4-7 their vertical ones */
        static const char *const corners[] = { "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius" };
        int differ = 0;
        for (int q = 0; q < 4; q++) {
            od_get(dl, corners[q], &s, 8 + q);
            const char *v = data(s.pool) + s.off[8 + q];
            uint32_t n = s.ln[8 + q], sp = 0;
            while (sp < n && v[sp] != ' ') sp++;
            vector<char> h, w;
            append_range(h, v, sp);
            if (sp < n) append_range(w, v + sp + 1, n - sp - 1);
            else append_range(w, v, sp);
            od_ts_setv(&s, q, &h);
            od_ts_setv(&s, 4 + q, &w);
            if (!od_ts_eq(&s, q, 4 + q)) differ = 1;
        }
        od_four_text(&s, 0, out);
        if (differ) { append_range(*out, " / ", 3); od_four_text(&s, 4, out); }
        return 1;
    }
    static const char *const sides[][4] = { { "border-top", "border-top-width", "border-top-style", "border-top-color" },
                                            { "border-right", "border-right-width", "border-right-style", "border-right-color" },
                                            { "border-bottom", "border-bottom-width", "border-bottom-style", "border-bottom-color" },
                                            { "border-left", "border-left-width", "border-left-style", "border-left-color" },
                                            { "border-block-start", "border-block-start-width", "border-block-start-style", "border-block-start-color" },
                                            { "border-block-end", "border-block-end-width", "border-block-end-style", "border-block-end-color" },
                                            { "border-inline-start", "border-inline-start-width", "border-inline-start-style", "border-inline-start-color" },
                                            { "border-inline-end", "border-inline-end-width", "border-inline-end-style", "border-inline-end-color" }, { 0, 0, 0, 0 } };
    static const char *const side_init[] = { "medium", "none", "currentcolor" };
    for (int k = 0; sides[k][0]; k++) {
        if (strcmp(nm, sides[k][0])) continue;
        int n = 0;
        for (int q = 0; q < 3; q++) {
            od_get(dl, sides[k][q + 1], &s, q);
            if (od_ts_is(&s, q, side_init[q]) || ((s.fl[q] & CSS_OM_IMPLICIT) && od_ts_is(&s, q, "initial"))) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        return n > 0;
    }
    if (!strcmp(nm, "column-rule")) {
        static const char *const names[] = { "column-rule-width", "column-rule-style", "column-rule-color" };
        int n = 0;
        for (int q = 0; q < 3; q++) {
            od_get(dl, names[q], &s, q);
            if (od_ts_is(&s, q, side_init[q])) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        if (!n) od_ts_out(&s, 0, out);
        return 1;
    }
    if (!strcmp(nm, "border-block") || !strcmp(nm, "border-inline")) {
        const char *pre = nm[7] == 'b' ? "border-block-" : "border-inline-";
        static const char *const what[] = { "width", "style", "color" };
        int n = 0;
        for (int q = 0; q < 3; q++) {
            char n1[48], n2[48];
            snprintf(n1, sizeof n1, "%sstart-%s", pre, what[q]);
            snprintf(n2, sizeof n2, "%send-%s", pre, what[q]);
            od_get(dl, n1, &s, q);
            od_get(dl, n2, &s, 3 + q);
            if (!od_ts_eq(&s, q, 3 + q)) return 0;
            if (od_ts_is(&s, q, side_init[q])) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        return n > 0;
    }
    if (!strcmp(nm, "border")) {
        static const char *const sets[3][4] = { { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" },
                                                { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" },
                                                { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" } };
        for (int g = 0; g < 3; g++)
            for (int q = 0; q < 4; q++) {
                od_get(dl, sets[g][q], &s, g * 4 + q);
                if (q && !od_ts_eq(&s, g * 4 + q, g * 4)) return 0;
            }
        static const char *const img[][2] = { { "border-image-source", "none" }, { "border-image-slice", "100%" }, { "border-image-width", "1" },
                                              { "border-image-outset", "0" }, { "border-image-repeat", "stretch" } };
        for (int q = 0; q < 5; q++) {
            od_get(dl, img[q][0], &s, 12 + q);
            if (!od_ts_is(&s, 12 + q, img[q][1]) && !od_ts_is(&s, 12 + q, "initial")) return 0;
        }
        int n = 0;
        for (int g = 0; g < 3; g++) {
            if (od_ts_is(&s, g * 4, side_init[g])) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, g * 4, out);
        }
        return n > 0;
    }
    if (!strcmp(nm, "outline") || !strcmp(nm, "list-style") || !strcmp(nm, "text-wrap") || !strcmp(nm, "text-emphasis")) {
        /* the longhands in the shorthand's order, the implicit ones left out */
        int n = 0;
        for (uint32_t q = 0; q < nl; q++) {
            uint32_t k = od_find(dl, css_om_longhand(sh, q), "", 0);
            css_om_decl d = dl->d[k - 1];
            if (d.flags & CSS_OM_IMPLICIT) continue;
            if (n++) push_back(*out, ' ');
            append_range(*out, data(dl->t) + d.voff, d.vlen);
        }
        return n > 0;
    }
    if (!strcmp(nm, "text-decoration")) {
        static const char *const names[] = { "text-decoration-line", "text-decoration-thickness", "text-decoration-style", "text-decoration-color" };
        static const char *const init[] = { "none", "auto", "solid", "currentcolor" };
        int n = 0;
        for (int q = 0; q < 4; q++) {
            od_get(dl, names[q], &s, q);
            if ((s.fl[q] & CSS_OM_IMPLICIT) || od_ts_is(&s, q, init[q])) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        if (!n) od_cat(out, "none");
        return 1;
    }
    if (!strcmp(nm, "flex")) {
        od_get(dl, "flex-grow", &s, 0);
        od_get(dl, "flex-shrink", &s, 1);
        od_get(dl, "flex-basis", &s, 2);
        for (int q = 0; q < 3; q++) { if (q) push_back(*out, ' '); od_ts_out(&s, q, out); }
        return 1;
    }
    if (!strcmp(nm, "flex-flow")) {
        od_get(dl, "flex-direction", &s, 0);
        od_get(dl, "flex-wrap", &s, 1);
        if (od_ts_is(&s, 1, "nowrap")) od_ts_out(&s, 0, out);
        else if (od_ts_is(&s, 0, "row")) od_ts_out(&s, 1, out);
        else { od_ts_out(&s, 0, out); push_back(*out, ' '); od_ts_out(&s, 1, out); }
        return 1;
    }
    if (!strcmp(nm, "white-space")) {
        od_get(dl, "white-space-collapse", &s, 0);
        od_get(dl, "text-wrap-mode", &s, 1);
        static const char *const kw[][3] = { { "normal", "collapse", "wrap" }, { "pre", "preserve", "nowrap" }, { "nowrap", "collapse", "nowrap" },
                                             { "pre-wrap", "preserve", "wrap" }, { "break-spaces", "break-spaces", "wrap" },
                                             { "pre-line", "preserve-breaks", "wrap" }, { 0, 0, 0 } };
        for (int q = 0; kw[q][0]; q++) if (od_ts_is(&s, 0, kw[q][1]) && od_ts_is(&s, 1, kw[q][2])) { od_cat(out, kw[q][0]); return 1; }
        int n = 0;
        if (!od_ts_is(&s, 0, "collapse")) { od_ts_out(&s, 0, out); n++; }
        if (!od_ts_is(&s, 1, "wrap")) { if (n++) push_back(*out, ' '); od_ts_out(&s, 1, out); }
        return n > 0;
    }
    if (!strcmp(nm, "container")) {
        od_get(dl, "container-name", &s, 0);
        od_get(dl, "container-type", &s, 1);
        od_ts_out(&s, 0, out);
        if (!od_ts_is(&s, 1, "normal")) { append_range(*out, " / ", 3); od_ts_out(&s, 1, out); }
        return 1;
    }
    if (!strcmp(nm, "columns")) {
        od_get(dl, "column-width", &s, 0);
        od_get(dl, "column-count", &s, 1);
        od_get(dl, "column-height", &s, 2);
        od_get(dl, "column-wrap", &s, 3);
        if (!od_ts_is(&s, 2, "auto") || !od_ts_is(&s, 3, "auto")) return 0;
        int n = 0;
        if (!od_ts_is(&s, 0, "auto")) { od_ts_out(&s, 0, out); n++; }
        if (!od_ts_is(&s, 1, "auto")) { if (n++) push_back(*out, ' '); od_ts_out(&s, 1, out); }
        if (!n) od_cat(out, "auto");
        return 1;
    }
    if (!strcmp(nm, "grid-row") || !strcmp(nm, "grid-column")) {
        int row = nm[5] == 'r';
        od_get(dl, row ? "grid-row-start" : "grid-column-start", &s, 0);
        od_get(dl, row ? "grid-row-end" : "grid-column-end", &s, 1);
        vector<char> d;
        od_line_default_ts(&s, 0, &d);
        od_ts_out(&s, 0, out);
        if (!css_omx_eq(data(s.pool) + s.off[1], s.ln[1], data(d), len(d))) { append_range(*out, " / ", 3); od_ts_out(&s, 1, out); }
        return 1;
    }
    if (!strcmp(nm, "grid-area")) {
        od_get(dl, "grid-row-start", &s, 0);
        od_get(dl, "grid-column-start", &s, 1);
        od_get(dl, "grid-row-end", &s, 2);
        od_get(dl, "grid-column-end", &s, 3);
        vector<char> d1, d3;
        od_line_default_ts(&s, 0, &d1);
        od_line_default_ts(&s, 1, &d3);
        int n = 4;
        if (css_omx_eq(data(s.pool) + s.off[3], s.ln[3], data(d3), len(d3))) {
            n = 3;
            if (css_omx_eq(data(s.pool) + s.off[2], s.ln[2], data(d1), len(d1))) {
                n = 2;
                if (css_omx_eq(data(s.pool) + s.off[1], s.ln[1], data(d1), len(d1))) n = 1;
            }
        }
        for (int q = 0; q < n; q++) { if (q) append_range(*out, " / ", 3); od_ts_out(&s, q, out); }
        return 1;
    }
    if (!strcmp(nm, "marker")) {
        od_get(dl, "marker-start", &s, 0);
        od_get(dl, "marker-mid", &s, 1);
        od_get(dl, "marker-end", &s, 2);
        if (!od_ts_eq(&s, 0, 1) || !od_ts_eq(&s, 0, 2)) return 0;
        od_ts_out(&s, 0, out);
        return 1;
    }
    if (!strcmp(nm, "background") || !strcmp(nm, "background-position")) {
        int pos_only = nm[10] == '-';
        static const char *const names[] = { "background-image", "background-position-x", "background-position-y", "background-size", "background-repeat",
                                             "background-attachment", "background-origin", "background-clip" };
        /* each longhand's layers at q * 15 + layer */
        int layers = pos_only ? od_layers(dl, names + 1, 2, &s, 15) : od_layers(dl, names, 8, &s, 15);
        if (layers <= 0) return 0;
        for (int layer = 0; layer < layers; layer++) {
            if (layer) append_range(*out, ", ", 2);
            int n = 0;
            if (pos_only) {
                od_ts_out(&s, layer, out);
                push_back(*out, ' ');
                od_ts_out(&s, 15 + layer, out);
                continue;
            }
            for (int q = 0; q < 8; q++) {
                int i = q * 15 + layer;
                int implicit = od_ts_is(&s, i, "initial");
                if (q == 1 && implicit && !od_ts_is(&s, 3 * 15 + layer, "initial")) return 0;
                if (implicit) continue;
                if (q == 3) { append_range(*out, " / ", 3); od_ts_out(&s, i, out); continue; }
                if (n++) push_back(*out, ' ');
                od_ts_out(&s, i, out);
            }
            if (layer == layers - 1) {
                od_get(dl, "background-color", &s, 120);
                if (!od_ts_is(&s, 120, "initial")) { if (n++) push_back(*out, ' '); od_ts_out(&s, 120, out); }
            }
            if (!n) return 0;
        }
        return 1;
    }
    if (!strcmp(nm, "font")) {
        static const char *const resets[][2] = { { "font-variant-ligatures", "normal" }, { "font-variant-numeric", "normal" },
                                                 { "font-variant-east-asian", "normal" }, { "font-variant-alternates", "normal" },
                                                 { "font-size-adjust", "none" }, { "font-language-override", "normal" }, { "font-kerning", "auto" },
                                                 { "font-optical-sizing", "auto" }, { "font-feature-settings", "normal" },
                                                 { "font-variation-settings", "normal" }, { "font-variant-position", "normal" },
                                                 { "font-variant-emoji", "normal" }, { 0, 0 } };
        for (int q = 0; resets[q][0]; q++) {
            od_get(dl, resets[q][0], &s, 20);
            if (!od_ts_is(&s, 20, resets[q][1])) return 0;
        }
        static const char *const names[] = { "font-style", "font-variant-caps", "font-weight", "font-stretch", "font-size", "line-height", "font-family" };
        for (int q = 0; q < 7; q++) od_get(dl, names[q], &s, q);
        if (!od_ts_is(&s, 1, "normal") && !od_ts_is(&s, 1, "small-caps")) return 0;
        if (!od_ts_is(&s, 3, "normal") && !od_keyword(data(s.pool) + s.off[3], s.ln[3])) return 0;
        int n = 0;
        for (int q = 0; q < 4; q++) {
            if (od_ts_is(&s, q, "normal")) continue;
            if (n++) push_back(*out, ' ');
            od_ts_out(&s, q, out);
        }
        if (n++) push_back(*out, ' ');
        od_ts_out(&s, 4, out);
        if (!od_ts_is(&s, 5, "normal")) { append_range(*out, " / ", 3); od_ts_out(&s, 5, out); }
        push_back(*out, ' ');
        od_ts_out(&s, 6, out);
        return 1;
    }
    if (!strcmp(nm, "transition")) {
        static const char *const names[] = { "transition-property", "transition-duration", "transition-timing-function", "transition-delay", "transition-behavior" };
        static const char *const init[] = { "all", "0s", "ease", "0s", "normal" };
        int layers = od_layers(dl, names, 5, &s, 25);
        if (layers <= 0) return 0;
        for (int item = 0; item < layers; item++) {
            if (item) append_range(*out, ", ", 2);
            int n = 0;
            for (int q = 0; q < 5; q++) {
                if (od_ts_is(&s, q * 25 + item, init[q])) continue;
                if (n++) push_back(*out, ' ');
                od_ts_out(&s, q * 25 + item, out);
            }
            if (!n) od_cat(out, "all");
        }
        return 1;
    }
    if (!strcmp(nm, "animation")) {
        static const char *const names[] = { "animation-duration", "animation-timing-function", "animation-delay", "animation-iteration-count",
                                             "animation-direction", "animation-fill-mode", "animation-play-state", "animation-name",
                                             "animation-timeline", "animation-range-start", "animation-range-end" };
        int layers = od_layers(dl, names, 11, &s, 11);
        if (layers <= 0) return 0;
        for (int item = 0; item < layers; item++) {
            if (!od_ts_is(&s, 8 * 11 + item, "auto") || !od_ts_is(&s, 9 * 11 + item, "normal") || !od_ts_is(&s, 10 * 11 + item, "normal")) return 0;
        }
        for (int item = 0; item < layers; item++) {
            if (item) append_range(*out, ", ", 2);
            for (int q = 0; q < 8; q++) {
                if (q) push_back(*out, ' ');
                od_ts_out(&s, q * 11 + item, out);
            }
        }
        return 1;
    }
    return 0;
}

/* ---- a list's text (Chromium's StylePropertySerializer::AsText) ---- */

/* the shorthands a longhand is in, the most longhands first (Chromium's
   getMatchingShorthandsForLonghand), made once */
static uint16_t od_sh_of[CSS_OM_NPROPS][8];
static uint8_t od_sh_n[CSS_OM_NPROPS];
static int od_sh_made;
static void od_sh_make(void) {
    od_sh_made = 1;
    for (uint32_t s = 0; s < CSS_OM_NPROPS; s++) {
        if (css_om_pdefs[s].kind != 1 || css_om_pdefs[s].n < 2) continue;
        for (uint32_t q = 0; q < css_om_pdefs[s].n; q++) {
            uint32_t l = css_om_longs[css_om_pdefs[s].first + q];
            if (l >= CSS_OM_NPROPS || od_sh_n[l] >= 8) continue;
            /* placed by its longhands' count, the most first */
            uint8_t n = od_sh_n[l];
            uint8_t at = n;
            while (at > 0 && css_om_pdefs[od_sh_of[l][at - 1]].n < css_om_pdefs[s].n) at--;
            for (uint8_t z = n; z > at; z--) od_sh_of[l][z] = od_sh_of[l][z - 1];
            od_sh_of[l][at] = (uint16_t)s;
            od_sh_n[l] = (uint8_t)(n + 1);
        }
    }
}
static void od_decl_text(vector<char> *out, const char *name, const char *v, size_t vn, int important) {
    if (len(*out)) push_back(*out, ' ');
    od_cat(out, name);
    append_range(*out, ": ", 2);
    append_range(*out, v, vn);
    if (important) od_cat(out, " !important");
    push_back(*out, ';');
}
/* the properties a list's text has written, and the shorthands it has tried, by the text's turn
   (od_turn): a property is done when its entry is the turn's, so no table is cleared a text */
static uint32_t od_done_at[CSS_OM_NPROPS];
static uint32_t od_tried_at[CSS_OM_NPROPS];
static uint32_t od_turn;
void css_om_decls_text(const css_om_dl *dl, uint32_t kind, vector<char> *out) {
    if (!od_sh_made) od_sh_make();
    size_t n = len(dl->d);
    if (++od_turn == 0) {
        memset(od_done_at, 0, sizeof od_done_at);
        memset(od_tried_at, 0, sizeof od_tried_at);
        od_turn = 1;
    }
    uint32_t turn = od_turn;
    vector<char> res;
    reserve(res, len(dl->t) + 16 * n + 16);
    if (kind == CSS_OM_B_PROPERTY || kind == CSS_OM_B_COUNTER) {
        /* @property's and @counter-style's descriptors in their own order */
        uint32_t base;
        const char *const *list = od_desc_list(kind, &base);
        for (int k = 0; list && list[k]; k++) {
            uint32_t q = od_find(dl, CSS_OM_NPROPS + base + (uint32_t)k, "", 0);
            if (!q) continue;
            css_om_decl d = dl->d[q - 1];
            od_decl_text(&res, list[k], data(dl->t) + d.voff, d.vlen, (d.flags & CSS_OM_IMPORTANT) != 0);
        }
        append_range(*out, data(res), len(res));
        return;
    }
    for (size_t i = 0; i < n; i++) {
        css_om_decl d = dl->d[i];
        int imp = (d.flags & CSS_OM_IMPORTANT) != 0;
        if (d.prop == CSS_OM_CUSTOM) {
            vector<char> nm;
            append_range(nm, data(dl->t) + d.noff, d.nlen);
            push_back(nm, 0);
            od_decl_text(&res, data(nm), data(dl->t) + d.voff, d.vlen, imp);
            continue;
        }
        if (d.prop >= CSS_OM_NPROPS) {
            od_decl_text(&res, od_desc_name(d.prop), data(dl->t) + d.voff, d.vlen, imp);
            continue;
        }
        if (od_done_at[d.prop] == turn) continue;
        int as_sh = 0;
        for (uint8_t s = 0; s < od_sh_n[d.prop] && !as_sh; s++) {
            uint32_t sh = od_sh_of[d.prop][s];
            if (od_tried_at[sh] == turn) continue;
            od_tried_at[sh] = turn;
            int other = 0;
            for (uint32_t q = 0; q < css_om_pdefs[sh].n && !other; q++) if (od_done_at[css_om_longs[css_om_pdefs[sh].first + q]] == turn) other = 1;
            if (other) continue;
            vector<char> v;
            if (!od_shorthand_text(dl, sh, &v) || !len(v)) continue;
            od_decl_text(&res, css_om_pdefs[sh].name, data(v), len(v), imp);
            for (uint32_t q = 0; q < css_om_pdefs[sh].n; q++) od_done_at[css_om_longs[css_om_pdefs[sh].first + q]] = turn;
            as_sh = 1;
        }
        if (as_sh) continue;
        od_done_at[d.prop] = turn;
        od_decl_text(&res, css_om_pdefs[d.prop].name, (d.flags & CSS_OM_PENDING) ? "" : data(dl->t) + d.voff, (d.flags & CSS_OM_PENDING) ? 0 : d.vlen, imp);
    }
    append_range(*out, data(res), len(res));
}
