/* src/script/cssom.c -- the CSS Object Model a page's scripts reach (W3C CSSOM 1, CSS Cascade
   5's adopted sheets), each part measured in Chromium first: document.styleSheets and a
   shadow root's, each sheet a CSSStyleSheet; its rules, CSSRuleList by CSSRuleList, as
   Chromium's classes; the changes insertRule, deleteRule, addRule, removeRule and the rules'
   own make, refused with Chromium's exceptions; constructed sheets, replace() and
   replaceSync(), adoptedStyleSheets; and CSSStyleDeclaration, a rule's style and an
   element's. The model is src/css/cssom.h's, in the page's host (h->om); the wrappers are
   host objects, one a record, made at their first use and kept for the page's life.

   SHEETS. A style element (HTML's or SVG's, of CSS's type) and a link of rel stylesheet with
   an href, in a connected tree, have a sheet; it is made when a script first asks for it
   (its rules read from its text when they are first asked for) and kept while its owner's
   children and its place in the tree do not change: the DOM's log, read when a script next
   asks, lets go of a sheet whose owner's children changed (a new text, a text node added,
   even the same text written anew), or that came out of the tree or went into it again,
   as Chromium makes a new sheet then. Its media list is its media attribute's until a script
   changes the list; disabled, a script's to set. A file: page's own style elements are
   readable, its links' and imports' sheets not (Chromium's origin check: a SecurityError).

   THE CASCADE. A change moves the model's revision and makes the view stale (src/script/
   view.c); before the next cascade (a script's read of the rendering, and the page's own
   at rest: script_settle) the host tells the CSS module (css_om_reg_*) each sheet a script
   changed: disabled, of other media, or its rules' text as CSSOM serializes them; and each
   tree's adopted sheets in their order. src/css/css.c reads them in their places, so a page
   that touches no CSSOM is cascaded as before.

   ADOPTED SHEETS. adoptedStyleSheets is an observable array (WebIDL 3.10.3): a proxy over an
   array of the engine's whose set, deleteProperty and defineProperty traps check each sheet
   (a CSSStyleSheet, constructed, of this document) and tell the host the tree's list anew. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "../net/url.h"
#include "host.h"
#include "script.h"
#include "../dom/dom.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);
void sh_page_url_v(js_vm *vm, vector<char> *out);

enum {
    CO_ILLEGAL = SH_CSSOM_FIRST, CO_SHEET_CTOR,
    /* StyleSheet and CSSStyleSheet */
    CO_S_TYPE, CO_S_HREF, CO_S_OWNER, CO_S_PARENT, CO_S_TITLE, CO_S_MEDIA, CO_S_DISABLED, CO_S_DISABLED_SET, CO_S_OWNER_RULE, CO_S_RULES,
    CO_S_INSERT, CO_S_DELETE, CO_S_ADD, CO_S_REMOVE, CO_S_REPLACE, CO_S_REPLACE_SYNC,
    /* the lists */
    CO_L_LENGTH, CO_L_ITEM, CO_M_TEXT, CO_M_TEXT_SET, CO_M_APPEND, CO_M_DELETE, CO_M_TOSTRING,
    /* CSSRule and its classes */
    CO_R_TYPE, CO_R_TEXT, CO_R_TEXT_SET, CO_R_PARENT_RULE, CO_R_PARENT_SHEET, CO_R_RULES, CO_R_INSERT, CO_R_DELETE, CO_R_CONDITION,
    CO_R_MEDIA, CO_R_CNAME, CO_R_CQUERY, CO_R_SELECTOR, CO_R_SELECTOR_SET, CO_R_STYLE, CO_R_NAME, CO_R_NAME_SET, CO_R_NAMELIST,
    CO_R_HREF, CO_R_SHEET, CO_R_LAYER, CO_R_SUPPORTS, CO_R_KEYTEXT, CO_R_KEYTEXT_SET, CO_R_LENGTH, CO_R_APPEND, CO_R_KDELETE, CO_R_FIND,
    CO_R_NSURI, CO_R_PREFIX, CO_R_SYNTAX, CO_R_INHERITS, CO_R_INITIAL, CO_R_START, CO_R_END, CO_R_COUNTER, CO_R_COUNTER_SET,
    /* CSSStyleDeclaration's own (the others are script.c's natives, through script_cssom_style) */
    CO_D_PRIORITY, CO_D_ITEM,
    /* the rules' rest: a palette's and a feature values rule's family, a palette's base and
       colours, a view transition's navigation and types */
    CO_R_FAMILY, CO_R_BASE_PALETTE, CO_R_OVERRIDE, CO_R_NAVIGATION, CO_R_TYPES,
    /* the trees' lists, adoptedStyleSheets and its traps, an element's sheet */
    CO_T_SHEETS, CO_T_ADOPTED, CO_T_ADOPTED_SET, CO_A_SET, CO_A_DELETE, CO_A_DEFINE, CO_E_SHEET,
    CO_N
};
/* the classes' prototypes, in the model's protos */
enum {
    CP_STYLESHEET, CP_CSSSTYLESHEET, CP_SHEETLIST, CP_RULELIST, CP_MEDIALIST, CP_RULE, CP_GROUPING, CP_CONDITION, CP_STYLE, CP_MEDIA,
    CP_SUPPORTS, CP_CONTAINER, CP_LAYER_BLOCK, CP_LAYER_STATEMENT, CP_IMPORT, CP_FONT_FACE, CP_KEYFRAMES, CP_KEYFRAME, CP_PAGE, CP_MARGIN,
    CP_NAMESPACE, CP_PROPERTY, CP_COUNTER_STYLE, CP_SCOPE, CP_STARTING_STYLE, CP_FONT_FEATURE_VALUES, CP_FONT_PALETTE_VALUES,
    CP_VIEW_TRANSITION, CP_POSITION_TRY, CP_NESTED_DECLARATIONS, CP_HANDLER, CP_N
};
/* a media list's owner: a sheet's, an @media rule's, an @import rule's */
enum { CM_SHEET, CM_MEDIA_RULE, CM_IMPORT_RULE };

static uint32_t co_atom(js_vm *vm, const char *s) { return js_atom_ascii(vm, s); }
static jv co_str(js_vm *vm, const vector<char> *v) { return sh_str(vm, data(*v), len(*v)); }
static void co_cat(vector<char> *out, const char *s) { append_range(*out, s, strlen(s)); }
static jv co_span(js_vm *vm, uint32_t off, uint32_t n) {
    vector<char> v;
    css_om_span(&vm->host->om, off, n, &v);
    return co_str(vm, &v);
}

/* ---- the view and the cascade told ---- */

/* a change of the model: its revision moved, the view stale */
static void co_changed(struct js_host *h) {
    h->om.rev++;
    h->view_ok = 0;
}

/* ---- the DOM read ---- */

static int co_is_elem(const dom_document *d, uint32_t x) { return x && x < d->node_len && d->node[x].live && d->node[x].kind == DOM_NODE_KIND_ELEMENT; }
static int co_named(const dom_document *d, uint32_t x, const char *name, dom_namespace ns) {
    return co_is_elem(d, x) && d->node[x].ns == ns && names_is(d, d->node[x].name, name, strlen(name));
}
/* x is connected: in the document, its shadow roots' hosts on the way */
static int co_connected(const dom_document *d, uint32_t x) {
    for (size_t steps = 0; x && x != 1 && steps <= d->node_len; steps++) {
        uint32_t p = d->node[x].parent;
        if (!p) p = dom_shadow_root_host(d, x);
        x = p;
    }
    return x == 1;
}
/* an attribute's value of x, into out: 1 when x has it */
static int co_attr(const dom_document *d, uint32_t x, const char *name, vector<char> *out) {
    clear(*out);
    uint32_t a = sh_attr_z(d, x, name);
    if (!a) return 0;
    append_range(*out, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
    return 1;
}
/* an attribute's value of x by its FNV-1a hash, its absence a value of its own: an attribute
   known again without its text kept */
static uint32_t co_attr_sig(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = sh_attr_z(d, x, name);
    if (!a) return 1;
    const char *s = text_ptr(d, d->attr[a].value);
    uint32_t hv = 2166136261u;
    for (size_t i = 0; i < d->attr[a].value.len; i++) {
        hv ^= (unsigned char)s[i];
        hv *= 16777619u;
    }
    return hv | 2;
}
/* whether a link's rel has the token stylesheet */
static int co_rel_stylesheet(const vector<char> *rel) {
    size_t i = 0, n = len(*rel);
    while (i < n) {
        while (i < n && sh_ws((*rel)[i])) i++;
        size_t b = i;
        while (i < n && !sh_ws((*rel)[i])) i++;
        if (i - b == 10 && sh_ci_eq(data(*rel) + b, "stylesheet", 10)) return 1;
    }
    return 0;
}
/* x's sheet's origin when x has a sheet now (CSS_OM_O_STYLE, CSS_OM_O_LINK), else 0 */
static uint32_t co_has_sheet(const dom_document *d, uint32_t x) {
    if (!co_is_elem(d, x) || !co_connected(d, x)) return 0;
    vector<char> v;
    if (co_named(d, x, "style", DOM_NAMESPACE_HTML) || co_named(d, x, "style", DOM_NAMESPACE_SVG)) {
        /* its type empty or text/css, ASCII case-insensitively, as it is written (HTML 4.2.6) */
        if (co_attr(d, x, "type", &v) && len(v) && !(len(v) == 8 && sh_ci_eq(data(v), "text/css", 8))) return 0;
        return CSS_OM_O_STYLE;
    }
    if (co_named(d, x, "link", DOM_NAMESPACE_HTML)) {
        if (!co_attr(d, x, "rel", &v) || !co_rel_stylesheet(&v)) return 0;
        if (!co_attr(d, x, "href", &v) || !len(v)) return 0;
        if (sh_attr_z(d, x, "disabled")) return 0;
        return CSS_OM_O_LINK;
    }
    return 0;
}
/* a style element's text: its text children's data */
static void co_style_text(const dom_document *d, uint32_t x, vector<char> *out) {
    clear(*out);
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling)
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) append_range(*out, text_ptr(d, d->node[c].data), d->node[c].data.len);
}
/* the root of x's tree (1 the document's, a shadow root) */
static uint32_t co_tree(const dom_document *d, uint32_t x) { return dom_tree_root(d, x); }
/* the document's URL */
static void co_doc_url(js_vm *vm, vector<char> *out) { sh_page_url_v(vm, out); }
/* the loader's resource a link element fetched (its stylesheet), 0 for none */
static uint32_t co_link_resource(const net_loader *nt, uint32_t x) {
    for (uint32_t r = 1; nt && r < nt->resource_len; r++)
        if (nt->resource[r].live && nt->resource[r].elem.index == x && nt->resource[r].kind == NET_KIND_STYLESHEET) return r;
    return 0;
}
/* the loader's resource of a URL, 0 for none */
static uint32_t co_url_resource(const net_loader *nt, const char *url) {
    size_t n = strlen(url);
    for (uint32_t r = 1; nt && r < nt->resource_len; r++) {
        if (!nt->resource[r].live) continue;
        amb_span u = nt->resource[r].url, f = nt->resource[r].final_url;
        if ((u.len == n && memcmp(text_ptr(nt, u), url, n) == 0) || (f.len == n && memcmp(text_ptr(nt, f), url, n) == 0)) return r;
    }
    return 0;
}
/* whether a sheet fetched from url is readable by a page of doc_url: of its origin, and the page
   no file: one (Chromium gives a file's sheets an opaque origin) */
static int co_same_origin(const char *doc_url, const char *url) {
    if (!strncmp(doc_url, "file:", 5)) return 0;
    char a[600], b[600];
    if (!net_url_origin(doc_url, a, sizeof a) || !net_url_origin(url, b, sizeof b)) return 0;
    return !strcmp(a, b);
}

/* ---- the sheets of the DOM's elements ---- */

/* the DOM's changes since the sheets were last checked against it (its mutation log): a sheet whose
   owner's children changed, or that came out of its tree or went into one, or no longer has one,
   let go of (its owner none, its object kept) */
static void co_check(struct js_host *h) {
    css_om *m = &h->om;
    const dom_document *d = h->d;
    if (m->dom_rev == dom_revision(d) && m->log_pos == d->mutation_len) return;
    size_t from = m->log_pos, n = d->mutation_len;
    m->log_pos = (uint32_t)n;
    m->dom_rev = dom_revision(d);
    int owners = 0;
    for (size_t s = 1; s < len(m->sheets); s++) if (m->sheets[s].live && m->sheets[s].owner) owners = 1;
    if (!owners) return;
    for (size_t i = from; i < n; i++) {
        dom_mutation e = d->mutation[i];
        uint32_t t = e.target, o = e.other;
        int child = e.op == DOM_OP_APPEND_CHILD || e.op == DOM_OP_INSERT_BEFORE || e.op == DOM_OP_REMOVE_CHILD;
        for (size_t s = 1; s < len(m->sheets); s++) {
            css_om_sheet sh = m->sheets[s];
            if (!sh.live || !sh.owner) continue;
            uint32_t x = sh.owner;
            int gone = 0;
            if (child && t == x && sh.origin == CSS_OM_O_STYLE) gone = 1;
            if (e.op == DOM_OP_SET_DATA && t && t < d->node_len && d->node[t].parent == x && sh.origin == CSS_OM_O_STYLE) gone = 1;
            if (child && o) {
                /* the owner itself or a subtree it is in moved */
                for (uint32_t y = x, steps = 0; y && steps <= d->node_len && !gone; steps++) {
                    if (y == o) gone = 1;
                    uint32_t p = y < d->node_len ? d->node[y].parent : 0;
                    if (!p && y < d->node_len) p = dom_shadow_root_host(d, y);
                    y = p;
                }
            }
            if (gone) { m->sheets[s].owner = 0; m->rev++; }
        }
    }
    /* and each owner as it is now: live, connected, still of a sheet; a media list a script set
       given up for the media attribute once that changed (Chromium sets the list from it) */
    for (size_t s = 1; s < len(m->sheets); s++) {
        css_om_sheet sh = m->sheets[s];
        if (!sh.live || !sh.owner) continue;
        uint32_t x = sh.owner;
        if (x >= d->node_len || !d->node[x].live || d->node[x].gen != sh.ogen || co_has_sheet(d, x) != sh.origin) { m->sheets[s].owner = 0; m->rev++; continue; }
        if (sh.media_set && co_attr_sig(d, x, "media") != sh.spare) { m->sheets[s].media_set = 0; m->rev++; }
    }
    h->view_ok = 0;
}
/* x's sheet: its record, made when x has a sheet and none is made yet; 0 when x has none */
static uint32_t co_owned(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    co_check(h);
    css_om *m = &h->om;
    const dom_document *d = h->d;
    for (size_t s = 1; s < len(m->sheets); s++)
        if (m->sheets[s].live && m->sheets[s].owner == x && x < d->node_len && m->sheets[s].ogen == d->node[x].gen) return (uint32_t)s;
    uint32_t origin = co_has_sheet(d, x);
    if (!origin) return 0;
    if (!m->log_pos && !m->dom_rev) {
        m->log_pos = (uint32_t)d->mutation_len;
        m->dom_rev = dom_revision(d);
    }
    m->on = 1;
    uint32_t s = css_om_sheet_new(m, origin);
    m->sheets[s].owner = x;
    m->sheets[s].ogen = d->node[x].gen;
    m->sheets[s].tree = co_tree(d, x);
    vector<char> v, url;
    if (co_attr(d, x, "title", &v)) {
        m->sheets[s].toff = css_om_put(m, data(v), len(v));
        m->sheets[s].tlen = (uint32_t)len(v);
        m->sheets[s].flags |= 1;
    }
    co_doc_url(vm, &url);
    push_back(url, 0);
    if (origin == CSS_OM_O_STYLE) {
        co_style_text(d, x, &v);
        m->sheets[s].soff = css_om_put(m, data(v), len(v));
        m->sheets[s].slen = (uint32_t)len(v);
    } else {
        /* a link: its URL resolved, its text the loader's */
        co_attr(d, x, "href", &v);
        push_back(v, 0);
        char abs[4096];
        if (!net_url_resolve(data(url), data(v), abs, sizeof abs)) snprintf(abs, sizeof abs, "%s", data(v));
        m->sheets[s].hoff = css_om_put(m, abs, strlen(abs));
        m->sheets[s].hlen = (uint32_t)strlen(abs);
        m->sheets[s].clean = co_same_origin(data(url), abs);
        uint32_t r = co_link_resource(h->nt, x);
        if (r && h->nt->resource[r].outcome == NET_OUTCOME_OK) {
            amb_span b = h->nt->resource[r].body;
            m->sheets[s].soff = css_om_put(m, bytes_ptr(h->nt, b), b.len);
            m->sheets[s].slen = b.len;
        }
    }
    return s;
}
/* a sheet's rules read from its text, the first time they are asked for */
static void co_parse(struct js_host *h, uint32_t s) {
    css_om *m = &h->om;
    if (m->sheets[s].parsed) return;
    vector<char> t;
    css_om_span(m, m->sheets[s].soff, m->sheets[s].slen, &t);
    css_om_read(m, s, data(t), len(t), 0);
}
/* the sheets of the tree whose root is root, in its order (the document's: root 1): their records */
static void co_tree_sheets(js_vm *vm, uint32_t root, vector<uint32_t> *out) {
    clear(*out);
    const dom_document *d = vm->host->d;
    for (uint32_t x = d->node[root].first_child; x;) {
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) {
            uint32_t s = co_owned(vm, x);
            if (s) push_back(*out, s);
        }
        /* the tree's own nodes in order (a template's contents and other trees not) */
        if (d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x && x != root && !d->node[x].next_sibling) x = d->node[x].parent;
        if (!x || x == root) break;
        x = d->node[x].next_sibling;
    }
}

/* ---- wrappers ---- */

static jv co_host_obj(js_vm *vm, uint32_t kind, uint32_t proto, uint32_t a, uint32_t b) {
    uint32_t o = js_obj_new(vm, JK_HOST, proto);
    vm->objs[o].a = a;
    vm->objs[o].b = b;
    vm->objs[o].c = kind;
    return jv_obj(o);
}
static uint32_t co_proto(js_vm *vm, uint32_t cp) {
    css_om *m = &vm->host->om;
    return cp < len(m->protos) ? m->protos[cp] : vm->object_proto;
}
static jv co_sheet_obj(js_vm *vm, uint32_t s) {
    if (!s) return JV_NULL;
    css_om *m = &vm->host->om;
    if (!m->sheets[s].obj) {
        jv o = co_host_obj(vm, HK_SHEET, co_proto(vm, CP_CSSSTYLESHEET), s, 0);
        m->sheets[s].obj = jv_idx(o);
    }
    return jv_obj(m->sheets[s].obj);
}
static uint32_t co_rule_class(uint32_t kind) {
    switch (kind) {
    case CSS_OM_STYLE: return CP_STYLE;
    case CSS_OM_IMPORT: return CP_IMPORT;
    case CSS_OM_MEDIA: return CP_MEDIA;
    case CSS_OM_FONT_FACE: return CP_FONT_FACE;
    case CSS_OM_PAGE: return CP_PAGE;
    case CSS_OM_KEYFRAMES: return CP_KEYFRAMES;
    case CSS_OM_KEYFRAME: return CP_KEYFRAME;
    case CSS_OM_MARGIN: return CP_MARGIN;
    case CSS_OM_NAMESPACE: return CP_NAMESPACE;
    case CSS_OM_COUNTER_STYLE: return CP_COUNTER_STYLE;
    case CSS_OM_SUPPORTS: return CP_SUPPORTS;
    case CSS_OM_FONT_FEATURE_VALUES: return CP_FONT_FEATURE_VALUES;
    case CSS_OM_CONTAINER: return CP_CONTAINER;
    case CSS_OM_LAYER_BLOCK: return CP_LAYER_BLOCK;
    case CSS_OM_LAYER_STATEMENT: return CP_LAYER_STATEMENT;
    case CSS_OM_PROPERTY: return CP_PROPERTY;
    case CSS_OM_SCOPE: return CP_SCOPE;
    case CSS_OM_STARTING_STYLE: return CP_STARTING_STYLE;
    case CSS_OM_FONT_PALETTE_VALUES: return CP_FONT_PALETTE_VALUES;
    case CSS_OM_VIEW_TRANSITION: return CP_VIEW_TRANSITION;
    case CSS_OM_POSITION_TRY: return CP_POSITION_TRY;
    default: return CP_NESTED_DECLARATIONS;
    }
}
/* a rule's CSSRule.type (CSSOM's constants: 0 for the classes that have none) */
static uint32_t co_rule_type(uint32_t kind) {
    switch (kind) {
    case CSS_OM_STYLE: return 1;
    case CSS_OM_IMPORT: return 3;
    case CSS_OM_MEDIA: return 4;
    case CSS_OM_FONT_FACE: return 5;
    case CSS_OM_PAGE: return 6;
    case CSS_OM_KEYFRAMES: return 7;
    case CSS_OM_KEYFRAME: return 8;
    case CSS_OM_MARGIN: return 9;
    case CSS_OM_NAMESPACE: return 10;
    case CSS_OM_COUNTER_STYLE: return 11;
    case CSS_OM_SUPPORTS: return 12;
    case CSS_OM_FONT_FEATURE_VALUES: return 14;
    default: return 0;
    }
}
static jv co_rule_obj(js_vm *vm, uint32_t r) {
    if (!r) return JV_NULL;
    css_om *m = &vm->host->om;
    if (!m->rules[r].obj) {
        jv o = co_host_obj(vm, HK_RULE, co_proto(vm, co_rule_class(m->rules[r].kind)), r, 0);
        m->rules[r].obj = jv_idx(o);
    }
    return jv_obj(m->rules[r].obj);
}
/* a list of rules: a sheet's (of_rule 0) or a rule's */
static jv co_rules_obj(js_vm *vm, uint32_t owner, int of_rule) {
    css_om *m = &vm->host->om;
    uint32_t have = of_rule ? m->rules[owner].rules_obj : m->sheets[owner].rules_obj;
    if (have) return jv_obj(have);
    jv o = co_host_obj(vm, HK_RULES, co_proto(vm, CP_RULELIST), owner, (uint32_t)of_rule);
    if (of_rule) m->rules[owner].rules_obj = jv_idx(o);
    else m->sheets[owner].rules_obj = jv_idx(o);
    return o;
}
static jv co_media_obj(js_vm *vm, uint32_t owner, uint32_t kind) {
    css_om *m = &vm->host->om;
    uint32_t have = kind == CM_SHEET ? m->sheets[owner].media_obj : m->rules[owner].media_obj;
    if (have) return jv_obj(have);
    jv o = co_host_obj(vm, HK_MEDIA, co_proto(vm, CP_MEDIALIST), owner, kind);
    if (kind == CM_SHEET) m->sheets[owner].media_obj = jv_idx(o);
    else m->rules[owner].media_obj = jv_idx(o);
    return o;
}
/* a rule's style: its block's CSSStyleDeclaration (a ROOT kept by the rule) */
static jv co_style_obj(js_vm *vm, uint32_t r) {
    css_om *m = &vm->host->om;
    if (!m->rules[r].block) return JV_NULL;
    if (!m->rules[r].style_obj) {
        jv o = co_host_obj(vm, HK_DECL, vm->host->protos[HP_STYLE], m->rules[r].block, 0);
        m->rules[r].style_obj = jv_idx(o);
    }
    return jv_obj(m->rules[r].style_obj);
}
/* a tree's entry in the model's trees (five numbers: root, generation, its StyleSheetList, its
   adoptedStyleSheets proxy, the array behind it): its place, made when it has none */
static size_t co_tree_entry(js_vm *vm, uint32_t root) {
    css_om *m = &vm->host->om;
    const dom_document *d = vm->host->d;
    uint32_t gen = d->node[root].gen;
    for (size_t i = 0; i + 4 < len(m->trees); i += 5)
        if (m->trees[i] == root && m->trees[i + 1] == gen) return i;
    size_t i = len(m->trees);
    push_back(m->trees, root);
    push_back(m->trees, gen);
    push_back(m->trees, 0u);
    push_back(m->trees, 0u);
    push_back(m->trees, 0u);
    return i;
}

/* the record of thisv when it is a host object of kind: else 0, a TypeError thrown */
static uint32_t co_this(js_vm *vm, jv thisv, uint32_t kind) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_HOST && vm->objs[jv_idx(thisv)].c == kind && vm->objs[jv_idx(thisv)].a)
        return vm->objs[jv_idx(thisv)].a;
    js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
    return 0;
}

/* ---- errors, as Chromium's bindings word them ---- */

static jv co_dom_error(js_vm *vm, const char *name, const char *prefix, const char *msg) {
    char full[2400];
    snprintf(full, sizeof full, "%s%s", prefix, msg);
    return sh_dom_error(vm, name, full);
}
static jv co_args(js_vm *vm, const char *what, const char *iface, uint32_t need, uint32_t have) {
    char msg[300];
    snprintf(msg, sizeof msg, "Failed to execute '%s' on '%s': %u argument%s required, but only %u present.", what, iface, need, need == 1 ? "" : "s", have);
    return js_throw_msg(vm, vm->type_error_proto, msg);
}
/* WebIDL's unsigned long: a number modulo 2^32 */
static uint32_t co_u32(js_vm *vm, jv v) {
    double n = js_to_number(vm, v);
    if (n != n || n == 1.0 / 0.0 || n == -1.0 / 0.0) return 0;
    double t = n < 0 ? -floor(-n) : floor(n);
    double r = fmod(t, 4294967296.0);
    if (r < 0) r += 4294967296.0;
    return (uint32_t)r;
}

/* ---- media lists ---- */

/* a media list's text, into out */
static void co_media_text(js_vm *vm, uint32_t owner, uint32_t kind, vector<char> *out) {
    clear(*out);
    css_om *m = &vm->host->om;
    if (kind == CM_SHEET) {
        css_om_sheet s = m->sheets[owner];
        if (s.media_set || !s.owner) { css_om_span(m, s.moff, s.mlen, out); return; }
        vector<char> a;
        if (co_attr(vm->host->d, s.owner, "media", &a)) css_om_media_text(data(a), len(a), out);
        return;
    }
    css_om_rule r = m->rules[owner];
    if (kind == CM_MEDIA_RULE) css_om_span(m, r.aoff, r.alen, out);
    else css_om_span(m, r.boff, r.blen, out);
}
/* a media list's text set (normalized already) */
static void co_media_set(js_vm *vm, uint32_t owner, uint32_t kind, const vector<char> *t) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    uint32_t off = css_om_put(m, data(*t), len(*t));
    if (kind == CM_SHEET) {
        m->sheets[owner].moff = off;
        m->sheets[owner].mlen = (uint32_t)len(*t);
        m->sheets[owner].media_set = 1;
        /* the media attribute as it is now: its change later sets the list from it again */
        uint32_t x = m->sheets[owner].owner;
        m->sheets[owner].spare = x ? co_attr_sig(h->d, x, "media") : 0;
    } else {
        if (kind == CM_MEDIA_RULE) { m->rules[owner].aoff = off; m->rules[owner].alen = (uint32_t)len(*t); }
        else { m->rules[owner].boff = off; m->rules[owner].blen = (uint32_t)len(*t); }
        uint32_t s = m->rules[owner].sheet;
        if (s) m->sheets[s].changed = 1;
    }
    co_changed(h);
}

/* ---- a sheet's rules changed ---- */

/* the sheet a rule is in changed (its rules are the cascade's from now on) */
static void co_rule_changed(struct js_host *h, uint32_t r) {
    uint32_t s = r < len(h->om.rules) ? h->om.rules[r].sheet : 0;
    if (s) h->om.sheets[s].changed = 1;
    co_changed(h);
}

/* ---- the natives ---- */

/* CSSStyleSheet's insertRule (and addRule's): the index or JV_HOLE thrown */
static jv co_insert(js_vm *vm, uint32_t s, const vector<char> *text, uint32_t index, const char *what) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    char pre[160];
    snprintf(pre, sizeof pre, "Failed to execute '%s' on 'CSSStyleSheet': ", what);
    if (!m->sheets[s].clean) return co_dom_error(vm, "SecurityError", pre, "Cannot access StyleSheet to insertRule");
    co_parse(h, s);
    uint32_t count = css_om_count(m, s, 0);
    if (index > count) {
        char msg[200];
        snprintf(msg, sizeof msg, "The index provided (%u) is larger than the maximum index (%u).", index, count);
        return co_dom_error(vm, "IndexSizeError", pre, msg);
    }
    uint32_t made;
    uint32_t mode = m->sheets[s].origin == CSS_OM_O_CONSTRUCTED ? CSS_OM_READ_CONSTRUCTED : 0;
    int e = css_om_insert(m, s, 0, index, data(*text), len(*text), mode, &made);
    if (e == CSS_OM_E_SYNTAX) {
        vector<char> msg;
        co_cat(&msg, "Failed to parse the rule '");
        append_range(msg, data(*text), len(*text));
        co_cat(&msg, "'.");
        push_back(msg, 0);
        return co_dom_error(vm, "SyntaxError", pre, data(msg));
    }
    if (e == CSS_OM_E_CONSTRUCTED_IMPORT) return co_dom_error(vm, "SyntaxError", pre, "Can't insert @import rules into a constructed stylesheet.");
    if (e == CSS_OM_E_STATE) return co_dom_error(vm, "InvalidStateError", pre, "Failed to insert the rule");
    if (e) return co_dom_error(vm, "HierarchyRequestError", pre, "Failed to insert the rule.");
    m->sheets[s].changed = 1;
    co_changed(h);
    return jv_num(index);
}
static jv co_delete(js_vm *vm, uint32_t s, uint32_t index, const char *what) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    char pre[160];
    snprintf(pre, sizeof pre, "Failed to execute '%s' on 'CSSStyleSheet': ", what);
    if (!m->sheets[s].clean) return co_dom_error(vm, "SecurityError", pre, "Cannot access StyleSheet to deleteRule");
    co_parse(h, s);
    uint32_t count = css_om_count(m, s, 0);
    if (index >= count) {
        if (!count) return co_dom_error(vm, "IndexSizeError", pre, "Rules list is empty.");
        char msg[200];
        snprintf(msg, sizeof msg, "The index provided (%u) is larger than the maximum index (%u).", index, count - 1);
        return co_dom_error(vm, "IndexSizeError", pre, msg);
    }
    if (css_om_remove(m, s, 0, index)) return co_dom_error(vm, "InvalidStateError", pre, "Failed to delete rule");
    m->sheets[s].changed = 1;
    co_changed(h);
    return JV_UNDEF;
}
/* a grouping rule's insertRule and deleteRule (CSSGroupingRule's; a style rule's CSSStyleRule's) */
static jv co_group_insert(js_vm *vm, uint32_t r, const vector<char> *text, uint32_t index) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    char pre[160];
    snprintf(pre, sizeof pre, "Failed to execute 'insertRule' on '%s': ", m->rules[r].kind == CSS_OM_STYLE ? "CSSStyleRule" : "CSSGroupingRule");
    uint32_t count = css_om_count(m, 0, r);
    if (index > count) {
        char msg[200];
        snprintf(msg, sizeof msg, "the index %u must be less than or equal to the length of the rule list.", index);
        return co_dom_error(vm, "IndexSizeError", pre, msg);
    }
    uint32_t made;
    int e = css_om_insert(m, m->rules[r].sheet, r, index, data(*text), len(*text), 0, &made);
    if (e == CSS_OM_E_SYNTAX) {
        vector<char> msg;
        co_cat(&msg, "the rule '");
        append_range(msg, data(*text), len(*text));
        co_cat(&msg, "' is invalid and cannot be parsed.");
        push_back(msg, 0);
        return co_dom_error(vm, "SyntaxError", pre, data(msg));
    }
    if (e == CSS_OM_E_NAMESPACE_GROUP) return co_dom_error(vm, "HierarchyRequestError", pre, "'@namespace' rules cannot be inserted inside a group rule.");
    if (e == CSS_OM_E_IMPORT_GROUP) return co_dom_error(vm, "HierarchyRequestError", pre, "'@import' rules cannot be inserted inside a group rule.");
    if (e) return co_dom_error(vm, "HierarchyRequestError", pre, "Failed to insert the rule.");
    co_rule_changed(h, r);
    return jv_num(index);
}
static jv co_group_delete(js_vm *vm, uint32_t r, uint32_t index) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    char pre[160];
    snprintf(pre, sizeof pre, "Failed to execute 'deleteRule' on '%s': ", m->rules[r].kind == CSS_OM_STYLE ? "CSSStyleRule" : "CSSGroupingRule");
    if (index >= css_om_count(m, 0, r)) {
        char msg[200];
        snprintf(msg, sizeof msg, "the index %u is greated than the length of the rule list.", index);
        return co_dom_error(vm, "IndexSizeError", pre, msg);
    }
    css_om_remove(m, 0, r, index);
    co_rule_changed(h, r);
    return JV_UNDEF;
}
/* replace() and replaceSync(): a constructed sheet's rules read anew (its @import rules refused,
   with Chromium's warning once) */
static int co_replace(js_vm *vm, uint32_t s, jv text) {
    struct js_host *h = vm->host;
    vector<char> t;
    if (!sh_utf8(vm, text, &t)) return 0;
    uint32_t warn = css_om_read(&h->om, s, data(t), len(t), CSS_OM_READ_CONSTRUCTED);
    if (warn) {
        const char *w = "@import rules are not allowed here. See https://github.com/WICG/construct-stylesheets/issues/119#issuecomment-588352418.";
        js_console(vm, 2, js_str_utf8(vm, w, strlen(w)));
    }
    h->om.sheets[s].changed = 1;
    co_changed(h);
    return 1;
}
/* a descriptor's value of a rule's block, into out: 1 when it has one */
static int co_desc(js_vm *vm, uint32_t r, uint32_t kind, const char *name, vector<char> *out) {
    clear(*out);
    css_om *m = &vm->host->om;
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    css_om_block_load(m, m->rules[r].block, &dl);
    css_om_get(&dl, kind, name, strlen(name), out);
    int has = len(*out) > 0;
    CSS_OM_DL_DROP(dl);
    return has;
}

/* the adopted sheets of a tree told anew from its array (each element a sheet's object) */
static void co_adopted_mirror(js_vm *vm, size_t te) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    uint32_t root = m->trees[te], gen = m->trees[te + 1], arr = m->trees[te + 4];
    vector<uint32_t> kept;
    for (size_t i = 0; i + 2 < len(m->adopted); i += 3)
        if (!(m->adopted[i] == root && m->adopted[i + 1] == gen)) {
            push_back(kept, m->adopted[i]);
            push_back(kept, m->adopted[i + 1]);
            push_back(kept, m->adopted[i + 2]);
        }
    if (arr) {
        uint32_t n = vm->objs[arr].b;
        for (uint32_t i = 0; i < n; i++) {
            jv v = js_array_get(vm, arr, i);
            if (!jv_is_obj(v) || vm->objs[jv_idx(v)].kind != JK_HOST || vm->objs[jv_idx(v)].c != HK_SHEET) continue;
            push_back(kept, root);
            push_back(kept, gen);
            push_back(kept, vm->objs[jv_idx(v)].a);
        }
    }
    clear(m->adopted);
    for (size_t i = 0; i < len(kept); i++) push_back(m->adopted, kept[i]);
    co_changed(h);
}
/* what a value is as a CSSStyleSheet (WebIDL's conversion): 1 a sheet of this document, 2 one of
   another frame's document (through the frames' membrane), its constructedness in *made; 0 none */
static int co_sheet_kind(js_vm *vm, jv v, int *made) {
    struct js_host *h = vm->host;
    *made = 0;
    if (!jv_is_obj(v)) return 0;
    uint32_t o = jv_idx(v);
    if (vm->objs[o].kind == JK_HOST && vm->objs[o].c == HK_SHEET) {
        *made = h->om.sheets[vm->objs[o].a].origin == CSS_OM_O_CONSTRUCTED;
        return 1;
    }
    if (vm->objs[o].kind != JK_PROXY || !h->mhandler || vm->objs[o].b != h->mhandler || !h->fr) return 0;
    for (size_t i = 0; i + 2 < len(h->remotes); i += 3) {
        if (h->remotes[i + 2] != o) continue;
        uint32_t k = h->remotes[i], r = h->remotes[i + 1];
        if (k >= SCRIPT_DOCS || !h->fr->vm[k]) return 0;
        const js_vm *ov = h->fr->vm[k];
        if (r >= len(ov->objs) || ov->objs[r].kind != JK_HOST || ov->objs[r].c != HK_SHEET || !ov->host) return 0;
        uint32_t s = ov->objs[r].a;
        *made = s < len(ov->host->om.sheets) && ov->host->om.sheets[s].origin == CSS_OM_O_CONSTRUCTED;
        return 2;
    }
    return 0;
}
/* a value converted to a CSSStyleSheet: 0, or 1 with the TypeError thrown (the prefix given) */
static int co_adopt_convert(js_vm *vm, jv v, const char *prefix) {
    int made;
    if (co_sheet_kind(vm, v, &made)) return 0;
    char msg[400];
    snprintf(msg, sizeof msg, "%sFailed to convert value to 'CSSStyleSheet'.", prefix);
    js_throw_msg(vm, vm->type_error_proto, msg);
    return 1;
}
/* a sheet to adopt checked (TreeScope's OnAdoptedStyleSheetSet): 0 when it is a constructed sheet
   of this document; else 1, NotAllowedError thrown with the prefix given */
static int co_adopt_check(js_vm *vm, jv v, const char *prefix) {
    int made;
    int k = co_sheet_kind(vm, v, &made);
    if (!made) { co_dom_error(vm, "NotAllowedError", prefix, "Can't adopt non-constructed stylesheets."); return 1; }
    if (k == 2) { co_dom_error(vm, "NotAllowedError", prefix, "Sharing constructed stylesheets in multiple documents is not allowed"); return 1; }
    return 0;
}
/* a value to adopt, converted then checked */
static int co_adoptable(js_vm *vm, jv v, const char *prefix) {
    return co_adopt_convert(vm, v, prefix) || co_adopt_check(vm, v, prefix);
}
/* a sequence of sheets to adopt read from an iterable v into the array list (held among the
   roots by the caller, the iterator here): each converted as it is read, then each checked; 1,
   or 0 with the exception thrown */
static int co_adopt_list(js_vm *vm, jv v, const char *pre, uint32_t list) {
    struct js_host *h = vm->host;
    jv it = js_iter_of(vm, v);
    if (vm->throwing) return 0;
    push_back(h->om.aux, jv_idx(it));
    jv x;
    while (js_iter_step(vm, jv_idx(it), &x)) {
        if (co_adopt_convert(vm, x, pre)) {
            js_iter_close(vm, jv_idx(it));
            return 0;
        }
        js_array_push(vm, list, x);
    }
    if (vm->throwing) return 0;
    uint32_t n = vm->objs[list].b;
    for (uint32_t i = 0; i < n; i++)
        if (co_adopt_check(vm, js_array_get(vm, list, i), pre)) return 0;
    return 1;
}
/* the tree a document's or a shadow root's this is (its root, 0 when it is neither) */
static uint32_t co_tree_of(js_vm *vm, jv thisv) {
    uint32_t x = sh_node(vm, thisv, 0);
    if (!x) return 0;
    const dom_document *d = vm->host->d;
    if (x == 1 || dom_is_shadow_root(d, x)) return x;
    return 0;
}
/* a tree's adoptedStyleSheets: its observable array, made at its first use */
static jv co_adopted(js_vm *vm, uint32_t root) {
    struct js_host *h = vm->host;
    size_t te = co_tree_entry(vm, root);
    if (!h->om.trees[te + 3]) {
        uint32_t arr = js_array_new(vm, 0);
        h->om.trees[te + 4] = arr;
        uint32_t px = js_obj_new(vm, JK_PROXY, vm->array_proto);
        vm->objs[px].a = arr;
        vm->objs[px].b = co_proto(vm, CP_HANDLER);
        vm->objs[px].c = 0;
        h->om.trees[te + 3] = px;
    }
    return jv_obj(h->om.trees[te + 3]);
}

jv script_cssom_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    switch (id) {
    case CO_ILLEGAL: {
        /* an interface with no constructor: called, refused; constructed, refused by its name */
        if (thisv != JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
        vector<char> nm;
        jv nv = js_get(vm, callee, A_NAME);
        if (vm->throwing || !sh_utf8(vm, nv, &nm)) return JV_HOLE;
        push_back(nm, 0);
        char msg[300];
        snprintf(msg, sizeof msg, "Failed to construct '%s': Illegal constructor", data(nm));
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    case CO_SHEET_CTOR: {
        if (thisv != JV_HOLE)
            return js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'CSSStyleSheet': Please use the 'new' operator, this DOM object constructor cannot be called as a function.");
        m->on = 1;
        uint32_t s = css_om_sheet_new(m, CSS_OM_O_CONSTRUCTED);
        m->sheets[s].parsed = 1;
        vector<char> url;
        co_doc_url(vm, &url);
        m->sheets[s].boff = css_om_put(m, data(url), len(url));
        m->sheets[s].blen = (uint32_t)len(url);
        if (jv_is_obj(a0)) {
            jv med = js_get(vm, a0, co_atom(vm, "media"));
            if (vm->throwing) return JV_HOLE;
            if (med != JV_UNDEF) {
                vector<char> t, n;
                if (jv_is_obj(med) && vm->objs[jv_idx(med)].kind == JK_HOST && vm->objs[jv_idx(med)].c == HK_MEDIA)
                    co_media_text(vm, vm->objs[jv_idx(med)].a, vm->objs[jv_idx(med)].b, &n);
                else {
                    if (!sh_utf8(vm, med, &t)) return JV_HOLE;
                    css_om_media_text(data(t), len(t), &n);
                }
                m->sheets[s].moff = css_om_put(m, data(n), len(n));
                m->sheets[s].mlen = (uint32_t)len(n);
            }
            jv dis = js_get(vm, a0, co_atom(vm, "disabled"));
            if (vm->throwing) return JV_HOLE;
            m->sheets[s].disabled = (uint32_t)js_truthy(vm, dis);
            jv base = js_get(vm, a0, co_atom(vm, "baseURL"));
            if (vm->throwing) return JV_HOLE;
            if (base != JV_UNDEF) {
                vector<char> b;
                if (!sh_utf8(vm, base, &b)) return JV_HOLE;
                push_back(b, 0);
                push_back(url, 0);
                char abs[4096];
                if (net_url_resolve(data(url), data(b), abs, sizeof abs)) {
                    m->sheets[s].boff = css_om_put(m, abs, strlen(abs));
                    m->sheets[s].blen = (uint32_t)strlen(abs);
                }
            }
        }
        return co_sheet_obj(vm, s);
    }
    /* ---- StyleSheet ---- */
    case CO_S_TYPE: case CO_S_HREF: case CO_S_OWNER: case CO_S_PARENT: case CO_S_TITLE: case CO_S_MEDIA: case CO_S_DISABLED:
    case CO_S_OWNER_RULE: case CO_S_RULES: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        if (id == CO_S_OWNER || id == CO_S_RULES || id == CO_S_MEDIA) co_check(h);
        css_om_sheet x = m->sheets[s];
        switch (id) {
        case CO_S_TYPE: return sh_cstr(vm, "text/css");
        case CO_S_HREF: return x.hlen ? co_span(vm, x.hoff, x.hlen) : JV_NULL;
        case CO_S_OWNER: return x.owner ? sh_wrap(vm, x.owner) : JV_NULL;
        case CO_S_PARENT: return x.parent ? co_sheet_obj(vm, x.parent) : JV_NULL;
        case CO_S_TITLE: {
            /* its owner's title attribute as it is now; none, or empty, null */
            vector<char> tv;
            if (x.owner) {
                if (!co_attr(h->d, x.owner, "title", &tv) || !len(tv)) return JV_NULL;
                return co_str(vm, &tv);
            }
            return (x.flags & 1) && x.tlen ? co_span(vm, x.toff, x.tlen) : JV_NULL;
        }
        case CO_S_MEDIA: return co_media_obj(vm, s, CM_SHEET);
        case CO_S_DISABLED: return jv_bool(x.disabled);
        case CO_S_OWNER_RULE: return x.import_rule ? co_rule_obj(vm, x.import_rule) : JV_NULL;
        default:
            if (!x.clean) return sh_dom_error(vm, "SecurityError", "Failed to read the 'cssRules' property from 'CSSStyleSheet': Cannot access rules");
            co_parse(h, s);
            return co_rules_obj(vm, s, 0);
        }
    }
    case CO_S_DISABLED_SET: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        uint32_t v = (uint32_t)js_truthy(vm, a0);
        if (m->sheets[s].disabled != v) { m->sheets[s].disabled = v; co_changed(h); }
        return JV_UNDEF;
    }
    case CO_S_INSERT: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        if (argc < 1) return co_args(vm, "insertRule", "CSSStyleSheet", 1, argc);
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        uint32_t index = argc >= 2 ? co_u32(vm, a1) : 0;
        if (vm->throwing) return JV_HOLE;
        return co_insert(vm, s, &t, index, "insertRule");
    }
    case CO_S_DELETE: case CO_S_REMOVE: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        if (id == CO_S_DELETE && argc < 1) return co_args(vm, "deleteRule", "CSSStyleSheet", 1, argc);
        uint32_t index = argc >= 1 ? co_u32(vm, a0) : 0;
        if (vm->throwing) return JV_HOLE;
        jv r = co_delete(vm, s, index, id == CO_S_DELETE ? "deleteRule" : "removeRule");
        return r;
    }
    case CO_S_ADD: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        vector<char> sel, style, t;
        if (argc >= 1 && a0 != JV_UNDEF) { if (!sh_utf8(vm, a0, &sel)) return JV_HOLE; }
        else co_cat(&sel, "undefined");
        if (argc >= 2 && a1 != JV_UNDEF) { if (!sh_utf8(vm, a1, &style)) return JV_HOLE; }
        else co_cat(&style, "undefined");
        append_range(t, data(sel), len(sel));
        co_cat(&t, " { ");
        append_range(t, data(style), len(style));
        if (len(style)) push_back(t, ' ');
        push_back(t, '}');
        uint32_t index;
        if (argc >= 3 && a2 != JV_UNDEF) { index = co_u32(vm, a2); if (vm->throwing) return JV_HOLE; }
        else { if (m->sheets[s].clean) co_parse(h, s); index = css_om_count(m, s, 0); }
        jv r = co_insert(vm, s, &t, index, "addRule");
        if (r == JV_HOLE) return r;
        return jv_num(-1);
    }
    case CO_S_REPLACE: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        uint32_t p = js_promise_new(vm);
        if (m->sheets[s].origin != CSS_OM_O_CONSTRUCTED) {
            sh_dom_error(vm, "NotAllowedError", "Failed to execute 'replace' on 'CSSStyleSheet': Can't call replace on non-constructed CSSStyleSheets.");
            jv ex = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, ex);
            return jv_obj(p);
        }
        if (!co_replace(vm, s, a0)) {
            jv ex = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, ex);
            return jv_obj(p);
        }
        js_promise_settle(vm, p, 0, co_sheet_obj(vm, s));
        return jv_obj(p);
    }
    case CO_S_REPLACE_SYNC: {
        uint32_t s = co_this(vm, thisv, HK_SHEET);
        if (!s) return JV_HOLE;
        if (m->sheets[s].origin != CSS_OM_O_CONSTRUCTED)
            return sh_dom_error(vm, "NotAllowedError", "Failed to execute 'replaceSync' on 'CSSStyleSheet': Can't call replaceSync on non-constructed CSSStyleSheets.");
        if (argc < 1) return co_args(vm, "replaceSync", "CSSStyleSheet", 1, argc);
        if (!co_replace(vm, s, a0)) return JV_HOLE;
        return JV_UNDEF;
    }
    /* ---- StyleSheetList, CSSRuleList, MediaList ---- */
    case CO_L_LENGTH: case CO_L_ITEM: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t o = jv_idx(thisv), kind = vm->objs[o].c, a = vm->objs[o].a, b = vm->objs[o].b;
        uint32_t i = 0;
        if (id == CO_L_ITEM) {
            if (argc < 1) return co_args(vm, "item", kind == HK_SHEETS ? "StyleSheetList" : kind == HK_MEDIA ? "MediaList" : "CSSRuleList", 1, argc);
            i = co_u32(vm, a0);
            if (vm->throwing) return JV_HOLE;
        }
        if (kind == HK_SHEETS) {
            vector<uint32_t> ss;
            co_tree_sheets(vm, a, &ss);
            if (id == CO_L_LENGTH) return jv_num((double)len(ss));
            return i < len(ss) ? co_sheet_obj(vm, ss[i]) : JV_NULL;
        }
        if (kind == HK_RULES) {
            if (!b) co_parse(h, a);
            uint32_t n = b ? css_om_count(m, 0, a) : css_om_count(m, a, 0);
            if (id == CO_L_LENGTH) return jv_num(n);
            return i < n ? co_rule_obj(vm, b ? css_om_at(m, 0, a, i) : css_om_at(m, a, 0, i)) : JV_NULL;
        }
        if (kind == HK_MEDIA) {
            vector<char> t, q;
            co_media_text(vm, a, b, &t);
            uint32_t n = css_om_media_count(data(t), len(t));
            if (id == CO_L_LENGTH) return jv_num(n);
            if (i >= n) return JV_NULL;
            css_om_media_item(data(t), len(t), i, &q);
            return co_str(vm, &q);
        }
        return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
    }
    case CO_M_TEXT: case CO_M_TOSTRING: {
        uint32_t a = co_this(vm, thisv, HK_MEDIA);
        if (!a) return JV_HOLE;
        vector<char> t;
        co_media_text(vm, a, vm->objs[jv_idx(thisv)].b, &t);
        return co_str(vm, &t);
    }
    case CO_M_TEXT_SET: {
        uint32_t a = co_this(vm, thisv, HK_MEDIA);
        if (!a) return JV_HOLE;
        vector<char> t, n;
        if (a0 != JV_NULL && !sh_utf8(vm, a0, &t)) return JV_HOLE;
        css_om_media_text(data(t), len(t), &n);
        co_media_set(vm, a, vm->objs[jv_idx(thisv)].b, &n);
        return JV_UNDEF;
    }
    case CO_M_APPEND: case CO_M_DELETE: {
        uint32_t a = co_this(vm, thisv, HK_MEDIA);
        if (!a) return JV_HOLE;
        uint32_t b = vm->objs[jv_idx(thisv)].b;
        if (argc < 1) return co_args(vm, id == CO_M_APPEND ? "appendMedium" : "deleteMedium", "MediaList", 1, argc);
        vector<char> q, t, n;
        if (!sh_utf8(vm, a0, &q)) return JV_HOLE;
        co_media_text(vm, a, b, &t);
        if (id == CO_M_APPEND) css_om_media_append(data(t), len(t), data(q), len(q), &n);
        else if (!css_om_media_delete(data(t), len(t), data(q), len(q), &n)) {
            push_back(q, 0);
            char msg[600];
            snprintf(msg, sizeof msg, "Failed to execute 'deleteMedium' on 'MediaList': Failed to delete '%s'.", data(q));
            return sh_dom_error(vm, "NotFoundError", msg);
        }
        co_media_set(vm, a, b, &n);
        return JV_UNDEF;
    }
    /* ---- CSSRule ---- */
    case CO_R_TYPE: case CO_R_TEXT: case CO_R_PARENT_RULE: case CO_R_PARENT_SHEET: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        css_om_rule x = m->rules[r];
        if (id == CO_R_TYPE) return jv_num(co_rule_type(x.kind));
        if (id == CO_R_PARENT_RULE) return x.parent ? co_rule_obj(vm, x.parent) : JV_NULL;
        if (id == CO_R_PARENT_SHEET) return x.sheet ? co_sheet_obj(vm, x.sheet) : JV_NULL;
        vector<char> t;
        css_om_rule_text(m, r, &t);
        return co_str(vm, &t);
    }
    case CO_R_TEXT_SET: return JV_UNDEF;
    case CO_R_RULES: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        return co_rules_obj(vm, r, 1);
    }
    case CO_R_INSERT: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        if (argc < 1) return co_args(vm, "insertRule", m->rules[r].kind == CSS_OM_STYLE ? "CSSStyleRule" : "CSSGroupingRule", 1, argc);
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        uint32_t index = argc >= 2 ? co_u32(vm, a1) : 0;
        if (vm->throwing) return JV_HOLE;
        return co_group_insert(vm, r, &t, index);
    }
    case CO_R_DELETE: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        if (argc < 1) return co_args(vm, "deleteRule", m->rules[r].kind == CSS_OM_STYLE ? "CSSStyleRule" : "CSSGroupingRule", 1, argc);
        uint32_t index = co_u32(vm, a0);
        if (vm->throwing) return JV_HOLE;
        return co_group_delete(vm, r, index);
    }
    case CO_R_CONDITION: case CO_R_CNAME: case CO_R_CQUERY: case CO_R_SELECTOR: case CO_R_NAME: case CO_R_HREF: case CO_R_KEYTEXT:
    case CO_R_NSURI: case CO_R_PREFIX: case CO_R_START: case CO_R_END: case CO_R_LAYER: case CO_R_SUPPORTS: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        css_om_rule x = m->rules[r];
        switch (id) {
        case CO_R_CONDITION: case CO_R_SELECTOR: case CO_R_HREF: case CO_R_KEYTEXT: case CO_R_PREFIX: case CO_R_NAME: return co_span(vm, x.aoff, x.alen);
        case CO_R_CNAME: case CO_R_NSURI: return co_span(vm, x.boff, x.blen);
        case CO_R_CQUERY: return co_span(vm, x.coff, x.clen);
        case CO_R_START: return (x.flags & 1) ? co_span(vm, x.aoff, x.alen) : JV_NULL;
        case CO_R_END: return (x.flags & 2) ? co_span(vm, x.boff, x.blen) : JV_NULL;
        case CO_R_LAYER: return (x.flags & 1) ? co_span(vm, x.coff, x.clen) : JV_NULL;
        default: return (x.flags & 2) ? co_span(vm, x.doff, x.dlen) : JV_NULL;
        }
    }
    case CO_R_SELECTOR_SET: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        if (css_om_set_selector(m, r, data(t), len(t))) co_rule_changed(h, r);
        return JV_UNDEF;
    }
    case CO_R_NAME_SET: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        if (m->rules[r].kind == CSS_OM_KEYFRAMES) { css_om_set_name(m, r, data(t), len(t)); co_rule_changed(h, r); }
        else if (m->rules[r].kind == CSS_OM_COUNTER_STYLE && css_om_set_counter(m, r, "name", data(t), len(t))) co_rule_changed(h, r);
        return JV_UNDEF;
    }
    case CO_R_KEYTEXT_SET: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        if (!css_om_set_keys(m, r, data(t), len(t))) {
            push_back(t, 0);
            char msg[600];
            snprintf(msg, sizeof msg, "Failed to set the 'keyText' property on 'CSSKeyframeRule': The key '%s' is invalid and cannot be parsed", data(t));
            return sh_dom_error(vm, "SyntaxError", msg);
        }
        co_rule_changed(h, r);
        return JV_UNDEF;
    }
    case CO_R_MEDIA: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        return co_media_obj(vm, r, m->rules[r].kind == CSS_OM_IMPORT ? CM_IMPORT_RULE : CM_MEDIA_RULE);
    }
    case CO_R_STYLE: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        return co_style_obj(vm, r);
    }
    case CO_R_NAMELIST: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        css_om_rule x = m->rules[r];
        vector<char> t;
        css_om_span(m, x.aoff, x.alen, &t);
        uint32_t arr = js_array_new(vm, 0);
        size_t i = 0;
        while (i < len(t)) {
            size_t e = i;
            while (e < len(t) && t[e] != ',') e++;
            js_array_push(vm, arr, sh_str(vm, data(t) + i, e - i));
            i = e + 2;
        }
        vm->objs[arr].flags = (uint16_t)((vm->objs[arr].flags & ~(uint32_t)JO_EXTENSIBLE) | JO_FROZEN | JO_SEALED | JO_LENGTH_RO);
        return jv_obj(arr);
    }
    case CO_R_SHEET: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        if (!m->rules[r].sub) {
            /* the imported sheet: of the rule's URL resolved against its sheet's (the page's) */
            uint32_t ps = m->rules[r].sheet;
            vector<char> base, u;
            if (ps && m->sheets[ps].hlen) css_om_span(m, m->sheets[ps].hoff, m->sheets[ps].hlen, &base);
            else co_doc_url(vm, &base);
            push_back(base, 0);
            css_om_span(m, m->rules[r].aoff, m->rules[r].alen, &u);
            push_back(u, 0);
            char abs[4096];
            if (!net_url_resolve(data(base), data(u), abs, sizeof abs)) snprintf(abs, sizeof abs, "%s", data(u));
            uint32_t s = css_om_sheet_new(m, CSS_OM_O_IMPORT);
            m->sheets[s].parent = ps;
            m->sheets[s].import_rule = r;
            m->sheets[s].hoff = css_om_put(m, abs, strlen(abs));
            m->sheets[s].hlen = (uint32_t)strlen(abs);
            vector<char> doc;
            co_doc_url(vm, &doc);
            push_back(doc, 0);
            m->sheets[s].clean = co_same_origin(data(doc), abs);
            uint32_t res = co_url_resource(h->nt, abs);
            if (res && h->nt->resource[res].outcome == NET_OUTCOME_OK) {
                amb_span b = h->nt->resource[res].body;
                m->sheets[s].soff = css_om_put(m, bytes_ptr(h->nt, b), b.len);
                m->sheets[s].slen = b.len;
            }
            /* its media list its own, empty (the rule's media are the rule's) */
            m->rules[r].sub = s;
        }
        return co_sheet_obj(vm, m->rules[r].sub);
    }
    case CO_R_LENGTH: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        return jv_num(css_om_count(m, 0, r));
    }
    case CO_R_APPEND: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        if (argc < 1) return co_args(vm, "appendRule", "CSSKeyframesRule", 1, argc);
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        uint32_t made;
        if (!css_om_insert(m, m->rules[r].sheet, r, 0, data(t), len(t), 0, &made)) co_rule_changed(h, r);
        return JV_UNDEF;
    }
    case CO_R_KDELETE: case CO_R_FIND: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        if (argc < 1) return co_args(vm, id == CO_R_FIND ? "findRule" : "deleteRule", "CSSKeyframesRule", 1, argc);
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        uint32_t k = css_om_find_key(m, r, data(t), len(t));
        if (id == CO_R_FIND) return k ? co_rule_obj(vm, k) : JV_NULL;
        if (k) {
            uint32_t i = 0;
            for (uint32_t c = m->rules[r].first; c && c != k; c = m->rules[c].next) i++;
            css_om_remove(m, 0, r, i);
            co_rule_changed(h, r);
        }
        return JV_UNDEF;
    }
    case CO_R_FAMILY: case CO_R_BASE_PALETTE: case CO_R_OVERRIDE: case CO_R_NAVIGATION: case CO_R_TYPES: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> v;
        if (id == CO_R_FAMILY && m->rules[r].kind == CSS_OM_FONT_FEATURE_VALUES) return co_span(vm, m->rules[r].aoff, m->rules[r].alen);
        uint32_t bk = id == CO_R_NAVIGATION || id == CO_R_TYPES ? CSS_OM_B_VIEW : CSS_OM_B_PALETTE;
        const char *dn = id == CO_R_FAMILY ? "font-family" : id == CO_R_BASE_PALETTE ? "base-palette" : id == CO_R_OVERRIDE ? "override-colors"
                       : id == CO_R_NAVIGATION ? "navigation" : "types";
        co_desc(vm, r, bk, dn, &v);
        if (id != CO_R_TYPES) return co_str(vm, &v);
        /* the types, a frozen array of their names */
        uint32_t arr = js_array_new(vm, 0);
        size_t i = 0;
        while (i < len(v)) {
            while (i < len(v) && v[i] == ' ') i++;
            size_t e = i;
            while (e < len(v) && v[e] != ' ') e++;
            if (e > i) js_array_push(vm, arr, sh_str(vm, data(v) + i, e - i));
            i = e;
        }
        vm->objs[arr].flags = (uint16_t)((vm->objs[arr].flags & ~(uint32_t)JO_EXTENSIBLE) | JO_FROZEN | JO_SEALED | JO_LENGTH_RO);
        return jv_obj(arr);
    }
    case CO_R_SYNTAX: case CO_R_INHERITS: case CO_R_INITIAL: {
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> v;
        int has = co_desc(vm, r, CSS_OM_B_PROPERTY, id == CO_R_SYNTAX ? "syntax" : id == CO_R_INHERITS ? "inherits" : "initial-value", &v);
        if (id == CO_R_INHERITS) return jv_bool(has && len(v) == 4 && !memcmp(data(v), "true", 4));
        if (id == CO_R_INITIAL) return has ? co_str(vm, &v) : JV_NULL;
        /* the syntax's string, its quotes off */
        if (len(v) >= 2 && v[0] == '"') return sh_str(vm, data(v) + 1, len(v) - 2);
        return co_str(vm, &v);
    }
    case CO_R_COUNTER: case CO_R_COUNTER_SET: {
        /* a counter style's descriptor, named by the accessor's own name ("get symbols",
           "set symbols") */
        uint32_t r = co_this(vm, thisv, HK_RULE);
        if (!r) return JV_HOLE;
        vector<char> nm;
        jv nmv = js_get(vm, callee, A_NAME);
        if (!sh_utf8(vm, nmv, &nm)) return JV_HOLE;
        static const char *const js[] = { "system", "symbols", "additiveSymbols", "negative", "prefix", "suffix", "range", "pad", "speakAs", "fallback", 0 };
        static const char *const css[] = { "system", "symbols", "additive-symbols", "negative", "prefix", "suffix", "range", "pad", "speak-as", "fallback" };
        size_t sp = 0;
        while (sp < len(nm) && nm[sp] != ' ') sp++;
        const char *want = sp < len(nm) ? data(nm) + sp + 1 : data(nm);
        size_t wn = sp < len(nm) ? len(nm) - sp - 1 : len(nm);
        int k = -1;
        for (int q = 0; js[q]; q++) if (strlen(js[q]) == wn && !memcmp(js[q], want, wn)) k = q;
        if (k < 0) return JV_UNDEF;
        if (id == CO_R_COUNTER) {
            vector<char> v;
            co_desc(vm, r, CSS_OM_B_COUNTER, css[k], &v);
            return co_str(vm, &v);
        }
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        if (css_om_set_counter(m, r, css[k], data(t), len(t))) co_rule_changed(h, r);
        return JV_UNDEF;
    }
    /* ---- CSSStyleDeclaration's own ---- */
    case CO_D_PRIORITY: case CO_D_ITEM: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t o = jv_idx(thisv), kind = vm->objs[o].c;
        if (argc < 1) return co_args(vm, id == CO_D_ITEM ? "item" : "getPropertyPriority", "CSSStyleDeclaration", 1, argc);
        if (kind == HK_COMPUTED) {
            if (id == CO_D_PRIORITY) return jv_str(A_EMPTY);
            uint32_t i = co_u32(vm, a0);
            if (vm->throwing) return JV_HOLE;
            char key[16];
            snprintf(key, sizeof key, "%u", i);
            jv v = js_get(vm, thisv, co_atom(vm, key));
            return v == JV_UNDEF ? jv_str(A_EMPTY) : v;
        }
        if (kind != HK_STYLE && kind != HK_DECL) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        return script_cssom_style(vm, id == CO_D_ITEM ? 4 : 3, thisv, args, argc);
    }
    /* ---- the trees' lists and adoptedStyleSheets ---- */
    case CO_T_SHEETS: {
        uint32_t root = co_tree_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (!root) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        size_t te = co_tree_entry(vm, root);
        if (!m->trees[te + 2]) {
            jv o = co_host_obj(vm, HK_SHEETS, co_proto(vm, CP_SHEETLIST), root, h->d->node[root].gen);
            m->trees[te + 2] = jv_idx(o);
        }
        return jv_obj(m->trees[te + 2]);
    }
    case CO_T_ADOPTED: {
        uint32_t root = co_tree_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (!root) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        return co_adopted(vm, root);
    }
    case CO_T_ADOPTED_SET: {
        uint32_t root = co_tree_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (!root) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        const char *pre = root == 1 ? "Failed to set the 'adoptedStyleSheets' property on 'Document': " : "Failed to set the 'adoptedStyleSheets' property on 'ShadowRoot': ";
        /* a sequence: any iterable object */
        if (!jv_is_obj(a0)) {
            char msg[300];
            snprintf(msg, sizeof msg, "%sThe provided value cannot be converted to a sequence.", pre);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        jv itf = js_get(vm, a0, JS_KEY_ITERATOR);
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, itf)) {
            char msg[300];
            snprintf(msg, sizeof msg, "%sThe object must have a callable @@iterator property.", pre);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        /* the list and the iterator held among the roots while the page's iterator runs */
        uint32_t list = js_array_new(vm, 0);
        size_t mark = len(m->aux);
        push_back(m->aux, list);
        int ok = co_adopt_list(vm, a0, pre, list);
        resize(h->om.aux, mark);
        if (!ok) return JV_HOLE;
        co_adopted(vm, root);
        size_t te = co_tree_entry(vm, root);
        uint32_t arr = h->om.trees[te + 4];
        js_array_set_length(vm, arr, 0);
        uint32_t n = vm->objs[list].b;
        for (uint32_t i = 0; i < n; i++) js_array_push(vm, arr, js_array_get(vm, list, i));
        co_adopted_mirror(vm, te);
        return JV_UNDEF;
    }
    case CO_A_SET: case CO_A_DELETE: case CO_A_DEFINE: {
        /* the observable array's traps: target, key, value (set) or descriptor (define) */
        jv target = a0, key = a1;
        if (!jv_is_obj(target)) return JV_FALSE;
        uint32_t arr = jv_idx(target);
        size_t te = len(m->trees);
        for (size_t i = 0; i + 4 < len(m->trees); i += 5) if (m->trees[i + 4] == arr) te = i;
        if (te >= len(m->trees)) return JV_FALSE;
        int ok;
        uint32_t k = js_prop_key(vm, key, &ok);
        if (!ok) return JV_HOLE;
        uint32_t idx;
        int is_index = !js_key_is_sym(k) && js_atom_index(vm, k, &idx);
        uint32_t n = vm->objs[arr].b;
        if (id == CO_A_DELETE) {
            if (!is_index) return jv_bool(js_delete(vm, arr, k));
            if (idx + 1 != n) return JV_FALSE;
            js_array_set_length(vm, arr, n - 1);
            co_adopted_mirror(vm, te);
            return JV_TRUE;
        }
        jv val = a2;
        if (id == CO_A_DEFINE) {
            if (!jv_is_obj(a2)) return JV_FALSE;
            val = js_get(vm, a2, A_VALUE);
            if (vm->throwing) return JV_HOLE;
        }
        if (!js_key_is_sym(k) && k == A_LENGTH) {
            /* WebIDL's set the length: a length past the list's refused */
            double num = js_to_number(vm, val);
            if (vm->throwing) return JV_HOLE;
            uint32_t nl = co_u32(vm, val);
            if (vm->throwing) return JV_HOLE;
            if ((double)nl != num) return js_throw_msg(vm, vm->range_error_proto, "Invalid array length");
            if (nl > n) return JV_FALSE;
            js_array_set_length(vm, arr, nl);
            co_adopted_mirror(vm, te);
            return JV_TRUE;
        }
        if (!is_index) {
            js_define(vm, arr, k, val, 0);
            return JV_TRUE;
        }
        /* WebIDL's set the indexed value: past the list's end refused, then the value converted */
        if (idx > n) return JV_FALSE;
        if (co_adoptable(vm, val, "")) return JV_HOLE;
        n = vm->objs[arr].b;
        if (idx > n) return JV_FALSE;
        if (idx == n) js_array_push(vm, arr, val);
        else js_array_set(vm, arr, idx, val);
        co_adopted_mirror(vm, te);
        return JV_TRUE;
    }
    case CO_E_SHEET: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        const dom_document *d = h->d;
        if (!co_named(d, x, "style", DOM_NAMESPACE_HTML) && !co_named(d, x, "style", DOM_NAMESPACE_SVG) && !co_named(d, x, "link", DOM_NAMESPACE_HTML))
            return JV_UNDEF;
        uint32_t s = co_owned(vm, x);
        return s ? co_sheet_obj(vm, s) : JV_NULL;
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* a text's FNV-1a hash: an inline style's attribute text known again without keeping it */
static uint64_t co_fnv(const char *s, size_t n) {
    uint64_t hv = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        hv ^= (unsigned char)s[i];
        hv *= 1099511628211ull;
    }
    return hv;
}
/* element x's inline style record: its place in the model's inl plus one, made when make is
   set and it has none (0 then for none); a record of a node an earlier element had let go */
static uint32_t co_inline(js_vm *vm, uint32_t x, int make) {
    css_om *m = &vm->host->om;
    const dom_document *d = vm->host->d;
    if (!x || x >= d->node_len) return 0;
    if (x < len(m->inl_at) && m->inl_at[x]) {
        uint32_t i = m->inl_at[x] - 1;
        if (i < len(m->inl) && m->inl[i].node == x && m->inl[i].gen == d->node[x].gen) return i + 1;
        if (i < len(m->inl) && m->inl[i].node == x) {
            m->inl[i].node = 0;
            m->inl[i].obj = 0;
            m->inl[i].block = 0;
        }
        m->inl_at[x] = 0;
    }
    if (!make) return 0;
    if (len(m->inl_at) < d->node_len) resize(m->inl_at, d->node_len, 0u);
    css_om_inline e;
    memset(&e, 0, sizeof e);
    e.node = x;
    e.gen = d->node[x].gen;
    push_back(m->inl, e);
    m->inl_at[x] = (uint32_t)len(m->inl);
    return (uint32_t)len(m->inl);
}
/* element x's style: its CSSStyleDeclaration, the same one each read ([SameObject]) */
jv script_cssom_inline_style(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    uint32_t k = co_inline(vm, x, 1);
    if (!k) return JV_UNDEF;
    if (!h->om.inl[k - 1].obj) {
        jv o = co_host_obj(vm, HK_STYLE, h->protos[HP_STYLE], x, h->d->node[x].gen);
        h->om.inl[k - 1].obj = jv_idx(o);
    }
    return jv_obj(h->om.inl[k - 1].obj);
}

/* ---- CSSStyleDeclaration: a rule's style and an element's ---- */

/* a style's declarations into dl: a rule's block; an element's inline style, its declarations
   as they were made while its style attribute is the text they were last read from or written
   as, else that attribute read anew (Chromium's inline style keeps its declarations' order, an
   important one where it was set; a text read files the important ones last) */
static int co_decl_load(js_vm *vm, uint32_t o, css_om_dl *dl, uint32_t *kind) {
    struct js_host *h = vm->host;
    uint32_t c = vm->objs[o].c;
    if (c == HK_DECL) {
        uint32_t b = vm->objs[o].a;
        css_om_block_load(&h->om, b, dl);
        *kind = h->om.blocks[b].kind;
        return 1;
    }
    const dom_document *d = h->d;
    uint32_t x = vm->objs[o].a;
    if (x >= d->node_len || !d->node[x].live || d->node[x].gen != vm->objs[o].b) {
        js_throw_msg(vm, vm->type_error_proto, "the node is gone: the page destroyed it (a stale handle)");
        return 0;
    }
    *kind = CSS_OM_B_STYLE;
    /* the attribute's text known again by its hash, read from the DOM's own text */
    uint32_t sa = sh_attr_z(d, x, "style");
    size_t tn = sa ? d->attr[sa].value.len : 0;
    uint64_t sig = co_fnv(sa ? text_ptr(d, d->attr[sa].value) : "", tn);
    uint32_t k = co_inline(vm, x, 1);
    css_om *m = &h->om;
    css_om_inline e = m->inl[k - 1];
    if (e.block && e.tlen == tn && e.sig == sig) {
        css_om_block_load(m, e.block, dl);
        return 1;
    }
    vector<char> v;
    co_attr(d, x, "style", &v);
    css_om_parse_decls(dl, data(v), len(v), CSS_OM_B_STYLE);
    if (e.block) css_om_block_store(m, e.block, dl);
    else {
        uint32_t b = css_om_block_new(m, CSS_OM_B_STYLE, 0, dl);
        m->inl[k - 1].block = b;
    }
    m->inl[k - 1].tlen = (uint32_t)len(v);
    m->inl[k - 1].sig = sig;
    return 1;
}
/* a style's declarations written back: a rule's block (its sheet changed); an element's kept as
   its inline style's and its style attribute set to their text, as Chromium writes it */
static void co_decl_store(js_vm *vm, uint32_t o, const css_om_dl *dl) {
    struct js_host *h = vm->host;
    if (vm->objs[o].c == HK_DECL) {
        uint32_t b = vm->objs[o].a;
        css_om_block_store(&h->om, b, dl);
        co_rule_changed(h, h->om.blocks[b].rule);
        return;
    }
    vector<char> t;
    css_om_decls_text(dl, CSS_OM_B_STYLE, &t);
    dom_document *d = h->d;
    uint32_t x = vm->objs[o].a;
    uint32_t k = co_inline(vm, x, 1);
    css_om *m = &h->om;
    if (k) {
        if (m->inl[k - 1].block) css_om_block_store(m, m->inl[k - 1].block, dl);
        else {
            uint32_t b = css_om_block_new(m, CSS_OM_B_STYLE, 0, dl);
            m->inl[k - 1].block = b;
        }
        m->inl[k - 1].tlen = (uint32_t)len(t);
        m->inl[k - 1].sig = co_fnv(data(t), len(t));
    }
    dom_set_attribute(d, node_handle(d, x), "style", 5, data(t), len(t));
}
/* a style's name as CSS writes it: camelCase to kebab-case, webkitX -webkit-x, cssFloat float */
static void co_css_name(const vector<char> *js, vector<char> *css) {
    clear(*css);
    if (len(*js) == 8 && memcmp(data(*js), "cssFloat", 8) == 0) { append_range(*css, "float", 5); return; }
    size_t i = 0;
    if (len(*js) > 6 && memcmp(data(*js), "webkit", 6) == 0 && (*js)[6] >= 'A' && (*js)[6] <= 'Z') push_back(*css, '-');
    for (; i < len(*js); i++) {
        char c = (*js)[i];
        if (c >= 'A' && c <= 'Z') { push_back(*css, '-'); push_back(*css, (char)(c + 32)); }
        else push_back(*css, c);
    }
}

/* setProperty (0, 1 there: which 1), getPropertyValue (0), removeProperty (2), getPropertyPriority
   (3), item (4) of a rule's style or an element's */
jv script_cssom_style(js_vm *vm, int which, jv thisv, uint32_t args, uint32_t argc) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST || (vm->objs[jv_idx(thisv)].c != HK_STYLE && vm->objs[jv_idx(thisv)].c != HK_DECL))
        return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
    static const char *const names[] = { "getPropertyValue", "setProperty", "removeProperty", "getPropertyPriority", "item" };
    uint32_t need = which == 1 ? 2 : 1;
    if (argc < need) return co_args(vm, names[which], "CSSStyleDeclaration", need, argc);
    uint32_t o = jv_idx(thisv);
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    /* the arguments converted first, in their order (a conversion may run the page's code), then
       the declarations read */
    uint32_t i = 0;
    vector<char> name, v, pri;
    if (which == 4) {
        i = co_u32(vm, a0);
        if (vm->throwing) return JV_HOLE;
    } else if (!sh_utf8(vm, a0, &name)) return JV_HOLE;
    if (which == 1) {
        /* setProperty(name, value, priority): a null value and priority empty */
        if (a1 != JV_NULL && !sh_utf8(vm, a1, &v)) return JV_HOLE;
        if (argc >= 3 && a2 != JV_UNDEF && a2 != JV_NULL && !sh_utf8(vm, a2, &pri)) return JV_HOLE;
    }
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    defer(dl) { CSS_OM_DL_DROP(dl); }
    uint32_t kind;
    if (!co_decl_load(vm, o, &dl, &kind)) return JV_HOLE;
    if (which == 4) {
        vector<char> nm;
        if (i < css_om_decl_count(&dl)) css_om_decl_name(&dl, i, &nm);
        return co_str(vm, &nm);
    }
    if (which == 0) { css_om_get(&dl, kind, data(name), len(name), &v); return co_str(vm, &v); }
    if (which == 3) return css_om_important(&dl, kind, data(name), len(name)) ? sh_cstr(vm, "important") : jv_str(A_EMPTY);
    if (which == 2) {
        if (!css_om_unset(&dl, kind, data(name), len(name), &v)) return co_str(vm, &v);
        co_decl_store(vm, o, &dl);
        return co_str(vm, &v);
    }
    /* a priority but important (ASCII case-insensitively) refused */
    int important = len(pri) == 9 && sh_ci_eq(data(pri), "important", 9);
    if (len(pri) && !important) return JV_UNDEF;
    if (css_om_set(&dl, kind, data(name), len(name), data(v), len(v), important)) co_decl_store(vm, o, &dl);
    return JV_UNDEF;
}

/* ---- named and indexed properties ---- */

jv script_cssom_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled) {
    *handled = 0;
    struct js_host *h = vm->host;
    uint32_t kind = vm->objs[o].c, idx;
    if (js_key_is_sym(atom)) return JV_UNDEF;
    int is_index = js_atom_index(vm, atom, &idx);
    if (kind == HK_STYLE || kind == HK_DECL) {
        jv ownv;
        uint32_t f;
        if (js_get_own(vm, o, atom, &ownv, &f)) return JV_UNDEF;
        if (!is_index && js_has(vm, h->protos[HP_STYLE], atom)) return JV_UNDEF;
        /* what is read: 1 an index's name, 2 cssText, 3 length, 4 parentRule, 5 a property's
           value (by its name camel-cased, or as CSS writes it); else nothing of the style's */
        vector<char> name, prop, v;
        int what = 1;
        if (!is_index) {
            js_str_to_utf8(vm, atom, &name);
            if (len(name) == 7 && !memcmp(data(name), "cssText", 7)) what = 2;
            else if (len(name) == 6 && !memcmp(data(name), "length", 6)) what = 3;
            else if (len(name) == 10 && !memcmp(data(name), "parentRule", 10)) what = 4;
            else {
                co_css_name(&name, &prop);
                int p = css_om_prop(data(prop), len(prop));
                if (p < 0 || p == CSS_OM_CUSTOM) return JV_UNDEF;
                what = 5;
            }
        }
        *handled = 1;
        if (what == 4) return kind == HK_STYLE ? JV_NULL : co_rule_obj(vm, h->om.blocks[vm->objs[o].a].rule);
        css_om_dl dl;
        memset(&dl, 0, sizeof dl);
        defer(dl) { CSS_OM_DL_DROP(dl); }
        uint32_t bk;
        if (!co_decl_load(vm, o, &dl, &bk)) return JV_HOLE;
        if (what == 1) {
            if (idx >= css_om_decl_count(&dl)) return JV_UNDEF;
            css_om_decl_name(&dl, idx, &v);
            return co_str(vm, &v);
        }
        if (what == 2) { css_om_decls_text(&dl, bk, &v); return co_str(vm, &v); }
        if (what == 3) return jv_num(css_om_decl_count(&dl));
        css_om_get(&dl, bk, data(prop), len(prop), &v);
        return co_str(vm, &v);
    }
    if (!is_index) return JV_UNDEF;
    if (kind == HK_SHEETS || kind == HK_RULES || kind == HK_MEDIA) {
        *handled = 1;
        /* the list's item(i), undefined past its end */
        css_om *m = &h->om;
        uint32_t a = vm->objs[o].a, b = vm->objs[o].b;
        if (kind == HK_SHEETS) {
            vector<uint32_t> ss;
            co_tree_sheets(vm, a, &ss);
            return idx < len(ss) ? co_sheet_obj(vm, ss[idx]) : JV_UNDEF;
        }
        if (kind == HK_RULES) {
            if (!b) co_parse(h, a);
            uint32_t n = b ? css_om_count(m, 0, a) : css_om_count(m, a, 0);
            return idx < n ? co_rule_obj(vm, b ? css_om_at(m, 0, a, idx) : css_om_at(m, a, 0, idx)) : JV_UNDEF;
        }
        vector<char> t, q;
        co_media_text(vm, a, b, &t);
        if (idx >= css_om_media_count(data(t), len(t))) return JV_UNDEF;
        css_om_media_item(data(t), len(t), idx, &q);
        return co_str(vm, &q);
    }
    if (kind == HK_RULE && h->om.rules[vm->objs[o].a].kind == CSS_OM_KEYFRAMES) {
        *handled = 1;
        uint32_t r = vm->objs[o].a;
        uint32_t c = css_om_at(&h->om, 0, r, idx);
        return c ? co_rule_obj(vm, c) : JV_UNDEF;
    }
    return JV_UNDEF;
}

int script_cssom_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled) {
    *handled = 0;
    uint32_t kind = vm->objs[o].c, idx;
    if (js_key_is_sym(atom) || (kind != HK_STYLE && kind != HK_DECL)) return 1;
    if (js_atom_index(vm, atom, &idx)) return 1;
    jv ownv;
    uint32_t f;
    if (js_get_own(vm, o, atom, &ownv, &f)) return 1;
    if (js_has(vm, vm->host->protos[HP_STYLE], atom)) return 1;
    vector<char> name, prop, t;
    js_str_to_utf8(vm, atom, &name);
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    defer(dl) { CSS_OM_DL_DROP(dl); }
    uint32_t bk;
    if (len(name) == 7 && !memcmp(data(name), "cssText", 7)) {
        *handled = 1;
        if (v != JV_NULL && !sh_utf8(vm, v, &t)) return 0;
        if (!co_decl_load(vm, o, &dl, &bk)) return 0;
        css_om_parse_decls(&dl, data(t), len(t), bk);
        co_decl_store(vm, o, &dl);
        return 1;
    }
    co_css_name(&name, &prop);
    int p = css_om_prop(data(prop), len(prop));
    if (p < 0 || p == CSS_OM_CUSTOM) return 1;
    *handled = 1;
    if (v != JV_NULL && !sh_utf8(vm, v, &t)) return 0;
    if (!co_decl_load(vm, o, &dl, &bk)) return 0;
    if (css_om_set(&dl, bk, data(prop), len(prop), data(t), len(t), 0)) co_decl_store(vm, o, &dl);
    return 1;
}

/* ---- the cascade told ---- */

void script_cssom_sync(struct js_host *h) {
    css_om *m = &h->om;
    if (!m->on) return;
    co_check(h);
    if (m->told == m->rev) return;
    m->told = m->rev;
    const dom_document *d = h->d;
    css_om_reg_clear(d);
    for (size_t s = 1; s < len(m->sheets); s++) {
        css_om_sheet x = m->sheets[s];
        if (!x.live || !x.owner || !(x.changed || x.disabled || x.media_set)) continue;
        uint32_t flags = (x.disabled ? CSS_OMR_DISABLED : 0) | (x.media_set ? CSS_OMR_MEDIA : 0) | (x.changed ? CSS_OMR_TEXT : 0);
        vector<char> med, t;
        css_om_span(m, x.moff, x.mlen, &med);
        if (x.changed) css_om_sheet_text(m, (uint32_t)s, &t);
        css_om_reg_owned(d, x.owner, x.ogen, flags, data(med), len(med), data(t), len(t));
    }
    for (size_t i = 0; i + 2 < len(m->adopted); i += 3) {
        uint32_t root = m->adopted[i], gen = m->adopted[i + 1], s = m->adopted[i + 2];
        if (root >= d->node_len || !d->node[root].live || d->node[root].gen != gen || s >= len(m->sheets)) continue;
        css_om_sheet x = m->sheets[s];
        vector<char> med, base, t;
        css_om_span(m, x.moff, x.mlen, &med);
        css_om_span(m, x.boff, x.blen, &base);
        css_om_sheet_text(m, s, &t);
        uint32_t flags = (x.disabled ? CSS_OMR_DISABLED : 0) | CSS_OMR_MEDIA | CSS_OMR_TEXT;
        css_om_reg_adopted(d, root, flags, data(med), len(med), data(base), len(base), data(t), len(t));
    }
}

/* ---- the collector's roots ---- */

void script_cssom_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    for (size_t i = 0; i < len(m->protos); i++) if (m->protos[i]) push_back(*out, jv_obj(m->protos[i]));
    for (size_t i = 1; i < len(m->sheets); i++) {
        css_om_sheet s = m->sheets[i];
        if (s.obj) push_back(*out, jv_obj(s.obj));
        if (s.rules_obj) push_back(*out, jv_obj(s.rules_obj));
        if (s.media_obj) push_back(*out, jv_obj(s.media_obj));
    }
    for (size_t i = 1; i < len(m->rules); i++) {
        css_om_rule r = m->rules[i];
        if (r.obj) push_back(*out, jv_obj(r.obj));
        if (r.rules_obj) push_back(*out, jv_obj(r.rules_obj));
        if (r.media_obj) push_back(*out, jv_obj(r.media_obj));
        if (r.style_obj) push_back(*out, jv_obj(r.style_obj));
    }
    for (size_t i = 0; i + 4 < len(m->trees); i += 5)
        for (int k = 2; k < 5; k++) if (m->trees[i + (size_t)k]) push_back(*out, jv_obj(m->trees[i + (size_t)k]));
    /* what a native holds while the page's code runs (an adopted list being read) */
    for (size_t i = 0; i < len(m->aux); i++) if (m->aux[i]) push_back(*out, jv_obj(m->aux[i]));
    /* each living element's inline style; a destroyed element's record let go */
    const dom_document *d = h->d;
    for (size_t i = 0; i < len(m->inl); i++) {
        css_om_inline e = m->inl[i];
        if (!e.node) continue;
        if (e.node < d->node_len && d->node[e.node].live && d->node[e.node].gen == e.gen) {
            if (e.obj) push_back(*out, jv_obj(e.obj));
            continue;
        }
        m->inl[i].node = 0;
        m->inl[i].obj = 0;
        m->inl[i].block = 0;
        if (e.node < len(m->inl_at) && m->inl_at[e.node] == i + 1) m->inl_at[e.node] = 0;
    }
}

/* ---- the classes ---- */

static void co_get(js_vm *vm, uint32_t proto, const char *name, uint32_t id) {
    uint32_t a = co_atom(vm, name);
    js_define_accessor(vm, proto, a, jv_obj(js_native_new(vm, id, a, 0)), JV_UNDEF, 0);
}
static void co_getset(js_vm *vm, uint32_t proto, const char *name, uint32_t gid, uint32_t sid) {
    uint32_t a = co_atom(vm, name);
    js_define_accessor(vm, proto, a, jv_obj(js_native_new(vm, gid, a, 0)), jv_obj(js_native_new(vm, sid, a, 1)), 0);
}
static uint32_t co_class(js_vm *vm, uint32_t cp, const char *name, uint32_t parent, uint32_t ctor) {
    css_om *m = &vm->host->om;
    uint32_t p = js_obj_new(vm, JK_PLAIN, parent);
    m->protos[cp] = p;
    uint32_t f = sh_interface(vm, name, ctor, p);
    js_define(vm, p, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, name), JS_SHAPE_HIDDEN);
    /* the constructor's prototype the parent interface's (CSSStyleRule's [[Prototype]] is CSSRule) */
    if (parent != vm->object_proto) {
        jv pc = js_get(vm, jv_obj(parent), A_CONSTRUCTOR);
        if (jv_is_obj(pc)) js_set_proto(vm, f, jv_idx(pc));
    }
    return p;
}

void script_cssom_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    css_om *m = &h->om;
    resize(m->protos, CP_N, 0u);
    uint32_t sheet = co_class(vm, CP_STYLESHEET, "StyleSheet", vm->object_proto, CO_ILLEGAL);
    co_get(vm, sheet, "type", CO_S_TYPE);
    co_get(vm, sheet, "href", CO_S_HREF);
    co_get(vm, sheet, "ownerNode", CO_S_OWNER);
    co_get(vm, sheet, "parentStyleSheet", CO_S_PARENT);
    co_get(vm, sheet, "title", CO_S_TITLE);
    co_get(vm, sheet, "media", CO_S_MEDIA);
    co_getset(vm, sheet, "disabled", CO_S_DISABLED, CO_S_DISABLED_SET);
    uint32_t css = co_class(vm, CP_CSSSTYLESHEET, "CSSStyleSheet", sheet, CO_SHEET_CTOR);
    co_get(vm, css, "ownerRule", CO_S_OWNER_RULE);
    co_get(vm, css, "cssRules", CO_S_RULES);
    co_get(vm, css, "rules", CO_S_RULES);
    sh_method(vm, css, "insertRule", CO_S_INSERT, 1);
    sh_method(vm, css, "deleteRule", CO_S_DELETE, 1);
    sh_method(vm, css, "addRule", CO_S_ADD, 0);
    sh_method(vm, css, "removeRule", CO_S_REMOVE, 0);
    sh_method(vm, css, "replace", CO_S_REPLACE, 1);
    sh_method(vm, css, "replaceSync", CO_S_REPLACE_SYNC, 1);
    jv values = js_get(vm, jv_obj(vm->array_proto), co_atom(vm, "values"));
    static const char *const lists[] = { "StyleSheetList", "CSSRuleList", "MediaList" };
    static const uint32_t lcp[] = { CP_SHEETLIST, CP_RULELIST, CP_MEDIALIST };
    for (int k = 0; k < 3; k++) {
        uint32_t p = co_class(vm, lcp[k], lists[k], vm->object_proto, CO_ILLEGAL);
        co_get(vm, p, "length", CO_L_LENGTH);
        sh_method(vm, p, "item", CO_L_ITEM, 1);
        js_define(vm, p, JS_KEY_ITERATOR, values, JS_SHAPE_HIDDEN);
    }
    uint32_t ml = m->protos[CP_MEDIALIST];
    co_getset(vm, ml, "mediaText", CO_M_TEXT, CO_M_TEXT_SET);
    sh_method(vm, ml, "appendMedium", CO_M_APPEND, 1);
    sh_method(vm, ml, "deleteMedium", CO_M_DELETE, 1);
    sh_method(vm, ml, "toString", CO_M_TOSTRING, 0);
    /* CSSRule and its constants */
    uint32_t rule = co_class(vm, CP_RULE, "CSSRule", vm->object_proto, CO_ILLEGAL);
    co_get(vm, rule, "type", CO_R_TYPE);
    co_getset(vm, rule, "cssText", CO_R_TEXT, CO_R_TEXT_SET);
    co_get(vm, rule, "parentRule", CO_R_PARENT_RULE);
    co_get(vm, rule, "parentStyleSheet", CO_R_PARENT_SHEET);
    static const char *const consts[] = { "STYLE_RULE", "CHARSET_RULE", "IMPORT_RULE", "MEDIA_RULE", "FONT_FACE_RULE", "PAGE_RULE", "KEYFRAMES_RULE",
                                          "KEYFRAME_RULE", "MARGIN_RULE", "NAMESPACE_RULE", "COUNTER_STYLE_RULE", "SUPPORTS_RULE", 0 };
    jv rule_ctor = js_get(vm, jv_obj(rule), A_CONSTRUCTOR);
    for (int k = 0; consts[k]; k++) {
        uint32_t a = co_atom(vm, consts[k]);
        js_define(vm, rule, a, jv_num(k + 1), JS_SHAPE_PROTO);
        if (jv_is_obj(rule_ctor)) js_define(vm, jv_idx(rule_ctor), a, jv_num(k + 1), JS_SHAPE_PROTO);
    }
    {
        uint32_t a = co_atom(vm, "FONT_FEATURE_VALUES_RULE");
        js_define(vm, rule, a, jv_num(14), JS_SHAPE_PROTO);
        if (jv_is_obj(rule_ctor)) js_define(vm, jv_idx(rule_ctor), a, jv_num(14), JS_SHAPE_PROTO);
    }
    uint32_t group = co_class(vm, CP_GROUPING, "CSSGroupingRule", rule, CO_ILLEGAL);
    co_get(vm, group, "cssRules", CO_R_RULES);
    sh_method(vm, group, "insertRule", CO_R_INSERT, 1);
    sh_method(vm, group, "deleteRule", CO_R_DELETE, 1);
    uint32_t cond = co_class(vm, CP_CONDITION, "CSSConditionRule", group, CO_ILLEGAL);
    co_get(vm, cond, "conditionText", CO_R_CONDITION);
    uint32_t media = co_class(vm, CP_MEDIA, "CSSMediaRule", cond, CO_ILLEGAL);
    co_get(vm, media, "media", CO_R_MEDIA);
    co_class(vm, CP_SUPPORTS, "CSSSupportsRule", cond, CO_ILLEGAL);
    uint32_t cont = co_class(vm, CP_CONTAINER, "CSSContainerRule", cond, CO_ILLEGAL);
    co_get(vm, cont, "containerName", CO_R_CNAME);
    co_get(vm, cont, "containerQuery", CO_R_CQUERY);
    uint32_t style = co_class(vm, CP_STYLE, "CSSStyleRule", rule, CO_ILLEGAL);
    co_getset(vm, style, "selectorText", CO_R_SELECTOR, CO_R_SELECTOR_SET);
    co_get(vm, style, "style", CO_R_STYLE);
    co_get(vm, style, "cssRules", CO_R_RULES);
    sh_method(vm, style, "insertRule", CO_R_INSERT, 1);
    sh_method(vm, style, "deleteRule", CO_R_DELETE, 1);
    uint32_t lb = co_class(vm, CP_LAYER_BLOCK, "CSSLayerBlockRule", group, CO_ILLEGAL);
    co_get(vm, lb, "name", CO_R_NAME);
    uint32_t ls = co_class(vm, CP_LAYER_STATEMENT, "CSSLayerStatementRule", rule, CO_ILLEGAL);
    co_get(vm, ls, "nameList", CO_R_NAMELIST);
    uint32_t imp = co_class(vm, CP_IMPORT, "CSSImportRule", rule, CO_ILLEGAL);
    co_get(vm, imp, "href", CO_R_HREF);
    co_get(vm, imp, "media", CO_R_MEDIA);
    co_get(vm, imp, "styleSheet", CO_R_SHEET);
    co_get(vm, imp, "layerName", CO_R_LAYER);
    co_get(vm, imp, "supportsText", CO_R_SUPPORTS);
    uint32_t ff = co_class(vm, CP_FONT_FACE, "CSSFontFaceRule", rule, CO_ILLEGAL);
    co_get(vm, ff, "style", CO_R_STYLE);
    uint32_t kfs = co_class(vm, CP_KEYFRAMES, "CSSKeyframesRule", rule, CO_ILLEGAL);
    co_getset(vm, kfs, "name", CO_R_NAME, CO_R_NAME_SET);
    co_get(vm, kfs, "cssRules", CO_R_RULES);
    co_get(vm, kfs, "length", CO_R_LENGTH);
    sh_method(vm, kfs, "appendRule", CO_R_APPEND, 1);
    sh_method(vm, kfs, "deleteRule", CO_R_KDELETE, 1);
    sh_method(vm, kfs, "findRule", CO_R_FIND, 1);
    uint32_t kf = co_class(vm, CP_KEYFRAME, "CSSKeyframeRule", rule, CO_ILLEGAL);
    co_getset(vm, kf, "keyText", CO_R_KEYTEXT, CO_R_KEYTEXT_SET);
    co_get(vm, kf, "style", CO_R_STYLE);
    uint32_t page = co_class(vm, CP_PAGE, "CSSPageRule", group, CO_ILLEGAL);
    co_getset(vm, page, "selectorText", CO_R_SELECTOR, CO_R_SELECTOR_SET);
    co_get(vm, page, "style", CO_R_STYLE);
    uint32_t mr = co_class(vm, CP_MARGIN, "CSSMarginRule", rule, CO_ILLEGAL);
    co_get(vm, mr, "name", CO_R_NAME);
    co_get(vm, mr, "style", CO_R_STYLE);
    uint32_t ns = co_class(vm, CP_NAMESPACE, "CSSNamespaceRule", rule, CO_ILLEGAL);
    co_get(vm, ns, "namespaceURI", CO_R_NSURI);
    co_get(vm, ns, "prefix", CO_R_PREFIX);
    uint32_t pr = co_class(vm, CP_PROPERTY, "CSSPropertyRule", rule, CO_ILLEGAL);
    co_get(vm, pr, "name", CO_R_NAME);
    co_get(vm, pr, "syntax", CO_R_SYNTAX);
    co_get(vm, pr, "inherits", CO_R_INHERITS);
    co_get(vm, pr, "initialValue", CO_R_INITIAL);
    uint32_t cs = co_class(vm, CP_COUNTER_STYLE, "CSSCounterStyleRule", rule, CO_ILLEGAL);
    co_getset(vm, cs, "name", CO_R_NAME, CO_R_NAME_SET);
    static const char *const descs[] = { "system", "symbols", "additiveSymbols", "negative", "prefix", "suffix", "range", "pad", "speakAs", "fallback", 0 };
    for (int k = 0; descs[k]; k++) {
        char gn[48], sn[48];
        snprintf(gn, sizeof gn, "get %s", descs[k]);
        snprintf(sn, sizeof sn, "set %s", descs[k]);
        uint32_t a = co_atom(vm, descs[k]);
        js_define_accessor(vm, cs, a, jv_obj(js_native_new(vm, CO_R_COUNTER, co_atom(vm, gn), 0)), jv_obj(js_native_new(vm, CO_R_COUNTER_SET, co_atom(vm, sn), 1)), 0);
    }
    uint32_t sc = co_class(vm, CP_SCOPE, "CSSScopeRule", group, CO_ILLEGAL);
    co_get(vm, sc, "start", CO_R_START);
    co_get(vm, sc, "end", CO_R_END);
    co_class(vm, CP_STARTING_STYLE, "CSSStartingStyleRule", group, CO_ILLEGAL);
    uint32_t ffv = co_class(vm, CP_FONT_FEATURE_VALUES, "CSSFontFeatureValuesRule", rule, CO_ILLEGAL);
    co_get(vm, ffv, "fontFamily", CO_R_FAMILY);
    uint32_t pal = co_class(vm, CP_FONT_PALETTE_VALUES, "CSSFontPaletteValuesRule", rule, CO_ILLEGAL);
    co_get(vm, pal, "name", CO_R_NAME);
    co_get(vm, pal, "fontFamily", CO_R_FAMILY);
    co_get(vm, pal, "basePalette", CO_R_BASE_PALETTE);
    co_get(vm, pal, "overrideColors", CO_R_OVERRIDE);
    uint32_t vt = co_class(vm, CP_VIEW_TRANSITION, "CSSViewTransitionRule", rule, CO_ILLEGAL);
    co_get(vm, vt, "navigation", CO_R_NAVIGATION);
    co_get(vm, vt, "types", CO_R_TYPES);
    uint32_t pt = co_class(vm, CP_POSITION_TRY, "CSSPositionTryRule", rule, CO_ILLEGAL);
    co_get(vm, pt, "name", CO_R_NAME);
    co_get(vm, pt, "style", CO_R_STYLE);
    uint32_t nd = co_class(vm, CP_NESTED_DECLARATIONS, "CSSNestedDeclarations", rule, CO_ILLEGAL);
    co_get(vm, nd, "style", CO_R_STYLE);
    /* CSSStyleDeclaration's own: getPropertyPriority and item (setProperty, getPropertyValue and
       removeProperty are script.c's) */
    uint32_t sp = h->protos[HP_STYLE];
    sh_method(vm, sp, "getPropertyPriority", CO_D_PRIORITY, 1);
    sh_method(vm, sp, "item", CO_D_ITEM, 1);
    js_define(vm, sp, JS_KEY_ITERATOR, values, JS_SHAPE_HIDDEN);
    js_define(vm, sp, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, "CSSStyleDeclaration"), JS_SHAPE_HIDDEN);
    /* the observable array's handler */
    uint32_t hd = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    m->protos[CP_HANDLER] = hd;
    sh_method(vm, hd, "set", CO_A_SET, 4);
    sh_method(vm, hd, "deleteProperty", CO_A_DELETE, 2);
    sh_method(vm, hd, "defineProperty", CO_A_DEFINE, 3);
    /* the trees' lists, adoptedStyleSheets, an element's sheet */
    uint32_t trees[2] = { h->protos[HP_DOCUMENT], h->protos[HP_SHADOWROOT] };
    for (int k = 0; k < 2; k++) {
        if (!trees[k]) continue;
        co_get(vm, trees[k], "styleSheets", CO_T_SHEETS);
        co_getset(vm, trees[k], "adoptedStyleSheets", CO_T_ADOPTED, CO_T_ADOPTED_SET);
    }
    co_get(vm, h->protos[HP_ELEMENT], "sheet", CO_E_SHEET);
}
