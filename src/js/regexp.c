/* src/js/regexp.c -- regular expressions (ECMA-262 22.2), and Unicode's case conversions.

   A pattern is parsed to a tree, then compiled to a program of 32-bit words for a
   backtracking matcher. The matcher keeps its choices and the undo of its captures on
   a stack of its own (vm->re_stack), never on the C stack, and counts its steps into
   the script's budget: a pattern that backtracks without end stops its script as an
   endless loop would. A program is compiled once for its source and flags.

   The syntax is ES2024's with Annex B's for patterns without u: alternatives, classes
   with ranges and escapes, \p{...} with u (the general categories, the scripts and
   their extensions, the binary properties: src/js/unicode.h), greedy and lazy
   quantifiers, groups (capturing, named, not capturing), back references by number
   and name, lookahead and lookbehind (matched backward), and the flags d g i m s u v
   y. Case-insensitive matching compares the characters' case orbits (the characters
   whose canonical forms are the same: simple case folding with u, the uppercase of
   code units without). Not read: v's set operations and nested classes, and the
   modifiers (?i:...): each a SyntaxError that names it.

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
       RN_REPEAT, RN_BACKREF, RN_NAMEDREF, RN_LOOK };
/* CHAR a; CLASS a b (its pairs in ranges), c negated; SEQ and ALT a b (a list); GROUP a its
   child, b its group (0: none); REPEAT a its child, b min, c max, d greedy, e f the groups
   within [e, f); BACKREF a; NAMEDREF a its name (in names); LOOK a its child, b its kind
   (0 ahead, 1 not ahead, 2 behind, 3 not behind) */
typedef struct re_node { uint32_t kind, a, b, c, d, e, f; } re_node;

#define RE_INF 0xFFFFFFFFu
#define RE_DEPTH_MAX 256u

/* the parser's errors, V8's words */
enum { RE_OK, RE_E_UNTERMINATED_GROUP, RE_E_UNMATCHED_PAREN, RE_E_NOTHING_TO_REPEAT, RE_E_LONE_QUANT, RE_E_OUT_OF_ORDER,
       RE_E_UNTERMINATED_CLASS, RE_E_RANGE_ORDER, RE_E_ESCAPE, RE_E_UNICODE_ESCAPE, RE_E_TRAILING_BACKSLASH, RE_E_GROUP,
       RE_E_GROUP_NAME, RE_E_DUP_NAME, RE_E_NAMED_REF, RE_E_PROPERTY, RE_E_CLASS_ESCAPE, RE_E_TOO_DEEP, RE_E_DECIMAL,
       RE_E_SET_OPS, RE_E_MODIFIERS, RE_E_INCOMPLETE_QUANT, RE_E_TOO_LARGE, RE_E_N };
static const char *const re_errors[RE_E_N] = {
    "", "Unterminated group", "Unmatched ')'", "Nothing to repeat", "Lone quantifier brackets",
    "numbers out of order in {} quantifier", "Unterminated character class", "Range out of order in character class",
    "Invalid escape", "Invalid Unicode escape", "\\ at end of pattern", "Invalid group", "Invalid capture group name",
    "Duplicate capture group name", "Invalid named capture referenced", "Invalid property name",
    "Invalid class escape", "the pattern is nested too deeply", "Invalid decimal escape",
    "class set operations and nested classes (the v flag's) are not supported",
    "modifiers (?i:...) are not supported", "Incomplete quantifier", "the regular expression is too large" };

typedef struct re_parser {
    vector<uint32_t> pat;         /* the pattern's characters: code points with u, code units without */
    vector<re_node> nodes;
    vector<uint32_t> lists;
    vector<uint32_t> tmp;
    vector<uint32_t> ranges;      /* the classes' [lo, hi] pairs */
    vector<uint32_t> cls;         /* a class being read: its pairs */
    vector<uint32_t> names;       /* each named group: its group, its name's first character in namech, its length */
    vector<uint32_t> namech;
    uint32_t pos, ngroups, flags, depth, total, has_names;
    uint32_t err;
} re_parser;
#define RE_PARSER_DROP(p) vector_uint32_t_drop(&(p).pat); vector_re_node_drop(&(p).nodes); vector_uint32_t_drop(&(p).lists); \
    vector_uint32_t_drop(&(p).tmp); vector_uint32_t_drop(&(p).ranges); vector_uint32_t_drop(&(p).cls); \
    vector_uint32_t_drop(&(p).names); vector_uint32_t_drop(&(p).namech)

static uint32_t rn_new(re_parser *p, uint32_t kind) {
    re_node n;
    memset(&n, 0, sizeof n);
    n.kind = kind;
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
    else if ((p->flags & RF_I) && rp_unicode(p)) cls_add_ranges(p, re_words_iu, 0, 6, neg, top);
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

/* a \p{...}'s text (the characters between the braces) into the class: 0 when it names no property */
static int cls_property(re_parser *p, uint32_t from, uint32_t to, int negate) {
    char name[96], value[96], want[96];
    uint32_t nn = 0, nv = 0;
    int eq = 0;
    for (uint32_t i = from; i < to; i++) {
        uint32_t c = p->pat[i];
        if (c > 127) return 0;
        if (c == '=') { if (eq) return 0; eq = 1; continue; }
        if (!eq) { if (nn + 1 >= sizeof name) return 0; name[nn++] = (char)c; }
        else { if (nv + 1 >= sizeof value) return 0; value[nv++] = (char)c; }
    }
    name[nn] = 0;
    value[nv] = 0;
    int kind = -1;
    memcpy(want, name, sizeof want);
    if (eq) {
        if (!strcmp(name, "General_Category") || !strcmp(name, "gc")) kind = 0;
        else if (!strcmp(name, "Script") || !strcmp(name, "sc")) kind = 1;
        else if (!strcmp(name, "Script_Extensions") || !strcmp(name, "scx")) kind = 2;
        else return 0;
        memcpy(want, value, sizeof want);
    }
    size_t wl = strlen(want);
    if (wl == 0) return 0;
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
            if (same) {
                cls_add_ranges(p, uni_ranges, uni_props[i].off, uni_props[i].n, negate, 0x10FFFF);
                return 1;
            }
            if (!uni_props[i].names[k + l]) break;
            k += l + 1;
        }
    }
    return 0;
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
        if (rp_unicode(p)) { re_fail(p, RE_E_ESCAPE); return -1; }
        p->pos--;
        return '\\';    /* \c read as a backslash, then c */
    }
    case '0':
        if (!re_digit(rp_at(p, 0))) return 0;
        if (rp_unicode(p)) { re_fail(p, RE_E_DECIMAL); return -1; }
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
        /* with u, only the syntax characters are escaped as themselves */
        if (re_syntax_char(c) || (c == '-' && in_class)) return c;
        re_fail(p, RE_E_ESCAPE);
        return -1;
    }
    /* without u, any character but a letter of a class escape is itself (\k without named groups too) */
    return c;
}

/* \p{...} or \P{...} after the backslash and the p: into the class; 0 with the error set */
static int rp_property(re_parser *p, int negate) {
    if (!rp_is(p, 0, '{')) { re_fail(p, RE_E_PROPERTY); return 0; }
    uint32_t from = p->pos + 1, k = from;
    while (k < len(p->pat) && p->pat[k] != '}') k++;
    if (k >= len(p->pat)) { re_fail(p, RE_E_PROPERTY); return 0; }
    if (!cls_property(p, from, k, negate)) { re_fail(p, RE_E_PROPERTY); return 0; }
    p->pos = k + 1;
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

/* ---- the parser ---- */

static uint32_t rp_disjunction(re_parser *p);

/* a class [...], the [ read */
static uint32_t rp_class(re_parser *p) {
    int neg = 0;
    if (rp_is(p, 0, '^')) { neg = 1; p->pos++; }
    clear(p->cls);
    for (;;) {
        int64_t c = rp_at(p, 0);
        if (c < 0) { re_fail(p, RE_E_UNTERMINATED_CLASS); return 0; }
        if (c == ']') { p->pos++; break; }
        if ((p->flags & RF_V) && (c == '[' || ((c == '&' || c == '-') && rp_at(p, 1) == c))) { re_fail(p, RE_E_SET_OPS); return 0; }
        /* an atom: a character, or a class escape (a set: no range from or to it) */
        int64_t lo = -1;
        int set = 0;
        p->pos++;
        if (c == '\\') {
            int64_t e = rp_at(p, 0);
            if (e == 'd' || e == 'D' || e == 's' || e == 'S' || e == 'w' || e == 'W') { p->pos++; cls_escape(p, (uint32_t)e); set = 1; }
            else if ((e == 'p' || e == 'P') && rp_unicode(p)) { p->pos++; if (!rp_property(p, e == 'P')) return 0; set = 1; }
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
                if (rp_unicode(p)) { re_fail(p, RE_E_CLASS_ESCAPE); return 0; }
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
        if (!rp_property(p, c == 'P')) return 0;
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
        if (!rp_is(p, 0, '<')) { re_fail(p, RE_E_NAMED_REF); return 0; }
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
        uint32_t group = 0, look_kind = 0;
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
            } else if (d == 'i' || d == 'm' || d == 's' || d == '-') { re_fail(p, RE_E_MODIFIERS); return 0; }
            else { re_fail(p, RE_E_GROUP); return 0; }
        } else group = ++p->ngroups;
        uint32_t child = rp_disjunction(p);
        if (p->err) return 0;
        if (!rp_is(p, 0, ')')) { re_fail(p, RE_E_UNTERMINATED_GROUP); return 0; }
        p->pos++;
        p->depth--;
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
    int icase = (p->flags & RF_I) != 0, uni = rp_unicode(p);
    if (x.kind == RN_CHAR) {
        uint32_t ch = x.a;
        if (icase && (re_rep(ch, uni) != ch || re_orbit_next(ch, uni) != ch)) { rc_emit(c, RE_CHAR_I | back); rc_emit(c, re_rep(ch, uni)); }
        else { rc_emit(c, RE_CHAR | back); rc_emit(c, ch); }
        return;
    }
    if (x.kind == RN_ANY) { rc_emit(c, ((p->flags & RF_S) ? RE_ANY_ALL : RE_ANY) | back); return; }
    rc_emit(c, RE_CLASS | back | (x.c ? RB_NEG : 0) | (icase ? RB_ICASE : 0));
    rc_emit(c, x.b);
    for (uint32_t i = 0; i < 2 * x.b; i++) rc_emit(c, p->ranges[2 * x.a + i]);
}

static void rc_node(re_parser *p, re_comp *c, uint32_t n, uint32_t back) {
    if (c->err) return;
    if (len(c->code) > (1u << 22)) { c->err = RE_E_TOO_LARGE; return; }
    re_node x = p->nodes[n];
    switch (x.kind) {
    case RN_EMPTY: return;
    case RN_CHAR: case RN_ANY: case RN_CLASS: rc_one(p, c, n, back); return;
    case RN_BOL: rc_emit(c, RE_BOL | ((p->flags & RF_M) ? RB_MULTI : 0)); return;
    case RN_EOL: rc_emit(c, RE_EOL | ((p->flags & RF_M) ? RB_MULTI : 0)); return;
    case RN_WORDB: rc_emit(c, RE_WORDB | ((p->flags & RF_I) ? RB_ICASE : 0)); return;
    case RN_NWORDB: rc_emit(c, RE_WORDB | RB_NEG | ((p->flags & RF_I) ? RB_ICASE : 0)); return;
    case RN_SEQ:
        /* a lookbehind's terms right to left; a simple loop followed by a literal told of it */
        for (uint32_t i = 0; i < x.b; i++) {
            uint32_t t = p->lists[x.a + (back ? x.b - 1 - i : i)];
            uint32_t at = rc_here(c);
            rc_node(p, c, t, back);
            if (back || i + 1 >= x.b || (p->flags & (RF_I | RF_U | RF_V))) continue;
            re_node nx = p->nodes[p->lists[x.a + i + 1]];
            re_node tx = p->nodes[t];
            if (tx.kind == RN_REPEAT && nx.kind == RN_CHAR && rc_here(c) > at + 5 && (c->code[at] & 0xFF) == RE_SIMPLE)
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
        rc_emit(c, RE_BACKREF | back | ((p->flags & RF_I) ? RB_ICASE : 0));
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
        if (x.kind == RN_CHAR) return (p->flags & RF_I) || x.a > 0xFFFF ? 0 : x.a + 1;
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
        if (x.kind == RN_BOL) return !(p->flags & RF_M);
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
    int icase = (p->flags & RF_I) != 0, uni = rp_unicode(p);
    switch (x.kind) {
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
   among them), as many as 64; with i only ASCII ones, folded, whose orbits are their
   two cases. 0 for none */
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
                if (c.kind != RN_CHAR || c.a >= 0xD800 || (icase && (c.a >= 128 || (uni && ((c.a | 0x20) == 'k' || (c.a | 0x20) == 's'))))) break;
                out[n++] = icase && c.a >= 'A' && c.a <= 'Z' ? c.a + 32 : c.a;
            }
            if (k < g.b) break;
            continue;
        }
        if (t.kind != RN_CHAR || t.a >= 0xD800 || (icase && (t.a >= 128 || (uni && ((t.a | 0x20) == 'k' || (t.a | 0x20) == 's'))))) break;
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

/* a SyntaxError "Invalid regular expression: /src/flags: why" thrown */
static uint32_t re_syntax_error(js_vm *vm, uint32_t src, uint32_t flags, const char *why) {
    vector<char> m;
    const char *pre = "Invalid regular expression: /";
    append_range(m, pre, strlen(pre));
    js_str_to_utf8(vm, src, &m);
    push_back(m, '/');
    char fb[12];
    re_flags_text(flags, fb);
    append_range(m, fb, strlen(fb));
    append_range(m, ": ", 2);
    append_range(m, why, strlen(why));
    push_back(m, 0);
    js_throw_msg(vm, vm->syntax_error_proto, data(m));
    return 0;
}

static uint32_t re_hash_of(uint32_t atom, uint32_t flags) { return (atom * 2654435761u) ^ (flags * 40503u); }

/* the pattern src (a string) compiled with flags (bits): its regex's index, or 0 with a
   SyntaxError thrown. A pattern compiled once is found again */
uint32_t js_re_compile(js_vm *vm, uint32_t src, uint32_t flags) {
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
    if (p->err) return re_syntax_error(vm, atom, flags, re_errors[p->err]);
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
    if (c->err) return re_syntax_error(vm, atom, flags, re_errors[c->err]);
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
            r.pre = (uint32_t)len(vm->re_code);
            r.npre = np;
            for (uint32_t i = 0; i < np; i++) push_back(vm->re_code, pre[i]);
            for (uint32_t b = 0; b < 256; b++) push_back(vm->re_code, np);
            for (uint32_t i = 0; i + 1 < np; i++) vm->re_code[r.pre + np + (pre[i] & 0xFF)] = np - 1 - i;
            if ((flags & RF_I)) for (uint32_t i = 0; i + 1 < np; i++)
                if (pre[i] >= 'a' && pre[i] <= 'z') vm->re_code[r.pre + np + (pre[i] - 32)] = np - 1 - i;
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
    *re = js_re_compile(vm, src, flags);
    if (*re) return 0;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    jv msg = jv_is_obj(e) ? js_get(vm, e, A_MESSAGE) : JV_UNDEF;
    if (jv_is_str(msg)) js_str_to_utf8(vm, jv_idx(msg), err);
    vm->throwing = 0;
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
    int uni = (flags & (RF_U | RF_V)) != 0, iu = uni && (flags & RF_I);
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
                int a = re_is_word(s, n, (int64_t)pos - 1, iu && (w & RB_ICASE)), b = re_is_word(s, n, pos, iu && (w & RB_ICASE));
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
                uint32_t k = prog[pc + 1];
                bt_push(st, BT_CAP, k, cap[k], 0);
                cap[k] = pos;
                pc += 2;
                goto next;
            }
            case RE_RESET: {
                for (uint32_t k = prog[pc + 1]; k < prog[pc + 2]; k++) {
                    if (cap[k] == RE_INF) continue;
                    bt_push(st, BT_CAP, k, cap[k], 0);
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
                bt_push(st, BT_REG, 2 * r, regs[2 * r], 0);
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
                bt_push(st, BT_REG, 2 * r + 1, regs[2 * r + 1], 0);
                regs[2 * r + 1] = pos;
                pc += 2;
                goto next;
            }
            case RE_LOOP_NEXT: {
                uint32_t r = prog[pc + 1], min = prog[pc + 2];
                /* an iteration past the least that took nothing ends the loop's tries here */
                if (pos == regs[2 * r + 1] && regs[2 * r] >= min) goto fail;
                bt_push(st, BT_REG, 2 * r, regs[2 * r], 0);
                regs[2 * r]++;
                pc = (uint32_t)((int32_t)(pc + 4) + (int32_t)prog[pc + 3]);
                goto next;
            }
            case RE_SIMPLE: {
                /* min, max, greedy, the atom's length, the literal that follows (plus 1), the atom */
                uint32_t min = prog[pc + 1], max = prog[pc + 2], greedy = prog[pc + 3], al = prog[pc + 4], lit = prog[pc + 5];
                uint32_t atom = pc + 6, after = pc + 6 + al;
                uint32_t k = 0, p = pos, width = 1;
                if (greedy) {
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

/* the regex re run on the string s from index start: 1 a match (the groups' slots in
   vm->re_caps, 2 a group, RE_INF unset), 0 none, -1 the step budget spent (vm->throwing 2) */
int js_re_exec(js_vm *vm, uint32_t re, uint32_t s, uint32_t start) {
    js_regex r = vm->regexes[re];
    js_str_flatten(vm, s);
    uint32_t n = vm->strs[s].len, off = vm->strs[s].off;
    resize(vm->re_caps, 2 * r.ngroups, 0);
    resize(vm->re_regs, 2 * r.nregs + 2, 0);
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
                /* Horspool over the literal prefix: the place's last character's skip */
                uint32_t np = r.npre, last = pre[np - 1];
                int found_at = 0;
                while (i + np <= n) {
                    uint32_t c = str[i + np - 1];
                    uint32_t fc = icase && c >= 'A' && c <= 'Z' ? c + 32 : c;
                    if (fc == last) {
                        uint32_t k = 0;
                        while (k + 1 < np) {
                            uint32_t d = str[i + k];
                            uint32_t fd = icase && d >= 'A' && d <= 'Z' ? d + 32 : d;
                            if (fd != pre[k]) break;
                            k++;
                        }
                        if (k + 1 >= np) { found_at = 1; break; }
                    }
                    i += pre[np + (c & 0xFF)];
                }
                if (!found_at) break;
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

/* this as a RegExp's regex, or 0 with a TypeError thrown */
static uint32_t re_this(js_vm *vm, jv thisv, const char *what) {
    if (jv_is_obj(thisv) && vm->objs[jv_idx(thisv)].kind == JK_REGEXP) return vm->objs[jv_idx(thisv)].a;
    char m[160];
    snprintf(m, sizeof m, "%s called on an object that is not a RegExp", what);
    js_throw_msg(vm, vm->type_error_proto, m);
    return 0;
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
        if (global) js_set(vm, rxv, A_LASTINDEX, jv_num(0));
        return vm->throwing ? -1 : 0;
    }
    int rc = js_re_exec(vm, re, s, (uint32_t)li);
    if (rc < 0) return -1;
    if (rc == 0) {
        if (global) js_set(vm, rxv, A_LASTINDEX, jv_num(0));
        return vm->throwing ? -1 : 0;
    }
    if (global) js_set(vm, rxv, A_LASTINDEX, jv_num(vm->re_caps[1]));
    return vm->throwing ? -1 : 1;
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
    js_str_flatten(vm, s);
    for (uint32_t k = from; k < to; k++) push_back(*out, vm->units[vm->strs[s].off + k]);
}

/* s with rx's matches replaced: every one with g (lastIndex from 0), else the first from
   exec's place; rep a function called for each, or a template */
static jv re_replace(js_vm *vm, jv rxv, uint32_t re, uint32_t s, jv rep) {
    js_regex r = vm->regexes[re];
    int global = (r.flags & RF_G) != 0, uni = (r.flags & (RF_U | RF_V)) != 0;
    int fn = js_is_callable(vm, rep);
    uint32_t tpl = 0;
    if (!fn) { tpl = js_to_string(vm, rep); if (!tpl) return JV_HOLE; }
    if (global) { js_set(vm, rxv, A_LASTINDEX, jv_num(0)); if (vm->throwing) return JV_HOLE; }
    /* every match's slots, one after another */
    vector<uint32_t> all;
    uint32_t ng = r.ngroups;
    for (;;) {
        int rc = re_builtin_exec(vm, rxv, re, s);
        if (rc < 0) return JV_HOLE;
        if (rc == 0) break;
        for (uint32_t k = 0; k < 2 * ng; k++) push_back(all, vm->re_caps[k]);
        if (!global) break;
        if (vm->re_caps[0] == vm->re_caps[1]) {
            js_set(vm, rxv, A_LASTINDEX, jv_num(re_advance(vm, s, vm->re_caps[1], uni)));
            if (vm->throwing) return JV_HOLE;
        }
    }
    vector<uint16_t> out;
    uint32_t next_src = 0;
    uint32_t nm = (uint32_t)(len(all) / (2 * ng));
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
            for (size_t k = 0; k < len(piece); k++) push_back(out, piece[k]);
            next_src = end;
        }
    }
    js_str_flatten(vm, s);
    if (next_src == 0 && nm == 0) return jv_str(s);
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
    js_define(vm, o, A_LASTINDEX, jv_num(0), JS_SHAPE_HIDDEN);
    return jv_obj(o);
}

/* a literal's object: its regex compiled with the script */
jv js_re_object(js_vm *vm, uint32_t re) { return re_object(vm, re, vm->regexp_proto); }

/* a value as a regex for String's methods: a RegExp's own, or a new one of its text */
static uint32_t re_of_value(js_vm *vm, jv v, uint32_t flags, jv *obj) {
    if (jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_REGEXP) { *obj = v; return vm->objs[jv_idx(v)].a; }
    uint32_t src = v == JV_UNDEF ? A_EMPTY : js_to_string(vm, v);
    if (!src) return 0;
    uint32_t re = js_re_compile(vm, src, flags);
    if (!re) return 0;
    *obj = re_object(vm, re, vm->regexp_proto);
    return re;
}

/* the matches of a global regex as an array of exec's results, from its lastIndex (matchAll's) */
static jv re_all_results(js_vm *vm, jv rxv, uint32_t re, uint32_t s) {
    uint32_t arr = js_array_new(vm, 0);
    int uni = (vm->regexes[re].flags & (RF_U | RF_V)) != 0;
    for (;;) {
        int rc = re_builtin_exec(vm, rxv, re, s);
        if (rc < 0) return JV_HOLE;
        if (rc == 0) break;
        uint32_t a = vm->re_caps[0], b = vm->re_caps[1];
        jv m = re_result(vm, re, s);
        js_array_push(vm, arr, m);
        if (!(vm->regexes[re].flags & (RF_G | RF_Y))) break;
        if (a == b) {
            js_set(vm, rxv, A_LASTINDEX, jv_num(re_advance(vm, s, b, uni)));
            if (vm->throwing) return JV_HOLE;
        }
    }
    return jv_obj(arr);
}

jv js_re_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    (void)callee;
    jv a0 = re_arg(vm, args, argc, 0), a1 = re_arg(vm, args, argc, 1);
    switch (which) {
    case JRE_CTOR: {
        int is_re = jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_REGEXP;
        if ((newt == JV_UNDEF || newt == JV_HOLE) && is_re && a1 == JV_UNDEF) {
            jv c = js_get(vm, a0, A_CONSTRUCTOR);
            if (vm->throwing) return JV_HOLE;
            if (c == callee) return a0;
        }
        uint32_t src, flags;
        if (is_re) {
            js_regex r = vm->regexes[vm->objs[jv_idx(a0)].a];
            src = r.source;
            flags = r.flags;
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
    case JRE_EXEC: case JRE_TEST: {
        uint32_t re = re_this(vm, thisv, which == JRE_EXEC ? "RegExp.prototype.exec" : "RegExp.prototype.test");
        if (!re) return JV_HOLE;
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        int rc = re_builtin_exec(vm, thisv, re, s);
        if (rc < 0) return JV_HOLE;
        if (which == JRE_TEST) return jv_bool(rc);
        return rc ? re_result(vm, re, s) : JV_NULL;
    }
    case JRE_TOSTRING: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "RegExp.prototype.toString called on a value that is not an object");
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
        uint32_t re = re_this(vm, thisv, "RegExp.prototype.source getter");
        if (!re) return JV_HOLE;
        return jv_str(re_source_text(vm, vm->regexes[re].source));
    }
    case JRE_FLAGS: {
        if (!jv_is_obj(thisv)) return js_throw_msg(vm, vm->type_error_proto, "RegExp.prototype.flags getter called on a value that is not an object");
        if (vm->objs[jv_idx(thisv)].kind != JK_REGEXP) return jv_str(A_EMPTY);
        char b[12];
        re_flags_text(vm->regexes[vm->objs[jv_idx(thisv)].a].flags, b);
        return jv_str(js_str_ascii(vm, b));
    }
    case JRE_GLOBAL: case JRE_IGNORECASE: case JRE_MULTILINE: case JRE_DOTALL: case JRE_UNICODE: case JRE_UNICODESETS:
    case JRE_STICKY: case JRE_HASINDICES: {
        if (jv_is_obj(thisv) && jv_idx(thisv) == vm->regexp_proto) return JV_UNDEF;
        uint32_t re = re_this(vm, thisv, "a RegExp flag's getter");
        if (!re) return JV_HOLE;
        static const uint32_t bit[] = { RF_G, RF_I, RF_M, RF_S, RF_U, RF_V, RF_Y, RF_D };
        return jv_bool((vm->regexes[re].flags & bit[which - JRE_GLOBAL]) != 0);
    }
    case JRE_MATCH: case JRE_SP_MATCH: {
        /* rx[Symbol.match](s), and s.match(rx) through it */
        jv rxv = thisv;
        uint32_t s, re;
        if (which == JRE_SP_MATCH) {
            if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "String.prototype.match called on null or undefined");
            s = js_to_string(vm, thisv);
            if (!s) return JV_HOLE;
            re = re_of_value(vm, a0, 0, &rxv);
        } else {
            re = re_this(vm, thisv, "RegExp.prototype[Symbol.match]");
            if (!re) return JV_HOLE;
            s = js_to_string(vm, a0);
            if (!s) return JV_HOLE;
        }
        if (!re) return JV_HOLE;
        if (!(vm->regexes[re].flags & RF_G)) {
            int rc = re_builtin_exec(vm, rxv, re, s);
            if (rc < 0) return JV_HOLE;
            return rc ? re_result(vm, re, s) : JV_NULL;
        }
        int uni = (vm->regexes[re].flags & (RF_U | RF_V)) != 0;
        js_set(vm, rxv, A_LASTINDEX, jv_num(0));
        if (vm->throwing) return JV_HOLE;
        uint32_t arr = js_array_new(vm, 0);
        for (;;) {
            int rc = re_builtin_exec(vm, rxv, re, s);
            if (rc < 0) return JV_HOLE;
            if (rc == 0) break;
            uint32_t a = vm->re_caps[0], b = vm->re_caps[1];
            js_array_push(vm, arr, jv_str(js_str_sub(vm, s, a, b)));
            if (a == b) { js_set(vm, rxv, A_LASTINDEX, jv_num(re_advance(vm, s, b, uni))); if (vm->throwing) return JV_HOLE; }
        }
        return vm->objs[arr].b ? jv_obj(arr) : JV_NULL;
    }
    case JRE_MATCHALL: case JRE_SP_MATCHALL: {
        jv rxv = thisv;
        uint32_t s, re;
        if (which == JRE_SP_MATCHALL) {
            if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "String.prototype.matchAll called on null or undefined");
            if (jv_is_obj(a0) && vm->objs[jv_idx(a0)].kind == JK_REGEXP && !(vm->regexes[vm->objs[jv_idx(a0)].a].flags & RF_G))
                return js_throw_msg(vm, vm->type_error_proto, "String.prototype.matchAll called with a non-global RegExp argument");
            s = js_to_string(vm, thisv);
            if (!s) return JV_HOLE;
            re = re_of_value(vm, a0, RF_G, &rxv);
        } else {
            re = re_this(vm, thisv, "RegExp.prototype[Symbol.matchAll]");
            if (!re) return JV_HOLE;
            s = js_to_string(vm, a0);
            if (!s) return JV_HOLE;
        }
        if (!re) return JV_HOLE;
        /* the regex copied, its lastIndex kept: the original's is not moved */
        jv li = js_get(vm, rxv, A_LASTINDEX);
        if (vm->throwing) return JV_HOLE;
        jv copy = re_object(vm, re, vm->regexp_proto);
        js_set(vm, copy, A_LASTINDEX, jv_num(re_to_length(vm, li)));
        jv all = re_all_results(vm, copy, re, s);
        if (vm->throwing) return JV_HOLE;
        uint32_t it = js_obj_new(vm, JK_ITER, vm->iterator_proto);
        vm->objs[it].a = jv_idx(all);
        vm->objs[it].c = JI_ARRAY;
        return jv_obj(it);
    }
    case JRE_REPLACE: case JRE_SP_REPLACE: case JRE_SP_REPLACEALL: {
        jv rxv = thisv;
        uint32_t s, re;
        jv rep = a1;
        if (which == JRE_REPLACE) {
            re = re_this(vm, thisv, "RegExp.prototype[Symbol.replace]");
            if (!re) return JV_HOLE;
            s = js_to_string(vm, a0);
            if (!s) return JV_HOLE;
        } else {
            s = js_to_string(vm, thisv);
            if (!s) return JV_HOLE;
            rxv = a0;
            re = vm->objs[jv_idx(a0)].a;
            if (which == JRE_SP_REPLACEALL && !(vm->regexes[re].flags & RF_G))
                return js_throw_msg(vm, vm->type_error_proto, "replaceAll must be called with a global RegExp");
        }
        return re_replace(vm, rxv, re, s, rep);
    }
    case JRE_SEARCH: case JRE_SP_SEARCH: {
        jv rxv = thisv;
        uint32_t s, re;
        if (which == JRE_SP_SEARCH) {
            if (jv_is_nullish(thisv)) return js_throw_msg(vm, vm->type_error_proto, "String.prototype.search called on null or undefined");
            s = js_to_string(vm, thisv);
            if (!s) return JV_HOLE;
            re = re_of_value(vm, a0, 0, &rxv);
        } else {
            re = re_this(vm, thisv, "RegExp.prototype[Symbol.search]");
            if (!re) return JV_HOLE;
            s = js_to_string(vm, a0);
            if (!s) return JV_HOLE;
        }
        if (!re) return JV_HOLE;
        /* lastIndex set to 0 for the search, then put back */
        jv prev = js_get(vm, rxv, A_LASTINDEX);
        if (vm->throwing) return JV_HOLE;
        uint32_t f = vm->regexes[re].flags;
        int rc = js_re_exec(vm, re, s, 0);
        if (rc < 0) return JV_HOLE;
        (void)f;
        js_set(vm, rxv, A_LASTINDEX, prev);
        return jv_num(rc ? (double)vm->re_caps[0] : -1.0);
    }
    case JRE_SPLIT: case JRE_SP_SPLIT: {
        uint32_t s, re;
        jv lim = a1;
        if (which == JRE_SPLIT) {
            re = re_this(vm, thisv, "RegExp.prototype[Symbol.split]");
            if (!re) return JV_HOLE;
            s = js_to_string(vm, a0);
            if (!s) return JV_HOLE;
        } else {
            s = js_to_string(vm, thisv);
            if (!s) return JV_HOLE;
            re = vm->objs[jv_idx(a0)].a;
        }
        return re_split(vm, re, s, lim);
    }
    default: return JV_UNDEF;
    }
}

/* the RegExp constructor and its prototype's methods and getters; first, the native id of JRE_CTOR */
void js_re_init(js_vm *vm, uint32_t first) {
    uint32_t rp = vm->regexp_proto;
    uint32_t c = js_native_new(vm, first + JRE_CTOR, js_atom_ascii(vm, "RegExp"), 2);
    js_define(vm, c, A_PROTOTYPE, jv_obj(rp), JS_SHAPE_HIDDEN);
    js_define(vm, rp, A_CONSTRUCTOR, jv_obj(c), JS_SHAPE_HIDDEN);
    js_define(vm, vm->global, js_atom_ascii(vm, "RegExp"), jv_obj(c), JS_SHAPE_HIDDEN);
    static const char *const fns[] = { "exec", "test", "toString" };
    static const uint32_t fids[] = { JRE_EXEC, JRE_TEST, JRE_TOSTRING };
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t at = js_atom_ascii(vm, fns[i]);
        js_define(vm, rp, at, jv_obj(js_native_new(vm, first + fids[i], at, i == 2 ? 0 : 1)), JS_SHAPE_HIDDEN);
    }
    static const char *const gets[] = { "source", "flags", "global", "ignoreCase", "multiline", "dotAll", "unicode", "unicodeSets", "sticky", "hasIndices" };
    static const uint32_t gids[] = { JRE_SOURCE, JRE_FLAGS, JRE_GLOBAL, JRE_IGNORECASE, JRE_MULTILINE, JRE_DOTALL, JRE_UNICODE, JRE_UNICODESETS, JRE_STICKY, JRE_HASINDICES };
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t at = js_atom_ascii(vm, gets[i]);
        js_define_accessor(vm, rp, at, jv_obj(js_native_new(vm, first + gids[i], at, 0)), JV_HOLE, JS_SHAPE_HIDDEN);
    }
    static const uint32_t syms[] = { JSYM_MATCH, JSYM_MATCH_ALL, JSYM_REPLACE, JSYM_SEARCH, JSYM_SPLIT };
    static const uint32_t sids[] = { JRE_MATCH, JRE_MATCHALL, JRE_REPLACE, JRE_SEARCH, JRE_SPLIT };
    for (uint32_t i = 0; i < 5; i++) js_define(vm, rp, JS_SYM_KEY | syms[i], jv_obj(js_native_new(vm, first + sids[i], A_EMPTY, i == 2 || i == 4 ? 2 : 1)), JS_SHAPE_HIDDEN);
}
