/* src/js/intl.c -- Intl (ECMA-402): the locale-sensitive services, for the locales the
   web's pages use most.

   What is here, in this order: the locale data's reader (src/js/intl_data.c, generated
   by tools/gen-intl.py from CLDR's JSON); language tags (BCP 47 read as Unicode locale
   identifiers, canonicalized as UTS #35 and ICU do it, with CLDR's aliases, and their
   likely subtags); the locale lists and their resolution (each service's available
   locales, lookup matching, the Unicode extension's keys); the options' readers;
   Intl.Locale; the natives and their prototypes.

   An Intl object is a JK_INTL: a its kind (JIK_*), x its internal slots, an array of
   values (numbers and strings) its kind's slot names index. The default locale is en-US,
   as Chromium's headless shell reports it. */
#include <stdlib.h>
#include <math.h>
#include "js.h"
#include "intl_data.h"
#include "intl_tz.h"
#include "tz_data.h"

extern const uint32_t js_intl_db[];

/* the kinds of Intl object */
enum { JIK_LOCALE, JIK_COLLATOR, JIK_DATETIMEFORMAT, JIK_NUMBERFORMAT, JIK_PLURALRULES, JIK_RELATIVETIMEFORMAT,
       JIK_LISTFORMAT, JIK_DISPLAYNAMES, JIK_SEGMENTER, JIK_SEGMENTS, JIK_SEGMENT_ITERATOR, JIK_N };

/* the natives, numbered from js_intl_init's first; a native's x says which of a family */
enum {
    IN_GETCANONICALLOCALES, IN_SUPPORTEDVALUESOF,
    IN_SUPPORTED,                       /* a constructor's supportedLocalesOf: x its kind */
    IN_LOCALE, IN_LOC_TOSTRING, IN_LOC_MAXIMIZE, IN_LOC_MINIMIZE,
    IN_LOC_GET,                         /* Intl.Locale's accessors: x which (LG_*) */
    IN_NUMBERFORMAT, IN_NF_FORMAT_GET, IN_NF_FORMAT_BOUND, IN_NF_FORMATTOPARTS, IN_NF_FORMATRANGE,
    IN_NF_FORMATRANGETOPARTS, IN_NF_RESOLVED,
    IN_NUM_TOLOCALE, IN_BIG_TOLOCALE,
    IN_DATETIMEFORMAT, IN_DTF_FORMAT_GET, IN_DTF_FORMAT_BOUND, IN_DTF_FORMATTOPARTS, IN_DTF_FORMATRANGE,
    IN_DTF_FORMATRANGETOPARTS, IN_DTF_RESOLVED,
    IN_DATE_TOLOCALE,                   /* Date.prototype's toLocale*String: x which (0 to 2) */
    IN_PLURALRULES, IN_PR_SELECT, IN_PR_SELECTRANGE, IN_PR_RESOLVED,
    IN_LISTFORMAT, IN_LF_FORMAT, IN_LF_FORMATTOPARTS, IN_LF_RESOLVED,
    IN_RELTIMEFORMAT, IN_RT_FORMAT, IN_RT_FORMATTOPARTS, IN_RT_RESOLVED,
    IN_DISPLAYNAMES, IN_DN_OF, IN_DN_RESOLVED,
    IN_SEGMENTER, IN_SG_SEGMENT, IN_SG_RESOLVED, IN_SGM_CONTAINING, IN_SGM_ITERATOR, IN_SGI_NEXT,
    IN_COLLATOR, IN_CL_COMPARE_GET, IN_CL_COMPARE_BOUND, IN_CL_RESOLVED, IN_STR_LOCALECOMPARE,
    IN_COUNT
};
_Static_assert(IN_COUNT <= JIN_N, "Intl's natives fit their range");

/* Intl.Locale's accessors */
enum { LG_BASENAME, LG_CALENDAR, LG_CASEFIRST, LG_COLLATION, LG_HOURCYCLE, LG_NUMERIC, LG_NUMBERINGSYSTEM, LG_LANGUAGE,
       LG_SCRIPT, LG_REGION, LG_CALENDARS, LG_COLLATIONS, LG_HOURCYCLES, LG_NUMBERINGSYSTEMS, LG_TEXTINFO, LG_TIMEZONES,
       LG_WEEKINFO, LG_N };

/* ---- the data: bytes four to a word, strings NUL-ended ---- */

static uint32_t db8(uint32_t i) { return (js_intl_db[i >> 2] >> ((i & 3u) << 3)) & 0xFFu; }
static uint32_t db16(uint32_t i) { return db8(i) | db8(i + 1) << 8; }
static uint32_t db32(uint32_t i) { return db16(i) | db16(i + 2) << 16; }

/* the data's string at off against s[0..n): below 0, 0 or above as the data's sorts first */
static int db_cmp(uint32_t off, const char *s, size_t n) {
    for (size_t i = 0;; i++) {
        uint32_t c = db8(off + (uint32_t)i);
        uint32_t d = i < n ? (uint8_t)s[i] : 0;
        if (c != d) return c < d ? -1 : 1;
        if (!c) return 0;
    }
}
static size_t db_len(uint32_t off) {
    size_t n = 0;
    while (db8(off + (uint32_t)n)) n++;
    return n;
}
/* the data's string copied into buf (cut to cap - 1): its length */
static size_t db_copy(uint32_t off, char *buf, size_t cap) {
    size_t n = 0;
    for (uint32_t c; (c = db8(off + (uint32_t)n)) && n + 1 < cap; n++) buf[n] = (char)c;
    buf[n] = 0;
    return n;
}
/* a table of string pairs sorted by key: the value's offset for key, 0 when there is none */
static uint32_t db_find(uint32_t table, uint32_t count, const char *key, size_t n) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = db_cmp(db32(table + mid * 8), key, n);
        if (c == 0) return db32(table + mid * 8 + 4);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
static uint32_t db_find_z(uint32_t table, uint32_t count, const char *key) { return db_find(table, count, key, strlen(key)); }

/* the data's string as a JS string (UTF-8 to UTF-16) */
static uint32_t db_jstr(js_vm *vm, uint32_t off) {
    vector<char> b;
    for (uint32_t c; (c = db8(off)); off++) push_back(b, (char)c);
    return js_str_utf8(vm, data(b), len(b));
}

/* ---- text ---- */

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int all_of(const char *s, size_t n, int kind) {
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)s[i];
        if (kind == 0 ? !is_alpha(c) : kind == 1 ? !is_digit(c) : !is_alnum(c)) return 0;
    }
    return 1;
}
/* the subtags of unicode_locale_id (UTS #35) */
static int sub_lang(const char *s, size_t n) { return ((n >= 2 && n <= 3) || (n >= 5 && n <= 8)) && all_of(s, n, 0); }
static int sub_script(const char *s, size_t n) { return n == 4 && all_of(s, n, 0); }
static int sub_region(const char *s, size_t n) { return (n == 2 && all_of(s, n, 0)) || (n == 3 && all_of(s, n, 1)); }
static int sub_variant(const char *s, size_t n) {
    return (n >= 5 && n <= 8 && all_of(s, n, 2)) || (n == 4 && is_digit((unsigned char)s[0]) && all_of(s, n, 2));
}
static int sub_alnum(const char *s, size_t n, size_t lo, size_t hi) { return n >= lo && n <= hi && all_of(s, n, 2); }

/* a C string's room: s appended to buf of cap bytes, cut, kept NUL-ended */
static void cat(char *buf, size_t cap, const char *s) {
    size_t n = strlen(buf);
    while (*s && n + 1 < cap) buf[n++] = *s++;
    buf[n] = 0;
}
static void catn(char *buf, size_t cap, const char *s, size_t k) {
    size_t n = strlen(buf);
    for (size_t i = 0; i < k && n + 1 < cap; i++) buf[n++] = s[i];
    buf[n] = 0;
}
static void copyn(char *buf, size_t cap, const char *s, size_t k) {
    buf[0] = 0;
    catn(buf, cap, s, k);
}
static void copyz(char *buf, size_t cap, const char *s) { copyn(buf, cap, s, strlen(s)); }
/* a, or b when a is empty, copied into buf */
static void pick(char *buf, size_t cap, const char *a, const char *b) {
    if (a[0]) copyz(buf, cap, a);
    else copyz(buf, cap, b);
}
/* s appended, or w when s is empty */
static void cat_or(char *buf, size_t cap, const char *s, const char *w) {
    if (s[0]) cat(buf, cap, s);
    else cat(buf, cap, w);
}

/* a JS string's text, ASCII alone, into buf: its length, or -1 when a unit is past
   ASCII or it does not fit */
static int str_ascii(js_vm *vm, uint32_t s, char *buf, size_t cap) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    if (n + 1 > cap) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[o + i];
        if (c >= 128 || c == 0) return -1;
        buf[i] = (char)c;
    }
    buf[n] = 0;
    return (int)n;
}

/* ---- language tags ---- */

#define TAG_MAX 255
#define T_VARS 8
#define T_KWS 24
#define T_EXTS 8

/* a tag read: its language (lowercase, "" for und), script (titlecase), region
   (uppercase), variants (lowercase), the Unicode extension's attributes and keywords
   (each a key and its value, the types joined by '-', "" for none), the transformed
   extension's text, the other extensions (a singleton, its text), the private use */
typedef struct itag {
    char lang[9], script[5], region[4];
    char var[T_VARS][9];
    uint32_t nvar;
    int has_u;
    char attr[T_KWS][9];
    uint32_t nattr;
    char key[T_KWS][3];
    char val[T_KWS][72];
    uint32_t nkw;
    char tx[TAG_MAX + 1];
    char ext_c[T_EXTS];
    char ext[T_EXTS][TAG_MAX + 1];
    uint32_t next;
    char priv[TAG_MAX + 1];
} itag;

static void tag_clear(itag *t) { memset(t, 0, sizeof *t); }

/* a tag's subtags: offsets and lengths into its text */
typedef struct toks { uint16_t off[TAG_MAX + 1], len[TAG_MAX + 1]; uint32_t n; } toks;

/* split at '-'; getline's way when getline is set (no last empty subtag) */
static void tokenize(const char *s, size_t n, toks *k, int getline) {
    k->n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '-') {
            if (i == n && getline && i == start) break;
            k->off[k->n] = (uint16_t)start;
            k->len[k->n] = (uint16_t)(i - start);
            k->n++;
            start = i + 1;
        }
    }
}

/* V8's first check (JSLocale::StartsWithUnicodeLanguageId): the subtags before the first
   singleton make a unicode_language_id */
static int starts_with_language_id(const char *s, size_t n) {
    toks k;
    tokenize(s, n, &k, 1);
    if (!k.n || !sub_lang(s + k.off[0], k.len[0])) return 0;
    if (k.n == 1) return 1;
    if (k.len[1] == 1 && is_alnum((unsigned char)s[k.off[1]])) return 1;
    uint32_t i = 1;
    if (sub_script(s + k.off[i], k.len[i])) {
        i++;
        if (i == k.n) return 1;
    }
    if (sub_region(s + k.off[i], k.len[i])) i++;
    for (; i < k.n; i++) {
        if (k.len[i] == 1 && is_alnum((unsigned char)s[k.off[i]])) return 1;
        if (!sub_variant(s + k.off[i], k.len[i])) return 0;
    }
    return 1;
}

static int find_var(const itag *t, const char *v) {
    for (uint32_t i = 0; i < t->nvar; i++) if (!strcmp(t->var[i], v)) return (int)i;
    return -1;
}
static int find_kw(const itag *t, const char *key) {
    for (uint32_t i = 0; i < t->nkw; i++) if (!strcmp(t->key[i], key)) return (int)i;
    return -1;
}

/* a lowercased tag's unicode_language_id from subtag *i (tlang's grammar when tl: no
   script alone, the language required) into lang/script/region/variants; 0 when malformed */
static int parse_langid(const char *s, const toks *k, uint32_t *i, itag *t) {
    uint32_t j = *i;
    if (j >= k->n || !sub_lang(s + k->off[j], k->len[j])) return 0;
    copyn(t->lang, sizeof t->lang, s + k->off[j], k->len[j]);
    j++;
    if (j < k->n && sub_script(s + k->off[j], k->len[j])) {
        copyn(t->script, sizeof t->script, s + k->off[j], 4);
        j++;
    }
    if (j < k->n && sub_region(s + k->off[j], k->len[j])) {
        copyn(t->region, sizeof t->region, s + k->off[j], k->len[j]);
        j++;
    }
    while (j < k->n && sub_variant(s + k->off[j], k->len[j])) {
        char v[9];
        copyn(v, sizeof v, s + k->off[j], k->len[j]);
        if (find_var(t, v) >= 0 || t->nvar >= T_VARS) return 0;
        memcpy(t->var[t->nvar++], v, sizeof v);
        j++;
    }
    *i = j;
    return 1;
}

/* a tag, lowercased, read as unicode_locale_id: 0 well formed, else 2 (ICU refuses it) */
static int tag_parse_lc(const char *s, size_t n, itag *t) {
    toks k;
    tag_clear(t);
    tokenize(s, n, &k, 0);
    for (uint32_t i = 0; i < k.n; i++) if (k.len[i] == 0 || k.len[i] > 8) return 2;
    uint32_t i = 0;
    if (!parse_langid(s, &k, &i, t)) return 2;
    if (!strcmp(t->lang, "root")) t->lang[0] = 0;
    char seen[40];
    uint32_t nseen = 0;
    while (i < k.n) {
        if (k.len[i] != 1) return 2;
        char c = s[k.off[i]];
        for (uint32_t q = 0; q < nseen; q++) if (seen[q] == c) return 2;
        seen[nseen++] = c;
        i++;
        if (c == 'x') {
            /* the private use: what is left, subtags of 1 to 8 */
            if (i >= k.n) return 2;
            for (; i < k.n; i++) {
                if (!sub_alnum(s + k.off[i], k.len[i], 1, 8)) return 2;
                if (t->priv[0]) cat(t->priv, sizeof t->priv, "-");
                catn(t->priv, sizeof t->priv, s + k.off[i], k.len[i]);
            }
            break;
        }
        if (c == 'u') {
            t->has_u = 1;
            uint32_t start = i;
            while (i < k.n && k.len[i] >= 3 && sub_alnum(s + k.off[i], k.len[i], 3, 8)) {
                char a[9];
                copyn(a, sizeof a, s + k.off[i], k.len[i]);
                int dup = 0;
                for (uint32_t q = 0; q < t->nattr; q++) if (!strcmp(t->attr[q], a)) dup = 1;
                if (!dup) {
                    if (t->nattr >= T_KWS) return 2;
                    memcpy(t->attr[t->nattr++], a, sizeof a);
                }
                i++;
            }
            while (i < k.n && k.len[i] == 2) {
                const char *p = s + k.off[i];
                if (!is_alnum((unsigned char)p[0]) || !is_alpha((unsigned char)p[1])) return 2;
                char key[3] = { p[0], p[1], 0 };
                i++;
                char v[72];
                v[0] = 0;
                while (i < k.n && k.len[i] >= 3) {
                    if (!sub_alnum(s + k.off[i], k.len[i], 3, 8)) return 2;
                    if (v[0]) cat(v, sizeof v, "-");
                    catn(v, sizeof v, s + k.off[i], k.len[i]);
                    i++;
                }
                if (find_kw(t, key) >= 0) continue;          /* a repeated key: the first kept */
                if (t->nkw >= T_KWS) return 2;
                memcpy(t->key[t->nkw], key, 3);
                memcpy(t->val[t->nkw], v, sizeof v);
                t->nkw++;
            }
            if (i == start) return 2;
            continue;
        }
        if (c == 't') {
            uint32_t start = i;
            char tx[TAG_MAX + 1];
            tx[0] = 0;
            if (i < k.n && sub_lang(s + k.off[i], k.len[i])) {
                itag tl;
                tag_clear(&tl);
                if (!parse_langid(s, &k, &i, &tl)) return 2;
                copyn(tx, sizeof tx, s + k.off[start], (size_t)(k.off[i - 1] + k.len[i - 1] - k.off[start]));
            }
            while (i < k.n && k.len[i] == 2 && is_alpha((unsigned char)s[k.off[i]]) && is_digit((unsigned char)s[k.off[i] + 1])) {
                uint32_t f = i;
                i++;
                if (i >= k.n || !sub_alnum(s + k.off[i], k.len[i], 3, 8)) return 2;
                while (i < k.n && k.len[i] >= 3 && sub_alnum(s + k.off[i], k.len[i], 3, 8)) i++;
                if (tx[0]) cat(tx, sizeof tx, "-");
                catn(tx, sizeof tx, s + k.off[f], (size_t)(k.off[i - 1] + k.len[i - 1] - k.off[f]));
            }
            if (i == start) return 2;
            memcpy(t->tx, tx, sizeof tx);
            continue;
        }
        /* another extension: subtags of 2 to 8 */
        uint32_t start = i;
        while (i < k.n && k.len[i] >= 2 && sub_alnum(s + k.off[i], k.len[i], 2, 8)) i++;
        if (i == start || t->next >= T_EXTS) return 2;
        t->ext_c[t->next] = c;
        copyn(t->ext[t->next], sizeof t->ext[0], s + k.off[start], (size_t)(k.off[i - 1] + k.len[i - 1] - k.off[start]));
        t->next++;
    }
    return 0;
}

/* the likely subtags: the value for a key (en, und-Latn, zh-TW) as a full tag, into out */
static int likely_lookup(const char *key, char *out, size_t cap) {
    uint32_t v = db_find_z(IDB_LIKELY, IDB_LIKELY_N, key);
    if (!v) return 0;
    out[0] = 0;
    if (db8(v) == '-') {
        const char *dash = strchr(key, '-');
        catn(out, cap, key, dash ? (size_t)(dash - key) : strlen(key));
    }
    char rest[40];
    db_copy(v, rest, sizeof rest);
    cat(out, cap, rest);
    return 1;
}

/* a likely tag's three parts (lang-Script-REGION) */
static void split3(const char *s, char *lang, char *script, char *region) {
    lang[0] = script[0] = region[0] = 0;
    const char *a = strchr(s, '-');
    if (!a) { copyn(lang, 9, s, strlen(s)); return; }
    copyn(lang, 9, s, (size_t)(a - s));
    const char *b = strchr(a + 1, '-');
    if (!b) return;
    copyn(script, 5, a + 1, (size_t)(b - a - 1));
    copyn(region, 4, b + 1, strlen(b + 1));
}

/* UTS #35's Add Likely Subtags on a tag's language, script and region: 0 when nothing matched */
static int add_likely(const char *lang, const char *script, const char *region, char *ol, char *os, char *orr) {
    char key[32], found[40];
    int got = 0;
    for (int pass = 0; pass < 5 && !got; pass++) {
        key[0] = 0;
        if (pass == 0 && script[0] && region[0]) { cat_or(key, 32, lang, "und"); cat(key, 32, "-"); cat(key, 32, script); cat(key, 32, "-"); cat(key, 32, region); }
        else if (pass == 1 && region[0]) { cat_or(key, 32, lang, "und"); cat(key, 32, "-"); cat(key, 32, region); }
        else if (pass == 2 && script[0]) { cat_or(key, 32, lang, "und"); cat(key, 32, "-"); cat(key, 32, script); }
        else if (pass == 3) cat_or(key, 32, lang, "und");
        else if (pass == 4 && script[0]) { cat(key, 32, "und-"); cat(key, 32, script); }
        if (key[0]) got = likely_lookup(key, found, sizeof found);
    }
    if (!got) return 0;
    char fl[9], fs[5], fr[4];
    split3(found, fl, fs, fr);
    pick(ol, 9, lang, fl);
    pick(os, 5, script, fs);
    pick(orr, 4, region, fr);
    return 1;
}

/* an alias replacement's parts ("sr-Latn", "und-AX", "en-GB-oxendict") */
static void parse_replacement(const char *r, char *lang, char *script, char *region, char *variant) {
    lang[0] = script[0] = region[0] = variant[0] = 0;
    toks k;
    tokenize(r, strlen(r), &k, 1);
    for (uint32_t i = 0; i < k.n; i++) {
        const char *p = r + k.off[i];
        size_t n = k.len[i];
        if (i == 0) copyn(lang, 9, p, n);
        else if (sub_script(p, n) && !script[0] && !region[0]) copyn(script, 5, p, n);
        else if (sub_region(p, n) && !region[0]) copyn(region, 4, p, n);
        else if (sub_variant(p, n)) copyn(variant, 9, p, n);
    }
}

static int cmp_str(const char *a, const char *b) { return strcmp(a, b); }

static void sort_vars(itag *t) {
    for (uint32_t i = 1; i < t->nvar; i++)
        for (uint32_t j = i; j > 0 && cmp_str(t->var[j - 1], t->var[j]) > 0; j--) {
            char x[9];
            memcpy(x, t->var[j], 9);
            memcpy(t->var[j], t->var[j - 1], 9);
            memcpy(t->var[j - 1], x, 9);
        }
}

static void remove_var(itag *t, uint32_t i) {
    for (uint32_t j = i; j + 1 < t->nvar; j++) memcpy(t->var[j], t->var[j + 1], 9);
    t->nvar--;
}

/* ICU's language alias step (AliasReplacer::replaceLanguage): the rule keyed by the
   language (or und), the region, one variant, as asked; 1 when it changed the tag */
static int replace_language(itag *t, int check_lang, int check_region, int check_vars) {
    if ((check_region && !t->region[0]) || (check_vars && !t->nvar)) return 0;
    uint32_t nv = check_vars ? t->nvar : 1;
    for (uint32_t vi = 0; vi < nv; vi++) {
        char key[40];
        key[0] = 0;
        if (check_lang) cat_or(key, 40, t->lang, "und");
        else cat(key, 40, "und");
        if (check_region) { cat(key, 40, "_"); cat(key, 40, t->region); }
        if (check_vars) { cat(key, 40, "_"); cat(key, 40, t->var[vi]); }
        uint32_t r = db_find_z(IDB_LANG_ALIAS, IDB_LANG_ALIAS_N, key);
        if (!r) continue;
        char rep[64], rl[9], rs[5], rr[4], rv[9];
        db_copy(r, rep, sizeof rep);
        parse_replacement(rep, rl, rs, rr, rv);
        /* a field the rule names is replaced (or deleted); one it does not name is kept,
           or taken from the replacement when the tag has none (ICU's deleteOrReplace) */
        char nl[9], ns[5], nr[4];
        if (!strcmp(rl, "und")) copyz(nl, 9, t->lang);
        else copyz(nl, 9, rl);
        pick(ns, 5, t->script, rs);
        if (check_region) copyz(nr, 4, rr);
        else pick(nr, 4, t->region, rr);
        int var_same = !check_vars || !strcmp(rv, t->var[vi]);
        if (!strcmp(nl, t->lang) && !strcmp(ns, t->script) && !strcmp(nr, t->region) && var_same) continue;
        memcpy(t->lang, nl, 9);
        memcpy(t->script, ns, 5);
        memcpy(t->region, nr, 4);
        if (check_vars) {
            if (rv[0]) memcpy(t->var[vi], rv, 9);
            else remove_var(t, vi);
        }
        return 1;
    }
    return 0;
}

/* a region alias: one region, or of several the one the language's likely region is */
static int replace_region(itag *t) {
    if (!t->region[0]) return 0;
    uint32_t r = db_find_z(IDB_REGION_ALIAS, IDB_REGION_ALIAS_N, t->region);
    if (!r) return 0;
    char rep[128];
    db_copy(r, rep, sizeof rep);
    char pick[8];
    const char *sp = strchr(rep, ' ');
    copyn(pick, sizeof pick, rep, sp ? (size_t)(sp - rep) : strlen(rep));
    if (sp) {
        char ll[9], ls[5], lr[4], none[1];
        none[0] = 0;
        if (add_likely(t->lang, t->script, none, ll, ls, lr) && lr[0]) {
            size_t n = strlen(rep), m = strlen(lr);
            for (size_t i = 0; i + m <= n; i++)
                if ((i == 0 || rep[i - 1] == ' ') && !memcmp(rep + i, lr, m) && (i + m == n || rep[i + m] == ' ')) { copyn(pick, sizeof pick, lr, m); break; }
        }
    }
    if (!strcmp(pick, t->region)) return 0;
    copyn(t->region, 4, pick, strlen(pick));
    return 1;
}

static int replace_script(itag *t) {
    if (!t->script[0]) return 0;
    uint32_t r = db_find_z(IDB_SCRIPT_ALIAS, IDB_SCRIPT_ALIAS_N, t->script);
    if (!r) return 0;
    char rep[8];
    db_copy(r, rep, sizeof rep);
    if (!strcmp(rep, t->script)) return 0;
    copyn(t->script, 5, rep, strlen(rep));
    return 1;
}

static int replace_variant(itag *t) {
    for (uint32_t i = 0; i < t->nvar; i++) {
        uint32_t r = db_find_z(IDB_VARIANT_ALIAS, IDB_VARIANT_ALIAS_N, t->var[i]);
        if (!r) continue;
        char rep[9];
        db_copy(r, rep, sizeof rep);
        if (!strcmp(rep, t->var[i])) continue;
        int heploc = !strcmp(t->var[i], "heploc");
        copyn(t->var[i], 9, rep, strlen(rep));
        if (heploc) {
            int h = find_var(t, "hepburn");
            if (h >= 0) remove_var(t, (uint32_t)h);
        }
        return 1;
    }
    return 0;
}

/* a Unicode extension keyword's value canonicalized: "yes" and "true" are no value; an
   old name its new one */
static void canon_kw(const char *key, char *val, size_t cap) {
    if (!strcmp(val, "yes") || !strcmp(val, "true")) { val[0] = 0; return; }
    char k[96];
    k[0] = 0;
    cat(k, sizeof k, key);
    cat(k, sizeof k, "-");
    cat(k, sizeof k, val);
    uint32_t r = db_find_z(IDB_UEXT_ALIAS, IDB_UEXT_ALIAS_N, k);
    if (r) db_copy(r, val, cap);
}

static void case_parts(itag *t) {
    if (t->script[0]) {
        t->script[0] = (char)upper(t->script[0]);
        for (int i = 1; t->script[i]; i++) t->script[i] = (char)lower(t->script[i]);
    }
    for (int i = 0; t->region[i]; i++) t->region[i] = (char)upper(t->region[i]);
}

static void tag_write(const itag *t, char *out, size_t cap);

/* the transformed extension's tlang and fields canonicalized: the tlang as a tag (its
   aliases replaced, lowercased), the fields sorted by key */
static void canon_tx(char *tx, size_t cap);

/* UTS #35's canonicalization as ICU makes it: case, the aliases until none applies, the
   variants sorted, the extensions' keys sorted and their values canonical */
static void tag_canon(itag *t) {
    case_parts(t);
    /* posix, a variant, is ICU's u-va-posix */
    int px = find_var(t, "posix");
    if (px >= 0 && t->nvar == 1) {
        remove_var(t, (uint32_t)px);
        if (find_kw(t, "va") < 0 && t->nkw < T_KWS) {
            t->has_u = 1;
            memcpy(t->key[t->nkw], "va", 3);
            copyn(t->val[t->nkw], sizeof t->val[0], "posix", 5);
            t->nkw++;
        }
    }
    sort_vars(t);
    for (int guard = 0; guard < 20; guard++) {
        if (replace_language(t, 1, 1, 1) || replace_language(t, 1, 1, 0) || replace_language(t, 1, 0, 1) ||
            replace_language(t, 1, 0, 0) || replace_language(t, 0, 0, 1) || replace_region(t) || replace_script(t) ||
            replace_variant(t))
            continue;
        break;
    }
    if (!strcmp(t->lang, "und")) t->lang[0] = 0;
    sort_vars(t);
    /* the extensions: attributes and keys sorted, values canonical */
    for (uint32_t i = 1; i < t->nattr; i++)
        for (uint32_t j = i; j > 0 && strcmp(t->attr[j - 1], t->attr[j]) > 0; j--) {
            char x[9];
            memcpy(x, t->attr[j], 9);
            memcpy(t->attr[j], t->attr[j - 1], 9);
            memcpy(t->attr[j - 1], x, 9);
        }
    for (uint32_t i = 0; i < t->nkw; i++) canon_kw(t->key[i], t->val[i], sizeof t->val[0]);
    for (uint32_t i = 1; i < t->nkw; i++)
        for (uint32_t j = i; j > 0 && strcmp(t->key[j - 1], t->key[j]) > 0; j--) {
            char k[3], v[72];
            memcpy(k, t->key[j], 3);
            memcpy(v, t->val[j], 72);
            memcpy(t->key[j], t->key[j - 1], 3);
            memcpy(t->val[j], t->val[j - 1], 72);
            memcpy(t->key[j - 1], k, 3);
            memcpy(t->val[j - 1], v, 72);
        }
    for (uint32_t i = 1; i < t->next; i++)
        for (uint32_t j = i; j > 0 && t->ext_c[j - 1] > t->ext_c[j]; j--) {
            char c = t->ext_c[j];
            t->ext_c[j] = t->ext_c[j - 1];
            t->ext_c[j - 1] = c;
            char x[TAG_MAX + 1];
            memcpy(x, t->ext[j], sizeof x);
            memcpy(t->ext[j], t->ext[j - 1], sizeof x);
            memcpy(t->ext[j - 1], x, sizeof x);
        }
    if (t->tx[0]) canon_tx(t->tx, sizeof t->tx);
}

static void canon_tx(char *tx, size_t cap) {
    toks k;
    tokenize(tx, strlen(tx), &k, 1);
    char out[TAG_MAX + 1];
    out[0] = 0;
    uint32_t i = 0;
    if (k.n && sub_lang(tx + k.off[0], k.len[0])) {
        itag tl;
        tag_clear(&tl);
        if (parse_langid(tx, &k, &i, &tl)) {
            tag_canon(&tl);
            char w[TAG_MAX + 1];
            tag_write(&tl, w, sizeof w);
            for (char *p = w; *p; p++) *p = (char)lower(*p);
            cat(out, sizeof out, w);
        }
    }
    /* the fields, each a key and its values, sorted by key */
    char fk[16][3];
    char fv[16][TAG_MAX + 1];
    uint32_t nf = 0;
    while (i < k.n && nf < 16) {
        copyn(fk[nf], 3, tx + k.off[i], 2);
        uint32_t f = ++i;
        while (i < k.n && k.len[i] >= 3) i++;
        copyn(fv[nf], sizeof fv[0], tx + k.off[f], i > f ? (size_t)(k.off[i - 1] + k.len[i - 1] - k.off[f]) : 0);
        nf++;
    }
    for (uint32_t a = 1; a < nf; a++)
        for (uint32_t b = a; b > 0 && strcmp(fk[b - 1], fk[b]) > 0; b--) {
            char x[3], y[TAG_MAX + 1];
            memcpy(x, fk[b], 3);
            memcpy(y, fv[b], sizeof y);
            memcpy(fk[b], fk[b - 1], 3);
            memcpy(fv[b], fv[b - 1], sizeof y);
            memcpy(fk[b - 1], x, 3);
            memcpy(fv[b - 1], y, sizeof y);
        }
    for (uint32_t a = 0; a < nf; a++) {
        if (out[0]) cat(out, sizeof out, "-");
        cat(out, sizeof out, fk[a]);
        cat(out, sizeof out, "-");
        cat(out, sizeof out, fv[a]);
    }
    copyn(tx, cap, out, strlen(out));
}

/* the base name: language, script, region, variants */
static void tag_base(const itag *t, char *out, size_t cap) {
    out[0] = 0;
    cat_or(out, cap, t->lang, "und");
    if (t->script[0]) { cat(out, cap, "-"); cat(out, cap, t->script); }
    if (t->region[0]) { cat(out, cap, "-"); cat(out, cap, t->region); }
    for (uint32_t i = 0; i < t->nvar; i++) { cat(out, cap, "-"); cat(out, cap, t->var[i]); }
}

/* the u extension's text ("u-ca-gregory"), "" when it has nothing */
static void tag_u(const itag *t, char *out, size_t cap) {
    out[0] = 0;
    if (!t->nattr && !t->nkw) return;
    cat(out, cap, "u");
    for (uint32_t i = 0; i < t->nattr; i++) { cat(out, cap, "-"); cat(out, cap, t->attr[i]); }
    for (uint32_t i = 0; i < t->nkw; i++) {
        cat(out, cap, "-");
        cat(out, cap, t->key[i]);
        if (t->val[i][0]) { cat(out, cap, "-"); cat(out, cap, t->val[i]); }
    }
}

static void tag_write(const itag *t, char *out, size_t cap) {
    tag_base(t, out, cap);
    /* the extensions by singleton: the others, t and u among them in order, x last */
    char u[TAG_MAX + 1];
    tag_u(t, u, sizeof u);
    int t_done = !t->tx[0], u_done = !u[0];
    for (uint32_t i = 0; i <= t->next; i++) {
        char c = i < t->next ? t->ext_c[i] : 'x';
        if (!t_done && 't' < c) { cat(out, cap, "-t-"); cat(out, cap, t->tx); t_done = 1; }
        if (!u_done && 'u' < c) { cat(out, cap, "-"); cat(out, cap, u); u_done = 1; }
        if (i < t->next) {
            char sing[4] = { '-', c, '-', 0 };
            cat(out, cap, sing);
            cat(out, cap, t->ext[i]);
        }
    }
    if (t->priv[0]) { cat(out, cap, "-x-"); cat(out, cap, t->priv); }
}

/* a tag's text read and canonicalized: 0, or 1 (V8's "Incorrect locale information
   provided") or 2 (ICU refused it: "Invalid language tag") */
static int tag_from_text(const char *s, size_t n, itag *t) {
    if (!starts_with_language_id(s, n)) return 1;
    char lc[TAG_MAX + 1];
    if (n > TAG_MAX) return 2;
    for (size_t i = 0; i < n; i++) lc[i] = (char)lower((unsigned char)s[i]);
    lc[n] = 0;
    int r = tag_parse_lc(lc, n, t);
    if (r) return r;
    tag_canon(t);
    return 0;
}

/* the error a tag's reading gave, thrown */
static jv tag_error(js_vm *vm, int r, const char *s, size_t n) {
    if (r == 1) return js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
    char m[TAG_MAX + 40];
    m[0] = 0;
    cat(m, sizeof m, "Invalid language tag: ");
    size_t at = strlen(m);
    for (size_t i = 0; i < n && at + 1 < sizeof m; i++) m[at++] = (char)lower((unsigned char)s[i]);
    m[at] = 0;
    return js_throw_msg(vm, vm->range_error_proto, m);
}

/* a JS string as a canonical tag, into t: 0 with a RangeError thrown */
static int tag_from_js(js_vm *vm, uint32_t s, itag *t) {
    char buf[TAG_MAX + 1];
    int n = str_ascii(vm, s, buf, sizeof buf);
    if (n < 0) {
        /* past ASCII, or too long: V8's first check refuses the former */
        js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
        return 0;
    }
    int r = tag_from_text(buf, (size_t)n, t);
    if (r) { tag_error(vm, r, buf, (size_t)n); return 0; }
    return 1;
}

/* ---- Intl objects ---- */

static int is_intl(js_vm *vm, jv v, uint32_t kind) {
    return jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_INTL && vm->objs[jv_idx(v)].a == kind;
}
static uint32_t intl_new(js_vm *vm, uint32_t kind, uint32_t proto, uint32_t nslots) {
    uint32_t o = js_obj_new(vm, JK_INTL, proto);
    uint32_t s = js_array_new(vm, nslots);
    for (uint32_t i = 0; i < nslots; i++) js_array_set(vm, s, i, JV_UNDEF);
    vm->objs[o].a = kind;
    vm->objs[o].x = jv_obj(s);
    return o;
}
static jv slot_get(js_vm *vm, uint32_t o, uint32_t i) { return js_array_get(vm, jv_idx(vm->objs[o].x), i); }
static void slot_set(js_vm *vm, uint32_t o, uint32_t i, jv v) { js_array_set(vm, jv_idx(vm->objs[o].x), i, v); }

/* an Intl.Locale's slots */
enum { LS_TAG, LS_N };

/* ---- locale lists (ECMA-402 9.2) ---- */

/* CanonicalizeLocaleList: the canonical tags, deduplicated, an array of strings; 0 thrown */
static uint32_t canonical_list(js_vm *vm, jv locales) {
    uint32_t out = js_array_new(vm, 0);
    if (locales == JV_UNDEF) return out;
    jv o;
    if (jv_is_str(locales) || is_intl(vm, locales, JIK_LOCALE)) {
        uint32_t a = js_array_new(vm, 0);
        js_array_push(vm, a, locales);
        o = jv_obj(a);
    } else {
        o = js_to_object(vm, locales);
        if (vm->throwing) return 0;
    }
    jv lv = js_get(vm, o, A_LENGTH);
    if (vm->throwing) return 0;
    double n = js_to_number(vm, lv);
    if (vm->throwing) return 0;
    if (n != n || n < 0) n = 0;
    if (n > 4294967295.0) n = 4294967295.0;
    for (double k = 0; k < n; k++) {
        uint32_t key = js_atom(vm, js_num_to_str(vm, k));
        if (!js_has(vm, jv_idx(o), key)) continue;
        jv v = js_get(vm, o, key);
        if (vm->throwing) return 0;
        if (!jv_is_str(v) && !jv_is_obj(v)) {
            js_throw_msg(vm, vm->type_error_proto, "Language ID should be string or object.");
            return 0;
        }
        uint32_t s;
        if (is_intl(vm, v, JIK_LOCALE)) s = jv_idx(slot_get(vm, jv_idx(v), LS_TAG));
        else {
            s = js_to_string(vm, v);
            if (!s) return 0;
        }
        itag t;
        if (!tag_from_js(vm, s, &t)) return 0;
        char w[TAG_MAX + 1];
        tag_write(&t, w, sizeof w);
        uint32_t ws = js_str_ascii(vm, w);
        int dup = 0;
        for (uint32_t i = 0; i < vm->objs[out].b && !dup; i++) dup = js_str_eq(vm, jv_idx(js_array_get(vm, out, i)), ws);
        if (!dup) js_array_push(vm, out, jv_str(ws));
    }
    return out;
}

/* the available locales of a service: the general list, the collator's, the plural rules' */
enum { AV_GENERAL, AV_COLLATOR, AV_PLURAL };

/* a locale's record in the data (16 bytes: its tag, its parent's index or 0xFFFF for
   root, its flags, its resources' table and their count), by its exact tag: its index,
   -1 for none */
#define LOC_REC 16u
static int locale_index(const char *tag) {
    uint32_t lo = 0, hi = IDB_LOCALES_N;
    size_t n = strlen(tag);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = db_cmp(db32(IDB_LOCALES + mid * LOC_REC), tag, n);
        if (c == 0) return (int)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}
static uint32_t locale_parent(uint32_t li) { return db16(IDB_LOCALES + li * LOC_REC + 4); }
static uint32_t locale_flags(uint32_t li) { return db16(IDB_LOCALES + li * LOC_REC + 6); }
static uint32_t locale_res(uint32_t li) { return db32(IDB_LOCALES + li * LOC_REC + 8); }
static uint32_t locale_nres(uint32_t li) { return db32(IDB_LOCALES + li * LOC_REC + 12); }

/* a resources table's value for a key (pairs of words sorted by key): 0 when absent */
static uint32_t res_in(uint32_t table, uint32_t n, uint32_t key) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, k = db32(table + mid * 8);
        if (k == key) return db32(table + mid * 8 + 4);
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
/* a locale's record: its own, its nearest ancestor's, else root's; 0 when none has it */
static uint32_t rec_get(int li, uint32_t key) {
    for (int guard = 0; li >= 0 && li != 0xFFFF && guard < 32; guard++) {
        uint32_t v = res_in(locale_res((uint32_t)li), locale_nres((uint32_t)li), key);
        if (v) return v;
        li = (int)locale_parent((uint32_t)li);
    }
    return res_in(IDB_ROOT_RES, IDB_ROOT_RES_N, key);
}
#define RKEY(cat, i) ((uint32_t)(cat) << 20 | (uint32_t)(i))
/* field i of a record (fields are joined by 0x1F): its first byte, *n its length */
static uint32_t fld(uint32_t off, uint32_t i, uint32_t *n) {
    uint32_t p = off;
    for (uint32_t k = 0; k < i; k++) {
        while (db8(p) && db8(p) != 0x1F) p++;
        if (!db8(p)) { *n = 0; return p; }
        p++;
    }
    uint32_t q = p;
    while (db8(q) && db8(q) != 0x1F) q++;
    *n = q - p;
    return p;
}
/* the data's UTF-8 bytes [p, p + n) as UTF-16 units into buf (cut to cap): their count */
static uint32_t u8_units(uint32_t p, uint32_t n, uint16_t *buf, uint32_t cap) {
    uint32_t k = 0, end = p + n;
    while (p < end && k + 2 <= cap) {
        uint32_t c = db8(p), cp, w = 1;
        if (c < 0x80) cp = c;
        else if (c < 0xE0) { cp = (c & 0x1F) << 6 | (db8(p + 1) & 0x3F); w = 2; }
        else if (c < 0xF0) { cp = (c & 0x0F) << 12 | (db8(p + 1) & 0x3F) << 6 | (db8(p + 2) & 0x3F); w = 3; }
        else { cp = (c & 0x07) << 18 | (db8(p + 1) & 0x3F) << 12 | (db8(p + 2) & 0x3F) << 6 | (db8(p + 3) & 0x3F); w = 4; }
        if (cp >= 0x10000) { buf[k++] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10)); buf[k++] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
        else buf[k++] = (uint16_t)cp;
        p += w;
    }
    return k;
}
/* a record's field i as units into buf: their count (0 for an empty or absent field) */
static uint32_t fld_units(uint32_t rec, uint32_t i, uint16_t *buf, uint32_t cap) {
    if (!rec) return 0;
    uint32_t n;
    uint32_t p = fld(rec, i, &n);
    return u8_units(p, n, buf, cap);
}

static int available(uint32_t av, const char *tag) {
    if (av == AV_PLURAL) return db_find_z(IDB_PLURAL_LOCALES, IDB_PLURAL_LOCALES_N, tag) != 0;
    int li = locale_index(tag);
    if (li < 0) return 0;
    return av == AV_COLLATOR ? (locale_flags((uint32_t)li) & 2) != 0 : 1;
}

/* BestAvailableLocale: the tag truncated until it is available (a singleton dropped with
   what follows it); into out, 0 when none is */
static int best_available(uint32_t av, const char *tag, char *out, size_t cap) {
    char c[TAG_MAX + 1];
    copyn(c, sizeof c, tag, strlen(tag));
    for (;;) {
        if (available(av, c)) { copyn(out, cap, c, strlen(c)); return 1; }
        char *dash = strrchr(c, '-');
        if (!dash) return 0;
        size_t pos = (size_t)(dash - c);
        if (pos >= 2 && c[pos - 2] == '-') pos -= 2;
        c[pos] = 0;
    }
}

/* a canonical tag without its Unicode extension (its singletons are lowercase): the
   subtags from "-u-" to the next singleton cut */
static void strip_u(const char *tag, char *out, size_t cap) {
    size_t n = strlen(tag), at = n;
    for (size_t i = 0; i + 2 < n; i++)
        if (tag[i] == '-' && tag[i + 1] == 'u' && tag[i + 2] == '-') { at = i; break; }
    copyn(out, cap, tag, at);
    if (at == n) return;
    /* the next singleton: a subtag of one character */
    size_t i = at + 2;
    while (i < n) {
        size_t j = i + 1;
        while (j < n && tag[j] != '-') j++;
        if (j - i - 1 == 1) { catn(out, cap, tag + i, n - i); return; }
        i = j;
    }
}

/* the default locale */
#define DEFAULT_LOCALE "en-US"

/* ResolveLocale's choice: the requested locale's best available match, its extension's
   keywords kept for the caller (the request's tag in t), else the default */
static void resolve_locale(js_vm *vm, uint32_t requested, uint32_t av, char *found, size_t cap, itag *t) {
    uint32_t n = vm->objs[requested].b;
    for (uint32_t i = 0; i < n; i++) {
        char tag[TAG_MAX + 1], noext[TAG_MAX + 1];
        if (str_ascii(vm, jv_idx(js_array_get(vm, requested, i)), tag, sizeof tag) < 0) continue;
        strip_u(tag, noext, sizeof noext);
        if (best_available(av, noext, found, cap)) {
            if (tag_from_text(tag, strlen(tag), t)) tag_clear(t);
            return;
        }
    }
    /* the default (V8 gives it when nothing matches, available or not) */
    copyz(found, cap, DEFAULT_LOCALE);
    tag_clear(t);
}

/* ---- options (ECMA-402 9.2.10 to 9.2.13) ---- */

/* CoerceOptionsToObject: undefined is an empty object, else ToObject (null a TypeError
   naming the method); JV_HOLE thrown */
static jv options_object(js_vm *vm, jv v, const char *method) {
    if (v == JV_UNDEF) return jv_obj(js_obj_new(vm, JK_PLAIN, 0));
    if (v == JV_NULL) {
        char m[96];
        m[0] = 0;
        cat(m, sizeof m, method);
        cat(m, sizeof m, " called on null or undefined");
        return js_throw_msg(vm, vm->type_error_proto, m);
    }
    jv o = js_to_object(vm, v);
    return vm->throwing ? JV_HOLE : o;
}
/* GetOptionsObject: undefined an empty object, an object itself, else a TypeError */
static jv options_object_strict(js_vm *vm, jv v) {
    if (v == JV_UNDEF) return jv_obj(js_obj_new(vm, JK_PLAIN, 0));
    if (jv_is_obj(v)) return v;
    return js_throw_msg(vm, vm->type_error_proto, "invalid_argument");
}

/* GetOption of a string among values (a list ended by 0, or 0 for any): the value's index
   in values (or 0 for any), -1 when undefined (the default), -2 thrown. *out the string */
static int get_option_str(js_vm *vm, jv opts, const char *name, const char *const *values, const char *service, uint32_t *out) {
    jv v = js_get(vm, opts, js_atom_ascii(vm, name));
    if (vm->throwing) return -2;
    if (v == JV_UNDEF) return -1;
    uint32_t s = js_to_string(vm, v);
    if (!s) return -2;
    if (out) *out = s;
    if (!values) return 0;
    char buf[64];
    int n = str_ascii(vm, s, buf, sizeof buf);
    for (int i = 0; values[i]; i++)
        if (n >= 0 && !strcmp(values[i], buf)) return i;
    vector<char> m;
    append_range(m, "Value ", 6);
    js_str_to_utf8(vm, s, &m);
    const char *mid = " out of range for ";
    append_range(m, mid, strlen(mid));
    append_range(m, service, strlen(service));
    const char *mid2 = " options property ";
    append_range(m, mid2, strlen(mid2));
    append_range(m, name, strlen(name));
    js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
    return -2;
}
/* GetOption of a boolean: 0 or 1, -1 undefined, -2 thrown */
static int get_option_bool(js_vm *vm, jv opts, const char *name) {
    jv v = js_get(vm, opts, js_atom_ascii(vm, name));
    if (vm->throwing) return -2;
    if (v == JV_UNDEF) return -1;
    return js_truthy(vm, v);
}

/* the localeMatcher option every constructor and supportedLocalesOf reads */
static int get_matcher(js_vm *vm, jv opts, const char *service) {
    static const char *const m[] = { "lookup", "best fit", 0 };
    return get_option_str(vm, opts, "localeMatcher", m, service, 0);
}

/* the names of the services, for messages */
static const char *const kind_name[JIK_N] = { "Intl.Locale", "Intl.Collator", "Intl.DateTimeFormat", "Intl.NumberFormat",
    "Intl.PluralRules", "Intl.RelativeTimeFormat", "Intl.ListFormat", "Intl.DisplayNames", "Intl.Segmenter", "", "" };
static const uint32_t kind_av[JIK_N] = { AV_GENERAL, AV_COLLATOR, AV_GENERAL, AV_GENERAL, AV_PLURAL, AV_GENERAL, AV_GENERAL,
    AV_GENERAL, AV_GENERAL, AV_GENERAL, AV_GENERAL };

/* SupportedLocales: the requested tags whose best available locale exists */
static jv supported_locales(js_vm *vm, uint32_t kind, jv locales, jv options) {
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    char svc[64];
    svc[0] = 0;
    cat(svc, sizeof svc, kind_name[kind]);
    cat(svc, sizeof svc, ".supportedLocalesOf");
    jv opts = options_object(vm, options, svc);
    if (opts == JV_HOLE) return JV_HOLE;
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    uint32_t out = js_array_new(vm, 0);
    for (uint32_t i = 0; i < vm->objs[req].b; i++) {
        char tag[TAG_MAX + 1], noext[TAG_MAX + 1], found[TAG_MAX + 1];
        jv s = js_array_get(vm, req, i);
        if (str_ascii(vm, jv_idx(s), tag, sizeof tag) < 0) continue;
        strip_u(tag, noext, sizeof noext);
        if (best_available(kind_av[kind], noext, found, sizeof found)) js_array_push(vm, out, s);
    }
    return jv_obj(out);
}

/* ---- region facts (calendars, hour cycles, week, time zones) ---- */

/* the region record's field f (0 calendars, 1 hour cycles, 2 week, 3 time zones) into out */
static void region_field(const char *region, int f, char *out, size_t cap) {
    uint32_t r = 0;
    if (region[0]) r = db_find_z(IDB_REGION, IDB_REGION_N, region);
    if (!r) r = db_find_z(IDB_REGION, IDB_REGION_N, "001");
    out[0] = 0;
    int field = 0;
    size_t n = 0;
    for (uint32_t c; (c = db8(r)); r++) {
        if (c == '|') { field++; continue; }
        if (field == f && n + 1 < cap) out[n++] = (char)c;
    }
    out[n] = 0;
}

/* a tag's region, else its likely one */
static void likely_region(const itag *t, char *region) {
    if (t->region[0]) { copyn(region, 4, t->region, strlen(t->region)); return; }
    char l[9], s[5];
    region[0] = 0;
    if (!add_likely(t->lang, t->script, t->region, l, s, region)) region[0] = 0;
}

/* ---- Intl.Locale (ECMA-402 14) ---- */

static const char *const locale_opt[] = { "calendar", "collation", "hourCycle", "caseFirst", "numeric", "numberingSystem" };
static const char *const locale_key[] = { "ca", "co", "hc", "kf", "kn", "nu" };

/* a keyword's value set (replaced, or added in its place among the sorted keys) */
static void set_kw(itag *t, const char *key, const char *val) {
    t->has_u = 1;
    int i = find_kw(t, key);
    if (i < 0) {
        if (t->nkw >= T_KWS) return;
        i = (int)t->nkw++;
        memcpy(t->key[i], key, 3);
    }
    copyn(t->val[i], sizeof t->val[0], val, strlen(val));
}

/* a type of the Unicode extension: (3*8alphanum) (- (3*8alphanum))* */
static int is_type(const char *s) {
    size_t n = strlen(s);
    if (!n) return 0;
    toks k;
    tokenize(s, n, &k, 0);
    for (uint32_t i = 0; i < k.n; i++) if (!sub_alnum(s + k.off[i], k.len[i], 3, 8)) return 0;
    return 1;
}

static jv locale_ctor(js_vm *vm, jv tagv, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return js_throw_msg(vm, vm->type_error_proto, "Constructor Intl.Locale requires 'new'");
    if (!jv_is_str(tagv) && !jv_is_obj(tagv))
        return js_throw_msg(vm, vm->type_error_proto, "First argument to Intl.Locale constructor can't be empty or missing");
    uint32_t s;
    if (is_intl(vm, tagv, JIK_LOCALE)) s = jv_idx(slot_get(vm, jv_idx(tagv), LS_TAG));
    else {
        s = js_to_string(vm, tagv);
        if (!s) return JV_HOLE;
    }
    jv opts = options_object(vm, options, "Intl.Locale");
    if (opts == JV_HOLE) return JV_HOLE;
    if (vm->strs[s].len == 0) return js_throw_msg(vm, vm->range_error_proto, "First argument to Intl.Locale constructor can't be empty or missing");
    itag t;
    if (!tag_from_js(vm, s, &t)) return JV_HOLE;
    /* ApplyOptionsToTag: language, script, region */
    static const char *const parts[] = { "language", "script", "region" };
    char got[3][16];
    for (int p = 0; p < 3; p++) {
        got[p][0] = 0;
        uint32_t v = 0;
        int r = get_option_str(vm, opts, parts[p], 0, "locale", &v);
        if (r == -2) return JV_HOLE;
        if (r == -1) continue;
        char b[16];
        int n = str_ascii(vm, v, b, sizeof b);
        int ok = n > 0 && (p == 0 ? sub_lang(b, (size_t)n) : p == 1 ? sub_script(b, (size_t)n) : sub_region(b, (size_t)n));
        if (!ok) return js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
        copyn(got[p], 16, b, (size_t)n);
    }
    if (got[0][0]) { for (char *c = got[0]; *c; c++) *c = (char)lower(*c); copyn(t.lang, 9, got[0], strlen(got[0])); }
    if (got[1][0]) copyn(t.script, 5, got[1], strlen(got[1]));
    if (got[2][0]) copyn(t.region, 4, got[2], strlen(got[2]));
    if (got[0][0] || got[1][0] || got[2][0]) {
        char w[TAG_MAX + 1];
        tag_write(&t, w, sizeof w);
        itag txb;
        if (tag_from_text(w, strlen(w), &txb)) return js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
        t = txb;
    }
    /* the keywords the options give */
    static const char *const hcs[] = { "h11", "h12", "h23", "h24", 0 };
    static const char *const kfs[] = { "upper", "lower", "false", 0 };
    for (int k = 0; k < 6; k++) {
        char b[80];
        if (k == 4) {
            int r = get_option_bool(vm, opts, "numeric");
            if (r == -2) return JV_HOLE;
            if (r == 1) set_kw(&t, "kn", "");
            else if (r == 0) set_kw(&t, "kn", "false");
            continue;
        }
        uint32_t v = 0;
        int r;
        if (k == 2) r = get_option_str(vm, opts, locale_opt[k], hcs, "locale", &v);
        else if (k == 3) r = get_option_str(vm, opts, locale_opt[k], kfs, "locale", &v);
        else r = get_option_str(vm, opts, locale_opt[k], 0, "locale", &v);
        if (r == -2) return JV_HOLE;
        if (r == -1) continue;
        int n = str_ascii(vm, v, b, sizeof b);
        if (n < 0 || !is_type(b)) return js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
        for (char *c = b; *c; c++) *c = (char)lower(*c);
        set_kw(&t, locale_key[k], b);
    }
    tag_canon(&t);
    char w[TAG_MAX + 1];
    tag_write(&t, w, sizeof w);
    jv p = JV_UNDEF;
    if (jv_is_obj(newt)) {
        p = js_get(vm, newt, A_PROTOTYPE);
        if (vm->throwing) return JV_HOLE;
    }
    uint32_t o = intl_new(vm, JIK_LOCALE, jv_is_obj(p) ? jv_idx(p) : vm->intl_protos[JIK_LOCALE], LS_N);
    slot_set(vm, o, LS_TAG, jv_str(js_str_ascii(vm, w)));
    return jv_obj(o);
}

/* V8's text for a receiver in its messages: an object as #<its constructor's name>, else
   the value as a string */
static void receiver_text(js_vm *vm, jv v, vector<char> *out) {
    if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_NATIVE) {
        const char *a = "function ";
        append_range(*out, a, strlen(a));
        if (vm->objs[jv_idx(v)].c) js_str_to_utf8(vm, vm->objs[jv_idx(v)].c, out);
        const char *b = "() { [native code] }";
        append_range(*out, b, strlen(b));
        return;
    }
    if (jv_is_obj(v)) {
        int was = vm->throwing;
        jv c = js_get(vm, v, A_CONSTRUCTOR);
        jv n = jv_is_obj(c) ? js_get(vm, c, A_NAME) : JV_UNDEF;
        if (vm->throwing && !was) { vm->throwing = 0; n = JV_UNDEF; }
        append_range(*out, "#<", 2);
        if (jv_is_str(n) && vm->strs[jv_idx(n)].len) js_str_to_utf8(vm, jv_idx(n), out);
        else append_range(*out, "Object", 6);
        push_back(*out, '>');
        return;
    }
    if (jv_is_sym(v)) { append_range(*out, "Symbol()", 8); return; }
    uint32_t s = js_to_string(vm, v);
    if (s) js_str_to_utf8(vm, s, out);
}

/* "Method NAME called on incompatible receiver X", thrown */
static jv incompatible(js_vm *vm, const char *method, jv thisv) {
    vector<char> m;
    const char *a = "Method ";
    append_range(m, a, strlen(a));
    append_range(m, method, strlen(method));
    const char *b = " called on incompatible receiver ";
    append_range(m, b, strlen(b));
    receiver_text(vm, thisv, &m);
    return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
}

static jv locale_this(js_vm *vm, jv thisv, itag *t, const char *what) {
    if (!is_intl(vm, thisv, JIK_LOCALE)) {
        char m[160];
        m[0] = 0;
        cat(m, sizeof m, "Intl.Locale.prototype.");
        cat(m, sizeof m, what);
        return incompatible(vm, m, thisv);
    }
    jv s = slot_get(vm, jv_idx(thisv), LS_TAG);
    char buf[TAG_MAX + 1];
    if (str_ascii(vm, jv_idx(s), buf, sizeof buf) < 0 || tag_from_text(buf, strlen(buf), t)) tag_clear(t);
    return s;
}

static jv new_locale_from(js_vm *vm, const itag *t) {
    char w[TAG_MAX + 1];
    tag_write(t, w, sizeof w);
    uint32_t o = intl_new(vm, JIK_LOCALE, vm->intl_protos[JIK_LOCALE], LS_N);
    slot_set(vm, o, LS_TAG, jv_str(js_str_ascii(vm, w)));
    return jv_obj(o);
}

/* the length of ICU's name for a tag ("zh_Hant_TW", "_Hant", "de__1996"), -1 when it has
   extensions (whose ICU names are longer than any base name): V8 keeps a maximized or
   minimized locale only when its name's length differs from the original's, so a result
   of the same length (und-Hant minimized to zh-TW) is the original */
static int icu_name_len(const itag *t) {
    if (t->has_u || t->nattr || t->nkw || t->tx[0] || t->next || t->priv[0]) return -1;
    int n = (int)strlen(t->lang);
    if (t->script[0]) n += 5;
    if (t->region[0]) n += 1 + (int)strlen(t->region);
    if (t->nvar) n += t->region[0] ? 0 : 1;
    for (uint32_t i = 0; i < t->nvar; i++) n += 1 + (int)strlen(t->var[i]);
    return n;
}

/* ICU's maximize: "und", Zzzz and ZZ count as absent */
static int icu_likely(const itag *t, char *l, char *s, char *r) {
    char il[9], is[5], ir[4];
    copyz(il, 9, t->lang);
    if (!strcmp(il, "und")) il[0] = 0;
    copyz(is, 5, t->script);
    copyz(ir, 4, t->region);
    if (!strcmp(is, "Zzzz")) is[0] = 0;
    if (!strcmp(ir, "ZZ")) ir[0] = 0;
    if (il[0] && is[0] && ir[0]) { copyz(l, 9, il); copyz(s, 5, is); copyz(r, 4, ir); return 1; }
    return add_likely(il, is, ir, l, s, r);
}

/* maximize: the likely subtags added to the base name, the extensions kept */
static jv locale_maximize(js_vm *vm, itag *t) {
    char l[9], s[5], r[4];
    int before = icu_name_len(t);
    if (icu_likely(t, l, s, r)) {
        itag m = *t;
        copyz(m.lang, 9, l);
        copyz(m.script, 5, s);
        copyz(m.region, 4, r);
        if (before < 0 || before != icu_name_len(&m)) *t = m;
    }
    return new_locale_from(vm, t);
}

/* minimize: the shortest of language, language-region, language-script that maximizes alike */
static jv locale_minimize(js_vm *vm, itag *t) {
    char ml[9], ms[5], mr[4];
    int before = icu_name_len(t);
    if (!icu_likely(t, ml, ms, mr)) return new_locale_from(vm, t);
    itag res = *t;
    int done = 0;
    for (int trial = 0; trial < 3 && !done; trial++) {
        char l[9], s[5], r[4], ts[5], tr[4];
        ts[0] = tr[0] = 0;
        if (trial == 2) copyz(ts, 5, ms);
        if (trial == 1) copyz(tr, 4, mr);
        if (add_likely(ml, ts, tr, l, s, r) && !strcmp(l, ml) && !strcmp(s, ms) && !strcmp(r, mr)) {
            copyz(res.lang, 9, ml);
            copyz(res.script, 5, ts);
            copyz(res.region, 4, tr);
            done = 1;
        }
    }
    if (!done) {
        copyz(res.lang, 9, ml);
        copyz(res.script, 5, ms);
        copyz(res.region, 4, mr);
    }
    if (!strcmp(res.lang, "und")) res.lang[0] = 0;
    if (before < 0 || before != icu_name_len(&res)) *t = res;
    return new_locale_from(vm, t);
}

/* an array of the words of a space-separated list */
static uint32_t words_array(js_vm *vm, const char *s) {
    uint32_t a = js_array_new(vm, 0);
    while (*s) {
        const char *e = strchr(s, ' ');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n) js_array_push(vm, a, jv_str(js_str_utf8(vm, s, n)));
        if (!e) break;
        s = e + 1;
    }
    return a;
}

/* the default numbering system of a locale: latn but where CLDR says another (filled by
   the numbers' data) */
static void default_numbering(const char *tag, char *out, size_t cap);

static jv locale_get(js_vm *vm, uint32_t which, itag *t) {
    char b[4096];                       /* a region's time zones run long (the US's are 29) */
    switch (which) {
    case LG_BASENAME: tag_base(t, b, sizeof b); return jv_str(js_str_ascii(vm, b));
    case LG_LANGUAGE: pick(b, sizeof b, t->lang, "und"); return jv_str(js_str_ascii(vm, b));
    case LG_SCRIPT: return t->script[0] ? jv_str(js_str_ascii(vm, t->script)) : JV_UNDEF;
    case LG_REGION: return t->region[0] ? jv_str(js_str_ascii(vm, t->region)) : JV_UNDEF;
    case LG_NUMERIC: {
        int i = find_kw(t, "kn");
        return jv_bool(i >= 0 && (!t->val[i][0] || !strcmp(t->val[i], "true")));
    }
    case LG_CALENDAR: case LG_CASEFIRST: case LG_COLLATION: case LG_HOURCYCLE: case LG_NUMBERINGSYSTEM: {
        static const char *const keys[] = { "ca", "kf", "co", "hc", "nu" };
        int ki = which == LG_CALENDAR ? 0 : which == LG_CASEFIRST ? 1 : which == LG_COLLATION ? 2 : which == LG_HOURCYCLE ? 3 : 4;
        int i = find_kw(t, keys[ki]);
        return i < 0 ? JV_UNDEF : jv_str(js_str_ascii(vm, t->val[i]));
    }
    case LG_CALENDARS: case LG_HOURCYCLES: case LG_TIMEZONES: case LG_WEEKINFO: {
        int i = -1;
        if (which == LG_CALENDARS) i = find_kw(t, "ca");
        else if (which == LG_HOURCYCLES) i = find_kw(t, "hc");
        if (i >= 0 && t->val[i][0]) {
            uint32_t a = js_array_new(vm, 0);
            js_array_push(vm, a, jv_str(js_str_ascii(vm, t->val[i])));
            return jv_obj(a);
        }
        if (which == LG_TIMEZONES && !t->region[0]) return JV_UNDEF;
        char region[4];
        likely_region(t, region);
        region_field(region, which == LG_CALENDARS ? 0 : which == LG_HOURCYCLES ? 1 : which == LG_WEEKINFO ? 2 : 3, b, sizeof b);
        if (which == LG_HOURCYCLES) {
            /* the preferred cycle alone */
            char *sp = strchr(b, ' ');
            if (sp) *sp = 0;
        }
        if (which != LG_WEEKINFO) return jv_obj(words_array(vm, b));
        /* first day, the weekend's days, the minimal days: "7672" */
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        size_t n = strlen(b);
        js_define(vm, o, js_atom_ascii(vm, "firstDay"), jv_num(n ? b[0] - '0' : 1), 0);
        uint32_t w = js_array_new(vm, 0);
        for (size_t i2 = 1; i2 + 1 < n; i2++) js_array_push(vm, w, jv_num(b[i2] - '0'));
        js_define(vm, o, js_atom_ascii(vm, "weekend"), jv_obj(w), 0);
        js_define(vm, o, js_atom_ascii(vm, "minimalDays"), jv_num(n ? b[n - 1] - '0' : 1), 0);
        return jv_obj(o);
    }
    case LG_COLLATIONS: {
        int i = find_kw(t, "co");
        if (i >= 0 && t->val[i][0]) {
            uint32_t a = js_array_new(vm, 0);
            js_array_push(vm, a, jv_str(js_str_ascii(vm, t->val[i])));
            return jv_obj(a);
        }
        uint32_t c = db_find_z(IDB_COLLATIONS, IDB_COLLATIONS_N, t->lang);
        if (c) db_copy(c, b, sizeof b);
        else copyn(b, sizeof b, "emoji eor", 9);
        return jv_obj(words_array(vm, b));
    }
    case LG_NUMBERINGSYSTEMS: {
        int i = find_kw(t, "nu");
        if (i >= 0 && t->val[i][0]) copyn(b, sizeof b, t->val[i], strlen(t->val[i]));
        else {
            char base[TAG_MAX + 1];
            tag_base(t, base, sizeof base);
            default_numbering(base, b, sizeof b);
        }
        uint32_t a = js_array_new(vm, 0);
        js_array_push(vm, a, jv_str(js_str_ascii(vm, b)));
        return jv_obj(a);
    }
    case LG_TEXTINFO: {
        char l[9], s[5], r[4];
        const char *dir = "ltr";
        if (add_likely(t->lang, t->script, t->region, l, s, r) && db_find_z(IDB_RTL_SCRIPTS, IDB_RTL_SCRIPTS_N, s)) dir = "rtl";
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        js_define(vm, o, js_atom_ascii(vm, "direction"), jv_str(js_str_ascii(vm, dir)), 0);
        return jv_obj(o);
    }
    default: return JV_UNDEF;
    }
}

/* the locale's own record or its nearest available ancestor's (the tag truncated) */
static void default_numbering(const char *tag, char *out, size_t cap) {
    char c[TAG_MAX + 1];
    if (!best_available(AV_GENERAL, tag, c, sizeof c)) { copyz(out, cap, "latn"); return; }
    uint32_t v = db_find_z(IDB_NUMSYS, IDB_NUMSYS_N, c);
    if (v) db_copy(v, out, cap);
    else copyz(out, cap, "latn");
}

/* ---- decimals: a number's digits, as ICU's DecimalQuantity holds them ---- */

#define DEC_MAX 1100
/* value = 0.d[0]d[1]... x 10^e: the digits most significant first, no leading or
   trailing zero (zero has none, and e 0); the sign kept for a negative zero; or a NaN or
   an infinity */
typedef struct dec { uint8_t d[DEC_MAX]; int nd, e, neg, kind; } dec;
enum { DK_FINITE, DK_NAN, DK_INF };

static void dec_zero(dec *x) { x->nd = 0; x->e = 0; x->neg = 0; x->kind = DK_FINITE; }
static void dec_trim(dec *x) {
    int lead = 0;
    while (lead < x->nd && x->d[lead] == 0) lead++;
    if (lead) {
        for (int k = lead; k < x->nd; k++) x->d[k - lead] = x->d[k];
        x->nd -= lead;
        x->e -= lead;
    }
    while (x->nd && x->d[x->nd - 1] == 0) x->nd--;
    if (!x->nd) x->e = 0;
}
/* the power of ten of the most significant digit (zero's: 0), of the least, a digit's */
static int dec_mag(const dec *x) { return x->nd ? x->e - 1 : 0; }
static int dec_low(const dec *x) { return x->nd ? x->e - x->nd : 0; }
static int dec_digit(const dec *x, int pow) {
    int k = x->e - 1 - pow;
    return k >= 0 && k < x->nd ? x->d[k] : 0;
}
static int dec_is_zero(const dec *x) { return x->kind == DK_FINITE && !x->nd; }

/* a double's shortest digits that read back as it (ICU's DoubleToStringConverter::SHORTEST) */
static void dec_from_double(double v, dec *x) {
    dec_zero(x);
    if (v != v) { x->kind = DK_NAN; return; }
    x->neg = signbit(v) != 0;
    if (v == INFINITY || v == -INFINITY) { x->kind = DK_INF; return; }
    if (v == 0) return;
    double a = fabs(v);
    if (a < 9007199254740992.0 && (double)(int64_t)a == a) {
        uint64_t u = (uint64_t)a;
        char b[24];
        int n = 0;
        while (u) { b[n++] = (char)(u % 10); u /= 10; }
        for (int k = 0; k < n; k++) x->d[k] = (uint8_t)b[n - 1 - k];
        x->nd = n;
        x->e = n;
        dec_trim(x);
        return;
    }
    char tmp[40];
    int prec = 1;
    for (; prec <= 17; prec++) {
        snprintf(tmp, sizeof tmp, "%.*e", prec - 1, a);
        if (strtod(tmp, 0) == a) break;
    }
    int i = 0, n = 0;
    for (; tmp[i] && tmp[i] != 'e'; i++) if (tmp[i] >= '0' && tmp[i] <= '9') x->d[n++] = (uint8_t)(tmp[i] - '0');
    x->nd = n;
    x->e = atoi(tmp + i + 1) + 1;
    dec_trim(x);
}

/* decimal text (digits, a point, an exponent: StrUnsignedDecimalLiteral's) into x: 0 malformed */
static int dec_from_text(const uint16_t *s, uint32_t n, dec *x) {
    uint32_t i = 0;
    int seen = 0, point = -1, nd = 0, e = 0;
    dec_zero(x);
    for (; i < n; i++) {
        uint16_t c = s[i];
        if (c >= '0' && c <= '9') {
            seen = 1;
            if (nd == 0 && c == '0') { if (point >= 0) e--; continue; }
            if (nd < DEC_MAX) x->d[nd++] = (uint8_t)(c - '0');
            else if (point < 0) e++;
            if (point < 0) e++;
            continue;
        }
        if (c == '.' && point < 0) { point = (int)i; continue; }
        break;
    }
    if (!seen) return 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        int sg = 1;
        if (i < n && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') sg = -1; i++; }
        if (i >= n) return 0;
        long ex = 0;
        for (; i < n; i++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            if (ex < 100000000) ex = ex * 10 + (s[i] - '0');
        }
        if (ex > 1000000) ex = 1000000;
        e += (int)(sg * ex);
    }
    if (i != n) return 0;
    x->nd = nd;
    x->e = e;
    dec_trim(x);
    return 1;
}

static int js_space(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0xA0 || c == 0xFEFF || c == 0x2028 ||
           c == 0x2029 || (c >= 0x2000 && c <= 0x200A) || c == 0x3000 || c == 0x1680 || c == 0x202F || c == 0x205F;
}

/* a BigInt's digits */
static void dec_from_big(js_vm *vm, jv b, dec *x) {
    uint32_t s = js_big_to_string(vm, b, 10);
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off, i = 0;
    vector<uint16_t> u;
    for (uint32_t k = 0; k < n; k++) push_back(u, vm->units[o + k]);
    int neg = n && u[0] == '-';
    if (neg) i = 1;
    dec_from_text(data(u) + i, n - i, x);
    x->neg = neg;
}

/* ToIntlMathematicalValue (ECMA-402 15.5.16): a number, a BigInt or a numeric string's exact
   value; 0 thrown */
static int to_intl_mv(js_vm *vm, jv v, dec *x) {
    jv p = js_to_primitive(vm, v, 0);
    if (vm->throwing) return 0;
    if (jv_is_big(p)) { dec_from_big(vm, p, x); return 1; }
    if (!jv_is_str(p)) {
        double d = js_to_number(vm, p);
        if (vm->throwing) return 0;
        dec_from_double(d, x);
        return 1;
    }
    uint32_t s = jv_idx(p);
    js_str_flatten(vm, s);
    vector<uint16_t> u;
    {
        uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
        for (uint32_t k = 0; k < n; k++) push_back(u, vm->units[o + k]);
    }
    uint32_t a = 0, b = (uint32_t)len(u);
    while (a < b && js_space(u[a])) a++;
    while (b > a && js_space(u[b - 1])) b--;
    dec_zero(x);
    if (a == b) return 1;
    int neg = 0;
    uint32_t i = a;
    if (u[i] == '+' || u[i] == '-') { neg = u[i] == '-'; i++; }
    static const char inf[] = "Infinity";
    if (b - i == 8) {
        int same = 1;
        for (uint32_t k = 0; k < 8; k++) if (u[i + k] != (uint16_t)inf[k]) same = 0;
        if (same) { x->kind = DK_INF; x->neg = neg; return 1; }
    }
    if (i == a && b - a > 2 && u[a] == '0' && (u[a + 1] == 'x' || u[a + 1] == 'X' || u[a + 1] == 'o' || u[a + 1] == 'O' || u[a + 1] == 'b' || u[a + 1] == 'B')) {
        jv big = js_big_parse(vm, js_str_new(vm, data(u) + a, b - a), 0);
        if (vm->throwing) vm->throwing = 0;
        if (big == JV_HOLE || !jv_is_big(big)) { x->kind = DK_NAN; return 1; }
        dec_from_big(vm, big, x);
        return 1;
    }
    if (!dec_from_text(data(u) + i, b - i, x)) { dec_zero(x); x->kind = DK_NAN; return 1; }
    x->neg = neg;
    /* past the doubles' range, an infinity (V8's: the string's number value overflows) */
    if (x->nd && x->e >= 309) {
        int inf_ = x->e > 309;
        if (!inf_) {
            char buf[DEC_MAX + 16];
            int k = 0;
            buf[k++] = '0';
            buf[k++] = '.';
            for (int j = 0; j < x->nd && k < DEC_MAX; j++) buf[k++] = (char)('0' + x->d[j]);
            snprintf(buf + k, sizeof buf - (size_t)k, "e%d", x->e);
            inf_ = strtod(buf, 0) == INFINITY;
        }
        if (inf_) { x->kind = DK_INF; x->nd = 0; x->e = 0; }
    }
    return 1;
}

/* the rounding modes, as ECMA-402 names them */
enum { RM_CEIL, RM_FLOOR, RM_EXPAND, RM_TRUNC, RM_HALFCEIL, RM_HALFFLOOR, RM_HALFEXPAND, RM_HALFTRUNC, RM_HALFEVEN };

/* x rounded to a multiple of 10^mag (ICU's roundToMagnitude) */
static void dec_round(dec *x, int mag, int mode) {
    if (x->kind != DK_FINITE || !x->nd) return;
    int keep = x->e - mag;
    if (keep >= x->nd) return;
    int sect;                                  /* the part dropped against half a unit */
    if (keep < 0) sect = -1;
    else {
        int fd = x->d[keep];
        if (fd < 5) sect = -1;
        else if (fd > 5) sect = 1;
        else {
            sect = 0;
            for (int k = keep + 1; k < x->nd; k++) if (x->d[k]) { sect = 1; break; }
        }
    }
    int up;
    if (mode == RM_EXPAND) up = 1;
    else if (mode == RM_TRUNC) up = 0;
    else if (mode == RM_CEIL) up = !x->neg;
    else if (mode == RM_FLOOR) up = x->neg;
    else if (sect < 0) up = 0;
    else if (sect > 0) up = 1;
    else if (mode == RM_HALFEXPAND) up = 1;
    else if (mode == RM_HALFTRUNC) up = 0;
    else if (mode == RM_HALFCEIL) up = !x->neg;
    else if (mode == RM_HALFFLOOR) up = x->neg;
    else up = keep > 0 && (x->d[keep - 1] & 1);
    if (keep < 0) {
        if (up) { x->d[0] = 1; x->nd = 1; x->e = mag + 1; }
        else { x->nd = 0; x->e = 0; }
        return;
    }
    x->nd = keep;
    if (up) {
        int k = keep - 1;
        while (k >= 0 && x->d[k] == 9) { x->d[k] = 0; k--; }
        if (k >= 0) x->d[k]++;
        else { x->d[0] = 1; x->nd = 1; x->e++; }
    }
    dec_trim(x);
}

/* x times a small integer */
static void dec_mul_small(dec *x, uint32_t k) {
    if (!x->nd) return;
    uint8_t tmp[DEC_MAX + 8];
    uint32_t carry = 0;
    int n = x->nd, w = 0;
    for (int i = n - 1; i >= 0; i--) {
        uint32_t v = x->d[i] * k + carry;
        tmp[w++] = (uint8_t)(v % 10);
        carry = v / 10;
    }
    while (carry) { tmp[w++] = (uint8_t)(carry % 10); carry /= 10; }
    int add = w - n;
    if (w > DEC_MAX) w = DEC_MAX;
    for (int i = 0; i < w; i++) x->d[i] = tmp[(n + add) - 1 - i];
    x->nd = w;
    x->e += add;
    dec_trim(x);
}

/* x rounded to a multiple of m x 10^mag, m 1, 2, 5 or 25 (ICU's roundToIncrement: x divided
   exactly, rounded to an integer, multiplied back) */
static void dec_round_inc(dec *x, uint32_t m, int mag, int mode) {
    if (m == 1) { dec_round(x, mag, mode); return; }
    uint32_t c = m == 2 ? 5 : m == 5 ? 2 : 4;
    int s = m == 25 ? 2 : 1;
    if (x->kind != DK_FINITE || !x->nd) return;
    dec_mul_small(x, c);
    if (x->nd) x->e -= mag + s;
    dec_round(x, 0, mode);
    dec_mul_small(x, m);
    if (x->nd) x->e += mag;
}

/* ---- plural rules (CLDR's, compiled by tools/gen-intl.py) ---- */

enum { PL_ZERO, PL_ONE, PL_TWO, PL_FEW, PL_MANY, PL_OTHER };
static const char *const plural_name[6] = { "zero", "one", "two", "few", "many", "other" };

/* the rule set of a locale's language (or the locale itself, pt-PT), cardinal or ordinal:
   its offset, 0 for none */
static uint32_t plural_set(const char *tag, int ordinal) {
    char key[TAG_MAX + 4];
    char c[TAG_MAX + 1];
    if (!best_available(AV_PLURAL, tag, c, sizeof c)) copyz(c, sizeof c, "en");
    for (int pass = 0; pass < 2; pass++) {
        key[0] = 0;
        cat(key, sizeof key, ordinal ? "o:" : "c:");
        cat(key, sizeof key, c);
        uint32_t v = db_find_z(IDB_PLURALS, IDB_PLURALS_N, key);
        if (v) {
            char num[16];
            db_copy(v, num, sizeof num);
            return (uint32_t)atol(num);
        }
        char *dash = strchr(c, '-');
        if (!dash) break;
        *dash = 0;
    }
    return 0;
}

/* an operand of a decimal shown with minfrac fraction digits at least: its value modulo
   mod (0 none), saturated; *integral whether n is an integer */
static uint64_t plural_operand(const dec *x, int minfrac, int cexp, int op, uint32_t mod, int *integral) {
    *integral = 1;
    int low = dec_low(x);
    int w = low < 0 ? -low : 0;               /* fraction digits without trailing zeros */
    int v = w > minfrac ? w : minfrac;        /* fraction digits shown */
    uint64_t r = 0;
    switch (op) {
    case 0: case 1: {                         /* n and i: the integer part (n's fraction checked) */
        int top = x->nd ? x->e - 1 : -1;
        for (int p = top; p >= 0; p--) {
            r = r * 10 + (uint64_t)dec_digit(x, p);
            if (mod) r %= mod;
            else if (r > 1000000000000000000ull) { r = 1000000000000000000ull; break; }
        }
        if (op == 0 && w > 0) *integral = 0;
        return r;
    }
    case 2: return (uint64_t)v;
    case 3: return (uint64_t)w;
    case 4: case 5: {                         /* f and t: the fraction digits as an integer */
        int digits = op == 4 ? v : w;
        for (int p = -1; p >= -digits; p--) {
            r = r * 10 + (uint64_t)dec_digit(x, p);
            if (mod) r %= mod;
            else if (r > 1000000000000000000ull) { r = 1000000000000000000ull; break; }
        }
        return r;
    }
    default: return (uint64_t)(cexp < 0 ? 0 : cexp);
    }
}

/* the plural category of a decimal by a rule set */
static int plural_of(uint32_t set, const dec *x, int minfrac, int cexp) {
    if (!set || x->kind != DK_FINITE) return PL_OTHER;
    uint32_t p = set, ncat = db8(p++);
    for (uint32_t c = 0; c < ncat; c++) {
        uint32_t cat_ = db8(p++), nor = db8(p++);
        int any = nor == 0;
        for (uint32_t g = 0; g < nor; g++) {
            uint32_t nand = db8(p++);
            int all = 1;
            for (uint32_t r = 0; r < nand; r++) {
                uint32_t op = db8(p), mod = db32(p + 1), neg = db8(p + 5), nr = db8(p + 6);
                p += 7;
                int integral;
                uint64_t val = plural_operand(x, minfrac, cexp, (int)op, mod, &integral);
                int in = 0;
                for (uint32_t k = 0; k < nr; k++) {
                    uint64_t lo = db32(p + k * 8), hi = db32(p + k * 8 + 4);
                    if (integral && val >= lo && val <= hi) in = 1;
                }
                p += nr * 8;
                if (neg ? in : !in) all = 0;
            }
            if (all) any = 1;
        }
        if (any) return (int)cat_;
    }
    return PL_OTHER;
}

/* a range's plural form from its ends' (CLDR's plural ranges; other by default) */
static int plural_range(const char *tag, int a, int b) {
    char c[TAG_MAX + 1];
    copyz(c, sizeof c, tag);
    for (;;) {
        uint32_t v = db_find_z(IDB_PLURAL_RANGES, IDB_PLURAL_RANGES_N, c);
        if (v) {
            uint32_t ch = db8(v + (uint32_t)(a * 6 + b));
            return ch == '-' ? PL_OTHER : (int)(ch - '0');
        }
        char *dash = strrchr(c, '-');
        if (!dash) return PL_OTHER;
        *dash = 0;
    }
}

/* ---- Intl.NumberFormat (ECMA-402 15) ---- */

enum { ST_DECIMAL, ST_PERCENT, ST_CURRENCY, ST_UNIT };
enum { CD_CODE, CD_SYMBOL, CD_NARROW, CD_NAME };
enum { UD_SHORT, UD_NARROW, UD_LONG };
enum { RT_FRACTION, RT_SIGNIFICANT, RT_MORE, RT_LESS };
enum { RP_AUTO, RP_MORE, RP_LESS };
enum { NN_STANDARD, NN_SCIENTIFIC, NN_ENGINEERING, NN_COMPACT };
enum { UG_ALWAYS, UG_AUTO, UG_MIN2, UG_OFF };
enum { SD_AUTO, SD_NEVER, SD_ALWAYS, SD_EXCEPTZERO, SD_NEGATIVE };
static const char *const st_names[] = { "decimal", "percent", "currency", "unit", 0 };
static const char *const cd_names[] = { "code", "symbol", "narrowSymbol", "name", 0 };
static const char *const cs_names[] = { "standard", "accounting", 0 };
static const char *const ud_names[] = { "short", "narrow", "long", 0 };
static const char *const nn_names[] = { "standard", "scientific", "engineering", "compact", 0 };
static const char *const cpd_names[] = { "short", "long", 0 };
static const char *const sd_names[] = { "auto", "never", "always", "exceptZero", "negative", 0 };
static const char *const rm_names[] = { "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven", 0 };
static const char *const rp_names[] = { "auto", "morePrecision", "lessPrecision", 0 };
static const char *const tz_names[] = { "auto", "stripIfInteger", 0 };
static const char *const ug_names[] = { "always", "auto", "min2", "false", 0 };

/* a NumberFormat's slots */
enum { NFS_LOCALE, NFS_NU, NFS_STYLE, NFS_CURRENCY, NFS_CURDISPLAY, NFS_CURSIGN, NFS_UNIT, NFS_UNITDISPLAY, NFS_MNID,
       NFS_MNFD, NFS_MXFD, NFS_MNSD, NFS_MXSD, NFS_RTYPE, NFS_INCREMENT, NFS_RMODE, NFS_RPRIORITY, NFS_TRAILING,
       NFS_NOTATION, NFS_COMPACT, NFS_GROUPING, NFS_SIGN, NFS_DATALOC, NFS_NSI, NFS_BOUND, NFS_N };

/* a NumberFormat's options, read from its slots for a format */
typedef struct nfo {
    int li;                                   /* the data locale's record */
    char tag[TAG_MAX + 1];                    /* the data locale's tag */
    int nsi;                                  /* the numbering system's index (IDB_NUMBERING) */
    int style, cur_display, cur_sign, unit_display;
    char cur[4];
    int cur_idx, cur_digits;                  /* the currency's record (-1: not carried) */
    int unit, per;                            /* the unit's record, the per unit's (-1 none); unit a whole compound's */
    int mnid, mnfd, mxfd, mnsd, mxsd, rtype, increment, rmode, trailing;
    int notation, compact, grouping, sign;
    int approx;                               /* a range's ~ (formatRange's identity) */
} nfo;

static int slot_int(js_vm *vm, uint32_t o, uint32_t i) {
    jv v = slot_get(vm, o, i);
    return jv_is_num(v) ? (int)jv_dbl(v) : 0;
}

/* a table's index of a key: its place among the pairs, -1 none */
static int db_index(uint32_t table, uint32_t count, const char *key) {
    uint32_t lo = 0, hi = count;
    size_t n = strlen(key);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = db_cmp(db32(table + mid * 8), key, n);
        if (c == 0) return (int)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

static void nfo_read(js_vm *vm, uint32_t o, nfo *n) {
    memset(n, 0, sizeof *n);
    n->li = slot_int(vm, o, NFS_DATALOC);
    db_copy(db32(IDB_LOCALES + (uint32_t)n->li * LOC_REC), n->tag, sizeof n->tag);
    n->nsi = slot_int(vm, o, NFS_NSI);
    n->style = slot_int(vm, o, NFS_STYLE);
    n->cur_display = slot_int(vm, o, NFS_CURDISPLAY);
    n->cur_sign = slot_int(vm, o, NFS_CURSIGN);
    n->unit_display = slot_int(vm, o, NFS_UNITDISPLAY);
    n->cur_idx = -1;
    n->unit = n->per = -1;
    if (n->style == ST_CURRENCY) {
        jv c = slot_get(vm, o, NFS_CURRENCY);
        if (jv_is_str(c)) str_ascii(vm, jv_idx(c), n->cur, sizeof n->cur);
        n->cur_idx = db_index(IDB_CURRENCIES, IDB_CURRENCIES_N, n->cur);
        uint32_t d = db_find_z(IDB_CUR_DIGITS, IDB_CUR_DIGITS_N, n->cur);
        n->cur_digits = d ? (int)(db8(d) - '0') : 2;
    }
    if (n->style == ST_UNIT) {
        char u[64];
        jv uv = slot_get(vm, o, NFS_UNIT);
        if (jv_is_str(uv) && str_ascii(vm, jv_idx(uv), u, sizeof u) > 0) {
            n->unit = db_index(IDB_UNITS, IDB_UNITS_N, u);
            if (n->unit < 0) {
                char *per = strstr(u, "-per-");
                if (per) {
                    n->per = db_index(IDB_UNITS, IDB_UNITS_N, per + 5);
                    *per = 0;
                    n->unit = db_index(IDB_UNITS, IDB_UNITS_N, u);
                }
            }
        }
    }
    n->mnid = slot_int(vm, o, NFS_MNID);
    n->mnfd = slot_int(vm, o, NFS_MNFD);
    n->mxfd = slot_int(vm, o, NFS_MXFD);
    n->mnsd = slot_int(vm, o, NFS_MNSD);
    n->mxsd = slot_int(vm, o, NFS_MXSD);
    n->rtype = slot_int(vm, o, NFS_RTYPE);
    n->increment = slot_int(vm, o, NFS_INCREMENT);
    n->rmode = slot_int(vm, o, NFS_RMODE);
    n->trailing = slot_int(vm, o, NFS_TRAILING);
    n->notation = slot_int(vm, o, NFS_NOTATION);
    n->compact = slot_int(vm, o, NFS_COMPACT);
    n->grouping = slot_int(vm, o, NFS_GROUPING);
    n->sign = slot_int(vm, o, NFS_SIGN);
}

/* the rounding the options make (V8's settings of ICU's Precision, applied as ICU's
   RoundingImpl::apply does); *minfrac the fraction digits shown at least */
static void nf_round(const nfo *o, dec *x, int *minfrac) {
    int mf = 0;
    if (x->kind != DK_FINITE) { *minfrac = 0; return; }
    if (o->increment != 1) {
        /* incrementExact(increment, -maxFraction), its mantissa's trailing zeros taken out */
        uint32_t m = (uint32_t)o->increment;
        int mag = -o->mxfd;
        while (m % 10 == 0) { m /= 10; mag++; }
        dec_round_inc(x, m, mag, o->rmode);
        mf = o->mnfd;
    } else if (o->rtype == RT_FRACTION) {
        dec_round(x, -o->mxfd, o->rmode);
        mf = o->mnfd;
    } else if (o->rtype == RT_SIGNIFICANT) {
        dec_round(x, dec_mag(x) - o->mxsd + 1, o->rmode);
        int dm = dec_mag(x) - o->mnsd + 1;
        mf = dm < 0 ? -dm : 0;
    } else {
        int rm1 = -o->mxfd, rm2 = dec_mag(x) - o->mxsd + 1;
        int relaxed = o->rtype == RT_MORE;
        int rm = relaxed ? (rm1 < rm2 ? rm1 : rm2) : (rm1 > rm2 ? rm1 : rm2);
        if (x->nd) {
            int upper = dec_mag(x);
            dec_round(x, rm, o->rmode);
            if (x->nd && dec_mag(x) != upper && rm1 == rm2) rm2 += 1;
        }
        int dm1 = o->mnfd == 0 ? 0x7FFFFFFF : -o->mnfd;
        int dm2 = dec_mag(x) - o->mnsd + 1;
        int dm;
        if (relaxed) dm = rm2 <= rm1 ? dm2 : dm1;
        else dm = rm2 <= rm1 ? dm1 : dm2;
        mf = dm < 0 ? -dm : 0;
    }
    /* stripIfInteger: no fraction digits shown on a whole number */
    if (o->trailing && dec_low(x) >= 0) mf = 0;
    *minfrac = mf;
}

/* the compact patterns of a locale's numbering system, style 0 short, 1 long, 2 currency:
   the record (with ICU's fallbacks to latn and to short), 0 none */
static uint32_t compact_rec(const nfo *o, int style) {
    int latn = db_index(IDB_NUMBERING, IDB_NUMBERING_N, "latn");
    uint32_t r = rec_get(o->li, RKEY(IDK_COMPACT, o->nsi * 4 + style));
    if (!r && o->nsi != latn) r = rec_get(o->li, RKEY(IDK_COMPACT, latn * 4 + style));
    if (!r && style == 1) r = rec_get(o->li, RKEY(IDK_COMPACT, o->nsi * 4));
    if (!r && style == 1 && o->nsi != latn) r = rec_get(o->li, RKEY(IDK_COMPACT, latn * 4));
    return r;
}
#define COMPACT_FORMS 7
/* a compact record's pattern for a magnitude (3 to 14) and a form: its field's units */
static uint32_t compact_pattern(uint32_t rec, int mag, int form, uint16_t *buf, uint32_t cap) {
    return fld_units(rec, (uint32_t)((mag - 3) * COMPACT_FORMS + form), buf, cap);
}
static int count_zeros(const uint16_t *p, uint32_t n) {
    int z = 0, q = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] == '\'') q = !q;
        else if (!q && p[i] == '0') z++;
    }
    return z;
}
/* the multiplier of a compact magnitude (ICU's CompactData): the power of ten the number is
   multiplied by; *largest the largest magnitude with data */
static int compact_multiplier(uint32_t rec, int mag, int *largest) {
    *largest = 0;
    if (!rec) return 0;
    for (int m = 14; m >= 3; m--) {
        int any = 0;
        for (int f = 0; f < COMPACT_FORMS && !any; f++) {
            uint16_t b[96];
            if (compact_pattern(rec, m, f, b, 96)) any = 1;
        }
        if (any) { *largest = m; break; }
    }
    if (mag < 0) return 0;
    if (mag > *largest) mag = *largest;
    if (mag < 3) return 0;
    for (int f = 0; f < COMPACT_FORMS; f++) {
        uint16_t b[96];
        uint32_t n = compact_pattern(rec, mag, f, b, 96);
        if (!n || (n == 1 && b[0] == '0')) continue;
        int z = count_zeros(b, n);
        if (z > 0) return z - mag - 1;
    }
    return 0;
}

/* the producer of an exponent: scientific (1), engineering (3), compact (its record) */
static int multiplier_of(const nfo *o, uint32_t crec, int mag) {
    if (o->notation == NN_SCIENTIFIC) return -mag;
    if (o->notation == NN_ENGINEERING) {
        int shown = ((mag % 3) + 3) % 3 + 1;
        return shown - mag - 1;
    }
    int largest;
    return compact_multiplier(crec, mag, &largest);
}

/* ICU's chooseMultiplierAndApply: the number scaled and rounded, its magnitude rechecked
   once (999.9 rounds to 1000, "1K" not "1000"); the multiplier */
static int choose_multiplier(const nfo *o, uint32_t crec, dec *x, int *minfrac) {
    int mag = dec_mag(x);
    int mult = multiplier_of(o, crec, mag);
    if (x->nd) x->e += mult;
    nf_round(o, x, minfrac);
    if (!x->nd) return mult;
    if (dec_mag(x) == mag + mult) return mult;
    int m2 = multiplier_of(o, crec, mag + 1);
    if (m2 == mult) return mult;
    if (x->nd) x->e += m2 - mult;
    nf_round(o, x, minfrac);
    return m2;
}

/* ---- the formatted text: units with a field each ---- */

/* the parts' types (formatToParts' names) */
enum { PT_LITERAL, PT_INTEGER, PT_GROUP, PT_DECIMAL, PT_FRACTION, PT_MINUS, PT_PLUS, PT_PERCENT, PT_CURRENCY, PT_UNIT,
       PT_COMPACT, PT_EXPSEP, PT_EXPMINUS, PT_EXPINT, PT_NAN, PT_INFINITY, PT_APPROX, PT_UNITPERCENT, PT_N_NUMBER };
static const char *const pt_names[] = { "literal", "integer", "group", "decimal", "fraction", "minusSign", "plusSign",
    "percentSign", "currency", "unit", "compact", "exponentSeparator", "exponentMinusSign", "exponentInteger", "nan",
    "infinity", "approximatelySign", "unit" };

static void put_u(vector<uint16_t> *tx, vector<uint8_t> *ty, const uint16_t *u, uint32_t n, uint32_t type) {
    for (uint32_t i = 0; i < n; i++) { push_back(*tx, u[i]); push_back(*ty, (uint8_t)type); }
}
static void put_c(vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t c, uint32_t type) {
    if (c >= 0x10000) {
        push_back(*tx, (uint16_t)(0xD800 + ((c - 0x10000) >> 10)));
        push_back(*ty, (uint8_t)type);
        c = 0xDC00 + ((c - 0x10000) & 0x3FF);
    }
    push_back(*tx, (uint16_t)c);
    push_back(*ty, (uint8_t)type);
}
/* a record's field appended, its type given */
static void put_fld(vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t rec, uint32_t f, uint32_t type) {
    uint16_t b[512];
    uint32_t n = fld_units(rec, f, b, 512);
    put_u(tx, ty, b, n, type);
}

/* ---- a numbering system's symbols and digits ---- */

enum { SY_DECIMAL, SY_GROUP, SY_PERCENT, SY_PLUS, SY_MINUS, SY_APPROX, SY_EXP, SY_SUPEXP, SY_PERMILLE, SY_INFINITY, SY_NAN,
       SY_TIMESEP, SY_N };
enum { PA_DECIMAL = SY_N, PA_PERCENT, PA_CURRENCY, PA_ACCOUNTING, PA_NOCURRENCY, PA_SCIENTIFIC, PA_APPROX, PA_ATLEAST,
       PA_ATMOST, PA_RANGE, PA_UNITPATTERN };

/* a field of the numbering system's record, else of latn's (root's aliases to the locale's latn) */
static uint32_t ns_field(const nfo *o, uint32_t f, uint16_t *buf, uint32_t cap) {
    uint32_t r = rec_get(o->li, RKEY(IDK_NUM, o->nsi));
    uint32_t n = fld_units(r, f, buf, cap);
    if (n) return n;
    int latn = db_index(IDB_NUMBERING, IDB_NUMBERING_N, "latn");
    return fld_units(rec_get(o->li, RKEY(IDK_NUM, latn)), f, buf, cap);
}
static void put_sym(const nfo *o, vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t sym, uint32_t type) {
    uint16_t b[64];
    uint32_t n = ns_field(o, sym, b, 64);
    put_u(tx, ty, b, n, type);
}
/* the numbering system's digit d (its UTF-8 in the table's value) */
static uint32_t ns_digit(int nsi, int d) {
    uint32_t p = db32(IDB_NUMBERING + (uint32_t)nsi * 8 + 4);
    for (int k = 0; k < d; k++) {
        uint32_t c = db8(p);
        p += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    }
    uint32_t c = db8(p);
    if (c < 0x80) return c;
    if (c < 0xE0) return (c & 0x1F) << 6 | (db8(p + 1) & 0x3F);
    if (c < 0xF0) return (c & 0x0F) << 12 | (db8(p + 1) & 0x3F) << 6 | (db8(p + 2) & 0x3F);
    return (c & 0x07) << 18 | (db8(p + 1) & 0x3F) << 12 | (db8(p + 2) & 0x3F) << 6 | (db8(p + 3) & 0x3F);
}
/* a decimal digit (Nd): ASCII's, or a numbering system's whose digits are consecutive */
static int is_nd(uint32_t c) {
    if (c >= '0' && c <= '9') return 1;
    if (c < 0x660) return 0;
    for (uint32_t i = 0; i < IDB_NUMBERING_N; i++) {
        uint32_t z = ns_digit((int)i, 0);
        if (c >= z && c <= z + 9 && ns_digit((int)i, 9) == z + 9) return 1;
    }
    return 0;
}

/* ---- patterns (CLDR's number patterns, as ICU's PatternParser reads them) ---- */

/* a pattern read: each subpattern's prefix and suffix (spans of the pattern), whether it
   has a negative one, the grouping sizes (ICU's: the last three groups, -1 none) */
typedef struct npat {
    uint16_t p[160];
    uint32_t n;
    uint32_t pre[2][2], suf[2][2];            /* [positive, negative][start, end] */
    int has_neg, has_body;
    int g1, g2, g3;
} npat;

static void pat_parse(npat *pt, const uint16_t *src, uint32_t n) {
    if (n > 160) n = 160;
    for (uint32_t i = 0; i < n; i++) pt->p[i] = src[i];
    pt->n = n;
    pt->has_neg = 0;
    pt->has_body = 0;
    pt->g1 = 0; pt->g2 = -1; pt->g3 = -1;
    uint32_t i = 0;
    for (int sub = 0; sub < 2; sub++) {
        uint32_t s = i;
        int q = 0;
        /* the prefix: up to the number's first character, outside quotes */
        while (i < n) {
            uint16_t c = pt->p[i];
            if (c == '\'') q = !q;
            else if (!q && (c == '#' || c == '0' || c == '@' || c == ',' || c == '.' || (c >= '1' && c <= '9'))) break;
            else if (!q && c == ';') break;
            i++;
        }
        pt->pre[sub][0] = s;
        pt->pre[sub][1] = i;
        /* the number: digits, groupings, the point, an exponent */
        int body = 0, seen_point = 0;
        while (i < n) {
            uint16_t c = pt->p[i];
            if (c == '#' || c == '0' || c == '@' || (c >= '1' && c <= '9')) {
                body = 1;
                if (!seen_point && sub == 0) pt->g1++;
            } else if (c == ',') {
                if (!seen_point && sub == 0) { pt->g3 = pt->g2; pt->g2 = pt->g1; pt->g1 = 0; }
            } else if (c == '.') seen_point = 1;
            else if (c == 'E' && i + 1 < n && (pt->p[i + 1] == '0' || pt->p[i + 1] == '+')) {
                i++;
                while (i < n && (pt->p[i] == '0' || pt->p[i] == '+')) i++;
                continue;
            } else break;
            i++;
        }
        if (sub == 0) pt->has_body = body;
        s = i;
        q = 0;
        while (i < n) {
            uint16_t c = pt->p[i];
            if (c == '\'') q = !q;
            else if (!q && c == ';') break;
            i++;
        }
        pt->suf[sub][0] = s;
        pt->suf[sub][1] = i;
        if (sub == 0) {
            if (i < n && pt->p[i] == ';') { pt->has_neg = 1; i++; }
            else break;
        }
    }
}
/* whether an affix span has c outside quotes */
static int span_has(const npat *pt, const uint32_t *span, uint16_t c) {
    int q = 0;
    for (uint32_t i = span[0]; i < span[1]; i++) {
        if (pt->p[i] == '\'') q = !q;
        else if (!q && pt->p[i] == c) return 1;
    }
    return 0;
}

/* the sign a pattern shows (ICU's PatternSignType) */
enum { PS_POS, PS_POS_SIGN, PS_NEG };
enum { SG_NEG, SG_NEGZERO, SG_POSZERO, SG_POS };

static int sign_type(int display, int signum) {
    switch (display) {
    case SD_AUTO: return signum <= SG_NEGZERO ? PS_NEG : PS_POS;
    case SD_ALWAYS: return signum <= SG_NEGZERO ? PS_NEG : PS_POS_SIGN;
    case SD_EXCEPTZERO: return signum == SG_NEG ? PS_NEG : signum == SG_POS ? PS_POS_SIGN : PS_POS;
    case SD_NEGATIVE: return signum == SG_NEG ? PS_NEG : PS_POS;
    default: return PS_POS;
    }
}

/* what an affix's currency signs show (the plural name's form), its literal text's field and
   its percent sign's (PT_PERCENT, or PT_UNITPERCENT for a unit's) */
typedef struct naff { int plural; uint32_t field; int percent_type; } naff;

static void put_currency(const nfo *o, vector<uint16_t> *tx, vector<uint8_t> *ty, int count, int plural) {
    uint32_t rec = o->cur_idx >= 0 ? rec_get(o->li, RKEY(IDK_CUR, o->cur_idx)) : 0;
    uint16_t b[256];
    uint32_t n = 0;
    if (count == 2 || o->cur_display == CD_CODE || (count == 1 && !rec)) {
        for (int k = 0; k < 3; k++) b[n++] = (uint16_t)o->cur[k];
    } else if (count == 3) {
        n = fld_units(rec, 3 + (uint32_t)plural, b, 256);
        if (!n) n = fld_units(rec, 3 + PL_OTHER, b, 256);
        if (!n) n = fld_units(rec, 2, b, 256);
    } else n = fld_units(rec, o->cur_display == CD_NARROW ? 1 : 0, b, 256);
    if (!n) for (int k = 0; k < 3; k++) b[n++] = (uint16_t)o->cur[k];
    put_u(tx, ty, b, n, PT_CURRENCY);
}

/* the currency symbol's spacing flags (bit 0 its last code point, bit 1 its first, may
   take spacing): the record's own, or a code's (letters) */
static int currency_flags(const nfo *o) {
    if (o->cur_display == CD_CODE || o->cur_idx < 0) return 3;
    uint32_t rec = rec_get(o->li, RKEY(IDK_CUR, o->cur_idx));
    uint32_t n;
    uint32_t p = fld(rec, o->cur_display == CD_NARROW ? 10 : 9, &n);
    return n ? (int)(db8(p) - '0') : 3;
}

/* an affix rendered: ICU's patternInfoToStringBuilder (the sign placed, '-' replaced),
   then its tokens (AffixUtils): quoted text, the signs, percent, per mille, currency */
static void render_affix(const npat *pt, const nfo *o, const naff *af, int prefix, int stype, int approx, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    int pos_plus = span_has(pt, pt->pre[0], '+') || span_has(pt, pt->suf[0], '+');
    int neg_minus = pt->has_neg && (span_has(pt, pt->pre[1], '-') || span_has(pt, pt->suf[1], '-'));
    int plus_rep = stype == PS_POS_SIGN && !pos_plus;
    int use_neg = pt->has_neg && (stype == PS_NEG || (neg_minus && (plus_rep || approx)));
    int prepend;
    if (!prefix || use_neg) prepend = 0;
    else if (stype == PS_NEG) prepend = 1;
    else prepend = plus_rep || approx;
    /* the sign's stand-ins: 'm' minus, 'p' plus, 'a' approximately */
    char signs[3];
    signs[0] = signs[1] = signs[2] = 0;
    if (approx) {
        signs[0] = 'a';
        if (plus_rep) signs[1] = 'p';
        else if (stype == PS_NEG) signs[1] = 'm';
    } else if (plus_rep) signs[0] = 'p';
    else signs[0] = 'm';
    uint32_t span[2];
    if (prefix) { span[0] = pt->pre[use_neg][0]; span[1] = pt->pre[use_neg][1]; }
    else { span[0] = pt->suf[use_neg][0]; span[1] = pt->suf[use_neg][1]; }
    /* the affix's characters, the sign first when prepended; a token's code: 0x110000 + kind */
    uint32_t seq[200];
    uint32_t ns = 0;
    if (prepend) seq[ns++] = '-';
    for (uint32_t i = span[0]; i < span[1] && ns < 190; i++) seq[ns++] = pt->p[i];
    int q = 0;
    for (uint32_t i = 0; i < ns; i++) {
        uint32_t c = seq[i];
        if (c == '\'') {
            if (i + 1 < ns && seq[i + 1] == '\'') { put_c(tx, ty, '\'', af->field); i++; continue; }
            q = !q;
            continue;
        }
        if (q) { put_c(tx, ty, c, af->field); continue; }
        if (c == '-') {
            for (int k = 0; k < 2 && signs[k]; k++) {
                if (signs[k] == 'm') put_sym(o, tx, ty, SY_MINUS, PT_MINUS);
                else if (signs[k] == 'p') put_sym(o, tx, ty, SY_PLUS, PT_PLUS);
                else put_sym(o, tx, ty, SY_APPROX, PT_APPROX);
            }
            continue;
        }
        if (c == '+') { put_sym(o, tx, ty, SY_PLUS, PT_PLUS); continue; }
        if (c == '~') { put_sym(o, tx, ty, SY_APPROX, PT_APPROX); continue; }
        if (c == '%') { put_sym(o, tx, ty, SY_PERCENT, (uint32_t)af->percent_type); continue; }
        if (c == 0x2030) { put_sym(o, tx, ty, SY_PERMILLE, (uint32_t)af->percent_type); continue; }
        if (c == 0xA4) {
            int count = 1;
            while (i + 1 < ns && seq[i + 1] == 0xA4) { count++; i++; }
            put_currency(o, tx, ty, count, af->plural);
            continue;
        }
        put_c(tx, ty, c, af->field);
    }
}

/* ---- a number formatted: its pieces ---- */

/* the pieces of a formatted number, segments of one text: the outer modifier's prefix (a
   unit's or a currency name's pattern), the middle's (the pattern's affixes), the number,
   the inner (an exponent), the middle's suffix, the outer's; and what a range compares */
enum { SEG_OUTER_PRE, SEG_MID_PRE, SEG_NUM, SEG_INNER, SEG_MID_SUF, SEG_OUTER_SUF, SEG_N };
typedef struct nfinfo {
    uint32_t cut[SEG_N + 1];
    int spacing;                              /* the middle's currency spacing: bit 0 its prefix's, bit 1 its suffix's */
    int signum, outer, plural, mult;
    int nan;
} nfinfo;

/* the integer and fraction digits, grouped, in the numbering system's digits */
static void put_number(const nfo *o, const npat *main_pat, const dec *x, int minfrac, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    int nsi = o->nsi;
    if (x->kind == DK_INF) { put_sym(o, tx, ty, SY_INFINITY, PT_INFINITY); return; }
    if (x->kind == DK_NAN) { put_sym(o, tx, ty, SY_NAN, PT_NAN); return; }
    int upper = dec_mag(x);
    if (upper < o->mnid - 1) upper = o->mnid - 1;
    if (upper < 0) upper = 0;
    /* grouping (ICU's Grouper): sizes from the pattern, the minimum by the strategy */
    int g1 = main_pat->g1, g2 = main_pat->g2, g3 = main_pat->g3;
    int min_group = 1;
    if (o->grouping == UG_OFF) g1 = -1;
    else {
        int locale_min = 1;
        uint32_t mg = rec_get(o->li, RKEY(IDK_MINGROUP, 0));
        if (mg) locale_min = (int)(db8(mg) - '0');
        if (o->grouping == UG_AUTO) min_group = locale_min;
        else if (o->grouping == UG_MIN2) min_group = locale_min > 2 ? locale_min : 2;
        if (g2 == -1) g1 = o->grouping == UG_ALWAYS ? 3 : -1;
        if (g3 == -1) g2 = g1;
    }
    int cur_sep = o->style == ST_CURRENCY;
    (void)cur_sep;
    for (int p = upper; p >= 0; p--) {
        put_c(tx, ty, ns_digit(nsi, dec_digit(x, p)), PT_INTEGER);
        if (p > 0 && g1 > 0) {
            int pos = p - g1;
            if (pos >= 0 && pos % g2 == 0 && upper - g1 + 1 >= min_group) put_sym(o, tx, ty, SY_GROUP, PT_GROUP);
        }
    }
    int low = dec_low(x);
    int lowest = low < -minfrac ? low : -minfrac;
    if (lowest < 0) {
        put_sym(o, tx, ty, SY_DECIMAL, PT_DECIMAL);
        for (int p = -1; p >= lowest; p--) put_c(tx, ty, ns_digit(nsi, dec_digit(x, p)), PT_FRACTION);
    }
}

/* the outer modifier's pattern: a unit's for the plural form ("{0} km"), or a currency
   name's ("{0} {1}" with the name); into buf, its length */
static uint32_t unit_pattern(const nfo *o, int unit, int plural, uint16_t *buf, uint32_t cap) {
    uint32_t rec = rec_get(o->li, RKEY(IDK_UNIT, unit));
    uint32_t w = (uint32_t)(o->unit_display == UD_LONG ? 0 : o->unit_display == UD_SHORT ? 1 : 2) * 8;
    uint32_t n = fld_units(rec, w + 2 + (uint32_t)plural, buf, cap);
    if (!n) n = fld_units(rec, w + 2 + PL_OTHER, buf, cap);
    return n;
}

/* a pattern "a{0}b" split at its placeholder into what precedes and follows; 0 none */
static int split_placeholder(const uint16_t *p, uint32_t n, uint32_t *at) {
    for (uint32_t i = 0; i + 2 < n; i++)
        if (p[i] == '{' && p[i + 1] == '0' && p[i + 2] == '}') { *at = i; return 1; }
    return 0;
}

/* a unit's pattern for a plural form, a compound's built from its parts (ICU's
   LongNameHandler: the per unit's perUnitPattern around the unit's, else the "per" pattern
   with the per unit's singular name) */
static uint32_t outer_unit_pattern(const nfo *o, int plural, uint16_t *out, uint32_t cap) {
    uint16_t a[256];
    uint32_t na = unit_pattern(o, o->unit, plural, a, 256);
    if (o->per < 0) {
        for (uint32_t i = 0; i < na && i < cap; i++) out[i] = a[i];
        return na < cap ? na : cap;
    }
    uint32_t rec = rec_get(o->li, RKEY(IDK_UNIT, o->per));
    uint32_t w = (uint32_t)(o->unit_display == UD_LONG ? 0 : o->unit_display == UD_SHORT ? 1 : 2) * 8;
    uint16_t perp[256];
    uint32_t np = fld_units(rec, w + 1, perp, 256);
    uint16_t compound[256];
    uint32_t nc = 0;
    if (np) {
        for (uint32_t i = 0; i < np; i++) compound[nc++] = perp[i];
    } else {
        /* "{0} per {1}" with {1} the per unit's singular pattern, its placeholder dropped, trimmed */
        uint16_t pp[128];
        uint32_t npp = fld_units(rec_get(o->li, RKEY(IDK_UNITPER, 0)), w / 8, pp, 128);
        uint16_t one[256];
        uint32_t n1 = unit_pattern(o, o->per, PL_ONE, one, 256);
        uint16_t den[256];
        uint32_t nd = 0;
        for (uint32_t i = 0; i < n1; i++) {
            if (i + 2 < n1 && one[i] == '{' && one[i + 1] == '0' && one[i + 2] == '}') { i += 2; continue; }
            den[nd++] = one[i];
        }
        uint32_t a0 = 0, b0 = nd;
        while (a0 < b0 && (den[a0] == ' ' || den[a0] == 0xA0 || den[a0] == 0x202F)) a0++;
        while (b0 > a0 && (den[b0 - 1] == ' ' || den[b0 - 1] == 0xA0 || den[b0 - 1] == 0x202F)) b0--;
        for (uint32_t i = 0; i < npp && nc < 250; i++) {
            if (i + 2 < npp && pp[i] == '{' && pp[i + 1] == '1' && pp[i + 2] == '}') {
                for (uint32_t k = a0; k < b0 && nc < 250; k++) compound[nc++] = den[k];
                i += 2;
                continue;
            }
            compound[nc++] = pp[i];
        }
    }
    /* the unit's pattern put in the compound's placeholder */
    uint32_t k = 0;
    for (uint32_t i = 0; i < nc && k + 1 < cap; i++) {
        if (i + 2 < nc && compound[i] == '{' && compound[i + 1] == '0' && compound[i + 2] == '}') {
            for (uint32_t j = 0; j < na && k + 1 < cap; j++) out[k++] = a[j];
            i += 2;
            continue;
        }
        out[k++] = compound[i];
    }
    return k;
}

/* the main pattern of the style (ICU's getPatternForStyle, the numbering system's else latn's) */
static void main_pattern(const nfo *o, npat *pt, int *cldr_unit) {
    int compact = o->notation == NN_COMPACT;
    int percent_unit = o->style == ST_UNIT && o->unit >= 0 && o->per < 0 && !strcmp("percent", "percent") && 0;
    (void)percent_unit;
    *cldr_unit = 0;
    uint32_t which = PA_DECIMAL;
    if (o->style == ST_PERCENT) {
        if (compact) *cldr_unit = 1;
        else which = PA_PERCENT;
    } else if (o->style == ST_UNIT) {
        /* the percent unit, short or narrow and not compact, is the percent pattern */
        char u[24];
        u[0] = 0;
        if (o->unit >= 0 && o->per < 0) db_copy(db32(IDB_UNITS + (uint32_t)o->unit * 8), u, sizeof u);
        if (!strcmp(u, "percent") && o->unit_display != UD_LONG && !compact) which = PA_PERCENT;
        else *cldr_unit = 1;
    } else if (o->style == ST_CURRENCY && o->cur_display != CD_NAME) which = o->cur_sign ? PA_ACCOUNTING : PA_CURRENCY;
    uint16_t b[160];
    uint32_t n = ns_field(o, which, b, 160);
    pat_parse(pt, b, n);
}

/* one number formatted into segments (ICU's NumberFormatterImpl, the micro-props in order:
   rounding and notation, the middle modifier's affixes, the outer's long names) */
static void nf_render(const nfo *o, dec *x, int force_plural, vector<uint16_t> *tx, vector<uint8_t> *ty, nfinfo *inf) {
    memset(inf, 0, sizeof *inf);
    npat mp;
    int cldr_unit;
    main_pattern(o, &mp, &cldr_unit);
    int minfrac = 0, exponent = 0;
    uint32_t crec = 0;
    if (x->kind == DK_FINITE && o->style == ST_PERCENT && x->nd) x->e += 2;
    if (o->notation == NN_COMPACT) {
        int style = o->style == ST_CURRENCY && o->cur_display != CD_NAME ? 2 : o->compact;
        crec = compact_rec(o, style);
    }
    uint32_t pset = plural_set(o->tag, 0);
    /* the outer modifier's plural form: ICU's long names come before compact notation in
       its chain, so they see the number rounded but not scaled; after scientific, its mantissa */
    int outer_plural = -1;
    if (o->notation == NN_COMPACT) {
        dec c = *x;
        int mf = 0;
        nf_round(o, &c, &mf);
        outer_plural = plural_of(pset, &c, mf, 0);
    }
    int mult = 0;
    if (x->kind == DK_FINITE && o->notation != NN_STANDARD && x->nd) mult = choose_multiplier(o, crec, x, &minfrac);
    else nf_round(o, x, &minfrac);
    if (o->notation == NN_SCIENTIFIC || o->notation == NN_ENGINEERING) exponent = -mult;
    inf->mult = mult;
    /* the sign after rounding */
    int zero = x->kind == DK_FINITE && !x->nd;
    inf->signum = x->kind == DK_NAN ? SG_POS : x->neg ? (zero ? SG_NEGZERO : SG_NEG) : (zero ? SG_POSZERO : SG_POS);
    inf->nan = x->kind == DK_NAN;
    int plural = plural_of(pset, x, minfrac, 0);
    if (outer_plural < 0) outer_plural = plural;
    if (force_plural >= 0) outer_plural = force_plural;
    /* the compact pattern, by the magnitude and the scaled number's plural form */
    npat cp;
    int use_compact = 0;
    if (o->notation == NN_COMPACT && x->kind == DK_FINITE) {
        int mag = x->nd ? dec_mag(x) - mult : 0;
        int largest;
        compact_multiplier(crec, 0, &largest);
        if (mag >= 0 && crec) {
            if (mag > largest) mag = largest;
            if (mag >= 3) {
                uint16_t b[96];
                uint32_t n = 0;
                if (dec_low(x) >= 0 && x->nd == 1 && dec_mag(x) == 0 && x->d[0] == 1) n = compact_pattern(crec, mag, 6, b, 96);
                if (!n) n = compact_pattern(crec, mag, plural, b, 96);
                if (!n && plural != PL_OTHER) n = compact_pattern(crec, mag, PL_OTHER, b, 96);
                if (n && !(n == 1 && b[0] == '0')) { pat_parse(&cp, b, n); use_compact = 1; }
            }
        }
    }
    /* the outer modifier */
    uint16_t outer[300];
    uint32_t no = 0;
    if (cldr_unit) {
        inf->outer = 1;
        if (o->style == ST_PERCENT) {
            nfo po = *o;
            po.unit = db_index(IDB_UNITS, IDB_UNITS_N, "percent");
            po.per = -1;
            po.unit_display = UD_SHORT;
            no = outer_unit_pattern(&po, outer_plural, outer, 300);
        } else no = outer_unit_pattern(o, outer_plural, outer, 300);
    } else if (o->style == ST_CURRENCY && o->cur_display == CD_NAME) {
        inf->outer = 2;
        uint16_t up[64];
        uint32_t nu = ns_field(o, PA_UNITPATTERN + (uint32_t)outer_plural, up, 64);
        if (!nu) nu = ns_field(o, PA_UNITPATTERN + PL_OTHER, up, 64);
        for (uint32_t i = 0; i < nu && no < 290; i++) {
            if (i + 2 < nu && up[i] == '{' && up[i + 1] == '1' && up[i + 2] == '}') { outer[no++] = 0xFFFF; i += 2; continue; }
            outer[no++] = up[i];
        }
    }
    inf->plural = outer_plural;
    /* the segments */
    uint32_t at;
    inf->cut[SEG_OUTER_PRE] = (uint32_t)len(*tx);
    int has_ph = no && split_placeholder(outer, no, &at);
    uint32_t outer_type = inf->outer == 2 ? PT_CURRENCY : PT_UNIT;
    if (has_ph) {
        for (uint32_t i = 0; i < at; i++) {
            if (outer[i] == 0xFFFF) put_currency(o, tx, ty, 3, outer_plural);
            else put_c(tx, ty, outer[i], outer_type);
        }
    } else if (no) {
        /* a long name with no placeholder ("لتر" for one liter) stands for the number */
        for (uint32_t i = 0; i < no; i++) {
            if (outer[i] == 0xFFFF) put_currency(o, tx, ty, 3, outer_plural);
            else put_c(tx, ty, outer[i], outer_type);
        }
        for (int k = SEG_MID_PRE; k <= SEG_N; k++) inf->cut[k] = (uint32_t)len(*tx);
        return;
    }
    inf->cut[SEG_MID_PRE] = (uint32_t)len(*tx);
    naff af;
    af.plural = plural;
    af.percent_type = o->style == ST_UNIT ? PT_UNITPERCENT : PT_PERCENT;
    int stype = sign_type(o->sign, inf->signum);
    if (use_compact) { af.field = PT_COMPACT; render_affix(&cp, o, &af, 1, stype, o->approx, tx, ty); }
    else { af.field = PT_LITERAL; render_affix(&mp, o, &af, 1, stype, o->approx, tx, ty); }
    inf->cut[SEG_NUM] = (uint32_t)len(*tx);
    /* a compact pattern with no digits ("mille") stands for the number */
    int body = !use_compact || cp.has_body;
    if (body) put_number(o, &mp, x, minfrac, tx, ty);
    inf->cut[SEG_INNER] = (uint32_t)len(*tx);
    if (body && (o->notation == NN_SCIENTIFIC || o->notation == NN_ENGINEERING)) {
        if (x->kind == DK_FINITE) {
            put_sym(o, tx, ty, SY_EXP, PT_EXPSEP);
            if (exponent < 0) put_sym(o, tx, ty, SY_MINUS, PT_EXPMINUS);
            int ex = exponent < 0 ? -exponent : exponent;
            char eb[16];
            int en = snprintf(eb, sizeof eb, "%d", ex);
            for (int k = 0; k < en; k++) put_c(tx, ty, ns_digit(o->nsi, eb[k] - '0'), PT_EXPINT);
        }
    }
    inf->cut[SEG_MID_SUF] = (uint32_t)len(*tx);
    if (use_compact) render_affix(&cp, o, &af, 0, stype, o->approx, tx, ty);
    else render_affix(&mp, o, &af, 0, stype, o->approx, tx, ty);
    inf->cut[SEG_OUTER_SUF] = (uint32_t)len(*tx);
    if (has_ph) {
        for (uint32_t i = at + 3; i < no; i++) {
            if (outer[i] == 0xFFFF) put_currency(o, tx, ty, 3, outer_plural);
            else put_c(tx, ty, outer[i], outer_type);
        }
    }
    inf->cut[SEG_N] = (uint32_t)len(*tx);
    /* the currency spacing the middle's affixes take (ICU's CurrencySpacingEnabledModifier):
       a currency sign at the affix's edge that is not a symbol nor a separator */
    if (o->style == ST_CURRENCY) {
        int fl = currency_flags(o);
        uint32_t a = inf->cut[SEG_MID_PRE], b = inf->cut[SEG_NUM];
        if (b > a && (*ty)[b - 1] == PT_CURRENCY && (fl & 1)) inf->spacing |= 1;
        a = inf->cut[SEG_MID_SUF];
        b = inf->cut[SEG_OUTER_SUF];
        if (b > a && (*ty)[a] == PT_CURRENCY && (fl & 2)) inf->spacing |= 2;
    }
}

/* the code point at units i (forward) or before i (backward) */
static uint32_t cp_at_u(vector<uint16_t> *tx, uint32_t i) {
    uint32_t c = (*tx)[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len(*tx)) c = 0x10000 + ((c - 0xD800) << 10) + ((*tx)[i + 1] - 0xDC00u);
    return c;
}
static uint32_t cp_before_u(vector<uint16_t> *tx, uint32_t i) {
    uint32_t c = (*tx)[i - 1];
    if (c >= 0xDC00 && c <= 0xDFFF && i >= 2) c = 0x10000 + (((*tx)[i - 2] - 0xD800u) << 10) + (c - 0xDC00);
    return c;
}

/* the segments [a, b) of a rendering copied to out */
static void copy_segs(vector<uint16_t> *tx, vector<uint8_t> *ty, const nfinfo *inf, int a, int b, vector<uint16_t> *ot, vector<uint8_t> *oy) {
    for (uint32_t i = inf->cut[a]; i < inf->cut[b]; i++) { push_back(*ot, (*tx)[i]); push_back(*oy, (*ty)[i]); }
}
static const uint16_t nbsp[1] = { 0xA0 };

/* a single number's text assembled from its segments, the currency spacing inserted */
static void nf_assemble(vector<uint16_t> *tx, vector<uint8_t> *ty, const nfinfo *inf, vector<uint16_t> *ot, vector<uint8_t> *oy) {
    copy_segs(tx, ty, inf, SEG_OUTER_PRE, SEG_NUM, ot, oy);
    uint32_t n0 = inf->cut[SEG_NUM], n1 = inf->cut[SEG_MID_SUF];
    if ((inf->spacing & 1) && n1 > n0 && is_nd(cp_at_u(tx, n0))) put_u(ot, oy, nbsp, 1, PT_LITERAL);
    copy_segs(tx, ty, inf, SEG_NUM, SEG_MID_SUF, ot, oy);
    if ((inf->spacing & 2) && n1 > n0 && is_nd(cp_before_u(tx, n1))) put_u(ot, oy, nbsp, 1, PT_LITERAL);
    copy_segs(tx, ty, inf, SEG_MID_SUF, SEG_N, ot, oy);
}

/* ICU's DEFAULT_IGNORABLES, trimmed from a field's edges: spaces, tab, bidi controls,
   variation selectors */
static int ignorable(uint32_t c) {
    return c == ' ' || c == '\t' || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F ||
           c == 0x3000 || c == 0x061C || c == 0x200E || c == 0x200F || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) ||
           (c >= 0xFE00 && c <= 0xFE0F);
}

/* the parts: runs of one field, each but a group's trimmed of ignorables (what is trimmed is
   literal), adjacent literals of one source joined: triples (start, end, type), and the
   source of each run when src is given */
static void parts_of(vector<uint16_t> *tx, vector<uint8_t> *ty, vector<uint8_t> *src, vector<uint32_t> *out) {
    uint32_t n = (uint32_t)len(*tx);
    vector<uint8_t> txb;
    for (uint32_t i = 0; i < n; i++) push_back(txb, (*ty)[i]);
    uint32_t i = 0;
    while (i < n) {
        uint32_t j = i + 1;
        while (j < n && (*ty)[j] == (*ty)[i] && (!src || (*src)[j] == (*src)[i])) j++;
        uint32_t t = (*ty)[i];
        if (t != PT_LITERAL && t != PT_GROUP) {
            uint32_t a = i, b = j;
            while (a < b && ignorable((*tx)[a])) { txb[a] = PT_LITERAL; a++; }
            while (b > a && ignorable((*tx)[b - 1])) { txb[b - 1] = PT_LITERAL; b--; }
        }
        i = j;
    }
    i = 0;
    while (i < n) {
        uint32_t j = i + 1;
        while (j < n && txb[j] == txb[i] && (!src || (*src)[j] == (*src)[i]) && (txb[i] == PT_LITERAL || (*ty)[j] == (*ty)[i])) j++;
        push_back(*out, i);
        push_back(*out, j);
        push_back(*out, txb[i]);
        i = j;
    }
}

/* the parts as an array of {type, value} (and source) objects */
static jv parts_array(js_vm *vm, vector<uint16_t> *tx, vector<uint32_t> *pt, const char *const *names, vector<uint8_t> *src) {
    uint32_t arr = js_array_new(vm, 0);
    uint32_t a_type = js_atom_ascii(vm, "type"), a_value = js_atom_ascii(vm, "value"), a_source = js_atom_ascii(vm, "source");
    static const char *const srcn[] = { "startRange", "endRange", "shared" };
    for (uint32_t k = 0; k + 2 < len(*pt); k += 3) {
        uint32_t a = (*pt)[k], b = (*pt)[k + 1], t = (*pt)[k + 2];
        uint32_t o = js_obj_new(vm, JK_PLAIN, vm->object_proto);
        js_define(vm, o, a_type, jv_str(js_str_ascii(vm, names[t])), 0);
        js_define(vm, o, a_value, jv_str(js_str_new(vm, data(*tx) + a, b - a)), 0);
        if (src) js_define(vm, o, a_source, jv_str(js_str_ascii(vm, srcn[(*src)[a]])), 0);
        js_array_push(vm, arr, jv_obj(o));
    }
    return jv_obj(arr);
}

/* a NumberFormat's one number: the string, or its parts */
static jv nf_format_value(js_vm *vm, uint32_t nf, jv v, int to_parts) {
    dec x;
    if (!to_intl_mv(vm, v, &x)) return JV_HOLE;
    nfo o;
    nfo_read(vm, nf, &o);
    vector<uint16_t> tx;
    vector<uint8_t> ty;
    nfinfo inf;
    nf_render(&o, &x, -1, &tx, &ty, &inf);
    vector<uint16_t> ot;
    vector<uint8_t> oy;
    nf_assemble(&tx, &ty, &inf, &ot, &oy);
    if (!to_parts) return jv_str(js_str_new(vm, data(ot), len(ot)));
    vector<uint32_t> pt;
    parts_of(&ot, &oy, 0, &pt);
    return parts_array(vm, &ot, &pt, pt_names, 0);
}

/* the segments of two renderings alike (a modifier semantically equivalent): their units and fields */
static int segs_equal(vector<uint16_t> *txa, vector<uint8_t> *y1, const nfinfo *i1, vector<uint16_t> *txb, vector<uint8_t> *y2, const nfinfo *i2, int a, int b) {
    uint32_t n1 = i1->cut[b] - i1->cut[a], n2 = i2->cut[b] - i2->cut[a];
    if (n1 != n2) return 0;
    for (uint32_t k = 0; k < n1; k++)
        if ((*txa)[i1->cut[a] + k] != (*txb)[i2->cut[a] + k] || (*y1)[i1->cut[a] + k] != (*y2)[i2->cut[a] + k]) return 0;
    return 1;
}
static uint32_t cp_count(vector<uint16_t> *tx, uint32_t a, uint32_t b) {
    uint32_t n = 0;
    for (uint32_t i = a; i < b; i++) if ((*tx)[i] < 0xDC00 || (*tx)[i] > 0xDFFF) n++;
    return n;
}

/* formatRange (ICU's NumberRangeFormatterImpl, collapse auto, identity fallback
   approximately): the string or its parts with their sources */
static int pattern_ws(uint32_t c) { return (c >= 9 && c <= 13) || c == ' ' || c == 0x85 || c == 0x200E || c == 0x200F || c == 0x2028 || c == 0x2029; }

/* units appended with a source */
static void put_src(vector<uint16_t> *ot, vector<uint8_t> *oy, vector<uint8_t> *os, vector<uint16_t> *tx, vector<uint8_t> *ty,
                    uint32_t a, uint32_t b, uint32_t src) {
    for (uint32_t i = a; i < b; i++) { push_back(*ot, (*tx)[i]); push_back(*oy, (*ty)[i]); push_back(*os, (uint8_t)src); }
}
static void put_lit(vector<uint16_t> *ot, vector<uint8_t> *oy, vector<uint8_t> *os, uint32_t c, uint32_t src) {
    push_back(*ot, (uint16_t)c);
    push_back(*oy, PT_LITERAL);
    push_back(*os, (uint8_t)src);
}

/* one end of a range: its number and inner modifier, its middle unless collapsed, its outer
   unless collapsed (with the currency spacing its own middle takes) */
static void range_end(vector<uint16_t> *ot, vector<uint8_t> *oy, vector<uint8_t> *os, vector<uint16_t> *t, vector<uint8_t> *y,
                      const nfinfo *in, int c_mid, int c_outer, uint32_t src) {
    if (!c_outer) put_src(ot, oy, os, t, y, in->cut[SEG_OUTER_PRE], in->cut[SEG_MID_PRE], src);
    uint32_t n0 = in->cut[SEG_NUM], n1 = in->cut[SEG_MID_SUF];
    if (!c_mid) {
        put_src(ot, oy, os, t, y, in->cut[SEG_MID_PRE], in->cut[SEG_NUM], src);
        if ((in->spacing & 1) && n1 > n0 && is_nd(cp_at_u(t, n0))) put_lit(ot, oy, os, 0xA0, src);
    }
    put_src(ot, oy, os, t, y, in->cut[SEG_NUM], in->cut[SEG_MID_SUF], src);
    if (!c_mid) {
        if ((in->spacing & 2) && n1 > n0 && is_nd(cp_before_u(t, n1))) put_lit(ot, oy, os, 0xA0, src);
        put_src(ot, oy, os, t, y, in->cut[SEG_MID_SUF], in->cut[SEG_OUTER_SUF], src);
    }
    if (!c_outer) put_src(ot, oy, os, t, y, in->cut[SEG_OUTER_SUF], in->cut[SEG_N], src);
}

/* the "Invalid start : X" an end that is not a number gives */
static jv range_error(js_vm *vm, uint32_t proto, const char *which, jv v) {
    vector<char> m;
    const char *a = "Invalid ";
    append_range(m, a, strlen(a));
    append_range(m, which, strlen(which));
    append_range(m, " : ", 3);
    uint32_t s = jv_is_sym(v) ? 0 : js_to_string(vm, v);
    if (vm->throwing) vm->throwing = 0;
    if (s) js_str_to_utf8(vm, s, &m);
    return js_throw(vm, jv_obj(js_error_new(vm, proto, js_str_utf8(vm, data(m), len(m)))));
}

static int dec_same(const dec *a, const dec *b) {
    if (a->kind != b->kind || a->neg != b->neg) return 0;
    if (a->kind != DK_FINITE) return 1;
    return a->nd == b->nd && a->e == b->e && !memcmp(a->d, b->d, (size_t)a->nd);
}

static jv nf_format_range(js_vm *vm, uint32_t nf, jv start, jv end, int to_parts) {
    if (start == JV_UNDEF) return range_error(vm, vm->type_error_proto, "start", start);
    if (end == JV_UNDEF) return range_error(vm, vm->type_error_proto, "end", end);
    dec x, y;
    if (!to_intl_mv(vm, start, &x)) return JV_HOLE;
    if (!to_intl_mv(vm, end, &y)) return JV_HOLE;
    if (x.kind == DK_NAN) return range_error(vm, vm->range_error_proto, "start", start);
    if (y.kind == DK_NAN) return range_error(vm, vm->range_error_proto, "end", end);
    int equal_before = dec_same(&x, &y);
    nfo o;
    nfo_read(vm, nf, &o);
    vector<uint16_t> txa, txb, ot;
    vector<uint8_t> y1, y2, oy, os;
    nfinfo i1, i2;
    dec xs = x;
    nf_render(&o, &x, -1, &txa, &y1, &i1);
    nf_render(&o, &y, -1, &txb, &y2, &i2);
    /* the modifiers alike: the middle's and inner's text, one outer handler */
    int mid_eq = i1.spacing == i2.spacing && segs_equal(&txa, &y1, &i1, &txb, &y2, &i2, SEG_MID_PRE, SEG_NUM) &&
                 segs_equal(&txa, &y1, &i1, &txb, &y2, &i2, SEG_MID_SUF, SEG_OUTER_SUF);
    int inner_eq = segs_equal(&txa, &y1, &i1, &txb, &y2, &i2, SEG_INNER, SEG_MID_SUF);
    int outer_eq = i1.outer == i2.outer && (i1.outer || segs_equal(&txa, &y1, &i1, &txb, &y2, &i2, SEG_OUTER_PRE, SEG_MID_PRE));
    int equal_after = dec_same(&x, &y) && i1.mult == i2.mult;
    if (mid_eq && inner_eq && outer_eq && (equal_before || equal_after)) {
        /* the approximately form of the first, as given */
        nfo oa = o;
        oa.approx = 1;
        vector<uint16_t> ta;
        vector<uint8_t> ya;
        nfinfo ia;
        nf_render(&oa, &xs, -1, &ta, &ya, &ia);
        nf_assemble(&ta, &ya, &ia, &ot, &oy);
        for (uint32_t k = 0; k < len(ot); k++) push_back(os, 2);
    } else {
        /* the outer collapses when alike, then the middle when alike and of more than one
           code point; the inner never (collapse auto) */
        int c_outer = outer_eq;
        uint32_t mid_cps = cp_count(&txa, i1.cut[SEG_MID_PRE], i1.cut[SEG_NUM]) + cp_count(&txa, i1.cut[SEG_MID_SUF], i1.cut[SEG_OUTER_SUF]);
        int c_mid = c_outer && mid_eq && mid_cps > 1;
        int r_inner = i1.cut[SEG_MID_SUF] > i1.cut[SEG_INNER];
        int r_mid = !c_mid && mid_cps > 0;
        int r_outer = !c_outer && (i1.cut[SEG_MID_PRE] > i1.cut[SEG_OUTER_PRE] || i1.cut[SEG_N] > i1.cut[SEG_OUTER_SUF]);
        /* a collapsed outer modifier of the range's plural form (the second's handler) */
        vector<uint16_t> tq;
        vector<uint8_t> yq;
        nfinfo iq;
        int rplural = plural_range(o.tag, i1.plural, i2.plural);
        dec yq0 = y;
        if (c_outer && i2.outer) nf_render(&o, &yq0, rplural, &tq, &yq, &iq);
        else nf_render(&o, &yq0, -1, &tq, &yq, &iq);
        uint16_t rp[64];
        uint32_t nr = ns_field(&o, PA_RANGE, rp, 64);
        uint32_t p0 = 0, p1 = 0;
        for (uint32_t k = 0; k + 2 < nr; k++) {
            if (rp[k] == '{' && rp[k + 1] == '0' && rp[k + 2] == '}') p0 = k;
            if (rp[k] == '{' && rp[k + 1] == '1' && rp[k + 2] == '}') p1 = k;
        }
        /* the range's prefix, then the collapsed prefixes (the middle's, then the outer's) */
        for (uint32_t k = 0; k < p0; k++) put_lit(&ot, &oy, &os, rp[k], 2);
        if (c_mid) put_src(&ot, &oy, &os, &txa, &y1, i1.cut[SEG_MID_PRE], i1.cut[SEG_NUM], 2);
        if (c_outer) put_src(&ot, &oy, &os, &tq, &yq, iq.cut[SEG_OUTER_PRE], iq.cut[SEG_MID_PRE], 2);
        if (c_mid && (i1.spacing & 1) && i1.cut[SEG_MID_SUF] > i1.cut[SEG_NUM] && is_nd(cp_at_u(&txa, i1.cut[SEG_NUM]))) put_lit(&ot, &oy, &os, 0xA0, 2);
        range_end(&ot, &oy, &os, &txa, &y1, &i1, c_mid, c_outer, 0);
        /* the infix, spaced when a modifier repeats and it has no space of its own */
        uint32_t ia0 = p0 + 3, ia1 = p1;
        int spaced = r_inner || r_mid || r_outer;
        if (spaced && ia1 > ia0 && !pattern_ws(rp[ia0])) put_lit(&ot, &oy, &os, ' ', 2);
        for (uint32_t k = ia0; k < ia1; k++) put_lit(&ot, &oy, &os, rp[k], 2);
        if (spaced && ia1 > ia0 && !pattern_ws(rp[ia1 - 1])) put_lit(&ot, &oy, &os, ' ', 2);
        range_end(&ot, &oy, &os, &txb, &y2, &i2, c_mid, c_outer, 1);
        /* the collapsed suffixes (the middle's, then the outer's), then the range's suffix */
        if (c_mid && (i2.spacing & 2) && i2.cut[SEG_MID_SUF] > i2.cut[SEG_NUM] && is_nd(cp_before_u(&txb, i2.cut[SEG_MID_SUF]))) put_lit(&ot, &oy, &os, 0xA0, 2);
        if (c_mid) put_src(&ot, &oy, &os, &txb, &y2, i2.cut[SEG_MID_SUF], i2.cut[SEG_OUTER_SUF], 2);
        if (c_outer) put_src(&ot, &oy, &os, &tq, &yq, iq.cut[SEG_OUTER_SUF], iq.cut[SEG_N], 2);
        for (uint32_t k = p1 + 3; k < nr; k++) put_lit(&ot, &oy, &os, rp[k], 2);
    }
    if (!to_parts) return jv_str(js_str_new(vm, data(ot), len(ot)));
    vector<uint32_t> pt;
    parts_of(&ot, &oy, &os, &pt);
    return parts_array(vm, &ot, &pt, pt_names, &os);
}

/* ---- Intl.NumberFormat: its options read (ECMA-402 15.1.2, V8's order) ---- */

/* GetNumberOption / DefaultNumberOption: an integer in [lo, hi], else a RangeError; def
   for undefined (-1 thrown, -2 undefined when def is -2) */
static int number_option(js_vm *vm, jv v, int lo, int hi, int def, const char *name) {
    if (v == JV_UNDEF) return def;
    double d = js_to_number(vm, v);
    if (vm->throwing) return -1;
    if (d != d || d < lo || d > hi) {
        char m[96];
        m[0] = 0;
        cat(m, sizeof m, name);
        cat(m, sizeof m, " value is out of range.");
        js_throw_msg(vm, vm->range_error_proto, m);
        return -1;
    }
    return (int)floor(d);
}

/* a numbering system ICU formats with: a numeric one of the table's; its index, -1 */
static int numbering_index(const char *nu) { return db_index(IDB_NUMBERING, IDB_NUMBERING_N, nu); }

/* IsWellFormedUnitIdentifier: a sanctioned simple unit, or one per another */
static int unit_ok(const char *u) {
    static const char *const sanctioned[] = { "acre", "bit", "byte", "celsius", "centimeter", "day", "degree", "fahrenheit",
        "fluid-ounce", "foot", "gallon", "gigabit", "gigabyte", "gram", "hectare", "hour", "inch", "kilobit", "kilobyte",
        "kilogram", "kilometer", "liter", "megabit", "megabyte", "meter", "microsecond", "mile", "mile-scandinavian",
        "milliliter", "millimeter", "millisecond", "minute", "month", "nanosecond", "ounce", "percent", "petabyte", "pound",
        "second", "stone", "terabit", "terabyte", "week", "yard", "year", 0 };
    for (int i = 0; sanctioned[i]; i++) if (!strcmp(sanctioned[i], u)) return 1;
    const char *per = strstr(u, "-per-");
    if (!per) return 0;
    char a[64];
    copyn(a, sizeof a, u, (size_t)(per - u));
    int ok_a = 0, ok_b = 0;
    for (int i = 0; sanctioned[i]; i++) {
        if (!strcmp(sanctioned[i], a)) ok_a = 1;
        if (!strcmp(sanctioned[i], per + 5)) ok_b = 1;
    }
    return ok_a && ok_b;
}

/* SetNumberFormatDigitOptions (V8's order): the digit options read and checked into d; 0
   thrown */
typedef struct dopts { int mnid, mnfd, mxfd, mnsd, mxsd, rtype, inc, rmode, rprio, tzd; } dopts;
static int js_throw_msg_0(js_vm *vm, uint32_t proto, const char *m) {
    js_throw_msg(vm, proto, m);
    return 0;
}
static int digit_options(js_vm *vm, jv opts, const char *svc, int mnfd_def, int mxfd_def, int compact, dopts *d) {
    int notation = NN_STANDARD;
    if (compact) notation = NN_COMPACT;
    int mnid = number_option(vm, js_get(vm, opts, js_atom_ascii(vm, "minimumIntegerDigits")), 1, 21, 1, "minimumIntegerDigits");
    if (mnid < 0 || vm->throwing) return 0;
    jv vmnfd = js_get(vm, opts, js_atom_ascii(vm, "minimumFractionDigits"));
    if (vm->throwing) return 0;
    jv vmxfd = js_get(vm, opts, js_atom_ascii(vm, "maximumFractionDigits"));
    if (vm->throwing) return 0;
    jv vmnsd = js_get(vm, opts, js_atom_ascii(vm, "minimumSignificantDigits"));
    if (vm->throwing) return 0;
    jv vmxsd = js_get(vm, opts, js_atom_ascii(vm, "maximumSignificantDigits"));
    if (vm->throwing) return 0;
    int inc = number_option(vm, js_get(vm, opts, js_atom_ascii(vm, "roundingIncrement")), 1, 5000, 1, "roundingIncrement");
    if (inc < 0 || vm->throwing) return 0;
    static const int incs[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000 };
    int inc_ok = 0;
    for (int k = 0; k < 15; k++) if (incs[k] == inc) inc_ok = 1;
    if (!inc_ok) return js_throw_msg_0(vm, vm->range_error_proto, "roundingIncrement value is out of range.");
    int rmode = get_option_str(vm, opts, "roundingMode", rm_names, svc, 0);
    if (rmode == -2) return 0;
    if (rmode < 0) rmode = RM_HALFEXPAND;
    int rprio = get_option_str(vm, opts, "roundingPriority", rp_names, svc, 0);
    if (rprio == -2) return 0;
    if (rprio < 0) rprio = RP_AUTO;
    int tzd = get_option_str(vm, opts, "trailingZeroDisplay", tz_names, svc, 0);
    if (tzd == -2) return 0;
    if (tzd < 0) tzd = 0;
    int has_sd = vmnsd != JV_UNDEF || vmxsd != JV_UNDEF, has_fd = vmnfd != JV_UNDEF || vmxfd != JV_UNDEF;
    int need_sd = 1, need_fd = 1;
    if (rprio == RP_AUTO) {
        need_sd = has_sd;
        if (need_sd || (!has_fd && notation == NN_COMPACT)) need_fd = 0;
    }
    int mnsd = 1, mxsd = 21, mnfd = mnfd_def, mxfd = mxfd_def;
    if (need_sd && has_sd) {
        mnsd = number_option(vm, vmnsd, 1, 21, 1, "minimumSignificantDigits");
        if (mnsd < 0 || vm->throwing) return 0;
        mxsd = number_option(vm, vmxsd, mnsd, 21, 21, "maximumSignificantDigits");
        if (mxsd < 0 || vm->throwing) return 0;
    }
    if (need_fd && has_fd) {
        int a = number_option(vm, vmnfd, 0, 100, -2, "minimumFractionDigits");
        if (a == -1 || vm->throwing) return 0;
        int b = number_option(vm, vmxfd, 0, 100, -2, "maximumFractionDigits");
        if (b == -1 || vm->throwing) return 0;
        if (a == -2) a = mnfd_def < b ? mnfd_def : b;
        else if (b == -2) b = mxfd_def > a ? mxfd_def : a;
        else if (a > b) return js_throw_msg_0(vm, vm->range_error_proto, "maximumFractionDigits value is out of range.");
        mnfd = a;
        mxfd = b;
    }
    int rtype, computed_prio;
    if (!need_sd && !need_fd) {
        mnfd = 0; mxfd = 0; mnsd = 1; mxsd = 2;
        rtype = RT_MORE;
        computed_prio = RP_MORE;
    } else if (rprio == RP_AUTO) {
        rtype = need_sd ? RT_SIGNIFICANT : RT_FRACTION;
        computed_prio = RP_AUTO;
    } else {
        rtype = rprio == RP_MORE ? RT_MORE : RT_LESS;
        computed_prio = rprio;
    }
    if (inc != 1) {
        if (rtype != RT_FRACTION) return js_throw_msg_0(vm, vm->type_error_proto, "RoundingType is not fractionDigits");
        if (mxfd != mnfd) return js_throw_msg_0(vm, vm->range_error_proto, "maximumFractionDigits value is out of range.");
    }
    d->mnid = mnid;
    d->mnfd = mnfd;
    d->mxfd = mxfd;
    d->mnsd = mnsd;
    d->mxsd = mxsd;
    d->rtype = rtype;
    d->inc = inc;
    d->rmode = rmode;
    d->rprio = computed_prio;
    d->tzd = tzd;
    return 1;
}

static jv nf_new(js_vm *vm, jv locales, jv options, jv newt, const char *method) {
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    jv opts = options_object(vm, options, method);
    if (opts == JV_HOLE) return JV_HOLE;
    const char *svc = "Intl.NumberFormat";
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    uint32_t nus = 0;
    char nu_opt[16];
    nu_opt[0] = 0;
    int r = get_option_str(vm, opts, "numberingSystem", 0, svc, &nus);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[80];
        int n = str_ascii(vm, nus, b, sizeof b);
        if (n < 0 || !is_type(b)) {
            vector<char> m;
            const char *a = "Invalid numberingSystem : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, nus, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        if (n < 16) copyz(nu_opt, sizeof nu_opt, b);
    }
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    char nu[16];
    int from_kw = 0;
    int kw = find_kw(&t, "nu");
    if (kw >= 0 && t.val[kw][0] && strlen(t.val[kw]) < 16 && numbering_index(t.val[kw]) >= 0) { copyz(nu, sizeof nu, t.val[kw]); from_kw = 1; }
    if (nu_opt[0] && numbering_index(nu_opt) >= 0) {
        if (!from_kw || strcmp(nu, nu_opt)) { copyz(nu, sizeof nu, nu_opt); from_kw = 0; }
    } else if (!from_kw) default_numbering(found, nu, sizeof nu);
    char loc[TAG_MAX + 1];
    copyz(loc, sizeof loc, found);
    if (from_kw) { cat(loc, sizeof loc, "-u-nu-"); cat(loc, sizeof loc, nu); }
    /* the style and its unit */
    int style = get_option_str(vm, opts, "style", st_names, svc, 0);
    if (style == -2) return JV_HOLE;
    if (style < 0) style = ST_DECIMAL;
    uint32_t curs = 0;
    char cur[4];
    cur[0] = 0;
    r = get_option_str(vm, opts, "currency", 0, svc, &curs);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[8];
        int n = str_ascii(vm, curs, b, sizeof b);
        if (n != 3 || !all_of(b, 3, 0)) {
            vector<char> m;
            const char *a = "Invalid currency code : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, curs, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        for (int k = 0; k < 3; k++) cur[k] = (char)upper(b[k]);
        cur[3] = 0;
    }
    int cd = get_option_str(vm, opts, "currencyDisplay", cd_names, svc, 0);
    if (cd == -2) return JV_HOLE;
    if (cd < 0) cd = CD_SYMBOL;
    int cs = get_option_str(vm, opts, "currencySign", cs_names, svc, 0);
    if (cs == -2) return JV_HOLE;
    if (cs < 0) cs = 0;
    uint32_t units = 0;
    char unit[64];
    unit[0] = 0;
    r = get_option_str(vm, opts, "unit", 0, svc, &units);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        int n = str_ascii(vm, units, unit, sizeof unit);
        if (n < 0 || !unit_ok(unit)) {
            vector<char> m;
            const char *a = "Invalid unit argument for Intl.NumberFormat() '";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, units, &m);
            push_back(m, '\'');
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
    }
    int ud = get_option_str(vm, opts, "unitDisplay", ud_names, svc, 0);
    if (ud == -2) return JV_HOLE;
    if (ud < 0) ud = UD_SHORT;
    if (style == ST_CURRENCY && !cur[0]) return js_throw_msg(vm, vm->type_error_proto, "Currency code is required with currency style.");
    if (style == ST_UNIT && !unit[0]) return js_throw_msg(vm, vm->type_error_proto, "Invalid unit argument for Intl.NumberFormat() ''");
    int notation = get_option_str(vm, opts, "notation", nn_names, svc, 0);
    if (notation == -2) return JV_HOLE;
    if (notation < 0) notation = NN_STANDARD;
    /* the digits (SetNumberFormatDigitOptions) */
    int mnfd_def = 0, mxfd_def = 3;
    if (style == ST_CURRENCY) {
        uint32_t d = db_find_z(IDB_CUR_DIGITS, IDB_CUR_DIGITS_N, cur);
        mnfd_def = mxfd_def = d ? (int)(db8(d) - '0') : 2;
    } else if (style == ST_PERCENT) mxfd_def = 0;
    dopts dg;
    if (!digit_options(vm, opts, svc, mnfd_def, mxfd_def, notation == NN_COMPACT, &dg)) return JV_HOLE;
    int mnid = dg.mnid, mnfd = dg.mnfd, mxfd = dg.mxfd, mnsd = dg.mnsd, mxsd = dg.mxsd, rtype = dg.rtype, inc = dg.inc;
    int rmode = dg.rmode, computed_prio = dg.rprio, tzd = dg.tzd;
    int compact = get_option_str(vm, opts, "compactDisplay", cpd_names, svc, 0);
    if (compact == -2) return JV_HOLE;
    if (compact < 0) compact = 0;
    /* useGrouping: a boolean or a string (GetBooleanOrStringNumberFormatOption) */
    int grouping = notation == NN_COMPACT ? UG_MIN2 : UG_AUTO;
    {
        jv g = js_get(vm, opts, js_atom_ascii(vm, "useGrouping"));
        if (vm->throwing) return JV_HOLE;
        if (g == JV_TRUE) grouping = UG_ALWAYS;
        else if (g != JV_UNDEF && !js_truthy(vm, g)) grouping = UG_OFF;
        else if (g != JV_UNDEF) {
            uint32_t gs = js_to_string(vm, g);
            if (!gs) return JV_HOLE;
            char b[16];
            int n = str_ascii(vm, gs, b, sizeof b);
            if (n >= 0 && (!strcmp(b, "true") || !strcmp(b, "false"))) { /* the default */ }
            else if (n >= 0 && !strcmp(b, "min2")) grouping = UG_MIN2;
            else if (n >= 0 && !strcmp(b, "auto")) grouping = UG_AUTO;
            else if (n >= 0 && !strcmp(b, "always")) grouping = UG_ALWAYS;
            else {
                vector<char> m;
                append_range(m, "Value ", 6);
                js_str_to_utf8(vm, gs, &m);
                const char *a = " out of range for Intl.NumberFormat options property useGrouping";
                append_range(m, a, strlen(a));
                return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
            }
        }
    }
    int sign = get_option_str(vm, opts, "signDisplay", sd_names, svc, 0);
    if (sign == -2) return JV_HOLE;
    if (sign < 0) sign = SD_AUTO;
    /* the object */
    jv p = JV_UNDEF;
    if (jv_is_obj(newt)) {
        p = js_get(vm, newt, A_PROTOTYPE);
        if (vm->throwing) return JV_HOLE;
    }
    uint32_t o = intl_new(vm, JIK_NUMBERFORMAT, jv_is_obj(p) ? jv_idx(p) : vm->intl_protos[JIK_NUMBERFORMAT], NFS_N);
    int li = locale_index(found);
    if (li < 0) li = locale_index("en");
    slot_set(vm, o, NFS_LOCALE, jv_str(js_str_ascii(vm, loc)));
    slot_set(vm, o, NFS_NU, jv_str(js_str_ascii(vm, nu)));
    slot_set(vm, o, NFS_STYLE, jv_num(style));
    if (cur[0]) slot_set(vm, o, NFS_CURRENCY, jv_str(js_str_ascii(vm, cur)));
    slot_set(vm, o, NFS_CURDISPLAY, jv_num(cd));
    slot_set(vm, o, NFS_CURSIGN, jv_num(cs));
    if (unit[0]) slot_set(vm, o, NFS_UNIT, jv_str(js_str_ascii(vm, unit)));
    slot_set(vm, o, NFS_UNITDISPLAY, jv_num(ud));
    slot_set(vm, o, NFS_MNID, jv_num(mnid));
    slot_set(vm, o, NFS_MNFD, jv_num(mnfd));
    slot_set(vm, o, NFS_MXFD, jv_num(mxfd));
    slot_set(vm, o, NFS_MNSD, jv_num(mnsd));
    slot_set(vm, o, NFS_MXSD, jv_num(mxsd));
    slot_set(vm, o, NFS_RTYPE, jv_num(rtype));
    slot_set(vm, o, NFS_INCREMENT, jv_num(inc));
    slot_set(vm, o, NFS_RMODE, jv_num(rmode));
    slot_set(vm, o, NFS_RPRIORITY, jv_num(computed_prio));
    slot_set(vm, o, NFS_TRAILING, jv_num(tzd));
    slot_set(vm, o, NFS_NOTATION, jv_num(notation));
    slot_set(vm, o, NFS_COMPACT, jv_num(compact));
    slot_set(vm, o, NFS_GROUPING, jv_num(grouping));
    slot_set(vm, o, NFS_SIGN, jv_num(sign));
    slot_set(vm, o, NFS_DATALOC, jv_num(li));
    slot_set(vm, o, NFS_NSI, jv_num(numbering_index(nu)));
    return jv_obj(o);
}

/* resolvedOptions, in V8's order */
static jv nf_resolved(js_vm *vm, uint32_t nf) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    nfo o;
    nfo_read(vm, nf, &o);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, nf, NFS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "numberingSystem"), slot_get(vm, nf, NFS_NU), 0);
    js_define(vm, r, js_atom_ascii(vm, "style"), jv_str(js_str_ascii(vm, st_names[o.style])), 0);
    if (o.style == ST_CURRENCY) {
        js_define(vm, r, js_atom_ascii(vm, "currency"), slot_get(vm, nf, NFS_CURRENCY), 0);
        js_define(vm, r, js_atom_ascii(vm, "currencyDisplay"), jv_str(js_str_ascii(vm, cd_names[o.cur_display])), 0);
        js_define(vm, r, js_atom_ascii(vm, "currencySign"), jv_str(js_str_ascii(vm, cs_names[o.cur_sign])), 0);
    }
    if (o.style == ST_UNIT) {
        js_define(vm, r, js_atom_ascii(vm, "unit"), slot_get(vm, nf, NFS_UNIT), 0);
        js_define(vm, r, js_atom_ascii(vm, "unitDisplay"), jv_str(js_str_ascii(vm, ud_names[o.unit_display])), 0);
    }
    js_define(vm, r, js_atom_ascii(vm, "minimumIntegerDigits"), jv_num(o.mnid), 0);
    if (o.rtype != RT_SIGNIFICANT) {
        js_define(vm, r, js_atom_ascii(vm, "minimumFractionDigits"), jv_num(o.mnfd), 0);
        js_define(vm, r, js_atom_ascii(vm, "maximumFractionDigits"), jv_num(o.mxfd), 0);
    }
    if (o.rtype != RT_FRACTION) {
        js_define(vm, r, js_atom_ascii(vm, "minimumSignificantDigits"), jv_num(o.mnsd), 0);
        js_define(vm, r, js_atom_ascii(vm, "maximumSignificantDigits"), jv_num(o.mxsd), 0);
    }
    js_define(vm, r, js_atom_ascii(vm, "useGrouping"), o.grouping == UG_OFF ? JV_FALSE : jv_str(js_str_ascii(vm, ug_names[o.grouping])), 0);
    js_define(vm, r, js_atom_ascii(vm, "notation"), jv_str(js_str_ascii(vm, nn_names[o.notation])), 0);
    if (o.notation == NN_COMPACT) js_define(vm, r, js_atom_ascii(vm, "compactDisplay"), jv_str(js_str_ascii(vm, cpd_names[o.compact])), 0);
    js_define(vm, r, js_atom_ascii(vm, "signDisplay"), jv_str(js_str_ascii(vm, sd_names[o.sign])), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingIncrement"), jv_num(o.increment), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingMode"), jv_str(js_str_ascii(vm, rm_names[o.rmode])), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingPriority"), jv_str(js_str_ascii(vm, rp_names[slot_int(vm, nf, NFS_RPRIORITY)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "trailingZeroDisplay"), jv_str(js_str_ascii(vm, tz_names[o.trailing])), 0);
    return jv_obj(r);
}

/* the formatters toLocaleString keeps for its defaults (no locales, no options), by use */
enum { IC_NUMBER, IC_DATE, IC_COLLATOR = IC_DATE + 3, IC_N };
static uint32_t intl_cache_get(js_vm *vm, uint32_t slot) {
    if (!vm->intl_cache) return 0;
    jv v = js_array_get(vm, vm->intl_cache, slot);
    return jv_is_obj(v) ? jv_idx(v) : 0;
}
static void intl_cache_put(js_vm *vm, uint32_t slot, uint32_t o) {
    if (!vm->intl_cache) {
        vm->intl_cache = js_array_new(vm, IC_N);
        for (uint32_t i = 0; i < IC_N; i++) js_array_set(vm, vm->intl_cache, i, JV_UNDEF);
    }
    js_array_set(vm, vm->intl_cache, slot, jv_obj(o));
}

/* Number.prototype.toLocaleString and BigInt's: a NumberFormat of the arguments formats this */
static jv number_to_locale(js_vm *vm, jv x, jv locales, jv options, const char *method) {
    uint32_t nf = 0;
    if (locales == JV_UNDEF && options == JV_UNDEF) nf = intl_cache_get(vm, IC_NUMBER);
    if (!nf) {
        jv f = nf_new(vm, locales, options, JV_UNDEF, method);
        if (f == JV_HOLE) return JV_HOLE;
        nf = jv_idx(f);
        if (locales == JV_UNDEF && options == JV_UNDEF) intl_cache_put(vm, IC_NUMBER, nf);
    }
    return nf_format_value(vm, nf, x, 0);
}

static jv this_nf(js_vm *vm, jv thisv, const char *method) {
    if (!is_intl(vm, thisv, JIK_NUMBERFORMAT)) return incompatible(vm, method, thisv);
    return thisv;
}
/* V8's UnwrapNumberFormat (the format getter's, resolvedOptions'): an object that is not
   one is reported as its legacy fallback, undefined */
static jv unwrap_nf(js_vm *vm, jv thisv, const char *method) {
    if (is_intl(vm, thisv, JIK_NUMBERFORMAT)) return thisv;
    if (jv_is_obj(thisv)) return incompatible(vm, "UnwrapNumberFormat", JV_UNDEF);
    return incompatible(vm, method, thisv);
}

/* ==== Intl.DateTimeFormat (ECMA-402 11) ==== */

/* ---- the calendar: the proleptic Gregorian (V8 moves ICU's Julian change to the start of
   ECMAScript's time) ---- */

static int64_t div_floor(int64_t a, int64_t b) {
    int64_t q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0)) q--;
    return q;
}

/* a time's fields in a zone (its offset in seconds east): the year (0 is 1 BC), the month
   (1 to 12), the day, the weekday (0 Sunday), the hour, minute, second, millisecond; the
   time (ms from 1970), the zone's standard offset and DST amount at it (seconds) */
typedef struct dfields { int64_t year; int month, day, wday, hour, minute, second, ms; double t; int raw, dst; } dfields;
static void date_fields(double t, int offset, dfields *f) {
    f->t = t;
    f->raw = offset;
    f->dst = 0;
    int64_t ms = (int64_t)t + (int64_t)offset * 1000;
    int64_t day = div_floor(ms, 86400000);
    int64_t rem = ms - day * 86400000;
    int64_t z = day + 719468;
    int64_t era = div_floor(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    f->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    if (mp < 10) f->month = (int)(mp + 3);
    else f->month = (int)(mp - 9);
    f->year = yoe + era * 400;
    if (f->month <= 2) f->year++;
    int64_t w = (day + 4) % 7;
    if (w < 0) w += 7;
    f->wday = (int)w;
    f->hour = (int)(rem / 3600000);
    f->minute = (int)(rem / 60000 % 60);
    f->second = (int)(rem / 1000 % 60);
    f->ms = (int)(rem % 1000);
}

/* ---- ICU's DateTimePatternGenerator: the best pattern of a skeleton ---- */

/* its fields, in its order (UDATPG_*_FIELD) */
enum { DF_ERA, DF_YEAR, DF_QUARTER, DF_MONTH, DF_WEEK_OF_YEAR, DF_WEEK_OF_MONTH, DF_WEEKDAY, DF_DAY_OF_YEAR,
       DF_DAY_OF_WEEK_IN_MONTH, DF_DAY, DF_DAYPERIOD, DF_HOUR, DF_MINUTE, DF_SECOND, DF_FRACTIONAL, DF_ZONE, DF_N };
#define DT_NUMERIC 0x100
#define DT_NARROW (-0x101)
#define DT_SHORTER (-0x102)
#define DT_SHORT (-0x103)
#define DT_LONG (-0x104)
#define DT_DELTA 0x10
#define DT_EXTRA_FIELD 0x10000
#define DT_MISSING_FIELD 0x1000
/* dtTypes: a pattern letter's field, its type and the least length of the row */
typedef struct dttype { char ch; uint8_t field; int16_t type; uint8_t min_len; } dttype;
static const dttype dt_types[] = {
    { 'G', DF_ERA, DT_SHORT, 1 }, { 'G', DF_ERA, DT_LONG, 4 }, { 'G', DF_ERA, DT_NARROW, 5 },
    { 'y', DF_YEAR, DT_NUMERIC, 1 }, { 'Y', DF_YEAR, DT_NUMERIC + DT_DELTA, 1 }, { 'u', DF_YEAR, DT_NUMERIC + 2 * DT_DELTA, 1 },
    { 'r', DF_YEAR, DT_NUMERIC + 3 * DT_DELTA, 1 }, { 'U', DF_YEAR, DT_SHORT, 1 }, { 'U', DF_YEAR, DT_LONG, 4 },
    { 'U', DF_YEAR, DT_NARROW, 5 },
    { 'Q', DF_QUARTER, DT_NUMERIC, 1 }, { 'Q', DF_QUARTER, DT_SHORT, 3 }, { 'Q', DF_QUARTER, DT_LONG, 4 },
    { 'Q', DF_QUARTER, DT_NARROW, 5 }, { 'q', DF_QUARTER, DT_NUMERIC + DT_DELTA, 1 }, { 'q', DF_QUARTER, DT_SHORT - DT_DELTA, 3 },
    { 'q', DF_QUARTER, DT_LONG - DT_DELTA, 4 }, { 'q', DF_QUARTER, DT_NARROW - DT_DELTA, 5 },
    { 'M', DF_MONTH, DT_NUMERIC, 1 }, { 'M', DF_MONTH, DT_SHORT, 3 }, { 'M', DF_MONTH, DT_LONG, 4 }, { 'M', DF_MONTH, DT_NARROW, 5 },
    { 'L', DF_MONTH, DT_NUMERIC + DT_DELTA, 1 }, { 'L', DF_MONTH, DT_SHORT - DT_DELTA, 3 }, { 'L', DF_MONTH, DT_LONG - DT_DELTA, 4 },
    { 'L', DF_MONTH, DT_NARROW - DT_DELTA, 5 }, { 'l', DF_MONTH, DT_NUMERIC + DT_DELTA, 1 },
    { 'w', DF_WEEK_OF_YEAR, DT_NUMERIC, 1 }, { 'W', DF_WEEK_OF_MONTH, DT_NUMERIC, 1 },
    { 'E', DF_WEEKDAY, DT_SHORT, 1 }, { 'E', DF_WEEKDAY, DT_LONG, 4 }, { 'E', DF_WEEKDAY, DT_NARROW, 5 },
    { 'E', DF_WEEKDAY, DT_SHORTER, 6 }, { 'c', DF_WEEKDAY, DT_NUMERIC + 2 * DT_DELTA, 1 }, { 'c', DF_WEEKDAY, DT_SHORT - 2 * DT_DELTA, 3 },
    { 'c', DF_WEEKDAY, DT_LONG - 2 * DT_DELTA, 4 }, { 'c', DF_WEEKDAY, DT_NARROW - 2 * DT_DELTA, 5 },
    { 'c', DF_WEEKDAY, DT_SHORTER - 2 * DT_DELTA, 6 }, { 'e', DF_WEEKDAY, DT_NUMERIC + DT_DELTA, 1 },
    { 'e', DF_WEEKDAY, DT_SHORT - DT_DELTA, 3 }, { 'e', DF_WEEKDAY, DT_LONG - DT_DELTA, 4 }, { 'e', DF_WEEKDAY, DT_NARROW - DT_DELTA, 5 },
    { 'e', DF_WEEKDAY, DT_SHORTER - DT_DELTA, 6 },
    { 'd', DF_DAY, DT_NUMERIC, 1 }, { 'g', DF_DAY, DT_NUMERIC + DT_DELTA, 1 },
    { 'D', DF_DAY_OF_YEAR, DT_NUMERIC, 1 }, { 'F', DF_DAY_OF_WEEK_IN_MONTH, DT_NUMERIC, 1 },
    { 'a', DF_DAYPERIOD, DT_SHORT, 1 }, { 'a', DF_DAYPERIOD, DT_LONG, 4 }, { 'a', DF_DAYPERIOD, DT_NARROW, 5 },
    { 'b', DF_DAYPERIOD, DT_SHORT - DT_DELTA, 1 }, { 'b', DF_DAYPERIOD, DT_LONG - DT_DELTA, 4 }, { 'b', DF_DAYPERIOD, DT_NARROW - DT_DELTA, 5 },
    { 'B', DF_DAYPERIOD, DT_SHORT - 3 * DT_DELTA, 1 }, { 'B', DF_DAYPERIOD, DT_LONG - 3 * DT_DELTA, 4 },
    { 'B', DF_DAYPERIOD, DT_NARROW - 3 * DT_DELTA, 5 },
    { 'H', DF_HOUR, DT_NUMERIC + 10 * DT_DELTA, 1 }, { 'k', DF_HOUR, DT_NUMERIC + 11 * DT_DELTA, 1 },
    { 'h', DF_HOUR, DT_NUMERIC, 1 }, { 'K', DF_HOUR, DT_NUMERIC + DT_DELTA, 1 },
    { 'J', DF_HOUR, DT_NUMERIC + 5 * DT_DELTA, 1 }, { 'j', DF_HOUR, DT_NUMERIC + 6 * DT_DELTA, 1 },
    { 'C', DF_HOUR, DT_NUMERIC + 7 * DT_DELTA, 1 },
    { 'm', DF_MINUTE, DT_NUMERIC, 1 }, { 's', DF_SECOND, DT_NUMERIC, 1 }, { 'A', DF_SECOND, DT_NUMERIC + DT_DELTA, 1 },
    { 'S', DF_FRACTIONAL, DT_NUMERIC, 1 },
    { 'v', DF_ZONE, DT_SHORT - 2 * DT_DELTA, 1 }, { 'v', DF_ZONE, DT_LONG - 2 * DT_DELTA, 4 },
    { 'z', DF_ZONE, DT_SHORT, 1 }, { 'z', DF_ZONE, DT_LONG, 4 },
    { 'Z', DF_ZONE, DT_NARROW - DT_DELTA, 1 }, { 'Z', DF_ZONE, DT_LONG - DT_DELTA, 4 }, { 'Z', DF_ZONE, DT_SHORT - DT_DELTA, 5 },
    { 'O', DF_ZONE, DT_SHORT - DT_DELTA, 1 }, { 'O', DF_ZONE, DT_LONG - DT_DELTA, 4 },
    { 'V', DF_ZONE, DT_SHORT - DT_DELTA, 1 }, { 'V', DF_ZONE, DT_LONG - DT_DELTA, 2 }, { 'V', DF_ZONE, DT_LONG - 1 - DT_DELTA, 3 },
    { 'V', DF_ZONE, DT_LONG - 2 - DT_DELTA, 4 },
    { 'X', DF_ZONE, DT_NARROW - DT_DELTA, 1 }, { 'X', DF_ZONE, DT_SHORT - DT_DELTA, 2 }, { 'X', DF_ZONE, DT_LONG - DT_DELTA, 4 },
    { 'x', DF_ZONE, DT_NARROW - DT_DELTA, 1 }, { 'x', DF_ZONE, DT_SHORT - DT_DELTA, 2 }, { 'x', DF_ZONE, DT_LONG - DT_DELTA, 4 },
    { 0, DF_N, 0, 0 } };

static int is_letter16(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

/* FormatParser::getCanonicalIndex: the row of a run of one letter, -1 for none */
static int canonical_row(uint32_t ch, int n) {
    for (int i = 0; dt_types[i].ch; i++) {
        if ((uint32_t)(uint8_t)dt_types[i].ch != ch) continue;
        if (dt_types[i + 1].ch != dt_types[i].ch) return i;
        if (dt_types[i + 1].min_len <= n) continue;
        return i;
    }
    return -1;
}

/* a skeleton (PtnSkeleton): each field's type, its letter and length as given, its base
   (the row's letter at the row's least length); whether the default 'a' was added */
typedef struct dskel { int type[DF_N]; uint8_t ch[DF_N], ln[DF_N], bch[DF_N], bln[DF_N]; int added_a; } dskel;

/* a pattern's tokens (FormatParser): runs of one ASCII letter, or one other unit */
#define DT_TOKENS 160
typedef struct dtoks { uint16_t off[DT_TOKENS], len[DT_TOKENS]; uint32_t n; } dtoks;
static void pattern_tokens(const uint16_t *p, uint32_t n, dtoks *t) {
    t->n = 0;
    uint32_t i = 0;
    while (i < n && t->n < DT_TOKENS) {
        uint32_t j = i + 1;
        if (is_letter16(p[i])) while (j < n && p[j] == p[i]) j++;
        t->off[t->n] = (uint16_t)i;
        t->len[t->n] = (uint16_t)(j - i);
        t->n++;
        i = j;
    }
}
/* the token after a quoted literal's opening quote at i: the index of its closing quote
   (FormatParser::getQuoteLiteral; '' within it a quote) */
static uint32_t quote_end(const uint16_t *p, const dtoks *t, uint32_t i) {
    i++;
    while (i < t->n) {
        if (p[t->off[i]] == '\'') {
            if (i + 1 < t->n && p[t->off[i + 1]] == '\'') { i += 2; continue; }
            break;
        }
        i++;
    }
    return i;
}

static void dskel_field(dskel *s, int f, uint32_t c, int n, int row) {
    s->ch[f] = (uint8_t)c;
    s->ln[f] = (uint8_t)n;
    s->bch[f] = (uint8_t)dt_types[row].ch;
    s->bln[f] = dt_types[row].min_len;
    int sub = dt_types[row].type;
    if (sub > 0) sub += n;
    s->type[f] = sub;
}

/* DateTimeMatcher::set */
static void dskel_set(const uint16_t *p, uint32_t n, dskel *s) {
    memset(s, 0, sizeof *s);
    dtoks t;
    pattern_tokens(p, n, &t);
    for (uint32_t i = 0; i < t.n; i++) {
        uint32_t c = p[t.off[i]];
        if (c == '\'') { i = quote_end(p, &t, i); continue; }
        if (!is_letter16(c)) continue;
        int row = canonical_row(c, t.len[i]);
        if (row >= 0) dskel_field(s, dt_types[row].field, c, t.len[i], row);
    }
    /* minutes and fractions without seconds: the seconds forced */
    if (s->ln[DF_MINUTE] && s->ln[DF_FRACTIONAL] && !s->ln[DF_SECOND]) dskel_field(s, DF_SECOND, 's', 1, canonical_row('s', 1));
    /* a 12-hour cycle gets the default day period, a 24-hour one loses its own */
    if (s->ln[DF_HOUR]) {
        if (s->ch[DF_HOUR] == 'h' || s->ch[DF_HOUR] == 'K') {
            if (!s->ln[DF_DAYPERIOD]) {
                dskel_field(s, DF_DAYPERIOD, 'a', 1, canonical_row('a', 1));
                s->added_a = 1;
            }
        } else {
            s->ch[DF_DAYPERIOD] = 0;
            s->ln[DF_DAYPERIOD] = 0;
            s->bch[DF_DAYPERIOD] = 0;
            s->bln[DF_DAYPERIOD] = 0;
            s->type[DF_DAYPERIOD] = 0;
        }
    }
}

static int dskel_distance(const dskel *me, const dskel *other, int include, int *missing, int *extra) {
    int r = 0;
    *missing = 0;
    *extra = 0;
    for (int i = 0; i < DF_N; i++) {
        int my = 0, ot = other->type[i];
        if (include & (1 << i)) my = me->type[i];
        if (my == ot) continue;
        if (my == 0) { r += DT_EXTRA_FIELD; *extra |= 1 << i; }
        else if (ot == 0) { r += DT_MISSING_FIELD; *missing |= 1 << i; }
        else if (my > ot) r += my - ot;
        else r += ot - my;
    }
    return r;
}
static int dskel_original_eq(const dskel *a, const dskel *b) {
    for (int i = 0; i < DF_N; i++) if (a->ch[i] != b->ch[i] || a->ln[i] != b->ln[i]) return 0;
    return 1;
}
static int dskel_base_eq(const dskel *a, const dskel *b) {
    for (int i = 0; i < DF_N; i++) if (a->bch[i] != b->bch[i] || a->bln[i] != b->bln[i]) return 0;
    return 1;
}
static int dskel_types_eq(const dskel *a, const dskel *b) {
    for (int i = 0; i < DF_N; i++) if (a->type[i] != b->type[i]) return 0;
    return 1;
}
static int dskel_mask(const dskel *s) {
    int m = 0;
    for (int i = 0; i < DF_N; i++) if (s->type[i]) m |= 1 << i;
    return m;
}
/* the base's first letter: the bucket of ICU's PatternMap (A-Z 0-25, a-z 26-51) */
static int dskel_bucket(const dskel *s) {
    for (int i = 0; i < DF_N; i++)
        if (s->bln[i]) {
            uint32_t c = s->bch[i];
            if (c >= 'a') return 26 + (int)(c - 'a');
            return (int)(c - 'A');
        }
    return -1;
}
/* PtnSkeleton::getSkeleton: the fields as given, in field order, without an added 'a' */
static void dskel_text(const dskel *s, vector<uint16_t> *out) {
    for (int i = 0; i < DF_N; i++) {
        if (i == DF_DAYPERIOD && s->added_a) continue;
        for (int k = 0; k < s->ln[i]; k++) push_back(*out, (uint16_t)s->ch[i]);
    }
}

/* the map: entries (a skeleton, its pattern's place in the text, whether its skeleton was
   given) in insertion order, searched bucket by bucket as ICU iterates */
typedef struct dpent { dskel sk; uint32_t off, n; int specified, bucket; } dpent;
typedef struct dmap { vector<dpent> e; vector<uint16_t> text; } dmap;

/* addPatternWithOptionalSkeleton: a pattern, its skeleton the given one or its own */
static void dp_add(dmap *m, const uint16_t *pat, uint32_t n, const uint16_t *skel, uint32_t sn, int override) {
    dskel s;
    if (skel) dskel_set(skel, sn, &s);
    else dskel_set(pat, n, &s);
    int b = dskel_bucket(&s);
    if (b < 0) return;
    for (size_t i = 0; i < len(m->e); i++) {
        if (m->e[i].bucket != b || !dskel_base_eq(&m->e[i].sk, &s)) continue;
        if ((!m->e[i].specified || (skel && !override)) && !override) return;
        break;
    }
    for (size_t i = 0; i < len(m->e); i++) {
        if (m->e[i].bucket != b || !dskel_original_eq(&m->e[i].sk, &s)) continue;
        if (!override || (skel && m->e[i].specified)) return;
        break;
    }
    /* PatternMap::add: the same base and types overwrite the pattern, else appended */
    uint32_t off = (uint32_t)len(m->text);
    for (uint32_t i = 0; i < n; i++) push_back(m->text, pat[i]);
    for (size_t i = 0; i < len(m->e); i++) {
        if (m->e[i].bucket == b && dskel_base_eq(&m->e[i].sk, &s) && dskel_types_eq(&m->e[i].sk, &s)) {
            m->e[i].off = off;
            m->e[i].n = n;
            m->e[i].specified = skel != 0;
            return;
        }
    }
    dpent e;
    e.sk = s;
    e.off = off;
    e.n = n;
    e.specified = skel != 0;
    e.bucket = b;
    push_back(m->e, e);
}

/* a generator: its locale's data, its default hour letter (fDefaultHourFormatChar), its
   decimal separator, its map */
typedef struct dgen { int li; uint32_t hour_char; uint16_t decimal[8]; uint32_t ndecimal; dmap m; } dgen;

/* getBestRaw: the entry nearest the skeleton by ICU's distance and order; its missing
   and extra fields; -1 for none */
static int dp_best_raw(dgen *g, const dskel *src, int include, int *missing, int *extra) {
    int best = -1, best_d = 0x7FFFFFFF, best_missing = -1;
    *missing = 0;
    *extra = 0;
    for (int b = 0; b < 52; b++) {
        for (size_t i = 0; i < len(g->m.e); i++) {
            if (g->m.e[i].bucket != b) continue;
            int mi, ex;
            int d = dskel_distance(src, &g->m.e[i].sk, include, &mi, &ex);
            if (d < best_d || (d == best_d && best_missing < mi)) {
                best_d = d;
                best_missing = mi;
                best = (int)i;
                *missing = mi;
                *extra = ex;
                if (d == 0) return best;
            }
        }
    }
    return best;
}

#define DTPG_FIXFRAC 2
#define DTPG_MATCH_HOUR 4

/* adjustFieldTypes: a found pattern's fields given the request's lengths and letters (with
   UDATPG_MATCH_HOUR_FIELD_LENGTH, as V8 asks); spec the entry's given skeleton, if any */
static void dp_adjust(const dgen *g, const uint16_t *pat, uint32_t n, const dskel *spec, int has_spec, int flags, const dskel *req, vector<uint16_t> *out) {
    dtoks t;
    pattern_tokens(pat, n, &t);
    for (uint32_t i = 0; i < t.n; i++) {
        uint32_t c0 = pat[t.off[i]];
        if (c0 == '\'') {
            uint32_t e = quote_end(pat, &t, i);
            for (uint32_t k = i; k <= e && k < t.n; k++)
                for (uint32_t u = 0; u < t.len[k]; u++) push_back(*out, pat[t.off[k] + u]);
            i = e;
            continue;
        }
        int row = -1;
        if (is_letter16(c0)) row = canonical_row(c0, t.len[i]);
        if (row < 0) {
            for (uint32_t k = 0; k < t.len[i]; k++) push_back(*out, pat[t.off[i] + k]);
            continue;
        }
        int f = dt_types[row].field;
        if ((flags & DTPG_FIXFRAC) && f == DF_SECOND) {
            for (uint32_t k = 0; k < t.len[i]; k++) push_back(*out, pat[t.off[i] + k]);
            for (uint32_t k = 0; k < g->ndecimal; k++) push_back(*out, g->decimal[k]);
            for (uint32_t k = 0; k < req->ln[DF_FRACTIONAL]; k++) push_back(*out, (uint16_t)req->ch[DF_FRACTIONAL]);
            continue;
        }
        if (req->type[f] == 0) {
            for (uint32_t k = 0; k < t.len[i]; k++) push_back(*out, pat[t.off[i] + k]);
            continue;
        }
        uint32_t req_ch = req->ch[f];
        int req_len = req->ln[f];
        if (req_ch == 'E' && req_len < 3) req_len = 3;
        int adj = req_len;
        if ((f == DF_HOUR && !(flags & DTPG_MATCH_HOUR)) || f == DF_MINUTE || f == DF_SECOND) adj = t.len[i];
        else if (has_spec && req_ch != 'c' && req_ch != 'e') {
            int skel_len = spec->ln[f];
            int pat_numeric = dt_types[row].type > 0, skel_numeric = spec->type[f] > 0;
            if (skel_len == req_len || (pat_numeric && !skel_numeric) || (skel_numeric && !pat_numeric)) adj = t.len[i];
        }
        uint32_t c = c0;
        if (f != DF_HOUR && f != DF_MONTH && f != DF_WEEKDAY && (f != DF_YEAR || req_ch == 'Y')) c = req_ch;
        if (c == 'E' && adj < 3) c = 'e';
        if (f == DF_HOUR && g->hour_char) {
            uint32_t dc = g->hour_char;
            if (req_ch == dc) c = dc;
            else if (req_ch == 'h' && dc == 'K') c = 'K';
            else if (req_ch == 'H' && dc == 'k') c = 'k';
            else if (req_ch == 'k' && dc == 'H') c = 'H';
            else if (req_ch == 'K' && dc == 'h') c = 'h';
        }
        for (int k = 0; k < adj; k++) push_back(*out, (uint16_t)c);
    }
}

/* an entry's pattern adjusted */
static void dp_adjust_entry(dgen *g, int bi, int flags, const dskel *req, vector<uint16_t> *out) {
    vector<uint16_t> pat;
    for (uint32_t k = 0; k < g->m.e[bi].n; k++) push_back(pat, g->m.text[g->m.e[bi].off + k]);
    dskel spec = g->m.e[bi].sk;
    dp_adjust(g, data(pat), (uint32_t)len(pat), &spec, g->m.e[bi].specified, flags, req, out);
}

/* ICU's SimpleFormatter: {0}, {1}, {2} replaced; an apostrophe before a brace or another
   apostrophe quotes, else it is itself */
static void simple_format(const uint16_t *pat, uint32_t n, vector<uint16_t> *a0, vector<uint16_t> *a1, vector<uint16_t> *a2, vector<uint16_t> *out) {
    int in_quote = 0;
    for (uint32_t i = 0; i < n;) {
        uint16_t c = pat[i++];
        if (c == '\'') {
            if (i < n && pat[i] == '\'') { i++; }
            else if (in_quote) { in_quote = 0; continue; }
            else if (i < n && (pat[i] == '{' || pat[i] == '}')) { c = pat[i]; i++; in_quote = 1; }
        } else if (!in_quote && c == '{' && i + 1 < n && pat[i + 1] == '}' && pat[i] >= '0' && pat[i] <= '2') {
            uint32_t k = pat[i] - '0';
            vector<uint16_t> *a = a0;
            if (k == 1) a = a1;
            if (k == 2) a = a2;
            if (a) for (size_t j = 0; j < len(*a); j++) push_back(*out, (*a)[j]);
            i += 2;
            continue;
        }
        push_back(*out, c);
    }
}

/* getTopBitNumber */
static int top_field(int mask) {
    if (!mask) return 0;
    int i = 0;
    while (mask) { mask >>= 1; i++; }
    if (i - 1 > DF_ZONE) return DF_ZONE;
    return i - 1;
}

/* the append item of a field (its locale's, else ICU's "{0} ├{2}: {1}┤") */
static uint32_t append_item(const dgen *g, int field, uint16_t *buf, uint32_t cap) {
    uint32_t n = fld_units(rec_get(g->li, RKEY(IDK_APPEND, 0)), (uint32_t)field, buf, cap);
    if (n) return n;
    static const uint16_t def[] = { '{', '0', '}', ' ', 0x251C, '{', '2', '}', ':', ' ', '{', '1', '}', 0x2524 };
    for (uint32_t i = 0; i < 14; i++) buf[i] = def[i];
    return 14;
}

/* getBestAppending: the fields of a mask from the map, each missing one appended with the
   locale's append items */
static void dp_best_appending(dgen *g, const dskel *req, int fields, int flags, vector<uint16_t> *out) {
    if (!fields) return;
    int missing, extra;
    int bi = dp_best_raw(g, req, fields, &missing, &extra);
    if (bi < 0) return;
    dp_adjust_entry(g, bi, flags, req, out);
    dskel spec = g->m.e[bi].sk;
    int has_spec = g->m.e[bi].specified;
    int last = 0;
    while (missing) {
        if (last == missing) break;
        int sf = (1 << DF_SECOND) | (1 << DF_FRACTIONAL);
        if ((missing & sf) == (1 << DF_FRACTIONAL) && (fields & sf) == sf) {
            vector<uint16_t> cur, fixed;
            for (size_t k = 0; k < len(*out); k++) push_back(cur, (*out)[k]);
            dp_adjust(g, data(cur), (uint32_t)len(cur), &spec, has_spec, flags | DTPG_FIXFRAC, req, &fixed);
            clear(*out);
            for (size_t k = 0; k < len(fixed); k++) push_back(*out, fixed[k]);
            missing &= ~(1 << DF_FRACTIONAL);
            continue;
        }
        int start = missing;
        int bj = dp_best_raw(g, req, missing, &missing, &extra);
        if (bj < 0) break;
        spec = g->m.e[bj].sk;
        has_spec = g->m.e[bj].specified;
        vector<uint16_t> tmp;
        dp_adjust_entry(g, bj, flags, req, &tmp);
        int top = top_field(start & ~missing);
        uint16_t ap[64], nm[64];
        uint32_t an = append_item(g, top, ap, 64);
        uint32_t nn = fld_units(rec_get(g->li, RKEY(IDK_FIELDNAMES, 0)), (uint32_t)top, nm, 64);
        vector<uint16_t> name, res;
        push_back(name, (uint16_t)'\'');
        if (nn) { for (uint32_t k = 0; k < nn; k++) push_back(name, nm[k]); }
        else {
            push_back(name, (uint16_t)'F');
            if (top >= 10) push_back(name, (uint16_t)'1');
            push_back(name, (uint16_t)('0' + top % 10));
        }
        push_back(name, (uint16_t)'\'');
        simple_format(ap, an, out, &tmp, &name, &res);
        clear(*out);
        for (size_t k = 0; k < len(res); k++) push_back(*out, res[k]);
        last = missing;
    }
}

/* the locale's default hour letter (getAllowedHourFormats): timeData by language-region,
   else region (the tag's, else its likely one's), else H; a -u-hc keyword's first */
static uint32_t default_hour_char(const itag *t) {
    int kw = find_kw(t, "hc");
    if (kw >= 0) {
        if (!strcmp(t->val[kw], "h24")) return 'k';
        if (!strcmp(t->val[kw], "h23")) return 'H';
        if (!strcmp(t->val[kw], "h12")) return 'h';
        if (!strcmp(t->val[kw], "h11")) return 'K';
    }
    char lang[9], region[4], key[16];
    copyz(lang, sizeof lang, t->lang);
    copyz(region, sizeof region, t->region);
    if (!lang[0] || !region[0]) {
        char l2[9], s2[5], r2[4];
        if (add_likely(t->lang, t->script, t->region, l2, s2, r2)) {
            copyz(lang, sizeof lang, l2);
            copyz(region, sizeof region, r2);
        }
    }
    if (!region[0]) copyz(region, sizeof region, "001");
    key[0] = 0;
    cat(key, sizeof key, lang);
    cat(key, sizeof key, "-");
    cat(key, sizeof key, region);
    uint32_t v = db_find_z(IDB_HOURS, IDB_HOURS_N, key);
    if (!v) v = db_find_z(IDB_HOURS, IDB_HOURS_N, region);
    if (!v) return 'H';
    uint32_t c = db8(v);
    if (c == 'h' || c == 'H' || c == 'K' || c == 'k') return c;
    return 'H';
}

/* the generator of a locale (ICU's initData): the canonical items, the time and date
   styles' patterns, the available formats */
static void dgen_init(dgen *g, int li, uint32_t hour_char, int nsi) {
    g->li = li;
    g->hour_char = hour_char;
    static const char canon[] = "GyQMwWEDFdaHmsSv";
    for (int i = 0; canon[i]; i++) {
        uint16_t c = (uint16_t)canon[i];
        dp_add(&g->m, &c, 1, 0, 0, 0);
    }
    for (int k = 0; k < 2; k++) {
        uint32_t rec;
        if (k == 0) rec = rec_get(li, RKEY(IDK_TIMEFMT, 0));
        else rec = rec_get(li, RKEY(IDK_DATEFMT, 0));
        for (uint32_t s = 0; s < 4; s++) {
            uint16_t b[160];
            uint32_t n = fld_units(rec, s, b, 160);
            if (n) dp_add(&g->m, b, n, 0, 0, 0);
        }
    }
    uint32_t av = rec_get(li, RKEY(IDK_AVAIL, 0));
    for (uint32_t i = 0;; i += 2) {
        uint16_t sk[64], pat[160];
        uint32_t sn = fld_units(av, i, sk, 64);
        if (!sn) break;
        uint32_t pn = fld_units(av, i + 1, pat, 160);
        dp_add(&g->m, pat, pn, sk, sn, 1);
    }
    nfo o;
    memset(&o, 0, sizeof o);
    o.li = li;
    o.nsi = nsi;
    g->ndecimal = ns_field(&o, SY_DECIMAL, g->decimal, 8);
}

/* getBestPattern(skeleton, flags: DTPG_MATCH_HOUR for UDATPG_MATCH_HOUR_FIELD_LENGTH, as V8
   asks); the skeleton has no j, C, J */
static void dp_best_pattern(dgen *g, const uint16_t *skel, uint32_t sn, int flags, vector<uint16_t> *out) {
    dskel req;
    dskel_set(skel, sn, &req);
    int missing, extra;
    int bi = dp_best_raw(g, &req, -1, &missing, &extra);
    if (bi >= 0 && !missing && !extra) {
        dp_adjust_entry(g, bi, flags, &req, out);
        return;
    }
    int needed = dskel_mask(&req);
    int date_mask = (1 << DF_DAYPERIOD) - 1, time_mask = ((1 << DF_N) - 1) - date_mask;
    vector<uint16_t> dpat, tpat;
    dp_best_appending(g, &req, needed & date_mask, flags, &dpat);
    dp_best_appending(g, &req, needed & time_mask, flags, &tpat);
    if (!len(dpat)) { for (size_t k = 0; k < len(tpat); k++) push_back(*out, tpat[k]); return; }
    if (!len(tpat)) { for (size_t k = 0; k < len(dpat); k++) push_back(*out, dpat[k]); return; }
    /* the glue by the month's width (and a weekday's): the atTime patterns */
    int style = 3;
    if (req.bln[DF_MONTH] == 4) {
        if (req.bln[DF_WEEKDAY]) style = 0;
        else style = 1;
    } else if (req.bln[DF_MONTH] == 3) style = 2;
    uint32_t glue = rec_get(g->li, RKEY(IDK_DTGLUE, 1));
    if (!glue) glue = rec_get(g->li, RKEY(IDK_DTGLUE, 0));
    uint16_t gp[64];
    uint32_t gn = fld_units(glue, (uint32_t)style, gp, 64);
    simple_format(gp, gn, &tpat, &dpat, 0, out);
}

/* ---- time zones: the IANA database's (src/js/tz_data.c, tools/gen-tz.py's: its zones
   compiled as ICU compiles them), their names (src/js/intl_tz.c, CLDR's, by
   tools/gen-intl.py), and the offsets V8 reads itself ---- */

extern const uint32_t js_tz_db[];
extern const uint32_t js_intl_tz[];

/* a zone's kind: UTC and GMT (V8's own UTC, ICU's Etc/GMT: names of their own), a fixed
   offset (+hh:mm and Etc/GMT's), a zone of the database */
enum { TZK_UTC, TZK_GMT, TZK_OFFSET, TZK_ZONE };

/* tz_data's words, and its strings' bytes */
static uint32_t tzw(uint32_t i) { return js_tz_db[i]; }
static uint32_t tzs8(uint32_t off) { return (js_tz_db[TZD_STRINGS + (off >> 2)] >> ((off & 3u) << 3)) & 0xFFu; }
static void tzs_copy(uint32_t off, char *buf, size_t cap) {
    size_t n = 0;
    for (uint32_t c; (c = tzs8(off + (uint32_t)n)) && n + 1 < cap; n++) buf[n] = (char)c;
    buf[n] = 0;
}
/* intl_tz's bytes, its strings, its tables of string pairs */
static uint32_t it8(uint32_t i) { return (js_intl_tz[i >> 2] >> ((i & 3u) << 3)) & 0xFFu; }
static uint32_t it16(uint32_t i) { return it8(i) | it8(i + 1) << 8; }
static uint32_t it32(uint32_t i) { return it16(i) | it16(i + 2) << 16; }
static int it_cmp(uint32_t off, const char *s, size_t n) {
    for (size_t i = 0;; i++) {
        uint32_t c = it8(off + (uint32_t)i);
        uint32_t d = i < n ? (uint8_t)s[i] : 0;
        if (c != d) return c < d ? -1 : 1;
        if (!c) return 0;
    }
}
static void it_copy(uint32_t off, char *buf, size_t cap) {
    size_t n = 0;
    for (uint32_t c; (c = it8(off + (uint32_t)n)) && n + 1 < cap; n++) buf[n] = (char)c;
    buf[n] = 0;
}
static uint32_t it_find(uint32_t table, uint32_t count, const char *key) {
    size_t n = strlen(key);
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = it_cmp(it32(table + mid * 8), key, n);
        if (c == 0) return it32(table + mid * 8 + 4);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

/* a name's index among tz_data's (sorted by their uppercased bytes; up uppercased), -1
   for none */
static int tzname_find(const char *up, size_t n) {
    uint32_t lo = 0, hi = TZD_NAMES_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, off = tzw(TZD_NAMES + mid * 2);
        int c = 0;
        for (size_t i = 0;; i++) {
            uint32_t a = (uint32_t)upper((int)tzs8(off + (uint32_t)i)), b = 0;
            if (i < n) b = (uint8_t)up[i];
            if (a != b) { c = a < b ? -1 : 1; break; }
            if (!a) break;
        }
        if (c == 0) return (int)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}
/* a zone's name (tz_data's) */
static void tzone_name(uint32_t z, char *buf, size_t cap) { tzs_copy(tzw(TZD_ZONES + z * 4), buf, cap); }

/* CLDR's zones by canonical name (16 bytes each): a name's index, -1 for none */
#define TZC_REC 16u
static int tzc_find(const char *name) {
    size_t n = strlen(name);
    uint32_t lo = 0, hi = IDT_ZONES_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = it_cmp(it32(IDT_ZONES + mid * TZC_REC), name, n);
        if (c == 0) return (int)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}
static void tzc_name(int ci, char *buf, size_t cap) { it_copy(it32(IDT_ZONES + (uint32_t)ci * TZC_REC), buf, cap); }
static void tzc_region(int ci, char *buf, size_t cap) { it_copy(it32(IDT_ZONES + (uint32_t)ci * TZC_REC + 4), buf, cap); }
static uint32_t tzc_flags(int ci) { return it8(IDT_ZONES + (uint32_t)ci * TZC_REC + 13); }

/* ICU's getCanonicalCLDRID of a name (zone its tz_data zone's name, ICU's Olson link):
   the name when CLDR has it, else its alias's, else the zone's alias's or the zone */
static void tz_canonical(const char *name, const char *zone, char *out, size_t cap) {
    if (tzc_find(name) >= 0) { copyz(out, cap, name); return; }
    uint32_t a = it_find(IDT_ALIAS, IDT_ALIAS_N, name);
    if (!a) a = it_find(IDT_ALIAS, IDT_ALIAS_N, zone);
    if (a) it_copy(a, out, cap);
    else copyz(out, cap, zone);
}

/* a timeZone option's zone (V8's CreateTimeZone): its kind, offset (minutes east, of a
   fixed one), tz_data's zone and CLDR's (-1 none) and resolved name; 0 for none. V8 makes
   UTC, GMT, Etc/UTC, Etc/GMT, GMT0, GMT+0 and GMT-0 its UTC, and finds a name in ICU's
   (the database's and icuzones') whatever its case; a name canonical as ICU's
   getCanonicalCLDRID, Etc/UTC's and Etc/GMT's UTC */
static int tz_parse(js_vm *vm, uint32_t s, int *kind, int *offset, int *zone, int *canon, char *name, size_t cap) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    *zone = -1;
    *canon = -1;
    /* an offset: [+-−]hh(:?mm)? */
    if (n >= 3) {
        uint32_t c = vm->units[o];
        int sign = 0;
        if (c == '+') sign = 1;
        else if (c == '-' || c == 0x2212) sign = -1;
        uint32_t h0 = vm->units[o + 1], h1 = vm->units[o + 2];
        int ok = (h0 >= '0' && h0 <= '1' && h1 >= '0' && h1 <= '9') || (h0 == '2' && h1 >= '0' && h1 <= '3');
        if (sign && ok) {
            int hh = (int)(h0 - '0') * 10 + (int)(h1 - '0'), mm = 0, good = n == 3;
            if (n > 3) {
                uint32_t p = 3;
                if (vm->units[o + 3] == ':') p = 4;
                if (n - p == 2) {
                    uint32_t m0 = vm->units[o + p], m1 = vm->units[o + p + 1];
                    if (m0 >= '0' && m0 <= '5' && m1 >= '0' && m1 <= '9') { mm = (int)(m0 - '0') * 10 + (int)(m1 - '0'); good = 1; }
                }
            }
            if (good) {
                *offset = sign * (hh * 60 + mm);
                if (!*offset) { *kind = TZK_GMT; copyz(name, cap, "+00:00"); return 1; }
                *kind = TZK_OFFSET;
                char b[8];
                b[0] = '+';
                if (sign < 0) b[0] = '-';
                b[1] = (char)('0' + hh / 10);
                b[2] = (char)('0' + hh % 10);
                b[3] = ':';
                b[4] = (char)('0' + mm / 10);
                b[5] = (char)('0' + mm % 10);
                b[6] = 0;
                copyz(name, cap, b);
                return 1;
            }
        }
    }
    /* a name: ASCII */
    char up[64];
    if (n == 0 || n >= sizeof up) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = vm->units[o + i];
        if (c == 0 || c >= 128) return 0;
        up[i] = (char)upper((int)c);
    }
    up[n] = 0;
    *offset = 0;
    static const char *const v8_utc[] = { "UTC", "GMT", "ETC/UTC", "ETC/GMT", "GMT0", "GMT+0", "GMT-0", 0 };
    for (int i = 0; v8_utc[i]; i++)
        if (!strcmp(up, v8_utc[i])) { *kind = TZK_UTC; copyz(name, cap, "UTC"); return 1; }
    int ni = tzname_find(up, n);
    if (ni < 0) return 0;
    char icu[64], zn[64], cn[64];
    tzs_copy(tzw(TZD_NAMES + (uint32_t)ni * 2), icu, sizeof icu);
    uint32_t z = tzw(TZD_NAMES + (uint32_t)ni * 2 + 1);
    tzone_name(z, zn, sizeof zn);
    if (!strcmp(zn, "Etc/UTC")) { *kind = TZK_UTC; copyz(name, cap, "UTC"); return 1; }
    if (!strcmp(zn, "Etc/GMT")) { *kind = TZK_GMT; copyz(name, cap, "UTC"); return 1; }
    if (!strncmp(zn, "Etc/GMT", 7) && (zn[7] == '+' || zn[7] == '-')) {
        int h = atoi(zn + 8);
        *kind = TZK_OFFSET;
        *offset = zn[7] == '+' ? -h * 60 : h * 60;
        copyz(name, cap, zn);
        return 1;
    }
    tz_canonical(icu, zn, cn, sizeof cn);
    if (!strcmp(cn, "Etc/Unknown")) return 0;
    *kind = TZK_ZONE;
    *zone = (int)z;
    *canon = tzc_find(cn);
    copyz(name, cap, cn);
    return 1;
}

/* ---- a zone's offsets: its transitions, then its final rule (ICU's OlsonTimeZone) ---- */

/* a zone's record read: its types' and transitions' places and counts, its final rule's */
typedef struct tzrec { uint32_t base, ntypes, ntrans, npre, npost, n32, times, map, fin; int has_final; } tzrec;
static void tz_rec(uint32_t z, tzrec *r) {
    uint32_t w = TZD_ZONES + z * 4u, a = tzw(w + 2), b = tzw(w + 3);
    r->base = tzw(w + 1);
    r->ntypes = a & 0xFFu;
    r->ntrans = (a >> 8) & 0xFFFFu;
    r->has_final = (int)((a >> 24) & 1u);
    r->npre = b & 0xFFFFu;
    r->npost = b >> 16;
    r->n32 = r->ntrans - r->npre - r->npost;
    r->times = r->base + 2u * r->ntypes;
    r->map = r->times + 2u * r->npre + r->n32 + 2u * r->npost;
    r->fin = r->map + (r->ntrans + 3u) / 4u;
}
/* transition k's time (UTC seconds), its type, a type's standard offset and DST amount */
static int64_t tz_time(const tzrec *r, uint32_t k) {
    uint32_t i;
    if (k < r->npre) i = r->times + 2u * k;
    else if (k < r->npre + r->n32) return (int64_t)(int32_t)tzw(r->times + 2u * r->npre + (k - r->npre));
    else i = r->times + 2u * r->npre + r->n32 + 2u * (k - r->npre - r->n32);
    return (int64_t)((uint64_t)tzw(i) | (uint64_t)tzw(i + 1) << 32);
}
static uint32_t tz_type(const tzrec *r, uint32_t k) { return (tzw(r->map + k / 4u) >> ((k & 3u) << 3)) & 0xFFu; }
static int tz_raw(const tzrec *r, uint32_t ty) { return (int)(int32_t)tzw(r->base + 2u * ty); }
static int tz_dst(const tzrec *r, uint32_t ty) { return (int)(int32_t)tzw(r->base + 2u * ty + 1u); }
/* the last transition at or before a time (UTC seconds): its index, -1 for none */
static int tz_at(const tzrec *r, int64_t sec) {
    int lo = 0, hi = (int)r->ntrans;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (tz_time(r, (uint32_t)mid) <= sec) lo = mid + 1;
        else hi = mid;
    }
    return lo - 1;
}

/* the final rule, as SimpleTimeZone keeps it: each end's month (0 January), day and
   weekday (1 Sunday) and how they are read (SimpleTimeZone's modes), time (ms) and
   whether it is wall (0), standard (1) or UTC (2) time; the DST amount, the standard
   offset (seconds), the year it starts in; its first transition (UTC seconds) and
   whether it is to daylight time */
enum { STZ_DOM, STZ_DOW_IN_MONTH, STZ_GE, STZ_LE };
typedef struct tzfinal {
    int month[2], day[2], dow[2], mode[2], tmode[2];
    int64_t time[2];
    int save, raw;
    int64_t year, first;
    int first_dst;
} tzfinal;
static void tz_final(const tzrec *r, tzfinal *f) {
    for (uint32_t e = 0; e < 2; e++) {
        uint32_t a = tzw(r->fin + 2u * e);
        int day = (int)(int8_t)(uint8_t)((a >> 8) & 0xFFu), dow = (int)(int8_t)(uint8_t)((a >> 16) & 0xFFu);
        f->month[e] = (int)(a & 0xFFu);
        f->tmode[e] = (int)(a >> 24);
        f->time[e] = (int64_t)(int32_t)tzw(r->fin + 2u * e + 1u) * 1000;
        /* SimpleTimeZone's decodeStartRule and decodeEndRule */
        if (dow == 0) f->mode[e] = STZ_DOM;
        else if (dow > 0) f->mode[e] = STZ_DOW_IN_MONTH;
        else {
            dow = -dow;
            if (day > 0) f->mode[e] = STZ_GE;
            else { day = -day; f->mode[e] = STZ_LE; }
        }
        f->day[e] = day;
        f->dow[e] = dow;
    }
    f->save = (int)(int32_t)tzw(r->fin + 4);
    f->raw = (int)(int32_t)tzw(r->fin + 5);
    f->year = (int64_t)(int32_t)tzw(r->fin + 6);
    f->first = (int64_t)tzw(r->fin + 7);
    f->first_dst = (int)tzw(r->fin + 8);
}

static int64_t days_from_civil(int64_t y, int m, int d) {
    if (m <= 2) y--;
    int64_t era = div_floor(y, 400);
    int64_t yoe = y - era * 400;
    int64_t mp = m > 2 ? m - 3 : m + 9;
    int64_t doy = (153 * mp + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static int is_leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int month_len(int64_t y, int m) {
    static const int len[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 1 && is_leap(y)) return 29;
    return len[m];
}
/* a day's year, month (0 January), day and weekday (1 Sunday), Grego::dayToFields's */
static void day_fields(int64_t day, int64_t *year, int *month, int *dom, int *dow) {
    int64_t z = day + 719468;
    int64_t era = div_floor(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *dom = (int)(doy - (153 * mp + 2) / 5 + 1);
    if (mp < 10) *month = (int)(mp + 2);
    else *month = (int)(mp - 10);
    *year = yoe + era * 400;
    if (*month <= 1) (*year)++;
    int64_t w = (day + 4) % 7;
    if (w < 0) w += 7;
    *dow = (int)w + 1;
}
/* SimpleTimeZone::compareToRule: a local time against a rule's, -1 before, 0 on, 1 after */
static int stz_compare(int month, int mlen, int plen, int dom, int dow, int64_t millis, int64_t delta, int rmode, int rmonth,
                       int rdow, int rday, int64_t rmillis) {
    millis += delta;
    while (millis >= 86400000) {
        millis -= 86400000;
        ++dom;
        dow = 1 + (dow % 7);
        if (dom > mlen) { dom = 1; ++month; }
    }
    while (millis < 0) {
        millis += 86400000;
        --dom;
        dow = 1 + ((dow + 5) % 7);
        if (dom < 1) { dom = plen; --month; }
    }
    if (month < rmonth) return -1;
    if (month > rmonth) return 1;
    int rdom = 0;
    if (rday > mlen) rday = mlen;
    if (rmode == STZ_DOM) rdom = rday;
    else if (rmode == STZ_DOW_IN_MONTH) {
        if (rday > 0) rdom = 1 + (rday - 1) * 7 + (7 + rdow - (dow - dom + 1)) % 7;
        else rdom = mlen + (rday + 1) * 7 - (7 + (dow + mlen - dom) - rdow) % 7;
    } else if (rmode == STZ_GE) rdom = rday + (49 + rdow - rday - dow + dom) % 7;
    else rdom = rday - (49 - rdow + rday + dow - dom) % 7;
    if (dom < rdom) return -1;
    if (dom > rdom) return 1;
    if (millis < rmillis) return -1;
    if (millis > rmillis) return 1;
    return 0;
}
/* the final rule's DST amount (seconds) at a local standard time (ms; SimpleTimeZone's
   getOffset of its fields) */
static int stz_dst(const tzfinal *f, int64_t local) {
    int64_t raw = (int64_t)f->raw * 1000;
    int64_t day = div_floor(local, 86400000), millis = local - day * 86400000, year;
    int month, dom, dow;
    day_fields(day, &year, &month, &dom, &dow);
    int mlen = month_len(year, month), plen = 31;
    if (month > 0) plen = month_len(year, month - 1);
    int southern = f->month[0] > f->month[1];
    int64_t sdelta = 0, edelta = 0;
    if (f->tmode[0] == 2) sdelta = -raw;
    if (f->tmode[1] == 0) edelta = (int64_t)f->save * 1000;
    else if (f->tmode[1] == 2) edelta = -raw;
    int sc = stz_compare(month, mlen, plen, dom, dow, millis, sdelta, f->mode[0], f->month[0], f->dow[0], f->day[0], f->time[0]);
    int ec = 0;
    if (southern != (sc >= 0))
        ec = stz_compare(month, mlen, plen, dom, dow, millis, edelta, f->mode[1], f->month[1], f->dow[1], f->day[1], f->time[1]);
    if ((!southern && sc >= 0 && ec < 0) || (southern && (sc >= 0 || ec < 0))) return f->save;
    return 0;
}

/* a zone's standard offset and DST amount (seconds) at a time (ms from 1970, whole):
   OlsonTimeZone::getOffset */
static void tz_offsets(uint32_t z, double t, int *raw, int *dst) {
    tzrec r;
    tz_rec(z, &r);
    int64_t ms = (int64_t)t;
    if (r.has_final) {
        tzfinal f;
        tz_final(&r, &f);
        if (ms >= days_from_civil(f.year, 1, 1) * 86400000) {
            *raw = f.raw;
            *dst = stz_dst(&f, ms + (int64_t)f.raw * 1000);
            return;
        }
    }
    int k = tz_at(&r, div_floor(ms, 1000));
    uint32_t ty = 0;
    if (k >= 0) ty = tz_type(&r, (uint32_t)k);
    *raw = tz_raw(&r, ty);
    *dst = tz_dst(&r, ty);
}
/* the offsets at a local wall time (getOffset's local reading, kFormer for a time a
   transition skips and kLatter for one it repeats: a transition counts from its time in
   the offset after it) */
static void tz_offsets_local(uint32_t z, int64_t local, int *raw, int *dst) {
    tzrec r;
    tz_rec(z, &r);
    if (r.has_final) {
        tzfinal f;
        tz_final(&r, &f);
        if (local >= days_from_civil(f.year, 1, 1) * 86400000) {
            /* TimeZone::getOffset's two passes */
            int d = stz_dst(&f, local);
            if (d) d = stz_dst(&f, local - (int64_t)d * 1000);
            *raw = f.raw;
            *dst = d;
            return;
        }
    }
    int64_t sec = div_floor(local, 1000);
    int k = (int)r.ntrans - 1;
    for (; k >= 0; k--) {
        uint32_t ty = tz_type(&r, (uint32_t)k);
        if (sec >= tz_time(&r, (uint32_t)k) + tz_raw(&r, ty) + tz_dst(&r, ty)) break;
    }
    uint32_t ty = 0;
    if (k >= 0) ty = tz_type(&r, (uint32_t)k);
    *raw = tz_raw(&r, ty);
    *dst = tz_dst(&r, ty);
}
/* TZGNCore's check of a time in standard time: whether daylight time is near, the
   transition at or before it (within 184 days) from it, or the one after it (within 184
   days) to it; a final rule's transitions always are */
static int tz_dst_near(uint32_t z, double t) {
    tzrec r;
    tz_rec(z, &r);
    int64_t ms = (int64_t)t, range = (int64_t)184 * 86400000;
    tzfinal f;
    memset(&f, 0, sizeof f);
    if (r.has_final) {
        tz_final(&r, &f);
        if (ms >= f.first * 1000) return 1;
    }
    int k = tz_at(&r, div_floor(ms, 1000));
    if (k >= 0) {
        uint32_t from = 0;
        if (k > 0) from = tz_type(&r, (uint32_t)k - 1);
        if (ms - tz_time(&r, (uint32_t)k) * 1000 < range && tz_dst(&r, from)) return 1;
    }
    if ((uint32_t)(k + 1) < r.ntrans) {
        if (tz_time(&r, (uint32_t)k + 1) * 1000 - ms < range && tz_dst(&r, tz_type(&r, (uint32_t)k + 1))) return 1;
    } else if (r.has_final) {
        if (f.first * 1000 - ms < range && f.first_dst) return 1;
    }
    return 0;
}

/* a number's digits in a numbering system, zero-padded to min (ICU's zeroPaddingNumber: at
   most ten digits) */
static void put_digits(int nsi, int64_t v, int min, vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t type) {
    if (v < 0) v = -v;
    char d[24];
    int n = 0;
    do { d[n++] = (char)(v % 10); v /= 10; } while (v && n < 10);
    if (min > 20) min = 20;
    while (n < min) d[n++] = 0;
    while (n > 0) put_c(tx, ty, ns_digit(nsi, d[--n]), type);
}

/* the localized GMT format of an offset in seconds (TimeZoneFormat::formatOffsetLocalizedGMT):
   the hour format with seconds where the offset has them (expandOffsetPattern: ":ss"
   after the minutes, the hours' separator before), else without its minutes in the short
   format where it has none (truncateOffsetPattern), the hour's leading zero dropped in
   the short format */
static void tz_gmt(int li, int nsi, int offset, int is_short, vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t type) {
    uint32_t rec = rec_get(li, RKEY(IDK_TZ, 0));
    uint16_t gf[48], hf[48];
    uint32_t gn = fld_units(rec, 0, gf, 48), hn = fld_units(rec, 2, hf, 48);
    uint32_t semi = hn;
    for (uint32_t i = 0; i < hn; i++) if (hf[i] == ';') { semi = i; break; }
    int neg = offset < 0, a = offset;
    if (neg) a = -offset;
    int hh = a / 3600, mm = a / 60 % 60, ss = a % 60;
    uint32_t ps = 0, pe = semi;
    if (neg && semi < hn) { ps = semi + 1; pe = hn; }
    uint32_t at = pe;
    for (uint32_t i = ps; i + 1 < pe; i++) if (hf[i] == 'm' && hf[i + 1] == 'm') { at = i; break; }
    /* the pattern: [ps, pe), with ":ss" (the separator) put after the minutes */
    uint16_t pat[64];
    uint32_t pn = 0;
    if (ss && at < pe) {
        uint32_t hpos = ps, found = 0;
        for (uint32_t i = ps; i < at; i++) if (hf[i] == 'H') { hpos = i; found = 1; }
        for (uint32_t i = ps; i < at + 2 && pn < 60; i++) pat[pn++] = hf[i];
        if (found) for (uint32_t i = hpos + 1; i < at && pn < 60; i++) pat[pn++] = hf[i];
        pat[pn++] = 's';
        pat[pn++] = 's';
        for (uint32_t i = at + 2; i < pe && pn < 62; i++) pat[pn++] = hf[i];
    } else {
        uint32_t cut = pe;
        if (is_short && mm == 0 && at < pe) {
            for (uint32_t i = at; i > ps; i--) if (hf[i - 1] == 'H') { cut = i; break; }
        }
        for (uint32_t i = ps; i < cut && pn < 64; i++) pat[pn++] = hf[i];
    }
    uint32_t z = gn;
    for (uint32_t i = 0; i + 2 < gn; i++) if (gf[i] == '{' && gf[i + 1] == '0' && gf[i + 2] == '}') { z = i; break; }
    put_u(tx, ty, gf, z, type);
    for (uint32_t i = 0; i < pn;) {
        uint32_t c = pat[i];
        if (c == 'H' || c == 'm' || c == 's') {
            while (i < pn && pat[i] == c) i++;
            if (c == 'H') {
                if (is_short) put_digits(nsi, hh, 1, tx, ty, type);
                else put_digits(nsi, hh, 2, tx, ty, type);
            } else if (c == 'm') put_digits(nsi, mm, 2, tx, ty, type);
            else put_digits(nsi, ss, 2, tx, ty, type);
            continue;
        }
        put_c(tx, ty, c, type);
        i++;
    }
    if (z + 3 <= gn) put_u(tx, ty, gf + z + 3, gn - z - 3, type);
}

/* ---- the pattern's formatting (SimpleDateFormat::subFormat) ---- */

/* the parts' types */
enum { DP_LITERAL, DP_ERA, DP_YEAR, DP_MONTH, DP_DAY, DP_WEEKDAY, DP_DAYPERIOD, DP_HOUR, DP_MINUTE, DP_SECOND, DP_FRACTION,
       DP_TZ, DP_N };
static const char *const dp_names[] = { "literal", "era", "year", "month", "day", "weekday", "dayPeriod", "hour", "minute",
    "second", "fractionalSecond", "timeZoneName" };

/* the day periods' names (a record's fields) and ICU's periods */
enum { DPN_AM, DPN_PM, DPN_MIDNIGHT, DPN_NOON, DPN_MORNING1, DPN_MORNING2, DPN_AFTERNOON1, DPN_AFTERNOON2, DPN_EVENING1,
       DPN_EVENING2, DPN_NIGHT1, DPN_NIGHT2, DPN_N };
static const char *const dpn_names[DPN_N] = { "am", "pm", "midnight", "noon", "morning1", "morning2", "afternoon1",
    "afternoon2", "evening1", "evening2", "night1", "night2" };

/* a formatter's context: its data locale, digits, zone (its kind, a fixed one's offset in
   minutes, tz_data's zone and CLDR's, -1 for none, the locale's region for the metazones'
   zones, its letters a byte each); its language's day period rules (each hour's period, -1
   none, and whether midnight and noon have one) */
typedef struct dctx { int li, nsi, tzkind, offset, zone, canon, region; int hours[24]; int has_rules, has_midnight, has_noon; } dctx;

/* a time's fields in the formatter's zone */
static void dtf_fields(const dctx *c, double t, dfields *f) {
    int raw = 0, dst = 0;
    if (c->tzkind == TZK_OFFSET) raw = c->offset * 60;
    else if (c->tzkind == TZK_ZONE) tz_offsets((uint32_t)c->zone, t, &raw, &dst);
    date_fields(t, raw + dst, f);
    f->raw = raw;
    f->dst = dst;
}

/* ---- the zones' names: ICU's TimeZoneNames, TimeZoneGenericNames and TimeZoneFormat
   over CLDR's (src/js/intl_tz.c) ---- */

static uint32_t region_name(int li, const char *code, int is_short, uint16_t *buf, uint32_t cap);
static void tz_put(vector<uint16_t> *out, const uint16_t *b, uint32_t n) { for (uint32_t i = 0; i < n; i++) push_back(*out, b[i]); }
static void tz_put_ascii(vector<uint16_t> *out, const char *s) { for (size_t i = 0; s[i]; i++) push_back(*out, (uint16_t)(uint8_t)s[i]); }

/* a resources table's value for a key (intl_tz's, pairs of words sorted by key): 0 none */
static uint32_t it_res_in(uint32_t table, uint32_t n, uint32_t key) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, k = it32(table + mid * 8);
        if (k == key) return it32(table + mid * 8 + 4);
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
/* a locale's record of zone names: its own, its nearest ancestor's, else root's; 0 none */
static uint32_t tzn_rec(int li, uint32_t key) {
    for (int guard = 0; li >= 0 && li != 0xFFFF && guard < 32; guard++) {
        uint32_t t = IDT_LOCALES + (uint32_t)li * 8;
        uint32_t v = it_res_in(it32(t), it32(t + 4), key);
        if (v) return v;
        li = (int)locale_parent((uint32_t)li);
    }
    return it_res_in(IDT_ROOT, IDT_ROOT_N, key);
}
/* a record's field i (fields joined by 0x1F) as UTF-16 units into buf: their count */
static uint32_t it_fld_units(uint32_t rec, uint32_t i, uint16_t *buf, uint32_t cap) {
    if (!rec) return 0;
    uint32_t p = rec;
    for (uint32_t k = 0; k < i; k++) {
        while (it8(p) && it8(p) != 0x1F) p++;
        if (!it8(p)) return 0;
        p++;
    }
    uint32_t n = 0;
    while (it8(p) && it8(p) != 0x1F && n + 2 <= cap) {
        uint32_t c = it8(p), cp, w = 1;
        if (c < 0x80) cp = c;
        else if (c < 0xE0) { cp = (c & 0x1F) << 6 | (it8(p + 1) & 0x3F); w = 2; }
        else if (c < 0xF0) { cp = (c & 0x0F) << 12 | (it8(p + 1) & 0x3F) << 6 | (it8(p + 2) & 0x3F); w = 3; }
        else { cp = (c & 0x07) << 18 | (it8(p + 1) & 0x3F) << 12 | (it8(p + 2) & 0x3F) << 6 | (it8(p + 3) & 0x3F); w = 4; }
        if (cp >= 0x10000) { buf[n++] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10)); buf[n++] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
        else buf[n++] = (uint16_t)cp;
        p += w;
    }
    return n;
}

/* the six names of a metazone or a zone, in their records' order */
enum { TZN_LG, TZN_LS, TZN_LD, TZN_SG, TZN_SS, TZN_SD };
#define TZN_MAX 128
/* a zone's own name of a type (getTimeZoneDisplayName), a metazone's (getMetaZoneDisplayName) */
static uint32_t tz_zone_name(int li, int ci, int type, uint16_t *buf, uint32_t cap) {
    if (ci < 0) return 0;
    return it_fld_units(tzn_rec(li, RKEY(IDT_ZNNAMES, ci)), (uint32_t)type + 1, buf, cap);
}
static uint32_t tz_mz_name(int li, int mz, int type, uint16_t *buf, uint32_t cap) {
    if (mz < 0) return 0;
    return it_fld_units(tzn_rec(li, RKEY(IDT_MZNAMES, mz)), (uint32_t)type, buf, cap);
}
/* a zone's metazone at a time (ZoneMeta::getMetazoneID: its periods from 1970), -1 none */
static int tz_metazone(int ci, double t) {
    if (ci < 0) return -1;
    uint32_t rec = IDT_ZONES + (uint32_t)ci * TZC_REC, p = it32(rec + 8), n = it8(rec + 12);
    for (uint32_t i = 0; i < n; i++, p += 10) {
        double from = (double)it32(p) * 60000.0, to = (double)it32(p + 4) * 60000.0;
        if (from <= t && to > t) return (int)it16(p + 8);
    }
    return -1;
}
/* a metazone's zone in a region (getReferenceZoneID: the region's, else the world's), its
   region's letters a byte each: its CLDR index, -1 for none */
#define TZ_WORLD ('0' | '0' << 8 | '1' << 16)
static int tz_golden(int mz, int region) {
    uint32_t rec = IDT_MZ + (uint32_t)mz * 12, p = it32(rec + 4), n = it32(rec + 8);
    int world = -1;
    for (uint32_t i = 0; i < n; i++, p += 6) {
        int r = (int)it32(p);
        if (r == region) return (int)it16(p + 4);
        if (r == TZ_WORLD) world = (int)it16(p + 4);
    }
    return world;
}
static int tz_region_code(const char *r) {
    int v = 0;
    for (int i = 0; i < 3 && r[i]; i++) v |= (int)(uint8_t)r[i] << (8 * i);
    return v;
}
/* a CLDR zone's tz_data zone (its name's), -1 for none: a table made once a process */
static int32_t tzc_tz[IDT_ZONES_N];
static int tzc_tz_made;
static int tzc_zone(int ci) {
    if (!tzc_tz_made) {
        for (uint32_t i = 0; i < IDT_ZONES_N; i++) {
            char name[64], up[64];
            tzc_name((int)i, name, sizeof name);
            size_t k = 0;
            for (; name[k] && k + 1 < sizeof up; k++) up[k] = (char)upper((unsigned char)name[k]);
            up[k] = 0;
            int ni = tzname_find(up, k);
            tzc_tz[i] = -1;
            if (ni >= 0) tzc_tz[i] = (int32_t)tzw(TZD_NAMES + (uint32_t)ni * 2 + 1);
        }
        tzc_tz_made = 1;
    }
    return (int)tzc_tz[ci];
}
/* a zone's exemplar city (getExemplarLocationName): the locale's, else its name's last
   part, its underscores spaces (none for Etc/, SystemV/ or a name with no area) */
static uint32_t tz_city(int li, int ci, uint16_t *buf, uint32_t cap) {
    uint32_t n = it_fld_units(tzn_rec(li, RKEY(IDT_ZNNAMES, ci)), 0, buf, cap);
    if (n) return n;
    char name[64];
    tzc_name(ci, name, sizeof name);
    if (!strncmp(name, "Etc/", 4) || !strncmp(name, "SystemV/", 8) || strstr(name, "Riyadh8")) return 0;
    const char *slash = strrchr(name, '/');
    if (!slash || !slash[1]) return 0;
    for (const char *s = slash + 1; *s && n < cap; s++) buf[n++] = (uint16_t)(*s == '_' ? ' ' : (uint8_t)*s);
    return n;
}
/* the locale's region for the metazones' zones (TZGNCore's target region: its own, else
   its likely one) */
static void tz_target_region(int li, char *out, size_t cap) {
    char tag[TAG_MAX + 1];
    db_copy(db32(IDB_LOCALES + (uint32_t)li * LOC_REC), tag, sizeof tag);
    out[0] = 0;
    itag t;
    if (tag_from_text(tag, strlen(tag), &t)) return;
    if (t.region[0]) { copyz(out, cap, t.region); return; }
    char l2[9], s2[5], r2[4];
    if (add_likely(t.lang, t.script, t.region, l2, s2, r2)) copyz(out, cap, r2);
}
/* units equal, ASCII letters' case aside (UnicodeString::caseCompare's verdict on CLDR's names) */
static int units_case_eq(const uint16_t *a, uint32_t an, const uint16_t *b, uint32_t bn) {
    if (an != bn) return 0;
    for (uint32_t i = 0; i < an; i++) {
        uint32_t x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}
/* a formatter's region format ({0} Time) or fallback format ({1} ({0})), as units */
static uint32_t tz_format(int li, uint32_t which, uint16_t *buf, uint32_t cap) {
    uint32_t n = it_fld_units(tzn_rec(li, RKEY(IDT_FMT, 0)), which, buf, cap);
    if (n || which) return n;
    buf[0] = '{';
    buf[1] = '0';
    buf[2] = '}';
    return 3;
}

/* the specific name (TimeZoneFormat::formatSpecific): the zone's own of its time's type
   (daylight where DST is in use), else its metazone's at the time */
static void tz_specific(const dctx *c, const dfields *f, int is_long, vector<uint16_t> *out) {
    int type = TZN_SS;
    if (is_long) type = TZN_LS;
    if (f->dst) type++;
    uint16_t b[TZN_MAX];
    uint32_t n = tz_zone_name(c->li, c->canon, type, b, TZN_MAX);
    if (!n) n = tz_mz_name(c->li, tz_metazone(c->canon, f->t), type, b, TZN_MAX);
    tz_put(out, b, n);
}
/* the generic location name (getGenericLocationName): the region format of the country's
   name, where the zone is its country's (its only one, or its primary zone), else of the
   exemplar city; none for a zone of no country */
static void tz_location(const dctx *c, vector<uint16_t> *out) {
    if (c->canon < 0) return;
    char region[8];
    tzc_region(c->canon, region, sizeof region);
    if (!strcmp(region, "001")) return;
    uint16_t b[TZN_MAX], p[64];
    uint32_t n = 0;
    if (tzc_flags(c->canon) & 2) n = region_name(c->li, region, 0, b, TZN_MAX);
    else n = tz_city(c->li, c->canon, b, TZN_MAX);
    vector<uint16_t> arg;
    tz_put(&arg, b, n);
    uint32_t pn = tz_format(c->li, 0, p, 64);
    simple_format(p, pn, &arg, 0, 0, out);
}
/* the partial location name (getPartialLocationName): the fallback format of the
   location (the country's name where the zone is the metazone's in its country, else
   the exemplar city; the zone's name for a zone of no country nor city) and the
   metazone's name */
static void tz_partial(const dctx *c, int mz, const uint16_t *mzn, uint32_t mn, vector<uint16_t> *out) {
    char region[8], name[64];
    tzc_region(c->canon, region, sizeof region);
    tzc_name(c->canon, name, sizeof name);
    uint16_t b[TZN_MAX], p[64];
    uint32_t n = 0;
    vector<uint16_t> loc, mzv;
    if (strcmp(region, "001")) {
        if (tz_golden(mz, tz_region_code(region)) == c->canon) n = region_name(c->li, region, 0, b, TZN_MAX);
        else n = tz_city(c->li, c->canon, b, TZN_MAX);
        tz_put(&loc, b, n);
    } else {
        n = tz_city(c->li, c->canon, b, TZN_MAX);
        if (n) tz_put(&loc, b, n);
        else tz_put_ascii(&loc, name);
    }
    tz_put(&mzv, mzn, mn);
    uint32_t pn = tz_format(c->li, 1, p, 64);
    simple_format(p, pn, &loc, &mzv, 0, out);
}
/* the generic name (TZGNCore::getDisplayName, its long or short non-location format):
   the zone's own, else its metazone's (its standard name where the zone keeps standard
   time all around and that is not the generic one; its partial location name where the
   metazone's zone in the locale's region keeps other offsets then), else the generic
   location name; empty for none */
static void tz_generic(const dctx *c, const dfields *f, int is_long, vector<uint16_t> *out) {
    int gtype = TZN_SG, stype = TZN_SS;
    if (is_long) { gtype = TZN_LG; stype = TZN_LS; }
    uint16_t b[TZN_MAX];
    uint32_t n = tz_zone_name(c->li, c->canon, gtype, b, TZN_MAX);
    if (n) { tz_put(out, b, n); return; }
    int mz = tz_metazone(c->canon, f->t);
    if (mz >= 0) {
        if (f->dst == 0 && !tz_dst_near((uint32_t)c->zone, f->t)) {
            uint16_t g[TZN_MAX];
            n = tz_zone_name(c->li, c->canon, stype, b, TZN_MAX);
            if (!n) n = tz_mz_name(c->li, mz, stype, b, TZN_MAX);
            if (n) {
                uint32_t gn = tz_mz_name(c->li, mz, gtype, g, TZN_MAX);
                if (!units_case_eq(b, n, g, gn)) { tz_put(out, b, n); return; }
            }
        }
        n = tz_mz_name(c->li, mz, gtype, b, TZN_MAX);
        if (n) {
            int gc = tz_golden(mz, c->region);
            if (gc >= 0 && gc != c->canon) {
                int gz = tzc_zone(gc);
                if (gz >= 0) {
                    int raw1 = 0, dst1 = 0;
                    tz_offsets_local((uint32_t)gz, (int64_t)f->t + (int64_t)(f->raw + f->dst) * 1000, &raw1, &dst1);
                    if (f->raw != raw1 || f->dst != dst1) { tz_partial(c, mz, b, n, out); return; }
                }
            }
            tz_put(out, b, n);
            return;
        }
    }
    tz_location(c, out);
}

/* DayPeriodRules of the locale's tag, truncated until CLDR has rules for it */
static void dp_rules(dctx *c) {
    for (int h = 0; h < 24; h++) c->hours[h] = -1;
    c->has_rules = 0;
    c->has_midnight = 0;
    c->has_noon = 0;
    char tag[TAG_MAX + 1];
    db_copy(db32(IDB_LOCALES + (uint32_t)c->li * LOC_REC), tag, sizeof tag);
    uint32_t r = 0;
    for (;;) {
        r = db_find_z(IDB_DAYPERIOD_RULES, IDB_DAYPERIOD_RULES_N, tag);
        if (r) break;
        char *dash = strrchr(tag, '-');
        if (!dash) return;
        *dash = 0;
    }
    /* "name@HHMM" or "name:HHMM-HHMM", by spaces */
    while (db8(r)) {
        char nm[16];
        size_t k = 0;
        while (db8(r) && db8(r) != '@' && db8(r) != ':' && k + 1 < sizeof nm) nm[k++] = (char)db8(r++);
        nm[k] = 0;
        int idx = -1;
        for (int i = 0; i < DPN_N; i++) if (!strcmp(nm, dpn_names[i])) idx = i;
        uint32_t sep = db8(r);
        r++;
        int a = (int)(db8(r) - '0') * 10 + (int)(db8(r + 1) - '0');
        r += 4;
        if (sep == '@') {
            if (idx == DPN_MIDNIGHT) c->has_midnight = 1;
            if (idx == DPN_NOON) c->has_noon = 1;
        } else {
            r++;
            int b = (int)(db8(r) - '0') * 10 + (int)(db8(r + 1) - '0');
            r += 4;
            for (int i = a; i != b; ++i) {
                if (i == 24) i = 0;
                if (i == b) break;
                c->hours[i] = idx;
            }
        }
        while (db8(r) == ' ') r++;
    }
    c->has_rules = c->hours[0] >= 0;
}

/* a name of a record's field (by its key's index) appended: 0 when it has none */
static int put_name(int li, uint32_t key, uint32_t f, vector<uint16_t> *tx, vector<uint8_t> *ty, uint32_t type) {
    uint16_t b[96];
    uint32_t n = fld_units(rec_get(li, key), f, b, 96);
    put_u(tx, ty, b, n, type);
    return n != 0;
}

/* the width of a day period's name by its letters' count: abbreviated, narrow, wide */
static uint32_t dp_width(int count) {
    if (count <= 3) return 0;
    if (count == 5) return 1;
    return 2;
}

static void dt_field(const dctx *c, uint32_t ch, int count, const dfields *f, int has_m, int has_s, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    int64_t yoe = f->year;
    if (yoe <= 0) yoe = 1 - yoe;
    switch (ch) {
    case 'G': {
        uint32_t w = 0;
        if (count == 5) w = 1;
        else if (count == 4) w = 2;
        uint32_t era = f->year > 0;
        put_name(c->li, RKEY(IDK_ERAS, w), era, tx, ty, DP_ERA);
        break;
    }
    case 'y': case 'Y': case 'u': case 'U': case 'r':
        if (count == 2) put_digits(c->nsi, yoe % 100, 2, tx, ty, DP_YEAR);
        else put_digits(c->nsi, yoe, count, tx, ty, DP_YEAR);
        break;
    case 'M': case 'L': {
        uint32_t ctx = ch == 'L';
        if (count == 5) put_name(c->li, RKEY(IDK_MONTHS, ctx * 3 + 1), (uint32_t)(f->month - 1), tx, ty, DP_MONTH);
        else if (count == 4) put_name(c->li, RKEY(IDK_MONTHS, ctx * 3 + 2), (uint32_t)(f->month - 1), tx, ty, DP_MONTH);
        else if (count == 3) put_name(c->li, RKEY(IDK_MONTHS, ctx * 3), (uint32_t)(f->month - 1), tx, ty, DP_MONTH);
        else put_digits(c->nsi, f->month, count, tx, ty, DP_MONTH);
        break;
    }
    case 'd': put_digits(c->nsi, f->day, count, tx, ty, DP_DAY); break;
    case 'E': case 'e': case 'c': {
        if (ch != 'E' && count < 3) {
            /* the local day of the week: Monday first where the week starts on Monday (not
               reached by Intl's skeletons); Sunday first here */
            put_digits(c->nsi, f->wday + 1, 1, tx, ty, DP_WEEKDAY);
            break;
        }
        uint32_t ctx = ch == 'c';
        uint32_t w = 0;
        if (count == 5) w = 1;
        else if (count == 4) w = 3;
        else if (count == 6) w = 2;
        put_name(c->li, RKEY(IDK_DAYS, ctx * 4 + w), (uint32_t)f->wday, tx, ty, DP_WEEKDAY);
        break;
    }
    case 'a': {
        uint32_t w = 0;
        if (count == 4) w = 2;
        else if (count == 5) w = 1;
        uint32_t pm = f->hour >= 12;
        put_name(c->li, RKEY(IDK_DAYPERIODS, w), pm, tx, ty, DP_DAYPERIOD);
        break;
    }
    case 'b':
        if (f->hour == 12 && (!has_m || f->minute == 0) && (!has_s || f->second == 0) &&
            put_name(c->li, RKEY(IDK_DAYPERIODS, dp_width(count)), DPN_NOON, tx, ty, DP_DAYPERIOD)) break;
        dt_field(c, 'a', count, f, has_m, has_s, tx, ty);
        break;
    case 'B': {
        if (!c->has_rules) { dt_field(c, 'a', count, f, has_m, has_s, tx, ty); break; }
        int minute = 0, second = 0;
        if (has_m) minute = f->minute;
        if (has_s) second = f->second;
        int period;
        if (f->hour == 0 && minute == 0 && second == 0 && c->has_midnight) period = DPN_MIDNIGHT;
        else if (f->hour == 12 && minute == 0 && second == 0 && c->has_noon) period = DPN_NOON;
        else period = c->hours[f->hour];
        uint32_t key = RKEY(IDK_DAYPERIODS, dp_width(count));
        if (period != DPN_AM && period != DPN_PM && period != DPN_MIDNIGHT && period >= 0 && put_name(c->li, key, (uint32_t)period, tx, ty, DP_DAYPERIOD)) break;
        if (period == DPN_MIDNIGHT || period == DPN_NOON) {
            period = c->hours[f->hour];
            if (period != DPN_AM && period != DPN_PM && period >= 0 && put_name(c->li, key, (uint32_t)period, tx, ty, DP_DAYPERIOD)) break;
        }
        dt_field(c, 'a', count, f, has_m, has_s, tx, ty);
        break;
    }
    case 'h': {
        int h = f->hour % 12;
        if (h == 0) h = 12;
        put_digits(c->nsi, h, count, tx, ty, DP_HOUR);
        break;
    }
    case 'H': put_digits(c->nsi, f->hour, count, tx, ty, DP_HOUR); break;
    case 'K': put_digits(c->nsi, f->hour % 12, count, tx, ty, DP_HOUR); break;
    case 'k': {
        int h = f->hour;
        if (h == 0) h = 24;
        put_digits(c->nsi, h, count, tx, ty, DP_HOUR);
        break;
    }
    case 'm': put_digits(c->nsi, f->minute, count, tx, ty, DP_MINUTE); break;
    case 's': put_digits(c->nsi, f->second, count, tx, ty, DP_SECOND); break;
    case 'S': {
        int v = f->ms, mind = count;
        if (mind > 3) mind = 3;
        if (count == 1) v /= 100;
        else if (count == 2) v /= 10;
        put_digits(c->nsi, v, mind, tx, ty, DP_FRACTION);
        if (count > 3) put_digits(c->nsi, 0, count - 3, tx, ty, DP_FRACTION);
        break;
    }
    case 'z': case 'v': case 'O': case 'Z': case 'X': case 'x': case 'V': {
        int is_long = count >= 4;
        if (ch == 'v' && count != 1 && count != 4) break;
        if (ch == 'O' && count != 1 && count != 4) break;
        if ((ch == 'z' || ch == 'v') && c->tzkind == TZK_ZONE) {
            vector<uint16_t> nm;
            if (ch == 'z') tz_specific(c, f, is_long, &nm);
            else tz_generic(c, f, is_long, &nm);
            if (len(nm)) { put_u(tx, ty, data(nm), (uint32_t)len(nm), DP_TZ); break; }
        } else if (ch == 'z' || ch == 'v') {
            uint32_t rec = rec_get(c->li, RKEY(IDK_TZ, 0));
            uint16_t b[96];
            uint32_t n = 0;
            if (c->tzkind == TZK_UTC && ch == 'z') {
                if (is_long) n = fld_units(rec, 3, b, 96);
                else n = fld_units(rec, 4, b, 96);
            } else if (c->tzkind == TZK_GMT) {
                if (is_long) n = fld_units(rec, 5, b, 96);
                else n = fld_units(rec, 6, b, 96);
            }
            if (n) { put_u(tx, ty, b, n, DP_TZ); break; }
        }
        tz_gmt(c->li, c->nsi, f->raw + f->dst, !is_long, tx, ty, DP_TZ);
        break;
    }
    default: break;
    }
}

/* a pattern applied to a time's fields (SimpleDateFormat::format): its units and their
   parts' types */
static void dt_render(const dctx *c, const uint16_t *pat, uint32_t pn, const dfields *f, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    int has_m = 0, has_s = 0, q = 0;
    for (uint32_t i = 0; i < pn; i++) {
        if (pat[i] == '\'') q = !q;
        else if (!q && pat[i] == 'm') has_m = 1;
        else if (!q && pat[i] == 's') has_s = 1;
    }
    int in_quote = 0, count = 0;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < pn; i++) {
        uint32_t ch = pat[i];
        if (ch != prev && count > 0) {
            dt_field(c, prev, count, f, has_m, has_s, tx, ty);
            count = 0;
        }
        if (ch == '\'') {
            if (i + 1 < pn && pat[i + 1] == '\'') { put_c(tx, ty, '\'', DP_LITERAL); i++; }
            else in_quote = !in_quote;
        } else if (!in_quote && is_letter16(ch)) {
            prev = ch;
            count++;
        } else put_c(tx, ty, ch, DP_LITERAL);
    }
    if (count > 0) dt_field(c, prev, count, f, has_m, has_s, tx, ty);
}

/* ---- Intl.DateTimeFormat: its options (V8's CreateDateTimeFormat) ---- */

/* the slots */
enum { DFS_LOCALE, DFS_NU, DFS_NSI, DFS_TZ, DFS_TZKIND, DFS_OFFSET, DFS_HC, DFS_DATESTYLE, DFS_TIMESTYLE, DFS_PATTERN,
       DFS_DATALOC, DFS_BOUND, DFS_HOURCHAR, DFS_ZONE, DFS_TZCANON, DFS_TZREGION, DFS_N };
enum { HC_H11, HC_H12, HC_H23, HC_H24 };
static const char *const hc_names[] = { "h11", "h12", "h23", "h24", 0 };
static const char hc_letters[] = "KhHk";
static const char *const style_names[] = { "full", "long", "medium", "short", 0 };
/* what toLocaleString and its kin require and default (ECMA-402's required, defaults) */
enum { DQ_ANY, DQ_DATE, DQ_TIME };
enum { DD_DATE, DD_TIME, DD_ALL };

static int hc_of_letter(uint32_t c) {
    if (c == 'K') return HC_H11;
    if (c == 'h') return HC_H12;
    if (c == 'k') return HC_H24;
    return HC_H23;
}
/* HourCycleFromPattern: the first unquoted hour letter's cycle, -1 for none */
static int hc_of_pattern(const uint16_t *p, uint32_t n) {
    int q = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = p[i];
        if (c == '\'') q = !q;
        else if (!q && (c == 'K' || c == 'h' || c == 'H' || c == 'k')) return hc_of_letter(c);
    }
    return -1;
}
/* ReplaceHourCycleInPattern: every hour letter the cycle's (a space before one after a 'd') */
static void hc_replace(vector<uint16_t> *p, int hc) {
    if (hc < 0) return;
    vector<uint16_t> r;
    int rep = 1;
    uint32_t last = 0;
    for (size_t i = 0; i < len(*p); i++) {
        uint32_t ch = (*p)[i];
        if (ch == '\'') { rep = !rep; push_back(r, (uint16_t)ch); }
        else if (ch == 'H' || ch == 'h' || ch == 'K' || ch == 'k') {
            if (rep && last == 'd') push_back(r, (uint16_t)' ');
            if (rep) push_back(r, (uint16_t)hc_letters[hc]);
            else push_back(r, (uint16_t)ch);
        } else push_back(r, (uint16_t)ch);
        last = ch;
    }
    clear(*p);
    for (size_t i = 0; i < len(r); i++) push_back(*p, r[i]);
}

/* the option components: V8's table, by property: its values' skeletons */
typedef struct dcomp { const char *prop; const char *values[7]; const char *skel[6]; } dcomp;
static const dcomp dt_comps[] = {
    { "weekday", { "narrow", "long", "short", 0 }, { "EEEEE", "EEEE", "EEE" } },
    { "era", { "narrow", "long", "short", 0 }, { "GGGGG", "GGGG", "GGG" } },
    { "year", { "2-digit", "numeric", 0 }, { "yy", "y" } },
    { "month", { "narrow", "long", "short", "2-digit", "numeric", 0 }, { "MMMMM", "MMMM", "MMM", "MM", "M" } },
    { "day", { "2-digit", "numeric", 0 }, { "dd", "d" } },
    { "dayPeriod", { "narrow", "long", "short", 0 }, { "BBBBB", "BBBB", "B" } },
    { "hour", { "2-digit", "numeric", 0 }, { "", "" } },
    { "minute", { "2-digit", "numeric", 0 }, { "mm", "m" } },
    { "second", { "2-digit", "numeric", 0 }, { "ss", "s" } },
    { "timeZoneName", { "short", "long", "shortOffset", "longOffset", "shortGeneric", "longGeneric", 0 }, { "z", "zzzz", "O", "OOOO", "v", "vvvv" } } };
enum { DC_WEEKDAY, DC_ERA, DC_YEAR, DC_MONTH, DC_DAY, DC_DAYPERIOD, DC_HOUR, DC_MINUTE, DC_SECOND, DC_TZNAME, DC_N, DC_FSD = DC_N };

/* the pattern of a date style, a time style or both (DateTimeStylePattern) */
static void style_pattern(dgen *g, int ds, int ts, int hc, vector<uint16_t> *out) {
    uint16_t b[200];
    if (ds >= 0 && ts < 0) {
        uint32_t n = fld_units(rec_get(g->li, RKEY(IDK_DATEFMT, 0)), (uint32_t)ds, b, 200);
        for (uint32_t i = 0; i < n; i++) push_back(*out, b[i]);
        return;
    }
    vector<uint16_t> tp, dpt;
    uint32_t n = fld_units(rec_get(g->li, RKEY(IDK_TIMEFMT, 0)), (uint32_t)ts, b, 200);
    for (uint32_t i = 0; i < n; i++) push_back(tp, b[i]);
    if (ds >= 0) {
        n = fld_units(rec_get(g->li, RKEY(IDK_DATEFMT, 0)), (uint32_t)ds, b, 200);
        for (uint32_t i = 0; i < n; i++) push_back(dpt, b[i]);
        uint32_t glue = rec_get(g->li, RKEY(IDK_DTGLUE, 1));
        if (!glue) glue = rec_get(g->li, RKEY(IDK_DTGLUE, 0));
        n = fld_units(glue, (uint32_t)ds, b, 200);
        simple_format(b, n, &tp, &dpt, 0, out);
    } else for (size_t i = 0; i < len(tp); i++) push_back(*out, tp[i]);
    if (hc == hc_of_pattern(data(*out), (uint32_t)len(*out))) return;
    /* the cycle differs: the skeleton's hour letters the cycle's, its day periods dropped */
    dskel s;
    dskel_set(data(*out), (uint32_t)len(*out), &s);
    vector<uint16_t> sk, sk2;
    dskel_text(&s, &sk);
    for (size_t i = 0; i < len(sk); i++) {
        uint32_t c = sk[i];
        if (c == 'a' || c == 'b' || c == 'B') continue;
        if (c == 'h' || c == 'H' || c == 'K' || c == 'k') c = (uint32_t)hc_letters[hc];
        push_back(sk2, (uint16_t)c);
    }
    clear(*out);
    dp_best_pattern(g, data(sk2), (uint32_t)len(sk2), DTPG_MATCH_HOUR, out);
    hc_replace(out, hc);
}

/* a -u- keyword kept: its value, valid for its key (ca the Gregorian alone, nu a numeric
   numbering system, hc a cycle), else empty */
static void dt_keyword(const itag *t, const char *key, char *out, size_t cap) {
    out[0] = 0;
    int kw = find_kw(t, key);
    if (kw < 0) return;
    const char *v = t->val[kw];
    int ok = 0;
    if (!strcmp(key, "ca")) ok = !strcmp(v, "gregory");
    else if (!strcmp(key, "nu")) ok = strlen(v) < 16 && numbering_index(v) >= 0;
    else for (int i = 0; hc_names[i]; i++) if (!strcmp(v, hc_names[i])) ok = 1;
    if (ok) copyz(out, cap, v);
}

static jv dtf_new(js_vm *vm, jv locales, jv options, jv newt, int required, int defaults, const char *method) {
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    jv opts = options_object(vm, options, method);
    if (opts == JV_HOLE) return JV_HOLE;
    const char *svc = method;
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    /* calendar, numberingSystem */
    uint32_t cs = 0, nus = 0;
    char ca_opt[16], nu_opt[16];
    ca_opt[0] = 0;
    nu_opt[0] = 0;
    int ca_given = 0, nu_given = 0;
    int r = get_option_str(vm, opts, "calendar", 0, svc, &cs);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[80];
        int n = str_ascii(vm, cs, b, sizeof b);
        if (n < 0 || !is_type(b)) {
            vector<char> m;
            const char *a = "Invalid calendar : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, cs, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        ca_given = 1;
        if (n < 16) copyz(ca_opt, sizeof ca_opt, b);
    }
    r = get_option_str(vm, opts, "numberingSystem", 0, svc, &nus);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[80];
        int n = str_ascii(vm, nus, b, sizeof b);
        if (n < 0 || !is_type(b)) {
            vector<char> m;
            const char *a = "Invalid numberingSystem : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, nus, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        nu_given = 1;
        if (n < 16) copyz(nu_opt, sizeof nu_opt, b);
    }
    int hour12 = get_option_bool(vm, opts, "hour12");
    if (hour12 == -2) return JV_HOLE;
    int hour_cycle = get_option_str(vm, opts, "hourCycle", hc_names, svc, 0);
    if (hour_cycle == -2) return JV_HOLE;
    int hour_cycle_given = hour_cycle >= 0;
    if (hour12 >= 0) hour_cycle = -1;
    /* the locale and its keywords */
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    char kw_ca[16], kw_nu[16], kw_hc[8];
    dt_keyword(&t, "ca", kw_ca, sizeof kw_ca);
    dt_keyword(&t, "nu", kw_nu, sizeof kw_nu);
    dt_keyword(&t, "hc", kw_hc, sizeof kw_hc);
    if (ca_given && kw_ca[0] && strcmp(kw_ca, ca_opt)) kw_ca[0] = 0;
    if (nu_given && kw_nu[0] && strcmp(kw_nu, nu_opt)) kw_nu[0] = 0;
    char nu[16];
    if (nu_given && nu_opt[0] && numbering_index(nu_opt) >= 0 && strcmp(nu_opt, "native") && strcmp(nu_opt, "traditio") && strcmp(nu_opt, "finance"))
        copyz(nu, sizeof nu, nu_opt);
    else if (kw_nu[0]) copyz(nu, sizeof nu, kw_nu);
    else default_numbering(found, nu, sizeof nu);
    int li = locale_index(found);
    if (li < 0) li = locale_index("en");
    int nsi = numbering_index(nu);
    /* the generator's hour letter: the data locale's, its kept hc keyword's */
    itag gt;
    if (tag_from_text(found, strlen(found), &gt)) tag_clear(&gt);
    if (kw_hc[0]) set_kw(&gt, "hc", kw_hc);
    uint32_t hour_char = default_hour_char(&gt);
    int hc_default = hc_of_letter(hour_char);
    int hc = -1;
    if (hour_cycle < 0) {
        if (kw_hc[0]) for (int i = 0; hc_names[i]; i++) if (!strcmp(kw_hc, hc_names[i])) hc = i;
    } else hc = hour_cycle;
    if (hour12 >= 0) {
        if (hour12) {
            if (hc_default == HC_H11 || hc_default == HC_H12) hc = hc_default;
            else if (!strcmp(gt.region, "JP")) hc = HC_H11;
            else hc = HC_H12;
        } else {
            if (hc_default == HC_H23 || hc_default == HC_H24) hc = hc_default;
            else hc = HC_H23;
        }
    } else if (hc < 0) hc = hc_default;
    /* the time zone */
    jv tzv = js_get(vm, opts, js_atom_ascii(vm, "timeZone"));
    if (vm->throwing) return JV_HOLE;
    int tzkind = TZK_UTC, offset = 0, tzzone = -1, tzcanon = -1;
    char tzname[64];
    copyz(tzname, sizeof tzname, "UTC");
    if (tzv != JV_UNDEF) {
        uint32_t ts = js_to_string(vm, tzv);
        if (!ts) return JV_HOLE;
        if (!tz_parse(vm, ts, &tzkind, &offset, &tzzone, &tzcanon, tzname, sizeof tzname)) {
            vector<char> m;
            const char *a = "Invalid time zone specified: ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, ts, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
    }
    /* the components */
    vector<uint16_t> skel;
    int explicit_mask = 0, has_hour = 0;
    for (int i = 0; i < DC_N; i++) {
        if (i == DC_TZNAME) {
            int fsd = number_option(vm, js_get(vm, opts, js_atom_ascii(vm, "fractionalSecondDigits")), 1, 3, 0, "fractionalSecondDigits");
            if (fsd < 0 || vm->throwing) return JV_HOLE;
            if (fsd > 0) explicit_mask |= 1 << DC_FSD;
            for (int k = 0; k < fsd; k++) push_back(skel, (uint16_t)'S');
        }
        int v = get_option_str(vm, opts, dt_comps[i].prop, dt_comps[i].values, svc, 0);
        if (v == -2) return JV_HOLE;
        if (v < 0) continue;
        explicit_mask |= 1 << i;
        if (i == DC_HOUR) {
            has_hour = 1;
            push_back(skel, (uint16_t)hc_letters[hc]);
            if (v == 0) push_back(skel, (uint16_t)hc_letters[hc]);
            continue;
        }
        for (size_t k = 0; dt_comps[i].skel[v][k]; k++) push_back(skel, (uint16_t)dt_comps[i].skel[v][k]);
    }
    static const char *const fm[] = { "basic", "best fit", 0 };
    if (get_option_str(vm, opts, "formatMatcher", fm, svc, 0) == -2) return JV_HOLE;
    int ds = get_option_str(vm, opts, "dateStyle", style_names, svc, 0);
    if (ds == -2) return JV_HOLE;
    int ts = get_option_str(vm, opts, "timeStyle", style_names, svc, 0);
    if (ts == -2) return JV_HOLE;
    int dtf_hc = -1;
    if (ts >= 0) dtf_hc = hc;
    dgen g;
    memset(&g, 0, sizeof g);
    defer(g) { vector_dpent_drop(&g.m.e); vector_uint16_t_drop(&g.m.text); }
    dgen_init(&g, li, hour_char, nsi);
    vector<uint16_t> pat;
    if (ds >= 0 || ts >= 0) {
        if (explicit_mask) return js_throw_msg(vm, vm->type_error_proto, "Invalid option : option");
        if (required == DQ_DATE && ts >= 0) return js_throw_msg(vm, vm->type_error_proto, "Invalid option : timeStyle");
        if (required == DQ_TIME && ds >= 0) return js_throw_msg(vm, vm->type_error_proto, "Invalid option : dateStyle");
        style_pattern(&g, ds, ts, dtf_hc, &pat);
    } else {
        int need = 1;
        int date_bits = (1 << DC_WEEKDAY) | (1 << DC_YEAR) | (1 << DC_MONTH) | (1 << DC_DAY);
        int time_bits = (1 << DC_DAYPERIOD) | (1 << DC_HOUR) | (1 << DC_MINUTE) | (1 << DC_SECOND) | (1 << DC_FSD);
        if ((required == DQ_DATE || required == DQ_ANY) && (explicit_mask & date_bits)) need = 0;
        if ((required == DQ_TIME || required == DQ_ANY) && (explicit_mask & time_bits)) need = 0;
        if (need && (defaults == DD_DATE || defaults == DD_ALL)) { push_back(skel, (uint16_t)'y'); push_back(skel, (uint16_t)'M'); push_back(skel, (uint16_t)'d'); }
        if (need && (defaults == DD_TIME || defaults == DD_ALL)) {
            push_back(skel, (uint16_t)hc_letters[hc]);
            push_back(skel, (uint16_t)'m');
            push_back(skel, (uint16_t)'s');
        }
        if (has_hour) dtf_hc = hc;
        dp_best_pattern(&g, data(skel), (uint32_t)len(skel), DTPG_MATCH_HOUR, &pat);
        hc_replace(&pat, dtf_hc);
    }
    /* hour12 and hourCycle drop a keyword's cycle they do not agree with */
    if ((hour12 >= 0 || hour_cycle_given) && kw_hc[0]) {
        int k = -1;
        for (int i = 0; hc_names[i]; i++) if (!strcmp(kw_hc, hc_names[i])) k = i;
        if (dtf_hc != k) kw_hc[0] = 0;
    }
    char loc[TAG_MAX + 1];
    copyz(loc, sizeof loc, found);
    if (kw_ca[0] || kw_hc[0] || kw_nu[0]) {
        cat(loc, sizeof loc, "-u");
        if (kw_ca[0]) { cat(loc, sizeof loc, "-ca-"); cat(loc, sizeof loc, kw_ca); }
        if (kw_hc[0]) { cat(loc, sizeof loc, "-hc-"); cat(loc, sizeof loc, kw_hc); }
        if (kw_nu[0]) { cat(loc, sizeof loc, "-nu-"); cat(loc, sizeof loc, kw_nu); }
    }
    jv p = JV_UNDEF;
    if (jv_is_obj(newt)) {
        p = js_get(vm, newt, A_PROTOTYPE);
        if (vm->throwing) return JV_HOLE;
    }
    uint32_t pr = vm->intl_protos[JIK_DATETIMEFORMAT];
    if (jv_is_obj(p)) pr = jv_idx(p);
    uint32_t o = intl_new(vm, JIK_DATETIMEFORMAT, pr, DFS_N);
    slot_set(vm, o, DFS_LOCALE, jv_str(js_str_ascii(vm, loc)));
    slot_set(vm, o, DFS_NU, jv_str(js_str_ascii(vm, nu)));
    slot_set(vm, o, DFS_NSI, jv_num(nsi));
    slot_set(vm, o, DFS_TZ, jv_str(js_str_ascii(vm, tzname)));
    slot_set(vm, o, DFS_TZKIND, jv_num(tzkind));
    slot_set(vm, o, DFS_OFFSET, jv_num(offset));
    slot_set(vm, o, DFS_ZONE, jv_num(tzzone));
    slot_set(vm, o, DFS_TZCANON, jv_num(tzcanon));
    int tzregion = 0;
    if (tzkind == TZK_ZONE) {
        char rg[8];
        tz_target_region(li, rg, sizeof rg);
        tzregion = tz_region_code(rg);
    }
    slot_set(vm, o, DFS_TZREGION, jv_num(tzregion));
    slot_set(vm, o, DFS_HC, jv_num(dtf_hc));
    slot_set(vm, o, DFS_DATESTYLE, jv_num(ds));
    slot_set(vm, o, DFS_TIMESTYLE, jv_num(ts));
    slot_set(vm, o, DFS_PATTERN, jv_str(js_str_new(vm, data(pat), (uint32_t)len(pat))));
    slot_set(vm, o, DFS_DATALOC, jv_num(li));
    slot_set(vm, o, DFS_HOURCHAR, jv_num(hour_char));
    return jv_obj(o);
}

/* a DateTimeFormat's pattern into buf: its length */
static uint32_t dtf_pattern(js_vm *vm, uint32_t o, vector<uint16_t> *buf) {
    uint32_t s = jv_idx(slot_get(vm, o, DFS_PATTERN));
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, off = vm->strs[s].off;
    for (uint32_t i = 0; i < n; i++) push_back(*buf, vm->units[off + i]);
    return n;
}
static void dtf_ctx(js_vm *vm, uint32_t o, dctx *c) {
    c->li = slot_int(vm, o, DFS_DATALOC);
    c->nsi = slot_int(vm, o, DFS_NSI);
    c->tzkind = slot_int(vm, o, DFS_TZKIND);
    c->offset = slot_int(vm, o, DFS_OFFSET);
    c->zone = -1;
    c->canon = -1;
    c->region = 0;
    if (c->tzkind == TZK_ZONE) {
        c->zone = slot_int(vm, o, DFS_ZONE);
        c->canon = slot_int(vm, o, DFS_TZCANON);
        c->region = slot_int(vm, o, DFS_TZREGION);
    }
    dp_rules(c);
}

/* whether units hold an ASCII string */
static int units_have(const uint16_t *p, uint32_t n, const char *s) {
    size_t ln = strlen(s);
    for (uint32_t k = 0; k + ln <= n; k++) {
        size_t j = 0;
        while (j < ln && p[k + j] == (uint16_t)(uint8_t)s[j]) j++;
        if (j == ln) return 1;
    }
    return 0;
}

/* resolvedOptions, in V8's order: the components found in the pattern */
static jv dtf_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, DFS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "calendar"), jv_str(js_str_ascii(vm, "gregory")), 0);
    js_define(vm, r, js_atom_ascii(vm, "numberingSystem"), slot_get(vm, o, DFS_NU), 0);
    js_define(vm, r, js_atom_ascii(vm, "timeZone"), slot_get(vm, o, DFS_TZ), 0);
    int hc = slot_int(vm, o, DFS_HC);
    if (hc >= 0) {
        js_define(vm, r, js_atom_ascii(vm, "hourCycle"), jv_str(js_str_ascii(vm, hc_names[hc])), 0);
        jv h12 = JV_FALSE;
        if (hc == HC_H11 || hc == HC_H12) h12 = JV_TRUE;
        js_define(vm, r, js_atom_ascii(vm, "hour12"), h12, 0);
    }
    int ds = slot_int(vm, o, DFS_DATESTYLE), ts = slot_int(vm, o, DFS_TIMESTYLE);
    if (ds < 0 && ts < 0) {
        vector<uint16_t> pat;
        uint32_t pn = dtf_pattern(vm, o, &pat);
        /* V8's pattern items: a property, its patterns and values (longest first), 0 */
        static const char *const items[] = {
            "weekday", "EEEEE", "narrow", "EEEE", "long", "EEE", "short", "ccccc", "narrow", "cccc", "long", "ccc", "short", 0,
            "era", "GGGGG", "narrow", "GGGG", "long", "GGG", "short", 0,
            "year", "yy", "2-digit", "y", "numeric", 0,
            "month", "MMMMM", "narrow", "MMMM", "long", "MMM", "short", "MM", "2-digit", "M", "numeric", "LLLLL", "narrow",
            "LLLL", "long", "LLL", "short", "LL", "2-digit", "L", "numeric", 0,
            "day", "dd", "2-digit", "d", "numeric", 0,
            "dayPeriod", "BBBBB", "narrow", "bbbbb", "narrow", "BBBB", "long", "bbbb", "long", "B", "short", "b", "short", 0,
            "hour", "HH", "2-digit", "H", "numeric", "hh", "2-digit", "h", "numeric", "kk", "2-digit", "k", "numeric", "KK",
            "2-digit", "K", "numeric", 0,
            "minute", "mm", "2-digit", "m", "numeric", 0,
            "second", "ss", "2-digit", "s", "numeric", 0,
            "timeZoneName", "zzzz", "long", "z", "short", "OOOO", "longOffset", "O", "shortOffset", "vvvv", "longGeneric", "v",
            "shortGeneric", 0, 0 };
        uint32_t k = 0;
        while (items[k]) {
            uint32_t name = k++;
            if (!strcmp(items[name], "timeZoneName")) {
                int fsd = 0;
                for (uint32_t i = 0; i < pn && fsd < 3; i++) if (pat[i] == 'S') fsd++;
                if (fsd) js_define(vm, r, js_atom_ascii(vm, "fractionalSecondDigits"), jv_num(fsd), 0);
            }
            int done = 0;
            for (; items[k]; k += 2) {
                if (done || !units_have(data(pat), pn, items[k])) continue;
                js_define(vm, r, js_atom_ascii(vm, items[name]), jv_str(js_str_ascii(vm, items[k + 1])), 0);
                done = 1;
            }
            k++;
        }
    }

    if (ds >= 0) js_define(vm, r, js_atom_ascii(vm, "dateStyle"), jv_str(js_str_ascii(vm, style_names[ds])), 0);
    if (ts >= 0) js_define(vm, r, js_atom_ascii(vm, "timeStyle"), jv_str(js_str_ascii(vm, style_names[ts])), 0);
    return jv_obj(r);
}

/* a time value for formatting: undefined now, else ToNumber, TimeClip; NaN a RangeError */
static int dtf_time(js_vm *vm, jv v, double *t) {
    double x;
    if (v == JV_UNDEF) x = floor(vm->now);
    else {
        x = js_to_number(vm, v);
        if (vm->throwing) return 0;
    }
    if (x != x || fabs(x) > 8.64e15) {
        js_throw_msg(vm, vm->range_error_proto, "Invalid time value");
        return 0;
    }
    *t = trunc(x) + 0.0;
    return 1;
}

/* a time formatted: its units and their parts' types */
static void dtf_render(js_vm *vm, uint32_t o, double t, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    dctx c;
    dtf_ctx(vm, o, &c);
    dfields f;
    dtf_fields(&c, t, &f);
    vector<uint16_t> pat;
    uint32_t pn = dtf_pattern(vm, o, &pat);
    dt_render(&c, data(pat), pn, &f, tx, ty);
}

/* FormatDateTime (U+202F a space, as V8 reverts ICU 72's change) or FormatDateTimeToParts */
static jv dtf_format_value(js_vm *vm, uint32_t o, jv v, int to_parts) {
    double t;
    if (!dtf_time(vm, v, &t)) return JV_HOLE;
    vector<uint16_t> tx;
    vector<uint8_t> ty;
    dtf_render(vm, o, t, &tx, &ty);
    if (!to_parts) {
        for (size_t i = 0; i < len(tx); i++) if (tx[i] == 0x202F) tx[i] = ' ';
        return jv_str(js_str_new(vm, data(tx), (uint32_t)len(tx)));
    }
    vector<uint32_t> pt;
    size_t i = 0;
    while (i < len(tx)) {
        size_t j = i + 1;
        while (j < len(tx) && ty[j] == ty[i]) j++;
        push_back(pt, (uint32_t)i);
        push_back(pt, (uint32_t)j);
        push_back(pt, (uint32_t)ty[i]);
        i = j;
    }
    return parts_array(vm, &tx, &pt, dp_names, 0);
}

/* ---- formatRange: ICU's DateIntervalFormat ---- */

/* the interval patterns' indexes (DateIntervalInfo's), their letters and levels */
enum { IPI_ERA, IPI_YEAR, IPI_MONTH, IPI_DATE, IPI_AM_PM, IPI_HOUR, IPI_MINUTE, IPI_SECOND, IPI_MS, IPI_N };
static const char ipi_letters[] = "GyMdahmsS";
static const int ipi_levels[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80 };

/* SimpleDateFormat::getLevelFromChar */
static int letter_level(uint32_t c) {
    static const signed char up[] = { 40, -1, -1, 20, 30, 30, 0, 50, -1, -1, 50, 20, 20, -1, 0, -1, 20, -1, 80, -1, 10, 0, 30, 0, 10, 0 };
    static const signed char lo[] = { 40, -1, 30, 30, 30, -1, 0, 50, -1, -1, 50, 0, 60, -1, -1, -1, 20, 10, 70, -1, 10, 0, 20, 0, 10, 0 };
    if (c >= 'A' && c <= 'Z') return up[c - 'A'];
    if (c >= 'a' && c <= 'z') return lo[c - 'a'];
    return -1;
}
/* SimpleDateFormat::isFieldUnitIgnored: no field of the pattern at the level or below */
static int unit_ignored(const uint16_t *p, uint32_t n, int ipi) {
    int q = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = p[i];
        if (c == '\'') {
            if (i + 1 < n && p[i + 1] == '\'') i++;
            else q = !q;
        } else if (!q && is_letter16(c) && ipi_levels[ipi] <= letter_level(c)) return 0;
    }
    return 1;
}

#define DI_MAX 200
typedef struct dipart { uint16_t first[DI_MAX], second[DI_MAX]; uint32_t nf, ns; int later; } dipart;
/* the interval formatter: its data, fallback, date-time glue, single-date pattern, the
   date and time patterns of a fallback, its skeleton, its patterns by field */
typedef struct difmt {
    uint32_t rec;
    int later_default;
    uint16_t fallback[48], glue[48], datefmt[DI_MAX], datep[DI_MAX], timep[DI_MAX], skel[64];
    uint32_t nfallback, nglue, ndatefmt, ndatep, ntimep, nskel;
    int has_datep, has_timep;
    dipart ip[IPI_N];
} difmt;

static uint32_t vec_to(vector<uint16_t> *v, uint16_t *buf, uint32_t cap) {
    uint32_t n = (uint32_t)len(*v);
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; i++) buf[i] = (*v)[i];
    return n;
}
/* DateFormat::getBestPattern (no hour matching) into a buffer */
static uint32_t di_best_pattern(dgen *g, const uint16_t *sk, uint32_t sn, uint16_t *buf, uint32_t cap) {
    vector<uint16_t> out;
    dp_best_pattern(g, sk, sn, 0, &out);
    return vec_to(&out, buf, cap);
}

/* the interval data: field 0 the fallback, then each skeleton and its patterns, each its
   letter, \x1d and the pattern, joined by \x1e */
static int di_find(uint32_t rec, const uint16_t *sk, uint32_t n) {
    for (int i = 0;; i++) {
        uint16_t b[64];
        uint32_t bn = fld_units(rec, (uint32_t)(1 + 2 * i), b, 64);
        if (!bn) return -1;
        if (bn != n) continue;
        uint32_t k = 0;
        while (k < n && b[k] == sk[k]) k++;
        if (k == n) return i;
    }
}
static int di_letter_index(uint32_t c) {
    if (c == 'G') return IPI_ERA;
    if (c == 'y') return IPI_YEAR;
    if (c == 'M') return IPI_MONTH;
    if (c == 'd') return IPI_DATE;
    if (c == 'a' || c == 'B') return IPI_AM_PM;
    if (c == 'h' || c == 'H') return IPI_HOUR;
    if (c == 'm') return IPI_MINUTE;
    return -1;
}
/* getIntervalPattern: a skeleton's pattern for a field (the first letter's mapping to it,
   as ICU keeps the first), 0 for none */
static uint32_t di_pattern(uint32_t rec, int si, int ipi, uint16_t *buf, uint32_t cap) {
    if (si < 0) return 0;
    uint16_t b[2048];
    uint32_t n = fld_units(rec, (uint32_t)(2 + 2 * si), b, 2048);
    uint32_t i = 0;
    while (i < n) {
        uint32_t j = i;
        while (j < n && b[j] != 0x1E) j++;
        if (i + 2 <= j && b[i + 1] == 0x1D && di_letter_index(b[i]) == ipi) {
            uint32_t k = 0;
            for (uint32_t u = i + 2; u < j && k < cap; u++) buf[k++] = b[u];
            return k;
        }
        i = j + 1;
    }
    return 0;
}
static uint32_t di_pattern_of(uint32_t rec, const uint16_t *sk, uint32_t sn, int ipi, uint16_t *buf, uint32_t cap) {
    return di_pattern(rec, di_find(rec, sk, sn), ipi, buf, cap);
}
static void sk_widths(const uint16_t *s, uint32_t n, int *w) {
    for (uint32_t i = 0; i < 58; i++) w[i] = 0;
    for (uint32_t i = 0; i < n; i++) if (s[i] >= 'A' && s[i] <= 'z') w[s[i] - 'A']++;
}
/* getBestSkeleton: the nearest skeleton's index into best, -1 none; *diff 0 the same, 1
   widths differ, 2 only z/v, h/K, H/k (or a day period) differ, -1 other fields */
static int di_best(uint32_t rec, const uint16_t *sk, uint32_t n, int *diff, uint16_t *best, uint32_t *bn) {
    uint16_t cp[64];
    uint32_t cn = 0;
    int replaced = 0;
    for (uint32_t i = 0; i < n && cn < 64; i++) {
        uint32_t c = sk[i];
        if (c == 'z' || c == 'k' || c == 'K' || c == 'a' || c == 'b') replaced = 1;
        if (c == 'z') c = 'v';
        else if (c == 'k') c = 'H';
        else if (c == 'K') c = 'h';
        else if (c == 'a' || c == 'b') continue;
        cp[cn++] = (uint16_t)c;
    }
    int inw[58], w[58];
    sk_widths(cp, cn, inw);
    int bi = -1, bd = 0x7FFFFFFF;
    *diff = 0;
    for (int i = 0;; i++) {
        uint16_t b[64];
        uint32_t m = fld_units(rec, (uint32_t)(1 + 2 * i), b, 64);
        if (!m) break;
        sk_widths(b, m, w);
        int d = 0, fd = 1;
        for (int k = 0; k < 58; k++) {
            int a = inw[k], c = w[k];
            if (a == c) continue;
            if (a == 0 || c == 0) { fd = -1; d += 0x1000; }
            else if (k + 'A' == 'M' && ((a <= 2 && c > 2) || (a > 2 && c <= 2))) d += 0x100;
            else if (a > c) d += a - c;
            else d += c - a;
        }
        if (d < bd) {
            bi = i;
            bd = d;
            *diff = fd;
            for (uint32_t k = 0; k < m; k++) best[k] = b[k];
            *bn = m;
        }
        if (d == 0) { *diff = 0; break; }
    }
    if (replaced && *diff != -1) *diff = 2;
    return bi;
}

/* splitPatternInto2Part: where the first repeated letter begins */
static uint32_t di_split(const uint16_t *p, uint32_t n) {
    int rep[58];
    for (int k = 0; k < 58; k++) rep[k] = 0;
    int q = 0, count = 0, found = 0;
    uint32_t prev = 0, i;
    for (i = 0; i < n; ++i) {
        uint32_t ch = p[i];
        if (ch != prev && count > 0) {
            if (!rep[prev - 'A']) rep[prev - 'A'] = 1;
            else { found = 1; break; }
            count = 0;
        }
        if (ch == '\'') {
            if (i + 1 < n && p[i + 1] == '\'') ++i;
            else q = !q;
        } else if (!q && is_letter16(ch)) { prev = ch; ++count; }
    }
    if (count > 0 && !found && !rep[prev - 'A']) count = 0;
    return i - (uint32_t)count;
}

/* setPatternInfo: the parts given (a null one kept), the order */
static void di_info(difmt *d, int ipi, const uint16_t *first, uint32_t nf, const uint16_t *second, uint32_t ns, int later) {
    dipart *pp = &d->ip[ipi];
    if (first) { for (uint32_t i = 0; i < nf && i < DI_MAX; i++) pp->first[i] = first[i]; pp->nf = nf; }
    if (second) { for (uint32_t i = 0; i < ns && i < DI_MAX; i++) pp->second[i] = second[i]; pp->ns = ns; }
    pp->later = later;
}
/* setIntervalPattern(field, pattern, order): a "latestFirst:" or "earliestFirst:" prefix
   read, the pattern split in two */
static void di_set(difmt *d, int ipi, const uint16_t *p, uint32_t n, int later) {
    static const char lf[] = "latestFirst:", ef[] = "earliestFirst:";
    uint32_t k = 0, off = 0;
    while (k < 12 && k < n && p[k] == (uint16_t)lf[k]) k++;
    if (k == 12) { off = 12; later = 1; }
    else {
        k = 0;
        while (k < 14 && k < n && p[k] == (uint16_t)ef[k]) k++;
        if (k == 14) { off = 14; later = 0; }
    }
    uint32_t at = di_split(p + off, n - off);
    di_info(d, ipi, p + off, at, p + off + at, n - off - at, later);
}

/* findReplaceInPattern of one letter: its unquoted occurrences replaced */
static void di_replace(vector<uint16_t> *p, uint32_t from, uint32_t to) {
    int q = 0;
    for (size_t i = 0; i < len(*p); i++) {
        if ((*p)[i] == '\'') q = !q;
        else if (!q && (*p)[i] == from) (*p)[i] = (uint16_t)to;
    }
}
static int units_has_char(const uint16_t *p, uint32_t n, uint32_t c) {
    for (uint32_t i = 0; i < n; i++) if (p[i] == c) return 1;
    return 0;
}
/* adjustFieldWidth: the pattern's fields widened to the input skeleton's */
static void di_adjust_width(const uint16_t *in, uint32_t inn, const uint16_t *best, uint32_t bn, const uint16_t *pat, uint32_t pn, int diff, vector<uint16_t> *out) {
    vector<uint16_t> a;
    for (uint32_t i = 0; i < pn; i++) push_back(a, pat[i]);
    int inw[58], bw[58];
    sk_widths(in, inn, inw);
    sk_widths(best, bn, bw);
    if (diff == 2) {
        if (units_has_char(in, inn, 'z')) di_replace(&a, 'v', 'z');
        if (units_has_char(in, inn, 'K')) di_replace(&a, 'h', 'K');
        if (units_has_char(in, inn, 'k')) di_replace(&a, 'H', 'k');
        if (units_has_char(in, inn, 'b')) di_replace(&a, 'a', 'b');
    }
    if (units_has_char(data(a), (uint32_t)len(a), 'a') && bw['a' - 'A'] == 0) bw['a' - 'A'] = 1;
    if (units_has_char(data(a), (uint32_t)len(a), 'b') && bw['b' - 'A'] == 0) bw['b' - 'A'] = 1;
    int q = 0, count = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < len(a); i++) {
        uint32_t ch = a[i];
        if (ch != prev && count > 0) {
            uint32_t sc = prev;
            if (sc == 'L') sc = 'M';
            int fc = bw[sc - 'A'], ic = inw[sc - 'A'];
            if (fc == count && ic > fc) {
                for (int k = 0; k < ic - fc; k++) push_back(*out, (uint16_t)prev);
            }
            count = 0;
        }
        push_back(*out, (uint16_t)ch);
        if (ch == '\'') {
            if (i + 1 < len(a) && a[i + 1] == '\'') { push_back(*out, a[i + 1]); i++; }
            else q = !q;
        } else if (!q && is_letter16(ch)) { prev = ch; ++count; }
    }
    if (count > 0) {
        uint32_t sc = prev;
        if (sc == 'L') sc = 'M';
        int fc = bw[sc - 'A'], ic = inw[sc - 'A'];
        if (fc == count && ic > fc) for (int k = 0; k < ic - fc; k++) push_back(*out, (uint16_t)prev);
    }
}

/* setIntervalPattern(field, skeleton, best, diff, extended...): whether the pattern was
   found by extending the skeleton */
static int di_set_interval(difmt *d, int ipi, const uint16_t *sk, uint32_t sn, const uint16_t *best0, uint32_t bn0, int diff,
                           int extend, uint16_t *ext, uint32_t *ext_n, uint16_t *ext_best, uint32_t *ext_bn) {
    uint16_t pat[DI_MAX], best[64];
    uint32_t bn = bn0;
    for (uint32_t i = 0; i < bn; i++) best[i] = best0[i];
    uint32_t pn = di_pattern_of(d->rec, best, bn, ipi, pat, DI_MAX);
    if (!pn) {
        if (unit_ignored(best, bn, ipi)) return 0;
        if (ipi == IPI_AM_PM) {
            pn = di_pattern_of(d->rec, best, bn, IPI_HOUR, pat, DI_MAX);
            if (pn) {
                vector<uint16_t> adj;
                di_adjust_width(sk, sn, best, bn, pat, pn, diff, &adj);
                di_set(d, ipi, data(adj), (uint32_t)len(adj), d->later_default);
            }
            return 0;
        }
        if (extend) {
            ext[0] = (uint16_t)ipi_letters[ipi];
            for (uint32_t i = 0; i < sn; i++) ext[i + 1] = sk[i];
            *ext_n = sn + 1;
            ext_best[0] = (uint16_t)ipi_letters[ipi];
            for (uint32_t i = 0; i < bn; i++) ext_best[i + 1] = best[i];
            *ext_bn = bn + 1;
            pn = di_pattern_of(d->rec, ext_best, *ext_bn, ipi, pat, DI_MAX);
            if (!pn && diff == 0) {
                uint16_t tb[64];
                uint32_t tn = 0;
                int ti = di_best(d->rec, ext_best, *ext_bn, &diff, tb, &tn);
                if (ti >= 0 && diff != -1) {
                    pn = di_pattern(d->rec, ti, ipi, pat, DI_MAX);
                    for (uint32_t i = 0; i < tn; i++) best[i] = tb[i];
                    bn = tn;
                }
            }
        }
    }
    if (pn) {
        if (diff != 0) {
            vector<uint16_t> adj;
            di_adjust_width(sk, sn, best, bn, pat, pn, diff, &adj);
            di_set(d, ipi, data(adj), (uint32_t)len(adj), d->later_default);
        } else di_set(d, ipi, pat, pn, d->later_default);
        if (extend && *ext_n) return 1;
    }
    return 0;
}

/* normalizeHourMetacharacters: the hour field and its day period as the locale's single
   hour pattern has them */
static void di_normalize_hour(dgen *g, const uint16_t *sk, uint32_t n, vector<uint16_t> *out) {
    uint32_t hc = 0, dc = 0;
    uint32_t hs = 0, hl = 0, ds = 0, dl = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = sk[i];
        if (c == 'j' || c == 'J' || c == 'C' || c == 'h' || c == 'H' || c == 'k' || c == 'K') {
            if (!hc) { hc = c; hs = i; }
            hl++;
        } else if (c == 'a' || c == 'b' || c == 'B') {
            if (!dc) { dc = c; ds = i; }
            dl++;
        } else if (hc && dc) break;
    }
    for (uint32_t i = 0; i < n; i++) push_back(*out, sk[i]);
    if (!hc) return;
    uint16_t one = (uint16_t)hc;
    uint16_t cpat[DI_MAX];
    uint32_t cn = di_best_pattern(g, &one, 1, cpat, DI_MAX);
    /* the pattern's literals stripped */
    uint16_t lit[DI_MAX];
    uint32_t ln = 0;
    int q = 0;
    for (uint32_t i = 0; i < cn; i++) {
        if (cpat[i] == '\'') { q = !q; continue; }
        if (!q) lit[ln++] = cpat[i];
    }
    uint32_t hour = 'H';
    if (units_has_char(lit, ln, 'h')) hour = 'h';
    else if (units_has_char(lit, ln, 'K')) hour = 'K';
    else if (units_has_char(lit, ln, 'k')) hour = 'k';
    if (units_has_char(lit, ln, 'b')) dc = 'b';
    else if (units_has_char(lit, ln, 'B')) dc = 'B';
    else if (!dc) dc = 'a';
    vector<uint16_t> hd;
    push_back(hd, (uint16_t)hour);
    if (hour != 'H' && hour != 'k') {
        int nl = 1;
        if (dl >= 5 || hl >= 5) nl = 5;
        else if (dl >= 3 || hl >= 3) nl = 3;
        for (int i = 0; i < nl; i++) push_back(hd, (uint16_t)dc);
    }
    vector<uint16_t> r;
    for (uint32_t i = 0; i < hs; i++) push_back(r, sk[i]);
    for (size_t i = 0; i < len(hd); i++) push_back(r, hd[i]);
    for (uint32_t i = hs + hl; i < n; i++) push_back(r, sk[i]);
    if (ds > hs) ds = ds + (uint32_t)len(hd) - hl;
    clear(*out);
    for (size_t i = 0; i < len(r); i++) if (i < ds || i >= ds + dl) push_back(*out, r[i]);
}

/* getDateTimeSkeleton: the date and time skeletons and their normal forms */
static void di_split_skeleton(const uint16_t *sk, uint32_t n, vector<uint16_t> *date, vector<uint16_t> *ndate, vector<uint16_t> *time, vector<uint16_t> *ntime) {
    int ec = 0, dc = 0, mc = 0, yc = 0, minc = 0, vc = 0, zc = 0;
    uint32_t hour = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ch = sk[i];
        switch (ch) {
        case 'E': push_back(*date, (uint16_t)ch); ec++; break;
        case 'd': push_back(*date, (uint16_t)ch); dc++; break;
        case 'M': push_back(*date, (uint16_t)ch); mc++; break;
        case 'y': push_back(*date, (uint16_t)ch); yc++; break;
        case 'G': case 'Y': case 'u': case 'Q': case 'q': case 'L': case 'l': case 'W': case 'w': case 'D': case 'F': case 'g':
        case 'e': case 'c': case 'U': case 'r':
            push_back(*ndate, (uint16_t)ch);
            push_back(*date, (uint16_t)ch);
            break;
        case 'h': case 'H': case 'k': case 'K':
            push_back(*time, (uint16_t)ch);
            if (!hour) hour = ch;
            break;
        case 'm': push_back(*time, (uint16_t)ch); minc++; break;
        case 'z': zc++; push_back(*time, (uint16_t)ch); break;
        case 'v': vc++; push_back(*time, (uint16_t)ch); break;
        case 'a': case 'V': case 'Z': case 'j': case 's': case 'S': case 'A': case 'b': case 'B':
            push_back(*time, (uint16_t)ch);
            push_back(*ntime, (uint16_t)ch);
            break;
        default: break;
        }
    }
    for (int i = 0; i < yc; i++) push_back(*ndate, (uint16_t)'y');
    if (mc) {
        if (mc < 3) push_back(*ndate, (uint16_t)'M');
        else for (int j = 0; j < mc && j < 5; j++) push_back(*ndate, (uint16_t)'M');
    }
    if (ec) {
        if (ec <= 3) push_back(*ndate, (uint16_t)'E');
        else for (int j = 0; j < ec && j < 5; j++) push_back(*ndate, (uint16_t)'E');
    }
    if (dc) push_back(*ndate, (uint16_t)'d');
    if (hour) push_back(*ntime, (uint16_t)hour);
    if (minc) push_back(*ntime, (uint16_t)'m');
    if (zc) push_back(*ntime, (uint16_t)'z');
    if (vc) push_back(*ntime, (uint16_t)'v');
}

/* setSeparateDateTimePtn */
static int di_separate(dgen *g, difmt *d, vector<uint16_t> *ndate, vector<uint16_t> *ntime) {
    uint16_t sk[64], best[64];
    uint32_t sn, bn = 0;
    if (len(*ntime)) sn = vec_to(ntime, sk, 64);
    else sn = vec_to(ndate, sk, 64);
    int diff = 0;
    int bi = di_best(d->rec, sk, sn, &diff, best, &bn);
    if (bi < 0) return 0;
    if (len(*ndate)) { d->ndatep = di_best_pattern(g, data(*ndate), (uint32_t)len(*ndate), d->datep, DI_MAX); d->has_datep = 1; }
    if (len(*ntime)) { d->ntimep = di_best_pattern(g, data(*ntime), (uint32_t)len(*ntime), d->timep, DI_MAX); d->has_timep = 1; }
    if (diff == -1) return 0;
    uint16_t ext[64], ext_best[64];
    uint32_t ext_n = 0, ext_bn = 0;
    if (!len(*ntime)) {
        di_set_interval(d, IPI_DATE, sk, sn, best, bn, diff, 1, ext, &ext_n, ext_best, &ext_bn);
        if (di_set_interval(d, IPI_MONTH, sk, sn, best, bn, diff, 1, ext, &ext_n, ext_best, &ext_bn)) {
            for (uint32_t i = 0; i < ext_bn; i++) best[i] = ext_best[i];
            bn = ext_bn;
            for (uint32_t i = 0; i < ext_n; i++) sk[i] = ext[i];
            sn = ext_n;
        }
        di_set_interval(d, IPI_YEAR, sk, sn, best, bn, diff, 1, ext, &ext_n, ext_best, &ext_bn);
        di_set_interval(d, IPI_ERA, sk, sn, best, bn, diff, 1, ext, &ext_n, ext_best, &ext_bn);
    } else {
        di_set_interval(d, IPI_MINUTE, sk, sn, best, bn, diff, 0, ext, &ext_n, ext_best, &ext_bn);
        di_set_interval(d, IPI_HOUR, sk, sn, best, bn, diff, 0, ext, &ext_n, ext_best, &ext_bn);
        di_set_interval(d, IPI_AM_PM, sk, sn, best, bn, diff, 0, ext, &ext_n, ext_best, &ext_bn);
    }
    return 1;
}

/* the fallbacks of a time skeleton without a date: yMd's pattern for the date's fields,
   GyMd's for the era */
static void di_time_fallbacks(dgen *g, difmt *d, vector<uint16_t> *time) {
    vector<uint16_t> s;
    push_back(s, (uint16_t)'y');
    push_back(s, (uint16_t)'M');
    push_back(s, (uint16_t)'d');
    for (size_t i = 0; i < len(*time); i++) push_back(s, (*time)[i]);
    uint16_t pat[DI_MAX];
    uint32_t pn = di_best_pattern(g, data(s), (uint32_t)len(s), pat, DI_MAX);
    di_info(d, IPI_DATE, 0, 0, pat, pn, d->later_default);
    di_info(d, IPI_MONTH, 0, 0, pat, pn, d->later_default);
    di_info(d, IPI_YEAR, 0, 0, pat, pn, d->later_default);
    vector<uint16_t> s2;
    push_back(s2, (uint16_t)'G');
    for (size_t i = 0; i < len(s); i++) push_back(s2, s[i]);
    pn = di_best_pattern(g, data(s2), (uint32_t)len(s2), pat, DI_MAX);
    di_info(d, IPI_ERA, 0, 0, pat, pn, d->later_default);
}

/* DateIntervalFormat's construction (createInstance, initializePattern) for a skeleton */
static void di_init(dgen *g, difmt *d, int li, const uint16_t *skel, uint32_t sn) {
    memset(d, 0, sizeof *d);
    d->rec = rec_get(li, RKEY(IDK_INTERVAL, 0));
    d->nfallback = fld_units(d->rec, 0, d->fallback, 48);
    /* fFirstDateInPtnIsLaterDate: {1} before {0} in the fallback */
    uint32_t i0 = d->nfallback, i1 = d->nfallback;
    for (uint32_t i = 0; i + 2 < d->nfallback; i++) {
        if (d->fallback[i] == '{' && d->fallback[i + 2] == '}' && d->fallback[i + 1] == '0' && i0 == d->nfallback) i0 = i;
        if (d->fallback[i] == '{' && d->fallback[i + 2] == '}' && d->fallback[i + 1] == '1' && i1 == d->nfallback) i1 = i;
    }
    d->later_default = i0 > i1;
    for (uint32_t i = 0; i < sn && i < 64; i++) d->skel[i] = skel[i];
    d->nskel = sn;
    d->ndatefmt = di_best_pattern(g, skel, sn, d->datefmt, DI_MAX);
    for (int i = 0; i < IPI_N; i++) d->ip[i].later = d->later_default;
    vector<uint16_t> conv, date, ndate, time, ntime;
    di_normalize_hour(g, skel, sn, &conv);
    di_split_skeleton(data(conv), (uint32_t)len(conv), &date, &ndate, &time, &ntime);
    if (len(time) && len(date)) {
        /* DateTimePatterns[kDateTime]: the default (medium) glue */
        d->nglue = fld_units(rec_get(li, RKEY(IDK_DTGLUE, 0)), 2, d->glue, 48);
        if (d->nglue < 3) d->nglue = 0;
    }
    int found = di_separate(g, d, &ndate, &ntime);
    if (!found) {
        if (len(time) && !len(date)) di_time_fallbacks(g, d, &time);
        return;
    }
    if (!len(time)) return;
    if (!len(date)) { di_time_fallbacks(g, d, &time); return; }
    /* both: the date's fields' fallbacks, then the date and each time interval */
    vector<uint16_t> s;
    for (uint32_t i = 0; i < sn; i++) push_back(s, skel[i]);
    static const int fields[] = { IPI_DATE, IPI_MONTH, IPI_YEAR, IPI_ERA };
    for (int k = 0; k < 4; k++) {
        uint32_t letter = (uint32_t)ipi_letters[fields[k]];
        if (units_has_char(data(date), (uint32_t)len(date), letter)) continue;
        vector<uint16_t> s2;
        push_back(s2, (uint16_t)letter);
        for (size_t i = 0; i < len(s); i++) push_back(s2, s[i]);
        clear(s);
        for (size_t i = 0; i < len(s2); i++) push_back(s, s2[i]);
        uint16_t pat[DI_MAX];
        uint32_t pn = di_best_pattern(g, data(s), (uint32_t)len(s), pat, DI_MAX);
        di_info(d, fields[k], 0, 0, pat, pn, d->later_default);
    }
    if (!d->nglue) return;
    uint16_t dpat[DI_MAX];
    uint32_t dn = di_best_pattern(g, data(date), (uint32_t)len(date), dpat, DI_MAX);
    static const int tf[] = { IPI_AM_PM, IPI_HOUR, IPI_MINUTE };
    for (int k = 0; k < 3; k++) {
        dipart *pp = &d->ip[tf[k]];
        if (!pp->nf) continue;
        vector<uint16_t> tip, dp, comb;
        for (uint32_t i = 0; i < pp->nf; i++) push_back(tip, pp->first[i]);
        for (uint32_t i = 0; i < pp->ns; i++) push_back(tip, pp->second[i]);
        for (uint32_t i = 0; i < dn; i++) push_back(dp, dpat[i]);
        simple_format(d->glue, d->nglue, &tip, &dp, 0, &comb);
        di_set(d, tf[k], data(comb), (uint32_t)len(comb), pp->later);
    }
}

/* a pattern rendered, appended */
static void di_render(const dctx *c, const uint16_t *pat, uint32_t pn, const dfields *f, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    dt_render(c, pat, pn, f, tx, ty);
}
/* the text of a SimpleFormatter pattern of two arguments: its literal text and where the
   arguments go (getTextWithNoArguments) */
static void di_body(const uint16_t *p, uint32_t n, vector<uint16_t> *body, uint32_t *off0, uint32_t *off1) {
    vector<uint16_t> a0, a1;
    push_back(a0, (uint16_t)0xFFFE);
    push_back(a1, (uint16_t)0xFFFF);
    vector<uint16_t> out;
    simple_format(p, n, &a0, &a1, 0, &out);
    *off0 = 0;
    *off1 = 0;
    for (size_t i = 0; i < len(out); i++) {
        if (out[i] == 0xFFFE) *off0 = (uint32_t)len(*body);
        else if (out[i] == 0xFFFF) *off1 = (uint32_t)len(*body);
        else push_back(*body, out[i]);
    }
}
static void put_span(vector<uint16_t> *body, uint32_t a, uint32_t b, vector<uint16_t> *tx, vector<uint8_t> *ty) {
    for (uint32_t i = a; i < b && i < len(*body); i++) put_c(tx, ty, (*body)[i], DP_LITERAL);
}

/* fallbackFormatRange: both dates by the pattern in the fallback; *first the index of the
   date put first */
static void di_fallback_range(const difmt *d, const dctx *c, const uint16_t *pat, uint32_t pn, const dfields *from, const dfields *to, vector<uint16_t> *tx, vector<uint8_t> *ty, int *first) {
    vector<uint16_t> body;
    uint32_t o0, o1;
    di_body(d->fallback, d->nfallback, &body, &o0, &o1);
    if (o0 < o1) {
        *first = 0;
        put_span(&body, 0, o0, tx, ty);
        di_render(c, pat, pn, from, tx, ty);
        put_span(&body, o0, o1, tx, ty);
        di_render(c, pat, pn, to, tx, ty);
        put_span(&body, o1, (uint32_t)len(body), tx, ty);
    } else {
        *first = 1;
        put_span(&body, 0, o1, tx, ty);
        di_render(c, pat, pn, to, tx, ty);
        put_span(&body, o1, o0, tx, ty);
        di_render(c, pat, pn, from, tx, ty);
        put_span(&body, o0, (uint32_t)len(body), tx, ty);
    }
}
/* fallbackFormat: on one day, the date once and the times' range (by the date-time glue) */
static void di_fallback(const difmt *d, const dctx *c, const uint16_t *pat, uint32_t pn, const dfields *from, const dfields *to, int same_day, vector<uint16_t> *tx, vector<uint8_t> *ty, int *first) {
    if (same_day && d->has_datep && d->has_timep && d->nglue) {
        vector<uint16_t> body;
        uint32_t o0, o1;
        di_body(d->glue, d->nglue, &body, &o0, &o1);
        if (o0 < o1) {
            put_span(&body, 0, o0, tx, ty);
            di_fallback_range(d, c, d->timep, d->ntimep, from, to, tx, ty, first);
            put_span(&body, o0, o1, tx, ty);
            di_render(c, d->datep, d->ndatep, from, tx, ty);
            put_span(&body, o1, (uint32_t)len(body), tx, ty);
        } else {
            put_span(&body, 0, o1, tx, ty);
            di_render(c, d->datep, d->ndatep, from, tx, ty);
            put_span(&body, o1, o0, tx, ty);
            di_fallback_range(d, c, d->timep, d->ntimep, from, to, tx, ty, first);
            put_span(&body, o0, (uint32_t)len(body), tx, ty);
        }
        return;
    }
    di_fallback_range(d, c, pat, pn, from, to, tx, ty, first);
}

/* formatImpl: the range's text and types; *first -1 for a single date (no range), else
   the index of the date put first */
static void di_format(const difmt *d, const dctx *c, const dfields *from, const dfields *to, vector<uint16_t> *tx, vector<uint8_t> *ty, int *first) {
    *first = -1;
    int64_t ya = from->year, yb = to->year;
    if (ya <= 0) ya = 1 - ya;
    if (yb <= 0) yb = 1 - yb;
    int field = IPI_N;
    if ((from->year > 0) != (to->year > 0)) field = IPI_ERA;
    else if (ya != yb) field = IPI_YEAR;
    else if (from->month != to->month) field = IPI_MONTH;
    else if (from->day != to->day) field = IPI_DATE;
    else if ((from->hour >= 12) != (to->hour >= 12)) field = IPI_AM_PM;
    else if (from->hour % 12 != to->hour % 12) field = IPI_HOUR;
    else if (from->minute != to->minute) field = IPI_MINUTE;
    else if (from->second != to->second) field = IPI_SECOND;
    else if (from->ms != to->ms) field = IPI_MS;
    if (field == IPI_N) { di_render(c, d->datefmt, d->ndatefmt, from, tx, ty); return; }
    int same_day = field >= IPI_AM_PM;
    const dipart *pp = &d->ip[field];
    if (!pp->nf && !pp->ns) {
        if (unit_ignored(d->datefmt, d->ndatefmt, field)) { di_render(c, d->datefmt, d->ndatefmt, from, tx, ty); return; }
        di_fallback(d, c, d->datefmt, d->ndatefmt, from, to, same_day, tx, ty, first);
        return;
    }
    if (!pp->nf) {
        di_fallback(d, c, pp->second, pp->ns, from, to, same_day, tx, ty, first);
        return;
    }
    if (pp->later) {
        *first = 1;
        di_render(c, pp->first, pp->nf, to, tx, ty);
        if (pp->ns) di_render(c, pp->second, pp->ns, from, tx, ty);
    } else {
        *first = 0;
        di_render(c, pp->first, pp->nf, from, tx, ty);
        if (pp->ns) di_render(c, pp->second, pp->ns, to, tx, ty);
    }
}

/* formatRange and formatRangeToParts */
static jv dtf_format_range(js_vm *vm, uint32_t o, jv x, jv y, int to_parts) {
    if (x == JV_UNDEF || y == JV_UNDEF) return js_throw_msg(vm, vm->type_error_proto, "Invalid time value");
    double a = js_to_number(vm, x);
    if (vm->throwing) return JV_HOLE;
    double b = js_to_number(vm, y);
    if (vm->throwing) return JV_HOLE;
    if (a != a || fabs(a) > 8.64e15 || b != b || fabs(b) > 8.64e15) return js_throw_msg(vm, vm->range_error_proto, "Invalid time value");
    a = trunc(a) + 0.0;
    b = trunc(b) + 0.0;
    dctx c;
    dtf_ctx(vm, o, &c);
    /* the interval formatter of the pattern's skeleton, its locale's hour cycle the format's */
    vector<uint16_t> pat;
    uint32_t pn = dtf_pattern(vm, o, &pat);
    dskel s;
    dskel_set(data(pat), pn, &s);
    vector<uint16_t> sk;
    dskel_text(&s, &sk);
    int hc = slot_int(vm, o, DFS_HC);
    uint32_t hour_char = (uint32_t)slot_int(vm, o, DFS_HOURCHAR);
    if (hc >= 0) hour_char = (uint32_t)hc_letters[hc];
    dgen g;
    memset(&g, 0, sizeof g);
    defer(g) { vector_dpent_drop(&g.m.e); vector_uint16_t_drop(&g.m.text); }
    dgen_init(&g, c.li, hour_char, c.nsi);
    difmt d;
    di_init(&g, &d, c.li, data(sk), (uint32_t)len(sk));
    dfields from, to;
    dtf_fields(&c, a, &from);
    dtf_fields(&c, b, &to);
    vector<uint16_t> tx;
    vector<uint8_t> ty;
    int first;
    di_format(&d, &c, &from, &to, &tx, &ty, &first);
    /* the spans: the first and second occurrences of the fields shown twice */
    vector<uint32_t> runs;
    size_t i = 0;
    while (i < len(tx)) {
        size_t j = i + 1;
        while (j < len(tx) && ty[j] == ty[i]) j++;
        if (ty[i] != DP_LITERAL) {
            push_back(runs, (uint32_t)i);
            push_back(runs, (uint32_t)j);
            push_back(runs, (uint32_t)ty[i]);
        }
        i = j;
    }
    uint32_t s1a = 0xFFFFFFFF, s1b = 0, s2a = 0xFFFFFFFF, s2b = 0;
    if (first >= 0) {
        size_t nr = len(runs) / 3;
        for (size_t p = 0; p < nr; p++)
            for (size_t q = p + 1; q < nr; q++) {
                if (runs[q * 3 + 2] != runs[p * 3 + 2]) continue;
                if (runs[p * 3] < s1a) s1a = runs[p * 3];
                if (runs[p * 3 + 1] > s1b) s1b = runs[p * 3 + 1];
                if (runs[q * 3] < s2a) s2a = runs[q * 3];
                if (runs[q * 3 + 1] > s2b) s2b = runs[q * 3 + 1];
                break;
            }
    }
    if (s1a == 0xFFFFFFFF) {
        /* no range: the format's own date */
        if (!to_parts) return dtf_format_value(vm, o, jv_num(a), 0);
        vector<uint16_t> t2;
        vector<uint8_t> y2, src;
        dtf_render(vm, o, a, &t2, &y2);
        vector<uint32_t> pt;
        size_t k = 0;
        while (k < len(t2)) {
            size_t j = k + 1;
            while (j < len(t2) && y2[j] == y2[k]) j++;
            push_back(pt, (uint32_t)k);
            push_back(pt, (uint32_t)j);
            push_back(pt, (uint32_t)y2[k]);
            k = j;
        }
        for (size_t u = 0; u < len(t2); u++) push_back(src, (uint8_t)2);
        return parts_array(vm, &t2, &pt, dp_names, &src);
    }
    if (!to_parts) return jv_str(js_str_new(vm, data(tx), (uint32_t)len(tx)));
    /* the parts: the fields and the literals between, each its source by the spans (the
       first date's span is the index first) */
    uint32_t start0 = s1a, end0 = s1b, start1 = s2a, end1 = s2b;
    if (first == 1) { start0 = s2a; end0 = s2b; start1 = s1a; end1 = s1b; }
    vector<uint32_t> pt;
    vector<uint8_t> src;
    for (size_t u = 0; u < len(tx); u++) push_back(src, (uint8_t)2);
    size_t k = 0;
    while (k < len(tx)) {
        size_t j = k + 1;
        while (j < len(tx) && ty[j] == ty[k]) j++;
        push_back(pt, (uint32_t)k);
        push_back(pt, (uint32_t)j);
        push_back(pt, (uint32_t)ty[k]);
        uint8_t sv = 2;
        if (start0 <= k && j <= end0) sv = 0;
        else if (start1 <= k && j <= end1) sv = 1;
        for (size_t u = k; u < j; u++) src[u] = sv;
        k = j;
    }
    return parts_array(vm, &tx, &pt, dp_names, &src);
}

/* Date.prototype.toLocaleString, toLocaleDateString, toLocaleTimeString (which 0, 1, 2): a
   DateTimeFormat of the arguments formats this */
static jv date_to_locale(js_vm *vm, jv thisv, jv locales, jv options, uint32_t which) {
    static const char *const names[] = { "Date.prototype.toLocaleString", "Date.prototype.toLocaleDateString",
        "Date.prototype.toLocaleTimeString" };
    static const int req[] = { DQ_ANY, DQ_DATE, DQ_TIME }, def[] = { DD_ALL, DD_DATE, DD_TIME };
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_DATE) return incompatible(vm, names[which], thisv);
    double t = jv_dbl(vm->objs[jv_idx(thisv)].x);
    if (t != t) return jv_str(js_str_ascii(vm, "Invalid Date"));
    uint32_t o = 0;
    int cache = locales == JV_UNDEF && options == JV_UNDEF;
    if (cache) o = intl_cache_get(vm, IC_DATE + which);
    if (!o) {
        jv f = dtf_new(vm, locales, options, JV_UNDEF, req[which], def[which], names[which]);
        if (f == JV_HOLE) return JV_HOLE;
        o = jv_idx(f);
        if (cache) intl_cache_put(vm, IC_DATE + which, o);
    }
    return dtf_format_value(vm, o, jv_num(t), 0);
}

static jv this_dtf(js_vm *vm, jv thisv, const char *method) {
    if (!is_intl(vm, thisv, JIK_DATETIMEFORMAT)) return incompatible(vm, method, thisv);
    return thisv;
}
/* V8's UnwrapDateTimeFormat (the format getter's, resolvedOptions'): an object that is not
   one is reported itself */
static jv unwrap_dtf(js_vm *vm, jv thisv, const char *method) {
    if (is_intl(vm, thisv, JIK_DATETIMEFORMAT)) return thisv;
    if (jv_is_obj(thisv)) return incompatible(vm, "UnwrapDateTimeFormat", thisv);
    return incompatible(vm, method, thisv);
}

/* ==== Intl.PluralRules (ECMA-402 16) ==== */

enum { PRS_LOCALE, PRS_TYPE, PRS_SET, PRS_MNID, PRS_MNFD, PRS_MXFD, PRS_MNSD, PRS_MXSD, PRS_RTYPE, PRS_INC, PRS_RMODE,
       PRS_RPRIO, PRS_TZD, PRS_N };
static const char *const pr_types[] = { "cardinal", "ordinal", 0 };

/* a new object of a service, its prototype the new target's (else the service's) */
static uint32_t service_new(js_vm *vm, jv newt, uint32_t kind, uint32_t nslots, int *ok) {
    jv p = JV_UNDEF;
    *ok = 1;
    if (jv_is_obj(newt)) {
        p = js_get(vm, newt, A_PROTOTYPE);
        if (vm->throwing) { *ok = 0; return 0; }
    }
    uint32_t pr = vm->intl_protos[kind];
    if (jv_is_obj(p)) pr = jv_idx(p);
    return intl_new(vm, kind, pr, nslots);
}
static jv requires_new(js_vm *vm, const char *name) {
    char m[96];
    m[0] = 0;
    cat(m, sizeof m, "Constructor ");
    cat(m, sizeof m, name);
    cat(m, sizeof m, " requires 'new'");
    return js_throw_msg(vm, vm->type_error_proto, m);
}

static jv pr_new(js_vm *vm, jv locales, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return requires_new(vm, "Intl.PluralRules");
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    const char *svc = "Intl.PluralRules";
    jv opts = options_object(vm, options, svc);
    if (opts == JV_HOLE) return JV_HOLE;
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    int type = get_option_str(vm, opts, "type", pr_types, svc, 0);
    if (type == -2) return JV_HOLE;
    if (type < 0) type = 0;
    dopts dg;
    if (!digit_options(vm, opts, svc, 0, 3, 0, &dg)) return JV_HOLE;
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_PLURAL, found, sizeof found, &t);
    int ok;
    uint32_t o = service_new(vm, newt, JIK_PLURALRULES, PRS_N, &ok);
    if (!ok) return JV_HOLE;
    slot_set(vm, o, PRS_LOCALE, jv_str(js_str_ascii(vm, found)));
    slot_set(vm, o, PRS_TYPE, jv_num(type));
    slot_set(vm, o, PRS_SET, jv_num(plural_set(found, type)));
    slot_set(vm, o, PRS_MNID, jv_num(dg.mnid));
    slot_set(vm, o, PRS_MNFD, jv_num(dg.mnfd));
    slot_set(vm, o, PRS_MXFD, jv_num(dg.mxfd));
    slot_set(vm, o, PRS_MNSD, jv_num(dg.mnsd));
    slot_set(vm, o, PRS_MXSD, jv_num(dg.mxsd));
    slot_set(vm, o, PRS_RTYPE, jv_num(dg.rtype));
    slot_set(vm, o, PRS_INC, jv_num(dg.inc));
    slot_set(vm, o, PRS_RMODE, jv_num(dg.rmode));
    slot_set(vm, o, PRS_RPRIO, jv_num(dg.rprio));
    slot_set(vm, o, PRS_TZD, jv_num(dg.tzd));
    return jv_obj(o);
}

/* a number's category: its value rounded as the digit options say, then the rules */
static int pr_category(js_vm *vm, uint32_t o, double d) {
    dec x;
    dec_from_double(d, &x);
    nfo n;
    memset(&n, 0, sizeof n);
    n.mnid = slot_int(vm, o, PRS_MNID);
    n.mnfd = slot_int(vm, o, PRS_MNFD);
    n.mxfd = slot_int(vm, o, PRS_MXFD);
    n.mnsd = slot_int(vm, o, PRS_MNSD);
    n.mxsd = slot_int(vm, o, PRS_MXSD);
    n.rtype = slot_int(vm, o, PRS_RTYPE);
    n.increment = slot_int(vm, o, PRS_INC);
    n.rmode = slot_int(vm, o, PRS_RMODE);
    n.trailing = slot_int(vm, o, PRS_TZD);
    int mf;
    nf_round(&n, &x, &mf);
    return plural_of((uint32_t)slot_int(vm, o, PRS_SET), &x, mf, 0);
}
static jv pr_select(js_vm *vm, uint32_t o, jv v) {
    double d = js_to_number(vm, v);
    if (vm->throwing) return JV_HOLE;
    return jv_str(js_str_ascii(vm, plural_name[pr_category(vm, o, d)]));
}
static jv pr_select_range(js_vm *vm, uint32_t o, jv x, jv y) {
    if (x == JV_UNDEF) return js_throw_msg(vm, vm->type_error_proto, "Invalid startRange : undefined");
    if (y == JV_UNDEF) return js_throw_msg(vm, vm->type_error_proto, "Invalid endRange : undefined");
    double a = js_to_number(vm, x);
    if (vm->throwing) return JV_HOLE;
    double b = js_to_number(vm, y);
    if (vm->throwing) return JV_HOLE;
    if (a != a) return js_throw_msg(vm, vm->range_error_proto, "Invalid startRange : NaN");
    if (b != b) return js_throw_msg(vm, vm->range_error_proto, "Invalid endRange : NaN");
    char tag[TAG_MAX + 1];
    str_ascii(vm, jv_idx(slot_get(vm, o, PRS_LOCALE)), tag, sizeof tag);
    return jv_str(js_str_ascii(vm, plural_name[plural_range(tag, pr_category(vm, o, a), pr_category(vm, o, b))]));
}
/* the categories of a rule set (each condition skipped) */
static int plural_cats(uint32_t set, int *cats) {
    if (!set) { cats[0] = PL_OTHER; return 1; }
    uint32_t p = set, ncat = db8(p++);
    int n = 0, other = 0;
    for (uint32_t c = 0; c < ncat; c++) {
        int cat_ = (int)db8(p++);
        uint32_t nor = db8(p++);
        for (uint32_t g = 0; g < nor; g++) {
            uint32_t nand = db8(p++);
            for (uint32_t r = 0; r < nand; r++) {
                uint32_t nr = db8(p + 6);
                p += 7 + nr * 8;
            }
        }
        if (cat_ == PL_OTHER) other = 1;
        cats[n++] = cat_;
    }
    if (!other) cats[n++] = PL_OTHER;
    return n;
}
static jv pr_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, PRS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "type"), jv_str(js_str_ascii(vm, pr_types[slot_int(vm, o, PRS_TYPE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "minimumIntegerDigits"), slot_get(vm, o, PRS_MNID), 0);
    /* the digits as V8 reads them back from the skeleton: significant ones when it has
       any, else fraction ones */
    if (slot_int(vm, o, PRS_RTYPE) != RT_FRACTION) {
        js_define(vm, r, js_atom_ascii(vm, "minimumSignificantDigits"), slot_get(vm, o, PRS_MNSD), 0);
        js_define(vm, r, js_atom_ascii(vm, "maximumSignificantDigits"), slot_get(vm, o, PRS_MXSD), 0);
    } else {
        js_define(vm, r, js_atom_ascii(vm, "minimumFractionDigits"), slot_get(vm, o, PRS_MNFD), 0);
        js_define(vm, r, js_atom_ascii(vm, "maximumFractionDigits"), slot_get(vm, o, PRS_MXFD), 0);
    }
    /* the categories as ICU's keywords list them: by name, other last */
    int cats[8];
    int n = plural_cats((uint32_t)slot_int(vm, o, PRS_SET), cats);
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (cats[best] == PL_OTHER || (cats[j] != PL_OTHER && strcmp(plural_name[cats[j]], plural_name[cats[best]]) < 0)) best = j;
        int keep = cats[i];
        cats[i] = cats[best];
        cats[best] = keep;
    }
    uint32_t arr = js_array_new(vm, 0);
    for (int i = 0; i < n; i++) js_array_push(vm, arr, jv_str(js_str_ascii(vm, plural_name[cats[i]])));
    js_define(vm, r, js_atom_ascii(vm, "pluralCategories"), jv_obj(arr), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingIncrement"), slot_get(vm, o, PRS_INC), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingMode"), jv_str(js_str_ascii(vm, rm_names[slot_int(vm, o, PRS_RMODE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "roundingPriority"), jv_str(js_str_ascii(vm, rp_names[slot_int(vm, o, PRS_RPRIO)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "trailingZeroDisplay"), jv_str(js_str_ascii(vm, tz_names[slot_int(vm, o, PRS_TZD)])), 0);
    return jv_obj(r);
}

/* ==== Intl.ListFormat (ECMA-402 13) ==== */

enum { LFS_LOCALE, LFS_TYPE, LFS_STYLE, LFS_DATALOC, LFS_N };
static const char *const lf_types[] = { "conjunction", "disjunction", "unit", 0 };
static const char *const lf_styles[] = { "long", "short", "narrow", 0 };

static jv lf_new(js_vm *vm, jv locales, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return requires_new(vm, "Intl.ListFormat");
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    jv opts = options_object_strict(vm, options);
    if (opts == JV_HOLE) return JV_HOLE;
    const char *svc = "Intl.ListFormat";
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    int type = get_option_str(vm, opts, "type", lf_types, svc, 0);
    if (type == -2) return JV_HOLE;
    if (type < 0) type = 0;
    int style = get_option_str(vm, opts, "style", lf_styles, svc, 0);
    if (style == -2) return JV_HOLE;
    if (style < 0) style = 0;
    int ok;
    uint32_t o = service_new(vm, newt, JIK_LISTFORMAT, LFS_N, &ok);
    if (!ok) return JV_HOLE;
    int li = locale_index(found);
    if (li < 0) li = locale_index("en");
    slot_set(vm, o, LFS_LOCALE, jv_str(js_str_ascii(vm, found)));
    slot_set(vm, o, LFS_TYPE, jv_num(type));
    slot_set(vm, o, LFS_STYLE, jv_num(style));
    slot_set(vm, o, LFS_DATALOC, jv_num(li));
    return jv_obj(o);
}

/* ICU's Spanish and Hebrew list handlers: whether the next item changes y to e, o to u,
   ו to ו- */
static int starts_ci(const uint16_t *s, uint32_t n, char c) { return n > 0 && (s[0] == (uint16_t)c || s[0] == (uint16_t)upper(c)); }
static int es_to_e(const uint16_t *s, uint32_t n) {
    if (!n) return 0;
    if (starts_ci(s, n, 'h') && n > 1 && (s[1] == 'i' || s[1] == 'I') &&
        (n == 2 || !(s[2] == 'a' || s[2] == 'A' || s[2] == 'e' || s[2] == 'E'))) return 1;
    return starts_ci(s, n, 'i');
}
static int es_to_u(const uint16_t *s, uint32_t n) {
    if (!n) return 0;
    if (starts_ci(s, n, 'o') || s[0] == '8') return 1;
    if (starts_ci(s, n, 'h') && n > 1 && (s[1] == 'o' || s[1] == 'O')) return 1;
    if (n >= 2 && s[0] == '1' && s[1] == '1' && (n == 2 || s[2] == ' ')) return 1;
    return 0;
}
static int he_to_dash(const uint16_t *s, uint32_t n) {
    if (!n) return 0;
    uint32_t c = s[0];
    return !((c >= 0x0591 && c <= 0x05F4) || (c >= 0xFB1D && c <= 0xFB4F));
}
static int units_eq_ascii(const uint16_t *a, uint32_t n, const uint16_t *b, uint32_t m) {
    if (n != m) return 0;
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
/* a two or end pattern for the next item, as the language's handler changes it */
static uint32_t lf_pattern(const char *lang, uint32_t rec, uint32_t f, const uint16_t *next, uint32_t nn, uint16_t *buf, uint32_t cap) {
    uint32_t n = fld_units(rec, f, buf, cap);
    static const uint16_t es_y[] = { '{', '0', '}', ' ', 'y', ' ', '{', '1', '}' };
    static const uint16_t es_o[] = { '{', '0', '}', ' ', 'o', ' ', '{', '1', '}' };
    static const uint16_t he_vav[] = { '{', '0', '}', ' ', 0x05D5, '{', '1', '}' };
    if (!strcmp(lang, "es")) {
        if (units_eq_ascii(buf, n, es_y, 9) && es_to_e(next, nn)) buf[4] = 'e';
        else if (units_eq_ascii(buf, n, es_o, 9) && es_to_u(next, nn)) buf[4] = 'u';
    } else if (!strcmp(lang, "he") || !strcmp(lang, "iw")) {
        if (units_eq_ascii(buf, n, he_vav, 8) && he_to_dash(next, nn) && n + 1 <= cap) {
            buf[8] = buf[7];
            buf[7] = buf[6];
            buf[6] = buf[5];
            buf[5] = '-';
            n++;
        }
    }
    return n;
}
/* a pattern of {0} and {1} applied to what is built (its {0}) and the next item; the
   types 0 literal, else an element's (its index's, modulo 255, plus 1) */
static void lf_apply(const uint16_t *pat, uint32_t pn, vector<uint16_t> *tx, vector<uint8_t> *ty, const uint16_t *item, uint32_t in, uint32_t type) {
    vector<uint16_t> ot;
    vector<uint8_t> oy;
    for (uint32_t i = 0; i < pn; i++) {
        if (pat[i] == '{' && i + 2 < pn && pat[i + 2] == '}' && (pat[i + 1] == '0' || pat[i + 1] == '1')) {
            if (pat[i + 1] == '0') {
                for (size_t k = 0; k < len(*tx); k++) { push_back(ot, (*tx)[k]); push_back(oy, (*ty)[k]); }
            } else put_u(&ot, &oy, item, in, type);
            i += 2;
            continue;
        }
        put_c(&ot, &oy, pat[i], 0);
    }
    clear(*tx);
    clear(*ty);
    for (size_t k = 0; k < len(ot); k++) { push_back(*tx, ot[k]); push_back(*ty, oy[k]); }
}
/* a string's units into a vector */
static void str_units(js_vm *vm, uint32_t s, vector<uint16_t> *out) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, off = vm->strs[s].off;
    for (uint32_t i = 0; i < n; i++) push_back(*out, vm->units[off + i]);
}
static jv lf_format(js_vm *vm, uint32_t o, jv list, int to_parts) {
    uint32_t items = js_array_new(vm, 0);
    if (list != JV_UNDEF) {
        jv it = js_iter_of(vm, list);
        if (vm->throwing) return JV_HOLE;
        jv x;
        while (js_iter_step(vm, jv_idx(it), &x)) {
            if (!jv_is_str(x)) {
                js_iter_close(vm, jv_idx(it));
                vector<char> m;
                const char *a = "Iterable yielded ";
                append_range(m, a, strlen(a));
                receiver_text(vm, x, &m);
                const char *b = " which is not a string";
                append_range(m, b, strlen(b));
                return js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
            }
            js_array_push(vm, items, x);
        }
        if (vm->throwing) return JV_HOLE;
    }
    uint32_t n = vm->objs[items].b;
    int li = slot_int(vm, o, LFS_DATALOC);
    uint32_t rec = rec_get(li, RKEY(IDK_LIST, (uint32_t)(slot_int(vm, o, LFS_TYPE) * 3 + slot_int(vm, o, LFS_STYLE))));
    char tag[TAG_MAX + 1], lang[9];
    db_copy(db32(IDB_LOCALES + (uint32_t)li * LOC_REC), tag, sizeof tag);
    size_t k = 0;
    while (tag[k] && tag[k] != '-' && k < 8) { lang[k] = tag[k]; k++; }
    lang[k] = 0;
    vector<uint16_t> tx;
    vector<uint8_t> ty;
    for (uint32_t i = 0; i < n; i++) {
        vector<uint16_t> item;
        str_units(vm, jv_idx(js_array_get(vm, items, i)), &item);
        uint32_t et = i % 255 + 1;
        if (i == 0) { put_u(&tx, &ty, data(item), (uint32_t)len(item), et); continue; }
        uint16_t pat[128];
        uint32_t pn;
        if (n == 2) pn = lf_pattern(lang, rec, 3, data(item), (uint32_t)len(item), pat, 128);
        else if (i == 1) pn = fld_units(rec, 0, pat, 128);
        else if (i < n - 1) pn = fld_units(rec, 1, pat, 128);
        else pn = lf_pattern(lang, rec, 2, data(item), (uint32_t)len(item), pat, 128);
        lf_apply(pat, pn, &tx, &ty, data(item), (uint32_t)len(item), et);
    }
    if (!to_parts) return jv_str(js_str_new(vm, data(tx), (uint32_t)len(tx)));
    static const char *const names[] = { "literal", "element" };
    vector<uint32_t> pt;
    size_t i = 0;
    while (i < len(tx)) {
        size_t j = i + 1;
        while (j < len(tx) && ty[j] == ty[i]) j++;
        push_back(pt, (uint32_t)i);
        push_back(pt, (uint32_t)j);
        push_back(pt, (uint32_t)(ty[i] != 0));
        i = j;
    }
    return parts_array(vm, &tx, &pt, names, 0);
}
static jv lf_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, LFS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "type"), jv_str(js_str_ascii(vm, lf_types[slot_int(vm, o, LFS_TYPE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "style"), jv_str(js_str_ascii(vm, lf_styles[slot_int(vm, o, LFS_STYLE)])), 0);
    return jv_obj(r);
}

/* ==== Intl.RelativeTimeFormat (ECMA-402 17) ==== */

enum { RTS_LOCALE, RTS_STYLE, RTS_NUMERIC, RTS_NU, RTS_NF, RTS_DATALOC, RTS_TAG, RTS_N };
static const char *const rt_styles[] = { "long", "short", "narrow", 0 };
static const char *const rt_numerics[] = { "always", "auto", 0 };
static const char *const rt_units[] = { "year", "quarter", "month", "week", "day", "hour", "minute", "second", 0 };

static jv rt_new(js_vm *vm, jv locales, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return requires_new(vm, "Intl.RelativeTimeFormat");
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    const char *svc = "Intl.RelativeTimeFormat";
    jv opts = options_object(vm, options, svc);
    if (opts == JV_HOLE) return JV_HOLE;
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    uint32_t nus = 0;
    char nu_opt[16];
    nu_opt[0] = 0;
    int r = get_option_str(vm, opts, "numberingSystem", 0, svc, &nus);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[80];
        int n = str_ascii(vm, nus, b, sizeof b);
        if (n < 0 || !is_type(b)) {
            vector<char> m;
            const char *a = "Invalid numberingSystem : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, nus, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        if (n < 16) copyz(nu_opt, sizeof nu_opt, b);
    }
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    char nu[16], kw_nu[16];
    dt_keyword(&t, "nu", kw_nu, sizeof kw_nu);
    if (nu_opt[0] && kw_nu[0] && strcmp(nu_opt, kw_nu)) kw_nu[0] = 0;
    if (nu_opt[0] && numbering_index(nu_opt) >= 0) copyz(nu, sizeof nu, nu_opt);
    else if (kw_nu[0]) copyz(nu, sizeof nu, kw_nu);
    else default_numbering(found, nu, sizeof nu);
    int style = get_option_str(vm, opts, "style", rt_styles, svc, 0);
    if (style == -2) return JV_HOLE;
    if (style < 0) style = 0;
    int numeric = get_option_str(vm, opts, "numeric", rt_numerics, svc, 0);
    if (numeric == -2) return JV_HOLE;
    if (numeric < 0) numeric = 0;
    char loc[TAG_MAX + 1], nftag[TAG_MAX + 1];
    copyz(loc, sizeof loc, found);
    if (kw_nu[0]) { cat(loc, sizeof loc, "-u-nu-"); cat(loc, sizeof loc, kw_nu); }
    copyz(nftag, sizeof nftag, found);
    cat(nftag, sizeof nftag, "-u-nu-");
    cat(nftag, sizeof nftag, nu);
    jv nf = nf_new(vm, jv_str(js_str_ascii(vm, nftag)), JV_UNDEF, JV_UNDEF, svc);
    if (nf == JV_HOLE) return JV_HOLE;
    int ok;
    uint32_t o = service_new(vm, newt, JIK_RELATIVETIMEFORMAT, RTS_N, &ok);
    if (!ok) return JV_HOLE;
    int li = locale_index(found);
    if (li < 0) li = locale_index("en");
    slot_set(vm, o, RTS_LOCALE, jv_str(js_str_ascii(vm, loc)));
    slot_set(vm, o, RTS_STYLE, jv_num(style));
    slot_set(vm, o, RTS_NUMERIC, jv_num(numeric));
    slot_set(vm, o, RTS_NU, jv_str(js_str_ascii(vm, nu)));
    slot_set(vm, o, RTS_NF, nf);
    slot_set(vm, o, RTS_DATALOC, jv_num(li));
    slot_set(vm, o, RTS_TAG, jv_str(js_str_ascii(vm, found)));
    return jv_obj(o);
}

/* a relative time field of a unit at a style, else at the wider ones (ICU's fallbacks) */
static uint32_t rt_field(int li, int unit, int style, uint32_t f, uint16_t *buf, uint32_t cap) {
    for (int s = style; s >= 0; s--) {
        uint32_t n = fld_units(rec_get(li, RKEY(IDK_RELTIME, (uint32_t)(unit * 3 + s))), f, buf, cap);
        if (n) return n;
    }
    return 0;
}

static jv rt_format(js_vm *vm, uint32_t o, jv value, jv unitv, int to_parts) {
    double v = js_to_number(vm, value);
    if (vm->throwing) return JV_HOLE;
    uint32_t us = js_to_string(vm, unitv);
    if (!us) return JV_HOLE;
    const char *fn = "Intl.RelativeTimeFormat.prototype.format";
    if (to_parts) fn = "Intl.RelativeTimeFormat.prototype.formatToParts";
    if (v != v || v == INFINITY || v == -INFINITY) {
        char m[160];
        m[0] = 0;
        cat(m, sizeof m, "Value need to be finite number for ");
        cat(m, sizeof m, fn);
        cat(m, sizeof m, "()");
        return js_throw_msg(vm, vm->range_error_proto, m);
    }
    char ub[16];
    int un = str_ascii(vm, us, ub, sizeof ub);
    int unit = -1;
    for (int i = 0; rt_units[i] && un > 0; i++) {
        size_t l = strlen(rt_units[i]);
        if (!strcmp(ub, rt_units[i]) || ((size_t)un == l + 1 && !strncmp(ub, rt_units[i], l) && ub[l] == 's')) unit = i;
    }
    if (unit < 0) {
        vector<char> m;
        const char *a = "Invalid unit argument for ";
        append_range(m, a, strlen(a));
        append_range(m, fn, strlen(fn));
        append_range(m, "() '", 4);
        js_str_to_utf8(vm, us, &m);
        push_back(m, '\'');
        return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
    }
    int li = slot_int(vm, o, RTS_DATALOC), style = slot_int(vm, o, RTS_STYLE);
    uint16_t buf[160];
    if (slot_int(vm, o, RTS_NUMERIC) == 1 && v > -2.1 && v < 2.1) {
        /* auto: an offset within 1% of -2 to 2 named, when the locale names it */
        double x100 = v * 100.0;
        int io;
        if (x100 < 0) io = (int)(x100 - 0.5);
        else io = (int)(x100 + 0.5);
        int idx = -1;
        if (io == -200) idx = 0;
        else if (io == -100) idx = 1;
        else if (io == 0) idx = 2;
        else if (io == 100) idx = 3;
        else if (io == 200) idx = 4;
        if (idx >= 0 && (unit != 7 || idx == 2)) {
            uint32_t n = rt_field(li, unit, style, (uint32_t)idx, buf, 160);
            if (n) {
                jv s = jv_str(js_str_new(vm, buf, n));
                if (!to_parts) return s;
                uint32_t arr = js_array_new(vm, 0);
                uint32_t po = js_obj_new(vm, JK_PLAIN, vm->object_proto);
                js_define(vm, po, js_atom_ascii(vm, "type"), jv_str(js_str_ascii(vm, "literal")), 0);
                js_define(vm, po, js_atom_ascii(vm, "value"), s, 0);
                js_array_push(vm, arr, jv_obj(po));
                return jv_obj(arr);
            }
        }
    }
    /* the number: its sign the direction, its plural the pattern's */
    int past = signbit(v) != 0;
    double a = fabs(v);
    uint32_t nf = jv_idx(slot_get(vm, o, RTS_NF));
    dec x;
    dec_from_double(a, &x);
    nfo n;
    nfo_read(vm, nf, &n);
    vector<uint16_t> ntx;
    vector<uint8_t> nty;
    nfinfo inf;
    nf_render(&n, &x, -1, &ntx, &nty, &inf);
    vector<uint16_t> ot;
    vector<uint8_t> oy;
    nf_assemble(&ntx, &nty, &inf, &ot, &oy);
    dec y;
    dec_from_double(a, &y);
    int mf;
    nf_round(&n, &y, &mf);
    char tag[TAG_MAX + 1];
    str_ascii(vm, jv_idx(slot_get(vm, o, RTS_TAG)), tag, sizeof tag);
    int plural = plural_of(plural_set(tag, 0), &y, mf, 0);
    uint32_t base = 5;
    if (past) base = 11;
    uint32_t pn = rt_field(li, unit, style, base + (uint32_t)plural, buf, 160);
    if (!pn) pn = rt_field(li, unit, style, base + PL_OTHER, buf, 160);
    /* the pattern's {0} the number */
    vector<uint16_t> tx;
    vector<uint8_t> ty;
    for (uint32_t i = 0; i < pn; i++) {
        if (buf[i] == '{' && i + 2 < pn && buf[i + 1] == '0' && buf[i + 2] == '}') {
            for (size_t k = 0; k < len(ot); k++) { push_back(tx, ot[k]); push_back(ty, (uint8_t)(oy[k] + 1)); }
            i += 2;
            continue;
        }
        put_c(&tx, &ty, buf[i], 0);
    }
    if (!to_parts) return jv_str(js_str_new(vm, data(tx), (uint32_t)len(tx)));
    /* the parts: the literals, and the number's (with their unit) */
    uint32_t arr = js_array_new(vm, 0);
    uint32_t a_type = js_atom_ascii(vm, "type"), a_value = js_atom_ascii(vm, "value"), a_unit = js_atom_ascii(vm, "unit");
    size_t i = 0;
    while (i < len(tx)) {
        size_t j = i + 1;
        if (ty[i] == 0) {
            while (j < len(tx) && ty[j] == 0) j++;
            uint32_t po = js_obj_new(vm, JK_PLAIN, vm->object_proto);
            js_define(vm, po, a_type, jv_str(js_str_ascii(vm, "literal")), 0);
            js_define(vm, po, a_value, jv_str(js_str_new(vm, data(tx) + i, (uint32_t)(j - i))), 0);
            js_array_push(vm, arr, jv_obj(po));
            i = j;
            continue;
        }
        while (j < len(tx) && ty[j] != 0) j++;
        vector<uint16_t> nt;
        vector<uint8_t> ny;
        for (size_t k = i; k < j; k++) { push_back(nt, tx[k]); push_back(ny, (uint8_t)(ty[k] - 1)); }
        vector<uint32_t> pt;
        parts_of(&nt, &ny, 0, &pt);
        for (size_t q = 0; q + 2 < len(pt); q += 3) {
            uint32_t s0 = pt[q], s1 = pt[q + 1], tp = pt[q + 2];
            uint32_t po = js_obj_new(vm, JK_PLAIN, vm->object_proto);
            js_define(vm, po, a_type, jv_str(js_str_ascii(vm, pt_names[tp])), 0);
            js_define(vm, po, a_value, jv_str(js_str_new(vm, data(nt) + s0, s1 - s0)), 0);
            js_define(vm, po, a_unit, jv_str(js_str_ascii(vm, rt_units[unit])), 0);
            js_array_push(vm, arr, jv_obj(po));
        }
        i = j;
    }
    return jv_obj(arr);
}
static jv rt_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, RTS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "style"), jv_str(js_str_ascii(vm, rt_styles[slot_int(vm, o, RTS_STYLE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "numeric"), jv_str(js_str_ascii(vm, rt_numerics[slot_int(vm, o, RTS_NUMERIC)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "numberingSystem"), slot_get(vm, o, RTS_NU), 0);
    return jv_obj(r);
}

/* ==== Intl.DisplayNames (ECMA-402 12) ==== */

enum { DNS_LOCALE, DNS_STYLE, DNS_TYPE, DNS_FALLBACK, DNS_LANGDISPLAY, DNS_DATALOC, DNS_N };
static const char *const dn_styles[] = { "narrow", "short", "long", 0 };
static const char *const dn_types[] = { "language", "region", "script", "currency", "calendar", "dateTimeField", 0 };
static const char *const dn_fallbacks[] = { "code", "none", 0 };
static const char *const dn_displays[] = { "dialect", "standard", 0 };
enum { DNT_LANGUAGE, DNT_REGION, DNT_SCRIPT, DNT_CURRENCY, DNT_CALENDAR, DNT_FIELD };

static jv dn_new(js_vm *vm, jv locales, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return requires_new(vm, "Intl.DisplayNames");
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    jv opts = options_object_strict(vm, options);
    if (opts == JV_HOLE) return JV_HOLE;
    const char *svc = "Intl.DisplayNames";
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    int style = get_option_str(vm, opts, "style", dn_styles, svc, 0);
    if (style == -2) return JV_HOLE;
    if (style < 0) style = 2;
    int type = get_option_str(vm, opts, "type", dn_types, svc, 0);
    if (type == -2) return JV_HOLE;
    int fallback = get_option_str(vm, opts, "fallback", dn_fallbacks, svc, 0);
    if (fallback == -2) return JV_HOLE;
    if (fallback < 0) fallback = 0;
    int display = get_option_str(vm, opts, "languageDisplay", dn_displays, svc, 0);
    if (display == -2) return JV_HOLE;
    if (display < 0) display = 0;
    if (type < 0) return js_throw_msg(vm, vm->type_error_proto, "invalid_argument");
    int ok;
    uint32_t o = service_new(vm, newt, JIK_DISPLAYNAMES, DNS_N, &ok);
    if (!ok) return JV_HOLE;
    int li = locale_index(found);
    if (li < 0) li = locale_index("en");
    slot_set(vm, o, DNS_LOCALE, jv_str(js_str_ascii(vm, found)));
    slot_set(vm, o, DNS_STYLE, jv_num(style));
    slot_set(vm, o, DNS_TYPE, jv_num(type));
    slot_set(vm, o, DNS_FALLBACK, jv_num(fallback));
    slot_set(vm, o, DNS_LANGDISPLAY, jv_num(display));
    slot_set(vm, o, DNS_DATALOC, jv_num(li));
    return jv_obj(o);
}
static jv dn_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    int type = slot_int(vm, o, DNS_TYPE);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, DNS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "style"), jv_str(js_str_ascii(vm, dn_styles[slot_int(vm, o, DNS_STYLE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "type"), jv_str(js_str_ascii(vm, dn_types[type])), 0);
    js_define(vm, r, js_atom_ascii(vm, "fallback"), jv_str(js_str_ascii(vm, dn_fallbacks[slot_int(vm, o, DNS_FALLBACK)])), 0);
    if (type == DNT_LANGUAGE)
        js_define(vm, r, js_atom_ascii(vm, "languageDisplay"), jv_str(js_str_ascii(vm, dn_displays[slot_int(vm, o, DNS_LANGDISPLAY)])), 0);
    return jv_obj(r);
}

/* a names record's entry for a code (entries code, GS, name, by FS): its length, 0 none */
static uint32_t names_find(uint32_t rec, const char *code, uint16_t *buf, uint32_t cap) {
    size_t cn = strlen(code);
    uint32_t p = rec;
    while (db8(p)) {
        uint32_t q = p;
        size_t k = 0;
        while (k < cn && db8(q) == (uint8_t)code[k]) { q++; k++; }
        if (k == cn && db8(q) == 0x1D) {
            q++;
            uint32_t e = q;
            while (db8(e) && db8(e) != 0x1F) e++;
            return u8_units(q, e - q, buf, cap);
        }
        while (db8(p) && db8(p) != 0x1F) p++;
        if (db8(p) == 0x1F) p++;
    }
    return 0;
}
/* a display name: the first of the locale's chain (each its own entries) and root */
static uint32_t name_of(int li, uint32_t cat_, const char *code, uint16_t *buf, uint32_t cap) {
    for (int guard = 0; li >= 0 && li != 0xFFFF && guard < 32; guard++) {
        uint32_t rec = res_in(locale_res((uint32_t)li), locale_nres((uint32_t)li), RKEY(cat_, 0));
        if (rec) {
            uint32_t n = names_find(rec, code, buf, cap);
            if (n) return n;
        }
        li = (int)locale_parent((uint32_t)li);
    }
    uint32_t rec = res_in(IDB_ROOT_RES, IDB_ROOT_RES_N, RKEY(cat_, 0));
    if (rec) return names_find(rec, code, buf, cap);
    return 0;
}
/* a name, its short alternative first at the short styles */
static uint32_t name_styled(int li, uint32_t cat_, const char *code, int is_short, uint16_t *buf, uint32_t cap) {
    if (is_short) {
        char alt[64];
        copyz(alt, sizeof alt, code);
        cat(alt, sizeof alt, "-alt-short");
        uint32_t n = name_of(li, cat_, alt, buf, cap);
        if (n) return n;
    }
    return name_of(li, cat_, code, buf, cap);
}
/* a region's name, a deprecated code's replacement's when it has none (uloc's) */
static uint32_t region_name(int li, const char *code, int is_short, uint16_t *buf, uint32_t cap) {
    uint32_t n = name_styled(li, IDK_REGIONN, code, is_short, buf, cap);
    if (n) return n;
    static const char *const dep[] = { "AN", "CW", "BU", "MM", "CS", "RS", "DD", "DE", "DY", "BJ", "FX", "FR", "HV", "BF", "NH", "VU",
        "RH", "ZW", "SU", "RU", "TP", "TL", "UK", "GB", "VD", "VN", "YD", "YE", "YU", "RS", "ZR", "CD", 0 };
    for (int i = 0; dep[i]; i += 2)
        if (!strcmp(code, dep[i])) return name_styled(li, IDK_REGIONN, dep[i + 1], is_short, buf, cap);
    return 0;
}
static void put_units(vector<uint16_t> *out, const uint16_t *b, uint32_t n) { for (uint32_t i = 0; i < n; i++) push_back(*out, b[i]); }
static void put_ascii(vector<uint16_t> *out, const char *s) { for (size_t i = 0; s[i]; i++) push_back(*out, (uint16_t)(uint8_t)s[i]); }

/* LocaleDisplayNamesImpl::localeDisplayName of a canonical language tag: 1, or 0 for a
   name the data lacks without the code's fallback */
static int dn_language(int li, const itag *t, int is_short, int dialect, int subst, vector<uint16_t> *out) {
    char lang[16];
    copyz(lang, sizeof lang, t->lang);
    if (!lang[0] || !strcmp(lang, "und")) copyz(lang, sizeof lang, "root");
    int has_script = t->script[0] != 0, has_region = t->region[0] != 0;
    uint16_t buf[256];
    uint32_t n = 0;
    if (dialect) {
        /* ICU's search: the buffer it builds each key in is not cleared between them, so
           with a script and a region only the three together can match */
        char key[64];
        if (has_script && has_region) {
            copyz(key, sizeof key, lang); cat(key, sizeof key, "-"); cat(key, sizeof key, t->script);
            cat(key, sizeof key, "-"); cat(key, sizeof key, t->region);
            n = name_styled(li, IDK_LANGN, key, is_short, buf, 256);
            if (n) { has_script = 0; has_region = 0; }
        } else if (has_script) {
            copyz(key, sizeof key, lang); cat(key, sizeof key, "-"); cat(key, sizeof key, t->script);
            n = name_styled(li, IDK_LANGN, key, is_short, buf, 256);
            if (n) has_script = 0;
        } else if (has_region) {
            copyz(key, sizeof key, lang); cat(key, sizeof key, "-"); cat(key, sizeof key, t->region);
            n = name_styled(li, IDK_LANGN, key, is_short, buf, 256);
            if (n) has_region = 0;
        }
    }
    vector<uint16_t> name;
    if (n) put_units(&name, buf, n);
    else {
        n = name_styled(li, IDK_LANGN, lang, is_short, buf, 256);
        if (n) put_units(&name, buf, n);
        else if (subst) put_ascii(&name, lang);
        else return 0;
    }
    uint32_t pat = rec_get(li, RKEY(IDK_LOCPAT, 0));
    uint16_t lp[48], sp[48];
    uint32_t lpn = fld_units(pat, 0, lp, 48), spn = fld_units(pat, 1, sp, 48);
    vector<uint16_t> rem;
    int any = 0;
    int kv = find_kw(t, "va"), posix = kv >= 0 && !strcmp(t->val[kv], "posix");
    for (int part = 0; part < 2 + (int)t->nvar + posix; part++) {
        vector<uint16_t> item;
        if (part == 0) {
            if (!has_script) continue;
            n = name_styled(li, IDK_SCRIPTN, t->script, is_short, buf, 256);
            if (n) put_units(&item, buf, n);
            else if (subst) put_ascii(&item, t->script);
            else return 0;
        } else if (part == 1) {
            if (!has_region) continue;
            n = region_name(li, t->region, is_short, buf, 256);
            if (n) put_units(&item, buf, n);
            else if (subst) put_ascii(&item, t->region);
            else return 0;
        } else {
            char v[16];
            if (part - 2 < (int)t->nvar) copyz(v, sizeof v, t->var[part - 2]);
            else copyz(v, sizeof v, "posix");
            for (size_t k = 0; v[k]; k++) v[k] = (char)upper(v[k]);
            n = name_of(li, IDK_VARIANTN, v, buf, 256);
            if (n) put_units(&item, buf, n);
            else if (subst) put_ascii(&item, v);
            else return 0;
        }
        if (!any) { put_units(&rem, data(item), (uint32_t)len(item)); any = 1; }
        else {
            vector<uint16_t> j;
            simple_format(sp, spn, &rem, &item, 0, &j);
            clear(rem);
            put_units(&rem, data(j), (uint32_t)len(j));
        }
    }
    if (!any) { put_units(out, data(name), (uint32_t)len(name)); return 1; }
    /* the parentheses within the remainder bracketed (fullwidth where the pattern is) */
    uint16_t op = '(', cp = ')', ob = '[', cb = ']';
    if (units_has_char(lp, lpn, 0xFF08)) { op = 0xFF08; cp = 0xFF09; ob = 0xFF3B; cb = 0xFF3D; }
    for (size_t k = 0; k < len(rem); k++) {
        if (rem[k] == op) rem[k] = ob;
        else if (rem[k] == cp) rem[k] = cb;
    }
    simple_format(lp, lpn, &name, &rem, 0, out);
    return 1;
}

static int is_region_code(const char *s, size_t n) { return (n == 2 && all_of(s, 2, 0)) || (n == 3 && all_of(s, 3, 1)); }

static jv dn_of(js_vm *vm, uint32_t o, jv codev) {
    uint32_t cs = js_to_string(vm, codev);
    if (!cs) return JV_HOLE;
    int type = slot_int(vm, o, DNS_TYPE), li = slot_int(vm, o, DNS_DATALOC);
    int style = slot_int(vm, o, DNS_STYLE), subst = slot_int(vm, o, DNS_FALLBACK) == 0;
    int is_short = style != 2;
    char code[TAG_MAX + 1];
    int n = str_ascii(vm, cs, code, sizeof code);
    uint16_t buf[256];
    uint32_t bn = 0;
    if (type == DNT_LANGUAGE) {
        /* a unicode_language_id alone (no extension, no private use), then canonical */
        itag t;
        int bad = n < 0 || !starts_with_language_id(code, (size_t)n);
        if (!bad) {
            char lc[TAG_MAX + 1];
            for (int k = 0; k < n; k++) lc[k] = (char)lower((unsigned char)code[k]);
            lc[n] = 0;
            bad = tag_parse_lc(lc, (size_t)n, &t) != 0;
        }
        if (!bad) bad = t.has_u || t.tx[0] || t.next || t.priv[0];
        if (bad) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        tag_canon(&t);
        vector<uint16_t> out;
        if (!dn_language(li, &t, is_short, slot_int(vm, o, DNS_LANGDISPLAY) == 0, subst, &out)) return JV_UNDEF;
        return jv_str(js_str_new(vm, data(out), (uint32_t)len(out)));
    }
    if (type == DNT_REGION) {
        if (n < 0 || !is_region_code(code, (size_t)n)) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        bn = region_name(li, code, is_short, buf, 256);
    } else if (type == DNT_SCRIPT) {
        if (n != 4 || !all_of(code, 4, 0)) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        bn = name_styled(li, IDK_SCRIPTN, code, is_short, buf, 256);
    } else if (type == DNT_CURRENCY) {
        if (n != 3 || !all_of(code, 3, 0)) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        char up[4];
        for (int k = 0; k < 3; k++) up[k] = (char)upper(code[k]);
        up[3] = 0;
        bn = name_of(li, IDK_CURN, up, buf, 256);
    } else if (type == DNT_CALENDAR) {
        if (n < 0 || !is_type(code)) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        char key[TAG_MAX + 1];
        copyz(key, sizeof key, code);
        if (!strcmp(code, "gregory")) copyz(key, sizeof key, "gregorian");
        else if (!strcmp(code, "ethioaa")) copyz(key, sizeof key, "ethiopic-amete-alem");
        bn = name_of(li, IDK_CALN, key, buf, 256);
    } else {
        static const char *const fields[] = { "era", "year", "quarter", "month", "weekOfYear", "weekday", "day", "dayPeriod",
            "hour", "minute", "second", "timeZoneName", 0 };
        int f = -1;
        for (int k = 0; fields[k] && n > 0; k++) if (!strcmp(code, fields[k])) f = k;
        if (f < 0) return js_throw_msg(vm, vm->range_error_proto, "invalid_argument");
        uint32_t w = 0;
        if (style == 1) w = 1;
        else if (style == 0) w = 2;
        for (int k = (int)w; k >= 0 && !bn; k--) bn = fld_units(rec_get(li, RKEY(IDK_DTFIELD, (uint32_t)k)), (uint32_t)f, buf, 256);
        return jv_str(js_str_new(vm, buf, bn));
    }
    if (bn) return jv_str(js_str_new(vm, buf, bn));
    if (subst) return jv_str(cs);
    return JV_UNDEF;
}

/* ==== Intl.Segmenter (ECMA-402 18) ==== */

enum { SGS_LOCALE, SGS_GRANULARITY, SGS_N };
enum { SGM_SEGMENTER, SGM_STRING, SGM_BOUNDS, SGM_N };
enum { SGI_SEGMENTS, SGI_POS, SGI_N };
static const char *const sg_names[] = { "grapheme", "word", "sentence", 0 };

static jv sg_new(js_vm *vm, jv locales, jv options, jv newt) {
    if (newt == JV_UNDEF || newt == JV_HOLE) return requires_new(vm, "Intl.Segmenter");
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    jv opts = options_object_strict(vm, options);
    if (opts == JV_HOLE) return JV_HOLE;
    const char *svc = "Intl.Segmenter";
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_GENERAL, found, sizeof found, &t);
    int g = get_option_str(vm, opts, "granularity", sg_names, svc, 0);
    if (g == -2) return JV_HOLE;
    if (g < 0) g = 0;
    int ok;
    uint32_t o = service_new(vm, newt, JIK_SEGMENTER, SGS_N, &ok);
    if (!ok) return JV_HOLE;
    slot_set(vm, o, SGS_LOCALE, jv_str(js_str_ascii(vm, found)));
    slot_set(vm, o, SGS_GRANULARITY, jv_num(g));
    return jv_obj(o);
}
static jv sg_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, SGS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "granularity"), jv_str(js_str_ascii(vm, sg_names[slot_int(vm, o, SGS_GRANULARITY)])), 0);
    return jv_obj(r);
}

/* a break property: the ranges' value for a code point (0 outside them) */
static uint32_t break_class(uint32_t table, uint32_t count, uint32_t cp) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, at = table + mid * 9;
        if (cp < db32(at)) hi = mid;
        else if (cp > db32(at + 4)) lo = mid + 1;
        else return db8(at + 8);
    }
    return 0;
}
/* the grapheme, word and sentence classes (tools/gen-intl.py's lists) */
enum { GB_OTHER, GB_CR, GB_LF, GB_CONTROL, GB_EXTEND, GB_ZWJ, GB_RI, GB_PREPEND, GB_SPACINGMARK, GB_L, GB_V, GB_T, GB_LV, GB_LVT };
enum { WB_OTHER, WB_CR, WB_LF, WB_NEWLINE, WB_EXTEND, WB_ZWJ, WB_RI, WB_FORMAT, WB_KATAKANA, WB_HL, WB_ALETTER, WB_SQ, WB_DQ,
       WB_MIDNUMLET, WB_MIDLETTER, WB_MIDNUM, WB_NUMERIC, WB_ENL, WB_WSEGSPACE };
enum { SB_OTHER, SB_CR, SB_LF, SB_EXTEND, SB_SEP, SB_FORMAT, SB_SP, SB_LOWER, SB_UPPER, SB_OLETTER, SB_NUMERIC, SB_ATERM,
       SB_SCONTINUE, SB_STERM, SB_CLOSE };

/* a string's code points and their UTF-16 offsets (the end's last) */
static void code_points(js_vm *vm, uint32_t s, vector<uint32_t> *cps, vector<uint32_t> *offs) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    uint32_t i = 0;
    while (i < n) {
        uint32_t c = vm->units[o + i], w = 1;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i + 1] - 0xDC00);
            w = 2;
        }
        push_back(*cps, c);
        push_back(*offs, i);
        i += w;
    }
    push_back(*offs, n);
}

/* UAX #29's extended grapheme clusters: whether there is a break before code point i */
static void grapheme_bounds(vector<uint32_t> *cps, vector<uint8_t> *brk) {
    size_t n = len(*cps);
    int ep = 0, incb = 0, ri = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = break_class(IDB_GCB, IDB_GCB_N, (*cps)[i]);
        uint32_t c = v & 15, ext = (v >> 4) & 1, ib = (v >> 5) & 3;
        int b = 1;
        if (i == 0) b = 1;
        else {
            uint32_t a = prev & 15;
            if (a == GB_CR && c == GB_LF) b = 0;
            else if (a == GB_CONTROL || a == GB_CR || a == GB_LF) b = 1;
            else if (c == GB_CONTROL || c == GB_CR || c == GB_LF) b = 1;
            else if (a == GB_L && (c == GB_L || c == GB_V || c == GB_LV || c == GB_LVT)) b = 0;
            else if ((a == GB_LV || a == GB_V) && (c == GB_V || c == GB_T)) b = 0;
            else if ((a == GB_LVT || a == GB_T) && c == GB_T) b = 0;
            else if (c == GB_EXTEND || c == GB_ZWJ) b = 0;
            else if (c == GB_SPACINGMARK) b = 0;
            else if (a == GB_PREPEND) b = 0;
            else if (ib == 2 && incb == 2) b = 0;
            else if (a == GB_ZWJ && ext && ep == 2) b = 0;
            else if (a == GB_RI && c == GB_RI && (ri & 1)) b = 0;
        }
        push_back(*brk, (uint8_t)b);
        /* the states through this code point */
        if (ext) ep = 1;
        else if (ep == 1 && c == GB_EXTEND) ep = 1;
        else if (ep == 1 && c == GB_ZWJ) ep = 2;
        else ep = 0;
        if (ib == 2) incb = 1;
        else if (incb && ib == 1) incb = 2;
        else if (incb && ib == 3) { /* kept */ }
        else incb = 0;
        if (c == GB_RI) ri++;
        else ri = 0;
        prev = v;
    }
}

static int ahl(uint32_t c) { return c == WB_ALETTER || c == WB_HL; }
static int midnumletq(uint32_t c) { return c == WB_MIDNUMLET || c == WB_SQ; }
static int wb_ignored(uint32_t c) { return c == WB_EXTEND || c == WB_FORMAT || c == WB_ZWJ; }

/* UAX #29's word boundaries */
static void word_bounds(vector<uint32_t> *cps, vector<uint8_t> *brk, vector<uint8_t> *cls) {
    size_t n = len(*cps);
    vector<uint8_t> ep;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = break_class(IDB_WB, IDB_WB_N, (*cps)[i]);
        push_back(*cls, (uint8_t)(v & 31));
        push_back(ep, (uint8_t)((v >> 5) & 1));
    }
    int ri = 0;   /* regional indicators in the run before (WB4's ignored skipped) */
    for (size_t i = 0; i < n; i++) {
        uint32_t c = (*cls)[i];
        int b = 1;
        if (i > 0) {
            uint32_t a = (*cls)[i - 1];
            /* the classes around, Extend, Format and ZWJ skipped (WB4) */
            int l = (int)i - 1;
            while (l >= 0 && wb_ignored((*cls)[l])) l--;
            int ll = l - 1;
            while (ll >= 0 && wb_ignored((*cls)[ll])) ll--;
            size_t r2 = i + 1;
            while (r2 < n && wb_ignored((*cls)[r2])) r2++;
            uint32_t L = WB_OTHER, LL = WB_OTHER, RR = WB_OTHER;
            int hasl = l >= 0, hasll = ll >= 0, hasrr = r2 < n;
            if (hasl) L = (*cls)[l];
            if (hasll) LL = (*cls)[ll];
            if (hasrr) RR = (*cls)[r2];
            if (a == WB_CR && c == WB_LF) b = 0;
            else if (a == WB_NEWLINE || a == WB_CR || a == WB_LF) b = 1;
            else if (c == WB_NEWLINE || c == WB_CR || c == WB_LF) b = 1;
            else if (a == WB_ZWJ && ep[i]) b = 0;
            else if (a == WB_WSEGSPACE && c == WB_WSEGSPACE) b = 0;
            else if (wb_ignored(c)) b = 0;
            else if (!hasl) b = 1;
            else if (ahl(L) && ahl(c)) b = 0;
            else if (ahl(L) && (c == WB_MIDLETTER || midnumletq(c)) && hasrr && ahl(RR)) b = 0;
            else if (hasll && ahl(LL) && (L == WB_MIDLETTER || midnumletq(L)) && ahl(c)) b = 0;
            else if (L == WB_HL && c == WB_SQ) b = 0;
            else if (L == WB_HL && c == WB_DQ && hasrr && RR == WB_HL) b = 0;
            else if (hasll && LL == WB_HL && L == WB_DQ && c == WB_HL) b = 0;
            else if (L == WB_NUMERIC && c == WB_NUMERIC) b = 0;
            else if (ahl(L) && c == WB_NUMERIC) b = 0;
            else if (L == WB_NUMERIC && ahl(c)) b = 0;
            else if (hasll && LL == WB_NUMERIC && (L == WB_MIDNUM || midnumletq(L)) && c == WB_NUMERIC) b = 0;
            else if (L == WB_NUMERIC && (c == WB_MIDNUM || midnumletq(c)) && hasrr && RR == WB_NUMERIC) b = 0;
            else if (L == WB_KATAKANA && c == WB_KATAKANA) b = 0;
            else if ((ahl(L) || L == WB_NUMERIC || L == WB_KATAKANA || L == WB_ENL) && c == WB_ENL) b = 0;
            else if (L == WB_ENL && (ahl(c) || c == WB_NUMERIC || c == WB_KATAKANA)) b = 0;
            else if (L == WB_RI && c == WB_RI && (ri & 1)) b = 0;
        }
        push_back(*brk, (uint8_t)b);
        if (c == WB_RI) ri++;
        else if (!wb_ignored(c)) ri = 0;
    }
}

static int sb_ignored(uint32_t c) { return c == SB_EXTEND || c == SB_FORMAT; }
static int parasep(uint32_t c) { return c == SB_SEP || c == SB_CR || c == SB_LF; }

/* SB8's look ahead: anything but a letter, a separator or a terminator, then a lower */
static int sb8_lower(vector<uint8_t> *cls, size_t i) {
    size_t n = len(*cls), j = i;
    while (j < n) {
        uint32_t c = (*cls)[j];
        if (c == SB_OLETTER || c == SB_UPPER || c == SB_LOWER || parasep(c) || c == SB_ATERM || c == SB_STERM) break;
        j++;
    }
    return j < n && (*cls)[j] == SB_LOWER;
}

/* UAX #29's sentence boundaries */
static void sentence_bounds(vector<uint32_t> *cps, vector<uint8_t> *brk) {
    size_t n = len(*cps);
    vector<uint8_t> cls;
    for (size_t i = 0; i < n; i++) push_back(cls, (uint8_t)break_class(IDB_SB, IDB_SB_N, (*cps)[i]));
    for (size_t i = 0; i < n; i++) {
        uint32_t c = cls[i];
        int b = 1;
        if (i > 0) {
            uint32_t a = cls[i - 1];
            int l = (int)i - 1;
            while (l >= 0 && sb_ignored(cls[l])) l--;
            int ll = l - 1;
            while (ll >= 0 && sb_ignored(cls[ll])) ll--;
            uint32_t L = SB_OTHER, LL = SB_OTHER;
            if (l >= 0) L = cls[l];
            if (ll >= 0) LL = cls[ll];
            /* SATerm Close* Sp* before: its terminator, and whether spaces follow it */
            int k = l, nsp = 0, term = -1;
            while (k >= 0 && (cls[k] == SB_SP || sb_ignored(cls[k]))) { if (cls[k] == SB_SP) nsp++; k--; }
            while (k >= 0 && (cls[k] == SB_CLOSE || sb_ignored(cls[k]))) k--;
            if (k >= 0 && (cls[k] == SB_ATERM || cls[k] == SB_STERM)) term = cls[k];
            if (a == SB_CR && c == SB_LF) b = 0;
            else if (parasep(a)) b = 1;
            else if (sb_ignored(c)) b = 0;
            else if (L == SB_ATERM && c == SB_NUMERIC) b = 0;
            else if ((LL == SB_UPPER || LL == SB_LOWER) && L == SB_ATERM && c == SB_UPPER) b = 0;
            else if (term == SB_ATERM && sb8_lower(&cls, i)) b = 0;
            else if (term >= 0 && (c == SB_SCONTINUE || c == SB_ATERM || c == SB_STERM)) b = 0;
            else if (term >= 0 && nsp == 0 && (c == SB_CLOSE || c == SB_SP || parasep(c))) b = 0;
            else if (term >= 0 && (c == SB_SP || parasep(c))) b = 0;
            else if (term >= 0) b = 1;
            else b = 0;
        }
        push_back(*brk, (uint8_t)b);
    }
}

/* the boundaries of a string at a granularity (UTF-16 offsets, the end's last), and each
   segment's word-likeness */
static void segment_bounds(js_vm *vm, uint32_t s, int g, vector<uint32_t> *bounds, vector<uint8_t> *wordlike) {
    vector<uint32_t> cps, offs;
    code_points(vm, s, &cps, &offs);
    vector<uint8_t> brk, cls;
    if (g == 0) grapheme_bounds(&cps, &brk);
    else if (g == 1) word_bounds(&cps, &brk, &cls);
    else sentence_bounds(&cps, &brk);
    size_t n = len(cps);
    vector<uint32_t> starts;
    for (size_t i = 0; i < n; i++) if (brk[i]) { push_back(*bounds, offs[i]); push_back(starts, (uint32_t)i); }
    push_back(*bounds, offs[n]);
    push_back(starts, (uint32_t)n);
    if (g != 1) return;
    /* a word: its letters, numbers, kana or ideographs (ICU's rule statuses 100 to 400) */
    for (size_t k = 0; k + 1 < len(starts); k++) {
        int like = 0, enl = 0;
        for (uint32_t i = starts[k]; i < starts[k + 1]; i++) {
            uint32_t c = cls[i], cp = cps[i];
            if (ahl(c) || c == WB_NUMERIC || c == WB_KATAKANA || (cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x3400 && cp <= 0x4DBF) ||
                (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x3FFFF)) like = 1;
            if (c == WB_ENL) enl++;
        }
        push_back(*wordlike, (uint8_t)(like || enl >= 2));
    }
}

static jv sg_segment(js_vm *vm, uint32_t o, jv sv) {
    uint32_t s = js_to_string(vm, sv);
    if (!s) return JV_HOLE;
    uint32_t m = intl_new(vm, JIK_SEGMENTS, vm->intl_protos[JIK_SEGMENTS], SGM_N);
    slot_set(vm, m, SGM_SEGMENTER, jv_obj(o));
    slot_set(vm, m, SGM_STRING, jv_str(s));
    return jv_obj(m);
}
/* the boundaries of a Segments object (made once): an array of their count, the offsets,
   then the word-likeness of each segment */
static uint32_t sg_bounds(js_vm *vm, uint32_t m, int *g) {
    *g = slot_int(vm, jv_idx(slot_get(vm, m, SGM_SEGMENTER)), SGS_GRANULARITY);
    jv b = slot_get(vm, m, SGM_BOUNDS);
    if (jv_is_obj(b)) return jv_idx(b);
    vector<uint32_t> bounds;
    vector<uint8_t> wl;
    segment_bounds(vm, jv_idx(slot_get(vm, m, SGM_STRING)), *g, &bounds, &wl);
    uint32_t arr = js_array_new(vm, 0);
    js_array_push(vm, arr, jv_num((double)len(bounds)));
    for (size_t i = 0; i < len(bounds); i++) js_array_push(vm, arr, jv_num(bounds[i]));
    for (size_t i = 0; i < len(wl); i++) js_array_push(vm, arr, jv_num(wl[i]));
    slot_set(vm, m, SGM_BOUNDS, jv_obj(arr));
    return arr;
}
/* the segment data of segment k */
static jv sg_data(js_vm *vm, uint32_t m, uint32_t arr, int g, uint32_t nb, uint32_t k) {
    uint32_t a = (uint32_t)jv_dbl(js_array_get(vm, arr, 1 + k)), b = (uint32_t)jv_dbl(js_array_get(vm, arr, 2 + k));
    uint32_t s = jv_idx(slot_get(vm, m, SGM_STRING));
    js_str_flatten(vm, s);
    vector<uint16_t> seg;
    for (uint32_t i = a; i < b; i++) push_back(seg, vm->units[vm->strs[s].off + i]);
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "segment"), jv_str(js_str_new(vm, data(seg), b - a)), 0);
    js_define(vm, r, js_atom_ascii(vm, "index"), jv_num(a), 0);
    js_define(vm, r, js_atom_ascii(vm, "input"), jv_str(s), 0);
    if (g == 1) {
        jv w = JV_FALSE;
        if (jv_dbl(js_array_get(vm, arr, 1 + nb + k)) != 0) w = JV_TRUE;
        js_define(vm, r, js_atom_ascii(vm, "isWordLike"), w, 0);
    }
    return jv_obj(r);
}
static jv sg_containing(js_vm *vm, uint32_t m, jv iv) {
    double d = js_to_number(vm, iv);
    if (vm->throwing) return JV_HOLE;
    if (d != d) d = 0;
    d = trunc(d);
    uint32_t s = jv_idx(slot_get(vm, m, SGM_STRING));
    if (d < 0 || d >= (double)vm->strs[s].len) return JV_UNDEF;
    int g;
    uint32_t arr = sg_bounds(vm, m, &g);
    uint32_t nb = (uint32_t)jv_dbl(js_array_get(vm, arr, 0));
    uint32_t idx = (uint32_t)d;
    for (uint32_t k = 0; k + 1 < nb; k++)
        if ((uint32_t)jv_dbl(js_array_get(vm, arr, 2 + k)) > idx) return sg_data(vm, m, arr, g, nb, k);
    return JV_UNDEF;
}
static jv sg_iterator(js_vm *vm, uint32_t m) {
    uint32_t it = intl_new(vm, JIK_SEGMENT_ITERATOR, vm->intl_protos[JIK_SEGMENT_ITERATOR], SGI_N);
    slot_set(vm, it, SGI_SEGMENTS, jv_obj(m));
    slot_set(vm, it, SGI_POS, jv_num(0));
    return jv_obj(it);
}
static jv sg_next(js_vm *vm, uint32_t it) {
    uint32_t m = jv_idx(slot_get(vm, it, SGI_SEGMENTS));
    int g;
    uint32_t arr = sg_bounds(vm, m, &g);
    uint32_t nb = (uint32_t)jv_dbl(js_array_get(vm, arr, 0));
    uint32_t k = (uint32_t)slot_int(vm, it, SGI_POS);
    if (k + 1 >= nb) return js_iter_result(vm, JV_UNDEF, 1);
    slot_set(vm, it, SGI_POS, jv_num(k + 1));
    return js_iter_result(vm, sg_data(vm, m, arr, g, nb, k), 0);
}

/* ==== Intl.Collator (ECMA-402 10) and String.prototype.localeCompare ==== */

/* the slots; the sensitivities and the case orders */
enum { CLS_LOCALE, CLS_USAGE, CLS_SENS, CLS_IGNP, CLS_COLL, CLS_NUMERIC, CLS_CASEFIRST, CLS_BOUND, CLS_N };
static const char *const cl_usages[] = { "sort", "search", 0 };
static const char *const cl_sens[] = { "base", "accent", "case", "variant", 0 };
static const char *const cl_casefirst[] = { "upper", "lower", "false", 0 };
enum { CF_UPPER, CF_LOWER, CF_OFF };

/* a collation's -u- keyword kept: co a collation of the language's (not standard or
   search), kn and kf their values */
static int cl_collation_ok(const char *lang, const char *co) {
    if (!co[0] || !strcmp(co, "standard") || !strcmp(co, "search")) return 0;
    uint32_t c = db_find_z(IDB_COLLATIONS, IDB_COLLATIONS_N, lang);
    char list[128];
    list[0] = 0;
    if (c) db_copy(c, list, sizeof list);
    cat(list, sizeof list, " emoji eor");
    size_t n = strlen(co), at = 0;
    while (list[at]) {
        while (list[at] == ' ') at++;
        size_t k = 0;
        while (list[at + k] && list[at + k] != ' ') k++;
        if (k == n && !strncmp(list + at, co, n)) return 1;
        at += k;
    }
    return 0;
}

static jv cl_new(js_vm *vm, jv locales, jv options, jv newt) {
    uint32_t req = canonical_list(vm, locales);
    if (!req) return JV_HOLE;
    const char *svc = "Intl.Collator";
    jv opts = options_object(vm, options, svc);
    if (opts == JV_HOLE) return JV_HOLE;
    int usage = get_option_str(vm, opts, "usage", cl_usages, svc, 0);
    if (usage == -2) return JV_HOLE;
    if (usage < 0) usage = 0;
    if (get_matcher(vm, opts, svc) == -2) return JV_HOLE;
    uint32_t cs = 0;
    char co_opt[16];
    co_opt[0] = 0;
    int r = get_option_str(vm, opts, "collation", 0, svc, &cs);
    if (r == -2) return JV_HOLE;
    if (r >= 0) {
        char b[80];
        int n = str_ascii(vm, cs, b, sizeof b);
        if (n < 0 || !is_type(b)) {
            vector<char> m;
            const char *a = "Invalid collation : ";
            append_range(m, a, strlen(a));
            js_str_to_utf8(vm, cs, &m);
            return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
        }
        if (n < 16) copyz(co_opt, sizeof co_opt, b);
    }
    int numeric = get_option_bool(vm, opts, "numeric");
    if (numeric == -2) return JV_HOLE;
    int cf = get_option_str(vm, opts, "caseFirst", cl_casefirst, svc, 0);
    if (cf == -2) return JV_HOLE;
    char found[TAG_MAX + 1];
    itag t;
    resolve_locale(vm, req, AV_COLLATOR, found, sizeof found, &t);
    char lang[9];
    copyz(lang, sizeof lang, found);
    char *dash = strchr(lang, '-');
    if (dash) *dash = 0;
    /* the keywords: kn and kf kept whatever the options say, co the option's when it is a
       collation of the language (V8's locale string) */
    char kco[16], kkn[8], kkf[8];
    kco[0] = kkn[0] = kkf[0] = 0;
    int kw = find_kw(&t, "co");
    if (kw >= 0 && strlen(t.val[kw]) < 16 && cl_collation_ok(lang, t.val[kw])) copyz(kco, sizeof kco, t.val[kw]);
    kw = find_kw(&t, "kn");
    if (kw >= 0 && (!t.val[kw][0] || !strcmp(t.val[kw], "true") || !strcmp(t.val[kw], "false"))) {
        if (t.val[kw][0]) copyz(kkn, sizeof kkn, t.val[kw]);
        else copyz(kkn, sizeof kkn, "true");
    }
    kw = find_kw(&t, "kf");
    if (kw >= 0 && (!strcmp(t.val[kw], "upper") || !strcmp(t.val[kw], "lower") || !strcmp(t.val[kw], "false"))) copyz(kkf, sizeof kkf, t.val[kw]);
    char co[16];
    co[0] = 0;
    if (co_opt[0] && cl_collation_ok(lang, co_opt)) copyz(kco, sizeof kco, co_opt);
    copyz(co, sizeof co, kco);
    int num = 0;
    if (numeric >= 0) num = numeric;
    else if (kkn[0]) num = !strcmp(kkn, "true");
    /* the case order: the option, the keyword, else the language's (Danish's upper) */
    int casefirst = CF_OFF;
    if (!strcmp(lang, "da")) casefirst = CF_UPPER;
    if (cf >= 0) casefirst = cf;
    else if (kkf[0]) {
        for (int i = 0; cl_casefirst[i]; i++) if (!strcmp(kkf, cl_casefirst[i])) casefirst = i;
    }
    int sens = get_option_str(vm, opts, "sensitivity", cl_sens, svc, 0);
    if (sens == -2) return JV_HOLE;
    if (sens < 0) sens = 3;
    int ignp = get_option_bool(vm, opts, "ignorePunctuation");
    if (ignp == -2) return JV_HOLE;
    if (ignp < 0) ignp = !strcmp(lang, "th");
    char loc[TAG_MAX + 1];
    copyz(loc, sizeof loc, found);
    if (kco[0] || kkf[0] || kkn[0]) {
        cat(loc, sizeof loc, "-u");
        if (kco[0]) { cat(loc, sizeof loc, "-co-"); cat(loc, sizeof loc, kco); }
        if (kkf[0]) { cat(loc, sizeof loc, "-kf-"); cat(loc, sizeof loc, kkf); }
        if (kkn[0]) {
            cat(loc, sizeof loc, "-kn");
            if (strcmp(kkn, "true")) { cat(loc, sizeof loc, "-"); cat(loc, sizeof loc, kkn); }
        }
    }
    int ok;
    uint32_t o = service_new(vm, newt, JIK_COLLATOR, CLS_N, &ok);
    if (!ok) return JV_HOLE;
    slot_set(vm, o, CLS_LOCALE, jv_str(js_str_ascii(vm, loc)));
    slot_set(vm, o, CLS_USAGE, jv_num(usage));
    slot_set(vm, o, CLS_SENS, jv_num(sens));
    slot_set(vm, o, CLS_IGNP, jv_num(ignp));
    slot_set(vm, o, CLS_COLL, jv_str(js_str_ascii(vm, co)));
    slot_set(vm, o, CLS_NUMERIC, jv_num(num));
    slot_set(vm, o, CLS_CASEFIRST, jv_num(casefirst));
    return jv_obj(o);
}
static jv cl_resolved(js_vm *vm, uint32_t o) {
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, r, js_atom_ascii(vm, "locale"), slot_get(vm, o, CLS_LOCALE), 0);
    js_define(vm, r, js_atom_ascii(vm, "usage"), jv_str(js_str_ascii(vm, cl_usages[slot_int(vm, o, CLS_USAGE)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "sensitivity"), jv_str(js_str_ascii(vm, cl_sens[slot_int(vm, o, CLS_SENS)])), 0);
    js_define(vm, r, js_atom_ascii(vm, "ignorePunctuation"), slot_int(vm, o, CLS_IGNP) ? JV_TRUE : JV_FALSE, 0);
    jv co = slot_get(vm, o, CLS_COLL);
    if (!vm->strs[jv_idx(co)].len) co = jv_str(js_str_ascii(vm, "default"));
    js_define(vm, r, js_atom_ascii(vm, "collation"), co, 0);
    js_define(vm, r, js_atom_ascii(vm, "numeric"), slot_int(vm, o, CLS_NUMERIC) ? JV_TRUE : JV_FALSE, 0);
    js_define(vm, r, js_atom_ascii(vm, "caseFirst"), jv_str(js_str_ascii(vm, cl_casefirst[slot_int(vm, o, CLS_CASEFIRST)])), 0);
    return jv_obj(r);
}

/* ---- the root collation (CLDR's DUCET): a string's collation elements ---- */

typedef struct cel { uint32_t p, s, t, var; } cel;
static void cel_at(uint32_t i, cel *e) {
    uint32_t a = IDB_COLL_CES + i * 6;
    e->p = db16(a);
    e->s = db16(a + 2);
    e->t = db8(a + 4);
    e->var = db8(a + 5);
}
/* a code point's elements: their count (0 none), *off the first's index */
static uint32_t coll_single(uint32_t cp, uint32_t *off) {
    uint32_t lo = 0, hi = IDB_COLL_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, w = db32(IDB_COLL + mid * 8), c = w & 0xFFFFFF;
        if (c == cp) { *off = db32(IDB_COLL + mid * 8 + 4); return w >> 24; }
        if (c < cp) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
/* a contraction of three code points (the third 0 for two): its count, *off its first's */
static uint32_t coll_contraction(uint32_t a, uint32_t b, uint32_t c, uint32_t *off) {
    uint32_t lo = 0, hi = IDB_COLLX_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, at = IDB_COLLX + mid * 16;
        uint32_t x = db32(at), y = db32(at + 4), z = db32(at + 8);
        int cmp = 0;
        if (x != a) cmp = x < a ? -1 : 1;
        else if (y != b) cmp = y < b ? -1 : 1;
        else if (z != c) cmp = z < c ? -1 : 1;
        if (!cmp) {
            uint32_t w = db32(at + 12);
            *off = w & 0xFFFFFF;
            return w >> 24;
        }
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
static void push_cels(vector<cel> *out, uint32_t off, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        cel e;
        cel_at(off + k, &e);
        push_back(*out, e);
    }
}
static void push_cel(vector<cel> *out, uint32_t p, uint32_t s, uint32_t t) {
    cel e;
    e.p = p;
    e.s = s;
    e.t = t;
    e.var = 0;
    push_back(*out, e);
}
/* a code point the table lacks: UCA's implicit weights (Han's bases first) */
static void implicit_cels(uint32_t cp, vector<cel> *out) {
    uint32_t base = 0xFBC0;
    if (cp >= 0x4E00 && cp <= 0x9FFF) base = 0xFB40;
    else if ((cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x3FFFF)) base = 0xFB80;
    push_cel(out, base + (cp >> 15), 0x20, 0x02);
    push_cel(out, (cp & 0x7FFF) | 0x8000, 0, 0);
}
/* a decimal digit's value (ASCII's and the numbering systems'), -1 none */
static int digit_value(uint32_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c < 0x660 || !is_nd(c)) return -1;
    for (uint32_t i = 0; i < IDB_NUMBERING_N; i++) {
        uint32_t z = ns_digit((int)i, 0);
        if (c >= z && c <= z + 9) return (int)(c - z);
    }
    return -1;
}
/* a string's elements; with numeric, a run of digits one number (its significant digits'
   count, then each digit, after the zero's primary) */
static void coll_elements(vector<uint32_t> *cps, int numeric, vector<cel> *out) {
    size_t n = len(*cps);
    uint32_t zero_off = 0;
    uint32_t zero_p = 0;
    if (coll_single('0', &zero_off)) {
        cel z;
        cel_at(zero_off, &z);
        zero_p = z.p;
    }
    for (size_t i = 0; i < n;) {
        uint32_t c = (*cps)[i], off;
        if (numeric && digit_value(c) >= 0) {
            size_t j = i;
            while (j < n && digit_value((*cps)[j]) >= 0) j++;
            size_t k = i;
            while (k + 1 < j && digit_value((*cps)[k]) == 0) k++;
            push_cel(out, zero_p, 0x20, 0x02);
            push_cel(out, (uint32_t)(j - k), 0x20, 0x02);
            for (size_t q = k; q < j; q++) push_cel(out, 1 + (uint32_t)digit_value((*cps)[q]), 0x20, 0x02);
            i = j;
            continue;
        }
        if (i + 2 < n) {
            uint32_t m = coll_contraction(c, (*cps)[i + 1], (*cps)[i + 2], &off);
            if (m) { push_cels(out, off, m); i += 3; continue; }
        }
        if (i + 1 < n) {
            uint32_t m = coll_contraction(c, (*cps)[i + 1], 0, &off);
            if (m) { push_cels(out, off, m); i += 2; continue; }
        }
        uint32_t m = coll_single(c, &off);
        if (m) { push_cels(out, off, m); i++; continue; }
        if (c >= 0xAC00 && c <= 0xD7A3) {
            /* a Hangul syllable: its jamo's */
            uint32_t si = c - 0xAC00, jamo[3];
            jamo[0] = 0x1100 + si / 588;
            jamo[1] = 0x1161 + (si % 588) / 28;
            jamo[2] = 0x11A7 + si % 28;
            for (int q = 0; q < 3; q++) {
                if (q == 2 && jamo[2] == 0x11A7) break;
                uint32_t jm = coll_single(jamo[q], &off);
                if (jm) push_cels(out, off, jm);
            }
            i++;
            continue;
        }
        implicit_cels(c, out);
        i++;
    }
}

/* an element's case at the case level and in the tertiary: upper from the tertiary weights
   of capitals */
static int cel_upper(uint32_t t) {
    return t == 0x08 || t == 0x09 || t == 0x0A || t == 0x0B || t == 0x0C || t == 0x0E || t == 0x11 || t == 0x12 || t == 0x1D;
}

/* the comparison: primaries, then (accent, variant) secondaries, then (case) the case
   level, then (variant) tertiaries; ignorePunctuation shifts the variable elements away */
static int coll_compare(js_vm *vm, uint32_t o, uint32_t a, uint32_t b) {
    int sens = slot_int(vm, o, CLS_SENS), shifted = slot_int(vm, o, CLS_IGNP), numeric = slot_int(vm, o, CLS_NUMERIC);
    int cf = slot_int(vm, o, CLS_CASEFIRST);
    vector<uint32_t> ca, cb, oa, ob;
    code_points(vm, a, &ca, &oa);
    code_points(vm, b, &cb, &ob);
    vector<cel> ea, eb;
    coll_elements(&ca, numeric, &ea);
    coll_elements(&cb, numeric, &eb);
    /* the shifted elements (and the ignorables after them) zeroed */
    for (int pass = 0; pass < 2 && shifted; pass++) {
        vector<cel> *e = &ea;
        if (pass) e = &eb;
        int after = 0;
        for (size_t i = 0; i < len(*e); i++) {
            if ((*e)[i].var) { (*e)[i].p = 0; (*e)[i].s = 0; (*e)[i].t = 0; after = 1; }
            else if ((*e)[i].p == 0 && after) { (*e)[i].s = 0; (*e)[i].t = 0; }
            else if ((*e)[i].p) after = 0;
        }
    }
    for (int level = 1; level <= 4; level++) {
        if (level == 2 && sens != 1 && sens != 3) continue;
        if (level == 3 && sens != 2) continue;
        if (level == 4 && sens != 3) continue;
        size_t i = 0, j = 0;
        for (;;) {
            uint32_t x = 0, y = 0;
            while (i < len(ea)) {
                cel e = ea[i++];
                if (level == 1) x = e.p;
                else if (level == 2) x = e.s;
                else if (level == 3) { if (e.p) x = 1 + (uint32_t)(cel_upper(e.t) != (cf == CF_UPPER)); }
                else if (e.t) {
                    x = e.t;
                    if (cf == CF_UPPER) x |= (uint32_t)(!cel_upper(e.t)) << 8;
                }
                if (x) break;
            }
            while (j < len(eb)) {
                cel e = eb[j++];
                if (level == 1) y = e.p;
                else if (level == 2) y = e.s;
                else if (level == 3) { if (e.p) y = 1 + (uint32_t)(cel_upper(e.t) != (cf == CF_UPPER)); }
                else if (e.t) {
                    y = e.t;
                    if (cf == CF_UPPER) y |= (uint32_t)(!cel_upper(e.t)) << 8;
                }
                if (y) break;
            }
            if (x != y) return x < y ? -1 : 1;
            if (!x) break;
        }
    }
    return 0;
}

static jv cl_compare(js_vm *vm, uint32_t o, jv x, jv y) {
    uint32_t a = js_to_string(vm, x);
    if (!a) return JV_HOLE;
    uint32_t b = js_to_string(vm, y);
    if (!b) return JV_HOLE;
    return jv_num(coll_compare(vm, o, a, b));
}

/* String.prototype.localeCompare: a Collator of the arguments compares this with that */
static jv locale_compare(js_vm *vm, jv thisv, jv that, jv locales, jv options) {
    if (thisv == JV_UNDEF || thisv == JV_NULL)
        return js_throw_msg(vm, vm->type_error_proto, "String.prototype.localeCompare called on null or undefined");
    uint32_t s = js_to_string(vm, thisv);
    if (!s) return JV_HOLE;
    uint32_t t = js_to_string(vm, that);
    if (!t) return JV_HOLE;
    uint32_t o = 0;
    int cache = locales == JV_UNDEF && options == JV_UNDEF;
    if (cache) o = intl_cache_get(vm, IC_COLLATOR);
    if (!o) {
        jv c = cl_new(vm, locales, options, JV_UNDEF);
        if (c == JV_HOLE) return JV_HOLE;
        o = jv_idx(c);
        if (cache) intl_cache_put(vm, IC_COLLATOR, o);
    }
    return jv_num(coll_compare(vm, o, s, t));
}

/* a receiver of a service's method: itself, else the TypeError */
static jv this_of(js_vm *vm, jv thisv, uint32_t kind, const char *method) {
    if (!is_intl(vm, thisv, kind)) return incompatible(vm, method, thisv);
    return thisv;
}

/* Intl.supportedValuesOf (ECMA-402 8.3.2, as V8 has it): ICU's calendars, collations
   (its types but standard and search) and ISO currencies (ucurr's common, not deprecated),
   the numbering systems with digits, the time zones of a country (ICU's canonical
   location zones, by name), the sanctioned units */
static jv supported_values_of(js_vm *vm, jv key) {
    uint32_t ks = js_to_string(vm, key);
    if (!ks) return JV_HOLE;
    char k[32];
    int kn = str_ascii(vm, ks, k, sizeof k);
    static const char *const calendars[] = { "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic", "gregory",
        "hebrew", "indian", "islamic", "islamic-civil", "islamic-rgsa", "islamic-tbla", "islamic-umalqura", "iso8601",
        "japanese", "persian", "roc", 0 };
    static const char *const collations[] = { "compat", "dict", "emoji", "eor", "phonebk", "phonetic", "pinyin", "searchjl",
        "stroke", "trad", "unihan", "zhuyin", 0 };
    static const char currencies[] =
        "AEDAFNALLAMDANGAOAARSAUDAWGAZNBAMBBDBDTBGNBHDBIFBMDBNDBOBBRLBSDBTNBWPBYNBZDCADCDFCHFCLPCNYCOPCRCCUCCUPCVECZKDJFDKKDOP"
        "DZDEGPERNETBEURFJDFKPGBPGELGHSGIPGMDGNFGTQGYDHKDHNLHRKHTGHUFIDRILSINRIQDIRRISKJMDJODJPYKESKGSKHRKMFKPWKRWKWDKYDKZT"
        "LAKLBPLKRLRDLSLLYDMADMDLMGAMKDMMKMNTMOPMRUMURMVRMWKMXNMYRMZNNADNGNNIONOKNPRNZDOMRPABPENPGKPHPPKRPLNPYGQARRONRSDRUB"
        "RWFSARSBDSCRSDGSEKSGDSHPSLESLLSOSSRDSSPSTNSVCSYPSZLTHBTJSTMTTNDTOPTRYTTDTWDTZSUAHUGXUSDUYUUZSVESVNDVUVWSTXAFXCDXCG"
        "XDRXOFXPFXSUYERZARZMWZWGZWL";
    uint32_t out = js_array_new(vm, 0);
    const char *const *list = 0;
    if (kn > 0 && !strcmp(k, "calendar")) list = calendars;
    else if (kn > 0 && !strcmp(k, "collation")) list = collations;
    if (list) {
        for (int i = 0; list[i]; i++) js_array_push(vm, out, jv_str(js_str_ascii(vm, list[i])));
        return jv_obj(out);
    }
    if (kn > 0 && !strcmp(k, "currency")) {
        for (size_t i = 0; i + 3 <= sizeof currencies - 1; i += 3) {
            char c[4];
            copyn(c, sizeof c, currencies + i, 3);
            js_array_push(vm, out, jv_str(js_str_ascii(vm, c)));
        }
        return jv_obj(out);
    }
    if (kn > 0 && !strcmp(k, "numberingSystem")) {
        for (uint32_t i = 0; i < IDB_NUMBERING_N; i++) js_array_push(vm, out, jv_str(db_jstr(vm, db32(IDB_NUMBERING + i * 8))));
        return jv_obj(out);
    }
    if (kn > 0 && !strcmp(k, "timeZone")) {
        for (int i = 0; i < (int)IDT_ZONES_N; i++) {
            if (!(tzc_flags(i) & 1)) continue;
            char name[64];
            tzc_name(i, name, sizeof name);
            js_array_push(vm, out, jv_str(js_str_ascii(vm, name)));
        }
        return jv_obj(out);
    }
    if (kn > 0 && !strcmp(k, "unit")) {
        for (uint32_t i = 0; i < IDB_UNITS_N; i++) {
            char u[48];
            db_copy(db32(IDB_UNITS + i * 8), u, sizeof u);
            if (!strstr(u, "-per-")) js_array_push(vm, out, jv_str(js_str_ascii(vm, u)));
        }
        return jv_obj(out);
    }
    vector<char> m;
    const char *a = "Invalid key : ";
    append_range(m, a, strlen(a));
    js_str_to_utf8(vm, ks, &m);
    return js_throw(vm, jv_obj(js_error_new(vm, vm->range_error_proto, js_str_utf8(vm, data(m), len(m)))));
}

/* ---- the natives ---- */

jv js_intl_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    switch (which) {
    case IN_GETCANONICALLOCALES: {
        uint32_t l = canonical_list(vm, a0);
        return l ? jv_obj(l) : JV_HOLE;
    }
    case IN_SUPPORTEDVALUESOF: return supported_values_of(vm, a0);
    case IN_SUPPORTED: return supported_locales(vm, (uint32_t)jv_dbl(vm->objs[jv_idx(callee)].x), a0, a1);
    case IN_LOCALE: return locale_ctor(vm, a0, a1, newt);
    case IN_LOC_TOSTRING: {
        itag t;
        return locale_this(vm, thisv, &t, "toString");
    }
    case IN_LOC_MAXIMIZE: case IN_LOC_MINIMIZE: {
        itag t;
        jv s;
        if (which == IN_LOC_MAXIMIZE) s = locale_this(vm, thisv, &t, "maximize");
        else s = locale_this(vm, thisv, &t, "minimize");
        if (s == JV_HOLE) return JV_HOLE;
        return which == IN_LOC_MAXIMIZE ? locale_maximize(vm, &t) : locale_minimize(vm, &t);
    }
    case IN_LOC_GET: {
        static const char *const gn[LG_N] = { "baseName", "calendar", "caseFirst", "collation", "hourCycle", "numeric",
            "numberingSystem", "language", "script", "region", "calendars", "collations", "hourCycles", "numberingSystems",
            "textInfo", "timeZones", "weekInfo" };
        uint32_t g = (uint32_t)jv_dbl(vm->objs[jv_idx(callee)].x);
        itag t;
        jv s = locale_this(vm, thisv, &t, gn[g]);
        if (s == JV_HOLE) return JV_HOLE;
        return locale_get(vm, g, &t);
    }
    case IN_NUMBERFORMAT: return nf_new(vm, a0, a1, newt, "Intl.NumberFormat");
    case IN_NF_FORMAT_GET: {
        if (unwrap_nf(vm, thisv, "get Intl.NumberFormat.prototype.format") == JV_HOLE) return JV_HOLE;
        uint32_t nf = jv_idx(thisv);
        jv b = slot_get(vm, nf, NFS_BOUND);
        if (jv_is_obj(b)) return b;
        uint32_t first = vm->objs[jv_idx(callee)].a - which;
        uint32_t f = js_native_new(vm, first + IN_NF_FORMAT_BOUND, A_EMPTY, 1);
        vm->objs[f].x = thisv;
        slot_set(vm, nf, NFS_BOUND, jv_obj(f));
        return jv_obj(f);
    }
    case IN_NF_FORMAT_BOUND: return nf_format_value(vm, jv_idx(vm->objs[jv_idx(callee)].x), a0, 0);
    case IN_NF_FORMATTOPARTS:
        if (this_nf(vm, thisv, "Intl.NumberFormat.prototype.formatToParts") == JV_HOLE) return JV_HOLE;
        return nf_format_value(vm, jv_idx(thisv), a0, 1);
    case IN_NF_FORMATRANGE:
        if (this_nf(vm, thisv, "Intl.NumberFormat.prototype.formatRange") == JV_HOLE) return JV_HOLE;
        return nf_format_range(vm, jv_idx(thisv), a0, a1, 0);
    case IN_NF_FORMATRANGETOPARTS:
        if (this_nf(vm, thisv, "Intl.NumberFormat.prototype.formatRangeToParts") == JV_HOLE) return JV_HOLE;
        return nf_format_range(vm, jv_idx(thisv), a0, a1, 1);
    case IN_NF_RESOLVED:
        if (unwrap_nf(vm, thisv, "Intl.NumberFormat.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return nf_resolved(vm, jv_idx(thisv));
    case IN_NUM_TOLOCALE: {
        jv x = thisv;
        if (jv_is_obj(x) && vm->objs[jv_idx(x)].kind == JK_NUMBER) x = vm->objs[jv_idx(x)].x;
        if (!jv_is_num(x)) return js_throw_msg(vm, vm->type_error_proto, "Number.prototype.toLocaleString requires that 'this' be a Number");
        return number_to_locale(vm, x, a0, a1, "Number.prototype.toLocaleString");
    }
    case IN_BIG_TOLOCALE: {
        jv x = thisv;
        if (jv_is_obj(x) && vm->objs[jv_idx(x)].kind == JK_BIGINT) x = vm->objs[jv_idx(x)].x;
        if (!jv_is_big(x)) return js_throw_msg(vm, vm->type_error_proto, "BigInt.prototype.toLocaleString requires that 'this' be a BigInt");
        return number_to_locale(vm, x, a0, a1, "BigInt.prototype.toLocaleString");
    }
    case IN_DATETIMEFORMAT: return dtf_new(vm, a0, a1, newt, DQ_ANY, DD_DATE, "Intl.DateTimeFormat");
    case IN_DTF_FORMAT_GET: {
        if (unwrap_dtf(vm, thisv, "get Intl.DateTimeFormat.prototype.format") == JV_HOLE) return JV_HOLE;
        uint32_t o = jv_idx(thisv);
        jv b = slot_get(vm, o, DFS_BOUND);
        if (jv_is_obj(b)) return b;
        uint32_t first = vm->objs[jv_idx(callee)].a - which;
        uint32_t f = js_native_new(vm, first + IN_DTF_FORMAT_BOUND, A_EMPTY, 1);
        vm->objs[f].x = thisv;
        slot_set(vm, o, DFS_BOUND, jv_obj(f));
        return jv_obj(f);
    }
    case IN_DTF_FORMAT_BOUND: return dtf_format_value(vm, jv_idx(vm->objs[jv_idx(callee)].x), a0, 0);
    case IN_DTF_FORMATTOPARTS:
        if (this_dtf(vm, thisv, "Intl.DateTimeFormat.prototype.formatToParts") == JV_HOLE) return JV_HOLE;
        return dtf_format_value(vm, jv_idx(thisv), a0, 1);
    case IN_DTF_RESOLVED:
        if (unwrap_dtf(vm, thisv, "Intl.DateTimeFormat.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return dtf_resolved(vm, jv_idx(thisv));
    case IN_DTF_FORMATRANGE:
        if (this_dtf(vm, thisv, "Intl.DateTimeFormat.prototype.formatRange") == JV_HOLE) return JV_HOLE;
        return dtf_format_range(vm, jv_idx(thisv), a0, a1, 0);
    case IN_DTF_FORMATRANGETOPARTS:
        if (this_dtf(vm, thisv, "Intl.DateTimeFormat.prototype.formatRangeToParts") == JV_HOLE) return JV_HOLE;
        return dtf_format_range(vm, jv_idx(thisv), a0, a1, 1);
    case IN_COLLATOR: return cl_new(vm, a0, a1, newt);
    case IN_CL_COMPARE_GET: {
        if (this_of(vm, thisv, JIK_COLLATOR, "get Intl.Collator.prototype.compare") == JV_HOLE) return JV_HOLE;
        uint32_t o = jv_idx(thisv);
        jv b = slot_get(vm, o, CLS_BOUND);
        if (jv_is_obj(b)) return b;
        uint32_t first = vm->objs[jv_idx(callee)].a - which;
        uint32_t f = js_native_new(vm, first + IN_CL_COMPARE_BOUND, A_EMPTY, 2);
        vm->objs[f].x = thisv;
        slot_set(vm, o, CLS_BOUND, jv_obj(f));
        return jv_obj(f);
    }
    case IN_CL_COMPARE_BOUND: return cl_compare(vm, jv_idx(vm->objs[jv_idx(callee)].x), a0, a1);
    case IN_CL_RESOLVED:
        if (this_of(vm, thisv, JIK_COLLATOR, "Intl.Collator.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return cl_resolved(vm, jv_idx(thisv));
    case IN_STR_LOCALECOMPARE: return locale_compare(vm, thisv, a0, a1, js_arg(vm, args, argc, 2));
    case IN_PLURALRULES: return pr_new(vm, a0, a1, newt);
    case IN_PR_SELECT:
        if (this_of(vm, thisv, JIK_PLURALRULES, "Intl.PluralRules.prototype.select") == JV_HOLE) return JV_HOLE;
        return pr_select(vm, jv_idx(thisv), a0);
    case IN_PR_SELECTRANGE:
        if (this_of(vm, thisv, JIK_PLURALRULES, "Intl.PluralRules.prototype.selectRange") == JV_HOLE) return JV_HOLE;
        return pr_select_range(vm, jv_idx(thisv), a0, a1);
    case IN_PR_RESOLVED:
        if (this_of(vm, thisv, JIK_PLURALRULES, "Intl.PluralRules.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return pr_resolved(vm, jv_idx(thisv));
    case IN_LISTFORMAT: return lf_new(vm, a0, a1, newt);
    case IN_LF_FORMAT:
        if (this_of(vm, thisv, JIK_LISTFORMAT, "Intl.ListFormat.prototype.format") == JV_HOLE) return JV_HOLE;
        return lf_format(vm, jv_idx(thisv), a0, 0);
    case IN_LF_FORMATTOPARTS:
        if (this_of(vm, thisv, JIK_LISTFORMAT, "Intl.ListFormat.prototype.formatToParts") == JV_HOLE) return JV_HOLE;
        return lf_format(vm, jv_idx(thisv), a0, 1);
    case IN_LF_RESOLVED:
        if (this_of(vm, thisv, JIK_LISTFORMAT, "Intl.ListFormat.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return lf_resolved(vm, jv_idx(thisv));
    case IN_RELTIMEFORMAT: return rt_new(vm, a0, a1, newt);
    case IN_RT_FORMAT:
        if (this_of(vm, thisv, JIK_RELATIVETIMEFORMAT, "Intl.RelativeTimeFormat.prototype.format") == JV_HOLE) return JV_HOLE;
        return rt_format(vm, jv_idx(thisv), a0, a1, 0);
    case IN_RT_FORMATTOPARTS:
        if (this_of(vm, thisv, JIK_RELATIVETIMEFORMAT, "Intl.RelativeTimeFormat.prototype.formatToParts") == JV_HOLE) return JV_HOLE;
        return rt_format(vm, jv_idx(thisv), a0, a1, 1);
    case IN_RT_RESOLVED:
        if (this_of(vm, thisv, JIK_RELATIVETIMEFORMAT, "Intl.RelativeTimeFormat.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return rt_resolved(vm, jv_idx(thisv));
    case IN_DISPLAYNAMES: return dn_new(vm, a0, a1, newt);
    case IN_DN_OF:
        if (this_of(vm, thisv, JIK_DISPLAYNAMES, "Intl.DisplayNames.prototype.of") == JV_HOLE) return JV_HOLE;
        return dn_of(vm, jv_idx(thisv), a0);
    case IN_DN_RESOLVED:
        if (this_of(vm, thisv, JIK_DISPLAYNAMES, "Intl.DisplayNames.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return dn_resolved(vm, jv_idx(thisv));
    case IN_SEGMENTER: return sg_new(vm, a0, a1, newt);
    case IN_SG_SEGMENT:
        if (this_of(vm, thisv, JIK_SEGMENTER, "Intl.Segmenter.prototype.segment") == JV_HOLE) return JV_HOLE;
        return sg_segment(vm, jv_idx(thisv), a0);
    case IN_SG_RESOLVED:
        if (this_of(vm, thisv, JIK_SEGMENTER, "Intl.Segmenter.prototype.resolvedOptions") == JV_HOLE) return JV_HOLE;
        return sg_resolved(vm, jv_idx(thisv));
    case IN_SGM_CONTAINING:
        if (this_of(vm, thisv, JIK_SEGMENTS, "%Segments.prototype%.containing") == JV_HOLE) return JV_HOLE;
        return sg_containing(vm, jv_idx(thisv), a0);
    case IN_SGM_ITERATOR:
        if (this_of(vm, thisv, JIK_SEGMENTS, "%Segments.prototype%[@@iterator]") == JV_HOLE) return JV_HOLE;
        return sg_iterator(vm, jv_idx(thisv));
    case IN_SGI_NEXT:
        if (this_of(vm, thisv, JIK_SEGMENT_ITERATOR, "%SegmentIterator.prototype%.next") == JV_HOLE) return JV_HOLE;
        return sg_next(vm, jv_idx(thisv));
    case IN_DATE_TOLOCALE: return date_to_locale(vm, thisv, a0, a1, (uint32_t)jv_dbl(vm->objs[jv_idx(callee)].x));
    default: return JV_UNDEF;
    }
}

/* ---- the objects ---- */

static uint32_t native(js_vm *vm, uint32_t first, uint32_t id, const char *name, uint32_t nargs) {
    return js_native_new(vm, first + id, js_atom_ascii(vm, name), nargs);
}
static void method(js_vm *vm, uint32_t o, uint32_t first, uint32_t id, const char *name, uint32_t nargs) {
    js_define(vm, o, js_atom_ascii(vm, name), jv_obj(native(vm, first, id, name, nargs)), JS_SHAPE_HIDDEN);
}
static void getter(js_vm *vm, uint32_t o, uint32_t first, uint32_t id, const char *name, double x) {
    char gname[64];
    gname[0] = 0;
    cat(gname, sizeof gname, "get ");
    cat(gname, sizeof gname, name);
    uint32_t g = native(vm, first, id, gname, 0);
    vm->objs[g].x = jv_num(x);
    js_define_accessor(vm, o, js_atom_ascii(vm, name), jv_obj(g), JV_HOLE, JS_SHAPE_HIDDEN);
}
static void tag_of(js_vm *vm, uint32_t o, const char *name) {
    js_define(vm, o, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, name)), JS_SHAPE_HIDDEN);
}
/* a constructor on Intl, its prototype made, with supportedLocalesOf when it has one */
static uint32_t intl_ctor(js_vm *vm, uint32_t intl, uint32_t first, uint32_t id, uint32_t kind, const char *name, uint32_t nargs, int supported) {
    uint32_t proto = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->intl_protos[kind] = proto;
    uint32_t c = native(vm, first, id, name, nargs);
    js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_PROTO);
    js_define(vm, proto, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, intl, js_atom_ascii(vm, name), jv_obj(c), JS_SHAPE_HIDDEN);
    char tag[48];
    tag[0] = 0;
    cat(tag, sizeof tag, "Intl.");
    cat(tag, sizeof tag, name);
    tag_of(vm, proto, tag);
    if (supported) {
        uint32_t s = native(vm, first, IN_SUPPORTED, "supportedLocalesOf", 1);
        vm->objs[s].x = jv_num(kind);
        js_define(vm, c, js_atom_ascii(vm, "supportedLocalesOf"), jv_obj(s), JS_SHAPE_HIDDEN);
    }
    return c;
}

void js_intl_init(js_vm *vm, uint32_t first) {
    uint32_t intl = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    js_define(vm, vm->global, js_atom_ascii(vm, "Intl"), jv_obj(intl), JS_SHAPE_HIDDEN);
    tag_of(vm, intl, "Intl");
    method(vm, intl, first, IN_GETCANONICALLOCALES, "getCanonicalLocales", 1);
    method(vm, intl, first, IN_SUPPORTEDVALUESOF, "supportedValuesOf", 1);
    vm->intl_cache = 0;

    intl_ctor(vm, intl, first, IN_LOCALE, JIK_LOCALE, "Locale", 1, 0);
    uint32_t lp = vm->intl_protos[JIK_LOCALE];
    method(vm, lp, first, IN_LOC_TOSTRING, "toString", 0);
    method(vm, lp, first, IN_LOC_MAXIMIZE, "maximize", 0);
    method(vm, lp, first, IN_LOC_MINIMIZE, "minimize", 0);
    static const char *const gn[LG_N] = { "baseName", "calendar", "caseFirst", "collation", "hourCycle", "numeric",
        "numberingSystem", "language", "script", "region", "calendars", "collations", "hourCycles", "numberingSystems",
        "textInfo", "timeZones", "weekInfo" };
    static const uint32_t order[LG_N] = { LG_LANGUAGE, LG_SCRIPT, LG_REGION, LG_BASENAME, LG_CALENDAR, LG_CASEFIRST, LG_COLLATION,
        LG_HOURCYCLE, LG_NUMERIC, LG_NUMBERINGSYSTEM, LG_CALENDARS, LG_COLLATIONS, LG_HOURCYCLES, LG_NUMBERINGSYSTEMS, LG_TEXTINFO,
        LG_TIMEZONES, LG_WEEKINFO };
    for (uint32_t i = 0; i < LG_N; i++) getter(vm, lp, first, IN_LOC_GET, gn[order[i]], order[i]);

    /* NumberFormat, and Number's and BigInt's toLocaleString over it */
    intl_ctor(vm, intl, first, IN_NUMBERFORMAT, JIK_NUMBERFORMAT, "NumberFormat", 0, 1);
    uint32_t np = vm->intl_protos[JIK_NUMBERFORMAT];
    method(vm, np, first, IN_NF_RESOLVED, "resolvedOptions", 0);
    method(vm, np, first, IN_NF_FORMATTOPARTS, "formatToParts", 1);
    getter(vm, np, first, IN_NF_FORMAT_GET, "format", 0);
    method(vm, np, first, IN_NF_FORMATRANGE, "formatRange", 2);
    method(vm, np, first, IN_NF_FORMATRANGETOPARTS, "formatRangeToParts", 2);
    method(vm, vm->number_proto, first, IN_NUM_TOLOCALE, "toLocaleString", 0);
    method(vm, vm->bigint_proto, first, IN_BIG_TOLOCALE, "toLocaleString", 0);

    /* DateTimeFormat, and Date's toLocale*String over it */
    intl_ctor(vm, intl, first, IN_DATETIMEFORMAT, JIK_DATETIMEFORMAT, "DateTimeFormat", 0, 1);
    uint32_t dp = vm->intl_protos[JIK_DATETIMEFORMAT];
    method(vm, dp, first, IN_DTF_RESOLVED, "resolvedOptions", 0);
    method(vm, dp, first, IN_DTF_FORMATTOPARTS, "formatToParts", 1);
    getter(vm, dp, first, IN_DTF_FORMAT_GET, "format", 0);
    method(vm, dp, first, IN_DTF_FORMATRANGE, "formatRange", 2);
    method(vm, dp, first, IN_DTF_FORMATRANGETOPARTS, "formatRangeToParts", 2);
    static const char *const dn[] = { "toLocaleString", "toLocaleDateString", "toLocaleTimeString" };
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t f = native(vm, first, IN_DATE_TOLOCALE, dn[i], 0);
        vm->objs[f].x = jv_num(i);
        js_define(vm, vm->date_proto, js_atom_ascii(vm, dn[i]), jv_obj(f), JS_SHAPE_HIDDEN);
    }

    intl_ctor(vm, intl, first, IN_COLLATOR, JIK_COLLATOR, "Collator", 0, 1);
    uint32_t cp = vm->intl_protos[JIK_COLLATOR];
    method(vm, cp, first, IN_CL_RESOLVED, "resolvedOptions", 0);
    getter(vm, cp, first, IN_CL_COMPARE_GET, "compare", 0);
    method(vm, vm->string_proto, first, IN_STR_LOCALECOMPARE, "localeCompare", 1);

    intl_ctor(vm, intl, first, IN_PLURALRULES, JIK_PLURALRULES, "PluralRules", 0, 1);
    uint32_t pp = vm->intl_protos[JIK_PLURALRULES];
    method(vm, pp, first, IN_PR_RESOLVED, "resolvedOptions", 0);
    method(vm, pp, first, IN_PR_SELECT, "select", 1);
    method(vm, pp, first, IN_PR_SELECTRANGE, "selectRange", 2);

    intl_ctor(vm, intl, first, IN_RELTIMEFORMAT, JIK_RELATIVETIMEFORMAT, "RelativeTimeFormat", 0, 1);
    uint32_t rp = vm->intl_protos[JIK_RELATIVETIMEFORMAT];
    method(vm, rp, first, IN_RT_RESOLVED, "resolvedOptions", 0);
    method(vm, rp, first, IN_RT_FORMAT, "format", 2);
    method(vm, rp, first, IN_RT_FORMATTOPARTS, "formatToParts", 2);

    intl_ctor(vm, intl, first, IN_LISTFORMAT, JIK_LISTFORMAT, "ListFormat", 0, 1);
    uint32_t lp2 = vm->intl_protos[JIK_LISTFORMAT];
    method(vm, lp2, first, IN_LF_RESOLVED, "resolvedOptions", 0);
    method(vm, lp2, first, IN_LF_FORMAT, "format", 1);
    method(vm, lp2, first, IN_LF_FORMATTOPARTS, "formatToParts", 1);

    intl_ctor(vm, intl, first, IN_DISPLAYNAMES, JIK_DISPLAYNAMES, "DisplayNames", 2, 1);
    uint32_t dnp = vm->intl_protos[JIK_DISPLAYNAMES];
    method(vm, dnp, first, IN_DN_RESOLVED, "resolvedOptions", 0);
    method(vm, dnp, first, IN_DN_OF, "of", 1);

    intl_ctor(vm, intl, first, IN_SEGMENTER, JIK_SEGMENTER, "Segmenter", 0, 1);
    uint32_t sp = vm->intl_protos[JIK_SEGMENTER];
    method(vm, sp, first, IN_SG_RESOLVED, "resolvedOptions", 0);
    method(vm, sp, first, IN_SG_SEGMENT, "segment", 1);
    /* %SegmentsPrototype% and %SegmentIteratorPrototype% */
    uint32_t smp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    vm->intl_protos[JIK_SEGMENTS] = smp;
    method(vm, smp, first, IN_SGM_CONTAINING, "containing", 1);
    js_define(vm, smp, JS_KEY_ITERATOR, jv_obj(native(vm, first, IN_SGM_ITERATOR, "[Symbol.iterator]", 0)), JS_SHAPE_HIDDEN);
    uint32_t sip = js_obj_new(vm, JK_PLAIN, vm->iterator_proto);
    vm->intl_protos[JIK_SEGMENT_ITERATOR] = sip;
    method(vm, sip, first, IN_SGI_NEXT, "next", 0);
    tag_of(vm, sip, "Segmenter String Iterator");
}
