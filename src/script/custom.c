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

   SHADOW TREES. attachShadow({mode, delegatesFocus, slotAssignment, clonable, serializable})
   on an element that may host one (an autonomous custom element, or article, aside,
   blockquote, body, div, footer, h1-h6, header, main, nav, p, section, span) makes its
   shadow root: the DOM's attach_shadow, a fragment that is the root of a tree of its own,
   which the cascade, layout and accessibility walk in the flat tree (src/dom/dom.h).
   shadowRoot answers an open one; the root's host, mode and options; innerHTML,
   getElementById and the ParentNode methods on it; a clonable one cloned with its host.
   A slot's assigned nodes are the DOM's (dom_flatten): its host's element and text
   children whose slot attribute names it (the default slot: those with none), or, under
   manual assignment, those assign() gave it, in that order; flattened on asking;
   assignedSlot and slot. Each change of a slot's assigned nodes, looked for at the end of
   each call into the host, signals the slot (the old slot of a node whose slot attribute
   changed before its new one), and so does a child put in or taken from a slot no node is
   assigned to, its fallback; its slotchange fired after the observers' callbacks. An
   event's path goes from a slotted node to its slot, from a shadow root to its host when
   the event is composed, and each listener sees the target retargeted to the host of the
   tree the listener is not in; composedPath() is the path as the listener may see it, a
   closed tree's nodes hidden from a listener outside it (DOM 2.9).

   What is not done: customized built-in elements (extends, is=); form-associated custom
   elements; adoptedStyleSheets (there are no constructable style sheets); focus is not
   delegated (delegatesFocus is kept and read back). */
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
#include "../dom/dom.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);

enum {
    SC_HTML_ELEMENT = SH_CUSTOM_FIRST, SC_DEFINE, SC_GET, SC_WHEN_DEFINED, SC_UPGRADE, SC_ATTACH_SHADOW, SC_SHADOW_ROOT, SC_HOST,
    SC_MODE, SC_ASSIGNED_NODES, SC_ASSIGNED_ELEMENTS, SC_ASSIGNED_SLOT, SC_COMPOSED_PATH, SC_ILLEGAL, SC_DELEGATES, SC_ASSIGNMENT,
    SC_CLONABLE, SC_SERIALIZABLE, SC_ASSIGN, SC_SLOT_GET, SC_SLOT_SET, SC_N
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
    if (!x || !d->node[x].shadow) return 0;
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
        sc_attr_cb(vm, x, data(an), len(an), JV_NULL, sh_span(vm, d, d->attr[at].value), JV_NULL);
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
            jv old = dt.had ? sh_span(vm, d, dt.old) : JV_NULL;
            jv now = JV_NULL;
            if (e.op != DOM_OP_REMOVE_ATTRIBUTE && e.other < d->attr_len && d->attr[e.other].live && d->attr[e.other].owner == x)
                now = sh_span(vm, d, d->attr[e.other].value);
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
/* the slot a node is assigned to (the DOM's flat tree, found by dom_flatten), 0 for none */
static uint32_t sc_slot_for(const dom_document *d, uint32_t x) { return x && x < d->node_len ? d->node[x].slot : 0; }
/* a slot in a shadow tree */
static int sc_tree_slot(const dom_document *d, uint32_t x) { return sh_named(d, x, "slot") && script_shadow_host(d, sc_root_of(d, x)); }
/* a slot's assigned nodes (find slottables), or flattened (find flattened slottables, DOM 4.2.2.3): each
   one that is a slot in a shadow tree giving its own flattened, and with none assigned the slot's element
   and text children instead; elements, the elements of them alone */
static void sc_assigned(const dom_document *d, uint32_t slot, int flatten, int elements, vector<uint32_t> *out, int depth) {
    if (!sc_tree_slot(d, slot) || depth > 64) return;
    uint32_t first = d->node[slot].assigned_first;
    for (uint32_t c = first ? first : flatten ? d->node[slot].first_child : 0; c; c = first ? d->node[c].assigned_next : d->node[c].next_sibling) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT && d->node[c].kind != DOM_NODE_KIND_TEXT) continue;
        if (flatten && sc_tree_slot(d, c)) { sc_assigned(d, c, 1, elements, out, depth + 1); continue; }
        if (!elements || d->node[c].kind == DOM_NODE_KIND_ELEMENT) push_back(*out, c);
    }
}

/* ---- slotchange (DOM 4.2.2.5, signal a slot change) ----
   The slots' assigned nodes are looked at again at the end of each call into the host that changed the
   document: each slot whose nodes are not as they were is signalled, in the order the call's changes
   reach them -- a node's slot attribute changed, its old slot before its new; a node inserted into a host
   or taken from one, its slot -- the others of a tree in its order; once each until its event is fired,
   in the observers' microtask (src/script/observe.c), after their callbacks. */
/* the current assignment of every slot that has nodes assigned: a slot, its generation, the count, the
   nodes, in out */
static void sc_slots_now(const dom_document *d, vector<uint32_t> *out) {
    clear(*out);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || !d->node[x].assigned_first) continue;
        push_back(*out, x);
        push_back(*out, d->node[x].gen);
        size_t at_count = len(*out);
        push_back(*out, 0u);
        uint32_t n = 0;
        for (uint32_t c = d->node[x].assigned_first; c && n < d->node_len; c = d->node[c].assigned_next, n++) push_back(*out, c);
        (*out)[at_count] = n;
    }
}
/* the slot's nodes in a record made by sc_slots_now: the place of its count, or ~0 for none */
static size_t sc_slots_find(const vector<uint32_t> *v, uint32_t slot, uint32_t gen) {
    for (size_t i = 0; i + 2 < len(*v); i += 3 + at(*v, i + 2))
        if (at(*v, i) == slot && at(*v, i + 1) == gen) return i + 2;
    return ~(size_t)0;
}
static int sc_slots_same(const vector<uint32_t> *a, const vector<uint32_t> *b, uint32_t slot, uint32_t gen) {
    size_t ia = sc_slots_find(a, slot, gen), ib = sc_slots_find(b, slot, gen);
    if (ia == ~(size_t)0 || ib == ~(size_t)0) return ia == ib;
    uint32_t n = at(*a, ia);
    if (n != at(*b, ib)) return 0;
    for (uint32_t k = 1; k <= n; k++) if (at(*a, ia + k) != at(*b, ib + k)) return 0;
    return 1;
}
/* the slot x was assigned to in a record (0 for none) */
static uint32_t sc_slots_of(const vector<uint32_t> *v, uint32_t x) {
    for (size_t i = 0; i + 2 < len(*v); i += 3 + at(*v, i + 2))
        for (uint32_t k = 0; k < at(*v, i + 2); k++) if (at(*v, i + 3 + k) == x) return at(*v, i);
    return 0;
}
/* the slot sl signalled, when its nodes changed (or force: its fallback did, while none is assigned it)
   and it is not signalled already */
static void sc_signal(struct js_host *h, const dom_document *d, const vector<uint32_t> *now, uint32_t sl, int force) {
    if (!sl || sl >= d->node_len || !d->node[sl].live) return;
    if (!force && sc_slots_same(&h->sl_was, now, sl, d->node[sl].gen)) return;
    for (size_t i = 0; i + 1 < len(h->sl_sig); i += 2) if (h->sl_sig[i] == sl && h->sl_sig[i + 1] == d->node[sl].gen) return;
    push_back(h->sl_sig, sl);
    push_back(h->sl_sig, d->node[sl].gen);
}
void script_slots_check(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    if (!h->sl_on || h->sl_rev == (uint32_t)dom_revision(d)) return;
    dom_flatten(d);
    vector<uint32_t> now;
    sc_slots_now(d, &now);
    size_t before = len(h->sl_sig);
    /* the call's changes in their order: the slots each reaches first. The elements the call made (an
       innerHTML's), which had their children before they were in a tree, are no slots whose fallback changed */
    uint32_t slot_atom = names_find(d, "slot", 4);
    vector<uint32_t> made;
    for (size_t i = h->sl_pos; i < d->mutation_len; i++)
        if (d->mutation[i].op == DOM_OP_CREATE_ELEMENT || d->mutation[i].op == DOM_OP_CREATE_ELEMENT_NS) push_back(made, d->mutation[i].target);
    for (size_t i = h->sl_pos; i < d->mutation_len; i++) {
        dom_mutation e = d->mutation[i];
        uint32_t x = e.target, c = e.other;
        if (x >= d->node_len || !d->node[x].live) continue;
        if ((e.op == DOM_OP_SET_ATTRIBUTE || e.op == DOM_OP_REMOVE_ATTRIBUTE) && e.detail && e.detail < d->detail_len && d->detail[e.detail].name == slot_atom) {
            sc_signal(h, d, &now, sc_slots_of(&h->sl_was, x), 0);
            sc_signal(h, d, &now, d->node[x].slot, 0);
        } else if (e.op == DOM_OP_APPEND_CHILD || e.op == DOM_OP_INSERT_BEFORE || e.op == DOM_OP_REMOVE_CHILD) {
            int added = e.op != DOM_OP_REMOVE_CHILD;
            if (d->node[x].shadow) sc_signal(h, d, &now, added ? (c < d->node_len && d->node[c].live ? d->node[c].slot : 0) : sc_slots_of(&h->sl_was, c), 0);
            /* a child in or out of a slot no node is assigned to: its fallback changed (DOM 4.2.3, insert and remove) */
            int fresh = 0;
            for (size_t k = 0; k < len(made) && !fresh; k++) fresh = at(made, k) == x;
            if (!fresh && !d->node[x].assigned_first && sc_tree_slot(d, x)) sc_signal(h, d, &now, x, 1);
        }
    }
    /* then every other slot whose nodes changed: those that have nodes now, those that had them */
    for (size_t i = 0; i + 2 < len(now); i += 3 + at(now, i + 2)) sc_signal(h, d, &now, at(now, i), 0);
    for (size_t i = 0; i + 2 < len(h->sl_was); i += 3 + at(h->sl_was, i + 2))
        if (at(h->sl_was, i) < d->node_len && d->node[at(h->sl_was, i)].gen == at(h->sl_was, i + 1)) sc_signal(h, d, &now, at(h->sl_was, i), 0);
    swap(&h->sl_was, &now);
    h->sl_rev = (uint32_t)dom_revision(d);
    h->sl_pos = (uint32_t)d->mutation_len;
    /* the observers' microtask, which fires them, queued once */
    if (len(h->sl_sig) > before && !h->mo_queued && h->mo_fn) {
        h->mo_queued = 1;
        js_job_push(vm, JOB_CALL, 0, jv_obj(h->mo_fn), JV_UNDEF, JV_UNDEF);
    }
}
void script_slots_fire(js_vm *vm) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    vector<uint32_t> sig;
    swap(&sig, &h->sl_sig);
    for (size_t i = 0; i + 1 < len(sig) && vm->throwing != 2; i += 2) {
        uint32_t sl = at(sig, i);
        if (sl >= d->node_len || !d->node[sl].live || d->node[sl].gen != at(sig, i + 1)) continue;
        sh_fire(vm, sl, "slotchange", EV_BUBBLES);
    }
}
/* the baseline: the slots' nodes as the parse left them, no change signalled */
static void sc_slots_begin(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    h->sl_on = d->shadow_len > 1;
    if (!h->sl_on) return;
    dom_keep_details(d);
    dom_flatten(d);
    sc_slots_now(d, &h->sl_was);
    h->sl_rev = (uint32_t)dom_revision(d);
    h->sl_pos = (uint32_t)d->mutation_len;
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
        /* the dictionary's other members (WebIDL reads them in their names' order): booleans, and the slots'
           assignment, named or manual */
        static const char *const flags[] = { "clonable", "delegatesFocus", "serializable" };
        uint8_t fv[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; k++) {
            jv f = js_get(vm, a0, sc_atom(vm, flags[k]));
            if (vm->throwing) return JV_HOLE;
            fv[k] = (uint8_t)js_truthy(vm, f);
        }
        jv sa = js_get(vm, a0, sc_atom(vm, "slotAssignment"));
        if (vm->throwing) return JV_HOLE;
        int manual = 0;
        if (sa != JV_UNDEF) {
            vector<char> av;
            if (!sh_utf8(vm, sa, &av)) return JV_HOLE;
            manual = len(av) == 6 && memcmp(data(av), "manual", 6) == 0;
            if (!manual && !(len(av) == 5 && memcmp(data(av), "named", 5) == 0)) {
                snprintf(msg, sizeof msg, "%sFailed to read the 'slotAssignment' property from 'ShadowRootInit': The provided value '%.*s' is not a valid enum value of type SlotAssignmentMode.",
                         pre, (int)len(av), data(av));
                return js_throw_msg(vm, vm->type_error_proto, msg);
            }
        }
        if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || !sc_shadow_host_ok(d, x)) {
            snprintf(msg, sizeof msg, "%sThis element does not support attachShadow", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        if (sc_shadow_of_host(d, x)) {
            snprintf(msg, sizeof msg, "%sShadow root cannot be created on a host which already hosts a shadow tree.", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        dom_node_h r = dom_attach_shadow(d, node_handle(d, x), open ? DOM_SHADOW_MODE_OPEN : DOM_SHADOW_MODE_CLOSED,
                                         manual ? DOM_SLOT_ASSIGNMENT_MANUAL : DOM_SLOT_ASSIGNMENT_NAMED, fv[1], fv[0], fv[2], 0);
        if (!r.index) return sh_status(vm, DOM_WRONG_KIND);
        /* slotchange looked for from now on */
        if (!h->sl_on) {
            h->sl_on = 1;
            dom_keep_details(d);
            dom_flatten(d);
            sc_slots_now(d, &h->sl_was);
            h->sl_rev = (uint32_t)dom_revision(d);
            h->sl_pos = (uint32_t)d->mutation_len;
        }
        return sh_wrap(vm, r.index);
    }
    case SC_SHADOW_ROOT: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t s = x && d->node[x].kind == DOM_NODE_KIND_ELEMENT ? sc_shadow_of_host(d, x) : 0;
        return s && d->shadow[s].mode == DOM_SHADOW_MODE_OPEN ? sh_wrap(vm, d->shadow[s].root) : JV_NULL;
    }
    case SC_HOST: case SC_MODE: case SC_DELEGATES: case SC_ASSIGNMENT: case SC_CLONABLE: case SC_SERIALIZABLE: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t s = x ? sc_shadow_of_root(d, x) : 0;
        if (!s) return id == SC_HOST ? JV_NULL : JV_UNDEF;
        dom_shadow sr = d->shadow[s];
        if (id == SC_HOST) return sh_wrap(vm, sr.host);
        if (id == SC_DELEGATES) return jv_bool(sr.delegates_focus);
        if (id == SC_CLONABLE) return jv_bool(sr.clonable);
        if (id == SC_SERIALIZABLE) return jv_bool(sr.serializable);
        if (id == SC_ASSIGNMENT) return sh_cstr(vm, sr.assignment == DOM_SLOT_ASSIGNMENT_MANUAL ? "manual" : "named");
        return sh_cstr(vm, sr.mode == DOM_SHADOW_MODE_OPEN ? "open" : "closed");
    }
    case SC_ASSIGNED_NODES: case SC_ASSIGNED_ELEMENTS: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        int flatten = jv_is_obj(a0) && js_truthy(vm, js_get(vm, a0, sc_atom(vm, "flatten")));
        if (vm->throwing) return JV_HOLE;
        vector<uint32_t> xs;
        dom_flatten(d);
        if (x) sc_assigned(d, x, flatten, id == SC_ASSIGNED_ELEMENTS, &xs, 0);
        uint32_t a = js_array_new(vm, 0);
        for (size_t i = 0; i < len(xs); i++) js_array_push(vm, a, sh_wrap(vm, xs[i]));
        return jv_obj(a);
    }
    case SC_ASSIGN: {
        /* assign(...nodes) (DOM 4.2.2.6): each an Element or a Text node, else a TypeError before anything changes;
           the slot's manually assigned nodes those, in that order, each once */
        uint32_t x = sh_node(vm, thisv, 1);
        if (vm->throwing) return JV_HOLE;
        if (!sh_named(d, x, "slot")) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        vector<uint32_t> ns;
        for (uint32_t i = 0; i < argc; i++) {
            jv v = js_arg(vm, args, argc, i);
            uint32_t n = jv_is_obj(v) ? sh_node(vm, v, 0) : 0;
            if (vm->throwing) return JV_HOLE;
            if (!n || (d->node[n].kind != DOM_NODE_KIND_ELEMENT && d->node[n].kind != DOM_NODE_KIND_TEXT))
                return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'assign' on 'HTMLSlotElement': The provided value is not of type '(Element or Text)'.");
            int seen = 0;
            for (size_t k = 0; k < len(ns) && !seen; k++) seen = at(ns, k) == n;
            if (!seen) push_back(ns, n);
        }
        dom_clear_slot(d, node_handle(d, x));
        for (size_t k = 0; k < len(ns); k++) dom_assign_slot(d, node_handle(d, x), node_handle(d, at(ns, k)));
        return JV_UNDEF;
    }
    case SC_SLOT_GET: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t at0 = x && d->node[x].kind == DOM_NODE_KIND_ELEMENT ? sh_attr_z(d, x, "slot") : 0;
        return at0 ? sh_span(vm, d, d->attr[at0].value) : sh_cstr(vm, "");
    }
    case SC_SLOT_SET: {
        uint32_t x = sh_node(vm, thisv, 1);
        if (vm->throwing) return JV_HOLE;
        vector<char> v;
        if (!sh_utf8(vm, a0, &v)) return JV_HOLE;
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) dom_set_attribute(d, node_handle(d, x), "slot", 4, data(v), len(v));
        return JV_UNDEF;
    }
    case SC_ASSIGNED_SLOT: {
        uint32_t x = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        dom_flatten(d);
        uint32_t slot = x ? sc_slot_for(d, x) : 0;
        /* a closed tree's slot is not shown */
        uint32_t s = slot ? sc_shadow_of_root(d, sc_root_of(d, slot)) : 0;
        return s && d->shadow[s].mode == DOM_SHADOW_MODE_OPEN ? sh_wrap(vm, slot) : JV_NULL;
    }
    case SC_COMPOSED_PATH: {
        /* the path as the listener may see it (DOM 2.9): from its current target, the nodes of each closed tree
           it is not in left out */
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        jv p = js_get(vm, thisv, sc_atom(vm, "\001path"));
        uint32_t a = js_array_new(vm, 0);
        (void)a1;
        if (!jv_is_obj(p)) return jv_obj(a);
        jv lv = js_get(vm, p, sc_atom(vm, "length"));
        uint32_t n = jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0;
        if (!n) return jv_obj(a);
        jv cur = js_get(vm, thisv, sc_atom(vm, "currentTarget"));
        vector<jv> items;
        vector<uint8_t> closed_root, closed_slot;
        for (uint32_t i = 0; i < n; i++) {
            jv it = js_array_get(vm, jv_idx(p), i);
            push_back(items, it);
            uint32_t x = jv_is_obj(it) && vm->objs[jv_idx(it)].c == HK_NODE ? sh_node(vm, it, 0) : 0;
            uint32_t rs = x ? sc_shadow_of_root(d, x) : 0;
            push_back(closed_root, (uint8_t)(rs && d->shadow[rs].mode == DOM_SHADOW_MODE_CLOSED));
            uint32_t ss = x && sh_named(d, x, "slot") ? sc_shadow_of_root(d, sc_root_of(d, x)) : 0;
            push_back(closed_slot, (uint8_t)(ss && d->shadow[ss].mode == DOM_SHADOW_MODE_CLOSED));
        }
        if (jv_is_nullish(cur)) return jv_obj(a);
        int ci = 0, hidden = 0;
        for (int i = (int)n - 1; i >= 0; i--) {
            if (at(closed_root, (size_t)i)) hidden++;
            if (at(items, (size_t)i) == cur) { ci = i; break; }
            if (at(closed_slot, (size_t)i)) hidden--;
        }
        vector<jv> before;
        int lvl = hidden, mx = hidden;
        for (int i = ci - 1; i >= 0; i--) {
            if (at(closed_root, (size_t)i)) lvl++;
            if (lvl <= mx) push_back(before, at(items, (size_t)i));
            if (at(closed_slot, (size_t)i)) { lvl--; if (lvl < mx) mx = lvl; }
        }
        for (size_t i = len(before); i > 0; i--) js_array_push(vm, a, at(before, i - 1));
        js_array_push(vm, a, cur);
        lvl = hidden;
        mx = hidden;
        for (uint32_t i = (uint32_t)ci + 1; i < n; i++) {
            if (at(closed_slot, i)) lvl++;
            if (lvl <= mx) js_array_push(vm, a, at(items, i));
            if (at(closed_root, i)) { lvl--; if (lvl < mx) mx = lvl; }
        }
        return jv_obj(a);
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* the collector's roots of this unit (script.h's sh_cdef, the host's cwait and registry):
   each definition's constructor, prototype and callbacks, each whenDefined promise not
   yet settled (its name an atom, which the engine keeps), the registry (an element's state
   and the construction stack hold nodes and places, no value) */
void script_custom_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    for (size_t i = 0; i < len(h->cdefs); i++) {
        sh_cdef c = h->cdefs[i];
        uint32_t objs[6] = { c.ctor, c.proto, c.connected, c.disconnected, c.adopted, c.attr_changed };
        for (int k = 0; k < 6; k++) if (objs[k]) push_back(*out, jv_obj(objs[k]));
    }
    for (size_t i = 0; i + 2 < len(h->cwait); i += 3) if (!h->cwait[i + 2] && h->cwait[i]) push_back(*out, jv_obj(h->cwait[i]));
    if (h->registry) push_back(*out, jv_obj(h->registry));
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
    sh_method(vm, elem, "assign", SC_ASSIGN, 0);
    {
        uint32_t sa = sc_atom(vm, "slot");
        js_define_accessor(vm, elem, sa, jv_obj(js_native_new(vm, SC_SLOT_GET, sa, 0)), jv_obj(js_native_new(vm, SC_SLOT_SET, sa, 1)), 0);
    }
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
    static const char *const sg[] = { "host", "mode", "delegatesFocus", "slotAssignment", "clonable", "serializable" };
    static const uint32_t sgi[] = { SC_HOST, SC_MODE, SC_DELEGATES, SC_ASSIGNMENT, SC_CLONABLE, SC_SERIALIZABLE };
    for (int i = 0; i < 6; i++) {
        uint32_t at = sc_atom(vm, sg[i]);
        js_define_accessor(vm, sr, at, jv_obj(js_native_new(vm, sgi[i], at, 0)), JV_UNDEF, 0);
    }
    sh_interface(vm, "ShadowRoot", SC_ILLEGAL, sr);
    sh_method(vm, h->protos[HP_EVENT], "composedPath", SC_COMPOSED_PATH, 0);
    /* the slots' nodes as the parse left them: slotchange from here */
    sc_slots_begin(vm);
}
