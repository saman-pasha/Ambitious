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
    /* the default, its own best match (en-US is available everywhere but plural rules') */
    if (!best_available(av, DEFAULT_LOCALE, found, cap)) copyn(found, cap, "en", 2);
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
    int mnid = number_option(vm, js_get(vm, opts, js_atom_ascii(vm, "minimumIntegerDigits")), 1, 21, 1, "minimumIntegerDigits");
    if (mnid < 0 || vm->throwing) return JV_HOLE;
    jv vmnfd = js_get(vm, opts, js_atom_ascii(vm, "minimumFractionDigits"));
    if (vm->throwing) return JV_HOLE;
    jv vmxfd = js_get(vm, opts, js_atom_ascii(vm, "maximumFractionDigits"));
    if (vm->throwing) return JV_HOLE;
    jv vmnsd = js_get(vm, opts, js_atom_ascii(vm, "minimumSignificantDigits"));
    if (vm->throwing) return JV_HOLE;
    jv vmxsd = js_get(vm, opts, js_atom_ascii(vm, "maximumSignificantDigits"));
    if (vm->throwing) return JV_HOLE;
    int inc = number_option(vm, js_get(vm, opts, js_atom_ascii(vm, "roundingIncrement")), 1, 5000, 1, "roundingIncrement");
    if (inc < 0 || vm->throwing) return JV_HOLE;
    static const int incs[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000 };
    int inc_ok = 0;
    for (int k = 0; k < 15; k++) if (incs[k] == inc) inc_ok = 1;
    if (!inc_ok) return js_throw_msg(vm, vm->range_error_proto, "roundingIncrement value is out of range.");
    int rmode = get_option_str(vm, opts, "roundingMode", rm_names, svc, 0);
    if (rmode == -2) return JV_HOLE;
    if (rmode < 0) rmode = RM_HALFEXPAND;
    int rprio = get_option_str(vm, opts, "roundingPriority", rp_names, svc, 0);
    if (rprio == -2) return JV_HOLE;
    if (rprio < 0) rprio = RP_AUTO;
    int tzd = get_option_str(vm, opts, "trailingZeroDisplay", tz_names, svc, 0);
    if (tzd == -2) return JV_HOLE;
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
        if (mnsd < 0 || vm->throwing) return JV_HOLE;
        mxsd = number_option(vm, vmxsd, mnsd, 21, 21, "maximumSignificantDigits");
        if (mxsd < 0 || vm->throwing) return JV_HOLE;
    }
    if (need_fd && has_fd) {
        int a = number_option(vm, vmnfd, 0, 100, -2, "minimumFractionDigits");
        if (a == -1 || vm->throwing) return JV_HOLE;
        int b = number_option(vm, vmxfd, 0, 100, -2, "maximumFractionDigits");
        if (b == -1 || vm->throwing) return JV_HOLE;
        if (a == -2) a = mnfd_def < b ? mnfd_def : b;
        else if (b == -2) b = mxfd_def > a ? mxfd_def : a;
        else if (a > b) return js_throw_msg(vm, vm->range_error_proto, "maximumFractionDigits value is out of range.");
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
        if (rtype != RT_FRACTION) return js_throw_msg(vm, vm->type_error_proto, "RoundingType is not fractionDigits");
        if (mxfd != mnfd) return js_throw_msg(vm, vm->range_error_proto, "maximumFractionDigits value is out of range.");
    }
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
enum { IC_NUMBER, IC_N };
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

/* ---- the natives ---- */

jv js_intl_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    switch (which) {
    case IN_GETCANONICALLOCALES: {
        uint32_t l = canonical_list(vm, a0);
        return l ? jv_obj(l) : JV_HOLE;
    }
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
    js_define(vm, c, A_PROTOTYPE, jv_obj(proto), JS_SHAPE_HIDDEN);
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
}
