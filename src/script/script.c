/* src/script/script.c -- the script module (contracts/script.pl): a page's scripts run
   by the engine (src/js/), the DOM's bindings, and the event loop; and the module's
   generated implementation.

   THE BINDINGS. The engine reaches the page through vm->host (script.h), set where the
   page's stores are declared. A node's wrapper is a host object (JK_HOST) whose slots
   hold the node's index and generation, never a pointer, one wrapper a node
   (vm->wrappers): a wrapper of a node the page has destroyed is stale, and using it
   throws. Every change a script makes is one of the DOM module's operations
   (dom_append_child, dom_set_attribute ...), so the contract's preconditions, its
   invariants and its mutation log hold for script as they do for the reasoner's
   requests. Selectors are the CSS module's own matcher.

   THE EVENT LOOP. The classic scripts run in document order once the page is parsed
   (no document.write, no parser blocking); then DOMContentLoaded at the document and
   load at the window; then the timers in virtual time, in order of their due times,
   as far as a horizon, every script, listener and timer under the engine's step
   budget. An event's listeners run in its path's order: the capturing ones from the
   window down, the target's, then the bubbling ones back up; an on* attribute's or
   property's handler first on its node.

   THE RECORDS. What ran, what the console was given, what was thrown and caught by
   nothing, the listeners and the timers are this module's records (its facts): the
   engine's values never are. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_implementation(script);
#include "../js/js.h"
#include "script.h"

/* the CSS module's matcher for a script's selector (src/css/css.c) */
int css_selector_matches(const dom_document *d, uint32_t x, const char *s, size_t n);
/* the view a script reads, a cascade and a layout run when it asks (src/script/view.c):
   made up to date; whether an element is rendered; its computed value of a property by
   CSS's name (0: the cascade computes no such property); its border box, its borders
   (top, right, bottom, left) and its client rects, in page px; the runs made so far */
void script_view_update(struct js_host *h);
int script_view_rendered(struct js_host *h, uint32_t x);
int script_view_style(struct js_host *h, uint32_t x, const char *name, size_t n, vector<char> *out);
int script_view_border_box(struct js_host *h, uint32_t x, double *r);
int script_view_borders(struct js_host *h, uint32_t x, double *r);
uint32_t script_view_rects(struct js_host *h, uint32_t x, double *r, uint32_t max);
uint32_t script_view_runs(const struct js_host *h);
uint32_t script_view_props(void);

/* ---- the host's kinds and names ---- */

/* a host object's kind, in its c slot */
enum { HK_NODE = 1, HK_CLASSLIST, HK_STYLE, HK_DATASET, HK_EVENT, HK_LIST, HK_COMPUTED };
/* its prototypes, in the host's protos */
enum { HP_NODE, HP_ELEMENT, HP_CHARDATA, HP_DOCUMENT, HP_CLASSLIST, HP_STYLE, HP_DATASET, HP_EVENT, HP_NODELIST,
       HP_HTMLCOLLECTION, HP_DOMRECT, HP_RECTLIST, HP_N };

/* the names the host answers for, interned once (vm->hatoms: these first, then the
   reverse map from an atom to its name's number plus one) */
enum {
    HA_TEXTCONTENT, HA_INNERHTML, HA_OUTERHTML, HA_INNERTEXT, HA_ID, HA_CLASSNAME, HA_CLASSLIST, HA_TAGNAME,
    HA_NODENAME, HA_NODETYPE, HA_NODEVALUE, HA_DATA, HA_PARENTNODE, HA_PARENTELEMENT, HA_CHILDNODES, HA_CHILDREN,
    HA_FIRSTCHILD, HA_LASTCHILD, HA_NEXTSIBLING, HA_PREVIOUSSIBLING, HA_FIRSTELEMENTCHILD, HA_LASTELEMENTCHILD,
    HA_NEXTELEMENTSIBLING, HA_PREVIOUSELEMENTSIBLING, HA_CHILDELEMENTCOUNT, HA_OWNERDOCUMENT, HA_ISCONNECTED,
    HA_STYLE, HA_DATASET, HA_VALUE, HA_CHECKED, HA_DISABLED, HA_HIDDEN, HA_HREF, HA_SRC, HA_ALT, HA_TITLE, HA_NAME,
    HA_TYPE, HA_PLACEHOLDER, HA_LANG, HA_DIR, HA_TABINDEX, HA_BODY, HA_HEAD, HA_DOCUMENTELEMENT, HA_READYSTATE,
    HA_DEFAULTVIEW, HA_TARGET, HA_CURRENTTARGET, HA_BUBBLES, HA_CANCELABLE, HA_DEFAULTPREVENTED, HA_EVENTPHASE,
    HA_TIMESTAMP, HA_ISTRUSTED, HA_DETAIL, HA_CSSTEXT, HA_SELECTED, HA_HTMLFOR, HA_REL, HA_LENGTH, HA_CAPTURE,
    HA_ONCE, HA_LOCALNAME, HA_WINDOW, HA_DOCUMENT, HA_SELF, HA_READONLY, HA_REQUIRED, HA_ACTION, HA_METHOD,
    HA_NAMESPACEURI, HA_HASH, HA_VALUE_STATE, HA_FORMS, HA_IMAGES, HA_LINKS, HA_ANCHORS, HA_SCRIPTS, HA_ELEMENTS,
    HA_OPTIONS, HA_ROWS, HA_CELLS, HA_TBODIES, HA_OFFSETWIDTH, HA_OFFSETHEIGHT, HA_OFFSETTOP, HA_OFFSETLEFT,
    HA_OFFSETPARENT, HA_CLIENTWIDTH, HA_CLIENTHEIGHT, HA_CLIENTTOP, HA_CLIENTLEFT, HA_SCROLLTOP, HA_SCROLLLEFT,
    HA_SCROLLWIDTH, HA_SCROLLHEIGHT, HA_N
};
static const char *const sh_names[HA_N] = {
    "textContent", "innerHTML", "outerHTML", "innerText", "id", "className", "classList", "tagName",
    "nodeName", "nodeType", "nodeValue", "data", "parentNode", "parentElement", "childNodes", "children",
    "firstChild", "lastChild", "nextSibling", "previousSibling", "firstElementChild", "lastElementChild",
    "nextElementSibling", "previousElementSibling", "childElementCount", "ownerDocument", "isConnected",
    "style", "dataset", "value", "checked", "disabled", "hidden", "href", "src", "alt", "title", "name",
    "type", "placeholder", "lang", "dir", "tabIndex", "body", "head", "documentElement", "readyState",
    "defaultView", "target", "currentTarget", "bubbles", "cancelable", "defaultPrevented", "eventPhase",
    "timeStamp", "isTrusted", "detail", "cssText", "selected", "htmlFor", "rel", "length", "capture",
    "once", "localName", "window", "document", "self", "readOnly", "required", "action", "method",
    "namespaceURI", "hash", "\001value", "forms", "images", "links", "anchors", "scripts", "elements",
    "options", "rows", "cells", "tBodies", "offsetWidth", "offsetHeight", "offsetTop", "offsetLeft",
    "offsetParent", "clientWidth", "clientHeight", "clientTop", "clientLeft", "scrollTop", "scrollLeft",
    "scrollWidth", "scrollHeight" };

/* the host's natives' numbers, from JS_HOST_NATIVES */
enum {
    H_APPEND_CHILD = JS_HOST_NATIVES, H_INSERT_BEFORE, H_REMOVE_CHILD, H_REPLACE_CHILD, H_CONTAINS, H_HAS_CHILD_NODES,
    H_CLONE_NODE, H_ADD_LISTENER, H_REMOVE_LISTENER, H_DISPATCH, H_REMOVE, H_BEFORE, H_AFTER, H_APPEND, H_PREPEND,
    H_REPLACE_WITH, H_GET_ATTR, H_SET_ATTR, H_REMOVE_ATTR, H_HAS_ATTR, H_TOGGLE_ATTR, H_ATTR_NAMES, H_QS, H_QSA,
    H_BY_TAG, H_BY_CLASS, H_MATCHES, H_CLOSEST, H_CLICK, H_FOCUS, H_BLUR, H_RECT, H_BY_ID, H_CREATE_ELEMENT,
    H_CREATE_TEXT, H_CREATE_COMMENT, H_CL_ADD, H_CL_REMOVE, H_CL_TOGGLE, H_CL_CONTAINS, H_CL_REPLACE, H_CL_ITEM,
    H_CL_TOSTRING, H_ST_SET, H_ST_GET, H_ST_REMOVE, H_EV_PREVENT, H_EV_STOP, H_EV_STOP_NOW, H_EVENT, H_CUSTOM_EVENT,
    H_RAF, H_CAF, H_COMPUTED, H_ALERT, H_ILLEGAL, H_WIN_ADD, H_WIN_REMOVE, H_WIN_DISPATCH, H_INSERT_ADJ_HTML,
    H_ADOPT_TEXT, H_LIST_ITEM, H_LIST_NAMED, H_BY_NAME, H_CLIENT_RECTS, H_RECT_JSON
};

/* an event's flags, in its a slot */
#define EV_STOP 1u
#define EV_STOP_NOW 2u
#define EV_PREVENTED 4u
#define EV_CANCELABLE 8u
#define EV_BUBBLES 16u
#define EV_TRUSTED 32u
#define EV_PASSIVE 64u

static uint32_t sh_atom(js_vm *vm, uint32_t ha) { return vm->hatoms[ha]; }
/* the host's name an atom is, plus one; 0 when none */
static uint32_t sh_name_of(js_vm *vm, uint32_t atom) {
    size_t i = (size_t)HA_N + atom;
    return i < len(vm->hatoms) ? vm->hatoms[i] : 0;
}

/* ---- text: the engine's strings and the DOM's UTF-8 ---- */

static jv sh_str(js_vm *vm, const char *p, size_t n) { return jv_str(js_str_utf8(vm, p, n)); }
static jv sh_cstr(js_vm *vm, const char *p) { return jv_str(js_str_utf8(vm, p, strlen(p))); }
/* a value's string as UTF-8 into out (cleared first): 0 when its conversion threw */
static int sh_utf8(js_vm *vm, jv v, vector<char> *out) {
    clear(*out);
    uint32_t s = jv_is_str(v) ? jv_idx(v) : js_to_string(vm, v);
    if (!s) return 0;
    js_str_to_utf8(vm, s, out);
    return 1;
}
static jv sh_span(js_vm *vm, const dom_document *d, amb_span sp) { return sh_str(vm, text_ptr(d, sp), sp.len); }

/* ---- the records ---- */

static uint32_t sh_run_new(js_vm *vm, uint32_t elem, script_source src, const char *url, size_t un) {
    struct js_host *h = vm->host;
    script_run_h r = run_new(h->rt);
    h->rt->run[r.index].elem = node_handle(h->d, elem);
    h->rt->run[r.index].source = src;
    amb_span u = text_add(h->rt, url, un);
    h->rt->run[r.index].url = u;
    h->rt->run[r.index].outcome = SCRIPT_OUTCOME_RAN;
    return r.index;
}
static void sh_error_rec(js_vm *vm, uint32_t run, uint32_t phase, const char *m, size_t n) {
    struct js_host *h = vm->host;
    amb_span t = text_add(h->rt, m, n);
    script_error_h e = error_new(h->rt);
    h->rt->error[e.index].run = run;
    h->rt->error[e.index].phase = (script_phase)phase;
    h->rt->error[e.index].message = t;
}
/* the exception in flight, recorded as uncaught and cleared: 1; the budget past: 2 */
static int sh_uncaught(js_vm *vm, uint32_t run, uint32_t phase) {
    if (vm->throwing == 2) return 2;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    vector<char> m;
    js_error_text(vm, e, &m);
    if (vm->host) sh_error_rec(vm, run, phase, data(m), len(m));
    else {
        push_back(m, '\n');
        fwrite(data(m), 1, len(m), stderr);
    }
    return 1;
}

/* an error a microtask threw that no code caught, or a promise's rejection no handler
   took (src/js/builtins.c): the page's record of the script whose code it was, in the
   phase given; with no page, stderr's line, as V8's shell writes it */
void js_report_error(js_vm *vm, jv e, uint32_t phase) {
    int was = vm->throwing;
    jv saved = vm->exception;
    vm->throwing = 0;
    vector<char> m;
    js_error_text(vm, e, &m);
    if (vm->throwing == 1) { vm->throwing = 0; vm->exception = JV_UNDEF; }   /* its text's getter threw: the text so far */
    if (vm->throwing == 0 && was) { vm->throwing = was; vm->exception = saved; }
    if (vm->host && vm->host->rt) sh_error_rec(vm, vm->cur_run, phase, data(m), len(m));
    else {
        const char *pre = phase == JS_PHASE_PROMISE ? "Uncaught (in promise) " : "Uncaught ";
        fwrite(pre, 1, strlen(pre), stderr);
        push_back(m, '\n');
        fwrite(data(m), 1, len(m), stderr);
    }
}

/* the microtasks, once no script code is on the stack: after a script, a listener a
   user's event ran, a timer */
static void sh_checkpoint(js_vm *vm) {
    if (vm->nframes != 0 || vm->throwing) return;
    uint32_t ph = vm->host ? vm->host->phase : 0;
    if (vm->host) vm->host->phase = JS_PHASE_PROMISE;
    js_run_jobs(vm);
    if (vm->host) vm->host->phase = ph;
}

/* ---- the console: every message kept (vm->msgs), the page's recorded ---- */

void js_console(js_vm *vm, uint32_t level, uint32_t text) {
    js_msg m = { level, text };
    push_back(vm->msgs, m);
    vector<char> t;
    if (vm->echo || vm->host) js_str_to_utf8(vm, text, &t);
    if (vm->host && vm->host->rt) {
        amb_span sp = text_add(vm->host->rt, data(t), len(t));
        script_console_h c = console_new(vm->host->rt);
        vm->host->rt->console[c.index].run = vm->cur_run;
        vm->host->rt->console[c.index].level = level <= 4 ? (script_level)level : SCRIPT_LEVEL_LOG;
        vm->host->rt->console[c.index].text = sp;
    }
    if (vm->echo) {
        push_back(t, '\n');
        fwrite(data(t), 1, len(t), level >= 2 ? stderr : stdout);
    }
}

/* ---- wrappers ---- */

static int sh_is_kind(const dom_document *d, uint32_t x, dom_node_kind k) { return x && x < d->node_len && d->node[x].live && d->node[x].kind == k; }
static int sh_is_elem(const dom_document *d, uint32_t x) { return sh_is_kind(d, x, DOM_NODE_KIND_ELEMENT); }
/* the HTML element of that name (an SVG or MathML element of the name is none) */
static int sh_named(const dom_document *d, uint32_t x, const char *name) {
    return sh_is_elem(d, x) && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, name, strlen(name));
}
/* a script element: HTML's, or SVG's, which runs too */
static int sh_script_elem(const dom_document *d, uint32_t x) {
    return sh_named(d, x, "script") ||
           (sh_is_elem(d, x) && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, "script", 6));
}

/* the wrapper of node x, made at its first use: null for none */
static jv sh_wrap(js_vm *vm, uint32_t x) {
    dom_document *d = vm->host->d;
    if (!x || x >= d->node_len || !d->node[x].live) return JV_NULL;
    if (x >= len(vm->wrappers)) resize(vm->wrappers, (size_t)d->node_len + 64, 0u);
    uint32_t w = vm->wrappers[x];
    if (w && vm->objs[w].kind == JK_HOST && vm->objs[w].b == d->node[x].gen && vm->objs[w].a == x) return jv_obj(w);
    dom_node_kind k = d->node[x].kind;
    uint32_t p = k == DOM_NODE_KIND_ELEMENT ? HP_ELEMENT : k == DOM_NODE_KIND_DOCUMENT ? HP_DOCUMENT : HP_CHARDATA;
    uint32_t o = js_obj_new(vm, JK_HOST, vm->host->protos[p]);
    vm->objs[o].a = x;
    vm->objs[o].b = d->node[x].gen;
    vm->objs[o].c = HK_NODE;
    vm->wrappers[x] = o;
    return jv_obj(o);
}

/* the node a value wraps, live: its index; 0 when it wraps none. A stale wrapper throws
   (stale set), as does a value that is no node when need is set */
static uint32_t sh_node(js_vm *vm, jv v, int need) {
    if (jv_is_obj(v)) {
        uint32_t o = jv_idx(v);
        if (vm->objs[o].kind == JK_HOST && vm->objs[o].c == HK_NODE) {
            dom_document *d = vm->host->d;
            uint32_t x = vm->objs[o].a;
            if (x < d->node_len && d->node[x].live && d->node[x].gen == vm->objs[o].b) return x;
            js_throw_msg(vm, vm->type_error_proto, "the node is gone: the page destroyed it (a stale handle)");
            return 0;
        }
    }
    if (need) js_throw_msg(vm, vm->type_error_proto, "the argument is not a Node");
    return 0;
}
/* the node of a host object of kind kind (a classList's, a style's: its element) */
static uint32_t sh_owner(js_vm *vm, uint32_t o) {
    dom_document *d = vm->host->d;
    uint32_t x = vm->objs[o].a;
    if (x < d->node_len && d->node[x].live && d->node[x].gen == vm->objs[o].b) return x;
    js_throw_msg(vm, vm->type_error_proto, "the node is gone: the page destroyed it (a stale handle)");
    return 0;
}
static jv sh_host_obj(js_vm *vm, uint32_t kind, uint32_t proto, uint32_t x) {
    uint32_t o = js_obj_new(vm, JK_HOST, vm->host->protos[proto]);
    vm->objs[o].a = x;
    vm->objs[o].b = vm->host->d->node[x].gen;
    vm->objs[o].c = kind;
    return jv_obj(o);
}

/* a DOMException of a name, with its message */
static jv sh_dom_error(js_vm *vm, const char *name, const char *msg) {
    uint32_t e = js_error_new(vm, vm->error_proto, js_str_ascii(vm, msg));
    js_define(vm, e, A_NAME, sh_cstr(vm, name), JS_SHAPE_HIDDEN);
    return js_throw(vm, jv_obj(e));
}
/* a DOM operation's refusal as the exception the DOM standard names */
static jv sh_status(js_vm *vm, dom_status st) {
    if (st == DOM_OK) return JV_UNDEF;
    if (st == DOM_WOULD_CYCLE || st == DOM_WRONG_KIND || st == DOM_HAS_PARENT)
        return sh_dom_error(vm, "HierarchyRequestError", "the node cannot be inserted there");
    if (st == DOM_NOT_A_CHILD) return sh_dom_error(vm, "NotFoundError", "the node is not a child of this node");
    if (st == DOM_INVALID_NAME) return sh_dom_error(vm, "InvalidCharacterError", "the name is not a valid name");
    if (st == DOM_STALE_HANDLE) return js_throw_msg(vm, vm->type_error_proto, "the node is gone: the page destroyed it (a stale handle)");
    return sh_dom_error(vm, "OperationError", "the DOM refused the operation");
}

/* ---- the DOM, read ---- */

static uint32_t sh_attr(const dom_document *d, uint32_t x, const char *name, size_t n) {
    uint32_t a = names_find(d, name, n);
    if (!a || !sh_is_elem(d, x)) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
static uint32_t sh_attr_z(const dom_document *d, uint32_t x, const char *name) { return sh_attr(d, x, name, strlen(name)); }

/* the text of node x's subtree (a text's or a comment's own data), appended to out */
static void sh_text_of(const dom_document *d, uint32_t x, vector<char> *out) {
    dom_node_kind k = d->node[x].kind;
    if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) {
        append_range(*out, text_ptr(d, d->node[x].data), d->node[x].data.len);
        return;
    }
    uint32_t c = d->node[x].first_child;
    while (c && c != x) {
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) append_range(*out, text_ptr(d, d->node[c].data), d->node[c].data.len);
        if (d->node[c].first_child) { c = d->node[c].first_child; }
        else {
            while (c && c != x && !d->node[c].next_sibling) c = d->node[c].parent;
            if (c && c != x) c = d->node[c].next_sibling;
        }
    }
}
/* the next node after c in tree order, within the subtree of scope */
static uint32_t sh_next_in(const dom_document *d, uint32_t c, uint32_t scope) {
    if (d->node[c].first_child) return d->node[c].first_child;
    while (c && c != scope) {
        if (d->node[c].next_sibling) return d->node[c].next_sibling;
        c = d->node[c].parent;
    }
    return 0;
}
static uint32_t sh_elem_sibling(const dom_document *d, uint32_t x, int next) {
    uint32_t y = next ? d->node[x].next_sibling : d->node[x].prev_sibling;
    while (y && d->node[y].kind != DOM_NODE_KIND_ELEMENT) y = next ? d->node[y].next_sibling : d->node[y].prev_sibling;
    return y;
}
static uint32_t sh_elem_child(const dom_document *d, uint32_t x, int last) {
    uint32_t y = last ? d->node[x].last_child : d->node[x].first_child;
    while (y && d->node[y].kind != DOM_NODE_KIND_ELEMENT) y = last ? d->node[y].prev_sibling : d->node[y].next_sibling;
    return y;
}
static int sh_connected(const dom_document *d, uint32_t x) {
    while (x && x != 1) x = d->node[x].parent;
    return x == 1;
}
/* the document's element of a name, under its root element (body, head) */
static uint32_t sh_doc_child(const dom_document *d, const char *name) {
    uint32_t html = sh_elem_child(d, 1, 0);
    if (!html) return 0;
    for (uint32_t c = d->node[html].first_child; c; c = d->node[c].next_sibling) if (sh_named(d, c, name)) return c;
    return 0;
}

/* a name's text, upper-cased (an HTML element's tagName) */
static jv sh_upper_name(js_vm *vm, const dom_document *d, uint32_t atom) {
    vector<char> b;
    append_range(b, names_ptr(d, atom), names_len(d, atom));
    for (size_t i = 0; i < len(b); i++) if (b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
    return sh_str(vm, data(b), len(b));
}
/* an element's tagName: an HTML element's name upper-cased, a foreign element's as it is */
static jv sh_tag_name(js_vm *vm, const dom_document *d, uint32_t x) {
    if (d->node[x].ns == DOM_NAMESPACE_HTML) return sh_upper_name(vm, d, d->node[x].name);
    return sh_str(vm, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name));
}

/* ---- the DOM, written: each change one of its operations ---- */

/* child out of its parent, if it has one */
static dom_status sh_detach(dom_document *d, uint32_t child) {
    uint32_t p = d->node[child].parent;
    if (!p) return DOM_OK;
    return dom_remove_child(d, node_handle(d, p), node_handle(d, child));
}
/* child inserted into parent before before (0: at the end), moved there if it was elsewhere */
static dom_status sh_insert(dom_document *d, uint32_t parent, uint32_t child, uint32_t before) {
    if (child == before) return DOM_OK;
    if (d->node[child].kind == DOM_NODE_KIND_DOCUMENT) return DOM_WRONG_KIND;
    if (children_inclusive_ancestor(d, child, parent)) return DOM_WOULD_CYCLE;
    if (before && d->node[before].parent != parent) return DOM_NOT_A_CHILD;
    if (parent != 1 && !sh_is_elem(d, parent)) return DOM_WRONG_KIND;
    dom_status st = sh_detach(d, child);
    if (st != DOM_OK) return st;
    if (before) return dom_insert_before(d, node_handle(d, parent), node_handle(d, child), node_handle(d, before));
    return dom_append_child(d, node_handle(d, parent), node_handle(d, child));
}
/* every child of x out (kept, parentless, for a script that holds them) */
static void sh_empty(dom_document *d, uint32_t x) {
    while (d->node[x].first_child) {
        uint32_t c = d->node[x].first_child;
        if (dom_remove_child(d, node_handle(d, x), node_handle(d, c)) != DOM_OK) break;
    }
}
/* x's content one text (none for the empty one): textContent's set */
static void sh_set_text(dom_document *d, uint32_t x, const char *t, size_t n) {
    dom_node_kind k = d->node[x].kind;
    if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) { dom_set_data(d, node_handle(d, x), t, n); return; }
    if (k != DOM_NODE_KIND_ELEMENT) return;
    sh_empty(d, x);
    if (!n) return;
    dom_node_h tn = dom_create_text(d, t, n);
    dom_append_child(d, node_handle(d, x), tn);
}

/* a copy of node x of the document from, made in to (with its subtree when deep): its index.
   An element keeps its namespace and its attributes theirs; a template's copy has a copy of
   its contents when deep */
static uint32_t sh_import(dom_document *to, const dom_document *from, uint32_t x, int deep) {
    dom_node_kind k = from->node[x].kind;
    dom_node_h h = { 0, 0, 0 };
    if (k == DOM_NODE_KIND_ELEMENT) {
        vector<char> nm;
        append_range(nm, names_ptr(from, from->node[x].name), names_len(from, from->node[x].name));
        h = dom_create_element_ns(to, from->node[x].ns, data(nm), len(nm));
        if (!h.index) return 0;
        for (uint32_t at = from->node[x].first_attr; at; at = from->attr[at].next) {
            vector<char> an, av;
            append_range(an, names_ptr(from, from->attr[at].name), names_len(from, from->attr[at].name));
            append_range(av, text_ptr(from, from->attr[at].value), from->attr[at].value.len);
            dom_set_attribute_ns(to, h, from->attr[at].ns, data(an), len(an), data(av), len(av));
        }
        uint32_t fc = from->node[x].content, tc = to->node[h.index].content;
        if (deep && fc && tc)
            for (uint32_t c = from->node[fc].first_child; c; c = from->node[c].next_sibling) {
                uint32_t y = sh_import(to, from, c, 1);
                if (y) dom_append_child(to, node_handle(to, tc), node_handle(to, y));
            }
    } else if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) {
        vector<char> t;
        append_range(t, text_ptr(from, from->node[x].data), from->node[x].data.len);
        h = k == DOM_NODE_KIND_TEXT ? dom_create_text(to, data(t), len(t)) : dom_create_comment(to, data(t), len(t));
    } else return 0;
    if (deep) {
        for (uint32_t c = from->node[x].first_child; c; c = from->node[c].next_sibling) {
            uint32_t y = sh_import(to, from, c, 1);
            if (y) dom_append_child(to, h, node_handle(to, y));
        }
    }
    return h.index;
}

/* html parsed as a fragment and its nodes inserted into parent before before (0: at the
   end): the fragment is the body of a document of its own, its nodes copied over */
static void sh_insert_html(dom_document *d, uint32_t parent, uint32_t before, const char *html, size_t n) {
    dom_document f;
    html_parser hp;
    dom_init(&f, 2);
    html_init(&hp, 2);
    vector<char> src;
    append_range(src, "<!DOCTYPE html><body>", 21);
    append_range(src, html, n);
    html_parse(&hp, &f, data(src), len(src));
    uint32_t body = 0;
    for (uint32_t x = 1; x < f.node_len && !body; x++) if (f.node[x].live && sh_named(&f, x, "body")) body = x;
    if (!body) return;
    for (uint32_t c = f.node[body].first_child; c; c = f.node[c].next_sibling) {
        uint32_t y = sh_import(d, &f, c, 1);
        if (!y) continue;
        if (before) dom_insert_before(d, node_handle(d, parent), node_handle(d, y), node_handle(d, before));
        else dom_append_child(d, node_handle(d, parent), node_handle(d, y));
    }
}

/* ---- the DOM, written as HTML (innerHTML, outerHTML) ---- */

static int sh_void(const dom_document *d, uint32_t x) {
    static const char *const v[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr" };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) if (sh_named(d, x, v[i])) return 1;
    return 0;
}
static void sh_escape(vector<char> *out, const char *s, size_t n, int attr) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') append_range(*out, "&amp;", 5);
        else if (c == '<' && !attr) append_range(*out, "&lt;", 4);
        else if (c == '>' && !attr) append_range(*out, "&gt;", 4);
        else if (c == '"' && attr) append_range(*out, "&quot;", 6);
        else push_back(*out, c);
    }
}
static void sh_html_of(const dom_document *d, uint32_t x, vector<char> *out, int self) {
    dom_node_kind k = d->node[x].kind;
    if (self && k == DOM_NODE_KIND_TEXT) {
        uint32_t p = d->node[x].parent;
        int raw = p && (sh_named(d, p, "script") || sh_named(d, p, "style"));
        if (raw) append_range(*out, text_ptr(d, d->node[x].data), d->node[x].data.len);
        else sh_escape(out, text_ptr(d, d->node[x].data), d->node[x].data.len, 0);
        return;
    }
    if (self && k == DOM_NODE_KIND_COMMENT) {
        append_range(*out, "<!--", 4);
        append_range(*out, text_ptr(d, d->node[x].data), d->node[x].data.len);
        append_range(*out, "-->", 3);
        return;
    }
    if (self && k == DOM_NODE_KIND_ELEMENT) {
        push_back(*out, '<');
        append_range(*out, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name));
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
            push_back(*out, ' ');
            append_range(*out, names_ptr(d, d->attr[at].name), names_len(d, d->attr[at].name));
            append_range(*out, "=\"", 2);
            sh_escape(out, text_ptr(d, d->attr[at].value), d->attr[at].value.len, 1);
            push_back(*out, '"');
        }
        push_back(*out, '>');
        if (sh_void(d, x)) return;
    }
    /* a template's markup is its contents' */
    uint32_t holder = k == DOM_NODE_KIND_ELEMENT && d->node[x].content ? d->node[x].content : x;
    for (uint32_t c = d->node[holder].first_child; c; c = d->node[c].next_sibling) sh_html_of(d, c, out, 1);
    if (self && k == DOM_NODE_KIND_ELEMENT) {
        append_range(*out, "</", 2);
        append_range(*out, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name));
        push_back(*out, '>');
    }
}

/* ---- class lists, inline styles, data attributes ---- */

static int sh_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
/* the tokens of an attribute's value, as (start, length) pairs */
static void sh_tokens(const char *s, size_t n, vector<uint32_t> *out) {
    clear(*out);
    size_t i = 0;
    while (i < n) {
        while (i < n && sh_ws(s[i])) i++;
        size_t b = i;
        while (i < n && !sh_ws(s[i])) i++;
        if (i > b) { push_back(*out, (uint32_t)b); push_back(*out, (uint32_t)(i - b)); }
    }
}
/* the class attribute of x, as text into cls */
static void sh_class_text(const dom_document *d, uint32_t x, vector<char> *cls) {
    clear(*cls);
    uint32_t at = sh_attr_z(d, x, "class");
    if (at) append_range(*cls, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
}
static int sh_has_token(const vector<char> *cls, const char *t, size_t n) {
    vector<uint32_t> tk;
    sh_tokens(data(*cls), len(*cls), &tk);
    for (size_t i = 0; i < len(tk); i += 2) if (tk[i + 1] == n && memcmp(data(*cls) + tk[i], t, n) == 0) return 1;
    return 0;
}
/* cls with the token t added (add) or taken out (every one of it): written back to x */
static void sh_class_edit(dom_document *d, uint32_t x, const char *t, size_t n, int add) {
    vector<char> cls, out;
    sh_class_text(d, x, &cls);
    vector<uint32_t> tk;
    sh_tokens(data(cls), len(cls), &tk);
    int had = 0;
    for (size_t i = 0; i < len(tk); i += 2) {
        int same = tk[i + 1] == n && memcmp(data(cls) + tk[i], t, n) == 0;
        if (same) { had = 1; if (!add) continue; }
        int dup = 0;
        for (size_t j = 0; j < i; j += 2) if (tk[j + 1] == tk[i + 1] && memcmp(data(cls) + tk[j], data(cls) + tk[i], tk[i + 1]) == 0) dup = 1;
        if (dup) continue;
        if (len(out)) push_back(out, ' ');
        append_range(out, data(cls) + tk[i], tk[i + 1]);
    }
    if (add && !had) { if (len(out)) push_back(out, ' '); append_range(out, t, n); }
    dom_set_attribute(d, node_handle(d, x), "class", 5, data(out), len(out));
}
/* a token a class list takes: no white space, not empty */
static int sh_token_ok(js_vm *vm, const vector<char> *t) {
    if (!len(*t)) { sh_dom_error(vm, "SyntaxError", "the token is empty"); return 0; }
    for (size_t i = 0; i < len(*t); i++) if (sh_ws((*t)[i])) { sh_dom_error(vm, "InvalidCharacterError", "the token has white space"); return 0; }
    return 1;
}

/* a style property's name as CSS writes it: camelCase to kebab-case (cssFloat: float) */
static void sh_css_name(const vector<char> *js, vector<char> *css) {
    clear(*css);
    if (len(*js) == 8 && memcmp(data(*js), "cssFloat", 8) == 0) { append_range(*css, "float", 5); return; }
    for (size_t i = 0; i < len(*js); i++) {
        char c = (*js)[i];
        if (c >= 'A' && c <= 'Z') { push_back(*css, '-'); push_back(*css, (char)(c + 32)); }
        else push_back(*css, c);
    }
}
/* the declarations of x's style attribute: (name start, name length, value start, value length) */
static void sh_decls(const char *s, size_t n, vector<uint32_t> *out) {
    clear(*out);
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && s[e] != ';') e++;
        size_t c = i;
        while (c < e && s[c] != ':') c++;
        if (c < e) {
            size_t a = i, b = c, v = c + 1, w = e;
            while (a < b && sh_ws(s[a])) a++;
            while (b > a && sh_ws(s[b - 1])) b--;
            while (v < w && sh_ws(s[v])) v++;
            while (w > v && sh_ws(s[w - 1])) w--;
            if (b > a) { push_back(*out, (uint32_t)a); push_back(*out, (uint32_t)(b - a)); push_back(*out, (uint32_t)v); push_back(*out, (uint32_t)(w - v)); }
        }
        i = e + 1;
    }
}
static int sh_ci_eq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        char y = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + 32) : b[i];
        if (x != y) return 0;
    }
    return 1;
}
/* x's inline value of the CSS property name into v: 1 when it has one */
static int sh_style_get(const dom_document *d, uint32_t x, const vector<char> *name, vector<char> *v) {
    clear(*v);
    uint32_t at = sh_attr_z(d, x, "style");
    if (!at) return 0;
    vector<char> s;
    append_range(s, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
    vector<uint32_t> ds;
    sh_decls(data(s), len(s), &ds);
    int found = 0;
    for (size_t i = 0; i < len(ds); i += 4)
        if (ds[i + 1] == len(*name) && sh_ci_eq(data(s) + ds[i], data(*name), len(*name))) {
            clear(*v);
            append_range(*v, data(s) + ds[i + 2], ds[i + 3]);
            found = 1;
        }
    return found;
}
/* x's inline value of the CSS property name set (an empty value takes it out) */
static void sh_style_set(dom_document *d, uint32_t x, const vector<char> *name, const char *val, size_t vn) {
    vector<char> s, out;
    uint32_t at = sh_attr_z(d, x, "style");
    if (at) append_range(s, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
    vector<uint32_t> ds;
    sh_decls(data(s), len(s), &ds);
    int done = 0;
    for (size_t i = 0; i < len(ds); i += 4) {
        int same = ds[i + 1] == len(*name) && sh_ci_eq(data(s) + ds[i], data(*name), len(*name));
        if (same && (done || !vn)) continue;
        if (len(out)) push_back(out, ' ');
        append_range(out, data(s) + ds[i], ds[i + 1]);
        append_range(out, ": ", 2);
        if (same) { append_range(out, val, vn); done = 1; }
        else append_range(out, data(s) + ds[i + 2], ds[i + 3]);
        push_back(out, ';');
    }
    if (!done && vn) {
        if (len(out)) push_back(out, ' ');
        append_range(out, data(*name), len(*name));
        append_range(out, ": ", 2);
        append_range(out, val, vn);
        push_back(out, ';');
    }
    if (len(out)) dom_set_attribute(d, node_handle(d, x), "style", 5, data(out), len(out));
    else if (at) dom_remove_attribute(d, node_handle(d, x), "style", 5);
}
/* a dataset key's attribute: fooBar to data-foo-bar */
static void sh_data_name(const vector<char> *key, vector<char> *attr) {
    clear(*attr);
    append_range(*attr, "data-", 5);
    for (size_t i = 0; i < len(*key); i++) {
        char c = (*key)[i];
        if (c >= 'A' && c <= 'Z') { push_back(*attr, '-'); push_back(*attr, (char)(c + 32)); }
        else push_back(*attr, c);
    }
}

/* ---- listeners and events ---- */

/* the listener record of a listener added, by the script running now */
static uint32_t sh_listener_rec(js_vm *vm, uint32_t target, uint32_t type, int capture) {
    struct js_host *h = vm->host;
    vector<char> t;
    js_str_to_utf8(vm, type, &t);
    amb_span sp = text_add(h->rt, data(t), len(t));
    script_listener_h l = listener_new(h->rt);
    h->rt->listener[l.index].on = target == JS_WINDOW ? SCRIPT_LISTEN_ON_WINDOW : SCRIPT_LISTEN_ON_NODE;
    h->rt->listener[l.index].target = node_handle(h->d, target == JS_WINDOW ? 1 : target);
    h->rt->listener[l.index].type = sp;
    h->rt->listener[l.index].run = vm->cur_run;
    h->rt->listener[l.index].capture = (uint8_t)(capture ? 1 : 0);
    return l.index;
}
/* addEventListener(type, fn, options) on target */
static jv sh_add_listener(js_vm *vm, uint32_t target, uint32_t args, uint32_t argc) {
    jv tv = js_arg(vm, args, argc, 0), fn = js_arg(vm, args, argc, 1), opt = js_arg(vm, args, argc, 2);
    if (!js_is_callable(vm, fn)) return JV_UNDEF;
    uint32_t ts = js_to_string(vm, tv);
    if (!ts) return JV_HOLE;
    uint32_t type = js_atom(vm, ts);
    int capture = 0, once = 0;
    if (jv_is_obj(opt)) {
        capture = js_truthy(vm, js_get(vm, opt, sh_atom(vm, HA_CAPTURE)));
        once = js_truthy(vm, js_get(vm, opt, sh_atom(vm, HA_ONCE)));
    } else capture = js_truthy(vm, opt);
    for (size_t i = 0; i < len(vm->listeners); i++) {
        js_listener l = vm->listeners[i];
        if (l.live && !l.attr && l.target == target && l.type == type && l.fn == jv_idx(fn) && l.capture == (uint32_t)capture) return JV_UNDEF;
    }
    js_listener l;
    memset(&l, 0, sizeof l);
    l.target = target;
    l.type = type;
    l.fn = jv_idx(fn);
    l.capture = (uint32_t)capture;
    l.once = (uint32_t)once;
    l.live = 1;
    l.run = vm->cur_run;
    l.rec = sh_listener_rec(vm, target, type, capture);
    push_back(vm->listeners, l);
    return JV_UNDEF;
}
static void sh_listener_gone(js_vm *vm, size_t i) {
    vm->listeners[i].live = 0;
    uint32_t r = vm->listeners[i].rec;
    if (r && vm->host->rt->listener[r].live) listener_free(vm->host->rt, listener_handle(vm->host->rt, r));
}
static jv sh_remove_listener(js_vm *vm, uint32_t target, uint32_t args, uint32_t argc) {
    jv tv = js_arg(vm, args, argc, 0), fn = js_arg(vm, args, argc, 1), opt = js_arg(vm, args, argc, 2);
    if (!jv_is_obj(fn)) return JV_UNDEF;
    uint32_t ts = js_to_string(vm, tv);
    if (!ts) return JV_HOLE;
    uint32_t type = js_atom(vm, ts);
    int capture = jv_is_obj(opt) ? js_truthy(vm, js_get(vm, opt, sh_atom(vm, HA_CAPTURE))) : js_truthy(vm, opt);
    for (size_t i = 0; i < len(vm->listeners); i++) {
        js_listener l = vm->listeners[i];
        if (l.live && !l.attr && l.target == target && l.type == type && l.fn == jv_idx(fn) && l.capture == (uint32_t)capture) sh_listener_gone(vm, i);
    }
    return JV_UNDEF;
}

/* a new event object: its type, its flags, its target set at dispatch */
static jv sh_event_new(js_vm *vm, uint32_t type, uint32_t flags) {
    uint32_t e = js_obj_new(vm, JK_HOST, vm->host->protos[HP_EVENT]);
    vm->objs[e].a = flags;
    vm->objs[e].b = type;
    vm->objs[e].c = HK_EVENT;
    vm->objs[e].x = JV_NULL;
    js_define(vm, e, sh_atom(vm, HA_TYPE), jv_str(type), 0);
    js_define(vm, e, sh_atom(vm, HA_TARGET), JV_NULL, 0);
    js_define(vm, e, sh_atom(vm, HA_CURRENTTARGET), JV_NULL, 0);
    js_define(vm, e, sh_atom(vm, HA_TIMESTAMP), jv_num(vm->now), 0);
    return jv_obj(e);
}
/* what a listener on node (JS_WINDOW the window) is called with as this */
static jv sh_this_of(js_vm *vm, uint32_t node) { return node == JS_WINDOW ? jv_obj(vm->global) : sh_wrap(vm, node); }

/* fn called as a listener of ev on node: its error recorded, not passed on. 0 when the
   step budget is spent */
static int sh_call_listener(js_vm *vm, jv fn, uint32_t node, jv ev, uint32_t run) {
    uint32_t saved = vm->cur_run;
    vm->cur_run = run;
    jv args[1] = { ev };
    jv r = js_call(vm, fn, sh_this_of(vm, node), args, 1);
    vm->cur_run = saved;
    if (vm->throwing && sh_uncaught(vm, run, SCRIPT_PHASE_EVENT) == 2) return 0;
    /* an on* handler's false cancels the event */
    if (r == JV_FALSE && (vm->objs[jv_idx(ev)].a & EV_CANCELABLE)) vm->objs[jv_idx(ev)].a |= EV_PREVENTED;
    sh_checkpoint(vm);
    return vm->throwing != 2;
}
/* the handler an on<type> property of node's wrapper (of the window: the global's) holds */
static jv sh_prop_handler(js_vm *vm, uint32_t node, uint32_t type) {
    uint32_t o = 0;
    if (node == JS_WINDOW) o = vm->global;
    else if (node < len(vm->wrappers) && vm->wrappers[node] && vm->objs[vm->wrappers[node]].a == node) o = vm->wrappers[node];
    if (!o) return JV_UNDEF;
    uint32_t on = js_str_concat(vm, js_str_ascii(vm, "on"), type);
    jv v;
    uint32_t f;
    if (js_get_own(vm, o, js_atom(vm, on), &v, &f) && js_is_callable(vm, v)) return v;
    return JV_UNDEF;
}
/* the listeners of ev on one node of its path, of the phase (1 capturing, 2 at the
   target, 3 bubbling): 0 when the step budget is spent */
static int sh_listeners_at(js_vm *vm, uint32_t node, jv ev, uint32_t phase) {
    uint32_t e = jv_idx(ev), type = vm->objs[e].b;
    js_define(vm, e, sh_atom(vm, HA_CURRENTTARGET), sh_this_of(vm, node), 0);
    js_define(vm, e, sh_atom(vm, HA_EVENTPHASE), jv_num(phase), JS_SHAPE_HIDDEN);
    /* the handler of an on* property, or else of the attribute: first, not in capture */
    if (phase != 1) {
        jv ph = sh_prop_handler(vm, node, type);
        int attr_done = 0;
        if (ph != JV_UNDEF) {
            if (!sh_call_listener(vm, ph, node, ev, vm->cur_run)) return 0;
            attr_done = 1;
        }
        for (size_t i = 0; i < len(vm->listeners) && !attr_done; i++) {
            js_listener l = vm->listeners[i];
            if (l.live && l.attr && l.target == node && l.type == type) {
                if (!sh_call_listener(vm, jv_obj(l.fn), node, ev, l.run)) return 0;
                attr_done = 1;
            }
        }
        if (vm->objs[e].a & EV_STOP_NOW) return 1;
    }
    /* the listeners there now, in the order they were added */
    vector<uint32_t> now;
    for (size_t i = 0; i < len(vm->listeners); i++) {
        js_listener l = vm->listeners[i];
        if (!l.live || l.attr || l.target != node || l.type != type) continue;
        if (phase == 1 && !l.capture) continue;
        if (phase == 3 && l.capture) continue;
        push_back(now, (uint32_t)i);
    }
    for (size_t k = 0; k < len(now); k++) {
        js_listener l = vm->listeners[now[k]];
        if (!l.live) continue;
        if (l.once) sh_listener_gone(vm, now[k]);
        if (!sh_call_listener(vm, jv_obj(l.fn), node, ev, l.run)) return 0;
        if (vm->objs[e].a & EV_STOP_NOW) return 1;
    }
    return 1;
}
/* ev dispatched at target (JS_WINDOW the window): its path's listeners, in order. 1
   unless a listener cancelled it */
static int sh_dispatch(js_vm *vm, uint32_t target, jv ev) {
    dom_document *d = vm->host->d;
    uint32_t e = jv_idx(ev);
    vector<uint32_t> path;
    push_back(path, target);
    if (target != JS_WINDOW) {
        for (uint32_t p = d->node[target].parent; p; p = d->node[p].parent) push_back(path, p);
        /* the window is on the path of an event at a node of the document, load aside */
        uint32_t lt = js_atom_ascii(vm, "load");
        if (path[len(path) - 1] == 1 && vm->objs[e].b != lt) push_back(path, JS_WINDOW);
    }
    js_define(vm, e, sh_atom(vm, HA_TARGET), sh_this_of(vm, target), 0);
    for (size_t i = len(path); i > 1 && !(vm->objs[e].a & (EV_STOP | EV_STOP_NOW)); i--)
        if (!sh_listeners_at(vm, path[i - 1], ev, 1)) return 1;
    if (!(vm->objs[e].a & (EV_STOP | EV_STOP_NOW)) && !sh_listeners_at(vm, target, ev, 2)) return 1;
    if (vm->objs[e].a & EV_BUBBLES)
        for (size_t i = 1; i < len(path) && !(vm->objs[e].a & (EV_STOP | EV_STOP_NOW)); i++)
            if (!sh_listeners_at(vm, path[i], ev, 3)) return 1;
    js_define(vm, e, sh_atom(vm, HA_CURRENTTARGET), JV_NULL, 0);
    js_define(vm, e, sh_atom(vm, HA_EVENTPHASE), jv_num(0), JS_SHAPE_HIDDEN);
    return !(vm->objs[e].a & EV_PREVENTED);
}
static int sh_fire(js_vm *vm, uint32_t target, const char *type, uint32_t flags) {
    jv ev = sh_event_new(vm, js_atom_ascii(vm, type), flags | EV_TRUSTED);
    return sh_dispatch(vm, target, ev);
}

/* a click at element x: a checkbox's or radio's checked state changes before it, back
   after a cancel; a label's control is clicked after it */
static int sh_click_at(js_vm *vm, uint32_t x, uint32_t trusted) {
    dom_document *d = vm->host->d;
    int box = 0, was = 0;
    if (sh_named(d, x, "input")) {
        uint32_t ty = sh_attr_z(d, x, "type");
        if (ty && (d->attr[ty].value.len == 8 && sh_ci_eq(text_ptr(d, d->attr[ty].value), "checkbox", 8))) box = 1;
        if (ty && (d->attr[ty].value.len == 5 && sh_ci_eq(text_ptr(d, d->attr[ty].value), "radio", 5))) box = 2;
    }
    if (sh_attr_z(d, x, "disabled") && (sh_named(d, x, "button") || sh_named(d, x, "input"))) return 0;
    if (box) {
        was = sh_attr_z(d, x, "checked") != 0;
        if (box == 1 && was) dom_remove_attribute(d, node_handle(d, x), "checked", 7);
        else dom_set_attribute(d, node_handle(d, x), "checked", 7, "", 0);
    }
    int ok = sh_fire(vm, x, "click", EV_BUBBLES | EV_CANCELABLE | trusted);
    if (box && !ok) {
        if (was) dom_set_attribute(d, node_handle(d, x), "checked", 7, "", 0);
        else dom_remove_attribute(d, node_handle(d, x), "checked", 7);
    }
    if (box && ok && (box == 1 || !was)) {
        sh_fire(vm, x, "input", EV_BUBBLES | trusted);
        sh_fire(vm, x, "change", EV_BUBBLES | trusted);
    }
    if (ok && sh_named(d, x, "label")) {
        uint32_t f = sh_attr_z(d, x, "for"), ctl = 0;
        if (f) {
            vector<char> id;
            append_range(id, text_ptr(d, d->attr[f].value), d->attr[f].value.len);
            for (uint32_t y = 1; y < d->node_len && !ctl; y++) {
                if (!d->node[y].live || !sh_is_elem(d, y)) continue;
                uint32_t ia = sh_attr_z(d, y, "id");
                if (ia && d->attr[ia].value.len == len(id) && memcmp(text_ptr(d, d->attr[ia].value), data(id), len(id)) == 0 &&
                    sh_connected(d, y)) ctl = y;
            }
        }
        if (ctl && ctl != x) sh_click_at(vm, ctl, trusted);
    }
    return ok;
}

/* ---- the timers: in virtual time, each one's record kept ---- */

static void sh_timer_rec(js_vm *vm, size_t i) {
    if (!vm->host || vm->timers[i].rec) return;
    struct js_host *h = vm->host;
    js_timer t = vm->timers[i];
    script_timer_h r = timer_new(h->rt);
    h->rt->timer[r.index].run = t.run;
    double delay = t.interval >= 0 ? t.interval : t.due - vm->now;
    h->rt->timer[r.index].delay = delay > 0 ? (uint32_t)delay : 0;
    h->rt->timer[r.index].repeats = (uint8_t)(t.interval >= 0);
    h->rt->timer[r.index].state = SCRIPT_TIMER_STATE_PENDING;
    vm->timers[i].rec = r.index;
}
/* every timer not yet recorded (a microtask is not one), recorded */
static void sh_timers_seen(js_vm *vm) {
    for (size_t i = 0; i < len(vm->timers); i++) if (!vm->timers[i].rec) sh_timer_rec(vm, i);
}

/* the timers run in virtual time, in order of their due times (then of their making),
   until none is left, the clock passes until, or max have run: how many ran. An
   uncaught exception in one is recorded (or written to stderr with no page). */
uint32_t js_run_timers(js_vm *vm, double until, uint32_t max) {
    uint32_t ran = 0;
    while (ran < max) {
        if (vm->host) sh_timers_seen(vm);
        int64_t best = -1;
        for (size_t i = 0; i < len(vm->timers); i++) {
            js_timer t = vm->timers[i];
            if (!t.live) continue;
            if (best < 0 || t.due < vm->timers[(size_t)best].due || (t.due == vm->timers[(size_t)best].due && t.seq < vm->timers[(size_t)best].seq)) best = (int64_t)i;
        }
        if (best < 0) break;
        js_timer t = vm->timers[(size_t)best];
        if (t.due > until) break;
        if (t.due > vm->now) vm->now = t.due;
        if (t.interval >= 0) {
            vm->timers[(size_t)best].due = vm->now + t.interval;
            vm->timers[(size_t)best].seq = ++vm->timer_seq;
        } else vm->timers[(size_t)best].live = 0;
        vm->timers[(size_t)best].fired++;
        vector<jv> av;
        for (uint32_t k = 0; t.args && k < t.nargs; k++) push_back(av, js_array_get(vm, t.args, k));
        if (t.rec == 0xFFFFFFFEu) push_back(av, jv_num(vm->now));       /* requestAnimationFrame's time */
        uint32_t saved = vm->cur_run;
        vm->cur_run = t.run;
        if (vm->host) vm->host->phase = SCRIPT_PHASE_TIMER;
        js_call(vm, t.fn, JV_UNDEF, data(av), (uint32_t)len(av));
        vm->cur_run = saved;
        ran++;
        if (vm->throwing == 2) break;
        if (vm->throwing) sh_uncaught(vm, t.run, SCRIPT_PHASE_TIMER);
        sh_checkpoint(vm);
        if (vm->throwing == 2) break;
    }
    return ran;
}

/* ---- the host's hooks ---- */

/* ---- the view: computed values and geometry (src/script/view.c) ---- */

/* the view made up to date for a script's read: a cascade and a layout it forced are
   counted to the script whose code reads */
static void sh_view(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t before = script_view_runs(h);
    script_view_update(h);
    uint32_t r = vm->cur_run;
    if (script_view_runs(h) != before && r && r < h->rt->run_len && h->rt->run[r].live) h->rt->run[r].layouts++;
}
/* a DOMRect of x, y, width and height, its top, right, bottom and left derived */
static jv sh_rect(js_vm *vm, const double *v) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->host->protos[HP_DOMRECT]);
    static const char *const fs[] = { "x", "y", "width", "height", "top", "right", "bottom", "left" };
    double x = v[0], y = v[1], w = v[2], hh = v[3];
    double all[8] = { x, y, w, hh, hh < 0 ? y + hh : y, w < 0 ? x : x + w, hh < 0 ? y : y + hh, w < 0 ? x + w : x };
    for (size_t i = 0; i < 8; i++) js_define(vm, r, js_atom_ascii(vm, fs[i]), jv_num(all[i] == 0 ? 0 : all[i]), 0);
    return jv_obj(r);
}
/* the bounding rect of an element's client rects (CSSOM View 6.1): zeros for none; the
   first when every one is empty; else the smallest that holds every one not empty */
static void sh_bounding(const double *r, uint32_t n, double *out) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!n) return;
    int any = 0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (uint32_t i = 0; i < n; i++) {
        const double *q = r + 4 * i;
        if (q[2] == 0 || q[3] == 0) continue;
        if (!any || q[0] < x0) x0 = q[0];
        if (!any || q[1] < y0) y0 = q[1];
        if (!any || q[0] + q[2] > x1) x1 = q[0] + q[2];
        if (!any || q[1] + q[3] > y1) y1 = q[1] + q[3];
        any = 1;
    }
    if (!any) { for (int k = 0; k < 4; k++) out[k] = r[k]; return; }
    out[0] = x0; out[1] = y0; out[2] = x1 - x0; out[3] = y1 - y0;
}
#define SH_RECTS_MAX 256
static double sh_round(double v) { return floor(v + 0.5); }
/* a length's pixels as an integer property gives them: its end's and its start's rounded */
static double sh_snap(double at, double size) { return sh_round(at + size) - sh_round(at); }

/* the element's offset parent (CSSOM View 7): none for the root, the body, an element not
   rendered; else its nearest ancestor that is a td, a th, a table or the body */
static uint32_t sh_offset_parent(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    sh_view(vm);
    if (!script_view_rendered(h, x) || sh_named(d, x, "body") || x == sh_elem_child(d, 1, 0)) return 0;
    for (uint32_t p = d->node[x].parent; p && p != 1; p = d->node[p].parent)
        if (sh_named(d, p, "td") || sh_named(d, p, "th") || sh_named(d, p, "table") || sh_named(d, p, "body")) return p;
    return 0;
}
/* offsetLeft, offsetTop, offsetWidth, offsetHeight (which 0 to 3): the first box's border
   edge, from the offset parent's padding edge (or the page's, for the body or none), and
   the size of every box's bounds */
static double sh_offset(js_vm *vm, uint32_t x, uint32_t which) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    sh_view(vm);
    double r[4 * SH_RECTS_MAX], b[4];
    uint32_t n = script_view_rects(h, x, r, SH_RECTS_MAX);
    if (!n) return 0;
    if (n > SH_RECTS_MAX) n = SH_RECTS_MAX;
    sh_bounding(r, n, b);
    if (which == 2) return sh_snap(b[0], b[2]);
    if (which == 3) return sh_snap(b[1], b[3]);
    if (sh_named(d, x, "body")) return 0;
    uint32_t op = sh_offset_parent(vm, x);
    double base = 0, pb[4], pbr[4];
    if (op && !sh_named(d, op, "body") && script_view_border_box(h, op, pb) && script_view_borders(h, op, pbr))
        base = which == 0 ? pb[0] + pbr[3] : pb[1] + pbr[0];
    return sh_round((which == 0 ? r[0] : r[1]) - base);
}
/* clientLeft, clientTop, clientWidth, clientHeight (which 0 to 3): the borders' widths and
   the padding box's size; the viewport's for the root element; 0 for an inline element */
static double sh_client(js_vm *vm, uint32_t x, uint32_t which) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    sh_view(vm);
    double bb[4], br[4];
    if (!script_view_border_box(h, x, bb) || !script_view_borders(h, x, br)) return 0;
    if (x == sh_elem_child(d, 1, 0) && which >= 2) return which == 2 ? h->view_w : h->view_h;
    if (which == 0) return sh_round(br[3]);
    if (which == 1) return sh_round(br[0]);
    if (which == 2) return sh_snap(bb[0] + br[3], bb[2] - br[1] - br[3]);
    return sh_snap(bb[1] + br[0], bb[3] - br[0] - br[2]);
}

/* an element's reflected attribute: its value, or "" */
static jv sh_attr_value(js_vm *vm, const dom_document *d, uint32_t x, const char *name) {
    uint32_t at = sh_attr_z(d, x, name);
    if (!at) return jv_str(A_EMPTY);
    return sh_span(vm, d, d->attr[at].value);
}
/* a static NodeList (querySelectorAll's): the nodes of an array of node indexes, as an
   array of their wrappers whose prototype is NodeList's */
static jv sh_node_array(js_vm *vm, const vector<uint32_t> *xs) {
    uint32_t a = js_array_new(vm, 0);
    js_set_proto(vm, a, vm->host->protos[HP_NODELIST]);
    for (size_t i = 0; i < len(*xs); i++) { jv w = sh_wrap(vm, (*xs)[i]); js_array_push(vm, a, w); }
    return jv_obj(a);
}

/* ---- live collections (DOM 4.2.10, HTML 2.6.2) ----
   childNodes, children, getElementsByTagName, getElementsByClassName, getElementsByName,
   and the collections of a document (forms, images, links, anchors, scripts), a form
   (elements), a select (options), a table (rows, tBodies) and a row (cells). A
   collection is a host object (HK_LIST) over a record of the host's (h->lists): its
   root, what it holds, and a cache of its length and of the last item read with its
   index, so a walk through it in order steps from one item to the next. The cache is
   valid at the versions of the DOM it was read at: the tree's, which moves with every
   node inserted or removed, and for a collection an attribute decides (by class, by
   name, links ...) the attributes', which moves with every attribute set or removed.
   The versions follow the DOM's own mutation log, read on from where they left it, so
   a script that sets attributes in a loop over a by-tag collection keeps its cache. A
   root and a key name one collection, the same object each time it is asked for. */

enum { LC_CHILD_NODES = 1, LC_CHILDREN, LC_BY_TAG, LC_BY_CLASS, LC_BY_NAME, LC_FORMS, LC_IMAGES, LC_LINKS, LC_ANCHORS,
       LC_SCRIPTS, LC_FORM_ELEMENTS, LC_OPTIONS, LC_ROWS, LC_CELLS, LC_TBODIES };
/* the collections of a root's own children (the others are of its subtree) */
static int sh_lc_shallow(uint32_t k) { return k == LC_CHILD_NODES || k == LC_CHILDREN || k == LC_CELLS || k == LC_TBODIES; }
/* the NodeLists (the others are HTMLCollections) */
static int sh_lc_nodelist(uint32_t k) { return k == LC_CHILD_NODES || k == LC_BY_NAME; }
/* those whose items an attribute decides */
static int sh_lc_by_attr(uint32_t k) {
    return k == LC_BY_CLASS || k == LC_BY_NAME || k == LC_LINKS || k == LC_ANCHORS || k == LC_FORM_ELEMENTS;
}

/* the DOM's versions brought up to its mutation log */
static void sh_versions(struct js_host *h) {
    dom_document *d = h->d;
    size_t n = d->mutation_len;
    int tree = 0, attrs = 0;
    for (size_t i = h->log_pos; i < n; i++) {
        dom_op op = d->mutation[i].op;
        if (op == DOM_OP_APPEND_CHILD || op == DOM_OP_INSERT_BEFORE || op == DOM_OP_REMOVE_CHILD || op == DOM_OP_DESTROY) tree = 1;
        else if (op == DOM_OP_SET_ATTRIBUTE || op == DOM_OP_SET_ATTRIBUTE_NS || op == DOM_OP_REMOVE_ATTRIBUTE) attrs = 1;
    }
    h->log_pos = n;
    if (tree) h->tree_ver++;
    if (attrs) h->attr_ver++;
}

/* whether an attribute's value (s, n) holds the token (t, tn) among its white-space-separated ones */
static int sh_has_token_in(const char *s, size_t n, const char *t, size_t tn) {
    size_t i = 0;
    while (i < n) {
        while (i < n && sh_ws(s[i])) i++;
        size_t b = i;
        while (i < n && !sh_ws(s[i])) i++;
        if (i > b && i - b == tn && memcmp(s + b, t, tn) == 0) return 1;
    }
    return 0;
}
static int sh_attr_is(const dom_document *d, uint32_t x, const char *name, const char *v, size_t vn) {
    uint32_t at = sh_attr_z(d, x, name);
    return at && d->attr[at].value.len == vn && memcmp(text_ptr(d, d->attr[at].value), v, vn) == 0;
}
/* an input whose type is the one given, ASCII case-insensitively */
static int sh_input_type_is(const dom_document *d, uint32_t x, const char *t) {
    uint32_t ty = sh_attr_z(d, x, "type");
    size_t n = strlen(t);
    return ty && d->attr[ty].value.len == n && sh_ci_eq(text_ptr(d, d->attr[ty].value), t, n);
}

/* whether the collection l holds the node c, one of its root's children or descendants */
static int sh_lc_holds(const struct js_host *h, const sh_list *l, uint32_t c) {
    const dom_document *d = h->d;
    if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) return l->kind == LC_CHILD_NODES;
    const char *key = data(h->keys) + l->koff;
    switch (l->kind) {
    case LC_CHILD_NODES: case LC_CHILDREN: return 1;
    case LC_BY_TAG:
        if (l->klen == 1 && key[0] == '*') return 1;
        /* an HTML element by its name lower-cased, a foreign one by the name as given */
        if (d->node[c].ns == DOM_NAMESPACE_HTML) return l->lower && d->node[c].name == l->lower;
        return l->given && d->node[c].name == l->given;
    case LC_BY_CLASS: {
        uint32_t at = sh_attr_z(d, c, "class");
        if (!at) return 0;
        const char *s = text_ptr(d, d->attr[at].value);
        size_t sn = d->attr[at].value.len, i = 0;
        int any = 0;
        while (i < l->klen) {
            while (i < l->klen && sh_ws(key[i])) i++;
            size_t b = i;
            while (i < l->klen && !sh_ws(key[i])) i++;
            if (i == b) break;
            if (!sh_has_token_in(s, sn, key + b, i - b)) return 0;
            any = 1;
        }
        return any;
    }
    case LC_BY_NAME: return d->node[c].ns == DOM_NAMESPACE_HTML && sh_attr_is(d, c, "name", key, l->klen);
    case LC_FORMS: return sh_named(d, c, "form");
    case LC_IMAGES: return sh_named(d, c, "img");
    case LC_LINKS: return (sh_named(d, c, "a") || sh_named(d, c, "area")) && sh_attr_z(d, c, "href");
    case LC_ANCHORS: return sh_named(d, c, "a") && sh_attr_z(d, c, "name");
    case LC_SCRIPTS: return sh_named(d, c, "script");
    case LC_FORM_ELEMENTS:
        return sh_named(d, c, "button") || sh_named(d, c, "fieldset") || sh_named(d, c, "object") || sh_named(d, c, "output") ||
               sh_named(d, c, "select") || sh_named(d, c, "textarea") || (sh_named(d, c, "input") && !sh_input_type_is(d, c, "image"));
    case LC_OPTIONS: return sh_named(d, c, "option");
    case LC_ROWS: {
        /* a table's rows: its own, and its sections' (the nearest table above a row is the root) */
        if (!sh_named(d, c, "tr")) return 0;
        uint32_t p = d->node[c].parent;
        while (p && p != l->root && !sh_named(d, p, "table")) p = d->node[p].parent;
        return p == l->root;
    }
    case LC_CELLS: return sh_named(d, c, "td") || sh_named(d, c, "th");
    case LC_TBODIES: return sh_named(d, c, "tbody");
    default: return 0;
    }
}

/* the node before c in tree order within scope (scope itself not): its previous
   sibling's last descendant, or its parent */
static uint32_t sh_prev_in(const dom_document *d, uint32_t c, uint32_t scope) {
    if (c == scope) return 0;
    uint32_t p = d->node[c].prev_sibling;
    if (!p) { uint32_t up = d->node[c].parent; return up == scope ? 0 : up; }
    while (d->node[p].last_child) p = d->node[p].last_child;
    return p;
}
/* the collection's item after c in its order (its first when c is 0), and before c: 0 when none */
static uint32_t sh_lc_next(const struct js_host *h, const sh_list *l, uint32_t c) {
    const dom_document *d = h->d;
    int shallow = sh_lc_shallow(l->kind);
    for (;;) {
        if (!c) c = shallow ? d->node[l->root].first_child : sh_next_in(d, l->root, l->root);
        else c = shallow ? d->node[c].next_sibling : sh_next_in(d, c, l->root);
        if (!c || sh_lc_holds(h, l, c)) return c;
    }
}
static uint32_t sh_lc_prev(const struct js_host *h, const sh_list *l, uint32_t c) {
    const dom_document *d = h->d;
    int shallow = sh_lc_shallow(l->kind);
    for (;;) {
        c = shallow ? d->node[c].prev_sibling : sh_prev_in(d, c, l->root);
        if (!c || sh_lc_holds(h, l, c)) return c;
    }
}

/* the collection's cache made valid: when the versions it depends on moved, its length
   and its last item forgotten, and a by-tag key's names found again (an element made
   since may have brought the name) */
static void sh_lc_sync(struct js_host *h, uint32_t id) {
    sh_versions(h);
    sh_list l = h->lists[id];
    if (l.tree == h->tree_ver && (!sh_lc_by_attr(l.kind) || l.attrs == h->attr_ver)) return;
    l.tree = h->tree_ver;
    l.attrs = h->attr_ver;
    l.n = ~0u;
    l.ci = 0;
    l.cn = 0;
    if (l.kind == LC_BY_TAG) {
        const dom_document *d = h->d;
        vector<char> low;
        append_range(low, data(h->keys) + l.koff, l.klen);
        for (size_t i = 0; i < len(low); i++) if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] + 32);
        l.lower = names_find(d, data(low), len(low));
        l.given = names_find(d, data(h->keys) + l.koff, l.klen);
    }
    /* a root the page destroyed holds nothing */
    if (!h->d->node[l.root].live || h->d->node[l.root].gen != l.gen) l.n = 0;
    h->lists[id] = l;
}

/* the collection's item i, or 0: stepped to from the last item read, from its first, or
   back from the last read when that is nearer */
static uint32_t sh_lc_item(struct js_host *h, uint32_t id, uint32_t i) {
    sh_lc_sync(h, id);
    sh_list l = h->lists[id];
    if (l.n != ~0u && i >= l.n) return 0;
    uint32_t c = 0, k = 0;
    if (l.cn && i >= l.ci) { c = l.cn; k = l.ci; }
    else if (l.cn && l.ci - i < i) {
        c = l.cn;
        k = l.ci;
        while (c && k > i) { c = sh_lc_prev(h, &l, c); k--; }
        if (!c) { k = 0; c = sh_lc_next(h, &l, 0); }
    } else c = sh_lc_next(h, &l, 0);
    while (c && k < i) { c = sh_lc_next(h, &l, c); k++; }
    if (c) { h->lists[id].ci = k; h->lists[id].cn = c; }
    else h->lists[id].n = k;
    return c;
}
static uint32_t sh_lc_length(struct js_host *h, uint32_t id) {
    sh_lc_sync(h, id);
    sh_list l = h->lists[id];
    if (l.n != ~0u) return l.n;
    uint32_t c = l.cn, k = l.cn ? l.ci + 1 : 0;
    if (!c) { c = sh_lc_next(h, &l, 0); if (c) { k = 1; h->lists[id].ci = 0; h->lists[id].cn = c; } }
    while (c) { c = sh_lc_next(h, &l, c); if (c) k++; }
    h->lists[id].n = k;
    return k;
}
/* an HTMLCollection's named item: the first element of the id, or an HTML element of the name */
static uint32_t sh_lc_named(struct js_host *h, uint32_t id, const char *name, size_t n) {
    if (!n) return 0;
    sh_lc_sync(h, id);
    sh_list l = h->lists[id];
    if (l.n == 0) return 0;
    const dom_document *d = h->d;
    for (uint32_t c = sh_lc_next(h, &l, 0); c; c = sh_lc_next(h, &l, c)) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
        if (sh_attr_is(d, c, "id", name, n)) return c;
        if (d->node[c].ns == DOM_NAMESPACE_HTML && sh_attr_is(d, c, "name", name, n)) return c;
    }
    return 0;
}

/* the collection of a kind and a key under root: the one made before for them (kept
   on the root's wrapper, by a hidden name of the kind and the key), or a new one */
static jv sh_lc_get(js_vm *vm, uint32_t root, uint32_t kind, const char *key, size_t kn) {
    struct js_host *h = vm->host;
    jv w = sh_wrap(vm, root);
    if (!jv_is_obj(w)) return JV_NULL;
    vector<char> name;
    push_back(name, '\001');
    push_back(name, (char)('@' + kind));
    append_range(name, key, kn);
    uint32_t atom = js_atom(vm, js_str_utf8(vm, data(name), len(name)));
    jv had;
    uint32_t f;
    if (js_get_own(vm, jv_idx(w), atom, &had, &f) && jv_is_obj(had)) return had;
    if (!len(h->lists)) { sh_list none; memset(&none, 0, sizeof none); push_back(h->lists, none); }
    sh_list l;
    memset(&l, 0, sizeof l);
    l.root = root;
    l.gen = h->d->node[root].gen;
    l.kind = kind;
    l.koff = (uint32_t)len(h->keys);
    l.klen = (uint32_t)kn;
    append_range(h->keys, key, kn);
    l.n = ~0u;
    uint32_t id = (uint32_t)len(h->lists);
    uint32_t o = js_obj_new(vm, JK_HOST, h->protos[sh_lc_nodelist(kind) ? HP_NODELIST : HP_HTMLCOLLECTION]);
    vm->objs[o].a = id;
    vm->objs[o].c = HK_LIST;
    l.obj = o;
    push_back(h->lists, l);
    js_define(vm, jv_idx(w), atom, jv_obj(o), JS_SHAPE_HIDDEN);
    return jv_obj(o);
}
/* a collection whose key is a script's value: its text */
static jv sh_lc_keyed(js_vm *vm, uint32_t root, uint32_t kind, jv arg) {
    vector<char> key;
    if (!sh_utf8(vm, arg, &key)) return JV_HOLE;
    return sh_lc_get(vm, root, kind, data(key), len(key));
}

/* a collection's property: its length, an item by index, or an HTMLCollection's item by
   its id or name when no prototype has the name */
static jv sh_lc_prop(js_vm *vm, uint32_t o, uint32_t atom, uint32_t ha1, int *handled) {
    struct js_host *h = vm->host;
    uint32_t id = vm->objs[o].a;
    *handled = 1;
    if (ha1 == HA_LENGTH + 1) return jv_num(sh_lc_length(h, id));
    uint32_t idx;
    if (js_atom_index(vm, atom, &idx)) {
        uint32_t c = sh_lc_item(h, id, idx);
        return c ? sh_wrap(vm, c) : JV_UNDEF;
    }
    if (!js_key_is_sym(atom) && !sh_lc_nodelist(h->lists[id].kind) && !js_has(vm, vm->objs[o].proto, atom)) {
        vector<char> name;
        js_str_to_utf8(vm, atom, &name);
        uint32_t c = sh_lc_named(h, id, data(name), len(name));
        if (c) return sh_wrap(vm, c);
    }
    *handled = 0;
    return JV_UNDEF;
}

static jv sh_node_get(js_vm *vm, uint32_t x, uint32_t ha, int *handled) {
    dom_document *d = vm->host->d;
    dom_node_kind k = d->node[x].kind;
    int el = k == DOM_NODE_KIND_ELEMENT;
    *handled = 1;
    switch (ha) {
    case HA_NODETYPE: return jv_num(k == DOM_NODE_KIND_ELEMENT ? 1 : k == DOM_NODE_KIND_TEXT ? 3 : k == DOM_NODE_KIND_COMMENT ? 8 : k == DOM_NODE_KIND_DOCUMENT ? 9 : 10);
    case HA_NODENAME:
        if (el) return sh_tag_name(vm, d, x);
        return sh_cstr(vm, k == DOM_NODE_KIND_TEXT ? "#text" : k == DOM_NODE_KIND_COMMENT ? "#comment" : k == DOM_NODE_KIND_DOCUMENT ? "#document" : "html");
    case HA_TAGNAME: if (el) return sh_tag_name(vm, d, x); break;
    case HA_LOCALNAME: if (el) return sh_str(vm, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name)); break;
    case HA_NAMESPACEURI:
        if (el) return sh_cstr(vm, d->node[x].ns == DOM_NAMESPACE_SVG ? "http://www.w3.org/2000/svg" :
                                   d->node[x].ns == DOM_NAMESPACE_MATHML ? "http://www.w3.org/1998/Math/MathML" : "http://www.w3.org/1999/xhtml");
        break;
    case HA_TEXTCONTENT: case HA_INNERTEXT: {
        if (k == DOM_NODE_KIND_DOCUMENT) return JV_NULL;
        vector<char> t;
        sh_text_of(d, x, &t);
        return sh_str(vm, data(t), len(t));
    }
    case HA_NODEVALUE: case HA_DATA:
        if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) return sh_span(vm, d, d->node[x].data);
        if (ha == HA_NODEVALUE) return JV_NULL;
        break;
    case HA_LENGTH:
        if (k == DOM_NODE_KIND_TEXT || k == DOM_NODE_KIND_COMMENT) {
            jv s = sh_span(vm, d, d->node[x].data);
            return jv_num(vm->strs[jv_idx(s)].len);
        }
        break;
    case HA_INNERHTML: case HA_OUTERHTML: {
        if (!el) break;
        vector<char> t;
        sh_html_of(d, x, &t, ha == HA_OUTERHTML);
        return sh_str(vm, data(t), len(t));
    }
    case HA_PARENTNODE: return sh_wrap(vm, d->node[x].parent);
    case HA_PARENTELEMENT: { uint32_t p = d->node[x].parent; return sh_is_elem(d, p) ? sh_wrap(vm, p) : JV_NULL; }
    case HA_FIRSTCHILD: return sh_wrap(vm, d->node[x].first_child);
    case HA_LASTCHILD: return sh_wrap(vm, d->node[x].last_child);
    case HA_NEXTSIBLING: return sh_wrap(vm, d->node[x].next_sibling);
    case HA_PREVIOUSSIBLING: return sh_wrap(vm, d->node[x].prev_sibling);
    case HA_FIRSTELEMENTCHILD: return sh_wrap(vm, sh_elem_child(d, x, 0));
    case HA_LASTELEMENTCHILD: return sh_wrap(vm, sh_elem_child(d, x, 1));
    case HA_NEXTELEMENTSIBLING: return sh_wrap(vm, sh_elem_sibling(d, x, 1));
    case HA_PREVIOUSELEMENTSIBLING: return sh_wrap(vm, sh_elem_sibling(d, x, 0));
    case HA_CHILDNODES: return sh_lc_get(vm, x, LC_CHILD_NODES, "", 0);
    case HA_CHILDREN: return k == DOM_NODE_KIND_ELEMENT || k == DOM_NODE_KIND_DOCUMENT ? sh_lc_get(vm, x, LC_CHILDREN, "", 0) : JV_UNDEF;
    case HA_FORMS: if (k == DOM_NODE_KIND_DOCUMENT) return sh_lc_get(vm, x, LC_FORMS, "", 0); break;
    case HA_IMAGES: if (k == DOM_NODE_KIND_DOCUMENT) return sh_lc_get(vm, x, LC_IMAGES, "", 0); break;
    case HA_LINKS: if (k == DOM_NODE_KIND_DOCUMENT) return sh_lc_get(vm, x, LC_LINKS, "", 0); break;
    case HA_ANCHORS: if (k == DOM_NODE_KIND_DOCUMENT) return sh_lc_get(vm, x, LC_ANCHORS, "", 0); break;
    case HA_SCRIPTS: if (k == DOM_NODE_KIND_DOCUMENT) return sh_lc_get(vm, x, LC_SCRIPTS, "", 0); break;
    case HA_ELEMENTS: if (sh_named(d, x, "form") || sh_named(d, x, "fieldset")) return sh_lc_get(vm, x, LC_FORM_ELEMENTS, "", 0); break;
    case HA_OPTIONS: if (sh_named(d, x, "select") || sh_named(d, x, "datalist")) return sh_lc_get(vm, x, LC_OPTIONS, "", 0); break;
    case HA_ROWS:
        if (sh_named(d, x, "table") || sh_named(d, x, "thead") || sh_named(d, x, "tbody") || sh_named(d, x, "tfoot")) return sh_lc_get(vm, x, LC_ROWS, "", 0);
        break;
    case HA_CELLS: if (sh_named(d, x, "tr")) return sh_lc_get(vm, x, LC_CELLS, "", 0); break;
    case HA_TBODIES: if (sh_named(d, x, "table")) return sh_lc_get(vm, x, LC_TBODIES, "", 0); break;
    case HA_CHILDELEMENTCOUNT: {
        uint32_t n = 0;
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) if (d->node[c].kind == DOM_NODE_KIND_ELEMENT) n++;
        return jv_num(n);
    }
    case HA_OWNERDOCUMENT: return k == DOM_NODE_KIND_DOCUMENT ? JV_NULL : sh_wrap(vm, 1);
    case HA_ISCONNECTED: return jv_bool(sh_connected(d, x));
    case HA_BODY: if (k == DOM_NODE_KIND_DOCUMENT) return sh_wrap(vm, sh_doc_child(d, "body")); break;
    case HA_HEAD: if (k == DOM_NODE_KIND_DOCUMENT) return sh_wrap(vm, sh_doc_child(d, "head")); break;
    case HA_DOCUMENTELEMENT: if (k == DOM_NODE_KIND_DOCUMENT) return sh_wrap(vm, sh_elem_child(d, 1, 0)); break;
    case HA_DEFAULTVIEW: if (k == DOM_NODE_KIND_DOCUMENT) return jv_obj(vm->global); break;
    case HA_READYSTATE: if (k == DOM_NODE_KIND_DOCUMENT) return sh_cstr(vm, vm->host->ready >= 3 ? "complete" : vm->host->ready == 2 ? "interactive" : "loading"); break;
    case HA_TITLE:
        if (k == DOM_NODE_KIND_DOCUMENT) {
            uint32_t t = 0;
            for (uint32_t y = 1; y < d->node_len && !t; y++) if (d->node[y].live && sh_named(d, y, "title") && sh_connected(d, y)) t = y;
            vector<char> s;
            if (t) sh_text_of(d, t, &s);
            return sh_str(vm, data(s), len(s));
        }
        if (el) return sh_attr_value(vm, d, x, "title");
        break;
    case HA_ID: if (el) return sh_attr_value(vm, d, x, "id"); break;
    case HA_CLASSNAME: if (el) return sh_attr_value(vm, d, x, "class"); break;
    case HA_HREF: if (el) return sh_attr_value(vm, d, x, "href"); break;
    case HA_SRC: if (el) return sh_attr_value(vm, d, x, "src"); break;
    case HA_ALT: if (el) return sh_attr_value(vm, d, x, "alt"); break;
    case HA_NAME: if (el) return sh_attr_value(vm, d, x, "name"); break;
    case HA_PLACEHOLDER: if (el) return sh_attr_value(vm, d, x, "placeholder"); break;
    case HA_LANG: if (el) return sh_attr_value(vm, d, x, "lang"); break;
    case HA_DIR: if (el) return sh_attr_value(vm, d, x, "dir"); break;
    case HA_REL: if (el) return sh_attr_value(vm, d, x, "rel"); break;
    case HA_HTMLFOR: if (el) return sh_attr_value(vm, d, x, "for"); break;
    case HA_ACTION: if (el) return sh_attr_value(vm, d, x, "action"); break;
    case HA_METHOD: if (el) return sh_attr_value(vm, d, x, "method"); break;
    case HA_TYPE:
        if (el) {
            if (sh_named(d, x, "input") && !sh_attr_z(d, x, "type")) return sh_cstr(vm, "text");
            if (sh_named(d, x, "button") && !sh_attr_z(d, x, "type")) return sh_cstr(vm, "submit");
            return sh_attr_value(vm, d, x, "type");
        }
        break;
    case HA_VALUE:
        if (el) {
            /* a field's value as typed or set, apart from its default (the attribute, the content) */
            if ((sh_named(d, x, "input") || sh_named(d, x, "textarea")) && x < len(vm->wrappers) && vm->wrappers[x]) {
                jv cur;
                uint32_t f;
                if (js_get_own(vm, vm->wrappers[x], sh_atom(vm, HA_VALUE_STATE), &cur, &f)) return cur;
            }
            if (sh_named(d, x, "textarea")) { vector<char> t; sh_text_of(d, x, &t); return sh_str(vm, data(t), len(t)); }
            if (sh_named(d, x, "input") && (sh_attr_z(d, x, "type") == 0 || !sh_attr_z(d, x, "value"))) {
                uint32_t ty = sh_attr_z(d, x, "type");
                int box = ty && ((d->attr[ty].value.len == 8 && sh_ci_eq(text_ptr(d, d->attr[ty].value), "checkbox", 8)) ||
                                 (d->attr[ty].value.len == 5 && sh_ci_eq(text_ptr(d, d->attr[ty].value), "radio", 5)));
                if (box && !sh_attr_z(d, x, "value")) return sh_cstr(vm, "on");
            }
            return sh_attr_value(vm, d, x, "value");
        }
        break;
    case HA_CHECKED: if (el) return jv_bool(sh_attr_z(d, x, "checked") != 0); break;
    case HA_SELECTED: if (el) return jv_bool(sh_attr_z(d, x, "selected") != 0); break;
    case HA_DISABLED: if (el) return jv_bool(sh_attr_z(d, x, "disabled") != 0); break;
    case HA_HIDDEN: if (el) return jv_bool(sh_attr_z(d, x, "hidden") != 0); break;
    case HA_READONLY: if (el) return jv_bool(sh_attr_z(d, x, "readonly") != 0); break;
    case HA_REQUIRED: if (el) return jv_bool(sh_attr_z(d, x, "required") != 0); break;
    case HA_TABINDEX:
        if (el) {
            uint32_t ti = sh_attr_z(d, x, "tabindex");
            if (ti) { vector<char> t; append_range(t, text_ptr(d, d->attr[ti].value), d->attr[ti].value.len); push_back(t, 0); return jv_num(atoi(data(t))); }
            int f = sh_named(d, x, "button") || sh_named(d, x, "input") || sh_named(d, x, "select") || sh_named(d, x, "textarea") ||
                    ((sh_named(d, x, "a") || sh_named(d, x, "area")) && sh_attr_z(d, x, "href"));
            return jv_num(f ? 0 : -1);
        }
        break;
    case HA_OFFSETPARENT: if (el) return sh_wrap(vm, sh_offset_parent(vm, x)); break;
    case HA_OFFSETLEFT: if (el) return jv_num(sh_offset(vm, x, 0)); break;
    case HA_OFFSETTOP: if (el) return jv_num(sh_offset(vm, x, 1)); break;
    case HA_OFFSETWIDTH: if (el) return jv_num(sh_offset(vm, x, 2)); break;
    case HA_OFFSETHEIGHT: if (el) return jv_num(sh_offset(vm, x, 3)); break;
    case HA_CLIENTLEFT: if (el) return jv_num(sh_client(vm, x, 0)); break;
    case HA_CLIENTTOP: if (el) return jv_num(sh_client(vm, x, 1)); break;
    case HA_CLIENTWIDTH: if (el) return jv_num(sh_client(vm, x, 2)); break;
    case HA_CLIENTHEIGHT: if (el) return jv_num(sh_client(vm, x, 3)); break;
    case HA_SCROLLTOP: case HA_SCROLLLEFT: if (el) return jv_num(0); break;
    case HA_SCROLLWIDTH: case HA_SCROLLHEIGHT:
        /* the viewport does not scroll: the root's is the page's, at least the viewport's; an
           element's its padding box's */
        if (el && x == sh_elem_child(d, 1, 0)) {
            double bb[4];
            sh_view(vm);
            double v = ha == HA_SCROLLWIDTH ? vm->host->view_w : vm->host->view_h;
            if (script_view_border_box(vm->host, x, bb)) { double e = ha == HA_SCROLLWIDTH ? bb[0] + bb[2] : bb[1] + bb[3]; if (e > v) v = sh_round(e); }
            return jv_num(v);
        }
        if (el) return jv_num(sh_client(vm, x, ha == HA_SCROLLWIDTH ? 2 : 3));
        break;
    case HA_CLASSLIST: if (el) return sh_host_obj(vm, HK_CLASSLIST, HP_CLASSLIST, x); break;
    case HA_STYLE: if (el) return sh_host_obj(vm, HK_STYLE, HP_STYLE, x); break;
    case HA_DATASET: if (el) return sh_host_obj(vm, HK_DATASET, HP_DATASET, x); break;
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

jv js_host_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled) {
    *handled = 0;
    if (!vm->host || !vm->host->ready) return JV_UNDEF;
    uint32_t kind = vm->objs[o].c;
    uint32_t ha1 = sh_name_of(vm, atom);
    if (kind == HK_EVENT) {
        uint32_t f = vm->objs[o].a;
        *handled = 1;
        if (ha1 == HA_DEFAULTPREVENTED + 1) return jv_bool(f & EV_PREVENTED);
        if (ha1 == HA_BUBBLES + 1) return jv_bool(f & EV_BUBBLES);
        if (ha1 == HA_CANCELABLE + 1) return jv_bool(f & EV_CANCELABLE);
        if (ha1 == HA_ISTRUSTED + 1) return jv_bool(f & EV_TRUSTED);
        if (ha1 == HA_DETAIL + 1) return vm->objs[o].x;
        *handled = 0;
        return JV_UNDEF;
    }
    if (kind == HK_LIST) return sh_lc_prop(vm, o, atom, ha1, handled);
    if (kind == HK_COMPUTED) {
        /* a computed style's property: its resolved value, by its name camel-cased or as CSS
           writes it; the empty string for a property the cascade does not compute */
        uint32_t x = sh_owner(vm, o);
        if (!x) { *handled = 1; return JV_HOLE; }
        uint32_t idx;
        if (js_key_is_sym(atom) || js_atom_index(vm, atom, &idx) || js_has(vm, vm->objs[o].proto, atom)) return JV_UNDEF;
        *handled = 1;
        if (ha1 == HA_CSSTEXT + 1) return jv_str(A_EMPTY);
        if (ha1 == HA_LENGTH + 1) return jv_num(script_view_props());
        vector<char> name, prop, v;
        js_str_to_utf8(vm, atom, &name);
        sh_css_name(&name, &prop);
        sh_view(vm);
        script_view_style(vm->host, x, data(prop), len(prop), &v);
        return sh_str(vm, data(v), len(v));
    }
    if (kind == HK_NODE) {
        uint32_t x = sh_node(vm, jv_obj(o), 0);
        if (!x) { *handled = 1; return JV_HOLE; }
        if (!ha1) return JV_UNDEF;
        return sh_node_get(vm, x, ha1 - 1, handled);
    }
    if (kind == HK_CLASSLIST || kind == HK_STYLE || kind == HK_DATASET) {
        uint32_t x = sh_owner(vm, o);
        if (!x) { *handled = 1; return JV_HOLE; }
        dom_document *d = vm->host->d;
        if (kind == HK_CLASSLIST) {
            if (ha1 == HA_LENGTH + 1) {
                vector<char> cls;
                sh_class_text(d, x, &cls);
                vector<uint32_t> tk;
                sh_tokens(data(cls), len(cls), &tk);
                *handled = 1;
                return jv_num((double)(len(tk) / 2));
            }
            if (ha1 == HA_VALUE + 1) { *handled = 1; return sh_attr_value(vm, d, x, "class"); }
            uint32_t idx;
            if (js_atom_index(vm, atom, &idx)) {
                vector<char> cls;
                sh_class_text(d, x, &cls);
                vector<uint32_t> tk;
                sh_tokens(data(cls), len(cls), &tk);
                *handled = 1;
                if ((size_t)idx * 2 >= len(tk)) return JV_UNDEF;
                return sh_str(vm, data(cls) + tk[idx * 2], tk[idx * 2 + 1]);
            }
            return JV_UNDEF;
        }
        /* a style's property, or a dataset's key: the name read, its value found */
        jv ownv;
        uint32_t f;
        if (js_get_own(vm, o, atom, &ownv, &f)) return JV_UNDEF;
        if (js_has(vm, vm->host->protos[kind == HK_STYLE ? HP_STYLE : HP_DATASET], atom)) return JV_UNDEF;
        vector<char> name, prop, v;
        js_str_to_utf8(vm, atom, &name);
        if (kind == HK_STYLE) {
            if (ha1 == HA_CSSTEXT + 1) { *handled = 1; return sh_attr_value(vm, d, x, "style"); }
            if (ha1 == HA_LENGTH + 1) {
                uint32_t at = sh_attr_z(d, x, "style");
                vector<char> s;
                if (at) append_range(s, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
                vector<uint32_t> ds;
                sh_decls(data(s), len(s), &ds);
                *handled = 1;
                return jv_num((double)(len(ds) / 4));
            }
            sh_css_name(&name, &prop);
            *handled = 1;
            sh_style_get(d, x, &prop, &v);
            return sh_str(vm, data(v), len(v));
        }
        sh_data_name(&name, &prop);
        uint32_t at = sh_attr(d, x, data(prop), len(prop));
        *handled = 1;
        if (!at) return JV_UNDEF;
        return sh_span(vm, d, d->attr[at].value);
    }
    return JV_UNDEF;
}

/* an attribute of x set to v's string */
static int sh_set_attr_v(js_vm *vm, uint32_t x, const char *name, jv v) {
    vector<char> t;
    if (!sh_utf8(vm, v, &t)) return 0;
    dom_document *d = vm->host->d;
    dom_set_attribute(d, node_handle(d, x), name, strlen(name), data(t), len(t));
    return 1;
}
static void sh_set_flag(js_vm *vm, uint32_t x, const char *name, jv v) {
    dom_document *d = vm->host->d;
    if (js_truthy(vm, v)) dom_set_attribute(d, node_handle(d, x), name, strlen(name), "", 0);
    else dom_remove_attribute(d, node_handle(d, x), name, strlen(name));
}

int js_host_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled) {
    *handled = 0;
    if (!vm->host || !vm->host->ready) return 1;
    uint32_t kind = vm->objs[o].c;
    uint32_t ha1 = sh_name_of(vm, atom);
    dom_document *d = vm->host->d;
    /* a computed style is read-only */
    if (kind == HK_COMPUTED) {
        uint32_t idx;
        if (js_key_is_sym(atom) || js_atom_index(vm, atom, &idx) || js_has(vm, vm->objs[o].proto, atom)) return 1;
        vector<char> name;
        js_str_to_utf8(vm, atom, &name);
        push_back(name, 0);
        char msg[300];
        snprintf(msg, sizeof msg, "Failed to set the '%s' property on 'CSSStyleDeclaration': These styles are computed, and therefore the '%s' property is read-only.", data(name), data(name));
        *handled = 1;
        sh_dom_error(vm, "NoModificationAllowedError", msg);
        return 0;
    }
    /* a collection's items and length have no setter: a write to one does nothing */
    if (kind == HK_LIST) {
        uint32_t idx;
        if (ha1 == HA_LENGTH + 1 || js_atom_index(vm, atom, &idx)) *handled = 1;
        return 1;
    }
    if (kind == HK_NODE) {
        uint32_t x = sh_node(vm, jv_obj(o), 0);
        if (!x) { *handled = 1; return 0; }
        dom_node_kind k = d->node[x].kind;
        int el = k == DOM_NODE_KIND_ELEMENT;
        if (!ha1) {
            /* an on* property given a function: a listener of that event, recorded */
            vector<char> nm;
            js_str_to_utf8(vm, atom, &nm);
            if (len(nm) > 2 && nm[0] == 'o' && nm[1] == 'n' && js_is_callable(vm, v)) {
                uint32_t type = js_atom(vm, js_str_utf8(vm, data(nm) + 2, len(nm) - 2));
                int known = 0;
                for (size_t i = 0; i < len(vm->listeners); i++) {
                    js_listener l = vm->listeners[i];
                    if (l.live && l.attr == 2 && l.target == x && l.type == type) known = 1;
                }
                if (!known) {
                    js_listener l;
                    memset(&l, 0, sizeof l);
                    l.target = x;
                    l.type = type;
                    l.attr = 2;
                    l.live = 0;
                    l.run = vm->cur_run;
                    l.rec = sh_listener_rec(vm, x, type, 0);
                    push_back(vm->listeners, l);
                }
            }
            return 1;
        }
        uint32_t ha = ha1 - 1;
        *handled = 1;
        switch (ha) {
        case HA_TEXTCONTENT: case HA_INNERTEXT: case HA_NODEVALUE: case HA_DATA: {
            if (k == DOM_NODE_KIND_DOCUMENT) return 1;
            if ((ha == HA_NODEVALUE || ha == HA_DATA) && el) return 1;
            vector<char> t;
            if (v != JV_NULL && !sh_utf8(vm, v, &t)) return 0;
            sh_set_text(d, x, data(t), len(t));
            return 1;
        }
        case HA_INNERHTML: {
            if (!el) return 1;
            vector<char> t;
            if (!sh_utf8(vm, v, &t)) return 0;
            sh_empty(d, x);
            sh_insert_html(d, x, 0, data(t), len(t));
            return 1;
        }
        case HA_ID: if (el) return sh_set_attr_v(vm, x, "id", v); break;
        case HA_CLASSNAME: if (el) return sh_set_attr_v(vm, x, "class", v); break;
        case HA_TITLE: if (el) return sh_set_attr_v(vm, x, "title", v); break;
        case HA_HREF: if (el) return sh_set_attr_v(vm, x, "href", v); break;
        case HA_SRC: if (el) return sh_set_attr_v(vm, x, "src", v); break;
        case HA_ALT: if (el) return sh_set_attr_v(vm, x, "alt", v); break;
        case HA_NAME: if (el) return sh_set_attr_v(vm, x, "name", v); break;
        case HA_TYPE: if (el) return sh_set_attr_v(vm, x, "type", v); break;
        case HA_PLACEHOLDER: if (el) return sh_set_attr_v(vm, x, "placeholder", v); break;
        case HA_LANG: if (el) return sh_set_attr_v(vm, x, "lang", v); break;
        case HA_DIR: if (el) return sh_set_attr_v(vm, x, "dir", v); break;
        case HA_REL: if (el) return sh_set_attr_v(vm, x, "rel", v); break;
        case HA_HTMLFOR: if (el) return sh_set_attr_v(vm, x, "for", v); break;
        case HA_TABINDEX: if (el) return sh_set_attr_v(vm, x, "tabindex", v); break;
        case HA_VALUE:
            if (el && (sh_named(d, x, "input") || sh_named(d, x, "textarea"))) {
                /* the field's own value, not its default: the attribute is left as it was */
                uint32_t s0 = jv_is_str(v) ? jv_idx(v) : js_to_string(vm, v);
                if (!s0) return 0;
                js_define(vm, o, sh_atom(vm, HA_VALUE_STATE), jv_str(s0), JS_SHAPE_HIDDEN);
                return 1;
            }
            if (el) return sh_set_attr_v(vm, x, "value", v);
            break;
        case HA_CHECKED: if (el) { sh_set_flag(vm, x, "checked", v); return 1; } break;
        case HA_SELECTED: if (el) { sh_set_flag(vm, x, "selected", v); return 1; } break;
        case HA_DISABLED: if (el) { sh_set_flag(vm, x, "disabled", v); return 1; } break;
        case HA_HIDDEN: if (el) { sh_set_flag(vm, x, "hidden", v); return 1; } break;
        case HA_READONLY: if (el) { sh_set_flag(vm, x, "readonly", v); return 1; } break;
        case HA_REQUIRED: if (el) { sh_set_flag(vm, x, "required", v); return 1; } break;
        case HA_TITLE + HA_N: break;
        default: break;
        }
        /* the document's title: its title element's text */
        if (ha == HA_TITLE && k == DOM_NODE_KIND_DOCUMENT) {
            uint32_t t = 0;
            for (uint32_t y = 1; y < d->node_len && !t; y++) if (d->node[y].live && sh_named(d, y, "title") && sh_connected(d, y)) t = y;
            vector<char> s;
            if (!sh_utf8(vm, v, &s)) return 0;
            if (t) sh_set_text(d, t, data(s), len(s));
            return 1;
        }
        *handled = 0;
        return 1;
    }
    if (kind == HK_STYLE || kind == HK_DATASET) {
        uint32_t x = sh_owner(vm, o);
        if (!x) { *handled = 1; return 0; }
        vector<char> name, prop, t;
        js_str_to_utf8(vm, atom, &name);
        *handled = 1;
        if (kind == HK_STYLE && ha1 == HA_CSSTEXT + 1) return sh_set_attr_v(vm, x, "style", v);
        if (v != JV_NULL && !sh_utf8(vm, v, &t)) return 0;
        if (kind == HK_STYLE) { sh_css_name(&name, &prop); sh_style_set(d, x, &prop, data(t), len(t)); return 1; }
        sh_data_name(&name, &prop);
        dom_set_attribute(d, node_handle(d, x), data(prop), len(prop), data(t), len(t));
        return 1;
    }
    if (kind == HK_CLASSLIST && ha1 == HA_VALUE + 1) {
        uint32_t x = sh_owner(vm, o);
        *handled = 1;
        return x ? sh_set_attr_v(vm, x, "class", v) : 0;
    }
    return 1;
}

/* the nodes in tree order under scope (scope itself not) that match: the first only
   when first is set */
static void sh_query(js_vm *vm, uint32_t scope, const vector<char> *sel, int first, vector<uint32_t> *out) {
    dom_document *d = vm->host->d;
    clear(*out);
    for (uint32_t c = sh_next_in(d, scope, scope); c; c = sh_next_in(d, c, scope)) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
        if (css_selector_matches(d, c, data(*sel), len(*sel))) { push_back(*out, c); if (first) return; }
    }
}
/* a selector argument: its text, or a SyntaxError for an empty one */
static int sh_selector(js_vm *vm, uint32_t args, uint32_t argc, vector<char> *sel) {
    if (!sh_utf8(vm, js_arg(vm, args, argc, 0), sel)) return 0;
    size_t i = 0;
    while (i < len(*sel) && sh_ws((*sel)[i])) i++;
    if (i == len(*sel)) { sh_dom_error(vm, "SyntaxError", "the selector is empty"); return 0; }
    int depth = 0;
    for (size_t k = 0; k < len(*sel); k++) {
        char c = (*sel)[k];
        if (c == '(' || c == '[') depth++;
        if (c == ')' || c == ']') depth--;
        if (depth < 0) break;
    }
    if (depth != 0) { sh_dom_error(vm, "SyntaxError", "the selector is not valid"); return 0; }
    return 1;
}
/* the nodes of append/prepend/before/after's arguments: a node, or a string as text */
static int sh_arg_nodes(js_vm *vm, uint32_t args, uint32_t argc, vector<uint32_t> *out) {
    dom_document *d = vm->host->d;
    clear(*out);
    for (uint32_t i = 0; i < argc; i++) {
        jv a = js_arg(vm, args, argc, i);
        uint32_t x = sh_node(vm, a, 0);
        if (vm->throwing) return 0;
        if (x) { push_back(*out, x); continue; }
        vector<char> t;
        if (!sh_utf8(vm, a, &t)) return 0;
        dom_node_h tn = dom_create_text(d, data(t), len(t));
        push_back(*out, tn.index);
    }
    return 1;
}

jv js_host_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)callee;
    *handled = 1;
    if (!vm->host || !vm->host->ready) { *handled = 0; return JV_UNDEF; }
    dom_document *d = vm->host->d;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    /* the window's own */
    if (id == H_WIN_ADD) return sh_add_listener(vm, JS_WINDOW, args, argc);
    if (id == H_WIN_REMOVE) return sh_remove_listener(vm, JS_WINDOW, args, argc);
    if (id == H_WIN_DISPATCH) {
        if (!jv_is_obj(a0) || vm->objs[jv_idx(a0)].c != HK_EVENT) return js_throw_msg(vm, vm->type_error_proto, "dispatchEvent needs an Event");
        return jv_bool(sh_dispatch(vm, JS_WINDOW, a0));
    }
    if (id == H_RAF) {
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        js_timer t;
        memset(&t, 0, sizeof t);
        t.id = ++vm->timer_id;
        t.due = vm->now + 16;
        t.interval = -1;
        t.fn = a0;
        t.live = 1;
        t.seq = ++vm->timer_seq;
        t.run = vm->cur_run;
        t.rec = 0xFFFFFFFEu;
        push_back(vm->timers, t);
        return jv_num(t.id);
    }
    if (id == H_CAF) {
        double tid = js_to_number(vm, a0);
        for (size_t i = 0; i < len(vm->timers); i++) if (vm->timers[i].id == (uint32_t)tid) vm->timers[i].live = 0;
        return JV_UNDEF;
    }
    if (id == H_ALERT) {
        vector<char> t;
        if (!sh_utf8(vm, argc ? a0 : jv_str(A_EMPTY), &t)) return JV_HOLE;
        js_console(vm, 1, js_str_utf8(vm, data(t), len(t)));
        return id == H_ALERT ? JV_UNDEF : JV_UNDEF;
    }
    if (id == H_ILLEGAL) return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    if (id == H_EVENT || id == H_CUSTOM_EVENT) {
        uint32_t ts = js_to_string(vm, a0);
        if (!ts) return JV_HOLE;
        uint32_t f = 0;
        if (jv_is_obj(a1)) {
            if (js_truthy(vm, js_get(vm, a1, sh_atom(vm, HA_BUBBLES)))) f |= EV_BUBBLES;
            if (js_truthy(vm, js_get(vm, a1, sh_atom(vm, HA_CANCELABLE)))) f |= EV_CANCELABLE;
        }
        jv ev = sh_event_new(vm, js_atom(vm, ts), f);
        if (id == H_CUSTOM_EVENT && jv_is_obj(a1)) {
            jv det = js_get(vm, a1, sh_atom(vm, HA_DETAIL));
            vm->objs[jv_idx(ev)].x = det;
        }
        return ev;
    }
    if (id == H_COMPUTED) {
        /* the element's resolved values, read when a property is (CSSOM 9.1): live */
        uint32_t x = sh_node(vm, a0, 0);
        if (vm->throwing) return JV_HOLE;
        if (!x || !sh_is_elem(d, x))
            return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'getComputedStyle' on 'Window': parameter 1 is not of type 'Element'.");
        return sh_host_obj(vm, HK_COMPUTED, HP_STYLE, x);
    }
    if (id == H_RECT_JSON) {
        /* a DOMRect's toJSON: its eight numbers */
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        static const char *const fs[] = { "x", "y", "width", "height", "top", "right", "bottom", "left" };
        for (size_t i = 0; i < 8; i++) {
            uint32_t at = js_atom_ascii(vm, fs[i]);
            jv v = js_get(vm, thisv, at);
            if (vm->throwing) return JV_HOLE;
            js_define(vm, o, at, v, 0);
        }
        return jv_obj(o);
    }
    /* an event's */
    if (id == H_EV_PREVENT || id == H_EV_STOP || id == H_EV_STOP_NOW) {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].c != HK_EVENT) return JV_UNDEF;
        uint32_t e = jv_idx(thisv);
        if (id == H_EV_PREVENT && (vm->objs[e].a & EV_CANCELABLE) && !(vm->objs[e].a & EV_PASSIVE)) vm->objs[e].a |= EV_PREVENTED;
        if (id == H_EV_STOP) vm->objs[e].a |= EV_STOP;
        if (id == H_EV_STOP_NOW) vm->objs[e].a |= EV_STOP | EV_STOP_NOW;
        return JV_UNDEF;
    }
    /* a class list's */
    if (id >= H_CL_ADD && id <= H_CL_TOSTRING) {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].c != HK_CLASSLIST) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t x = sh_owner(vm, jv_idx(thisv));
        if (!x) return JV_HOLE;
        vector<char> cls;
        sh_class_text(d, x, &cls);
        if (id == H_CL_TOSTRING) return sh_str(vm, data(cls), len(cls));
        if (id == H_CL_ITEM) {
            vector<uint32_t> tk;
            sh_tokens(data(cls), len(cls), &tk);
            double i = js_to_number(vm, a0);
            if (!(i >= 0) || (size_t)i * 2 >= len(tk)) return JV_NULL;
            return sh_str(vm, data(cls) + tk[(size_t)i * 2], tk[(size_t)i * 2 + 1]);
        }
        vector<char> t;
        if (id == H_CL_ADD || id == H_CL_REMOVE) {
            for (uint32_t i = 0; i < argc; i++) {
                if (!sh_utf8(vm, js_arg(vm, args, argc, i), &t) || !sh_token_ok(vm, &t)) return JV_HOLE;
                sh_class_edit(d, x, data(t), len(t), id == H_CL_ADD);
            }
            return JV_UNDEF;
        }
        if (!sh_utf8(vm, a0, &t) || !sh_token_ok(vm, &t)) return JV_HOLE;
        int has = sh_has_token(&cls, data(t), len(t));
        if (id == H_CL_CONTAINS) return jv_bool(has);
        if (id == H_CL_TOGGLE) {
            int want = argc > 1 ? js_truthy(vm, a1) : !has;
            if (want != has) sh_class_edit(d, x, data(t), len(t), want);
            return jv_bool(want);
        }
        /* replace(old, new) */
        vector<char> nt;
        if (!sh_utf8(vm, a1, &nt) || !sh_token_ok(vm, &nt)) return JV_HOLE;
        if (!has) return JV_FALSE;
        vector<uint32_t> tk;
        sh_tokens(data(cls), len(cls), &tk);
        vector<char> out;
        int put = 0;
        for (size_t i = 0; i < len(tk); i += 2) {
            const char *w = data(cls) + tk[i];
            size_t wn = tk[i + 1];
            int is_old = wn == len(t) && memcmp(w, data(t), wn) == 0, is_new = wn == len(nt) && memcmp(w, data(nt), wn) == 0;
            if (is_old || is_new) {
                if (put) continue;
                put = 1;
                if (len(out)) push_back(out, ' ');
                append_range(out, data(nt), len(nt));
                continue;
            }
            if (len(out)) push_back(out, ' ');
            append_range(out, w, wn);
        }
        dom_set_attribute(d, node_handle(d, x), "class", 5, data(out), len(out));
        return JV_TRUE;
    }
    /* a style's */
    if ((id == H_ST_SET || id == H_ST_GET || id == H_ST_REMOVE) && jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_HOST &&
        vm->objs[jv_idx(thisv)].c == HK_COMPUTED) {
        uint32_t x = sh_owner(vm, jv_idx(thisv));
        if (!x) return JV_HOLE;
        vector<char> name, v;
        if (!sh_utf8(vm, a0, &name)) return JV_HOLE;
        if (id != H_ST_GET) {
            push_back(name, 0);
            char msg[300];
            snprintf(msg, sizeof msg, "Failed to execute '%s' on 'CSSStyleDeclaration': These styles are computed, and therefore the '%s' property is read-only.",
                     id == H_ST_SET ? "setProperty" : "removeProperty", data(name));
            return sh_dom_error(vm, "NoModificationAllowedError", msg);
        }
        for (size_t i = 0; i < len(name); i++) if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
        sh_view(vm);
        script_view_style(vm->host, x, data(name), len(name), &v);
        return sh_str(vm, data(v), len(v));
    }
    if (id == H_ST_SET || id == H_ST_GET || id == H_ST_REMOVE) {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].c != HK_STYLE) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t x = sh_owner(vm, jv_idx(thisv));
        if (!x) return JV_HOLE;
        vector<char> name, v;
        if (!sh_utf8(vm, a0, &name)) return JV_HOLE;
        if (id == H_ST_GET) { sh_style_get(d, x, &name, &v); return sh_str(vm, data(v), len(v)); }
        if (id == H_ST_REMOVE) {
            sh_style_get(d, x, &name, &v);
            jv old = sh_str(vm, data(v), len(v));
            sh_style_set(d, x, &name, "", 0);
            return old;
        }
        if (a1 != JV_NULL && a1 != JV_UNDEF && !sh_utf8(vm, a1, &v)) return JV_HOLE;
        sh_style_set(d, x, &name, data(v), len(v));
        return JV_UNDEF;
    }
    /* a collection's: a live one, or querySelectorAll's array of the NodeList prototype */
    if (id == H_LIST_ITEM || id == H_LIST_NAMED) {
        if (!argc) return js_throw_msg(vm, vm->type_error_proto, id == H_LIST_ITEM ? "Failed to execute 'item': 1 argument required, but only 0 present."
                                                                               : "Failed to execute 'namedItem': 1 argument required, but only 0 present.");
        int live = jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_HOST && vm->objs[jv_idx(thisv)].c == HK_LIST;
        int fixed = jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_ARRAY;
        if (!live && !fixed) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        if (id == H_LIST_ITEM) {
            /* WebIDL's unsigned long: the number modulo 2^32 */
            double n = js_to_number(vm, a0);
            if (vm->throwing) return JV_HOLE;
            double m = n != n || n == 1.0 / 0.0 || n == -1.0 / 0.0 ? 0 : fmod(n < 0 ? -floor(-n) : floor(n), 4294967296.0);
            if (m < 0) m += 4294967296.0;
            uint32_t i = (uint32_t)m;
            if (fixed) return i < vm->objs[jv_idx(thisv)].b ? js_array_get(vm, jv_idx(thisv), i) : JV_NULL;
            uint32_t c = sh_lc_item(vm->host, vm->objs[jv_idx(thisv)].a, i);
            return c ? sh_wrap(vm, c) : JV_NULL;
        }
        vector<char> name;
        if (!sh_utf8(vm, a0, &name)) return JV_HOLE;
        if (fixed) return JV_NULL;
        uint32_t c = sh_lc_named(vm->host, vm->objs[jv_idx(thisv)].a, data(name), len(name));
        return c ? sh_wrap(vm, c) : JV_NULL;
    }
    /* a node's, an element's, the document's: this a live node */
    uint32_t x = sh_node(vm, thisv, 0);
    if (!x) {
        if (vm->throwing) return JV_HOLE;
        return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation: this is not a Node");
    }
    dom_node_kind k = d->node[x].kind;
    int el = k == DOM_NODE_KIND_ELEMENT, doc = k == DOM_NODE_KIND_DOCUMENT;
    switch (id) {
    case H_APPEND_CHILD: {
        uint32_t c = sh_node(vm, a0, 1);
        if (!c) return JV_HOLE;
        dom_status st = sh_insert(d, x, c, 0);
        if (st != DOM_OK) return sh_status(vm, st);
        return a0;
    }
    case H_INSERT_BEFORE: {
        uint32_t c = sh_node(vm, a0, 1);
        if (!c) return JV_HOLE;
        uint32_t b = jv_is_nullish(a1) ? 0 : sh_node(vm, a1, 1);
        if (vm->throwing) return JV_HOLE;
        dom_status st = sh_insert(d, x, c, b);
        if (st != DOM_OK) return sh_status(vm, st);
        return a0;
    }
    case H_REMOVE_CHILD: {
        uint32_t c = sh_node(vm, a0, 1);
        if (!c) return JV_HOLE;
        if (d->node[c].parent != x) return sh_status(vm, DOM_NOT_A_CHILD);
        dom_status st = dom_remove_child(d, node_handle(d, x), node_handle(d, c));
        if (st != DOM_OK) return sh_status(vm, st);
        return a0;
    }
    case H_REPLACE_CHILD: {
        uint32_t nw = sh_node(vm, a0, 1);
        if (!nw) return JV_HOLE;
        uint32_t old = sh_node(vm, a1, 1);
        if (!old) return JV_HOLE;
        if (d->node[old].parent != x) return sh_status(vm, DOM_NOT_A_CHILD);
        if (nw == old) return a1;
        uint32_t next = d->node[old].next_sibling;
        if (next == nw) next = d->node[nw].next_sibling;
        dom_status st = dom_remove_child(d, node_handle(d, x), node_handle(d, old));
        if (st == DOM_OK) st = sh_insert(d, x, nw, next);
        if (st != DOM_OK) return sh_status(vm, st);
        return a1;
    }
    case H_CONTAINS: {
        if (jv_is_nullish(a0)) return JV_FALSE;
        uint32_t c = sh_node(vm, a0, 1);
        if (!c) return JV_HOLE;
        while (c && c != x) c = d->node[c].parent;
        return jv_bool(c == x);
    }
    case H_HAS_CHILD_NODES: return jv_bool(d->node[x].first_child != 0);
    case H_CLONE_NODE: {
        if (doc) return sh_dom_error(vm, "NotSupportedError", "the document cannot be cloned");
        vector<char> buf;
        (void)buf;
        uint32_t c = sh_import(d, d, x, js_truthy(vm, a0));
        return sh_wrap(vm, c);
    }
    case H_ADD_LISTENER: return sh_add_listener(vm, x, args, argc);
    case H_REMOVE_LISTENER: return sh_remove_listener(vm, x, args, argc);
    case H_DISPATCH: {
        if (!jv_is_obj(a0) || vm->objs[jv_idx(a0)].c != HK_EVENT) return js_throw_msg(vm, vm->type_error_proto, "dispatchEvent needs an Event");
        return jv_bool(sh_dispatch(vm, x, a0));
    }
    case H_REMOVE: {
        uint32_t p = d->node[x].parent;
        if (p) dom_remove_child(d, node_handle(d, p), node_handle(d, x));
        return JV_UNDEF;
    }
    case H_BEFORE: case H_AFTER: case H_REPLACE_WITH: {
        uint32_t p = d->node[x].parent;
        vector<uint32_t> ns;
        if (!sh_arg_nodes(vm, args, argc, &ns)) return JV_HOLE;
        if (!p) return JV_UNDEF;
        uint32_t at = id == H_BEFORE ? x : d->node[x].next_sibling;
        /* the reference not among those moved */
        for (int moved = 1; moved && at; ) {
            moved = 0;
            for (size_t i = 0; i < len(ns); i++) if (ns[i] == at) moved = 1;
            if (moved) at = d->node[at].next_sibling;
        }
        for (size_t i = 0; i < len(ns); i++) {
            dom_status st = sh_insert(d, p, ns[i], at);
            if (st != DOM_OK) return sh_status(vm, st);
        }
        if (id == H_REPLACE_WITH) {
            int kept = 0;
            for (size_t i = 0; i < len(ns); i++) if (ns[i] == x) kept = 1;
            if (!kept && d->node[x].parent == p) dom_remove_child(d, node_handle(d, p), node_handle(d, x));
        }
        return JV_UNDEF;
    }
    case H_APPEND: case H_PREPEND: {
        vector<uint32_t> ns;
        if (!sh_arg_nodes(vm, args, argc, &ns)) return JV_HOLE;
        uint32_t at = id == H_PREPEND ? d->node[x].first_child : 0;
        for (size_t i = 0; i < len(ns); i++) {
            dom_status st = sh_insert(d, x, ns[i], at == ns[i] ? d->node[at].next_sibling : at);
            if (st != DOM_OK) return sh_status(vm, st);
        }
        return JV_UNDEF;
    }
    case H_QS: case H_QSA: {
        vector<char> sel;
        if (!sh_selector(vm, args, argc, &sel)) return JV_HOLE;
        vector<uint32_t> xs;
        sh_query(vm, x, &sel, id == H_QS, &xs);
        if (id == H_QS) return len(xs) ? sh_wrap(vm, xs[0]) : JV_NULL;
        return sh_node_array(vm, &xs);
    }
    case H_BY_TAG: if (!el && !doc) break; return sh_lc_keyed(vm, x, LC_BY_TAG, a0);
    case H_BY_CLASS: if (!el && !doc) break; return sh_lc_keyed(vm, x, LC_BY_CLASS, a0);
    case H_BY_NAME: if (!doc) break; return sh_lc_keyed(vm, x, LC_BY_NAME, a0);
    case H_BY_ID: {
        vector<char> want;
        if (!sh_utf8(vm, a0, &want)) return JV_HOLE;
        for (uint32_t c = sh_next_in(d, x, x); c; c = sh_next_in(d, c, x)) {
            uint32_t ia = d->node[c].kind == DOM_NODE_KIND_ELEMENT ? sh_attr_z(d, c, "id") : 0;
            if (ia && d->attr[ia].value.len == len(want) && memcmp(text_ptr(d, d->attr[ia].value), data(want), len(want)) == 0) return sh_wrap(vm, c);
        }
        return JV_NULL;
    }
    case H_CREATE_ELEMENT: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        dom_node_h h = dom_create_element(d, data(nm), len(nm));
        if (!h.index) return sh_status(vm, DOM_INVALID_NAME);
        return sh_wrap(vm, h.index);
    }
    case H_CREATE_TEXT: case H_CREATE_COMMENT: {
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        dom_node_h h = id == H_CREATE_TEXT ? dom_create_text(d, data(t), len(t)) : dom_create_comment(d, data(t), len(t));
        return sh_wrap(vm, h.index);
    }
    default: break;
    }
    if (!el) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation: this is not an Element");
    switch (id) {
    case H_GET_ATTR: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        uint32_t at = sh_attr(d, x, data(nm), len(nm));
        return at ? sh_span(vm, d, d->attr[at].value) : JV_NULL;
    }
    case H_HAS_ATTR: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        return jv_bool(sh_attr(d, x, data(nm), len(nm)) != 0);
    }
    case H_SET_ATTR: {
        vector<char> nm, v;
        if (!sh_utf8(vm, a0, &nm) || !sh_utf8(vm, a1, &v)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        dom_status st = dom_set_attribute(d, node_handle(d, x), data(nm), len(nm), data(v), len(v));
        if (st != DOM_OK) return sh_status(vm, st);
        return JV_UNDEF;
    }
    case H_REMOVE_ATTR: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        dom_remove_attribute(d, node_handle(d, x), data(nm), len(nm));
        return JV_UNDEF;
    }
    case H_TOGGLE_ATTR: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        for (size_t i = 0; i < len(nm); i++) if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] = (char)(nm[i] + 32);
        int has = sh_attr(d, x, data(nm), len(nm)) != 0;
        int want = argc > 1 ? js_truthy(vm, a1) : !has;
        if (want && !has) dom_set_attribute(d, node_handle(d, x), data(nm), len(nm), "", 0);
        if (!want && has) dom_remove_attribute(d, node_handle(d, x), data(nm), len(nm));
        return jv_bool(want);
    }
    case H_ATTR_NAMES: {
        uint32_t a = js_array_new(vm, 0);
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
            jv s = sh_str(vm, names_ptr(d, d->attr[at].name), names_len(d, d->attr[at].name));
            js_array_push(vm, a, s);
        }
        return jv_obj(a);
    }
    case H_MATCHES: case H_CLOSEST: {
        vector<char> sel;
        if (!sh_selector(vm, args, argc, &sel)) return JV_HOLE;
        if (id == H_MATCHES) return jv_bool(css_selector_matches(d, x, data(sel), len(sel)));
        for (uint32_t c = x; sh_is_elem(d, c); c = d->node[c].parent) if (css_selector_matches(d, c, data(sel), len(sel))) return sh_wrap(vm, c);
        return JV_NULL;
    }
    case H_CLICK: sh_click_at(vm, x, 0); return JV_UNDEF;
    case H_FOCUS: case H_BLUR: return JV_UNDEF;
    case H_RECT: case H_CLIENT_RECTS: {
        /* the cascade and the layout as the page is now (run if the DOM changed since) */
        sh_view(vm);
        double r[4 * SH_RECTS_MAX], b[4];
        uint32_t n = script_view_rects(vm->host, x, r, SH_RECTS_MAX);
        if (n > SH_RECTS_MAX) n = SH_RECTS_MAX;
        if (id == H_RECT) { sh_bounding(r, n, b); return sh_rect(vm, b); }
        uint32_t a = js_array_new(vm, 0);
        js_set_proto(vm, a, vm->host->protos[HP_RECTLIST]);
        for (uint32_t i = 0; i < n; i++) { jv q = sh_rect(vm, r + 4 * i); js_array_push(vm, a, q); }
        return jv_obj(a);
    }
    case H_INSERT_ADJ_HTML: {
        vector<char> where, html;
        if (!sh_utf8(vm, a0, &where) || !sh_utf8(vm, a1, &html)) return JV_HOLE;
        for (size_t i = 0; i < len(where); i++) if (where[i] >= 'A' && where[i] <= 'Z') where[i] = (char)(where[i] + 32);
        push_back(where, 0);
        uint32_t p = d->node[x].parent;
        if (strcmp(data(where), "beforebegin") == 0 && p) sh_insert_html(d, p, x, data(html), len(html));
        else if (strcmp(data(where), "afterbegin") == 0) sh_insert_html(d, x, d->node[x].first_child, data(html), len(html));
        else if (strcmp(data(where), "beforeend") == 0) sh_insert_html(d, x, 0, data(html), len(html));
        else if (strcmp(data(where), "afterend") == 0 && p) sh_insert_html(d, p, d->node[x].next_sibling, data(html), len(html));
        else return sh_dom_error(vm, "SyntaxError", "insertAdjacentHTML's position is none of beforebegin afterbegin beforeend afterend");
        return JV_UNDEF;
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* ---- the page's globals and prototypes ---- */

static void sh_method(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t nargs) {
    uint32_t a = js_atom_ascii(vm, name);
    uint32_t f = js_native_new(vm, id, a, nargs);
    js_define(vm, o, a, jv_obj(f), JS_SHAPE_HIDDEN);
}
/* a constructor of the page's interfaces (Node, Element ...): its prototype p */
static uint32_t sh_interface(js_vm *vm, const char *name, uint32_t id, uint32_t p) {
    uint32_t a = js_atom_ascii(vm, name);
    uint32_t f = js_native_new(vm, id, a, 0);
    js_define(vm, f, A_PROTOTYPE, jv_obj(p), JS_SHAPE_HIDDEN);
    js_define(vm, p, A_CONSTRUCTOR, jv_obj(f), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, a, jv_obj(f), JS_SHAPE_HIDDEN);
    return f;
}

/* the VM made for the page: the host's names, its prototypes, window and document */
static void sh_ready(js_vm *vm) {
    struct js_host *h = vm->host;
    if (h->ready) return;
    /* the VM was zeroed with its host set (SCRIPT_HOST): made ready, the host kept */
    js_setup(vm);
    resize(vm->hatoms, HA_N, 0u);
    for (uint32_t i = 0; i < HA_N; i++) {
        uint32_t a = js_atom_ascii(vm, sh_names[i]);
        vm->hatoms[i] = a;
        if ((size_t)HA_N + a >= len(vm->hatoms)) resize(vm->hatoms, (size_t)HA_N + a + 1, 0u);
        vm->hatoms[HA_N + a] = i + 1;
    }
    uint32_t node = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    uint32_t elem = js_obj_new(vm, JK_PLAIN, node);
    uint32_t chr = js_obj_new(vm, JK_PLAIN, node);
    uint32_t docp = js_obj_new(vm, JK_PLAIN, node);
    vm->host->protos[HP_NODE] = node;
    vm->host->protos[HP_ELEMENT] = elem;
    vm->host->protos[HP_CHARDATA] = chr;
    vm->host->protos[HP_DOCUMENT] = docp;
    vm->host->protos[HP_CLASSLIST] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_STYLE] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_DATASET] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_EVENT] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_NODELIST] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_HTMLCOLLECTION] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_DOMRECT] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host->protos[HP_RECTLIST] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->host_kinds = HP_N;
    sh_method(vm, node, "appendChild", H_APPEND_CHILD, 1);
    sh_method(vm, node, "insertBefore", H_INSERT_BEFORE, 2);
    sh_method(vm, node, "removeChild", H_REMOVE_CHILD, 1);
    sh_method(vm, node, "replaceChild", H_REPLACE_CHILD, 2);
    sh_method(vm, node, "contains", H_CONTAINS, 1);
    sh_method(vm, node, "hasChildNodes", H_HAS_CHILD_NODES, 0);
    sh_method(vm, node, "cloneNode", H_CLONE_NODE, 1);
    sh_method(vm, node, "addEventListener", H_ADD_LISTENER, 2);
    sh_method(vm, node, "removeEventListener", H_REMOVE_LISTENER, 2);
    sh_method(vm, node, "dispatchEvent", H_DISPATCH, 1);
    sh_method(vm, node, "remove", H_REMOVE, 0);
    sh_method(vm, node, "before", H_BEFORE, 0);
    sh_method(vm, node, "after", H_AFTER, 0);
    sh_method(vm, node, "replaceWith", H_REPLACE_WITH, 0);
    sh_method(vm, node, "append", H_APPEND, 0);
    sh_method(vm, node, "prepend", H_PREPEND, 0);
    sh_method(vm, node, "querySelector", H_QS, 1);
    sh_method(vm, node, "querySelectorAll", H_QSA, 1);
    sh_method(vm, node, "getElementsByTagName", H_BY_TAG, 1);
    sh_method(vm, node, "getElementsByClassName", H_BY_CLASS, 1);
    sh_method(vm, elem, "getAttribute", H_GET_ATTR, 1);
    sh_method(vm, elem, "setAttribute", H_SET_ATTR, 2);
    sh_method(vm, elem, "removeAttribute", H_REMOVE_ATTR, 1);
    sh_method(vm, elem, "hasAttribute", H_HAS_ATTR, 1);
    sh_method(vm, elem, "toggleAttribute", H_TOGGLE_ATTR, 1);
    sh_method(vm, elem, "getAttributeNames", H_ATTR_NAMES, 0);
    sh_method(vm, elem, "matches", H_MATCHES, 1);
    sh_method(vm, elem, "closest", H_CLOSEST, 1);
    sh_method(vm, elem, "click", H_CLICK, 0);
    sh_method(vm, elem, "focus", H_FOCUS, 0);
    sh_method(vm, elem, "blur", H_BLUR, 0);
    sh_method(vm, elem, "getBoundingClientRect", H_RECT, 0);
    sh_method(vm, elem, "getClientRects", H_CLIENT_RECTS, 0);
    sh_method(vm, elem, "insertAdjacentHTML", H_INSERT_ADJ_HTML, 2);
    sh_method(vm, docp, "getElementById", H_BY_ID, 1);
    sh_method(vm, docp, "createElement", H_CREATE_ELEMENT, 1);
    sh_method(vm, docp, "createTextNode", H_CREATE_TEXT, 1);
    sh_method(vm, docp, "createComment", H_CREATE_COMMENT, 1);
    sh_method(vm, docp, "getElementsByName", H_BY_NAME, 1);
    /* the collections: item and namedItem their own; a NodeList's forEach and iterators,
       and an HTMLCollection's iterator, Array.prototype's (WebIDL's indexed iterables) */
    uint32_t nlp = vm->host->protos[HP_NODELIST], hcp = vm->host->protos[HP_HTMLCOLLECTION];
    sh_method(vm, nlp, "item", H_LIST_ITEM, 1);
    sh_method(vm, hcp, "item", H_LIST_ITEM, 1);
    sh_method(vm, hcp, "namedItem", H_LIST_NAMED, 1);
    static const char *const iters[] = { "forEach", "entries", "keys", "values" };
    for (size_t i = 0; i < 4; i++) {
        uint32_t a = js_atom_ascii(vm, iters[i]);
        js_define(vm, nlp, a, js_get(vm, jv_obj(vm->array_proto), a), JS_SHAPE_HIDDEN);
    }
    jv values = js_get(vm, jv_obj(vm->array_proto), js_atom_ascii(vm, "values"));
    js_define(vm, nlp, JS_KEY_ITERATOR, values, JS_SHAPE_HIDDEN);
    js_define(vm, hcp, JS_KEY_ITERATOR, values, JS_SHAPE_HIDDEN);
    js_define(vm, nlp, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, "NodeList"), JS_SHAPE_HIDDEN);
    js_define(vm, hcp, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, "HTMLCollection"), JS_SHAPE_HIDDEN);
    uint32_t cl = vm->host->protos[HP_CLASSLIST];
    sh_method(vm, cl, "add", H_CL_ADD, 1);
    sh_method(vm, cl, "remove", H_CL_REMOVE, 1);
    sh_method(vm, cl, "toggle", H_CL_TOGGLE, 1);
    sh_method(vm, cl, "contains", H_CL_CONTAINS, 1);
    sh_method(vm, cl, "replace", H_CL_REPLACE, 2);
    sh_method(vm, cl, "item", H_CL_ITEM, 1);
    sh_method(vm, cl, "toString", H_CL_TOSTRING, 0);
    uint32_t sp = vm->host->protos[HP_STYLE];
    sh_method(vm, sp, "setProperty", H_ST_SET, 2);
    sh_method(vm, sp, "getPropertyValue", H_ST_GET, 1);
    sh_method(vm, sp, "removeProperty", H_ST_REMOVE, 1);
    uint32_t ep = vm->host->protos[HP_EVENT];
    sh_method(vm, ep, "preventDefault", H_EV_PREVENT, 0);
    sh_method(vm, ep, "stopPropagation", H_EV_STOP, 0);
    sh_method(vm, ep, "stopImmediatePropagation", H_EV_STOP_NOW, 0);
    /* the interfaces, for instanceof */
    sh_interface(vm, "Node", H_ILLEGAL, node);
    sh_interface(vm, "Element", H_ILLEGAL, elem);
    sh_interface(vm, "HTMLElement", H_ILLEGAL, elem);
    sh_interface(vm, "CharacterData", H_ILLEGAL, chr);
    sh_interface(vm, "Text", H_ILLEGAL, chr);
    sh_interface(vm, "Document", H_ILLEGAL, docp);
    sh_interface(vm, "DOMTokenList", H_ILLEGAL, cl);
    sh_interface(vm, "CSSStyleDeclaration", H_ILLEGAL, sp);
    sh_interface(vm, "NodeList", H_ILLEGAL, nlp);
    /* DOMRect, and getClientRects' list of them: an array of DOMRectList's prototype */
    uint32_t drp = vm->host->protos[HP_DOMRECT], rlp = vm->host->protos[HP_RECTLIST];
    sh_method(vm, drp, "toJSON", H_RECT_JSON, 0);
    sh_method(vm, rlp, "item", H_LIST_ITEM, 1);
    js_define(vm, rlp, JS_KEY_ITERATOR, values, JS_SHAPE_HIDDEN);
    sh_interface(vm, "DOMRect", H_ILLEGAL, drp);
    sh_interface(vm, "DOMRectList", H_ILLEGAL, rlp);
    sh_interface(vm, "HTMLCollection", H_ILLEGAL, hcp);
    sh_interface(vm, "Event", H_EVENT, ep);
    uint32_t cep = js_obj_new(vm, JK_PLAIN, ep);
    sh_interface(vm, "CustomEvent", H_CUSTOM_EVENT, cep);
    /* the window: the global object */
    uint32_t g = vm->global;
    js_define(vm, g, sh_atom(vm, HA_WINDOW), jv_obj(g), JS_SHAPE_HIDDEN);
    js_define(vm, g, sh_atom(vm, HA_SELF), jv_obj(g), JS_SHAPE_HIDDEN);
    h->ready = 1;
    jv dw = sh_wrap(vm, 1);
    js_define(vm, g, sh_atom(vm, HA_DOCUMENT), dw, JS_SHAPE_HIDDEN);
    sh_method(vm, g, "addEventListener", H_WIN_ADD, 2);
    sh_method(vm, g, "removeEventListener", H_WIN_REMOVE, 2);
    sh_method(vm, g, "dispatchEvent", H_WIN_DISPATCH, 1);
    sh_method(vm, g, "requestAnimationFrame", H_RAF, 1);
    sh_method(vm, g, "cancelAnimationFrame", H_CAF, 1);
    sh_method(vm, g, "getComputedStyle", H_COMPUTED, 1);
    sh_method(vm, g, "alert", H_ALERT, 1);
    js_define(vm, g, js_atom_ascii(vm, "innerWidth"), jv_num(h->view_w), JS_SHAPE_HIDDEN);
    js_define(vm, g, js_atom_ascii(vm, "innerHeight"), jv_num(h->view_h), JS_SHAPE_HIDDEN);
    /* the viewport does not scroll */
    static const char *const scrolls[] = { "scrollX", "scrollY", "pageXOffset", "pageYOffset" };
    for (size_t i = 0; i < 4; i++) js_define(vm, g, js_atom_ascii(vm, scrolls[i]), jv_num(0), JS_SHAPE_HIDDEN);
    js_define(vm, g, js_atom_ascii(vm, "devicePixelRatio"), jv_num(1), JS_SHAPE_HIDDEN);
    /* location: the page's URL */
    uint32_t loc = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    if (h->page && h->page < h->nt->resource_len) {
        amb_span u = h->nt->resource[h->page].final_url;
        jv us = sh_str(vm, text_ptr(h->nt, u), u.len);
        js_define(vm, loc, sh_atom(vm, HA_HREF), us, 0);
    }
    js_define(vm, g, js_atom_ascii(vm, "location"), jv_obj(loc), JS_SHAPE_HIDDEN);
    uint32_t nav = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, nav, js_atom_ascii(vm, "userAgent"), sh_cstr(vm, "Ambitious/0.1 (a reasoning-native browser)"), 0);
    js_define(vm, nav, js_atom_ascii(vm, "language"), sh_cstr(vm, "en"), 0);
    js_define(vm, g, js_atom_ascii(vm, "navigator"), jv_obj(nav), JS_SHAPE_HIDDEN);
}

/* ---- the page's scripts ---- */

static int sh_script_type_ok(const dom_document *d, uint32_t x) {
    uint32_t ty = sh_attr_z(d, x, "type");
    if (!ty) return 1;
    amb_span v = d->attr[ty].value;
    const char *t = text_ptr(d, v);
    size_t n = v.len;
    while (n && sh_ws(t[n - 1])) n--;
    size_t i = 0;
    while (i < n && sh_ws(t[i])) i++;
    t += i;
    n -= i;
    if (n == 0) return 1;
    static const char *const ok[] = { "text/javascript", "application/javascript", "text/ecmascript", "application/ecmascript",
                                      "application/x-javascript", "text/x-javascript", "text/jscript", "text/livescript" };
    for (size_t k = 0; k < sizeof ok / sizeof ok[0]; k++) if (n == strlen(ok[k]) && sh_ci_eq(t, ok[k], n)) return 1;
    return 0;
}

/* a script's source run, its record's outcome and its errors set */
static void sh_run_source(js_vm *vm, uint32_t run, const char *src, size_t n) {
    struct js_host *h = vm->host;
    vm->cur_run = run;
    h->phase = SCRIPT_PHASE_SCRIPT;
    h->nscripts++;
    int rc = js_eval(vm, src, n, "page", h->nscripts);
    script_outcome oc = rc == 0 ? SCRIPT_OUTCOME_RAN : rc == -1 ? SCRIPT_OUTCOME_SYNTAX_ERROR : rc == -3 ? SCRIPT_OUTCOME_STOPPED : SCRIPT_OUTCOME_THREW;
    h->rt->run[run].outcome = oc;
    if (rc == -1 || rc == -2) sh_error_rec(vm, run, SCRIPT_PHASE_SCRIPT, data(vm->out), len(vm->out));
    if (rc != -3) sh_checkpoint(vm);
    vm->cur_run = 0;
}

/* the on* attributes of the page's elements: each a handler compiled as a function of
   event, a listener and a run of source attribute */
static void sh_attr_handlers(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT) continue;
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
            uint32_t na = d->attr[at].name;
            size_t nl = names_len(d, na);
            if (nl < 3 || names_ptr(d, na)[0] != 'o' || names_ptr(d, na)[1] != 'n') continue;
            if (!sh_connected(d, x)) break;     /* a template's contents are inert */
            vector<char> type, body;
            append_range(type, names_ptr(d, na) + 2, nl - 2);
            append_range(body, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
            uint32_t run = sh_run_new(vm, x, SCRIPT_SOURCE_ATTRIBUTE, "", 0);
            uint32_t func = 0;
            vector<char> err;
            h->nscripts++;
            if (!js_compile(vm, data(body), len(body), h->nscripts, 1, &func, &err)) {
                h->rt->run[run].outcome = SCRIPT_OUTCOME_SYNTAX_ERROR;
                sh_error_rec(vm, run, SCRIPT_PHASE_SCRIPT, data(err), len(err));
                continue;
            }
            uint32_t c = js_closure_new(vm, func, 0, JV_UNDEF);
            js_listener l;
            memset(&l, 0, sizeof l);
            l.target = x;
            l.type = js_atom(vm, js_str_utf8(vm, data(type), len(type)));
            l.fn = c;
            l.live = 1;
            l.attr = 1;
            l.run = run;
            vm->cur_run = run;
            l.rec = sh_listener_rec(vm, x, l.type, 0);
            vm->cur_run = 0;
            push_back(vm->listeners, l);
        }
    }
}

static int sh_has_scripts(const dom_document *d) {
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT) continue;
        if (names_is(d, d->node[x].name, "script", 6)) return 1;
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
            uint32_t na = d->attr[at].name;
            if (names_len(d, na) > 2 && names_ptr(d, na)[0] == 'o' && names_ptr(d, na)[1] == 'n') return 1;
        }
    }
    return 0;
}

uint32_t script_run_page(js_vm *vm, double horizon) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    if (!sh_has_scripts(d)) return 0;
    sh_ready(vm);
    sh_attr_handlers(vm);
    /* the scripts there were when the parse ended, in document order */
    vector<uint32_t> xs;
    for (uint32_t x = sh_next_in(d, 1, 1); x; x = sh_next_in(d, x, 1)) if (sh_script_elem(d, x)) push_back(xs, x);
    uint32_t ran = 0;
    for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) {
        uint32_t x = xs[i];
        if (!d->node[x].live) continue;
        uint32_t sa = sh_attr_z(d, x, "src");
        if (!sh_script_type_ok(d, x)) {
            uint32_t r = sh_run_new(vm, x, sa ? SCRIPT_SOURCE_EXTERNAL : SCRIPT_SOURCE_INLINE, "", 0);
            h->rt->run[r].outcome = SCRIPT_OUTCOME_NOT_RUN;
            continue;
        }
        vector<char> src;
        if (sa) {
            /* the loader's fetch of it: the resource this element asked for */
            uint32_t res = 0;
            for (uint32_t r = 1; r < h->nt->resource_len && !res; r++)
                if (h->nt->resource[r].live && h->nt->resource[r].kind == NET_KIND_SCRIPT && h->nt->resource[r].elem.index == x) res = r;
            vector<char> url;
            if (res) append_range(url, text_ptr(h->nt, h->nt->resource[res].url), h->nt->resource[res].url.len);
            else append_range(url, text_ptr(d, d->attr[sa].value), d->attr[sa].value.len);
            uint32_t run = sh_run_new(vm, x, SCRIPT_SOURCE_EXTERNAL, data(url), len(url));
            if (!res || h->nt->resource[res].outcome != NET_OUTCOME_OK) { h->rt->run[run].outcome = SCRIPT_OUTCOME_NOT_FETCHED; continue; }
            append_range(src, bytes_ptr(h->nt, h->nt->resource[res].body), h->nt->resource[res].body.len);
            sh_run_source(vm, run, data(src), len(src));
        } else {
            sh_text_of(d, x, &src);
            uint32_t run = sh_run_new(vm, x, SCRIPT_SOURCE_INLINE, "", 0);
            sh_run_source(vm, run, data(src), len(src));
        }
        ran++;
    }
    vm->throwing = 0;
    /* the page parsed and its scripts run: DOMContentLoaded at the document, load at the window */
    h->ready = 2;
    h->phase = SCRIPT_PHASE_EVENT;
    sh_fire(vm, 1, "DOMContentLoaded", EV_BUBBLES);
    vm->throwing = 0;
    h->ready = 3;
    sh_fire(vm, JS_WINDOW, "load", 0);
    vm->throwing = 0;
    js_run_timers(vm, vm->now + horizon, 100000);
    vm->throwing = 0;
    sh_timers_seen(vm);
    return ran;
}

int script_click(js_vm *vm, uint32_t x, double horizon) {
    struct js_host *h = vm->host;
    if (!sh_is_elem(h->d, x)) return 0;
    if (!h->ready) sh_ready(vm);
    h->phase = SCRIPT_PHASE_EVENT;
    sh_click_at(vm, x, EV_TRUSTED);
    vm->throwing = 0;
    js_run_timers(vm, vm->now + horizon, 100000);
    vm->throwing = 0;
    sh_timers_seen(vm);
    return 1;
}

uint32_t script_select(const dom_document *d, const char *sel, size_t n) {
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1))
        if (d->node[c].kind == DOM_NODE_KIND_ELEMENT && css_selector_matches(d, c, sel, n)) return c;
    return 0;
}

void script_settle(js_vm *vm) {
    struct js_host *h = vm->host;
    if (!h || !h->ready) return;
    sh_timers_seen(vm);
    for (size_t i = 0; i < len(vm->timers); i++) {
        js_timer t = vm->timers[i];
        if (!t.rec || t.rec >= 0xFFFFFFFEu || !h->rt->timer[t.rec].live) continue;
        h->rt->timer[t.rec].fired = t.fired;
        h->rt->timer[t.rec].state = t.live ? SCRIPT_TIMER_STATE_PENDING : (t.fired && t.interval < 0 ? SCRIPT_TIMER_STATE_FIRED : SCRIPT_TIMER_STATE_CLEARED);
    }
    /* a property handler is a listener as long as its function is there */
    for (size_t i = 0; i < len(vm->listeners); i++) {
        js_listener l = vm->listeners[i];
        if (l.attr != 2 || !l.rec || !h->rt->listener[l.rec].live) continue;
        if (sh_prop_handler(vm, l.target, l.type) == JV_UNDEF) listener_free(h->rt, listener_handle(h->rt, l.rec));
    }
    /* the nodes no tree holds: destroyed, their subtrees with them */
    dom_document *d = h->d;
    for (uint32_t x = 2; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].parent || d->node[x].kind == DOM_NODE_KIND_DOCUMENT) continue;
        dom_destroy(d, node_handle(d, x));
    }
    /* a listener on a destroyed node listens on nothing: its record goes with it */
    for (uint32_t l = 1; l < h->rt->listener_len; l++) {
        if (!h->rt->listener[l].live || h->rt->listener[l].on != SCRIPT_LISTEN_ON_NODE) continue;
        dom_node_h t = h->rt->listener[l].target;
        if (t.index >= d->node_len || !d->node[t.index].live || d->node[t.index].gen != t.gen) listener_free(h->rt, listener_handle(h->rt, l));
    }
}
