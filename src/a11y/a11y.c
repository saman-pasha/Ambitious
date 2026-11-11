/* src/a11y/a11y.c -- the accessibility module (contracts/a11y.pl): the
   accessibility tree of a styled document; and the module's generated
   implementation.

   THE TREE, in document order. An element whose computed display is none, or
   that is hidden or aria-hidden="true", is not in it, nor is anything inside it;
   the root of each such subtree is kept, with what inside it still takes focus
   (inside aria-hidden content, which is drawn) or is a live region. What is never
   drawn by its nature (head, script, style, template, ...) is passed over. An
   element's role is its role attribute's first token that is a role, compared
   without case, or else its own (HTML-AAM: a with href a link, img an image --
   presentation when its alt is empty --, input by its type, h1 to h6 headings,
   ul and ol lists, nav, main, aside, header and footer the landmarks, a section
   with a name a region, output a status, a details element's summary a button,
   ...). role=none is presentation, unless the element takes focus or has a
   global aria-* attribute: then it keeps its own role (WAI-ARIA 1.2's conflict
   resolution). An element whose role is generic or presentation is no node, and
   its children join its parent's node; but a generic element that takes focus,
   or that carries a global state or property other than a name (aria-live,
   aria-describedby, aria-current, ...), is a node, as Core-AAM includes it.

   A NODE'S NAME (accname 1.2, HTML-AAM): aria-labelledby's elements' text;
   aria-label; an image's alt; a labelable element's labels (each label for it,
   then the label it is the first control of); an input button's value; a
   table's caption, a fieldset's legend, a figure's figcaption, an optgroup's or
   option's label; the content of a role named from content; then title, then a
   field's placeholder; an input button's default. Content is walked through:
   text, an image's alt, a descendant's aria-labelledby or aria-label, and an
   embedded control's value -- a textbox its value, a combobox or listbox its
   chosen options, a range its valuetext or valuenow (one with nothing chosen
   and no value is read as any element) --; a line break and an element that is
   not inline set their text off by spaces. As Chromium reads content, a nested
   container of widgets, a landmark or a live region adds nothing (a tree item's
   group, a menu item's submenu), and a row is named from its content in a grid
   alone. What is hidden is left out, unless aria-labelledby or aria-describedby
   names a hidden element: then all of that element's subtree counts. White space
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
   invalid, current, busy, haspopup (a select's popup a menu, as Chromium has it;
   a combobox's a listbox), orientation (with each role's default),
   multiselectable, modal, autocomplete (an input with a list), multiline. A token
   is compared without case, trimmed; a value that is none of a state's tokens
   counts as the state's default (the rules name it). A range's values: an
   input's, a progress element's or a meter's (HTML's defaults and clamping, a
   range input's value put on its step), or aria-valuenow, -valuemin and
   -valuemax with the role's defaults (a slider's value half way, a spinbutton's
   or meter's 0), aria-valuenow kept inside the range as Chromium keeps it; and
   aria-valuetext. A field's value (a password's as bullets), a combobox's chosen
   option; a field's placeholder.

   LIVE REGIONS: aria-live, or the role's politeness (alert assertive; status and
   log polite; marquee and timer off), aria-atomic (alert and status atomic) and
   aria-relevant ("additions text" by default). Each node inside a region knows
   the innermost one.

   LEVELS AND POSITIONS: a heading's level (aria-level, or h1 to h6, or 2), a list
   item's (the lists it is in), a tree item's (aria-level, or the groups of its
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

   Not done, and why: visibility (the cascade does not compute it); the content
   of a closed details element and a dialog without open (the user agent sheet
   draws them); CSS generated content; constraint validation (invalid comes from
   aria-invalid alone); presentational children and inherited presentation;
   aria-owns reordering the tree. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_interface(css);
amb_implementation(a11y);

/* ---- the names this module reads, found among the document's atoms when first asked ---- */

/* the element names, in strcmp order (a name's kind is its index, found by halving) */
enum { E_NONE, E_A, E_AREA, E_ARTICLE, E_ASIDE, E_AUDIO, E_BASE, E_BLOCKQUOTE, E_BODY, E_BR, E_BUTTON, E_CAPTION, E_CODE,
       E_DATALIST, E_DD, E_DETAILS, E_DIALOG, E_DT, E_FIELDSET, E_FIGCAPTION, E_FIGURE, E_FOOTER, E_FORM, E_H1, E_H2, E_H3,
       E_H4, E_H5, E_H6, E_HEAD, E_HEADER, E_HR, E_IFRAME, E_IMG, E_INPUT, E_LABEL, E_LEGEND, E_LI, E_LINK, E_MAIN, E_MENU,
       E_META, E_METER, E_NAV, E_NOSCRIPT, E_OL, E_OPTGROUP, E_OPTION, E_OUTPUT, E_P, E_PARAM, E_PROGRESS, E_SCRIPT, E_SEARCH,
       E_SECTION, E_SELECT, E_SOURCE, E_STYLE, E_SUMMARY, E_TABLE, E_TD, E_TEMPLATE, E_TEXTAREA, E_TH, E_TITLE, E_TR, E_TRACK,
       E_UL, E_VIDEO, E_COUNT };
static const char *const ax_elements[E_COUNT] = { "", "a", "area", "article", "aside", "audio", "base", "blockquote", "body", "br",
    "button", "caption", "code", "datalist", "dd", "details", "dialog", "dt", "fieldset", "figcaption", "figure", "footer", "form",
    "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr", "iframe", "img", "input", "label", "legend", "li", "link", "main",
    "menu", "meta", "meter", "nav", "noscript", "ol", "optgroup", "option", "output", "p", "param", "progress", "script", "search",
    "section", "select", "source", "style", "summary", "table", "td", "template", "textarea", "th", "title", "tr", "track", "ul",
    "video" };

/* the attributes: HTML's, then the aria-* ones from A_FIRST, in strcmp order */
enum { K_NONE, K_ROLE, K_ID, K_HIDDEN, K_TABINDEX, K_DISABLED, K_HREF, K_ALT, K_TITLE, K_TYPE, K_VALUE, K_PLACEHOLDER, K_FOR,
       K_LABEL, K_MULTIPLE, K_SELECTED, K_CHECKED, K_REQUIRED, K_READONLY, K_OPEN, K_NAME, K_LIST, K_MIN, K_MAX, K_STEP, K_SCOPE,
       K_CONTENTEDITABLE, K_SIZE, K_CONTROLS,
       A_ACTIVEDESCENDANT, A_ATOMIC, A_AUTOCOMPLETE, A_BUSY, A_CHECKED, A_COLCOUNT, A_COLINDEX, A_COLSPAN, A_CONTROLS, A_CURRENT,
       A_DESCRIBEDBY, A_DESCRIPTION, A_DETAILS, A_DISABLED, A_DROPEFFECT, A_ERRORMESSAGE, A_EXPANDED, A_FLOWTO, A_GRABBED,
       A_HASPOPUP, A_HIDDEN, A_INVALID, A_KEYSHORTCUTS, A_LABEL, A_LABELLEDBY, A_LEVEL, A_LIVE, A_MODAL, A_MULTILINE,
       A_MULTISELECTABLE, A_ORIENTATION, A_OWNS, A_PLACEHOLDER, A_POSINSET, A_PRESSED, A_READONLY, A_RELEVANT, A_REQUIRED,
       A_ROLEDESCRIPTION, A_ROWCOUNT, A_ROWINDEX, A_ROWSPAN, A_SELECTED, A_SETSIZE, A_SORT, A_VALUEMAX, A_VALUEMIN, A_VALUENOW,
       A_VALUETEXT, K_COUNT };
#define A_FIRST A_ACTIVEDESCENDANT
static const char *const ax_attrs[K_COUNT] = { "", "role", "id", "hidden", "tabindex", "disabled", "href", "alt", "title", "type",
    "value", "placeholder", "for", "label", "multiple", "selected", "checked", "required", "readonly", "open", "name", "list", "min",
    "max", "step", "scope", "contenteditable", "size", "controls",
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
       IT_TEL, IT_URL, IT_PASSWORD, IT_OTHER };

/* the display keywords this module tells apart, as the style's atoms */
enum { W_NONE, W_INLINE, W_CONTENTS, W_COUNT };

/* the walk of a name's text: inside an aria-labelledby or -describedby walk; all of a hidden element's subtree in it;
   at the element such a walk names, whose own content counts whatever its role */
enum { T_REF = 1, T_ALL = 2, T_TARGET = 4 };

/* every helper reads the document and its style. What it learns of the document's names is kept for the build: ek an
   element name's kind and ak an aria-* attribute's, by its atom (0xFF not yet known), aid each attribute kind's atom (~0
   not yet asked); the ids, and the labels with for, are indexed when first asked */
#define ACTX a11y_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sx, vector<uint8_t> *ek, \
             vector<uint8_t> *ak, uint32_t *aid, const uint32_t *cw, vector<uint32_t> *ids, vector<uint32_t> *labels
#define AARGS t, d, st, sx, ek, ak, aid, cw, ids, labels

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
/* an element's kind, by its name's atom; E_NONE for any other node */
static int ax_kind(ACTX, uint32_t x) {
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return E_NONE;
    uint32_t nm = d->node[x].name;
    if (nm >= len(*ek)) return E_NONE;
    int k = at(*ek, nm);
    if (k == 0xFF) {
        k = ax_find_name(ax_elements, 1, E_COUNT, names_ptr(d, nm), names_len(d, nm));
        (*ek)[nm] = (uint8_t)k;
    }
    return k;
}
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

static int ax_input_type(ACTX, uint32_t x) {
    int v = ax_tok(d, ax_get(AARGS, x, K_TYPE),
                   "text hidden checkbox radio range number button submit reset image search email tel url password date month week time datetime-local color file");
    if (v < 0) return IT_TEXT;
    if (v > IT_PASSWORD) return IT_OTHER;
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
        if (!a || !d->attr[a].value.len) continue;
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
    for (int k = 0; k < (int)A11Y_ROLE_UNKNOWN; k++)
        if (ax_word_is(p, n, a11y_role_name((a11y_role)k))) return k;
    return -1;
}
/* a role attribute's first token that is a role: its role, 1; or 0, its first token in bad */
static int ax_role_attr(ACTX, uint32_t x, a11y_role *role, size_t *bad_off, size_t *bad_len) {
    *bad_len = 0;
    if (!d->node[x].first_attr) return 0;
    uint32_t a = ax_get(AARGS, x, K_ROLE);
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && ax_white(p[i])) i++;
        size_t b = i;
        while (i < v.len && !ax_white(p[i])) i++;
        if (i == b) break;
        int r = ax_role_word(p + b, i - b);
        if (r >= 0) { *role = (a11y_role)r; return 1; }
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
    case E_TABLE: return A11Y_ROLE_TABLE;
    case E_TR: return A11Y_ROLE_ROW;
    case E_TD: return A11Y_ROLE_CELL;
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
    default: return A11Y_ROLE_GENERIC;
    }
}
/* an element's role, as a name's walk meets it: its role attribute's, or its own */
static a11y_role ax_role_of(ACTX, uint32_t x, int k) {
    a11y_role r;
    size_t bo = 0, bl = 0;
    if (ax_role_attr(AARGS, x, &r, &bo, &bl)) return r;
    return ax_implicit(AARGS, x, k);
}

static int ax_from_content(a11y_role r) {
    return r == A11Y_ROLE_LINK || r == A11Y_ROLE_BUTTON || r == A11Y_ROLE_HEADING || r == A11Y_ROLE_CELL || r == A11Y_ROLE_COLUMNHEADER ||
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
        int f2 = flags | (ax_hidden_how(AARGS, target) >= 0 ? T_ALL : 0);
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
        *has_min = ax_num(d, ax_get(AARGS, x, K_MIN), mn);
        *has_max = ax_num(d, ax_get(AARGS, x, K_MAX), mx);
        return ax_num(d, ax_get(AARGS, x, K_VALUE), now);
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
        if (k == E_INPUT) { uint32_t va = ax_get(AARGS, y, K_VALUE); if (va && ty != IT_PASSWORD) ax_put_attr(d, va, out); }
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
    return 0;
}

/* the text of root's subtree for a name: what each element gives, or its content walked through; an element that is
   not inline set off by spaces. What is hidden is left out unless the walk counts all of a hidden subtree; the
   element being named (self) adds nothing to its own label. The walk is a loop: a subtree of any depth is read */
static void ax_content(ACTX, uint32_t root, int flags, uint32_t self, vector<char> *out) {
    uint32_t x = d->node[root].first_child;
    while (x) {
        int down = 0;
        int nk = d->node[x].kind;
        if (nk == DOM_NODE_KIND_TEXT) ax_put(out, text_ptr(d, d->node[x].data), d->node[x].data.len);
        else if (nk == DOM_NODE_KIND_ELEMENT && x != self) {
            int k = ax_kind(AARGS, x);
            if (k == E_SCRIPT || k == E_STYLE || k == E_TEMPLATE) {}
            else if (!(flags & T_ALL) && ax_hidden_how(AARGS, x) >= 0) {}
            else if (k == E_BR) ax_space(out);
            else {
                int block = ax_block(AARGS, x);
                if (block) ax_space(out);
                if (!ax_part(AARGS, x, k, flags & ~T_TARGET, self, out) && d->node[x].first_child) down = 1;
                else if (block) ax_space(out);
            }
        }
        if (down) { x = d->node[x].first_child; continue; }
        while (x != root && !d->node[x].next_sibling) {
            x = d->node[x].parent;
            if (x != root && ax_block(AARGS, x)) ax_space(out);
        }
        if (x == root) break;
        x = d->node[x].next_sibling;
    }
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
                if (d->node[l].live && ax_kind(AARGS, l) == E_LABEL && ax_get(AARGS, l, K_FOR)) push_back(labels, l);
        }
        amb_span idv = d->attr[ida].value;
        for (size_t q = 1; q < len(*labels); q++) {
            uint32_t l = at(*labels, q);
            uint32_t f = ax_get(AARGS, l, K_FOR);
            if (!f || d->attr[f].value.len != idv.len || memcmp(text_ptr(d, d->attr[f].value), text_ptr(d, idv), idv.len) != 0) continue;
            ax_space(out);
            ax_content(AARGS, l, ax_hidden_how(AARGS, l) >= 0 ? T_ALL : 0, x, out);
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

/* a node's states and properties, in the contract's order. sl holds its aria-* attributes; dis is where its disabled
   state came from (-1 none), dis_by the element it came from, off that it is natively disabled; sel how each option
   of a select is chosen */
static void ax_states(ACTX, const vector<uint8_t> *sel, const uint32_t *sl, uint32_t a, uint32_t x, int k, int ty, a11y_role role,
                      int dis, uint32_t dis_by, int off) {
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
    int texty = k == E_INPUT && (ax_text_input(ty) || ty == IT_NUMBER || ty == IT_OTHER);
    if ((texty || (k == E_INPUT && (ty == IT_CHECKBOX || ty == IT_RADIO)) || k == E_SELECT || k == E_TEXTAREA) && ax_get(AARGS, x, K_REQUIRED))
        AX_FOUND(A11Y_STATE_REQUIRED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (AX_TRUE(AS(A_REQUIRED))) AX_FOUND(A11Y_STATE_REQUIRED, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    uint32_t ce = k != E_INPUT && k != E_TEXTAREA ? ax_get(AARGS, x, K_CONTENTEDITABLE) : 0;
    int editing = ce && ax_tok(d, ce, "false") != 0;
    if ((texty || k == E_TEXTAREA) && ax_get(AARGS, x, K_READONLY)) AX_FOUND(A11Y_STATE_READONLY, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_HTML);
    else if (!editing && AX_TRUE(AS(A_READONLY))) AX_FOUND(A11Y_STATE_READONLY, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    /* invalid and current: any value but false is true (WAI-ARIA 1.2) */
    v = ax_tok(d, AS(A_INVALID), "true false grammar spelling");
    if (v == 0 || v == -1) AX_FOUND(A11Y_STATE_INVALID, A11Y_TOKEN_TRUE, A11Y_STATE_FROM_ARIA);
    else if (v >= 2) AX_FOUND(A11Y_STATE_INVALID, v == 2 ? A11Y_TOKEN_GRAMMAR : A11Y_TOKEN_SPELLING, A11Y_STATE_FROM_ARIA);
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
        uint32_t va = ax_get(AARGS, x, K_VALUE);
        if (va && d->attr[va].value.len) {
            amb_span s = d->attr[va].value;
            if (ty == IT_PASSWORD) {
                const char *p = text_ptr(d, s);
                for (uint32_t i = 0; i < s.len; i++) if (((unsigned char)p[i] & 0xC0) != 0x80) append_range(scratch, "\xe2\x80\xa2", 3);
            } else append_range(scratch, text_ptr(d, s), s.len);
        }
    } else if (k == E_TEXTAREA) {
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_TEXT) append_range(scratch, text_ptr(d, d->node[c].data), d->node[c].data.len);
    } else if (k == E_SELECT && role == A11Y_ROLE_COMBOBOX) {
        if (pick) ax_option_text(AARGS, pick, scratch);
    } else if (k != E_INPUT && k != E_SELECT && (role == A11Y_ROLE_TEXTBOX || role == A11Y_ROLE_SEARCHBOX || role == A11Y_ROLE_COMBOBOX))
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
    uint32_t in_tree;         /* its children are inside a tree */
    uint32_t form;            /* the form its children are inside */
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
    /* no node: a generic or presentational element, unless a generic one takes focus or has a global state */
    int node = role != A11Y_ROLE_GENERIC && role != A11Y_ROLE_PRESENTATION;
    if (role == A11Y_ROLE_GENERIC && (focusable || (glob & 2))) node = 1;
    if (!node) {
        if (aria) {
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
    a11y_name_from nf = ax_name_of(AARGS, sl, c, k, role, ax_from_content(role) || (role == A11Y_ROLE_ROW && in_grid), name);
    amb_span nsp = ax_name(t, name);
    t->anode[a].name = nsp;
    t->anode[a].name_from = nf;
    if (has_attrs || k == E_TABLE) {
        a11y_desc_from df = ax_desc_of(AARGS, sl, c, k, nf, name, desc);
        if (df != A11Y_DESC_FROM_NOTHING) ax_text(t, a, A11Y_TEXT_PROP_DESCRIPTION, desc, df, 0);
    }
    /* states, values and a live region come from aria-* attributes, form controls, the roles of widgets and live
       regions, and a disabled ancestor: a node with none of those (a paragraph, a list item, a cell ...) has none */
    int live = 0;
    if (sl || dis >= 0 || ax_form_control(k) || k == E_OPTION || k == E_OPTGROUP || k == E_SUMMARY || k == E_PROGRESS ||
        k == E_METER || k == E_OUTPUT || ax_stateful(role)) {
        ax_states(AARGS, sel, sl, a, c, k, ty, role, dis, by, native_dis);
        ax_values(AARGS, sl, desc, pick, a, c, k, ty, role, focusable);
        live = ax_live(AARGS, sl, a, role);
    }
    /* its level */
    long lv = 0;
    int given = ax_int(d, AS(A_LEVEL), &lv) && lv >= 1;
    if (role == A11Y_ROLE_HEADING) {
        if (!given) lv = k >= E_H1 && k <= E_H6 ? k - E_H1 + 1 : 2;
    } else if (role == A11Y_ROLE_LISTITEM) { if (!given) lv = f->lists; }
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
    /* a tree item's level counts the groups it is in, inside its tree (as Chromium counts it) */
    if (role == A11Y_ROLE_TREE) { g->items = 0; g->in_tree = 1; }
    if (role == A11Y_ROLE_GROUP && f->in_tree) g->items = f->items + 1;
    return 1;
}

/* the walk, in document order: a loop over a stack of frames, so a tree of any depth is walked */
static void ax_walk(ACTX, vector<uint8_t> *sel, vector<ax_radio> *radios, vector<char> *name, vector<char> *desc, uint32_t root) {
    vector<ax_frame> stack;
    ax_frame f0;
    memset(&f0, 0, sizeof f0);
    f0.x = root;
    f0.next = d->node[root].first_child;
    push_back(stack, f0);
    while (len(stack)) {
        size_t top = len(stack) - 1;
        ax_frame f = at(stack, top);
        uint32_t c = f.next;
        if (!c) { pop_back(stack); continue; }
        stack[top].next = d->node[c].next_sibling;
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
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
    vector<uint32_t> ids, labels;
    vector<uint8_t> sel;
    vector<ax_radio> radios;
    vector<char> name, desc;          /* the walk's text, one node at a time */
    /* the root element's children: html itself is no node */
    uint32_t root = 0;
    for (uint32_t c = d->node_len > 1 ? d->node[1].first_child : 0; c; c = d->node[c].next_sibling)
        if (d->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    if (root) ax_walk(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &sel, &radios, &name, &desc, root);
    ax_positions(t, d, st, &sx, &ek, &ak, aid, cw, &ids, &labels, &radios);
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
