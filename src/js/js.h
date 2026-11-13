/* src/js/js.h -- Ambitious's JavaScript engine: the shared declarations (PLAN.md Phase 8).

   One VM per page. Everything it holds is an index arena, as every other module's
   state is: no value, object or string holds a pointer, so the safe part sees integers.

   VALUES are NaN-boxed 64-bit words. A double is itself (its NaNs canonical); above
   the doubles, the top 16 bits tag the rest: 0xFFF9 the specials (undefined, null,
   false, true, and the hole that marks an uninitialized binding or a missing array
   element), 0xFFFA an object's index, 0xFFFB a string's index, 0xFFFC a symbol's, 0xFFFD
   a BigInt's (src/js/bigint.c).
   A property's key is an atom (a string's id) or a symbol's index with JS_SYM_KEY
   set: one key space for the shapes, two for what keys are.

   STRINGS are UTF-16 code units in one append-only pool. A string is a span of it, or
   a rope of two strings, flattened when it is read; a substring shares its parent's
   units. Names are interned atoms, and every atom is a string.

   OBJECTS have shapes: a shape is a node of a transition tree rooted at a prototype
   (its parent, the key it adds, that key's slot), so an object is its shape and its
   slots, and a property access at a cached site is one comparison and one load.
   Arrays keep their elements dense, in the same slot heap.

   CODE is stack bytecode, compiled from the AST with every variable resolved: a
   frame slot, a cell of an environment (the variables closures capture), or a global.
   Calls push frames on one value stack; natives are cases of a switch, never pointers.

   MEMORY is the page's arena, collected between tasks (src/js/gc.c) and freed with the
   VM, under a cap; TIME is virtual. */
#ifndef AMB_JS_H
#define AMB_JS_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector.pl>

typedef uint64_t jv;

#define JV_UNDEF 0xFFF9000000000000ull
#define JV_NULL  0xFFF9000000000001ull
#define JV_FALSE 0xFFF9000000000002ull
#define JV_TRUE  0xFFF9000000000003ull
#define JV_HOLE  0xFFF9000000000004ull
#define JV_TDZ   0xFFF9000000000005ull      /* a global let or const not yet set */
#define JV_OBJ_TAG 0xFFFA000000000000ull
#define JV_STR_TAG 0xFFFB000000000000ull
#define JV_SYM_TAG 0xFFFC000000000000ull
#define JV_BIG_TAG 0xFFFD000000000000ull
#define JV_NAN   0x7FF8000000000000ull

#define jv_is_num(v) ((v) < 0xFFF9000000000000ull)
#define jv_is_obj(v) (((v) >> 48) == 0xFFFAu)
#define jv_is_str(v) (((v) >> 48) == 0xFFFBu)
#define jv_is_sym(v) (((v) >> 48) == 0xFFFCu)
#define jv_is_big(v) (((v) >> 48) == 0xFFFDu)
#define jv_is_bool(v) ((v) == JV_TRUE || (v) == JV_FALSE)
#define jv_is_nullish(v) ((v) == JV_UNDEF || (v) == JV_NULL)
#define jv_obj(i) (JV_OBJ_TAG | (uint64_t)(uint32_t)(i))
#define jv_str(i) (JV_STR_TAG | (uint64_t)(uint32_t)(i))
#define jv_sym(i) (JV_SYM_TAG | (uint64_t)(uint32_t)(i))
#define jv_big(i) (JV_BIG_TAG | (uint64_t)(uint32_t)(i))
/* a property key that is a symbol's: its index with this bit (an atom never has it) */
#define JS_SYM_KEY 0x80000000u
#define js_key_is_sym(k) (((k) & JS_SYM_KEY) != 0)
#define jv_bool(b) ((b) ? JV_TRUE : JV_FALSE)
#define jv_idx(v) ((uint32_t)(v))

/* a double as a value: the hardware's NaNs are quiet ones below the tags (0x7FF8... and
   0xFFF8...), and no NaN with another payload is ever made, so none needs rewriting */
static inline jv jv_num(double d) {
    jv u;
    memcpy(&u, &d, sizeof u);
    return u;
}
static inline double jv_dbl(jv v) {
    double d;
    memcpy(&d, &v, sizeof d);
    return d;
}

/* ---- the heap's records ---- */

enum {
    JK_PLAIN, JK_ARRAY, JK_CLOSURE, JK_NATIVE, JK_BOUND, JK_ERROR, JK_BOOLEAN, JK_NUMBER, JK_STRING, JK_ARGUMENTS,
    JK_ENV, JK_ACCESSOR, JK_DATE, JK_REGEXP, JK_MAP, JK_SET, JK_HOST, JK_ITER,
    JK_SYMBOL,       /* a symbol's wrapper: the symbol in x */
    JK_GENERATOR,    /* a generator: a its state (JG_*), b its saved frame (an array), x its function */
    JK_ASYNC,        /* an async function's run: as a generator's, c its promise */
    JK_PROMISE,      /* a its state (JP_*), x its result, b its reactions (an array), c its flags */
    JK_PROXY,        /* a its target, b its handler (0: revoked), c 1 when it can be called (src/js/proxy.c) */
    JK_ARRAYBUFFER,  /* a its first byte in vm->bytes, b its length, c its first view, x a resizable one's maximum
                        (a double); resizable and detached in its flags (src/js/typed.c) */
    JK_TYPEDARRAY,   /* a its buffer, b its length in elements now (0 out of its buffer's bounds), c its first byte
                        in the buffer, x its declared length and the buffer's next view; its type in its flags */
    JK_DATAVIEW,     /* a its buffer, b its length in bytes now, c its first byte in the buffer, x as a typed array's */
    JK_BIGINT,       /* a BigInt's wrapper: the value in x */
    JK_ASYNCGEN,     /* an async generator: a its state (JG_*), b its saved frame, c its requests (an array of
                        mode, value and promise, three a request, the first the one its body answers), x its function */
    JK_RECORD,       /* the engine's own record, never a script's value: a an object it holds, x a value, b and c numbers */
    JK_INTL,         /* an Intl object (src/js/intl.c): a its kind (JIK_*), x its internal slots (an array, as a value) */
    JK_FREE          /* a record the collector freed: a the next on the free list (src/js/gc.c) */
};
/* a typed array's element type, in its flags above JO_TA_SHIFT */
enum { JTA_INT8, JTA_UINT8, JTA_UINT8C, JTA_INT16, JTA_UINT16, JTA_INT32, JTA_UINT32, JTA_FLOAT32, JTA_FLOAT64,
       JTA_BIGINT64, JTA_BIGUINT64, JTA_N };
#define JO_TA_SHIFT 8
/* an iterator: a its source, b its place, c its mode; JI_PROTOCOL's source is an
   iterator object whose next (kept in x) is called, JI_GEN's a generator resumed.
   for await's and an async yield*'s: JI_ASYNC's source an async iterator whose next is
   in x, JI_FROM_SYNC's a sync iterator (a JK_ITER) seen as an async one; b is 1 while
   a step is under way, when an error leaves the iterator unclosed */
enum { JI_KEYS, JI_ARRAY, JI_STRING, JI_MAP, JI_SET, JI_MAP_KEYS, JI_MAP_VALUES, JI_ENTRIES, JI_PROTOCOL, JI_GEN,
       JI_ASYNC, JI_FROM_SYNC,
       JI_ARRAY_KEYS, JI_ARRAY_ENTRIES,  /* an array's (an array-like's) keys and [key, value] pairs, read at each step */
       JI_SET_ENTRIES,                   /* a Set's [value, value] pairs */
       JI_REGEXP,                        /* matchAll's matches: its matcher in a, the string in x (src/js/regexp.c) */
       JI_HELPER };                      /* an Iterator Helper (map, filter, take, drop, flatMap): its record in a */
/* a generator's (an async run's) state; an async generator's also suspended at an await
   (executing, to its requests), and awaiting a return's value once done */
enum { JG_START, JG_YIELD, JG_RUNNING, JG_DONE, JG_AWAIT, JG_AWAIT_RETURN };
/* a promise's state, and its flags: handled (a reaction was added), reported */
enum { JP_PENDING, JP_FULFILLED, JP_REJECTED };
#define JPF_HANDLED 1u
#define JPF_REPORTED 2u

/* an object. a, b, c, x are its kind's: an array's elements (slot offset, length,
   capacity); a closure's function, environment and home object, with its captured
   this in x; a native's case number in a and its data in x; a bound function's
   target in a, its arguments in b and c (offset, count) and its this in x; an
   environment's parent in a and its cells' count in b; an accessor's getter in x
   and setter in a; a wrapper's or a date's value in x; a host object's numbers in a,
   b and c (its node and generation, its record: src/script/) and a value in x (an
   event's detail), which the collector follows */
typedef struct js_obj {
    uint32_t shape, slots, cap, proto;
    uint16_t kind, flags;
    uint32_t a, b, c;
    uint64_t x;
} js_obj;

#define JO_EXTENSIBLE 1
#define JO_CLASS_CTOR 2       /* a class's constructor: calling it without new throws */
#define JO_DERIVED 4          /* ... of a class that extends another */
#define JO_FROZEN 8
#define JO_FASTCALL 16        /* a closure the interpreter calls with a frame of its own: not a class's
                                 constructor, without arguments or a rest parameter */
#define JO_WEAK 32            /* a WeakMap, a WeakSet or a WeakRef (src/js/proxy.c) */
#define JO_SEALED 64          /* sealed: every own property not configurable (src/js/proxy.c) */
#define JO_FINREG 128         /* a FinalizationRegistry: x its cleanup, a its cells (an array of target, held
                                 value and token, three a cell; the collector holds the targets and tokens
                                 weakly, and for each target that died calls the cleanup with its held
                                 value in a task of its own: src/js/gc.c) */
/* JO_ELEMPROPS: an array (or an arguments object) with an element of attributes of its
   own, kept in its shape under its index, the element's dense slot a hole (src/js/vm.c);
   JO_LENGTH_RO: an array whose length is not writable. (A comment after a #define kept
   to one line: toolchain/REPORT.md, finding 30.) */
#define JO_ELEMPROPS 256
#define JO_LENGTH_RO 512
/* JO_FN_NOLENGTH, JO_FN_NONAME: a function whose own length (name) was deleted, the engine's no longer its own
   (src/js/vm.c, js_fn_virtual) */
#define JO_FN_NOLENGTH 1024
#define JO_FN_NONAME 2048
/* an array whose elements a write may not take as it finds them: frozen, sealed, not
   extensible, with elements in its shape, or its length read-only (the interpreter's
   fast paths want exactly JO_EXTENSIBLE of these) */
#define JO_ELEM_SLOW (JO_EXTENSIBLE | JO_FROZEN | JO_SEALED | JO_ELEMPROPS | JO_LENGTH_RO)

/* a shape: the key it adds to its parent's, at which slot; its proto, its count */
typedef struct js_shape { uint32_t parent, atom, slot, count, proto, flags; } js_shape;
#define JS_SHAPE_ACCESSOR 1   /* the slot holds an accessor pair */
#define JS_SHAPE_HIDDEN 2     /* not enumerable */
#define JS_SHAPE_READONLY 4   /* not writable (Object.defineProperty's; src/js/proxy.c) */
#define JS_SHAPE_FIXED 8      /* not configurable (the same) */
/* a constructor's prototype as a class's or a native's has it: neither writable, enumerable nor configurable */
#define JS_SHAPE_PROTO (JS_SHAPE_HIDDEN | JS_SHAPE_READONLY | JS_SHAPE_FIXED)

typedef struct js_str { uint32_t off, len, hash, atom, left, right; uint32_t rope; } js_str;

/* a compiled function */
typedef struct js_func {
    uint32_t code, code_len;          /* in vm->code */
    uint32_t nparams, nslots;         /* frame slots: the parameters, then the locals */
    uint32_t name;                    /* an atom, 0 for none */
    uint32_t ics, nics;               /* its property caches, in vm->ics */
    uint32_t flags, line, script;
    uint32_t src_off, src_len;        /* its source text (Function.prototype.toString's) in vm->src; 0 long: none */
    uint32_t arity;                   /* its length: the parameters before a default or a rest */
    uint32_t module;                  /* the module whose code it is, plus one (0: a script's) */
} js_func;
#define JF_ARROW 1
#define JF_ARGUMENTS 2                /* uses arguments */
#define JF_CLASS_CTOR 4
#define JF_DERIVED 8
#define JF_METHOD 16
#define JF_STRICT 32
#define JF_HAS_REST 64
#define JF_GETTER 128
#define JF_GENERATOR 256
#define JF_ASYNC 512
#define JF_NONSIMPLE 1024             /* its parameters not all plain names (a default, a rest, a pattern): its arguments unmapped */
#define JF_TOPLEVEL 2048              /* a script's, a module's or an eval's code: no caller a function's caller can be */

/* a property access's cache: the receiver's shape, the holder (0: the receiver
   itself), the holder's shape, the slot */
typedef struct js_ic { uint32_t shape, holder, hshape, slot; } js_ic;

/* a place in the code whose TypeError names an expression as V8 writes it (its call
   printer): the instruction at pc, the expression's text at [start, end) of vm->src (read
   again only when the error is thrown), what the error says (JSS_*), and how the
   expression is named. The compiler appends them in the order of their code */
typedef struct js_site { uint32_t pc, start, end, kind; } js_site;
enum {
    JSS_CALL,          /* a call's callee: "f.reset is not a function" */
    JSS_NEW,           /* new's: "X is not a constructor" */
    JSS_ITER,          /* for-of's value, an array literal's spread: "x is not iterable" */
    JSS_AITER,         /* for await's: "x is not async iterable" */
    JSS_SPREAD_CALL,   /* a call's spread argument: "Spread syntax requires ...iterable[Symbol.iterator] to be a function" */
    JSS_DESTRUCT_ARR,  /* an array pattern's value */
    JSS_DESTRUCT_OBJ,  /* an object pattern's value: "Cannot destructure property 'a' of 'x' as it is undefined." */
    JSS_YIELD_STAR     /* yield*'s operand */
};
#define JSS_KIND 0xFFu
#define JSS_CALLED 0x100u        /* the expression a call (iterated: "is not a function or its return value is not iterable") */
#define JSS_PLAIN 0x200u         /* named by its value alone (V8's default: "number 5", "object") */
#define JSS_DOTFOR 0x400u        /* a for-of's binding, which V8 names ".for" */
#define JSS_DOTCATCH 0x2000u     /* a catch's binding: ".catch" */
#define JSS_DEFAULTED 0x800u     /* a parameter's default, which V8 names by three intermediate values */
#define JSS_INTERMEDIATE 0x1000u /* "(intermediate value)" */
#define JSS_AWAITED 0x4000u      /* a call for await iterates: "f(...) is not a function or its return value is not async iterable" */
#define JSS_NOSYM 0x8000u        /* an array pattern within an object pattern: its whole value's text, "(cannot read property ...)" */
#define JSS_GENERATOR 0x10000u   /* the code's function a generator: yield an operator when the text is read again */
#define JSS_ASYNC 0x20000u       /* ... async: await one */

typedef struct js_frame {
    uint32_t func, pc, base, env, callee, argc, sp0, flags;
    jv thisv, newt;
    uint32_t args;                    /* the arguments object, made at its first use */
    uint32_t handlers;                /* the handler stack's height at entry */
    uint32_t gen;                     /* a generator's or an async run's object, 0 for a plain frame */
} js_frame;
#define JFR_CONSTRUCT 1               /* a [[Construct]]: the result is this unless an object is returned */
#define JFR_NATIVE_ENTRY 2            /* a native's call: return to C when it returns */

typedef struct js_handler { uint32_t frame, pc, sp, env; } js_handler;

/* a direct eval's site, written as its caller compiles and read as its code does: the
   bindings the code sees (first..first+n in vm->eval_binds: each one's atom, kind (the
   compiler's), environment depth from the site's and cell), whether the caller is strict
   and whether the site stands in a function (a sloppy eval's vars go to that function) */
/* pkind: a private name's PV_ flags plus one (0: no private name), pcls its class's name -- for the eval's code to
   name the private names of the classes around its site, as V8's does */
typedef struct js_evalbind { uint32_t atom, kind, depth, slot, pkind, pcls; } js_evalbind;
typedef struct js_evalsite { uint32_t first, n, flags; } js_evalsite;
/* an eval's code compiled: by its mode, its site and its source (a string), the function */
typedef struct js_evalcache { uint32_t mode, site, src, func; } js_evalcache;
#define JS_EVAL_CACHE_MAX 256
#define JS_EVAL_STRICT 1
#define JS_EVAL_INFN 2

/* a module (src/js/module.c, ECMA-262 16.2.1): its URL (a string, its key), its code (a
   function whose environment, made when the module is linked, holds its top-level
   bindings in nenv cells), its requests, imports, exports and cells made at linking
   (spans of the tables below), its namespace and import.meta (made at first need), and
   its linking's and evaluation's state: status (JM_*), flags (JMF_*), the evaluation's
   error, the depth-first indexes, its pending async dependencies, its async evaluation's
   order, its cycle's root, its top-level promise (0 none) and its async parents (an array) */
typedef struct js_module {
    uint32_t url, func, env, nenv, status, flags;
    uint32_t req, nreq, imp, nimp, exp, nexp, init, ninit;
    uint32_t ns, meta;
    jv error;
    uint32_t dfs, anc, pending, order, cycle_root, cap, parents;
} js_module;
enum { JM_NEW, JM_LINKING, JM_LINKED, JM_EVALUATING, JM_EVALUATING_ASYNC, JM_EVALUATED };
#define JMF_TLA 1                     /* its code awaits at its top level */
#define JMF_ASYNC_EVAL 2              /* its evaluation is (or was) asynchronous */
#define JMF_ERROR 4                   /* its evaluation threw: error holds what */
#define JMF_REQUESTED 8               /* its requests loaded */
/* a request: the specifier as written (a string), the module it names (0: not loaded) */
typedef struct js_modreq { uint32_t spec, module; } js_modreq;
/* an import: its request, the name imported (an atom; 0: the namespace), the binding
   entry its local name reads (vm->mod_binds) or, for the namespace, the module's cell */
typedef struct js_modimp { uint32_t req, name, bind, cell; } js_modimp;
/* an export: a local one (its name, the module's cell), an indirect one (its name, the
   request, the name imported there: 0 for the namespace), a star one (the request) */
typedef struct js_modexp { uint32_t kind, name, cell, req, import; } js_modexp;
enum { JX_LOCAL, JX_INDIRECT, JX_STAR };
/* a cell made when its module is linked: a var's (undefined), a let's, const's or
   class's (not yet initialized), a hoisted function's (the closure of func) */
typedef struct js_modinit { uint32_t cell, kind, func; } js_modinit;
enum { JMI_VAR, JMI_LEXICAL, JMI_FUNC };
/* an import's binding, resolved when its module is linked: the module and the cell it
   reads (module 0: cell is the namespace object) */
typedef struct js_modbind { uint32_t module, cell; } js_modbind;

/* a timer, in virtual time */
/* run: the script that set it (vm->cur_run then), rec its record (src/script/script.c) */
typedef struct js_timer { uint32_t id; double due, interval; jv fn; uint32_t args, nargs, live, seq, run, rec, fired; } js_timer;

/* a BigInt: its sign, its magnitude's 32-bit limbs (least first, none for zero) a span of vm->bigl */
typedef struct js_big { uint32_t neg, off, n; } js_big;

/* a symbol: its description (a string, 0 for none); reg, its key in Symbol.for's registry plus one */
typedef struct js_sym { uint32_t desc, reg; } js_sym;
/* a private name's symbol (#x, made each time its class is): its reg; no reflection lists it, no key reaches it */
#define JS_SYM_PRIVATE 0xFFFFFFFFu
/* the well-known symbols, made first, in this order */
enum { JSYM_NONE, JSYM_ITERATOR, JSYM_ASYNC_ITERATOR, JSYM_HAS_INSTANCE, JSYM_IS_CONCAT_SPREADABLE, JSYM_MATCH, JSYM_MATCH_ALL,
       JSYM_REPLACE, JSYM_SEARCH, JSYM_SPECIES, JSYM_SPLIT, JSYM_TO_PRIMITIVE, JSYM_TO_STRING_TAG, JSYM_UNSCOPABLES, JSYM_N };
#define JS_KEY_ITERATOR (JS_SYM_KEY | JSYM_ITERATOR)
#define JS_KEY_ASYNC_ITERATOR (JS_SYM_KEY | JSYM_ASYNC_ITERATOR)

/* a microtask: a promise's reaction (fn the handler or undefined, arg the value, target
   the derived promise, mode 0 fulfilled 1 rejected); an async run resumed (target the
   run, arg the value, mode); a thenable's then called (target the promise, arg the
   thenable, fn its then); a queued function (fn). run: the page's script that queued
   it (vm->cur_run then), whose code the job's is */
typedef struct js_job { uint32_t kind, mode, run; jv fn, arg, target; } js_job;
enum { JOB_REACTION, JOB_RESUME, JOB_THENABLE, JOB_CALL };

/* a regular expression compiled (src/js/regexp.c): its program's place in vm->re_code,
   its groups (the whole match's the first), its flags' bits, its source (an atom), its
   groups' names (an array of strings by group, 0 for none), the character every match
   begins with (plus 1, 0 for none), its loops' registers, whether it matches only at
   the input's start; and the characters a match can begin with, when they are known
   (filter): the ASCII ones a bitmap, high when any beyond ASCII can; the literal every
   match begins with when it is two characters or more (npre of them at pre in
   re_code, then its 256 Horspool skips); lead, the place of a leading greedy loop
   whose run a failed try may be passed over by (plus 1) */
typedef struct js_regex {
    uint32_t code, len, ngroups, flags, source, names, first, nregs, anchored;
    uint32_t filter, fb0, fb1, fb2, fb3, high;
    uint32_t pre, npre, lead;
} js_regex;

/* a console message, a script's error, an event listener: what the page's facts say */
typedef struct js_msg { uint32_t level, text; } js_msg;
/* a listener: its target (a node's index, or JS_WINDOW), its event type (an atom), its
   function; attr when an on* attribute's handler; run and rec, its script and its record */
typedef struct js_listener { uint32_t target, type, fn, capture, once, live, attr, run, rec; } js_listener;
#define JS_WINDOW 0xFFFFFFFFu

#define JS_STACK_MAX (1u << 20)       /* values: a call deeper than this is a RangeError */
#define JS_FRAMES_MAX 10000
#define JS_STACK_START (1u << 16)     /* the stack's first size; it grows by doubling (js_room) */
/* the native-to-script calls nested at most (a sort's comparator sorting, a listener
   that clicks): each is a C frame of the interpreter's, so they stop well within the
   C stack */
#define JS_DEPTH_MAX 400u
/* the values a frame may push past its slots: the compiler bounds an expression's
   nesting (JS_NEST_MAX, 400 levels) and a call's or an array's values on the stack (64), so a frame's
   operands stay under this */
#define JS_STACK_MARGIN 16384u

/* the page the VM runs in, while a page's script runs (src/script/script.c): its stores */
struct js_host;

/* the VM. Its owner drops its vectors (JS_VM_DROP): a struct's are not autodropped */
typedef struct js_vm {
    vector<js_obj> objs;
    vector<uint64_t> slots;                 /* every object's slots and every array's elements */
    vector<js_shape> shapes;
    vector<uint32_t> trans;           /* the transitions' hash table: shape ids */
    vector<uint32_t> roots;           /* each prototype's root shape: pairs (proto, shape) in a hash table */
    vector<uint16_t> units;           /* every string's code units */
    vector<js_str> strs;
    vector<uint32_t> atom_slots;      /* the atoms' hash table: string ids */
    vector<js_func> funcs;
    vector<uint8_t> code;
    vector<uint64_t> consts;                /* the code's constants: numbers and strings */
    vector<js_ic> ics;
    vector<char> src;                 /* every compiled source's text (UTF-8), appended as it is compiled: the
                                         functions' own text (toString) and the error sites' expressions */
    vector<js_site> sites;            /* the code's error sites, by their pc */
    vector<uint64_t> stack;
    vector<js_frame> frames;
    vector<js_handler> handlers;
    vector<js_timer> timers;
    vector<js_msg> msgs;
    vector<js_listener> listeners;
    vector<js_sym> syms;              /* the symbols: index 0 none, then the well-known ones */
    vector<js_job> jobs;              /* the microtasks, run in order from jobs_head */
    vector<uint32_t> rejected;        /* the promises rejected with no handler yet, each with its run (pairs), checked at each checkpoint */
    uint32_t in_jobs;                 /* a checkpoint running: another waits for it */
    uint32_t has_instance;            /* a Symbol.hasInstance key was ever given a property: instanceof looks for it */
    vector<js_regex> regexes;         /* the regular expressions compiled, each once for its source and flags */
    vector<uint32_t> re_code;         /* their programs */
    vector<uint32_t> re_hash;         /* (source, flags) -> regex, open addressing */
    vector<uint32_t> re_stack;        /* the matcher's backtrack stack */
    vector<uint32_t> re_caps;         /* the last match's slots: two a group */
    vector<uint32_t> re_regs;         /* the matcher's loop registers */
    vector<uint32_t> lookup;          /* a (shape, atom) -> slot cache for the slow path */
    vector<char> out;                 /* scratch text: what a native writes */
    uint32_t global, global_lex;      /* the global object; the global lexical bindings */
    uint32_t object_proto, function_proto, array_proto, string_proto, number_proto, boolean_proto, error_proto;
    uint32_t type_error_proto, range_error_proto, syntax_error_proto, reference_error_proto, date_proto, regexp_proto;
    uint32_t map_proto, set_proto, iterator_proto;
    uint32_t symbol_proto, generator_proto, promise_proto, promise_ctor, aggregate_error_proto;
    uint32_t async_iterator_proto, async_generator_proto;
    vector<js_evalbind> eval_binds;   /* the direct evals' sites' bindings */
    vector<js_evalsite> eval_sites;
    uint32_t eval_site;               /* the site a direct eval's code is compiled for */
    uint32_t eval_fn;                 /* eval itself: a call of it by its name is a direct eval */
    vector<js_evalcache> eval_cache;  /* the evals' code compiled, newest last: the same code at the same site compiled once */
    vector<js_module> modules;        /* the modules, by their index (0 none) */
    vector<js_modreq> mod_reqs;
    vector<js_modimp> mod_imps;
    vector<js_modexp> mod_exps;
    vector<js_modinit> mod_inits;
    vector<js_modbind> mod_binds;
    uint32_t mod_compiling;           /* the module js_compile reads (JS_CC_MODULE) */
    uint32_t mod_order;               /* the async evaluations' count */
    uint32_t mod_first;               /* the modules' first native id (src/js/module.c) */
    /* the collector (src/js/gc.c): the free lists' heads (an object's and a string's index;
       a BigInt's plus one), the collections made, the heap's size after the last */
    uint32_t obj_free, str_free, big_free, gc_count;
    uint64_t gc_last;                 /* the live heap's size after the last collection, in eight bytes */
    uint64_t gc_allocs;               /* the records made since (objects, strings, BigInts) */
    uint64_t gc_slots, gc_units;      /* the slots' and the units' lengths after it */
    uint32_t gc_enabled;              /* collections between tasks (js_gc_maybe): the host's to allow (ambitious js's, a page's) */
    /* collections inside a running script, at its loops' turns (js_gc_live): allowed at this depth of calls into the
       engine (vm->depth) alone, the host's own call, where no C code below the script holds a value the collector
       cannot see (0: never; the host sets it); and the allocations past which a turn asks whether one is due */
    uint32_t gc_live_depth;
    uint64_t gc_due;
    uint32_t base_url;                /* the classic scripts' base for import(): a string (0: the current directory) */
    /* the host's module loader: the specifier (a string) resolved against the referrer
       (a module's URL; 0 for a classic script's import(), whose base is base_url), its
       URL and its source (strings); 0 when it cannot, the message in *source. With none,
       files are read */
    int (*load_module)(js_vm *vm, uint32_t spec, uint32_t referrer, uint32_t *url, uint32_t *source);
    uint32_t mod_resolve_only;        /* the loader is asked for the URL alone (import.meta.resolve): no fetch */
    uint32_t weakmap_proto, weakset_proto, weakref_proto, finreg_proto;
    uint32_t arraybuffer_proto, typedarray_proto, dataview_proto, textenc_proto, textdec_proto;
    uint32_t ta_protos[JTA_N];        /* each typed array's prototype, by its element type */
    vector<uint8_t> bytes;            /* every ArrayBuffer's bytes (src/js/typed.c) */
    vector<js_big> bigs;              /* every BigInt (src/js/bigint.c) */
    vector<uint32_t> bigl;            /* their limbs */
    uint32_t bigint_proto;
    uint32_t intl_protos[16];         /* each Intl constructor's prototype, by its kind (src/js/intl.c) */
    uint32_t intl_cache;              /* the formatters toLocaleString made for its defaults (an array, 0 none yet) */
    uint32_t kept;                    /* the targets WeakRefs were made of or gave since the microtask checkpoint (ECMA-262
                                         AddToKeptObjects): an array, a root, let go at the checkpoint's end (0 none) */
    uint32_t proxy_first;             /* Proxy's first native id (src/js/proxy.c) */
    uint32_t re_first;                /* RegExp's first native id, and %RegExp% (src/js/regexp.c) */
    uint32_t regexp_ctor;
    /* the regexp protector (src/js/regexp.c, re_plain): RegExp.prototype's, RegExp's and a plain RegExp's shapes as the
       engine made them, and for each property the methods read (exec first), its holder, its slot, the value the
       engine put there and, for an accessor, its getter (JV_HOLE for none): roots of the collector, so that no other
       object takes their numbers */
    uint32_t re_proto_shape, re_ctor_shape, re_root_shape, re_guard_n;
    uint32_t re_guard_obj[20], re_guard_slot[20];
    jv re_guard_val[20], re_guard_get[20];
    uint32_t jobs_head;               /* the next microtask to run */
    js_job job_running;               /* the microtask running, whose target only it may hold: a root of the collector */
    uint32_t error_ctor, type_error_ctor, range_error_ctor, syntax_error_ctor, reference_error_ctor, eval_error_proto;
    uint32_t throw_te;                /* %ThrowTypeError%: an unmapped arguments object's callee, both its get and its set */
    uint32_t uri_error_proto;         /* URIError.prototype (the URI functions' errors) */
    uint32_t array_values;            /* %Array.prototype.values%: an arguments object's Symbol.iterator */
    /* the iterators' prototypes, each its kind's next and Symbol.toStringTag: arrays', maps', sets', strings',
       matchAll's */
    uint32_t array_iter_proto, map_iter_proto, set_iter_proto, string_iter_proto, regexp_iter_proto;
    uint32_t helper_proto, wrap_proto, iterator_ctor;   /* %IteratorHelperPrototype%, %WrapForValidIteratorPrototype%, %Iterator% */
    /* the promise protector (src/js/builtins.c): Promise.prototype's and Promise's shapes as the engine made them, the
       slots of the constructor and of Symbol.species, the species accessor and its getter, a plain promise's shape */
    uint32_t pr_proto_shape, pr_ctor_shape, pr_ctor_slot, pr_species_slot, pr_species_acc, pr_species_get, pr_root_shape;
    /* the host's roots: every value the host holds outside the VM's tables (a page's
       prototypes, its records' objects and values: src/script/script.c) appended to out,
       each marked by the collector as a root (src/js/gc.c); 0 with no host */
    void (*host_roots)(js_vm *vm, vector<jv> *out);
    jv exception;                     /* the thrown value, while one is in flight */
    int throwing;
    /* why an array's [[Set]] or [[Delete]] last refused (JSR_*), the key it names, for strict
       code's TypeError (src/js/vm.c) */
    uint32_t refused, refused_key;
    uint64_t steps, step_budget;      /* the bytecodes run; past the budget, a script is stopped */
    double now;                       /* virtual time, ms */
    double real_clock;                /* set: performance.now() reads the real clock (benchmarks) */
    uint32_t timer_seq, timer_id, depth;
    uint64_t heap_cap;                /* the slots and units past which a script ends with a RangeError */
    uint32_t rng;
    uint32_t script;                  /* the script running: its number, for errors */
    uint32_t cur_run;                 /* the page's script whose code runs now: its record (src/script/) */
    uint32_t host_kinds;
    uint32_t natoms, nroots, ntrans;
    uint32_t digit_strs[10];
    uint32_t int_strs[1024];          /* the strings of 0 .. 1023, each made at its first use */
    uint32_t sp, nframes, nhandlers;
    int echo;                         /* the console written to stdout as well (ambitious js) */
    struct js_host *host;             /* the page, while one of its scripts runs; 0 with no page */
    vector<uint32_t> wrappers;        /* a DOM node's index -> its wrapper object (src/script/script.c) */
    vector<uint32_t> hatoms;          /* the host's names, interned once (src/script/script.c) */
    uint32_t root_proto, root_shape, root_valid;   /* the last prototype's root shape */
} js_vm;

#define JS_VM_DROP(v) vector_js_obj_drop(&(v).objs); vector_uint64_t_drop(&(v).slots); vector_js_shape_drop(&(v).shapes); \
    vector_uint32_t_drop(&(v).trans); vector_uint32_t_drop(&(v).roots); vector_uint16_t_drop(&(v).units); vector_js_str_drop(&(v).strs); \
    vector_uint32_t_drop(&(v).atom_slots); vector_js_func_drop(&(v).funcs); vector_uint8_t_drop(&(v).code); vector_uint64_t_drop(&(v).consts); \
    vector_js_ic_drop(&(v).ics); vector_uint64_t_drop(&(v).stack); vector_js_frame_drop(&(v).frames); \
    vector_js_handler_drop(&(v).handlers); vector_js_timer_drop(&(v).timers); vector_js_msg_drop(&(v).msgs); \
    vector_js_listener_drop(&(v).listeners); vector_uint32_t_drop(&(v).lookup); vector_char_drop(&(v).out); \
    vector_uint32_t_drop(&(v).wrappers); vector_uint32_t_drop(&(v).hatoms); vector_js_sym_drop(&(v).syms); \
    vector_js_job_drop(&(v).jobs); vector_uint32_t_drop(&(v).rejected); vector_js_regex_drop(&(v).regexes); \
    vector_uint32_t_drop(&(v).re_code); vector_uint32_t_drop(&(v).re_hash); vector_uint32_t_drop(&(v).re_stack); \
    vector_uint32_t_drop(&(v).re_caps); vector_uint32_t_drop(&(v).re_regs); vector_uint8_t_drop(&(v).bytes); \
    vector_js_big_drop(&(v).bigs); vector_uint32_t_drop(&(v).bigl); vector_js_evalbind_drop(&(v).eval_binds); \
    vector_js_evalsite_drop(&(v).eval_sites); vector_js_evalcache_drop(&(v).eval_cache); \
    vector_js_module_drop(&(v).modules); vector_js_modreq_drop(&(v).mod_reqs); vector_js_modimp_drop(&(v).mod_imps); \
    vector_js_modexp_drop(&(v).mod_exps); vector_js_modinit_drop(&(v).mod_inits); vector_js_modbind_drop(&(v).mod_binds); \
    vector_char_drop(&(v).src); vector_js_site_drop(&(v).sites)

/* ---- the atoms every part names (src/js/vm.c makes them first, in this order) ---- */
enum {
    A_NONE, A_LENGTH, A_PROTOTYPE, A_CONSTRUCTOR, A_TOSTRING, A_VALUEOF, A_NAME, A_MESSAGE, A_STACK, A_CALLEE,
    A_GET, A_SET, A_VALUE, A_WRITABLE, A_ENUMERABLE, A_CONFIGURABLE, A_UNDEFINED, A_NULL, A_TRUE, A_FALSE,
    A_OBJECT, A_FUNCTION, A_NUMBER, A_STRING, A_BOOLEAN, A_SYMBOL, A_EMPTY, A_DEFAULT, A_DONE, A_NEXT,
    A_THEN, A_TOJSON, A_INDEX, A_INPUT, A_ARGUMENTS, A_CAUSE, A_LASTINDEX, A_SIZE,
    A_RETURN, A_THROW, A_DESCRIPTION, A_STATUS, A_REASON, A_FULFILLED, A_REJECTED, A_ERRORS, A_BIGINT, A_CALLER,
    A_NATOMS
};

/* ---- the operations the parts share ---- */

/* the VM: made, its prototypes and globals built (src/js/builtins.c), and its limits */
void js_init(js_vm *vm);
void js_setup(js_vm *vm);                 /* js_init's work on a VM already zeroed: its host kept */
/* a script compiled and run as a classic script: 0 done, -1 a syntax error, -2 an
   uncaught exception, -3 the step budget spent. Its error's text goes to vm->out */
int js_eval(js_vm *vm, const char *src, size_t n, const char *name, uint32_t script);
/* a value called from C: its result, or JV_HOLE with vm->exception set. The arguments
   are argc values of a C array; a native's own arguments are argc values of the stack
   from index args (no pointer into the stack is ever held) */
jv js_call(js_vm *vm, jv fn, jv thisv, const jv *argv, uint32_t argc);
jv js_construct(js_vm *vm, jv fn, const jv *argv, uint32_t argc, jv newt);
jv js_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i);

/* symbols: a new one of a description (a string, 0 for none); a key's printable name */
uint32_t js_sym_new(js_vm *vm, uint32_t desc);
uint32_t js_key_name(js_vm *vm, uint32_t key);       /* a string: the atom, or "Symbol(desc)" */
uint32_t js_sym_text(js_vm *vm, uint32_t sym);       /* "Symbol(desc)" */
/* a value as a property key: an atom or a symbol's key; 0 when its conversion threw */
uint32_t js_prop_key(js_vm *vm, jv v, int *ok);
void js_own_symbols(js_vm *vm, uint32_t o, uint32_t arr);   /* the own symbol keys, pushed as symbols */

/* generators and async runs (src/js/vm.c): one resumed with a value, as next (mode 0),
   throw (1) or return (2): what it yields or returns, *done set when it returned */
jv js_gen_resume(js_vm *vm, uint32_t g, jv value, int mode, int *done);
/* {value, done}: an iterator's result */
jv js_iter_result(js_vm *vm, jv v, int done);
/* eval's code compiled (js_compile's mode) and run with the scope env, this and home
   object given: a string's completion value; any other value as it is; a SyntaxError
   thrown when it does not compile (src/js/vm.c) */
jv js_eval_code(js_vm *vm, jv code, int mode, uint32_t env, jv thisv, uint32_t home);
/* async generators (src/js/builtins.c, ECMA-262 27.6): the first request answered (its
   promise rejected with v, or resolved with {v, done}); the requests left once the body
   is done answered; an async iterator's step as for await and yield* take it: next (kind
   0 with arg, 3 without), throw (1) or return (2 with arg, 4 without) called on a
   JI_ASYNC or JI_FROM_SYNC record, the result (a promise from a JI_FROM_SYNC one) in *out;
   0 when the method is missing, -1 when it threw */
void js_ag_complete(js_vm *vm, uint32_t g, int rejected, jv v, int done);
void js_ag_drain(js_vm *vm, uint32_t g);
int js_aiter_call(js_vm *vm, uint32_t rec, int kind, jv arg, jv *out);
/* an async iterator record for a value: its Symbol.asyncIterator's, else its sync iterator's (src/js/vm.c) */
jv js_aiter_of(js_vm *vm, jv v);
/* an iterable's iterator (for-of's), and its next value: 0 when done */
jv js_iter_of(js_vm *vm, jv v);
int js_iter_step(js_vm *vm, uint32_t it, jv *out);
int js_helper_step(js_vm *vm, uint32_t it, jv *out);   /* an Iterator Helper's next value (src/js/builtins.c): 1, 0 done */
void js_iter_close(js_vm *vm, uint32_t it);
/* a sync iterator sent next (mode 0), throw (1) or return (2): its result's value and
   done; 1 sent, 0 no such method, -1 it threw */
int js_iter_send(js_vm *vm, uint32_t it, int mode, jv arg, int has_arg, jv *value, int *done);

/* promises (src/js/builtins.c): a new pending one; one settled; resolved (a thenable
   followed); a promise for any value; a reaction added (an async run's when target
   is one); the microtasks run until none is left: a checkpoint, which the host makes
   after each script, listener and timer, and which does nothing while script code is
   on the stack (a listener a script's click() ran) or a checkpoint is running. A
   rejection no handler took by the checkpoint's end is reported then, once */
uint32_t js_promise_new(js_vm *vm);
void js_promise_settle(js_vm *vm, uint32_t p, int rejected, jv v);
void js_promise_resolve_with(js_vm *vm, uint32_t p, jv v);
uint32_t js_promise_of(js_vm *vm, jv v);
void js_promise_react(js_vm *vm, uint32_t p, jv on_ok, jv on_err, jv target, int resume);
void js_job_push(js_vm *vm, uint32_t kind, uint32_t mode, jv fn, jv arg, jv target);
void js_run_jobs(js_vm *vm);
/* the phase an error a microtask threw is reported in (the script module's promise phase) */
#define JS_PHASE_PROMISE 3u

/* regular expressions (src/js/regexp.c): a pattern compiled (its flags' bits), 0 with a
   SyntaxError thrown; a literal's checked as the compiler reads it (0 well formed, its
   regex in *re; else the error's text); a regex run on a string from an index (1 a match,
   its slots in vm->re_caps; 0 none; -1 the step budget spent); a literal's object made;
   the natives' cases, after the first's id */
enum { JRE_CTOR, JRE_EXEC, JRE_TEST, JRE_TOSTRING, JRE_SOURCE, JRE_FLAGS, JRE_GLOBAL, JRE_IGNORECASE, JRE_MULTILINE,
       JRE_DOTALL, JRE_UNICODE, JRE_UNICODESETS, JRE_STICKY, JRE_HASINDICES, JRE_MATCH, JRE_MATCHALL, JRE_REPLACE,
       JRE_SEARCH, JRE_SPLIT, JRE_SP_MATCH, JRE_SP_MATCHALL, JRE_SP_REPLACE, JRE_SP_REPLACEALL, JRE_SP_SEARCH,
       JRE_SP_SPLIT, JRE_COMPILE, JRE_N };
uint32_t js_re_compile(js_vm *vm, uint32_t src, uint32_t flags);
int js_re_check(js_vm *vm, uint32_t src, uint32_t flags_str, uint32_t *re, vector<char> *err);
jv js_re_object(js_vm *vm, uint32_t re);
int js_re_exec(js_vm *vm, uint32_t re, uint32_t s, uint32_t start);
jv js_re_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_re_guard(js_vm *vm);                                          /* the regexp protector, once every built-in is made */
int js_re_iter_step(js_vm *vm, uint32_t it, jv *out);                 /* a matchAll iterator's next match */
int js_is_regexp(js_vm *vm, jv v);                                    /* IsRegExp: 1, 0; -1 thrown */
/* String.prototype.replace (all: replaceAll) and split (sym JSYM_REPLACE, JSYM_SPLIT) by the pattern's own Symbol
   method: 1 done (*out), 0 none, -1 thrown */
int js_re_string_method(js_vm *vm, uint32_t sym, int all, jv thisv, jv a0, jv a1, jv *out);
void js_re_init(js_vm *vm, uint32_t first);
/* HTML's pattern attribute: whether a value matches a pattern, compiled as ^(?:pattern)$ with
   the v flag in a machine of its own: 1 it does, 0 it does not, -1 no regular expression */
int js_pattern_test(const char *p, size_t pn, const char *v, size_t vn);
/* proxies (src/js/proxy.c): a proxy's internal methods, each its handler's trap or its
   target's own; Proxy's, Reflect's and the weak collections' natives, after the first's id */
jv js_proxy_get(js_vm *vm, uint32_t p, uint32_t key, jv receiver);
int js_proxy_set(js_vm *vm, uint32_t p, uint32_t key, jv v, jv receiver);
int js_proxy_has(js_vm *vm, uint32_t p, uint32_t key);
int js_proxy_delete(js_vm *vm, uint32_t p, uint32_t key);
int js_proxy_get_own(js_vm *vm, uint32_t p, uint32_t key, jv *v, uint32_t *flags);
void js_proxy_own_keys(js_vm *vm, uint32_t p, int strings, int symbols, int enumerable_only, uint32_t arr);
jv js_proxy_get_proto(js_vm *vm, uint32_t p);
jv js_proxy_call(js_vm *vm, uint32_t p, jv thisv, uint32_t args, uint32_t argc, jv newt);
enum { JPX_PROXY, JPX_REVOCABLE, JPX_REVOKE,
       JPX_R_APPLY, JPX_R_CONSTRUCT, JPX_R_DEFINE, JPX_R_DELETE, JPX_R_GET, JPX_R_GETOWNDESC, JPX_R_GETPROTO, JPX_R_HAS,
       JPX_R_ISEXTENSIBLE, JPX_R_OWNKEYS, JPX_R_PREVENTEXT, JPX_R_SET, JPX_R_SETPROTO,
       JPX_WEAKMAP, JPX_WEAKSET, JPX_WM_GET, JPX_WM_SET, JPX_WM_HAS, JPX_WM_DELETE, JPX_WS_ADD, JPX_WS_HAS, JPX_WS_DELETE,
       JPX_WEAKREF, JPX_WR_DEREF, JPX_FINREG, JPX_FR_REGISTER, JPX_FR_UNREGISTER,
       /* Object's functions over the descriptors (builtins.c's Object passes them here) */
       JPX_OBJ_DEFPROP, JPX_OBJ_DEFPROPS, JPX_OBJ_GETOWNDESC, JPX_OBJ_PREVENTEXT, JPX_OBJ_SEAL, JPX_OBJ_ISSEALED, JPX_OBJ_ISFROZEN,
       JPX_N };
jv js_proxy_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_proxy_init(js_vm *vm, uint32_t first);
/* typed arrays (src/js/typed.c): an element read and written (0: the conversion threw; an
   index past the end ignored), the length; the natives of ArrayBuffer, the typed arrays,
   DataView, TextEncoder and TextDecoder, after the first's id */
jv js_ta_get(js_vm *vm, uint32_t o, uint32_t i);
int js_ta_set(js_vm *vm, uint32_t o, uint32_t i, jv v);
uint32_t js_ta_length(js_vm *vm, uint32_t o);
enum { JTN_AB, JTN_AB_ISVIEW, JTN_AB_BYTELENGTH, JTN_AB_SLICE, JTN_AB_RESIZABLE, JTN_AB_MAXLENGTH,
       JTN_CTOR, JTN_FROM, JTN_OF, JTN_BUFFER, JTN_BYTELENGTH, JTN_BYTEOFFSET, JTN_LENGTH, JTN_TAG,
       JTN_SET, JTN_SUBARRAY, JTN_SLICE, JTN_FILL, JTN_MAP, JTN_FILTER, JTN_SORT, JTN_TOSORTED, JTN_REVERSE,
       JTN_TOREVERSED, JTN_WITH, JTN_COPYWITHIN,
       JTN_DV, JTN_DV_BUFFER, JTN_DV_BYTELENGTH, JTN_DV_BYTEOFFSET, JTN_DV_GET, JTN_DV_SET,
       JTN_TE, JTN_TD, JTN_TE_ENCODING, JTN_TD_ENCODING, JTN_TD_FATAL, JTN_TD_IGNOREBOM, JTN_TE_ENCODE,
       JTN_TE_ENCODEINTO, JTN_TD_DECODE,
       JTN_AB_RESIZE, JTN_AB_TRANSFER, JTN_AB_TRANSFER_FIXED, JTN_AB_DETACHED, JTN_SHARED,
       JTN_AT_ADD, JTN_AT_AND, JTN_AT_CMPXCHG, JTN_AT_EXCHANGE, JTN_AT_ISLOCKFREE, JTN_AT_LOAD, JTN_AT_NOTIFY, JTN_AT_OR,
       JTN_AT_STORE, JTN_AT_SUB, JTN_AT_WAIT, JTN_AT_WAITASYNC, JTN_AT_XOR, JTN_N };
jv js_typed_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_typed_init(js_vm *vm, uint32_t first);
/* the methods the typed arrays share with Array's (builtins.c puts them on %TypedArray%.prototype)
   wrapped: each first refuses a typed array out of its buffer's bounds, as V8 does */
void js_typed_shared(js_vm *vm);

/* BigInts (src/js/bigint.c): made, read, compared, written; an operator's result (op one of
   the bytecode's OP_ADD .. OP_SHR); ToBigInt; the natives, after the first's id */
jv js_big_from_u64(js_vm *vm, uint64_t u, int neg);
jv js_big_from_i64(js_vm *vm, int64_t v);
jv js_big_from_double(js_vm *vm, double d);
double js_big_to_double(js_vm *vm, jv v);
uint64_t js_big_to_u64(js_vm *vm, jv v);
int js_big_is_zero(js_vm *vm, jv v);
int js_big_cmp(js_vm *vm, jv a, jv b);
int js_big_eq(js_vm *vm, jv a, jv b);
int js_big_cmp_num(js_vm *vm, jv a, double d);
uint32_t js_big_hash(js_vm *vm, jv v);
uint32_t js_big_to_string(js_vm *vm, jv v, uint32_t radix);
jv js_big_parse(js_vm *vm, uint32_t s, int literal);
jv js_big_literal(js_vm *vm, const char *text, size_t n);
jv js_big_binop(js_vm *vm, uint32_t op, jv a, jv b);
jv js_big_neg(js_vm *vm, jv a);
jv js_big_not(js_vm *vm, jv a);
jv js_big_inc(js_vm *vm, jv a, int by);
jv js_to_bigint(js_vm *vm, jv v);
enum { JBG_CTOR, JBG_ASINTN, JBG_ASUINTN, JBG_TOSTRING, JBG_TOLOCALE, JBG_VALUEOF, JBG_N };
jv js_big_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_big_init(js_vm *vm, uint32_t first);

/* modules (src/js/module.c): a module's graph loaded (spec resolved against referrer, a
   module or 0 for the classic scripts' base), linked and evaluated, the promise of its
   evaluation returned (0 with an exception thrown); a source run as the main module (its
   URL given); import()'s promise; import.meta; a module's namespace; the natives' cases */
uint32_t js_module_import(js_vm *vm, uint32_t spec, uint32_t referrer);
/* the collector (src/js/gc.c): a collection now, which only a moment between tasks may
   ask for (no C code then holds a value outside the VM's tables and the host's roots);
   one when the heap has doubled since the last; gc() for a test's script (ambitious js
   alone, src/js/builtins.c) */
void js_gc(js_vm *vm);
void js_gc_maybe(js_vm *vm);
void js_gc_live(js_vm *vm);                                           /* a loop's turn: a collection when one is due */
void js_gc_expose(js_vm *vm);
uint32_t js_env_new(js_vm *vm, uint32_t parent, uint32_t n);    /* an environment of n cells (src/js/vm.c) */
uint32_t js_module_main(js_vm *vm, uint32_t url, const char *src, size_t n);
/* a host's module script in two steps, as HTML runs one: its graph fetched (a source made a
   module, a new one when fresh, else the one its URL has; its requests loaded through the
   loader): the module's number, 0 thrown; then linked and evaluated: its evaluation's
   promise, 0 thrown */
uint32_t js_module_fetch(js_vm *vm, uint32_t url, const char *src, size_t n, int fresh);
uint32_t js_module_eval(js_vm *vm, uint32_t m);
/* a timer's record mark for a module's task (import()'s load and evaluation), which is no
   page's timer */
#define JS_TIMER_MODULE 0xFFFFFFFCu
jv js_module_dynamic(js_vm *vm, jv spec, uint32_t referrer);
jv js_module_meta(js_vm *vm, uint32_t m);
jv js_module_namespace(js_vm *vm, uint32_t m);
enum { JMD_NS_GET, JMD_ASYNC_OK, JMD_ASYNC_ERR, JMD_DYN_RUN, JMD_DYN_DONE, JMD_DYN_FAIL, JMD_META_RESOLVE, JMD_N };
jv js_module_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_module_init(js_vm *vm, uint32_t first);
/* Intl (src/js/intl.c, ECMA-402): its natives, JIN_N ids from the first (intl.c numbers
   them), and the locale-sensitive methods of Number, BigInt, String, Date and Array it
   gives their prototypes in place of the plain ones */
#define JIN_N 256
jv js_intl_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);
void js_intl_init(js_vm *vm, uint32_t first);

/* a Map's or a Set's own operations (src/js/builtins.c), which the weak collections use */
enum { JCOLL_GET, JCOLL_SET, JCOLL_HAS, JCOLL_DELETE, JCOLL_ADD };
uint32_t js_coll_new(js_vm *vm, uint32_t kind, uint32_t proto);
jv js_coll_op(js_vm *vm, uint32_t op, uint32_t c, jv k, jv v);

/* Unicode's simple case mappings, and the full ones of more than one code point (their count, into out) */
uint32_t js_uni_upper(uint32_t c);
uint32_t js_uni_lower(uint32_t c);
uint32_t js_uni_special(uint32_t c, int upper, uint32_t *out);
/* the binary properties toLowerCase's final sigma reads (Cased, Case_Ignorable), and an
   identifier's characters beyond ASCII: its first ID_Start, the rest ID_Continue or ZWNJ or ZWJ */
int js_uni_cased(uint32_t c);
int js_uni_case_ignorable(uint32_t c);
int js_uni_id_start(uint32_t c);
int js_uni_id_part(uint32_t c);
/* String.prototype.normalize's work (src/js/normalize.c): s in the form a value names
   (undefined: NFC), JV_HOLE with a RangeError thrown for an unknown one */
jv js_str_normalize(js_vm *vm, uint32_t s, jv form);

/* strings */
uint32_t js_str_new(js_vm *vm, const uint16_t *u, size_t n);
uint32_t js_str_utf8(js_vm *vm, const char *s, size_t n);
uint32_t js_str_ascii(js_vm *vm, const char *s);
void js_str_flatten(js_vm *vm, uint32_t s);
uint32_t js_str_concat(js_vm *vm, uint32_t a, uint32_t b);
uint32_t js_str_sub(js_vm *vm, uint32_t s, uint32_t from, uint32_t to);
int js_str_eq(js_vm *vm, uint32_t a, uint32_t b);
int js_str_cmp(js_vm *vm, uint32_t a, uint32_t b);
uint32_t js_str_hash(js_vm *vm, uint32_t s);
void js_str_to_utf8(js_vm *vm, uint32_t s, vector<char> *out);
uint16_t js_str_at(js_vm *vm, uint32_t s, uint32_t i);
uint32_t js_atom(js_vm *vm, uint32_t s);              /* the atom of a string: its interned string's id */
uint32_t js_atom_ascii(js_vm *vm, const char *s);
int js_atom_index(js_vm *vm, uint32_t atom, uint32_t *index);   /* an array index's atom: "0" .. "4294967294" */

/* objects */
uint32_t js_obj_new(js_vm *vm, uint32_t kind, uint32_t proto);
uint32_t js_array_new(js_vm *vm, uint32_t n);
void js_array_push(js_vm *vm, uint32_t a, jv v);
jv js_array_get(js_vm *vm, uint32_t a, uint32_t i);
void js_array_set(js_vm *vm, uint32_t a, uint32_t i, jv v);
void js_array_set_length(js_vm *vm, uint32_t a, uint32_t n);
jv js_get(js_vm *vm, jv obj, uint32_t atom);                          /* obj[atom], through getters */
int js_set(js_vm *vm, jv obj, uint32_t atom, jv v);                   /* 0: thrown */
jv js_get_value(js_vm *vm, jv obj, jv key);                           /* obj[key] for any key */
int js_set_value(js_vm *vm, jv obj, jv key, jv v);
void js_define(js_vm *vm, uint32_t o, uint32_t atom, jv v, uint32_t flags);   /* an own data property, made or replaced */
void js_define_accessor(js_vm *vm, uint32_t o, uint32_t atom, jv getter, jv setter, uint32_t flags);
int js_has(js_vm *vm, uint32_t o, uint32_t atom);                    /* through the prototypes */
/* a function's own property the engine keeps for it, not its shape (length, name, a sloppy function's arguments and
   caller, a prototype not yet made): 1 when atom is one; its value (the prototype then made, in the shape) */
int js_fn_virtual(js_vm *vm, uint32_t o, uint32_t atom);
int js_fn_lazy(js_vm *vm, uint32_t o, uint32_t atom, jv *out);
int js_get_own(js_vm *vm, uint32_t o, uint32_t atom, jv *v, uint32_t *flags);
/* o's shape's entry for a key: 1 when it has one, its flags in *flags and in *live whether
   it holds a value (a deleted property's entry holds none) */
int js_shape_entry(js_vm *vm, uint32_t o, uint32_t key, uint32_t *flags, int *live);
int js_delete(js_vm *vm, uint32_t o, uint32_t atom);
void js_own_keys(js_vm *vm, uint32_t o, int enumerable_only, uint32_t arr);   /* the own string keys, in order, pushed as strings */
uint32_t js_shape_of(js_vm *vm, uint32_t proto);
uint32_t js_shape_find(js_vm *vm, uint32_t sh, uint32_t atom);          /* the shape's entry for atom, 0 for none */
int js_is_callable(js_vm *vm, jv v);
/* IsConstructor: a closure but an arrow's, a method's, a generator's or an async one's; a
   native with a prototype of its own (or Proxy); a bound or proxied constructor */
int js_is_constructor(js_vm *vm, jv v);
int js_instance_of(js_vm *vm, jv v, jv ctor);

/* conversions */
int js_truthy(js_vm *vm, jv v);
double js_to_number(js_vm *vm, jv v);                                 /* NaN, with vm->throwing, when valueOf throws */
uint32_t js_to_string(js_vm *vm, jv v);                               /* 0 when it throws */
jv js_to_primitive(js_vm *vm, jv v, int hint_string);
jv js_to_object(js_vm *vm, jv v);
/* the object o given the prototype p, its properties kept (src/js/builtins.c) */
void js_set_proto(js_vm *vm, uint32_t o, uint32_t p);
uint32_t js_num_to_str(js_vm *vm, double d);
void js_num_format(double d, char *buf, size_t cap);
double js_str_to_num(js_vm *vm, uint32_t s);
int32_t js_to_int32(double d);
uint32_t js_typeof(js_vm *vm, jv v);                                  /* an atom */
int js_strict_eq(js_vm *vm, jv a, jv b);
int js_loose_eq(js_vm *vm, jv a, jv b);
int js_same_value_zero(js_vm *vm, jv a, jv b);

/* errors: thrown into vm, and JV_HOLE returned for a caller to pass on */
jv js_throw(js_vm *vm, jv v);
jv js_throw_msg(js_vm *vm, uint32_t proto, const char *msg);
jv js_type_error(js_vm *vm, const char *what, uint32_t name_str);     /* "<name> <what>", name a string or 0 */
uint32_t js_error_new(js_vm *vm, uint32_t proto, uint32_t msg);
void js_error_text(js_vm *vm, jv e, vector<char> *out);              /* "TypeError: x is not a function" */
/* a value as V8 names it where no expression can be (its call printer's default): its
   type, then a string's text in double quotes, null, a boolean or a number: number 5,
   string "abc", object null, object, undefined */
void js_value_name(js_vm *vm, jv v, vector<char> *out);
/* a value's text without its methods run (V8's NoSideEffectsToString): a primitive's
   string, a function's source, "Error: x", "#<Object>", "[object Array]" */
void js_value_text(js_vm *vm, jv v, vector<char> *out);
/* a mapped arguments object's element i: its parameter's cell in vm->slots, plus one (0 not mapped); unmapped, its
   element keeping the parameter's value; every element unmapped (src/js/vm.c) */
uint32_t js_args_cell(js_vm *vm, uint32_t o, uint32_t i);
void js_args_unmap(js_vm *vm, uint32_t o, uint32_t i);
void js_args_unmap_all(js_vm *vm, uint32_t o);
/* Function.prototype.toString of a callable object: its source text, or V8's native
   form ("function push() { [native code] }"); 0 when o is not callable */
uint32_t js_func_source(js_vm *vm, uint32_t o);
jv js_not_function(js_vm *vm, jv v);                                  /* "number 5 is not a function" */
jv js_not_constructor(js_vm *vm, jv v);
/* a native that calls its receiver (Function.prototype.call, apply) given no function: V8
   names the script's call of the native ("f.call is not a function"), else the value */
jv js_caller_not_function(js_vm *vm, jv v);
/* why an array's write was refused (vm->refused), for strict code's TypeError */
enum { JSR_NONE, JSR_READONLY, JSR_GETTER, JSR_NOT_EXTENSIBLE, JSR_LENGTH, JSR_DELETE };
/* the standard's throwing operations, as the natives write through them: Set(O, P, V,
   true), DeletePropertyOrThrow, CreateDataPropertyOrThrow (the last src/js/proxy.c's):
   1 done, 0 with a TypeError thrown in V8's words */
int js_set_throw(js_vm *vm, jv obj, jv key, jv v);
int js_delete_throw(js_vm *vm, jv obj, jv key);
int js_create_data_throw(js_vm *vm, jv obj, jv key, jv v);
int js_create_data(js_vm *vm, jv obj, jv key, jv v);
jv js_ordinary_to_primitive(js_vm *vm, jv v, int string_first);       /* OrdinaryToPrimitive: toString first, or valueOf */
int js_set_proto_of(js_vm *vm, uint32_t o, jv np);                     /* [[SetPrototypeOf]]: 1, 0 refused, -1 thrown */                 /* CreateDataProperty: 1, 0 refused, -1 thrown */
/* an array's length set as [[Set]] sets it (elements past it deleted, a non-configurable
   one stopping it): 1, 2 refused (vm->refused says why), 0 thrown */
int js_array_set_length_checked(js_vm *vm, uint32_t a, double n);
/* an error site's expression as V8's call printer writes it (src/js/compile.c): its text
   read again and printed, a call's own callee alone when callee_only; 0 when it cannot
   be read (the error names the value instead) */
int js_site_text(js_vm *vm, js_site s, int callee_only, vector<char> *out);

/* functions made by natives and the host */
uint32_t js_native_new(js_vm *vm, uint32_t id, uint32_t name, uint32_t nargs);
uint32_t js_closure_new(js_vm *vm, uint32_t func, uint32_t env, jv thisv);
jv js_native_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);   /* src/js/builtins.c */
void js_builtins_init(js_vm *vm);

/* the console and the host's hooks (src/script/script.c) */
void js_console(js_vm *vm, uint32_t level, uint32_t text);
/* an error no code caught, thrown by the code of vm->cur_run in the phase given (a
   microtask's: JS_PHASE_PROMISE): the page's record, or stderr with no page */
void js_report_error(js_vm *vm, jv e, uint32_t phase);
jv js_host_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled);
int js_host_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled);
jv js_host_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled);

/* the property caches' paths (src/js/vm.c) */
jv js_get_ic(js_vm *vm, jv obj, uint32_t atom, uint32_t ic);
int js_set_ic(js_vm *vm, jv obj, uint32_t atom, jv v, uint32_t ic);

/* the compiler (src/js/compile.c): a script's text into a function of no parameters */
/* what js_compile reads (its mode): a script, an event handler's body (a function of
   event), an eval's code in the global scope (indirect eval: its completion value
   returned), the Function constructor's source (one function expression, checked to be
   no more), an eval's code in its caller's scopes (direct eval, the site vm->eval_site) */
enum { JS_CC_SCRIPT, JS_CC_HANDLER, JS_CC_EVAL, JS_CC_FUNCTION, JS_CC_DIRECT, JS_CC_MODULE };
int js_compile(js_vm *vm, const char *src, size_t n, uint32_t script, int mode, uint32_t *func, vector<char> *err);
/* the interpreter (src/js/vm.c) */
jv js_run(js_vm *vm, uint32_t entry_frames);


/* ---- the bytecode (src/js/compile.c writes it, src/js/vm.c runs it) ----
   An opcode is a byte; its operands follow, little-endian: u8, u16, u32, or i32 for a
   jump (relative to the opcode's end). A call's stack is this, the callee, then the
   arguments; its result takes the place of all three. */
enum {
    OP_UNDEF, OP_NULL, OP_TRUE, OP_FALSE, OP_HOLE,
    OP_CONST,        /* u32: vm->consts[i] */
    OP_INT,          /* i32 */
    OP_STR,          /* u32: a string */
    OP_DUP, OP_DUP2, OP_POP, OP_SWAP,
    OP_ROT3,         /* a b c -> c a b */
    OP_ROT4,         /* a b c d -> d a b c */
    OP_GET_LOC, OP_PUT_LOC, OP_SET_LOC,           /* u16 slot (put pops, set keeps) */
    OP_GET_LOC_CHK,                               /* u16 slot, u32 atom: a let or const read before it is set throws */
    OP_PUT_LOC_CHK, OP_SET_LOC_CHK,               /* u16 slot, u32 atom: a let assigned before it is set throws */
    OP_GET_ENV, OP_PUT_ENV, OP_SET_ENV,           /* u8 depth, u16 cell */
    OP_GET_ENV_CHK,                               /* u8, u16, u32 atom */
    OP_PUT_ENV_CHK, OP_SET_ENV_CHK,               /* u8, u16, u32 atom */
    OP_GET_GLOBAL, OP_PUT_GLOBAL, OP_SET_GLOBAL,  /* u32 atom, u32 cache */
    OP_TYPEOF_GLOBAL,                             /* u32 atom: typeof of an undeclared name is "undefined" */
    OP_DEF_VAR, OP_DEF_LEX, OP_DEF_FUNC,          /* u32 atom: a script's var, let or const, function */
    OP_ENTER_ENV,                                 /* u16 cells */
    OP_LEAVE_ENV, OP_COPY_ENV,
    OP_OBJECT, OP_ARRAY,                          /* ARRAY: u32 n values */
    OP_APPEND,                                    /* arr v -> arr */
    OP_APPEND_HOLE,                               /* arr -> arr */
    OP_SPREAD,                                    /* arr iterable -> arr */
    OP_DEF_PROP,                                  /* u32 atom: obj v -> obj */
    OP_SET_PROTO_LIT,                             /* obj v -> obj: a literal's __proto__: v, its prototype when v is an object or null */
    OP_TO_PROPKEY,                                /* v -> key: ToPropertyKey (a class's computed field key, read when the class is made) */
    OP_RUN_STATIC,                                /* c fn -> c: fn called with c its this and its home object (a class's static
                                                     initializer, a static block) */
    OP_DEF_ELEM,                                  /* obj key v -> obj */
    OP_DEF_GET, OP_DEF_SET,                       /* u32 atom: obj fn -> obj */
    OP_DEF_GET_ELEM, OP_DEF_SET_ELEM,             /* obj key fn -> obj */
    OP_DEF_METHOD,                                /* u32 atom: obj fn -> obj; the method's home is obj, it is not enumerable */
    OP_DEF_METHOD_ELEM,                           /* obj key fn -> obj */
    OP_COPY_PROPS,                                /* obj src -> obj */
    OP_GET_PROP,                                  /* u32 atom, u32 cache: obj -> v */
    OP_GET_PROP2,                                 /* u32 atom, u32 cache: obj -> obj v */
    OP_PUT_PROP,                                  /* u32 atom, u32 cache: obj v -> v */
    OP_GET_ELEM, OP_GET_ELEM2, OP_PUT_ELEM,       /* obj k -> v; obj k -> obj v; obj k v -> v */
    OP_DEL_PROP,                                  /* u32 atom: obj -> bool */
    OP_DEL_ELEM,                                  /* obj k -> bool */
    OP_IN, OP_INSTANCEOF,
    OP_GET_SUPER,                                 /* u32 atom: -> v, from the home object's prototype, with this */
    OP_GET_SUPER_ELEM,                            /* k -> v */
    OP_CLOSURE,                                   /* u32 func */
    OP_CALL,                                      /* u16 argc */
    OP_NEW,                                       /* u16 argc: callee args -> object */
    OP_CALL_ARR,                                  /* this callee array -> r */
    OP_NEW_ARR,                                   /* callee array -> object */
    OP_SUPER_CALL,                                /* u16 argc: args -> this */
    OP_SUPER_CALL_ARR,                            /* array -> this */
    OP_RETURN, OP_RETURN_UNDEF,
    OP_THIS, OP_NEW_TARGET, OP_CALLEE, OP_ARGUMENTS,
    OP_REST,                                      /* u16 from: the arguments from there, an array */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_NEG, OP_PLUS, OP_NOT, OP_BNOT,
    OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SAR, OP_SHR,
    OP_EQ, OP_NE, OP_SEQ, OP_SNE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_TYPEOF, OP_INC, OP_DEC, OP_TONUM, OP_TOSTR,
    OP_JMP, OP_JF, OP_JT,                         /* i32 */
    OP_JF_KEEP, OP_JT_KEEP,                       /* i32: jump keeping the value, or pop it and go on */
    OP_JNN_KEEP,                                  /* i32: not nullish: jump keeping it; nullish: pop it */
    OP_JNULLISH,                                  /* i32: nullish: replace it by undefined and jump */
    OP_TRY,                                       /* i32: the handler; the thrown value is pushed for it */
    OP_END_TRY, OP_THROW,
    OP_THROW_REF,                                 /* u32 atom: "x is not defined" */
    OP_THROW_CONST,                               /* u32 atom: "assignment to constant variable x" */
    OP_ITER_IN, OP_ITER_OF,                       /* v -> iterator */
    OP_ITER_NEXT,                                 /* i32: iter -> iter v, or pop iter and jump when done */
    OP_CLASS,                                     /* u32 func, u32 atom: parent -> ctor proto (parent: hole for none) */
    OP_INIT_FIELDS,                               /* run the class's field initializer on this */
    OP_SET_FIELDS,                                /* ctor fn -> ctor: fn initializes each instance's fields */
    OP_INIT_LEX,                                  /* u32 atom: a script's let, const or class set at its declaration */
    OP_JNULLISH2,                                 /* i32: a call's callee nullish: drop it and its this, push undefined, jump */
    OP_INC_LOC, OP_DEC_LOC,                       /* u16 slot: the slot's number plus or minus one, nothing pushed */
    /* a comparison and its jump, one instruction: i32, a b -> ; JLT jumps when a < b,
       JNLT when not (NaN included) */
    OP_JLT, OP_JLE, OP_JGT, OP_JGE, OP_JNLT, OP_JNLE, OP_JNGT, OP_JNGE,
    OP_JSEQ, OP_JSNE, OP_JEQ, OP_JNE,
    OP_CALL0,                                     /* u16 argc: callee args -> r, this undefined */
    OP_ADD_I, OP_SUB_I, OP_MUL_I, OP_MOD_I,       /* i32 k: a -> a op k */
    /* a local's own instructions (u16 slot first): the comparison with an integer and
       its jump (i32 k, i32), the arithmetic with an integer (i32 k) or another local
       (u16 slot), its return */
    OP_JLT_LI, OP_JLE_LI, OP_JGT_LI, OP_JGE_LI, OP_JNLT_LI, OP_JNLE_LI, OP_JNGT_LI, OP_JNGE_LI,
    OP_ADD_LI, OP_SUB_LI, OP_MUL_LI, OP_MOD_LI,
    OP_ADD_LL, OP_SUB_LL, OP_MUL_LL,
    OP_RETURN_LOC,
    /* generators and async functions: a frame suspended into its object and resumed */
    OP_GEN_START,                                 /* a generator's prologue done: its object made and returned, the body run at next() */
    OP_ASYNC_START,                               /* an async function's start: its run and its promise made */
    OP_YIELD,                                     /* v -> suspended; resumed: received mode */
    OP_AWAIT,                                     /* v -> suspended until v settles; resumed: value mode */
    OP_GEN_RESUME,                                /* i32, u16 n: value mode -> value (mode 0), thrown (1), or (2) the stack cut to
                                                     the slots and n iterators, value pushed, and the return path at i32 */
    OP_YIELD_STAR,                                /* i32 done, i32 return, u16 n: it received mode -> it v (yield v), or r
                                                     (done: jump; a return: GEN_RESUME's cut, then jump) */
    OP_GET_ITER,                                  /* v -> its iterator object (yield*'s) */
    OP_ITER_CLOSE,                                /* u8 quiet: iter -> : a for-of's iterator closed on an early exit (its
                                                     return called; quiet: an error that throws is lost) */
    OP_DESTRUCT_ARR,                              /* u16 n: v -> an array pattern's array: v if an array, else the
                                                     first n values its iterator gives (0xFFFF: all), it closed */
    OP_REGEXP,                                    /* u32 regex: a literal's RegExp object, its pattern compiled with the script */
    /* async generators and for await (their iterators are records: JK_ITER, JI_ASYNC or JI_FROM_SYNC) */
    OP_AYIELD,                                    /* v -> received mode: the first request answered {v, false}; the next
                                                     request's value and mode at once if one waits, else suspended */
    OP_ITER_ASYNC,                                /* v -> its async iterator record */
    OP_ITER_ANEXT,                                /* rec -> rec r: next() called (for await's step) */
    OP_ITER_ARESULT,                              /* i32 done: rec r -> rec value; done: both popped, then jump */
    OP_ITER_ACLOSE,                               /* u8 quiet, i32 skip: rec -> r, return()'s result to await; none (no
                                                     return, the record mid-step, or quiet and it threw): jump */
    OP_ITER_ACHECK,                               /* r -> : a TypeError unless r is an object */
    OP_AYS_STEP,                                  /* i32 noreturn, i32 nothrow, u16 n: rec recv mode -> rec mode r, the
                                                     inner's next, throw or return called; no return: recv at the
                                                     statement's stack (GEN_RESUME's cut), jump; no throw: rec, jump */
    OP_AYS_RESULT,                                /* i32 done, i32 return, u16 n: rec mode r -> rec value; done: value
                                                     (the expression's), jump; done in a return: value at the cut, jump */
    OP_AYS_UNWRAP,                                /* i32 loop: rec recv mode -> rec recv: a return's value to await; any
                                                     other mode: left, jump */
    OP_AYS_RETMODE,                               /* rec v m -> rec v mode: the awaited return value (mode 2) or its
                                                     rejection (1) */
    OP_AYS_NOTHROW,                               /* throws yield*'s TypeError: the inner iterator has no throw */
    /* eval and with: a name's dynamic scopes (a with's object, a sloppy function's eval vars) */
    OP_CALL_EVAL,                                 /* u16 argc, u32 site: callee args -> r: a direct eval when the callee
                                                     is eval itself, else a plain call */
    OP_DYN_GET,                                   /* u8 depth, u16 cell, u32 atom, u8 flags, i32: the cell's object has
                                                     the name (flags 1: a with's, its unscopables heeded; 2: the call's
                                                     this below set to it): its value pushed, jump */
    OP_DYN_PUT,                                   /* u8 depth, u16 cell, u32 atom, u8 flags, i32: v -> (v if flags 4):
                                                     the cell's object has the name: set, jump; else v left */
    OP_EVALVAR_DEF,                               /* u8 depth, u16 cell, u32 atom, u8 func: v -> : a sloppy eval's var
                                                     (or function) declared in its caller's eval vars object */
    OP_TO_OBJECT,                                 /* v -> ToObject(v): with's object */
    /* modules */
    OP_GET_IMPORT,                                /* u32 binding, u32 atom: an imported name's value, read where its
                                                     module's binding lives (vm->mod_binds) */
    OP_IMPORT_META,                               /* -> this module's import.meta */
    OP_IMPORT_DYN,                                /* spec options -> import(spec)'s promise */
    /* a class's private names (src/js/vm.c, js_priv_*): kind the compiler's PV_ flags (field 1, method 2, getter 4,
       setter 8, static 16), atom the name (#x), class its class's name (0 none), for V8's TypeErrors */
    OP_PRIV_NEW,                                  /* u32 atom: -> a new private name */
    OP_PRIV_GET,                                  /* u8 kind, u32 atom, u32 class: obj key -> v */
    OP_PRIV_GET2,                                 /* u8 kind, u32 atom, u32 class: obj key -> obj v */
    OP_PRIV_PUT,                                  /* u8 kind, u32 atom, u32 class: obj key v -> v */
    OP_PRIV_DEFINE,                               /* u8 kind, u32 atom, u32 class: obj key v -> obj (a field's value, a method) */
    OP_PRIV_ACCESSOR,                             /* u8 kind, u32 atom, u32 class: obj key get set -> obj */
    OP_PRIV_IN,                                   /* u32 atom: obj key -> bool (#x in obj) */
    OP_HOME,                                      /* home fn -> home fn: a method's home object home */
    OP_ARGUMENTS_MAPPED,                          /* u16 n, n u16 cells: -> a sloppy function's arguments object, its
                                                     element i its parameter i's cell of the function's environment
                                                     (0xFFFF: none), while i is less than its count of arguments */
    OP_NOP,
    OP_NOPS
};
/* the interpreter's switch covers every byte: the values past the opcodes, 8 at a time */
#define OP_UNUSED8(b) case (b): case (b) + 1: case (b) + 2: case (b) + 3: case (b) + 4: case (b) + 5: case (b) + 6: case (b) + 7:

/* the timers run in virtual time up to until, at most max of them (src/script/script.c) */
uint32_t js_run_timers(js_vm *vm, double until, uint32_t max);
/* room on the stack and in the frames (src/js/vm.c): 0 past their maximums */
int js_room(js_vm *vm, uint32_t sp, uint32_t n);

/* the native functions' numbers: the builtins' first, then the host's from JS_HOST_NATIVES */
#define JS_HOST_NATIVES 1000

#endif
