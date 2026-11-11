/* src/css/css.c -- the CSS module (contracts/css.pl): style sheets parsed,
   selectors matched against the DOM, the cascade run, and each element's
   computed values kept with where they came from; and the module's generated
   implementation.

   Parsing has CSS Syntax 3's shapes: comments, strings, the top level's <!-- and
   -->, at-rules (an @media whose query holds for the viewport is read, every
   other at-rule skipped), style rules, declarations with !important. A
   shorthand is expanded into the module's properties (margin, padding,
   border-width, border and its sides, border-color, font, background,
   list-style, text-decoration). A value a property does not take drops its
   declaration, as CSS has it.

   Selectors 4: type, universal, #id, .class, [a], [a=v], [a~=v], [a|=v],
   [a^=v], [a$=v], [a*=v] (and their i flag), :first-child, :last-child,
   :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(),
   :nth-last-child(), :nth-of-type(), :nth-last-of-type(), :not(), :is(),
   :where(), :root, :empty, :link, :any-link, :checked, :disabled, :enabled;
   the combinators ' ', >, + and ~. A pseudo-class of state (:hover, :focus,
   :visited, ...) and a pseudo-element never match: a page is laid out at rest.
   :not(), :is() and :where() nest at most 16 deep, and one match tests at most
   4096 compounds: past either, the selector matches nothing.

   The cascade (Cascade 4): origin and importance, element-attached style over
   rules, specificity, order of appearance; then inheritance and initial
   values. Presentational hints (HTML 15.3: bgcolor, text, color, size, width,
   height, align, a table's border) are author rules of specificity 0 before
   every other author rule. Lengths compute to px: em against the font size
   (the parent's for font-size), rem against 16px, the viewport's units
   against 800 x 600; percentages stay percentages for layout.

   What is simplified: border-style is no property of its own -- the border
   shorthands' style sets their widths to 0 when it is none or missing, and a
   border-width written alone is taken at its word; one border colour serves
   the four sides. The DOM is read through its fields, never changed. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_implementation(css);

#define CSS_NPROPS 29
#define CSS_MAXPARTS 32
#define CSS_VIEWPORT_W 800.0
#define CSS_VIEWPORT_H 600.0
/* a selector's functional pseudo-classes nest at most so deep, and one match of an
   element against a selector tests at most so many compounds: a selector made to
   recurse or to backtrack without end matches nothing, and costs no more */
#define CSS_MAX_NESTING 16
#define CSS_MATCH_BUDGET 4096

/* ---- the user agent's style sheet: the HTML Standard's rendering section (15.3), abridged ---- */
static const char css_ua_sheet[] =
    "area, base, basefont, datalist, head, link, meta, noembed, noframes, param, rp, script, style, template, title { display: none }\n"
    "[hidden] { display: none }\n"
    "html, body { display: block }\n"
    "body { margin: 8px }\n"
    "address, blockquote, center, dialog, div, figure, figcaption, footer, form, header, hr, legend, listing, main, p, plaintext, pre, search, xmp { display: block }\n"
    "blockquote, figure, listing, p, plaintext, pre, xmp { margin-top: 1em; margin-bottom: 1em }\n"
    "blockquote, figure { margin-left: 40px; margin-right: 40px }\n"
    "address { font-style: italic }\n"
    "listing, plaintext, pre, xmp { white-space: pre }\n"
    "code, kbd, listing, plaintext, pre, samp, tt, xmp { font-family: monospace }\n"
    "center { text-align: center }\n"
    "cite, dfn, em, i, var { font-style: italic }\n"
    "b, strong { font-weight: bolder }\n"
    "small { font-size: smaller }\n"
    "big { font-size: larger }\n"
    "s, strike, del { text-decoration: line-through }\n"
    "u, ins { text-decoration: underline }\n"
    "mark { background-color: yellow; color: black }\n"
    "nobr { white-space: nowrap }\n"
    ":link { color: #0000ee; text-decoration: underline }\n"
    "article, aside, h1, h2, h3, h4, h5, h6, hgroup, nav, section { display: block }\n"
    "h1 { margin-top: 0.67em; margin-bottom: 0.67em; font-size: 2em }\n"
    "h2 { margin-top: 0.83em; margin-bottom: 0.83em; font-size: 1.5em }\n"
    "h3 { margin-top: 1em; margin-bottom: 1em; font-size: 1.17em }\n"
    "h4 { margin-top: 1.33em; margin-bottom: 1.33em; font-size: 1em }\n"
    "h5 { margin-top: 1.67em; margin-bottom: 1.67em; font-size: 0.83em }\n"
    "h6 { margin-top: 2.33em; margin-bottom: 2.33em; font-size: 0.67em }\n"
    "h1, h2, h3, h4, h5, h6 { font-weight: bold }\n"
    "dir, dd, dl, dt, menu, ol, ul { display: block }\n"
    "li { display: list-item }\n"
    "dir, dl, menu, ol, ul { margin-top: 1em; margin-bottom: 1em }\n"
    ":is(dir, dl, menu, ol, ul) :is(dir, dl, menu, ol, ul) { margin-top: 0; margin-bottom: 0 }\n"
    "dd { margin-left: 40px }\n"
    "dir, menu, ol, ul { padding-left: 40px }\n"
    "ol { list-style-type: decimal }\n"
    "dir, menu, ul { list-style-type: disc }\n"
    ":is(dir, menu, ol, ul) :is(dir, menu, ul) { list-style-type: circle }\n"
    ":is(dir, menu, ol, ul) :is(dir, menu, ol, ul) :is(dir, menu, ul) { list-style-type: square }\n"
    "table { display: table }\n"
    "caption { display: table-caption; text-align: center }\n"
    "colgroup { display: table-column-group }\n"
    "col { display: table-column }\n"
    "thead { display: table-header-group }\n"
    "tbody { display: table-row-group }\n"
    "tfoot { display: table-footer-group }\n"
    "tr { display: table-row }\n"
    "td, th { display: table-cell; padding: 1px }\n"
    "th { font-weight: bold; text-align: center }\n"
    "hr { color: gray; border-width: 1px; margin: 0.5em auto }\n"
    "fieldset { margin-left: 2px; margin-right: 2px; padding: 0.35em 0.75em 0.625em; border: 2px groove }\n"
    "details, summary { display: block }\n"
    "button, input, select, textarea { display: inline-block }\n"
    "button { padding: 1px 6px; border: 2px solid #767676; background-color: #efefef }\n"
    "input, textarea, select { padding: 1px 2px; border: 2px solid #767676; background-color: white }\n";

/* ---- reading text ---- */

static int css_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char css_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int css_namechar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (unsigned char)c >= 0x80;
}
static size_t css_skip_ws(const char *s, size_t n, size_t i) { while (i < n && css_ws(s[i])) i++; return i; }
static int css_eq_ci(const char *s, size_t n, const char *w) {
    size_t k = strlen(w);
    if (n != k) return 0;
    for (size_t i = 0; i < k; i++) if (css_lower(s[i]) != w[i]) return 0;
    return 1;
}
static int css_bytes_eq(const char *a, const char *b, size_t n, int ci) {
    for (size_t i = 0; i < n; i++) if (ci ? css_lower(a[i]) != css_lower(b[i]) : a[i] != b[i]) return 0;
    return 1;
}
/* the word w is one of the list l's words, which one space separates */
static int css_word_in(const char *w, const char *l) {
    size_t wn = strlen(w), n = strlen(l), i = 0;
    while (i < n) {
        size_t b = i;
        while (i < n && l[i] != ' ') i++;
        if (i - b == wn && memcmp(l + b, w, wn) == 0) return 1;
        i++;
    }
    return 0;
}
/* the closing quote of the string that begins at i, or n */
static size_t css_string_end(const char *s, size_t n, size_t i) {
    char q = s[i];
    i++;
    while (i < n && s[i] != q) { if (s[i] == '\\') i++; i++; }
    return i < n ? i : n;
}
/* the close that matches the open at i, past strings and nesting; or n */
static size_t css_block_end(const char *s, size_t n, size_t i, char open, char close) {
    int depth = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\'') { i = css_string_end(s, n, i); if (i >= n) return n; continue; }
        if (c == open) depth++;
        else if (c == close) { depth--; if (depth == 0) return i; }
    }
    return n;
}
/* the next comma at the top level of s from i, or n */
static size_t css_comma(const char *s, size_t n, size_t i) {
    for (; i < n; i++) {
        char c = s[i];
        if (c == ',') return i;
        if (c == '"' || c == '\'') i = css_string_end(s, n, i);
        else if (c == '(') i = css_block_end(s, n, i, '(', ')');
        else if (c == '[') i = css_block_end(s, n, i, '[', ']');
        if (i >= n) return n;
    }
    return n;
}
/* the text without its comments; strings kept as they are */
static void css_strip_comments(const char *s, size_t n, vector<char> *out) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
            push_back(out, ' ');
            continue;
        }
        if (s[i] == '"' || s[i] == '\'') {
            size_t e = css_string_end(s, n, i);
            size_t stop = e < n ? e + 1 : n;
            append_range(out, s + i, stop - i);
            i = stop;
            continue;
        }
        push_back(out, s[i]);
        i++;
    }
}
/* a value's parts, which white space separates (a function's parentheses kept whole) */
static size_t css_parts(const char *s, size_t n, size_t *st, size_t *ln, size_t max) {
    size_t k = 0, i = 0;
    for (;;) {
        i = css_skip_ws(s, n, i);
        if (i >= n || k == max) break;
        size_t b = i;
        while (i < n && !css_ws(s[i])) {
            if (s[i] == '(') i = css_block_end(s, n, i, '(', ')');
            else if (s[i] == '"' || s[i] == '\'') i = css_string_end(s, n, i);
            if (i < n) i++;
        }
        st[k] = b;
        ln[k] = i - b;
        k++;
    }
    return k;
}

/* ---- values ---- */

typedef struct css_val { css_unit unit; double num; uint32_t color; char word[32]; } css_val;

static void css_word_val(css_val *v, const char *w) {
    memset(v, 0, sizeof *v);
    v->unit = CSS_UNIT_KEYWORD;
    snprintf(v->word, sizeof v->word, "%s", w);
}
static int css_is_word(const css_val *v, const char *w) { return v->unit == CSS_UNIT_KEYWORD && strcmp(v->word, w) == 0; }
static int css_is_global(const css_val *v) {
    return v->unit == CSS_UNIT_KEYWORD && css_word_in(v->word, "inherit initial unset revert");
}

/* a number: its bytes, 0 when there is none */
static size_t css_number(const char *s, size_t n, double *out) {
    size_t i = 0;
    int neg = 0, any = 0;
    double x = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    while (i < n && s[i] >= '0' && s[i] <= '9') { x = x * 10 + (s[i] - '0'); i++; any = 1; }
    if (i + 1 < n && s[i] == '.' && s[i + 1] >= '0' && s[i + 1] <= '9') {
        double f = 0.1;
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { x += (s[i] - '0') * f; f /= 10; i++; any = 1; }
    }
    if (!any) return 0;
    *out = neg ? -x : x;
    return i;
}

/* the named colours of CSS Color 4 */
static const struct css_named { const char *name; uint32_t rgb; } css_colors[] = {
    { "aliceblue", 0xf0f8ff }, { "antiquewhite", 0xfaebd7 }, { "aqua", 0x00ffff }, { "aquamarine", 0x7fffd4 },
    { "azure", 0xf0ffff }, { "beige", 0xf5f5dc }, { "bisque", 0xffe4c4 }, { "black", 0x000000 },
    { "blanchedalmond", 0xffebcd }, { "blue", 0x0000ff }, { "blueviolet", 0x8a2be2 }, { "brown", 0xa52a2a },
    { "burlywood", 0xdeb887 }, { "cadetblue", 0x5f9ea0 }, { "chartreuse", 0x7fff00 }, { "chocolate", 0xd2691e },
    { "coral", 0xff7f50 }, { "cornflowerblue", 0x6495ed }, { "cornsilk", 0xfff8dc }, { "crimson", 0xdc143c },
    { "cyan", 0x00ffff }, { "darkblue", 0x00008b }, { "darkcyan", 0x008b8b }, { "darkgoldenrod", 0xb8860b },
    { "darkgray", 0xa9a9a9 }, { "darkgreen", 0x006400 }, { "darkgrey", 0xa9a9a9 }, { "darkkhaki", 0xbdb76b },
    { "darkmagenta", 0x8b008b }, { "darkolivegreen", 0x556b2f }, { "darkorange", 0xff8c00 }, { "darkorchid", 0x9932cc },
    { "darkred", 0x8b0000 }, { "darksalmon", 0xe9967a }, { "darkseagreen", 0x8fbc8f }, { "darkslateblue", 0x483d8b },
    { "darkslategray", 0x2f4f4f }, { "darkslategrey", 0x2f4f4f }, { "darkturquoise", 0x00ced1 }, { "darkviolet", 0x9400d3 },
    { "deeppink", 0xff1493 }, { "deepskyblue", 0x00bfff }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 },
    { "dodgerblue", 0x1e90ff }, { "firebrick", 0xb22222 }, { "floralwhite", 0xfffaf0 }, { "forestgreen", 0x228b22 },
    { "fuchsia", 0xff00ff }, { "gainsboro", 0xdcdcdc }, { "ghostwhite", 0xf8f8ff }, { "gold", 0xffd700 },
    { "goldenrod", 0xdaa520 }, { "gray", 0x808080 }, { "green", 0x008000 }, { "greenyellow", 0xadff2f },
    { "grey", 0x808080 }, { "honeydew", 0xf0fff0 }, { "hotpink", 0xff69b4 }, { "indianred", 0xcd5c5c },
    { "indigo", 0x4b0082 }, { "ivory", 0xfffff0 }, { "khaki", 0xf0e68c }, { "lavender", 0xe6e6fa },
    { "lavenderblush", 0xfff0f5 }, { "lawngreen", 0x7cfc00 }, { "lemonchiffon", 0xfffacd }, { "lightblue", 0xadd8e6 },
    { "lightcoral", 0xf08080 }, { "lightcyan", 0xe0ffff }, { "lightgoldenrodyellow", 0xfafad2 }, { "lightgray", 0xd3d3d3 },
    { "lightgreen", 0x90ee90 }, { "lightgrey", 0xd3d3d3 }, { "lightpink", 0xffb6c1 }, { "lightsalmon", 0xffa07a },
    { "lightseagreen", 0x20b2aa }, { "lightskyblue", 0x87cefa }, { "lightslategray", 0x778899 }, { "lightslategrey", 0x778899 },
    { "lightsteelblue", 0xb0c4de }, { "lightyellow", 0xffffe0 }, { "lime", 0x00ff00 }, { "limegreen", 0x32cd32 },
    { "linen", 0xfaf0e6 }, { "magenta", 0xff00ff }, { "maroon", 0x800000 }, { "mediumaquamarine", 0x66cdaa },
    { "mediumblue", 0x0000cd }, { "mediumorchid", 0xba55d3 }, { "mediumpurple", 0x9370db }, { "mediumseagreen", 0x3cb371 },
    { "mediumslateblue", 0x7b68ee }, { "mediumspringgreen", 0x00fa9a }, { "mediumturquoise", 0x48d1cc }, { "mediumvioletred", 0xc71585 },
    { "midnightblue", 0x191970 }, { "mintcream", 0xf5fffa }, { "mistyrose", 0xffe4e1 }, { "moccasin", 0xffe4b5 },
    { "navajowhite", 0xffdead }, { "navy", 0x000080 }, { "oldlace", 0xfdf5e6 }, { "olive", 0x808000 },
    { "olivedrab", 0x6b8e23 }, { "orange", 0xffa500 }, { "orangered", 0xff4500 }, { "orchid", 0xda70d6 },
    { "palegoldenrod", 0xeee8aa }, { "palegreen", 0x98fb98 }, { "paleturquoise", 0xafeeee }, { "palevioletred", 0xdb7093 },
    { "papayawhip", 0xffefd5 }, { "peachpuff", 0xffdab9 }, { "peru", 0xcd853f }, { "pink", 0xffc0cb },
    { "plum", 0xdda0dd }, { "powderblue", 0xb0e0e6 }, { "purple", 0x800080 }, { "rebeccapurple", 0x663399 },
    { "red", 0xff0000 }, { "rosybrown", 0xbc8f8f }, { "royalblue", 0x4169e1 }, { "saddlebrown", 0x8b4513 },
    { "salmon", 0xfa8072 }, { "sandybrown", 0xf4a460 }, { "seagreen", 0x2e8b57 }, { "seashell", 0xfff5ee },
    { "sienna", 0xa0522d }, { "silver", 0xc0c0c0 }, { "skyblue", 0x87ceeb }, { "slateblue", 0x6a5acd },
    { "slategray", 0x708090 }, { "slategrey", 0x708090 }, { "snow", 0xfffafa }, { "springgreen", 0x00ff7f },
    { "steelblue", 0x4682b4 }, { "tan", 0xd2b48c }, { "teal", 0x008080 }, { "thistle", 0xd8bfd8 },
    { "tomato", 0xff6347 }, { "turquoise", 0x40e0d0 }, { "violet", 0xee82ee }, { "wheat", 0xf5deb3 },
    { "white", 0xffffff }, { "whitesmoke", 0xf5f5f5 }, { "yellow", 0xffff00 }, { "yellowgreen", 0x9acd32 } };

static int css_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = css_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static uint32_t css_byte(double x) { return x <= 0 ? 0 : x >= 255 ? 255 : (uint32_t)(x + 0.5); }
static double css_hue(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}
/* a colour function's arguments: up to four numbers, each with whether it is a percentage */
static int css_color_args(const char *s, size_t n, double *v, int *pct) {
    size_t i = 0;
    int k = 0;
    for (;;) {
        i = css_skip_ws(s, n, i);
        if (i >= n) break;
        if (k == 4) return 0;
        double x;
        size_t used = css_number(s + i, n - i, &x);
        if (!used) return 0;
        i += used;
        pct[k] = 0;
        if (i < n && s[i] == '%') { pct[k] = 1; i++; }
        else if (n - i >= 3 && css_eq_ci(s + i, 3, "deg")) i += 3;
        v[k++] = x;
        i = css_skip_ws(s, n, i);
        if (i < n && (s[i] == ',' || s[i] == '/')) i++;
    }
    return k;
}
/* a colour: #rgb, #rgba, #rrggbb, #rrggbbaa, a name, transparent, rgb(), rgba(), hsl(), hsla();
   RGBA in the u32, alpha in the low byte */
static int css_parse_color(const char *s, size_t n, uint32_t *out) {
    if (n > 1 && s[0] == '#') {
        size_t k = n - 1;
        int h[8];
        if (k != 3 && k != 4 && k != 6 && k != 8) return 0;
        for (size_t i = 0; i < k; i++) if ((h[i] = css_hex(s[i + 1])) < 0) return 0;
        uint32_t r, g, b, a = 255;
        if (k <= 4) {
            r = (uint32_t)(h[0] * 17); g = (uint32_t)(h[1] * 17); b = (uint32_t)(h[2] * 17);
            if (k == 4) a = (uint32_t)(h[3] * 17);
        } else {
            r = (uint32_t)(h[0] * 16 + h[1]); g = (uint32_t)(h[2] * 16 + h[3]); b = (uint32_t)(h[4] * 16 + h[5]);
            if (k == 8) a = (uint32_t)(h[6] * 16 + h[7]);
        }
        *out = r << 24 | g << 16 | b << 8 | a;
        return 1;
    }
    if (css_eq_ci(s, n, "transparent")) { *out = 0; return 1; }
    for (size_t i = 0; i < sizeof css_colors / sizeof css_colors[0]; i++)
        if (css_eq_ci(s, n, css_colors[i].name)) { *out = css_colors[i].rgb << 8 | 0xff; return 1; }
    size_t f = 0;
    while (f < n && css_namechar(s[f])) f++;
    if (f == 0 || f >= n || s[f] != '(' || s[n - 1] != ')') return 0;
    double v[4];
    int pct[4];
    int k = css_color_args(s + f + 1, n - f - 2, v, pct);
    if (k < 3) return 0;
    double alpha = k == 4 ? (pct[3] ? v[3] / 100 : v[3]) : 1;
    if (css_eq_ci(s, f, "rgb") || css_eq_ci(s, f, "rgba")) {
        uint32_t c[3];
        for (int i = 0; i < 3; i++) c[i] = css_byte(pct[i] ? v[i] * 255 / 100 : v[i]);
        *out = c[0] << 24 | c[1] << 16 | c[2] << 8 | css_byte(alpha * 255);
        return 1;
    }
    if (css_eq_ci(s, f, "hsl") || css_eq_ci(s, f, "hsla")) {
        double h = v[0] / 360, sat = v[1] / 100, l = v[2] / 100;
        h -= (double)(long)h;
        if (h < 0) h += 1;
        if (sat < 0) sat = 0;
        if (sat > 1) sat = 1;
        if (l < 0) l = 0;
        if (l > 1) l = 1;
        double q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
        *out = css_byte(css_hue(p, q, h + 1.0 / 3) * 255) << 24 | css_byte(css_hue(p, q, h) * 255) << 16 |
               css_byte(css_hue(p, q, h - 1.0 / 3) * 255) << 8 | css_byte(alpha * 255);
        return 1;
    }
    return 0;
}

/* a value: a length (absolute units made px), a percentage, a number, auto, a colour or a keyword */
static int css_parse_val(const char *s, size_t n, css_val *v) {
    memset(v, 0, sizeof *v);
    if (n == 0) return 0;
    if (css_eq_ci(s, n, "auto")) { v->unit = CSS_UNIT_AUTO; return 1; }
    double x;
    size_t used = css_number(s, n, &x);
    if (used) {
        const char *u = s + used;
        size_t un = n - used;
        v->num = x;
        if (un == 0) v->unit = CSS_UNIT_NUMBER;
        else if (css_eq_ci(u, un, "px")) v->unit = CSS_UNIT_PX;
        else if (css_eq_ci(u, un, "em")) v->unit = CSS_UNIT_EM;
        else if (css_eq_ci(u, un, "%")) v->unit = CSS_UNIT_PERCENT;
        else if (css_eq_ci(u, un, "ex") || css_eq_ci(u, un, "ch")) { v->unit = CSS_UNIT_EM; v->num = x / 2; }
        else {
            v->unit = CSS_UNIT_PX;
            if (css_eq_ci(u, un, "rem")) v->num = x * 16;
            else if (css_eq_ci(u, un, "pt")) v->num = x * 4 / 3;
            else if (css_eq_ci(u, un, "pc")) v->num = x * 16;
            else if (css_eq_ci(u, un, "in")) v->num = x * 96;
            else if (css_eq_ci(u, un, "cm")) v->num = x * 96 / 2.54;
            else if (css_eq_ci(u, un, "mm")) v->num = x * 96 / 25.4;
            else if (css_eq_ci(u, un, "vw")) v->num = x * CSS_VIEWPORT_W / 100;
            else if (css_eq_ci(u, un, "vh")) v->num = x * CSS_VIEWPORT_H / 100;
            else return 0;
        }
        return 1;
    }
    if (css_parse_color(s, n, &v->color)) { v->unit = CSS_UNIT_COLOR; return 1; }
    if (n >= sizeof v->word) return 0;
    for (size_t i = 0; i < n; i++) {
        if (!css_namechar(s[i])) return 0;
        v->word[i] = css_lower(s[i]);
    }
    v->word[n] = 0;
    v->unit = CSS_UNIT_KEYWORD;
    return 1;
}

/* the values a property takes (and every property takes the global keywords) */
static int css_valid(css_prop p, const css_val *v) {
    if (css_is_global(v)) return 1;
    css_unit u = v->unit;
    int kw = u == CSS_UNIT_KEYWORD;
    int length = u == CSS_UNIT_PX || u == CSS_UNIT_EM || u == CSS_UNIT_PERCENT || (u == CSS_UNIT_NUMBER && v->num == 0);
    if (p == CSS_PROP_DISPLAY)
        return kw && css_word_in(v->word, "inline block list-item inline-block none flex inline-flex grid inline-grid flow-root contents "
                                          "table inline-table table-row table-cell table-row-group table-header-group table-footer-group "
                                          "table-caption table-column table-column-group");
    if (p == CSS_PROP_COLOR || p == CSS_PROP_BACKGROUND_COLOR || p == CSS_PROP_BORDER_COLOR)
        return u == CSS_UNIT_COLOR || css_is_word(v, "currentcolor");
    if (p == CSS_PROP_FONT_SIZE)
        return (length && v->num >= 0) || (kw && css_word_in(v->word, "xx-small x-small small medium large x-large xx-large smaller larger"));
    if (p == CSS_PROP_FONT_WEIGHT)
        return (u == CSS_UNIT_NUMBER && v->num >= 1 && v->num <= 1000) || (kw && css_word_in(v->word, "normal bold bolder lighter"));
    if (p == CSS_PROP_FONT_STYLE) return kw && css_word_in(v->word, "normal italic oblique");
    if (p == CSS_PROP_TEXT_ALIGN) return kw && css_word_in(v->word, "left right center justify start end");
    if (p == CSS_PROP_LINE_HEIGHT)
        return ((u == CSS_UNIT_NUMBER || u == CSS_UNIT_PX || u == CSS_UNIT_EM || u == CSS_UNIT_PERCENT) && v->num >= 0) || css_is_word(v, "normal");
    if (p == CSS_PROP_TEXT_DECORATION) return kw && css_word_in(v->word, "none underline overline line-through");
    if (p == CSS_PROP_WHITE_SPACE) return kw && css_word_in(v->word, "normal pre nowrap pre-wrap pre-line break-spaces");
    if (p >= CSS_PROP_MARGIN_TOP && p <= CSS_PROP_MARGIN_LEFT) return length || u == CSS_UNIT_AUTO;
    if (p >= CSS_PROP_PADDING_TOP && p <= CSS_PROP_PADDING_LEFT) return length && v->num >= 0;
    if (p >= CSS_PROP_BORDER_TOP_WIDTH && p <= CSS_PROP_BORDER_LEFT_WIDTH)
        return (length && u != CSS_UNIT_PERCENT && v->num >= 0) || (kw && css_word_in(v->word, "thin medium thick"));
    if (p == CSS_PROP_WIDTH || p == CSS_PROP_HEIGHT) return (length && v->num >= 0) || u == CSS_UNIT_AUTO;
    if (p == CSS_PROP_LIST_STYLE_TYPE)
        return kw && css_word_in(v->word, "none disc circle square decimal decimal-leading-zero lower-roman upper-roman "
                                          "lower-alpha upper-alpha lower-latin upper-latin lower-greek");
    if (p == CSS_PROP_FONT_FAMILY) return kw && css_word_in(v->word, "serif sans-serif monospace");
    if (p == CSS_PROP_BOX_SIZING) return kw && css_word_in(v->word, "content-box border-box");
    if (p == CSS_PROP_MAX_WIDTH) return (length && v->num >= 0) || css_is_word(v, "none");
    return 0;
}

/* ---- the sheet's records ---- */

static const char *const css_prop_names[CSS_NPROPS] = { "display", "color", "background-color", "font-size", "font-weight",
    "font-style", "text-align", "line-height", "text-decoration", "white-space", "margin-top", "margin-right", "margin-bottom",
    "margin-left", "padding-top", "padding-right", "padding-bottom", "padding-left", "border-top-width", "border-right-width",
    "border-bottom-width", "border-left-width", "border-color", "width", "height", "list-style-type", "font-family", "box-sizing",
    "max-width" };

/* font-family's list made the generic family the module draws with: the first
   generic family of the list, or the first name it knows the kind of */
static int css_family_of(const char *s, size_t n, css_val *v) {
    static const char *const serif = "georgia times timesnewroman cambria garamond palatino palatinolinotype bookantiqua "
        "baskerville didot constantia dejavuserif liberationserif notoserif sourceserifpro charter cursive fantasy";
    static const char *const sans = "arial helvetica helveticaneue verdana tahoma trebuchetms segoeui roboto opensans lato "
        "calibri ubuntu cantarell dejavusans liberationsans notosans sourcesanspro inter system-ui -apple-system "
        "blinkmacsystemfont ui-sans-serif";
    static const char *const mono = "courier couriernew consolas menlo monaco lucidaconsole sourcecodepro dejavusansmono "
        "liberationmono notosansmono firacode firamono jetbrainsmono ui-monospace sfmono-regular";
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        size_t i = css_skip_ws(s, b, a), e = b;
        while (e > i && css_ws(s[e - 1])) e--;
        if (e > i && (s[i] == '"' || s[i] == '\'')) { i++; if (e > i && (s[e - 1] == '"' || s[e - 1] == '\'')) e--; }
        char w[48];
        size_t k = 0;
        for (size_t j = i; j < e && k + 1 < sizeof w; j++) if (!css_ws(s[j])) w[k++] = css_lower(s[j]);
        w[k] = 0;
        if (strcmp(w, "serif") == 0 || strcmp(w, "sans-serif") == 0 || strcmp(w, "monospace") == 0) { css_word_val(v, w); return 1; }
        if (css_word_in(w, serif)) { css_word_val(v, "serif"); return 1; }
        if (css_word_in(w, sans)) { css_word_val(v, "sans-serif"); return 1; }
        if (css_word_in(w, mono)) { css_word_val(v, "monospace"); return 1; }
        a = b + 1;
    }
    return 0;
}

static void css_add_decl(css_style *s, uint32_t rule, css_prop prop, const css_val *v, int important, const char *text, size_t tn) {
    amb_span t = text_add(s, text, tn);
    uint32_t w = v->unit == CSS_UNIT_KEYWORD ? names_intern(s, v->word, strlen(v->word)) : 0;
    css_decl_h h = decl_new(s);
    s->decl[h.index].prop = prop;
    s->decl[h.index].important = (uint8_t)important;
    s->decl[h.index].unit = v->unit;
    s->decl[h.index].num = v->num;
    s->decl[h.index].color = v->color;
    s->decl[h.index].word = w;
    s->decl[h.index].text = t;
    decls_append(s, rule, h.index);
}

/* one value for each of props[0..n) */
static void css_add_all(css_style *s, uint32_t rule, const css_prop *props, int n, const css_val *v, int important, const char *t, size_t tn) {
    for (int i = 0; i < n; i++) css_add_decl(s, rule, props[i], v, important, t, tn);
}

/* margin, padding, border-width: one to four values, top right bottom left */
static void css_box(css_style *s, uint32_t rule, css_prop base, const char *val, size_t vn, int important) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    if (k == 0 || k > 4) return;
    css_val v[4];
    for (size_t j = 0; j < k; j++)
        if (!css_parse_val(val + st[j], ln[j], &v[j]) || !css_valid(base, &v[j]) || css_is_global(&v[j])) return;
    static const size_t side_of[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
    for (int side = 0; side < 4; side++) {
        size_t j = side_of[k - 1][side];
        css_add_decl(s, rule, (css_prop)(base + side), &v[j], important, val + st[j], ln[j]);
    }
}

/* border and border-top ... border-left: a width, a style and a colour in any order; side -1 is all four */
static void css_border(css_style *s, uint32_t rule, int side, const char *val, size_t vn, int important) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    if (k == 0 || k > 3) return;
    css_val width, color, v;
    int has_width = 0, has_color = 0, has_style = 0, visible = 0;
    css_word_val(&width, "medium");
    css_word_val(&color, "currentcolor");
    for (size_t j = 0; j < k; j++) {
        if (!css_parse_val(val + st[j], ln[j], &v) || css_is_global(&v)) return;
        if (css_valid(CSS_PROP_BORDER_COLOR, &v)) { if (has_color) return; color = v; has_color = 1; }
        else if (v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "none hidden")) { if (has_style) return; has_style = 1; }
        else if (v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "solid dotted dashed double groove ridge inset outset")) {
            if (has_style) return;
            has_style = 1;
            visible = 1;
        } else if (css_valid(CSS_PROP_BORDER_TOP_WIDTH, &v)) { if (has_width) return; width = v; has_width = 1; }
        else return;
    }
    /* a border whose style is none is no border: its width computes to 0 */
    if (!visible) { memset(&width, 0, sizeof width); width.unit = CSS_UNIT_PX; }
    for (int i = 0; i < 4; i++)
        if (side < 0 || side == i) css_add_decl(s, rule, (css_prop)(CSS_PROP_BORDER_TOP_WIDTH + i), &width, important, val, vn);
    css_add_decl(s, rule, CSS_PROP_BORDER_COLOR, &color, important, val, vn);
}

/* font: [style || weight || variant]? size[/line-height] family; what it leaves out is reset */
static void css_font(css_style *s, uint32_t rule, const char *val, size_t vn, int important) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    css_val style, weight, size, lh, v;
    int have_size = 0;
    css_word_val(&style, "normal");
    css_word_val(&weight, "normal");
    css_word_val(&lh, "normal");
    memset(&size, 0, sizeof size);
    size_t size_j = 0;
    for (size_t j = 0; j < k && !have_size; j++) {
        const char *p = val + st[j];
        size_t pn = ln[j], slash = 0;
        size_j = j;
        while (slash < pn && p[slash] != '/') slash++;
        if (!css_parse_val(p, slash, &v) || css_is_global(&v)) return;
        if (slash == pn && v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "italic oblique")) style = v;
        else if (slash == pn && v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "normal small-caps")) {}
        else if (slash == pn && css_valid(CSS_PROP_FONT_WEIGHT, &v)) weight = v;
        else if (css_valid(CSS_PROP_FONT_SIZE, &v)) {
            size = v;
            have_size = 1;
            if (slash < pn && (!css_parse_val(p + slash + 1, pn - slash - 1, &lh) || !css_valid(CSS_PROP_LINE_HEIGHT, &lh) || css_is_global(&lh))) return;
        } else return;
    }
    if (!have_size) return;     /* caption, menu and the other system fonts: not read */
    /* the family list follows the size (and its line height) */
    css_val fam;
    size_t after = size_j + 1 < k ? st[size_j + 1] : vn;
    if (after >= vn || !css_family_of(val + after, vn - after, &fam)) css_word_val(&fam, "serif");
    css_add_decl(s, rule, CSS_PROP_FONT_FAMILY, &fam, important, val, vn);
    css_add_decl(s, rule, CSS_PROP_FONT_STYLE, &style, important, val, vn);
    css_add_decl(s, rule, CSS_PROP_FONT_WEIGHT, &weight, important, val, vn);
    css_add_decl(s, rule, CSS_PROP_FONT_SIZE, &size, important, val, vn);
    css_add_decl(s, rule, CSS_PROP_LINE_HEIGHT, &lh, important, val, vn);
}

/* background, list-style, text-decoration: the one part the module keeps, the rest
   read past; what is not written is reset */
static void css_pick(css_style *s, uint32_t rule, css_prop p, const char *reset, const char *val, size_t vn, int important) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    css_val v, pick;
    int have = 0;
    if (p == CSS_PROP_BACKGROUND_COLOR) { memset(&pick, 0, sizeof pick); pick.unit = CSS_UNIT_COLOR; }
    else css_word_val(&pick, reset);
    for (size_t j = 0; j < k; j++) {
        if (!css_parse_val(val + st[j], ln[j], &v)) continue;
        if (css_is_global(&v)) return;
        if (css_valid(p, &v) && !(p == CSS_PROP_BACKGROUND_COLOR && v.unit == CSS_UNIT_KEYWORD)) {
            if (have) return;
            pick = v;
            have = 1;
        }
    }
    css_add_decl(s, rule, p, &pick, important, val, vn);
}

static int css_side_of(const char *name, size_t nn, const char *prefix, const char *suffix, int *side) {
    static const char *const sides[4] = { "top", "right", "bottom", "left" };
    for (int i = 0; i < 4; i++) {
        char w[48];
        snprintf(w, sizeof w, "%s%s%s", prefix, sides[i], suffix);
        if (css_eq_ci(name, nn, w)) { *side = i; return 1; }
    }
    return 0;
}

/* a declaration, its shorthand expanded */
static void css_declaration(css_style *s, uint32_t rule, const char *name, size_t nn, const char *val, size_t vn) {
    int important = 0, side = 0;
    while (vn && css_ws(val[vn - 1])) vn--;
    if (vn >= 9 && css_eq_ci(val + vn - 9, 9, "important")) {
        size_t b = vn - 9;
        while (b && css_ws(val[b - 1])) b--;
        if (b && val[b - 1] == '!') {
            important = 1;
            vn = b - 1;
            while (vn && css_ws(val[vn - 1])) vn--;
        }
    }
    size_t vs = css_skip_ws(val, vn, 0);
    val += vs;
    vn -= vs;
    if (vn == 0 || nn == 0) return;
    css_val g;
    int global = css_parse_val(val, vn, &g) && css_is_global(&g);
    static const css_prop margins[4] = { CSS_PROP_MARGIN_TOP, CSS_PROP_MARGIN_RIGHT, CSS_PROP_MARGIN_BOTTOM, CSS_PROP_MARGIN_LEFT };
    static const css_prop paddings[4] = { CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_RIGHT, CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT };
    static const css_prop borders[5] = { CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_RIGHT_WIDTH, CSS_PROP_BORDER_BOTTOM_WIDTH,
                                         CSS_PROP_BORDER_LEFT_WIDTH, CSS_PROP_BORDER_COLOR };
    static const css_prop fonts[4] = { CSS_PROP_FONT_STYLE, CSS_PROP_FONT_WEIGHT, CSS_PROP_FONT_SIZE, CSS_PROP_LINE_HEIGHT };
    if (css_eq_ci(name, nn, "margin")) { if (global) css_add_all(s, rule, margins, 4, &g, important, val, vn); else css_box(s, rule, CSS_PROP_MARGIN_TOP, val, vn, important); return; }
    if (css_eq_ci(name, nn, "padding")) { if (global) css_add_all(s, rule, paddings, 4, &g, important, val, vn); else css_box(s, rule, CSS_PROP_PADDING_TOP, val, vn, important); return; }
    if (css_eq_ci(name, nn, "border-width")) { if (global) css_add_all(s, rule, borders, 4, &g, important, val, vn); else css_box(s, rule, CSS_PROP_BORDER_TOP_WIDTH, val, vn, important); return; }
    if (css_eq_ci(name, nn, "border")) { if (global) css_add_all(s, rule, borders, 5, &g, important, val, vn); else css_border(s, rule, -1, val, vn, important); return; }
    if (css_side_of(name, nn, "border-", "", &side)) {
        if (global) { css_add_decl(s, rule, borders[side], &g, important, val, vn); css_add_decl(s, rule, CSS_PROP_BORDER_COLOR, &g, important, val, vn); }
        else css_border(s, rule, side, val, vn, important);
        return;
    }
    if (css_eq_ci(name, nn, "font")) { if (global) css_add_all(s, rule, fonts, 4, &g, important, val, vn); else css_font(s, rule, val, vn, important); return; }
    if (css_eq_ci(name, nn, "background")) { if (global) css_add_decl(s, rule, CSS_PROP_BACKGROUND_COLOR, &g, important, val, vn); else css_pick(s, rule, CSS_PROP_BACKGROUND_COLOR, "", val, vn, important); return; }
    if (css_eq_ci(name, nn, "font-family")) {
        css_val f;
        if (global) css_add_decl(s, rule, CSS_PROP_FONT_FAMILY, &g, important, val, vn);
        else if (css_family_of(val, vn, &f)) css_add_decl(s, rule, CSS_PROP_FONT_FAMILY, &f, important, val, vn);
        return;
    }
    if (css_eq_ci(name, nn, "list-style")) { if (global) css_add_decl(s, rule, CSS_PROP_LIST_STYLE_TYPE, &g, important, val, vn); else css_pick(s, rule, CSS_PROP_LIST_STYLE_TYPE, "disc", val, vn, important); return; }
    if (css_eq_ci(name, nn, "text-decoration") || css_eq_ci(name, nn, "text-decoration-line")) {
        if (global) css_add_decl(s, rule, CSS_PROP_TEXT_DECORATION, &g, important, val, vn);
        else css_pick(s, rule, CSS_PROP_TEXT_DECORATION, "none", val, vn, important);
        return;
    }
    css_prop p = CSS_PROP_DISPLAY;
    int found = 0;
    for (int i = 0; i < CSS_NPROPS && !found; i++) if (css_eq_ci(name, nn, css_prop_names[i])) { p = (css_prop)i; found = 1; }
    if (!found && css_side_of(name, nn, "border-", "-color", &side)) { p = CSS_PROP_BORDER_COLOR; found = 1; }
    if (!found) return;
    css_val v;
    if (p == CSS_PROP_BORDER_COLOR && !global) {
        /* border-color's one to four colours: the module keeps one, the first */
        size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
        size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
        if (k == 0 || k > 4 || !css_parse_val(val + st[0], ln[0], &v) || !css_valid(p, &v)) return;
        css_add_decl(s, rule, p, &v, important, val, vn);
        return;
    }
    if (!css_parse_val(val, vn, &v) || !css_valid(p, &v)) return;
    css_add_decl(s, rule, p, &v, important, val, vn);
}

/* a declaration block: name: value; ... */
static void css_parse_block(css_style *s, uint32_t rule, const char *t, size_t n) {
    size_t d = 0;
    while (d < n) {
        size_t e = d;
        while (e < n && t[e] != ';') {
            if (t[e] == '(') e = css_block_end(t, n, e, '(', ')');
            else if (t[e] == '{') e = css_block_end(t, n, e, '{', '}');
            else if (t[e] == '"' || t[e] == '\'') e = css_string_end(t, n, e);
            if (e < n) e++;
        }
        size_t colon = d;
        while (colon < e && t[colon] != ':') colon++;
        if (colon < e) {
            size_t na = css_skip_ws(t, colon, d), nb = colon;
            while (nb > na && css_ws(t[nb - 1])) nb--;
            css_declaration(s, rule, t + na, nb - na, t + colon + 1, e - colon - 1);
        }
        d = e + 1;
    }
}

/* ---- media queries (Media Queries 4, abridged): against the viewport ---- */

static int css_media_feature(const char *s, size_t n) {
    size_t c = 0;
    while (c < n && s[c] != ':') c++;
    size_t a = css_skip_ws(s, c, 0), b = c;
    while (b > a && css_ws(s[b - 1])) b--;
    const char *name = s + a;
    size_t nn = b - a;
    if (c >= n) return css_eq_ci(name, nn, "color") || css_eq_ci(name, nn, "hover") || css_eq_ci(name, nn, "pointer");
    size_t va = css_skip_ws(s, n, c + 1), vb = n;
    while (vb > va && css_ws(s[vb - 1])) vb--;
    css_val v;
    if (!css_parse_val(s + va, vb - va, &v)) return 0;
    double px = v.unit == CSS_UNIT_PX ? v.num : v.unit == CSS_UNIT_EM ? v.num * 16 : v.unit == CSS_UNIT_NUMBER && v.num == 0 ? 0 : -1;
    if (css_eq_ci(name, nn, "min-width")) return px >= 0 && CSS_VIEWPORT_W >= px;
    if (css_eq_ci(name, nn, "max-width")) return px >= 0 && CSS_VIEWPORT_W <= px;
    if (css_eq_ci(name, nn, "width")) return px == CSS_VIEWPORT_W;
    if (css_eq_ci(name, nn, "min-height")) return px >= 0 && CSS_VIEWPORT_H >= px;
    if (css_eq_ci(name, nn, "max-height")) return px >= 0 && CSS_VIEWPORT_H <= px;
    if (css_eq_ci(name, nn, "orientation")) return css_is_word(&v, "landscape");
    if (css_eq_ci(name, nn, "prefers-color-scheme")) return css_is_word(&v, "light");
    if (css_eq_ci(name, nn, "prefers-reduced-motion")) return css_is_word(&v, "no-preference");
    return 0;
}

/* one query: [not|only]? type? (and (feature))* */
static int css_media_query(const char *s, size_t n) {
    size_t i = css_skip_ws(s, n, 0);
    int neg = 0, ok = 1, first = 1;
    if (i >= n) return 0;
    while (i < n) {
        if (s[i] == '(') {
            size_t e = css_block_end(s, n, i, '(', ')');
            if (e >= n) return 0;
            if (!css_media_feature(s + i + 1, e - i - 1)) ok = 0;
            i = e + 1;
        } else {
            size_t b = i;
            while (i < n && css_namechar(s[i])) i++;
            if (i == b) return 0;
            const char *w = s + b;
            size_t wn = i - b;
            if (first && css_eq_ci(w, wn, "not")) neg = 1;
            else if (first && css_eq_ci(w, wn, "only")) {}
            else if (css_eq_ci(w, wn, "and") || css_eq_ci(w, wn, "all") || css_eq_ci(w, wn, "screen")) {}
            else ok = 0;    /* print, speech, an unknown type, or `or' */
        }
        first = 0;
        i = css_skip_ws(s, n, i);
    }
    return neg ? !ok : ok;
}

/* a media query list: empty, or one of its queries holds */
static int css_media_matches(const char *s, size_t n) {
    if (css_skip_ws(s, n, 0) >= n) return 1;
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        if (css_media_query(s + a, b - a)) return 1;
        a = b + 1;
    }
    return 0;
}

/* ---- specificity (Selectors 4, 17) ---- */

typedef struct css_part { uint32_t start, len; char comb; } css_part;

/* a complex selector's compounds, left to right, each with the combinator before it
   (0 for the first); 0 when it is not a selector this reads */
static size_t css_split(const char *s, size_t n, css_part *parts, size_t max) {
    size_t k = 0, i = css_skip_ws(s, n, 0);
    char comb = 0;
    while (i < n) {
        if (k == max) return 0;
        size_t b = i;
        while (i < n && !css_ws(s[i]) && s[i] != '>' && s[i] != '+' && s[i] != '~') {
            if (s[i] == '[') i = css_block_end(s, n, i, '[', ']');
            else if (s[i] == '(') i = css_block_end(s, n, i, '(', ')');
            if (i < n) i++;
        }
        if (i == b) return 0;
        parts[k].start = (uint32_t)b;
        parts[k].len = (uint32_t)(i - b);
        parts[k].comb = comb;
        k++;
        size_t j = css_skip_ws(s, n, i);
        if (j >= n) return k;
        comb = ' ';
        if (s[j] == '>' || s[j] == '+' || s[j] == '~') {
            comb = s[j];
            j = css_skip_ws(s, n, j + 1);
            if (j >= n) return 0;
        }
        i = j;
    }
    return k;
}

static uint32_t css_spec_list(const char *s, size_t n, int depth);

static uint32_t css_spec_compound(const char *s, size_t n, int depth) {
    uint32_t a = 0, b = 0, c = 0, extra = 0;
    if (depth > CSS_MAX_NESTING) return 0;
    size_t i = 0;
    while (i < n) {
        char ch = s[i];
        if (ch == '*') { i++; continue; }
        if (css_namechar(ch)) { c++; while (i < n && css_namechar(s[i])) i++; continue; }
        if (ch == '#' || ch == '.') {
            if (ch == '#') a++; else b++;
            i++;
            while (i < n && css_namechar(s[i])) i++;
            continue;
        }
        if (ch == '[') { b++; i = css_block_end(s, n, i, '[', ']') + 1; continue; }
        if (ch == ':') {
            int element = i + 1 < n && s[i + 1] == ':';
            i += element ? 2 : 1;
            size_t pb = i;
            while (i < n && css_namechar(s[i])) i++;
            const char *ps = s + pb;
            size_t pn = i - pb;
            if (i < n && s[i] == '(') {
                size_t e = css_block_end(s, n, i, '(', ')');
                if (e >= n) return a * 65536 + b * 256 + c + extra;
                if (css_eq_ci(ps, pn, "where")) {}
                else if (css_eq_ci(ps, pn, "not") || css_eq_ci(ps, pn, "is") || css_eq_ci(ps, pn, "matches") || css_eq_ci(ps, pn, "has"))
                    extra += css_spec_list(s + i + 1, e - i - 1, depth + 1);
                else if (element) c++;
                else b++;
                i = e + 1;
                continue;
            }
            if (element || css_eq_ci(ps, pn, "before") || css_eq_ci(ps, pn, "after") || css_eq_ci(ps, pn, "first-line") ||
                css_eq_ci(ps, pn, "first-letter")) c++;
            else b++;
            continue;
        }
        i++;
    }
    return a * 65536 + b * 256 + c + extra;
}

static uint32_t css_spec_complex(const char *s, size_t n, int depth) {
    css_part parts[CSS_MAXPARTS];
    size_t k = css_split(s, n, parts, CSS_MAXPARTS);
    uint32_t sum = 0;
    for (size_t j = 0; j < k; j++) sum += css_spec_compound(s + parts[j].start, parts[j].len, depth);
    return sum;
}

/* a list's specificity: its most specific selector's */
static uint32_t css_spec_list(const char *s, size_t n, int depth) {
    uint32_t best = 0;
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        uint32_t sp = css_spec_complex(s + a, b - a, depth);
        if (sp > best) best = sp;
        a = b + 1;
    }
    return best;
}

/* ---- the sheet's rules ---- */

/* a style rule: each selector of its list a rule of its own, with the block's declarations */
static void css_style_rule(css_style *s, uint32_t sheet, const char *pre, size_t pn, const char *blk, size_t bn) {
    size_t a = 0;
    while (a < pn) {
        size_t b = css_comma(pre, pn, a);
        size_t sa = css_skip_ws(pre, b, a), sb = b;
        while (sb > sa && css_ws(pre[sb - 1])) sb--;
        if (sb > sa) {
            /* the selector as written, each run of white space one space */
            vector<char> sel;
            size_t i = sa;
            while (i < sb) {
                if (pre[i] == '"' || pre[i] == '\'') {
                    size_t e = css_string_end(pre, sb, i);
                    size_t stop = e < sb ? e + 1 : sb;
                    append_range(sel, pre + i, stop - i);
                    i = stop;
                    continue;
                }
                if (css_ws(pre[i])) { if (len(sel) && back(sel) != ' ') push_back(sel, ' '); }
                else push_back(sel, pre[i]);
                i++;
            }
            amb_span sp = text_add(s, data(sel), len(sel));
            css_rule_h r = rule_new(s);
            s->rule[r.index].selector = sp;
            s->rule[r.index].spec = css_spec_complex(data(sel), len(sel), 0);
            s->rule[r.index].order = r.index;
            rules_append(s, sheet, r.index);
            css_parse_block(s, r.index, blk, bn);
        }
        a = b + 1;
    }
}

/* a sheet's text, its comments gone: rules and at-rules */
static void css_rules(css_style *s, uint32_t sheet, const char *t, size_t n) {
    size_t i = 0;
    while (i < n) {
        i = css_skip_ws(t, n, i);
        if (i >= n) break;
        if (n - i >= 4 && memcmp(t + i, "<!--", 4) == 0) { i += 4; continue; }
        if (n - i >= 3 && memcmp(t + i, "-->", 3) == 0) { i += 3; continue; }
        int at = t[i] == '@';
        size_t k = i;
        while (k < n && t[k] != '{' && !(at && t[k] == ';')) {
            if (t[k] == '"' || t[k] == '\'') k = css_string_end(t, n, k);
            else if (t[k] == '(') k = css_block_end(t, n, k, '(', ')');
            else if (t[k] == '[') k = css_block_end(t, n, k, '[', ']');
            if (k < n) k++;
        }
        if (k >= n) break;
        if (t[k] == ';') { i = k + 1; continue; }    /* @import, @charset, @namespace: not read */
        size_t end = css_block_end(t, n, k, '{', '}');
        size_t inner = end < n ? end - k - 1 : n - k - 1;
        if (at) {
            size_t j = i + 1;
            while (j < k && css_namechar(t[j])) j++;
            /* @media whose query holds: its rules are the sheet's; @supports, @layer,
               @font-face, @keyframes, @page and the rest: not read */
            if (css_eq_ci(t + i + 1, j - i - 1, "media") && css_media_matches(t + j, k - j)) css_rules(s, sheet, t + k + 1, inner);
        } else {
            css_style_rule(s, sheet, t + i, k - i, t + k + 1, inner);
        }
        i = end + 1;
    }
}

css_status css_parse_sheet(css_style *s, css_origin origin, const char *src, size_t src_len) {
    vector<char> t;
    css_strip_comments(src, src_len, &t);
    css_sheet_h sh = sheet_new(s);
    s->sheet[sh.index].origin = origin;
    css_rules(s, sh.index, data(t), len(t));
    s->revision++;
    return CSS_OK;
}

/* ---- matching (Selectors 4) ---- */

static int css_is_element(const dom_document *d, uint32_t x) { return x != 0 && d->node[x].kind == DOM_NODE_KIND_ELEMENT; }
static int css_is_named(const dom_document *d, uint32_t x, const char *name) {
    return css_is_element(d, x) && names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t css_attr_of(const dom_document *d, uint32_t x, const char *name, size_t nn) {
    uint32_t a = names_find(d, name, nn);
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
static uint32_t css_prev_element(const dom_document *d, uint32_t x) {
    for (uint32_t p = d->node[x].prev_sibling; p; p = d->node[p].prev_sibling) if (d->node[p].kind == DOM_NODE_KIND_ELEMENT) return p;
    return 0;
}
static uint32_t css_next_element(const dom_document *d, uint32_t x) {
    for (uint32_t p = d->node[x].next_sibling; p; p = d->node[p].next_sibling) if (d->node[p].kind == DOM_NODE_KIND_ELEMENT) return p;
    return 0;
}
static uint32_t css_parent_element(const dom_document *d, uint32_t x) {
    uint32_t p = d->node[x].parent;
    return css_is_element(d, p) ? p : 0;
}
/* the sibling elements before x (or after it), of x's type only if of_type */
static int css_siblings(const dom_document *d, uint32_t x, int after, int of_type) {
    int k = 0;
    for (uint32_t y = after ? css_next_element(d, x) : css_prev_element(d, x); y; y = after ? css_next_element(d, y) : css_prev_element(d, y))
        if (!of_type || d->node[y].name == d->node[x].name) k++;
    return k;
}
/* the next node of the document in tree order, or 0 */
static uint32_t css_next_in_tree(const dom_document *d, uint32_t x) {
    if (d->node[x].first_child) return d->node[x].first_child;
    while (x != 1 && !d->node[x].next_sibling) x = d->node[x].parent;
    return x == 1 ? 0 : d->node[x].next_sibling;
}

/* the value v (vn bytes) holds the token w, which white space separates */
static int css_has_token(const char *v, size_t vn, const char *w, size_t wn, int ci) {
    size_t i = 0;
    if (wn == 0) return 0;
    while (i < vn) {
        while (i < vn && css_ws(v[i])) i++;
        size_t b = i;
        while (i < vn && !css_ws(v[i])) i++;
        if (i - b == wn && css_bytes_eq(v + b, w, wn, ci)) return 1;
    }
    return 0;
}

/* An+B (CSS Syntax 3, 6): odd, even, B, An, An+B */
static int css_anb(const char *s, size_t n, int *a, int *b) {
    size_t i = css_skip_ws(s, n, 0), e = n;
    while (e > i && css_ws(s[e - 1])) e--;
    if (css_eq_ci(s + i, e - i, "odd")) { *a = 2; *b = 1; return 1; }
    if (css_eq_ci(s + i, e - i, "even")) { *a = 2; *b = 0; return 1; }
    int sign = 1, num = 0, digits = 0;
    if (i < e && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') sign = -1; i++; }
    while (i < e && s[i] >= '0' && s[i] <= '9') { num = num * 10 + (s[i] - '0'); digits++; i++; }
    if (i < e && css_lower(s[i]) == 'n') {
        *a = sign * (digits ? num : 1);
        *b = 0;
        i = css_skip_ws(s, e, i + 1);
        if (i < e) {
            int bs = s[i] == '-' ? -1 : 1, bn = 0, bd = 0;
            if (s[i] != '+' && s[i] != '-') return 0;
            i = css_skip_ws(s, e, i + 1);
            while (i < e && s[i] >= '0' && s[i] <= '9') { bn = bn * 10 + (s[i] - '0'); bd++; i++; }
            if (!bd) return 0;
            *b = bs * bn;
        }
    } else {
        if (!digits) return 0;
        *a = 0;
        *b = sign * num;
    }
    return i == e;
}
static int css_nth(const dom_document *d, uint32_t x, const char *s, size_t n, int from_end, int of_type) {
    int a, b;
    if (!css_anb(s, n, &a, &b)) return 0;
    int k = css_siblings(d, x, from_end, of_type) + 1 - b;
    if (a == 0) return k == 0;
    return k % a == 0 && k / a >= 0;
}

static int css_complex_matches(const dom_document *d, uint32_t x, const char *s, size_t n, int depth, int *budget);

/* one of a selector list's complex selectors matches x */
static int css_list_matches(const dom_document *d, uint32_t x, const char *s, size_t n, int depth, int *budget) {
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        if (css_complex_matches(d, x, s + a, b - a, depth, budget)) return 1;
        a = b + 1;
    }
    return 0;
}

static int css_form_control(const dom_document *d, uint32_t x) {
    return css_is_named(d, x, "button") || css_is_named(d, x, "input") || css_is_named(d, x, "select") ||
           css_is_named(d, x, "textarea") || css_is_named(d, x, "option") || css_is_named(d, x, "optgroup") || css_is_named(d, x, "fieldset");
}

/* a pseudo-class without arguments holds of x */
static int css_pseudo(const dom_document *d, uint32_t x, const char *ps, size_t pn) {
    if (css_eq_ci(ps, pn, "first-child")) return css_siblings(d, x, 0, 0) == 0;
    if (css_eq_ci(ps, pn, "last-child")) return css_siblings(d, x, 1, 0) == 0;
    if (css_eq_ci(ps, pn, "only-child")) return css_siblings(d, x, 0, 0) == 0 && css_siblings(d, x, 1, 0) == 0;
    if (css_eq_ci(ps, pn, "first-of-type")) return css_siblings(d, x, 0, 1) == 0;
    if (css_eq_ci(ps, pn, "last-of-type")) return css_siblings(d, x, 1, 1) == 0;
    if (css_eq_ci(ps, pn, "only-of-type")) return css_siblings(d, x, 0, 1) == 0 && css_siblings(d, x, 1, 1) == 0;
    if (css_eq_ci(ps, pn, "root")) return d->node[x].parent == 1;
    if (css_eq_ci(ps, pn, "empty")) {
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_ELEMENT || (d->node[c].kind == DOM_NODE_KIND_TEXT && d->node[c].data.len > 0)) return 0;
        return 1;
    }
    if (css_eq_ci(ps, pn, "link") || css_eq_ci(ps, pn, "any-link"))
        return (css_is_named(d, x, "a") || css_is_named(d, x, "area")) && css_attr_of(d, x, "href", 4);
    if (css_eq_ci(ps, pn, "checked"))
        return (css_is_named(d, x, "input") && css_attr_of(d, x, "checked", 7)) || (css_is_named(d, x, "option") && css_attr_of(d, x, "selected", 8));
    if (css_eq_ci(ps, pn, "disabled")) return css_form_control(d, x) && css_attr_of(d, x, "disabled", 8);
    if (css_eq_ci(ps, pn, "enabled")) return css_form_control(d, x) && !css_attr_of(d, x, "disabled", 8);
    return 0;   /* :hover, :focus, :visited, :target and the rest: a page at rest has no such state */
}

/* an attribute selector, [ to ] (e is the ]), holds of x */
static int css_attr_sel(const dom_document *d, uint32_t x, const char *s, size_t i, size_t e) {
    size_t a = css_skip_ws(s, e, i + 1), ae = a;
    while (ae < e && css_namechar(s[ae])) ae++;
    char an[64];
    size_t anl = ae - a;
    if (anl == 0 || anl >= sizeof an) return 0;
    for (size_t k = 0; k < anl; k++) an[k] = css_lower(s[a + k]);
    uint32_t at = css_attr_of(d, x, an, anl);
    if (!at) return 0;
    size_t o = css_skip_ws(s, e, ae);
    if (o >= e) return 1;
    char op = s[o];
    if (op != '=') {
        if (o + 1 >= e || s[o + 1] != '=') return 0;
        o++;
    }
    o = css_skip_ws(s, e, o + 1);
    size_t vs, ve, after;
    if (o < e && (s[o] == '"' || s[o] == '\'')) {
        vs = o + 1;
        ve = css_string_end(s, e, o);
        after = ve < e ? ve + 1 : e;
    } else {
        vs = o;
        ve = o;
        while (ve < e && css_namechar(s[ve])) ve++;
        after = ve;
    }
    after = css_skip_ws(s, e, after);
    int ci = after < e && css_lower(s[after]) == 'i';
    amb_span v = d->attr[at].value;
    const char *vp = text_ptr(d, v);
    const char *w = s + vs;
    size_t wn = ve - vs;
    if (op == '=') return v.len == wn && css_bytes_eq(vp, w, wn, ci);
    if (op == '~') return css_has_token(vp, v.len, w, wn, ci);
    if (op == '|') return (v.len == wn && css_bytes_eq(vp, w, wn, ci)) || (v.len > wn && css_bytes_eq(vp, w, wn, ci) && vp[wn] == '-');
    if (op == '^') return wn > 0 && v.len >= wn && css_bytes_eq(vp, w, wn, ci);
    if (op == '$') return wn > 0 && v.len >= wn && css_bytes_eq(vp + v.len - wn, w, wn, ci);
    if (op == '*') {
        for (size_t k = 0; wn > 0 && k + wn <= v.len; k++) if (css_bytes_eq(vp + k, w, wn, ci)) return 1;
        return 0;
    }
    return 0;
}

/* a compound selector holds of the element x */
static int css_compound(const dom_document *d, uint32_t x, const char *s, size_t n, int depth, int *budget) {
    size_t i = 0;
    if (n == 0 || depth > CSS_MAX_NESTING || --*budget < 0) return 0;
    while (i < n) {
        char c = s[i];
        if (c == '*') { i++; continue; }
        if (css_namechar(c)) {
            char lw[64];
            size_t k = 0;
            while (i < n && css_namechar(s[i])) { if (k == sizeof lw) return 0; lw[k++] = css_lower(s[i]); i++; }
            if (!names_is(d, d->node[x].name, lw, k)) return 0;
            continue;
        }
        if (c == '#' || c == '.') {
            size_t b = ++i;
            while (i < n && css_namechar(s[i])) i++;
            uint32_t at = css_attr_of(d, x, c == '#' ? "id" : "class", c == '#' ? 2 : 5);
            if (!at || i == b) return 0;
            amb_span v = d->attr[at].value;
            if (c == '#') { if (v.len != i - b || memcmp(text_ptr(d, v), s + b, v.len) != 0) return 0; }
            else if (!css_has_token(text_ptr(d, v), v.len, s + b, i - b, 0)) return 0;
            continue;
        }
        if (c == '[') {
            size_t e = css_block_end(s, n, i, '[', ']');
            if (e >= n || !css_attr_sel(d, x, s, i, e)) return 0;
            i = e + 1;
            continue;
        }
        if (c == ':') {
            if (i + 1 < n && s[i + 1] == ':') return 0;     /* a pseudo-element is no element */
            size_t b = ++i;
            while (i < n && css_namechar(s[i])) i++;
            const char *ps = s + b;
            size_t pn = i - b;
            if (i < n && s[i] == '(') {
                size_t e = css_block_end(s, n, i, '(', ')');
                if (e >= n) return 0;
                const char *as = s + i + 1;
                size_t an = e - i - 1;
                int ok;
                if (css_eq_ci(ps, pn, "not")) ok = !css_list_matches(d, x, as, an, depth + 1, budget) && *budget >= 0;
                else if (css_eq_ci(ps, pn, "is") || css_eq_ci(ps, pn, "where") || css_eq_ci(ps, pn, "matches")) ok = css_list_matches(d, x, as, an, depth + 1, budget);
                else if (css_eq_ci(ps, pn, "nth-child")) ok = css_nth(d, x, as, an, 0, 0);
                else if (css_eq_ci(ps, pn, "nth-last-child")) ok = css_nth(d, x, as, an, 1, 0);
                else if (css_eq_ci(ps, pn, "nth-of-type")) ok = css_nth(d, x, as, an, 0, 1);
                else if (css_eq_ci(ps, pn, "nth-last-of-type")) ok = css_nth(d, x, as, an, 1, 1);
                else ok = 0;
                if (!ok) return 0;
                i = e + 1;
                continue;
            }
            if (css_eq_ci(ps, pn, "before") || css_eq_ci(ps, pn, "after") || css_eq_ci(ps, pn, "first-line") || css_eq_ci(ps, pn, "first-letter")) return 0;
            if (!css_pseudo(d, x, ps, pn)) return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

/* the compounds parts[0..k) hold, parts[k-1] of x and the rest through their combinators */
static int css_parts_match(const dom_document *d, uint32_t x, const char *s, const css_part *parts, size_t k, int depth, int *budget) {
    if (!css_compound(d, x, s + parts[k - 1].start, parts[k - 1].len, depth, budget)) return 0;
    if (k == 1) return 1;
    char comb = parts[k - 1].comb;
    if (comb == '>') { uint32_t y = css_parent_element(d, x); return y && css_parts_match(d, y, s, parts, k - 1, depth, budget); }
    if (comb == '+') { uint32_t y = css_prev_element(d, x); return y && css_parts_match(d, y, s, parts, k - 1, depth, budget); }
    if (comb == '~') {
        for (uint32_t y = css_prev_element(d, x); y && *budget >= 0; y = css_prev_element(d, y))
            if (css_parts_match(d, y, s, parts, k - 1, depth, budget)) return 1;
        return 0;
    }
    for (uint32_t y = css_parent_element(d, x); y && *budget >= 0; y = css_parent_element(d, y))
        if (css_parts_match(d, y, s, parts, k - 1, depth, budget)) return 1;
    return 0;
}

static int css_complex_matches(const dom_document *d, uint32_t x, const char *s, size_t n, int depth, int *budget) {
    css_part parts[CSS_MAXPARTS];
    size_t k = css_split(s, n, parts, CSS_MAXPARTS);
    return k > 0 && css_parts_match(d, x, s, parts, k, depth, budget);
}

/* ---- the cascade ---- */

static int css_inherited(css_prop p) {
    return p == CSS_PROP_COLOR || p == CSS_PROP_FONT_SIZE || p == CSS_PROP_FONT_WEIGHT || p == CSS_PROP_FONT_STYLE ||
           p == CSS_PROP_TEXT_ALIGN || p == CSS_PROP_LINE_HEIGHT || p == CSS_PROP_WHITE_SPACE || p == CSS_PROP_LIST_STYLE_TYPE ||
           p == CSS_PROP_FONT_FAMILY;
}

/* a property's initial value, as computed */
static void css_initial(css_prop p, css_val *v) {
    memset(v, 0, sizeof *v);
    v->unit = CSS_UNIT_PX;
    if (p == CSS_PROP_DISPLAY) css_word_val(v, "inline");
    else if (p == CSS_PROP_COLOR || p == CSS_PROP_BORDER_COLOR) { v->unit = CSS_UNIT_COLOR; v->color = 0x000000ff; }
    else if (p == CSS_PROP_BACKGROUND_COLOR) v->unit = CSS_UNIT_COLOR;
    else if (p == CSS_PROP_FONT_SIZE) v->num = 16;
    else if (p == CSS_PROP_FONT_WEIGHT) { v->unit = CSS_UNIT_NUMBER; v->num = 400; }
    else if (p == CSS_PROP_FONT_STYLE || p == CSS_PROP_LINE_HEIGHT || p == CSS_PROP_WHITE_SPACE) css_word_val(v, "normal");
    else if (p == CSS_PROP_TEXT_ALIGN) css_word_val(v, "start");
    else if (p == CSS_PROP_TEXT_DECORATION) css_word_val(v, "none");
    else if (p == CSS_PROP_WIDTH || p == CSS_PROP_HEIGHT) v->unit = CSS_UNIT_AUTO;
    else if (p == CSS_PROP_LIST_STYLE_TYPE) css_word_val(v, "disc");
    else if (p == CSS_PROP_FONT_FAMILY) css_word_val(v, "serif");     /* as browsers have it */
    else if (p == CSS_PROP_BOX_SIZING) css_word_val(v, "content-box");
    else if (p == CSS_PROP_MAX_WIDTH) css_word_val(v, "none");
}
/* the initial value at an element: border-color's is currentcolor, the element's colour */
static void css_initial_at(css_prop p, const css_val *cur, css_val *v) {
    css_initial(p, v);
    if (p == CSS_PROP_BORDER_COLOR) v->color = cur[CSS_PROP_COLOR].color;
}

/* the computed value, as CSS serializes it */
static size_t css_value_text(const css_val *v, char *buf, size_t cap) {
    int n;
    if (v->unit == CSS_UNIT_PX) n = snprintf(buf, cap, "%gpx", v->num);
    else if (v->unit == CSS_UNIT_PERCENT) n = snprintf(buf, cap, "%g%%", v->num);
    else if (v->unit == CSS_UNIT_NUMBER) n = snprintf(buf, cap, "%g", v->num);
    else if (v->unit == CSS_UNIT_EM) n = snprintf(buf, cap, "%gem", v->num);
    else if (v->unit == CSS_UNIT_AUTO) n = snprintf(buf, cap, "auto");
    else if (v->unit == CSS_UNIT_COLOR) {
        uint32_t c = v->color;
        if ((c & 0xff) == 0xff) n = snprintf(buf, cap, "rgb(%u, %u, %u)", c >> 24, (c >> 16) & 0xff, (c >> 8) & 0xff);
        else n = snprintf(buf, cap, "rgba(%u, %u, %u, %g)", c >> 24, (c >> 16) & 0xff, (c >> 8) & 0xff, (double)(c & 0xff) / 255.0);
    } else n = snprintf(buf, cap, "%s", v->word);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* font-size's keywords (CSS Fonts 4, 2.5), against the parent's size for smaller and larger */
static double css_size_keyword(const char *w, double parent) {
    if (strcmp(w, "xx-small") == 0) return 9;
    if (strcmp(w, "x-small") == 0) return 10;
    if (strcmp(w, "small") == 0) return 13;
    if (strcmp(w, "large") == 0) return 18;
    if (strcmp(w, "x-large") == 0) return 24;
    if (strcmp(w, "xx-large") == 0) return 32;
    if (strcmp(w, "smaller") == 0) return parent / 1.2;
    if (strcmp(w, "larger") == 0) return parent * 1.2;
    return 16;
}

/* a declared value made computed: pv the parent's computed values, cur the element's
   computed so far (its font size and colour come first) */
static void css_compute(css_prop p, css_val *v, const css_val *pv, const css_val *cur) {
    double pfs = pv[CSS_PROP_FONT_SIZE].num, fs = cur[CSS_PROP_FONT_SIZE].num;
    if (p == CSS_PROP_FONT_SIZE) {
        if (v->unit == CSS_UNIT_EM) v->num *= pfs;
        else if (v->unit == CSS_UNIT_PERCENT) v->num = pfs * v->num / 100;
        else if (v->unit == CSS_UNIT_KEYWORD) v->num = css_size_keyword(v->word, pfs);
        v->unit = CSS_UNIT_PX;
        return;
    }
    if (p == CSS_PROP_FONT_WEIGHT) {
        if (v->unit != CSS_UNIT_KEYWORD) return;
        double w = pv[CSS_PROP_FONT_WEIGHT].num, r = 400;
        if (strcmp(v->word, "bold") == 0) r = 700;
        else if (strcmp(v->word, "bolder") == 0) r = w < 350 ? 400 : w < 550 ? 700 : w < 900 ? 900 : w;
        else if (strcmp(v->word, "lighter") == 0) r = w < 100 ? w : w < 550 ? 100 : w < 750 ? 400 : 700;
        v->unit = CSS_UNIT_NUMBER;
        v->num = r;
        return;
    }
    if (css_is_word(v, "currentcolor")) {
        /* color's currentcolor is its parent's colour; another property's is the element's */
        v->unit = CSS_UNIT_COLOR;
        v->color = p == CSS_PROP_COLOR ? pv[CSS_PROP_COLOR].color : cur[CSS_PROP_COLOR].color;
        return;
    }
    if (p == CSS_PROP_LINE_HEIGHT) {
        if (v->unit == CSS_UNIT_EM) { v->unit = CSS_UNIT_PX; v->num *= fs; }
        else if (v->unit == CSS_UNIT_PERCENT) { v->unit = CSS_UNIT_PX; v->num = fs * v->num / 100; }
        return;
    }
    if (v->unit == CSS_UNIT_KEYWORD && p >= CSS_PROP_BORDER_TOP_WIDTH && p <= CSS_PROP_BORDER_LEFT_WIDTH) {
        v->num = strcmp(v->word, "thin") == 0 ? 1 : strcmp(v->word, "thick") == 0 ? 5 : 3;
        v->unit = CSS_UNIT_PX;
        return;
    }
    if (v->unit == CSS_UNIT_EM) { v->unit = CSS_UNIT_PX; v->num *= fs; }
    else if (v->unit == CSS_UNIT_NUMBER) v->unit = CSS_UNIT_PX;
}

/* a declaration's place in the cascade, larger winning: origin and importance with
   element-attached style above rules, then specificity, then order of appearance */
static uint64_t css_rank(const css_style *s, uint32_t dc) {
    uint32_t r = s->decl[dc].rule;
    css_origin o = s->sheet[s->rule[r].sheet].origin;
    int imp = s->decl[dc].important;
    uint64_t layer = o == CSS_ORIGIN_USER_AGENT ? (imp ? 6 : 1) : (uint64_t)((imp ? 4 : 2) + (o == CSS_ORIGIN_INLINE));
    return layer << 56 | (uint64_t)s->rule[r].spec << 24 | s->rule[r].order;
}

static void css_decl_value(const css_style *s, uint32_t dc, css_val *v) {
    memset(v, 0, sizeof *v);
    v->unit = s->decl[dc].unit;
    v->num = s->decl[dc].num;
    v->color = s->decl[dc].color;
    if (s->decl[dc].word) snprintf(v->word, sizeof v->word, "%s", names_ptr(s, s->decl[dc].word));
}

static void css_record(css_style *s, dom_node_h nh, css_prop p, const css_val *v, css_how how, uint32_t winner) {
    char buf[64];
    size_t bn = css_value_text(v, buf, sizeof buf);
    amb_span t = text_add(s, buf, bn);
    uint32_t w = v->unit == CSS_UNIT_KEYWORD ? names_intern(s, v->word, strlen(v->word)) : 0;
    css_cval_h h = cval_new(s);
    s->cval[h.index].node = nh;
    s->cval[h.index].prop = p;
    s->cval[h.index].unit = v->unit;
    s->cval[h.index].num = v->num;
    s->cval[h.index].color = v->color;
    s->cval[h.index].word = w;
    s->cval[h.index].text = t;
    s->cval[h.index].how = how;
    s->cval[h.index].winner = winner;
}

/* an element's computed values from its matched rules' declarations, pv its parent's
   (or the initial values, at the root); recorded, and written into out */
static void css_element(css_style *s, dom_node_h nh, const vector<uint32_t> *matched, const css_val *pv, int has_parent, css_val *out) {
    uint32_t win[CSS_NPROPS];
    uint64_t best[CSS_NPROPS];
    memset(win, 0, sizeof win);
    memset(best, 0, sizeof best);
    for (size_t k = 0; k < len(*matched); k++) {
        uint32_t r = at(*matched, k);
        for (uint32_t dc = s->rule[r].first_decl; dc; dc = s->decl[dc].next) {
            int p = (int)s->decl[dc].prop;
            uint64_t rk = css_rank(s, dc);
            if (rk >= best[p]) { best[p] = rk; win[p] = dc; }
        }
    }
    /* font-size first, for the em of the others; then color, for their currentcolor */
    static const int order[CSS_NPROPS] = { 3, 1, 0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };
    for (int oi = 0; oi < CSS_NPROPS; oi++) {
        css_prop p = (css_prop)order[oi];
        uint32_t w = win[p];
        css_val v;
        css_how how;
        if (w) {
            css_decl_value(s, w, &v);
            int unset = css_is_word(&v, "unset") || css_is_word(&v, "revert");
            int inherit = css_is_word(&v, "inherit") || (unset && css_inherited(p));
            int initial = css_is_word(&v, "initial") || (unset && !css_inherited(p));
            if (inherit && has_parent) { v = pv[p]; how = CSS_HOW_INHERITED; }
            else if (inherit || initial) { css_initial_at(p, out, &v); how = CSS_HOW_INITIAL; }
            else { css_compute(p, &v, pv, out); how = CSS_HOW_DECLARED; }
        } else if (css_inherited(p) && has_parent) { v = pv[p]; how = CSS_HOW_INHERITED; }
        else { css_initial_at(p, out, &v); how = CSS_HOW_INITIAL; }
        out[p] = v;
        css_record(s, nh, p, &v, how, w);
    }
}

/* a rule of an element's own -- a presentational hint's, or its style attribute's --
   in the sheet *sheet (made at the first) */
static uint32_t css_own_rule(css_style *s, uint32_t *sheet, css_origin origin, const char *what, size_t wn, int first) {
    if (!*sheet) {
        css_sheet_h sh = sheet_new(s);
        s->sheet[sh.index].origin = origin;
        *sheet = sh.index;
    }
    amb_span sp = text_add(s, what, wn);
    css_rule_h r = rule_new(s);
    s->rule[r.index].selector = sp;
    s->rule[r.index].order = first ? 0 : r.index;
    rules_append(s, *sheet, r.index);
    return r.index;
}

/* the presentational hints (HTML 15.3) the module reads: which elements, which attribute, which property */
typedef struct css_hint { const char *elements; const char *attr; css_prop prop; int kind; } css_hint;
enum { CSS_HINT_COLOR, CSS_HINT_LENGTH, CSS_HINT_ALIGN, CSS_HINT_FONT_SIZE, CSS_HINT_BORDER };
static const css_hint css_hints[] = {
    { "body table tr td th thead tbody tfoot", "bgcolor", CSS_PROP_BACKGROUND_COLOR, CSS_HINT_COLOR },
    { "body", "text", CSS_PROP_COLOR, CSS_HINT_COLOR },
    { "font", "color", CSS_PROP_COLOR, CSS_HINT_COLOR },
    { "font", "size", CSS_PROP_FONT_SIZE, CSS_HINT_FONT_SIZE },
    { "table td th col img hr iframe embed object video canvas", "width", CSS_PROP_WIDTH, CSS_HINT_LENGTH },
    { "table tr td th img iframe embed object video canvas", "height", CSS_PROP_HEIGHT, CSS_HINT_LENGTH },
    { "td th tr thead tbody tfoot caption div p h1 h2 h3 h4 h5 h6", "align", CSS_PROP_TEXT_ALIGN, CSS_HINT_ALIGN },
    { "table", "border", CSS_PROP_BORDER_TOP_WIDTH, CSS_HINT_BORDER } };

/* an attribute's value read as a hint of its kind */
static int css_hint_value(int kind, const char *t, size_t n, css_val *v) {
    size_t a = css_skip_ws(t, n, 0), b = n;
    while (b > a && css_ws(t[b - 1])) b--;
    t += a;
    n = b - a;
    memset(v, 0, sizeof *v);
    if (n == 0) return 0;
    if (kind == CSS_HINT_COLOR) {
        if (css_eq_ci(t, n, "transparent")) return 0;
        if (css_parse_color(t, n, &v->color)) { v->unit = CSS_UNIT_COLOR; return 1; }
        char h[16];
        if (n != 3 && n != 6) return 0;         /* hex digits without their # */
        h[0] = '#';
        memcpy(h + 1, t, n);
        if (!css_parse_color(h, n + 1, &v->color)) return 0;
        v->unit = CSS_UNIT_COLOR;
        return 1;
    }
    double x;
    size_t used = css_number(t, n, &x);
    if (kind == CSS_HINT_LENGTH || kind == CSS_HINT_BORDER) {
        if (!used || x < 0 || t[0] == '+' || t[0] == '-') return 0;
        v->num = x;
        v->unit = kind == CSS_HINT_LENGTH && used < n && t[used] == '%' ? CSS_UNIT_PERCENT : CSS_UNIT_PX;
        return 1;
    }
    if (kind == CSS_HINT_FONT_SIZE) {
        static const double px[8] = { 0, 10, 13, 16, 18, 24, 32, 48 };
        if (!used) return 0;
        int k = (int)x;
        if (t[0] == '+' || t[0] == '-') k += 3;
        if (k < 1) k = 1;
        if (k > 7) k = 7;
        v->unit = CSS_UNIT_PX;
        v->num = px[k];
        return 1;
    }
    if (css_eq_ci(t, n, "middle")) { css_word_val(v, "center"); return 1; }
    if (css_eq_ci(t, n, "left") || css_eq_ci(t, n, "right") || css_eq_ci(t, n, "center") || css_eq_ci(t, n, "justify")) {
        css_word_val(v, "");
        for (size_t i = 0; i < n; i++) v->word[i] = css_lower(t[i]);
        return 1;
    }
    return 0;
}

static void css_element_hints(css_style *s, const dom_document *d, uint32_t x, uint32_t *sheet, vector<uint32_t> *matched) {
    for (size_t h = 0; h < sizeof css_hints / sizeof css_hints[0]; h++) {
        if (!css_word_in(names_ptr(d, d->node[x].name), css_hints[h].elements)) continue;
        uint32_t at = css_attr_of(d, x, css_hints[h].attr, strlen(css_hints[h].attr));
        if (!at) continue;
        amb_span sp = d->attr[at].value;
        css_val v;
        if (!css_hint_value(css_hints[h].kind, text_ptr(d, sp), sp.len, &v)) continue;
        char what[64], text[160];
        int wn = snprintf(what, sizeof what, "the %s attribute", css_hints[h].attr);
        int tn = snprintf(text, sizeof text, "%s=%.*s", css_hints[h].attr, (int)(sp.len > 100 ? 100 : sp.len), text_ptr(d, sp));
        if (wn < 0 || tn < 0) continue;
        if ((size_t)tn >= sizeof text) tn = (int)sizeof text - 1;
        /* author-level, specificity 0, before every other author rule: order 0 */
        uint32_t r = css_own_rule(s, sheet, CSS_ORIGIN_AUTHOR, what, (size_t)wn, 1);
        if (css_hints[h].kind == CSS_HINT_BORDER)
            for (int side = 0; side < 4; side++) css_add_decl(s, r, (css_prop)(CSS_PROP_BORDER_TOP_WIDTH + side), &v, 0, text, (size_t)tn);
        else css_add_decl(s, r, css_hints[h].prop, &v, 0, text, (size_t)tn);
        push_back(matched, r);
    }
}

static void css_style_attr(css_style *s, const dom_document *d, uint32_t x, uint32_t *sheet, vector<uint32_t> *matched) {
    uint32_t at = css_attr_of(d, x, "style", 5);
    if (!at) return;
    uint32_t r = css_own_rule(s, sheet, CSS_ORIGIN_INLINE, "the style attribute", 19, 0);
    amb_span sp = d->attr[at].value;
    vector<char> t;
    css_strip_comments(text_ptr(d, sp), sp.len, &t);
    css_parse_block(s, r, data(t), len(t));
    push_back(matched, r);
}

/* the stylesheet the loader fetched for the link element x: its record, or 0 */
static uint32_t css_linked_sheet(const net_loader *net, uint32_t x) {
    for (uint32_t r = 1; r < net->resource_len; r++)
        if (net->resource[r].live && net->resource[r].elem.index == x && net->resource[r].kind == NET_KIND_STYLESHEET &&
            net->resource[r].outcome == NET_OUTCOME_OK) return r;
    return 0;
}

css_status css_cascade(css_style *s, dom_document *doc, net_loader *net) {
    /* the user agent's sheet, then the document's style elements and linked sheets, in tree order */
    css_parse_sheet(s, CSS_ORIGIN_USER_AGENT, css_ua_sheet, sizeof css_ua_sheet - 1);
    for (uint32_t x = css_next_in_tree(doc, 1); x; x = css_next_in_tree(doc, x)) {
        int link = css_is_named(doc, x, "link");
        if (!css_is_named(doc, x, "style") && !link) continue;
        uint32_t ma = css_attr_of(doc, x, "media", 5);
        if (ma && !css_media_matches(text_ptr(doc, doc->attr[ma].value), doc->attr[ma].value.len)) continue;
        if (link) {
            uint32_t r = net ? css_linked_sheet(net, x) : 0;
            if (!r) continue;
            vector<char> body;
            append_range(body, bytes_ptr(net, net->resource[r].body), net->resource[r].body.len);
            css_parse_sheet(s, CSS_ORIGIN_AUTHOR, data(body), len(body));
            continue;
        }
        vector<char> t;
        for (uint32_t ch = doc->node[x].first_child; ch; ch = doc->node[ch].next_sibling)
            if (doc->node[ch].kind == DOM_NODE_KIND_TEXT) append_range(t, text_ptr(doc, doc->node[ch].data), doc->node[ch].data.len);
        css_parse_sheet(s, CSS_ORIGIN_AUTHOR, data(t), len(t));
    }
    uint32_t shared = (uint32_t)s->rule_len;
    css_val initial[CSS_NPROPS];
    for (int k = 0; k < CSS_NPROPS; k++) css_initial((css_prop)k, &initial[k]);
    /* each element, parents before children: its rules, its hints and style attribute, its values */
    vector<css_val> vals;
    resize(vals, (size_t)doc->node_len * CSS_NPROPS);
    vector<uint32_t> matched;
    uint32_t hint_sheet = 0, inline_sheet = 0;
    for (uint32_t x = css_next_in_tree(doc, 1); x; x = css_next_in_tree(doc, x)) {
        if (!css_is_element(doc, x)) continue;
        clear(matched);
        for (uint32_t r = 1; r < shared; r++) {
            amb_span sel = s->rule[r].selector;
            int budget = CSS_MATCH_BUDGET;
            if (css_complex_matches(doc, x, text_ptr(s, sel), sel.len, 0, &budget)) push_back(matched, r);
        }
        css_element_hints(s, doc, x, &hint_sheet, &matched);
        css_style_attr(s, doc, x, &inline_sheet, &matched);
        dom_node_h nh = node_handle(doc, x);
        for (size_t k = 0; k < len(matched); k++) {
            css_match_h m = match_new(s);
            s->match[m.index].node = nh;
            s->match[m.index].rule = at(matched, k);
        }
        uint32_t par = css_parent_element(doc, x);
        css_val pv[CSS_NPROPS], out[CSS_NPROPS];
        for (int k = 0; k < CSS_NPROPS; k++) pv[k] = par ? at(vals, (size_t)par * CSS_NPROPS + k) : initial[k];
        memset(out, 0, sizeof out);
        css_element(s, nh, &matched, pv, par != 0, out);
        for (int k = 0; k < CSS_NPROPS; k++) vals[(size_t)x * CSS_NPROPS + k] = out[k];
    }
    s->revision++;
    return CSS_OK;
}
