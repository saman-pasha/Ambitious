/* src/js/gc.c -- the collector: mark and sweep over the engine's heap, at a moment no
   C code holds a value outside the VM's own tables and the host's roots (between tasks:
   a script's end, a timer's, a listener's). It runs for `ambitious js` and for a page
   (src/script/script.c turns it on), when the heap has doubled since the last
   collection (32 MB at least); AMB_GC_STRESS=1 makes it run at every such moment.

   What is live is what the roots reach: the global object and the prototypes, the
   value stack and the frames on it, the code's constants, the jobs, timers, listeners
   and console messages, the regular expressions, the modules, the host's wrappers, the
   symbols, every atom, every prototype a shape names (shapes are never freed, and
   theirs stay with them), and every value the host says it holds (vm->host_roots: a
   page's prototypes, its records' objects, its tasks' functions ...; each of the page's
   units names its own, src/script/). An object reaches its prototype, its properties and
   what its kind holds (an array's elements, a closure's environment, home and this, a
   bound function's arguments, a promise's result and reactions, a generator's saved
   frame, a host object's value, ...). A WeakMap's or a WeakSet's entry is live only
   while its key is (its value then marked: ephemerons, to a fixpoint); a WeakRef's
   target is cleared once nothing else reaches it.

   An object, a string and a BigInt keep their index while they live (the host and the
   values hold them by it); a dead one's record goes to a free list its kind's
   allocator takes first. The storage they own is compacted: the slots (properties,
   elements, cells), the strings' code units (a substring shares its parent's: ranges
   that overlap move as one block) and the BigInts' limbs. The property caches, which
   name holders by index, are cleared. */
#include "js.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ---- marking ---- */

typedef struct gc_state {
    vector<uint8_t> om, sm, bm;       /* marks: objects, strings, BigInts */
    vector<uint32_t> ostack, sstack;  /* objects and strings to visit */
    vector<uint32_t> weaks;           /* the weak collections and WeakRefs met */
    vector<uint32_t> finregs;         /* the FinalizationRegistries met */
    vector<jv> cleanups;              /* each target that died: its registry's cleanup, its held value */
} gc_state;
#define GC_STATE_DROP(g) vector_uint8_t_drop(&(g).om); vector_uint8_t_drop(&(g).sm); vector_uint8_t_drop(&(g).bm); \
    vector_uint32_t_drop(&(g).ostack); vector_uint32_t_drop(&(g).sstack); vector_uint32_t_drop(&(g).weaks); \
    vector_uint32_t_drop(&(g).finregs); vector_jv_drop(&(g).cleanups)

static void mark_obj(js_vm *vm, gc_state *g, uint32_t o) {
    if (!o || o >= len(vm->objs) || g->om[o]) return;
    g->om[o] = 1;
    push_back(g->ostack, o);
}
static void mark_str(js_vm *vm, gc_state *g, uint32_t s) {
    if (s >= len(vm->strs) || g->sm[s]) return;
    g->sm[s] = 1;
    if (vm->strs[s].rope) push_back(g->sstack, s);
}
static void mark_val(js_vm *vm, gc_state *g, jv v) {
    if (jv_is_obj(v)) mark_obj(vm, g, jv_idx(v));
    else if (jv_is_str(v)) mark_str(vm, g, jv_idx(v));
    else if (jv_is_big(v)) { uint32_t b = jv_idx(v); if (b < len(vm->bigs)) g->bm[b] = 1; }
}
static void mark_slots(js_vm *vm, gc_state *g, uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n && off + i < len(vm->slots); i++) mark_val(vm, g, vm->slots[off + i]);
}

/* a weak object: a WeakMap's or a WeakSet's entries, a WeakRef's target, held weakly */
static int weak_coll(js_vm *vm, uint32_t o) {
    uint32_t k = vm->objs[o].kind;
    return (vm->objs[o].flags & JO_WEAK) && (k == JK_MAP || k == JK_SET);
}
static int weak_ref(js_vm *vm, uint32_t o) {
    return (vm->objs[o].flags & JO_WEAK) && vm->objs[o].kind == JK_PLAIN && vm->objs[o].proto == vm->weakref_proto;
}

/* what an object reaches, by its kind (js.h's js_obj) */
static void visit_obj(js_vm *vm, gc_state *g, uint32_t o) {
    js_obj x = vm->objs[o];
    mark_obj(vm, g, x.proto);
    mark_slots(vm, g, x.slots, x.cap);
    switch (x.kind) {
    case JK_ARRAY: mark_slots(vm, g, x.a, x.b); break;
    case JK_CLOSURE: mark_obj(vm, g, x.b); mark_obj(vm, g, x.c); mark_val(vm, g, x.x); break;
    case JK_NATIVE: mark_str(vm, g, x.c); mark_val(vm, g, x.x); break;
    case JK_BOUND: mark_obj(vm, g, x.a); mark_slots(vm, g, x.b, x.c); mark_val(vm, g, x.x); break;
    case JK_ARGUMENTS: mark_obj(vm, g, x.a); if (x.c) mark_obj(vm, g, x.c); mark_val(vm, g, x.x); break;
    case JK_ENV: mark_obj(vm, g, x.a); break;
    case JK_ACCESSOR: mark_val(vm, g, x.x); mark_obj(vm, g, x.a); break;
    case JK_MAP: case JK_SET:
        /* a weak one's entries array is kept, its keys and values left to the ephemerons */
        if (weak_coll(vm, o)) {
            if (x.a && !g->om[x.a]) g->om[x.a] = 1;
            push_back(g->weaks, o);
        } else mark_obj(vm, g, x.a);
        mark_obj(vm, g, x.c);
        break;
    case JK_ITER:
        if (x.c == JI_STRING) mark_str(vm, g, x.a);
        else mark_obj(vm, g, x.a);
        mark_val(vm, g, x.x);
        break;
    case JK_GENERATOR: case JK_ASYNC: case JK_ASYNCGEN: mark_obj(vm, g, x.b); mark_obj(vm, g, x.c); mark_val(vm, g, x.x); break;
    case JK_PROMISE: mark_obj(vm, g, x.b); mark_val(vm, g, x.x); break;
    case JK_PROXY: mark_obj(vm, g, x.a); mark_obj(vm, g, x.b); break;
    case JK_TYPEDARRAY: case JK_DATAVIEW: mark_obj(vm, g, x.a); break;
    case JK_RECORD: mark_obj(vm, g, x.a); mark_val(vm, g, x.x); break;
    /* a host object: a, b and c the host's numbers (a node, a record), x a value (an event's detail) */
    case JK_HOST: mark_val(vm, g, x.x); break;
    default:
        if (x.flags & JO_FINREG) {
            /* a FinalizationRegistry: its cleanup and its cells' held values kept, the cells'
               array live but not followed, its targets and tokens weak */
            mark_val(vm, g, x.x);
            if (x.a && x.a < len(vm->objs) && !g->om[x.a]) {
                g->om[x.a] = 1;
                uint32_t base = vm->objs[x.a].a, n = vm->objs[x.a].b;
                for (uint32_t i = 1; i < n; i += 3) mark_val(vm, g, vm->slots[base + i]);
            }
            push_back(g->finregs, o);
            break;
        }
        /* a WeakRef's target is weak; any other kind's x a value (a wrapper's, a date's, ...) */
        if (weak_ref(vm, o)) push_back(g->weaks, o);
        else mark_val(vm, g, x.x);
        break;
    }
}

static void drain(js_vm *vm, gc_state *g) {
    for (;;) {
        if (len(g->ostack)) {
            uint32_t o = back(g->ostack);
            pop_back(g->ostack);
            visit_obj(vm, g, o);
            continue;
        }
        if (len(g->sstack)) {
            uint32_t s = back(g->sstack);
            pop_back(g->sstack);
            mark_str(vm, g, vm->strs[s].left);
            mark_str(vm, g, vm->strs[s].right);
            continue;
        }
        break;
    }
}

static void mark_roots(js_vm *vm, gc_state *g) {
    /* the global object and every object the VM names */
    uint32_t named[] = { vm->global, vm->global_lex, vm->object_proto, vm->function_proto, vm->array_proto, vm->string_proto,
        vm->number_proto, vm->boolean_proto, vm->error_proto, vm->type_error_proto, vm->range_error_proto,
        vm->syntax_error_proto, vm->reference_error_proto, vm->date_proto, vm->regexp_proto, vm->map_proto, vm->set_proto,
        vm->iterator_proto, vm->symbol_proto, vm->generator_proto, vm->promise_proto, vm->promise_ctor,
        vm->aggregate_error_proto, vm->async_iterator_proto, vm->async_generator_proto, vm->eval_fn, vm->weakmap_proto,
        vm->weakset_proto, vm->weakref_proto, vm->finreg_proto, vm->arraybuffer_proto, vm->typedarray_proto,
        vm->dataview_proto, vm->textenc_proto, vm->textdec_proto, vm->bigint_proto, vm->error_ctor, vm->type_error_ctor,
        vm->range_error_ctor, vm->syntax_error_ctor, vm->reference_error_ctor, vm->eval_error_proto, vm->root_proto, vm->throw_te, vm->uri_error_proto, vm->array_values, vm->pr_species_acc, vm->pr_species_get,
        vm->array_iter_proto, vm->map_iter_proto, vm->set_iter_proto, vm->string_iter_proto, vm->regexp_iter_proto,
        vm->helper_proto, vm->wrap_proto, vm->iterator_ctor };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++) mark_obj(vm, g, named[i]);
    for (uint32_t i = 0; i < JTA_N; i++) mark_obj(vm, g, vm->ta_protos[i]);
    /* Intl's prototypes, and the formatters toLocaleString keeps for its defaults (src/js/intl.c) */
    for (uint32_t i = 0; i < 16; i++) mark_obj(vm, g, vm->intl_protos[i]);
    mark_obj(vm, g, vm->intl_cache);
    /* every prototype a shape names */
    for (size_t i = 0; i < len(vm->shapes); i++) mark_obj(vm, g, vm->shapes[i].proto);
    /* the code's constants, the stack and its frames */
    for (size_t i = 0; i < len(vm->consts); i++) mark_val(vm, g, vm->consts[i]);
    for (uint32_t i = 0; i < vm->sp && i < len(vm->stack); i++) mark_val(vm, g, vm->stack[i]);
    for (uint32_t i = 0; i < vm->nframes; i++) {
        js_frame f = vm->frames[i];
        mark_obj(vm, g, f.callee);
        mark_obj(vm, g, f.env);
        mark_obj(vm, g, f.args);
        mark_obj(vm, g, f.gen);
        mark_val(vm, g, f.thisv);
        mark_val(vm, g, f.newt);
    }
    for (uint32_t i = 0; i < vm->nhandlers; i++) mark_obj(vm, g, vm->handlers[i].env);
    if (vm->throwing) mark_val(vm, g, vm->exception);
    /* the jobs to run, the promises rejected, the live timers and listeners, the console */
    for (size_t i = vm->jobs_head; i < len(vm->jobs); i++) {
        js_job j = vm->jobs[i];
        mark_val(vm, g, j.fn);
        mark_val(vm, g, j.arg);
        mark_val(vm, g, j.target);
    }
    for (size_t i = 0; i + 1 < len(vm->rejected); i += 2) mark_obj(vm, g, vm->rejected[i]);
    for (size_t i = 0; i < len(vm->timers); i++) {
        if (!vm->timers[i].live) continue;
        mark_val(vm, g, vm->timers[i].fn);
        mark_obj(vm, g, vm->timers[i].args);
    }
    for (size_t i = 0; i < len(vm->listeners); i++) {
        mark_str(vm, g, vm->listeners[i].type);
        if (vm->listeners[i].live) mark_obj(vm, g, vm->listeners[i].fn);
    }
    for (size_t i = 0; i < len(vm->msgs); i++) mark_str(vm, g, vm->msgs[i].text);
    /* the symbols' descriptions, the regular expressions' sources and names */
    for (size_t i = 0; i < len(vm->syms); i++) mark_str(vm, g, vm->syms[i].desc);
    for (size_t i = 0; i < len(vm->regexes); i++) {
        mark_str(vm, g, vm->regexes[i].source);
        mark_obj(vm, g, vm->regexes[i].names);
    }
    /* the modules */
    for (size_t i = 1; i < len(vm->modules); i++) {
        js_module m = vm->modules[i];
        mark_str(vm, g, m.url);
        uint32_t objs_of[] = { m.env, m.ns, m.meta, m.cap, m.parents };
        for (size_t k = 0; k < sizeof objs_of / sizeof objs_of[0]; k++) mark_obj(vm, g, objs_of[k]);
        mark_val(vm, g, m.error);
    }
    for (size_t i = 0; i < len(vm->mod_reqs); i++) mark_str(vm, g, vm->mod_reqs[i].spec);
    for (size_t i = 0; i < len(vm->mod_binds); i++) if (!vm->mod_binds[i].module) mark_obj(vm, g, vm->mod_binds[i].cell);
    mark_str(vm, g, vm->base_url);
    /* the host's wrappers and names, the atoms, the small numbers' strings */
    for (size_t i = 0; i < len(vm->wrappers); i++) mark_obj(vm, g, vm->wrappers[i]);
    for (size_t i = 0; i < len(vm->hatoms); i++) mark_str(vm, g, vm->hatoms[i]);
    for (size_t i = 0; i < len(vm->atom_slots); i++) if (vm->atom_slots[i]) mark_str(vm, g, vm->atom_slots[i]);
    for (uint32_t i = 0; i < 10; i++) if (vm->digit_strs[i]) mark_str(vm, g, vm->digit_strs[i]);
    for (uint32_t i = 0; i < 1024; i++) if (vm->int_strs[i]) mark_str(vm, g, vm->int_strs[i]);
    mark_str(vm, g, 0);
    /* every value the host holds outside these tables, as it says (a page's: its
       prototypes, its records' objects and values, its tasks' functions) */
    if (vm->host_roots) {
        vector<jv> held;
        vm->host_roots(vm, &held);
        for (size_t i = 0; i < len(held); i++) mark_val(vm, g, held[i]);
    }
}

/* the ephemerons: a weak collection's value marked once its key is, until nothing more is */
static int mark_ephemerons(js_vm *vm, gc_state *g) {
    int more = 0;
    for (size_t w = 0; w < len(g->weaks); w++) {
        uint32_t o = g->weaks[w];
        if (!weak_coll(vm, o) || vm->objs[o].kind != JK_MAP) continue;
        uint32_t arr = vm->objs[o].a;
        if (!arr) continue;
        uint32_t n = vm->objs[arr].b, base = vm->objs[arr].a;
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            jv k = vm->slots[base + i], v = vm->slots[base + i + 1];
            if (k == JV_HOLE) continue;
            int live = !jv_is_obj(k) || g->om[jv_idx(k)];
            if (!live) continue;
            if (jv_is_obj(v) && !g->om[jv_idx(v)]) { mark_val(vm, g, v); more = 1; }
            else if (jv_is_str(v) && !g->sm[jv_idx(v)]) { mark_val(vm, g, v); more = 1; }
            else if (jv_is_big(v) && !g->bm[jv_idx(v)]) { mark_val(vm, g, v); more = 1; }
        }
    }
    return more;
}

/* a weak collection's entries whose keys died taken out, its index made again at need;
   a WeakRef whose target died cleared */
static void clear_weak(js_vm *vm, gc_state *g) {
    for (size_t w = 0; w < len(g->weaks); w++) {
        uint32_t o = g->weaks[w];
        if (weak_ref(vm, o)) {
            jv t = vm->objs[o].x;
            if (jv_is_obj(t) && !g->om[jv_idx(t)]) vm->objs[o].x = JV_UNDEF;
            continue;
        }
        if (!weak_coll(vm, o)) continue;
        uint32_t arr = vm->objs[o].a, step = vm->objs[o].kind == JK_MAP ? 2 : 1;
        if (!arr) continue;
        uint32_t n = vm->objs[arr].b, base = vm->objs[arr].a, w = 0, live = 0;
        for (uint32_t i = 0; i < n; i += step) {
            jv k = vm->slots[base + i];
            if (k == JV_HOLE || (jv_is_obj(k) && !g->om[jv_idx(k)])) continue;
            vm->slots[base + w] = k;
            if (step == 2) vm->slots[base + w + 1] = vm->slots[base + i + 1];
            w += step;
            live++;
        }
        vm->objs[arr].b = w;
        vm->objs[o].b = live;
        vm->objs[o].c = 0;
        vm->objs[o].x = jv_num(0);
    }
}

/* each registry's cells whose targets died taken out, their cleanups to be called (after
   the sweep: nothing is made while marks stand); a token that died, no cell's */
static void finalize(js_vm *vm, gc_state *g) {
    for (size_t r = 0; r < len(g->finregs); r++) {
        uint32_t o = g->finregs[r], cells = vm->objs[o].a;
        if (!cells) continue;
        uint32_t base = vm->objs[cells].a, n = vm->objs[cells].b, w = 0;
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            jv t = vm->slots[base + i], h = vm->slots[base + i + 1], tk = vm->slots[base + i + 2];
            if (jv_is_obj(t) && !g->om[jv_idx(t)]) {
                push_back(g->cleanups, vm->objs[o].x);
                push_back(g->cleanups, h);
                continue;
            }
            if (jv_is_obj(tk) && !g->om[jv_idx(tk)]) tk = JV_UNDEF;
            vm->slots[base + w] = t;
            vm->slots[base + w + 1] = h;
            vm->slots[base + w + 2] = tk;
            w += 3;
        }
        vm->objs[cells].b = w;
    }
}

/* each buffer's chain of views (its c, then each view's x above 32 bits: src/js/typed.c)
   kept to the views that live: the chain is weak, so that no buffer names a dead view's
   index, which the sweep frees and a new object takes */
static void prune_views(js_vm *vm, gc_state *g) {
    for (uint32_t o = 1; o < len(vm->objs); o++) {
        if (!g->om[o] || vm->objs[o].kind != JK_ARRAYBUFFER || !vm->objs[o].c) continue;
        uint32_t prev = 0, v = vm->objs[o].c;
        for (uint32_t guard = 0; v && v < len(vm->objs) && guard < (1u << 30); guard++) {
            uint32_t next = (uint32_t)(vm->objs[v].x >> 32);
            if (g->om[v]) {
                if (prev) vm->objs[prev].x = (vm->objs[prev].x & 0xffffffffu) | ((uint64_t)v << 32);
                else vm->objs[o].c = v;
                prev = v;
            }
            v = next;
        }
        if (prev) vm->objs[prev].x &= 0xffffffffu;
        else vm->objs[o].c = 0;
    }
}

/* ---- sweeping: the dead records onto their free lists ---- */

static void sweep(js_vm *vm, gc_state *g) {
    vm->obj_free = 0;
    for (size_t o = len(vm->objs); o-- > 1;) {
        if (g->om[o]) continue;
        js_obj f;
        memset(&f, 0, sizeof f);
        f.kind = JK_FREE;
        f.a = vm->obj_free;
        vm->objs[o] = f;
        vm->obj_free = (uint32_t)o;
    }
    vm->str_free = 0;
    for (size_t s = len(vm->strs); s-- > 1;) {
        if (g->sm[s]) continue;
        js_str f;
        memset(&f, 0, sizeof f);
        f.rope = 2;
        f.left = vm->str_free;
        vm->strs[s] = f;
        vm->str_free = (uint32_t)s;
    }
    vm->big_free = 0;
    for (size_t b = len(vm->bigs); b-- > 0;) {
        if (g->bm[b]) continue;
        js_big f = { 2, vm->big_free, 0 };
        vm->bigs[b] = f;
        vm->big_free = (uint32_t)b + 1;
    }
}

/* ---- compaction ---- */

/* ascending, by the high 32 bits (an offset) then the low (an index): a radix sort of
   four 16-bit digits, linear in the count */
static void sort_u64(vector<uint64_t> *v) {
    size_t n = len(*v);
    if (n < 2) return;
    vector<uint64_t> tmp;
    resize(tmp, n);
    vector<uint32_t> count;
    resize(count, 65536);
    for (uint32_t shift = 0; shift < 64; shift += 16) {
        uint32_t *c = data(count);
        memset(c, 0, 65536 * sizeof(uint32_t));
        uint64_t *src = data(*v);
        for (size_t i = 0; i < n; i++) c[(src[i] >> shift) & 0xFFFF]++;
        uint32_t sum = 0;
        for (uint32_t d = 0; d < 65536; d++) { uint32_t k = c[d]; c[d] = sum; sum += k; }
        uint64_t *dst = data(tmp);
        for (size_t i = 0; i < n; i++) dst[c[(src[i] >> shift) & 0xFFFF]++] = src[i];
        for (size_t i = 0; i < n; i++) src[i] = dst[i];
    }
}

/* a range of the slots an object owns: its properties (0), an array's elements (1), a
   bound function's arguments (2) */
typedef struct gc_range { uint32_t off, n, obj, field; } gc_range;

/* the slots of the live objects moved down over the dead ones' */
static void compact_slots(js_vm *vm) {
    vector<gc_range> rs;
    vector<uint64_t> order;
    for (size_t o = 1; o < len(vm->objs); o++) {
        js_obj x = vm->objs[o];
        if (x.kind == JK_FREE) continue;
        if (x.cap) { gc_range r = { x.slots, x.cap, (uint32_t)o, 0 }; push_back(rs, r); }
        if (x.kind == JK_ARRAY && x.b) { gc_range r = { x.a, x.b, (uint32_t)o, 1 }; push_back(rs, r); }
        if (x.kind == JK_BOUND && x.c) { gc_range r = { x.b, x.c, (uint32_t)o, 2 }; push_back(rs, r); }
    }
    for (size_t i = 0; i < len(rs); i++) push_back(order, (uint64_t)rs[i].off << 32 | (uint64_t)i);
    sort_u64(&order);
    uint32_t dst = 0;
    uint64_t *sl = data(vm->slots);
    for (size_t k = 0; k < len(order); k++) {
        gc_range r = rs[(size_t)(order[k] & 0xFFFFFFFFu)];
        if (r.off != dst) memmove(sl + dst, sl + r.off, (size_t)r.n * sizeof(uint64_t));
        if (r.field == 0) vm->objs[r.obj].slots = dst;
        else if (r.field == 1) { vm->objs[r.obj].a = dst; vm->objs[r.obj].c = r.n; }
        else vm->objs[r.obj].b = dst;
        dst += r.n;
    }
    /* an array with no elements, its capacity none */
    for (size_t o = 1; o < len(vm->objs); o++) if (vm->objs[o].kind == JK_ARRAY && !vm->objs[o].b) { vm->objs[o].a = dst; vm->objs[o].c = 0; }
    resize(vm->slots, dst);
}

/* the live flat strings' code units moved down; overlapping ranges (a substring and
   its parent) as one block */
static void compact_units(js_vm *vm) {
    vector<uint64_t> order;
    for (size_t s = 1; s < len(vm->strs); s++) {
        js_str x = vm->strs[s];
        if (x.rope || !x.len) continue;
        push_back(order, (uint64_t)x.off << 32 | (uint64_t)s);
    }
    sort_u64(&order);
    uint32_t dst = 0;
    uint16_t *u = data(vm->units);
    size_t k = 0;
    while (k < len(order)) {
        /* a block: the strings whose ranges overlap the first's, as far as they reach */
        uint32_t s0 = (uint32_t)(order[k] & 0xFFFFFFFFu);
        uint32_t start = vm->strs[s0].off, end = start + vm->strs[s0].len;
        size_t j = k + 1;
        while (j < len(order)) {
            uint32_t sj = (uint32_t)(order[j] & 0xFFFFFFFFu);
            if (vm->strs[sj].off >= end) break;
            uint32_t e = vm->strs[sj].off + vm->strs[sj].len;
            if (e > end) end = e;
            j++;
        }
        if (start != dst) memmove(u + dst, u + start, (size_t)(end - start) * sizeof(uint16_t));
        for (size_t i = k; i < j; i++) {
            uint32_t si = (uint32_t)(order[i] & 0xFFFFFFFFu);
            vm->strs[si].off = dst + (vm->strs[si].off - start);
        }
        dst += end - start;
        k = j;
    }
    for (size_t s = 1; s < len(vm->strs); s++) if (!vm->strs[s].rope && !vm->strs[s].len) vm->strs[s].off = 0;
    resize(vm->units, dst);
}

/* the live BigInts' limbs moved down */
static void compact_limbs(js_vm *vm) {
    vector<uint64_t> order;
    for (size_t b = 0; b < len(vm->bigs); b++) {
        js_big x = vm->bigs[b];
        if (x.neg == 2 || !x.n) continue;
        push_back(order, (uint64_t)x.off << 32 | (uint64_t)b);
    }
    sort_u64(&order);
    uint32_t dst = 0;
    uint32_t *l = data(vm->bigl);
    for (size_t k = 0; k < len(order); k++) {
        uint32_t b = (uint32_t)(order[k] & 0xFFFFFFFFu);
        uint32_t off = vm->bigs[b].off, n = vm->bigs[b].n;
        if (off != dst) memmove(l + dst, l + off, (size_t)n * sizeof(uint32_t));
        vm->bigs[b].off = dst;
        dst += n;
    }
    resize(vm->bigl, dst);
}

/* ---- the collection ---- */

/* a heap's size in eight bytes: its slots, units and limbs, and its records counted (an
   object six, a string four) */
static uint64_t heap_size(js_vm *vm, uint64_t objs, uint64_t strs) {
    return (uint64_t)len(vm->slots) + len(vm->units) / 4 + objs * 6 + strs * 4 + len(vm->bigl) / 2;
}

static double gc_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void js_gc(js_vm *vm) {
    double t0 = gc_clock();
    uint64_t before = heap_size(vm, len(vm->objs), len(vm->strs));
    gc_state g;
    memset(&g, 0, sizeof g);
    defer(g) { GC_STATE_DROP(g); }
    resize(g.om, len(vm->objs), 0);
    resize(g.sm, len(vm->strs), 0);
    resize(g.bm, len(vm->bigs), 0);
    mark_roots(vm, &g);
    drain(vm, &g);
    while (mark_ephemerons(vm, &g)) drain(vm, &g);
    finalize(vm, &g);
    clear_weak(vm, &g);
    prune_views(vm, &g);
    /* the timers and listeners no longer live let go of their functions */
    for (size_t i = 0; i < len(vm->timers); i++) if (!vm->timers[i].live) { vm->timers[i].fn = JV_UNDEF; vm->timers[i].args = 0; }
    for (size_t i = 0; i < len(vm->listeners); i++) if (!vm->listeners[i].live) vm->listeners[i].fn = 0;
    sweep(vm, &g);
    compact_slots(vm);
    compact_units(vm);
    compact_limbs(vm);
    /* the caches that name objects or hold compiled code's sources */
    for (size_t i = 0; i < len(vm->ics); i++) { js_ic z = { 0, 0, 0, 0 }; vm->ics[i] = z; }
    clear(vm->eval_cache);
    /* each cleanup a FinalizationRegistry owes, a task of its own due now, its held value
       its argument (as V8 posts them after a collection) */
    for (size_t i = 0; i + 1 < len(g.cleanups); i += 2) {
        uint32_t args = js_array_new(vm, 0);
        js_array_push(vm, args, g.cleanups[i + 1]);
        js_timer t;
        memset(&t, 0, sizeof t);
        t.id = ++vm->timer_id;
        t.due = vm->now;
        t.interval = -1;
        t.fn = g.cleanups[i];
        t.live = 1;
        t.seq = ++vm->timer_seq;
        t.run = vm->cur_run;
        t.args = args;
        t.nargs = 1;
        push_back(vm->timers, t);
    }
    vm->gc_count++;
    uint64_t lo = 0, ls = 0;
    for (size_t i = 0; i < len(g.om); i++) lo += g.om[i];
    for (size_t i = 0; i < len(g.sm); i++) ls += g.sm[i];
    vm->gc_last = heap_size(vm, lo, ls);
    vm->gc_allocs = 0;
    vm->gc_slots = len(vm->slots);
    vm->gc_units = len(vm->units);
    /* AMB_GC_DEBUG: each collection's heap before and after, in eight bytes, and its time */
    if (getenv("AMB_GC_DEBUG"))
        fprintf(stderr, "gc %u: %llu -> %llu, %.2f ms\n", vm->gc_count, (unsigned long long)before, (unsigned long long)vm->gc_last, gc_clock() - t0);
}

void js_gc_maybe(js_vm *vm) {
    if (vm->nframes || vm->in_jobs || vm->throwing) return;
    /* AMB_GC_STRESS: a collection at every chance, the collector's own test */
    if (getenv("AMB_GC_STRESS")) { js_gc(vm); return; }
    if (!vm->gc_enabled) return;
    /* what was made since the last: as much again as was live then (and 32 MB at least) */
    uint64_t made = vm->gc_allocs * 5 + (len(vm->slots) - (vm->gc_slots < len(vm->slots) ? vm->gc_slots : len(vm->slots)))
                    + (len(vm->units) - (vm->gc_units < len(vm->units) ? vm->gc_units : len(vm->units))) / 4;
    uint64_t floor = (uint64_t)1 << 22;
    if (made < (vm->gc_last > floor ? vm->gc_last : floor)) return;
    js_gc(vm);
}
