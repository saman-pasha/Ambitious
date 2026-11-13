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

   CUSTOMIZED BUILT-IN ELEMENTS: define(name, class, { extends }) of an HTML element's local
   name (not a custom element's, nor one Chromium makes an HTMLUnknownElement); the elements
   of that local name whose is value is the name upgraded as an autonomous one's are (the is
   value createElement(name, { is }) or the constructor gave, else the is attribute's), and
   written with an is attribute when they have none; the class extends the interface of the
   element (HTMLButtonElement ...), each of Chromium's HTML element interfaces a constructor
   whose prototype every element of its names' wrapper has (HTMLElement's for the rest, and
   HTMLUnknownElement's for a name HTML does not know). The interfaces are made when a
   script first names one -- reads its name from the window, which the global's prototype
   (the engine's object for a host) answers until then -- or defines a customized built-in
   element: until then every element's wrapper has HTMLElement's prototype, which keeps a
   page that names none of them as fast as before.

   FORM-ASSOCIATED CUSTOM ELEMENTS (static formAssociated): attachInternals() once, on an
   autonomous custom element; ElementInternals' form, setFormValue (a string or null; what
   the form's entry list sends under the element's name), setValidity, validity,
   validationMessage, willValidate, checkValidity and reportValidity (the form's and
   constraint validation's: src/script/input.c), and states (a set of the element's custom
   states); the element is listed in its form's elements, validated and reset with it
   (formResetCallback), and told when its form owner changes (formAssociatedCallback) and
   when it becomes disabled or enabled (formDisabledCallback), at the end of each call into
   the host.

   What is not done: :state() is not matched (the cascade does not see an element's custom
   states); formStateRestoreCallback never comes (no state is restored); a form value that is
   a File or a FormData is its string; ElementInternals' labels and ARIA mixin are not there;
   focus is not delegated
   (delegatesFocus is kept and read back); an HTML element interface's own members are the
   shared element prototype's (HTMLInputElement.prototype holds no value); before a script
   names an interface, an element's prototype and constructor are HTMLElement's, and the
   window has none of the interfaces as its own property. A shadow root's styleSheets and
   adoptedStyleSheets are src/script/cssom.c's. */
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
#include "../html/forms.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);

enum {
    SC_HTML_ELEMENT = SH_CUSTOM_FIRST, SC_DEFINE, SC_GET, SC_WHEN_DEFINED, SC_UPGRADE, SC_ATTACH_SHADOW, SC_SHADOW_ROOT, SC_HOST,
    SC_MODE, SC_ASSIGNED_NODES, SC_ASSIGNED_ELEMENTS, SC_ASSIGNED_SLOT, SC_COMPOSED_PATH, SC_ILLEGAL, SC_DELEGATES, SC_ASSIGNMENT,
    SC_CLONABLE, SC_SERIALIZABLE, SC_ASSIGN, SC_SLOT_GET, SC_SLOT_SET, SC_IFACE, SC_ATTACH_INTERNALS, SC_EI_FORM, SC_EI_WILL,
    SC_EI_VALIDITY, SC_EI_MESSAGE, SC_EI_STATES, SC_EI_SET_VALUE, SC_EI_SET_VALIDITY, SC_EI_CHECK, SC_EI_REPORT, SC_N
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
uint32_t script_custom_def(js_vm *vm, const char *s, size_t n) {
    uint32_t def = sc_def_of_name(vm->host, s, n);
    /* a customized built-in one's name is no element's: createElement of it makes an element of that name */
    return def && !vm->host->cdefs[def - 1].llen ? def : 0;
}
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
/* x's is value (HTML 4.13.3): the one createElement(name, { is }) or a customized built-in
   one's constructor gave it, else its is attribute's: 1 and its text in out */
static int sc_is_value(struct js_host *h, uint32_t x, vector<char> *out) {
    const dom_document *d = h->d;
    clear(*out);
    uint32_t k = sc_el(h, x);
    if (k && h->cels[k - 1].islen) {
        append_range(*out, data(h->ctext) + h->cels[k - 1].isoff, h->cels[k - 1].islen);
        return 1;
    }
    uint32_t a = sh_attr_z(d, x, "is");
    if (!a || !d->attr[a].value.len) return 0;
    append_range(*out, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
    return 1;
}
/* the definition an HTML element of the document is of (its place plus one, 0 for none): an
   autonomous one of its local name, or a customized built-in one its is value names that
   extends its local name */
static uint32_t sc_def_for(struct js_host *h, uint32_t x) {
    const dom_document *d = h->d;
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    vector<char> nm, is;
    sc_name(d, x, &nm);
    uint32_t def = sc_def_of_name(h, data(nm), len(nm));
    if (def) return h->cdefs[def - 1].llen ? 0 : def;
    if (!sc_is_value(h, x, &is)) return 0;
    def = sc_def_of_name(h, data(is), len(is));
    if (!def) return 0;
    sh_cdef c = h->cdefs[def - 1];
    return c.llen == len(nm) && memcmp(data(h->ctext) + c.loff, data(nm), len(nm)) == 0 ? def : 0;
}
/* x's is value given (createElement's, a constructor's) */
static void sc_set_is(struct js_host *h, uint32_t x, const char *s, size_t n) {
    uint32_t k = sc_el(h, x);
    if (!k) {
        const dom_document *d = h->d;
        sh_cel e;
        memset(&e, 0, sizeof e);
        e.node = x;
        e.gen = d->node[x].gen;
        push_back(h->cels, e);
        if (x >= len(h->cmap)) resize(h->cmap, (size_t)d->node_len + 64, 0u);
        h->cmap[x] = (uint32_t)len(h->cels);
        k = h->cmap[x];
    }
    h->cels[k - 1].isoff = (uint32_t)len(h->ctext);
    append_range(h->ctext, s, n);
    h->cels[k - 1].islen = (uint32_t)n;
}
/* an element's is value as its serialization writes it (one given, not its attribute's): 1 and its text */
int script_custom_is_given(const struct js_host *h, uint32_t x, vector<char> *out) {
    clear(*out);
    if (x >= len(h->cmap) || !h->cmap[x]) return 0;
    sh_cel e = h->cels[h->cmap[x] - 1];
    if (e.gen != h->d->node[x].gen || !e.islen) return 0;
    append_range(*out, data(h->ctext) + e.isoff, e.islen);
    return 1;
}

/* ---- the HTML element interfaces (HTML 3.2.8): Chromium's, each with its local names ---- */
static const char *const sc_iface_names[] = {
    "HTMLAnchorElement", "a", "HTMLAreaElement", "area", "HTMLAudioElement", "audio", "HTMLBRElement", "br", "HTMLBaseElement", "base",
    "HTMLBodyElement", "body", "HTMLButtonElement", "button", "HTMLCanvasElement", "canvas", "HTMLDListElement", "dl",
    "HTMLDataElement", "data", "HTMLDataListElement", "datalist", "HTMLDetailsElement", "details", "HTMLDialogElement", "dialog",
    "HTMLDirectoryElement", "dir", "HTMLDivElement", "div", "HTMLEmbedElement", "embed", "HTMLFieldSetElement", "fieldset",
    "HTMLFontElement", "font", "HTMLFormElement", "form", "HTMLFrameElement", "frame", "HTMLFrameSetElement", "frameset",
    "HTMLHRElement", "hr", "HTMLHeadElement", "head", "HTMLHeadingElement", "h1 h2 h3 h4 h5 h6", "HTMLHtmlElement", "html",
    "HTMLIFrameElement", "iframe", "HTMLImageElement", "img", "HTMLInputElement", "input", "HTMLLIElement", "li",
    "HTMLLabelElement", "label", "HTMLLegendElement", "legend", "HTMLLinkElement", "link", "HTMLMapElement", "map",
    "HTMLMarqueeElement", "marquee", "HTMLMenuElement", "menu", "HTMLMetaElement", "meta", "HTMLMeterElement", "meter",
    "HTMLModElement", "del ins", "HTMLOListElement", "ol", "HTMLObjectElement", "object", "HTMLOptGroupElement", "optgroup",
    "HTMLOptionElement", "option", "HTMLOutputElement", "output", "HTMLParagraphElement", "p", "HTMLParamElement", "param",
    "HTMLPictureElement", "picture", "HTMLPreElement", "pre listing xmp", "HTMLProgressElement", "progress",
    "HTMLQuoteElement", "blockquote q", "HTMLScriptElement", "script", "HTMLSelectElement", "select", "HTMLSlotElement", "slot",
    "HTMLSourceElement", "source", "HTMLSpanElement", "span", "HTMLStyleElement", "style", "HTMLTableCaptionElement", "caption",
    "HTMLTableCellElement", "td th", "HTMLTableColElement", "col colgroup", "HTMLTableElement", "table",
    "HTMLTableRowElement", "tr", "HTMLTableSectionElement", "thead tbody tfoot", "HTMLTemplateElement", "template",
    "HTMLTextAreaElement", "textarea", "HTMLTimeElement", "time", "HTMLTitleElement", "title", "HTMLTrackElement", "track",
    "HTMLUListElement", "ul", "HTMLVideoElement", "video" };
enum { SC_IFACES = (int)(sizeof sc_iface_names / sizeof sc_iface_names[0]) / 2 };
/* the names Chromium makes HTMLElements (the custom elements' too) */
static const char sc_plain_names[] = " abbr acronym address article aside b basefont bdi bdo big center cite code dd dfn dt em "
    "figcaption figure footer header hgroup i kbd main mark nav nobr noembed noframes noscript plaintext rb rp rt rtc ruby s samp "
    "search section small strike strong sub summary sup tt u var wbr ";
/* the word w[0, n) is one of the space-separated words of list */
static int sc_word_in(const char *list, const char *w, size_t n) {
    size_t i = 0, ln = strlen(list);
    while (i < ln) {
        while (i < ln && list[i] == ' ') i++;
        size_t b = i;
        while (i < ln && list[i] != ' ') i++;
        if (i - b == n && n && memcmp(list + b, w, n) == 0) return 1;
    }
    return 0;
}
/* the interface of an HTML element's local name: its place in sc_iface_names (0 ..), -1 for
   HTMLElement, -2 for HTMLUnknownElement */
static int sc_iface_of(const char *s, size_t n) {
    for (int i = 0; i < SC_IFACES; i++) if (sc_word_in(sc_iface_names[2 * i + 1], s, n)) return i;
    if (sc_word_in(sc_plain_names, s, n) || sc_valid_name(s, n)) return -1;
    return -2;
}
uint32_t script_iface_proto(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    if (!h->ifaces_on) return 0;
    const dom_document *d = h->d;
    uint32_t nm = d->node[x].name;
    if (nm < len(h->iproto) && h->iproto[nm]) return h->iproto[nm] - 1;
    if (!len(h->iface)) return 0;
    int k = sc_iface_of(names_ptr(d, nm), names_len(d, nm));
    uint32_t p = k >= 0 ? h->iface[(size_t)k] : k == -2 ? h->iface[SC_IFACES] : h->protos[HP_ELEMENT];
    if (nm >= len(h->iproto)) resize(h->iproto, (size_t)nm + 64, 0u);
    h->iproto[nm] = p + 1;
    return p;
}

/* the HTML element interfaces made, the first time a script names one (it reads one of their
   names from the global, which has none of them until then: the global's prototype, an
   object of the host's, answers it, script_custom_iface_get), or defines a customized
   built-in element. Each interface's constructor, its prototype (HTMLElement's below it)
   and its global; each HTML element's wrapper made before given its interface's prototype
   (one an upgrade, or a script, gave another kept). Until then every wrapper has
   HTMLElement's prototype, a property's look one step shorter and the wrappers one shape
   in the engine's caches, and a page that names none makes none of them */
static void sc_ifaces_on(js_vm *vm) {
    struct js_host *h = vm->host;
    if (h->ifaces_on || !h->hef) return;
    h->ifaces_on = 1;
    uint32_t elem = h->protos[HP_ELEMENT], hef = h->hef, g = vm->global;
    clear(h->iface);
    for (int i = 0; i <= SC_IFACES; i++) push_back(h->iface, js_obj_new(vm, JK_PLAIN, elem));
    uint32_t ia = sc_atom(vm, "\001iface");
    for (int i = 0; i <= SC_IFACES; i++) {
        uint32_t ip = h->iface[(size_t)i], at = sc_atom(vm, i < SC_IFACES ? sc_iface_names[2 * i] : "HTMLUnknownElement");
        uint32_t f = js_native_new(vm, i < SC_IFACES ? SC_IFACE : SC_ILLEGAL, at, 0);
        js_set_proto(vm, f, hef);
        js_define(vm, f, A_PROTOTYPE, jv_obj(ip), JS_SHAPE_PROTO);
        js_define(vm, ip, A_CONSTRUCTOR, jv_obj(f), JS_SHAPE_HIDDEN);
        js_define(vm, f, ia, jv_num(i), JS_SHAPE_HIDDEN);
        push_back(h->iface, f);
        /* the global's own, but where a script made one of that name first */
        uint32_t e = js_shape_find(vm, vm->objs[g].shape, at);
        if (!e || vm->slots[vm->objs[g].slots + vm->shapes[e].slot] == JV_HOLE) js_define(vm, g, at, jv_obj(f), JS_SHAPE_HIDDEN);
    }
    for (uint32_t x = 1; x < len(vm->wrappers) && x < h->d->node_len; x++) {
        uint32_t w = vm->wrappers[x];
        if (!w || vm->objs[w].kind != JK_HOST || vm->objs[w].c != HK_NODE || vm->objs[w].a != x || vm->objs[w].b != h->d->node[x].gen) continue;
        if (h->d->node[x].kind != DOM_NODE_KIND_ELEMENT || h->d->node[x].ns != DOM_NAMESPACE_HTML || vm->objs[w].proto != elem) continue;
        uint32_t ip = script_iface_proto(vm, x);
        if (ip && ip != elem) js_set_proto(vm, w, ip);
    }
}

/* the interface an atom names: its place in sc_iface_names, SC_IFACES for HTMLUnknownElement,
   -1 for none (a name that is not HTML...Element told at once) */
static int sc_iface_named(js_vm *vm, uint32_t atom) {
    if (js_key_is_sym(atom)) return -1;
    js_str_flatten(vm, atom);
    uint32_t n = vm->strs[atom].len, off = vm->strs[atom].off;
    if (n < 12 || n > 40 || vm->units[off] != 'H' || vm->units[off + 1] != 'T' || vm->units[off + 2] != 'M' || vm->units[off + 3] != 'L' ||
        vm->units[off + n - 1] != 't')
        return -1;
    char buf[48];
    for (uint32_t i = 0; i < n; i++) {
        uint16_t u = vm->units[off + i];
        if (u > 127) return -1;
        buf[i] = (char)u;
    }
    buf[n] = 0;
    for (int i = 0; i < SC_IFACES; i++) if (strcmp(buf, sc_iface_names[2 * i]) == 0) return i;
    return strcmp(buf, "HTMLUnknownElement") == 0 ? SC_IFACES : -1;
}
jv script_custom_iface_get(js_vm *vm, uint32_t atom, int *handled) {
    *handled = 0;
    if (vm->host->ifaces_on || sc_iface_named(vm, atom) < 0) return JV_UNDEF;
    sc_ifaces_on(vm);
    jv v = JV_UNDEF;
    uint32_t f = 0;
    if (!js_get_own(vm, vm->global, atom, &v, &f) || (f & JS_SHAPE_ACCESSOR)) return JV_UNDEF;
    *handled = 1;
    return v;
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
    vector<uint32_t> xs;
    for (uint32_t x = scope; x; x = sc_next(d, x, scope)) {
        if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) continue;
        if (sc_def_for(h, x) == def) push_back(xs, x);
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
        uint32_t def = sc_def_for(h, x);
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
            uint32_t def = sc_def_for(h, x);
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
    /* what the form-associated elements were not told yet */
    sc_face_check(vm);
    h->in_reactions = 0;
}

/* createElement(name, { is }) (DOM 4.5, create an element): an element of the local name with
   that is value, upgraded at once when a customized built-in definition of the name extends
   the local name (the synchronous custom elements flag), else an element waiting for one */
jv script_custom_create_is(js_vm *vm, const char *name, size_t n, const char *is, size_t isn) {
    struct js_host *h = vm->host;
    dom_node_h e = dom_create_element(h->d, name, n);
    if (!e.index) return sh_status(vm, DOM_INVALID_NAME);
    sc_set_is(h, e.index, is, isn);
    uint32_t def = sc_def_for(h, e.index);
    if (def) sc_upgrade(vm, e.index, def);
    return sh_wrap(vm, e.index);
}

/* ---- the natives ---- */

/* define(name, constructor, options) (HTML 4.13.4) */
static jv sc_define(js_vm *vm, jv name, jv ctor, jv opts) {
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
    /* a customized built-in one: the HTML element's local name it extends */
    vector<char> ext;
    int has_ext = 0;
    if (jv_is_obj(opts)) {
        jv e = js_get(vm, opts, sc_atom(vm, "extends"));
        if (vm->throwing) return JV_HOLE;
        if (e != JV_UNDEF) {
            if (!sh_utf8(vm, e, &ext)) return JV_HOLE;
            has_ext = 1;
            sc_ifaces_on(vm);
            if (sc_valid_name(data(ext), len(ext))) {
                snprintf(msg, sizeof msg, "%s\"%.*s\" is a valid custom element name", pre, (int)len(ext), data(ext));
                return sh_dom_error(vm, "NotSupportedError", msg);
            }
            if (sc_iface_of(data(ext), len(ext)) == -2) {
                snprintf(msg, sizeof msg, "%s\"%.*s\" is an HTMLUnknownElement", pre, (int)len(ext), data(ext));
                return sh_dom_error(vm, "NotSupportedError", msg);
            }
        }
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
    /* form-associated: its form callbacks (HTML 4.13.4, step 14) */
    jv fa = js_get(vm, ctor, sc_atom(vm, "formAssociated"));
    if (vm->throwing) return JV_HOLE;
    c.face = (uint32_t)js_truthy(vm, fa);
    if (c.face) {
        static const char *const fcbs[] = { "formAssociatedCallback", "formResetCallback", "formDisabledCallback", "formStateRestoreCallback" };
        uint32_t fgot[4];
        for (int i = 0; i < 4; i++) {
            jv f = js_get(vm, proto, sc_atom(vm, fcbs[i]));
            if (vm->throwing) return JV_HOLE;
            fgot[i] = 0;
            if (f == JV_UNDEF) continue;
            if (!js_is_callable(vm, f)) {
                snprintf(msg, sizeof msg, "%sThe '%s' property on the prototype is not a function.", pre, fcbs[i]);
                return js_throw_msg(vm, vm->type_error_proto, msg);
            }
            fgot[i] = jv_idx(f);
        }
        c.f_assoc = fgot[0];
        c.f_reset = fgot[1];
        c.f_disabled = fgot[2];
        c.f_restore = fgot[3];
    }
    c.noff = (uint32_t)len(h->ctext);
    append_range(h->ctext, data(nm), len(nm));
    c.nlen = (uint32_t)len(nm);
    c.ooff = (uint32_t)len(h->ctext);
    append_range(h->ctext, data(obs), len(obs));
    c.olen = (uint32_t)len(obs);
    c.loff = (uint32_t)len(h->ctext);
    if (has_ext) append_range(h->ctext, data(ext), len(ext));
    c.llen = has_ext ? (uint32_t)len(ext) : 0;
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
/* an HTML element interface's constructor (HTML 3.2.3, the HTML element constructors),
   reached by a custom element class's super(): new.target from that constructor's frame
   (its class extends this interface), its definition's element made -- a customized
   built-in one's of the local name it extends, its is value the definition's name -- or the
   one being upgraded. iface: the interface's place in sc_iface_names, -1 HTMLElement's */
static jv sc_element_ctor(js_vm *vm, jv callee, int iface) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    jv newt = JV_UNDEF;
    if (vm->nframes) {
        js_frame f = vm->frames[vm->nframes - 1];
        if (f.callee && jv_is_obj(callee) && vm->objs[f.callee].proto == jv_idx(callee) && jv_is_obj(f.newt)) newt = f.newt;
    }
    uint32_t def = jv_is_obj(newt) ? sc_def_of_ctor(h, jv_idx(newt)) : 0;
    char msg[300];
    if (!def) {
        snprintf(msg, sizeof msg, "Failed to construct '%s': Illegal constructor", iface >= 0 ? sc_iface_names[2 * iface] : "HTMLElement");
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    /* the definition's interface is this one: an autonomous one's HTMLElement, a customized built-in one's its local name's */
    sh_cdef dc = h->cdefs[def - 1];
    int want = dc.llen ? sc_iface_of(data(h->ctext) + dc.loff, dc.llen) : -1;
    if (want != iface) {
        snprintf(msg, sizeof msg, "Failed to construct '%s': Illegal constructor: localName does not match the HTML element interface",
                 iface >= 0 ? sc_iface_names[2 * iface] : "HTMLElement");
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    sh_cdef c = h->cdefs[def - 1];
    jv proto = js_get(vm, newt, A_PROTOTYPE);
    if (vm->throwing) return JV_HOLE;
    if (!len(h->cstack)) {
        /* a new element of the definition's name, or of the local name it extends */
        dom_node_h e = c.llen ? dom_create_element(d, data(h->ctext) + c.loff, c.llen) : dom_create_element(d, data(h->ctext) + c.noff, c.nlen);
        if (!e.index) return sh_status(vm, DOM_INVALID_NAME);
        if (c.llen) {
            vector<char> is;
            append_range(is, data(h->ctext) + c.noff, c.nlen);
            sc_set_is(h, e.index, data(is), len(is));
        }
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
/* ---- form-associated custom elements (HTML 4.13.7) ---- */

/* x is a custom element its form-associated definition made */
static int sc_face_cel(const struct js_host *h, uint32_t x, sh_cel *out) {
    const dom_document *d = h->d;
    if (!x || x >= d->node_len || x >= len(h->cmap) || !h->cmap[x]) return 0;
    sh_cel e = h->cels[h->cmap[x] - 1];
    if (e.gen != d->node[x].gen || e.state != SC_CUSTOM || !e.def || !h->cdefs[e.def - 1].face) return 0;
    *out = e;
    return 1;
}
int script_custom_face(const struct js_host *h, uint32_t x) {
    sh_cel e;
    return len(h->cdefs) && sc_face_cel(h, x, &e);
}
/* x is form-associated to its ElementInternals: a form-associated custom element, or one being
   constructed (undefined, or failed while its upgrade runs) whose name's definition is
   form-associated, as Chromium's ElementInternals::IsTargetFormAssociated lets a constructor
   use the form's operations */
static int sc_face_ei(struct js_host *h, uint32_t x) {
    if (script_custom_face(h, x)) return 1;
    uint32_t k = sc_el(h, x);
    uint32_t st = k ? h->cels[k - 1].state : 0;
    if (st == SC_CUSTOM) return 0;
    uint32_t def = sc_def_for(h, x);
    return def && h->cdefs[def - 1].face && !h->cdefs[def - 1].llen;
}
/* a form-associated element's internals' state, hidden on its ElementInternals: 1 and the
   value, when it has internals and the state is set */
static int sc_ei_state(js_vm *vm, uint32_t x, const char *name, jv *out) {
    uint32_t k = sc_el(vm->host, x);
    if (!k || !vm->host->cels[k - 1].internals || !sc_face_ei(vm->host, x)) return 0;
    uint32_t f;
    return js_get_own(vm, vm->host->cels[k - 1].internals, sc_atom(vm, name), out, &f);
}
int script_custom_will_validate(js_vm *vm, uint32_t x) {
    const dom_document *d = vm->host->d;
    if (!script_custom_face(vm->host, x) || form_disabled(d, x) || sh_attr_z(d, x, "readonly")) return 0;
    for (uint32_t p = d->node[x].parent; p; p = d->node[p].parent) if (sh_named(d, p, "datalist")) return 0;
    return 1;
}
/* the flags setValidity gave (src/html/forms.h's FORM_V_*), its message into msg */
uint32_t script_custom_flags(js_vm *vm, uint32_t x, vector<char> *msg) {
    clear(*msg);
    jv f, m;
    uint32_t fl = sc_ei_state(vm, x, "\001flags", &f) && jv_is_num(f) ? (uint32_t)jv_dbl(f) : 0;
    if (fl && sc_ei_state(vm, x, "\001message", &m) && jv_is_str(m)) js_str_to_utf8(vm, jv_idx(m), msg);
    return fl;
}
/* the form value setFormValue gave: 1 and its text, 0 for none (null) */
int script_custom_form_value(js_vm *vm, uint32_t x, vector<char> *out) {
    clear(*out);
    jv v;
    if (!sc_ei_state(vm, x, "\001value", &v) || !jv_is_str(v)) return 0;
    js_str_to_utf8(vm, jv_idx(v), out);
    return 1;
}
/* a form reset: formResetCallback of each form-associated element it owns, in tree order */
void script_custom_reset(js_vm *vm, uint32_t form) {
    struct js_host *h = vm->host;
    if (!len(h->cdefs)) return;
    const dom_document *d = h->d;
    vector<uint32_t> xs;
    for (uint32_t x = 1; x; x = sc_next(d, x, 1)) if (script_custom_face(h, x) && form_owner(d, x) == form) push_back(xs, x);
    for (size_t i = 0; i < len(xs) && vm->throwing != 2; i++) {
        sh_cel e;
        if (!sc_face_cel(h, xs[i], &e)) continue;
        sc_callback(vm, xs[i], h->cdefs[e.def - 1].f_reset, 0, 0);
    }
}
/* each form-associated element told what changed since it was last told (the end of a call
   into the host): its form owner (formAssociatedCallback), its disabled state
   (formDisabledCallback) */
static void sc_face_check(js_vm *vm) {
    struct js_host *h = vm->host;
    for (size_t i = 0; i < len(h->cels) && vm->throwing != 2; i++) {
        sh_cel e = h->cels[i];
        const dom_document *d = h->d;
        uint32_t x = e.node;
        if (e.state != SC_CUSTOM || !e.def || !h->cdefs[e.def - 1].face || x >= d->node_len || !d->node[x].live || d->node[x].gen != e.gen) continue;
        uint32_t form = form_owner(d, x), dis = (uint32_t)form_disabled(d, x);
        sh_cdef c = h->cdefs[e.def - 1];
        if (e.ftold != form + 1 && !(e.ftold == 0 && form == 0)) {
            h->cels[i].ftold = form + 1;
            jv a[1] = { form ? sh_wrap(vm, form) : JV_NULL };
            sc_callback(vm, x, c.f_assoc, a, 1);
        } else if (e.ftold == 0) h->cels[i].ftold = 1;
        if (e.dtold != dis + 1 && !(e.dtold == 0 && !dis)) {
            h->cels[i].dtold = dis + 1;
            jv a[1] = { jv_bool(dis) };
            sc_callback(vm, x, c.f_disabled, a, 1);
        } else if (e.dtold == 0) h->cels[i].dtold = 1;
    }
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
    case SC_HTML_ELEMENT: return sc_element_ctor(vm, callee, -1);
    case SC_IFACE: {
        /* an HTML element interface's constructor: its place, kept on the function */
        jv k = jv_is_obj(callee) ? js_get(vm, callee, sc_atom(vm, "\001iface")) : JV_UNDEF;
        vm->throwing = 0;
        return sc_element_ctor(vm, callee, jv_is_num(k) ? (int)jv_dbl(k) : -2);
    }
    case SC_DEFINE: return sc_define(vm, a0, a1, js_arg(vm, args, argc, 2));
    case SC_ATTACH_INTERNALS: {
        uint32_t x = sh_node(vm, thisv, 1);
        if (vm->throwing) return JV_HOLE;
        static const char *const pre = "Failed to execute 'attachInternals' on 'HTMLElement': ";
        char msg[300];
        vector<char> is;
        uint32_t k = sc_el(h, x);
        if (sc_is_value(h, x, &is) && k) {
            snprintf(msg, sizeof msg, "%sUnable to attach ElementInternals to a customized built-in element.", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        uint32_t def = sc_def_for(h, x);
        if (!def || !k || h->cels[k - 1].def != def) {
            snprintf(msg, sizeof msg, "%sUnable to attach ElementInternals to non-custom elements.", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        if (h->cels[k - 1].internals) {
            snprintf(msg, sizeof msg, "%sElementInternals for the specified element was already attached.", pre);
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        uint32_t o = js_obj_new(vm, JK_HOST, h->protos[HP_INTERNALS]);
        vm->objs[o].a = x;
        vm->objs[o].b = d->node[x].gen;
        vm->objs[o].c = HK_INTERNALS;
        k = sc_el(h, x);
        h->cels[k - 1].internals = o;
        return jv_obj(o);
    }
    case SC_EI_FORM: case SC_EI_WILL: case SC_EI_VALIDITY: case SC_EI_MESSAGE: case SC_EI_STATES: case SC_EI_SET_VALUE:
    case SC_EI_SET_VALIDITY: case SC_EI_CHECK: case SC_EI_REPORT: {
        /* ElementInternals' members, of its element (each but states its form-associated element's alone) */
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST || vm->objs[jv_idx(thisv)].c != HK_INTERNALS)
            return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t o = jv_idx(thisv), x = vm->objs[o].a;
        if (x >= d->node_len || !d->node[x].live || d->node[x].gen != vm->objs[o].b) return JV_UNDEF;
        if (id == SC_EI_STATES) {
            /* a set of the element's custom states, made once (a Set, its prototype CustomStateSet's) */
            jv cur;
            uint32_t f;
            if (js_get_own(vm, o, sc_atom(vm, "\001states"), &cur, &f) && jv_is_obj(cur)) return cur;
            jv setc = js_get(vm, jv_obj(vm->global), sc_atom(vm, "Set"));
            if (vm->throwing) return JV_HOLE;
            jv st = js_construct(vm, setc, 0, 0, setc);
            if (vm->throwing || !jv_is_obj(st)) return JV_HOLE;
            js_set_proto(vm, jv_idx(st), h->protos[HP_CUSTOMSTATESET]);
            js_define(vm, o, sc_atom(vm, "\001states"), st, JS_SHAPE_HIDDEN);
            return st;
        }
        static const char *const what[] = { "form", "willValidate", "validity", "validationMessage", "", "setFormValue", "setValidity",
                                            "checkValidity", "reportValidity" };
        if (!sc_face_ei(h, x)) {
            char msg[300];
            int prop = id <= SC_EI_MESSAGE;
            snprintf(msg, sizeof msg, "Failed to %s '%s' %s 'ElementInternals': The target element is not a form-associated custom element.",
                     prop ? "read the" : "execute", what[id - SC_EI_FORM], prop ? "property from" : "on");
            return sh_dom_error(vm, "NotSupportedError", msg);
        }
        if (id == SC_EI_FORM) { uint32_t f = form_owner(d, x); return f ? sh_wrap(vm, f) : JV_NULL; }
        if (id == SC_EI_WILL) return jv_bool(script_custom_will_validate(vm, x));
        if (id == SC_EI_VALIDITY) return script_validity_object(vm, x);
        if (id == SC_EI_MESSAGE) return script_validation_message(vm, x);
        if (id == SC_EI_CHECK || id == SC_EI_REPORT) return script_control_validity(vm, x, id == SC_EI_REPORT);
        if (id == SC_EI_SET_VALUE) {
            jv v = JV_NULL;
            if (!jv_is_nullish(a0)) {
                uint32_t t = jv_is_str(a0) ? jv_idx(a0) : js_to_string(vm, a0);
                if (!t) return JV_HOLE;
                v = jv_str(t);
            }
            js_define(vm, o, sc_atom(vm, "\001value"), v, JS_SHAPE_HIDDEN);
            return JV_UNDEF;
        }
        /* setValidity(flags, message): a message wanted when a flag holds */
        static const char *const flags[] = { "valueMissing", "typeMismatch", "patternMismatch", "tooLong", "tooShort", "rangeUnderflow",
                                             "rangeOverflow", "stepMismatch", "badInput", "customError" };
        static const uint32_t bits[] = { FORM_V_MISSING, FORM_V_TYPE, FORM_V_PATTERN, FORM_V_LONG, FORM_V_SHORT, FORM_V_UNDER, FORM_V_OVER,
                                         FORM_V_STEP, FORM_V_BAD, FORM_V_CUSTOM };
        uint32_t fl = 0;
        for (int i = 0; i < 10 && jv_is_obj(a0); i++) {
            jv f = js_get(vm, a0, sc_atom(vm, flags[i]));
            if (vm->throwing) return JV_HOLE;
            if (js_truthy(vm, f)) fl |= bits[i];
        }
        uint32_t m = A_EMPTY;
        if (argc > 1 && a1 != JV_UNDEF) {
            m = jv_is_str(a1) ? jv_idx(a1) : js_to_string(vm, a1);
            if (!m) return JV_HOLE;
        }
        if (fl && js_str_eq(vm, m, A_EMPTY))
            return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'setValidity' on 'ElementInternals': The second argument should not be empty if one or more flags in the first argument are true.");
        js_define(vm, o, sc_atom(vm, "\001flags"), jv_num(fl), JS_SHAPE_HIDDEN);
        js_define(vm, o, sc_atom(vm, "\001message"), jv_str(fl ? m : A_EMPTY), JS_SHAPE_HIDDEN);
        return JV_UNDEF;
    }
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
            uint32_t def = sc_def_for(h, xs[i]);
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
        uint32_t objs[10] = { c.ctor, c.proto, c.connected, c.disconnected, c.adopted, c.attr_changed, c.f_assoc, c.f_reset, c.f_disabled,
                              c.f_restore };
        for (int k = 0; k < 10; k++) if (objs[k]) push_back(*out, jv_obj(objs[k]));
    }
    for (size_t i = 0; i < len(h->cels); i++) if (h->cels[i].internals) push_back(*out, jv_obj(h->cels[i].internals));
    for (size_t i = 0; i < len(h->iface); i++) if (h->iface[i]) push_back(*out, jv_obj(h->iface[i]));
    if (h->hef) push_back(*out, jv_obj(h->hef));
    for (size_t i = 0; i + 2 < len(h->cwait); i += 3) if (!h->cwait[i + 2] && h->cwait[i]) push_back(*out, jv_obj(h->cwait[i]));
    if (h->registry) push_back(*out, jv_obj(h->registry));
}

void script_custom_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t elem = h->protos[HP_ELEMENT];
    /* HTMLElement's constructor, for its subclasses' super(), and the prototype of each HTML
       element interface's, made when a script first names one (sc_ifaces_on): in iface then
       their prototypes, the wrappers' of their elements (HTMLUnknownElement's last), then
       their constructors */
    h->hef = sh_interface(vm, "HTMLElement", SC_HTML_ELEMENT, elem);
    clear(h->iface);
    /* attachInternals, ElementInternals and CustomStateSet */
    sh_method(vm, elem, "attachInternals", SC_ATTACH_INTERNALS, 0);
    uint32_t eip = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_INTERNALS] = eip;
    static const char *const eig[] = { "form", "willValidate", "validity", "validationMessage", "states" };
    static const uint32_t eigi[] = { SC_EI_FORM, SC_EI_WILL, SC_EI_VALIDITY, SC_EI_MESSAGE, SC_EI_STATES };
    for (int i = 0; i < 5; i++) {
        uint32_t at = sc_atom(vm, eig[i]);
        js_define_accessor(vm, eip, at, jv_obj(js_native_new(vm, eigi[i], at, 0)), JV_UNDEF, 0);
    }
    sh_method(vm, eip, "setFormValue", SC_EI_SET_VALUE, 1);
    sh_method(vm, eip, "setValidity", SC_EI_SET_VALIDITY, 0);
    sh_method(vm, eip, "checkValidity", SC_EI_CHECK, 0);
    sh_method(vm, eip, "reportValidity", SC_EI_REPORT, 0);
    js_define(vm, eip, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, "ElementInternals"), JS_SHAPE_HIDDEN);
    sh_interface(vm, "ElementInternals", SC_ILLEGAL, eip);
    uint32_t css = js_obj_new(vm, JK_PLAIN, vm->set_proto);
    h->protos[HP_CUSTOMSTATESET] = css;
    js_define(vm, css, JS_SYM_KEY | JSYM_TO_STRING_TAG, sh_cstr(vm, "CustomStateSet"), JS_SHAPE_HIDDEN);
    sh_interface(vm, "CustomStateSet", SC_ILLEGAL, css);
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
