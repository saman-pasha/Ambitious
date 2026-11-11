/* src/js/js.h -- Ambitious's JavaScript engine: the shared declarations (PLAN.md Phase 8).

   One VM per page. Everything it holds is an index arena, as every other module's
   state is: no value, object or string holds a pointer, so the safe part sees integers.

   VALUES are NaN-boxed 64-bit words. A double is itself (its NaNs canonical); above
   the doubles, the top 16 bits tag the rest: 0xFFF9 the specials (undefined, null,
   false, true, and the hole that marks an uninitialized binding or a missing array
   element), 0xFFFA an object's index, 0xFFFB a string's index.

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

   MEMORY is the page's arena, freed with the VM, under a cap; TIME is virtual. */
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
#define JV_NAN   0x7FF8000000000000ull

#define jv_is_num(v) ((v) < 0xFFF9000000000000ull)
#define jv_is_obj(v) (((v) >> 48) == 0xFFFAu)
#define jv_is_str(v) (((v) >> 48) == 0xFFFBu)
#define jv_is_bool(v) ((v) == JV_TRUE || (v) == JV_FALSE)
#define jv_is_nullish(v) ((v) == JV_UNDEF || (v) == JV_NULL)
#define jv_obj(i) (JV_OBJ_TAG | (uint64_t)(uint32_t)(i))
#define jv_str(i) (JV_STR_TAG | (uint64_t)(uint32_t)(i))
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
    JK_ENV, JK_ACCESSOR, JK_DATE, JK_REGEXP, JK_MAP, JK_SET, JK_HOST, JK_ITER
};
/* an iterator: a its source, b its place, c its mode */
enum { JI_KEYS, JI_ARRAY, JI_STRING, JI_MAP, JI_SET, JI_MAP_KEYS, JI_MAP_VALUES, JI_ENTRIES };

/* an object. a, b, c, x are its kind's: an array's elements (slot offset, length,
   capacity); a closure's function, environment and home object, with its captured
   this in x; a native's case number in a and its data in x; a bound function's
   target in a, its arguments in b and c (offset, count) and its this in x; an
   environment's parent in a and its cells' count in b; an accessor's getter in x
   and setter in a; a wrapper's or a date's value in x; a host object's handle in x */
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

/* a shape: the key it adds to its parent's, at which slot; its proto, its count */
typedef struct js_shape { uint32_t parent, atom, slot, count, proto, flags; } js_shape;
#define JS_SHAPE_ACCESSOR 1   /* the slot holds an accessor pair */
#define JS_SHAPE_HIDDEN 2     /* not enumerable */

typedef struct js_str { uint32_t off, len, hash, atom, left, right; uint32_t rope; } js_str;

/* a compiled function */
typedef struct js_func {
    uint32_t code, code_len;          /* in vm->code */
    uint32_t nparams, nslots;         /* frame slots: the parameters, then the locals */
    uint32_t name;                    /* an atom, 0 for none */
    uint32_t ics, nics;               /* its property caches, in vm->ics */
    uint32_t flags, line, script;
    uint32_t src_off, src_len;        /* its source text's place in its script */
    uint32_t arity;                   /* its length: the parameters before a default or a rest */
} js_func;
#define JF_ARROW 1
#define JF_ARGUMENTS 2                /* uses arguments */
#define JF_CLASS_CTOR 4
#define JF_DERIVED 8
#define JF_METHOD 16
#define JF_STRICT 32
#define JF_HAS_REST 64
#define JF_GETTER 128

/* a property access's cache: the receiver's shape, the holder (0: the receiver
   itself), the holder's shape, the slot */
typedef struct js_ic { uint32_t shape, holder, hshape, slot; } js_ic;

typedef struct js_frame {
    uint32_t func, pc, base, env, callee, argc, sp0, flags;
    jv thisv, newt;
    uint32_t args;                    /* the arguments object, made at its first use */
    uint32_t handlers;                /* the handler stack's height at entry */
} js_frame;
#define JFR_CONSTRUCT 1               /* a [[Construct]]: the result is this unless an object is returned */
#define JFR_NATIVE_ENTRY 2            /* a native's call: return to C when it returns */

typedef struct js_handler { uint32_t frame, pc, sp, env; } js_handler;

/* a timer, in virtual time */
typedef struct js_timer { uint32_t id; double due, interval; jv fn; uint32_t args, nargs, live, seq; } js_timer;

/* a console message, a script's error, an event listener: what the page's facts say */
typedef struct js_msg { uint32_t level, text; } js_msg;
typedef struct js_listener { uint32_t target, type, fn, capture, once, live; } js_listener;

#define JS_STACK_MAX (1u << 20)       /* values: a call deeper than this is a RangeError */
#define JS_FRAMES_MAX 10000

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
    vector<uint64_t> stack;
    vector<js_frame> frames;
    vector<js_handler> handlers;
    vector<js_timer> timers;
    vector<js_msg> msgs;
    vector<js_listener> listeners;
    vector<uint32_t> lookup;          /* a (shape, atom) -> slot cache for the slow path */
    vector<char> out;                 /* scratch text: what a native writes */
    uint32_t global, global_lex;      /* the global object; the global lexical bindings */
    uint32_t object_proto, function_proto, array_proto, string_proto, number_proto, boolean_proto, error_proto;
    uint32_t type_error_proto, range_error_proto, syntax_error_proto, reference_error_proto, date_proto, regexp_proto;
    uint32_t map_proto, set_proto, iterator_proto;
    uint32_t error_ctor, type_error_ctor, range_error_ctor, syntax_error_ctor, reference_error_ctor, eval_error_proto;
    uint32_t host_protos[16];         /* the host's prototypes, by its kinds (src/js/dom.c) */
    jv exception;                     /* the thrown value, while one is in flight */
    int throwing;
    uint64_t steps, step_budget;      /* the bytecodes run; past the budget, a script is stopped */
    double now;                       /* virtual time, ms */
    double real_clock;                /* set: performance.now() reads the real clock (benchmarks) */
    uint32_t timer_seq, timer_id, depth;
    uint64_t heap_cap;                /* the slots and units past which a script ends with a RangeError */
    uint32_t rng;
    uint32_t script;                  /* the script running: its number, for errors */
    uint32_t host_kinds;
    uint32_t natoms, nroots, ntrans;
    uint32_t digit_strs[10];
    uint32_t sp, nframes, nhandlers;
    int echo;                         /* the console written to stdout as well (ambitious js) */
    uint32_t root_proto, root_shape, root_valid;   /* the last prototype's root shape */
} js_vm;

#define JS_VM_DROP(v) vector_js_obj_drop(&(v).objs); vector_uint64_t_drop(&(v).slots); vector_js_shape_drop(&(v).shapes); \
    vector_uint32_t_drop(&(v).trans); vector_uint32_t_drop(&(v).roots); vector_uint16_t_drop(&(v).units); vector_js_str_drop(&(v).strs); \
    vector_uint32_t_drop(&(v).atom_slots); vector_js_func_drop(&(v).funcs); vector_uint8_t_drop(&(v).code); vector_uint64_t_drop(&(v).consts); \
    vector_js_ic_drop(&(v).ics); vector_uint64_t_drop(&(v).stack); vector_js_frame_drop(&(v).frames); \
    vector_js_handler_drop(&(v).handlers); vector_js_timer_drop(&(v).timers); vector_js_msg_drop(&(v).msgs); \
    vector_js_listener_drop(&(v).listeners); vector_uint32_t_drop(&(v).lookup); vector_char_drop(&(v).out)

/* ---- the atoms every part names (src/js/vm.c makes them first, in this order) ---- */
enum {
    A_NONE, A_LENGTH, A_PROTOTYPE, A_CONSTRUCTOR, A_TOSTRING, A_VALUEOF, A_NAME, A_MESSAGE, A_STACK, A_CALLEE,
    A_GET, A_SET, A_VALUE, A_WRITABLE, A_ENUMERABLE, A_CONFIGURABLE, A_UNDEFINED, A_NULL, A_TRUE, A_FALSE,
    A_OBJECT, A_FUNCTION, A_NUMBER, A_STRING, A_BOOLEAN, A_SYMBOL, A_EMPTY, A_DEFAULT, A_DONE, A_NEXT,
    A_THEN, A_TOJSON, A_INDEX, A_INPUT, A_ARGUMENTS, A_CAUSE, A_LASTINDEX, A_SIZE,
    A_NATOMS
};

/* ---- the operations the parts share ---- */

/* the VM: made, its prototypes and globals built (src/js/builtins.c), and its limits */
void js_init(js_vm *vm);
/* a script compiled and run as a classic script: 0 done, -1 a syntax error, -2 an
   uncaught exception, -3 the step budget spent. Its error's text goes to vm->out */
int js_eval(js_vm *vm, const char *src, size_t n, const char *name, uint32_t script);
/* a value called from C: its result, or JV_HOLE with vm->exception set. The arguments
   are argc values of a C array; a native's own arguments are argc values of the stack
   from index args (no pointer into the stack is ever held) */
jv js_call(js_vm *vm, jv fn, jv thisv, const jv *argv, uint32_t argc);
jv js_construct(js_vm *vm, jv fn, const jv *argv, uint32_t argc, jv newt);
jv js_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i);

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
int js_get_own(js_vm *vm, uint32_t o, uint32_t atom, jv *v, uint32_t *flags);
int js_delete(js_vm *vm, uint32_t o, uint32_t atom);
void js_own_keys(js_vm *vm, uint32_t o, int enumerable_only, uint32_t arr);   /* the own string keys, in order, pushed as strings */
uint32_t js_shape_of(js_vm *vm, uint32_t proto);
int js_is_callable(js_vm *vm, jv v);
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

/* functions made by natives and the host */
uint32_t js_native_new(js_vm *vm, uint32_t id, uint32_t name, uint32_t nargs);
uint32_t js_closure_new(js_vm *vm, uint32_t func, uint32_t env, jv thisv);
jv js_native_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt);   /* src/js/builtins.c */
void js_builtins_init(js_vm *vm);

/* the console and the host's hooks (src/js/dom.c) */
void js_console(js_vm *vm, uint32_t level, uint32_t text);
jv js_host_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled);
int js_host_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled);
jv js_host_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled);

/* the property caches' paths (src/js/vm.c) */
jv js_get_ic(js_vm *vm, jv obj, uint32_t atom, uint32_t ic);
int js_set_ic(js_vm *vm, jv obj, uint32_t atom, jv v, uint32_t ic);

/* the compiler (src/js/compile.c): a script's text into a function of no parameters */
int js_compile(js_vm *vm, const char *src, size_t n, uint32_t script, int as_function_body, uint32_t *func, vector<char> *err);
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
    OP_GET_ENV, OP_PUT_ENV, OP_SET_ENV,           /* u8 depth, u16 cell */
    OP_GET_ENV_CHK,                               /* u8, u16, u32 atom */
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
    OP_NOP,
    OP_NOPS
};
/* the interpreter's switch covers every byte: the values past the opcodes, 8 at a time */
#define OP_UNUSED8(b) case (b): case (b) + 1: case (b) + 2: case (b) + 3: case (b) + 4: case (b) + 5: case (b) + 6: case (b) + 7:

/* the timers run in virtual time up to until, at most max of them (src/js/dom.c) */
uint32_t js_run_timers(js_vm *vm, double until, uint32_t max);

/* the native functions' numbers: the builtins' first, then the host's from JS_HOST_NATIVES */
#define JS_HOST_NATIVES 1000

#endif
