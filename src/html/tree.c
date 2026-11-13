/* src/html/tree.c -- the WHATWG tree construction (HTML 13.2.6) and the fragment
   parsing algorithm (13.4), from the token table of the HTML module's store to a
   DOM document; and the HTML module's generated implementation.

   The tree builder speaks to the DOM through its operations and its traversal
   only (dom_create_element, dom_append_child, dom_insert_before, dom_children_*
   ...): the parser's output contract is the DOM's contract, never its storage
   (the architecture's §7). The token table is read as const; the tree builder's
   own parse errors are gathered apart and recorded when the tree is built, so
   no borrow of the store outlives a change to it.

   The tree builder drives the tokenizer (src/html/tokenizer.h). A run of the
   tokenizer stops after the start tag of an element whose text may be raw, and
   at <![CDATA[; the tree builder builds the tokens up to there and says how the
   tokenizer goes on: the state it chose for the start tag, and whether the
   adjusted current node is foreign, which makes a CDATA section.

   Foreign content: SVG and MathML elements are made in their namespaces, by
   the tree construction dispatcher and the rules for parsing tokens in foreign
   content (13.2.6.5): the adjusted current node (the fragment's context element
   where it stands for the stack), the MathML text integration points and the
   HTML integration points, the breakout start tags, the adjusted SVG tag names,
   the SVG, MathML and foreign attribute adjustments, self-closing foreign
   elements, the foreign elements of the special category and of the scopes.
   Template contents: a template's children go to its contents, the fragment
   the DOM makes with it, by the appropriate place for inserting a node; the
   "in template" insertion mode and the stack of template insertion modes. A template
   whose shadowrootmode is open or closed (in any case), in a document parsed whole (not a
   fragment: innerHTML's), whose adjusted current node may host a shadow root and hosts none,
   is a declarative shadow root (13.2.6.4.4): the root attached to that node with the
   template's shadowrootdelegatesfocus, shadowrootclonable and shadowrootserializable, and the
   template pushed onto the stack alone, its contents the root, never in the document; any
   other is a template as before. The
   select element is parsed by the rules of 2025: no "in select" insertion
   modes, select in the default scope, and the start tags of input, select,
   option, optgroup and hr closing or keeping what a select holds.

   The document's mode (13.2.6.4.1): quirks mode when the DOCTYPE asks for it (its force-quirks
   flag, a name other than html, the public and system identifiers the standard lists) or
   there is none; limited-quirks mode for XHTML 1.0's transitional and frameset identifiers
   and HTML 4.01's with a system identifier; kept on the document (dom_set_mode). A fragment
   is parsed in quirks mode when its document is. An empty system identifier is a missing
   one, as Blink takes it (the standard has it there: HTML 4.01's transitional and frameset
   identifiers with it are quirks mode in Chromium, limited-quirks mode by the standard). An
   iframe srcdoc document (html_set_srcdoc) is never in quirks mode by its DOCTYPE or its
   lack of one (Phase 12u).

   The scripting flag is off unless html_set_scripting turns it on (a page whose scripts
   run): noscript's content is then raw text. A document parsed whole has its radio
   button groups as Chromium's insertions leave them (src/html/forms.c: each group's last
   checked radio its checked one). Everything else follows the standard's steps, by its
   names. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_implementation(html);
#include "tokenizer.h"
#include "../dom/dom.h"
#include "forms.h"

/* the scripting flag (13.2.3.5): the parser of a document whose scripts run */
void html_set_scripting(int on) { html_scripting_flag = on != 0; }
/* an iframe srcdoc document's parse (13.2.6.4.1): its DOCTYPE, or the lack of one, sets no mode */
static int html_srcdoc_flag = 0;
void html_set_srcdoc(int on) { html_srcdoc_flag = on != 0; }

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
    T_U, T_UL, T_VAR, T_WBR, T_XMP,
    T_MARKER,      /* a marker of the list of active formatting elements */
    T_FOREIGN      /* an element of the SVG or MathML namespace: no HTML element's tag */
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
    "u", "ul", "var", "wbr", "xmp"
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

/* the special category's HTML elements (13.2.4.2) */
static int html_special_tag(int t) {
    switch (t) {
    case T_ADDRESS: case T_APPLET: case T_AREA: case T_ARTICLE: case T_ASIDE: case T_BASE: case T_BASEFONT:
    case T_BGSOUND: case T_BLOCKQUOTE: case T_BODY: case T_BR: case T_BUTTON: case T_CAPTION: case T_CENTER:
    case T_COL: case T_COLGROUP: case T_DD: case T_DETAILS: case T_DIR: case T_DIV: case T_DL: case T_DT:
    case T_EMBED: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_FORM: case T_FRAME:
    case T_FRAMESET: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEAD: case T_HEADER:
    case T_HGROUP: case T_HR: case T_HTML: case T_IFRAME: case T_IMG: case T_INPUT: case T_KEYGEN: case T_LI:
    case T_LINK: case T_LISTING: case T_MAIN: case T_MARQUEE: case T_MENU: case T_META: case T_NAV: case T_NOEMBED:
    case T_NOFRAMES: case T_NOSCRIPT: case T_OBJECT: case T_OL: case T_P: case T_PARAM: case T_PLAINTEXT: case T_PRE:
    case T_SCRIPT: case T_SEARCH: case T_SECTION: case T_SELECT: case T_SOURCE: case T_STYLE: case T_SUMMARY:
    case T_TABLE: case T_TBODY: case T_TD: case T_TEMPLATE: case T_TEXTAREA: case T_TFOOT: case T_TH: case T_THEAD:
    case T_TITLE: case T_TR: case T_TRACK: case T_UL: case T_WBR: case T_XMP:
        return 1;
    default:
        return 0;
    }
}
/* the HTML elements that end the default scope (13.2.4.2: has an element in scope) */
static int html_scope_tag(int t) {
    switch (t) {
    case T_APPLET: case T_CAPTION: case T_HTML: case T_TABLE: case T_TD: case T_TH: case T_MARQUEE: case T_OBJECT:
    case T_SELECT: case T_TEMPLATE:
        return 1;
    default:
        return 0;
    }
}
static int html_formatting(int t) {
    return IN(t, T_A, T_B, T_BIG, T_CODE, T_EM, T_FONT, T_I, T_NOBR, T_S, T_SMALL, T_STRIKE, T_STRONG, T_TT, T_U);
}
static int html_heading(int t) { return IN(t, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6); }

/* the start tags that break out of foreign content (13.2.6.5) */
static int html_breakout(int t) {
    switch (t) {
    case T_B: case T_BIG: case T_BLOCKQUOTE: case T_BODY: case T_BR: case T_CENTER: case T_CODE: case T_DD: case T_DIV:
    case T_DL: case T_DT: case T_EM: case T_EMBED: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6:
    case T_HEAD: case T_HR: case T_I: case T_IMG: case T_LI: case T_LISTING: case T_MENU: case T_META: case T_NOBR:
    case T_OL: case T_P: case T_PRE: case T_RUBY: case T_S: case T_SMALL: case T_SPAN: case T_STRONG: case T_STRIKE:
    case T_SUB: case T_SUP: case T_TABLE: case T_TT: case T_U: case T_UL: case T_VAR:
        return 1;
    default:
        return 0;
    }
}

/* ---- the adjustments of foreign elements' names (13.2.6.1, 13.2.6.5) ---- */

/* each pair: the token's name, the SVG element's */
static const char *const svg_tag_names[] = {
    "altglyph", "altGlyph", "altglyphdef", "altGlyphDef", "altglyphitem", "altGlyphItem",
    "animatecolor", "animateColor", "animatemotion", "animateMotion", "animatetransform", "animateTransform",
    "clippath", "clipPath", "feblend", "feBlend", "fecolormatrix", "feColorMatrix",
    "fecomponenttransfer", "feComponentTransfer", "fecomposite", "feComposite", "feconvolvematrix", "feConvolveMatrix",
    "fediffuselighting", "feDiffuseLighting", "fedisplacementmap", "feDisplacementMap", "fedistantlight", "feDistantLight",
    "fedropshadow", "feDropShadow", "feflood", "feFlood", "fefunca", "feFuncA", "fefuncb", "feFuncB",
    "fefuncg", "feFuncG", "fefuncr", "feFuncR", "fegaussianblur", "feGaussianBlur", "feimage", "feImage",
    "femerge", "feMerge", "femergenode", "feMergeNode", "femorphology", "feMorphology", "feoffset", "feOffset",
    "fepointlight", "fePointLight", "fespecularlighting", "feSpecularLighting", "fespotlight", "feSpotLight",
    "fetile", "feTile", "feturbulence", "feTurbulence", "foreignobject", "foreignObject", "glyphref", "glyphRef",
    "lineargradient", "linearGradient", "radialgradient", "radialGradient", "textpath", "textPath"
};
/* each pair: the token's attribute name, the SVG element's */
static const char *const svg_attr_names[] = {
    "attributename", "attributeName", "attributetype", "attributeType", "basefrequency", "baseFrequency",
    "baseprofile", "baseProfile", "calcmode", "calcMode", "clippathunits", "clipPathUnits",
    "diffuseconstant", "diffuseConstant", "edgemode", "edgeMode", "filterunits", "filterUnits",
    "glyphref", "glyphRef", "gradienttransform", "gradientTransform", "gradientunits", "gradientUnits",
    "kernelmatrix", "kernelMatrix", "kernelunitlength", "kernelUnitLength", "keypoints", "keyPoints",
    "keysplines", "keySplines", "keytimes", "keyTimes", "lengthadjust", "lengthAdjust",
    "limitingconeangle", "limitingConeAngle", "markerheight", "markerHeight", "markerunits", "markerUnits",
    "markerwidth", "markerWidth", "maskcontentunits", "maskContentUnits", "maskunits", "maskUnits",
    "numoctaves", "numOctaves", "pathlength", "pathLength", "patterncontentunits", "patternContentUnits",
    "patterntransform", "patternTransform", "patternunits", "patternUnits", "pointsatx", "pointsAtX",
    "pointsaty", "pointsAtY", "pointsatz", "pointsAtZ", "preservealpha", "preserveAlpha",
    "preserveaspectratio", "preserveAspectRatio", "primitiveunits", "primitiveUnits", "refx", "refX",
    "refy", "refY", "repeatcount", "repeatCount", "repeatdur", "repeatDur",
    "requiredextensions", "requiredExtensions", "requiredfeatures", "requiredFeatures",
    "specularconstant", "specularConstant", "specularexponent", "specularExponent", "spreadmethod", "spreadMethod",
    "startoffset", "startOffset", "stddeviation", "stdDeviation", "stitchtiles", "stitchTiles",
    "surfacescale", "surfaceScale", "systemlanguage", "systemLanguage", "tablevalues", "tableValues",
    "targetx", "targetX", "targety", "targetY", "textlength", "textLength", "viewbox", "viewBox",
    "viewtarget", "viewTarget", "xchannelselector", "xChannelSelector", "ychannelselector", "yChannelSelector",
    "zoomandpan", "zoomAndPan"
};

#define SVG_TAG_PAIRS (sizeof svg_tag_names / sizeof svg_tag_names[0])
#define SVG_ATTR_PAIRS (sizeof svg_attr_names / sizeof svg_attr_names[0])

/* the index of the second of the pair whose first is s[0..n), or 0 */
static size_t html_adjusted(const char *const *pairs, size_t count, const char *s, size_t n) {
    for (size_t i = 0; i + 1 < count; i += 2)
        if (strlen(pairs[i]) == n && memcmp(pairs[i], s, n) == 0) return i + 1;
    return 0;
}

/* the namespace of a foreign attribute of this name (adjust foreign attributes),
   or none: its name is its qualified name, prefix and all */
static dom_attr_namespace html_foreign_attr_ns(const char *s, size_t n) {
    static const char *const xlink[] = { "xlink:actuate", "xlink:arcrole", "xlink:href", "xlink:role", "xlink:show",
                                         "xlink:title", "xlink:type" };
    for (size_t i = 0; i < sizeof xlink / sizeof xlink[0]; i++)
        if (strlen(xlink[i]) == n && memcmp(xlink[i], s, n) == 0) return DOM_ATTR_NAMESPACE_XLINK;
    if ((n == 8 && memcmp(s, "xml:lang", 8) == 0) || (n == 9 && memcmp(s, "xml:space", 9) == 0)) return DOM_ATTR_NAMESPACE_XML;
    if ((n == 5 && memcmp(s, "xmlns", 5) == 0) || (n == 11 && memcmp(s, "xmlns:xlink", 11) == 0)) return DOM_ATTR_NAMESPACE_XMLNS;
    return DOM_ATTR_NAMESPACE_NULL;
}

/* ---- the builder's state -------------------------------------------------------- */

enum html_mode {
    M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_IN_HEAD_NOSCRIPT, M_AFTER_HEAD, M_IN_BODY, M_TEXT,
    M_IN_TABLE, M_IN_TABLE_TEXT, M_IN_CAPTION, M_IN_COLUMN_GROUP, M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL,
    M_IN_TEMPLATE, M_AFTER_BODY, M_IN_FRAMESET, M_AFTER_FRAMESET, M_AFTER_AFTER_BODY, M_AFTER_AFTER_FRAMESET
};

/* what an open element is to the algorithm, found once when it is pushed */
enum {
    F_SCOPE = 1,            /* it ends the default scope, and the list item and button scopes */
    F_TABLE_SCOPE = 2,      /* it ends the table scope: html, table, template */
    F_SPECIAL = 4,          /* the special category */
    F_MATHML_TIP = 8,       /* a MathML text integration point: mi, mo, mn, ms, mtext */
    F_HTML_IP = 16,         /* an HTML integration point: SVG foreignObject, desc, title; MathML annotation-xml of an HTML encoding */
    F_ANNOTATION_XML = 32   /* MathML annotation-xml */
};

/* an entry of the stack of open elements or of the list of active formatting
   elements: the node, its tag (T_FOREIGN for a foreign element), its name's atom in
   the DOM, the token it was made for, its namespace and what it is to the algorithm */
typedef struct html_open { dom_node_h node; uint32_t tag; uint32_t atom; uint32_t token; uint8_t ns; uint8_t flags; } html_open;

/* the tree builder: its scalars and its lists. Its owner drops the lists
   (HTML_TB_DROP: a struct's vectors are not autodropped) */
typedef struct html_tb {
    int mode, orig_mode;
    dom_node_h head, form;
    int frameset_ok, foster, quirks, fragment, skip_lf, stopped;
    int ctx_tag, ctx_ns, ctx_flags;     /* the fragment's context element: its tag, its namespace, what it is */
    int tz_state;                       /* the tokenizer's state the start tag just built asks for */
    uint32_t tok;                       /* the token being processed ... */
    int tag;                            /* ... and its tag */
    uint32_t next;                      /* the next token to build */
    int track_opts;                     /* a selectedcontent has been made: the options are followed */
    int opt_cut;                        /* an option has left the stack from below its top */
    vector<html_open> l_open;           /* the stack of open elements */
    vector<html_open> l_afe;            /* the list of active formatting elements */
    vector<char> l_pend;                /* the pending table characters */
    vector<uint32_t> l_errs;            /* the builder's own parse errors: the tokens they are at */
    vector<int> l_tmodes;               /* the stack of template insertion modes */
    vector<uint32_t> l_opts;            /* the options pushed and not yet seen popped: pairs of node and stack index */
} html_tb;
#define HTML_TB_DROP(b) vector_html_open_drop(&(b).l_open); vector_html_open_drop(&(b).l_afe); vector_char_drop(&(b).l_pend); \
    vector_uint32_t_drop(&(b).l_errs); vector_int_drop(&(b).l_tmodes); vector_uint32_t_drop(&(b).l_opts)

/* every routine takes the same context: the document, the token table and the
   builder, whose lists it names open, afe, pend, errs, tmodes and opts */
#define CTX dom_document *d, const html_parser *p, html_tb *c
#define ARGS d, p, c
#define open (&c->l_open)
#define afe (&c->l_afe)
#define pend (&c->l_pend)
#define errs (&c->l_errs)
#define tmodes (&c->l_tmodes)
#define opts (&c->l_opts)

static void html_terr(CTX) { push_back(errs, c->tok); }

/* the document set to quirks mode (13.2.6.4.1): the builder's flag, and the mode kept on
   the document, where CSS and layout read it */
static void set_quirks(CTX) {
    c->quirks = 1;
    dom_set_mode(d, DOM_DOCUMENT_MODE_QUIRKS);
}

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

/* 13.2.4.2: has an element in the specific scope. An HTML element is named by
   its tag; one the algorithm does not name (T_UNKNOWN), by its atom */
enum { SC_DEFAULT, SC_LIST_ITEM, SC_BUTTON, SC_TABLE };
static int html_scope_boundary(html_open e, int kind) {
    if (kind == SC_TABLE) return (e.flags & F_TABLE_SCOPE) != 0;
    if (e.flags & F_SCOPE) return 1;
    if (kind == SC_LIST_ITEM && (e.tag == T_OL || e.tag == T_UL)) return 1;
    if (kind == SC_BUTTON && e.tag == T_BUTTON) return 1;
    return 0;
}
static int in_scope(const vector<html_open> *s, int t, uint32_t atom, int kind) {
    for (size_t i = len(*s); i-- > 0;) {
        html_open e = at(*s, i);
        if (t != T_UNKNOWN ? (int)e.tag == t : (e.tag == T_UNKNOWN && e.atom == atom)) return 1;
        if (html_scope_boundary(e, kind)) return 0;
    }
    return 0;
}
/* the node itself in the default scope */
static int node_in_scope(const vector<html_open> *s, dom_node_h n) {
    for (size_t i = len(*s); i-- > 0;) {
        html_open e = at(*s, i);
        if (e.node.index == n.index) return 1;
        if (html_scope_boundary(e, SC_DEFAULT)) return 0;
    }
    return 0;
}
static int heading_in_scope(const vector<html_open> *s) {
    for (size_t i = len(*s); i-- > 0;) {
        html_open e = at(*s, i);
        if (html_heading((int)e.tag)) return 1;
        if (html_scope_boundary(e, SC_DEFAULT)) return 0;
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
   before (none: at the end). Foster parenting puts it before the last table, or
   into the last template if that is later; a template's place is its contents */
static void insertion_place(CTX, dom_node_h target, dom_node_h *parent, dom_node_h *before) {
    dom_node_h none = { 0, 0, 0 };
    *before = none;
    *parent = target;
    if (c->foster) {
        int tt = -1;
        int ti = st_index_of_node(open, target);
        if (ti >= 0) tt = (int)st_at(open, (size_t)ti).tag;
        if (IN(tt, T_TABLE, T_TBODY, T_TFOOT, T_THEAD, T_TR)) {
            int last = -1;
            for (size_t i = st_len(open); i-- > 0;) {
                int lt = (int)st_at(open, i).tag;
                if (lt == T_TABLE || lt == T_TEMPLATE) { last = (int)i; break; }
            }
            if (last < 0) { *parent = st_at(open, 0).node; return; }
            dom_node_h lnode = st_at(open, (size_t)last).node;
            if ((int)st_at(open, (size_t)last).tag == T_TEMPLATE) *parent = lnode;
            else {
                dom_node_h tp = dom_children_up(d, lnode);
                if (tp.index) { *parent = tp; *before = lnode; }
                else *parent = st_at(open, (size_t)last - 1).node;
            }
        }
    }
    uint32_t contents = d->node[parent->index].content;
    if (contents) { *parent = node_handle(d, contents); *before = none; }
    /* past 512 open elements a node goes beside the current one, not into it -- a
       template's contents' beside the template: the DOM's depth stays bounded, as
       Blink bounds it (kMaximumHTMLParserDOMTreeDepth), and so does every walk of it
       that recurses */
    if (st_len(open) > 512 && !before->index) {
        uint32_t x = parent->index;
        uint32_t up = d->node[x].parent;
        if (!up && d->node[x].host) up = d->node[d->node[x].host].parent;
        if (up) *parent = node_handle(d, up);
    }
}

static void insert_node_at(CTX, dom_node_h parent, dom_node_h before, dom_node_h node) {
    if (before.index) dom_insert_before(d, parent, node, before);
    else dom_append_child(d, parent, node);
}

static dom_node_h current_node(const vector<html_open> *s) { return st_top(s).node; }

/* the adjusted current node: the fragment's context element while the stack
   holds the root alone, else the current node */
static html_open adjusted_current(const html_tb *c) {
    if (c->fragment && st_len(open) == 1) {
        html_open e;
        memset(&e, 0, sizeof e);
        e.tag = (uint32_t)c->ctx_tag;
        e.ns = (uint8_t)c->ctx_ns;
        e.flags = (uint8_t)c->ctx_flags;
        return e;
    }
    return st_top(open);
}

/* an element for the token, in its namespace, with its attributes; a foreign
   element's name and attributes adjusted (13.2.6.1, 13.2.6.5) */
static dom_node_h create_element_ns(CTX, uint32_t t, int ns) {
    amb_span n = p->token[t].name;
    dom_node_h e;
    if (ns == DOM_NAMESPACE_HTML) e = dom_create_element(d, bytes_ptr(p, n), n.len);
    else {
        size_t k = ns == DOM_NAMESPACE_SVG ? html_adjusted(svg_tag_names, SVG_TAG_PAIRS, bytes_ptr(p, n), n.len) : 0;
        if (k) e = dom_create_element_ns(d, (dom_namespace)ns, svg_tag_names[k], strlen(svg_tag_names[k]));
        else e = dom_create_element_ns(d, (dom_namespace)ns, bytes_ptr(p, n), n.len);
    }
    for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
        amb_span an = p->tattr[a].name, av = p->tattr[a].value;
        if (ns == DOM_NAMESPACE_HTML) { dom_set_attribute(d, e, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len); continue; }
        dom_attr_namespace ans = html_foreign_attr_ns(bytes_ptr(p, an), an.len);
        size_t k = ns == DOM_NAMESPACE_SVG ? html_adjusted(svg_attr_names, SVG_ATTR_PAIRS, bytes_ptr(p, an), an.len) : 0;
        if (k) dom_set_attribute_ns(d, e, ans, svg_attr_names[k], strlen(svg_attr_names[k]), bytes_ptr(p, av), av.len);
        else if (ns == DOM_NAMESPACE_MATHML && an.len == 13 && memcmp(bytes_ptr(p, an), "definitionurl", 13) == 0)
            dom_set_attribute_ns(d, e, ans, "definitionURL", 13, bytes_ptr(p, av), av.len);
        else dom_set_attribute_ns(d, e, ans, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
    }
    return e;
}
static dom_node_h create_element(CTX, uint32_t t) { return create_element_ns(ARGS, t, DOM_NAMESPACE_HTML); }

/* what a foreign element of this namespace and (adjusted) name is to the
   algorithm; html_ip, that its start tag made annotation-xml an HTML integration point */
static uint8_t foreign_flags(int ns, const char *s, size_t n, int html_ip) {
#define NAMED(w) (n == sizeof(w) - 1 && memcmp(s, w, n) == 0)
    if (ns == DOM_NAMESPACE_MATHML) {
        if (NAMED("mi") || NAMED("mo") || NAMED("mn") || NAMED("ms") || NAMED("mtext")) return F_SCOPE | F_SPECIAL | F_MATHML_TIP;
        if (NAMED("annotation-xml")) return (uint8_t)(F_SCOPE | F_SPECIAL | F_ANNOTATION_XML | (html_ip ? F_HTML_IP : 0));
    } else if (ns == DOM_NAMESPACE_SVG) {
        if (NAMED("foreignObject") || NAMED("desc") || NAMED("title")) return F_SCOPE | F_SPECIAL | F_HTML_IP;
    }
    return 0;
#undef NAMED
}
/* what an HTML element of this tag is to the algorithm */
static uint8_t html_flags(int tag) {
    uint8_t f = 0;
    if (html_scope_tag(tag)) f |= F_SCOPE;
    if (tag == T_HTML || tag == T_TABLE || tag == T_TEMPLATE) f |= F_TABLE_SCOPE;
    if (html_special_tag(tag)) f |= F_SPECIAL;
    return f;
}

static html_open open_entry(const dom_document *d, dom_node_h e, int tag, uint32_t t) {
    html_open o;
    o.node = e;
    o.tag = (uint32_t)tag;
    o.atom = d->node[e.index].name;
    o.token = t;
    o.ns = DOM_NAMESPACE_HTML;
    o.flags = html_flags(tag);
    return o;
}
static html_open open_entry_foreign(const dom_document *d, const html_parser *p, dom_node_h e, int ns, uint32_t t) {
    html_open o;
    o.node = e;
    o.tag = T_FOREIGN;
    o.atom = d->node[e.index].name;
    o.token = t;
    o.ns = (uint8_t)ns;
    int html_ip = 0;
    if (ns == DOM_NAMESPACE_MATHML) {
        uint32_t enc = tk_attr(p, t, "encoding");
        html_ip = enc && (tk_attr_value_is_ci(p, enc, "text/html") || tk_attr_value_is_ci(p, enc, "application/xhtml+xml"));
    }
    o.flags = foreign_flags(ns, names_ptr(d, o.atom), names_len(d, o.atom), html_ip);
    return o;
}

/* ---- a select's selectedcontent (HTML 4.10.17), as the parser keeps it ----
   When the parser pops an option off the stack and the option is the one its
   select shows, each selectedcontent of the select takes a clone of the
   option's content (4.10.10). An option the parser makes is never dirty, so the
   one a select shows is the last of its options with the selected attribute,
   or else, at a display size of 1, the first that is not disabled. */

static int el_is(const dom_document *d, uint32_t x, const char *name) {
    return x && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML &&
           names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t el_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}

/* an option's nearest ancestor select: none past a datalist, an hr, an option or a second optgroup */
static uint32_t option_select(const dom_document *d, uint32_t o) {
    int optgroups = 0;
    for (uint32_t a = d->node[o].parent; a; a = d->node[a].parent) {
        if (el_is(d, a, "datalist") || el_is(d, a, "hr") || el_is(d, a, "option")) return 0;
        if (el_is(d, a, "optgroup") && ++optgroups > 1) return 0;
        if (el_is(d, a, "select")) return a;
    }
    return 0;
}

/* the option a select shows, by its list of options (4.10.7), or 0 */
static uint32_t select_shown(const dom_document *d, uint32_t sel) {
    uint32_t last_selected = 0, first_enabled = 0;
    uint32_t x = d->node[sel].first_child;
    while (x) {
        int skip = el_is(d, x, "select") || el_is(d, x, "hr") || el_is(d, x, "datalist");
        if (el_is(d, x, "option")) {
            skip = 1;
            uint32_t up = d->node[x].parent;
            if (el_attr(d, x, "selected")) last_selected = x;
            if (!first_enabled && !el_attr(d, x, "disabled") && !(el_is(d, up, "optgroup") && el_attr(d, up, "disabled"))) first_enabled = x;
        } else if (el_is(d, x, "optgroup")) {
            for (uint32_t a = d->node[x].parent; a && a != sel; a = d->node[a].parent) if (el_is(d, a, "optgroup")) skip = 1;
        }
        /* the next node of the select's subtree in tree order, past x's own when skip */
        if (!skip && d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x && x != sel && !d->node[x].next_sibling) x = d->node[x].parent;
        x = x && x != sel ? d->node[x].next_sibling : 0;
    }
    if (last_selected) return last_selected;
    long size = el_attr(d, sel, "multiple") ? 4 : 1;
    uint32_t sz = el_attr(d, sel, "size");
    if (sz) {
        amb_span v = d->attr[sz].value;
        const char *s = text_ptr(d, v);
        size_t i = 0;
        while (i < v.len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\f' || s[i] == '\r')) i++;
        if (i < v.len && s[i] == '+') i++;
        if (i < v.len && s[i] >= '0' && s[i] <= '9') {
            size = 0;
            while (i < v.len && s[i] >= '0' && s[i] <= '9' && size < 100000) size = size * 10 + (s[i++] - '0');
        }
    }
    return size == 1 ? first_enabled : 0;
}

/* a deep clone of x, made in the document, or 0 for what a selectedcontent leaves
   out (script, SVG script, frame, iframe, embed, object); a template's clone has
   its contents cloned too */
static uint32_t clone_node(dom_document *d, uint32_t x) {
    dom_node_kind k = d->node[x].kind;
    dom_node_h h = { 0, 0, 0 };
    if (k == DOM_NODE_KIND_ELEMENT) {
        dom_namespace ns = d->node[x].ns;
        if (el_is(d, x, "script") || el_is(d, x, "frame") || el_is(d, x, "iframe") || el_is(d, x, "embed") || el_is(d, x, "object") ||
            (ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, "script", 6))) return 0;
        vector<char> nm;
        append_range(nm, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name));
        h = dom_create_element_ns(d, ns, data(nm), len(nm));
        if (!h.index) return 0;
        for (uint32_t a = d->node[x].first_attr; a; a = d->attr[a].next) {
            vector<char> an, av;
            append_range(an, names_ptr(d, d->attr[a].name), names_len(d, d->attr[a].name));
            append_range(av, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
            dom_set_attribute_ns(d, h, d->attr[a].ns, data(an), len(an), data(av), len(av));
        }
        uint32_t from = d->node[x].content, to = d->node[h.index].content;
        if (from && to)
            for (uint32_t ch = d->node[from].first_child; ch; ch = d->node[ch].next_sibling) {
                uint32_t y = clone_node(d, ch);
                if (y) dom_append_child(d, node_handle(d, to), node_handle(d, y));
            }
    } else if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) {
        vector<char> t;
        append_range(t, text_ptr(d, d->node[x].data), d->node[x].data.len);
        h = k == DOM_NODE_KIND_TEXT ? dom_create_text(d, data(t), len(t)) : dom_create_comment(d, data(t), len(t));
        return h.index;
    } else return 0;
    for (uint32_t ch = d->node[x].first_child; ch; ch = d->node[ch].next_sibling) {
        uint32_t y = clone_node(d, ch);
        if (y) dom_append_child(d, h, node_handle(d, y));
    }
    return h.index;
}

/* a selectedcontent that shows nothing: inside an option or another
   selectedcontent, or under a second select */
static int selectedcontent_disabled(const dom_document *d, uint32_t sc) {
    int selects = 0;
    for (uint32_t a = d->node[sc].parent; a; a = d->node[a].parent) {
        if (el_is(d, a, "select") && ++selects > 1) return 1;
        if (el_is(d, a, "option") || el_is(d, a, "selectedcontent")) return 1;
    }
    return 0;
}

/* each selectedcontent of the select, its children replaced by clones of the option's */
static void selectedcontent_update(dom_document *d, uint32_t sel, uint32_t opt) {
    if (el_attr(d, sel, "multiple")) return;
    vector<uint32_t> targets;
    for (uint32_t x = d->node[sel].first_child; x;) {
        if (el_is(d, x, "selectedcontent") && !selectedcontent_disabled(d, x)) push_back(targets, x);
        if (d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x && x != sel && !d->node[x].next_sibling) x = d->node[x].parent;
        x = x && x != sel ? d->node[x].next_sibling : 0;
    }
    for (size_t i = 0; i < len(targets); i++) {
        uint32_t sc = at(targets, i);
        while (d->node[sc].first_child) {
            dom_node_h ch = node_handle(d, d->node[sc].first_child);
            dom_remove_child(d, node_handle(d, sc), ch);
            dom_destroy(d, ch);
        }
        for (uint32_t ch = d->node[opt].first_child; ch; ch = d->node[ch].next_sibling) {
            uint32_t y = clone_node(d, ch);
            if (y) dom_append_child(d, node_handle(d, sc), node_handle(d, y));
        }
    }
}

/* the k-th option followed is on the stack still: where it was pushed, or, moved
   by the adoption agency, wherever it is now */
static int option_open(CTX, size_t k) {
    uint32_t o = at(*opts, 2 * k), i0 = at(*opts, 2 * k + 1);
    if (i0 < st_len(open) && st_at(open, i0).node.index == o) return 1;
    int i = st_index_of_node(open, node_handle(d, o));
    if (i < 0) return 0;
    (*opts)[2 * k + 1] = (uint32_t)i;
    return 1;
}

/* each option followed that the stack holds no more: popped since the last look.
   An option below the newest still open is open too, unless one left from below
   the stack's top (opt_cut): so the look stops at the first open, and costs
   nothing while the options stay */
static void options_popped(CTX) {
    for (size_t k = len(*opts) / 2; k-- > 0;) {
        if (option_open(ARGS, k)) { if (!c->opt_cut) break; continue; }
        uint32_t o = at(*opts, 2 * k);
        erase(opts, 2 * k + 1);
        erase(opts, 2 * k);
        uint32_t sel = option_select(d, o);
        if (sel && select_shown(d, sel) == o) selectedcontent_update(d, sel, o);
    }
    c->opt_cut = 0;
}

/* the options are followed from the first selectedcontent on: those open then, and
   each pushed after */
static void track_options(CTX) {
    c->track_opts = 1;
    for (size_t i = 0; i < st_len(open); i++)
        if (st_at(open, i).tag == T_OPTION) { push_back(opts, st_at(open, i).node.index); push_back(opts, (uint32_t)i); }
}

/* insert an HTML element for the token, at the appropriate place, and push it */
static dom_node_h insert_element(CTX, uint32_t t) {
    if (len(*opts)) options_popped(ARGS);
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    dom_node_h e = create_element(ARGS, t);
    insert_node_at(ARGS, parent, before, e);
    int tag = c->tok == t ? c->tag : tk_tag(p, t);
    if (!c->track_opts && tag == T_UNKNOWN && tk_name_is(p, t, "selectedcontent")) track_options(ARGS);
    push_back(open, open_entry(d, e, tag, t));
    if (c->track_opts && tag == T_OPTION) { push_back(opts, e.index); push_back(opts, (uint32_t)(st_len(open) - 1)); }
    return e;
}

/* insert a foreign element for the token, in the namespace, and push it */
static dom_node_h insert_foreign(CTX, uint32_t t, int ns) {
    dom_node_h parent, before;
    insertion_place(ARGS, current_node(open), &parent, &before);
    dom_node_h e = create_element_ns(ARGS, t, ns);
    insert_node_at(ARGS, parent, before, e);
    push_back(open, open_entry_foreign(d, p, e, ns, t));
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

static void afe_push_marker(vector<html_open> *l) {
    html_open m;
    memset(&m, 0, sizeof m);
    m.tag = T_MARKER;
    push_back(l, m);
}
/* two elements have the same attributes: names, namespaces and values, in any order */
static int same_attrs(const dom_document *d, dom_node_h a, dom_node_h b) {
    size_t na = 0, nb = 0;
    for (uint32_t x = d->node[a.index].first_attr; x; x = d->attr[x].next) na++;
    for (uint32_t y = d->node[b.index].first_attr; y; y = d->attr[y].next) nb++;
    if (na != nb) return 0;
    for (uint32_t x = d->node[a.index].first_attr; x; x = d->attr[x].next) {
        int found = 0;
        for (uint32_t y = d->node[b.index].first_attr; y; y = d->attr[y].next) {
            if (d->attr[x].name != d->attr[y].name || d->attr[x].ns != d->attr[y].ns) continue;
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
static int afe_index_of_node(const vector<html_open> *l, dom_node_h n) {
    for (size_t i = len(*l); i-- > 0;) if (at(*l, i).tag != T_MARKER && at(*l, i).node.index == n.index) return (int)i;
    return -1;
}
static void afe_clear_to_marker(vector<html_open> *l) {
    while (len(*l)) { int t = (int)back(*l).tag; pop_back(l); if (t == T_MARKER) break; }
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
        if (!node_in_scope(open, fe.node)) { html_terr(ARGS); return 1; }
        if (st_top(open).node.index != fe.node.index) html_terr(ARGS);
        int fb = -1;
        for (size_t i = (size_t)si + 1; i < st_len(open); i++) if (st_at(open, i).flags & F_SPECIAL) { fb = (int)i; break; }
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
            if (ai < 0) {
                if (node_e.tag == T_OPTION) c->opt_cut = 1;
                st_remove_at(open, (size_t)node_i);
                continue;
            }
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

static int current_template_mode(const vector<int> *s) { return len(*s) ? back(*s) : M_IN_BODY; }

static void reset_insertion_mode(CTX) {
    for (size_t i = st_len(open); i-- > 0;) {
        int last = i == 0;
        int t = (int)st_at(open, i).tag;
        if (last && c->fragment) t = c->ctx_tag;
        if ((t == T_TD || t == T_TH) && !last) { c->mode = M_IN_CELL; return; }
        if (t == T_TR) { c->mode = M_IN_ROW; return; }
        if (IN(t, T_TBODY, T_THEAD, T_TFOOT)) { c->mode = M_IN_TABLE_BODY; return; }
        if (t == T_CAPTION) { c->mode = M_IN_CAPTION; return; }
        if (t == T_COLGROUP) { c->mode = M_IN_COLUMN_GROUP; return; }
        if (t == T_TABLE) { c->mode = M_IN_TABLE; return; }
        if (t == T_TEMPLATE) { c->mode = current_template_mode(tmodes); return; }
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

/* the generic RCDATA and raw text element parsing algorithm: the element, the
   tokenizer's state, then text mode */
static void generic_text_element(CTX, uint32_t t, int tz_state) {
    insert_element(ARGS, t);
    c->tz_state = tz_state;
    c->orig_mode = c->mode;
    c->mode = M_TEXT;
}

/* in head, for the tags it shares with in body and in table */
/* ---- declarative shadow roots (HTML 13.2.6.4.4, a template start tag in head) ---- */

/* an element that may host a shadow root (DOM 4.2.14, attach a shadow root): an HTML element whose name is
   a valid custom element name, or one of the names the DOM lists */
static int html_shadow_host_ok(const dom_document *d, uint32_t x) {
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    const char *s = names_ptr(d, d->node[x].name);
    size_t n = names_len(d, d->node[x].name);
    static const char *const ok[] = { "article", "aside", "blockquote", "body", "div", "footer", "h1", "h2", "h3", "h4", "h5", "h6",
                                      "header", "main", "nav", "p", "section", "span" };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) if (n == strlen(ok[i]) && memcmp(s, ok[i], n) == 0) return 1;
    if (!n || s[0] < 'a' || s[0] > 'z') return 0;
    int hyphen = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 'A' && ch <= 'Z') return 0;
        if (ch == '-') hyphen = 1;
        if (ch < 0x80 && !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_')) return 0;
    }
    static const char *const kept[] = { "annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri", "font-face-format",
                                        "font-face-name", "missing-glyph" };
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) if (n == strlen(kept[i]) && memcmp(s, kept[i], n) == 0) return 0;
    return hyphen;
}
/* a template's shadowrootmode (open or closed, ASCII case-insensitive), when a document parsed whole (not a
   fragment: innerHTML's) has it on a template whose adjusted current node is not the topmost element, and
   that node may host a shadow root and hosts none: its shadow root attached, declarative, with the
   template's shadowrootdelegatesfocus, shadowrootclonable and shadowrootserializable; the template is pushed
   onto the stack of open elements alone, its contents the root, so that what it holds goes into the shadow
   tree, and is never in the document. 1 when it was so; 0, a template inserted as any other */
static int declarative_shadow(CTX, uint32_t t) {
    uint32_t ma = tk_attr(p, t, "shadowrootmode");
    if (!ma || c->fragment || st_len(open) < 2) return 0;
    int opened = tk_attr_value_is_ci(p, ma, "open"), closed = tk_attr_value_is_ci(p, ma, "closed");
    if (!opened && !closed) return 0;
    html_open ac = adjusted_current(c);
    uint32_t host = ac.node.index;
    if (!host || ac.ns != DOM_NAMESPACE_HTML || !html_shadow_host_ok(d, host) || d->node[host].shadow) return 0;
    dom_node_h r = dom_attach_shadow(d, ac.node, opened ? DOM_SHADOW_MODE_OPEN : DOM_SHADOW_MODE_CLOSED, DOM_SLOT_ASSIGNMENT_NAMED,
                                     tk_attr(p, t, "shadowrootdelegatesfocus") != 0, tk_attr(p, t, "shadowrootclonable") != 0,
                                     tk_attr(p, t, "shadowrootserializable") != 0, 1);
    if (!r.index) return 0;
    html_open o;
    o.node = r;
    o.tag = T_TEMPLATE;
    o.atom = names_intern(d, "template", 8);
    o.token = t;
    o.ns = DOM_NAMESPACE_HTML;
    o.flags = html_flags(T_TEMPLATE);
    push_back(open, o);
    return 1;
}

static int in_head(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = c->tag;
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    if (k == HTML_TOKEN_KIND_START_TAG) {
        if (tag == T_HTML) return process(ARGS, M_IN_BODY);
        if (IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META)) { insert_element(ARGS, t); st_pop(open); return 0; }
        if (tag == T_TITLE) { generic_text_element(ARGS, t, HTML_TZ_RCDATA); return 0; }
        if (IN(tag, T_NOFRAMES, T_STYLE)) { generic_text_element(ARGS, t, HTML_TZ_RAWTEXT); return 0; }
        if (tag == T_NOSCRIPT && html_scripting_flag) { generic_text_element(ARGS, t, HTML_TZ_RAWTEXT); return 0; }
        if (tag == T_NOSCRIPT) { insert_element(ARGS, t); c->mode = M_IN_HEAD_NOSCRIPT; return 0; }
        if (tag == T_SCRIPT) { generic_text_element(ARGS, t, HTML_TZ_SCRIPT); return 0; }
        if (tag == T_TEMPLATE) {
            afe_push_marker(afe);
            c->frameset_ok = 0;
            c->mode = M_IN_TEMPLATE;
            push_back(tmodes, M_IN_TEMPLATE);
            if (!declarative_shadow(ARGS, t)) insert_element(ARGS, t);
            return 0;
        }
        if (tag == T_HEAD) { html_terr(ARGS); return 0; }
    }
    if (k == HTML_TOKEN_KIND_END_TAG) {
        if (tag == T_HEAD) { st_pop(open); c->mode = M_AFTER_HEAD; return 0; }
        if (tag == T_TEMPLATE) {
            if (!st_has_tag(open, T_TEMPLATE)) { html_terr(ARGS); return 0; }
            implied_end_tags_thoroughly(open);
            if (cur_tag(open) != T_TEMPLATE) html_terr(ARGS);
            st_pop_until(open, T_TEMPLATE);
            afe_clear_to_marker(afe);
            if (len(*tmodes)) pop_back(tmodes);
            reset_insertion_mode(ARGS);
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

/* characters in body: U+0000 dropped, the rest inserted */
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
        if (m == M_INITIAL || m == M_BEFORE_HTML || m == M_BEFORE_HEAD) {
            if (ws) { s += ws; n -= ws; }
            else if (m == M_INITIAL) { html_terr(ARGS); set_quirks(ARGS); c->mode = M_BEFORE_HTML; }
            else if (m == M_BEFORE_HTML) { insert_html_root(ARGS); c->mode = M_BEFORE_HEAD; }
            else { c->head = insert_implied(ARGS, "head", T_HEAD); c->mode = M_IN_HEAD; }
        } else if (m == M_AFTER_AFTER_BODY || m == M_AFTER_AFTER_FRAMESET || m == M_AFTER_BODY) {
            /* white space in body; anything else in body (after body), or ignored (after after frameset) */
            if (ws) { in_body_chars(ARGS, s, ws); s += ws; n -= ws; }
            else if (m == M_AFTER_AFTER_FRAMESET) { html_terr(ARGS); s++; n--; }
            else { html_terr(ARGS); c->mode = M_IN_BODY; }
        } else if (m == M_IN_HEAD || m == M_IN_HEAD_NOSCRIPT || m == M_AFTER_HEAD) {
            if (ws) { insert_text(ARGS, s, ws); s += ws; n -= ws; }
            else if (m == M_IN_HEAD_NOSCRIPT) { html_terr(ARGS); st_pop(open); c->mode = M_IN_HEAD; }
            else if (m == M_IN_HEAD) { st_pop(open); c->mode = M_AFTER_HEAD; }
            else { insert_implied(ARGS, "body", T_BODY); c->mode = M_IN_BODY; }
        } else if (m == M_IN_BODY || m == M_IN_CAPTION || m == M_IN_CELL || m == M_IN_TEMPLATE) {
            in_body_chars(ARGS, s, n);
            n = 0;
        } else if (m == M_TEXT) {
            insert_text(ARGS, s, n);
            n = 0;
        } else if (m == M_IN_TABLE || m == M_IN_TABLE_BODY || m == M_IN_ROW) {
            if (IN(cur_tag(open), T_TABLE, T_TBODY, T_TEMPLATE, T_TFOOT, T_THEAD, T_TR)) {
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
            else if (cur_tag(open) != T_COLGROUP) { html_terr(ARGS); s++; n--; }
            else { st_pop(open); c->mode = M_IN_TABLE; }
        } else if (m == M_IN_FRAMESET || m == M_AFTER_FRAMESET) {
            for (size_t i = 0; i < n; i++) { if (is_ws(s[i])) insert_text(ARGS, s + i, 1); else html_terr(ARGS); }
            n = 0;
        } else n = 0;
    }
}

/* characters in foreign content: U+0000 as U+FFFD, the rest inserted */
static void foreign_chars(CTX, const char *s, size_t n) {
    vector<char> t;
    int any = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0) { html_terr(ARGS); push_back(t, (char)0xEF); push_back(t, (char)0xBF); push_back(t, (char)0xBD); }
        else { push_back(t, s[i]); if (!is_ws(s[i])) any = 1; }
    }
    insert_text(ARGS, data(t), len(t));
    if (any) c->frameset_ok = 0;
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
        if (e.flags & F_SPECIAL) { html_terr(ARGS); return; }
    }
}

static uint32_t tok_atom(const dom_document *d, const html_parser *p, uint32_t t) {
    amb_span s = p->token[t].name;
    return names_find(d, bytes_ptr(p, s), s.len);
}

/* the attributes of the token added to an element that lacks them (html, body) */
static void merge_attrs(CTX, dom_node_h h, uint32_t t) {
    for (uint32_t a = p->token[t].first_attr; a; a = p->tattr[a].next) {
        amb_span an = p->tattr[a].name, av = p->tattr[a].value;
        uint32_t atom = names_find(d, bytes_ptr(p, an), an.len);
        int has = 0;
        for (uint32_t x = d->node[h.index].first_attr; x; x = d->attr[x].next) if (atom && d->attr[x].name == atom) has = 1;
        if (!has) dom_set_attribute(d, h, bytes_ptr(p, an), an.len, bytes_ptr(p, av), av.len);
    }
}

static int in_body(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    if (k == HTML_TOKEN_KIND_EOF) {
        if (len(*tmodes)) return process(ARGS, M_IN_TEMPLATE);
        c->stopped = 1;
        return 0;
    }
    int tag = c->tag;
    if (k == HTML_TOKEN_KIND_START_TAG) {
        if (tag == T_HTML) {
            html_terr(ARGS);
            if (st_has_tag(open, T_TEMPLATE)) return 0;
            merge_attrs(ARGS, st_at(open, 0).node, t);
            return 0;
        }
        if (IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META, T_NOFRAMES, T_SCRIPT, T_STYLE, T_TEMPLATE, T_TITLE))
            return process(ARGS, M_IN_HEAD);
        if (tag == T_BODY) {
            html_terr(ARGS);
            if (st_len(open) < 2 || (int)st_at(open, 1).tag != T_BODY || st_has_tag(open, T_TEMPLATE)) return 0;
            c->frameset_ok = 0;
            merge_attrs(ARGS, st_at(open, 1).node, t);
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
                html_open e = st_at(open, i);
                int nt = (int)e.tag;
                if ((tag == T_LI && nt == T_LI) || (tag != T_LI && (nt == T_DD || nt == T_DT))) {
                    implied_end_tags(open, nt);
                    if (cur_tag(open) != nt) html_terr(ARGS);
                    st_pop_until(open, nt);
                    break;
                }
                if ((e.flags & F_SPECIAL) && !IN(nt, T_ADDRESS, T_DIV, T_P)) break;
            }
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (tag == T_PLAINTEXT) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            insert_element(ARGS, t);
            c->tz_state = HTML_TZ_PLAINTEXT;
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
            if (c->fragment && c->ctx_tag == T_SELECT) { html_terr(ARGS); return 0; }
            if (in_scope(open, T_SELECT, 0, SC_DEFAULT)) { html_terr(ARGS); st_pop_until(open, T_SELECT); }
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
            if (in_scope(open, T_SELECT, 0, SC_DEFAULT)) {
                implied_end_tags(open, -1);
                if (in_scope(open, T_OPTION, 0, SC_DEFAULT) || in_scope(open, T_OPTGROUP, 0, SC_DEFAULT)) html_terr(ARGS);
            }
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
            c->tz_state = HTML_TZ_RCDATA;
            c->frameset_ok = 0;
            c->orig_mode = c->mode;
            c->mode = M_TEXT;
            return 0;
        }
        if (tag == T_XMP) {
            if (in_scope(open, T_P, 0, SC_BUTTON)) close_p(ARGS);
            reconstruct_afe(ARGS);
            c->frameset_ok = 0;
            generic_text_element(ARGS, t, HTML_TZ_RAWTEXT);
            return 0;
        }
        if (tag == T_IFRAME) { c->frameset_ok = 0; generic_text_element(ARGS, t, HTML_TZ_RAWTEXT); return 0; }
        if (tag == T_NOEMBED || (tag == T_NOSCRIPT && html_scripting_flag)) { generic_text_element(ARGS, t, HTML_TZ_RAWTEXT); return 0; }
        if (tag == T_SELECT) {
            if (c->fragment && c->ctx_tag == T_SELECT) { html_terr(ARGS); return 0; }
            if (in_scope(open, T_SELECT, 0, SC_DEFAULT)) { html_terr(ARGS); st_pop_until(open, T_SELECT); return 0; }
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            c->frameset_ok = 0;
            return 0;
        }
        if (tag == T_OPTION) {
            if (in_scope(open, T_SELECT, 0, SC_DEFAULT)) {
                implied_end_tags(open, T_OPTGROUP);
                if (in_scope(open, T_OPTION, 0, SC_DEFAULT)) html_terr(ARGS);
            } else if (cur_tag(open) == T_OPTION) st_pop(open);
            reconstruct_afe(ARGS);
            insert_element(ARGS, t);
            return 0;
        }
        if (tag == T_OPTGROUP) {
            if (in_scope(open, T_SELECT, 0, SC_DEFAULT)) {
                implied_end_tags(open, -1);
                if (in_scope(open, T_OPTION, 0, SC_DEFAULT) || in_scope(open, T_OPTGROUP, 0, SC_DEFAULT)) html_terr(ARGS);
            } else if (cur_tag(open) == T_OPTION) st_pop(open);
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
        if (tag == T_MATH || tag == T_SVG) {
            reconstruct_afe(ARGS);
            insert_foreign(ARGS, t, tag == T_MATH ? DOM_NAMESPACE_MATHML : DOM_NAMESPACE_SVG);
            if (p->token[t].self_closing) st_pop(open);
            return 0;
        }
        if (IN(tag, T_CAPTION, T_COL, T_COLGROUP, T_FRAME, T_HEAD, T_TBODY, T_TD, T_TFOOT, T_TH, T_THEAD, T_TR)) {
            html_terr(ARGS);
            return 0;
        }
        /* any other start tag */
        reconstruct_afe(ARGS);
        insert_element(ARGS, t);
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
           T_MENU, T_NAV, T_OL, T_PRE, T_SEARCH, T_SECTION, T_SELECT, T_SUMMARY, T_UL)) {
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
            if (fi < 0 || !node_in_scope(open, f)) { html_terr(ARGS); return 0; }
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

/* clear the stack back to a table (kind 0), table body (1) or table row (2) context */
static void clear_to_table_context(vector<html_open> *s, int kind) {
    while (st_len(s) > 1) {
        int t = cur_tag(s);
        if (t == T_HTML || t == T_TEMPLATE) return;
        if (kind == 0 && t == T_TABLE) return;
        if (kind == 1 && IN(t, T_TBODY, T_TFOOT, T_THEAD)) return;
        if (kind == 2 && t == T_TR) return;
        st_pop(s);
    }
}

static int in_table(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = c->tag;
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

/* the in template insertion mode's switch to another, for the rest of the template */
static int template_switch(CTX, int mode) {
    if (len(*tmodes)) pop_back(tmodes);
    push_back(tmodes, mode);
    c->mode = mode;
    return 1;
}

/* 13.2.6.4.1: the doctype that puts the document in quirks mode (1), or in limited-quirks
   mode (2); 0 for no-quirks. The identifiers compare without ASCII case; an empty system
   identifier is a missing one, as Blink reads it (SetCompatibilityModeFromDoctype) */
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
    /* a system identifier that says nothing is none */
    if ((ids & 2) && sy.len == 0) ids = (uint8_t)(ids & ~2);
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
    if (ids & 1) {
        if (ci_prefix(pp, pu.len, "-//W3C//DTD XHTML 1.0 Frameset//") || ci_prefix(pp, pu.len, "-//W3C//DTD XHTML 1.0 Transitional//")) return 2;
        if ((ids & 2) && (ci_prefix(pp, pu.len, "-//W3C//DTD HTML 4.01 Frameset//") || ci_prefix(pp, pu.len, "-//W3C//DTD HTML 4.01 Transitional//")))
            return 2;
    }
    return 0;
}

static int process(CTX, int mode) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = c->tag;
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
            int qm = html_srcdoc_flag ? 0 : doctype_quirks(p, t);
            if (qm == 1) set_quirks(ARGS);
            else if (qm == 2) dom_set_mode(d, DOM_DOCUMENT_MODE_LIMITED_QUIRKS);
            c->mode = M_BEFORE_HTML;
            return 0;
        }
        if (!html_srcdoc_flag) {
            html_terr(ARGS);
            set_quirks(ARGS);
        }
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
    case M_IN_TEMPLATE:
        if (k == HTML_TOKEN_KIND_COMMENT || k == HTML_TOKEN_KIND_DOCTYPE) return process(ARGS, M_IN_BODY);
        if ((start && IN(tag, T_BASE, T_BASEFONT, T_BGSOUND, T_LINK, T_META, T_NOFRAMES, T_SCRIPT, T_STYLE, T_TEMPLATE, T_TITLE)) ||
            (end && tag == T_TEMPLATE))
            return process(ARGS, M_IN_HEAD);
        if (start && IN(tag, T_CAPTION, T_COLGROUP, T_TBODY, T_TFOOT, T_THEAD)) return template_switch(ARGS, M_IN_TABLE);
        if (start && tag == T_COL) return template_switch(ARGS, M_IN_COLUMN_GROUP);
        if (start && tag == T_TR) return template_switch(ARGS, M_IN_TABLE_BODY);
        if (start && IN(tag, T_TD, T_TH)) return template_switch(ARGS, M_IN_ROW);
        if (start) return template_switch(ARGS, M_IN_BODY);
        if (end) { html_terr(ARGS); return 0; }
        if (k == HTML_TOKEN_KIND_EOF) {
            if (!st_has_tag(open, T_TEMPLATE)) { c->stopped = 1; return 0; }
            html_terr(ARGS);
            st_pop_until(open, T_TEMPLATE);
            afe_clear_to_marker(afe);
            if (len(*tmodes)) pop_back(tmodes);
            reset_insertion_mode(ARGS);
            return 1;
        }
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

/* ---- foreign content (13.2.6.5) ---- */

/* the element's name, ASCII-lowercased, is the token's tag name */
static int open_named_ci(const dom_document *d, const html_parser *p, html_open e, uint32_t t) {
    amb_span s = p->token[t].name;
    if (names_len(d, e.atom) != s.len) return 0;
    const char *a = names_ptr(d, e.atom);
    const char *b = bytes_ptr(p, s);
    for (uint32_t i = 0; i < s.len; i++) {
        char x = a[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (x != b[i]) return 0;
    }
    return 1;
}

static int in_foreign(CTX) {
    uint32_t t = c->tok;
    html_token_kind k = tk_kind(p, t);
    int tag = c->tag;
    if (k == HTML_TOKEN_KIND_COMMENT) { insert_comment(ARGS, t); return 0; }
    if (k == HTML_TOKEN_KIND_DOCTYPE) { html_terr(ARGS); return 0; }
    /* the start tags of HTML's structure, and the end tags br and p, leave foreign content */
    if ((k == HTML_TOKEN_KIND_START_TAG &&
         (html_breakout(tag) || (tag == T_FONT && (tk_attr(p, t, "color") || tk_attr(p, t, "face") || tk_attr(p, t, "size"))))) ||
        (k == HTML_TOKEN_KIND_END_TAG && (tag == T_BR || tag == T_P))) {
        html_terr(ARGS);
        while (st_len(open) > 1) {
            html_open e = st_top(open);
            if (e.ns == DOM_NAMESPACE_HTML || (e.flags & (F_MATHML_TIP | F_HTML_IP))) break;
            st_pop(open);
        }
        return process(ARGS, c->mode);
    }
    if (k == HTML_TOKEN_KIND_START_TAG) {
        int ns = adjusted_current(c).ns;
        insert_foreign(ARGS, t, ns);
        /* self-closing: popped (an SVG script's end tag would pop it too) */
        if (p->token[t].self_closing) st_pop(open);
        return 0;
    }
    if (k == HTML_TOKEN_KIND_END_TAG) {
        html_open cur = st_top(open);
        if (tk_name_is(p, t, "script") && cur.ns == DOM_NAMESPACE_SVG && names_is(d, cur.atom, "script", 6)) { st_pop(open); return 0; }
        /* any other end tag */
        size_t i = st_len(open) - 1;
        html_open e = cur;
        if (!open_named_ci(d, p, e, t)) html_terr(ARGS);
        for (;;) {
            if (i == 0) return 0;
            if (open_named_ci(d, p, e, t)) { while (st_len(open) > i) st_pop(open); return 0; }
            i--;
            e = st_at(open, i);
            if (e.ns != DOM_NAMESPACE_HTML) continue;
            return process(ARGS, c->mode);
        }
    }
    return 0;
}

/* 13.2.6: the tree construction dispatcher -- the token goes to HTML content's
   rules (its insertion mode's) unless the adjusted current node is foreign and
   no integration point takes it */
static int html_content(CTX) {
    if (st_len(open) == 0) return 1;
    html_open a = adjusted_current(c);
    if (a.ns == DOM_NAMESPACE_HTML) return 1;
    html_token_kind k = tk_kind(p, c->tok);
    if (k == HTML_TOKEN_KIND_EOF) return 1;
    int start = k == HTML_TOKEN_KIND_START_TAG, chars = k == HTML_TOKEN_KIND_CHARACTERS;
    if ((a.flags & F_MATHML_TIP) && (chars || (start && !tk_name_is(p, c->tok, "mglyph") && !tk_name_is(p, c->tok, "malignmark"))))
        return 1;
    if ((a.flags & F_ANNOTATION_XML) && start && c->tag == T_SVG) return 1;
    if ((a.flags & F_HTML_IP) && (start || chars)) return 1;
    return 0;
}

static int dispatch(CTX) { return html_content(ARGS) ? process(ARGS, c->mode) : in_foreign(ARGS); }

/* the tokens of the table not yet built, in order, through the dispatcher */
static void run_tokens(CTX) {
    for (uint32_t t = c->next; t < p->token_len && !c->stopped; t++) {
        c->next = t + 1;
        if (!p->token[t].live) continue;
        c->tok = t;
        html_token_kind k = tk_kind(p, t);
        c->tag = k == HTML_TOKEN_KIND_START_TAG || k == HTML_TOKEN_KIND_END_TAG ? tk_tag(p, t) : T_UNKNOWN;
        if (k == HTML_TOKEN_KIND_CHARACTERS) {
            amb_span s = p->token[t].data;
            vector<char> text;
            append_range(text, bytes_ptr(p, s), s.len);
            if (html_content(ARGS)) characters(ARGS, data(text), len(text));
            else foreign_chars(ARGS, data(text), len(text));
            continue;
        }
        flush_table_text(ARGS);
        c->skip_lf = 0;
        /* a token is reprocessed a few times, or, at the end of the input in nested
           templates, once a template popped: past that, a builder's fault ends it */
        size_t guard = 64 + st_len(open);
        while (dispatch(ARGS) && guard-- > 0) {}
        if (len(*opts)) options_popped(ARGS);
    }
}

/* 13.2.7, the end: every node popped off the stack */
static void pop_all(CTX) {
    clear(open);
    if (len(*opts)) options_popped(ARGS);
}

/* the builder's parse errors, as records of the store */
static void record_errors(html_parser *p, const vector<uint32_t> *toks) {
    for (size_t i = 0; i < len(*toks); i++) {
        uint32_t t = at(*toks, i);
        uint32_t line = t && t < p->token_len ? p->token[t].line : 0, col = t && t < p->token_len ? p->token[t].col : 0;
        html_error_h e = error_new(p);
        p->error[e.index].code = HTML_ERROR_CODE_TREE_CONSTRUCTION;
        p->error[e.index].line = line;
        p->error[e.index].col = col;
    }
}

static void init_tb(html_tb *c) {
    memset(c, 0, sizeof *c);
    c->mode = M_INITIAL;
    c->frameset_ok = 1;
    c->next = 1;
}

/* the document built as the tokenizer reads in[0..n): each run of the tokenizer
   stops where the tree builder decides what it reads next, the tokens so far
   are built, and the next run goes on as the tree builder says */
static void build_driven(html_parser *p, dom_document *d, html_tb *c, const char *in, size_t n, html_tz *z) {
    for (;;) {
        html_tokenize_run(p, in, n, z);
        run_tokens(ARGS);
        if (z->stop == HTML_TZ_END || c->stopped) break;
        if (z->stop == HTML_TZ_TAG) {
            z->state = c->tz_state;
            c->tz_state = HTML_TZ_DATA;
        } else z->cdata = st_len(open) > 0 && adjusted_current(c).ns != DOM_NAMESPACE_HTML;
    }
    pop_all(ARGS);
    record_errors(p, errs);
}

html_status html_build(html_parser *p, dom_document *d) {
    html_tb tb;
    init_tb(&tb);
    defer(tb) { HTML_TB_DROP(tb); }
    run_tokens(d, p, &tb);
    pop_all(d, p, &tb);
    record_errors(p, &tb.l_errs);
    return HTML_OK;
}

/* the token table of src alone: no tree builder answers, so the tokenizer
   switches as HTML content would and reads no CDATA section */
html_status html_tokenize(html_parser *p, const char *src, size_t src_len) {
    vector<char> norm;
    resize(norm, src_len);
    size_t n = html_normalize_newlines(src, src_len, data(norm));
    html_tz z;
    html_tz_start(&z, HTML_TZ_DATA, 0);
    return html_tokenize_run(p, data(norm), n, &z);
}

/* the input's newlines normalized into norm when it has a CR (1); else there
   is nothing to normalize (0) */
static int normalized(const char *src, size_t src_len, vector<char> *norm, size_t *n) {
    *n = src_len;
    if (!src_len || !memchr(src, '\r', src_len)) return 0;
    resize(norm, src_len);
    *n = html_normalize_newlines(src, src_len, data(*norm));
    return 1;
}

html_status html_parse(html_parser *p, dom_document *d, const char *src, size_t src_len) {
    vector<char> norm;
    size_t n;
    int copied = normalized(src, src_len, &norm, &n);
    html_tb tb;
    init_tb(&tb);
    defer(tb) { HTML_TB_DROP(tb); }
    html_tz z;
    html_tz_start(&z, HTML_TZ_DATA, 1);
    if (copied) build_driven(p, d, &tb, data(norm), n, &z);
    else build_driven(p, d, &tb, src, n, &z);
    /* the radio button groups as the parser's insertions leave them: each one's last checked radio (src/html/forms.c) */
    form_parsed(d);
    return HTML_OK;
}

/* 13.4: the fragment parsing algorithm. The context element is named by its
   local name in the HTML namespace, or by "svg NAME" or "math NAME" in SVG's or
   MathML's (html5lib's notation); it stands for the adjusted current node while
   the stack holds the root alone */
html_status html_parse_fragment(html_parser *p, dom_document *d, const char *context, size_t context_len,
                                const char *src, size_t src_len) {
    int ns = DOM_NAMESPACE_HTML;
    const char *cn = context;
    size_t cl = context_len;
    if (cl > 4 && memcmp(cn, "svg ", 4) == 0) { ns = DOM_NAMESPACE_SVG; cn += 4; cl -= 4; }
    else if (cl > 5 && memcmp(cn, "math ", 5) == 0) { ns = DOM_NAMESPACE_MATHML; cn += 5; cl -= 5; }
    int ctx_tag = ns == DOM_NAMESPACE_HTML ? html_tag_of(cn, cl) : T_FOREIGN;
    int state = HTML_TZ_DATA;
    if (ctx_tag == T_TITLE || ctx_tag == T_TEXTAREA) state = HTML_TZ_RCDATA;
    else if (IN(ctx_tag, T_STYLE, T_XMP, T_IFRAME, T_NOEMBED, T_NOFRAMES) || (ctx_tag == T_NOSCRIPT && html_scripting_flag)) state = HTML_TZ_RAWTEXT;
    else if (ctx_tag == T_SCRIPT) state = HTML_TZ_SCRIPT;
    else if (ctx_tag == T_PLAINTEXT) state = HTML_TZ_PLAINTEXT;
    vector<char> norm;
    size_t n;
    int copied = normalized(src, src_len, &norm, &n);
    html_tb tb;
    init_tb(&tb);
    defer(tb) { HTML_TB_DROP(tb); }
    tb.fragment = 1;
    /* in quirks mode when the document is: the context element's document's mode, which
       the caller gives the fragment's document */
    tb.quirks = d->node_len > 1 && d->node[1].mode == DOM_DOCUMENT_MODE_QUIRKS;
    tb.ctx_tag = ctx_tag;
    tb.ctx_ns = ns;
    tb.ctx_flags = ns == DOM_NAMESPACE_HTML ? html_flags(ctx_tag) : foreign_flags(ns, cn, cl, 0);
    /* the root: an html element, its children the fragment's */
    dom_node_h root = dom_create_element(d, "html", 4);
    push_back(tb.l_open, open_entry(d, root, T_HTML, 0));
    if (ctx_tag == T_TEMPLATE) push_back(tb.l_tmodes, M_IN_TEMPLATE);
    if (ctx_tag == T_FORM) tb.form = root;
    reset_insertion_mode(d, p, &tb);
    html_tz z;
    html_tz_start(&z, state, 1);
    if (copied) build_driven(p, d, &tb, data(norm), n, &z);
    else build_driven(p, d, &tb, src, n, &z);
    /* the fragment's nodes become the document node's children */
    for (dom_node_h ch = dom_children_first(d, root); ch.index; ch = dom_children_first(d, root)) {
        dom_remove_child(d, root, ch);
        dom_append_child(d, dom_root(d), ch);
    }
    dom_destroy(d, root);
    return HTML_OK;
}
