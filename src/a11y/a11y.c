/* src/a11y/a11y.c -- the accessibility module (contracts/a11y.pl): the
   accessibility tree of a styled document; and the module's generated
   implementation.

   THE TREE, in document order, but that an element aria-owns takes (an id it
   lists, of an element not taken already and not above it) is its child after
   its own children. An element whose computed display is none -- the user agent
   sheet's for a closed details element's content and a dialog without open --,
   or that is hidden or aria-hidden="true", is not in it, nor is anything inside
   it; the root of each such subtree is kept, with what inside it still takes
   focus (inside aria-hidden content, which is drawn) or is a live region. An
   element whose computed visibility is hidden or collapse is no node, but its
   children are walked, as one may be drawn. What is never drawn by its nature
   (head, script, style, template, ...) is passed over. An
   element's role is its role attribute's first token that is a role, compared
   without case, or else its own (HTML-AAM: a with href a link, img an image --
   presentation when its alt is empty --, input by its type, h1 to h6 headings,
   ul and ol lists, nav, main, aside, header and footer the landmarks, a section
   with a name a region, output a status, a details element's summary a button,
   ...). role=none is presentation, unless the element takes focus or has a
   global aria-* attribute: then it keeps its own role (WAI-ARIA 1.2's conflict
   resolution). A list's items, and a table's row groups, rows and cells, whose
   list or table says presentation are presentation too (inherited presentation,
   as Chromium has it: whatever the list keeps, unless the item takes focus). An element whose role is generic or presentation is no node, and
   its children join its parent's node; but a generic element that takes focus,
   or that carries a global state or property other than a name (aria-live,
   aria-describedby, aria-current, ...), is a node, as Core-AAM includes it.

   A NODE'S NAME (accname 1.2, HTML-AAM): aria-labelledby's elements' text;
   aria-label; an image's alt; a labelable element's labels (each label for it,
   then the label it is the first control of); an input button's value; a
   table's caption, a fieldset's legend, a figure's figcaption, an optgroup's or
   option's label; the content of a role named from content; then title, then a
   field's placeholder; an input button's default. Content is walked through:
   text, the ::before and ::after content of each element (its alternative text
   after a solidus when it has one), an image's alt, a descendant's
   aria-labelledby or aria-label, and an embedded control's value -- a textbox
   its value, a combobox or listbox its chosen options, a range its valuetext or
   valuenow (one with nothing chosen and no value is read as any element) --; a
   line break, an element that is not inline, and what an element gives whole
   (an alt, a label, a value, an alternative text) set their text off by spaces,
   as Chromium sets them. As Chromium reads content, a nested
   container of widgets, a landmark or a live region adds nothing (a tree item's
   group, a menu item's submenu), and a row is named from its content in a grid
   alone. What is hidden or drawn as nothing is left out, unless aria-labelledby,
   aria-describedby or a label names such an element: then all of that element's
   subtree counts. White space
   collapses, and the name is trimmed.

   ITS DESCRIPTION (accname 1.2, HTML-AAM): aria-describedby's elements' text,
   aria-description, a table's caption when it is not the name, title when it is
   not the name and says something else.

   ITS STATES AND PROPERTIES (WAI-ARIA 1.2, HTML-AAM), each with where it came
   from: checked (an input's checked attribute; aria-checked, an ARIA checkbox,
   radio or switch without it unchecked), selected (an option's selectedness, a
   select's first enabled option when none is selected; aria-selected), expanded
   (a details element's summary; a select is closed; aria-expanded), pressed,
   disabled (its disabled attribute; a disabled fieldset's controls but those in
   its first legend; a disabled optgroup's or select's options; aria-disabled,
   and an aria-disabled element's focusable descendants), required, readonly,
   invalid (aria-invalid, false included; else HTML's constraint validation of a
   page no one has edited, each flag that holds kept: a value missing -- told of
   all but a text field --, an e-mail or URL of the wrong type, a value off its
   pattern (the script engine's regular expressions, with the v flag), under its
   min or over its max, off its step; a disabled or readonly control is barred),
   current, busy, haspopup (a select's popup a menu, as Chromium has it;
   a combobox's a listbox), orientation (with each role's default),
   multiselectable, modal, autocomplete (an input with a list), multiline. A token
   is compared without case, trimmed; a value that is none of a state's tokens
   counts as the state's default (the rules name it). A range's values: an
   input's, a progress element's or a meter's (HTML's defaults and clamping, a
   range input's value put on its step), or aria-valuenow, -valuemin and
   -valuemax with the role's defaults (a slider's value half way, a spinbutton's
   or meter's 0), aria-valuenow kept inside the range as Chromium keeps it; and
   aria-valuetext. A field's value as HTML's value sanitization leaves it (a
   password's as bullets), a combobox's chosen option; a field's placeholder.

   LIVE REGIONS: aria-live, or the role's politeness (alert assertive; status and
   log polite; marquee and timer off), aria-atomic (alert and status atomic) and
   aria-relevant ("additions text" by default). Each node inside a region knows
   the innermost one.

   LEVELS AND POSITIONS: a heading's level (aria-level, or h1 to h6, or 2), a list
   item's (the lists it is in, 1 for none), a tree item's (aria-level, or the groups of its
   tree it is in), a row's aria-level. aria-posinset and aria-setsize, or else counted: a
   list's items, a listbox's, select's or optgroup's options, a tablist's tabs, a
   tree's or group's tree items, a menu's items, a radio group's radios (one name
   in one form, or a radiogroup's), a feed's articles, a tree grid's rows of one
   level.

   Every id that aria-labelledby, aria-describedby, aria-controls, aria-owns,
   aria-activedescendant, aria-details, aria-errormessage, aria-flowto or a
   label's for names is kept with the element that has it. An element that is no
   node but has aria-* attributes is kept with its role.

   THE WORK. The walk is a loop over a stack of frames, each carrying what its
   element's children inherit (a disabled fieldset, optgroup or select, an
   aria-disabled ancestor, the live region, the lists and tree groups around
   them), so a tree of any depth is walked. An element's kind and its attributes'
   are integers, by their names' atoms, each name looked up when first asked; an
   element's aria-* attributes are read in one pass. Ids and labels are indexed
   when first asked. A node's record is kept small, its rarer facts (texts, a
   range's values, a live region's settings) records of their own.

   Not done, and why: Chromium's guess that a table is for layout (it is a table
   here); a name from content walks the document's children, not the elements
   aria-owns moved; a counter() in generated content adds nothing, and quotes are
   the first level's; a field's value too long or too short is never told, as no
   one has typed; a URL is judged by the network module's parser, which refuses a
   space in a host that Chromium takes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_interface(css);
amb_implementation(a11y);
#include "../css/css.h"
#include "../net/url.h"
/* HTML's pattern attribute, read by the script engine's regular expressions (src/js/js.h) */
int js_pattern_test(const char *p, size_t pn, const char *v, size_t vn);

/* ---- the names this module reads, found among the document's atoms when first asked ---- */

/* the element names, in strcmp order (a name's kind is its index, found by halving) */
enum { E_NONE, E_A, E_AREA, E_ARTICLE, E_ASIDE, E_AUDIO, E_BASE, E_BLOCKQUOTE, E_BODY, E_BR, E_BUTTON, E_CAPTION, E_CODE,
       E_COL, E_COLGROUP, E_DATALIST, E_DD, E_DETAILS, E_DIALOG, E_DIV, E_DT, E_FIELDSET, E_FIGCAPTION, E_FIGURE, E_FOOTER, E_FORM, E_H1, E_H2, E_H3,
       E_H4, E_H5, E_H6, E_HEAD, E_HEADER, E_HR, E_IFRAME, E_IMG, E_INPUT, E_LABEL, E_LEGEND, E_LI, E_LINK, E_MAIN, E_MENU,
       E_META, E_METER, E_NAV, E_NOSCRIPT, E_OL, E_OPTGROUP, E_OPTION, E_OUTPUT, E_P, E_PARAM, E_PROGRESS, E_SCRIPT, E_SEARCH,
       E_SECTION, E_SELECT, E_SLOT, E_SOURCE, E_SPAN, E_STYLE, E_SUMMARY, E_TABLE, E_TBODY, E_TD, E_TEMPLATE, E_TEXTAREA, E_TFOOT, E_TH,
       E_THEAD, E_TITLE, E_TR, E_TRACK, E_UL, E_VIDEO, E_COUNT };
static const char *const ax_elements[E_COUNT] = { "", "a", "area", "article", "aside", "audio", "base", "blockquote", "body", "br",
    "button", "caption", "code", "col", "colgroup", "datalist", "dd", "details", "dialog", "div", "dt", "fieldset", "figcaption", "figure", "footer", "form",
    "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr", "iframe", "img", "input", "label", "legend", "li", "link", "main",
    "menu", "meta", "meter", "nav", "noscript", "ol", "optgroup", "option", "output", "p", "param", "progress", "script", "search",
    "section", "select", "slot", "source", "span", "style", "summary", "table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead",
    "title", "tr", "track", "ul", "video" };

/* the attributes: HTML's, then the aria-* ones from A_FIRST, in strcmp order */
enum { K_NONE, K_ROLE, K_ID, K_HIDDEN, K_TABINDEX, K_DISABLED, K_HREF, K_ALT, K_TITLE, K_TYPE, K_VALUE, K_PLACEHOLDER, K_FOR,
       K_LABEL, K_MULTIPLE, K_SELECTED, K_CHECKED, K_REQUIRED, K_READONLY, K_OPEN, K_NAME, K_LIST, K_MIN, K_MAX, K_STEP, K_SCOPE,
       K_CONTENTEDITABLE, K_SIZE, K_CONTROLS, K_PATTERN, K_SUMMARY, K_RULES, K_HEADERS, K_ABBR, K_AXIS,
       A_ACTIVEDESCENDANT, A_ATOMIC, A_AUTOCOMPLETE, A_BUSY, A_CHECKED, A_COLCOUNT, A_COLINDEX, A_COLSPAN, A_CONTROLS, A_CURRENT,
       A_DESCRIBEDBY, A_DESCRIPTION, A_DETAILS, A_DISABLED, A_DROPEFFECT, A_ERRORMESSAGE, A_EXPANDED, A_FLOWTO, A_GRABBED,
       A_HASPOPUP, A_HIDDEN, A_INVALID, A_KEYSHORTCUTS, A_LABEL, A_LABELLEDBY, A_LEVEL, A_LIVE, A_MODAL, A_MULTILINE,
       A_MULTISELECTABLE, A_ORIENTATION, A_OWNS, A_PLACEHOLDER, A_POSINSET, A_PRESSED, A_READONLY, A_RELEVANT, A_REQUIRED,
       A_ROLEDESCRIPTION, A_ROWCOUNT, A_ROWINDEX, A_ROWSPAN, A_SELECTED, A_SETSIZE, A_SORT, A_VALUEMAX, A_VALUEMIN, A_VALUENOW,
       A_VALUETEXT, K_COUNT };
#define A_FIRST A_ACTIVEDESCENDANT
static const char *const ax_attrs[K_COUNT] = { "", "role", "id", "hidden", "tabindex", "disabled", "href", "alt", "title", "type",
    "value", "placeholder", "for", "label", "multiple", "selected", "checked", "required", "readonly", "open", "name", "list", "min",
    "max", "step", "scope", "contenteditable", "size", "controls", "pattern", "summary", "rules", "headers", "abbr", "axis",
    "aria-activedescendant", "aria-atomic", "aria-autocomplete", "aria-busy", "aria-checked", "aria-colcount", "aria-colindex",
    "aria-colspan", "aria-controls", "aria-current", "aria-describedby", "aria-description", "aria-details", "aria-disabled",
    "aria-dropeffect", "aria-errormessage", "aria-expanded", "aria-flowto", "aria-grabbed", "aria-haspopup", "aria-hidden",
    "aria-invalid", "aria-keyshortcuts", "aria-label", "aria-labelledby", "aria-level", "aria-live", "aria-modal",
    "aria-multiline", "aria-multiselectable", "aria-orientation", "aria-owns", "aria-placeholder", "aria-posinset",
    "aria-pressed", "aria-readonly", "aria-relevant", "aria-required", "aria-roledescription", "aria-rowcount",
    "aria-rowindex", "aria-rowspan", "aria-selected", "aria-setsize", "aria-sort", "aria-valuemax", "aria-valuemin",
    "aria-valuenow", "aria-valuetext" };

/* the attributes that name other elements by their ids, and what each is to the facts */
static const int ax_ref_kinds[8] = { A_LABELLEDBY, A_DESCRIBEDBY, A_CONTROLS, A_OWNS, A_ACTIVEDESCENDANT, A_DETAILS, A_ERRORMESSAGE,
                                     A_FLOWTO };
static const a11y_ref_attr ax_ref_attrs[8] = { A11Y_REF_ATTR_ARIA_LABELLEDBY, A11Y_REF_ATTR_ARIA_DESCRIBEDBY, A11Y_REF_ATTR_ARIA_CONTROLS,
    A11Y_REF_ATTR_ARIA_OWNS, A11Y_REF_ATTR_ARIA_ACTIVEDESCENDANT, A11Y_REF_ATTR_ARIA_DETAILS, A11Y_REF_ATTR_ARIA_ERRORMESSAGE,
    A11Y_REF_ATTR_ARIA_FLOWTO };

/* an input's type, its type attribute's state (an unknown one is text) */
enum { IT_TEXT, IT_HIDDEN, IT_CHECKBOX, IT_RADIO, IT_RANGE, IT_NUMBER, IT_BUTTON, IT_SUBMIT, IT_RESET, IT_IMAGE, IT_SEARCH, IT_EMAIL,
       IT_TEL, IT_URL, IT_PASSWORD, IT_DATE, IT_MONTH, IT_WEEK, IT_TIME, IT_DTL, IT_COLOR, IT_FILE };

/* the display and visibility keywords this module tells apart, as the style's atoms */
enum { W_NONE, W_INLINE, W_CONTENTS, W_HIDDEN, W_COLLAPSE, W_UPPERCASE, W_LOWERCASE, W_CAPITALIZE, W_MATH_AUTO, W_COUNT };

/* the walk of a name's text: inside an aria-labelledby or -describedby walk; all of a hidden element's subtree in it;
   at the element such a walk names, whose own content counts whatever its role */
enum { T_REF = 1, T_ALL = 2, T_TARGET = 4 };

/* every helper reads the document and its style. What it learns of the document's names is kept for the build: ek an
   element name's kind and ak an aria-* attribute's, by its atom (0xFF not yet known), aid each attribute kind's atom (~0
   not yet asked); the ids, and the labels with for, are indexed when first asked. What the build finds before its walk:
   gen each element's first pseudo-element style (its place in the style's table, 0 none); owner the element that owns
   each one by aria-owns (0 none; AX_MET added once the walk has met it there); rmiss each radio button whose group
   misses a checked button it needs */
#define ACTX a11y_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sx, vector<uint8_t> *ek, \
             vector<uint8_t> *ak, uint32_t *aid, const uint32_t *cw, vector<uint32_t> *ids, vector<uint32_t> *labels, \
             const vector<uint32_t> *gen, vector<uint32_t> *owner, vector<uint8_t> *rmiss
#define AARGS t, d, st, sx, ek, ak, aid, cw, ids, labels, gen, owner, rmiss
#define AX_MET 0x80000000u

/* ---- the DOM and its style, as this module reads them ---- */

static int ax_white(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char ax_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* p[0..n) is the word w (lower case), compared without case */
static int ax_word_is(const char *p, size_t n, const char *w) {
    size_t i = 0;
    while (i < n && w[i] && ax_lower(p[i]) == w[i]) i++;
    return i == n && w[i] == 0;
}

/* the index of the name p[0..n) in names[lo..hi), which are in strcmp order; 0 when it is none of them */
static int ax_find_name(const char *const *names, int lo, int hi, const char *p, size_t n) {
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const char *w = names[mid];
        size_t i = 0;
        while (i < n && w[i] && w[i] == p[i]) i++;
        int c;
        if (i == n) c = w[i] ? -1 : 0;
        else if (!w[i]) c = 1;
        else c = (unsigned char)p[i] < (unsigned char)w[i] ? -1 : 1;
        if (c == 0) return mid;
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return 0;
}
/* an element's kind, by its name's atom; E_NONE for any other node, and for an SVG or
   MathML element (an HTML name in another namespace names no HTML element) */
static int ax_kind(ACTX, uint32_t x) {
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) return E_NONE;
    uint32_t nm = d->node[x].name;
    if (nm >= len(*ek)) return E_NONE;
    int k = at(*ek, nm);
    if (k == 0xFF) {
        k = ax_find_name(ax_elements, 1, E_COUNT, names_ptr(d, nm), names_len(d, nm));
        (*ek)[nm] = (uint8_t)k;
    }
    return k;
}
/* MathML's elements whose roles Chromium names, in strcmp order, and those roles: an mrow's for those laid out as one
   (mstyle, mpadded, merror, semantics, maction); any other MathML element is generic */
enum { AX_MATHML_N = 28 };
static const char *const ax_mathml_names[AX_MATHML_N] = { "", "maction", "math", "merror", "mfrac", "mi", "mmultiscripts",
    "mn", "mo", "mover", "mpadded", "mprescripts", "mroot", "mrow", "ms", "msqrt", "mstyle", "msub", "msubsup", "msup",
    "mtable", "mtd", "mtext", "mtr", "munder", "munderover", "none", "semantics" };
static const a11y_role ax_mathml_roles[AX_MATHML_N] = { A11Y_ROLE_GENERIC, A11Y_ROLE_MATHML_ROW, A11Y_ROLE_MATH,
    A11Y_ROLE_MATHML_ROW, A11Y_ROLE_MATHML_FRACTION, A11Y_ROLE_MATHML_IDENTIFIER, A11Y_ROLE_MATHML_MULTISCRIPTS,
    A11Y_ROLE_MATHML_NUMBER, A11Y_ROLE_MATHML_OPERATOR, A11Y_ROLE_MATHML_OVER, A11Y_ROLE_MATHML_ROW,
    A11Y_ROLE_MATHML_PRESCRIPT_DELIMITER, A11Y_ROLE_MATHML_ROOT, A11Y_ROLE_MATHML_ROW, A11Y_ROLE_MATHML_STRING_LITERAL,
    A11Y_ROLE_MATHML_SQUARE_ROOT, A11Y_ROLE_MATHML_ROW, A11Y_ROLE_MATHML_SUB, A11Y_ROLE_MATHML_SUB_SUP,
    A11Y_ROLE_MATHML_SUP, A11Y_ROLE_MATHML_TABLE, A11Y_ROLE_MATHML_TABLE_CELL, A11Y_ROLE_MATHML_TEXT,
    A11Y_ROLE_MATHML_TABLE_ROW, A11Y_ROLE_MATHML_UNDER, A11Y_ROLE_MATHML_UNDER_OVER, A11Y_ROLE_MATHML_NONE_SCRIPT,
    A11Y_ROLE_MATHML_ROW };
/* x is a MathML element */
static int ax_mathml(const dom_document *d, uint32_t x) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_MATHML;
}
/* a MathML element's own role, as Chromium gives it */
static a11y_role ax_mathml_role(const dom_document *d, uint32_t x) {
    uint32_t nm = d->node[x].name;
    int i = ax_find_name(ax_mathml_names, 1, AX_MATHML_N, names_ptr(d, nm), names_len(d, nm));
    return ax_mathml_roles[i];
}
/* a role Chromium gives MathML alone (math aside, which a role attribute may give an element of any kind) */
static int ax_mathml_only(a11y_role r) { return r >= A11Y_ROLE_MATHML_ROW && r <= A11Y_ROLE_MATHML_TABLE_CELL; }

/* x's attribute of the kind k, or 0 */
static uint32_t ax_get(ACTX, uint32_t x, int k) {
    uint32_t a = d->node[x].first_attr;
    if (!a) return 0;
    uint32_t id = aid[k];
    if (id == 0xFFFFFFFFu) {
        id = names_find(d, ax_attrs[k], strlen(ax_attrs[k]));
        aid[k] = id;
    }
    if (!id) return 0;
    for (; a; a = d->attr[a].next) if (d->attr[a].name == id) return a;
    return 0;
}
/* the attribute a's value, trimmed, compared without case with each of the space-separated words: the index of
   the word it is, -1 for none of them, -2 when a is 0 or its value is empty */
static int ax_tok(const dom_document *d, uint32_t a, const char *words) {
    if (!a) return -2;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t b = 0, e = v.len;
    while (b < e && ax_white(p[b])) b++;
    while (e > b && ax_white(p[e - 1])) e--;
    if (b == e) return -2;
    int idx = 0;
    size_t i = 0, wl = strlen(words);
    while (i < wl) {
        size_t j = i;
        while (j < wl && words[j] != ' ') j++;
        if (j - i == e - b) {
            size_t q = 0;
            while (q < e - b && ax_lower(p[b + q]) == words[i + q]) q++;
            if (q == e - b) return idx;
        }
        idx++;
        i = j + 1;
    }
    return -1;
}
/* the attribute's value, trimmed, into buf as a C string; its length (0 when a is 0 or it does not fit) */
static size_t ax_copy(const dom_document *d, uint32_t a, char *buf, size_t cap) {
    buf[0] = 0;
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t b = 0, e = v.len;
    while (b < e && ax_white(p[b])) b++;
    while (e > b && ax_white(p[e - 1])) e--;
    if (e - b + 1 > cap) return 0;
    memcpy(buf, p + b, e - b);
    buf[e - b] = 0;
    return e - b;
}
/* the attribute's value as a number written in decimal: 1 and *v, or 0 */
static int ax_num(const dom_document *d, uint32_t a, double *v) {
    char buf[64];
    size_t n = ax_copy(d, a, buf, sizeof buf);
    if (!n) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) return 0;
    }
    char *end = buf;
    double x = strtod(buf, &end);
    if (end == buf || *end || x != x || x > 1e300 || x < -1e300) return 0;
    *v = x;
    return 1;
}
/* the attribute's value as an integer: 1 and *v, or 0 */
static int ax_int(const dom_document *d, uint32_t a, long *v) {
    char buf[32];
    size_t n = ax_copy(d, a, buf, sizeof buf);
    if (!n) return 0;
    size_t i = buf[0] == '-' || buf[0] == '+' ? 1 : 0;
    if (i == n) return 0;
    long x = 0;
    for (; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9') return 0;
        if (x < 100000000) x = x * 10 + (buf[i] - '0');
    }
    *v = buf[0] == '-' ? -x : x;
    return 1;
}

/* x's computed display keyword, as the style's atom: 0 when it has no style */
static uint32_t ax_display(ACTX, uint32_t x) {
    if (x >= len(*sx) || !at(*sx, x)) return 0;
    uint32_t c0 = at(*sx, x);
    for (uint32_t k = c0; k < st->cval_len && st->cval[k].style == st->cval[c0].style; k++)
        if (st->cval[k].prop == CSS_PROP_DISPLAY) return st->cval[k].word;
    return 0;
}
static int ax_display_none(ACTX, uint32_t x) {
    if (x >= len(*sx) || !at(*sx, x)) return 1;
    uint32_t w = ax_display(AARGS, x);
    return cw[W_NONE] && w == cw[W_NONE];
}
/* an element drawn as more than inline content: its text is set off by spaces in a name */
static int ax_block(ACTX, uint32_t x) {
    uint32_t w = ax_display(AARGS, x);
    return w && w != cw[W_INLINE] && w != cw[W_CONTENTS] && w != cw[W_NONE];
}
/* why an element is hidden from a reader -- its hidden attribute, aria-hidden="true", display none (or no style) --; or -1 */
static int ax_hidden_how(ACTX, uint32_t x) {
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return -1;
    if (d->node[x].first_attr) {
        if (ax_get(AARGS, x, K_HIDDEN)) return A11Y_HIDDEN_HOW_HIDDEN_ATTRIBUTE;
        if (ax_tok(d, ax_get(AARGS, x, A_HIDDEN), "true") == 0) return A11Y_HIDDEN_HOW_ARIA_HIDDEN;
    }
    if (ax_display_none(AARGS, x)) return A11Y_HIDDEN_HOW_DISPLAY_NONE;
    return -1;
}

/* x's computed visibility is hidden or collapse: it is drawn as nothing, though a child of it may be drawn */
static int ax_invisible(ACTX, uint32_t x) {
    if (x >= len(*sx) || !at(*sx, x)) return 0;
    uint32_t c = at(*sx, x) + CSS_PROP_VISIBILITY;
    if (c >= st->cval_len || st->cval[c].prop != CSS_PROP_VISIBILITY) return 0;
    uint32_t w = st->cval[c].word;
    return w && (w == cw[W_HIDDEN] || w == cw[W_COLLAPSE]);
}

static int ax_input_type(ACTX, uint32_t x) {
    int v = ax_tok(d, ax_get(AARGS, x, K_TYPE),
                   "text hidden checkbox radio range number button submit reset image search email tel url password date month week time datetime-local color file");
    if (v < 0) return IT_TEXT;
    return v;
}
static int ax_text_input(int ty) {
    return ty == IT_TEXT || ty == IT_SEARCH || ty == IT_EMAIL || ty == IT_TEL || ty == IT_URL || ty == IT_PASSWORD;
}
static int ax_input_button(int ty) { return ty == IT_BUTTON || ty == IT_SUBMIT || ty == IT_RESET; }

/* the first child element of x of the kind k, or 0 */
static uint32_t ax_child(ACTX, uint32_t x, int k) {
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) if (ax_kind(AARGS, c) == k) return c;
    return 0;
}

/* the element whose id is the text p[0..n), the first in the document's order; the ids are hashed when first asked */
static uint32_t ax_id_hash(const char *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 16777619u; }
    return h;
}
static int ax_id_is(ACTX, uint32_t y, const char *p, size_t n) {
    uint32_t a = ax_get(AARGS, y, K_ID);
    if (!a || d->attr[a].value.len != n) return 0;
    return n == 0 || memcmp(text_ptr(d, d->attr[a].value), p, n) == 0;
}
/* in the document's tree: not detached, nor in a template's contents (12c) */
static int ax_connected(const dom_document *d, uint32_t x) {
    while (x && x != 1) x = d->node[x].parent;
    return x == 1;
}
static void ax_ids_index(ACTX) {
    size_t count = 0;
    for (uint32_t x = 1; x < d->node_len; x++)
        if (d->node[x].live && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].first_attr && ax_get(AARGS, x, K_ID)) count++;
    size_t cap = 16;
    while (cap < 2 * count + 1) cap *= 2;
    resize(ids, cap);
    uint32_t m = (uint32_t)(cap - 1);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT || !d->node[x].first_attr) continue;
        uint32_t a = ax_get(AARGS, x, K_ID);
        if (!a || !d->attr[a].value.len || !ax_connected(d, x)) continue;
        amb_span v = d->attr[a].value;
        uint32_t h = ax_id_hash(text_ptr(d, v), v.len) & m;
        int dup = 0;
        while (at(*ids, h)) {
            if (ax_id_is(AARGS, at(*ids, h), text_ptr(d, v), v.len)) { dup = 1; break; }
            h = (h + 1) & m;
        }
        if (!dup) (*ids)[h] = x;
    }
}
static uint32_t ax_by_id(ACTX, const char *p, size_t n) {
    if (!aid[K_ID] || !n) return 0;
    if (!len(*ids)) ax_ids_index(AARGS);
    uint32_t m = (uint32_t)(len(*ids) - 1);
    uint32_t h = ax_id_hash(p, n) & m;
    while (at(*ids, h)) {
        uint32_t y = at(*ids, h);
        if (ax_id_is(AARGS, y, p, n)) return y;
        h = (h + 1) & m;
    }
    return 0;
}

/* ---- text ---- */

/* text into out, white space runs one space */
static void ax_put(vector<char> *out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (ax_white(c)) { if (len(*out) && back(*out) != ' ') push_back(out, ' '); }
        else push_back(out, c);
    }
}
static void ax_space(vector<char> *out) { if (len(*out) && back(*out) != ' ') push_back(out, ' '); }
static int ax_says(const vector<char> *v, size_t from) {
    for (size_t i = from; i < len(*v); i++) if (at(*v, i) != ' ') return 1;
    return 0;
}
/* an attribute's value into out: 1 when it says something, else nothing is added */
static int ax_put_attr(const dom_document *d, uint32_t a, vector<char> *out) {
    size_t mark = len(*out);
    amb_span v = d->attr[a].value;
    ax_put(out, text_ptr(d, v), v.len);
    if (ax_says(out, mark)) return 1;
    resize(out, mark);
    return 0;
}
/* the trimmed text, into the store's text */
static amb_span ax_name(a11y_tree *t, const vector<char> *v) {
    size_t a = 0, b = len(*v);
    while (a < b && at(*v, a) == ' ') a++;
    while (b > a && at(*v, b - 1) == ' ') b--;
    amb_span none = { 0, 0 };
    if (a == b) return none;
    return text_add(t, data(*v) + a, b - a);
}
/* two texts the same once trimmed */
static int ax_same_text(const vector<char> *x, const vector<char> *y) {
    size_t a = 0, b = len(*x), c = 0, e = len(*y);
    while (a < b && at(*x, a) == ' ') a++;
    while (b > a && at(*x, b - 1) == ' ') b--;
    while (c < e && at(*y, c) == ' ') c++;
    while (e > c && at(*y, e - 1) == ' ') e--;
    if (b - a != e - c) return 0;
    for (size_t i = 0; i < b - a; i++) if (at(*x, a + i) != at(*y, c + i)) return 0;
    return 1;
}

/* ---- roles ---- */

/* a role's name, compared without case: its role, or -1 (none is presentation, image is img) */
static int ax_role_word(const char *p, size_t n) {
    if (ax_word_is(p, n, "none")) return A11Y_ROLE_PRESENTATION;
    if (ax_word_is(p, n, "image")) return A11Y_ROLE_IMG;
    if (ax_word_is(p, n, "directory")) return A11Y_ROLE_LIST;   /* deprecated in WAI-ARIA 1.2: Chromium's list */
    for (int k = 0; k < (int)A11Y_ROLE_UNKNOWN; k++)
        if (ax_word_is(p, n, a11y_role_name((a11y_role)k))) return k;
    return -1;
}
/* a role attribute's first token that is a role, whatever its context: that role, or -1 (none for no attribute) */
static int ax_raw_role(ACTX, uint32_t x) {
    uint32_t a = d->node[x].first_attr ? ax_get(AARGS, x, K_ROLE) : 0;
    if (!a) return -1;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        int r = ax_role_word(p + b, i - b);
        if (r >= 0) return r;
    }
    return -1;
}
static a11y_role ax_role_of(ACTX, uint32_t x, int k);
/* an element a list item, option or tree item looks through for its context: a div, span or slot of no role attribute,
   or an element of a custom element's name (Blink's IsGenericWrapper) */
static int ax_wrapper(ACTX, uint32_t x, int k) {
    uint32_t ra = d->node[x].first_attr ? ax_get(AARGS, x, K_ROLE) : 0;
    if (ra && d->attr[ra].value.len) return 0;
    if (k == E_DIV || k == E_SPAN || k == E_SLOT) return 1;
    if (d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    uint32_t nm = d->node[x].name;
    const char *p = names_ptr(d, nm);
    size_t n = names_len(d, nm);
    if (!n || p[0] < 'a' || p[0] > 'z') return 0;
    for (size_t i = 0; i < n; i++) if (p[i] == '-') return 1;
    return 0;
}
/* an element of role r -- listitem, option or treeitem -- outside the context WAI-ARIA asks of it, as Chromium judges it
   (Blink's IsOrphanedListItem, IsOrphanedOption and IsOrphanedTreeItem): not owned by aria-owns by what may hold it (a
   list, listbox or tree, or a group), and the nearest of its ancestors that is not looked through (a wrapper, an element
   whose role attribute says none, a tree item's tree item) not one that may: a ul, ol or menu for a list item, a select
   for an option, or one whose role attribute's role is that list, listbox, tree or group */
static int ax_orphaned(ACTX, uint32_t x, a11y_role r) {
    a11y_role holder = r == A11Y_ROLE_LISTITEM ? A11Y_ROLE_LIST : r == A11Y_ROLE_OPTION ? A11Y_ROLE_LISTBOX : A11Y_ROLE_TREE;
    uint32_t o = x < len(*owner) ? at(*owner, x) & ~AX_MET : 0;
    if (o) {
        a11y_role orole = ax_role_of(AARGS, o, ax_kind(AARGS, o));
        if (orole == holder || orole == A11Y_ROLE_GROUP) return 0;
    }
    for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent) {
        if (d->node[p].kind != DOM_NODE_KIND_ELEMENT) continue;
        int k = ax_kind(AARGS, p);
        if (r == A11Y_ROLE_LISTITEM && (k == E_UL || k == E_OL || k == E_MENU)) return 0;
        if (r == A11Y_ROLE_OPTION && k == E_SELECT) return 0;
        int pr = ax_raw_role(AARGS, p);
        if (pr == (int)holder || pr == A11Y_ROLE_GROUP) return 0;
        if (pr == A11Y_ROLE_PRESENTATION || (r == A11Y_ROLE_TREEITEM && pr == A11Y_ROLE_TREEITEM) || ax_wrapper(AARGS, p, k)) continue;
        break;
    }
    return 1;
}
/* a role attribute's first token that is a role in its context (a list item, an option or a tree item must be where
   one may be, or the next token is read): its role, 1; or 0, its first token that is no role in bad */
static int ax_role_attr(ACTX, uint32_t x, a11y_role *role, size_t *bad_off, size_t *bad_len) {
    *bad_len = 0;
    if (!d->node[x].first_attr) return 0;
    uint32_t a = ax_get(AARGS, x, K_ROLE);
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    size_t i = 0;
    while (i < v.len) {
        const char *p = text_ptr(d, d->attr[a].value);
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        int r = ax_role_word(p + b, i - b);
        if (r >= 0) {
            if ((r == A11Y_ROLE_LISTITEM || r == A11Y_ROLE_OPTION || r == A11Y_ROLE_TREEITEM) && ax_orphaned(AARGS, x, (a11y_role)r)) continue;
            *role = (a11y_role)r;
            return 1;
        }
        if (!*bad_len) { *bad_off = b; *bad_len = i - b; }
    }
    return 0;
}

/* inside a sectioning element: then header and footer are no landmarks */
static int ax_in_section(ACTX, uint32_t x) {
    for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent) {
        int k = ax_kind(AARGS, p);
        if (k == E_ARTICLE || k == E_ASIDE || k == E_MAIN || k == E_NAV || k == E_SECTION) return 1;
    }
    return 0;
}
/* a select shown as a list: multiple, or a size above 1 */
static int ax_select_list(ACTX, uint32_t x) {
    if (ax_get(AARGS, x, K_MULTIPLE)) return 1;
    long n = 0;
    return ax_int(d, ax_get(AARGS, x, K_SIZE), &n) && n > 1;
}
/* a details element's first summary: its disclosure button */
static int ax_summary_button(ACTX, uint32_t x) {
    uint32_t p = d->node[x].parent;
    return p && ax_kind(AARGS, p) == E_DETAILS && ax_child(AARGS, p, E_SUMMARY) == x;
}
static a11y_role ax_input_role(ACTX, uint32_t x) {
    int ty = ax_input_type(AARGS, x);
    if (ty == IT_CHECKBOX) return A11Y_ROLE_CHECKBOX;
    if (ty == IT_RADIO) return A11Y_ROLE_RADIO;
    if (ty == IT_RANGE) return A11Y_ROLE_SLIDER;
    if (ty == IT_NUMBER) return A11Y_ROLE_SPINBUTTON;
    if (ax_input_button(ty) || ty == IT_IMAGE) return A11Y_ROLE_BUTTON;
    int listed = ax_get(AARGS, x, K_LIST) != 0;
    if (ty == IT_SEARCH) return listed ? A11Y_ROLE_COMBOBOX : A11Y_ROLE_SEARCHBOX;
    if (listed && (ty == IT_TEXT || ty == IT_EMAIL || ty == IT_TEL || ty == IT_URL)) return A11Y_ROLE_COMBOBOX;
    return A11Y_ROLE_TEXTBOX;
}

/* ---- tables Chromium takes for layout ---- */

/* x's computed value of prop: its record's index, 0 when it has none */
static uint32_t ax_cval(ACTX, uint32_t x, uint32_t prop) {
    if (x >= len(*sx) || !at(*sx, x)) return 0;
    uint32_t c = at(*sx, x) + prop;
    if (c >= st->cval_len || st->cval[c].prop != prop) return 0;
    return c;
}
/* a side of x's border drawn: a width above 0 in a style that draws */
static int ax_border(ACTX, uint32_t x, uint32_t wprop, uint32_t sprop) {
    uint32_t w = ax_cval(AARGS, x, wprop), s = ax_cval(AARGS, x, sprop);
    if (!w || !(st->cval[w].num > 0)) return 0;
    return !s || !st->cval[s].word || (st->cval[s].word != cw[W_NONE] && st->cval[s].word != cw[W_HIDDEN]);
}
static uint32_t ax_bg(ACTX, uint32_t x) {
    uint32_t c = ax_cval(AARGS, x, CSS_PROP_BACKGROUND_COLOR);
    return c ? st->cval[c].color : 0;
}
/* a table's rows, as its rows collection has them: its thead's, then its own and its tbodies' in order, then its
   tfoot's (the first of each) */
static void ax_table_rows(ACTX, uint32_t x, vector<uint32_t> *rows) {
    uint32_t th = ax_child(AARGS, x, E_THEAD), tf = ax_child(AARGS, x, E_TFOOT);
    for (int pass = 0; pass < 3; pass++)
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
            int ck = ax_kind(AARGS, c);
            if (pass == 1 && ck == E_TR) { push_back(*rows, c); continue; }
            if (!((pass == 0 && c == th) || (pass == 1 && ck == E_TBODY) || (pass == 2 && c == tf))) continue;
            for (uint32_t r = d->node[c].first_child; r; r = d->node[r].next_sibling)
                if (ax_kind(AARGS, r) == E_TR) push_back(*rows, r);
        }
}
/* Chromium's guess that a table holds data, not layout (Blink's AXNodeObject::IsDataTable, measured on
   tests/pages/a11y-tables.html): a caption, summary, thead, tfoot, rules or col; 20 rows or more; a th, or a cell
   with headers, abbr, axis or scope; of the cells drawn, half or more bordered (both sides of an axis, or each on the
   same side), or of a background of their own other than the table's (the spacing between them taken to be the user
   agent's: border-spacing is not read), ten of either enough; its first rows' backgrounds alternating. Otherwise, with
   two cells drawn or fewer, one row of one cell, or no row: layout */
static int ax_data_table(ACTX, uint32_t x) {
    if (d->node[x].first_attr && (ax_tok(d, ax_get(AARGS, x, K_SUMMARY), "") != -2 || ax_tok(d, ax_get(AARGS, x, K_RULES), "") != -2)) return 1;
    if (ax_child(AARGS, x, E_CAPTION) || ax_child(AARGS, x, E_THEAD) || ax_child(AARGS, x, E_TFOOT) || ax_child(AARGS, x, E_COL) ||
        ax_child(AARGS, x, E_COLGROUP)) return 1;
    vector<uint32_t> rows;
    ax_table_rows(AARGS, x, &rows);
    size_t nr = len(rows);
    if (nr >= 20) return 1;
    if (nr == 0) return 0;
    uint32_t first_cells = 0;
    for (uint32_t c = d->node[at(rows, 0)].first_child; c; c = d->node[c].next_sibling) {
        int ck = ax_kind(AARGS, c);
        if (ck == E_TD || ck == E_TH) first_cells++;
    }
    if (nr == 1 && first_cells == 1) return 0;
    uint32_t tbg = ax_bg(AARGS, x);
    uint32_t valid = 0, bordered = 0, bgdiff = 0, top = 0, bottom = 0, left = 0, right = 0;
    uint32_t zebra[5];
    uint32_t nz = 0;
    for (size_t ri = 0; ri < nr; ri++) {
        uint32_t row = at(rows, ri);
        for (uint32_t c = d->node[row].first_child; c; c = d->node[c].next_sibling) {
            int ck = ax_kind(AARGS, c);
            if (ck != E_TD && ck != E_TH) continue;
            if (ck == E_TH) return 1;
            if (d->node[c].first_attr && (ax_tok(d, ax_get(AARGS, c, K_HEADERS), "") != -2 || ax_tok(d, ax_get(AARGS, c, K_ABBR), "") != -2 ||
                                          ax_tok(d, ax_get(AARGS, c, K_AXIS), "") != -2 || ax_tok(d, ax_get(AARGS, c, K_SCOPE), "") != -2)) return 1;
            /* a cell drawn: neither it nor its row nor its group of display none */
            if (ax_display_none(AARGS, c) || ax_display_none(AARGS, row) ||
                (d->node[row].parent != x && ax_display_none(AARGS, d->node[row].parent))) continue;
            valid++;
            int bt = ax_border(AARGS, c, CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_TOP_STYLE),
                bb = ax_border(AARGS, c, CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_BOTTOM_STYLE),
                bl = ax_border(AARGS, c, CSS_PROP_BORDER_LEFT_WIDTH, CSS_PROP_BORDER_LEFT_STYLE),
                br = ax_border(AARGS, c, CSS_PROP_BORDER_RIGHT_WIDTH, CSS_PROP_BORDER_RIGHT_STYLE);
            if ((bt && bb) || (bl && br)) bordered++;
            top += bt;
            bottom += bb;
            left += bl;
            right += br;
            uint32_t cbg = ax_bg(AARGS, c);
            if (cbg != tbg && (cbg & 0xff) != 0) bgdiff++;
            if (bordered >= 10 || bgdiff >= 10) return 1;
            if (ri < 5 && ri == nz) zebra[nz++] = ax_bg(AARGS, row);
        }
    }
    if (valid <= 1) return 0;
    uint32_t need = valid / 2;
    if (bordered >= need || top >= need || bottom >= need || left >= need || right >= need) return 1;
    if (bgdiff >= need) return 1;
    if (nz > 2) {
        for (uint32_t q = 1; q < nz; q++) {
            if (q % 2 == 1 && zebra[q] == zebra[0]) return 0;
            if (q % 2 == 0 && zebra[q] != zebra[0]) return 0;
        }
        return 1;
    }
    return 0;
}
/* a row's, a cell's or a row group's table, up its rows and groups: 0 when it is in none */
static uint32_t ax_table_of(ACTX, uint32_t x) {
    for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent) {
        int pk = ax_kind(AARGS, p);
        if (pk == E_TABLE) return p;
        if (pk != E_TR && pk != E_THEAD && pk != E_TBODY && pk != E_TFOOT) return 0;
    }
    return 0;
}
static int ax_role_attr(ACTX, uint32_t x, a11y_role *role, size_t *bad_off, size_t *bad_len);
/* a table of no role attribute's role that Chromium takes for layout */
static int ax_layout_table(ACTX, uint32_t x) {
    a11y_role r = A11Y_ROLE_GENERIC;
    size_t bo = 0, bl = 0;
    if (!x || (d->node[x].first_attr && ax_role_attr(AARGS, x, &r, &bo, &bl))) return 0;
    return !ax_data_table(AARGS, x);
}

/* an element's own role (HTML-AAM) */
static a11y_role ax_implicit(ACTX, uint32_t x, int k) {
    switch (k) {
    case E_BODY: return A11Y_ROLE_DOCUMENT;
    case E_A: case E_AREA: return ax_get(AARGS, x, K_HREF) ? A11Y_ROLE_LINK : A11Y_ROLE_GENERIC;
    case E_BUTTON: return A11Y_ROLE_BUTTON;
    case E_INPUT: return ax_input_role(AARGS, x);
    case E_TEXTAREA: return A11Y_ROLE_TEXTBOX;
    case E_SELECT: return ax_select_list(AARGS, x) ? A11Y_ROLE_LISTBOX : A11Y_ROLE_COMBOBOX;
    case E_OPTION: return A11Y_ROLE_OPTION;
    case E_OPTGROUP: case E_FIELDSET: case E_DETAILS: return A11Y_ROLE_GROUP;
    case E_IMG: {
        uint32_t alt = ax_get(AARGS, x, K_ALT);
        return alt && d->attr[alt].value.len == 0 ? A11Y_ROLE_PRESENTATION : A11Y_ROLE_IMG;
    }
    case E_H1: case E_H2: case E_H3: case E_H4: case E_H5: case E_H6: return A11Y_ROLE_HEADING;
    case E_P: return A11Y_ROLE_PARAGRAPH;
    case E_UL: case E_OL: case E_MENU: return A11Y_ROLE_LIST;
    case E_LI: return A11Y_ROLE_LISTITEM;
    case E_TABLE: return ax_data_table(AARGS, x) ? A11Y_ROLE_TABLE : A11Y_ROLE_LAYOUT_TABLE;
    case E_CAPTION: return A11Y_ROLE_CAPTION;
    case E_TR: return ax_layout_table(AARGS, ax_table_of(AARGS, x)) ? A11Y_ROLE_LAYOUT_TABLE_ROW : A11Y_ROLE_ROW;
    case E_TD: return ax_layout_table(AARGS, ax_table_of(AARGS, x)) ? A11Y_ROLE_LAYOUT_TABLE_CELL : A11Y_ROLE_CELL;
    case E_TH: return ax_tok(d, ax_get(AARGS, x, K_SCOPE), "row") == 0 ? A11Y_ROLE_ROWHEADER : A11Y_ROLE_COLUMNHEADER;
    case E_NAV: return A11Y_ROLE_NAVIGATION;
    case E_MAIN: return A11Y_ROLE_MAIN;
    case E_ASIDE: return A11Y_ROLE_COMPLEMENTARY;
    case E_HEADER: return ax_in_section(AARGS, x) ? A11Y_ROLE_GENERIC : A11Y_ROLE_BANNER;
    case E_FOOTER: return ax_in_section(AARGS, x) ? A11Y_ROLE_GENERIC : A11Y_ROLE_CONTENTINFO;
    case E_SECTION:
        return ax_get(AARGS, x, A_LABEL) || ax_get(AARGS, x, A_LABELLEDBY) || ax_get(AARGS, x, K_TITLE) ? A11Y_ROLE_REGION : A11Y_ROLE_GENERIC;
    case E_FORM: return A11Y_ROLE_FORM;
    case E_SEARCH: return A11Y_ROLE_SEARCH;
    case E_ARTICLE: return A11Y_ROLE_ARTICLE;
    case E_DIALOG: return A11Y_ROLE_DIALOG;
    case E_FIGURE: return A11Y_ROLE_FIGURE;
    case E_HR: return A11Y_ROLE_SEPARATOR;
    case E_DT: return A11Y_ROLE_TERM;
    case E_DD: return A11Y_ROLE_DEFINITION;
    case E_BLOCKQUOTE: return A11Y_ROLE_BLOCKQUOTE;
    case E_CODE: return A11Y_ROLE_CODE;
    case E_PROGRESS: return A11Y_ROLE_PROGRESSBAR;
    case E_METER: return A11Y_ROLE_METER;
    case E_OUTPUT: return A11Y_ROLE_STATUS;
    case E_SUMMARY: return ax_summary_button(AARGS, x) ? A11Y_ROLE_BUTTON : A11Y_ROLE_GENERIC;
    default: return ax_mathml(d, x) ? ax_mathml_role(d, x) : A11Y_ROLE_GENERIC;
    }
}
/* an element whose role attribute's role is presentation (none), whatever conflict resolution keeps */
static int ax_says_none(ACTX, uint32_t x) {
    a11y_role r = A11Y_ROLE_GENERIC;
    size_t bo = 0, bl = 0;
    return d->node[x].first_attr && ax_role_attr(AARGS, x, &r, &bo, &bl) && r == A11Y_ROLE_PRESENTATION;
}
/* an element whose own role its context's presentation takes away (WAI-ARIA 1.2's inherited presentation, as Chromium
   has it): an item of a list, a row group, row or cell of a table, whose list or table says presentation or none */
static int ax_presented_away(ACTX, uint32_t x, int k) {
    uint32_t p = d->node[x].parent;
    if (k == E_LI) {
        int pk = p > 1 ? ax_kind(AARGS, p) : E_NONE;
        return (pk == E_UL || pk == E_OL || pk == E_MENU) && ax_says_none(AARGS, p);
    }
    if (k != E_TR && k != E_TD && k != E_TH && k != E_THEAD && k != E_TBODY && k != E_TFOOT) return 0;
    for (; p > 1; p = d->node[p].parent) {
        int pk = ax_kind(AARGS, p);
        if (pk == E_TABLE) return ax_says_none(AARGS, p);
        if (pk != E_TR && pk != E_THEAD && pk != E_TBODY && pk != E_TFOOT) return 0;
    }
    return 0;
}
/* an element's role, as a name's walk meets it: its role attribute's, or its own */
static a11y_role ax_role_of(ACTX, uint32_t x, int k) {
    a11y_role r;
    size_t bo = 0, bl = 0;
    if (ax_role_attr(AARGS, x, &r, &bo, &bl)) return r;
    return ax_implicit(AARGS, x, k);
}

static int ax_from_content(a11y_role r) {
    return r == A11Y_ROLE_LINK || r == A11Y_ROLE_BUTTON || r == A11Y_ROLE_HEADING || r == A11Y_ROLE_CELL || r == A11Y_ROLE_LAYOUT_TABLE_CELL ||
           r == A11Y_ROLE_COLUMNHEADER ||
           r == A11Y_ROLE_ROWHEADER || r == A11Y_ROLE_GRIDCELL || r == A11Y_ROLE_OPTION || r == A11Y_ROLE_TAB || r == A11Y_ROLE_MENUITEM ||
           r == A11Y_ROLE_MENUITEMCHECKBOX || r == A11Y_ROLE_MENUITEMRADIO || r == A11Y_ROLE_TERM || r == A11Y_ROLE_CHECKBOX ||
           r == A11Y_ROLE_RADIO || r == A11Y_ROLE_SWITCH || r == A11Y_ROLE_TREEITEM || r == A11Y_ROLE_TOOLTIP;
}
static int ax_is_range(a11y_role r) {
    return r == A11Y_ROLE_SLIDER || r == A11Y_ROLE_SPINBUTTON || r == A11Y_ROLE_SCROLLBAR || r == A11Y_ROLE_PROGRESSBAR || r == A11Y_ROLE_METER;
}
/* a control whose value a name it is embedded in reads (accname 1.2, 2E): a textbox, a combobox that is a select or
   an input, a listbox, a range */
static int ax_embedded(int k, a11y_role r) {
    if (r == A11Y_ROLE_TEXTBOX || r == A11Y_ROLE_SEARCHBOX || r == A11Y_ROLE_LISTBOX || ax_is_range(r)) return 1;
    return r == A11Y_ROLE_COMBOBOX && (k == E_SELECT || k == E_INPUT);
}
/* a role whose content a name leaves out where the element is nested in what is named (as Chromium does): containers
   of widgets, landmarks, live regions, and the widgets that give a value instead. A table of HTML's own is walked: inside a
   control Chromium takes it for a table of layout */
static int ax_nested_silent(a11y_role r) {
    switch (r) {
    case A11Y_ROLE_GROUP: case A11Y_ROLE_NAVIGATION: case A11Y_ROLE_TABLE: case A11Y_ROLE_TOOLBAR: case A11Y_ROLE_TABLIST:
    case A11Y_ROLE_LISTBOX: case A11Y_ROLE_TREE: case A11Y_ROLE_GRID: case A11Y_ROLE_TREEGRID: case A11Y_ROLE_RADIOGROUP:
    case A11Y_ROLE_MENUBAR: case A11Y_ROLE_MENU: case A11Y_ROLE_DIALOG: case A11Y_ROLE_ALERTDIALOG: case A11Y_ROLE_IMG:
    case A11Y_ROLE_ARTICLE: case A11Y_ROLE_SEARCH: case A11Y_ROLE_NOTE: case A11Y_ROLE_DOCUMENT: case A11Y_ROLE_APPLICATION:
    case A11Y_ROLE_FEED: case A11Y_ROLE_LOG: case A11Y_ROLE_STATUS: case A11Y_ROLE_ALERT: case A11Y_ROLE_MARQUEE:
    case A11Y_ROLE_TIMER: case A11Y_ROLE_TABPANEL: case A11Y_ROLE_COMPLEMENTARY: case A11Y_ROLE_BANNER: case A11Y_ROLE_MAIN:
    case A11Y_ROLE_CONTENTINFO: case A11Y_ROLE_FIGURE: case A11Y_ROLE_BLOCKQUOTE: case A11Y_ROLE_SEPARATOR:
    case A11Y_ROLE_ROWGROUP: case A11Y_ROLE_ROW: case A11Y_ROLE_COMBOBOX: case A11Y_ROLE_PROGRESSBAR: case A11Y_ROLE_METER:
    case A11Y_ROLE_SLIDER: case A11Y_ROLE_SPINBUTTON: case A11Y_ROLE_SCROLLBAR: case A11Y_ROLE_TEXTBOX: case A11Y_ROLE_SEARCHBOX:
        return 1;
    default: return 0;
    }
}
/* a role with states, values or a live region of its own, aria-* attributes aside: a widget, a container of widgets
   with an orientation, a range, a live region */
static int ax_stateful(a11y_role r) {
    switch (r) {
    case A11Y_ROLE_CHECKBOX: case A11Y_ROLE_RADIO: case A11Y_ROLE_SWITCH: case A11Y_ROLE_MENUITEMCHECKBOX: case A11Y_ROLE_MENUITEMRADIO:
    case A11Y_ROLE_COMBOBOX: case A11Y_ROLE_TEXTBOX: case A11Y_ROLE_SEARCHBOX: case A11Y_ROLE_LISTBOX: case A11Y_ROLE_SLIDER:
    case A11Y_ROLE_SCROLLBAR: case A11Y_ROLE_SPINBUTTON: case A11Y_ROLE_PROGRESSBAR: case A11Y_ROLE_METER: case A11Y_ROLE_SEPARATOR:
    case A11Y_ROLE_TABLIST: case A11Y_ROLE_TOOLBAR: case A11Y_ROLE_MENUBAR: case A11Y_ROLE_MENU: case A11Y_ROLE_TREE:
    case A11Y_ROLE_RADIOGROUP: case A11Y_ROLE_TREEGRID: case A11Y_ROLE_ALERT: case A11Y_ROLE_STATUS: case A11Y_ROLE_LOG:
    case A11Y_ROLE_MARQUEE: case A11Y_ROLE_TIMER:
        return 1;
    default: return 0;
    }
}
/* an element HTML lets a label name */
static int ax_labelable(int k, int ty) {
    return k == E_BUTTON || k == E_METER || k == E_OUTPUT || k == E_PROGRESS || k == E_SELECT || k == E_TEXTAREA ||
           (k == E_INPUT && ty != IT_HIDDEN);
}
static int ax_form_control(int k) { return k == E_BUTTON || k == E_INPUT || k == E_SELECT || k == E_TEXTAREA || k == E_FIELDSET; }

/* an element the keyboard reaches: a tabindex of 0 or more, else a link, a form control, an editing host ... */
static int ax_tabbable(ACTX, uint32_t x, int k) {
    if (d->node[x].first_attr) {
        if ((ax_form_control(k) || k == E_OPTION || k == E_OPTGROUP) && ax_get(AARGS, x, K_DISABLED)) return 0;
        uint32_t ti = ax_get(AARGS, x, K_TABINDEX);
        long v = 0;
        if (ti && ax_int(d, ti, &v)) return v >= 0;
        uint32_t ce = ax_get(AARGS, x, K_CONTENTEDITABLE);
        if (ce && ax_tok(d, ce, "false") != 0) return 1;
    }
    if (k == E_A || k == E_AREA) return ax_get(AARGS, x, K_HREF) != 0;
    if (k == E_BUTTON || k == E_SELECT || k == E_TEXTAREA || k == E_IFRAME) return 1;
    if (k == E_INPUT) return ax_input_type(AARGS, x) != IT_HIDDEN;
    if (k == E_SUMMARY) return ax_summary_button(AARGS, x);
    if (k == E_AUDIO || k == E_VIDEO) return ax_get(AARGS, x, K_CONTROLS) != 0;
    return 0;
}
/* an element's attributes read once: each aria-* one into sl by its kind (sl[k - A_FIRST], which the caller zeroes),
   *aria when it has any; the answer says which global ones it has: 1 any, 2 one that makes a generic element a node (a
   global state or property but a name, aria-hidden and those deprecated as global) */
#define AX_ARIA (K_COUNT - A_FIRST)
static int ax_scan(ACTX, uint32_t x, uint32_t *sl, int *aria) {
    int g = 0;
    *aria = 0;
    for (uint32_t a = d->node[x].first_attr; a; a = d->attr[a].next) {
        uint32_t nm = d->attr[a].name;
        int k = 0;
        if (nm < len(*ak)) {
            k = at(*ak, nm);
            if (k == 0xFF) {
                /* an aria-* name's kind, found once; 0xFE for one this module does not know */
                size_t n = names_len(d, nm);
                const char *p = names_ptr(d, nm);
                k = 0;
                if (n > 5 && memcmp(p, "aria-", 5) == 0) {
                    k = ax_find_name(ax_attrs, A_FIRST, K_COUNT, p, n);
                    if (!k) k = 0xFE;
                }
                (*ak)[nm] = (uint8_t)k;
            }
        }
        if (k == 0xFE) { *aria = 1; continue; }
        if (k < A_FIRST) continue;
        *aria = 1;
        if (!sl[k - A_FIRST]) sl[k - A_FIRST] = a;
        if (k == A_ATOMIC || k == A_BUSY || k == A_CONTROLS || k == A_CURRENT || k == A_DESCRIBEDBY || k == A_DESCRIPTION ||
            k == A_DETAILS || k == A_DROPEFFECT || k == A_FLOWTO || k == A_GRABBED || k == A_KEYSHORTCUTS || k == A_LIVE ||
            k == A_OWNS || k == A_RELEVANT) g |= 3;
        else if (k == A_DISABLED || k == A_ERRORMESSAGE || k == A_HASPOPUP || k == A_INVALID || k == A_LABEL || k == A_LABELLEDBY ||
                 k == A_ROLEDESCRIPTION) g |= 1;
    }
    return g;
}

/* ---- selects ---- */

/* a select's options in tree order (its own and its optgroups'), each with how it is chosen: 0 not, 1 by its
   selected attribute, 2 by default (HTML's selectedness setting: a single select keeps the last option selected,
   and one that shows a single option chooses its first enabled one when none is) */
static void ax_choose(ACTX, uint32_t s, vector<uint32_t> *opts, vector<uint8_t> *how) {
    clear(*opts);
    clear(*how);
    for (uint32_t c = d->node[s].first_child; c; c = d->node[c].next_sibling) {
        int k = ax_kind(AARGS, c);
        if (k == E_OPTION) push_back(opts, c);
        else if (k == E_OPTGROUP)
            for (uint32_t o = d->node[c].first_child; o; o = d->node[o].next_sibling) if (ax_kind(AARGS, o) == E_OPTION) push_back(opts, o);
    }
    int last = -1;
    for (size_t i = 0; i < len(*opts); i++) {
        uint8_t h = ax_get(AARGS, at(*opts, i), K_SELECTED) ? 1 : 0;
        push_back(how, h);
        if (h) last = (int)i;
    }
    if (ax_get(AARGS, s, K_MULTIPLE)) return;
    for (size_t i = 0; i < len(*how); i++) if ((int)i != last) (*how)[i] = 0;
    if (last >= 0 || ax_select_list(AARGS, s)) return;
    for (size_t i = 0; i < len(*opts); i++) {
        uint32_t o = at(*opts, i);
        uint32_t p = d->node[o].parent;
        int off = ax_get(AARGS, o, K_DISABLED) || (ax_kind(AARGS, p) == E_OPTGROUP && ax_get(AARGS, p, K_DISABLED));
        if (!off) { (*how)[i] = 2; return; }
    }
}

/* ---- names ---- */

static void ax_content(ACTX, uint32_t root, int flags, uint32_t self, vector<char> *out);
static int ax_part(ACTX, uint32_t y, int k, int flags, uint32_t self, vector<char> *out);

/* the text of the elements an ID reference list names, in order, each set off by a space: 1 when it says something,
   else nothing is added. A named element that is hidden counts whole */
static int ax_refs_text(ACTX, uint32_t a, int flags, uint32_t self, vector<char> *out) {
    size_t mark = len(*out);
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        uint32_t target = ax_by_id(AARGS, p + b, i - b);
        if (!target) continue;
        ax_space(out);
        int f2 = flags | (ax_hidden_how(AARGS, target) >= 0 || ax_invisible(AARGS, target) ? T_ALL : 0);
        if (!ax_part(AARGS, target, ax_kind(AARGS, target), f2 | T_TARGET, self, out)) ax_content(AARGS, target, f2, self, out);
    }
    if (ax_says(out, mark)) return 1;
    resize(out, mark);
    return 0;
}

/* an option's text: its label attribute, or its content */
static void ax_option_text(ACTX, uint32_t o, vector<char> *out) {
    uint32_t la = ax_get(AARGS, o, K_LABEL);
    if (la && ax_put_attr(d, la, out)) return;
    ax_content(AARGS, o, 0, 0, out);
}
/* ---- an input's value, as HTML reads it ---- */

/* p[0..n) is a valid floating-point number (HTML 2.3.4.3: no sign but a minus, no white space), finite and inside a
   float's range as Chromium keeps a number input's (its -0 made 0): 1 and *v */
static int ax_float(const char *p, size_t n, double *v) {
    size_t i = 0;
    if (i < n && p[i] == '-') i++;
    size_t d0 = i;
    while (i < n && p[i] >= '0' && p[i] <= '9') i++;
    size_t di = i - d0, df = 0;
    if (i < n && p[i] == '.') {
        i++;
        size_t f0 = i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        df = i - f0;
        if (!df) return 0;
    }
    if (!di && !df) return 0;
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        if (i < n && (p[i] == '-' || p[i] == '+')) i++;
        size_t e0 = i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        if (i == e0) return 0;
    }
    if (i != n || n >= 64) return 0;
    char buf[64];
    memcpy(buf, p, n);
    buf[n] = 0;
    char *end = buf;
    double x = strtod(buf, &end);
    if (x != x || x > 3.4028234663852886e38 || x < -3.4028234663852886e38) return 0;
    *v = x == 0 ? 0 : x;
    return 1;
}
/* the attribute a's value (raw, as HTML reads it) as a valid floating-point number: 1 and *v */
static int ax_attr_float(const dom_document *d, uint32_t a, double *v) {
    if (!a) return 0;
    amb_span s = d->attr[a].value;
    return ax_float(text_ptr(d, s), s.len, v);
}

static const int ax_mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int ax_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int ax_month_days(long y, long m) { return m == 2 && ax_leap(y) ? 29 : ax_mdays[m - 1]; }
/* the days from 1970-01-01 to y-m-dd of the proleptic Gregorian calendar */
static long ax_days(long y, long m, long dd) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
/* the weekday of a day from 1970-01-01 (a Thursday): 0 Monday ... 6 Sunday */
static long ax_weekday(long days) { return ((days % 7) + 7 + 3) % 7; }
/* the weeks of a year (ISO 8601): 53 when it starts on a Thursday, or on a Wednesday in a leap year */
static long ax_weeks(long y) {
    long wd = ax_weekday(ax_days(y, 1, 1));
    return wd == 3 || (wd == 2 && ax_leap(y)) ? 53 : 52;
}
/* least to most digits at p[*i] as a number: 1, *v and *i past them */
static int ax_digits(const char *p, size_t n, size_t *i, size_t least, size_t most, long *v) {
    size_t b = *i;
    long x = 0;
    while (*i < n && *i - b < most && p[*i] >= '0' && p[*i] <= '9') { x = x * 10 + (p[*i] - '0'); (*i)++; }
    if (*i - b < least) return 0;
    *v = x;
    return 1;
}
/* a year (four digits or more, above 0), a month, and with want_day a day of it, at p[*i] */
static int ax_ymd(const char *p, size_t n, size_t *i, long *y, long *m, long *dd, int want_day) {
    if (!ax_digits(p, n, i, 4, 9, y) || *y < 1 || *i >= n || p[*i] != '-') return 0;
    (*i)++;
    if (!ax_digits(p, n, i, 2, 2, m) || *m < 1 || *m > 12) return 0;
    if (!want_day) return 1;
    if (*i >= n || p[*i] != '-') return 0;
    (*i)++;
    return ax_digits(p, n, i, 2, 2, dd) && *dd >= 1 && *dd <= ax_month_days(*y, *m);
}
/* a time, HH:MM with :SS and a fraction of one to three digits or not, at p[*i]: its milliseconds after midnight */
static int ax_hms(const char *p, size_t n, size_t *i, double *ms) {
    long h = 0, mi = 0, sec = 0, frac = 0;
    if (!ax_digits(p, n, i, 2, 2, &h) || h > 23 || *i >= n || p[*i] != ':') return 0;
    (*i)++;
    if (!ax_digits(p, n, i, 2, 2, &mi) || mi > 59) return 0;
    double f = 0;
    if (*i < n && p[*i] == ':') {
        (*i)++;
        if (!ax_digits(p, n, i, 2, 2, &sec) || sec > 59) return 0;
        if (*i < n && p[*i] == '.') {
            (*i)++;
            size_t b = *i;
            if (!ax_digits(p, n, i, 1, 3, &frac)) return 0;
            size_t k = *i - b;
            f = (double)frac * (k == 1 ? 100 : k == 2 ? 10 : 1);
        }
    }
    *ms = (double)((h * 60 + mi) * 60 + sec) * 1000.0 + f;
    return 1;
}
/* a string of an input's type as HTML's algorithm to convert a string to a number reads it: a number's own; a date's,
   a week's (its Monday's) or a local date and time's milliseconds since the epoch, a time's since midnight; a month's
   months since 1970-01. 1 and *v; 0 when it is no valid string of the type */
static int ax_type_number(int ty, const char *p, size_t n, double *v) {
    size_t i = 0;
    long y = 0, m = 0, dd = 0, w = 0;
    double ms = 0;
    if (ty == IT_NUMBER || ty == IT_RANGE) return ax_float(p, n, v);
    if (ty == IT_DATE) {
        if (!ax_ymd(p, n, &i, &y, &m, &dd, 1) || i != n) return 0;
        *v = (double)ax_days(y, m, dd) * 86400000.0;
        return 1;
    }
    if (ty == IT_MONTH) {
        if (!ax_ymd(p, n, &i, &y, &m, &dd, 0) || i != n) return 0;
        *v = (double)((y - 1970) * 12 + m - 1);
        return 1;
    }
    if (ty == IT_WEEK) {
        if (!ax_digits(p, n, &i, 4, 9, &y) || y < 1 || i + 2 > n || p[i] != '-' || p[i + 1] != 'W') return 0;
        i += 2;
        if (!ax_digits(p, n, &i, 2, 2, &w) || i != n || w < 1 || w > ax_weeks(y)) return 0;
        long jan1 = ax_days(y, 1, 1), wd = ax_weekday(jan1);
        long mon1 = jan1 - wd + (wd > 3 ? 7 : 0);      /* week 1's Monday: the week of the year's first Thursday */
        *v = (double)(mon1 + (w - 1) * 7) * 86400000.0;
        return 1;
    }
    if (ty == IT_TIME) {
        if (!ax_hms(p, n, &i, &ms) || i != n) return 0;
        *v = ms;
        return 1;
    }
    if (ty == IT_DTL) {
        if (!ax_ymd(p, n, &i, &y, &m, &dd, 1) || i >= n || (p[i] != 'T' && p[i] != ' ')) return 0;
        i++;
        if (!ax_hms(p, n, &i, &ms) || i != n) return 0;
        *v = (double)ax_days(y, m, dd) * 86400000.0 + ms;
        return 1;
    }
    return 0;
}
static int ax_dated(int ty) { return ty >= IT_DATE && ty <= IT_DTL; }
/* an input's value as its value attribute leaves it through HTML's value sanitization (4.10.5.1): a text field's line
   breaks gone; an e-mail's and a URL's white space at either end gone too, a list of e-mails (multiple) trimmed each and
   joined by commas; a number's, a date's or a time's empty unless it is a valid one; any other's as it is */
static void ax_input_value(ACTX, uint32_t x, int ty, vector<char> *out) {
    clear(*out);
    uint32_t va = ax_get(AARGS, x, K_VALUE);
    if (!va) return;
    amb_span s = d->attr[va].value;
    const char *p = text_ptr(d, s);
    if (ty == IT_EMAIL && ax_get(AARGS, x, K_MULTIPLE)) {
        size_t i = 0;
        int first = 1;
        while (i <= s.len) {
            size_t b = i;
            while (i < s.len && p[i] != ',') i++;
            size_t e = i;
            while (b < e && ax_white(p[b])) b++;
            while (e > b && ax_white(p[e - 1])) e--;
            if (!first) push_back(out, ',');
            first = 0;
            for (size_t q = b; q < e; q++) if (p[q] != '\n' && p[q] != '\r') push_back(out, p[q]);
            i++;
        }
        return;
    }
    if (ax_text_input(ty)) { for (uint32_t i = 0; i < s.len; i++) if (p[i] != '\n' && p[i] != '\r') push_back(out, p[i]); }
    else append_range(out, p, s.len);
    if (ty == IT_EMAIL || ty == IT_URL) {
        size_t b = 0, e = len(*out);
        while (b < e && ax_white(at(*out, b))) b++;
        while (e > b && ax_white(at(*out, e - 1))) e--;
        if (b) memmove(data(*out), data(*out) + b, e - b);
        resize(out, e - b);
    }
    double v = 0;
    if ((ty == IT_NUMBER || ax_dated(ty)) && !ax_type_number(ty, data(*out), len(*out), &v)) clear(*out);
}

/* a range input's value, least and greatest values (HTML: 0 and 100, the value half way, kept in the range and put
   on its step, the nearer step and the greater of two) */
static void ax_range_input(ACTX, uint32_t x, double *now, double *mn, double *mx) {
    double lo = 0, hi = 100, step = 1, v = 0;
    if (ax_num(d, ax_get(AARGS, x, K_MIN), &v)) lo = v;
    if (ax_num(d, ax_get(AARGS, x, K_MAX), &v)) hi = v;
    uint32_t sa = ax_get(AARGS, x, K_STEP);
    int any = ax_tok(d, sa, "any") == 0;
    if (!any && ax_num(d, sa, &v) && v > 0) step = v;
    double val = hi < lo ? lo : lo + (hi - lo) / 2;
    if (ax_num(d, ax_get(AARGS, x, K_VALUE), &v)) val = v;
    if (val < lo) val = lo;
    if (hi >= lo && val > hi) val = hi;
    if (!any) {
        double n = (val - lo) / step;
        double fl = (double)(long long)n;
        if (fl > n) fl -= 1;
        double down = lo + fl * step, up = lo + (fl + 1) * step;
        if (val - down < up - val) val = down;
        else if (val != down) val = up;
        if (hi >= lo && val > hi) val = down;
    }
    *now = val;
    *mn = lo;
    *mx = hi < lo ? lo : hi;
}
/* a progress element's or a meter's value and range (HTML): 1 when it has a value */
static int ax_gauge(ACTX, uint32_t x, int k, double *now, double *mn, double *mx) {
    double v = 0;
    if (k == E_PROGRESS) {
        *mn = 0;
        *mx = ax_num(d, ax_get(AARGS, x, K_MAX), &v) && v > 0 ? v : 1;
        if (!ax_num(d, ax_get(AARGS, x, K_VALUE), &v)) return 0;
        *now = v < 0 ? 0 : v > *mx ? *mx : v;
        return 1;
    }
    *mn = ax_num(d, ax_get(AARGS, x, K_MIN), &v) ? v : 0;
    double hi = ax_num(d, ax_get(AARGS, x, K_MAX), &v) ? v : 1;
    *mx = hi >= *mn ? hi : *mn;
    double val = ax_num(d, ax_get(AARGS, x, K_VALUE), &v) ? v : 0;
    *now = val < *mn ? *mn : val > *mx ? *mx : val;
    return 1;
}
static void ax_put_num(vector<char> *out, double v) {
    char buf[40];
    int n = snprintf(buf, sizeof buf, "%g", v);
    if (n > 0) ax_put(out, buf, (size_t)n);
}
/* a range's value, least and greatest values: an input's, a progress element's or a meter's, or aria-valuenow, -valuemin and
   -valuemax with the role's defaults (WAI-ARIA: 0 and 100 but for a spinbutton; aria-valuenow kept inside them, else a
   slider's, scrollbar's or separator's half way, a spinbutton's or meter's 0; a progressbar without it is indeterminate).
   1 when it has a value, and where the value came from */
static int ax_range_values(ACTX, uint32_t x, int k, int ty, a11y_role role, int aria, double *now, double *mn, double *mx, int *has_min,
                           int *has_max, a11y_value_from *from) {
    *now = 0;
    *mn = 0;
    *mx = 0;
    *from = A11Y_VALUE_FROM_HTML;
    if (k == E_INPUT && ty == IT_RANGE) { ax_range_input(AARGS, x, now, mn, mx); *has_min = 1; *has_max = 1; return 1; }
    if (k == E_INPUT && ty == IT_NUMBER) {
        *has_min = ax_attr_float(d, ax_get(AARGS, x, K_MIN), mn);
        *has_max = ax_attr_float(d, ax_get(AARGS, x, K_MAX), mx);
        return ax_attr_float(d, ax_get(AARGS, x, K_VALUE), now);
    }
    if (k == E_PROGRESS || k == E_METER) { *has_min = 1; *has_max = 1; return ax_gauge(AARGS, x, k, now, mn, mx); }
    *has_min = aria && ax_num(d, ax_get(AARGS, x, A_VALUEMIN), mn);
    *has_max = aria && ax_num(d, ax_get(AARGS, x, A_VALUEMAX), mx);
    if (role != A11Y_ROLE_SPINBUTTON) {
        if (!*has_min) { *mn = 0; *has_min = 1; }
        if (!*has_max) { *mx = 100; *has_max = 1; }
    }
    *from = A11Y_VALUE_FROM_ARIA;
    if (aria && ax_num(d, ax_get(AARGS, x, A_VALUENOW), now)) {
        if (*has_max && *now > *mx) *now = *mx;
        if (*has_min && *now < *mn) *now = *mn;
        return 1;
    }
    *from = A11Y_VALUE_FROM_DEFAULT;
    if (role == A11Y_ROLE_SLIDER || role == A11Y_ROLE_SCROLLBAR || role == A11Y_ROLE_SEPARATOR) {
        if (*mx < *mn) *now = *mn;
        else *now = *mn + (*mx - *mn) / 2;
        return 1;
    }
    if (role == A11Y_ROLE_SPINBUTTON || role == A11Y_ROLE_METER) {
        *now = 0;
        if (*has_max && *now > *mx) *now = *mx;
        if (*has_min && *now < *mn) *now = *mn;
        return 1;
    }
    return 0;
}
/* an embedded control's value, as a name it is part of reads it (accname 1.2, 2E): a textbox's value; a combobox's or
   listbox's chosen options; a range's valuetext, else its valuenow, else its own value */
static void ax_control_value(ACTX, uint32_t y, int k, a11y_role r, vector<char> *out) {
    int ty = k == E_INPUT ? ax_input_type(AARGS, y) : -1;
    if (r == A11Y_ROLE_TEXTBOX || r == A11Y_ROLE_SEARCHBOX || (r == A11Y_ROLE_COMBOBOX && k == E_INPUT)) {
        if (k == E_INPUT && ty != IT_PASSWORD) {
            vector<char> v;
            ax_input_value(AARGS, y, ty, &v);
            ax_put(out, data(v), len(v));
        }
        else ax_content(AARGS, y, 0, 0, out);
        return;
    }
    if (k == E_SELECT) {
        vector<uint32_t> opts;
        vector<uint8_t> how;
        ax_choose(AARGS, y, &opts, &how);
        for (size_t i = 0; i < len(opts); i++) if (at(how, i)) { ax_space(out); ax_option_text(AARGS, at(opts, i), out); }
        return;
    }
    if (r == A11Y_ROLE_LISTBOX) {
        /* an ARIA listbox: its options with aria-selected="true" */
        uint32_t x = d->node[y].first_child;
        while (x) {
            int down = 1;
            if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) {
                int kx = ax_kind(AARGS, x);
                if (ax_hidden_how(AARGS, x) >= 0) down = 0;
                else if (ax_role_of(AARGS, x, kx) == A11Y_ROLE_OPTION) {
                    down = 0;
                    if (ax_tok(d, ax_get(AARGS, x, A_SELECTED), "true") == 0) { ax_space(out); ax_option_text(AARGS, x, out); }
                }
            } else down = 0;
            if (down && d->node[x].first_child) { x = d->node[x].first_child; continue; }
            while (x != y && !d->node[x].next_sibling) x = d->node[x].parent;
            if (x == y) break;
            x = d->node[x].next_sibling;
        }
        return;
    }
    /* a range: aria-valuetext, aria-valuenow as written, else its value */
    uint32_t vt = ax_get(AARGS, y, A_VALUETEXT);
    if (vt && ax_put_attr(d, vt, out)) return;
    uint32_t vn = ax_get(AARGS, y, A_VALUENOW);
    double v = 0, lo = 0, hi = 0;
    if (vn && ax_num(d, vn, &v)) { ax_put_attr(d, vn, out); return; }
    int hl = 0, hh = 0;
    a11y_value_from from = A11Y_VALUE_FROM_HTML;
    if (ax_range_values(AARGS, y, k, ty, r, 1, &v, &lo, &hi, &hl, &hh, &from)) ax_put_num(out, v);
}

/* what an element gives a text alternative it is part of, when it gives it whole (accname 1.2, step 2 in recursion):
   its aria-labelledby's elements' text (outside such a walk), an embedded control's value, its aria-label, an
   image's alt, an input button's value: 1; or 0, and its content is walked */
static int ax_part(ACTX, uint32_t y, int k, int flags, uint32_t self, vector<char> *out) {
    if (d->node[y].first_attr) {
        if (!(flags & T_REF)) {
            uint32_t lb = ax_get(AARGS, y, A_LABELLEDBY);
            if (lb && ax_refs_text(AARGS, lb, T_REF, self, out)) return 1;
        }
    }
    a11y_role r = ax_role_of(AARGS, y, k);
    if (y != self && ax_embedded(k, r)) {
        /* a control with nothing chosen and no value is read as any other element */
        size_t mark = len(*out);
        ax_control_value(AARGS, y, k, r, out);
        if (ax_says(out, mark)) return 1;
        resize(out, mark);
    }
    if (d->node[y].first_attr) {
        uint32_t al = ax_get(AARGS, y, A_LABEL);
        if (al && ax_put_attr(d, al, out)) return 1;
    }
    if (k == E_IMG || k == E_AREA) {
        uint32_t alt = ax_get(AARGS, y, K_ALT);
        if (alt) ax_put_attr(d, alt, out);
        return 1;
    }
    if (k == E_INPUT) {
        int ty = ax_input_type(AARGS, y);
        uint32_t a = 0;
        if (ty == IT_IMAGE) a = ax_get(AARGS, y, K_ALT);
        else if (ax_input_button(ty)) a = ax_get(AARGS, y, K_VALUE);
        if (a && ax_put_attr(d, a, out)) return 1;
        if (ty == IT_SUBMIT || ty == IT_IMAGE) ax_put(out, "Submit", 6);
        else if (ty == IT_RESET) ax_put(out, "Reset", 5);
        return 1;
    }
    if (!(flags & T_TARGET) && ax_nested_silent(r) && !((k == E_TABLE || k == E_TR) && !ax_get(AARGS, y, K_ROLE))) return 1;
    /* MathML gives an element it is in no text, outside a walk aria-labelledby starts (Chromium names none of its roles
       from content) */
    if (!(flags & T_REF) && ax_mathml(d, y) && (r == A11Y_ROLE_MATH || ax_mathml_only(r))) return 1;
    return 0;
}

/* a code point into out, in UTF-8 */
static void ax_utf8(vector<char> *out, uint32_t cp) {
    if (cp < 0x80) push_back(out, (char)cp);
    else if (cp < 0x800) { push_back(out, (char)(0xC0 | cp >> 6)); push_back(out, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        push_back(out, (char)(0xE0 | cp >> 12));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
    } else {
        push_back(out, (char)(0xF0 | cp >> 18));
        push_back(out, (char)(0x80 | (cp >> 12 & 0x3F)));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
    }
}
/* a CSS string, its quotes s[0] and s[n - 1], into out with its escapes read */
static void ax_unquote(const char *s, size_t n, vector<char> *out) {
    if (n < 2) return;
    for (size_t i = 1; i + 1 < n; i++) {
        if (s[i] != '\\') { push_back(out, s[i]); continue; }
        i++;
        if (i + 1 >= n) break;
        if (s[i] == '\n') continue;
        uint32_t cp = 0;
        size_t k = 0;
        while (k < 6 && i + 1 < n) {
            char c = s[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) break;
            cp = cp * 16 + (uint32_t)v;
            i++;
            k++;
        }
        if (k == 0) { push_back(out, s[i]); continue; }
        if (i + 1 < n && ax_white(s[i])) i++;
        i--;
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        ax_utf8(out, cp);
    }
}
/* the first value of x's pseudo-element pk (::before, ::after) when it generates one, else 0 */
static uint32_t ax_pseudo(ACTX, uint32_t x, int pk) {
    if (x >= len(*gen) || !at(*gen, x)) return 0;
    for (uint32_t i = at(*gen, x); i < st->pstyle_len && st->pstyle[i].node.index == x; i++)
        if ((int)st->pstyle[i].pseudo == pk) return st->pstyle[i].first;
    return 0;
}
static int ax_tt(ACTX, uint32_t c);
static void ax_put_tt(int tt, const char *s, size_t n, vector<char> *out);
/* a text node's text into out, as it is drawn: its element's text-transform applied */
static void ax_text_node(ACTX, uint32_t x, vector<char> *out) {
    uint32_t p = d->node[x].parent;
    int tt = p ? ax_tt(AARGS, ax_cval(AARGS, p, CSS_PROP_TEXT_TRANSFORM)) : 0;
    ax_put_tt(tt, text_ptr(d, d->node[x].data), d->node[x].data.len, out);
}

/* ---- quotes: their depth at each pseudo-element, as layout's walk of generated content has it ---- */

/* the quotes' depth as each element's ::before (2x) and ::after (2x + 1) begins, made at a build's first quote;
   empty until then (a document whose generated content has no quotes walks nothing for them) */
static vector<int32_t> ax_qd;
static int ax_qd_made;
/* the depth after x's pseudo-element pk's quotes, from *depth: an open quote (drawn or not) one deeper, a close one
   one shallower, never below 0 */
static void ax_quote_steps(ACTX, uint32_t x, int pk, int *depth) {
    uint32_t c0 = ax_pseudo(AARGS, x, pk);
    uint32_t cd = c0 + CSS_PROP_DISPLAY, cc = c0 + CSS_PROP_CONTENT;
    if (!c0 || cd >= st->cval_len || cc >= st->cval_len || st->cval[cc].unit != CSS_UNIT_TEXT) return;
    if (st->cval[cd].word && st->cval[cd].word == cw[W_NONE]) return;
    vector<char> src;
    amb_span sp = st->cval[cc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        const char *q = data(src) + a;
        if (n == 1 && q[0] == '/') break;
        if (ax_word_is(q, n, "open-quote") || ax_word_is(q, n, "no-open-quote")) (*depth)++;
        else if ((ax_word_is(q, n, "close-quote") || ax_word_is(q, n, "no-close-quote")) && *depth > 0) (*depth)--;
    }
}
/* the walk, in tree order (layout's lay_gen_walk): x's ::before, its children drawn (display none's subtrees left
   out), its ::after */
static void ax_quote_walk(ACTX, uint32_t x, int *depth, int level) {
    if (2 * (size_t)x + 1 < len(ax_qd)) ax_qd[2 * (size_t)x] = *depth;
    ax_quote_steps(AARGS, x, CSS_PSEUDO_BEFORE, depth);
    if (level < 1024)
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_ELEMENT && !ax_display_none(AARGS, c)) ax_quote_walk(AARGS, c, depth, level + 1);
    if (2 * (size_t)x + 1 < len(ax_qd)) ax_qd[2 * (size_t)x + 1] = *depth;
    ax_quote_steps(AARGS, x, CSS_PSEUDO_AFTER, depth);
}
/* the depth x's pseudo-element pk's content begins at */
static int ax_quote_depth(ACTX, uint32_t x, int pk) {
    if (!ax_qd_made) {
        ax_qd_made = 1;
        clear(ax_qd);
        resize(&ax_qd, 2 * (size_t)d->node_len, 0);
        int depth = 0;
        for (uint32_t c = d->node_len > 1 ? d->node[1].first_child : 0; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_ELEMENT && !ax_display_none(AARGS, c)) ax_quote_walk(AARGS, c, &depth, 0);
    }
    size_t k = 2 * (size_t)x + (pk == CSS_PSEUDO_AFTER ? 1 : 0);
    return k < len(ax_qd) ? at(ax_qd, k) : 0;
}
/* the quote of the quotes property's value qc at depth (an open one, or a close one) into out, as layout's
   lay_quote picks it: auto's English's (double quotes, then single ones), none's none, else the pair of the depth
   (the last pair past them) */
static void ax_quote(ACTX, uint32_t qc, int depth, int close, vector<char> *out) {
    if (qc >= st->cval_len) return;
    if (st->cval[qc].unit != CSS_UNIT_TEXT) {
        if (names_is(st, st->cval[qc].word, "none", 4)) return;
        if (depth == 0 && close) append_range(out, "\xe2\x80\x9d", 3);
        else if (depth == 0) append_range(out, "\xe2\x80\x9c", 3);
        else if (close) append_range(out, "\xe2\x80\x99", 3);
        else append_range(out, "\xe2\x80\x98", 3);
        return;
    }
    vector<char> src;
    amb_span sp = st->cval[qc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n, k = 0, pa = 0, pn = 0, total = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) total++;
    size_t pairs = total / 2;
    if (pairs == 0) return;
    size_t want = 2 * ((size_t)depth < pairs ? (size_t)depth : pairs - 1) + (close ? 1 : 0);
    i = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        if (k == want) { pa = a; pn = n; }
        k++;
    }
    if (pn) ax_unquote(data(src) + pa, pn, out);
}

/* the text x's ::before or ::after (pk) adds to a name it is in (accname 1.2, 2F; CSS Content 3): its content's
   alternative text, after a solidus, when it has one; else its strings, attr()s and quotes, each quote its depth's
   (an image or a counter adds nothing, as Chromium names it), in its text-transform. An alternative text, or a
   pseudo-element drawn as more than inline content, is set off by spaces, as Chromium sets it; one with display
   none, or drawn as nothing outside a walk that counts all, adds nothing */
static void ax_gen_text(ACTX, uint32_t x, int pk, int flags, vector<char> *out) {
    uint32_t c0 = ax_pseudo(AARGS, x, pk);
    uint32_t cd = c0 + CSS_PROP_DISPLAY, cv = c0 + CSS_PROP_VISIBILITY, cc = c0 + CSS_PROP_CONTENT;
    if (!c0 || cd >= st->cval_len || cv >= st->cval_len || cc >= st->cval_len) return;
    uint32_t dw = st->cval[cd].word, vw = st->cval[cv].word;
    if (dw && dw == cw[W_NONE]) return;
    if (!(flags & T_ALL) && vw && (vw == cw[W_HIDDEN] || vw == cw[W_COLLAPSE])) return;
    if (st->cval[cc].unit != CSS_UNIT_TEXT) return;
    vector<char> src, piece;
    amb_span sp = st->cval[cc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a = 0, n = 0, from = 0;
    int alt = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n))
        if (n == 1 && at(src, a) == '/') { alt = 1; from = i; break; }
    int spaced = alt || (dw && dw != cw[W_INLINE] && dw != cw[W_CONTENTS]);
    if (spaced) ax_space(out);
    int tt = ax_tt(AARGS, c0 + CSS_PROP_TEXT_TRANSFORM), depth = -1;
    i = from;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        const char *q = data(src) + a;
        if (!alt && n == 1 && q[0] == '/') break;
        clear(piece);
        int open = !alt && (ax_word_is(q, n, "open-quote") || ax_word_is(q, n, "no-open-quote"));
        int shut = !alt && (ax_word_is(q, n, "close-quote") || ax_word_is(q, n, "no-close-quote"));
        if ((open || shut) && depth < 0) depth = ax_quote_depth(AARGS, x, pk);
        if (q[0] == '"' || q[0] == '\'') ax_unquote(q, n, &piece);
        else if (open) {
            if (q[0] == 'o' || q[0] == 'O') ax_quote(AARGS, c0 + CSS_PROP_QUOTES, depth, 0, &piece);
            depth++;
        } else if (shut) {
            if (depth > 0) {
                depth--;
                if (q[0] == 'c' || q[0] == 'C') ax_quote(AARGS, c0 + CSS_PROP_QUOTES, depth, 1, &piece);
            }
        } else if (n > 6 && ax_word_is(q, 5, "attr(") && q[n - 1] == ')') {
            size_t b = 5, e = 5;
            while (b < n - 1 && ax_white(q[b])) b++;
            e = b;
            while (e < n - 1 && !ax_white(q[e]) && q[e] != ',') e++;
            uint32_t nm = e > b ? names_find(d, q + b, e - b) : 0;
            for (uint32_t r = nm ? d->node[x].first_attr : 0; r; r = d->attr[r].next)
                if (d->attr[r].name == nm) { amb_span v = d->attr[r].value; append_range(piece, text_ptr(d, v), v.len); break; }
        }
        ax_put_tt(tt, data(piece), len(piece), out);
    }
    if (spaced) ax_space(out);
}

/* the text of root's subtree for a name: its ::before, then what each element gives, or its content walked through
   (its own ::before and ::after about it), then root's ::after; an element that is not inline set off by spaces, and
   so is what an element gives whole -- an alt, a label, a control's value --, as Chromium sets it off. What is hidden
   or drawn as nothing (visibility hidden or collapse) is left out unless the walk counts all of a hidden subtree; the
   element being named (self) adds nothing to its own label. The walk is a loop: a subtree of any depth is read */
/* the element whose inline content x's text is in: x's nearest ancestor (itself too) set off as a block, the
   document's root for none (Blink's IsInSameNonInlineBlockFlow) */
static uint32_t ax_flow_of(ACTX, uint32_t x) {
    for (uint32_t p = x; p > 1; p = d->node[p].parent)
        if (d->node[p].kind == DOM_NODE_KIND_ELEMENT && ax_block(AARGS, p)) return p;
    return 1;
}
static void ax_content_child(ACTX, uint32_t x, int flags, uint32_t self, vector<char> *out);
/* the elements x owns by aria-owns, in its list's order, read after its children as children of its own: a space
   before each whose text is in another block's inline content than what came before it, as Chromium sets them */
static void ax_owned_content(ACTX, uint32_t x, int flags, uint32_t self, vector<char> *out) {
    uint32_t oa = d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].first_attr ? ax_get(AARGS, x, A_OWNS) : 0;
    if (!oa) return;
    uint32_t prev = x;
    size_t i = 0;
    while (i < d->attr[oa].value.len) {
        amb_span v = d->attr[oa].value;
        const char *p = text_ptr(d, v);
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        uint32_t y = ax_by_id(AARGS, p + b, i - b);
        if (!y || y >= len(*owner) || (at(*owner, y) & ~AX_MET) != x || y == self) continue;
        if (ax_flow_of(AARGS, prev) != ax_flow_of(AARGS, y)) ax_space(out);
        ax_content_child(AARGS, y, flags, self, out);
        prev = y;
    }
}
/* an element read as a child in a name from content: left out (a script, hidden), a space (br), an embedded control's
   part, or its content (its pseudo-elements, children and what it owns), a block's set off by spaces */
static void ax_content_child(ACTX, uint32_t x, int flags, uint32_t self, vector<char> *out) {
    if (x == self) return;
    int k = ax_kind(AARGS, x);
    if (k == E_SCRIPT || k == E_STYLE || k == E_TEMPLATE) return;
    if (!(flags & T_ALL) && (ax_hidden_how(AARGS, x) >= 0 || ax_invisible(AARGS, x))) return;
    if (k == E_BR) { ax_space(out); return; }
    int block = ax_block(AARGS, x);
    if (block) ax_space(out);
    size_t m0 = len(*out);
    ax_space(out);
    size_t m1 = len(*out);
    if (ax_part(AARGS, x, k, flags & ~T_TARGET, self, out)) {
        if (ax_says(out, m1)) ax_space(out);
        else resize(out, m0);
    } else {
        resize(out, m0);
        ax_content(AARGS, x, flags, self, out);
    }
    if (block) ax_space(out);
}

/* the class of the code point at s[i] in a word, as text-transform's capitalize splits words (layout's lay_word_class):
   1 a letter, 2 a digit, 3 _, 4 ' or a right single quote, 5 the middle dot, 6 . , or ;, 0 anything else (a
   continuation byte its code point's); its length into bl */
static int ax_word_class(const char *s, size_t n, size_t i, size_t *bl) {
    unsigned char c = (unsigned char)s[i];
    *bl = 1;
    if (c < 0x80) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
        if (c >= '0' && c <= '9') return 2;
        return c == '_' ? 3 : c == '\'' ? 4 : c == '.' || c == ',' || c == ';' ? 6 : 0;
    }
    if ((c & 0xC0) == 0x80) {
        /* the last byte of a code point: its lead byte's class */
        size_t b = i;
        while (b > 0 && ((unsigned char)s[b] & 0xC0) == 0x80) b--;
        if (b == i || ((unsigned char)s[b] & 0xC0) == 0x80) return 0;
        size_t lb;
        int cl = ax_word_class(s, n, b, &lb);
        return b + lb > i ? cl : 0;
    }
    size_t k = 1;
    while (i + k < n && ((unsigned char)s[i + k] & 0xC0) == 0x80) k++;
    *bl = k;
    unsigned char e = k > 1 ? (unsigned char)s[i + 1] : 0, e2 = k > 2 ? (unsigned char)s[i + 2] : 0;
    if (c == 0xC2) return e == 0xB7 ? 5 : e == 0xAA || e == 0xB5 || e == 0xBA ? 1 : 0;
    if (c == 0xC3) return e == 0x97 || e == 0xB7 ? 0 : 1;
    if (c == 0xE2 && e == 0x80) return e2 == 0x98 || e2 == 0x99 ? 4 : 0;
    if (c == 0xE2 && e == 0x81 && e2 <= 0xAF) return 0;
    if (c == 0xF0 && e == 0x9F) return 0;
    return 1;
}
/* math-auto's italic of a code point (MathML Core's italic mapping), or the code point itself */
static uint32_t ax_math_italic(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return 0x1D434 + (c - 'A');
    if (c == 'h') return 0x210E;
    if (c >= 'a' && c <= 'z') return 0x1D44E + (c - 'a');
    if (c >= 0x391 && c <= 0x3A1) return 0x1D6E2 + (c - 0x391);
    if (c >= 0x3A3 && c <= 0x3A9) return 0x1D6F4 + (c - 0x3A3);
    if (c >= 0x3B1 && c <= 0x3C9) return 0x1D6FC + (c - 0x3B1);
    switch (c) {
    case 0x131: return 0x1D6A4;
    case 0x237: return 0x1D6A5;
    case 0x3F4: return 0x1D6F3;
    case 0x2207: return 0x1D6FB;
    case 0x2202: return 0x1D715;
    case 0x3F5: return 0x1D716;
    case 0x3D1: return 0x1D717;
    case 0x3F0: return 0x1D718;
    case 0x3D5: return 0x1D719;
    case 0x3F1: return 0x1D71A;
    case 0x3D6: return 0x1D71B;
    default: return c;
    }
}
/* the text-transform of the computed value c (a record's index, 0 for none): 1 uppercase, 2 lowercase, 3 capitalize,
   4 math-auto, 0 none */
static int ax_tt(ACTX, uint32_t c) {
    uint32_t w = c && c < st->cval_len && st->cval[c].prop == CSS_PROP_TEXT_TRANSFORM ? st->cval[c].word : 0;
    return !w ? 0 : w == cw[W_UPPERCASE] ? 1 : w == cw[W_LOWERCASE] ? 2 : w == cw[W_CAPITALIZE] ? 3 : w == cw[W_MATH_AUTO] ? 4 : 0;
}
/* s[0, n) into out as text-transform tt draws it, as layout applies it: ASCII's and Latin-1's letters cased
   (capitalize: a word's first, a word starting after white space, or after what out ends with); math-auto's italic
   to a text of one character, as Chromium has it */
static void ax_put_tt(int tt, const char *s, size_t n, vector<char> *out) {
    if (!tt) { ax_put(out, s, n); return; }
    vector<char> tx;
    append_range(tx, s, n);
    if (tt == 4) {
        size_t b = 0, e = len(tx);
        unsigned char c0 = b < e ? (unsigned char)at(tx, b) : 0;
        size_t n = c0 < 0x80 ? 1 : c0 >= 0xF0 ? 4 : c0 >= 0xE0 ? 3 : 2;
        if (b < e && e - b == n) {
            uint32_t cp = n == 1 ? c0 : n == 2 ? (c0 & 0x1Fu) << 6 | ((unsigned char)at(tx, b + 1) & 0x3Fu)
                        : n == 3 ? (c0 & 0x0Fu) << 12 | ((unsigned char)at(tx, b + 1) & 0x3Fu) << 6 | ((unsigned char)at(tx, b + 2) & 0x3Fu)
                                 : (c0 & 0x07u) << 18 | ((unsigned char)at(tx, b + 1) & 0x3Fu) << 12 | ((unsigned char)at(tx, b + 2) & 0x3Fu) << 6 |
                                       ((unsigned char)at(tx, b + 3) & 0x3Fu);
            vector<char> it;
            append_range(it, data(tx), b);
            ax_utf8(&it, ax_math_italic(cp));
            append_range(it, data(tx) + e, len(tx) - e);
            ax_put(out, data(it), len(it));
        } else ax_put(out, data(tx), len(tx));
        return;
    }
    /* a word runs on from what out ends with */
    size_t ob;
    int in = len(*out) && back(*out) != ' ' ? ax_word_class(data(*out), len(*out), len(*out) - 1, &ob) : 0;
    int last = in >= 1 && in <= 3 ? in : 0;
    in = last != 0;
    size_t i = 0;
    while (i < len(tx)) {
        size_t bl;
        int cl = ax_word_class(data(tx), len(tx), i, &bl);
        int up = tt == 1;
        if (cl >= 1 && cl <= 3) {
            if (tt == 3 && !in) up = 1;
            in = 1;
            last = cl;
        } else if (in && cl >= 4) {
            size_t nb;
            int nc = i + bl < len(tx) ? ax_word_class(data(tx), len(tx), i + bl, &nb) : 0;
            if (!((cl != 6 && last == 1 && nc == 1) || (cl != 5 && last == 2 && nc == 2))) in = 0;
        } else in = 0;
        unsigned char ch = (unsigned char)at(tx, i);
        if (ch < 0x80) {
            if (up && ch >= 'a' && ch <= 'z') tx[i] = (char)(ch - 32);
            else if (tt == 2 && ch >= 'A' && ch <= 'Z') tx[i] = (char)(ch + 32);
        } else if (ch == 0xC3 && bl == 2) {
            unsigned char e = (unsigned char)at(tx, i + 1);
            if (up && e >= 0xA0 && e <= 0xBE && e != 0xB7) tx[i + 1] = (char)(e - 0x20);
            else if (tt == 2 && e >= 0x80 && e <= 0x9E && e != 0x97) tx[i + 1] = (char)(e + 0x20);
        }
        i += bl;
    }
    ax_put(out, data(tx), len(tx));
}

static void ax_content(ACTX, uint32_t root, int flags, uint32_t self, vector<char> *out) {
    ax_gen_text(AARGS, root, CSS_PSEUDO_BEFORE, flags, out);
    uint32_t x = d->node[root].first_child;
    while (x) {
        int down = 0;
        int nk = d->node[x].kind;
        if (nk == DOM_NODE_KIND_TEXT) ax_text_node(AARGS, x, out);
        /* an element another owns by aria-owns is read under its owner, not here */
        else if (nk == DOM_NODE_KIND_ELEMENT && x != self && !(x < len(*owner) && at(*owner, x))) {
            int k = ax_kind(AARGS, x);
            if (k == E_SCRIPT || k == E_STYLE || k == E_TEMPLATE) {}
            else if (!(flags & T_ALL) && (ax_hidden_how(AARGS, x) >= 0 || ax_invisible(AARGS, x))) {}
            else if (k == E_BR) ax_space(out);
            else {
                int block = ax_block(AARGS, x);
                if (block) ax_space(out);
                size_t m0 = len(*out);
                ax_space(out);
                size_t m1 = len(*out);
                if (ax_part(AARGS, x, k, flags & ~T_TARGET, self, out)) {
                    if (ax_says(out, m1)) ax_space(out);
                    else resize(out, m0);
                    if (block) ax_space(out);
                } else {
                    resize(out, m0);
                    ax_gen_text(AARGS, x, CSS_PSEUDO_BEFORE, flags, out);
                    if (d->node[x].first_child) down = 1;
                    else {
                        ax_owned_content(AARGS, x, flags, self, out);
                        ax_gen_text(AARGS, x, CSS_PSEUDO_AFTER, flags, out);
                        if (block) ax_space(out);
                    }
                }
            }
        }
        if (down) { x = d->node[x].first_child; continue; }
        while (x != root && !d->node[x].next_sibling) {
            x = d->node[x].parent;
            if (x != root) {
                ax_owned_content(AARGS, x, flags, self, out);
                ax_gen_text(AARGS, x, CSS_PSEUDO_AFTER, flags, out);
                if (ax_block(AARGS, x)) ax_space(out);
            }
        }
        if (x == root) break;
        x = d->node[x].next_sibling;
    }
    ax_owned_content(AARGS, root, flags, self, out);
    ax_gen_text(AARGS, root, CSS_PSEUDO_AFTER, flags, out);
}

/* the first control a label holds (HTML's labeled control of a label without for) */
static uint32_t ax_first_control(ACTX, uint32_t l) {
    uint32_t x = d->node[l].first_child;
    while (x) {
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) {
            int k = ax_kind(AARGS, x);
            int ty = k == E_INPUT ? ax_input_type(AARGS, x) : -1;
            if (ax_labelable(k, ty)) return x;
            if (d->node[x].first_child) { x = d->node[x].first_child; continue; }
        }
        while (x != l && !d->node[x].next_sibling) x = d->node[x].parent;
        if (x == l) break;
        x = d->node[x].next_sibling;
    }
    return 0;
}
/* the text of a labelable element's labels: each label whose for names it, in order; else the label around it whose
   first control it is. 1 when they say something */
static int ax_labels_text(ACTX, uint32_t x, vector<char> *out) {
    size_t mark = len(*out);
    uint32_t ida = d->node[x].first_attr ? ax_get(AARGS, x, K_ID) : 0;
    if (ida && aid[K_FOR] && d->attr[ida].value.len) {
        if (!len(*labels)) {
            push_back(labels, 0);
            for (uint32_t l = 1; l < d->node_len; l++)
                if (d->node[l].live && ax_kind(AARGS, l) == E_LABEL && ax_get(AARGS, l, K_FOR) && ax_connected(d, l)) push_back(labels, l);
        }
        amb_span idv = d->attr[ida].value;
        for (size_t q = 1; q < len(*labels); q++) {
            uint32_t l = at(*labels, q);
            uint32_t f = ax_get(AARGS, l, K_FOR);
            if (!f || d->attr[f].value.len != idv.len || memcmp(text_ptr(d, d->attr[f].value), text_ptr(d, idv), idv.len) != 0) continue;
            ax_space(out);
            ax_content(AARGS, l, ax_hidden_how(AARGS, l) >= 0 || ax_invisible(AARGS, l) ? T_ALL : 0, x, out);
        }
    }
    if (!ax_says(out, mark))
        for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent)
            if (ax_kind(AARGS, p) == E_LABEL) {
                if (!ax_get(AARGS, p, K_FOR) && ax_first_control(AARGS, p) == x) ax_content(AARGS, p, 0, x, out);
                break;
            }
    if (ax_says(out, mark)) return 1;
    resize(out, mark);
    return 0;
}

/* the attribute of the kind k of the element whose aria-* attributes sl holds (0 when it has none) */
#define AS(k) (sl ? sl[(k) - A_FIRST] : 0)

/* the node's name, and where it came from; content tells whether its content names it; sl its aria-* attributes */
static a11y_name_from ax_name_of(ACTX, const uint32_t *sl, uint32_t x, int k, a11y_role role, int content, vector<char> *out) {
    clear(*out);
    uint32_t lb = AS(A_LABELLEDBY);
    if (lb && ax_refs_text(AARGS, lb, T_REF, x, out)) return A11Y_NAME_FROM_ARIA_LABELLEDBY;
    uint32_t al = AS(A_LABEL);
    if (al && ax_put_attr(d, al, out)) return A11Y_NAME_FROM_ARIA_LABEL;
    int ty = k == E_INPUT ? ax_input_type(AARGS, x) : -1;
    if (k == E_IMG || k == E_AREA) {
        uint32_t a = ax_get(AARGS, x, K_ALT);
        if (a) { ax_put_attr(d, a, out); return A11Y_NAME_FROM_ALT; }
    }
    if (ty == IT_IMAGE) {
        uint32_t a = ax_get(AARGS, x, K_ALT);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_ALT;
    }
    if (ax_labelable(k, ty) && ax_labels_text(AARGS, x, out)) return A11Y_NAME_FROM_LABEL;
    if (ax_input_button(ty)) {
        uint32_t a = ax_get(AARGS, x, K_VALUE);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_VALUE;
    }
    uint32_t cap = 0;
    if (k == E_TABLE) cap = ax_child(AARGS, x, E_CAPTION);
    else if (k == E_FIGURE) cap = ax_child(AARGS, x, E_FIGCAPTION);
    if (cap) { ax_content(AARGS, cap, 0, 0, out); if (ax_says(out, 0)) return A11Y_NAME_FROM_CAPTION; clear(*out); }
    if (k == E_TABLE && d->node[x].first_attr) {
        uint32_t a = ax_get(AARGS, x, K_SUMMARY);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_SUMMARY;
    }
    uint32_t leg = k == E_FIELDSET ? ax_child(AARGS, x, E_LEGEND) : 0;
    if (leg) { ax_content(AARGS, leg, 0, 0, out); if (ax_says(out, 0)) return A11Y_NAME_FROM_LEGEND; clear(*out); }
    if (k == E_OPTGROUP || k == E_OPTION) {
        uint32_t a = ax_get(AARGS, x, K_LABEL);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_LABEL_ATTRIBUTE;
    }
    if (content) {
        ax_content(AARGS, x, 0, 0, out);
        if (ax_says(out, 0)) return A11Y_NAME_FROM_CONTENT;
        clear(*out);
    }
    uint32_t ti = d->node[x].first_attr ? ax_get(AARGS, x, K_TITLE) : 0;
    if (ti && ax_put_attr(d, ti, out)) return A11Y_NAME_FROM_TITLE;
    if ((k == E_INPUT && (ax_text_input(ty) || ty == IT_NUMBER)) || k == E_TEXTAREA) {
        uint32_t a = ax_get(AARGS, x, K_PLACEHOLDER);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_PLACEHOLDER;
    }
    if (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX || role == A11Y_ROLE_COMBOBOX) {
        uint32_t a = AS(A_PLACEHOLDER);
        if (a && ax_put_attr(d, a, out)) return A11Y_NAME_FROM_PLACEHOLDER;
    }
    if (ty == IT_SUBMIT || ty == IT_IMAGE) { ax_put(out, "Submit", 6); return A11Y_NAME_FROM_VALUE; }
    if (ty == IT_RESET) { ax_put(out, "Reset", 5); return A11Y_NAME_FROM_VALUE; }
    return A11Y_NAME_FROM_NOTHING;
}

/* the node's description, and where it came from */
static a11y_desc_from ax_desc_of(ACTX, const uint32_t *sl, uint32_t x, int k, a11y_name_from nf, const vector<char> *name,
                                 vector<char> *out) {
    clear(*out);
    uint32_t db = AS(A_DESCRIBEDBY);
    if (db && ax_refs_text(AARGS, db, T_REF, x, out)) return A11Y_DESC_FROM_ARIA_DESCRIBEDBY;
    uint32_t ad = AS(A_DESCRIPTION);
    if (ad && ax_put_attr(d, ad, out)) return A11Y_DESC_FROM_ARIA_DESCRIPTION;
    if (k == E_TABLE && nf != A11Y_NAME_FROM_CAPTION) {
        uint32_t cap = ax_child(AARGS, x, E_CAPTION);
        if (cap) { ax_content(AARGS, cap, 0, 0, out); if (ax_says(out, 0)) return A11Y_DESC_FROM_CAPTION; clear(*out); }
    }
    uint32_t ti = d->node[x].first_attr ? ax_get(AARGS, x, K_TITLE) : 0;
    if (ti && nf != A11Y_NAME_FROM_TITLE && ax_put_attr(d, ti, out)) {
        if (!ax_same_text(out, name)) return A11Y_DESC_FROM_TITLE;
        clear(*out);
    }
    return A11Y_DESC_FROM_NOTHING;
}

/* ---- the facts of a node ---- */

/* each id an attribute names, kept with its element */
static void ax_ref_list(ACTX, uint32_t x, uint32_t a, a11y_ref_attr kind) {
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        char id[256];
        size_t n = i - b < sizeof id - 1 ? i - b : sizeof id - 1;
        memcpy(id, p + b, n);
        id[n] = 0;
        uint32_t target = ax_by_id(AARGS, id, n);
        a11y_aref_h r = aref_new(t);
        t->aref[r.index].from = node_handle(d, x);
        t->aref[r.index].attr = kind;
        amb_span sp = text_add(t, id, n);
        t->aref[r.index].idref = sp;
        if (target) t->aref[r.index].target = node_handle(d, target);
    }
}
static void ax_refs(ACTX, const uint32_t *sl, uint32_t x, int k) {
    for (int i = 0; i < 8; i++) {
        uint32_t a = AS(ax_ref_kinds[i]);
        if (a) ax_ref_list(AARGS, x, a, ax_ref_attrs[i]);
    }
    if (k == E_LABEL) { uint32_t a = ax_get(AARGS, x, K_FOR); if (a) ax_ref_list(AARGS, x, a, A11Y_REF_ATTR_LABEL_FOR); }
}

static void ax_state(a11y_tree *t, uint32_t a, a11y_state s, a11y_token v, a11y_state_from from, dom_node_h by) {
    a11y_astate_h h = astate_new(t);
    t->astate[h.index].node = a;
    t->astate[h.index].state = s;
    t->astate[h.index].value = v;
    t->astate[h.index].from = from;
    t->astate[h.index].by = by;
}
/* aria-checked's and aria-pressed's, aria-current's and aria-haspopup's tokens, as ax_tok numbers them */
static const a11y_token ax_tristate[3] = { A11Y_TOKEN_TRUE, A11Y_TOKEN_FALSE, A11Y_TOKEN_MIXED };
static const a11y_token ax_current_tokens[7] = { A11Y_TOKEN_TRUE, A11Y_TOKEN_FALSE, A11Y_TOKEN_PAGE, A11Y_TOKEN_STEP, A11Y_TOKEN_LOCATION,
                                                 A11Y_TOKEN_DATE, A11Y_TOKEN_TIME };
static const a11y_token ax_popup_tokens[7] = { A11Y_TOKEN_MENU, A11Y_TOKEN_FALSE, A11Y_TOKEN_MENU, A11Y_TOKEN_LISTBOX, A11Y_TOKEN_TREE,
                                               A11Y_TOKEN_GRID, A11Y_TOKEN_DIALOG };

/* a state as it is found, before it is kept: the state, its value, where it came from */
typedef struct ax_found { uint8_t state, value, from; } ax_found;
#define AX_FOUND(S, V, F) do { fs[nf].state = (uint8_t)(S); fs[nf].value = (uint8_t)(V); fs[nf].from = (uint8_t)(F); nf++; } while (0)
/* an attribute that is true */
#define AX_TRUE(a) (ax_tok(d, (a), "true") == 0)

/* ---- constraint validation (HTML 4.10.20.2), of a page no one has edited ---- */

/* the flags of a control's validity state, by bit, and as the contract names them */
enum { V_MISSING = 1, V_TYPE = 2, V_PATTERN = 4, V_UNDER = 8, V_OVER = 16, V_STEP = 32, V_COUNT = 6 };
static const a11y_validity ax_validity_flags[V_COUNT] = { A11Y_VALIDITY_VALUE_MISSING, A11Y_VALIDITY_TYPE_MISMATCH,
    A11Y_VALIDITY_PATTERN_MISMATCH, A11Y_VALIDITY_RANGE_UNDERFLOW, A11Y_VALIDITY_RANGE_OVERFLOW, A11Y_VALIDITY_STEP_MISMATCH };

static int ax_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
/* p[0..n) is a valid e-mail address (HTML 4.10.5.1.5): a local part of letters, digits and .!#$%&'*+/=?^_`{|}~-, an @,
   and a domain of labels with dots between, each of 1 to 63 letters, digits and hyphens with no hyphen at either end. A
   label beyond ASCII is taken as one whose IDNA form is good, as Chromium converts it before it looks */
static int ax_email_ok(const char *p, size_t n) {
    size_t a = 0;
    while (a < n && p[a] != '@') a++;
    if (a == 0 || a + 1 >= n) return 0;
    const char *local = ".!#$%&'*+/=?^_`{|}~-";
    for (size_t i = 0; i < a; i++) {
        char c = p[i];
        int ok = ax_alnum(c);
        for (size_t q = 0; !ok && local[q]; q++) if (local[q] == c) ok = 1;
        if (!ok) return 0;
    }
    size_t i = a + 1;
    while (i <= n) {
        size_t b = i;
        while (i < n && p[i] != '.') {
            char c = p[i];
            if (!ax_alnum(c) && c != '-' && !((unsigned char)c & 0x80)) return 0;
            i++;
        }
        if (i == b || i - b > 63 || p[b] == '-' || p[i - 1] == '-') return 0;
        i++;
    }
    return 1;
}
/* p[0..n) is a valid absolute URL, as Chromium takes one: a scheme and its colon, then what the URL parser reads */
static int ax_url_ok(const char *p, size_t n) {
    if (!n || !((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'))) return 0;
    size_t i = 1;
    while (i < n && (ax_alnum(p[i]) || p[i] == '+' || p[i] == '-' || p[i] == '.')) i++;
    if (i >= n || p[i] != ':') return 0;
    vector<char> ref, out;
    append_range(ref, p, n);
    push_back(ref, 0);
    resize(out, 3 * n + 256);
    return net_url_resolve("about:blank", data(ref), data(out), len(out));
}
/* an option's value is empty: its value attribute's, else its text's (white space aside) */
static int ax_option_empty(ACTX, uint32_t o) {
    uint32_t va = ax_get(AARGS, o, K_VALUE);
    if (va) return d->attr[va].value.len == 0;
    uint32_t x = d->node[o].first_child;
    while (x) {
        if (d->node[x].kind == DOM_NODE_KIND_TEXT) {
            amb_span v = d->node[x].data;
            const char *q = text_ptr(d, v);
            for (uint32_t i = 0; i < v.len; i++) if (!ax_white(q[i])) return 0;
        }
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT && ax_kind(AARGS, x) != E_SCRIPT && d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x != o && !d->node[x].next_sibling) x = d->node[x].parent;
        if (x == o) break;
        x = d->node[x].next_sibling;
    }
    return 1;
}
/* the flags of a form control's validity state that hold (HTML 4.10.20.2), as a page no one has edited has them: no
   field is dirty, so none is too long or too short. 0 for what is barred from constraint validation (a disabled
   control, a field that is readonly, a button) or is no control. value is the input's value, sanitized */
static int ax_validity(ACTX, uint32_t x, int k, int ty, int disabled, const vector<char> *value) {
    if (disabled) return 0;
    int req = ax_get(AARGS, x, K_REQUIRED) != 0;
    if (k == E_TEXTAREA) {
        if (!req || ax_get(AARGS, x, K_READONLY)) return 0;
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_TEXT && d->node[c].data.len) return 0;
        return V_MISSING;
    }
    if (k == E_SELECT) {
        /* missing: no option chosen but its placeholder label option (a single select of one row's first option,
           its own child, whose value is empty) */
        if (!req) return 0;
        vector<uint32_t> opts;
        vector<uint8_t> how;
        ax_choose(AARGS, x, &opts, &how);
        uint32_t ph = 0;
        if (len(opts) && !ax_select_list(AARGS, x) && d->node[at(opts, 0)].parent == x && ax_option_empty(AARGS, at(opts, 0))) ph = at(opts, 0);
        for (size_t i = 0; i < len(opts); i++) if (at(how, i) && at(opts, i) != ph) return 0;
        return V_MISSING;
    }
    if (k != E_INPUT || ty == IT_HIDDEN || ax_input_button(ty) || ty == IT_IMAGE) return 0;
    int field = ax_text_input(ty) || ty == IT_NUMBER || ax_dated(ty);
    if (field && ax_get(AARGS, x, K_READONLY)) return 0;
    int f = 0;
    size_t n = len(*value);
    if (req && ((field && !n) || ty == IT_FILE || (ty == IT_CHECKBOX && !ax_get(AARGS, x, K_CHECKED)))) f |= V_MISSING;
    if (ty == IT_RADIO && x < len(*rmiss) && at(*rmiss, x)) f |= V_MISSING;
    if (!n) return f;
    const char *p = data(*value);
    /* the type of an e-mail (each of a list's) and of a URL; a text field's pattern, each of a list's e-mails */
    uint32_t pa = ax_text_input(ty) ? ax_get(AARGS, x, K_PATTERN) : 0;
    if (ty == IT_EMAIL || ty == IT_URL || pa) {
        int each = ty == IT_EMAIL && ax_get(AARGS, x, K_MULTIPLE);
        size_t i = 0;
        while (i <= n) {
            size_t b = i;
            while (i < n && !(each && p[i] == ',')) i++;
            if (ty == IT_EMAIL && !ax_email_ok(p + b, i - b)) f |= V_TYPE;
            if (ty == IT_URL && !ax_url_ok(p + b, i - b)) f |= V_TYPE;
            if (pa && js_pattern_test(text_ptr(d, d->attr[pa].value), d->attr[pa].value.len, p + b, i - b) == 0) f |= V_PATTERN;
            i++;
        }
    }
    /* a number's, a date's or a time's range, and its step from its step base: its min (with none, its value
       attribute, which is its value: on its step) */
    if (ty == IT_NUMBER || ax_dated(ty)) {
        double v = 0, lo = 0, hi = 0, sv = 0;
        if (!ax_type_number(ty, p, n, &v)) return f;
        uint32_t ma = ax_get(AARGS, x, K_MIN), xa = ax_get(AARGS, x, K_MAX);
        int has_lo = ma && ax_type_number(ty, text_ptr(d, d->attr[ma].value), d->attr[ma].value.len, &lo);
        int has_hi = xa && ax_type_number(ty, text_ptr(d, d->attr[xa].value), d->attr[xa].value.len, &hi);
        if (has_lo && v < lo) f |= V_UNDER;
        if (has_hi && v > hi) f |= V_OVER;
        uint32_t sa = ax_get(AARGS, x, K_STEP);
        if (has_lo && !(sa && ax_tok(d, sa, "any") == 0)) {
            /* the default step and the step scale factor of the type (HTML 4.10.5.1): a day, a month, a week, a minute */
            double def = ty == IT_TIME || ty == IT_DTL ? 60 : 1;
            double scale = ty == IT_DATE ? 86400000.0 : ty == IT_WEEK ? 604800000.0 : ty == IT_TIME || ty == IT_DTL ? 1000.0 : 1;
            double step = sa && ax_attr_float(d, sa, &sv) && sv > 0 ? sv : def;
            /* Chromium's rounding: a date's, a month's or a week's step whole, a time's in whole milliseconds */
            if (ty == IT_DATE || ty == IT_MONTH || ty == IT_WEEK) { step = floor(step + 0.5); if (step < 1) step = 1; }
            step *= scale;
            if (ty == IT_TIME || ty == IT_DTL) { step = floor(step + 0.5); if (step < 1) step = 1; }
            double q = fabs(v - lo), r = q - step * floor(q / step);
            double err = ty == IT_NUMBER ? step / 16777216.0 : 0;   /* what a float cannot tell apart, as Chromium allows */
            if (err < r && r < step - err) f |= V_STEP;
        }
    }
    return f;
}
/* each radio button whose group (one non-empty name in one form) holds a required button and no checked one: marked
   in rmiss, as each is missing its value (HTML 4.10.5.1.18) */
static void ax_radio_groups(ACTX) {
    resize(rmiss, d->node_len, 0);
    vector<uint32_t> rs, fm;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || ax_kind(AARGS, x) != E_INPUT || ax_input_type(AARGS, x) != IT_RADIO || !ax_connected(d, x)) continue;
        uint32_t na = ax_get(AARGS, x, K_NAME);
        if (!na || !d->attr[na].value.len) continue;
        uint32_t form = 0;
        for (uint32_t q = d->node[x].parent; q > 1 && !form; q = d->node[q].parent) if (ax_kind(AARGS, q) == E_FORM) form = q;
        push_back(rs, x);
        push_back(fm, form);
    }
    for (size_t i = 0; i < len(rs); i++) {
        uint32_t x = at(rs, i);
        if (!ax_get(AARGS, x, K_REQUIRED) || at(*rmiss, x)) continue;
        amb_span nm = d->attr[ax_get(AARGS, x, K_NAME)].value;
        int checked = 0;
        for (int pass = 0; pass < 2 && !checked; pass++)
            for (size_t j = 0; j < len(rs); j++) {
                uint32_t y = at(rs, j);
                amb_span ym = d->attr[ax_get(AARGS, y, K_NAME)].value;
                if (at(fm, j) != at(fm, i) || ym.len != nm.len || memcmp(text_ptr(d, ym), text_ptr(d, nm), nm.len) != 0) continue;
                if (pass == 0 && ax_get(AARGS, y, K_CHECKED)) { checked = 1; break; }
                if (pass == 1) (*rmiss)[y] = 1;
            }
    }
}

/* a node's states and properties, in the contract's order. sl holds its aria-* attributes; dis is where its disabled
   state came from (-1 none), dis_by the element it came from, off that it is natively disabled; sel how each option
   of a select is chosen */
static void ax_states(ACTX, const vector<uint8_t> *sel, const uint32_t *sl, uint32_t a, uint32_t x, int k, int ty, a11y_role role,
                      int dis, uint32_t dis_by, int off, int vf) {
    ax_found fs[16];
    int nf = 0, v;
    /* checked: an input's attribute; aria-checked, unchecked without it (no mixed radio or switch) */
    if (k == E_INPUT && (ty == IT_CHECKBOX || ty == IT_RADIO))
        AX_FOUND(A11Y_STATE_CHECKED, ax_get(AARGS, x, K_CHECKED) ? A11Y_TOKEN_TRUE : A11Y_TOKEN_FALSE, A11Y_STATE_FROM_HTML);
    else if (role == A11Y_ROLE_CHECKBOX || role == A11Y_ROLE_RADIO || role == A11Y_ROLE_SWITCH || role == A11Y_ROLE_MENUITEMCHECKBOX ||
             role == A11Y_ROLE_MENUITEMRADIO) {
        v = ax_tok(d, AS(A_CHECKED), "true false mixed");
        if (v == 2 && role != A11Y_ROLE_CHECKBOX && role != A11Y_ROLE_MENUITEMCHECKBOX) v = -1;
        if (v >= 0) AX_FOUND(A11Y_STATE_CHECKED, ax_tristate[v], A11Y_STATE_FROM_ARIA);
        else AX_FOUND(A11Y_STATE_CHECKED, A11Y_TOKEN_FALSE, A11Y_STATE_FROM_IMPLICIT);
    }
    /* selected: an option's selectedness (a disabled option is none to choose, as Chromium has it); aria-selected */
    uint8_t chosen = 0;
    if (k == E_OPTION && x < len(*sel)) chosen = at(*sel, x);
    if (off) chosen = 4;
    if (chosen == 1) AX_FOUND(A11Y_STATE_SELECTED, A11Y_TOKEN_FALSE, A11Y_STATE_FROM_HTML);
    else if (chosen == 2) AX_FOUND(A11Y_STATE_SELECTED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (chosen == 3) AX_FOUND(A11Y_STATE_SELECTED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_COMPUTED);
    else if (!off && (v = ax_tok(d, AS(A_SELECTED), "true false")) >= 0) AX_FOUND(A11Y_STATE_SELECTED, ax_tristate[v], A11Y_STATE_FROM_ARIA);
    /* expanded: a summary's details open; a select closed; aria-expanded */
    if (k == E_SUMMARY && role == A11Y_ROLE_BUTTON)
        AX_FOUND(A11Y_STATE_EXPANDED, ax_get(AARGS, d->node[x].parent, K_OPEN) ? A11Y_TOKEN_TRUE : A11Y_TOKEN_FALSE, A11Y_STATE_FROM_HTML);
    else if (k == E_SELECT && role == A11Y_ROLE_COMBOBOX) AX_FOUND(A11Y_STATE_EXPANDED, A11Y_TOKEN_FALSE, A11Y_STATE_FROM_IMPLICIT);
    else if ((v = ax_tok(d, AS(A_EXPANDED), "true false")) >= 0) AX_FOUND(A11Y_STATE_EXPANDED, ax_tristate[v], A11Y_STATE_FROM_ARIA);
    if ((v = ax_tok(d, AS(A_PRESSED), "true false mixed")) >= 0) AX_FOUND(A11Y_STATE_PRESSED, ax_tristate[v], A11Y_STATE_FROM_ARIA);
    if (dis >= 0) AX_FOUND(A11Y_STATE_DISABLED, A11Y_TOKEN_TRUE, dis);
    /* required, readonly: an input's attribute where its type takes it; aria-required, aria-readonly but on an editing
       host, whose content is edited whatever it says (as Chromium has it) */
    int texty = k == E_INPUT && (ax_text_input(ty) || ty == IT_NUMBER || ty >= IT_DATE);
    if ((texty || (k == E_INPUT && (ty == IT_CHECKBOX || ty == IT_RADIO)) || k == E_SELECT || k == E_TEXTAREA) && ax_get(AARGS, x, K_REQUIRED))
        AX_FOUND(A11Y_STATE_REQUIRED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (AX_TRUE(AS(A_REQUIRED))) AX_FOUND(A11Y_STATE_REQUIRED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    uint32_t ce = k != E_INPUT && k != E_TEXTAREA ? ax_get(AARGS, x, K_CONTENTEDITABLE) : 0;
    int editing = ce && ax_tok(d, ce, "false") != 0;
    if ((texty || k == E_TEXTAREA) && ax_get(AARGS, x, K_READONLY)) AX_FOUND(A11Y_STATE_READONLY, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (!editing && AX_TRUE(AS(A_READONLY))) AX_FOUND(A11Y_STATE_READONLY, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    /* invalid and current: any value but false is true (WAI-ARIA 1.2). Without aria-invalid, invalid is HTML's
       constraint validation's (vf): a value of the wrong type, off its pattern, outside its range or off its step, or
       a control missing its value -- but a text field, whose missing value Chromium leaves untold */
    v = ax_tok(d, AS(A_INVALID), "true false grammar spelling");
    int text_field = (k == E_INPUT && (ax_text_input(ty) || ty == IT_NUMBER)) || k == E_TEXTAREA;
    if (v == 0 || v == -1) AX_FOUND(A11Y_STATE_INVALID, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    else if (v >= 2) AX_FOUND(A11Y_STATE_INVALID, v == 2 ? A11Y_TOKEN_GRAMMAR : A11Y_TOKEN_SPELLING, A11Y_STATE_FROM_ARIA);
    else if (v == -2 && ((vf & ~V_MISSING) || ((vf & V_MISSING) && !text_field)))
        AX_FOUND(A11Y_STATE_INVALID, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    v = ax_tok(d, AS(A_CURRENT), "true false page step location date time");
    if (v == -1) v = 0;
    if (v >= 0 && v != 1) AX_FOUND(A11Y_STATE_CURRENT, ax_current_tokens[v], A11Y_STATE_FROM_ARIA);
    if (AX_TRUE(AS(A_BUSY))) AX_FOUND(A11Y_STATE_BUSY, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    /* haspopup: true is a menu; a combobox's is a listbox, a select's a menu (as Chromium has it) */
    v = ax_tok(d, AS(A_HASPOPUP), "true false menu listbox tree grid dialog");
    if (v >= 0 && v != 1) AX_FOUND(A11Y_STATE_HASPOPUP, ax_popup_tokens[v], A11Y_STATE_FROM_ARIA);
    else if (v < 0 && role == A11Y_ROLE_COMBOBOX)
        AX_FOUND(A11Y_STATE_HASPOPUP, k == E_SELECT ? A11Y_TOKEN_MENU : A11Y_TOKEN_LISTBOX, A11Y_STATE_FROM_IMPLICIT);
    /* orientation, where the role has one, with its default */
    int horiz = role == A11Y_ROLE_SLIDER || role == A11Y_ROLE_SEPARATOR || role == A11Y_ROLE_TABLIST || role == A11Y_ROLE_TOOLBAR ||
                role == A11Y_ROLE_MENUBAR;
    int vert = role == A11Y_ROLE_SCROLLBAR || role == A11Y_ROLE_LISTBOX || role == A11Y_ROLE_MENU || role == A11Y_ROLE_TREE;
    if (horiz || vert || role == A11Y_ROLE_RADIOGROUP || role == A11Y_ROLE_TREEGRID) {
        v = ax_tok(d, AS(A_ORIENTATION), "horizontal vertical");
        if (v >= 0) AX_FOUND(A11Y_STATE_ORIENTATION, v == 0 ? A11Y_TOKEN_HORIZONTAL : A11Y_TOKEN_VERTICAL, A11Y_STATE_FROM_ARIA);
        else if (horiz || vert) AX_FOUND(A11Y_STATE_ORIENTATION, horiz ? A11Y_TOKEN_HORIZONTAL : A11Y_TOKEN_VERTICAL, A11Y_STATE_FROM_IMPLICIT);
    }
    if (k == E_SELECT && ax_get(AARGS, x, K_MULTIPLE)) AX_FOUND(A11Y_STATE_MULTISELECTABLE, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (AX_TRUE(AS(A_MULTISELECTABLE))) AX_FOUND(A11Y_STATE_MULTISELECTABLE, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    if (AX_TRUE(AS(A_MODAL))) AX_FOUND(A11Y_STATE_MODAL, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    /* autocomplete: aria-autocomplete (none is none); an input with a list */
    v = ax_tok(d, AS(A_AUTOCOMPLETE), "inline list both none");
    if (v >= 0 && v <= 2) AX_FOUND(A11Y_STATE_AUTOCOMPLETE, v == 0 ? A11Y_TOKEN_INLINE : v == 1 ? A11Y_TOKEN_LIST : A11Y_TOKEN_BOTH, A11Y_STATE_FROM_ARIA);
    else if (v != 3 && k == E_INPUT && ax_get(AARGS, x, K_LIST)) AX_FOUND(A11Y_STATE_AUTOCOMPLETE, A11Y_TOKEN_LIST, A11Y_STATE_FROM_HTML);
    /* multiline: a textarea; aria-multiline; an editing host that is a textbox takes lines, as Chromium has it */
    v = ax_tok(d, AS(A_MULTILINE), "true false");
    if (k == E_TEXTAREA) AX_FOUND(A11Y_STATE_MULTILINE, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (v == 0) AX_FOUND(A11Y_STATE_MULTILINE, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    else if (v != 1 && editing && (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX))
        AX_FOUND(A11Y_STATE_MULTILINE, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    /* kept, in their order: each by the node's own element but disabled, by the element it came from */
    for (int i = 0; i < nf; i++) {
        uint32_t by = fs[i].state == A11Y_STATE_DISABLED ? dis_by : x;
        ax_state(t, a, (a11y_state)fs[i].state, (a11y_token)fs[i].value, (a11y_state_from)fs[i].from, node_handle(d, by));
    }
}

/* a text of the node a kept: trimmed, or as it is when keep says it is a field's value; nothing when it is empty */
static void ax_text(a11y_tree *t, uint32_t a, a11y_text_prop prop, const vector<char> *v, a11y_desc_from from, int keep) {
    amb_span sp;
    if (keep) sp = text_add(t, data(*v), len(*v));
    else sp = ax_name(t, v);
    if (!sp.len) return;
    a11y_atext_h h = atext_new(t);
    t->atext[h.index].node = a;
    t->atext[h.index].prop = prop;
    t->atext[h.index].text = sp;
    t->atext[h.index].desc_from = from;
}

/* a node's values: a range's (its element's own, or aria-*'s with the role's defaults), a field's value (pick a select's
   chosen option) and placeholder; sl its aria-* attributes, scratch a text for the while */
static void ax_values(ACTX, const uint32_t *sl, vector<char> *scratch, uint32_t pick, uint32_t a, uint32_t x, int k, int ty, a11y_role role,
                      int focusable) {
    int aria = sl != 0;
    if (ax_is_range(role) || (role == A11Y_ROLE_SEPARATOR && focusable)) {
        double now = 0, mn = 0, mx = 0;
        int has_min = 0, has_max = 0;
        a11y_value_from from = A11Y_VALUE_FROM_HTML;
        int has_now = ax_range_values(AARGS, x, k, ty, role, aria, &now, &mn, &mx, &has_min, &has_max, &from);
        a11y_arange_h h = arange_new(t);
        t->arange[h.index].node = a;
        t->arange[h.index].valuenow = now;
        t->arange[h.index].valuemin = mn;
        t->arange[h.index].valuemax = mx;
        t->arange[h.index].has_now = (uint8_t)has_now;
        t->arange[h.index].has_min = (uint8_t)has_min;
        t->arange[h.index].has_max = (uint8_t)has_max;
        t->arange[h.index].now_from = from;
        uint32_t vt = AS(A_VALUETEXT);
        clear(*scratch);
        if (vt && ax_put_attr(d, vt, scratch)) ax_text(t, a, A11Y_TEXT_PROP_VALUETEXT, scratch, A11Y_DESC_FROM_NOTHING, 0);
    }
    /* a field's value; a select's chosen option; an ARIA textbox's or combobox's content */
    clear(*scratch);
    if (k == E_INPUT && (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX || role == A11Y_ROLE_COMBOBOX)) {
        vector<char> v;
        ax_input_value(AARGS, x, ty, &v);
        if (ty == IT_PASSWORD) {
            for (size_t i = 0; i < len(v); i++) if (((unsigned char)at(v, i) & 0xC0) != 0x80) append_range(scratch, "\xe2\x80\xa2", 3);
        } else append_range(scratch, data(v), len(v));
    } else if (k == E_TEXTAREA) {
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_TEXT) append_range(scratch, text_ptr(d, d->node[c].data), d->node[c].data.len);
    } else if (k == E_SELECT && role == A11Y_ROLE_COMBOBOX) {
        if (pick) ax_option_text(AARGS, pick, scratch);
    } else if (k != E_INPUT && k != E_SELECT &&
               (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX || (role == A11Y_ROLE_COMBOBOX && (focusable || k == E_BUTTON))))
        /* a combobox's content is its value when it takes focus, a menu button's as Chromium has it; one that holds a
           field and a list has none */
        ax_content(AARGS, x, 0, 0, scratch);
    if (len(*scratch)) ax_text(t, a, A11Y_TEXT_PROP_VALUE, scratch, A11Y_DESC_FROM_NOTHING, k == E_INPUT || k == E_TEXTAREA);
    /* its placeholder */
    uint32_t ph = 0;
    if ((k == E_INPUT && (ax_text_input(ty) || ty == IT_NUMBER)) || k == E_TEXTAREA) ph = ax_get(AARGS, x, K_PLACEHOLDER);
    if (!ph && (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX || role == A11Y_ROLE_COMBOBOX)) ph = AS(A_PLACEHOLDER);
    clear(*scratch);
    if (ph && ax_put_attr(d, ph, scratch)) ax_text(t, a, A11Y_TEXT_PROP_PLACEHOLDER, scratch, A11Y_DESC_FROM_NOTHING, 0);
}

/* a live region's politeness (aria-live, or its role's), atomicity and relevant changes; sl its aria-* attributes. 1 when
   the node is a live region */
static int ax_live(ACTX, const uint32_t *sl, uint32_t a, a11y_role role) {
    int v = ax_tok(d, AS(A_LIVE), "off polite assertive");
    a11y_politeness p = A11Y_POLITENESS_NOT_LIVE;
    if (v == 0) p = A11Y_POLITENESS_OFF;
    else if (v == 1) p = A11Y_POLITENESS_POLITE;
    else if (v == 2) p = A11Y_POLITENESS_ASSERTIVE;
    else if (role == A11Y_ROLE_ALERT) p = A11Y_POLITENESS_ASSERTIVE;
    else if (role == A11Y_ROLE_STATUS || role == A11Y_ROLE_LOG) p = A11Y_POLITENESS_POLITE;
    else if (role == A11Y_ROLE_MARQUEE || role == A11Y_ROLE_TIMER) p = A11Y_POLITENESS_OFF;
    if (p == A11Y_POLITENESS_NOT_LIVE) return 0;
    int at_ = ax_tok(d, AS(A_ATOMIC), "true false");
    int atomic = at_ == 0 || (at_ < 0 && (role == A11Y_ROLE_ALERT || role == A11Y_ROLE_STATUS));
    /* aria-relevant's tokens, in their own order; all alone; additions text when it names none */
    int adds = 0, rems = 0, txt = 0, all = 0;
    uint32_t ra = AS(A_RELEVANT);
    if (ra) {
        amb_span s = d->attr[ra].value;
        const char *q = text_ptr(d, s);
        size_t i = 0;
        while (i < s.len) {
            while (i < s.len && ax_white(q[i])) i++;
            size_t b = i;
            while (i < s.len && !ax_white(q[i])) i++;
            if (ax_word_is(q + b, i - b, "additions")) adds = 1;
            else if (ax_word_is(q + b, i - b, "removals")) rems = 1;
            else if (ax_word_is(q + b, i - b, "text")) txt = 1;
            else if (ax_word_is(q + b, i - b, "all")) all = 1;
        }
    }
    if (!adds && !rems && !txt && !all) adds = txt = 1;
    char buf[40];
    size_t n = 0;
    if (all) { memcpy(buf, "all", 3); n = 3; }
    else {
        if (adds) { memcpy(buf + n, "additions ", 10); n += 10; }
        if (rems) { memcpy(buf + n, "removals ", 9); n += 9; }
        if (txt) { memcpy(buf + n, "text ", 5); n += 5; }
        n--;
    }
    amb_span sp = text_add(t, buf, n);
    a11y_alive_h h = alive_new(t);
    t->alive[h.index].node = a;
    t->alive[h.index].politeness = p;
    t->alive[h.index].atomic = (uint8_t)atomic;
    t->alive[h.index].relevant = sp;
    return 1;
}

/* a hidden subtree's root, why it is hidden, and what inside it matters: what takes focus inside aria-hidden content
   (which is drawn, so the keyboard reaches it), and every live region */
static int ax_live_element(ACTX, uint32_t y, int k) {
    if (d->node[y].first_attr) {
        int v = ax_tok(d, ax_get(AARGS, y, A_LIVE), "off polite assertive");
        if (v >= 0) return v > 0;
    }
    a11y_role r = ax_role_of(AARGS, y, k);
    return r == A11Y_ROLE_ALERT || r == A11Y_ROLE_STATUS || r == A11Y_ROLE_LOG;
}
static int ax_drawn_within(ACTX, uint32_t root, uint32_t y) {
    for (uint32_t p = y; p && p != root; p = d->node[p].parent)
        if (ax_get(AARGS, p, K_HIDDEN) || ax_display_none(AARGS, p)) return 0;
    return 1;
}
static void ax_inside(a11y_tree *t, const dom_document *d, uint32_t y, uint32_t root, a11y_inside_what what) {
    a11y_ahinside_h h = ahinside_new(t);
    t->ahinside[h.index].node = node_handle(d, y);
    t->ahinside[h.index].root = node_handle(d, root);
    t->ahinside[h.index].what = what;
}
static void ax_hidden_root(ACTX, uint32_t x, int how) {
    a11y_ahidden_h h = ahidden_new(t);
    t->ahidden[h.index].node = node_handle(d, x);
    t->ahidden[h.index].how = (a11y_hidden_how)how;
    uint32_t y = x;
    while (y) {
        if (d->node[y].kind == DOM_NODE_KIND_ELEMENT) {
            int k = ax_kind(AARGS, y);
            if (how == A11Y_HIDDEN_HOW_ARIA_HIDDEN && ax_tabbable(AARGS, y, k) && ax_drawn_within(AARGS, x, y))
                ax_inside(t, d, y, x, A11Y_INSIDE_WHAT_FOCUSABLE);
            if (ax_live_element(AARGS, y, k)) ax_inside(t, d, y, x, A11Y_INSIDE_WHAT_LIVE_REGION);
            if (d->node[y].first_child) { y = d->node[y].first_child; continue; }
        }
        while (y != x && !d->node[y].next_sibling) y = d->node[y].parent;
        if (y == x) break;
        y = d->node[y].next_sibling;
    }
}

/* ---- the tree ---- */

/* an element whose children are being walked, with what they inherit from it and its ancestors */
typedef struct ax_frame {
    uint32_t x, next;         /* the element, and the next child to visit */
    uint32_t parent;          /* the node its children join */
    uint32_t fs, fs_up;       /* the disabled fieldset whose controls are disabled, and the one in effect outside it */
    uint32_t fs_legend;       /* that fieldset's first legend, whose content it leaves enabled */
    uint32_t og;              /* the disabled select or optgroup whose options are disabled */
    uint32_t ad;              /* the element with aria-disabled="true" whose focusable descendants are disabled */
    uint32_t live;            /* the live region (a node) its children are inside */
    uint32_t lists, items;    /* the lists its children are inside, and the groups of a tree (a tree item's level) */
    uint32_t form;            /* the form its children are inside */
    uint32_t oa, op;          /* its aria-owns attribute, and the place in it of the next id whose element it owns */
} ax_frame;
/* a radio button with a name, for its group: its node, its form, its name attribute */
typedef struct ax_radio { uint32_t node, form, name; } ax_radio;

/* never drawn by its nature, so neither in the tree nor hidden from it */
static int ax_undrawn(ACTX, uint32_t x, int k) {
    if (k == E_HEAD || k == E_SCRIPT || k == E_STYLE || k == E_TEMPLATE || k == E_TITLE || k == E_META || k == E_LINK ||
        k == E_BASE || k == E_DATALIST || k == E_PARAM || k == E_SOURCE || k == E_TRACK || k == E_AREA) return 1;
    return k == E_INPUT && ax_input_type(AARGS, x) == IT_HIDDEN;
}

/* one element met by the walk, its parent's frame f: its node and facts made, and g its children's frame. 1 when
   its children are walked */
static int ax_visit(ACTX, vector<uint8_t> *sel, vector<ax_radio> *radios, vector<char> *name, vector<char> *desc, const ax_frame *f,
                    uint32_t c, ax_frame *g) {
    int k = ax_kind(AARGS, c);
    if (ax_undrawn(AARGS, c, k)) return 0;
    int how = ax_hidden_how(AARGS, c);
    if (how >= 0) { ax_hidden_root(AARGS, c, how); return 0; }
    *g = *f;
    g->x = c;
    g->next = d->node[c].first_child;
    g->fs_legend = 0;
    g->oa = d->node[c].first_attr ? ax_get(AARGS, c, A_OWNS) : 0;
    g->op = 0;
    if (c == f->fs_legend) { g->fs = f->fs_up; g->fs_up = 0; }
    int has_attrs = d->node[c].first_attr != 0;
    int ty = k == E_INPUT ? ax_input_type(AARGS, c) : -1;
    /* its role */
    a11y_role role = A11Y_ROLE_GENERIC;
    size_t bad_off = 0, bad_len = 0;
    int stated = ax_role_attr(AARGS, c, &role, &bad_off, &bad_len);
    if (bad_len) {
        a11y_badrole_h br = badrole_new(t);
        t->badrole[br.index].node = node_handle(d, c);
        uint32_t ra = ax_get(AARGS, c, K_ROLE);
        char word[256];
        size_t n = bad_len < sizeof word ? bad_len : sizeof word;
        memcpy(word, text_ptr(d, d->attr[ra].value) + bad_off, n);
        amb_span sp = text_add(t, word, n);
        t->badrole[br.index].value = sp;
    }
    if (!stated) role = ax_implicit(AARGS, c, k);
    int aria = 0, glob = 0;
    uint32_t slots[AX_ARIA];
    if (has_attrs) { memset(slots, 0, sizeof slots); glob = ax_scan(AARGS, c, slots, &aria); }
    const uint32_t *sl = 0;
    if (aria) sl = slots;
    int tab = ax_tabbable(AARGS, c, k);
    /* role=none on an element that takes focus or has a global aria-* attribute: its own role is kept */
    if (stated && role == A11Y_ROLE_PRESENTATION && (tab || glob)) { role = ax_implicit(AARGS, c, k); stated = 0; }
    /* a list's item, a table's row or cell, whose list or table says presentation, is presentation too, unless it
       takes focus (a global aria-* attribute does not keep it, as Chromium has it) */
    else if (!stated && !tab && ax_presented_away(AARGS, c, k)) role = A11Y_ROLE_PRESENTATION;
    if (aria) ax_refs(AARGS, sl, c, k);
    else if (k == E_LABEL && has_attrs) ax_refs(AARGS, 0, c, k);
    /* disabled: its own disabled attribute; a disabled fieldset's, optgroup's or select's; aria-disabled, its own or a
       focusable element's ancestor's */
    int dis = -1, native_dis = 0;
    uint32_t by = 0;
    int own_dis = has_attrs && (ax_form_control(k) || k == E_OPTION || k == E_OPTGROUP) && ax_get(AARGS, c, K_DISABLED);
    if (own_dis) { dis = A11Y_STATE_FROM_HTML; by = c; native_dis = 1; }
    else if (ax_form_control(k) && g->fs) { dis = A11Y_STATE_FROM_INHERITED; by = g->fs; native_dis = 1; }
    else if ((k == E_OPTION || k == E_OPTGROUP) && f->og) { dis = A11Y_STATE_FROM_INHERITED; by = f->og; native_dis = 1; }
    else if (AX_TRUE(AS(A_DISABLED))) { dis = A11Y_STATE_FROM_ARIA; by = c; }
    else if (f->ad && tab) { dis = A11Y_STATE_FROM_INHERITED; by = f->ad; }
    int focusable = tab && !native_dis;
    /* what its children inherit */
    if (k == E_FIELDSET && own_dis) {
        uint32_t outer = g->fs;
        g->fs = c;
        g->fs_up = outer;
        g->fs_legend = ax_child(AARGS, c, E_LEGEND);
    }
    if ((k == E_SELECT && native_dis) || (k == E_OPTGROUP && own_dis)) g->og = c;
    if (dis == A11Y_STATE_FROM_ARIA) g->ad = c;
    if (k == E_FORM) g->form = c;
    uint32_t pick = 0;
    if (k == E_SELECT) {
        /* how each of its options is chosen, for their states; the first chosen, for its value */
        vector<uint32_t> opts;
        vector<uint8_t> chosen;
        ax_choose(AARGS, c, &opts, &chosen);
        if (len(opts) && len(*sel) < d->node_len) resize(sel, d->node_len);
        for (size_t i = 0; i < len(opts); i++) {
            (*sel)[at(opts, i)] = (uint8_t)(1 + at(chosen, i));
            if (!pick && at(chosen, i)) pick = at(opts, i);
        }
    }
    /* no node: a generic or presentational element, unless a generic one takes focus or has a global state; an
       element drawn as nothing (visibility hidden or collapse), whose children are walked, as one may be drawn */
    int node = role != A11Y_ROLE_GENERIC && role != A11Y_ROLE_PRESENTATION;
    if (role == A11Y_ROLE_GENERIC && (focusable || (glob & 2))) node = 1;
    int invisible = ax_invisible(AARGS, c);
    if (invisible) node = 0;
    if (!node) {
        if (aria && !invisible) {
            a11y_aplain_h ph = aplain_new(t);
            t->aplain[ph.index].node = node_handle(d, c);
            t->aplain[ph.index].role = role;
        }
        return 1;
    }
    a11y_anode_h h = anode_new(t);
    uint32_t a = h.index;
    t->anode[a].node = node_handle(d, c);
    t->anode[a].role = role;
    t->anode[a].stated = (uint8_t)stated;
    t->anode[a].parent = f->parent;
    t->anode[a].focusable = (uint8_t)focusable;
    t->anode[a].region = f->live;
    /* a row is named from its content in a grid, as Chromium names it */
    uint32_t up = f->parent;
    if (up && t->anode[up].role == A11Y_ROLE_ROWGROUP) up = t->anode[up].parent;
    int in_grid = up && (t->anode[up].role == A11Y_ROLE_GRID || t->anode[up].role == A11Y_ROLE_TREEGRID);
    /* a role attribute's math is named from its content too, as Chromium names it; MathML's math is not */
    int content = ax_from_content(role) || (role == A11Y_ROLE_ROW && in_grid) || (role == A11Y_ROLE_MATH && !ax_mathml(d, c));
    a11y_name_from nf = ax_name_of(AARGS, sl, c, k, role, content, name);
    amb_span nsp = ax_name(t, name);
    t->anode[a].name = nsp;
    t->anode[a].name_from = nf;
    if (has_attrs || k == E_TABLE) {
        a11y_desc_from df = ax_desc_of(AARGS, sl, c, k, nf, name, desc);
        if (df != A11Y_DESC_FROM_NOTHING) ax_text(t, a, A11Y_TEXT_PROP_DESCRIPTION, desc, df, 0);
    }
    /* states, values and a live region come from aria-* attributes, form controls, the roles of widgets and live
       regions, and a disabled ancestor: a node with none of those (a paragraph, a list item, a cell ...) has none */
    int live = 0, vf = 0;
    if (k == E_INPUT || k == E_SELECT || k == E_TEXTAREA) {
        vector<char> v;
        if (k == E_INPUT) ax_input_value(AARGS, c, ty, &v);
        vf = ax_validity(AARGS, c, k, ty, native_dis, &v);
    }
    if (sl || dis >= 0 || ax_form_control(k) || k == E_OPTION || k == E_OPTGROUP || k == E_SUMMARY || k == E_PROGRESS ||
        k == E_METER || k == E_OUTPUT || ax_stateful(role)) {
        ax_states(AARGS, sel, sl, a, c, k, ty, role, dis, by, native_dis, vf);
        ax_values(AARGS, sl, desc, pick, a, c, k, ty, role, focusable);
        live = ax_live(AARGS, sl, a, role);
    }
    for (int b = 0; b < V_COUNT; b++)
        if (vf & (1 << b)) {
            a11y_avalid_h vh = avalid_new(t);
            t->avalid[vh.index].node = a;
            t->avalid[vh.index].flag = ax_validity_flags[b];
        }
    /* its level */
    long lv = 0;
    int given = ax_int(d, AS(A_LEVEL), &lv) && lv >= 1;
    if (role == A11Y_ROLE_HEADING) {
        if (!given) lv = k >= E_H1 && k <= E_H6 ? k - E_H1 + 1 : 2;
    } else if (role == A11Y_ROLE_LISTITEM) { if (!given) lv = f->lists > 1 ? f->lists : 1; }   /* 1 in no list, as Chromium has it */
    else if (role == A11Y_ROLE_TREEITEM) { if (!given) lv = f->items + 1; }
    else if (role != A11Y_ROLE_ROW || !given) lv = 0;
    t->anode[a].level = (uint32_t)(lv > 0 && lv < 100000 ? lv : 0);
    if (k == E_INPUT && ty == IT_RADIO) {
        uint32_t na = ax_get(AARGS, c, K_NAME);
        if (na && d->attr[na].value.len) { ax_radio r = { a, f->form, na }; push_back(radios, r); }
    }
    g->parent = a;
    if (live) g->live = a;
    if (role == A11Y_ROLE_LIST) g->lists = f->lists + 1;
    /* a tree item's level counts the groups it is in, up to its tree, or all of them outside one (as Chromium counts) */
    if (role == A11Y_ROLE_TREE) g->items = 0;
    if (role == A11Y_ROLE_GROUP) g->items = f->items + 1;
    return 1;
}

/* the next element the frame's element owns by aria-owns, from the id at f->op on: met now, so marked; 0 when none
   is left */
static uint32_t ax_next_owned(ACTX, ax_frame *f) {
    if (!f->oa) return 0;
    amb_span v = d->attr[f->oa].value;
    const char *p = text_ptr(d, v);
    size_t i = f->op;
    while (i < v.len) {
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        uint32_t y = ax_by_id(AARGS, p + b, i - b);
        if (y && y < len(*owner) && at(*owner, y) == f->x) {
            (*owner)[y] |= AX_MET;
            f->op = (uint32_t)i;
            return y;
        }
    }
    f->op = (uint32_t)v.len;
    f->oa = 0;
    return 0;
}

/* an element and every element above it are walked: none is hidden, nor never drawn */
static int ax_walked(ACTX, uint32_t x) {
    for (uint32_t p = x; p > 1; p = d->node[p].parent)
        if (d->node[p].kind == DOM_NODE_KIND_ELEMENT && (ax_undrawn(AARGS, p, ax_kind(AARGS, p)) || ax_hidden_how(AARGS, p) >= 0)) return 0;
    return 1;
}
/* who owns what by aria-owns (WAI-ARIA 1.2): in tree order, each id an element's aria-owns lists names an element it
   takes as a child of its own, after its children, when that element is not taken already, is neither the owner nor
   above it (the tree as it stands, each owned element under its owner), and both are walked */
static void ax_owners(ACTX) {
    resize(owner, d->node_len, 0);
    uint32_t x = d->node_len > 1 ? d->node[1].first_child : 0;
    while (x) {
        uint32_t oa = d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].first_attr ? ax_get(AARGS, x, A_OWNS) : 0;
        if (oa && ax_walked(AARGS, x)) {
            amb_span v = d->attr[oa].value;
            const char *p = text_ptr(d, v);
            size_t i = 0;
            while (i < v.len) {
                while (i < v.len && ax_white(p[i])) i++;
                size_t b = i;
                while (i < v.len && !ax_white(p[i])) i++;
                if (i == b) break;
                uint32_t y = ax_by_id(AARGS, p + b, i - b);
                if (!y || y == x || at(*owner, y) || !ax_walked(AARGS, y)) continue;
                int above = 0;
                for (uint32_t q = x; q > 1 && !above; q = at(*owner, q) ? at(*owner, q) : d->node[q].parent) if (q == y) above = 1;
                if (!above) (*owner)[y] = x;
            }
        }
        if (d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x && !d->node[x].next_sibling) x = d->node[x].parent;
        if (x) x = d->node[x].next_sibling;
    }
}

/* the walk, in the tree's order: each element's children, then what it owns by aria-owns; an owned element is met
   under its owner, not where it is. A loop over a stack of frames, so a tree of any depth is walked */
static void ax_walk(ACTX, vector<uint8_t> *sel, vector<ax_radio> *radios, vector<char> *name, vector<char> *desc, uint32_t root) {
    vector<ax_frame> stack;
    ax_frame f0;
    memset(&f0, 0, sizeof f0);
    f0.x = root;
    f0.next = d->node[root].first_child;
    push_back(stack, f0);
    while (len(stack)) {
        size_t top = len(stack) - 1;
        uint32_t c = at(stack, top).next;
        if (c) {
            stack[top].next = d->node[c].next_sibling;
            if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
            if (c < len(*owner) && at(*owner, c)) continue;
        } else {
            ax_frame fo = at(stack, top);
            c = ax_next_owned(AARGS, &fo);
            stack[top].oa = fo.oa;
            stack[top].op = fo.op;
            if (!c) { pop_back(stack); continue; }
        }
        ax_frame f = at(stack, top);
        ax_frame g;
        memset(&g, 0, sizeof g);
        if (ax_visit(AARGS, sel, radios, name, desc, &f, c, &g)) push_back(stack, g);
    }
}

/* the set an item is counted in: its container (a list's items, a listbox's, select's or optgroup's options, a
   tablist's tabs, a tree's or group's tree items, a menu's items, a radiogroup's radios, a feed's articles), as the
   container's node plus 1; 0 for none */
static uint32_t ax_set_of(a11y_tree *t, uint32_t a) {
    a11y_role r = t->anode[a].role;
    uint32_t p = t->anode[a].parent;
    if (!p) return 0;
    a11y_role pr = t->anode[p].role;
    uint32_t cls = 0;
    if (r == A11Y_ROLE_LISTITEM && (pr == A11Y_ROLE_LIST || pr == A11Y_ROLE_DIRECTORY)) cls = 1;
    else if (r == A11Y_ROLE_OPTION && (pr == A11Y_ROLE_LISTBOX || pr == A11Y_ROLE_COMBOBOX || pr == A11Y_ROLE_GROUP)) cls = 2;
    else if (r == A11Y_ROLE_TAB && pr == A11Y_ROLE_TABLIST) cls = 3;
    else if (r == A11Y_ROLE_TREEITEM && (pr == A11Y_ROLE_TREE || pr == A11Y_ROLE_GROUP)) cls = 4;
    else if ((r == A11Y_ROLE_MENUITEM || r == A11Y_ROLE_MENUITEMCHECKBOX || r == A11Y_ROLE_MENUITEMRADIO) &&
             (pr == A11Y_ROLE_MENU || pr == A11Y_ROLE_MENUBAR || pr == A11Y_ROLE_GROUP)) cls = 5;
    else if (r == A11Y_ROLE_RADIO && pr == A11Y_ROLE_RADIOGROUP) cls = 6;
    else if (r == A11Y_ROLE_ARTICLE && pr == A11Y_ROLE_FEED) cls = 7;
    if (!cls) return 0;
    return p + 1;
}
static int ax_item_role(a11y_role r) {
    return r == A11Y_ROLE_LISTITEM || r == A11Y_ROLE_OPTION || r == A11Y_ROLE_TAB || r == A11Y_ROLE_TREEITEM || r == A11Y_ROLE_MENUITEM ||
           r == A11Y_ROLE_MENUITEMCHECKBOX || r == A11Y_ROLE_MENUITEMRADIO || r == A11Y_ROLE_RADIO || r == A11Y_ROLE_ARTICLE || r == A11Y_ROLE_ROW;
}

/* each item's position in its set and the set's size: counted, then what aria-posinset and aria-setsize say. A set is
   keyed by a node of its own: its container, a named radio group's first radio, a tree grid's first row of a level */
static void ax_positions(ACTX, const vector<ax_radio> *radios) {
    size_t n = t->anode_len;
    if (n < 2) return;
    vector<uint32_t> key, cnt, grp;
    /* a named radio's set: the radios of its name in its form, keyed by the first of them */
    if (len(*radios)) resize(grp, n);
    for (size_t i = 0; i < len(*radios); i++) {
        ax_radio r = at(*radios, i);
        uint32_t first = r.node;
        amb_span rn = d->attr[r.name].value;
        for (size_t j = 0; j < i; j++) {
            ax_radio q = at(*radios, j);
            amb_span qn = d->attr[q.name].value;
            if (q.form == r.form && qn.len == rn.len && memcmp(text_ptr(d, qn), text_ptr(d, rn), rn.len) == 0) { first = at(grp, q.node); break; }
        }
        grp[r.node] = first;
    }
    /* a tree grid's rows: those of one level after the last row above them */
    uint32_t open[65];
    memset(open, 0, sizeof open);
    uint32_t grid = 0;
    for (uint32_t a = 1; a < n; a++) {
        if (!t->anode[a].live) continue;
        uint32_t k = 0;
        if (len(grp) && at(grp, a)) k = at(grp, a) + 1;
        else if (t->anode[a].role == A11Y_ROLE_ROW) {
            uint32_t p = t->anode[a].parent, gr = 0;
            if (p && t->anode[p].role == A11Y_ROLE_TREEGRID) gr = p;
            else if (p && t->anode[p].role == A11Y_ROLE_ROWGROUP && t->anode[p].parent && t->anode[t->anode[p].parent].role == A11Y_ROLE_TREEGRID)
                gr = t->anode[p].parent;
            if (gr) {
                if (gr != grid) { memset(open, 0, sizeof open); grid = gr; }
                uint32_t lv = t->anode[a].level;
                if (lv < 1) lv = 1;
                if (lv > 64) lv = 64;
                for (uint32_t l = lv + 1; l <= 64; l++) open[l] = 0;
                if (!open[lv]) open[lv] = a;
                k = open[lv] + 1;
            }
        } else k = ax_set_of(t, a);
        if (!k) continue;
        if (!len(key)) { resize(key, n); resize(cnt, n); }
        key[a] = k;
        cnt[k - 1]++;
        t->anode[a].posinset = (int32_t)at(cnt, k - 1);
    }
    for (uint32_t a = 1; a < len(key); a++) if (at(key, a)) t->anode[a].setsize = (int32_t)at(cnt, at(key, a) - 1);
    /* what the page says: an aria-posinset of 1 or more, an aria-setsize of -1 (unknown) or more; a value outside
       those counts as none (the rules name it) */
    for (uint32_t a = 1; a < n; a++) {
        if (!t->anode[a].live || !ax_item_role(t->anode[a].role)) continue;
        uint32_t x = t->anode[a].node.index;
        if (x >= d->node_len || !d->node[x].first_attr) continue;
        long v = 0;
        if (ax_int(d, ax_get(AARGS, x, A_POSINSET), &v) && v >= 1 && v < 0x7fffffff) {
            t->anode[a].posinset = (int32_t)v;
            if (a >= len(key) || !at(key, a)) t->anode[a].setsize = -1;
        }
        if (ax_int(d, ax_get(AARGS, x, A_SETSIZE), &v) && v >= -1 && v < 0x7fffffff && t->anode[a].posinset) t->anode[a].setsize = (int32_t)v;
    }
}

a11y_status a11y_build(a11y_tree *t, dom_document *d, css_style *st) {
    vector<uint32_t> sx;
    resize(sx, d->node_len);
    for (uint32_t e = 1; e < st->estyle_len; e++) {      /* each element's first value: its style's */
        uint32_t x = st->estyle[e].node.index;
        if (x < d->node_len && !at(sx, x)) sx[x] = st->estyle[e].first;
    }
    /* what is learnt of the document's names, each found when first asked: none yet */
    size_t na = names_count(d) + 1;
    vector<uint8_t> ek, ak;
    resize(ek, na, 0xFF);
    resize(ak, na, 0xFF);
    uint32_t aid[K_COUNT];
    for (int k = 0; k < K_COUNT; k++) aid[k] = 0xFFFFFFFFu;
    aid[0] = 0;
    uint32_t cw[W_COUNT];
    cw[W_NONE] = names_find(st, "none", 4);
    cw[W_INLINE] = names_find(st, "inline", 6);
    cw[W_CONTENTS] = names_find(st, "contents", 8);
    cw[W_HIDDEN] = names_find(st, "hidden", 6);
    cw[W_COLLAPSE] = names_find(st, "collapse", 8);
    cw[W_UPPERCASE] = names_find(st, "uppercase", 9);
    cw[W_LOWERCASE] = names_find(st, "lowercase", 9);
    cw[W_CAPITALIZE] = names_find(st, "capitalize", 10);
    cw[W_MATH_AUTO] = names_find(st, "math-auto", 9);
    ax_qd_made = 0;
    clear(ax_qd);
    vector<uint32_t> ids, labels;
    vector<uint8_t> sel;
    vector<ax_radio> radios;
    vector<char> name, desc;          /* the walk's text, one node at a time */
    /* each element's first pseudo-element style; who owns what; the radio groups missing a checked button */
    vector<uint32_t> gen, owner;
    vector<uint8_t> rmiss;
    resize(gen, d->node_len, 0);
    for (uint32_t i = st->pstyle_len; i > 1; i--) {
        uint32_t x = st->pstyle[i - 1].node.index;
        if (x < d->node_len) gen[x] = i - 1;
    }
    ax_owners(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &gen, &owner, &rmiss);
    ax_radio_groups(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &gen, &owner, &rmiss);
    /* the root element's children: html itself is no node */
    uint32_t root = 0;
    for (uint32_t c = d->node_len > 1 ? d->node[1].first_child : 0; c; c = d->node[c].next_sibling)
        if (d->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    if (root) ax_walk(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &gen, &owner, &rmiss, &sel, &radios, &name, &desc, root);
    ax_positions(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &gen, &owner, &rmiss, &radios);
    t->revision++;
    return A11Y_OK;
}

/* ---- the developer's view ---- */

/* a text on the view's line: quoted, each run of white space one space */
static void ax_print_text(FILE *out, const char *what, const char *p, size_t n) {
    fprintf(out, " %s\"", what);
    int white = 0;
    for (size_t i = 0; i < n; i++) {
        if (ax_white(p[i])) { if (!white) fputc(' ', out); white = 1; }
        else { fputc(p[i], out); white = 0; }
    }
    fputc('"', out);
}

a11y_status a11y_print_tree(const a11y_tree *t, dom_document *d, FILE *out) {
    uint32_t idk = names_find(d, "id", 2);
    /* the side records, each table in its nodes' order: a cursor each */
    uint32_t si = 1, ti = 1, ri = 1, li = 1;
    for (uint32_t a = 1; a < t->anode_len; a++) {
        if (!t->anode[a].live) continue;
        int depth = 0;
        for (uint32_t q = t->anode[a].parent; q; q = t->anode[q].parent) depth++;
        fprintf(out, "  %*s%s", 2 * depth, "", a11y_role_name(t->anode[a].role));
        uint32_t x = t->anode[a].node.index;
        if (idk && node_live(d, t->anode[a].node))
            for (uint32_t ai = d->node[x].first_attr; ai; ai = d->attr[ai].next)
                if (d->attr[ai].name == idk) { amb_span v = d->attr[ai].value; fprintf(out, "#%.*s", (int)v.len, text_ptr(d, v)); break; }
        /* its texts, by what each is */
        uint32_t tx[4] = { 0, 0, 0, 0 };
        while (ti < t->atext_len && t->atext[ti].node < a) ti++;
        for (uint32_t q = ti; q < t->atext_len && t->atext[q].node == a; q++)
            if (t->atext[q].live && (int)t->atext[q].prop < 4) tx[t->atext[q].prop] = q;
        amb_span nm = t->anode[a].name;
        if (nm.len) {
            ax_print_text(out, "", text_ptr(t, nm), nm.len);
            fprintf(out, " (%s)", a11y_name_from_name(t->anode[a].name_from));
        }
        if (tx[A11Y_TEXT_PROP_DESCRIPTION]) {
            amb_span ds = t->atext[tx[A11Y_TEXT_PROP_DESCRIPTION]].text;
            ax_print_text(out, "desc ", text_ptr(t, ds), ds.len);
            fprintf(out, " (%s)", a11y_desc_from_name(t->atext[tx[A11Y_TEXT_PROP_DESCRIPTION]].desc_from));
        }
        if (t->anode[a].level) fprintf(out, " level=%u", (unsigned)t->anode[a].level);
        if (t->anode[a].posinset) fprintf(out, " posinset=%d setsize=%d", (int)t->anode[a].posinset, (int)t->anode[a].setsize);
        while (si < t->astate_len && t->astate[si].node < a) si++;
        for (; si < t->astate_len && t->astate[si].node == a; si++)
            if (t->astate[si].live) fprintf(out, " %s=%s", a11y_state_name(t->astate[si].state), a11y_token_name(t->astate[si].value));
        if (tx[A11Y_TEXT_PROP_VALUE]) { amb_span v = t->atext[tx[A11Y_TEXT_PROP_VALUE]].text; ax_print_text(out, "value=", text_ptr(t, v), v.len); }
        if (tx[A11Y_TEXT_PROP_PLACEHOLDER]) {
            amb_span v = t->atext[tx[A11Y_TEXT_PROP_PLACEHOLDER]].text;
            ax_print_text(out, "placeholder=", text_ptr(t, v), v.len);
        }
        while (ri < t->arange_len && t->arange[ri].node < a) ri++;
        if (ri < t->arange_len && t->arange[ri].node == a && t->arange[ri].live) {
            if (t->arange[ri].has_now) fprintf(out, " valuenow=%g (%s)", t->arange[ri].valuenow, a11y_value_from_name(t->arange[ri].now_from));
            if (t->arange[ri].has_min) fprintf(out, " valuemin=%g", t->arange[ri].valuemin);
            if (t->arange[ri].has_max) fprintf(out, " valuemax=%g", t->arange[ri].valuemax);
        }
        if (tx[A11Y_TEXT_PROP_VALUETEXT]) {
            amb_span v = t->atext[tx[A11Y_TEXT_PROP_VALUETEXT]].text;
            ax_print_text(out, "valuetext=", text_ptr(t, v), v.len);
        }
        while (li < t->alive_len && t->alive[li].node < a) li++;
        if (li < t->alive_len && t->alive[li].node == a && t->alive[li].live) {
            amb_span r = t->alive[li].relevant;
            fprintf(out, " live=%s atomic=%s relevant=\"%.*s\"", a11y_politeness_name(t->alive[li].politeness),
                    t->alive[li].atomic ? "true" : "false", (int)r.len, text_ptr(t, r));
        }
        if (t->anode[a].focusable) fprintf(out, " focusable");
        fputc('\n', out);
    }
    return A11Y_OK;
}
