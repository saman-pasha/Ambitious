/* src/js/regexp.c -- regular expressions (ECMA-262 22.2), and Unicode's case conversions.

   A pattern is parsed to a tree, then compiled to a program of 32-bit words for a
   backtracking matcher. The matcher keeps its choices and the undo of its captures on
   a stack of its own (vm->re_stack), never on the C stack, and counts its steps into
   the script's budget: a pattern that backtracks without end stops its script as an
   endless loop would. A program is compiled once for its source and flags.

   The syntax is ES2025's with Annex B's for patterns without u or v: alternatives,
   classes with ranges and escapes, \p{...} with u or v (the general categories, the
   scripts and their extensions, the binary properties: src/js/unicode.h), greedy and
   lazy quantifiers, groups (capturing, named, not capturing), the modifiers (?ims-ims:
   ...), back references by number and name, lookahead and lookbehind (matched
   backward), and the flags d g i m s u v y. Case-insensitive matching compares the
   characters' case orbits (the characters whose canonical forms are the same: simple
   case folding with u or v, the uppercase of code units without).

   With v, a class is a set of characters and strings: nested classes, unions,
   intersections (&&) and differences (--), strings (\q{...}) and the properties of
   strings (\p{RGI_Emoji} and its kin). Its strings are matched longest first, as a
   trie: one switch on each next character, whatever the number of strings.

   A modifier changes i, m or s for the part of the pattern within it: each node of
   the tree keeps the flags it was read under, and its instruction is compiled with them.

   Then the natives: the RegExp constructor and its prototype's methods and getters,
   and String's match, matchAll, replace, replaceAll, search and split over them. */
#include <stdlib.h>
#include <math.h>
#include "js.h"
#include "unicode.h"

enum { RF_G = 1, RF_I = 2, RF_M = 4, RF_S = 8, RF_U = 16, RF_Y = 32, RF_D = 64, RF_V = 128 };

/* ---- Unicode: case mappings, orbits, properties ---- */

static uint32_t uni_case_map(const struct uni_case *t, uint32_t n, uint32_t c) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (t[mid].hi < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && t[lo].lo <= c && c <= t[lo].hi && (c - t[lo].lo) % t[lo].stride == 0) return (uint32_t)((int32_t)c + t[lo].delta);
    return c;
}
uint32_t js_uni_upper(uint32_t c) {
    if (c < 128) return c >= 'a' && c <= 'z' ? c - 32 : c;
    return uni_case_map(uni_upper, UNI_UPPER_N, c);
}
uint32_t js_uni_lower(uint32_t c) {
    if (c < 128) return c >= 'A' && c <= 'Z' ? c + 32 : c;
    return uni_case_map(uni_lower, UNI_LOWER_N, c);
}
/* a full mapping of more than one code point: how many, into out (a conditional between
   the two tables is not lowered: each is passed) */
static uint32_t uni_special_in(const struct uni_special *t, uint32_t n, uint32_t c, uint32_t *out) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (t[mid].cp < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= n || t[lo].cp != c) return 0;
    uint32_t k = 0;
    while (k < 3 && t[lo].to[k]) { out[k] = t[lo].to[k]; k++; }
    return k;
}
uint32_t js_uni_special(uint32_t c, int upper, uint32_t *out) {
    if (upper) return uni_special_in(uni_upper_special, UNI_UPPER_SPECIAL_N, c, out);
    return uni_special_in(uni_lower_special, UNI_LOWER_SPECIAL_N, c, out);
}

/* the next member of c's case orbit, c itself when it has none */
static uint32_t re_orbit_in(const uint32_t *t, uint32_t n, uint32_t c) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (t[2 * mid] < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && t[2 * lo] == c) return t[2 * lo + 1];
    return c;
}
static uint32_t re_orbit_next(uint32_t c, int uni) {
    if (uni) return re_orbit_in(uni_orbit_u, UNI_ORBIT_U_N, c);
    return re_orbit_in(uni_orbit_b, UNI_ORBIT_B_N, c);
}
/* c's orbit's least member: the same for every character a case-insensitive match takes for c */
static uint32_t re_rep(uint32_t c, int uni) {
    if (c < 128) return c >= 'a' && c <= 'z' ? c - 32 : c;
    uint32_t m = c, x = c;
    for (int k = 0; k < 8; k++) {
        x = re_orbit_next(x, uni);
        if (x == c) break;
        if (x < m) m = x;
    }
    return m;
}

/* ---- the tree ---- */

enum { RN_EMPTY, RN_CHAR, RN_ANY, RN_CLASS, RN_BOL, RN_EOL, RN_WORDB, RN_NWORDB, RN_SEQ, RN_ALT, RN_GROUP,
       RN_REPEAT, RN_BACKREF, RN_NAMEDREF, RN_LOOK, RN_STRSET };
/* CHAR a; CLASS a b (its pairs in ranges), c negated, d its case already in its set (v's);
   SEQ and ALT a b (a list); GROUP a its child, b its group (0: none); REPEAT a its child, b
   min, c max, d greedy, e f the groups within [e, f); BACKREF a; NAMEDREF a its name (in
   names); LOOK a its child, b its kind (0 ahead, 1 not ahead, 2 behind, 3 not behind);
   STRSET (a v class with strings) a b its strings' ids (in vl), c d its single characters'
   pairs (in ranges). fl: the flags i, m and s it was read under (the modifiers') */
typedef struct re_node { uint32_t kind, a, b, c, d, e, f, fl; } re_node;

#define RE_INF 0xFFFFFFFFu
#define RE_DEPTH_MAX 256u

/* the parser's errors, V8's words */
enum { RE_OK, RE_E_UNTERMINATED_GROUP, RE_E_UNMATCHED_PAREN, RE_E_NOTHING_TO_REPEAT, RE_E_LONE_QUANT, RE_E_OUT_OF_ORDER,
       RE_E_UNTERMINATED_CLASS, RE_E_RANGE_ORDER, RE_E_ESCAPE, RE_E_UNICODE_ESCAPE, RE_E_TRAILING_BACKSLASH, RE_E_GROUP,
       RE_E_GROUP_NAME, RE_E_DUP_NAME, RE_E_NAMED_REF, RE_E_PROPERTY, RE_E_CLASS_ESCAPE, RE_E_TOO_DEEP, RE_E_DECIMAL,
       RE_E_INCOMPLETE_QUANT, RE_E_TOO_LARGE, RE_E_CLASS, RE_E_CLASS_CHAR, RE_E_SET_OP, RE_E_NEG_STRINGS,
       RE_E_PROPERTY_CLASS, RE_E_NAMED_REFERENCE, RE_E_FLAG_GROUP, RE_E_REPEATED_FLAG, RE_E_MULTI_DASH, RE_E_N };
static const char *const re_errors[RE_E_N] = {
    "", "Unterminated group", "Unmatched ')'", "Nothing to repeat", "Lone quantifier brackets",
    "numbers out of order in {} quantifier", "Unterminated character class", "Range out of order in character class",
    "Invalid escape", "Invalid Unicode escape", "\\ at end of pattern", "Invalid group", "Invalid capture group name",
    "Duplicate capture group name", "Invalid named capture referenced", "Invalid property name",
    "Invalid class escape", "the pattern is nested too deeply", "Invalid decimal escape",
    "Incomplete quantifier", "the regular expression is too large", "Invalid character class",
    "Invalid character in character class", "Invalid set operation in character class",
    "Negated character class may contain strings", "Invalid property name in character class", "Invalid named reference",
    "Invalid flag group", "Repeated flag in flag group", "Multiple dashes in flag group" };

typedef struct re_parser {
    vector<uint32_t> pat;         /* the pattern's characters: code points with u, code units without */
    vector<re_node> nodes;
    vector<uint32_t> lists;
    vector<uint32_t> tmp;
    vector<uint32_t> ranges;      /* the classes' [lo, hi] pairs */
    vector<uint32_t> cls;         /* a class being read: its pairs */
    vector<uint32_t> names;       /* each named group: its group, its name's first character in namech, its length */
    vector<uint32_t> namech;
    /* the v flag's sets (re_cset): their ranges' pairs, their strings' ids; the strings, two
       words each (the first code point in vc, the count); the strings of a class being read */
    vector<uint32_t> vr;
    vector<uint32_t> vl;
    vector<uint32_t> vs;
    vector<uint32_t> vc;
    vector<uint32_t> stmp;
    vector<uint32_t> srt;         /* a sort's scratch */
    uint32_t pos, ngroups, flags, depth, total, has_names;
    uint32_t cur;                 /* the flags i, m and s where the parser is (the modifiers change them) */
    uint32_t err;
} re_parser;
#define RE_PARSER_DROP(p) vector_uint32_t_drop(&(p).pat); vector_re_node_drop(&(p).nodes); vector_uint32_t_drop(&(p).lists); \
    vector_uint32_t_drop(&(p).tmp); vector_uint32_t_drop(&(p).ranges); vector_uint32_t_drop(&(p).cls); \
    vector_uint32_t_drop(&(p).names); vector_uint32_t_drop(&(p).namech); vector_uint32_t_drop(&(p).vr); \
    vector_uint32_t_drop(&(p).vl); vector_uint32_t_drop(&(p).vs); vector_uint32_t_drop(&(p).vc); \
    vector_uint32_t_drop(&(p).stmp); vector_uint32_t_drop(&(p).srt)

static uint32_t rn_new(re_parser *p, uint32_t kind) {
    re_node n;
    memset(&n, 0, sizeof n);
    n.kind = kind;
    n.fl = p->cur;
    push_back(p->nodes, n);
    return (uint32_t)(len(p->nodes) - 1);
}
static void re_fail(re_parser *p, uint32_t e) { if (!p->err) p->err = e; }
static int64_t rp_at(re_parser *p, uint32_t k) { return p->pos + k < len(p->pat) ? (int64_t)p->pat[p->pos + k] : -1; }
static int rp_unicode(re_parser *p) { return (p->flags & (RF_U | RF_V)) != 0; }
static int rp_is(re_parser *p, uint32_t k, uint32_t c) { return rp_at(p, k) == (int64_t)c; }

static int re_digit(int64_t c) { return c >= '0' && c <= '9'; }
static int re_hexval(int64_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}
static int re_syntax_char(int64_t c) {
    return c == '^' || c == '$' || c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' || c == '(' || c == ')' ||
           c == '[' || c == ']' || c == '{' || c == '}' || c == '|' || c == '/';
}
static int re_word_char(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static int re_line_term(uint32_t c) { return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029; }

/* the groups a pattern has, counted before it is read (a back reference \N's number is one
   only up to them), and whether any is named */
static void rp_prescan(re_parser *p) {
    uint32_t n = (uint32_t)len(p->pat), groups = 0;
    int in_class = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = p->pat[i];
        if (c == '\\') { i++; goto next; }
        if (in_class) { if (c == ']') in_class = 0; goto next; }
        if (c == '[') { in_class = 1; goto next; }
        if (c != '(') goto next;
        if (i + 1 < n && p->pat[i + 1] == '?') {
            if (i + 2 < n && p->pat[i + 2] == '<' && i + 3 < n && p->pat[i + 3] != '=' && p->pat[i + 3] != '!') { groups++; p->has_names = 1; }
        } else groups++;
    next:;
    }
    p->total = groups;
}

/* ---- classes ---- */

static void cls_add(re_parser *p, uint32_t lo, uint32_t hi) { push_back(p->cls, lo); push_back(p->cls, hi); }
/* the pairs [first, first + npairs) of r into the class, or their complement up to top */
static void cls_add_ranges(re_parser *p, const uint32_t *r, uint32_t first, uint32_t npairs, int negate, uint32_t top) {
    if (!negate) { for (uint32_t i = first; i < first + npairs; i++) cls_add(p, r[2 * i], r[2 * i + 1]); return; }
    uint32_t from = 0;
    for (uint32_t i = first; i < first + npairs; i++) {
        if (r[2 * i] > from) cls_add(p, from, r[2 * i] - 1);
        from = r[2 * i + 1] + 1;
    }
    if (from <= top) cls_add(p, from, top);
}
static const uint32_t re_digits[] = { '0', '9' };
static const uint32_t re_words[] = { '0', '9', 'A', 'Z', '_', '_', 'a', 'z' };
static const uint32_t re_words_iu[] = { '0', '9', 'A', 'Z', '_', '_', 'a', 'z', 0x17F, 0x17F, 0x212A, 0x212A };
static const uint32_t re_spaces[] = { 9, 13, 32, 32, 0xA0, 0xA0, 0x1680, 0x1680, 0x2000, 0x200A, 0x2028, 0x2029, 0x202F, 0x202F,
                                      0x205F, 0x205F, 0x3000, 0x3000, 0xFEFF, 0xFEFF };

/* the class escape \d \D \s \S \w \W into the class being read */
static void cls_escape(re_parser *p, uint32_t c) {
    uint32_t top = 0x10FFFF;
    int neg = c == 'D' || c == 'S' || c == 'W';
    uint32_t lc = c | 0x20;
    if (lc == 'd') cls_add_ranges(p, re_digits, 0, 1, neg, top);
    else if (lc == 's') cls_add_ranges(p, re_spaces, 0, 10, neg, top);
    else if ((p->cur & RF_I) && rp_unicode(p)) cls_add_ranges(p, re_words_iu, 0, 6, neg, top);
    else cls_add_ranges(p, re_words, 0, 4, neg, top);
}

/* the class being read sorted and merged into the parser's ranges: its first pair and count */
static void cls_take(re_parser *p, uint32_t *off, uint32_t *n) {
    uint32_t k = (uint32_t)len(p->cls) / 2;
    /* an insertion sort by lo: classes are short, and \p's come sorted */
    for (uint32_t i = 1; i < k; i++) {
        uint32_t lo = p->cls[2 * i], hi = p->cls[2 * i + 1];
        uint32_t j = i;
        while (j > 0 && p->cls[2 * (j - 1)] > lo) { p->cls[2 * j] = p->cls[2 * (j - 1)]; p->cls[2 * j + 1] = p->cls[2 * (j - 1) + 1]; j--; }
        p->cls[2 * j] = lo;
        p->cls[2 * j + 1] = hi;
    }
    *off = (uint32_t)len(p->ranges) / 2;
    uint32_t m = 0;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t lo = p->cls[2 * i], hi = p->cls[2 * i + 1];
        if (m > 0 && lo <= p->ranges[len(p->ranges) - 1] + 1) {
            if (hi > p->ranges[len(p->ranges) - 1]) p->ranges[len(p->ranges) - 1] = hi;
        } else { push_back(p->ranges, lo); push_back(p->ranges, hi); m++; }
    }
    *n = m;
    clear(p->cls);
}

/* the property a \p{...}'s text names (the characters between the braces): its place in
   uni_props; UNI_NPROPS + k for the k-th property of strings, UNI_NPROPS + UNI_NSTRPROPS
   for RGI_Emoji, their union; -1 for none */
static int64_t rp_prop_find(re_parser *p, uint32_t from, uint32_t to) {
    char name[96], value[96], want[96];
    uint32_t nn = 0, nv = 0;
    int eq = 0;
    for (uint32_t i = from; i < to; i++) {
        uint32_t c = p->pat[i];
        if (c > 127) return -1;
        if (c == '=') { if (eq) return -1; eq = 1; continue; }
        if (!eq) { if (nn + 1 >= sizeof name) return -1; name[nn++] = (char)c; }
        else { if (nv + 1 >= sizeof value) return -1; value[nv++] = (char)c; }
    }
    name[nn] = 0;
    value[nv] = 0;
    int kind = -1;
    memcpy(want, name, sizeof want);
    if (eq) {
        if (!strcmp(name, "General_Category") || !strcmp(name, "gc")) kind = 0;
        else if (!strcmp(name, "Script") || !strcmp(name, "sc")) kind = 1;
        else if (!strcmp(name, "Script_Extensions") || !strcmp(name, "scx")) kind = 2;
        else return -1;
        memcpy(want, value, sizeof want);
    } else {
        /* a property of strings, by its one name */
        if (!strcmp(name, "RGI_Emoji")) return UNI_NPROPS + UNI_NSTRPROPS;
        for (uint32_t s = 0; s < UNI_NSTRPROPS; s++) {
            uint32_t j = 0;
            while (j < nn && uni_strprops[s].name[j] == name[j]) j++;
            if (j == nn && uni_strprops[s].name[j] == 0) return UNI_NPROPS + s;
        }
    }
    size_t wl = strlen(want);
    if (wl == 0) return -1;
    for (uint32_t i = 0; i < UNI_NPROPS; i++) {
        uint32_t k = uni_props[i].kind;
        if (eq ? (int)k != kind : (k != 0 && k != 3)) continue;
        /* its aliases, | between them */
        uint32_t k = 0;
        for (;;) {
            uint32_t l = 0;
            while (uni_props[i].names[k + l] && uni_props[i].names[k + l] != '|') l++;
            int same = l == wl;
            for (uint32_t j = 0; same && j < l; j++) if (uni_props[i].names[k + j] != want[j]) same = 0;
            if (same) return i;
            if (!uni_props[i].names[k + l]) break;
            k += l + 1;
        }
    }
    return -1;
}

/* a \p{...}'s text into the class (u's): 0 when it names no property of characters */
static int cls_property(re_parser *p, uint32_t from, uint32_t to, int negate) {
    int64_t i = rp_prop_find(p, from, to);
    if (i < 0 || i >= UNI_NPROPS) return 0;
    cls_add_ranges(p, uni_ranges, uni_props[i].off, uni_props[i].n, negate, 0x10FFFF);
    return 1;
}

/* ---- escapes ---- */

/* \u's hex digits after the u: the code unit or (with u) code point, or -1 */
static int64_t rp_unicode_escape(re_parser *p) {
    if (rp_is(p, 0, '{') && rp_unicode(p)) {
        uint32_t k = 1;
        int64_t v = 0;
        while (re_hexval(rp_at(p, k)) >= 0) { v = v * 16 + re_hexval(rp_at(p, k)); if (v > 0x10FFFF) return -1; k++; }
        if (k == 1 || !rp_is(p, k, '}')) return -1;
        p->pos += k + 1;
        return v;
    }
    int64_t v = 0;
    for (uint32_t k = 0; k < 4; k++) { int h = re_hexval(rp_at(p, k)); if (h < 0) return -1; v = v * 16 + h; }
    p->pos += 4;
    /* with u, a surrogate pair written as two escapes is one code point */
    if (rp_unicode(p) && v >= 0xD800 && v <= 0xDBFF && rp_is(p, 0, '\\') && rp_is(p, 1, 'u')) {
        int64_t w = 0;
        int ok = 1;
        for (uint32_t k = 0; k < 4; k++) { int h = re_hexval(rp_at(p, 2 + k)); if (h < 0) { ok = 0; break; } w = w * 16 + h; }
        if (ok && w >= 0xDC00 && w <= 0xDFFF) { p->pos += 6; v = 0x10000 + ((v - 0xD800) << 10) + (w - 0xDC00); }
    }
    return v;
}

/* a character escape after its backslash (the escape's first character current): the
   character, or -1 with the error set. in_class: inside [...], where \b is a backspace
   and \- (with u) a hyphen */
static int64_t rp_char_escape(re_parser *p, int in_class) {
    int64_t c = rp_at(p, 0);
    if (c < 0) { re_fail(p, RE_E_TRAILING_BACKSLASH); return -1; }
    p->pos++;
    switch (c) {
    case 't': return 9;
    case 'n': return 10;
    case 'v': return 11;
    case 'f': return 12;
    case 'r': return 13;
    case 'b': if (in_class) return 8; break;
    case '-': if (in_class) return '-'; break;
    case 'c': {
        int64_t l = rp_at(p, 0);
        if ((l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z')) { p->pos++; return l % 32; }
        /* Annex B: in a class, \c and a digit or _ too */
        if (in_class && !rp_unicode(p) && (re_digit(l) || l == '_')) { p->pos++; return l % 32; }
        if (rp_unicode(p)) { re_fail(p, RE_E_UNICODE_ESCAPE); return -1; }
        p->pos--;
        return '\\';    /* \c read as a backslash, then c */
    }
    case '0':
        if (!re_digit(rp_at(p, 0))) return 0;
        if (rp_unicode(p)) { re_fail(p, in_class ? RE_E_CLASS_ESCAPE : RE_E_DECIMAL); return -1; }
        break;
    case 'x': {
        int h1 = re_hexval(rp_at(p, 0)), h2 = re_hexval(rp_at(p, 1));
        if (h1 >= 0 && h2 >= 0) { p->pos += 2; return h1 * 16 + h2; }
        if (rp_unicode(p)) { re_fail(p, RE_E_ESCAPE); return -1; }
        return 'x';
    }
    case 'u': {
        int64_t v = rp_unicode_escape(p);
        if (v >= 0) return v;
        if (rp_unicode(p)) { re_fail(p, RE_E_UNICODE_ESCAPE); return -1; }
        return 'u';
    }
    default: break;
    }
    /* an octal escape (Annex B, without u): up to three digits, to 0377 */
    if (c >= '0' && c <= '7' && !rp_unicode(p)) {
        int64_t v = c - '0';
        if (rp_at(p, 0) >= '0' && rp_at(p, 0) <= '7') {
            v = v * 8 + (rp_at(p, 0) - '0');
            p->pos++;
            if (c <= '3' && rp_at(p, 0) >= '0' && rp_at(p, 0) <= '7') { v = v * 8 + (rp_at(p, 0) - '0'); p->pos++; }
        }
        return v;
    }
    if (rp_unicode(p)) {
        /* with u, only the syntax characters are escaped as themselves (in a class, the
           hyphen, and with v the reserved punctuators too); an octal digit in a class is none */
        if (in_class && c >= '1' && c <= '7') { re_fail(p, RE_E_CLASS_ESCAPE); return -1; }
        if (re_syntax_char(c) || (c == '-' && in_class)) return c;
        if (in_class && (p->flags & RF_V) && (c == '&' || c == '!' || c == '#' || c == '%' || c == ',' || c == ':' || c == ';' ||
                                              c == '<' || c == '=' || c == '>' || c == '@' || c == '`' || c == '~')) return c;
        re_fail(p, RE_E_ESCAPE);
        return -1;
    }
    /* without u, any character but a letter of a class escape is itself (\k without named groups too) */
    return c;
}

/* a \p{...}'s braces after the backslash and the p: the property's text [*from, *to), the
   } read; 0 with the error set (its words in a class or not) */
static int rp_prop_text(re_parser *p, int in_class, uint32_t *from, uint32_t *to) {
    uint32_t e = in_class ? RE_E_PROPERTY_CLASS : RE_E_PROPERTY;
    if (!rp_is(p, 0, '{')) { re_fail(p, e); return 0; }
    uint32_t k = p->pos + 1;
    while (k < len(p->pat) && p->pat[k] != '}') k++;
    if (k >= len(p->pat)) { re_fail(p, e); return 0; }
    *from = p->pos + 1;
    *to = k;
    p->pos = k + 1;
    return 1;
}

/* \p{...} or \P{...} after the backslash and the p: into the class; 0 with the error set */
static int rp_property(re_parser *p, int negate, int in_class) {
    uint32_t from = 0, to = 0;
    if (!rp_prop_text(p, in_class, &from, &to)) return 0;
    if (!cls_property(p, from, to, negate)) { re_fail(p, in_class ? RE_E_PROPERTY_CLASS : RE_E_PROPERTY); return 0; }
    return 1;
}

/* a group's name after (?< or \k<: its characters into namech, the > read. 0 when it is not one */
static int rp_group_name(re_parser *p, uint32_t *off, uint32_t *n) {
    *off = (uint32_t)len(p->namech);
    uint32_t k = 0;
    for (;;) {
        int64_t c = rp_at(p, 0);
        if (c == '>') { p->pos++; break; }
        if (c < 0) return 0;
        if (c == '\\') {
            if (!rp_is(p, 1, 'u')) return 0;
            p->pos += 2;
            uint32_t saved = p->flags;
            p->flags |= RF_U;          /* \u{...} is read in a name with or without u */
            c = rp_unicode_escape(p);
            p->flags = saved;
            if (c < 0) return 0;
        } else p->pos++;
        int ok = c == '$' || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80 || (k > 0 && re_digit(c));
        if (!ok) return 0;
        if (c > 0xFFFF) { push_back(p->namech, (uint32_t)(0xD800 + ((c - 0x10000) >> 10))); push_back(p->namech, (uint32_t)(0xDC00 + ((c - 0x10000) & 0x3FF))); k += 2; }
        else { push_back(p->namech, (uint32_t)c); k++; }
    }
    *n = k;
    return k > 0;
}
static int re_name_eq(re_parser *p, uint32_t a_off, uint32_t a_n, uint32_t b_off, uint32_t b_n) {
    if (a_n != b_n) return 0;
    for (uint32_t i = 0; i < a_n; i++) if (p->namech[a_off + i] != p->namech[b_off + i]) return 0;
    return 1;
}

/* ---- the v flag's classes ----
   A class with v is a set of characters, its ranges (sorted [lo, hi] pairs), and of
   strings, their ids (sorted longest first, then by code point, no two alike). Each
   operand of a class is read into the class being read (cls, stmp); an intersection's
   or a difference's sets are computed from its operands' (in vr and vl). With i every
   set is closed under case: each character's whole orbit in it, each string's
   characters their orbits' least. Unions, intersections, differences and complements
   of closed sets are closed, so these are ECMA-262's sets (every operand simple-case-
   folded, a character matched through Canonicalize) seen through the orbits: a
   character is in the class when it is in its closed set. */

/* a set: its ranges' rn pairs in vr from pair ro, its strings' sn ids in vl from so; may,
   whether it may contain strings as ECMA-262's MayContainStrings says of its syntax (a
   negated class may not, whatever its strings turn out to be) */
typedef struct re_cset { uint32_t ro, rn, so, sn, may; } re_cset;

/* two strings compared: in a set's order (mode 0: the longer first, then by code point), or
   by their code points read forward (1) or backward (2), a prefix first */
static int rs_order(re_parser *p, uint32_t a, uint32_t b, int mode) {
    uint32_t al = p->vs[2 * a + 1], bl = p->vs[2 * b + 1], ao = p->vs[2 * a], bo = p->vs[2 * b];
    if (mode == 0 && al != bl) return al > bl ? -1 : 1;
    for (uint32_t i = 0; i < al && i < bl; i++) {
        uint32_t x = p->vc[ao + i], y = p->vc[bo + i];
        if (mode == 2) { x = p->vc[ao + al - 1 - i]; y = p->vc[bo + bl - 1 - i]; }
        if (x != y) return x < y ? -1 : 1;
    }
    if (al == bl) return 0;
    return al < bl ? -1 : 1;
}

/* the n pairs of cls from word at, sorted by their first word (a merge sort) */
static void rs_sort_pairs(re_parser *p, uint32_t at, uint32_t n) {
    if (n < 2) return;
    resize(p->srt, 2 * n, 0);
    for (uint32_t w = 1; w < n; w *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * w) {
            uint32_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            uint32_t x = lo, y = mid, k = lo;
            while (x < mid || y < hi) {
                uint32_t from = 0;
                if (y >= hi || (x < mid && p->cls[at + 2 * x] <= p->cls[at + 2 * y])) from = x++;
                else from = y++;
                p->srt[2 * k] = p->cls[at + 2 * from];
                p->srt[2 * k + 1] = p->cls[at + 2 * from + 1];
                k++;
            }
        }
        for (uint32_t k = 0; k < 2 * n; k++) p->cls[at + k] = p->srt[k];
    }
}

/* the n string ids of stmp from at, sorted as rs_order's mode says (a merge sort) */
static void rs_sort_ids(re_parser *p, uint32_t at, uint32_t n, int mode) {
    if (n < 2) return;
    resize(p->srt, n, 0);
    for (uint32_t w = 1; w < n; w *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * w) {
            uint32_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            uint32_t x = lo, y = mid, k = lo;
            while (x < mid || y < hi) {
                if (y >= hi || (x < mid && rs_order(p, p->stmp[at + x], p->stmp[at + y], mode) <= 0)) p->srt[k++] = p->stmp[at + x++];
                else p->srt[k++] = p->stmp[at + y++];
            }
        }
        for (uint32_t k = 0; k < n; k++) p->stmp[at + k] = p->srt[k];
    }
}

/* the class being read from rmark (in cls) and smark (in stmp) made a set, its pieces taken */
static re_cset cs_make(re_parser *p, uint32_t rmark, uint32_t smark, uint32_t may) {
    re_cset s = {0};
    uint32_t n = (uint32_t)(len(p->cls) - rmark) / 2;
    rs_sort_pairs(p, rmark, n);
    s.ro = (uint32_t)(len(p->vr) / 2);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lo = p->cls[rmark + 2 * i], hi = p->cls[rmark + 2 * i + 1];
        size_t top = len(p->vr);
        if (s.rn > 0 && lo <= p->vr[top - 1] + 1) { if (hi > p->vr[top - 1]) p->vr[top - 1] = hi; }
        else { push_back(p->vr, lo); push_back(p->vr, hi); s.rn++; }
    }
    resize(p->cls, rmark, 0);
    uint32_t m = (uint32_t)(len(p->stmp) - smark);
    rs_sort_ids(p, smark, m, 0);
    s.so = (uint32_t)len(p->vl);
    for (uint32_t i = 0; i < m; i++) {
        uint32_t id = p->stmp[smark + i];
        if (s.sn > 0 && rs_order(p, p->vl[len(p->vl) - 1], id, 0) == 0) continue;
        push_back(p->vl, id);
        s.sn++;
    }
    resize(p->stmp, smark, 0);
    s.may = may;
    return s;
}

/* a set's pieces into the class being read */
static void rv_append(re_parser *p, re_cset s) {
    for (uint32_t i = 0; i < s.rn; i++) cls_add(p, p->vr[2 * (s.ro + i)], p->vr[2 * (s.ro + i) + 1]);
    for (uint32_t i = 0; i < s.sn; i++) { uint32_t id = p->vl[s.so + i]; push_back(p->stmp, id); }
}

/* the strings of a in b too (and), or not in b (else), from the set's own order */
static void cs_strings(re_parser *p, re_cset a, re_cset b, int and_, re_cset *s) {
    s->so = (uint32_t)len(p->vl);
    s->sn = 0;
    uint32_t i = 0, j = 0;
    while (i < a.sn) {
        int c = j < b.sn ? rs_order(p, p->vl[a.so + i], p->vl[b.so + j], 0) : -1;
        if (c > 0) { j++; continue; }
        uint32_t id = p->vl[a.so + i];
        if ((c == 0) == (and_ != 0)) { push_back(p->vl, id); s->sn++; }
        i++;
        if (c == 0) j++;
    }
}

/* a and b: their intersection */
static re_cset cs_inter(re_parser *p, re_cset a, re_cset b) {
    re_cset s = {0};
    s.ro = (uint32_t)(len(p->vr) / 2);
    uint32_t i = 0, j = 0;
    while (i < a.rn && j < b.rn) {
        uint32_t alo = p->vr[2 * (a.ro + i)], ahi = p->vr[2 * (a.ro + i) + 1];
        uint32_t blo = p->vr[2 * (b.ro + j)], bhi = p->vr[2 * (b.ro + j) + 1];
        uint32_t lo = alo > blo ? alo : blo, hi = ahi < bhi ? ahi : bhi;
        if (lo <= hi) { push_back(p->vr, lo); push_back(p->vr, hi); s.rn++; }
        if (ahi < bhi) i++;
        else j++;
    }
    cs_strings(p, a, b, 1, &s);
    s.may = a.may & b.may;
    return s;
}

/* a less b */
static re_cset cs_minus(re_parser *p, re_cset a, re_cset b) {
    re_cset s = {0};
    s.ro = (uint32_t)(len(p->vr) / 2);
    uint32_t j = 0;
    for (uint32_t i = 0; i < a.rn; i++) {
        uint32_t lo = p->vr[2 * (a.ro + i)], hi = p->vr[2 * (a.ro + i) + 1];
        while (j < b.rn && p->vr[2 * (b.ro + j) + 1] < lo) j++;
        uint32_t cur = lo, k = j;
        int done = 0;
        while (k < b.rn && p->vr[2 * (b.ro + k)] <= hi) {
            uint32_t blo = p->vr[2 * (b.ro + k)], bhi = p->vr[2 * (b.ro + k) + 1];
            if (blo > cur) { push_back(p->vr, cur); push_back(p->vr, blo - 1); s.rn++; }
            if (bhi >= hi) { done = 1; break; }
            if (bhi + 1 > cur) cur = bhi + 1;
            k++;
        }
        if (!done && cur <= hi) { push_back(p->vr, cur); push_back(p->vr, hi); s.rn++; }
    }
    cs_strings(p, a, b, 0, &s);
    s.may = a.may;
    return s;
}

/* the characters a does not hold (a holds no string: the syntax sees to it) */
static re_cset cs_complement(re_parser *p, re_cset a) {
    re_cset s = {0};
    s.ro = (uint32_t)(len(p->vr) / 2);
    uint32_t from = 0;
    for (uint32_t i = 0; i < a.rn; i++) {
        uint32_t lo = p->vr[2 * (a.ro + i)], hi = p->vr[2 * (a.ro + i) + 1];
        if (lo > from) { push_back(p->vr, from); push_back(p->vr, lo - 1); s.rn++; }
        if (hi + 1 > from) from = hi + 1;
    }
    if (from <= 0x10FFFF) { push_back(p->vr, from); push_back(p->vr, 0x10FFFF); s.rn++; }
    s.so = (uint32_t)len(p->vl);
    return s;
}

/* [lo, hi] into the class being read; with i each character's orbit too */
static void rv_add_range(re_parser *p, uint32_t lo, uint32_t hi) {
    cls_add(p, lo, hi);
    if (!(p->cur & RF_I)) return;
    uint32_t a = 0, b = UNI_ORBIT_U_N;
    while (a < b) {
        uint32_t mid = (a + b) / 2;
        if (uni_orbit_u[2 * mid] < lo) a = mid + 1;
        else b = mid;
    }
    for (uint32_t k = a; k < UNI_ORBIT_U_N && uni_orbit_u[2 * k] <= hi; k++) {
        uint32_t x = uni_orbit_u[2 * k], y = uni_orbit_u[2 * k + 1];
        for (int g = 0; g < 8 && y != x; g++) {
            if (y < lo || y > hi) cls_add(p, y, y);
            y = re_orbit_next(y, 1);
        }
    }
}
/* a string's character as its set holds it: with i, its orbit's least */
static uint32_t rv_canon(re_parser *p, uint32_t c) { return (p->cur & RF_I) ? re_rep(c, 1) : c; }

/* the k-th property of strings (UNI_NSTRPROPS: RGI_Emoji, all of them) into the class being read */
static void rv_strprop(re_parser *p, uint32_t k) {
    for (uint32_t t = 0; t < UNI_NSTRPROPS; t++) {
        if (k != UNI_NSTRPROPS && t != k) continue;
        for (uint32_t i = 0; i < uni_strprops[t].rn; i++)
            rv_add_range(p, uni_ranges[2 * (uni_strprops[t].roff + i)], uni_ranges[2 * (uni_strprops[t].roff + i) + 1]);
        uint32_t w = uni_strprops[t].off, end = w + uni_strprops[t].len;
        while (w < end) {
            uint32_t n = uni_seqs[w], off = (uint32_t)len(p->vc);
            for (uint32_t j = 0; j < n; j++) push_back(p->vc, rv_canon(p, uni_seqs[w + 1 + j]));
            push_back(p->vs, off);
            push_back(p->vs, n);
            push_back(p->stmp, (uint32_t)(len(p->vs) / 2 - 1));
            w += n + 1;
        }
    }
}

/* a class escape with v, its letter read (\d \D \s \S \w \W, and \p{...} \P{...} at their {):
   its set's pieces into the class being read, *may when it may contain strings (a
   property of strings). 0 with the error set */
static int rv_escape(re_parser *p, uint32_t e, int in_class, uint32_t *may) {
    uint32_t rmark = (uint32_t)len(p->cls), smark = (uint32_t)len(p->stmp);
    int neg = e == 'D' || e == 'S' || e == 'W' || e == 'P';
    *may = 0;
    if (e == 'p' || e == 'P') {
        uint32_t from = 0, to = 0;
        if (!rp_prop_text(p, in_class, &from, &to)) return 0;
        int64_t k = rp_prop_find(p, from, to);
        if (k < 0 || (k >= UNI_NPROPS && neg)) { re_fail(p, in_class ? RE_E_PROPERTY_CLASS : RE_E_PROPERTY); return 0; }
        if (k >= UNI_NPROPS) { rv_strprop(p, (uint32_t)(k - UNI_NPROPS)); *may = 1; return 1; }
        uint32_t pk = (uint32_t)k;
        for (uint32_t i = 0; i < uni_props[pk].n; i++)
            rv_add_range(p, uni_ranges[2 * (uni_props[pk].off + i)], uni_ranges[2 * (uni_props[pk].off + i) + 1]);
    } else {
        uint32_t lc = e | 0x20;
        if (lc == 'd') rv_add_range(p, '0', '9');
        else if (lc == 's') for (uint32_t i = 0; i < 10; i++) rv_add_range(p, re_spaces[2 * i], re_spaces[2 * i + 1]);
        else for (uint32_t i = 0; i < 4; i++) rv_add_range(p, re_words[2 * i], re_words[2 * i + 1]);
    }
    if (neg) {
        /* the complement of the closed set: closed too */
        re_cset s = cs_make(p, rmark, smark, 0);
        rv_append(p, cs_complement(p, s));
    }
    return 1;
}

/* a ClassSetCharacter: the character, or -1 with the error set */
static int64_t rv_char(re_parser *p) {
    int64_t c = rp_at(p, 0);
    if (c < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return -1; }
    if (c == '\\') {
        if (rp_is(p, 1, 'b')) { p->pos += 2; return 8; }
        if (rp_at(p, 1) < 0) { re_fail(p, RE_E_TRAILING_BACKSLASH); return -1; }
        p->pos++;
        return rp_char_escape(p, 1);
    }
    if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '-' || c == '|') {
        re_fail(p, RE_E_CLASS_CHAR);
        return -1;
    }
    if (rp_at(p, 1) == c && (c == '&' || c == '!' || c == '#' || c == '$' || c == '%' || c == '*' || c == '+' || c == ',' ||
                             c == '.' || c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '@' ||
                             c == '^' || c == '`' || c == '~')) {
        re_fail(p, RE_E_SET_OP);
        return -1;
    }
    p->pos++;
    return c;
}

/* \q{...}, its backslash read: its strings into the class being read (a string of one
   character a character), *may when one is not one character. 0 with the error set.
   With i a string of one character is V8's: its simple case folding alone, no orbit (so
   /[\q{a}]/vi takes a but not A, where /[a]/vi takes both) */
static int rv_strings(re_parser *p, uint32_t *may) {
    p->pos++;
    if (!rp_is(p, 0, '{')) { re_fail(p, RE_E_ESCAPE); return 0; }
    p->pos++;
    uint32_t off = (uint32_t)len(p->vc);
    for (;;) {
        int64_t c = rp_at(p, 0);
        if (c < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
        if (c == '|' || c == '}') {
            uint32_t n = (uint32_t)len(p->vc) - off;
            if (n == 1) {
                uint32_t ch = p->vc[off];
                resize(p->vc, off, 0);
                if (p->cur & RF_I) ch = ch < 128 ? (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch) : uni_case_map(uni_fold, UNI_FOLD_N, ch);
                cls_add(p, ch, ch);
            } else {
                *may = 1;
                push_back(p->vs, off);
                push_back(p->vs, n);
                push_back(p->stmp, (uint32_t)(len(p->vs) / 2 - 1));
            }
            p->pos++;
            if (c == '}') return 1;
            off = (uint32_t)len(p->vc);
            continue;
        }
        int64_t x = rv_char(p);
        if (x < 0) return 0;
        push_back(p->vc, rv_canon(p, (uint32_t)x));
    }
}

/* the kinds of a class's operand: the last one read decides whether a hyphen makes a range */
enum { RV_CHAR, RV_STRINGS, RV_ESCAPE, RV_NESTED, RV_RANGE };

static int rv_class(re_parser *p, re_cset *out);

/* an operand of a v class (ClassSetOperand): its set's pieces into the class being read;
   its kind, its character when it is one, *may when it may contain strings. 0 with the
   error set */
static int rv_operand(re_parser *p, uint32_t *kind, uint32_t *ch, uint32_t *may) {
    int64_t c = rp_at(p, 0);
    *may = 0;
    if (c == '\\') {
        int64_t e = rp_at(p, 1);
        if (e == 'q') { *kind = RV_STRINGS; p->pos++; return rv_strings(p, may); }
        if (e == 'd' || e == 'D' || e == 's' || e == 'S' || e == 'w' || e == 'W' || e == 'p' || e == 'P') {
            *kind = RV_ESCAPE;
            p->pos += 2;
            return rv_escape(p, (uint32_t)e, 1, may);
        }
    }
    if (c == '[') {
        *kind = RV_NESTED;
        p->pos++;
        re_cset s = {0};
        if (!rv_class(p, &s)) return 0;
        rv_append(p, s);
        *may = s.may;
        return 1;
    }
    *kind = RV_CHAR;
    int64_t x = rv_char(p);
    if (x < 0) return 0;
    *ch = (uint32_t)x;
    rv_add_range(p, (uint32_t)x, (uint32_t)x);
    return 1;
}

/* a v class after its [ (a nested one's too): the set it is, with V8's errors in V8's
   order. 0 with the error set */
static int rv_class(re_parser *p, re_cset *out) {
    if (++p->depth > RE_DEPTH_MAX) { re_fail(p, RE_E_TOO_DEEP); return 0; }
    int neg = 0;
    if (rp_is(p, 0, '^')) { neg = 1; p->pos++; }
    uint32_t rmark = (uint32_t)len(p->cls), smark = (uint32_t)len(p->stmp);
    re_cset r = {0};
    if (rp_is(p, 0, ']')) {
        p->pos++;
        r = cs_make(p, rmark, smark, 0);
    } else {
        uint32_t kind = 0, ch = 0, may = 0;
        if (!rv_operand(p, &kind, &ch, &may)) return 0;
        if (rp_is(p, 0, '-') && rp_is(p, 1, '-')) {
            /* a difference: the first operand's set less each next one's */
            if (neg && may) { re_fail(p, RE_E_NEG_STRINGS); return 0; }
            r = cs_make(p, rmark, smark, may);
            while (rp_at(p, 0) >= 0 && !rp_is(p, 0, ']')) {
                if (!rp_is(p, 0, '-') || !rp_is(p, 1, '-')) { re_fail(p, RE_E_SET_OP); return 0; }
                p->pos += 2;
                uint32_t k2 = 0, c2 = 0, m2 = 0, rm2 = (uint32_t)len(p->cls), sm2 = (uint32_t)len(p->stmp);
                if (!rv_operand(p, &k2, &c2, &m2)) return 0;
                re_cset b = cs_make(p, rm2, sm2, m2);
                r = cs_minus(p, r, b);
            }
            if (rp_at(p, 0) < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
            p->pos++;
        } else if (rp_is(p, 0, '&') && rp_is(p, 1, '&')) {
            /* an intersection: it may contain strings when every operand may */
            uint32_t all = may;
            r = cs_make(p, rmark, smark, may);
            while (rp_at(p, 0) >= 0 && !rp_is(p, 0, ']')) {
                if (!rp_is(p, 0, '&') || !rp_is(p, 1, '&')) { re_fail(p, RE_E_SET_OP); return 0; }
                p->pos += 2;
                if (rp_is(p, 0, '&')) { re_fail(p, RE_E_CLASS_CHAR); return 0; }
                uint32_t k2 = 0, c2 = 0, m2 = 0, rm2 = (uint32_t)len(p->cls), sm2 = (uint32_t)len(p->stmp);
                if (!rv_operand(p, &k2, &c2, &m2)) return 0;
                re_cset b = cs_make(p, rm2, sm2, m2);
                r = cs_inter(p, r, b);
                all &= m2;
            }
            if (rp_at(p, 0) < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
            if (neg && all) { re_fail(p, RE_E_NEG_STRINGS); return 0; }
            p->pos++;
            r.may = all;
        } else {
            /* a union: its operands' pieces gathered, a hyphen between two characters a range */
            uint32_t any = may;
            while (rp_at(p, 0) >= 0 && !rp_is(p, 0, ']')) {
                if (rp_is(p, 0, '-')) {
                    if (rp_is(p, 1, '-')) { re_fail(p, RE_E_SET_OP); return 0; }
                    if (kind != RV_CHAR) { re_fail(p, RE_E_CLASS); return 0; }
                    p->pos++;
                    uint32_t from = ch, m2 = 0, rm2 = (uint32_t)len(p->cls);
                    if (!rv_operand(p, &kind, &ch, &m2)) return 0;
                    if (kind != RV_CHAR) { re_fail(p, RE_E_CLASS); return 0; }
                    if (from > ch) { re_fail(p, RE_E_RANGE_ORDER); return 0; }
                    resize(p->cls, rm2, 0);
                    rv_add_range(p, from, ch);
                    kind = RV_RANGE;
                    continue;
                }
                if (rp_is(p, 0, '&') && rp_is(p, 1, '&')) { re_fail(p, RE_E_SET_OP); return 0; }
                uint32_t m2 = 0;
                if (!rv_operand(p, &kind, &ch, &m2)) return 0;
                any |= m2;
            }
            if (rp_at(p, 0) < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
            p->pos++;
            if (neg && any) { re_fail(p, RE_E_NEG_STRINGS); return 0; }
            r = cs_make(p, rmark, smark, any);
        }
    }
    if (neg) r = cs_complement(p, r);
    p->depth--;
    *out = r;
    return 1;
}

/* a v set as the tree's node: a class when it holds no string, else a STRSET */
static uint32_t rv_node(re_parser *p, re_cset s) {
    uint32_t off = (uint32_t)(len(p->ranges) / 2);
    for (uint32_t i = 0; i < 2 * s.rn; i++) { uint32_t w = p->vr[2 * s.ro + i]; push_back(p->ranges, w); }
    if (s.sn == 0) {
        uint32_t n = rn_new(p, RN_CLASS);
        p->nodes[n].a = off;
        p->nodes[n].b = s.rn;
        p->nodes[n].d = 1;
        return n;
    }
    uint32_t n = rn_new(p, RN_STRSET);
    p->nodes[n].a = s.so;
    p->nodes[n].b = s.sn;
    p->nodes[n].c = off;
    p->nodes[n].d = s.rn;
    return n;
}

/* ---- the parser ---- */

static uint32_t rp_disjunction(re_parser *p);

/* a class [...], the [ read */
static uint32_t rp_class(re_parser *p) {
    if (p->flags & RF_V) {
        re_cset s = {0};
        if (!rv_class(p, &s)) return 0;
        return rv_node(p, s);
    }
    int neg = 0;
    if (rp_is(p, 0, '^')) { neg = 1; p->pos++; }
    clear(p->cls);
    for (;;) {
        int64_t c = rp_at(p, 0);
        if (c < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
        if (c == ']') { p->pos++; break; }
        /* an atom: a character, or a class escape (a set: no range from or to it) */
        int64_t lo = -1;
        int set = 0;
        p->pos++;
        if (c == '\\') {
            int64_t e = rp_at(p, 0);
            if (e == 'd' || e == 'D' || e == 's' || e == 'S' || e == 'w' || e == 'W') { p->pos++; cls_escape(p, (uint32_t)e); set = 1; }
            else if ((e == 'p' || e == 'P') && rp_unicode(p)) { p->pos++; if (!rp_property(p, e == 'P', 1)) return 0; set = 1; }
            else { lo = rp_char_escape(p, 1); if (lo < 0) return 0; }
        } else lo = c;
        if (rp_is(p, 0, '-') && !rp_is(p, 1, ']') && rp_at(p, 1) >= 0) {
            /* a range, unless either end is a set (Annex B: then the hyphen is itself) */
            uint32_t save = p->pos;
            p->pos++;
            int64_t d = rp_at(p, 0);
            int64_t hi = -1;
            int set2 = 0;
            p->pos++;
            if (d == '\\') {
                int64_t e = rp_at(p, 0);
                if (e == 'd' || e == 'D' || e == 's' || e == 'S' || e == 'w' || e == 'W' || ((e == 'p' || e == 'P') && rp_unicode(p))) set2 = 1;
                else { hi = rp_char_escape(p, 1); if (hi < 0) return 0; }
            } else hi = d;
            if (set || set2) {
                if (rp_unicode(p)) { re_fail(p, RE_E_CLASS); return 0; }
                if (!set) cls_add(p, (uint32_t)lo, (uint32_t)lo);
                cls_add(p, '-', '-');
                p->pos = save + 1;      /* the hyphen taken; the next atom read on its own */
                continue;
            }
            if (lo > hi) { re_fail(p, RE_E_RANGE_ORDER); return 0; }
            cls_add(p, (uint32_t)lo, (uint32_t)hi);
            continue;
        }
        if (!set) cls_add(p, (uint32_t)lo, (uint32_t)lo);
    }
    uint32_t n = rn_new(p, RN_CLASS);
    uint32_t off = 0, cnt = 0;
    cls_take(p, &off, &cnt);
    p->nodes[n].a = off;
    p->nodes[n].b = cnt;
    p->nodes[n].c = (uint32_t)neg;
    return n;
}

/* {n}, {n,}, {n,m} at the current {: 1 with *min *max, 0 when it is not a quantifier's */
static int rp_braces(re_parser *p, uint32_t *min, uint32_t *max) {
    uint32_t k = 1;
    if (!re_digit(rp_at(p, k))) return 0;
    uint64_t a = 0;
    while (re_digit(rp_at(p, k))) { a = a * 10 + (uint64_t)(rp_at(p, k) - '0'); if (a > 0x7FFFFFFF) a = 0x7FFFFFFF; k++; }
    uint64_t b = a;
    if (rp_is(p, k, ',')) {
        k++;
        if (re_digit(rp_at(p, k))) {
            b = 0;
            while (re_digit(rp_at(p, k))) { b = b * 10 + (uint64_t)(rp_at(p, k) - '0'); if (b > 0x7FFFFFFF) b = 0x7FFFFFFF; k++; }
        } else b = RE_INF;
    }
    if (!rp_is(p, k, '}')) return 0;
    p->pos += k + 1;
    *min = (uint32_t)a;
    *max = b == RE_INF ? RE_INF : (uint32_t)b;
    return 1;
}

/* an atom's escape, the backslash read */
static uint32_t rp_atom_escape(re_parser *p) {
    int64_t c = rp_at(p, 0);
    if (c < 0) { re_fail(p, RE_E_TRAILING_BACKSLASH); return 0; }
    if ((p->flags & RF_V) && (c == 'd' || c == 'D' || c == 's' || c == 'S' || c == 'w' || c == 'W' || c == 'p' || c == 'P')) {
        /* with v, a set as a class's are (a property of strings among them) */
        p->pos++;
        uint32_t rmark = (uint32_t)len(p->cls), smark = (uint32_t)len(p->stmp), may = 0;
        if (!rv_escape(p, (uint32_t)c, 0, &may)) return 0;
        return rv_node(p, cs_make(p, rmark, smark, may));
    }
    if (c == 'd' || c == 'D' || c == 's' || c == 'S' || c == 'w' || c == 'W') {
        p->pos++;
        clear(p->cls);
        cls_escape(p, (uint32_t)c);
        uint32_t n = rn_new(p, RN_CLASS);
        uint32_t off = 0, cnt = 0;
        cls_take(p, &off, &cnt);
        p->nodes[n].a = off;
        p->nodes[n].b = cnt;
        return n;
    }
    if ((c == 'p' || c == 'P') && rp_unicode(p)) {
        p->pos++;
        clear(p->cls);
        if (!rp_property(p, c == 'P', 0)) return 0;
        uint32_t n = rn_new(p, RN_CLASS);
        uint32_t off = 0, cnt = 0;
        cls_take(p, &off, &cnt);
        p->nodes[n].a = off;
        p->nodes[n].b = cnt;
        return n;
    }
    if (c >= '1' && c <= '9') {
        /* a back reference when its number is a group's; else (without u) an octal escape or the digit */
        uint32_t k = 0;
        uint64_t v = 0;
        while (re_digit(rp_at(p, k))) { v = v * 10 + (uint64_t)(rp_at(p, k) - '0'); if (v > 0xFFFFFF) v = 0xFFFFFF; k++; }
        if (v <= p->total) {
            p->pos += k;
            uint32_t n = rn_new(p, RN_BACKREF);
            p->nodes[n].a = (uint32_t)v;
            return n;
        }
        if (rp_unicode(p)) { re_fail(p, RE_E_ESCAPE); return 0; }
        if (c >= '8') { p->pos++; uint32_t n = rn_new(p, RN_CHAR); p->nodes[n].a = (uint32_t)c; return n; }
    }
    if (c == 'k' && (rp_unicode(p) || p->has_names)) {
        p->pos++;
        if (!rp_is(p, 0, '<')) { re_fail(p, RE_E_NAMED_REFERENCE); return 0; }
        p->pos++;
        uint32_t off = 0, nl = 0;
        if (!rp_group_name(p, &off, &nl)) { re_fail(p, RE_E_GROUP_NAME); return 0; }
        uint32_t n = rn_new(p, RN_NAMEDREF);
        p->nodes[n].a = off;
        p->nodes[n].b = nl;
        return n;
    }
    int64_t ch = rp_char_escape(p, 0);
    if (ch < 0) return 0;
    uint32_t n = rn_new(p, RN_CHAR);
    p->nodes[n].a = (uint32_t)ch;
    return n;
}

/* a term: an assertion, or an atom and its quantifier */
static uint32_t rp_term(re_parser *p) {
    int64_t c = rp_at(p, 0);
    if (c == '^') { p->pos++; return rn_new(p, RN_BOL); }
    if (c == '$') { p->pos++; return rn_new(p, RN_EOL); }
    if (c == '\\' && rp_is(p, 1, 'b')) { p->pos += 2; return rn_new(p, RN_WORDB); }
    if (c == '\\' && rp_is(p, 1, 'B')) { p->pos += 2; return rn_new(p, RN_NWORDB); }
    uint32_t atom = 0, g_lo = p->ngroups;
    int look = 0;
    if (c == '(') {
        if (++p->depth > RE_DEPTH_MAX) { re_fail(p, RE_E_TOO_DEEP); return 0; }
        p->pos++;
        uint32_t group = 0, look_kind = 0, saved = p->cur;
        int is_look = 0;
        if (rp_is(p, 0, '?')) {
            int64_t d = rp_at(p, 1);
            if (d == ':') p->pos += 2;
            else if (d == '=' || d == '!') { is_look = 1; look_kind = d == '=' ? 0 : 1; p->pos += 2; }
            else if (d == '<' && (rp_is(p, 2, '=') || rp_is(p, 2, '!'))) { is_look = 1; look_kind = rp_is(p, 2, '=') ? 2 : 3; p->pos += 3; }
            else if (d == '<') {
                p->pos += 2;
                uint32_t off = 0, nl = 0;
                if (!rp_group_name(p, &off, &nl)) { re_fail(p, RE_E_GROUP_NAME); return 0; }
                for (size_t i = 0; i < len(p->names); i += 3)
                    if (re_name_eq(p, p->names[i + 1], p->names[i + 2], off, nl)) { re_fail(p, RE_E_DUP_NAME); return 0; }
                group = ++p->ngroups;
                push_back(p->names, group);
                push_back(p->names, off);
                push_back(p->names, nl);
            } else if (d == 'i' || d == 'm' || d == 's' || d == '-') {
                /* the modifiers (?ims-ims: ...): the flags they add, then those they take away */
                uint32_t add = 0, rem = 0, seen = 0, dash = 0;
                p->pos++;
                for (;;) {
                    int64_t m = rp_at(p, 0);
                    uint32_t bit = m == 'i' ? RF_I : m == 'm' ? RF_M : m == 's' ? RF_S : 0;
                    if (m == ':') { p->pos++; break; }
                    if (m == '-') {
                        if (dash) { re_fail(p, RE_E_MULTI_DASH); return 0; }
                        dash = 1;
                    } else if (bit) {
                        if (seen & bit) { re_fail(p, RE_E_REPEATED_FLAG); return 0; }
                        seen |= bit;
                        if (dash) rem |= bit;
                        else add |= bit;
                    } else { re_fail(p, RE_E_GROUP); return 0; }
                    p->pos++;
                }
                if (dash && !seen) { re_fail(p, RE_E_FLAG_GROUP); return 0; }
                p->cur = (p->cur | add) & ~rem;
            } else { re_fail(p, RE_E_GROUP); return 0; }
        } else group = ++p->ngroups;
        uint32_t child = rp_disjunction(p);
        if (p->err) return 0;
        if (!rp_is(p, 0, ')')) { re_fail(p, RE_E_UNTERMINATED_GROUP); return 0; }
        p->pos++;
        p->depth--;
        p->cur = saved;
        if (is_look) {
            atom = rn_new(p, RN_LOOK);
            p->nodes[atom].a = child;
            p->nodes[atom].b = look_kind;
            /* Annex B: a lookahead may be quantified without u */
            look = look_kind >= 2 || rp_unicode(p) ? 2 : 1;
        } else {
            atom = rn_new(p, RN_GROUP);
            p->nodes[atom].a = child;
            p->nodes[atom].b = group;
        }
    } else if (c == '.') { p->pos++; atom = rn_new(p, RN_ANY); }
    else if (c == '[') { p->pos++; atom = rp_class(p); }
    else if (c == '\\') { p->pos++; atom = rp_atom_escape(p); }
    else if (c == '*' || c == '+' || c == '?') { re_fail(p, RE_E_NOTHING_TO_REPEAT); return 0; }
    else if (c == '{') {
        uint32_t mn, mx;
        if (rp_unicode(p)) { re_fail(p, RE_E_LONE_QUANT); return 0; }
        uint32_t save = p->pos;
        if (rp_braces(p, &mn, &mx)) { p->pos = save; re_fail(p, RE_E_NOTHING_TO_REPEAT); return 0; }
        p->pos++;
        atom = rn_new(p, RN_CHAR);
        p->nodes[atom].a = '{';
    } else if ((c == ']' || c == '}') && rp_unicode(p)) { re_fail(p, RE_E_LONE_QUANT); return 0; }
    else { p->pos++; atom = rn_new(p, RN_CHAR); p->nodes[atom].a = (uint32_t)c; }
    if (p->err) return 0;
    /* its quantifier */
    uint32_t min = 1, max = 1;
    int64_t q = rp_at(p, 0);
    if (q == '*') { min = 0; max = RE_INF; p->pos++; }
    else if (q == '+') { min = 1; max = RE_INF; p->pos++; }
    else if (q == '?') { min = 0; max = 1; p->pos++; }
    else if (q == '{') {
        if (!rp_braces(p, &min, &max)) {
            if (rp_unicode(p)) { re_fail(p, RE_E_INCOMPLETE_QUANT); return 0; }
            return atom;      /* Annex B: a { that begins no quantifier is itself, read next */
        }
        if (min > max) { re_fail(p, RE_E_OUT_OF_ORDER); return 0; }
    } else return atom;
    int greedy = 1;
    if (rp_is(p, 0, '?')) { greedy = 0; p->pos++; }
    if (look == 2) { re_fail(p, RE_E_NOTHING_TO_REPEAT); return 0; }
    if (look == 1) return min == 0 ? rn_new(p, RN_EMPTY) : atom;   /* a lookahead repeated is itself once, or nothing */
    uint32_t r = rn_new(p, RN_REPEAT);
    p->nodes[r].a = atom;
    p->nodes[r].b = min;
    p->nodes[r].c = max;
    p->nodes[r].d = (uint32_t)greedy;
    p->nodes[r].e = g_lo + 1;
    p->nodes[r].f = p->ngroups + 1;
    return r;
}

static uint32_t rp_alternative(re_parser *p) {
    size_t mark = len(p->tmp);
    while (!p->err && rp_at(p, 0) >= 0 && !rp_is(p, 0, '|') && !rp_is(p, 0, ')')) push_back(p->tmp, rp_term(p));
    uint32_t n = rn_new(p, RN_SEQ);
    p->nodes[n].a = (uint32_t)len(p->lists);
    p->nodes[n].b = (uint32_t)(len(p->tmp) - mark);
    for (size_t i = mark; i < len(p->tmp); i++) push_back(p->lists, p->tmp[i]);
    resize(p->tmp, mark, 0);
    return n;
}

static uint32_t rp_disjunction(re_parser *p) {
    uint32_t first = rp_alternative(p);
    if (!rp_is(p, 0, '|')) return first;
    size_t mark = len(p->tmp);
    push_back(p->tmp, first);
    while (!p->err && rp_is(p, 0, '|')) { p->pos++; push_back(p->tmp, rp_alternative(p)); }
    uint32_t n = rn_new(p, RN_ALT);
    p->nodes[n].a = (uint32_t)len(p->lists);
    p->nodes[n].b = (uint32_t)(len(p->tmp) - mark);
    for (size_t i = mark; i < len(p->tmp); i++) push_back(p->lists, p->tmp[i]);
    resize(p->tmp, mark, 0);
    return n;
}

/* ---- the program ----
   Each instruction is an opcode word (its flags above its low byte) and its operands; a
   jump's offset is from the end of its instruction. */
enum {
    RE_CHAR,         /* c */
    RE_CHAR_I,       /* the orbit's least of c */
    RE_ANY, RE_ANY_ALL,
    RE_CLASS,        /* n, n [lo, hi] pairs (RB_NEG, RB_ICASE) */
    RE_BOL, RE_EOL,  /* RB_MULTI */
    RE_WORDB,        /* RB_NEG */
    RE_SPLIT,        /* off: try what follows, then the place at off */
    RE_JMP,          /* off */
    RE_SAVE,         /* k: capture slot k set to here */
    RE_RESET,        /* lo, hi: the slots [lo, hi) unset */
    RE_BACKREF,      /* group (RB_ICASE) */
    RE_LOOK,         /* kind, off: the sub-match that follows, to its LOOK_END; then on at off */
    RE_LOOK_END,
    RE_LOOP_INIT,    /* r: its count 0 */
    RE_LOOP,         /* r, min, max, greedy, off (the loop's exit) */
    RE_LOOP_BODY,    /* r: the iteration's start kept (its empty check's) */
    RE_LOOP_NEXT,    /* r, min, off (back to its LOOP) */
    RE_SIMPLE,       /* min, max, greedy, n, lit: the literal that follows (plus 1, 0 none), then a one-character
                        instruction of n words */
    RE_SWITCH,       /* n, n (c, off) pairs by c: the character read (its orbit's least with RB_ICASE) found
                        among them, then on at its off; else a failure (a trie's level) */
    RE_MATCH
};
#define RB_BACK 0x100u      /* read backward (a lookbehind's) */
#define RB_NEG 0x200u
#define RB_ICASE 0x400u
#define RB_MULTI 0x800u

typedef struct re_comp { vector<uint32_t> code; uint32_t nloops; uint32_t err; } re_comp;

static void rc_emit(re_comp *c, uint32_t w) { push_back(c->code, w); }
static uint32_t rc_here(re_comp *c) { return (uint32_t)len(c->code); }
/* the offset word at `at` patched to jump to target (from the end of its instruction, end) */
static void rc_patch(re_comp *c, uint32_t at, uint32_t end, uint32_t target) { c->code[at] = (uint32_t)((int32_t)target - (int32_t)end); }

static int rn_single(re_parser *p, uint32_t n) {
    uint32_t k = p->nodes[n].kind;
    return k == RN_CHAR || k == RN_ANY || k == RN_CLASS;
}

/* a one-character node's instruction */
static void rc_one(re_parser *p, re_comp *c, uint32_t n, uint32_t back) {
    re_node x = p->nodes[n];
    int icase = (x.fl & RF_I) != 0, uni = rp_unicode(p);
    if (x.kind == RN_CHAR) {
        uint32_t ch = x.a;
        if (icase && (re_rep(ch, uni) != ch || re_orbit_next(ch, uni) != ch)) { rc_emit(c, RE_CHAR_I | back); rc_emit(c, re_rep(ch, uni)); }
        else { rc_emit(c, RE_CHAR | back); rc_emit(c, ch); }
        return;
    }
    if (x.kind == RN_ANY) { rc_emit(c, ((x.fl & RF_S) ? RE_ANY_ALL : RE_ANY) | back); return; }
    /* a v class's set holds its case already */
    rc_emit(c, RE_CLASS | back | (x.c ? RB_NEG : 0) | (icase && !x.d ? RB_ICASE : 0));
    rc_emit(c, x.b);
    for (uint32_t i = 0; i < 2 * x.b; i++) rc_emit(c, p->ranges[2 * x.a + i]);
}

/* a set with strings (STRSET): its strings as a trie, the longest tried first, each level
   one switch on the next character (read backward in a lookbehind); then its single
   characters; then the empty string when it is one of its strings */
static void rc_strset(re_parser *p, re_comp *c, uint32_t n, uint32_t back) {
    re_node x = p->nodes[n];
    uint32_t icase = (x.fl & RF_I) ? RB_ICASE : 0;
    uint32_t at = (uint32_t)len(p->stmp), empty = 0;
    for (uint32_t i = 0; i < x.b; i++) {
        uint32_t id = p->vl[x.a + i];
        if (p->vs[2 * id + 1] == 0) empty = 1;
        else push_back(p->stmp, id);
    }
    uint32_t ns = (uint32_t)len(p->stmp) - at;
    rs_sort_ids(p, at, ns, back ? 2 : 1);
    /* the trie, five words a node (its character, its first and last child, its next
       sibling, whether a string ends there), the root first: the strings in code point
       order put each new child after its siblings, which stay sorted */
    vector<uint32_t> t;
    resize(t, 5, 0);
    for (uint32_t i = 0; i < ns; i++) {
        uint32_t id = p->stmp[at + i], off = p->vs[2 * id], l = p->vs[2 * id + 1], cur = 0;
        for (uint32_t j = 0; j < l; j++) {
            uint32_t ch = back ? p->vc[off + l - 1 - j] : p->vc[off + j];
            uint32_t last = t[5 * cur + 2];
            if (last && t[5 * last] == ch) { cur = last; continue; }
            uint32_t m = (uint32_t)(len(t) / 5);
            push_back(t, ch);
            push_back(t, 0);
            push_back(t, 0);
            push_back(t, 0);
            push_back(t, 0);
            if (last) t[5 * last + 3] = m;
            else t[5 * cur + 1] = m;
            t[5 * cur + 2] = m;
            cur = m;
        }
        t[5 * cur + 4] = 1;
    }
    resize(p->stmp, at, 0);
    uint32_t nt = (uint32_t)(len(t) / 5);
    /* each node's code: a split to its end when a string ends there and goes on, its
       switch, its end's jump past the set; its switch's entries patched as each child's
       code is placed */
    vector<uint32_t> patch, ends, stack;
    resize(patch, 2 * nt, 0);
    uint32_t alt = 0;
    if (nt > 1 && (x.d || empty)) { rc_emit(c, RE_SPLIT); alt = rc_here(c); rc_emit(c, 0); }
    if (nt > 1) push_back(stack, 0);
    while (len(stack)) {
        uint32_t m = stack[len(stack) - 1];
        pop_back(stack);
        if (m) c->code[patch[2 * m]] = rc_here(c) - patch[2 * m + 1];
        uint32_t nk = 0;
        for (uint32_t k = t[5 * m + 1]; k; k = t[5 * k + 3]) nk++;
        uint32_t split = 0;
        if (nk && t[5 * m + 4]) { rc_emit(c, RE_SPLIT); split = rc_here(c); rc_emit(c, 0); }
        if (nk) {
            rc_emit(c, RE_SWITCH | back | icase);
            rc_emit(c, nk);
            uint32_t end = rc_here(c) + 2 * nk;
            for (uint32_t k = t[5 * m + 1]; k; k = t[5 * k + 3]) {
                rc_emit(c, t[5 * k]);
                patch[2 * k] = rc_here(c);
                patch[2 * k + 1] = end;
                rc_emit(c, 0);
            }
        }
        if (t[5 * m + 4]) {
            if (split) rc_patch(c, split, split + 1, rc_here(c));
            rc_emit(c, RE_JMP);
            push_back(ends, rc_here(c));
            rc_emit(c, 0);
        }
        for (uint32_t k = t[5 * m + 1]; k; k = t[5 * k + 3]) push_back(stack, k);
    }
    if (alt) rc_patch(c, alt, alt + 1, rc_here(c));
    if (x.d) {
        uint32_t es = 0;
        if (empty) { rc_emit(c, RE_SPLIT); es = rc_here(c); rc_emit(c, 0); }
        rc_emit(c, RE_CLASS | back);
        rc_emit(c, x.d);
        for (uint32_t i = 0; i < 2 * x.d; i++) rc_emit(c, p->ranges[2 * x.c + i]);
        if (empty) {
            rc_emit(c, RE_JMP);
            push_back(ends, rc_here(c));
            rc_emit(c, 0);
            rc_patch(c, es, es + 1, rc_here(c));
        }
    }
    for (size_t i = 0; i < len(ends); i++) rc_patch(c, ends[i], ends[i] + 1, rc_here(c));
}

static void rc_node(re_parser *p, re_comp *c, uint32_t n, uint32_t back) {
    if (c->err) return;
    if (len(c->code) > (1u << 22)) { c->err = RE_E_TOO_LARGE; return; }
    re_node x = p->nodes[n];
    switch (x.kind) {
    case RN_EMPTY: return;
    case RN_CHAR: case RN_ANY: case RN_CLASS: rc_one(p, c, n, back); return;
    case RN_STRSET: rc_strset(p, c, n, back); return;
    case RN_BOL: rc_emit(c, RE_BOL | ((x.fl & RF_M) ? RB_MULTI : 0)); return;
    case RN_EOL: rc_emit(c, RE_EOL | ((x.fl & RF_M) ? RB_MULTI : 0)); return;
    case RN_WORDB: rc_emit(c, RE_WORDB | ((x.fl & RF_I) ? RB_ICASE : 0)); return;
    case RN_NWORDB: rc_emit(c, RE_WORDB | RB_NEG | ((x.fl & RF_I) ? RB_ICASE : 0)); return;
    case RN_SEQ:
        /* a lookbehind's terms right to left; a simple loop followed by a literal (matched
           as it is) told of it */
        for (uint32_t i = 0; i < x.b; i++) {
            uint32_t t = p->lists[x.a + (back ? x.b - 1 - i : i)];
            uint32_t at = rc_here(c);
            rc_node(p, c, t, back);
            if (back || i + 1 >= x.b || (p->flags & (RF_U | RF_V))) continue;
            re_node nx = p->nodes[p->lists[x.a + i + 1]];
            re_node tx = p->nodes[t];
            if (tx.kind == RN_REPEAT && nx.kind == RN_CHAR && !(nx.fl & RF_I) && rc_here(c) > at + 5 && (c->code[at] & 0xFF) == RE_SIMPLE)
                c->code[at + 5] = nx.a + 1;
        }
        return;
    case RN_ALT: {
        vector<uint32_t> ends;
        for (uint32_t i = 0; i < x.b; i++) {
            uint32_t split = 0;
            if (i + 1 < x.b) { rc_emit(c, RE_SPLIT); split = rc_here(c); rc_emit(c, 0); }
            rc_node(p, c, p->lists[x.a + i], back);
            if (i + 1 < x.b) {
                rc_emit(c, RE_JMP);
                push_back(ends, rc_here(c));
                rc_emit(c, 0);
                rc_patch(c, split, split + 1, rc_here(c));
            }
        }
        for (size_t i = 0; i < len(ends); i++) rc_patch(c, ends[i], ends[i] + 1, rc_here(c));
        return;
    }
    case RN_GROUP:
        if (!x.b) { rc_node(p, c, x.a, back); return; }
        rc_emit(c, RE_SAVE);
        rc_emit(c, 2 * x.b + (back ? 1 : 0));
        rc_node(p, c, x.a, back);
        rc_emit(c, RE_SAVE);
        rc_emit(c, 2 * x.b + (back ? 0 : 1));
        return;
    case RN_BACKREF: case RN_NAMEDREF:
        rc_emit(c, RE_BACKREF | back | ((x.fl & RF_I) ? RB_ICASE : 0));
        rc_emit(c, x.a);
        return;
    case RN_LOOK: {
        rc_emit(c, RE_LOOK);
        rc_emit(c, x.b);
        uint32_t at = rc_here(c);
        rc_emit(c, 0);
        rc_node(p, c, x.a, x.b >= 2 ? RB_BACK : 0);
        rc_emit(c, RE_LOOK_END);
        rc_patch(c, at, at + 1, rc_here(c));
        return;
    }
    case RN_REPEAT: {
        uint32_t min = x.b, max = x.c;
        if (max == 0) return;
        if (min == 1 && max == 1) { rc_node(p, c, x.a, back); return; }
        if (rn_single(p, x.a)) {
            rc_emit(c, RE_SIMPLE | back);
            rc_emit(c, min);
            rc_emit(c, max);
            rc_emit(c, x.d);
            uint32_t at = rc_here(c);
            rc_emit(c, 0);
            rc_emit(c, 0);          /* the literal that follows, plus 1 (set by its sequence, RN_SEQ's case) */
            rc_one(p, c, x.a, back);
            c->code[at] = rc_here(c) - at - 2;
            return;
        }
        uint32_t r = c->nloops++;
        rc_emit(c, RE_LOOP_INIT);
        rc_emit(c, r);
        uint32_t head = rc_here(c);
        rc_emit(c, RE_LOOP);
        rc_emit(c, r);
        rc_emit(c, min);
        rc_emit(c, max);
        rc_emit(c, x.d);
        uint32_t exit_at = rc_here(c);
        rc_emit(c, 0);
        rc_emit(c, RE_LOOP_BODY);
        rc_emit(c, r);
        if (x.f > x.e) { rc_emit(c, RE_RESET); rc_emit(c, 2 * x.e); rc_emit(c, 2 * x.f); }
        rc_node(p, c, x.a, back);
        rc_emit(c, RE_LOOP_NEXT);
        rc_emit(c, r);
        rc_emit(c, min);
        uint32_t back_at = rc_here(c);
        rc_emit(c, 0);
        rc_patch(c, back_at, back_at + 1, head);
        rc_patch(c, exit_at, exit_at + 1, rc_here(c));
        return;
    }
    default: return;
    }
}

/* the first character every match begins with, when there is one (a literal, case kept): plus 1, else 0 */
static uint32_t rc_first(re_parser *p, uint32_t n) {
    for (int guard = 0; guard < 64; guard++) {
        re_node x = p->nodes[n];
        if (x.kind == RN_CHAR) return (x.fl & RF_I) || x.a > 0xFFFF ? 0 : x.a + 1;
        if (x.kind == RN_GROUP) { n = x.a; continue; }
        if (x.kind == RN_SEQ) {
            if (x.b == 0) return 0;
            n = p->lists[x.a];
            continue;
        }
        if (x.kind == RN_REPEAT && x.b >= 1) { n = x.a; continue; }
        return 0;
    }
    return 0;
}
/* does every match begin at the input's start (a ^ without m first)? */
static int rc_anchored(re_parser *p, uint32_t n) {
    for (int guard = 0; guard < 64; guard++) {
        re_node x = p->nodes[n];
        if (x.kind == RN_BOL) return !(x.fl & RF_M);
        if (x.kind == RN_GROUP) { n = x.a; continue; }
        if (x.kind == RN_SEQ && x.b > 0) { n = p->lists[x.a]; continue; }
        return 0;
    }
    return 0;
}

/* the characters a match can begin with: the ASCII ones a bitmap; high, any beyond
   ASCII can; any, nothing is known (a back reference, a negated class) */
typedef struct re_first { uint32_t bits[4]; uint32_t high, any; } re_first;

static void rf_add(re_first *f, uint32_t c) {
    if (c < 128) f->bits[c >> 5] |= 1u << (c & 31);
    else f->high = 1;
}
/* n's first characters into f: 1 when n can match the empty string (so what follows it adds its own) */
static int rf_node(re_parser *p, uint32_t n, re_first *f, uint32_t depth) {
    if (depth > 64) { f->any = 1; return 1; }
    re_node x = p->nodes[n];
    /* a v class's set holds its case already */
    int icase = (x.fl & RF_I) != 0 && !(x.kind == RN_CLASS && x.d), uni = rp_unicode(p);
    switch (x.kind) {
    case RN_STRSET: {
        int empty = 0;
        if (icase) { f->any = 1; return 1; }
        for (uint32_t i = 0; i < x.b; i++) {
            uint32_t id = p->vl[x.a + i];
            if (p->vs[2 * id + 1] == 0) empty = 1;
            else rf_add(f, p->vc[p->vs[2 * id]]);
        }
        for (uint32_t i = 0; i < x.d; i++) {
            uint32_t lo = p->ranges[2 * (x.c + i)], hi = p->ranges[2 * (x.c + i) + 1];
            if (hi >= 128) f->high = 1;
            for (uint32_t c = lo; c <= hi && c < 128; c++) rf_add(f, c);
        }
        return empty;
    }
    case RN_CHAR: {
        rf_add(f, x.a);
        if (icase) {
            /* the other members of its orbit */
            uint32_t y = x.a;
            for (int k = 0; k < 8; k++) { y = re_orbit_next(y, uni); if (y == x.a) break; rf_add(f, y); }
            if (x.a < 128 && ((x.a | 0x20) >= 'a' && (x.a | 0x20) <= 'z')) { rf_add(f, x.a | 0x20); rf_add(f, x.a & ~0x20u); }
        }
        return 0;
    }
    case RN_ANY: f->any = 1; return 0;
    case RN_CLASS: {
        if (x.c) { f->any = 1; return 0; }
        for (uint32_t i = 0; i < x.b; i++) {
            uint32_t lo = p->ranges[2 * (x.a + i)], hi = p->ranges[2 * (x.a + i) + 1];
            if (hi >= 128) f->high = 1;
            if (icase && hi >= 128) f->any = 1;      /* a character beyond ASCII may take an ASCII one's case */
            for (uint32_t c = lo; c <= hi && c < 128; c++) {
                rf_add(f, c);
                if (icase && ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')) { rf_add(f, c | 0x20); rf_add(f, c & ~0x20u); }
            }
        }
        return 0;
    }
    case RN_BOL: case RN_EOL: case RN_WORDB: case RN_NWORDB: case RN_LOOK: case RN_EMPTY: return 1;
    case RN_BACKREF: case RN_NAMEDREF: f->any = 1; return 1;
    case RN_GROUP: return rf_node(p, x.a, f, depth + 1);
    case RN_SEQ:
        for (uint32_t i = 0; i < x.b; i++) if (!rf_node(p, p->lists[x.a + i], f, depth + 1)) return 0;
        return 1;
    case RN_ALT: {
        int empty = 0;
        for (uint32_t i = 0; i < x.b; i++) if (rf_node(p, p->lists[x.a + i], f, depth + 1)) empty = 1;
        return empty;
    }
    case RN_REPEAT:
        if (x.c == 0) return 1;
        return rf_node(p, x.a, f, depth + 1) || x.b == 0;
    default: f->any = 1; return 1;
    }
}

/* the literal every match begins with: the root's leading characters (a group's first
   among them), as many as 64, each read under the pattern's own i (no modifier's); with
   i only ASCII ones, folded, whose orbits are their two cases. 0 for none */
static uint32_t rc_prefix(re_parser *p, uint32_t root, uint32_t *out) {
    uint32_t n = 0;
    re_node x = p->nodes[root];
    if (x.kind == RN_GROUP) x = p->nodes[x.a];
    if (x.kind != RN_SEQ) return 0;
    int icase = (p->flags & RF_I) != 0, uni = rp_unicode(p);
    for (uint32_t i = 0; i < x.b && n < 64; i++) {
        re_node t = p->nodes[p->lists[x.a + i]];
        if (t.kind == RN_GROUP && i == 0 && p->nodes[t.a].kind == RN_SEQ) {
            /* a leading group's own leading characters */
            re_node g = p->nodes[t.a];
            uint32_t k = 0;
            for (; k < g.b && n < 64; k++) {
                re_node c = p->nodes[p->lists[g.a + k]];
                if (c.kind != RN_CHAR || c.a >= 0xD800 || ((c.fl & RF_I) != 0) != icase ||
                    (icase && (c.a >= 128 || (uni && ((c.a | 0x20) == 'k' || (c.a | 0x20) == 's'))))) break;
                out[n++] = icase && c.a >= 'A' && c.a <= 'Z' ? c.a + 32 : c.a;
            }
            if (k < g.b) break;
            continue;
        }
        if (t.kind != RN_CHAR || t.a >= 0xD800 || ((t.fl & RF_I) != 0) != icase ||
            (icase && (t.a >= 128 || (uni && ((t.a | 0x20) == 'k' || (t.a | 0x20) == 's'))))) break;
        out[n++] = icase && t.a >= 'A' && t.a <= 'Z' ? t.a + 32 : t.a;
    }
    return n;
}

/* does the program begin with a greedy loop of one character class, as long as it runs
   (after the whole match's and its groups' starts), in a pattern without back
   references? Its place plus 1, else 0. A try that failed there fails from every place
   the loop's run covered: each would try what follows at places the first tried */
static uint32_t rc_lead(re_parser *p, const uint32_t *code, uint32_t len) {
    for (size_t i = 1; i < len(p->nodes); i++) if (p->nodes[i].kind == RN_BACKREF || p->nodes[i].kind == RN_NAMEDREF) return 0;
    uint32_t pc = 0;
    while (pc + 1 < len && code[pc] == RE_SAVE && code[pc + 1] % 2 == 0) pc += 2;
    if (pc + 6 < len && code[pc] == RE_SIMPLE && code[pc + 3] && code[pc + 2] == RE_INF) return pc + 1;
    return 0;
}

/* ---- compiling ---- */

/* a flags string's bits: RE_INF when a flag is unknown or given twice (or u with v) */
static uint32_t re_flags_of(js_vm *vm, uint32_t fs) {
    js_str_flatten(vm, fs);
    uint32_t n = vm->strs[fs].len, o = vm->strs[fs].off, bits = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[o + i];
        uint32_t b = c == 'g' ? RF_G : c == 'i' ? RF_I : c == 'm' ? RF_M : c == 's' ? RF_S : c == 'u' ? RF_U :
                     c == 'y' ? RF_Y : c == 'd' ? RF_D : c == 'v' ? RF_V : 0;
        if (!b || (bits & b)) return RE_INF;
        bits |= b;
    }
    if ((bits & RF_U) && (bits & RF_V)) return RE_INF;
    return bits;
}

static void re_flags_text(uint32_t f, char *b) {
    uint32_t k = 0;
    if (f & RF_D) b[k++] = 'd';
    if (f & RF_G) b[k++] = 'g';
    if (f & RF_I) b[k++] = 'i';
    if (f & RF_M) b[k++] = 'm';
    if (f & RF_S) b[k++] = 's';
    if (f & RF_U) b[k++] = 'u';
    if (f & RF_V) b[k++] = 'v';
    if (f & RF_Y) b[k++] = 'y';
    b[k] = 0;
}

/* "Invalid regular expression: /src/flags: why" into m: the flags as fs writes them (a
   literal's, as written), or with fs 0 in the standard's order (the constructor's) */
static void re_error_text(js_vm *vm, uint32_t src, uint32_t flags, uint32_t fs, const char *why, vector<char> *m) {
    const char *pre = "Invalid regular expression: /";
    append_range(*m, pre, strlen(pre));
    js_str_to_utf8(vm, src, m);
    push_back(*m, '/');
    if (fs) js_str_to_utf8(vm, fs, m);
    else {
        char fb[12];
        re_flags_text(flags, fb);
        append_range(*m, fb, strlen(fb));
    }
    append_range(*m, ": ", 2);
    append_range(*m, why, strlen(why));
}

static uint32_t re_hash_of(uint32_t atom, uint32_t flags) { return (atom * 2654435761u) ^ (flags * 40503u); }

static uint32_t re_compile(js_vm *vm, uint32_t src, uint32_t flags, uint32_t *why);

/* the pattern src (a string) compiled with flags (bits): its regex's index, or 0 with a
   SyntaxError thrown. A pattern compiled once is found again */
uint32_t js_re_compile(js_vm *vm, uint32_t src, uint32_t flags) {
    uint32_t why = 0;
    uint32_t re = re_compile(vm, src, flags, &why);
    if (re) return re;
    vector<char> m;
    re_error_text(vm, src, flags, 0, re_errors[why], &m);
    push_back(m, 0);
    js_throw_msg(vm, vm->syntax_error_proto, data(m));
    return 0;
}

/* js_re_compile's work: 0 with *why the parser's error when the pattern is not one */
static uint32_t re_compile(js_vm *vm, uint32_t src, uint32_t flags, uint32_t *why) {
    uint32_t atom = js_atom(vm, src);
    if (len(vm->re_hash) == 0) resize(vm->re_hash, 256, 0);
    {
        size_t mask = len(vm->re_hash) - 1;
        for (size_t i = re_hash_of(atom, flags) & mask;; i = (i + 1) & mask) {
            uint32_t r = vm->re_hash[i];
            if (!r) break;
            if (vm->regexes[r].source == atom && vm->regexes[r].flags == flags) return r;
        }
    }
    if (len(vm->regexes) == 0) { js_regex r0; memset(&r0, 0, sizeof r0); push_back(vm->regexes, r0); }
    re_parser ps;
    memset(&ps, 0, sizeof ps);
    defer(ps) { RE_PARSER_DROP(ps); }
    re_parser *p = &ps;
    p->flags = flags;
    p->cur = flags & (RF_I | RF_M | RF_S);
    js_str_flatten(vm, atom);
    {
        uint32_t n = vm->strs[atom].len, o = vm->strs[atom].off;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = vm->units[o + i];
            if (rp_unicode(p) && c >= 0xD800 && c <= 0xDBFF && i + 1 < n && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (vm->units[o + i + 1] - 0xDC00u);
                i++;
            }
            push_back(p->pat, c);
        }
    }
    re_node n0;
    memset(&n0, 0, sizeof n0);
    push_back(p->nodes, n0);
    rp_prescan(p);
    uint32_t root = rp_disjunction(p);
    if (!p->err && rp_at(p, 0) >= 0) re_fail(p, rp_is(p, 0, ')') ? RE_E_UNMATCHED_PAREN : RE_E_GROUP);
    /* the named references resolved: each the group of its name */
    for (size_t i = 1; i < len(p->nodes) && !p->err; i++) {
        if (p->nodes[i].kind != RN_NAMEDREF) continue;
        uint32_t g = 0;
        for (size_t j = 0; j < len(p->names); j += 3)
            if (re_name_eq(p, p->names[j + 1], p->names[j + 2], p->nodes[i].a, p->nodes[i].b)) g = p->names[j];
        if (!g) { re_fail(p, RE_E_NAMED_REF); break; }
        p->nodes[i].a = g;
    }
    if (p->err) { *why = p->err; return 0; }
    re_comp cs;
    memset(&cs, 0, sizeof cs);
    defer(cs) { vector_uint32_t_drop(&cs.code); }
    re_comp *c = &cs;
    rc_emit(c, RE_SAVE);
    rc_emit(c, 0);
    rc_node(p, c, root, 0);
    rc_emit(c, RE_SAVE);
    rc_emit(c, 1);
    rc_emit(c, RE_MATCH);
    if (c->err) { *why = c->err; return 0; }
    js_regex r;
    memset(&r, 0, sizeof r);
    r.code = (uint32_t)len(vm->re_code);
    r.len = (uint32_t)len(c->code);
    r.ngroups = p->ngroups + 1;
    r.flags = flags;
    r.source = atom;
    r.nregs = c->nloops;
    r.first = rc_first(p, root);
    r.anchored = (uint32_t)rc_anchored(p, root);
    {
        re_first f;
        memset(&f, 0, sizeof f);
        if (!rf_node(p, root, &f, 0) && !f.any) {
            r.filter = 1;
            r.fb0 = f.bits[0];
            r.fb1 = f.bits[1];
            r.fb2 = f.bits[2];
            r.fb3 = f.bits[3];
            r.high = f.high;
        }
    }
    for (size_t i = 0; i < len(c->code); i++) push_back(vm->re_code, c->code[i]);
    r.lead = rc_lead(p, data(c->code), (uint32_t)len(c->code));
    {
        /* a literal prefix and its Horspool skips, by a code unit's low byte (ASCII folded with i) */
        uint32_t pre[64];
        uint32_t np = rc_prefix(p, root, pre);
        if (np >= 2) {
            /* the prefix's last character's bytes skip 0 (the place compared whole), and
               the shift after a place that was not a match follows the table at 256 */
            r.pre = (uint32_t)len(vm->re_code);
            r.npre = np;
            for (uint32_t i = 0; i < np; i++) push_back(vm->re_code, pre[i]);
            for (uint32_t b = 0; b < 257; b++) push_back(vm->re_code, np);
            for (uint32_t i = 0; i + 1 < np; i++) vm->re_code[r.pre + np + (pre[i] & 0xFF)] = np - 1 - i;
            if ((flags & RF_I)) for (uint32_t i = 0; i + 1 < np; i++)
                if (pre[i] >= 'a' && pre[i] <= 'z') vm->re_code[r.pre + np + (pre[i] - 32)] = np - 1 - i;
            uint32_t lb = pre[np - 1] & 0xFF;
            vm->re_code[r.pre + np + 256] = vm->re_code[r.pre + np + lb];
            vm->re_code[r.pre + np + lb] = 0;
            if ((flags & RF_I) && pre[np - 1] >= 'a' && pre[np - 1] <= 'z') vm->re_code[r.pre + np + (pre[np - 1] - 32)] = 0;
        }
    }
    if (len(p->names)) {
        /* the names, by group: an array of strings, undefined for a group without one */
        uint32_t arr = js_array_new(vm, 0);
        for (uint32_t g = 0; g <= p->ngroups; g++) js_array_push(vm, arr, JV_UNDEF);
        for (size_t j = 0; j < len(p->names); j += 3) {
            vector<uint16_t> u;
            for (uint32_t k = 0; k < p->names[j + 2]; k++) push_back(u, (uint16_t)p->namech[p->names[j + 1] + k]);
            js_array_set(vm, arr, p->names[j], jv_str(js_str_new(vm, data(u), len(u))));
        }
        r.names = arr;
    }
    push_back(vm->regexes, r);
    uint32_t id = (uint32_t)(len(vm->regexes) - 1);
    if (2 * len(vm->regexes) > len(vm->re_hash)) {
        /* the table doubled, every regex placed again */
        size_t m = 2 * len(vm->re_hash);
        clear(vm->re_hash);
        resize(vm->re_hash, m, 0);
        for (uint32_t k = 1; k < id; k++) {
            size_t mask = m - 1;
            size_t i = re_hash_of(vm->regexes[k].source, vm->regexes[k].flags) & mask;
            while (vm->re_hash[i]) i = (i + 1) & mask;
            vm->re_hash[i] = k;
        }
    }
    {
        size_t mask = len(vm->re_hash) - 1;
        size_t i = re_hash_of(atom, flags) & mask;
        while (vm->re_hash[i]) i = (i + 1) & mask;
        vm->re_hash[i] = id;
    }
    return id;
}

/* a literal's pattern and flags checked as the compiler reads it: 0 when well formed, else
   the SyntaxError's text into err */
int js_re_check(js_vm *vm, uint32_t src, uint32_t flags_str, uint32_t *re, vector<char> *err) {
    uint32_t flags = re_flags_of(vm, flags_str);
    if (flags == RE_INF) {
        const char *m = "Invalid regular expression flags";
        append_range(*err, m, strlen(m));
        return 1;
    }
    uint32_t why = 0;
    *re = re_compile(vm, src, flags, &why);
    if (*re) return 0;
    re_error_text(vm, src, flags, flags_str, re_errors[why], err);
    return 1;
}

/* ---- the matcher ---- */

/* the backtrack stack's entries, four words each: a choice (pc, pos), a slot's or a
   register's old value, a simple loop's give-back (greedy) or take-more (lazy), a
   lookaround's frame */
enum { BT_CHOICE, BT_CAP, BT_REG, BT_SIMPLE, BT_SIMPLE_LAZY, BT_LOOK };

static void bt_push(vector<uint32_t> *st, uint32_t kind, uint32_t a, uint32_t b, uint32_t c) {
    push_back(*st, kind);
    push_back(*st, a);
    push_back(*st, b);
    push_back(*st, c);
}

/* the character at i reading forward (with u a pair whole): *w its width */
static uint32_t re_fwd(const uint16_t *s, uint32_t n, uint32_t i, int uni, uint32_t *w) {
    uint32_t c = s[i];
    *w = 1;
    if (uni && c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
        *w = 2;
        return 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00u);
    }
    return c;
}
/* the character before i reading backward */
static uint32_t re_bwd(const uint16_t *s, uint32_t i, int uni, uint32_t *w) {
    uint32_t c = s[i - 1];
    *w = 1;
    if (uni && c >= 0xDC00 && c <= 0xDFFF && i >= 2 && s[i - 2] >= 0xD800 && s[i - 2] <= 0xDBFF) {
        *w = 2;
        return 0x10000 + (((uint32_t)s[i - 2] - 0xD800) << 10) + (c - 0xDC00);
    }
    return c;
}

static int re_in(const uint32_t *prog, uint32_t at, uint32_t n, uint32_t c) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (prog[at + 2 * mid + 1] < c) lo = mid + 1;
        else hi = mid;
    }
    return lo < n && prog[at + 2 * lo] <= c;
}

/* does the one-character instruction at pc take c? */
static int re_takes(const uint32_t *prog, uint32_t pc, uint32_t c, int uni) {
    uint32_t w = prog[pc], op = w & 0xFF;
    if (op == RE_CHAR) return c == prog[pc + 1];
    if (op == RE_CHAR_I) return re_rep(c, uni) == prog[pc + 1];
    if (op == RE_ANY) return !re_line_term(c);
    if (op == RE_ANY_ALL) return 1;
    uint32_t n = prog[pc + 1];
    int in = re_in(prog, pc + 2, n, c);
    if (!in && (w & RB_ICASE)) {
        uint32_t x = c;
        for (int k = 0; k < 8 && !in; k++) {
            x = re_orbit_next(x, uni);
            if (x == c) break;
            in = re_in(prog, pc + 2, n, x);
        }
    }
    return (w & RB_NEG) ? !in : in;
}

static int re_is_word(const uint16_t *s, uint32_t n, int64_t i, int iu) {
    if (i < 0 || i >= (int64_t)n) return 0;
    uint32_t c = s[i];
    return re_word_char(c) || (iu && (c == 0x17F || c == 0x212A));
}

#define RE_STEP_CHUNK 4096u

/* the program at prog[0] run on s[0, n) from pos0: 1 a match, 0 none, -1 the budget spent.
   cap holds 2 slots a group (RE_INF unset), regs 2 a loop; *steps counts what ran */
static int re_run(const uint32_t *prog, const uint16_t *s, uint32_t n, uint32_t pos0, uint32_t *cap, uint32_t *regs,
                  vector<uint32_t> *st, uint32_t flags, uint64_t *steps, uint64_t budget) {
    int uni = (flags & (RF_U | RF_V)) != 0;
    uint32_t pc = 0, pos = pos0;
    uint64_t count = 0;
    clear(*st);
    for (;;) {
    next:
        if (++count >= RE_STEP_CHUNK) { *steps += count; count = 0; if (*steps > budget) return -1; }
        {
            uint32_t w = prog[pc], op = w & 0xFF;
            uint32_t back = w & RB_BACK;
            switch (op) {
            case RE_CHAR: case RE_CHAR_I: case RE_ANY: case RE_ANY_ALL: case RE_CLASS: {
                uint32_t width, c;
                if (back) { if (pos == 0) goto fail; c = re_bwd(s, pos, uni, &width); }
                else { if (pos >= n) goto fail; c = re_fwd(s, n, pos, uni, &width); }
                if (!re_takes(prog, pc, c, uni)) goto fail;
                pos = back ? pos - width : pos + width;
                pc += op == RE_CLASS ? 2 + 2 * prog[pc + 1] : (op == RE_ANY || op == RE_ANY_ALL ? 1 : 2);
                goto next;
            }
            case RE_BOL:
                if (pos == 0 || ((w & RB_MULTI) && re_line_term(s[pos - 1]))) { pc++; goto next; }
                goto fail;
            case RE_EOL:
                if (pos == n || ((w & RB_MULTI) && re_line_term(s[pos]))) { pc++; goto next; }
                goto fail;
            case RE_WORDB: {
                int iu = uni && (w & RB_ICASE);
                int a = re_is_word(s, n, (int64_t)pos - 1, iu), b = re_is_word(s, n, pos, iu);
                int at_b = a != b;
                if ((w & RB_NEG) ? at_b : !at_b) goto fail;
                pc++;
                goto next;
            }
            case RE_SPLIT:
                bt_push(st, BT_CHOICE, (uint32_t)((int32_t)(pc + 2) + (int32_t)prog[pc + 1]), pos, 0);
                pc += 2;
                goto next;
            case RE_JMP: pc = (uint32_t)((int32_t)(pc + 2) + (int32_t)prog[pc + 1]); goto next;
            case RE_SAVE: {
                /* an undo only with a choice or a lookaround below it: with none a failure
                   ends the run, and the next run starts with the slots unset */
                uint32_t k = prog[pc + 1];
                if (len(*st)) bt_push(st, BT_CAP, k, cap[k], 0);
                cap[k] = pos;
                pc += 2;
                goto next;
            }
            case RE_RESET: {
                for (uint32_t k = prog[pc + 1]; k < prog[pc + 2]; k++) {
                    if (cap[k] == RE_INF) continue;
                    if (len(*st)) bt_push(st, BT_CAP, k, cap[k], 0);
                    cap[k] = RE_INF;
                }
                pc += 3;
                goto next;
            }
            case RE_BACKREF: {
                uint32_t g = prog[pc + 1];
                uint32_t a = cap[2 * g], b = cap[2 * g + 1];
                pc += 2;
                if (a == RE_INF || b == RE_INF || b <= a) goto next;
                uint32_t l = b - a;
                if (back) {
                    if (pos < l) goto fail;
                    uint32_t from = pos - l;
                    for (uint32_t k = 0; k < l; k++) {
                        uint32_t x = s[a + k], y = s[from + k];
                        if (x != y && (!(w & RB_ICASE) || re_rep(x, uni) != re_rep(y, uni))) goto fail;
                    }
                    pos = from;
                } else {
                    if (pos + l > n) goto fail;
                    for (uint32_t k = 0; k < l; k++) {
                        uint32_t x = s[a + k], y = s[pos + k];
                        if (x != y && (!(w & RB_ICASE) || re_rep(x, uni) != re_rep(y, uni))) goto fail;
                    }
                    pos += l;
                }
                goto next;
            }
            case RE_LOOK:
                bt_push(st, BT_LOOK, pc, pos, 0);
                pc += 3;
                goto next;
            case RE_LOOK_END: {
                /* the sub-match done: its frame found below its own entries */
                size_t top = len(*st);
                size_t f = top;
                while (f >= 4 && (*st)[f - 4] != BT_LOOK) f -= 4;
                if (f < 4) return 0;
                f -= 4;
                uint32_t lpc = (*st)[f + 1], lpos = (*st)[f + 2];
                uint32_t kind = prog[lpc + 1];
                uint32_t after = (uint32_t)((int32_t)(lpc + 3) + (int32_t)prog[lpc + 2]);
                if (kind == 0 || kind == 2) {
                    /* it held: its choices dropped, the undo of its captures kept, the frame gone */
                    size_t out = f;
                    for (size_t i = f + 4; i < top; i += 4) {
                        uint32_t k = (*st)[i];
                        if (k != BT_CAP && k != BT_REG) continue;
                        for (uint32_t j = 0; j < 4; j++) (*st)[out + j] = (*st)[i + j];
                        out += 4;
                    }
                    resize(*st, out, 0);
                    pos = lpos;
                    pc = after;
                    goto next;
                }
                /* a negative one matched: undone to its frame, and failed */
                while (len(*st) > f + 4) {
                    size_t t = len(*st) - 4;
                    uint32_t k = (*st)[t];
                    if (k == BT_CAP) cap[(*st)[t + 1]] = (*st)[t + 2];
                    else if (k == BT_REG) regs[(*st)[t + 1]] = (*st)[t + 2];
                    resize(*st, t, 0);
                }
                resize(*st, f, 0);
                goto fail;
            }
            case RE_LOOP_INIT: {
                uint32_t r = prog[pc + 1];
                if (len(*st)) bt_push(st, BT_REG, 2 * r, regs[2 * r], 0);
                regs[2 * r] = 0;
                pc += 2;
                goto next;
            }
            case RE_LOOP: {
                uint32_t r = prog[pc + 1], min = prog[pc + 2], max = prog[pc + 3], greedy = prog[pc + 4];
                uint32_t body = pc + 6, ex = (uint32_t)((int32_t)(pc + 6) + (int32_t)prog[pc + 5]);
                uint32_t k = regs[2 * r];
                (void)r;
                if (k < min) { pc = body; goto next; }
                if (max != RE_INF && k >= max) { pc = ex; goto next; }
                if (greedy) { bt_push(st, BT_CHOICE, ex, pos, 0); pc = body; }
                else { bt_push(st, BT_CHOICE, body, pos, 0); pc = ex; }
                goto next;
            }
            case RE_LOOP_BODY: {
                uint32_t r = prog[pc + 1];
                if (len(*st)) bt_push(st, BT_REG, 2 * r + 1, regs[2 * r + 1], 0);
                regs[2 * r + 1] = pos;
                pc += 2;
                goto next;
            }
            case RE_LOOP_NEXT: {
                uint32_t r = prog[pc + 1], min = prog[pc + 2];
                /* an iteration past the least that took nothing ends the loop's tries here */
                if (pos == regs[2 * r + 1] && regs[2 * r] >= min) goto fail;
                if (len(*st)) bt_push(st, BT_REG, 2 * r, regs[2 * r], 0);
                regs[2 * r]++;
                pc = (uint32_t)((int32_t)(pc + 4) + (int32_t)prog[pc + 3]);
                goto next;
            }
            case RE_SIMPLE: {
                /* min, max, greedy, the atom's length, the literal that follows (plus 1), the atom */
                uint32_t min = prog[pc + 1], max = prog[pc + 2], greedy = prog[pc + 3], al = prog[pc + 4], lit = prog[pc + 5];
                uint32_t atom = pc + 6, after = pc + 6 + al;
                uint32_t k = 0, p = pos, width = 1;
                if (greedy && !back && !uni) {
                    /* forward without u: a code unit a character, the atom's test in line */
                    uint32_t aw = prog[atom], aop = aw & 0xFF, lim = max < n - p ? max : n - p;
                    if (aop == RE_CHAR) {
                        uint32_t ch = prog[atom + 1];
                        while (k < lim && s[p + k] == ch) k++;
                    } else if (aop == RE_ANY) {
                        while (k < lim && !re_line_term(s[p + k])) k++;
                    } else if (aop == RE_ANY_ALL) {
                        k = lim;
                    } else if (aop == RE_CLASS && !(aw & RB_ICASE) && prog[atom + 1] <= 8) {
                        uint32_t nr = prog[atom + 1], neg = (aw & RB_NEG) ? 1u : 0u;
                        while (k < lim) {
                            uint32_t c = s[p + k], in = 0;
                            for (uint32_t j = 0; j < nr; j++) {
                                if (c < prog[atom + 2 + 2 * j]) break;
                                if (c <= prog[atom + 3 + 2 * j]) { in = 1; break; }
                            }
                            if (in == neg) break;
                            k++;
                        }
                    } else {
                        while (k < lim && re_takes(prog, atom, s[p + k], 0)) k++;
                    }
                    p += k;
                    count += k & ~1023u;
                    if (k < min) goto fail;
                    if (lit) while (k > min && (p >= n || s[p] != lit - 1)) { p--; k--; }
                    if (k > min) bt_push(st, BT_SIMPLE, pc, p, k);
                } else if (greedy) {
                    while (k < max) {
                        uint32_t c;
                        if (back) { if (p == 0) break; c = re_bwd(s, p, uni, &width); }
                        else { if (p >= n) break; c = re_fwd(s, n, p, uni, &width); }
                        if (!re_takes(prog, atom, c, uni)) break;
                        p = back ? p - width : p + width;
                        k++;
                        if ((k & 1023) == 0) { count += 1024; }
                    }
                    if (k < min) goto fail;
                    /* the places the literal is not at given back at once (code units: one each) */
                    if (lit) while (k > min && (p >= n || s[p] != lit - 1)) { p--; k--; }
                    if (k > min) bt_push(st, BT_SIMPLE, pc, p, k);
                } else {
                    while (k < min) {
                        uint32_t c;
                        if (back) { if (p == 0) goto fail; c = re_bwd(s, p, uni, &width); }
                        else { if (p >= n) goto fail; c = re_fwd(s, n, p, uni, &width); }
                        if (!re_takes(prog, atom, c, uni)) goto fail;
                        p = back ? p - width : p + width;
                        k++;
                    }
                    /* lazy before a literal: on to where the literal is, as far as the atom takes */
                    if (lit) while (k < max && p < n && s[p] != lit - 1 && re_takes(prog, atom, s[p], uni)) { p++; k++; }
                    if (k < max) bt_push(st, BT_SIMPLE_LAZY, pc, p, k);
                }
                pos = p;
                pc = after;
                goto next;
            }
            case RE_SWITCH: {
                /* a trie's level: the next character among its children's, by a binary search */
                uint32_t width, c;
                if (back) { if (pos == 0) goto fail; c = re_bwd(s, pos, uni, &width); }
                else { if (pos >= n) goto fail; c = re_fwd(s, n, pos, uni, &width); }
                if (w & RB_ICASE) c = re_rep(c, uni);
                uint32_t nk = prog[pc + 1], lo = 0, hi = nk;
                while (lo < hi) {
                    uint32_t mid = (lo + hi) / 2;
                    if (prog[pc + 2 + 2 * mid] < c) lo = mid + 1;
                    else hi = mid;
                }
                if (lo >= nk || prog[pc + 2 + 2 * lo] != c) goto fail;
                pos = back ? pos - width : pos + width;
                pc = (uint32_t)((int32_t)(pc + 2 + 2 * nk) + (int32_t)prog[pc + 3 + 2 * lo]);
                goto next;
            }
            case RE_MATCH:
                *steps += count;
                return 1;
            default: return 0;
            }
        }
    fail:
        /* the last choice taken back: undo entries applied on the way */
        for (;;) {
            size_t t = len(*st);
            if (t == 0) { *steps += count; return 0; }
            t -= 4;
            uint32_t kind = (*st)[t], a = (*st)[t + 1], b = (*st)[t + 2], c3 = (*st)[t + 3];
            resize(*st, t, 0);
            if (++count >= RE_STEP_CHUNK) { *steps += count; count = 0; if (*steps > budget) return -1; }
            if (kind == BT_CAP) { cap[a] = b; goto pop; }
            if (kind == BT_REG) { regs[a] = b; goto pop; }
            if (kind == BT_CHOICE) { pc = a; pos = b; goto next; }
            if (kind == BT_LOOK) {
                /* its sub-match failed: a negative one holds, a positive one fails */
                uint32_t lk = prog[a + 1];
                if (lk == 1 || lk == 3) { pos = b; pc = (uint32_t)((int32_t)(a + 3) + (int32_t)prog[a + 2]); goto next; }
                goto pop;
            }
            {
                uint32_t sw = prog[a], sback = sw & RB_BACK;
                uint32_t min = prog[a + 1], max = prog[a + 2], al = prog[a + 4], lit = prog[a + 5];
                uint32_t atom = a + 6, after = a + 6 + al;
                uint32_t width = 1;
                if (kind == BT_SIMPLE) {
                    /* one character given back, and those the literal is not at */
                    uint32_t p = b, k = c3 - 1;
                    if (sback) { re_fwd(s, n, p, uni, &width); p += width; }
                    else { re_bwd(s, p, uni, &width); p -= width; }
                    if (lit) while (k > min && s[p] != lit - 1) { p--; k--; }
                    if (k > min) bt_push(st, BT_SIMPLE, a, p, k);
                    pos = p;
                    pc = after;
                    goto next;
                }
                /* lazy: one more taken (and on to the literal) */
                uint32_t p = b, k = c3, c;
                if (sback) { if (p == 0) goto pop; c = re_bwd(s, p, uni, &width); }
                else { if (p >= n) goto pop; c = re_fwd(s, n, p, uni, &width); }
                if (!re_takes(prog, atom, c, uni)) goto pop;
                p = sback ? p - width : p + width;
                k++;
                if (lit) while (k < max && p < n && s[p] != lit - 1 && re_takes(prog, atom, s[p], uni)) { p++; k++; }
                if (k < max) bt_push(st, BT_SIMPLE_LAZY, a, p, k);
                pos = p;
                pc = after;
                goto next;
            }
        pop:;
        }
    }
}

/* Horspool over the literal prefix pre[0, np) in str[0, n) from i: the first place it is
   at, else n + 1. The place's last code unit's skip (two a turn), 0 where it may be the
   prefix's last character, the place then compared; q the place's last unit */
static uint32_t re_find_prefix(const uint16_t *str, uint32_t n, uint32_t i, const uint32_t *pre, uint32_t np, int icase) {
    const uint32_t *skips = pre + np;
    uint32_t shift = skips[256], q = i + np - 1;
    while (q < n) {
        uint32_t skip = skips[str[q] & 0xFF];
        if (skip) {
            q += skip;
            if (q >= n) break;
            skip = skips[str[q] & 0xFF];
            if (skip) { q += skip; continue; }
        }
        uint32_t k = 0, at = q + 1 - np;
        while (k < np) {
            uint32_t d = str[at + k];
            uint32_t fd = icase && d >= 'A' && d <= 'Z' ? d + 32 : d;
            if (fd != pre[k]) break;
            k++;
        }
        if (k >= np) return at;
        q += shift;
    }
    return n + 1;
}

/* the regex re run on the string s from index start: 1 a match (the groups' slots in
   vm->re_caps, 2 a group, RE_INF unset), 0 none, -1 the step budget spent (vm->throwing 2) */
int js_re_exec(js_vm *vm, uint32_t re, uint32_t s, uint32_t start) {
    js_regex r = vm->regexes[re];
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, off = vm->strs[s].off;
    if (len(vm->re_caps) != 2 * r.ngroups) resize(vm->re_caps, 2 * r.ngroups, 0);
    if (len(vm->re_regs) != 2 * r.nregs + 2) resize(vm->re_regs, 2 * r.nregs + 2, 0);
    int sticky = (r.flags & RF_Y) != 0, uni = (r.flags & (RF_U | RF_V)) != 0;
    uint64_t steps = vm->steps, budget = vm->step_budget;
    int found = 0;
    {
        const uint32_t *prog = data(vm->re_code) + r.code;
        const uint16_t *str = data(vm->units) + off;
        uint32_t *cap = data(vm->re_caps);
        uint32_t *regs = data(vm->re_regs);
        uint32_t fb[4] = { r.fb0, r.fb1, r.fb2, r.fb3 };
        const uint32_t *pre = data(vm->re_code) + r.pre;
        int icase = (r.flags & RF_I) != 0;
        for (uint32_t i = start; i <= n; i++) {
            if (r.npre && !sticky) {
                i = re_find_prefix(str, n, i, pre, r.npre, icase);
                if (i > n) break;
            } else if (r.first && !sticky) {
                uint32_t ch = r.first - 1;
                while (i < n && str[i] != ch) i++;
                if (i >= n) break;
            } else if (r.filter && !sticky) {
                /* the places no match can begin at passed over */
                while (i < n) {
                    uint32_t ch = str[i];
                    if (ch < 128 ? (fb[ch >> 5] >> (ch & 31)) & 1 : r.high) break;
                    i++;
                }
                if (i >= n) break;
            }
            for (uint32_t k = 0; k < 2 * r.ngroups; k++) cap[k] = RE_INF;
            int rc = re_run(prog, str, n, i, cap, regs, &vm->re_stack, r.flags, &steps, budget);
            if (rc != 0) { found = rc; break; }
            if (sticky || r.anchored) break;
            if (r.lead) {
                /* the leading loop's run from here passed over: no try in it can match */
                uint32_t e = i, w = 1;
                while (e < n && re_takes(prog, r.lead - 1 + 6, re_fwd(str, n, e, uni, &w), uni)) e += w;
                if (e > i + 1) { i = e - 1; continue; }
            }
            if (uni && i + 1 < n && str[i] >= 0xD800 && str[i] <= 0xDBFF && str[i + 1] >= 0xDC00 && str[i + 1] <= 0xDFFF) i++;
        }
    }
    vm->steps = steps;
    if (found < 0) { vm->throwing = 2; return -1; }
    return found;
}

/* ---- the natives ---- */

static jv re_arg(js_vm *vm, uint32_t args, uint32_t argc, uint32_t i) { return js_arg(vm, args, argc, i); }

/* this as a RegExp's regex, or 0 with V8's TypeError thrown: "Method <what> called on incompatible receiver <this>" */
static uint32_t re_this(js_vm *vm, jv thisv, const char *what) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_REGEXP) return vm->objs[jv_idx(thisv)].a;
    vector<char> m;
    const char *pre = "Method ", *mid = " called on incompatible receiver ";
    append_range(m, pre, strlen(pre));
    append_range(m, what, strlen(what));
    append_range(m, mid, strlen(mid));
    js_value_text(vm, thisv, &m);
    js_throw(vm, jv_obj(js_error_new(vm, vm->type_error_proto, js_str_utf8(vm, data(m), len(m)))));
    return 0;
}

/* a TypeError "<what> called on non-object <v>", V8's words */
static jv re_not_object(js_vm *vm, const char *what, jv v) {
    vector<char> m;
    append_range(m, what, strlen(what));
    const char *mid = " called on non-object ";
    append_range(m, mid, strlen(mid));
    uint32_t s = jv_is_sym(v) ? js_sym_text(vm, jv_idx(v)) : js_to_string(vm, v);
    if (s) js_str_to_utf8(vm, s, &m);
    vm->throwing = 0;
    push_back(m, 0);
    return js_throw_msg(vm, vm->type_error_proto, data(m));
}

static double re_to_length(js_vm *vm, jv v) {
    double d = js_to_number(vm, v);
    if (d != d || d <= 0) return 0;
    if (d > 9007199254740991.0) return 9007199254740991.0;
    return floor(d);
}

/* the index after i in s, a pair's whole with u */
static uint32_t re_advance(js_vm *vm, uint32_t s, uint32_t i, int uni) {
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, o = vm->strs[s].off;
    if (uni && i + 1 < n && vm->units[o + i] >= 0xD800 && vm->units[o + i] <= 0xDBFF && vm->units[o + i + 1] >= 0xDC00 && vm->units[o + i + 1] <= 0xDFFF) return i + 2;
    return i + 1;
}

/* SameValue: NaN is NaN, +0 is not -0 */
static int re_same(js_vm *vm, jv a, jv b) {
    if (jv_is_num(a) && jv_is_num(b)) {
        double x = jv_dbl(a), y = jv_dbl(b);
        if (x != x) return y != y;
        return x == y && signbit(x) == signbit(y);
    }
    return js_strict_eq(vm, a, b);
}

/* Set(R, "lastIndex", v, true) on a RegExp object: its own data property written; a
   frozen regex's or a read-only lastIndex refuses with V8's TypeError. 1 written, 0 thrown */
static int re_set_last(js_vm *vm, jv rxv, jv v) {
    uint32_t o = jv_is_obj(rxv) ? jv_idx(rxv) : 0;
    jv cur = JV_UNDEF;
    uint32_t f = 0;
    if (!o || vm->objs[o].kind != JK_REGEXP || !js_get_own(vm, o, A_LASTINDEX, &cur, &f) || (f & JS_SHAPE_ACCESSOR)) {
        js_set(vm, rxv, A_LASTINDEX, v);
        return !vm->throwing;
    }
    if ((vm->objs[o].flags & JO_FROZEN) || (f & JS_SHAPE_READONLY)) {
        js_throw_msg(vm, vm->type_error_proto, "Cannot assign to read only property 'lastIndex' of object '[object RegExp]'");
        return 0;
    }
    js_define(vm, o, A_LASTINDEX, v, f);
    return 1;
}

/* RegExpBuiltinExec's search: the start from lastIndex (g, y), a match's slots in
   vm->re_caps; lastIndex set as the flags say. 1 a match, 0 none, -1 thrown */
static int re_builtin_exec(js_vm *vm, jv rxv, uint32_t re, uint32_t s) {
    uint32_t flags = vm->regexes[re].flags;
    int global = (flags & (RF_G | RF_Y)) != 0;
    double li = 0;
    if (global) {
        li = re_to_length(vm, js_get(vm, rxv, A_LASTINDEX));
        if (vm->throwing) return -1;
    }
    if (li > vm->strs[s].len) {
        if (global && !re_set_last(vm, rxv, jv_num(0))) return -1;
        return 0;
    }
    int rc = js_re_exec(vm, re, s, (uint32_t)li);
    if (rc < 0) return -1;
    if (rc == 0) {
        if (global && !re_set_last(vm, rxv, jv_num(0))) return -1;
        return 0;
    }
    if (global && !re_set_last(vm, rxv, jv_num(vm->re_caps[1]))) return -1;
    return 1;
}

/* the match in vm->re_caps as exec's array: the groups' strings, index, input, groups
   (and indices with d) */
static jv re_result(js_vm *vm, uint32_t re, uint32_t s) {
    js_regex r = vm->regexes[re];
    vector<uint32_t> caps;
    for (uint32_t k = 0; k < 2 * r.ngroups; k++) push_back(caps, vm->re_caps[k]);
    uint32_t arr = js_array_new(vm, 0);
    for (uint32_t g = 0; g < r.ngroups; g++) {
        uint32_t a = caps[2 * g], b = caps[2 * g + 1];
        if (a == RE_INF || b == RE_INF) js_array_push(vm, arr, JV_UNDEF);
        else js_array_push(vm, arr, jv_str(js_str_sub(vm, s, a, b)));
    }
    js_define(vm, arr, A_INDEX, jv_num(caps[0]), 0);
    js_define(vm, arr, A_INPUT, jv_str(s), 0);
    jv groups = JV_UNDEF;
    if (r.names) {
        uint32_t go = js_obj_new(vm, JK_PLAIN, 0);
        for (uint32_t g = 1; g < r.ngroups; g++) {
            jv nm = js_array_get(vm, r.names, g);
            if (!jv_is_str(nm)) continue;
            js_define(vm, go, js_atom(vm, jv_idx(nm)), js_array_get(vm, arr, g), 0);
        }
        groups = jv_obj(go);
    }
    js_define(vm, arr, js_atom_ascii(vm, "groups"), groups, 0);
    if (r.flags & RF_D) {
        uint32_t ind = js_array_new(vm, 0);
        for (uint32_t g = 0; g < r.ngroups; g++) {
            uint32_t a = caps[2 * g], b = caps[2 * g + 1];
            if (a == RE_INF || b == RE_INF) { js_array_push(vm, ind, JV_UNDEF); continue; }
            uint32_t pair = js_array_new(vm, 0);
            js_array_push(vm, pair, jv_num(a));
            js_array_push(vm, pair, jv_num(b));
            js_array_push(vm, ind, jv_obj(pair));
        }
        jv ig = JV_UNDEF;
        if (r.names) {
            uint32_t go = js_obj_new(vm, JK_PLAIN, 0);
            for (uint32_t g = 1; g < r.ngroups; g++) {
                jv nm = js_array_get(vm, r.names, g);
                if (jv_is_str(nm)) js_define(vm, go, js_atom(vm, jv_idx(nm)), js_array_get(vm, ind, g), 0);
            }
            ig = jv_obj(go);
        }
        js_define(vm, ind, js_atom_ascii(vm, "groups"), ig, 0);
        js_define(vm, arr, js_atom_ascii(vm, "indices"), jv_obj(ind), 0);
    }
    return jv_obj(arr);
}

/* a replacement template's text for one match (GetSubstitution): $$ $& $` $' $n $nn $<name> */
static void re_substitute(js_vm *vm, uint32_t tpl, uint32_t s, const uint32_t *caps, uint32_t ngroups, jv groups, vector<uint16_t> *out) {
    js_str_flatten(vm, tpl);
    js_str_flatten(vm, s);
    uint32_t tn = vm->strs[tpl].len, sn = vm->strs[s].len;
    uint32_t pos = caps[0], end = caps[1];
    for (uint32_t i = 0; i < tn; i++) {
        uint16_t c = vm->units[vm->strs[tpl].off + i];
        if (c != '$' || i + 1 >= tn) { push_back(*out, c); goto next; }
        {
            uint16_t d = vm->units[vm->strs[tpl].off + i + 1];
            if (d == '$') { push_back(*out, (uint16_t)'$'); i++; goto next; }
            if (d == '&') { for (uint32_t k = pos; k < end; k++) push_back(*out, vm->units[vm->strs[s].off + k]); i++; goto next; }
            if (d == '`') { for (uint32_t k = 0; k < pos; k++) push_back(*out, vm->units[vm->strs[s].off + k]); i++; goto next; }
            if (d == '\'') { for (uint32_t k = end; k < sn; k++) push_back(*out, vm->units[vm->strs[s].off + k]); i++; goto next; }
            if (d >= '0' && d <= '9') {
                uint32_t g = d - '0', used = 1;
                if (i + 2 < tn) {
                    uint16_t e = vm->units[vm->strs[tpl].off + i + 2];
                    if (e >= '0' && e <= '9' && g * 10 + (e - '0') < ngroups && g * 10 + (e - '0') > 0) { g = g * 10 + (e - '0'); used = 2; }
                }
                if (g == 0 || g >= ngroups) { push_back(*out, c); goto next; }
                uint32_t a = caps[2 * g], b = caps[2 * g + 1];
                if (a != RE_INF && b != RE_INF) for (uint32_t k = a; k < b; k++) push_back(*out, vm->units[vm->strs[s].off + k]);
                i += used;
                goto next;
            }
            if (d == '<' && groups != JV_UNDEF) {
                uint32_t close = i + 2;
                while (close < tn && vm->units[vm->strs[tpl].off + close] != '>') close++;
                if (close >= tn) { push_back(*out, c); goto next; }
                uint32_t nm = js_str_sub(vm, tpl, i + 2, close);
                jv v = js_get(vm, groups, js_atom(vm, nm));
                if (vm->throwing) return;
                if (v != JV_UNDEF) {
                    uint32_t vs = js_to_string(vm, v);
                    if (!vs) return;
                    js_str_flatten(vm, vs);
                    js_str_flatten(vm, tpl);
                    js_str_flatten(vm, s);
                    for (uint32_t k = 0; k < vm->strs[vs].len; k++) push_back(*out, vm->units[vm->strs[vs].off + k]);
                }
                i = close;
                goto next;
            }
            push_back(*out, c);
        }
    next:;
    }
}

static void re_append(js_vm *vm, uint32_t s, uint32_t from, uint32_t to, vector<uint16_t> *out) {
    if (to <= from) return;
    js_str_flatten(vm, s);
    const uint16_t *u = data(vm->units) + vm->strs[s].off;
    append_range(*out, u + from, to - from);
}

/* a global regex's matches in s from 0 (match's and replace's), the first keep slots of
   every one onto all: each match's end the next's start, an empty one's a step further.
   lastIndex, which no code reads between them, is set to 0 first and left 0 as the last
   failed search leaves it; a regex that refuses the first write refuses them all.
   1 done, 0 thrown */
static int re_global_matches(js_vm *vm, jv rxv, uint32_t re, uint32_t s, uint32_t keep, vector<uint32_t> *all) {
    js_regex r = vm->regexes[re];
    int uni = (r.flags & (RF_U | RF_V)) != 0;
    if (!re_set_last(vm, rxv, jv_num(0))) return 0;
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, start = 0;
    while (start <= n) {
        int rc = js_re_exec(vm, re, s, start);
        if (rc < 0) return 0;
        if (rc == 0) break;
        if (keep == 2) {
            push_back(*all, vm->re_caps[0]);
            push_back(*all, vm->re_caps[1]);
        } else {
            const uint32_t *cp = data(vm->re_caps);
            append_range(*all, cp, keep);
        }
        uint32_t a = vm->re_caps[0], e = vm->re_caps[1];
        start = a == e ? re_advance(vm, s, e, uni) : e;
    }
    return re_set_last(vm, rxv, jv_num(0));
}

/* s with rx's matches replaced: every one with g (lastIndex from 0), else the first from
   exec's place; rep a function called for each, or a template */
static jv re_replace(js_vm *vm, jv rxv, uint32_t re, uint32_t s, jv rep) {
    js_regex r = vm->regexes[re];
    int global = (r.flags & RF_G) != 0;
    int fn = js_is_callable(vm, rep);
    uint32_t tpl = 0;
    if (!fn) { tpl = js_to_string(vm, rep); if (!tpl) return JV_HOLE; }
    /* every match's slots, one after another */
    vector<uint32_t> all;
    uint32_t ng = r.ngroups;
    if (global) { if (!re_global_matches(vm, rxv, re, s, 2 * ng, &all)) return JV_HOLE; }
    else {
        int rc = re_builtin_exec(vm, rxv, re, s);
        if (rc < 0) return JV_HOLE;
        if (rc) for (uint32_t k = 0; k < 2 * ng; k++) push_back(all, vm->re_caps[k]);
    }
    uint32_t nm = (uint32_t)(len(all) / (2 * ng));
    js_str_flatten(vm, s);
    if (nm == 0) return jv_str(s);
    vector<uint16_t> out;
    uint32_t next_src = 0;
    /* a template without $ is its own text for every match: the result's length known
       first, the pieces copied into it */
    int plain = 0;
    if (!fn) {
        js_str_flatten(vm, tpl);
        uint32_t tn = vm->strs[tpl].len, to = vm->strs[tpl].off, k = 0;
        while (k < tn && vm->units[to + k] != '$') k++;
        plain = k == tn;
    }
    if (plain) {
        uint32_t tn = vm->strs[tpl].len, n = vm->strs[s].len;
        uint64_t total = 0;
        for (uint32_t m = 0; m < nm; m++) {
            uint32_t pos = all[m * 2 * ng], end = all[m * 2 * ng + 1];
            if (pos < next_src) continue;
            total += (uint64_t)(pos - next_src) + tn;
            next_src = end;
        }
        total += n - next_src;
        /* V8's longest string, 2^29 - 24 units */
        if (total > 536870888u) return js_throw_msg(vm, vm->range_error_proto, "Invalid string length");
        resize(out, (size_t)total, 0);
        {
            const uint16_t *su = data(vm->units) + vm->strs[s].off;
            const uint16_t *tu = data(vm->units) + vm->strs[tpl].off;
            uint16_t *o = data(out);
            uint32_t w = 0, at = 0;
            for (uint32_t m = 0; m < nm; m++) {
                uint32_t pos = all[m * 2 * ng], end = all[m * 2 * ng + 1];
                if (pos < at) continue;
                for (uint32_t k = at; k < pos; k++) o[w++] = su[k];
                for (uint32_t k = 0; k < tn; k++) o[w++] = tu[k];
                at = end;
            }
            for (uint32_t k = at; k < n; k++) o[w++] = su[k];
        }
        return jv_str(js_str_new(vm, data(out), (uint32_t)total));
    }
    for (uint32_t m = 0; m < nm; m++) {
        vector<uint32_t> caps;
        for (uint32_t k = 0; k < 2 * ng; k++) push_back(caps, all[m * 2 * ng + k]);
        uint32_t pos = caps[0], end = caps[1];
        jv groups = JV_UNDEF;
        if (r.names) {
            uint32_t go = js_obj_new(vm, JK_PLAIN, 0);
            for (uint32_t g = 1; g < ng; g++) {
                jv nmv = js_array_get(vm, r.names, g);
                if (!jv_is_str(nmv)) continue;
                jv v = caps[2 * g] == RE_INF || caps[2 * g + 1] == RE_INF ? JV_UNDEF : jv_str(js_str_sub(vm, s, caps[2 * g], caps[2 * g + 1]));
                js_define(vm, go, js_atom(vm, jv_idx(nmv)), v, 0);
            }
            groups = jv_obj(go);
        }
        vector<uint16_t> piece;
        if (fn) {
            vector<jv> av;
            for (uint32_t g = 0; g < ng; g++) {
                if (caps[2 * g] == RE_INF || caps[2 * g + 1] == RE_INF) push_back(av, JV_UNDEF);
                else push_back(av, jv_str(js_str_sub(vm, s, caps[2 * g], caps[2 * g + 1])));
            }
            push_back(av, jv_num(pos));
            push_back(av, jv_str(s));
            if (groups != JV_UNDEF) push_back(av, groups);
            jv res = js_call(vm, rep, JV_UNDEF, data(av), (uint32_t)len(av));
            if (vm->throwing) return JV_HOLE;
            uint32_t rs = js_to_string(vm, res);
            if (!rs) return JV_HOLE;
            re_append(vm, rs, 0, vm->strs[rs].len, &piece);
        } else {
            re_substitute(vm, tpl, s, data(caps), ng, groups, &piece);
            if (vm->throwing) return JV_HOLE;
        }
        if (pos >= next_src) {
            re_append(vm, s, next_src, pos, &out);
            if (len(piece)) append_range(out, data(piece), len(piece));
            next_src = end;
        }
    }
    js_str_flatten(vm, s);
    re_append(vm, s, next_src, vm->strs[s].len, &out);
    return jv_str(js_str_new(vm, data(out), len(out)));
}

/* s split at rx's matches, its groups spliced in, at most lim pieces */
static jv re_split(js_vm *vm, uint32_t re, uint32_t s, jv limit) {
    js_regex r = vm->regexes[re];
    int uni = (r.flags & (RF_U | RF_V)) != 0;
    uint32_t lim = 0xFFFFFFFFu;
    if (limit != JV_UNDEF) {
        double d = js_to_number(vm, limit);
        if (vm->throwing) return JV_HOLE;
        lim = (uint32_t)js_to_int32(d);
    }
    uint32_t arr = js_array_new(vm, 0);
    if (lim == 0) return jv_obj(arr);
    js_str_flatten(vm, s);
    uint32_t size = vm->strs[s].len;
    /* the splitter is sticky: each place tried on its own (a search from the place gives the same) */
    uint32_t flags = r.flags & ~(uint32_t)(RF_Y | RF_G);
    uint32_t sre = flags == r.flags ? re : js_re_compile(vm, r.source, flags);
    if (!sre) return JV_HOLE;
    if (size == 0) {
        int rc = js_re_exec(vm, sre, s, 0);
        if (rc < 0) return JV_HOLE;
        if (rc == 0 || vm->re_caps[0] != 0) js_array_push(vm, arr, jv_str(s));
        return jv_obj(arr);
    }
    uint32_t p = 0, q = 0, ng = r.ngroups;
    while (q < size) {
        int rc = js_re_exec(vm, sre, s, q);
        if (rc < 0) return JV_HOLE;
        if (rc == 0) break;
        uint32_t at = vm->re_caps[0], e = vm->re_caps[1];
        if (at >= size) break;
        if (e > size) e = size;
        /* an empty match where the last piece ended: the next place tried */
        if (e == p) { q = re_advance(vm, s, at, uni); continue; }
        vector<uint32_t> caps;
        for (uint32_t k = 0; k < 2 * ng; k++) push_back(caps, vm->re_caps[k]);
        js_array_push(vm, arr, jv_str(js_str_sub(vm, s, p, at)));
        if (vm->objs[arr].b >= lim) return jv_obj(arr);
        p = e;
        for (uint32_t g = 1; g < ng; g++) {
            if (caps[2 * g] == RE_INF || caps[2 * g + 1] == RE_INF) js_array_push(vm, arr, JV_UNDEF);
            else js_array_push(vm, arr, jv_str(js_str_sub(vm, s, caps[2 * g], caps[2 * g + 1])));
            if (vm->objs[arr].b >= lim) return jv_obj(arr);
        }
        q = p;
    }
    js_array_push(vm, arr, jv_str(js_str_sub(vm, s, p, size)));
    return jv_obj(arr);
}

/* the escaped source a RegExp's source property shows: / and line breaks escaped, (?:) for none */
static uint32_t re_source_text(js_vm *vm, uint32_t src) {
    js_str_flatten(vm, src);
    uint32_t n = vm->strs[src].len;
    if (n == 0) return js_str_ascii(vm, "(?:)");
    vector<uint16_t> u;
    int in_class = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = vm->units[vm->strs[src].off + i];
        if (c == '\\' && i + 1 < n) { push_back(u, c); push_back(u, vm->units[vm->strs[src].off + i + 1]); i++; continue; }
        if (c == '[') in_class = 1;
        else if (c == ']') in_class = 0;
        if (c == '/' && !in_class) { push_back(u, (uint16_t)'\\'); push_back(u, c); continue; }
        if (c == '\n') { push_back(u, (uint16_t)'\\'); push_back(u, (uint16_t)'n'); continue; }
        if (c == '\r') { push_back(u, (uint16_t)'\\'); push_back(u, (uint16_t)'r'); continue; }
        if (c == 0x2028) { push_back(u, (uint16_t)'\\'); push_back(u, (uint16_t)'u'); push_back(u, (uint16_t)'2'); push_back(u, (uint16_t)'0'); push_back(u, (uint16_t)'2'); push_back(u, (uint16_t)'8'); continue; }
        if (c == 0x2029) { push_back(u, (uint16_t)'\\'); push_back(u, (uint16_t)'u'); push_back(u, (uint16_t)'2'); push_back(u, (uint16_t)'0'); push_back(u, (uint16_t)'2'); push_back(u, (uint16_t)'9'); continue; }
        push_back(u, c);
    }
    return js_str_new(vm, data(u), len(u));
}

/* a RegExp object of the regex re, its prototype proto */
static jv re_object(js_vm *vm, uint32_t re, uint32_t proto) {
    uint32_t o = js_obj_new(vm, JK_REGEXP, proto);
    vm->objs[o].a = re;
    /* writable, neither enumerable nor configurable (22.2.3.1) */
    js_define(vm, o, A_LASTINDEX, jv_num(0), JS_SHAPE_HIDDEN | JS_SHAPE_FIXED);
    return jv_obj(o);
}

/* a literal's object: its regex compiled with the script */
jv js_re_object(js_vm *vm, uint32_t re) { return re_object(vm, re, vm->regexp_proto); }

/* ---- the RegExp protocol (ECMA-262 22.2.6, 22.2.7), as V8 keeps it ----
   What the methods reach through properties: an exec the receiver has (RegExpExec), the Symbol methods String's
   methods call, a subclass's species, the flags read by name. A RegExp no script has touched (re_plain: its own
   properties as made, of a RegExp.prototype and a RegExp as the engine made them) is matched by the engine's own
   matcher, its flags read from its regex, with nothing observable left out; any other receiver -- a subclass's
   instance, an object with an exec of its own, any object at all -- the generic way, each property read and each
   function called where the standard has it. */

/* fn is RegExp's native which (its exec, its Symbol.match, ...) */
static int re_native_is(js_vm *vm, jv fn, uint32_t which) {
    return jv_is_obj(fn) && vm->objs[jv_idx(fn)].kind == JK_NATIVE && vm->objs[jv_idx(fn)].a == vm->re_first + which;
}

/* the regexp protector: RegExp.prototype and RegExp as the engine made them -- their shapes, and each property the
   methods read still holding the value the engine put there (an accessor its getter): js_re_guard. n the guards to
   check, the first n (1: exec alone, what RegExpExec reads) */
static int re_pristine(js_vm *vm, uint32_t n) {
    uint32_t rp = vm->regexp_proto, rc = vm->regexp_ctor;
    if (!vm->re_guard_n || vm->objs[rp].shape != vm->re_proto_shape || vm->objs[rc].shape != vm->re_ctor_shape) return 0;
    if (n > vm->re_guard_n) n = vm->re_guard_n;
    for (uint32_t i = 0; i < n; i++) {
        jv v = vm->slots[vm->objs[vm->re_guard_obj[i]].slots + vm->re_guard_slot[i]];
        if (v != vm->re_guard_val[i]) return 0;
        if (vm->re_guard_get[i] != JV_HOLE && vm->objs[jv_idx(v)].x != vm->re_guard_get[i]) return 0;
    }
    return 1;
}

/* a RegExp the engine's matcher may serve: its own properties as made (lastIndex alone, writable), its prototype
   RegExp.prototype, the protector whole */
static int re_plain(js_vm *vm, jv rxv) {
    if (!jv_is_obj(rxv)) return 0;
    uint32_t o = jv_idx(rxv);
    return vm->objs[o].kind == JK_REGEXP && vm->objs[o].shape == vm->re_root_shape && re_pristine(vm, 20);
}

/* the protector's record, made once every built-in is: RegExp.prototype's exec (first), flags, the flags' getters,
   constructor and Symbol methods, and RegExp's Symbol.species -- each one's slot, its value and an accessor's getter --
   with the shapes of the two and of a RegExp as made. Anything not as expected leaves none, every RegExp then
   matched the generic way */
void js_re_guard(js_vm *vm) {
    static const char *const names[] = { "exec", "flags", "global", "ignoreCase", "multiline", "dotAll", "unicode",
                                         "unicodeSets", "sticky", "hasIndices", "constructor" };
    static const uint32_t syms[] = { JSYM_MATCH, JSYM_MATCH_ALL, JSYM_REPLACE, JSYM_SEARCH, JSYM_SPLIT };
    uint32_t rp = vm->regexp_proto, rc = vm->regexp_ctor, n = 0;
    vm->re_guard_n = 0;
    if (!rp || !rc) return;
    for (uint32_t i = 0; i < 17; i++) {
        uint32_t o = i < 16 ? rp : rc;
        uint32_t key = i < 11 ? js_atom_ascii(vm, names[i]) : i < 16 ? (JS_SYM_KEY | syms[i - 11]) : (JS_SYM_KEY | JSYM_SPECIES);
        uint32_t e = js_shape_find(vm, vm->objs[o].shape, key);
        if (!e) return;
        jv v = vm->slots[vm->objs[o].slots + vm->shapes[e].slot];
        if (!jv_is_obj(v)) return;
        jv get = JV_HOLE;
        if (vm->shapes[e].flags & JS_SHAPE_ACCESSOR) {
            get = vm->objs[jv_idx(v)].x;
            if (!jv_is_obj(get) || vm->objs[jv_idx(get)].kind != JK_NATIVE) return;
        } else if (vm->objs[jv_idx(v)].kind != JK_NATIVE) return;
        vm->re_guard_obj[n] = o;
        vm->re_guard_slot[n] = vm->shapes[e].slot;
        vm->re_guard_val[n] = v;
        vm->re_guard_get[n] = get;
        n++;
    }
    uint32_t re = js_re_compile(vm, A_EMPTY, 0);
    if (!re) return;
    vm->re_root_shape = vm->objs[jv_idx(re_object(vm, re, rp))].shape;
    vm->re_proto_shape = vm->objs[rp].shape;
    vm->re_ctor_shape = vm->objs[rc].shape;
    vm->re_guard_n = n;
}

/* RegExpExec (ECMA-262 22.2.7.1): R's exec called when it is callable and not the engine's (its result an object or
   null, else V8's TypeError); else R's own matcher, R a RegExp. The result, JV_NULL for none, JV_HOLE thrown */
static jv re_exec_any(js_vm *vm, jv rxv, uint32_t s) {
    uint32_t o = jv_is_obj(rxv) ? jv_idx(rxv) : 0;
    if (!o || vm->objs[o].kind != JK_REGEXP || vm->objs[o].shape != vm->re_root_shape || !re_pristine(vm, 1)) {
        jv ex = js_get(vm, rxv, js_atom_ascii(vm, "exec"));
        if (vm->throwing) return JV_HOLE;
        if (js_is_callable(vm, ex) && !re_native_is(vm, ex, JRE_EXEC)) {
            jv av[1] = { jv_str(s) };
            jv r = js_call(vm, ex, rxv, av, 1);
            if (vm->throwing) return JV_HOLE;
            if (!jv_is_obj(r) && r != JV_NULL)
                return js_throw_msg(vm, vm->type_error_proto, "RegExp exec method returned something other than an Object or null");
            return r;
        }
    }
    uint32_t re = re_this(vm, rxv, "RegExp.prototype.exec");
    if (!re) return JV_HOLE;
    int rc = re_builtin_exec(vm, rxv, re, s);
    if (rc < 0) return JV_HOLE;
    return rc ? re_result(vm, re, s) : JV_NULL;
}

/* Set(R, "lastIndex", v, true): a RegExp's own property written (V8's TypeError when it is read-only), any other
   object's set, its refusal thrown. 1, 0 thrown */
static int re_put_last(js_vm *vm, jv rxv, jv v) {
    if (jv_is_obj(rxv) && vm->objs[jv_idx(rxv)].kind == JK_REGEXP) return re_set_last(vm, rxv, v);
    return js_set_throw(vm, rxv, jv_str(A_LASTINDEX), v);
}

/* V8's fullUnicode for a global match (unicode read, then unicodeSets) or replace (unicode alone). 1, 0; -1 thrown */
static int re_get_unicode(js_vm *vm, jv rxv, int sets) {
    jv u = js_get(vm, rxv, js_atom_ascii(vm, "unicode"));
    if (vm->throwing) return -1;
    if (js_truthy(vm, u)) return 1;
    if (!sets) return 0;
    jv v = js_get(vm, rxv, js_atom_ascii(vm, "unicodeSets"));
    if (vm->throwing) return -1;
    return js_truthy(vm, v) ? 1 : 0;
}

/* whether a flags text holds the flag c */
static int re_has_flag(js_vm *vm, uint32_t fs, uint16_t c) {
    js_str_flatten(vm, fs);
    for (uint32_t i = 0; i < vm->strs[fs].len; i++) if (vm->units[vm->strs[fs].off + i] == c) return 1;
    return 0;
}

/* a global match's, replace's or matchAll's step past an empty match (22.2.6.8 6.e.iii): the match's text read (into
   *text), and when it is empty lastIndex moved on from its ToLength. 1, 0 thrown */
static int re_bump_empty(js_vm *vm, jv rxv, jv result, uint32_t s, int uni, uint32_t *text) {
    jv m0 = js_get_value(vm, result, jv_num(0));
    if (vm->throwing) return 0;
    uint32_t ms = js_to_string(vm, m0);
    if (!ms) return 0;
    if (text) *text = ms;
    if (vm->strs[ms].len) return 1;
    jv li = js_get(vm, rxv, A_LASTINDEX);
    if (vm->throwing) return 0;
    double t = re_to_length(vm, li);
    if (vm->throwing) return 0;
    js_str_flatten(vm, s);
    double nx = t + 1;
    if (uni && t + 1 < vm->strs[s].len) nx = (double)re_advance(vm, s, (uint32_t)t, 1);
    return re_put_last(vm, rxv, jv_num(nx));
}

/* RegExp.prototype[Symbol.match] (22.2.6.8) on any receiver, read as V8 reads it (global, then unicode; not flags) */
static jv re_m_match(js_vm *vm, jv rxv, uint32_t s, int plain) {
    if (plain) {
        uint32_t re = vm->objs[jv_idx(rxv)].a;
        if (!(vm->regexes[re].flags & RF_G)) {
            int rc = re_builtin_exec(vm, rxv, re, s);
            if (rc < 0) return JV_HOLE;
            return rc ? re_result(vm, re, s) : JV_NULL;
        }
        vector<uint32_t> all;
        if (!re_global_matches(vm, rxv, re, s, 2, &all)) return JV_HOLE;
        uint32_t nm = (uint32_t)(len(all) / 2);
        if (!nm) return JV_NULL;
        uint32_t arr = js_array_new(vm, nm);
        for (uint32_t m = 0; m < nm; m++) js_array_set(vm, arr, m, jv_str(js_str_sub(vm, s, all[2 * m], all[2 * m + 1])));
        return jv_obj(arr);
    }
    jv g = js_get(vm, rxv, js_atom_ascii(vm, "global"));
    if (vm->throwing) return JV_HOLE;
    if (!js_truthy(vm, g)) return re_exec_any(vm, rxv, s);
    int uni = re_get_unicode(vm, rxv, 1);
    if (uni < 0) return JV_HOLE;
    if (!re_put_last(vm, rxv, jv_num(0))) return JV_HOLE;
    uint32_t arr = 0;
    for (;;) {
        jv r = re_exec_any(vm, rxv, s);
        if (r == JV_HOLE) return JV_HOLE;
        if (r == JV_NULL) return arr ? jv_obj(arr) : JV_NULL;
        uint32_t ms = 0;
        if (!re_bump_empty(vm, rxv, r, s, uni, &ms)) return JV_HOLE;
        if (!arr) arr = js_array_new(vm, 0);
        js_array_push(vm, arr, jv_str(ms));
    }
}

/* RegExp.prototype[Symbol.search] (22.2.6.12): lastIndex set to 0 for the search, then put back, each write only when
   the value differs (a frozen RegExp searches when its lastIndex is 0 and not g or y) */
static jv re_m_search(js_vm *vm, jv rxv, uint32_t s, int plain) {
    jv prev = js_get(vm, rxv, A_LASTINDEX);
    if (vm->throwing) return JV_HOLE;
    if (!re_same(vm, prev, jv_num(0)) && !re_put_last(vm, rxv, jv_num(0))) return JV_HOLE;
    jv r = JV_NULL;
    double at = -1;
    if (plain) {
        int rc = re_builtin_exec(vm, rxv, vm->objs[jv_idx(rxv)].a, s);
        if (rc < 0) return JV_HOLE;
        if (rc) at = (double)vm->re_caps[0];
    } else {
        r = re_exec_any(vm, rxv, s);
        if (r == JV_HOLE) return JV_HOLE;
    }
    jv cur = js_get(vm, rxv, A_LASTINDEX);
    if (vm->throwing) return JV_HOLE;
    if (!re_same(vm, cur, prev) && !re_put_last(vm, rxv, prev)) return JV_HOLE;
    if (plain) return jv_num(at);
    if (r == JV_NULL) return jv_num(-1);
    return js_get(vm, r, A_INDEX);
}

/* GetSubstitution (22.1.3.19.1) for a match the generic way gave: its captures strings or undefined, its named
   captures an object or undefined. 1, 0 thrown */
static int re_substitute_any(js_vm *vm, uint32_t matched, uint32_t s, uint32_t position, const jv *caps, uint32_t ncaps,
                             jv named, uint32_t tpl, vector<uint16_t> *out) {
    js_str_flatten(vm, tpl);
    js_str_flatten(vm, s);
    js_str_flatten(vm, matched);
    uint32_t tn = vm->strs[tpl].len, sn = vm->strs[s].len;
    for (uint32_t i = 0; i < tn;) {
        uint16_t c = vm->units[vm->strs[tpl].off + i];
        if (c != '$' || i + 1 >= tn) { push_back(*out, c); i++; continue; }
        uint16_t d = vm->units[vm->strs[tpl].off + i + 1];
        if (d == '$') { push_back(*out, (uint16_t)'$'); i += 2; continue; }
        if (d == '&') { re_append(vm, matched, 0, vm->strs[matched].len, out); i += 2; continue; }
        if (d == '`') { re_append(vm, s, 0, position < sn ? position : sn, out); i += 2; continue; }
        if (d == '\'') {
            uint64_t tail = (uint64_t)position + vm->strs[matched].len;
            if (tail < sn) re_append(vm, s, (uint32_t)tail, sn, out);
            i += 2;
            continue;
        }
        if (d >= '0' && d <= '9') {
            uint16_t e = i + 2 < tn ? vm->units[vm->strs[tpl].off + i + 2] : 0;
            uint32_t digits = e >= '0' && e <= '9' ? 2 : 1;
            uint32_t index = digits == 2 ? (uint32_t)(d - '0') * 10 + (uint32_t)(e - '0') : (uint32_t)(d - '0');
            if (index > ncaps && digits == 2) { digits = 1; index = (uint32_t)(d - '0'); }
            if (index >= 1 && index <= ncaps) {
                jv cv = caps[index - 1];
                if (jv_is_str(cv)) re_append(vm, jv_idx(cv), 0, vm->strs[jv_idx(cv)].len, out);
            } else re_append(vm, tpl, i, i + 1 + digits, out);
            i += 1 + digits;
            continue;
        }
        if (d == '<') {
            uint32_t gt = i + 2;
            while (gt < tn && vm->units[vm->strs[tpl].off + gt] != '>') gt++;
            if (gt >= tn || named == JV_UNDEF) { push_back(*out, (uint16_t)'$'); push_back(*out, (uint16_t)'<'); i += 2; continue; }
            jv cap = js_get_value(vm, named, jv_str(js_str_sub(vm, tpl, i + 2, gt)));
            if (vm->throwing) return 0;
            if (cap != JV_UNDEF) {
                uint32_t cs = js_to_string(vm, cap);
                if (!cs) return 0;
                re_append(vm, cs, 0, vm->strs[cs].len, out);
            }
            i = gt + 1;
            continue;
        }
        push_back(*out, (uint16_t)'$');
        i++;
    }
    return 1;
}

/* RegExp.prototype[Symbol.replace] (22.2.6.11) on any receiver, read as V8 reads it (global, then unicode alone): every
   match RegExpExec gives, then each one's text, place, captures and groups read and its replacement made */
static jv re_m_replace(js_vm *vm, jv rxv, uint32_t s, jv rep, int plain) {
    if (plain) return re_replace(vm, rxv, vm->objs[jv_idx(rxv)].a, s, rep);
    js_str_flatten(vm, s);
    uint32_t sn = vm->strs[s].len;
    int fn = js_is_callable(vm, rep);
    uint32_t tpl = 0;
    if (!fn) { tpl = js_to_string(vm, rep); if (!tpl) return JV_HOLE; }
    jv g = js_get(vm, rxv, js_atom_ascii(vm, "global"));
    if (vm->throwing) return JV_HOLE;
    int global = js_truthy(vm, g), uni = 0;
    if (global) {
        uni = re_get_unicode(vm, rxv, 0);
        if (uni < 0) return JV_HOLE;
        if (!re_put_last(vm, rxv, jv_num(0))) return JV_HOLE;
    }
    vector<jv> results;
    for (;;) {
        jv r = re_exec_any(vm, rxv, s);
        if (r == JV_HOLE) return JV_HOLE;
        if (r == JV_NULL) break;
        push_back(results, r);
        if (!global) break;
        if (!re_bump_empty(vm, rxv, r, s, uni, 0)) return JV_HOLE;
    }
    vector<uint16_t> out;
    uint32_t next = 0;
    for (size_t k = 0; k < len(results); k++) {
        jv r = results[k];
        jv lv = js_get(vm, r, A_LENGTH);
        if (vm->throwing) return JV_HOLE;
        double rl = re_to_length(vm, lv);
        if (vm->throwing) return JV_HOLE;
        /* (a length past any regex's groups is a script's: its captures read up to a bound) */
        uint32_t ncap = rl > 1 ? (rl - 1 > 65535 ? 65535 : (uint32_t)(rl - 1)) : 0;
        jv m0 = js_get_value(vm, r, jv_num(0));
        if (vm->throwing) return JV_HOLE;
        uint32_t matched = js_to_string(vm, m0);
        if (!matched) return JV_HOLE;
        uint32_t mlen = vm->strs[matched].len;
        jv pv = js_get(vm, r, A_INDEX);
        if (vm->throwing) return JV_HOLE;
        double pd = js_to_number(vm, pv);
        if (vm->throwing) return JV_HOLE;
        pd = pd != pd ? 0 : trunc(pd);
        uint32_t pos = pd <= 0 ? 0 : pd >= sn ? sn : (uint32_t)pd;
        vector<jv> caps;
        for (uint32_t n = 1; n <= ncap; n++) {
            jv cv = js_get_value(vm, r, jv_num(n));
            if (vm->throwing) return JV_HOLE;
            if (cv != JV_UNDEF) {
                uint32_t cs = js_to_string(vm, cv);
                if (!cs) return JV_HOLE;
                cv = jv_str(cs);
            }
            push_back(caps, cv);
        }
        jv named = js_get(vm, r, js_atom_ascii(vm, "groups"));
        if (vm->throwing) return JV_HOLE;
        vector<uint16_t> piece;
        if (fn) {
            vector<jv> av;
            push_back(av, jv_str(matched));
            for (size_t c = 0; c < len(caps); c++) push_back(av, caps[c]);
            push_back(av, jv_num(pos));
            push_back(av, jv_str(s));
            if (named != JV_UNDEF) push_back(av, named);
            jv rv = js_call(vm, rep, JV_UNDEF, data(av), (uint32_t)len(av));
            if (vm->throwing) return JV_HOLE;
            uint32_t rs = js_to_string(vm, rv);
            if (!rs) return JV_HOLE;
            re_append(vm, rs, 0, vm->strs[rs].len, &piece);
        } else {
            if (named != JV_UNDEF) {
                named = js_to_object(vm, named);
                if (vm->throwing) return JV_HOLE;
            }
            if (!re_substitute_any(vm, matched, s, pos, data(caps), (uint32_t)len(caps), named, tpl, &piece)) return JV_HOLE;
        }
        if (pos >= next) {
            re_append(vm, s, next, pos, &out);
            if (len(piece)) append_range(out, data(piece), len(piece));
            next = pos + mlen;
        }
    }
    if (next < sn) re_append(vm, s, next, sn, &out);
    return jv_str(js_str_new(vm, data(out), len(out)));
}

/* SpeciesConstructor(rx, %RegExp%) (7.3.22): the constructor split and matchAll make their matcher with. JV_HOLE thrown */
static jv re_species_ctor(js_vm *vm, jv rxv) {
    jv c = js_get(vm, rxv, A_CONSTRUCTOR);
    if (vm->throwing) return JV_HOLE;
    if (c == JV_UNDEF) return jv_obj(vm->regexp_ctor);
    if (!jv_is_obj(c)) return js_throw_msg(vm, vm->type_error_proto, "The .constructor property is not an object");
    jv sp = js_get(vm, c, JS_SYM_KEY | JSYM_SPECIES);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_nullish(sp)) return jv_obj(vm->regexp_ctor);
    if (!js_is_constructor(vm, sp)) return js_throw_msg(vm, vm->type_error_proto, "object.constructor[Symbol.species] is not a constructor");
    return sp;
}

/* RegExp.prototype[Symbol.split] (22.2.6.14): the splitter the species makes of rx and its flags with y, each place
   tried by RegExpExec; a plain RegExp split by its own regex */
static jv re_m_split(js_vm *vm, jv rxv, uint32_t s, jv limit, int plain) {
    if (plain) return re_split(vm, vm->objs[jv_idx(rxv)].a, s, limit);
    jv c = re_species_ctor(vm, rxv);
    if (c == JV_HOLE) return JV_HOLE;
    jv fl = js_get(vm, rxv, js_atom_ascii(vm, "flags"));
    if (vm->throwing) return JV_HOLE;
    uint32_t fs = js_to_string(vm, fl);
    if (!fs) return JV_HOLE;
    int uni = re_has_flag(vm, fs, 'u') || re_has_flag(vm, fs, 'v');
    uint32_t nf = re_has_flag(vm, fs, 'y') ? fs : js_str_concat(vm, fs, js_str_ascii(vm, "y"));
    jv av[2] = { rxv, jv_str(nf) };
    jv sp = js_construct(vm, c, av, 2, c);
    if (vm->throwing) return JV_HOLE;
    uint32_t lim = 0xFFFFFFFFu;
    if (limit != JV_UNDEF) {
        double d = js_to_number(vm, limit);
        if (vm->throwing) return JV_HOLE;
        lim = (uint32_t)js_to_int32(d);
    }
    uint32_t arr = js_array_new(vm, 0);
    if (lim == 0) return jv_obj(arr);
    js_str_flatten(vm, s);
    uint32_t size = vm->strs[s].len;
    if (size == 0) {
        jv z = re_exec_any(vm, sp, s);
        if (z == JV_HOLE) return JV_HOLE;
        if (z == JV_NULL) js_array_push(vm, arr, jv_str(s));
        return jv_obj(arr);
    }
    uint32_t p = 0, q = 0;
    while (q < size) {
        if (!re_put_last(vm, sp, jv_num(q))) return JV_HOLE;
        jv z = re_exec_any(vm, sp, s);
        if (z == JV_HOLE) return JV_HOLE;
        if (z == JV_NULL) { q = re_advance(vm, s, q, uni); continue; }
        jv ev = js_get(vm, sp, A_LASTINDEX);
        if (vm->throwing) return JV_HOLE;
        double ed = re_to_length(vm, ev);
        if (vm->throwing) return JV_HOLE;
        uint32_t e = ed > size ? size : (uint32_t)ed;
        if (e == p) { q = re_advance(vm, s, q, uni); continue; }
        js_array_push(vm, arr, jv_str(js_str_sub(vm, s, p, q)));
        if (vm->objs[arr].b == lim) return jv_obj(arr);
        p = e;
        jv nl = js_get(vm, z, A_LENGTH);
        if (vm->throwing) return JV_HOLE;
        double nc = re_to_length(vm, nl);
        if (vm->throwing) return JV_HOLE;
        for (double i = 1; i < nc && i <= 65535; i++) {
            jv cap = js_get_value(vm, z, jv_num(i));
            if (vm->throwing) return JV_HOLE;
            js_array_push(vm, arr, cap);
            if (vm->objs[arr].b == lim) return jv_obj(arr);
        }
        q = p;
    }
    js_array_push(vm, arr, jv_str(js_str_sub(vm, s, p, size)));
    return jv_obj(arr);
}

/* RegExp.prototype[Symbol.matchAll] (22.2.6.9): the matcher (a plain RegExp's copy; else what the species makes of rx
   and its flags), lastIndex carried over, and the iterator over its matches, found as it is stepped (js_re_iter_step):
   a its matcher, x the string, b its flags (1 g, 2 u or v) and 4 once done */
static jv re_m_matchall(js_vm *vm, jv rxv, uint32_t s, int plain) {
    jv m;
    uint32_t bits = 0;
    if (plain) {
        uint32_t re = vm->objs[jv_idx(rxv)].a, f = vm->regexes[re].flags;
        jv li = js_get(vm, rxv, A_LASTINDEX);
        if (vm->throwing) return JV_HOLE;
        double l = re_to_length(vm, li);
        if (vm->throwing) return JV_HOLE;
        m = re_object(vm, re, vm->regexp_proto);
        js_set(vm, m, A_LASTINDEX, jv_num(l));
        bits = ((f & RF_G) ? 1u : 0u) | ((f & (RF_U | RF_V)) ? 2u : 0u);
    } else {
        jv c = re_species_ctor(vm, rxv);
        if (c == JV_HOLE) return JV_HOLE;
        jv fl = js_get(vm, rxv, js_atom_ascii(vm, "flags"));
        if (vm->throwing) return JV_HOLE;
        uint32_t fs = js_to_string(vm, fl);
        if (!fs) return JV_HOLE;
        jv av[2] = { rxv, jv_str(fs) };
        m = js_construct(vm, c, av, 2, c);
        if (vm->throwing) return JV_HOLE;
        jv li = js_get(vm, rxv, A_LASTINDEX);
        if (vm->throwing) return JV_HOLE;
        double l = re_to_length(vm, li);
        if (vm->throwing) return JV_HOLE;
        if (!re_put_last(vm, m, jv_num(l))) return JV_HOLE;
        bits = (re_has_flag(vm, fs, 'g') ? 1u : 0u) | (re_has_flag(vm, fs, 'u') || re_has_flag(vm, fs, 'v') ? 2u : 0u);
    }
    uint32_t it = js_obj_new(vm, JK_ITER, vm->regexp_iter_proto);
    vm->objs[it].a = jv_idx(m);
    vm->objs[it].x = jv_str(s);
    vm->objs[it].b = bits;
    vm->objs[it].c = JI_REGEXP;
    return jv_obj(it);
}

/* %RegExpStringIteratorPrototype%.next's step (22.2.9.2.1): the matcher's next match by RegExpExec; with g, past an
   empty one lastIndex moved on; without, done after the first. 1 a match in *out; 0 done or thrown (a throw leaves it
   to be stepped again, as V8's does) */
int js_re_iter_step(js_vm *vm, uint32_t it, jv *out) {
    uint32_t bits = vm->objs[it].b;
    if (bits & 4) return 0;
    jv m = jv_obj(vm->objs[it].a);
    uint32_t s = jv_idx(vm->objs[it].x);
    jv r = re_exec_any(vm, m, s);
    if (r == JV_HOLE) return 0;
    if (r == JV_NULL) { vm->objs[it].b = bits | 4; return 0; }
    if (!(bits & 1)) vm->objs[it].b = bits | 4;
    else if (!re_bump_empty(vm, m, r, s, (bits & 2) != 0, 0)) return 0;
    *out = r;
    return 1;
}

/* IsRegExp (7.2.8): an object whose Symbol.match is truthy, or, that undefined, a RegExp. 1, 0; -1 thrown */
int js_is_regexp(js_vm *vm, jv v) {
    if (!jv_is_obj(v)) return 0;
    jv m = js_get(vm, v, JS_SYM_KEY | JSYM_MATCH);
    if (vm->throwing) return -1;
    if (m != JV_UNDEF) return js_truthy(vm, m) ? 1 : 0;
    return vm->objs[jv_idx(v)].kind == JK_REGEXP;
}

/* GetMethod(v, Symbol[sym]) (7.3.11) as String's methods ask it: undefined for none; one not callable a TypeError in
   the words V8's method uses (prop: "'5' returned for property 'Symbol(Symbol.match)' of object '#<Object>' is not a
   function"; else "number 5 is not a function"). JV_HOLE thrown */
static jv re_get_method(js_vm *vm, jv v, uint32_t sym, int prop) {
    jv f = js_get(vm, v, JS_SYM_KEY | sym);
    if (vm->throwing) return JV_HOLE;
    if (jv_is_nullish(f)) return JV_UNDEF;
    if (js_is_callable(vm, f)) return f;
    if (!prop) return js_not_function(vm, f);
    const char *nm = sym == JSYM_MATCH ? "Symbol.match" : sym == JSYM_MATCH_ALL ? "Symbol.matchAll" :
                     sym == JSYM_REPLACE ? "Symbol.replace" : sym == JSYM_SEARCH ? "Symbol.search" : "Symbol.split";
    const char *mid = "' returned for property 'Symbol(", *mid2 = ")' of object '", *end = "' is not a function";
    vector<char> m;
    push_back(m, '\'');
    js_value_text(vm, f, &m);
    append_range(m, mid, strlen(mid));
    append_range(m, nm, strlen(nm));
    append_range(m, mid2, strlen(mid2));
    js_value_text(vm, v, &m);
    append_range(m, end, strlen(end));
    push_back(m, 0);
    return js_throw_msg(vm, vm->type_error_proto, data(m));
}

/* RegExpCreate(P, F) (22.2.3.1): a RegExp of P's text (undefined: none) and the flags. JV_HOLE thrown */
static jv re_create(js_vm *vm, jv p, uint32_t flags) {
    uint32_t src = p == JV_UNDEF ? A_EMPTY : js_to_string(vm, p);
    if (!src) return JV_HOLE;
    uint32_t re = js_re_compile(vm, src, flags);
    if (!re) return JV_HOLE;
    return re_object(vm, re, vm->regexp_proto);
}

/* RegExp.prototype's Symbol method which (JRE_MATCH, JRE_SEARCH, JRE_MATCHALL) on rx and s, plain re_plain's answer */
static jv re_method(js_vm *vm, uint32_t which, jv rxv, uint32_t s, int plain) {
    if (which == JRE_MATCH) return re_m_match(vm, rxv, s, plain);
    if (which == JRE_SEARCH) return re_m_search(vm, rxv, s, plain);
    return re_m_matchall(vm, rxv, s, plain);
}

/* Invoke(rx, Symbol[sym], s) for a RegExp RegExpCreate made: the engine's method while the protector holds */
static jv re_invoke(js_vm *vm, jv rx, uint32_t sym, uint32_t which, uint32_t s) {
    if (re_plain(vm, rx)) return re_method(vm, which, rx, s, 1);
    jv f = js_get(vm, rx, JS_SYM_KEY | sym);
    if (vm->throwing) return JV_HOLE;
    if (!js_is_callable(vm, f)) return js_not_function(vm, f);
    jv av[1] = { jv_str(s) };
    return js_call(vm, f, rx, av, 1);
}

/* String.prototype.replace, replaceAll and split with a pattern of any kind (22.1.3.19, .20, .23): the pattern's
   Symbol.replace (Symbol.split) called when it has one, replaceAll's RegExp refused unless global, a plain RegExp's
   served by the engine. 1 done (*out), 0 none (the string's own way then), -1 thrown */
int js_re_string_method(js_vm *vm, uint32_t sym, int all, jv thisv, jv a0, jv a1, jv *out) {
    if (jv_is_nullish(a0)) return 0;
    int plain = re_plain(vm, a0);
    if (all) {
        int g = 1;
        if (plain) g = (vm->regexes[vm->objs[jv_idx(a0)].a].flags & RF_G) != 0;
        else {
            int isre = js_is_regexp(vm, a0);
            if (isre < 0) return -1;
            if (isre) {
                jv fl = js_get(vm, a0, js_atom_ascii(vm, "flags"));
                if (vm->throwing) return -1;
                if (jv_is_nullish(fl)) { js_throw_msg(vm, vm->type_error_proto, "String.prototype.replaceAll called on null or undefined"); return -1; }
                uint32_t fs = js_to_string(vm, fl);
                if (!fs) return -1;
                g = re_has_flag(vm, fs, 'g');
            }
        }
        if (!g) { js_throw_msg(vm, vm->type_error_proto, "String.prototype.replaceAll called with a non-global RegExp argument"); return -1; }
    }
    if (plain) {
        uint32_t s = js_to_string(vm, thisv);
        if (!s) return -1;
        *out = sym == JSYM_SPLIT ? re_m_split(vm, a0, s, a1, 1) : re_m_replace(vm, a0, s, a1, 1);
        return *out == JV_HOLE ? -1 : 1;
    }
    jv f = re_get_method(vm, a0, sym, all);
    if (f == JV_HOLE) return -1;
    if (f == JV_UNDEF) return 0;
    jv av[2] = { thisv, a1 };
    *out = js_call(vm, f, a0, av, 2);
    return vm->throwing ? -1 : 1;
}

jv js_re_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    (void)callee;
    jv a0 = re_arg(vm, args, argc, 0), a1 = re_arg(vm, args, argc, 1);
    switch (which) {
    case JRE_CTOR: {
        /* the pattern a RegExp (its own source and flags), else one IsRegExp says is (an object with a truthy
           Symbol.match: its source and flags read), else text; RegExp(p) without new p itself when p is one whose
           constructor is RegExp (22.2.4.1) */
        int is_re = jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_REGEXP;
        int like = js_is_regexp(vm, a0);
        if (like < 0) return JV_HOLE;
        if ((newt == JV_UNDEF || newt == JV_HOLE) && like && a1 == JV_UNDEF) {
            jv c = js_get(vm, a0, A_CONSTRUCTOR);
            if (vm->throwing) return JV_HOLE;
            if (c == callee) return a0;
        }
        uint32_t src, flags;
        if (is_re) {
            js_regex r = vm->regexes[vm->objs[jv_idx(a0)].a];
            src = r.source;
            flags = r.flags;
        } else if (like) {
            jv sv = js_get(vm, a0, js_atom_ascii(vm, "source"));
            if (vm->throwing) return JV_HOLE;
            if (a1 == JV_UNDEF) {
                a1 = js_get(vm, a0, js_atom_ascii(vm, "flags"));
                if (vm->throwing) return JV_HOLE;
            }
            src = sv == JV_UNDEF ? A_EMPTY : js_to_string(vm, sv);
            if (!src) return JV_HOLE;
            flags = 0;
        } else {
            src = a0 == JV_UNDEF ? A_EMPTY : js_to_string(vm, a0);
            if (!src) return JV_HOLE;
            flags = 0;
        }
        if (a1 != JV_UNDEF || !is_re) {
            uint32_t fs = a1 == JV_UNDEF ? A_EMPTY : js_to_string(vm, a1);
            if (!fs) return JV_HOLE;
            flags = re_flags_of(vm, fs);
            if (flags == RE_INF) {
                vector<char> m;
                const char *pre = "Invalid flags supplied to RegExp constructor '";
                append_range(m, pre, strlen(pre));
                js_str_to_utf8(vm, fs, &m);
                push_back(m, '\'');
                push_back(m, 0);
                return js_throw_msg(vm, vm->syntax_error_proto, data(m));
            }
        }
        uint32_t re = js_re_compile(vm, src, flags);
        if (!re) return JV_HOLE;
        uint32_t proto = vm->regexp_proto;
        if (newt != JV_UNDEF && newt != JV_HOLE && jv_is_obj(newt)) {
            jv pv = js_get(vm, newt, A_PROTOTYPE);
            if (vm->throwing) return JV_HOLE;
            if (jv_is_obj(pv)) proto = jv_idx(pv);
        }
        return re_object(vm, re, proto);
    }
    case JRE_COMPILE: {
        /* Annex B: the RegExp made again in place from a pattern (a RegExp's own source and flags) and flags */
        if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_REGEXP) { re_this(vm, thisv, "RegExp.prototype.compile"); return JV_HOLE; }
        int is_re = jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_REGEXP;
        uint32_t src, flags = 0;
        if (is_re) {
            if (a1 != JV_UNDEF) return js_throw_msg(vm, vm->type_error_proto, "Cannot supply flags when constructing one RegExp from another");
            js_regex r = vm->regexes[vm->objs[jv_idx(a0)].a];
            src = r.source;
            flags = r.flags;
        } else {
            src = a0 == JV_UNDEF ? A_EMPTY : js_to_string(vm, a0);
            if (!src) return JV_HOLE;
            uint32_t fs = a1 == JV_UNDEF ? A_EMPTY : js_to_string(vm, a1);
            if (!fs) return JV_HOLE;
            flags = re_flags_of(vm, fs);
            if (flags == RE_INF) {
                vector<char> m;
                const char *pre = "Invalid flags supplied to RegExp constructor '";
                append_range(m, pre, strlen(pre));
                js_str_to_utf8(vm, fs, &m);
                push_back(m, '\'');
                push_back(m, 0);
                return js_throw_msg(vm, vm->syntax_error_proto, data(m));
            }
        }
        uint32_t re = js_re_compile(vm, src, flags);
        if (!re) return JV_HOLE;
        vm->objs[jv_idx(thisv)].a = re;
        js_set(vm, thisv, A_LASTINDEX, jv_num(0));
        return vm->throwing ? JV_HOLE : thisv;
    }
    case JRE_EXEC: {
        uint32_t re = re_this(vm, thisv, "RegExp.prototype.exec");
        if (!re) return JV_HOLE;
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        int rc = re_builtin_exec(vm, thisv, re, s);
        if (rc < 0) return JV_HOLE;
        return rc ? re_result(vm, re, s) : JV_NULL;
    }
    case JRE_TEST: {
        /* RegExpExec's answer, any exec's (an object that is no RegExp, without one, fails in the matcher it falls back
           to, as V8's does) */
        if (!jv_is_obj(thisv)) { re_this(vm, thisv, "RegExp.prototype.test"); return JV_HOLE; }
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        jv r = re_exec_any(vm, thisv, s);
        if (r == JV_HOLE) return JV_HOLE;
        return jv_bool(r != JV_NULL);
    }
    case JRE_TOSTRING: {
        if (!jv_is_obj(thisv)) { re_this(vm, thisv, "RegExp.prototype.toString"); return JV_HOLE; }
        jv src = js_get(vm, thisv, js_atom_ascii(vm, "source"));
        if (vm->throwing) return JV_HOLE;
        jv fl = js_get(vm, thisv, js_atom_ascii(vm, "flags"));
        if (vm->throwing) return JV_HOLE;
        uint32_t ss = js_to_string(vm, src);
        if (!ss) return JV_HOLE;
        uint32_t fs = js_to_string(vm, fl);
        if (!fs) return JV_HOLE;
        uint32_t sl = js_str_ascii(vm, "/");
        return jv_str(js_str_concat(vm, js_str_concat(vm, js_str_concat(vm, sl, ss), sl), fs));
    }
    case JRE_SOURCE: {
        if (jv_is_obj(thisv) && jv_idx(thisv) == vm->regexp_proto) return jv_str(js_str_ascii(vm, "(?:)"));
        if (!jv_is_obj(thisv)) return re_not_object(vm, "RegExp.prototype.source getter", thisv);
        if (vm->objs[jv_idx(thisv)].kind != JK_REGEXP) return js_throw_msg(vm, vm->type_error_proto, "RegExp.prototype.source getter called on non-RegExp object");
        uint32_t re = vm->objs[jv_idx(thisv)].a;
        return jv_str(re_source_text(vm, vm->regexes[re].source));
    }
    case JRE_FLAGS: {
        /* each flag's property read, in V8's order (sticky before unicodeSets), and written in the standard's */
        if (!jv_is_obj(thisv)) return re_not_object(vm, "RegExp.prototype.flags getter", thisv);
        static const char *const fnames[] = { "hasIndices", "global", "ignoreCase", "multiline", "dotAll", "unicode", "sticky", "unicodeSets" };
        static const uint32_t fbits[] = { RF_D, RF_G, RF_I, RF_M, RF_S, RF_U, RF_Y, RF_V };
        uint32_t f = 0;
        for (uint32_t i = 0; i < 8; i++) {
            jv v = js_get(vm, thisv, js_atom_ascii(vm, fnames[i]));
            if (vm->throwing) return JV_HOLE;
            if (js_truthy(vm, v)) f |= fbits[i];
        }
        char b[12];
        re_flags_text(f, b);
        return jv_str(js_str_ascii(vm, b));
    }
    case JRE_GLOBAL: case JRE_IGNORECASE: case JRE_MULTILINE: case JRE_DOTALL: case JRE_UNICODE: case JRE_UNICODESETS:
    case JRE_STICKY: case JRE_HASINDICES: {
        static const char *const gnames[] = { "global", "ignoreCase", "multiline", "dotAll", "unicode", "unicodeSets", "sticky", "hasIndices" };
        if (jv_is_obj(thisv) && jv_idx(thisv) == vm->regexp_proto) return JV_UNDEF;
        char what[64];
        snprintf(what, sizeof what, "RegExp.prototype.%s getter", gnames[which - JRE_GLOBAL]);
        if (!jv_is_obj(thisv)) return re_not_object(vm, what, thisv);
        if (vm->objs[jv_idx(thisv)].kind != JK_REGEXP) {
            char m[128];
            snprintf(m, sizeof m, "%s called on non-RegExp object", what);
            return js_throw_msg(vm, vm->type_error_proto, m);
        }
        static const uint32_t bit[] = { RF_G, RF_I, RF_M, RF_S, RF_U, RF_V, RF_Y, RF_D };
        return jv_bool((vm->regexes[vm->objs[jv_idx(thisv)].a].flags & bit[which - JRE_GLOBAL]) != 0);
    }
    case JRE_MATCH: case JRE_SEARCH: case JRE_MATCHALL: case JRE_REPLACE: case JRE_SPLIT: {
        /* RegExp.prototype's Symbol methods, any object their receiver */
        if (!jv_is_obj(thisv)) {
            re_this(vm, thisv, which == JRE_MATCH ? "RegExp.prototype.@@match" : which == JRE_SEARCH ? "RegExp.prototype.@@search" :
                               which == JRE_MATCHALL ? "RegExp.prototype.@@matchAll" : which == JRE_REPLACE ? "RegExp.prototype.@@replace" :
                               "RegExp.prototype.@@split");
            return JV_HOLE;
        }
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        int plain = re_plain(vm, thisv);
        if (which == JRE_REPLACE) return re_m_replace(vm, thisv, s, a1, plain);
        if (which == JRE_SPLIT) return re_m_split(vm, thisv, s, a1, plain);
        return re_method(vm, which, thisv, s, plain);
    }
    case JRE_SP_MATCH: case JRE_SP_SEARCH: case JRE_SP_MATCHALL: {
        /* String.prototype.match, search and matchAll (22.1.3.13, .14, .22): the pattern's own Symbol method when it has
           one (matchAll's RegExp refused unless global), else a RegExp made of the pattern and its method */
        if (jv_is_nullish(thisv) || thisv == JV_HOLE)
            return js_throw_msg(vm, vm->type_error_proto, which == JRE_SP_MATCH ? "String.prototype.match called on null or undefined" :
                                which == JRE_SP_SEARCH ? "String.prototype.search called on null or undefined" :
                                "String.prototype.matchAll called on null or undefined");
        uint32_t sym = which == JRE_SP_MATCH ? JSYM_MATCH : which == JRE_SP_SEARCH ? JSYM_SEARCH : JSYM_MATCH_ALL;
        uint32_t mw = which == JRE_SP_MATCH ? JRE_MATCH : which == JRE_SP_SEARCH ? JRE_SEARCH : JRE_MATCHALL;
        if (!jv_is_nullish(a0)) {
            int plain = re_plain(vm, a0);
            if (which == JRE_SP_MATCHALL) {
                int g = 1;
                if (plain) g = (vm->regexes[vm->objs[jv_idx(a0)].a].flags & RF_G) != 0;
                else {
                    int isre = js_is_regexp(vm, a0);
                    if (isre < 0) return JV_HOLE;
                    if (isre) {
                        jv fl = js_get(vm, a0, js_atom_ascii(vm, "flags"));
                        if (vm->throwing) return JV_HOLE;
                        if (jv_is_nullish(fl))
                            return js_throw_msg(vm, vm->type_error_proto, "The .flags property of the argument to String.prototype.matchAll cannot be null or undefined");
                        uint32_t fs = js_to_string(vm, fl);
                        if (!fs) return JV_HOLE;
                        g = re_has_flag(vm, fs, 'g');
                    }
                }
                if (!g) return js_throw_msg(vm, vm->type_error_proto, "String.prototype.matchAll called with a non-global RegExp argument");
            }
            if (plain) {
                uint32_t s = js_to_string(vm, thisv);
                if (!s) return JV_HOLE;
                return re_method(vm, mw, a0, s, 1);
            }
            jv f = re_get_method(vm, a0, sym, which != JRE_SP_MATCHALL);
            if (f == JV_HOLE) return JV_HOLE;
            if (f != JV_UNDEF) {
                jv av[1] = { thisv };
                return js_call(vm, f, a0, av, 1);
            }
        }
        uint32_t s = js_to_string(vm, thisv);
        if (!s) return JV_HOLE;
        jv rx = re_create(vm, a0, which == JRE_SP_MATCHALL ? RF_G : 0);
        if (rx == JV_HOLE) return JV_HOLE;
        return re_invoke(vm, rx, sym, mw, s);
    }
    default: return JV_UNDEF;
    }
}

/* the RegExp constructor and its prototype's methods and getters; first, the native id of JRE_CTOR */
void js_re_init(js_vm *vm, uint32_t first) {
    uint32_t rp = vm->regexp_proto;
    uint32_t c = js_native_new(vm, first + JRE_CTOR, js_atom_ascii(vm, "RegExp"), 2);
    vm->re_first = first;
    vm->regexp_ctor = c;
    js_define(vm, c, A_PROTOTYPE, jv_obj(rp), JS_SHAPE_PROTO);
    js_define(vm, rp, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, js_atom_ascii(vm, "RegExp"), jv_obj(c), JS_SHAPE_HIDDEN);
    static const char *const fns[] = { "exec", "test", "toString" };
    static const uint32_t fids[] = { JRE_EXEC, JRE_TEST, JRE_TOSTRING };
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t at = js_atom_ascii(vm, fns[i]);
        js_define(vm, rp, at, jv_obj(js_native_new(vm, first + fids[i], at, i == 2 ? 0 : 1)), JS_SHAPE_HIDDEN);
    }
    {
        uint32_t ca = js_atom_ascii(vm, "compile");
        js_define(vm, rp, ca, jv_obj(js_native_new(vm, first + JRE_COMPILE, ca, 2)), JS_SHAPE_HIDDEN);
    }
    static const char *const gets[] = { "source", "flags", "global", "ignoreCase", "multiline", "dotAll", "unicode", "unicodeSets", "sticky", "hasIndices" };
    static const uint32_t gids[] = { JRE_SOURCE, JRE_FLAGS, JRE_GLOBAL, JRE_IGNORECASE, JRE_MULTILINE, JRE_DOTALL, JRE_UNICODE, JRE_UNICODESETS, JRE_STICKY, JRE_HASINDICES };
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t at = js_atom_ascii(vm, gets[i]);
        js_define_accessor(vm, rp, at, jv_obj(js_native_new(vm, first + gids[i], at, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    }
    static const uint32_t syms[] = { JSYM_MATCH, JSYM_MATCH_ALL, JSYM_REPLACE, JSYM_SEARCH, JSYM_SPLIT };
    static const uint32_t sids[] = { JRE_MATCH, JRE_MATCHALL, JRE_REPLACE, JRE_SEARCH, JRE_SPLIT };
    /* named as V8 names them: "[Symbol.match]" */
    static const char *const snames[] = { "[Symbol.match]", "[Symbol.matchAll]", "[Symbol.replace]", "[Symbol.search]", "[Symbol.split]" };
    for (uint32_t i = 0; i < 5; i++)
        js_define(vm, rp, JS_SYM_KEY | syms[i], jv_obj(js_native_new(vm, first + sids[i], js_atom_ascii(vm, snames[i]), i == 2 || i == 4 ? 2 : 1)), JS_SHAPE_HIDDEN);
}

/* HTML's pattern attribute (4.10.5.3.6): whether the value v[0..vn) matches the pattern
   p[0..pn), compiled as ^(?:p)$ with the v flag: 1 it does, 0 it does not, -1 the pattern
   is no regular expression (it then constrains nothing). Both are UTF-8. The machine the
   pattern is compiled in is made when first asked and kept: a page's patterns are few,
   and one compiled once is found again */
static js_vm re_pattern_vm;
static int re_pattern_ready;
int js_pattern_test(const char *p, size_t pn, const char *v, size_t vn) {
    js_vm *vm = &re_pattern_vm;
    if (!re_pattern_ready) { js_init(vm); re_pattern_ready = 1; }
    vector<char> src;
    append_range(src, "^(?:", 4);
    append_range(src, p, pn);
    append_range(src, ")$", 2);
    uint32_t why = 0;
    uint32_t re = re_compile(vm, js_str_utf8(vm, data(src), len(src)), RF_V, &why);
    if (!re) return -1;
    int r = js_re_exec(vm, re, js_str_utf8(vm, v, vn), 0);
    return r != 0;   /* the step budget spent (-1) counts as a match: no constraint is proved broken */
}
