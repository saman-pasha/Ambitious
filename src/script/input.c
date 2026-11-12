/* src/script/input.c -- what a user does to a page, as its scripts see it: a pointer's
   moves, presses and clicks (Pointer Events, UI Events), keys (UI Events' KeyboardEvent),
   focus (HTML 6.6), and forms' submission (HTML 4.10.21) with FormData, and the
   navigations they make.

   THE POINTER is a mouse, as Playwright's drives Chromium. It comes onto the page at
   (0, 0), over the element there, at its first action, and moves straight to where it
   acts: the boundary events between the elements it leaves and enters (out, leave, over,
   enter, the pointer's then the mouse's), then a move; a press and a release (pointerdown
   and mousedown, pointerup and mouseup) and the click, at the element the layout's boxes
   put under the point (the view's: the deepest box or line fragment that holds it). Its
   coordinates are the element's box's centre. A press moves focus to the focusable area
   it is in, or takes it away. enter and leave events reach their target alone.
   The layout is asked for only when what it decides can be seen: a click at an element
   with no element inside it, on a page where no listener hears the pointer's moves
   between elements, goes to that element, and its events' coordinates are found when a
   listener first reads one (the layout as the page is then: an element its listeners
   moved before that read gives its new place).

   USER ACTIONS come at load, one after another: the timers due run between them, the
   last one's for the horizon (src/shell/main.c's --click and its kin).

   NAMED PROPERTIES: a form's controls by their names and ids, before its own properties
   ([LegacyOverrideBuiltIns]); its elements collection holds the controls its form
   attribute names from anywhere in the document.

   KEYS are those of a US keyboard: each key down, its keypress when it makes a character
   (and for Enter), its default action, then up. The default actions: a character typed
   into a text field or a textarea (beforeinput, the value, input), Backspace's deletion,
   Enter's implicit submission of a field's form (or a line break in a textarea, a click
   on a button or link), Space's click on a button or a box, Tab's move to the next
   element in the sequential focus order (tabindex's positive values first, then the
   document's order), Shift+Tab's back.

   FOCUS: focus() and blur(), document.activeElement and hasFocus(); blur, focusout,
   focus and focusin in Chromium's order, a field's change before its blur when its value
   changed while it had focus.

   FORMS: a submit button's click, Enter's implicit submission, requestSubmit() (its
   constraint validation: a required control empty fires invalid and stops it) and
   submit() (no event); the form data set of each control type (a checked box's or
   radio's value or on, each selected option, a textarea's value, the submitter's own),
   urlencoded, multipart or text/plain, its names and values in the form's encoding (its
   accept-charset's, else the document's: a single-byte one's bytes, or UTF-8's; a
   character it lacks as &#N;); GET with the set as its URL's query, POST as its body.

   NAVIGATION: a form's is planned, its task queued (HTML 4.10.21.3), a link's and
   location's (href, assign, replace, reload) made at once. Each is recorded with the
   fields it sent (their names, never a value); when it is made, the window's
   beforeunload fires, the loader makes its request, and the page stops (its timers and
   events end): its document stays the one that navigated, the page a navigation leads
   to fetched, not shown. As Chromium does, a form whose method is POST has its entry list
   constructed again as its navigation is made (the navigate event's formData), its
   formdata event fired a second time. A navigation to a fragment of the page changes
   location's hash, fires popstate at once and hashchange in a task. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "../net/url.h"
amb_interface(dom);
amb_interface(net);
amb_interface(html);
amb_interface(script);
#include "../html/encoding.h"
#include "../js/js.h"
#include "host.h"
#include "script.h"
HTML_DECODE_DECL;

/* script.c's, view.c's and the loader's, which take vectors or are another unit's */
int sh_utf8(js_vm *vm, jv v, vector<char> *out);
void sh_text_of(const dom_document *d, uint32_t x, vector<char> *out);
int script_view_rendered(struct js_host *h, uint32_t x);
int script_view_fresh(struct js_host *h);
int script_view_border_box(struct js_host *h, uint32_t x, double *r);
int script_view_borders(struct js_host *h, uint32_t x, double *r);
uint32_t script_view_rects(struct js_host *h, uint32_t x, double *r, uint32_t max);
net_resource_h net_fetch_request(net_loader *l, const char *url, size_t url_len, net_kind kind, const char *method, const char *sent,
                                 size_t sent_n, const char *hdrs, size_t hdrs_n, uint32_t from);

enum {
    SI_UIEVENT = SH_INPUT_FIRST, SI_MOUSEEVENT, SI_POINTEREVENT, SI_KEYBOARDEVENT, SI_FOCUSEVENT, SI_INPUTEVENT, SI_SUBMITEVENT,
    SI_FORMDATAEVENT, SI_POPSTATEEVENT, SI_HASHCHANGEEVENT, SI_ILLEGAL, SI_CHARSET, SI_MODIFIER, SI_FORMDATA, SI_FD_APPEND, SI_FD_SET, SI_FD_GET, SI_FD_GETALL, SI_FD_HAS, SI_FD_DELETE, SI_FD_ENTRIES,
    SI_FD_KEYS, SI_FD_VALUES, SI_FD_FOREACH, SI_SUBMIT, SI_REQUEST_SUBMIT, SI_RESET, SI_CHECK_VALIDITY, SI_REPORT_VALIDITY,
    SI_HAS_FOCUS, SI_COMPAT_MODE, SI_N
};
/* the modifier keys held, as bits */
#define SI_SHIFT 1u
#define SI_CTRL 2u
#define SI_ALT 4u
#define SI_META 8u

static uint32_t si_atom(js_vm *vm, const char *s) { return js_atom_ascii(vm, s); }
static void si_def(js_vm *vm, jv ev, const char *name, jv v) { js_define(vm, jv_idx(ev), si_atom(vm, name), v, 0); }
static int si_is(const dom_document *d, uint32_t x, const char *name) { return sh_named(d, x, name); }
/* an attribute's value, as text into out: 0 when x has none of that name */
static int si_attr(const dom_document *d, uint32_t x, const char *name, vector<char> *out) {
    clear(*out);
    uint32_t at = sh_attr_z(d, x, name);
    if (!at) return 0;
    append_range(*out, text_ptr(d, d->attr[at].value), d->attr[at].value.len);
    return 1;
}
static int si_eq(const vector<char> *v, const char *w) { return len(*v) == strlen(w) && sh_ci_eq(data(*v), w, len(*v)); }
/* an input's type, lower-cased (text when it has none or one not known) */
static void si_type(const dom_document *d, uint32_t x, vector<char> *out) {
    if (!si_attr(d, x, "type", out) || !len(*out)) { clear(*out); append_range(*out, "text", 4); }
    for (size_t i = 0; i < len(*out); i++) if ((*out)[i] >= 'A' && (*out)[i] <= 'Z') (*out)[i] = (char)((*out)[i] + 32);
}
static int si_input_is(const dom_document *d, uint32_t x, const char *t) {
    if (!si_is(d, x, "input")) return 0;
    vector<char> ty;
    si_type(d, x, &ty);
    return si_eq(&ty, t);
}

/* ---- a control's value: as typed or set (the wrapper's own), else its default ---- */

static void si_value(js_vm *vm, uint32_t x, vector<char> *out) {
    clear(*out);
    jv v = js_get(vm, sh_wrap(vm, x), si_atom(vm, "value"));
    if (!vm->throwing && jv_is_str(v)) js_str_to_utf8(vm, jv_idx(v), out);
    vm->throwing = 0;
}
static void si_value_set(js_vm *vm, uint32_t x, const char *p, size_t n) {
    jv s = sh_str(vm, p, n);
    js_set(vm, sh_wrap(vm, x), si_atom(vm, "value"), s);
}
/* a field a key types text into: a textarea, an input of a text type */
static int si_editable(const dom_document *d, uint32_t x) {
    if (si_is(d, x, "textarea")) return !sh_attr_z(d, x, "readonly") && !sh_attr_z(d, x, "disabled");
    if (!si_is(d, x, "input") || sh_attr_z(d, x, "readonly") || sh_attr_z(d, x, "disabled")) return 0;
    static const char *const texts[] = { "text", "search", "url", "tel", "email", "password", "number" };
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) if (si_input_is(d, x, texts[i])) return 1;
    vector<char> ty;
    si_type(d, x, &ty);
    static const char *const others[] = { "checkbox", "radio", "submit", "reset", "button", "image", "hidden", "file", "range", "color",
                                          "date", "month", "week", "time", "datetime-local" };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) if (si_eq(&ty, others[i])) return 0;
    return 1;
}

/* ---- the form's encoding (HTML 4.10.21.7; Encoding's encode, its html error mode) ---- */

/* whether an encoding is one of a byte a character */
static int si_single_byte(int enc) {
    static const char *const multi[] = { "UTF-8", "UTF-16BE", "UTF-16LE", "GBK", "gb18030", "Big5", "EUC-JP", "ISO-2022-JP", "Shift_JIS",
                                         "EUC-KR", "replacement" };
    if (enc < 0) return 0;
    for (size_t i = 0; i < sizeof multi / sizeof multi[0]; i++) if (strcmp(html_enc_name(enc), multi[i]) == 0) return 0;
    return 1;
}
/* a UTF-8 sequence's code point and length (a bad byte read as itself) */
static uint32_t si_cp(const char *s, size_t n, size_t *used) {
    unsigned char c = (unsigned char)s[0];
    uint32_t cp = c, need = 0;
    if (c >= 0xF0) { cp = c & 7; need = 3; }
    else if (c >= 0xE0) { cp = c & 15; need = 2; }
    else if (c >= 0xC0) { cp = c & 31; need = 1; }
    if (need >= n) { *used = 1; return c; }
    for (uint32_t k = 1; k <= need; k++) cp = (cp << 6) | ((unsigned char)s[k] & 0x3F);
    *used = need + 1;
    return cp;
}
/* text (UTF-8) in the encoding enc (its number; -1 UTF-8): each byte of a single-byte
   encoding found by decoding them all; a character it lacks, and any not ASCII in a
   multi-byte one but UTF-8, as &#N; */
static void si_encode(int enc, const char *s, size_t n, vector<char> *out) {
    clear(*out);
    if (enc < 0 || strcmp(html_enc_name(enc), "UTF-8") == 0) { append_range(*out, s, n); return; }
    uint32_t table[128];
    int single = si_single_byte(enc);
    for (uint32_t b = 0; single && b < 128; b++) {
        char byte = (char)(0x80 + b);
        vector<char> one;
        html_decode(enc, &byte, 1, &one);
        size_t used = 0;
        table[b] = len(one) ? si_cp(data(one), len(one), &used) : 0xFFFD;
    }
    for (size_t i = 0; i < n;) {
        size_t used = 1;
        uint32_t cp = (unsigned char)s[i] < 0x80 ? (unsigned char)s[i] : si_cp(s + i, n - i, &used);
        i += used;
        if (cp < 0x80) { push_back(*out, (char)cp); continue; }
        int put = 0;
        for (uint32_t b = 0; single && b < 128 && !put; b++)
            if (table[b] == cp && cp != 0xFFFD) { push_back(*out, (char)(0x80 + b)); put = 1; }
        if (put) continue;
        char ref[16];
        int rn = snprintf(ref, sizeof ref, "&#%u;", cp);
        append_range(*out, ref, (size_t)rn);
    }
}
/* the encoding a form submits in: its accept-charset's first known label, else the
   document's; UTF-16's as UTF-8 (-1) */
static int si_form_enc(js_vm *vm, uint32_t form) {
    const dom_document *d = vm->host->d;
    int enc = vm->host->charset - 1;
    uint32_t at = sh_attr_z(d, form, "accept-charset");
    if (at) {
        const char *t = text_ptr(d, d->attr[at].value);
        size_t n = d->attr[at].value.len, i = 0;
        while (i < n) {
            while (i < n && sh_ws(t[i])) i++;
            size_t e = i;
            while (e < n && !sh_ws(t[e])) e++;
            int got = e > i ? html_enc_of_label(t + i, e - i) : -1;
            if (got >= 0) { enc = got; break; }
            i = e;
        }
    }
    if (enc >= 0 && (strcmp(html_enc_name(enc), "UTF-16BE") == 0 || strcmp(html_enc_name(enc), "UTF-16LE") == 0)) enc = -1;
    return enc;
}

void script_charset(js_vm *vm, const char *name, size_t n) {
    int enc = html_enc_of_label(name, n);
    vm->host->charset = enc >= 0 ? enc + 1 : 0;
}

/* ---- events ---- */

/* an event of an interface's prototype, with its flags; a UI event's detail and view */
static jv si_event(js_vm *vm, const char *type, uint32_t flags, uint32_t proto, double detail) {
    jv ev = sh_event_new(vm, si_atom(vm, type), flags);
    js_set_proto(vm, jv_idx(ev), vm->host->protos[proto]);
    /* a UI event's detail and view */
    if (proto >= HP_UIEVENT && proto <= HP_INPUTEVENT) {
        vm->objs[jv_idx(ev)].x = jv_num(detail);
        si_def(vm, ev, "view", jv_obj(vm->global));
    }
    return ev;
}
static void si_modifiers(js_vm *vm, jv ev, uint32_t mods) {
    si_def(vm, ev, "ctrlKey", jv_bool(mods & SI_CTRL));
    si_def(vm, ev, "shiftKey", jv_bool(mods & SI_SHIFT));
    si_def(vm, ev, "altKey", jv_bool(mods & SI_ALT));
    si_def(vm, ev, "metaKey", jv_bool(mods & SI_META));
}
/* a mouse event's or a pointer event's fields: where, which button, which held */
/* the coordinates of a point, and the offsets from a target's padding box */
static const char *const si_coords[] = { "clientX", "clientY", "x", "y", "pageX", "pageY", "screenX", "screenY", "offsetX", "offsetY" };
static void si_point_fields(js_vm *vm, jv ev, uint32_t target, double x, double y) {
    struct js_host *h = vm->host;
    double r[4], b[4], ox = x, oy = y;
    if (target && script_view_border_box(h, target, r) && script_view_borders(h, target, b)) { ox = x - r[0] - b[3]; oy = y - r[1] - b[0]; }
    double v[10] = { x, y, x, y, x, y, x, y, ox, oy };
    for (int i = 0; i < 10; i++) si_def(vm, ev, si_coords[i], jv_num(v[i]));
}
static void si_mouse_fields(js_vm *vm, jv ev, uint32_t target, double x, double y, int button, int buttons, uint32_t related, int pointer) {
    struct js_host *h = vm->host;
    /* a click whose point no one has asked for yet: its element kept, the point found
       when a listener reads it (script_event_point) */
    if (h->pt_lazy) {
        vm->objs[jv_idx(ev)].a |= EV_LAZYPOINT;
        js_define(vm, jv_idx(ev), si_atom(vm, "\001pt"), jv_num(h->pt_lazy), JS_SHAPE_HIDDEN);
    } else si_point_fields(vm, ev, target, x, y);
    si_def(vm, ev, "movementX", jv_num(0));
    si_def(vm, ev, "movementY", jv_num(0));
    si_def(vm, ev, "button", jv_num(button));
    si_def(vm, ev, "buttons", jv_num(buttons));
    si_def(vm, ev, "relatedTarget", related ? sh_wrap(vm, related) : JV_NULL);
    si_modifiers(vm, ev, 0);
    if (pointer) {
        si_def(vm, ev, "pointerId", jv_num(1));
        si_def(vm, ev, "pointerType", sh_cstr(vm, "mouse"));
        si_def(vm, ev, "isPrimary", JV_TRUE);
        si_def(vm, ev, "width", jv_num(1));
        si_def(vm, ev, "height", jv_num(1));
        si_def(vm, ev, "pressure", jv_num(buttons ? 0.5 : 0));
    }
}
/* a mouse or pointer event dispatched: enter and leave reach their target alone, and
   neither bubbles nor can be cancelled; 1 unless cancelled */
static int si_mouse(js_vm *vm, uint32_t target, const char *type, double x, double y, int button, int buttons, double detail, uint32_t related) {
    int pointer = strncmp(type, "pointer", 7) == 0;
    int boundary = strstr(type, "enter") != 0 || strstr(type, "leave") != 0;
    uint32_t flags = EV_TRUSTED | (boundary ? EV_NOPATH : EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED);
    jv ev = si_event(vm, type, flags, pointer ? HP_POINTEREVENT : HP_MOUSEEVENT, pointer ? 0 : detail);
    si_mouse_fields(vm, ev, target, x, y, button, buttons, related, pointer);
    return sh_dispatch(vm, target, ev);
}

/* ---- where the pointer is: the element the layout puts at a point ---- */

static uint32_t si_depth(const dom_document *d, uint32_t x) {
    uint32_t n = 0;
    for (uint32_t y = d->node[x].parent; y; y = d->node[y].parent) n++;
    return n;
}
static uint32_t si_hit(js_vm *vm, double x, double y) {
    struct js_host *h = vm->host;
    sh_view(vm);
    const dom_document *d = h->d;
    uint32_t best = 0, bd = 0;
    for (size_t i = 0; i < len(h->vboxes); i++) {
        sh_vbox v = h->vboxes[i];
        double x0 = v.x - v.p[3] - v.b[3], y0 = v.y - v.p[0] - v.b[0];
        double x1 = v.x + v.w + v.p[1] + v.b[1], y1 = v.y + v.h + v.p[2] + v.b[2];
        if (x < x0 || x >= x1 || y < y0 || y >= y1 || !d->node[v.node].live) continue;
        uint32_t dp = si_depth(d, v.node);
        if (!best || dp >= bd) { best = v.node; bd = dp; }
    }
    for (size_t i = 0; i < len(h->vfrags); i++) {
        sh_vfrag f = h->vfrags[i];
        if (x < f.x || x >= f.x + f.w || y < f.y || y >= f.y + f.h) continue;
        uint32_t el = f.node;
        if (el && d->node[el].live && d->node[el].kind == DOM_NODE_KIND_TEXT) el = d->node[el].parent;
        if (!el || !sh_is_elem(d, el)) continue;
        uint32_t dp = si_depth(d, el);
        if (!best || dp >= bd) { best = el; bd = dp; }
    }
    if (!best) best = sh_elem_child(d, 1, 0);
    return best;
}
/* the elements from x up to the root, in order (x first) */
static void si_chain(const dom_document *d, uint32_t x, vector<uint32_t> *out) {
    clear(*out);
    for (uint32_t y = x; y && y != 1; y = d->node[y].parent) push_back(*out, y);
}
static int si_in(const vector<uint32_t> *chain, uint32_t x) {
    for (size_t i = 0; i < len(*chain); i++) if ((*chain)[i] == x) return 1;
    return 0;
}
/* the pointer moved to a point: the boundary events from the element under it to the
   one there, then a move */
static void si_move(js_vm *vm, double x, double y) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    uint32_t to = si_hit(vm, x, y), from = h->mouse_in ? h->hover : 0;
    if (from && (!d->node[from].live || !sh_connected(d, from))) from = 0;
    if (to != from) {
        vector<uint32_t> oc, nc;
        if (from) si_chain(d, from, &oc);
        si_chain(d, to, &nc);
        static const char *const kinds[] = { "pointer", "mouse" };
        for (int k = 0; k < 2; k++) {
            char over[32], out[32], enter[32], leave[32];
            snprintf(over, sizeof over, "%sover", kinds[k]);
            snprintf(out, sizeof out, "%sout", kinds[k]);
            snprintf(enter, sizeof enter, "%senter", kinds[k]);
            snprintf(leave, sizeof leave, "%sleave", kinds[k]);
            int bt = k == 0 ? -1 : 0;
            if (from) {
                si_mouse(vm, from, out, x, y, bt, 0, 0, to);
                for (size_t i = 0; i < len(oc); i++) if (!si_in(&nc, oc[i])) si_mouse(vm, oc[i], leave, x, y, bt, 0, 0, to);
            }
            si_mouse(vm, to, over, x, y, bt, 0, 0, from);
            for (size_t i = len(nc); i > 0; i--) if (!si_in(&oc, nc[i - 1])) si_mouse(vm, nc[i - 1], enter, x, y, bt, 0, 0, from);
        }
        h->hover = to;
    }
    h->mouse_in = 1;
    h->mouse_x = x;
    h->mouse_y = y;
    si_mouse(vm, to, "pointermove", x, y, -1, 0, 0, 0);
    si_mouse(vm, to, "mousemove", x, y, 0, 0, 0, 0);
}

/* ---- focus ---- */

/* a tabindex attribute's value, as HTML's rules for integers read it: 0 and *ok unset when none is valid */
static int si_tabindex_attr(const dom_document *d, uint32_t x, int *ok) {
    vector<char> v;
    *ok = 0;
    if (!si_attr(d, x, "tabindex", &v)) return 0;
    size_t i = 0;
    while (i < len(v) && sh_ws(v[i])) i++;
    int neg = 0;
    if (i < len(v) && (v[i] == '-' || v[i] == '+')) { neg = v[i] == '-'; i++; }
    if (i >= len(v) || v[i] < '0' || v[i] > '9') return 0;
    long n = 0;
    while (i < len(v) && v[i] >= '0' && v[i] <= '9' && n < 100000000) n = n * 10 + (v[i++] - '0');
    *ok = 1;
    return (int)(neg ? -n : n);
}
/* whether an element is focusable by itself (HTML 6.6.2), disabled aside */
static int si_native(const dom_document *d, uint32_t x) {
    if ((si_is(d, x, "a") || si_is(d, x, "area")) && sh_attr_z(d, x, "href")) return 1;
    if (si_is(d, x, "button") || si_is(d, x, "select") || si_is(d, x, "textarea") || si_is(d, x, "iframe")) return 1;
    if (si_is(d, x, "input")) return !si_input_is(d, x, "hidden");
    if (si_is(d, x, "summary")) return 1;
    vector<char> ce;
    if (si_attr(d, x, "contenteditable", &ce) && !si_eq(&ce, "false")) return 1;
    return 0;
}
static int si_disabled(const dom_document *d, uint32_t x) {
    static const char *const forms[] = { "button", "input", "select", "textarea", "optgroup", "option", "fieldset" };
    for (size_t i = 0; i < sizeof forms / sizeof forms[0]; i++) if (si_is(d, x, forms[i])) return sh_attr_z(d, x, "disabled") != 0;
    return 0;
}
/* a focusable area: connected, rendered, not disabled, focusable by itself or by tabindex */
static int si_focusable(js_vm *vm, uint32_t x) {
    const dom_document *d = vm->host->d;
    if (!sh_is_elem(d, x) || !sh_connected(d, x) || si_disabled(d, x)) return 0;
    int ok = 0;
    si_tabindex_attr(d, x, &ok);
    if (!ok && !si_native(d, x)) return 0;
    /* the element a user clicks, and what holds it, are rendered */
    for (uint32_t y = vm->host->pt_lazy; y; y = d->node[y].parent) if (y == x) return 1;
    sh_view(vm);
    return script_view_rendered(vm->host, x);
}
/* an element's place in the sequential focus order: its tabindex, or 0 for one focusable
   by itself; -1 for none */
static int si_tab_order(js_vm *vm, uint32_t x) {
    if (!si_focusable(vm, x)) return -1;
    int ok = 0, t = si_tabindex_attr(vm->host->d, x, &ok);
    return ok ? (t < 0 ? -1 : t) : 0;
}

uint32_t script_active(js_vm *vm) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (h->focus && h->focus < d->node_len && d->node[h->focus].live && d->node[h->focus].gen == h->focus_gen && sh_connected(d, h->focus))
        return h->focus;
    h->focus = 0;
    uint32_t html = sh_elem_child(d, 1, 0), body = 0;
    for (uint32_t c = html ? d->node[html].first_child : 0; c && !body; c = d->node[c].next_sibling) if (si_is(d, c, "body")) body = c;
    return body;
}
static void si_focus_event(js_vm *vm, uint32_t target, const char *type, uint32_t related) {
    int bubbles = strcmp(type, "focusin") == 0 || strcmp(type, "focusout") == 0;
    jv ev = si_event(vm, type, EV_TRUSTED | EV_COMPOSED | (bubbles ? EV_BUBBLES : 0), HP_FOCUSEVENT, 0);
    si_def(vm, ev, "relatedTarget", related ? sh_wrap(vm, related) : JV_NULL);
    sh_dispatch(vm, target, ev);
}
/* a field that leaves focus with its value changed since it took it: its change event */
static void si_change_check(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (!si_editable(d, x)) return;
    vector<char> v;
    si_value(vm, x, &v);
    if (len(v) == len(h->fvalue) && memcmp(data(v), data(h->fvalue), len(v)) == 0) return;
    sh_fire(vm, x, "change", EV_BUBBLES);
}
/* focus from the element that had it (0: none) to x (0: the body), its events in order */
static void si_focus_move(js_vm *vm, uint32_t from, uint32_t to) {
    struct js_host *h = vm->host;
    if (from) {
        si_change_check(vm, from);
        si_focus_event(vm, from, "blur", to);
        si_focus_event(vm, from, "focusout", to);
    }
    h->focus = to;
    h->focus_gen = to ? h->d->node[to].gen : 0;
    clear(h->fvalue);
    if (to) {
        if (si_editable(h->d, to)) si_value(vm, to, &h->fvalue);
        si_focus_event(vm, to, "focus", from);
        si_focus_event(vm, to, "focusin", from);
    }
}
int script_focus(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    if (!si_focusable(vm, x)) return 0;
    uint32_t cur = script_active(vm);
    uint32_t from = h->focus && cur == h->focus ? cur : 0;
    if (from == x) return 0;
    si_focus_move(vm, from, x);
    return 1;
}
void script_blur(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    if (!h->focus || script_active(vm) != x) return;
    si_focus_move(vm, x, 0);
}
/* the press's focus: the focusable area the pressed element is in, or none */
static void si_press_focus(js_vm *vm, uint32_t x) {
    const dom_document *d = vm->host->d;
    for (uint32_t y = x; y && y != 1; y = d->node[y].parent)
        if (si_focusable(vm, y)) { script_focus(vm, y); return; }
    if (vm->host->focus) script_blur(vm, script_active(vm));
}
/* Tab's move: the next (back: the previous) element of the sequential focus order */
static void si_tab(js_vm *vm, int back) {
    const dom_document *d = vm->host->d;
    vector<uint32_t> order;
    vector<int> keys;
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
        int t = si_tab_order(vm, c);
        if (t < 0) continue;
        push_back(order, c);
        push_back(keys, t);
    }
    /* positive tabindexes first, ascending, the document's order among equals; then 0s */
    vector<uint32_t> seq;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            int last = 0;
            for (;;) {
                int next = 0;
                for (size_t i = 0; i < len(keys); i++) if (keys[i] > last && (!next || keys[i] < next)) next = keys[i];
                if (!next) break;
                for (size_t i = 0; i < len(keys); i++) if (keys[i] == next) push_back(seq, order[i]);
                last = next;
            }
        } else for (size_t i = 0; i < len(keys); i++) if (keys[i] == 0) push_back(seq, order[i]);
    }
    if (!len(seq)) return;
    uint32_t cur = vm->host->focus ? script_active(vm) : 0;
    size_t at = len(seq);
    for (size_t i = 0; i < len(seq); i++) if (seq[i] == cur) at = i;
    size_t next;
    if (at == len(seq)) next = back ? len(seq) - 1 : 0;
    else if (back) { if (at == 0) return; next = at - 1; }
    else { if (at + 1 >= len(seq)) return; next = at + 1; }
    script_focus(vm, seq[next]);
}

/* ---- forms: the form data set (HTML 4.10.21.4) ---- */

/* an element's form owner: its form attribute's form, or its nearest form ancestor */
static uint32_t si_form_of(const dom_document *d, uint32_t x) {
    vector<char> id;
    if (si_attr(d, x, "form", &id)) {
        for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
            if (!si_is(d, c, "form")) continue;
            vector<char> cid;
            if (si_attr(d, c, "id", &cid) && len(cid) == len(id) && memcmp(data(cid), data(id), len(id)) == 0) return c;
        }
        return 0;
    }
    for (uint32_t y = d->node[x].parent; y; y = d->node[y].parent) if (si_is(d, y, "form")) return y;
    return 0;
}
uint32_t script_form_owner(const dom_document *d, uint32_t x) { return si_form_of(d, x); }
/* a submit button: a button of type submit (its default), an input of type submit or image */
static int si_submit_button(const dom_document *d, uint32_t x) {
    if (si_is(d, x, "button")) {
        vector<char> t;
        return !si_attr(d, x, "type", &t) || si_eq(&t, "submit") || (!si_eq(&t, "button") && !si_eq(&t, "reset"));
    }
    return si_input_is(d, x, "submit") || si_input_is(d, x, "image");
}
/* the entry list's newlines as the form's serializations make them: CR, LF and CRLF all CRLF */
static void si_crlf(const char *s, size_t n, vector<char> *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\r') { append_range(*out, "\r\n", 2); if (i + 1 < n && s[i + 1] == '\n') i++; }
        else if (s[i] == '\n') append_range(*out, "\r\n", 2);
        else push_back(*out, s[i]);
    }
}
/* an option's value: its value attribute, else its text with white space collapsed */
static void si_option_value(const dom_document *d, uint32_t o, vector<char> *out) {
    if (si_attr(d, o, "value", out)) return;
    vector<char> t;
    sh_text_of(d, o, &t);
    clear(*out);
    for (size_t i = 0; i < len(t); i++) {
        if (sh_ws(t[i])) { if (len(*out) && (*out)[len(*out) - 1] != ' ') push_back(*out, ' '); }
        else push_back(*out, t[i]);
    }
    while (len(*out) && (*out)[len(*out) - 1] == ' ') pop_back(*out);
}
/* an entry of the list owner's: its name, value and control */
static void si_fd_add(struct js_host *h, uint32_t owner, const char *n, size_t nn, const char *v, size_t vn, uint32_t control) {
    sh_fd e;
    memset(&e, 0, sizeof e);
    e.owner = owner;
    e.noff = (uint32_t)len(h->ntext);
    append_range(h->ntext, n, nn);
    e.nlen = (uint32_t)nn;
    e.voff = (uint32_t)len(h->ntext);
    append_range(h->ntext, v, vn);
    e.vlen = (uint32_t)vn;
    e.control = control;
    e.live = 1;
    push_back(h->fds, e);
}
/* a form's entry list, as the list of owner: each control the form owns, in tree order,
   that has a name and is not disabled; a button only as the submitter */
static void si_entries(js_vm *vm, uint32_t form, uint32_t submitter, uint32_t owner);
/* a FormData of its own, empty */
static uint32_t si_fd_new(js_vm *vm) {
    uint32_t o = js_obj_new(vm, JK_HOST, vm->host->protos[HP_FORMDATA]);
    vm->objs[o].c = HK_FORMDATA;
    vm->objs[o].a = o;
    return o;
}
/* a form's entry list constructed (HTML 4.10.21.4): a FormData filled from its controls,
   then the form's formdata event fired with it, its listeners free to change it; 0 while
   the same form's list is being constructed */
static uint32_t si_form_list(js_vm *vm, uint32_t form, uint32_t submitter) {
    struct js_host *h = vm->host;
    if (h->building == form) return 0;
    uint32_t o = si_fd_new(vm);
    si_entries(vm, form, submitter, o);
    uint32_t was = h->building;
    h->building = form;
    jv ev = si_event(vm, "formdata", EV_TRUSTED | EV_BUBBLES, HP_FORMDATAEVENT, 0);
    si_def(vm, ev, "formData", jv_obj(o));
    sh_dispatch(vm, form, ev);
    h->building = was;
    return o;
}
static void si_entries(js_vm *vm, uint32_t form, uint32_t submitter, uint32_t owner) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    vector<char> name, v, ty;
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT) continue;
        int input = si_is(d, c, "input"), select = si_is(d, c, "select"), area = si_is(d, c, "textarea"), button = si_is(d, c, "button");
        if (!input && !select && !area && !button) continue;
        if (si_form_of(d, c) != form || si_disabled(d, c)) continue;
        /* a control in a datalist, or in a disabled fieldset (but its first legend), is none */
        int barred = 0;
        for (uint32_t y = d->node[c].parent; y && !barred; y = d->node[y].parent) {
            if (si_is(d, y, "datalist")) barred = 1;
            if (si_is(d, y, "fieldset") && sh_attr_z(d, y, "disabled")) barred = 1;
        }
        if (barred) continue;
        if (button || (input && (si_input_is(d, c, "submit") || si_input_is(d, c, "image") || si_input_is(d, c, "reset") || si_input_is(d, c, "button"))))
            if (c != submitter) continue;
        if (input && si_input_is(d, c, "image")) {
            /* the submitter image's coordinates, as name.x and name.y */
            vector<char> nx;
            si_attr(d, c, "name", &name);
            append_range(nx, data(name), len(name));
            if (len(nx)) push_back(nx, '.');
            push_back(nx, 'x');
            si_fd_add(h, owner, data(nx), len(nx), "0", 1, c);
            nx[len(nx) - 1] = 'y';
            si_fd_add(h, owner, data(nx), len(nx), "0", 1, c);
            continue;
        }
        if (!si_attr(d, c, "name", &name) || !len(name)) continue;
        if (input && (si_input_is(d, c, "checkbox") || si_input_is(d, c, "radio"))) {
            if (!sh_attr_z(d, c, "checked")) continue;
            if (!si_attr(d, c, "value", &v)) { clear(v); append_range(v, "on", 2); }
            si_fd_add(h, owner, data(name), len(name), data(v), len(v), c);
            continue;
        }
        if (input && si_input_is(d, c, "file")) { si_fd_add(h, owner, data(name), len(name), "", 0, c); continue; }
        if (select) {
            /* each selected option; a single select with none selected, its first enabled one */
            uint32_t first = 0;
            int any = 0;
            for (uint32_t o = sh_next_in(d, c, c); o; o = sh_next_in(d, o, c)) {
                if (!si_is(d, o, "option")) continue;
                if (!first && !sh_attr_z(d, o, "disabled")) first = o;
                if (!sh_attr_z(d, o, "selected") || sh_attr_z(d, o, "disabled")) continue;
                si_option_value(d, o, &v);
                si_fd_add(h, owner, data(name), len(name), data(v), len(v), c);
                any = 1;
                if (!sh_attr_z(d, c, "multiple")) break;
            }
            if (!any && first && !sh_attr_z(d, c, "multiple") && !sh_attr_z(d, c, "size")) {
                si_option_value(d, first, &v);
                si_fd_add(h, owner, data(name), len(name), data(v), len(v), c);
            }
            continue;
        }
        si_value(vm, c, &v);
        si_fd_add(h, owner, data(name), len(name), data(v), len(v), c);
    }
}

/* the entries of a list as application/x-www-form-urlencoded (URL 5.2): a byte kept, a
   space +, the rest %XX; names and values' newlines CRLF */
static void si_urlbyte(vector<char> *out, unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_') push_back(*out, (char)c);
    else if (c == ' ') push_back(*out, '+');
    else {
        static const char hex[] = "0123456789ABCDEF";
        push_back(*out, '%');
        push_back(*out, hex[c >> 4]);
        push_back(*out, hex[c & 15]);
    }
}
static void si_urlencoded(struct js_host *h, uint32_t owner, int enc, vector<char> *out) {
    clear(*out);
    int first = 1;
    for (size_t i = 0; i < len(h->fds); i++) {
        sh_fd e = h->fds[i];
        if (!e.live || e.owner != owner) continue;
        vector<char> n, v, t;
        si_crlf(data(h->ntext) + e.noff, e.nlen, &t);
        si_encode(enc, data(t), len(t), &n);
        clear(t);
        si_crlf(data(h->ntext) + e.voff, e.vlen, &t);
        si_encode(enc, data(t), len(t), &v);
        if (!first) push_back(*out, '&');
        first = 0;
        for (size_t k = 0; k < len(n); k++) si_urlbyte(out, (unsigned char)n[k]);
        push_back(*out, '=');
        for (size_t k = 0; k < len(v); k++) si_urlbyte(out, (unsigned char)v[k]);
    }
}
/* as multipart/form-data (RFC 7578), the boundary given; as text/plain */
static void si_multipart(struct js_host *h, uint32_t owner, int enc, const char *boundary, vector<char> *out) {
    clear(*out);
    for (size_t i = 0; i < len(h->fds); i++) {
        sh_fd e = h->fds[i];
        if (!e.live || e.owner != owner) continue;
        append_range(*out, "--", 2);
        append_range(*out, boundary, strlen(boundary));
        static const char disp[] = "\r\nContent-Disposition: form-data; name=\"";
        append_range(*out, disp, sizeof disp - 1);
        vector<char> nm, v, t;
        si_encode(enc, data(h->ntext) + e.noff, e.nlen, &nm);
        for (size_t k = 0; k < len(nm); k++) {
            char c = nm[k];
            if (c == '"') append_range(*out, "%22", 3);
            else if (c == '\n') append_range(*out, "%0A", 3);
            else if (c == '\r') append_range(*out, "%0D", 3);
            else push_back(*out, c);
        }
        append_range(*out, "\"\r\n\r\n", 5);
        si_crlf(data(h->ntext) + e.voff, e.vlen, &t);
        si_encode(enc, data(t), len(t), &v);
        append_range(*out, data(v), len(v));
        append_range(*out, "\r\n", 2);
    }
    append_range(*out, "--", 2);
    append_range(*out, boundary, strlen(boundary));
    append_range(*out, "--\r\n", 4);
}
static void si_text_plain(struct js_host *h, uint32_t owner, int enc, vector<char> *out) {
    clear(*out);
    for (size_t i = 0; i < len(h->fds); i++) {
        sh_fd e = h->fds[i];
        if (!e.live || e.owner != owner) continue;
        vector<char> t, c;
        si_crlf(data(h->ntext) + e.noff, e.nlen, &c);
        si_encode(enc, data(c), len(c), &t);
        append_range(*out, data(t), len(t));
        push_back(*out, '=');
        clear(c);
        si_crlf(data(h->ntext) + e.voff, e.vlen, &c);
        si_encode(enc, data(c), len(c), &t);
        append_range(*out, data(t), len(t));
        append_range(*out, "\r\n", 2);
    }
}

/* ---- navigation ---- */

/* the window's beforeunload, as a navigation is made: cancelable, its cancelling not
   kept (a user's leave is assumed) */
static void si_unload(js_vm *vm) {
    struct js_host *h = vm->host;
    h->unloading = 1;
    jv ev = si_event(vm, "beforeunload", EV_TRUSTED | EV_CANCELABLE, HP_BEFOREUNLOADEVENT, 0);
    si_def(vm, ev, "returnValue", jv_str(A_EMPTY));
    sh_dispatch(vm, JS_WINDOW, ev);
    h->unloading = 0;
}
/* the navigation n made: beforeunload, the loader's request (the body and type kept for
   it), the page stopped */
static void si_go(js_vm *vm, uint32_t n) {
    struct js_host *h = vm->host;
    si_unload(vm);
    char method[16], hdrs[300];
    amb_span ms = h->rt->navigation[n].method;
    snprintf(method, sizeof method, "%.*s", (int)ms.len, text_ptr(h->rt, ms));
    hdrs[0] = 0;
    if (len(h->nav_ctype)) snprintf(hdrs, sizeof hdrs, "Content-Type: %.*s\r\n", (int)len(h->nav_ctype), data(h->nav_ctype));
    net_resource_h r = net_fetch_request(h->nt, data(h->nav_url), len(h->nav_url), NET_KIND_DOCUMENT, method, data(h->nav_body),
                                         len(h->nav_body), hdrs, strlen(hdrs), h->page);
    h->rt->navigation[n].resource = r;
    h->rt->navigation[n].status = h->nt->resource[r.index].status;
    h->navigated = 1;
}
/* a navigation's record (its URL's first shown bytes: a form's GET's without the query of
   its fields, whose values are not kept); a form's planned (its task makes it), another's
   made at once (none for another browsing context's, a javascript: URL's, or one asked
   for while beforeunload is fired); a form's fields recorded with it, by name */
static void si_navigate(js_vm *vm, script_nav_cause cause, const char *method, const char *url, size_t shown, const char *body,
                        size_t bn, const char *ctype, uint32_t form, uint32_t submitter, const char *target, uint32_t owner) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    amb_span m = text_add(h->rt, method, strlen(method));
    amb_span u = text_add(h->rt, url, shown);
    amb_span t = text_add(h->rt, target, strlen(target));
    script_navigation_h nh = navigation_new(h->rt);
    uint32_t n = nh.index;
    dom_node_h none = { 0, 0, 0 };
    h->rt->navigation[n].run = vm->cur_run;
    h->rt->navigation[n].cause = cause;
    h->rt->navigation[n].method = m;
    h->rt->navigation[n].url = u;
    h->rt->navigation[n].form = form ? node_handle(d, form) : none;
    h->rt->navigation[n].submitter = submitter ? node_handle(d, submitter) : none;
    h->rt->navigation[n].target = t;
    net_resource_h rnone = { 0, 0, 0 };
    h->rt->navigation[n].resource = rnone;
    uint32_t fields = 0;
    if (owner)
        for (size_t i = 0; i < len(h->fds); i++) {
            sh_fd e = h->fds[i];
            if (!e.live || e.owner != owner) continue;
            fields++;
            vector<char> fname;
            append_range(fname, data(h->ntext) + e.noff, e.nlen);
            amb_span fn = text_add(h->rt, data(fname), len(fname));
            script_form_field_h fh = form_field_new(h->rt);
            h->rt->form_field[fh.index].navigation = n;
            h->rt->form_field[fh.index].name = fn;
            h->rt->form_field[fh.index].control = e.control ? node_handle(d, e.control) : none;
        }
    h->rt->navigation[n].fields = fields;
    /* another browsing context's navigation leaves this page as it is */
    int here = !target[0] || strcmp(target, "_self") == 0 || strcmp(target, "_top") == 0 || strcmp(target, "_parent") == 0;
    if (!here || strncmp(url, "javascript:", 11) == 0 || h->unloading) return;
    clear(h->nav_url);
    append_range(h->nav_url, url, strlen(url));
    clear(h->nav_body);
    append_range(h->nav_body, body, bn);
    clear(h->nav_ctype);
    append_range(h->nav_ctype, ctype, strlen(ctype));
    h->nav_planned = n;
    if (cause == SCRIPT_NAV_CAUSE_FORM) sh_task(vm, 0, SH_TASK_NAV, n);
    else si_go(vm, n);
}
/* the page's URL without its fragment, and a URL's: equal when only the fragment differs */
static size_t si_unfrag(const char *u) {
    size_t i = 0;
    while (u[i] && u[i] != '#') i++;
    return i;
}
static void si_hash_task(js_vm *vm, const char *old, const char *now);
void script_navigate(js_vm *vm, uint32_t cause, const char *url, size_t n) {
    char base[4200], ref[8192], abs[8400];
    sh_page_url(vm, base, sizeof base);
    snprintf(ref, sizeof ref, "%.*s", (int)n, url);
    if (!net_url_resolve(base, ref, abs, sizeof abs)) return;
    /* the fragment kept: net_url_resolve drops it, so it is put back from the reference */
    size_t hn = si_unfrag(ref);
    if (ref[hn] == '#' && strlen(abs) + strlen(ref + hn) < sizeof abs) strcat(abs, ref + hn);
    size_t a = si_unfrag(abs), b = si_unfrag(base);
    if (abs[a] == '#' && a == b && memcmp(abs, base, a) == 0 && cause != SCRIPT_NAV_CAUSE_RELOAD) {
        si_hash_task(vm, base, abs);
        return;
    }
    si_navigate(vm, (script_nav_cause)cause, "GET", abs, strlen(abs), "", 0, "", 0, 0, "", 0);
}

/* ---- a form submitted (HTML 4.10.21.3) ---- */

/* an attribute of the submitter (formaction ...) or else the form's (action ...) */
static int si_form_attr(const dom_document *d, uint32_t form, uint32_t submitter, const char *sub, const char *mine, vector<char> *out) {
    if (submitter && si_attr(d, submitter, sub, out)) return 1;
    return si_attr(d, form, mine, out);
}
/* the constraint validation of a form's controls: a required one empty, or a box not
   checked, is invalid; its invalid event fired. 1 when every one is valid */
static int si_valid(js_vm *vm, uint32_t form, int report) {
    const dom_document *d = vm->host->d;
    int ok = 1;
    vector<char> v;
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
        if (!sh_is_elem(d, c) || si_form_of(d, c) != form || !sh_attr_z(d, c, "required") || si_disabled(d, c)) continue;
        int bad = 0;
        if (si_input_is(d, c, "checkbox") || si_input_is(d, c, "radio")) bad = !sh_attr_z(d, c, "checked");
        else if (si_is(d, c, "input") || si_is(d, c, "textarea")) { si_value(vm, c, &v); bad = len(v) == 0; }
        else if (si_is(d, c, "select")) {
            bad = 1;
            for (uint32_t o = sh_next_in(d, c, c); o; o = sh_next_in(d, o, c)) {
                if (!si_is(d, o, "option") || !sh_attr_z(d, o, "selected")) continue;
                si_option_value(d, o, &v);
                if (len(v)) bad = 0;
            }
        }
        if (!bad) continue;
        ok = 0;
        sh_fire(vm, c, "invalid", EV_CANCELABLE);
        if (report) { (void)report; }
    }
    return ok;
}
/* the submission of form by submitter (0: none); submit() skips the validation and the
   submit event (via_submit) */
static void si_submit(js_vm *vm, uint32_t form, uint32_t submitter, int via_submit) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (h->navigated || !sh_connected(d, form)) return;
    if (!via_submit) {
        if (!(submitter && sh_attr_z(d, submitter, "formnovalidate")) && !sh_attr_z(d, form, "novalidate") && !si_valid(vm, form, 1)) return;
        jv ev = si_event(vm, "submit", EV_TRUSTED | EV_BUBBLES | EV_CANCELABLE, HP_SUBMITEVENT, 0);
        si_def(vm, ev, "submitter", submitter ? sh_wrap(vm, submitter) : JV_NULL);
        if (!sh_dispatch(vm, form, ev)) return;
        if (h->navigated || !sh_connected(d, form)) return;
    }
    uint32_t owner = si_form_list(vm, form, submitter);
    if (!owner || h->navigated || !sh_connected(d, form)) return;
    int enc = si_form_enc(vm, form);
    vector<char> action, method, enctype, target, body;
    char base[4200], ref[8192], abs[8400], ctype[200];
    sh_page_url(vm, base, sizeof base);
    if (!si_form_attr(d, form, submitter, "formaction", "action", &action) || !len(action)) { clear(action); append_range(action, base, strlen(base)); }
    snprintf(ref, sizeof ref, "%.*s", (int)len(action), data(action));
    if (!net_url_resolve(base, ref, abs, sizeof abs)) return;
    int post = si_form_attr(d, form, submitter, "formmethod", "method", &method) && si_eq(&method, "post");
    si_form_attr(d, form, submitter, "formtarget", "target", &target);
    push_back(target, 0);
    ctype[0] = 0;
    if (post) {
        if (si_form_attr(d, form, submitter, "formenctype", "enctype", &enctype) && si_eq(&enctype, "multipart/form-data")) {
            static const char boundary[] = "----AmbitiousFormBoundary0123456789";
            si_multipart(h, owner, enc, boundary, &body);
            snprintf(ctype, sizeof ctype, "multipart/form-data; boundary=%s", boundary);
        } else if (len(enctype) && si_eq(&enctype, "text/plain")) {
            si_text_plain(h, owner, enc, &body);
            snprintf(ctype, sizeof ctype, "text/plain");
        } else {
            si_urlencoded(h, owner, enc, &body);
            snprintf(ctype, sizeof ctype, "application/x-www-form-urlencoded");
        }
        si_navigate(vm, SCRIPT_NAV_CAUSE_FORM, "POST", abs, strlen(abs), data(body), len(body), ctype, form, submitter, data(target), owner);
        return;
    }
    /* GET: the entry list as the URL's query, in place of its own */
    vector<char> q, url;
    si_urlencoded(h, owner, enc, &q);
    size_t cut = 0;
    while (abs[cut] && abs[cut] != '?' && abs[cut] != '#') cut++;
    append_range(url, abs, cut);
    push_back(url, '?');
    append_range(url, data(q), len(q));
    push_back(url, 0);
    si_navigate(vm, SCRIPT_NAV_CAUSE_FORM, "GET", data(url), cut, "", 0, "", form, submitter, data(target), owner);
}
/* implicit submission (HTML 4.10.21.2): the form's default button clicked, or the form
   submitted when it has one field that blocks implicit submission */
static void si_implicit(js_vm *vm, uint32_t field) {
    const dom_document *d = vm->host->d;
    uint32_t form = si_form_of(d, field);
    if (!form) return;
    uint32_t fields = 0;
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
        if (!sh_is_elem(d, c) || si_form_of(d, c) != form) continue;
        if (si_submit_button(d, c)) {
            if (!si_disabled(d, c)) sh_click_at(vm, c, 0);
            return;
        }
        if (si_editable(d, c) && si_is(d, c, "input")) fields++;
    }
    if (fields == 1) si_submit(vm, form, 0, 0);
}
/* a form's reset: its reset event, then each field's value back to its default */
static void si_reset(js_vm *vm, uint32_t form) {
    const dom_document *d = vm->host->d;
    if (!sh_fire(vm, form, "reset", EV_BUBBLES | EV_CANCELABLE)) return;
    for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
        if (!sh_is_elem(d, c) || si_form_of(d, c) != form || (!si_is(d, c, "input") && !si_is(d, c, "textarea"))) continue;
        jv w = sh_wrap(vm, c);
        js_delete(vm, jv_idx(w), si_atom(vm, "\001value"));
    }
}

void script_click_default(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (h->navigated) return;
    /* the nearest link at or above the click, unless a button or field took it first */
    for (uint32_t y = x; y && y != 1; y = d->node[y].parent) {
        if (si_is(d, y, "button") || si_is(d, y, "input")) {
            if (si_disabled(d, y)) return;
            uint32_t form = si_form_of(d, y);
            if (form && si_submit_button(d, y)) { si_submit(vm, form, y, 0); return; }
            vector<char> t;
            if (form && ((si_is(d, y, "button") && si_attr(d, y, "type", &t) && si_eq(&t, "reset")) || si_input_is(d, y, "reset"))) si_reset(vm, form);
            return;
        }
        if ((si_is(d, y, "a") || si_is(d, y, "area")) && sh_attr_z(d, y, "href")) {
            vector<char> href, target;
            si_attr(d, y, "href", &href);
            si_attr(d, y, "target", &target);
            push_back(target, 0);
            char base[4200], ref[8192], abs[8400];
            sh_page_url(vm, base, sizeof base);
            snprintf(ref, sizeof ref, "%.*s", (int)len(href), data(href));
            if (!target[0] || strcmp(data(target), "_self") == 0) { script_navigate(vm, SCRIPT_NAV_CAUSE_LINK, data(href), len(href)); return; }
            if (net_url_resolve(base, ref, abs, sizeof abs))
                si_navigate(vm, SCRIPT_NAV_CAUSE_LINK, "GET", abs, strlen(abs), "", 0, "", 0, 0, data(target), 0);
            return;
        }
    }
}

/* ---- hashchange ---- */

/* a navigation to a fragment of the page: location's hash at once, popstate at once (its
   state the history entry's), hashchange in a task */
static void si_hash_task(js_vm *vm, const char *old, const char *now) {
    sh_location_set(vm, now);
    uint32_t a = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, a, si_atom(vm, "old"), sh_cstr(vm, old), 0);
    js_define(vm, a, si_atom(vm, "now"), sh_cstr(vm, now), 0);
    jv ev = si_event(vm, "popstate", EV_TRUSTED, HP_POPSTATEEVENT, 0);
    si_def(vm, ev, "state", JV_NULL);
    si_def(vm, ev, "hasUAVisualTransition", JV_FALSE);
    sh_dispatch(vm, JS_WINDOW, ev);
    /* the URLs' object the task's own argument, which keeps it until the task runs */
    sh_task_obj(vm, 0, SH_TASK_HASH, a);
}
void script_input_task(js_vm *vm, uint32_t kind, uint32_t arg) {
    struct js_host *h = vm->host;
    if (kind == SH_TASK_NAV) {
        /* a form's planned navigation, unless another took its place */
        if (h->navigated || h->nav_planned != arg) return;
        dom_node_h fh = h->rt->navigation[arg].form;
        const dom_document *d = h->d;
        uint32_t form = fh.index && fh.index < d->node_len && d->node[fh.index].live && d->node[fh.index].gen == fh.gen ? fh.index : 0;
        vector<char> m;
        if (form && si_attr(d, form, "method", &m) && si_eq(&m, "post")) si_form_list(vm, form, 0);
        if (h->navigated || h->nav_planned != arg) return;
        si_go(vm, arg);
        return;
    }
    if (kind != SH_TASK_HASH) return;
    jv old = js_get(vm, jv_obj(arg), si_atom(vm, "old")), now = js_get(vm, jv_obj(arg), si_atom(vm, "now"));
    jv ev = si_event(vm, "hashchange", EV_TRUSTED, HP_HASHCHANGEEVENT, 0);
    si_def(vm, ev, "oldURL", old);
    si_def(vm, ev, "newURL", now);
    sh_dispatch(vm, JS_WINDOW, ev);
}

/* ---- keys (UI Events' KeyboardEvent, a US keyboard) ---- */

/* a key by its name or character: its key, code and keyCode, the character it makes (0
   for none), and the modifier it is (0 for none) */
typedef struct si_key { char key[24], code[24]; uint32_t keycode, ch, mod; } si_key;
static int si_key_of(const char *name, size_t n, si_key *k) {
    memset(k, 0, sizeof *k);
    static const struct { const char *name, *code; uint32_t keycode, ch, mod; } named[] = {
        { "Enter", "Enter", 13, 13, 0 }, { "Tab", "Tab", 9, 0, 0 }, { "Backspace", "Backspace", 8, 0, 0 }, { "Escape", "Escape", 27, 0, 0 },
        { "Delete", "Delete", 46, 0, 0 }, { "ArrowLeft", "ArrowLeft", 37, 0, 0 }, { "ArrowUp", "ArrowUp", 38, 0, 0 },
        { "ArrowRight", "ArrowRight", 39, 0, 0 }, { "ArrowDown", "ArrowDown", 40, 0, 0 }, { "Home", "Home", 36, 0, 0 }, { "End", "End", 35, 0, 0 },
        { "PageUp", "PageUp", 33, 0, 0 }, { "PageDown", "PageDown", 34, 0, 0 }, { "Insert", "Insert", 45, 0, 0 },
        { "Shift", "ShiftLeft", 16, 0, SI_SHIFT }, { "Control", "ControlLeft", 17, 0, SI_CTRL }, { "Alt", "AltLeft", 18, 0, SI_ALT },
        { "Meta", "MetaLeft", 91, 0, SI_META }, { "Space", "Space", 32, 32, 0 } };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
        if (n == strlen(named[i].name) && memcmp(name, named[i].name, n) == 0) {
            snprintf(k->key, sizeof k->key, "%s", strcmp(named[i].name, "Space") == 0 ? " " : named[i].name);
            snprintf(k->code, sizeof k->code, "%s", named[i].code);
            k->keycode = named[i].keycode;
            k->ch = named[i].ch;
            k->mod = named[i].mod;
            return 1;
        }
    if (n >= 2 && name[0] == 'F' && name[1] >= '1' && name[1] <= '9') {
        int f = atoi(name + 1);
        if (f >= 1 && f <= 12) {
            snprintf(k->key, sizeof k->key, "%.*s", (int)n, name);
            snprintf(k->code, sizeof k->code, "%.*s", (int)n, name);
            k->keycode = 111 + (uint32_t)f;
            return 1;
        }
    }
    if (n != 1) return 0;
    char c = name[0];
    k->key[0] = c;
    k->ch = (unsigned char)c;
    if (c >= 'a' && c <= 'z') { snprintf(k->code, sizeof k->code, "Key%c", c - 32); k->keycode = (uint32_t)(c - 32); return 1; }
    if (c >= 'A' && c <= 'Z') { snprintf(k->code, sizeof k->code, "Key%c", c); k->keycode = (uint32_t)c; return 1; }
    if (c >= '0' && c <= '9') { snprintf(k->code, sizeof k->code, "Digit%c", c); k->keycode = (uint32_t)c; return 1; }
    if (c == ' ') { snprintf(k->code, sizeof k->code, "Space"); k->keycode = 32; return 1; }
    /* the punctuation of a US keyboard: each character's key, the shifted ones' too */
    static const char plain[] = "-=[]\\;',./`", shifted[] = "_+{}|:\"<>?~";
    static const char *const codes[] = { "Minus", "Equal", "BracketLeft", "BracketRight", "Backslash", "Semicolon", "Quote", "Comma",
                                          "Period", "Slash", "Backquote" };
    static const uint32_t kcs[] = { 189, 187, 219, 221, 220, 186, 222, 188, 190, 191, 192 };
    for (int i = 0; i < 11; i++)
        if (c == plain[i] || c == shifted[i]) { snprintf(k->code, sizeof k->code, "%s", codes[i]); k->keycode = kcs[i]; return 1; }
    static const char digits_shifted[] = ")!@#$%^&*(";
    for (int i = 0; i < 10; i++)
        if (c == digits_shifted[i]) { snprintf(k->code, sizeof k->code, "Digit%d", i); k->keycode = (uint32_t)('0' + i); return 1; }
    snprintf(k->code, sizeof k->code, "Unidentified");
    return 1;
}
/* a keyboard event of a key at the focused element: 1 unless cancelled */
static int si_key_event(js_vm *vm, const char *type, const si_key *k, uint32_t mods) {
    uint32_t target = script_active(vm);
    if (!target) return 1;
    int press = strcmp(type, "keypress") == 0;
    jv ev = si_event(vm, type, EV_TRUSTED | EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED, HP_KEYBOARDEVENT, 0);
    si_def(vm, ev, "key", sh_cstr(vm, k->key));
    si_def(vm, ev, "code", sh_cstr(vm, k->code));
    uint32_t kc = press ? k->ch : k->keycode;
    si_def(vm, ev, "keyCode", jv_num(kc));
    si_def(vm, ev, "which", jv_num(kc));
    si_def(vm, ev, "charCode", jv_num(press ? k->ch : 0));
    si_def(vm, ev, "location", jv_num(k->mod ? 1 : 0));
    si_def(vm, ev, "repeat", JV_FALSE);
    si_def(vm, ev, "isComposing", JV_FALSE);
    si_modifiers(vm, ev, mods);
    return sh_dispatch(vm, target, ev);
}
/* an input event (beforeinput: cancelable, before the change) at a field */
static int si_input_event(js_vm *vm, uint32_t x, const char *type, const char *data_p, size_t dn, int has_data, const char *kind) {
    int before = strcmp(type, "beforeinput") == 0;
    jv ev = si_event(vm, type, EV_TRUSTED | EV_BUBBLES | EV_COMPOSED | (before ? EV_CANCELABLE : 0), HP_INPUTEVENT, 0);
    si_def(vm, ev, "data", has_data ? sh_str(vm, data_p, dn) : JV_NULL);
    si_def(vm, ev, "inputType", sh_cstr(vm, kind));
    si_def(vm, ev, "isComposing", JV_FALSE);
    return sh_dispatch(vm, x, ev);
}
/* text put in at a field's end, as typing does: beforeinput, the value, input */
static void si_insert(js_vm *vm, uint32_t x, const char *t, size_t n, const char *kind) {
    if (!si_input_event(vm, x, "beforeinput", t, n, strcmp(kind, "insertLineBreak") != 0, kind)) return;
    vector<char> v;
    si_value(vm, x, &v);
    append_range(v, t, n);
    si_value_set(vm, x, data(v), len(v));
    si_input_event(vm, x, "input", t, n, strcmp(kind, "insertLineBreak") != 0, kind);
}
/* a key pressed and released, its default action between */
static void si_press(js_vm *vm, const si_key *k, uint32_t mods) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (h->navigated) return;
    int go = si_key_event(vm, "keydown", k, mods);
    uint32_t at = script_active(vm);
    /* Enter at a link follows it on the key's going down, its keypress not sent (as
       Chromium's anchor does) */
    int link = at && (si_is(d, at, "a") || si_is(d, at, "area")) && sh_attr_z(d, at, "href");
    if (go && strcmp(k->key, "Tab") == 0) si_tab(vm, (mods & SI_SHIFT) != 0);
    else if (go && k->ch == 13 && link) sh_click_at(vm, at, EV_TRUSTED);
    else if (go && strcmp(k->key, "Backspace") == 0 && at && si_editable(d, at)) {
        vector<char> v;
        si_value(vm, at, &v);
        if (len(v) && si_input_event(vm, at, "beforeinput", "", 0, 0, "deleteContentBackward")) {
            /* the last character, whole: its UTF-8 bytes */
            size_t e = len(v) - 1;
            while (e > 0 && ((unsigned char)v[e] & 0xC0) == 0x80) e--;
            si_value_set(vm, at, data(v), e);
            si_input_event(vm, at, "input", "", 0, 0, "deleteContentBackward");
        }
    } else if (go && k->ch && !(mods & (SI_CTRL | SI_ALT | SI_META))) {
        int press = si_key_event(vm, "keypress", k, mods);
        at = script_active(vm);
        if (press && at) {
            if (k->ch == 13) {
                if (si_is(d, at, "textarea") && si_editable(d, at)) si_insert(vm, at, "\n", 1, "insertLineBreak");
                else if (si_editable(d, at)) si_implicit(vm, at);
                else if (si_is(d, at, "button") || si_input_is(d, at, "submit") || si_input_is(d, at, "reset") || si_input_is(d, at, "button") ||
                         si_input_is(d, at, "image"))
                    sh_click_at(vm, at, EV_TRUSTED);
            } else if (si_editable(d, at)) {
                char t[8];
                size_t tn = 0;
                uint32_t c = k->ch;
                if (c < 0x80) t[tn++] = (char)c;
                else { t[tn++] = (char)(0xC0 | (c >> 6)); t[tn++] = (char)(0x80 | (c & 0x3F)); }
                si_insert(vm, at, t, tn, "insertText");
            }
        }
    }
    int space = k->ch == 32 && at && !si_editable(d, at) &&
                (si_is(d, at, "button") || si_input_is(d, at, "checkbox") || si_input_is(d, at, "radio") || si_input_is(d, at, "submit") ||
                 si_input_is(d, at, "reset") || si_input_is(d, at, "button"));
    if (h->navigated) return;
    int up = si_key_event(vm, "keyup", k, mods);
    if (up && space) sh_click_at(vm, at, EV_TRUSTED);
}
/* a key by its name, its modifiers held around it (Shift+A, Control+a): 0 when unknown */
static int si_key(js_vm *vm, const char *name, size_t n) {
    uint32_t mods = 0;
    si_key held[4];
    uint32_t nh = 0;
    size_t i = 0;
    for (;;) {
        size_t e = i;
        while (e < n && name[e] != '+') e++;
        if (e >= n || e == i) break;
        si_key m;
        if (!si_key_of(name + i, e - i, &m) || !m.mod || nh >= 4) break;
        mods |= m.mod;
        si_key_event(vm, "keydown", &m, mods);
        held[nh++] = m;
        i = e + 1;
    }
    si_key k;
    if (!si_key_of(name + i, n - i, &k)) return 0;
    if ((mods & SI_SHIFT) && k.key[0] >= 'a' && k.key[0] <= 'z' && !k.key[1]) { k.key[0] = (char)(k.key[0] - 32); k.ch = (unsigned char)k.key[0]; }
    si_press(vm, &k, mods);
    while (nh > 0) {
        nh--;
        mods &= ~held[nh].mod;
        si_key_event(vm, "keyup", &held[nh], mods);
    }
    return 1;
}

/* ---- a user's clicks ---- */

/* a click (and a double click's second) where the element's box is: the pointer moved
   there, pressed and released, the click dispatched at the element under it */
/* whether an event of a type has a listener anywhere: added, or an on* handler */
static int si_heard(js_vm *vm, const char *type) {
    uint32_t t = si_atom(vm, type);
    for (size_t i = 0; i < len(vm->listeners); i++) {
        js_listener l = vm->listeners[i];
        if ((l.live || l.attr) && l.type == t) return 1;
    }
    char on[40];
    snprintf(on, sizeof on, "on%s", type);
    jv w = js_get(vm, jv_obj(vm->global), si_atom(vm, on));
    jv dv = js_get(vm, sh_wrap(vm, 1), si_atom(vm, on));
    vm->throwing = 0;
    return js_is_callable(vm, w) || js_is_callable(vm, dv);
}
/* a click at x that needs no layout: the view not up to date (when it is, its point costs
   nothing), x with no element inside it the point could be on, nothing hidden by its
   attribute, and no listener of the pointer's moves between elements. Its events then go
   to x, and their point is found only when a listener reads it */
static int si_click_lazy(js_vm *vm, uint32_t x) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    if (script_view_fresh(h) || si_input_is(d, x, "hidden")) return 0;
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) if (d->node[c].kind == DOM_NODE_KIND_ELEMENT) return 0;
    for (uint32_t y = x; y && y != 1; y = d->node[y].parent) if (sh_attr_z(d, y, "hidden")) return 0;
    static const char *const moves[] = { "pointerover", "pointerenter", "pointerout", "pointerleave", "pointermove",
                                         "mouseover", "mouseenter", "mouseout", "mouseleave", "mousemove" };
    for (size_t i = 0; i < sizeof moves / sizeof moves[0]; i++) if (si_heard(vm, moves[i])) return 0;
    return 1;
}
static int si_click(js_vm *vm, uint32_t x, int count) {
    struct js_host *h = vm->host;
    if (si_click_lazy(vm, x)) {
        /* the pointer at x, its moves heard by no one */
        h->pt_lazy = x;
        h->mouse_in = 1;
        h->hover = x;
        for (int k = 1; k <= count && !h->navigated; k++) {
            int pd = si_mouse(vm, x, "pointerdown", 0, 0, 0, 1, 0, 0);
            int md = pd && si_mouse(vm, x, "mousedown", 0, 0, 0, 1, k, 0);
            if (md) si_press_focus(vm, x);
            si_mouse(vm, x, "pointerup", 0, 0, 0, 0, 0, 0);
            si_mouse(vm, x, "mouseup", 0, 0, 0, 0, k, 0);
            jv ev = si_event(vm, "click", EV_TRUSTED | EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED, HP_POINTEREVENT, k);
            si_mouse_fields(vm, ev, x, 0, 0, 0, 0, 0, 1);
            h->click_ev = ev;
            sh_click_at(vm, x, EV_TRUSTED);
            h->click_ev = JV_UNDEF;
        }
        if (count == 2 && !h->navigated) si_mouse(vm, x, "dblclick", 0, 0, 0, 0, 2, 0);
        h->pt_lazy = 0;
        return 1;
    }
    sh_view(vm);
    double r[4 * 64], b[4];
    uint32_t n = script_view_rects(h, x, r, 64);
    if (!n) return 0;
    if (n > 64) n = 64;
    b[0] = r[0]; b[1] = r[1]; b[2] = r[2]; b[3] = r[3];
    for (uint32_t i = 1; i < n; i++) {
        double x1 = b[0] + b[2], y1 = b[1] + b[3];
        if (r[4 * i] < b[0]) b[0] = r[4 * i];
        if (r[4 * i + 1] < b[1]) b[1] = r[4 * i + 1];
        if (r[4 * i] + r[4 * i + 2] > x1) x1 = r[4 * i] + r[4 * i + 2];
        if (r[4 * i + 1] + r[4 * i + 3] > y1) y1 = r[4 * i + 1] + r[4 * i + 3];
        b[2] = x1 - b[0];
        b[3] = y1 - b[1];
    }
    double cx = b[0] + b[2] / 2, cy = b[1] + b[3] / 2;
    if (!h->mouse_in) si_move(vm, 0, 0);
    si_move(vm, cx, cy);
    for (int k = 1; k <= count && !h->navigated; k++) {
        uint32_t t = si_hit(vm, cx, cy);
        int pd = si_mouse(vm, t, "pointerdown", cx, cy, 0, 1, 0, 0);
        int md = pd && si_mouse(vm, t, "mousedown", cx, cy, 0, 1, k, 0);
        if (md) si_press_focus(vm, t);
        si_mouse(vm, t, "pointerup", cx, cy, 0, 0, 0, 0);
        si_mouse(vm, t, "mouseup", cx, cy, 0, 0, k, 0);
        /* the click, a mouse event of its own, through the click's behaviour and default */
        jv ev = si_event(vm, "click", EV_TRUSTED | EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED, HP_POINTEREVENT, k);
        si_mouse_fields(vm, ev, t, cx, cy, 0, 0, 0, 1);
        h->click_ev = ev;
        sh_click_at(vm, t, EV_TRUSTED);
        h->click_ev = JV_UNDEF;
    }
    if (count == 2 && !h->navigated) si_mouse(vm, si_hit(vm, cx, cy), "dblclick", cx, cy, 0, 0, 2, 0);
    return 1;
}

int script_action(js_vm *vm, uint32_t kind, uint32_t x, const char *text, size_t n, double horizon) {
    struct js_host *h = vm->host;
    if (h->navigated) return 0;
    script_ready(vm);
    h->phase = SCRIPT_PHASE_EVENT;
    int ok = 1;
    if (kind == SCRIPT_ACT_CLICK || kind == SCRIPT_ACT_DBLCLICK) ok = sh_is_elem(h->d, x) && si_click(vm, x, kind == SCRIPT_ACT_DBLCLICK ? 2 : 1);
    else if (kind == SCRIPT_ACT_FOCUS) { ok = sh_is_elem(h->d, x); if (ok) script_focus(vm, x); }
    else if (kind == SCRIPT_ACT_TYPE) {
        ok = sh_is_elem(h->d, x) && si_click(vm, x, 1);
        /* each character a key: UTF-8's lead bytes begin each */
        for (size_t i = 0; ok && i < n && !h->navigated; i++) {
            if (((unsigned char)text[i] & 0xC0) == 0x80) continue;
            if ((unsigned char)text[i] >= 0x80) continue;
            si_key(vm, text + i, 1);
        }
    } else if (kind == SCRIPT_ACT_KEY) ok = si_key(vm, text, n);
    else ok = 0;
    script_timers_after(vm, horizon);
    return ok;
}

/* ---- FormData (XHR 5) ---- */

static uint32_t si_this_fd(js_vm *vm, jv thisv) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST || vm->objs[jv_idx(thisv)].c != HK_FORMDATA) {
        js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        return 0;
    }
    return jv_idx(thisv);
}

/* ---- a form's named properties (HTML 4.10.3: [LegacyOverrideBuiltIns]) ---- */

static int si_attr_is(const dom_document *d, uint32_t x, const char *name, const vector<char> *w) {
    uint32_t at = sh_attr_z(d, x, name);
    return at && d->attr[at].value.len == len(*w) && memcmp(text_ptr(d, d->attr[at].value), data(*w), len(*w)) == 0;
}
/* the listed element the form owns whose id or name is the property's name, else such an
   img; the first in tree order when several are */
jv script_form_named(js_vm *vm, uint32_t form, uint32_t atom, int *handled) {
    const dom_document *d = vm->host->d;
    uint32_t idx;
    if (js_key_is_sym(atom) || js_atom_index(vm, atom, &idx)) return JV_UNDEF;
    vector<char> name;
    js_str_to_utf8(vm, atom, &name);
    if (!len(name) || name[0] == 1) return JV_UNDEF;
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t c = sh_next_in(d, 1, 1); c; c = sh_next_in(d, c, 1)) {
            if (!sh_is_elem(d, c)) continue;
            int listed = si_is(d, c, "button") || si_is(d, c, "fieldset") || (si_is(d, c, "input") && !si_input_is(d, c, "image")) ||
                         si_is(d, c, "object") || si_is(d, c, "output") || si_is(d, c, "select") || si_is(d, c, "textarea");
            if (pass == 0 ? !listed : !si_is(d, c, "img")) continue;
            if (!si_attr_is(d, c, "id", &name) && !si_attr_is(d, c, "name", &name)) continue;
            if (si_form_of(d, c) != form) continue;
            *handled = 1;
            return sh_wrap(vm, c);
        }
    return JV_UNDEF;
}

/* ---- the unit's natives ---- */

jv script_input_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)callee;
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    vector<char> n, v;
    if (id == SI_ILLEGAL) return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    if (id == SI_CHARSET) {
        int enc = h->charset - 1;
        return sh_cstr(vm, enc >= 0 ? html_enc_name(enc) : "UTF-8");
    }
    /* document.compatMode (DOM 4.5): BackCompat in quirks mode, else CSS1Compat */
    if (id == SI_COMPAT_MODE) return sh_cstr(vm, d->node[1].mode == DOM_DOCUMENT_MODE_QUIRKS ? "BackCompat" : "CSS1Compat");
    if (id >= SI_UIEVENT && id <= SI_HASHCHANGEEVENT) {
        /* an event a script makes: its type, bubbles and cancelable, and its interface's fields */
        static const uint32_t protos[] = { HP_UIEVENT, HP_MOUSEEVENT, HP_POINTEREVENT, HP_KEYBOARDEVENT, HP_FOCUSEVENT, HP_INPUTEVENT, HP_SUBMITEVENT,
                                           HP_FORMDATAEVENT, HP_POPSTATEEVENT, HP_HASHCHANGEEVENT };
        jv form_data = JV_UNDEF;
        if (id == SI_FORMDATAEVENT) {
            form_data = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, "formData")) : JV_UNDEF;
            if (!jv_is_obj(form_data) || vm->objs[jv_idx(form_data)].kind != JK_HOST || vm->objs[jv_idx(form_data)].c != HK_FORMDATA)
                return js_throw_msg(vm, vm->type_error_proto,
                                    "Failed to construct 'FormDataEvent': Failed to read the 'formData' property from 'FormDataEventInit': Required member is undefined.");
        }
        if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
        push_back(n, 0);
        uint32_t flags = 0;
        double detail = 0;
        if (jv_is_obj(a1)) {
            if (js_truthy(vm, js_get(vm, a1, si_atom(vm, "bubbles")))) flags |= EV_BUBBLES;
            if (js_truthy(vm, js_get(vm, a1, si_atom(vm, "cancelable")))) flags |= EV_CANCELABLE;
            if (js_truthy(vm, js_get(vm, a1, si_atom(vm, "composed")))) flags |= EV_COMPOSED;
            jv dt = js_get(vm, a1, si_atom(vm, "detail"));
            if (jv_is_num(dt)) detail = jv_dbl(dt);
        }
        jv ev = si_event(vm, data(n), flags, protos[id - SI_UIEVENT], detail);
        static const char *const nums[] = { "clientX", "clientY", "screenX", "screenY", "button", "buttons", "keyCode", "charCode", "which",
                                            "location", "pointerId", "width", "height", "pressure" };
        static const char *const strs[] = { "key", "code", "pointerType", "data", "inputType" };
        static const char *const bools[] = { "ctrlKey", "shiftKey", "altKey", "metaKey", "repeat", "isComposing", "isPrimary" };
        int mouse = id == SI_MOUSEEVENT || id == SI_POINTEREVENT, key = id == SI_KEYBOARDEVENT;
        for (size_t i = 0; i < sizeof nums / sizeof nums[0] && (mouse || key); i++) {
            int keyf = i >= 6 && i <= 9, ptr = i >= 10;
            if ((keyf && !key) || ((!keyf && !ptr) && !mouse) || (ptr && id != SI_POINTEREVENT)) continue;
            jv x = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, nums[i])) : JV_UNDEF;
            si_def(vm, ev, nums[i], jv_is_num(x) ? x : jv_num(0));
        }
        for (size_t i = 0; i < sizeof strs / sizeof strs[0]; i++) {
            int keyf = i <= 1, ptr = i == 2, inp = i >= 3;
            if ((keyf && !key) || (ptr && id != SI_POINTEREVENT) || (inp && id != SI_INPUTEVENT)) continue;
            jv x = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, strs[i])) : JV_UNDEF;
            si_def(vm, ev, strs[i], jv_is_str(x) ? x : (inp && i == 3 ? JV_NULL : jv_str(A_EMPTY)));
        }
        for (size_t i = 0; i < sizeof bools / sizeof bools[0]; i++) {
            int mod = i <= 3, keyf = i == 4 || i == 5, ptr = i == 6;
            if ((mod && !mouse && !key) || (keyf && !key && !(i == 5 && id == SI_INPUTEVENT)) || (ptr && id != SI_POINTEREVENT)) continue;
            jv x = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, bools[i])) : JV_UNDEF;
            si_def(vm, ev, bools[i], jv_bool(js_truthy(vm, x)));
        }
        if (mouse || id == SI_FOCUSEVENT) {
            jv rt = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, "relatedTarget")) : JV_UNDEF;
            si_def(vm, ev, "relatedTarget", jv_is_obj(rt) ? rt : JV_NULL);
        }
        if (id == SI_SUBMITEVENT) {
            jv sb = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, "submitter")) : JV_UNDEF;
            si_def(vm, ev, "submitter", jv_is_obj(sb) ? sb : JV_NULL);
        }
        if (id == SI_FORMDATAEVENT) si_def(vm, ev, "formData", form_data);
        if (id == SI_POPSTATEEVENT) {
            jv st = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, "state")) : JV_UNDEF;
            si_def(vm, ev, "state", st == JV_UNDEF ? JV_NULL : st);
            si_def(vm, ev, "hasUAVisualTransition", JV_FALSE);
        }
        if (id == SI_HASHCHANGEEVENT) {
            static const char *const urls[] = { "oldURL", "newURL" };
            for (int i = 0; i < 2; i++) {
                jv u = jv_is_obj(a1) ? js_get(vm, a1, si_atom(vm, urls[i])) : JV_UNDEF;
                uint32_t us = u == JV_UNDEF ? A_EMPTY : js_to_string(vm, u);
                if (vm->throwing) return JV_HOLE;
                si_def(vm, ev, urls[i], jv_str(us));
            }
        }
        return ev;
    }
    switch (id) {
    case SI_MODIFIER: {
        if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
        push_back(n, 0);
        static const char *const keys[] = { "Shift", "Control", "Alt", "Meta" };
        static const char *const fields[] = { "shiftKey", "ctrlKey", "altKey", "metaKey" };
        for (int i = 0; i < 4; i++)
            if (strcmp(data(n), keys[i]) == 0) return jv_bool(js_truthy(vm, js_get(vm, thisv, si_atom(vm, fields[i]))));
        return JV_FALSE;
    }
    case SI_HAS_FOCUS: return JV_TRUE;
    case SI_FORMDATA: {
        if (argc && !jv_is_nullish(a0)) {
            uint32_t form = sh_node(vm, a0, 0);
            if (vm->throwing) return JV_HOLE;
            if (!form || !si_is(d, form, "form"))
                return js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'FormData': parameter 1 is not of type 'HTMLFormElement'.");
            uint32_t submitter = argc > 1 && !jv_is_nullish(a1) ? sh_node(vm, a1, 0) : 0;
            uint32_t o = si_form_list(vm, form, submitter);
            if (vm->throwing) return JV_HOLE;
            if (!o) return sh_dom_error(vm, "InvalidStateError", "Failed to construct 'FormData': The form is constructing entry list.");
            return jv_obj(o);
        }
        return jv_obj(si_fd_new(vm));
    }
    case SI_FD_APPEND: case SI_FD_SET: case SI_FD_GET: case SI_FD_GETALL: case SI_FD_HAS: case SI_FD_DELETE: {
        uint32_t o = si_this_fd(vm, thisv);
        if (!o) return JV_HOLE;
        if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
        if (id == SI_FD_APPEND || id == SI_FD_SET) {
            if (!sh_utf8(vm, a1, &v)) return JV_HOLE;
            if (id == SI_FD_SET) {
                /* the first of the name takes the value, the others go */
                int put = 0;
                for (size_t i = 0; i < len(h->fds); i++) {
                    sh_fd e = h->fds[i];
                    if (!e.live || e.owner != o || e.nlen != len(n) || memcmp(data(h->ntext) + e.noff, data(n), len(n)) != 0) continue;
                    if (!put) {
                        h->fds[i].voff = (uint32_t)len(h->ntext);
                        h->fds[i].vlen = (uint32_t)len(v);
                        append_range(h->ntext, data(v), len(v));
                        put = 1;
                    } else h->fds[i].live = 0;
                }
                if (put) return JV_UNDEF;
            }
            si_fd_add(h, o, data(n), len(n), data(v), len(v), 0);
            return JV_UNDEF;
        }
        uint32_t all = id == SI_FD_GETALL ? js_array_new(vm, 0) : 0;
        for (size_t i = 0; i < len(h->fds); i++) {
            sh_fd e = h->fds[i];
            if (!e.live || e.owner != o || e.nlen != len(n) || memcmp(data(h->ntext) + e.noff, data(n), len(n)) != 0) continue;
            if (id == SI_FD_DELETE) { h->fds[i].live = 0; continue; }
            if (id == SI_FD_HAS) return JV_TRUE;
            jv s = sh_str(vm, data(h->ntext) + e.voff, e.vlen);
            if (id == SI_FD_GET) return s;
            js_array_push(vm, all, s);
        }
        if (id == SI_FD_GETALL) return jv_obj(all);
        if (id == SI_FD_HAS) return JV_FALSE;
        if (id == SI_FD_GET) return JV_NULL;
        return JV_UNDEF;
    }
    case SI_FD_ENTRIES: case SI_FD_KEYS: case SI_FD_VALUES: case SI_FD_FOREACH: {
        uint32_t o = si_this_fd(vm, thisv);
        if (!o) return JV_HOLE;
        uint32_t out = js_array_new(vm, 0);
        for (size_t i = 0; i < len(h->fds); i++) {
            sh_fd e = h->fds[i];
            if (!e.live || e.owner != o) continue;
            jv k = sh_str(vm, data(h->ntext) + e.noff, e.nlen);
            jv val = sh_str(vm, data(h->ntext) + e.voff, e.vlen);
            if (id == SI_FD_KEYS) { js_array_push(vm, out, k); continue; }
            if (id == SI_FD_VALUES) { js_array_push(vm, out, val); continue; }
            if (id == SI_FD_FOREACH) {
                if (!js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'forEach' on 'FormData': The callback provided as parameter 1 is not a function.");
                jv cargs[3] = { val, k, thisv };
                js_call(vm, a0, a1, cargs, 3);
                if (vm->throwing) return JV_HOLE;
                continue;
            }
            uint32_t pair = js_array_new(vm, 0);
            js_array_push(vm, pair, k);
            js_array_push(vm, pair, val);
            js_array_push(vm, out, jv_obj(pair));
        }
        if (id == SI_FD_FOREACH) return JV_UNDEF;
        return js_iter_of(vm, jv_obj(out));
    }
    case SI_SUBMIT: case SI_REQUEST_SUBMIT: case SI_RESET: case SI_CHECK_VALIDITY: case SI_REPORT_VALIDITY: {
        uint32_t form = sh_node(vm, thisv, 0);
        if (vm->throwing) return JV_HOLE;
        if (!form || !si_is(d, form, "form")) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        if (id == SI_SUBMIT) { si_submit(vm, form, 0, 1); return JV_UNDEF; }
        if (id == SI_RESET) { si_reset(vm, form); return JV_UNDEF; }
        if (id == SI_CHECK_VALIDITY || id == SI_REPORT_VALIDITY) return jv_bool(si_valid(vm, form, id == SI_REPORT_VALIDITY));
        uint32_t submitter = 0;
        if (argc && !jv_is_nullish(a0)) {
            submitter = sh_node(vm, a0, 0);
            if (vm->throwing) return JV_HOLE;
            if (!submitter || !si_submit_button(d, submitter))
                return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not a submit button.");
            if (si_form_of(d, submitter) != form)
                return sh_dom_error(vm, "NotFoundError", "Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not owned by this form element.");
        }
        si_submit(vm, form, submitter, 0);
        return JV_UNDEF;
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

jv script_input_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled) {
    (void)vm;
    (void)o;
    (void)atom;
    *handled = 0;
    return JV_UNDEF;
}

/* ---- the globals ---- */

void script_input_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t ev = h->protos[HP_EVENT];
    uint32_t ui = js_obj_new(vm, JK_PLAIN, ev);
    uint32_t mouse = js_obj_new(vm, JK_PLAIN, ui);
    uint32_t pointer = js_obj_new(vm, JK_PLAIN, mouse);
    uint32_t key = js_obj_new(vm, JK_PLAIN, ui);
    uint32_t focus = js_obj_new(vm, JK_PLAIN, ui);
    uint32_t input = js_obj_new(vm, JK_PLAIN, ui);
    uint32_t submit = js_obj_new(vm, JK_PLAIN, ev);
    uint32_t formdata = js_obj_new(vm, JK_PLAIN, ev);
    uint32_t popstate = js_obj_new(vm, JK_PLAIN, ev), hashchange = js_obj_new(vm, JK_PLAIN, ev), unload = js_obj_new(vm, JK_PLAIN, ev);
    h->protos[HP_UIEVENT] = ui;
    h->protos[HP_MOUSEEVENT] = mouse;
    h->protos[HP_POINTEREVENT] = pointer;
    h->protos[HP_KEYBOARDEVENT] = key;
    h->protos[HP_FOCUSEVENT] = focus;
    h->protos[HP_INPUTEVENT] = input;
    h->protos[HP_SUBMITEVENT] = submit;
    h->protos[HP_FORMDATAEVENT] = formdata;
    h->protos[HP_POPSTATEEVENT] = popstate;
    h->protos[HP_HASHCHANGEEVENT] = hashchange;
    h->protos[HP_BEFOREUNLOADEVENT] = unload;
    sh_method(vm, mouse, "getModifierState", SI_MODIFIER, 1);
    sh_method(vm, key, "getModifierState", SI_MODIFIER, 1);
    static const char *const names[] = { "UIEvent", "MouseEvent", "PointerEvent", "KeyboardEvent", "FocusEvent", "InputEvent", "SubmitEvent",
                                         "FormDataEvent", "PopStateEvent", "HashChangeEvent" };
    uint32_t protos[] = { ui, mouse, pointer, key, focus, input, submit, formdata, popstate, hashchange };
    for (uint32_t i = 0; i < 10; i++) sh_interface(vm, names[i], SI_UIEVENT + i, protos[i]);
    sh_interface(vm, "BeforeUnloadEvent", SI_ILLEGAL, unload);
    /* the document's encoding, by each of its names */
    static const char *const cs[] = { "characterSet", "charset", "inputEncoding" };
    for (int i = 0; i < 3; i++) {
        uint32_t a = si_atom(vm, cs[i]);
        js_define_accessor(vm, h->protos[HP_DOCUMENT], a, jv_obj(js_native_new(vm, SI_CHARSET, a, 0)), JV_UNDEF, 0);
    }
    uint32_t cma = si_atom(vm, "compatMode");
    js_define_accessor(vm, h->protos[HP_DOCUMENT], cma, jv_obj(js_native_new(vm, SI_COMPAT_MODE, cma, 0)), JV_UNDEF, 0);
    uint32_t fd = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_FORMDATA] = fd;
    static const char *const fm[] = { "append", "set", "get", "getAll", "has", "delete", "entries", "keys", "values", "forEach" };
    for (uint32_t i = 0; i < 10; i++) sh_method(vm, fd, fm[i], SI_FD_APPEND + i, i < 2 ? 2 : i < 6 ? 1 : 0);
    js_define(vm, fd, JS_KEY_ITERATOR, js_get(vm, jv_obj(fd), si_atom(vm, "entries")), JS_SHAPE_HIDDEN);
    sh_interface(vm, "FormData", SI_FORMDATA, fd);
    uint32_t elem = h->protos[HP_ELEMENT];
    sh_method(vm, elem, "submit", SI_SUBMIT, 0);
    sh_method(vm, elem, "requestSubmit", SI_REQUEST_SUBMIT, 0);
    sh_method(vm, elem, "reset", SI_RESET, 0);
    sh_method(vm, elem, "checkValidity", SI_CHECK_VALIDITY, 0);
    sh_method(vm, elem, "reportValidity", SI_REPORT_VALIDITY, 0);
    sh_method(vm, h->protos[HP_DOCUMENT], "hasFocus", SI_HAS_FOCUS, 0);
}

jv script_click_event(js_vm *vm, uint32_t trusted) {
    jv ev = si_event(vm, "click", (trusted ? EV_TRUSTED : 0) | EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED, HP_POINTEREVENT, 0);
    si_mouse_fields(vm, ev, 0, 0, 0, 0, 0, 0, 1);
    si_def(vm, ev, "pointerId", jv_num(-1));
    si_def(vm, ev, "pointerType", jv_str(A_EMPTY));
    si_def(vm, ev, "isPrimary", JV_FALSE);
    return ev;
}

jv script_click_copy(js_vm *vm, jv ev) {
    uint32_t f = vm->objs[jv_idx(ev)].a & (EV_TRUSTED | EV_LAZYPOINT);
    jv x = vm->objs[jv_idx(ev)].x;
    jv c = si_event(vm, "click", f | EV_BUBBLES | EV_CANCELABLE | EV_COMPOSED, HP_POINTEREVENT, jv_is_num(x) ? jv_dbl(x) : 0);
    static const char *const fields[] = { "clientX", "clientY", "x", "y", "pageX", "pageY", "screenX", "screenY", "offsetX", "offsetY",
                                          "movementX", "movementY", "button", "buttons", "relatedTarget", "ctrlKey", "shiftKey", "altKey",
                                          "metaKey", "pointerId", "pointerType", "isPrimary", "width", "height", "pressure", "\001pt" };
    /* a lazy click's point stays to be found, its coordinates not read here */
    for (size_t i = (f & EV_LAZYPOINT) ? 10 : 0; i < sizeof fields / sizeof fields[0]; i++) {
        jv v = js_get(vm, ev, si_atom(vm, fields[i]));
        if (v != JV_UNDEF) js_define(vm, jv_idx(c), si_atom(vm, fields[i]), v, i == 25 ? JS_SHAPE_HIDDEN : 0);
    }
    vm->throwing = 0;
    return c;
}

jv script_event_point(js_vm *vm, uint32_t ev, uint32_t atom, int *handled) {
    int which = -1;
    for (int i = 0; i < 10 && which < 0; i++) if (atom == si_atom(vm, si_coords[i])) which = i;
    if (which < 0) return JV_UNDEF;
    struct js_host *h = vm->host;
    vm->objs[ev].a &= ~EV_LAZYPOINT;
    jv at = js_get(vm, jv_obj(ev), si_atom(vm, "\001pt"));
    uint32_t x = jv_is_num(at) ? (uint32_t)jv_dbl(at) : 0;
    /* the centre of the element's box, the view as it is now */
    double cx = 0, cy = 0, r[4 * 64];
    sh_view(vm);
    uint32_t n = x && x < h->d->node_len && h->d->node[x].live ? script_view_rects(h, x, r, 64) : 0;
    if (n > 64) n = 64;
    if (n) {
        double x0 = r[0], y0 = r[1], x1 = r[0] + r[2], y1 = r[1] + r[3];
        for (uint32_t i = 1; i < n; i++) {
            if (r[4 * i] < x0) x0 = r[4 * i];
            if (r[4 * i + 1] < y0) y0 = r[4 * i + 1];
            if (r[4 * i] + r[4 * i + 2] > x1) x1 = r[4 * i] + r[4 * i + 2];
            if (r[4 * i + 1] + r[4 * i + 3] > y1) y1 = r[4 * i + 1] + r[4 * i + 3];
        }
        cx = x0 + (x1 - x0) / 2;
        cy = y0 + (y1 - y0) / 2;
    }
    jv t = js_get(vm, jv_obj(ev), si_atom(vm, "target"));
    uint32_t target = jv_is_obj(t) ? sh_node(vm, t, 0) : 0;
    vm->throwing = 0;
    si_point_fields(vm, jv_obj(ev), target, cx, cy);
    *handled = 1;
    return js_get(vm, jv_obj(ev), atom);
}

/* a FormData's entries as a request's body: multipart/form-data, and its content type */
void script_formdata_body(js_vm *vm, uint32_t owner, vector<char> *out, char *ctype, size_t cap) {
    static const char boundary[] = "----AmbitiousFormBoundary0123456789";
    si_multipart(vm->host, owner, -1, boundary, out);
    snprintf(ctype, cap, "multipart/form-data; boundary=%s", boundary);
}

/* the collector's roots of this unit (script.h's sh_fd, the user's state): the object of
   each FormData with a live entry (its entries are kept by its index), and the click event
   a user's click is dispatching */
void script_input_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    uint32_t last = 0;
    for (size_t i = 0; i < len(h->fds); i++) {
        sh_fd e = h->fds[i];
        if (!e.live || !e.owner || e.owner == last) continue;
        push_back(*out, jv_obj(e.owner));
        last = e.owner;
    }
    if (jv_is_obj(h->click_ev)) push_back(*out, h->click_ev);
}
