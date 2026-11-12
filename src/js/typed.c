/* src/js/typed.c -- ArrayBuffer, the typed arrays and DataView (ECMA-262 25.1-25.3), and
   TextEncoder and TextDecoder (the Encoding standard's).

   An ArrayBuffer's bytes are a span of vm->bytes: a its first, b its length. A resizable
   one (new ArrayBuffer(n, { maxByteLength })) keeps its maximum in x and reserves room
   ahead, a power of two up to its maximum: it grows in place within the room, else moves
   to a larger span at the pool's end (its views find it by their buffer). transfer and
   transferToFixedLength give its bytes to a new buffer and detach it: length 0, each of
   its methods refused.

   A typed array is a view of one: a the buffer, c its first byte, its element type in its
   flags above JO_TA_SHIFT; x its declared length (TA_TRACK when it tracks its buffer's
   length) and, above, the next view of its buffer: every buffer chains its views from c.
   b is the view's length now, so that the interpreter's paths read it as they read an
   array's: when its buffer is resized or detached, each of its views is given its length
   again, 0 out of the buffer's bounds. A DataView is a view of bytes the same way. The
   bytes are in the machine's order (little-endian); a DataView reads and writes either.
   BigInt64Array's and BigUint64Array's elements are BigInts (src/js/bigint.c), as
   DataView's BigInt methods read and write them.

   Each method refuses a view out of its buffer's bounds with V8's TypeError; the methods
   the typed arrays share with Array's are builtins.c's, wrapped here to refuse it too
   (js_typed_shared). */
#include <stdlib.h>
#include <math.h>
#include "js.h"

static const uint32_t ta_size[JTA_N] = { 1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8 };
static const char *const ta_name[JTA_N] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
                                            "Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array" };

/* a buffer's flags above the object's own */
#define AB_RESIZABLE 0x100u
#define AB_DETACHED 0x200u
/* a view's declared length when it tracks its buffer's */
#define TA_TRACK 0xFFFFFFFFu
/* the bytes a buffer may have */
#define AB_MAX (1u << 30)

static uint32_t ta_type(js_vm *vm, uint32_t o) { return ((uint32_t)vm->objs[o].flags >> JO_TA_SHIFT) & 15u; }
uint32_t js_ta_length(js_vm *vm, uint32_t o) { return vm->objs[o].b; }

static int ab_detached(js_vm *vm, uint32_t buf) { return (vm->objs[buf].flags & AB_DETACHED) != 0; }
static int ab_resizable(js_vm *vm, uint32_t buf) { return (vm->objs[buf].flags & AB_RESIZABLE) != 0; }
static double ab_max(js_vm *vm, uint32_t buf) { return jv_dbl(vm->objs[buf].x); }

/* ---- the views' lengths ---- */

static uint32_t vw_declared(js_vm *vm, uint32_t v) { return (uint32_t)vm->objs[v].x; }
static uint32_t vw_next(js_vm *vm, uint32_t v) { return (uint32_t)(vm->objs[v].x >> 32); }
static uint32_t vw_size(js_vm *vm, uint32_t v) { return vm->objs[v].kind == JK_TYPEDARRAY ? ta_size[ta_type(vm, v)] : 1; }

/* is the view out of its buffer's bounds (the buffer detached, or too short for it)? */
static int vw_oob(js_vm *vm, uint32_t v) {
    uint32_t buf = vm->objs[v].a;
    if (ab_detached(vm, buf)) return 1;
    uint32_t blen = vm->objs[buf].b, off = vm->objs[v].c, d = vw_declared(vm, v);
    if (d == TA_TRACK) return off > blen;
    return (uint64_t)off + (uint64_t)d * vw_size(vm, v) > blen;
}
/* its length now: its declared one, or as much of the buffer as it covers when it tracks it; 0 out of bounds */
static uint32_t vw_length(js_vm *vm, uint32_t v) {
    if (vw_oob(vm, v)) return 0;
    uint32_t d = vw_declared(vm, v);
    if (d != TA_TRACK) return d;
    return (vm->objs[vm->objs[v].a].b - vm->objs[v].c) / vw_size(vm, v);
}
/* each view of the buffer given its length again: after a resize, or a detach */
static void ab_refresh(js_vm *vm, uint32_t buf) {
    for (uint32_t v = vm->objs[buf].c, g = 0; v && g < (1u << 30); v = vw_next(vm, v), g++) vm->objs[v].b = vw_length(vm, v);
}
/* the view v of buf, its declared length d: chained to the buffer, its length now set
   (fresh, of a buffer made for it: d itself) */
static void vw_link(js_vm *vm, uint32_t v, uint32_t buf, uint32_t d, int fresh) {
    {
        js_obj *ob = data(vm->objs);
        ob[v].a = buf;
        ob[v].x = ((uint64_t)ob[buf].c << 32) | d;
        ob[buf].c = v;
        ob[v].b = d;
    }
    if (!fresh) vm->objs[v].b = vw_length(vm, v);
}

/* ---- buffers ---- */

/* the room a resizable buffer of n bytes keeps: n's power of two from 16, within its maximum */
static uint32_t ab_room(uint32_t n, double max) {
    uint32_t r = 16;
    while (r < n && r < AB_MAX) r *= 2;
    if ((double)r > max) r = (uint32_t)max;
    return r < n ? n : r;
}

/* a new buffer of n zero bytes, room of them reserved */
static uint32_t ab_alloc(js_vm *vm, uint32_t n, uint32_t room) {
    uint32_t o = js_obj_new(vm, JK_ARRAYBUFFER, vm->arraybuffer_proto);
    size_t at = len(vm->bytes);
    resize(vm->bytes, at + room, 0);
    vm->objs[o].a = (uint32_t)at;
    vm->objs[o].b = n;
    return o;
}
static uint32_t ab_new(js_vm *vm, uint32_t n) { return ab_alloc(vm, n, n); }

/* a resizable buffer of n bytes, its maximum max (n within it) */
static uint32_t ab_new_resizable(js_vm *vm, uint32_t n, double max) {
    uint32_t o = ab_alloc(vm, n, ab_room(n, max));
    vm->objs[o].flags |= AB_RESIZABLE;
    vm->objs[o].x = jv_num(max);
    return o;
}

/* a resizable buffer given n bytes: within its room in place, the new bytes zero; else moved */
static void ab_resize(js_vm *vm, uint32_t o, uint32_t n) {
    uint32_t old = vm->objs[o].b;
    double max = ab_max(vm, o);
    if (n > old && n <= ab_room(old, max)) {
        uint32_t a = vm->objs[o].a;
        for (uint32_t i = old; i < n; i++) vm->bytes[a + i] = 0;
    } else if (n > old) {
        uint32_t room = ab_room(n, max), from = vm->objs[o].a;
        size_t at = len(vm->bytes);
        resize(vm->bytes, at + room, 0);
        uint8_t *bp = data(vm->bytes);
        memcpy(bp + at, bp + from, old);
        vm->objs[o].a = (uint32_t)at;
    }
    vm->objs[o].b = n;
    ab_refresh(vm, o);
}

/* the buffer detached: its bytes no one's, each of its views out of bounds */
static void ab_detach(js_vm *vm, uint32_t o) {
    vm->objs[o].flags |= AB_DETACHED;
    vm->objs[o].b = 0;
    ab_refresh(vm, o);
}

/* the byte in vm->bytes where a view's element i begins */
static uint32_t ta_at(js_vm *vm, uint32_t o, uint32_t i) {
    uint32_t buf = vm->objs[o].a;
    return vm->objs[buf].a + vm->objs[o].c + i * ta_size[ta_type(vm, o)];
}

/* a value read from bytes at p as the element type t */
static double ta_read(js_vm *vm, uint32_t t, uint32_t p, int little) {
    uint8_t b[8];
    uint32_t n = ta_size[t];
    for (uint32_t k = 0; k < n; k++) b[k] = vm->bytes[p + (little ? k : n - 1 - k)];
    switch (t) {
    case JTA_INT8: return (double)(int8_t)b[0];
    case JTA_UINT8: case JTA_UINT8C: return (double)b[0];
    case JTA_INT16: { int16_t v; memcpy(&v, b, 2); return v; }
    case JTA_UINT16: { uint16_t v; memcpy(&v, b, 2); return v; }
    case JTA_INT32: { int32_t v; memcpy(&v, b, 4); return v; }
    case JTA_UINT32: { uint32_t v; memcpy(&v, b, 4); return v; }
    case JTA_FLOAT32: { float v; memcpy(&v, b, 4); return (double)v; }
    case JTA_FLOAT64: { double v; memcpy(&v, b, 8); return v; }
    default: return 0;
    }
}

/* a number written at p as the element type t (ToInt8 and its kin: modulo, or clamped) */
static void ta_write(js_vm *vm, uint32_t t, uint32_t p, double d, int little) {
    uint8_t b[8];
    uint32_t n = ta_size[t];
    int32_t iv = js_to_int32(d);
    switch (t) {
    case JTA_INT8: case JTA_UINT8: b[0] = (uint8_t)iv; break;
    case JTA_UINT8C: {
        /* ToUint8Clamp: rounded half to even, within 0 to 255 */
        double c = d != d ? 0 : d < 0 ? 0 : d > 255 ? 255 : d;
        double f = floor(c);
        double r = c - f > 0.5 ? f + 1 : c - f < 0.5 ? f : (fmod(f, 2) == 0 ? f : f + 1);
        b[0] = (uint8_t)r;
        break;
    }
    case JTA_INT16: case JTA_UINT16: { uint16_t v = (uint16_t)iv; memcpy(b, &v, 2); break; }
    case JTA_INT32: case JTA_UINT32: { uint32_t v = (uint32_t)iv; memcpy(b, &v, 4); break; }
    case JTA_FLOAT32: { float v = (float)d; memcpy(b, &v, 4); break; }
    case JTA_FLOAT64: memcpy(b, &d, 8); break;
    default: memset(b, 0, sizeof b); break;
    }
    for (uint32_t k = 0; k < n; k++) vm->bytes[p + (little ? k : n - 1 - k)] = b[k];
}

/* eight bytes at p as one 64-bit number, and written back */
static uint64_t ta_read64(js_vm *vm, uint32_t p, int little) {
    uint64_t u = 0;
    for (uint32_t k = 0; k < 8; k++) u |= (uint64_t)vm->bytes[p + (little ? k : 7 - k)] << (8 * k);
    return u;
}
static void ta_write64(js_vm *vm, uint32_t p, uint64_t u, int little) {
    for (uint32_t k = 0; k < 8; k++) vm->bytes[p + (little ? k : 7 - k)] = (uint8_t)(u >> (8 * k));
}
static jv ta_big_of(js_vm *vm, uint32_t t, uint64_t u) { return t == JTA_BIGINT64 ? js_big_from_i64(vm, (int64_t)u) : js_big_from_u64(vm, u, 0); }

/* a typed array's element i (i below its length): a number, or a BigInt for the two of them */
jv js_ta_get(js_vm *vm, uint32_t o, uint32_t i) {
    uint32_t t = ta_type(vm, o);
    if (t >= JTA_BIGINT64) return ta_big_of(vm, t, ta_read64(vm, ta_at(vm, o, i), 1));
    return jv_num(ta_read(vm, t, ta_at(vm, o, i), 1));
}

/* element i set to v converted (ToNumber, or ToBigInt for the BigInt arrays): 0 when the
   conversion threw. An index past the end (every index of a view out of bounds) is ignored */
int js_ta_set(js_vm *vm, uint32_t o, uint32_t i, jv v) {
    uint32_t t = ta_type(vm, o);
    if (t >= JTA_BIGINT64) {
        jv b = js_to_bigint(vm, v);
        if (vm->throwing) return 0;
        if (i < vm->objs[o].b) ta_write64(vm, ta_at(vm, o, i), js_big_to_u64(vm, b), 1);
        return 1;
    }
    double d = jv_is_num(v) ? jv_dbl(v) : js_to_number(vm, v);
    if (vm->throwing) return 0;
    if (i >= vm->objs[o].b) return 1;
    ta_write(vm, t, ta_at(vm, o, i), d, 1);
    return 1;
}

/* a new typed array of type t over buf from byte off, its declared length d (TA_TRACK: the
   buffer's); a new buffer of d elements when buf is 0 */
static uint32_t ta_new(js_vm *vm, uint32_t t, uint32_t d, uint32_t buf, uint32_t off, uint32_t proto) {
    int fresh = !buf;
    if (fresh) buf = ab_new(vm, d * ta_size[t]);
    uint32_t o = js_obj_new(vm, JK_TYPEDARRAY, proto ? proto : vm->ta_protos[t]);
    vm->objs[o].c = off;
    vm->objs[o].flags = (uint16_t)(vm->objs[o].flags | (t << JO_TA_SHIFT));
    vw_link(vm, o, buf, d, fresh);
    return o;
}

/* ToIndex: 0 with a RangeError thrown when v is no index; *num the integer it is */
static int to_index_num(js_vm *vm, jv v, uint32_t *out, double *num, const char *what) {
    *num = 0;
    if (v == JV_UNDEF) { *out = 0; return 1; }
    if (jv_is_num(v)) {
        /* an index already: the common case, no conversion */
        double x = jv_dbl(v);
        if (x >= 0 && x < 4294967296.0 && x == (double)(uint32_t)x) { *out = (uint32_t)x; *num = x; return 1; }
    }
    double d = js_to_number(vm, v);
    if (vm->throwing) return 0;
    d = d != d ? 0 : trunc(d);
    *num = d;
    if (d < 0 || d > 4294967295.0) { if (what) js_throw_msg(vm, vm->range_error_proto, what); return 0; }
    *out = (uint32_t)d;
    return 1;
}
static int to_index(js_vm *vm, jv v, uint32_t *out, const char *what) {
    double d;
    return to_index_num(vm, v, out, &d, what);
}

/* a RangeError of V8's with a number in it: pre, the number, post */
static jv range_num(js_vm *vm, const char *pre, double d, const char *post) {
    char m[160], nb[40];
    js_num_format(d, nb, sizeof nb);
    snprintf(m, sizeof m, "%s%s%s", pre, nb, post);
    return js_throw_msg(vm, vm->range_error_proto, m);
}

/* "Cannot perform <what> on a detached ArrayBuffer": V8's words for a detached buffer, or a view out of its bounds */
static jv ta_detached(js_vm *vm, const char *what) {
    char m[160];
    snprintf(m, sizeof m, "Cannot perform %s on a detached ArrayBuffer", what);
    return js_throw_msg(vm, vm->type_error_proto, m);
}

/* a relative place (slice's and fill's): from the end when negative, clamped to [0, n] */
static uint32_t rel_index(js_vm *vm, jv v, uint32_t n, uint32_t dflt) {
    if (v == JV_UNDEF) return dflt;
    double d = js_to_number(vm, v);
    if (vm->throwing) return 0;
    d = d != d ? 0 : trunc(d);
    if (d < 0) { d += n; if (d < 0) d = 0; }
    if (d > n) d = n;
    return (uint32_t)d;
}

static uint32_t this_ta(js_vm *vm, jv thisv, const char *what) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_TYPEDARRAY) return jv_idx(thisv);
    char m[160];
    snprintf(m, sizeof m, "%s called on an object that is not a typed array", what);
    js_throw_msg(vm, vm->type_error_proto, m);
    return 0;
}
/* this as a typed array in its buffer's bounds (ValidateTypedArray): 0 with V8's TypeError thrown */
static uint32_t valid_ta(js_vm *vm, jv thisv, const char *name) {
    char what[96];
    snprintf(what, sizeof what, "%%TypedArray%%.prototype.%s", name);
    uint32_t o = this_ta(vm, thisv, what);
    if (!o) return 0;
    if (vw_oob(vm, o)) { ta_detached(vm, what); return 0; }
    return o;
}

/* the values a source gives (a typed array's, an iterable's, an array-like's) onto out; a
   typed array out of its buffer's bounds refused, what its operation is */
static int ta_values_of(js_vm *vm, jv src, vector<jv> *out, const char *what) {
    if (jv_is_obj(src) && vm->objs[jv_idx(src)].kind == JK_TYPEDARRAY) {
        uint32_t s = jv_idx(src);
        if (vw_oob(vm, s)) { ta_detached(vm, what); return 0; }
        for (uint32_t i = 0; i < vm->objs[s].b; i++) push_back(*out, js_ta_get(vm, s, i));
        return 1;
    }
    if (jv_is_obj(src)) {
        jv f = js_get(vm, src, JS_SYM_KEY | JSYM_ITERATOR);
        if (vm->throwing) return 0;
        if (js_is_callable(vm, f)) {
            jv it = js_iter_of(vm, src);
            if (vm->throwing) return 0;
            jv x;
            while (js_iter_step(vm, jv_idx(it), &x)) push_back(*out, x);
            return !vm->throwing;
        }
    }
    jv lv = js_get(vm, src, A_LENGTH);
    if (vm->throwing) return 0;
    double n = js_to_number(vm, lv);
    if (vm->throwing) return 0;
    if (n != n || n < 0) n = 0;
    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        jv v = js_get_value(vm, src, jv_num(i));
        if (vm->throwing) return 0;
        push_back(*out, v);
    }
    return 1;
}

/* a typed array of type t holding the values (each converted) */
static jv ta_from_values(js_vm *vm, uint32_t t, vector<jv> *vals, uint32_t proto) {
    uint32_t o = ta_new(vm, t, (uint32_t)len(*vals), 0, 0, proto);
    for (size_t i = 0; i < len(*vals); i++) if (!js_ta_set(vm, o, (uint32_t)i, (*vals)[i])) return JV_HOLE;
    return jv_obj(o);
}

/* a typed array of type t over the buffer buf (InitializeTypedArrayFromArrayBuffer), from
   the byte offv, of the length lenv (undefined: as much as the buffer has, tracking a
   resizable one's); 0 with V8's error thrown */
static uint32_t ta_over(js_vm *vm, uint32_t t, uint32_t buf, jv offv, jv lenv, uint32_t proto) {
    uint32_t off = 0, n = 0, sz = ta_size[t];
    double od = 0, nd = 0;
    if (!to_index_num(vm, offv, &off, &od, 0)) {
        if (!vm->throwing) range_num(vm, "Start offset ", od, " is outside the bounds of the buffer");
        return 0;
    }
    if (off % sz) {
        char m[96];
        snprintf(m, sizeof m, "start offset of %s should be a multiple of %u", ta_name[t], sz);
        js_throw_msg(vm, vm->range_error_proto, m);
        return 0;
    }
    if (lenv != JV_UNDEF && !to_index_num(vm, lenv, &n, &nd, 0)) {
        if (!vm->throwing) range_num(vm, "Invalid typed array length: ", nd, "");
        return 0;
    }
    if (ab_detached(vm, buf)) { ta_detached(vm, "Construct"); return 0; }
    uint32_t blen = vm->objs[buf].b;
    if (lenv == JV_UNDEF && ab_resizable(vm, buf)) {
        if (off > blen) { range_num(vm, "Start offset ", off, " is outside the bounds of the buffer"); return 0; }
        return ta_new(vm, t, TA_TRACK, buf, off, proto);
    }
    if (lenv == JV_UNDEF) {
        if (blen % sz) {
            char m[96];
            snprintf(m, sizeof m, "byte length of %s should be a multiple of %u", ta_name[t], sz);
            js_throw_msg(vm, vm->range_error_proto, m);
            return 0;
        }
        if (off > blen) { range_num(vm, "Start offset ", off, " is outside the bounds of the buffer"); return 0; }
        n = (blen - off) / sz;
    } else if ((uint64_t)off + (uint64_t)n * sz > blen) {
        range_num(vm, "Invalid typed array length: ", n, "");
        return 0;
    }
    return ta_new(vm, t, n, buf, off, proto);
}

/* the typed array's type from its constructor (a callee whose x holds the type) */
static uint32_t ctor_type(js_vm *vm, jv callee) { return (uint32_t)jv_dbl(vm->objs[jv_idx(callee)].x); }

/* a new typed array like o's (its type), of n elements: a species construction, simplified */
static uint32_t ta_like(js_vm *vm, uint32_t o, uint32_t n) { return ta_new(vm, ta_type(vm, o), n, 0, 0, vm->objs[o].proto); }

/* a DataView's place for size bytes at off (GetViewValue's and SetViewValue's checks after
   the value's conversion): 0 with V8's error thrown, its method named by set and the
   element type t */
static int dv_place(js_vm *vm, uint32_t dv, uint32_t off, uint32_t size, uint32_t *p, int set, uint32_t t) {
    if ((uint64_t)off + size <= vm->objs[dv].b) {
        *p = vm->objs[vm->objs[dv].a].a + vm->objs[dv].c + off;
        return 1;
    }
    if (vw_oob(vm, dv)) {
        static const char *const dn[] = { "Int8", "Uint8", "", "Int16", "Uint16", "Int32", "Uint32", "Float32", "Float64", "BigInt64", "BigUint64" };
        char what[64];
        snprintf(what, sizeof what, "DataView.prototype.%s%s", set ? "set" : "get", dn[t]);
        ta_detached(vm, what);
        return 0;
    }
    js_throw_msg(vm, vm->range_error_proto, "Offset is outside the bounds of the DataView");
    return 0;
}

static int cmp_num(double x, double y) {
    /* the typed arrays' sort: numbers ascending, -0 before +0, NaN last */
    if (x != x) return y != y ? 0 : 1;
    if (y != y) return -1;
    if (x < y) return -1;
    if (x > y) return 1;
    if (x == 0 && y == 0) return signbit(x) && !signbit(y) ? -1 : (!signbit(x) && signbit(y) ? 1 : 0);
    return 0;
}

/* a receiver as V8 names it in "called on incompatible receiver": a primitive's text, an
   array's or a typed array's [object ...], any other object's #<its constructor's name> */
static void recv_text(js_vm *vm, jv v, vector<char> *out) {
    if (!jv_is_obj(v)) {
        uint32_t s = jv_is_sym(v) ? js_sym_text(vm, jv_idx(v)) : js_to_string(vm, v);
        vm->throwing = 0;
        if (s) js_str_to_utf8(vm, s, out);
        return;
    }
    uint32_t o = jv_idx(v);
    const char *pre = "[object ";
    if (vm->objs[o].kind == JK_ARRAY) { const char *t = "[object Array]"; append_range(*out, t, strlen(t)); return; }
    if (vm->objs[o].kind == JK_TYPEDARRAY) {
        append_range(*out, pre, strlen(pre));
        uint32_t t = ta_type(vm, o);
        append_range(*out, ta_name[t], strlen(ta_name[t]));
        push_back(*out, ']');
        return;
    }
    push_back(*out, '#');
    push_back(*out, '<');
    jv c = js_get(vm, v, A_CONSTRUCTOR);
    jv nm = vm->throwing || !jv_is_obj(c) ? JV_UNDEF : js_get(vm, c, A_NAME);
    vm->throwing = 0;
    if (jv_is_str(nm) && vm->strs[jv_idx(nm)].len) js_str_to_utf8(vm, jv_idx(nm), out);
    else { const char *t = "Object"; append_range(*out, t, strlen(t)); }
    push_back(*out, '>');
}
/* "Method <what> called on incompatible receiver <v>" */
static jv incompatible(js_vm *vm, const char *what, jv v) {
    vector<char> m;
    const char *pre = "Method ";
    append_range(m, pre, strlen(pre));
    append_range(m, what, strlen(what));
    const char *mid = " called on incompatible receiver ";
    append_range(m, mid, strlen(mid));
    recv_text(vm, v, &m);
    push_back(m, 0);
    return js_throw_msg(vm, vm->type_error_proto, data(m));
}
/* this as an ArrayBuffer: 0 with V8's TypeError thrown */
static uint32_t this_ab(js_vm *vm, jv thisv, const char *what) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_ARRAYBUFFER) return jv_idx(thisv);
    incompatible(vm, what, thisv);
    return 0;
}

/* UTF-8 of a string's code units, a lone surrogate as U+FFFD */
static void utf8_of(js_vm *vm, uint32_t s, vector<uint8_t> *out, uint32_t *read_units, uint32_t max_bytes) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off, i = 0;
    while (i < n) {
        uint32_t c = vm->units[o + i], w = 1;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i + 1] - 0xDC00u);
            w = 2;
        } else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        uint32_t need = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (len(*out) + need > max_bytes) break;
        if (c < 0x80) push_back(*out, (uint8_t)c);
        else if (c < 0x800) { push_back(*out, (uint8_t)(0xC0 | c >> 6)); push_back(*out, (uint8_t)(0x80 | (c & 63))); }
        else if (c < 0x10000) { push_back(*out, (uint8_t)(0xE0 | c >> 12)); push_back(*out, (uint8_t)(0x80 | (c >> 6 & 63))); push_back(*out, (uint8_t)(0x80 | (c & 63))); }
        else {
            push_back(*out, (uint8_t)(0xF0 | c >> 18));
            push_back(*out, (uint8_t)(0x80 | (c >> 12 & 63)));
            push_back(*out, (uint8_t)(0x80 | (c >> 6 & 63)));
            push_back(*out, (uint8_t)(0x80 | (c & 63)));
        }
        i += w;
    }
    *read_units = i;
}

/* the bytes [p, p + n) of vm->bytes as UTF-8, U+FFFD for each maximal bad subpart (the
   Encoding standard's decoder): 0 when fatal and one was met */
static int utf8_decode(js_vm *vm, uint32_t p, uint32_t n, int fatal, int bom, vector<uint16_t> *out) {
    uint32_t i = 0;
    if (!bom && n >= 3 && vm->bytes[p] == 0xEF && vm->bytes[p + 1] == 0xBB && vm->bytes[p + 2] == 0xBF) i = 3;
    while (i < n) {
        uint32_t c = vm->bytes[p + i];
        if (c < 0x80) { push_back(*out, (uint16_t)c); i++; continue; }
        uint32_t need = 0, cp = 0, lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else { if (fatal) return 0; push_back(*out, (uint16_t)0xFFFD); i++; continue; }
        uint32_t k = 1;
        int bad = 0;
        for (; k <= need; k++) {
            if (i + k >= n) { bad = 1; break; }
            uint32_t b = vm->bytes[p + i + k];
            if (b < lo || b > hi) { bad = 1; break; }
            lo = 0x80;
            hi = 0xBF;
            cp = cp << 6 | (b & 63);
        }
        if (bad) { if (fatal) return 0; push_back(*out, (uint16_t)0xFFFD); i += k; continue; }
        if (cp >= 0x10000) { push_back(*out, (uint16_t)(0xD800 + ((cp - 0x10000) >> 10))); push_back(*out, (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF))); }
        else push_back(*out, (uint16_t)cp);
        i += need + 1;
    }
    return 1;
}

/* a BufferSource's bytes: their first in vm->bytes, their count (none for a detached buffer) */
static int buffer_source(js_vm *vm, jv v, uint32_t *p, uint32_t *n) {
    if (!jv_is_obj(v)) return 0;
    uint32_t o = jv_idx(v), k = vm->objs[o].kind;
    if (k == JK_ARRAYBUFFER) { *p = vm->objs[o].a; *n = vm->objs[o].b; return 1; }
    if (k == JK_TYPEDARRAY) { *p = vm->objs[vm->objs[o].a].a + vm->objs[o].c; *n = vm->objs[o].b * ta_size[ta_type(vm, o)]; return 1; }
    if (k == JK_DATAVIEW) { *p = vm->objs[vm->objs[o].a].a + vm->objs[o].c; *n = vm->objs[o].b; return 1; }
    return 0;
}

/* a buffer's bytes given to a new one of n bytes (resizable as it was when keep), it detached */
static jv ab_transfer(js_vm *vm, jv thisv, jv lenv, int keep, const char *what) {
    uint32_t o = this_ab(vm, thisv, what);
    if (!o) return JV_HOLE;
    uint32_t n = vm->objs[o].b;
    if (lenv != JV_UNDEF && !to_index(vm, lenv, &n, "Invalid array buffer length")) return JV_HOLE;
    if (ab_detached(vm, o)) return ta_detached(vm, what);
    int rs = keep && ab_resizable(vm, o);
    double max = ab_max(vm, o);
    if (n > AB_MAX || (rs && (double)n > max)) return js_throw_msg(vm, vm->range_error_proto, "Invalid array buffer length");
    uint32_t r = rs ? ab_new_resizable(vm, n, max) : ab_new(vm, n);
    uint32_t m = n < vm->objs[o].b ? n : vm->objs[o].b, from = vm->objs[o].a, to = vm->objs[r].a;
    {
        uint8_t *bp = data(vm->bytes);
        memmove(bp + to, bp + from, m);
    }
    ab_detach(vm, o);
    return jv_obj(r);
}

/* the methods the typed arrays share with Array's, which js_typed_shared wraps */
static const char *const ta_shared[] = { "forEach", "every", "some", "find", "findIndex", "findLast", "findLastIndex", "indexOf",
                                         "lastIndexOf", "includes", "join", "reduce", "reduceRight", "at", "keys", "entries",
                                         "toLocaleString", "values" };
#define TA_NSHARED 18

jv js_typed_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    int constructing = newt != JV_UNDEF && newt != JV_HOLE;
    switch (which) {
    /* ---- ArrayBuffer ---- */
    case JTN_AB: {
        if (!constructing) return js_throw_msg(vm, vm->type_error_proto, "Constructor ArrayBuffer requires 'new'");
        uint32_t n;
        if (!to_index(vm, a0, &n, "Invalid array buffer length")) return JV_HOLE;
        /* a resizable one's maximum, from its options */
        double max = -1;
        if (jv_is_obj(a1)) {
            jv mv = js_get(vm, a1, js_atom_ascii(vm, "maxByteLength"));
            if (vm->throwing) return JV_HOLE;
            if (mv != JV_UNDEF) {
                double md = js_to_number(vm, mv);
                if (vm->throwing) return JV_HOLE;
                md = md != md ? 0 : trunc(md);
                if (md < 0 || md > 9007199254740990.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid array buffer max length");
                max = md;
            }
        }
        if (max >= 0 && (double)n > max) return js_throw_msg(vm, vm->range_error_proto, "Invalid array buffer max length");
        if (n > AB_MAX) return js_throw_msg(vm, vm->range_error_proto, "Array buffer allocation failed");
        return jv_obj(max >= 0 ? ab_new_resizable(vm, n, max) : ab_new(vm, n));
    }
    case JTN_AB_ISVIEW: return jv_bool(jv_is_obj(a0) && (vm->objs[jv_idx(a0)].kind == JK_TYPEDARRAY || vm->objs[jv_idx(a0)].kind == JK_DATAVIEW));
    case JTN_AB_BYTELENGTH: case JTN_AB_MAXLENGTH: case JTN_AB_RESIZABLE: case JTN_AB_DETACHED: {
        static const char *const gn[] = { "get ArrayBuffer.prototype.byteLength", "get ArrayBuffer.prototype.maxByteLength",
                                          "get ArrayBuffer.prototype.resizable", "get ArrayBuffer.prototype.detached" };
        uint32_t k = which == JTN_AB_BYTELENGTH ? 0 : which == JTN_AB_MAXLENGTH ? 1 : which == JTN_AB_RESIZABLE ? 2 : 3;
        uint32_t o = this_ab(vm, thisv, gn[k]);
        if (!o) return JV_HOLE;
        if (k == 0) return jv_num(vm->objs[o].b);
        if (k == 2) return jv_bool(ab_resizable(vm, o));
        if (k == 3) return jv_bool(ab_detached(vm, o));
        if (ab_detached(vm, o)) return jv_num(0);
        return jv_num(ab_resizable(vm, o) ? ab_max(vm, o) : (double)vm->objs[o].b);
    }
    case JTN_AB_SLICE: {
        uint32_t o = this_ab(vm, thisv, "ArrayBuffer.prototype.slice");
        if (!o) return JV_HOLE;
        if (ab_detached(vm, o)) return ta_detached(vm, "ArrayBuffer.prototype.slice");
        uint32_t n = vm->objs[o].b;
        uint32_t from = rel_index(vm, a0, n, 0), to = rel_index(vm, a1, n, n);
        if (vm->throwing) return JV_HOLE;
        if (ab_detached(vm, o)) return ta_detached(vm, "ArrayBuffer.prototype.slice");
        if (to > vm->objs[o].b) to = vm->objs[o].b;
        uint32_t m = to > from ? to - from : 0;
        uint32_t r = ab_new(vm, m);
        uint32_t src = vm->objs[o].a + from, dst = vm->objs[r].a;
        for (uint32_t i = 0; i < m; i++) vm->bytes[dst + i] = vm->bytes[src + i];
        return jv_obj(r);
    }
    case JTN_AB_RESIZE: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ARRAYBUFFER || !ab_resizable(vm, jv_idx(thisv)))
            return incompatible(vm, "ArrayBuffer.prototype.resize", thisv);
        uint32_t o = jv_idx(thisv), n;
        if (!to_index(vm, a0, &n, "ArrayBuffer.prototype.resize: Invalid length parameter")) return JV_HOLE;
        if (ab_detached(vm, o)) return ta_detached(vm, "ArrayBuffer.prototype.resize");
        if ((double)n > ab_max(vm, o) || n > AB_MAX) return js_throw_msg(vm, vm->range_error_proto, "ArrayBuffer.prototype.resize: Invalid length parameter");
        ab_resize(vm, o, n);
        return JV_UNDEF;
    }
    case JTN_AB_TRANSFER: return ab_transfer(vm, thisv, a0, 1, "ArrayBuffer.prototype.transfer");
    case JTN_AB_TRANSFER_FIXED: return ab_transfer(vm, thisv, a0, 0, "ArrayBuffer.prototype.transferToFixedLength");
    /* ---- the typed arrays ---- */
    case JTN_CTOR: {
        uint32_t t = ctor_type(vm, callee);
        if (!constructing) {
            char m[96];
            snprintf(m, sizeof m, "Constructor %s requires 'new'", ta_name[t]);
            return js_throw_msg(vm, vm->type_error_proto, m);
        }
        uint32_t proto = vm->ta_protos[t];
        if (jv_is_obj(newt) && newt != callee) {
            jv pv = js_get(vm, newt, A_PROTOTYPE);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_obj(pv)) proto = jv_idx(pv);
        }
        if (!jv_is_obj(a0)) {
            uint32_t n;
            double nd = 0;
            if (!to_index_num(vm, a0, &n, &nd, 0)) {
                if (!vm->throwing) range_num(vm, "Invalid typed array length: ", nd, "");
                return JV_HOLE;
            }
            if ((uint64_t)n * ta_size[t] > AB_MAX) return range_num(vm, "Invalid typed array length: ", n, "");
            return jv_obj(ta_new(vm, t, n, 0, 0, proto));
        }
        uint32_t src = jv_idx(a0);
        if (vm->objs[src].kind == JK_ARRAYBUFFER) {
            uint32_t o = ta_over(vm, t, src, a1, a2, proto);
            return o ? jv_obj(o) : JV_HOLE;
        }
        vector<jv> vals;
        if (!ta_values_of(vm, a0, &vals, "Construct")) return JV_HOLE;
        return ta_from_values(vm, t, &vals, proto);
    }
    case JTN_FROM: case JTN_OF: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_NATIVE) return js_throw_msg(vm, vm->type_error_proto, "TypedArray.from and of need a typed array's constructor");
        uint32_t t = ctor_type(vm, thisv);
        vector<jv> vals;
        if (which == JTN_OF) { for (uint32_t i = 0; i < argc; i++) push_back(vals, js_arg(vm, args, argc, i)); }
        else {
            if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_TYPEDARRAY && vw_oob(vm, jv_idx(a0))) return ta_detached(vm, "%TypedArray%.prototype.values");
            if (!ta_values_of(vm, a0, &vals, "%TypedArray%.prototype.values")) return JV_HOLE;
            if (a1 != JV_UNDEF) {
                if (!js_is_callable(vm, a1)) return js_type_error(vm, "is not a function", js_typeof(vm, a1));
                for (size_t i = 0; i < len(vals); i++) {
                    jv av[2] = { vals[i], jv_num((double)i) };
                    jv r = js_call(vm, a1, a2, av, 2);
                    if (vm->throwing) return JV_HOLE;
                    vals[i] = r;
                }
            }
        }
        return ta_from_values(vm, t, &vals, 0);
    }
    case JTN_BUFFER: case JTN_BYTELENGTH: case JTN_BYTEOFFSET: case JTN_LENGTH: {
        uint32_t o = this_ta(vm, thisv, "a typed array's getter");
        if (!o) return JV_HOLE;
        if (which == JTN_BUFFER) return jv_obj(vm->objs[o].a);
        if (which == JTN_LENGTH) return jv_num(vm->objs[o].b);
        if (which == JTN_BYTEOFFSET) return jv_num(vw_oob(vm, o) ? 0 : vm->objs[o].c);
        return jv_num((double)vm->objs[o].b * ta_size[ta_type(vm, o)]);
    }
    case JTN_TAG:
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_TYPEDARRAY) return JV_UNDEF;
        return jv_str(js_str_ascii(vm, ta_name[ta_type(vm, jv_idx(thisv))]));
    case JTN_SET: {
        uint32_t o = this_ta(vm, thisv, "%TypedArray%.prototype.set");
        if (!o) return JV_HOLE;
        uint32_t off;
        if (!to_index(vm, a1, &off, "offset is out of bounds")) return JV_HOLE;
        if (vw_oob(vm, o)) return ta_detached(vm, "%TypedArray%.prototype.set");
        vector<jv> vals;
        if (!jv_is_obj(a0)) { jv o2 = js_to_object(vm, a0); if (vm->throwing) return JV_HOLE; a0 = o2; }
        if (!ta_values_of(vm, a0, &vals, "%TypedArray%.prototype.set")) return JV_HOLE;
        if ((uint64_t)off + len(vals) > vm->objs[o].b) return js_throw_msg(vm, vm->range_error_proto, "offset is out of bounds");
        for (size_t i = 0; i < len(vals); i++) if (!js_ta_set(vm, o, off + (uint32_t)i, vals[i])) return JV_HOLE;
        return JV_UNDEF;
    }
    case JTN_SUBARRAY: {
        /* a new view of the same buffer: from the place begin is, tracking the buffer when this does and no end is given */
        uint32_t o = this_ta(vm, thisv, "%TypedArray%.prototype.subarray");
        if (!o) return JV_HOLE;
        uint32_t n = vm->objs[o].b, from = rel_index(vm, a0, n, 0), to = rel_index(vm, a1, n, n);
        if (vm->throwing) return JV_HOLE;
        uint32_t m = to > from ? to - from : 0;
        uint32_t t = ta_type(vm, o);
        double begin = (double)vm->objs[o].c + (double)from * ta_size[t];
        jv lenv = vw_declared(vm, o) == TA_TRACK && a1 == JV_UNDEF ? JV_UNDEF : jv_num(m);
        uint32_t r = ta_over(vm, t, vm->objs[o].a, jv_num(begin), lenv, vm->objs[o].proto);
        return r ? jv_obj(r) : JV_HOLE;
    }
    case JTN_SLICE: {
        uint32_t o = valid_ta(vm, thisv, "slice");
        if (!o) return JV_HOLE;
        uint32_t n = vm->objs[o].b, from = rel_index(vm, a0, n, 0), to = rel_index(vm, a1, n, n);
        if (vm->throwing) return JV_HOLE;
        uint32_t m = to > from ? to - from : 0;
        uint32_t r = ta_like(vm, o, m);
        if (vw_oob(vm, o)) return ta_detached(vm, "%TypedArray%.prototype.slice");
        if (to > vm->objs[o].b) to = vm->objs[o].b;
        uint32_t k = to > from ? to - from : 0;
        uint32_t sz = ta_size[ta_type(vm, o)];
        uint32_t src = ta_at(vm, o, from), dst = ta_at(vm, r, 0);
        for (uint32_t i = 0; i < k * sz; i++) vm->bytes[dst + i] = vm->bytes[src + i];
        return jv_obj(r);
    }
    case JTN_FILL: {
        uint32_t o = valid_ta(vm, thisv, "fill");
        if (!o) return JV_HOLE;
        uint32_t t = ta_type(vm, o);
        jv bv = JV_UNDEF;
        double d = 0;
        if (t >= JTA_BIGINT64) bv = js_to_bigint(vm, a0);
        else d = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        uint32_t n = vm->objs[o].b, from = rel_index(vm, a1, n, 0), to = rel_index(vm, a2, n, n);
        if (vm->throwing) return JV_HOLE;
        if (vw_oob(vm, o)) return ta_detached(vm, "%TypedArray%.prototype.fill");
        if (to > vm->objs[o].b) to = vm->objs[o].b;
        for (uint32_t i = from; i < to; i++) {
            if (t >= JTA_BIGINT64) ta_write64(vm, ta_at(vm, o, i), js_big_to_u64(vm, bv), 1);
            else ta_write(vm, t, ta_at(vm, o, i), d, 1);
        }
        return thisv;
    }
    case JTN_MAP: case JTN_FILTER: {
        uint32_t o = valid_ta(vm, thisv, which == JTN_MAP ? "map" : "filter");
        if (!o) return JV_HOLE;
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        uint32_t n = vm->objs[o].b;
        vector<jv> vals;
        for (uint32_t i = 0; i < n; i++) {
            jv v = i < vm->objs[o].b ? js_ta_get(vm, o, i) : JV_UNDEF;
            jv av[3] = { v, jv_num(i), thisv };
            jv r = js_call(vm, a0, a1, av, 3);
            if (vm->throwing) return JV_HOLE;
            if (which == JTN_MAP) push_back(vals, r);
            else if (js_truthy(vm, r)) push_back(vals, v);
        }
        return ta_from_values(vm, ta_type(vm, o), &vals, vm->objs[o].proto);
    }
    case JTN_SORT: case JTN_TOSORTED: {
        uint32_t o = valid_ta(vm, thisv, which == JTN_SORT ? "sort" : "toSorted");
        if (!o) return JV_HOLE;
        if (a0 != JV_UNDEF && !js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "The comparison function must be either a function or undefined");
        uint32_t n = vm->objs[o].b;
        vector<jv> vals;
        for (uint32_t i = 0; i < n; i++) push_back(vals, js_ta_get(vm, o, i));
        /* a merge sort, stable, the comparator's or the numbers' order */
        vector<jv> tmp;
        resize(tmp, len(vals), JV_UNDEF);
        for (size_t w = 1; w < len(vals); w *= 2) {
            for (size_t lo = 0; lo < len(vals); lo += 2 * w) {
                size_t mid = lo + w < len(vals) ? lo + w : len(vals), hi = lo + 2 * w < len(vals) ? lo + 2 * w : len(vals);
                size_t x = lo, y = mid, k = lo;
                while (x < mid && y < hi) {
                    int c;
                    if (a0 == JV_UNDEF && jv_is_big(vals[y])) c = js_big_cmp(vm, vals[y], vals[x]);
                    else if (a0 == JV_UNDEF) c = cmp_num(jv_dbl(vals[y]), jv_dbl(vals[x]));
                    else {
                        jv av[2] = { vals[y], vals[x] };
                        jv r = js_call(vm, a0, JV_UNDEF, av, 2);
                        if (vm->throwing) return JV_HOLE;
                        double d = js_to_number(vm, r);
                        if (vm->throwing) return JV_HOLE;
                        c = d < 0 ? -1 : 0;
                    }
                    if (c < 0) tmp[k++] = vals[y++];
                    else tmp[k++] = vals[x++];
                }
                while (x < mid) tmp[k++] = vals[x++];
                while (y < hi) tmp[k++] = vals[y++];
            }
            for (size_t k = 0; k < len(vals); k++) vals[k] = tmp[k];
        }
        uint32_t dst = which == JTN_SORT ? o : ta_like(vm, o, n);
        for (uint32_t i = 0; i < n && i < vm->objs[dst].b; i++) if (!js_ta_set(vm, dst, i, vals[i])) return JV_HOLE;
        return jv_obj(dst);
    }
    case JTN_REVERSE: case JTN_TOREVERSED: {
        uint32_t o = valid_ta(vm, thisv, which == JTN_REVERSE ? "reverse" : "toReversed");
        if (!o) return JV_HOLE;
        uint32_t n = vm->objs[o].b;
        uint32_t dst = which == JTN_REVERSE ? o : ta_like(vm, o, n);
        /* the elements' bytes, whatever their type, in the other order */
        uint32_t sz = ta_size[ta_type(vm, o)];
        vector<uint8_t> tmp;
        uint32_t src = ta_at(vm, o, 0);
        for (uint32_t i = 0; i < n * sz; i++) push_back(tmp, vm->bytes[src + i]);
        uint32_t d0 = ta_at(vm, dst, 0);
        for (uint32_t i = 0; i < n; i++) for (uint32_t k = 0; k < sz; k++) vm->bytes[d0 + i * sz + k] = tmp[(n - 1 - i) * sz + k];
        return jv_obj(dst);
    }
    case JTN_WITH: {
        uint32_t o = valid_ta(vm, thisv, "with");
        if (!o) return JV_HOLE;
        uint32_t n = vm->objs[o].b;
        double d = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        d = d != d ? 0 : trunc(d);
        if (d < 0) d += n;
        jv v = ta_type(vm, o) >= JTA_BIGINT64 ? js_to_bigint(vm, a1) : jv_num(js_to_number(vm, a1));
        if (vm->throwing) return JV_HOLE;
        if (d < 0 || d >= vm->objs[o].b) return js_throw_msg(vm, vm->range_error_proto, "Invalid typed array index");
        uint32_t r = ta_like(vm, o, n);
        uint32_t sz = ta_size[ta_type(vm, o)], src = ta_at(vm, o, 0), dst = ta_at(vm, r, 0);
        for (uint32_t i = 0; i < n * sz; i++) vm->bytes[dst + i] = vm->bytes[src + i];
        if (!js_ta_set(vm, r, (uint32_t)d, v)) return JV_HOLE;
        return jv_obj(r);
    }
    case JTN_COPYWITHIN: {
        uint32_t o = valid_ta(vm, thisv, "copyWithin");
        if (!o) return JV_HOLE;
        uint32_t n = vm->objs[o].b, to = rel_index(vm, a0, n, 0), from = rel_index(vm, a1, n, 0), end = rel_index(vm, a2, n, n);
        if (vm->throwing) return JV_HOLE;
        if (vw_oob(vm, o)) return ta_detached(vm, "%TypedArray%.prototype.copyWithin");
        n = vm->objs[o].b;
        if (end > n) end = n;
        if (end <= from || to >= n) return thisv;
        uint32_t cnt = end - from;
        if (cnt > n - to) cnt = n - to;
        uint32_t sz = ta_size[ta_type(vm, o)], src = ta_at(vm, o, from), dst = ta_at(vm, o, to);
        vector<uint8_t> tmp;
        for (uint32_t i = 0; i < cnt * sz; i++) push_back(tmp, vm->bytes[src + i]);
        for (uint32_t i = 0; i < cnt * sz; i++) vm->bytes[dst + i] = tmp[i];
        return thisv;
    }
    case JTN_SHARED: {
        /* a shared method's wrapper: a typed array out of its buffer's bounds refused, then
           the method itself (in the callee's x), named in V8's words (its findLastIndex is
           findIndexLast) */
        jv fn = vm->objs[jv_idx(callee)].x;
        if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_TYPEDARRAY && vw_oob(vm, jv_idx(thisv))) {
            vector<char> w;
            const char *pre = "%TypedArray%.prototype.";
            append_range(w, pre, strlen(pre));
            uint32_t name = vm->objs[jv_idx(callee)].c;
            if (name == js_atom_ascii(vm, "findLastIndex")) { const char *q = "findIndexLast"; append_range(w, q, strlen(q)); }
            else js_str_to_utf8(vm, name, &w);
            push_back(w, 0);
            return ta_detached(vm, data(w));
        }
        if (argc <= 4) {
            jv av[4] = { a0, a1, a2, js_arg(vm, args, argc, 3) };
            return js_call(vm, fn, thisv, av, argc);
        }
        vector<jv> av;
        for (uint32_t i = 0; i < argc; i++) push_back(av, js_arg(vm, args, argc, i));
        return js_call(vm, fn, thisv, data(av), argc);
    }
    /* ---- DataView ---- */
    case JTN_DV: {
        if (!constructing) return js_throw_msg(vm, vm->type_error_proto, "Constructor DataView requires 'new'");
        if (!jv_is_obj(a0) || vm->objs[jv_idx(a0)].kind != JK_ARRAYBUFFER) return js_throw_msg(vm, vm->type_error_proto, "First argument to DataView constructor must be an ArrayBuffer");
        uint32_t buf = jv_idx(a0), off = 0, n = 0;
        double od = 0, nd = 0;
        if (!to_index_num(vm, a1, &off, &od, 0)) {
            if (!vm->throwing) range_num(vm, "Start offset ", od, " is outside the bounds of the buffer");
            return JV_HOLE;
        }
        if (ab_detached(vm, buf)) return ta_detached(vm, "DataView constructor");
        uint32_t blen = vm->objs[buf].b, d = 0;
        if (off > blen) return range_num(vm, "Start offset ", off, " is outside the bounds of the buffer");
        if (a2 == JV_UNDEF) d = ab_resizable(vm, buf) ? TA_TRACK : blen - off;
        else {
            if (!to_index_num(vm, a2, &n, &nd, 0)) {
                if (!vm->throwing) range_num(vm, "Invalid DataView length ", nd, "");
                return JV_HOLE;
            }
            if ((uint64_t)off + n > blen) return range_num(vm, "Invalid DataView length ", n, "");
            d = n;
        }
        uint32_t o = js_obj_new(vm, JK_DATAVIEW, vm->dataview_proto);
        vm->objs[o].c = off;
        vw_link(vm, o, buf, d, 0);
        return jv_obj(o);
    }
    case JTN_DV_BUFFER: case JTN_DV_BYTELENGTH: case JTN_DV_BYTEOFFSET: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_DATAVIEW) return js_throw_msg(vm, vm->type_error_proto, "a DataView's getter called on an object that is not one");
        uint32_t o = jv_idx(thisv);
        if (which == JTN_DV_BUFFER) return jv_obj(vm->objs[o].a);
        if (vw_oob(vm, o)) return ta_detached(vm, which == JTN_DV_BYTELENGTH ? "get DataView.prototype.byteLength" : "get DataView.prototype.byteOffset");
        return jv_num(which == JTN_DV_BYTELENGTH ? vm->objs[o].b : vm->objs[o].c);
    }
    case JTN_DV_GET: case JTN_DV_SET: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_DATAVIEW) return js_throw_msg(vm, vm->type_error_proto, "a DataView's method called on an object that is not one");
        /* the element type in the callee's x */
        uint32_t t = (uint32_t)jv_dbl(vm->objs[jv_idx(callee)].x), dv = jv_idx(thisv);
        uint32_t off, p;
        if (!to_index(vm, a0, &off, "Offset is outside the bounds of the DataView")) return JV_HOLE;
        if (which == JTN_DV_GET) {
            int little = js_truthy(vm, a1);
            if (!dv_place(vm, dv, off, ta_size[t], &p, 0, t)) return JV_HOLE;
            if (t >= JTA_BIGINT64) return ta_big_of(vm, t, ta_read64(vm, p, little));
            return jv_num(ta_read(vm, t, p, little));
        }
        if (t >= JTA_BIGINT64) {
            jv b = js_to_bigint(vm, a1);
            if (vm->throwing) return JV_HOLE;
            int little = js_truthy(vm, a2);
            if (!dv_place(vm, dv, off, 8, &p, 1, t)) return JV_HOLE;
            ta_write64(vm, p, js_big_to_u64(vm, b), little);
            return JV_UNDEF;
        }
        double d = js_to_number(vm, a1);
        if (vm->throwing) return JV_HOLE;
        int little = js_truthy(vm, a2);
        if (!dv_place(vm, dv, off, ta_size[t], &p, 1, t)) return JV_HOLE;
        ta_write(vm, t, p, d, little);
        return JV_UNDEF;
    }
    /* ---- TextEncoder and TextDecoder: UTF-8 ---- */
    case JTN_TE: case JTN_TD: {
        if (!constructing) return js_throw_msg(vm, vm->type_error_proto, which == JTN_TE ? "Constructor TextEncoder requires 'new'" : "Constructor TextDecoder requires 'new'");
        uint32_t o = js_obj_new(vm, JK_PLAIN, which == JTN_TE ? vm->textenc_proto : vm->textdec_proto);
        if (which == JTN_TD) {
            if (a0 != JV_UNDEF) {
                uint32_t l = js_to_string(vm, a0);
                if (!l) return JV_HOLE;
                vector<char> b;
                js_str_to_utf8(vm, l, &b);
                push_back(b, 0);
                const char *ok[] = { "utf-8", "utf8", "unicode-1-1-utf-8", "unicode11utf8", "unicode20utf8", "x-unicode20utf8" };
                int good = 0;
                for (uint32_t i = 0; i < 6; i++) if (!strcasecmp(data(b), ok[i])) good = 1;
                if (!good) {
                    char m[200];
                    snprintf(m, sizeof m, "The encoding label provided ('%s') is invalid.", data(b));
                    return js_throw_msg(vm, vm->range_error_proto, m);
                }
            }
            uint32_t fl = 0;
            if (jv_is_obj(a1)) {
                jv f = js_get(vm, a1, js_atom_ascii(vm, "fatal"));
                jv b = js_get(vm, a1, js_atom_ascii(vm, "ignoreBOM"));
                if (vm->throwing) return JV_HOLE;
                fl = (js_truthy(vm, f) ? 1u : 0u) | (js_truthy(vm, b) ? 2u : 0u);
            }
            vm->objs[o].a = fl;
            vm->objs[o].flags |= JO_WEAK;      /* marks it a decoder (no collection reads it) */
        }
        return jv_obj(o);
    }
    case JTN_TE_ENCODING: case JTN_TD_ENCODING: return jv_str(js_str_ascii(vm, "utf-8"));
    case JTN_TD_FATAL: case JTN_TD_IGNOREBOM:
        if (!jv_is_obj(thisv)) return JV_UNDEF;
        return jv_bool((vm->objs[jv_idx(thisv)].a & (which == JTN_TD_FATAL ? 1u : 2u)) != 0);
    case JTN_TE_ENCODE: case JTN_TE_ENCODEINTO: {
        uint32_t s = a0 == JV_UNDEF ? A_EMPTY : js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        vector<uint8_t> b;
        uint32_t read = 0;
        if (which == JTN_TE_ENCODE) {
            utf8_of(vm, s, &b, &read, 0xFFFFFFFFu);
            uint32_t r = ta_new(vm, JTA_UINT8, (uint32_t)len(b), 0, 0, 0);
            uint32_t dst = ta_at(vm, r, 0);
            for (size_t i = 0; i < len(b); i++) vm->bytes[dst + i] = b[i];
            return jv_obj(r);
        }
        if (!jv_is_obj(a1) || vm->objs[jv_idx(a1)].kind != JK_TYPEDARRAY || ta_type(vm, jv_idx(a1)) != JTA_UINT8)
            return js_throw_msg(vm, vm->type_error_proto, "TextEncoder.encodeInto's second argument must be a Uint8Array");
        uint32_t u = jv_idx(a1);
        utf8_of(vm, s, &b, &read, vm->objs[u].b);
        uint32_t dst = ta_at(vm, u, 0);
        for (size_t i = 0; i < len(b); i++) vm->bytes[dst + i] = b[i];
        uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        js_define(vm, r, js_atom_ascii(vm, "read"), jv_num(read), 0);
        js_define(vm, r, js_atom_ascii(vm, "written"), jv_num((double)len(b)), 0);
        return jv_obj(r);
    }
    case JTN_TD_DECODE: {
        uint32_t fl = jv_is_obj(thisv) ? vm->objs[jv_idx(thisv)].a : 0;
        uint32_t p = 0, n = 0;
        if (a0 != JV_UNDEF && !buffer_source(vm, a0, &p, &n))
            return js_throw_msg(vm, vm->type_error_proto, "TextDecoder.decode's argument must be an ArrayBuffer or an ArrayBufferView");
        vector<uint16_t> out;
        if (!utf8_decode(vm, p, n, (fl & 1) != 0, (fl & 2) != 0, &out)) return js_throw_msg(vm, vm->type_error_proto, "The encoded data was not valid.");
        return jv_str(js_str_new(vm, data(out), len(out)));
    }
    default: return JV_UNDEF;
    }
}

static void td_def(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t nargs) {
    uint32_t atom = js_atom_ascii(vm, name);
    js_define(vm, o, atom, jv_obj(js_native_new(vm, id, atom, nargs)), JS_SHAPE_HIDDEN);
}
static void td_get(js_vm *vm, uint32_t o, const char *name, uint32_t id) {
    uint32_t atom = js_atom_ascii(vm, name);
    js_define_accessor(vm, o, atom, jv_obj(js_native_new(vm, id, atom, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
}
static uint32_t td_ctor(js_vm *vm, const char *name, uint32_t id, uint32_t nargs, uint32_t proto) {
    uint32_t atom = js_atom_ascii(vm, name);
    uint32_t c = js_native_new(vm, id, atom, nargs);
    js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_HIDDEN);
    js_define(vm, proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, atom, jv_obj(c), JS_SHAPE_HIDDEN);
    return c;
}

/* ArrayBuffer, %TypedArray% and each typed array, DataView, TextEncoder and TextDecoder;
   first, the native id of JTN_AB. The methods the typed arrays share with Array's
   (forEach, join, indexOf and their like) are builtins.c's, put on %TypedArray%.prototype there */
void js_typed_init(js_vm *vm, uint32_t first) {
    vm->arraybuffer_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    uint32_t ab = td_ctor(vm, "ArrayBuffer", first + JTN_AB, 1, vm->arraybuffer_proto);
    td_def(vm, ab, "isView", first + JTN_AB_ISVIEW, 1);
    td_get(vm, vm->arraybuffer_proto, "byteLength", first + JTN_AB_BYTELENGTH);
    td_get(vm, vm->arraybuffer_proto, "maxByteLength", first + JTN_AB_MAXLENGTH);
    td_get(vm, vm->arraybuffer_proto, "resizable", first + JTN_AB_RESIZABLE);
    td_get(vm, vm->arraybuffer_proto, "detached", first + JTN_AB_DETACHED);
    td_def(vm, vm->arraybuffer_proto, "slice", first + JTN_AB_SLICE, 2);
    td_def(vm, vm->arraybuffer_proto, "resize", first + JTN_AB_RESIZE, 1);
    td_def(vm, vm->arraybuffer_proto, "transfer", first + JTN_AB_TRANSFER, 0);
    td_def(vm, vm->arraybuffer_proto, "transferToFixedLength", first + JTN_AB_TRANSFER_FIXED, 0);
    js_define(vm, vm->arraybuffer_proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "ArrayBuffer")), JS_SHAPE_HIDDEN);
    /* %TypedArray%: the constructors' prototype, and its prototype the arrays' */
    vm->typedarray_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    uint32_t ta = js_native_new(vm, first + JTN_CTOR, js_atom_ascii(vm, "TypedArray"), 0);
    vm->objs[ta].x = jv_num(JTA_UINT8);
    js_define(vm, ta, A_PROTOTYPE, jv_obj(vm->typedarray_proto), JS_SHAPE_HIDDEN);
    js_define(vm, vm->typedarray_proto, A_CONSTRUCTOR, jv_obj(ta), JS_SHAPE_HIDDEN);
    td_get(vm, vm->typedarray_proto, "buffer", first + JTN_BUFFER);
    td_get(vm, vm->typedarray_proto, "byteLength", first + JTN_BYTELENGTH);
    td_get(vm, vm->typedarray_proto, "byteOffset", first + JTN_BYTEOFFSET);
    td_get(vm, vm->typedarray_proto, "length", first + JTN_LENGTH);
    js_define_accessor(vm, vm->typedarray_proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_obj(js_native_new(vm, first + JTN_TAG, A_EMPTY, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    static const char *const mn[] = { "set", "subarray", "slice", "fill", "map", "filter", "sort", "toSorted", "reverse", "toReversed", "with", "copyWithin" };
    static const uint32_t mi[] = { JTN_SET, JTN_SUBARRAY, JTN_SLICE, JTN_FILL, JTN_MAP, JTN_FILTER, JTN_SORT, JTN_TOSORTED, JTN_REVERSE, JTN_TOREVERSED, JTN_WITH, JTN_COPYWITHIN };
    static const uint32_t ma[] = { 1, 2, 2, 1, 1, 1, 1, 1, 0, 0, 2, 2 };
    for (uint32_t i = 0; i < 12; i++) td_def(vm, vm->typedarray_proto, mn[i], first + mi[i], ma[i]);
    for (uint32_t t = 0; t < JTA_N; t++) {
        uint32_t p = js_obj_new(vm, JK_PLAIN, vm->typedarray_proto);
        vm->ta_protos[t] = p;
        uint32_t c = td_ctor(vm, ta_name[t], first + JTN_CTOR, 3, p);
        vm->objs[c].x = jv_num(t);
        js_set_proto(vm, c, ta);
        js_define(vm, c, js_atom_ascii(vm, "BYTES_PER_ELEMENT"), jv_num(ta_size[t]), JS_SHAPE_HIDDEN);
        js_define(vm, p, js_atom_ascii(vm, "BYTES_PER_ELEMENT"), jv_num(ta_size[t]), JS_SHAPE_HIDDEN);
        td_def(vm, c, "from", first + JTN_FROM, 1);
        td_def(vm, c, "of", first + JTN_OF, 0);
    }
    vm->dataview_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    td_ctor(vm, "DataView", first + JTN_DV, 1, vm->dataview_proto);
    td_get(vm, vm->dataview_proto, "buffer", first + JTN_DV_BUFFER);
    td_get(vm, vm->dataview_proto, "byteLength", first + JTN_DV_BYTELENGTH);
    td_get(vm, vm->dataview_proto, "byteOffset", first + JTN_DV_BYTEOFFSET);
    js_define(vm, vm->dataview_proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "DataView")), JS_SHAPE_HIDDEN);
    static const char *const dn[] = { "Int8", "Uint8", "", "Int16", "Uint16", "Int32", "Uint32", "Float32", "Float64", "BigInt64", "BigUint64" };
    for (uint32_t t = 0; t < JTA_N; t++) {
        if (t == JTA_UINT8C) continue;
        char gname[24], sname[24];
        snprintf(gname, sizeof gname, "get%s", dn[t]);
        snprintf(sname, sizeof sname, "set%s", dn[t]);
        uint32_t ga = js_atom_ascii(vm, gname), sa = js_atom_ascii(vm, sname);
        uint32_t g = js_native_new(vm, first + JTN_DV_GET, ga, 1), s = js_native_new(vm, first + JTN_DV_SET, sa, 2);
        vm->objs[g].x = jv_num(t);
        vm->objs[s].x = jv_num(t);
        js_define(vm, vm->dataview_proto, ga, jv_obj(g), JS_SHAPE_HIDDEN);
        js_define(vm, vm->dataview_proto, sa, jv_obj(s), JS_SHAPE_HIDDEN);
    }
    vm->textenc_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->textdec_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    td_ctor(vm, "TextEncoder", first + JTN_TE, 0, vm->textenc_proto);
    td_ctor(vm, "TextDecoder", first + JTN_TD, 0, vm->textdec_proto);
    td_get(vm, vm->textenc_proto, "encoding", first + JTN_TE_ENCODING);
    td_def(vm, vm->textenc_proto, "encode", first + JTN_TE_ENCODE, 0);
    td_def(vm, vm->textenc_proto, "encodeInto", first + JTN_TE_ENCODEINTO, 2);
    td_get(vm, vm->textdec_proto, "encoding", first + JTN_TD_ENCODING);
    td_get(vm, vm->textdec_proto, "fatal", first + JTN_TD_FATAL);
    td_get(vm, vm->textdec_proto, "ignoreBOM", first + JTN_TD_IGNOREBOM);
    td_def(vm, vm->textdec_proto, "decode", first + JTN_TD_DECODE, 0);
}

/* the shared methods on %TypedArray%.prototype wrapped (builtins.c calls this once it has
   put them there): each wrapper keeps the method in its x, and has its name and length;
   values is Symbol.iterator too. The first native id is ArrayBuffer's, less its case's */
void js_typed_shared(js_vm *vm) {
    uint32_t tp = vm->typedarray_proto;
    jv abc = js_get(vm, jv_obj(vm->arraybuffer_proto), A_CONSTRUCTOR);
    if (!jv_is_obj(abc) || vm->objs[jv_idx(abc)].kind != JK_NATIVE) return;
    uint32_t first = vm->objs[jv_idx(abc)].a - JTN_AB;
    jv values = JV_UNDEF;
    for (uint32_t k = 0; k < TA_NSHARED; k++) {
        uint32_t atom = js_atom_ascii(vm, ta_shared[k]);
        jv fn = js_get(vm, jv_obj(tp), atom);
        if (!jv_is_obj(fn)) continue;
        jv lv = js_get(vm, fn, A_LENGTH);
        uint32_t w = js_native_new(vm, first + JTN_SHARED, atom, jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0);
        vm->objs[w].x = fn;
        js_define(vm, tp, atom, jv_obj(w), JS_SHAPE_HIDDEN);
        if (k == TA_NSHARED - 1) values = jv_obj(w);
    }
    if (values != JV_UNDEF) js_define(vm, tp, JS_SYM_KEY | JSYM_ITERATOR, values, JS_SHAPE_HIDDEN);
    vm->throwing = 0;
}
