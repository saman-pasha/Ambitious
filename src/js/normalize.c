/* src/js/normalize.c -- String.prototype.normalize (ECMA-262 22.1.3.15): Unicode's
   normalization forms NFC, NFD, NFKC and NFKD (UAX #15), by its algorithm. Each code
   point is decomposed in full, canonically or with the compatibility mappings too
   (Hangul's syllables by their arithmetic); the combining marks are put in canonical
   order, each run of non-starters sorted by class, stably; then for NFC and NFKC the
   canonical composition joins each character to the last starter unless something
   between blocks it, into the primary composites alone (the exclusions never made).
   The tables are src/js/normalize.h, from the Unicode Character Database 17.0
   (tools/gen-unicode.py --normalize).

   A quick check comes first (UAX #15's): a string whose every code point is in the form
   already, its marks in order, is the result as it is, nothing allocated. ASCII, and
   Latin-1 for NFC, never reach a table. Lone surrogates are code points of their own. */
#include <stdlib.h>
#include "js.h"
#include "normalize.h"

#define NM_SBASE 0xAC00u
#define NM_LBASE 0x1100u
#define NM_VBASE 0x1161u
#define NM_TBASE 0x11A7u
#define NM_LCOUNT 19u
#define NM_VCOUNT 21u
#define NM_TCOUNT 28u
#define NM_NCOUNT (NM_VCOUNT * NM_TCOUNT)
#define NM_SCOUNT (NM_LCOUNT * NM_NCOUNT)

enum { NM_NFC, NM_NFD, NM_NFKC, NM_NFKD };

/* the canonical combining class of c */
static uint32_t nm_ccc(uint32_t c) {
    if (c < 0x300) return 0;
    uint32_t lo = 0, hi = UNI_CCC_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (uni_ccc[3 * mid + 1] < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo < UNI_CCC_N && uni_ccc[3 * lo] <= c) return uni_ccc[3 * lo + 2];
    return 0;
}

/* is c in one of the n ranges of t? */
static int nm_in(const uint32_t *t, uint32_t n, uint32_t c) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (t[2 * mid + 1] < c) lo = mid + 1;
        else hi = mid;
    }
    return lo < n && t[2 * lo] <= c;
}

/* c's decomposition in t (n entries): its length (0 for none), its first code point's place in *at */
static uint32_t nm_find(const uint32_t *t, uint32_t n, uint32_t c, uint32_t *at) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if ((t[2 * mid] & 0xFFFFFFu) < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= n || (t[2 * lo] & 0xFFFFFFu) != c) return 0;
    *at = t[2 * lo + 1];
    return t[2 * lo] >> 24;
}

/* c decomposed in full onto out, with the compatibility mappings when compat */
static void nm_decompose(uint32_t c, int compat, vector<uint32_t> *out) {
    if (c - NM_SBASE < NM_SCOUNT) {
        uint32_t s = c - NM_SBASE;
        push_back(*out, NM_LBASE + s / NM_NCOUNT);
        push_back(*out, NM_VBASE + (s % NM_NCOUNT) / NM_TCOUNT);
        if (s % NM_TCOUNT) push_back(*out, NM_TBASE + s % NM_TCOUNT);
        return;
    }
    uint32_t at = 0, l = 0;
    if (c >= 0xA0) {
        if (compat) l = nm_find(uni_nfkd, UNI_NFKD_N, c, &at);
        if (!l && c >= 0xC0) l = nm_find(uni_nfd, UNI_NFD_N, c, &at);
    }
    if (!l) { push_back(*out, c); return; }
    for (uint32_t k = 0; k < l; k++) push_back(*out, uni_dec[at + k]);
}

/* the primary composite of a then b, 0 for none (Hangul's by their arithmetic) */
static uint32_t nm_compose(uint32_t a, uint32_t b) {
    if (a - NM_LBASE < NM_LCOUNT && b - NM_VBASE < NM_VCOUNT) return NM_SBASE + ((a - NM_LBASE) * NM_VCOUNT + (b - NM_VBASE)) * NM_TCOUNT;
    if (a - NM_SBASE < NM_SCOUNT && (a - NM_SBASE) % NM_TCOUNT == 0 && b - NM_TBASE - 1 < NM_TCOUNT - 1) return a + (b - NM_TBASE);
    uint32_t lo = 0, hi = UNI_COMP_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint32_t x = uni_comp[3 * mid], y = uni_comp[3 * mid + 1];
        if (x < a || (x == a && y < b)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < UNI_COMP_N && uni_comp[3 * lo] == a && uni_comp[3 * lo + 1] == b) return uni_comp[3 * lo + 2];
    return 0;
}

/* is c in the form already, whatever surrounds it (its quick check Yes)? */
static int nm_yes(uint32_t c, int form) {
    switch (form) {
    case NM_NFC: return !nm_in(uni_nfc_qc_n, UNI_NFC_QC_N_N, c) && !nm_in(uni_nfc_qc_m, UNI_NFC_QC_M_N, c);
    case NM_NFKC: return !nm_in(uni_nfkc_qc_n, UNI_NFKC_QC_N_N, c) && !nm_in(uni_nfkc_qc_m, UNI_NFKC_QC_M_N, c);
    default: break;
    }
    if (c - NM_SBASE < NM_SCOUNT) return 0;
    uint32_t at = 0;
    if (nm_find(uni_nfd, UNI_NFD_N, c, &at)) return 0;
    return form == NM_NFD || !nm_find(uni_nfkd, UNI_NFKD_N, c, &at);
}

/* the quick check (UAX #15 9.1) of the units u[0, n): n when they are in the form already,
   else where the last starter before the first unit it cannot pass begins. What comes
   before that starter is in the form whatever follows: a starter whose check is Yes
   neither joins what is before it nor lets what follows reach past it */
static uint32_t nm_quick(const uint16_t *u, uint32_t n, int form) {
    /* below these no code point changes, and none has a class */
    uint32_t fast = form == NM_NFC ? 0x300 : form == NM_NFD ? 0xC0 : 0xA0;
    uint32_t last = 0, safe = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = u[i], at = i;
        if (c < fast) { last = 0; safe = i; continue; }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00u);
            i++;
        }
        uint32_t cc = nm_ccc(c);
        if (cc && last > cc) return safe;
        if (!nm_yes(c, form)) return safe;
        if (!cc) safe = at;
        last = cc;
    }
    return n;
}

/* s in the normalization form the value form names (undefined: NFC); a RangeError for any other */
jv js_str_normalize(js_vm *vm, uint32_t s, jv formv) {
    int form = NM_NFC;
    if (formv != JV_UNDEF) {
        uint32_t f = js_to_string(vm, formv);
        if (!f) return JV_HOLE;
        js_str_flatten(vm, f);
        static const char *const names[] = { "NFC", "NFD", "NFKC", "NFKD" };
        int found = -1;
        for (int k = 0; k < 4 && found < 0; k++) {
            uint32_t l = (uint32_t)strlen(names[k]);
            if (vm->strs[f].len != l) continue;
            uint32_t j = 0;
            while (j < l && vm->units[vm->strs[f].off + j] == (uint16_t)names[k][j]) j++;
            if (j == l) found = k;
        }
        if (found < 0) return js_throw_msg(vm, vm->range_error_proto, "The normalization form should be one of NFC, NFD, NFKC, NFKD.");
        form = found;
    }
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    int compat = form == NM_NFKC || form == NM_NFKD;
    /* what the quick check passes is kept as it is; the rest decomposed */
    vector<uint16_t> u;
    vector<uint32_t> d;
    {
        const uint16_t *src = data(vm->units) + o;
        uint32_t safe = nm_quick(src, n, form);
        if (safe >= n) return jv_str(s);
        append_range(u, src, safe);
        for (uint32_t i = safe; i < n; i++) {
            uint32_t c = src[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00u);
                i++;
            }
            if (c < 0xA0) push_back(d, c);
            else nm_decompose(c, compat, &d);
        }
    }
    uint32_t m = (uint32_t)len(d);
    vector<uint32_t> cv;
    resize(cv, m, 0);
    {
        uint32_t *dp = data(d), *cc = data(cv);
        for (uint32_t i = 0; i < m; i++) cc[i] = nm_ccc(dp[i]);
        /* canonical order: each non-starter moved before the marks of a greater class */
        for (uint32_t i = 1; i < m; i++) {
            uint32_t c = dp[i], k = cc[i], j = i;
            if (!k) continue;
            while (j > 0 && cc[j - 1] > k) { dp[j] = dp[j - 1]; cc[j] = cc[j - 1]; j--; }
            dp[j] = c;
            cc[j] = k;
        }
        if ((form == NM_NFC || form == NM_NFKC) && m > 0) {
            /* canonical composition: each character joined to the last starter when nothing
               between blocks it (a mark of its class or greater, or a starter) */
            uint32_t starter = 0, sc = dp[0], last = cc[0] ? 256 : 0, out = 1;
            for (uint32_t i = 1; i < m; i++) {
                uint32_t c = dp[i], k = cc[i];
                /* no composite's second character is below U+0300 */
                uint32_t x = c >= 0x300 && (last < k || last == 0) ? nm_compose(sc, c) : 0;
                if (x) { dp[starter] = x; sc = x; continue; }
                if (!k) { starter = out; sc = c; }
                last = k;
                dp[out++] = c;
            }
            m = out;
        }
    }
    for (uint32_t i = 0; i < m; i++) {
        uint32_t c = d[i];
        if (c >= 0x10000) { push_back(u, (uint16_t)(0xD800 + ((c - 0x10000) >> 10))); push_back(u, (uint16_t)(0xDC00 + ((c - 0x10000) & 0x3FF))); }
        else push_back(u, (uint16_t)c);
    }
    return jv_str(js_str_new(vm, data(u), len(u)));
}
