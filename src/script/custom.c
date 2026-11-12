/* src/script/custom.c -- custom elements (HTML 4.13) and shadow trees (DOM 4.8): the
   page's own elements, their classes and lifecycle callbacks, and the trees of their own
   an element may host.

   CUSTOM ELEMENTS. customElements.define(name, class) checks the name (a valid custom
   element name: a lower-case ASCII letter first, a hyphen in it, none of the names SVG and
   MathML keep), that neither it nor the class is defined, the class's prototype's
   lifecycle callbacks (connectedCallback, disconnectedCallback, adoptedCallback,
   attributeChangedCallback) and its observedAttributes; then every element of the name in
   the document (its shadow trees too, in shadow-including tree order) is upgraded, and
   whenDefined's promise for the name resolved. An upgrade runs the class's constructor on
   the element (its super() call, HTMLElement's constructor, answers the element itself),
   then its attributeChangedCallback for each attribute it observes the element has, then
   its connectedCallback when the element is connected. document.createElement and new
   make an element of a defined name by its constructor at once; an element the parser,
   innerHTML or createElement made before its definition is upgraded at the definition
   when it is in the document, else when it comes into one (customElements.upgrade does it
   for a subtree). The callbacks come at the end of each call into the host (the
   standard's [CEReactions]): a custom element connected (connectedCallback), removed from
   the document (disconnectedCallback), an attribute it observes set or removed
   (attributeChangedCallback, with the old value, the new and the namespace). An error a
   constructor or a callback throws is reported; the element's upgrade then failed.
   HTMLElement's constructor reads new.target from the frame of the constructor calling
   super() (the engine gives a host native none).

   SHADOW TREES. attachShadow({mode}) on an element that may host one (an autonomous custom
   element, or article, aside, blockquote, body, div, footer, h1-h6, header, main, nav, p,
   section, span) makes its shadow root: the DOM's attach_shadow, a fragment that is the
   root of a tree of its own. shadowRoot answers an open one; the root's host and mode;
   innerHTML, getElementById and the ParentNode methods on it. A slot's assigned nodes are
   its host's children whose slot attribute names it (the default slot: those with none),
   flattened on asking; assignedSlot and slot. An event's path goes from a slotted node to
   its slot, from a shadow root to its host when the event is composed, and each listener
   sees the target retargeted to the host of the tree the listener is not in;
   composedPath() is the path.

   What is not done: shadow trees are not rendered (the cascade and the layout walk the
   document's own tree); customized built-in elements (extends, is=); form-associated
   custom elements; slotchange events. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "host.h"
#include "script.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);

enum {
    SC_HTML_ELEMENT = SH_CUSTOM_FIRST, SC_DEFINE, SC_GET, SC_WHEN_DEFINED, SC_UPGRADE, SC_ATTACH_SHADOW, SC_SHADOW_ROOT, SC_HOST,
    SC_MODE, SC_ASSIGNED_NODES, SC_ASSIGNED_ELEMENTS, SC_ASSIGNED_SLOT, SC_COMPOSED_PATH, SC_ILLEGAL, SC_N
};
/* an element's state (HTML 4.13.3): undefined, failed, custom */
enum { SC_UNDEFINED = 1, SC_FAILED, SC_CUSTOM };

static uint32_t sc_atom(js_vm *vm, const char *s) { return js_atom_ascii(vm, s); }

/* ---- the DOM read: names, shadow roots, connectedness ---- */

/* an element's local name as text */
static void sc_name(const dom_document *d, uint32_t x, vector<char> *out) {
    clear(*out);
    append_range(*out, names_ptr(d, d->node[x].name), names_len(d, d->node[x].name));
}
/* a valid custom element name (HTML 4.13.2): a lower-case ASCII letter first, a hyphen,
   no ASCII upper case, not one of the names other specifications keep */
static int sc_valid_name(const char *s, size_t n) {
    if (!n || s[0] < 'a' || s[0] > 'z') return 0;
    int hyphen = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') return 0;
        if (c == '-') hyphen = 1;
        if (c < 0x80 && !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')) return 0;
    }
    static const char *const kept[] = { "annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri", "font-face-format",
                                        "font-face-name", "missing-glyph" };
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) if (n == strlen(kept[i]) && memcmp(s, kept[i], n) == 0) return 0;
    return hyphen;
}
/* the shadow root x is the root of (its record's place), 0 when x is none */
static uint32_t sc_shadow_of_root(const dom_document *d, uint32_t x) {
    if (!x || d->node[x].kind != DOM_NODE_KIND_FRAGMENT || !d->node[x].host) return 0;
    for (uint32_t s = 1; s < d->shadow_len; s++) if (d->shadow[s].live && d->shadow[s].root == x) return s;
    return 0;
}
/* the shadow root an element hosts (its record's place), 0 for none */
static uint32_t sc_shadow_of_host(const dom_document *d, uint32_t x) {
    for (uint32_t s = 1; s < d->shadow_len; s++) if (d->shadow[s].live && d->shadow[s].host == x) return s;
    return 0;
}
uint32_t script_shadow_host(const dom_document *d, uint32_t x) {
    uint32_t s = sc_shadow_of_root(d, x);
    return s ? d->shadow[s].host : 0;
}
/* the shadow-including parent: a shadow root's host, else the parent */
static uint32_t sc_up(const dom_document *d, uint32_t x) {
    uint32_t p = d->node[x].parent;
    return p ? p : script_shadow_host(d, x);
}
/* connected: in the document, through the shadow roots on the way */
static int sc_connected(const dom_document *d, uint32_t x) {
    while (x && x != 1) x = sc_up(d, x);
    return x == 1;
}
/* the next node in shadow-including tree order within scope: a host's shadow tree right
   after it, before its children */
static uint32_t sc_next(const dom_document *d, uint32_t c, uint32_t scope) {
    uint32_t s = d->node[c].kind == DOM_NODE_KIND_ELEMENT ? sc_shadow_of_host(d, c) : 0;
    if (s) return d->shadow[s].root;
    if (d->node[c].first_child) return d->node[c].first_child;
    for (uint32_t y = c; y && y != scope;) {
        if (d->node[y].next_sibling) return d->node[y].next_sibling;
        uint32_t p = d->node[y].parent;
        if (!p) {
            /* a shadow root's end: on to its host's children */
            uint32_t h = script_shadow_host(d, y);
            if (!h || h == scope) return 0;
            if (d->node[h].first_child) return d->node[h].first_child;
            y = h;
            continue;
        }
        y = p;
    }
    return 0;
}

/* ---- the registry ---- */

static uint32_t sc_def_of_name(struct js_host *h, const char *s, size_t n) {
    for (size_t i = 0; i < len(h->cdefs); i++)
        if (h->cdefs[i].nlen == n && memcmp(data(h->ctext) + h->cdefs[i].noff, s, n) == 0) return (uint32_t)i + 1;
    return 0;
}
static uint32_t sc_def_of_ctor(struct js_host *h, uint32_t ctor) {
    for (size_t i = 0; i < len(h->cdefs); i++) if (h->cdefs[i].ctor == ctor) return (uint32_t)i + 1;
    return 0;
}
uint32_t script_custom_def(js_vm *vm, const char *s, size_t n) { return sc_def_of_name(vm->host, s, n); }
/* an element's state's record: its place in the host's list plus one, 0 for none */
static uint32_t sc_el(struct js_host *h, uint32_t x) {
    const dom_document *d = h->d;
    if (x >= len(h->cmap) || !h->cmap[x]) return 0;
    sh_cel e = h->cels[h->cmap[x] - 1];
    return e.gen == d->node[x].gen ? h->cmap[x] : 0;
}
static void sc_set_state(struct js_host *h, uint32_t x, uint32_t def, uint32_t state) {
    const dom_document *d = h->d;
    uint32_t k = sc_el(h, x);
    if (!k) {
        sh_cel e;
        memset(&e, 0, sizeof e);
        e.node = x;
        e.gen = d->node[x].gen;
        push_back(h->cels, e);
        if (x >= len(h->cmap)) resize(h->cmap, (size_t)d->node_len + 64, 0u);
        h->cmap[x] = (uint32_t)len(h->cels);
        k = h->cmap[x];
    }
    h->cels[k - 1].def = def;
    h->cels[k - 1].state = state;
}
static uint32_t sc_state(struct js_host *h, uint32_t x) {
    uint32_t k = sc_el(h, x);
    return k ? h->cels[k - 1].state : 0;
}
/* whether an attribute is one of a definition's observed attributes */
static int sc_observes(struct js_host *h, const sh_cdef *def, const char *s, size_t n) {
    size_t i = def->ooff, end = (size_t)def->ooff + def->olen;
    while (i < end) {
        size_t e = i;
        while (e < end && h->ctext[e]) e++;
        if (e - i == n && memcmp(data(h->ctext) + i, s, n) == 0) return 1;
        i = e + 1;
    }
    return 0;
}
/* an error a constructor or a callback threw, reported as uncaught */
static void sc_report(js_vm *vm) {
    if (vm->throwing != 1) return;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    js_report_error(vm, e, SCRIPT_PHASE_EVENT);
}
/* a lifecycle callback of x's definition called with its arguments */
static void sc_callback(js_vm *vm, uint32_t x, uint32_t fn, const jv *args, uint32_t n) {
    if (!fn || vm->throwing == 2) return;
    struct js_host *h = vm->host;
    uint32_t k = sc_el(h, x);
    uint32_t saved = vm->cur_run;
    if (k && h->cels[k - 1].def) vm->cur_run = h->cdefs[h->cels[k - 1].def - 1].run;
    js_call(vm, jv_obj(fn), sh_wrap(vm, x), args, n);
    sc_report(vm);
    vm->cur_run = saved;
}
/* connectedCallback once as the element comes into the document, disconnectedCallback
   once as it leaves (an element the call made and inserted is told once) */
static void sc_connected_cb(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    uint32_t k = sc_el(h, x);
    if (!k || h->cels[k - 1].state != SC_CUSTOM || h->cels[k - 1].conn) return;
    h->cels[k - 1].conn = 1;
    sc_callback(vm, x, h->cdefs[h->cels[k - 1].def - 1].connected, 0, 0);
}
static void sc_disconnected_cb(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    uint32_t k = sc_el(h, x);
    if (!k || h->cels[k - 1].state != SC_CUSTOM || !h->cels[k - 1].conn) return;
    h->cels[k - 1].conn = 0;
    sc_callback(vm, x, h->cdefs[h->cels[k - 1].def - 1].disconnected, 0, 0);
}
/* attributeChangedCallback(name, old, new, namespace) of a custom element that observes it */
static void sc_attr_cb(js_vm *vm, uint32_t x, const char *name, size_t nn, jv old, jv now, jv ns) {
    struct js_host *h = vm->host;
    uint32_t k = sc_el(h, x);
    if (!k || h->cels[k - 1].state != SC_CUSTOM) return;
    sh_cdef def = h->cdefs[h->cels[k - 1].def - 1];
    if (!def.attr_changed || !sc_observes(h, &def, name, nn)) return;
    jv args[4] = { sh_str(vm, name, nn), old, now, ns };
    sc_callback(vm, x, def.attr_changed, args, 4);
}

/* an upgrade (HTML 4.13.5): the constructor run on the element, then its observed
   attributes' callbacks and its connectedCallback; a failure reported */
static void sc_upgrade(js_vm *vm, uint32_t x, uint32_t def) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    uint32_t st = sc_state(h, x);
    if (st == SC_CUSTOM || st == SC_FAILED) return;
    sc_set_state(h, x, def, SC_FAILED);
    sh_cdef c = h->cdefs[def - 1];
    push_back(h->cstack, x);
    uint32_t saved = vm->cur_run;
    vm->cur_run = c.run;
    jv r = js_construct(vm, jv_obj(c.ctor), 0, 0, jv_obj(c.ctor));
    vm->cur_run = saved;
    pop_back(h->cstack);
    if (vm->throwing) { sc_report(vm); return; }
    if (r != sh_wrap(vm, x)) {
        js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'CustomElement': The result must implement HTMLElement interface");
        sc_report(vm);
        return;
    }
    sc_set_state(h, x, def, SC_CUSTOM);
    if (c.rec) h->rt->definition[c.rec].upgraded++;
    /* its attributes, in order, then connected */
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) {
        vector<char> an;
        append_range(an, names_ptr(d, d->attr[at].name), names_len(d, d->attr[at].name));
        sc_attr_cb(vm, x, data(an), len(an), JV_NULL, sh_str(vm, text_ptr(d, d->attr[at].value), d->attr[at].value.len), JV_NULL);
    }
    if (sc_connected(d, x)) sc_connected_cb(vm, x);
}

/* the elements of a definition's name within scope (the document: those connected),
   upgraded in shadow-including tree order */
static void sc_upgrade_in(js_vm *vm, uint32_t scope, uint32_t def) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    sh_cdef c = h->cdefs[def - 1];
    vector<uint32_t> xs;
    for (uint32_t x = scope; x; x = sc_next(d, x, scope)) {
        if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) continue;
        if (names_len(d, d->node[x].name) != c.nlen || memcmp(names_ptr(d, d->node[x].name), data(h->ctext) + c.noff, c.nlen) != 0) continue;
        push_back(xs, x);
    }
    for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) sc_upgrade(vm, xs[i], def);
}
/* a node and what it holds come into the document: each element of a defined name not yet
   upgraded upgraded, each custom one's connectedCallback */
static void sc_inserted(js_vm *vm, uint32_t c) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    vector<uint32_t> xs;
    for (uint32_t x = c; x; x = sc_next(d, x, c)) if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) push_back(xs, x);
    for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) {
        uint32_t x = xs[i];
        if (!d->node[x].live) continue;
        uint32_t st = sc_state(h, x);
        if (st == SC_CUSTOM) { sc_connected_cb(vm, x); continue; }
        if (st == SC_FAILED || d->node[x].ns != DOM_NAMESPACE_HTML) continue;
        vector<char> nm;
        sc_name(d, x, &nm);
        uint32_t def = sc_def_of_name(h, data(nm), len(nm));
        if (def) sc_upgrade(vm, x, def);
    }
}
static void sc_removed(js_vm *vm, uint32_t c) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    vector<uint32_t> xs;
    for (uint32_t x = c; x; x = sc_next(d, x, c)) if (d->node[x].kind == DOM_NODE_KIND_ELEMENT && sc_state(h, x) == SC_CUSTOM) push_back(xs, x);
    for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) sc_disconnected_cb(vm, xs[i]);
}

/* the reactions to the DOM's changes since they were last read (the end of a call into the
   host): connected, disconnected, attribute changed; the log read on as callbacks change it */
void script_custom_flush(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    if (h->in_reactions) return;
    h->in_reactions = 1;
    while (h->ce_pos < d->mutation_len && vm->throwing != 2) {
        dom_mutation e = d->mutation[h->ce_pos];
        h->ce_pos++;
        if (e.op == DOM_OP_CREATE_ELEMENT || e.op == DOM_OP_CREATE_ELEMENT_NS) {
            /* an element of a defined name made without its constructor (innerHTML's, a
               clone's): its upgrade, connected or not */
            uint32_t x = e.target;
            if (x >= d->node_len || !d->node[x].live || d->node[x].ns != DOM_NAMESPACE_HTML || sc_state(h, x)) continue;
            vector<char> nm;
            sc_name(d, x, &nm);
            uint32_t def = sc_def_of_name(h, data(nm), len(nm));
            if (def) sc_upgrade(vm, x, def);
        } else if (e.op == DOM_OP_APPEND_CHILD || e.op == DOM_OP_INSERT_BEFORE) {
            if (e.other < d->node_len && d->node[e.other].live && sc_connected(d, e.target)) sc_inserted(vm, e.other);
        } else if (e.op == DOM_OP_REMOVE_CHILD) {
            if (e.other < d->node_len && d->node[e.other].live && sc_connected(d, e.target)) sc_removed(vm, e.other);
        } else if ((e.op == DOM_OP_SET_ATTRIBUTE || e.op == DOM_OP_SET_ATTRIBUTE_NS || e.op == DOM_OP_REMOVE_ATTRIBUTE) && e.detail && e.detail < d->detail_len) {
            uint32_t x = e.target;
            if (x >= d->node_len || !d->node[x].live || sc_state(h, x) != SC_CUSTOM) continue;
            dom_detail dt = d->detail[e.detail];
            vector<char> an;
            append_range(an, names_ptr(d, dt.name), names_len(d, dt.name));
            jv old = dt.had ? sh_str(vm, text_ptr(d, dt.old), dt.old.len) : JV_NULL;
            jv now = JV_NULL;
            if (e.op != DOM_OP_REMOVE_ATTRIBUTE && e.other < d->attr_len && d->attr[e.other].live && d->attr[e.other].owner == x)
                now = sh_str(vm, text_ptr(d, d->attr[e.other].value), d->attr[e.other].value.len);
            static const char *const spaces[] = { "", "http://www.w3.org/1999/xlink", "http://www.w3.org/XML/1998/namespace", "http://www.w3.org/2000/xmlns/" };
            jv ns = dt.ns && dt.ns < 4 ? sh_cstr(vm, spaces[dt.ns]) : JV_NULL;
            sc_attr_cb(vm, x, data(an), len(an), old, now, ns);
        }
    }
    h->in_reactions = 0;
}

/* ---- the natives ---- */

/* define(name, constructor) (HTML 4.13.4) */
static jv sc_define(js_vm *vm, jv name, jv ctor) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    static const char *const pre = "Failed to execute 'define' on 'CustomElementRegistry': ";
    char msg[400];
    if (!js_is_callable(vm, ctor)) {
        snprintf(msg, sizeof msg, "%sparameter 2 is not of type 'Function'.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    vector<char> nm;
    if (!sh_utf8(vm, name, &nm)) return JV_HOLE;
    if (!sc_valid_name(data(nm), len(nm))) {
        snprintf(msg, sizeof msg, "%s\"%.*s\" is not a valid custom element name", pre, (int)len(nm), data(nm));
        return sh_dom_error(vm, "SyntaxError", msg);
    }
    if (sc_def_of_name(h, data(nm), len(nm))) {
        snprintf(msg, sizeof msg, "%sthe name \"%.*s\" has already been used with this registry", pre, (int)len(nm), data(nm));
        return sh_dom_error(vm, "NotSupportedError", msg);
    }
    if (sc_def_of_ctor(h, jv_idx(ctor))) {
        snprintf(msg, sizeof msg, "%sthis constructor has already been used with this registry", pre);
        return sh_dom_error(vm, "NotSupportedError", msg);
    }
    jv proto = js_get(vm, ctor, A_PROTOTYPE);
    if (vm->throwing) return JV_HOLE;
    if (!jv_is_obj(proto)) {
        snprintf(msg, sizeof msg, "%sThe prototype of this constructor is not an object.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    sh_cdef c;
    memset(&c, 0, sizeof c);
    c.ctor = jv_idx(ctor);
    c.proto = jv_idx(proto);
    c.run = vm->cur_run;
    static const char *const cbs[] = { "connectedCallback", "disconnectedCallback", "adoptedCallback", "attributeChangedCallback" };
    uint32_t got[4];
    for (int i = 0; i < 4; i++) {
        jv f = js_get(vm, proto, sc_atom(vm, cbs[i]));
        if (vm->throwing) return JV_HOLE;
        got[i] = 0;
        if (f == JV_UNDEF) continue;
        if (!js_is_callable(vm, f)) {
            snprintf(msg, sizeof msg, "%sThe '%s' property on the prototype is not a function.", pre, cbs[i]);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        got[i] = jv_idx(f);
    }
    c.connected = got[0];
    c.disconnected = got[1];
    c.adopted = got[2];
    c.attr_changed = got[3];
    /* the attributes it observes, each ended by a zero byte */
    vector<char> obs;
    uint32_t nobs = 0;
    if (c.attr_changed) {
        jv oa = js_get(vm, ctor, sc_atom(vm, "observedAttributes"));
        if (vm->throwing) return JV_HOLE;
        if (oa != JV_UNDEF) {
            if (!jv_is_obj(oa)) {
                snprintf(msg, sizeof msg, "%sThe 'observedAttributes' property is not a sequence.", pre);
                return js_throw_msg(vm, vm->type_error_proto, msg);
            }
            jv lv = js_get(vm, oa, sc_atom(vm, "length"));
            uint32_t n = jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0;
            for (uint32_t i = 0; i < n; i++) {
                vector<char> one;
                if (!sh_utf8(vm, js_array_get(vm, jv_idx(oa), i), &one)) return JV_HOLE;
                append_range(obs, data(one), len(one));
                push_back(obs, 0);
                nobs++;
            }
        }
    }
    c.noff = (uint32_t)len(h->ctext);
    append_range(h->ctext, data(nm), len(nm));
    c.nlen = (uint32_t)len(nm);
    c.ooff = (uint32_t)len(h->ctext);
    append_range(h->ctext, data(obs), len(obs));
    c.olen = (uint32_t)len(obs);
    /* the reasoner's record of it */
    script_definition_h dh = definition_new(h->rt);
    h->rt->definition[dh.index].run = vm->cur_run;
    h->rt->definition[dh.index].name = text_add(h->rt, data(nm), len(nm));
    h->rt->definition[dh.index].observed = nobs;
    h->rt->definition[dh.index].connected = (uint8_t)(c.connected != 0);
    h->rt->definition[dh.index].disconnected = (uint8_t)(c.disconnected != 0);
    h->rt->definition[dh.index].attribute_changed = (uint8_t)(c.attr_changed != 0);
    c.rec = dh.index;
    /* the callbacks keep: the constructor's object holds them as the prototype does */
    push_back(h->cdefs, c);
    uint32_t def = (uint32_t)len(h->cdefs);
    /* the DOM's changes read from here, their details kept for the attributes' old values */
    if (def == 1) {
        dom_keep_details(d);
        h->ce_pos = (uint32_t)d->mutation_len;
    }
    sc_upgrade_in(vm, 1, def);
    /* whenDefined's promises for the name */
    for (size_t i = 0; i + 2 < len(h->cwait); i += 3) {
        if (h->cwait[i + 2]) continue;
        uint32_t p = h->cwait[i];
        vector<char> wn;
        js_str_to_utf8(vm, h->cwait[i + 1], &wn);
        if (len(wn) == len(nm) && memcmp(data(wn), data(nm), len(nm)) == 0) {
            js_promise_settle(vm, p, 0, ctor);
            h->cwait[i + 2] = 1;
        }
    }
    return JV_UNDEF;
}
/* HTMLElement's constructor (HTML 3.2.3), reached by a custom element class's super():
   new.target from that constructor's frame, its definition's element made, or the one being
   upgraded */
static jv sc_html_element(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    jv newt = JV_UNDEF;
    if (vm->nframes) {
        js_frame f = vm->frames[vm->nframes - 1];
        uint32_t he = js_atom_ascii(vm, "HTMLElement");
        jv base = js_get(vm, jv_obj(vm->global), he);
        if (f.callee && jv_is_obj(base) && vm->objs[f.callee].proto == jv_idx(base) && jv_is_obj(f.newt)) newt = f.newt;
    }
    uint32_t def = jv_is_obj(newt) ? sc_def_of_ctor(h, jv_idx(newt)) : 0;
    if (!def) return js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'HTMLElement': Illegal constructor");
    sh_cdef c = h->cdefs[def - 1];
    jv proto = js_get(vm, newt, A_PROTOTYPE);
    if (vm->throwing) return JV_HOLE;
    if (!len(h->cstack)) {
        /* a new element of the definition's name */
        dom_node_h e = dom_create_element(d, data(h->ctext) + c.noff, c.nlen);
        if (!e.index) return sh_status(vm, DOM_INVALID_NAME);
        sc_set_state(h, e.index, def, SC_CUSTOM);
        jv w = sh_wrap(vm, e.index);
        if (jv_is_obj(proto)) js_set_proto(vm, jv_idx(w), jv_idx(proto));
        return w;
    }
    uint32_t x = h->cstack[len(h->cstack) - 1];
    if (x == 0xFFFFFFFFu) return sh_dom_error(vm, "InvalidStateError", "Failed to construct 'HTMLElement': This instance is already constructed");
    h->cstack[len(h->cstack) - 1] = 0xFFFFFFFFu;
    jv w = sh_wrap(vm, x);
    if (jv_is_obj(proto)) js_set_proto(vm, jv_idx(w), jv_idx(proto));
    return w;
}
/* document.createElement of a defined name: the element its constructor makes; when the
   constructor throws, the error reported and an element of the name whose upgrade failed */
jv script_custom_create(js_vm *vm, uint32_t def) {
    struct js_host *h = vm->host;
    sh_cdef c = h->cdefs[def - 1];
    uint32_t saved = vm->cur_run;
    jv r = js_construct(vm, jv_obj(c.ctor), 0, 0, jv_obj(c.ctor));
    vm->cur_run = saved;
    if (vm->throwing == 1) {
        sc_report(vm);
        dom_node_h e = dom_create_element(h->d, data(h->ctext) + c.noff, c.nlen);
        if (!e.index) return sh_status(vm, DOM_INVALID_NAME);
        sc_set_state(h, e.index, def, SC_FAILED);
        return sh_wrap(vm, e.index);
    }
    return r;
}

/* the elements that may host a shadow root (DOM 4.2.14, attachShadow) */
static int sc_shadow_host_ok(const dom_document *d, uint32_t x) {
    if (d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    vector<char> nm;
    sc_name(d, x, &nm);
    if (sc_valid_name(data(nm), len(nm))) return 1;
    static const char *const ok[] = { "article", "aside", "blockquote", "body", "div", "footer", "h1", "h2", "h3", "h4", "h5", "h6",
                                      "header", "main", "nav", "p", "section", "span" };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) if (len(nm) == strlen(ok[i]) && memcmp(data(nm), ok[i], len(nm)) == 0) return 1;
    return 0;
}

/* a slot's assigned nodes (DOM 4.2.2.2): its tree's host's children whose slot attribute is
   its name (the default slot: those with none or an empty one); the first slot of the name
   in tree order takes them. flatten: a slot assigned none gives its own children, and a slot
   among them gives its assigned nodes */
static uint32_t sc_root_of(const dom_document *d, uint32_t x) {
    while (d->node[x].parent) x = d->node[x].parent;
    return x;
}
static void sc_attr_text(const dom_document *d, uint32_t x, const char *name, vector<char> *out) {
    clear(*out);
    uint32_t at = sh_attr_z(d, x, name);
    if (at) append_range(*out, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
}
/* the slot a host's child is assigned to, 0 for none */
static uint32_t sc_slot_for(const dom_document *d, uint32_t child) {
    uint32_t host = d->node[child].parent;
    if (!host || d->node[child].kind == DOM_NODE_KIND_COMMENT) return 0;
    uint32_t s = d->node[host].kind == DOM_NODE_KIND_ELEMENT ? sc_shadow_of_host(d, host) : 0;
    if (!s) return 0;
    vector<char> want, nm;
    if (d->node[child].kind == DOM_NODE_KIND_ELEMENT) sc_attr_text(d, child, "slot", &want);
    uint32_t root = d->shadow[s].root;
    for (uint32_t c = sh_next_in(d, root, root); c; c = sh_next_in(d, c, root)) {
        if (!sh_named(d, c, "slot")) continue;
        sc_attr_text(d, c, "name", &nm);
        if (len(nm) == len(want) && memcmp(data(nm), data(want), len(want)) == 0) return c;
    }
    return 0;
}
static void sc_assigned(const dom_document *d, uint32_t slot, int flatten, int elements, vector<uint32_t> *out) {
    uint32_t root = sc_root_of(d, slot), host = script_shadow_host(d, root);
    size_t before = len(*out);
    for (uint32_t c = host ? d->node[host].first_child : 0; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT && d->node[c].kind != DOM_NODE_KIND_TEXT) continue;
        if (sc_slot_for(d, c) != slot) continue;
        if (flatten && sh_named(d, c, "slot") && script_shadow_host(d, sc_root_of(d, c))) { sc_assigned(d, c, 1, elements, out); continue; }
        if (!elements || d->node[c].kind == DOM_NODE_KIND_ELEMENT) push_back(*out, c);
    }
    if (flatten && len(*out) == before)
        for (uint32_t c = d->node[slot].first_child; c; c = d->node[c].next_sibling) {
            if (sh_named(d, c, "slot")) { sc_assigned(d, c, 1, elements, out); continue; }
            if (!elements || d->node[c].kind == DOM_NODE_KIND_ELEMENT) push_back(*out, c);
        }
}
/* an event's next node (DOM 4.2.10, get the parent): a slotted node's slot; a shadow root's
   host, unless the event is not composed and the root is its target's tree's */
uint32_t script_event_parent(const dom_document *d, uint32_t x, uint32_t target, int composed) {
    if (d->shadow_len <= 1) return d->node[x].parent;
    uint32_t slot = sc_slot_for(d, x);
    if (slot) return slot;
    if (d->node[x].parent) return d->node[x].parent;
    uint32_t h = script_shadow_host(d, x);
    if (!h || (!composed && sc_root_of(d, target) == x)) return 0;
    return h;
}
uint32_t script_retarget(const dom_document *d, uint32_t a, uint32_t b) {
    if (d->shadow_len <= 1 || a == JS_WINDOW || (a & 0x80000000u)) return a;
    for (;;) {
        uint32_t root = sc_root_of(d, a), host = script_shadow_host(d, root);
        if (!host) return a;
        /* b in a's tree (root a shadow-including inclusive ancestor of b): a as it is */
        int inside = 0;
        if (b != JS_WINDOW && !(b & 0x80000000u))
            for (uint32_t y = b; y && !inside; y = sc_up(d, y)) if (y == root) inside = 1;
        if (inside) return a;
        a = host;
    }
}

jv script_custom_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)callee;
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    switch (id) {
    case SC_ILLEGAL: return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    case SC_HTML_ELEMENT: return sc_html_element(vm);
    case SC_DEFINE: return sc_define(vm, a0, a1);
    case SC_GET: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        uint32_t def = sc_def_of_name(h, data(nm), len(nm));
        return def ? jv_obj(h->cdefs[def - 1].ctor) : JV_UNDEF;
    }
    case SC_WHEN_DEFINED: {
        vector<char> nm;
        if (!sh_utf8(vm, a0, &nm)) return JV_HOLE;
        uint32_t p = js_promise_new(vm);
        if (!sc_valid_name(data(nm), len(nm))) {
            char msg[300];
            snprintf(msg, sizeof msg, "Failed to execute 'whenDefined' on 'CustomElementRegistry': \"%.*s\" is not a valid custom element name", (int)len(nm), data(nm));
            jv e = sh_dom_error(vm, "SyntaxError", msg);
            (void)e;
            jv ex = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, ex);
            return jv_obj(p);
        }
        uint32_t def = sc_def_of_name(h, data(nm), len(nm));
        if (def) { js_promise_settle(vm, p, 0, jv_obj(h->cdefs[def - 1].ctor)); return jv_obj(p); }
        push_back(h->cwait, p);
        push_back(h->cwait, js_atom(vm, js_str_utf8(vm, data(nm), len(nm))));
        push_back(h->cwait, 0u);
        /* the promise kept: the registry holds it */
        js_define(vm, h->registry, js_atom(vm, js_str_utf8(vm, data(nm), len(nm))), jv_obj(p), JS_SHAPE_HIDDEN);
        return jv_obj(p);
    }
    case SC_UPGRADE: {
        uint32_t root = sh_node(vm, a0, 1);
        if (vm->throwing) return JV_HOLE;
        vector<uint32_t> xs;
        for (uint32_t x = root; x; x = sc_next(d, x, root)) if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) push_back(xs, x);
        for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) {
            vector<char> nm;
            sc_name(d, xs[i], &nm);
            uint32_t def = d->node[xs[i]].ns == DOM_NAMESPACE_HTML ? sc_def_of_name(h, data(nm), len(nm)) : 0;
            if (def) sc_upgrade(vm, xs[i], def);
        }
        return JV_UNDEF;
    }
    case SC_ATTACH_SHADOW: {
        uint32_t x = sh_node(vm, thisv, 1);
        if (vm->throwing) return JV_HOLE;
        static const char *const pre = "Failed to execute 'attachShadow' on 'Element': ";
        char msg[300];
        jv mode = jv_is_obj(a0) ? js_get(vm, a0, sc_atom(vm, "mode")) : JV_UNDEF;
        if (vm->throwing) return JV_HOLE;
        if (mode == JV_UNDEF) {
            snprintf(msg, sizeof msg, "%sFailed to read the 'mode' property from 'ShadowRootInit': Required member is undefined.", pre);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        vector<char> m;
        if (!sh_utf8(vm, mode, &m)) return JV_HOLE;
        int open = len(m) == 4 && memcmp(data(m), "open", 4) == 0, closed = len(m) == 6 && memcmp(data(m), "closed", 6) == 0;
        if (!open && !closed) {
            snprintf(msg, sizeof msg, "%sFailed to read the 'mode' property from 'ShadowRootInit': The provided value '%.*s' is not a valid enum value of type ShadowRootMode.",
                     pre, (int)len(m), data(m));
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || !sc_shadow_host_ok(d, x)) {
            snprintf(msg, sizeof msg, "%sThis element does not support attachShadow", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        if (sc_shadow_of_host(d, x)) {
            snprintf(msg, sizeof msg, "%sShadow root cannot be created on a host which already hosts a shadow tree.", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        dom_node_h r = dom_attach_shadow(d, node_handle(d, x), open ? DOM_SHADOW_MODE_OPEN : DOM_SHADOW_MODE_CLOSED);
        if (!r.index) return sh_status(vm, DOM_WRONG_KIND);
        return sh_wrap(vm, r.index);
    }
    case SC_SHADOW_ROOT: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t s = x && d->node[x].kind == DOM_NODE_KIND_ELEMENT ? sc_shadow_of_host(d, x) : 0;
        return s && d->shadow[s].mode == DOM_SHADOW_MODE_OPEN ? sh_wrap(vm, d->shadow[s].root) : JV_NULL;
    }
    case SC_HOST: case SC_MODE: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t s = x ? sc_shadow_of_root(d, x) : 0;
        if (!s) return id == SC_HOST ? JV_NULL : JV_UNDEF;
        if (id == SC_HOST) return sh_wrap(vm, d->shadow[s].host);
        return sh_cstr(vm, d->shadow[s].mode == DOM_SHADOW_MODE_OPEN ? "open" : "closed");
    }
    case SC_ASSIGNED_NODES: case SC_ASSIGNED_ELEMENTS: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        int flatten = jv_is_obj(a0) && js_truthy(vm, js_get(vm, a0, sc_atom(vm, "flatten")));
        vector<uint32_t> xs;
        if (x && sh_named(d, x, "slot") && script_shadow_host(d, sc_root_of(d, x))) sc_assigned(d, x, flatten, id == SC_ASSIGNED_ELEMENTS, &xs);
        uint32_t a = js_array_new(vm, 0);
        for (size_t i = 0; i < len(xs); i++) js_array_push(vm, a, sh_wrap(vm, xs[i]));
        return jv_obj(a);
    }
    case SC_ASSIGNED_SLOT: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t slot = x ? sc_slot_for(d, x) : 0;
        /* a closed tree's slot is not shown */
        uint32_t s = slot ? sc_shadow_of_root(d, sc_root_of(d, slot)) : 0;
        return s && d->shadow[s].mode == DOM_SHADOW_MODE_OPEN ? sh_wrap(vm, slot) : JV_NULL;
    }
    case SC_COMPOSED_PATH: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        jv p = js_get(vm, thisv, sc_atom(vm, "\001path"));
        uint32_t a = js_array_new(vm, 0);
        if (jv_is_obj(p)) {
            jv lv = js_get(vm, p, sc_atom(vm, "length"));
            uint32_t n = jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0;
            for (uint32_t i = 0; i < n; i++) js_array_push(vm, a, js_array_get(vm, jv_idx(p), i));
        }
        (void)a1;
        return jv_obj(a);
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

void script_custom_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t elem = h->protos[HP_ELEMENT];
    /* HTMLElement's constructor, for its subclasses' super() */
    sh_interface(vm, "HTMLElement", SC_HTML_ELEMENT, elem);
    uint32_t rp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    sh_method(vm, rp, "define", SC_DEFINE, 2);
    sh_method(vm, rp, "get", SC_GET, 1);
    sh_method(vm, rp, "whenDefined", SC_WHEN_DEFINED, 1);
    sh_method(vm, rp, "upgrade", SC_UPGRADE, 1);
    sh_interface(vm, "CustomElementRegistry", SC_ILLEGAL, rp);
    uint32_t reg = js_obj_new(vm, JK_PLAIN, rp);
    h->registry = reg;
    js_define(vm, vm->global, sc_atom(vm, "customElements"), jv_obj(reg), JS_SHAPE_HIDDEN);
    /* shadow trees: attachShadow and shadowRoot; ShadowRoot's host and mode; slots */
    sh_method(vm, elem, "attachShadow", SC_ATTACH_SHADOW, 1);
    sh_method(vm, elem, "assignedNodes", SC_ASSIGNED_NODES, 0);
    sh_method(vm, elem, "assignedElements", SC_ASSIGNED_ELEMENTS, 0);
    static const char *const eg[] = { "shadowRoot", "assignedSlot" };
    static const uint32_t egi[] = { SC_SHADOW_ROOT, SC_ASSIGNED_SLOT };
    for (int i = 0; i < 2; i++) {
        uint32_t a = sc_atom(vm, eg[i]);
        js_define_accessor(vm, elem, a, jv_obj(js_native_new(vm, egi[i], a, 0)), JV_UNDEF, 0);
    }
    uint32_t a = sc_atom(vm, "assignedSlot");
    js_define_accessor(vm, h->protos[HP_CHARDATA], a, jv_obj(js_native_new(vm, SC_ASSIGNED_SLOT, a, 0)), JV_UNDEF, 0);
    uint32_t sr = js_obj_new(vm, JK_PLAIN, h->protos[HP_NODE]);
    h->protos[HP_SHADOWROOT] = sr;
    static const char *const sg[] = { "host", "mode" };
    static const uint32_t sgi[] = { SC_HOST, SC_MODE };
    for (int i = 0; i < 2; i++) {
        uint32_t at = sc_atom(vm, sg[i]);
        js_define_accessor(vm, sr, at, jv_obj(js_native_new(vm, sgi[i], at, 0)), JV_UNDEF, 0);
    }
    sh_interface(vm, "ShadowRoot", SC_ILLEGAL, sr);
    sh_method(vm, h->protos[HP_EVENT], "composedPath", SC_COMPOSED_PATH, 0);
}
