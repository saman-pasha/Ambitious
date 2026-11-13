/* src/js/builtins.c -- the standard library: the prototypes, the constructors and the
   global functions, each native a case of js_native_call's switch.

   What is here: Object, Function, Array, String, Number, Boolean, Math, JSON, Error
   and its kinds, Map, Set, Symbol, Promise, generators' methods, Date (in virtual
   time), console, performance.now, the timers, queueMicrotask, parseInt and
   parseFloat. RegExp and String's methods over it are src/js/regexp.c's, reached
   through the native ids from N_RE_FIRST. */
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "js.h"

enum {
    N_NONE,
    /* globals */
    N_PARSE_INT, N_PARSE_FLOAT, N_IS_NAN, N_IS_FINITE, N_EVAL,
    /* constructors */
    N_OBJECT, N_FUNCTION, N_ARRAY, N_STRING, N_NUMBER, N_BOOLEAN, N_ERROR, N_TYPE_ERROR, N_RANGE_ERROR, N_SYNTAX_ERROR,
    N_REFERENCE_ERROR, N_EVAL_ERROR, N_DATE, N_MAP, N_SET, N_SYMBOL,
    /* Object */
    N_OBJ_KEYS, N_OBJ_VALUES, N_OBJ_ENTRIES, N_OBJ_ASSIGN, N_OBJ_CREATE, N_OBJ_GETPROTO, N_OBJ_SETPROTO, N_OBJ_DEFPROP,
    N_OBJ_DEFPROPS, N_OBJ_NAMES, N_OBJ_FREEZE, N_OBJ_ISFROZEN, N_OBJ_FROMENTRIES, N_OBJ_GETOWNDESC, N_OBJ_IS, N_OBJ_PREVENTEXT,
    N_OP_HASOWN, N_OP_TOSTRING, N_OP_VALUEOF, N_OP_ISPROTO, N_OP_PROPENUM, N_OP_TOLOCALE,
    /* Function */
    N_FP_CALL, N_FP_APPLY, N_FP_BIND, N_FP_TOSTRING,
    /* Array */
    N_ARR_ISARRAY, N_ARR_FROM, N_ARR_OF,
    N_AP_PUSH, N_AP_POP, N_AP_SHIFT, N_AP_UNSHIFT, N_AP_SLICE, N_AP_SPLICE, N_AP_CONCAT, N_AP_JOIN, N_AP_REVERSE, N_AP_INDEXOF,
    N_AP_LASTINDEXOF, N_AP_INCLUDES, N_AP_FIND, N_AP_FINDINDEX, N_AP_FINDLAST, N_AP_FINDLASTINDEX, N_AP_FILTER, N_AP_MAP,
    N_AP_FOREACH, N_AP_REDUCE, N_AP_REDUCERIGHT, N_AP_SOME, N_AP_EVERY, N_AP_SORT, N_AP_FILL, N_AP_KEYS, N_AP_VALUES,
    N_AP_ENTRIES, N_AP_FLAT, N_AP_FLATMAP, N_AP_AT, N_AP_COPYWITHIN, N_AP_TOSTRING,
    /* String */
    N_STR_FROMCHARCODE, N_STR_FROMCODEPOINT, N_STR_RAW,
    N_SP_CHARAT, N_SP_CHARCODEAT, N_SP_CODEPOINTAT, N_SP_INDEXOF, N_SP_LASTINDEXOF, N_SP_INCLUDES, N_SP_STARTSWITH,
    N_SP_ENDSWITH, N_SP_SLICE, N_SP_SUBSTRING, N_SP_SUBSTR, N_SP_TOUPPER, N_SP_TOLOWER, N_SP_TRIM, N_SP_TRIMSTART, N_SP_TRIMEND,
    N_SP_PADSTART, N_SP_PADEND, N_SP_REPEAT, N_SP_SPLIT, N_SP_REPLACE, N_SP_REPLACEALL, N_SP_CONCAT, N_SP_AT, N_SP_LOCALECOMPARE,
    N_SP_TOSTRING, N_SP_NORMALIZE, N_SP_MATCH, N_SP_SEARCH,
    /* Number, Boolean */
    N_NUM_ISINTEGER, N_NUM_ISSAFEINTEGER, N_NUM_ISFINITE, N_NUM_ISNAN,
    N_NP_TOSTRING, N_NP_TOFIXED, N_NP_TOPRECISION, N_NP_VALUEOF, N_NP_TOLOCALE, N_BP_TOSTRING, N_BP_VALUEOF,
    /* Math */
    N_M_ABS, N_M_FLOOR, N_M_CEIL, N_M_ROUND, N_M_TRUNC, N_M_SIGN, N_M_MAX, N_M_MIN, N_M_POW, N_M_SQRT, N_M_CBRT, N_M_EXP,
    N_M_EXPM1, N_M_LOG, N_M_LOG2, N_M_LOG10, N_M_LOG1P, N_M_SIN, N_M_COS, N_M_TAN, N_M_ASIN, N_M_ACOS, N_M_ATAN, N_M_ATAN2,
    N_M_SINH, N_M_COSH, N_M_TANH, N_M_HYPOT, N_M_RANDOM, N_M_FROUND, N_M_IMUL, N_M_CLZ32,
    /* JSON, console, Error, time */
    N_JSON_STRINGIFY, N_JSON_PARSE, N_CON_LOG, N_CON_INFO, N_CON_WARN, N_CON_ERROR, N_CON_DEBUG,
    N_EP_TOSTRING, N_DATE_NOW, N_DATE_UTC, N_DP_GETTIME, N_DP_TOISO, N_DP_GETFULLYEAR, N_DP_GETMONTH, N_DP_GETDATE, N_DP_GETDAY,
    N_DP_GETHOURS, N_DP_GETMINUTES, N_DP_GETSECONDS, N_DP_GETMS, N_DP_TOSTRING, N_PERF_NOW,
    N_DATE_PARSE, N_DP_GETTZO, N_DP_GETYEAR, N_DP_SETTIME, N_DP_SETMS, N_DP_SETSEC, N_DP_SETMIN, N_DP_SETHOURS, N_DP_SETDATE,
    N_DP_SETMONTH, N_DP_SETFULLYEAR, N_DP_SETUTCMS, N_DP_SETUTCSEC, N_DP_SETUTCMIN, N_DP_SETUTCHOURS, N_DP_SETUTCDATE,
    N_DP_SETUTCMONTH, N_DP_SETUTCFULLYEAR, N_DP_SETYEAR, N_DP_TODATESTRING, N_DP_TOTIMESTRING, N_DP_TOUTCSTRING, N_DP_TOJSON,
    N_DP_TOPRIM,
    N_SET_TIMEOUT, N_SET_INTERVAL, N_CLEAR_TIMEOUT, N_QUEUE_MICROTASK,
    /* Map, Set, RegExp, iterators */
    N_MP_GET, N_MP_SET, N_MP_HAS, N_MP_DELETE, N_MP_CLEAR, N_MP_SIZE, N_MP_FOREACH, N_MP_KEYS, N_MP_VALUES, N_MP_ENTRIES,
    N_SETP_ADD, N_SETP_FOREACH, N_SETP_VALUES, N_SETP_ENTRIES, N_SETP_HAS, N_SETP_DELETE, N_SETP_CLEAR, N_SETP_SIZE,
    N_SETP_UNION, N_SETP_INTERSECTION, N_SETP_DIFFERENCE, N_SETP_SYMDIFF, N_SETP_ISSUBSET, N_SETP_ISSUPERSET, N_SETP_ISDISJOINT, N_ITP_NEXT,
    /* Symbol, generators, promises */
    N_SYM_FOR, N_SYM_KEYFOR, N_SYMP_TOSTRING, N_SYMP_VALUEOF, N_SYMP_DESC, N_OBJ_GETOWNSYMS, N_ITP_SELF,
    N_GEN_NEXT, N_GEN_RETURN, N_GEN_THROW,
    N_AG_NEXT, N_AG_RETURN, N_AG_THROW, N_AG_RETURN_OK, N_AG_RETURN_ERR, N_AFS_UNWRAP, N_AFS_CLOSE, N_GC,
    N_PROMISE, N_PR_RESOLVE_FN, N_PR_REJECT_FN, N_PRP_THEN, N_PRP_CATCH, N_PRP_FINALLY, N_PR_FIN_OK, N_PR_FIN_ERR,
    N_PR_THUNK_VALUE, N_PR_THUNK_THROW, N_PR_RESOLVE, N_PR_REJECT, N_PR_ALL, N_PR_ALLSETTLED, N_PR_RACE, N_PR_ANY,
    N_PR_ELEM_OK, N_PR_ELEM_ERR, N_AGGREGATE_ERROR, N_SP_ITER, N_SP_MATCHALL,
    N_OBJ_HASOWN, N_OBJ_ISEXT, N_OBJ_ISSEALED, N_OBJ_GETOWNDESCS, N_OBJ_GROUPBY, N_MAP_GROUPBY,
    N_PR_WITHRESOLVERS, N_PR_CAP_EXEC, N_SPECIES, N_THROW_TE, N_OP_PROTO_GET, N_OP_PROTO_SET,
    N_MIT_NEXT, N_SIT_NEXT, N_STIT_NEXT, N_RSIT_NEXT, N_SP_VALUEOF,
    N_ENCODE_URI, N_ENCODE_URIC, N_DECODE_URI, N_DECODE_URIC, N_ESCAPE, N_UNESCAPE, N_URI_ERROR,
    N_AP_TOREVERSED, N_AP_TOSORTED, N_AP_TOSPLICED, N_AP_WITH, N_AP_TOLOCALE, N_SP_ISWELLFORMED, N_SP_TOWELLFORMED,
    N_SP_ANCHOR, N_SP_BIG, N_SP_BLINK, N_SP_BOLD, N_SP_FIXED, N_SP_FONTCOLOR, N_SP_FONTSIZE, N_SP_ITALICS, N_SP_LINK, N_SP_SMALL,
    N_SP_STRIKE, N_SP_SUB, N_SP_SUP, N_NP_TOEXPONENTIAL, N_M_ACOSH, N_M_ASINH, N_M_ATANH, N_OP_DEFGETTER, N_OP_DEFSETTER,
    N_OP_LOOKUPGETTER, N_OP_LOOKUPSETTER,
    N_ITERATOR, N_ITER_FROM, N_WRAP_NEXT, N_WRAP_RETURN, N_ITP_MAP, N_ITP_FILTER, N_ITP_TAKE, N_ITP_DROP, N_ITP_FLATMAP,
    N_ITP_REDUCE, N_ITP_TOARRAY, N_ITP_FOREACH, N_ITP_SOME, N_ITP_EVERY, N_ITP_FIND, N_ITP_CTOR_GET, N_ITP_CTOR_SET, N_ITP_TAG_GET,
    N_ITP_TAG_SET, N_HELPER_NEXT, N_HELPER_RETURN,
    /* RegExp's natives (src/js/regexp.c), JRE_N of them; Proxy's, Reflect's and the weak
       collections' (src/js/proxy.c), JPX_N */
    N_RE_FIRST,
    N_PX_FIRST = N_RE_FIRST + JRE_N,
    N_TA_FIRST = N_PX_FIRST + JPX_N,
    N_BG_FIRST = N_TA_FIRST + JTN_N,
    N_MD_FIRST = N_BG_FIRST + JBG_N,
    N_IN_FIRST = N_MD_FIRST + JMD_N,      /* Intl's (src/js/intl.c), JIN_N */
    N_LAST = N_IN_FIRST + JIN_N
};

/* ---- helpers ---- */

static jv arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i) { return js_arg(vm, args, argc, i); }

static void defn(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t nargs) {
    uint32_t atom = js_atom_ascii(vm, name);
    js_define(vm, o, atom, jv_obj(js_native_new(vm, id, atom, nargs)), JS_SHAPE_HIDDEN);
}
static void defv(js_vm *vm, uint32_t o, const char *name, jv v) { js_define(vm, o, js_atom_ascii(vm, name), v, JS_SHAPE_HIDDEN); }

static uint32_t ctor(js_vm *vm, const char *name, uint32_t id, uint32_t nargs, uint32_t proto) {
    uint32_t atom = js_atom_ascii(vm, name);
    uint32_t c = js_native_new(vm, id, atom, nargs);
    js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_PROTO);
    js_define(vm, proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, atom, jv_obj(c), JS_SHAPE_HIDDEN);
    return c;
}

static double num_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i) { return js_to_number(vm, arg(vm, args, argc, i)); }

/* an integer argument, ToIntegerOrInfinity, with a default for undefined */
static double int_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i, double def) {
    jv v = arg(vm, args, argc, i);
    if (v == JV_UNDEF) return def;
    double d = js_to_number(vm, v);
    if (d != d) return 0;
    return trunc(d);
}

/* a relative index into a length: -1 from the end, clamped */
static uint32_t rel_index(double d, uint32_t n) {
    if (d < 0) { d += n; if (d < 0) d = 0; }
    if (d > n) d = n;
    return (uint32_t)d;
}

static uint32_t this_str(js_vm *vm, jv thisv) {
    if (jv_is_str(thisv)) return jv_idx(thisv);
    if (jv_is_nullish(thisv) || thisv == JV_HOLE) { js_throw_msg(vm, vm->type_error_proto, "String.prototype method called on null or undefined"); return 0; }
    return js_to_string(vm, thisv);
}

/* an object's length, and its elements, for the generic array methods */
/* the object o given the prototype p: its properties moved onto a shape of p's */
void js_set_proto(js_vm *vm, uint32_t o, uint32_t p) {
    /* the shape's own entries, oldest first (symbols' among them; an array's dense
       elements are not the shape's), each defined again on a shape of p's */
    vector<uint32_t> es, keys, flags;
    vector<jv> vals;
    for (uint32_t x = vm->objs[o].shape; x && vm->shapes[x].count; x = vm->shapes[x].parent) push_back(es, x);
    for (size_t q = len(es); q > 0; q--) {
        js_shape e = vm->shapes[es[q - 1]];
        jv v = vm->slots[vm->objs[o].slots + e.slot];
        if (v == JV_HOLE) continue;
        push_back(keys, e.atom);
        push_back(flags, e.flags);
        push_back(vals, v);
    }
    vm->objs[o].proto = p;
    vm->objs[o].shape = js_shape_of(vm, p);
    vm->objs[o].cap = 0;
    for (size_t k = 0; k < len(keys); k++) js_define(vm, o, keys[k], vals[k], flags[k]);
}

static double len_of(js_vm *vm, jv o) {
    if (jv_is_obj(o) && (vm->objs[jv_idx(o)].kind == JK_ARRAY || vm->objs[jv_idx(o)].kind == JK_TYPEDARRAY)) return vm->objs[jv_idx(o)].b;
    jv n = js_get(vm, o, A_LENGTH);
    if (vm->throwing) return 0;
    double d = js_to_number(vm, n);
    return d != d || d < 0 ? 0 : trunc(d);
}
static jv elem_of(js_vm *vm, jv o, uint32_t i) {
    if (jv_is_obj(o) && vm->objs[jv_idx(o)].kind == JK_ARRAY) {
        uint32_t a = jv_idx(o);
        jv v = i < vm->objs[a].b ? vm->slots[vm->objs[a].a + i] : JV_HOLE;
        if (v != JV_HOLE) return v;
        /* a hole: an element with attributes of its own (a getter's) is read as Get reads it */
        if (!(vm->objs[a].flags & JO_ELEMPROPS)) return JV_UNDEF;
    }
    if (jv_is_obj(o) && vm->objs[jv_idx(o)].kind == JK_TYPEDARRAY) return i < vm->objs[jv_idx(o)].b ? js_ta_get(vm, jv_idx(o), i) : JV_UNDEF;
    return js_get_value(vm, o, jv_num((double)i));
}
/* an array as the engine keeps one (the natives' fast paths) */
static int is_array(js_vm *vm, jv v) { return jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_ARRAY; }
/* an array the natives may write as they find it: its elements dense and of the default
   attributes, extensible, its length writable (else the standard's own steps, which
   throw where V8 throws) */
static int plain_array(js_vm *vm, jv v) { return is_array(vm, v) && (vm->objs[jv_idx(v)].flags & JO_ELEM_SLOW) == JO_EXTENSIBLE; }
/* HasProperty(ToObject(o), i): an array's (an arguments object's) dense element, a typed
   array's or a string's index, else as the object and its prototypes say */
static int has_elem(js_vm *vm, jv o, uint32_t i) {
    if (jv_is_str(o)) return i < vm->strs[jv_idx(o)].len;
    if (!jv_is_obj(o)) return 0;
    uint32_t x = jv_idx(o), k = vm->objs[x].kind;
    if (k == JK_ARRAY || k == JK_ARGUMENTS) {
        uint32_t st = k == JK_ARGUMENTS ? vm->objs[x].a : x;
        if (i < vm->objs[st].b && vm->slots[vm->objs[st].a + i] != JV_HOLE) return 1;
    }
    if (k == JK_TYPEDARRAY) return i < vm->objs[x].b;
    return js_has(vm, x, js_atom(vm, js_num_to_str(vm, (double)i)));
}
/* the same as the read-only natives ask it: an array without elements of attributes of
   their own by its slot alone (its prototypes' indexes not looked for), else has_elem */
static int elem_present(js_vm *vm, jv o, uint32_t i) {
    if (is_array(vm, o) && !(vm->objs[jv_idx(o)].flags & JO_ELEMPROPS)) {
        uint32_t a = jv_idx(o);
        return i < vm->objs[a].b && vm->slots[vm->objs[a].a + i] != JV_HOLE;
    }
    return has_elem(vm, o, i);
}
/* elem_present then elem_of, in one: 1 with the value in *v, 0 not there, -1 thrown (an
   array's dense element read at once, the walk the methods that skip holes take) */
static int elem_take(js_vm *vm, jv o, uint32_t i, jv *v) {
    if (jv_is_obj(o) && vm->objs[jv_idx(o)].kind == JK_ARRAY) {
        uint32_t a = jv_idx(o);
        jv x = i < vm->objs[a].b ? vm->slots[vm->objs[a].a + i] : JV_HOLE;
        if (x != JV_HOLE) { *v = x; return 1; }
        if (!(vm->objs[a].flags & JO_ELEMPROPS)) return 0;
    }
    int has = has_elem(vm, o, i);
    if (vm->throwing) return -1;
    if (!has) return 0;
    *v = elem_of(vm, o, i);
    return vm->throwing ? -1 : 1;
}
/* IsArray: an array, or a proxy whose target is one (through every proxy) */
static int is_array_spec(js_vm *vm, jv v) {
    for (int d = 0; d < 10000 && jv_is_obj(v); d++) {
        uint32_t o = jv_idx(v);
        if (vm->objs[o].kind == JK_ARRAY) return 1;
        if (vm->objs[o].kind != JK_PROXY) return 0;
        if (!vm->objs[o].b) { js_throw_msg(vm, vm->type_error_proto, "Cannot perform 'IsArray' on a proxy that has been revoked"); return 0; }
        v = jv_obj(vm->objs[o].a);
    }
    return 0;
}

/* a TypeError of pre, the value's text without its methods run (V8's), then post */
static jv value_error(js_vm *vm, const char *pre, jv v, const char *post) {
    if (vm->throwing) return JV_HOLE;
    vector<char> m;
    append_range(m, pre, strlen(pre));
    js_value_text(vm, v, &m);
    append_range(m, post, strlen(post));
    return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
}

/* why [[SetPrototypeOf]] refused o, in V8's words */
static jv proto_refused(js_vm *vm, jv o) {
    uint32_t x = jv_idx(o);
    if (vm->objs[x].kind == JK_PROXY) return js_throw_msg(vm, vm->type_error_proto, "'setPrototypeOf' on proxy: trap returned falsish for property 'undefined'");
    if (x == vm->object_proto) return js_throw_msg(vm, vm->type_error_proto, "Immutable prototype object 'Object.prototype' cannot have their prototype set");
    if (!(vm->objs[x].flags & JO_EXTENSIBLE)) return value_error(vm, "", o, " is not extensible");
    return js_throw_msg(vm, vm->type_error_proto, "Cyclic __proto__ value");
}

static jv callback(js_vm *vm, jv fn, jv thisv, jv a, jv b, jv c) {
    jv args[3] = { a, b, c };
    return js_call(vm, fn, thisv, args, 3);
}

static uint32_t str_of_ascii(js_vm *vm, const char *s) { return js_str_ascii(vm, s); }

/* ---- strings: searching ---- */

static int64_t str_index_of(js_vm *vm, uint32_t s, uint32_t t, uint32_t from) {
    js_str_flatten(vm, s);
    js_str_flatten(vm, t);
    uint32_t n = vm->strs[s].len, m = vm->strs[t].len, os = vm->strs[s].off, ot = vm->strs[t].off;
    if (m == 0) return from <= n ? from : n;
    if (m > n) return -1;
    uint16_t first = vm->units[ot];
    for (uint32_t i = from; i + m <= n; i++) {
        if (vm->units[os + i] != first) continue;
        uint32_t k = 1;
        while (k < m && vm->units[os + i + k] == vm->units[ot + k]) k++;
        if (k == m) return i;
    }
    return -1;
}

static int64_t str_last_index_of(js_vm *vm, uint32_t s, uint32_t t, uint32_t from) {
    js_str_flatten(vm, s);
    js_str_flatten(vm, t);
    uint32_t n = vm->strs[s].len, m = vm->strs[t].len, os = vm->strs[s].off, ot = vm->strs[t].off;
    if (m > n) return -1;
    uint32_t start = from + m > n ? n - m : from;
    for (int64_t i = start; i >= 0; i--) {
        uint32_t k = 0;
        while (k < m && vm->units[os + i + k] == vm->units[ot + k]) k++;
        if (k == m) return i;
    }
    return -1;
}

static int is_space(uint16_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0xA0 || c == 0xFEFF || c == 0x2028 || c == 0x2029 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x3000 || c == 0x1680 || c == 0x202F || c == 0x205F;
}

static uint32_t str_trim(js_vm *vm, uint32_t s, int start, int end) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off, a = 0, b = n;
    if (start) while (a < b && is_space(vm->units[o + a])) a++;
    if (end) while (b > a && is_space(vm->units[o + b - 1])) b--;
    return js_str_sub(vm, s, a, b);
}

/* a code point as UTF-16 */
static void put_cp(vector<uint16_t> *u, uint32_t c) {
    if (c > 0xFFFF) { push_back(*u, (uint16_t)(0xD800 + ((c - 0x10000) >> 10))); push_back(*u, (uint16_t)(0xDC00 + ((c - 0x10000) & 0x3FF))); }
    else push_back(*u, (uint16_t)c);
}
/* the code point at i (a pair whole), *w its width */
static uint32_t cp_at(js_vm *vm, uint32_t o, uint32_t n, uint32_t i, uint32_t *w) {
    uint32_t c = vm->units[o + i];
    *w = 1;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] <= 0xDFFF) {
        *w = 2;
        return 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i + 1] - 0xDC00u);
    }
    return c;
}
/* is a cased letter before the unit at i (dir < 0), or at i or after it (dir > 0), the
   case-ignorable characters between skipped? SpecialCasing's Final_Sigma as ICU reads it:
   by code points, and a character both case-ignorable and cased is skipped. (Its pairs
   are read here, not by cp_at, which str_case's loop keeps inline as its one caller.) */
static int sigma_cased_near(js_vm *vm, uint32_t o, uint32_t n, uint32_t i, int dir) {
    for (;;) {
        uint32_t c;
        if (dir < 0) {
            if (i == 0) return 0;
            c = vm->units[o + --i];
            if (c >= 0xDC00 && c <= 0xDFFF && i > 0 && vm->units[o + i - 1] >= 0xD800 && vm->units[o + i - 1] <= 0xDBFF) {
                i--;
                c = 0x10000 + ((vm->units[o + i] - 0xD800u) << 10) + (c - 0xDC00);
            }
        } else {
            if (i >= n) return 0;
            c = vm->units[o + i++];
            if (c >= 0xD800 && c <= 0xDBFF && i < n && vm->units[o + i] >= 0xDC00 && vm->units[o + i] <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i] - 0xDC00u);
                i++;
            }
        }
        /* ASCII's: the letters cased, ' . : ^ ` case-ignorable */
        if (c < 0x80) {
            if (c == '\'' || c == '.' || c == ':' || c == '^' || c == '`') continue;
            return (c | 0x20) >= 'a' && (c | 0x20) <= 'z';
        }
        if (!js_uni_case_ignorable(c)) return js_uni_cased(c);
    }
}

/* toUpperCase and toLowerCase: Unicode's full mappings (SpecialCasing's that hold
   everywhere), and a final sigma lowercased as one */
static uint32_t str_case(js_vm *vm, uint32_t s, int upper) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    vector<uint16_t> u;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = vm->units[o + i];
        if (c < 128) {
            if (upper && c >= 'a' && c <= 'z') c -= 32;
            else if (!upper && c >= 'A' && c <= 'Z') c += 32;
            push_back(u, (uint16_t)c);
            continue;
        }
        uint32_t w = 1;
        c = cp_at(vm, o, n, i, &w);
        uint32_t out[3];
        uint32_t k = js_uni_special(c, upper, out);
        if (!k) {
            out[0] = upper ? js_uni_upper(c) : js_uni_lower(c);
            k = 1;
            /* a capital sigma after a cased letter and before none is a word's final one */
            if (!upper && c == 0x3A3 && sigma_cased_near(vm, o, n, i, -1) && !sigma_cased_near(vm, o, n, i + 1, 1)) out[0] = 0x3C2;
        }
        for (uint32_t j = 0; j < k; j++) put_cp(&u, out[j]);
        i += w - 1;
    }
    return js_str_new(vm, data(u), len(u));
}

static uint32_t str_repeat(js_vm *vm, uint32_t s, uint32_t count) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    vector<uint16_t> u;
    for (uint32_t k = 0; k < count; k++) for (uint32_t i = 0; i < n; i++) push_back(u, vm->units[o + i]);
    return js_str_new(vm, data(u), len(u));
}

/* a string's replacement text for a match: $& $$ $` $' expanded */
static void replacement(js_vm *vm, uint32_t rep, uint32_t s, uint32_t pos, uint32_t mlen, vector<uint16_t> *out) {
    js_str_flatten(vm, rep);
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[rep].len, o = vm->strs[rep].off, so = vm->strs[s].off, sn = vm->strs[s].len;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[o + i];
        if (c == '$' && i + 1 < n) {
            uint16_t d = vm->units[o + i + 1];
            if (d == '$') { push_back(out, (uint16_t)'$'); i++; continue; }
            if (d == '&') { for (uint32_t k = 0; k < mlen; k++) push_back(out, vm->units[so + pos + k]); i++; continue; }
            if (d == '`') { for (uint32_t k = 0; k < pos; k++) push_back(out, vm->units[so + k]); i++; continue; }
            if (d == '\'') { for (uint32_t k = pos + mlen; k < sn; k++) push_back(out, vm->units[so + k]); i++; continue; }
        }
        push_back(out, c);
    }
}

/* includes', startsWith's and endsWith's refusal of a pattern (IsRegExp): 1 none, 0 thrown in V8's words */
static int str_not_regexp(js_vm *vm, jv v, const char *method) {
    int r = js_is_regexp(vm, v);
    if (r < 0) return 0;
    if (!r) return 1;
    char m[128];
    snprintf(m, sizeof m, "First argument to String.prototype.%s must not be a regular expression", method);
    js_throw_msg(vm, vm->type_error_proto, m);
    return 0;
}

static jv str_replace(js_vm *vm, uint32_t s, jv pat, jv rep, int all) {
    uint32_t p = js_to_string(vm, pat);
    if (!p) return JV_HOLE;
    int fn = js_is_callable(vm, rep);
    uint32_t r = 0;
    if (!fn) { r = js_to_string(vm, rep); if (!r) return JV_HOLE; }
    vector<uint16_t> out;
    uint32_t from = 0, pl = vm->strs[p].len;
    js_str_flatten(vm, s);
    for (;;) {
        int64_t at_i = str_index_of(vm, s, p, from);
        if (at_i < 0) break;
        uint32_t pos = (uint32_t)at_i;
        js_str_flatten(vm, s);
        for (uint32_t k = from; k < pos; k++) push_back(out, vm->units[vm->strs[s].off + k]);
        if (fn) {
            jv res = callback(vm, rep, JV_UNDEF, jv_str(js_str_sub(vm, s, pos, pos + pl)), jv_num(pos), jv_str(s));
            if (vm->throwing) return JV_HOLE;
            uint32_t rs = js_to_string(vm, res);
            if (!rs) return JV_HOLE;
            js_str_flatten(vm, rs);
            for (uint32_t k = 0; k < vm->strs[rs].len; k++) push_back(out, vm->units[vm->strs[rs].off + k]);
        } else replacement(vm, r, s, pos, pl, &out);
        from = pos + pl;
        if (pl == 0) {
            if (pos < vm->strs[s].len) push_back(out, vm->units[vm->strs[s].off + pos]);
            from = pos + 1;
            if (from > vm->strs[s].len) break;
        }
        if (!all) break;
    }
    js_str_flatten(vm, s);
    for (uint32_t k = from; k < vm->strs[s].len; k++) push_back(out, vm->units[vm->strs[s].off + k]);
    return jv_str(js_str_new(vm, data(out), len(out)));
}

/* ---- JSON ---- */

static void out_ascii(vector<uint16_t> *o, const char *s) { while (*s) { push_back(o, (uint16_t)(uint8_t)*s); s++; } }

static void json_quote(js_vm *vm, uint32_t s, vector<uint16_t> *o) {
    js_str_flatten(vm, s);
    push_back(o, (uint16_t)'"');
    uint32_t n = vm->strs[s].len, off = vm->strs[s].off;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[off + i];
        if (c == '"') out_ascii(o, "\\\"");
        else if (c == '\\') out_ascii(o, "\\\\");
        else if (c == '\n') out_ascii(o, "\\n");
        else if (c == '\r') out_ascii(o, "\\r");
        else if (c == '\t') out_ascii(o, "\\t");
        else if (c == '\b') out_ascii(o, "\\b");
        else if (c == '\f') out_ascii(o, "\\f");
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out_ascii(o, b); }
        else if (c >= 0xD800 && c <= 0xDFFF) {
            /* a surrogate pair as it is; a lone surrogate escaped (well-formed JSON.stringify, ES2019) */
            uint16_t d = i + 1 < n ? vm->units[off + i + 1] : 0;
            if (c <= 0xDBFF && d >= 0xDC00 && d <= 0xDFFF) { push_back(o, c); push_back(o, d); i++; }
            else { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out_ascii(o, b); }
        }
        else push_back(o, c);
    }
    push_back(o, (uint16_t)'"');
}

static void json_indent(vector<uint16_t> *o, uint32_t gap, uint32_t depth) {
    if (!gap) return;
    push_back(o, (uint16_t)'\n');
    for (uint32_t i = 0; i < gap * depth; i++) push_back(o, (uint16_t)' ');
}

/* the value's JSON into o: 0 when it has none (undefined, a function) */
static int json_write(js_vm *vm, jv v, vector<uint16_t> *o, uint32_t gap, uint32_t depth, vector<uint32_t> *stack) {
    if (vm->throwing) return 0;
    if (jv_is_obj(v) || jv_is_big(v)) {
        jv tj = js_get(vm, v, A_TOJSON);
        if (vm->throwing) return 0;
        if (js_is_callable(vm, tj)) { v = js_call(vm, tj, v, 0, 0); if (vm->throwing) return 0; }
    }
    if (jv_is_obj(v)) {
        uint32_t k = vm->objs[jv_idx(v)].kind;
        if (k == JK_NUMBER || k == JK_STRING || k == JK_BOOLEAN || k == JK_BIGINT) v = vm->objs[jv_idx(v)].x;
    }
    if (jv_is_big(v)) { js_throw_msg(vm, vm->type_error_proto, "Do not know how to serialize a BigInt"); return 0; }
    if (v == JV_NULL) { out_ascii(o, "null"); return 1; }
    if (v == JV_TRUE) { out_ascii(o, "true"); return 1; }
    if (v == JV_FALSE) { out_ascii(o, "false"); return 1; }
    if (jv_is_num(v)) {
        double d = jv_dbl(v);
        if (d != d || d == INFINITY || d == -INFINITY) { out_ascii(o, "null"); return 1; }
        char b[40];
        js_num_format(d, b, sizeof b);
        out_ascii(o, b);
        return 1;
    }
    if (jv_is_str(v)) { json_quote(vm, jv_idx(v), o); return 1; }
    if (!jv_is_obj(v) || js_is_callable(vm, v)) return 0;
    uint32_t ob = jv_idx(v);
    for (size_t i = 0; i < len(*stack); i++) if (at(*stack, i) == ob) { js_throw_msg(vm, vm->type_error_proto, "Converting circular structure to JSON"); return 0; }
    push_back(stack, ob);
    if (vm->objs[ob].kind == JK_ARRAY || (vm->objs[ob].kind == JK_PROXY && is_array_spec(vm, v))) {
        push_back(o, (uint16_t)'[');
        uint32_t n = (uint32_t)len_of(vm, v);
        for (uint32_t i = 0; i < n && !vm->throwing; i++) {
            if (i) push_back(o, (uint16_t)',');
            json_indent(o, gap, depth + 1);
            jv ev = elem_of(vm, v, i);
            if (vm->throwing) break;
            if (!json_write(vm, ev, o, gap, depth + 1, stack)) out_ascii(o, "null");
        }
        if (n) json_indent(o, gap, depth);
        push_back(o, (uint16_t)']');
    } else {
        push_back(o, (uint16_t)'{');
        uint32_t keys = js_array_new(vm, 0);
        js_own_keys(vm, ob, 1, keys);
        int first = 1;
        for (uint32_t i = 0; i < vm->objs[keys].b && !vm->throwing; i++) {
            uint32_t ks = jv_idx(js_array_get(vm, keys, i));
            jv pv = js_get(vm, v, js_atom(vm, ks));
            if (vm->throwing) break;
            size_t mark = len(*o);
            if (!first) push_back(o, (uint16_t)',');
            json_indent(o, gap, depth + 1);
            json_quote(vm, ks, o);
            push_back(o, (uint16_t)':');
            if (gap) push_back(o, (uint16_t)' ');
            if (!json_write(vm, pv, o, gap, depth + 1, stack)) { resize(*o, mark); continue; }
            first = 0;
        }
        if (!first) json_indent(o, gap, depth);
        push_back(o, (uint16_t)'}');
    }
    pop_back(stack);
    return 1;
}

/* JSON.parse: a reader over the string's units, strict (ECMA-404), its first error kept as V8 words it: its kind (ek)
   and position (epos) */
enum { JPE_NONE, JPE_END, JPE_TOKEN, JPE_PROP_OR_BRACE, JPE_COLON, JPE_COMMA_BRACE, JPE_DQ_PROP, JPE_COMMA_BRACK, JPE_UNTERM_STR,
       JPE_BAD_ESC, JPE_BAD_UESC, JPE_CTRL, JPE_MINUS, JPE_EXP, JPE_FRAC, JPE_NUMBER, JPE_AFTER };
typedef struct jp { uint32_t off, n, i; int err, ek; uint32_t epos; } jp;

static void jp_ws(js_vm *vm, jp *p) { while (p->i < p->n && (vm->units[p->off + p->i] == ' ' || vm->units[p->off + p->i] == '\t' || vm->units[p->off + p->i] == '\n' || vm->units[p->off + p->i] == '\r')) p->i++; }
/* the first error: its kind at its position (EOF where a value was wanted is the end's) */
static void jp_fail(jp *p, int kind, uint32_t at) {
    if (p->err) return;
    p->err = 1;
    p->ek = kind == JPE_TOKEN && at >= p->n ? JPE_END : kind;
    p->epos = at;
}

static uint32_t jp_string(js_vm *vm, jp *p) {
    vector<uint16_t> u;
    p->i++;
    for (;;) {
        if (p->i >= p->n) { jp_fail(p, JPE_UNTERM_STR, p->n); return A_EMPTY; }
        uint16_t c = vm->units[p->off + p->i];
        if (c == '"') { p->i++; break; }
        if (c < 0x20) { jp_fail(p, JPE_CTRL, p->i); return A_EMPTY; }
        if (c == '\\') {
            if (p->i + 1 >= p->n) { jp_fail(p, JPE_END, p->n); return A_EMPTY; }
            uint16_t d = vm->units[p->off + p->i + 1];
            if (d == 'n') push_back(u, (uint16_t)'\n');
            else if (d == 't') push_back(u, (uint16_t)'\t');
            else if (d == 'r') push_back(u, (uint16_t)'\r');
            else if (d == 'b') push_back(u, (uint16_t)'\b');
            else if (d == 'f') push_back(u, (uint16_t)'\f');
            else if (d == '"' || d == '\\' || d == '/') push_back(u, d);
            else if (d == 'u') {
                uint32_t cp = 0;
                p->i += 2;
                for (int k = 0; k < 4; k++) {
                    if (p->i >= p->n) { jp_fail(p, JPE_BAD_UESC, p->i); return A_EMPTY; }
                    uint16_t h = vm->units[p->off + p->i];
                    int x = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (x < 0) { jp_fail(p, JPE_BAD_UESC, p->i); return A_EMPTY; }
                    cp = cp * 16 + (uint32_t)x;
                    p->i++;
                }
                push_back(u, (uint16_t)cp);
                continue;
            } else { jp_fail(p, JPE_BAD_ESC, p->i + 1); return A_EMPTY; }
            p->i += 2;
            continue;
        }
        push_back(u, c);
        p->i++;
    }
    return js_str_new(vm, data(u), len(u));
}

/* a literal's letters, each as V8 meets them: the first that differs its unexpected token, the end its end */
static int jp_word(js_vm *vm, jp *p, const char *w) {
    for (size_t k = 0; w[k]; k++) {
        if (p->i >= p->n) { jp_fail(p, JPE_END, p->n); return 0; }
        if (vm->units[p->off + p->i] != (uint16_t)w[k]) { jp_fail(p, JPE_TOKEN, p->i); return 0; }
        p->i++;
    }
    return 1;
}

static int jp_digit(js_vm *vm, const jp *p) { return p->i < p->n && vm->units[p->off + p->i] >= '0' && vm->units[p->off + p->i] <= '9'; }

/* a value that is no container, its first unit c at the reader: a string, a literal, a number (an error else) */
static jv jp_primitive(js_vm *vm, jp *p, uint16_t c) {
    if (c == '"') return jv_str(jp_string(vm, p));
    if (c == 't') return jp_word(vm, p, "true") ? JV_TRUE : JV_UNDEF;
    if (c == 'f') return jp_word(vm, p, "false") ? JV_FALSE : JV_UNDEF;
    if (c == 'n') return jp_word(vm, p, "null") ? JV_NULL : JV_UNDEF;
    if (c != '-' && (c < '0' || c > '9')) { jp_fail(p, JPE_TOKEN, p->i); return JV_UNDEF; }
    /* -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)? */
    uint32_t st = p->i;
    if (c == '-') {
        p->i++;
        if (!jp_digit(vm, p)) { jp_fail(p, JPE_MINUS, p->i); return JV_UNDEF; }
    }
    if (vm->units[p->off + p->i] == '0') {
        p->i++;
        if (jp_digit(vm, p)) { jp_fail(p, JPE_NUMBER, p->i); return JV_UNDEF; }
    } else
        while (jp_digit(vm, p)) p->i++;
    if (p->i < p->n && vm->units[p->off + p->i] == '.') {
        p->i++;
        if (!jp_digit(vm, p)) { jp_fail(p, JPE_FRAC, p->i); return JV_UNDEF; }
        while (jp_digit(vm, p)) p->i++;
    }
    if (p->i < p->n && (vm->units[p->off + p->i] == 'e' || vm->units[p->off + p->i] == 'E')) {
        p->i++;
        if (p->i < p->n && (vm->units[p->off + p->i] == '+' || vm->units[p->off + p->i] == '-')) p->i++;
        if (!jp_digit(vm, p)) { jp_fail(p, JPE_EXP, p->i); return JV_UNDEF; }
        while (jp_digit(vm, p)) p->i++;
    }
    char b[400];
    uint32_t k = 0;
    for (uint32_t q = st; q < p->i && k + 1 < sizeof b; q++) b[k++] = (char)vm->units[p->off + q];
    b[k] = 0;
    return jv_num(strtod(b, 0));
}

/* an object's key at the reader and the colon after it: its atom; else the error kind (missing the key's quote) */
static int jp_key(js_vm *vm, jp *p, uint32_t *atom, int kind) {
    jp_ws(vm, p);
    if (p->i >= p->n || vm->units[p->off + p->i] != '"') { jp_fail(p, kind, p->i); return 0; }
    uint32_t k = jp_string(vm, p);
    if (p->err) return 0;
    jp_ws(vm, p);
    if (p->i >= p->n || vm->units[p->off + p->i] != ':') { jp_fail(p, JPE_COLON, p->i); return 0; }
    p->i++;
    *atom = js_atom(vm, k);
    return 1;
}

/* what a reviver's walk reads of the parse when JSON.parse keeps the source (V8's val_nodes): each value's record, its
   snapshot (what the parse made) and a primitive's source text (its units' span), or a container's members (pairs in
   kids from kid0: an object's key's atom, 0 for an array's element, and the member's record) */
typedef struct jp_rec { jv snap; uint32_t from, to, kid0, nkid, prim; } jp_rec;
#define JP_NOREC 0xFFFFFFFFu

static uint32_t jp_rec_add(vector<jp_rec> *recs, jv snap, uint32_t from, uint32_t to, uint32_t kid0, uint32_t nkid, uint32_t prim) {
    jp_rec r = { snap, from, to, kid0, nkid, prim };
    push_back(*recs, r);
    return (uint32_t)len(*recs) - 1;
}

/* a container the parse has open: the object or the array, an object's key whose value comes next, where its members'
   pairs start among those pending */
typedef struct jp_open { uint32_t obj, atom, mark, is_obj; } jp_open;

/* the text parsed without recursion (V8's parser keeps its own stack: no nesting is too deep for it). With recs, each
   value's record made (*root the top's), every value the heap holds pushed onto keep (an array the caller roots, so
   that a reviver's collections keep the snapshots) */
static jv jp_parse(js_vm *vm, jp *p, vector<jp_rec> *recs, vector<uint32_t> *kids, uint32_t keep, uint32_t *root) {
    vector<jp_open> open;
    vector<uint32_t> pending;
    int track = recs != 0;
    for (;;) {
        /* a value: a container opened, or a primitive read */
        jv v = JV_UNDEF;
        uint32_t vrec = 0;
        jp_ws(vm, p);
        if (p->i >= p->n) { jp_fail(p, JPE_END, p->n); return JV_UNDEF; }
        uint16_t c = vm->units[p->off + p->i];
        uint32_t from = p->i;
        if (c == '{' || c == '[') {
            uint32_t is_obj = c == '{';
            p->i++;
            uint32_t o = is_obj ? js_obj_new(vm, JK_PLAIN, vm->object_proto) : js_array_new(vm, 0);
            jp_ws(vm, p);
            if (!(p->i < p->n && vm->units[p->off + p->i] == (is_obj ? '}' : ']'))) {
                jp_open f = { o, 0, (uint32_t)len(pending), is_obj };
                if (is_obj && !jp_key(vm, p, &f.atom, JPE_PROP_OR_BRACE)) return JV_UNDEF;
                push_back(open, f);
                continue;
            }
            p->i++;
            v = jv_obj(o);
            if (track) {
                js_array_push(vm, keep, v);
                vrec = jp_rec_add(recs, v, 0, 0, 0, 0, 0);
            }
        } else {
            v = jp_primitive(vm, p, c);
            if (p->err) return JV_UNDEF;
            if (track) {
                if (jv_is_str(v)) js_array_push(vm, keep, v);
                vrec = jp_rec_add(recs, v, from, p->i, 0, 0, 1);
            }
        }
        /* the value ends: the container about it takes it, and each it closes in turn */
        for (;;) {
            if (!len(open)) {
                if (track) *root = vrec;
                return v;
            }
            size_t top = len(open) - 1;
            uint32_t o = open[top].obj, is_obj = open[top].is_obj;
            if (is_obj) js_define(vm, o, open[top].atom, v, 0);
            else js_array_push(vm, o, v);
            if (track) {
                push_back(pending, is_obj ? open[top].atom : 0u);
                push_back(pending, vrec);
            }
            jp_ws(vm, p);
            uint16_t d = p->i < p->n ? vm->units[p->off + p->i] : 0;
            if (p->i < p->n && d == ',') {
                p->i++;
                if (is_obj) {
                    uint32_t a = 0;
                    if (!jp_key(vm, p, &a, JPE_DQ_PROP)) return JV_UNDEF;
                    open[top].atom = a;
                }
                break;
            }
            if (!(p->i < p->n && d == (is_obj ? '}' : ']'))) {
                jp_fail(p, is_obj ? JPE_COMMA_BRACE : JPE_COMMA_BRACK, p->i);
                return JV_UNDEF;
            }
            p->i++;
            v = jv_obj(o);
            if (track) {
                uint32_t mark = open[top].mark, k0 = (uint32_t)(len(*kids) / 2), nk = (uint32_t)((len(pending) - mark) / 2);
                for (size_t q = mark; q < len(pending); q++) push_back(*kids, at(pending, q));
                resize(pending, mark);
                js_array_push(vm, keep, v);
                vrec = jp_rec_add(recs, v, 0, 0, k0, nk, 0);
            }
            pop_back(open);
        }
    }
}

/* SameValue: the reviver's value still the parse's (a number by its bits' meaning: -0 not 0, NaN itself) */
static int jp_same(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) {
        double x = jv_dbl(a), y = jv_dbl(b);
        if (x != x) return y != y;
        return x == y && (x != 0 || signbit(x) == signbit(y));
    }
    return js_strict_eq(vm, a, b);
}

/* the record of an object's member named by atom (the last of the parse's members of that name, as V8's table keeps
   it), JP_NOREC for one the parse did not make */
static uint32_t jp_member(const vector<uint32_t> *kids, const vector<uint32_t> *table, uint32_t kid0, uint32_t nkid, uint32_t atom) {
    if (len(*table)) {
        size_t mask = len(*table) - 1;
        for (size_t h = (atom * 2654435761u) & mask;; h = (h + 1) & mask) {
            uint32_t e = at(*table, h);
            if (!e) return JP_NOREC;
            if (at(*kids, 2 * (kid0 + e - 1)) == atom) return at(*kids, 2 * (kid0 + e - 1) + 1);
        }
    }
    for (uint32_t q = nkid; q-- > 0;)
        if (at(*kids, 2 * (kid0 + q)) == atom) return at(*kids, 2 * (kid0 + q) + 1);
    return JP_NOREC;
}

/* the reviver's result put back: undefined deletes the member, else it is defined (neither throws where it is refused) */
static int jp_store(js_vm *vm, jv obj, jv key, jv nv) {
    if (nv == JV_UNDEF) {
        js_delete(vm, jv_idx(obj), js_atom(vm, jv_idx(key)));
        return !vm->throwing;
    }
    return js_create_data(vm, obj, key, nv) >= 0;
}

/* InternalizeJSONProperty (ECMA-262 25.5.1.1) with the source text access V8 ships: holder's member name (a string)
   revived, its value's own members first. rec is the record of what the parse put there (JP_NOREC: none): while the
   value is still the parse's, its members' records go with them and a primitive's context holds its source. The value
   and an object's keys sit on the value stack while the reviver runs, so that a collection keeps them */
static jv jp_revive(js_vm *vm, jv reviver, uint32_t src, const vector<jp_rec> *recs, const vector<uint32_t> *kids, jv holder, jv name,
                    uint32_t rec, uint32_t depth) {
    if (depth > 4000 || !js_room(vm, vm->sp + 2, vm->nframes)) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    jv val = js_get_value(vm, holder, name);
    if (vm->throwing) return JV_HOLE;
    int pass = rec != JP_NOREC && jp_same(vm, val, at(*recs, rec).snap);
    uint32_t kid0 = pass ? at(*recs, rec).kid0 : 0, nkid = pass ? at(*recs, rec).nkid : 0;
    uint32_t sp0 = vm->sp;
    vm->stack[vm->sp++] = val;
    if (jv_is_obj(val)) {
        int arr = is_array_spec(vm, val);
        if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
        if (arr) {
            double n = len_of(vm, val);
            if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
            for (double i = 0; i < n; i++) {
                uint32_t kr = pass && i < nkid ? at(*kids, 2 * (kid0 + (uint32_t)i) + 1) : JP_NOREC;
                jv key = jv_str(js_num_to_str(vm, i));
                jv nv = jp_revive(vm, reviver, src, recs, kids, val, key, kr, depth + 1);
                if (vm->throwing || !jp_store(vm, val, key, nv)) { vm->sp = sp0; return JV_HOLE; }
            }
        } else {
            uint32_t keys = js_array_new(vm, 0);
            vm->stack[vm->sp++] = jv_obj(keys);
            js_own_keys(vm, jv_idx(val), 1, keys);
            if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
            /* the members' records by their atoms: a table when there are many */
            vector<uint32_t> table;
            if (nkid > 8) {
                size_t sz = 16;
                while (sz < 2 * (size_t)nkid) sz *= 2;
                resize(table, sz, 0u);
                for (uint32_t q = 0; q < nkid; q++) {
                    uint32_t atom = at(*kids, 2 * (kid0 + q));
                    for (size_t h = (atom * 2654435761u) & (sz - 1);; h = (h + 1) & (sz - 1)) {
                        uint32_t e = at(table, h);
                        if (!e || at(*kids, 2 * (kid0 + e - 1)) == atom) { table[h] = q + 1; break; }
                    }
                }
            }
            for (uint32_t q = 0; q < vm->objs[keys].b; q++) {
                jv key = js_array_get(vm, keys, q);
                uint32_t kr = pass ? jp_member(kids, &table, kid0, nkid, js_atom(vm, jv_idx(key))) : JP_NOREC;
                jv nv = jp_revive(vm, reviver, src, recs, kids, val, key, kr, depth + 1);
                if (vm->throwing || !jp_store(vm, val, key, nv)) { vm->sp = sp0; return JV_HOLE; }
            }
        }
    }
    uint32_t ctx = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    if (pass && at(*recs, rec).prim) js_define(vm, ctx, js_atom_ascii(vm, "source"), jv_str(js_str_sub(vm, src, at(*recs, rec).from, at(*recs, rec).to)), 0);
    jv argv[3] = { name, val, jv_obj(ctx) };
    jv r = js_call(vm, reviver, holder, argv, 3);
    vm->sp = sp0;
    return r;
}

/* V8's message for a JSON.parse error (its JsonParser's templates): a position's with its line and column, a token's
   with the source about it (10 units each way of a source of 21 or more, an ellipsis where it is cut) */
static uint32_t jp_message(js_vm *vm, const jp *p) {
    static const char *const at_msgs[] = { "", "", "", "Expected property name or '}'", "Expected ':' after property name",
        "Expected ',' or '}' after property value", "Expected double-quoted property name", "Expected ',' or ']' after array element",
        "Unterminated string", "Bad escaped character", "Bad Unicode escape", "Bad control character in string literal",
        "No number after minus sign", "Exponent part is missing a number", "Unterminated fractional number", "Unexpected number",
        "Unexpected non-whitespace character after JSON" };
    if (p->ek == JPE_END) return js_str_ascii(vm, "Unexpected end of JSON input");
    if (p->ek == JPE_TOKEN) {
        /* the whole source a word V8 names: "undefined" is not valid JSON */
        static const char *const special[] = { "[object Object]", "undefined", "Infinity", "NaN" };
        for (size_t q = 0; q < sizeof special / sizeof special[0]; q++) {
            size_t sl = strlen(special[q]);
            if (sl != p->n) continue;
            size_t k = 0;
            while (k < sl && vm->units[p->off + k] == (uint16_t)special[q][k]) k++;
            if (k == sl) {
                vector<uint16_t> m;
                push_back(m, (uint16_t)'"');
                for (size_t r = 0; r < sl; r++) push_back(m, (uint16_t)special[q][r]);
                const char *tail = "\" is not valid JSON";
                for (size_t r = 0; tail[r]; r++) push_back(m, (uint16_t)tail[r]);
                return js_str_new(vm, data(m), len(m));
            }
        }
        vector<uint16_t> m;
        const char *head = "Unexpected token '";
        for (size_t r = 0; head[r]; r++) push_back(m, (uint16_t)head[r]);
        uint32_t pos = p->epos, tl = 1;
        if (pos + 1 < p->n && vm->units[p->off + pos] >= 0xD800 && vm->units[p->off + pos] < 0xDC00 && vm->units[p->off + pos + 1] >= 0xDC00 &&
            vm->units[p->off + pos + 1] < 0xE000) tl = 2;
        for (uint32_t r = 0; r < tl; r++) push_back(m, vm->units[p->off + pos + r]);
        push_back(m, (uint16_t)'\'');
        push_back(m, (uint16_t)',');
        push_back(m, (uint16_t)' ');
        uint32_t s0 = 0, s1 = p->n;
        int lead = 0, trail = 0;
        if (p->n >= 21) {
            if (pos < 10) { s1 = pos + 10; trail = 1; }
            else if (pos < p->n - 10) { s0 = pos - 10; s1 = pos + 10; lead = trail = 1; }
            else { s0 = pos - 10; lead = 1; }
        }
        if (lead) for (int r = 0; r < 3; r++) push_back(m, (uint16_t)'.');
        push_back(m, (uint16_t)'"');
        for (uint32_t r = s0; r < s1 && r < p->n; r++) push_back(m, vm->units[p->off + r]);
        push_back(m, (uint16_t)'"');
        if (trail) for (int r = 0; r < 3; r++) push_back(m, (uint16_t)'.');
        const char *tail = " is not valid JSON";
        for (size_t r = 0; tail[r]; r++) push_back(m, (uint16_t)tail[r]);
        return js_str_new(vm, data(m), len(m));
    }
    /* line and column, 1-based: lines broken at LF (CR LF one break, a lone CR one) */
    uint32_t line = 1, col = 1;
    for (uint32_t r = 0; r < p->epos && r < p->n; r++) {
        uint16_t u = vm->units[p->off + r];
        if (u == '\n' || (u == '\r' && !(r + 1 < p->n && vm->units[p->off + r + 1] == '\n'))) { line++; col = 1; }
        else if (u != '\r') col++;
    }
    char b[200];
    snprintf(b, sizeof b, "%s%s at position %u (line %u column %u)", at_msgs[p->ek], p->ek == JPE_AFTER ? "" : " in JSON", p->epos, line, col);
    return js_str_ascii(vm, b);
}

/* ---- maps and sets: entries in an array, keys then values for a map, the dead holes ----
   Past COLL_LINEAR entries a collection keeps a hash index of its keys (c, an array of
   entry places plus 1: 0 empty, -1 removed; x the places used, removed ones counted),
   made at its first need and remade when half full. */
#define COLL_LINEAR 8u

static uint32_t coll_hash(js_vm *vm, jv k) {
    uint64_t h;
    if (jv_is_num(k)) {
        double d = jv_dbl(k);
        if (d != d) return 0x9E3779B9u;
        if (d == 0) d = 0;
        memcpy(&h, &d, sizeof h);
    } else if (jv_is_str(k)) return js_str_hash(vm, jv_idx(k));
    else if (jv_is_big(k)) return js_big_hash(vm, k);
    else h = k;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    return (uint32_t)h;
}

/* the index made for the collection's live entries, its size twice what they fill and more */
static void coll_index_make(js_vm *vm, uint32_t coll) {
    uint32_t arr = vm->objs[coll].a, step = vm->objs[coll].kind == JK_MAP ? 2 : 1;
    uint32_t cap = 32;
    while (cap < 4 * vm->objs[coll].b) cap *= 2;
    uint32_t ix = js_array_new(vm, cap);
    for (uint32_t i = 0; i < cap; i++) vm->slots[vm->objs[ix].a + i] = jv_num(0);
    uint32_t n = vm->objs[arr].b, used = 0;
    for (uint32_t i = 0; i < n; i += step) {
        jv k = vm->slots[vm->objs[arr].a + i];
        if (k == JV_HOLE) continue;
        uint32_t m = cap - 1;
        for (uint32_t j = coll_hash(vm, k) & m;; j = (j + 1) & m) {
            if (jv_dbl(vm->slots[vm->objs[ix].a + j]) == 0) { vm->slots[vm->objs[ix].a + j] = jv_num(i + 1); used++; break; }
        }
    }
    vm->objs[coll].c = ix;
    vm->objs[coll].x = jv_num(used);
}

/* the key's entry place, or -1; *slot the index's place for it (where it would go when absent) */
static int64_t coll_lookup(js_vm *vm, uint32_t coll, jv key, int64_t *slot) {
    uint32_t arr = vm->objs[coll].a;
    *slot = -1;
    if (vm->objs[arr].b < COLL_LINEAR * (vm->objs[coll].kind == JK_MAP ? 2u : 1u) && !vm->objs[coll].c) {
        uint32_t step = vm->objs[coll].kind == JK_MAP ? 2 : 1, n = vm->objs[arr].b;
        for (uint32_t i = 0; i < n; i += step) {
            jv k = vm->slots[vm->objs[arr].a + i];
            if (k != JV_HOLE && js_same_value_zero(vm, k, key)) return i;
        }
        return -1;
    }
    if (!vm->objs[coll].c) coll_index_make(vm, coll);
    uint32_t ix = vm->objs[coll].c, cap = vm->objs[ix].b, m = cap - 1;
    int64_t first_free = -1;
    for (uint32_t j = coll_hash(vm, key) & m, probes = 0; probes < cap; j = (j + 1) & m, probes++) {
        double e = jv_dbl(vm->slots[vm->objs[ix].a + j]);
        if (e == 0) { *slot = first_free >= 0 ? first_free : (int64_t)j; return -1; }
        if (e < 0) { if (first_free < 0) first_free = j; continue; }
        jv k = vm->slots[vm->objs[arr].a + (uint32_t)e - 1];
        if (k != JV_HOLE && js_same_value_zero(vm, k, key)) { *slot = j; return (int64_t)e - 1; }
    }
    *slot = first_free;
    return -1;
}

static int64_t coll_find(js_vm *vm, uint32_t coll, jv key) {
    int64_t slot;
    return coll_lookup(vm, coll, key, &slot);
}

/* a new key (its value with a map's) appended, the index told */
static void coll_append(js_vm *vm, uint32_t coll, jv key, jv val) {
    int64_t slot;
    if (coll_lookup(vm, coll, key, &slot) >= 0) return;
    uint32_t arr = vm->objs[coll].a;
    uint32_t at = vm->objs[arr].b;
    js_array_push(vm, arr, key);
    if (vm->objs[coll].kind == JK_MAP) js_array_push(vm, arr, val);
    vm->objs[coll].b++;
    uint32_t ix = vm->objs[coll].c;
    if (!ix) return;
    if (slot < 0 || 2 * (uint32_t)jv_dbl(vm->objs[coll].x) + 2 > vm->objs[ix].b) { coll_index_make(vm, coll); return; }
    if (jv_dbl(vm->slots[vm->objs[ix].a + (uint32_t)slot]) == 0) vm->objs[coll].x = jv_num(jv_dbl(vm->objs[coll].x) + 1);
    vm->slots[vm->objs[ix].a + (uint32_t)slot] = jv_num(at + 1);
}

/* the entry at place i removed: a hole in the entries, a removed mark in the index */
static void coll_remove(js_vm *vm, uint32_t coll, jv key, uint32_t i) {
    int64_t slot;
    coll_lookup(vm, coll, key, &slot);
    uint32_t arr = vm->objs[coll].a;
    vm->slots[vm->objs[arr].a + i] = JV_HOLE;
    if (vm->objs[coll].kind == JK_MAP) vm->slots[vm->objs[arr].a + i + 1] = JV_HOLE;
    vm->objs[coll].b--;
    uint32_t ix = vm->objs[coll].c;
    if (ix && slot >= 0) vm->slots[vm->objs[ix].a + (uint32_t)slot] = jv_num(-1);
}

static uint32_t coll_new(js_vm *vm, uint32_t kind, uint32_t proto) {
    uint32_t c = js_obj_new(vm, kind, proto);
    vm->objs[c].a = js_array_new(vm, 0);
    vm->objs[c].b = 0;
    vm->objs[c].c = 0;
    vm->objs[c].x = jv_num(0);
    return c;
}
uint32_t js_coll_new(js_vm *vm, uint32_t kind, uint32_t proto) { return coll_new(vm, kind, proto); }

/* the weak collections' operations over a Map's or a Set's (src/js/proxy.c) */
jv js_coll_op(js_vm *vm, uint32_t op, uint32_t c, jv k, jv v) {
    if (jv_is_num(k) && jv_dbl(k) == 0) k = jv_num(0);
    int64_t at_i = coll_find(vm, c, k);
    uint32_t arr = vm->objs[c].a;
    switch (op) {
    case JCOLL_GET: return at_i >= 0 && vm->objs[c].kind == JK_MAP ? vm->slots[vm->objs[arr].a + at_i + 1] : JV_UNDEF;
    case JCOLL_HAS: return jv_bool(at_i >= 0);
    case JCOLL_DELETE:
        if (at_i < 0) return JV_FALSE;
        coll_remove(vm, c, k, (uint32_t)at_i);
        return JV_TRUE;
    default:
        if (at_i >= 0 && vm->objs[c].kind == JK_MAP) vm->slots[vm->objs[arr].a + at_i + 1] = v;
        else if (at_i < 0) coll_append(vm, c, k, v);
        return JV_UNDEF;
    }
}

/* the collection a Map's or a Set's method was called on, else V8's TypeError naming the method (by its native id)
   and the receiver */
static uint32_t this_coll(js_vm *vm, jv thisv, uint32_t kind, uint32_t id) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == kind && !(vm->objs[jv_idx(thisv)].flags & JO_WEAK)) return jv_idx(thisv);
    static const char *const names[] = { "Map.prototype.get", "Map.prototype.set", "Map.prototype.has", "Map.prototype.delete",
        "Map.prototype.clear", "get Map.prototype.size", "Map.prototype.forEach", "Map.prototype.keys", "Map.prototype.values",
        "Map.prototype.entries", "Set.prototype.add", "Set.prototype.forEach", "Set.prototype.values", "Set.prototype.entries",
        "Set.prototype.has", "Set.prototype.delete", "Set.prototype.clear", "get Set.prototype.size", "Set.prototype.union",
        "Set.prototype.intersection", "Set.prototype.difference", "Set.prototype.symmetricDifference", "Set.prototype.isSubsetOf",
        "Set.prototype.isSupersetOf", "Set.prototype.isDisjointFrom" };
    char pre[96];
    snprintf(pre, sizeof pre, "Method %s called on incompatible receiver ", names[id - N_MP_GET]);
    value_error(vm, pre, thisv, "");
    return 0;
}

/* ---- dates: milliseconds since the epoch; the local time zone UTC (Chromium's on the machines it is measured on) ---- */

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (*m <= 2);
}
/* a time's field: 0 its year, 1 month (0-based), 2 date, 3 weekday, 4 hours, 5 minutes, 6 seconds, 7 ms */
static double date_field(double t, int which) {
    if (t != t) return NAN;
    int64_t ms = (int64_t)t;
    int64_t day = ms >= 0 ? ms / 86400000 : -((-ms + 86399999) / 86400000);
    int64_t rem = ms - day * 86400000;
    int64_t y;
    unsigned m, d;
    civil_from_days(day, &y, &m, &d);
    switch (which) {
    case 0: return (double)y;
    case 1: return (double)(m - 1);
    case 2: return (double)d;
    case 3: { int64_t w = (day + 4) % 7; return (double)(w < 0 ? w + 7 : w); }
    case 4: return (double)(rem / 3600000);
    case 5: return (double)(rem / 60000 % 60);
    case 6: return (double)(rem / 1000 % 60);
    default: return (double)(rem % 1000);
    }
}

/* MakeTime, MakeDay, MakeDate and TimeClip (ECMA-262 21.4.1), with V8's bounds: a year within a million of 0, a month
   within ten million */
static double date_make_time(double h, double m, double s, double ms) {
    if (!isfinite(h) || !isfinite(m) || !isfinite(s) || !isfinite(ms)) return NAN;
    return trunc(h) * 3600000.0 + trunc(m) * 60000.0 + trunc(s) * 1000.0 + trunc(ms);
}
static double date_make_day(double year, double month, double date) {
    if (!(year >= -1000000.0 && year <= 1000000.0) || !(month >= -10000000.0 && month <= 10000000.0) || !isfinite(date)) return NAN;
    int64_t y = (int64_t)year, m = (int64_t)month;
    y += m / 12;
    m %= 12;
    if (m < 0) { m += 12; y -= 1; }
    return (double)days_from_civil(y, (unsigned)m + 1, 1) + trunc(date) - 1;
}
static double date_make_date(double day, double time) {
    if (!isfinite(day) || !isfinite(time)) return NAN;
    if (time == 0.0 && day != 0.0) return day * 86400000.0;
    return time + day * 86400000.0;
}
static double date_clip(double t) {
    if (!(t >= -8.64e15 && t <= 8.64e15)) return NAN;
    return trunc(t) + 0.0;
}
/* a local time made a UTC one (the zone's UTC, so only V8's bound: ten days past the range is no time) */
static double date_utc(double t) {
    if (!(t >= -8.64e15 - 864000000.0 && t <= 8.64e15 + 864000000.0)) return NAN;
    return t;
}
/* a year argument as the constructor and Date.UTC read it: 0 to 99 the 1900s' */
static double date_full_year(double y) {
    if (y != y) return y;
    double yi = trunc(y);
    return yi >= 0 && yi <= 99 ? 1900 + yi : y;
}

/* ---- Date.parse: V8's date parser (src/date/dateparser*), its ES5 date-time strings first, then its legacy grammar ---- */

enum { DT_INVALID, DT_UNKNOWN, DT_NUMBER, DT_SYMBOL, DT_WS, DT_END, DT_KEYWORD };
enum { DK_INVALID, DK_MONTH, DK_TZ, DK_TSEP, DK_AMPM };
#define DP_NONE 0x7FFFFFFF
typedef struct dtok { int tag, kw, len, val; } dtok;
typedef struct dscan { uint32_t off, n, idx; uint32_t ch; dtok next; } dscan;

static int dp_ws_char(uint32_t c) {
    return c == 9 || c == 11 || c == 12 || c == 32 || c == 0xA0 || c == 0xFEFF || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}
static int dp_ws_or_lt(uint32_t c) { return dp_ws_char(c) || c == 10 || c == 13 || c == 0x2028 || c == 0x2029; }
static void dp_next_ch(js_vm *vm, dscan *s) {
    s->ch = s->idx < s->n ? vm->units[s->off + s->idx] : 0;
    s->idx++;
}
static const struct { char k[3]; int type, value; } dp_words[] = {
    { "jan", DK_MONTH, 1 }, { "feb", DK_MONTH, 2 }, { "mar", DK_MONTH, 3 }, { "apr", DK_MONTH, 4 }, { "may", DK_MONTH, 5 },
    { "jun", DK_MONTH, 6 }, { "jul", DK_MONTH, 7 }, { "aug", DK_MONTH, 8 }, { "sep", DK_MONTH, 9 }, { "oct", DK_MONTH, 10 },
    { "nov", DK_MONTH, 11 }, { "dec", DK_MONTH, 12 }, { "am", DK_AMPM, 0 }, { "pm", DK_AMPM, 12 }, { "ut", DK_TZ, 0 },
    { "utc", DK_TZ, 0 }, { "z", DK_TZ, 0 }, { "gmt", DK_TZ, 0 }, { "cdt", DK_TZ, -5 }, { "cst", DK_TZ, -6 }, { "edt", DK_TZ, -4 },
    { "est", DK_TZ, -5 }, { "mdt", DK_TZ, -6 }, { "mst", DK_TZ, -7 }, { "pdt", DK_TZ, -7 }, { "pst", DK_TZ, -8 }, { "t", DK_TSEP, 0 } };

static dtok dp_scan(js_vm *vm, dscan *s) {
    dtok t = { DT_UNKNOWN, 0, 1, -1 };
    uint32_t pre = s->idx;
    if (!s->ch) { t.tag = DT_END; t.len = 0; return t; }
    if (s->ch >= '0' && s->ch <= '9') {
        /* an unsigned numeral: its leading zeros skipped, nine significant digits read, the rest passed over */
        int n = 0, k = 0;
        while (s->ch == '0') dp_next_ch(vm, s);
        while (s->ch >= '0' && s->ch <= '9') {
            if (k < 9) n = n * 10 + (int)(s->ch - '0');
            k++;
            dp_next_ch(vm, s);
        }
        t.tag = DT_NUMBER;
        t.val = n;
        t.len = (int)(s->idx - pre);
        return t;
    }
    if (s->ch == ':' || s->ch == '-' || s->ch == '+' || s->ch == '.' || s->ch == ')') {
        t.tag = DT_SYMBOL;
        t.val = (int)s->ch;
        dp_next_ch(vm, s);
        return t;
    }
    if (s->ch >= 'A' && !dp_ws_char(s->ch)) {
        /* a word: its lower-cased prefix of three looked up (a longer word a month's alone) */
        uint32_t pre3[3] = { 0, 0, 0 };
        int n = 0;
        for (; s->ch >= 'A' && !dp_ws_char(s->ch); dp_next_ch(vm, s), n++)
            if (n < 3) pre3[n] = s->ch | 0x20;
        t.tag = DT_KEYWORD;
        t.kw = DK_INVALID;
        t.val = 0;
        t.len = n;
        for (size_t w = 0; w < sizeof dp_words / sizeof dp_words[0]; w++) {
            int j = 0;
            while (j < 3 && pre3[j] == (uint32_t)(uint8_t)dp_words[w].k[j]) j++;
            if (j == 3 && (n <= 3 || dp_words[w].type == DK_MONTH)) { t.kw = dp_words[w].type; t.val = dp_words[w].value; break; }
        }
        return t;
    }
    if (dp_ws_or_lt(s->ch)) {
        dp_next_ch(vm, s);
        t.tag = DT_WS;
        t.len = (int)(s->idx - pre);
        return t;
    }
    if (s->ch == '(') {
        /* parenthesized text, nested, passed over */
        int balance = 0;
        do {
            if (s->ch == ')') balance--;
            else if (s->ch == '(') balance++;
            dp_next_ch(vm, s);
        } while (balance > 0 && s->ch);
        return t;
    }
    dp_next_ch(vm, s);
    return t;
}
static dtok dp_take(js_vm *vm, dscan *s) {
    dtok r = s->next;
    s->next = dp_scan(vm, s);
    return r;
}
static int dp_is_sym(dtok t, int c) { return t.tag == DT_SYMBOL && t.val == c; }
static int dp_skip(js_vm *vm, dscan *s, int c) {
    if (!dp_is_sym(s->next, c)) return 0;
    s->next = dp_scan(vm, s);
    return 1;
}
static int dp_fixed(dtok t, int n) { return t.tag == DT_NUMBER && t.len == n; }
static int dp_sign(dtok t) { return t.tag == DT_SYMBOL && (t.val == '-' || t.val == '+'); }
static int dp_kw_z(dtok t) { return t.tag == DT_KEYWORD && t.kw == DK_TZ && t.len == 1 && t.val == 0; }
static int dp_between(int x, int lo, int hi) { return x >= lo && x <= hi; }
/* a fraction's digits as milliseconds: its first three significant ones */
static int dp_ms(dtok t) {
    int n = t.val, k = t.len;
    if (k == 1) n *= 100;
    else if (k == 2) n *= 10;
    else if (k > 3) {
        if (k > 9) k = 9;
        int f = 1;
        do { f *= 10; k--; } while (k > 3);
        n /= f;
    }
    return n;
}

/* the composers: a day's up to three numbers and its month's name, a time's four fields and its am or pm, a zone */
typedef struct dday { int comp[3], index, named_month, iso; } dday;
typedef struct dtime { int comp[4], index, hour_offset; } dtime;
typedef struct dzone { int sign, hour, minute; } dzone;

static int dd_add(dday *d, int n) { if (d->index >= 3) return 0; d->comp[d->index++] = n; return 1; }
static int dt_add(dtime *t, int n) { if (t->index >= 4) return 0; t->comp[t->index++] = n; return 1; }
static int dt_add_final(dtime *t, int n) {
    if (!dt_add(t, n)) return 0;
    while (t->index < 4) t->comp[t->index++] = 0;
    return 1;
}
static int dt_expecting(const dtime *t, int n) {
    return (t->index == 1 && dp_between(n, 0, 59)) || (t->index == 2 && dp_between(n, 0, 59)) || (t->index == 3 && dp_between(n, 0, 999));
}
static void dz_set(dzone *z, int hours) { z->sign = hours < 0 ? -1 : 1; z->hour = hours * z->sign; z->minute = 0; }
static int dz_expecting(const dzone *z, int n) { return z->hour != DP_NONE && z->minute == DP_NONE && dp_between(n, 0, 59); }
static int dz_utc(const dzone *z) { return z->hour == 0 && z->minute == 0; }

/* ParseES5DateTime: the token after what it read, an END when it read the whole string, an INVALID when it is no date */
static dtok dp_es5(js_vm *vm, dscan *s, dday *day, dtime *time, dzone *tz) {
    dtok bad = { DT_INVALID, 0, 0, -1 }, end = { DT_END, 0, 0, -1 };
    if (dp_sign(s->next)) {
        dtok st = dp_take(vm, s);
        if (!dp_fixed(s->next, 6)) return st;
        int sign = 44 - st.val, year = dp_take(vm, s).val;
        if (sign < 0 && year == 0) return st;
        dd_add(day, sign * year);
    } else if (dp_fixed(s->next, 4)) dd_add(day, dp_take(vm, s).val);
    else return dp_take(vm, s);
    if (dp_skip(vm, s, '-')) {
        if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 1, 12)) return dp_take(vm, s);
        dd_add(day, dp_take(vm, s).val);
        if (dp_skip(vm, s, '-')) {
            if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 1, 31)) return dp_take(vm, s);
            dd_add(day, dp_take(vm, s).val);
        }
    }
    if (!(s->next.tag == DT_KEYWORD && s->next.kw == DK_TSEP)) {
        if (s->next.tag != DT_END) return dp_take(vm, s);
    } else {
        dp_take(vm, s);
        if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 0, 24)) return bad;
        int h24 = s->next.val == 24;
        dt_add(time, dp_take(vm, s).val);
        if (!dp_skip(vm, s, ':')) return bad;
        if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 0, 59) || (h24 && s->next.val > 0)) return bad;
        dt_add(time, dp_take(vm, s).val);
        if (dp_skip(vm, s, ':')) {
            if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 0, 59) || (h24 && s->next.val > 0)) return bad;
            dt_add(time, dp_take(vm, s).val);
            if (dp_skip(vm, s, '.')) {
                if (s->next.tag != DT_NUMBER || (h24 && s->next.val > 0)) return bad;
                dt_add(time, dp_ms(dp_take(vm, s)));
            }
        }
        if (dp_kw_z(s->next)) {
            dp_take(vm, s);
            dz_set(tz, 0);
        } else if (dp_is_sym(s->next, '+') || dp_is_sym(s->next, '-')) {
            tz->sign = dp_take(vm, s).val == '+' ? 1 : -1;
            if (dp_fixed(s->next, 4)) {
                int hm = dp_take(vm, s).val, hh = hm / 100, mm = hm % 100;
                if (!dp_between(hh, 0, 23) || !dp_between(mm, 0, 59)) return bad;
                tz->hour = hh;
                tz->minute = mm;
            } else {
                if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 0, 23)) return bad;
                tz->hour = dp_take(vm, s).val;
                if (!dp_skip(vm, s, ':')) return bad;
                if (!dp_fixed(s->next, 2) || !dp_between(s->next.val, 0, 59)) return bad;
                tz->minute = dp_take(vm, s).val;
            }
        }
        if (s->next.tag != DT_END) return bad;
    }
    /* a date alone is UTC's, a date and a time the local zone's */
    if (tz->hour == DP_NONE && time->index == 0) dz_set(tz, 0);
    day->iso = 1;
    return end;
}

/* the string s's time (NaN: no date V8 reads) */
static double date_parse(js_vm *vm, uint32_t str) {
    js_str_flatten(vm, str);
    dscan sc;
    memset(&sc, 0, sizeof sc);
    sc.off = vm->strs[str].off;
    sc.n = vm->strs[str].len;
    dp_next_ch(vm, &sc);
    sc.next = dp_scan(vm, &sc);
    dday day = { { 0, 0, 0 }, 0, DP_NONE, 0 };
    dtime time = { { 0, 0, 0, 0 }, 0, DP_NONE };
    dzone tz = { DP_NONE, DP_NONE, DP_NONE };
    dtok tok = dp_es5(vm, &sc, &day, &time, &tz);
    if (tok.tag == DT_INVALID) return NAN;
    int has_number = day.index > 0;
    for (; tok.tag != DT_END; tok = dp_take(vm, &sc)) {
        if (tok.tag == DT_NUMBER) {
            has_number = 1;
            int n = tok.val;
            if (dp_skip(vm, &sc, ':')) {
                if (dp_skip(vm, &sc, ':')) {
                    if (time.index) return NAN;
                    dt_add(&time, n);
                    dt_add(&time, 0);
                } else {
                    if (!dt_add(&time, n)) return NAN;
                    if (dp_is_sym(sc.next, '.')) dp_take(vm, &sc);
                }
            } else if (dp_skip(vm, &sc, '.') && dt_expecting(&time, n)) {
                dt_add(&time, n);
                if (sc.next.tag != DT_NUMBER) return NAN;
                int ms = dp_ms(dp_take(vm, &sc));
                if (ms < 0) return NAN;
                dt_add_final(&time, ms);
            } else if (dz_expecting(&tz, n)) {
                tz.minute = n;
            } else if (dt_expecting(&time, n)) {
                dt_add_final(&time, n);
                /* a time ends at the end, white space, a Z or a sign */
                dtok pk = sc.next;
                if (pk.tag != DT_END && pk.tag != DT_WS && !dp_kw_z(pk) && !dp_sign(pk)) return NAN;
            } else {
                if (!dd_add(&day, n)) return NAN;
                dp_skip(vm, &sc, '-');
            }
        } else if (tok.tag == DT_KEYWORD) {
            if (tok.kw == DK_AMPM && time.index) time.hour_offset = tok.val;
            else if (tok.kw == DK_MONTH) {
                day.named_month = tok.val;
                dp_skip(vm, &sc, '-');
            } else if (tok.kw == DK_TZ && has_number) dz_set(&tz, tok.val);
            else {
                /* a word no date knows: none once a number came, nor one a number follows at once */
                if (has_number) return NAN;
                if (sc.next.tag == DT_NUMBER) return NAN;
            }
        } else if (dp_sign(tok) && (dz_utc(&tz) || time.index)) {
            /* an offset, after UTC or a time */
            tz.sign = 44 - tok.val < 0 ? -1 : 1;
            int n = 0, k = 0;
            if (sc.next.tag == DT_NUMBER) {
                dtok nt = dp_take(vm, &sc);
                k = nt.len;
                n = nt.val;
            }
            has_number = 1;
            if (dp_is_sym(sc.next, ':')) {
                tz.hour = n;
                tz.minute = DP_NONE;
            } else if (k == 1 || k == 2) {
                tz.hour = n;
                tz.minute = 0;
            } else if (k == 3 || k == 4) {
                tz.hour = n / 100;
                tz.minute = n % 100;
            } else return NAN;
        } else if ((dp_sign(tok) || dp_is_sym(tok, ')')) && has_number) return NAN;
    }
    /* the day: its numbers read as YMD, MD(Y), DMY or the like as V8 orders them */
    if (day.index < 1) return NAN;
    while (day.index < 3) day.comp[day.index++] = 1;
    int year = 0, month = DP_NONE, date = DP_NONE;
    if (day.named_month == DP_NONE) {
        if (day.iso || (day.index == 3 && !dp_between(day.comp[0], 1, 31))) { year = day.comp[0]; month = day.comp[1]; date = day.comp[2]; }
        else { month = day.comp[0]; date = day.comp[1]; if (day.index == 3) year = day.comp[2]; }
    } else {
        month = day.named_month;
        if (day.index == 1) date = day.comp[0];
        else if (!dp_between(day.comp[0], 1, 31)) { year = day.comp[0]; date = day.comp[1]; }
        else { date = day.comp[0]; year = day.comp[1]; }
    }
    if (!day.iso) {
        if (dp_between(year, 0, 49)) year += 2000;
        else if (dp_between(year, 50, 99)) year += 1900;
    }
    if (!dp_between(month, 1, 12) || !dp_between(date, 1, 31)) return NAN;
    /* the time */
    while (time.index < 4) time.comp[time.index++] = 0;
    int hour = time.comp[0], minute = time.comp[1], second = time.comp[2], milli = time.comp[3];
    if (time.hour_offset != DP_NONE) {
        if (!dp_between(hour, 0, 12)) return NAN;
        hour %= 12;
        hour += time.hour_offset;
    }
    if (!dp_between(hour, 0, 23) || !dp_between(minute, 0, 59) || !dp_between(second, 0, 59) || !dp_between(milli, 0, 999))
        if (hour != 24 || minute != 0 || second != 0 || milli != 0) return NAN;
    double t = date_make_date(date_make_day(year, month - 1, date), date_make_time(hour, minute, second, milli));
    if (tz.sign != DP_NONE) {
        int64_t secs = (int64_t)(tz.hour == DP_NONE ? 0 : tz.hour) * 3600 + (int64_t)(tz.minute == DP_NONE ? 0 : tz.minute) * 60;
        if (secs > 0x3FFFFFFF) return NAN;
        t -= (double)(tz.sign < 0 ? -secs : secs) * 1000.0;
        if (!(t >= -8.64e15 && t <= 8.64e15)) return NAN;
    } else t = date_utc(t);
    return date_clip(t);
}

/* ---- a date written as V8 writes it: toString (0), toDateString (1), toTimeString (2), toUTCString (3), toISOString
   (4); the local zone UTC, named as ICU names it ---- */
static uint32_t date_text(js_vm *vm, double t, int which) {
    static const char *const wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (t != t) return js_str_ascii(vm, "Invalid Date");
    int y = (int)date_field(t, 0), m = (int)date_field(t, 1), d = (int)date_field(t, 2), w = (int)date_field(t, 3);
    int h = (int)date_field(t, 4), mi = (int)date_field(t, 5), s = (int)date_field(t, 6), ms = (int)date_field(t, 7);
    char b[128];
    const char *zone = "GMT+0000 (Coordinated Universal Time)";
    if (which == 0) snprintf(b, sizeof b, y < 0 ? "%s %s %02d %05d %02d:%02d:%02d %s" : "%s %s %02d %04d %02d:%02d:%02d %s", wd[w], mo[m], d, y, h, mi, s, zone);
    else if (which == 1) snprintf(b, sizeof b, y < 0 ? "%s %s %02d %05d" : "%s %s %02d %04d", wd[w], mo[m], d, y);
    else if (which == 2) snprintf(b, sizeof b, "%02d:%02d:%02d %s", h, mi, s, zone);
    else if (which == 3) snprintf(b, sizeof b, y < 0 ? "%s, %02d %s %05d %02d:%02d:%02d GMT" : "%s, %02d %s %04d %02d:%02d:%02d GMT", wd[w], d, mo[m], y, h, mi, s);
    else if (y >= 0 && y <= 9999) snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, m + 1, d, h, mi, s, ms);
    else if (y < 0) snprintf(b, sizeof b, "-%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", -y, m + 1, d, h, mi, s, ms);
    else snprintf(b, sizeof b, "+%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, m + 1, d, h, mi, s, ms);
    return js_str_ascii(vm, b);
}

/* the Date a method was called on, its time value in *t; else V8's TypeError: the getters' "this is not a Date
   object." (name 0), the others' naming the method and the receiver */
static int this_date(js_vm *vm, jv thisv, const char *name, double *t) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_DATE) { *t = jv_dbl(vm->objs[jv_idx(thisv)].x); return 1; }
    if (!name) js_throw_msg(vm, vm->type_error_proto, "this is not a Date object.");
    else {
        char pre[96];
        snprintf(pre, sizeof pre, "Method Date.prototype.%s called on incompatible receiver ", name);
        value_error(vm, pre, thisv, "");
    }
    return 0;
}

/* ---- timers ---- */

static jv add_timer(js_vm *vm, uint32_t args, uint32_t argc, int repeat) {
    jv fn = arg(vm, args, argc, 0);
    double delay = argc > 1 ? js_to_number(vm, arg(vm, args, argc, 1)) : 0;
    if (delay != delay || delay < 0) delay = 0;
    if (!js_is_callable(vm, fn)) return jv_num(0);
    js_timer t;
    memset(&t, 0, sizeof t);
    t.id = ++vm->timer_id;
    t.due = vm->now + delay;
    t.interval = repeat ? (delay < 1 ? 1 : delay) : -1;
    t.fn = fn;
    t.live = 1;
    t.seq = ++vm->timer_seq;
    t.run = vm->cur_run;
    if (argc > 2) {
        uint32_t extra = js_array_new(vm, 0);
        for (uint32_t i = 2; i < argc; i++) js_array_push(vm, extra, arg(vm, args, argc, i));
        t.args = extra;
        t.nargs = argc - 2;
    }
    push_back(vm->timers, t);
    return jv_num(t.id);
}

/* ---- the console ---- */

static jv console_out(js_vm *vm, uint32_t level, uint32_t args, uint32_t argc) {
    uint32_t line = A_EMPTY;
    for (uint32_t i = 0; i < argc; i++) {
        jv v = arg(vm, args, argc, i);
        uint32_t s;
        if (jv_is_obj(v) && !js_is_callable(vm, v) && vm->objs[jv_idx(v)].kind != JK_ERROR) {
            vector<uint16_t> o;
            vector<uint32_t> st;
            if (!json_write(vm, v, &o, 0, 0, &st)) out_ascii(&o, "undefined");
            if (vm->throwing) { vm->throwing = 0; vm->exception = JV_UNDEF; clear(o); out_ascii(&o, "[object]"); }
            s = js_str_new(vm, data(o), len(o));
        } else if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_ERROR) {
            vector<char> t;
            js_error_text(vm, v, &t);
            s = js_str_utf8(vm, data(t), len(t));
        } else if (jv_is_sym(v)) {
            s = js_sym_text(vm, jv_idx(v));
        } else if (jv_is_big(v)) {
            /* a BigInt written as V8's console writes one: its digits and an n */
            s = js_str_concat(vm, js_big_to_string(vm, v, 10), js_str_ascii(vm, "n"));
        } else if (jv_is_num(v) && jv_dbl(v) == 0 && signbit(jv_dbl(v))) {
            /* -0 as the console shows it (its string is 0) */
            s = js_str_ascii(vm, "-0");
        } else {
            s = js_to_string(vm, v);
            if (!s) return JV_HOLE;
        }
        line = i ? js_str_concat(vm, js_str_concat(vm, line, str_of_ascii(vm, " ")), s) : s;
    }
    js_console(vm, level, line);
    return JV_UNDEF;
}

/* ---- sorting: a stable merge sort, the comparator's or the strings' order ---- */

static int sort_less(js_vm *vm, jv cmp, jv a, jv b, int *err) {
    if (a == JV_UNDEF) return 0;
    if (b == JV_UNDEF) return 1;
    if (cmp != JV_UNDEF) {
        jv args[2] = { a, b };
        jv r = js_call(vm, cmp, JV_UNDEF, args, 2);
        if (vm->throwing) { *err = 1; return 0; }
        double d = js_to_number(vm, r);
        return d < 0;
    }
    if (jv_is_num(a) && jv_is_num(b)) {
        char x[40], y[40];
        js_num_format(jv_dbl(a), x, sizeof x);
        js_num_format(jv_dbl(b), y, sizeof y);
        return strcmp(x, y) < 0;
    }
    uint32_t sa = js_to_string(vm, a), sb = js_to_string(vm, b);
    if (!sa || !sb) { *err = 1; return 0; }
    return js_str_cmp(vm, sa, sb) < 0;
}

static void merge_sort(js_vm *vm, vector<jv> *v, vector<jv> *tmp, size_t lo, size_t hi, jv cmp, int *err) {
    if (hi - lo < 2 || *err) return;
    if (hi - lo <= 8) {
        for (size_t i = lo + 1; i < hi && !*err; i++) {
            jv x = at(*v, i);
            size_t j = i;
            while (j > lo && sort_less(vm, cmp, x, at(*v, j - 1), err)) { (*v)[j] = at(*v, j - 1); j--; }
            (*v)[j] = x;
        }
        return;
    }
    size_t mid = (lo + hi) / 2;
    merge_sort(vm, v, tmp, lo, mid, cmp, err);
    merge_sort(vm, v, tmp, mid, hi, cmp, err);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi && !*err) {
        if (sort_less(vm, cmp, at(*v, j), at(*v, i), err)) (*tmp)[k++] = at(*v, j++);
        else (*tmp)[k++] = at(*v, i++);
    }
    while (i < mid) (*tmp)[k++] = at(*v, i++);
    while (j < hi) (*tmp)[k++] = at(*v, j++);
    for (size_t q = lo; q < hi; q++) (*v)[q] = at(*tmp, q);
}

/* ---- Array.prototype.flat ---- */

static void flat_into(js_vm *vm, uint32_t out, jv src, double depth) {
    uint32_t n = (uint32_t)len_of(vm, src);
    for (uint32_t i = 0; i < n && !vm->throwing; i++) {
        /* what is not there (a hole) is skipped */
        int has = elem_present(vm, src, i);
        if (vm->throwing || !has) continue;
        jv v = elem_of(vm, src, i);
        if (vm->throwing) return;
        if (depth >= 1 && is_array_spec(vm, v)) flat_into(vm, out, v, depth - 1);
        else js_array_push(vm, out, v);
    }
}

/* ---- the natives ---- */

static jv make_error(js_vm *vm, uint32_t proto, uint32_t args, uint32_t argc) {
    jv m = arg(vm, args, argc, 0);
    uint32_t ms = A_EMPTY;
    if (m != JV_UNDEF) { ms = js_to_string(vm, m); if (!ms) return JV_HOLE; }
    uint32_t e = js_error_new(vm, proto, ms);
    jv opts = arg(vm, args, argc, 1);
    if (jv_is_obj(opts) && js_has(vm, jv_idx(opts), A_CAUSE)) js_define(vm, e, A_CAUSE, js_get(vm, opts, A_CAUSE), JS_SHAPE_HIDDEN);
    return jv_obj(e);
}

static jv obj_keys_like(js_vm *vm, jv o, int which) {
    jv ov = js_to_object(vm, o);
    if (vm->throwing) return JV_HOLE;
    uint32_t keys = js_array_new(vm, 0);
    js_own_keys(vm, jv_idx(ov), 1, keys);
    if (which == 0) return jv_obj(keys);
    uint32_t out = js_array_new(vm, 0);
    for (uint32_t i = 0; i < vm->objs[keys].b && !vm->throwing; i++) {
        jv k = js_array_get(vm, keys, i);
        jv v = js_get(vm, ov, js_atom(vm, jv_idx(k)));
        if (which == 1) js_array_push(vm, out, v);
        else {
            uint32_t pair = js_array_new(vm, 0);
            js_array_push(vm, pair, k);
            js_array_push(vm, pair, v);
            js_array_push(vm, out, jv_obj(pair));
        }
    }
    return jv_obj(out);
}


/* ---- iteration, for the builtins that take an iterable ---- */

/* {value, done}: an iterator's result */
static jv iter_result(js_vm *vm, jv v, int done) { return js_iter_result(vm, v, done); }
/* every value an iterable gives, pushed onto the array out: 0 when it threw */
static int iterate_into(js_vm *vm, jv v, uint32_t out) {
    jv it = js_iter_of(vm, v);
    if (vm->throwing) return 0;
    jv x;
    while (js_iter_step(vm, jv_idx(it), &x)) js_array_push(vm, out, x);
    return !vm->throwing;
}

/* ---- promises (ECMA-262 27.2) ----
   A promise is its state, its result and its reactions: four values each (the handlers
   for fulfilment and rejection, the derived promise or the async run to resume, and
   which of the two). Settling one queues a job per reaction; a handler runs only as
   a job, at a checkpoint, never inside the code that settled it. */

uint32_t js_promise_new(js_vm *vm) {
    uint32_t p = js_obj_new(vm, JK_PROMISE, vm->promise_proto);
    vm->objs[p].a = JP_PENDING;
    vm->objs[p].x = JV_UNDEF;
    return p;
}

void js_job_push(js_vm *vm, uint32_t kind, uint32_t mode, jv fn, jv arg, jv target) {
    js_job j = { kind, mode, vm->cur_run, fn, arg, target };
    push_back(vm->jobs, j);
}

static void promise_queue(js_vm *vm, jv on_ok, jv on_err, jv target, int resume, int rejected, jv v) {
    if (resume) js_job_push(vm, JOB_RESUME, (uint32_t)rejected, JV_UNDEF, v, target);
    else js_job_push(vm, JOB_REACTION, (uint32_t)rejected, rejected ? on_err : on_ok, v, target);
}

void js_promise_settle(js_vm *vm, uint32_t p, int rejected, jv v) {
    if (vm->objs[p].kind != JK_PROMISE || vm->objs[p].a != JP_PENDING) return;
    vm->objs[p].a = rejected ? JP_REJECTED : JP_FULFILLED;
    vm->objs[p].x = v;
    uint32_t rs = vm->objs[p].b;
    vm->objs[p].b = 0;
    if (rs) {
        uint32_t n = vm->objs[rs].b;
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            jv on_ok = js_array_get(vm, rs, i), on_err = js_array_get(vm, rs, i + 1), target = js_array_get(vm, rs, i + 2);
            int resume = jv_dbl(js_array_get(vm, rs, i + 3)) != 0;
            promise_queue(vm, on_ok, on_err, target, resume, rejected, v);
        }
    }
    if (rejected && !(vm->objs[p].c & JPF_HANDLED)) { push_back(vm->rejected, p); push_back(vm->rejected, vm->cur_run); }
}

void js_promise_react(js_vm *vm, uint32_t p, jv on_ok, jv on_err, jv target, int resume) {
    vm->objs[p].c |= JPF_HANDLED;
    if (vm->objs[p].a == JP_PENDING) {
        uint32_t rs = vm->objs[p].b;
        if (!rs) { rs = js_array_new(vm, 0); vm->objs[p].b = rs; }
        js_array_push(vm, rs, on_ok);
        js_array_push(vm, rs, on_err);
        js_array_push(vm, rs, target);
        js_array_push(vm, rs, jv_num(resume ? 1 : 0));
        return;
    }
    promise_queue(vm, on_ok, on_err, target, resume, vm->objs[p].a == JP_REJECTED, vm->objs[p].x);
}

void js_promise_resolve_with(js_vm *vm, uint32_t p, jv v) {
    if (vm->objs[p].a != JP_PENDING) return;
    if (v == jv_obj(p)) {
        uint32_t m = js_str_ascii(vm, "Chaining cycle detected for promise #<Promise>");
        js_promise_settle(vm, p, 1, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
        return;
    }
    if (jv_is_obj(v)) {
        /* a native promise is followed without a job of its own for its then */
        uint32_t o = jv_idx(v);
        if (vm->objs[o].kind == JK_PROMISE && vm->objs[o].proto == vm->promise_proto) {
            js_job_push(vm, JOB_THENABLE, 1, JV_UNDEF, v, jv_obj(p));
            return;
        }
        jv then = js_get(vm, v, A_THEN);
        if (vm->throwing) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, e);
            return;
        }
        if (js_is_callable(vm, then)) { js_job_push(vm, JOB_THENABLE, 0, then, v, jv_obj(p)); return; }
    }
    js_promise_settle(vm, p, 0, v);
}

/* PromiseResolve(%Promise%, v): v itself when it is a promise %Promise% made (its constructor Promise), else a new
   promise resolved with it; what reading the constructor throws, that promise's rejection */
static int promise_plain(js_vm *vm, jv v);
uint32_t js_promise_of(js_vm *vm, jv v) {
    if (promise_plain(vm, v)) return jv_idx(v);
    if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_PROMISE) {
        jv c = js_get(vm, v, A_CONSTRUCTOR);
        if (vm->throwing == 1) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            uint32_t p = js_promise_new(vm);
            js_promise_settle(vm, p, 1, e);
            return p;
        }
        if (c == jv_obj(vm->promise_ctor)) return jv_idx(v);
    }
    uint32_t p = js_promise_new(vm);
    js_promise_resolve_with(vm, p, v);
    return p;
}

/* a promise's resolving functions: a pair sharing one record whose a says one ran */
static void promise_resolvers(js_vm *vm, uint32_t p, jv *res, jv *rej) {
    uint32_t rec = js_obj_new(vm, JK_PLAIN, 0);
    uint32_t f = js_native_new(vm, N_PR_RESOLVE_FN, A_EMPTY, 1);
    vm->objs[f].x = jv_obj(p);
    vm->objs[f].c = 0;
    uint32_t g = js_native_new(vm, N_PR_REJECT_FN, A_EMPTY, 1);
    vm->objs[g].x = jv_obj(p);
    vm->objs[g].c = 0;
    /* the shared record in each one's own property, hidden */
    js_define(vm, f, A_INDEX, jv_obj(rec), JS_SHAPE_HIDDEN);
    js_define(vm, g, A_INDEX, jv_obj(rec), JS_SHAPE_HIDDEN);
    *res = jv_obj(f);
    *rej = jv_obj(g);
}

/* ---- promise capabilities (ECMA-262 27.2.1.5) ----
   NewPromiseCapability(C): for %Promise% a native promise, which the engine settles (a reaction's target is the
   promise itself); for any other constructor, C constructed with an executor that takes its resolving functions, the
   target then the capability's record (an array: the promise, its resolve, its reject), settled through them */
static jv promise_cap(js_vm *vm, jv c, jv *target) {
    if (c == jv_obj(vm->promise_ctor)) {
        uint32_t p = js_promise_new(vm);
        *target = jv_obj(p);
        return jv_obj(p);
    }
    if (!js_is_constructor(vm, c)) return js_not_constructor(vm, c);
    uint32_t rec = js_array_new(vm, 0);
    js_array_push(vm, rec, JV_UNDEF);
    js_array_push(vm, rec, JV_UNDEF);
    js_array_push(vm, rec, JV_UNDEF);
    uint32_t ex = js_native_new(vm, N_PR_CAP_EXEC, A_EMPTY, 2);
    vm->objs[ex].x = jv_obj(rec);
    jv exv = jv_obj(ex);
    jv p = js_construct(vm, c, &exv, 1, c);
    if (vm->throwing) return JV_HOLE;
    if (!js_is_callable(vm, js_array_get(vm, rec, 1)) || !js_is_callable(vm, js_array_get(vm, rec, 2)))
        return js_throw_msg(vm, vm->type_error_proto, "Promise resolve or reject function is not callable");
    js_array_set(vm, rec, 0, p);
    *target = jv_obj(rec);
    return p;
}

/* a capability's target resolved (rejected 0) or rejected with v: a native promise by the engine, a constructor's
   through its functions (what they throw is the caller's) */
static void promise_target_settle(js_vm *vm, jv target, int rejected, jv v) {
    uint32_t d = jv_idx(target);
    if (vm->objs[d].kind == JK_PROMISE) {
        if (rejected) js_promise_settle(vm, d, 1, v);
        else js_promise_resolve_with(vm, d, v);
        return;
    }
    jv a1[1] = { v };
    js_call(vm, js_array_get(vm, d, rejected ? 2 : 1), JV_UNDEF, a1, 1);
}

/* the resolving functions of a capability's target: a native promise's made for it, a constructor's its own */
static void promise_target_fns(js_vm *vm, jv target, jv *res, jv *rej) {
    uint32_t d = jv_idx(target);
    if (vm->objs[d].kind == JK_PROMISE) { promise_resolvers(vm, d, res, rej); return; }
    *res = js_array_get(vm, d, 1);
    *rej = js_array_get(vm, d, 2);
}

/* the promise protector (V8's): while Promise.prototype.constructor is still Promise and Promise[Symbol.species] still
   its own getter, neither object's properties changed since the engine made them, a native promise of Promise.prototype
   with no property of its own is %Promise%'s for SpeciesConstructor and PromiseResolve: no lookup need run */
static int promise_plain(js_vm *vm, jv v) {
    if (!jv_is_obj(v) || !vm->pr_root_shape) return 0;
    uint32_t p = jv_idx(v), pp = vm->promise_proto, pc = vm->promise_ctor;
    return vm->objs[p].kind == JK_PROMISE && vm->objs[p].shape == vm->pr_root_shape && vm->objs[pp].shape == vm->pr_proto_shape &&
           vm->slots[vm->objs[pp].slots + vm->pr_ctor_slot] == jv_obj(pc) && vm->objs[pc].shape == vm->pr_ctor_shape &&
           vm->slots[vm->objs[pc].slots + vm->pr_species_slot] == jv_obj(vm->pr_species_acc) &&
           vm->objs[vm->pr_species_acc].x == jv_obj(vm->pr_species_get);
}

/* SpeciesConstructor(O, default) (ECMA-262 7.3.22), in V8's words */
static jv species_ctor(js_vm *vm, jv o, jv dflt) {
    if (dflt == jv_obj(vm->promise_ctor) && promise_plain(vm, o)) return dflt;
    jv c = js_get(vm, o, A_CONSTRUCTOR);
    if (vm->throwing) return JV_HOLE;
    if (c == JV_UNDEF) return dflt;
    if (!jv_is_obj(c)) return js_throw_msg(vm, vm->type_error_proto, "The .constructor property is not an object");
    jv sp = js_get(vm, c, JS_SYM_KEY | JSYM_SPECIES);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_nullish(sp)) return dflt;
    if (js_is_constructor(vm, sp)) return sp;
    return js_throw_msg(vm, vm->type_error_proto, "object.constructor[Symbol.species] is not a constructor");
}

/* PromiseResolve(C, x) (ECMA-262 27.2.4.7.1): x itself when it is a promise C made, else a promise of C resolved
   with it */
static jv promise_resolve_c(js_vm *vm, jv c, jv x) {
    if (c == jv_obj(vm->promise_ctor) && promise_plain(vm, x)) return x;
    if (jv_is_obj(x) && vm->objs[jv_idx(x)].kind == JK_PROMISE) {
        jv xc = js_get(vm, x, A_CONSTRUCTOR);
        if (vm->throwing) return JV_HOLE;
        if (xc == c) return x;
    }
    jv target;
    jv p = promise_cap(vm, c, &target);
    if (vm->throwing) return JV_HOLE;
    promise_target_settle(vm, target, 0, x);
    return vm->throwing ? JV_HOLE : p;
}

/* Invoke(v, "then", a, b): what it returns, JV_HOLE with what it threw */
static jv promise_invoke_then(js_vm *vm, jv v, jv a, jv b, uint32_t argc) {
    jv then = js_get_value(vm, v, jv_str(A_THEN));
    if (vm->throwing) return JV_HOLE;
    if (!js_is_callable(vm, then)) return js_not_function(vm, then);
    jv a2[2] = { a, b };
    return js_call(vm, then, v, a2, argc);
}

/* the error a job's code threw and nothing caught: the host's to record */
static void job_uncaught(js_vm *vm) {
    if (vm->throwing != 1) return;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    js_report_error(vm, e, JS_PHASE_PROMISE);
}

static void run_job(js_vm *vm, js_job j) {
    if (j.kind == JOB_REACTION) {
        /* the derived promise or capability (none: a reaction for its handlers alone) */
        int d = jv_is_obj(j.target);
        if (js_is_callable(vm, j.fn)) {
            jv a1[1] = { j.arg };
            jv r = js_call(vm, j.fn, JV_UNDEF, a1, 1);
            if (vm->throwing == 2) return;
            if (vm->throwing) {
                jv e = vm->exception;
                vm->throwing = 0;
                vm->exception = JV_UNDEF;
                if (d) promise_target_settle(vm, j.target, 1, e);
                else js_report_error(vm, e, JS_PHASE_PROMISE);
                return;
            }
            if (d) promise_target_settle(vm, j.target, 0, r);
        } else if (d) promise_target_settle(vm, j.target, (int)j.mode, j.arg);
        return;
    }
    if (j.kind == JOB_RESUME) {
        int done = 0;
        js_gen_resume(vm, jv_idx(j.target), j.arg, (int)j.mode, &done);
        job_uncaught(vm);
        return;
    }
    if (j.kind == JOB_THENABLE) {
        uint32_t p = jv_idx(j.target);
        if (j.mode == 1) {
            /* a native promise followed: its settlement becomes this one's */
            js_promise_react(vm, jv_idx(j.arg), JV_UNDEF, JV_UNDEF, j.target, 0);
            return;
        }
        jv res, rej;
        promise_resolvers(vm, p, &res, &rej);
        jv a2[2] = { res, rej };
        js_call(vm, j.fn, j.arg, a2, 2);
        if (vm->throwing == 1) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            jv recv;
            uint32_t f;
            if (js_get_own(vm, jv_idx(rej), A_INDEX, &recv, &f) && jv_is_obj(recv) && !vm->objs[jv_idx(recv)].a) {
                vm->objs[jv_idx(recv)].a = 1;
                js_promise_settle(vm, p, 1, e);
            }
        }
        return;
    }
    js_call(vm, j.fn, JV_UNDEF, 0, 0);
    job_uncaught(vm);
}

void js_run_jobs(js_vm *vm) {
    if (vm->nframes != 0 || vm->in_jobs || vm->throwing) return;
    vm->in_jobs = 1;
    uint32_t saved = vm->cur_run;
    while (vm->jobs_head < len(vm->jobs) && vm->throwing != 2) {
        js_job j = vm->jobs[vm->jobs_head];
        vm->jobs_head++;
        vm->cur_run = j.run;
        /* the job a root while it runs: its derived promise may be held nowhere else */
        vm->job_running = j;
        run_job(vm, j);
        vm->job_running.fn = vm->job_running.arg = vm->job_running.target = JV_UNDEF;
        if (vm->throwing == 1) job_uncaught(vm);
    }
    if (vm->jobs_head >= len(vm->jobs)) { clear(vm->jobs); vm->jobs_head = 0; }
    /* a rejection still without a handler when the queue is empty: reported once */
    for (size_t i = 0; i + 1 < len(vm->rejected) && vm->throwing != 2; i += 2) {
        uint32_t p = vm->rejected[i];
        if (vm->objs[p].kind != JK_PROMISE || (vm->objs[p].c & (JPF_HANDLED | JPF_REPORTED))) continue;
        vm->objs[p].c |= JPF_REPORTED;
        vm->cur_run = vm->rejected[i + 1];
        js_report_error(vm, vm->objs[p].x, JS_PHASE_PROMISE);
    }
    clear(vm->rejected);
    /* ClearKeptObjects: the WeakRefs' targets let go at the checkpoint's end */
    vm->kept = 0;
    vm->cur_run = saved;
    vm->in_jobs = 0;
}

/* ---- async generators (ECMA-262 27.6) ----
   Each request (next, throw or return, with its value and promise) joins the queue; the
   body answers the first one at each yield, at its end and when it throws. A request
   resumes the body only when it is suspended at a yield (or not yet started); one that
   comes while the body runs or awaits waits its turn. */

static void ag_enqueue(js_vm *vm, uint32_t g, int mode, jv v, uint32_t p) {
    uint32_t q = vm->objs[g].c;
    if (!q) { q = js_array_new(vm, 0); vm->objs[g].c = q; }
    js_array_push(vm, q, jv_num(mode));
    js_array_push(vm, q, v);
    js_array_push(vm, q, jv_obj(p));
}

void js_ag_complete(js_vm *vm, uint32_t g, int rejected, jv v, int done) {
    uint32_t q = vm->objs[g].c;
    if (!q || vm->objs[q].b < 3) return;
    uint32_t p = jv_idx(js_array_get(vm, q, 2));
    /* the request taken off the queue before its promise settles (which may run code) */
    uint32_t n = vm->objs[q].b, base = vm->objs[q].a;
    for (uint32_t i = 3; i < n; i++) vm->slots[base + i - 3] = vm->slots[base + i];
    js_array_set_length(vm, q, n - 3);
    if (rejected) js_promise_settle(vm, p, 1, v);
    else js_promise_resolve_with(vm, p, js_iter_result(vm, v, done));
}

/* a return that came once the body was done (or before it began): its value awaited,
   then its request answered {value, true}, or rejected with the value's rejection */
static void ag_await_return(js_vm *vm, uint32_t g) {
    uint32_t q = vm->objs[g].c;
    uint32_t pr = js_promise_of(vm, js_array_get(vm, q, 1));
    uint32_t ok = js_native_new(vm, N_AG_RETURN_OK, A_EMPTY, 1);
    vm->objs[ok].x = jv_obj(g);
    uint32_t err = js_native_new(vm, N_AG_RETURN_ERR, A_EMPTY, 1);
    vm->objs[err].x = jv_obj(g);
    js_promise_react(vm, pr, jv_obj(ok), jv_obj(err), JV_UNDEF, 0);
}

void js_ag_drain(js_vm *vm, uint32_t g) {
    for (;;) {
        uint32_t q = vm->objs[g].c;
        if (!q || vm->objs[q].b < 3 || vm->objs[g].a != JG_DONE) return;
        int mode = (int)jv_dbl(js_array_get(vm, q, 0));
        if (mode == 2) { vm->objs[g].a = JG_AWAIT_RETURN; ag_await_return(vm, g); return; }
        js_ag_complete(vm, g, mode == 1, mode == 1 ? js_array_get(vm, q, 1) : JV_UNDEF, 1);
    }
}

/* next (mode 0), throw (1) or return (2) called on the generator: its promise p */
static void ag_request(js_vm *vm, uint32_t g, int mode, jv v, uint32_t p) {
    uint32_t st = vm->objs[g].a;
    if (mode == 1 && st == JG_START) { vm->objs[g].a = JG_DONE; vm->objs[g].b = 0; st = JG_DONE; }
    if (st == JG_DONE && mode != 2) {
        if (mode == 1) js_promise_settle(vm, p, 1, v);
        else js_promise_resolve_with(vm, p, js_iter_result(vm, JV_UNDEF, 1));
        return;
    }
    ag_enqueue(vm, g, mode, v, p);
    if (mode == 2 && (st == JG_START || st == JG_DONE)) {
        vm->objs[g].a = JG_AWAIT_RETURN;
        vm->objs[g].b = 0;
        ag_await_return(vm, g);
        return;
    }
    if (st != JG_START && st != JG_YIELD) return;
    int done = 0;
    js_gen_resume(vm, g, v, mode, &done);
    if (vm->throwing == 1) {
        /* the body could not be resumed (the stack is full): the request rejected */
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        vm->objs[g].a = JG_DONE;
        vm->objs[g].b = 0;
        js_ag_complete(vm, g, 1, e, 1);
        js_ag_drain(vm, g);
    }
}

/* %AsyncFromSyncIteratorPrototype%'s continuation (27.1.6.4): the step's value awaited,
   the promise p resolved {v, done} then; when the value rejects, the sync iterator
   closed first if it is not done and the step asks it (next and throw do) */
static void afs_continue(js_vm *vm, uint32_t p, uint32_t it, jv value, int done, int close_on_reject) {
    uint32_t vw = js_promise_of(vm, value);
    uint32_t ok = js_native_new(vm, N_AFS_UNWRAP, A_EMPTY, 1);
    vm->objs[ok].x = jv_bool(done);
    jv on_err = JV_UNDEF;
    if (!done && close_on_reject) {
        uint32_t f = js_native_new(vm, N_AFS_CLOSE, A_EMPTY, 1);
        vm->objs[f].x = jv_obj(it);
        on_err = jv_obj(f);
    }
    js_promise_react(vm, vw, jv_obj(ok), on_err, jv_obj(p), 0);
}

/* the error a step threw: its promise's rejection */
static void reject_with_exception(js_vm *vm, uint32_t p) {
    if (vm->throwing != 1) return;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    js_promise_settle(vm, p, 1, e);
}

/* a sync iterator's next, throw or return as an async iterator's: always a promise */
static uint32_t afs_call(js_vm *vm, uint32_t it, int kind, jv arg) {
    uint32_t p = js_promise_new(vm);
    int mode = kind == 3 ? 0 : kind == 4 ? 2 : kind, has_arg = kind < 3;
    jv value = JV_UNDEF;
    int done = 1;
    int r = js_iter_send(vm, it, mode, arg, has_arg, &value, &done);
    if (r < 0) { reject_with_exception(vm, p); return p; }
    if (r == 0 && mode == 2) { js_promise_resolve_with(vm, p, js_iter_result(vm, has_arg ? arg : JV_UNDEF, 1)); return p; }
    if (r == 0) {
        /* no throw: the iterator closed, then a TypeError for the protocol broken */
        js_iter_close(vm, it);
        if (vm->throwing) { reject_with_exception(vm, p); return p; }
        uint32_t m = js_str_ascii(vm, "The iterator does not provide a 'throw' method.");
        js_promise_settle(vm, p, 1, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
        return p;
    }
    afs_continue(vm, p, it, value, done, mode != 2);
    return p;
}

int js_aiter_call(js_vm *vm, uint32_t rec, int kind, jv arg, jv *out) {
    uint32_t src = vm->objs[rec].a;
    *out = JV_UNDEF;
    if (kind == 3) vm->objs[rec].b = 1;
    if (vm->objs[rec].c == JI_FROM_SYNC) {
        *out = jv_obj(afs_call(vm, src, kind, arg));
        return vm->throwing ? -1 : 1;
    }
    jv it = jv_obj(src), m = vm->objs[rec].x;
    if (kind != 0 && kind != 3) {
        m = js_get(vm, it, kind == 1 ? A_THROW : A_RETURN);
        if (vm->throwing) return -1;
        if (jv_is_nullish(m)) return 0;
    }
    if (!js_is_callable(vm, m)) { js_not_function(vm, m); return -1; }
    jv a1[1] = { arg };
    *out = js_call(vm, m, it, a1, kind == 3 || kind == 4 ? 0 : 1);
    return vm->throwing ? -1 : 1;
}

void js_gc_expose(js_vm *vm) { defn(vm, vm->global, "gc", N_GC, 0); }

/* a combinator's element: its function's record [aggregate, index, called] */
static jv promise_elem_fn(js_vm *vm, uint32_t id, uint32_t agg, uint32_t index) {
    uint32_t f = js_native_new(vm, id, A_EMPTY, 1);
    uint32_t rec = js_obj_new(vm, JK_RECORD, 0);
    vm->objs[rec].a = agg;
    vm->objs[rec].b = index;
    vm->objs[rec].c = 0;
    vm->objs[f].x = jv_obj(rec);
    return jv_obj(f);
}
/* the aggregate's values array, made with the count still waiting; done once all came */
static void promise_agg_done(js_vm *vm, uint32_t agg) {
    uint32_t kind = vm->objs[agg].c >> 8;
    if (kind == 2) {
        /* any: every one rejected */
        uint32_t e = js_error_new(vm, vm->aggregate_error_proto, js_str_ascii(vm, "All promises were rejected"));
        js_define(vm, e, A_ERRORS, jv_obj(vm->objs[agg].a), JS_SHAPE_HIDDEN);
        promise_target_settle(vm, vm->objs[agg].x, 1, jv_obj(e));
    } else promise_target_settle(vm, vm->objs[agg].x, 0, jv_obj(vm->objs[agg].a));
}

/* ---- the URI functions (ECMA-262 19.2.6) and escape/unescape (B.2.1) ---- */

static jv uri_malformed(js_vm *vm) {
    return js_throw(vm, jv_obj(js_error_new(vm, vm->uri_error_proto, js_str_ascii(vm, "URI malformed"))));
}
/* a unit the encoding keeps: an alphanumeric or a mark, and for encodeURI the reserved and '#' */
static int uri_kept(uint16_t c, int full) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return 1;
    if (c && c < 128 && strchr("-_.!~*'()", (int)c)) return 1;
    return full && c && c < 128 && strchr(";/?:@&=+$,#", (int)c) != 0;
}
static jv uri_encode(js_vm *vm, uint32_t s, int full) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len;
    static const char hx[] = "0123456789ABCDEF";
    vector<uint16_t> out;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[vm->strs[s].off + i];
        if (uri_kept(c, full)) { push_back(out, c); continue; }
        uint32_t cp = c;
        if (c >= 0xDC00 && c <= 0xDFFF) return uri_malformed(vm);
        if (c >= 0xD800 && c <= 0xDBFF) {
            uint16_t d = i + 1 < n ? vm->units[vm->strs[s].off + i + 1] : 0;
            if (d < 0xDC00 || d > 0xDFFF) return uri_malformed(vm);
            cp = 0x10000 + ((uint32_t)(c - 0xD800) << 10) + (uint32_t)(d - 0xDC00);
            i++;
        }
        uint8_t b[4];
        int k = 0;
        if (cp < 0x80) b[k++] = (uint8_t)cp;
        else if (cp < 0x800) { b[k++] = (uint8_t)(0xC0 | cp >> 6); b[k++] = (uint8_t)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { b[k++] = (uint8_t)(0xE0 | cp >> 12); b[k++] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); b[k++] = (uint8_t)(0x80 | (cp & 0x3F)); }
        else {
            b[k++] = (uint8_t)(0xF0 | cp >> 18); b[k++] = (uint8_t)(0x80 | (cp >> 12 & 0x3F));
            b[k++] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); b[k++] = (uint8_t)(0x80 | (cp & 0x3F));
        }
        for (int q = 0; q < k; q++) {
            push_back(out, (uint16_t)'%');
            push_back(out, (uint16_t)hx[b[q] >> 4]);
            push_back(out, (uint16_t)hx[b[q] & 15]);
        }
    }
    return jv_str(js_str_new(vm, data(out), len(out)));
}
static int uri_hex(uint16_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* the byte of the escape %XX at i of s (-1: none) */
static int uri_byte(js_vm *vm, uint32_t s, uint32_t n, uint32_t i) {
    if (i + 2 >= n) return -1;
    if (vm->units[vm->strs[s].off + i] != '%') return -1;
    int h = uri_hex(vm->units[vm->strs[s].off + i + 1]), l = uri_hex(vm->units[vm->strs[s].off + i + 2]);
    return h < 0 || l < 0 ? -1 : h * 16 + l;
}
static jv uri_decode(js_vm *vm, uint32_t s, int full) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len;
    vector<uint16_t> out;
    for (uint32_t k = 0; k < n; k++) {
        uint16_t c = vm->units[vm->strs[s].off + k];
        if (c != '%') { push_back(out, c); continue; }
        uint32_t start = k;
        if (k + 2 >= n) return uri_malformed(vm);
        int b = uri_byte(vm, s, n, k);
        if (b < 0) return uri_malformed(vm);
        k += 2;
        if (b < 0x80) {
            /* decodeURI keeps a reserved character's escape as it was */
            if (full && strchr(";/?:@&=+$,#", b)) for (uint32_t q = start; q <= k; q++) push_back(out, vm->units[vm->strs[s].off + q]);
            else push_back(out, (uint16_t)b);
            continue;
        }
        int nb = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        if (!nb) return uri_malformed(vm);
        uint32_t cp = (uint32_t)(b & (0xFF >> (nb + 1)));
        if (k + 3 * (uint32_t)(nb - 1) >= n) return uri_malformed(vm);
        for (int j = 1; j < nb; j++) {
            k++;
            int bb = uri_byte(vm, s, n, k);
            if (bb < 0 || (bb & 0xC0) != 0x80) return uri_malformed(vm);
            k += 2;
            cp = cp << 6 | (uint32_t)(bb & 0x3F);
        }
        /* no overlong form, no surrogate, nothing past U+10FFFF */
        uint32_t least = nb == 2 ? 0x80 : nb == 3 ? 0x800 : 0x10000;
        if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return uri_malformed(vm);
        if (cp < 0x10000) push_back(out, (uint16_t)cp);
        else { cp -= 0x10000; push_back(out, (uint16_t)(0xD800 + (cp >> 10))); push_back(out, (uint16_t)(0xDC00 + (cp & 0x3FF))); }
    }
    return jv_str(js_str_new(vm, data(out), len(out)));
}
static jv js_escape(js_vm *vm, uint32_t s) {
    js_str_flatten(vm, s);
    static const char hx[] = "0123456789ABCDEF";
    vector<uint16_t> out;
    for (uint32_t i = 0; i < vm->strs[s].len; i++) {
        uint16_t c = vm->units[vm->strs[s].off + i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && c < 128 && strchr("@*_+-./", (int)c))) push_back(out, c);
        else if (c < 256) { push_back(out, (uint16_t)'%'); push_back(out, (uint16_t)hx[c >> 4]); push_back(out, (uint16_t)hx[c & 15]); }
        else {
            push_back(out, (uint16_t)'%');
            push_back(out, (uint16_t)'u');
            for (int sh = 12; sh >= 0; sh -= 4) push_back(out, (uint16_t)hx[(c >> sh) & 15]);
        }
    }
    return jv_str(js_str_new(vm, data(out), len(out)));
}
static jv js_unescape(js_vm *vm, uint32_t s) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len;
    vector<uint16_t> out;
    for (uint32_t k = 0; k < n; k++) {
        uint16_t c = vm->units[vm->strs[s].off + k];
        if (c == '%') {
            const uint16_t *u = data(vm->units) + vm->strs[s].off;
            if (k + 6 <= n && u[k + 1] == 'u' && uri_hex(u[k + 2]) >= 0 && uri_hex(u[k + 3]) >= 0 && uri_hex(u[k + 4]) >= 0 && uri_hex(u[k + 5]) >= 0) {
                c = (uint16_t)(uri_hex(u[k + 2]) << 12 | uri_hex(u[k + 3]) << 8 | uri_hex(u[k + 4]) << 4 | uri_hex(u[k + 5]));
                k += 5;
            } else if (k + 3 <= n && uri_hex(u[k + 1]) >= 0 && uri_hex(u[k + 2]) >= 0) {
                c = (uint16_t)(uri_hex(u[k + 1]) << 4 | uri_hex(u[k + 2]));
                k += 2;
            }
        }
        push_back(out, c);
    }
    return jv_str(js_str_new(vm, data(out), len(out)));
}

/* ---- the Set methods of ES2025: an argument read as a set-like record (its size, has and keys) ---- */

typedef struct setrec { jv obj, has, keys; double size; } setrec;
static int set_record(js_vm *vm, jv other, const char *method, setrec *r) {
    if (!jv_is_obj(other)) {
        char m[96];
        snprintf(m, sizeof m, "%s argument must be an object", method);
        js_throw_msg(vm, vm->type_error_proto, m);
        return 0;
    }
    jv rs = js_get(vm, other, A_SIZE);
    if (vm->throwing) return 0;
    double ns = js_to_number(vm, rs);
    if (vm->throwing) return 0;
    if (ns != ns) { js_throw_msg(vm, vm->type_error_proto, "The .size property is NaN"); return 0; }
    double is = trunc(ns);
    if (is < 0) {
        char m[96];
        js_num_format(is, m, 40);
        char m2[128];
        snprintf(m2, sizeof m2, "'%s' is an invalid size", m);
        js_throw_msg(vm, vm->range_error_proto, m2);
        return 0;
    }
    r->obj = other;
    r->size = is;
    r->has = js_get(vm, other, js_atom_ascii(vm, "has"));
    if (vm->throwing) return 0;
    if (!js_is_callable(vm, r->has)) { js_throw_msg(vm, vm->type_error_proto, "string \"has\" is not a function"); return 0; }
    r->keys = js_get(vm, other, js_atom_ascii(vm, "keys"));
    if (vm->throwing) return 0;
    if (!js_is_callable(vm, r->keys)) { js_throw_msg(vm, vm->type_error_proto, "string \"keys\" is not a function"); return 0; }
    return 1;
}
/* the set-like's keys(): its iterator and that iterator's next (0 with a TypeError thrown) */
static int set_keys(js_vm *vm, const setrec *r, jv *it, jv *next) {
    *it = js_call(vm, r->keys, r->obj, 0, 0);
    if (vm->throwing) return 0;
    if (!jv_is_obj(*it)) { value_error(vm, "", *it, " is not an object"); return 0; }
    *next = js_get(vm, *it, A_NEXT);
    if (vm->throwing) return 0;
    if (!js_is_callable(vm, *next)) { js_not_function(vm, *next); return 0; }
    return 1;
}
/* a step of it: 1 its value in *out (-0 made +0), 0 done, -1 thrown */
static int set_step(js_vm *vm, jv it, jv next, jv *out) {
    jv r = js_call(vm, next, it, 0, 0);
    if (vm->throwing) return -1;
    if (!jv_is_obj(r)) { value_error(vm, "Iterator result ", r, " is not an object"); return -1; }
    jv d = js_get(vm, r, A_DONE);
    if (vm->throwing) return -1;
    if (js_truthy(vm, d)) return 0;
    *out = js_get(vm, r, A_VALUE);
    if (vm->throwing) return -1;
    if (jv_is_num(*out) && jv_dbl(*out) == 0) *out = jv_num(0);
    return 1;
}
/* IteratorClose of a set-like's keys, what it throws its own */
static void set_close(js_vm *vm, jv it) {
    jv rf = js_get(vm, it, A_RETURN);
    if (vm->throwing || !js_is_callable(vm, rf)) return;
    js_call(vm, rf, it, 0, 0);
}
/* a copy of the set c's elements in order, as a new Set */
static uint32_t set_copy(js_vm *vm, uint32_t c) {
    uint32_t r = coll_new(vm, JK_SET, vm->set_proto);
    uint32_t arr = vm->objs[c].a;
    for (uint32_t i = 0; i < vm->objs[arr].b; i++) {
        jv k = vm->slots[vm->objs[arr].a + i];
        if (k != JV_HOLE) coll_append(vm, r, k, JV_UNDEF);
    }
    return r;
}
static void set_drop(js_vm *vm, uint32_t c, jv k) {
    int64_t at_i = coll_find(vm, c, k);
    if (at_i >= 0) coll_remove(vm, c, k, (uint32_t)at_i);
}
static void set_add(js_vm *vm, uint32_t c, jv k) {
    if (coll_find(vm, c, k) < 0) coll_append(vm, c, k, JV_UNDEF);
}
/* the set c's element at its i-th place (JV_HOLE: none), its places read live as the standard's loops read them */
static jv set_at(js_vm *vm, uint32_t c, uint32_t i) {
    uint32_t arr = vm->objs[c].a;
    return i < vm->objs[arr].b ? vm->slots[vm->objs[arr].a + i] : JV_UNDEF;
}
static uint32_t set_places(js_vm *vm, uint32_t c) { return vm->objs[vm->objs[c].a].b; }

static jv set_method(js_vm *vm, uint32_t id, jv thisv, jv other) {
    static const char *const sm[] = { "Set.prototype.union", "Set.prototype.intersection", "Set.prototype.difference",
        "Set.prototype.symmetricDifference", "Set.prototype.isSubsetOf", "Set.prototype.isSupersetOf", "Set.prototype.isDisjointFrom" };
    uint32_t c = this_coll(vm, thisv, JK_SET, id);
    if (!c) return JV_HOLE;
    setrec r;
    if (!set_record(vm, other, sm[id - N_SETP_UNION], &r)) return JV_HOLE;
    double this_size = vm->objs[c].b;
    jv it, next, v;
    int st;
    if (id == N_SETP_UNION || id == N_SETP_SYMDIFF) {
        uint32_t res = set_copy(vm, c);
        if (!set_keys(vm, &r, &it, &next)) return JV_HOLE;
        while ((st = set_step(vm, it, next, &v)) > 0) {
            if (id == N_SETP_UNION) set_add(vm, res, v);
            else if (coll_find(vm, c, v) >= 0) set_drop(vm, res, v);
            else set_add(vm, res, v);
        }
        return st < 0 ? JV_HOLE : jv_obj(res);
    }
    if (id == N_SETP_INTERSECTION) {
        uint32_t res = coll_new(vm, JK_SET, vm->set_proto);
        if (this_size <= r.size) {
            for (uint32_t i = 0; i < set_places(vm, c); i++) {
                jv e = set_at(vm, c, i);
                if (e == JV_HOLE) continue;
                jv in = js_call(vm, r.has, r.obj, &e, 1);
                if (vm->throwing) return JV_HOLE;
                if (js_truthy(vm, in)) set_add(vm, res, e);
            }
            return jv_obj(res);
        }
        if (!set_keys(vm, &r, &it, &next)) return JV_HOLE;
        while ((st = set_step(vm, it, next, &v)) > 0) if (coll_find(vm, c, v) >= 0) set_add(vm, res, v);
        return st < 0 ? JV_HOLE : jv_obj(res);
    }
    if (id == N_SETP_DIFFERENCE) {
        uint32_t res = set_copy(vm, c);
        if (this_size <= r.size) {
            for (uint32_t i = 0; i < set_places(vm, c); i++) {
                jv e = set_at(vm, c, i);
                if (e == JV_HOLE) continue;
                jv in = js_call(vm, r.has, r.obj, &e, 1);
                if (vm->throwing) return JV_HOLE;
                if (js_truthy(vm, in)) set_drop(vm, res, e);
            }
            return jv_obj(res);
        }
        if (!set_keys(vm, &r, &it, &next)) return JV_HOLE;
        while ((st = set_step(vm, it, next, &v)) > 0) set_drop(vm, res, v);
        return st < 0 ? JV_HOLE : jv_obj(res);
    }
    if (id == N_SETP_ISSUBSET) {
        if (this_size > r.size) return JV_FALSE;
        for (uint32_t i = 0; i < set_places(vm, c); i++) {
            jv e = set_at(vm, c, i);
            if (e == JV_HOLE) continue;
            jv in = js_call(vm, r.has, r.obj, &e, 1);
            if (vm->throwing) return JV_HOLE;
            if (!js_truthy(vm, in)) return JV_FALSE;
        }
        return JV_TRUE;
    }
    if (id == N_SETP_ISSUPERSET) {
        if (this_size < r.size) return JV_FALSE;
        if (!set_keys(vm, &r, &it, &next)) return JV_HOLE;
        while ((st = set_step(vm, it, next, &v)) > 0)
            if (coll_find(vm, c, v) < 0) { set_close(vm, it); return vm->throwing ? JV_HOLE : JV_FALSE; }
        return st < 0 ? JV_HOLE : JV_TRUE;
    }
    /* isDisjointFrom */
    if (this_size <= r.size) {
        for (uint32_t i = 0; i < set_places(vm, c); i++) {
            jv e = set_at(vm, c, i);
            if (e == JV_HOLE) continue;
            jv in = js_call(vm, r.has, r.obj, &e, 1);
            if (vm->throwing) return JV_HOLE;
            if (js_truthy(vm, in)) return JV_FALSE;
        }
        return JV_TRUE;
    }
    if (!set_keys(vm, &r, &it, &next)) return JV_HOLE;
    while ((st = set_step(vm, it, next, &v)) > 0)
        if (coll_find(vm, c, v) >= 0) { set_close(vm, it); return vm->throwing ? JV_HOLE : JV_FALSE; }
    return st < 0 ? JV_HOLE : JV_TRUE;
}

/* ---- the Iterator global and its helpers (ECMA-262 27.1.3, Iterator Helpers) ----
   A helper is an iterator object (JI_HELPER) whose record (an array in a) holds: the iterator it reads and its next,
   the function given it, its counter, what remains to take or drop, flatMap's inner iterator and its next, its kind
   and its state (0 not started, 1 suspended, 2 running, 3 done) */
enum { IH_ITER, IH_NEXT, IH_FN, IH_COUNT, IH_REMAIN, IH_INNER, IH_INNER_NEXT, IH_KIND, IH_STATE, IH_N };
enum { IHK_MAP, IHK_FILTER, IHK_TAKE, IHK_DROP, IHK_FLATMAP };

/* IteratorStepValue: 1 a value in *out, 0 done, -1 thrown */
static int iter_step_value(js_vm *vm, jv it, jv next, jv *out) {
    if (!js_is_callable(vm, next)) { js_not_function(vm, next); return -1; }
    jv r = js_call(vm, next, it, 0, 0);
    if (vm->throwing) return -1;
    if (!jv_is_obj(r)) { value_error(vm, "Iterator result ", r, " is not an object"); return -1; }
    jv d = js_get(vm, r, A_DONE);
    if (vm->throwing) return -1;
    if (js_truthy(vm, d)) return 0;
    *out = js_get(vm, r, A_VALUE);
    return vm->throwing ? -1 : 1;
}
/* IteratorClose: the iterator's return called; with an exception in flight, it stays the one thrown */
static void iter_close_obj(js_vm *vm, jv it) {
    int was = vm->throwing;
    jv saved = vm->exception;
    vm->throwing = 0;
    jv rf = js_get(vm, it, A_RETURN);
    if (!vm->throwing && js_is_callable(vm, rf)) {
        jv r = js_call(vm, rf, it, 0, 0);
        if (!vm->throwing && !was && !jv_is_obj(r)) value_error(vm, "Iterator result ", r, " is not an object");
    }
    if (was) { vm->throwing = was; vm->exception = saved; }
}
static jv ih_get(js_vm *vm, uint32_t rec, uint32_t k) { return js_array_get(vm, rec, k); }
static void ih_set(js_vm *vm, uint32_t rec, uint32_t k, jv v) { js_array_set(vm, rec, k, v); }
static jv ih_done(js_vm *vm, uint32_t rec) { ih_set(vm, rec, IH_STATE, jv_num(3)); return JV_UNDEF; }

int js_helper_step(js_vm *vm, uint32_t it, jv *out) {
    uint32_t rec = vm->objs[it].a;
    int st = (int)jv_dbl(ih_get(vm, rec, IH_STATE));
    if (st == 3) return 0;
    if (st == 2) { js_throw_msg(vm, vm->type_error_proto, "Generator is already running"); return 0; }
    ih_set(vm, rec, IH_STATE, jv_num(2));
    jv src = ih_get(vm, rec, IH_ITER), next = ih_get(vm, rec, IH_NEXT), fn = ih_get(vm, rec, IH_FN);
    int kind = (int)jv_dbl(ih_get(vm, rec, IH_KIND));
    double count = jv_dbl(ih_get(vm, rec, IH_COUNT));
    jv v = JV_UNDEF;
    for (;;) {
        if (kind == IHK_FLATMAP) {
            jv inner = ih_get(vm, rec, IH_INNER);
            if (inner != JV_UNDEF) {
                int r = iter_step_value(vm, inner, ih_get(vm, rec, IH_INNER_NEXT), &v);
                if (r < 0) { iter_close_obj(vm, src); ih_done(vm, rec); return 0; }
                if (r > 0) { ih_set(vm, rec, IH_STATE, jv_num(1)); *out = v; return 1; }
                ih_set(vm, rec, IH_INNER, JV_UNDEF);
            }
        }
        if (kind == IHK_TAKE) {
            double rem = jv_dbl(ih_get(vm, rec, IH_REMAIN));
            if (rem == 0) { iter_close_obj(vm, src); ih_done(vm, rec); return 0; }
            if (rem != INFINITY) ih_set(vm, rec, IH_REMAIN, jv_num(rem - 1));
        }
        if (kind == IHK_DROP) {
            double rem = jv_dbl(ih_get(vm, rec, IH_REMAIN));
            while (rem > 0) {
                if (rem != INFINITY) { rem--; ih_set(vm, rec, IH_REMAIN, jv_num(rem)); }
                int r = iter_step_value(vm, src, next, &v);
                if (r <= 0) { ih_done(vm, rec); return 0; }
            }
        }
        int r = iter_step_value(vm, src, next, &v);
        if (r <= 0) { ih_done(vm, rec); return 0; }
        if (kind == IHK_TAKE || kind == IHK_DROP) { ih_set(vm, rec, IH_STATE, jv_num(1)); *out = v; return 1; }
        jv av[2] = { v, jv_num(count) };
        jv m = js_call(vm, fn, JV_UNDEF, av, 2);
        if (vm->throwing) { iter_close_obj(vm, src); ih_done(vm, rec); return 0; }
        count++;
        ih_set(vm, rec, IH_COUNT, jv_num(count));
        if (kind == IHK_MAP) { ih_set(vm, rec, IH_STATE, jv_num(1)); *out = m; return 1; }
        if (kind == IHK_FILTER) {
            if (js_truthy(vm, m)) { ih_set(vm, rec, IH_STATE, jv_num(1)); *out = v; return 1; }
            continue;
        }
        /* flatMap: the mapped value's own iterator (a string refused), read until done */
        if (!jv_is_obj(m)) {
            js_throw_msg(vm, vm->type_error_proto, "Iterator.prototype.flatMap called on non-object");
            iter_close_obj(vm, src);
            ih_done(vm, rec);
            return 0;
        }
        jv im = js_get(vm, m, JS_KEY_ITERATOR), inner = m;
        if (!vm->throwing && !jv_is_nullish(im)) {
            inner = js_call(vm, im, m, 0, 0);
            if (!vm->throwing && !jv_is_obj(inner)) js_throw_msg(vm, vm->type_error_proto, "Result of the Symbol.iterator method is not an object");
        }
        jv inext = vm->throwing ? JV_UNDEF : js_get(vm, inner, A_NEXT);
        if (vm->throwing) { iter_close_obj(vm, src); ih_done(vm, rec); return 0; }
        ih_set(vm, rec, IH_INNER, inner);
        ih_set(vm, rec, IH_INNER_NEXT, inext);
    }
}

/* GetIteratorDirect of this for a helper method (V8's "<method> called on non-object") */
static int iter_direct(js_vm *vm, jv thisv, const char *method, jv *next) {
    if (!jv_is_obj(thisv)) {
        char m[96];
        snprintf(m, sizeof m, "%s called on non-object", method);
        js_throw_msg(vm, vm->type_error_proto, m);
        return 0;
    }
    *next = js_get(vm, thisv, A_NEXT);
    return !vm->throwing;
}
/* a helper's or a consumer's function argument, V8's words when it is none */
static int iter_fn_ok(js_vm *vm, jv fn, const char *method) {
    if (js_is_callable(vm, fn)) return 1;
    char m[96];
    snprintf(m, sizeof m, "string \"%s\" is not a function", method);
    js_throw_msg(vm, vm->type_error_proto, m);
    return 0;
}
static jv iter_helper(js_vm *vm, uint32_t id, jv thisv, jv a0) {
    static const char *const names[] = { "Iterator.prototype.map", "Iterator.prototype.filter", "Iterator.prototype.take",
                                         "Iterator.prototype.drop", "Iterator.prototype.flatMap" };
    int kind = (int)(id - N_ITP_MAP);
    if (!jv_is_obj(thisv)) return iter_direct(vm, thisv, names[kind], &a0) ? JV_UNDEF : JV_HOLE;
    double rem = 0;
    if (kind == IHK_TAKE || kind == IHK_DROP) {
        double n = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        if (n != n || trunc(n) < 0) {
            char b[64], m[96];
            js_num_format(n, b, sizeof b);
            snprintf(m, sizeof m, "%s must be positive", b);
            return js_throw_msg(vm, vm->range_error_proto, m);
        }
        rem = trunc(n);
    } else if (!iter_fn_ok(vm, a0, names[kind])) return JV_HOLE;
    jv next;
    if (!iter_direct(vm, thisv, names[kind], &next)) return JV_HOLE;
    uint32_t rec = js_array_new(vm, 0);
    for (uint32_t k = 0; k < IH_N; k++) js_array_push(vm, rec, JV_UNDEF);
    ih_set(vm, rec, IH_ITER, thisv);
    ih_set(vm, rec, IH_NEXT, next);
    ih_set(vm, rec, IH_FN, kind == IHK_TAKE || kind == IHK_DROP ? JV_UNDEF : a0);
    ih_set(vm, rec, IH_COUNT, jv_num(0));
    ih_set(vm, rec, IH_REMAIN, jv_num(rem));
    ih_set(vm, rec, IH_KIND, jv_num(kind));
    ih_set(vm, rec, IH_STATE, jv_num(0));
    uint32_t h = js_obj_new(vm, JK_ITER, vm->helper_proto);
    vm->objs[h].a = rec;
    vm->objs[h].c = JI_HELPER;
    return jv_obj(h);
}
/* reduce, toArray, forEach, some, every, find: the iterator read here and now */
static jv iter_consume(js_vm *vm, uint32_t id, jv thisv, uint32_t args, uint32_t argc) {
    static const char *const names[] = { "Iterator.prototype.reduce", "Iterator.prototype.toArray", "Iterator.prototype.forEach",
                                         "Iterator.prototype.some", "Iterator.prototype.every", "Iterator.prototype.find" };
    uint32_t k = id - N_ITP_REDUCE;
    jv fn = arg(vm, args, argc, 0), next;
    if (!jv_is_obj(thisv)) return iter_direct(vm, thisv, names[k], &next) ? JV_UNDEF : JV_HOLE;
    if (id != N_ITP_TOARRAY && !iter_fn_ok(vm, fn, names[k])) return JV_HOLE;
    if (!iter_direct(vm, thisv, names[k], &next)) return JV_HOLE;
    double count = 0;
    jv v, acc = JV_UNDEF;
    int r;
    uint32_t out = 0;
    if (id == N_ITP_TOARRAY) out = js_array_new(vm, 0);
    if (id == N_ITP_REDUCE) {
        if (argc >= 2) acc = arg(vm, args, argc, 1);
        else {
            r = iter_step_value(vm, thisv, next, &acc);
            if (r < 0) return JV_HOLE;
            if (r == 0) return js_throw_msg(vm, vm->type_error_proto, "Reduce of a done iterator with no initial value");
            count = 1;
        }
    }
    while ((r = iter_step_value(vm, thisv, next, &v)) > 0) {
        if (id == N_ITP_TOARRAY) { js_array_push(vm, out, v); continue; }
        jv res;
        if (id == N_ITP_REDUCE) {
            jv av[3] = { acc, v, jv_num(count) };
            res = js_call(vm, fn, JV_UNDEF, av, 3);
        } else {
            jv av[2] = { v, jv_num(count) };
            res = js_call(vm, fn, JV_UNDEF, av, 2);
        }
        if (vm->throwing) { iter_close_obj(vm, thisv); return JV_HOLE; }
        count++;
        if (id == N_ITP_REDUCE) acc = res;
        else if (id == N_ITP_SOME && js_truthy(vm, res)) { iter_close_obj(vm, thisv); return vm->throwing ? JV_HOLE : JV_TRUE; }
        else if (id == N_ITP_EVERY && !js_truthy(vm, res)) { iter_close_obj(vm, thisv); return vm->throwing ? JV_HOLE : JV_FALSE; }
        else if (id == N_ITP_FIND && js_truthy(vm, res)) { iter_close_obj(vm, thisv); return vm->throwing ? JV_HOLE : v; }
    }
    if (r < 0) return JV_HOLE;
    if (id == N_ITP_REDUCE) return acc;
    if (id == N_ITP_TOARRAY) return jv_obj(out);
    if (id == N_ITP_SOME) return JV_FALSE;
    if (id == N_ITP_EVERY) return JV_TRUE;
    return JV_UNDEF;
}
/* SetterThatIgnoresPrototypeProperties (Iterator.prototype's constructor and Symbol.toStringTag) */
static jv iter_proto_setter(js_vm *vm, jv thisv, uint32_t key, jv v, const char *name) {
    if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Iterator.prototype setter called on non-object");
    if (jv_idx(thisv) == vm->iterator_proto) {
        char m[128];
        snprintf(m, sizeof m, "Cannot assign to read only property '%s' of object '[object Iterator]'", name);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    jv cur;
    uint32_t fl;
    if (!js_get_own(vm, jv_idx(thisv), key, &cur, &fl)) {
        if (js_create_data(vm, thisv, key == (JS_SYM_KEY | JSYM_TO_STRING_TAG) ? jv_sym(JSYM_TO_STRING_TAG) : jv_str(key), v) < 0) return JV_HOLE;
    } else js_set(vm, thisv, key, v);
    return vm->throwing ? JV_HOLE : JV_UNDEF;
}

/* ---- Number.prototype.toExponential: the digits rounded half up, as the standard picks the larger n ---- */
static uint32_t num_to_exponential(js_vm *vm, double x, double f, int have_f) {
    char out[160];
    size_t k = 0;
    if (x < 0) { out[k++] = '-'; x = -x; }
    char digs[128];
    int nd = 0, e10 = 0;
    if (x == 0) {
        digs[0] = '0';
        nd = have_f ? (int)f + 1 : 1;
        for (int q = 1; q < nd; q++) digs[q] = '0';
    } else if (!have_f) {
        /* the shortest digits that read back as x */
        char tmp[40];
        int prec = 1;
        for (; prec <= 17; prec++) {
            snprintf(tmp, sizeof tmp, "%.*e", prec - 1, x);
            if (strtod(tmp, 0) == x) break;
        }
        int i = 0;
        for (; tmp[i] && tmp[i] != 'e'; i++) if (tmp[i] >= '0' && tmp[i] <= '9') digs[nd++] = tmp[i];
        e10 = atoi(tmp + i + 1);
        while (nd > 1 && digs[nd - 1] == '0') nd--;
    } else {
        /* f + 1 digits, from the exact expansion (glibc prints it): a remainder of exactly one half rounds up */
        int p = (int)f + 1;
        char tmp[200];
        snprintf(tmp, sizeof tmp, "%.*e", p + 40, x);
        char all[160];
        int na = 0, i = 0;
        for (; tmp[i] && tmp[i] != 'e'; i++) if (tmp[i] >= '0' && tmp[i] <= '9') all[na++] = tmp[i];
        e10 = atoi(tmp + i + 1);
        int up = 0;
        if (all[p] > '5') up = 1;
        else if (all[p] == '5') up = 1;
        for (int q = 0; q < p; q++) digs[q] = all[q];
        nd = p;
        if (up) {
            int q = p - 1;
            while (q >= 0 && digs[q] == '9') { digs[q] = '0'; q--; }
            if (q >= 0) digs[q]++;
            else {
                /* 9.99 up to 10.0: one more place before the point */
                for (int z = nd; z > 0; z--) digs[z] = digs[z - 1];
                digs[0] = '1';
                e10++;
            }
        }
    }
    out[k++] = digs[0];
    if (nd > 1) {
        out[k++] = '.';
        for (int q = 1; q < nd; q++) out[k++] = digs[q];
    }
    out[k] = 0;
    char tail[16];
    snprintf(tail, sizeof tail, "e%c%d", e10 < 0 ? '-' : '+', e10 < 0 ? -e10 : e10);
    char fin[180];
    snprintf(fin, sizeof fin, "%s%s", out, tail);
    return js_str_ascii(vm, fin);
}

/* ---- String's HTML methods (B.2.2): CreateHTML ---- */
static jv string_html(js_vm *vm, jv thisv, const char *name, const char *tag, const char *attr, jv value) {
    if (jv_is_nullish(thisv)) {
        char m[96];
        snprintf(m, sizeof m, "String.prototype.%s called on null or undefined", name);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    uint32_t s = js_to_string(vm, thisv);
    if (!s) return JV_HOLE;
    vector<char> o;
    push_back(o, '<');
    append_range(o, tag, strlen(tag));
    if (attr) {
        uint32_t v = js_to_string(vm, value);
        if (!v) return JV_HOLE;
        push_back(o, ' ');
        append_range(o, attr, strlen(attr));
        append_range(o, "=\"", 2);
        vector<char> vt;
        js_str_to_utf8(vm, v, &vt);
        for (size_t i = 0; i < len(vt); i++) {
            if (at(vt, i) == '"') append_range(o, "&quot;", 6);
            else push_back(o, at(vt, i));
        }
        push_back(o, '"');
    }
    push_back(o, '>');
    js_str_to_utf8(vm, s, &o);
    append_range(o, "</", 2);
    append_range(o, tag, strlen(tag));
    push_back(o, '>');
    return jv_str(js_str_utf8(vm, data(o), len(o)));
}

/* the iterators' kinds (V8's names for them) by mode: 0 an array's, 1 a map's, 2 a set's, 3 a string's, 4 matchAll's */
static int iter_family(uint32_t mode) {
    if (mode == JI_ARRAY || mode == JI_ARRAY_KEYS || mode == JI_ARRAY_ENTRIES) return 0;
    if (mode == JI_MAP || mode == JI_MAP_KEYS || mode == JI_MAP_VALUES || mode == JI_ENTRIES) return 1;
    if (mode == JI_SET || mode == JI_SET_ENTRIES) return 2;
    if (mode == JI_STRING) return 3;
    if (mode == JI_REGEXP) return 4;
    return -1;
}
static jv iter_of_coll(js_vm *vm, uint32_t coll, uint32_t mode) {
    int fam = iter_family(mode);
    uint32_t proto = fam == 0 ? vm->array_iter_proto : fam == 1 ? vm->map_iter_proto : fam == 2 ? vm->set_iter_proto :
                     fam == 3 ? vm->string_iter_proto : fam == 4 ? vm->regexp_iter_proto : vm->iterator_proto;
    uint32_t it = js_obj_new(vm, JK_ITER, proto);
    vm->objs[it].a = coll;
    vm->objs[it].c = mode;
    return jv_obj(it);
}

/* a prototype's method called on undefined or null, which it reads as an object: V8's
   "Array.prototype.join called on null or undefined" */
static jv called_on_nothing(js_vm *vm, const char *proto, jv callee) {
    vector<char> m;
    append_range(m, proto, strlen(proto));
    if (jv_is_obj(callee) && vm->objs[jv_idx(callee)].kind == JK_NATIVE && vm->objs[jv_idx(callee)].c) js_str_to_utf8(vm, vm->objs[jv_idx(callee)].c, &m);
    const char *post = " called on null or undefined";
    append_range(m, post, strlen(post));
    return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
}

/* an Array.prototype method called on undefined or null, in V8's words: its callback-taking
   methods (and concat, indexOf) name themselves; the others are ToObject's */
static jv array_on_nothing(js_vm *vm, uint32_t id, jv callee) {
    if (id == N_AP_CONCAT || id == N_AP_INDEXOF || (id >= N_AP_FIND && id <= N_AP_EVERY)) return called_on_nothing(vm, "Array.prototype.", callee);
    return js_throw_msg(vm, vm->type_error_proto, "Cannot convert undefined or null to object");
}

/* Array.from's and Array.of's values (an array of them) given to their this when it is a
   constructor other than Array (ECMA-262 23.1.2.1, 23.1.2.3): its object made (given the
   count, with_len), each value defined as CreateDataPropertyOrThrow defines one (a
   frozen or non-extensible object's refusal a TypeError), then its length set */
static jv into_ctor(js_vm *vm, jv c, uint32_t vals, int with_len) {
    if (!jv_is_obj(c) || (vm->objs[jv_idx(c)].kind == JK_NATIVE && vm->objs[jv_idx(c)].a == N_ARRAY) || !js_is_constructor(vm, c)) return jv_obj(vals);
    uint32_t n = vm->objs[vals].b;
    jv av[1] = { jv_num(n) };
    jv a = js_construct(vm, c, av, with_len ? 1 : 0, c);
    if (vm->throwing) return JV_HOLE;
    for (uint32_t i = 0; i < n; i++)
        if (!js_create_data_throw(vm, a, jv_num(i), js_array_get(vm, vals, i))) return JV_HOLE;
    if (!js_set_throw(vm, a, jv_str(A_LENGTH), jv_num(n))) return JV_HOLE;
    return a;
}

/* ---- ArraySpeciesCreate (ECMA-262 10.4.2.3) ----
   An array method's result made by its array's species: its constructor's Symbol.species when o is an array and that is
   a constructor other than Array (undefined or null a plain array; anything else not a constructor V8's TypeError). An
   array of Array.prototype with no constructor of its own, while Array.prototype.constructor is Array and Array's
   species the engine's getter, is answered without a read */
static int is_array_ctor_val(js_vm *vm, jv c) { return jv_is_obj(c) && vm->objs[jv_idx(c)].kind == JK_NATIVE && vm->objs[jv_idx(c)].a == N_ARRAY; }
static int array_species_plain(js_vm *vm, jv o) {
    uint32_t a = jv_idx(o);
    if (vm->objs[a].kind != JK_ARRAY || vm->objs[a].proto != vm->array_proto || js_shape_find(vm, vm->objs[a].shape, A_CONSTRUCTOR)) return 0;
    jv c = JV_UNDEF, s = JV_UNDEF;
    uint32_t f = 0;
    if (!js_get_own(vm, vm->array_proto, A_CONSTRUCTOR, &c, &f) || (f & JS_SHAPE_ACCESSOR) || !is_array_ctor_val(vm, c)) return 0;
    if (!js_get_own(vm, jv_idx(c), JS_SYM_KEY | JSYM_SPECIES, &s, &f) || !(f & JS_SHAPE_ACCESSOR)) return 0;
    jv g = vm->objs[jv_idx(s)].x;
    return jv_is_obj(g) && vm->objs[jv_idx(g)].kind == JK_NATIVE && vm->objs[jv_idx(g)].a == N_SPECIES;
}
/* 1, *sobj the species' new object (new C(n)); 0 a plain array; -1 thrown */
static int array_species(js_vm *vm, jv o, double n, jv *sobj) {
    *sobj = JV_UNDEF;
    if (!jv_is_obj(o) || array_species_plain(vm, o)) return 0;
    int arr = is_array_spec(vm, o);
    if (vm->throwing) return -1;
    if (!arr) return 0;
    jv c = js_get(vm, o, A_CONSTRUCTOR);
    if (vm->throwing) return -1;
    if (jv_is_obj(c)) {
        c = js_get(vm, c, JS_SYM_KEY | JSYM_SPECIES);
        if (vm->throwing) return -1;
        if (c == JV_NULL) c = JV_UNDEF;
    }
    if (c == JV_UNDEF || is_array_ctor_val(vm, c)) return 0;
    if (!js_is_constructor(vm, c)) { js_throw_msg(vm, vm->type_error_proto, "object.constructor[Symbol.species] is not a constructor"); return -1; }
    jv av[1] = { jv_num(n) };
    *sobj = js_construct(vm, c, av, 1, c);
    return vm->throwing ? -1 : 1;
}
/* what the method gathered (a plain array, its holes passed over) defined on the species' object, then its length set
   when length is not negative */
static jv species_fill(js_vm *vm, jv a, uint32_t vals, double length) {
    for (uint32_t i = 0; i < vm->objs[vals].b; i++) {
        jv v = vm->slots[vm->objs[vals].a + i];
        if (v == JV_HOLE) continue;
        if (!js_create_data_throw(vm, a, jv_num(i), v)) return JV_HOLE;
    }
    if (length >= 0 && !js_set_throw(vm, a, jv_str(A_LENGTH), jv_num(length))) return JV_HOLE;
    return a;
}

/* ---- the array methods that write, as the standard has them (ECMA-262 23.1.3) ----
   For what the fast paths do not take: an array whose elements or length have
   attributes of their own (frozen, sealed, not extensible, defineProperty's), an
   arguments object, any array-like. Each write is Set(O, P, V, true) and each removal
   DeletePropertyOrThrow, so a refusal is a TypeError in V8's words, the caller's code
   strict or not. Each returns JV_HOLE when one threw. */

/* LengthOfArrayLike(O): -1 when it threw */
static double ga_len(js_vm *vm, jv o) {
    if (is_array(vm, o)) return vm->objs[jv_idx(o)].b;
    jv n = js_get(vm, o, A_LENGTH);
    if (vm->throwing) return -1;
    double d = js_to_number(vm, n);
    if (vm->throwing) return -1;
    if (d != d || d <= 0) return 0;
    d = trunc(d);
    return d > 9007199254740991.0 ? 9007199254740991.0 : d;
}
/* HasProperty(O, k) and Get(O, k) at an index that may pass 2^32 - 2 (an array-like's) */
static int ga_has(js_vm *vm, jv o, double k) {
    if (k < 4294967295.0) return has_elem(vm, o, (uint32_t)k);
    return jv_is_obj(o) && js_has(vm, jv_idx(o), js_atom(vm, js_num_to_str(vm, k)));
}
static jv ga_get(js_vm *vm, jv o, double k) {
    if (is_array(vm, o) && k < vm->objs[jv_idx(o)].b) {
        jv v = vm->slots[vm->objs[jv_idx(o)].a + (uint32_t)k];
        if (v != JV_HOLE) return v;
    }
    return js_get_value(vm, o, jv_num(k));
}
/* the element at from moved to to (the shifting loops' step): Set when it is there,
   else DeletePropertyOrThrow; 0 thrown */
static int ga_move(js_vm *vm, jv o, double from, double to) {
    int has = ga_has(vm, o, from);
    if (vm->throwing) return 0;
    if (!has) return js_delete_throw(vm, o, jv_num(to));
    jv v = ga_get(vm, o, from);
    if (vm->throwing) return 0;
    return js_set_throw(vm, o, jv_num(to), v);
}
static int ga_set_len(js_vm *vm, jv o, double n) { return js_set_throw(vm, o, jv_str(A_LENGTH), jv_num(n)); }
/* a relative index (from the end when negative) clamped to [0, n] */
static double ga_rel(double d, double n) {
    if (d < 0) return n + d < 0 ? 0 : n + d;
    return d > n ? n : d;
}

static jv ga_push(js_vm *vm, jv o, uint32_t args, uint32_t argc) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    for (uint32_t i = 0; i < argc; i++)
        if (!js_set_throw(vm, o, jv_num(n + i), arg(vm, args, argc, i))) return JV_HOLE;
    if (!ga_set_len(vm, o, n + argc)) return JV_HOLE;
    return jv_num(n + argc);
}

static jv ga_pop_shift(js_vm *vm, jv o, int shift) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    if (n == 0) return ga_set_len(vm, o, 0) ? JV_UNDEF : JV_HOLE;
    jv r = ga_get(vm, o, shift ? 0 : n - 1);
    if (vm->throwing) return JV_HOLE;
    if (shift)
        for (double k = 1; k < n; k++) if (!ga_move(vm, o, k, k - 1)) return JV_HOLE;
    if (!js_delete_throw(vm, o, jv_num(n - 1))) return JV_HOLE;
    if (!ga_set_len(vm, o, n - 1)) return JV_HOLE;
    return r;
}

static jv ga_unshift(js_vm *vm, jv o, uint32_t args, uint32_t argc) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    if (argc) {
        for (double k = n; k > 0; k--) if (!ga_move(vm, o, k - 1, k + argc - 1)) return JV_HOLE;
        for (uint32_t j = 0; j < argc; j++)
            if (!js_set_throw(vm, o, jv_num(j), arg(vm, args, argc, j))) return JV_HOLE;
    }
    if (!ga_set_len(vm, o, n + argc)) return JV_HOLE;
    return jv_num(n + argc);
}

/* splice's start and delete count, its arguments' ToIntegerOrInfinity clamped: 0 thrown */
static int ga_splice_args(js_vm *vm, uint32_t args, uint32_t argc, double n, double *start, double *dc) {
    double s = int_arg(vm, args, argc, 0, 0);
    if (vm->throwing) return 0;
    s = ga_rel(s, n);
    double d = argc == 0 ? 0 : argc == 1 ? n - s : int_arg(vm, args, argc, 1, 0);
    if (vm->throwing) return 0;
    *start = s;
    *dc = d < 0 ? 0 : d > n - s ? n - s : d;
    return 1;
}

static jv ga_splice_at(js_vm *vm, jv o, double n, double start, double dc, uint32_t args, uint32_t argc) {
    uint32_t ins = argc > 2 ? argc - 2 : 0;
    jv sobj;
    if (array_species(vm, o, dc, &sobj) < 0) return JV_HOLE;
    /* the removed elements, a new array's (holes kept), the species' when it has one */
    uint32_t removed = js_array_new(vm, 0);
    for (double k = 0; k < dc; k++) {
        int has = ga_has(vm, o, start + k);
        if (vm->throwing) return JV_HOLE;
        if (!has) continue;
        jv v = ga_get(vm, o, start + k);
        if (vm->throwing) return JV_HOLE;
        js_array_set(vm, removed, (uint32_t)k, v);
    }
    js_array_set_length(vm, removed, (uint32_t)dc);
    if (ins < dc) {
        for (double k = start; k < n - dc; k++) if (!ga_move(vm, o, k + dc, k + ins)) return JV_HOLE;
        for (double k = n; k > n - dc + ins; k--) if (!js_delete_throw(vm, o, jv_num(k - 1))) return JV_HOLE;
    } else if (ins > dc) {
        for (double k = n - dc; k > start; k--) if (!ga_move(vm, o, k + dc - 1, k + ins - 1)) return JV_HOLE;
    }
    for (uint32_t i = 0; i < ins; i++)
        if (!js_set_throw(vm, o, jv_num(start + i), arg(vm, args, argc, i + 2))) return JV_HOLE;
    if (!ga_set_len(vm, o, n - dc + ins)) return JV_HOLE;
    return sobj != JV_UNDEF ? species_fill(vm, sobj, removed, dc) : jv_obj(removed);
}

static jv ga_splice(js_vm *vm, jv o, uint32_t args, uint32_t argc) {
    double n = ga_len(vm, o), start = 0, dc = 0;
    if (n < 0 || !ga_splice_args(vm, args, argc, n, &start, &dc)) return JV_HOLE;
    return ga_splice_at(vm, o, n, start, dc, args, argc);
}

static jv ga_reverse(js_vm *vm, jv o) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    double mid = floor(n / 2);
    for (double lo = 0; lo < mid; lo++) {
        double up = n - lo - 1;
        jv lv = JV_UNDEF, uv = JV_UNDEF;
        int le = ga_has(vm, o, lo);
        if (vm->throwing) return JV_HOLE;
        if (le) { lv = ga_get(vm, o, lo); if (vm->throwing) return JV_HOLE; }
        int ue = ga_has(vm, o, up);
        if (vm->throwing) return JV_HOLE;
        if (ue) { uv = ga_get(vm, o, up); if (vm->throwing) return JV_HOLE; }
        int ok = 1;
        if (le && ue) ok = js_set_throw(vm, o, jv_num(lo), uv) && js_set_throw(vm, o, jv_num(up), lv);
        else if (ue) ok = js_set_throw(vm, o, jv_num(lo), uv) && js_delete_throw(vm, o, jv_num(up));
        else if (le) ok = js_delete_throw(vm, o, jv_num(lo)) && js_set_throw(vm, o, jv_num(up), lv);
        if (!ok) return JV_HOLE;
    }
    return o;
}

/* the sorted values written back from index 0, the rest of the length deleted */
static jv ga_sort_store(js_vm *vm, jv o, vector<jv> *v, double n) {
    double j = 0;
    for (; j < (double)len(*v); j++) if (!js_set_throw(vm, o, jv_num(j), at(*v, (size_t)j))) return JV_HOLE;
    for (; j < n; j++) if (!js_delete_throw(vm, o, jv_num(j))) return JV_HOLE;
    return o;
}

static jv ga_sort(js_vm *vm, jv o, jv cmp) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    if (n < 2) return o;                        /* V8's: nothing read or written */
    vector<jv> v, tmp;
    for (double k = 0; k < n; k++) {
        int has = ga_has(vm, o, k);
        if (vm->throwing) return JV_HOLE;
        if (!has) continue;
        jv x = ga_get(vm, o, k);
        if (vm->throwing) return JV_HOLE;
        push_back(v, x);
    }
    resize(tmp, len(v));
    int err = 0;
    merge_sort(vm, &v, &tmp, 0, len(v), cmp, &err);
    if (err || vm->throwing) return JV_HOLE;
    return ga_sort_store(vm, o, &v, n);
}

/* fill's start and end, its arguments' ToIntegerOrInfinity relative to n: 0 thrown */
static int ga_fill_args(js_vm *vm, uint32_t args, uint32_t argc, double n, double *from, double *to) {
    double s = int_arg(vm, args, argc, 1, 0);
    if (vm->throwing) return 0;
    double e = int_arg(vm, args, argc, 2, n);
    if (vm->throwing) return 0;
    *from = ga_rel(s, n);
    *to = ga_rel(e, n);
    return 1;
}
static jv ga_fill_at(js_vm *vm, jv o, double from, double to, jv val) {
    for (double k = from; k < to; k++) if (!js_set_throw(vm, o, jv_num(k), val)) return JV_HOLE;
    return o;
}
static jv ga_fill(js_vm *vm, jv o, uint32_t args, uint32_t argc) {
    double n = ga_len(vm, o), from = 0, to = 0;
    if (n < 0 || !ga_fill_args(vm, args, argc, n, &from, &to)) return JV_HOLE;
    return ga_fill_at(vm, o, from, to, arg(vm, args, argc, 0));
}

/* Array.prototype.copyWithin (23.1.3.4): a plain array's slots moved, else element by element */
static jv ga_copy_within(js_vm *vm, jv o, uint32_t args, uint32_t argc) {
    double n = ga_len(vm, o);
    if (n < 0) return JV_HOLE;
    double to = int_arg(vm, args, argc, 0, 0);
    if (vm->throwing) return JV_HOLE;
    double from = int_arg(vm, args, argc, 1, 0);
    if (vm->throwing) return JV_HOLE;
    double end = int_arg(vm, args, argc, 2, n);
    if (vm->throwing) return JV_HOLE;
    to = ga_rel(to, n);
    from = ga_rel(from, n);
    end = ga_rel(end, n);
    double count = end - from < n - to ? end - from : n - to;
    if (count <= 0) return o;
    if (plain_array(vm, o)) {
        uint32_t a = jv_idx(o), b = vm->objs[a].a, t = (uint32_t)to, f = (uint32_t)from, c = (uint32_t)count;
        if (f < t && t < f + c) for (uint32_t i = c; i-- > 0;) vm->slots[b + t + i] = vm->slots[b + f + i];
        else for (uint32_t i = 0; i < c; i++) vm->slots[b + t + i] = vm->slots[b + f + i];
        return o;
    }
    double dir = 1;
    if (from < to && to < from + count) { dir = -1; from += count - 1; to += count - 1; }
    for (; count > 0; count--, from += dir, to += dir)
        if (!ga_move(vm, o, from, to)) return JV_HOLE;
    return o;
}

jv js_native_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = arg(vm, args, argc, 0), a1 = arg(vm, args, argc, 1);
    /* (an Array.prototype method's receiver undefined or null: each case's first test,
       AP_ON_NOTHING, where the standard asks ToObject) */
#define AP_ON_NOTHING() do { if (jv_is_nullish(thisv) || thisv == JV_HOLE) return array_on_nothing(vm, id, callee); } while (0)
    switch (id) {
    case N_PARSE_INT: {
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        s = str_trim(vm, s, 1, 1);
        double radix = js_to_number(vm, a1);
        int r = radix != radix ? 0 : js_to_int32(radix);
        js_str_flatten(vm, s);
        uint32_t o = vm->strs[s].off, n = vm->strs[s].len, i = 0;
        int neg = 0;
        if (i < n && (vm->units[o] == '-' || vm->units[o] == '+')) { neg = vm->units[o] == '-'; i++; }
        if ((r == 0 || r == 16) && i + 1 < n && vm->units[o + i] == '0' && (vm->units[o + i + 1] == 'x' || vm->units[o + i + 1] == 'X')) { i += 2; r = 16; }
        if (r == 0) r = 10;
        if (r < 2 || r > 36) return jv_num(NAN);
        double v = 0;
        int digits = 0;
        for (; i < n; i++) {
            uint16_t c = vm->units[o + i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
            if (d >= r) break;
            v = v * r + d;
            digits++;
        }
        return jv_num(digits ? (neg ? -v : v) : NAN);
    }
    case N_PARSE_FLOAT: {
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        s = str_trim(vm, s, 1, 0);
        js_str_flatten(vm, s);
        char b[400];
        uint32_t k = 0, o = vm->strs[s].off, n = vm->strs[s].len;
        for (uint32_t i = 0; i < n && k + 1 < sizeof b; i++) {
            uint16_t c = vm->units[o + i];
            if (c > 127) break;
            b[k++] = (char)c;
        }
        b[k] = 0;
        if (strncmp(b, "Infinity", 8) == 0 || strncmp(b, "+Infinity", 9) == 0) return jv_num(INFINITY);
        if (strncmp(b, "-Infinity", 9) == 0) return jv_num(-INFINITY);
        char *end = b;
        if ((b[0] == '0' && (b[1] == 'x' || b[1] == 'X'))) return jv_num(0);
        double v = strtod(b, &end);
        return jv_num(end == b ? NAN : v);
    }
    case N_IS_NAN: { double d = js_to_number(vm, a0); return jv_bool(d != d); }
    case N_IS_FINITE: { double d = js_to_number(vm, a0); return jv_bool(d == d && d != INFINITY && d != -INFINITY); }
    /* eval called by any other name than eval itself (indirect): its code in the global scope */
    case N_EVAL: return js_eval_code(vm, a0, JS_CC_EVAL, 0, jv_obj(vm->global), 0);
    case N_OBJECT:
        if (jv_is_nullish(a0)) return jv_obj(js_obj_new(vm, JK_PLAIN, vm->object_proto));
        return js_to_object(vm, a0);
    case N_FUNCTION: {
        /* new Function(p1, ..., body): the parameters' text joined by commas and the body's
           read as one function expression, in the global scope */
        uint32_t src = js_str_ascii(vm, "(function anonymous(");
        for (uint32_t i = 0; i + 1 < argc; i++) {
            uint32_t ps = js_to_string(vm, arg(vm, args, argc, i));
            if (!ps) return JV_HOLE;
            if (i) src = js_str_concat(vm, src, js_str_ascii(vm, ","));
            src = js_str_concat(vm, src, ps);
        }
        src = js_str_concat(vm, src, js_str_ascii(vm, "\n) {\n"));
        if (argc) {
            uint32_t bs = js_to_string(vm, arg(vm, args, argc, argc - 1));
            if (!bs) return JV_HOLE;
            src = js_str_concat(vm, src, bs);
        }
        src = js_str_concat(vm, src, js_str_ascii(vm, "\n})"));
        return js_eval_code(vm, jv_str(src), JS_CC_FUNCTION, 0, jv_obj(vm->global), 0);
    }
    case N_ARRAY: {
        if (argc == 1 && jv_is_num(a0)) {
            double d = jv_dbl(a0);
            if (d < 0 || d != trunc(d) || d > 4294967295.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid array length");
            uint32_t a = js_array_new(vm, 0);
            js_array_set_length(vm, a, (uint32_t)d);
            return jv_obj(a);
        }
        uint32_t a = js_array_new(vm, 0);
        for (uint32_t i = 0; i < argc; i++) js_array_push(vm, a, arg(vm, args, argc, i));
        return jv_obj(a);
    }
    case N_STRING: {
        if (jv_is_sym(a0) && (newt == JV_UNDEF || newt == JV_HOLE)) return jv_str(js_sym_text(vm, jv_idx(a0)));
        uint32_t s = argc ? js_to_string(vm, a0) : A_EMPTY;
        if (!s) return JV_HOLE;
        if (newt != JV_UNDEF && newt != JV_HOLE) return js_to_object(vm, jv_str(s));
        return jv_str(s);
    }
    case N_NUMBER: {
        /* Number(big): the BigInt's value, rounded */
        if (argc) {
            jv p = js_to_primitive(vm, a0, 0);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_big(p)) {
                jv r = jv_num(js_big_to_double(vm, p));
                return newt != JV_UNDEF && newt != JV_HOLE ? js_to_object(vm, r) : r;
            }
        }
        double d = argc ? js_to_number(vm, a0) : 0;
        if (newt != JV_UNDEF && newt != JV_HOLE) return js_to_object(vm, jv_num(d));
        return jv_num(d);
    }
    case N_BOOLEAN: {
        jv b = jv_bool(js_truthy(vm, a0));
        if (newt != JV_UNDEF && newt != JV_HOLE) return js_to_object(vm, b);
        return b;
    }
    case N_ERROR: return make_error(vm, vm->error_proto, args, argc);
    case N_TYPE_ERROR: return make_error(vm, vm->type_error_proto, args, argc);
    case N_RANGE_ERROR: return make_error(vm, vm->range_error_proto, args, argc);
    case N_SYNTAX_ERROR: return make_error(vm, vm->syntax_error_proto, args, argc);
    case N_REFERENCE_ERROR: return make_error(vm, vm->reference_error_proto, args, argc);
    case N_EVAL_ERROR: return make_error(vm, vm->eval_error_proto, args, argc);

    /* ---- Symbol ---- */
    case N_SYMBOL: {
        if (newt != JV_UNDEF && newt != JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Symbol is not a constructor");
        uint32_t d = 0;
        if (argc && a0 != JV_UNDEF) { d = js_to_string(vm, a0); if (!d) return JV_HOLE; }
        return jv_sym(js_sym_new(vm, d));
    }
    case N_SYM_FOR: {
        uint32_t k = js_to_string(vm, a0);
        if (!k) return JV_HOLE;
        uint32_t ka = js_atom(vm, k);
        for (size_t i = 1; i < len(vm->syms); i++) if (vm->syms[i].reg == ka + 1) return jv_sym((uint32_t)i);
        uint32_t y = js_sym_new(vm, ka);
        vm->syms[y].reg = ka + 1;
        return jv_sym(y);
    }
    case N_SYM_KEYFOR: {
        if (!jv_is_sym(a0)) return value_error(vm, "", a0, " is not a symbol");
        uint32_t r = vm->syms[jv_idx(a0)].reg;
        return r ? jv_str(r - 1) : JV_UNDEF;
    }
    case N_SYMP_TOSTRING: case N_SYMP_VALUEOF: case N_SYMP_DESC: {
        jv y = thisv;
        if (jv_is_obj(y) && vm->objs[jv_idx(y)].kind == JK_SYMBOL) y = vm->objs[jv_idx(y)].x;
        if (!jv_is_sym(y))
            return js_throw_msg(vm, vm->type_error_proto, id == N_SYMP_VALUEOF ? "Symbol.prototype.valueOf requires that 'this' be a Symbol" :
                                id == N_SYMP_DESC ? "Symbol.prototype.description requires that 'this' be a Symbol" :
                                "Symbol.prototype.toString requires that 'this' be a Symbol");
        if (id == N_SYMP_VALUEOF) return y;
        if (id == N_SYMP_DESC) { uint32_t d = vm->syms[jv_idx(y)].desc; return d ? jv_str(d) : JV_UNDEF; }
        return jv_str(js_sym_text(vm, jv_idx(y)));
    }
    case N_OBJ_GETOWNSYMS: {
        jv o = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        uint32_t out = js_array_new(vm, 0);
        js_own_symbols(vm, jv_idx(o), out);
        return jv_obj(out);
    }
    case N_ITP_SELF: return thisv;
    case N_SP_ITER: {
        uint32_t st = js_to_string(vm, thisv);
        if (!st) return JV_HOLE;
        return iter_of_coll(vm, st, JI_STRING);
    }
    /* ---- generators ---- */
    case N_GEN_NEXT: case N_GEN_RETURN: case N_GEN_THROW: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_GENERATOR)
            return value_error(vm, id == N_GEN_NEXT ? "Method [Generator].prototype.next called on incompatible receiver " :
                                   id == N_GEN_RETURN ? "Method [Generator].prototype.return called on incompatible receiver " :
                                   "Method [Generator].prototype.throw called on incompatible receiver ", thisv, "");
        int done = 0;
        jv r = js_gen_resume(vm, jv_idx(thisv), a0, id == N_GEN_NEXT ? 0 : id == N_GEN_THROW ? 1 : 2, &done);
        if (vm->throwing) return JV_HOLE;
        return iter_result(vm, r, done);
    }
    /* ---- async generators ---- */
    case N_AG_NEXT: case N_AG_RETURN: case N_AG_THROW: {
        uint32_t p = js_promise_new(vm);
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ASYNCGEN) {
            /* the TypeError as the request's rejection */
            value_error(vm, id == N_AG_NEXT ? "Method [AsyncGenerator].prototype.next called on incompatible receiver " :
                            id == N_AG_RETURN ? "Method [AsyncGenerator].prototype.return called on incompatible receiver " :
                            "Method [AsyncGenerator].prototype.throw called on incompatible receiver ", thisv, "");
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, e);
            return jv_obj(p);
        }
        ag_request(vm, jv_idx(thisv), id == N_AG_NEXT ? 0 : id == N_AG_THROW ? 1 : 2, a0, p);
        if (vm->throwing) return JV_HOLE;
        return jv_obj(p);
    }
    case N_AG_RETURN_OK: case N_AG_RETURN_ERR: {
        uint32_t g = jv_idx(vm->objs[jv_idx(callee)].x);
        vm->objs[g].a = JG_DONE;
        js_ag_complete(vm, g, id == N_AG_RETURN_ERR, a0, 1);
        js_ag_drain(vm, g);
        return JV_UNDEF;
    }
    case N_AFS_UNWRAP: return iter_result(vm, a0, vm->objs[jv_idx(callee)].x == JV_TRUE);
    /* gc(): a collection now, for a test's script (called where no native's work is under way) */
    case N_GC: js_gc(vm); return JV_UNDEF;
    case N_AFS_CLOSE: {
        /* IteratorClose with the rejection as its completion: what the closing throws is lost */
        js_iter_close(vm, jv_idx(vm->objs[jv_idx(callee)].x));
        if (vm->throwing == 2) return JV_HOLE;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        return js_throw(vm, a0);
    }
    /* ---- Promise ---- */
    case N_PROMISE: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Promise constructor cannot be invoked without 'new'");
        if (!js_is_callable(vm, a0)) return value_error(vm, "Promise resolver ", a0, " is not a function");
        uint32_t p = js_promise_new(vm);
        jv res, rej;
        promise_resolvers(vm, p, &res, &rej);
        jv a2[2] = { res, rej };
        js_call(vm, a0, JV_UNDEF, a2, 2);
        if (vm->throwing == 1) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            jv recv;
            uint32_t f;
            if (js_get_own(vm, jv_idx(rej), A_INDEX, &recv, &f) && jv_is_obj(recv) && !vm->objs[jv_idx(recv)].a) {
                vm->objs[jv_idx(recv)].a = 1;
                js_promise_settle(vm, p, 1, e);
            }
        }
        if (vm->throwing) return JV_HOLE;
        return jv_obj(p);
    }
    case N_PR_RESOLVE_FN: case N_PR_REJECT_FN: {
        uint32_t f = jv_idx(callee), p = jv_idx(vm->objs[f].x);
        jv recv;
        uint32_t fl;
        if (js_get_own(vm, f, A_INDEX, &recv, &fl) && jv_is_obj(recv)) {
            if (vm->objs[jv_idx(recv)].a) return JV_UNDEF;
            vm->objs[jv_idx(recv)].a = 1;
        }
        if (id == N_PR_RESOLVE_FN) js_promise_resolve_with(vm, p, a0);
        else js_promise_settle(vm, p, 1, a0);
        return JV_UNDEF;
    }
    case N_PRP_THEN: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_PROMISE)
            return value_error(vm, "Method Promise.prototype.then called on incompatible receiver ", thisv, "");
        jv c = species_ctor(vm, thisv, jv_obj(vm->promise_ctor));
        if (vm->throwing) return JV_HOLE;
        jv target;
        jv d = promise_cap(vm, c, &target);
        if (vm->throwing) return JV_HOLE;
        js_promise_react(vm, jv_idx(thisv), js_is_callable(vm, a0) ? a0 : JV_UNDEF, js_is_callable(vm, a1) ? a1 : JV_UNDEF, target, 0);
        return d;
    }
    case N_PRP_CATCH: return promise_invoke_then(vm, thisv, JV_UNDEF, a0, 2);
    case N_PRP_FINALLY: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Promise.prototype.finally called on non-object");
        jv c = species_ctor(vm, thisv, jv_obj(vm->promise_ctor));
        if (vm->throwing) return JV_HOLE;
        jv ok = a0, er = a0;
        if (js_is_callable(vm, a0)) {
            /* thenFinally and catchFinally: the function and the constructor theirs */
            uint32_t box = js_array_new(vm, 0);
            js_array_push(vm, box, a0);
            js_array_push(vm, box, c);
            uint32_t f1 = js_native_new(vm, N_PR_FIN_OK, A_EMPTY, 1), f2 = js_native_new(vm, N_PR_FIN_ERR, A_EMPTY, 1);
            vm->objs[f1].x = jv_obj(box);
            vm->objs[f2].x = jv_obj(box);
            ok = jv_obj(f1);
            er = jv_obj(f2);
        }
        return promise_invoke_then(vm, thisv, ok, er, 2);
    }
    case N_PR_FIN_OK: case N_PR_FIN_ERR: {
        /* finally's handler: its function run, then the value (or the reason) passed on once what that returned has
           settled, through a promise of the constructor */
        uint32_t box = jv_idx(vm->objs[jv_idx(callee)].x);
        jv r = js_call(vm, js_array_get(vm, box, 0), JV_UNDEF, 0, 0);
        if (vm->throwing) return JV_HOLE;
        jv pr = promise_resolve_c(vm, js_array_get(vm, box, 1), r);
        if (vm->throwing) return JV_HOLE;
        uint32_t th = js_native_new(vm, id == N_PR_FIN_OK ? N_PR_THUNK_VALUE : N_PR_THUNK_THROW, A_EMPTY, 0);
        vm->objs[th].x = a0;
        return promise_invoke_then(vm, pr, jv_obj(th), JV_UNDEF, 1);
    }
    case N_PR_THUNK_VALUE: return vm->objs[jv_idx(callee)].x;
    case N_PR_THUNK_THROW: return js_throw(vm, vm->objs[jv_idx(callee)].x);
    case N_PR_RESOLVE: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "PromiseResolve called on non-object");
        return promise_resolve_c(vm, thisv, a0);
    }
    case N_PR_REJECT: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "PromiseReject called on non-object");
        jv target;
        jv p = promise_cap(vm, thisv, &target);
        if (vm->throwing) return JV_HOLE;
        promise_target_settle(vm, target, 1, a0);
        return vm->throwing ? JV_HOLE : p;
    }
    case N_PR_WITHRESOLVERS: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Promise.withResolvers called on non-object");
        jv target, res, rej;
        jv p = promise_cap(vm, thisv, &target);
        if (vm->throwing) return JV_HOLE;
        promise_target_fns(vm, target, &res, &rej);
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        js_define(vm, o, js_atom_ascii(vm, "promise"), p, 0);
        js_define(vm, o, js_atom_ascii(vm, "resolve"), res, 0);
        js_define(vm, o, js_atom_ascii(vm, "reject"), rej, 0);
        return jv_obj(o);
    }
    case N_PR_CAP_EXEC: {
        /* GetCapabilitiesExecutor: the resolving functions kept, once */
        uint32_t rec = jv_idx(vm->objs[jv_idx(callee)].x);
        if (js_array_get(vm, rec, 1) != JV_UNDEF || js_array_get(vm, rec, 2) != JV_UNDEF)
            return js_throw_msg(vm, vm->type_error_proto, "Promise executor has already been invoked with non-undefined arguments");
        js_array_set(vm, rec, 1, a0);
        js_array_set(vm, rec, 2, a1);
        return JV_UNDEF;
    }
    case N_SPECIES: return thisv;
    case N_PR_ALL: case N_PR_ALLSETTLED: case N_PR_RACE: case N_PR_ANY: {
        static const char *const pn[] = { "Promise.all called on non-object", "Promise.allSettled called on non-object",
                                          "Promise.race called on non-object", "Promise.any called on non-object" };
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, pn[id - N_PR_ALL]);
        jv target;
        jv p = promise_cap(vm, thisv, &target);
        if (vm->throwing) return JV_HOLE;
        jv res, rej;
        promise_target_fns(vm, target, &res, &rej);
        /* GetPromiseResolve, then the iterator: what either throws, the promise's rejection */
        jv presolve = js_get(vm, thisv, js_atom_ascii(vm, "resolve"));
        if (!vm->throwing && !js_is_callable(vm, presolve)) js_not_function(vm, presolve);
        jv it = vm->throwing ? JV_UNDEF : js_iter_of(vm, a0);
        if (vm->throwing == 1) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            promise_target_settle(vm, target, 1, e);
            return vm->throwing ? JV_HOLE : p;
        }
        if (vm->throwing) return JV_HOLE;
        /* the aggregate: a the values (any: the errors), b how many are still waiting (one more until the iterator
           is done), c its kind, x the capability's target */
        uint32_t agg = 0, vals = 0;
        if (id != N_PR_RACE) {
            agg = js_obj_new(vm, JK_RECORD, 0);
            vals = js_array_new(vm, 0);
            vm->objs[agg].a = vals;
            vm->objs[agg].b = 1;
            vm->objs[agg].c = (id == N_PR_ALL ? 0u : id == N_PR_ALLSETTLED ? 1u : 2u) << 8;
            vm->objs[agg].x = target;
        }
        int iter_done = 0;
        for (uint32_t i = 0;; i++) {
            jv v;
            int more = js_iter_step(vm, jv_idx(it), &v);
            if (vm->throwing) { iter_done = 1; break; }
            if (!more) break;
            jv np = js_call(vm, presolve, thisv, &v, 1);
            if (vm->throwing) break;
            jv ok = res, er = rej;
            if (id != N_PR_RACE) {
                js_array_push(vm, vals, JV_UNDEF);
                vm->objs[agg].b++;
                if (id != N_PR_ANY) ok = promise_elem_fn(vm, N_PR_ELEM_OK, agg, i);
                if (id != N_PR_ALL) er = promise_elem_fn(vm, N_PR_ELEM_ERR, agg, i);
            }
            promise_invoke_then(vm, np, ok, er, 2);
            if (vm->throwing) break;
        }
        if (vm->throwing == 1) {
            /* the step's error: the iterator closed (unless it threw itself), the promise rejected */
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            if (!iter_done) {
                js_iter_close(vm, jv_idx(it));
                if (vm->throwing == 1) { vm->throwing = 0; vm->exception = JV_UNDEF; }
            }
            promise_target_settle(vm, target, 1, e);
            return vm->throwing ? JV_HOLE : p;
        }
        if (vm->throwing) return JV_HOLE;
        if (id != N_PR_RACE && --vm->objs[agg].b == 0) promise_agg_done(vm, agg);
        return vm->throwing ? JV_HOLE : p;
    }
    case N_PR_ELEM_OK: case N_PR_ELEM_ERR: {
        uint32_t rec = jv_idx(vm->objs[jv_idx(callee)].x);
        if (vm->objs[rec].c) return JV_UNDEF;
        vm->objs[rec].c = 1;
        uint32_t agg = vm->objs[rec].a, i = vm->objs[rec].b, kind = vm->objs[agg].c >> 8;
        jv v = a0;
        if (kind == 1) {
            uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
            js_define(vm, o, A_STATUS, jv_str(id == N_PR_ELEM_OK ? A_FULFILLED : A_REJECTED), 0);
            js_define(vm, o, id == N_PR_ELEM_OK ? A_VALUE : A_REASON, a0, 0);
            v = jv_obj(o);
        }
        js_array_set(vm, vm->objs[agg].a, i, v);
        if (--vm->objs[agg].b == 0) promise_agg_done(vm, agg);
        return vm->throwing ? JV_HOLE : JV_UNDEF;
    }
    case N_AGGREGATE_ERROR: {
        uint32_t errs = js_array_new(vm, 0);
        if (!iterate_into(vm, a0, errs)) return JV_HOLE;
        uint32_t m = argc > 1 && a1 != JV_UNDEF ? js_to_string(vm, a1) : A_EMPTY;
        if (!m) return JV_HOLE;
        uint32_t e = js_error_new(vm, vm->aggregate_error_proto, m);
        js_define(vm, e, A_ERRORS, jv_obj(errs), JS_SHAPE_HIDDEN);
        return jv_obj(e);
    }

    case N_MAP: case N_SET: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, id == N_MAP ? "Constructor Map requires 'new'" : "Constructor Set requires 'new'");
        uint32_t kind = id == N_MAP ? JK_MAP : JK_SET;
        uint32_t c = coll_new(vm, kind, kind == JK_MAP ? vm->map_proto : vm->set_proto);
        if (!jv_is_nullish(a0)) {
            uint32_t items = js_array_new(vm, 0);
            if (!iterate_into(vm, a0, items)) return JV_HOLE;
            uint32_t n = vm->objs[items].b;
            for (uint32_t i = 0; i < n && !vm->throwing; i++) {
                jv v = js_array_get(vm, items, i);
                if (kind == JK_MAP) {
                    if (!jv_is_obj(v)) return value_error(vm, "Iterator value ", v, " is not an entry object");
                    jv k = js_get_value(vm, v, jv_num(0)), val = js_get_value(vm, v, jv_num(1));
                    if (vm->throwing) return JV_HOLE;
                    js_coll_op(vm, JCOLL_SET, c, k, val);
                } else js_coll_op(vm, JCOLL_ADD, c, v, JV_UNDEF);
            }
        }
        return jv_obj(c);
    }
    /* ---- Object ---- */
    case N_OBJ_KEYS: return obj_keys_like(vm, a0, 0);
    case N_OBJ_VALUES: return obj_keys_like(vm, a0, 1);
    case N_OBJ_ENTRIES: return obj_keys_like(vm, a0, 2);
    case N_OBJ_ASSIGN: {
        jv t = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        for (uint32_t i = 1; i < argc && !vm->throwing; i++) {
            jv src = arg(vm, args, argc, i);
            if (jv_is_nullish(src)) continue;
            jv so = js_to_object(vm, src);
            if (vm->throwing) return JV_HOLE;
            /* Set(to, key, value, true) for each own enumerable key, symbols' too: a refusal a TypeError */
            if (!js_copy_own(vm, jv_idx(t), jv_idx(so), 1)) return JV_HOLE;
        }
        return vm->throwing ? JV_HOLE : t;
    }
    case N_OBJ_CREATE: {
        if (!jv_is_obj(a0) && a0 != JV_NULL) return value_error(vm, "Object prototype may only be an Object or null: ", a0, "");
        uint32_t o = js_obj_new(vm, JK_PLAIN, a0 == JV_NULL ? 0 : jv_idx(a0));
        if (jv_is_obj(a1)) {
            uint32_t keys = js_array_new(vm, 0);
            js_own_keys(vm, jv_idx(a1), 1, keys);
            for (uint32_t k = 0; k < vm->objs[keys].b && !vm->throwing; k++) {
                uint32_t atom = js_atom(vm, jv_idx(js_array_get(vm, keys, k)));
                jv d = js_get(vm, a1, atom);
                if (jv_is_obj(d)) js_define(vm, o, atom, js_get(vm, d, A_VALUE), js_truthy(vm, js_get(vm, d, A_ENUMERABLE)) ? 0 : JS_SHAPE_HIDDEN);
            }
        }
        return jv_obj(o);
    }
    case N_OBJ_GETPROTO: {
        jv o = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        if (vm->objs[jv_idx(o)].kind == JK_PROXY) return js_proxy_get_proto(vm, jv_idx(o));
        uint32_t p = vm->objs[jv_idx(o)].proto;
        return p ? jv_obj(p) : JV_NULL;
    }
    case N_OBJ_SETPROTO: {
        if (jv_is_nullish(a0)) return js_throw_msg(vm, vm->type_error_proto, "Object.setPrototypeOf called on null or undefined");
        if (!jv_is_obj(a1) && a1 != JV_NULL) return value_error(vm, "Object prototype may only be an Object or null: ", a1, "");
        if (!jv_is_obj(a0)) return a0;
        int r = js_set_proto_of(vm, jv_idx(a0), a1);
        if (r < 0) return JV_HOLE;
        if (!r) return proto_refused(vm, a0);
        return a0;
    }
    /* %ThrowTypeError% */
    case N_THROW_TE:
        return js_throw_msg(vm, vm->type_error_proto,
                            "'caller', 'callee', and 'arguments' properties may not be accessed on strict mode functions or the arguments objects for calls to them");
    /* Object.prototype.__proto__ (ECMA-262 B.2.2.1) */
    case N_OP_PROTO_GET: {
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        uint32_t x = jv_idx(o);
        if (vm->objs[x].kind == JK_PROXY) return js_proxy_get_proto(vm, x);
        return vm->objs[x].proto ? jv_obj(vm->objs[x].proto) : JV_NULL;
    }
    case N_OP_PROTO_SET: {
        if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "set Object.prototype.__proto__ called on null or undefined");
        if ((!jv_is_obj(a0) && a0 != JV_NULL) || !jv_is_obj(thisv)) return JV_UNDEF;
        int r = js_set_proto_of(vm, jv_idx(thisv), a0);
        if (r < 0) return JV_HOLE;
        if (!r) return proto_refused(vm, thisv);
        return JV_UNDEF;
    }
    case N_OBJ_DEFPROP: case N_OBJ_DEFPROPS:
        /* over the descriptors: src/js/proxy.c's, which keeps writable and configurable */
        return js_proxy_native(vm, id == N_OBJ_DEFPROP ? JPX_OBJ_DEFPROP : JPX_OBJ_DEFPROPS, callee, thisv, args, argc, newt);
    case N_OBJ_NAMES: {
        jv o = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        uint32_t keys = js_array_new(vm, 0);
        js_own_keys(vm, jv_idx(o), 0, keys);
        return jv_obj(keys);
    }
    case N_OBJ_FREEZE: case N_OBJ_PREVENTEXT:
        if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_PROXY) {
            js_proxy_native(vm, JPX_R_PREVENTEXT, callee, thisv, args, argc, newt);
            return vm->throwing ? JV_HOLE : a0;
        }
        if (jv_is_obj(a0)) {
            /* a mapped arguments object frozen: its elements no longer its parameters (each made not writable) */
            if (id == N_OBJ_FREEZE) js_args_unmap_all(vm, jv_idx(a0));
            vm->objs[jv_idx(a0)].flags &= (uint16_t)~JO_EXTENSIBLE;
            if (id == N_OBJ_FREEZE) vm->objs[jv_idx(a0)].flags |= JO_FROZEN;
        }
        return a0;
    case N_OBJ_ISFROZEN: return jv_bool(!jv_is_obj(a0) || (vm->objs[jv_idx(a0)].flags & JO_FROZEN));
    case N_OBJ_ISEXT: case N_OBJ_ISSEALED: {
        if (!jv_is_obj(a0)) return jv_bool(id == N_OBJ_ISSEALED);
        if (vm->objs[jv_idx(a0)].kind == JK_PROXY) {
            jv r = js_proxy_native(vm, JPX_R_ISEXTENSIBLE, callee, thisv, args, argc, newt);
            if (vm->throwing) return JV_HOLE;
            return id == N_OBJ_ISEXT ? r : jv_bool(r == JV_FALSE);
        }
        int ext = (vm->objs[jv_idx(a0)].flags & JO_EXTENSIBLE) != 0;
        return jv_bool(id == N_OBJ_ISEXT ? ext : !ext);
    }
    case N_OBJ_HASOWN: {
        jv o = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        jv a2[1] = { a1 };
        return js_call(vm, js_get(vm, jv_obj(vm->object_proto), js_atom_ascii(vm, "hasOwnProperty")), o, a2, 1);
    }
    case N_OBJ_GETOWNDESCS: {
        jv o = js_to_object(vm, a0);
        if (vm->throwing) return JV_HOLE;
        uint32_t keys = js_array_new(vm, 0);
        js_own_keys(vm, jv_idx(o), 0, keys);
        js_own_symbols(vm, jv_idx(o), keys);
        uint32_t out = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        jv gd = js_get(vm, jv_obj(vm->global), js_atom_ascii(vm, "Object"));
        jv gf = js_get(vm, gd, js_atom_ascii(vm, "getOwnPropertyDescriptor"));
        for (uint32_t i = 0; i < vm->objs[keys].b && !vm->throwing; i++) {
            jv k = js_array_get(vm, keys, i);
            jv av[2] = { o, k };
            jv d = js_call(vm, gf, gd, av, 2);
            if (vm->throwing) return JV_HOLE;
            if (d != JV_UNDEF) js_set_value(vm, jv_obj(out), k, d);
        }
        return vm->throwing ? JV_HOLE : jv_obj(out);
    }
    case N_OBJ_GROUPBY: case N_MAP_GROUPBY: {
        /* each value of the iterable under the key its callback gives: a null-prototype object's arrays, or a Map's */
        if (!js_is_callable(vm, a1)) return value_error(vm, "", a1, " is not a function");
        uint32_t items = js_array_new(vm, 0);
        if (!iterate_into(vm, a0, items)) return JV_HOLE;
        uint32_t out = id == N_MAP_GROUPBY ? coll_new(vm, JK_MAP, vm->map_proto) : js_obj_new(vm, JK_PLAIN, 0);
        for (uint32_t i = 0; i < vm->objs[items].b && !vm->throwing; i++) {
            jv v = js_array_get(vm, items, i);
            jv av[2] = { v, jv_num(i) };
            jv k = js_call(vm, a1, JV_UNDEF, av, 2);
            if (vm->throwing) return JV_HOLE;
            jv group;
            if (id == N_MAP_GROUPBY) {
                group = js_coll_op(vm, JCOLL_GET, out, k, JV_UNDEF);
                if (group == JV_UNDEF) { group = jv_obj(js_array_new(vm, 0)); js_coll_op(vm, JCOLL_SET, out, k, group); }
            } else {
                int kok = 1;
                uint32_t key = js_prop_key(vm, k, &kok);
                if (!kok) return JV_HOLE;
                jv g;
                uint32_t f;
                if (js_get_own(vm, out, key, &g, &f)) group = g;
                else { group = jv_obj(js_array_new(vm, 0)); js_define(vm, out, key, group, 0); }
            }
            js_array_push(vm, jv_idx(group), v);
        }
        return vm->throwing ? JV_HOLE : jv_obj(out);
    }
    case N_OBJ_FROMENTRIES: {
        /* the iterable's entries, each an object of a key and a value (V8's words else) */
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        uint32_t items = js_array_new(vm, 0);
        if (!iterate_into(vm, a0, items)) return JV_HOLE;
        for (uint32_t i = 0; i < vm->objs[items].b && !vm->throwing; i++) {
            jv e = js_array_get(vm, items, i);
            if (!jv_is_obj(e)) return value_error(vm, "Iterator value ", e, " is not an entry object");
            jv k = js_get_value(vm, e, jv_num(0));
            if (vm->throwing) return JV_HOLE;
            jv v = js_get_value(vm, e, jv_num(1));
            if (vm->throwing) return JV_HOLE;
            js_set_value(vm, jv_obj(o), k, v);
        }
        return vm->throwing ? JV_HOLE : jv_obj(o);
    }
    case N_OBJ_GETOWNDESC: return js_proxy_native(vm, JPX_OBJ_GETOWNDESC, callee, thisv, args, argc, newt);
    case N_OBJ_IS: {
        if (jv_is_num(a0) && jv_is_num(a1)) {
            double x = jv_dbl(a0), y = jv_dbl(a1);
            if (x != x && y != y) return JV_TRUE;
            return jv_bool(x == y && signbit(x) == signbit(y));
        }
        return jv_bool(js_strict_eq(vm, a0, a1));
    }
    case N_OP_HASOWN: {
        /* ToPropertyKey, then ToObject (ECMA-262 20.1.3.2); the shape's own first, then what
           the kind keeps (an array's or arguments object's elements, a string's indexes, a
           function's name and length): its [[GetOwnProperty]] */
        int kok = 1;
        uint32_t atom = js_prop_key(vm, a0, &kok);
        if (!kok) return JV_HOLE;
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        uint32_t ob = jv_idx(o);
        jv v;
        uint32_t f;
        if (js_get_own(vm, ob, atom, &v, &f)) return JV_TRUE;
        if (vm->throwing) return JV_HOLE;
        if (vm->objs[ob].kind == JK_PLAIN || vm->objs[ob].kind == JK_PROXY) return JV_FALSE;
        int r = js_proxy_get_own(vm, ob, atom, &v, &f);
        return vm->throwing ? JV_HOLE : jv_bool(r);
    }
    case N_OP_TOLOCALE: {
        /* Invoke(this, "toString") */
        if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "Object.prototype.toLocaleString called on null or undefined");
        jv f = js_get_value(vm, thisv, jv_str(A_TOSTRING));
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, f)) return js_not_function(vm, f);
        return js_call(vm, f, thisv, 0, 0);
    }
    case N_OP_TOSTRING: {
        if (thisv == JV_UNDEF) return jv_str(js_str_ascii(vm, "[object Undefined]"));
        if (thisv == JV_NULL) return jv_str(js_str_ascii(vm, "[object Null]"));
        const char *t = "[object Object]";
        if (jv_is_num(thisv)) t = "[object Number]";
        else if (jv_is_str(thisv)) t = "[object String]";
        else if (jv_is_bool(thisv)) t = "[object Boolean]";
        else if (jv_is_big(thisv)) t = "[object BigInt]";
        else if (jv_is_sym(thisv)) t = "[object Symbol]";
        else if (jv_is_obj(thisv)) {
            uint32_t k = vm->objs[jv_idx(thisv)].kind;
            if (k == JK_PROXY) { if (is_array_spec(vm, thisv)) k = JK_ARRAY; else if (vm->throwing) return JV_HOLE; }
            if (k == JK_ARRAY) t = "[object Array]";
            else if (js_is_callable(vm, thisv)) t = "[object Function]";
            else if (k == JK_BOOLEAN) t = "[object Boolean]";
            else if (k == JK_NUMBER) t = "[object Number]";
            else if (k == JK_STRING) t = "[object String]";
            else if (k == JK_ERROR) t = "[object Error]";
            else if (k == JK_DATE) t = "[object Date]";
            else if (k == JK_REGEXP) t = "[object RegExp]";
            else if (k == JK_ARGUMENTS) t = "[object Arguments]";
            else if (k == JK_MAP) t = "[object Map]";
            else if (k == JK_SET) t = "[object Set]";
            else if (k == JK_PROMISE) t = "[object Promise]";
            else if (k == JK_GENERATOR) t = "[object Generator]";
            else if (k == JK_SYMBOL) t = "[object Symbol]";
            jv tag = js_get(vm, thisv, JS_SYM_KEY | JSYM_TO_STRING_TAG);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_str(tag)) {
                uint32_t r = js_str_concat(vm, js_str_concat(vm, js_str_ascii(vm, "[object "), jv_idx(tag)), js_str_ascii(vm, "]"));
                return jv_str(r);
            }
        }
        return jv_str(js_str_ascii(vm, t));
    }
    case N_OP_VALUEOF: return js_to_object(vm, thisv);
    case N_OP_ISPROTO: {
        if (!jv_is_obj(a0) || !jv_is_obj(thisv)) return JV_FALSE;
        for (uint32_t x = vm->objs[jv_idx(a0)].proto, d = 0; x && d < 10000; x = vm->objs[x].proto, d++) if (x == jv_idx(thisv)) return JV_TRUE;
        return JV_FALSE;
    }
    case N_OP_PROPENUM: {
        /* the own property's [[Enumerable]] (an array's length not, its elements as their
           attributes say, a string's indexes so) */
        int kok = 1;
        uint32_t key = js_prop_key(vm, a0, &kok);
        if (!kok) return JV_HOLE;
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        jv v;
        uint32_t f;
        int r = js_proxy_get_own(vm, jv_idx(o), key, &v, &f);
        if (vm->throwing) return JV_HOLE;
        return jv_bool(r && !(f & JS_SHAPE_HIDDEN));
    }
    /* ---- Function ---- */
    case N_FP_CALL: {
        if (!js_is_callable(vm, thisv)) return js_caller_not_function(vm, thisv);
        vector<jv> av;
        for (uint32_t i = 1; i < argc; i++) push_back(av, arg(vm, args, argc, i));
        return js_call(vm, thisv, a0, data(av), (uint32_t)len(av));
    }
    case N_FP_APPLY: {
        if (!js_is_callable(vm, thisv)) return js_caller_not_function(vm, thisv);
        vector<jv> av;
        if (!jv_is_nullish(a1)) {
            uint32_t n = (uint32_t)len_of(vm, a1);
            for (uint32_t i = 0; i < n && !vm->throwing; i++) push_back(av, elem_of(vm, a1, i));
        }
        if (vm->throwing) return JV_HOLE;
        return js_call(vm, thisv, a0, data(av), (uint32_t)len(av));
    }
    case N_FP_BIND: {
        /* BoundFunctionCreate (its prototype the target's), then its length (the target's own, less what is bound) and
           its name ("bound " and the target's), read-only and configurable (ECMA-262 20.2.3.2) */
        if (!js_is_callable(vm, thisv)) return js_throw_msg(vm, vm->type_error_proto, "Bind must be called on a function");
        jv tp = js_proxy_get_proto(vm, jv_idx(thisv));
        if (tp == JV_HOLE || vm->throwing) return JV_HOLE;
        uint32_t b = js_obj_new(vm, JK_BOUND, jv_is_obj(tp) ? jv_idx(tp) : 0);
        vm->objs[b].a = jv_idx(thisv);
        vm->objs[b].x = a0;
        uint32_t n = argc > 1 ? argc - 1 : 0;
        uint32_t off = (uint32_t)len(vm->slots);
        for (uint32_t i = 0; i < n; i++) push_back(vm->slots, arg(vm, args, argc, i + 1));
        vm->objs[b].b = off;
        vm->objs[b].c = n;
        uint32_t sp0 = vm->sp;
        if (!js_room(vm, sp0 + 1, vm->nframes)) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
        vm->stack[vm->sp++] = jv_obj(b);
        double L = 0;
        jv lv, nv;
        uint32_t lf;
        int has = js_proxy_get_own(vm, jv_idx(thisv), A_LENGTH, &lv, &lf);
        if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
        if (has) {
            lv = js_get(vm, thisv, A_LENGTH);
            if (vm->throwing) { vm->sp = sp0; return JV_HOLE; }
            if (jv_is_num(lv)) {
                double t = jv_dbl(lv);
                if (t == INFINITY) L = INFINITY;
                else if (t != -INFINITY) {
                    t = t != t ? 0 : trunc(t);
                    L = t - n > 0 ? t - n : 0;
                }
            }
        }
        nv = js_get(vm, thisv, A_NAME);
        vm->sp = sp0;
        if (vm->throwing) return JV_HOLE;
        uint32_t tn = jv_is_str(nv) ? jv_idx(nv) : A_EMPTY;
        js_define(vm, b, A_LENGTH, jv_num(L), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
        js_define(vm, b, A_NAME, jv_str(js_str_concat(vm, js_str_ascii(vm, "bound "), tn)), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
        return jv_obj(b);
    }
    case N_FP_TOSTRING: {
        /* a function's own source text, as written (ECMA-262 2019): src/js/vm.c's */
        uint32_t s = jv_is_obj(thisv) ? js_func_source(vm, jv_idx(thisv)) : 0;
        if (s) return jv_str(s);
        return js_throw_msg(vm, vm->type_error_proto, "Function.prototype.toString requires that 'this' be a Function");
    }
    /* ---- Array ---- */
    case N_ARR_ISARRAY: return jv_bool(is_array_spec(vm, a0));
    case N_ARR_FROM: {
        /* its mapper checked first; nothing is an iteration's error, as V8 words it */
        if (a1 != JV_UNDEF && !js_is_callable(vm, a1)) return js_not_function(vm, a1);
        if (jv_is_nullish(a0)) { js_iter_of(vm, a0); return JV_HOLE; }
        uint32_t out = js_array_new(vm, 0);
        jv src = a0;
        if (jv_is_obj(src) && (vm->objs[jv_idx(src)].kind == JK_MAP || vm->objs[jv_idx(src)].kind == JK_SET)) {
            uint32_t c = jv_idx(src);
            uint32_t arr = vm->objs[c].a, step = vm->objs[c].kind == JK_MAP ? 2 : 1;
            for (uint32_t i = 0; i < vm->objs[arr].b; i += step) {
                jv k = vm->slots[vm->objs[arr].a + i];
                if (k == JV_HOLE) continue;
                if (step == 1) js_array_push(vm, out, k);
                else {
                    uint32_t pair = js_array_new(vm, 0);
                    js_array_push(vm, pair, k);
                    js_array_push(vm, pair, vm->slots[vm->objs[arr].a + i + 1]);
                    js_array_push(vm, out, jv_obj(pair));
                }
            }
        } else if (jv_is_obj(src) && (vm->objs[jv_idx(src)].kind == JK_ITER || vm->objs[jv_idx(src)].kind == JK_GENERATOR ||
                                       js_has(vm, jv_idx(src), JS_KEY_ITERATOR))) {
            if (!iterate_into(vm, src, out)) return JV_HOLE;
        } else if (jv_is_str(src)) {
            uint32_t s = jv_idx(src);
            for (uint32_t i = 0; i < vm->strs[s].len; i++) {
                uint16_t c = js_str_at(vm, s, i);
                uint32_t w = 1;
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < vm->strs[s].len) {
                    uint16_t d = js_str_at(vm, s, i + 1);
                    if (d >= 0xDC00 && d <= 0xDFFF) w = 2;          /* a pair; a lone surrogate alone */
                }
                js_array_push(vm, out, jv_str(js_str_sub(vm, s, i, i + w)));
                i += w - 1;
            }
        } else {
            uint32_t n = (uint32_t)len_of(vm, src);
            for (uint32_t i = 0; i < n && !vm->throwing; i++) js_array_push(vm, out, elem_of(vm, src, i));
        }
        if (js_is_callable(vm, a1)) {
            jv ta = arg(vm, args, argc, 2);
            for (uint32_t i = 0; i < vm->objs[out].b && !vm->throwing; i++) {
                jv av[2] = { js_array_get(vm, out, i), jv_num(i) };
                jv r = js_call(vm, a1, ta, av, 2);
                js_array_set(vm, out, i, r);
            }
        }
        if (vm->throwing) return JV_HOLE;
        return into_ctor(vm, thisv, out, js_is_callable(vm, js_get(vm, a0, JS_KEY_ITERATOR)) ? 0 : 1);
    }
    case N_ARR_OF: {
        uint32_t a = js_array_new(vm, 0);
        for (uint32_t i = 0; i < argc; i++) js_array_push(vm, a, arg(vm, args, argc, i));
        return into_ctor(vm, thisv, a, 1);
    }
    /* the writing methods: a plain array's slots directly, anything else by the standard's
       steps (ga_*), whose writes throw where V8's do */
    case N_AP_PUSH: case N_AP_POP: case N_AP_SHIFT: case N_AP_UNSHIFT: case N_AP_SPLICE: case N_AP_REVERSE: case N_AP_FILL:
    case N_AP_COPYWITHIN:
        AP_ON_NOTHING();
        if (!plain_array(vm, thisv) || id == N_AP_COPYWITHIN) {
            jv o = js_to_object(vm, thisv);
            if (vm->throwing) return JV_HOLE;
            switch (id) {
            case N_AP_PUSH: return ga_push(vm, o, args, argc);
            case N_AP_POP: case N_AP_SHIFT: return ga_pop_shift(vm, o, id == N_AP_SHIFT);
            case N_AP_UNSHIFT: return ga_unshift(vm, o, args, argc);
            case N_AP_SPLICE: return ga_splice(vm, o, args, argc);
            case N_AP_REVERSE: return ga_reverse(vm, o);
            case N_AP_FILL: return ga_fill(vm, o, args, argc);
            default: return ga_copy_within(vm, o, args, argc);
            }
        }
        switch (id) {
        case N_AP_PUSH: {
            uint32_t a = jv_idx(thisv);
            for (uint32_t i = 0; i < argc; i++) js_array_push(vm, a, arg(vm, args, argc, i));
            return jv_num(vm->objs[a].b);
        }
        case N_AP_POP: case N_AP_SHIFT: break;
        case N_AP_UNSHIFT: {
            uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
            js_array_set_length(vm, a, n + argc);
            for (uint32_t i = n; i > 0; i--) vm->slots[vm->objs[a].a + i - 1 + argc] = vm->slots[vm->objs[a].a + i - 1];
            for (uint32_t i = 0; i < argc; i++) vm->slots[vm->objs[a].a + i] = arg(vm, args, argc, i);
            return jv_num(n + argc);
        }
        case N_AP_SPLICE: {
            uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
            double ds = 0, dc = 0;
            if (!ga_splice_args(vm, args, argc, n, &ds, &dc)) return JV_HOLE;
            /* the conversions may have run a valueOf that changed the array */
            if (!plain_array(vm, thisv) || vm->objs[a].b != n || !array_species_plain(vm, thisv)) return ga_splice_at(vm, thisv, n, ds, dc, args, argc);
            uint32_t start = (uint32_t)ds, del = (uint32_t)dc, ins = argc > 2 ? argc - 2 : 0;
            uint32_t removed = js_array_new(vm, 0);
            for (uint32_t i = 0; i < del; i++) js_array_push(vm, removed, vm->slots[vm->objs[a].a + start + i]);
            vector<jv> tail;
            for (uint32_t i = start + del; i < n; i++) push_back(tail, vm->slots[vm->objs[a].a + i]);
            js_array_set_length(vm, a, start + ins + (uint32_t)len(tail));
            for (uint32_t i = 0; i < ins; i++) vm->slots[vm->objs[a].a + start + i] = arg(vm, args, argc, i + 2);
            for (size_t i = 0; i < len(tail); i++) vm->slots[vm->objs[a].a + start + ins + i] = at(tail, i);
            return jv_obj(removed);
        }
        case N_AP_REVERSE: {
            uint32_t a = jv_idx(thisv), n = vm->objs[a].b, o = vm->objs[a].a;
            for (uint32_t i = 0; i < n / 2; i++) { jv t = vm->slots[o + i]; vm->slots[o + i] = vm->slots[o + n - 1 - i]; vm->slots[o + n - 1 - i] = t; }
            return thisv;
        }
        default: {
            uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
            double from = 0, to = 0;
            if (!ga_fill_args(vm, args, argc, n, &from, &to)) return JV_HOLE;
            if (!plain_array(vm, thisv) || vm->objs[a].b != n) return ga_fill_at(vm, thisv, from, to, a0);
            for (uint32_t i = (uint32_t)from; i < (uint32_t)to; i++) vm->slots[vm->objs[a].a + i] = a0;
            return thisv;
        }
        }
        {
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
        if (!n) return JV_UNDEF;
        jv r;
        if (id == N_AP_POP) { r = js_array_get(vm, a, n - 1); vm->objs[a].b = n - 1; }
        else {
            r = js_array_get(vm, a, 0);
            for (uint32_t i = 1; i < n; i++) vm->slots[vm->objs[a].a + i - 1] = vm->slots[vm->objs[a].a + i];
            vm->objs[a].b = n - 1;
        }
        return r;
        }
    case N_AP_SLICE: {
        AP_ON_NOTHING();
        double n = len_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        uint32_t from = rel_index(int_arg(vm, args, argc, 0, 0), (uint32_t)n);
        if (vm->throwing) return JV_HOLE;
        uint32_t to = rel_index(int_arg(vm, args, argc, 1, n), (uint32_t)n);
        if (vm->throwing) return JV_HOLE;
        jv sobj;
        if (array_species(vm, thisv, to > from ? to - from : 0, &sobj) < 0) return JV_HOLE;
        uint32_t out = js_array_new(vm, 0);
        /* a hole stays one (HasProperty), an element of attributes of its own read as Get reads it */
        for (uint32_t i = from; i < to && !vm->throwing; i++) {
            jv v = JV_HOLE;
            if (elem_take(vm, thisv, i, &v) < 0) break;
            js_array_push(vm, out, v);
        }
        if (vm->throwing) return JV_HOLE;
        return sobj != JV_UNDEF ? species_fill(vm, sobj, out, to > from ? to - from : 0) : jv_obj(out);
    }
    case N_AP_CONCAT: {
        AP_ON_NOTHING();
        jv sobj;
        if (array_species(vm, thisv, 0, &sobj) < 0) return JV_HOLE;
        uint32_t out = js_array_new(vm, 0);
        for (uint32_t k = 0; k <= argc && !vm->throwing; k++) {
            jv v = k == 0 ? thisv : arg(vm, args, argc, k - 1);
            if (is_array(vm, v)) {
                uint32_t n = vm->objs[jv_idx(v)].b;
                for (uint32_t i = 0; i < n && !vm->throwing; i++) {
                    jv x = i < vm->objs[jv_idx(v)].b ? vm->slots[vm->objs[jv_idx(v)].a + i] : JV_HOLE;
                    /* a hole stays one; an element of attributes of its own is read as Get reads it */
                    if (x == JV_HOLE && (vm->objs[jv_idx(v)].flags & JO_ELEMPROPS) && has_elem(vm, v, i)) x = elem_of(vm, v, i);
                    js_array_push(vm, out, x);
                }
            } else js_array_push(vm, out, v);
        }
        if (vm->throwing) return JV_HOLE;
        return sobj != JV_UNDEF ? species_fill(vm, sobj, out, vm->objs[out].b) : jv_obj(out);
    }
    case N_AP_JOIN: case N_AP_TOSTRING: {
        AP_ON_NOTHING();
        uint32_t sep = A_EMPTY;
        if (id == N_AP_TOSTRING || a0 == JV_UNDEF) sep = js_str_ascii(vm, ",");
        else { sep = js_to_string(vm, a0); if (!sep) return JV_HOLE; }
        uint32_t n = (uint32_t)len_of(vm, thisv);
        vector<uint16_t> out;
        js_str_flatten(vm, sep);
        for (uint32_t i = 0; i < n && !vm->throwing; i++) {
            if (i) for (uint32_t k = 0; k < vm->strs[sep].len; k++) push_back(out, vm->units[vm->strs[sep].off + k]);
            jv v = elem_of(vm, thisv, i);
            if (jv_is_nullish(v)) continue;
            uint32_t s = js_to_string(vm, v);
            if (!s) return JV_HOLE;
            js_str_flatten(vm, s);
            uint32_t o = vm->strs[s].off, sl = vm->strs[s].len;
            for (uint32_t k = 0; k < sl; k++) push_back(out, vm->units[o + k]);
        }
        if (vm->throwing) return JV_HOLE;
        return jv_str(js_str_new(vm, data(out), len(out)));
    }
    case N_AP_INDEXOF: case N_AP_LASTINDEXOF: case N_AP_INCLUDES: {
        AP_ON_NOTHING();
        uint32_t n = (uint32_t)len_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (id == N_AP_LASTINDEXOF) {
            int64_t from = argc > 1 ? (int64_t)int_arg(vm, args, argc, 1, 0) : (int64_t)n - 1;
            if (vm->throwing) return JV_HOLE;
            if (from < 0) from += n;
            if (from >= (int64_t)n) from = (int64_t)n - 1;
            for (int64_t i = from; i >= 0; i--) {
                int has = elem_present(vm, thisv, (uint32_t)i);
                if (vm->throwing) return JV_HOLE;
                if (!has) continue;
                jv v = elem_of(vm, thisv, (uint32_t)i);
                if (vm->throwing) return JV_HOLE;
                if (js_strict_eq(vm, v, a0)) return jv_num((double)i);
            }
            return jv_num(-1);
        }
        uint32_t from = rel_index(int_arg(vm, args, argc, 1, 0), n);
        if (vm->throwing) return JV_HOLE;
        if (is_array(vm, thisv) && !(vm->objs[jv_idx(thisv)].flags & JO_ELEMPROPS)) {
            uint32_t a = jv_idx(thisv);
            for (uint32_t i = from; i < n && i < vm->objs[a].b; i++) {
                jv v = vm->slots[vm->objs[a].a + i];
                if (v == JV_HOLE) {
                    if (id != N_AP_INCLUDES) continue;
                    v = JV_UNDEF;
                }
                if (id == N_AP_INCLUDES ? js_same_value_zero(vm, v, a0) : js_strict_eq(vm, v, a0))
                    return id == N_AP_INCLUDES ? JV_TRUE : jv_num(i);
            }
        } else {
            /* indexOf skips what is not there (HasProperty); includes reads it as undefined */
            for (uint32_t i = from; i < n && !vm->throwing; i++) {
                if (id != N_AP_INCLUDES) {
                    int has = elem_present(vm, thisv, i);
                    if (vm->throwing) return JV_HOLE;
                    if (!has) continue;
                }
                jv v = elem_of(vm, thisv, i);
                if (vm->throwing) return JV_HOLE;
                if (id == N_AP_INCLUDES ? js_same_value_zero(vm, v, a0) : js_strict_eq(vm, v, a0)) return id == N_AP_INCLUDES ? JV_TRUE : jv_num(i);
            }
            if (vm->throwing) return JV_HOLE;
        }
        return id == N_AP_INCLUDES ? JV_FALSE : jv_num(-1);
    }
    case N_AP_FIND: case N_AP_FINDINDEX: case N_AP_FINDLAST: case N_AP_FINDLASTINDEX: case N_AP_FILTER: case N_AP_MAP:
    case N_AP_FOREACH: case N_AP_SOME: case N_AP_EVERY: case N_AP_FLATMAP: {
        AP_ON_NOTHING();
        if (!js_is_callable(vm, a0)) return js_not_function(vm, a0);
        uint32_t n = (uint32_t)len_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        jv sobj = JV_UNDEF;
        if ((id == N_AP_FILTER || id == N_AP_MAP || id == N_AP_FLATMAP) && array_species(vm, thisv, id == N_AP_MAP ? n : 0, &sobj) < 0) return JV_HOLE;
        uint32_t out = id == N_AP_FILTER || id == N_AP_MAP || id == N_AP_FLATMAP ? js_array_new(vm, 0) : 0;
        int last = id == N_AP_FINDLAST || id == N_AP_FINDLASTINDEX;
        /* the find family visits every index; the others skip what is not there (HasProperty) */
        int every_index = last || id == N_AP_FIND || id == N_AP_FINDINDEX;
        for (uint32_t q = 0; q < n && !vm->throwing; q++) {
            uint32_t i = last ? n - 1 - q : q;
            jv v = JV_UNDEF;
            if (!every_index) {
                int has = elem_take(vm, thisv, i, &v);
                if (has < 0) return JV_HOLE;
                if (!has) {
                    if (id == N_AP_MAP) js_array_push(vm, out, JV_HOLE);
                    continue;
                }
            } else {
                v = elem_of(vm, thisv, i);
                if (vm->throwing) return JV_HOLE;
            }
            jv r = callback(vm, a0, a1, v, jv_num(i), thisv);
            if (vm->throwing) return JV_HOLE;
            int t = id == N_AP_MAP || id == N_AP_FOREACH || id == N_AP_FLATMAP ? 0 : js_truthy(vm, r);
            if (id == N_AP_MAP) js_array_push(vm, out, r);
            else if (id == N_AP_FLATMAP) { if (is_array_spec(vm, r)) flat_into(vm, out, r, 0); else js_array_push(vm, out, r); }
            else if (id == N_AP_FILTER) { if (t) js_array_push(vm, out, v); }
            else if ((id == N_AP_FIND || id == N_AP_FINDLAST) && t) return v;
            else if ((id == N_AP_FINDINDEX || id == N_AP_FINDLASTINDEX) && t) return jv_num(i);
            else if (id == N_AP_SOME && t) return JV_TRUE;
            else if (id == N_AP_EVERY && !t) return JV_FALSE;
        }
        if (out) return sobj != JV_UNDEF ? species_fill(vm, sobj, out, -1) : jv_obj(out);
        if (id == N_AP_FINDINDEX || id == N_AP_FINDLASTINDEX) return jv_num(-1);
        if (id == N_AP_SOME) return JV_FALSE;
        if (id == N_AP_EVERY) return JV_TRUE;
        return JV_UNDEF;
    }
    case N_AP_REDUCE: case N_AP_REDUCERIGHT: {
        AP_ON_NOTHING();
        if (!js_is_callable(vm, a0)) return js_not_function(vm, a0);
        uint32_t n = (uint32_t)len_of(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        int right = id == N_AP_REDUCERIGHT;
        uint32_t q = 0;
        jv acc = a1;
        if (argc < 2) {
            /* the first element there (HasProperty) the accumulator */
            int found = 0;
            for (; q < n && !found; q++) {
                found = elem_take(vm, thisv, right ? n - 1 - q : q, &acc);
                if (found < 0) return JV_HOLE;
            }
            if (!found) return js_throw_msg(vm, vm->type_error_proto, "Reduce of empty array with no initial value");
        }
        for (; q < n && !vm->throwing; q++) {
            uint32_t i = right ? n - 1 - q : q;
            jv v = JV_UNDEF;
            int has = elem_take(vm, thisv, i, &v);
            if (has < 0) return JV_HOLE;
            if (!has) continue;
            jv args4[4] = { acc, v, jv_num(i), thisv };
            acc = js_call(vm, a0, JV_UNDEF, args4, 4);
        }
        return vm->throwing ? JV_HOLE : acc;
    }
    case N_AP_SORT: {
        if (a0 != JV_UNDEF && !js_is_callable(vm, a0)) return value_error(vm, "The comparison function must be either a function or undefined: ", a0, "");
        AP_ON_NOTHING();                      /* after the comparator's test, as the standard has it */
        if (!plain_array(vm, thisv)) {
            jv o = js_to_object(vm, thisv);
            if (vm->throwing) return JV_HOLE;
            return ga_sort(vm, o, a0);
        }
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
        vector<jv> v;
        vector<jv> tmp;
        uint32_t holes = 0;
        for (uint32_t i = 0; i < n; i++) {
            jv x = vm->slots[vm->objs[a].a + i];
            if (x == JV_HOLE) holes++;
            else push_back(v, x);
        }
        resize(tmp, len(v));
        int err = 0;
        merge_sort(vm, &v, &tmp, 0, len(v), a0, &err);
        if (err || vm->throwing) return JV_HOLE;
        /* a comparator that froze the array, or changed its length: the standard's writes */
        if (!plain_array(vm, thisv) || vm->objs[a].b != n) return ga_sort_store(vm, thisv, &v, n);
        for (size_t i = 0; i < len(v); i++) vm->slots[vm->objs[a].a + i] = at(v, i);
        for (uint32_t i = 0; i < holes; i++) vm->slots[vm->objs[a].a + len(v) + i] = JV_HOLE;
        return thisv;
    }
    case N_AP_KEYS: case N_AP_VALUES: case N_AP_ENTRIES: {
        /* an Array Iterator over the object itself, its length and elements read at each step */
        AP_ON_NOTHING();
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        return iter_of_coll(vm, jv_idx(o), id == N_AP_KEYS ? JI_ARRAY_KEYS : id == N_AP_VALUES ? JI_ARRAY : JI_ARRAY_ENTRIES);
    }
    case N_AP_FLAT: {
        AP_ON_NOTHING();
        double depth = argc ? int_arg(vm, args, argc, 0, 1) : 1;
        if (vm->throwing) return JV_HOLE;
        jv sobj;
        if (array_species(vm, thisv, 0, &sobj) < 0) return JV_HOLE;
        uint32_t out = js_array_new(vm, 0);
        flat_into(vm, out, thisv, depth);
        if (vm->throwing) return JV_HOLE;
        return sobj != JV_UNDEF ? species_fill(vm, sobj, out, -1) : jv_obj(out);
    }
    case N_AP_AT: {
        AP_ON_NOTHING();
        uint32_t n = (uint32_t)len_of(vm, thisv);
        double i = int_arg(vm, args, argc, 0, 0);
        if (i < 0) i += n;
        if (i < 0 || i >= n) return JV_UNDEF;
        return elem_of(vm, thisv, (uint32_t)i);
    }
    /* ---- String ---- */
    case N_STR_FROMCHARCODE: {
        vector<uint16_t> u;
        for (uint32_t i = 0; i < argc; i++) push_back(u, (uint16_t)(js_to_int32(js_to_number(vm, arg(vm, args, argc, i))) & 0xFFFF));
        /* one ASCII unit, an atom (not U+0000, which a C string can't hold: its string is "\0", not "") */
        if (len(u) == 1 && at(u, 0) < 128 && at(u, 0) != 0) {
            char b[2] = { (char)at(u, 0), 0 };
            return jv_str(js_atom_ascii(vm, b));
        }
        return jv_str(js_str_new(vm, data(u), len(u)));
    }
    case N_STR_RAW: {
        /* String.raw`...`: the raw strings, the substitutions between them */
        jv raw = jv_is_obj(a0) ? js_get(vm, a0, js_atom_ascii(vm, "raw")) : JV_UNDEF;
        if (vm->throwing) return JV_HOLE;
        if (!jv_is_obj(raw)) return js_throw_msg(vm, vm->type_error_proto, "String.raw needs a template's strings");
        uint32_t n = (uint32_t)len_of(vm, raw);
        uint32_t r = A_EMPTY;
        for (uint32_t i = 0; i < n && !vm->throwing; i++) {
            uint32_t part = js_to_string(vm, elem_of(vm, raw, i));
            if (!part) return JV_HOLE;
            r = js_str_concat(vm, r, part);
            if (i + 1 < n && i + 1 < argc) {
                uint32_t sub = js_to_string(vm, arg(vm, args, argc, i + 1));
                if (!sub) return JV_HOLE;
                r = js_str_concat(vm, r, sub);
            }
        }
        return vm->throwing ? JV_HOLE : jv_str(r);
    }
    case N_STR_FROMCODEPOINT: {
        vector<uint16_t> u;
        for (uint32_t i = 0; i < argc; i++) {
            double d = js_to_number(vm, arg(vm, args, argc, i));
            if (d != d || d < 0 || d > 0x10FFFF || d != trunc(d)) return js_throw_msg(vm, vm->range_error_proto, "Invalid code point");
            uint32_t cp = (uint32_t)d;
            if (cp >= 0x10000) { push_back(u, (uint16_t)(0xD800 + ((cp - 0x10000) >> 10))); push_back(u, (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF))); }
            else push_back(u, (uint16_t)cp);
        }
        return jv_str(js_str_new(vm, data(u), len(u)));
    }
    case N_SP_TOSTRING: case N_SP_VALUEOF: {
        if (jv_is_str(thisv)) return thisv;
        if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_STRING) return vm->objs[jv_idx(thisv)].x;
        return js_throw_msg(vm, vm->type_error_proto, id == N_SP_VALUEOF ? "String.prototype.valueOf requires that 'this' be a String" :
                            "String.prototype.toString requires that 'this' be a String");
    }
    default: break;
    }
    /* the rest of String.prototype: this a string */
    /* RegExp, and String's methods over regular expressions (src/js/regexp.c); Proxy,
       Reflect and the weak collections (src/js/proxy.c) */
    if (id >= N_RE_FIRST && id < N_RE_FIRST + JRE_N) return js_re_native(vm, id - N_RE_FIRST, callee, thisv, args, argc, newt);
    if (id >= N_PX_FIRST && id < N_PX_FIRST + JPX_N) return js_proxy_native(vm, id - N_PX_FIRST, callee, thisv, args, argc, newt);
    if (id >= N_TA_FIRST && id < N_TA_FIRST + JTN_N) return js_typed_native(vm, id - N_TA_FIRST, callee, thisv, args, argc, newt);
    if (id >= N_BG_FIRST && id < N_BG_FIRST + JBG_N) return js_big_native(vm, id - N_BG_FIRST, callee, thisv, args, argc, newt);
    if (id >= N_MD_FIRST && id < N_MD_FIRST + JMD_N) return js_module_native(vm, id - N_MD_FIRST, callee, thisv, args, argc, newt);
    if (id >= N_IN_FIRST && id < N_IN_FIRST + JIN_N) return js_intl_native(vm, id - N_IN_FIRST, callee, thisv, args, argc, newt);
    if (id == N_SP_MATCH) return js_re_native(vm, JRE_SP_MATCH, callee, thisv, args, argc, newt);
    if (id == N_SP_SEARCH) return js_re_native(vm, JRE_SP_SEARCH, callee, thisv, args, argc, newt);
    if (id == N_SP_MATCHALL) return js_re_native(vm, JRE_SP_MATCHALL, callee, thisv, args, argc, newt);
    /* split, replace and replaceAll: the pattern's own Symbol.split (Symbol.replace) when it has one, a RegExp's or any
       object's (src/js/regexp.c), before this is read as a string */
    if ((id == N_SP_SPLIT || id == N_SP_REPLACE || id == N_SP_REPLACEALL) && !jv_is_nullish(a0)) {
        if (jv_is_nullish(thisv) || thisv == JV_HOLE) return called_on_nothing(vm, "String.prototype.", callee);
        jv out = JV_UNDEF;
        int r = js_re_string_method(vm, id == N_SP_SPLIT ? JSYM_SPLIT : JSYM_REPLACE, id == N_SP_REPLACEALL, thisv, a0, a1, &out);
        if (r < 0) return JV_HOLE;
        if (r > 0) return out;
    }
    if (id >= N_SP_CHARAT && id <= N_SP_SEARCH) {
        if (jv_is_nullish(thisv) || thisv == JV_HOLE) return called_on_nothing(vm, "String.prototype.", callee);
        uint32_t s = this_str(vm, thisv);
        if (!s) return JV_HOLE;
        uint32_t n = vm->strs[s].len;
        switch (id) {
        case N_SP_CHARAT: case N_SP_CHARCODEAT: case N_SP_CODEPOINTAT: case N_SP_AT: {
            double i = int_arg(vm, args, argc, 0, 0);
            if (id == N_SP_AT && i < 0) i += n;
            if (i < 0 || i >= n) return id == N_SP_CHARAT ? jv_str(A_EMPTY) : id == N_SP_AT ? JV_UNDEF : jv_num(NAN);
            uint32_t k = (uint32_t)i;
            if (id == N_SP_CHARAT || id == N_SP_AT) return jv_str(js_str_sub(vm, s, k, k + 1));
            uint16_t c = js_str_at(vm, s, k);
            if (id == N_SP_CODEPOINTAT && c >= 0xD800 && c <= 0xDBFF && k + 1 < n) {
                uint16_t d = js_str_at(vm, s, k + 1);
                if (d >= 0xDC00 && d <= 0xDFFF) return jv_num(0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00));
            }
            return jv_num(c);
        }
        case N_SP_INDEXOF: case N_SP_INCLUDES: {
            if (id == N_SP_INCLUDES && !str_not_regexp(vm, a0, "includes")) return JV_HOLE;
            uint32_t t = js_to_string(vm, a0);
            if (!t) return JV_HOLE;
            uint32_t from = rel_index(int_arg(vm, args, argc, 1, 0), n);
            if (from > n) from = n;
            int64_t r = str_index_of(vm, s, t, from);
            return id == N_SP_INCLUDES ? jv_bool(r >= 0) : jv_num((double)r);
        }
        case N_SP_LASTINDEXOF: {
            uint32_t t = js_to_string(vm, a0);
            if (!t) return JV_HOLE;
            double f = argc > 1 ? js_to_number(vm, a1) : NAN;
            uint32_t from = f != f ? n : (uint32_t)rel_index(trunc(f), n);
            return jv_num((double)str_last_index_of(vm, s, t, from));
        }
        case N_SP_STARTSWITH: case N_SP_ENDSWITH: {
            if (!str_not_regexp(vm, a0, id == N_SP_STARTSWITH ? "startsWith" : "endsWith")) return JV_HOLE;
            uint32_t t = js_to_string(vm, a0);
            if (!t) return JV_HOLE;
            uint32_t m = vm->strs[t].len;
            if (id == N_SP_STARTSWITH) {
                uint32_t pos = rel_index(int_arg(vm, args, argc, 1, 0), n);
                if (pos + m > n) return JV_FALSE;
                for (uint32_t i = 0; i < m; i++) if (js_str_at(vm, s, pos + i) != js_str_at(vm, t, i)) return JV_FALSE;
                return JV_TRUE;
            }
            uint32_t end = rel_index(int_arg(vm, args, argc, 1, n), n);
            if (m > end) return JV_FALSE;
            for (uint32_t i = 0; i < m; i++) if (js_str_at(vm, s, end - m + i) != js_str_at(vm, t, i)) return JV_FALSE;
            return JV_TRUE;
        }
        case N_SP_SLICE: {
            uint32_t from = rel_index(int_arg(vm, args, argc, 0, 0), n), to = rel_index(int_arg(vm, args, argc, 1, n), n);
            return jv_str(js_str_sub(vm, s, from, to < from ? from : to));
        }
        case N_SP_SUBSTRING: {
            double f = int_arg(vm, args, argc, 0, 0), t = int_arg(vm, args, argc, 1, n);
            if (f < 0) f = 0;
            if (t < 0) t = 0;
            if (f > n) f = n;
            if (t > n) t = n;
            if (f > t) { double x = f; f = t; t = x; }
            return jv_str(js_str_sub(vm, s, (uint32_t)f, (uint32_t)t));
        }
        case N_SP_SUBSTR: {
            uint32_t from = rel_index(int_arg(vm, args, argc, 0, 0), n);
            double l = int_arg(vm, args, argc, 1, n - from);
            if (l < 0) l = 0;
            uint32_t to = from + (uint32_t)(l > n - from ? n - from : l);
            return jv_str(js_str_sub(vm, s, from, to));
        }
        case N_SP_TOUPPER: return jv_str(str_case(vm, s, 1));
        case N_SP_TOLOWER: return jv_str(str_case(vm, s, 0));
        case N_SP_TRIM: return jv_str(str_trim(vm, s, 1, 1));
        case N_SP_TRIMSTART: return jv_str(str_trim(vm, s, 1, 0));
        case N_SP_TRIMEND: return jv_str(str_trim(vm, s, 0, 1));
        case N_SP_PADSTART: case N_SP_PADEND: {
            double target = int_arg(vm, args, argc, 0, 0);
            if (target <= n) return jv_str(s);
            uint32_t fill = argc > 1 && a1 != JV_UNDEF ? js_to_string(vm, a1) : js_str_ascii(vm, " ");
            if (!fill) return JV_HOLE;
            uint32_t fl = vm->strs[fill].len;
            if (!fl) return jv_str(s);
            vector<uint16_t> u;
            uint32_t need = (uint32_t)target - n;
            js_str_flatten(vm, fill);
            js_str_flatten(vm, s);
            if (id == N_SP_PADEND) for (uint32_t i = 0; i < n; i++) push_back(u, vm->units[vm->strs[s].off + i]);
            for (uint32_t i = 0; i < need; i++) push_back(u, vm->units[vm->strs[fill].off + i % fl]);
            if (id == N_SP_PADSTART) for (uint32_t i = 0; i < n; i++) push_back(u, vm->units[vm->strs[s].off + i]);
            return jv_str(js_str_new(vm, data(u), len(u)));
        }
        case N_SP_REPEAT: {
            double c = int_arg(vm, args, argc, 0, 0);
            if (c < 0 || c == INFINITY || c * n > 1e9) return js_throw_msg(vm, vm->range_error_proto, "Invalid count value");
            return jv_str(str_repeat(vm, s, (uint32_t)c));
        }
        case N_SP_SPLIT: {
            /* the limit ToUint32 (-1 is every piece), then the separator's text, then a limit of 0 */
            uint32_t out = js_array_new(vm, 0);
            double lim = 4294967295.0;
            if (a1 != JV_UNDEF) {
                double d = js_to_number(vm, a1);
                if (vm->throwing) return JV_HOLE;
                lim = (double)(uint32_t)js_to_int32(d);
            }
            uint32_t sep = a0 == JV_UNDEF ? 0 : js_to_string(vm, a0);
            if (a0 != JV_UNDEF && !sep) return JV_HOLE;
            if (lim == 0) return jv_obj(out);
            if (a0 == JV_UNDEF) { js_array_push(vm, out, jv_str(s)); return jv_obj(out); }
            uint32_t m = vm->strs[sep].len;
            if (m == 0) {
                for (uint32_t i = 0; i < n && vm->objs[out].b < lim; i++) js_array_push(vm, out, jv_str(js_str_sub(vm, s, i, i + 1)));
                return jv_obj(out);
            }
            uint32_t from = 0;
            while (vm->objs[out].b < lim) {
                int64_t at_i = str_index_of(vm, s, sep, from);
                if (at_i < 0) { js_array_push(vm, out, jv_str(js_str_sub(vm, s, from, n))); break; }
                js_array_push(vm, out, jv_str(js_str_sub(vm, s, from, (uint32_t)at_i)));
                from = (uint32_t)at_i + m;
            }
            return jv_obj(out);
        }
        case N_SP_REPLACE: return str_replace(vm, s, a0, a1, 0);
        case N_SP_REPLACEALL: return str_replace(vm, s, a0, a1, 1);
        case N_SP_CONCAT: {
            uint32_t r = s;
            for (uint32_t i = 0; i < argc; i++) {
                uint32_t t = js_to_string(vm, arg(vm, args, argc, i));
                if (!t) return JV_HOLE;
                r = js_str_concat(vm, r, t);
            }
            return jv_str(r);
        }
        case N_SP_LOCALECOMPARE: {
            uint32_t t = js_to_string(vm, a0);
            if (!t) return JV_HOLE;
            return jv_num(js_str_cmp(vm, s, t));
        }
        case N_SP_NORMALIZE: return js_str_normalize(vm, s, a0);
        default: break;
        }
    }
    switch (id) {
    /* ---- Number ---- */
    case N_NUM_ISINTEGER: case N_NUM_ISSAFEINTEGER: {
        if (!jv_is_num(a0)) return JV_FALSE;
        double d = jv_dbl(a0);
        if (d != d || d == INFINITY || d == -INFINITY || d != trunc(d)) return JV_FALSE;
        return jv_bool(id == N_NUM_ISINTEGER || fabs(d) <= 9007199254740991.0);
    }
    case N_NUM_ISFINITE: { if (!jv_is_num(a0)) return JV_FALSE; double d = jv_dbl(a0); return jv_bool(d == d && d != INFINITY && d != -INFINITY); }
    case N_NUM_ISNAN: { if (!jv_is_num(a0)) return JV_FALSE; double d = jv_dbl(a0); return jv_bool(d != d); }
    case N_NP_VALUEOF: case N_NP_TOSTRING: case N_NP_TOFIXED: case N_NP_TOPRECISION: case N_NP_TOLOCALE: {
        jv tv = thisv;
        if (jv_is_obj(tv) && vm->objs[jv_idx(tv)].kind == JK_NUMBER) tv = vm->objs[jv_idx(tv)].x;
        if (!jv_is_num(tv))
            return js_throw_msg(vm, vm->type_error_proto, id == N_NP_VALUEOF ? "Number.prototype.valueOf requires that 'this' be a Number" :
                                id == N_NP_TOSTRING ? "Number.prototype.toString requires that 'this' be a Number" :
                                id == N_NP_TOFIXED ? "Number.prototype.toFixed requires that 'this' be a Number" :
                                id == N_NP_TOPRECISION ? "Number.prototype.toPrecision requires that 'this' be a Number" :
                                "Number.prototype.toLocaleString requires that 'this' be a Number");
        double d = jv_dbl(tv);
        if (id == N_NP_VALUEOF) return tv;
        if (id == N_NP_TOSTRING) {
            double r = a0 == JV_UNDEF ? 10 : js_to_number(vm, a0);
            if (r != 10 && (r < 2 || r > 36 || r != trunc(r))) return js_throw_msg(vm, vm->range_error_proto, "toString() radix must be between 2 and 36");
            if (r == 10 || d != d || d == INFINITY || d == -INFINITY) return jv_str(js_num_to_str(vm, d));
            int radix = (int)r;
            char b[80];
            int k = 0;
            int neg = d < 0;
            double x = trunc(fabs(d));
            double frac = fabs(d) - x;
            char t[80];
            int tk = 0;
            if (x == 0) t[tk++] = '0';
            while (x > 0 && tk < 70) { int dg = (int)fmod(x, radix); t[tk++] = (char)(dg < 10 ? '0' + dg : 'a' + dg - 10); x = floor(x / radix); }
            if (neg) b[k++] = '-';
            while (tk) b[k++] = t[--tk];
            if (frac > 0) {
                b[k++] = '.';
                for (int q = 0; q < 20 && frac > 0 && k < 78; q++) { frac *= radix; int dg = (int)frac; b[k++] = (char)(dg < 10 ? '0' + dg : 'a' + dg - 10); frac -= dg; }
            }
            b[k] = 0;
            return jv_str(js_str_ascii(vm, b));
        }
        if (id == N_NP_TOFIXED) {
            double f = int_arg(vm, args, argc, 0, 0);
            if (f < 0 || f > 100) return js_throw_msg(vm, vm->range_error_proto, "toFixed() digits argument must be between 0 and 100");
            if (d != d) return jv_str(js_str_ascii(vm, "NaN"));
            if (fabs(d) >= 1e21) return jv_str(js_num_to_str(vm, d));
            char b[160];
            snprintf(b, sizeof b, "%.*f", (int)f, d);
            if (b[0] == '-' && strspn(b + 1, "0.") == strlen(b + 1)) memmove(b, b + 1, strlen(b));
            return jv_str(js_str_ascii(vm, b));
        }
        if (id == N_NP_TOPRECISION) {
            if (a0 == JV_UNDEF) return jv_str(js_num_to_str(vm, d));
            double p = int_arg(vm, args, argc, 0, 1);
            if (p < 1 || p > 100) return js_throw_msg(vm, vm->range_error_proto, "toPrecision() argument must be between 1 and 100");
            char b[160];
            int e = d == 0 ? 0 : (int)floor(log10(fabs(d)));
            if (e < -6 || e >= (int)p) snprintf(b, sizeof b, "%.*e", (int)p - 1, d);
            else snprintf(b, sizeof b, "%.*f", (int)p - 1 - e < 0 ? 0 : (int)p - 1 - e, d);
            char *ep = strchr(b, 'e');
            if (ep && (ep[1] == '+' || ep[1] == '-') && ep[2] == '0') memmove(ep + 2, ep + 3, strlen(ep + 3) + 1);
            return jv_str(js_str_ascii(vm, b));
        }
        return jv_str(js_num_to_str(vm, d));
    }
    case N_BP_TOSTRING: case N_BP_VALUEOF: {
        jv tv = thisv;
        if (jv_is_obj(tv) && vm->objs[jv_idx(tv)].kind == JK_BOOLEAN) tv = vm->objs[jv_idx(tv)].x;
        if (!jv_is_bool(tv))
            return js_throw_msg(vm, vm->type_error_proto, id == N_BP_VALUEOF ? "Boolean.prototype.valueOf requires that 'this' be a Boolean" :
                                "Boolean.prototype.toString requires that 'this' be a Boolean");
        return id == N_BP_VALUEOF ? tv : jv_str(tv == JV_TRUE ? A_TRUE : A_FALSE);
    }
    /* ---- Math ---- */
    case N_M_ABS: return jv_num(fabs(js_to_number(vm, a0)));
    case N_M_FLOOR: return jv_num(floor(js_to_number(vm, a0)));
    case N_M_CEIL: return jv_num(ceil(js_to_number(vm, a0)));
    case N_M_ROUND: { double d = js_to_number(vm, a0); return jv_num(d != d || d == INFINITY || d == -INFINITY ? d : floor(d + 0.5)); }
    case N_M_TRUNC: return jv_num(trunc(js_to_number(vm, a0)));
    case N_M_SIGN: { double d = js_to_number(vm, a0); return jv_num(d != d ? NAN : d > 0 ? 1 : d < 0 ? -1 : d); }
    case N_M_MAX: case N_M_MIN: {
        double r = id == N_M_MAX ? -INFINITY : INFINITY;
        for (uint32_t i = 0; i < argc; i++) {
            double d = js_to_number(vm, arg(vm, args, argc, i));
            if (d != d) return jv_num(NAN);
            if (id == N_M_MAX ? d > r : d < r) r = d;
        }
        return jv_num(r);
    }
    case N_M_POW: { double x = js_to_number(vm, a0), y = js_to_number(vm, a1); return jv_num(y != y ? NAN : y == 0 ? 1 : pow(x, y)); }
    case N_M_SQRT: return jv_num(sqrt(js_to_number(vm, a0)));
    case N_M_CBRT: return jv_num(cbrt(js_to_number(vm, a0)));
    case N_M_EXP: return jv_num(exp(js_to_number(vm, a0)));
    case N_M_EXPM1: return jv_num(expm1(js_to_number(vm, a0)));
    case N_M_LOG: return jv_num(log(js_to_number(vm, a0)));
    case N_M_LOG2: return jv_num(log2(js_to_number(vm, a0)));
    case N_M_LOG10: return jv_num(log10(js_to_number(vm, a0)));
    case N_M_LOG1P: return jv_num(log1p(js_to_number(vm, a0)));
    case N_M_SIN: return jv_num(sin(js_to_number(vm, a0)));
    case N_M_COS: return jv_num(cos(js_to_number(vm, a0)));
    case N_M_TAN: return jv_num(tan(js_to_number(vm, a0)));
    case N_M_ASIN: return jv_num(asin(js_to_number(vm, a0)));
    case N_M_ACOS: return jv_num(acos(js_to_number(vm, a0)));
    case N_M_ATAN: return jv_num(atan(js_to_number(vm, a0)));
    case N_M_ATAN2: return jv_num(atan2(js_to_number(vm, a0), js_to_number(vm, a1)));
    case N_M_SINH: return jv_num(sinh(js_to_number(vm, a0)));
    case N_M_COSH: return jv_num(cosh(js_to_number(vm, a0)));
    case N_M_TANH: return jv_num(tanh(js_to_number(vm, a0)));
    case N_M_HYPOT: {
        double s = 0;
        for (uint32_t i = 0; i < argc; i++) { double d = js_to_number(vm, arg(vm, args, argc, i)); s += d * d; }
        return jv_num(sqrt(s));
    }
    case N_M_RANDOM: {
        /* xorshift: the page's runs repeat */
        vm->rng ^= vm->rng << 13;
        vm->rng ^= vm->rng >> 17;
        vm->rng ^= vm->rng << 5;
        return jv_num((double)vm->rng / 4294967296.0);
    }
    case N_M_FROUND: return jv_num((double)(float)js_to_number(vm, a0));
    case N_M_IMUL: return jv_num((double)(int32_t)((uint32_t)js_to_int32(js_to_number(vm, a0)) * (uint32_t)js_to_int32(js_to_number(vm, a1))));
    case N_M_CLZ32: {
        uint32_t x = (uint32_t)js_to_int32(js_to_number(vm, a0));
        int n = 0;
        if (!x) return jv_num(32);
        while (!(x & 0x80000000u)) { x <<= 1; n++; }
        return jv_num(n);
    }
    /* ---- JSON ---- */
    case N_JSON_STRINGIFY: {
        uint32_t gap = 0;
        jv sp = arg(vm, args, argc, 2);
        if (jv_is_num(sp)) { double g = jv_dbl(sp); gap = g < 0 ? 0 : g > 10 ? 10 : (uint32_t)g; }
        else if (jv_is_str(sp)) gap = vm->strs[jv_idx(sp)].len > 10 ? 10 : vm->strs[jv_idx(sp)].len;
        vector<uint16_t> o;
        vector<uint32_t> st;
        if (!json_write(vm, a0, &o, gap, 0, &st)) return vm->throwing ? JV_HOLE : JV_UNDEF;
        return jv_str(js_str_new(vm, data(o), len(o)));
    }
    case N_JSON_PARSE: {
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        js_str_flatten(vm, s);
        jp p = { vm->strs[s].off, vm->strs[s].len, 0, 0, JPE_NONE, 0 };
        /* with a reviver, the source and the parse's values kept on the value stack (a collection in the reviver keeps
           them), each value's record made for the walk */
        int revive = js_is_callable(vm, a1);
        uint32_t sp0 = vm->sp, keep = 0, root = JP_NOREC;
        vector<jp_rec> recs;
        vector<uint32_t> kids;
        if (revive) {
            if (!js_room(vm, sp0 + 4, vm->nframes)) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
            keep = js_array_new(vm, 0);
            vm->stack[vm->sp++] = jv_obj(keep);
            vm->stack[vm->sp++] = jv_str(s);
            vm->stack[vm->sp++] = a1;
        }
        jv v = jp_parse(vm, &p, revive ? &recs : 0, &kids, keep, &root);
        if (!p.err) {
            jp_ws(vm, &p);
            if (p.i != p.n) jp_fail(&p, JPE_AFTER, p.i);
        }
        if (p.err) { vm->sp = sp0; return js_throw(vm, jv_obj(js_error_new(vm, vm->syntax_error_proto, jp_message(vm, &p)))); }
        if (!revive) return v;
        uint32_t holder = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        vm->stack[vm->sp++] = jv_obj(holder);
        js_define(vm, holder, A_EMPTY, v, 0);
        jv r = jp_revive(vm, a1, s, &recs, &kids, jv_obj(holder), jv_str(A_EMPTY), root, 0);
        vm->sp = sp0;
        return r;
    }
    /* ---- console ---- */
    case N_CON_LOG: return console_out(vm, 0, args, argc);
    case N_CON_INFO: return console_out(vm, 1, args, argc);
    case N_CON_WARN: return console_out(vm, 2, args, argc);
    case N_CON_ERROR: return console_out(vm, 3, args, argc);
    case N_CON_DEBUG: return console_out(vm, 4, args, argc);
    case N_EP_TOSTRING: {
        /* Error.prototype.toString (ECMA-262 20.5.3.4): any object's name and message */
        if (!jv_is_obj(thisv)) return value_error(vm, "Method Error.prototype.toString called on incompatible receiver ", thisv, "");
        jv nv = js_get(vm, thisv, A_NAME);
        if (vm->throwing) return JV_HOLE;
        uint32_t nm = nv == JV_UNDEF ? js_str_ascii(vm, "Error") : js_to_string(vm, nv);
        if (!nm) return JV_HOLE;
        jv mv = js_get(vm, thisv, A_MESSAGE);
        if (vm->throwing) return JV_HOLE;
        uint32_t ms = mv == JV_UNDEF ? A_EMPTY : js_to_string(vm, mv);
        if (!ms) return JV_HOLE;
        if (!vm->strs[nm].len) return jv_str(ms);
        if (!vm->strs[ms].len) return jv_str(nm);
        return jv_str(js_str_concat(vm, js_str_concat(vm, nm, js_str_ascii(vm, ": ")), ms));
    }
    /* ---- time ---- */
    case N_DATE_NOW: return jv_num(floor(vm->now));
    case N_PERF_NOW: {
        if (vm->real_clock) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return jv_num(ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6 - vm->real_clock);
        }
        return jv_num(vm->now);
    }
    case N_DATE: {
        /* called: now as toString writes it; constructed: no argument now, one a date's time, a string parsed, or a
           number; more the local parts of a time */
        if (newt == JV_UNDEF || newt == JV_HOLE) return jv_str(date_text(vm, date_clip(floor(vm->now)), 0));
        double t;
        if (argc == 0) t = date_clip(floor(vm->now));
        else if (argc == 1) {
            if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_DATE) t = jv_dbl(vm->objs[jv_idx(a0)].x);
            else {
                jv pv = js_to_primitive(vm, a0, -1);
                if (vm->throwing) return JV_HOLE;
                if (jv_is_str(pv)) t = date_parse(vm, jv_idx(pv));
                else {
                    t = js_to_number(vm, pv);
                    if (vm->throwing) return JV_HOLE;
                }
            }
            t = date_clip(t);
        } else {
            double v[7] = { 0, 0, 1, 0, 0, 0, 0 };
            for (uint32_t i = 0; i < argc && i < 7; i++) {
                v[i] = js_to_number(vm, arg(vm, args, argc, i));
                if (vm->throwing) return JV_HOLE;
            }
            t = date_clip(date_utc(date_make_date(date_make_day(date_full_year(v[0]), v[1], v[2]), date_make_time(v[3], v[4], v[5], v[6]))));
        }
        uint32_t o = js_obj_new(vm, JK_DATE, vm->date_proto);
        vm->objs[o].x = jv_num(t);
        return jv_obj(o);
    }
    case N_DATE_UTC: {
        double v[7] = { NAN, 0, 1, 0, 0, 0, 0 };
        for (uint32_t i = 0; i < argc && i < 7; i++) {
            v[i] = js_to_number(vm, arg(vm, args, argc, i));
            if (vm->throwing) return JV_HOLE;
        }
        return jv_num(date_clip(date_make_date(date_make_day(date_full_year(v[0]), v[1], v[2]), date_make_time(v[3], v[4], v[5], v[6]))));
    }
    case N_DATE_PARSE: {
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        return jv_num(date_parse(vm, s));
    }
    case N_DP_GETTIME: {
        double t;
        if (!this_date(vm, thisv, 0, &t)) return JV_HOLE;
        return jv_num(t);
    }
    case N_DP_GETTZO: {
        double t;
        if (!this_date(vm, thisv, 0, &t)) return JV_HOLE;
        return jv_num(t != t ? NAN : 0);
    }
    case N_DP_GETFULLYEAR: case N_DP_GETMONTH: case N_DP_GETDATE: case N_DP_GETDAY: case N_DP_GETHOURS: case N_DP_GETMINUTES:
    case N_DP_GETSECONDS: case N_DP_GETMS: {
        double t;
        if (!this_date(vm, thisv, 0, &t)) return JV_HOLE;
        return jv_num(date_field(t, (int)(id - N_DP_GETFULLYEAR)));
    }
    case N_DP_GETYEAR: {
        double t;
        if (!this_date(vm, thisv, "getYear", &t)) return JV_HOLE;
        return jv_num(t != t ? NAN : date_field(t, 0) - 1900);
    }
    case N_DP_SETTIME: {
        double t;
        if (!this_date(vm, thisv, "setTime", &t)) return JV_HOLE;
        double v = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        v = date_clip(v);
        vm->objs[jv_idx(thisv)].x = jv_num(v);
        return jv_num(v);
    }
    case N_DP_SETMS: case N_DP_SETSEC: case N_DP_SETMIN: case N_DP_SETHOURS: case N_DP_SETDATE: case N_DP_SETMONTH: case N_DP_SETFULLYEAR:
    case N_DP_SETUTCMS: case N_DP_SETUTCSEC: case N_DP_SETUTCMIN: case N_DP_SETUTCHOURS: case N_DP_SETUTCDATE: case N_DP_SETUTCMONTH:
    case N_DP_SETUTCFULLYEAR: {
        /* a setter's fields (year, month, date, hours, minutes, seconds, ms) from its first: its arguments read, then
           the time's own (setFullYear's of +0 when the time is NaN; the others give NaN) */
        static const char *const sn[] = { "setMilliseconds", "setSeconds", "setMinutes", "setHours", "setDate", "setMonth", "setFullYear",
            "setUTCMilliseconds", "setUTCSeconds", "setUTCMinutes", "setUTCHours", "setUTCDate", "setUTCMonth", "setUTCFullYear" };
        static const int first[] = { 6, 5, 4, 3, 2, 1, 0 }, most[] = { 1, 2, 3, 4, 1, 2, 3 };
        uint32_t k = id - N_DP_SETMS, which = k % 7;
        double t;
        if (!this_date(vm, thisv, sn[k], &t)) return JV_HOLE;
        if (t != t && which == 6) t = 0;
        double f[7];
        for (int q = 0; q < 7; q++) f[q] = date_field(t, q < 3 ? q : q + 1);
        /* V8 reads the first argument always, the others only of a valid date */
        uint32_t n = argc < (uint32_t)most[which] ? argc : (uint32_t)most[which];
        if (n == 0) n = 1;
        for (uint32_t q = 0; q < n; q++) {
            if (q == 1 && t != t) break;
            f[first[which] + (int)q] = js_to_number(vm, arg(vm, args, argc, q));
            if (vm->throwing) return JV_HOLE;
        }
        if (t != t) return jv_num(NAN);
        double v = date_clip(date_utc(date_make_date(date_make_day(f[0], f[1], f[2]), date_make_time(f[3], f[4], f[5], f[6]))));
        vm->objs[jv_idx(thisv)].x = jv_num(v);
        return jv_num(v);
    }
    case N_DP_SETYEAR: {
        double t;
        if (!this_date(vm, thisv, "setYear", &t)) return JV_HOLE;
        double y = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        if (t != t) t = 0;
        double v = NAN;
        if (y == y)
            v = date_clip(date_utc(date_make_date(date_make_day(date_full_year(y), date_field(t, 1), date_field(t, 2)),
                                                  date_make_time(date_field(t, 4), date_field(t, 5), date_field(t, 6), date_field(t, 7)))));
        vm->objs[jv_idx(thisv)].x = jv_num(v);
        return jv_num(v);
    }
    case N_DP_TOSTRING: case N_DP_TODATESTRING: case N_DP_TOTIMESTRING: case N_DP_TOUTCSTRING: case N_DP_TOISO: {
        static const char *const tn[] = { "toString", "toDateString", "toTimeString", "toUTCString", "toISOString" };
        int which = id == N_DP_TOSTRING ? 0 : id == N_DP_TODATESTRING ? 1 : id == N_DP_TOTIMESTRING ? 2 : id == N_DP_TOUTCSTRING ? 3 : 4;
        double t;
        if (!this_date(vm, thisv, tn[which], &t)) return JV_HOLE;
        if (which == 4 && t != t) return js_throw_msg(vm, vm->range_error_proto, "Invalid time value");
        return jv_str(date_text(vm, t, which));
    }
    case N_DP_TOJSON: {
        /* generic: a number that is not finite is null, else the object's own toISOString */
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        jv tv = js_to_primitive(vm, o, 0);
        if (vm->throwing) return JV_HOLE;
        if (jv_is_num(tv) && !isfinite(jv_dbl(tv))) return JV_NULL;
        jv f = js_get(vm, o, js_atom_ascii(vm, "toISOString"));
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, f)) return js_throw_msg(vm, vm->type_error_proto, "toISOString is not a function");
        return js_call(vm, f, o, 0, 0);
    }
    case N_DP_TOPRIM: {
        /* Date.prototype[Symbol.toPrimitive]: "default" a string's, as "string" */
        if (!jv_is_obj(thisv)) return value_error(vm, "Method Date.prototype [ @@toPrimitive ] called on incompatible receiver ", thisv, "");
        int hs = -1;
        if (jv_is_str(a0)) {
            uint32_t h = js_atom(vm, jv_idx(a0));
            if (h == A_STRING || h == A_DEFAULT) hs = 1;
            else if (h == A_NUMBER) hs = 0;
        }
        if (hs < 0) return value_error(vm, "Invalid hint: ", a0, "");
        return js_ordinary_to_primitive(vm, thisv, hs);
    }
    case N_SET_TIMEOUT: return add_timer(vm, args, argc, 0);
    case N_SET_INTERVAL: return add_timer(vm, args, argc, 1);
    case N_CLEAR_TIMEOUT: {
        double d = js_to_number(vm, a0);
        for (size_t i = 0; i < len(vm->timers); i++) if (vm->timers[i].id == (uint32_t)d) vm->timers[i].live = 0;
        return JV_UNDEF;
    }
    case N_QUEUE_MICROTASK: {
        /* a microtask, run at the next checkpoint in order with the promises' */
        if (!js_is_callable(vm, a0)) return js_not_function(vm, a0);
        js_job_push(vm, JOB_CALL, 0, a0, JV_UNDEF, JV_UNDEF);
        return JV_UNDEF;
    }
    /* ---- Map and Set ---- */
    /* ---- Iterator ---- */
    case N_ITERATOR: {
        if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Constructor Iterator requires 'new'");
        if (newt == jv_obj(vm->iterator_ctor)) return js_throw_msg(vm, vm->type_error_proto, "Abstract class Iterator not directly constructable");
        jv pv = js_get(vm, newt, A_PROTOTYPE);
        if (vm->throwing) return JV_HOLE;
        return jv_obj(js_obj_new(vm, JK_PLAIN, jv_is_obj(pv) ? jv_idx(pv) : vm->iterator_proto));
    }
    case N_ITER_FROM: {
        /* GetIteratorFlattenable (strings iterated), then the iterator itself when it is one, else wrapped */
        jv it;
        if (jv_is_str(a0)) it = iter_of_coll(vm, jv_idx(a0), JI_STRING);
        else {
            if (!jv_is_obj(a0)) return js_throw_msg(vm, vm->type_error_proto, "Iterator.from called on non-object");
            jv m = js_get(vm, a0, JS_KEY_ITERATOR);
            if (vm->throwing) return JV_HOLE;
            it = a0;
            if (!jv_is_nullish(m)) {
                if (!js_is_callable(vm, m)) return js_not_function(vm, m);
                it = js_call(vm, m, a0, 0, 0);
                if (vm->throwing) return JV_HOLE;
                if (!jv_is_obj(it)) return js_throw_msg(vm, vm->type_error_proto, "Result of the Symbol.iterator method is not an object");
            }
        }
        jv next = js_get(vm, it, A_NEXT);
        if (vm->throwing) return JV_HOLE;
        for (uint32_t x = vm->objs[jv_idx(it)].proto, d = 0; x && d < 10000; x = vm->objs[x].proto, d++)
            if (x == vm->iterator_proto) return it;
        /* the wrapper: an iterator of the protocol over it, its next kept */
        uint32_t w = js_obj_new(vm, JK_ITER, vm->wrap_proto);
        vm->objs[w].a = jv_idx(it);
        vm->objs[w].c = JI_PROTOCOL;
        vm->objs[w].x = next;
        return jv_obj(w);
    }
    case N_WRAP_NEXT: case N_WRAP_RETURN: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ITER || vm->objs[jv_idx(thisv)].c != JI_PROTOCOL)
            return value_error(vm, id == N_WRAP_NEXT ? "Method WrapForValidIteratorPrototype.next called on incompatible receiver " :
                                   "Method WrapForValidIteratorPrototype.return called on incompatible receiver ", thisv, "");
        jv it = jv_obj(vm->objs[jv_idx(thisv)].a);
        if (id == N_WRAP_NEXT) return js_call(vm, vm->objs[jv_idx(thisv)].x, it, 0, 0);
        jv rf = js_get(vm, it, A_RETURN);
        if (vm->throwing) return JV_HOLE;
        if (jv_is_nullish(rf)) return iter_result(vm, JV_UNDEF, 1);
        return js_call(vm, rf, it, 0, 0);
    }
    case N_ITP_MAP: case N_ITP_FILTER: case N_ITP_TAKE: case N_ITP_DROP: case N_ITP_FLATMAP: return iter_helper(vm, id, thisv, a0);
    case N_ITP_REDUCE: case N_ITP_TOARRAY: case N_ITP_FOREACH: case N_ITP_SOME: case N_ITP_EVERY: case N_ITP_FIND:
        return iter_consume(vm, id, thisv, args, argc);
    case N_ITP_CTOR_GET: return jv_obj(vm->iterator_ctor);
    case N_ITP_CTOR_SET: return iter_proto_setter(vm, thisv, A_CONSTRUCTOR, a0, "constructor");
    case N_ITP_TAG_GET: return jv_str(js_str_ascii(vm, "Iterator"));
    case N_ITP_TAG_SET: return iter_proto_setter(vm, thisv, JS_SYM_KEY | JSYM_TO_STRING_TAG, a0, "Symbol(Symbol.toStringTag)");
    case N_HELPER_NEXT: case N_HELPER_RETURN: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ITER || vm->objs[jv_idx(thisv)].c != JI_HELPER)
            return value_error(vm, id == N_HELPER_NEXT ? "Method Iterator Helper.prototype.next called on incompatible receiver " :
                                   "Method Iterator Helper.prototype.return called on incompatible receiver ", thisv, "");
        uint32_t h = jv_idx(thisv), rec = vm->objs[h].a;
        if (id == N_HELPER_NEXT) {
            jv v = JV_UNDEF;
            int more = js_helper_step(vm, h, &v);
            if (vm->throwing) return JV_HOLE;
            return iter_result(vm, more ? v : JV_UNDEF, !more);
        }
        int st = (int)jv_dbl(ih_get(vm, rec, IH_STATE));
        if (st == 2) return js_throw_msg(vm, vm->type_error_proto, "Generator is already running");
        if (st != 3) {
            ih_done(vm, rec);
            jv inner = ih_get(vm, rec, IH_INNER);
            if (inner != JV_UNDEF) { iter_close_obj(vm, inner); if (vm->throwing) return JV_HOLE; }
            iter_close_obj(vm, ih_get(vm, rec, IH_ITER));
            if (vm->throwing) return JV_HOLE;
        }
        return iter_result(vm, JV_UNDEF, 1);
    }
    /* ---- the URI functions, escape, unescape ---- */
    case N_ENCODE_URI: case N_ENCODE_URIC: case N_DECODE_URI: case N_DECODE_URIC: case N_ESCAPE: case N_UNESCAPE: {
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        if (id == N_ESCAPE) return js_escape(vm, s);
        if (id == N_UNESCAPE) return js_unescape(vm, s);
        if (id == N_ENCODE_URI || id == N_ENCODE_URIC) return uri_encode(vm, s, id == N_ENCODE_URI);
        return uri_decode(vm, s, id == N_DECODE_URI);
    }
    case N_URI_ERROR: return make_error(vm, vm->uri_error_proto, args, argc);
    /* ---- Set's ES2025 methods ---- */
    case N_SETP_UNION: case N_SETP_INTERSECTION: case N_SETP_DIFFERENCE: case N_SETP_SYMDIFF: case N_SETP_ISSUBSET: case N_SETP_ISSUPERSET:
    case N_SETP_ISDISJOINT: return set_method(vm, id, thisv, a0);
    /* ---- Array's methods that change a copy ---- */
    case N_AP_TOREVERSED: case N_AP_TOSORTED: case N_AP_TOSPLICED: case N_AP_WITH: {
        if (id == N_AP_TOSORTED && a0 != JV_UNDEF && !js_is_callable(vm, a0))
            return value_error(vm, "The comparison function must be either a function or undefined: ", a0, "");
        AP_ON_NOTHING();
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        jv lv = js_get(vm, o, A_LENGTH);
        if (vm->throwing) return JV_HOLE;
        double n = js_to_number(vm, lv);
        if (vm->throwing) return JV_HOLE;
        n = n != n || n <= 0 ? 0 : n > 9007199254740991.0 ? 9007199254740991.0 : trunc(n);
        vector<jv> out;
        if (id == N_AP_WITH) {
            double rel = js_to_number(vm, a0);
            if (vm->throwing) return JV_HOLE;
            rel = rel != rel ? 0 : trunc(rel);
            double ix = rel >= 0 ? rel : n + rel;
            if (ix >= n || ix < 0) {
                char b[64], m[96];
                js_num_format(rel, b, sizeof b);
                snprintf(m, sizeof m, "Invalid index : %s", b);
                return js_throw_msg(vm, vm->range_error_proto, m);
            }
            if (n > 4294967295.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid array length");
            for (double i = 0; i < n; i++) {
                jv v = i == ix ? a1 : elem_of(vm, o, (uint32_t)i);
                if (vm->throwing) return JV_HOLE;
                push_back(out, v);
            }
        } else if (id == N_AP_TOSPLICED) {
            double st = 0, skip = 0;
            if (argc > 0) {
                double rs = js_to_number(vm, a0);
                if (vm->throwing) return JV_HOLE;
                rs = rs != rs ? 0 : trunc(rs);
                st = rs < 0 ? (n + rs > 0 ? n + rs : 0) : (rs < n ? rs : n);
            }
            if (argc == 1) skip = n - st;
            else if (argc > 1) {
                double sc = js_to_number(vm, a1);
                if (vm->throwing) return JV_HOLE;
                sc = sc != sc ? 0 : trunc(sc);
                skip = sc < 0 ? 0 : sc > n - st ? n - st : sc;
            }
            double ins = argc > 2 ? argc - 2 : 0, nl = n + ins - skip;
            if (nl > 9007199254740991.0) return js_throw_msg(vm, vm->type_error_proto, "Invalid array length");
            if (nl > 4294967295.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid array length");
            for (double i = 0; i < st; i++) {
                jv v = elem_of(vm, o, (uint32_t)i);
                if (vm->throwing) return JV_HOLE;
                push_back(out, v);
            }
            for (uint32_t i = 2; i < argc; i++) push_back(out, arg(vm, args, argc, i));
            for (double i = st + skip; i < n; i++) {
                jv v = elem_of(vm, o, (uint32_t)i);
                if (vm->throwing) return JV_HOLE;
                push_back(out, v);
            }
        } else {
            if (n > 4294967295.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid array length");
            for (double i = 0; i < n; i++) {
                jv v = elem_of(vm, o, (uint32_t)(id == N_AP_TOREVERSED ? n - 1 - i : i));
                if (vm->throwing) return JV_HOLE;
                push_back(out, v);
            }
            if (id == N_AP_TOSORTED) {
                vector<jv> tmp;
                resize(tmp, len(out));
                int err = 0;
                merge_sort(vm, &out, &tmp, 0, len(out), a0, &err);
                if (err || vm->throwing) return JV_HOLE;
            }
        }
        uint32_t r = js_array_new(vm, 0);
        for (size_t i = 0; i < len(out); i++) js_array_push(vm, r, at(out, i));
        return jv_obj(r);
    }
    case N_AP_TOLOCALE: {
        /* each element's own toLocaleString, given the locales and options; an empty string for null and undefined */
        AP_ON_NOTHING();
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        double n = len_of(vm, o);
        if (vm->throwing) return JV_HOLE;
        uint32_t r = A_EMPTY, comma = js_str_ascii(vm, ",");
        for (double i = 0; i < n; i++) {
            if (i > 0) r = js_str_concat(vm, r, comma);
            jv e = elem_of(vm, o, (uint32_t)i);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_nullish(e)) continue;
            jv f = js_get_value(vm, e, jv_str(js_atom_ascii(vm, "toLocaleString")));
            if (vm->throwing) return JV_HOLE;
            if (!js_is_callable(vm, f)) return js_not_function(vm, f);
            jv av[2] = { a0, a1 };
            jv sv = js_call(vm, f, e, av, 2);
            if (vm->throwing) return JV_HOLE;
            uint32_t ss = js_to_string(vm, sv);
            if (!ss) return JV_HOLE;
            r = js_str_concat(vm, r, ss);
        }
        return jv_str(r);
    }
    /* ---- String: well-formedness, and the HTML methods ---- */
    case N_SP_ISWELLFORMED: case N_SP_TOWELLFORMED: {
        if (jv_is_nullish(thisv))
            return js_throw_msg(vm, vm->type_error_proto, id == N_SP_ISWELLFORMED ? "String.prototype.isWellFormed called on null or undefined" :
                                "String.prototype.toWellFormed called on null or undefined");
        uint32_t s = js_to_string(vm, thisv);
        if (!s) return JV_HOLE;
        js_str_flatten(vm, s);
        uint32_t n = vm->strs[s].len;
        vector<uint16_t> out;
        int ok = 1;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t c = vm->units[vm->strs[s].off + i];
            int lone = 0;
            if (c >= 0xD800 && c <= 0xDBFF) {
                uint16_t d = i + 1 < n ? vm->units[vm->strs[s].off + i + 1] : 0;
                if (d >= 0xDC00 && d <= 0xDFFF) { push_back(out, c); push_back(out, d); i++; continue; }
                lone = 1;
            } else if (c >= 0xDC00 && c <= 0xDFFF) lone = 1;
            if (lone) { ok = 0; push_back(out, (uint16_t)0xFFFD); }
            else push_back(out, c);
        }
        if (id == N_SP_ISWELLFORMED) return jv_bool(ok);
        return ok ? jv_str(s) : jv_str(js_str_new(vm, data(out), len(out)));
    }
    case N_SP_ANCHOR: return string_html(vm, thisv, "anchor", "a", "name", a0);
    case N_SP_BIG: return string_html(vm, thisv, "big", "big", 0, JV_UNDEF);
    case N_SP_BLINK: return string_html(vm, thisv, "blink", "blink", 0, JV_UNDEF);
    case N_SP_BOLD: return string_html(vm, thisv, "bold", "b", 0, JV_UNDEF);
    case N_SP_FIXED: return string_html(vm, thisv, "fixed", "tt", 0, JV_UNDEF);
    case N_SP_FONTCOLOR: return string_html(vm, thisv, "fontcolor", "font", "color", a0);
    case N_SP_FONTSIZE: return string_html(vm, thisv, "fontsize", "font", "size", a0);
    case N_SP_ITALICS: return string_html(vm, thisv, "italics", "i", 0, JV_UNDEF);
    case N_SP_LINK: return string_html(vm, thisv, "link", "a", "href", a0);
    case N_SP_SMALL: return string_html(vm, thisv, "small", "small", 0, JV_UNDEF);
    case N_SP_STRIKE: return string_html(vm, thisv, "strike", "strike", 0, JV_UNDEF);
    case N_SP_SUB: return string_html(vm, thisv, "sub", "sub", 0, JV_UNDEF);
    case N_SP_SUP: return string_html(vm, thisv, "sup", "sup", 0, JV_UNDEF);
    /* ---- Number.prototype.toExponential ---- */
    case N_NP_TOEXPONENTIAL: {
        jv tv = thisv;
        if (jv_is_obj(tv) && vm->objs[jv_idx(tv)].kind == JK_NUMBER) tv = vm->objs[jv_idx(tv)].x;
        if (!jv_is_num(tv)) return js_throw_msg(vm, vm->type_error_proto, "Number.prototype.toExponential requires that 'this' be a Number");
        double x = jv_dbl(tv), f = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        f = f != f ? 0 : trunc(f);
        if (!isfinite(x)) return jv_str(js_num_to_str(vm, x));
        if (f < 0 || f > 100) return js_throw_msg(vm, vm->range_error_proto, "toExponential() argument must be between 0 and 100");
        return jv_str(num_to_exponential(vm, x, f, a0 != JV_UNDEF));
    }
    case N_M_ACOSH: return jv_num(acosh(num_arg(vm, args, argc, 0)));
    case N_M_ASINH: { double x = num_arg(vm, args, argc, 0); return jv_num(x == 0 ? x : asinh(x)); }
    case N_M_ATANH: { double x = num_arg(vm, args, argc, 0); return jv_num(x == 0 ? x : atanh(x)); }
    /* ---- Object.prototype's legacy accessor methods (B.2.2.2-5) ---- */
    case N_OP_DEFGETTER: case N_OP_DEFSETTER: {
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, a1))
            return js_throw_msg(vm, vm->type_error_proto, id == N_OP_DEFGETTER ? "Object.prototype.__defineGetter__: Expecting function" :
                                "Object.prototype.__defineSetter__: Expecting function");
        int ok = 1;
        uint32_t key = js_prop_key(vm, a0, &ok);
        if (!ok) return JV_HOLE;
        js_define_accessor(vm, jv_idx(o), key, id == N_OP_DEFGETTER ? a1 : JV_HOLE, id == N_OP_DEFSETTER ? a1 : JV_HOLE, 0);
        return JV_UNDEF;
    }
    case N_OP_LOOKUPGETTER: case N_OP_LOOKUPSETTER: {
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        int ok = 1;
        uint32_t key = js_prop_key(vm, a0, &ok);
        if (!ok) return JV_HOLE;
        for (uint32_t x = jv_idx(o), d = 0; x && d < 10000; x = vm->objs[x].proto, d++) {
            jv v;
            uint32_t fl;
            if (!js_get_own(vm, x, key, &v, &fl)) continue;
            if (!(fl & JS_SHAPE_ACCESSOR)) return JV_UNDEF;
            uint32_t acc = jv_idx(v);
            if (id == N_OP_LOOKUPGETTER) return vm->objs[acc].x;
            return vm->objs[acc].a ? jv_obj(vm->objs[acc].a) : JV_UNDEF;
        }
        return JV_UNDEF;
    }
    case N_MP_GET: case N_MP_SET: case N_MP_HAS: case N_MP_DELETE: case N_MP_CLEAR: case N_MP_SIZE: case N_MP_FOREACH:
    case N_MP_KEYS: case N_MP_VALUES: case N_MP_ENTRIES: case N_SETP_ADD: case N_SETP_FOREACH: case N_SETP_VALUES: case N_SETP_ENTRIES:
    case N_SETP_HAS: case N_SETP_DELETE: case N_SETP_CLEAR: case N_SETP_SIZE: {
        int set = id >= N_SETP_ADD;
        uint32_t c = this_coll(vm, thisv, set ? JK_SET : JK_MAP, id);
        if (!c) return JV_HOLE;
        /* Set's own ids do what Map's do */
        if (id == N_SETP_HAS) id = N_MP_HAS;
        else if (id == N_SETP_DELETE) id = N_MP_DELETE;
        else if (id == N_SETP_CLEAR) id = N_MP_CLEAR;
        else if (id == N_SETP_SIZE) id = N_MP_SIZE;
        uint32_t arr = vm->objs[c].a;
        if (id == N_MP_SIZE) return jv_num(vm->objs[c].b);
        if (id == N_MP_CLEAR) {
            /* the entries truncated, the index dropped */
            js_array_set_length(vm, arr, 0);
            vm->objs[c].b = 0;
            vm->objs[c].c = 0;
            vm->objs[c].x = jv_num(0);
            return JV_UNDEF;
        }
        if (id == N_MP_KEYS) return iter_of_coll(vm, c, set ? JI_SET : JI_MAP_KEYS);
        if (id == N_MP_VALUES || id == N_SETP_VALUES) return iter_of_coll(vm, c, set ? JI_SET : JI_MAP_VALUES);
        if (id == N_MP_ENTRIES || id == N_SETP_ENTRIES) return iter_of_coll(vm, c, set ? JI_SET_ENTRIES : JI_ENTRIES);
        if (id == N_MP_FOREACH || id == N_SETP_FOREACH) {
            if (!js_is_callable(vm, a0)) return js_not_function(vm, a0);
            uint32_t step = set ? 1 : 2;
            for (uint32_t i = 0; i < vm->objs[arr].b && !vm->throwing; i += step) {
                jv k = vm->slots[vm->objs[arr].a + i];
                if (k == JV_HOLE) continue;
                jv v = set ? k : vm->slots[vm->objs[arr].a + i + 1];
                callback(vm, a0, a1, v, k, thisv);
            }
            return vm->throwing ? JV_HOLE : JV_UNDEF;
        }
        int64_t at_i = coll_find(vm, c, a0);
        if (id == N_MP_HAS) return jv_bool(at_i >= 0);
        if (id == N_MP_GET) return at_i >= 0 && !set ? vm->slots[vm->objs[arr].a + at_i + 1] : JV_UNDEF;
        if (id == N_MP_DELETE) {
            if (at_i < 0) return JV_FALSE;
            coll_remove(vm, c, a0, (uint32_t)at_i);
            return JV_TRUE;
        }
        jv key = a0;
        if (jv_is_num(key) && jv_dbl(key) == 0) key = jv_num(0);
        if (id == N_SETP_ADD) {
            if (at_i < 0) coll_append(vm, c, key, JV_UNDEF);
            return thisv;
        }
        if (at_i >= 0) vm->slots[vm->objs[arr].a + at_i + 1] = a1;
        else coll_append(vm, c, key, a1);
        return thisv;
    }
    case N_ITP_NEXT: case N_MIT_NEXT: case N_SIT_NEXT: case N_STIT_NEXT: case N_RSIT_NEXT: {
        /* each kind's next takes an iterator of its own kind */
        int fam = id == N_ITP_NEXT ? 0 : id == N_MIT_NEXT ? 1 : id == N_SIT_NEXT ? 2 : id == N_STIT_NEXT ? 3 : 4;
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ITER || iter_family(vm->objs[jv_idx(thisv)].c) != fam) {
            static const char *const fn[] = { "Array Iterator", "Map Iterator", "Set Iterator", "String Iterator", "%RegExpStringIterator%" };
            char pre[96];
            snprintf(pre, sizeof pre, "Method %s.prototype.next called on incompatible receiver ", fn[fam]);
            return value_error(vm, pre, thisv, "");
        }
        jv v = JV_UNDEF;
        int more = js_iter_step(vm, jv_idx(thisv), &v);
        if (vm->throwing) return JV_HOLE;
        return iter_result(vm, more ? v : JV_UNDEF, !more);
    }
    default: break;
    }
    return js_throw_msg(vm, vm->type_error_proto, "a native function the engine does not have");
}

/* ---- the prototypes and globals ---- */

void js_builtins_init(js_vm *vm) {
    vm->object_proto = js_obj_new(vm, JK_PLAIN, 0);
    vm->function_proto = js_obj_new(vm, JK_NATIVE, vm->object_proto);
    vm->objs[vm->function_proto].a = N_NONE;
    /* Array.prototype an array itself (ECMA-262 23.1.3, V8's): its length 0, its elements an array's */
    vm->array_proto = js_array_new(vm, 0);
    js_set_proto(vm, vm->array_proto, vm->object_proto);
    vm->string_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->number_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->boolean_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->error_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->type_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->range_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->syntax_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->reference_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->eval_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->aggregate_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
    vm->date_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->regexp_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->map_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->set_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->iterator_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->symbol_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->generator_proto = js_obj_new(vm, JK_PLAIN, vm->iterator_proto);
    vm->async_iterator_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->async_generator_proto = js_obj_new(vm, JK_PLAIN, vm->async_iterator_proto);
    vm->promise_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    /* the global object; a host's above an object of the host's (a JK_HOST of kind 0),
       whose properties the host answers (src/script/custom.c: the window's HTML element
       interfaces, made when a script first names one) */
    uint32_t gp = vm->object_proto;
    if (vm->host) {
        gp = js_obj_new(vm, JK_HOST, vm->object_proto);
        vm->objs[gp].c = 0;
    }
    vm->global = js_obj_new(vm, JK_PLAIN, gp);
    vm->global_lex = js_obj_new(vm, JK_PLAIN, 0);
    uint32_t g = vm->global;
    defv(vm, g, "globalThis", jv_obj(g));
    defv(vm, g, "undefined", JV_UNDEF);
    defv(vm, g, "NaN", jv_num(NAN));
    defv(vm, g, "Infinity", jv_num(INFINITY));
    defn(vm, g, "parseInt", N_PARSE_INT, 2);
    defn(vm, g, "parseFloat", N_PARSE_FLOAT, 1);
    defn(vm, g, "isNaN", N_IS_NAN, 1);
    defn(vm, g, "isFinite", N_IS_FINITE, 1);
    defn(vm, g, "eval", N_EVAL, 1);
    vm->eval_fn = jv_idx(js_get(vm, jv_obj(g), js_atom_ascii(vm, "eval")));
    defn(vm, g, "setTimeout", N_SET_TIMEOUT, 2);
    defn(vm, g, "setInterval", N_SET_INTERVAL, 2);
    defn(vm, g, "clearTimeout", N_CLEAR_TIMEOUT, 1);
    defn(vm, g, "clearInterval", N_CLEAR_TIMEOUT, 1);
    defn(vm, g, "queueMicrotask", N_QUEUE_MICROTASK, 1);
    defn(vm, g, "decodeURI", N_DECODE_URI, 1);
    defn(vm, g, "decodeURIComponent", N_DECODE_URIC, 1);
    defn(vm, g, "encodeURI", N_ENCODE_URI, 1);
    defn(vm, g, "encodeURIComponent", N_ENCODE_URIC, 1);
    defn(vm, g, "escape", N_ESCAPE, 1);
    defn(vm, g, "unescape", N_UNESCAPE, 1);

    uint32_t o = ctor(vm, "Object", N_OBJECT, 1, vm->object_proto);
    defn(vm, o, "keys", N_OBJ_KEYS, 1);
    defn(vm, o, "values", N_OBJ_VALUES, 1);
    defn(vm, o, "entries", N_OBJ_ENTRIES, 1);
    defn(vm, o, "assign", N_OBJ_ASSIGN, 2);
    defn(vm, o, "create", N_OBJ_CREATE, 2);
    defn(vm, o, "getPrototypeOf", N_OBJ_GETPROTO, 1);
    defn(vm, o, "setPrototypeOf", N_OBJ_SETPROTO, 2);
    defn(vm, o, "defineProperty", N_OBJ_DEFPROP, 3);
    defn(vm, o, "defineProperties", N_OBJ_DEFPROPS, 2);
    defn(vm, o, "getOwnPropertyNames", N_OBJ_NAMES, 1);
    defn(vm, o, "getOwnPropertyDescriptor", N_OBJ_GETOWNDESC, 2);
    defn(vm, o, "freeze", N_OBJ_FREEZE, 1);
    defn(vm, o, "isFrozen", N_PX_FIRST + JPX_OBJ_ISFROZEN, 1);
    defn(vm, o, "preventExtensions", N_PX_FIRST + JPX_OBJ_PREVENTEXT, 1);
    defn(vm, o, "seal", N_PX_FIRST + JPX_OBJ_SEAL, 1);
    defn(vm, o, "isExtensible", N_OBJ_ISEXT, 1);
    defn(vm, o, "isSealed", N_PX_FIRST + JPX_OBJ_ISSEALED, 1);
    defn(vm, o, "hasOwn", N_OBJ_HASOWN, 2);
    defn(vm, o, "getOwnPropertyDescriptors", N_OBJ_GETOWNDESCS, 1);
    defn(vm, o, "groupBy", N_OBJ_GROUPBY, 2);
    defn(vm, o, "fromEntries", N_OBJ_FROMENTRIES, 1);
    defn(vm, o, "is", N_OBJ_IS, 2);
    uint32_t op = vm->object_proto;
    js_define_accessor(vm, op, js_atom_ascii(vm, "__proto__"), jv_obj(js_native_new(vm, N_OP_PROTO_GET, js_atom_ascii(vm, "__proto__"), 0)),
                       jv_obj(js_native_new(vm, N_OP_PROTO_SET, js_atom_ascii(vm, "__proto__"), 1)), JS_SHAPE_HIDDEN);
    /* %ThrowTypeError%: no name, no length to count, not extensible */
    vm->throw_te = js_native_new(vm, N_THROW_TE, A_EMPTY, 0);
    vm->objs[vm->throw_te].flags &= (uint16_t)~JO_EXTENSIBLE;
    vm->objs[vm->throw_te].flags |= JO_SEALED;   /* its length and name not configurable: it is frozen (ECMA-262 10.2.4.1) */
    /* Function.prototype's arguments and caller (AddRestrictedFunctionProperties): %ThrowTypeError% to get and set */
    js_define_accessor(vm, vm->function_proto, A_ARGUMENTS, jv_obj(vm->throw_te), jv_obj(vm->throw_te), JS_SHAPE_HIDDEN);
    js_define_accessor(vm, vm->function_proto, A_CALLER, jv_obj(vm->throw_te), jv_obj(vm->throw_te), JS_SHAPE_HIDDEN);
    defn(vm, op, "hasOwnProperty", N_OP_HASOWN, 1);
    defn(vm, op, "toString", N_OP_TOSTRING, 0);
    defn(vm, op, "toLocaleString", N_OP_TOLOCALE, 0);
    defn(vm, op, "valueOf", N_OP_VALUEOF, 0);
    defn(vm, op, "isPrototypeOf", N_OP_ISPROTO, 1);
    defn(vm, op, "propertyIsEnumerable", N_OP_PROPENUM, 1);
    defn(vm, op, "__defineGetter__", N_OP_DEFGETTER, 2);
    defn(vm, op, "__defineSetter__", N_OP_DEFSETTER, 2);
    defn(vm, op, "__lookupGetter__", N_OP_LOOKUPGETTER, 1);
    defn(vm, op, "__lookupSetter__", N_OP_LOOKUPSETTER, 1);

    uint32_t fp = vm->function_proto;
    ctor(vm, "Function", N_FUNCTION, 1, fp);
    defn(vm, fp, "call", N_FP_CALL, 1);
    defn(vm, fp, "apply", N_FP_APPLY, 2);
    defn(vm, fp, "bind", N_FP_BIND, 1);
    defn(vm, fp, "toString", N_FP_TOSTRING, 0);

    uint32_t a = ctor(vm, "Array", N_ARRAY, 1, vm->array_proto);
    defn(vm, a, "isArray", N_ARR_ISARRAY, 1);
    defn(vm, a, "from", N_ARR_FROM, 1);
    defn(vm, a, "of", N_ARR_OF, 0);
    uint32_t ap = vm->array_proto;
    static const char *const an[] = { "push", "pop", "shift", "unshift", "slice", "splice", "concat", "join", "reverse", "indexOf",
        "lastIndexOf", "includes", "find", "findIndex", "findLast", "findLastIndex", "filter", "map", "forEach", "reduce", "reduceRight",
        "some", "every", "sort", "fill", "keys", "values", "entries", "flat", "flatMap", "at", "copyWithin", "toString" };
    for (uint32_t i = 0; i < sizeof an / sizeof an[0]; i++) defn(vm, ap, an[i], N_AP_PUSH + i, 1);
    defn(vm, ap, "toReversed", N_AP_TOREVERSED, 0);
    defn(vm, ap, "toSorted", N_AP_TOSORTED, 1);
    defn(vm, ap, "toSpliced", N_AP_TOSPLICED, 2);
    defn(vm, ap, "with", N_AP_WITH, 2);
    defn(vm, ap, "toLocaleString", N_AP_TOLOCALE, 0);

    uint32_t s = ctor(vm, "String", N_STRING, 1, vm->string_proto);
    defn(vm, s, "fromCharCode", N_STR_FROMCHARCODE, 1);
    defn(vm, s, "raw", N_STR_RAW, 1);
    defn(vm, s, "fromCodePoint", N_STR_FROMCODEPOINT, 1);
    uint32_t sp = vm->string_proto;
    static const char *const sn[] = { "charAt", "charCodeAt", "codePointAt", "indexOf", "lastIndexOf", "includes", "startsWith", "endsWith",
        "slice", "substring", "substr", "toUpperCase", "toLowerCase", "trim", "trimStart", "trimEnd", "padStart", "padEnd", "repeat",
        "split", "replace", "replaceAll", "concat", "at", "localeCompare", "toString", "normalize", "match", "search" };
    for (uint32_t i = 0; i < sizeof sn / sizeof sn[0]; i++) defn(vm, sp, sn[i], N_SP_CHARAT + i, 1);
    defn(vm, sp, "normalize", N_SP_NORMALIZE, 0);
    defn(vm, sp, "valueOf", N_SP_VALUEOF, 0);
    defn(vm, sp, "toLocaleUpperCase", N_SP_TOUPPER, 0);
    defn(vm, sp, "toLocaleLowerCase", N_SP_TOLOWER, 0);
    /* Annex B's names for trimStart and trimEnd: the same functions (V8's toString names them so) */
    js_define(vm, sp, js_atom_ascii(vm, "trimLeft"), js_get(vm, jv_obj(sp), js_atom_ascii(vm, "trimStart")), JS_SHAPE_HIDDEN);
    defn(vm, sp, "isWellFormed", N_SP_ISWELLFORMED, 0);
    defn(vm, sp, "toWellFormed", N_SP_TOWELLFORMED, 0);
    {
        static const char *const hm[] = { "anchor", "big", "blink", "bold", "fixed", "fontcolor", "fontsize", "italics", "link", "small", "strike", "sub", "sup" };
        static const uint32_t hl[] = { 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0 };
        for (uint32_t i = 0; i < 13; i++) defn(vm, sp, hm[i], N_SP_ANCHOR + i, hl[i]);
    }
    js_define(vm, sp, js_atom_ascii(vm, "trimRight"), js_get(vm, jv_obj(sp), js_atom_ascii(vm, "trimEnd")), JS_SHAPE_HIDDEN);

    uint32_t nm = ctor(vm, "Number", N_NUMBER, 1, vm->number_proto);
    defn(vm, nm, "isInteger", N_NUM_ISINTEGER, 1);
    defn(vm, nm, "isSafeInteger", N_NUM_ISSAFEINTEGER, 1);
    defn(vm, nm, "isFinite", N_NUM_ISFINITE, 1);
    defn(vm, nm, "isNaN", N_NUM_ISNAN, 1);
    defn(vm, nm, "parseFloat", N_PARSE_FLOAT, 1);
    defn(vm, nm, "parseInt", N_PARSE_INT, 2);
    defv(vm, nm, "MAX_SAFE_INTEGER", jv_num(9007199254740991.0));
    defv(vm, nm, "MIN_SAFE_INTEGER", jv_num(-9007199254740991.0));
    defv(vm, nm, "EPSILON", jv_num(2.220446049250313e-16));
    defv(vm, nm, "MAX_VALUE", jv_num(1.7976931348623157e308));
    defv(vm, nm, "MIN_VALUE", jv_num(5e-324));
    defv(vm, nm, "POSITIVE_INFINITY", jv_num(INFINITY));
    defv(vm, nm, "NEGATIVE_INFINITY", jv_num(-INFINITY));
    defv(vm, nm, "NaN", jv_num(NAN));
    uint32_t np = vm->number_proto;
    defn(vm, np, "toString", N_NP_TOSTRING, 1);
    defn(vm, np, "toFixed", N_NP_TOFIXED, 1);
    defn(vm, np, "toPrecision", N_NP_TOPRECISION, 1);
    defn(vm, np, "toExponential", N_NP_TOEXPONENTIAL, 1);
    defn(vm, np, "valueOf", N_NP_VALUEOF, 0);
    defn(vm, np, "toLocaleString", N_NP_TOLOCALE, 0);
    ctor(vm, "Boolean", N_BOOLEAN, 1, vm->boolean_proto);
    defn(vm, vm->boolean_proto, "toString", N_BP_TOSTRING, 0);
    defn(vm, vm->boolean_proto, "valueOf", N_BP_VALUEOF, 0);

    uint32_t m = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    defv(vm, g, "Math", jv_obj(m));
    static const char *const mn[] = { "abs", "floor", "ceil", "round", "trunc", "sign", "max", "min", "pow", "sqrt", "cbrt", "exp", "expm1",
        "log", "log2", "log10", "log1p", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "hypot", "random",
        "fround", "imul", "clz32" };
    /* each's length V8's: 2 max, min, pow, atan2, hypot and imul, 0 random, else 1 */
    for (uint32_t i = 0; i < sizeof mn / sizeof mn[0]; i++) {
        uint32_t nl = 1;
        if (!strcmp(mn[i], "max") || !strcmp(mn[i], "min") || !strcmp(mn[i], "pow") || !strcmp(mn[i], "atan2") || !strcmp(mn[i], "hypot") ||
            !strcmp(mn[i], "imul")) nl = 2;
        else if (!strcmp(mn[i], "random")) nl = 0;
        defn(vm, m, mn[i], N_M_ABS + i, nl);
    }
    defn(vm, m, "acosh", N_M_ACOSH, 1);
    defn(vm, m, "asinh", N_M_ASINH, 1);
    defn(vm, m, "atanh", N_M_ATANH, 1);
    defv(vm, m, "PI", jv_num(3.141592653589793));
    defv(vm, m, "E", jv_num(2.718281828459045));
    defv(vm, m, "LN2", jv_num(0.6931471805599453));
    defv(vm, m, "LN10", jv_num(2.302585092994046));
    defv(vm, m, "LOG2E", jv_num(1.4426950408889634));
    defv(vm, m, "LOG10E", jv_num(0.4342944819032518));
    defv(vm, m, "SQRT2", jv_num(1.4142135623730951));
    defv(vm, m, "SQRT1_2", jv_num(0.7071067811865476));
    /* Math's and JSON's tags (ECMA-262 21.3.1.9, 25.5.3): not writable, not enumerable */
    js_define(vm, m, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Math")), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);

    uint32_t j = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    defv(vm, g, "JSON", jv_obj(j));
    js_define(vm, j, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "JSON")), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
    defn(vm, j, "stringify", N_JSON_STRINGIFY, 3);
    defn(vm, j, "parse", N_JSON_PARSE, 2);
    uint32_t c = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    defv(vm, g, "console", jv_obj(c));
    defn(vm, c, "log", N_CON_LOG, 0);
    defn(vm, c, "info", N_CON_INFO, 0);
    defn(vm, c, "warn", N_CON_WARN, 0);
    defn(vm, c, "error", N_CON_ERROR, 0);
    defn(vm, c, "debug", N_CON_DEBUG, 0);
    defn(vm, c, "trace", N_CON_DEBUG, 0);
    uint32_t perf = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    defv(vm, g, "performance", jv_obj(perf));
    defn(vm, perf, "now", N_PERF_NOW, 0);

    static const char *const en[] = { "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError", "EvalError" };
    uint32_t eps[6] = { vm->error_proto, vm->type_error_proto, vm->range_error_proto, vm->syntax_error_proto, vm->reference_error_proto, vm->eval_error_proto };
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t e = ctor(vm, en[i], N_ERROR + i, 1, eps[i]);
        defv(vm, eps[i], "name", jv_str(js_atom_ascii(vm, en[i])));
        defv(vm, eps[i], "message", jv_str(A_EMPTY));
        if (i == 0) vm->error_ctor = e;
        else vm->objs[e].proto = vm->error_ctor;
    }
    defn(vm, vm->error_proto, "toString", N_EP_TOSTRING, 0);
    {
        /* URIError, the URI functions' */
        vm->uri_error_proto = js_obj_new(vm, JK_PLAIN, vm->error_proto);
        uint32_t ue = ctor(vm, "URIError", N_URI_ERROR, 1, vm->uri_error_proto);
        defv(vm, vm->uri_error_proto, "name", jv_str(js_atom_ascii(vm, "URIError")));
        defv(vm, vm->uri_error_proto, "message", jv_str(A_EMPTY));
        vm->objs[ue].proto = vm->error_ctor;
    }

    uint32_t d = ctor(vm, "Date", N_DATE, 7, vm->date_proto);
    defn(vm, d, "now", N_DATE_NOW, 0);
    defn(vm, d, "parse", N_DATE_PARSE, 1);
    defn(vm, d, "UTC", N_DATE_UTC, 7);
    {
        /* Date.prototype's methods in V8's order (the UTC getters and the local ones one function each, the zone UTC;
           toGMTString toUTCString itself) */
        uint32_t dp = vm->date_proto;
        static const struct { const char *name; uint32_t id, len; } dm[] = {
            { "toString", N_DP_TOSTRING, 0 }, { "toDateString", N_DP_TODATESTRING, 0 }, { "toTimeString", N_DP_TOTIMESTRING, 0 },
            { "toISOString", N_DP_TOISO, 0 }, { "toUTCString", N_DP_TOUTCSTRING, 0 }, { "toGMTString", 0, 0 },
            { "getDate", N_DP_GETDATE, 0 }, { "setDate", N_DP_SETDATE, 1 }, { "getDay", N_DP_GETDAY, 0 },
            { "getFullYear", N_DP_GETFULLYEAR, 0 }, { "setFullYear", N_DP_SETFULLYEAR, 3 }, { "getHours", N_DP_GETHOURS, 0 },
            { "setHours", N_DP_SETHOURS, 4 }, { "getMilliseconds", N_DP_GETMS, 0 }, { "setMilliseconds", N_DP_SETMS, 1 },
            { "getMinutes", N_DP_GETMINUTES, 0 }, { "setMinutes", N_DP_SETMIN, 3 }, { "getMonth", N_DP_GETMONTH, 0 },
            { "setMonth", N_DP_SETMONTH, 2 }, { "getSeconds", N_DP_GETSECONDS, 0 }, { "setSeconds", N_DP_SETSEC, 2 },
            { "getTime", N_DP_GETTIME, 0 }, { "setTime", N_DP_SETTIME, 1 }, { "getTimezoneOffset", N_DP_GETTZO, 0 },
            { "getUTCDate", N_DP_GETDATE, 0 }, { "setUTCDate", N_DP_SETUTCDATE, 1 }, { "getUTCDay", N_DP_GETDAY, 0 },
            { "getUTCFullYear", N_DP_GETFULLYEAR, 0 }, { "setUTCFullYear", N_DP_SETUTCFULLYEAR, 3 }, { "getUTCHours", N_DP_GETHOURS, 0 },
            { "setUTCHours", N_DP_SETUTCHOURS, 4 }, { "getUTCMilliseconds", N_DP_GETMS, 0 }, { "setUTCMilliseconds", N_DP_SETUTCMS, 1 },
            { "getUTCMinutes", N_DP_GETMINUTES, 0 }, { "setUTCMinutes", N_DP_SETUTCMIN, 3 }, { "getUTCMonth", N_DP_GETMONTH, 0 },
            { "setUTCMonth", N_DP_SETUTCMONTH, 2 }, { "getUTCSeconds", N_DP_GETSECONDS, 0 }, { "setUTCSeconds", N_DP_SETUTCSEC, 2 },
            { "valueOf", N_DP_GETTIME, 0 }, { "getYear", N_DP_GETYEAR, 0 }, { "setYear", N_DP_SETYEAR, 1 }, { "toJSON", N_DP_TOJSON, 1 } };
        for (size_t i = 0; i < sizeof dm / sizeof dm[0]; i++) {
            if (dm[i].id) defn(vm, dp, dm[i].name, dm[i].id, dm[i].len);
            else js_define(vm, dp, js_atom_ascii(vm, dm[i].name), js_get(vm, jv_obj(dp), js_atom_ascii(vm, "toUTCString")), JS_SHAPE_HIDDEN);
        }
        /* Date.prototype[Symbol.toPrimitive]: not writable, configurable */
        js_define(vm, dp, JS_SYM_KEY | JSYM_TO_PRIMITIVE, jv_obj(js_native_new(vm, N_DP_TOPRIM, js_str_ascii(vm, "[Symbol.toPrimitive]"), 1)),
                  JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
    }

    js_re_init(vm, N_RE_FIRST);
    defn(vm, vm->string_proto, "matchAll", N_SP_MATCHALL, 1);

    uint32_t mapc = ctor(vm, "Map", N_MAP, 0, vm->map_proto);
    defn(vm, mapc, "groupBy", N_MAP_GROUPBY, 2);
    uint32_t mp = vm->map_proto;
    static const char *const mpn[] = { "get", "set", "has", "delete", "clear" };
    for (uint32_t i = 0; i < 5; i++) defn(vm, mp, mpn[i], N_MP_GET + i, 1);
    defn(vm, mp, "forEach", N_MP_FOREACH, 1);
    defn(vm, mp, "keys", N_MP_KEYS, 0);
    defn(vm, mp, "values", N_MP_VALUES, 0);
    defn(vm, mp, "entries", N_MP_ENTRIES, 0);
    js_define_accessor(vm, mp, A_SIZE, jv_obj(js_native_new(vm, N_MP_SIZE, A_SIZE, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    ctor(vm, "Set", N_SET, 0, vm->set_proto);
    uint32_t stp = vm->set_proto;
    defn(vm, stp, "add", N_SETP_ADD, 1);
    defn(vm, stp, "has", N_SETP_HAS, 1);
    defn(vm, stp, "delete", N_SETP_DELETE, 1);
    defn(vm, stp, "clear", N_SETP_CLEAR, 0);
    defn(vm, stp, "forEach", N_SETP_FOREACH, 1);
    defn(vm, stp, "values", N_SETP_VALUES, 0);
    /* keys is values itself, as Symbol.iterator is */
    js_define(vm, stp, js_atom_ascii(vm, "keys"), js_get(vm, jv_obj(stp), js_atom_ascii(vm, "values")), JS_SHAPE_HIDDEN);
    {
        static const char *const sm[] = { "union", "intersection", "difference", "symmetricDifference", "isSubsetOf", "isSupersetOf", "isDisjointFrom" };
        for (uint32_t i = 0; i < 7; i++) defn(vm, stp, sm[i], N_SETP_UNION + i, 1);
    }
    defn(vm, stp, "entries", N_SETP_ENTRIES, 0);
    js_define_accessor(vm, stp, A_SIZE, jv_obj(js_native_new(vm, N_SETP_SIZE, A_SIZE, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    {
        /* %ArrayIteratorPrototype% and its kin: each under %IteratorPrototype%, its next and its tag */
        static const char *const tags[] = { "Array Iterator", "Map Iterator", "Set Iterator", "String Iterator", "RegExp String Iterator" };
        static const uint32_t nexts[] = { N_ITP_NEXT, N_MIT_NEXT, N_SIT_NEXT, N_STIT_NEXT, N_RSIT_NEXT };
        uint32_t protos[5];
        for (uint32_t i = 0; i < 5; i++) {
            protos[i] = js_obj_new(vm, JK_PLAIN, vm->iterator_proto);
            defn(vm, protos[i], "next", nexts[i], 0);
            js_define(vm, protos[i], JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, tags[i])), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
        }
        vm->array_iter_proto = protos[0];
        vm->map_iter_proto = protos[1];
        vm->set_iter_proto = protos[2];
        vm->string_iter_proto = protos[3];
        vm->regexp_iter_proto = protos[4];
    }
    js_proxy_init(vm, N_PX_FIRST);
    js_big_init(vm, N_BG_FIRST);
    js_module_init(vm, N_MD_FIRST);
    /* the typed arrays, and the methods they share with Array (generic over array-likes) */
    js_typed_init(vm, N_TA_FIRST);
    {
        uint32_t tp = vm->typedarray_proto;
        static const char *const shared[] = { "forEach", "every", "some", "find", "findIndex", "findLast", "findLastIndex", "indexOf",
                                              "lastIndexOf", "includes", "join", "reduce", "reduceRight", "at", "keys", "entries", "toLocaleString" };
        static const uint32_t sids[] = { N_AP_FOREACH, N_AP_EVERY, N_AP_SOME, N_AP_FIND, N_AP_FINDINDEX, N_AP_FINDLAST, N_AP_FINDLASTINDEX,
                                         N_AP_INDEXOF, N_AP_LASTINDEXOF, N_AP_INCLUDES, N_AP_JOIN, N_AP_REDUCE, N_AP_REDUCERIGHT, N_AP_AT,
                                         N_AP_KEYS, N_AP_ENTRIES, N_AP_JOIN };
        for (uint32_t i = 0; i < 17; i++) defn(vm, tp, shared[i], sids[i], 1);
        /* values and Symbol.iterator one function, toString Array's own */
        uint32_t va = js_atom_ascii(vm, "values");
        uint32_t vf = js_native_new(vm, N_AP_VALUES, va, 0);
        js_define(vm, tp, va, jv_obj(vf), JS_SHAPE_HIDDEN);
        js_define(vm, tp, JS_SYM_KEY | JSYM_ITERATOR, jv_obj(vf), JS_SHAPE_HIDDEN);
        js_define(vm, tp, A_TOSTRING, js_get(vm, jv_obj(vm->array_proto), A_TOSTRING), JS_SHAPE_HIDDEN);
        js_typed_shared(vm);
    }

    /* Symbol: the function, its registry, the well-known ones; its prototype */
    uint32_t sy = ctor(vm, "Symbol", N_SYMBOL, 0, vm->symbol_proto);
    defn(vm, sy, "for", N_SYM_FOR, 1);
    defn(vm, sy, "keyFor", N_SYM_KEYFOR, 1);
    static const char *const wk[JSYM_N] = { "", "iterator", "asyncIterator", "hasInstance", "isConcatSpreadable", "match", "matchAll",
                                            "replace", "search", "species", "split", "toPrimitive", "toStringTag", "unscopables" };
    for (uint32_t i = 1; i < JSYM_N; i++) defv(vm, sy, wk[i], jv_sym(i));
    uint32_t syp = vm->symbol_proto;
    defn(vm, syp, "toString", N_SYMP_TOSTRING, 0);
    defn(vm, syp, "valueOf", N_SYMP_VALUEOF, 0);
    js_define_accessor(vm, syp, A_DESCRIPTION, jv_obj(js_native_new(vm, N_SYMP_DESC, A_DESCRIPTION, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    js_define(vm, syp, JS_SYM_KEY | JSYM_TO_PRIMITIVE, jv_obj(js_native_new(vm, N_SYMP_VALUEOF, js_atom_ascii(vm, "[Symbol.toPrimitive]"), 1)), JS_SHAPE_HIDDEN);
    js_define(vm, syp, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Symbol")), JS_SHAPE_HIDDEN);
    {
        uint32_t oc = jv_idx(js_get(vm, jv_obj(vm->global), js_atom_ascii(vm, "Object")));
        defn(vm, oc, "getOwnPropertySymbols", N_OBJ_GETOWNSYMS, 1);
    }

    /* the iterators' protocol: each iterable's Symbol.iterator, and an iterator its own */
    js_define(vm, vm->iterator_proto, JS_KEY_ITERATOR, jv_obj(js_native_new(vm, N_ITP_SELF, js_str_ascii(vm, "[Symbol.iterator]"), 0)), JS_SHAPE_HIDDEN);
    {
        /* %Iterator%, its from, Iterator.prototype's helpers (its constructor and its tag accessors whose setters let an
           object inheriting them have its own), %IteratorHelperPrototype% and %WrapForValidIteratorPrototype% */
        uint32_t ic = js_native_new(vm, N_ITERATOR, js_atom_ascii(vm, "Iterator"), 0);
        js_define(vm, ic, A_PROTOTYPE, jv_obj(vm->iterator_proto), JS_SHAPE_PROTO);
        js_define(vm, vm->global, js_atom_ascii(vm, "Iterator"), jv_obj(ic), JS_SHAPE_HIDDEN);
        vm->iterator_ctor = ic;
        defn(vm, ic, "from", N_ITER_FROM, 1);
        uint32_t ip = vm->iterator_proto;
        static const char *const hn[] = { "map", "filter", "take", "drop", "flatMap", "reduce", "toArray", "forEach", "some", "every", "find" };
        static const uint32_t hl[] = { 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 };
        for (uint32_t i = 0; i < 11; i++) defn(vm, ip, hn[i], N_ITP_MAP + i, hl[i]);
        js_define_accessor(vm, ip, A_CONSTRUCTOR, jv_obj(js_native_new(vm, N_ITP_CTOR_GET, A_CONSTRUCTOR, 0)),
                           jv_obj(js_native_new(vm, N_ITP_CTOR_SET, A_CONSTRUCTOR, 1)), JS_SHAPE_HIDDEN);
        js_define_accessor(vm, ip, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_obj(js_native_new(vm, N_ITP_TAG_GET, A_EMPTY, 0)),
                           jv_obj(js_native_new(vm, N_ITP_TAG_SET, A_EMPTY, 1)), JS_SHAPE_HIDDEN);
        vm->helper_proto = js_obj_new(vm, JK_PLAIN, ip);
        defn(vm, vm->helper_proto, "next", N_HELPER_NEXT, 0);
        defn(vm, vm->helper_proto, "return", N_HELPER_RETURN, 0);
        js_define(vm, vm->helper_proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Iterator Helper")), JS_SHAPE_HIDDEN | JS_SHAPE_READONLY);
        vm->wrap_proto = js_obj_new(vm, JK_PLAIN, ip);
        defn(vm, vm->wrap_proto, "next", N_WRAP_NEXT, 0);
        defn(vm, vm->wrap_proto, "return", N_WRAP_RETURN, 0);
    }
    js_define(vm, vm->array_proto, JS_KEY_ITERATOR, js_get(vm, jv_obj(vm->array_proto), js_atom_ascii(vm, "values")), JS_SHAPE_HIDDEN);
    vm->array_values = jv_idx(js_get(vm, jv_obj(vm->array_proto), js_atom_ascii(vm, "values")));
    {
        /* the names a with over an array leaves to the scopes outside it (ECMA-262 23.1.3.41) */
        static const char *const unscopable[] = { "at", "copyWithin", "entries", "fill", "find", "findIndex", "findLast",
            "findLastIndex", "flat", "flatMap", "includes", "keys", "toReversed", "toSorted", "toSpliced", "values" };
        uint32_t un = js_obj_new(vm, JK_PLAIN, 0);
        for (size_t i = 0; i < sizeof unscopable / sizeof unscopable[0]; i++) js_define(vm, un, js_atom_ascii(vm, unscopable[i]), JV_TRUE, 0);
        js_define(vm, vm->array_proto, JS_SYM_KEY | JSYM_UNSCOPABLES, jv_obj(un), JS_SHAPE_HIDDEN);
    }
    js_define(vm, vm->map_proto, JS_KEY_ITERATOR, js_get(vm, jv_obj(vm->map_proto), js_atom_ascii(vm, "entries")), JS_SHAPE_HIDDEN);
    js_define(vm, vm->set_proto, JS_KEY_ITERATOR, js_get(vm, jv_obj(vm->set_proto), js_atom_ascii(vm, "values")), JS_SHAPE_HIDDEN);
    js_define(vm, vm->string_proto, JS_KEY_ITERATOR, jv_obj(js_native_new(vm, N_SP_ITER, js_str_ascii(vm, "[Symbol.iterator]"), 0)), JS_SHAPE_HIDDEN);

    /* get [Symbol.species]: the receiver, on each constructor whose methods make their results through it */
    {
        static const char *const sp_ctors[] = { "Array", "Map", "Set", "RegExp", "ArrayBuffer", "Uint8Array" };
        for (size_t i = 0; i < sizeof sp_ctors / sizeof sp_ctors[0]; i++) {
            jv c = js_get(vm, jv_obj(g), js_atom_ascii(vm, sp_ctors[i]));
            if (!jv_is_obj(c)) continue;
            uint32_t co = jv_idx(c);
            /* the typed arrays': %TypedArray%'s, which they inherit */
            if (i == 5) co = vm->objs[co].proto;
            js_define_accessor(vm, co, JS_SYM_KEY | JSYM_SPECIES, jv_obj(js_native_new(vm, N_SPECIES, A_EMPTY, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
        }
    }

    /* generators */
    uint32_t gp = vm->generator_proto;
    defn(vm, gp, "next", N_GEN_NEXT, 1);
    defn(vm, gp, "return", N_GEN_RETURN, 1);
    defn(vm, gp, "throw", N_GEN_THROW, 1);
    js_define(vm, gp, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Generator")), JS_SHAPE_HIDDEN);
    /* async iterators and async generators */
    js_define(vm, vm->async_iterator_proto, JS_KEY_ASYNC_ITERATOR, jv_obj(js_native_new(vm, N_ITP_SELF, js_str_ascii(vm, "[Symbol.asyncIterator]"), 0)), JS_SHAPE_HIDDEN);
    uint32_t agp = vm->async_generator_proto;
    defn(vm, agp, "next", N_AG_NEXT, 1);
    defn(vm, agp, "return", N_AG_RETURN, 1);
    defn(vm, agp, "throw", N_AG_THROW, 1);
    js_define(vm, agp, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "AsyncGenerator")), JS_SHAPE_HIDDEN);

    /* Promise */
    uint32_t prc = ctor(vm, "Promise", N_PROMISE, 1, vm->promise_proto);
    vm->promise_ctor = prc;
    defn(vm, prc, "resolve", N_PR_RESOLVE, 1);
    defn(vm, prc, "reject", N_PR_REJECT, 1);
    defn(vm, prc, "all", N_PR_ALL, 1);
    defn(vm, prc, "allSettled", N_PR_ALLSETTLED, 1);
    defn(vm, prc, "race", N_PR_RACE, 1);
    defn(vm, prc, "any", N_PR_ANY, 1);
    defn(vm, prc, "withResolvers", N_PR_WITHRESOLVERS, 0);
    js_define_accessor(vm, prc, JS_SYM_KEY | JSYM_SPECIES, jv_obj(js_native_new(vm, N_SPECIES, A_EMPTY, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    uint32_t pp = vm->promise_proto;
    defn(vm, pp, "then", N_PRP_THEN, 2);
    defn(vm, pp, "catch", N_PRP_CATCH, 1);
    defn(vm, pp, "finally", N_PRP_FINALLY, 1);
    js_define(vm, pp, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Promise")), JS_SHAPE_HIDDEN);
    {
        uint32_t ae = ctor(vm, "AggregateError", N_AGGREGATE_ERROR, 2, vm->aggregate_error_proto);
        defv(vm, vm->aggregate_error_proto, "name", jv_str(js_atom_ascii(vm, "AggregateError")));
        defv(vm, vm->aggregate_error_proto, "message", jv_str(A_EMPTY));
        vm->objs[ae].proto = vm->error_ctor;
    }
    /* Intl, and the locale-sensitive methods it gives the prototypes made above */
    js_intl_init(vm, N_IN_FIRST);
    /* the promise protector's record, taken last */
    {
        uint32_t pp2 = vm->promise_proto, pc2 = vm->promise_ctor;
        uint32_t ec = js_shape_find(vm, vm->objs[pp2].shape, A_CONSTRUCTOR), es = js_shape_find(vm, vm->objs[pc2].shape, JS_SYM_KEY | JSYM_SPECIES);
        if (ec && es) {
            vm->pr_proto_shape = vm->objs[pp2].shape;
            vm->pr_ctor_shape = vm->objs[pc2].shape;
            vm->pr_ctor_slot = vm->shapes[ec].slot;
            vm->pr_species_slot = vm->shapes[es].slot;
            vm->pr_species_acc = jv_idx(vm->slots[vm->objs[pc2].slots + vm->pr_species_slot]);
            vm->pr_species_get = jv_idx(vm->objs[vm->pr_species_acc].x);
            vm->pr_root_shape = js_shape_of(vm, pp2);
        }
    }
    /* and the regexp protector's */
    js_re_guard(vm);
}
