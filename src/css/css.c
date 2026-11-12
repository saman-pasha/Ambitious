/* src/css/css.c -- the CSS module (contracts/css.pl): style sheets parsed,
   selectors matched against the DOM, the cascade run, and each element's
   computed values kept with where they came from; and the module's generated
   implementation.

   Parsing has CSS Syntax 3's shapes: comments, strings, the top level's <!-- and
   -->, at-rules (an @media whose query holds for the viewport, an @supports
   whose condition does, @layer, an @import fetched through the loader, and
   @font-face are read; every other at-rule skipped), style rules, declarations
   with !important. The 106
   longhands of css_pdefs are read, each by its grammar: keywords, lengths and
   percentages, colours, numbers, and the values kept as their text (track
   lists, grid lines and areas, images and gradients, shadows, transforms, content,
   counters, quotes), each checked by a reader of its own. A shorthand is
   expanded into its longhands (css_shorts: margin, padding, inset, the border
   shorthands and border-radius, outline, font, background's layers,
   list-style, text-decoration, overflow, flex, flex-flow, gap, place-*,
   grid-row, grid-column, grid-area, grid-template). A value a property does
   not take drops its declaration, as CSS has it.

   Values: px and the absolute units, em, ex and ch (half an em), rem (the
   root's font size), the viewport's units against the reader's viewport (800 x 600
   over the zoom: css_set_env, which also gives the default font size and the
   prefers-color-scheme and prefers-reduced-motion media features); calc(), min(),
   max() and clamp() over them, reckoned once the font is known, a percentage
   kept beside px for layout (under min(), max() and clamp(), the expression
   kept as text, its lengths made px). Custom properties (--name) are
   inherited, their var()s replaced at computed-value time: a cycle, or a var()
   of nothing with no fallback, makes a value invalid at computed-value time,
   and its property is then unset.

   Selectors 4: type, universal, #id, .class, [a], [a=v], [a~=v], [a|=v],
   [a^=v], [a$=v], [a*=v] (their i and s flags; HTML's attributes whose values
   compare without case), :first-child, :last-child,
   :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(),
   :nth-last-child(), :nth-of-type(), :nth-last-of-type(), :not(), :is(),
   :where(), :has() (its relative selectors over the subject's descendants, for
   ' ' and >, or its later siblings and theirs, for + and ~; at most 20000
   elements searched a :has()), :root, :empty, :link, :any-link, :checked,
   :disabled, :enabled; the combinators ' ', >, + and ~. The pseudo-classes of
   state read the page's state (css_set_state; the command's --hover, --focus and
   --active): :hover and :active hold of the element in that state and its
   ancestors, :focus and :focus-visible of the focused element, :focus-within of
   it and its ancestors, :target of the element the URL's fragment names; a page
   at rest has none of these, and :visited holds of nothing (no history is read).
   :not(), :is(), :where() and :has() nest at most 16 deep, and one match tests at
   most 4096 compounds: past either, the selector matches nothing.

   Pseudo-elements: ::before, ::after, ::first-line and ::first-letter (CSS 2's
   single colon too), ::marker and ::placeholder. A rule whose subject ends in one
   is matched as its element is, and makes a style under the element's
   (css_pseudo_style_of): a ::before's or an ::after's only when its content is
   something, as only then is it generated.

   The cascade (Cascade 4): origin and importance, element-attached style over
   rules, specificity, order of appearance; then inheritance and initial
   values. Presentational hints (HTML 15.3: bgcolor, text, color, size, width,
   height, align, a table's border; dir, as Chromium maps it) are author rules of specificity 0 before
   every other author rule. A computed value is as each property's spec has
   it: lengths px, percentages kept for layout, currentcolor the colour, a
   border's width 0 under style none, display blockified for a float, an
   absolute box, the root and a flex or grid item. Two differ from Chromium's:
   an outline's width under style none is kept (as Chromium keeps it), and a
   computed font family is the family drawn with -- a generic one, or the first
   the list names that the module knows the kind of.

   Speed (PLAN.md 11): the user agent's sheet is read once a process
   (css_ua_load); names and keywords are found by hash; elements under one
   parent style start from that style's template of inherited and initial
   values, so only the declarations that won are computed. The DOM is read
   through its fields, never changed. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_implementation(css);
#include "css.h"
#include "../net/url.h"

#define CSS_MAXPARTS 32
/* the reader's settings the cascade reads (css_set_env, from the profile's preferences):
   the viewport media queries and the viewport units read, in CSS px (the window over
   the zoom); the default font size (medium, font-size's initial value, rem's at the root,
   a media query's em); prefers-color-scheme and prefers-reduced-motion */
static double css_view_w = 800.0, css_view_h = 600.0, css_font_px = 16.0;
static int css_pref_dark = 0, css_pref_reduce = 0;
/* scripting (Media Queries 5, 7.1): whether the page's scripts run; they do, as Chromium's */
static int css_scripting = 1;
void css_set_scripting(int on) { css_scripting = on != 0; }
#define CSS_VIEWPORT_W css_view_w
#define CSS_VIEWPORT_H css_view_h

void css_set_env(double view_w, double view_h, double font_size, int dark, int reduced_motion) {
    css_view_w = view_w > 0 ? view_w : 800.0;
    css_view_h = view_h > 0 ? view_h : 600.0;
    css_font_px = font_size > 0 ? font_size : 16.0;
    css_pref_dark = dark != 0;
    css_pref_reduce = reduced_motion != 0;
}
/* a selector's functional pseudo-classes nest at most so deep, and one match of an
   element against a selector tests at most so many compounds: a selector made to
   recurse or to backtrack without end matches nothing, and costs no more */
#define CSS_MAX_NESTING 16
#define CSS_MATCH_BUDGET 4096

/* ---- the user agent's style sheet: the HTML Standard's rendering section (15.3), abridged ---- */
static const char css_ua_sheet[] =
    "area, base, basefont, datalist, head, link, meta, noembed, noframes, param, rp, script, style, template, title { display: none }\n"
    "[hidden] { display: none }\n"
    "@media (scripting: enabled) { noscript { display: none !important } }\n"
    "html, body { display: block }\n"
    "body { margin: 8px }\n"
    "address, blockquote, center, dialog, div, figure, figcaption, footer, form, header, hr, legend, listing, main, p, plaintext, pre, search, xmp { display: block; unicode-bidi: isolate }\n"
    "blockquote, figure, listing, p, plaintext, pre, xmp { margin-top: 1em; margin-bottom: 1em }\n"
    "blockquote, figure { margin-left: 40px; margin-right: 40px }\n"
    "address { font-style: italic }\n"
    "listing, plaintext, pre, xmp { white-space: pre }\n"
    "code, kbd, listing, plaintext, pre, samp, tt, xmp { font-family: monospace }\n"
    "center { text-align: center }\n"
    "cite, dfn, em, i, var { font-style: italic }\n"
    "b, strong { font-weight: bolder }\n"
    "small { font-size: smaller }\n"
    "sub { vertical-align: sub; font-size: smaller; line-height: normal }\n"
    "sup { vertical-align: super; font-size: smaller; line-height: normal }\n"
    "big { font-size: larger }\n"
    "s, strike, del { text-decoration: line-through }\n"
    "u, ins { text-decoration: underline }\n"
    "mark { background-color: Mark; color: MarkText }\n"
    "nobr { white-space: nowrap }\n"
    ":link { color: LinkText; text-decoration: underline }\n"
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
    "table, td, th { border-color: gray }\n"
    "thead, tbody, tfoot, tr { border-color: inherit }\n"
    "table[border] { border-style: outset }\n"
    "table[border] td, table[border] th { border-width: 1px; border-style: inset }\n"
    "th { font-weight: bold; text-align: center }\n"
    "hr { color: gray; border-style: inset; border-width: 1px; margin: 0.5em auto; overflow: hidden }\n"
    "fieldset { display: block; margin-left: 2px; margin-right: 2px; padding: 0.35em 0.75em 0.625em; border: 2px groove ThreeDFace }\n"
    "legend { padding-left: 2px; padding-right: 2px }\n"
    "details, summary { display: block }\n"
    /* a details element's summary: a list item with its disclosure triangle inside (HTML 15.5.17, Chromium's sheet) */
    "details > summary:first-of-type { display: list-item; counter-increment: list-item 0; list-style: disclosure-closed inside }\n"
    "details[open] > summary:first-of-type { list-style-type: disclosure-open }\n"
    /* a closed details element shows its first summary alone (HTML 15.5.17; its text
       directly inside is layout's to pass over); a dialog without open is not drawn, and
       an open one is Chromium's box: absolutely positioned, centred, its own border and
       canvas (HTML 15.3.3) */
    "details:not([open]) > :not(summary:first-of-type) { display: none }\n"
    "dialog:not([open]) { display: none }\n"
    "dialog { position: absolute; left: 0; right: 0; width: fit-content; height: fit-content; margin: auto; border: solid; padding: 1em; background-color: Canvas; color: CanvasText }\n"
    "button, input, select, textarea { display: inline-block }\n"
    "button { padding: 1px 6px; border: 2px solid light-dark(#767676, #858585); color: ButtonText; background-color: ButtonFace }\n"
    "input, textarea, select { padding: 1px 2px; border: 2px solid light-dark(#767676, #858585); color: FieldText; background-color: Field }\n"
    "select { background-color: ButtonFace }\n"
    "q::before { content: open-quote }\n"
    "q::after { content: close-quote }\n"
    "input::placeholder, textarea::placeholder { color: #757575 }\n"
    "bdi, output { unicode-bidi: isolate }\n"
    "bdo { unicode-bidi: bidi-override }\n";

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

/* a value as the module reads it. tx: in the store's pool, the text of a value kept as
   text (a list, a function, a calc() over percentages, a var()); ck: what a calc()
   reckons, CSS_CK_* */
typedef struct css_val { css_unit unit; double num, pct; uint32_t color, ck; amb_span tx; char word[32]; } css_val;

enum { CSS_CK_NUM = 1, CSS_CK_LEN = 2, CSS_CK_PCT = 4 };

static void css_word_val(css_val *v, const char *w) {
    memset(v, 0, sizeof *v);
    v->unit = CSS_UNIT_KEYWORD;
    snprintf(v->word, sizeof v->word, "%s", w);
}
static int css_is_word(const css_val *v, const char *w) { return v->unit == CSS_UNIT_KEYWORD && strcmp(v->word, w) == 0; }
static int css_is_global(const css_val *v) {
    char c = v->word[0];
    return v->unit == CSS_UNIT_KEYWORD && (c == 'i' || c == 'u' || c == 'r') && css_word_in(v->word, "inherit initial unset revert revert-layer");
}
static int css_is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int css_is_digit(char c) { return c >= '0' && c <= '9'; }

/* a number: its bytes, 0 when there is none */
static size_t css_number(const char *s, size_t n, double *out) {
    size_t i = 0;
    int neg = 0, any = 0;
    double x = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    while (i < n && css_is_digit(s[i])) { x = x * 10 + (s[i] - '0'); i++; any = 1; }
    if (i + 1 < n && s[i] == '.' && css_is_digit(s[i + 1])) {
        double f = 0.1;
        i++;
        while (i < n && css_is_digit(s[i])) { x += (s[i] - '0') * f; f /= 10; i++; any = 1; }
    }
    if (!any) return 0;
    /* an exponent: e, a sign, digits (not the e of em or ex) */
    if (i + 1 < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        int eneg = 0, ex = 0, ed = 0;
        if (j < n && (s[j] == '+' || s[j] == '-')) { eneg = s[j] == '-'; j++; }
        while (j < n && css_is_digit(s[j]) && ed < 4) { ex = ex * 10 + (s[j] - '0'); j++; ed++; }
        if (ed) {
            for (int k = 0; k < ex && k < 300; k++) x = eneg ? x / 10 : x * 10;
            i = j;
        }
    }
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
    /* the named colours (lower case, in order in the table): found by halves */
    if (n >= 3 && n <= 20 && css_is_alpha(s[0])) {
        char w[24];
        for (size_t i = 0; i < n; i++) w[i] = css_lower(s[i]);
        w[n] = 0;
        size_t lo = 0, hi = sizeof css_colors / sizeof css_colors[0];
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            int c = strcmp(css_colors[mid].name, w);
            if (c == 0) { *out = css_colors[mid].rgb << 8 | 0xff; return 1; }
            if (c < 0) lo = mid + 1; else hi = mid;
        }
    }
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

int css_color_parse(const char *s, size_t n, uint32_t *rgba) { return css_parse_color(s, n, rgba); }

/* ---- calc(), min(), max(), clamp() (CSS Values 4, 10) ----
   An expression is reckoned to px + pct% + num, of a kind (CSS_CK_*). em and rem are
   known in the cascade; before it (em < 0) they leave the length open. A percentage
   is of the basis when there is one (layout's), else kept. min(), max() and clamp()
   over percentages cannot be such a sum: nonlin, and the value is reckoned from its
   text where the basis is known. */

typedef struct css_lin { double px, pct, num; uint32_t kind; int open, nonlin; } css_lin;
typedef struct css_cenv { double em, rem, basis; } css_cenv;

/* the px of one of an absolute or viewport unit: 0 when the unit is no such one */
static double css_abs_unit(const char *u, size_t un) {
    if (css_eq_ci(u, un, "px")) return 1;
    if (css_eq_ci(u, un, "pt")) return 4.0 / 3;
    if (css_eq_ci(u, un, "pc")) return 16;
    if (css_eq_ci(u, un, "in")) return 96;
    if (css_eq_ci(u, un, "cm")) return 96 / 2.54;
    if (css_eq_ci(u, un, "mm")) return 96 / 25.4;
    if (css_eq_ci(u, un, "q")) return 96 / 101.6;
    if (css_eq_ci(u, un, "vw") || css_eq_ci(u, un, "svw") || css_eq_ci(u, un, "lvw") || css_eq_ci(u, un, "dvw")) return CSS_VIEWPORT_W / 100;
    if (css_eq_ci(u, un, "vh") || css_eq_ci(u, un, "svh") || css_eq_ci(u, un, "lvh") || css_eq_ci(u, un, "dvh")) return CSS_VIEWPORT_H / 100;
    if (css_eq_ci(u, un, "vmin")) return (CSS_VIEWPORT_W < CSS_VIEWPORT_H ? CSS_VIEWPORT_W : CSS_VIEWPORT_H) / 100;
    if (css_eq_ci(u, un, "vmax")) return (CSS_VIEWPORT_W > CSS_VIEWPORT_H ? CSS_VIEWPORT_W : CSS_VIEWPORT_H) / 100;
    return 0;
}
/* a font-relative unit: 1 for a multiple k of the em (em, ex and ch as half an em), 2
   for the rem; 0 for none */
static int css_font_unit(const char *u, size_t un, double *k) {
    if (css_eq_ci(u, un, "em")) { *k = 1; return 1; }
    if (css_eq_ci(u, un, "ex") || css_eq_ci(u, un, "ch")) { *k = 0.5; return 1; }
    if (css_eq_ci(u, un, "rem")) { *k = 1; return 2; }
    return 0;
}

static int css_calc_sum(const char *s, size_t n, size_t *i, const css_cenv *e, int depth, css_lin *out);

static void css_lin_scale(css_lin *l, double k) { l->px *= k; l->pct *= k; l->num *= k; }

/* min() (which 0), max() (1) or clamp() (2) over the arguments s[a, close) */
static int css_calc_minmax(const char *s, size_t a, size_t close, const css_cenv *e, int depth, int which, css_lin *out) {
    css_lin args[16];
    int k = 0;
    size_t p = a;
    while (p < close) {
        size_t end = css_comma(s, close, p);
        if (k == 16) return 0;
        size_t q = p;
        if (!css_calc_sum(s, end, &q, e, depth, &args[k]) || css_skip_ws(s, end, q) != end) return 0;
        k++;
        p = end + 1;
    }
    if (k == 0 || (which == 2 && k != 3)) return 0;
    int num = args[0].kind == CSS_CK_NUM, pct = 0, open = 0, nonlin = 0;
    uint32_t kind = 0;
    for (int j = 0; j < k; j++) {
        if ((args[j].kind == CSS_CK_NUM) != num) return 0;
        if (args[j].pct != 0 || (args[j].kind & CSS_CK_PCT)) pct = 1;
        open |= args[j].open;
        nonlin |= args[j].nonlin;
        kind |= args[j].kind;
    }
    *out = args[0];
    out->kind = kind;
    if (pct || open || nonlin) { out->open = open; out->nonlin = pct || nonlin; return 1; }
    double v[16];
    for (int j = 0; j < k; j++) v[j] = num ? args[j].num : args[j].px;
    double r = v[0];
    if (which == 2) { r = v[1] < v[2] ? v[1] : v[2]; if (v[0] > r) r = v[0]; }
    else for (int j = 1; j < k; j++) if (which == 0 ? v[j] < r : v[j] > r) r = v[j];
    memset(out, 0, sizeof *out);
    out->kind = kind;
    if (num) out->num = r; else out->px = r;
    return 1;
}

/* a value of an expression: a number, a dimension, a percentage, a parenthesis, a function */
static int css_calc_value(const char *s, size_t n, size_t *i, const css_cenv *e, int depth, css_lin *out) {
    memset(out, 0, sizeof *out);
    size_t p = css_skip_ws(s, n, *i);
    if (p >= n || depth > 32) return 0;
    if (s[p] == '(') {
        size_t q = p + 1;
        if (!css_calc_sum(s, n, &q, e, depth + 1, out)) return 0;
        q = css_skip_ws(s, n, q);
        if (q >= n || s[q] != ')') return 0;
        *i = q + 1;
        return 1;
    }
    if (css_is_alpha(s[p])) {
        size_t q = p;
        while (q < n && css_namechar(s[q])) q++;
        if (q >= n || s[q] != '(') return 0;
        size_t close = css_block_end(s, n, q, '(', ')');
        if (close >= n) return 0;
        const char *f = s + p;
        size_t fn = q - p;
        int ok;
        if (css_eq_ci(f, fn, "calc")) {
            size_t r = q + 1;
            ok = css_calc_sum(s, close, &r, e, depth + 1, out) && css_skip_ws(s, close, r) == close;
        } else if (css_eq_ci(f, fn, "min")) ok = css_calc_minmax(s, q + 1, close, e, depth + 1, 0, out);
        else if (css_eq_ci(f, fn, "max")) ok = css_calc_minmax(s, q + 1, close, e, depth + 1, 1, out);
        else if (css_eq_ci(f, fn, "clamp")) ok = css_calc_minmax(s, q + 1, close, e, depth + 1, 2, out);
        else return 0;
        *i = close + 1;
        return ok;
    }
    double x;
    size_t used = css_number(s + p, n - p, &x);
    if (!used) return 0;
    p += used;
    if (p < n && s[p] == '%') {
        p++;
        if (e->basis >= 0) { out->px = x * e->basis / 100; out->kind = CSS_CK_LEN; }
        else { out->pct = x; out->kind = CSS_CK_PCT; }
        *i = p;
        return 1;
    }
    size_t ue = p;
    while (ue < n && css_is_alpha(s[ue])) ue++;
    if (ue == p) { out->num = x; out->kind = CSS_CK_NUM; *i = p; return 1; }
    out->kind = CSS_CK_LEN;
    double k = css_abs_unit(s + p, ue - p);
    if (k > 0) out->px = x * k;
    else {
        int fu = css_font_unit(s + p, ue - p, &k);
        if (!fu) return 0;
        double base = fu == 2 ? e->rem : e->em;
        if (base < 0) out->open = 1;
        else out->px = x * k * base;
    }
    *i = ue;
    return 1;
}

static int css_calc_product(const char *s, size_t n, size_t *i, const css_cenv *e, int depth, css_lin *out) {
    if (!css_calc_value(s, n, i, e, depth, out)) return 0;
    for (;;) {
        size_t p = css_skip_ws(s, n, *i);
        if (p >= n || (s[p] != '*' && s[p] != '/')) return 1;
        char op = s[p];
        p++;
        css_lin b;
        if (!css_calc_value(s, n, &p, e, depth, &b)) return 0;
        if (op == '*') {
            if (b.kind == CSS_CK_NUM && !b.nonlin) css_lin_scale(out, b.num);
            else if (out->kind == CSS_CK_NUM && !out->nonlin) { double k = out->num; *out = b; css_lin_scale(out, k); }
            else return 0;
        } else {
            if (b.kind != CSS_CK_NUM || b.nonlin || b.num == 0) return 0;
            css_lin_scale(out, 1 / b.num);
        }
        out->open |= b.open;
        *i = p;
    }
}

static int css_calc_sum(const char *s, size_t n, size_t *i, const css_cenv *e, int depth, css_lin *out) {
    if (!css_calc_product(s, n, i, e, depth, out)) return 0;
    for (;;) {
        size_t p = css_skip_ws(s, n, *i);
        if (p >= n || (s[p] != '+' && s[p] != '-')) return 1;
        double k = s[p] == '-' ? -1 : 1;
        p++;
        css_lin b;
        if (!css_calc_product(s, n, &p, e, depth, &b)) return 0;
        if ((out->kind == CSS_CK_NUM) != (b.kind == CSS_CK_NUM)) return 0;
        out->px += k * b.px;
        out->pct += k * b.pct;
        out->num += k * b.num;
        out->kind |= b.kind;
        out->open |= b.open;
        out->nonlin |= b.nonlin;
        *i = p;
    }
}

/* s is one calc(), min(), max() or clamp() */
static int css_calc_fn(const char *s, size_t n) {
    size_t f = 0;
    while (f < n && css_namechar(s[f])) f++;
    if (f >= n || s[f] != '(' || s[n - 1] != ')' || css_block_end(s, n, f, '(', ')') != n - 1) return 0;
    return css_eq_ci(s, f, "calc") || css_eq_ci(s, f, "min") || css_eq_ci(s, f, "max") || css_eq_ci(s, f, "clamp");
}

int css_calc_eval(const char *s, size_t n, double basis, double *out) {
    css_cenv e = { 16, 16, basis };
    css_lin l;
    size_t i = 0;
    if (!css_calc_value(s, n, &i, &e, 0, &l) || css_skip_ws(s, n, i) != n) return 0;
    *out = l.kind == CSS_CK_NUM ? l.num : l.px + l.pct * (basis > 0 ? basis : 0) / 100;
    return 1;
}

int css_length_eval(const char *s, size_t n, double basis, double *out) {
    css_cenv e = { 16, 16, basis };
    css_lin l;
    size_t i = 0;
    if (!css_calc_value(s, n, &i, &e, 0, &l) || css_skip_ws(s, n, i) != n) return 0;
    if (l.kind == CSS_CK_NUM) { if (l.num != 0) return 0; *out = 0; return 1; }
    *out = l.px + l.pct * (basis > 0 ? basis : 0) / 100;
    return 1;
}

int css_next_part(const char *s, size_t n, size_t *i, size_t *start, size_t *len) {
    size_t p = *i;
    while (p < n && (css_ws(s[p]) || s[p] == ',')) p++;
    if (p >= n) { *i = n; return 0; }
    size_t b = p;
    while (p < n && !css_ws(s[p]) && s[p] != ',') {
        if (s[p] == '(') p = css_block_end(s, n, p, '(', ')');
        else if (s[p] == '"' || s[p] == '\'') p = css_string_end(s, n, p);
        if (p < n) p++;
    }
    *start = b;
    *len = p - b;
    *i = p;
    return 1;
}

/* a value: a length (absolute and viewport units made px; em, ex, ch as em; rem), a
   percentage, a number, auto, a colour, a calc(), or a keyword */
static int css_parse_val(const char *s, size_t n, css_val *v) {
    memset(v, 0, sizeof *v);
    if (n == 0) return 0;
    if (css_eq_ci(s, n, "auto")) { v->unit = CSS_UNIT_AUTO; return 1; }
    if (css_calc_fn(s, n)) {
        /* reckoned now when nothing in it waits on the font or the basis; else its text,
           the declaration's, is reckoned in the cascade */
        css_cenv e = { -1, -1, -1 };
        css_lin l;
        size_t i = 0;
        if (!css_calc_value(s, n, &i, &e, 0, &l) || css_skip_ws(s, n, i) != n) return 0;
        v->ck = l.kind;
        if (l.kind == CSS_CK_NUM && !l.nonlin) { v->unit = CSS_UNIT_NUMBER; v->num = l.num; return 1; }
        if (!l.open && !l.nonlin && !(l.kind & CSS_CK_PCT)) { v->unit = CSS_UNIT_PX; v->num = l.px; return 1; }
        if (!l.open && !l.nonlin && l.kind == CSS_CK_PCT) { v->unit = CSS_UNIT_PERCENT; v->num = l.pct; return 1; }
        v->unit = CSS_UNIT_CALC;
        return 1;
    }
    double x;
    size_t used = css_number(s, n, &x);
    if (used) {
        const char *u = s + used;
        size_t un = n - used;
        v->num = x;
        double k;
        if (un == 0) v->unit = CSS_UNIT_NUMBER;
        else if (css_eq_ci(u, un, "%")) v->unit = CSS_UNIT_PERCENT;
        else if (css_eq_ci(u, un, "rem")) v->unit = CSS_UNIT_REM;
        else if (css_font_unit(u, un, &k) == 1) { v->unit = CSS_UNIT_EM; v->num = x * k; }
        else if ((k = css_abs_unit(u, un)) > 0) { v->unit = CSS_UNIT_PX; v->num = x * k; }
        else return 0;
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

/* ---- the properties (contracts/css.pl's enum(prop), in its order) ----
   Each with its name, whether it is inherited, its grammar, and its initial value as
   CSS writes it. A grammar's words are the keywords it takes; G_TEXT values are kept as
   their text, each property's reader checking it (css_text_valid). */

enum { G_KW, G_LEN, G_COLOR, G_NUM, G_TEXT, G_FAMILY, G_RADIUS };
#define F_NEG 1         /* a negative length or number */
#define F_PCT 2         /* a percentage */
#define F_INT 4         /* an integer only */
#define F_NUM 8         /* G_LEN: a number besides (line-height) */
typedef struct css_pdef { const char *name; uint8_t inh, gram, flags; const char *words; const char *init; } css_pdef;

#define CSS_W_BSTYLE "none hidden dotted dashed solid double groove ridge inset outset"
#define CSS_W_SIZE "auto min-content max-content fit-content"
#define CSS_W_SELF "auto normal stretch start end center flex-start flex-end self-start self-end left right baseline"
static const css_pdef css_pdefs[CSS_NPROPS] = {
    { "display", 0, G_KW, 0, "inline block list-item inline-block none flex inline-flex grid inline-grid flow-root contents table "
      "inline-table table-row table-cell table-row-group table-header-group table-footer-group table-caption table-column "
      "table-column-group -webkit-box -webkit-inline-box", "inline" },
    { "color", 1, G_COLOR, 0, 0, "black" },
    { "background-color", 0, G_COLOR, 0, 0, "transparent" },
    { "font-size", 1, G_LEN, F_PCT, "xx-small x-small small medium large x-large xx-large xxx-large smaller larger", "16px" },
    { "font-weight", 1, G_NUM, 0, "normal bold bolder lighter", "400" },
    { "font-style", 1, G_KW, 0, "normal italic oblique", "normal" },
    { "text-align", 1, G_KW, 0, "left right center justify start end", "start" },
    { "line-height", 1, G_LEN, F_PCT | F_NUM, "normal", "normal" },
    { "text-decoration-line", 0, G_KW, 0, "none underline overline line-through", "none" },
    { "white-space", 1, G_KW, 0, "normal pre nowrap pre-wrap pre-line break-spaces", "normal" },
    { "margin-top", 0, G_LEN, F_NEG | F_PCT, "auto", "0px" },
    { "margin-right", 0, G_LEN, F_NEG | F_PCT, "auto", "0px" },
    { "margin-bottom", 0, G_LEN, F_NEG | F_PCT, "auto", "0px" },
    { "margin-left", 0, G_LEN, F_NEG | F_PCT, "auto", "0px" },
    { "padding-top", 0, G_LEN, F_PCT, 0, "0px" },
    { "padding-right", 0, G_LEN, F_PCT, 0, "0px" },
    { "padding-bottom", 0, G_LEN, F_PCT, 0, "0px" },
    { "padding-left", 0, G_LEN, F_PCT, 0, "0px" },
    { "border-top-width", 0, G_LEN, 0, "thin medium thick", "medium" },
    { "border-right-width", 0, G_LEN, 0, "thin medium thick", "medium" },
    { "border-bottom-width", 0, G_LEN, 0, "thin medium thick", "medium" },
    { "border-left-width", 0, G_LEN, 0, "thin medium thick", "medium" },
    { "border-top-style", 0, G_KW, 0, CSS_W_BSTYLE, "none" },
    { "border-right-style", 0, G_KW, 0, CSS_W_BSTYLE, "none" },
    { "border-bottom-style", 0, G_KW, 0, CSS_W_BSTYLE, "none" },
    { "border-left-style", 0, G_KW, 0, CSS_W_BSTYLE, "none" },
    { "border-top-color", 0, G_COLOR, 0, 0, "currentcolor" },
    { "border-right-color", 0, G_COLOR, 0, 0, "currentcolor" },
    { "border-bottom-color", 0, G_COLOR, 0, 0, "currentcolor" },
    { "border-left-color", 0, G_COLOR, 0, 0, "currentcolor" },
    { "border-top-left-radius", 0, G_RADIUS, F_PCT, 0, "0px" },
    { "border-top-right-radius", 0, G_RADIUS, F_PCT, 0, "0px" },
    { "border-bottom-right-radius", 0, G_RADIUS, F_PCT, 0, "0px" },
    { "border-bottom-left-radius", 0, G_RADIUS, F_PCT, 0, "0px" },
    { "width", 0, G_LEN, F_PCT, CSS_W_SIZE, "auto" },
    { "height", 0, G_LEN, F_PCT, CSS_W_SIZE, "auto" },
    { "min-width", 0, G_LEN, F_PCT, CSS_W_SIZE, "auto" },
    { "min-height", 0, G_LEN, F_PCT, CSS_W_SIZE, "auto" },
    { "max-width", 0, G_LEN, F_PCT, "none min-content max-content fit-content", "none" },
    { "max-height", 0, G_LEN, F_PCT, "none min-content max-content fit-content", "none" },
    { "list-style-type", 1, G_KW, 0, "none disc circle square decimal decimal-leading-zero lower-roman upper-roman lower-alpha "
      "upper-alpha lower-latin upper-latin lower-greek disclosure-open disclosure-closed", "disc" },
    { "list-style-position", 1, G_KW, 0, "outside inside", "outside" },
    { "font-family", 1, G_FAMILY, 0, 0, "serif" },
    { "box-sizing", 0, G_KW, 0, "content-box border-box", "content-box" },
    { "position", 0, G_KW, 0, "static relative absolute fixed sticky", "static" },
    { "top", 0, G_LEN, F_NEG | F_PCT, "auto", "auto" },
    { "right", 0, G_LEN, F_NEG | F_PCT, "auto", "auto" },
    { "bottom", 0, G_LEN, F_NEG | F_PCT, "auto", "auto" },
    { "left", 0, G_LEN, F_NEG | F_PCT, "auto", "auto" },
    { "float", 0, G_KW, 0, "none left right inline-start inline-end", "none" },
    { "clear", 0, G_KW, 0, "none left right both inline-start inline-end", "none" },
    { "z-index", 0, G_NUM, F_INT | F_NEG, "auto", "auto" },
    { "overflow-x", 0, G_KW, 0, "visible hidden clip scroll auto", "visible" },
    { "overflow-y", 0, G_KW, 0, "visible hidden clip scroll auto", "visible" },
    { "vertical-align", 0, G_LEN, F_NEG | F_PCT, "baseline sub super text-top text-bottom middle top bottom", "baseline" },
    { "opacity", 0, G_NUM, F_PCT, 0, "1" },
    { "visibility", 1, G_KW, 0, "visible hidden collapse", "visible" },
    { "flex-direction", 0, G_KW, 0, "row row-reverse column column-reverse", "row" },
    { "flex-wrap", 0, G_KW, 0, "nowrap wrap wrap-reverse", "nowrap" },
    { "flex-grow", 0, G_NUM, 0, 0, "0" },
    { "flex-shrink", 0, G_NUM, 0, 0, "1" },
    { "flex-basis", 0, G_LEN, F_PCT, "auto content min-content max-content fit-content", "auto" },
    { "order", 0, G_NUM, F_INT | F_NEG, 0, "0" },
    { "justify-content", 0, G_KW, 0, "normal flex-start flex-end start end center left right space-between space-around "
      "space-evenly stretch", "normal" },
    { "justify-items", 0, G_KW, 0, "legacy normal stretch start end center flex-start flex-end self-start self-end left right "
      "baseline", "legacy" },
    { "justify-self", 0, G_KW, 0, CSS_W_SELF, "auto" },
    { "align-items", 0, G_KW, 0, "normal stretch start end center flex-start flex-end self-start self-end baseline", "normal" },
    { "align-self", 0, G_KW, 0, CSS_W_SELF, "auto" },
    { "align-content", 0, G_KW, 0, "normal stretch start end center flex-start flex-end space-between space-around space-evenly "
      "baseline", "normal" },
    { "row-gap", 0, G_LEN, F_PCT, "normal", "normal" },
    { "column-gap", 0, G_LEN, F_PCT, "normal", "normal" },
    { "grid-template-columns", 0, G_TEXT, 0, "none", "none" },
    { "grid-template-rows", 0, G_TEXT, 0, "none", "none" },
    { "grid-auto-columns", 0, G_TEXT, 0, 0, "auto" },
    { "grid-auto-rows", 0, G_TEXT, 0, 0, "auto" },
    { "grid-auto-flow", 0, G_TEXT, 0, 0, "row" },
    { "grid-column-start", 0, G_TEXT, 0, 0, "auto" },
    { "grid-column-end", 0, G_TEXT, 0, 0, "auto" },
    { "grid-row-start", 0, G_TEXT, 0, 0, "auto" },
    { "grid-row-end", 0, G_TEXT, 0, 0, "auto" },
    { "background-image", 0, G_TEXT, 0, "none", "none" },
    { "background-position", 0, G_TEXT, 0, 0, "0% 0%" },
    { "background-size", 0, G_TEXT, 0, 0, "auto" },
    { "background-repeat", 0, G_TEXT, 0, 0, "repeat" },
    { "box-shadow", 0, G_TEXT, 0, "none", "none" },
    { "text-shadow", 1, G_TEXT, 0, "none", "none" },
    { "transform", 0, G_TEXT, 0, "none", "none" },
    { "transform-origin", 0, G_TEXT, 0, 0, "50% 50%" },
    { "outline-width", 0, G_LEN, 0, "thin medium thick", "medium" },
    { "outline-style", 0, G_KW, 0, "none auto dotted dashed solid double groove ridge inset outset", "none" },
    { "outline-color", 0, G_COLOR, 0, 0, "currentcolor" },
    { "outline-offset", 0, G_LEN, F_NEG, 0, "0px" },
    { "letter-spacing", 1, G_LEN, F_NEG, "normal", "normal" },
    { "word-spacing", 1, G_LEN, F_NEG | F_PCT, "normal", "normal" },
    { "text-indent", 1, G_LEN, F_NEG | F_PCT, 0, "0px" },
    { "text-transform", 1, G_KW, 0, "none capitalize uppercase lowercase full-width", "none" },
    { "direction", 1, G_KW, 0, "ltr rtl", "ltr" },
    { "unicode-bidi", 0, G_KW, 0, "normal embed isolate bidi-override isolate-override plaintext", "normal" },
    { "writing-mode", 1, G_KW, 0, "horizontal-tb vertical-rl vertical-lr sideways-rl sideways-lr", "horizontal-tb" },
    { "hyphens", 1, G_KW, 0, "none manual auto", "manual" },
    { "content", 0, G_TEXT, 0, "normal none", "normal" },
    { "counter-reset", 0, G_TEXT, 0, "none", "none" },
    { "counter-increment", 0, G_TEXT, 0, "none", "none" },
    { "quotes", 1, G_TEXT, 0, "auto none", "auto" },
    { "color-scheme", 1, G_TEXT, 0, "normal", "normal" },
    { "background-clip", 0, G_TEXT, 0, 0, "border-box" },
    { "background-origin", 0, G_TEXT, 0, 0, "padding-box" },
    { "filter", 0, G_TEXT, 0, "none", "none" },
    { "backdrop-filter", 0, G_TEXT, 0, "none", "none" },
    { "perspective", 0, G_TEXT, 0, "none", "none" },
    { "perspective-origin", 0, G_TEXT, 0, 0, "50% 50%" },
    { "transform-style", 0, G_KW, 0, "flat preserve-3d", "flat" },
    { "backface-visibility", 0, G_KW, 0, "visible hidden", "visible" },
    { "translate", 0, G_TEXT, 0, "none", "none" },
    { "rotate", 0, G_TEXT, 0, "none", "none" },
    { "scale", 0, G_TEXT, 0, "none", "none" },
    { "animation-name", 0, G_TEXT, 0, "none", "none" },
    { "animation-duration", 0, G_TEXT, 0, 0, "0s" },
    { "animation-timing-function", 0, G_TEXT, 0, 0, "ease" },
    { "animation-delay", 0, G_TEXT, 0, 0, "0s" },
    { "animation-iteration-count", 0, G_TEXT, 0, 0, "1" },
    { "animation-direction", 0, G_TEXT, 0, 0, "normal" },
    { "animation-fill-mode", 0, G_TEXT, 0, 0, "none" },
    { "animation-play-state", 0, G_TEXT, 0, 0, "running" },
    { "transition-property", 0, G_TEXT, 0, 0, "all" },
    { "transition-duration", 0, G_TEXT, 0, 0, "0s" },
    { "transition-timing-function", 0, G_TEXT, 0, 0, "ease" },
    { "transition-delay", 0, G_TEXT, 0, 0, "0s" },
    { "transition-behavior", 0, G_TEXT, 0, 0, "normal" },
    { "grid-template-areas", 0, G_TEXT, 0, "none", "none" },
    { "-webkit-text-fill-color", 1, G_COLOR, 0, 0, "currentcolor" },
    { "text-overflow", 0, G_KW, 0, "clip ellipsis", "clip" },
    { "-webkit-line-clamp", 0, G_NUM, F_INT, "none", "none" },
    { "-webkit-box-orient", 0, G_KW, 0, "horizontal vertical inline-axis block-axis", "horizontal" },
};

static int css_inherited(css_prop p) { return p < CSS_NPROPS && css_pdefs[p].inh; }
/* a colour whose initial value is currentcolor: the element's colour */
static int css_init_cc(css_prop p) { return css_pdefs[p].gram == G_COLOR && strcmp(css_pdefs[p].init, "currentcolor") == 0; }

/* ---- the tables the module reads names through, made once a process ----
   The property names (longhands, shorthands, other names) by their hash, and each
   property's keywords as hashes: a declaration's name and a keyword are each found in a
   few steps, not by a walk of every name */
#define CSS_NAME_SLOTS 512
#define CSS_MAXKW 40
static int css_ready;
static uint32_t css_name_hash[CSS_NAME_SLOTS];
static int16_t css_name_code[CSS_NAME_SLOTS];        /* 0 empty; p + 1 the longhand p; -(i + 1) the shorthand i */
static uint32_t css_kwh[CSS_NPROPS][CSS_MAXKW];
static uint16_t css_kwo[CSS_NPROPS][CSS_MAXKW];     /* each keyword's place in the property's words, and its length */
static uint8_t css_kwl[CSS_NPROPS][CSS_MAXKW];
static uint8_t css_kwn[CSS_NPROPS];                 /* its keywords' count; CSS_MAXKW + 1: more, read as text */

static uint32_t css_hash_ci(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)css_lower(s[i]); h *= 16777619u; }
    return h;
}
static void css_name_put(const char *name, int code) {
    uint32_t h = css_hash_ci(name, strlen(name));
    for (uint32_t i = h & (CSS_NAME_SLOTS - 1);; i = (i + 1) & (CSS_NAME_SLOTS - 1))
        if (!css_name_code[i]) { css_name_hash[i] = h; css_name_code[i] = (int16_t)code; return; }
}
static const char *css_short_name(int i);
/* the properties whose -webkit- prefixed names Chromium keeps as aliases of theirs (of
   those this module has) */
static const char css_webkit_aliases[] =
    "align-content align-items align-self animation animation-delay animation-direction animation-duration animation-fill-mode "
    "animation-iteration-count animation-name animation-play-state animation-timing-function backface-visibility background-clip "
    "background-origin background-size border-bottom-left-radius border-bottom-right-radius border-radius border-top-left-radius "
    "border-top-right-radius box-shadow box-sizing filter flex flex-basis flex-direction flex-flow flex-grow flex-shrink flex-wrap "
    "justify-content order perspective perspective-origin transform transform-origin transform-style transition transition-delay "
    "transition-duration transition-property transition-timing-function";

/* the code of a name (as css_name_code holds it), or 0; a -webkit- alias, its property's */
static int css_name_find(const char *name, size_t nn) {
    if (nn == 0 || nn > 40) return 0;
    uint32_t h = css_hash_ci(name, nn);
    for (uint32_t i = h & (CSS_NAME_SLOTS - 1); css_name_code[i]; i = (i + 1) & (CSS_NAME_SLOTS - 1)) {
        if (css_name_hash[i] != h) continue;
        int c = css_name_code[i];
        if (css_eq_ci(name, nn, c > 0 ? css_pdefs[c - 1].name : css_short_name(-c - 1))) return c;
    }
    if (nn > 8 && css_eq_ci(name, 8, "-webkit-")) {
        char un[48];
        size_t k = 0;
        for (; k < nn - 8 && k + 1 < sizeof un; k++) un[k] = css_lower(name[8 + k]);
        un[k] = 0;
        if (k == nn - 8 && css_word_in(un, css_webkit_aliases)) return css_name_find(name + 8, nn - 8);
    }
    return 0;
}
static void css_order_make(void);

/* the property a longhand's name names, or -1 */
static int css_prop_of(const char *name, size_t nn) {
    if (!css_ready) css_order_make();
    int c = css_name_find(name, nn);
    return c > 0 ? c - 1 : -1;
}

/* the keyword w is one of the property p's */
static int css_kw_of(css_prop p, const char *w) {
    if (!css_ready) css_order_make();
    if (css_kwn[p] > CSS_MAXKW) return css_pdefs[p].words && css_word_in(w, css_pdefs[p].words);
    size_t wn = strlen(w);
    uint32_t h = css_hash_ci(w, wn);
    for (int i = 0; i < css_kwn[p]; i++)
        if (css_kwh[p][i] == h && css_kwl[p][i] == wn && memcmp(css_pdefs[p].words + css_kwo[p][i], w, wn) == 0) return 1;
    return 0;
}

/* the values a property takes, read as a single value (every property takes the global
   keywords); auto, for a property whose keywords hold it, is kept as that keyword */
static int css_valid(css_prop p, css_val *v) {
    if (css_is_global(v)) return 1;
    const css_pdef *d = &css_pdefs[p];
    css_unit u = v->unit;
    int g = d->gram, f = d->flags;
    if (u == CSS_UNIT_AUTO && g != G_LEN && d->words && css_word_in("auto", d->words)) { css_word_val(v, "auto"); return 1; }
    if (u == CSS_UNIT_KEYWORD) {
        if (g == G_COLOR) { uint32_t c; return strcmp(v->word, "currentcolor") == 0 || css_system_color(v->word, strlen(v->word), 0, &c); }
        return d->words && css_kw_of(p, v->word);
    }
    if (g == G_COLOR) return u == CSS_UNIT_COLOR;
    int neg = v->num < 0;
    if (g == G_LEN) {
        if (u == CSS_UNIT_AUTO) return d->words && css_word_in("auto", d->words);
        if (u == CSS_UNIT_PX || u == CSS_UNIT_EM || u == CSS_UNIT_REM) return !neg || (f & F_NEG);
        if (u == CSS_UNIT_PERCENT) return (f & F_PCT) && (!neg || (f & F_NEG));
        if (u == CSS_UNIT_NUMBER) return v->num == 0 || ((f & F_NUM) && !neg);
        if (u == CSS_UNIT_CALC) return (v->ck & (CSS_CK_LEN | CSS_CK_PCT)) ? (!(v->ck & CSS_CK_PCT) || (f & F_PCT)) : (f & F_NUM) != 0;
        return 0;
    }
    if (g == G_NUM) {
        if (u == CSS_UNIT_NUMBER) {
            if ((f & F_INT) && v->num != (double)(long)v->num) return 0;
            if (p == CSS_PROP_FONT_WEIGHT) return v->num >= 1 && v->num <= 1000;
            if (p == CSS_PROP_WEBKIT_LINE_CLAMP) return v->num >= 1;
            return !neg || (f & F_NEG);
        }
        if (u == CSS_UNIT_PERCENT) return (f & F_PCT) && !neg;
        if (u == CSS_UNIT_CALC) return v->ck == CSS_CK_NUM;
        if (u == CSS_UNIT_AUTO) return d->words && css_word_in("auto", d->words);
        return 0;
    }
    return 0;
}

/* ---- the values kept as text: each property's reader ---- */

static int css_is_length(const char *s, size_t n, int pct, int neg) {
    css_val v;
    if (!css_parse_val(s, n, &v)) return 0;
    if (v.unit == CSS_UNIT_CALC) return (v.ck & CSS_CK_LEN) || (pct && (v.ck & CSS_CK_PCT));
    if (v.unit == CSS_UNIT_NUMBER) return v.num == 0;
    if (v.unit == CSS_UNIT_PX || v.unit == CSS_UNIT_EM || v.unit == CSS_UNIT_REM) return neg || v.num >= 0;
    if (v.unit == CSS_UNIT_PERCENT) return pct && (neg || v.num >= 0);
    return 0;
}
static int css_is_fn(const char *s, size_t n, const char *name) {
    size_t k = strlen(name);
    return n > k + 1 && css_bytes_eq(s, name, k, 1) && s[k] == '(' && s[n - 1] == ')';
}
static int css_is_string(const char *s, size_t n) { return n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]; }
static int css_is_ident(const char *s, size_t n) {
    if (n == 0 || css_is_digit(s[0]) || (s[0] == '-' && n > 1 && css_is_digit(s[1]))) return 0;
    for (size_t i = 0; i < n; i++) if (!css_namechar(s[i])) return 0;
    return 1;
}
static int css_is_int(const char *s, size_t n, int *out) {
    double x;
    size_t used = css_number(s, n, &x);
    if (!used || used != n || x != (double)(long)x) return 0;
    *out = (int)x;
    return 1;
}
/* ---- the colours that hang on the color scheme (CSS Color 4, 6.2; Color 5, 4) ---- */

/* the Canvas of each scheme: what a color-scheme value's colour holds (contracts/css.pl) */
#define CSS_CANVAS_LIGHT 0xffffffffu
#define CSS_CANVAS_DARK 0x121212ffu

/* the system colours in the light scheme and the dark, as Chromium has them (its
   deprecated names mapped as CSS Color 4, 6.3 maps them) */
typedef struct css_syscolor { const char *name; uint32_t light, dark; } css_syscolor;
static const css_syscolor css_syscolors[] = {
    { "accentcolor", 0x0075ffff, 0x0075ffff }, { "accentcolortext", 0xffffffff, 0xffffffff },
    { "activeborder", 0x000000ff, 0xffffffff }, { "activecaption", 0xffffffff, 0x121212ff },
    { "activetext", 0xff0000ff, 0xff0000ff }, { "appworkspace", 0xffffffff, 0x121212ff },
    { "background", 0xffffffff, 0x121212ff }, { "buttonborder", 0x000000ff, 0xffffffff },
    { "buttonface", 0xefefefff, 0x6b6b6bff }, { "buttonhighlight", 0xefefefff, 0x6b6b6bff },
    { "buttonshadow", 0xefefefff, 0x6b6b6bff }, { "buttontext", 0x000000ff, 0xffffffff },
    { "canvas", 0xffffffff, 0x121212ff }, { "canvastext", 0x000000ff, 0xffffffff },
    { "captiontext", 0x000000ff, 0xffffffff }, { "field", 0xffffffff, 0x3b3b3bff },
    { "fieldtext", 0x000000ff, 0xffffffff }, { "graytext", 0x808080ff, 0x808080ff },
    { "highlight", 0x0041c6cc, 0x0041c6cc }, { "highlighttext", 0xffffffff, 0xffffffff },
    { "inactiveborder", 0x000000ff, 0xffffffff }, { "inactivecaption", 0xffffffff, 0x121212ff },
    { "inactivecaptiontext", 0x808080ff, 0x808080ff }, { "infobackground", 0xffffffff, 0x121212ff },
    { "infotext", 0x000000ff, 0xffffffff }, { "linktext", 0x0000eeff, 0x9e9effff },
    { "mark", 0xffff00ff, 0xffff00ff }, { "marktext", 0x000000ff, 0x000000ff },
    { "menu", 0xffffffff, 0x121212ff }, { "menutext", 0x000000ff, 0xffffffff },
    { "scrollbar", 0xffffffff, 0x121212ff }, { "selecteditem", 0x1967d2ff, 0x99c8ffff },
    { "selecteditemtext", 0xffffffff, 0x3b3b3bff }, { "threeddarkshadow", 0x000000ff, 0xffffffff },
    { "threedface", 0xefefefff, 0x6b6b6bff }, { "threedhighlight", 0x000000ff, 0xffffffff },
    { "threedlightshadow", 0x000000ff, 0xffffffff }, { "threedshadow", 0x000000ff, 0xffffffff },
    { "visitedtext", 0x551a8bff, 0xd0adf0ff }, { "window", 0xffffffff, 0x121212ff },
    { "windowframe", 0x000000ff, 0xffffffff }, { "windowtext", 0x000000ff, 0xffffffff } };

/* the system colour w names (any case) in the dark scheme or the light: 1, or 0 for none */
static int css_system_color(const char *w, size_t n, int dark, uint32_t *rgba) {
    for (size_t k = 0; k < sizeof css_syscolors / sizeof css_syscolors[0]; k++)
        if (css_eq_ci(w, n, css_syscolors[k].name)) { *rgba = dark ? css_syscolors[k].dark : css_syscolors[k].light; return 1; }
    return 0;
}

/* a colour that hangs on the scheme -- a system colour, or light-dark(a, b): a in the
   light scheme, b in the dark, each a colour of any kind -- in the scheme dark says: 1,
   or 0 when s is none (currentcolor in light-dark() is not read) */
static int css_scheme_color(const char *s, size_t n, int dark, uint32_t *rgba) {
    while (n && css_ws(*s)) { s++; n--; }
    while (n && css_ws(s[n - 1])) n--;
    if (css_system_color(s, n, dark, rgba)) return 1;
    if (n < 13 || !css_bytes_eq(s, "light-dark(", 11, 1) || s[n - 1] != ')') return 0;
    size_t comma = css_comma(s, n - 1, 11);
    if (comma >= n - 1 || css_comma(s, n - 1, comma + 1) < n - 1) return 0;
    uint32_t c[2];
    size_t a[2] = { 11, comma + 1 }, e[2] = { comma, n - 1 };
    for (int k = 0; k < 2; k++) {
        const char *q = s + a[k];
        size_t qn = e[k] - a[k];
        while (qn && css_ws(*q)) { q++; qn--; }
        while (qn && css_ws(q[qn - 1])) qn--;
        if (!css_parse_color(q, qn, &c[k]) && !css_scheme_color(q, qn, dark, &c[k])) return 0;
    }
    *rgba = c[dark ? 1 : 0];
    return 1;
}

static int css_is_color(const char *s, size_t n) {
    uint32_t c;
    return css_eq_ci(s, n, "currentcolor") || css_parse_color(s, n, &c) || css_scheme_color(s, n, 0, &c);
}

/* the scheme a color-scheme value is used in (CSS Color Adjust 1, 2.1): of the schemes it
   lists, the one the reader prefers, else the first of light and dark; normal, the page's
   (page_dark: its meta name=color-scheme's), else light. 1 for dark */
static int css_scheme_of(const char *t, size_t n, int page_dark) {
    size_t i = 0, st, ln;
    int light = 0, dark = 0, first = -1;
    if (css_eq_ci(t, n, "normal")) return page_dark;
    while (css_next_part(t, n, &i, &st, &ln)) {
        if (css_eq_ci(t + st, ln, "light")) { light = 1; if (first < 0) first = 0; }
        else if (css_eq_ci(t + st, ln, "dark")) { dark = 1; if (first < 0) first = 1; }
    }
    if (css_pref_dark ? dark : light) return css_pref_dark;
    return first > 0;
}

/* an image: url(), or a gradient (its arguments read by paint) */
static int css_is_image(const char *s, size_t n) {
    return css_is_fn(s, n, "url") || css_is_fn(s, n, "linear-gradient") || css_is_fn(s, n, "radial-gradient") ||
           css_is_fn(s, n, "repeating-linear-gradient") || css_is_fn(s, n, "repeating-radial-gradient");
}

/* a track size of a grid template: a length or percentage, fr, a sizing keyword,
   minmax(), fit-content() */
static int css_is_track(const char *s, size_t n) {
    double x;
    size_t used = css_number(s, n, &x);
    if (used && used + 2 == n && css_eq_ci(s + used, 2, "fr")) return x >= 0;
    if (css_eq_ci(s, n, "auto") || css_eq_ci(s, n, "min-content") || css_eq_ci(s, n, "max-content")) return 1;
    if (css_is_fn(s, n, "minmax")) {
        size_t a = 7, c = css_comma(s, n - 1, a);
        if (c >= n - 1) return 0;
        size_t a0 = css_skip_ws(s, c, a), a1 = c, b0 = css_skip_ws(s, n - 1, c + 1), b1 = n - 1;
        while (a1 > a0 && css_ws(s[a1 - 1])) a1--;
        while (b1 > b0 && css_ws(s[b1 - 1])) b1--;
        return css_is_track(s + a0, a1 - a0) && css_is_track(s + b0, b1 - b0);
    }
    if (css_is_fn(s, n, "fit-content")) return css_is_length(s + 12, n - 13, 1, 0);
    return css_is_length(s, n, 1, 0);
}
/* a track list: track sizes, repeat(), [line names] */
static int css_is_tracks(const char *s, size_t n, int depth) {
    size_t i = 0, st, ln;
    int any = 0;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        if (p[0] == '[') {
            size_t e = css_block_end(s, n, st, '[', ']');
            if (e >= n) return 0;
            i = e + 1;
            continue;
        }
        if (css_is_fn(p, ln, "repeat")) {
            if (depth > 0) return 0;
            size_t c = css_comma(p, ln - 1, 7);
            if (c >= ln - 1) return 0;
            size_t a0 = css_skip_ws(p, c, 7), a1 = c;
            while (a1 > a0 && css_ws(p[a1 - 1])) a1--;
            int k;
            if (!css_is_int(p + a0, a1 - a0, &k) && !css_eq_ci(p + a0, a1 - a0, "auto-fill") && !css_eq_ci(p + a0, a1 - a0, "auto-fit")) return 0;
            if (!css_is_tracks(p + c + 1, ln - 2 - c, depth + 1)) return 0;
            any = 1;
            continue;
        }
        if (!css_is_track(p, ln)) return 0;
        any = 1;
    }
    return any;
}

/* each item of a comma list holds, by kind: 1 shadow (box), 2 shadow (text), 3 background
   position, 4 background size, 5 background repeat, 6 image */
static int css_is_list_item(int kind, const char *s, size_t n) {
    size_t i = 0, st, ln;
    int lengths = 0, colors = 0, insets = 0, parts = 0;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        parts++;
        if (kind == 6) { if (parts > 1 || !css_is_image(p, ln)) return 0; continue; }
        if (kind == 1 || kind == 2) {
            if (kind == 1 && css_eq_ci(p, ln, "inset")) { if (insets++) return 0; continue; }
            if (css_is_color(p, ln)) { if (colors++) return 0; continue; }
            if (!css_is_length(p, ln, 0, 1)) return 0;
            lengths++;
            continue;
        }
        if (kind == 3) {
            if (!(css_eq_ci(p, ln, "left") || css_eq_ci(p, ln, "right") || css_eq_ci(p, ln, "top") ||
                  css_eq_ci(p, ln, "bottom") || css_eq_ci(p, ln, "center") || css_is_length(p, ln, 1, 1))) return 0;
            continue;
        }
        if (kind == 4) {
            if ((css_eq_ci(p, ln, "cover") || css_eq_ci(p, ln, "contain")) && parts == 1) continue;
            if (!(css_eq_ci(p, ln, "auto") || css_is_length(p, ln, 1, 0))) return 0;
            continue;
        }
        if (kind == 7 || kind == 8) {
            /* a box: background-origin's (7), background-clip's (8, text besides) */
            if (!(css_eq_ci(p, ln, "border-box") || css_eq_ci(p, ln, "padding-box") || css_eq_ci(p, ln, "content-box") ||
                  (kind == 8 && css_eq_ci(p, ln, "text")))) return 0;
            continue;
        }
        if (kind == 5) {
            if ((css_eq_ci(p, ln, "repeat-x") || css_eq_ci(p, ln, "repeat-y")) && parts == 1) continue;
            if (!(css_eq_ci(p, ln, "repeat") || css_eq_ci(p, ln, "no-repeat") || css_eq_ci(p, ln, "space") || css_eq_ci(p, ln, "round"))) return 0;
            continue;
        }
    }
    if (kind == 1) return lengths >= 2 && lengths <= 4;
    if (kind == 2) return lengths >= 2 && lengths <= 3;
    if (kind == 3) return parts >= 1 && parts <= 4;
    if (kind == 4 || kind == 5) return parts >= 1 && parts <= 2;
    return parts == 1;
}
static int css_is_list(int kind, const char *s, size_t n) {
    size_t a = 0;
    while (a <= n) {
        size_t b = css_comma(s, n, a);
        if (!css_is_list_item(kind, s + a, b - a)) return 0;
        a = b + 1;
    }
    return 1;
}

/* transform's functions, 2D and 3D */
static int css_is_transform(const char *s, size_t n) {
    static const char *const fns = "translate translatex translatey translatez translate3d scale scalex scaley scalez scale3d rotate rotatex "
                                   "rotatey rotatez rotate3d skew skewx skewy matrix matrix3d perspective";
    size_t i = 0, st, ln;
    int any = 0;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        size_t f = 0;
        while (f < ln && css_namechar(p[f])) f++;
        if (f == 0 || f >= ln || p[f] != '(' || p[ln - 1] != ')' || f >= 16) return 0;
        char w[16];
        for (size_t k = 0; k < f; k++) w[k] = css_lower(p[k]);
        w[f] = 0;
        if (!css_word_in(w, fns)) return 0;
        any = 1;
    }
    return any;
}

/* an angle: a number with deg, grad, rad or turn, or zero */
static int css_is_angle(const char *s, size_t n) {
    double x;
    size_t used = css_number(s, n, &x);
    if (!used) return 0;
    if (used == n) return x == 0;
    const char *u = s + used;
    size_t un = n - used;
    return css_eq_ci(u, un, "deg") || css_eq_ci(u, un, "grad") || css_eq_ci(u, un, "rad") || css_eq_ci(u, un, "turn");
}

/* filter's and backdrop-filter's functions (Filter Effects 1, 5.1), one or more in turn:
   blur() a length not negative; brightness(), contrast(), grayscale(), invert(),
   opacity(), saturate() and sepia() a number or a percentage not negative; hue-rotate()
   an angle; each of them empty for its default; drop-shadow() two or three lengths (its
   blur, the third, not negative) and a colour before or after them; url() an SVG
   filter's. Their arguments are not separated by commas */
static int css_is_filter(const char *s, size_t n) {
    static const char *const amounts = "brightness contrast grayscale invert opacity saturate sepia";
    size_t i = 0, st, ln;
    int any = 0;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        size_t f = 0;
        while (f < ln && css_namechar(p[f])) f++;
        if (f == 0 || f >= ln || p[f] != '(' || p[ln - 1] != ')' || f >= 16) return 0;
        char w[16];
        for (size_t k = 0; k < f; k++) w[k] = css_lower(p[k]);
        w[f] = 0;
        any = 1;
        if (strcmp(w, "url") == 0) continue;
        const char *a = p + f + 1;
        size_t an = ln - f - 2;
        if (css_comma(a, an, 0) < an) return 0;
        if (strcmp(w, "drop-shadow") == 0) {
            if (!css_is_list_item(2, a, an)) return 0;
            /* its blur: the third length */
            size_t j = 0, xs, xl;
            int lengths = 0;
            while (css_next_part(a, an, &j, &xs, &xl)) {
                if (css_is_color(a + xs, xl)) continue;
                if (++lengths == 3 && !css_is_length(a + xs, xl, 0, 0)) return 0;
            }
            continue;
        }
        size_t j = 0, xs, xl;
        if (!css_next_part(a, an, &j, &xs, &xl)) {
            if (css_word_in(w, amounts) || strcmp(w, "blur") == 0 || strcmp(w, "hue-rotate") == 0) continue;
            return 0;
        }
        const char *x = a + xs;
        size_t j2 = j, ys, yl;
        if (css_next_part(a, an, &j2, &ys, &yl)) return 0;
        if (strcmp(w, "blur") == 0) { if (!css_is_length(x, xl, 0, 0)) return 0; continue; }
        if (strcmp(w, "hue-rotate") == 0) { if (!css_is_angle(x, xl)) return 0; continue; }
        if (!css_word_in(w, amounts)) return 0;
        double v;
        size_t used = css_number(x, xl, &v);
        if (!used || v < 0 || !(used == xl || (used + 1 == xl && x[used] == '%'))) return 0;
    }
    return any;
}

/* the individual transforms (CSS Transforms 2, 5): translate's one to three lengths (the
   third not a percentage); rotate's angle, about an axis named (x, y or z) or given by
   three numbers; scale's one to three numbers or percentages */
static int css_is_indiv(css_prop p, const char *s, size_t n) {
    size_t i = 0, st, ln;
    int parts = 0, nums = 0, angles = 0, axis = 0;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *q = s + st;
        parts++;
        if (p == CSS_PROP_TRANSLATE) {
            if (parts > 3 || !css_is_length(q, ln, parts < 3, 1)) return 0;
            continue;
        }
        double x;
        size_t used = css_number(q, ln, &x);
        if (p == CSS_PROP_SCALE) {
            if (parts > 3 || !used || !(used == ln || (used + 1 == ln && q[used] == '%'))) return 0;
            continue;
        }
        if (css_eq_ci(q, ln, "x") || css_eq_ci(q, ln, "y") || css_eq_ci(q, ln, "z")) { if (axis++ || nums) return 0; continue; }
        if (used == ln) { if (axis || ++nums > 3) return 0; continue; }
        if (!css_is_angle(q, ln) || angles++) return 0;
    }
    if (p == CSS_PROP_ROTATE) return angles == 1 && (nums == 0 || nums == 3);
    return parts >= 1;
}

/* a time (CSS Values 4, 7.3): a number with s or ms, into *ms */
static int css_time_ms(const char *s, size_t n, double *ms) {
    double x;
    size_t used = css_number(s, n, &x);
    if (!used || used == n) return 0;
    if (css_eq_ci(s + used, n - used, "s")) { *ms = x * 1000; return 1; }
    if (css_eq_ci(s + used, n - used, "ms")) { *ms = x; return 1; }
    return 0;
}

/* an easing function (CSS Easing 2): linear, ease and its kinds, step-start, step-end,
   cubic-bezier() with its x values from 0 to 1, steps() with a count (two or more for
   jump-none) and its position, linear() with its stops */
static int css_is_easing(const char *s, size_t n) {
    static const char *const words = "linear ease ease-in ease-out ease-in-out step-start step-end";
    char w[16];
    if (n < sizeof w && css_is_ident(s, n)) {
        for (size_t k = 0; k < n; k++) w[k] = css_lower(s[k]);
        w[n] = 0;
        return css_word_in(w, words);
    }
    size_t open = 0;
    while (open < n && s[open] != '(') open++;
    if (open >= n || s[n - 1] != ')') return 0;
    const char *a = s + open + 1;
    size_t an = n - open - 2, j = 0, xs, xl;
    double v[4];
    int nv = 0;
    if (css_eq_ci(s, open, "cubic-bezier")) {
        while (css_next_part(a, an, &j, &xs, &xl)) {
            if (nv == 4 || css_number(a + xs, xl, &v[nv]) != xl) return 0;
            nv++;
        }
        return nv == 4 && v[0] >= 0 && v[0] <= 1 && v[2] >= 0 && v[2] <= 1;
    }
    if (css_eq_ci(s, open, "steps")) {
        int count = 0, pos = 0, jn = 0;
        while (css_next_part(a, an, &j, &xs, &xl)) {
            const char *q = a + xs;
            if (!count) {
                double c;
                if (css_number(q, xl, &c) != xl || c != (double)(long)c || c < 1) return 0;
                count = (int)c;
                continue;
            }
            if (pos++) return 0;
            if (css_eq_ci(q, xl, "jump-none")) jn = 1;
            else if (!(css_eq_ci(q, xl, "jump-start") || css_eq_ci(q, xl, "jump-end") || css_eq_ci(q, xl, "jump-both") ||
                       css_eq_ci(q, xl, "start") || css_eq_ci(q, xl, "end"))) return 0;
        }
        return count >= 1 + jn;
    }
    if (css_eq_ci(s, open, "linear")) {
        /* its stops: a number, with up to two percentages each */
        size_t b = 0;
        int stops = 0;
        while (b <= an) {
            size_t e = css_comma(a, an, b);
            size_t k = 0, ps, pl;
            int nums = 0, pcts = 0;
            while (css_next_part(a + b, e - b, &k, &ps, &pl)) {
                double x;
                size_t used = css_number(a + b + ps, pl, &x);
                if (used == pl && !nums && !pcts) nums++;
                else if (used && used + 1 == pl && a[b + ps + used] == '%' && nums && pcts < 2) pcts++;
                else return 0;
            }
            if (!nums) return 0;
            stops++;
            if (e >= an) break;
            b = e + 1;
        }
        return stops >= 2;
    }
    return 0;
}

/* an item of an animation's or a transition's list (kind: the property's), valid */
static int css_anim_item(css_prop p, const char *s, size_t n) {
    double x;
    switch (p) {
    case CSS_PROP_ANIMATION_NAME: return css_is_ident(s, n) || css_is_string(s, n);
    case CSS_PROP_ANIMATION_DURATION:
    case CSS_PROP_TRANSITION_DURATION: return (css_time_ms(s, n, &x) && x >= 0) || (p == CSS_PROP_ANIMATION_DURATION && css_eq_ci(s, n, "auto"));
    case CSS_PROP_ANIMATION_DELAY:
    case CSS_PROP_TRANSITION_DELAY: return css_time_ms(s, n, &x);
    case CSS_PROP_ANIMATION_TIMING_FUNCTION:
    case CSS_PROP_TRANSITION_TIMING_FUNCTION: return css_is_easing(s, n);
    case CSS_PROP_ANIMATION_ITERATION_COUNT: return css_eq_ci(s, n, "infinite") || (css_number(s, n, &x) == n && x >= 0);
    case CSS_PROP_ANIMATION_DIRECTION:
        return css_eq_ci(s, n, "normal") || css_eq_ci(s, n, "reverse") || css_eq_ci(s, n, "alternate") || css_eq_ci(s, n, "alternate-reverse");
    case CSS_PROP_ANIMATION_FILL_MODE:
        return css_eq_ci(s, n, "none") || css_eq_ci(s, n, "forwards") || css_eq_ci(s, n, "backwards") || css_eq_ci(s, n, "both");
    case CSS_PROP_ANIMATION_PLAY_STATE: return css_eq_ci(s, n, "running") || css_eq_ci(s, n, "paused");
    case CSS_PROP_TRANSITION_PROPERTY: return css_is_ident(s, n);
    case CSS_PROP_TRANSITION_BEHAVIOR: return css_eq_ci(s, n, "normal") || css_eq_ci(s, n, "allow-discrete");
    default: return 0;
    }
}

/* an animation's or a transition's longhand: its items, a comma between each; none and
   all alone (transition-property's none in a list of one) */
static int css_is_anim_list(css_prop p, const char *s, size_t n) {
    size_t a = 0;
    int items = 0;
    while (a <= n) {
        size_t b = css_comma(s, n, a);
        size_t sa = css_skip_ws(s, b, a), sb = b;
        while (sb > sa && css_ws(s[sb - 1])) sb--;
        if (sb == sa) return 0;
        size_t k = 0, ps, pl;
        css_next_part(s + sa, sb - sa, &k, &ps, &pl);
        if (ps + pl != sb - sa && !(p == CSS_PROP_ANIMATION_TIMING_FUNCTION || p == CSS_PROP_TRANSITION_TIMING_FUNCTION)) return 0;
        if (!css_anim_item(p, s + sa, sb - sa)) return 0;
        if (p == CSS_PROP_TRANSITION_PROPERTY && css_eq_ci(s + sa, sb - sa, "none") && (items || b < n)) return 0;
        items++;
        if (b >= n) break;
        a = b + 1;
    }
    return items > 0;
}

/* content: strings, attr(), counter(), counters(), the quotes' keywords, url() */
/* content's list (Content 3, 2): strings, attr(), counters, images, quotes; then, after a
   solidus, its alternative text for a reader: strings, attr(), counters */
static int css_is_content(const char *s, size_t n) {
    size_t i = 0, st, ln;
    int any = 0, alt = -1;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        if (ln == 1 && p[0] == '/') { if (alt >= 0 || !any) return 0; alt = 0; continue; }
        if (alt >= 0) {
            if (css_is_string(p, ln) || css_is_fn(p, ln, "attr") || css_is_fn(p, ln, "counter") || css_is_fn(p, ln, "counters")) { alt = 1; continue; }
            return 0;
        }
        if (css_is_string(p, ln) || css_is_fn(p, ln, "attr") || css_is_fn(p, ln, "counter") || css_is_fn(p, ln, "counters") ||
            css_is_fn(p, ln, "url") || css_eq_ci(p, ln, "open-quote") || css_eq_ci(p, ln, "close-quote") ||
            css_eq_ci(p, ln, "no-open-quote") || css_eq_ci(p, ln, "no-close-quote")) { any = 1; continue; }
        return 0;
    }
    return any && alt != 0;
}

/* a grid line: auto, an integer, span and an integer or a name, a name */
static int css_is_grid_line(const char *s, size_t n) {
    size_t i = 0, st, ln;
    int parts = 0, span = 0, num = 0, name = 0, k;
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *p = s + st;
        parts++;
        if (css_eq_ci(p, ln, "auto")) { if (n != ln + st) return 0; return parts == 1; }
        if (css_eq_ci(p, ln, "span")) { if (span++) return 0; continue; }
        if (css_is_int(p, ln, &k)) { if (num++ || k == 0 || (span && k < 0)) return 0; continue; }
        if (css_is_ident(p, ln)) { if (name++) return 0; continue; }
        return 0;
    }
    return parts > 0 && (num || name);
}

/* grid-template-areas (Grid 1, 7.3): strings, a row each, of as many cells each -- a cell
   a name, or a run of full stops (a null cell) -- every name's cells a rectangle: as many
   as the box its first and last rows and columns make. At most 128 names */
static int css_is_areas(const char *s, size_t n) {
    size_t i = 0, st, ln;
    int rows = 0, cols = -1, nn = 0;
    size_t nmo[128], nml[128];
    int r0[128], r1[128], c0[128], c1[128], cnt[128];
    while (css_next_part(s, n, &i, &st, &ln)) {
        const char *q = s + st;
        if (!css_is_string(q, ln)) return 0;
        int c = 0;
        size_t k = 1;
        while (k + 1 < ln) {
            if (css_ws(q[k])) { k++; continue; }
            size_t b = k;
            if (q[k] == '.') { while (k + 1 < ln && q[k] == '.') k++; c++; continue; }
            while (k + 1 < ln && css_namechar(q[k])) k++;
            if (k == b) return 0;            /* a trash token */
            int a = 0;
            while (a < nn && !(nml[a] == k - b && memcmp(s + nmo[a], q + b, k - b) == 0)) a++;
            if (a == nn) {
                if (nn == 128) return 0;
                nmo[a] = st + b; nml[a] = k - b; r0[a] = r1[a] = rows; c0[a] = c1[a] = c; cnt[a] = 0; nn++;
            }
            if (rows < r0[a]) r0[a] = rows;
            if (rows > r1[a]) r1[a] = rows;
            if (c < c0[a]) c0[a] = c;
            if (c > c1[a]) c1[a] = c;
            cnt[a]++;
            c++;
        }
        if (c == 0 || (cols >= 0 && c != cols)) return 0;
        cols = c;
        rows++;
    }
    for (int a = 0; a < nn; a++) if (cnt[a] != (r1[a] - r0[a] + 1) * (c1[a] - c0[a] + 1)) return 0;
    return rows > 0;
}

static int css_text_valid(css_prop p, const char *s, size_t n) {
    size_t i = 0, st, ln;
    int parts = 0, k;
    switch (p) {
    case CSS_PROP_COLOR_SCHEME: {
        /* [light | dark | <custom-ident>]+ && only? (CSS Color Adjust 1, 2.1): names a space
           apart, no comma among them */
        int only = 0;
        if (memchr(s, ',', n)) return 0;
        while (css_next_part(s, n, &i, &st, &ln)) {
            const char *q = s + st;
            if (!css_is_ident(q, ln) || css_eq_ci(q, ln, "normal") || css_eq_ci(q, ln, "default") || css_eq_ci(q, ln, "none")) return 0;
            if (css_eq_ci(q, ln, "only")) { if (only++) return 0; continue; }
            css_val g;
            if (css_parse_val(q, ln, &g) && css_is_global(&g)) return 0;
            parts++;
        }
        return parts > 0;
    }
    case CSS_PROP_GRID_TEMPLATE_COLUMNS: case CSS_PROP_GRID_TEMPLATE_ROWS: return css_is_tracks(s, n, 0);
    case CSS_PROP_GRID_AUTO_COLUMNS: case CSS_PROP_GRID_AUTO_ROWS:
        while (css_next_part(s, n, &i, &st, &ln)) { parts++; if (!css_is_track(s + st, ln)) return 0; }
        return parts > 0;
    case CSS_PROP_GRID_AUTO_FLOW: {
        int dir = 0, dense = 0;
        while (css_next_part(s, n, &i, &st, &ln)) {
            if (css_eq_ci(s + st, ln, "row") || css_eq_ci(s + st, ln, "column")) { if (dir++) return 0; }
            else if (css_eq_ci(s + st, ln, "dense")) { if (dense++) return 0; }
            else return 0;
        }
        return dir || dense;
    }
    case CSS_PROP_GRID_COLUMN_START: case CSS_PROP_GRID_COLUMN_END: case CSS_PROP_GRID_ROW_START: case CSS_PROP_GRID_ROW_END:
        return css_is_grid_line(s, n);
    case CSS_PROP_GRID_TEMPLATE_AREAS: return css_is_areas(s, n);
    case CSS_PROP_BACKGROUND_IMAGE: return css_is_list(6, s, n);
    case CSS_PROP_BACKGROUND_POSITION: return css_is_list(3, s, n);
    case CSS_PROP_BACKGROUND_SIZE: return css_is_list(4, s, n);
    case CSS_PROP_BACKGROUND_REPEAT: return css_is_list(5, s, n);
    case CSS_PROP_BACKGROUND_ORIGIN: return css_is_list(7, s, n);
    case CSS_PROP_BACKGROUND_CLIP: return css_is_list(8, s, n);
    case CSS_PROP_FILTER:
    case CSS_PROP_BACKDROP_FILTER: return css_is_filter(s, n);
    case CSS_PROP_PERSPECTIVE: return css_is_length(s, n, 0, 0);
    case CSS_PROP_TRANSLATE:
    case CSS_PROP_ROTATE:
    case CSS_PROP_SCALE: return css_is_indiv(p, s, n);
    case CSS_PROP_ANIMATION_NAME: case CSS_PROP_ANIMATION_DURATION: case CSS_PROP_ANIMATION_TIMING_FUNCTION:
    case CSS_PROP_ANIMATION_DELAY: case CSS_PROP_ANIMATION_ITERATION_COUNT: case CSS_PROP_ANIMATION_DIRECTION:
    case CSS_PROP_ANIMATION_FILL_MODE: case CSS_PROP_ANIMATION_PLAY_STATE: case CSS_PROP_TRANSITION_PROPERTY:
    case CSS_PROP_TRANSITION_DURATION: case CSS_PROP_TRANSITION_TIMING_FUNCTION: case CSS_PROP_TRANSITION_DELAY:
    case CSS_PROP_TRANSITION_BEHAVIOR: return css_is_anim_list(p, s, n);
    case CSS_PROP_BOX_SHADOW: return css_is_list(1, s, n);
    case CSS_PROP_TEXT_SHADOW: return css_is_list(2, s, n);
    case CSS_PROP_TRANSFORM: return css_is_transform(s, n);
    case CSS_PROP_TRANSFORM_ORIGIN:
    case CSS_PROP_PERSPECTIVE_ORIGIN:
        if (p == CSS_PROP_PERSPECTIVE_ORIGIN && css_next_part(s, n, &i, &st, &ln) && css_next_part(s, n, &i, &st, &ln) &&
            css_next_part(s, n, &i, &st, &ln)) return 0;
        i = 0;
        while (css_next_part(s, n, &i, &st, &ln)) {
            parts++;
            const char *q = s + st;
            if (!(css_eq_ci(q, ln, "left") || css_eq_ci(q, ln, "right") || css_eq_ci(q, ln, "top") || css_eq_ci(q, ln, "bottom") ||
                  css_eq_ci(q, ln, "center") || css_is_length(q, ln, 1, 1))) return 0;
        }
        return parts >= 1 && parts <= 3;
    case CSS_PROP_CONTENT: return css_is_content(s, n);
    case CSS_PROP_COUNTER_RESET: case CSS_PROP_COUNTER_INCREMENT: {
        int names = 0, last_name = 0;
        while (css_next_part(s, n, &i, &st, &ln)) {
            if (css_is_int(s + st, ln, &k)) { if (!last_name) return 0; last_name = 0; continue; }
            if (!css_is_ident(s + st, ln) || css_eq_ci(s + st, ln, "none")) return 0;
            names++;
            last_name = 1;
        }
        return names > 0;
    }
    case CSS_PROP_QUOTES:
        while (css_next_part(s, n, &i, &st, &ln)) { parts++; if (!css_is_string(s + st, ln)) return 0; }
        return parts > 0 && parts % 2 == 0;
    default: return 0;
    }
}

/* ---- font-family ---- */

/* font-family's list made the family the module draws with: the first of the list that
   is a web font the page declared, a generic family, or a name of one of the built-in
   faces (src/paint/font.h) -- a name it has no face under is passed over (CSS Fonts 4,
   5), as Chromium passes over a font its machine has not. The word: the web font's name
   (lower case, no spaces), or what names the built-in face Chromium draws the family
   with on a Linux machine whose fonts are these (fontconfig's metric aliases taken):
   serif for serif, cursive, fantasy, Times New Roman and Times (Liberation Serif, the
   default too); sans-serif for sans-serif, Arial, Helvetica and system-ui (Chromium's is
   the desktop's face; Liberation Sans stands in); couriernew for Courier New and Courier
   (Liberation Mono); monospace for the generic family when it is the whole list (the
   list font-size's defaults read), dejavusansmono for it in a longer one and by name
   (DejaVu Sans Mono); dejavusans and dejavuserif by name. A quoted name is no generic
   family. */
static int css_family_of(const char *s, size_t n, const char *faces, css_val *v) {
    static const char *const serif = "timesnewroman times";
    static const char *const sans = "arial helvetica";
    static const char *const courier = "couriernew courier";
    static const char *const named = "dejavusans dejavuserif dejavusansmono";
    int alone = css_comma(s, n, 0) >= n;
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        size_t i = css_skip_ws(s, b, a), e = b;
        while (e > i && css_ws(s[e - 1])) e--;
        int quoted = e > i && (s[i] == '"' || s[i] == '\'');
        if (quoted) { i++; if (e > i && (s[e - 1] == '"' || s[e - 1] == '\'')) e--; }
        char w[48];
        size_t k = 0;
        for (size_t j = i; j < e && k + 1 < sizeof w; j++) if (!css_ws(s[j])) w[k++] = css_lower(s[j]);
        w[k] = 0;
        a = b + 1;
        if (!k) continue;
        if (faces && faces[0] && css_word_in(w, faces) && k < sizeof v->word) { css_word_val(v, w); return 1; }
        if (!quoted) {
            if (strcmp(w, "serif") == 0 || strcmp(w, "cursive") == 0 || strcmp(w, "fantasy") == 0) { css_word_val(v, "serif"); return 1; }
            if (strcmp(w, "sans-serif") == 0 || strcmp(w, "system-ui") == 0) { css_word_val(v, "sans-serif"); return 1; }
            if (strcmp(w, "monospace") == 0) { css_word_val(v, alone ? "monospace" : "dejavusansmono"); return 1; }
        }
        if (css_word_in(w, serif)) { css_word_val(v, "serif"); return 1; }
        if (css_word_in(w, sans)) { css_word_val(v, "sans-serif"); return 1; }
        if (css_word_in(w, courier)) { css_word_val(v, "couriernew"); return 1; }
        if (css_word_in(w, named)) { css_word_val(v, w); return 1; }
    }
    return 0;
}


/* ---- declarations: a name and a value made longhands ---- */

/* a longhand a declaration sets: its value, and its text -- a part of the value (ts, tn),
   the whole value for one a shorthand reset, or text the reader made (mine: in buf) */
typedef struct css_item { css_prop prop; css_val v; uint32_t ts, tn, mine; } css_item;
#define CSS_MAXITEMS 40
typedef struct css_items { int n; uint32_t vn, bn; css_item it[CSS_MAXITEMS]; char buf[2048]; } css_items;

static void css_item_add(css_items *it, css_prop p, const css_val *v, size_t ts, size_t tn) {
    if (it->n == CSS_MAXITEMS) return;
    css_item *x = &it->it[it->n];
    x->prop = p;
    x->v = *v;
    x->ts = (uint32_t)ts;
    x->tn = (uint32_t)tn;
    x->mine = 0;
    it->n++;
}
/* an item whose text the reader made */
static void css_item_made(css_items *it, css_prop p, const css_val *v, const char *t, size_t tn) {
    if (it->n == CSS_MAXITEMS || it->bn + tn > sizeof it->buf) return;
    memcpy(it->buf + it->bn, t, tn);
    css_item_add(it, p, v, it->bn, tn);
    it->it[it->n - 1].mine = 1;
    it->bn += (uint32_t)tn;
}
/* the longhand p reset to its initial value by a shorthand */
static void css_item_reset(css_items *it, css_prop p) {
    css_val v;
    css_word_val(&v, "initial");
    css_item_add(it, p, &v, 0, it->vn);
}

/* a font-family list: names (strings, or identifiers a space apart), a comma between each */
static int css_is_family(const char *s, size_t n) {
    size_t a = 0;
    while (a <= n) {
        size_t b = css_comma(s, n, a), i = css_skip_ws(s, b, a), e = b;
        while (e > i && css_ws(s[e - 1])) e--;
        if (e == i) return 0;
        if (!css_is_string(s + i, e - i))
            for (size_t j = i; j < e; j++) if (!css_namechar(s[j]) && !css_ws(s[j])) return 0;
        a = b + 1;
    }
    return 1;
}

/* a value read for the longhand p: a single value, or the text p keeps; 0 when p does not take it */
static int css_longhand(css_prop p, const char *s, size_t n, css_val *v) {
    int g = css_pdefs[p].gram;
    if (g == G_TEXT || g == G_FAMILY) {
        if (css_parse_val(s, n, v)) {
            if (v->unit == CSS_UNIT_AUTO && css_pdefs[p].words && css_word_in("auto", css_pdefs[p].words)) css_word_val(v, "auto");
            if (css_is_global(v) || (v->unit == CSS_UNIT_KEYWORD && css_pdefs[p].words && css_word_in(v->word, css_pdefs[p].words))) return 1;
        }
        if (g == G_FAMILY ? !css_is_family(s, n) : !css_text_valid(p, s, n)) return 0;
        memset(v, 0, sizeof *v);
        v->unit = CSS_UNIT_TEXT;
        return 1;
    }
    if (g == G_RADIUS) {
        /* one length, or two (horizontal, vertical) kept as text */
        size_t st[3], ln[3];
        size_t k = css_parts(s, n, st, ln, 3);
        if (k == 1) return css_parse_val(s, n, v) && (css_is_global(v) || css_is_length(s, n, 1, 0));
        if (k != 2 || !css_is_length(s + st[0], ln[0], 1, 0) || !css_is_length(s + st[1], ln[1], 1, 0)) return 0;
        memset(v, 0, sizeof *v);
        v->unit = CSS_UNIT_TEXT;
        return 1;
    }
    if (g == G_COLOR) {
        /* light-dark(): kept as its text, made a colour by the element's scheme in the cascade */
        uint32_t c;
        if (css_parse_val(s, n, v) && css_valid(p, v)) return 1;
        if (!css_scheme_color(s, n, 0, &c)) return 0;
        memset(v, 0, sizeof *v);
        v->unit = CSS_UNIT_TEXT;
        return 1;
    }
    if (p == CSS_PROP_ALIGN_ITEMS || p == CSS_PROP_ALIGN_SELF) {
        /* first baseline (baseline's other name) and last baseline: the keywords baseline and
           last-baseline, the text kept as written (Box Alignment 3, 4.2) */
        size_t st[3], ln[3];
        if (css_parts(s, n, st, ln, 3) == 2 && css_eq_ci(s + st[1], ln[1], "baseline")) {
            if (css_eq_ci(s + st[0], ln[0], "first")) { css_word_val(v, "baseline"); return 1; }
            if (css_eq_ci(s + st[0], ln[0], "last")) { css_word_val(v, "last-baseline"); return 1; }
        }
    }
    return css_parse_val(s, n, v) && css_valid(p, v);
}

static const size_t css_side_of[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };

/* sides: one to four values for the four longhands base.. (top right bottom left; or the
   corners, top-left top-right bottom-right bottom-left) */
static int css_sides(css_prop base, const char *val, size_t vn, css_items *it) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    if (k == 0 || k > 4) return 0;
    css_val v[4];
    for (size_t j = 0; j < k; j++)
        if (!css_longhand(base, val + st[j], ln[j], &v[j]) || css_is_global(&v[j])) return 0;
    for (int side = 0; side < 4; side++) {
        size_t j = css_side_of[k - 1][side];
        css_item_add(it, (css_prop)(base + side), &v[j], st[j], ln[j]);
    }
    return 1;
}

/* border-radius: one to four radii, and after a / one to four vertical ones */
static int css_radii(const char *val, size_t vn, css_items *it) {
    size_t slash = 0;
    while (slash < vn && val[slash] != '/') slash++;
    if (slash >= vn) return css_sides(CSS_PROP_BORDER_TOP_LEFT_RADIUS, val, vn, it);
    const char *vv = val + slash + 1;
    size_t hs[CSS_MAXPARTS], hl[CSS_MAXPARTS], vs[CSS_MAXPARTS], vl[CSS_MAXPARTS];
    size_t hk = css_parts(val, slash, hs, hl, CSS_MAXPARTS), vk = css_parts(vv, vn - slash - 1, vs, vl, CSS_MAXPARTS);
    if (hk == 0 || hk > 4 || vk == 0 || vk > 4) return 0;
    for (size_t j = 0; j < hk; j++) if (!css_is_length(val + hs[j], hl[j], 1, 0)) return 0;
    for (size_t j = 0; j < vk; j++) if (!css_is_length(vv + vs[j], vl[j], 1, 0)) return 0;
    for (int c = 0; c < 4; c++) {
        css_val v;
        memset(&v, 0, sizeof v);
        v.unit = CSS_UNIT_TEXT;
        size_t a = css_side_of[hk - 1][c], b = css_side_of[vk - 1][c];
        char t[160];
        int tn = snprintf(t, sizeof t, "%.*s %.*s", (int)hl[a], val + hs[a], (int)vl[b], vv + vs[b]);
        if (tn < 0 || (size_t)tn >= sizeof t) return 0;
        css_item_made(it, (css_prop)(CSS_PROP_BORDER_TOP_LEFT_RADIUS + c), &v, t, (size_t)tn);
    }
    return 1;
}

/* border, border-top ... border-left, outline: a width, a style and a colour in any
   order, what is left out reset; side -1 is the four, 4 the outline */
static int css_border(int side, const char *val, size_t vn, css_items *it) {
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
    if (k == 0 || k > 3) return 0;
    css_val width, style, color, v;
    size_t wj = 0, sj = 0, cj = 0;
    int has_width = 0, has_color = 0, has_style = 0;
    css_prop wp = side == 4 ? CSS_PROP_OUTLINE_WIDTH : CSS_PROP_BORDER_TOP_WIDTH;
    css_prop sp = side == 4 ? CSS_PROP_OUTLINE_STYLE : CSS_PROP_BORDER_TOP_STYLE;
    for (size_t j = 0; j < k; j++) {
        uint32_t c;
        if (!css_parse_val(val + st[j], ln[j], &v)) {
            /* light-dark(): kept as its text, made a colour by the element's scheme */
            if (has_color || !css_scheme_color(val + st[j], ln[j], 0, &c)) return 0;
            memset(&color, 0, sizeof color);
            color.unit = CSS_UNIT_TEXT;
            has_color = 1;
            cj = j;
            continue;
        }
        if (css_is_global(&v)) return 0;
        if (v.unit == CSS_UNIT_COLOR || css_is_word(&v, "currentcolor") ||
            (v.unit == CSS_UNIT_KEYWORD && css_system_color(v.word, strlen(v.word), 0, &c))) {
            if (has_color) return 0;
            color = v; has_color = 1; cj = j;
        } else if ((v.unit == CSS_UNIT_KEYWORD || v.unit == CSS_UNIT_AUTO) && css_valid(sp, &v)) {
            if (has_style) return 0;
            style = v; has_style = 1; sj = j;
        } else if (css_valid(wp, &v)) {
            if (has_width) return 0;
            width = v; has_width = 1; wj = j;
        } else return 0;
    }
    int from = side < 0 || side == 4 ? 0 : side, to = side < 0 ? 4 : side == 4 ? 1 : side + 1;
    for (int i = from; i < to; i++) {
        css_prop pw = side == 4 ? CSS_PROP_OUTLINE_WIDTH : (css_prop)(CSS_PROP_BORDER_TOP_WIDTH + i);
        css_prop ps = side == 4 ? CSS_PROP_OUTLINE_STYLE : (css_prop)(CSS_PROP_BORDER_TOP_STYLE + i);
        css_prop pc = side == 4 ? CSS_PROP_OUTLINE_COLOR : (css_prop)(CSS_PROP_BORDER_TOP_COLOR + i);
        if (has_width) css_item_add(it, pw, &width, st[wj], ln[wj]); else css_item_reset(it, pw);
        if (has_style) css_item_add(it, ps, &style, st[sj], ln[sj]); else css_item_reset(it, ps);
        if (has_color) css_item_add(it, pc, &color, st[cj], ln[cj]); else css_item_reset(it, pc);
    }
    return 1;
}

/* font: [style || weight || variant]? size[/line-height] family; what it leaves out is reset */
static int css_font(const char *val, size_t vn, css_items *it) {
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
        if (!css_parse_val(p, slash, &v) || css_is_global(&v)) return 0;
        if (slash == pn && v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "italic oblique")) style = v;
        else if (slash == pn && v.unit == CSS_UNIT_KEYWORD && css_word_in(v.word, "normal small-caps")) {}
        else if (slash == pn && css_valid(CSS_PROP_FONT_WEIGHT, &v)) weight = v;
        else if (css_valid(CSS_PROP_FONT_SIZE, &v)) {
            size = v;
            have_size = 1;
            if (slash < pn && (!css_parse_val(p + slash + 1, pn - slash - 1, &lh) || !css_valid(CSS_PROP_LINE_HEIGHT, &lh) || css_is_global(&lh))) return 0;
        } else return 0;
    }
    if (!have_size) return 0;     /* caption, menu and the other system fonts: not read */
    /* the family list follows the size (and its line height) */
    size_t after = size_j + 1 < k ? st[size_j + 1] : vn;
    if (after >= vn || !css_is_family(val + after, vn - after)) return 0;
    css_val fam;
    memset(&fam, 0, sizeof fam);
    fam.unit = CSS_UNIT_TEXT;
    css_item_add(it, CSS_PROP_FONT_FAMILY, &fam, after, vn - after);
    css_item_add(it, CSS_PROP_FONT_STYLE, &style, 0, vn);
    css_item_add(it, CSS_PROP_FONT_WEIGHT, &weight, 0, vn);
    css_item_add(it, CSS_PROP_FONT_SIZE, &size, 0, vn);
    css_item_add(it, CSS_PROP_LINE_HEIGHT, &lh, 0, vn);
    return 1;
}

/* background: layers, a comma between each, of an image, a position (and / a size) and a
   repeat; the last layer's colour. Each list longhand an item a layer; what a layer
   leaves out is its initial value */
static int css_background(const char *val, size_t vn, css_items *it) {
    vector<char> img, pos, size, rep, org, clp;
    css_val color;
    memset(&color, 0, sizeof color);
    color.unit = CSS_UNIT_COLOR;
    vector<char> ctext;                                 /* a light-dark() colour's text */
    int layers = 0, any_image = 0, has_color = 0;
    size_t cs = 0, cl = 0, a = 0;
    for (;;) {
        size_t b = css_comma(val, vn, a);
        if (has_color) return 0;                        /* a colour only in the last layer */
        /* the layer's parts, a / a part of its own */
        vector<char> lay;
        for (size_t i = a; i < b; i++) {
            char c = val[i];
            if (c == '(') { size_t e = css_block_end(val, b, i, '(', ')'); size_t stop = e < b ? e + 1 : b; append_range(lay, val + i, stop - i); i = stop - 1; continue; }
            if (c == '"' || c == '\'') { size_t e = css_string_end(val, b, i); size_t stop = e < b ? e + 1 : b; append_range(lay, val + i, stop - i); i = stop - 1; continue; }
            if (c == '/') { append_range(lay, " / ", 3); continue; }
            push_back(lay, c);
        }
        size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
        size_t k = css_parts(data(lay), len(lay), st, ln, CSS_MAXPARTS);
        int has_img = 0, has_size = 0, has_pos = 0, after_slash = 0, boxes = 0;
        vector<char> limg, lpos, lsize, lrep, lorg, lclp;
        for (size_t j = 0; j < k; j++) {
            const char *p = data(lay) + st[j];
            size_t pn = ln[j];
            uint32_t c;
            if (pn == 1 && p[0] == '/') { if (!has_pos || after_slash || has_size) return 0; after_slash = 1; continue; }
            if (after_slash && (css_eq_ci(p, pn, "cover") || css_eq_ci(p, pn, "contain") || css_eq_ci(p, pn, "auto") || css_is_length(p, pn, 1, 0))) {
                if (len(lsize)) push_back(lsize, ' ');
                append_range(lsize, p, pn);
                has_size = 1;
                continue;
            }
            if (after_slash) { if (!has_size) return 0; after_slash = 0; }
            if (css_is_image(p, pn) || css_eq_ci(p, pn, "none")) {
                if (has_img) return 0;
                has_img = 1;
                append_range(limg, p, pn);
                if (!css_eq_ci(p, pn, "none")) any_image = 1;
                continue;
            }
            if (css_eq_ci(p, pn, "repeat-x") || css_eq_ci(p, pn, "repeat-y") || css_eq_ci(p, pn, "repeat") || css_eq_ci(p, pn, "no-repeat") ||
                css_eq_ci(p, pn, "space") || css_eq_ci(p, pn, "round")) {
                if (len(lrep)) push_back(lrep, ' ');
                append_range(lrep, p, pn);
                continue;
            }
            if (css_eq_ci(p, pn, "left") || css_eq_ci(p, pn, "right") || css_eq_ci(p, pn, "top") || css_eq_ci(p, pn, "bottom") ||
                css_eq_ci(p, pn, "center") || css_is_length(p, pn, 1, 1)) {
                if (has_size) return 0;
                if (len(lpos)) push_back(lpos, ' ');
                append_range(lpos, p, pn);
                has_pos = 1;
                continue;
            }
            if (css_eq_ci(p, pn, "scroll") || css_eq_ci(p, pn, "fixed") || css_eq_ci(p, pn, "local")) continue;
            if (css_eq_ci(p, pn, "border-box") || css_eq_ci(p, pn, "padding-box") || css_eq_ci(p, pn, "content-box") ||
                (boxes == 1 && css_eq_ci(p, pn, "text"))) {
                /* a layer's boxes: the first its origin and its clip, a second its clip */
                if (boxes == 2) return 0;
                if (boxes == 0) append_range(lorg, p, pn);
                clear(lclp);
                append_range(lclp, p, pn);
                boxes++;
                continue;
            }
            if (css_eq_ci(p, pn, "currentcolor")) { if (has_color) return 0; css_word_val(&color, "currentcolor"); has_color = 1; continue; }
            if (css_parse_color(p, pn, &c)) {
                if (has_color) return 0;
                color.unit = CSS_UNIT_COLOR;
                color.color = c;
                has_color = 1;
                continue;
            }
            if (css_scheme_color(p, pn, 0, &c)) {
                /* a system colour (its keyword) or light-dark() (its text), made a colour by the
                   element's scheme in the cascade */
                if (has_color) return 0;
                if (!css_parse_val(p, pn, &color) || color.unit != CSS_UNIT_KEYWORD) {
                    memset(&color, 0, sizeof color);
                    color.unit = CSS_UNIT_TEXT;
                    append_range(ctext, p, pn);
                }
                has_color = 1;
                continue;
            }
            return 0;
        }
        if (after_slash && !has_size) return 0;
        if (layers) { append_range(img, ", ", 2); append_range(pos, ", ", 2); append_range(size, ", ", 2); append_range(rep, ", ", 2); }
        if (has_img) append_range(img, data(limg), len(limg)); else append_range(img, "none", 4);
        if (has_pos) append_range(pos, data(lpos), len(lpos)); else append_range(pos, "0% 0%", 5);
        if (has_size) append_range(size, data(lsize), len(lsize)); else append_range(size, "auto", 4);
        if (len(lrep)) append_range(rep, data(lrep), len(lrep)); else append_range(rep, "repeat", 6);
        if (layers) { append_range(org, ", ", 2); append_range(clp, ", ", 2); }
        if (len(lorg)) append_range(org, data(lorg), len(lorg)); else append_range(org, "padding-box", 11);
        if (len(lclp)) append_range(clp, data(lclp), len(lclp)); else append_range(clp, "border-box", 10);
        layers++;
        if (b >= vn) break;
        a = b + 1;
    }
    (void)cs;
    (void)cl;
    if (color.unit == CSS_UNIT_TEXT) css_item_made(it, CSS_PROP_BACKGROUND_COLOR, &color, data(ctext), len(ctext));
    else css_item_add(it, CSS_PROP_BACKGROUND_COLOR, &color, 0, vn);
    css_val t, none;
    memset(&t, 0, sizeof t);
    t.unit = CSS_UNIT_TEXT;
    css_word_val(&none, "none");
    if (any_image) css_item_made(it, CSS_PROP_BACKGROUND_IMAGE, &t, data(img), len(img));
    else css_item_made(it, CSS_PROP_BACKGROUND_IMAGE, &none, "none", 4);
    css_item_made(it, CSS_PROP_BACKGROUND_POSITION, &t, data(pos), len(pos));
    css_item_made(it, CSS_PROP_BACKGROUND_SIZE, &t, data(size), len(size));
    css_item_made(it, CSS_PROP_BACKGROUND_REPEAT, &t, data(rep), len(rep));
    css_item_made(it, CSS_PROP_BACKGROUND_ORIGIN, &t, data(org), len(org));
    css_item_made(it, CSS_PROP_BACKGROUND_CLIP, &t, data(clp), len(clp));
    return 1;
}

/* the text up to the top-level /s of s: its pieces' bounds, at most max; their count */
static size_t css_slashes(const char *s, size_t n, size_t *st, size_t *ln, size_t max) {
    size_t k = 0, a = 0;
    for (size_t i = 0; i <= n && k < max; i++) {
        if (i < n && s[i] == '(') { i = css_block_end(s, n, i, '(', ')'); continue; }
        if (i < n && s[i] != '/') continue;
        size_t b = css_skip_ws(s, i, a), e = i;
        while (e > b && css_ws(s[e - 1])) e--;
        st[k] = b;
        ln[k] = e - b;
        k++;
        a = i + 1;
    }
    return k;
}

/* the shorthands this reads, each with its longhands */
typedef struct css_short { const char *name; uint8_t n; uint8_t p[12]; } css_short;
static const css_short css_shorts[] = {
    { "margin", 4, { CSS_PROP_MARGIN_TOP, CSS_PROP_MARGIN_RIGHT, CSS_PROP_MARGIN_BOTTOM, CSS_PROP_MARGIN_LEFT } },
    { "padding", 4, { CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_RIGHT, CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT } },
    { "inset", 4, { CSS_PROP_TOP, CSS_PROP_RIGHT, CSS_PROP_BOTTOM, CSS_PROP_LEFT } },
    { "border-width", 4, { CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_RIGHT_WIDTH, CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_LEFT_WIDTH } },
    { "border-style", 4, { CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_RIGHT_STYLE, CSS_PROP_BORDER_BOTTOM_STYLE, CSS_PROP_BORDER_LEFT_STYLE } },
    { "border-color", 4, { CSS_PROP_BORDER_TOP_COLOR, CSS_PROP_BORDER_RIGHT_COLOR, CSS_PROP_BORDER_BOTTOM_COLOR, CSS_PROP_BORDER_LEFT_COLOR } },
    { "border-radius", 4, { CSS_PROP_BORDER_TOP_LEFT_RADIUS, CSS_PROP_BORDER_TOP_RIGHT_RADIUS, CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS,
                            CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS } },
    { "border", 12, { CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_TOP_COLOR, CSS_PROP_BORDER_RIGHT_WIDTH,
                      CSS_PROP_BORDER_RIGHT_STYLE, CSS_PROP_BORDER_RIGHT_COLOR, CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_BOTTOM_STYLE,
                      CSS_PROP_BORDER_BOTTOM_COLOR, CSS_PROP_BORDER_LEFT_WIDTH, CSS_PROP_BORDER_LEFT_STYLE, CSS_PROP_BORDER_LEFT_COLOR } },
    { "border-top", 3, { CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_TOP_COLOR } },
    { "border-right", 3, { CSS_PROP_BORDER_RIGHT_WIDTH, CSS_PROP_BORDER_RIGHT_STYLE, CSS_PROP_BORDER_RIGHT_COLOR } },
    { "border-bottom", 3, { CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_BOTTOM_STYLE, CSS_PROP_BORDER_BOTTOM_COLOR } },
    { "border-left", 3, { CSS_PROP_BORDER_LEFT_WIDTH, CSS_PROP_BORDER_LEFT_STYLE, CSS_PROP_BORDER_LEFT_COLOR } },
    { "outline", 3, { CSS_PROP_OUTLINE_WIDTH, CSS_PROP_OUTLINE_STYLE, CSS_PROP_OUTLINE_COLOR } },
    { "font", 5, { CSS_PROP_FONT_FAMILY, CSS_PROP_FONT_STYLE, CSS_PROP_FONT_WEIGHT, CSS_PROP_FONT_SIZE, CSS_PROP_LINE_HEIGHT } },
    { "background", 7, { CSS_PROP_BACKGROUND_COLOR, CSS_PROP_BACKGROUND_IMAGE, CSS_PROP_BACKGROUND_POSITION, CSS_PROP_BACKGROUND_SIZE,
                         CSS_PROP_BACKGROUND_REPEAT, CSS_PROP_BACKGROUND_ORIGIN, CSS_PROP_BACKGROUND_CLIP } },
    { "list-style", 2, { CSS_PROP_LIST_STYLE_TYPE, CSS_PROP_LIST_STYLE_POSITION } },
    { "text-decoration", 1, { CSS_PROP_TEXT_DECORATION } },
    { "overflow", 2, { CSS_PROP_OVERFLOW_X, CSS_PROP_OVERFLOW_Y } },
    { "flex", 3, { CSS_PROP_FLEX_GROW, CSS_PROP_FLEX_SHRINK, CSS_PROP_FLEX_BASIS } },
    { "flex-flow", 2, { CSS_PROP_FLEX_DIRECTION, CSS_PROP_FLEX_WRAP } },
    { "gap", 2, { CSS_PROP_ROW_GAP, CSS_PROP_COLUMN_GAP } },
    { "grid-gap", 2, { CSS_PROP_ROW_GAP, CSS_PROP_COLUMN_GAP } },
    { "place-content", 2, { CSS_PROP_ALIGN_CONTENT, CSS_PROP_JUSTIFY_CONTENT } },
    { "place-items", 2, { CSS_PROP_ALIGN_ITEMS, CSS_PROP_JUSTIFY_ITEMS } },
    { "place-self", 2, { CSS_PROP_ALIGN_SELF, CSS_PROP_JUSTIFY_SELF } },
    { "grid-row", 2, { CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_ROW_END } },
    { "grid-column", 2, { CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_COLUMN_END } },
    { "grid-area", 4, { CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_ROW_END, CSS_PROP_GRID_COLUMN_END } },
    { "grid-template", 3, { CSS_PROP_GRID_TEMPLATE_ROWS, CSS_PROP_GRID_TEMPLATE_COLUMNS, CSS_PROP_GRID_TEMPLATE_AREAS } },
    { "animation", 8, { CSS_PROP_ANIMATION_NAME, CSS_PROP_ANIMATION_DURATION, CSS_PROP_ANIMATION_TIMING_FUNCTION, CSS_PROP_ANIMATION_DELAY,
                        CSS_PROP_ANIMATION_ITERATION_COUNT, CSS_PROP_ANIMATION_DIRECTION, CSS_PROP_ANIMATION_FILL_MODE,
                        CSS_PROP_ANIMATION_PLAY_STATE } },
    { "transition", 5, { CSS_PROP_TRANSITION_PROPERTY, CSS_PROP_TRANSITION_DURATION, CSS_PROP_TRANSITION_TIMING_FUNCTION,
                         CSS_PROP_TRANSITION_DELAY, CSS_PROP_TRANSITION_BEHAVIOR } },
};
#define CSS_NSHORTS (int)(sizeof css_shorts / sizeof css_shorts[0])

static const char *css_short_name(int i) { return css_shorts[i].name; }

static int css_short_of(const char *name, size_t nn) {
    if (!css_ready) css_order_make();
    int c = css_name_find(name, nn);
    return c < 0 ? -c - 1 : -1;
}

/* the longhand a property's other name names: grid-row-gap, grid-column-gap */
static int css_alias_of(const char *name, size_t nn) {
    if (css_eq_ci(name, nn, "grid-row-gap")) return CSS_PROP_ROW_GAP;
    if (css_eq_ci(name, nn, "grid-column-gap")) return CSS_PROP_COLUMN_GAP;
    return -1;
}

/* animation's and transition's shorthands (CSS Animations 1, 3.10; CSS Transitions 1,
   2.5): each item of the comma-separated list one animation (transition), its parts in
   any order: the first time its duration and the second its delay, an easing, and the
   rest each its own longhand's (a count, a direction, a fill mode, a play state; a
   transition's behaviour); a keyword the first longhand that takes it and has none yet
   takes, so that what is left is the animation's name (the transition's property). Each
   longhand's list as text, what an item leaves out its initial value */
static int css_anim_short(int transition, const char *val, size_t vn, css_items *it) {
    static const css_prop ap[8] = { CSS_PROP_ANIMATION_NAME, CSS_PROP_ANIMATION_DURATION, CSS_PROP_ANIMATION_TIMING_FUNCTION,
                                    CSS_PROP_ANIMATION_DELAY, CSS_PROP_ANIMATION_ITERATION_COUNT, CSS_PROP_ANIMATION_DIRECTION,
                                    CSS_PROP_ANIMATION_FILL_MODE, CSS_PROP_ANIMATION_PLAY_STATE };
    static const css_prop tp[5] = { CSS_PROP_TRANSITION_PROPERTY, CSS_PROP_TRANSITION_DURATION, CSS_PROP_TRANSITION_TIMING_FUNCTION,
                                    CSS_PROP_TRANSITION_DELAY, CSS_PROP_TRANSITION_BEHAVIOR };
    static const char *const adef[8] = { "none", "0s", "ease", "0s", "1", "normal", "none", "running" };
    static const char *const tdef[5] = { "all", "0s", "ease", "0s", "normal" };
    int nl = transition ? 5 : 8;
    vector<char> l0, l1, l2, l3, l4, l5, l6, l7;
    size_t a = 0;
    int items = 0;
    while (a <= vn) {
        size_t b = css_comma(val, vn, a);
        size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
        size_t k = css_parts(val + a, b - a, st, ln, CSS_MAXPARTS);
        /* each longhand's part in this item: its start and length, unset at -1 */
        long ps[8], pl[8];
        for (int q = 0; q < 8; q++) { ps[q] = -1; pl[q] = 0; }
        int times = 0;
        for (size_t j = 0; j < k; j++) {
            const char *x = val + a + st[j];
            size_t xl = ln[j];
            double ms;
            int slot = -1;
            if (css_time_ms(x, xl, &ms)) {
                if (times == 2) return 0;
                slot = times++ == 0 ? 1 : 3;
                if (slot == 1 && ms < 0) return 0;
            } else if (css_is_easing(x, xl) && ps[2] < 0) slot = 2;
            else if (!transition && ps[4] < 0 && css_anim_item(CSS_PROP_ANIMATION_ITERATION_COUNT, x, xl)) slot = 4;
            else if (!transition && ps[5] < 0 && css_anim_item(CSS_PROP_ANIMATION_DIRECTION, x, xl)) slot = 5;
            else if (!transition && ps[6] < 0 && css_anim_item(CSS_PROP_ANIMATION_FILL_MODE, x, xl)) slot = 6;
            else if (!transition && ps[7] < 0 && css_anim_item(CSS_PROP_ANIMATION_PLAY_STATE, x, xl)) slot = 7;
            else if (transition && ps[4] < 0 && css_anim_item(CSS_PROP_TRANSITION_BEHAVIOR, x, xl)) slot = 4;
            else if (ps[0] < 0 && (transition ? css_is_ident(x, xl) : css_anim_item(CSS_PROP_ANIMATION_NAME, x, xl))) slot = 0;
            if (slot < 0 || ps[slot] >= 0) return 0;
            ps[slot] = (long)(a + st[j]);
            pl[slot] = (long)xl;
        }
        /* a transition of none: none alone in the list */
        if (transition && ps[0] >= 0 && css_eq_ci(val + ps[0], (size_t)pl[0], "none") && (items || b < vn)) return 0;
        for (int q = 0; q < nl; q++) {
            vector<char> *l = q == 0 ? &l0 : q == 1 ? &l1 : q == 2 ? &l2 : q == 3 ? &l3 : q == 4 ? &l4 : q == 5 ? &l5 : q == 6 ? &l6 : &l7;
            if (items) append_range(*l, ", ", 2);
            if (ps[q] >= 0) append_range(*l, val + ps[q], (size_t)pl[q]);
            else if (transition) append_range(*l, tdef[q], strlen(tdef[q]));
            else append_range(*l, adef[q], strlen(adef[q]));
        }
        items++;
        if (b >= vn) break;
        a = b + 1;
    }
    if (!items) return 0;
    css_val t;
    memset(&t, 0, sizeof t);
    t.unit = CSS_UNIT_TEXT;
    for (int q = 0; q < nl; q++) {
        vector<char> *l = q == 0 ? &l0 : q == 1 ? &l1 : q == 2 ? &l2 : q == 3 ? &l3 : q == 4 ? &l4 : q == 5 ? &l5 : q == 6 ? &l6 : &l7;
        css_item_made(it, transition ? tp[q] : ap[q], &t, data(*l), len(*l));
    }
    return 1;
}

/* a shorthand's value read into its longhands: 0 when it is none it takes */
static int css_short_read(int sh, const char *val, size_t vn, css_items *it) {
    const css_short *d = &css_shorts[sh];
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
    size_t k;
    css_val v, w;
    switch (sh) {
    case 0: case 1: case 2: case 3: case 4: case 5: return css_sides((css_prop)d->p[0], val, vn, it);
    case 6: return css_radii(val, vn, it);
    case 7: return css_border(-1, val, vn, it);
    case 8: case 9: case 10: case 11: return css_border(sh - 8, val, vn, it);
    case 12: return css_border(4, val, vn, it);
    case 13: return css_font(val, vn, it);
    case 14: return css_background(val, vn, it);
    case 15: {
        /* list-style: a type, a position, an image (read past); none is the type's */
        int typ = 0, posn = 0;
        k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
        for (size_t j = 0; j < k; j++) {
            if (!css_parse_val(val + st[j], ln[j], &v) || css_is_global(&v)) {
                if (css_is_fn(val + st[j], ln[j], "url")) continue;
                return 0;
            }
            if (css_valid(CSS_PROP_LIST_STYLE_POSITION, &v)) { if (posn++) return 0; css_item_add(it, CSS_PROP_LIST_STYLE_POSITION, &v, st[j], ln[j]); }
            else if (css_valid(CSS_PROP_LIST_STYLE_TYPE, &v)) { if (typ++) return 0; css_item_add(it, CSS_PROP_LIST_STYLE_TYPE, &v, st[j], ln[j]); }
            else return 0;
        }
        if (!typ) { css_word_val(&w, "disc"); css_item_add(it, CSS_PROP_LIST_STYLE_TYPE, &w, 0, vn); }
        if (!posn) css_item_reset(it, CSS_PROP_LIST_STYLE_POSITION);
        return 1;
    }
    case 16: {
        /* text-decoration: its line (the colour, style and thickness read past) */
        int line = 0;
        k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
        for (size_t j = 0; j < k; j++) {
            if (!css_parse_val(val + st[j], ln[j], &v) || css_is_global(&v)) return 0;
            if (css_valid(CSS_PROP_TEXT_DECORATION, &v)) { if (!line++) css_item_add(it, CSS_PROP_TEXT_DECORATION, &v, st[j], ln[j]); }
        }
        if (!line) { css_word_val(&w, "none"); css_item_add(it, CSS_PROP_TEXT_DECORATION, &w, 0, vn); }
        return 1;
    }
    case 17: case 19: case 20: case 21: case 22: case 23: case 24: {
        /* two longhands: one value for both (place-*, overflow, gap) or two; flex-flow's in any order */
        k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
        if (k == 0 || k > 2) return 0;
        if (sh == 19) {
            int dir = 0, wrap = 0;
            for (size_t j = 0; j < k; j++) {
                if (!css_parse_val(val + st[j], ln[j], &v) || css_is_global(&v)) return 0;
                if (css_valid(CSS_PROP_FLEX_DIRECTION, &v)) { if (dir++) return 0; css_item_add(it, CSS_PROP_FLEX_DIRECTION, &v, st[j], ln[j]); }
                else if (css_valid(CSS_PROP_FLEX_WRAP, &v)) { if (wrap++) return 0; css_item_add(it, CSS_PROP_FLEX_WRAP, &v, st[j], ln[j]); }
                else return 0;
            }
            if (!dir) css_item_reset(it, CSS_PROP_FLEX_DIRECTION);
            if (!wrap) css_item_reset(it, CSS_PROP_FLEX_WRAP);
            return 1;
        }
        if (!css_longhand((css_prop)d->p[0], val + st[0], ln[0], &v) || css_is_global(&v)) return 0;
        size_t j2 = k == 2 ? 1 : 0;
        if (!css_longhand((css_prop)d->p[1], val + st[j2], ln[j2], &w) || css_is_global(&w)) return 0;
        css_item_add(it, (css_prop)d->p[0], &v, st[0], ln[0]);
        css_item_add(it, (css_prop)d->p[1], &w, st[j2], ln[j2]);
        return 1;
    }
    case 18: {
        /* flex: none, auto, or a grow factor, a shrink factor and a basis (a number alone
           is a grow factor with a basis of 0%) */
        css_val g, s, b;
        memset(&g, 0, sizeof g); g.unit = CSS_UNIT_NUMBER; g.num = 1;
        s = g;
        memset(&b, 0, sizeof b); b.unit = CSS_UNIT_PERCENT; b.num = 0;
        if (css_eq_ci(val, vn, "none")) { g.num = 0; s.num = 0; b.unit = CSS_UNIT_AUTO; }
        else if (css_eq_ci(val, vn, "auto")) b.unit = CSS_UNIT_AUTO;
        else {
            k = css_parts(val, vn, st, ln, CSS_MAXPARTS);
            if (k == 0 || k > 3) return 0;
            int nums = 0, basis = 0;
            for (size_t j = 0; j < k; j++) {
                if (!css_parse_val(val + st[j], ln[j], &v) || css_is_global(&v)) return 0;
                if (v.unit == CSS_UNIT_NUMBER && !(basis && nums == 1) && nums < 2 && !(v.num == 0 && nums == 2)) {
                    if (v.num < 0) return 0;
                    if (nums == 0) g.num = v.num; else s.num = v.num;
                    nums++;
                    continue;
                }
                if (basis || !css_valid(CSS_PROP_FLEX_BASIS, &v)) return 0;
                b = v;
                basis = 1;
            }
        }
        css_item_add(it, CSS_PROP_FLEX_GROW, &g, 0, vn);
        css_item_add(it, CSS_PROP_FLEX_SHRINK, &s, 0, vn);
        css_item_add(it, CSS_PROP_FLEX_BASIS, &b, 0, vn);
        return 1;
    }
    case 25: case 26: {
        /* grid-row, grid-column: a start line, and after a / an end line (a name alone is both) */
        k = css_slashes(val, vn, st, ln, 3);
        if (k == 0 || k > 2) return 0;
        if (!css_longhand((css_prop)d->p[0], val + st[0], ln[0], &v) || css_is_global(&v)) return 0;
        css_item_add(it, (css_prop)d->p[0], &v, st[0], ln[0]);
        if (k == 2) {
            if (!css_longhand((css_prop)d->p[1], val + st[1], ln[1], &w) || css_is_global(&w)) return 0;
            css_item_add(it, (css_prop)d->p[1], &w, st[1], ln[1]);
        } else if (css_is_ident(val + st[0], ln[0]) && !css_eq_ci(val + st[0], ln[0], "auto")) css_item_add(it, (css_prop)d->p[1], &v, st[0], ln[0]);
        else { css_val au; memset(&au, 0, sizeof au); au.unit = CSS_UNIT_TEXT; css_item_made(it, (css_prop)d->p[1], &au, "auto", 4); }
        return 1;
    }
    case 27: {
        /* grid-area: row-start / column-start / row-end / column-end; a part left out is the
           one it pairs with when that is a name (column-start and row-end row-start's,
           column-end column-start's), else auto (Grid 1, 8.4) */
        k = css_slashes(val, vn, st, ln, 5);
        if (k == 0 || k > 4) return 0;
        for (size_t j = 0; j < 4; j++) {
            css_val x;
            if (j < k) {
                if (!css_longhand((css_prop)d->p[j], val + st[j], ln[j], &x) || css_is_global(&x)) return 0;
                css_item_add(it, (css_prop)d->p[j], &x, st[j], ln[j]);
                continue;
            }
            size_t f = j == 3 && k >= 2 ? 1 : 0;
            if (css_is_ident(val + st[f], ln[f]) && !css_eq_ci(val + st[f], ln[f], "auto") && !css_eq_ci(val + st[f], ln[f], "span")) {
                if (!css_longhand((css_prop)d->p[j], val + st[f], ln[f], &x)) return 0;
                css_item_add(it, (css_prop)d->p[j], &x, st[f], ln[f]);
            } else { memset(&x, 0, sizeof x); x.unit = CSS_UNIT_TEXT; css_item_made(it, (css_prop)d->p[j], &x, "auto", 4); }
        }
        return 1;
    }
    case 28: {
        /* grid-template: none; rows / columns; or the areas' strings, each row's after its
           string with its line names, and after a / the columns (Grid 1, 7.4) */
        if (css_eq_ci(val, vn, "none")) {
            css_word_val(&v, "none");
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_ROWS, &v, 0, vn);
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_COLUMNS, &v, 0, vn);
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_AREAS, &v, 0, vn);
            return 1;
        }
        k = css_slashes(val, vn, st, ln, 3);
        if (k == 0 || k > 2) return 0;
        size_t q = css_skip_ws(val, st[0] + ln[0], st[0]);
        int strings = 0;
        for (size_t j = st[0]; j < st[0] + ln[0]; j++) if (val[j] == '"' || val[j] == '\'') strings = 1;
        if (!strings) {
            if (k != 2) return 0;
            if (!css_longhand(CSS_PROP_GRID_TEMPLATE_ROWS, val + st[0], ln[0], &v) || !css_longhand(CSS_PROP_GRID_TEMPLATE_COLUMNS, val + st[1], ln[1], &w)) return 0;
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_ROWS, &v, st[0], ln[0]);
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_COLUMNS, &w, st[1], ln[1]);
            css_word_val(&v, "none");
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_AREAS, &v, 0, vn);
            return 1;
        }
        /* the strings are the areas; the rows their sizes (auto where none is given) and
           the line names about them. state: 0 before a string, 1 after one, 2 after its size */
        char rows[1024], areas[1024];
        size_t rn = 0, an = 0, e0 = st[0] + ln[0];
        int state = 0;
        while (q < e0) {
            size_t b = q, e;
            if (val[q] == '"' || val[q] == '\'') {
                e = css_string_end(val, e0, q);
                if (e >= e0) return 0;
                if (state == 1) { if (rn + 5 > sizeof rows) return 0; memcpy(rows + rn, "auto ", 5); rn += 5; }
                if (an + (e - b) + 2 > sizeof areas) return 0;
                memcpy(areas + an, val + b, e - b + 1);
                an += e - b + 1;
                areas[an++] = ' ';
                state = 1;
            } else if (val[q] == '[') {
                e = css_block_end(val, e0, q, '[', ']');
                if (e >= e0) return 0;
                if (state == 1) { if (rn + 5 > sizeof rows) return 0; memcpy(rows + rn, "auto ", 5); rn += 5; state = 2; }
                if (rn + (e - b) + 2 > sizeof rows) return 0;
                memcpy(rows + rn, val + b, e - b + 1);
                rn += e - b + 1;
                rows[rn++] = ' ';
            } else {
                /* a row's size: to the next white space, a function's parentheses kept whole */
                size_t z = q;
                while (z < e0 && !css_ws(val[z])) { if (val[z] == '(') z = css_block_end(val, e0, z, '(', ')'); if (z < e0) z++; }
                e = z - 1;
                if (state != 1 || !css_is_track(val + b, e - b + 1) || rn + (e - b) + 2 > sizeof rows) return 0;
                memcpy(rows + rn, val + b, e - b + 1);
                rn += e - b + 1;
                rows[rn++] = ' ';
                state = 2;
            }
            q = css_skip_ws(val, e0, e + 1);
        }
        if (state == 1) { if (rn + 4 > sizeof rows) return 0; memcpy(rows + rn, "auto", 4); rn += 4; }
        while (rn > 0 && rows[rn - 1] == ' ') rn--;
        while (an > 0 && areas[an - 1] == ' ') an--;
        if (!css_is_areas(areas, an) || !css_is_tracks(rows, rn, 0)) return 0;
        css_val tv;
        memset(&tv, 0, sizeof tv);
        tv.unit = CSS_UNIT_TEXT;
        if (k == 2) {
            if (!css_longhand(CSS_PROP_GRID_TEMPLATE_COLUMNS, val + st[1], ln[1], &w) || css_is_global(&w)) return 0;
            css_item_add(it, CSS_PROP_GRID_TEMPLATE_COLUMNS, &w, st[1], ln[1]);
        } else { css_word_val(&w, "none"); css_item_add(it, CSS_PROP_GRID_TEMPLATE_COLUMNS, &w, 0, vn); }
        css_item_made(it, CSS_PROP_GRID_TEMPLATE_ROWS, &tv, rows, rn);
        css_item_made(it, CSS_PROP_GRID_TEMPLATE_AREAS, &tv, areas, an);
        return 1;
    }
    case 29: return css_anim_short(0, val, vn, it);
    case 30: return css_anim_short(1, val, vn, it);
    default: return 0;
    }
}

/* a declaration's longhands and their values: 0 when it is none this reads */
static int css_expand(const char *name, size_t nn, const char *val, size_t vn, css_items *it) {
    it->n = 0;
    it->bn = 0;
    it->vn = (uint32_t)vn;
    css_val g;
    int global = vn >= 5 && vn <= 12 && css_parse_val(val, vn, &g) && css_is_global(&g);
    int sh = css_short_of(name, nn);
    if (sh >= 0) {
        if (global) {
            for (int k = 0; k < css_shorts[sh].n; k++) css_item_add(it, (css_prop)css_shorts[sh].p[k], &g, 0, vn);
            return 1;
        }
        return css_short_read(sh, val, vn, it);
    }
    int p = css_prop_of(name, nn);
    if (p < 0) p = css_alias_of(name, nn);
    if (p < 0) return 0;
    css_val v;
    if (!css_longhand((css_prop)p, val, vn, &v)) return 0;
    css_item_add(it, (css_prop)p, &v, 0, vn);
    return 1;
}

/* the value holds var() */
static int css_has_var(const char *s, size_t n) {
    for (size_t i = 0; i + 4 <= n; i++)
        if (css_lower(s[i]) == 'v' && css_lower(s[i + 1]) == 'a' && css_lower(s[i + 2]) == 'r' && s[i + 3] == '(' &&
            (i == 0 || !css_namechar(s[i - 1]))) return 1;
    return 0;
}

static void css_add_decl(css_style *s, uint32_t rule, css_prop prop, const css_val *v, int important, const char *text, size_t tn,
                         uint32_t word) {
    amb_span t = text_add(s, text, tn);
    uint32_t w = word ? word : v->unit == CSS_UNIT_KEYWORD ? names_intern(s, v->word, strlen(v->word)) : 0;
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

/* a declaration: its name and value, its shorthand expanded */
static void css_declaration(css_style *s, uint32_t rule, const char *name, size_t nn, const char *val, size_t vn) {
    int important = 0;
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
    if (nn == 0) return;
    css_val v;
    memset(&v, 0, sizeof v);
    if (nn > 2 && name[0] == '-' && name[1] == '-') {
        /* a custom property: its value as written, var() and all, read in the cascade */
        v.unit = CSS_UNIT_TEXT;
        uint32_t w = names_intern(s, name, nn);
        css_add_decl(s, rule, CSS_PROP_CUSTOM, &v, important, val, vn, w);
        return;
    }
    if (vn == 0) return;
    if (css_has_var(val, vn)) {
        /* each longhand waits for the cascade: the value whole, and a shorthand's name */
        v.unit = CSS_UNIT_VAR;
        int sh = css_short_of(name, nn);
        if (sh >= 0) {
            char ln[32];
            snprintf(ln, sizeof ln, "%s", css_shorts[sh].name);
            uint32_t w = names_intern(s, ln, strlen(ln));
            for (int k = 0; k < css_shorts[sh].n; k++) css_add_decl(s, rule, (css_prop)css_shorts[sh].p[k], &v, important, val, vn, w);
            return;
        }
        int p = css_prop_of(name, nn);
        if (p < 0) p = css_alias_of(name, nn);
        if (p >= 0) css_add_decl(s, rule, (css_prop)p, &v, important, val, vn, 0);
        return;
    }
    css_items it;
    if (!css_expand(name, nn, val, vn, &it)) return;
    for (int k = 0; k < it.n; k++) {
        css_item x = it.it[k];
        if (x.mine) css_add_decl(s, rule, x.prop, &x.v, important, it.buf + x.ts, x.tn, 0);
        else css_add_decl(s, rule, x.prop, &x.v, important, val + x.ts, x.tn, 0);
    }
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

/* ---- media queries (Media Queries 4, abridged): against the viewport ---- */

static int css_media_feature(const char *s, size_t n) {
    size_t c = 0;
    while (c < n && s[c] != ':') c++;
    size_t a = css_skip_ws(s, c, 0), b = c;
    while (b > a && css_ws(s[b - 1])) b--;
    const char *name = s + a;
    size_t nn = b - a;
    if (c >= n) return css_eq_ci(name, nn, "color") || css_eq_ci(name, nn, "hover") || css_eq_ci(name, nn, "pointer") ||
                       css_eq_ci(name, nn, "prefers-color-scheme") || (css_eq_ci(name, nn, "prefers-reduced-motion") && css_pref_reduce) ||
                       (css_eq_ci(name, nn, "scripting") && css_scripting);
    size_t va = css_skip_ws(s, n, c + 1), vb = n;
    while (vb > va && css_ws(s[vb - 1])) vb--;
    css_val v;
    if (!css_parse_val(s + va, vb - va, &v)) return 0;
    double px = v.unit == CSS_UNIT_PX ? v.num : v.unit == CSS_UNIT_EM ? v.num * css_font_px : v.unit == CSS_UNIT_NUMBER && v.num == 0 ? 0 : -1;
    if (css_eq_ci(name, nn, "min-width")) return px >= 0 && CSS_VIEWPORT_W >= px;
    if (css_eq_ci(name, nn, "max-width")) return px >= 0 && CSS_VIEWPORT_W <= px;
    if (css_eq_ci(name, nn, "width")) return px == CSS_VIEWPORT_W;
    if (css_eq_ci(name, nn, "min-height")) return px >= 0 && CSS_VIEWPORT_H >= px;
    if (css_eq_ci(name, nn, "max-height")) return px >= 0 && CSS_VIEWPORT_H <= px;
    if (css_eq_ci(name, nn, "orientation")) return css_is_word(&v, "landscape");
    if (css_eq_ci(name, nn, "prefers-color-scheme")) return css_is_word(&v, css_pref_dark ? "dark" : "light");
    if (css_eq_ci(name, nn, "prefers-reduced-motion")) return css_is_word(&v, css_pref_reduce ? "reduce" : "no-preference");
    if (css_eq_ci(name, nn, "scripting")) return css_is_word(&v, css_scripting ? "enabled" : "none");
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

/* ---- the sheet's rules ---- */

/* a style rule: each selector of its list a rule of its own, in the layer layer, with the
   block's declarations */
static void css_style_rule(css_style *s, uint32_t sheet, uint32_t layer, const char *pre, size_t pn, const char *blk, size_t bn) {
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
            s->rule[r.index].layer = layer;
            rules_append(s, sheet, r.index);
            css_parse_block(s, r.index, blk, bn);
        }
        a = b + 1;
    }
}

/* ---- @supports (CSS Conditional 3): its condition, over what the module reads ---- */

static int css_selector_ok(const char *s, size_t n, int depth);

/* a selector's pseudo-class or pseudo-element is one the matcher knows */
static int css_pseudo_known(const char *p, size_t pn, int element, int fn) {
    if (element)
        return !fn && (css_eq_ci(p, pn, "before") || css_eq_ci(p, pn, "after") || css_eq_ci(p, pn, "first-line") ||
                       css_eq_ci(p, pn, "first-letter") || css_eq_ci(p, pn, "marker") || css_eq_ci(p, pn, "placeholder"));
    if (fn)
        return css_eq_ci(p, pn, "not") || css_eq_ci(p, pn, "is") || css_eq_ci(p, pn, "where") || css_eq_ci(p, pn, "matches") ||
               css_eq_ci(p, pn, "has") || css_eq_ci(p, pn, "nth-child") || css_eq_ci(p, pn, "nth-last-child") ||
               css_eq_ci(p, pn, "nth-of-type") || css_eq_ci(p, pn, "nth-last-of-type");
    char w[24];
    if (pn == 0 || pn >= sizeof w) return 0;
    for (size_t i = 0; i < pn; i++) w[i] = css_lower(p[i]);
    w[pn] = 0;
    return css_word_in(w, "first-child last-child only-child first-of-type last-of-type only-of-type root empty link any-link "
                          "visited checked disabled enabled hover focus focus-visible focus-within active target scope before after "
                          "first-line first-letter");
}

/* each compound of a selector is one the matcher reads */
static int css_selector_ok(const char *s, size_t n, int depth) {
    css_part parts[CSS_MAXPARTS];
    size_t k = css_split(s, n, parts, CSS_MAXPARTS);
    if (k == 0 || depth > CSS_MAX_NESTING) return 0;
    for (size_t j = 0; j < k; j++) {
        const char *c = s + parts[j].start;
        size_t cn = parts[j].len, i = 0;
        while (i < cn) {
            char ch = c[i];
            if (ch == '*' || css_namechar(ch)) { i++; continue; }
            if (ch == '#' || ch == '.') { size_t b = ++i; while (i < cn && css_namechar(c[i])) i++; if (i == b) return 0; continue; }
            if (ch == '[') { size_t e = css_block_end(c, cn, i, '[', ']'); if (e >= cn) return 0; i = e + 1; continue; }
            if (ch != ':') return 0;
            int element = i + 1 < cn && c[i + 1] == ':';
            i += element ? 2 : 1;
            size_t b = i;
            while (i < cn && css_namechar(c[i])) i++;
            int fn = i < cn && c[i] == '(';
            if (!css_pseudo_known(c + b, i - b, element, fn)) return 0;
            if (fn) {
                size_t e = css_block_end(c, cn, i, '(', ')');
                if (e >= cn) return 0;
                if (css_eq_ci(c + b, i - b, "not") || css_eq_ci(c + b, i - b, "is") || css_eq_ci(c + b, i - b, "where")) {
                    /* a selector list: each of its selectors read */
                    size_t a = i + 1;
                    while (a < e) {
                        size_t z = css_comma(c, e, a);
                        if (!css_selector_ok(c + a, z - a, depth + 1)) return 0;
                        a = z + 1;
                    }
                }
                i = e + 1;
            }
        }
    }
    return 1;
}

static int css_supports(const char *s, size_t n, int depth);

/* what a test's parentheses hold: a declaration the module reads, or a condition */
static int css_supports_test(const char *in, size_t inn, int depth) {
    {
        size_t colon = 0;
        while (colon < inn && in[colon] != ':' && in[colon] != '(') colon++;
        if (colon < inn && in[colon] == ':') {
            size_t a = css_skip_ws(in, colon, 0), b = colon;
            while (b > a && css_ws(in[b - 1])) b--;
            if (b - a > 2 && in[a] == '-' && in[a + 1] == '-') return 1;   /* a custom property takes any value */
            size_t va = css_skip_ws(in, inn, colon + 1), vb = inn;
            while (vb > va && css_ws(in[vb - 1])) vb--;
            if (vb == va) return 0;
            if (css_has_var(in + va, vb - va)) return css_short_of(in + a, b - a) >= 0 || css_prop_of(in + a, b - a) >= 0;
            css_items it;
            return css_expand(in + a, b - a, in + va, vb - va, &it);
        }
        return css_supports(in, inn, depth + 1);
    }
}

/* one parenthesized test, or selector() */
static int css_supports_in(const char *s, size_t n, int depth) {
    if (n >= 2 && s[0] == '(' && s[n - 1] == ')') return css_supports_test(s + 1, n - 2, depth);
    if (css_is_fn(s, n, "selector")) return css_selector_ok(s + 9, n - 10, 0);
    return 0;               /* font-tech(), font-format() and what else: not known, false */
}

static int css_supports(const char *s, size_t n, int depth) {
    if (depth > 16) return 0;
    /* its parts: parenthesized tests, functions, and the words not, and, or */
    size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS], k = 0, i = 0;
    while (i < n && k < CSS_MAXPARTS) {
        i = css_skip_ws(s, n, i);
        if (i >= n) break;
        size_t b = i;
        if (s[i] == '(') { size_t e = css_block_end(s, n, i, '(', ')'); if (e >= n) return 0; i = e + 1; }
        else {
            while (i < n && css_namechar(s[i])) i++;
            if (i < n && s[i] == '(') { size_t e = css_block_end(s, n, i, '(', ')'); if (e >= n) return 0; i = e + 1; }
            if (i == b) return 0;
        }
        st[k] = b;
        ln[k] = i - b;
        k++;
    }
    if (k == 0) return 0;
    if (css_eq_ci(s + st[0], ln[0], "not")) return k == 2 && !css_supports_in(s + st[1], ln[1], depth);
    int v = css_supports_in(s + st[0], ln[0], depth), op = 0;
    for (size_t j = 1; j + 1 < k; j += 2) {
        int this_op = css_eq_ci(s + st[j], ln[j], "and") ? 1 : css_eq_ci(s + st[j], ln[j], "or") ? 2 : 0;
        if (!this_op || (op && this_op != op)) return 0;          /* and and or are not mixed without parentheses */
        op = this_op;
        int w = css_supports_in(s + st[j + 1], ln[j + 1], depth);
        v = op == 1 ? v && w : v || w;
    }
    return k % 2 == 1 ? v : 0;
}

/* ---- @layer (CSS Cascade 5) ---- */

/* the layer name (one name of a dotted one) in the layer parent, made if it is new */
static uint32_t css_layer_named(css_style *s, uint32_t parent, const char *name, size_t nn) {
    for (uint32_t l = 1; l < s->layer_len; l++) {
        if (s->layer[l].parent != parent) continue;
        amb_span sp = s->layer[l].name;
        const char *t = text_ptr(s, sp);
        size_t dot = sp.len;
        while (dot > 0 && t[dot - 1] != '.') dot--;
        if (sp.len - dot == nn && memcmp(t + dot, name, nn) == 0) return l;
    }
    vector<char> full;
    if (parent) { amb_span pp = s->layer[parent].name; append_range(full, text_ptr(s, pp), pp.len); push_back(full, '.'); }
    append_range(full, name, nn);
    amb_span sp = text_add(s, data(full), len(full));
    css_layer_h h = layer_new(s);
    s->layer[h.index].name = sp;
    s->layer[h.index].parent = parent;
    return h.index;
}

/* a dotted layer name (a.b.c) in the layer parent: its last layer */
static uint32_t css_layer_path(css_style *s, uint32_t parent, const char *name, size_t nn) {
    size_t a = css_skip_ws(name, nn, 0), e = nn;
    while (e > a && css_ws(name[e - 1])) e--;
    uint32_t l = parent;
    while (a < e) {
        size_t b = a;
        while (b < e && name[b] != '.') b++;
        if (b == a) return parent;
        l = css_layer_named(s, l, name + a, b - a);
        a = b + 1;
    }
    return l;
}

/* an anonymous layer in parent: a layer of its own, which no rule names */
static uint32_t css_layer_anon(css_style *s, uint32_t parent) {
    char nm[40];
    int k = snprintf(nm, sizeof nm, "<anonymous %u>", (unsigned)s->layer_len);
    return css_layer_named(s, parent, nm, k > 0 ? (size_t)k : 0);
}

/* each layer's rank: the order its rules cascade in -- a layer's sublayers before its own
   rules, siblings in the order they were first named (a walk, children before parents) */
static uint32_t css_layer_rank(css_style *s, uint32_t parent, uint32_t next) {
    for (uint32_t l = 1; l < s->layer_len; l++) {
        if (s->layer[l].parent != parent) continue;
        next = css_layer_rank(s, l, next);
        s->layer[l].rank = next++;
    }
    return next;
}

/* ---- @import and @font-face: what the loader fetches for a sheet ---- */

/* where a sheet's rules are read: its sheet, its layer, its base URL, how deep in
   @imports, and whether a rule other than @import has been met */
/* a sheet being read: its sheet, layer, depth of @import, origin, base URL; the @scope its
   rules are in (scoped; its roots' and limits' selectors) and the element a sheet's @scope
   without roots is rooted at (a style element's parent) */
typedef struct css_pctx { uint32_t sheet, layer; int depth, ruled; css_origin origin; char base[2048]; int scoped; amb_span scope, scope_end;
                          uint32_t scope_host; } css_pctx;

static void css_rules(css_style *s, net_loader *net, css_pctx *cx, const char *t, size_t n);

static int css_selector_ok(const char *s, size_t n, int depth);

/* a selector list each of whose selectors this reads */
static int css_selector_list_ok(const char *s, size_t n) {
    size_t a = 0;
    while (a <= n) {
        size_t b = css_comma(s, n, a), sa = css_skip_ws(s, b, a), sb = b;
        while (sb > sa && css_ws(s[sb - 1])) sb--;
        if (sb == sa || !css_selector_ok(s + sa, sb - sa, 0)) return 0;
        if (b >= n) break;
        a = b + 1;
    }
    return 1;
}

/* an @scope's prelude (Cascade 6, 2.5): [(roots)] [to (limits)], each a selector list,
   into the context its rules are read in; 0 when it is not one */
static int css_scope_prelude(css_style *s, const char *pre, size_t pn, css_pctx *cx) {
    size_t i = css_skip_ws(pre, pn, 0);
    amb_span none = { 0, 0 };
    cx->scoped = 1;
    cx->scope = none;
    cx->scope_end = none;
    if (i < pn && pre[i] == '(') {
        size_t e = css_block_end(pre, pn, i, '(', ')');
        if (e >= pn || !css_selector_list_ok(pre + i + 1, e - i - 1)) return 0;
        cx->scope = text_add(s, pre + i + 1, e - i - 1);
        i = css_skip_ws(pre, pn, e + 1);
    }
    if (i + 2 <= pn && css_eq_ci(pre + i, 2, "to")) {
        i = css_skip_ws(pre, pn, i + 2);
        if (i >= pn || pre[i] != '(') return 0;
        size_t e = css_block_end(pre, pn, i, '(', ')');
        if (e >= pn || !css_selector_list_ok(pre + i + 1, e - i - 1)) return 0;
        cx->scope_end = text_add(s, pre + i + 1, e - i - 1);
        i = css_skip_ws(pre, pn, e + 1);
    }
    return i >= pn;
}

/* the rules from r0 on, read in an @scope: scoped, their selectors matched below a scoping
   root -- one without :scope or & as the root's descendant (:where(:scope) before it),
   & as the root itself with no specificity of its own (:where(:scope)), as Chromium
   matches and ranks them; the scope's own selectors add none */
static void css_scope_rules(css_style *s, const css_pctx *cx, uint32_t r0) {
    for (uint32_t r = r0; r < s->rule_len; r++) {
        if (!s->rule[r].live || s->rule[r].keyframes) continue;
        s->rule[r].scoped = 1;
        s->rule[r].scope = cx->scope;
        s->rule[r].scope_end = cx->scope_end;
        s->rule[r].scope_host.index = cx->scope_host;
        vector<char> sel, out;
        append_range(sel, text_ptr(s, s->rule[r].selector), s->rule[r].selector.len);
        const char *t = data(sel);
        size_t n = len(sel);
        int amp = 0, scope = 0;
        for (size_t i = 0; i < n; i++) {
            if (t[i] == '"' || t[i] == '\'') { i = css_string_end(t, n, i); continue; }
            if (t[i] == '&') amp = 1;
            if (t[i] == ':' && i + 6 <= n && css_eq_ci(t + i + 1, 5, "scope") && (i + 6 == n || !css_namechar(t[i + 6]))) scope = 1;
        }
        if (amp) {
            for (size_t i = 0; i < n; i++) {
                if (t[i] == '"' || t[i] == '\'') {
                    size_t e = css_string_end(t, n, i);
                    size_t stop = e < n ? e + 1 : n;
                    append_range(out, t + i, stop - i);
                    i = stop - 1;
                    continue;
                }
                if (t[i] == '&') append_range(out, ":where(:scope)", 14);
                else push_back(out, t[i]);
            }
        } else if (scope) append_range(out, t, n);
        else {
            append_range(out, ":where(:scope) ", 15);
            append_range(out, t, n);
        }
        s->rule[r].selector = text_add(s, data(out), len(out));
        s->rule[r].spec = css_spec_complex(data(out), len(out), 0);
    }
}

/* an @keyframes rule (CSS Animations 1, 4): its name (an identifier other than none and
   the CSS-wide keywords, or a string) and its keyframes, each selector of a block (from,
   to, a percentage) a rule of its own with its place, in the sheet but matched by no
   element (css_index_rules leaves it out); a block whose selector this does not read is
   dropped, as the standard has it */
static void css_parse_block(css_style *s, uint32_t rule, const char *t, size_t n);
static void css_property_rule(css_style *s, const css_pctx *cx, const char *pre, size_t pn, const char *blk, size_t bn);
static void css_keyframes_rule(css_style *s, const css_pctx *cx, const char *pre, size_t pn, const char *blk, size_t bn) {
    size_t a = css_skip_ws(pre, pn, 0), b = pn;
    while (b > a && css_ws(pre[b - 1])) b--;
    if (b <= a) return;
    size_t na = a, nl = b - a;
    if (css_is_string(pre + a, b - a)) { na = a + 1; nl = b - a - 2; }
    else if (!css_is_ident(pre + a, nl) || css_eq_ci(pre + a, nl, "none") || css_eq_ci(pre + a, nl, "initial") ||
             css_eq_ci(pre + a, nl, "inherit") || css_eq_ci(pre + a, nl, "unset") || css_eq_ci(pre + a, nl, "revert") ||
             css_eq_ci(pre + a, nl, "revert-layer") || css_eq_ci(pre + a, nl, "default")) return;
    amb_span nsp = text_add(s, pre + na, nl);
    css_keyframes_h kh = keyframes_new(s);
    s->keyframes[kh.index].name = nsp;
    s->keyframes[kh.index].sheet = cx->sheet;
    s->keyframes[kh.index].layer = cx->layer;
    size_t i = 0;
    while (i < bn) {
        size_t open = i;
        while (open < bn && blk[open] != '{') open++;
        if (open >= bn) break;
        size_t end = css_block_end(blk, bn, open, '{', '}');
        size_t stop = end < bn ? end : bn;
        /* its selectors, each a place */
        double offs[64];
        size_t os[64], ol[64];
        int no = 0, ok = 1;
        size_t q = i;
        while (q <= open && ok) {
            size_t c = css_comma(blk, open, q);
            size_t sa = css_skip_ws(blk, c, q), sb = c;
            while (sb > sa && css_ws(blk[sb - 1])) sb--;
            double x = 0;
            size_t used = sb > sa ? css_number(blk + sa, sb - sa, &x) : 0;
            if (sb > sa && css_eq_ci(blk + sa, sb - sa, "from")) x = 0;
            else if (sb > sa && css_eq_ci(blk + sa, sb - sa, "to")) x = 1;
            else if (used && used + 1 == sb - sa && blk[sa + used] == '%' && x >= 0 && x <= 100) x /= 100;
            else ok = 0;
            if (ok && no < 64) { offs[no] = x; os[no] = sa; ol[no] = sb - sa; no++; }
            if (c >= open) break;
            q = c + 1;
        }
        for (int z = 0; ok && z < no; z++) {
            amb_span sp = text_add(s, blk + os[z], ol[z]);
            css_rule_h r = rule_new(s);
            s->rule[r.index].selector = sp;
            s->rule[r.index].order = r.index;
            s->rule[r.index].layer = cx->layer;
            s->rule[r.index].keyframes = kh.index;
            s->rule[r.index].offset = offs[z];
            rules_append(s, cx->sheet, r.index);
            css_parse_block(s, r.index, blk + open + 1, stop - open - 1);
        }
        i = stop + 1;
    }
}

/* a url() or a string's text, its quotes and white space gone, into out: 1 when it is one */
static int css_url_of(const char *s, size_t n, char *out, size_t cap) {
    size_t a = 0, e = n;
    if (css_is_fn(s, n, "url")) { a = 4; e = n - 1; }
    a = css_skip_ws(s, e, a);
    while (e > a && css_ws(s[e - 1])) e--;
    if (e > a && (s[a] == '"' || s[a] == '\'')) { if (s[e - 1] != s[a] || e - a < 2) return 0; a++; e--; }
    else if (!css_is_fn(s, n, "url")) return 0;
    if (e - a + 1 > cap) return 0;
    memcpy(out, s + a, e - a);
    out[e - a] = 0;
    return 1;
}

/* a URL a sheet names, made absolute against its base, and fetched -- unless a page not
   read from a file names a file (the loader's rule, net.c): the resource, or 0 */
static uint32_t css_fetch(net_loader *net, const css_pctx *cx, const char *ref, net_kind kind, char *abs, size_t cap) {
    if (!net || !cx->base[0] || !net_url_resolve(cx->base, ref, abs, cap)) return 0;
    if (strncmp(abs, "file:", 5) == 0 && strncmp(cx->base, "file:", 5) != 0) return 0;
    net_resource_h h = net_fetch(net, abs, strlen(abs), kind);
    return net->resource[h.index].outcome == NET_OUTCOME_OK ? h.index : 0;
}

/* a sheet's text, its comments stripped, read into a new sheet of the store */
static uint32_t css_sheet_read(css_style *s, net_loader *net, css_origin origin, const char *src, size_t n, const char *url,
                               uint32_t parent, uint32_t layer, int depth, const char *base, uint32_t host) {
    vector<char> t;
    css_strip_comments(src, n, &t);
    css_sheet_h sh = sheet_new(s);
    s->sheet[sh.index].origin = origin;
    s->sheet[sh.index].parent = parent;
    if (url) s->sheet[sh.index].url = text_add(s, url, strlen(url));
    css_pctx cx;
    memset(&cx, 0, sizeof cx);
    cx.sheet = sh.index;
    cx.layer = layer;
    cx.depth = depth;
    cx.origin = origin;
    cx.scope_host = host;
    snprintf(cx.base, sizeof cx.base, "%s", base ? base : "");
    css_rules(s, net, &cx, data(t), len(t));
    s->revision++;
    return sh.index;
}

/* @import url [layer | layer(name)] [supports(...)] [media]: the sheet fetched and read
   where the rule stands, its rules in the layer it names */
static void css_import(css_style *s, net_loader *net, css_pctx *cx, const char *p, size_t n) {
    size_t i = 0, st, ln;
    char ref[2048], abs[4096];
    if (!css_next_part(p, n, &i, &st, &ln) || !css_url_of(p + st, ln, ref, sizeof ref)) return;
    uint32_t layer = cx->layer;
    size_t rest = i;
    while (css_next_part(p, n, &i, &st, &ln)) {
        if (css_eq_ci(p + st, ln, "layer")) { layer = css_layer_anon(s, cx->layer); rest = i; continue; }
        if (css_is_fn(p + st, ln, "layer")) { layer = css_layer_path(s, cx->layer, p + st + 6, ln - 7); rest = i; continue; }
        if (css_is_fn(p + st, ln, "supports")) {
            if (!css_supports_test(p + st + 9, ln - 10, 0)) return;
            rest = i;
            continue;
        }
        break;
    }
    if (!css_media_matches(p + rest, n - rest) || cx->depth >= 8) return;
    uint32_t r = css_fetch(net, cx, ref, NET_KIND_STYLESHEET, abs, sizeof abs);
    if (!r) return;
    /* an import of a sheet already being read (a cycle) is not read again */
    for (uint32_t a = cx->sheet; a; a = s->sheet[a].parent) {
        amb_span u = s->sheet[a].url;
        if (u.len == strlen(abs) && memcmp(text_ptr(s, u), abs, u.len) == 0) return;
    }
    char base[4096];
    amb_span fu = net->resource[r].final_url;
    snprintf(base, sizeof base, "%.*s", (int)fu.len, text_ptr(net, fu));
    vector<char> body;
    append_range(body, bytes_ptr(net, net->resource[r].body), net->resource[r].body.len);
    css_sheet_read(s, net, cx->origin, data(body), len(body), abs, cx->sheet, layer, cx->depth + 1, base, cx->scope_host);
}

/* the bytes begin as a font the painter reads: TrueType, OpenType (with TrueType or CFF
   outlines), WOFF, WOFF2 */
static int css_font_magic(const char *b, size_t n) {
    if (n < 4) return 0;
    return (b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) || memcmp(b, "true", 4) == 0 || memcmp(b, "OTTO", 4) == 0 ||
           memcmp(b, "wOFF", 4) == 0 || memcmp(b, "wOF2", 4) == 0;
}

/* @font-face: its family, the sources whose format the painter reads, its weight and
   style; the first of those sources that is fetched and is a font */
static void css_font_face(css_style *s, net_loader *net, const css_pctx *cx, const char *t, size_t n) {
    vector<char> family;
    char refs[4][2048], abs[4096], first[4096];
    int nref = 0;
    uint32_t weight = 400;
    int italic = 0;
    size_t d = 0;
    while (d < n) {
        size_t e = d;
        while (e < n && t[e] != ';') {
            if (t[e] == '(') e = css_block_end(t, n, e, '(', ')');
            else if (t[e] == '"' || t[e] == '\'') e = css_string_end(t, n, e);
            if (e < n) e++;
        }
        size_t colon = d;
        while (colon < e && t[colon] != ':') colon++;
        if (colon < e) {
            size_t na = css_skip_ws(t, colon, d), nb = colon, va = css_skip_ws(t, e, colon + 1), vb = e;
            while (nb > na && css_ws(t[nb - 1])) nb--;
            while (vb > va && css_ws(t[vb - 1])) vb--;
            const char *v = t + va;
            size_t vn = vb - va;
            if (css_eq_ci(t + na, nb - na, "font-family")) {
                clear(family);
                if (vn >= 2 && (v[0] == '"' || v[0] == '\'')) append_range(family, v + 1, vn - 2);
                else append_range(family, v, vn);
            } else if (css_eq_ci(t + na, nb - na, "src") && !nref) {
                /* the sources in order: the url()s whose format() is none or one read (the
                   first four) */
                size_t a = 0;
                while (a < vn && nref < 4) {
                    size_t b = css_comma(v, vn, a), i = a, st, ln;
                    int ok = 1;
                    char u[2048];
                    u[0] = 0;
                    while (css_next_part(v, b, &i, &st, &ln)) {
                        if (css_is_fn(v + st, ln, "url")) { if (!css_url_of(v + st, ln, u, sizeof u)) ok = 0; }
                        else if (css_is_fn(v + st, ln, "format")) {
                            char f[64];
                            if (!css_url_of(v + st + 7, ln - 8, f, sizeof f)) snprintf(f, sizeof f, "%.*s", (int)(ln - 8), v + st + 7);
                            for (size_t q = 0; f[q]; q++) f[q] = css_lower(f[q]);
                            if (!css_word_in(f, "truetype opentype woff woff2")) ok = 0;
                        } else if (css_is_fn(v + st, ln, "tech")) ok = 0;
                    }
                    if (ok && u[0]) snprintf(refs[nref++], sizeof refs[0], "%s", u);
                    a = b + 1;
                }
            } else if (css_eq_ci(t + na, nb - na, "font-weight")) {
                double x;
                if (css_eq_ci(v, vn, "bold")) weight = 700;
                else if (css_number(v, vn, &x)) weight = (uint32_t)x;
            } else if (css_eq_ci(t + na, nb - na, "font-style")) italic = vn >= 6 && (css_eq_ci(v, 6, "italic") || css_eq_ci(v, 7, "oblique"));
        }
        d = e + 1;
    }
    if (!len(family) || !nref) return;
    css_face_h h = face_new(s);
    uint32_t fi = h.index;
    amb_span fam = text_add(s, data(family), len(family));
    s->face[fi].family = fam;
    s->face[fi].weight = weight;
    s->face[fi].italic = (uint8_t)italic;
    /* each source fetched in turn till one is a font (as Chromium tries the next when one
       fails to load): the face's URL that one's, else the first fetched, else the first
       as written; an if, as a ternary between two arrays is not lowered (toolchain
       finding) */
    uint32_t r = 0, fetched = 0;
    for (int k = 0; k < nref && !s->face[fi].font.index; k++) {
        r = css_fetch(net, cx, refs[k], NET_KIND_OTHER, abs, sizeof abs);
        if (r && !fetched) {
            fetched = r;
            snprintf(first, sizeof first, "%s", abs);
        }
        if (r && css_font_magic(bytes_ptr(net, net->resource[r].body), net->resource[r].body.len)) s->face[fi].font = resource_handle(net, r);
    }
    amb_span ua;
    if (s->face[fi].font.index) ua = text_add(s, abs, strlen(abs));
    else if (fetched) ua = text_add(s, first, strlen(first));
    else ua = text_add(s, refs[0], strlen(refs[0]));
    s->face[fi].url = ua;
}

/* a sheet's text, its comments gone: rules and at-rules */
static void css_rules(css_style *s, net_loader *net, css_pctx *cx, const char *t, size_t n) {
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
        size_t j = i + 1;
        while (at && j < k && css_namechar(t[j])) j++;
        const char *name = t + i + 1;
        size_t nn = j - i - 1;
        if (k >= n || t[k] == ';') {
            /* a statement: @import (before any other rule), @layer's names; @charset and
               @namespace read past */
            size_t stop = k < n ? k : n;
            if (at && css_eq_ci(name, nn, "import") && !cx->ruled) css_import(s, net, cx, t + j, stop - j);
            else if (at && css_eq_ci(name, nn, "layer")) {
                size_t a = j;
                while (a < stop) {
                    size_t b = css_comma(t, stop, a);
                    css_layer_path(s, cx->layer, t + a, b - a);
                    a = b + 1;
                }
            } else if (!(at && (css_eq_ci(name, nn, "charset") || css_eq_ci(name, nn, "namespace")))) cx->ruled = 1;
            if (k >= n) break;
            i = k + 1;
            continue;
        }
        size_t end = css_block_end(t, n, k, '{', '}');
        size_t inner = end < n ? end - k - 1 : n - k - 1;
        if (at && css_eq_ci(name, nn, "layer")) {
            /* @layer name { } (or anonymous): its rules in that layer */
            css_pctx sub = *cx;
            size_t a = css_skip_ws(t, k, j);
            sub.layer = a < k ? css_layer_path(s, cx->layer, t + a, k - a) : css_layer_anon(s, cx->layer);
            css_rules(s, net, &sub, t + k + 1, inner);
            cx->ruled = 1;
        } else if (at) {
            /* @media whose query holds, @supports whose condition does: their rules are the
               sheet's; @font-face's face is fetched; @keyframes' keyframes are rules of their own;
               @property registers its custom property; @scope's rules are scoped (css_scope_rules); @page
               and the rest: not read */
            if (css_eq_ci(name, nn, "media")) { if (css_media_matches(t + j, k - j)) css_rules(s, net, cx, t + k + 1, inner); }
            else if (css_eq_ci(name, nn, "supports")) { if (css_supports(t + j, k - j, 0)) css_rules(s, net, cx, t + k + 1, inner); }
            else if (css_eq_ci(name, nn, "font-face")) css_font_face(s, net, cx, t + k + 1, inner);
            else if (css_eq_ci(name, nn, "keyframes") || css_eq_ci(name, nn, "-webkit-keyframes")) css_keyframes_rule(s, cx, t + j, k - j, t + k + 1, inner);
            else if (css_eq_ci(name, nn, "property")) css_property_rule(s, cx, t + j, k - j, t + k + 1, inner);
            else if (css_eq_ci(name, nn, "scope")) {
                css_pctx sub = *cx;
                if (css_scope_prelude(s, t + j, k - j, &sub)) css_rules(s, net, &sub, t + k + 1, inner);
            }
            cx->ruled = 1;
        } else {
            uint32_t r0 = (uint32_t)s->rule_len;
            css_style_rule(s, cx->sheet, cx->layer, t + i, k - i, t + k + 1, inner);
            if (cx->scoped) css_scope_rules(s, cx, r0);
            cx->ruled = 1;
        }
        i = end + 1;
    }
}

css_status css_parse_sheet(css_style *s, css_origin origin, const char *src, size_t src_len) {
    css_sheet_read(s, 0, origin, src, src_len, 0, 0, 0, 0, 0, 0);
    return CSS_OK;
}

/* ---- matching (Selectors 4) ---- */

static int css_is_element(const dom_document *d, uint32_t x) { return x != 0 && d->node[x].kind == DOM_NODE_KIND_ELEMENT; }
/* the HTML element of that name (an SVG or MathML element of the name is none) */
static int css_is_named(const dom_document *d, uint32_t x, const char *name) {
    return css_is_element(d, x) && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, name, strlen(name));
}
static int css_is_svg_named(const dom_document *d, uint32_t x, const char *name) {
    return css_is_element(d, x) && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, name, strlen(name));
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

/* the page's state the pseudo-classes of state read: the element hovered, focused (and
   whether visibly), active, and the URL's target; 0 for none. scope: the scoping root a
   scoped rule is matched under (:scope), 0 for none (the root element then) */
typedef struct css_mstate { uint32_t hover, focus, focus_visible, active, target, scope; } css_mstate;

static int css_complex_matches(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget);

/* one of a selector list's complex selectors matches x */
static int css_list_matches(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget) {
    size_t a = 0;
    while (a < n) {
        size_t b = css_comma(s, n, a);
        if (css_complex_matches(d, x, ms, s + a, b - a, depth, budget)) return 1;
        a = b + 1;
    }
    return 0;
}

/* a script's selector list against the element x (querySelector, matches, closest:
   src/script/script.c), by the matcher the cascade uses: 1 when x matches */
int css_selector_matches(const dom_document *d, uint32_t x, const char *s, size_t n) {
    int budget = 100000;
    css_mstate none;
    memset(&none, 0, sizeof none);
    return css_list_matches(d, x, &none, s, n, 0, &budget) && budget >= 0;
}

static int css_form_control(const dom_document *d, uint32_t x) {
    return css_is_named(d, x, "button") || css_is_named(d, x, "input") || css_is_named(d, x, "select") ||
           css_is_named(d, x, "textarea") || css_is_named(d, x, "option") || css_is_named(d, x, "optgroup") || css_is_named(d, x, "fieldset");
}

/* a pseudo-class without arguments holds of x */
/* y is x or one of x's descendants */
static int css_within(const dom_document *d, uint32_t x, uint32_t y) {
    for (int k = 0; y && k < 4096; k++, y = d->node[y].parent) if (y == x) return 1;
    return 0;
}

static int css_pseudo(const dom_document *d, uint32_t x, const css_mstate *ms, const char *ps, size_t pn) {
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
        return (css_is_named(d, x, "a") || css_is_named(d, x, "area") || css_is_svg_named(d, x, "a")) && css_attr_of(d, x, "href", 4);
    if (css_eq_ci(ps, pn, "checked"))
        return (css_is_named(d, x, "input") && css_attr_of(d, x, "checked", 7)) || (css_is_named(d, x, "option") && css_attr_of(d, x, "selected", 8));
    if (css_eq_ci(ps, pn, "disabled")) return css_form_control(d, x) && css_attr_of(d, x, "disabled", 8);
    if (css_eq_ci(ps, pn, "enabled")) return css_form_control(d, x) && !css_attr_of(d, x, "disabled", 8);
    if (css_eq_ci(ps, pn, "scope")) return ms->scope ? x == ms->scope : d->node[x].parent == 1;
    /* the state the page is in: :hover and :active hold of the element and its ancestors,
       :focus-within of the focused one and its; :visited of none (no history is read). A
       page at rest: none of them holds */
    if (!ms->hover && !ms->active && !ms->focus && !ms->focus_visible && !ms->target) return 0;
    if (css_eq_ci(ps, pn, "hover")) return ms->hover && css_within(d, x, ms->hover);
    if (css_eq_ci(ps, pn, "active")) return ms->active && css_within(d, x, ms->active);
    if (css_eq_ci(ps, pn, "focus")) return ms->focus == x;
    if (css_eq_ci(ps, pn, "focus-visible")) return ms->focus_visible == x;
    if (css_eq_ci(ps, pn, "focus-within")) return ms->focus && css_within(d, x, ms->focus);
    if (css_eq_ci(ps, pn, "target")) return ms->target == x;
    return 0;   /* :visited and what else: no such state */
}

/* an attribute selector, [ to ] (e is the ]), holds of x */
static int css_attr_sel(const dom_document *d, uint32_t x, const char *s, size_t i, size_t e) {
    size_t a = css_skip_ws(s, e, i + 1), ae = a;
    while (ae < e && css_namechar(s[ae])) ae++;
    char an[64];
    size_t anl = ae - a;
    if (anl == 0 || anl >= sizeof an) return 0;
    for (size_t k = 0; k < anl; k++) an[k] = css_lower(s[a + k]);
    an[anl] = 0;
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
    /* the i flag; or, without the s flag, an attribute whose value HTML compares without case (HTML 4.2.6.3?) */
    int ci = after < e && css_lower(s[after]) == 'i';
    if (!ci && !(after < e && css_lower(s[after]) == 's') &&
        css_word_in(an, "accept accept-charset align alink axis bgcolor charset checked clear codetype color compact declare defer dir "
                        "direction disabled enctype face frame hreflang http-equiv lang language link media method multiple nohref "
                        "noresize noshade nowrap readonly rel rev rules scope scrolling selected shape target text type valign valuetype vlink")) ci = 1;
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
static int css_has(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget);

static int css_compound(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget) {
    size_t i = 0;
    if (depth > CSS_MAX_NESTING || --*budget < 0) return 0;
    if (n == 0) return 1;                       /* nothing left of it but its pseudo-element: universal */
    while (i < n) {
        char c = s[i];
        if (c == '*') { i++; continue; }
        if (css_namechar(c)) {
            char lw[64];
            size_t k = 0;
            while (i < n && css_namechar(s[i])) { if (k == sizeof lw) return 0; lw[k++] = css_lower(s[i]); i++; }
            /* in an HTML document a type selector names an element of any namespace, its
               case aside, as Chromium matches it: an SVG element keeps its mixed case */
            if (!names_is(d, d->node[x].name, lw, k) &&
                (d->node[x].ns == DOM_NAMESPACE_HTML || names_len(d, d->node[x].name) != k ||
                 !css_bytes_eq(names_ptr(d, d->node[x].name), lw, k, 1))) return 0;
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
                if (css_eq_ci(ps, pn, "not")) ok = !css_list_matches(d, x, ms, as, an, depth + 1, budget) && *budget >= 0;
                else if (css_eq_ci(ps, pn, "is") || css_eq_ci(ps, pn, "where") || css_eq_ci(ps, pn, "matches")) ok = css_list_matches(d, x, ms, as, an, depth + 1, budget);
                else if (css_eq_ci(ps, pn, "has")) ok = css_has(d, x, ms, as, an, depth + 1, budget);
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
            if (!css_pseudo(d, x, ms, ps, pn)) return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

/* the compounds parts[0..k) hold, parts[k-1] of x and the rest through their combinators */
static int css_parts_match(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, const css_part *parts, size_t k, int depth,
                           int *budget) {
    if (!css_compound(d, x, ms, s + parts[k - 1].start, parts[k - 1].len, depth, budget)) return 0;
    if (k == 1) return 1;
    char comb = parts[k - 1].comb;
    if (comb == '>') { uint32_t y = css_parent_element(d, x); return y && css_parts_match(d, y, ms, s, parts, k - 1, depth, budget); }
    if (comb == '+') { uint32_t y = css_prev_element(d, x); return y && css_parts_match(d, y, ms, s, parts, k - 1, depth, budget); }
    if (comb == '~') {
        for (uint32_t y = css_prev_element(d, x); y && *budget >= 0; y = css_prev_element(d, y))
            if (css_parts_match(d, y, ms, s, parts, k - 1, depth, budget)) return 1;
        return 0;
    }
    for (uint32_t y = css_parent_element(d, x); y && *budget >= 0; y = css_parent_element(d, y))
        if (css_parts_match(d, y, ms, s, parts, k - 1, depth, budget)) return 1;
    return 0;
}

static int css_complex_matches(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget) {
    css_part parts[CSS_MAXPARTS];
    size_t k = css_split(s, n, parts, CSS_MAXPARTS);
    return k > 0 && css_parts_match(d, x, ms, s, parts, k, depth, budget);
}

/* the relative selector parts[0..k) (after its leading combinator lead) holds of y,
   anchored at x: parts[k-1] of y, the rest through their combinators, and the first
   compound in the relation lead names with x */
static int css_rel_match(const dom_document *d, uint32_t y, uint32_t x, char lead, const css_mstate *ms, const char *s, const css_part *parts,
                         size_t k, int depth, int *budget) {
    if (!css_compound(d, y, ms, s + parts[k - 1].start, parts[k - 1].len, depth, budget)) return 0;
    if (k == 1) {
        if (lead == '>') return css_parent_element(d, y) == x;
        if (lead == '+') return css_prev_element(d, y) == x;
        if (lead == '~') { for (uint32_t z = css_prev_element(d, y); z; z = css_prev_element(d, z)) if (z == x) return 1; return 0; }
        for (uint32_t z = css_parent_element(d, y); z; z = css_parent_element(d, z)) if (z == x) return 1;
        return 0;
    }
    char comb = parts[k - 1].comb;
    if (comb == '>') { uint32_t z = css_parent_element(d, y); return z && css_rel_match(d, z, x, lead, ms, s, parts, k - 1, depth, budget); }
    if (comb == '+') { uint32_t z = css_prev_element(d, y); return z && css_rel_match(d, z, x, lead, ms, s, parts, k - 1, depth, budget); }
    if (comb == '~') {
        for (uint32_t z = css_prev_element(d, y); z && *budget >= 0; z = css_prev_element(d, z))
            if (css_rel_match(d, z, x, lead, ms, s, parts, k - 1, depth, budget)) return 1;
        return 0;
    }
    for (uint32_t z = css_parent_element(d, y); z && z != x && *budget >= 0; z = css_parent_element(d, z))
        if (css_rel_match(d, z, x, lead, ms, s, parts, k - 1, depth, budget)) return 1;
    return 0;
}

/* the next node after y in tree order inside top's subtree (top not counted), or 0 */
static uint32_t css_next_under(const dom_document *d, uint32_t y, uint32_t top) {
    if (d->node[y].first_child) return d->node[y].first_child;
    while (y != top && !d->node[y].next_sibling) y = d->node[y].parent;
    return y == top ? 0 : d->node[y].next_sibling;
}

/* :has(): one of its relative selectors holds of an element of its scope -- x's
   descendants (no combinator, or >), or x's later siblings and their descendants (+, ~).
   Its search has a budget of its own: past it, it holds of none */
static int css_has(const dom_document *d, uint32_t x, const css_mstate *ms, const char *s, size_t n, int depth, int *budget) {
    size_t a = 0;
    int hb = 20000;
    while (a < n) {
        size_t b = css_comma(s, n, a), i = css_skip_ws(s, b, a);
        char lead = ' ';
        if (i < b && (s[i] == '>' || s[i] == '+' || s[i] == '~')) { lead = s[i]; i++; }
        css_part parts[CSS_MAXPARTS];
        size_t k = css_split(s + i, b - i, parts, CSS_MAXPARTS);
        for (size_t q = 0; q < k; q++) parts[q].start += (uint32_t)i;
        if (k > 0 && (lead == ' ' || lead == '>')) {
            for (uint32_t y = d->node[x].first_child; y && hb > 0; y = css_next_under(d, y, x))
                if (d->node[y].kind == DOM_NODE_KIND_ELEMENT && hb-- > 0 && css_rel_match(d, y, x, lead, ms, s, parts, k, depth, budget)) return 1;
        } else if (k > 0) {
            for (uint32_t z = css_next_element(d, x); z && hb > 0; z = lead == '+' ? 0 : css_next_element(d, z)) {
                if (hb-- > 0 && css_rel_match(d, z, x, lead, ms, s, parts, k, depth, budget)) return 1;
                for (uint32_t y = d->node[z].first_child; y && hb > 0; y = css_next_under(d, y, z))
                    if (d->node[y].kind == DOM_NODE_KIND_ELEMENT && hb-- > 0 && css_rel_match(d, y, x, lead, ms, s, parts, k, depth, budget)) return 1;
            }
        }
        a = b + 1;
    }
    return 0;
}

/* ---- the rules' index (PLAN.md §11) ----

   Matching is the matcher above, unchanged; the index only spares it the pairs that
   cannot match. Each rule's selector is split once. Each rule sits in one bucket,
   keyed by its rightmost compound's id, else its first class, else its type, else
   universal, so an element meets the rules of its own buckets alone, merged back into
   the rules' order. A rule also carries the keys its ancestors must have -- those of
   each compound joined to the next by a descendant or child combinator -- as a 256-bit
   Bloom filter, and an element the filter of its ancestors' keys: a rule whose keys are
   not all in the element's filter is not tried. Buckets are told apart by kind, length
   and hash alone; two keys that collide share a bucket, which the matcher then sorts. */

enum { CSS_KEY_UNIVERSAL, CSS_KEY_TYPE, CSS_KEY_ID, CSS_KEY_CLASS, CSS_KEY_ATTR };
#define CSS_MAX_KEYS 32

typedef struct css_key { uint32_t kind, start, len; } css_key;
typedef struct css_bucket { uint32_t kind, len, hash, head, tail; } css_bucket;
typedef struct css_bent { uint32_t rule, next; } css_bent;                    /* a bucket's rule */
/* a rule as the index holds it: its parts (nparts: their count, and its pseudo-element above
   the low 8 bits), its ancestors' keys */
typedef struct css_rx { uint32_t parts, nparts; uint64_t anc[4]; } css_rx;

/* the pseudo-element a subject compound ends with (::before, ...; :before, :after,
   :first-line and :first-letter as CSS 2 wrote them): its css_pseudo, and *n made the
   compound's length without it; CSS_PSEUDO_ELEMENT when it has none */
static css_pseudo css_pseudo_of(const char *s, size_t *n) {
    if (!memchr(s, ':', *n)) return CSS_PSEUDO_ELEMENT;
    size_t j = 0;
    while (j < *n) {
        if (s[j] == '(') { j = css_block_end(s, *n, j, '(', ')'); if (j < *n) j++; continue; }
        if (s[j] == '[') { j = css_block_end(s, *n, j, '[', ']'); if (j < *n) j++; continue; }
        if (s[j] != ':') { j++; continue; }
        size_t c0 = j;
        int dbl = j + 1 < *n && s[j + 1] == ':';
        j += dbl ? 2 : 1;
        size_t b = j;
        while (j < *n && css_namechar(s[j])) j++;
        if (j != *n) continue;
        const char *w = s + b;
        size_t wn = j - b;
        css_pseudo k = CSS_PSEUDO_ELEMENT;
        if (css_eq_ci(w, wn, "before")) k = CSS_PSEUDO_BEFORE;
        else if (css_eq_ci(w, wn, "after")) k = CSS_PSEUDO_AFTER;
        else if (css_eq_ci(w, wn, "first-line")) k = CSS_PSEUDO_FIRST_LINE;
        else if (css_eq_ci(w, wn, "first-letter")) k = CSS_PSEUDO_FIRST_LETTER;
        else if (dbl && css_eq_ci(w, wn, "marker")) k = CSS_PSEUDO_MARKER;
        else if (dbl && css_eq_ci(w, wn, "placeholder")) k = CSS_PSEUDO_PLACEHOLDER;
        if (k != CSS_PSEUDO_ELEMENT) *n = c0;
        return k;
    }
    return CSS_PSEUDO_ELEMENT;
}

/* a key's hash: its kind, then its bytes (a type's and an attribute name's lowered) */
static uint32_t css_key_hash(uint32_t kind, const char *s, size_t n) {
    uint32_t h = 2166136261u ^ kind;
    h *= 16777619u;
    int lower = kind == CSS_KEY_TYPE || kind == CSS_KEY_ATTR;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)(lower ? css_lower(s[i]) : s[i]);
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

static void css_bloom_add(uint64_t *m, uint32_t h) {
    m[(h >> 6) & 3] |= 1ull << (h & 63);
    m[(h >> 14) & 3] |= 1ull << ((h >> 8) & 63);
}

/* a compound's own keys, outside its parentheses: its type (only first, as the matcher
   reads one), each id, each class, and each attribute selector's name */
static size_t css_compound_keys(const char *s, size_t n, css_key *k, size_t max) {
    size_t i = 0, c = 0;
    if (n > 0 && css_namechar(s[0])) {
        while (i < n && css_namechar(s[i])) i++;
        k[c].kind = CSS_KEY_TYPE;
        k[c].start = 0;
        k[c].len = (uint32_t)i;
        c++;
    }
    while (i < n) {
        char ch = s[i];
        if (ch == '#' || ch == '.') {
            size_t b = ++i;
            while (i < n && css_namechar(s[i])) i++;
            if (i > b && c < max) {
                k[c].kind = ch == '#' ? CSS_KEY_ID : CSS_KEY_CLASS;
                k[c].start = (uint32_t)b;
                k[c].len = (uint32_t)(i - b);
                c++;
            }
            continue;
        }
        if (ch == '[') {
            size_t e = css_block_end(s, n, i, '[', ']');
            size_t b = css_skip_ws(s, e, i + 1), ae = b;
            while (ae < e && css_namechar(s[ae])) ae++;
            if (ae > b && c < max) {
                k[c].kind = CSS_KEY_ATTR;
                k[c].start = (uint32_t)b;
                k[c].len = (uint32_t)(ae - b);
                c++;
            }
            i = e < n ? e + 1 : n;
            continue;
        }
        if (ch == '(') { i = css_block_end(s, n, i, '(', ')'); if (i < n) i++; continue; }
        i++;
    }
    return c;
}

/* the keys a rule is bucketed under, from its subject compound t[cs, cs+cn): the
   compound's own best key (an id, else a class, else an attribute, else its type); or,
   for a compound that is :is(...) or :where(...) alone, each alternative's (at depth 0
   only); or none, universal. Offsets are into t. */
static size_t css_bucket_keys(const char *t, size_t cs, size_t cn, css_key *out, size_t max, int depth) {
    css_key keys[CSS_MAX_KEYS];
    size_t nk = css_compound_keys(t + cs, cn, keys, CSS_MAX_KEYS);
    for (int pi = 0; pi < 4; pi++) {
        uint32_t pick = pi == 0 ? CSS_KEY_ID : pi == 1 ? CSS_KEY_CLASS : pi == 2 ? CSS_KEY_ATTR : CSS_KEY_TYPE;
        for (size_t q = 0; q < nk; q++)
            if (keys[q].kind == pick) {
                if (max == 0) return 0;
                out[0].kind = pick;
                out[0].start = (uint32_t)(cs + keys[q].start);
                out[0].len = keys[q].len;
                return 1;
            }
    }
    if (depth > 0 || nk > 0) return 0;
    size_t i = cs, e = cs + cn;
    if (i < e && t[i] == '*') i++;
    if (i + 1 >= e || t[i] != ':') return 0;
    size_t nb = ++i;
    while (i < e && css_namechar(t[i])) i++;
    if (!(css_eq_ci(t + nb, i - nb, "is") || css_eq_ci(t + nb, i - nb, "where")) || i >= e || t[i] != '(') return 0;
    size_t close = css_block_end(t, e, i, '(', ')');
    if (close + 1 != e) return 0;                       /* :is(...) and nothing after it */
    size_t n = 0, alt = i + 1;
    while (alt < close) {
        size_t end = css_comma(t, close, alt);
        css_part ps[CSS_MAXPARTS];
        size_t k = css_split(t + alt, end - alt, ps, CSS_MAXPARTS);
        if (k == 0 || n == max) return 0;
        size_t got = css_bucket_keys(t, alt + ps[k - 1].start, ps[k - 1].len, out + n, max - n, depth + 1);
        if (got == 0) return 0;
        n += got;
        alt = end + 1;
    }
    return n;
}

/* the bucket of a key: its number from 1, or 0 */
static uint32_t css_bucket_find(const vector<css_bucket> *bk, const vector<uint32_t> *slots, uint32_t kind, uint32_t kl, uint32_t h) {
    size_t mask = len(*slots) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t b = at(*slots, i);
        if (!b) return 0;
        css_bucket e = at(*bk, b - 1);
        if (e.hash == h && e.kind == kind && e.len == kl) return b;
    }
}

/* the rule r put in the bucket of a key, made if need be (once, if a rule names it twice) */
static void css_bucket_put(vector<css_bucket> *bk, vector<uint32_t> *slots, vector<css_bent> *be, uint32_t kind, uint32_t kl, uint32_t h,
                           uint32_t r) {
    size_t mask = len(*slots) - 1;
    uint32_t b = 0;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t x = at(*slots, i);
        if (!x) {
            css_bucket e = { kind, kl, h, 0, 0 };
            push_back(bk, e);
            (*slots)[i] = (uint32_t)len(*bk);
            b = (uint32_t)len(*bk);
            break;
        }
        css_bucket e = at(*bk, x - 1);
        if (e.hash == h && e.kind == kind && e.len == kl) { b = x; break; }
    }
    css_bucket e = at(*bk, b - 1);
    if (e.tail && at(*be, e.tail).rule == r) return;
    css_bent en = { r, 0 };
    push_back(be, en);
    uint32_t ei = (uint32_t)(len(*be) - 1);
    if (e.tail) (*be)[e.tail].next = ei;
    else e.head = ei;
    e.tail = ei;
    (*bk)[b - 1] = e;
}

/* the index of the rules 1 .. n-1: each split once, bucketed, with its ancestors' keys */
static void css_index_rules(const css_style *s, uint32_t n, vector<css_part> *parts, vector<css_rx> *rxs, vector<css_bucket> *bk,
                            vector<uint32_t> *slots, vector<css_bent> *be) {
    size_t cap = 16;
    while (cap < 8 * (size_t)n + 8) cap *= 2;
    resize(slots, cap);
    resize(rxs, (size_t)n);
    css_bent none = { 0, 0 };
    push_back(be, none);                                /* entry 0: the end of a bucket */
    for (uint32_t r = 1; r < n; r++) {
        if (s->rule[r].keyframes) continue;                /* a keyframe: matched by no element */
        amb_span sel = s->rule[r].selector;
        const char *t = text_ptr(s, sel);
        css_part ps[CSS_MAXPARTS];
        size_t k = css_split(t, sel.len, ps, CSS_MAXPARTS);
        css_rx rx;
        memset(&rx, 0, sizeof rx);
        uint32_t pseudo = 0;
        if (k > 0) {
            /* a pseudo-element: the rule matches its element, the pseudo-element cut from its subject */
            size_t ln = ps[k - 1].len;
            pseudo = (uint32_t)css_pseudo_of(t + ps[k - 1].start, &ln);
            ps[k - 1].len = (uint32_t)ln;
        }
        rx.parts = (uint32_t)len(*parts);
        rx.nparts = (uint32_t)k | pseudo << 8;
        for (size_t j = 0; j < k; j++) push_back(parts, ps[j]);
        if (k == 0) { (*rxs)[r] = rx; continue; }        /* not a selector this reads: it matches nothing */
        css_key keys[CSS_MAX_KEYS];
        /* the ancestors' keys: each compound followed by a descendant or child combinator */
        for (size_t j = 0; j + 1 < k; j++) {
            if (ps[j + 1].comb != ' ' && ps[j + 1].comb != '>') continue;
            size_t nk = css_compound_keys(t + ps[j].start, ps[j].len, keys, CSS_MAX_KEYS);
            for (size_t q = 0; q < nk; q++) css_bloom_add(rx.anc, css_key_hash(keys[q].kind, t + ps[j].start + keys[q].start, keys[q].len));
        }
        (*rxs)[r] = rx;
        size_t nb = css_bucket_keys(t, ps[k - 1].start, ps[k - 1].len, keys, CSS_MAX_KEYS, 0);
        if (nb == 0) { css_bucket_put(bk, slots, be, CSS_KEY_UNIVERSAL, 0, css_key_hash(CSS_KEY_UNIVERSAL, "", 0), r); continue; }
        for (size_t q = 0; q < nb; q++)
            css_bucket_put(bk, slots, be, keys[q].kind, keys[q].len, css_key_hash(keys[q].kind, t + keys[q].start, keys[q].len), r);
    }
}

/* the keys of the element x: its type, its id, its classes, its attributes' names (at
   most max; past that, -1) */
static int css_element_keys(const dom_document *d, uint32_t x, uint32_t id_atom, uint32_t class_atom, uint32_t *kind, uint32_t *kl,
                            uint32_t *h, int max) {
    int c = 0;
    size_t nl = names_len(d, d->node[x].name);
    kind[c] = CSS_KEY_TYPE;
    kl[c] = (uint32_t)nl;
    h[c] = css_key_hash(CSS_KEY_TYPE, names_ptr(d, d->node[x].name), nl);
    c++;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
        uint32_t an = d->attr[at].name;
        if (an == 0) continue;
        size_t anl = names_len(d, an);
        if (c == max) return -1;
        kind[c] = CSS_KEY_ATTR;
        kl[c] = (uint32_t)anl;
        h[c] = css_key_hash(CSS_KEY_ATTR, names_ptr(d, an), anl);
        c++;
        if (an != id_atom && an != class_atom) continue;
        amb_span v = d->attr[at].value;
        const char *vp = text_ptr(d, v);
        if (an == id_atom) {
            if (c == max) return -1;
            kind[c] = CSS_KEY_ID;
            kl[c] = (uint32_t)v.len;
            h[c] = css_key_hash(CSS_KEY_ID, vp, v.len);
            c++;
            continue;
        }
        size_t i = 0;
        while (i < v.len) {
            while (i < v.len && css_ws(vp[i])) i++;
            size_t b = i;
            while (i < v.len && !css_ws(vp[i])) i++;
            if (i == b) continue;
            if (c == max) return -1;
            kind[c] = CSS_KEY_CLASS;
            kl[c] = (uint32_t)(i - b);
            h[c] = css_key_hash(CSS_KEY_CLASS, vp + b, i - b);
            c++;
        }
    }
    return c;
}

/* the rule r matches x (its ancestors' keys in x's filter am, then the matcher): 1, or 1
   and its pseudo-element (shifted 8 up); 0 when it does not */
static uint32_t css_rule_matches(const css_style *s, const dom_document *d, uint32_t x, const css_mstate *ms, uint32_t r,
                            const vector<css_part> *parts, const vector<css_rx> *rxs, const uint64_t *am, uint32_t *prox) {
    css_rx rx = at(*rxs, r);
    uint32_t np = rx.nparts & 0xff;
    *prox = 0;
    if (np == 0) return 0;
    if ((rx.anc[0] & ~am[0]) | (rx.anc[1] & ~am[1]) | (rx.anc[2] & ~am[2]) | (rx.anc[3] & ~am[3])) return 0;
    css_part ps[CSS_MAXPARTS];
    for (uint32_t j = 0; j < np; j++) ps[j] = at(*parts, rx.parts + j);
    int budget = CSS_MATCH_BUDGET;
    amb_span sel = s->rule[r].selector;
    if (!s->rule[r].scoped) return css_parts_match(d, x, ms, text_ptr(s, sel), ps, np, 0, &budget) ? 1 | (rx.nparts & ~0xffu) : 0;
    /* a scoped rule (Cascade 6, 2.5): under the nearest scoping root it matches below -- x
       or an ancestor that matches the roots' selector (or is the @scope's host, the root
       element when it has none), with no limit at or above x below it -- its proximity
       the generations between them, plus one */
    amb_span st = s->rule[r].scope, et = s->rule[r].scope_end;
    uint32_t host = s->rule[r].scope_host.index, gen = 0;
    for (uint32_t root = x; root && root != 1 && css_is_element(d, root); root = d->node[root].parent, gen++) {
        int is_root = st.len ? css_list_matches(d, root, ms, text_ptr(s, st), st.len, 0, &budget)
                             : host ? root == host : d->node[root].parent == 1;
        if (!is_root) continue;
        css_mstate m2 = *ms;
        m2.scope = root;
        int limited = 0;
        for (uint32_t l = x; et.len && l && l != root && !limited; l = d->node[l].parent)
            if (css_list_matches(d, l, &m2, text_ptr(s, et), et.len, 0, &budget)) limited = 1;
        if (limited) continue;
        if (css_parts_match(d, x, &m2, text_ptr(s, sel), ps, np, 0, &budget)) {
            *prox = gen + 1 < 255 ? gen + 1 : 255;
            return 1 | (rx.nparts & ~0xffu);
        }
    }
    return 0;
}

/* ---- the cascade ---- */

/* the computed value, as CSS serializes it (a value kept as text is its text, not this) */
static size_t css_value_text(const css_val *v, char *buf, size_t cap) {
    int n;
    if (v->unit == CSS_UNIT_PX) n = snprintf(buf, cap, "%gpx", v->num);
    else if (v->unit == CSS_UNIT_PERCENT) n = snprintf(buf, cap, "%g%%", v->num);
    else if (v->unit == CSS_UNIT_NUMBER) n = snprintf(buf, cap, "%g", v->num);
    else if (v->unit == CSS_UNIT_EM) n = snprintf(buf, cap, "%gem", v->num);
    else if (v->unit == CSS_UNIT_REM) n = snprintf(buf, cap, "%grem", v->num);
    else if (v->unit == CSS_UNIT_AUTO) n = snprintf(buf, cap, "auto");
    else if (v->unit == CSS_UNIT_CALC) n = snprintf(buf, cap, "calc(%g%% %c %gpx)", v->pct, v->num < 0 ? '-' : '+', v->num < 0 ? -v->num : v->num);
    else if (v->unit == CSS_UNIT_COLOR) {
        uint32_t c = v->color;
        if ((c & 0xff) == 0xff) n = snprintf(buf, cap, "rgb(%u, %u, %u)", c >> 24, (c >> 16) & 0xff, (c >> 8) & 0xff);
        else {
            /* alpha as the fewest decimals that come back to its byte (CSS Color 4, 15) */
            uint32_t a = c & 0xff;
            double x = a / 255.0;
            int d = 1;
            for (; d < 6; d++) {
                double k = d == 1 ? 10 : d == 2 ? 100 : d == 3 ? 1000 : d == 4 ? 10000 : 100000;
                double r = (double)(long)(x * k + 0.5) / k;
                if ((uint32_t)(r * 255 + 0.5) == a) { x = r; break; }
            }
            n = snprintf(buf, cap, "rgba(%u, %u, %u, %g)", c >> 24, (c >> 16) & 0xff, (c >> 8) & 0xff, x);
        }
    } else n = snprintf(buf, cap, "%s", v->word);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* font-size's absolute keywords against the default font size, as Chromium has them
   (read from it under each default, in standards mode and quirks mode alike): a default
   of 9 to 16 px through its table, another by CSS Fonts 4's scaling factors, at least
   its minimum logical font size (6 px) */
static const int css_size_table[8][8] = {
    { 9, 9, 9, 9, 11, 14, 18, 27 }, { 9, 9, 9, 10, 12, 15, 20, 30 }, { 9, 9, 10, 11, 13, 17, 22, 33 },
    { 9, 9, 10, 12, 14, 18, 24, 36 }, { 9, 10, 12, 13, 16, 20, 26, 39 }, { 9, 10, 12, 14, 17, 21, 28, 42 },
    { 9, 10, 13, 15, 18, 23, 30, 45 }, { 9, 10, 13, 16, 18, 24, 32, 48 } };
static double css_size_at(int k, double medium) {
    static const double factor[8] = { 0.6, 0.75, 0.89, 1.0, 1.2, 1.5, 2.0, 3.0 };
    int row = (int)(medium + 0.5);
    if (medium == (double)row && row >= 9 && row <= 16) return css_size_table[row - 9][k];
    double x = medium * factor[k];
    return x < 6 ? 6 : x;
}
/* Chromium's default fixed font size: the medium of a family that is monospace alone */
#define CSS_FIXED_PX 13.0
/* the absolute keyword w's column (0 xx-small to 7 xxx-large), or -1 */
static int css_size_column(const char *w) {
    static const char *const kws[8] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" };
    for (int k = 0; k < 8; k++) if (strcmp(w, kws[k]) == 0) return k;
    return -1;
}
/* font-size's keywords (CSS Fonts 4, 2.5): the absolute ones against the default font
   size, or the fixed one under a family that is monospace alone (as Chromium's
   FontSizeForKeyword has them); smaller and larger against the parent's size */
static double css_size_keyword(const char *w, double parent, int mono) {
    if (strcmp(w, "smaller") == 0) return parent / 1.2;
    if (strcmp(w, "larger") == 0) return parent * 1.2;
    int k = css_size_column(w);
    return css_size_at(k < 0 ? 3 : k, mono ? CSS_FIXED_PX : css_font_px);
}

/* the text t with every font- and viewport-relative length made px, currentcolor made the
   colour cur, and the system colours and light-dark() made colours in the dark scheme or
   the light, into out (strings and url()s as they are): what a value kept as text
   computes to. 1 when it changed anything */
static int css_absolutize(const char *t, size_t n, double em, double rem, uint32_t cur, int dark, vector<char> *out) {
    int changed = 0;
    size_t i = 0;
    while (i < n) {
        char c = t[i];
        if (c == '"' || c == '\'') {
            size_t e = css_string_end(t, n, i);
            size_t stop = e < n ? e + 1 : n;
            append_range(out, t + i, stop - i);
            i = stop;
            continue;
        }
        int word_start = i == 0 || !css_namechar(t[i - 1]);
        if (word_start && i + 4 <= n && css_bytes_eq(t + i, "url(", 4, 1)) {
            size_t e = css_block_end(t, n, i + 3, '(', ')');
            size_t stop = e < n ? e + 1 : n;
            append_range(out, t + i, stop - i);
            i = stop;
            continue;
        }
        if (word_start && css_is_alpha(c)) {
            size_t e = i;
            while (e < n && css_namechar(t[e])) e++;
            uint32_t sc = cur;
            int is_cur = css_eq_ci(t + i, e - i, "currentcolor"), is_sys = !is_cur && css_system_color(t + i, e - i, dark, &sc);
            if (!is_cur && !is_sys && e < n && t[e] == '(' && css_eq_ci(t + i, e - i, "light-dark")) {
                size_t close = css_block_end(t, n, e, '(', ')');
                if (close < n && css_scheme_color(t + i, close + 1 - i, dark, &sc)) { is_sys = 1; e = close + 1; }
            }
            if (is_cur || is_sys) {
                char buf[48];
                css_val cv;
                memset(&cv, 0, sizeof cv);
                cv.unit = CSS_UNIT_COLOR;
                cv.color = sc;
                size_t bn = css_value_text(&cv, buf, sizeof buf);
                append_range(out, buf, bn);
                changed = 1;
            } else append_range(out, t + i, e - i);
            i = e;
            continue;
        }
        double x;
        size_t used = word_start && (css_is_digit(c) || c == '.' || c == '+' || c == '-') ? css_number(t + i, n - i, &x) : 0;
        if (used) {
            size_t ub = i + used, ue = ub;
            while (ue < n && css_is_alpha(t[ue])) ue++;
            double k;
            int fu = css_font_unit(t + ub, ue - ub, &k);
            double ab = fu ? 0 : css_abs_unit(t + ub, ue - ub);
            if (fu || (ab > 0 && !css_eq_ci(t + ub, ue - ub, "px"))) {
                double px = fu == 2 ? x * rem : fu == 1 ? x * k * em : x * ab;
                char buf[48];
                int bn = snprintf(buf, sizeof buf, "%gpx", px);
                if (bn > 0) append_range(out, buf, (size_t)bn);
                changed = 1;
            } else append_range(out, t + i, ue - i);
            i = ue;
            continue;
        }
        push_back(out, c);
        i++;
    }
    return changed;
}

/* a filter's drop-shadow()s without a colour given currentcolor, which css_text_computed
   makes the element's colour, as Chromium computes them */
static void css_filter_colours(css_style *s, css_val *v) {
    vector<char> src, out;
    append_range(src, text_ptr(s, v->tx), v->tx.len);
    const char *t = data(src);
    size_t n = len(src), i = 0, st, ln;
    int changed = 0;
    while (css_next_part(t, n, &i, &st, &ln)) {
        if (len(out)) push_back(out, ' ');
        const char *p = t + st;
        if (css_is_fn(p, ln, "drop-shadow")) {
            const char *a = p + 12;
            size_t an = ln - 13, j = 0, xs, xl;
            int coloured = 0;
            while (css_next_part(a, an, &j, &xs, &xl)) if (css_is_color(a + xs, xl)) coloured = 1;
            if (!coloured) {
                append_range(out, "drop-shadow(currentcolor ", 25);
                append_range(out, a, an);
                push_back(out, ')');
                changed = 1;
                continue;
            }
        }
        append_range(out, p, ln);
    }
    if (changed) v->tx = text_add(s, data(out), len(out));
}

/* an animation's or a transition's times and easings as Chromium computes them: each
   time in seconds; linear() with each stop's place given; steps() without its default
   position (end, jump-end) */
static void css_anim_computed(css_style *s, css_val *v) {
    vector<char> src, out;
    append_range(src, text_ptr(s, v->tx), v->tx.len);
    const char *t = data(src);
    size_t n = len(src), a = 0;
    while (a <= n) {
        size_t b = css_comma(t, n, a), sa = css_skip_ws(t, b, a), sb = b;
        while (sb > sa && css_ws(t[sb - 1])) sb--;
        if (len(out)) append_range(out, ", ", 2);
        const char *x = t + sa;
        size_t xl = sb - sa, open = 0;
        while (open < xl && x[open] != '(') open++;
        double ms;
        char buf[64];
        if (css_time_ms(x, xl, &ms)) {
            int bn = snprintf(buf, sizeof buf, "%gs", ms / 1000);
            if (bn > 0) append_range(out, buf, (size_t)bn);
        } else if (open < xl && css_eq_ci(x, open, "steps")) {
            /* its count, and its position unless it is the default */
            const char *ar = x + open + 1;
            size_t an = xl >= open + 2 ? xl - open - 2 : 0, j = 0, ps, pl;
            append_range(out, "steps(", 6);
            if (css_next_part(ar, an, &j, &ps, &pl)) append_range(out, ar + ps, pl);
            if (css_next_part(ar, an, &j, &ps, &pl) && !css_eq_ci(ar + ps, pl, "end") && !css_eq_ci(ar + ps, pl, "jump-end")) {
                append_range(out, ", ", 2);
                append_range(out, ar + ps, pl);
            }
            push_back(out, ')');
        } else if (open < xl && css_eq_ci(x, open, "linear")) {
            /* its stops, each output at its input (css_ease_at's places) */
            const char *ar = x + open + 1;
            size_t an = xl >= open + 2 ? xl - open - 2 : 0, c0 = 0;
            double o[64], in[64];
            int given[64], m = 0;
            while (c0 <= an && m < 62) {
                size_t e = css_comma(ar, an, c0), k = 0, ps, pl;
                int nums = 0, pcts = 0;
                double ov = 0, p1 = 0, p2 = 0;
                while (css_next_part(ar + c0, e - c0, &k, &ps, &pl)) {
                    double val;
                    css_number(ar + c0 + ps, pl, &val);
                    if (!nums) { ov = val; nums = 1; }
                    else if (!pcts) { p1 = val; pcts = 1; }
                    else { p2 = val; pcts = 2; }
                }
                o[m] = ov; in[m] = p1; given[m] = pcts > 0; m++;
                if (pcts == 2) { o[m] = ov; in[m] = p2; given[m] = 1; m++; }
                if (e >= an) break;
                c0 = e + 1;
            }
            if (m >= 2) {
                if (!given[0]) { in[0] = 0; given[0] = 1; }
                if (!given[m - 1]) { in[m - 1] = 100; given[m - 1] = 1; }
                double hi = in[0];
                for (int k = 1; k < m; k++) if (given[k]) { if (in[k] < hi) in[k] = hi; hi = in[k]; }
                for (int k = 1; k < m; k++) {
                    if (given[k]) continue;
                    int e = k;
                    while (!given[e]) e++;
                    for (int q = k; q < e; q++) in[q] = in[k - 1] + (in[e] - in[k - 1]) * (q - k + 1) / (e - k + 1);
                    k = e;
                }
                append_range(out, "linear(", 7);
                for (int k = 0; k < m; k++) {
                    int bn = snprintf(buf, sizeof buf, "%s%g %g%%", k ? ", " : "", o[k], in[k]);
                    if (bn > 0) append_range(out, buf, (size_t)bn);
                }
                push_back(out, ')');
            } else append_range(out, x, xl);
        } else append_range(out, x, xl);
        if (b >= n) break;
        a = b + 1;
    }
    if (len(out) != len(src) || memcmp(data(out), data(src), len(src)) != 0) v->tx = text_add(s, data(out), len(out));
}

/* a value kept as text made computed: its span, or a new one when it changed */
static void css_text_computed(css_style *s, css_val *v, double em, double rem, uint32_t cur, int dark) {
    vector<char> src, out;
    append_range(src, text_ptr(s, v->tx), v->tx.len);
    if (css_absolutize(data(src), len(src), em, rem, cur, dark, &out)) v->tx = text_add(s, data(out), len(out));
}

/* a calc() made computed: px, a percentage, px and a percentage, a number, or (over
   percentages, not such a sum) its text with its lengths made px */
static int css_calc_computed(css_style *s, css_prop p, css_val *v, double em, double rem, double basis, uint32_t cur) {
    vector<char> src;
    append_range(src, text_ptr(s, v->tx), v->tx.len);
    css_cenv e = { em, rem, basis };
    css_lin l;
    size_t i = 0;
    if (!css_calc_value(data(src), len(src), &i, &e, 0, &l)) return 0;
    if (l.kind == CSS_CK_NUM && !l.nonlin) { v->unit = CSS_UNIT_NUMBER; v->num = l.num; if (p != CSS_PROP_LINE_HEIGHT && css_pdefs[p].gram == G_LEN) v->unit = CSS_UNIT_PX; return 1; }
    if (l.nonlin) {
        vector<char> out;
        css_absolutize(data(src), len(src), em, rem, cur, 0, &out);
        v->unit = CSS_UNIT_EXPR;
        v->tx = text_add(s, data(out), len(out));
        return 1;
    }
    if (!(l.kind & CSS_CK_PCT) || l.pct == 0) { v->unit = CSS_UNIT_PX; v->num = l.px; return 1; }
    if (l.px == 0) { v->unit = CSS_UNIT_PERCENT; v->num = l.pct; return 1; }
    v->unit = CSS_UNIT_CALC;
    v->num = l.px;
    v->pct = l.pct;
    return 1;
}

/* the web fonts' families the page declared (src/css: @font-face), and the font sizes the
   cascade is at: the root's, for rem */
typedef struct css_env { double rem; int page_dark; char faces[1024]; } css_env;

/* a declared value made computed: pv the parent's computed values, cur the element's
   computed so far (its font size and colour come first) */
/* what a value made computed needs of the parent's: its font size, weight and colour */
typedef struct css_par { double fs, weight; uint32_t color; } css_par;

static void css_compute(css_style *s, const css_env *env, css_prop p, css_val *v, const css_par *pv, const css_val *cur) {
    double pfs = pv->fs, fs = cur[CSS_PROP_FONT_SIZE].num;
    uint32_t color = cur[CSS_PROP_COLOR].color;
    int g = css_pdefs[p].gram;
    if (p == CSS_PROP_FONT_SIZE) {
        if (v->unit == CSS_UNIT_EM) v->num *= pfs;
        else if (v->unit == CSS_UNIT_REM) v->num *= env->rem;
        else if (v->unit == CSS_UNIT_PERCENT) v->num = pfs * v->num / 100;
        else if (v->unit == CSS_UNIT_KEYWORD) v->num = css_size_keyword(v->word, pfs, css_is_word(&cur[CSS_PROP_FONT_FAMILY], "monospace"));
        else if (v->unit == CSS_UNIT_CALC || v->unit == CSS_UNIT_EXPR) {
            css_val c = *v;
            if (css_calc_computed(s, p, &c, pfs, env->rem, pfs, color) && c.unit == CSS_UNIT_PX) v->num = c.num;
            else v->num = pfs;
        }
        v->unit = CSS_UNIT_PX;
        memset(v->word, 0, sizeof v->word);
        return;
    }
    if (p == CSS_PROP_FONT_WEIGHT) {
        if (v->unit == CSS_UNIT_CALC) { css_calc_computed(s, p, v, fs, env->rem, -1, color); if (v->unit != CSS_UNIT_NUMBER) v->num = 400; v->unit = CSS_UNIT_NUMBER; return; }
        if (v->unit != CSS_UNIT_KEYWORD) return;
        double w = pv->weight, r = 400;
        if (strcmp(v->word, "bold") == 0) r = 700;
        else if (strcmp(v->word, "bolder") == 0) r = w < 350 ? 400 : w < 550 ? 700 : w < 900 ? 900 : w;
        else if (strcmp(v->word, "lighter") == 0) r = w < 100 ? w : w < 550 ? 100 : w < 750 ? 400 : 700;
        memset(v, 0, sizeof *v);
        v->unit = CSS_UNIT_NUMBER;
        v->num = r;
        return;
    }
    if (g == G_COLOR) {
        if (css_is_word(v, "currentcolor")) {
            /* color's currentcolor is its parent's colour; another property's is the element's.
               An inherited property keeps the word (CSS Color 4: currentcolor is its own
               computed value), so that a child takes its own colour */
            memset(v, 0, sizeof *v);
            v->unit = CSS_UNIT_COLOR;
            v->color = p == CSS_PROP_COLOR ? pv->color : color;
            if (p != CSS_PROP_COLOR && css_pdefs[p].inh) memcpy(v->word, "currentcolor", 13);
        } else if (v->unit == CSS_UNIT_KEYWORD || v->unit == CSS_UNIT_TEXT) {
            /* a system colour or light-dark(), in the scheme the element is used in */
            int dark = cur[CSS_PROP_COLOR_SCHEME].color == CSS_CANVAS_DARK;
            uint32_t c;
            int ok = v->unit == CSS_UNIT_KEYWORD ? css_system_color(v->word, strlen(v->word), dark, &c)
                                                  : css_scheme_color(text_ptr(s, v->tx), v->tx.len, dark, &c);
            if (ok) { memset(v, 0, sizeof *v); v->unit = CSS_UNIT_COLOR; v->color = c; }
        }
        return;
    }
    if (p == CSS_PROP_COLOR_SCHEME) {
        /* as specified; its colour the Canvas of the scheme it is used in */
        int dark = v->unit == CSS_UNIT_TEXT ? css_scheme_of(text_ptr(s, v->tx), v->tx.len, env->page_dark) : env->page_dark;
        v->color = dark ? CSS_CANVAS_DARK : CSS_CANVAS_LIGHT;
        return;
    }
    if (g == G_FAMILY) {
        if (v->unit != CSS_UNIT_TEXT) return;
        vector<char> t;
        append_range(t, text_ptr(s, v->tx), v->tx.len);
        if (!css_family_of(data(t), len(t), env->faces, v)) css_word_val(v, "serif");
        return;
    }
    if ((p == CSS_PROP_COUNTER_RESET || p == CSS_PROP_COUNTER_INCREMENT) && v->unit == CSS_UNIT_TEXT) {
        /* each counter with its value: a name alone is reset to 0, incremented by 1 */
        vector<char> src, out;
        append_range(src, text_ptr(s, v->tx), v->tx.len);
        size_t i = 0, st, ln, pend = 0;
        int k;
        while (css_next_part(data(src), len(src), &i, &st, &ln)) {
            if (css_is_int(data(src) + st, ln, &k)) { pend = 0; push_back(out, ' '); append_range(out, data(src) + st, ln); continue; }
            if (pend) append_range(out, p == CSS_PROP_COUNTER_RESET ? " 0" : " 1", 2);
            if (len(out)) push_back(out, ' ');
            append_range(out, data(src) + st, ln);
            pend = 1;
        }
        if (pend) append_range(out, p == CSS_PROP_COUNTER_RESET ? " 0" : " 1", 2);
        v->tx = text_add(s, data(out), len(out));
        return;
    }
    if (g == G_TEXT || g == G_RADIUS) {
        if (v->unit == CSS_UNIT_TEXT) {
            if (p == CSS_PROP_FILTER || p == CSS_PROP_BACKDROP_FILTER) css_filter_colours(s, v);
            css_text_computed(s, v, fs, env->rem, color, cur[CSS_PROP_COLOR_SCHEME].color == CSS_CANVAS_DARK);
            if (p == CSS_PROP_ANIMATION_DURATION || p == CSS_PROP_ANIMATION_DELAY || p == CSS_PROP_TRANSITION_DURATION ||
                p == CSS_PROP_TRANSITION_DELAY || p == CSS_PROP_ANIMATION_TIMING_FUNCTION || p == CSS_PROP_TRANSITION_TIMING_FUNCTION)
                css_anim_computed(s, v);
        } else if (v->unit == CSS_UNIT_EM) { v->unit = CSS_UNIT_PX; v->num *= fs; }
        else if (v->unit == CSS_UNIT_REM) { v->unit = CSS_UNIT_PX; v->num *= env->rem; }
        else if (v->unit == CSS_UNIT_NUMBER) v->unit = CSS_UNIT_PX;
        else if (v->unit == CSS_UNIT_CALC) css_calc_computed(s, p, v, fs, env->rem, -1, color);
        return;
    }
    if (g == G_NUM) {
        if (v->unit == CSS_UNIT_CALC) css_calc_computed(s, p, v, fs, env->rem, -1, color);
        if (v->unit == CSS_UNIT_PERCENT) { v->unit = CSS_UNIT_NUMBER; v->num /= 100; }
        if (p == CSS_PROP_OPACITY && v->unit == CSS_UNIT_NUMBER) v->num = v->num < 0 ? 0 : v->num > 1 ? 1 : v->num;
        return;
    }
    if (g != G_LEN) return;
    if (p == CSS_PROP_LINE_HEIGHT) {
        if (v->unit == CSS_UNIT_EM) { v->unit = CSS_UNIT_PX; v->num *= fs; }
        else if (v->unit == CSS_UNIT_REM) { v->unit = CSS_UNIT_PX; v->num *= env->rem; }
        else if (v->unit == CSS_UNIT_PERCENT) { v->unit = CSS_UNIT_PX; v->num = fs * v->num / 100; }
        else if (v->unit == CSS_UNIT_CALC) css_calc_computed(s, p, v, fs, env->rem, fs, color);
        return;
    }
    if (v->unit == CSS_UNIT_KEYWORD && (css_word_in(v->word, "thin medium thick"))) {
        v->num = strcmp(v->word, "thin") == 0 ? 1 : strcmp(v->word, "thick") == 0 ? 5 : 3;
        v->unit = CSS_UNIT_PX;
        memset(v->word, 0, sizeof v->word);
        return;
    }
    if (v->unit == CSS_UNIT_EM) { v->unit = CSS_UNIT_PX; v->num *= fs; }
    else if (v->unit == CSS_UNIT_REM) { v->unit = CSS_UNIT_PX; v->num *= env->rem; }
    else if (v->unit == CSS_UNIT_NUMBER) v->unit = CSS_UNIT_PX;
    else if (v->unit == CSS_UNIT_CALC) css_calc_computed(s, p, v, fs, env->rem, -1, color);
}

/* a declaration's place in the cascade, larger winning: origin and importance with
   element-attached style above rules, then specificity, then order of appearance */
#define CSS_LAYER_LAST 8191
static uint64_t css_rank(const css_style *s, uint32_t dc) {
    uint32_t r = s->decl[dc].rule;
    css_origin o = s->sheet[s->rule[r].sheet].origin;
    int imp = s->decl[dc].important;
    uint64_t cls = o == CSS_ORIGIN_USER_AGENT ? (imp ? 6 : 1) : (uint64_t)((imp ? 4 : 2) + (o == CSS_ORIGIN_INLINE));
    /* the cascade layer: the unlayered rules after every layer; for !important, the order
       reversed (Cascade 5, 6.4) */
    uint32_t ly = s->rule[r].layer;
    uint64_t lr = ly ? s->layer[ly].rank : CSS_LAYER_LAST;
    if (lr > CSS_LAYER_LAST) lr = CSS_LAYER_LAST;
    if (imp) lr = CSS_LAYER_LAST - lr;
    return cls << 61 | lr << 48 | (uint64_t)(s->rule[r].spec & 0xffffff) << 24 | (s->rule[r].order & 0xffffff);
}

static void css_decl_value(const css_style *s, uint32_t dc, css_val *v) {
    memset(v, 0, sizeof *v);
    v->unit = s->decl[dc].unit;
    v->num = s->decl[dc].num;
    v->color = s->decl[dc].color;
    v->tx = s->decl[dc].text;
    if (s->decl[dc].word && v->unit == CSS_UNIT_KEYWORD) snprintf(v->word, sizeof v->word, "%s", names_ptr(s, s->decl[dc].word));
}

/* a computed value's text, kept for the values met before: a page's values repeat, so
   most are written once (PLAN.md §11) */
typedef struct css_vtext { uint32_t used; css_unit unit; uint32_t color, word; uint64_t num, pct; amb_span text; } css_vtext;
#define CSS_VTEXT 1024

/* what a computed value records beside its value: its word's atom, its text, how it was
   found and the declaration that won */
typedef struct css_rec { uint32_t word; amb_span text; css_how how; uint32_t winner; } css_rec;

static css_rec css_value_rec(css_style *s, const css_val *v, css_how how, uint32_t winner, vector<css_vtext> *vc) {
    css_rec r;
    /* a keyword's word; and an inherited colour's currentcolor, which its children follow */
    r.word = v->unit == CSS_UNIT_KEYWORD || (v->unit == CSS_UNIT_COLOR && v->word[0]) ? names_intern(s, v->word, strlen(v->word)) : 0;
    r.how = how;
    r.winner = winner;
    if (v->unit == CSS_UNIT_TEXT || v->unit == CSS_UNIT_EXPR) { r.text = v->tx; return r; }
    uint64_t nb, pb;
    memcpy(&nb, &v->num, sizeof nb);
    memcpy(&pb, &v->pct, sizeof pb);
    uint32_t hh = ((uint32_t)(nb ^ (nb >> 29) ^ (pb * 31)) * 2654435761u) ^ (v->color * 40503u) ^ (r.word * 9973u) ^ (uint32_t)v->unit;
    size_t slot = (hh ^ (hh >> 15)) & (CSS_VTEXT - 1);
    css_vtext e = at(*vc, slot);
    if (e.used && e.unit == v->unit && e.color == v->color && e.word == r.word && e.num == nb && e.pct == pb) { r.text = e.text; return r; }
    char buf[96];
    size_t bn = css_value_text(v, buf, sizeof buf);
    r.text = text_add(s, buf, bn);
    e.used = 1;
    e.unit = v->unit;
    e.color = v->color;
    e.word = r.word;
    e.num = nb;
    e.pct = pb;
    e.text = r.text;
    (*vc)[slot] = e;
    return r;
}

static css_cval_h css_put(css_style *s, uint32_t style, css_prop p, const css_val *v, css_rec r) {
    css_cval_h h = cval_new(s);
    s->cval[h.index].style = style;
    s->cval[h.index].prop = p;
    s->cval[h.index].unit = v->unit;
    s->cval[h.index].num = v->num;
    s->cval[h.index].pct = v->pct;
    s->cval[h.index].color = v->color;
    s->cval[h.index].word = r.word;
    s->cval[h.index].text = r.text;
    s->cval[h.index].how = r.how;
    s->cval[h.index].winner = r.winner;
    return h;
}

/* ---- custom properties and var() (CSS Variables 1) ----
   An element's custom properties are its parent's, then its own declarations' (the
   cascade's winner a name). A value's var()s are replaced at computed-value time; a
   cycle of references makes every property in it invalid, and a var() of an invalid or
   missing property takes its fallback, or makes the whole value invalid. */

/* a custom property of a style: its name's atom, its value (var() replaced once state is
   2), how and by which declaration; state 0 to replace, 1 replacing, 2 done, 3 invalid */
typedef struct css_cust { uint32_t name; amb_span value; css_how how; uint32_t winner; uint8_t state, cyc; } css_cust;

static int css_subst(css_style *s, const char *t, size_t n, vector<css_cust> *cur, int depth, vector<char> *out);

/* the custom property cur[k] made computed: 1 when its value is valid */
static int css_resolve_custom(css_style *s, vector<css_cust> *cur, uint32_t k, int depth) {
    css_cust c = at(*cur, k);
    if (c.state == 2) return 1;
    if (c.state == 3) return 0;
    if (c.state == 1 || depth > 32) {
        /* a cycle: every property being replaced is in it */
        for (size_t q = 0; q < len(*cur); q++) if (at(*cur, q).state == 1) (*cur)[q].cyc = 1;
        return 0;
    }
    (*cur)[k].state = 1;
    vector<char> src, out;
    append_range(src, text_ptr(s, c.value), c.value.len);
    int ok = css_subst(s, data(src), len(src), cur, depth + 1, &out) && !at(*cur, k).cyc;
    if (ok) {
        /* trimmed, as a custom property's value is */
        size_t a = 0, b = len(out);
        while (a < b && css_ws(at(out, a))) a++;
        while (b > a && css_ws(at(out, b - 1))) b--;
        amb_span sp = text_add(s, data(out) + a, b - a);
        (*cur)[k].value = sp;
        (*cur)[k].state = 2;
    } else {
        (*cur)[k].state = 3;
        (*cur)[k].how = CSS_HOW_INITIAL;
        (*cur)[k].value = text_add(s, "", 0);
    }
    return ok;
}

/* the text t (the caller's copy, n bytes) with each var() replaced by its custom
   property's value, or by its fallback: 0 when one has neither */
static int css_subst(css_style *s, const char *t, size_t n, vector<css_cust> *cur, int depth, vector<char> *out) {
    size_t i = 0;
    if (depth > 64) return 0;
    while (i < n) {
        int at_var = i + 4 <= n && css_lower(t[i]) == 'v' && css_lower(t[i + 1]) == 'a' && css_lower(t[i + 2]) == 'r' && t[i + 3] == '(' &&
                     (i == 0 || !css_namechar(t[i - 1]));
        if (!at_var) {
            if (t[i] == '"' || t[i] == '\'') {
                size_t e = css_string_end(t, n, i);
                size_t stop = e < n ? e + 1 : n;
                append_range(out, t + i, stop - i);
                i = stop;
                continue;
            }
            push_back(out, t[i]);
            i++;
            continue;
        }
        size_t close = css_block_end(t, n, i + 3, '(', ')');
        if (close >= n) return 0;
        size_t a = css_skip_ws(t, close, i + 4), ne = a;
        while (ne < close && css_namechar(t[ne])) ne++;
        if (ne - a < 3 || t[a] != '-' || t[a + 1] != '-') return 0;
        uint32_t name = names_find(s, t + a, ne - a);
        size_t comma = css_skip_ws(t, close, ne);
        int has_fb = comma < close && t[comma] == ',';
        if (!has_fb && comma != close) return 0;
        int done = 0;
        for (uint32_t k = 0; name && k < (uint32_t)len(*cur) && !done; k++) {
            if (at(*cur, k).name != name) continue;
            if (css_resolve_custom(s, cur, k, depth)) {
                amb_span v = at(*cur, k).value;
                append_range(out, text_ptr(s, v), v.len);
                done = 1;
            } else k = (uint32_t)len(*cur);
        }
        if (!done) {
            if (!has_fb) return 0;
            if (!css_subst(s, t + comma + 1, close - comma - 1, cur, depth + 1, out)) return 0;
        }
        i = close + 1;
    }
    return 1;
}

/* a declared value with var() in it: replaced, then read as its declaration was (its
   shorthand's, the declaration's word); 0 when it is invalid at computed-value time */
static int css_var_value(css_style *s, uint32_t dc, css_prop p, vector<css_cust> *cur, css_val *v) {
    vector<char> src, out;
    amb_span sp = s->decl[dc].text;
    append_range(src, text_ptr(s, sp), sp.len);
    if (!css_subst(s, data(src), len(src), cur, 0, &out)) return 0;
    size_t a = 0, b = len(out);
    while (a < b && css_ws(at(out, a))) a++;
    while (b > a && css_ws(at(out, b - 1))) b--;
    char name[48];
    uint32_t w = s->decl[dc].word;
    if (w) snprintf(name, sizeof name, "%.*s", (int)names_len(s, w), names_ptr(s, w));
    else snprintf(name, sizeof name, "%s", css_pdefs[p].name);
    css_items it;
    if (b == a || !css_expand(name, strlen(name), data(out) + a, b - a, &it)) return 0;
    for (int k = 0; k < it.n; k++) {
        css_item x = it.it[k];
        if (x.prop != p) continue;
        *v = x.v;
        if (v->unit == CSS_UNIT_TEXT || v->unit == CSS_UNIT_CALC) {
            if (x.mine) v->tx = text_add(s, it.buf + x.ts, x.tn);
            else v->tx = text_add(s, data(out) + a + x.ts, x.tn);
        }
        return 1;
    }
    return 0;
}

/* what each property is to the cascade, made once: inherited, its initial value
   currentcolor, a border's width (its style the property 4 on) */
enum { CSS_PF_INH = 1, CSS_PF_CC = 2, CSS_PF_BW = 4 };
static uint8_t css_pf[CSS_NPROPS];

/* the order values are computed in: color-scheme (the scheme the colours are taken in),
   font-family, then font-size (the em of the others, whose defaults the family reads),
   then color (their currentcolor), position and float
   before display (which they blockify), the border and outline styles before their
   widths (none makes a width 0) */
static int css_order[CSS_NPROPS];
static void css_order_make(void) {
    css_ready = 1;
    for (int p = 0; p < CSS_NPROPS; p++) css_name_put(css_pdefs[p].name, p + 1);
    for (int i = 0; i < CSS_NSHORTS; i++) css_name_put(css_shorts[i].name, -(i + 1));
    for (int p = 0; p < CSS_NPROPS; p++) {
        int k = 0;
        size_t i = 0, n = css_pdefs[p].words ? strlen(css_pdefs[p].words) : 0;
        while (i < n) {
            size_t b = i;
            while (i < n && css_pdefs[p].words[i] != ' ') i++;
            if (k < CSS_MAXKW) {
                css_kwh[p][k] = css_hash_ci(css_pdefs[p].words + b, i - b);
                css_kwo[p][k] = (uint16_t)b;
                css_kwl[p][k] = (uint8_t)(i - b);
            }
            k++;
            i++;
        }
        css_kwn[p] = (uint8_t)(k > CSS_MAXKW ? CSS_MAXKW + 1 : k);
    }
    static const int first[] = { CSS_PROP_COLOR_SCHEME, CSS_PROP_FONT_FAMILY, CSS_PROP_FONT_SIZE, CSS_PROP_COLOR, CSS_PROP_POSITION,
                                 CSS_PROP_FLOAT, CSS_PROP_DISPLAY,
                                 CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_RIGHT_STYLE, CSS_PROP_BORDER_BOTTOM_STYLE,
                                 CSS_PROP_BORDER_LEFT_STYLE, CSS_PROP_OUTLINE_STYLE };
    int n = 0, nf = (int)(sizeof first / sizeof first[0]);
    for (int i = 0; i < nf; i++) css_order[n++] = first[i];
    for (int p = 0; p < CSS_NPROPS; p++) {
        int seen = 0;
        for (int i = 0; i < nf; i++) if (first[i] == p) seen = 1;
        if (!seen) css_order[n++] = p;
    }
    for (int p = 0; p < CSS_NPROPS; p++)
        css_pf[p] = (uint8_t)((css_pdefs[p].inh ? CSS_PF_INH : 0) | (css_init_cc((css_prop)p) ? CSS_PF_CC : 0) |
                              (p >= CSS_PROP_BORDER_TOP_WIDTH && p <= CSS_PROP_BORDER_LEFT_WIDTH ? CSS_PF_BW : 0));
}

/* display blockified (CSS Display 3, 2.7): a float's, an absolutely positioned box's, the
   root's, a flex or grid item's */
static void css_blockify(css_val *v) {
    if (v->unit != CSS_UNIT_KEYWORD) return;
    const char *w = v->word;
    if (strcmp(w, "inline-table") == 0) css_word_val(v, "table");
    else if (strcmp(w, "inline-flex") == 0) css_word_val(v, "flex");
    else if (strcmp(w, "inline-grid") == 0) css_word_val(v, "grid");
    else if (strcmp(w, "-webkit-inline-box") == 0) css_word_val(v, "-webkit-box");
    else if (strcmp(w, "inline") == 0 || strcmp(w, "inline-block") == 0 || strncmp(w, "table-", 6) == 0) css_word_val(v, "block");
}

/* the per-style arrays the cascade keeps: values, what they record, custom properties;
   each parent style's template (tmpl_of: its index from 1 in tv and tr); and where each
   style's font size came from (fsx: CSS_FS_*), which its children's read */
typedef struct css_styles { vector<uint32_t> first; vector<css_cust> cpool; vector<uint32_t> cfirst; vector<uint32_t> ccount;
                            vector<uint32_t> tmpl_of; vector<css_val> tv; vector<css_rec> tr; vector<uint8_t> fsx; } css_styles;
#define CSS_STYLES_DROP(x) vector_uint32_t_drop(&(x).first); vector_css_cust_drop(&(x).cpool); \
    vector_uint32_t_drop(&(x).cfirst); vector_uint32_t_drop(&(x).ccount); vector_uint32_t_drop(&(x).tmpl_of); \
    vector_css_val_drop(&(x).tv); vector_css_rec_drop(&(x).tr); vector_uint8_t_drop(&(x).fsx)
/* a font size's origin, as Chromium's FontDescription keeps it: the absolute keyword it
   is (its column + 1; 0 for another), whether it is absolute (a length that is not
   relative to the parent's size, or relative to an absolute one), whether its family is
   monospace alone. The root's parent's: medium */
enum { CSS_FS_KW = 0x0f, CSS_FS_ABS = 0x10, CSS_FS_MONO = 0x20, CSS_FS_MEDIUM = 4 };

/* a style's value of p (its cvals from c0), or the initial one under no style, and its record */
static void css_val_of(const css_style *s, uint32_t c0, css_prop p, const css_val *initial, css_val *v) {
    if (!c0) { *v = initial[p]; return; }
    uint32_t c = c0 + (uint32_t)p;
    memset(v, 0, sizeof *v);
    v->unit = s->cval[c].unit;
    v->num = s->cval[c].num;
    v->pct = s->cval[c].pct;
    v->color = s->cval[c].color;
    v->tx = s->cval[c].text;
    if (v->unit == CSS_UNIT_KEYWORD || (v->unit == CSS_UNIT_COLOR && names_len(s, s->cval[c].word)))
        snprintf(v->word, sizeof v->word, "%.*s", (int)names_len(s, s->cval[c].word), names_ptr(s, s->cval[c].word));
}
static css_rec css_rec_of(const css_style *s, uint32_t c) {
    css_rec r;
    r.word = s->cval[c].word;
    r.text = s->cval[c].text;
    r.how = s->cval[c].how;
    r.winner = s->cval[c].winner;
    return r;
}
static void css_par_of(const css_style *s, uint32_t c0, const css_val *initial, css_par *pv) {
    pv->fs = c0 ? s->cval[c0 + CSS_PROP_FONT_SIZE].num : initial[CSS_PROP_FONT_SIZE].num;
    pv->weight = c0 ? s->cval[c0 + CSS_PROP_FONT_WEIGHT].num : initial[CSS_PROP_FONT_WEIGHT].num;
    pv->color = c0 ? s->cval[c0 + CSS_PROP_COLOR].color : initial[CSS_PROP_COLOR].color;
}

static int css_word_is(const css_val *v, const char *w) { return v->unit == CSS_UNIT_KEYWORD && v->word[0] == w[0] && strcmp(v->word, w) == 0; }

/* a border width made computed by its style: 0 under none or hidden, else a whole px (a
   thin one 1); an outline's is only snapped */
static void css_snap_width(css_val *v, const css_val *style, int border) {
    if (border && (css_word_is(style, "none") || css_word_is(style, "hidden"))) { v->unit = CSS_UNIT_PX; v->num = 0; return; }
    if (v->unit == CSS_UNIT_PX && v->num > 0 && v->num < 1) v->num = 1;
    else if (v->unit == CSS_UNIT_PX && v->num >= 1) v->num = (double)(long)v->num;
}

/* display blockified where the element's position, float, root or parent ask it; then the
   legacy line clamp's (CSS Overflow 4, 4.2): a -webkit-box or -webkit-inline-box whose
   -webkit-box-orient is vertical and whose -webkit-line-clamp is a number computes to
   flow-root or inline-block, as Chromium's does. Its num 1 records that the old box was
   specified (Chromium's IsSpecifiedDisplayWebkitBox): layout clamps such a box alone, not
   one specified flow-root */
static int css_display_fix(css_val *v, const css_val *out, const css_val *pdisplay, int is_root) {
    int pflex = !is_root && (css_word_is(pdisplay, "flex") || css_word_is(pdisplay, "inline-flex") || css_word_is(pdisplay, "grid") ||
                             css_word_is(pdisplay, "inline-grid"));
    css_val b = *v;
    if (is_root && css_word_is(v, "contents")) css_word_val(v, "block");
    if (is_root || pflex || !css_word_is(&out[CSS_PROP_FLOAT], "none") || css_word_is(&out[CSS_PROP_POSITION], "absolute") ||
        css_word_is(&out[CSS_PROP_POSITION], "fixed")) css_blockify(v);
    int box = css_word_is(v, "-webkit-box"), ibox = css_word_is(v, "-webkit-inline-box");
    if ((box || ibox) && css_word_is(&out[CSS_PROP_WEBKIT_BOX_ORIENT], "vertical") &&
        out[CSS_PROP_WEBKIT_LINE_CLAMP].unit == CSS_UNIT_NUMBER) {
        css_word_val(v, box ? "flow-root" : "inline-block");
        v->num = 1;
    }
    return strcmp(b.word, v->word) != 0;
}

/* the template of a parent style ps (0: none, the root's): the values an element under it
   takes before its own declarations -- an inherited property's the parent's, any other's
   its initial value -- with what each records. Made once a parent style */
static uint32_t css_template(css_style *s, css_styles *ss, uint32_t ps, const css_val *initial, const css_rec *irec, vector<css_vtext> *vc) {
    if (ps < len(ss->tmpl_of) && at(ss->tmpl_of, ps)) return at(ss->tmpl_of, ps);
    if (ps >= len(ss->tmpl_of)) resize(&ss->tmpl_of, (size_t)ps + 1);
    uint32_t ti = (uint32_t)(len(ss->tv) / CSS_NPROPS) + 1;
    uint32_t pc0 = ps ? at(ss->first, ps) : 0;
    css_val pcolor, pdisplay;
    css_val_of(s, pc0, CSS_PROP_COLOR, initial, &pcolor);
    css_val_of(s, pc0, CSS_PROP_DISPLAY, initial, &pdisplay);
    css_val out[CSS_NPROPS];
    css_rec rec[CSS_NPROPS];
    for (int k = 0; k < CSS_NPROPS; k++) {
        if ((css_pf[k] & CSS_PF_INH) && ps) {
            css_val_of(s, pc0, (css_prop)k, initial, &out[k]);
            rec[k] = css_rec_of(s, pc0 + (uint32_t)k);
            rec[k].how = CSS_HOW_INHERITED;
            rec[k].winner = 0;
            continue;
        }
        out[k] = initial[k];
        rec[k] = irec[k];
    }
    for (int k = 0; k < CSS_NPROPS; k++) {
        int fix = 0;
        if ((css_pf[k] & CSS_PF_CC) && (!(css_pf[k] & CSS_PF_INH) || !ps || strcmp(out[k].word, "currentcolor") == 0)) {
            out[k].color = pcolor.color;
            if (css_pf[k] & CSS_PF_INH) memcpy(out[k].word, "currentcolor", 13);
            fix = 1;
        }
        if (css_pf[k] & CSS_PF_BW) { css_snap_width(&out[k], &out[k + 4], 1); fix = 1; }
        if (k == CSS_PROP_DISPLAY) fix = css_display_fix(&out[k], out, &pdisplay, ps == 0);
        if (fix) rec[k] = css_value_rec(s, &out[k], CSS_HOW_INITIAL, 0, vc);
    }
    for (int k = 0; k < CSS_NPROPS; k++) { push_back(ss->tv, out[k]); push_back(ss->tr, rec[k]); }
    ss->tmpl_of[ps] = ti;
    return ti;
}

/* whether a calc()'s text is relative to the parent's font size (an em, ex, ch, rem or
   percentage in it) */
static int css_text_font_relative(const css_style *s, amb_span tx) {
    const char *t = text_ptr(s, tx);
    for (uint32_t i = 0; i < tx.len; i++) {
        char c = css_lower(t[i]);
        if (c == '%') return 1;
        if (i + 1 < tx.len && i > 0 && (css_is_digit(t[i - 1]) || t[i - 1] == '.')) {
            char d = css_lower(t[i + 1]);
            if ((c == 'e' && (d == 'm' || d == 'x')) || (c == 'c' && d == 'h') || (c == 'r' && d == 'e')) return 1;
        }
    }
    return 0;
}

/* an element's font size: its winning declaration's (w), made computed as any value is,
   or its parent's; then Chromium's rule for a family that is monospace alone
   (FontBuilder's CheckForGenericFamilyChange): when the element's is and its parent's
   is not, or the reverse, a size that is not absolute takes the other default -- a
   keyword's from its table, another scaled by the fixed default over the standard one.
   A size that rule changes, which no declaration of its own gave, is recorded as its
   family's declaration gave it (the reasoner's derived values read the declared ones).
   *fso: the size's origin, for the element's children */
static void css_font_size(css_style *s, const css_env *env, uint32_t w, uint32_t wfam, const vector<css_cust> *cust, uint32_t pc0,
                          const css_val *initial, const css_par *pv, uint8_t pfso, int is_root, css_val *out, css_rec *rec,
                          vector<css_vtext> *vc, uint8_t *fso) {
    css_prop p = CSS_PROP_FONT_SIZE;
    int mono = css_word_is(&out[CSS_PROP_FONT_FAMILY], "monospace"), pmono = (pfso & CSS_FS_MONO) != 0;
    uint8_t o = (uint8_t)(pfso & (CSS_FS_KW | CSS_FS_ABS));
    css_val v = out[p];
    css_how how = CSS_HOW_INHERITED;
    if (w) {
        css_decl_value(s, w, &v);
        int valid = v.unit != CSS_UNIT_VAR || css_var_value(s, w, p, cust, &v);
        int unset = !valid || css_is_word(&v, "unset") || css_is_word(&v, "revert") || css_is_word(&v, "revert-layer");
        if ((css_is_word(&v, "inherit") || unset) && !is_root) { css_val_of(s, pc0, p, initial, &v); how = CSS_HOW_INHERITED; }
        else if (css_is_word(&v, "inherit") || unset || css_is_word(&v, "initial")) {
            v = initial[p];
            v.num = css_size_at(3, mono ? CSS_FIXED_PX : css_font_px);
            o = CSS_FS_MEDIUM;
            how = CSS_HOW_INITIAL;
        } else {
            int k = v.unit == CSS_UNIT_KEYWORD ? css_size_column(v.word) : -1;
            if (k >= 0) o = (uint8_t)(k + 1);
            else if (v.unit == CSS_UNIT_KEYWORD || v.unit == CSS_UNIT_EM || v.unit == CSS_UNIT_REM || v.unit == CSS_UNIT_PERCENT ||
                     ((v.unit == CSS_UNIT_CALC || v.unit == CSS_UNIT_EXPR) && css_text_font_relative(s, v.tx))) o = (uint8_t)(pfso & CSS_FS_ABS);
            else o = CSS_FS_ABS;
            css_compute(s, env, p, &v, pv, out);
            how = CSS_HOW_DECLARED;
        }
    }
    int changed = 0;
    if (!(o & CSS_FS_ABS) && mono != pmono) {
        int kw = o & CSS_FS_KW;
        v.num = kw ? css_size_at(kw - 1, mono ? CSS_FIXED_PX : css_font_px) :
                pmono ? v.num * css_font_px / CSS_FIXED_PX : v.num * CSS_FIXED_PX / css_font_px;
        changed = 1;
    }
    *fso = (uint8_t)(o | (mono ? CSS_FS_MONO : 0));
    if (!w && !changed) return;
    if (!w) { w = wfam; how = CSS_HOW_DECLARED; }
    out[p] = v;
    rec[p] = css_value_rec(s, &v, how, w, vc);
}

/* the value declaration dc gives property p of an element (its parent's values pv, its
   own computed so far out): var() substituted, a CSS-wide keyword taken, computed, a
   border's width snapped; how it came */
static css_how css_decl_computed(css_style *s, const css_env *env, css_prop p, uint32_t dc, vector<css_cust> *cust, uint32_t pc0,
                                 const css_val *initial, const css_par *pv, css_val *out, int is_root, css_val *v) {
    css_how how;
    css_decl_value(s, dc, v);
    int valid = v->unit != CSS_UNIT_VAR || css_var_value(s, dc, p, cust, v);
    int unset = !valid || css_is_word(v, "unset") || css_is_word(v, "revert") || css_is_word(v, "revert-layer");
    int inherit = css_is_word(v, "inherit") || (unset && (css_pf[p] & CSS_PF_INH));
    int initial_kw = css_is_word(v, "initial") || (unset && !(css_pf[p] & CSS_PF_INH));
    if (inherit && !is_root) { css_val_of(s, pc0, p, initial, v); how = CSS_HOW_INHERITED; }
    else if (inherit || initial_kw) { *v = initial[p]; how = CSS_HOW_INITIAL; }
    else { css_compute(s, env, p, v, pv, out); how = CSS_HOW_DECLARED; }
    if (how == CSS_HOW_INITIAL && (css_pf[p] & CSS_PF_CC)) v->color = out[CSS_PROP_COLOR].color;
    if (css_pf[p] & CSS_PF_BW) { if (v->unit == CSS_UNIT_KEYWORD) css_compute(s, env, p, v, pv, out); css_snap_width(v, &out[p + 4], 1); }
    if (p == CSS_PROP_OUTLINE_WIDTH) { if (v->unit == CSS_UNIT_KEYWORD) css_compute(s, env, p, v, pv, out); css_snap_width(v, 0, 0); }
    return how;
}

/* ---- animations at rest (CSS Animations 1, Web Animations 1, CSS Easing 2) ---- */

/* the time a page is drawn at rest at, ms after it was loaded: what the shell's scripts
   run to (src/shell/main.c, SCRIPT_HORIZON); each animation is evaluated then */
#define CSS_REST_MS 10000.0

/* a cubic Bézier easing's output at x (its x solved for t: Newton's steps, then halving);
   past 0 and 1 the line its end's tangent makes (CSS Easing 2, 3.1) */
static double css_bezier(double x1, double y1, double x2, double y2, double x) {
    if (x <= 0) return (x1 > 0 ? y1 / x1 : x2 > 0 ? y2 / x2 : 0) * x;
    if (x >= 1) return 1 + (x2 < 1 ? (y2 - 1) / (x2 - 1) : x1 < 1 ? (y1 - 1) / (x1 - 1) : 0) * (x - 1);
    double cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx;
    double cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
    double t = x;
    for (int i = 0; i < 8; i++) {
        double e = ((ax * t + bx) * t + cx) * t - x;
        if (fabs(e) < 1e-7) return ((ay * t + by) * t + cy) * t;
        double d = (3 * ax * t + 2 * bx) * t + cx;
        if (fabs(d) < 1e-6) break;
        t -= e / d;
    }
    double lo = 0, hi = 1;
    t = x;
    for (int i = 0; i < 64; i++) {
        double xt = ((ax * t + bx) * t + cx) * t;
        if (fabs(xt - x) < 1e-7) break;
        if (xt < x) lo = t;
        else hi = t;
        t = (lo + hi) / 2;
    }
    return ((ay * t + by) * t + cy) * t;
}

/* an easing function's output at x (its text s): the keywords, cubic-bezier(), steps()
   and its positions, linear() and its stops (CSS Easing 2) */
static double css_ease_at(const char *s, size_t n, double x) {
    if (n == 0 || css_eq_ci(s, n, "linear")) return x;
    if (css_eq_ci(s, n, "ease")) return css_bezier(0.25, 0.1, 0.25, 1, x);
    if (css_eq_ci(s, n, "ease-in")) return css_bezier(0.42, 0, 1, 1, x);
    if (css_eq_ci(s, n, "ease-out")) return css_bezier(0, 0, 0.58, 1, x);
    if (css_eq_ci(s, n, "ease-in-out")) return css_bezier(0.42, 0, 0.58, 1, x);
    /* steps: their count; their position: 0 jump-end, 1 jump-start, 2 jump-none, 3 jump-both */
    int steps = 0, pos = 0;
    if (css_eq_ci(s, n, "step-start")) { steps = 1; pos = 1; }
    else if (css_eq_ci(s, n, "step-end")) steps = 1;
    size_t open = 0;
    while (open < n && s[open] != '(') open++;
    const char *a = s + (open < n ? open + 1 : n);
    size_t an = open + 2 <= n ? n - open - 2 : 0, j = 0, xs, xl;
    if (!steps && open < n && css_eq_ci(s, open, "cubic-bezier")) {
        double v[4] = { 0, 0, 1, 1 };
        int k = 0;
        while (k < 4 && css_next_part(a, an, &j, &xs, &xl)) { css_number(a + xs, xl, &v[k]); k++; }
        return css_bezier(v[0], v[1], v[2], v[3], x);
    }
    if (!steps && open < n && css_eq_ci(s, open, "steps")) {
        double c = 1;
        if (css_next_part(a, an, &j, &xs, &xl)) css_number(a + xs, xl, &c);
        steps = c < 1 ? 1 : (int)c;
        if (css_next_part(a, an, &j, &xs, &xl)) {
            const char *q = a + xs;
            if (css_eq_ci(q, xl, "jump-start") || css_eq_ci(q, xl, "start")) pos = 1;
            else if (css_eq_ci(q, xl, "jump-none")) pos = 2;
            else if (css_eq_ci(q, xl, "jump-both")) pos = 3;
        }
    }
    if (steps) {
        double step = floor(x * steps);
        if (pos == 1 || pos == 3) step += 1;
        int jumps = steps + (pos == 3) - (pos == 2);
        if (x >= 0 && step < 0) step = 0;
        if (x <= 1 && step > jumps) step = jumps;
        return jumps > 0 ? step / jumps : 0;
    }
    if (open < n && css_eq_ci(s, open, "linear")) {
        /* its stops: an output each, at the input given, or spread evenly between those given */
        double out[64], in[64];
        int given[64], m = 0;
        size_t b = 0;
        while (b <= an && m < 62) {
            size_t e = css_comma(a, an, b);
            size_t k = 0, ps, pl;
            int nums = 0, pcts = 0;
            double o = 0, p1 = 0, p2 = 0;
            while (css_next_part(a + b, e - b, &k, &ps, &pl)) {
                double v;
                css_number(a + b + ps, pl, &v);
                if (!nums) { o = v; nums = 1; }
                else if (!pcts) { p1 = v / 100; pcts = 1; }
                else { p2 = v / 100; pcts = 2; }
            }
            out[m] = o; in[m] = p1; given[m] = pcts > 0; m++;
            if (pcts == 2) { out[m] = o; in[m] = p2; given[m] = 1; m++; }
            if (e >= an) break;
            b = e + 1;
        }
        if (m < 2) return x;
        if (!given[0]) { in[0] = 0; given[0] = 1; }
        if (!given[m - 1]) { in[m - 1] = 1; given[m - 1] = 1; }
        double hi = in[0];
        for (int k = 1; k < m; k++) if (given[k]) { if (in[k] < hi) in[k] = hi; hi = in[k]; }
        for (int k = 1; k < m; k++) {
            if (given[k]) continue;
            int e = k;
            while (!given[e]) e++;
            for (int q = k; q < e; q++) in[q] = in[k - 1] + (in[e] - in[k - 1]) * (q - k + 1) / (e - k + 1);
            k = e;
        }
        if (x <= in[0]) return out[0];
        for (int k = 1; k < m; k++)
            if (x <= in[k]) return in[k] > in[k - 1] ? out[k - 1] + (out[k] - out[k - 1]) * (x - in[k - 1]) / (in[k] - in[k - 1]) : out[k];
        return out[m - 1];
    }
    return x;
}

/* the i-th item of a comma-separated list, trimmed (the list repeated as far as it must
   be): its start and length */
static void css_list_nth(const char *s, size_t n, size_t i, size_t *st, size_t *ln) {
    size_t count = 1, a = 0, c;
    while ((c = css_comma(s, n, a)) < n) { count++; a = c + 1; }
    i %= count;
    a = 0;
    for (size_t k = 0; k < i; k++) a = css_comma(s, n, a) + 1;
    size_t b = css_comma(s, n, a), sa = css_skip_ws(s, b, a), sb = b;
    while (sb > sa && css_ws(s[sb - 1])) sb--;
    *st = sa;
    *ln = sb - sa;
}

/* an animation's iteration progress at time t (ms from its start): its duration D, delay
   d, iteration count it (INFINITY for infinite), direction (0 normal, 1 reverse, 2
   alternate, 3 alternate-reverse) and fill (1 backwards, 2 forwards, both 3); 0 when it
   has no effect then (Web Animations 1, 4.8 to 4.10) */
static int css_anim_progress(double t, double D, double d, double it, int dir, int fill, double *out) {
    double ad = D <= 0 || it <= 0 ? 0 : it == INFINITY ? INFINITY : D * it;
    double end = d + ad > 0 ? d + ad : 0;
    double bab = fmax(fmin(d, end), 0), aab = fmax(fmin(d + ad, end), 0);
    int phase = t < bab ? 0 : t >= aab ? 2 : 1;
    double act;
    if (phase == 0) { if (!(fill & 1)) return 0; act = fmax(t - d, 0); }
    else if (phase == 2) { if (!(fill & 2)) return 0; act = fmax(fmin(t - d, ad), 0); }
    else act = t - d;
    double op = D <= 0 ? (phase == 0 ? 0 : it) : act / D;
    double sip = 0;
    if (op != INFINITY) {
        sip = op - floor(op);
        if (sip == 0 && phase != 0 && act == ad && it != 0) sip = 1;
    }
    double ci = phase == 2 && it == INFINITY ? 0 : sip == 1 ? floor(op) - 1 : floor(op);
    int fwd = dir != 1;
    if (dir >= 2) fwd = fmod(ci + (dir == 3), 2) == 0;
    *out = fwd ? sip : 1 - sip;
    return 1;
}

/* the @keyframes an animation's name names: of those so named, the one in the cascade
   layer that comes last (the unlayered after every layer), the last of them in the
   sheets' order (CSS Animations 1, 4; Cascade 5, 6.4.3) */
static uint32_t css_find_keyframes(const css_style *s, const char *nm, size_t nl) {
    uint32_t best = 0;
    uint64_t bk = 0;
    for (uint32_t k = 1; k < s->keyframes_len; k++) {
        if (!s->keyframes[k].live) continue;
        amb_span sp = s->keyframes[k].name;
        if (sp.len != nl || memcmp(text_ptr(s, sp), nm, nl) != 0) continue;
        uint32_t ly = s->keyframes[k].layer;
        uint64_t lr = ly ? s->layer[ly].rank : CSS_LAYER_LAST;
        uint64_t key = lr << 32 | k;
        if (key >= bk) { bk = key; best = k; }
    }
    return best;
}

/* the text of an element's animation longhand p: its winner's (var() substituted,
   inherit the parent's, the other CSS-wide keywords its initial value's) */
static void css_list_text(css_style *s, css_prop p, uint32_t w, vector<css_cust> *cust, uint32_t pc0, const css_val *initial,
                          int is_root, vector<char> *out) {
    css_val v = initial[p];
    if (w) {
        css_decl_value(s, w, &v);
        if (v.unit == CSS_UNIT_VAR && !css_var_value(s, w, p, cust, &v)) v = initial[p];
        if (css_is_word(&v, "inherit") && !is_root) css_val_of(s, pc0, p, initial, &v);
        else if (css_is_global(&v)) v = initial[p];
    }
    clear(*out);
    if (v.unit == CSS_UNIT_TEXT || v.unit == CSS_UNIT_EXPR) append_range(*out, text_ptr(s, v.tx), v.tx.len);
    else {
        char buf[96];
        size_t bn = css_value_text(&v, buf, sizeof buf);
        append_range(*out, buf, bn);
    }
}

/* a longhand an animation leaves alone in its keyframes: the animations' and the
   transitions' own (a keyframe's animation-timing-function is its easing) */
static int css_anim_own(css_prop p) { return p >= CSS_PROP_ANIMATION_NAME && p <= CSS_PROP_TRANSITION_BEHAVIOR; }

/* of an animation's keyframes kr (rules, by place), the two that property p's value (a
   custom property's, cname its name) at progress x lies between and the progress between them, eased (CSS Animations 1, 4;
   Web Animations 1, 5.3): each place's last declaration of p; at places 0 and 1 the
   element's own value (0) when no keyframe there gives one; a stretch eased by its first
   keyframe's animation-timing-function, else by the animation's (ease) */
static void css_anim_prop(const css_style *s, const vector<uint32_t> *kr, css_prop p, uint32_t cname, double x, const char *ease, size_t en,
                          uint32_t *k0, uint32_t *k1, double *f) {
    double off[66];
    uint32_t dec[66], er[66];
    int m = 0;
    for (size_t q = 0; q < len(*kr) && m < 64; q++) {
        uint32_t r = at(*kr, q), last = 0;
        for (uint32_t dc = s->rule[r].first_decl; dc; dc = s->decl[dc].next)
            if (s->decl[dc].prop == p && !s->decl[dc].important && (!cname || s->decl[dc].word == cname)) last = dc;
        if (!last) continue;
        double o = s->rule[r].offset;
        if (m && off[m - 1] == o) { dec[m - 1] = last; er[m - 1] = r; continue; }
        off[m] = o; dec[m] = last; er[m] = r; m++;
    }
    if (m == 0 || off[0] > 0) {
        for (int q = m; q > 0; q--) { off[q] = off[q - 1]; dec[q] = dec[q - 1]; er[q] = er[q - 1]; }
        off[0] = 0; dec[0] = 0; er[0] = 0; m++;
    }
    if (off[m - 1] < 1) { off[m] = 1; dec[m] = 0; er[m] = 0; m++; }
    int i = 0;
    if (x >= 1) i = m - 2;
    else if (x >= 0) while (i + 2 < m && off[i + 1] <= x) i++;
    double a = off[i], b = off[i + 1];
    double local = b > a ? (x - a) / (b - a) : 1;
    /* the stretch's easing */
    const char *et = ease;
    size_t etn = en;
    vector<char> kease;
    uint32_t tdc = 0;
    if (er[i]) {
        for (uint32_t dc = s->rule[er[i]].first_decl; dc; dc = s->decl[dc].next)
            if (s->decl[dc].prop == CSS_PROP_ANIMATION_TIMING_FUNCTION) tdc = dc;
    }
    if (tdc) append_range(kease, text_ptr(s, s->decl[tdc].text), s->decl[tdc].text.len);
    if (len(kease)) {
        size_t st, ln;
        css_list_nth(data(kease), len(kease), 0, &st, &ln);
        et = data(kease) + st;
        etn = ln;
    }
    *f = local == 0 ? 0 : local == 1 ? 1 : css_ease_at(et, etn, local);
    *k0 = dec[i];
    *k1 = dec[i + 1];
}

/* an element's animations at the page's rest time (CSS Animations 1): for each property a
   keyframe of one of them gives, the declarations its value lies between (0 for the
   element's own value) and the eased progress between them, in ak0, ak1 and af, with aon
   set; a later animation of the list over an earlier one. A property an !important
   declaration gives is left alone: animations come under the important declarations and
   over every other (Cascade 5, 6.1) */
static void css_anim_custom(css_style *s, const vector<uint32_t> *kr, uint32_t name, double x, const char *ease, size_t en,
                            vector<css_cust> *cust, double pem, double rem);
static void css_animate(css_style *s, const uint32_t *win, vector<css_cust> *cust, uint32_t pc0, const css_val *initial, int is_root,
                        double pem, double rem, uint32_t *ak0, uint32_t *ak1, double *af, uint8_t *aon) {
    vector<char> names, durs, eases, dels, its, dirs, fills, plays;
    css_list_text(s, CSS_PROP_ANIMATION_NAME, win[CSS_PROP_ANIMATION_NAME], cust, pc0, initial, is_root, &names);
    css_list_text(s, CSS_PROP_ANIMATION_DURATION, win[CSS_PROP_ANIMATION_DURATION], cust, pc0, initial, is_root, &durs);
    css_list_text(s, CSS_PROP_ANIMATION_TIMING_FUNCTION, win[CSS_PROP_ANIMATION_TIMING_FUNCTION], cust, pc0, initial, is_root, &eases);
    css_list_text(s, CSS_PROP_ANIMATION_DELAY, win[CSS_PROP_ANIMATION_DELAY], cust, pc0, initial, is_root, &dels);
    css_list_text(s, CSS_PROP_ANIMATION_ITERATION_COUNT, win[CSS_PROP_ANIMATION_ITERATION_COUNT], cust, pc0, initial, is_root, &its);
    css_list_text(s, CSS_PROP_ANIMATION_DIRECTION, win[CSS_PROP_ANIMATION_DIRECTION], cust, pc0, initial, is_root, &dirs);
    css_list_text(s, CSS_PROP_ANIMATION_FILL_MODE, win[CSS_PROP_ANIMATION_FILL_MODE], cust, pc0, initial, is_root, &fills);
    css_list_text(s, CSS_PROP_ANIMATION_PLAY_STATE, win[CSS_PROP_ANIMATION_PLAY_STATE], cust, pc0, initial, is_root, &plays);
    size_t count = 1, a = 0, c;
    while ((c = css_comma(data(names), len(names), a)) < len(names)) { count++; a = c + 1; }
    for (size_t i = 0; i < count; i++) {
        size_t st, ln;
        css_list_nth(data(names), len(names), i, &st, &ln);
        vector<char> nm;
        append_range(nm, data(names) + st, ln);
        if (css_eq_ci(data(nm), len(nm), "none")) continue;
        size_t ns = 0, nl = len(nm);
        if (css_is_string(data(nm), nl)) { ns = 1; nl -= 2; }
        uint32_t kf = css_find_keyframes(s, data(nm) + ns, nl);
        if (!kf) continue;
        double D = 0, d = 0, itc = 1, x;
        int dir = 0, fill = 0, paused = 0;
        css_list_nth(data(durs), len(durs), i, &st, &ln);
        if (!css_time_ms(data(durs) + st, ln, &D)) D = 0;
        css_list_nth(data(dels), len(dels), i, &st, &ln);
        if (!css_time_ms(data(dels) + st, ln, &d)) d = 0;
        css_list_nth(data(its), len(its), i, &st, &ln);
        if (css_eq_ci(data(its) + st, ln, "infinite")) itc = INFINITY;
        else if (css_number(data(its) + st, ln, &x) == ln && x >= 0) itc = x;
        css_list_nth(data(dirs), len(dirs), i, &st, &ln);
        if (css_eq_ci(data(dirs) + st, ln, "reverse")) dir = 1;
        else if (css_eq_ci(data(dirs) + st, ln, "alternate")) dir = 2;
        else if (css_eq_ci(data(dirs) + st, ln, "alternate-reverse")) dir = 3;
        css_list_nth(data(fills), len(fills), i, &st, &ln);
        if (css_eq_ci(data(fills) + st, ln, "backwards")) fill = 1;
        else if (css_eq_ci(data(fills) + st, ln, "forwards")) fill = 2;
        else if (css_eq_ci(data(fills) + st, ln, "both")) fill = 3;
        css_list_nth(data(plays), len(plays), i, &st, &ln);
        paused = css_eq_ci(data(plays) + st, ln, "paused");
        vector<char> ease;
        css_list_nth(data(eases), len(eases), i, &st, &ln);
        append_range(ease, data(eases) + st, ln);
        double prog;
        /* a paused animation stays at its start */
        if (!css_anim_progress(paused ? 0 : CSS_REST_MS, D, d, itc, dir, fill, &prog)) continue;
        /* its keyframes, by their places (the sheets' order among equals) */
        vector<uint32_t> kr;
        for (uint32_t r = 1; r < s->rule_len; r++) {
            if (!s->rule[r].live || s->rule[r].keyframes != kf) continue;
            push_back(kr, r);
            size_t q = len(kr) - 1;
            while (q > 0 && s->rule[at(kr, q - 1)].offset > s->rule[r].offset) { kr[q] = at(kr, q - 1); q--; }
            kr[q] = r;
        }
        uint8_t seen[CSS_NPROPS];
        memset(seen, 0, sizeof seen);
        for (size_t q = 0; q < len(kr); q++)
            for (uint32_t dc = s->rule[at(kr, q)].first_decl; dc; dc = s->decl[dc].next) {
                css_prop p = s->decl[dc].prop;
                if (p >= CSS_NPROPS || seen[p] || s->decl[dc].important || p == CSS_PROP_CUSTOM || css_anim_own(p)) continue;
                seen[p] = 1;
                if (win[p] && s->decl[win[p]].important) continue;
                css_anim_prop(s, &kr, p, 0, prog, data(ease), len(ease), &ak0[p], &ak1[p], &af[p]);
                aon[p] = 1;
            }
        /* its custom properties: a registered one's ends mixed by its syntax where they pair
           up, the rest discrete (the first value until halfway) */
        vector<uint32_t> cseen;
        for (size_t q = 0; q < len(kr); q++)
            for (uint32_t dc = s->rule[at(kr, q)].first_decl; dc; dc = s->decl[dc].next) {
                if (s->decl[dc].prop != CSS_PROP_CUSTOM || s->decl[dc].important) continue;
                uint32_t name = s->decl[dc].word;
                int done = 0;
                for (size_t z = 0; z < len(cseen) && !done; z++) done = at(cseen, z) == name;
                if (done) continue;
                push_back(cseen, name);
                css_anim_custom(s, &kr, name, prog, data(ease), len(ease), cust, pem, rem);
            }
    }
}

/* two colours mixed at f, premultiplied (CSS Color 4, 12.3) */
static uint32_t css_mix_rgba(uint32_t a, uint32_t b, double f) {
    double aa = (a & 0xff) / 255.0, ba = (b & 0xff) / 255.0, al = aa + (ba - aa) * f;
    if (al <= 0) return 0;
    if (al > 1) al = 1;
    uint32_t out = 0;
    for (int sh = 24; sh >= 8; sh -= 8) {
        double ca = (a >> sh & 0xff) * aa, cb = (b >> sh & 0xff) * ba, v = (ca + (cb - ca) * f) / al;
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5) << sh;
    }
    return out | (uint32_t)(al * 255 + 0.5);
}

/* a value's text broken into its tokens: numbers (with their units) and the rest
   (names, punctuation), each a token; white space between them kept */
typedef struct css_tok { size_t s, n, u; int num; double v; } css_tok;
static void css_tokens(const char *t, size_t n, vector<css_tok> *out) {
    size_t i = 0;
    while (i < n) {
        css_tok k;
        memset(&k, 0, sizeof k);
        k.s = i;
        double x;
        int start = i == 0 || !(css_namechar(t[i - 1]) || t[i - 1] == '#');
        size_t used = start && (css_is_digit(t[i]) || t[i] == '.' || t[i] == '-' || t[i] == '+') ? css_number(t + i, n - i, &x) : 0;
        if (used) {
            size_t e = i + used;
            while (e < n && (css_is_alpha(t[e]) || t[e] == '%')) e++;
            k.num = 1;
            k.v = x;
            k.u = i + used;
            k.n = e - i;
        } else if (css_namechar(t[i]) || t[i] == '#') {
            size_t e = i;
            while (e < n && (css_namechar(t[e]) || t[e] == '#')) e++;
            k.n = e - i;
        } else k.n = 1;
        push_back(out, k);
        i += k.n;
    }
}

/* an angle's degrees, by its unit (t its text, its unit from u) */
static int css_tok_angle(const char *t, const css_tok *k, double *deg) {
    const char *u = t + k->u;
    size_t un = k->s + k->n - k->u;
    if (css_eq_ci(u, un, "deg")) *deg = k->v;
    else if (css_eq_ci(u, un, "rad")) *deg = k->v * 180 / M_PI;
    else if (css_eq_ci(u, un, "turn")) *deg = k->v * 360;
    else if (css_eq_ci(u, un, "grad")) *deg = k->v * 0.9;
    else return 0;
    return 1;
}

/* two values kept as text (transforms, shadows, filters, ...) interpolated at f where
   their tokens pair up: the same names and punctuation, numbers with the same unit (angles
   in degrees, a unitless 0 as the other's unit); 1 when they did, the text in out */
static int css_interp_tokens(const char *a, size_t an, const char *b, size_t bn, double f, vector<char> *out) {
    vector<css_tok> ta, tb;
    css_tokens(a, an, &ta);
    css_tokens(b, bn, &tb);
    /* white space is not a token that must pair: the tokens with it dropped */
    vector<css_tok> xa, xb;
    for (size_t i = 0; i < len(ta); i++) if (!(at(ta, i).n == 1 && !at(ta, i).num && css_ws(a[at(ta, i).s]))) push_back(xa, at(ta, i));
    for (size_t i = 0; i < len(tb); i++) if (!(at(tb, i).n == 1 && !at(tb, i).num && css_ws(b[at(tb, i).s]))) push_back(xb, at(tb, i));
    if (len(xa) != len(xb)) return 0;
    for (size_t i = 0; i < len(xa); i++) {
        css_tok p = at(xa, i), q = at(xb, i);
        if (p.num != q.num) return 0;
        if (!p.num) {
            if (p.n != q.n || !css_bytes_eq(a + p.s, b + q.s, p.n, 1)) return 0;
            continue;
        }
        size_t pu = p.s + p.n - p.u, qu = q.s + q.n - q.u;
        double d1, d2;
        if (pu == qu && css_bytes_eq(a + p.u, b + q.u, pu, 1)) continue;
        if (css_tok_angle(a, &p, &d1) && css_tok_angle(b, &q, &d2)) continue;
        if ((pu == 0 && p.v == 0) || (qu == 0 && q.v == 0)) continue;
        return 0;
    }
    /* a's text, its numbers mixed */
    size_t j = 0;
    for (size_t i = 0; i < len(ta); i++) {
        css_tok p = at(ta, i);
        if (!p.num) { append_range(*out, a + p.s, p.n); continue; }
        while (j < len(xb) && !at(xb, j).num) j++;
        css_tok q = at(xb, j);
        j++;
        double d1, d2;
        char buf[64];
        int bn2;
        if (css_tok_angle(a, &p, &d1) && css_tok_angle(b, &q, &d2)) bn2 = snprintf(buf, sizeof buf, "%gdeg", d1 + (d2 - d1) * f);
        else {
            /* the unit: a's, or b's when a's number is a bare 0 */
            const char *u = a + p.u;
            size_t un = p.s + p.n - p.u;
            if (un == 0) { u = b + q.u; un = q.s + q.n - q.u; }
            bn2 = snprintf(buf, sizeof buf, "%g%.*s", p.v + (q.v - p.v) * f, (int)un, u);
        }
        if (bn2 > 0) append_range(*out, buf, (size_t)bn2);
    }
    return 1;
}

/* the identity of a transform's functions (CSS Transforms 1, 9): each function's values
   those that leave a box as it is, to stand for none beside them */
static void css_identity_fns(const char *t, size_t n, vector<char> *out) {
    size_t i = 0, pa, pl;
    while (css_next_part(t, n, &i, &pa, &pl)) {
        const char *fn = t + pa;
        size_t open = 0;
        while (open < pl && fn[open] != '(') open++;
        if (len(*out)) push_back(*out, ' ');
        append_range(*out, fn, open);
        push_back(*out, '(');
        size_t j = 0, xa, xl;
        int k = 0;
        const char *args = fn + open + 1;
        size_t al = open + 2 <= pl ? pl - open - 2 : 0;
        int scale = open >= 5 && css_bytes_eq(fn, "scale", 5, 1);
        int matrix = open >= 6 && css_bytes_eq(fn, "matrix", 6, 1), m3 = open == 8;
        while (css_next_part(args, al, &j, &xa, &xl)) {
            if (k) append_range(*out, ", ", 2);
            const char *v = "0";
            if (scale) v = "1";
            else if (matrix && !m3 && (k == 0 || k == 3)) v = "1";
            else if (matrix && m3 && k % 5 == 0) v = "1";
            else if (css_bytes_eq(fn, "rotate3d", 8, 1) && open == 8 && k < 3) {
                /* rotate3d's axis kept, its angle 0 */
                append_range(*out, args + xa, xl);
                k++;
                continue;
            }
            append_range(*out, v, strlen(v));
            k++;
        }
        push_back(*out, ')');
    }
}

/* two computed values of property p mixed at f (CSS Values 4, 3; Web Animations 1, 5.3):
   lengths, numbers and percentages along a line (a length and a percentage as their
   calc()); colours premultiplied; values kept as text token by token where they pair up
   (none the identity of a transform's functions); visibility visible while either end
   is; the rest discrete, the first value until halfway */
static void css_interp(css_style *s, css_prop p, const css_val *a, const css_val *b, double f, css_val *out) {
    if (f == 0) { *out = *a; return; }
    if (f == 1) { *out = *b; return; }
    css_unit ua = a->unit, ub = b->unit;
    int la = ua == CSS_UNIT_PX || ua == CSS_UNIT_PERCENT || ua == CSS_UNIT_CALC, lb = ub == CSS_UNIT_PX || ub == CSS_UNIT_PERCENT || ub == CSS_UNIT_CALC;
    if (ua == ub && (ua == CSS_UNIT_PX || ua == CSS_UNIT_NUMBER || ua == CSS_UNIT_PERCENT)) {
        *out = *a;
        out->num = a->num + (b->num - a->num) * f;
        if (css_pdefs[p].flags & F_INT) out->num = floor(out->num + 0.5);
        return;
    }
    if (la && lb) {
        double pxa = ua == CSS_UNIT_PERCENT ? 0 : a->num, pca = ua == CSS_UNIT_PERCENT ? a->num : ua == CSS_UNIT_CALC ? a->pct : 0;
        double pxb = ub == CSS_UNIT_PERCENT ? 0 : b->num, pcb = ub == CSS_UNIT_PERCENT ? b->num : ub == CSS_UNIT_CALC ? b->pct : 0;
        memset(out, 0, sizeof *out);
        out->unit = CSS_UNIT_CALC;
        out->num = pxa + (pxb - pxa) * f;
        out->pct = pca + (pcb - pca) * f;
        return;
    }
    if (ua == CSS_UNIT_COLOR && ub == CSS_UNIT_COLOR) {
        *out = *a;
        out->color = css_mix_rgba(a->color, b->color, f);
        return;
    }
    if (p == CSS_PROP_VISIBILITY && (css_is_word(a, "visible") || css_is_word(b, "visible"))) {
        *out = css_is_word(a, "visible") ? *a : *b;
        return;
    }
    if ((ua == CSS_UNIT_TEXT || css_is_word(a, "none")) && (ub == CSS_UNIT_TEXT || css_is_word(b, "none")) && css_pdefs[p].gram == G_TEXT) {
        vector<char> ta, tb, mixed;
        if (ua == CSS_UNIT_TEXT) append_range(ta, text_ptr(s, a->tx), a->tx.len);
        if (ub == CSS_UNIT_TEXT) append_range(tb, text_ptr(s, b->tx), b->tx.len);
        int fns = p == CSS_PROP_TRANSFORM || p == CSS_PROP_FILTER || p == CSS_PROP_BACKDROP_FILTER;
        if (ua != CSS_UNIT_TEXT && fns) css_identity_fns(data(tb), len(tb), &ta);
        if (ub != CSS_UNIT_TEXT && fns) css_identity_fns(data(ta), len(ta), &tb);
        if (len(ta) && len(tb) && css_interp_tokens(data(ta), len(ta), data(tb), len(tb), f, &mixed)) {
            memset(out, 0, sizeof *out);
            out->unit = CSS_UNIT_TEXT;
            out->tx = text_add(s, data(mixed), len(mixed));
            return;
        }
    }
    *out = f < 0.5 ? *a : *b;
}

/* ---- registered custom properties (@property, CSS Properties and Values 1) ---- */

static int css_absolutize(const char *t, size_t n, double em, double rem, uint32_t cur, int dark, vector<char> *out);

/* what a registered property's value is computed against: the element's em and rem, its
   colour, its scheme */
typedef struct css_senv { double em, rem; uint32_t cur; int dark; } css_senv;

static int css_is_relative_unit(const char *u, size_t un) {
    static const char *const units = "em rem ex rex ch rch cap rcap ic ric lh rlh vw vh vi vb vmin vmax svw svh lvw lvh dvw dvh cqw cqh cqi cqb cqmin cqmax";
    char w[8];
    if (un == 0 || un >= sizeof w) return 0;
    for (size_t k = 0; k < un; k++) w[k] = css_lower(u[k]);
    w[un] = 0;
    return css_word_in(w, units);
}

/* a value of one type of a registered property's syntax (its name, without its angle
   brackets) is part p: 1 when it is, its computed text then appended to out when out is
   given (lengths px, colours rgb(), angles deg, times s, numbers as numbers) */
static int css_syntax_type(const char *ty, size_t tn, const char *p, size_t pn, const css_senv *e, vector<char> *out) {
    css_val v;
    double x;
    char buf[96];
    int bn = 0;
    if (css_eq_ci(ty, tn, "length") || css_eq_ci(ty, tn, "length-percentage")) {
        int lp = tn > 6;
        if (!css_parse_val(p, pn, &v)) return 0;
        int ok = v.unit == CSS_UNIT_PX || v.unit == CSS_UNIT_EM || v.unit == CSS_UNIT_REM || (v.unit == CSS_UNIT_NUMBER && v.num == 0) ||
                 (v.unit == CSS_UNIT_CALC && (lp ? (v.ck & (CSS_CK_LEN | CSS_CK_PCT)) != 0 : v.ck == CSS_CK_LEN)) || (lp && v.unit == CSS_UNIT_PERCENT);
        if (!ok) return 0;
        if (!out) return 1;
        if (v.unit == CSS_UNIT_NUMBER) bn = snprintf(buf, sizeof buf, "0px");
        else if (v.unit == CSS_UNIT_PX) bn = snprintf(buf, sizeof buf, "%gpx", v.num);
        else if (v.unit == CSS_UNIT_EM) bn = snprintf(buf, sizeof buf, "%gpx", v.num * e->em);
        else if (v.unit == CSS_UNIT_REM) bn = snprintf(buf, sizeof buf, "%gpx", v.num * e->rem);
        else if (v.unit == CSS_UNIT_PERCENT) bn = snprintf(buf, sizeof buf, "%g%%", v.num);
        else { css_absolutize(p, pn, e->em, e->rem, e->cur, e->dark, out); return 1; }
        if (bn > 0) append_range(*out, buf, (size_t)bn);
        return 1;
    }
    if (css_eq_ci(ty, tn, "number") || css_eq_ci(ty, tn, "integer")) {
        if (css_number(p, pn, &x) != pn) return 0;
        if (tn == 7 && x != floor(x)) return 0;
        if (out) { bn = snprintf(buf, sizeof buf, "%g", x); if (bn > 0) append_range(*out, buf, (size_t)bn); }
        return 1;
    }
    if (css_eq_ci(ty, tn, "percentage")) {
        size_t used = css_number(p, pn, &x);
        if (!used || used + 1 != pn || p[used] != '%') return 0;
        if (out) { bn = snprintf(buf, sizeof buf, "%g%%", x); if (bn > 0) append_range(*out, buf, (size_t)bn); }
        return 1;
    }
    if (css_eq_ci(ty, tn, "color")) {
        if (!css_is_color(p, pn)) return 0;
        if (!out) return 1;
        /* currentcolor is its own computed value here (the element's colour is taken where it is used) */
        if (css_eq_ci(p, pn, "currentcolor")) { append_range(*out, "currentcolor", 12); return 1; }
        vector<char> t;
        uint32_t c = 0;
        css_absolutize(p, pn, e->em, e->rem, e->cur, e->dark, &t);
        if (!css_parse_color(data(t), len(t), &c)) { append_range(*out, p, pn); return 1; }
        memset(&v, 0, sizeof v);
        v.unit = CSS_UNIT_COLOR;
        v.color = c;
        size_t cn = css_value_text(&v, buf, sizeof buf);
        append_range(*out, buf, cn);
        return 1;
    }
    if (css_eq_ci(ty, tn, "angle")) {
        if (!css_is_angle(p, pn)) return 0;
        if (!out) return 1;
        size_t used = css_number(p, pn, &x);
        const char *u = p + used;
        size_t un = pn - used;
        if (css_eq_ci(u, un, "rad")) x = x * 180 / M_PI;
        else if (css_eq_ci(u, un, "turn")) x *= 360;
        else if (css_eq_ci(u, un, "grad")) x *= 0.9;
        bn = snprintf(buf, sizeof buf, "%gdeg", x);
        if (bn > 0) append_range(*out, buf, (size_t)bn);
        return 1;
    }
    if (css_eq_ci(ty, tn, "time")) {
        if (!css_time_ms(p, pn, &x)) return 0;
        if (out) { bn = snprintf(buf, sizeof buf, "%gs", x / 1000); if (bn > 0) append_range(*out, buf, (size_t)bn); }
        return 1;
    }
    int ok = 0;
    if (css_eq_ci(ty, tn, "resolution")) {
        size_t used = css_number(p, pn, &x);
        ok = used && used < pn && (css_eq_ci(p + used, pn - used, "dpi") || css_eq_ci(p + used, pn - used, "dpcm") ||
                                   css_eq_ci(p + used, pn - used, "dppx") || css_eq_ci(p + used, pn - used, "x"));
    } else if (css_eq_ci(ty, tn, "custom-ident")) {
        ok = css_is_ident(p, pn) && !css_eq_ci(p, pn, "default") && !css_eq_ci(p, pn, "initial") && !css_eq_ci(p, pn, "inherit") &&
             !css_eq_ci(p, pn, "unset") && !css_eq_ci(p, pn, "revert") && !css_eq_ci(p, pn, "revert-layer");
    } else if (css_eq_ci(ty, tn, "string")) ok = css_is_string(p, pn);
    else if (css_eq_ci(ty, tn, "url")) ok = css_is_fn(p, pn, "url");
    else if (css_eq_ci(ty, tn, "image")) ok = css_is_image(p, pn);
    else if (css_eq_ci(ty, tn, "transform-function")) {
        size_t i = 0, st, ln;
        int parts = 0;
        while (css_next_part(p, pn, &i, &st, &ln)) parts++;
        ok = parts == 1 && css_is_transform(p, pn);
    } else if (css_eq_ci(ty, tn, "transform-list")) ok = css_is_transform(p, pn);
    if (ok && out) css_absolutize(p, pn, e->em, e->rem, e->cur, e->dark, out);
    return ok;
}

/* a registered property's syntax (the text of its string) is valid: * alone, or its
   components (a type in angle brackets that this reads, with + or # after it but for
   transform-list, or an identifier) with | between them */
static int css_syntax_ok(const char *syn, size_t sn) {
    static const char *const types = "length number percentage length-percentage color image url integer angle time resolution "
                                     "transform-function transform-list custom-ident string";
    size_t a = css_skip_ws(syn, sn, 0), z = sn;
    while (z > a && css_ws(syn[z - 1])) z--;
    if (z - a == 1 && syn[a] == '*') return 1;
    while (a < z) {
        size_t b = a;
        while (b < z && syn[b] != '|') b++;
        size_t ca = css_skip_ws(syn, b, a), cb = b;
        while (cb > ca && css_ws(syn[cb - 1])) cb--;
        if (cb == ca) return 0;
        if (syn[cb - 1] == '+' || syn[cb - 1] == '#') cb--;
        if (syn[ca] == '<') {
            if (cb - ca < 3 || syn[cb - 1] != '>' || cb - ca - 2 >= 24) return 0;
            char w[24];
            for (size_t k = 0; k < cb - ca - 2; k++) w[k] = css_lower(syn[ca + 1 + k]);
            w[cb - ca - 2] = 0;
            if (!css_word_in(w, types)) return 0;
            if (strcmp(w, "transform-list") == 0 && cb < b && (syn[cb] == '+' || syn[cb] == '#')) return 0;
        } else if (!css_is_ident(syn + ca, cb - ca)) return 0;
        if (b >= z) break;
        a = b + 1;
    }
    return 1;
}

/* a registered property's value v (its var()s replaced) against its syntax syn: 1 when a
   component of it takes v (a type, its + list or its # list, or the identifier itself);
   the value computed into out when out is given; * takes any value, as it is */
static int css_syntax_match(const char *syn, size_t sn, const char *v, size_t vn, const css_senv *e, vector<char> *out) {
    size_t va = css_skip_ws(v, vn, 0), vz = vn;
    while (vz > va && css_ws(v[vz - 1])) vz--;
    const char *x = v + va;
    size_t xn = vz - va;
    size_t a = css_skip_ws(syn, sn, 0), z = sn;
    while (z > a && css_ws(syn[z - 1])) z--;
    if (z - a == 1 && syn[a] == '*') { if (out) append_range(*out, x, xn); return 1; }
    if (xn == 0) return 0;
    while (a < z) {
        size_t b = a;
        while (b < z && syn[b] != '|') b++;
        size_t ca = css_skip_ws(syn, b, a), cb = b;
        while (cb > ca && css_ws(syn[cb - 1])) cb--;
        char mult = cb > ca && (syn[cb - 1] == '+' || syn[cb - 1] == '#') ? syn[cb - 1] : 0;
        if (mult) cb--;
        vector<char> got;
        int ok = 0;
        if (cb > ca && syn[ca] == '<' && syn[cb - 1] == '>') {
            const char *ty = syn + ca + 1;
            size_t tn = cb - ca - 2;
            if (!mult) ok = css_syntax_type(ty, tn, x, xn, e, out ? &got : 0);
            else if (mult == '+') {
                size_t st[CSS_MAXPARTS], ln[CSS_MAXPARTS];
                size_t k = css_parts(x, xn, st, ln, CSS_MAXPARTS);
                ok = k > 0;
                for (size_t j = 0; j < k && ok; j++) {
                    if (j && out) push_back(got, ' ');
                    ok = css_syntax_type(ty, tn, x + st[j], ln[j], e, out ? &got : 0);
                }
            } else {
                size_t p = 0;
                ok = 1;
                int first = 1;
                while (p <= xn && ok) {
                    size_t q = css_comma(x, xn, p), pa = css_skip_ws(x, q, p), pb = q;
                    while (pb > pa && css_ws(x[pb - 1])) pb--;
                    if (!first && out) append_range(got, ", ", 2);
                    first = 0;
                    ok = pb > pa && css_syntax_type(ty, tn, x + pa, pb - pa, e, out ? &got : 0);
                    if (q >= xn) break;
                    p = q + 1;
                }
            }
        } else if (!mult && cb - ca == xn && memcmp(syn + ca, x, xn) == 0) {
            ok = 1;
            if (out) append_range(got, x, xn);
        }
        if (ok) {
            if (out) append_range(*out, data(got), len(got));
            return 1;
        }
        if (b >= z) break;
        a = b + 1;
    }
    return 0;
}

/* the registration of a custom property (its name's atom) that holds: the last of those
   for it; 0 when it is not registered */
static uint32_t css_regprop_of(const css_style *s, uint32_t name) {
    uint32_t best = 0;
    for (uint32_t r = 1; name && r < s->regprop_len; r++)
        if (s->regprop[r].live && s->regprop[r].name == name) best = r;
    return best;
}

/* an @property rule (CSS Properties and Values 1, 3): its name (a custom property's) and
   its descriptors: syntax (a string this reads), inherits (true or false), initial-value
   (which the syntax must take, computationally independent: no font- or viewport-relative
   length, no var(), no currentcolor; it may be left out for *). A rule without them, or
   with one this does not take, is dropped */
static void css_property_rule(css_style *s, const css_pctx *cx, const char *pre, size_t pn, const char *blk, size_t bn) {
    size_t a = css_skip_ws(pre, pn, 0), b = pn;
    while (b > a && css_ws(pre[b - 1])) b--;
    if (b - a < 3 || pre[a] != '-' || pre[a + 1] != '-' || !css_is_ident(pre + a, b - a)) return;
    long sy0 = -1, sy1 = -1, iv0 = -1, iv1 = -1;
    int inh = -1;
    size_t i = 0;
    while (i < bn) {
        /* a descriptor: its name, then its value up to the next semicolon outside a string */
        size_t e = i;
        while (e < bn && blk[e] != ';') {
            if (blk[e] == '"' || blk[e] == '\'') { e = css_string_end(blk, bn, e); if (e >= bn) break; }
            else if (blk[e] == '(') { e = css_block_end(blk, bn, e, '(', ')'); if (e >= bn) break; }
            e++;
        }
        if (e > bn) e = bn;
        size_t colon = i;
        while (colon < e && blk[colon] != ':') colon++;
        if (colon < e) {
            size_t na = css_skip_ws(blk, colon, i), nb = colon;
            while (nb > na && css_ws(blk[nb - 1])) nb--;
            size_t va = css_skip_ws(blk, e, colon + 1), vb = e;
            while (vb > va && css_ws(blk[vb - 1])) vb--;
            if (css_eq_ci(blk + na, nb - na, "syntax") && vb - va >= 2 && css_is_string(blk + va, vb - va)) { sy0 = (long)va + 1; sy1 = (long)vb - 1; }
            else if (css_eq_ci(blk + na, nb - na, "inherits")) inh = css_eq_ci(blk + va, vb - va, "true") ? 1 : css_eq_ci(blk + va, vb - va, "false") ? 0 : -2;
            else if (css_eq_ci(blk + na, nb - na, "initial-value")) { iv0 = (long)va; iv1 = (long)vb; }
        }
        i = e + 1;
    }
    if (sy0 < 0 || inh < 0 || !css_syntax_ok(blk + sy0, (size_t)(sy1 - sy0))) return;
    const char *syn = blk + sy0;
    size_t sn = (size_t)(sy1 - sy0);
    int universal = sn == 1 && syn[0] == '*';
    vector<char> init;
    if (iv0 < 0) { if (!universal) return; }
    else {
        const char *iv = blk + iv0;
        size_t ivn = (size_t)(iv1 - iv0);
        /* computationally independent: no relative length, var(), env() or currentcolor */
        vector<css_tok> tk;
        css_tokens(iv, ivn, &tk);
        for (size_t k = 0; k < len(tk); k++) {
            css_tok t = at(tk, k);
            if (t.num && css_is_relative_unit(iv + t.u, t.s + t.n - t.u)) return;
            if (!t.num && (css_eq_ci(iv + t.s, t.n, "var") || css_eq_ci(iv + t.s, t.n, "env") || css_eq_ci(iv + t.s, t.n, "currentcolor"))) return;
        }
        css_senv e0 = { 16, 16, 0x000000ff, 0 };
        if (!css_syntax_match(syn, sn, iv, ivn, &e0, &init)) return;
    }
    css_regprop_h h = regprop_new(s);
    s->regprop[h.index].name = names_intern(s, pre + a, b - a);
    s->regprop[h.index].syntax = text_add(s, syn, sn);
    s->regprop[h.index].inherits = inh == 1;
    s->regprop[h.index].initial = text_add(s, data(init), len(init));
    s->regprop[h.index].sheet = cx->sheet;
}

/* custom property name's value at progress x of an animation (its keyframes kr): its two
   ends (a keyframe's declaration, its var()s replaced, or the element's own value) mixed
   at the eased progress when it is registered and its syntax takes both (computed
   against the parent's font size: an em in an end is the parent's), else the first until
   halfway; how animated, the keyframe its winner. An important declaration of it keeps it */
static void css_anim_custom(css_style *s, const vector<uint32_t> *kr, uint32_t name, double x, const char *ease, size_t en,
                            vector<css_cust> *cust, double pem, double rem) {
    size_t q = 0;
    while (q < len(*cust) && at(*cust, q).name != name) q++;
    if (q < len(*cust) && at(*cust, q).winner && s->decl[at(*cust, q).winner].important) return;
    uint32_t k0, k1;
    double f;
    css_anim_prop(s, kr, CSS_PROP_CUSTOM, name, x, ease, en, &k0, &k1, &f);
    vector<char> t0, t1;
    for (int e = 0; e < 2; e++) {
        uint32_t k = e ? k1 : k0;
        vector<char> *dst = e ? &t1 : &t0;
        if (k) {
            vector<char> src;
            amb_span sp = s->decl[k].text;
            append_range(src, text_ptr(s, sp), sp.len);
            if (!css_subst(s, data(src), len(src), cust, 0, dst)) clear(*dst);
        } else if (q < len(*cust) && at(*cust, q).state == 2) {
            amb_span sp = at(*cust, q).value;
            append_range(*dst, text_ptr(s, sp), sp.len);
        }
    }
    vector<char> res;
    uint32_t r = css_regprop_of(s, name);
    int mixed = 0;
    if (r && f > 0 && f < 1) {
        vector<char> sy, c0, c1;
        append_range(sy, text_ptr(s, s->regprop[r].syntax), s->regprop[r].syntax.len);
        css_senv e0 = { pem, rem, 0x000000ff, 0 };
        if (css_syntax_match(data(sy), len(sy), data(t0), len(t0), &e0, &c0) && css_syntax_match(data(sy), len(sy), data(t1), len(t1), &e0, &c1))
            mixed = css_interp_tokens(data(c0), len(c0), data(c1), len(c1), f, &res);
    }
    if (!mixed) {
        if (f < 0.5) append_range(res, data(t0), len(t0));
        else append_range(res, data(t1), len(t1));
    }
    css_cust c;
    memset(&c, 0, sizeof c);
    c.name = name;
    c.value = text_add(s, data(res), len(res));
    c.how = CSS_HOW_ANIMATED;
    c.winner = f >= 0.5 ? (k1 ? k1 : k0) : (k0 ? k0 : k1);
    c.state = len(res) ? 2 : 3;
    if (q < len(*cust)) (*cust)[q] = c;
    else push_back(*cust, c);
}

/* an element's registered custom properties (cust) as the cascade leaves them (CSS
   Properties and Values 1, 2.4): one of its own (declared, or an animation's) whose
   syntax does not take its value is invalid at computed-value time, the property then
   unset; one the element has none of, or an unset one, its parent's when it inherits,
   else its initial value; initial its initial value. The parent's are pf to pf + pn of
   the styles' pool */
static void css_register_customs(css_style *s, vector<css_cust> *cust, const css_styles *ss, uint32_t pf, uint32_t pn) {
    for (uint32_t r = 1; r < s->regprop_len; r++) {
        if (!s->regprop[r].live) continue;
        uint32_t name = s->regprop[r].name;
        if (css_regprop_of(s, name) != r) continue;
        size_t q = 0;
        while (q < len(*cust) && at(*cust, q).name != name) q++;
        int here = q < len(*cust), own = here && at(*cust, q).how != CSS_HOW_INHERITED;
        uint32_t w = here ? at(*cust, q).winner : 0;
        if (own && at(*cust, q).state == 2) {
            vector<char> v, sy;
            amb_span sp = at(*cust, q).value;
            append_range(v, text_ptr(s, sp), sp.len);
            append_range(sy, text_ptr(s, s->regprop[r].syntax), s->regprop[r].syntax.len);
            css_senv e0 = { 16, 16, 0x000000ff, 0 };
            if (css_syntax_match(data(sy), len(sy), data(v), len(v), &e0, 0)) continue;   /* computed after the font size */
        }
        if (here && !own && at(*cust, q).state == 2) continue;                              /* the parent's, inherited */
        int kw_initial = w && css_eq_ci(text_ptr(s, s->decl[w].text), s->decl[w].text.len, "initial");
        css_cust c;
        memset(&c, 0, sizeof c);
        c.name = name;
        c.state = 2;
        c.winner = w;
        c.how = CSS_HOW_INITIAL;
        c.value = s->regprop[r].initial;
        if (s->regprop[r].inherits && !kw_initial)
            for (uint32_t z = 0; z < pn; z++)
                if (at(ss->cpool, pf + z).name == name) { c.value = at(ss->cpool, pf + z).value; c.how = CSS_HOW_INHERITED; }
        if (here) (*cust)[q] = c;
        else push_back(*cust, c);
    }
}

/* an element's registered custom properties of its own (declared or animated) computed
   by their syntax, against its font size, the root's and its colour */
static void css_register_compute(css_style *s, vector<css_cust> *cust, const css_senv *e) {
    for (size_t q = 0; q < len(*cust); q++) {
        css_cust c = at(*cust, q);
        if (c.state != 2 || c.how == CSS_HOW_INHERITED || c.how == CSS_HOW_INITIAL) continue;
        uint32_t r = css_regprop_of(s, c.name);
        if (!r) continue;
        vector<char> v, sy, out;
        append_range(v, text_ptr(s, c.value), c.value.len);
        append_range(sy, text_ptr(s, s->regprop[r].syntax), s->regprop[r].syntax.len);
        if (css_syntax_match(data(sy), len(sy), data(v), len(v), e, &out)) (*cust)[q].value = text_add(s, data(out), len(out));
    }
}

/* an element's computed values from its matched rules' declarations under the parent
   style ps (0 at the root, whose parent values are the initial ones): its template, then
   each declaration that won made computed, and what those change of the values that hang
   on them (currentcolor, blockified display, border widths under their styles). Written
   into out and rec, its custom properties into cust */
static void css_element(css_style *s, const css_env *env, const vector<uint32_t> *matched, css_styles *ss, uint32_t ps,
                        const css_val *initial, const css_rec *irec, int is_root, css_val *out, css_rec *rec, vector<css_cust> *cust,
                        vector<css_vtext> *vc, uint8_t *fso) {
    uint32_t win[CSS_NPROPS];
    uint64_t best[CSS_NPROPS];
    memset(win, 0, sizeof win);
    memset(best, 0, sizeof best);
    uint32_t ti = css_template(s, ss, ps, initial, irec, vc);
    size_t tb = (size_t)(ti - 1) * CSS_NPROPS;
    memcpy(out, data(ss->tv) + tb, sizeof(css_val) * CSS_NPROPS);
    memcpy(rec, data(ss->tr) + tb, sizeof(css_rec) * CSS_NPROPS);
    /* the parent's custom properties, then the winners of this element's own */
    clear(*cust);
    uint32_t pf = ps ? at(ss->cfirst, ps) : 0, pn = ps ? at(ss->ccount, ps) : 0;
    vector<uint64_t> crank;
    for (uint32_t q = 0; q < pn; q++) {
        css_cust c = at(ss->cpool, pf + q);
        uint32_t rg = s->regprop_len > 1 ? css_regprop_of(s, c.name) : 0;
        if (rg && !s->regprop[rg].inherits) continue;
        c.how = CSS_HOW_INHERITED;
        c.winner = 0;
        push_back(cust, c);
        push_back(crank, 0);
    }
    int any = 0;
    /* a declaration's rank, and after its specificity its rule's scope proximity (Cascade 6,
       6.1: a nearer scoping root over a farther, a scoped rule over an unscoped one), then
       its order: rk's high 40 bits, the proximity's key, rk's order */
    uint32_t bestx[CSS_NPROPS];
    memset(bestx, 0, sizeof bestx);
    for (size_t k = 0; k < len(*matched); k++) {
        uint32_t r = at(*matched, k) & 0xffffff, px = at(*matched, k) >> 24;
        for (uint32_t dc = s->rule[r].first_decl; dc; dc = s->decl[dc].next) {
            int p = (int)s->decl[dc].prop;
            uint64_t rk = css_rank(s, dc);
            if (p != CSS_PROP_CUSTOM) {
                uint64_t hi = rk >> 24, bhi = best[p] >> 24;
                uint32_t lo = px << 24 | (uint32_t)(rk & 0xffffff), blo = bestx[p] << 24 | (uint32_t)(best[p] & 0xffffff);
                if (!win[p] || hi > bhi || (hi == bhi && lo >= blo)) { best[p] = rk; bestx[p] = px; win[p] = dc; any = 1; }
                continue;
            }
            uint32_t name = s->decl[dc].word;
            size_t q = 0;
            while (q < len(*cust) && at(*cust, q).name != name) q++;
            if (q == len(*cust)) {
                css_cust c;
                memset(&c, 0, sizeof c);
                c.name = name;
                c.state = 3;
                push_back(cust, c);
                push_back(crank, 0);
            }
            if (at(crank, q) > rk) continue;
            crank[q] = rk;
            css_cust c = at(*cust, q);
            c.winner = dc;
            c.how = CSS_HOW_DECLARED;
            c.value = s->decl[dc].text;
            c.state = 0;
            c.cyc = 0;
            (*cust)[q] = c;
        }
    }
    /* the CSS-wide keywords of a custom property: initial makes it invalid; inherit,
       unset and revert take the parent's */
    for (size_t q = 0; q < len(*cust); q++) {
        css_cust c = at(*cust, q);
        if (!c.winner) continue;
        css_val g;
        int global = c.value.len < 16 && css_parse_val(text_ptr(s, c.value), c.value.len, &g) && css_is_global(&g);
        if (!global) continue;
        int found = 0;
        for (uint32_t z = 0; z < pn && !found; z++)
            if (at(ss->cpool, pf + z).name == c.name && !css_is_word(&g, "initial")) {
                c.value = at(ss->cpool, pf + z).value;
                c.state = 2;
                c.how = CSS_HOW_INHERITED;
                found = 1;
            }
        if (!found) { c.state = 3; c.how = CSS_HOW_INITIAL; c.value = text_add(s, "", 0); }
        (*cust)[q] = c;
    }
    for (uint32_t q = 0; q < (uint32_t)len(*cust); q++) if (at(*cust, q).state == 0) css_resolve_custom(s, cust, q, 0);
    if (s->regprop_len > 1) css_register_customs(s, cust, ss, pf, pn);
    /* the parent's font size's origin: an element that declares nothing has its size and
       family, and so its origin */
    uint8_t pfso = ps && ps < len(ss->fsx) ? at(ss->fsx, ps) : (uint8_t)CSS_FS_MEDIUM;
    *fso = pfso;
    if (!any && !is_root) return;
    /* the properties a declaration won, in the order they depend on each other; the
       parent's values (the initial ones, at the root) read from its cvals */
    uint32_t pc0 = ps ? at(ss->first, ps) : 0;
    css_par pv;
    css_par_of(s, pc0, initial, &pv);
    /* the element's animations at the page's rest time: the values they give, over every
       declaration but the important ones */
    uint32_t ak0[CSS_NPROPS], ak1[CSS_NPROPS];
    double af[CSS_NPROPS];
    uint8_t aon[CSS_NPROPS];
    memset(aon, 0, sizeof aon);
    if (win[CSS_PROP_ANIMATION_NAME]) {
        css_val pfs;
        css_val_of(s, pc0, CSS_PROP_FONT_SIZE, initial, &pfs);
        css_animate(s, win, cust, pc0, initial, is_root, pfs.num, env->rem, ak0, ak1, af, aon);
    }
    for (int oi = 0; oi < CSS_NPROPS; oi++) {
        css_prop p = (css_prop)css_order[oi];
        uint32_t w = win[p];
        if (oi > 0 && css_order[oi - 1] == CSS_PROP_COLOR && s->regprop_len > 1) {
            /* the registered custom properties, now that the font size and colour are known */
            css_senv se = { out[CSS_PROP_FONT_SIZE].num, env->rem, out[CSS_PROP_COLOR].color, out[CSS_PROP_COLOR_SCHEME].color == CSS_CANVAS_DARK };
            css_register_compute(s, cust, &se);
        }
        if (aon[p]) {
            /* the values at the two keyframes (the element's own where one gives none), mixed */
            css_val ends[2], v;
            for (int e = 0; e < 2; e++) {
                uint32_t k = e ? ak1[p] : ak0[p], src = k ? k : w;
                if (p == CSS_PROP_FONT_SIZE) {
                    css_font_size(s, env, src, win[CSS_PROP_FONT_FAMILY], cust, pc0, initial, &pv, pfso, is_root, out, rec, vc, fso);
                    ends[e] = out[p];
                } else if (src) css_decl_computed(s, env, p, src, cust, pc0, initial, &pv, out, is_root, &ends[e]);
                else ends[e] = out[p];
            }
            css_interp(s, p, &ends[0], &ends[1], af[p], &v);
            uint32_t kw = af[p] >= 0.5 ? (ak1[p] ? ak1[p] : ak0[p]) : (ak0[p] ? ak0[p] : ak1[p]);
            win[p] = kw;
            out[p] = v;
            rec[p] = css_value_rec(s, &v, CSS_HOW_ANIMATED, kw, vc);
            continue;
        }
        if (p == CSS_PROP_FONT_SIZE) { css_font_size(s, env, w, win[CSS_PROP_FONT_FAMILY], cust, pc0, initial, &pv, pfso, is_root, out, rec, vc, fso); continue; }
        if (!w) continue;
        css_val v;
        css_how how = css_decl_computed(s, env, p, w, cust, pc0, initial, &pv, out, is_root, &v);
        out[p] = v;
        rec[p] = css_value_rec(s, &v, how, w, vc);
    }
    /* the root's colour no declaration gave: color's initial value, CanvasText, in the
       scheme the root is used in (CSS Color 4) -- white in the dark */
    int root_text = is_root && !win[CSS_PROP_COLOR] && out[CSS_PROP_COLOR_SCHEME].color == CSS_CANVAS_DARK;
    if (root_text) {
        out[CSS_PROP_COLOR].color = 0xffffffff;
        rec[CSS_PROP_COLOR] = css_value_rec(s, &out[CSS_PROP_COLOR], CSS_HOW_INITIAL, 0, vc);
    }
    /* what the winners change of the values that hang on them */
    if (win[CSS_PROP_COLOR] || root_text)
        for (int k = 0; k < CSS_NPROPS; k++)
            if ((css_pf[k] & CSS_PF_CC) && !win[k] && (!(css_pf[k] & CSS_PF_INH) || strcmp(out[k].word, "currentcolor") == 0)) {
                out[k].color = out[CSS_PROP_COLOR].color;
                rec[k] = css_value_rec(s, &out[k], (css_pf[k] & CSS_PF_INH) && rec[k].how == CSS_HOW_INHERITED ? CSS_HOW_INHERITED : CSS_HOW_INITIAL, 0, vc);
            }
    if (win[CSS_PROP_POSITION] && (css_word_is(&out[CSS_PROP_POSITION], "absolute") || css_word_is(&out[CSS_PROP_POSITION], "fixed")) &&
        !css_word_is(&out[CSS_PROP_FLOAT], "none")) {
        css_word_val(&out[CSS_PROP_FLOAT], "none");
        rec[CSS_PROP_FLOAT] = css_value_rec(s, &out[CSS_PROP_FLOAT], rec[CSS_PROP_FLOAT].how, win[CSS_PROP_FLOAT], vc);
    }
    /* -webkit-box-orient's logical keywords computed as Chromium computes them: block-axis vertical,
       inline-axis horizontal */
    if (win[CSS_PROP_WEBKIT_BOX_ORIENT] && (css_word_is(&out[CSS_PROP_WEBKIT_BOX_ORIENT], "block-axis") ||
                                            css_word_is(&out[CSS_PROP_WEBKIT_BOX_ORIENT], "inline-axis"))) {
        css_word_val(&out[CSS_PROP_WEBKIT_BOX_ORIENT], css_word_is(&out[CSS_PROP_WEBKIT_BOX_ORIENT], "block-axis") ? "vertical" : "horizontal");
        rec[CSS_PROP_WEBKIT_BOX_ORIENT] = css_value_rec(s, &out[CSS_PROP_WEBKIT_BOX_ORIENT], rec[CSS_PROP_WEBKIT_BOX_ORIENT].how,
                                                        win[CSS_PROP_WEBKIT_BOX_ORIENT], vc);
    }
    css_val pdisplay;
    css_val_of(s, pc0, CSS_PROP_DISPLAY, initial, &pdisplay);
    if (win[CSS_PROP_DISPLAY] || win[CSS_PROP_FLOAT] || win[CSS_PROP_POSITION])
        if (css_display_fix(&out[CSS_PROP_DISPLAY], out, &pdisplay, is_root))
            rec[CSS_PROP_DISPLAY] = css_value_rec(s, &out[CSS_PROP_DISPLAY], rec[CSS_PROP_DISPLAY].how, win[CSS_PROP_DISPLAY], vc);
    for (int k = 0; k < 4; k++) {
        int wp = CSS_PROP_BORDER_TOP_WIDTH + k;
        if (win[wp] || !win[wp + 4]) continue;
        /* a style a declaration gave, over a width none did: the initial width (medium) under it */
        css_val v = initial[wp];
        css_snap_width(&v, &out[wp + 4], 1);
        out[wp] = v;
        rec[wp] = css_value_rec(s, &v, CSS_HOW_INITIAL, 0, vc);
    }
}

/* ---- the computed styles kept (PLAN.md §11) ----
   An element's values are a function of its matched rules, in order, and its parent's
   values alone; so elements with the same matched rules under parents of the same style
   share one style, computed once. A style is a number from 1: its 29 values and what
   they record. An element's own rules (its hints, its style attribute) are its alone,
   so an element that has them never shares. */
typedef struct css_skey { uint32_t hash, parent, list, n, style; } css_skey;

static uint32_t css_style_find(const vector<css_skey> *se, const vector<uint32_t> *slots, const vector<uint32_t> *lists,
                               const vector<uint32_t> *matched, uint32_t parent, uint32_t h) {
    size_t mask = len(*slots) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t e = at(*slots, i);
        if (!e) return 0;
        css_skey k = at(*se, e - 1);
        if (k.hash != h || k.parent != parent || k.n != (uint32_t)len(*matched)) continue;
        int same = 1;
        for (uint32_t q = 0; q < k.n && same; q++) if (at(*lists, k.list + q) != at(*matched, q)) same = 0;
        if (same) return k.style;
    }
}

static void css_style_add(vector<css_skey> *se, vector<uint32_t> *slots, vector<uint32_t> *lists, const vector<uint32_t> *matched,
                          uint32_t parent, uint32_t h, uint32_t style) {
    css_skey k = { h, parent, (uint32_t)len(*lists), (uint32_t)len(*matched), style };
    for (size_t q = 0; q < len(*matched); q++) push_back(lists, at(*matched, q));
    push_back(se, k);
    size_t mask = len(*slots) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask)
        if (!at(*slots, i)) { (*slots)[i] = (uint32_t)len(*se); return; }
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
    if (!d->node[x].first_attr) return;             /* no attribute, no hint */
    if (d->node[x].ns != DOM_NAMESPACE_HTML) return;  /* the hints are HTML elements' */
    for (size_t h = 0; h < sizeof css_hints / sizeof css_hints[0]; h++) {
        uint32_t at = css_attr_of(d, x, css_hints[h].attr, strlen(css_hints[h].attr));
        if (!at) continue;
        if (!css_word_in(names_ptr(d, d->node[x].name), css_hints[h].elements)) continue;
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
            for (int side = 0; side < 4; side++) css_add_decl(s, r, (css_prop)(CSS_PROP_BORDER_TOP_WIDTH + side), &v, 0, text, (size_t)tn, 0);
        else css_add_decl(s, r, css_hints[h].prop, &v, 0, text, (size_t)tn, 0);
        push_back(matched, r);
    }
    /* dir (HTML 15.3.6), a hint as Chromium maps it: ltr and rtl its direction; and every
       dir but a bdi's, a bdo's and an output's isolates (auto a pre's or a textarea's
       plaintext) */
    uint32_t da = css_attr_of(d, x, "dir", 3);
    if (!da) return;
    amb_span dv = d->attr[da].value;
    int ltr = css_eq_ci(text_ptr(d, dv), dv.len, "ltr"), rtl = css_eq_ci(text_ptr(d, dv), dv.len, "rtl");
    int autod = css_eq_ci(text_ptr(d, dv), dv.len, "auto");
    if (!ltr && !rtl && !autod) return;
    uint32_t r = css_own_rule(s, sheet, CSS_ORIGIN_AUTHOR, "the dir attribute", 17, 1);
    css_val v;
    const char *text = rtl ? "dir=rtl" : ltr ? "dir=ltr" : "dir=auto";
    if (ltr || rtl) {
        css_word_val(&v, rtl ? "rtl" : "ltr");
        css_add_decl(s, r, CSS_PROP_DIRECTION, &v, 0, text, strlen(text), 0);
    }
    if (!css_word_in(names_ptr(d, d->node[x].name), "bdi bdo output")) {
        css_word_val(&v, autod && css_word_in(names_ptr(d, d->node[x].name), "pre textarea") ? "plaintext" : "isolate");
        css_add_decl(s, r, CSS_PROP_UNICODE_BIDI, &v, 0, text, strlen(text), 0);
    }
    push_back(matched, r);
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

/* ---- the user agent's sheet, read once a process ----
   Its rules and declarations are every page's: read from its text at the first cascade,
   kept in these tables, and each later page's records made from them, not read again */
#define CSS_UA_RULES 320
#define CSS_UA_DECLS 768
#define CSS_UA_TEXT 24576
typedef struct css_uarule { uint32_t sel, sel_len, spec, first, n; } css_uarule;
typedef struct css_uadecl { uint32_t prop, unit, important, color, text, text_len, word, word_len; double num; } css_uadecl;
static css_uarule css_ua_rules[CSS_UA_RULES];
static css_uadecl css_ua_decls[CSS_UA_DECLS];
static char css_ua_text[CSS_UA_TEXT];
static uint32_t css_ua_nrules, css_ua_ndecls, css_ua_ntext;
static int css_ua_state;        /* 0 not read, 1 kept, 2 read each time (too large to keep) */

static uint32_t css_ua_keep(const char *t, size_t n) {
    if (css_ua_ntext + n > CSS_UA_TEXT) { css_ua_state = 2; return 0; }
    memcpy(css_ua_text + css_ua_ntext, t, n);
    uint32_t off = css_ua_ntext;
    css_ua_ntext += (uint32_t)n;
    return off;
}

/* the user agent's sheet, the store's first: made from the tables, or read (and kept) */
static void css_ua_load(css_style *s) {
    if (css_ua_state != 1 || s->rule_len > 1) {
        css_parse_sheet(s, CSS_ORIGIN_USER_AGENT, css_ua_sheet, sizeof css_ua_sheet - 1);
        if (css_ua_state != 0 || s->sheet_len != 2) return;
        /* the first read: its records kept */
        css_ua_state = 1;
        for (uint32_t r = 1; r < s->rule_len && css_ua_state == 1; r++) {
            if (css_ua_nrules == CSS_UA_RULES) { css_ua_state = 2; break; }
            css_uarule ur;
            amb_span sel = s->rule[r].selector;
            ur.sel = css_ua_keep(text_ptr(s, sel), sel.len);
            ur.sel_len = sel.len;
            ur.spec = s->rule[r].spec;
            ur.first = css_ua_ndecls;
            ur.n = 0;
            for (uint32_t dc = s->rule[r].first_decl; dc && css_ua_state == 1; dc = s->decl[dc].next) {
                if (css_ua_ndecls == CSS_UA_DECLS) { css_ua_state = 2; break; }
                css_uadecl ud;
                amb_span tx = s->decl[dc].text;
                ud.prop = (uint32_t)s->decl[dc].prop;
                ud.unit = (uint32_t)s->decl[dc].unit;
                ud.important = s->decl[dc].important;
                ud.color = s->decl[dc].color;
                ud.num = s->decl[dc].num;
                ud.text = css_ua_keep(text_ptr(s, tx), tx.len);
                ud.text_len = tx.len;
                uint32_t w = s->decl[dc].word;
                ud.word_len = w ? (uint32_t)names_len(s, w) : 0;
                ud.word = w ? css_ua_keep(names_ptr(s, w), ud.word_len) : 0;
                css_ua_decls[css_ua_ndecls++] = ud;
                ur.n++;
            }
            css_ua_rules[css_ua_nrules++] = ur;
        }
        return;
    }
    css_sheet_h sh = sheet_new(s);
    s->sheet[sh.index].origin = CSS_ORIGIN_USER_AGENT;
    for (uint32_t k = 0; k < css_ua_nrules; k++) {
        css_uarule ur = css_ua_rules[k];
        amb_span sp = text_add(s, css_ua_text + ur.sel, ur.sel_len);
        css_rule_h r = rule_new(s);
        s->rule[r.index].selector = sp;
        s->rule[r.index].spec = ur.spec;
        s->rule[r.index].order = r.index;
        rules_append(s, sh.index, r.index);
        for (uint32_t q = ur.first; q < ur.first + ur.n; q++) {
            css_uadecl ud = css_ua_decls[q];
            amb_span t = text_add(s, css_ua_text + ud.text, ud.text_len);
            uint32_t w = ud.word_len ? names_intern(s, css_ua_text + ud.word, ud.word_len) : 0;
            css_decl_h h = decl_new(s);
            s->decl[h.index].prop = (css_prop)ud.prop;
            s->decl[h.index].important = (uint8_t)ud.important;
            s->decl[h.index].unit = (css_unit)ud.unit;
            s->decl[h.index].num = ud.num;
            s->decl[h.index].color = ud.color;
            s->decl[h.index].word = w;
            s->decl[h.index].text = t;
            decls_append(s, r.index, h.index);
        }
    }
    s->revision++;
}

/* the style of an element (or a pseudo-element) whose matched rules are matched, under
   the parent style ps: the one those rules under ps made before, or a new one, its
   values recorded once (PLAN.md 11) */
static uint32_t css_style_make(css_style *s, css_env *env, css_styles *ss, vector<css_skey> *se, vector<uint32_t> *sslots,
                               vector<uint32_t> *slists, const vector<uint32_t> *matched, uint32_t ps, int is_root, const css_val *initial,
                               const css_rec *irec, vector<css_cust> *cust, vector<css_vtext> *vc) {
    uint32_t sh = 2166136261u ^ ps;
    for (size_t k = 0; k < len(*matched); k++) { sh ^= at(*matched, k); sh *= 16777619u; }
    sh ^= (uint32_t)len(*matched) * 0x9E3779B9u;
    uint32_t sid = css_style_find(se, sslots, slists, matched, ps, sh);
    if (!sid) {
        css_val out[CSS_NPROPS];
        css_rec rec[CSS_NPROPS];
        uint8_t fso = 0;
        css_element(s, env, matched, ss, ps, initial, irec, is_root, out, rec, cust, vc, &fso);
        if (is_root) env->rem = out[CSS_PROP_FONT_SIZE].num;      /* the root's size: the rem of the rest */
        sid = (uint32_t)len(ss->first);
        push_back(ss->cfirst, (uint32_t)len(ss->cpool));
        uint32_t nc = 0;
        for (size_t q = 0; q < len(*cust); q++) {
            css_cust c = at(*cust, q);
            if (c.state != 2) continue;                          /* an invalid one is no property of the style's */
            push_back(ss->cpool, c);
            nc++;
        }
        push_back(ss->ccount, nc);
        css_style_add(se, sslots, slists, matched, ps, sh, sid);
        /* the new style's values, recorded once: enum(prop)'s order, then its custom
           properties (those its own declarations gave, invalid ones too) */
        uint32_t first = 0;
        for (int k = 0; k < CSS_NPROPS; k++) {
            css_cval_h ch = css_put(s, sid, (css_prop)k, &out[k], rec[k]);
            if (k == 0) first = ch.index;
        }
        for (size_t q = 0; q < len(*cust); q++) {
            css_cust c = at(*cust, q);
            if (c.state != 2 && !c.winner) continue;
            css_val cv;
            memset(&cv, 0, sizeof cv);
            cv.unit = CSS_UNIT_TEXT;
            cv.tx = c.value;
            css_rec cr;
            cr.word = c.name;
            cr.text = c.value;
            cr.how = c.how;
            cr.winner = c.winner;
            css_put(s, sid, CSS_PROP_CUSTOM, &cv, cr);
        }
        push_back(ss->first, first);
        push_back(ss->fsx, fso);
    }
    return sid;
}

/* the page's color scheme (HTML, 4.2.5.4): the first meta name=color-scheme's content that
   is a color-scheme value, as normal uses it; 0 light (none, or none valid), 1 dark */
static int css_page_dark(const dom_document *d) {
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!css_is_named(d, x, "meta") || !d->node[x].parent) continue;
        uint32_t a = css_attr_of(d, x, "name", 4);
        if (!a || !css_eq_ci(text_ptr(d, d->attr[a].value), d->attr[a].value.len, "color-scheme")) continue;
        uint32_t c = css_attr_of(d, x, "content", 7);
        if (!c) continue;
        amb_span v = d->attr[c].value;
        const char *t = text_ptr(d, v);
        size_t i = 0, e = v.len;
        while (i < e && css_ws(t[i])) i++;
        while (e > i && css_ws(t[e - 1])) e--;
        if (!css_eq_ci(t + i, e - i, "normal") && !css_text_valid(CSS_PROP_COLOR_SCHEME, t + i, e - i)) continue;
        return css_scheme_of(t + i, e - i, 0);
    }
    return 0;
}

/* the element the URL's fragment names: the page's #id (or an a's name), 0 for none */
static uint32_t css_url_target(const css_style *s, const dom_document *doc, const net_loader *net) {
    (void)s;
    for (uint32_t r = 1; r < net->resource_len; r++) {
        if (!net->resource[r].live || net->resource[r].kind != NET_KIND_DOCUMENT) continue;
        amb_span u = net->resource[r].url;
        const char *t = text_ptr(net, u);
        size_t h = 0;
        while (h < u.len && t[h] != '#') h++;
        if (h + 1 >= u.len) return 0;
        char frag[256];
        size_t fn = u.len - h - 1 < sizeof frag ? u.len - h - 1 : sizeof frag - 1;
        memcpy(frag, t + h + 1, fn);
        for (uint32_t x = css_next_in_tree(doc, 1); x; x = css_next_in_tree(doc, x)) {
            if (!css_is_element(doc, x)) continue;
            uint32_t a = css_attr_of(doc, x, "id", 2);
            if (!a && css_is_named(doc, x, "a")) a = css_attr_of(doc, x, "name", 4);
            if (a && doc->attr[a].value.len == fn && memcmp(text_ptr(doc, doc->attr[a].value), frag, fn) == 0) return x;
        }
        return 0;
    }
    return 0;
}

css_status css_set_state(css_style *s, dom_document *doc, css_state_kind kind, uint32_t node) {
    if (node == 0 || node >= doc->node_len || !doc->node[node].live || doc->node[node].kind != DOM_NODE_KIND_ELEMENT) return CSS_WRONG_KIND;
    css_state_h h = state_new(s);
    s->state[h.index].kind = kind;
    s->state[h.index].node = node_handle(doc, node);
    s->revision++;
    return CSS_OK;
}

css_status css_cascade(css_style *s, dom_document *doc, net_loader *net) {
    /* the user agent's sheet, then the document's style elements and linked sheets, in tree order */
    css_ua_load(s);
    /* the user agent's rules are the HTML elements' (its sheet is in the HTML namespace) */
    uint32_t ua_end = (uint32_t)s->rule_len;
    /* the page's URL: the base its style elements' @imports and fonts resolve against */
    char page[4096];
    page[0] = 0;
    for (uint32_t r = 1; net && r < net->resource_len && !page[0]; r++)
        if (net->resource[r].live && net->resource[r].kind == NET_KIND_DOCUMENT) {
            amb_span fu = net->resource[r].final_url;
            snprintf(page, sizeof page, "%.*s", (int)fu.len, text_ptr(net, fu));
        }
    for (uint32_t x = css_next_in_tree(doc, 1); x; x = css_next_in_tree(doc, x)) {
        int link = css_is_named(doc, x, "link");
        if (!css_is_named(doc, x, "style") && !css_is_svg_named(doc, x, "style") && !link) continue;
        uint32_t ma = css_attr_of(doc, x, "media", 5);
        if (ma && !css_media_matches(text_ptr(doc, doc->attr[ma].value), doc->attr[ma].value.len)) continue;
        if (link) {
            uint32_t r = net ? css_linked_sheet(net, x) : 0;
            if (!r) continue;
            char base[4096];
            amb_span fu = net->resource[r].final_url;
            snprintf(base, sizeof base, "%.*s", (int)fu.len, text_ptr(net, fu));
            vector<char> body;
            append_range(body, bytes_ptr(net, net->resource[r].body), net->resource[r].body.len);
            css_sheet_read(s, net, CSS_ORIGIN_AUTHOR, data(body), len(body), base, 0, 0, 0, base, 0);
            continue;
        }
        vector<char> t;
        for (uint32_t ch = doc->node[x].first_child; ch; ch = doc->node[ch].next_sibling)
            if (doc->node[ch].kind == DOM_NODE_KIND_TEXT) append_range(t, text_ptr(doc, doc->node[ch].data), doc->node[ch].data.len);
        /* a style element's @scope without roots is rooted at its parent element */
        uint32_t host = doc->node[x].parent && css_is_element(doc, doc->node[x].parent) ? doc->node[x].parent : 0;
        css_sheet_read(s, net, CSS_ORIGIN_AUTHOR, data(t), len(t), 0, 0, 0, 0, page, host);
    }
    /* the cascade layers' order, now every sheet has named its layers */
    css_layer_rank(s, 0, 1);
    uint32_t shared = (uint32_t)s->rule_len;
    if (!css_ready) css_order_make();
    css_env env;
    memset(&env, 0, sizeof env);
    env.rem = css_font_px;
    env.page_dark = css_page_dark(doc);
    /* the web fonts' families whose font was fetched: each a word of env.faces, lower case
       and without its spaces, as font-family's names are matched */
    for (uint32_t fi = 1; fi < s->face_len; fi++) {
        if (!s->face[fi].font.index) continue;
        amb_span fam = s->face[fi].family;
        size_t k = strlen(env.faces);
        if (k + fam.len + 2 >= sizeof env.faces) break;
        if (k) env.faces[k++] = ' ';
        for (uint32_t q = 0; q < fam.len; q++) {
            char c = text_ptr(s, fam)[q];
            if (!css_ws(c)) env.faces[k++] = css_lower(c);
        }
        env.faces[k] = 0;
    }
    /* each property's initial value, computed (currentcolor black, a border's medium 3px:
       made the element's in css_element), and what it records */
    css_val initial[CSS_NPROPS];
    css_rec irec[CSS_NPROPS];
    vector<css_vtext> vc;
    resize(vc, CSS_VTEXT);
    for (int k = 0; k < CSS_NPROPS; k++) {
        css_val v;
        size_t il = strlen(css_pdefs[k].init);
        if (!css_longhand((css_prop)k, css_pdefs[k].init, il, &v)) css_word_val(&v, css_pdefs[k].init);
        if (v.unit == CSS_UNIT_TEXT) v.tx = text_add(s, css_pdefs[k].init, il);
        initial[k] = v;
    }
    /* the default font size the reader chose: the root's, which the page's inherit */
    initial[CSS_PROP_FONT_SIZE].num = css_font_px;
    css_par ipar;
    css_par_of(s, 0, initial, &ipar);
    for (int k = 0; k < CSS_NPROPS; k++) {
        css_val v = initial[k];
        css_compute(s, &env, (css_prop)k, &v, &ipar, initial);
        initial[k] = v;
        irec[k] = css_value_rec(s, &v, CSS_HOW_INITIAL, 0, &vc);
    }
    /* each property's initial value, for the reasoner to derive the values no declaration gave */
    uint32_t iv_color = 0;
    for (int k = 0; k < CSS_NPROPS; k++) {
        css_initval_h ih = initval_new(s);
        if (k == CSS_PROP_COLOR) iv_color = ih.index;
        s->initval[ih.index].prop = (css_prop)k;
        s->initval[ih.index].unit = initial[k].unit;
        s->initval[ih.index].color = initial[k].color;
        s->initval[ih.index].text = irec[k].text;
    }
    /* the styles, from 1 (style 0 a row unused), and each element's */
    css_styles ss = {0};
    defer(ss) { CSS_STYLES_DROP(ss); }
    resize(&ss.first, 1);
    resize(&ss.fsx, 1);
    resize(&ss.cfirst, 1);
    resize(&ss.ccount, 1);
    vector<css_skey> se;
    vector<uint32_t> sslots;
    vector<uint32_t> slists;
    vector<uint32_t> nstyle;
    resize(nstyle, (size_t)doc->node_len);
    size_t scap = 16;
    while (scap < 2 * (size_t)doc->node_len + 2) scap *= 2;
    resize(sslots, scap);
    vector<uint32_t> matched;
    vector<uint32_t> pmatched;          /* the pseudo-elements' rules: pseudo << 24 | rule */
    vector<css_cust> cust;
    /* the page's state: what css_set_state recorded, and the target the URL's fragment names */
    css_mstate ms;
    memset(&ms, 0, sizeof ms);
    for (uint32_t q = 1; q < s->state_len; q++) {
        uint32_t nx = s->state[q].node.index;
        css_state_kind k = s->state[q].kind;
        if (k == CSS_STATE_KIND_HOVER) ms.hover = nx;
        else if (k == CSS_STATE_KIND_FOCUS) ms.focus = nx;
        else if (k == CSS_STATE_KIND_FOCUS_VISIBLE) ms.focus_visible = nx;
        else if (k == CSS_STATE_KIND_ACTIVE) ms.active = nx;
        else ms.target = nx;
    }
    if (!ms.target && net) ms.target = css_url_target(s, doc, net);
    uint32_t hint_sheet = 0, inline_sheet = 0;
    /* the shared rules' index, and each element's filters: its ancestors' keys, its own */
    vector<css_part> parts;
    vector<css_rx> rxs;
    vector<css_bucket> bk;
    vector<uint32_t> slots;
    vector<css_bent> be;
    css_index_rules(s, shared, &parts, &rxs, &bk, &slots, &be);
    uint32_t univ = css_bucket_find(&bk, &slots, CSS_KEY_UNIVERSAL, 0, css_key_hash(CSS_KEY_UNIVERSAL, "", 0));
    uint32_t id_atom = names_find(doc, "id", 2), class_atom = names_find(doc, "class", 5);
    vector<uint64_t> anc;
    vector<uint64_t> selfk;
    resize(anc, (size_t)doc->node_len * 4);
    resize(selfk, (size_t)doc->node_len * 4);
    for (uint32_t x = css_next_in_tree(doc, 1); x; x = css_next_in_tree(doc, x)) {
        if (!css_is_element(doc, x)) continue;
        clear(matched);
        clear(pmatched);
        uint32_t px = css_parent_element(doc, x);
        uint64_t am[4], om[4];
        for (int q = 0; q < 4; q++) am[q] = px ? at(anc, (size_t)px * 4 + q) | at(selfk, (size_t)px * 4 + q) : 0;
        memset(om, 0, sizeof om);
        uint32_t kk[CSS_MAX_KEYS], kl[CSS_MAX_KEYS], kh[CSS_MAX_KEYS], heads[CSS_MAX_KEYS + 1], seen[CSS_MAX_KEYS + 1];
        int nk = css_element_keys(doc, x, id_atom, class_atom, kk, kl, kh, CSS_MAX_KEYS);
        int nb = 0;
        uint32_t first_rule = doc->node[x].ns == DOM_NAMESPACE_HTML ? 1 : ua_end;
        if (nk < 0) {
            /* more keys than the index keeps: every rule is tried, and the filter is full */
            for (int q = 0; q < 4; q++) om[q] = ~0ull;
            for (uint32_t r = first_rule; r < shared; r++)
                {
                    if (s->rule[r].keyframes) continue;
                    uint32_t prox, mk = css_rule_matches(s, doc, x, &ms, r, &parts, &rxs, am, &prox);
                    if (mk == 1) push_back(matched, (prox ? 256 - prox : 0) << 24 | r);
                    else if (mk) push_back(pmatched, (mk >> 8) << 24 | r);
                }
        } else {
            for (int q = 0; q < nk; q++) css_bloom_add(om, kh[q]);
            if (univ) { heads[nb] = at(bk, univ - 1).head; seen[nb] = univ; nb++; }
            for (int q = 0; q < nk; q++) {
                uint32_t b = css_bucket_find(&bk, &slots, kk[q], kl[q], kh[q]);
                int dup = b == 0;
                for (int z = 0; z < nb && !dup; z++) if (seen[z] == b) dup = 1;
                if (dup) continue;
                heads[nb] = at(bk, b - 1).head;
                seen[nb] = b;
                nb++;
            }
            /* the buckets' rules merged back into the rules' order, each once */
            uint32_t last = 0;
            for (;;) {
                uint32_t r = 0;
                int bi = -1;
                for (int q = 0; q < nb; q++) {
                    if (!heads[q]) continue;
                    uint32_t hr = at(be, heads[q]).rule;
                    if (r == 0 || hr < r) { r = hr; bi = q; }
                }
                if (bi < 0) break;
                heads[bi] = at(be, heads[bi]).next;
                if (r == last) continue;
                last = r;
                if (r >= first_rule) {
                    uint32_t prox, mk = css_rule_matches(s, doc, x, &ms, r, &parts, &rxs, am, &prox);
                    if (mk == 1) push_back(matched, (prox ? 256 - prox : 0) << 24 | r);
                    else if (mk) push_back(pmatched, (mk >> 8) << 24 | r);
                }
            }
        }
        for (int q = 0; q < 4; q++) { anc[(size_t)x * 4 + q] = am[q]; selfk[(size_t)x * 4 + q] = om[q]; }
        css_element_hints(s, doc, x, &hint_sheet, &matched);
        css_style_attr(s, doc, x, &inline_sheet, &matched);
        dom_node_h nh = node_handle(doc, x);
        for (size_t k = 0; k < len(matched); k++) {
            css_match_h m = match_new(s);
            s->match[m.index].node = nh;
            s->match[m.index].rule = at(matched, k) & 0xffffff;
            s->match[m.index].proximity = at(matched, k) >> 24 ? 256 - (at(matched, k) >> 24) : 0;
        }
        /* the element's style: the one its matched rules under its parent's style made, or a new one */
        uint32_t ps = px ? at(nstyle, px) : 0;
        uint32_t sid = css_style_make(s, &env, &ss, &se, &sslots, &slists, &matched, ps, px == 0, initial, irec, &cust, &vc);
        nstyle[x] = sid;
        if (px == 0 && s->cval[at(ss.first, sid) + CSS_PROP_COLOR_SCHEME].color == CSS_CANVAS_DARK) {
            /* the root in the dark scheme: color's initial value, CanvasText, is white there,
               for color: initial below it and for the reasoner's initial values */
            initial[CSS_PROP_COLOR].color = 0xffffffff;
            irec[CSS_PROP_COLOR] = css_value_rec(s, &initial[CSS_PROP_COLOR], CSS_HOW_INITIAL, 0, &vc);
            s->initval[iv_color].color = 0xffffffff;
            s->initval[iv_color].text = irec[CSS_PROP_COLOR].text;
        }
        css_estyle_h eh = estyle_new(s);
        s->estyle[eh.index].node = nh;
        s->estyle[eh.index].style = sid;
        s->estyle[eh.index].first = at(ss.first, sid);
        /* its pseudo-elements': the rules that matched each, under the element's style */
        for (uint32_t pk = CSS_PSEUDO_BEFORE; pk <= CSS_PSEUDO_PLACEHOLDER && len(pmatched); pk++) {
            clear(matched);
            for (size_t q = 0; q < len(pmatched); q++) if (at(pmatched, q) >> 24 == pk) push_back(matched, at(pmatched, q) & 0xffffff);
            if (!len(matched)) continue;
            for (size_t q = 0; q < len(matched); q++) {
                css_match_h m = match_new(s);
                s->match[m.index].node = nh;
                s->match[m.index].rule = at(matched, q);
                s->match[m.index].pseudo = (css_pseudo)pk;
            }
            uint32_t psid = css_style_make(s, &env, &ss, &se, &sslots, &slists, &matched, sid, 0, initial, irec, &cust, &vc);
            /* a ::before or an ::after whose content is none (or normal) is not generated */
            if (pk == CSS_PSEUDO_BEFORE || pk == CSS_PSEUDO_AFTER) {
                uint32_t cc = at(ss.first, psid) + CSS_PROP_CONTENT;
                if (s->cval[cc].unit == CSS_UNIT_KEYWORD && (names_is(s, s->cval[cc].word, "none", 4) || names_is(s, s->cval[cc].word, "normal", 6)))
                    continue;
            }
            css_pstyle_h ph = pstyle_new(s);
            s->pstyle[ph.index].node = nh;
            s->pstyle[ph.index].pseudo = (css_pseudo)pk;
            s->pstyle[ph.index].style = psid;
            s->pstyle[ph.index].first = at(ss.first, psid);
        }
    }
    s->revision++;
    return CSS_OK;
}
