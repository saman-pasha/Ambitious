/* src/js/bigint.c -- BigInt (ECMA-262 21.2): integers of any size.

   A BigInt value is its own tag (JV_BIG_TAG) over a record of vm->bigs: its sign, and
   its magnitude, 32-bit limbs least first with no high zero limb (zero has none), a
   span of vm->bigl. A value is never changed: each operation makes a new record. The
   arithmetic is the schoolbook's: Knuth's long division, binary powers; the bitwise
   operators work on two's complement as the standard says, as if the number had
   infinitely many sign bits. A result past JS_BIG_LIMBS limbs is a RangeError. */
#include <stdlib.h>
#include <math.h>
#include "js.h"

#define JS_BIG_LIMBS (1u << 20)        /* 32 million bits */

/* ---- magnitudes ---- */

static void mag_trim(vector<uint32_t> *m) { while (len(*m) && (*m)[len(*m) - 1] == 0) pop_back(*m); }

static void big_load(js_vm *vm, jv v, vector<uint32_t> *m, int *neg) {
    js_big b = vm->bigs[jv_idx(v)];
    clear(*m);
    for (uint32_t i = 0; i < b.n; i++) push_back(*m, vm->bigl[b.off + i]);
    *neg = (int)b.neg;
}

/* a BigInt's record: a freed one's taken first (src/js/gc.c) */
static jv big_push(js_vm *vm, js_big b) {
    vm->gc_allocs++;
    if (vm->big_free) {
        uint32_t i = vm->big_free - 1;
        vm->big_free = vm->bigs[i].off;
        vm->bigs[i] = b;
        return jv_big(i);
    }
    push_back(vm->bigs, b);
    return jv_big((uint32_t)(len(vm->bigs) - 1));
}

/* a magnitude and a sign as a BigInt value; JV_HOLE with a RangeError past the size */
static jv big_make(js_vm *vm, vector<uint32_t> *m, int neg) {
    mag_trim(m);
    if (len(*m) > JS_BIG_LIMBS) return js_throw_msg(vm, vm->range_error_proto, "Maximum BigInt size exceeded");
    js_big b;
    b.off = (uint32_t)len(vm->bigl);
    b.n = (uint32_t)len(*m);
    b.neg = b.n ? (uint32_t)(neg != 0) : 0;
    for (size_t i = 0; i < len(*m); i++) push_back(vm->bigl, (*m)[i]);
    return big_push(vm, b);
}

/* a BigInt of at most 64 bits: its magnitude and sign (1), else 0 */
static int big_small(js_vm *vm, jv v, uint64_t *u, int *neg) {
    js_big b = vm->bigs[jv_idx(v)];
    if (b.n > 2) return 0;
    *u = b.n ? vm->bigl[b.off] : 0;
    if (b.n > 1) *u |= (uint64_t)vm->bigl[b.off + 1] << 32;
    *neg = (int)b.neg;
    return 1;
}
/* a BigInt of a 64-bit magnitude, made without a vector between */
static jv big_make_u64(js_vm *vm, uint64_t u, int neg) {
    js_big b;
    b.off = (uint32_t)len(vm->bigl);
    b.n = u ? (u >> 32 ? 2u : 1u) : 0u;
    b.neg = u ? (uint32_t)(neg != 0) : 0;
    if (u) push_back(vm->bigl, (uint32_t)u);
    if (u >> 32) push_back(vm->bigl, (uint32_t)(u >> 32));
    return big_push(vm, b);
}

static int mag_cmp(vector<uint32_t> *a, vector<uint32_t> *b) {
    if (len(*a) != len(*b)) return len(*a) < len(*b) ? -1 : 1;
    for (size_t i = len(*a); i > 0; i--) {
        uint32_t x = (*a)[i - 1], y = (*b)[i - 1];
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

static void mag_add(vector<uint32_t> *a, vector<uint32_t> *b, vector<uint32_t> *out) {
    size_t n = len(*a) > len(*b) ? len(*a) : len(*b);
    vector<uint32_t> r;
    uint64_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t s = (uint64_t)(i < len(*a) ? (*a)[i] : 0) + (i < len(*b) ? (*b)[i] : 0) + carry;
        push_back(r, (uint32_t)s);
        carry = s >> 32;
    }
    if (carry) push_back(r, (uint32_t)carry);
    clear(*out);
    for (size_t i = 0; i < len(r); i++) push_back(*out, r[i]);
}

/* a - b, a at least b */
static void mag_sub(vector<uint32_t> *a, vector<uint32_t> *b, vector<uint32_t> *out) {
    vector<uint32_t> r;
    int64_t borrow = 0;
    for (size_t i = 0; i < len(*a); i++) {
        int64_t t = (int64_t)(*a)[i] - (int64_t)(i < len(*b) ? (*b)[i] : 0) - borrow;
        borrow = t < 0;
        if (t < 0) t += (int64_t)1 << 32;
        push_back(r, (uint32_t)t);
    }
    mag_trim(&r);
    clear(*out);
    for (size_t i = 0; i < len(r); i++) push_back(*out, r[i]);
}

static void mag_mul(vector<uint32_t> *a, vector<uint32_t> *b, vector<uint32_t> *out) {
    vector<uint32_t> r;
    resize(r, len(*a) + len(*b), 0);
    for (size_t i = 0; i < len(*a); i++) {
        uint64_t carry = 0, ai = (*a)[i];
        if (!ai) continue;
        for (size_t j = 0; j < len(*b); j++) {
            uint64_t t = (uint64_t)r[i + j] + ai * (*b)[j] + carry;
            r[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        size_t k = i + len(*b);
        while (carry) {
            uint64_t t = (uint64_t)r[k] + carry;
            r[k] = (uint32_t)t;
            carry = t >> 32;
            k++;
        }
    }
    mag_trim(&r);
    clear(*out);
    for (size_t i = 0; i < len(r); i++) push_back(*out, r[i]);
}

/* a / d and a % d for one limb d */
static uint32_t mag_divsmall(vector<uint32_t> *a, uint32_t d, vector<uint32_t> *q) {
    vector<uint32_t> r;
    resize(r, len(*a), 0);
    uint64_t rem = 0;
    for (size_t i = len(*a); i > 0; i--) {
        uint64_t cur = rem << 32 | (*a)[i - 1];
        r[i - 1] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    mag_trim(&r);
    clear(*q);
    for (size_t i = 0; i < len(r); i++) push_back(*q, r[i]);
    return (uint32_t)rem;
}

static uint32_t clz32(uint32_t x) {
    uint32_t n = 0;
    if (!x) return 32;
    while (!(x & 0x80000000u)) { x <<= 1; n++; }
    return n;
}

/* a / b and a % b (Knuth's algorithm D), b not zero */
static void mag_divmod(vector<uint32_t> *a, vector<uint32_t> *b, vector<uint32_t> *q, vector<uint32_t> *r) {
    if (mag_cmp(a, b) < 0) {
        clear(*q);
        clear(*r);
        for (size_t i = 0; i < len(*a); i++) push_back(*r, (*a)[i]);
        return;
    }
    if (len(*b) == 1) {
        uint32_t rem = mag_divsmall(a, (*b)[0], q);
        clear(*r);
        if (rem) push_back(*r, rem);
        return;
    }
    uint32_t n = (uint32_t)len(*b), m = (uint32_t)len(*a) - n;
    uint32_t s = clz32((*b)[n - 1]);
    /* normalized: v the divisor shifted so its top bit is set, u the dividend with it */
    vector<uint32_t> v, u, qq;
    resize(v, n, 0);
    resize(u, len(*a) + 1, 0);
    resize(qq, m + 1, 0);
    for (uint32_t i = n; i > 0; i--) {
        uint32_t hi = (*b)[i - 1] << s, lo = (i >= 2 && s) ? (*b)[i - 2] >> (32 - s) : 0;
        v[i - 1] = hi | lo;
    }
    u[len(*a)] = s ? (*a)[len(*a) - 1] >> (32 - s) : 0;
    for (size_t i = len(*a); i > 0; i--) {
        uint32_t hi = (*a)[i - 1] << s, lo = (i >= 2 && s) ? (*a)[i - 2] >> (32 - s) : 0;
        u[i - 1] = hi | lo;
    }
    for (uint32_t jj = m + 1; jj > 0; jj--) {
        uint32_t j = jj - 1;
        uint64_t num = (uint64_t)u[j + n] << 32 | u[j + n - 1];
        uint64_t qhat = num / v[n - 1], rhat = num % v[n - 1];
        while (qhat >= 0x100000000ull || qhat * v[n - 2] > ((rhat << 32) | u[j + n - 2])) {
            qhat--;
            rhat += v[n - 1];
            if (rhat >= 0x100000000ull) break;
        }
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t pr = qhat * v[i] + carry;
            carry = pr >> 32;
            int64_t t = (int64_t)u[i + j] - (int64_t)(pr & 0xFFFFFFFFu) - borrow;
            borrow = t < 0;
            if (t < 0) t += (int64_t)1 << 32;
            u[i + j] = (uint32_t)t;
        }
        int64_t t = (int64_t)u[j + n] - (int64_t)carry - borrow;
        if (t < 0) {
            /* qhat one too big: v added back */
            u[j + n] = (uint32_t)(t + ((int64_t)1 << 32));
            qhat--;
            uint64_t c2 = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint64_t s2 = (uint64_t)u[i + j] + v[i] + c2;
                u[i + j] = (uint32_t)s2;
                c2 = s2 >> 32;
            }
            u[j + n] = (uint32_t)((uint64_t)u[j + n] + c2);
        } else u[j + n] = (uint32_t)t;
        qq[j] = (uint32_t)qhat;
    }
    mag_trim(&qq);
    clear(*q);
    for (size_t i = 0; i < len(qq); i++) push_back(*q, qq[i]);
    /* the remainder, shifted back */
    clear(*r);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lo = u[i] >> s, hi = (s && i + 1 < n + 1) ? u[i + 1] << (32 - s) : 0;
        push_back(*r, s ? (lo | hi) : u[i]);
    }
    mag_trim(r);
}

static void mag_shl(vector<uint32_t> *a, uint64_t bits, vector<uint32_t> *out) {
    uint32_t limbs = (uint32_t)(bits / 32), s = (uint32_t)(bits % 32);
    vector<uint32_t> r;
    resize(r, limbs, 0);
    uint32_t carry = 0;
    for (size_t i = 0; i < len(*a); i++) {
        uint32_t x = (*a)[i];
        push_back(r, s ? (x << s | carry) : x);
        carry = s ? x >> (32 - s) : 0;
    }
    if (carry) push_back(r, carry);
    mag_trim(&r);
    clear(*out);
    for (size_t i = 0; i < len(r); i++) push_back(*out, r[i]);
}

/* a >> bits; *lost when a one bit was shifted out */
static void mag_shr(vector<uint32_t> *a, uint64_t bits, vector<uint32_t> *out, int *lost) {
    *lost = 0;
    uint64_t limbs = bits / 32;
    uint32_t s = (uint32_t)(bits % 32);
    vector<uint32_t> r;
    for (size_t i = 0; i < len(*a) && i < limbs; i++) if ((*a)[i]) *lost = 1;
    if (limbs < len(*a) && s && ((*a)[limbs] & ((1u << s) - 1))) *lost = 1;
    for (uint64_t i = limbs; i < len(*a); i++) {
        uint32_t lo = (*a)[i] >> s, hi = (s && i + 1 < len(*a)) ? (*a)[i + 1] << (32 - s) : 0;
        push_back(r, s ? (lo | hi) : (*a)[i]);
    }
    mag_trim(&r);
    clear(*out);
    for (size_t i = 0; i < len(r); i++) push_back(*out, r[i]);
}

static void mag_one(vector<uint32_t> *m) { clear(*m); push_back(*m, 1); }

/* ---- making and reading BigInts ---- */

jv js_big_from_u64(js_vm *vm, uint64_t u, int neg) { return big_make_u64(vm, u, neg); }
jv js_big_from_i64(js_vm *vm, int64_t v) { return js_big_from_u64(vm, v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v, v < 0); }

/* an integral double as a BigInt */
jv js_big_from_double(js_vm *vm, double d) {
    int neg = d < 0;
    d = fabs(d);
    vector<uint32_t> m;
    if (d < 18446744073709551616.0) return js_big_from_u64(vm, (uint64_t)d, neg);
    int e;
    double f = frexp(d, &e);              /* d = f * 2^e, f in [0.5, 1) */
    uint64_t mant = (uint64_t)ldexp(f, 53);
    push_back(m, (uint32_t)mant);
    push_back(m, (uint32_t)(mant >> 32));
    vector<uint32_t> r;
    mag_shl(&m, (uint64_t)(e - 53), &r);
    return big_make(vm, &r, neg);
}

/* the double nearest (ties to even) */
double js_big_to_double(js_vm *vm, jv v) {
    js_big b = vm->bigs[jv_idx(v)];
    if (!b.n) return 0;
    uint32_t top = vm->bigl[b.off + b.n - 1];
    uint64_t bits = (uint64_t)(b.n - 1) * 32 + (32 - clz32(top));
    if (bits > 1024) return b.neg ? -INFINITY : INFINITY;
    /* the top 64 bits and whether any bit below them is set: the rounding's */
    vector<uint32_t> m;
    int neg;
    big_load(vm, v, &m, &neg);
    double d;
    if (bits <= 64) {
        uint64_t u = 0;
        for (size_t i = len(m); i > 0; i--) u = u << 32 | m[i - 1];
        d = (double)u;
        if (bits > 53) {
            /* the conversion of a 64-bit integer rounds as the standard asks (to nearest, ties even) */
            d = (double)u;
        }
    } else {
        vector<uint32_t> hi;
        int lost = 0;
        mag_shr(&m, bits - 64, &hi, &lost);
        uint64_t u = 0;
        for (size_t i = len(hi); i > 0; i--) u = u << 32 | hi[i - 1];
        /* 64 bits, the low 11 below the double's: round to nearest even with the lost ones as sticky */
        uint64_t keep = u >> 11, rest = u & 0x7FF;
        if (rest > 0x400 || (rest == 0x400 && (lost || (keep & 1)))) keep++;
        d = ldexp((double)keep, (int)(bits - 53));
    }
    return neg ? -d : d;
}

/* the low 64 bits in two's complement (BigInt.asUintN(64)'s) */
uint64_t js_big_to_u64(js_vm *vm, jv v) {
    js_big b = vm->bigs[jv_idx(v)];
    uint64_t u = 0;
    if (b.n > 0) u = vm->bigl[b.off];
    if (b.n > 1) u |= (uint64_t)vm->bigl[b.off + 1] << 32;
    return b.neg ? (uint64_t)0 - u : u;
}

int js_big_is_zero(js_vm *vm, jv v) { return vm->bigs[jv_idx(v)].n == 0; }

int js_big_cmp(js_vm *vm, jv a, jv b) {
    uint64_t ua, ub;
    int sa, sb;
    if (big_small(vm, a, &ua, &sa) && big_small(vm, b, &ub, &sb)) {
        if (sa != sb) return sa ? -1 : 1;
        int c = ua < ub ? -1 : ua > ub ? 1 : 0;
        return sa ? -c : c;
    }
    vector<uint32_t> x, y;
    int nx, ny;
    big_load(vm, a, &x, &nx);
    big_load(vm, b, &y, &ny);
    if (nx != ny) return nx ? -1 : 1;
    int c = mag_cmp(&x, &y);
    return nx ? -c : c;
}
int js_big_eq(js_vm *vm, jv a, jv b) { return a == b || js_big_cmp(vm, a, b) == 0; }

/* a BigInt against a number: -1, 0, 1, or 2 when the number is NaN */
int js_big_cmp_num(js_vm *vm, jv a, double d) {
    if (d != d) return 2;
    if (d == INFINITY) return -1;
    if (d == -INFINITY) return 1;
    double fl = floor(d);
    jv b = js_big_from_double(vm, fl);
    int c = js_big_cmp(vm, a, b);
    if (c != 0) return c;
    return fl < d ? -1 : 0;
}

uint32_t js_big_hash(js_vm *vm, jv v) {
    js_big b = vm->bigs[jv_idx(v)];
    uint32_t h = 2166136261u ^ b.neg;
    for (uint32_t i = 0; i < b.n; i++) { h ^= vm->bigl[b.off + i]; h *= 16777619u; }
    return h | 1;
}

/* its digits in radix (2 to 36) */
uint32_t js_big_to_string(js_vm *vm, jv v, uint32_t radix) {
    vector<uint32_t> m;
    int neg;
    big_load(vm, v, &m, &neg);
    if (!len(m)) return vm->digit_strs[0];
    /* the largest power of radix in one limb, each division by it giving that many digits */
    uint32_t chunk = radix, per = 1;
    while ((uint64_t)chunk * radix <= 0xFFFFFFFFu) { chunk *= radix; per++; }
    vector<char> digits;
    vector<uint32_t> q;
    while (len(m)) {
        uint32_t rem = mag_divsmall(&m, chunk, &q);
        clear(m);
        for (size_t i = 0; i < len(q); i++) push_back(m, q[i]);
        for (uint32_t k = 0; k < per && (len(m) || rem); k++) {
            uint32_t dgt = rem % radix;
            rem /= radix;
            push_back(digits, (char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10));
        }
    }
    vector<char> out;
    if (neg) push_back(out, '-');
    for (size_t i = len(digits); i > 0; i--) push_back(out, digits[i - 1]);
    return js_str_utf8(vm, data(out), len(out));
}

/* the text (code units o..o+n of vm->units) as a BigInt: JV_HOLE when it is not one (no
   exception thrown). literal: a source literal (its n gone, _ separators allowed, no sign) */
static jv big_parse_units(js_vm *vm, uint32_t o, uint32_t n, int literal) {
    uint32_t i = 0;
    if (!literal) {
        /* StringToBigInt: white space trimmed, a sign for decimal digits only */
        while (i < n && (vm->units[o + i] == ' ' || (vm->units[o + i] >= 9 && vm->units[o + i] <= 13) || vm->units[o + i] == 0xA0 || vm->units[o + i] == 0xFEFF || vm->units[o + i] == 0x2028 || vm->units[o + i] == 0x2029 || (vm->units[o + i] >= 0x2000 && vm->units[o + i] <= 0x200A) || vm->units[o + i] == 0x3000 || vm->units[o + i] == 0x1680 || vm->units[o + i] == 0x202F || vm->units[o + i] == 0x205F)) i++;
        while (n > i && (vm->units[o + n - 1] == ' ' || (vm->units[o + n - 1] >= 9 && vm->units[o + n - 1] <= 13) || vm->units[o + n - 1] == 0xA0 || vm->units[o + n - 1] == 0xFEFF)) n--;
    }
    int neg = 0;
    uint32_t radix = 10;
    if (i < n && (vm->units[o + i] == '+' || vm->units[o + i] == '-') && !literal) { neg = vm->units[o + i] == '-'; i++; if (i >= n) return JV_HOLE; }
    if (i + 1 < n && vm->units[o + i] == '0') {
        uint16_t x = vm->units[o + i + 1] | 0x20;
        if (x == 'x' || x == 'o' || x == 'b') {
            if (neg && !literal) return JV_HOLE;
            radix = x == 'x' ? 16 : x == 'o' ? 8 : 2;
            i += 2;
            if (i >= n) return JV_HOLE;
        }
    }
    vector<uint32_t> m, t, base;
    push_back(base, radix);
    int any = 0;
    for (; i < n; i++) {
        uint16_t c = vm->units[o + i];
        if (c == '_' && literal) continue;
        uint32_t dgt = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'z' ? (uint32_t)((c | 0x20) - 'a' + 10) : 99;
        if (dgt >= radix) return JV_HOLE;
        mag_mul(&m, &base, &t);
        vector<uint32_t> d1;
        if (dgt) push_back(d1, dgt);
        mag_add(&t, &d1, &m);
        any = 1;
    }
    if (!any && !(literal == 0 && i == n && radix == 10)) { if (radix != 10) return JV_HOLE; }
    return big_make(vm, &m, neg);
}

jv js_big_parse(js_vm *vm, uint32_t s, int literal) {
    js_str_flatten(vm, s);
    return big_parse_units(vm, vm->strs[s].off, vm->strs[s].len, literal);
}

/* a literal's text (ASCII, its n dropped) as a BigInt: the compiler's */
jv js_big_literal(js_vm *vm, const char *text, size_t n) {
    uint32_t s = js_str_utf8(vm, text, n);
    return js_big_parse(vm, s, 1);
}

/* two's complement in k limbs (k past the magnitude's: the sign limbs) */
static void twos(vector<uint32_t> *m, int neg, size_t k, vector<uint32_t> *out) {
    clear(*out);
    for (size_t i = 0; i < k; i++) push_back(*out, i < len(*m) ? (*m)[i] : 0);
    if (!neg) return;
    /* -m: every limb inverted, then one added */
    uint64_t carry = 1;
    for (size_t i = 0; i < k; i++) {
        uint64_t t = (uint64_t)(uint32_t)~(*out)[i] + carry;
        (*out)[i] = (uint32_t)t;
        carry = t >> 32;
    }
}
/* two's complement back to a sign and a magnitude */
static jv from_twos(js_vm *vm, vector<uint32_t> *t) {
    int neg = len(*t) && ((*t)[len(*t) - 1] & 0x80000000u);
    if (neg) {
        uint64_t carry = 1;
        for (size_t i = 0; i < len(*t); i++) {
            uint64_t x = (uint64_t)(uint32_t)~(*t)[i] + carry;
            (*t)[i] = (uint32_t)x;
            carry = x >> 32;
        }
    }
    return big_make(vm, t, neg);
}

/* a op b for BigInts, op one of OP_ADD .. OP_SHR (src/js/js.h's opcodes) */
jv js_big_binop(js_vm *vm, uint32_t op, jv a, jv b) {
    /* both at most 64 bits: the machine's arithmetic, where the result fits */
    uint64_t ua, ub;
    int sa, sb;
    if (big_small(vm, a, &ua, &sa) && big_small(vm, b, &ub, &sb)) {
        if (op == OP_ADD || op == OP_SUB) {
            int sb2 = op == OP_SUB ? (ub ? !sb : 0) : sb;
            if (sa == sb2) { uint64_t t = ua + ub; if (t >= ua) return big_make_u64(vm, t, sa); }
            else if (ua >= ub) return big_make_u64(vm, ua - ub, sa);
            else return big_make_u64(vm, ub - ua, sb2);
        } else if (op == OP_MUL && ua <= 0xFFFFFFFFu && ub <= 0xFFFFFFFFu) return big_make_u64(vm, ua * ub, sa != sb);
        else if ((op == OP_DIV || op == OP_MOD) && ub) return op == OP_DIV ? big_make_u64(vm, ua / ub, sa != sb) : big_make_u64(vm, ua % ub, sa);
    }
    vector<uint32_t> x, y, r, q;
    int nx, ny;
    big_load(vm, a, &x, &nx);
    big_load(vm, b, &y, &ny);
    switch (op) {
    case OP_ADD: case OP_SUB: {
        if (op == OP_SUB) ny = !ny && len(y) ? 1 : 0;
        if (nx == ny) { mag_add(&x, &y, &r); return big_make(vm, &r, nx); }
        int c = mag_cmp(&x, &y);
        if (c == 0) { clear(r); return big_make(vm, &r, 0); }
        if (c > 0) { mag_sub(&x, &y, &r); return big_make(vm, &r, nx); }
        mag_sub(&y, &x, &r);
        return big_make(vm, &r, ny);
    }
    case OP_MUL:
        mag_mul(&x, &y, &r);
        return big_make(vm, &r, nx != ny);
    case OP_DIV: case OP_MOD:
        if (!len(y)) return js_throw_msg(vm, vm->range_error_proto, "Division by zero");
        mag_divmod(&x, &y, &q, &r);
        if (op == OP_DIV) return big_make(vm, &q, nx != ny);
        return big_make(vm, &r, nx);
    case OP_POW: {
        if (ny && len(y)) return js_throw_msg(vm, vm->range_error_proto, "Exponent must be non-negative");
        if (len(y) > 1) {
            /* a huge exponent: only 0, 1 and -1 stay small */
            if (!len(x)) { clear(r); return big_make(vm, &r, 0); }
            if (len(x) == 1 && x[0] == 1) { mag_one(&r); return big_make(vm, &r, nx && (y[0] & 1)); }
            return js_throw_msg(vm, vm->range_error_proto, "Maximum BigInt size exceeded");
        }
        uint32_t e = len(y) ? y[0] : 0;
        mag_one(&r);
        vector<uint32_t> base, t;
        for (size_t i = 0; i < len(x); i++) push_back(base, x[i]);
        int neg = nx && (e & 1);
        while (e) {
            if (e & 1) { mag_mul(&r, &base, &t); clear(r); for (size_t i = 0; i < len(t); i++) push_back(r, t[i]); }
            e >>= 1;
            if (e) {
                if (len(base) > JS_BIG_LIMBS / 2) return js_throw_msg(vm, vm->range_error_proto, "Maximum BigInt size exceeded");
                mag_mul(&base, &base, &t);
                clear(base);
                for (size_t i = 0; i < len(t); i++) push_back(base, t[i]);
            }
            if (len(r) > JS_BIG_LIMBS) return js_throw_msg(vm, vm->range_error_proto, "Maximum BigInt size exceeded");
        }
        return big_make(vm, &r, neg);
    }
    case OP_BAND: case OP_BOR: case OP_BXOR: {
        size_t k = (len(x) > len(y) ? len(x) : len(y)) + 1;
        vector<uint32_t> tx, ty;
        twos(&x, nx, k, &tx);
        twos(&y, ny, k, &ty);
        for (size_t i = 0; i < k; i++) {
            uint32_t v = op == OP_BAND ? (tx[i] & ty[i]) : op == OP_BOR ? (tx[i] | ty[i]) : (tx[i] ^ ty[i]);
            push_back(r, v);
        }
        return from_twos(vm, &r);
    }
    case OP_SHL: case OP_SAR: {
        /* a shift by a BigInt count, a negative one the other way */
        int left = op == OP_SHL;
        if (ny) left = !left;
        if (len(y) > 1 || (len(y) == 1 && y[0] > 32u * JS_BIG_LIMBS)) {
            if (left && len(x)) return js_throw_msg(vm, vm->range_error_proto, "Maximum BigInt size exceeded");
            if (!left) { clear(r); if (nx && len(x)) push_back(r, 1); return big_make(vm, &r, nx && len(x)); }
            clear(r);
            return big_make(vm, &r, 0);
        }
        uint64_t s = len(y) ? y[0] : 0;
        if (left) { mag_shl(&x, s, &r); return big_make(vm, &r, nx); }
        int lost = 0;
        mag_shr(&x, s, &r, &lost);
        /* a negative number's shift rounds toward minus infinity */
        if (nx && lost) {
            vector<uint32_t> one, t;
            mag_one(&one);
            mag_add(&r, &one, &t);
            return big_make(vm, &t, 1);
        }
        return big_make(vm, &r, nx);
    }
    case OP_SHR: return js_throw_msg(vm, vm->type_error_proto, "BigInts have no unsigned right shift, use >> instead");
    default: return js_throw_msg(vm, vm->type_error_proto, "an operator BigInt does not have");
    }
}

jv js_big_neg(js_vm *vm, jv a) {
    vector<uint32_t> x;
    int nx;
    big_load(vm, a, &x, &nx);
    return big_make(vm, &x, !nx);
}
/* ~a, -a - 1 */
jv js_big_not(js_vm *vm, jv a) {
    jv one = js_big_from_u64(vm, 1, 0);
    jv na = js_big_neg(vm, a);
    return js_big_binop(vm, OP_SUB, na, one);
}
jv js_big_inc(js_vm *vm, jv a, int by) {
    jv one = js_big_from_u64(vm, 1, 0);
    return js_big_binop(vm, by > 0 ? OP_ADD : OP_SUB, a, one);
}

/* a modulo 2^bits, as a signed (asIntN) or unsigned (asUintN) number */
static jv big_as_n(js_vm *vm, jv a, uint64_t bits, int is_signed) {
    if (bits == 0) return js_big_from_u64(vm, 0, 0);
    vector<uint32_t> x, t;
    int nx;
    big_load(vm, a, &x, &nx);
    if (bits > 32ull * JS_BIG_LIMBS) bits = 32ull * JS_BIG_LIMBS;
    size_t k = (size_t)((bits + 31) / 32) + 1;
    if (k < len(x) + 1) k = len(x) + 1;
    twos(&x, nx, k, &t);
    /* the low bits kept */
    size_t keep = (size_t)((bits + 31) / 32);
    resize(t, keep, 0);
    uint32_t rbits = (uint32_t)(bits % 32);
    if (rbits) t[keep - 1] &= (1u << rbits) - 1;
    int top = 0;
    if (is_signed) {
        uint32_t tb = rbits ? rbits - 1 : 31;
        top = (t[keep - 1] >> tb) & 1;
    }
    if (is_signed && top) {
        /* the sign bit set: the value minus 2^bits, its high limbs filled with ones */
        if (rbits) t[keep - 1] |= ~((1u << rbits) - 1);
        push_back(t, 0xFFFFFFFFu);
        return from_twos(vm, &t);
    }
    return big_make(vm, &t, 0);
}

/* ToBigInt: a BigInt, a boolean, or a string's digits; else a TypeError (a SyntaxError for a string that is none) */
jv js_to_bigint(js_vm *vm, jv v) {
    jv p = js_to_primitive(vm, v, 0);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_big(p)) return p;
    if (p == JV_TRUE || p == JV_FALSE) return js_big_from_u64(vm, p == JV_TRUE, 0);
    if (jv_is_str(p)) {
        jv r = js_big_parse(vm, jv_idx(p), 0);
        if (r != JV_HOLE) return r;
        vector<char> m;
        const char *pre = "Cannot convert ";
        append_range(m, pre, strlen(pre));
        js_str_to_utf8(vm, jv_idx(p), &m);
        const char *post = " to a BigInt";
        append_range(m, post, strlen(post));
        push_back(m, 0);
        return js_throw_msg(vm, vm->syntax_error_proto, data(m));
    }
    if (jv_is_num(p)) {
        char m[120];
        char nb[40];
        js_num_format(jv_dbl(p), nb, sizeof nb);
        snprintf(m, sizeof m, "Cannot convert %s to a BigInt", nb);
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    return js_throw_msg(vm, vm->type_error_proto, p == JV_UNDEF ? "Cannot convert undefined to a BigInt" : p == JV_NULL ? "Cannot convert null to a BigInt" : "Cannot convert a Symbol value to a BigInt");
}

/* this as a BigInt (a value or its wrapper) */
static jv big_this(js_vm *vm, jv thisv, const char *what) {
    if (jv_is_big(thisv)) return thisv;
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_BIGINT) return vm->objs[jv_idx(thisv)].x;
    char m[120];
    snprintf(m, sizeof m, "%s requires that 'this' be a BigInt", what);
    return js_throw_msg(vm, vm->type_error_proto, m);
}

jv js_big_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    (void)callee;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    switch (which) {
    case JBG_CTOR: {
        if (newt != JV_UNDEF && newt != JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "BigInt is not a constructor");
        jv p = js_to_primitive(vm, a0, 0);
        if (vm->throwing) return JV_HOLE;
        if (jv_is_num(p)) {
            double d = jv_dbl(p);
            if (d != d || d == INFINITY || d == -INFINITY || d != trunc(d)) {
                char m[140], nb[40];
                js_num_format(d, nb, sizeof nb);
                snprintf(m, sizeof m, "The number %s cannot be converted to a BigInt because it is not an integer", nb);
                return js_throw_msg(vm, vm->range_error_proto, m);
            }
            return js_big_from_double(vm, d);
        }
        return js_to_bigint(vm, p);
    }
    case JBG_ASINTN: case JBG_ASUINTN: {
        double bits = js_to_number(vm, a0);
        if (vm->throwing) return JV_HOLE;
        bits = bits != bits ? 0 : trunc(bits);
        if (bits < 0 || bits > 9007199254740991.0) return js_throw_msg(vm, vm->range_error_proto, "Invalid value: not (convertible to) a safe integer");
        jv b = js_to_bigint(vm, a1);
        if (vm->throwing) return JV_HOLE;
        return big_as_n(vm, b, (uint64_t)bits, which == JBG_ASINTN);
    }
    case JBG_TOSTRING: case JBG_TOLOCALE: {
        jv b = big_this(vm, thisv, "BigInt.prototype.toString");
        if (vm->throwing) return JV_HOLE;
        uint32_t radix = 10;
        if (which == JBG_TOSTRING && a0 != JV_UNDEF) {
            double r = js_to_number(vm, a0);
            if (vm->throwing) return JV_HOLE;
            if (r != r || r < 2 || r > 36) return js_throw_msg(vm, vm->range_error_proto, "toString() radix must be between 2 and 36");
            radix = (uint32_t)r;
        }
        return jv_str(js_big_to_string(vm, b, radix));
    }
    case JBG_VALUEOF: return big_this(vm, thisv, "BigInt.prototype.valueOf");
    default: return JV_UNDEF;
    }
}

/* BigInt and its prototype; first, the native id of JBG_CTOR */
void js_big_init(js_vm *vm, uint32_t first) {
    vm->bigint_proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    uint32_t atom = js_atom_ascii(vm, "BigInt");
    uint32_t c = js_native_new(vm, first + JBG_CTOR, atom, 1);
    js_define(vm, c, A_PROTOTYPE, jv_obj(vm->bigint_proto), JS_SHAPE_HIDDEN);
    js_define(vm, vm->bigint_proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, atom, jv_obj(c), JS_SHAPE_HIDDEN);
    static const char *const sn[] = { "asIntN", "asUintN" };
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t at = js_atom_ascii(vm, sn[i]);
        js_define(vm, c, at, jv_obj(js_native_new(vm, first + JBG_ASINTN + i, at, 2)), JS_SHAPE_HIDDEN);
    }
    static const char *const pn[] = { "toString", "toLocaleString", "valueOf" };
    static const uint32_t pi[] = { JBG_TOSTRING, JBG_TOLOCALE, JBG_VALUEOF };
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t at = js_atom_ascii(vm, pn[i]);
        js_define(vm, vm->bigint_proto, at, jv_obj(js_native_new(vm, first + pi[i], at, 0)), JS_SHAPE_HIDDEN);
    }
    js_define(vm, vm->bigint_proto, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "BigInt")), JS_SHAPE_HIDDEN);
}
