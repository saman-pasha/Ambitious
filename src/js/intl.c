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

/* a locale's record in the data, by its exact tag: its index, -1 for none */
static int locale_index(const char *tag) {
    uint32_t lo = 0, hi = IDB_LOCALES_N;
    size_t n = strlen(tag);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = db_cmp(db32(IDB_LOCALES + mid * 8), tag, n);
        if (c == 0) return (int)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}
static uint32_t locale_tag_off(uint32_t li) { return db32(IDB_LOCALES + li * 8); }
static uint32_t locale_parent(uint32_t li) { return db16(IDB_LOCALES + li * 8 + 4); }
static uint32_t locale_flags(uint32_t li) { return db16(IDB_LOCALES + li * 8 + 6); }

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
        itag t2;
        if (tag_from_text(w, strlen(w), &t2)) return js_throw_msg(vm, vm->range_error_proto, "Incorrect locale information provided");
        t = t2;
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
}
