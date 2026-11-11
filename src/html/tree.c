/* src/html/tree.c -- the WHATWG tree construction (HTML 13.2.6) and the fragment
   parsing algorithm (13.4), from the token table of the HTML module's store to a
   DOM document; and the HTML module's generated implementation.

   The tree builder speaks to the DOM through its operations and its traversal
   only (dom_create_element, dom_append_child, dom_insert_before, dom_children_*
   ...): the parser's output contract is the DOM's contract, never its storage
   (the architecture's §7). The token table is read as const; the tree builder's
   own parse errors are gathered apart and recorded when the tree is built, so
   no borrow of the store outlives a change to it.

   What is not done yet: foreign content (SVG and MathML elements are HTML
   elements here) and template contents (a template is an ordinary element);
   scripting is off. Everything else follows the standard's steps, by its names. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_implementation(html);

html_status html_tokenize_in(html_parser *p, const char *src, size_t src_len, int state, const char *last, size_t last_len);

/* ---- the tags the algorithm names --------------------------------------------- */

enum html_tag {
    T_UNKNOWN, T_A, T_ADDRESS, T_APPLET, T_AREA, T_ARTICLE, T_ASIDE, T_B, T_BASE, T_BASEFONT, T_BGSOUND, T_BIG,
    T_BLOCKQUOTE, T_BODY, T_BR, T_BUTTON, T_CAPTION, T_CENTER, T_CODE, T_COL, T_COLGROUP, T_DD, T_DETAILS,
    T_DIALOG, T_DIR, T_DIV, T_DL, T_DT, T_EM, T_EMBED, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FONT, T_FOOTER,
    T_FORM, T_FRAME, T_FRAMESET, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER, T_HGROUP, T_HR, T_HTML,
    T_I, T_IFRAME, T_IMAGE, T_IMG, T_INPUT, T_KEYGEN, T_LI, T_LINK, T_LISTING, T_MAIN, T_MARQUEE, T_MATH,
    T_MENU, T_META, T_NAV, T_NOBR, T_NOEMBED, T_NOFRAMES, T_NOSCRIPT, T_OBJECT, T_OL, T_OPTGROUP, T_OPTION,
    T_P, T_PARAM, T_PLAINTEXT, T_PRE, T_RB, T_RP, T_RT, T_RTC, T_RUBY, T_S, T_SCRIPT, T_SEARCH, T_SECTION,
    T_SELECT, T_SMALL, T_SOURCE, T_SPAN, T_STRIKE, T_STRONG, T_STYLE, T_SUB, T_SUMMARY, T_SUP, T_SVG,
    T_TABLE, T_TBODY, T_TD, T_TEMPLATE, T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TITLE, T_TR, T_TRACK, T_TT,
    T_U, T_UL, T_WBR, T_XMP, T_MARKER
};

/* sorted, for the binary search; the order of the enum above */
static const char *html_tag_names[] = {
    "a", "address", "applet", "area", "article", "aside", "b", "base", "basefont", "bgsound", "big",
    "blockquote", "body", "br", "button", "caption", "center", "code", "col", "colgroup", "dd", "details",
    "dialog", "dir", "div", "dl", "dt", "em", "embed", "fieldset", "figcaption", "figure", "font", "footer",
    "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr", "html",
    "i", "iframe", "image", "img", "input", "keygen", "li", "link", "listing", "main", "marquee", "math",
    "menu", "meta", "nav", "nobr", "noembed", "noframes", "noscript", "object", "ol", "optgroup", "option",
    "p", "param", "plaintext", "pre", "rb", "rp", "rt", "rtc", "ruby", "s", "script", "search", "section",
    "select", "small", "source", "span", "strike", "strong", "style", "sub", "summary", "sup", "svg",
    "table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "title", "tr", "track", "tt",
    "u", "ul", "wbr", "xmp"
};
#define HTML_TAG_COUNT ((int)(sizeof html_tag_names / sizeof html_tag_names[0]))

static int html_tag_of(const char *s, size_t n) {
    int lo = 0, hi = HTML_TAG_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        size_t m = strlen(html_tag_names[mid]);
        int r = memcmp(html_tag_names[mid], s, m < n ? m : n);
        if (r == 0) r = m < n ? -1 : (m > n ? 1 : 0);
        if (r == 0) return mid + 1;
        if (r < 0) lo = mid + 1; else hi = mid - 1;
    }
    return T_UNKNOWN;
}

static int html_in(int t, const int *set, size_t n) {
    for (size_t i = 0; i < n; i++) if (set[i] == t) return 1;
    return 0;
}
#define IN(t, ...) html_in((t), (const int[]){ __VA_ARGS__ }, sizeof((const int[]){ __VA_ARGS__ }) / sizeof(int))

static int html_special(int t) {
    return IN(t, T_ADDRESS, T_APPLET, T_AREA, T_ARTICLE, T_ASIDE, T_BASE, T_BASEFONT, T_BGSOUND, T_BLOCKQUOTE,
              T_BODY, T_BR, T_BUTTON, T_CAPTION, T_CENTER, T_COL, T_COLGROUP, T_DD, T_DETAILS, T_DIR, T_DIV, T_DL,
              T_DT, T_EMBED, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FOOTER, T_FORM, T_FRAME, T_FRAMESET, T_H1, T_H2,
              T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER, T_HGROUP, T_HR, T_HTML, T_IFRAME, T_IMG, T_INPUT,
              T_KEYGEN, T_LI, T_LINK, T_LISTING, T_MAIN, T_MARQUEE, T_MENU, T_META, T_NAV, T_NOEMBED,
              T_NOFRAMES, T_NOSCRIPT, T_OBJECT, T_OL, T_P, T_PARAM, T_PLAINTEXT, T_PRE, T_SCRIPT, T_SEARCH,
              T_SECTION, T_SELECT, T_SOURCE, T_STYLE, T_SUMMARY, T_TABLE, T_TBODY, T_TD, T_TEMPLATE,
              T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TITLE, T_TR, T_TRACK, T_UL, T_WBR, T_XMP);
}
static int html_formatting(int t) {
    return IN(t, T_A, T_B, T_BIG, T_CODE, T_EM, T_FONT, T_I, T_NOBR, T_S, T_SMALL, T_STRIKE, T_STRONG, T_TT, T_U);
}
static int html_heading(int t) { return IN(t, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6); }

/* ---- the builder's state -------------------------------------------------------- */

enum html_mode {
    M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_IN_HEAD_NOSCRIPT, M_AFTER_HEAD, M_IN_BODY, M_TEXT,
    M_IN_TABLE, M_IN_TABLE_TEXT, M_IN_CAPTION, M_IN_COLUMN_GROUP, M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL,
    M_IN_SELECT, M_IN_SELECT_IN_TABLE, M_AFTER_BODY, M_IN_FRAMESET, M_AFTER_FRAMESET, M_AFTER_AFTER_BODY,
    M_AFTER_AFTER_FRAMESET
};

/* an entry of the stack of open elements or of the list of active formatting
   elements: the node, its tag, its name's atom in the DOM, the token it was made for */
typedef struct html_open { dom_node_h node; uint32_t tag; uint32_t atom; uint32_t token; } html_open;

typedef struct html_ctx {
    int mode, orig_mode;
    dom_node_h head, form, ctx_elem;
    int frameset_ok, foster, quirks, fragment, ctx_tag, skip_lf, stopped;
    uint32_t tok;                       /* the token being processed */
} html_ctx;

/* every routine takes the same context: the document, the token table, the
   builder's scalars, the stack, the active formatting list, the pending table
   characters and the builder's own parse errors */
#define CTX dom_document *d, const html_parser *p, html_ctx *c, vector<html_open> *open, vector<html_open> *afe, vector<char> *pend, vector<uint32_t> *errs
#define ARGS d, p, c, open, afe, pend, errs

static void html_terr(CTX) { push_back(errs, c->tok); }

/* ---- the token being processed ---- */
static html_token_kind tk_kind(const html_parser *p, uint32_t t) { return p->token[t].kind; }
static int tk_tag(const html_parser *p, uint32_t t) {
    amb_span s = p->token[t].name;
    return html_tag_of(bytes_ptr(p, s), s.len);
}
static int tk_name_is(const html_parser *p, uint32_t t, const char *w) {
    amb_span s = p->token[t].name;
    return s.len == strlen(w) && memcmp(bytes_ptr(p, s), w, s.len) == 0;
}
/* the token's attribute of this name, or 0 */
static uint32_t tk_attr(const html_parser *p, uint32_t t, const char *w) {
    for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
        amb_span s = p->tattr[a].name;
        if (s.len == strlen(w) && memcmp(bytes_ptr(p, s), w, s.len) == 0) return a;
    }
    return 0;
}
static int tk_attr_value_is_ci(const html_parser *p, uint32_t a, const char *w) {
    amb_span s = p->tattr[a].value;
    if (s.len != strlen(w)) return 0;
    for (uint32_t i = 0; i < s.len; i++) {
        char x = bytes_ptr(p, s)[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (x != w[i]) return 0;
    }
    return 1;
}

/* ---- the stack of open elements ---- */
static size_t st_len(const vector<html_open> *s) { return len(*s); }
static html_open st_at(const vector<html_open> *s, size_t i) { return at(*s, i); }
static html_open st_top(const vector<html_open> *s) { return back(*s); }
static int cur_tag(const vector<html_open> *s) { return len(*s) ? (int)back(*s).tag : T_UNKNOWN; }
static int st_has_tag(const vector<html_open> *s, int t) {
    for (size_t i = 0; i < len(*s); i++) if ((int)at(*s, i).tag == t) return 1;
    return 0;
}
static int st_index_of_node(const vector<html_open> *s, dom_node_h n) {
    for (size_t i = len(*s); i-- > 0;) if (at(*s, i).node.index == n.index) return (int)i;
    return -1;
}

/* 13.2.4.2: has an element in the specific scope */
enum { SC_DEFAULT, SC_LIST_ITEM, SC_BUTTON, SC_TABLE, SC_SELECT };
static int html_scope_boundary(int t, int kind) {
    if (kind == SC_TABLE) return IN(t, T_HTML, T_TABLE, T_TEMPLATE);
    if (kind == SC_SELECT) return !IN(t, T_OPTGROUP, T_OPTION);
    if (IN(t, T_APPLET, T_CAPTION, T_HTML, T_TABLE, T_TD, T_TH, T_MARQUEE, T_OBJECT, T_TEMPLATE)) return 1;
    if (kind == SC_LIST_ITEM && IN(t, T_OL, T_UL)) return 1;
    if (kind == SC_BUTTON && t == T_BUTTON) return 1;
    return 0;
}
/* t in scope; for an unknown tag, the atom names it */
static int in_scope(const vector<html_open> *s, int t, uint32_t atom, int kind) {
    for (size_t i = len(*s); i-- > 0;) {
        html_open e = at(*s, i);
        if (t != T_UNKNOWN ? (int)e.tag == t : e.atom == atom) return 1;
        if (html_scope_boundary((int)e.tag, kind)) return 0;
    }
    return 0;
}
static int heading_in_scope(const vector<html_open> *s) {
    for (size_t i = len(*s); i-- > 0;) {
        html_open e = at(*s, i);
        if (html_heading((int)e.tag)) return 1;
        if (html_scope_boundary((int)e.tag, SC_DEFAULT)) return 0;
    }
    return 0;
}
/* the root (the html element) is never popped */
static void st_pop(vector<html_open> *s) { if (len(*s) > 1) pop_back(s); }
/* pop until an element of tag t has been popped */
static void st_pop_until(vector<html_open> *s, int t) {
    while (len(*s) > 1) { int top = (int)back(*s).tag; pop_back(s); if (top == t) break; }
}
static void st_pop_until_heading(vector<html_open> *s) {
    while (len(*s) > 1) { int top = (int)back(*s).tag; pop_back(s); if (html_heading(top)) break; }
}
static void st_remove_at(vector<html_open> *s, size_t i) { erase(s, i); }

/* 13.2.6.3: generate implied end tags, but not for except */
static void implied_end_tags(vector<html_open> *s, int except) {
    while (len(*s) > 1) {
        int t = (int)back(*s).tag;
        if (t == except || !IN(t, T_DD, T_DT, T_LI, T_OPTGROUP, T_OPTION, T_P, T_RB, T_RP, T_RT, T_RTC)) return;
        pop_back(s);
    }
}
static void implied_end_tags_thoroughly(vector<html_open> *s) {
    while (len(*s) > 1) {
        int t = (int)back(*s).tag;
        if (!IN(t, T_CAPTION, T_COLGROUP, T_DD, T_DT, T_LI, T_OPTGROUP, T_OPTION, T_P, T_RB, T_RP, T_RT, T_RTC,
                T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) return;
        pop_back(s);
    }
}

/* ---- inserting nodes (13.2.6.1) ---- */

/* the appropriate place for inserting a node: a parent, and the node to insert
   before (none: at the end); foster parenting puts it before the last table */
static void insertion_place(CTX, dom_node_h target, dom_node_h *parent, dom_node_h *before) {
    dom_node_h none = { 0, 0, 0 };
    *before = none;
    *parent = target;
    int tt = -1;
    int ti = st_index_of_node(open, target);
    if (ti >= 0) tt = (int)st_at(open, (size_t)ti).tag;
    if (c->foster && IN(tt, T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR)) {
        int last_table = -1;
        for (size_t i = st_len(open); i-- > 0;) if ((int)st_at(open, i).tag == T_TABLE) { last_table = (int)i; break; }
        if (last_table < 0) { *parent = st_at(open, 0).node; return; }
        dom_node_h table = st_at(open, (size_t)last_table).node;
        dom_node_h tp = dom_children_up(d, table);
        if (tp.index) { *parent = tp; *before = table; return; }
        *parent = st_at(open, (size_t)last_table - 1).node;
    }
}

static void insert_node_at(CTX, dom_node_h parent, dom_node_h before, dom_node_h node) {
    if (before.index) dom_insert_before(d, parent, node, before);
    else dom_append_child(d, parent, node);
}

static dom_node_h current_node(const vector<html_open> *open) { return st_top(open).node; }

/* an element for the token: its name and its attributes */
static dom_node_h create_element(CTX, uint32_t t) {
    amb_span n = p->token[t].name;
    dom_node_h e = dom_create_element(d, bytes_ptr(p, n), n.len);
    for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
        amb_span an = p->tattr[a].name, av = p->tattr[a].value;
        dom_set_attribute(d, e, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
    }
    return e;
}

static html_open open_entry(const dom_document *d, dom_node_h e, int tag, uint32_t t) {
    html_open o;
    o.node = e;
    o.tag = (uint32_t)tag;
    o.atom = d->node[e.index].name;
    o.token = t;
    return o;
}

/* insert an HTML element for the token, at the appropriate place, and push it */
static dom_node_h insert_element(CTX, uint32_t t) {
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    dom_node_h e = create_element(ARGS, t);
    insert_node_at(ARGS, parent, before, e);
    push_back(open, open_entry(d, e, tk_tag(p, t), t));
    return e;
}

/* an element made for a name alone (an implied html, head, body, tbody ...) */
static dom_node_h insert_implied(CTX, const char *name, int tag) {
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    dom_node_h e = dom_create_element(d, name, strlen(name));
    insert_node_at(ARGS, parent, before, e);
    push_back(open, open_entry(d, e, tag, 0));
    return e;
}

/* the html element, made for no token, appended to the document (before html) */
static void insert_html_root(CTX) {
    dom_node_h e = dom_create_element(d, "html", 4);
    dom_append_child(d, dom_root(d), e);
    push_back(open, open_entry(d, e, T_HTML, 0));
}

/* insert characters: into the text node just before the place, or a new one */
static void insert_text(CTX, const char *s, size_t n) {
    if (n == 0) return;
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    if (d->node[parent.index].kind == DOM_NODE_KIND_DOCUMENT) return;
    dom_node_h prev = before.index ? dom_children_prev(d, before) : dom_children_last(d, parent);
    if (prev.index && d->node[prev.index].kind == DOM_NODE_KIND_TEXT) {
        vector<char> joined;
        amb_span old = d->node[prev.index].data;
        append_range(joined, text_ptr(d, old), old.len);
        append_range(joined, s, n);
        dom_set_data(d, prev, data(joined), len(joined));
        return;
    }
    dom_node_h tn = dom_create_text(d, s, n);
    insert_node_at(ARGS, parent, before, tn);
}

static void insert_comment_in(CTX, dom_node_h parent, uint32_t t) {
    amb_span s = p->token[t].data;
    dom_node_h cm = dom_create_comment(d, bytes_ptr(p, s), s.len);
    dom_append_child(d, parent, cm);
}
static void insert_comment(CTX, uint32_t t) {
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    amb_span s = p->token[t].data;
    dom_node_h cm = dom_create_comment(d, bytes_ptr(p, s), s.len);
    insert_node_at(ARGS, parent, before, cm);
}

/* ---- the list of active formatting elements (13.2.4.3) ---- */

static void afe_push_marker(vector<html_open> *afe) {
    html_open m;
    memset(&m, 0, sizeof m);
    m.tag = T_MARKER;
    push_back(afe, m);
}
/* two elements have the same attributes: names and values, in any order */
static int same_attrs(const dom_document *d, dom_node_h a, dom_node_h b) {
    size_t na = 0, nb = 0;
    for (uint32_t x = d->node[a.index].first_attr; x; x = d->attr[x].next) na++;
    for (uint32_t y = d->node[b.index].first_attr; y; y = d->attr[y].next) nb++;
    if (na != nb) return 0;
    for (uint32_t x = d->node[a.index].first_attr; x; x = d->attr[x].next) {
        int found = 0;
        for (uint32_t y = d->node[b.index].first_attr; y; y = d->attr[y].next) {
            if (d->attr[x].name != d->attr[y].name) continue;
            amb_span vx = d->attr[x].value, vy = d->attr[y].value;
            if (vx.len == vy.len && memcmp(text_ptr(d, vx), text_ptr(d, vy), vx.len) == 0) found = 1;
            break;
        }
        if (!found) return 0;
    }
    return 1;
}
/* push an element: the Noah's Ark clause keeps at most three alike after the last marker */
static void afe_push(CTX, html_open e) {
    int alike = 0, earliest = -1;
    for (size_t i = len(*afe); i-- > 0;) {
        html_open x = at(*afe, i);
        if (x.tag == T_MARKER) break;
        if (x.tag == e.tag && x.atom == e.atom && same_attrs(d, x.node, e.node)) { alike++; earliest = (int)i; }
    }
    if (alike >= 3) erase(afe, (size_t)earliest);
    push_back(afe, e);
}
static int afe_index_of_node(const vector<html_open> *afe, dom_node_h n) {
    for (size_t i = len(*afe); i-- > 0;) if (at(*afe, i).tag != T_MARKER && at(*afe, i).node.index == n.index) return (int)i;
    return -1;
}
static void afe_clear_to_marker(vector<html_open> *afe) {
    while (len(*afe)) { int t = (int)back(*afe).tag; pop_back(afe); if (t == T_MARKER) break; }
}
/* reconstruct the active formatting elements */
static void reconstruct_afe(CTX) {
    size_t n = len(*afe);
    if (n == 0) return;
    html_open last = at(*afe, n - 1);
    if (last.tag == T_MARKER || st_index_of_node(open, last.node) >= 0) return;
    size_t i = n - 1;
    while (i > 0) {
        html_open e = at(*afe, i - 1);
        if (e.tag == T_MARKER || st_index_of_node(open, e.node) >= 0) break;
        i--;
    }
    for (; i < n; i++) {
        html_open e = at(*afe, i);
        dom_node_h ne = insert_element(ARGS, e.token);
        html_open ne_entry = st_top(open);
        ne_entry.node = ne;
        (*afe)[i] = ne_entry;
    }
}

/* ---- the adoption agency algorithm (13.2.6.4.7) ---- */

static int adoption_agency(CTX, int tag, uint32_t atom) {
    /* step 2: the current node is the subject and not in the list: pop it */
    html_open cn = st_top(open);
    if ((tag != T_UNKNOWN ? (int)cn.tag == tag : cn.atom == atom) && afe_index_of_node(afe, cn.node) < 0) {
        st_pop(open);
        return 1;
    }
    for (int outer = 0; outer < 8; outer++) {
        int fi = -1;
        for (size_t i = len(*afe); i-- > 0;) {
            html_open x = at(*afe, i);
            if (x.tag == T_MARKER) break;
            if (tag != T_UNKNOWN ? (int)x.tag == tag : x.atom == atom) { fi = (int)i; break; }
        }
        if (fi < 0) return 0;                       /* act as any other end tag */
        html_open fe = at(*afe, (size_t)fi);
        int si = st_index_of_node(open, fe.node);
        if (si < 0) { html_terr(ARGS); erase(afe, (size_t)fi); return 1; }
        if (!in_scope(open, (int)fe.tag, fe.atom, SC_DEFAULT)) { html_terr(ARGS); return 1; }
        if (st_top(open).node.index != fe.node.index) html_terr(ARGS);
        int fb = -1;
        for (size_t i = (size_t)si + 1; i < st_len(open); i++) if (html_special((int)st_at(open, i).tag)) { fb = (int)i; break; }
        if (fb < 0) {
            while (st_len(open) > (size_t)si) st_pop(open);
            erase(afe, (size_t)fi);
            return 1;
        }
        html_open common = st_at(open, (size_t)si - 1);
        html_open furthest = st_at(open, (size_t)fb);
        int bookmark = fi;
        html_open node_e = furthest, last_node = furthest;
        int node_i = fb;
        for (int inner = 1;; inner++) {
            node_i--;
            node_e = st_at(open, (size_t)node_i);
            if (node_e.node.index == fe.node.index) break;
            int ai = afe_index_of_node(afe, node_e.node);
            if (inner > 3 && ai >= 0) {
                erase(afe, (size_t)ai);
                if (ai < bookmark) bookmark--;
                ai = -1;
            }
            if (ai < 0) { st_remove_at(open, (size_t)node_i); continue; }
            /* a new element for the node's token, in the list and the stack */
            dom_node_h ne = create_element(ARGS, node_e.token);
            html_open ne_entry = open_entry(d, ne, (int)node_e.tag, node_e.token);
            (*afe)[(size_t)ai] = ne_entry;
            (*open)[(size_t)node_i] = ne_entry;
            node_e = ne_entry;
            if (last_node.node.index == furthest.node.index) bookmark = ai + 1;
            dom_node_h lp = dom_children_up(d, last_node.node);
            if (lp.index) dom_remove_child(d, lp, last_node.node);
            dom_append_child(d, node_e.node, last_node.node);
            last_node = node_e;
        }
        /* the last node into the common ancestor, at the appropriate place */
        dom_node_h lp = dom_children_up(d, last_node.node);
        if (lp.index) dom_remove_child(d, lp, last_node.node);
        dom_node_h parent, before;
        insertion_place(ARGS, common.node, &parent, &before);
        insert_node_at(ARGS, parent, before, last_node.node);
        /* a new formatting element takes the furthest block's children */
        dom_node_h ne = create_element(ARGS, fe.token);
        for (dom_node_h ch = dom_children_first(d, furthest.node); ch.index; ch = dom_children_first(d, furthest.node)) {
            dom_remove_child(d, furthest.node, ch);
            dom_append_child(d, ne, ch);
        }
        dom_append_child(d, furthest.node, ne);
        html_open ne_entry = open_entry(d, ne, (int)fe.tag, fe.token);
        int old = afe_index_of_node(afe, fe.node);
        if (old >= 0) { erase(afe, (size_t)old); if (old < bookmark) bookmark--; }
        if (bookmark > (int)len(*afe)) bookmark = (int)len(*afe);
        insert(afe, (size_t)bookmark, ne_entry);
        int fsi = st_index_of_node(open, fe.node);
        if (fsi >= 0) st_remove_at(open, (size_t)fsi);
        int fbi = st_index_of_node(open, furthest.node);
        insert(open, (size_t)fbi + 1, ne_entry);
    }
    return 1;
}

/* ---- the insertion modes ---------------------------------------------------------- */

static void reset_insertion_mode(CTX) {
    for (size_t i = st_len(open); i-- > 0;) {
        int last = i == 0;
        int t = (int)st_at(open, i).tag;
        if (last && c->fragment) t = c->ctx_tag;
        if (t == T_SELECT) {
            if (!last) for (size_t j = i; j-- > 0;) {
                int a = (int)st_at(open, j).tag;
                if (a == T_TEMPLATE) break;
                if (a == T_TABLE) { c->mode = M_IN_SELECT_IN_TABLE; return; }
            }
            c->mode = M_IN_SELECT; return;
        }
        if ((t == T_TD || t == T_TH) && !last) { c->mode = M_IN_CELL; return; }
        if (t == T_TR) { c->mode = M_IN_ROW; return; }
        if (IN(t, T_TBODY, T_THEAD, T_TFOOT)) { c->mode = M_IN_TABLE_BODY; return; }
        if (t == T_CAPTION) { c->mode = M_IN_CAPTION; return; }
        if (t == T_COLGROUP) { c->mode = M_IN_COLUMN_GROUP; return; }
        if (t == T_TABLE) { c->mode = M_IN_TABLE; return; }
        if (t == T_HEAD && !last) { c->mode = M_IN_HEAD; return; }
        if (t == T_BODY) { c->mode = M_IN_BODY; return; }
        if (t == T_FRAMESET) { c->mode = M_IN_FRAMESET; return; }
        if (t == T_HTML) { c->mode = c->head.index ? M_AFTER_HEAD : M_BEFORE_HEAD; return; }
        if (last) { c->mode = M_IN_BODY; return; }
    }
    c->mode = M_IN_BODY;
}

static int is_ws(char ch) { return ch == '\t' || ch == '\n' || ch == '\f' || ch == ' ' || ch == '\r'; }
static size_t ws_prefix(const char *s, size_t n) { size_t i = 0; while (i < n && is_ws(s[i])) i++; return i; }

static void close_p(CTX) {
    implied_end_tags(open, T_P);
    if (cur_tag(open) != T_P) html_terr(ARGS);
    st_pop_until(open, T_P);
}

static int process(CTX, int mode);

/* the generic RCDATA and raw text element parsing algorithm: the element, then text mode */
static void generic_text_element(CTX, uint32_t t) {
    insert_element(ARGS, t);
    c->orig_mode = c->mode;
    c->mode = M_TEXT;
}

/* in head, for the tags it shares with in body and in table */
static int in_head(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = k == HTML_TOKEN_KIND_START_TAG || k == HTML_TOKEN_KIND_END_TAG ? tk_tag(p, t) : T_UNKNOWN;
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    if (k == HTML_TOKEN_KIND_START_TAG) {
        if (tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META)) { insert_element(ARGS, t); st_pop(open); return 0; }
        if (tag == T_TITLE) { generic_text_element(ARGS, t); return 0; }
        if (IN(tag, T_NOFRAMES, T_STYLE)) { generic_text_element(ARGS, t); return 0; }
        if (tag == T_NOSCRIPT) { insert_element(ARGS, t); c->mode = M_IN_HEAD_NOSCRIPT; return 0; }
        if (tag == T_SCRIPT) { generic_text_element(ARGS, t); return 0; }
        if (tag == T_TEMPLATE) { insert_element(ARGS, t); afe_push_marker(afe); c->frameset_ok = 0; return 0; }
        if (tag == T_HEAD) { html_terr(ARGS); return 0; }
    }
    if (k == HTML_TOKEN_KIND_END_TAG) {
        if (tag == T_HEAD) { st_pop(open); c->mode = M_AFTER_HEAD; return 0; }
        if (tag == T_TEMPLATE) {
            if (!st_has_tag(open, T_TEMPLATE)) { html_terr(ARGS); return 0; }
            implied_end_tags_thoroughly(open);
            st_pop_until(open, T_TEMPLATE);
            afe_clear_to_marker(afe);
            return 0;
        }
        if (!IN(tag, T_BODY, T_HTML, T_BR)) { html_terr(ARGS); return 0; }
    }
    /* anything else: pop the head, reprocess after head */
    st_pop(open);
    c->mode = M_AFTER_HEAD;
    return 1;
}

static int in_body(CTX);
static int in_table(CTX);

/* a characters token, its text through the modes: the leading white space where
   a mode treats it apart, the rest reprocessed */
static void characters(CTX, const char *s, size_t n);

static int in_body_chars(CTX, const char *s, size_t n) {
    vector<char> keep;
    for (size_t i = 0; i < n; i++) if (s[i] != 0) push_back(keep, s[i]);
    if (len(keep) == 0) return 0;
    reconstruct_afe(ARGS);
    insert_text(ARGS, data(keep), len(keep));
    if (ws_prefix(data(keep), len(keep)) != len(keep)) c->frameset_ok = 0;
    return 0;
}

static void characters(CTX, const char *s, size_t n) {
    while (n > 0) {
        if (c->skip_lf) { c->skip_lf = 0; if (s[0] == '\n') { s++; n--; continue; } }
        size_t ws = ws_prefix(s, n);
        int m = c->mode;
        if (m == M_INITIAL || m == M_BEFORE_HTML || m == M_BEFORE_HEAD || m == M_AFTER_AFTER_BODY || m == M_AFTER_AFTER_FRAMESET) {
            if (ws) {
                if (m == M_AFTER_AFTER_BODY || m == M_AFTER_AFTER_FRAMESET) { c->mode = M_IN_BODY; in_body_chars(ARGS, s, ws); c->mode = m; }
                s += ws; n -= ws;
            } else if (m == M_INITIAL) { html_terr(ARGS); c->quirks = 1; c->mode = M_BEFORE_HTML; }
            else if (m == M_BEFORE_HTML) { insert_html_root(ARGS); c->mode = M_BEFORE_HEAD; }
            else if (m == M_BEFORE_HEAD) { c->head = insert_implied(ARGS, "head", T_HEAD); c->mode = M_IN_HEAD; }
            else if (m == M_AFTER_AFTER_FRAMESET) { html_terr(ARGS); n = 0; }
            else { html_terr(ARGS); c->mode = M_IN_BODY; }
        } else if (m == M_IN_HEAD || m == M_IN_HEAD_NOSCRIPT || m == M_AFTER_HEAD) {
            if (ws) { insert_text(ARGS, s, ws); s += ws; n -= ws; }
            else {
                if (m == M_IN_HEAD_NOSCRIPT) { html_terr(ARGS); st_pop(open); c->mode = M_IN_HEAD; }
                if (c->mode == M_IN_HEAD) { st_pop(open); c->mode = M_AFTER_HEAD; }
                insert_implied(ARGS, "body", T_BODY);
                c->mode = M_IN_BODY;
            }
        } else if (m == M_IN_BODY || m == M_IN_CAPTION || m == M_IN_CELL) {
            in_body_chars(ARGS, s, n);
            n = 0;
        } else if (m == M_TEXT) {
            insert_text(ARGS, s, n);
            n = 0;
        } else if (m == M_IN_TABLE || m == M_IN_TABLE_BODY || m == M_IN_ROW) {
            if (IN(cur_tag(open), T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR)) {
                clear(pend);
                c->orig_mode = m;
                c->mode = M_IN_TABLE_TEXT;
            } else {
                html_terr(ARGS);
                c->foster = 1; in_body_chars(ARGS, s, n); c->foster = 0;
                n = 0;
            }
        } else if (m == M_IN_TABLE_TEXT) {
            for (size_t i = 0; i < n; i++) if (s[i] != 0) push_back(pend, s[i]);
            n = 0;
        } else if (m == M_IN_COLUMN_GROUP) {
            if (ws) { insert_text(ARGS, s, ws); s += ws; n -= ws; }
            else if (cur_tag(open) != T_COLGROUP) { html_terr(ARGS); n = 0; }
            else { st_pop(open); c->mode = M_IN_TABLE; }
        } else if (m == M_IN_SELECT || m == M_IN_SELECT_IN_TABLE) {
            vector<char> keep;
            for (size_t i = 0; i < n; i++) if (s[i] != 0) push_back(keep, s[i]);
            insert_text(ARGS, data(keep), len(keep));
            n = 0;
        } else if (m == M_AFTER_BODY) {
            if (ws) { c->mode = M_IN_BODY; in_body_chars(ARGS, s, ws); c->mode = m; s += ws; n -= ws; }
            else { html_terr(ARGS); c->mode = M_IN_BODY; }
        } else if (m == M_IN_FRAMESET || m == M_AFTER_FRAMESET) {
            for (size_t i = 0; i < n; i++) { if (is_ws(s[i])) insert_text(ARGS, s + i, 1); else html_terr(ARGS); }
            n = 0;
        } else n = 0;
    }
}

/* in table text, at the token after the characters: the pending text inserted, or
   foster-parented when it is not all white space */
static void flush_table_text(CTX) {
    if (c->mode != M_IN_TABLE_TEXT) return;
    c->mode = c->orig_mode;
    if (len(*pend) == 0) return;
    vector<char> t;
    append_range(t, data(*pend), len(*pend));
    clear(pend);
    if (ws_prefix(data(t), len(t)) != len(t)) {
        html_terr(ARGS);
        c->foster = 1;
        int m = c->mode;
        c->mode = M_IN_BODY; in_body_chars(ARGS, data(t), len(t)); c->mode = m;
        c->foster = 0;
    } else insert_text(ARGS, data(t), len(t));
}

/* any other end tag, in body */
static void any_other_end_tag(CTX, int tag, uint32_t atom) {
    for (size_t i = st_len(open); i-- > 0;) {
        html_open e = st_at(open, i);
        if (tag != T_UNKNOWN ? (int)e.tag == tag : (e.tag == T_UNKNOWN && e.atom == atom)) {
            implied_end_tags(open, tag != T_UNKNOWN ? tag : -1);
            if (st_top(open).node.index != e.node.index) html_terr(ARGS);
            while (st_len(open) > i) st_pop(open);
            return;
        }
        if (html_special((int)e.tag)) { html_terr(ARGS); return; }
    }
}

static uint32_t tok_atom(const dom_document *d, const html_parser *p, uint32_t t) {
    amb_span s = p->token[t].name;
    return names_find(d, bytes_ptr(p, s), s.len);
}

static int in_body(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    if (k == HTML_TOKEN_KIND_EOF) {
        /* stop parsing (template insertion modes are not kept) */
        c->stopped = 1;
        return 0;
    }
    int tag = tk_tag(p, t);
    if (k == HTML_TOKEN_KIND_START_TAG) {
        if (tag == T_HTML) {
            html_terr(ARGS);
            if (st_has_tag(open, T_TEMPLATE)) return 0;
            dom_node_h h = st_at(open, 0).node;
            for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
                amb_span an = p->tattr[a].name, av = p->tattr[a].value;
                uint32_t atom = names_find(d, bytes_ptr(p, an), an.len);
                int has = 0;
                for (uint32_t x = d->node[h.index].first_attr; x; x = d->attr[x].next) if (atom && d->attr[x].name == atom) has = 1;
                if (!has) dom_set_attribute(d, h, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
            }
            return 0;
        }
        if (IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META, T_NOFRAMES, T_SCRIPT, T_STYLE, T_TEMPLATE, T_TITLE))
            return process(ARGS, M_IN_HEAD);
        if (tag == T_BODY) {
            html_terr(ARGS);
            if (st_len(open) < 2 || (int)st_at(open, 1).tag != T_BODY || st_has_tag(open, T_TEMPLATE)) return 0;
            c->frameset_ok = 0;
            dom_node_h b = st_at(open, 1).node;
            for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
                amb_span an = p->tattr[a].name, av = p->tattr[a].value;
                uint32_t atom = names_find(d, bytes_ptr(p, an), an.len);
                int has = 0;
                for (uint32_t x = d->node[b.index].first_attr; x; x = d->attr[x].next) if (atom && d->attr[x].name == atom) has = 1;
                if (!has) dom_set_attribute(d, b, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
            }
            return 0;
        }
        if (tag == T_FRAMESET) {
            html_terr(ARGS);
            if (st_len(open) < 2 || (int)st_at(open, 1).tag != T_BODY || !c->frameset_ok) return 0;
            dom_node_h b = st_at(open, 1).node;
            dom_node_h bp = dom_children_up(d, b);
            if (bp.index) dom_remove_child(d, bp, b);
            while (st_len(open) > 1) st_pop(open);
            insert_element(ARGS, t);
            c->mode = M_IN_FRAMESET;
            return 0;
        }
        if (IN(tag, T_ADDRESS, T_ARTICLE, T_ASIDE, T_BLOCKQUOTE, T_CENTER, T_DETAILS, T_DIALOG, T_DIR, T_DIV,
               T_DL, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FOOTER, T_HEADER, T_HGROUP, T_MAIN, T_MENU, T_NAV,
               T_OL, T_P, T_SEARCH, T_SECTION, T_SUMMARY, T_UL)) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (html_heading(tag)) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            if (html_heading(cur_tag(open))) { html_terr(ARGS); st_pop(open); }
            insert_element(ARGS, t);
            return 0;
        }
        if (IN(tag, T_PRE, T_LISTING)) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            c->skip_lf = 1;
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_FORM) {
            if (c->form.index && !st_has_tag(open, T_TEMPLATE)) { html_terr(ARGS); return 0; }
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            dom_node_h f = insert_element(ARGS, t);
            if (!st_has_tag(open, T_TEMPLATE)) c->form = f;
            return 0;
        }
        if (IN(tag, T_LI, T_DD, T_DT)) {
            c->frameset_ok = 0;
            for (size_t i = st_len(open); i-- > 0;) {
                int nt = (int)st_at(open, i).tag;
                if ((tag == T_LI && nt == T_LI) || (tag != T_LI && (nt == T_DD || nt == T_DT))) {
                    implied_end_tags(open, nt);
                    if (cur_tag(open) != nt) html_terr(ARGS);
                    st_pop_until(open, nt);
                    break;
                }
                if (html_special(nt) && !IN(nt, T_ADDRESS, T_DIV, T_P)) break;
            }
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (tag == T_PLAINTEXT) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (tag == T_BUTTON) {
            if (in_scope(open, T_BUTTON, 0, SC_DEFAULT)) {
                html_terr(ARGS);
                implied_end_tags(open, -1);
                st_pop_until(open, T_BUTTON);
            }
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_A) {
            for (size_t i = len(*afe); i-- > 0;) {
                html_open x = at(*afe, i);
                if (x.tag == T_MARKER) break;
                if (x.tag == T_A) {
                    html_terr(ARGS);
                    if (!adoption_agency(ARGS, T_A, 0)) any_other_end_tag(ARGS, T_A, 0);
                    int ai = afe_index_of_node(afe, x.node);
                    if (ai >= 0) erase(afe, (size_t)ai);
                    int si = st_index_of_node(open, x.node);
                    if (si >= 0) st_remove_at(open, (size_t)si);
                    break;
                }
            }
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            afe_push(ARGS, st_top(open));
            return 0;
        }
        if (IN(tag, T_B, T_BIG, T_CODE, T_EM, T_FONT, T_I, T_S, T_SMALL, T_STRIKE, T_STRONG, T_TT, T_U)) {
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            afe_push(ARGS, st_top(open));
            return 0;
        }
        if (tag == T_NOBR) {
            reconstruct_afe(ARGS);
            if (in_scope(open, T_NOBR, 0, SC_DEFAULT)) {
                html_terr(ARGS);
                if (!adoption_agency(ARGS, T_NOBR, 0)) any_other_end_tag(ARGS, T_NOBR, 0);
                reconstruct_afe(ARGS);
            }
            insert_element(ARGS, t);
            afe_push(ARGS, st_top(open));
            return 0;
        }
        if (IN(tag, T_APPLET, T_MARQUEE, T_OBJECT)) {
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            afe_push_marker(afe);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_TABLE) {
            if (!c->quirks && in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            c->frameset_ok = 0;
            c->mode = M_IN_TABLE;
            return 0;
        }
        if (IN(tag, T_AREA, T_BR, T_EMBED, T_IMG, T_KEYGEN, T_WBR)) {
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            st_pop(open);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_INPUT) {
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            st_pop(open);
            uint32_t ty = tk_attr(p, t, "type");
            if (!ty || !tk_attr_value_is_ci(p, ty, "hidden")) c->frameset_ok = 0;
            return 0;
        }
        if (IN(tag, T_PARAM, T_SOURCE, T_TRACK)) { insert_element(ARGS, t); st_pop(open); return 0; }
        if (tag == T_HR) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            st_pop(open);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_IMAGE) {
            html_terr(ARGS);
            reconstruct_afe(ARGS);
            dom_node_h parent, before;
            insertion_place(ARGS, current_node(open), &parent, &before);
            dom_node_h e = dom_create_element(d, "img", 3);
            for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
                amb_span an = p->tattr[a].name, av = p->tattr[a].value;
                dom_set_attribute(d, e, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
            }
            insert_node_at(ARGS, parent, before, e);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_TEXTAREA) {
            insert_element(ARGS, t);
            c->skip_lf = 1;
            c->frameset_ok = 0;
            c->orig_mode = c->mode;
            c->mode = M_TEXT;
            return 0;
        }
        if (tag == T_XMP) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            reconstruct_afe(ARGS);
            c->frameset_ok = 0;
            generic_text_element(ARGS, t);
            return 0;
        }
        if (tag == T_IFRAME) { c->frameset_ok = 0; generic_text_element(ARGS, t); return 0; }
        if (tag == T_NOEMBED) { generic_text_element(ARGS, t); return 0; }
        if (tag == T_SELECT) {
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            c->frameset_ok = 0;
            c->mode = IN(c->mode, M_IN_TABLE, M_IN_CAPTION, M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL) ? M_IN_SELECT_IN_TABLE : M_IN_SELECT;
            return 0;
        }
        if (IN(tag, T_OPTGROUP, T_OPTION)) {
            if (cur_tag(open) == T_OPTION) st_pop(open);
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (IN(tag, T_RB, T_RTC)) {
            if (in_scope(open, T_RUBY, 0, SC_DEFAULT)) {
                implied_end_tags(open, -1);
                if (cur_tag(open) != T_RUBY) html_terr(ARGS);
            }
            insert_element(ARGS, t);
            return 0;
        }
        if (IN(tag, T_RP, T_RT)) {
            if (in_scope(open, T_RUBY, 0, SC_DEFAULT)) {
                implied_end_tags(open, T_RTC);
                if (!IN(cur_tag(open), T_RUBY, T_RTC)) html_terr(ARGS);
            }
            insert_element(ARGS, t);
            return 0;
        }
        if (IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_FRAME, T_HEAD, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) {
            html_terr(ARGS);
            return 0;
        }
        /* any other start tag */
        reconstruct_afe(ARGS);
        insert_element(ARGS, t);
        if (p->token[t].self_closing && (tag == T_MATH || tag == T_SVG)) st_pop(open);
        return 0;
    }
    /* end tags */
    if (tag == T_TEMPLATE) return process(ARGS, M_IN_HEAD);
    if (tag == T_BODY || tag == T_HTML) {
        if (!in_scope(open, T_BODY, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
        c->mode = M_AFTER_BODY;
        return tag == T_HTML;
    }
    if (IN(tag, T_ADDRESS, T_ARTICLE, T_ASIDE, T_BLOCKQUOTE, T_BUTTON, T_CENTER, T_DETAILS, T_DIALOG, T_DIR,
           T_DIV, T_DL, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FOOTER, T_HEADER, T_HGROUP, T_LISTING, T_MAIN,
           T_MENU, T_NAV, T_OL, T_PRE, T_SEARCH, T_SECTION, T_SUMMARY, T_UL)) {
        if (!in_scope(open, tag, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
        implied_end_tags(open, -1);
        if (cur_tag(open) != tag) html_terr(ARGS);
        st_pop_until(open, tag);
        return 0;
    }
    if (tag == T_FORM) {
        if (!st_has_tag(open, T_TEMPLATE)) {
            dom_node_h f = c->form;
            dom_node_h none = { 0, 0, 0 };
            c->form = none;
            int fi = f.index ? st_index_of_node(open, f) : -1;
            if (fi < 0 || !in_scope(open, T_FORM, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
            implied_end_tags(open, -1);
            if (st_top(open).node.index != f.index) html_terr(ARGS);
            st_remove_at(open, (size_t)fi);
        } else {
            if (!in_scope(open, T_FORM, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
            implied_end_tags(open, -1);
            if (cur_tag(open) != T_FORM) html_terr(ARGS);
            st_pop_until(open, T_FORM);
        }
        return 0;
    }
    if (tag == T_P) {
        if (!in_scope(open, T_P, 0, SC_BUTTON)) { html_terr(ARGS); insert_implied(ARGS, "p", T_P); }
        close_p(ARGS);
        return 0;
    }
    if (tag == T_LI) {
        if (!in_scope(open, T_LI, 0, SC_LIST_ITEM)) { html_terr(ARGS); return 0; }
        implied_end_tags(open, T_LI);
        if (cur_tag(open) != T_LI) html_terr(ARGS);
        st_pop_until(open, T_LI);
        return 0;
    }
    if (IN(tag, T_DD, T_DT)) {
        if (!in_scope(open, tag, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
        implied_end_tags(open, tag);
        if (cur_tag(open) != tag) html_terr(ARGS);
        st_pop_until(open, tag);
        return 0;
    }
    if (html_heading(tag)) {
        if (!heading_in_scope(open)) { html_terr(ARGS); return 0; }
        implied_end_tags(open, -1);
        if (cur_tag(open) != tag) html_terr(ARGS);
        st_pop_until_heading(open);
        return 0;
    }
    if (html_formatting(tag)) {
        if (!adoption_agency(ARGS, tag, 0)) any_other_end_tag(ARGS, tag, 0);
        return 0;
    }
    if (IN(tag, T_APPLET, T_MARQUEE, T_OBJECT)) {
        if (!in_scope(open, tag, 0, SC_DEFAULT)) { html_terr(ARGS); return 0; }
        implied_end_tags(open, -1);
        if (cur_tag(open) != tag) html_terr(ARGS);
        st_pop_until(open, tag);
        afe_clear_to_marker(afe);
        return 0;
    }
    if (tag == T_BR) {
        html_terr(ARGS);
        reconstruct_afe(ARGS);
        insert_implied(ARGS, "br", T_BR);
        st_pop(open);
        c->frameset_ok = 0;
        return 0;
    }
    any_other_end_tag(ARGS, tag, tag == T_UNKNOWN ? tok_atom(d, p, t) : 0);
    return 0;
}

static void clear_to_table_context(vector<html_open> *open, int kind) {
    for (;;) {
        int t = cur_tag(open);
        if (t == T_HTML || t == T_TEMPLATE || t == T_UNKNOWN) return;
        if (kind == 0 && t == T_TABLE) return;
        if (kind == 1 && IN(t, T_TBODY, T_TFOOT, T_THEAD)) return;
        if (kind == 2 && t == T_TR) return;
        if (st_len(open) <= 1) return;
        st_pop(open);
    }
}

static int in_table(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = k == HTML_TOKEN_KIND_START_TAG || k == HTML_TOKEN_KIND_END_TAG ? tk_tag(p, t) : T_UNKNOWN;
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    if (k == HTML_TOKEN_KIND_START_TAG) {
        if (tag == T_CAPTION) { clear_to_table_context(open, 0); afe_push_marker(afe); insert_element(ARGS, t); c->mode = M_IN_CAPTION; return 0; }
        if (tag == T_COLGROUP) { clear_to_table_context(open, 0); insert_element(ARGS, t); c->mode = M_IN_COLUMN_GROUP; return 0; }
        if (tag == T_COL) { clear_to_table_context(open, 0); insert_implied(ARGS, "colgroup", T_COLGROUP); c->mode = M_IN_COLUMN_GROUP; return 1; }
        if (IN(tag, T_TBODY, T_TFOOT, T_THEAD)) { clear_to_table_context(open, 0); insert_element(ARGS, t); c->mode = M_IN_TABLE_BODY; return 0; }
        if (IN(tag, T_TD, T_TH, T_TR)) { clear_to_table_context(open, 0); insert_implied(ARGS, "tbody", T_TBODY); c->mode = M_IN_TABLE_BODY; return 1; }
        if (tag == T_TABLE) {
            html_terr(ARGS);
            if (!in_scope(open, T_TABLE, 0, SC_TABLE)) return 0;
            st_pop_until(open, T_TABLE);
            reset_insertion_mode(ARGS);
            return 1;
        }
        if (IN(tag, T_STYLE, T_SCRIPT, T_TEMPLATE)) return process(ARGS, M_IN_HEAD);
        if (tag == T_INPUT) {
            uint32_t ty = tk_attr(p, t, "type");
            if (ty && tk_attr_value_is_ci(p, ty, "hidden")) { html_terr(ARGS); insert_element(ARGS, t); st_pop(open); return 0; }
        }
        if (tag == T_FORM) {
            html_terr(ARGS);
            if (st_has_tag(open, T_TEMPLATE) || c->form.index) return 0;
            c->form = insert_element(ARGS, t);
            st_pop(open);
            return 0;
        }
    }
    if (k == HTML_TOKEN_KIND_END_TAG) {
        if (tag == T_TABLE) {
            if (!in_scope(open, T_TABLE, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            st_pop_until(open, T_TABLE);
            reset_insertion_mode(ARGS);
            return 0;
        }
        if (IN(tag, T_BODY, T_CAPTION, T_COL, T_COLGROUP, T_HTML, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) { html_terr(ARGS); return 0; }
        if (tag == T_TEMPLATE) return process(ARGS, M_IN_HEAD);
    }
    if (k == HTML_TOKEN_KIND_EOF) return process(ARGS, M_IN_BODY);
    /* anything else: in body, foster parenting */
    html_terr(ARGS);
    c->foster = 1;
    int r = process(ARGS, M_IN_BODY);
    c->foster = 0;
    return r;
}

/* 13.2.6.4.1: the doctype that puts the document in quirks mode */
static int ci_prefix(const char *s, size_t n, const char *w) {
    size_t k = strlen(w);
    if (n < k) return 0;
    for (size_t i = 0; i < k; i++) {
        char a = s[i], b = w[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}
static int ci_equal(const char *s, size_t n, const char *w) { return n == strlen(w) && ci_prefix(s, n, w); }
static int doctype_quirks(const html_parser *p, uint32_t t) {
    static const char *prefixes[] = {
        "+//Silmaril//dtd html Pro v0r11 19970101//", "-//AS//DTD HTML 3.0 asWedit + extensions//",
        "-//AdvaSoft Ltd//DTD HTML 3.0 asWedit + extensions//", "-//IETF//DTD HTML 2.0 Level 1//",
        "-//IETF//DTD HTML 2.0 Level 2//", "-//IETF//DTD HTML 2.0 Strict Level 1//", "-//IETF//DTD HTML 2.0 Strict Level 2//",
        "-//IETF//DTD HTML 2.0 Strict//", "-//IETF//DTD HTML 2.0//", "-//IETF//DTD HTML 2.1E//", "-//IETF//DTD HTML 3.0//",
        "-//IETF//DTD HTML 3.2 Final//", "-//IETF//DTD HTML 3.2//", "-//IETF//DTD HTML 3//", "-//IETF//DTD HTML Level 0//",
        "-//IETF//DTD HTML Level 1//", "-//IETF//DTD HTML Level 2//", "-//IETF//DTD HTML Level 3//",
        "-//IETF//DTD HTML Strict Level 0//", "-//IETF//DTD HTML Strict Level 1//", "-//IETF//DTD HTML Strict Level 2//",
        "-//IETF//DTD HTML Strict Level 3//", "-//IETF//DTD HTML Strict//", "-//IETF//DTD HTML//",
        "-//Metrius//DTD Metrius Presentational//", "-//Microsoft//DTD Internet Explorer 2.0 HTML Strict//",
        "-//Microsoft//DTD Internet Explorer 2.0 HTML//", "-//Microsoft//DTD Internet Explorer 2.0 Tables//",
        "-//Microsoft//DTD Internet Explorer 3.0 HTML Strict//", "-//Microsoft//DTD Internet Explorer 3.0 HTML//",
        "-//Microsoft//DTD Internet Explorer 3.0 Tables//", "-//Netscape Comm. Corp.//DTD HTML//",
        "-//Netscape Comm. Corp.//DTD Strict HTML//", "-//O'Reilly and Associates//DTD HTML 2.0//",
        "-//O'Reilly and Associates//DTD HTML Extended 1.0//", "-//O'Reilly and Associates//DTD HTML Extended Relaxed 1.0//",
        "-//SQ//DTD HTML 2.0 HoTMetaL + extensions//",
        "-//SoftQuad Software//DTD HoTMetaL PRO 6.0::19990601::extensions to HTML 4.0//",
        "-//SoftQuad//DTD HoTMetaL PRO 4.0::19971010::extensions to HTML 4.0//", "-//Spyglass//DTD HTML 2.0 Extended//",
        "-//Sun Microsystems Corp.//DTD HotJava HTML//", "-//Sun Microsystems Corp.//DTD HotJava Strict HTML//",
        "-//W3C//DTD HTML 3 1995-03-24//", "-//W3C//DTD HTML 3.2 Draft//", "-//W3C//DTD HTML 3.2 Final//",
        "-//W3C//DTD HTML 3.2//", "-//W3C//DTD HTML 3.2S Draft//", "-//W3C//DTD HTML 4.0 Frameset//",
        "-//W3C//DTD HTML 4.0 Transitional//", "-//W3C//DTD HTML Experimental 19960712//",
        "-//W3C//DTD HTML Experimental 970421//", "-//W3C//DTD W3 HTML//", "-//W3O//DTD W3 HTML 3.0//",
        "-//WebTechs//DTD Mozilla HTML 2.0//", "-//WebTechs//DTD Mozilla HTML//" };
    uint8_t ids = p->token[t].ids;
    amb_span pu = p->token[t].public_id, sy = p->token[t].system_id;
    const char *pp = bytes_ptr(p, pu);
    if (p->token[t].force_quirks || !(ids & 4) || !tk_name_is(p, t, "html")) return 1;
    if (ids & 1) {
        if (ci_equal(pp, pu.len, "-//W3O//DTD W3 HTML Strict 3.0//EN//") || ci_equal(pp, pu.len, "-/W3C/DTD HTML 4.0 Transitional/EN") ||
            ci_equal(pp, pu.len, "HTML")) return 1;
        for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) if (ci_prefix(pp, pu.len, prefixes[i])) return 1;
        if (!(ids & 2) && (ci_prefix(pp, pu.len, "-//W3C//DTD HTML 4.01 Frameset//") || ci_prefix(pp, pu.len, "-//W3C//DTD HTML 4.01 Transitional//")))
            return 1;
    }
    if ((ids & 2) && ci_equal(bytes_ptr(p, sy), sy.len, "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd")) return 1;
    return 0;
}

static int process(CTX, int mode) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = k == HTML_TOKEN_KIND_START_TAG || k == HTML_TOKEN_KIND_END_TAG ? tk_tag(p, t) : T_UNKNOWN;
    int start = k == HTML_TOKEN_KIND_START_TAG, end = k == HTML_TOKEN_KIND_END_TAG;
    switch (mode) {
    case M_INITIAL:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment_in(ARGS, dom_root(d), t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) {
            amb_span nm = p->token[t].name, pu = p->token[t].public_id, sy = p->token[t].system_id;
            uint8_t ids = p->token[t].ids;
            int named = (ids & 4) != 0;
            if (!named || !tk_name_is(p, t, "html") || (ids & 1) || ((ids & 2) && !(sy.len == 19 && memcmp(bytes_ptr(p, sy), "about:legacy-compat", 19) == 0)))
                html_terr(ARGS);
            dom_node_h dt = dom_create_doctype(d, named ? bytes_ptr(p, nm) : "", named ? nm.len : 0,
                                               bytes_ptr(p, pu), pu.len, bytes_ptr(p, sy), sy.len, (uint8_t)(ids & 3));
            if (!dt.index && named) dt = dom_create_doctype(d, "", 0, bytes_ptr(p, pu), pu.len, bytes_ptr(p, sy), sy.len, (uint8_t)(ids & 3));
            if (dt.index) dom_append_child(d, dom_root(d), dt);
            if (doctype_quirks(p, t)) c->quirks = 1;
            c->mode = M_BEFORE_HTML;
            return 0;
        }
        html_terr(ARGS);
        c->quirks = 1;
        c->mode = M_BEFORE_HTML;
        return 1;
    case M_BEFORE_HTML:
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment_in(ARGS, dom_root(d), t); return 0; }
        if (start && tag == T_HTML) {
            dom_node_h e = create_element(ARGS, t);
            dom_append_child(d, dom_root(d), e);
            push_back(open, open_entry(d, e, T_HTML, t));
            c->mode = M_BEFORE_HEAD;
            return 0;
        }
        if (end && !IN(tag, T_HEAD, T_BODY, T_HTML, T_BR)) { html_terr(ARGS); return 0; }
        insert_html_root(ARGS);
        c->mode = M_BEFORE_HEAD;
        return 1;
    case M_BEFORE_HEAD:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (start && tag == T_HEAD) { c->head = insert_element(ARGS, t); c->mode = M_IN_HEAD; return 0; }
        if (end && !IN(tag, T_HEAD, T_BODY, T_HTML, T_BR)) { html_terr(ARGS); return 0; }
        c->head = insert_implied(ARGS, "head", T_HEAD);
        c->mode = M_IN_HEAD;
        return 1;
    case M_IN_HEAD:
        return in_head(ARGS);
    case M_IN_HEAD_NOSCRIPT:
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (end && tag == T_NOSCRIPT) { st_pop(open); c->mode = M_IN_HEAD; return 0; }
        if (k == HTML_TOKEN_KIND_COMMENT || (start && IN(tag, T_BASEFONT, T_BGSOUND, T_LINK, T_META, T_NOFRAMES, T_STYLE)))
            return process(ARGS, M_IN_HEAD);
        if ((start && IN(tag, T_HEAD, T_NOSCRIPT)) || (end && tag != T_BR)) { html_terr(ARGS); return 0; }
        html_terr(ARGS);
        st_pop(open);
        c->mode = M_IN_HEAD;
        return 1;
    case M_AFTER_HEAD:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (start && tag == T_BODY) { insert_element(ARGS, t); c->frameset_ok = 0; c->mode = M_IN_BODY; return 0; }
        if (start && tag == T_FRAMESET) { insert_element(ARGS, t); c->mode = M_IN_FRAMESET; return 0; }
        if (start && IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META, T_NOFRAMES, T_SCRIPT, T_STYLE, T_TEMPLATE, T_TITLE)) {
            html_terr(ARGS);
            if (c->head.index) push_back(open, open_entry(d, c->head, T_HEAD, 0));
            int r = process(ARGS, M_IN_HEAD);
            if (c->head.index) { int hi = st_index_of_node(open, c->head); if (hi >= 0) st_remove_at(open, (size_t)hi); }
            return r;
        }
        if (end && tag == T_TEMPLATE) return process(ARGS, M_IN_HEAD);
        if ((start && tag == T_HEAD) || (end && !IN(tag, T_BODY, T_HTML, T_BR))) { html_terr(ARGS); return 0; }
        insert_implied(ARGS, "body", T_BODY);
        c->mode = M_IN_BODY;
        return 1;
    case M_IN_BODY:
        return in_body(ARGS);
    case M_TEXT:
        if (k == HTML_TOKEN_KIND_EOF) { html_terr(ARGS); st_pop(open); c->mode = c->orig_mode; return 1; }
        if (end) { st_pop(open); c->mode = c->orig_mode; return 0; }
        return 0;
    case M_IN_TABLE:
        return in_table(ARGS);
    case M_IN_CAPTION:
        if (end && tag == T_CAPTION) {
            if (!in_scope(open, T_CAPTION, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            implied_end_tags(open, -1);
            if (cur_tag(open) != T_CAPTION) html_terr(ARGS);
            st_pop_until(open, T_CAPTION);
            afe_clear_to_marker(afe);
            c->mode = M_IN_TABLE;
            return 0;
        }
        if ((start && IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) || (end && tag == T_TABLE)) {
            if (!in_scope(open, T_CAPTION, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            implied_end_tags(open, -1);
            if (cur_tag(open) != T_CAPTION) html_terr(ARGS);
            st_pop_until(open, T_CAPTION);
            afe_clear_to_marker(afe);
            c->mode = M_IN_TABLE;
            return 1;
        }
        if (end && IN(tag, T_BODY, T_COL, T_COLGROUP, T_HTML, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) { html_terr(ARGS); return 0; }
        return process(ARGS, M_IN_BODY);
    case M_IN_COLUMN_GROUP:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (start && tag == T_COL) { insert_element(ARGS, t); st_pop(open); return 0; }
        if (end && tag == T_COLGROUP) {
            if (cur_tag(open) != T_COLGROUP) { html_terr(ARGS); return 0; }
            st_pop(open); c->mode = M_IN_TABLE; return 0;
        }
        if (end && tag == T_COL) { html_terr(ARGS); return 0; }
        if ((start || end) && tag == T_TEMPLATE) return process(ARGS, M_IN_HEAD);
        if (k == HTML_TOKEN_KIND_EOF) return process(ARGS, M_IN_BODY);
        if (cur_tag(open) != T_COLGROUP) { html_terr(ARGS); return 0; }
        st_pop(open);
        c->mode = M_IN_TABLE;
        return 1;
    case M_IN_TABLE_BODY:
        if (start && tag == T_TR) { clear_to_table_context(open, 1); insert_element(ARGS, t); c->mode = M_IN_ROW; return 0; }
        if (start && IN(tag, T_TH, T_TD)) { html_terr(ARGS); clear_to_table_context(open, 1); insert_implied(ARGS, "tr", T_TR); c->mode = M_IN_ROW; return 1; }
        if (end && IN(tag, T_TBODY, T_TFOOT, T_THEAD)) {
            if (!in_scope(open, tag, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            clear_to_table_context(open, 1); st_pop(open); c->mode = M_IN_TABLE; return 0;
        }
        if ((start && IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_TBODY, T_TFOOT, T_THEAD)) || (end && tag == T_TABLE)) {
            if (!in_scope(open, T_TBODY, 0, SC_TABLE) && !in_scope(open, T_THEAD, 0, SC_TABLE) && !in_scope(open, T_TFOOT, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            clear_to_table_context(open, 1); st_pop(open); c->mode = M_IN_TABLE; return 1;
        }
        if (end && IN(tag, T_BODY, T_CAPTION, T_COL, T_COLGROUP, T_HTML, T_TD, T_TH, T_TR)) { html_terr(ARGS); return 0; }
        return in_table(ARGS);
    case M_IN_ROW:
        if (start && IN(tag, T_TH, T_TD)) { clear_to_table_context(open, 2); insert_element(ARGS, t); c->mode = M_IN_CELL; afe_push_marker(afe); return 0; }
        if (end && tag == T_TR) {
            if (!in_scope(open, T_TR, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            clear_to_table_context(open, 2); st_pop(open); c->mode = M_IN_TABLE_BODY; return 0;
        }
        if ((start && IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_TBODY, T_TFOOT, T_THEAD, T_TR)) || (end && tag == T_TABLE)) {
            if (!in_scope(open, T_TR, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            clear_to_table_context(open, 2); st_pop(open); c->mode = M_IN_TABLE_BODY; return 1;
        }
        if (end && IN(tag, T_TBODY, T_TFOOT, T_THEAD)) {
            if (!in_scope(open, tag, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            if (!in_scope(open, T_TR, 0, SC_TABLE)) return 0;
            clear_to_table_context(open, 2); st_pop(open); c->mode = M_IN_TABLE_BODY; return 1;
        }
        if (end && IN(tag, T_BODY, T_CAPTION, T_COL, T_COLGROUP, T_HTML, T_TD, T_TH)) { html_terr(ARGS); return 0; }
        return in_table(ARGS);
    case M_IN_CELL:
        if (end && IN(tag, T_TD, T_TH)) {
            if (!in_scope(open, tag, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            implied_end_tags(open, -1);
            if (cur_tag(open) != tag) html_terr(ARGS);
            st_pop_until(open, tag);
            afe_clear_to_marker(afe);
            c->mode = M_IN_ROW;
            return 0;
        }
        if (start && IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) {
            if (!in_scope(open, T_TD, 0, SC_TABLE) && !in_scope(open, T_TH, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            int cell = in_scope(open, T_TD, 0, SC_TABLE) ? T_TD : T_TH;
            implied_end_tags(open, -1);
            if (cur_tag(open) != cell) html_terr(ARGS);
            st_pop_until(open, cell);
            afe_clear_to_marker(afe);
            c->mode = M_IN_ROW;
            return 1;
        }
        if (end && IN(tag, T_BODY, T_CAPTION, T_COL, T_COLGROUP, T_HTML)) { html_terr(ARGS); return 0; }
        if (end && IN(tag, T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR)) {
            if (!in_scope(open, tag, 0, SC_TABLE)) { html_terr(ARGS); return 0; }
            int cell = in_scope(open, T_TD, 0, SC_TABLE) ? T_TD : T_TH;
            implied_end_tags(open, -1);
            if (cur_tag(open) != cell) html_terr(ARGS);
            st_pop_until(open, cell);
            afe_clear_to_marker(afe);
            c->mode = M_IN_ROW;
            return 1;
        }
        return process(ARGS, M_IN_BODY);
    case M_IN_SELECT:
    case M_IN_SELECT_IN_TABLE:
        if (mode == M_IN_SELECT_IN_TABLE) {
            if (start && IN(tag, T_CAPTION, T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR, T_TD, T_TH)) {
                html_terr(ARGS); st_pop_until(open, T_SELECT); reset_insertion_mode(ARGS); return 1;
            }
            if (end && IN(tag, T_CAPTION, T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR, T_TD, T_TH)) {
                html_terr(ARGS);
                if (!in_scope(open, tag, 0, SC_TABLE)) return 0;
                st_pop_until(open, T_SELECT); reset_insertion_mode(ARGS); return 1;
            }
        }
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (start && tag == T_OPTION) { if (cur_tag(open) == T_OPTION) st_pop(open); insert_element(ARGS, t); return 0; }
        if (start && tag == T_OPTGROUP) {
            if (cur_tag(open) == T_OPTION) st_pop(open);
            if (cur_tag(open) == T_OPTGROUP) st_pop(open);
            insert_element(ARGS, t);
            return 0;
        }
        if (start && tag == T_HR) {
            if (cur_tag(open) == T_OPTION) st_pop(open);
            if (cur_tag(open) == T_OPTGROUP) st_pop(open);
            insert_element(ARGS, t); st_pop(open);
            return 0;
        }
        if (end && tag == T_OPTGROUP) {
            if (cur_tag(open) == T_OPTION && st_len(open) >= 2 && (int)st_at(open, st_len(open) - 2).tag == T_OPTGROUP) st_pop(open);
            if (cur_tag(open) == T_OPTGROUP) st_pop(open); else html_terr(ARGS);
            return 0;
        }
        if (end && tag == T_OPTION) { if (cur_tag(open) == T_OPTION) st_pop(open); else html_terr(ARGS); return 0; }
        if (end && tag == T_SELECT) {
            if (!in_scope(open, T_SELECT, 0, SC_SELECT)) { html_terr(ARGS); return 0; }
            st_pop_until(open, T_SELECT); reset_insertion_mode(ARGS); return 0;
        }
        if (start && tag == T_SELECT) {
            html_terr(ARGS);
            if (!in_scope(open, T_SELECT, 0, SC_SELECT)) return 0;
            st_pop_until(open, T_SELECT); reset_insertion_mode(ARGS); return 0;
        }
        if (start && IN(tag, T_INPUT, T_KEYGEN, T_TEXTAREA)) {
            html_terr(ARGS);
            if (!in_scope(open, T_SELECT, 0, SC_SELECT)) return 0;
            st_pop_until(open, T_SELECT); reset_insertion_mode(ARGS); return 1;
        }
        if ((start && IN(tag, T_SCRIPT, T_TEMPLATE)) || (end && tag == T_TEMPLATE)) return process(ARGS, M_IN_HEAD);
        if (k == HTML_TOKEN_KIND_EOF) return process(ARGS, M_IN_BODY);
        html_terr(ARGS);
        return 0;
    case M_AFTER_BODY:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment_in(ARGS, st_at(open, 0).node, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (end && tag == T_HTML) { if (c->fragment) { html_terr(ARGS); return 0; } c->mode = M_AFTER_AFTER_BODY; return 0; }
        if (k == HTML_TOKEN_KIND_EOF) { c->stopped = 1; return 0; }
        html_terr(ARGS);
        c->mode = M_IN_BODY;
        return 1;
    case M_IN_FRAMESET:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (start && tag == T_FRAMESET) { insert_element(ARGS, t); return 0; }
        if (end && tag == T_FRAMESET) {
            if (cur_tag(open) == T_HTML) { html_terr(ARGS); return 0; }
            st_pop(open);
            if (!c->fragment && cur_tag(open) != T_FRAMESET) c->mode = M_AFTER_FRAMESET;
            return 0;
        }
        if (start && tag == T_FRAME) { insert_element(ARGS, t); st_pop(open); return 0; }
        if (start && tag == T_NOFRAMES) return process(ARGS, M_IN_HEAD);
        if (k == HTML_TOKEN_KIND_EOF) { if (cur_tag(open) != T_HTML) html_terr(ARGS); c->stopped = 1; return 0; }
        html_terr(ARGS);
        return 0;
    case M_AFTER_FRAMESET:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
        if (start && tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (end && tag == T_HTML) { c->mode = M_AFTER_AFTER_FRAMESET; return 0; }
        if (start && tag == T_NOFRAMES) return process(ARGS, M_IN_HEAD);
        if (k == HTML_TOKEN_KIND_EOF) { c->stopped = 1; return 0; }
        html_terr(ARGS);
        return 0;
    case M_AFTER_AFTER_BODY:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment_in(ARGS, dom_root(d), t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE || (start && tag == T_HTML)) return process(ARGS, M_IN_BODY);
        if (k == HTML_TOKEN_KIND_EOF) { c->stopped = 1; return 0; }
        html_terr(ARGS);
        c->mode = M_IN_BODY;
        return 1;
    case M_AFTER_AFTER_FRAMESET:
        if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment_in(ARGS, dom_root(d), t); return 0; }
        if (k == HTML_TOKEN_KIND_DOCTYPE || (start && tag == T_HTML)) return process(ARGS, M_IN_BODY);
        if (k == HTML_TOKEN_KIND_EOF) { c->stopped = 1; return 0; }
        if (start && tag == T_NOFRAMES) return process(ARGS, M_IN_HEAD);
        html_terr(ARGS);
        return 0;
    default:
        return 0;
    }
}

/* every token of the table, in order, through the modes */
static void run_tokens(CTX) {
    for (uint32_t t = 1; t < p->token_len && !c->stopped; t++) {
        if (!p->token[t].live) continue;
        c->tok = t;
        if (tk_kind(p, t) == HTML_TOKEN_KIND_CHARACTERS) {
            amb_span s = p->token[t].data;
            vector<char> text;
            append_range(text, bytes_ptr(p, s), s.len);
            characters(ARGS, data(text), len(text));
            continue;
        }
        flush_table_text(ARGS);
        if (tk_kind(p, t) != HTML_TOKEN_KIND_CHARACTERS) c->skip_lf = 0;
        for (int guard = 0; guard < 64 && process(ARGS, c->mode); guard++) {}
    }
}

/* the builder's parse errors, as records of the store */
static void record_errors(html_parser *p, const vector<uint32_t> *errs) {
    for (size_t i = 0; i < len(*errs); i++) {
        uint32_t t = at(*errs, i);
        uint32_t line = t && t < p->token_len ? p->token[t].line : 0, col = t && t < p->token_len ? p->token[t].col : 0;
        html_error_h e = error_new(p);
        p->error[e.index].code = HTML_ERROR_CODE_TREE_CONSTRUCTION;
        p->error[e.index].line = line;
        p->error[e.index].col = col;
    }
}

static void init_ctx(html_ctx *c) {
    memset(c, 0, sizeof *c);
    c->mode = M_INITIAL;
    c->frameset_ok = 1;
}

html_status html_build(html_parser *p, dom_document *d) {
    html_ctx ctx;
    vector<html_open> open, afe;
    vector<char> pend;
    vector<uint32_t> errs;
    init_ctx(&ctx);
    run_tokens(d, p, &ctx, &open, &afe, &pend, &errs);
    record_errors(p, &errs);
    return HTML_OK;
}

html_status html_tokenize(html_parser *p, const char *src, size_t src_len) {
    return html_tokenize_in(p, src, src_len, 0, "", 0);
}

html_status html_parse(html_parser *p, dom_document *doc, const char *src, size_t src_len) {
    html_status st = html_tokenize(p, src, src_len);
    if (st != HTML_OK) return st;
    return html_build(p, doc);
}

/* 13.4: the fragment parsing algorithm, the context an element of that name */
html_status html_parse_fragment(html_parser *p, dom_document *d, const char *context, size_t context_len,
                                const char *src, size_t src_len) {
    int ctx_tag = html_tag_of(context, context_len);
    int state = 0;
    if (ctx_tag == T_TITLE || ctx_tag == T_TEXTAREA) state = 1;
    else if (IN(ctx_tag, T_STYLE, T_XMP, T_IFRAME, T_NOEMBED, T_NOFRAMES)) state = 2;
    else if (ctx_tag == T_SCRIPT) state = 3;
    else if (ctx_tag == T_PLAINTEXT) state = 4;
    html_status st = html_tokenize_in(p, src, src_len, state, context, context_len);
    if (st != HTML_OK) return st;
    html_ctx ctx;
    vector<html_open> open, afe;
    vector<char> pend;
    vector<uint32_t> errs;
    init_ctx(&ctx);
    ctx.fragment = 1;
    ctx.ctx_tag = ctx_tag;
    /* the root: an html element, its children the fragment's */
    dom_node_h root = dom_create_element(d, "html", 4);
    push_back(open, open_entry(d, root, T_HTML, 0));
    if (ctx_tag == T_FORM) ctx.form = root;
    reset_insertion_mode(d, p, &ctx, &open, &afe, &pend, &errs);
    run_tokens(d, p, &ctx, &open, &afe, &pend, &errs);
    record_errors(p, &errs);
    /* the fragment's nodes become the document node's children */
    for (dom_node_h ch = dom_children_first(d, root); ch.index; ch = dom_children_first(d, root)) {
        dom_remove_child(d, root, ch);
        dom_append_child(d, dom_root(d), ch);
    }
    dom_destroy(d, root);
    return HTML_OK;
}
