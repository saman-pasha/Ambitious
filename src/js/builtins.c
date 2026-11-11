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
    N_AP_ENTRIES, N_AP_FLAT, N_AP_FLATMAP, N_AP_AT, N_AP_TOSTRING,
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
    N_EP_TOSTRING, N_DATE_NOW, N_DP_GETTIME, N_DP_TOISO, N_DP_GETFULLYEAR, N_DP_GETMONTH, N_DP_GETDATE, N_DP_GETDAY,
    N_DP_GETHOURS, N_DP_GETMINUTES, N_DP_GETSECONDS, N_DP_GETMS, N_DP_TOSTRING, N_PERF_NOW,
    N_SET_TIMEOUT, N_SET_INTERVAL, N_CLEAR_TIMEOUT, N_QUEUE_MICROTASK,
    /* Map, Set, RegExp, iterators */
    N_MP_GET, N_MP_SET, N_MP_HAS, N_MP_DELETE, N_MP_CLEAR, N_MP_SIZE, N_MP_FOREACH, N_MP_KEYS, N_MP_VALUES, N_MP_ENTRIES,
    N_SETP_ADD, N_SETP_FOREACH, N_SETP_VALUES, N_SETP_ENTRIES, N_ITP_NEXT,
    /* Symbol, generators, promises */
    N_SYM_FOR, N_SYM_KEYFOR, N_SYMP_TOSTRING, N_SYMP_VALUEOF, N_SYMP_DESC, N_OBJ_GETOWNSYMS, N_ITP_SELF,
    N_GEN_NEXT, N_GEN_RETURN, N_GEN_THROW,
    N_AG_NEXT, N_AG_RETURN, N_AG_THROW, N_AG_RETURN_OK, N_AG_RETURN_ERR, N_AFS_UNWRAP, N_AFS_CLOSE,
    N_PROMISE, N_PR_RESOLVE_FN, N_PR_REJECT_FN, N_PRP_THEN, N_PRP_CATCH, N_PRP_FINALLY, N_PR_FIN_OK, N_PR_FIN_ERR,
    N_PR_THUNK_VALUE, N_PR_THUNK_THROW, N_PR_RESOLVE, N_PR_REJECT, N_PR_ALL, N_PR_ALLSETTLED, N_PR_RACE, N_PR_ANY,
    N_PR_ELEM_OK, N_PR_ELEM_ERR, N_AGGREGATE_ERROR, N_SP_ITER, N_SP_MATCHALL,
    N_OBJ_HASOWN, N_OBJ_ISEXT, N_OBJ_ISSEALED, N_OBJ_GETOWNDESCS, N_OBJ_GROUPBY, N_MAP_GROUPBY,
    /* RegExp's natives (src/js/regexp.c), JRE_N of them; Proxy's, Reflect's and the weak
       collections' (src/js/proxy.c), JPX_N */
    N_RE_FIRST,
    N_PX_FIRST = N_RE_FIRST + JRE_N,
    N_TA_FIRST = N_PX_FIRST + JPX_N,
    N_BG_FIRST = N_TA_FIRST + JTN_N,
    N_LAST = N_BG_FIRST + JBG_N
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
    js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_HIDDEN);
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
    uint32_t keys = js_array_new(vm, 0);
    js_own_keys(vm, o, 0, keys);
    uint32_t vals = js_array_new(vm, 0);
    for (uint32_t k = 0; k < vm->objs[keys].b; k++) {
        jv v;
        uint32_t f;
        js_get_own(vm, o, js_atom(vm, jv_idx(js_array_get(vm, keys, k))), &v, &f);
        js_array_push(vm, vals, v);
        js_array_push(vm, vals, jv_num(f));
    }
    vm->objs[o].proto = p;
    vm->objs[o].shape = js_shape_of(vm, p);
    vm->objs[o].cap = 0;
    for (uint32_t k = 0; k < vm->objs[keys].b; k++)
        js_define(vm, o, js_atom(vm, jv_idx(js_array_get(vm, keys, k))), js_array_get(vm, vals, 2 * k), (uint32_t)jv_dbl(js_array_get(vm, vals, 2 * k + 1)));
}

static double len_of(js_vm *vm, jv o) {
    if (jv_is_obj(o) && (vm->objs[jv_idx(o)].kind == JK_ARRAY || vm->objs[jv_idx(o)].kind == JK_TYPEDARRAY)) return vm->objs[jv_idx(o)].b;
    jv n = js_get(vm, o, A_LENGTH);
    if (vm->throwing) return 0;
    double d = js_to_number(vm, n);
    return d != d || d < 0 ? 0 : trunc(d);
}
static jv elem_of(js_vm *vm, jv o, uint32_t i) {
    if (jv_is_obj(o) && vm->objs[jv_idx(o)].kind == JK_ARRAY) return js_array_get(vm, jv_idx(o), i);
    if (jv_is_obj(o) && vm->objs[jv_idx(o)].kind == JK_TYPEDARRAY) return i < vm->objs[jv_idx(o)].b ? js_ta_get(vm, jv_idx(o), i) : JV_UNDEF;
    return js_get_value(vm, o, jv_num((double)i));
}
/* an array as the engine keeps one (the natives' fast paths) */
static int is_array(js_vm *vm, jv v) { return jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_ARRAY; }
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
static int cased(uint32_t c) { return js_uni_lower(c) != c || js_uni_upper(c) != c; }

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
            if (!upper && c == 0x3A3) {
                /* a capital sigma after a cased letter and before none is a word's final one */
                int before = i > 0 && cased(vm->units[o + i - 1]);
                int after = i + 1 < n && cased(vm->units[o + i + 1]);
                if (before && !after) out[0] = 0x3C2;
            }
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

/* JSON.parse: a reader over the string's units */
typedef struct jp { uint32_t off, n, i; int err; } jp;

static void jp_ws(js_vm *vm, jp *p) { while (p->i < p->n && (vm->units[p->off + p->i] == ' ' || vm->units[p->off + p->i] == '\t' || vm->units[p->off + p->i] == '\n' || vm->units[p->off + p->i] == '\r')) p->i++; }

static jv jp_value(js_vm *vm, jp *p, int depth);

static uint32_t jp_string(js_vm *vm, jp *p) {
    vector<uint16_t> u;
    p->i++;
    for (;;) {
        if (p->i >= p->n) { p->err = 1; return A_EMPTY; }
        uint16_t c = vm->units[p->off + p->i];
        if (c == '"') { p->i++; break; }
        if (c < 0x20) { p->err = 1; return A_EMPTY; }
        if (c == '\\') {
            if (p->i + 1 >= p->n) { p->err = 1; return A_EMPTY; }
            uint16_t d = vm->units[p->off + p->i + 1];
            p->i += 2;
            if (d == 'n') push_back(u, (uint16_t)'\n');
            else if (d == 't') push_back(u, (uint16_t)'\t');
            else if (d == 'r') push_back(u, (uint16_t)'\r');
            else if (d == 'b') push_back(u, (uint16_t)'\b');
            else if (d == 'f') push_back(u, (uint16_t)'\f');
            else if (d == '"' || d == '\\' || d == '/') push_back(u, d);
            else if (d == 'u') {
                uint32_t cp = 0;
                for (int k = 0; k < 4; k++) {
                    if (p->i >= p->n) { p->err = 1; return A_EMPTY; }
                    uint16_t h = vm->units[p->off + p->i];
                    int x = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (x < 0) { p->err = 1; return A_EMPTY; }
                    cp = cp * 16 + (uint32_t)x;
                    p->i++;
                }
                push_back(u, (uint16_t)cp);
            } else { p->err = 1; return A_EMPTY; }
            continue;
        }
        push_back(u, c);
        p->i++;
    }
    return js_str_new(vm, data(u), len(u));
}

static jv jp_value(js_vm *vm, jp *p, int depth) {
    jp_ws(vm, p);
    if (p->i >= p->n || depth > 500) { p->err = 1; return JV_UNDEF; }
    uint16_t c = vm->units[p->off + p->i];
    if (c == '{') {
        p->i++;
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        jp_ws(vm, p);
        if (p->i < p->n && vm->units[p->off + p->i] == '}') { p->i++; return jv_obj(o); }
        for (;;) {
            jp_ws(vm, p);
            if (p->i >= p->n || vm->units[p->off + p->i] != '"') { p->err = 1; return JV_UNDEF; }
            uint32_t k = jp_string(vm, p);
            jp_ws(vm, p);
            if (p->err || p->i >= p->n || vm->units[p->off + p->i] != ':') { p->err = 1; return JV_UNDEF; }
            p->i++;
            jv v = jp_value(vm, p, depth + 1);
            if (p->err) return JV_UNDEF;
            js_set_value(vm, jv_obj(o), jv_str(k), v);
            jp_ws(vm, p);
            if (p->i < p->n && vm->units[p->off + p->i] == ',') { p->i++; continue; }
            if (p->i < p->n && vm->units[p->off + p->i] == '}') { p->i++; return jv_obj(o); }
            p->err = 1;
            return JV_UNDEF;
        }
    }
    if (c == '[') {
        p->i++;
        uint32_t a = js_array_new(vm, 0);
        jp_ws(vm, p);
        if (p->i < p->n && vm->units[p->off + p->i] == ']') { p->i++; return jv_obj(a); }
        for (;;) {
            jv v = jp_value(vm, p, depth + 1);
            if (p->err) return JV_UNDEF;
            js_array_push(vm, a, v);
            jp_ws(vm, p);
            if (p->i < p->n && vm->units[p->off + p->i] == ',') { p->i++; continue; }
            if (p->i < p->n && vm->units[p->off + p->i] == ']') { p->i++; return jv_obj(a); }
            p->err = 1;
            return JV_UNDEF;
        }
    }
    if (c == '"') return jv_str(jp_string(vm, p));
    if (c == 't' && p->i + 4 <= p->n && vm->units[p->off + p->i + 1] == 'r') { p->i += 4; return JV_TRUE; }
    if (c == 'f' && p->i + 5 <= p->n && vm->units[p->off + p->i + 1] == 'a') { p->i += 5; return JV_FALSE; }
    if (c == 'n' && p->i + 4 <= p->n && vm->units[p->off + p->i + 1] == 'u') { p->i += 4; return JV_NULL; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char b[64];
        uint32_t k = 0;
        while (p->i < p->n && k + 1 < sizeof b) {
            uint16_t d = vm->units[p->off + p->i];
            if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') { b[k++] = (char)d; p->i++; }
            else break;
        }
        b[k] = 0;
        return jv_num(strtod(b, 0));
    }
    p->err = 1;
    return JV_UNDEF;
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

static uint32_t this_coll(js_vm *vm, jv thisv, uint32_t kind) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != kind) {
        js_throw_msg(vm, vm->type_error_proto, kind == JK_MAP ? "a Map method called on something not a Map" : "a Set method called on something not a Set");
        return 0;
    }
    return jv_idx(thisv);
}

/* ---- dates: virtual milliseconds since the epoch, UTC ---- */

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

static double this_time(js_vm *vm, jv thisv) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_DATE) { js_throw_msg(vm, vm->type_error_proto, "this is not a Date object"); return NAN; }
    return jv_dbl(vm->objs[jv_idx(thisv)].x);
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
        jv v = elem_of(vm, src, i);
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

uint32_t js_promise_of(js_vm *vm, jv v) {
    if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_PROMISE) return jv_idx(v);
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
        uint32_t d = jv_is_obj(j.target) ? jv_idx(j.target) : 0;
        if (js_is_callable(vm, j.fn)) {
            jv a1[1] = { j.arg };
            jv r = js_call(vm, j.fn, JV_UNDEF, a1, 1);
            if (vm->throwing == 2) return;
            if (vm->throwing) {
                jv e = vm->exception;
                vm->throwing = 0;
                vm->exception = JV_UNDEF;
                if (d) js_promise_settle(vm, d, 1, e);
                else js_report_error(vm, e, JS_PHASE_PROMISE);
                return;
            }
            if (d) js_promise_resolve_with(vm, d, r);
        } else if (d) {
            if (j.mode) js_promise_settle(vm, d, 1, j.arg);
            else js_promise_resolve_with(vm, d, j.arg);
        }
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
        run_job(vm, j);
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
    if (!js_is_callable(vm, m)) { js_type_error(vm, "is not a function", js_typeof(vm, m)); return -1; }
    jv a1[1] = { arg };
    *out = js_call(vm, m, it, a1, kind == 3 || kind == 4 ? 0 : 1);
    return vm->throwing ? -1 : 1;
}

/* a combinator's element: its function's record [aggregate, index, called] */
static jv promise_elem_fn(js_vm *vm, uint32_t id, uint32_t agg, uint32_t index) {
    uint32_t f = js_native_new(vm, id, A_EMPTY, 1);
    uint32_t rec = js_obj_new(vm, JK_PLAIN, 0);
    vm->objs[rec].a = agg;
    vm->objs[rec].b = index;
    vm->objs[rec].c = 0;
    vm->objs[f].x = jv_obj(rec);
    return jv_obj(f);
}
/* the aggregate's values array, made with the count still waiting; done once all came */
static void promise_agg_done(js_vm *vm, uint32_t agg) {
    uint32_t kind = vm->objs[agg].c >> 8, p = (uint32_t)jv_idx(vm->objs[agg].x);
    if (kind == 2) {
        /* any: every one rejected */
        uint32_t e = js_error_new(vm, vm->aggregate_error_proto, js_str_ascii(vm, "All promises were rejected"));
        js_define(vm, e, A_ERRORS, jv_obj(vm->objs[agg].a), JS_SHAPE_HIDDEN);
        js_promise_settle(vm, p, 1, jv_obj(e));
    } else js_promise_settle(vm, p, 0, jv_obj(vm->objs[agg].a));
}

static jv iter_of_coll(js_vm *vm, uint32_t coll, uint32_t mode) {
    uint32_t it = js_obj_new(vm, JK_ITER, vm->iterator_proto);
    vm->objs[it].a = coll;
    vm->objs[it].c = mode;
    return jv_obj(it);
}

jv js_native_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    (void)callee;
    jv a0 = arg(vm, args, argc, 0), a1 = arg(vm, args, argc, 1);
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
        if (!jv_is_sym(a0)) return js_type_error(vm, "is not a symbol", js_typeof(vm, a0));
        uint32_t r = vm->syms[jv_idx(a0)].reg;
        return r ? jv_str(r - 1) : JV_UNDEF;
    }
    case N_SYMP_TOSTRING: case N_SYMP_VALUEOF: case N_SYMP_DESC: {
        jv y = thisv;
        if (jv_is_obj(y) && vm->objs[jv_idx(y)].kind == JK_SYMBOL) y = vm->objs[jv_idx(y)].x;
        if (!jv_is_sym(y)) return js_throw_msg(vm, vm->type_error_proto, "this is not a Symbol");
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
        uint32_t it = js_obj_new(vm, JK_ITER, vm->iterator_proto);
        vm->objs[it].a = st;
        vm->objs[it].c = JI_STRING;
        return jv_obj(it);
    }
    /* ---- generators ---- */
    case N_GEN_NEXT: case N_GEN_RETURN: case N_GEN_THROW: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_GENERATOR)
            return js_throw_msg(vm, vm->type_error_proto, "next, return or throw called on something not a generator");
        int done = 0;
        jv r = js_gen_resume(vm, jv_idx(thisv), a0, id == N_GEN_NEXT ? 0 : id == N_GEN_THROW ? 1 : 2, &done);
        if (vm->throwing) return JV_HOLE;
        return iter_result(vm, r, done);
    }
    /* ---- async generators ---- */
    case N_AG_NEXT: case N_AG_RETURN: case N_AG_THROW: {
        uint32_t p = js_promise_new(vm);
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ASYNCGEN) {
            uint32_t m = js_str_ascii(vm, "next, return or throw called on something not an async generator");
            js_promise_settle(vm, p, 1, jv_obj(js_error_new(vm, vm->type_error_proto, m)));
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
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function: Promise resolver", js_typeof(vm, a0));
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
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_PROMISE) return js_throw_msg(vm, vm->type_error_proto, "then called on something not a Promise");
        uint32_t d = js_promise_new(vm);
        js_promise_react(vm, jv_idx(thisv), js_is_callable(vm, a0) ? a0 : JV_UNDEF, js_is_callable(vm, a1) ? a1 : JV_UNDEF, jv_obj(d), 0);
        return jv_obj(d);
    }
    case N_PRP_CATCH: case N_PRP_FINALLY: {
        jv then = js_get(vm, thisv, A_THEN);
        if (vm->throwing) return JV_HOLE;
        if (!js_is_callable(vm, then)) return js_throw_msg(vm, vm->type_error_proto, "catch or finally called on something without then");
        jv args2[2] = { JV_UNDEF, a0 };
        if (id == N_PRP_FINALLY && js_is_callable(vm, a0)) {
            uint32_t ok = js_native_new(vm, N_PR_FIN_OK, A_EMPTY, 1), er = js_native_new(vm, N_PR_FIN_ERR, A_EMPTY, 1);
            vm->objs[ok].x = a0;
            vm->objs[er].x = a0;
            args2[0] = jv_obj(ok);
            args2[1] = jv_obj(er);
        } else if (id == N_PRP_FINALLY) args2[0] = a0;
        return js_call(vm, then, thisv, args2, 2);
    }
    case N_PR_FIN_OK: case N_PR_FIN_ERR: {
        /* finally's handler: its function run, then the value (or the reason) passed on
           once what that returned has settled */
        jv f = vm->objs[jv_idx(callee)].x;
        jv r = js_call(vm, f, JV_UNDEF, 0, 0);
        if (vm->throwing) return JV_HOLE;
        uint32_t pr = js_promise_of(vm, r);
        uint32_t th = js_native_new(vm, id == N_PR_FIN_OK ? N_PR_THUNK_VALUE : N_PR_THUNK_THROW, A_EMPTY, 0);
        vm->objs[th].x = a0;
        uint32_t d = js_promise_new(vm);
        js_promise_react(vm, pr, jv_obj(th), JV_UNDEF, jv_obj(d), 0);
        return jv_obj(d);
    }
    case N_PR_THUNK_VALUE: return vm->objs[jv_idx(callee)].x;
    case N_PR_THUNK_THROW: return js_throw(vm, vm->objs[jv_idx(callee)].x);
    case N_PR_RESOLVE: {
        if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_PROMISE) return a0;
        return jv_obj(js_promise_of(vm, a0));
    }
    case N_PR_REJECT: {
        uint32_t p = js_promise_new(vm);
        js_promise_settle(vm, p, 1, a0);
        return jv_obj(p);
    }
    case N_PR_ALL: case N_PR_ALLSETTLED: case N_PR_RACE: case N_PR_ANY: {
        uint32_t p = js_promise_new(vm);
        uint32_t items = js_array_new(vm, 0);
        if (!iterate_into(vm, a0, items)) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_promise_settle(vm, p, 1, e);
            return jv_obj(p);
        }
        uint32_t n = vm->objs[items].b;
        jv res, rej;
        promise_resolvers(vm, p, &res, &rej);
        if (id == N_PR_RACE) {
            for (uint32_t i = 0; i < n; i++) js_promise_react(vm, js_promise_of(vm, js_array_get(vm, items, i)), res, rej, JV_UNDEF, 0);
            return jv_obj(p);
        }
        /* the aggregate: a the values, b how many are still waiting, c its kind, x the promise */
        uint32_t agg = js_obj_new(vm, JK_PLAIN, 0);
        uint32_t vals = js_array_new(vm, n);
        for (uint32_t i = 0; i < n; i++) vm->slots[vm->objs[vals].a + i] = JV_UNDEF;
        vm->objs[agg].a = vals;
        vm->objs[agg].b = n;
        vm->objs[agg].c = (id == N_PR_ALL ? 0u : id == N_PR_ALLSETTLED ? 1u : 2u) << 8;
        vm->objs[agg].x = jv_obj(p);
        if (n == 0) { promise_agg_done(vm, agg); return jv_obj(p); }
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pi = js_promise_of(vm, js_array_get(vm, items, i));
            jv ok = id == N_PR_ANY ? res : promise_elem_fn(vm, N_PR_ELEM_OK, agg, i);
            jv er = id == N_PR_ALL ? rej : promise_elem_fn(vm, N_PR_ELEM_ERR, agg, i);
            js_promise_react(vm, pi, ok, er, JV_UNDEF, 0);
        }
        return jv_obj(p);
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
        return JV_UNDEF;
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

    case N_DATE: {
        double t = vm->now;
        if (argc == 1 && jv_is_num(a0)) t = jv_dbl(a0);
        else if (argc >= 2) {
            double y = num_arg(vm, args, argc, 0), m = num_arg(vm, args, argc, 1), d = argc > 2 ? num_arg(vm, args, argc, 2) : 1;
            double h = argc > 3 ? num_arg(vm, args, argc, 3) : 0, mi = argc > 4 ? num_arg(vm, args, argc, 4) : 0, s = argc > 5 ? num_arg(vm, args, argc, 5) : 0;
            double ms = argc > 6 ? num_arg(vm, args, argc, 6) : 0;
            int64_t yy = (int64_t)y + (int64_t)floor(m / 12);
            unsigned mm = (unsigned)((int64_t)m - (int64_t)floor(m / 12) * 12) + 1;
            t = (double)days_from_civil(yy, mm, 1) * 86400000.0 + (d - 1) * 86400000.0 + h * 3600000.0 + mi * 60000.0 + s * 1000.0 + ms;
        } else if (argc == 1) t = NAN;
        if (newt == JV_UNDEF || newt == JV_HOLE) {
            char b[64];
            double tt = t;
            snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)date_field(tt, 0), (int)date_field(tt, 1) + 1, (int)date_field(tt, 2),
                     (int)date_field(tt, 4), (int)date_field(tt, 5), (int)date_field(tt, 6), (int)date_field(tt, 7));
            return jv_str(js_str_ascii(vm, b));
        }
        uint32_t o = js_obj_new(vm, JK_DATE, vm->date_proto);
        vm->objs[o].x = jv_num(t);
        return jv_obj(o);
    }
    case N_MAP: case N_SET: {
        uint32_t kind = id == N_MAP ? JK_MAP : JK_SET;
        uint32_t c = coll_new(vm, kind, kind == JK_MAP ? vm->map_proto : vm->set_proto);
        if (!jv_is_nullish(a0)) {
            uint32_t items = js_array_new(vm, 0);
            if (!iterate_into(vm, a0, items)) return JV_HOLE;
            uint32_t n = vm->objs[items].b;
            for (uint32_t i = 0; i < n && !vm->throwing; i++) {
                jv v = js_array_get(vm, items, i);
                if (kind == JK_MAP) {
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
            uint32_t keys = js_array_new(vm, 0);
            js_own_keys(vm, jv_idx(so), 1, keys);
            for (uint32_t k = 0; k < vm->objs[keys].b && !vm->throwing; k++) {
                uint32_t atom = js_atom(vm, jv_idx(js_array_get(vm, keys, k)));
                js_set(vm, t, atom, js_get(vm, so, atom));
            }
        }
        return t;
    }
    case N_OBJ_CREATE: {
        if (!jv_is_obj(a0) && a0 != JV_NULL) return js_throw_msg(vm, vm->type_error_proto, "Object prototype may only be an Object or null");
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
        if (!jv_is_obj(a0)) return a0;
        if (!jv_is_obj(a1) && a1 != JV_NULL) return js_throw_msg(vm, vm->type_error_proto, "Object prototype may only be an Object or null");
        if (vm->objs[jv_idx(a0)].kind == JK_PROXY) {
            jv r = js_proxy_native(vm, JPX_R_SETPROTO, callee, thisv, args, argc, newt);
            if (vm->throwing) return JV_HOLE;
            if (r == JV_FALSE) return js_throw_msg(vm, vm->type_error_proto, "'setPrototypeOf' on proxy: trap returned falsish");
            return a0;
        }
        js_set_proto(vm, jv_idx(a0), a1 == JV_NULL ? 0 : jv_idx(a1));
        return a0;
    }
    case N_OBJ_DEFPROP: case N_OBJ_DEFPROPS: {
        if (!jv_is_obj(a0)) return js_throw_msg(vm, vm->type_error_proto, "Object.defineProperty called on non-object");
        if (id == N_OBJ_DEFPROP && vm->objs[jv_idx(a0)].kind == JK_PROXY) {
            jv r = js_proxy_native(vm, JPX_R_DEFINE, callee, thisv, args, argc, newt);
            if (vm->throwing) return JV_HOLE;
            if (r == JV_FALSE) return js_throw_msg(vm, vm->type_error_proto, "'defineProperty' on proxy: trap returned falsish");
            return a0;
        }
        uint32_t o = jv_idx(a0);
        uint32_t keys = js_array_new(vm, 0);
        jv descs = id == N_OBJ_DEFPROPS ? a1 : JV_UNDEF;
        if (id == N_OBJ_DEFPROP) {
            int kok = 1;
            uint32_t key = js_prop_key(vm, a1, &kok);
            if (!kok) return JV_HOLE;
            js_array_push(vm, keys, js_key_is_sym(key) ? jv_sym(key & ~JS_SYM_KEY) : jv_str(key));
        } else if (jv_is_obj(descs)) { js_own_keys(vm, jv_idx(descs), 1, keys); js_own_symbols(vm, jv_idx(descs), keys); }
        for (uint32_t k = 0; k < vm->objs[keys].b && !vm->throwing; k++) {
            jv kv = js_array_get(vm, keys, k);
            uint32_t atom = jv_is_sym(kv) ? (JS_SYM_KEY | jv_idx(kv)) : js_atom(vm, jv_idx(kv));
            jv d = id == N_OBJ_DEFPROP ? arg(vm, args, argc, 2) : js_get(vm, descs, atom);
            if (!jv_is_obj(d)) return js_throw_msg(vm, vm->type_error_proto, "Property description must be an object");
            uint32_t flags = js_truthy(vm, js_get(vm, d, A_ENUMERABLE)) ? 0 : JS_SHAPE_HIDDEN;
            jv g = js_get(vm, d, A_GET), s = js_get(vm, d, A_SET);
            if (g != JV_UNDEF || s != JV_UNDEF) js_define_accessor(vm, o, atom, g == JV_UNDEF ? JV_HOLE : g, s == JV_UNDEF ? JV_HOLE : s, flags);
            else if (vm->objs[o].kind == JK_ARRAY) js_set(vm, a0, atom, js_get(vm, d, A_VALUE));
            else js_define(vm, o, atom, js_get(vm, d, A_VALUE), flags);
        }
        return a0;
    }
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
        if (!js_is_callable(vm, a1)) return js_type_error(vm, "is not a function", js_typeof(vm, a1));
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
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        jv src = a0;
        if (jv_is_obj(src) && vm->objs[jv_idx(src)].kind == JK_MAP) src = jv_obj(vm->objs[jv_idx(src)].a);
        if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_MAP) {
            uint32_t arr = vm->objs[jv_idx(a0)].a;
            for (uint32_t i = 0; i + 1 < vm->objs[arr].b; i += 2) {
                jv k = vm->slots[vm->objs[arr].a + i];
                if (k == JV_HOLE) continue;
                js_set_value(vm, jv_obj(o), k, vm->slots[vm->objs[arr].a + i + 1]);
            }
            return jv_obj(o);
        }
        uint32_t n = (uint32_t)len_of(vm, src);
        for (uint32_t i = 0; i < n && !vm->throwing; i++) {
            jv e = elem_of(vm, src, i);
            js_set_value(vm, jv_obj(o), js_get_value(vm, e, jv_num(0)), js_get_value(vm, e, jv_num(1)));
        }
        return jv_obj(o);
    }
    case N_OBJ_GETOWNDESC: {
        if (!jv_is_obj(a0)) return JV_UNDEF;
        int kok = 1;
        uint32_t atom = js_prop_key(vm, a1, &kok);
        if (!kok) return JV_HOLE;
        jv v;
        uint32_t f;
        if (!js_get_own(vm, jv_idx(a0), atom, &v, &f)) {
            if (vm->objs[jv_idx(a0)].kind == JK_ARRAY || vm->objs[jv_idx(a0)].kind == JK_STRING) {
                jv x = js_get(vm, a0, atom);
                if (x == JV_UNDEF) return JV_UNDEF;
                v = x;
                f = 0;
            } else return JV_UNDEF;
        }
        uint32_t d = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        if (f & JS_SHAPE_ACCESSOR) {
            js_define(vm, d, A_GET, vm->objs[jv_idx(v)].x, 0);
            js_define(vm, d, A_SET, vm->objs[jv_idx(v)].a ? jv_obj(vm->objs[jv_idx(v)].a) : JV_UNDEF, 0);
        } else {
            js_define(vm, d, A_VALUE, v, 0);
            js_define(vm, d, A_WRITABLE, JV_TRUE, 0);
        }
        js_define(vm, d, A_ENUMERABLE, jv_bool(!(f & JS_SHAPE_HIDDEN)), 0);
        js_define(vm, d, A_CONFIGURABLE, JV_TRUE, 0);
        return jv_obj(d);
    }
    case N_OBJ_IS: {
        if (jv_is_num(a0) && jv_is_num(a1)) {
            double x = jv_dbl(a0), y = jv_dbl(a1);
            if (x != x && y != y) return JV_TRUE;
            return jv_bool(x == y && signbit(x) == signbit(y));
        }
        return jv_bool(js_strict_eq(vm, a0, a1));
    }
    case N_OP_HASOWN: {
        jv o = js_to_object(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        int kok = 1;
        uint32_t atom = js_prop_key(vm, a0, &kok), ob = jv_idx(o);
        if (!kok) return JV_HOLE;
        jv v;
        uint32_t f;
        if (js_get_own(vm, ob, atom, &v, &f)) return JV_TRUE;
        uint32_t k = vm->objs[ob].kind;
        uint32_t i;
        if (k == JK_ARRAY) return jv_bool(atom == A_LENGTH || (js_atom_index(vm, atom, &i) && i < vm->objs[ob].b && vm->slots[vm->objs[ob].a + i] != JV_HOLE));
        if (k == JK_STRING) return jv_bool(atom == A_LENGTH || (js_atom_index(vm, atom, &i) && i < vm->strs[jv_idx(vm->objs[ob].x)].len));
        return JV_FALSE;
    }
    case N_OP_TOSTRING: case N_OP_TOLOCALE: {
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
        if (!jv_is_obj(thisv)) return JV_FALSE;
        int kok = 1;
        uint32_t key = js_prop_key(vm, a0, &kok);
        if (!kok) return JV_HOLE;
        jv v;
        uint32_t f;
        if (js_get_own(vm, jv_idx(thisv), key, &v, &f)) return jv_bool(!(f & JS_SHAPE_HIDDEN));
        return jv_bool(vm->objs[jv_idx(thisv)].kind == JK_ARRAY);
    }
    /* ---- Function ---- */
    case N_FP_CALL: {
        if (!js_is_callable(vm, thisv)) return js_type_error(vm, "is not a function", A_FUNCTION);
        vector<jv> av;
        for (uint32_t i = 1; i < argc; i++) push_back(av, arg(vm, args, argc, i));
        return js_call(vm, thisv, a0, data(av), (uint32_t)len(av));
    }
    case N_FP_APPLY: {
        if (!js_is_callable(vm, thisv)) return js_type_error(vm, "is not a function", A_FUNCTION);
        vector<jv> av;
        if (!jv_is_nullish(a1)) {
            uint32_t n = (uint32_t)len_of(vm, a1);
            for (uint32_t i = 0; i < n && !vm->throwing; i++) push_back(av, elem_of(vm, a1, i));
        }
        if (vm->throwing) return JV_HOLE;
        return js_call(vm, thisv, a0, data(av), (uint32_t)len(av));
    }
    case N_FP_BIND: {
        if (!js_is_callable(vm, thisv)) return js_type_error(vm, "is not a function", A_FUNCTION);
        uint32_t b = js_obj_new(vm, JK_BOUND, vm->function_proto);
        vm->objs[b].a = jv_idx(thisv);
        vm->objs[b].x = a0;
        uint32_t n = argc > 1 ? argc - 1 : 0;
        uint32_t off = (uint32_t)len(vm->slots);
        for (uint32_t i = 0; i < n; i++) push_back(vm->slots, arg(vm, args, argc, i + 1));
        vm->objs[b].b = off;
        vm->objs[b].c = n;
        return jv_obj(b);
    }
    case N_FP_TOSTRING: {
        uint32_t name = A_EMPTY;
        if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_CLOSURE) name = vm->funcs[vm->objs[jv_idx(thisv)].a].name;
        else if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_NATIVE) name = vm->objs[jv_idx(thisv)].c;
        uint32_t s = js_str_concat(vm, js_str_ascii(vm, "function "), js_str_concat(vm, name ? name : A_EMPTY, js_str_ascii(vm, "() { [code] }")));
        return jv_str(s);
    }
    /* ---- Array ---- */
    case N_ARR_ISARRAY: return jv_bool(is_array_spec(vm, a0));
    case N_ARR_FROM: {
        uint32_t out = js_array_new(vm, 0);
        if (jv_is_nullish(a0)) return js_throw_msg(vm, vm->type_error_proto, "Array.from needs an iterable or an array-like object");
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
                uint32_t w = c >= 0xD800 && c <= 0xDBFF && i + 1 < vm->strs[s].len ? 2 : 1;
                js_array_push(vm, out, jv_str(js_str_sub(vm, s, i, i + w)));
                i += w - 1;
            }
        } else {
            uint32_t n = (uint32_t)len_of(vm, src);
            for (uint32_t i = 0; i < n && !vm->throwing; i++) js_array_push(vm, out, elem_of(vm, src, i));
        }
        if (js_is_callable(vm, a1)) {
            for (uint32_t i = 0; i < vm->objs[out].b && !vm->throwing; i++) {
                jv r = callback(vm, a1, JV_UNDEF, js_array_get(vm, out, i), jv_num(i), JV_UNDEF);
                js_array_set(vm, out, i, r);
            }
        }
        return vm->throwing ? JV_HOLE : jv_obj(out);
    }
    case N_ARR_OF: {
        uint32_t a = js_array_new(vm, 0);
        for (uint32_t i = 0; i < argc; i++) js_array_push(vm, a, arg(vm, args, argc, i));
        return jv_obj(a);
    }
    case N_AP_PUSH: {
        if (is_array(vm, thisv)) {
            uint32_t a = jv_idx(thisv);
            if (vm->objs[a].flags & JO_FROZEN) return js_throw_msg(vm, vm->type_error_proto, "Cannot add property, object is not extensible");
            for (uint32_t i = 0; i < argc; i++) js_array_push(vm, a, arg(vm, args, argc, i));
            return jv_num(vm->objs[a].b);
        }
        double n = len_of(vm, thisv);
        for (uint32_t i = 0; i < argc; i++) js_set_value(vm, thisv, jv_num(n + i), arg(vm, args, argc, i));
        js_set(vm, thisv, A_LENGTH, jv_num(n + argc));
        return jv_num(n + argc);
    }
    case N_AP_POP: case N_AP_SHIFT: {
        if (!is_array(vm, thisv)) return JV_UNDEF;
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
    case N_AP_UNSHIFT: {
        if (!is_array(vm, thisv)) return JV_UNDEF;
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
        js_array_set_length(vm, a, n + argc);
        for (uint32_t i = n; i > 0; i--) vm->slots[vm->objs[a].a + i - 1 + argc] = vm->slots[vm->objs[a].a + i - 1];
        for (uint32_t i = 0; i < argc; i++) vm->slots[vm->objs[a].a + i] = arg(vm, args, argc, i);
        return jv_num(n + argc);
    }
    case N_AP_SLICE: {
        double n = len_of(vm, thisv);
        uint32_t from = rel_index(int_arg(vm, args, argc, 0, 0), (uint32_t)n), to = rel_index(int_arg(vm, args, argc, 1, n), (uint32_t)n);
        uint32_t out = js_array_new(vm, 0);
        for (uint32_t i = from; i < to && !vm->throwing; i++) js_array_push(vm, out, elem_of(vm, thisv, i));
        return jv_obj(out);
    }
    case N_AP_SPLICE: {
        if (!is_array(vm, thisv)) return js_throw_msg(vm, vm->type_error_proto, "splice is only for arrays here");
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
        uint32_t start = rel_index(int_arg(vm, args, argc, 0, 0), n);
        double dc = argc < 2 ? (double)(n - start) : int_arg(vm, args, argc, 1, 0);
        if (dc < 0) dc = 0;
        if (dc > n - start) dc = n - start;
        uint32_t del = (uint32_t)dc, ins = argc > 2 ? argc - 2 : 0;
        uint32_t removed = js_array_new(vm, 0);
        for (uint32_t i = 0; i < del; i++) js_array_push(vm, removed, js_array_get(vm, a, start + i));
        vector<jv> tail;
        for (uint32_t i = start + del; i < n; i++) push_back(tail, vm->slots[vm->objs[a].a + i]);
        js_array_set_length(vm, a, start + ins + (uint32_t)len(tail));
        for (uint32_t i = 0; i < ins; i++) vm->slots[vm->objs[a].a + start + i] = arg(vm, args, argc, i + 2);
        for (size_t i = 0; i < len(tail); i++) vm->slots[vm->objs[a].a + start + ins + i] = at(tail, i);
        return jv_obj(removed);
    }
    case N_AP_CONCAT: {
        uint32_t out = js_array_new(vm, 0);
        for (uint32_t k = 0; k <= argc && !vm->throwing; k++) {
            jv v = k == 0 ? thisv : arg(vm, args, argc, k - 1);
            if (is_array(vm, v)) {
                uint32_t n = vm->objs[jv_idx(v)].b;
                for (uint32_t i = 0; i < n; i++) js_array_push(vm, out, vm->slots[vm->objs[jv_idx(v)].a + i]);
            } else js_array_push(vm, out, v);
        }
        return jv_obj(out);
    }
    case N_AP_JOIN: case N_AP_TOSTRING: {
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
    case N_AP_REVERSE: {
        if (!is_array(vm, thisv)) return thisv;
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b, o = vm->objs[a].a;
        for (uint32_t i = 0; i < n / 2; i++) { jv t = vm->slots[o + i]; vm->slots[o + i] = vm->slots[o + n - 1 - i]; vm->slots[o + n - 1 - i] = t; }
        return thisv;
    }
    case N_AP_INDEXOF: case N_AP_LASTINDEXOF: case N_AP_INCLUDES: {
        uint32_t n = (uint32_t)len_of(vm, thisv);
        if (id == N_AP_LASTINDEXOF) {
            int64_t from = argc > 1 ? (int64_t)int_arg(vm, args, argc, 1, 0) : (int64_t)n - 1;
            if (from < 0) from += n;
            if (from >= (int64_t)n) from = (int64_t)n - 1;
            for (int64_t i = from; i >= 0; i--) {
                if (is_array(vm, thisv) && (uint32_t)i < vm->objs[jv_idx(thisv)].b && vm->slots[vm->objs[jv_idx(thisv)].a + (uint32_t)i] == JV_HOLE) continue;
                if (js_strict_eq(vm, elem_of(vm, thisv, (uint32_t)i), a0)) return jv_num((double)i);
            }
            return jv_num(-1);
        }
        uint32_t from = rel_index(int_arg(vm, args, argc, 1, 0), n);
        if (is_array(vm, thisv)) {
            uint32_t a = jv_idx(thisv), o = vm->objs[a].a;
            for (uint32_t i = from; i < n; i++) {
                jv v = vm->slots[o + i];
                if (v == JV_HOLE) {
                    if (id != N_AP_INCLUDES) continue;
                    v = JV_UNDEF;
                }
                if (id == N_AP_INCLUDES ? js_same_value_zero(vm, v, a0) : js_strict_eq(vm, v, a0))
                    return id == N_AP_INCLUDES ? JV_TRUE : jv_num(i);
            }
        } else {
            for (uint32_t i = from; i < n && !vm->throwing; i++) {
                jv v = elem_of(vm, thisv, i);
                if (id == N_AP_INCLUDES ? js_same_value_zero(vm, v, a0) : js_strict_eq(vm, v, a0)) return id == N_AP_INCLUDES ? JV_TRUE : jv_num(i);
            }
        }
        return id == N_AP_INCLUDES ? JV_FALSE : jv_num(-1);
    }
    case N_AP_FIND: case N_AP_FINDINDEX: case N_AP_FINDLAST: case N_AP_FINDLASTINDEX: case N_AP_FILTER: case N_AP_MAP:
    case N_AP_FOREACH: case N_AP_SOME: case N_AP_EVERY: case N_AP_FLATMAP: {
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        uint32_t n = (uint32_t)len_of(vm, thisv);
        uint32_t out = id == N_AP_FILTER || id == N_AP_MAP || id == N_AP_FLATMAP ? js_array_new(vm, 0) : 0;
        int last = id == N_AP_FINDLAST || id == N_AP_FINDLASTINDEX;
        for (uint32_t q = 0; q < n && !vm->throwing; q++) {
            uint32_t i = last ? n - 1 - q : q;
            if (is_array(vm, thisv) && (i >= vm->objs[jv_idx(thisv)].b || (vm->slots[vm->objs[jv_idx(thisv)].a + i] == JV_HOLE && id != N_AP_FIND && id != N_AP_FINDINDEX))) {
                if (id == N_AP_MAP) js_array_push(vm, out, JV_HOLE);
                continue;
            }
            jv v = elem_of(vm, thisv, i);
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
        if (out) return jv_obj(out);
        if (id == N_AP_FINDINDEX || id == N_AP_FINDLASTINDEX) return jv_num(-1);
        if (id == N_AP_SOME) return JV_FALSE;
        if (id == N_AP_EVERY) return JV_TRUE;
        return JV_UNDEF;
    }
    case N_AP_REDUCE: case N_AP_REDUCERIGHT: {
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        uint32_t n = (uint32_t)len_of(vm, thisv);
        int right = id == N_AP_REDUCERIGHT;
        uint32_t q = 0;
        jv acc;
        if (argc > 1) acc = a1;
        else {
            if (!n) return js_throw_msg(vm, vm->type_error_proto, "Reduce of empty array with no initial value");
            acc = elem_of(vm, thisv, right ? n - 1 : 0);
            q = 1;
        }
        for (; q < n && !vm->throwing; q++) {
            uint32_t i = right ? n - 1 - q : q;
            jv args4[4] = { acc, elem_of(vm, thisv, i), jv_num(i), thisv };
            acc = js_call(vm, a0, JV_UNDEF, args4, 4);
        }
        return vm->throwing ? JV_HOLE : acc;
    }
    case N_AP_SORT: {
        if (!is_array(vm, thisv)) return thisv;
        if (a0 != JV_UNDEF && !js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "The comparison function must be either a function or undefined");
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
        for (size_t i = 0; i < len(v); i++) vm->slots[vm->objs[a].a + i] = at(v, i);
        for (uint32_t i = 0; i < holes; i++) vm->slots[vm->objs[a].a + len(v) + i] = JV_HOLE;
        return thisv;
    }
    case N_AP_FILL: {
        if (!is_array(vm, thisv)) return thisv;
        uint32_t a = jv_idx(thisv), n = vm->objs[a].b;
        uint32_t from = rel_index(int_arg(vm, args, argc, 1, 0), n), to = rel_index(int_arg(vm, args, argc, 2, n), n);
        for (uint32_t i = from; i < to; i++) vm->slots[vm->objs[a].a + i] = a0;
        return thisv;
    }
    case N_AP_KEYS: case N_AP_VALUES: case N_AP_ENTRIES: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "not an array");
        uint32_t n = (uint32_t)len_of(vm, thisv);
        uint32_t out = js_array_new(vm, 0);
        for (uint32_t i = 0; i < n && !vm->throwing; i++) {
            if (id == N_AP_KEYS) js_array_push(vm, out, jv_num(i));
            else if (id == N_AP_VALUES) js_array_push(vm, out, elem_of(vm, thisv, i));
            else {
                uint32_t pair = js_array_new(vm, 0);
                js_array_push(vm, pair, jv_num(i));
                js_array_push(vm, pair, elem_of(vm, thisv, i));
                js_array_push(vm, out, jv_obj(pair));
            }
        }
        return iter_of_coll(vm, out, JI_ARRAY);
    }
    case N_AP_FLAT: {
        double depth = argc ? int_arg(vm, args, argc, 0, 1) : 1;
        uint32_t out = js_array_new(vm, 0);
        flat_into(vm, out, thisv, depth);
        return jv_obj(out);
    }
    case N_AP_AT: {
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
        if (len(u) == 1 && at(u, 0) < 128) {
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
    case N_SP_TOSTRING: {
        if (jv_is_str(thisv)) return thisv;
        if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_STRING) return vm->objs[jv_idx(thisv)].x;
        return js_throw_msg(vm, vm->type_error_proto, "String.prototype.toString requires that 'this' be a String");
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
    if (id == N_SP_MATCH) return js_re_native(vm, JRE_SP_MATCH, callee, thisv, args, argc, newt);
    if (id == N_SP_SEARCH) return js_re_native(vm, JRE_SP_SEARCH, callee, thisv, args, argc, newt);
    if (id == N_SP_MATCHALL) return js_re_native(vm, JRE_SP_MATCHALL, callee, thisv, args, argc, newt);
    if ((id == N_SP_SPLIT || id == N_SP_REPLACE || id == N_SP_REPLACEALL) && jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_REGEXP) {
        if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "String.prototype method called on null or undefined");
        uint32_t which = id == N_SP_SPLIT ? JRE_SP_SPLIT : id == N_SP_REPLACE ? JRE_SP_REPLACE : JRE_SP_REPLACEALL;
        return js_re_native(vm, which, callee, thisv, args, argc, newt);
    }
    if (id >= N_SP_CHARAT && id <= N_SP_SEARCH) {
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
            uint32_t out = js_array_new(vm, 0);
            double lim = a1 == JV_UNDEF ? 4294967295.0 : js_to_number(vm, a1);
            if (lim == 0) return jv_obj(out);
            if (a0 == JV_UNDEF) { js_array_push(vm, out, jv_str(s)); return jv_obj(out); }
            uint32_t sep = js_to_string(vm, a0);
            if (!sep) return JV_HOLE;
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
        case N_SP_NORMALIZE: return jv_str(s);
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
        if (!jv_is_num(tv)) return js_throw_msg(vm, vm->type_error_proto, "Number.prototype method called on something not a number");
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
        if (!jv_is_bool(tv)) return js_throw_msg(vm, vm->type_error_proto, "Boolean.prototype method called on something not a boolean");
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
        jp p = { vm->strs[s].off, vm->strs[s].len, 0, 0 };
        jv v = jp_value(vm, &p, 0);
        jp_ws(vm, &p);
        if (p.err || p.i != p.n) return js_throw_msg(vm, vm->syntax_error_proto, "Unexpected token in JSON");
        return v;
    }
    /* ---- console ---- */
    case N_CON_LOG: return console_out(vm, 0, args, argc);
    case N_CON_INFO: return console_out(vm, 1, args, argc);
    case N_CON_WARN: return console_out(vm, 2, args, argc);
    case N_CON_ERROR: return console_out(vm, 3, args, argc);
    case N_CON_DEBUG: return console_out(vm, 4, args, argc);
    case N_EP_TOSTRING: {
        vector<char> t;
        js_error_text(vm, thisv, &t);
        return jv_str(js_str_utf8(vm, data(t), len(t)));
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
    case N_DP_GETTIME: { double t = this_time(vm, thisv); return vm->throwing ? JV_HOLE : jv_num(t); }
    case N_DP_GETFULLYEAR: case N_DP_GETMONTH: case N_DP_GETDATE: case N_DP_GETDAY: case N_DP_GETHOURS: case N_DP_GETMINUTES:
    case N_DP_GETSECONDS: case N_DP_GETMS: {
        double t = this_time(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        int which = id == N_DP_GETFULLYEAR ? 0 : id == N_DP_GETMONTH ? 1 : id == N_DP_GETDATE ? 2 : id == N_DP_GETDAY ? 3 : id == N_DP_GETHOURS ? 4 :
                    id == N_DP_GETMINUTES ? 5 : id == N_DP_GETSECONDS ? 6 : 7;
        return jv_num(date_field(t, which));
    }
    case N_DP_TOISO: case N_DP_TOSTRING: {
        double t = this_time(vm, thisv);
        if (vm->throwing) return JV_HOLE;
        if (t != t) {
            if (id == N_DP_TOISO) return js_throw_msg(vm, vm->range_error_proto, "Invalid time value");
            return jv_str(js_str_ascii(vm, "Invalid Date"));
        }
        char b[64];
        snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)date_field(t, 0), (int)date_field(t, 1) + 1, (int)date_field(t, 2),
                 (int)date_field(t, 4), (int)date_field(t, 5), (int)date_field(t, 6), (int)date_field(t, 7));
        return jv_str(js_str_ascii(vm, b));
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
        if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
        js_job_push(vm, JOB_CALL, 0, a0, JV_UNDEF, JV_UNDEF);
        return JV_UNDEF;
    }
    /* ---- Map and Set ---- */
    case N_MP_GET: case N_MP_SET: case N_MP_HAS: case N_MP_DELETE: case N_MP_CLEAR: case N_MP_SIZE: case N_MP_FOREACH:
    case N_MP_KEYS: case N_MP_VALUES: case N_MP_ENTRIES: case N_SETP_ADD: case N_SETP_FOREACH: case N_SETP_VALUES: case N_SETP_ENTRIES: {
        int set = id >= N_SETP_ADD || (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_SET);
        uint32_t c = this_coll(vm, thisv, set ? JK_SET : JK_MAP);
        if (!c) return JV_HOLE;
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
        if (id == N_MP_ENTRIES || id == N_SETP_ENTRIES) return iter_of_coll(vm, c, set ? JI_SET : JI_ENTRIES);
        if (id == N_MP_FOREACH || id == N_SETP_FOREACH) {
            if (!js_is_callable(vm, a0)) return js_type_error(vm, "is not a function", js_typeof(vm, a0));
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
    case N_ITP_NEXT: {
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_ITER) return js_throw_msg(vm, vm->type_error_proto, "next called on something not an iterator");
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
    vm->array_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
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
    vm->global = js_obj_new(vm, JK_PLAIN, vm->object_proto);
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
    defn(vm, o, "isFrozen", N_OBJ_ISFROZEN, 1);
    defn(vm, o, "preventExtensions", N_OBJ_PREVENTEXT, 1);
    defn(vm, o, "seal", N_OBJ_PREVENTEXT, 1);
    defn(vm, o, "isExtensible", N_OBJ_ISEXT, 1);
    defn(vm, o, "isSealed", N_OBJ_ISSEALED, 1);
    defn(vm, o, "hasOwn", N_OBJ_HASOWN, 2);
    defn(vm, o, "getOwnPropertyDescriptors", N_OBJ_GETOWNDESCS, 1);
    defn(vm, o, "groupBy", N_OBJ_GROUPBY, 2);
    defn(vm, o, "fromEntries", N_OBJ_FROMENTRIES, 1);
    defn(vm, o, "is", N_OBJ_IS, 2);
    uint32_t op = vm->object_proto;
    defn(vm, op, "hasOwnProperty", N_OP_HASOWN, 1);
    defn(vm, op, "toString", N_OP_TOSTRING, 0);
    defn(vm, op, "toLocaleString", N_OP_TOLOCALE, 0);
    defn(vm, op, "valueOf", N_OP_VALUEOF, 0);
    defn(vm, op, "isPrototypeOf", N_OP_ISPROTO, 1);
    defn(vm, op, "propertyIsEnumerable", N_OP_PROPENUM, 1);

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
        "some", "every", "sort", "fill", "keys", "values", "entries", "flat", "flatMap", "at", "toString" };
    for (uint32_t i = 0; i < sizeof an / sizeof an[0]; i++) defn(vm, ap, an[i], N_AP_PUSH + i, 1);

    uint32_t s = ctor(vm, "String", N_STRING, 1, vm->string_proto);
    defn(vm, s, "fromCharCode", N_STR_FROMCHARCODE, 1);
    defn(vm, s, "raw", N_STR_RAW, 1);
    defn(vm, s, "fromCodePoint", N_STR_FROMCODEPOINT, 1);
    uint32_t sp = vm->string_proto;
    static const char *const sn[] = { "charAt", "charCodeAt", "codePointAt", "indexOf", "lastIndexOf", "includes", "startsWith", "endsWith",
        "slice", "substring", "substr", "toUpperCase", "toLowerCase", "trim", "trimStart", "trimEnd", "padStart", "padEnd", "repeat",
        "split", "replace", "replaceAll", "concat", "at", "localeCompare", "toString", "normalize", "match", "search" };
    for (uint32_t i = 0; i < sizeof sn / sizeof sn[0]; i++) defn(vm, sp, sn[i], N_SP_CHARAT + i, 1);
    defn(vm, sp, "valueOf", N_SP_TOSTRING, 0);
    defn(vm, sp, "toLocaleUpperCase", N_SP_TOUPPER, 0);
    defn(vm, sp, "toLocaleLowerCase", N_SP_TOLOWER, 0);
    defn(vm, sp, "trimLeft", N_SP_TRIMSTART, 0);
    defn(vm, sp, "trimRight", N_SP_TRIMEND, 0);

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
    for (uint32_t i = 0; i < sizeof mn / sizeof mn[0]; i++) defn(vm, m, mn[i], N_M_ABS + i, 1);
    defv(vm, m, "PI", jv_num(3.141592653589793));
    defv(vm, m, "E", jv_num(2.718281828459045));
    defv(vm, m, "LN2", jv_num(0.6931471805599453));
    defv(vm, m, "LN10", jv_num(2.302585092994046));
    defv(vm, m, "LOG2E", jv_num(1.4426950408889634));
    defv(vm, m, "LOG10E", jv_num(0.4342944819032518));
    defv(vm, m, "SQRT2", jv_num(1.4142135623730951));
    defv(vm, m, "SQRT1_2", jv_num(0.7071067811865476));

    uint32_t j = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    defv(vm, g, "JSON", jv_obj(j));
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

    uint32_t d = ctor(vm, "Date", N_DATE, 7, vm->date_proto);
    defn(vm, d, "now", N_DATE_NOW, 0);
    uint32_t dp = vm->date_proto;
    defn(vm, dp, "getTime", N_DP_GETTIME, 0);
    defn(vm, dp, "valueOf", N_DP_GETTIME, 0);
    defn(vm, dp, "toISOString", N_DP_TOISO, 0);
    defn(vm, dp, "toJSON", N_DP_TOISO, 0);
    defn(vm, dp, "toString", N_DP_TOSTRING, 0);
    static const char *const dn[] = { "getFullYear", "getMonth", "getDate", "getDay", "getHours", "getMinutes", "getSeconds", "getMilliseconds" };
    static const char *const dun[] = { "getUTCFullYear", "getUTCMonth", "getUTCDate", "getUTCDay", "getUTCHours", "getUTCMinutes", "getUTCSeconds", "getUTCMilliseconds" };
    for (uint32_t i = 0; i < 8; i++) { defn(vm, dp, dn[i], N_DP_GETFULLYEAR + i, 0); defn(vm, dp, dun[i], N_DP_GETFULLYEAR + i, 0); }

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
    defn(vm, stp, "has", N_MP_HAS, 1);
    defn(vm, stp, "delete", N_MP_DELETE, 1);
    defn(vm, stp, "clear", N_MP_CLEAR, 0);
    defn(vm, stp, "forEach", N_SETP_FOREACH, 1);
    defn(vm, stp, "values", N_SETP_VALUES, 0);
    defn(vm, stp, "keys", N_SETP_VALUES, 0);
    defn(vm, stp, "entries", N_SETP_ENTRIES, 0);
    js_define_accessor(vm, stp, A_SIZE, jv_obj(js_native_new(vm, N_MP_SIZE, A_SIZE, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    defn(vm, vm->iterator_proto, "next", N_ITP_NEXT, 0);
    js_proxy_init(vm, N_PX_FIRST);
    js_big_init(vm, N_BG_FIRST);
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
    js_define(vm, syp, JS_SYM_KEY | JSYM_TO_PRIMITIVE, jv_obj(js_native_new(vm, N_SYMP_VALUEOF, A_EMPTY, 1)), JS_SHAPE_HIDDEN);
    js_define(vm, syp, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Symbol")), JS_SHAPE_HIDDEN);
    {
        uint32_t oc = jv_idx(js_get(vm, jv_obj(vm->global), js_atom_ascii(vm, "Object")));
        defn(vm, oc, "getOwnPropertySymbols", N_OBJ_GETOWNSYMS, 1);
    }

    /* the iterators' protocol: each iterable's Symbol.iterator, and an iterator its own */
    js_define(vm, vm->iterator_proto, JS_KEY_ITERATOR, jv_obj(js_native_new(vm, N_ITP_SELF, js_str_ascii(vm, "[Symbol.iterator]"), 0)), JS_SHAPE_HIDDEN);
    js_define(vm, vm->array_proto, JS_KEY_ITERATOR, js_get(vm, jv_obj(vm->array_proto), js_atom_ascii(vm, "values")), JS_SHAPE_HIDDEN);
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
}
