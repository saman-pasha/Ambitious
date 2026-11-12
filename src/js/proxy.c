/* src/js/proxy.c -- Proxy and Reflect (ECMA-262 28.1 and 28.2), Object's functions over
   property descriptors, and the weak collections.

   A proxy is an object of kind JK_PROXY: its target in a, its handler in b (0 once it
   is revoked), and in c whether it can be called. Its every internal method is its
   handler's trap when the handler has one, else the target's own: src/js/vm.c's
   property paths (get, set, has, delete, the own keys and properties) and its calls
   come here for a proxy, as they go to the host for a host object. A proxy's shape is
   one no ordinary object can have (it holds a key no script can name), and a proxy is
   never cached, so no inline cache answers for it.

   Each trap's result is checked against its target as ECMA-262 10.5 says (a property
   the target holds non-configurable and non-writable reported as it is, a
   non-extensible target's keys and prototype reported whole, ownKeys' duplicates, and
   the rest), each failure a TypeError in V8's words. The checks read the target's own
   properties as descriptors: an ordinary object keeps writable and configurable in its
   shape's flags (JS_SHAPE_READONLY, JS_SHAPE_FIXED, which Object.defineProperty here
   sets), a frozen or sealed object (JO_FROZEN, JO_SEALED) has none of them writable or
   configurable, and the exotic objects' own (an array's length, a string's indices, a
   function's prototype, a typed array's elements) are what the standard gives them. The
   interpreter's own [[Set]] and [[Delete]] do not read those flags yet (src/js/vm.c).

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

static void px_value_text(js_vm *vm, jv v, vector<char> *out);

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
        /* GetMethod's error, V8's words: the value, the trap's name, the handler */
        vector<char> m;
        push_back(m, '\'');
        px_value_text(vm, t, &m);
        const char *a = "' returned for property '";
        append_range(m, a, strlen(a));
        append_range(m, name, strlen(name));
        const char *b = "' of object '";
        append_range(m, b, strlen(b));
        px_value_text(vm, jv_obj(h), &m);
        const char *c = "' is not a function";
        append_range(m, c, strlen(c));
        push_back(m, 0);
        return js_throw_msg(vm, vm->type_error_proto, data(m));
    }
    return t;
}

static jv px_call3(js_vm *vm, jv trap, uint32_t p, jv a, jv b, jv c, uint32_t n) {
    jv av[4] = { jv_obj(vm->objs[p].a), a, b, c };
    return js_call(vm, trap, jv_obj(vm->objs[p].b), av, n + 1);
}

/* SameValue */
static int px_same(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) {
        double x = jv_dbl(a), y = jv_dbl(b);
        if (x != x) return y != y;
        return x == y && signbit(x) == signbit(y);
    }
    return js_strict_eq(vm, a, b);
}

/* a key's text in V8's messages: the name, or Symbol(description) */
static void px_key_text(js_vm *vm, uint32_t key, vector<char> *out) { js_str_to_utf8(vm, js_key_name(vm, key), out); }
/* a value's text in V8's messages, without its methods run: a primitive's string, an object's #<its constructor> */
static void px_value_text(js_vm *vm, jv v, vector<char> *out) {
    if (!jv_is_obj(v)) {
        uint32_t s = jv_is_sym(v) ? js_sym_text(vm, jv_idx(v)) : js_to_string(vm, v);
        vm->throwing = 0;
        if (s) js_str_to_utf8(vm, s, out);
        return;
    }
    const char *pre = "#<";
    append_range(*out, pre, 2);
    jv c = js_get(vm, v, A_CONSTRUCTOR);
    jv nm = vm->throwing || !jv_is_obj(c) ? JV_UNDEF : js_get(vm, c, A_NAME);
    vm->throwing = 0;
    if (jv_is_str(nm) && vm->strs[jv_idx(nm)].len) js_str_to_utf8(vm, jv_idx(nm), out);
    else { const char *t = "Object"; append_range(*out, t, strlen(t)); }
    push_back(*out, '>');
}
/* a TypeError of V8's: pre, then key's text when key, mid, then a value's text when has_v, post */
static jv px_error(js_vm *vm, const char *pre, uint32_t key, int has_key, const char *mid, jv v, int has_v, const char *post) {
    vector<char> m;
    append_range(m, pre, strlen(pre));
    if (has_key) px_key_text(vm, key, &m);
    append_range(m, mid, strlen(mid));
    if (has_v) px_value_text(vm, v, &m);
    append_range(m, post, strlen(post));
    push_back(m, 0);
    return js_throw_msg(vm, vm->type_error_proto, data(m));
}

/* ---- property descriptors ---- */

/* a property descriptor: the fields it has (PD_), their values */
typedef struct px_desc { uint32_t has; jv value, get, set; uint32_t writable, enumerable, configurable; } px_desc;
enum { PD_VALUE = 1, PD_GET = 2, PD_SET = 4, PD_WRITABLE = 8, PD_ENUMERABLE = 16, PD_CONFIGURABLE = 32 };
static int pd_accessor(const px_desc *d) { return (d->has & (PD_GET | PD_SET)) != 0; }
static int pd_data(const px_desc *d) { return (d->has & (PD_VALUE | PD_WRITABLE)) != 0; }

/* ToPropertyDescriptor: 0 with a TypeError thrown */
static int px_to_desc(js_vm *vm, jv o, px_desc *d) {
    memset(d, 0, sizeof *d);
    d->value = d->get = d->set = JV_UNDEF;
    if (!jv_is_obj(o)) { px_error(vm, "Property description must be an object: ", 0, 0, "", o, 1, ""); return 0; }
    uint32_t ob = jv_idx(o);
    static const uint32_t fields[] = { A_ENUMERABLE, A_CONFIGURABLE, A_VALUE, A_WRITABLE, A_GET, A_SET };
    static const uint32_t bits[] = { PD_ENUMERABLE, PD_CONFIGURABLE, PD_VALUE, PD_WRITABLE, PD_GET, PD_SET };
    for (uint32_t k = 0; k < 6; k++) {
        int has = js_has(vm, ob, fields[k]);
        if (vm->throwing) return 0;
        if (!has) continue;
        jv v = js_get(vm, o, fields[k]);
        if (vm->throwing) return 0;
        d->has |= bits[k];
        if (k == 0) d->enumerable = (uint32_t)js_truthy(vm, v);
        else if (k == 1) d->configurable = (uint32_t)js_truthy(vm, v);
        else if (k == 2) d->value = v;
        else if (k == 3) d->writable = (uint32_t)js_truthy(vm, v);
        else {
            if (v != JV_UNDEF && !js_is_callable(vm, v)) { px_error(vm, k == 4 ? "Getter must be a function: " : "Setter must be a function: ", 0, 0, "", v, 1, ""); return 0; }
            if (k == 4) d->get = v;
            else d->set = v;
        }
    }
    if (pd_accessor(d) && pd_data(d)) {
        px_error(vm, "Invalid property descriptor. Cannot both specify accessors and a value or writable attribute, ", 0, 0, "", o, 1, "");
        return 0;
    }
    return 1;
}

/* FromPropertyDescriptor: its fields, in the standard's order */
static jv px_from_desc(js_vm *vm, const px_desc *d) {
    uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    if (d->has & PD_VALUE) js_define(vm, o, A_VALUE, d->value, 0);
    if (d->has & PD_WRITABLE) js_define(vm, o, A_WRITABLE, jv_bool(d->writable), 0);
    if (d->has & PD_GET) js_define(vm, o, A_GET, d->get, 0);
    if (d->has & PD_SET) js_define(vm, o, A_SET, d->set, 0);
    if (d->has & PD_ENUMERABLE) js_define(vm, o, A_ENUMERABLE, jv_bool(d->enumerable), 0);
    if (d->has & PD_CONFIGURABLE) js_define(vm, o, A_CONFIGURABLE, jv_bool(d->configurable), 0);
    return jv_obj(o);
}

/* CompletePropertyDescriptor */
static void px_complete(px_desc *d) {
    if (!pd_accessor(d)) {
        if (!(d->has & PD_VALUE)) d->value = JV_UNDEF;
        if (!(d->has & PD_WRITABLE)) d->writable = 0;
        d->has |= PD_VALUE | PD_WRITABLE;
    } else {
        if (!(d->has & PD_GET)) d->get = JV_UNDEF;
        if (!(d->has & PD_SET)) d->set = JV_UNDEF;
        d->has |= PD_GET | PD_SET;
    }
    if (!(d->has & PD_ENUMERABLE)) d->enumerable = 0;
    if (!(d->has & PD_CONFIGURABLE)) d->configurable = 0;
    d->has |= PD_ENUMERABLE | PD_CONFIGURABLE;
}

/* IsCompatiblePropertyDescriptor (ValidateAndApplyPropertyDescriptor's checks): may d be
   applied to the current property cur (none when !has_cur) of an object, extensible or not? */
static int px_compatible(js_vm *vm, int extensible, const px_desc *d, const px_desc *cur, int has_cur) {
    if (!has_cur) return extensible;
    if (!d->has) return 1;
    if (cur->configurable) return 1;
    if ((d->has & PD_CONFIGURABLE) && d->configurable) return 0;
    if ((d->has & PD_ENUMERABLE) && d->enumerable != cur->enumerable) return 0;
    int generic = !pd_accessor(d) && !pd_data(d);
    if (!generic && pd_accessor(d) != pd_accessor(cur)) return 0;
    if (pd_accessor(cur)) {
        if ((d->has & PD_GET) && !px_same(vm, d->get, cur->get)) return 0;
        if ((d->has & PD_SET) && !px_same(vm, d->set, cur->set)) return 0;
    } else if (!cur->writable) {
        if ((d->has & PD_WRITABLE) && d->writable) return 0;
        if ((d->has & PD_VALUE) && !px_same(vm, d->value, cur->value)) return 0;
    }
    return 1;
}

static int px_own(js_vm *vm, uint32_t o, uint32_t key, px_desc *d);
static int px_is_extensible(js_vm *vm, uint32_t o);

/* ---- the ordinary objects' own properties ---- */

/* an object's own property as the shape (or the kind) keeps it: its value, its flags */
static int px_get_own(js_vm *vm, uint32_t o, uint32_t key, jv *v, uint32_t *f) {
    if (js_get_own(vm, o, key, v, f)) return 1;
    if (vm->throwing) return 0;
    /* an array's or a string's element, an array's length; a function's name and length,
       which it makes as it is read, and its prototype, made at its first reading */
    uint32_t k = vm->objs[o].kind, i;
    if ((k == JK_CLOSURE || k == JK_NATIVE) && js_fn_virtual(vm, o, key)) {
        jv x;
        if (!js_fn_lazy(vm, o, key, &x) || vm->throwing) return 0;
        if (key == A_PROTOTYPE) return js_get_own(vm, o, key, v, f);   /* made, in the shape */
        *v = x;
        *f = JS_SHAPE_HIDDEN | JS_SHAPE_READONLY | (key == A_ARGUMENTS || key == A_CALLER ? JS_SHAPE_FIXED : 0);
        return 1;
    }
    if (k == JK_ARRAY && key == A_LENGTH) {
        *v = jv_num(vm->objs[o].b);
        *f = JS_SHAPE_HIDDEN | JS_SHAPE_FIXED | ((vm->objs[o].flags & JO_LENGTH_RO) ? JS_SHAPE_READONLY : 0);
        return 1;
    }
    if (k == JK_STRING && key == A_LENGTH) { *v = jv_num(vm->strs[jv_idx(vm->objs[o].x)].len); *f = JS_SHAPE_HIDDEN; return 1; }
    /* an array's or an arguments object's dense element (one with attributes of its own
       is its shape's, above): the default attributes */
    if ((k == JK_ARRAY || k == JK_ARGUMENTS) && js_atom_index(vm, key, &i)) {
        uint32_t st = k == JK_ARGUMENTS ? vm->objs[o].a : o;
        if (i >= vm->objs[st].b || vm->slots[vm->objs[st].a + i] == JV_HOLE) return 0;
        uint32_t cell = js_args_cell(vm, o, i);
        *v = cell ? vm->slots[cell - 1] : vm->slots[vm->objs[st].a + i];
        *f = 0;
        return 1;
    }
    if (k == JK_STRING && js_atom_index(vm, key, &i)) {
        if (i >= vm->strs[jv_idx(vm->objs[o].x)].len) return 0;
        *v = js_get(vm, jv_obj(o), key);
        *f = 0;
        return !vm->throwing;
    }
    return 0;
}

/* an array's or an arguments object's element i defined (ValidateAndApplyPropertyDescriptor
   over its element: ArrayDefineOwnProperty's step 3): kept dense while its attributes are
   the default ones, else in the object's shape with its own; an array's length past it.
   1 done, 0 refused, -1 thrown */
static int px_elem_define(js_vm *vm, uint32_t o, uint32_t i, uint32_t key, const px_desc *d);
/* an array's length defined (ArraySetLength) */
static int px_length_define(js_vm *vm, uint32_t o, const px_desc *d);

/* OrdinaryGetOwnProperty, with the exotic objects' own: 1 found, 0 none, -1 thrown */
static int px_ordinary_own(js_vm *vm, uint32_t o, uint32_t key, px_desc *d) {
    jv v;
    uint32_t f = 0;
    memset(d, 0, sizeof *d);
    d->value = d->get = d->set = JV_UNDEF;
    if (!px_get_own(vm, o, key, &v, &f)) return vm->throwing ? -1 : 0;
    uint32_t of = vm->objs[o].flags, k = vm->objs[o].kind, i = 0;
    int frozen = (of & JO_FROZEN) != 0, sealed = frozen || (of & JO_SEALED) != 0;
    if (f & JS_SHAPE_ACCESSOR) {
        d->has = PD_GET | PD_SET | PD_ENUMERABLE | PD_CONFIGURABLE;
        jv g = vm->objs[jv_idx(v)].x;
        d->get = js_is_callable(vm, g) ? g : JV_UNDEF;
        d->set = vm->objs[jv_idx(v)].a ? jv_obj(vm->objs[jv_idx(v)].a) : JV_UNDEF;
    } else {
        d->has = PD_VALUE | PD_WRITABLE | PD_ENUMERABLE | PD_CONFIGURABLE;
        d->value = v;
        d->writable = !(f & JS_SHAPE_READONLY) && !frozen;
    }
    d->enumerable = !(f & JS_SHAPE_HIDDEN);
    d->configurable = !(f & JS_SHAPE_FIXED) && !sealed;
    int index = js_atom_index(vm, key, &i);
    if (k == JK_ARRAY && key == A_LENGTH) { d->configurable = 0; d->enumerable = 0; }
    else if (k == JK_STRING && (key == A_LENGTH || index)) { d->writable = 0; d->configurable = 0; d->enumerable = index; }
    else if (k == JK_TYPEDARRAY && index) { d->writable = 1; d->enumerable = 1; d->configurable = 1; }
    return 1;
}

/* the object's own property key replaced: its flags and value (an accessor pair when the
   flags say so), the others' places kept. The shape is made again, the keys in their order */
static void px_reshape(js_vm *vm, uint32_t o, uint32_t key, jv nv, uint32_t nf) {
    vector<uint32_t> es;
    for (uint32_t x = vm->objs[o].shape; x && vm->shapes[x].count; x = vm->shapes[x].parent) push_back(es, x);
    vector<uint32_t> keys, flags;
    vector<jv> vals;
    for (size_t q = len(es); q > 0; q--) {
        uint32_t e = es[q - 1];
        jv v = vm->slots[vm->objs[o].slots + vm->shapes[e].slot];
        if (v == JV_HOLE) continue;
        push_back(keys, vm->shapes[e].atom);
        push_back(flags, vm->shapes[e].flags);
        push_back(vals, v);
    }
    vm->objs[o].shape = js_shape_of(vm, vm->objs[o].proto);
    vm->objs[o].cap = 0;
    for (size_t i = 0; i < len(keys); i++) {
        if (keys[i] == key) js_define(vm, o, key, nv, nf);
        else js_define(vm, o, keys[i], vals[i], flags[i]);
    }
}

/* is the key a CanonicalNumericIndexString ("-0", or a number's own string), *num its number? */
static int px_numeric_key(js_vm *vm, uint32_t key, double *num) {
    if (js_key_is_sym(key)) return 0;
    js_str_flatten(vm, key);
    uint32_t n = vm->strs[key].len, off = vm->strs[key].off;
    if (n == 2 && vm->units[off] == '-' && vm->units[off + 1] == '0') { *num = -0.0; return 1; }
    double d = js_str_to_num(vm, key);
    *num = d;
    return js_str_eq(vm, js_num_to_str(vm, d), key);
}
/* a typed array's numeric key that is no element of it (not an integer, -0, out of its bounds) */
static int px_bad_index(js_vm *vm, uint32_t o, uint32_t key) {
    double num = 0;
    if (vm->objs[o].kind != JK_TYPEDARRAY || !px_numeric_key(vm, key, &num)) return 0;
    return num != trunc(num) || (num == 0 && signbit(num)) || num < 0 || num >= (double)vm->objs[o].b;
}

/* OrdinaryDefineOwnProperty (ValidateAndApplyPropertyDescriptor): 1 done, 0 refused, -1 thrown */
static int px_ordinary_define(js_vm *vm, uint32_t o, uint32_t key, const px_desc *d) {
    uint32_t k = vm->objs[o].kind, i = 0;
    if (px_bad_index(vm, o, key)) return 0;
    if (k == JK_TYPEDARRAY && js_atom_index(vm, key, &i)) {
        /* a typed array's element: an index within it, a data property writable, enumerable, configurable */
        if (i >= vm->objs[o].b) return 0;
        if (((d->has & PD_CONFIGURABLE) && !d->configurable) || ((d->has & PD_ENUMERABLE) && !d->enumerable) || pd_accessor(d) ||
            ((d->has & PD_WRITABLE) && !d->writable)) return 0;
        if ((d->has & PD_VALUE) && !js_ta_set(vm, o, i, d->value)) return -1;
        return 1;
    }
    if (k == JK_ARRAY && key == A_LENGTH) return px_length_define(vm, o, d);
    if ((k == JK_ARRAY || k == JK_ARGUMENTS) && js_atom_index(vm, key, &i)) return px_elem_define(vm, o, i, key, d);
    px_desc cur;
    int has = px_ordinary_own(vm, o, key, &cur);
    if (has < 0) return -1;
    int ext = (vm->objs[o].flags & JO_EXTENSIBLE) != 0;
    if (!px_compatible(vm, ext, d, &cur, has)) return 0;
    if (k == JK_STRING && (key == A_LENGTH || js_atom_index(vm, key, &i)) && has) return 1;
    /* a sloppy function's arguments and caller: neither writable nor configurable, the engine's, so unchanged */
    if (has && (key == A_ARGUMENTS || key == A_CALLER) && js_fn_virtual(vm, o, key)) return 1;
    /* the property as it is to be: the descriptor's fields over the current's, else the defaults */
    int acc = pd_accessor(d) || (!pd_data(d) && has && pd_accessor(&cur));
    uint32_t enumerable = (d->has & PD_ENUMERABLE) ? d->enumerable : has ? cur.enumerable : 0;
    uint32_t configurable = (d->has & PD_CONFIGURABLE) ? d->configurable : has ? cur.configurable : 0;
    uint32_t nf = (enumerable ? 0 : JS_SHAPE_HIDDEN) | (configurable ? 0 : JS_SHAPE_FIXED);
    jv nv;
    if (acc) {
        jv g = (d->has & PD_GET) ? d->get : has && pd_accessor(&cur) ? cur.get : JV_UNDEF;
        jv s = (d->has & PD_SET) ? d->set : has && pd_accessor(&cur) ? cur.set : JV_UNDEF;
        uint32_t a = js_obj_new(vm, JK_ACCESSOR, 0);
        vm->objs[a].x = g;
        vm->objs[a].a = jv_is_obj(s) ? jv_idx(s) : 0;
        nv = jv_obj(a);
        nf |= JS_SHAPE_ACCESSOR;
    } else {
        uint32_t writable = (d->has & PD_WRITABLE) ? d->writable : has && pd_data(&cur) ? cur.writable : 0;
        nv = (d->has & PD_VALUE) ? d->value : has && pd_data(&cur) ? cur.value : JV_UNDEF;
        nf |= writable ? 0 : JS_SHAPE_READONLY;
    }
    /* a frozen or sealed object's flags are its own: they need not be in the shape */
    if (vm->objs[o].flags & (JO_FROZEN | JO_SEALED)) {
        if (!has) return 0;
        if (vm->objs[o].flags & JO_FROZEN) return 1;
    }
    jv old;
    uint32_t of = 0;
    int in_shape = js_get_own(vm, o, key, &old, &of);
    if (vm->throwing) return -1;
    if (!in_shape) { js_define(vm, o, key, nv, nf); return 1; }
    if (of == nf) {
        if (acc) {
            /* the pair in place: its getter and setter given */
            vm->objs[jv_idx(old)].x = vm->objs[jv_idx(nv)].x;
            vm->objs[jv_idx(old)].a = vm->objs[jv_idx(nv)].a;
        } else js_define(vm, o, key, nv, nf);
        return 1;
    }
    px_reshape(vm, o, key, nv, nf);
    return 1;
}

static int px_elem_define_in(js_vm *vm, uint32_t o, uint32_t i, uint32_t key, const px_desc *d);
static int px_elem_define(js_vm *vm, uint32_t o, uint32_t i, uint32_t key, const px_desc *d) {
    /* a mapped arguments object's element (10.4.4.2): made not writable without a value, its parameter's value; then,
       defined, its parameter given the value, and the element no longer mapped as an accessor or not writable */
    uint32_t cell = js_args_cell(vm, o, i);
    if (!cell) return px_elem_define_in(vm, o, i, key, d);
    px_desc nd = *d;
    if (pd_data(d) && !(d->has & PD_VALUE) && (d->has & PD_WRITABLE) && !d->writable) { nd.value = vm->slots[cell - 1]; nd.has |= PD_VALUE; }
    int r = px_elem_define_in(vm, o, i, key, &nd);
    if (r <= 0) return r;
    cell = js_args_cell(vm, o, i);
    if (!cell) return r;
    if (pd_accessor(d)) { vm->slots[vm->objs[vm->objs[o].c].a + i] = JV_UNDEF; return r; }
    if (d->has & PD_VALUE) vm->slots[cell - 1] = d->value;
    if ((d->has & PD_WRITABLE) && !d->writable) js_args_unmap(vm, o, i);
    return r;
}
static int px_elem_define_in(js_vm *vm, uint32_t o, uint32_t i, uint32_t key, const px_desc *d) {
    uint32_t fl = vm->objs[o].flags, st = vm->objs[o].kind == JK_ARGUMENTS ? vm->objs[o].a : o;
    int arr = vm->objs[o].kind == JK_ARRAY;
    /* past a length that is not writable: none (ArrayDefineOwnProperty 3.g) */
    if (arr && i >= vm->objs[o].b && (fl & (JO_LENGTH_RO | JO_FROZEN))) return 0;
    px_desc cur;
    int has = px_ordinary_own(vm, o, key, &cur);
    if (has < 0) return -1;
    if (!px_compatible(vm, (fl & JO_EXTENSIBLE) != 0, d, &cur, has)) return 0;
    /* frozen, and compatible with itself: nothing to change */
    if (fl & JO_FROZEN) return 1;
    int acc = pd_accessor(d) || (!pd_data(d) && has && pd_accessor(&cur));
    uint32_t enumerable = (d->has & PD_ENUMERABLE) ? d->enumerable : has ? cur.enumerable : 0;
    uint32_t configurable = (d->has & PD_CONFIGURABLE) ? d->configurable : has ? cur.configurable : 0;
    uint32_t writable = 0;
    jv nv;
    if (acc) {
        jv g = (d->has & PD_GET) ? d->get : has && pd_accessor(&cur) ? cur.get : JV_UNDEF;
        jv s = (d->has & PD_SET) ? d->set : has && pd_accessor(&cur) ? cur.set : JV_UNDEF;
        uint32_t a = js_obj_new(vm, JK_ACCESSOR, 0);
        vm->objs[a].x = g;
        vm->objs[a].a = jv_is_obj(s) ? jv_idx(s) : 0;
        nv = jv_obj(a);
    } else {
        writable = (d->has & PD_WRITABLE) ? d->writable : has && pd_data(&cur) ? cur.writable : 0;
        nv = (d->has & PD_VALUE) ? d->value : has && pd_data(&cur) ? cur.value : JV_UNDEF;
    }
    uint32_t ef = 0;
    int in_shape = 0;
    int entry = js_shape_entry(vm, o, key, &ef, &in_shape);
    /* the default attributes (a sealed object's non-configurable its flag's): a dense element */
    if (!acc && writable && enumerable && (configurable || (fl & JO_SEALED))) {
        if (in_shape) js_define(vm, o, key, JV_HOLE, ef);      /* no longer one of its own */
        if (i >= vm->objs[st].b) js_array_set(vm, st, i, nv);
        else vm->slots[vm->objs[st].a + i] = nv;
        return 1;
    }
    /* else its own attributes, in the shape: the dense slot a hole, an array's length past it */
    uint32_t nf = (enumerable ? 0 : JS_SHAPE_HIDDEN) | (configurable ? 0 : JS_SHAPE_FIXED) |
                  (acc ? JS_SHAPE_ACCESSOR : writable ? 0 : JS_SHAPE_READONLY);
    if (i < vm->objs[st].b) vm->slots[vm->objs[st].a + i] = JV_HOLE;
    else if (arr) js_array_set_length(vm, o, i + 1);
    vm->objs[o].flags |= JO_ELEMPROPS;
    if (!entry) { js_define(vm, o, key, nv, nf); return 1; }
    if (ef == nf) {
        jv old = JV_UNDEF;
        uint32_t of = 0;
        if (acc && in_shape && js_get_own(vm, o, key, &old, &of)) {
            /* the pair in place: its getter and setter given */
            vm->objs[jv_idx(old)].x = vm->objs[jv_idx(nv)].x;
            vm->objs[jv_idx(old)].a = vm->objs[jv_idx(nv)].a;
        } else js_define(vm, o, key, nv, nf);
        return 1;
    }
    /* other flags: the shape made again (a deleted entry is not in it: the element added) */
    px_reshape(vm, o, key, nv, nf);
    if (!in_shape) js_define(vm, o, key, nv, nf);
    return 1;
}

static int px_length_define(js_vm *vm, uint32_t o, const px_desc *d) {
    uint32_t fl = vm->objs[o].flags;
    int ro = (fl & (JO_LENGTH_RO | JO_FROZEN)) != 0;
    /* a data property, neither enumerable nor configurable, not writable again once not */
    if (pd_accessor(d) || ((d->has & PD_CONFIGURABLE) && d->configurable) || ((d->has & PD_ENUMERABLE) && d->enumerable)) return 0;
    if (ro && (d->has & PD_WRITABLE) && d->writable) return 0;
    int make_ro = (d->has & PD_WRITABLE) && !d->writable;
    if (d->has & PD_VALUE) {
        double n = js_to_number(vm, d->value);
        if (vm->throwing) return -1;
        if (n != n || n < 0 || n != trunc(n) || n > 4294967295.0) { js_throw_msg(vm, vm->range_error_proto, "Invalid array length"); return -1; }
        if (ro) return (uint32_t)n == vm->objs[o].b;
        /* shorter: the elements past it deleted, a non-configurable one stopping it (vm->refused) */
        int r = js_array_set_length_checked(vm, o, n);
        if (r == 0) return -1;
        if (r == 2) { if (make_ro) vm->objs[o].flags |= JO_LENGTH_RO; return 0; }
    }
    if (make_ro) vm->objs[o].flags |= JO_LENGTH_RO;
    return 1;
}

/* ---- the proxies' internal methods, each checked against its target ---- */

/* [[GetOwnProperty]] of any object: 1 found (into d), 0 none, -1 thrown */
static int px_own(js_vm *vm, uint32_t o, uint32_t key, px_desc *d) {
    if (vm->objs[o].kind != JK_PROXY) return px_ordinary_own(vm, o, key, d);
    jv t = px_trap(vm, o, "getOwnPropertyDescriptor");
    if (t == JV_HOLE) return -1;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_own(vm, target, key, d);
    jv r = px_call3(vm, t, o, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    if (vm->throwing) return -1;
    if (!jv_is_obj(r) && r != JV_UNDEF) {
        px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap returned neither object nor undefined for property '", key, 1, "'", 0, 0, "");
        return -1;
    }
    px_desc td;
    int th = px_own(vm, target, key, &td);
    if (th < 0) return -1;
    if (r == JV_UNDEF) {
        if (!th) return 0;
        if (!td.configurable) {
            px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap returned undefined for property '", key, 1, "' which is non-configurable in the proxy target", 0, 0, "");
            return -1;
        }
        int ext = px_is_extensible(vm, target);
        if (ext < 0) return -1;
        if (!ext) {
            px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap returned undefined for property '", key, 1, "' which exists in the non-extensible proxy target", 0, 0, "");
            return -1;
        }
        return 0;
    }
    int ext = px_is_extensible(vm, target);
    if (ext < 0) return -1;
    if (!px_to_desc(vm, r, d)) return -1;
    px_complete(d);
    if (!px_compatible(vm, ext, d, &td, th)) {
        px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap returned descriptor for property '", key, 1, "' that is incompatible with the existing property in the proxy target", 0, 0, "");
        return -1;
    }
    if (!d->configurable) {
        if (!th || td.configurable) {
            px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap reported non-configurability for property '", key, 1,
                     "' which is either non-existent or configurable in the proxy target", 0, 0, "");
            return -1;
        }
        if ((d->has & PD_WRITABLE) && !d->writable && td.writable) {
            px_error(vm, "'getOwnPropertyDescriptor' on proxy: trap reported non-configurable and writable for property '", key, 1,
                     "' which is non-configurable, non-writable in the proxy target", 0, 0, "");
            return -1;
        }
    }
    return 1;
}

/* [[DefineOwnProperty]] of any object: 1 done, 0 refused, -1 thrown */
static int px_define_desc(js_vm *vm, uint32_t o, uint32_t key, const px_desc *d) {
    vm->refused = JSR_NONE;
    if (vm->objs[o].kind != JK_PROXY) return px_ordinary_define(vm, o, key, d);
    jv t = px_trap(vm, o, "defineProperty");
    if (t == JV_HOLE) return -1;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_define_desc(vm, target, key, d);
    jv r = px_call3(vm, t, o, px_key_value(key), px_from_desc(vm, d), JV_UNDEF, 2);
    if (vm->throwing) return -1;
    if (!js_truthy(vm, r)) return 0;
    px_desc td;
    int th = px_own(vm, target, key, &td);
    if (th < 0) return -1;
    int ext = px_is_extensible(vm, target);
    if (ext < 0) return -1;
    int config_false = (d->has & PD_CONFIGURABLE) && !d->configurable;
    if (!th) {
        if (!ext) { px_error(vm, "'defineProperty' on proxy: trap returned truish for adding property '", key, 1, "'  to the non-extensible proxy target", 0, 0, ""); return -1; }
        if (config_false) {
            px_error(vm, "'defineProperty' on proxy: trap returned truish for defining non-configurable property '", key, 1,
                     "' which is either non-existent or configurable in the proxy target", 0, 0, "");
            return -1;
        }
        return 1;
    }
    if (!px_compatible(vm, ext, d, &td, 1)) {
        px_error(vm, "'defineProperty' on proxy: trap returned truish for adding property '", key, 1, "'  that is incompatible with the existing property in the proxy target", 0, 0, "");
        return -1;
    }
    if (config_false && td.configurable) {
        px_error(vm, "'defineProperty' on proxy: trap returned truish for defining non-configurable property '", key, 1,
                 "' which is either non-existent or configurable in the proxy target", 0, 0, "");
        return -1;
    }
    if (pd_data(&td) && !td.configurable && td.writable && (d->has & PD_WRITABLE) && !d->writable) {
        px_error(vm, "'defineProperty' on proxy: trap returned truish for defining non-configurable property '", key, 1,
                 "' which cannot be non-writable, unless there exists a corresponding non-configurable, non-writable own property of the target object.", 0, 0, "");
        return -1;
    }
    return 1;
}

/* IsExtensible of any object: 1, 0, -1 thrown */
static int px_is_extensible(js_vm *vm, uint32_t o) {
    if (vm->objs[o].kind != JK_PROXY) return (vm->objs[o].flags & JO_EXTENSIBLE) != 0;
    jv t = px_trap(vm, o, "isExtensible");
    if (t == JV_HOLE) return -1;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_is_extensible(vm, target);
    jv r = px_call3(vm, t, o, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
    if (vm->throwing) return -1;
    int b = js_truthy(vm, r), te = px_is_extensible(vm, target);
    if (te < 0) return -1;
    if (b != te) {
        px_error(vm, "'isExtensible' on proxy: trap result does not reflect extensibility of proxy target (which is '", 0, 0, te ? "true" : "false", 0, 0, "')");
        return -1;
    }
    return b;
}

/* [[PreventExtensions]] of any object: 1 done, 0 refused, -1 thrown */
static int px_prevent(js_vm *vm, uint32_t o) {
    if (vm->objs[o].kind != JK_PROXY) { vm->objs[o].flags &= (uint16_t)~JO_EXTENSIBLE; return 1; }
    jv t = px_trap(vm, o, "preventExtensions");
    if (t == JV_HOLE) return -1;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_prevent(vm, target);
    jv r = px_call3(vm, t, o, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
    if (vm->throwing) return -1;
    if (!js_truthy(vm, r)) return 0;
    int te = px_is_extensible(vm, target);
    if (te < 0) return -1;
    if (te) { js_throw_msg(vm, vm->type_error_proto, "'preventExtensions' on proxy: trap returned truish but the proxy target is extensible"); return -1; }
    return 1;
}

/* [[GetPrototypeOf]] of any object: an object, JV_NULL, or JV_HOLE thrown */
static jv px_get_proto(js_vm *vm, uint32_t o) {
    if (vm->objs[o].kind != JK_PROXY) return vm->objs[o].proto ? jv_obj(vm->objs[o].proto) : JV_NULL;
    jv t = px_trap(vm, o, "getPrototypeOf");
    if (t == JV_HOLE) return JV_HOLE;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_get_proto(vm, target);
    jv r = px_call3(vm, t, o, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
    if (vm->throwing) return JV_HOLE;
    if (!jv_is_obj(r) && r != JV_NULL) return js_throw_msg(vm, vm->type_error_proto, "'getPrototypeOf' on proxy: trap returned neither object nor null");
    int ext = px_is_extensible(vm, target);
    if (ext < 0) return JV_HOLE;
    if (ext) return r;
    jv tp = px_get_proto(vm, target);
    if (tp == JV_HOLE) return JV_HOLE;
    if (!px_same(vm, r, tp)) return js_throw_msg(vm, vm->type_error_proto, "'getPrototypeOf' on proxy: proxy target is non-extensible but the trap did not return its actual prototype");
    return r;
}

/* [[SetPrototypeOf]] of any object (np an object or JV_NULL): 1 done, 0 refused, -1 thrown */
static int px_set_proto(js_vm *vm, uint32_t o, jv np) {
    if (vm->objs[o].kind != JK_PROXY) {
        uint32_t p = jv_is_obj(np) ? jv_idx(np) : 0;
        if (vm->objs[o].proto == p) return 1;
        /* Object.prototype's prototype is immutable (an immutable prototype exotic object) */
        if (o == vm->object_proto) return 0;
        if (!(vm->objs[o].flags & JO_EXTENSIBLE)) return 0;
        /* no cycle */
        for (uint32_t x = p, d = 0; x && d < 10000; d++) {
            if (x == o) return 0;
            if (vm->objs[x].kind == JK_PROXY) break;
            x = vm->objs[x].proto;
        }
        js_set_proto(vm, o, p);
        return 1;
    }
    jv t = px_trap(vm, o, "setPrototypeOf");
    if (t == JV_HOLE) return -1;
    uint32_t target = vm->objs[o].a;
    if (t == JV_UNDEF) return px_set_proto(vm, target, np);
    jv r = px_call3(vm, t, o, np, JV_UNDEF, JV_UNDEF, 1);
    if (vm->throwing) return -1;
    if (!js_truthy(vm, r)) return 0;
    int ext = px_is_extensible(vm, target);
    if (ext < 0) return -1;
    if (ext) return 1;
    jv tp = px_get_proto(vm, target);
    if (tp == JV_HOLE) return -1;
    if (!px_same(vm, np, tp)) {
        js_throw_msg(vm, vm->type_error_proto, "'setPrototypeOf' on proxy: trap returned truish for setting a new prototype on the non-extensible proxy target");
        return -1;
    }
    return 1;
}

/* the keys an object owns, strings (an array's length after its indices, js_own_keys's)
   then symbols: OrdinaryOwnPropertyKeys, or the proxy's [[OwnPropertyKeys]] */
static uint32_t px_own_keys_all(js_vm *vm, uint32_t o) {
    uint32_t arr = js_array_new(vm, 0);
    if (vm->objs[o].kind == JK_PROXY) { js_proxy_own_keys(vm, o, 1, 1, 0, arr); return arr; }
    js_own_keys(vm, o, 0, arr);
    js_own_symbols(vm, o, arr);
    return arr;
}

/* a key value (a string or a symbol) as a key */
static uint32_t px_key_of(js_vm *vm, jv k) { return jv_is_sym(k) ? (JS_SYM_KEY | jv_idx(k)) : js_atom(vm, jv_idx(k)); }

jv js_proxy_get(js_vm *vm, uint32_t p, uint32_t key, jv receiver);
static jv px_ordinary_get(js_vm *vm, uint32_t o, uint32_t key, jv receiver);
static int px_ordinary_set(js_vm *vm, uint32_t o, uint32_t key, jv v, jv receiver);

jv js_proxy_get(js_vm *vm, uint32_t p, uint32_t key, jv receiver) {
    jv t = px_trap(vm, p, "get");
    if (t == JV_HOLE) return JV_HOLE;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) return px_ordinary_get(vm, target, key, receiver);
    jv r = px_call3(vm, t, p, px_key_value(key), receiver, JV_UNDEF, 2);
    if (vm->throwing) return JV_HOLE;
    px_desc td;
    int th = px_own(vm, target, key, &td);
    if (th < 0) return JV_HOLE;
    if (th && !td.configurable) {
        if (pd_data(&td) && !td.writable && !px_same(vm, r, td.value)) {
            vector<char> m;
            const char *a = "'get' on proxy: property '";
            append_range(m, a, strlen(a));
            px_key_text(vm, key, &m);
            const char *b = "' is a read-only and non-configurable data property on the proxy target but the proxy did not return its actual value (expected '";
            append_range(m, b, strlen(b));
            px_value_text(vm, td.value, &m);
            const char *c = "' but got '";
            append_range(m, c, strlen(c));
            px_value_text(vm, r, &m);
            push_back(m, '\'');
            push_back(m, ')');
            push_back(m, 0);
            return js_throw_msg(vm, vm->type_error_proto, data(m));
        }
        if (pd_accessor(&td) && td.get == JV_UNDEF && r != JV_UNDEF)
            return px_error(vm, "'get' on proxy: property '", key, 1,
                            "' is a non-configurable accessor property on the proxy target and does not have a getter function, but the trap did not return 'undefined' (got '",
                            r, 1, "')");
    }
    return r;
}

int js_proxy_set(js_vm *vm, uint32_t p, uint32_t key, jv v, jv receiver) {
    jv t = px_trap(vm, p, "set");
    if (t == JV_HOLE) return 0;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) return px_ordinary_set(vm, target, key, v, receiver);
    jv r = px_call3(vm, t, p, px_key_value(key), v, receiver, 3);
    if (vm->throwing || !js_truthy(vm, r)) return 0;
    px_desc td;
    int th = px_own(vm, target, key, &td);
    if (th < 0) return 0;
    if (th && !td.configurable) {
        if (pd_data(&td) && !td.writable && !px_same(vm, v, td.value)) {
            px_error(vm, "'set' on proxy: trap returned truish for property '", key, 1,
                     "' which exists in the proxy target as a non-configurable and non-writable data property with a different value", 0, 0, "");
            return 0;
        }
        if (pd_accessor(&td) && td.set == JV_UNDEF) {
            px_error(vm, "'set' on proxy: trap returned truish for property '", key, 1,
                     "' which exists in the proxy target as a non-configurable and non-writable accessor property without a setter", 0, 0, "");
            return 0;
        }
    }
    return 1;
}

int js_proxy_has(js_vm *vm, uint32_t p, uint32_t key) {
    jv t = px_trap(vm, p, "has");
    if (t == JV_HOLE) return 0;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) return js_has(vm, target, key);
    jv r = px_call3(vm, t, p, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    if (vm->throwing) return 0;
    int b = js_truthy(vm, r);
    if (!b) {
        px_desc td;
        int th = px_own(vm, target, key, &td);
        if (th < 0) return 0;
        if (th) {
            if (!td.configurable) {
                px_error(vm, "'has' on proxy: trap returned falsish for property '", key, 1, "' which exists in the proxy target as non-configurable", 0, 0, "");
                return 0;
            }
            int ext = px_is_extensible(vm, target);
            if (ext < 0) return 0;
            if (!ext) { px_error(vm, "'has' on proxy: trap returned falsish for property '", key, 1, "' but the proxy target is not extensible", 0, 0, ""); return 0; }
        }
    }
    return b;
}

int js_proxy_delete(js_vm *vm, uint32_t p, uint32_t key) {
    jv t = px_trap(vm, p, "deleteProperty");
    if (t == JV_HOLE) return 0;
    uint32_t target = vm->objs[p].a;
    if (t == JV_UNDEF) return js_delete(vm, target, key);
    jv r = px_call3(vm, t, p, px_key_value(key), JV_UNDEF, JV_UNDEF, 1);
    if (vm->throwing || !js_truthy(vm, r)) return 0;
    px_desc td;
    int th = px_own(vm, target, key, &td);
    if (th < 0) return 0;
    if (!th) return 1;
    if (!td.configurable) {
        px_error(vm, "'deleteProperty' on proxy: trap returned truish for property '", key, 1, "' which is non-configurable in the proxy target", 0, 0, "");
        return 0;
    }
    int ext = px_is_extensible(vm, target);
    if (ext < 0) return 0;
    if (!ext) { px_error(vm, "'deleteProperty' on proxy: trap returned truish for property '", key, 1, "' but the proxy target is non-extensible", 0, 0, ""); return 0; }
    return 1;
}

/* the descriptor as js_get_own reports a property: its value (an accessor's pair made) and flags */
static void px_desc_flags(js_vm *vm, const px_desc *d, jv *v, uint32_t *flags) {
    *flags = (d->enumerable ? 0 : JS_SHAPE_HIDDEN) | (d->configurable ? 0 : JS_SHAPE_FIXED);
    if (pd_accessor(d)) {
        uint32_t acc = js_obj_new(vm, JK_ACCESSOR, 0);
        vm->objs[acc].x = d->get;
        vm->objs[acc].a = jv_is_obj(d->set) ? jv_idx(d->set) : 0;
        *v = jv_obj(acc);
        *flags |= JS_SHAPE_ACCESSOR;
        return;
    }
    *v = d->value;
    if (!d->writable) *flags |= JS_SHAPE_READONLY;
}

int js_proxy_get_own(js_vm *vm, uint32_t p, uint32_t key, jv *v, uint32_t *flags) {
    px_desc d;
    if (px_own(vm, p, key, &d) <= 0) return 0;
    px_desc_flags(vm, &d, v, flags);
    return 1;
}

/* the proxy's own keys onto arr ([[OwnPropertyKeys]], checked against its target): its
   strings (strings), its symbols (symbols), only the enumerable ones when asked */
void js_proxy_own_keys(js_vm *vm, uint32_t p, int strings, int symbols, int enumerable_only, uint32_t arr) {
    jv t = px_trap(vm, p, "ownKeys");
    if (t == JV_HOLE) return;
    uint32_t target = vm->objs[p].a;
    uint32_t keys = 0;
    if (t == JV_UNDEF) keys = px_own_keys_all(vm, target);
    else {
        jv r = px_call3(vm, t, p, JV_UNDEF, JV_UNDEF, JV_UNDEF, 0);
        if (vm->throwing) return;
        if (!jv_is_obj(r)) { js_throw_msg(vm, vm->type_error_proto, "CreateListFromArrayLike called on non-object"); return; }
        jv lv = js_get(vm, r, A_LENGTH);
        if (vm->throwing) return;
        double n = js_to_number(vm, lv);
        if (vm->throwing) return;
        if (n != n || n < 0) n = 0;
        keys = js_array_new(vm, 0);
        for (uint32_t i = 0; i < (uint32_t)n; i++) {
            jv k = js_get_value(vm, r, jv_num(i));
            if (vm->throwing) return;
            if (!jv_is_str(k) && !jv_is_sym(k)) { px_error(vm, "", 0, 0, "", k, 1, " is not a valid property name"); return; }
            for (uint32_t j = 0; j < vm->objs[keys].b; j++)
                if (px_same(vm, js_array_get(vm, keys, j), k)) { js_throw_msg(vm, vm->type_error_proto, "'ownKeys' on proxy: trap returned duplicate entries"); return; }
            js_array_push(vm, keys, k);
        }
        /* the target's keys: the non-configurable ones must be there, and with a target
           that cannot be extended every one of them, and no other */
        int ext = px_is_extensible(vm, target);
        if (ext < 0) return;
        uint32_t tk = px_own_keys_all(vm, target);
        if (vm->throwing) return;
        uint32_t nt = vm->objs[tk].b;
        vector<uint32_t> fixed;
        resize(fixed, nt, 0);
        int any_fixed = 0;
        for (uint32_t i = 0; i < nt; i++) {
            px_desc td;
            int th = px_own(vm, target, px_key_of(vm, js_array_get(vm, tk, i)), &td);
            if (th < 0) return;
            if (th && !td.configurable) { fixed[i] = 1; any_fixed = 1; }
        }
        if (!ext || any_fixed) {
            uint32_t nk = vm->objs[keys].b;
            vector<uint32_t> used;
            resize(used, nk, 0);
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 1 && ext) break;
                for (uint32_t i = 0; i < nt; i++) {
                    if ((uint32_t)fixed[i] != (uint32_t)(pass == 0)) continue;
                    jv want = js_array_get(vm, tk, i);
                    int found = 0;
                    for (uint32_t j = 0; j < nk && !found; j++)
                        if (!used[j] && px_same(vm, js_array_get(vm, keys, j), want)) { used[j] = 1; found = 1; }
                    if (!found) { px_error(vm, "'ownKeys' on proxy: trap result did not include '", px_key_of(vm, want), 1, "'", 0, 0, ""); return; }
                }
            }
            if (!ext) {
                for (uint32_t j = 0; j < nk; j++)
                    if (!used[j]) { js_throw_msg(vm, vm->type_error_proto, "'ownKeys' on proxy: trap returned extra keys but proxy target is non-extensible"); return; }
            }
        }
    }
    for (uint32_t i = 0; i < vm->objs[keys].b && !vm->throwing; i++) {
        jv k = js_array_get(vm, keys, i);
        if (jv_is_str(k) ? !strings : !symbols) continue;
        if (enumerable_only) {
            px_desc d;
            int h = px_own(vm, p, px_key_of(vm, k), &d);
            if (h < 0) return;
            if (!h || !d.enumerable) continue;
        }
        js_array_push(vm, arr, k);
    }
}

/* the proxy's prototype: an object, or JV_NULL */
jv js_proxy_get_proto(js_vm *vm, uint32_t p) { return px_get_proto(vm, p); }

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
        if (!jv_is_obj(r)) return px_error(vm, "'construct' on proxy: trap returned non-object ('", 0, 0, "", r, 1, "')");
        return r;
    }
    return px_call3(vm, t, p, thisv == JV_HOLE ? JV_UNDEF : thisv, jv_obj(arr), JV_UNDEF, 2);
}

/* ---- [[Get]] and [[Set]] with a receiver of their own ---- */

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

/* [[Set]] of an ordinary object with a receiver: a setter up the prototypes called with the
   receiver as this; else the receiver's own data property set, or made (through its
   [[GetOwnProperty]] and [[DefineOwnProperty]], a proxy's traps among them) */
static int px_ordinary_set(js_vm *vm, uint32_t o, uint32_t key, jv v, jv receiver) {
    if (receiver == jv_obj(o) && vm->objs[o].kind != JK_PROXY) return js_set(vm, receiver, key, v);
    for (uint32_t x = o, d = 0; x && d < 10000; d++) {
        if (vm->objs[x].kind == JK_PROXY) return js_proxy_set(vm, x, key, v, receiver);
        px_desc cur;
        int h = px_ordinary_own(vm, x, key, &cur);
        if (h < 0) return 0;
        if (h) {
            if (pd_accessor(&cur)) {
                if (cur.set == JV_UNDEF) return 0;
                jv a1[1] = { v };
                js_call(vm, cur.set, receiver, a1, 1);
                return !vm->throwing;
            }
            if (!cur.writable) return 0;
            break;
        }
        x = vm->objs[x].proto;
    }
    if (!jv_is_obj(receiver)) return 0;
    uint32_t r = jv_idx(receiver);
    px_desc ex;
    int has = px_own(vm, r, key, &ex);
    if (has < 0) return 0;
    px_desc nd;
    memset(&nd, 0, sizeof nd);
    nd.get = nd.set = JV_UNDEF;
    if (has) {
        if (pd_accessor(&ex) || !ex.writable) return 0;
        nd.has = PD_VALUE;
        nd.value = v;
    } else {
        nd.has = PD_VALUE | PD_WRITABLE | PD_ENUMERABLE | PD_CONFIGURABLE;
        nd.value = v;
        nd.writable = nd.enumerable = nd.configurable = 1;
    }
    return px_define_desc(vm, r, key, &nd) > 0;
}

/* ---- the natives ---- */

/* a value as a key for Reflect: an atom or a symbol's key; 0 with ok cleared when its conversion threw */
static uint32_t px_key(js_vm *vm, jv v, int *ok) { return js_prop_key(vm, v, ok); }

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

/* Object.defineProperty's refusal, in V8's words: a proxy's trap, or an ordinary object's */
static jv px_define_refused(js_vm *vm, uint32_t o, uint32_t key) {
    if (vm->objs[o].kind == JK_PROXY) return px_error(vm, "'defineProperty' on proxy: trap returned falsish for property '", key, 1, "'", 0, 0, "");
    if (vm->refused == JSR_DELETE) {
        /* an array's length shortened to a non-configurable element: V8's words of the delete */
        vm->refused = JSR_NONE;
        vector<char> m;
        const char *a = "Cannot delete property '";
        append_range(m, a, strlen(a));
        px_key_text(vm, vm->refused_key, &m);
        const char *b = "' of ";
        append_range(m, b, strlen(b));
        js_value_text(vm, jv_obj(o), &m);
        push_back(m, 0);
        return js_throw_msg(vm, vm->type_error_proto, data(m));
    }
    if (px_bad_index(vm, o, key)) return js_throw_msg(vm, vm->type_error_proto, "Invalid typed array index");
    px_desc cur;
    int h = px_ordinary_own(vm, o, key, &cur);
    vm->throwing = 0;
    if (h > 0) return px_error(vm, "Cannot redefine property: ", key, 1, "", 0, 0, "");
    return px_error(vm, "Cannot define property ", key, 1, ", object is not extensible", 0, 0, "");
}

/* every own property non-configurable (sealed), and non-writable too (frozen)? the object
   not extensible: SetIntegrityLevel's test */
static int px_integrity(js_vm *vm, uint32_t o, int frozen) {
    int ext = px_is_extensible(vm, o);
    if (ext != 0) return ext < 0 ? -1 : 0;
    if (vm->objs[o].kind != JK_PROXY && (vm->objs[o].flags & (frozen ? JO_FROZEN : JO_FROZEN | JO_SEALED))) return 1;
    uint32_t keys = px_own_keys_all(vm, o);
    if (vm->throwing) return -1;
    for (uint32_t i = 0; i < vm->objs[keys].b; i++) {
        px_desc d;
        uint32_t key = px_key_of(vm, js_array_get(vm, keys, i));
        int h = px_own(vm, o, key, &d);
        if (h < 0) return -1;
        if (!h) continue;
        if (d.configurable) return 0;
        /* V8's: an array's length is not counted writable (its own accessor, to V8) */
        if (frozen && pd_data(&d) && d.writable && !(key == A_LENGTH && vm->objs[o].kind == JK_ARRAY)) return 0;
    }
    return 1;
}

jv js_proxy_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    (void)callee;
    if (which >= JPX_R_APPLY && which <= JPX_R_SETPROTO && which != JPX_R_APPLY && which != JPX_R_CONSTRUCT && !jv_is_obj(a0)) {
        static const char *const rn[] = { "apply", "construct", "defineProperty", "deleteProperty", "get", "getOwnPropertyDescriptor",
                                          "getPrototypeOf", "has", "isExtensible", "ownKeys", "preventExtensions", "set", "setPrototypeOf" };
        char m[96];
        snprintf(m, sizeof m, "Reflect.%s called on non-object", rn[which - JPX_R_APPLY]);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
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
        if (!js_is_callable(vm, a0)) {
            /* V8's words: the value's text, then its type with its article */
            vector<char> m;
            const char *pre = "Function.prototype.apply was called on ";
            append_range(m, pre, strlen(pre));
            js_value_text(vm, a0, &m);
            const char *mid = a0 == JV_NULL ? ", which is null" : jv_is_obj(a0) ? ", which is an object" : ", which is a ";
            append_range(m, mid, strlen(mid));
            if (a0 != JV_NULL && !jv_is_obj(a0)) js_str_to_utf8(vm, js_typeof(vm, a0), &m);
            const char *post = " and not a function";
            append_range(m, post, strlen(post));
            push_back(m, 0);
            return js_throw_msg(vm, vm->type_error_proto, data(m));
        }
        vector<jv> av;
        if (!px_list(vm, a2, &av)) return JV_HOLE;
        return js_call(vm, a0, a1, data(av), (uint32_t)len(av));
    }
    case JPX_R_CONSTRUCT: {
        if (!js_is_callable(vm, a0)) return js_not_constructor(vm, a0);
        jv nt = argc > 2 ? a2 : a0;
        if (!js_is_callable(vm, nt)) return js_not_constructor(vm, nt);
        vector<jv> av;
        if (!px_list(vm, a1, &av)) return JV_HOLE;
        return js_construct(vm, a0, data(av), (uint32_t)len(av), nt);
    }
    case JPX_R_DEFINE: case JPX_OBJ_DEFPROP: {
        if (which == JPX_OBJ_DEFPROP && !jv_is_obj(a0)) return js_throw_msg(vm, vm->type_error_proto, "Object.defineProperty called on non-object");
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        px_desc d;
        if (!px_to_desc(vm, a2, &d)) return JV_HOLE;
        int r = px_define_desc(vm, jv_idx(a0), key, &d);
        if (r < 0) return JV_HOLE;
        if (which == JPX_R_DEFINE) return jv_bool(r);
        if (!r) return px_define_refused(vm, jv_idx(a0), key);
        return a0;
    }
    case JPX_OBJ_DEFPROPS: {
        /* every descriptor read first, then each property defined */
        if (!jv_is_obj(a0)) return js_throw_msg(vm, vm->type_error_proto, "Object.defineProperties called on non-object");
        jv props = js_to_object(vm, a1);
        if (vm->throwing) return JV_HOLE;
        uint32_t keys = px_own_keys_all(vm, jv_idx(props));
        if (vm->throwing) return JV_HOLE;
        vector<uint32_t> ks;
        vector<px_desc> ds;
        for (uint32_t i = 0; i < vm->objs[keys].b; i++) {
            uint32_t key = px_key_of(vm, js_array_get(vm, keys, i));
            px_desc pd;
            int h = px_own(vm, jv_idx(props), key, &pd);
            if (h < 0) return JV_HOLE;
            if (!h || !pd.enumerable) continue;
            jv dv = js_get(vm, props, key);
            if (vm->throwing) return JV_HOLE;
            px_desc d;
            if (!px_to_desc(vm, dv, &d)) return JV_HOLE;
            push_back(ks, key);
            push_back(ds, d);
        }
        for (size_t i = 0; i < len(ks); i++) {
            px_desc d = ds[i];
            int r = px_define_desc(vm, jv_idx(a0), ks[i], &d);
            if (r < 0) return JV_HOLE;
            if (!r) return px_define_refused(vm, jv_idx(a0), ks[i]);
        }
        return a0;
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
    case JPX_R_GETOWNDESC: case JPX_OBJ_GETOWNDESC: {
        jv o = a0;
        if (which == JPX_OBJ_GETOWNDESC) { o = js_to_object(vm, a0); if (vm->throwing) return JV_HOLE; }
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        px_desc d;
        int h = px_own(vm, jv_idx(o), key, &d);
        if (h < 0) return JV_HOLE;
        if (!h) return JV_UNDEF;
        return px_from_desc(vm, &d);
    }
    case JPX_R_GETPROTO: return px_get_proto(vm, jv_idx(a0));
    case JPX_R_HAS: {
        int ok = 1;
        uint32_t key = px_key(vm, a1, &ok);
        if (!ok) return JV_HOLE;
        int r = js_has(vm, jv_idx(a0), key);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_ISEXTENSIBLE: {
        int r = px_is_extensible(vm, jv_idx(a0));
        return r < 0 ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_OWNKEYS: {
        uint32_t keys = px_own_keys_all(vm, jv_idx(a0));
        return vm->throwing ? JV_HOLE : jv_obj(keys);
    }
    case JPX_R_PREVENTEXT: case JPX_OBJ_PREVENTEXT: {
        if (which == JPX_OBJ_PREVENTEXT && !jv_is_obj(a0)) return a0;
        int r = px_prevent(vm, jv_idx(a0));
        if (r < 0) return JV_HOLE;
        if (which == JPX_R_PREVENTEXT) return jv_bool(r);
        if (!r) return js_throw_msg(vm, vm->type_error_proto, "'preventExtensions' on proxy: trap returned falsish");
        return a0;
    }
    case JPX_OBJ_SEAL: {
        /* every own property non-configurable, then no new one: SetIntegrityLevel sealed */
        if (!jv_is_obj(a0)) return a0;
        uint32_t o = jv_idx(a0);
        int r = px_prevent(vm, o);
        if (r < 0) return JV_HOLE;
        if (!r) return js_throw_msg(vm, vm->type_error_proto, "'preventExtensions' on proxy: trap returned falsish");
        if (vm->objs[o].kind != JK_PROXY) { vm->objs[o].flags |= JO_SEALED; return a0; }
        uint32_t keys = px_own_keys_all(vm, o);
        if (vm->throwing) return JV_HOLE;
        for (uint32_t i = 0; i < vm->objs[keys].b; i++) {
            px_desc d;
            memset(&d, 0, sizeof d);
            d.value = d.get = d.set = JV_UNDEF;
            d.has = PD_CONFIGURABLE;
            int k = px_define_desc(vm, o, px_key_of(vm, js_array_get(vm, keys, i)), &d);
            if (k < 0) return JV_HOLE;
            if (!k) return px_define_refused(vm, o, px_key_of(vm, js_array_get(vm, keys, i)));
        }
        return a0;
    }
    case JPX_OBJ_ISSEALED: case JPX_OBJ_ISFROZEN: {
        if (!jv_is_obj(a0)) return JV_TRUE;
        int r = px_integrity(vm, jv_idx(a0), which == JPX_OBJ_ISFROZEN);
        return r < 0 ? JV_HOLE : jv_bool(r);
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
            /* the plain set, refused where the property is read-only or the object takes no new one */
            px_desc cur;
            int h = px_ordinary_own(vm, o, key, &cur);
            if (h < 0) return JV_HOLE;
            if (h && pd_data(&cur) && !cur.writable) return JV_FALSE;
            if (!h && !(vm->objs[o].flags & JO_EXTENSIBLE)) return JV_FALSE;
            r = js_set(vm, a0, key, a2) == 1;             /* 2: refused */
        } else r = px_ordinary_set(vm, o, key, a2, recv);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case JPX_R_SETPROTO: {
        if (!jv_is_obj(a1) && a1 != JV_NULL) return px_error(vm, "Object prototype may only be an Object or null: ", 0, 0, "", a1, 1, "");
        int r = px_set_proto(vm, jv_idx(a0), a1);
        return r < 0 ? JV_HOLE : jv_bool(r);
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
    case JPX_WM_GET: case JPX_WM_SET: case JPX_WM_HAS: case JPX_WM_DELETE: case JPX_WS_ADD: case JPX_WS_HAS: case JPX_WS_DELETE: {
        uint32_t want = which >= JPX_WS_ADD ? JK_SET : JK_MAP;
        if (!jv_is_obj(thisv) || !(vm->objs[jv_idx(thisv)].flags & JO_WEAK) || vm->objs[jv_idx(thisv)].kind != want) {
            static const char *const names[] = { "WeakMap.prototype.get", "WeakMap.prototype.set", "WeakMap.prototype.has",
                "WeakMap.prototype.delete", "WeakSet.prototype.add", "WeakSet.prototype.has", "WeakSet.prototype.delete" };
            vector<char> m;
            const char *pre = "Method ";
            append_range(m, pre, strlen(pre));
            append_range(m, names[which - JPX_WM_GET], strlen(names[which - JPX_WM_GET]));
            const char *mid = " called on incompatible receiver ";
            append_range(m, mid, strlen(mid));
            js_value_text(vm, thisv, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        if (which == JPX_WS_HAS) which = JPX_WM_HAS;
        else if (which == JPX_WS_DELETE) which = JPX_WM_DELETE;
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
            return px_incompatible(vm, "WeakRef.prototype.deref", thisv);
        return vm->objs[jv_idx(thisv)].x;
    case JPX_FINREG: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Constructor FinalizationRegistry requires 'new'");
        if (!js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "FinalizationRegistry: cleanup must be callable");
        uint32_t cells = js_array_new(vm, 0);
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->finreg_proto);
        vm->objs[o].x = a0;
        vm->objs[o].a = cells;
        vm->objs[o].flags |= JO_FINREG;
        return jv_obj(o);
    }
    case JPX_FR_REGISTER: case JPX_FR_UNREGISTER: {
        if (!jv_is_obj(thisv) || !(vm->objs[jv_idx(thisv)].flags & JO_FINREG))
            return js_throw_msg(vm, vm->type_error_proto, which == JPX_FR_REGISTER ?
                                "Method FinalizationRegistry.prototype.register called on incompatible receiver #<Object>" :
                                "Method FinalizationRegistry.prototype.unregister called on incompatible receiver #<Object>");
        uint32_t cells = vm->objs[jv_idx(thisv)].a;
        jv tok = which == JPX_FR_REGISTER ? js_arg(vm, args, argc, 2) : a0;
        if (which == JPX_FR_REGISTER) {
            if (!px_weak_key(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "FinalizationRegistry.prototype.register: invalid target");
            if (a0 == a1) return js_throw_msg(vm, vm->type_error_proto, "FinalizationRegistry.prototype.register: target and holdings must not be same");
        }
        if ((which == JPX_FR_UNREGISTER || tok != JV_UNDEF) && !px_weak_key(vm, tok)) {
            /* V8's words, the token written in them */
            uint32_t t = jv_is_sym(tok) ? js_sym_text(vm, jv_idx(tok)) : js_to_string(vm, tok);
            if (vm->throwing) return JV_HOLE;
            uint32_t m = js_str_concat(vm, js_str_ascii(vm, "Invalid unregisterToken ('"), js_str_concat(vm, t, js_str_ascii(vm, "')")));
            js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
            return JV_HOLE;
        }
        if (which == JPX_FR_REGISTER) {
            js_array_push(vm, cells, a0);
            js_array_push(vm, cells, a1);
            js_array_push(vm, cells, tok);
            return JV_UNDEF;
        }
        /* the cells the token unregisters taken out */
        uint32_t base = vm->objs[cells].a, n = vm->objs[cells].b, w = 0;
        int any = 0;
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            if (vm->slots[base + i + 2] == tok) { any = 1; continue; }
            for (uint32_t k = 0; k < 3; k++) vm->slots[base + w + k] = vm->slots[base + i + k];
            w += 3;
        }
        vm->objs[cells].b = w;
        return jv_bool(any);
    }
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
        js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_PROTO);
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
    px_def(vm, vm->weakset_proto, "has", first + JPX_WS_HAS, 1);
    px_def(vm, vm->weakset_proto, "delete", first + JPX_WS_DELETE, 1);
    px_def(vm, vm->weakref_proto, "deref", first + JPX_WR_DEREF, 0);
    px_def(vm, vm->finreg_proto, "register", first + JPX_FR_REGISTER, 2);
    px_def(vm, vm->finreg_proto, "unregister", first + JPX_FR_UNREGISTER, 1);
    px_tag(vm, vm->weakmap_proto, "WeakMap");
    px_tag(vm, vm->weakset_proto, "WeakSet");
    px_tag(vm, vm->weakref_proto, "WeakRef");
    px_tag(vm, vm->finreg_proto, "FinalizationRegistry");
}

int js_set_proto_of(js_vm *vm, uint32_t o, jv np) { return px_set_proto(vm, o, np); }

/* V8's "Method <what> called on incompatible receiver <v>" */
static jv px_incompatible(js_vm *vm, const char *what, jv v) {
    vector<char> m;
    const char *pre = "Method ", *mid = " called on incompatible receiver ";
    append_range(m, pre, strlen(pre));
    append_range(m, what, strlen(what));
    append_range(m, mid, strlen(mid));
    js_value_text(vm, v, &m);
    return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
}

/* CreateDataProperty: 1 defined, 0 refused, -1 thrown (a key's conversion, a proxy's trap) */
int js_create_data(js_vm *vm, jv obj, jv key, jv v) {
    if (!jv_is_obj(obj)) return 0;
    int ok = 1;
    uint32_t k = js_prop_key(vm, key, &ok);
    if (!ok) return -1;
    px_desc d;
    memset(&d, 0, sizeof d);
    d.get = d.set = JV_UNDEF;
    d.has = PD_VALUE | PD_WRITABLE | PD_ENUMERABLE | PD_CONFIGURABLE;
    d.value = v;
    d.writable = d.enumerable = d.configurable = 1;
    return px_define_desc(vm, jv_idx(obj), k, &d);
}

int js_create_data_throw(js_vm *vm, jv obj, jv key, jv v) {
    if (!jv_is_obj(obj)) { js_throw_msg(vm, vm->type_error_proto, "CreateDataProperty called on non-object"); return 0; }
    int ok = 1;
    uint32_t k = js_prop_key(vm, key, &ok);
    if (!ok) return 0;
    px_desc d;
    memset(&d, 0, sizeof d);
    d.get = d.set = JV_UNDEF;
    d.has = PD_VALUE | PD_WRITABLE | PD_ENUMERABLE | PD_CONFIGURABLE;
    d.value = v;
    d.writable = d.enumerable = d.configurable = 1;
    int r = px_define_desc(vm, jv_idx(obj), k, &d);
    if (r < 0) return 0;
    if (!r) { px_define_refused(vm, jv_idx(obj), k); return 0; }
    return 1;
}
