/* src/a11y/a11y.c -- the accessibility module (contracts/a11y.pl): the
   accessibility tree of a styled document; and the module's generated
   implementation.

   The tree, in document order. An element whose computed display is none, or
   that is hidden or aria-hidden="true", is not in it, nor is anything inside it.
   An element's role is its role attribute's first token that is a role, or else
   its own (HTML-AAM: a with href a link, img an image -- none when its alt is
   empty --, input by its type, h1 to h6 headings, ul and ol lists, nav, main,
   aside, header and footer the landmarks, a section with a name a region, ...;
   role=none is presentation). An element whose role is generic or presentation
   is not a node: its children join its parent's node.

   A node's name (accname 1.2, the common cases): aria-labelledby's elements'
   text; aria-label; an image's alt; a form field's label (by for, or around
   it); a table's caption, a fieldset's legend, a figure's figcaption; an input
   button's value; the content of a link, a button, a heading, a cell, an option,
   a tab, a menu item (text, and the alt of images in it); then title, then a
   field's placeholder. White space collapses, and the name is trimmed. Every id
   that aria-labelledby, aria-describedby, aria-controls or a label's for names is
   kept with the element that has it. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_interface(css);
amb_implementation(a11y);

#define ACTX a11y_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sx
#define AARGS t, d, st, sx

/* ---- the DOM and its style, as this module reads them ---- */

static int ax_is(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t ax_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
static int ax_attr_is(const dom_document *d, uint32_t x, const char *name, const char *value) {
    uint32_t a = ax_attr(d, x, name);
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    size_t n = strlen(value);
    if (v.len != n) return 0;
    const char *p = text_ptr(d, v);
    for (size_t i = 0; i < n; i++) {
        char c = p[i] >= 'A' && p[i] <= 'Z' ? (char)(p[i] + 32) : p[i];
        if (c != value[i]) return 0;
    }
    return 1;
}
/* an element not shown to a reader: display none, hidden, aria-hidden="true" */
static int ax_hidden(ACTX, uint32_t x) {
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return 0;
    if (ax_attr(d, x, "hidden") || ax_attr_is(d, x, "aria-hidden", "true")) return 1;
    if (x >= len(*sx) || !at(*sx, x)) return 1;
    for (uint32_t k = at(*sx, x); k < st->cval_len && st->cval[k].style == st->cval[at(*sx, x)].style; k++)
        if (st->cval[k].prop == CSS_PROP_DISPLAY) return names_is(st, st->cval[k].word, "none", 4);
    return 0;
}

/* text into out, white space runs one space */
static void ax_put(vector<char> *out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        if (white) { if (len(*out) && back(*out) != ' ') push_back(out, ' '); }
        else push_back(out, c);
    }
}
/* an element's text for a name: its text, and the alt of the images in it, what is hidden left out */
static void ax_text(ACTX, uint32_t x, vector<char> *out) {
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) { ax_put(out, text_ptr(d, d->node[c].data), d->node[c].data.len); continue; }
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || ax_hidden(AARGS, c)) continue;
        if (ax_is(d, c, "img")) {
            uint32_t a = ax_attr(d, c, "alt");
            if (a) { push_back(out, ' '); ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); push_back(out, ' '); }
            continue;
        }
        ax_text(AARGS, c, out);
    }
}
/* the trimmed name, into the store's text */
static amb_span ax_name(a11y_tree *t, const vector<char> *v) {
    size_t a = 0, b = len(*v);
    while (a < b && at(*v, a) == ' ') a++;
    while (b > a && at(*v, b - 1) == ' ') b--;
    vector<char> w;
    for (size_t i = a; i < b; i++) push_back(w, at(*v, i));
    return text_add(t, data(w), len(w));
}

/* the element whose id is the given text, or 0 */
static uint32_t ax_by_id(const dom_document *d, const char *id, size_t n) {
    uint32_t ida = names_find(d, "id", 2);
    if (!ida) return 0;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT) continue;
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next)
            if (d->attr[at].name == ida && d->attr[at].value.len == n && memcmp(text_ptr(d, d->attr[at].value), id, n) == 0) return x;
    }
    return 0;
}

/* ---- roles ---- */

static const char *const ax_role_names[] = { "document", "generic", "presentation", "link", "button", "checkbox", "radio", "textbox", "combobox",
    "listbox", "option", "slider", "img", "heading", "paragraph", "list", "listitem", "table", "row", "cell", "columnheader", "rowheader",
    "navigation", "main", "banner", "contentinfo", "complementary", "region", "form", "search", "article", "dialog", "figure", "group",
    "separator", "term", "definition", "blockquote", "code", "progressbar", "menu", "menuitem", "tab", "tablist", "tabpanel", "alert",
    "status" };

/* a role attribute's first token that is a role: its role, 1; or 0, its first token in bad */
static int ax_role_attr(const dom_document *d, uint32_t x, a11y_role *role, size_t *bad_off, size_t *bad_len) {
    uint32_t a = ax_attr(d, x, "role");
    *bad_len = 0;
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && p[i] == ' ') i++;
        size_t b = i;
        while (i < v.len && p[i] != ' ') i++;
        if (i == b) break;
        size_t n = i - b;
        if (n == 4 && memcmp(p + b, "none", 4) == 0) { *role = A11Y_ROLE_PRESENTATION; return 1; }
        if (n == 9 && memcmp(p + b, "searchbox", 9) == 0) { *role = A11Y_ROLE_TEXTBOX; return 1; }
        for (size_t k = 0; k < sizeof ax_role_names / sizeof ax_role_names[0]; k++)
            if (strlen(ax_role_names[k]) == n && memcmp(ax_role_names[k], p + b, n) == 0) { *role = (a11y_role)k; return 1; }
        if (!*bad_len) { *bad_off = b; *bad_len = n; }
    }
    return 0;
}

/* inside a sectioning element: then header and footer are no landmarks */
static int ax_in_section(const dom_document *d, uint32_t x) {
    for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent)
        if (ax_is(d, p, "article") || ax_is(d, p, "aside") || ax_is(d, p, "main") || ax_is(d, p, "nav") || ax_is(d, p, "section")) return 1;
    return 0;
}

static a11y_role ax_implicit(const dom_document *d, uint32_t x) {
    if (ax_is(d, x, "body")) return A11Y_ROLE_DOCUMENT;
    if (ax_is(d, x, "a") || ax_is(d, x, "area")) return ax_attr(d, x, "href") ? A11Y_ROLE_LINK : A11Y_ROLE_GENERIC;
    if (ax_is(d, x, "button")) return A11Y_ROLE_BUTTON;
    if (ax_is(d, x, "input")) {
        if (ax_attr_is(d, x, "type", "checkbox")) return A11Y_ROLE_CHECKBOX;
        if (ax_attr_is(d, x, "type", "radio")) return A11Y_ROLE_RADIO;
        if (ax_attr_is(d, x, "type", "range")) return A11Y_ROLE_SLIDER;
        if (ax_attr_is(d, x, "type", "button") || ax_attr_is(d, x, "type", "submit") || ax_attr_is(d, x, "type", "reset") ||
            ax_attr_is(d, x, "type", "image")) return A11Y_ROLE_BUTTON;
        return A11Y_ROLE_TEXTBOX;
    }
    if (ax_is(d, x, "textarea")) return A11Y_ROLE_TEXTBOX;
    if (ax_is(d, x, "select")) return ax_attr(d, x, "multiple") ? A11Y_ROLE_LISTBOX : A11Y_ROLE_COMBOBOX;
    if (ax_is(d, x, "option")) return A11Y_ROLE_OPTION;
    if (ax_is(d, x, "img")) return ax_attr_is(d, x, "alt", "") ? A11Y_ROLE_PRESENTATION : A11Y_ROLE_IMG;
    if (ax_is(d, x, "h1") || ax_is(d, x, "h2") || ax_is(d, x, "h3") || ax_is(d, x, "h4") || ax_is(d, x, "h5") || ax_is(d, x, "h6"))
        return A11Y_ROLE_HEADING;
    if (ax_is(d, x, "p")) return A11Y_ROLE_PARAGRAPH;
    if (ax_is(d, x, "ul") || ax_is(d, x, "ol") || ax_is(d, x, "menu")) return A11Y_ROLE_LIST;
    if (ax_is(d, x, "li")) return A11Y_ROLE_LISTITEM;
    if (ax_is(d, x, "table")) return A11Y_ROLE_TABLE;
    if (ax_is(d, x, "tr")) return A11Y_ROLE_ROW;
    if (ax_is(d, x, "td")) return A11Y_ROLE_CELL;
    if (ax_is(d, x, "th")) return ax_attr_is(d, x, "scope", "row") ? A11Y_ROLE_ROWHEADER : A11Y_ROLE_COLUMNHEADER;
    if (ax_is(d, x, "nav")) return A11Y_ROLE_NAVIGATION;
    if (ax_is(d, x, "main")) return A11Y_ROLE_MAIN;
    if (ax_is(d, x, "aside")) return A11Y_ROLE_COMPLEMENTARY;
    if (ax_is(d, x, "header")) return ax_in_section(d, x) ? A11Y_ROLE_GENERIC : A11Y_ROLE_BANNER;
    if (ax_is(d, x, "footer")) return ax_in_section(d, x) ? A11Y_ROLE_GENERIC : A11Y_ROLE_CONTENTINFO;
    if (ax_is(d, x, "section"))
        return ax_attr(d, x, "aria-label") || ax_attr(d, x, "aria-labelledby") || ax_attr(d, x, "title") ? A11Y_ROLE_REGION : A11Y_ROLE_GENERIC;
    if (ax_is(d, x, "form")) return A11Y_ROLE_FORM;
    if (ax_is(d, x, "search")) return A11Y_ROLE_SEARCH;
    if (ax_is(d, x, "article")) return A11Y_ROLE_ARTICLE;
    if (ax_is(d, x, "dialog")) return A11Y_ROLE_DIALOG;
    if (ax_is(d, x, "figure")) return A11Y_ROLE_FIGURE;
    if (ax_is(d, x, "fieldset") || ax_is(d, x, "details") || ax_is(d, x, "optgroup")) return A11Y_ROLE_GROUP;
    if (ax_is(d, x, "hr")) return A11Y_ROLE_SEPARATOR;
    if (ax_is(d, x, "dt")) return A11Y_ROLE_TERM;
    if (ax_is(d, x, "dd")) return A11Y_ROLE_DEFINITION;
    if (ax_is(d, x, "blockquote")) return A11Y_ROLE_BLOCKQUOTE;
    if (ax_is(d, x, "code")) return A11Y_ROLE_CODE;
    if (ax_is(d, x, "progress")) return A11Y_ROLE_PROGRESSBAR;
    return A11Y_ROLE_GENERIC;
}

static int ax_name_from_content(a11y_role r) {
    return r == A11Y_ROLE_LINK || r == A11Y_ROLE_BUTTON || r == A11Y_ROLE_HEADING || r == A11Y_ROLE_CELL || r == A11Y_ROLE_COLUMNHEADER ||
           r == A11Y_ROLE_ROWHEADER || r == A11Y_ROLE_OPTION || r == A11Y_ROLE_TAB || r == A11Y_ROLE_MENUITEM || r == A11Y_ROLE_TERM;
}
static int ax_field(const dom_document *d, uint32_t x) { return ax_is(d, x, "input") || ax_is(d, x, "textarea") || ax_is(d, x, "select"); }

/* the first child element of x with the given name, or 0 */
static uint32_t ax_child(const dom_document *d, uint32_t x, const char *name) {
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) if (ax_is(d, c, name)) return c;
    return 0;
}

/* the ids an attribute names, each kept with its element */
static void ax_refs(ACTX, uint32_t x, const char *attr, a11y_ref_attr kind, vector<char> *names) {
    uint32_t a = ax_attr(d, x, attr);
    if (!a) return;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len) {
        while (i < v.len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        size_t b = i;
        while (i < v.len && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
        if (i == b) break;
        char id[256];
        size_t n = i - b < sizeof id - 1 ? i - b : sizeof id - 1;
        memcpy(id, p + b, n);
        id[n] = 0;
        uint32_t target = ax_by_id(d, id, n);
        a11y_aref_h r = aref_new(t);
        t->aref[r.index].from = node_handle(d, x);
        t->aref[r.index].attr = kind;
        t->aref[r.index].idref = text_add(t, id, n);
        if (target) t->aref[r.index].target = node_handle(d, target);
        if (names && target) { if (len(*names)) push_back(names, ' '); ax_text(AARGS, target, names); }
    }
}

/* the node's name, and where it came from */
static a11y_name_from ax_name_of(ACTX, uint32_t x, a11y_role role, vector<char> *out) {
    clear(*out);
    if (ax_attr(d, x, "aria-labelledby")) {
        ax_refs(AARGS, x, "aria-labelledby", A11Y_REF_ATTR_ARIA_LABELLEDBY, out);
        if (len(*out)) return A11Y_NAME_FROM_ARIA_LABELLEDBY;
    }
    uint32_t a = ax_attr(d, x, "aria-label");
    if (a && d->attr[a].value.len) {
        ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
        int any = 0;
        for (size_t i = 0; i < len(*out); i++) if (at(*out, i) != ' ') any = 1;
        if (any) return A11Y_NAME_FROM_ARIA_LABEL;
        clear(*out);
    }
    if (ax_is(d, x, "img") || ax_is(d, x, "area") || (ax_is(d, x, "input") && ax_attr_is(d, x, "type", "image"))) {
        a = ax_attr(d, x, "alt");
        if (a) { ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return A11Y_NAME_FROM_ALT; }
    }
    if (ax_field(d, x) && !(ax_is(d, x, "input") && (ax_attr_is(d, x, "type", "submit") || ax_attr_is(d, x, "type", "button") ||
                                                      ax_attr_is(d, x, "type", "reset")))) {
        /* its label: one that names it by its id, or one around it */
        uint32_t ida = ax_attr(d, x, "id");
        if (ida) {
            uint32_t fora = names_find(d, "for", 3);
            amb_span idv = d->attr[ida].value;
            for (uint32_t l = 1; l < d->node_len && fora; l++) {
                if (!d->node[l].live || !ax_is(d, l, "label")) continue;
                uint32_t f = ax_attr(d, l, "for");
                if (f && d->attr[f].value.len == idv.len && memcmp(text_ptr(d, d->attr[f].value), text_ptr(d, idv), idv.len) == 0) {
                    ax_text(AARGS, l, out);
                    if (len(*out)) return A11Y_NAME_FROM_LABEL;
                }
            }
        }
        for (uint32_t p = d->node[x].parent; p > 1; p = d->node[p].parent)
            if (ax_is(d, p, "label")) { ax_text(AARGS, p, out); if (len(*out)) return A11Y_NAME_FROM_LABEL; break; }
    }
    if (ax_is(d, x, "input") && (ax_attr_is(d, x, "type", "submit") || ax_attr_is(d, x, "type", "button") || ax_attr_is(d, x, "type", "reset"))) {
        a = ax_attr(d, x, "value");
        if (a && d->attr[a].value.len) { ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return A11Y_NAME_FROM_VALUE; }
        if (ax_attr_is(d, x, "type", "submit")) { ax_put(out, "Submit", 6); return A11Y_NAME_FROM_VALUE; }
        if (ax_attr_is(d, x, "type", "reset")) { ax_put(out, "Reset", 5); return A11Y_NAME_FROM_VALUE; }
    }
    uint32_t cap = ax_is(d, x, "table") ? ax_child(d, x, "caption") : ax_is(d, x, "figure") ? ax_child(d, x, "figcaption") : 0;
    if (cap) { ax_text(AARGS, cap, out); if (len(*out)) return A11Y_NAME_FROM_CAPTION; }
    uint32_t leg = ax_is(d, x, "fieldset") ? ax_child(d, x, "legend") : 0;
    if (leg) { ax_text(AARGS, leg, out); if (len(*out)) return A11Y_NAME_FROM_LEGEND; }
    if (ax_name_from_content(role)) {
        ax_text(AARGS, x, out);
        for (size_t i = 0; i < len(*out); i++) if (at(*out, i) != ' ') return A11Y_NAME_FROM_CONTENT;
        clear(*out);
    }
    a = ax_attr(d, x, "title");
    if (a && d->attr[a].value.len) { ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return A11Y_NAME_FROM_TITLE; }
    a = ax_field(d, x) ? ax_attr(d, x, "placeholder") : 0;
    if (a && d->attr[a].value.len) { ax_put(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return A11Y_NAME_FROM_PLACEHOLDER; }
    return A11Y_NAME_FROM_NOTHING;
}

static int ax_focusable(const dom_document *d, uint32_t x, a11y_role role) {
    if (ax_attr(d, x, "disabled")) return 0;
    uint32_t ti = ax_attr(d, x, "tabindex");
    if (ti) return !(d->attr[ti].value.len && text_ptr(d, d->attr[ti].value)[0] == '-');
    if (role == A11Y_ROLE_LINK) return 1;
    return ax_is(d, x, "button") || ax_field(d, x) || ax_attr(d, x, "contenteditable");
}

/* ---- the tree ---- */

static void ax_walk(ACTX, uint32_t x, uint32_t parent) {
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || ax_hidden(AARGS, c)) continue;
        a11y_role role;
        size_t bad_off = 0, bad_len = 0;
        int stated_role = ax_role_attr(d, c, &role, &bad_off, &bad_len);
        if (!stated_role) role = ax_implicit(d, c);
        if (bad_len) {
            a11y_badrole_h br = badrole_new(t);
            t->badrole[br.index].node = node_handle(d, c);
            uint32_t ra = ax_attr(d, c, "role");
            t->badrole[br.index].value = text_add(t, text_ptr(d, d->attr[ra].value) + bad_off, bad_len);
        }
        /* the ids it names, kept whatever its role */
        ax_refs(AARGS, c, "aria-describedby", A11Y_REF_ATTR_ARIA_DESCRIBEDBY, 0);
        ax_refs(AARGS, c, "aria-controls", A11Y_REF_ATTR_ARIA_CONTROLS, 0);
        if (ax_is(d, c, "label")) ax_refs(AARGS, c, "for", A11Y_REF_ATTR_LABEL_FOR, 0);
        if (role == A11Y_ROLE_GENERIC || role == A11Y_ROLE_PRESENTATION) {
            /* no node: its aria-labelledby kept, its children its parent's */
            if (ax_attr(d, c, "aria-labelledby")) ax_refs(AARGS, c, "aria-labelledby", A11Y_REF_ATTR_ARIA_LABELLEDBY, 0);
            ax_walk(AARGS, c, parent);
            continue;
        }
        vector<char> name;
        a11y_name_from from = ax_name_of(AARGS, c, role, &name);
        amb_span sp = ax_name(t, &name);
        a11y_anode_h h = anode_new(t);
        uint32_t a = h.index;
        t->anode[a].node = node_handle(d, c);
        t->anode[a].role = role;
        t->anode[a].stated = (uint8_t)stated_role;
        t->anode[a].name = sp;
        t->anode[a].name_from = from;
        t->anode[a].parent = parent;
        t->anode[a].focusable = (uint8_t)ax_focusable(d, c, role);
        if (role == A11Y_ROLE_HEADING) {
            uint32_t lv = 2;
            uint32_t al = ax_attr(d, c, "aria-level");
            if (al && d->attr[al].value.len) lv = (uint32_t)atoi(text_ptr(d, d->attr[al].value));
            else for (uint32_t k = 1; k <= 6; k++) { char hn[3] = { 'h', (char)('0' + k), 0 }; if (ax_is(d, c, hn)) lv = k; }
            t->anode[a].level = lv;
        }
        ax_walk(AARGS, c, a);
    }
}

a11y_status a11y_build(a11y_tree *t, dom_document *doc, css_style *style) {
    vector<uint32_t> sx;
    resize(sx, doc->node_len);
    for (uint32_t e = 1; e < style->estyle_len; e++) {      /* each element's first value: its style's */
        uint32_t x = style->estyle[e].node.index;
        if (x < doc->node_len && !at(sx, x)) sx[x] = style->estyle[e].first;
    }
    /* the root element's children: html itself is no node */
    uint32_t root = 0;
    for (uint32_t c = doc->node_len > 1 ? doc->node[1].first_child : 0; c; c = doc->node[c].next_sibling)
        if (doc->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    if (root) ax_walk(t, doc, style, &sx, root, 0);
    t->revision++;
    return A11Y_OK;
}
