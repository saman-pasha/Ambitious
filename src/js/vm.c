/* src/js/vm.c -- the engine's heart: strings and atoms, shapes and objects, properties
   (an array's and an arguments object's elements among them, each with its attributes),
   conversions, errors in V8's words, and the interpreter (src/js/js.h says what each is).

   What it does not do is docs/limitations.md's JavaScript row: among it, a sloppy
   function's arguments object is not mapped to its parameters, and an assignment to a
   let or const before its declaration is no ReferenceError. */
#include <stdlib.h>
#include <math.h>
#include "js.h"

jv js_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i) {
    if (i >= argc) return JV_UNDEF;
    jv v = vm->stack[args + i];
    return v;
}

/* ---- strings ---- */

/* a string's record: a freed one's taken first (src/js/gc.c) */
static uint32_t js_str_push(js_vm *vm, js_str s) {
    vm->gc_allocs++;
    if (vm->str_free) {
        uint32_t i = vm->str_free;
        vm->str_free = vm->strs[i].left;
        vm->strs[i] = s;
        return i;
    }
    push_back(vm->strs, s);
    return (uint32_t)(len(vm->strs) - 1);
}

uint32_t js_str_new(js_vm *vm, const uint16_t *u, size_t n) {
    js_str s;
    memset(&s, 0, sizeof s);
    s.off = (uint32_t)len(vm->units);
    s.len = (uint32_t)n;
    append_range(vm->units, u, n);
    return js_str_push(vm, s);
}

/* UTF-8 into UTF-16; a malformed sequence is U+FFFD */
uint32_t js_str_utf8(js_vm *vm, const char *p, size_t n) {
    js_str s;
    memset(&s, 0, sizeof s);
    s.off = (uint32_t)len(vm->units);
    size_t i = 0;
    while (i < n) {
        uint32_t c = (uint8_t)p[i], cp = 0xFFFD;
        size_t k = 1;
        if (c < 0x80) cp = c;
        else if (c >= 0xC2 && c <= 0xDF && i + 1 < n && ((uint8_t)p[i + 1] & 0xC0) == 0x80) { cp = (c & 0x1F) << 6 | ((uint8_t)p[i + 1] & 0x3F); k = 2; }
        else if (c >= 0xE0 && c <= 0xEF && i + 2 < n && ((uint8_t)p[i + 1] & 0xC0) == 0x80 && ((uint8_t)p[i + 2] & 0xC0) == 0x80) {
            cp = (c & 0x0F) << 12 | ((uint8_t)p[i + 1] & 0x3F) << 6 | ((uint8_t)p[i + 2] & 0x3F);
            k = 3;
            if (cp < 0x800) cp = 0xFFFD;
        } else if (c >= 0xF0 && c <= 0xF4 && i + 3 < n && ((uint8_t)p[i + 1] & 0xC0) == 0x80 && ((uint8_t)p[i + 2] & 0xC0) == 0x80 &&
                   ((uint8_t)p[i + 3] & 0xC0) == 0x80) {
            cp = (c & 0x07) << 18 | ((uint8_t)p[i + 1] & 0x3F) << 12 | ((uint8_t)p[i + 2] & 0x3F) << 6 | ((uint8_t)p[i + 3] & 0x3F);
            k = 4;
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
        }
        if (cp >= 0x10000) {
            push_back(vm->units, (uint16_t)(0xD800 + ((cp - 0x10000) >> 10)));
            push_back(vm->units, (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else push_back(vm->units, (uint16_t)cp);
        i += k;
    }
    s.len = (uint32_t)(len(vm->units) - s.off);
    return js_str_push(vm, s);
}

uint32_t js_str_ascii(js_vm *vm, const char *p) { return js_str_utf8(vm, p, strlen(p)); }

/* a rope's text written out, its leaves in order (no recursion: a loop's += makes deep ropes) */
void js_str_flatten(js_vm *vm, uint32_t s) {
    if (!vm->strs[s].rope) return;
    uint32_t off = (uint32_t)len(vm->units);
    vector<uint32_t> st;
    push_back(st, s);
    while (len(st)) {
        uint32_t x = back(st);
        pop_back(st);
        if (vm->strs[x].rope) {
            push_back(st, vm->strs[x].right);
            push_back(st, vm->strs[x].left);
            continue;
        }
        uint32_t o = vm->strs[x].off, n = vm->strs[x].len;
        for (uint32_t i = 0; i < n; i++) push_back(vm->units, vm->units[o + i]);
    }
    vm->strs[s].off = off;
    vm->strs[s].rope = 0;
    vm->strs[s].left = 0;
    vm->strs[s].right = 0;
}

uint32_t js_str_concat(js_vm *vm, uint32_t a, uint32_t b) {
    uint32_t la = vm->strs[a].len, lb = vm->strs[b].len;
    if (la == 0) return b;
    if (lb == 0) return a;
    js_str r;
    memset(&r, 0, sizeof r);
    r.len = la + lb;
    if (la + lb <= 24) {
        /* a short one copied flat: the units grown once, then written through their borrow */
        js_str_flatten(vm, a);
        js_str_flatten(vm, b);
        uint32_t oa = vm->strs[a].off, ob = vm->strs[b].off;
        size_t at = len(vm->units);
        resize(vm->units, at + la + lb, 0);
        uint16_t *u = data(vm->units);
        for (uint32_t i = 0; i < la; i++) u[at + i] = u[oa + i];
        for (uint32_t i = 0; i < lb; i++) u[at + la + i] = u[ob + i];
        r.off = (uint32_t)at;
        return js_str_push(vm, r);
    }
    r.rope = 1;
    r.left = a;
    r.right = b;
    return js_str_push(vm, r);
}

uint32_t js_str_sub(js_vm *vm, uint32_t s, uint32_t from, uint32_t to) {
    js_str_flatten(vm, s);
    if (from >= to) return A_EMPTY;
    if (from == 0 && to == vm->strs[s].len) return s;
    js_str r;
    memset(&r, 0, sizeof r);
    r.off = vm->strs[s].off + from;
    r.len = to - from;
    return js_str_push(vm, r);
}

uint16_t js_str_at(js_vm *vm, uint32_t s, uint32_t i) {
    js_str_flatten(vm, s);
    return vm->units[vm->strs[s].off + i];
}

uint32_t js_str_hash(js_vm *vm, uint32_t s) {
    if (vm->strs[s].hash) return vm->strs[s].hash;
    js_str_flatten(vm, s);
    uint32_t h = 2166136261u, o = vm->strs[s].off, n = vm->strs[s].len;
    for (uint32_t i = 0; i < n; i++) { h ^= vm->units[o + i]; h *= 16777619u; }
    h |= 1;
    vm->strs[s].hash = h;
    return h;
}

int js_str_eq(js_vm *vm, uint32_t a, uint32_t b) {
    if (a == b) return 1;
    uint32_t n = vm->strs[a].len;
    if (n != vm->strs[b].len) return 0;
    if (vm->strs[a].atom && vm->strs[a].atom == vm->strs[b].atom) return 1;
    if (vm->strs[a].hash && vm->strs[b].hash && vm->strs[a].hash != vm->strs[b].hash) return 0;
    js_str_flatten(vm, a);
    js_str_flatten(vm, b);
    uint32_t oa = vm->strs[a].off, ob = vm->strs[b].off;
    for (uint32_t i = 0; i < n; i++) if (vm->units[oa + i] != vm->units[ob + i]) return 0;
    return 1;
}

int js_str_cmp(js_vm *vm, uint32_t a, uint32_t b) {
    js_str_flatten(vm, a);
    js_str_flatten(vm, b);
    uint32_t na = vm->strs[a].len, nb = vm->strs[b].len, oa = vm->strs[a].off, ob = vm->strs[b].off;
    uint32_t n = na < nb ? na : nb;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t x = vm->units[oa + i], y = vm->units[ob + i];
        if (x != y) return x < y ? -1 : 1;
    }
    return na < nb ? -1 : na > nb ? 1 : 0;
}

void js_str_to_utf8(js_vm *vm, uint32_t s, vector<char> *out) {
    js_str_flatten(vm, s);
    uint32_t o = vm->strs[s].off, n = vm->strs[s].len;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = vm->units[o + i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i + 1] - 0xDC00u);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) push_back(out, (char)c);
        else if (c < 0x800) { push_back(out, (char)(0xC0 | c >> 6)); push_back(out, (char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) {
            push_back(out, (char)(0xE0 | c >> 12));
            push_back(out, (char)(0x80 | (c >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (c & 0x3F)));
        } else {
            push_back(out, (char)(0xF0 | c >> 18));
            push_back(out, (char)(0x80 | (c >> 12 & 0x3F)));
            push_back(out, (char)(0x80 | (c >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (c & 0x3F)));
        }
    }
}

/* ---- atoms: interned strings, a name's identity ---- */

static void js_atoms_grow(js_vm *vm) {
    size_t cap = len(vm->atom_slots) * 2;
    vector<uint32_t> old;
    append_range(old, data(vm->atom_slots), len(vm->atom_slots));
    clear(vm->atom_slots);
    resize(vm->atom_slots, cap);
    for (size_t i = 0; i < len(old); i++) {
        uint32_t s = at(old, i);
        if (!s) continue;
        size_t mask = cap - 1;
        for (size_t j = vm->strs[s].hash & mask;; j = (j + 1) & mask)
            if (!at(vm->atom_slots, j)) { vm->atom_slots[j] = s; break; }
    }
}

uint32_t js_atom(js_vm *vm, uint32_t s) {
    if (vm->strs[s].atom) return vm->strs[s].atom;
    uint32_t h = js_str_hash(vm, s);
    size_t mask = len(vm->atom_slots) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t t = at(vm->atom_slots, i);
        if (!t) {
            vm->atom_slots[i] = s;
            vm->strs[s].atom = s;
            vm->natoms++;
            if (vm->natoms * 2 > len(vm->atom_slots)) js_atoms_grow(vm);
            return s;
        }
        if (vm->strs[t].hash == h && js_str_eq(vm, t, s)) {
            vm->strs[s].atom = t;
            return t;
        }
    }
}

uint32_t js_atom_ascii(js_vm *vm, const char *p) { return js_atom(vm, js_str_ascii(vm, p)); }

int js_atom_index(js_vm *vm, uint32_t atom, uint32_t *index) {
    if (js_key_is_sym(atom)) return 0;
    uint32_t n = vm->strs[atom].len;
    if (n == 0 || n > 10) return 0;
    js_str_flatten(vm, atom);
    uint32_t o = vm->strs[atom].off;
    uint16_t c0 = vm->units[o];
    if (c0 < '0' || c0 > '9' || (c0 == '0' && n > 1)) return 0;
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[o + i];
        if (c < '0' || c > '9') return 0;
        v = v * 10 + (c - '0');
    }
    if (v >= 4294967295ull) return 0;
    *index = (uint32_t)v;
    return 1;
}

/* ---- shapes ---- */

static uint32_t js_shape_push(js_vm *vm, js_shape s) {
    push_back(vm->shapes, s);
    return (uint32_t)(len(vm->shapes) - 1);
}

static uint32_t js_hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

static void js_table_grow(js_vm *vm, vector<uint32_t> *tab, int roots) {
    size_t cap = len(*tab) * 2;
    vector<uint32_t> old;
    append_range(old, data(*tab), len(*tab));
    clear(*tab);
    resize(*tab, cap);
    size_t mask = cap - 1;
    for (size_t i = 0; i < len(old); i++) {
        uint32_t t = at(old, i);
        if (!t) continue;
        js_shape sh = vm->shapes[t];
        uint32_t h = roots ? js_hash3(sh.proto, 0, 7) : js_hash3(sh.parent, sh.atom, sh.flags);
        for (size_t j = h & mask;; j = (j + 1) & mask)
            if (!at(*tab, j)) { (*tab)[j] = t; break; }
    }
}

/* the shape objects of a prototype start from */
static uint32_t js_shape_of_slow(js_vm *vm, uint32_t proto);
uint32_t js_shape_of(js_vm *vm, uint32_t proto) {
    if (vm->root_valid && vm->root_proto == proto) return vm->root_shape;
    uint32_t r = js_shape_of_slow(vm, proto);
    vm->root_proto = proto;
    vm->root_shape = r;
    vm->root_valid = 1;
    return r;
}

static uint32_t js_shape_of_slow(js_vm *vm, uint32_t proto) {
    uint32_t h = js_hash3(proto, 0, 7);
    size_t mask = len(vm->roots) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t t = at(vm->roots, i);
        if (!t) {
            js_shape s = { 0, 0, 0, 0, proto, 0 };
            uint32_t id = js_shape_push(vm, s);
            vm->roots[i] = id;
            vm->nroots++;
            if (vm->nroots * 2 > len(vm->roots)) js_table_grow(vm, &vm->roots, 1);
            return id;
        }
        if (vm->shapes[t].proto == proto && vm->shapes[t].parent == 0 && vm->shapes[t].atom == 0) return t;
    }
}

/* the shape that adds atom to sh */
static uint32_t js_shape_add(js_vm *vm, uint32_t sh, uint32_t atom, uint32_t flags) {
    uint32_t h = js_hash3(sh, atom, flags);
    size_t mask = len(vm->trans) - 1;
    for (size_t i = h & mask;; i = (i + 1) & mask) {
        uint32_t t = at(vm->trans, i);
        if (!t) {
            if (atom == (JS_SYM_KEY | JSYM_HAS_INSTANCE)) vm->has_instance = 1;
            js_shape s = { sh, atom, vm->shapes[sh].count, vm->shapes[sh].count + 1, vm->shapes[sh].proto, flags };
            uint32_t id = js_shape_push(vm, s);
            vm->trans[i] = id;
            vm->ntrans++;
            if (vm->ntrans * 2 > len(vm->trans)) js_table_grow(vm, &vm->trans, 0);
            return id;
        }
        js_shape e = vm->shapes[t];
        if (e.parent == sh && e.atom == atom && e.flags == flags) return t;
    }
}

/* the where of sh that holds atom, or 0: through a cache of the lookups made */
uint32_t js_shape_find(js_vm *vm, uint32_t sh, uint32_t atom) {
    if (vm->shapes[sh].count == 0) return 0;
    size_t k = (js_hash3(sh, atom, 1) & 4095) * 3;
    if (vm->lookup[k] == sh && vm->lookup[k + 1] == atom) return vm->lookup[k + 2];
    uint32_t found = 0;
    for (uint32_t x = sh; x && vm->shapes[x].count; x = vm->shapes[x].parent)
        if (vm->shapes[x].atom == atom) { found = x; break; }
    vm->lookup[k] = sh;
    vm->lookup[k + 1] = atom;
    vm->lookup[k + 2] = found;
    return found;
}

/* ---- the heap ---- */

static int js_heap_full(js_vm *vm) {
    return (uint64_t)len(vm->slots) * 8 + (uint64_t)len(vm->units) * 2 + (uint64_t)len(vm->objs) * sizeof(js_obj) > vm->heap_cap;
}

/* n slots at the heap's end, holes */
static uint32_t js_slots_alloc(js_vm *vm, uint32_t n) {
    uint32_t off = (uint32_t)len(vm->slots);
    resize(vm->slots, (size_t)off + n, JV_HOLE);
    return off;
}

uint32_t js_obj_new(js_vm *vm, uint32_t kind, uint32_t proto) {
    js_obj o;
    memset(&o, 0, sizeof o);
    o.kind = (uint16_t)kind;
    o.flags = JO_EXTENSIBLE;
    o.proto = proto;
    o.shape = js_shape_of(vm, proto);
    vm->gc_allocs++;
    if (vm->obj_free) {
        /* a record the collector freed */
        uint32_t i = vm->obj_free;
        vm->obj_free = vm->objs[i].a;
        vm->objs[i] = o;
        return i;
    }
    push_back(vm->objs, o);
    return (uint32_t)(len(vm->objs) - 1);
}

/* room for slot i of o's named properties */
static void js_obj_room(js_vm *vm, uint32_t o, uint32_t i) {
    uint32_t cap = vm->objs[o].cap;
    if (i < cap) return;
    uint32_t nc = cap ? cap * 2 : 4;
    while (nc <= i) nc *= 2;
    uint32_t off = js_slots_alloc(vm, nc), old = vm->objs[o].slots;
    for (uint32_t k = 0; k < cap; k++) vm->slots[off + k] = vm->slots[old + k];
    vm->objs[o].slots = off;
    vm->objs[o].cap = nc;
}

int js_get_own(js_vm *vm, uint32_t o, uint32_t atom, jv *v, uint32_t *flags) {
    if (vm->objs[o].kind == JK_PROXY) return js_proxy_get_own(vm, o, atom, v, flags);
    if (vm->objs[o].kind == JK_TYPEDARRAY) {
        uint32_t i;
        if (js_atom_index(vm, atom, &i)) {
            if (i >= vm->objs[o].b) return 0;
            *v = js_ta_get(vm, o, i);
            *flags = 0;
            return 1;
        }
    }
    uint32_t e = js_shape_find(vm, vm->objs[o].shape, atom);
    if (!e) return 0;
    jv x = vm->slots[vm->objs[o].slots + vm->shapes[e].slot];
    if (x == JV_HOLE) return 0;
    *v = x;
    *flags = vm->shapes[e].flags;
    return 1;
}

int js_shape_entry(js_vm *vm, uint32_t o, uint32_t key, uint32_t *flags, int *live) {
    uint32_t e = js_shape_find(vm, vm->objs[o].shape, key);
    if (!e) return 0;
    *flags = vm->shapes[e].flags;
    *live = vm->slots[vm->objs[o].slots + vm->shapes[e].slot] != JV_HOLE;
    return 1;
}

void js_define(js_vm *vm, uint32_t o, uint32_t atom, jv v, uint32_t flags) {
    uint32_t sh = vm->objs[o].shape;
    uint32_t e = js_shape_find(vm, sh, atom);
    if (e && vm->shapes[e].flags == flags) { vm->slots[vm->objs[o].slots + vm->shapes[e].slot] = v; return; }
    if (e) {
        /* the same key with other flags: the slot kept, the shape's where left as it was (rare) */
        vm->slots[vm->objs[o].slots + vm->shapes[e].slot] = v;
        return;
    }
    uint32_t ns = js_shape_add(vm, sh, atom, flags);
    uint32_t slot = vm->shapes[ns].slot;
    js_obj_room(vm, o, slot);
    vm->objs[o].shape = ns;
    vm->slots[vm->objs[o].slots + slot] = v;
}

/* a native getter's or setter's name as V8 gives it ("get size", "get [Symbol.toStringTag]"),
   when it was made with its key's own name (or none, under a symbol) */
static void js_accessor_name(js_vm *vm, jv fn, uint32_t key, int setter) {
    if (!jv_is_obj(fn) || vm->objs[jv_idx(fn)].kind != JK_NATIVE) return;
    uint32_t f = jv_idx(fn), nm = vm->objs[f].c;
    uint32_t t = key;
    if (js_key_is_sym(key)) {
        if (nm && nm != A_EMPTY) return;
        uint32_t d = vm->syms[key & ~JS_SYM_KEY].desc;
        t = js_str_concat(vm, js_str_ascii(vm, "["), js_str_concat(vm, d ? d : A_EMPTY, js_str_ascii(vm, "]")));
    } else if (nm != key) return;
    vm->objs[f].c = js_atom(vm, js_str_concat(vm, js_str_ascii(vm, setter ? "set " : "get "), t));
}

void js_define_accessor(js_vm *vm, uint32_t o, uint32_t atom, jv getter, jv setter, uint32_t flags) {
    jv cur;
    uint32_t cf;
    uint32_t acc;
    js_accessor_name(vm, getter, atom, 0);
    js_accessor_name(vm, setter, atom, 1);
    if (js_get_own(vm, o, atom, &cur, &cf) && (cf & JS_SHAPE_ACCESSOR)) acc = jv_idx(cur);
    else {
        acc = js_obj_new(vm, JK_ACCESSOR, 0);
        vm->objs[acc].x = JV_UNDEF;
        vm->objs[acc].a = 0;
        js_define(vm, o, atom, jv_obj(acc), flags | JS_SHAPE_ACCESSOR);
    }
    if (getter != JV_HOLE) vm->objs[acc].x = getter;
    if (setter != JV_HOLE) vm->objs[acc].a = jv_is_obj(setter) ? jv_idx(setter) : 0;
}

/* ---- an array's (and an arguments object's) elements ----
   An element is dense, its value in the slots the object's dense array holds (an
   arguments object's own array, in a), its attributes the default ones (writable,
   enumerable, configurable) but as the object's being frozen or sealed makes them; or,
   given attributes of its own (Object.defineProperty's, an accessor), kept in the
   object's shape under its index as any property is, the dense slot a hole, and the
   object marked JO_ELEMPROPS. An element is never both. An array's length is past every
   element of either kind. */

/* an index's atom */
static uint32_t js_index_atom(js_vm *vm, uint32_t i) { return js_atom(vm, js_num_to_str(vm, (double)i)); }
/* the array that holds an object's dense elements: an array its own, an arguments object's */
static uint32_t js_elem_store(js_vm *vm, uint32_t o) { return vm->objs[o].kind == JK_ARGUMENTS ? vm->objs[o].a : o; }
/* element i kept in o's shape, with attributes of its own: its shape entry, 0 for none */
static uint32_t js_elem_entry(js_vm *vm, uint32_t o, uint32_t i, uint32_t atom) {
    if (!(vm->objs[o].flags & JO_ELEMPROPS)) return 0;
    uint32_t e = js_shape_find(vm, vm->objs[o].shape, atom ? atom : js_index_atom(vm, i));
    if (!e || vm->slots[vm->objs[o].slots + vm->shapes[e].slot] == JV_HOLE) return 0;
    return e;
}
/* a refusal and why (vm->refused): 2 */
static int js_refuse(js_vm *vm, uint32_t why, uint32_t key) {
    vm->refused = why;
    vm->refused_key = key;
    return 2;
}

/* element i deleted ([[Delete]]): 1, or 0 when it stays (not configurable: its own
   attribute, or the object sealed or frozen) */
static int js_elem_delete(js_vm *vm, uint32_t o, uint32_t i, uint32_t atom) {
    uint32_t st = js_elem_store(vm, o), fl = vm->objs[o].flags;
    if (i < vm->objs[st].b && vm->slots[vm->objs[st].a + i] != JV_HOLE) {
        if (fl & (JO_SEALED | JO_FROZEN)) return 0;
        vm->slots[vm->objs[st].a + i] = JV_HOLE;
        return 1;
    }
    uint32_t e = js_elem_entry(vm, o, i, atom);
    if (e) {
        if ((vm->shapes[e].flags & JS_SHAPE_FIXED) || (fl & (JO_SEALED | JO_FROZEN))) return 0;
        vm->slots[vm->objs[o].slots + vm->shapes[e].slot] = JV_HOLE;
    }
    return 1;
}

int js_delete(js_vm *vm, uint32_t o, uint32_t atom) {
    if (vm->objs[o].kind == JK_PROXY) return js_proxy_delete(vm, o, atom);
    uint32_t k = vm->objs[o].kind, i;
    if ((k == JK_ARRAY || k == JK_ARGUMENTS) && js_atom_index(vm, atom, &i)) return js_elem_delete(vm, o, i, atom);
    if (k == JK_ARRAY && atom == A_LENGTH) return 0;
    int fnk = (k == JK_CLOSURE || k == JK_NATIVE || k == JK_BOUND) && (atom == A_LENGTH || atom == A_NAME);
    uint32_t e = js_shape_find(vm, vm->objs[o].shape, atom);
    if (e && vm->slots[vm->objs[o].slots + vm->shapes[e].slot] != JV_HOLE) {
        /* a property not configurable (defineProperty's, a sealed or frozen object's) stays */
        if ((vm->shapes[e].flags & JS_SHAPE_FIXED) || (vm->objs[o].flags & (JO_SEALED | JO_FROZEN))) return 0;
        vm->slots[vm->objs[o].slots + vm->shapes[e].slot] = JV_HOLE;
        /* a function's length or name gone: not the engine's again */
        if (fnk) vm->objs[o].flags |= atom == A_LENGTH ? JO_FN_NOLENGTH : JO_FN_NONAME;
        return 1;
    }
    if ((k == JK_CLOSURE || k == JK_NATIVE) && js_fn_virtual(vm, o, atom)) {
        /* the engine's own: arguments, caller and a prototype not configurable; length and name as a sealed or
           frozen function's stay */
        if (!fnk || (vm->objs[o].flags & (JO_SEALED | JO_FROZEN))) return 0;
        vm->objs[o].flags |= atom == A_LENGTH ? JO_FN_NOLENGTH : JO_FN_NONAME;
    }
    return 1;
}

/* ---- arrays: dense elements in the slot heap ---- */

uint32_t js_array_new(js_vm *vm, uint32_t n) {
    uint32_t a = js_obj_new(vm, JK_ARRAY, vm->array_proto);
    uint32_t cap = n < 4 ? 4 : n;
    vm->objs[a].a = js_slots_alloc(vm, cap);
    vm->objs[a].b = n;
    vm->objs[a].c = cap;
    return a;
}

static void js_array_room(js_vm *vm, uint32_t a, uint32_t n) {
    uint32_t cap = vm->objs[a].c;
    if (n <= cap) return;
    uint32_t nc = cap ? cap * 2 : 4;
    while (nc < n) nc *= 2;
    uint32_t off = js_slots_alloc(vm, nc), old = vm->objs[a].a, used = vm->objs[a].b;
    uint64_t *slots = data(vm->slots);
    for (uint32_t k = 0; k < used; k++) slots[off + k] = slots[old + k];
    vm->objs[a].a = off;
    vm->objs[a].c = nc;
}

void js_array_push(js_vm *vm, uint32_t a, jv v) {
    uint32_t n = vm->objs[a].b;
    if (n == vm->objs[a].c) js_array_room(vm, a, n + 1);
    js_obj *objs = data(vm->objs);
    uint64_t *slots = data(vm->slots);
    slots[objs[a].a + n] = v;
    objs[a].b = n + 1;
}

jv js_array_get(js_vm *vm, uint32_t a, uint32_t i) {
    if (i >= vm->objs[a].b) return JV_UNDEF;
    jv v = vm->slots[vm->objs[a].a + i];
    return v == JV_HOLE ? JV_UNDEF : v;
}

void js_array_set(js_vm *vm, uint32_t a, uint32_t i, jv v) {
    uint32_t n = vm->objs[a].b;
    if (i >= n) {
        js_array_room(vm, a, i + 1);
        for (uint32_t k = n; k < i; k++) vm->slots[vm->objs[a].a + k] = JV_HOLE;
        vm->objs[a].b = i + 1;
    }
    vm->slots[vm->objs[a].a + i] = v;
}

void js_array_set_length(js_vm *vm, uint32_t a, uint32_t n) {
    uint32_t cur = vm->objs[a].b;
    if (n > cur) {
        js_array_room(vm, a, n);
        for (uint32_t k = cur; k < n; k++) vm->slots[vm->objs[a].a + k] = JV_HOLE;
    }
    vm->objs[a].b = n;
}

int js_array_set_length_checked(js_vm *vm, uint32_t a, double d) {
    if (d != d || d < 0 || d != trunc(d) || d > 4294967295.0) { js_throw_msg(vm, vm->range_error_proto, "Invalid array length"); return 0; }
    uint32_t n = (uint32_t)d, fl = vm->objs[a].flags, cur = vm->objs[a].b;
    if (fl & (JO_FROZEN | JO_LENGTH_RO)) return js_refuse(vm, JSR_LENGTH, A_LENGTH);
    if (n < cur && (fl & (JO_SEALED | JO_ELEMPROPS))) {
        /* the elements past it deleted from the end, a non-configurable one stopping it */
        for (uint32_t i = cur; i-- > n;) {
            if (!js_elem_delete(vm, a, i, 0)) {
                js_array_set_length(vm, a, i + 1);
                return js_refuse(vm, JSR_DELETE, js_index_atom(vm, i));
            }
        }
    }
    js_array_set_length(vm, a, n);
    return 1;
}

/* ---- errors ---- */

jv js_throw(js_vm *vm, jv v) {
    vm->exception = v;
    vm->throwing = 1;
    return JV_HOLE;
}

uint32_t js_error_new(js_vm *vm, uint32_t proto, uint32_t msg) {
    uint32_t e = js_obj_new(vm, JK_ERROR, proto);
    js_define(vm, e, A_MESSAGE, jv_str(msg), JS_SHAPE_HIDDEN);
    js_define(vm, e, A_STACK, jv_str(A_EMPTY), JS_SHAPE_HIDDEN);
    return e;
}

jv js_throw_msg(js_vm *vm, uint32_t proto, const char *msg) {
    if (vm->throwing) return JV_HOLE;
    return js_throw(vm, jv_obj(js_error_new(vm, proto, js_str_ascii(vm, msg))));
}

/* "<name> <what>": a TypeError naming the value that was not what it had to be */
jv js_type_error(js_vm *vm, const char *what, uint32_t name_str) {
    if (vm->throwing) return JV_HOLE;
    uint32_t w = js_str_ascii(vm, what);
    uint32_t m = name_str ? js_str_concat(vm, js_str_concat(vm, name_str, js_str_ascii(vm, " ")), w) : w;
    return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
}

void js_error_text(js_vm *vm, jv e, vector<char> *out) {
    int was = vm->throwing;
    jv saved = vm->exception;
    vm->throwing = 0;
    if (jv_is_obj(e) && vm->objs[jv_idx(e)].kind == JK_ERROR) {
        jv nm = js_get(vm, e, A_NAME), msg = js_get(vm, e, A_MESSAGE);
        if (!vm->throwing && jv_is_str(nm)) js_str_to_utf8(vm, jv_idx(nm), out);
        else append_range(out, "Error", 5);
        if (!vm->throwing && jv_is_str(msg) && vm->strs[jv_idx(msg)].len) {
            append_range(out, ": ", 2);
            js_str_to_utf8(vm, jv_idx(msg), out);
        }
    } else {
        uint32_t s = js_to_string(vm, e);
        if (s && !vm->throwing) js_str_to_utf8(vm, s, out);
        else append_range(out, "(an exception)", 14);
    }
    vm->throwing = was;
    vm->exception = saved;
}

/* ---- the TypeErrors that name an expression or a value, in V8's words ----
   V8 names the expression an error is about by printing it again from its syntax (its
   call printer: f.reset, obj.a[0], this.x, (intermediate value).m); where it cannot, by
   the value (its default: "number 5", "object"). The compiler records where each such
   expression stands (vm->sites, src/js/compile.c), and js_site_text prints one again. */

static void js_put(vector<char> *m, const char *s) { append_range(*m, s, strlen(s)); }

static jv js_throw_chars(js_vm *vm, uint32_t proto, vector<char> *m) {
    if (vm->throwing) return JV_HOLE;
    uint32_t s = js_str_utf8(vm, data(*m), len(*m));
    return js_throw(vm, jv_obj(js_error_new(vm, proto, s)));
}

void js_value_name(js_vm *vm, jv v, vector<char> *out) {
    js_str_to_utf8(vm, js_typeof(vm, v), out);
    if (jv_is_str(v)) {
        uint32_t s = jv_idx(v), n = vm->strs[s].len;
        js_put(out, " \"");
        js_str_to_utf8(vm, n > 100 ? js_str_sub(vm, s, 0, 100) : s, out);
        if (n > 100) js_put(out, "<...>");
        push_back(*out, '"');
    } else if (v == JV_NULL) js_put(out, " null");
    else if (v == JV_TRUE) js_put(out, " true");
    else if (v == JV_FALSE) js_put(out, " false");
    else if (jv_is_num(v)) {
        char b[64];
        js_num_format(jv_dbl(v), b, sizeof b);
        push_back(*out, ' ');
        js_put(out, b);
    }
}

jv js_not_function(js_vm *vm, jv v) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    js_value_name(vm, v, &m);
    js_put(&m, " is not a function");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

jv js_not_constructor(js_vm *vm, jv v) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    js_value_text(vm, v, &m);
    js_put(&m, " is not a constructor");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

uint32_t js_func_source(js_vm *vm, uint32_t o) {
    uint32_t k = vm->objs[o].kind;
    if (k == JK_CLOSURE) {
        js_func f = vm->funcs[vm->objs[o].a];
        if (f.src_len && (size_t)f.src_off + f.src_len <= len(vm->src)) {
            vector<char> t;
            append_range(t, data(vm->src) + f.src_off, f.src_len);
            return js_str_utf8(vm, data(t), len(t));
        }
    }
    if (k == JK_NATIVE) {
        uint32_t name = vm->objs[o].c;
        return js_str_concat(vm, js_str_ascii(vm, "function "), js_str_concat(vm, name ? name : A_EMPTY, js_str_ascii(vm, "() { [native code] }")));
    }
    if (k == JK_CLOSURE || k == JK_BOUND || (k == JK_PROXY && vm->objs[o].c)) return js_str_ascii(vm, "function () { [native code] }");
    return 0;
}

/* a property as GetDataProperty reads it: up the prototypes, an accessor's or a proxy's undefined */
static jv js_data_prop(js_vm *vm, uint32_t o, uint32_t key) {
    for (uint32_t x = o, d = 0; x && d < 10000; x = vm->objs[x].proto, d++) {
        if (vm->objs[x].kind == JK_PROXY) return JV_UNDEF;
        uint32_t e = js_shape_find(vm, vm->objs[x].shape, key);
        if (!e) continue;
        jv v = vm->slots[vm->objs[x].slots + vm->shapes[e].slot];
        if (v == JV_HOLE) continue;
        return (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) ? JV_UNDEF : v;
    }
    return JV_UNDEF;
}

void js_value_text(js_vm *vm, jv v, vector<char> *out) {
    if (!jv_is_obj(v)) {
        int was = vm->throwing;
        uint32_t s = jv_is_sym(v) ? js_sym_text(vm, jv_idx(v)) : js_to_string(vm, v);
        vm->throwing = was;
        if (s) js_str_to_utf8(vm, s, out);
        return;
    }
    uint32_t o = jv_idx(v), k = vm->objs[o].kind;
    /* a proxy is its target's text (through every proxy; a revoked one's is null's) */
    for (int d = 0; k == JK_PROXY && d < 10000; d++) {
        if (!vm->objs[o].a) { js_put(out, "null"); return; }
        o = vm->objs[o].a;
        k = vm->objs[o].kind;
    }
    uint32_t fs = js_func_source(vm, o);
    if (fs) { js_str_to_utf8(vm, fs, out); return; }
    if (k == JK_ERROR) {
        jv nm = js_data_prop(vm, o, A_NAME), msg = js_data_prop(vm, o, A_MESSAGE);
        if (jv_is_str(nm)) js_str_to_utf8(vm, jv_idx(nm), out);
        else js_put(out, "Error");
        if (jv_is_str(msg) && vm->strs[jv_idx(msg)].len) { js_put(out, ": "); js_str_to_utf8(vm, jv_idx(msg), out); }
        return;
    }
    /* Object.prototype.toString its toString: #<its constructor's name> */
    jv ots = JV_UNDEF;
    uint32_t of = 0;
    js_get_own(vm, vm->object_proto, A_TOSTRING, &ots, &of);
    if (k != JK_PROXY && js_data_prop(vm, o, A_TOSTRING) == ots) {
        jv c = js_data_prop(vm, o, A_CONSTRUCTOR);
        uint32_t cn = 0;
        if (jv_is_obj(c) && vm->objs[jv_idx(c)].kind == JK_CLOSURE) cn = vm->funcs[vm->objs[jv_idx(c)].a].name;
        else if (jv_is_obj(c) && vm->objs[jv_idx(c)].kind == JK_NATIVE) cn = vm->objs[jv_idx(c)].c;
        if (cn && vm->strs[cn].len) { js_put(out, "#<"); js_str_to_utf8(vm, cn, out); push_back(*out, '>'); return; }
    }
    /* else [object its tag], or its kind's class */
    jv tag = k == JK_PROXY ? JV_UNDEF : js_data_prop(vm, o, JS_SYM_KEY | JSYM_TO_STRING_TAG);
    js_put(out, "[object ");
    static const char *const ta_names[JTA_N] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
        "Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array" };
    int weak = (vm->objs[o].flags & JO_WEAK) != 0;
    if (jv_is_str(tag)) js_str_to_utf8(vm, jv_idx(tag), out);
    else if (k == JK_TYPEDARRAY) js_put(out, ta_names[(((uint32_t)vm->objs[o].flags >> JO_TA_SHIFT) & 15u) % JTA_N]);
    else js_put(out, k == JK_ARRAY ? "Array" : k == JK_DATE ? "Date" : k == JK_REGEXP ? "RegExp" : k == JK_ARGUMENTS ? "Arguments" :
                     k == JK_BOOLEAN ? "Boolean" : k == JK_NUMBER ? "Number" : k == JK_STRING ? "String" :
                     k == JK_MAP ? (weak ? "WeakMap" : "Map") : k == JK_SET ? (weak ? "WeakSet" : "Set") : k == JK_ARRAYBUFFER ? "ArrayBuffer" :
                     k == JK_ERROR ? "Error" : "Object");
    push_back(*out, ']');
}

/* an iterator's next gave no object: "Iterator result 5 is not an object" */
static jv js_result_error(js_vm *vm, jv r) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    js_put(&m, "Iterator result ");
    js_value_text(vm, r, &m);
    js_put(&m, " is not an object");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* the site of the instruction at pc, found among vm->sites (in pc order): 1 when there is one */
static int js_site_at(js_vm *vm, uint32_t pc, js_site *out) {
    size_t lo = 0, hi = len(vm->sites);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vm->sites[mid].pc < pc) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= len(vm->sites) || vm->sites[lo].pc != pc) return 0;
    *out = vm->sites[lo];
    return 1;
}

/* the name a site gives its expression: V8's own for the bindings it makes (".for",
   ".catch"), its text printed again, else the value's default name */
static void js_site_name(js_vm *vm, js_site s, int have, jv v, int callee_only, vector<char> *out) {
    if (have && (s.kind & JSS_DOTFOR)) { js_put(out, ".for"); return; }
    if (have && (s.kind & JSS_DOTCATCH)) { js_put(out, ".catch"); return; }
    if (have && (s.kind & JSS_DEFAULTED)) { js_put(out, "(intermediate value)(intermediate value)(intermediate value)"); return; }
    if (have && (s.kind & JSS_INTERMEDIATE)) { js_put(out, "(intermediate value)"); return; }
    if (have && !(s.kind & JSS_PLAIN) && js_site_text(vm, s, callee_only, out)) return;
    js_value_name(vm, v, out);
}

/* a call's (or new's) callee that is no function: "f.reset is not a function" */
static jv js_call_error(js_vm *vm, uint32_t pc, jv callee, int construct) {
    if (vm->throwing) return JV_HOLE;
    js_site s = { 0, 0, 0, 0 };
    int have = js_site_at(vm, pc, &s);
    vector<char> m;
    /* a call an iteration takes its value from: V8 words its error as the iteration's */
    if (have && !construct && (s.kind & JSS_INTERMEDIATE)) {
        js_put(&m, "yield* (intermediate value) is not iterable");
        return js_throw_chars(vm, vm->type_error_proto, &m);
    }
    js_site_name(vm, s, have, callee, 0, &m);
    if (have && !construct && (s.kind & JSS_CALLED) && (s.kind & JSS_AWAITED)) js_put(&m, "(...) is not a function or its return value is not async iterable");
    else if (have && !construct && (s.kind & JSS_CALLED)) js_put(&m, " is not a function or its return value is not iterable");
    else js_put(&m, construct ? " is not a constructor" : " is not a function");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

jv js_caller_not_function(js_vm *vm, jv v) {
    if (vm->throwing) return JV_HOLE;
    if (vm->nframes) {
        /* the script's frame, its pc past the call that reached the native */
        uint32_t pc = vm->frames[vm->nframes - 1].pc, at = 0;
        if (pc >= 3 && (vm->code[pc - 3] == OP_CALL || vm->code[pc - 3] == OP_CALL0)) at = pc - 3;
        else if (pc >= 1 && vm->code[pc - 1] == OP_CALL_ARR) at = pc - 1;
        js_site s = { 0, 0, 0, 0 };
        if (at && js_site_at(vm, at, &s) && (s.kind & JSS_KIND) == JSS_CALL) return js_call_error(vm, at, v, 0);
    }
    return js_not_function(vm, v);
}

/* a class's constructor called without new: V8 names the class, or says it has none */
static jv js_class_call_error(js_vm *vm, uint32_t c) {
    if (vm->throwing) return JV_HOLE;
    uint32_t name = vm->objs[c].kind == JK_CLOSURE ? vm->funcs[vm->objs[c].a].name : 0;
    vector<char> m;
    if (name && vm->strs[name].len) {
        js_put(&m, "Class constructor ");
        js_str_to_utf8(vm, name, &m);
        js_put(&m, " cannot be invoked without 'new'");
    } else js_put(&m, "Class constructors cannot be invoked without 'new'");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* key in v, v no object: "Cannot use 'in' operator to search for 'x' in 5" */
static jv js_in_error(js_vm *vm, jv key, jv v) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    js_put(&m, "Cannot use 'in' operator to search for '");
    js_value_text(vm, key, &m);
    js_put(&m, "' in ");
    js_value_text(vm, v, &m);
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* class extends a value that is no constructor (proto 0), or whose prototype is neither an
   object nor null (proto 1, v that prototype) */
static jv js_extends_error(js_vm *vm, jv v, int proto) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    js_put(&m, proto ? "Class extends value does not have valid prototype property " : "Class extends value ");
    js_value_text(vm, v, &m);
    if (!proto) js_put(&m, " is not a constructor or null");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* super(...) whose parent is no constructor: "Super constructor null of A is not a constructor" */
static jv js_super_error(js_vm *vm, uint32_t func, jv parent) {
    if (vm->throwing) return JV_HOLE;
    uint32_t name = vm->funcs[func].name;
    vector<char> m;
    js_put(&m, "Super constructor ");
    /* V8 names a function by its name (none: null), any other value by its text */
    uint32_t pk = jv_is_obj(parent) ? vm->objs[jv_idx(parent)].kind : JK_FREE;
    if (pk == JK_CLOSURE || pk == JK_NATIVE || pk == JK_BOUND) {
        uint32_t pn = pk == JK_CLOSURE ? vm->funcs[vm->objs[jv_idx(parent)].a].name : pk == JK_NATIVE ? vm->objs[jv_idx(parent)].c : 0;
        if (pn && vm->strs[pn].len) js_str_to_utf8(vm, pn, &m);
        else js_put(&m, "null");
    } else js_value_text(vm, parent, &m);
    js_put(&m, " of ");
    if (name && vm->strs[name].len) js_str_to_utf8(vm, name, &m);
    else js_put(&m, "anonymous class");
    js_put(&m, " is not a constructor");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* the interpreter's call instruction op that cannot be made, its frame fi's pc past its
   operands: super(...)'s error, else the call's or new's at the instruction's site. Its
   arguments few, so that js_run's hot paths keep their registers */
static void js_call_fail(js_vm *vm, uint32_t op, uint32_t fi, jv callee) {
    uint32_t pc = vm->frames[fi].pc;
    if (op == OP_SUPER_CALL || op == OP_SUPER_CALL_ARR) { js_super_error(vm, vm->frames[fi].func, callee); return; }
    js_call_error(vm, op == OP_CALL_ARR || op == OP_NEW_ARR ? pc - 1 : pc - 3, callee, op == OP_NEW || op == OP_NEW_ARR);
}

/* a value its site iterates that is not iterable (pc 0: a native's): V8's words for each
   way to iterate one */
static jv js_not_iterable(js_vm *vm, jv v, uint32_t pc, int async) {
    if (vm->throwing) return JV_HOLE;
    js_site s;
    int have = pc && js_site_at(vm, pc, &s);
    uint32_t k = have ? s.kind & JSS_KIND : 0xFFu;
    vector<char> m;
    if (k == JSS_SPREAD_CALL) {
        if (!jv_is_nullish(v)) {
            js_put(&m, "Spread syntax requires ...iterable[Symbol.iterator] to be a function");
            return js_throw_chars(vm, vm->type_error_proto, &m);
        }
        js_site_name(vm, s, have, v, 0, &m);
        js_put(&m, v == JV_NULL ? " is not iterable (cannot read property null)" : " is not iterable (cannot read property undefined)");
        return js_throw_chars(vm, vm->type_error_proto, &m);
    }
    if (have && (s.kind & JSS_INTERMEDIATE)) {
        js_put(&m, "yield* (intermediate value) is not iterable");
        return js_throw_chars(vm, vm->type_error_proto, &m);
    }
    if (have && !(s.kind & JSS_PLAIN)) {
        size_t n0 = len(m);
        js_site_name(vm, s, have, v, (s.kind & JSS_CALLED) && !async, &m);
        if (len(m) > n0) {
            if (s.kind & JSS_CALLED) js_put(&m, async ? " is not a function or its return value is not async iterable" : " is not a function or its return value is not iterable");
            else if (s.kind & JSS_NOSYM) js_put(&m, " is not iterable (cannot read property Symbol(Symbol.iterator))");
            else js_put(&m, async ? " is not async iterable" : " is not iterable");
            return js_throw_chars(vm, vm->type_error_proto, &m);
        }
    }
    js_value_name(vm, v, &m);
    js_put(&m, async ? " is not async iterable" : " is not iterable (cannot read property Symbol(Symbol.iterator))");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* an object pattern's value undefined or null, its first key (0: none): "Cannot
   destructure property 'a' of 'obj.u' as it is undefined." */
static jv js_destructure_error(js_vm *vm, js_site s, jv v, uint32_t key) {
    if (vm->throwing) return JV_HOLE;
    vector<char> name, m;
    js_site_name(vm, s, 1, v, 0, &name);
    js_put(&m, "Cannot destructure ");
    if (key) {
        js_put(&m, "property '");
        js_str_to_utf8(vm, js_key_name(vm, key), &m);
        js_put(&m, "' of ");
    }
    push_back(m, '\'');
    append_range(m, data(name), len(name));
    js_put(&m, v == JV_NULL ? "' as it is null." : "' as it is undefined.");
    return js_throw_chars(vm, vm->type_error_proto, &m);
}

/* the interpreter's own: a nullish value read at an object pattern's first key (the read
   at site_pc) or checked for one (key 0), thrown in V8's words: 1; 0 when the instruction
   is no pattern's (kept out of js_run, whose registers its hot paths want) */
static int js_destructure_at(js_vm *vm, uint32_t site_pc, jv v, uint32_t key) {
    js_site s = { 0, 0, 0, 0 };
    if (!js_site_at(vm, site_pc, &s) || (s.kind & JSS_KIND) != JSS_DESTRUCT_OBJ) return 0;
    js_destructure_error(vm, s, v, key);
    return 1;
}

/* ---- conversions ---- */

int js_truthy(js_vm *vm, jv v) {
    if (jv_is_num(v)) { double d = jv_dbl(v); return d == d && d != 0; }
    if (jv_is_str(v)) return vm->strs[jv_idx(v)].len > 0;
    if (jv_is_big(v)) return !js_big_is_zero(vm, v);
    if (jv_is_obj(v) || jv_is_sym(v)) return 1;
    return v == JV_TRUE;
}

/* ---- symbols ---- */

uint32_t js_sym_new(js_vm *vm, uint32_t desc) {
    js_sym y = { desc, 0 };
    push_back(vm->syms, y);
    return (uint32_t)(len(vm->syms) - 1);
}
uint32_t js_sym_text(js_vm *vm, uint32_t sym) {
    uint32_t d = vm->syms[sym].desc;
    uint32_t t = js_str_concat(vm, js_str_ascii(vm, "Symbol("), d ? d : A_EMPTY);
    return js_str_concat(vm, t, js_str_ascii(vm, ")"));
}
uint32_t js_key_name(js_vm *vm, uint32_t key) {
    if (js_key_is_sym(key)) return js_sym_text(vm, key & ~JS_SYM_KEY);
    return key;
}
uint32_t js_prop_key(js_vm *vm, jv v, int *ok) {
    *ok = 1;
    if (jv_is_sym(v)) return JS_SYM_KEY | jv_idx(v);
    if (jv_is_obj(v)) {
        jv p = js_to_primitive(vm, v, 1);
        if (vm->throwing) { *ok = 0; return 0; }
        if (jv_is_sym(p)) return JS_SYM_KEY | jv_idx(p);
        v = p;
    }
    uint32_t s = js_to_string(vm, v);
    if (!s) { *ok = 0; return 0; }
    return js_atom(vm, s);
}

int32_t js_to_int32(double d) {
    if (d != d || d == INFINITY || d == -INFINITY) return 0;
    if (d >= -2147483648.0 && d <= 2147483647.0) return (int32_t)d;
    double m = fmod(trunc(d), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (int32_t)(uint32_t)m;
}

static int js_is_ws(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0xA0 || c == 0xFEFF || c == 0x2028 ||
           c == 0x2029 || (c >= 0x2000 && c <= 0x200A) || c == 0x3000 || c == 0x1680 || c == 0x202F || c == 0x205F;
}

/* StringToNumber: white space trimmed; decimal, Infinity, or 0x / 0o / 0b; else NaN */
double js_str_to_num(js_vm *vm, uint32_t s) {
    js_str_flatten(vm, s);
    uint32_t o = vm->strs[s].off, n = vm->strs[s].len, i = 0;
    while (i < n && js_is_ws(vm->units[o + i])) i++;
    while (n > i && js_is_ws(vm->units[o + n - 1])) n--;
    if (i == n) return 0;
    char buf[400];
    uint32_t k = 0;
    for (uint32_t j = i; j < n; j++) {
        uint16_t c = vm->units[o + j];
        if (c > 127 || k + 1 >= sizeof buf) return NAN;
        buf[k++] = (char)c;
    }
    buf[k] = 0;
    if (k > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X' || buf[1] == 'o' || buf[1] == 'O' || buf[1] == 'b' || buf[1] == 'B')) {
        int base = buf[1] == 'x' || buf[1] == 'X' ? 16 : buf[1] == 'o' || buf[1] == 'O' ? 8 : 2;
        double v = 0;
        for (uint32_t j = 2; j < k; j++) {
            char c = buf[j];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (d >= base) return NAN;
            v = v * base + d;
        }
        return v;
    }
    const char *p = buf;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if (strcmp(p, "Infinity") == 0) return neg ? -INFINITY : INFINITY;
    /* only what a numeric literal allows: digits, one point, an exponent */
    int digits = 0, point = 0, expo = 0;
    for (const char *q = p; *q; q++) {
        if (*q >= '0' && *q <= '9') digits = 1;
        else if (*q == '.' && !point && !expo) point = 1;
        else if ((*q == 'e' || *q == 'E') && digits && !expo) { expo = 1; if (q[1] == '+' || q[1] == '-') q++; if (!(q[1] >= '0' && q[1] <= '9')) return NAN; }
        else return NAN;
    }
    if (!digits) return NAN;
    double v = strtod(p, 0);
    return neg ? -v : v;
}

/* Number::toString: the shortest digits that read back as the same number */
void js_num_format(double d, char *buf, size_t cap) {
    if (d != d) { snprintf(buf, cap, "NaN"); return; }
    if (d == 0) { snprintf(buf, cap, "0"); return; }
    if (d == INFINITY) { snprintf(buf, cap, "Infinity"); return; }
    if (d == -INFINITY) { snprintf(buf, cap, "-Infinity"); return; }
    if (d == trunc(d) && fabs(d) < 1e21) {
        if (fabs(d) < 9007199254740992.0) { snprintf(buf, cap, "%lld", (long long)d); return; }
    }
    char tmp[40];
    int prec = 1;
    for (; prec <= 17; prec++) {
        snprintf(tmp, sizeof tmp, "%.*e", prec - 1, d);
        if (strtod(tmp, 0) == d) break;
    }
    /* tmp: [-]D.DDDDe[+-]XX: its digits and its exponent */
    char digs[24];
    int nd = 0, neg = 0, i = 0;
    if (tmp[0] == '-') { neg = 1; i = 1; }
    for (; tmp[i] && tmp[i] != 'e'; i++) if (tmp[i] >= '0' && tmp[i] <= '9') digs[nd++] = tmp[i];
    int e10 = atoi(tmp + i + 1);
    while (nd > 1 && digs[nd - 1] == '0') nd--;
    int n = e10 + 1;              /* the decimal point's place, after n digits */
    size_t k = 0;
    char out[64];
    if (neg) out[k++] = '-';
    if (nd <= n && n <= 21) {
        for (int j = 0; j < nd; j++) out[k++] = digs[j];
        for (int j = nd; j < n; j++) out[k++] = '0';
    } else if (0 < n && n <= 21) {
        for (int j = 0; j < n; j++) out[k++] = digs[j];
        out[k++] = '.';
        for (int j = n; j < nd; j++) out[k++] = digs[j];
    } else if (-6 < n && n <= 0) {
        out[k++] = '0';
        out[k++] = '.';
        for (int j = 0; j < -n; j++) out[k++] = '0';
        for (int j = 0; j < nd; j++) out[k++] = digs[j];
    } else {
        out[k++] = digs[0];
        if (nd > 1) {
            out[k++] = '.';
            for (int j = 1; j < nd; j++) out[k++] = digs[j];
        }
        out[k++] = 'e';
        out[k++] = n - 1 < 0 ? '-' : '+';
        int ex = n - 1 < 0 ? -(n - 1) : n - 1;
        char eb[8];
        int en = snprintf(eb, sizeof eb, "%d", ex);
        for (int j = 0; j < en; j++) out[k++] = eb[j];
    }
    out[k] = 0;
    snprintf(buf, cap, "%s", out);
}

uint32_t js_num_to_str(js_vm *vm, double d) {
    if (d >= 0 && d < 10 && d == trunc(d)) return vm->digit_strs[(int)d];
    /* an integer's digits written here (snprintf's are slower), the small ones' strings kept */
    if (d == trunc(d) && fabs(d) < 9007199254740992.0 && !(d == 0 && signbit(d))) {
        if (d >= 0 && d < 1024) {
            uint32_t k = (uint32_t)d;
            if (vm->int_strs[k]) return vm->int_strs[k];
        }
        char buf[24];
        uint32_t at = sizeof buf;
        int64_t v = (int64_t)d;
        uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
        buf[--at] = 0;
        do { buf[--at] = (char)('0' + u % 10); u /= 10; } while (u);
        if (v < 0) buf[--at] = '-';
        uint32_t s = js_str_utf8(vm, buf + at, sizeof buf - 1 - at);
        if (d >= 0 && d < 1024) vm->int_strs[(uint32_t)d] = s;
        return s;
    }
    char buf[64];
    js_num_format(d, buf, sizeof buf);
    return js_str_ascii(vm, buf);
}

jv js_to_primitive(js_vm *vm, jv v, int hint_string) {
    if (!jv_is_obj(v)) return v;
    uint32_t o = jv_idx(v);
    uint32_t k = vm->objs[o].kind;
    if (k == JK_NUMBER || k == JK_BOOLEAN || k == JK_STRING || k == JK_SYMBOL || k == JK_BIGINT) return vm->objs[o].x;
    /* the object's Symbol.toPrimitive, given the hint (a Date's: "default" read as "string") */
    {
        jv ex = js_get(vm, v, JS_SYM_KEY | JSYM_TO_PRIMITIVE);
        if (vm->throwing) return JV_HOLE;
        if (!jv_is_nullish(ex)) {
            if (!js_is_callable(vm, ex)) return js_throw_msg(vm, vm->type_error_proto, "Symbol.toPrimitive is not a function");
            jv hint[1] = { jv_str(hint_string > 0 ? A_STRING : hint_string == 0 ? A_NUMBER : A_DEFAULT) };
            jv r = js_call(vm, ex, v, hint, 1);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_obj(r)) return js_throw_msg(vm, vm->type_error_proto, "Cannot convert object to primitive value");
            return r;
        }
    }
    return js_ordinary_to_primitive(vm, v, hint_string > 0);
}

/* OrdinaryToPrimitive (ECMA-262 7.1.1.1): toString then valueOf (string_first), or the other way */
jv js_ordinary_to_primitive(js_vm *vm, jv v, int string_first) {
    uint32_t first = string_first ? A_TOSTRING : A_VALUEOF, second = string_first ? A_VALUEOF : A_TOSTRING;
    for (int pass = 0; pass < 2; pass++) {
        jv f = js_get(vm, v, pass ? second : first);
        if (vm->throwing) return JV_HOLE;
        if (js_is_callable(vm, f)) {
            jv r = js_call(vm, f, v, 0, 0);
            if (vm->throwing) return JV_HOLE;
            if (!jv_is_obj(r)) return r;
        }
    }
    return js_throw_msg(vm, vm->type_error_proto, "Cannot convert object to primitive value");
}

double js_to_number(js_vm *vm, jv v) {
    if (jv_is_num(v)) return jv_dbl(v);
    if (v == JV_UNDEF || v == JV_HOLE) return NAN;
    if (v == JV_NULL || v == JV_FALSE) return 0;
    if (v == JV_TRUE) return 1;
    if (jv_is_str(v)) return js_str_to_num(vm, jv_idx(v));
    if (jv_is_sym(v)) { js_throw_msg(vm, vm->type_error_proto, "Cannot convert a Symbol value to a number"); return NAN; }
    if (jv_is_big(v)) { js_throw_msg(vm, vm->type_error_proto, "Cannot convert a BigInt value to a number"); return NAN; }
    jv p = js_to_primitive(vm, v, 0);
    if (vm->throwing) return NAN;
    return js_to_number(vm, p);
}

uint32_t js_to_string(js_vm *vm, jv v) {
    if (jv_is_str(v)) return jv_idx(v);
    if (jv_is_num(v)) return js_num_to_str(vm, jv_dbl(v));
    if (v == JV_UNDEF || v == JV_HOLE) return A_UNDEFINED;
    if (v == JV_NULL) return A_NULL;
    if (v == JV_TRUE) return A_TRUE;
    if (v == JV_FALSE) return A_FALSE;
    if (jv_is_sym(v)) { js_throw_msg(vm, vm->type_error_proto, "Cannot convert a Symbol value to a string"); return 0; }
    if (jv_is_big(v)) return js_big_to_string(vm, v, 10);
    jv p = js_to_primitive(vm, v, 1);
    if (vm->throwing) return 0;
    return js_to_string(vm, p);
}

jv js_to_object(js_vm *vm, jv v) {
    if (jv_is_obj(v)) return v;
    if (jv_is_nullish(v) || v == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Cannot convert undefined or null to object");
    uint32_t kind = jv_is_num(v) ? JK_NUMBER : jv_is_str(v) ? JK_STRING : jv_is_sym(v) ? JK_SYMBOL : jv_is_big(v) ? JK_BIGINT : JK_BOOLEAN;
    uint32_t proto = kind == JK_NUMBER ? vm->number_proto : kind == JK_STRING ? vm->string_proto : kind == JK_SYMBOL ? vm->symbol_proto :
                     kind == JK_BIGINT ? vm->bigint_proto : vm->boolean_proto;
    uint32_t o = js_obj_new(vm, kind, proto);
    vm->objs[o].x = v;
    return jv_obj(o);
}

int js_is_callable(js_vm *vm, jv v) {
    if (!jv_is_obj(v)) return 0;
    uint32_t k = vm->objs[jv_idx(v)].kind;
    return k == JK_CLOSURE || k == JK_NATIVE || k == JK_BOUND || (k == JK_PROXY && vm->objs[jv_idx(v)].c);
}

int js_is_constructor(js_vm *vm, jv v) {
    for (int d = 0; d < 10000 && jv_is_obj(v); d++) {
        uint32_t o = jv_idx(v), k = vm->objs[o].kind;
        if (k == JK_CLOSURE) return !(vm->funcs[vm->objs[o].a].flags & (JF_ARROW | JF_METHOD | JF_GENERATOR | JF_ASYNC));
        /* a native constructor has a prototype of its own (the natives' methods none), Proxy aside */
        if (k == JK_NATIVE) return vm->objs[o].a == vm->proxy_first + JPX_PROXY || js_shape_find(vm, vm->objs[o].shape, A_PROTOTYPE) != 0;
        if (k == JK_BOUND) { v = jv_obj(vm->objs[o].a); continue; }
        if (k == JK_PROXY && vm->objs[o].c) { v = jv_obj(vm->objs[o].a); continue; }
        return 0;
    }
    return 0;
}

uint32_t js_typeof(js_vm *vm, jv v) {
    if (jv_is_num(v)) return A_NUMBER;
    if (jv_is_str(v)) return A_STRING;
    if (v == JV_TRUE || v == JV_FALSE) return A_BOOLEAN;
    if (v == JV_UNDEF || v == JV_HOLE) return A_UNDEFINED;
    if (v == JV_NULL) return A_OBJECT;
    if (jv_is_sym(v)) return A_SYMBOL;
    if (jv_is_big(v)) return A_BIGINT;
    return js_is_callable(vm, v) ? A_FUNCTION : A_OBJECT;
}

int js_strict_eq(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) return jv_dbl(a) == jv_dbl(b);
    if (jv_is_str(a) && jv_is_str(b)) return js_str_eq(vm, jv_idx(a), jv_idx(b));
    if (jv_is_big(a) && jv_is_big(b)) return js_big_eq(vm, a, b);
    return a == b;
}

int js_same_value_zero(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) {
        double x = jv_dbl(a), y = jv_dbl(b);
        return x == y || (x != x && y != y);
    }
    return js_strict_eq(vm, a, b);
}

int js_loose_eq(js_vm *vm, jv a, jv b) {
    for (int guard = 0; guard < 4; guard++) {
        if ((jv_is_num(a) && jv_is_num(b)) || (jv_is_str(a) && jv_is_str(b)) || (jv_is_obj(a) && jv_is_obj(b))) return js_strict_eq(vm, a, b);
        if (jv_is_sym(a) && jv_is_sym(b)) return a == b;
        if (jv_is_nullish(a) || jv_is_nullish(b)) return jv_is_nullish(a) && jv_is_nullish(b);
        /* a BigInt against a BigInt, a number (by value), a string (its digits, else unequal) */
        if (jv_is_big(a) && jv_is_big(b)) return js_big_eq(vm, a, b);
        if (jv_is_big(a) && jv_is_num(b)) return js_big_cmp_num(vm, a, jv_dbl(b)) == 0;
        if (jv_is_num(a) && jv_is_big(b)) return js_big_cmp_num(vm, b, jv_dbl(a)) == 0;
        if (jv_is_big(a) && jv_is_str(b)) { jv x = js_big_parse(vm, jv_idx(b), 0); return x != JV_HOLE && js_big_eq(vm, a, x); }
        if (jv_is_str(a) && jv_is_big(b)) { jv x = js_big_parse(vm, jv_idx(a), 0); return x != JV_HOLE && js_big_eq(vm, x, b); }
        if (jv_is_bool(a)) { a = jv_num(a == JV_TRUE ? 1 : 0); continue; }
        if (jv_is_bool(b)) { b = jv_num(b == JV_TRUE ? 1 : 0); continue; }
        if (jv_is_num(a) && jv_is_str(b)) return jv_dbl(a) == js_str_to_num(vm, jv_idx(b));
        if (jv_is_str(a) && jv_is_num(b)) return js_str_to_num(vm, jv_idx(a)) == jv_dbl(b);
        if (jv_is_obj(a)) { a = js_to_primitive(vm, a, -1); if (vm->throwing) return 0; continue; }
        if (jv_is_obj(b)) { b = js_to_primitive(vm, b, -1); if (vm->throwing) return 0; continue; }
        return 0;
    }
    return 0;
}

/* ---- property access ---- */

/* ---- a function's own properties as the engine keeps them ----
   A closure's and a native's length and name, a sloppy function's arguments and caller and a constructor's prototype
   (not yet made) are its own without its shape holding them, as V8's are accessors of the engine's: read (and the
   prototype made, in the shape, at its first reading) as asked. Length and name are read-only and configurable (a
   definition puts one in the shape, a deletion marks it gone: JO_FN_NOLENGTH, JO_FN_NONAME); arguments and caller
   neither writable nor configurable, never in the shape; the prototype writable, not configurable. A bound function
   has its length and name in its shape from its making. */

/* a function V8 gives arguments and caller of its own: a sloppy function that is neither an arrow, a method, a
   class's constructor, a generator nor async */
static int js_fn_sloppy(uint32_t ff) { return !(ff & (JF_STRICT | JF_ARROW | JF_METHOD | JF_CLASS_CTOR | JF_GENERATOR | JF_ASYNC | JF_GETTER | JF_TOPLEVEL)); }
/* a function with a prototype of its own: a constructor (a class's is made with its own) or a generator */
static int js_fn_constructs(uint32_t ff) { return (ff & JF_GENERATOR) || !(ff & (JF_ARROW | JF_METHOD | JF_ASYNC)); }
static int js_shape_live(js_vm *vm, uint32_t o, uint32_t atom) {
    uint32_t e = js_shape_find(vm, vm->objs[o].shape, atom);
    return e && vm->slots[vm->objs[o].slots + vm->shapes[e].slot] != JV_HOLE;
}

int js_fn_virtual(js_vm *vm, uint32_t o, uint32_t atom) {
    if (atom != A_LENGTH && atom != A_NAME && atom != A_PROTOTYPE && atom != A_ARGUMENTS && atom != A_CALLER) return 0;
    uint32_t k = vm->objs[o].kind;
    if (k != JK_CLOSURE && k != JK_NATIVE) return 0;
    uint32_t ff = k == JK_CLOSURE ? vm->funcs[vm->objs[o].a].flags : 0;
    if (atom == A_ARGUMENTS || atom == A_CALLER) return k == JK_CLOSURE && js_fn_sloppy(ff);
    if (js_shape_live(vm, o, atom)) return 0;
    if (atom == A_LENGTH) return !(vm->objs[o].flags & JO_FN_NOLENGTH);
    if (atom == A_NAME) return !(vm->objs[o].flags & JO_FN_NONAME);
    return k == JK_CLOSURE && js_fn_constructs(ff);
}

/* a sloppy function's arguments or caller (V8's FunctionArgumentsGetter, FunctionCallerGetter): of its newest call
   running, a new arguments object of what it was given, and the function that called it -- the scripts' code passed
   over, none when that one is strict; null when it is not running */
static jv js_fn_frame_prop(js_vm *vm, uint32_t o, uint32_t atom) {
    for (uint32_t fi = vm->nframes; fi-- > 0;) {
        if (vm->frames[fi].callee != o || vm->frames[fi].gen) continue;
        if (atom == A_ARGUMENTS) {
            js_frame fr = vm->frames[fi];
            uint32_t arr = js_array_new(vm, 0), n = fr.argc;
            if (fr.args) for (uint32_t i = 0; i < vm->objs[fr.args].b; i++) js_array_push(vm, arr, js_array_get(vm, fr.args, i));
            else {
                /* its parameters' slots (what it was given past them not kept) */
                uint32_t np = vm->funcs[fr.func].nparams;
                if (n > np) n = np;
                for (uint32_t i = 0; i < n; i++) js_array_push(vm, arr, vm->stack[fr.base + i]);
            }
            uint32_t a = js_obj_new(vm, JK_ARGUMENTS, vm->object_proto);
            vm->objs[a].a = arr;
            js_define(vm, a, A_LENGTH, jv_num(vm->objs[arr].b), JS_SHAPE_HIDDEN);
            js_define(vm, a, JS_KEY_ITERATOR, jv_obj(vm->array_values), JS_SHAPE_HIDDEN);
            js_define(vm, a, A_CALLEE, jv_obj(o), JS_SHAPE_HIDDEN);
            return jv_obj(a);
        }
        for (uint32_t ci = fi; ci-- > 0;) {
            uint32_t cf = vm->frames[ci].callee, ff = vm->funcs[vm->frames[ci].func].flags;
            if ((ff & JF_TOPLEVEL) || !cf || vm->objs[cf].kind != JK_CLOSURE) continue;
            return (ff & JF_STRICT) ? JV_NULL : jv_obj(cf);
        }
        return JV_NULL;
    }
    return JV_NULL;
}

int js_fn_lazy(js_vm *vm, uint32_t o, uint32_t atom, jv *out) {
    if (!js_fn_virtual(vm, o, atom)) return 0;
    uint32_t k = vm->objs[o].kind;
    if (k == JK_CLOSURE) {
        js_func f = vm->funcs[vm->objs[o].a];
        if (atom == A_ARGUMENTS || atom == A_CALLER) { *out = js_fn_frame_prop(vm, o, atom); return 1; }
        if (atom == A_PROTOTYPE) {
            uint32_t p;
            if (f.flags & JF_GENERATOR) p = js_obj_new(vm, JK_PLAIN, (f.flags & JF_ASYNC) ? vm->async_generator_proto : vm->generator_proto);
            else {
                p = js_obj_new(vm, JK_PLAIN, vm->object_proto);
                js_define(vm, p, A_CONSTRUCTOR, jv_obj(o), JS_SHAPE_HIDDEN);
            }
            js_define(vm, o, A_PROTOTYPE, jv_obj(p), JS_SHAPE_HIDDEN | JS_SHAPE_FIXED);
            *out = jv_obj(p);
            return 1;
        }
        if (atom == A_LENGTH) { *out = jv_num(f.arity); return 1; }
        *out = jv_str(f.name ? f.name : A_EMPTY);
        return 1;
    }
    if (atom == A_LENGTH) { *out = jv_num(vm->objs[o].b); return 1; }
    *out = jv_str(vm->objs[o].c ? vm->objs[o].c : A_EMPTY);
    return 1;
}
static int js_func_lazy(js_vm *vm, uint32_t o, uint32_t atom, jv *out) { return js_fn_lazy(vm, o, atom, out); }

static jv js_call_getter(js_vm *vm, jv acc, jv receiver) {
    jv g = vm->objs[jv_idx(acc)].x;
    if (!js_is_callable(vm, g)) return JV_UNDEF;
    return js_call(vm, g, receiver, 0, 0);
}

/* o's property atom, from o up its prototypes, with receiver as a getter's this; holder
   and where say where a plain data property was found (for the caches) */
static jv js_obj_get(js_vm *vm, uint32_t o, uint32_t atom, jv receiver, uint32_t *holder, uint32_t *where) {
    uint32_t x = o;
    for (int depth = 0; x && depth < 10000; depth++) {
        uint32_t k = vm->objs[x].kind;
        /* a proxy, here or up the prototypes: its get, never cached (holder left unset) */
        if (k == JK_PROXY) return js_proxy_get(vm, x, atom, receiver);
        if (k == JK_TYPEDARRAY) {
            uint32_t i;
            if (js_atom_index(vm, atom, &i)) return i < vm->objs[x].b ? js_ta_get(vm, x, i) : JV_UNDEF;
        }
        if (k == JK_ARRAY) {
            if (atom == A_LENGTH) return jv_num(vm->objs[x].b);
            uint32_t i;
            if (js_atom_index(vm, atom, &i)) {
                if (i < vm->objs[x].b && vm->slots[vm->objs[x].a + i] != JV_HOLE) return vm->slots[vm->objs[x].a + i];
            }
        } else if (k == JK_STRING || k == JK_ARGUMENTS) {
            if (k == JK_STRING) {
                uint32_t s = jv_idx(vm->objs[x].x);
                if (atom == A_LENGTH) return jv_num(vm->strs[s].len);
                uint32_t i;
                if (js_atom_index(vm, atom, &i) && i < vm->strs[s].len) return jv_str(js_str_sub(vm, s, i, i + 1));
            } else {
                /* an arguments object's dense element (its length an ordinary property) */
                uint32_t arr = vm->objs[x].a;
                uint32_t i;
                if (js_atom_index(vm, atom, &i) && i < vm->objs[arr].b && vm->slots[vm->objs[arr].a + i] != JV_HOLE) return vm->slots[vm->objs[arr].a + i];
            }
        } else if (k == JK_HOST) {
            int handled = 0;
            jv r = js_host_get(vm, x, atom, &handled);
            if (handled) return r;
        }
        uint32_t e = js_shape_find(vm, vm->objs[x].shape, atom);
        if (e) {
            jv v = vm->slots[vm->objs[x].slots + vm->shapes[e].slot];
            if (v != JV_HOLE) {
                if (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) return js_call_getter(vm, v, receiver);
                *holder = x;
                *where = e;
                return v;
            }
        }
        jv lz;
        if ((k == JK_CLOSURE || k == JK_NATIVE) && js_func_lazy(vm, x, atom, &lz)) return lz;
        x = vm->objs[x].proto;
    }
    return JV_UNDEF;
}

static uint32_t js_proto_of_primitive(js_vm *vm, jv v) {
    if (jv_is_num(v)) return vm->number_proto;
    if (jv_is_big(v)) return vm->bigint_proto;
    if (jv_is_str(v)) return vm->string_proto;
    if (jv_is_bool(v)) return vm->boolean_proto;
    if (jv_is_sym(v)) return vm->symbol_proto;
    return 0;
}

static jv js_get_from(js_vm *vm, jv objv, uint32_t atom, uint32_t *holder, uint32_t *where) {
    if (jv_is_obj(objv)) return js_obj_get(vm, jv_idx(objv), atom, objv, holder, where);
    if (jv_is_str(objv)) {
        uint32_t s = jv_idx(objv);
        if (atom == A_LENGTH) return jv_num(vm->strs[s].len);
        uint32_t i;
        if (js_atom_index(vm, atom, &i)) return i < vm->strs[s].len ? jv_str(js_str_sub(vm, s, i, i + 1)) : JV_UNDEF;
    }
    uint32_t p = js_proto_of_primitive(vm, objv);
    if (!p) {
        if (vm->throwing) return JV_HOLE;
        /* V8's: Symbol.iterator read from nothing is an iteration's error */
        if (atom == JS_KEY_ITERATOR) return js_not_iterable(vm, objv, 0, 0);
        uint32_t m = js_str_concat(vm, js_str_ascii(vm, objv == JV_NULL ? "Cannot read properties of null (reading '" : "Cannot read properties of undefined (reading '"),
                                   js_str_concat(vm, js_key_name(vm, atom), js_str_ascii(vm, "')")));
        return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
    }
    uint32_t h2 = 0, e2 = 0;
    return js_obj_get(vm, p, atom, objv, &h2, &e2);
}

jv js_get(js_vm *vm, jv obj, uint32_t atom) {
    uint32_t h = 0, e = 0;
    return js_get_from(vm, obj, atom, &h, &e);
}

/* a get, the cache at ic filled when a plain data property of the receiver or of its
   own prototype was found */
jv js_get_ic(js_vm *vm, jv obj, uint32_t atom, uint32_t ic) {
    uint32_t h = 0, e = 0;
    jv v = js_get_from(vm, obj, atom, &h, &e);
    if (h && e && jv_is_obj(obj) && !vm->throwing) {
        uint32_t o = jv_idx(obj);
        uint32_t k = vm->objs[o].kind;
        /* an array's (a string's, an arguments') named property is in its shape like any
           object's: cached; its elements and length are not, and never reach here */
        uint32_t idx;
        int named = atom != A_LENGTH && !js_atom_index(vm, atom, &idx);
        if (k != JK_HOST && (named || (k != JK_ARRAY && k != JK_STRING && k != JK_ARGUMENTS))) {
            js_ic c;
            c.shape = vm->objs[o].shape;
            c.slot = vm->shapes[e].slot;
            if (h == o) { c.holder = 0; c.hshape = 0; vm->ics[ic] = c; }
            else if (h == vm->objs[o].proto && vm->objs[h].kind != JK_HOST) { c.holder = h; c.hshape = vm->objs[h].shape; vm->ics[ic] = c; }
        }
    }
    return v;
}

static int js_call_setter(js_vm *vm, jv acc, jv receiver, jv v) {
    uint32_t s = vm->objs[jv_idx(acc)].a;
    if (!s) return 2;
    jv args[1] = { v };
    js_call(vm, jv_obj(s), receiver, args, 1);
    return !vm->throwing;
}

/* a [[Set]] o has no own property for, up its prototypes: a setter there called, a
   read-only property refusing (2), a proxy's or a host's own; -1 when none decides (the
   receiver's own property is then made) */
static int js_proto_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, jv objv) {
    for (uint32_t p = vm->objs[o].proto, depth = 0; p && depth < 10000; p = vm->objs[p].proto, depth++) {
        if (vm->objs[p].kind == JK_PROXY) return js_proxy_set(vm, p, atom, v, objv);
        if (vm->objs[p].kind == JK_HOST) {
            int handled = 0;
            int ok = js_host_set(vm, p, atom, v, &handled);
            if (handled) return ok;
        }
        if ((vm->objs[p].kind == JK_CLOSURE || vm->objs[p].kind == JK_NATIVE) && js_fn_virtual(vm, p, atom))
            return atom == A_PROTOTYPE && !(vm->objs[p].flags & JO_FROZEN) ? -1 : js_refuse(vm, JSR_READONLY, atom);
        uint32_t pe = js_shape_find(vm, vm->objs[p].shape, atom);
        if (!pe) continue;
        jv pv = vm->slots[vm->objs[p].slots + vm->shapes[pe].slot];
        if (pv == JV_HOLE) continue;                       /* deleted: not there */
        if (vm->shapes[pe].flags & JS_SHAPE_ACCESSOR) {
            int r = js_call_setter(vm, pv, objv, v);
            return r == 2 ? js_refuse(vm, JSR_GETTER, atom) : r;
        }
        if ((vm->shapes[pe].flags & JS_SHAPE_READONLY) || (vm->objs[p].flags & JO_FROZEN)) return js_refuse(vm, JSR_READONLY, atom);
        return -1;                                         /* a data property: the receiver's own is made */
    }
    return -1;
}

/* element i of an array or an arguments object set, itself the receiver: its dense slot,
   its own attributes' element (a setter, a read-only one), a prototype's say, else a new
   element (refused on an object not extensible, past a read-only length): 1 set, 2
   refused (vm->refused says why), 0 thrown; -1 for an index too far past an array's end
   for its dense elements (a property of its shape) */
static int js_elem_set(js_vm *vm, uint32_t o, uint32_t i, uint32_t atom, jv v, jv objv) {
    uint32_t st = js_elem_store(vm, o), fl = vm->objs[o].flags;
    if (i < vm->objs[st].b && vm->slots[vm->objs[st].a + i] != JV_HOLE) {
        if (fl & JO_FROZEN) return js_refuse(vm, JSR_READONLY, atom);
        vm->slots[vm->objs[st].a + i] = v;
        return 1;
    }
    uint32_t e = js_elem_entry(vm, o, i, atom);
    if (e) {
        jv cur = vm->slots[vm->objs[o].slots + vm->shapes[e].slot];
        if (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) {
            int r = js_call_setter(vm, cur, objv, v);
            return r == 2 ? js_refuse(vm, JSR_GETTER, atom) : r;
        }
        if ((vm->shapes[e].flags & JS_SHAPE_READONLY) || (fl & JO_FROZEN)) return js_refuse(vm, JSR_READONLY, atom);
        vm->slots[vm->objs[o].slots + vm->shapes[e].slot] = v;
        return 1;
    }
    int r = js_proto_set(vm, o, atom, v, objv);
    if (r >= 0) return r;
    if (!(fl & JO_EXTENSIBLE)) return js_refuse(vm, JSR_NOT_EXTENSIBLE, atom);
    if (vm->objs[o].kind == JK_ARRAY && i >= vm->objs[o].b && (fl & JO_LENGTH_RO)) return js_refuse(vm, JSR_LENGTH, A_LENGTH);
    if (i >= vm->objs[st].b + 1000000u) return -1;
    js_array_set(vm, st, i, v);
    return 1;
}

/* obj.atom = v; ic, when not 0, is filled for the next time. 1 set; 0 thrown; 2 refused
   (a primitive's property, a frozen object's, a read-only one, own or the nearest up the
   prototypes, a new one on an object not extensible, an accessor without a setter), which
   strict code throws for */
int js_set_ic(js_vm *vm, jv objv, uint32_t atom, jv v, uint32_t ic) {
    if (!jv_is_obj(objv)) {
        if (jv_is_nullish(objv) || objv == JV_HOLE) {
            uint32_t m = js_str_concat(vm, js_str_ascii(vm, objv == JV_NULL ? "Cannot set properties of null (setting '" : "Cannot set properties of undefined (setting '"),
                                       js_str_concat(vm, js_key_name(vm, atom), js_str_ascii(vm, "')")));
            js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
            return 0;
        }
        return 2;
    }
    uint32_t o = jv_idx(objv);
    uint32_t k = vm->objs[o].kind;
    vm->refused = JSR_NONE;
    if (k == JK_PROXY) return js_proxy_set(vm, o, atom, v, objv);
    if (k == JK_TYPEDARRAY) {
        uint32_t i;
        if (js_atom_index(vm, atom, &i)) return js_ta_set(vm, o, i, v);
    }
    if (k == JK_ARRAY || k == JK_ARGUMENTS) {
        uint32_t i;
        if (k == JK_ARRAY && atom == A_LENGTH) {
            /* not writable: refused before the value is read */
            if (vm->objs[o].flags & (JO_FROZEN | JO_LENGTH_RO)) return js_refuse(vm, JSR_LENGTH, A_LENGTH);
            double d = js_to_number(vm, v);
            if (vm->throwing) return 0;
            return js_array_set_length_checked(vm, o, d);
        }
        if (js_atom_index(vm, atom, &i)) {
            int r = js_elem_set(vm, o, i, atom, v, objv);
            if (r >= 0) return r;
        }
    } else if (k == JK_HOST) {
        int handled = 0;
        int ok = js_host_set(vm, o, atom, v, &handled);
        if (handled) return ok;
    } else if ((k == JK_CLOSURE || k == JK_NATIVE) && js_fn_virtual(vm, o, atom)) {
        /* a function's own the engine keeps: read-only but its prototype, which is made (into the shape) and written */
        if (atom != A_PROTOTYPE) return js_refuse(vm, JSR_READONLY, atom);
        jv pv;
        js_func_lazy(vm, o, atom, &pv);
    }
    uint32_t sh = vm->objs[o].shape;
    uint32_t e = js_shape_find(vm, sh, atom);
    if (e) {
        uint32_t slot = vm->shapes[e].slot;
        jv cur = vm->slots[vm->objs[o].slots + slot];
        if (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) { if (cur != JV_HOLE) return js_call_setter(vm, cur, objv, v); }
        else if (((vm->shapes[e].flags & JS_SHAPE_READONLY) || (vm->objs[o].flags & JO_FROZEN)) && cur != JV_HOLE) return 2;
        else if (cur != JV_HOLE || (vm->objs[o].flags & JO_EXTENSIBLE)) {
            vm->slots[vm->objs[o].slots + slot] = v;
            /* a host object's writes are not cached: the host sees each one */
            if (ic && cur != JV_HOLE && k != JK_HOST) { js_ic c = { sh, 0, 0, slot }; vm->ics[ic] = c; }
            return 1;
        }
    }
    int pr = js_proto_set(vm, o, atom, v, objv);
    if (pr >= 0) return pr;
    if (!(vm->objs[o].flags & JO_EXTENSIBLE)) return 2;
    uint32_t ns = js_shape_add(vm, sh, atom, 0);
    uint32_t slot = vm->shapes[ns].slot;
    js_obj_room(vm, o, slot);
    vm->objs[o].shape = ns;
    vm->slots[vm->objs[o].slots + slot] = v;
    if (ic && k != JK_HOST) { js_ic c = { sh, 0xFFFFFFFFu, ns, slot }; vm->ics[ic] = c; }
    return 1;
}

int js_set(js_vm *vm, jv obj, uint32_t atom, jv v) { return js_set_ic(vm, obj, atom, v, 0); }

/* strict code's assignment the object refused: a TypeError naming the property */
static void js_set_refused(js_vm *vm, jv obj, uint32_t key) {
    uint32_t name = js_key_name(vm, key), m;
    if (!jv_is_obj(obj)) {
        /* V8's words: the primitive's type and its text */
        const char *ty = jv_is_str(obj) ? "' on string '" : jv_is_num(obj) ? "' on number '" : jv_is_big(obj) ? "' on bigint '" :
                         jv_is_sym(obj) ? "' on symbol '" : "' on boolean '";
        uint32_t v = jv_is_sym(obj) ? js_sym_text(vm, jv_idx(obj)) : js_to_string(vm, obj);
        if (vm->throwing) return;
        m = js_str_concat(vm, js_str_ascii(vm, "Cannot create property '"),
                          js_str_concat(vm, name, js_str_concat(vm, js_str_ascii(vm, ty), js_str_concat(vm, v, js_str_ascii(vm, "'")))));
        js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
        return;
    }
    /* why: an array's write says (vm->refused); else the property read-only (own, up the
       prototypes, or the object frozen), only a getter's, or a new one on an object not
       extensible -- each in V8's words, the object as V8 writes it ([object Array], #<Object>) */
    uint32_t o = jv_idx(obj), why = vm->refused;
    if (why) { key = vm->refused_key; name = js_key_name(vm, key); }
    vm->refused = JSR_NONE;
    if (!why) {
        /* the nearest holder of the key says: only a getter, read-only (its own attribute,
           or the holder frozen); a frozen or non-extensible object's new property else */
        why = JSR_NOT_EXTENSIBLE;
        for (uint32_t p = o, depth = 0; p && depth < 10000; p = vm->objs[p].proto, depth++) {
            uint32_t e = js_shape_find(vm, vm->objs[p].shape, key);
            if (!e || vm->slots[vm->objs[p].slots + vm->shapes[e].slot] == JV_HOLE) continue;
            if (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) why = JSR_GETTER;
            else if ((vm->shapes[e].flags & JS_SHAPE_READONLY) || (vm->objs[p].flags & JO_FROZEN)) why = JSR_READONLY;
            break;
        }
    }
    vector<char> t;
    js_value_text(vm, obj, &t);
    uint32_t of = js_str_utf8(vm, data(t), len(t));
    if (why == JSR_READONLY || why == JSR_LENGTH)
        m = js_str_concat(vm, js_str_ascii(vm, "Cannot assign to read only property '"),
                          js_str_concat(vm, name, js_str_concat(vm, js_str_ascii(vm, js_is_callable(vm, obj) ? "' of function '" : "' of object '"),
                                                                js_str_concat(vm, of, js_str_ascii(vm, "'")))));
    else if (why == JSR_GETTER)
        m = js_str_concat(vm, js_str_ascii(vm, "Cannot set property "),
                          js_str_concat(vm, name, js_str_concat(vm, js_str_ascii(vm, " of "), js_str_concat(vm, of, js_str_ascii(vm, " which has only a getter")))));
    else if (why == JSR_DELETE)
        m = js_str_concat(vm, js_str_ascii(vm, "Cannot delete property '"), js_str_concat(vm, name, js_str_concat(vm, js_str_ascii(vm, "' of "), of)));
    else m = js_str_concat(vm, js_str_ascii(vm, "Cannot add property "), js_str_concat(vm, name, js_str_ascii(vm, ", object is not extensible")));
    js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
}

/* strict code's delete the object refused (a property not configurable): V8's TypeError */
static void js_delete_refused(js_vm *vm, jv obj, uint32_t key) {
    uint32_t name = js_key_name(vm, key);
    vector<char> t;
    js_value_text(vm, obj, &t);
    uint32_t m = js_str_concat(vm, js_str_ascii(vm, "Cannot delete property '"),
                               js_str_concat(vm, name, js_str_concat(vm, js_str_ascii(vm, "' of "), js_str_utf8(vm, data(t), len(t)))));
    js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
}

int js_set_throw(js_vm *vm, jv obj, jv key, jv v) {
    int r = js_set_value(vm, obj, key, v);
    if (vm->throwing) return 0;
    if (r != 2) return 1;
    int ok = 1;
    uint32_t ka = js_prop_key(vm, key, &ok);
    if (!ok) return 0;
    js_set_refused(vm, obj, ka);
    return 0;
}

int js_delete_throw(js_vm *vm, jv obj, jv key) {
    int ok = 1;
    uint32_t ka = js_prop_key(vm, key, &ok);
    if (!ok) return 0;
    if (!jv_is_obj(obj)) return 1;
    int r = js_delete(vm, jv_idx(obj), ka);
    if (vm->throwing) return 0;
    if (!r) { js_delete_refused(vm, obj, ka); return 0; }
    return 1;
}

/* a key's atom, or an array index */
static int js_key(js_vm *vm, jv key, uint32_t *atom, uint32_t *index) {
    if (jv_is_num(key)) {
        double d = jv_dbl(key);
        if (d >= 0 && d < 4294967295.0 && d == trunc(d)) { *index = (uint32_t)d; *atom = 0; return 2; }
    }
    if (jv_is_sym(key)) { *atom = JS_SYM_KEY | jv_idx(key); return 1; }
    int ok = 1;
    *atom = js_prop_key(vm, key, &ok);
    if (!ok) return 0;
    if (js_key_is_sym(*atom)) return 1;
    uint32_t i;
    if (js_atom_index(vm, *atom, &i)) { *index = i; return 3; }
    return 1;
}

jv js_get_value(js_vm *vm, jv obj, jv key) {
    uint32_t atom = 0, index = 0;
    int kk = js_key(vm, key, &atom, &index);
    if (!kk) return JV_HOLE;
    if (kk >= 2) {
        if (jv_is_obj(obj)) {
            uint32_t o = jv_idx(obj);
            if (vm->objs[o].kind == JK_ARRAY && index < vm->objs[o].b) {
                jv v = vm->slots[vm->objs[o].a + index];
                if (v != JV_HOLE) return v;
            }
            /* a typed array's element, or nothing past its end (no prototype has its indexes) */
            if (vm->objs[o].kind == JK_TYPEDARRAY) return index < vm->objs[o].b ? js_ta_get(vm, o, index) : JV_UNDEF;
        } else if (jv_is_str(obj)) {
            uint32_t s = jv_idx(obj);
            return index < vm->strs[s].len ? jv_str(js_str_sub(vm, s, index, index + 1)) : JV_UNDEF;
        }
        if (!atom) atom = js_atom(vm, js_num_to_str(vm, (double)index));
    }
    return js_get(vm, obj, atom);
}

int js_set_value(js_vm *vm, jv obj, jv key, jv v) {
    uint32_t atom = 0, index = 0;
    int kk = js_key(vm, key, &atom, &index);
    if (!kk) return 0;
    if (kk >= 2 && jv_is_obj(obj)) {
        uint32_t o = jv_idx(obj);
        if (vm->objs[o].kind == JK_ARRAY && (vm->objs[o].flags & JO_ELEM_SLOW) == JO_EXTENSIBLE && index < vm->objs[o].b + 1000000u) {
            js_array_set(vm, o, index, v);
            return 1;
        }
        if (vm->objs[o].kind == JK_TYPEDARRAY) return js_ta_set(vm, o, index, v);
    }
    if (!atom) atom = js_atom(vm, js_num_to_str(vm, (double)index));
    return js_set(vm, obj, atom, v);
}

int js_has(js_vm *vm, uint32_t o, uint32_t atom) {
    for (uint32_t x = o, depth = 0; x && depth < 10000; x = vm->objs[x].proto, depth++) {
        uint32_t k = vm->objs[x].kind;
        if (k == JK_PROXY) return js_proxy_has(vm, x, atom);
        uint32_t i;
        if (k == JK_TYPEDARRAY && js_atom_index(vm, atom, &i)) return i < vm->objs[x].b;
        if (k == JK_ARRAY) {
            if (atom == A_LENGTH) return 1;
            if (js_atom_index(vm, atom, &i) && i < vm->objs[x].b && vm->slots[vm->objs[x].a + i] != JV_HOLE) return 1;
        }
        if (k == JK_ARGUMENTS) {
            uint32_t st = vm->objs[x].a;
            if (js_atom_index(vm, atom, &i) && i < vm->objs[st].b && vm->slots[vm->objs[st].a + i] != JV_HOLE) return 1;
        }
        if (k == JK_STRING && jv_is_str(vm->objs[x].x)) {
            /* a String's wrapper: its length and its indexes */
            if (atom == A_LENGTH) return 1;
            if (js_atom_index(vm, atom, &i) && i < vm->strs[jv_idx(vm->objs[x].x)].len) return 1;
        }
        if (k == JK_HOST) {
            int handled = 0;
            jv r = js_host_get(vm, x, atom, &handled);
            if (handled && r != JV_UNDEF) return 1;
        }
        jv v;
        uint32_t f;
        if (js_get_own(vm, x, atom, &v, &f)) return 1;
        if ((k == JK_CLOSURE || k == JK_NATIVE) && js_fn_virtual(vm, x, atom)) return 1;
    }
    return 0;
}

/* the own enumerable (or every) string keys of o, as strings pushed onto arr: array
   indexes first, ascending, then the names in the order they were made */
/* ascending, by merging runs that double (a stable sort, n log n) */
static void js_sort_u64(vector<uint64_t> *v) {
    size_t n = len(*v);
    if (n < 2) return;
    vector<uint64_t> tmp;
    resize(tmp, n);
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            size_t a = lo, b = mid, k = lo;
            while (a < mid && b < hi) { if ((*v)[b] < (*v)[a]) tmp[k++] = (*v)[b++]; else tmp[k++] = (*v)[a++]; }
            while (a < mid) tmp[k++] = (*v)[a++];
            while (b < hi) tmp[k++] = (*v)[b++];
        }
        for (size_t k = 0; k < n; k++) (*v)[k] = tmp[k];
    }
}

void js_own_keys(js_vm *vm, uint32_t o, int enumerable_only, uint32_t arr) {
    uint32_t k = vm->objs[o].kind;
    if (k == JK_PROXY) { js_proxy_own_keys(vm, o, 1, 0, enumerable_only, arr); return; }
    /* the shape's entries, newest first: collected, then reversed. The integer keys come
       first, ascending (index << 32 | atom, sorted), then the names as they were added */
    vector<uint32_t> es;
    for (uint32_t x = vm->objs[o].shape; x && vm->shapes[x].count; x = vm->shapes[x].parent) push_back(es, x);
    vector<uint64_t> ix;
    for (size_t q = len(es); q > 0; q--) {
        uint32_t e = at(es, q - 1);
        if (vm->slots[vm->objs[o].slots + vm->shapes[e].slot] == JV_HOLE) continue;
        if (enumerable_only && (vm->shapes[e].flags & JS_SHAPE_HIDDEN)) continue;
        uint32_t i;
        if (js_atom_index(vm, vm->shapes[e].atom, &i)) push_back(ix, (uint64_t)i << 32 | vm->shapes[e].atom);
    }
    /* the exotic objects' own indices, before the shape's: an array's (an arguments
       object's) dense elements, merged in order with those its shape keeps */
    uint32_t n = 0, st = 0;
    if (k == JK_TYPEDARRAY) n = vm->objs[o].b;
    else if (k == JK_STRING) n = vm->strs[jv_idx(vm->objs[o].x)].len;
    else if (k == JK_ARRAY || k == JK_ARGUMENTS) { st = k == JK_ARGUMENTS ? vm->objs[o].a : o; n = vm->objs[st].b; }
    if (len(ix) && n) {
        for (uint32_t i = 0; i < n; i++) if (!st || vm->slots[vm->objs[st].a + i] != JV_HOLE) push_back(ix, (uint64_t)i << 32);
    } else
        for (uint32_t i = 0; i < n; i++) if (!st || vm->slots[vm->objs[st].a + i] != JV_HOLE) js_array_push(vm, arr, jv_str(js_num_to_str(vm, (double)i)));
    js_sort_u64(&ix);
    for (size_t q = 0; q < len(ix); q++) {
        uint32_t atom = (uint32_t)(at(ix, q) & 0xFFFFFFFFu);
        js_array_push(vm, arr, jv_str(atom ? atom : js_num_to_str(vm, (double)(uint32_t)(at(ix, q) >> 32))));
    }
    /* an array's or a String's length, not enumerable, the first of its names */
    if (!enumerable_only && (k == JK_ARRAY || k == JK_STRING)) js_array_push(vm, arr, jv_str(A_LENGTH));
    /* a function's length, name, a sloppy one's arguments and caller and its prototype, first and in that order (V8's),
       whether the engine keeps them or its shape does */
    int fnk = !enumerable_only && (k == JK_CLOSURE || k == JK_NATIVE || k == JK_BOUND);
    if (fnk) {
        static const uint32_t fn_names[] = { A_LENGTH, A_NAME, A_ARGUMENTS, A_CALLER, A_PROTOTYPE };
        for (uint32_t q = 0; q < 5; q++)
            if (js_shape_live(vm, o, fn_names[q]) || js_fn_virtual(vm, o, fn_names[q])) js_array_push(vm, arr, jv_str(fn_names[q]));
    }
    for (size_t q = len(es); q > 0; q--) {
        uint32_t e = at(es, q - 1);
        if (vm->slots[vm->objs[o].slots + vm->shapes[e].slot] == JV_HOLE) continue;
        if (enumerable_only && (vm->shapes[e].flags & JS_SHAPE_HIDDEN)) continue;
        uint32_t i, a = vm->shapes[e].atom;
        if (js_key_is_sym(a) || js_atom_index(vm, a, &i)) continue;
        if (fnk && (a == A_LENGTH || a == A_NAME || a == A_ARGUMENTS || a == A_CALLER || a == A_PROTOTYPE)) continue;
        js_array_push(vm, arr, jv_str(a));
    }
}

void js_own_symbols(js_vm *vm, uint32_t o, uint32_t arr) {
    if (vm->objs[o].kind == JK_PROXY) { js_proxy_own_keys(vm, o, 0, 1, 0, arr); return; }
    vector<uint32_t> es;
    for (uint32_t x = vm->objs[o].shape; x && vm->shapes[x].count; x = vm->shapes[x].parent) push_back(es, x);
    for (size_t q = len(es); q > 0; q--) {
        uint32_t e = at(es, q - 1);
        if (vm->slots[vm->objs[o].slots + vm->shapes[e].slot] == JV_HOLE) continue;
        uint32_t k = vm->shapes[e].atom;
        if (js_key_is_sym(k) && vm->syms[k & ~JS_SYM_KEY].reg != JS_SYM_PRIVATE) js_array_push(vm, arr, jv_sym(k & ~JS_SYM_KEY));
    }
}

int js_instance_of(js_vm *vm, jv v, jv ctor) {
    if (!jv_is_obj(ctor)) { js_throw_msg(vm, vm->type_error_proto, "Right-hand side of 'instanceof' is not an object"); return 0; }
    /* the constructor's own test, Symbol.hasInstance, once any object was given one */
    if (vm->has_instance) {
        jv h = js_get(vm, ctor, JS_SYM_KEY | JSYM_HAS_INSTANCE);
        if (vm->throwing) return 0;
        if (!jv_is_nullish(h)) {
            if (!js_is_callable(vm, h)) { js_not_function(vm, h); return 0; }
            jv a1[1] = { v };
            jv r = js_call(vm, h, ctor, a1, 1);
            return !vm->throwing && js_truthy(vm, r);
        }
    }
    if (!js_is_callable(vm, ctor)) { js_throw_msg(vm, vm->type_error_proto, "Right-hand side of 'instanceof' is not callable"); return 0; }
    if (vm->objs[jv_idx(ctor)].kind == JK_BOUND) ctor = jv_obj(vm->objs[jv_idx(ctor)].a);
    if (!jv_is_obj(v)) return 0;
    jv p = js_get(vm, ctor, A_PROTOTYPE);
    if (vm->throwing) return 0;
    if (!jv_is_obj(p)) {
        vector<char> m;
        js_put(&m, "Function has non-object prototype '");
        js_value_text(vm, p, &m);
        js_put(&m, "' in instanceof check");
        js_throw_chars(vm, vm->type_error_proto, &m);
        return 0;
    }
    /* up the prototypes, a proxy's its getPrototypeOf's */
    uint32_t x = jv_idx(v);
    for (uint32_t depth = 0; depth < 10000; depth++) {
        if (vm->objs[x].kind == JK_PROXY) {
            jv pp = js_proxy_get_proto(vm, x);
            if (vm->throwing) return 0;
            x = jv_is_obj(pp) ? jv_idx(pp) : 0;
        } else x = vm->objs[x].proto;
        if (!x) return 0;
        if (x == jv_idx(p)) return 1;
    }
    return 0;
}

/* ---- private names (ECMA-262 7.3.31, PrivateGet, PrivateSet, PrivateFieldAdd, PrivateMethodOrAccessorAdd) ----
   An object's private elements are its own properties under a private name's symbol: found in its own shape alone
   (no prototype, no proxy's trap, no host's hook), written whether it is frozen or not, added whether it is
   extensible or not. kind is the compiler's: field 1, method 2, getter 4, setter 8, static 16 */
#define JPV_FIELD 1
#define JPV_METHOD 2
#define JPV_GET 4
#define JPV_SET 8
#define JPV_STATIC 16
static uint32_t js_priv_slot(js_vm *vm, jv ov, jv key, uint32_t *flags) {
    if (!jv_is_obj(ov) || !jv_is_sym(key)) return 0;
    uint32_t o = jv_idx(ov), e = js_shape_find(vm, vm->objs[o].shape, JS_SYM_KEY | jv_idx(key));
    if (!e) return 0;
    uint32_t s = vm->objs[o].slots + vm->shapes[e].slot;
    if (vm->slots[s] == JV_HOLE) return 0;
    *flags = vm->shapes[e].flags;
    return s + 1;
}
/* V8's words: a method's or an accessor's brand missing, a field's member */
static void js_priv_missing(js_vm *vm, uint32_t kind, uint32_t name, uint32_t cls, int write) {
    vector<char> m;
    if (kind & (JPV_METHOD | JPV_GET | JPV_SET)) {
        js_put(&m, (kind & JPV_STATIC) ? "Receiver must be class " : "Receiver must be an instance of class ");
        if (cls) js_str_to_utf8(vm, cls, &m);
        else js_put(&m, "anonymous");
    } else {
        js_put(&m, write ? "Cannot write private member " : "Cannot read private member ");
        js_str_to_utf8(vm, name, &m);
        js_put(&m, write ? " to an object whose class did not declare it" : " from an object whose class did not declare it");
    }
    js_throw_chars(vm, vm->type_error_proto, &m);
}
static void js_priv_error(js_vm *vm, const char *a, uint32_t name, const char *b) {
    vector<char> m;
    js_put(&m, a);
    js_str_to_utf8(vm, name, &m);
    js_put(&m, b);
    js_throw_chars(vm, vm->type_error_proto, &m);
}
static jv js_priv_get(js_vm *vm, jv ov, jv key, uint32_t kind, uint32_t name, uint32_t cls) {
    uint32_t fl = 0, s = js_priv_slot(vm, ov, key, &fl);
    if (!s) { js_priv_missing(vm, kind, name, cls, 0); return JV_UNDEF; }
    jv v = vm->slots[s - 1];
    if (!(fl & JS_SHAPE_ACCESSOR)) return v;
    jv g = vm->objs[jv_idx(v)].x;
    if (!js_is_callable(vm, g)) { js_priv_error(vm, "'", name, "' was defined without a getter"); return JV_UNDEF; }
    return js_call(vm, g, ov, 0, 0);
}
static void js_priv_put(js_vm *vm, jv ov, jv key, jv v, uint32_t kind, uint32_t name, uint32_t cls) {
    uint32_t fl = 0, s = js_priv_slot(vm, ov, key, &fl);
    if (!s) { js_priv_missing(vm, kind, name, cls, 1); return; }
    jv cur = vm->slots[s - 1];
    if (fl & JS_SHAPE_ACCESSOR) {
        uint32_t st = vm->objs[jv_idx(cur)].a;
        if (!st) { js_priv_error(vm, "'", name, "' was defined without a setter"); return; }
        jv a1[1] = { v };
        js_call(vm, jv_obj(st), ov, a1, 1);
        return;
    }
    if (fl & JS_SHAPE_READONLY) { js_priv_error(vm, "Private method '", name, "' is not writable"); return; }
    vm->slots[s - 1] = v;
}
/* a field, a method or an accessor added: refused when the object has it (V8's words, a method's naming its class) */
static void js_priv_add(js_vm *vm, jv ov, jv key, jv v, jv g, jv st, uint32_t kind, uint32_t name, uint32_t cls) {
    uint32_t fl = 0;
    if (!jv_is_obj(ov)) { js_priv_missing(vm, kind, name, cls, 1); return; }
    if (js_priv_slot(vm, ov, key, &fl)) {
        if (kind & JPV_FIELD) { js_priv_error(vm, "Cannot initialize ", name, " twice on the same object"); return; }
        vector<char> m;
        js_put(&m, "Cannot initialize private methods of class ");
        if (cls) js_str_to_utf8(vm, cls, &m);
        else js_put(&m, "anonymous");
        js_put(&m, " twice on the same object");
        js_throw_chars(vm, vm->type_error_proto, &m);
        return;
    }
    uint32_t o = jv_idx(ov), k = JS_SYM_KEY | jv_idx(key);
    if (kind & (JPV_GET | JPV_SET)) js_define_accessor(vm, o, k, g, st, JS_SHAPE_HIDDEN | JS_SHAPE_FIXED);
    else js_define(vm, o, k, v, JS_SHAPE_HIDDEN | JS_SHAPE_FIXED | ((kind & JPV_METHOD) ? JS_SHAPE_READONLY : 0));
}

/* ---- functions ---- */

uint32_t js_closure_new(js_vm *vm, uint32_t func, uint32_t env, jv thisv) {
    uint32_t c = js_obj_new(vm, JK_CLOSURE, vm->function_proto);
    vm->objs[c].a = func;
    vm->objs[c].b = env;
    vm->objs[c].x = thisv;
    if (!(vm->funcs[func].flags & (JF_ARGUMENTS | JF_HAS_REST | JF_CLASS_CTOR))) vm->objs[c].flags |= JO_FASTCALL;
    return c;
}

uint32_t js_native_new(js_vm *vm, uint32_t id, uint32_t name, uint32_t nargs) {
    uint32_t c = js_obj_new(vm, JK_NATIVE, vm->function_proto);
    vm->objs[c].a = id;
    vm->objs[c].b = nargs;
    vm->objs[c].c = name;
    vm->objs[c].x = JV_UNDEF;
    return c;
}

/* ---- operators' slow paths ---- */

static jv js_add_slow(js_vm *vm, jv a, jv b) {
    jv pa = js_to_primitive(vm, a, -1);
    if (vm->throwing) return JV_HOLE;
    jv pb = js_to_primitive(vm, b, -1);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_str(pa) || jv_is_str(pb)) {
        uint32_t sa = js_to_string(vm, pa);
        if (!sa) return JV_HOLE;
        uint32_t sb = js_to_string(vm, pb);
        if (!sb) return JV_HOLE;
        return jv_str(js_str_concat(vm, sa, sb));
    }
    if (jv_is_big(pa) || jv_is_big(pb)) {
        if (jv_is_big(pa) && jv_is_big(pb)) return js_big_binop(vm, OP_ADD, pa, pb);
        return js_throw_msg(vm, vm->type_error_proto, "Cannot mix BigInt and other types, use explicit conversions");
    }
    double x = js_to_number(vm, pa), y = js_to_number(vm, pb);
    return jv_num(x + y);
}

/* a < b (or b < a, swapped): 1, 0, or -1 for undefined (a NaN) */
static int js_less(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) {
        double x = jv_dbl(a), y = jv_dbl(b);
        if (x != x || y != y) return -1;
        return x < y;
    }
    jv pa = js_to_primitive(vm, a, 0);
    if (vm->throwing) return 0;
    jv pb = js_to_primitive(vm, b, 0);
    if (vm->throwing) return 0;
    if (jv_is_str(pa) && jv_is_str(pb)) return js_str_cmp(vm, jv_idx(pa), jv_idx(pb)) < 0;
    if (jv_is_big(pa) || jv_is_big(pb)) {
        /* a BigInt against a BigInt, a number, or a string's digits (none: undefined) */
        if (jv_is_str(pa)) { pa = js_big_parse(vm, jv_idx(pa), 0); if (pa == JV_HOLE) return -1; }
        if (jv_is_str(pb)) { pb = js_big_parse(vm, jv_idx(pb), 0); if (pb == JV_HOLE) return -1; }
        if (jv_is_big(pa) && jv_is_big(pb)) return js_big_cmp(vm, pa, pb) < 0;
        if (jv_is_big(pa)) {
            double y = js_to_number(vm, pb);
            if (vm->throwing) return 0;
            int c = js_big_cmp_num(vm, pa, y);
            return c == 2 ? -1 : c < 0;
        }
        double x = js_to_number(vm, pa);
        if (vm->throwing) return 0;
        int c = js_big_cmp_num(vm, pb, x);
        return c == 2 ? -1 : c > 0;
    }
    double x = js_to_number(vm, pa), y = js_to_number(vm, pb);
    if (x != x || y != y) return -1;
    return x < y;
}

static double js_mod(double a, double b) {
    if (a > -9007199254740992.0 && a < 9007199254740992.0 && b > -9007199254740992.0 && b < 9007199254740992.0) {
        int64_t ia = (int64_t)a, ib = (int64_t)b;
        if ((double)ia == a && (double)ib == b && ib != 0) {
            /* the sign the dividend's: -0 for a zero remainder of a negative (or -0) */
            int64_t r = ia >= -2147483647 && ia <= 2147483647 && ib >= -2147483647 && ib <= 2147483647
                      ? (int64_t)((int32_t)ia % (int32_t)ib) : ia % ib;
            if (r == 0 && signbit(a)) return -0.0;
            return (double)r;
        }
    }
    if (b != b || a != a || b == 0 || a == INFINITY || a == -INFINITY) return NAN;
    if (b == INFINITY || b == -INFINITY) return a;
    if (a == trunc(a) && b == trunc(b) && fabs(a) < 9007199254740992.0 && fabs(b) < 9007199254740992.0) {
        int64_t r = (int64_t)a % (int64_t)b;
        if (r == 0 && a < 0) return -0.0;
        return (double)r;
    }
    return fmod(a, b);
}

static double js_pow(double a, double b) {
    if (b != b) return NAN;
    if (b == 0) return 1;
    if ((a == 1 || a == -1) && (b == INFINITY || b == -INFINITY)) return NAN;
    return pow(a, b);
}

/* ---- iteration ---- */

static uint32_t js_iter_new(js_vm *vm, uint32_t source, uint32_t mode) {
    uint32_t it = js_obj_new(vm, JK_ITER, 0);
    vm->objs[it].a = source;
    vm->objs[it].b = 0;
    vm->objs[it].c = mode;
    return it;
}

/* for-in's keys: the enumerable string keys of the object and its prototypes, each once */
static uint32_t js_iter_keys(js_vm *vm, jv v) {
    uint32_t keys = js_array_new(vm, 0);
    if (jv_is_obj(v) || jv_is_str(v)) {
        jv ov = js_to_object(vm, v);
        uint32_t all = js_array_new(vm, 0);
        for (uint32_t x = jv_idx(ov), depth = 0; x && depth < 1000; x = vm->objs[x].proto, depth++) {
            uint32_t from = vm->objs[all].b;
            js_own_keys(vm, x, 1, all);
            for (uint32_t i = from; i < vm->objs[all].b; i++) {
                jv k = js_array_get(vm, all, i);
                int dup = 0;
                for (uint32_t j = 0; j < vm->objs[keys].b && !dup; j++) if (js_str_eq(vm, jv_idx(js_array_get(vm, keys, j)), jv_idx(k))) dup = 1;
                if (!dup) js_array_push(vm, keys, k);
            }
        }
    }
    return js_iter_new(vm, keys, JI_KEYS);
}

/* an iterable's iterator, a TypeError naming it as the site at pc says (0: a native's) */
static jv js_iter_of_at(js_vm *vm, jv v, uint32_t pc);
jv js_iter_of(js_vm *vm, jv v) { return js_iter_of_at(vm, v, 0); }
static jv js_iter_of_at(js_vm *vm, jv v, uint32_t pc) {
    if (jv_is_str(v)) return jv_obj(js_iter_new(vm, jv_idx(v), JI_STRING));
    if (jv_is_obj(v)) {
        uint32_t o = jv_idx(v);
        uint32_t k = vm->objs[o].kind;
        if (k == JK_ARRAY || k == JK_TYPEDARRAY) return jv_obj(js_iter_new(vm, o, JI_ARRAY));
        /* an arguments object by its length and its elements, as Array.prototype.values reads one */
        if (k == JK_ARGUMENTS) return jv_obj(js_iter_new(vm, o, JI_ARRAY));
        if (k == JK_MAP) return jv_obj(js_iter_new(vm, o, JI_MAP));
        if (k == JK_SET) return jv_obj(js_iter_new(vm, o, JI_SET));
        if (k == JK_ITER) return v;
        if (k == JK_GENERATOR) return jv_obj(js_iter_new(vm, o, JI_GEN));
        if (k == JK_STRING) return jv_obj(js_iter_new(vm, jv_idx(vm->objs[o].x), JI_STRING));
        /* the iteration protocol: obj[Symbol.iterator](), its next */
        jv f = js_get(vm, v, JS_KEY_ITERATOR);
        if (vm->throwing) return JV_HOLE;
        if (js_is_callable(vm, f)) {
            jv it = js_call(vm, f, v, 0, 0);
            if (vm->throwing) return JV_HOLE;
            if (!jv_is_obj(it)) return js_throw_msg(vm, vm->type_error_proto, "Result of the Symbol.iterator method is not an object");
            uint32_t io = jv_idx(it);
            if (vm->objs[io].kind == JK_ITER) return it;
            if (vm->objs[io].kind == JK_GENERATOR) return jv_obj(js_iter_new(vm, io, JI_GEN));
            jv next = js_get(vm, it, A_NEXT);
            if (vm->throwing) return JV_HOLE;
            uint32_t w = js_iter_new(vm, io, JI_PROTOCOL);
            vm->objs[w].x = next;
            return jv_obj(w);
        }
        /* an array-like host collection: by its length */
        if (k == JK_HOST) {
            jv n = js_get(vm, v, A_LENGTH);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_num(n)) return jv_obj(js_iter_new(vm, o, JI_ARRAY));
        }
    }
    return js_not_iterable(vm, v, pc, 0);
}

/* the iterator's next value into *out: 0 when it is done */
int js_iter_step(js_vm *vm, uint32_t it, jv *out) {
    uint32_t src = vm->objs[it].a, i = vm->objs[it].b, mode = vm->objs[it].c;
    if (mode == JI_HELPER) return js_helper_step(vm, it, out);
    if (mode == JI_GEN) {
        if (vm->objs[it].b) return 0;
        int done = 0;
        jv r = js_gen_resume(vm, src, JV_UNDEF, 0, &done);
        if (vm->throwing || done) { vm->objs[it].b = 1; return 0; }
        *out = r;
        return 1;
    }
    if (mode == JI_PROTOCOL) {
        if (vm->objs[it].b) return 0;
        jv next = vm->objs[it].x;
        if (!js_is_callable(vm, next)) { js_not_function(vm, next); return 0; }
        /* a next that threw (or gave no object) leaves the iterator done: it is not closed */
        vm->objs[it].b = 1;
        jv r = js_call(vm, next, jv_obj(src), 0, 0);
        if (vm->throwing) return 0;
        if (!jv_is_obj(r)) { js_result_error(vm, r); return 0; }
        jv d = js_get(vm, r, A_DONE);
        if (vm->throwing || js_truthy(vm, d)) return 0;
        *out = js_get(vm, r, A_VALUE);
        if (vm->throwing) return 0;
        vm->objs[it].b = 0;
        return 1;
    }
    if (mode == JI_KEYS || mode == JI_ARRAY || mode == JI_ARRAY_KEYS || mode == JI_ARRAY_ENTRIES || mode == JI_REGEXP) {
        /* past the end once, done for good (the array may grow after) */
        if (i == 0xFFFFFFFFu) return 0;
        uint32_t n;
        if (vm->objs[src].kind == JK_ARRAY || vm->objs[src].kind == JK_TYPEDARRAY) n = vm->objs[src].b;
        else {
            jv lv = js_get(vm, jv_obj(src), A_LENGTH);
            if (vm->throwing) return 0;
            double d = js_to_number(vm, lv);
            if (vm->throwing) return 0;
            n = d != d || d <= 0 ? 0 : d >= 4294967295.0 ? 0xFFFFFFFEu : (uint32_t)d;
        }
        if (i >= n) { vm->objs[it].b = 0xFFFFFFFFu; return 0; }
        if (mode == JI_ARRAY_KEYS) { *out = jv_num((double)i); vm->objs[it].b = i + 1; return 1; }
        jv x;
        if (vm->objs[src].kind == JK_ARRAY) {
            /* a hole read as Get reads it: an element with attributes of its own, a getter's */
            x = vm->slots[vm->objs[src].a + i];
            if (x == JV_HOLE && (vm->objs[src].flags & JO_ELEMPROPS)) {
                x = js_get_value(vm, jv_obj(src), jv_num((double)i));
                if (vm->throwing) return 0;
            }
            if (x == JV_HOLE) x = JV_UNDEF;
        } else if (vm->objs[src].kind == JK_TYPEDARRAY) x = js_ta_get(vm, src, i);
        else {
            x = js_get_value(vm, jv_obj(src), jv_num((double)i));
            if (vm->throwing) return 0;
        }
        if (mode == JI_ARRAY_ENTRIES) {
            uint32_t pair = js_array_new(vm, 0);
            js_array_push(vm, pair, jv_num((double)i));
            js_array_push(vm, pair, x);
            x = jv_obj(pair);
        }
        *out = x;
        vm->objs[it].b = i + 1;
        return 1;
    }
    if (mode == JI_STRING) {
        if (i >= vm->strs[src].len) return 0;
        uint16_t c = js_str_at(vm, src, i);
        /* a surrogate pair is one code point; a lone surrogate, high or low, one of its own */
        uint32_t w = 1;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < vm->strs[src].len) {
            uint16_t d = js_str_at(vm, src, i + 1);
            if (d >= 0xDC00 && d <= 0xDFFF) w = 2;
        }
        *out = jv_str(js_str_sub(vm, src, i, i + w));
        vm->objs[it].b = i + w;
        return 1;
    }
    /* a Map's or a Set's live entries: an array of keys and values, the dead holes; past the end, done for good */
    uint32_t arr = vm->objs[src].a, step = mode == JI_SET || mode == JI_SET_ENTRIES ? 1 : 2;
    while (i < vm->objs[arr].b && vm->slots[vm->objs[arr].a + i] == JV_HOLE) i += step;
    if (i >= vm->objs[arr].b) { vm->objs[it].b = 0xFFFFFFFFu; return 0; }
    jv k = vm->slots[vm->objs[arr].a + i];
    if (mode == JI_SET || mode == JI_MAP_KEYS) *out = k;
    else if (mode == JI_MAP_VALUES) *out = vm->slots[vm->objs[arr].a + i + 1];
    else {
        uint32_t pair = js_array_new(vm, 0);
        js_array_push(vm, pair, k);
        js_array_push(vm, pair, mode == JI_SET_ENTRIES ? k : vm->slots[vm->objs[arr].a + i + 1]);
        *out = jv_obj(pair);
    }
    vm->objs[it].b = i + step;
    return 1;
}

/* an array pattern's values from an iterable that is not an array: the first n its
   iterator gives (0xFFFF: all, for a rest element), the iterator closed when the
   pattern leaves it before its end */
static jv js_iter_take(js_vm *vm, jv v, uint32_t n, uint32_t pc) {
    jv it = js_iter_of_at(vm, v, pc);
    if (vm->throwing) return JV_HOLE;
    uint32_t arr = js_array_new(vm, 0);
    for (uint32_t k = 0; n == 0xFFFF || k < n; k++) {
        jv x = JV_UNDEF;
        if (!js_iter_step(vm, jv_idx(it), &x)) return vm->throwing ? JV_HOLE : jv_obj(arr);
        js_array_push(vm, arr, x);
    }
    js_iter_close(vm, jv_idx(it));
    return vm->throwing ? JV_HOLE : jv_obj(arr);
}

/* an iterator left before its end (a for-of's break, return or throw): a generator
   resumed to return, the protocol's return() called; an exception in flight kept */
void js_iter_close(js_vm *vm, uint32_t it) {
    if (!it || vm->objs[it].kind != JK_ITER) return;
    uint32_t mode = vm->objs[it].c, src = vm->objs[it].a;
    if (mode == JI_HELPER) {
        /* a helper closed through its own return (which closes what it reads) */
        int hw = vm->throwing;
        jv hs = vm->exception;
        vm->throwing = 0;
        jv rf = js_get(vm, jv_obj(it), A_RETURN);
        if (!vm->throwing && js_is_callable(vm, rf)) js_call(vm, rf, jv_obj(it), 0, 0);
        if (hw) { vm->throwing = hw; vm->exception = hs; }
        return;
    }
    if ((mode != JI_GEN && mode != JI_PROTOCOL) || vm->objs[it].b) return;
    vm->objs[it].b = 1;
    int was = vm->throwing;
    jv saved = vm->exception;
    vm->throwing = 0;
    if (mode == JI_GEN) {
        int done = 0;
        js_gen_resume(vm, src, JV_UNDEF, 2, &done);
    } else {
        jv rf = js_get(vm, jv_obj(src), A_RETURN);
        if (!vm->throwing && js_is_callable(vm, rf)) {
            jv r = js_call(vm, rf, jv_obj(src), 0, 0);
            if (!vm->throwing && !jv_is_obj(r)) js_result_error(vm, r);
        }
    }
    if (was) { vm->throwing = was; vm->exception = saved; }
}

jv js_iter_result(js_vm *vm, jv v, int done) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, A_VALUE, v, 0);
    js_define(vm, r, A_DONE, jv_bool(done), 0);
    return jv_obj(r);
}

/* a sync iterator sent next (mode 0), throw (1) or return (2), with arg when has_arg:
   its result's value and done; 1 when sent, 0 when it has no such method, -1 when it
   threw (a result that is not an object a TypeError) */
int js_iter_send(js_vm *vm, uint32_t it, int mode, jv arg, int has_arg, jv *value, int *done) {
    uint32_t src = vm->objs[it].a, im = vm->objs[it].c;
    *value = JV_UNDEF;
    *done = 1;
    if (im == JI_GEN) {
        int d = 0;
        jv r = js_gen_resume(vm, src, arg, mode, &d);
        if (vm->throwing) { vm->objs[it].b = 1; return -1; }
        if (d) vm->objs[it].b = 1;
        *value = r;
        *done = d;
        return 1;
    }
    if (im == JI_PROTOCOL) {
        jv m = vm->objs[it].x;
        if (mode != 0) {
            m = js_get(vm, jv_obj(src), mode == 1 ? A_THROW : A_RETURN);
            if (vm->throwing) return -1;
            if (jv_is_nullish(m)) return 0;
        }
        if (!js_is_callable(vm, m)) { js_not_function(vm, m); return -1; }
        jv a1[1] = { arg };
        jv r = js_call(vm, m, jv_obj(src), a1, has_arg ? 1 : 0);
        if (vm->throwing) return -1;
        if (!jv_is_obj(r)) { js_result_error(vm, r); return -1; }
        jv d = js_get(vm, r, A_DONE);
        if (vm->throwing) return -1;
        *done = js_truthy(vm, d);
        *value = js_get(vm, r, A_VALUE);
        return vm->throwing ? -1 : 1;
    }
    /* a builtin iterator: next alone */
    if (mode != 0) return 0;
    jv v = JV_UNDEF;
    if (js_iter_step(vm, it, &v)) { *value = v; *done = 0; return 1; }
    return vm->throwing ? -1 : 1;
}

/* for await's and an async yield*'s iterator (GetIterator, async): a record of the
   value's Symbol.asyncIterator's iterator and its next, else of its sync iterator,
   seen as an async one */
static jv js_aiter_of_at(js_vm *vm, jv v, uint32_t pc);
jv js_aiter_of(js_vm *vm, jv v) { return js_aiter_of_at(vm, v, 0); }
static jv js_aiter_of_at(js_vm *vm, jv v, uint32_t pc) {
    /* V8's: nothing's Symbol.asyncIterator a property read from nothing */
    jv f = js_get(vm, v, JS_KEY_ASYNC_ITERATOR);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_nullish(f)) {
        jv sf = jv_is_obj(v) || jv_is_str(v) ? js_get(vm, v, JS_KEY_ITERATOR) : JV_UNDEF;
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, sf)) return js_not_iterable(vm, v, pc, 1);
        jv it = js_iter_of_at(vm, v, pc);
        if (vm->throwing) return JV_HOLE;
        return jv_obj(js_iter_new(vm, jv_idx(it), JI_FROM_SYNC));
    }
    if (!js_is_callable(vm, f)) return js_not_iterable(vm, v, pc, 1);
    jv it = js_call(vm, f, v, 0, 0);
    if (vm->throwing) return JV_HOLE;
    if (!jv_is_obj(it)) return js_throw_msg(vm, vm->type_error_proto, "Result of the Symbol.asyncIterator method is not an object");
    jv next = js_get(vm, it, A_NEXT);
    if (vm->throwing) return JV_HOLE;
    uint32_t rec = js_iter_new(vm, jv_idx(it), JI_ASYNC);
    vm->objs[rec].x = next;
    return jv_obj(rec);
}

/* ---- generators and async runs ----
   A suspended frame lives in its object's array: [pc, env, callee, argc, func, flags,
   this, new.target, arguments, handlers' count], then each try handler of the frame
   (pc, sp from the base, env), then its slots and operands from its base. Resumed, it
   is a frame again at the top of the stack, entered from C (its return comes back to
   js_gen_resume), the value it was sent and how pushed for its GEN_RESUME. */
#define JS_GEN_HEAD 10u

static void js_gen_save(js_vm *vm, uint32_t fi, uint32_t pc, uint32_t sp) {
    js_frame f = vm->frames[fi];
    uint32_t g = f.gen;
    uint32_t arr = vm->objs[g].b;
    if (!arr) { arr = js_array_new(vm, 0); vm->objs[g].b = arr; }
    else js_array_set_length(vm, arr, 0);
    uint32_t nh = vm->nhandlers - f.handlers;
    jv head[JS_GEN_HEAD] = { jv_num(pc), jv_num(f.env), jv_num(f.callee), jv_num(f.argc), jv_num(f.func),
                             jv_num(f.flags & ~(uint32_t)JFR_NATIVE_ENTRY), f.thisv, f.newt, jv_num(f.args), jv_num(nh) };
    for (uint32_t i = 0; i < JS_GEN_HEAD; i++) js_array_push(vm, arr, head[i]);
    for (uint32_t h = f.handlers; h < vm->nhandlers; h++) {
        js_handler hd = vm->handlers[h];
        js_array_push(vm, arr, jv_num(hd.pc));
        js_array_push(vm, arr, jv_num(hd.sp - f.base));
        js_array_push(vm, arr, jv_num(hd.env));
    }
    for (uint32_t i = f.base; i < sp; i++) js_array_push(vm, arr, vm->stack[i]);
}

static uint32_t js_gen_num(js_vm *vm, uint32_t arr, uint32_t i) { return (uint32_t)jv_dbl(vm->slots[vm->objs[arr].a + i]); }

jv js_gen_resume(js_vm *vm, uint32_t g, jv value, int mode, int *done) {
    *done = 1;
    uint32_t st = vm->objs[g].a;
    if (st == JG_RUNNING) return js_throw_msg(vm, vm->type_error_proto, "Generator is already running");
    if (st == JG_DONE || (st == JG_START && mode != 0)) {
        vm->objs[g].a = JG_DONE;
        vm->objs[g].b = 0;
        if (mode == 1) return js_throw(vm, value);
        return mode == 2 ? value : JV_UNDEF;
    }
    if (vm->depth >= JS_DEPTH_MAX) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    uint32_t arr = vm->objs[g].b;
    uint32_t total = vm->objs[arr].b, nh = js_gen_num(vm, arr, 9);
    uint32_t nstack = total - JS_GEN_HEAD - 3 * nh;
    uint32_t base = vm->sp, n = vm->nframes;
    if (!js_room(vm, base + nstack + 2 + JS_STACK_MARGIN, n + 2) || vm->nhandlers + nh > len(vm->handlers))
        return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    js_frame fr;
    memset(&fr, 0, sizeof fr);
    fr.pc = js_gen_num(vm, arr, 0);
    fr.env = js_gen_num(vm, arr, 1);
    fr.callee = js_gen_num(vm, arr, 2);
    fr.argc = js_gen_num(vm, arr, 3);
    fr.func = js_gen_num(vm, arr, 4);
    fr.flags = js_gen_num(vm, arr, 5) | JFR_NATIVE_ENTRY;
    fr.thisv = vm->slots[vm->objs[arr].a + 6];
    fr.newt = vm->slots[vm->objs[arr].a + 7];
    fr.args = js_gen_num(vm, arr, 8);
    fr.base = base;
    fr.sp0 = base;
    fr.handlers = vm->nhandlers;
    fr.gen = g;
    for (uint32_t h = 0; h < nh; h++) {
        js_handler hd;
        hd.frame = n;
        hd.pc = js_gen_num(vm, arr, JS_GEN_HEAD + 3 * h);
        hd.sp = base + js_gen_num(vm, arr, JS_GEN_HEAD + 3 * h + 1);
        hd.env = js_gen_num(vm, arr, JS_GEN_HEAD + 3 * h + 2);
        vm->handlers[vm->nhandlers++] = hd;
    }
    {
        uint64_t *stack = data(vm->stack);
        const uint64_t *slots = data(vm->slots);
        uint32_t from = vm->objs[arr].a + JS_GEN_HEAD + 3 * nh;
        for (uint32_t i = 0; i < nstack; i++) stack[base + i] = slots[from + i];
        uint32_t sp = base + nstack;
        if (st == JG_YIELD || st == JG_AWAIT) { stack[sp] = value; stack[sp + 1] = jv_num(mode); sp += 2; }
        vm->sp = sp;
    }
    js_frame *frames = data(vm->frames);
    frames[n] = fr;
    vm->nframes = n + 1;
    vm->objs[g].a = JG_RUNNING;
    vm->depth++;
    jv r = js_run(vm, n);
    vm->depth--;
    if (vm->throwing) { vm->objs[g].a = JG_DONE; vm->objs[g].b = 0; return JV_HOLE; }
    *done = vm->objs[g].a == JG_DONE;
    return r;
}

/* yield*'s step: the inner iterator sent the value as next, throw or return; *fin when
   it finished (*ret: as a return), *out its value then, else the value to yield */
static void js_yield_star(js_vm *vm, jv it, jv recv, int mode, jv *out, int *fin, int *ret) {
    *fin = 0;
    *ret = 0;
    jv res = JV_UNDEF;
    if (jv_is_obj(it) && vm->objs[jv_idx(it)].kind == JK_GENERATOR) {
        int done = 0;
        jv r = js_gen_resume(vm, jv_idx(it), recv, mode, &done);
        if (vm->throwing) return;
        *out = r;
        if (done) { *fin = 1; *ret = mode == 2; }
        return;
    }
    if (mode == 0) {
        jv nf = js_get(vm, it, A_NEXT);
        if (vm->throwing) return;
        jv a1[1] = { recv };
        res = js_call(vm, nf, it, a1, 1);
    } else {
        jv m = js_get(vm, it, mode == 1 ? A_THROW : A_RETURN);
        if (vm->throwing) return;
        if (jv_is_nullish(m)) {
            if (mode == 2) { *fin = 1; *ret = 1; *out = recv; return; }
            jv rf = js_get(vm, it, A_RETURN);
            if (!vm->throwing && js_is_callable(vm, rf)) js_call(vm, rf, it, 0, 0);
            if (!vm->throwing) js_throw_msg(vm, vm->type_error_proto, "The iterator does not provide a 'throw' method");
            return;
        }
        jv a1[1] = { recv };
        res = js_call(vm, m, it, a1, 1);
    }
    if (vm->throwing) return;
    if (!jv_is_obj(res)) { js_result_error(vm, res); return; }
    jv d = js_get(vm, res, A_DONE);
    if (vm->throwing) return;
    *out = js_get(vm, res, A_VALUE);
    if (vm->throwing) return;
    if (js_truthy(vm, d)) { *fin = 1; *ret = mode == 2; }
}

/* an iterable's iterator object (yield*'s): what Symbol.iterator gives; a generator itself */
static jv js_iterator_object(js_vm *vm, jv v, uint32_t pc) {
    if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_GENERATOR) return v;
    if (jv_is_nullish(v)) return js_not_iterable(vm, v, pc, 0);
    jv f = js_get(vm, v, JS_KEY_ITERATOR);
    if (vm->throwing) return JV_HOLE;
    if (!js_is_callable(vm, f)) return js_not_iterable(vm, v, pc, 0);
    jv it = js_call(vm, f, v, 0, 0);
    if (vm->throwing) return JV_HOLE;
    if (!jv_is_obj(it)) return js_throw_msg(vm, vm->type_error_proto, "Result of the Symbol.iterator method is not an object");
    return it;
}

/* ---- calls ---- */

/* a constructor's prototype property: its own data property, else through js_get */
static jv js_ctor_proto(js_vm *vm, jv ctor) {
    if (jv_is_obj(ctor)) {
        uint32_t o = jv_idx(ctor);
        uint32_t e = js_shape_find(vm, vm->objs[o].shape, A_PROTOTYPE);
        if (e && !(vm->shapes[e].flags & JS_SHAPE_ACCESSOR)) {
            jv v = vm->slots[vm->objs[o].slots + vm->shapes[e].slot];
            if (v != JV_HOLE) return v;
        }
    }
    return js_get(vm, ctor, A_PROTOTYPE);
}

/* room on the value stack for sp values and in the frames for n frames: each grows by
   doubling, up to its maximum (0 past it: the caller throws a RangeError). The
   interpreter's borrows are taken again after any call, so a growth moves nothing it
   holds. */
int js_room(js_vm *vm, uint32_t sp, uint32_t n) {
    if (sp > JS_STACK_MAX || n > JS_FRAMES_MAX) return 0;
    if (sp > len(vm->stack)) {
        size_t m = len(vm->stack) * 2;
        if (m < sp) m = sp;
        if (m > JS_STACK_MAX) m = JS_STACK_MAX;
        resize(vm->stack, m, JV_UNDEF);
    }
    if (n > len(vm->frames)) {
        size_t m = len(vm->frames) * 2;
        if (m < n) m = n;
        if (m > JS_FRAMES_MAX) m = JS_FRAMES_MAX;
        js_frame f0;
        memset(&f0, 0, sizeof f0);
        resize(vm->frames, m, f0);
    }
    return 1;
}

/* a frame for the closure c, its argc arguments the values below vm->sp */
static int js_enter(js_vm *vm, uint32_t c, jv thisv, uint32_t argc, jv newt, uint32_t flags, uint32_t sp0) {
    if (!js_room(vm, vm->sp + JS_STACK_MARGIN + vm->funcs[vm->objs[c].a].nslots, vm->nframes + 2)) {
        js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
        return 0;
    }
    /* a sloppy function's this: undefined and null the global object, a primitive its wrapper */
    if (!(vm->funcs[vm->objs[c].a].flags & (JF_STRICT | JF_ARROW)) && !(flags & JFR_CONSTRUCT) &&
        !jv_is_obj(thisv) && !jv_is_nullish(thisv) && thisv != JV_HOLE)
        thisv = js_to_object(vm, thisv);
    const js_obj *objs = data(vm->objs);
    const js_func *funcs = data(vm->funcs);
    uint32_t func = objs[c].a;
    uint32_t ff = funcs[func].flags, np = funcs[func].nparams, ns = funcs[func].nslots;
    uint32_t base = vm->sp - argc;
    js_frame fr;
    fr.gen = 0;
    fr.func = func;
    fr.pc = funcs[func].code;
    fr.base = base;
    fr.env = objs[c].b;
    fr.callee = c;
    fr.argc = argc;
    fr.sp0 = sp0;
    fr.flags = flags;
    fr.newt = newt;
    fr.handlers = vm->nhandlers;
    fr.args = 0;
    if (ff & JF_ARROW) fr.thisv = objs[c].x;
    else if (!(ff & JF_STRICT) && (jv_is_nullish(thisv) || thisv == JV_HOLE) && !(flags & JFR_CONSTRUCT)) fr.thisv = jv_obj(vm->global);
    else fr.thisv = thisv;
    if (ff & (JF_ARGUMENTS | JF_HAS_REST)) {
        uint32_t arr = js_array_new(vm, argc);
        for (uint32_t i = 0; i < argc; i++) vm->slots[vm->objs[arr].a + i] = vm->stack[base + i];
        fr.args = arr;
    }
    uint64_t *stack = data(vm->stack);
    uint32_t sp = base + (argc > np ? np : argc);
    while (sp < base + ns) stack[sp++] = JV_UNDEF;
    vm->sp = sp;
    js_frame *frames = data(vm->frames);
    frames[vm->nframes++] = fr;
    return 1;
}

static jv js_call_bound(js_vm *vm, uint32_t b, uint32_t args, uint32_t argc, jv newt, int construct) {
    uint32_t n = vm->objs[b].c;
    vector<jv> all;
    for (uint32_t i = 0; i < n; i++) push_back(all, vm->slots[vm->objs[b].b + i]);
    for (uint32_t i = 0; i < argc; i++) push_back(all, vm->stack[args + i]);
    jv target = jv_obj(vm->objs[b].a);
    if (construct) return js_construct(vm, target, data(all), (uint32_t)len(all), newt == jv_obj(b) ? target : newt);
    return js_call(vm, target, vm->objs[b].x, data(all), (uint32_t)len(all));
}

/* a class's heritage (ClassDefinitionEvaluation's): null, or a constructor whose prototype
   is an object or null. Its prototype object's parent and its constructor's parent into
   *proto and *ctor: 1; else 0, V8's TypeError thrown (the value as it reads without
   running it, a function by its source) */
static int js_class_heritage(js_vm *vm, jv parent, uint32_t *proto, uint32_t *ctor) {
    if (parent == JV_NULL) { *proto = 0; return 1; }
    if (!js_is_constructor(vm, parent)) { js_extends_error(vm, parent, 0); return 0; }
    jv pp = js_get(vm, parent, A_PROTOTYPE);
    if (vm->throwing) return 0;
    if (!jv_is_obj(pp) && pp != JV_NULL) { js_extends_error(vm, pp, 1); return 0; }
    *proto = jv_is_obj(pp) ? jv_idx(pp) : 0;
    *ctor = jv_idx(parent);
    return 1;
}

/* a function constructed by the interpreter's new or super() that is no constructor: the
   error of the instruction the top frame is at (its pc past the operands; the _ARR forms
   have none), V8's words naming its expression */
static jv js_new_fail(js_vm *vm, jv fn) {
    uint32_t fi = vm->nframes - 1, pc = vm->frames[fi].pc;
    uint32_t b1 = pc >= 1 ? vm->code[pc - 1] : 0;
    uint32_t op = b1 == OP_NEW_ARR || b1 == OP_SUPER_CALL_ARR ? b1 : pc >= 3 ? vm->code[pc - 3] : OP_NEW;
    js_call_fail(vm, op, fi, fn);
    return JV_HOLE;
}

/* a call of a function that is not a closure: a native's, or a bound one's */
static jv js_call_other(js_vm *vm, jv fn, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    uint32_t o = jv_idx(fn);
    /* constructed (newt given): a constructor only. js_construct has asked already; the
       interpreter's new and super() ask here, out of js_run's hot path */
    if (newt != JV_UNDEF && !js_is_constructor(vm, fn)) return js_new_fail(vm, fn);
    if (vm->objs[o].kind == JK_PROXY) return js_proxy_call(vm, o, thisv, args, argc, newt);
    if (vm->objs[o].kind == JK_NATIVE) {
        uint32_t id = vm->objs[o].a;
        if (id >= JS_HOST_NATIVES) {
            int handled = 0;
            jv r = js_host_call(vm, id, fn, thisv, args, argc, &handled);
            if (handled) return r;
        }
        jv r = js_native_call(vm, id, fn, thisv, args, argc, newt);
        /* a class extending a native (super() reaching Error, Array, Map): the object the
           native made takes new.target's prototype */
        if (jv_is_obj(newt) && newt != fn && jv_is_obj(r) && !vm->throwing) {
            jv p = js_ctor_proto(vm, newt);
            if (!vm->throwing && jv_is_obj(p) && vm->objs[jv_idx(r)].proto != jv_idx(p)) js_set_proto(vm, jv_idx(r), jv_idx(p));
        }
        return r;
    }
    return js_call_bound(vm, o, args, argc, newt, newt != JV_UNDEF);
}

jv js_call(js_vm *vm, jv fn, jv thisv, const jv *argv, uint32_t argc) {
    if (vm->throwing) return JV_HOLE;
    /* a native or the host calling back into script, nested: bounded before the C stack is */
    if (vm->depth >= JS_DEPTH_MAX) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    uint32_t sp0 = vm->sp;
    if (jv_is_obj(fn) && (vm->objs[jv_idx(fn)].flags & JO_FASTCALL) && vm->nframes + 1 < len(vm->frames) &&
        (jv_is_obj(thisv) || jv_is_nullish(thisv) || (vm->funcs[vm->objs[jv_idx(fn)].a].flags & (JF_STRICT | JF_ARROW)))) {
        /* a closure the interpreter runs, the common case (a native's callback): its
           frame made here, read and written through borrows */
        uint32_t o = jv_idx(fn);
        const js_obj *objs = data(vm->objs);
        const js_func *funcs = data(vm->funcs);
        uint32_t func = objs[o].a;
        uint32_t ff = funcs[func].flags, np = funcs[func].nparams, ns = funcs[func].nslots;
        if (sp0 + argc + ns + JS_STACK_MARGIN < len(vm->stack)) {
            js_frame fr;
            fr.gen = 0;
            fr.func = func;
            fr.pc = funcs[func].code;
            fr.base = sp0 + 2;
            fr.env = objs[o].b;
            fr.callee = o;
            fr.argc = argc;
            fr.sp0 = sp0;
            fr.flags = JFR_NATIVE_ENTRY;
            fr.newt = JV_UNDEF;
            fr.handlers = vm->nhandlers;
            fr.args = 0;
            if (ff & JF_ARROW) fr.thisv = objs[o].x;
            else if (!(ff & JF_STRICT) && (jv_is_nullish(thisv) || thisv == JV_HOLE)) fr.thisv = jv_obj(vm->global);
            else fr.thisv = thisv;
            uint64_t *stack = data(vm->stack);
            stack[sp0] = thisv;
            stack[sp0 + 1] = fn;
            uint32_t base = sp0 + 2, given = argc > np ? np : argc;
            for (uint32_t i = 0; i < argc; i++) stack[base + i] = argv[i];
            for (uint32_t i = given; i < ns; i++) stack[base + i] = JV_UNDEF;
            vm->sp = base + (ns > given ? ns : given);
            js_frame *frames = data(vm->frames);
            uint32_t n = vm->nframes;
            frames[n] = fr;
            vm->nframes = n + 1;
            vm->depth++;
            jv r = js_run(vm, n);
            vm->depth--;
            vm->sp = sp0;
            return r;
        }
    }
    if (!js_is_callable(vm, fn)) return js_not_function(vm, fn);
    if (!js_room(vm, sp0 + argc + JS_STACK_MARGIN, vm->nframes + 2)) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    {
        uint64_t *stack = data(vm->stack);
        stack[sp0] = thisv;
        stack[sp0 + 1] = fn;
        for (uint32_t i = 0; i < argc; i++) stack[sp0 + 2 + i] = argv[i];
        vm->sp = sp0 + 2 + argc;
    }
    uint32_t o = jv_idx(fn);
    jv r;
    if (vm->objs[o].kind == JK_CLOSURE) {
        if (vm->objs[o].flags & JO_CLASS_CTOR) { vm->sp = sp0; return js_class_call_error(vm, o); }
        if (!js_enter(vm, o, thisv, argc, JV_UNDEF, JFR_NATIVE_ENTRY, sp0)) { vm->sp = sp0; return JV_HOLE; }
        vm->depth++;
        r = js_run(vm, vm->nframes - 1);
        vm->depth--;
    } else r = js_call_other(vm, fn, thisv, sp0 + 2, argc, JV_UNDEF);
    vm->sp = sp0;
    return r;
}

/* new fn(...): the object its prototype makes, unless the function returns one */
jv js_construct(js_vm *vm, jv fn, const jv *argv, uint32_t argc, jv newt) {
    if (vm->throwing) return JV_HOLE;
    if (vm->depth >= JS_DEPTH_MAX) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    if (!js_is_constructor(vm, fn)) return js_not_constructor(vm, fn);
    uint32_t o = jv_idx(fn);
    uint32_t sp0 = vm->sp;
    if (!js_room(vm, sp0 + argc + JS_STACK_MARGIN, vm->nframes + 2)) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    vm->stack[vm->sp++] = JV_UNDEF;
    vm->stack[vm->sp++] = fn;
    for (uint32_t i = 0; i < argc; i++) vm->stack[vm->sp++] = argv[i];
    jv r;
    if (vm->objs[o].kind == JK_CLOSURE) {
        js_func f = vm->funcs[vm->objs[o].a];
        if (f.flags & (JF_ARROW | JF_METHOD | JF_GENERATOR | JF_ASYNC)) { vm->sp = sp0; return js_not_constructor(vm, fn); }
        jv thisv = JV_HOLE;
        if (!(vm->objs[o].flags & JO_DERIVED)) {
            jv p = js_ctor_proto(vm, newt);
            if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
            thisv = jv_obj(js_obj_new(vm, JK_PLAIN, jv_is_obj(p) ? jv_idx(p) : vm->object_proto));
        }
        if (!js_enter(vm, o, thisv, argc, newt, JFR_NATIVE_ENTRY | JFR_CONSTRUCT, sp0)) { vm->sp = sp0; return JV_HOLE; }
        vm->depth++;
        r = js_run(vm, vm->nframes - 1);
        vm->depth--;
    } else r = js_call_other(vm, fn, JV_HOLE, sp0 + 2, argc, newt);
    vm->sp = sp0;
    return r;
}

/* ---- the interpreter ---- */

static uint32_t js_rd16(const uint8_t *c, uint32_t p) { uint16_t v; memcpy(&v, c + p, 2); return v; }
static uint32_t js_rd32(const uint8_t *c, uint32_t p) { uint32_t v; memcpy(&v, c + p, 4); return v; }
#define RD16(p) js_rd16(code, p)
#define RD32(p) js_rd32(code, p)
#define CALLABLE(v) (jv_is_obj(v) && (objs[jv_idx(v)].kind == JK_CLOSURE || objs[jv_idx(v)].kind == JK_NATIVE || objs[jv_idx(v)].kind == JK_BOUND || \
                     (objs[jv_idx(v)].kind == JK_PROXY && objs[jv_idx(v)].c)))
#define RELOAD() (code = data(vm->code), stack = data(vm->stack), objs = data(vm->objs), funcs = data(vm->funcs), frames = data(vm->frames), slots = data(vm->slots))
#define SYNC() (vm->sp = sp)

static jv js_ref_error(js_vm *vm, uint32_t atom, const char *what) {
    uint32_t m = js_str_concat(vm, atom, js_str_ascii(vm, what));
    return js_throw(vm, jv_obj(js_error_new(vm, vm->reference_error_proto, m)));
}
/* a let, const, class or import read before its declaration ran (its temporal dead zone) */
static jv js_tdz_error(js_vm *vm, uint32_t atom) {
    uint32_t m = js_str_concat(vm, js_str_ascii(vm, "Cannot access '"), js_str_concat(vm, atom, js_str_ascii(vm, "' before initialization")));
    return js_throw(vm, jv_obj(js_error_new(vm, vm->reference_error_proto, m)));
}


uint32_t js_env_new(js_vm *vm, uint32_t parent, uint32_t n) {
    uint32_t e = js_obj_new(vm, JK_ENV, 0);
    vm->objs[e].slots = js_slots_alloc(vm, n ? n : 1);
    vm->objs[e].cap = n;
    vm->objs[e].a = parent;
    vm->objs[e].b = n;
    return e;
}

/* the global name atom: the script's lexical bindings, then the global object */
static jv js_global_get(js_vm *vm, uint32_t atom, uint32_t ic, int *found) {
    jv v;
    uint32_t f;
    *found = 1;
    if (js_get_own(vm, vm->global_lex, atom, &v, &f)) {
        if (ic) {
            uint32_t e = js_shape_find(vm, vm->objs[vm->global_lex].shape, atom);
            js_ic c = { 0, vm->global_lex, vm->objs[vm->global_lex].shape, vm->shapes[e].slot };
            vm->ics[ic] = c;
        }
        return v;
    }
    uint32_t g = vm->global;
    if (js_has(vm, g, atom)) {
        uint32_t h = 0, e = 0;
        jv r = js_obj_get(vm, g, atom, jv_obj(g), &h, &e);
        if (ic && h == g && e) { js_ic c = { 0, g, vm->objs[g].shape, vm->shapes[e].slot }; vm->ics[ic] = c; }
        return r;
    }
    *found = 0;
    return JV_UNDEF;
}

/* the operators' slow paths, shared by their instructions: the operands converted
   (valueOf and toString may run, and throw: NaN then, vm->throwing set) */
/* a - b, a * b, a / b, a % b, a ** b, a & b ... a >>> b for any operands: numbers, or
   BigInts both (one of each is a TypeError) */
static jv js_arith_slow(js_vm *vm, uint32_t op, jv a, jv b) {
    jv pa = jv_is_num(a) ? a : js_to_primitive(vm, a, 0);
    if (vm->throwing) return JV_HOLE;
    jv pb = jv_is_num(b) ? b : js_to_primitive(vm, b, 0);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_big(pa) || jv_is_big(pb)) {
        if (jv_is_big(pa) && jv_is_big(pb)) return js_big_binop(vm, op, pa, pb);
        return js_throw_msg(vm, vm->type_error_proto, "Cannot mix BigInt and other types, use explicit conversions");
    }
    double x = jv_is_num(pa) ? jv_dbl(pa) : js_to_number(vm, pa);
    if (vm->throwing) return JV_HOLE;
    double y = jv_is_num(pb) ? jv_dbl(pb) : js_to_number(vm, pb);
    if (vm->throwing) return JV_HOLE;
    if (op == OP_SUB) return jv_num(x - y);
    if (op == OP_MUL) return jv_num(x * y);
    if (op == OP_DIV) return jv_num(x / y);
    if (op == OP_MOD) return jv_num(js_mod(x, y));
    if (op == OP_POW) return jv_num(js_pow(x, y));
    int32_t ia = js_to_int32(x), ib = js_to_int32(y);
    if (op == OP_BAND) return jv_num((double)(ia & ib));
    if (op == OP_BOR) return jv_num((double)(ia | ib));
    if (op == OP_BXOR) return jv_num((double)(ia ^ ib));
    if (op == OP_SHL) return jv_num((double)(int32_t)((uint32_t)ia << (ib & 31)));
    if (op == OP_SAR) return jv_num((double)(ia >> (ib & 31)));
    return jv_num((double)((uint32_t)ia >> (ib & 31)));
}
/* a comparison by LT LE GT GE SEQ SNE EQ NE, any operands: its truth */
static int js_cmp_slow(js_vm *vm, uint32_t cmp, jv a, jv b) {
    if (cmp >= 4) {
        int r = cmp <= 5 ? js_strict_eq(vm, a, b) : js_loose_eq(vm, a, b);
        return cmp == 5 || cmp == 7 ? !r : r;
    }
    int l = cmp == 0 || cmp == 3 ? js_less(vm, a, b) : js_less(vm, b, a);
    return cmp == 0 || cmp == 2 ? l == 1 : l == 0;
}

/* the instructions written once each by these: x and y the operands' numbers. A fast
   path goes on to the next instruction (goto next); a slow one may throw, and leaves
   the switch for the exception's check (break). */
#define ARITH(OPC, EXPR) case OPC: { \
    jv b_ = stack[sp - 1], a_ = stack[sp - 2]; \
    if (jv_is_num(a_) && jv_is_num(b_)) { double x = jv_dbl(a_), y = jv_dbl(b_); stack[sp - 2] = jv_num(EXPR); sp--; goto next; } \
    SYNC(); \
    jv r_ = js_arith_slow(vm, OPC, a_, b_); RELOAD(); \
    if (vm->throwing) break; \
    sp--; \
    stack[sp - 1] = r_; \
    goto next; }
#define ARITH_I(OPC, BASE, EXPR) case OPC: { \
    jv a_ = stack[sp - 1]; \
    double y = (double)(int32_t)RD32(pc); \
    pc += 4; \
    if (jv_is_num(a_)) { double x = jv_dbl(a_); stack[sp - 1] = jv_num(EXPR); goto next; } \
    SYNC(); \
    jv r_ = js_arith_slow(vm, BASE, a_, jv_num(y)); RELOAD(); \
    if (vm->throwing) break; \
    stack[sp - 1] = r_; \
    goto next; }
#define REL(OPC, EXPR, CMPN) case OPC: { \
    jv b_ = stack[sp - 1], a_ = stack[sp - 2]; \
    int r_; \
    if (jv_is_num(a_) && jv_is_num(b_)) { double x = jv_dbl(a_), y = jv_dbl(b_); r_ = EXPR; } \
    else { SYNC(); r_ = js_cmp_slow(vm, CMPN, a_, b_); RELOAD(); if (vm->throwing) break; } \
    sp--; \
    stack[sp - 1] = jv_bool(r_); \
    goto next; }
/* the jump taken when the truth is WHEN; a backward one counts a step */
#define JUMP_WHEN(R, WHEN) \
    if ((R) != (WHEN)) { pc += 4; goto next; } \
    int32_t off_ = (int32_t)RD32(pc); \
    pc = (uint32_t)((int32_t)pc + 4 + off_); \
    if (off_ < 0 && ++vm->steps > vm->step_budget) { vm->throwing = 2; vm->exception = JV_UNDEF; break; } \
    goto next;
#define JREL(OPC, EXPR, CMPN, WHEN) case OPC: { \
    jv b_ = stack[sp - 1], a_ = stack[sp - 2]; \
    sp -= 2; \
    int r_; \
    if (jv_is_num(a_) && jv_is_num(b_)) { double x = jv_dbl(a_), y = jv_dbl(b_); r_ = EXPR; } \
    else { SYNC(); r_ = js_cmp_slow(vm, CMPN, a_, b_); RELOAD(); if (vm->throwing) break; } \
    JUMP_WHEN(r_, WHEN) }
/* a local's: its comparison with an integer and the jump; its arithmetic with an
   integer or another local, the result pushed */
#define JREL_LI(OPC, EXPR, CMPN, WHEN) case OPC: { \
    jv a_ = stack[base + RD16(pc)]; \
    double y = (double)(int32_t)RD32(pc + 2); \
    pc += 6; \
    int r_; \
    if (jv_is_num(a_)) { double x = jv_dbl(a_); r_ = EXPR; } \
    else { SYNC(); r_ = js_cmp_slow(vm, CMPN, a_, jv_num(y)); RELOAD(); if (vm->throwing) break; } \
    JUMP_WHEN(r_, WHEN) }
#define ARITH_LI(OPC, BASE, EXPR) case OPC: { \
    jv a_ = stack[base + RD16(pc)]; \
    double y = (double)(int32_t)RD32(pc + 2); \
    pc += 6; \
    if (jv_is_num(a_)) { double x = jv_dbl(a_); stack[sp++] = jv_num(EXPR); goto next; } \
    SYNC(); \
    jv r_ = BASE == OP_ADD ? js_add_slow(vm, a_, jv_num(y)) : js_arith_slow(vm, BASE, a_, jv_num(y)); RELOAD(); \
    if (vm->throwing) break; \
    stack[sp++] = r_; \
    goto next; }
#define ARITH_LL(OPC, BASE, EXPR) case OPC: { \
    jv a_ = stack[base + RD16(pc)], b_ = stack[base + RD16(pc + 2)]; \
    pc += 4; \
    if (jv_is_num(a_) && jv_is_num(b_)) { double x = jv_dbl(a_), y = jv_dbl(b_); stack[sp++] = jv_num(EXPR); goto next; } \
    SYNC(); \
    jv r_ = BASE == OP_ADD ? js_add_slow(vm, a_, b_) : js_arith_slow(vm, BASE, a_, b_); RELOAD(); \
    if (vm->throwing) break; \
    stack[sp++] = r_; \
    goto next; }
#define JEQ_STRICT(OPC, WHEN) case OPC: { \
    jv b_ = stack[sp - 1], a_ = stack[sp - 2]; \
    sp -= 2; \
    int r_; \
    if (jv_is_num(a_) && jv_is_num(b_)) r_ = jv_dbl(a_) == jv_dbl(b_); \
    else if ((jv_is_str(a_) && jv_is_str(b_)) || (jv_is_big(a_) && jv_is_big(b_))) { r_ = js_strict_eq(vm, a_, b_); RELOAD(); } \
    else r_ = a_ == b_; \
    JUMP_WHEN(r_, WHEN) }
#define JEQ_LOOSE(OPC, WHEN) case OPC: { \
    jv b_ = stack[sp - 1], a_ = stack[sp - 2]; \
    sp -= 2; \
    int r_; \
    if (jv_is_num(a_) && jv_is_num(b_)) r_ = jv_dbl(a_) == jv_dbl(b_); \
    else { SYNC(); r_ = js_loose_eq(vm, a_, b_); RELOAD(); if (vm->throwing) break; } \
    JUMP_WHEN(r_, WHEN) }

/* runs the frames from the top until the frame where returns: its result; or
   JV_HOLE when an exception passes it (vm->exception holds it) */
jv js_run(js_vm *vm, uint32_t entry_frame) {
    uint32_t fi = vm->nframes - 1;
    /* the code, the stack, the objects, the functions and the frames read through
       borrows, taken again after any call that may grow them (RELOAD): the safe part
       refuses a borrow used past such a call */
    const uint8_t *code = data(vm->code);
    uint64_t *stack = data(vm->stack);
    js_obj *objs = data(vm->objs);
    js_func *funcs = data(vm->funcs);
    js_frame *frames = data(vm->frames);
    uint64_t *slots = data(vm->slots);
    uint32_t pc = frames[fi].pc, base = frames[fi].base, sp = vm->sp;
    for (;;) {
    next:;
        uint8_t op = code[pc];
        pc++;
        switch (op) {
        case OP_UNDEF: stack[sp++] = JV_UNDEF; goto next;
        case OP_NULL: stack[sp++] = JV_NULL; goto next;
        case OP_TRUE: stack[sp++] = JV_TRUE; goto next;
        case OP_FALSE: stack[sp++] = JV_FALSE; goto next;
        case OP_HOLE: stack[sp++] = JV_HOLE; goto next;
        case OP_CONST: stack[sp++] = vm->consts[RD32(pc)]; pc += 4; goto next;
        case OP_INT: stack[sp++] = jv_num((double)(int32_t)RD32(pc)); pc += 4; goto next;
        case OP_STR: stack[sp++] = jv_str(RD32(pc)); pc += 4; goto next;
        case OP_DUP: stack[sp] = stack[sp - 1]; sp++; goto next;
        case OP_DUP2: stack[sp] = stack[sp - 2]; stack[sp + 1] = stack[sp - 1]; sp += 2; goto next;
        case OP_POP: sp--; goto next;
        case OP_SWAP: { jv t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; goto next; }
        case OP_ROT3: { jv c = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = stack[sp - 3]; stack[sp - 3] = c; goto next; }
        case OP_ROT4: {
            jv d = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2];
            stack[sp - 2] = stack[sp - 3];
            stack[sp - 3] = stack[sp - 4];
            stack[sp - 4] = d;
            goto next;
        }
        case OP_GET_LOC: stack[sp++] = stack[base + RD16(pc)]; pc += 2; goto next;
        case OP_PUT_LOC: stack[base + RD16(pc)] = stack[--sp]; pc += 2; goto next;
        case OP_SET_LOC: stack[base + RD16(pc)] = stack[sp - 1]; pc += 2; goto next;
        case OP_GET_LOC_CHK: {
            jv v = stack[base + RD16(pc)];
            uint32_t atom = RD32(pc + 2);
            pc += 6;
            if (v == JV_HOLE) { SYNC(); js_tdz_error(vm, atom); RELOAD(); break; }
            stack[sp++] = v;
            goto next;
        }
        case OP_PUT_LOC_CHK: case OP_SET_LOC_CHK: {
            /* an assignment to a let or a class before its declaration ran: V8's ReferenceError */
            uint32_t slot = RD16(pc), atom = RD32(pc + 2);
            pc += 6;
            if (stack[base + slot] == JV_HOLE) { SYNC(); js_tdz_error(vm, atom); RELOAD(); break; }
            stack[base + slot] = stack[sp - 1];
            if (op == OP_PUT_LOC_CHK) sp--;
            goto next;
        }
        case OP_PUT_ENV_CHK: case OP_SET_ENV_CHK: {
            uint32_t depth = code[pc], idx = RD16(pc + 1), atom = RD32(pc + 3);
            pc += 7;
            uint32_t e = frames[fi].env;
            for (uint32_t d = 0; d < depth; d++) e = objs[e].a;
            if (slots[objs[e].slots + idx] == JV_HOLE) { SYNC(); js_tdz_error(vm, atom); RELOAD(); break; }
            slots[objs[e].slots + idx] = stack[sp - 1];
            if (op == OP_PUT_ENV_CHK) sp--;
            goto next;
        }
        case OP_GET_ENV: {
            uint32_t depth = code[pc], idx = RD16(pc + 1);
            pc += 3;
            uint32_t e = frames[fi].env;
            for (uint32_t d = 0; d < depth; d++) e = objs[e].a;
            stack[sp++] = slots[objs[e].slots + idx];
            goto next;
        }
        case OP_PUT_ENV: case OP_SET_ENV: {
            uint32_t depth = code[pc], idx = RD16(pc + 1);
            pc += 3;
            uint32_t e = frames[fi].env;
            for (uint32_t d = 0; d < depth; d++) e = objs[e].a;
            slots[objs[e].slots + idx] = stack[sp - 1];
            if (op == OP_PUT_ENV) sp--;
            goto next;
        }
        case OP_GET_ENV_CHK: {
            uint32_t depth = code[pc], idx = RD16(pc + 1), atom = RD32(pc + 3);
            pc += 7;
            uint32_t e = frames[fi].env;
            for (uint32_t d = 0; d < depth; d++) e = objs[e].a;
            jv v = slots[objs[e].slots + idx];
            if (v == JV_HOLE) { SYNC(); js_tdz_error(vm, atom); RELOAD(); break; }
            stack[sp++] = v;
            goto next;
        }
        case OP_GET_GLOBAL: case OP_TYPEOF_GLOBAL: {
            uint32_t atom = RD32(pc), ic = op == OP_GET_GLOBAL ? RD32(pc + 4) : 0;
            pc += op == OP_GET_GLOBAL ? 8 : 4;
            if (ic) {
                js_ic c = vm->ics[ic];
                if (c.holder && objs[c.holder].shape == c.hshape) {
                    jv v = slots[objs[c.holder].slots + c.slot];
                    if (v != JV_HOLE && v != JV_TDZ) { stack[sp++] = v; goto next; }
                }
            }
            SYNC();
            int found = 0;
            jv v = js_global_get(vm, atom, ic, &found); RELOAD();
            if (vm->throwing) break;
            if (v == JV_TDZ) { js_tdz_error(vm, atom); RELOAD(); break; }
            if (!found && op == OP_GET_GLOBAL) { js_ref_error(vm, atom, " is not defined"); RELOAD(); break; }
            { jv rv_ = op == OP_TYPEOF_GLOBAL ? jv_str(found ? js_typeof(vm, v) : A_UNDEFINED) : v; RELOAD(); stack[sp++] = rv_; }
            break;
        }
        case OP_PUT_GLOBAL: case OP_SET_GLOBAL: {
            uint32_t atom = RD32(pc);
            pc += 8;
            jv v = stack[sp - 1];
            if (op == OP_PUT_GLOBAL) sp--;
            SYNC();
            jv cur;
            uint32_t f;
            int has_lex = js_get_own(vm, vm->global_lex, atom, &cur, &f);
            RELOAD();
            if (has_lex) {
                if (cur == JV_TDZ) { js_tdz_error(vm, atom); RELOAD(); break; }
                js_define(vm, vm->global_lex, atom, v, 0); RELOAD();
                break;
            }
            /* strict code's assignment to a name nowhere declared throws */
            if (funcs[frames[fi].func].flags & JF_STRICT) {
                int known = js_has(vm, vm->global, atom); RELOAD();
                if (!known) { js_ref_error(vm, atom, " is not defined"); RELOAD(); break; }
            }
            js_set(vm, jv_obj(vm->global), atom, v); RELOAD();
            break;
        }
        case OP_DEF_VAR: {
            uint32_t atom = RD32(pc);
            pc += 4;
            int hv = js_has(vm, vm->global, atom);
            RELOAD();
            if (!hv) { js_define(vm, vm->global, atom, JV_UNDEF, 0); RELOAD(); }
            break;
        }
        case OP_DEF_LEX: {
            uint32_t atom = RD32(pc);
            pc += 4;
            jv cur;
            uint32_t f;
            int has_lex = js_get_own(vm, vm->global_lex, atom, &cur, &f);
            RELOAD();
            if (has_lex) {
                SYNC();
                uint32_t m = js_str_concat(vm, js_str_ascii(vm, "Identifier '"), js_str_concat(vm, atom, js_str_ascii(vm, "' has already been declared"))); RELOAD();
                js_throw(vm, jv_obj(js_error_new(vm, vm->syntax_error_proto, m))); RELOAD();
                break;
            }
            js_define(vm, vm->global_lex, atom, JV_TDZ, 0); RELOAD();
            break;
        }
        case OP_DEF_FUNC: {
            uint32_t atom = RD32(pc);
            pc += 4;
            js_define(vm, vm->global, atom, stack[--sp], 0); RELOAD();
            break;
        }
        case OP_ENTER_ENV: {
            uint32_t n = RD16(pc);
            pc += 2;
            { uint32_t ne = js_env_new(vm, frames[fi].env, n); RELOAD(); frames[fi].env = ne; }
            break;
        }
        case OP_LEAVE_ENV: frames[fi].env = objs[frames[fi].env].a; goto next;
        case OP_COPY_ENV: {
            uint32_t old = frames[fi].env, n = objs[old].b;
            uint32_t e = js_env_new(vm, objs[old].a, n); RELOAD();
            for (uint32_t i = 0; i < n; i++) slots[objs[e].slots + i] = slots[objs[old].slots + i];
            frames[fi].env = e;
            break;
        }
        case OP_OBJECT: { jv rv_ = jv_obj(js_obj_new(vm, JK_PLAIN, vm->object_proto)); RELOAD(); stack[sp++] = rv_; } break;
        case OP_ARRAY: {
            uint32_t n = RD32(pc);
            pc += 4;
            uint32_t a = js_array_new(vm, n); RELOAD();
            for (uint32_t i = 0; i < n; i++) slots[objs[a].a + i] = stack[sp - n + i];
            sp -= n;
            stack[sp++] = jv_obj(a);
            break;
        }
        case OP_APPEND: js_array_push(vm, jv_idx(stack[sp - 2]), stack[sp - 1]); RELOAD(); sp--; break;
        case OP_APPEND_HOLE: js_array_push(vm, jv_idx(stack[sp - 1]), JV_HOLE); RELOAD(); break;
        case OP_SPREAD: {
            SYNC();
            jv it = js_iter_of_at(vm, stack[sp - 1], pc - 1); RELOAD();
            if (vm->throwing) break;
            uint32_t arr = jv_idx(stack[sp - 2]);
            jv v;
            while (js_iter_step(vm, jv_idx(it), &v)) js_array_push(vm, arr, v); RELOAD();
            sp--;
            break;
        }
        case OP_TO_PROPKEY: {
            SYNC();
            int kok = 1;
            uint32_t key = js_prop_key(vm, stack[sp - 1], &kok); RELOAD();
            if (!kok) break;
            stack[sp - 1] = js_key_is_sym(key) ? jv_sym(key & ~JS_SYM_KEY) : jv_str(key);
            goto next;
        }
        case OP_RUN_STATIC: {
            jv fn = stack[--sp];
            jv c = stack[sp - 1];
            objs[jv_idx(fn)].c = jv_idx(c);
            frames[fi].pc = pc;
            SYNC();
            js_call(vm, fn, c, 0, 0); RELOAD();
            break;
        }
        case OP_SET_PROTO_LIT: {
            jv v = stack[--sp];
            jv o = stack[sp - 1];
            if (jv_is_obj(v) || v == JV_NULL) { SYNC(); js_set_proto(vm, jv_idx(o), v == JV_NULL ? 0 : jv_idx(v)); RELOAD(); }
            goto next;
        }
        case OP_DEF_PROP: case OP_DEF_METHOD: {
            uint32_t atom = RD32(pc);
            pc += 4;
            jv v = stack[--sp];
            uint32_t o = jv_idx(stack[sp - 1]);
            if (jv_is_obj(v) && objs[jv_idx(v)].kind == JK_CLOSURE && (funcs[objs[jv_idx(v)].a].flags & JF_METHOD)) objs[jv_idx(v)].c = o;
            js_define(vm, o, atom, v, op == OP_DEF_METHOD ? JS_SHAPE_HIDDEN : 0); RELOAD();
            break;
        }
        case OP_DEF_ELEM: case OP_DEF_METHOD_ELEM: {
            jv v = stack[sp - 1], k = stack[sp - 2];
            uint32_t o = jv_idx(stack[sp - 3]);
            SYNC();
            int kok = 1;
            uint32_t key = js_prop_key(vm, k, &kok); RELOAD();
            if (!kok) break;
            /* a class's computed static element named prototype: V8's TypeError */
            if (key == A_PROTOTYPE && (objs[o].flags & JO_CLASS_CTOR)) {
                js_throw_msg(vm, vm->type_error_proto, "Classes may not have a static property named 'prototype'"); RELOAD();
                break;
            }
            if (jv_is_obj(v) && objs[jv_idx(v)].kind == JK_CLOSURE && (funcs[objs[jv_idx(v)].a].flags & JF_METHOD)) objs[jv_idx(v)].c = o;
            js_define(vm, o, key, v, op == OP_DEF_METHOD_ELEM ? JS_SHAPE_HIDDEN : 0); RELOAD();
            sp -= 2;
            break;
        }
        case OP_DEF_GET: case OP_DEF_SET: case OP_DEF_GET_ELEM: case OP_DEF_SET_ELEM: {
            uint32_t atom;
            jv fn = stack[sp - 1];
            if (op == OP_DEF_GET || op == OP_DEF_SET) { atom = RD32(pc); pc += 4; sp--; }
            else {
                SYNC();
                int kok = 1;
                atom = js_prop_key(vm, stack[sp - 2], &kok); RELOAD();
                if (!kok) break;
                sp -= 2;
            }
            uint32_t o = jv_idx(stack[sp - 1]);
            objs[jv_idx(fn)].c = o;
            int get = op == OP_DEF_GET || op == OP_DEF_GET_ELEM;
            uint32_t hidden = objs[o].kind == JK_PLAIN && objs[o].proto != vm->object_proto ? JS_SHAPE_HIDDEN : 0;
            js_define_accessor(vm, o, atom, get ? fn : JV_HOLE, get ? JV_HOLE : fn, hidden); RELOAD();
            break;
        }
        case OP_COPY_PROPS: {
            jv src = stack[--sp];
            uint32_t o = jv_idx(stack[sp - 1]);
            if (jv_is_obj(src) || jv_is_str(src)) {
                SYNC();
                jv so = js_to_object(vm, src); RELOAD();
                uint32_t keys = js_array_new(vm, 0); RELOAD();
                js_own_keys(vm, jv_idx(so), 1, keys); RELOAD();
                for (uint32_t i = 0; i < objs[keys].b && !vm->throwing; i++) {
                    uint32_t atom = js_atom(vm, jv_idx(js_array_get(vm, keys, i))); RELOAD();
                    jv v = js_get(vm, so, atom); RELOAD();
                    if (!vm->throwing) js_define(vm, o, atom, v, 0); RELOAD();
                }
            }
            break;
        }
        case OP_GET_PROP: case OP_GET_PROP2: {
            uint32_t atom = RD32(pc), ic = RD32(pc + 4);
            pc += 8;
            jv ov = stack[sp - 1];
            if (jv_is_obj(ov)) {
                uint32_t o = jv_idx(ov);
                js_ic c = vm->ics[ic];
                uint32_t sh = objs[o].shape;
                if (sh == c.shape) {
                    jv v = JV_HOLE;
                    if (c.holder == 0) v = slots[objs[o].slots + c.slot];
                    else if (c.holder != 0xFFFFFFFFu && objs[c.holder].shape == c.hshape) v = slots[objs[c.holder].slots + c.slot];
                    if (v != JV_HOLE) {
                        if (op == OP_GET_PROP) stack[sp - 1] = v;
                        else stack[sp++] = v;
                        goto next;
                    }
                }
                if (objs[o].kind == JK_ARRAY && atom == A_LENGTH) {
                    jv v = jv_num(objs[o].b);
                    if (op == OP_GET_PROP) stack[sp - 1] = v;
                    else stack[sp++] = v;
                    goto next;
                }
            }
            SYNC();
            /* an object pattern's first key read from nothing: V8's destructuring words */
            if (jv_is_nullish(ov) && js_destructure_at(vm, pc - 9, ov, atom)) { RELOAD(); break; }
            jv v = js_get_ic(vm, ov, atom, ic); RELOAD();
            if (vm->throwing) break;
            if (op == OP_GET_PROP) stack[sp - 1] = v;
            else stack[sp++] = v;
            break;
        }
        case OP_PUT_PROP: {
            uint32_t atom = RD32(pc), ic = RD32(pc + 4);
            pc += 8;
            jv v = stack[sp - 1], ov = stack[sp - 2];
            if (jv_is_obj(ov)) {
                uint32_t o = jv_idx(ov);
                js_ic c = vm->ics[ic];
                if (objs[o].shape == c.shape && c.shape && !(objs[o].flags & JO_FROZEN)) {
                    if (c.holder == 0) {
                        slots[objs[o].slots + c.slot] = v;
                        stack[sp - 2] = v;
                        sp--;
                        goto next;
                    }
                    if (c.holder == 0xFFFFFFFFu && (objs[o].flags & JO_EXTENSIBLE)) {
                        js_obj_room(vm, o, c.slot); RELOAD();
                        objs[o].shape = c.hshape;
                        slots[objs[o].slots + c.slot] = v;
                        stack[sp - 2] = v;
                        sp--;
                        break;
                    }
                }
            }
            SYNC();
            int r = js_set_ic(vm, ov, atom, v, ic); RELOAD();
            if (vm->throwing) break;
            if (r == 2 && (funcs[frames[fi].func].flags & JF_STRICT)) { js_set_refused(vm, ov, atom); RELOAD(); break; }
            stack[sp - 2] = v;
            sp--;
            break;
        }
        case OP_GET_ELEM: case OP_GET_ELEM2: {
            jv k = stack[sp - 1], ov = stack[sp - 2];
            jv v = JV_HOLE;
            if (jv_is_obj(ov) && jv_is_num(k)) {
                uint32_t o = jv_idx(ov);
                double d = jv_dbl(k);
                if (objs[o].kind == JK_ARRAY && d >= 0 && d < (double)objs[o].b && d == (double)(uint32_t)d) {
                    v = slots[objs[o].a + (uint32_t)d];
                    if (v != JV_HOLE) {
                        if (op == OP_GET_ELEM) { sp--; stack[sp - 1] = v; }
                        else stack[sp - 1] = v;
                        goto next;
                    }
                } else if (objs[o].kind == JK_TYPEDARRAY && d >= 0 && d < (double)objs[o].b && d == (double)(uint32_t)d) {
                    v = js_ta_get(vm, o, (uint32_t)d); RELOAD();
                    if (op == OP_GET_ELEM) { sp--; stack[sp - 1] = v; }
                    else stack[sp - 1] = v;
                    goto next;
                }
            }
            if (v == JV_HOLE) {
                SYNC();
                v = js_get_value(vm, ov, k); RELOAD();
                if (vm->throwing) break;
            }
            if (op == OP_GET_ELEM) { sp--; stack[sp - 1] = v; }
            else stack[sp - 1] = v;
            break;
        }
        case OP_PUT_ELEM: {
            jv v = stack[sp - 1], k = stack[sp - 2], ov = stack[sp - 3];
            int done = 0;
            if (jv_is_obj(ov) && jv_is_num(k)) {
                uint32_t o = jv_idx(ov);
                double d = jv_dbl(k);
                if (objs[o].kind == JK_ARRAY && (objs[o].flags & JO_ELEM_SLOW) == JO_EXTENSIBLE && d >= 0 && d <= (double)objs[o].b && d == (double)(uint32_t)d) {
                    js_array_set(vm, o, (uint32_t)d, v); RELOAD();
                    done = 1;
                } else if (objs[o].kind == JK_TYPEDARRAY && d >= 0 && d < (double)objs[o].b && d == (double)(uint32_t)d) {
                    SYNC();
                    js_ta_set(vm, o, (uint32_t)d, v); RELOAD();
                    if (vm->throwing) break;
                    done = 1;
                }
            }
            if (!done) {
                SYNC();
                int r = js_set_value(vm, ov, k, v); RELOAD();
                if (vm->throwing) break;
                if (r == 2 && (funcs[frames[fi].func].flags & JF_STRICT)) {
                    uint32_t ka = js_prop_key(vm, k, &r); RELOAD();
                    if (vm->throwing) break;
                    js_set_refused(vm, ov, ka); RELOAD();
                    break;
                }
            }
            sp -= 2;
            stack[sp - 1] = v;
            break;
        }
        case OP_DEL_PROP: case OP_DEL_ELEM: {
            uint32_t atom = 0;
            if (op == OP_DEL_PROP) { atom = RD32(pc); pc += 4; }
            else {
                SYNC();
                int kok = 1;
                atom = js_prop_key(vm, stack[--sp], &kok); RELOAD();
                if (!kok) break;
            }
            jv ov = stack[sp - 1];
            int dr = jv_is_obj(ov) ? js_delete(vm, jv_idx(ov), atom) : 1;
            RELOAD();
            if (vm->throwing) break;
            if (!dr && (funcs[frames[fi].func].flags & JF_STRICT)) { js_delete_refused(vm, ov, atom); RELOAD(); break; }
            stack[sp - 1] = jv_bool(dr);
            break;
        }
        case OP_IN: {
            jv ov = stack[--sp], k = stack[sp - 1];
            SYNC();
            if (!jv_is_obj(ov)) { js_in_error(vm, k, ov); RELOAD(); break; }
            int kok = 1;
            uint32_t key = js_prop_key(vm, k, &kok); RELOAD();
            if (!kok) break;
            uint32_t idx = 0;
            int found;
            int isarr = objs[jv_idx(ov)].kind == JK_ARRAY;
            int isidx = isarr && js_atom_index(vm, key, &idx);
            RELOAD();
            if (isidx && idx < objs[jv_idx(ov)].b && slots[objs[jv_idx(ov)].a + idx] != JV_HOLE) found = 1;
            else found = js_has(vm, jv_idx(ov), key);
            RELOAD();
            stack[sp - 1] = jv_bool(found);
            break;
        }
        case OP_INSTANCEOF: {
            jv c = stack[--sp];
            SYNC();
            int r = js_instance_of(vm, stack[sp - 1], c); RELOAD();
            stack[sp - 1] = jv_bool(r);
            break;
        }
        case OP_GET_SUPER: case OP_GET_SUPER_ELEM: {
            uint32_t atom = 0;
            SYNC();
            if (op == OP_GET_SUPER) { atom = RD32(pc); pc += 4; }
            else {
                int kok = 1;
                atom = js_prop_key(vm, stack[--sp], &kok); RELOAD();
                if (!kok) break;
            }
            uint32_t home = objs[frames[fi].callee].c;
            uint32_t p = home ? objs[home].proto : 0;
            uint32_t h = 0, e = 0;
            jv v = p ? js_obj_get(vm, p, atom, frames[fi].thisv, &h, &e) : JV_UNDEF; RELOAD();
            if (vm->throwing) break;
            stack[sp++] = v;
            break;
        }
        case OP_CLOSURE: {
            uint32_t func = RD32(pc);
            pc += 4;
            jv thisv = (funcs[func].flags & JF_ARROW) ? frames[fi].thisv : JV_UNDEF;
            uint32_t c = js_closure_new(vm, func, frames[fi].env, thisv); RELOAD();
            if (funcs[func].flags & JF_ARROW) objs[c].c = objs[frames[fi].callee].c;   /* super, through the arrow */
            stack[sp++] = jv_obj(c);
            break;
        }
        case OP_CALL: case OP_CALL0: case OP_NEW: case OP_CALL_ARR: case OP_NEW_ARR: case OP_SUPER_CALL: case OP_SUPER_CALL_ARR: {
            if (op == OP_CALL || op == OP_CALL0) {
                /* a closure's call, the common case: its frame made here, no record copied
                   (CALL0: no this on the stack, the callee's plain-call this) */
                uint32_t argc0 = RD16(pc);
                uint32_t nb = sp - argc0;
                jv cv = stack[nb - 1];
                if (jv_is_obj(cv) && (objs[jv_idx(cv)].flags & JO_FASTCALL)) {
                    uint32_t o = jv_idx(cv);
                    uint32_t func = objs[o].a;
                    uint32_t fl = funcs[func].flags, np = funcs[func].nparams, ns = funcs[func].nslots;
                    uint32_t n = vm->nframes;
                    jv tv0 = op == OP_CALL ? stack[nb - 2] : JV_UNDEF;
                    /* a sloppy function's this a primitive: boxed, on the general path */
                    int boxed = !(fl & (JF_STRICT | JF_ARROW)) && !jv_is_obj(tv0) && !jv_is_nullish(tv0) && tv0 != JV_HOLE;
                    if (!boxed && n + 1 < len(vm->frames) && sp + ns + JS_STACK_MARGIN < len(vm->stack)) {
                        frames[fi].pc = pc + 2;
                        js_frame fr;
                        fr.gen = 0;
                        fr.func = func;
                        fr.pc = funcs[func].code;
                        fr.base = nb;
                        fr.env = objs[o].b;
                        fr.callee = o;
                        fr.argc = argc0;
                        fr.flags = 0;
                        fr.newt = JV_UNDEF;
                        fr.args = 0;
                        fr.handlers = vm->nhandlers;
                        if (fl & JF_ARROW) fr.thisv = objs[o].x;
                        else if (!(fl & JF_STRICT) && (jv_is_nullish(tv0) || tv0 == JV_HOLE)) fr.thisv = jv_obj(vm->global);
                        else fr.thisv = tv0;
                        fr.sp0 = op == OP_CALL ? nb - 2 : nb - 1;
                        uint32_t top = nb + (argc0 > np ? np : argc0);
                        while (top < nb + ns) stack[top++] = JV_UNDEF;
                        vm->nframes = n + 1;
                        frames[n] = fr;
                        fi = n;
                        pc = fr.pc;
                        base = nb;
                        sp = top;
                        goto next;
                    }
                }
                if (op == OP_CALL0) {
                    /* any other callee: the stack made a call's, this undefined below it */
                    for (uint32_t i = sp; i >= nb; i--) stack[i] = stack[i - 1];
                    stack[nb - 1] = JV_UNDEF;
                    sp++;
                    op = OP_CALL;
                }
            }
            uint32_t argc;
            int construct = op == OP_NEW || op == OP_NEW_ARR, super = op == OP_SUPER_CALL || op == OP_SUPER_CALL_ARR;
            if (op == OP_CALL || op == OP_NEW || op == OP_SUPER_CALL) { argc = RD16(pc); pc += 2; }
            else {
                /* the arguments an array: spread onto the stack */
                uint32_t arr = jv_idx(stack[--sp]);
                argc = objs[arr].b;
                {
                    SYNC();
                    int roomy = js_room(vm, sp + argc + JS_STACK_MARGIN, vm->nframes + 2); RELOAD();
                    if (!roomy) { js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded"); RELOAD(); break; }
                }
                for (uint32_t i = 0; i < argc; i++) {
                    jv ev = slots[objs[arr].a + i];
                    stack[sp++] = ev == JV_HOLE ? JV_UNDEF : ev;
                }
            }
            uint32_t sp0 = sp - argc - (construct || super ? 1 : 2);
            jv callee, thisv, newt = JV_UNDEF;
            if (super) {
                /* super(...): the parent class's constructor, constructing this class's new.target */
                uint32_t me = frames[fi].callee;
                callee = objs[me].proto ? jv_obj(objs[me].proto) : JV_NULL;
                for (uint32_t i = 0; i < argc; i++) stack[sp - i] = stack[sp - i - 1];
                stack[sp - argc] = callee;
                sp++;
                sp0 = sp - argc - 1;
                newt = frames[fi].newt;
                thisv = JV_HOLE;
                if (frames[fi].thisv != JV_HOLE) { SYNC(); js_throw_msg(vm, vm->reference_error_proto, "Super constructor may only be called once"); RELOAD(); break; }
            } else if (construct) { callee = stack[sp0]; thisv = JV_HOLE; newt = callee; }
            else { callee = stack[sp0 + 1]; thisv = stack[sp0]; }
            /* (the frame's pc past the operands: an error's site is found by it) */
            frames[fi].pc = pc;
            SYNC();
            if (!CALLABLE(callee)) { js_call_fail(vm, op, fi, callee); RELOAD(); break; }
            uint32_t o = jv_idx(callee);
            if (objs[o].kind == JK_CLOSURE) {
                uint32_t flags = 0;
                if (construct || super) {
                    js_func f = funcs[objs[o].a];
                    if (f.flags & (JF_ARROW | JF_METHOD | JF_GENERATOR | JF_ASYNC)) { js_call_fail(vm, op, fi, callee); RELOAD(); break; }
                    flags = JFR_CONSTRUCT | (super ? JFR_NATIVE_ENTRY : 0);
                    if (!(objs[o].flags & JO_DERIVED)) {
                        jv p = js_ctor_proto(vm, newt); RELOAD();
                        if (vm->throwing) break;
                        thisv = jv_obj(js_obj_new(vm, JK_PLAIN, jv_is_obj(p) ? jv_idx(p) : vm->object_proto)); RELOAD();
                    }
                } else if (objs[o].flags & JO_CLASS_CTOR) { js_class_call_error(vm, o); RELOAD(); break; }
                int entered = js_enter(vm, o, thisv, argc, newt, flags, sp0);
                RELOAD();
                if (!entered) break;
                if (super) {
                    /* the parent's construction run to its end here, its object this class's this */
                    vm->depth++;
                    jv r = js_run(vm, vm->nframes - 1); RELOAD();
                    vm->depth--;
                    sp = sp0;
                    if (vm->throwing) break;
                    frames[fi].thisv = r;
                    stack[sp++] = r;
                    break;
                }
                fi = vm->nframes - 1;
                pc = frames[fi].pc;
                base = frames[fi].base;
                sp = vm->sp;
                break;
            }
            /* (a native or bound function constructed that is no constructor: js_call_other's error) */
            jv r = js_call_other(vm, callee, thisv, sp0 + (construct || super ? 1 : 2), argc, newt); RELOAD();
            sp = sp0;
            if (vm->throwing) break;
            if (super) frames[fi].thisv = r;
            stack[sp++] = r;
            break;
        }
        case OP_RETURN: case OP_RETURN_UNDEF: case OP_RETURN_LOC: {
            jv r = JV_UNDEF;
            if (op == OP_RETURN) r = stack[sp - 1];
            else if (op == OP_RETURN_LOC) r = stack[base + RD16(pc)];
            uint32_t rfl = frames[fi].flags;
            uint32_t gob = frames[fi].gen;
            if (gob) {
                if (objs[gob].kind == JK_ASYNC) {
                    uint32_t pr = objs[gob].c;
                    SYNC();
                    js_promise_resolve_with(vm, pr, r); RELOAD();
                    r = jv_obj(pr);
                } else if (objs[gob].kind == JK_ASYNCGEN) {
                    /* done: its request answered {r, true}, the requests left after it */
                    objs[gob].a = JG_DONE;
                    objs[gob].b = 0;
                    SYNC();
                    js_ag_complete(vm, gob, 0, r, 1); RELOAD();
                    js_ag_drain(vm, gob); RELOAD();
                    r = JV_UNDEF;
                }
                objs[gob].a = JG_DONE;
                objs[gob].b = 0;
            }
            if (rfl) {
                if (rfl & JFR_CONSTRUCT) {
                    if (!jv_is_obj(r)) {
                        jv tv = frames[fi].thisv;
                        if (tv == JV_HOLE) { SYNC(); js_throw_msg(vm, vm->reference_error_proto, "Must call super constructor in derived class before accessing 'this' or returning from derived constructor"); RELOAD(); break; }
                        r = tv;
                    }
                }
                if (rfl & JFR_NATIVE_ENTRY) {
                    vm->nhandlers = frames[fi].handlers;
                    vm->nframes = fi;
                    vm->sp = frames[fi].sp0;
                    return r;
                }
            }
            vm->nhandlers = frames[fi].handlers;
            vm->nframes = fi;
            sp = frames[fi].sp0;
            stack[sp++] = r;
            fi--;
            pc = frames[fi].pc;
            base = frames[fi].base;
            goto next;
        }
        case OP_THIS: {
            jv t = frames[fi].thisv;
            if (t == JV_HOLE) { SYNC(); js_throw_msg(vm, vm->reference_error_proto, "Must call super constructor in derived class before accessing 'this' or returning from derived constructor"); RELOAD(); break; }
            stack[sp++] = t;
            goto next;
        }
        case OP_NEW_TARGET: stack[sp++] = frames[fi].newt; goto next;
        case OP_CALLEE: stack[sp++] = jv_obj(frames[fi].callee); goto next;
        case OP_ARGUMENTS: {
            uint32_t a = js_obj_new(vm, JK_ARGUMENTS, vm->object_proto); RELOAD();
            { uint32_t aa = frames[fi].args; if (!aa) { aa = js_array_new(vm, 0); RELOAD(); } objs[a].a = aa; }
            /* its length an ordinary property of its own (writable, configurable), as a sloppy function's callee; an
               unmapped one's (a strict function's, or one whose parameters are not plain names) is %ThrowTypeError%'s
               accessor, neither enumerable nor configurable */
            js_define(vm, a, A_LENGTH, jv_num(frames[fi].argc), JS_SHAPE_HIDDEN); RELOAD();
            /* its Symbol.iterator Array.prototype.values itself, an own property */
            js_define(vm, a, JS_KEY_ITERATOR, jv_obj(vm->array_values), JS_SHAPE_HIDDEN); RELOAD();
            if (funcs[frames[fi].func].flags & (JF_STRICT | JF_NONSIMPLE)) {
                js_define_accessor(vm, a, A_CALLEE, jv_obj(vm->throw_te), jv_obj(vm->throw_te), JS_SHAPE_HIDDEN | JS_SHAPE_FIXED); RELOAD();
            } else { js_define(vm, a, A_CALLEE, jv_obj(frames[fi].callee), JS_SHAPE_HIDDEN); RELOAD(); }
            stack[sp++] = jv_obj(a);
            break;
        }
        case OP_REST: {
            uint32_t from = RD16(pc);
            pc += 2;
            uint32_t args = frames[fi].args, r = js_array_new(vm, 0); RELOAD();
            for (uint32_t i = from; args && i < objs[args].b; i++) js_array_push(vm, r, js_array_get(vm, args, i)); RELOAD();
            stack[sp++] = jv_obj(r);
            break;
        }
        case OP_ADD: {
            jv b = stack[sp - 1], a = stack[sp - 2];
            if (jv_is_num(a) && jv_is_num(b)) { stack[sp - 2] = jv_num(jv_dbl(a) + jv_dbl(b)); sp--; goto next; }
            if (jv_is_str(a) && jv_is_str(b)) { { jv rv_ = jv_str(js_str_concat(vm, jv_idx(a), jv_idx(b))); RELOAD(); stack[sp - 2] = rv_; } sp--; goto next; }
            SYNC();
            jv r = js_add_slow(vm, a, b); RELOAD();
            if (vm->throwing) break;
            sp--;
            stack[sp - 1] = r;
            break;
        }
        ARITH(OP_SUB, x - y)
        ARITH(OP_MUL, x * y)
        ARITH(OP_DIV, x / y)
        ARITH(OP_MOD, js_mod(x, y))
        ARITH(OP_POW, js_pow(x, y))
        case OP_ADD_I: {
            jv a = stack[sp - 1];
            double y = (double)(int32_t)RD32(pc);
            pc += 4;
            if (jv_is_num(a)) { stack[sp - 1] = jv_num(jv_dbl(a) + y); goto next; }
            SYNC();
            jv r = js_add_slow(vm, a, jv_num(y)); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = r;
            goto next;
        }
        ARITH_I(OP_SUB_I, OP_SUB, x - y)
        ARITH_I(OP_MUL_I, OP_MUL, x * y)
        ARITH_I(OP_MOD_I, OP_MOD, js_mod(x, y))
        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SAR: case OP_SHR: {
            jv b = stack[sp - 1], a = stack[sp - 2];
            if (!jv_is_num(a) || !jv_is_num(b)) {
                /* objects, strings, BigInts: the general path */
                SYNC();
                jv r = js_arith_slow(vm, op, a, b); RELOAD();
                if (vm->throwing) break;
                sp--;
                stack[sp - 1] = r;
                break;
            }
            double x = jv_dbl(a), y = jv_dbl(b);
            int32_t ia = js_to_int32(x), ib = js_to_int32(y);
            double r;
            if (op == OP_BAND) r = (double)(ia & ib);
            else if (op == OP_BOR) r = (double)(ia | ib);
            else if (op == OP_BXOR) r = (double)(ia ^ ib);
            else if (op == OP_SHL) r = (double)(int32_t)((uint32_t)ia << (ib & 31));
            else if (op == OP_SAR) r = (double)(ia >> (ib & 31));
            else r = (double)((uint32_t)ia >> (ib & 31));
            sp--;
            stack[sp - 1] = jv_num(r);
            break;
        }
        case OP_NEG: case OP_PLUS: case OP_BNOT: case OP_TONUM: case OP_INC: case OP_DEC: {
            jv a = stack[sp - 1];
            double x;
            if (jv_is_num(a)) x = jv_dbl(a);
            else {
                SYNC();
                /* ToNumeric: a BigInt stays one (unary + alone refuses it) */
                jv pa = jv_is_obj(a) ? js_to_primitive(vm, a, 0) : a; RELOAD();
                if (vm->throwing) break;
                if (jv_is_big(pa) && op != OP_PLUS) {
                    jv r = op == OP_NEG ? js_big_neg(vm, pa) : op == OP_BNOT ? js_big_not(vm, pa) : op == OP_INC ? js_big_inc(vm, pa, 1) :
                           op == OP_DEC ? js_big_inc(vm, pa, -1) : pa;
                    RELOAD();
                    if (vm->throwing) break;
                    stack[sp - 1] = r;
                    break;
                }
                x = js_to_number(vm, pa); RELOAD();
                if (vm->throwing) break;
            }
            double r = op == OP_NEG ? -x : op == OP_BNOT ? (double)(~js_to_int32(x)) : op == OP_INC ? x + 1 : op == OP_DEC ? x - 1 : x;
            stack[sp - 1] = jv_num(r);
            break;
        }
        case OP_NOT: { jv rv_ = jv_bool(!js_truthy(vm, stack[sp - 1])); RELOAD(); stack[sp - 1] = rv_; } break;
        case OP_TYPEOF: { jv rv_ = jv_str(js_typeof(vm, stack[sp - 1])); RELOAD(); stack[sp - 1] = rv_; } break;
        case OP_TOSTR: {
            jv a = stack[sp - 1];
            if (!jv_is_str(a)) {
                SYNC();
                uint32_t s = js_to_string(vm, a); RELOAD();
                if (!s) break;
                stack[sp - 1] = jv_str(s);
            }
            break;
        }
        case OP_EQ: case OP_NE: case OP_SEQ: case OP_SNE: {
            jv b = stack[sp - 1], a = stack[sp - 2];
            int r;
            if (op == OP_SEQ || op == OP_SNE) { r = js_strict_eq(vm, a, b); RELOAD(); }
            else { SYNC(); r = js_loose_eq(vm, a, b); RELOAD(); if (vm->throwing) break; }
            if (op == OP_NE || op == OP_SNE) r = !r;
            sp--;
            stack[sp - 1] = jv_bool(r);
            break;
        }
        REL(OP_LT, x < y, 0)
        REL(OP_LE, x <= y, 1)
        REL(OP_GT, x > y, 2)
        REL(OP_GE, x >= y, 3)
        case OP_JMP: {
            int32_t off = (int32_t)RD32(pc);
            pc = (uint32_t)((int32_t)pc + 4 + off);
            if (off < 0 && ++vm->steps > vm->step_budget) { vm->throwing = 2; vm->exception = JV_UNDEF; break; }
            goto next;
        }
        case OP_INC_LOC: case OP_DEC_LOC: {
            uint32_t slot = RD16(pc);
            pc += 2;
            jv v = stack[base + slot];
            double x;
            if (jv_is_num(v)) x = jv_dbl(v);
            else if (v == JV_HOLE) { SYNC(); js_throw_msg(vm, vm->reference_error_proto, "Cannot access a variable before its declaration ran"); RELOAD(); break; }
            else {
                SYNC();
                jv pv = jv_is_obj(v) ? js_to_primitive(vm, v, 0) : v; RELOAD();
                if (vm->throwing) break;
                if (jv_is_big(pv)) {
                    jv r = js_big_inc(vm, pv, op == OP_INC_LOC ? 1 : -1); RELOAD();
                    if (vm->throwing) break;
                    stack[base + slot] = r;
                    goto next;
                }
                x = js_to_number(vm, pv); RELOAD();
                if (vm->throwing) break;
            }
            stack[base + slot] = jv_num(op == OP_INC_LOC ? x + 1 : x - 1);
            goto next;
        }
        JREL(OP_JLT, x < y, 0, 1)
        JREL(OP_JLE, x <= y, 1, 1)
        JREL(OP_JGT, x > y, 2, 1)
        JREL(OP_JGE, x >= y, 3, 1)
        JREL(OP_JNLT, x < y, 0, 0)
        JREL(OP_JNLE, x <= y, 1, 0)
        JREL(OP_JNGT, x > y, 2, 0)
        JREL(OP_JNGE, x >= y, 3, 0)
        JEQ_STRICT(OP_JSEQ, 1)
        JEQ_STRICT(OP_JSNE, 0)
        JEQ_LOOSE(OP_JEQ, 1)
        JEQ_LOOSE(OP_JNE, 0)
        JREL_LI(OP_JLT_LI, x < y, 0, 1)
        JREL_LI(OP_JLE_LI, x <= y, 1, 1)
        JREL_LI(OP_JGT_LI, x > y, 2, 1)
        JREL_LI(OP_JGE_LI, x >= y, 3, 1)
        JREL_LI(OP_JNLT_LI, x < y, 0, 0)
        JREL_LI(OP_JNLE_LI, x <= y, 1, 0)
        JREL_LI(OP_JNGT_LI, x > y, 2, 0)
        JREL_LI(OP_JNGE_LI, x >= y, 3, 0)
        ARITH_LI(OP_ADD_LI, OP_ADD, x + y)
        ARITH_LI(OP_SUB_LI, OP_SUB, x - y)
        ARITH_LI(OP_MUL_LI, OP_MUL, x * y)
        ARITH_LI(OP_MOD_LI, OP_MOD, js_mod(x, y))
        ARITH_LL(OP_ADD_LL, OP_ADD, x + y)
        ARITH_LL(OP_SUB_LL, OP_SUB, x - y)
        ARITH_LL(OP_MUL_LL, OP_MUL, x * y)
        case OP_JF: case OP_JT: {
            jv c = stack[--sp];
            int t;
            if (c == JV_TRUE) t = 1;
            else if (c == JV_FALSE) t = 0;
            else if (jv_is_num(c)) { double d = jv_dbl(c); t = d == d && d != 0; }
            else if (jv_is_str(c)) t = vm->strs[jv_idx(c)].len > 0;
            else t = jv_is_obj(c);
            if (t == (op == OP_JT)) {
                int32_t off = (int32_t)RD32(pc);
                pc = (uint32_t)((int32_t)pc + 4 + off);
                if (off < 0 && ++vm->steps > vm->step_budget) { vm->throwing = 2; vm->exception = JV_UNDEF; break; }
            } else pc += 4;
            goto next;
        }
        case OP_JF_KEEP: case OP_JT_KEEP: {
            int t = js_truthy(vm, stack[sp - 1]); RELOAD();
            if (t == (op == OP_JT_KEEP)) pc = pc + 4 + RD32(pc);
            else { pc += 4; sp--; }
            break;
        }
        case OP_JNN_KEEP: {
            if (!jv_is_nullish(stack[sp - 1])) pc = pc + 4 + RD32(pc);
            else { pc += 4; sp--; }
            goto next;
        }
        case OP_JNULLISH: {
            if (jv_is_nullish(stack[sp - 1])) { stack[sp - 1] = JV_UNDEF; pc = pc + 4 + RD32(pc); }
            else pc += 4;
            goto next;
        }
        case OP_TRY: {
            if (vm->nhandlers >= len(vm->handlers)) { SYNC(); js_throw_msg(vm, vm->range_error_proto, "too many nested try blocks"); RELOAD(); break; }
            js_handler h = { fi, pc + 4 + RD32(pc), sp, frames[fi].env };
            vm->handlers[vm->nhandlers++] = h;
            pc += 4;
            break;
        }
        case OP_END_TRY: vm->nhandlers--; goto next;
        case OP_THROW: SYNC(); js_throw(vm, stack[--sp]); RELOAD(); break;
        case OP_THROW_REF: { uint32_t atom = RD32(pc); pc += 4; SYNC(); js_ref_error(vm, atom, " is not defined"); RELOAD(); break; }
        case OP_THROW_CONST: {
            /* V8's words name no variable */
            pc += 4;
            SYNC();
            js_throw_msg(vm, vm->type_error_proto, "Assignment to constant variable."); RELOAD();
            break;
        }
        case OP_ITER_IN: { SYNC(); { jv rv_ = jv_obj(js_iter_keys(vm, stack[sp - 1])); RELOAD(); stack[sp - 1] = rv_; } break; }
        case OP_ITER_OF: {
            SYNC();
            jv it = js_iter_of_at(vm, stack[sp - 1], pc - 1); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = it;
            break;
        }
        case OP_ITER_NEXT: {
            jv v;
            SYNC();
            int more = js_iter_step(vm, jv_idx(stack[sp - 1]), &v);
            RELOAD();
            if (more) { stack[sp++] = v; pc += 4; }
            else if (!vm->throwing) { sp--; pc = pc + 4 + RD32(pc); }
            break;
        }
        case OP_CLASS: {
            uint32_t func = RD32(pc);
            pc += 8;
            jv parent = stack[sp - 1];
            SYNC();
            uint32_t proto_parent = vm->object_proto, ctor_parent = vm->function_proto;
            int derived = parent != JV_HOLE;
            if (derived) {
                int ok = js_class_heritage(vm, parent, &proto_parent, &ctor_parent); RELOAD();
                if (!ok) break;
            }
            uint32_t proto = js_obj_new(vm, JK_PLAIN, proto_parent); RELOAD();
            uint32_t c = js_closure_new(vm, func, frames[fi].env, JV_UNDEF); RELOAD();
            objs[c].proto = ctor_parent;
            { uint32_t cs = js_shape_of(vm, ctor_parent); RELOAD(); objs[c].shape = cs; }
            objs[c].flags = (uint16_t)((objs[c].flags | JO_CLASS_CTOR | (derived ? JO_DERIVED : 0)) & ~JO_FASTCALL);
            objs[c].c = proto;
            js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_PROTO); RELOAD();
            js_define(vm, proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN); RELOAD();
            stack[sp - 1] = jv_obj(c);
            stack[sp++] = jv_obj(proto);
            break;
        }
        case OP_SET_FIELDS: {
            jv fn = stack[--sp];
            uint32_t c = jv_idx(stack[sp - 1]);
            objs[c].x = fn;
            objs[jv_idx(fn)].c = objs[c].c;
            break;
        }
        case OP_INIT_FIELDS: {
            jv fn = objs[frames[fi].callee].x;
            if (CALLABLE(fn)) {
                frames[fi].pc = pc;
                SYNC();
                js_call(vm, fn, frames[fi].thisv, 0, 0); RELOAD();
            }
            break;
        }
        case OP_INIT_LEX: {
            uint32_t atom = RD32(pc);
            pc += 4;
            js_define(vm, vm->global_lex, atom, stack[--sp], 0); RELOAD();
            break;
        }
        case OP_JNULLISH2: {
            if (jv_is_nullish(stack[sp - 1])) { sp--; stack[sp - 1] = JV_UNDEF; pc = pc + 4 + RD32(pc); }
            else pc += 4;
            break;
        }
        case OP_GEN_START: case OP_YIELD: case OP_AWAIT: {
            /* the frame suspended into its object; what the caller (or the resumer) gets */
            jv r = JV_UNDEF;
            SYNC();
            if (op == OP_GEN_START) {
                jv pv = js_get(vm, jv_obj(frames[fi].callee), A_PROTOTYPE); RELOAD();
                if (vm->throwing) break;
                int ag = (funcs[frames[fi].func].flags & JF_ASYNC) != 0;
                uint32_t dp = ag ? vm->async_generator_proto : vm->generator_proto;
                uint32_t g = js_obj_new(vm, ag ? JK_ASYNCGEN : JK_GENERATOR, jv_is_obj(pv) ? jv_idx(pv) : dp); RELOAD();
                objs[g].x = jv_obj(frames[fi].callee);
                frames[fi].gen = g;
                js_gen_save(vm, fi, pc, sp); RELOAD();
                objs[g].a = JG_START;
                r = jv_obj(g);
            } else if (op == OP_YIELD) {
                r = stack[--sp];
                SYNC();
                js_gen_save(vm, fi, pc, sp); RELOAD();
                objs[frames[fi].gen].a = JG_YIELD;
            } else {
                jv v = stack[--sp];
                SYNC();
                uint32_t g = frames[fi].gen;
                uint32_t p2 = js_promise_of(vm, v); RELOAD();
                if (vm->throwing) break;
                js_gen_save(vm, fi, pc, sp); RELOAD();
                int ag = objs[g].kind == JK_ASYNCGEN;
                objs[g].a = ag ? JG_AWAIT : JG_YIELD;
                js_promise_react(vm, p2, JV_UNDEF, JV_UNDEF, jv_obj(g), 1); RELOAD();
                r = ag ? JV_UNDEF : jv_obj(objs[g].c);
            }
            vm->nhandlers = frames[fi].handlers;
            vm->nframes = fi;
            sp = frames[fi].sp0;
            if (frames[fi].flags & JFR_NATIVE_ENTRY) { vm->sp = sp; return r; }
            stack[sp++] = r;
            fi--;
            pc = frames[fi].pc;
            base = frames[fi].base;
            goto next;
        }
        case OP_ASYNC_START: {
            SYNC();
            uint32_t g = js_obj_new(vm, JK_ASYNC, 0); RELOAD();
            uint32_t pr = js_promise_new(vm); RELOAD();
            objs[g].c = pr;
            objs[g].a = JG_RUNNING;
            objs[g].x = jv_obj(frames[fi].callee);
            frames[fi].gen = g;
            goto next;
        }
        case OP_GEN_RESUME: {
            /* how the run was resumed: with a value (mode 0), a throw (1), or a return (2):
               the stack cut to its statement's (the slots, then u16 loops' iterators), the
               value on it, and the return path at i32 */
            uint32_t at = pc;
            int32_t off = (int32_t)RD32(at);
            uint32_t nit = RD16(at + 4);
            pc = at + 6;
            double m = jv_dbl(stack[--sp]);
            if (m == 0) goto next;
            if (m == 1) { jv e = stack[--sp]; SYNC(); js_throw(vm, e); RELOAD(); break; }
            jv v = stack[--sp];
            sp = base + funcs[frames[fi].func].nslots + nit;
            stack[sp++] = v;
            pc = (uint32_t)((int32_t)(at + 4) + off);
            goto next;
        }
        case OP_YIELD_STAR: {
            /* i32 done, i32 return, u16 the loops' iterators (GEN_RESUME's) */
            uint32_t at = pc;
            int32_t done_off = (int32_t)RD32(at), ret_off = (int32_t)RD32(at + 4);
            uint32_t nit = RD16(at + 8);
            pc = at + 10;
            double m = jv_dbl(stack[--sp]);
            jv recv = stack[--sp], it = stack[sp - 1];
            jv out = JV_UNDEF;
            int fin = 0, asret = 0;
            SYNC();
            js_yield_star(vm, it, recv, (int)m, &out, &fin, &asret); RELOAD();
            if (vm->throwing) break;
            if (!fin) { stack[sp++] = out; goto next; }
            if (asret) {
                sp = base + funcs[frames[fi].func].nslots + nit;
                stack[sp++] = out;
                pc = (uint32_t)((int32_t)(at + 8) + ret_off);
                goto next;
            }
            stack[sp - 1] = out;
            pc = (uint32_t)((int32_t)(at + 4) + done_off);
            goto next;
        }
        case OP_GET_ITER: {
            SYNC();
            jv it = js_iterator_object(vm, stack[sp - 1], pc - 1); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = it;
            goto next;
        }
        case OP_ITER_CLOSE: {
            /* u8: 1 on an exception's way out, where an error the closing throws is lost */
            uint32_t quiet = code[pc];
            pc++;
            jv it = stack[--sp];
            if (jv_is_obj(it) && objs[jv_idx(it)].kind == JK_ITER && (objs[jv_idx(it)].c == JI_GEN || objs[jv_idx(it)].c == JI_PROTOCOL)) {
                SYNC();
                js_iter_close(vm, jv_idx(it)); RELOAD();
                if (quiet && vm->throwing == 1) { vm->throwing = 0; vm->exception = JV_UNDEF; }
            }
            if (vm->throwing) break;
            goto next;
        }
        case OP_REGEXP: {
            uint32_t re = RD32(pc);
            pc += 4;
            SYNC();
            jv r = js_re_object(vm, re); RELOAD();
            stack[sp++] = r;
            goto next;
        }
        case OP_DESTRUCT_ARR: {
            uint32_t n = RD16(pc);
            pc += 2;
            jv v = stack[sp - 1];
            if (jv_is_obj(v) && objs[jv_idx(v)].kind == JK_ARRAY) goto next;
            SYNC();
            jv r = js_iter_take(vm, v, n, pc - 3); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = r;
            goto next;
        }
        case OP_AYIELD: {
            /* an async generator's yield, its value awaited before: the first request
               answered {v, false}; the next request's value and mode at once when one
               waits, else the frame suspended until a request resumes it */
            jv v = stack[--sp];
            uint32_t g = frames[fi].gen;
            SYNC();
            js_ag_complete(vm, g, 0, v, 0); RELOAD();
            if (vm->throwing) break;
            uint32_t q = objs[g].c;
            if (q && objs[q].b >= 3) {
                stack[sp++] = slots[objs[q].a + 1];
                stack[sp++] = slots[objs[q].a];
                goto next;
            }
            js_gen_save(vm, fi, pc, sp); RELOAD();
            objs[g].a = JG_YIELD;
            vm->nhandlers = frames[fi].handlers;
            vm->nframes = fi;
            sp = frames[fi].sp0;
            if (frames[fi].flags & JFR_NATIVE_ENTRY) { vm->sp = sp; return JV_UNDEF; }
            stack[sp++] = JV_UNDEF;
            fi--;
            pc = frames[fi].pc;
            base = frames[fi].base;
            goto next;
        }
        case OP_ITER_ASYNC: {
            SYNC();
            jv r = js_aiter_of_at(vm, stack[sp - 1], pc - 1); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = r;
            goto next;
        }
        case OP_ITER_ANEXT: {
            jv out = JV_UNDEF;
            SYNC();
            js_aiter_call(vm, jv_idx(stack[sp - 1]), 3, JV_UNDEF, &out); RELOAD();
            if (vm->throwing) break;
            stack[sp++] = out;
            goto next;
        }
        case OP_ITER_ARESULT: {
            /* the step's result awaited: done ends the loop; its value, the record no longer mid-step */
            uint32_t at = pc;
            int32_t off = (int32_t)RD32(at);
            pc = at + 4;
            jv r = stack[sp - 1];
            SYNC();
            if (!jv_is_obj(r)) { js_result_error(vm, r); RELOAD(); break; }
            jv d = js_get(vm, r, A_DONE); RELOAD();
            if (vm->throwing) break;
            int dn = js_truthy(vm, d); RELOAD();
            if (dn) { sp -= 2; pc = (uint32_t)((int32_t)(at + 4) + off); goto next; }
            jv v = js_get(vm, r, A_VALUE); RELOAD();
            if (vm->throwing) break;
            objs[jv_idx(stack[sp - 2])].b = 0;
            stack[sp - 1] = v;
            goto next;
        }
        case OP_ITER_ACLOSE: {
            /* AsyncIteratorClose: return() called, its result left to await; nothing to
               await when the record is mid-step (an error of its own), already closed,
               has no return, or (quiet) its return threw */
            uint32_t quiet = code[pc];
            uint32_t at = pc + 1;
            int32_t off = (int32_t)RD32(at);
            pc = at + 4;
            uint32_t ro = jv_idx(stack[--sp]);
            if (objs[ro].b) { pc = (uint32_t)((int32_t)(at + 4) + off); goto next; }
            objs[ro].b = 1;
            jv out = JV_UNDEF;
            SYNC();
            int k = js_aiter_call(vm, ro, 4, JV_UNDEF, &out); RELOAD();
            if (k < 0 && quiet && vm->throwing == 1) { vm->throwing = 0; vm->exception = JV_UNDEF; k = 0; }
            if (vm->throwing) break;
            if (k == 0) { pc = (uint32_t)((int32_t)(at + 4) + off); goto next; }
            stack[sp++] = out;
            goto next;
        }
        case OP_ITER_ACHECK: {
            jv r = stack[--sp];
            if (!jv_is_obj(r)) { SYNC(); js_result_error(vm, r); RELOAD(); break; }
            goto next;
        }
        case OP_AYS_STEP: {
            /* an async yield*'s step: the inner iterator's next, throw or return called
               with what the generator received */
            uint32_t at = pc;
            int32_t nr_off = (int32_t)RD32(at), nt_off = (int32_t)RD32(at + 4);
            uint32_t nit = RD16(at + 8);
            pc = at + 10;
            double m = jv_dbl(stack[--sp]);
            jv recv = stack[--sp];
            uint32_t ro = jv_idx(stack[sp - 1]);
            jv out = JV_UNDEF;
            SYNC();
            int k = js_aiter_call(vm, ro, (int)m, recv, &out); RELOAD();
            if (vm->throwing) break;
            if (k == 0 && m == 2) {
                sp = base + funcs[frames[fi].func].nslots + nit;
                stack[sp++] = recv;
                pc = (uint32_t)((int32_t)(at + 4) + nr_off);
                goto next;
            }
            if (k == 0) { pc = (uint32_t)((int32_t)(at + 8) + nt_off); goto next; }
            stack[sp++] = jv_num(m);
            stack[sp++] = out;
            goto next;
        }
        case OP_AYS_RESULT: {
            uint32_t at = pc;
            int32_t d_off = (int32_t)RD32(at), r_off = (int32_t)RD32(at + 4);
            uint32_t nit = RD16(at + 8);
            pc = at + 10;
            jv r = stack[sp - 1];
            double m = jv_dbl(stack[sp - 2]);
            SYNC();
            if (!jv_is_obj(r)) { js_result_error(vm, r); RELOAD(); break; }
            jv d = js_get(vm, r, A_DONE); RELOAD();
            if (vm->throwing) break;
            int dn = js_truthy(vm, d);
            jv v = js_get(vm, r, A_VALUE); RELOAD();
            if (vm->throwing) break;
            if (!dn) { sp--; stack[sp - 1] = v; goto next; }
            if (m == 2) {
                sp = base + funcs[frames[fi].func].nslots + nit;
                stack[sp++] = v;
                pc = (uint32_t)((int32_t)(at + 8) + r_off);
                goto next;
            }
            sp -= 2;
            stack[sp - 1] = v;
            pc = (uint32_t)((int32_t)(at + 4) + d_off);
            goto next;
        }
        case OP_AYS_UNWRAP: {
            uint32_t at = pc;
            int32_t off = (int32_t)RD32(at);
            pc = at + 4;
            if (jv_dbl(stack[sp - 1]) != 2) { pc = (uint32_t)((int32_t)(at + 4) + off); goto next; }
            sp--;
            goto next;
        }
        case OP_AYS_RETMODE: {
            stack[sp - 1] = jv_num(jv_dbl(stack[sp - 1]) == 0 ? 2 : 1);
            goto next;
        }
        case OP_AYS_NOTHROW: {
            SYNC();
            js_throw_msg(vm, vm->type_error_proto, "The iterator does not provide a 'throw' method"); RELOAD();
            break;
        }
        case OP_CALL_EVAL: {
            /* eval called by its name: a direct eval when the name holds eval itself, the
               code compiled for this site and run in this frame's environment, with its
               this and super; any other function a plain call */
            uint32_t argc = RD16(pc), site = RD32(pc + 2);
            pc += 6;
            uint32_t sp0 = sp - argc - 1;
            jv callee = stack[sp0];
            if (jv_is_obj(callee) && jv_idx(callee) == vm->eval_fn) {
                jv code_v = argc ? stack[sp0 + 1] : JV_UNDEF;
                sp = sp0;
                SYNC();
                uint32_t cl = frames[fi].callee;
                uint32_t home = cl && objs[cl].kind == JK_CLOSURE ? objs[cl].c : 0;
                vm->eval_site = site;
                jv r = js_eval_code(vm, code_v, JS_CC_DIRECT, frames[fi].env, frames[fi].thisv, home); RELOAD();
                if (vm->throwing) break;
                stack[sp++] = r;
                goto next;
            }
            if (!CALLABLE(callee)) { frames[fi].pc = pc; SYNC(); js_call_error(vm, pc - 7, callee, 0); RELOAD(); break; }
            jv av[64];
            for (uint32_t i = 0; i < argc && i < 64; i++) av[i] = stack[sp0 + 1 + i];
            sp = sp0;
            SYNC();
            jv r = js_call(vm, callee, JV_UNDEF, av, argc < 64 ? argc : 64); RELOAD();
            if (vm->throwing) break;
            stack[sp++] = r;
            goto next;
        }
        case OP_DYN_GET: case OP_DYN_PUT: {
            /* a name looked for in a dynamic scope's object: a with's (HasProperty, its
               unscopables heeded) or a sloppy function's eval vars */
            uint32_t depth = code[pc], cell = RD16(pc + 1), atom = RD32(pc + 3), fl = code[pc + 7];
            uint32_t at = pc + 8;
            int32_t off = (int32_t)RD32(at);
            pc = at + 4;
            uint32_t e = frames[fi].env;
            for (uint32_t k = 0; k < depth; k++) e = objs[e].a;
            jv o = slots[objs[e].slots + cell];
            if (!jv_is_obj(o)) goto next;
            SYNC();
            int has = js_has(vm, jv_idx(o), atom); RELOAD();
            if (vm->throwing) break;
            if (has && (fl & 1)) {
                jv un = js_get(vm, o, JS_SYM_KEY | JSYM_UNSCOPABLES); RELOAD();
                if (vm->throwing) break;
                if (jv_is_obj(un)) {
                    jv blocked = js_get(vm, un, atom); RELOAD();
                    if (vm->throwing) break;
                    if (js_truthy(vm, blocked)) has = 0;
                    RELOAD();
                }
            }
            if (!has) goto next;
            if (op == OP_DYN_GET) {
                jv v = js_get(vm, o, atom); RELOAD();
                if (vm->throwing) break;
                if (fl & 2) stack[sp - 1] = o;
                stack[sp++] = v;
            } else {
                js_set(vm, o, atom, stack[sp - 1]); RELOAD();
                if (vm->throwing) break;
                if (!(fl & 4)) sp--;
            }
            pc = (uint32_t)((int32_t)(at + 4) + off);
            goto next;
        }
        case OP_EVALVAR_DEF: {
            /* a sloppy eval's var: its caller's eval vars object made at the first one;
               a var already there kept, a function's value set */
            uint32_t depth = code[pc], cell = RD16(pc + 1), atom = RD32(pc + 3), isfn = code[pc + 7];
            pc += 8;
            jv v = stack[--sp];
            uint32_t e = frames[fi].env;
            for (uint32_t k = 0; k < depth; k++) e = objs[e].a;
            jv o = slots[objs[e].slots + cell];
            SYNC();
            if (!jv_is_obj(o)) {
                uint32_t vo = js_obj_new(vm, JK_PLAIN, 0); RELOAD();
                o = jv_obj(vo);
                e = frames[fi].env;
                for (uint32_t k = 0; k < depth; k++) e = objs[e].a;
                slots[objs[e].slots + cell] = o;
            }
            jv cur;
            uint32_t cf;
            int had = js_get_own(vm, jv_idx(o), atom, &cur, &cf); RELOAD();
            if (isfn || !had) { js_define(vm, jv_idx(o), atom, v, 0); RELOAD(); }
            goto next;
        }
        case OP_TO_OBJECT: {
            jv v = stack[sp - 1];
            if (jv_is_obj(v)) goto next;
            if (jv_is_nullish(v)) {
                SYNC();
                /* an object pattern's value checked (its first key computed, a rest, or none) */
                if (!js_destructure_at(vm, pc - 1, v, 0)) js_throw_msg(vm, vm->type_error_proto, "Cannot convert undefined or null to object");
                RELOAD();
                break;
            }
            SYNC();
            jv r = js_to_object(vm, v); RELOAD();
            if (vm->throwing) break;
            stack[sp - 1] = r;
            goto next;
        }
        case OP_GET_IMPORT: {
            /* an imported name: its exporter's cell (or the namespace), read as it is now */
            uint32_t b = RD32(pc), atom = RD32(pc + 4);
            pc += 8;
            js_modbind mb = vm->mod_binds[b];
            jv v = JV_HOLE;
            if (!mb.module) { if (mb.cell) v = jv_obj(mb.cell); }
            else {
                uint32_t env = vm->modules[mb.module].env;
                if (env) v = slots[objs[env].slots + mb.cell];
            }
            if (v == JV_HOLE) { SYNC(); js_tdz_error(vm, atom); RELOAD(); break; }
            stack[sp++] = v;
            goto next;
        }
        case OP_IMPORT_META: {
            SYNC();
            jv r = js_module_meta(vm, funcs[frames[fi].func].module); RELOAD();
            stack[sp++] = r;
            goto next;
        }
        case OP_IMPORT_DYN: {
            /* import(): the referrer this code's module, or (0) the classic scripts' base */
            sp--;
            jv spec = stack[--sp];
            uint32_t mod = funcs[frames[fi].func].module;
            uint32_t referrer = mod ? vm->modules[mod].url : 0;
            SYNC();
            jv r = js_module_dynamic(vm, spec, referrer); RELOAD();
            stack[sp++] = r;
            goto next;
        }
        case OP_PRIV_NEW: {
            uint32_t atom = RD32(pc);
            pc += 4;
            uint32_t y = js_sym_new(vm, atom); RELOAD();
            vm->syms[y].reg = JS_SYM_PRIVATE;
            stack[sp++] = jv_sym(y);
            break;
        }
        case OP_PRIV_GET: case OP_PRIV_GET2: {
            uint32_t kind = code[pc], atom = RD32(pc + 1), cls = RD32(pc + 5);
            pc += 9;
            jv k = stack[sp - 1], ov = stack[sp - 2];
            SYNC();
            jv v = js_priv_get(vm, ov, k, kind, atom, cls); RELOAD();
            if (vm->throwing) break;
            if (op == OP_PRIV_GET) { sp--; stack[sp - 1] = v; }
            else stack[sp - 1] = v;
            break;
        }
        case OP_PRIV_PUT: {
            uint32_t kind = code[pc], atom = RD32(pc + 1), cls = RD32(pc + 5);
            pc += 9;
            jv v = stack[sp - 1], k = stack[sp - 2], ov = stack[sp - 3];
            SYNC();
            js_priv_put(vm, ov, k, v, kind, atom, cls); RELOAD();
            if (vm->throwing) break;
            sp -= 2;
            stack[sp - 1] = v;
            break;
        }
        case OP_PRIV_DEFINE: case OP_PRIV_ACCESSOR: {
            uint32_t kind = code[pc], atom = RD32(pc + 1), cls = RD32(pc + 5);
            pc += 9;
            int acc = op == OP_PRIV_ACCESSOR;
            uint32_t base = sp - (acc ? 4 : 3);
            jv ov = stack[base], k = stack[base + 1];
            jv v = acc ? JV_UNDEF : stack[base + 2], g = acc ? stack[base + 2] : JV_UNDEF, st = acc ? stack[base + 3] : JV_UNDEF;
            SYNC();
            js_priv_add(vm, ov, k, v, g, st, kind, atom, cls); RELOAD();
            if (vm->throwing) break;
            sp = base + 1;
            break;
        }
        case OP_PRIV_IN: {
            uint32_t atom = RD32(pc);
            pc += 4;
            jv k = stack[sp - 1], ov = stack[sp - 2];
            if (!jv_is_obj(ov)) {
                SYNC();
                vector<char> m;
                js_put(&m, "Cannot use 'in' operator to search for '");
                js_str_to_utf8(vm, atom, &m);
                js_put(&m, "' in ");
                js_value_text(vm, ov, &m);
                js_throw_chars(vm, vm->type_error_proto, &m); RELOAD();
                break;
            }
            uint32_t fl = 0;
            int has = js_priv_slot(vm, ov, k, &fl) != 0; RELOAD();
            sp--;
            stack[sp - 1] = jv_bool(has);
            goto next;
        }
        case OP_HOME: {
            jv fn = stack[sp - 1], home = stack[sp - 2];
            if (jv_is_obj(fn) && objs[jv_idx(fn)].kind == JK_CLOSURE && jv_is_obj(home)) objs[jv_idx(fn)].c = jv_idx(home);
            goto next;
        }
        case OP_NOP: goto next;
        /* every byte has its case (no default): the switch's jump has no range check */
        OP_UNUSED8(OP_NOPS) OP_UNUSED8(OP_NOPS + 8) OP_UNUSED8(OP_NOPS + 16) OP_UNUSED8(OP_NOPS + 24)
        OP_UNUSED8(OP_NOPS + 32) OP_UNUSED8(OP_NOPS + 40) OP_UNUSED8(OP_NOPS + 48) OP_UNUSED8(OP_NOPS + 56)
        OP_UNUSED8(OP_NOPS + 64) OP_UNUSED8(OP_NOPS + 72) OP_UNUSED8(OP_NOPS + 80) OP_UNUSED8(OP_NOPS + 88)
        OP_UNUSED8(OP_NOPS + 96) OP_UNUSED8(OP_NOPS + 104) OP_UNUSED8(OP_NOPS + 112) OP_UNUSED8(OP_NOPS + 120) {
            SYNC();
            js_throw_msg(vm, vm->syntax_error_proto, "the engine met an opcode it does not know"); RELOAD();
            break;
        }
        }
        if (vm->throwing) {
            RELOAD();
            uint32_t floor = frames[entry_frame].handlers;
            {
                /* the frames the exception leaves on its way to its handler (or out of
                   this run): a generator's run is over; an async run's catches it, its
                   promise rejected, the run returning the promise */
                int has_h = vm->throwing == 1 && vm->nhandlers > floor;
                int64_t lo = has_h ? (int64_t)vm->handlers[vm->nhandlers - 1].frame + 1 : (int64_t)entry_frame;
                int64_t caught = -1;
                for (int64_t f = fi; f >= lo && caught < 0; f--) {
                    uint32_t g = frames[f].gen;
                    if (!g) continue;
                    if ((objs[g].kind == JK_ASYNC || objs[g].kind == JK_ASYNCGEN) && vm->throwing == 1) caught = f;
                    objs[g].a = JG_DONE;
                    objs[g].b = 0;
                }
                if (caught >= 0) {
                    /* the frames above the run's dropped first: an async generator's request
                       answered may run code (a then getter) */
                    uint32_t af = (uint32_t)caught;
                    uint32_t g = frames[af].gen, aflags = frames[af].flags, asp0 = frames[af].sp0;
                    jv e = vm->exception;
                    vm->throwing = 0;
                    vm->exception = JV_UNDEF;
                    vm->nhandlers = frames[af].handlers;
                    vm->nframes = af;
                    vm->sp = asp0;
                    jv res = JV_UNDEF;
                    if (objs[g].kind == JK_ASYNC) {
                        uint32_t pr = objs[g].c;
                        js_promise_settle(vm, pr, 1, e); RELOAD();
                        res = jv_obj(pr);
                    } else {
                        js_ag_complete(vm, g, 1, e, 1); RELOAD();
                        js_ag_drain(vm, g); RELOAD();
                    }
                    sp = asp0;
                    if (aflags & JFR_NATIVE_ENTRY) { vm->sp = sp; return res; }
                    stack[sp++] = res;
                    fi = af - 1;
                    pc = frames[fi].pc;
                    base = frames[fi].base;
                    goto next;
                }
            }
            if (vm->throwing == 1 && vm->nhandlers > floor) {
                js_handler h = vm->handlers[--vm->nhandlers];
                vm->nframes = h.frame + 1;
                fi = h.frame;
                base = frames[fi].base;
                frames[fi].env = h.env;
                sp = h.sp;
                pc = h.pc;
                stack[sp++] = vm->exception;
                vm->exception = JV_UNDEF;
                vm->throwing = 0;
            } else {
                vm->nhandlers = floor;
                vm->nframes = entry_frame;
                vm->sp = frames[entry_frame].sp0;
                return JV_HOLE;
            }
        }
    }
}

/* ---- the VM's life ---- */

static const char *const js_atom_names[A_NATOMS] = {
    "", "length", "prototype", "constructor", "toString", "valueOf", "name", "message", "stack", "callee",
    "get", "set", "value", "writable", "enumerable", "configurable", "undefined", "null", "true", "false",
    "object", "function", "number", "string", "boolean", "symbol", "", "default", "done", "next",
    "then", "toJSON", "index", "input", "arguments", "cause", "lastIndex", "size",
    "return", "throw", "description", "status", "reason", "fulfilled", "rejected", "errors", "bigint", "caller" };

void js_init(js_vm *vm) {
    memset(vm, 0, sizeof *vm);
    js_setup(vm);
}

/* a zeroed VM made ready (js_init's work after its memset): the host it was given, if
   any, kept */
void js_setup(js_vm *vm) {
    js_str dummy;
    memset(&dummy, 0, sizeof dummy);
    push_back(vm->strs, dummy);
    resize(vm->atom_slots, 1024);
    for (uint32_t i = 1; i < A_NATOMS; i++) {
        uint32_t s = js_str_utf8(vm, js_atom_names[i], strlen(js_atom_names[i]));
        js_atom(vm, s);
    }
    for (int d = 0; d < 10; d++) {
        char b[2] = { (char)('0' + d), 0 };
        vm->digit_strs[d] = js_atom_ascii(vm, b);
    }
    static const char *const wk[JSYM_N] = { "", "Symbol.iterator", "Symbol.asyncIterator", "Symbol.hasInstance",
        "Symbol.isConcatSpreadable", "Symbol.match", "Symbol.matchAll", "Symbol.replace", "Symbol.search", "Symbol.species",
        "Symbol.split", "Symbol.toPrimitive", "Symbol.toStringTag", "Symbol.unscopables" };
    for (uint32_t i = 0; i < JSYM_N; i++) {
        js_sym y = { i ? js_str_ascii(vm, wk[i]) : 0, 0 };
        push_back(vm->syms, y);
    }
    js_shape s0 = { 0, 0, 0, 0, 0, 0 };
    push_back(vm->shapes, s0);
    resize(vm->roots, 64);
    resize(vm->trans, 1024);
    resize(vm->lookup, 3 * 4096);
    js_obj o0;
    memset(&o0, 0, sizeof o0);
    push_back(vm->objs, o0);
    push_back(vm->slots, JV_UNDEF);
    js_ic ic0 = { 0, 0, 0, 0 };
    push_back(vm->ics, ic0);
    js_func f0;
    memset(&f0, 0, sizeof f0);
    push_back(vm->funcs, f0);
    push_back(vm->code, (uint8_t)OP_NOP);
    push_back(vm->consts, JV_UNDEF);
    resize(vm->stack, JS_STACK_START);
    resize(vm->frames, 256);
    resize(vm->handlers, 4096);
    vm->exception = JV_UNDEF;
    vm->step_budget = 20000000000ull;
    vm->heap_cap = 1024ull * 1024 * 1024;
    vm->rng = 0x2545F491u;
    js_builtins_init(vm);
}

jv js_eval_code(js_vm *vm, jv code, int mode, uint32_t env, jv thisv, uint32_t home) {
    if (!jv_is_str(code)) return code;
    uint32_t site = mode == JS_CC_DIRECT ? vm->eval_site : 0, src = jv_idx(code), func = 0;
    int found = 0;
    for (size_t i = len(vm->eval_cache); i-- > 0 && !found;) {
        js_evalcache c = vm->eval_cache[i];
        if (c.mode == (uint32_t)mode && c.site == site && js_str_eq(vm, c.src, src)) { func = c.func; found = 1; }
    }
    if (!found) {
        vector<char> text;
        js_str_to_utf8(vm, src, &text);
        vector<char> err;
        if (!js_compile(vm, data(text), len(text), vm->script, mode, &func, &err)) {
            /* the compiler's message, without its own "SyntaxError: " and its " (line N)" (V8's message names no
               line: the error's position is the eval's) */
            size_t skip = len(err) > 13 && memcmp(data(err), "SyntaxError: ", 13) == 0 ? 13 : 0;
            size_t end = len(err);
            if (end > skip && err[end - 1] == ')') {
                size_t q = end - 1;
                while (q > skip && err[q - 1] >= '0' && err[q - 1] <= '9') q--;
                if (q >= skip + 7 && q < end - 1 && memcmp(data(err) + q - 7, " (line ", 7) == 0) end = q - 7;
            }
            uint32_t m = js_str_utf8(vm, data(err) + skip, end - skip);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->syntax_error_proto, m)));
        }
        if (len(vm->eval_cache) >= JS_EVAL_CACHE_MAX) clear(vm->eval_cache);
        js_evalcache c = { (uint32_t)mode, site, src, func };
        push_back(vm->eval_cache, c);
    }
    uint32_t c = js_closure_new(vm, func, env, thisv);
    vm->objs[c].c = home;
    return js_call(vm, jv_obj(c), thisv, 0, 0);
}

int js_eval(js_vm *vm, const char *src, size_t n, const char *name, uint32_t script) {
    clear(vm->out);
    vm->script = script;
    uint32_t func = 0;
    if (!js_compile(vm, src, n, script, 0, &func, &vm->out)) return -1;
    (void)name;
    uint32_t c = js_closure_new(vm, func, 0, JV_UNDEF);
    vm->throwing = 0;
    js_call(vm, jv_obj(c), jv_obj(vm->global), 0, 0);
    if (vm->throwing == 2) { vm->throwing = 0; append_range(vm->out, "the script ran past its step budget", 35); return -3; }
    if (vm->throwing) {
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        js_error_text(vm, e, &vm->out);
        return -2;
    }
    return 0;
}
