/* src/js/proxy.c -- Proxy and Reflect (ECMA-262 28.1 and 28.2), and the weak collections.

   A proxy is an object of kind JK_PROXY: its target in a, its handler in b (0 once it
   is revoked), and in c whether it can be called. Its every internal method is its
   handler's trap when the handler has one, else the target's own: src/js/vm.c's
   property paths (get, set, has, delete, the own keys and properties) and its calls
   come here for a proxy, as they go to the host for a host object. A proxy's shape is
   one no ordinary object can have (it holds a key no script can name), and a proxy is
   never cached, so no inline cache answers for it. The traps' invariants (a
   non-configurable property's value reported as the target holds it, and their like)
   are not checked.

   Reflect's functions are the internal methods themselves, for any object.

   WeakMap, WeakSet and WeakRef hold their objects strongly: the engine has no collector
   yet, so nothing they hold is ever collected; FinalizationRegistry's callbacks never
   run. Their keys are checked as the standard says. */
#include <stdlib.h>
#include <math.h>
#include "js.h"

/* a key as the value a trap is given: the atom's string, or the symbol */
static jv px_key_value(uint32_t key) {
    if (js_key_is_sym(key)) return jv_sym(key & ~JS_SYM_KEY);
    return jv_str(key);
}

/* the trap name of p's handler: the function, JV_UNDEF for none, JV_HOLE with a TypeError
   thrown when p was revoked or the trap is not a function */
static jv px_trap(js_vm *vm, uint32_t p, const char *name) {
    uint32_t h = vm->objs[p].b;
    if (!h) {
        char m[120];
        snprintf(m, sizeof m, "Cannot perform '%s' on a proxy that has been revoked", name);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    jv t = js_get(vm, jv_obj(h), js_atom_ascii(vm, name));
    if (vm->throwing) return JV_HOLE;
    if (jv_is_nullish(t)) return JV_UNDEF;
    if (!js_is_callable(vm, t)) {
        char m[120];
        snprintf(m, sizeof m, "the proxy's handler's %s is not a function", name);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    return t;
}

static jv px_ordinary_get(js_vm *vm, uint32_t o, uint32_t key, jv receiver);
static int px_ordinary_set(js_vm *vm, uint32_t o, uint32_t key, jv v, jv receiver);

static jv px_call3(js_vm *vm, jv trap, uint32_t p, jv a, jv b, jv c, uint32_t n) {
    jv av[4] = { jv_obj(vm->objs[p].a), a, b, c };
    return js_call(vm, trap, jv_obj(vm->objs[p].b), av, n + 1);
}

jv js_proxy_get(js_vm *vm, uint32_t p, uint32_t key, jv receiver) {
    jv t = px_trap(vm, p, "get");
    if (t == JV_HOLE) return JV_HOLE;
    if (t == JV_UNDEF) return px_ordinary_get(vm, vm->objs[p].a, key, receiver);
    return px_call3(vm, t, p, px_key_value(key), receiver, JV_UNDEF, 2);
}

int js_proxy_set(js_vm *vm, uint32_t p, uint32_t key, jv v, jv receiver) {
    jv t = px_trap(vm, p, "set");
    if (t == JV_HOLE) return 0;
    if (t == JV_UNDEF) return px_ordinary_set(vm, vm->objs[p].a, key, v, receiver);
    jv r = px_call3(vm, t, p, px_key_value(key), v, receiver, 3);
    return !vm->throwing && js_truthy(vm, r);
}

int js_proxy_has(js_vm *vm, uint32_t p, uint32_t key) {
    jv t = px_trap(vm, p, "has");
    if (t == JV_HOLE) return 0;
    if (t == JV_UNDEF) return js_has(vm, vm->objs[p].a, key);
    jv r = px_call3(vm, t, p, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    return !vm->throwing && js_truthy(vm, r);
}

int js_proxy_delete(js_vm *vm, uint32_t p, uint32_t key) {
    jv t = px_trap(vm, p, "deleteProperty");
    if (t == JV_HOLE) return 0;
    if (t == JV_UNDEF) return js_delete(vm, vm->objs[p].a, key);
    jv r = px_call3(vm, t, p, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    return !vm->throwing && js_truthy(vm, r);
}

/* a descriptor object's property as a value and flags (js_get_own's): an accessor's pair
   made, a data property's value, enumerable or hidden */
static int px_from_desc(js_vm *vm, jv d, jv *v, uint32_t *flags) {
    if (!jv_is_obj(d)) return 0;
    jv en = js_get(vm, d, A_ENUMERABLE);
    if (vm->throwing) return 0;
    *flags = js_truthy(vm, en) ? 0 : JS_SHAPE_HIDDEN;
    int has_get = js_has(vm, jv_idx(d), A_GET), has_set = js_has(vm, jv_idx(d), A_SET);
    if (has_get || has_set) {
        jv g = js_get(vm, d, A_GET), s = js_get(vm, d, A_SET);
        if (vm->throwing) return 0;
        uint32_t acc = js_obj_new(vm, JK_ACCESSOR, 0);
        vm->objs[acc].x = jv_is_obj(g) ? g : JV_UNDEF;
        vm->objs[acc].a = jv_is_obj(s) ? jv_idx(s) : 0;
        *v = jv_obj(acc);
        *flags |= JS_SHAPE_ACCESSOR;
        return 1;
    }
    *v = js_get(vm, d, A_VALUE);
    return !vm->throwing;
}

int js_proxy_get_own(js_vm *vm, uint32_t p, uint32_t key, jv *v, uint32_t *flags) {
    jv t = px_trap(vm, p, "getOwnPropertyDescriptor");
    if (t == JV_HOLE) return 0;
    if (t == JV_UNDEF) return js_get_own(vm, vm->objs[p].a, key, v, flags);
    jv d = px_call3(vm, t, p, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    if (vm->throwing) return 0;
    if (d == JV_UNDEF) return 0;
    if (!jv_is_obj(d)) { js_throw_msg(vm, vm->type_error_proto, "getOwnPropertyDescriptor's trap gave neither an object nor undefined"); return 0; }
    return px_from_desc(vm, d, v, flags);
}

/* the proxy's own keys onto arr: its strings (strings), its symbols (symbols), only the
   enumerable ones when asked */
void js_proxy_own_keys(js_vm *vm, uint32_t p, int strings, int symbols, int enumerable_only, uint32_t arr) {
    jv t = px_trap(vm, p, "ownKeys");
    if (t == JV_HOLE) return;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) {
        if (strings) js_own_keys(vm, target, enumerable_only, arr);
        if (symbols) js_own_symbols(vm, target, arr);
        return;
    }
    jv r = px_call3(vm, t, p, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
    if (vm->throwing) return;
    if (!jv_is_obj(r)) { js_throw_msg(vm, vm->type_error_proto, "ownKeys's trap gave no object"); return; }
    jv lv = js_get(vm, r, A_LENGTH);
    if (vm->throwing) return;
    double n = js_to_number(vm, lv);
    if (vm->throwing || n != n) return;
    for (uint32_t i = 0; i < (uint32_t)n && !vm->throwing; i++) {
        jv k = js_get_value(vm, r, jv_num(i));
        if (vm->throwing) return;
        if (!jv_is_str(k) && !jv_is_sym(k)) { js_throw_msg(vm, vm->type_error_proto, "ownKeys's trap gave a key that is neither a string nor a symbol"); return; }
        if (jv_is_str(k) ? !strings : !symbols) continue;
        if (enumerable_only) {
            uint32_t key = jv_is_sym(k) ? (JS_SYM_KEY | jv_idx(k)) : js_atom(vm, jv_idx(k));
            jv v;
            uint32_t f = 0;
            if (!js_proxy_get_own(vm, p, key, &v, &f) || (f & JS_SHAPE_HIDDEN)) continue;
        }
        js_array_push(vm, arr, k);
    }
}

/* the proxy's prototype: an object, or JV_NULL */
jv js_proxy_get_proto(js_vm *vm, uint32_t p) {
    jv t = px_trap(vm, p, "getPrototypeOf");
    if (t == JV_HOLE) return JV_HOLE;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) {
        if (vm->objs[target].kind == JK_PROXY) return js_proxy_get_proto(vm, target);
        uint32_t pr = vm->objs[target].proto;
        return pr ? jv_obj(pr) : JV_NULL;
    }
    jv r = px_call3(vm, t, p, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
    if (vm->throwing) return JV_HOLE;
    if (!jv_is_obj(r) && r != JV_NULL) return js_throw_msg(vm, vm->type_error_proto, "getPrototypeOf's trap gave neither an object nor null");
    return r;
}

/* a call (newt JV_UNDEF) or a construction of the proxy, its arguments the argc values
   of the stack from args */
jv js_proxy_call(js_vm *vm, uint32_t p, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    int construct = newt != JV_UNDEF;
    jv t = px_trap(vm, p, construct ? "construct" : "apply");
    if (t == JV_HOLE) return JV_HOLE;
    uint32_t target = vm->objs[p].a;
    vector<jv> av;
    for (uint32_t i = 0; i < argc; i++) push_back(av, js_arg(vm, args, argc, i));
    if (t == JV_UNDEF) {
        if (construct) return js_construct(vm, jv_obj(target), data(av), (uint32_t)len(av), newt == jv_obj(p) ? jv_obj(target) : newt);
        return js_call(vm, jv_obj(target), thisv, data(av), (uint32_t)len(av));
    }
    uint32_t arr = js_array_new(vm, 0);
    for (size_t i = 0; i < len(av); i++) js_array_push(vm, arr, av[i]);
    if (construct) {
        jv r = px_call3(vm, t, p, jv_obj(arr), newt, JV_UNDEF, 2);
        if (vm->throwing) return JV_HOLE;
        if (!jv_is_obj(r)) return js_throw_msg(vm, vm->type_error_proto, "construct's trap gave no object");
        return r;
    }
    return px_call3(vm, t, p, thisv == JV_HOLE ? JV_UNDEF : thisv, jv_obj(arr), JV_UNDEF, 2);
}

/* ---- the natives ---- */

/* a value as a key for Reflect: an atom or a symbol's key; 0 with ok cleared when its conversion threw */
static uint32_t px_key(js_vm *vm, jv v, int *ok) { return js_prop_key(vm, v, ok); }

static jv px_desc_object(js_vm *vm, jv v, uint32_t flags) {
    uint32_t d = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    if (flags & JS_SHAPE_ACCESSOR) {
        js_define(vm, d, A_GET, vm->objs[jv_idx(v)].x, 0);
        js_define(vm, d, A_SET, vm->objs[jv_idx(v)].a ? jv_obj(vm->objs[jv_idx(v)].a) : JV_UNDEF, 0);
    } else {
        js_define(vm, d, A_VALUE, v, 0);
        js_define(vm, d, A_WRITABLE, JV_TRUE, 0);
    }
    js_define(vm, d, A_ENUMERABLE, jv_bool(!(flags & JS_SHAPE_HIDDEN)), 0);
    js_define(vm, d, A_CONFIGURABLE, JV_TRUE, 0);
    return jv_obj(d);
}

/* an object's own property: js_get_own's, or the proxy's */
static int px_get_own(js_vm *vm, uint32_t o, uint32_t key, jv *v, uint32_t *f) {
    if (vm->objs[o].kind == JK_PROXY) return js_proxy_get_own(vm, o, key, v, f);
    if (js_get_own(vm, o, key, v, f)) return 1;
    /* an array's or a string's element, an array's length; a function's name, length and
       prototype, which it makes at their first reading */
    uint32_t k = vm->objs[o].kind, i;
    if ((k == JK_CLOSURE || k == JK_NATIVE || k == JK_BOUND) && (key == A_NAME || key == A_LENGTH || key == A_PROTOTYPE)) {
        jv x = js_get(vm, jv_obj(o), key);
        if (vm->throwing || x == JV_UNDEF) return 0;
        *v = x;
        *f = JS_SHAPE_HIDDEN;
        return 1;
    }
    if (k == JK_ARRAY && key == A_LENGTH) { *v = jv_num(vm->objs[o].b); *f = JS_SHAPE_HIDDEN; return 1; }
    if ((k == JK_ARRAY || k == JK_STRING) && js_atom_index(vm, key, &i)) {
        jv x = js_get(vm, jv_obj(o), key);
        if (vm->throwing || x == JV_UNDEF) return 0;
        *v = x;
        *f = 0;
        return 1;
    }
    return 0;
}

/* the keys an object owns, strings then symbols (Reflect.ownKeys) */
static uint32_t px_own_keys_all(js_vm *vm, uint32_t o) {
    uint32_t arr = js_array_new(vm, 0);
    if (vm->objs[o].kind == JK_PROXY) { js_proxy_own_keys(vm, o, 1, 1, 0, arr); return arr; }
    js_own_keys(vm, o, 0, arr);
    if (vm->objs[o].kind == JK_ARRAY) js_array_push(vm, arr, jv_str(A_LENGTH));
    js_own_symbols(vm, o, arr);
    return arr;
}

/* [[Get]] of an ordinary object (or a proxy) with a receiver of its own: the property found
   up the prototypes, a getter called with the receiver as this */
static jv px_ordinary_get(js_vm *vm, uint32_t o, uint32_t key, jv receiver) {
    if (receiver == jv_obj(o) && vm->objs[o].kind != JK_PROXY) return js_get(vm, receiver, key);
    for (uint32_t x = o, d = 0; x && d < 10000; d++) {
        if (vm->objs[x].kind == JK_PROXY) return js_proxy_get(vm, x, key, receiver);
        jv v;
        uint32_t f = 0;
        if (px_get_own(vm, x, key, &v, &f)) {
            if (f & JS_SHAPE_ACCESSOR) {
                jv g = vm->objs[jv_idx(v)].x;
                return js_is_callable(vm, g) ? js_call(vm, g, receiver, 0, 0) : JV_UNDEF;
            }
            return v;
        }
        if (vm->throwing) return JV_HOLE;
        x = vm->objs[x].proto;
    }
    return JV_UNDEF;
}

/* a property defined as a descriptor object says, on o or through its defineProperty
   trap: 1 done, 0 refused (or thrown) */
static int px_define(js_vm *vm, uint32_t o, uint32_t key, jv desc) {
    if (vm->objs[o].kind == JK_PROXY) {
        jv t = px_trap(vm, o, "defineProperty");
        if (t == JV_HOLE) return 0;
        if (t != JV_UNDEF) {
            jv r = px_call3(vm, t, o, px_key_value(key), desc, JV_UNDEF, 2);
            return !vm->throwing && js_truthy(vm, r);
        }
        return px_define(vm, vm->objs[o].a, key, desc);
    }
    jv cur;
    uint32_t cf = 0;
    int exists = px_get_own(vm, o, key, &cur, &cf);
    if (vm->throwing) return 0;
    if (!exists && !(vm->objs[o].flags & JO_EXTENSIBLE)) return 0;
    if (vm->objs[o].flags & JO_FROZEN) return 0;
    jv v = JV_UNDEF;
    uint32_t f = 0;
    if (!px_from_desc(vm, desc, &v, &f)) return 0;
    /* a descriptor without enumerable keeps an existing property's */
    if (exists && !js_has(vm, jv_idx(desc), A_ENUMERABLE)) f = (f & ~(uint32_t)JS_SHAPE_HIDDEN) | (cf & JS_SHAPE_HIDDEN);
    if (f & JS_SHAPE_ACCESSOR) {
        jv g = vm->objs[jv_idx(v)].x;
        uint32_t s = vm->objs[jv_idx(v)].a;
        js_define_accessor(vm, o, key, g == JV_UNDEF ? JV_HOLE : g, s ? jv_obj(s) : JV_HOLE, f & JS_SHAPE_HIDDEN);
    } else if (vm->objs[o].kind == JK_ARRAY) js_set(vm, jv_obj(o), key, v);
    else js_define(vm, o, key, v, f & JS_SHAPE_HIDDEN);
    return !vm->throwing;
}

/* [[Set]] of an ordinary object with a receiver: a setter up the prototypes called with the
   receiver as this; else the receiver's own data property set, or made (through its
   getOwnPropertyDescriptor and defineProperty traps when it is a proxy) */
static int px_ordinary_set(js_vm *vm, uint32_t o, uint32_t key, jv v, jv receiver) {
    if (receiver == jv_obj(o) && vm->objs[o].kind != JK_PROXY) return js_set(vm, receiver, key, v);
    for (uint32_t x = o, d = 0; x && d < 10000; d++) {
        if (vm->objs[x].kind == JK_PROXY) return js_proxy_set(vm, x, key, v, receiver);
        jv cur;
        uint32_t f = 0;
        if (px_get_own(vm, x, key, &cur, &f)) {
            if (f & JS_SHAPE_ACCESSOR) {
                uint32_t s = vm->objs[jv_idx(cur)].a;
                if (!s) return 0;
                jv a1[1] = { v };
                js_call(vm, jv_obj(s), receiver, a1, 1);
                return !vm->throwing;
            }
            if (vm->objs[x].flags & JO_FROZEN) return 0;
            break;
        }
        if (vm->throwing) return 0;
        x = vm->objs[x].proto;
    }
    if (!jv_is_obj(receiver)) return 0;
    uint32_t r = jv_idx(receiver);
    jv ex;
    uint32_t ef = 0;
    int has = px_get_own(vm, r, key, &ex, &ef);
    if (vm->throwing) return 0;
    if (has && (ef & JS_SHAPE_ACCESSOR)) return 0;
    uint32_t d = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, d, A_VALUE, v, 0);
    if (!has) {
        js_define(vm, d, A_WRITABLE, JV_TRUE, 0);
        js_define(vm, d, A_ENUMERABLE, JV_TRUE, 0);
        js_define(vm, d, A_CONFIGURABLE, JV_TRUE, 0);
    }
    return px_define(vm, r, key, jv_obj(d));
}

/* the argument array-like as a C vector of values */
static int px_list(js_vm *vm, jv a, vector<jv> *out) {
    if (!jv_is_obj(a)) { js_throw_msg(vm, vm->type_error_proto, "CreateListFromArrayLike called on non-object"); return 0; }
    jv lv = js_get(vm, a, A_LENGTH);
    if (vm->throwing) return 0;
    double n = js_to_number(vm, lv);
    if (vm->throwing) return 0;
    if (n != n || n < 0) n = 0;
    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        jv v = js_get_value(vm, a, jv_num(i));
        if (vm->throwing) return 0;
        push_back(*out, v);
    }
    return 1;
}

/* a weak collection's key: an object, or a symbol no registry holds */
static int px_weak_key(js_vm *vm, jv k) {
    if (jv_is_obj(k)) return 1;
    if (jv_is_sym(k)) return vm->syms[jv_idx(k)].reg == 0;
    return 0;
}

jv js_proxy_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    (void)callee;
    if (which >= JPX_R_APPLY && which <= JPX_R_SETPROTO && which != JPX_R_APPLY && which != JPX_R_CONSTRUCT && !jv_is_obj(a0))
        return js_throw_msg(vm, vm->type_error_proto, "Reflect's function called on a value that is not an object");
    switch (which) {
    case JPX_PROXY: case JPX_REVOCABLE: {
        if (which == JPX_PROXY && (newt == JV_UNDEF || newt == JV_HOLE)) return js_throw_msg(vm, vm->type_error_proto, "Constructor Proxy requires 'new'");
        if (!jv_is_obj(a0) || !jv_is_obj(a1)) return js_throw_msg(vm, vm->type_error_proto, "Cannot create proxy with a non-object as target or handler");
        uint32_t p = js_obj_new(vm, JK_PROXY, 0);
        vm->objs[p].a = jv_idx(a0);
        vm->objs[p].b = jv_idx(a1);
        vm->objs[p].c = (uint32_t)js_is_callable(vm, a0);
        /* a key no script can name (the symbol that is none): a shape no ordinary object has */
        js_define(vm, p, JS_SYM_KEY, JV_UNDEF, JS_SHAPE_HIDDEN);
        if (which == JPX_PROXY) return jv_obj(p);
        uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        js_define(vm, r, js_atom_ascii(vm, "proxy"), jv_obj(p), 0);
        uint32_t rv = js_native_new(vm, vm->proxy_first + JPX_REVOKE, A_EMPTY, 0);
        vm->objs[rv].x = jv_obj(p);
        js_define(vm, r, js_atom_ascii(vm, "revoke"), jv_obj(rv), 0);
        return jv_obj(r);
    }
    case JPX_REVOKE: {
        jv pv = vm->objs[jv_idx(callee)].x;
        if (jv_is_obj(pv) && vm->objs[jv_idx(pv)].kind == JK_PROXY) { vm->objs[jv_idx(pv)].b = 0; vm->objs[jv_idx(callee)].x = JV_UNDEF; }
        return JV_UNDEF;
    }
    case JPX_R_APPLY: {
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        vector<jv> av;
        if (!px_list(vm, a2, &av)) return JV_HOLE;
        return js_call(vm, a0, a1, data(av), (uint32_t)len(av));
    }
    case JPX_R_CONSTRUCT: {
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a constructor", js_typeof(vm, a0));
        jv nt = argc > 2 ? a2 : a0;
        if (!js_is_callable(vm, nt)) return js_type_error(vm, "is not a constructor", js_typeof(vm, nt));
        vector<jv> av;
        if (!px_list(vm, a1, &av)) return JV_HOLE;
        return js_construct(vm, a0, data(av), (uint32_t)len(av), nt);
    }
    case JPX_R_DEFINE: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        if (!jv_is_obj(a2)) return js_throw_msg(vm, vm->type_error_proto, "Property description must be an object");
        int r = px_define(vm, jv_idx(a0), key, a2);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_DELETE: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        int r = js_delete(vm, jv_idx(a0), key);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_GET: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        /* a receiver of its own: the getter's this */
        uint32_t o = jv_idx(a0);
        jv recv = argc > 2 ? a2 : a0;
        if (vm->objs[o].kind == JK_PROXY) return js_proxy_get(vm, o, key, recv);
        return px_ordinary_get(vm, o, key, recv);
    }
    case JPX_R_GETOWNDESC: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        jv v = JV_UNDEF;
        uint32_t f = 0;
        if (!px_get_own(vm, jv_idx(a0), key, &v, &f)) return vm->throwing ? JV_HOLE : JV_UNDEF;
        return px_desc_object(vm, v, f);
    }
    case JPX_R_GETPROTO: {
        uint32_t o = jv_idx(a0);
        if (vm->objs[o].kind == JK_PROXY) return js_proxy_get_proto(vm, o);
        return vm->objs[o].proto ? jv_obj(vm->objs[o].proto) : JV_NULL;
    }
    case JPX_R_HAS: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        int r = js_has(vm, jv_idx(a0), key);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_ISEXTENSIBLE: {
        uint32_t o = jv_idx(a0);
        if (vm->objs[o].kind == JK_PROXY) {
            jv t = px_trap(vm, o, "isExtensible");
            if (t == JV_HOLE) return JV_HOLE;
            if (t != JV_UNDEF) { jv r = px_call3(vm, t, o, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0); return vm->throwing ? JV_HOLE : jv_bool(js_truthy(vm, r)); }
            o = vm->objs[o].a;
        }
        return jv_bool((vm->objs[o].flags & JO_EXTENSIBLE) != 0);
    }
    case JPX_R_OWNKEYS: return jv_obj(px_own_keys_all(vm, jv_idx(a0)));
    case JPX_R_PREVENTEXT: {
        uint32_t o = jv_idx(a0);
        if (vm->objs[o].kind == JK_PROXY) {
            jv t = px_trap(vm, o, "preventExtensions");
            if (t == JV_HOLE) return JV_HOLE;
            if (t != JV_UNDEF) { jv r = px_call3(vm, t, o, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0); return vm->throwing ? JV_HOLE : jv_bool(js_truthy(vm, r)); }
            o = vm->objs[o].a;
        }
        vm->objs[o].flags &= (uint16_t)~JO_EXTENSIBLE;
        return JV_TRUE;
    }
    case JPX_R_SET: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        uint32_t o = jv_idx(a0);
        jv recv = argc > 3 ? js_arg(vm, args, argc, 3) : a0;
        int r;
        if (vm->objs[o].kind == JK_PROXY) r = js_proxy_set(vm, o, key, a2, recv);
        else if (recv == a0) {
            /* the plain set, refused where the object takes no new property or is frozen */
            if (vm->objs[o].flags & JO_FROZEN) return JV_FALSE;
            if (!(vm->objs[o].flags & JO_EXTENSIBLE)) {
                jv v;
                uint32_t f;
                if (!px_get_own(vm, o, key, &v, &f)) return JV_FALSE;
            }
            r = js_set(vm, a0, key, a2);
        } else r = px_ordinary_set(vm, o, key, a2, recv);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_SETPROTO: {
        if (!jv_is_obj(a1) && a1 != JV_NULL) return js_throw_msg(vm, vm->type_error_proto, "Object prototype may only be an Object or null");
        uint32_t o = jv_idx(a0);
        if (vm->objs[o].kind == JK_PROXY) {
            jv t = px_trap(vm, o, "setPrototypeOf");
            if (t == JV_HOLE) return JV_HOLE;
            if (t != JV_UNDEF) { jv r = px_call3(vm, t, o, a1, JV_UNDEF, JV_UNDEF, 1); return vm->throwing ? JV_HOLE : jv_bool(js_truthy(vm, r)); }
            o = vm->objs[o].a;
        }
        uint32_t np = jv_is_obj(a1) ? jv_idx(a1) : 0;
        /* no cycle, and no new prototype for an object that cannot be extended */
        for (uint32_t x = np, d = 0; x && d < 10000; x = vm->objs[x].proto, d++) if (x == o) return JV_FALSE;
        if (!(vm->objs[o].flags & JO_EXTENSIBLE)) return jv_bool(vm->objs[o].proto == np);
        js_set_proto(vm, o, np);
        return JV_TRUE;
    }
    /* ---- the weak collections ---- */
    case JPX_WEAKMAP: case JPX_WEAKSET: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, which == JPX_WEAKMAP ? "Constructor WeakMap requires 'new'" : "Constructor WeakSet requires 'new'");
        uint32_t m = js_coll_new(vm, which == JPX_WEAKMAP ? JK_MAP : JK_SET, which == JPX_WEAKMAP ? vm->weakmap_proto : vm->weakset_proto);
        vm->objs[m].flags |= JO_WEAK;
        jv mv = jv_obj(m);
        if (!jv_is_nullish(a0)) {
            /* the entries (or values) of the iterable, each added */
            jv it = js_iter_of(vm, a0);
            if (vm->throwing) return JV_HOLE;
            jv x;
            while (js_iter_step(vm, jv_idx(it), &x)) {
                jv k = x, v = JV_UNDEF;
                if (which == JPX_WEAKMAP) {
                    if (!jv_is_obj(x)) { js_iter_close(vm, jv_idx(it)); return js_throw_msg(vm, vm->type_error_proto, "Iterator value is not an entry object"); }
                    k = js_get_value(vm, x, jv_num(0));
                    v = js_get_value(vm, x, jv_num(1));
                    if (vm->throwing) return JV_HOLE;
                }
                if (!px_weak_key(vm, k)) { js_iter_close(vm, jv_idx(it)); return js_throw_msg(vm, vm->type_error_proto, which == JPX_WEAKMAP ? "Invalid value used as weak map key" : "Invalid value used in weak set"); }
                js_coll_op(vm, which == JPX_WEAKMAP ? JCOLL_SET : JCOLL_ADD, m, k, v);
            }
            if (vm->throwing) return JV_HOLE;
        }
        return mv;
    }
    case JPX_WM_GET: case JPX_WM_SET: case JPX_WM_HAS: case JPX_WM_DELETE: case JPX_WS_ADD: {
        uint32_t want = which == JPX_WM_GET || which == JPX_WM_SET ? JK_MAP : which == JPX_WS_ADD ? JK_SET : 0;
        if (!jv_is_obj(thisv) || !(vm->objs[jv_idx(thisv)].flags & JO_WEAK) || (want && vm->objs[jv_idx(thisv)].kind != want) ||
            (vm->objs[jv_idx(thisv)].kind != JK_MAP && vm->objs[jv_idx(thisv)].kind != JK_SET))
            return js_throw_msg(vm, vm->type_error_proto, "a WeakMap's or a WeakSet's method called on another object");
        if (!px_weak_key(vm, a0)) {
            if (which == JPX_WM_SET) return js_throw_msg(vm, vm->type_error_proto, "Invalid value used as weak map key");
            if (which == JPX_WS_ADD) return js_throw_msg(vm, vm->type_error_proto, "Invalid value used in weak set");
            return which == JPX_WM_GET ? JV_UNDEF : JV_FALSE;
        }
        static const uint32_t ops[] = { JCOLL_GET, JCOLL_SET, JCOLL_HAS, JCOLL_DELETE, JCOLL_ADD };
        jv r = js_coll_op(vm, ops[which - JPX_WM_GET], jv_idx(thisv), a0, a1);
        if (vm->throwing) return JV_HOLE;
        return (which == JPX_WM_SET || which == JPX_WS_ADD) ? thisv : r;
    }
    case JPX_WEAKREF: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Constructor WeakRef requires 'new'");
        if (!px_weak_key(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "WeakRef: invalid target");
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->weakref_proto);
        vm->objs[o].x = a0;
        vm->objs[o].flags |= JO_WEAK;
        return jv_obj(o);
    }
    case JPX_WR_DEREF:
        if (!jv_is_obj(thisv) || !(vm->objs[jv_idx(thisv)].flags & JO_WEAK) || vm->objs[jv_idx(thisv)].proto != vm->weakref_proto)
            return js_throw_msg(vm, vm->type_error_proto, "WeakRef.prototype.deref called on an object that is not a WeakRef");
        return vm->objs[jv_idx(thisv)].x;
    case JPX_FINREG: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Constructor FinalizationRegistry requires 'new'");
        if (!js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "FinalizationRegistry: cleanup must be callable");
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->finreg_proto);
        vm->objs[o].x = a0;
        return jv_obj(o);
    }
    case JPX_FR_REGISTER:
        if (!px_weak_key(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "FinalizationRegistry.prototype.register: invalid target");
        return JV_UNDEF;
    case JPX_FR_UNREGISTER:
        if (!px_weak_key(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "Invalid unregisterToken");
        return JV_FALSE;
    default: return JV_UNDEF;
    }
}

static void px_def(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t nargs) {
    uint32_t atom = js_atom_ascii(vm, name);
    js_define(vm, o, atom, jv_obj(js_native_new(vm, id, atom, nargs)), JS_SHAPE_HIDDEN);
}
static uint32_t px_ctor(js_vm *vm, const char *name, uint32_t id, uint32_t nargs, uint32_t proto) {
    uint32_t atom = js_atom_ascii(vm, name);
    uint32_t c = js_native_new(vm, id, atom, nargs);
    if (proto) {
        js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_HIDDEN);
        js_define(vm, proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    }
    js_define(vm, vm->global, atom, jv_obj(c), JS_SHAPE_HIDDEN);
    return c;
}
static void px_tag(js_vm *vm, uint32_t proto, const char *tag) {
    js_define(vm, proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, tag)), JS_SHAPE_HIDDEN);
}

/* Proxy, Reflect and the weak collections; first, the native id of JPX_PROXY */
void js_proxy_init(js_vm *vm, uint32_t first) {
    vm->proxy_first = first;
    uint32_t px = px_ctor(vm, "Proxy", first + JPX_PROXY, 2, 0);
    px_def(vm, px, "revocable", first + JPX_REVOCABLE, 2);
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, vm->global, js_atom_ascii(vm, "Reflect"), jv_obj(r), JS_SHAPE_HIDDEN);
    px_tag(vm, r, "Reflect");
    static const char *const rn[] = { "apply", "construct", "defineProperty", "deleteProperty", "get", "getOwnPropertyDescriptor",
                                      "getPrototypeOf", "has", "isExtensible", "ownKeys", "preventExtensions", "set", "setPrototypeOf" };
    static const uint32_t ra[] = { 3, 2, 3, 2, 2, 2, 1, 2, 1, 1, 1, 3, 2 };
    for (uint32_t i = 0; i < 13; i++) px_def(vm, r, rn[i], first + JPX_R_APPLY + i, ra[i]);
    vm->weakmap_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->weakset_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->weakref_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->finreg_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    px_ctor(vm, "WeakMap", first + JPX_WEAKMAP, 0, vm->weakmap_proto);
    px_ctor(vm, "WeakSet", first + JPX_WEAKSET, 0, vm->weakset_proto);
    px_ctor(vm, "WeakRef", first + JPX_WEAKREF, 1, vm->weakref_proto);
    px_ctor(vm, "FinalizationRegistry", first + JPX_FINREG, 1, vm->finreg_proto);
    px_def(vm, vm->weakmap_proto, "get", first + JPX_WM_GET, 1);
    px_def(vm, vm->weakmap_proto, "set", first + JPX_WM_SET, 2);
    px_def(vm, vm->weakmap_proto, "has", first + JPX_WM_HAS, 1);
    px_def(vm, vm->weakmap_proto, "delete", first + JPX_WM_DELETE, 1);
    px_def(vm, vm->weakset_proto, "add", first + JPX_WS_ADD, 1);
    px_def(vm, vm->weakset_proto, "has", first + JPX_WM_HAS, 1);
    px_def(vm, vm->weakset_proto, "delete", first + JPX_WM_DELETE, 1);
    px_def(vm, vm->weakref_proto, "deref", first + JPX_WR_DEREF, 0);
    px_def(vm, vm->finreg_proto, "register", first + JPX_FR_REGISTER, 2);
    px_def(vm, vm->finreg_proto, "unregister", first + JPX_FR_UNREGISTER, 1);
    px_tag(vm, vm->weakmap_proto, "WeakMap");
    px_tag(vm, vm->weakset_proto, "WeakSet");
    px_tag(vm, vm->weakref_proto, "WeakRef");
    px_tag(vm, vm->finreg_proto, "FinalizationRegistry");
}
