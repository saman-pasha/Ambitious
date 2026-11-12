/* src/script/frames.c -- a page's frames as its scripts see them (Phase 12u): each
   document's window and realm a VM of its own, and the membrane between them.

   THE FRAMES. Every document of a page (its top level's, each iframe's, at any depth) has
   its own VM: its realm (its own Object, Array, Event ...) and its window. The VMs are
   entered in a table every host shares (struct sh_frames, src/script/script.h), which
   src/shell/frame.c fills as it makes the frames, with each frame's parent, its iframe,
   its origin and its sandbox. A window has parent, top, frames (the window itself),
   length, its frames by index (0 .. 31) and by name, frameElement (its iframe, when its
   parent is of its origin; else null), name and postMessage; an iframe element has
   contentWindow and contentDocument (null when the frame's document is of another origin,
   or there is none).

   THE MEMBRANE. An object of one VM is seen in another as a proxy of the engine's
   (src/js/proxy.c) whose handler's traps are this unit's natives: each takes the object
   it stands for (its frame and its index there, kept in the proxy's target), runs the
   operation in that frame's VM (a get, a set, has, delete, the own keys and properties,
   the prototype, a call, a construction) and brings the result back: a primitive as it
   is, a string copied, an error copied as an error of its kind, any other object as the
   proxy of it (one proxy an object, so each look at an object gives the same one, and an
   object's proxy carried back to its own VM is the object itself). Each VM keeps the
   objects others' proxies stand for as its roots, for the page's life. Entering another
   VM brings its clock and its timers' order up to the caller's, and the cascade's
   viewport its frame's (a script's forced layout there is its frame's).

   ORIGINS. A frame's document is of its parent's origin when it is about:srcdoc or
   about:blank (unless its sandbox takes allow-same-origin away), else of its URL's; a
   file: URL's origin is opaque, one of its own, as Chromium has it. A window of another
   origin than the script's offers only HTML's cross-origin properties (window, self,
   frames, length, top, parent, opener, closed, postMessage, close, focus, blur, location,
   and its frames by index); any other read or write throws Chromium's SecurityError:
   "Failed to read a named property 'x' from 'Window': Blocked a frame with origin "o"
   from accessing a cross-origin frame." (o the script's origin).

   MESSAGES. postMessage(message, targetOrigin) copies the message into the target's realm
   as the structured clone algorithm does (primitives, strings, plain objects, arrays,
   dates, errors; their sharing and cycles kept; a function or a node a DataCloneError),
   and queues a task there that fires a MessageEvent at its window: data, origin (the
   sender's), source (the sender's window), lastEventId, ports. A targetOrigin that is not
   "*" must be the target's origin ("/": the sender's), else the message is dropped, as
   Chromium drops it, with its console error.

   THE EVENT LOOP. The timers and tasks of every frame's VM run as one loop in virtual
   time (script_frames_run): the earliest due first, then in the order they were made,
   each VM's microtasks after each task. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "../net/url.h"
#include "host.h"
#include "script.h"

/* the cascade's viewport and the reader's settings (src/css/css.c) */
void css_set_env(double view_w, double view_h, double font_size, int dark, int reduced_motion);
/* an origin's serialization (src/net/net.c) */
int net_url_origin(const char *url, char *out, size_t cap);

/* the natives, from SH_FRAMES_FIRST */
enum {
    F_PARENT = SH_FRAMES_FIRST, F_TOP, F_LENGTH, F_FRAMEELEMENT, F_NAME_GET, F_NAME_SET, F_INDEX, F_NAMED, F_POST, F_XPOST, F_STUB,
    F_NOOP, F_CLOSED,
    F_T_GET, F_T_SET, F_T_HAS, F_T_DELETE, F_T_OWNKEYS, F_T_GOPD, F_T_DEFINE, F_T_GETPROTO, F_T_SETPROTO, F_T_ISEXT, F_T_PREVENT,
    F_T_APPLY, F_T_CONSTRUCT, F_MESSAGE_EVENT, F_DOC_URL
};
/* MessageEvent's prototype, in the host's prototypes' last slot (the script module's own
   kinds stop well short of it) */
enum { F_MSG_PROTO = SCRIPT_PROTOS - 1 };

static jv fr_window(js_vm *vm, uint32_t k);
static jv fr_value(js_vm *to, js_vm *from, uint32_t ff, jv v);

/* ---- the table ---- */

static int fr_same_origin(const sh_frames *fr, uint32_t a, uint32_t b) {
    return a <= fr->n && b <= fr->n && fr->f[a].origin && fr->f[a].origin == fr->f[b].origin;
}
/* the VM's frame may reach frame k's objects whole */
static int fr_open(js_vm *vm, uint32_t k) { return fr_same_origin(vm->host->fr, vm->host->frame, k); }

/* a frame's VM made ready (its window and document), as a page's is at its first script */
static void fr_ready(js_vm *pv) { script_ready(pv); }

/* the cascade's viewport and settings the VM's frame's: entering it */
static void fr_env(js_vm *v) {
    const sh_frames *fr = v->host->fr;
    css_set_env(v->host->view_w, v->host->view_h, fr->font_size > 0 ? fr->font_size : 16, fr->dark, fr->reduced);
}

/* another VM entered from vm: its clock and its timers' order brought up to the caller's */
static void fr_enter(js_vm *vm, js_vm *pv) {
    if (pv->now < vm->now) pv->now = vm->now;
    if (pv->timer_seq < vm->timer_seq) pv->timer_seq = vm->timer_seq;
    fr_env(pv);
}
/* ... and left: the caller's brought up to it */
static void fr_leave(js_vm *vm, js_vm *pv) {
    if (vm->now < pv->now) vm->now = pv->now;
    if (vm->timer_seq < pv->timer_seq) vm->timer_seq = pv->timer_seq;
    fr_env(vm);
}

/* ---- values across realms ---- */

/* a string of from's, copied into to */
static jv fr_str(js_vm *to, js_vm *from, uint32_t s) {
    js_str_flatten(from, s);
    vector<uint16_t> u;
    uint32_t off = from->strs[s].off, n = from->strs[s].len;
    for (uint32_t i = 0; i < n; i++) push_back(u, at(from->units, (size_t)off + i));
    return jv_str(js_str_new(to, data(u), len(u)));
}

/* a key of from's as to's: an atom's text interned there, a well-known symbol itself; 0 for
   a symbol of from's own, which to cannot name */
static uint32_t fr_key(js_vm *to, js_vm *from, uint32_t key) {
    if (js_key_is_sym(key)) return (key & ~JS_SYM_KEY) < JSYM_N ? key : 0;
    jv s = fr_str(to, from, key);
    return js_atom(to, jv_idx(s));
}
/* a trap's key argument (a string or a symbol) as a key */
static uint32_t fr_key_arg(js_vm *vm, jv k) {
    if (jv_is_sym(k)) return JS_SYM_KEY | jv_idx(k);
    if (jv_is_str(k)) return js_atom(vm, jv_idx(k));
    uint32_t s = js_to_string(vm, k);
    return s ? js_atom(vm, s) : A_EMPTY;
}

/* the proxy of vm's that stands for frame k's object obj: its place in the host's remotes
   (three numbers a proxy), or -1 */
static int64_t fr_find(js_vm *vm, uint32_t k, uint32_t obj) {
    struct js_host *h = vm->host;
    for (size_t i = 0; i + 2 < len(h->remotes); i += 3)
        if (h->remotes[i] == k && h->remotes[i + 1] == obj) return (int64_t)i;
    return -1;
}
/* the frame and the object vm's proxy p stands for: 1, or 0 when p is none of the membrane's */
static int fr_proxied(js_vm *vm, uint32_t p, uint32_t *k, uint32_t *obj) {
    struct js_host *h = vm->host;
    if (vm->objs[p].kind != JK_PROXY || !h->mhandler || vm->objs[p].b != h->mhandler) return 0;
    for (size_t i = 0; i + 2 < len(h->remotes); i += 3)
        if (h->remotes[i + 2] == p) {
            *k = h->remotes[i];
            *obj = h->remotes[i + 1];
            return 1;
        }
    return 0;
}
/* the frame and object a trap's target names: 1, or 0 */
static int fr_target(js_vm *vm, jv t, uint32_t *k, uint32_t *obj) {
    if (!jv_is_obj(t)) return 0;
    uint32_t o = jv_idx(t);
    if (vm->objs[o].kind == JK_RECORD) { *k = vm->objs[o].b; *obj = vm->objs[o].c; return 1; }
    if (vm->objs[o].kind == JK_NATIVE && jv_is_num(vm->objs[o].x)) {
        double v = jv_dbl(vm->objs[o].x);
        *k = (uint32_t)(v / 4294967296.0);
        *obj = (uint32_t)(v - (double)*k * 4294967296.0);
        return 1;
    }
    /* an array's: an empty array of this VM's (Array.isArray sees a proxy's target), whose
       proxy the remotes name */
    if (vm->objs[o].kind == JK_ARRAY) {
        struct js_host *h = vm->host;
        for (size_t i = 0; i + 2 < len(h->remotes); i += 3)
            if (vm->objs[h->remotes[i + 2]].a == o) {
                *k = h->remotes[i];
                *obj = h->remotes[i + 1];
                return 1;
            }
    }
    return 0;
}

static void fr_trap(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t n) { sh_method(vm, o, name, id, n); }
/* the membrane's handler of vm's proxies, made at its first need */
static uint32_t fr_handler(js_vm *vm) {
    struct js_host *h = vm->host;
    if (h->mhandler) return h->mhandler;
    uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    fr_trap(vm, o, "get", F_T_GET, 3);
    fr_trap(vm, o, "set", F_T_SET, 4);
    fr_trap(vm, o, "has", F_T_HAS, 2);
    fr_trap(vm, o, "deleteProperty", F_T_DELETE, 2);
    fr_trap(vm, o, "ownKeys", F_T_OWNKEYS, 1);
    fr_trap(vm, o, "getOwnPropertyDescriptor", F_T_GOPD, 2);
    fr_trap(vm, o, "defineProperty", F_T_DEFINE, 3);
    fr_trap(vm, o, "getPrototypeOf", F_T_GETPROTO, 1);
    fr_trap(vm, o, "setPrototypeOf", F_T_SETPROTO, 2);
    fr_trap(vm, o, "isExtensible", F_T_ISEXT, 1);
    fr_trap(vm, o, "preventExtensions", F_T_PREVENT, 1);
    fr_trap(vm, o, "apply", F_T_APPLY, 3);
    fr_trap(vm, o, "construct", F_T_CONSTRUCT, 3);
    h->mhandler = o;
    return o;
}

/* vm's proxy of frame k's object obj, made at its first need; obj kept by its VM */
static jv fr_remote(js_vm *vm, uint32_t k, uint32_t obj) {
    int64_t at_ = fr_find(vm, k, obj);
    if (at_ >= 0) return jv_obj(vm->host->remotes[(size_t)at_ + 2]);
    js_vm *pv = vm->host->fr->vm[k];
    int callable = js_is_callable(pv, jv_obj(obj));
    uint32_t t;
    if (callable) {
        t = js_native_new(vm, F_STUB, A_EMPTY, 0);
        vm->objs[t].x = jv_num((double)k * 4294967296.0 + (double)obj);
        /* a constructor there is one here: a native with a prototype of its own is (the proxy's
           construct trap makes the object, there). Its prototype the proxy of the one there, as
           the proxy's get must give it back (a function's prototype is not configurable) */
        if (js_is_constructor(pv, jv_obj(obj))) {
            jv rp = js_get(pv, jv_obj(obj), A_PROTOTYPE);
            pv->throwing = 0;
            js_define(vm, t, A_PROTOTYPE, fr_value(vm, pv, k, rp), JS_SHAPE_PROTO);
        }
    } else if (pv->objs[obj].kind == JK_ARRAY) {
        t = js_array_new(vm, 0);
    } else {
        t = js_obj_new(vm, JK_RECORD, 0);
        vm->objs[t].b = k;
        vm->objs[t].c = obj;
    }
    uint32_t hd = fr_handler(vm);
    uint32_t p = js_obj_new(vm, JK_PROXY, 0);
    vm->objs[p].a = t;
    vm->objs[p].b = hd;
    vm->objs[p].c = (uint32_t)callable;
    /* a key no script can name: a shape no ordinary object has (as Proxy's own) */
    js_define(vm, p, JS_SYM_KEY, JV_UNDEF, JS_SHAPE_HIDDEN);
    push_back(vm->host->remotes, k);
    push_back(vm->host->remotes, obj);
    push_back(vm->host->remotes, p);
    push_back(pv->host->exports, obj);
    return jv_obj(p);
}

/* an error of from's copied into to as an error of its kind, its message and name kept */
static jv fr_error(js_vm *to, js_vm *from, uint32_t e) {
    jv m = js_get(from, jv_obj(e), A_MESSAGE);
    jv n = js_get(from, jv_obj(e), A_NAME);
    from->throwing = 0;
    vector<char> name;
    if (jv_is_str(n)) js_str_to_utf8(from, jv_idx(n), &name);
    push_back(name, 0);
    uint32_t proto = to->error_proto;
    if (strcmp(data(name), "TypeError") == 0) proto = to->type_error_proto;
    else if (strcmp(data(name), "RangeError") == 0) proto = to->range_error_proto;
    else if (strcmp(data(name), "SyntaxError") == 0) proto = to->syntax_error_proto;
    else if (strcmp(data(name), "ReferenceError") == 0) proto = to->reference_error_proto;
    jv ms = jv_is_str(m) ? fr_str(to, from, jv_idx(m)) : jv_str(A_EMPTY);
    uint32_t c = js_error_new(to, proto, jv_idx(ms));
    if (proto == to->error_proto && strcmp(data(name), "Error") != 0 && jv_is_str(n))
        js_define(to, c, A_NAME, fr_str(to, from, jv_idx(n)), JS_SHAPE_HIDDEN);
    return jv_obj(c);
}

/* a value of from's (the VM of frame ff) as to sees it */
static jv fr_value(js_vm *to, js_vm *from, uint32_t ff, jv v) {
    if (jv_is_str(v)) return fr_str(to, from, jv_idx(v));
    if (jv_is_sym(v)) return jv_idx(v) < JSYM_N ? v : JV_UNDEF;
    if (jv_is_big(v)) return JV_UNDEF;
    if (!jv_is_obj(v)) return v;
    uint32_t o = jv_idx(v), k, obj;
    if (fr_proxied(from, o, &k, &obj)) {
        if (k == to->host->frame) return jv_obj(obj);
        return fr_remote(to, k, obj);
    }
    if (from->objs[o].kind == JK_ERROR) return fr_error(to, from, o);
    return fr_remote(to, ff, o);
}

/* after an operation run in pv for vm: its exception taken over as vm's, or its result
   brought back */
static jv fr_back(js_vm *vm, js_vm *pv, uint32_t k, jv r) {
    if (pv->throwing) {
        jv e = pv->exception;
        pv->throwing = 0;
        pv->exception = JV_UNDEF;
        jv le = fr_value(vm, pv, k, e);
        fr_leave(vm, pv);
        return js_throw(vm, le);
    }
    jv out = fr_value(vm, pv, k, r);
    fr_leave(vm, pv);
    return out;
}

/* ---- the windows ---- */

/* frame k's window as the VM sees it: its own global, or the proxy of another's; null for
   a frame not made */
static jv fr_window(js_vm *vm, uint32_t k) {
    sh_frames *fr = vm->host->fr;
    if (k == vm->host->frame) return jv_obj(vm->global);
    if (k > fr->n || fr->f[k].state == SH_FRAME_NONE) return JV_NULL;
    js_vm *pv = fr->vm[k];
    fr_ready(pv);
    return fr_remote(vm, k, pv->global);
}

/* the i-th frame of frame k's document (in the order they were made: tree order), or 0 */
static uint32_t fr_child(const sh_frames *fr, uint32_t k, uint32_t i) {
    uint32_t seen = 0;
    for (uint32_t j = 1; j <= fr->n; j++)
        if (fr->f[j].parent == k && fr->f[j].state != SH_FRAME_NONE && seen++ == i) return j;
    return 0;
}

/* a DOMException named name, its message msg (UTF-8), thrown */
static jv fr_dom_error(js_vm *vm, const char *name, const char *msg) {
    uint32_t e = js_error_new(vm, vm->error_proto, js_str_utf8(vm, msg, strlen(msg)));
    js_define(vm, e, A_NAME, sh_cstr(vm, name), JS_SHAPE_HIDDEN);
    return js_throw(vm, jv_obj(e));
}
/* Chromium's SecurityError for a cross-origin frame's property read (set 0) or written (1),
   or for any other look at it (2); key the property's */
static jv fr_blocked(js_vm *vm, uint32_t key, int how) {
    vector<char> name;
    if (!js_key_is_sym(key)) js_str_to_utf8(vm, key, &name);
    push_back(name, 0);
    char msg[1024];
    const char *o = vm->host->fr->f[vm->host->frame].origin_text;
    if (how == 0) snprintf(msg, sizeof msg, "Failed to read a named property '%s' from 'Window': Blocked a frame with origin \"%s\" from accessing a cross-origin frame.", data(name), o);
    else if (how == 1) snprintf(msg, sizeof msg, "Failed to set a named property '%s' on 'Window': Blocked a frame with origin \"%s\" from accessing a cross-origin frame.", data(name), o);
    else snprintf(msg, sizeof msg, "Blocked a frame with origin \"%s\" from accessing a cross-origin frame.", o);
    return fr_dom_error(vm, "SecurityError", msg);
}

/* the VM's postMessage of frame k: a function of its own realm whose call sends from it */
static jv fr_forwarder(js_vm *vm, uint32_t k) {
    struct js_host *h = vm->host;
    if (k >= len(h->xwins)) resize(h->xwins, (size_t)k + 1, 0u);
    if (!h->xwins[k]) {
        uint32_t f = js_native_new(vm, F_XPOST, js_atom_ascii(vm, "postMessage"), 1);
        vm->objs[f].x = jv_num(k);
        h->xwins[k] = f;
    }
    return jv_obj(h->xwins[k]);
}

/* a cross-origin window's own: its properties HTML lets another origin read (1 handled,
   the value in *out), else a SecurityError (handled too) */
static jv fr_cross_get(js_vm *vm, uint32_t k, uint32_t key) {
    sh_frames *fr = vm->host->fr;
    if (js_key_is_sym(key)) return JV_UNDEF;                    /* @@toStringTag, @@hasInstance ...: undefined */
    vector<char> t;
    js_str_to_utf8(vm, key, &t);
    push_back(t, 0);
    const char *n = data(t);
    if (strcmp(n, "window") == 0 || strcmp(n, "self") == 0 || strcmp(n, "frames") == 0) return fr_window(vm, k);
    if (strcmp(n, "parent") == 0) return fr_window(vm, k ? fr->f[k].parent : 0);
    if (strcmp(n, "top") == 0) return fr_window(vm, 0);
    if (strcmp(n, "opener") == 0) return JV_NULL;
    if (strcmp(n, "closed") == 0) return JV_FALSE;
    if (strcmp(n, "then") == 0) return JV_UNDEF;
    if (strcmp(n, "postMessage") == 0) return fr_forwarder(vm, k);
    if (strcmp(n, "close") == 0 || strcmp(n, "focus") == 0 || strcmp(n, "blur") == 0)
        return jv_obj(js_native_new(vm, F_NOOP, key, 0));
    if (strcmp(n, "length") == 0) {
        uint32_t c = 0;
        while (fr_child(fr, k, c)) c++;
        return jv_num(c);
    }
    if (strcmp(n, "location") == 0) return jv_obj(js_obj_new(vm, JK_PLAIN, vm->object_proto));
    uint32_t idx;
    if (js_atom_index(vm, key, &idx)) {
        uint32_t c = fr_child(fr, k, idx);
        return c ? fr_window(vm, c) : JV_UNDEF;
    }
    return fr_blocked(vm, key, 0);
}

/* ---- the traps ---- */

static jv fr_get(js_vm *vm, uint32_t k, uint32_t obj, uint32_t key) {
    js_vm *pv = vm->host->fr->vm[k];
    int win = obj == pv->global;
    if (win && !fr_open(vm, k)) return fr_cross_get(vm, k, key);
    if (!fr_open(vm, k)) return fr_blocked(vm, key, 0);
    if (win && key == js_atom_ascii(vm, "postMessage")) return fr_forwarder(vm, k);
    uint32_t rk = fr_key(pv, vm, key);
    if (!rk) return JV_UNDEF;
    fr_enter(vm, pv);
    jv r = js_get(pv, jv_obj(obj), rk);
    return fr_back(vm, pv, k, r);
}

/* the trap's arguments, from the args array of an apply or construct, brought into pv */
static void fr_args(js_vm *vm, js_vm *pv, jv arr, vector<jv> *out) {
    if (!jv_is_obj(arr)) return;
    uint32_t a = jv_idx(arr);
    uint32_t n = vm->objs[a].kind == JK_ARRAY ? vm->objs[a].b : 0;
    for (uint32_t i = 0; i < n; i++) push_back(*out, fr_value(pv, vm, vm->host->frame, js_array_get(vm, a, i)));
}

/* a descriptor object of vm's for a property of pv's (configurable, whatever it is there:
   a proxy may not report one that its target has not as such) */
static jv fr_desc(js_vm *vm, js_vm *pv, uint32_t k, jv v, uint32_t flags, int fixed) {
    uint32_t d = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    if (flags & JS_SHAPE_ACCESSOR) {
        uint32_t acc = jv_is_obj(v) ? jv_idx(v) : 0;
        jv g = acc ? pv->objs[acc].x : JV_UNDEF;
        jv s = acc && pv->objs[acc].a ? jv_obj(pv->objs[acc].a) : JV_UNDEF;
        js_define(vm, d, A_GET, fr_value(vm, pv, k, g), 0);
        js_define(vm, d, A_SET, fr_value(vm, pv, k, s), 0);
    } else {
        js_define(vm, d, A_VALUE, fr_value(vm, pv, k, v), 0);
        js_define(vm, d, A_WRITABLE, jv_bool(!(flags & JS_SHAPE_READONLY)), 0);
    }
    js_define(vm, d, A_ENUMERABLE, jv_bool(!(flags & JS_SHAPE_HIDDEN)), 0);
    js_define(vm, d, A_CONFIGURABLE, jv_bool(!fixed), 0);
    return jv_obj(d);
}

static jv fr_trap_call(js_vm *vm, uint32_t id, uint32_t args, uint32_t argc) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    uint32_t k, obj;
    if (!fr_target(vm, a0, &k, &obj) || k > vm->host->fr->n) return JV_UNDEF;
    js_vm *pv = vm->host->fr->vm[k];
    if (id == F_T_GET) return fr_get(vm, k, obj, fr_key_arg(vm, a1));
    int open = fr_open(vm, k);
    if (id == F_T_GETPROTO) {
        if (!open) return JV_NULL;
        fr_enter(vm, pv);
        uint32_t pr = pv->objs[obj].kind == JK_PROXY ? 0 : pv->objs[obj].proto;
        return fr_back(vm, pv, k, pr ? jv_obj(pr) : JV_NULL);
    }
    if (id == F_T_ISEXT) return JV_TRUE;
    if (id == F_T_PREVENT || id == F_T_SETPROTO) return JV_FALSE;
    if (id == F_T_SET) {
        uint32_t key = fr_key_arg(vm, a1);
        if (!open) {
            if (!js_key_is_sym(key) && key == js_atom_ascii(vm, "location")) return JV_TRUE;
            return fr_blocked(vm, key, 1);
        }
        uint32_t rk = fr_key(pv, vm, key);
        if (!rk) return JV_TRUE;
        jv v = fr_value(pv, vm, vm->host->frame, a2);
        fr_enter(vm, pv);
        int ok = js_set(pv, jv_obj(obj), rk, v);
        jv r = fr_back(vm, pv, k, JV_UNDEF);
        if (r == JV_HOLE) return JV_HOLE;
        return jv_bool(ok);
    }
    if (!open) return fr_blocked(vm, id == F_T_OWNKEYS ? A_EMPTY : fr_key_arg(vm, a1), 2);
    if (id == F_T_HAS || id == F_T_DELETE || id == F_T_GOPD || id == F_T_DEFINE) {
        uint32_t rk = fr_key(pv, vm, fr_key_arg(vm, a1));
        if (!rk) return id == F_T_GOPD ? JV_UNDEF : JV_FALSE;
        fr_enter(vm, pv);
        if (id == F_T_HAS) {
            int b = pv->objs[obj].kind == JK_PROXY ? js_proxy_has(pv, obj, rk) : js_has(pv, obj, rk);
            jv r = fr_back(vm, pv, k, JV_UNDEF);
            return r == JV_HOLE ? JV_HOLE : jv_bool(b);
        }
        if (id == F_T_DELETE) {
            int b = js_delete(pv, obj, rk);
            jv r = fr_back(vm, pv, k, JV_UNDEF);
            return r == JV_HOLE ? JV_HOLE : jv_bool(b);
        }
        if (id == F_T_DEFINE) {
            /* a data property's value defined there (an accessor's pair is not carried) */
            jv v = jv_is_obj(a2) ? js_get(vm, a2, A_VALUE) : JV_UNDEF;
            jv pvv = fr_value(pv, vm, vm->host->frame, v);
            js_define(pv, obj, rk, pvv, 0);
            jv r = fr_back(vm, pv, k, JV_UNDEF);
            return r == JV_HOLE ? JV_HOLE : JV_TRUE;
        }
        jv v = JV_UNDEF;
        uint32_t flags = 0;
        int has = js_get_own(pv, obj, rk, &v, &flags);
        jv r = fr_back(vm, pv, k, JV_UNDEF);
        if (r == JV_HOLE) return JV_HOLE;
        if (!has || v == JV_HOLE) return JV_UNDEF;
        /* an array's length, its target's own too, is not configurable there either */
        int fixed = jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_ARRAY && fr_key_arg(vm, a1) == A_LENGTH;
        return fr_desc(vm, pv, k, v, flags, fixed);
    }
    if (id == F_T_OWNKEYS) {
        fr_enter(vm, pv);
        uint32_t arr = js_array_new(pv, 0);
        js_own_keys(pv, obj, 0, arr);
        uint32_t out = js_array_new(vm, 0);
        uint32_t n = pv->objs[arr].b;
        for (uint32_t i = 0; i < n; i++) {
            jv s = js_array_get(pv, arr, i);
            if (jv_is_str(s)) js_array_push(vm, out, fr_str(vm, pv, jv_idx(s)));
        }
        jv r = fr_back(vm, pv, k, JV_UNDEF);
        return r == JV_HOLE ? JV_HOLE : jv_obj(out);
    }
    if (id == F_T_APPLY || id == F_T_CONSTRUCT) {
        vector<jv> av;
        fr_args(vm, pv, id == F_T_APPLY ? a2 : a1, &av);
        jv th = id == F_T_APPLY ? fr_value(pv, vm, vm->host->frame, a1) : JV_UNDEF;
        fr_enter(vm, pv);
        jv r = id == F_T_APPLY ? js_call(pv, jv_obj(obj), th, data(av), (uint32_t)len(av))
                               : js_construct(pv, jv_obj(obj), data(av), (uint32_t)len(av), jv_obj(obj));
        return fr_back(vm, pv, k, r);
    }
    return JV_UNDEF;
}

/* ---- messages ---- */

/* a structured clone of from's value v (from the VM of frame ff) made in to: memo pairs
   from's objects with their copies (shared objects and cycles kept). 0 when it cannot be
   cloned: a DataCloneError thrown in from */
static int fr_clone(js_vm *to, js_vm *from, uint32_t ff, jv v, vector<uint32_t> *memo, jv *out, int depth) {
    if (!jv_is_obj(v)) { *out = fr_value(to, from, ff, v); return 1; }
    uint32_t o = jv_idx(v), k, obj;
    if (fr_proxied(from, o, &k, &obj)) {
        js_vm *ov = from->host->fr->vm[k];
        return fr_clone(to, ov, k, jv_obj(obj), memo, out, depth);
    }
    for (size_t i = 0; i + 2 < len(*memo); i += 3)
        if (at(*memo, i) == ff && at(*memo, i + 1) == o) { *out = jv_obj(at(*memo, i + 2)); return 1; }
    if (depth > 200) { fr_dom_error(from, "DataCloneError", "Failed to execute 'postMessage' on 'Window': the message is nested too deep."); return 0; }
    uint32_t kind = from->objs[o].kind;
    if (kind == JK_ERROR) { *out = fr_error(to, from, o); return 1; }
    if (kind == JK_DATE) {
        uint32_t d = js_obj_new(to, JK_DATE, to->date_proto);
        to->objs[d].x = from->objs[o].x;
        *out = jv_obj(d);
        return 1;
    }
    if (kind == JK_ARRAY) {
        uint32_t a = js_array_new(to, 0);
        push_back(*memo, ff);
        push_back(*memo, o);
        push_back(*memo, a);
        uint32_t n = from->objs[o].b;
        for (uint32_t i = 0; i < n; i++) {
            jv e;
            if (!fr_clone(to, from, ff, js_array_get(from, o, i), memo, &e, depth + 1)) return 0;
            js_array_push(to, a, e);
        }
        *out = jv_obj(a);
        return 1;
    }
    if (kind != JK_PLAIN) {
        const char *what = js_is_callable(from, v) ? "function" : kind == JK_HOST ? "the node" : "the object";
        char msg[256];
        snprintf(msg, sizeof msg, "Failed to execute 'postMessage' on 'Window': %s could not be cloned.", what);
        fr_dom_error(from, "DataCloneError", msg);
        return 0;
    }
    uint32_t c = js_obj_new(to, JK_PLAIN, to->object_proto);
    push_back(*memo, ff);
    push_back(*memo, o);
    push_back(*memo, c);
    uint32_t keys = js_array_new(from, 0);
    js_own_keys(from, o, 1, keys);
    uint32_t n = from->objs[keys].b;
    for (uint32_t i = 0; i < n; i++) {
        jv s = js_array_get(from, keys, i);
        if (!jv_is_str(s)) continue;
        uint32_t ka = js_atom(from, jv_idx(s));
        jv e;
        if (!fr_clone(to, from, ff, js_get(from, v, ka), memo, &e, depth + 1)) return 0;
        js_define(to, c, fr_key(to, from, ka), e, 0);
    }
    *out = jv_obj(c);
    return 1;
}

/* the VM's frame sends message msg to frame k's window, for targetOrigin to (a string, or
   an options object's targetOrigin): a MessageEvent queued there */
static jv fr_post(js_vm *vm, uint32_t k, jv msg, jv to_) {
    sh_frames *fr = vm->host->fr;
    uint32_t me = vm->host->frame;
    if (k > fr->n || fr->f[k].state == SH_FRAME_NONE) return JV_UNDEF;
    jv tov = to_;
    if (jv_is_obj(to_)) tov = js_get(vm, to_, js_atom_ascii(vm, "targetOrigin"));
    vector<char> target;
    if (jv_is_str(tov)) js_str_to_utf8(vm, jv_idx(tov), &target);
    else if (tov == JV_UNDEF) push_back(target, '/');
    else {
        uint32_t s = js_to_string(vm, tov);
        if (!s) return JV_HOLE;
        js_str_to_utf8(vm, s, &target);
    }
    push_back(target, 0);
    const char *want = fr->f[k].origin_text;
    char mismatch[1200];
    mismatch[0] = 0;
    if (strcmp(data(target), "*") != 0) {
        char og[512];
        if (strcmp(data(target), "/") == 0) snprintf(og, sizeof og, "%s", fr->f[me].origin_text);
        else if (!net_url_origin(data(target), og, sizeof og)) {
            char m[700];
            snprintf(m, sizeof m, "Failed to execute 'postMessage' on 'Window': Invalid target origin '%s' in a call to 'postMessage'.", data(target));
            return fr_dom_error(vm, "SyntaxError", m);
        }
        /* an opaque origin is no other's, its own included, as a URL names none: the message
           dropped when it would be dispatched, with Chromium's console error then */
        if (strcmp(og, "null") == 0 || strcmp(og, want) != 0 || !fr->f[k].origin)
            snprintf(mismatch, sizeof mismatch,
                     "Failed to execute 'postMessage' on 'DOMWindow': The target origin provided ('%s') does not match the recipient window's origin ('%s').",
                     og, want);
    }
    js_vm *tv = fr->vm[k];
    fr_ready(tv);
    vector<uint32_t> memo;
    jv data_ = JV_UNDEF;
    fr_enter(vm, tv);
    int ok = fr_clone(tv, vm, me, msg, &memo, &data_, 0);
    fr_leave(vm, tv);
    if (!ok) return JV_HOLE;
    jv ev = sh_event_new(tv, js_atom_ascii(tv, "message"), EV_TRUSTED);
    uint32_t e = jv_idx(ev);
    if (tv->host->protos[F_MSG_PROTO]) js_set_proto(tv, e, tv->host->protos[F_MSG_PROTO]);
    js_define(tv, e, js_atom_ascii(tv, "data"), data_, 0);
    js_define(tv, e, js_atom_ascii(tv, "origin"), sh_cstr(tv, fr->f[me].origin_text), 0);
    js_define(tv, e, js_atom_ascii(tv, "lastEventId"), jv_str(A_EMPTY), 0);
    js_define(tv, e, js_atom_ascii(tv, "source"), fr_window(tv, me), 0);
    js_define(tv, e, js_atom_ascii(tv, "ports"), jv_obj(js_array_new(tv, 0)), 0);
    if (mismatch[0]) tv->objs[e].x = jv_str(js_str_utf8(tv, mismatch, strlen(mismatch)));
    if (tv->now < vm->now) tv->now = vm->now;
    if (tv->timer_seq < vm->timer_seq) tv->timer_seq = vm->timer_seq;
    sh_task_obj(tv, 0, SH_TASK_MESSAGE, e);
    if (vm->timer_seq < tv->timer_seq) vm->timer_seq = tv->timer_seq;
    fr_env(vm);
    return JV_UNDEF;
}

void script_frames_task(js_vm *vm, uint32_t kind, uint32_t arg) {
    if (kind != SH_TASK_MESSAGE) return;
    struct js_host *h = vm->host;
    /* a target origin that did not hold: its error, and no event */
    if (jv_is_str(vm->objs[arg].x)) {
        js_console(vm, 3, jv_idx(vm->objs[arg].x));
        return;
    }
    uint32_t ph = h->phase;
    h->phase = SCRIPT_PHASE_EVENT;
    sh_dispatch(vm, JS_WINDOW, jv_obj(arg));
    h->phase = ph;
}

/* ---- the window's own ---- */

/* the frame's window named on its parent's: its iframe's name, read as the parent's
   property, when that window is made and the name no property of its own */
static void fr_name_child(js_vm *vm, uint32_t k) {
    sh_frames *fr = vm->host->fr;
    if (!vm->host->ready || k > fr->n || !fr->f[k].name[0]) return;
    uint32_t a = js_atom(vm, js_str_utf8(vm, fr->f[k].name, strlen(fr->f[k].name)));
    jv v;
    uint32_t fl;
    if (js_get_own(vm, vm->global, a, &v, &fl)) return;
    uint32_t g = js_native_new(vm, F_NAMED, a, 0);
    vm->objs[g].x = jv_num(k);
    js_define_accessor(vm, vm->global, a, jv_obj(g), JV_UNDEF, JS_SHAPE_HIDDEN);
}

void script_frame_made(js_vm *vm, uint32_t k) {
    if (vm->host && vm->host->fr) fr_name_child(vm, k);
}

static void fr_getter(js_vm *vm, const char *name, uint32_t get, uint32_t set) {
    uint32_t a = js_atom_ascii(vm, name);
    jv g = jv_obj(js_native_new(vm, get, a, 0));
    jv s = set ? jv_obj(js_native_new(vm, set, a, 1)) : JV_UNDEF;
    js_define_accessor(vm, vm->global, a, g, s, JS_SHAPE_HIDDEN);
}

void script_frames_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    sh_frames *fr = h->fr;
    uint32_t g = vm->global;
    fr_getter(vm, "parent", F_PARENT, 0);
    fr_getter(vm, "top", F_TOP, 0);
    fr_getter(vm, "length", F_LENGTH, 0);
    fr_getter(vm, "frameElement", F_FRAMEELEMENT, 0);
    fr_getter(vm, "name", F_NAME_GET, F_NAME_SET);
    fr_getter(vm, "closed", F_CLOSED, 0);
    js_define(vm, g, js_atom_ascii(vm, "frames"), jv_obj(g), JS_SHAPE_HIDDEN);
    js_define(vm, g, js_atom_ascii(vm, "opener"), JV_NULL, JS_SHAPE_HIDDEN);
    sh_method(vm, g, "postMessage", F_POST, 1);
    /* the window's frames by index: its i-th frame's window, or undefined */
    for (uint32_t i = 0; i < SCRIPT_FRAMES; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "%u", i);
        uint32_t a = js_atom_ascii(vm, nm);
        uint32_t f = js_native_new(vm, F_INDEX, a, 0);
        vm->objs[f].x = jv_num(i);
        js_define_accessor(vm, g, a, jv_obj(f), JV_UNDEF, JS_SHAPE_HIDDEN);
    }
    /* the document's URL (and documentURI): about:srcdoc, about:blank, its own */
    static const char *const du[] = { "URL", "documentURI" };
    for (int i = 0; i < 2; i++) {
        uint32_t a = js_atom_ascii(vm, du[i]);
        js_define_accessor(vm, h->protos[HP_DOCUMENT], a, jv_obj(js_native_new(vm, F_DOC_URL, a, 0)), JV_UNDEF, 0);
    }
    /* MessageEvent: an Event's kind, for instanceof */
    uint32_t mp = js_obj_new(vm, JK_PLAIN, h->protos[HP_EVENT]);
    h->protos[F_MSG_PROTO] = mp;
    sh_interface(vm, "MessageEvent", F_MESSAGE_EVENT, mp);
    for (uint32_t k = 1; k <= fr->n; k++) if (fr->f[k].parent == h->frame) fr_name_child(vm, k);
}

jv script_frames_get(js_vm *vm, uint32_t x, uint32_t atom, int *handled) {
    struct js_host *h = vm->host;
    sh_frames *fr = h->fr;
    int win = atom == js_atom_ascii(vm, "contentWindow"), doc = atom == js_atom_ascii(vm, "contentDocument");
    if (!win && !doc) return JV_UNDEF;
    *handled = 1;
    uint32_t k = 0;
    for (uint32_t j = 1; j <= fr->n && !k; j++)
        if (fr->f[j].parent == h->frame && fr->f[j].iframe == x && fr->f[j].iframe_gen == h->d->node[x].gen && fr->f[j].state != SH_FRAME_NONE) k = j;
    if (!k) return JV_NULL;
    if (win) return fr_window(vm, k);
    if (!fr_open(vm, k) || fr->f[k].state == SH_FRAME_FAILED) return JV_NULL;
    js_vm *pv = fr->vm[k];
    fr_ready(pv);
    return fr_value(vm, pv, k, sh_wrap(pv, 1));
}

jv script_frames_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt, int *handled) {
    (void)thisv;
    (void)newt;
    *handled = 1;
    struct js_host *h = vm->host;
    sh_frames *fr = h->fr;
    if (!fr) { *handled = 0; return JV_UNDEF; }
    uint32_t me = h->frame;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    double data_ = jv_is_obj(callee) && jv_is_num(vm->objs[jv_idx(callee)].x) ? jv_dbl(vm->objs[jv_idx(callee)].x) : 0;
    if (id >= F_T_GET && id <= F_T_CONSTRUCT) return fr_trap_call(vm, id, args, argc);
    if (id == F_PARENT) return fr_window(vm, me ? fr->f[me].parent : 0);
    if (id == F_TOP) return fr_window(vm, 0);
    if (id == F_LENGTH) {
        uint32_t c = 0;
        while (fr_child(fr, me, c)) c++;
        return jv_num(c);
    }
    if (id == F_INDEX) {
        uint32_t c = fr_child(fr, me, (uint32_t)data_);
        return c ? fr_window(vm, c) : JV_UNDEF;
    }
    if (id == F_NAMED) return fr_window(vm, (uint32_t)data_);
    if (id == F_FRAMEELEMENT) {
        if (!me) return JV_NULL;
        uint32_t p = fr->f[me].parent;
        if (!fr_same_origin(fr, me, p)) return JV_NULL;
        js_vm *pv = fr->vm[p];
        fr_ready(pv);
        dom_document *pd = pv->host->d;
        uint32_t x = fr->f[me].iframe;
        if (!x || x >= pd->node_len || !pd->node[x].live || pd->node[x].gen != fr->f[me].iframe_gen) return JV_NULL;
        return fr_value(vm, pv, p, sh_wrap(pv, x));
    }
    if (id == F_NAME_GET) return sh_cstr(vm, fr->f[me].name);
    if (id == F_NAME_SET) {
        vector<char> t;
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        js_str_to_utf8(vm, s, &t);
        snprintf(fr->f[me].name, sizeof fr->f[me].name, "%.*s", (int)len(t), data(t));
        return JV_UNDEF;
    }
    if (id == F_CLOSED) return JV_FALSE;
    if (id == F_NOOP || id == F_STUB) return JV_UNDEF;
    if (id == F_MESSAGE_EVENT) return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    if (id == F_DOC_URL) {
        char u[8400];
        sh_page_url(vm, u, sizeof u);
        return sh_cstr(vm, u);
    }
    if (id == F_POST) return fr_post(vm, me, a0, a1);
    if (id == F_XPOST) return fr_post(vm, (uint32_t)data_, a0, a1);
    *handled = 0;
    return JV_UNDEF;
}

void script_frames_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    if (!h || !h->fr) return;
    for (size_t i = 0; i < len(h->exports); i++) push_back(*out, jv_obj(h->exports[i]));
    for (size_t i = 0; i + 2 < len(h->remotes); i += 3) push_back(*out, jv_obj(h->remotes[i + 2]));
    for (size_t i = 0; i < len(h->xwins); i++) if (h->xwins[i]) push_back(*out, jv_obj(h->xwins[i]));
    if (h->mhandler) push_back(*out, jv_obj(h->mhandler));
}

/* ---- the event loop over every frame's VM ---- */

/* every made VM's timers' order brought up to the latest */
static void fr_sync(sh_frames *fr) {
    uint32_t seq = 0;
    for (uint32_t k = 0; k <= fr->n; k++) if (fr->vm[k] && fr->vm[k]->host && fr->vm[k]->timer_seq > seq) seq = fr->vm[k]->timer_seq;
    for (uint32_t k = 0; k <= fr->n; k++) if (fr->vm[k] && fr->vm[k]->host) fr->vm[k]->timer_seq = seq;
}

void script_frames_run(sh_frames *fr, double horizon) {
    double now = 0;
    for (uint32_t k = 0; k <= fr->n; k++) {
        js_vm *v = fr->vm[k];
        if (v && v->host && v->host->ready && v->now > now) now = v->now;
    }
    double until = now + horizon;
    fr_sync(fr);
    for (uint32_t guard = 0; guard < 100000; guard++) {
        int64_t best = -1;
        double bd = 0;
        uint32_t bs = 0;
        for (uint32_t k = 0; k <= fr->n; k++) {
            js_vm *v = fr->vm[k];
            if (!v || !v->host || !v->host->ready || v->host->navigated) continue;
            for (size_t i = 0; i < len(v->timers); i++) {
                js_timer t = v->timers[i];
                if (!t.live) continue;
                if (best < 0 || t.due < bd || (t.due == bd && t.seq < bs)) { best = (int64_t)k; bd = t.due; bs = t.seq; }
            }
        }
        if (best < 0 || bd > until) break;
        js_vm *v = fr->vm[(size_t)best];
        if (v->now < now) v->now = now;
        fr_env(v);
        js_run_timers(v, bd, 1);
        v->throwing = 0;
        if (v->now > now) now = v->now;
        fr_sync(fr);
        for (uint32_t k = 0; k <= fr->n; k++) {
            js_vm *w = fr->vm[k];
            if (w && w->host && w->host->ready && k != (uint32_t)best) { fr_env(w); sh_checkpoint(w); w->throwing = 0; }
        }
    }
    for (uint32_t k = 0; k <= fr->n; k++) {
        js_vm *v = fr->vm[k];
        if (v && v->host && v->host->ready) script_timers_record(v);
    }
    if (fr->vm[0] && fr->vm[0]->host) fr_env(fr->vm[0]);
}

void script_console_note(js_vm *vm, uint32_t level, const char *text) {
    struct js_host *h = vm->host;
    if (!h || !h->rt) return;
    amb_span sp = text_add(h->rt, text, strlen(text));
    script_console_h c = console_new(h->rt);
    h->rt->console[c.index].run = 0;
    h->rt->console[c.index].level = level <= 4 ? (script_level)level : SCRIPT_LEVEL_LOG;
    h->rt->console[c.index].text = sp;
    if (h->fr) {
        if (c.index >= len(h->cseq)) resize(h->cseq, (size_t)c.index + 1, 0u);
        h->cseq[c.index] = ++h->fr->seq;
    }
}
