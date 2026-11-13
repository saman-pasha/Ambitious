/* src/css/cssom.c -- the CSS Object Model's sheets and rules (W3C CSSOM 1; src/css/cssom.h),
   as Chromium parses and serializes them, each measured in Chromium first.

   PARSING. A sheet's text is CSS Syntax 3's tokens (css_omt_tokens) read into rules: at the
   top level @import and @layer statements first, then @namespace, then the rest, as
   Chromium's sheet keeps them (one out of that order dropped); @charset is no rule; an
   unknown at-rule, and a rule whose prelude its kind does not take, dropped. A style rule's
   selector list is checked and written as Chromium writes it (Selectors 4: compounds, the
   combinators, attribute selectors' values as strings, the pseudo-classes and
   pseudo-elements Chromium knows, An+B, :is() :where() :not() :has() :nth-*(of S), :host()
   ::slotted() ::part(), the nesting selector; a nested rule's relative selector given its
   & as Chromium gives it); its block holds declarations (src/css/cssom_decl.c) and nested
   rules, a run of declarations after a nested rule a CSSNestedDeclarations rule. @media's
   and @container's conditions are written anew (lower case, a space after each colon and
   about each comparison), @supports' kept as written; @keyframes' keys made percentages,
   one past 100% dropping its keyframe; @font-face, @page (and its margin rules), @property,
   @counter-style, @font-palette-values, @view-transition and @position-try hold their
   descriptors; @font-feature-values its blocks as written; @layer, @scope and
   @starting-style rules as grouping rules.

   SERIALIZING. Each rule's cssText as Chromium's classes write it: a grouping rule's own
   rules each after a line break and two spaces (not indented again), @keyframes' with a
   space before its first line break, a style rule's declarations on its line unless it
   has rules of its own.

   CHANGES. insertRule's checks in Chromium's order (the index, the parse, a constructed
   sheet's @import, the sheet's order: an @import after other rules, a rule before an
   @import, an @namespace among other rules), deleteRule's (an @namespace among other rules
   is kept); a grouping rule's @import and @namespace refused.

   THE CASCADE. css_om_reg_* keep, for each document, what its host said of the sheets its
   scripts changed; src/css/css.c reads it as it reads a document's sheets (a disabled one
   passed over, a changed one's text the CSSOM's, the adopted ones after each tree's own). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include "cssom.h"
#include "cssom_int.h"
#include "cssom_props.h"

/* ---- properties ---- */

static char om_low(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int css_om_prop(const char *name, size_t n) {
    if (n >= 2 && name[0] == '-' && name[1] == '-') return CSS_OM_CUSTOM;
    char low[96];
    if (n == 0 || n >= sizeof low) return -1;
    for (size_t i = 0; i < n; i++) low[i] = om_low(name[i]);
    low[n] = 0;
    int lo = 0, hi = CSS_OM_NPROPS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(css_om_pdefs[mid].name, low);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

const char *css_om_prop_name(uint32_t p) {
    if (p < CSS_OM_NPROPS) return css_om_pdefs[p].name;
    return "";
}
uint32_t css_om_unalias(uint32_t p) {
    if (p < CSS_OM_NPROPS && css_om_pdefs[p].kind == 2) return css_om_pdefs[p].target;
    return p;
}
int css_om_is_shorthand(uint32_t p) { return p < CSS_OM_NPROPS && css_om_pdefs[p].kind == 1; }
uint32_t css_om_longhands(uint32_t p) { return p < CSS_OM_NPROPS && css_om_pdefs[p].kind == 1 ? css_om_pdefs[p].n : 0; }
uint32_t css_om_longhand(uint32_t p, uint32_t i) {
    if (p >= CSS_OM_NPROPS || i >= css_om_pdefs[p].n) return 0;
    return css_om_longs[css_om_pdefs[p].first + i];
}

/* ---- text helpers ---- */

void css_omx_lower(const char *s, size_t n, vector<char> *out) {
    for (size_t i = 0; i < n; i++) push_back(*out, om_low(s[i]));
}
int css_omx_eq(const char *a, size_t an, const char *b, size_t bn) { return an == bn && (an == 0 || memcmp(a, b, an) == 0); }
int css_omx_ieq(const char *a, size_t an, const char *w) {
    size_t wn = strlen(w);
    if (an != wn) return 0;
    for (size_t i = 0; i < an; i++) if (om_low(a[i]) != w[i]) return 0;
    return 1;
}
static void om_cat(vector<char> *out, const char *s) { append_range(*out, s, strlen(s)); }

/* ---- tokens (CSS Syntax 3, 4) ---- */

static int om_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int om_digit(char c) { return c >= '0' && c <= '9'; }
static int om_hexd(char c) { return om_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int om_hexv(char c) { return om_digit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10; }
static int om_nstart(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
static int om_nchar(unsigned char c) { return om_nstart(c) || om_digit((char)c) || c == '-'; }
static int om_nl(char c) { return c == '\n' || c == '\r' || c == '\f'; }
/* a valid escape: a backslash not before a newline */
static int om_esc(const char *p, size_t n, size_t i) { return i < n && p[i] == '\\' && !(i + 1 < n && om_nl(p[i + 1])); }
static int om_ident_start(const char *p, size_t n, size_t i) {
    if (i >= n) return 0;
    unsigned char c = (unsigned char)p[i];
    if (c == '-') {
        if (i + 1 < n && (om_nstart((unsigned char)p[i + 1]) || p[i + 1] == '-')) return 1;
        return om_esc(p, n, i + 1);
    }
    if (om_nstart(c)) return 1;
    return om_esc(p, n, i);
}
static int om_num_start(const char *p, size_t n, size_t i) {
    if (i >= n) return 0;
    char c = p[i];
    if (c == '+' || c == '-') {
        if (i + 1 >= n) return 0;
        if (om_digit(p[i + 1])) return 1;
        return p[i + 1] == '.' && i + 2 < n && om_digit(p[i + 2]);
    }
    if (c == '.') return i + 1 < n && om_digit(p[i + 1]);
    return om_digit(c);
}
/* a UTF-8 sequence's length from its lead byte */
static size_t om_u8len(unsigned char c) { return c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }
/* past an escape whose backslash is before i */
static size_t om_esc_end(const char *p, size_t n, size_t i) {
    if (i >= n) return i;
    if (om_hexd(p[i])) {
        size_t k = 0;
        while (i < n && k < 6 && om_hexd(p[i])) { i++; k++; }
        if (i < n && om_ws(p[i])) {
            if (p[i] == '\r' && i + 1 < n && p[i + 1] == '\n') i++;
            i++;
        }
        return i;
    }
    size_t l = om_u8len((unsigned char)p[i]);
    return i + l <= n ? i + l : n;
}
static size_t om_name_end(const char *p, size_t n, size_t i) {
    while (i < n) {
        if (om_nchar((unsigned char)p[i])) { i++; continue; }
        if (om_esc(p, n, i)) { i = om_esc_end(p, n, i + 1); continue; }
        break;
    }
    return i;
}
static size_t om_number_end(const char *p, size_t n, size_t i, int *integer) {
    *integer = 1;
    if (i < n && (p[i] == '+' || p[i] == '-')) i++;
    while (i < n && om_digit(p[i])) i++;
    if (i + 1 < n && p[i] == '.' && om_digit(p[i + 1])) {
        *integer = 0;
        i++;
        while (i < n && om_digit(p[i])) i++;
    }
    if (i + 1 < n && (p[i] == 'e' || p[i] == 'E')) {
        size_t k = i + 1;
        if (k < n && (p[k] == '+' || p[k] == '-')) k++;
        if (k < n && om_digit(p[k])) {
            *integer = 0;
            i = k;
            while (i < n && om_digit(p[i])) i++;
        }
    }
    return i;
}
static double om_number_of(const char *p, size_t s, size_t e) {
    char buf[80];
    size_t k = e - s;
    if (k >= sizeof buf) k = sizeof buf - 1;
    memcpy(buf, p + s, k);
    buf[k] = 0;
    return strtod(buf, 0);
}
static void om_tok_push(vector<css_omt> *out, uint32_t t, size_t s, size_t e) {
    css_omt k;
    memset(&k, 0, sizeof k);
    k.t = t;
    k.s = (uint32_t)s;
    k.e = (uint32_t)e;
    push_back(*out, k);
}

void css_omt_tokens(const char *p, size_t n, vector<css_omt> *out) {
    clear(*out);
    /* room for a token every few bytes at once, not grown token by token */
    reserve(*out, n / 3 + 8);
    size_t i = 0;
    while (i < n) {
        /* comments */
        if (p[i] == '/' && i + 1 < n && p[i + 1] == '*') {
            size_t e = i + 2;
            while (e + 1 < n && !(p[e] == '*' && p[e + 1] == '/')) e++;
            i = e + 1 < n ? e + 2 : n;
            continue;
        }
        size_t s = i;
        char c = p[i];
        if (om_ws(c)) {
            while (i < n && om_ws(p[i])) i++;
            om_tok_push(out, OT_WS, s, i);
            continue;
        }
        if (c == '"' || c == '\'') {
            i++;
            uint32_t t = OT_STR;
            while (i < n && p[i] != c) {
                if (om_nl(p[i])) { t = OT_BADSTR; break; }
                if (p[i] == '\\') {
                    if (i + 1 >= n) { i++; break; }
                    if (om_nl(p[i + 1])) { i += (p[i + 1] == '\r' && i + 2 < n && p[i + 2] == '\n') ? 3 : 2; continue; }
                    i = om_esc_end(p, n, i + 1);
                    continue;
                }
                i++;
            }
            if (t == OT_STR && i < n) i++;
            om_tok_push(out, t, s, i);
            continue;
        }
        if (c == '#') {
            if (i + 1 < n && (om_nchar((unsigned char)p[i + 1]) || om_esc(p, n, i + 1))) {
                int id = om_ident_start(p, n, i + 1);
                i = om_name_end(p, n, i + 1);
                om_tok_push(out, OT_HASH, s, i);
                if (id) (*out)[len(*out) - 1].f = OTF_ID;
                continue;
            }
            om_tok_push(out, OT_DELIM, s, i + 1);
            i++;
            continue;
        }
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == ',' || c == ':' || c == ';') {
            uint32_t t = c == '(' ? OT_LPAREN : c == ')' ? OT_RPAREN : c == '[' ? OT_LBRACK : c == ']' ? OT_RBRACK : c == '{' ? OT_LBRACE :
                         c == '}' ? OT_RBRACE : c == ',' ? OT_COMMA : c == ':' ? OT_COLON : OT_SEMI;
            om_tok_push(out, t, s, i + 1);
            i++;
            continue;
        }
        if (om_num_start(p, n, i)) {
            int integer;
            size_t e = om_number_end(p, n, i, &integer);
            double v = om_number_of(p, i, e);
            uint32_t t = OT_NUM;
            size_t u = e;
            if (om_ident_start(p, n, e)) { t = OT_DIM; e = om_name_end(p, n, e); }
            else if (e < n && p[e] == '%') { t = OT_PCT; e++; }
            om_tok_push(out, t, s, e);
            size_t k = len(*out) - 1;
            (*out)[k].v = v;
            (*out)[k].u = (uint32_t)u;
            (*out)[k].f = (integer ? OTF_INT : 0) | (p[s] == '+' || p[s] == '-' ? OTF_SIGN : 0);
            i = e;
            continue;
        }
        if (c == '-' && i + 2 < n && p[i + 1] == '-' && p[i + 2] == '>') {
            om_tok_push(out, OT_CDC, s, i + 3);
            i += 3;
            continue;
        }
        if (c == '<' && i + 3 < n && p[i + 1] == '!' && p[i + 2] == '-' && p[i + 3] == '-') {
            om_tok_push(out, OT_CDO, s, i + 4);
            i += 4;
            continue;
        }
        if (c == '@') {
            if (om_ident_start(p, n, i + 1)) {
                i = om_name_end(p, n, i + 1);
                om_tok_push(out, OT_AT, s, i);
                continue;
            }
            om_tok_push(out, OT_DELIM, s, i + 1);
            i++;
            continue;
        }
        if (om_ident_start(p, n, i)) {
            size_t e = om_name_end(p, n, i);
            if (e < n && p[e] == '(') {
                /* url( with no quote after its white space: a URL token; else a function */
                int is_url = e - i == 3 && om_low(p[i]) == 'u' && om_low(p[i + 1]) == 'r' && om_low(p[i + 2]) == 'l';
                size_t k = e + 1;
                while (k < n && om_ws(p[k])) k++;
                if (is_url && !(k < n && (p[k] == '"' || p[k] == '\''))) {
                    uint32_t t = OT_URL;
                    size_t u = k;
                    while (k < n && p[k] != ')') {
                        if (om_ws(p[k])) {
                            size_t w = k;
                            while (w < n && om_ws(p[w])) w++;
                            if (w < n && p[w] != ')') t = OT_BADURL;
                            k = w;
                            continue;
                        }
                        if (p[k] == '"' || p[k] == '\'' || p[k] == '(' || (unsigned char)p[k] < 0x20) t = OT_BADURL;
                        if (p[k] == '\\') {
                            if (om_esc(p, n, k)) { k = om_esc_end(p, n, k + 1); continue; }
                            t = OT_BADURL;
                        }
                        k++;
                    }
                    if (k < n) k++;
                    om_tok_push(out, t, s, k);
                    (*out)[len(*out) - 1].u = (uint32_t)u;
                    i = k;
                    continue;
                }
                om_tok_push(out, OT_FUNC, s, e + 1);
                i = e + 1;
                continue;
            }
            om_tok_push(out, OT_IDENT, s, e);
            i = e;
            continue;
        }
        /* any other code point: a delimiter (a UTF-8 sequence whole) */
        size_t l = om_u8len((unsigned char)c);
        if (i + l > n) l = n - i;
        om_tok_push(out, OT_DELIM, s, i + l);
        i += l;
    }
    om_tok_push(out, OT_EOF, n, n);
    /* each opener's closer */
    vector<uint32_t> stack;
    size_t nt = len(*out);
    for (size_t k = 0; k < nt; k++) (*out)[k].m = (uint32_t)(nt - 1);
    for (size_t k = 0; k < nt; k++) {
        uint32_t t = (*out)[k].t;
        if (t == OT_LPAREN || t == OT_FUNC || t == OT_LBRACK || t == OT_LBRACE) { push_back(stack, (uint32_t)k); continue; }
        if (t != OT_RPAREN && t != OT_RBRACK && t != OT_RBRACE) continue;
        if (!len(stack)) continue;
        uint32_t top = stack[len(stack) - 1];
        uint32_t ot = (*out)[top].t;
        int match = (t == OT_RPAREN && (ot == OT_LPAREN || ot == OT_FUNC)) || (t == OT_RBRACK && ot == OT_LBRACK) || (t == OT_RBRACE && ot == OT_LBRACE);
        if (!match) continue;
        (*out)[top].m = (uint32_t)k;
        resize(stack, len(stack) - 1);
    }
}

/* a code point as UTF-8 */
static void om_put_cp(vector<char> *out, uint32_t c) {
    if (c == 0 || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) c = 0xFFFD;
    if (c < 0x80) push_back(*out, (char)c);
    else if (c < 0x800) { push_back(*out, (char)(0xC0 | (c >> 6))); push_back(*out, (char)(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) {
        push_back(*out, (char)(0xE0 | (c >> 12)));
        push_back(*out, (char)(0x80 | ((c >> 6) & 0x3F)));
        push_back(*out, (char)(0x80 | (c & 0x3F)));
    } else {
        push_back(*out, (char)(0xF0 | (c >> 18)));
        push_back(*out, (char)(0x80 | ((c >> 12) & 0x3F)));
        push_back(*out, (char)(0x80 | ((c >> 6) & 0x3F)));
        push_back(*out, (char)(0x80 | (c & 0x3F)));
    }
}
/* p[s, e) with its escapes read, appended to out (a string's line continuations dropped) */
static void om_unescape(const char *p, size_t s, size_t e, vector<char> *out) {
    size_t i = s;
    while (i < e) {
        if (p[i] != '\\') { push_back(*out, p[i]); i++; continue; }
        if (i + 1 >= e) { om_put_cp(out, 0xFFFD); i++; continue; }
        if (om_nl(p[i + 1])) { i += (p[i + 1] == '\r' && i + 2 < e && p[i + 2] == '\n') ? 3 : 2; continue; }
        if (om_hexd(p[i + 1])) {
            uint32_t v = 0;
            size_t k = i + 1, d = 0;
            while (k < e && d < 6 && om_hexd(p[k])) { v = v * 16 + (uint32_t)om_hexv(p[k]); k++; d++; }
            if (k < e && om_ws(p[k])) { if (p[k] == '\r' && k + 1 < e && p[k + 1] == '\n') k++; k++; }
            om_put_cp(out, v);
            i = k;
            continue;
        }
        size_t l = om_u8len((unsigned char)p[i + 1]);
        if (i + 1 + l > e) l = e - i - 1;
        append_range(*out, p + i + 1, l);
        i += 1 + l;
    }
}

void css_omt_text(const char *p, const css_omt *t, vector<char> *out) {
    size_t s = t->s, e = t->e;
    switch (t->t) {
    case OT_FUNC: e--; break;
    case OT_AT: case OT_HASH: s++; break;
    case OT_STR: case OT_BADSTR:
        s++;
        if (t->t == OT_STR && e > s && p[e - 1] == p[t->s]) e--;
        break;
    case OT_URL: case OT_BADURL:
        s = t->u;
        if (e > s && p[e - 1] == ')') e--;
        while (e > s && om_ws(p[e - 1])) e--;
        break;
    case OT_DIM: e = t->u; break;
    default: break;
    }
    if (t->t == OT_DIM) { om_unescape(p, t->s, t->u, out); return; }
    om_unescape(p, s, e, out);
}
void css_omt_unit(const char *p, const css_omt *t, vector<char> *out) {
    if (t->t == OT_DIM) om_unescape(p, t->u, t->e, out);
}
int css_omt_is(const char *p, const css_omt *t, const char *w) {
    if (t->t != OT_IDENT && t->t != OT_FUNC) return 0;
    size_t e = t->t == OT_FUNC ? t->e - 1 : t->e;
    /* no escape in the keywords compared: the text as it is */
    return css_omx_ieq(p + t->s, e - t->s, w);
}
uint32_t css_omt_skip(const vector<css_omt> *tk, uint32_t i, uint32_t b) {
    while (i < b && (*tk)[i].t == OT_WS) i++;
    return i;
}

/* ---- serializers (CSSOM 2.1) ---- */

/* the code point at s[i] (UTF-8), its length in *l */
static uint32_t om_cp(const char *s, size_t n, size_t i, size_t *l) {
    unsigned char c = (unsigned char)s[i];
    size_t k = om_u8len(c);
    if (i + k > n) k = 1;
    *l = k;
    if (k == 1) return c;
    uint32_t v = k == 2 ? (c & 0x1Fu) : k == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t j = 1; j < k; j++) v = (v << 6) | ((unsigned char)s[i + j] & 0x3Fu);
    return v;
}
static void om_esc_cp(vector<char> *out, uint32_t c) {
    char b[16];
    int k = snprintf(b, sizeof b, "\\%x ", (unsigned)c);
    if (k > 0) append_range(*out, b, (size_t)k);
}
void css_omx_ident(const char *s, size_t n, vector<char> *out) {
    size_t i = 0;
    uint32_t first = 0;
    for (size_t k = 0; i < n; k++) {
        size_t l;
        uint32_t c = om_cp(s, n, i, &l);
        if (k == 0) first = c;
        if (c == 0) om_put_cp(out, 0xFFFD);
        else if ((c >= 1 && c <= 0x1F) || c == 0x7F) om_esc_cp(out, c);
        else if (k == 0 && c >= '0' && c <= '9') om_esc_cp(out, c);
        else if (k == 1 && c >= '0' && c <= '9' && first == '-') om_esc_cp(out, c);
        else if (k == 0 && c == '-' && n == 1) append_range(*out, "\\-", 2);
        else if (c >= 0x80 || c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            append_range(*out, s + i, l);
        else { push_back(*out, '\\'); append_range(*out, s + i, l); }
        i += l;
    }
}
void css_omx_string(const char *s, size_t n, vector<char> *out) {
    push_back(*out, '"');
    size_t i = 0;
    while (i < n) {
        size_t l;
        uint32_t c = om_cp(s, n, i, &l);
        if (c == 0) om_put_cp(out, 0xFFFD);
        else if ((c >= 1 && c <= 0x1F) || c == 0x7F) om_esc_cp(out, c);
        else if (c == '"' || c == '\\') { push_back(*out, '\\'); push_back(*out, (char)c); }
        else append_range(*out, s + i, l);
        i += l;
    }
    push_back(*out, '"');
}
void css_omx_num(double v, vector<char> *out) {
    char b[48];
    if (v == 0 || !isfinite(v)) v = isfinite(v) ? 0 : v;
    /* an integer below a million: its digits, as %.6g writes them, without printf's cost */
    if (v > -1e6 && v < 1e6 && v == (double)(long)v) {
        long iv = (long)v;
        int k = 0;
        if (iv < 0) { push_back(*out, '-'); iv = -iv; }
        do { b[k++] = (char)('0' + iv % 10); iv /= 10; } while (iv);
        while (k) push_back(*out, b[--k]);
        return;
    }
    int k = snprintf(b, sizeof b, "%.6g", v);
    if (k <= 0) return;
    if (k == 2 && b[0] == '-' && b[1] == '0') { push_back(*out, '0'); return; }
    append_range(*out, b, (size_t)k);
}
void css_omx_raw(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int keep_ws, vector<char> *out) {
    while (a < b && (*tk)[a].t == OT_WS) a++;
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    if (a >= b) return;
    if (keep_ws) { append_range(*out, p + (*tk)[a].s, (*tk)[b - 1].e - (*tk)[a].s); return; }
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS) { push_back(*out, ' '); continue; }
        append_range(*out, p + t.s, t.e - t.s);
    }
}
/* a component value list's tokens written: white space one space, none after an opener or
   before a closer or a comma; a comma ", " */
static void om_tokens_rec(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int lower, vector<char> *out) {
    int space = 0;
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS) { space = 1; continue; }
        if (t.t == OT_COMMA) { append_range(*out, ", ", 2); space = 0; continue; }
        if (t.t == OT_RPAREN || t.t == OT_RBRACK || t.t == OT_RBRACE) space = 0;
        if (space && len(*out) && (*out)[len(*out) - 1] != ' ' && (*out)[len(*out) - 1] != '(' && (*out)[len(*out) - 1] != '[') push_back(*out, ' ');
        space = 0;
        vector<char> v;
        switch (t.t) {
        case OT_IDENT:
            css_omt_text(p, &t, &v);
            if (lower) css_omx_lower(data(v), len(v), out);
            else css_omx_ident(data(v), len(v), out);
            break;
        case OT_FUNC: {
            css_omt_text(p, &t, &v);
            css_omx_lower(data(v), len(v), out);
            push_back(*out, '(');
            uint32_t e = t.m < b ? t.m : b;
            om_tokens_rec(p, tk, i + 1, e, lower, out);
            while (len(*out) && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
            push_back(*out, ')');
            i = e;
            break;
        }
        case OT_LPAREN: case OT_LBRACK: case OT_LBRACE: {
            push_back(*out, t.t == OT_LPAREN ? '(' : t.t == OT_LBRACK ? '[' : '{');
            uint32_t e = t.m < b ? t.m : b;
            om_tokens_rec(p, tk, i + 1, e, lower, out);
            while (len(*out) && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
            push_back(*out, t.t == OT_LPAREN ? ')' : t.t == OT_LBRACK ? ']' : '}');
            i = e;
            break;
        }
        case OT_NUM: css_omx_num(t.v, out); break;
        case OT_PCT: css_omx_num(t.v, out); push_back(*out, '%'); break;
        case OT_DIM:
            css_omx_num(t.v, out);
            css_omt_unit(p, &t, &v);
            css_omx_lower(data(v), len(v), out);
            break;
        case OT_STR: css_omt_text(p, &t, &v); css_omx_string(data(v), len(v), out); break;
        case OT_URL: css_omt_text(p, &t, &v); om_cat(out, "url("); css_omx_string(data(v), len(v), out); push_back(*out, ')'); break;
        case OT_HASH: push_back(*out, '#'); css_omt_text(p, &t, &v); append_range(*out, data(v), len(v)); break;
        default: append_range(*out, p + t.s, t.e - t.s); break;
        }
    }
}
void css_omx_tokens(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int lower, vector<char> *out) {
    while (a < b && (*tk)[a].t == OT_WS) a++;
    while (b > a && (*tk)[b - 1].t == OT_WS) b--;
    size_t start = len(*out);
    om_tokens_rec(p, tk, a, b, lower, out);
    while (len(*out) > start && (*out)[len(*out) - 1] == ' ') resize(*out, len(*out) - 1);
}

/* ---- the model's records ---- */

uint32_t css_om_put(css_om *m, const char *t, size_t n) {
    if (!len(m->text)) push_back(m->text, 0);
    uint32_t off = (uint32_t)len(m->text);
    append_range(m->text, t, n);
    return off;
}
void css_om_span(const css_om *m, uint32_t off, uint32_t n, vector<char> *out) {
    if (n && (size_t)off + n <= len(m->text)) append_range(*out, data(m->text) + off, n);
}
static void om_put_v(css_om *m, const vector<char> *t, uint32_t *off, uint32_t *n) {
    *off = css_om_put(m, data(*t), len(*t));
    *n = (uint32_t)len(*t);
}

uint32_t css_om_sheet_new(css_om *m, uint32_t origin) {
    if (!len(m->sheets)) {
        css_om_sheet z;
        memset(&z, 0, sizeof z);
        push_back(m->sheets, z);
    }
    css_om_sheet s;
    memset(&s, 0, sizeof s);
    s.live = 1;
    s.origin = origin;
    s.clean = 1;
    push_back(m->sheets, s);
    m->rev++;
    return (uint32_t)len(m->sheets) - 1;
}
static uint32_t om_rule_new(css_om *m, uint32_t kind) {
    if (!len(m->rules)) {
        css_om_rule z;
        memset(&z, 0, sizeof z);
        push_back(m->rules, z);
    }
    css_om_rule r;
    memset(&r, 0, sizeof r);
    r.live = 1;
    r.kind = kind;
    push_back(m->rules, r);
    return (uint32_t)len(m->rules) - 1;
}
/* a list's first rule and count: a sheet's (parent 0) or a rule's */
static uint32_t om_first(const css_om *m, uint32_t sheet, uint32_t parent) {
    if (parent) return m->rules[parent].first;
    return sheet && sheet < len(m->sheets) ? m->sheets[sheet].first : 0;
}
uint32_t css_om_count(const css_om *m, uint32_t sheet, uint32_t parent) {
    if (parent) return parent < len(m->rules) ? m->rules[parent].count : 0;
    return sheet && sheet < len(m->sheets) ? m->sheets[sheet].count : 0;
}
uint32_t css_om_at(const css_om *m, uint32_t sheet, uint32_t parent, uint32_t index) {
    uint32_t r = om_first(m, sheet, parent);
    for (uint32_t k = 0; r && k < index; k++) r = m->rules[r].next;
    return r;
}
/* rule r linked into a list before the rule `before` (0: at its end) */
static void om_link(css_om *m, uint32_t sheet, uint32_t parent, uint32_t r, uint32_t before) {
    uint32_t prev = before ? m->rules[before].prev : (parent ? m->rules[parent].last : m->sheets[sheet].last);
    m->rules[r].sheet = sheet;
    m->rules[r].parent = parent;
    m->rules[r].prev = prev;
    m->rules[r].next = before;
    if (prev) m->rules[prev].next = r;
    else if (parent) m->rules[parent].first = r;
    else m->sheets[sheet].first = r;
    if (before) m->rules[before].prev = r;
    else if (parent) m->rules[parent].last = r;
    else m->sheets[sheet].last = r;
    if (parent) m->rules[parent].count++;
    else m->sheets[sheet].count++;
}
/* a rule's sheet set in its subtree (0: taken out of its sheet; its parent links kept) */
static void om_set_sheet(css_om *m, uint32_t r, uint32_t sheet) {
    m->rules[r].sheet = sheet;
    for (uint32_t c = m->rules[r].first; c; c = m->rules[c].next) om_set_sheet(m, c, sheet);
}
static void om_unlink(css_om *m, uint32_t r) {
    uint32_t sheet = m->rules[r].sheet, parent = m->rules[r].parent, prev = m->rules[r].prev, next = m->rules[r].next;
    if (prev) m->rules[prev].next = next;
    else if (parent) m->rules[parent].first = next;
    else if (sheet) m->sheets[sheet].first = next;
    if (next) m->rules[next].prev = prev;
    else if (parent) m->rules[parent].last = prev;
    else if (sheet) m->sheets[sheet].last = prev;
    if (parent) m->rules[parent].count--;
    else if (sheet) m->sheets[sheet].count--;
    m->rules[r].prev = m->rules[r].next = 0;
    /* a rule taken out is no sheet's (its parentStyleSheet null), nor any rule's: its own rules keep it */
    m->rules[r].parent = 0;
    om_set_sheet(m, r, 0);
}

/* ---- selectors (Selectors 4, as Chromium's parser takes them) ---- */

/* the pseudo-classes and pseudo-elements Chromium knows: a name, a function's with "(" */
static const char *const om_pclasses[] = {
    "active", "active-view-transition", "any-link", "autofill", "buffering", "checked", "closed", "current", "default", "defined",
    "disabled", "empty", "enabled", "first", "first-child", "first-of-type", "focus", "focus-visible", "focus-within", "fullscreen",
    "future", "has-slotted", "host", "hover", "in-range", "indeterminate", "invalid", "last-child", "last-of-type", "left", "link",
    "modal", "muted", "only-child", "only-of-type", "open", "optional", "out-of-range", "past", "paused", "picture-in-picture",
    "placeholder-shown", "playing", "popover-open", "read-only", "read-write", "required", "right", "root", "scope", "seeking",
    "stalled", "target", "target-current", "user-invalid", "user-valid", "valid", "visited", "volume-locked", "xr-overlay",
    "-webkit-any-link", "-webkit-autofill", "-webkit-drag", "-webkit-full-screen", "-webkit-full-screen-ancestor",
    "-webkit-full-screen-document", "-webkit-full-screen-controls-hidden", "window-inactive", "horizontal", "vertical",
    "decrement", "increment", "start", "end", "double-button", "single-button", "no-button", "corner-present",
    "dir(", "has(", "host(", "host-context(", "is(", "lang(", "not(", "nth-child(", "nth-last-child(", "nth-last-of-type(",
    "nth-of-type(", "state(", "where(", "-webkit-any(", "active-view-transition-type(", 0 };
static const char *const om_pelements[] = {
    "after", "before", "backdrop", "checkmark", "column", "cue", "details-content", "file-selector-button", "first-letter",
    "first-line", "grammar-error", "marker", "picker-icon", "placeholder", "scroll-marker", "scroll-marker-group", "selection",
    "spelling-error", "target-text", "view-transition", "cue(", "highlight(", "part(", "picker(", "scroll-button(", "slotted(",
    "view-transition-group(", "view-transition-image-pair(", "view-transition-new(", "view-transition-old(", 0 };
/* the scrollbar's pseudo-elements and the pseudo-classes they take */
static const char *const om_scroll_pcs[] = { "hover", "active", "horizontal", "vertical", "decrement", "increment", "start", "end",
                                             "double-button", "single-button", "no-button", "corner-present", "window-inactive",
                                             "enabled", "disabled", 0 };
static int om_known(const char *const *list, const char *name, size_t n, int fn) {
    for (size_t k = 0; list[k]; k++) {
        size_t l = strlen(list[k]);
        int f = l && list[k][l - 1] == '(';
        if (f != fn) continue;
        if (f) l--;
        if (l == n && memcmp(list[k], name, n) == 0) return 1;
    }
    return 0;
}

/* the selector parser's state: the text, its tokens and the sheet whose @namespace prefixes
   it may use */
typedef struct om_sel {
    uint32_t sheet;
    int nested;        /* a nested rule's selector: relative, an & given where none is */
    int in_has;        /* inside a :has(), where another :has() is none */
} om_sel;

static int om_sel_list(const css_om *m, const om_sel *sc, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int relative, int forgiving,
                       int depth, vector<char> *out);

/* a namespace prefix the sheet declared */
static int om_ns_declared(const css_om *m, const om_sel *sc, const char *pre, size_t n) {
    if (!m || !sc->sheet || sc->sheet >= len(m->sheets)) return 0;
    for (uint32_t r = m->sheets[sc->sheet].first; r; r = m->rules[r].next) {
        css_om_rule x = m->rules[r];
        if (x.kind != CSS_OM_NAMESPACE) continue;
        if (x.alen == n && memcmp(data(m->text) + x.aoff, pre, n) == 0) return 1;
    }
    return 0;
}

/* An+B (CSS Syntax 3, 6.2) from the tokens [a, b): 1 and a, b in *pa, *pb */
static int om_anb(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, long *pa, long *pb) {
    vector<char> s;
    for (uint32_t i = a; i < b; i++) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS) continue;
        if (t.t != OT_IDENT && t.t != OT_NUM && t.t != OT_DIM && t.t != OT_DELIM) return 0;
        css_omx_lower(p + t.s, t.e - t.s, &s);
    }
    push_back(s, 0);
    const char *c = data(s);
    if (!strcmp(c, "odd")) { *pa = 2; *pb = 1; return 1; }
    if (!strcmp(c, "even")) { *pa = 2; *pb = 0; return 1; }
    size_t i = 0, n = strlen(c);
    long sign = 1, A = 0, B = 0;
    int hasn = 0, digits = 0;
    if (c[i] == '+' || c[i] == '-') { if (c[i] == '-') sign = -1; i++; }
    while (i < n && om_digit(c[i])) { A = A * 10 + (c[i] - '0'); i++; digits = 1; if (A > 1000000000L) return 0; }
    if (i < n && c[i] == 'n') {
        hasn = 1;
        i++;
        if (!digits) A = 1;
        A *= sign;
        if (i < n) {
            long s2 = 1;
            if (c[i] == '+') i++;
            else if (c[i] == '-') { s2 = -1; i++; }
            else return 0;
            int d2 = 0;
            while (i < n && om_digit(c[i])) { B = B * 10 + (c[i] - '0'); i++; d2 = 1; if (B > 1000000000L) return 0; }
            if (!d2 || i < n) return 0;
            B *= s2;
        }
    } else {
        if (!digits || i < n) return 0;
        B = A * sign;
        A = 0;
    }
    (void)hasn;
    *pa = A;
    *pb = B;
    return 1;
}
static void om_anb_text(long a, long b, vector<char> *out) {
    char buf[64];
    int k;
    if (a == 0) k = snprintf(buf, sizeof buf, "%ld", b);
    else {
        const char *as = a == 1 ? "" : a == -1 ? "-" : 0;
        char an[32];
        if (as) snprintf(an, sizeof an, "%s", as);
        else snprintf(an, sizeof an, "%ld", a);
        if (b > 0) k = snprintf(buf, sizeof buf, "%sn+%ld", an, b);
        else if (b < 0) k = snprintf(buf, sizeof buf, "%sn%ld", an, b);
        else k = snprintf(buf, sizeof buf, "%sn", an);
    }
    if (k > 0) append_range(*out, buf, (size_t)k);
}

/* an attribute selector's tokens inside its brackets [a, b) */
static int om_attr_sel(const css_om *m, const om_sel *sc, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    vector<char> name, pre;
    int has_pre = 0;
    /* [ns|name], [*|name], [|name] */
    if (i < b && (*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '|') { has_pre = 1; i++; }
    else if (i + 1 < b && (*tk)[i + 1].t == OT_DELIM && p[(*tk)[i + 1].s] == '|' && !(i + 2 < b && (*tk)[i + 2].t == OT_DELIM && p[(*tk)[i + 2].s] == '=') &&
             ((*tk)[i].t == OT_IDENT || ((*tk)[i].t == OT_DELIM && p[(*tk)[i].s] == '*'))) {
        css_omt t = (*tk)[i];
        if (t.t == OT_IDENT) {
            css_omt_text(p, &t, &pre);
            if (!om_ns_declared(m, sc, data(pre), len(pre))) return 0;
        } else push_back(pre, '*');
        has_pre = 1;
        i += 2;
    }
    if (i >= b || (*tk)[i].t != OT_IDENT) return 0;
    css_omt nt = (*tk)[i];
    css_omt_text(p, &nt, &name);
    i = css_omt_skip(tk, i + 1, b);
    push_back(*out, '[');
    if (has_pre) { append_range(*out, data(pre), len(pre)); push_back(*out, '|'); }
    vector<char> low;
    css_omx_lower(data(name), len(name), &low);
    css_omx_ident(data(low), len(low), out);
    if (i >= b) { push_back(*out, ']'); return 1; }
    /* the operator */
    char op[3] = { 0, 0, 0 };
    css_omt t = (*tk)[i];
    if (t.t != OT_DELIM) return 0;
    char c = p[t.s];
    if (c == '=') { op[0] = '='; i++; }
    else if ((c == '~' || c == '|' || c == '^' || c == '$' || c == '*') && i + 1 < b && (*tk)[i + 1].t == OT_DELIM && p[(*tk)[i + 1].s] == '=') {
        op[0] = c;
        op[1] = '=';
        i += 2;
    } else return 0;
    i = css_omt_skip(tk, i, b);
    if (i >= b) return 0;
    css_omt v = (*tk)[i];
    if (v.t != OT_IDENT && v.t != OT_STR) return 0;
    vector<char> val;
    css_omt_text(p, &v, &val);
    i = css_omt_skip(tk, i + 1, b);
    int ci = 0;
    if (i < b) {
        css_omt f = (*tk)[i];
        if (f.t != OT_IDENT || !css_omt_is(p, &f, "i")) return 0;
        ci = 1;
        i = css_omt_skip(tk, i + 1, b);
        if (i < b) return 0;
    }
    append_range(*out, op, strlen(op));
    css_omx_string(data(val), len(val), out);
    if (ci) append_range(*out, " i", 2);
    push_back(*out, ']');
    return 1;
}

/* a compound selector's tokens from *ip (no white space in it); its text appended to out. kind:
   what its last pseudo-element is (1 before/after-like that takes nothing after it, 2 a
   scrollbar's, 3 ::part() and the others that take user-action pseudo-classes); 0 when it is
   no compound */
static int om_compound(const css_om *m, const om_sel *sc, const char *p, const vector<css_omt> *tk, uint32_t *ip, uint32_t b, int depth, int *amp, vector<char> *out) {
    uint32_t i = *ip;
    size_t start = len(*out);
    int any = 0, pe = 0, univ_only = 0;
    size_t univ_at = 0;
    /* a type selector, with its namespace */
    {
        uint32_t k = i;
        vector<char> pre;
        int has_pre = 0, pre_star = 0, pre_empty = 0;
        if (k + 1 < b && (*tk)[k + 1].t == OT_DELIM && p[(*tk)[k + 1].s] == '|' &&
            ((*tk)[k].t == OT_IDENT || ((*tk)[k].t == OT_DELIM && p[(*tk)[k].s] == '*'))) {
            if ((*tk)[k].t == OT_IDENT) { css_omt t0 = (*tk)[k]; css_omt_text(p, &t0, &pre); }
            else pre_star = 1;
            has_pre = 1;
            k += 2;
        } else if (k < b && (*tk)[k].t == OT_DELIM && p[(*tk)[k].s] == '|') {
            has_pre = 1;
            pre_empty = 1;
            k++;
        }
        if (k < b && ((*tk)[k].t == OT_IDENT || ((*tk)[k].t == OT_DELIM && p[(*tk)[k].s] == '*'))) {
            if (has_pre && !pre_star && !pre_empty && !om_ns_declared(m, sc, data(pre), len(pre))) return 0;
            if (has_pre && !pre_star) {
                if (!pre_empty) css_omx_ident(data(pre), len(pre), out);
                push_back(*out, '|');
            }
            css_omt t = (*tk)[k];
            if (t.t == OT_IDENT) {
                vector<char> nm, low;
                css_omt_text(p, &t, &nm);
                css_omx_lower(data(nm), len(nm), &low);
                css_omx_ident(data(low), len(low), out);
            } else {
                push_back(*out, '*');
                if (!has_pre || pre_star) { univ_only = 1; univ_at = len(*out) - 1; }
            }
            any = 1;
            i = k + 1;
        } else if (has_pre) return 0;
    }
    while (i < b) {
        css_omt t = (*tk)[i];
        if (t.t == OT_HASH) {
            if (pe || !(t.f & OTF_ID)) return 0;
            vector<char> v;
            css_omt_text(p, &t, &v);
            push_back(*out, '#');
            css_omx_ident(data(v), len(v), out);
            i++;
            any = 1;
            univ_only = univ_only ? 2 : 0;
            continue;
        }
        if (t.t == OT_DELIM && p[t.s] == '.') {
            if (pe || i + 1 >= b || (*tk)[i + 1].t != OT_IDENT) return 0;
            css_omt c = (*tk)[i + 1];
            vector<char> v;
            css_omt_text(p, &c, &v);
            push_back(*out, '.');
            css_omx_ident(data(v), len(v), out);
            i += 2;
            any = 1;
            univ_only = univ_only ? 2 : 0;
            continue;
        }
        if (t.t == OT_DELIM && p[t.s] == '&') {
            if (pe) return 0;
            push_back(*out, '&');
            *amp = 1;
            i++;
            any = 1;
            univ_only = univ_only ? 2 : 0;
            continue;
        }
        if (t.t == OT_LBRACK) {
            if (pe || t.m >= b) return 0;
            if (!om_attr_sel(m, sc, p, tk, i + 1, t.m, out)) return 0;
            i = t.m + 1;
            any = 1;
            univ_only = univ_only ? 2 : 0;
            continue;
        }
        if (t.t == OT_COLON) {
            int el = i + 1 < b && (*tk)[i + 1].t == OT_COLON;
            uint32_t k = i + (el ? 2 : 1);
            if (k >= b || ((*tk)[k].t != OT_IDENT && (*tk)[k].t != OT_FUNC)) return 0;
            css_omt nt = (*tk)[k];
            int fn = nt.t == OT_FUNC;
            vector<char> nm, low;
            css_omt_text(p, &nt, &nm);
            css_omx_lower(data(nm), len(nm), &low);
            const char *ln = data(low);
            size_t ll = len(low);
            int legacy = !el && !fn && (css_omx_eq(ln, ll, "before", 6) || css_omx_eq(ln, ll, "after", 5) || css_omx_eq(ln, ll, "first-line", 10) ||
                                         css_omx_eq(ln, ll, "first-letter", 12));
            int webkit_el = (el || legacy) && ll > 8 && memcmp(ln, "-webkit-", 8) == 0 && !fn;
            if (el || legacy) {
                /* a pseudo-element: known (any -webkit- one Chromium keeps), at most one but ::part()'s and ::slotted()'s own */
                if (!webkit_el && !om_known(om_pelements, ln, ll, fn)) return 0;
                if (pe && pe != 3) return 0;
                if (pe == 3 && !(css_omx_eq(ln, ll, "before", 6) || css_omx_eq(ln, ll, "after", 5) || css_omx_eq(ln, ll, "placeholder", 11) ||
                                 css_omx_eq(ln, ll, "marker", 6) || css_omx_eq(ln, ll, "selection", 9) || css_omx_eq(ln, ll, "file-selector-button", 20)))
                    return 0;
                append_range(*out, "::", 2);
                css_omx_ident(ln, ll, out);
                if (fn) {
                    uint32_t e = nt.m;
                    if (e >= b) return 0;
                    push_back(*out, '(');
                    if (css_omx_eq(ln, ll, "slotted", 7)) {
                        /* a compound selector */
                        uint32_t a2 = css_omt_skip(tk, k + 1, e), j = a2;
                        int amp2 = 0;
                        if (a2 >= e || !om_compound(m, sc, p, tk, &j, e, depth + 1, &amp2, out)) return 0;
                        if (css_omt_skip(tk, j, e) != e) return 0;
                    } else if (css_omx_eq(ln, ll, "part", 4) || css_omx_eq(ln, ll, "highlight", 9) || css_omx_eq(ln, ll, "picker", 6) ||
                               css_omx_eq(ln, ll, "view-transition-group", 21) || css_omx_eq(ln, ll, "view-transition-image-pair", 26) ||
                               css_omx_eq(ln, ll, "view-transition-new", 19) || css_omx_eq(ln, ll, "view-transition-old", 19)) {
                        /* idents, one a space apart */
                        int nid = 0;
                        for (uint32_t j = k + 1; j < e; j++) {
                            css_omt it = (*tk)[j];
                            if (it.t == OT_WS) continue;
                            if (it.t != OT_IDENT && !(it.t == OT_DELIM && p[it.s] == '*')) return 0;
                            if (nid++) push_back(*out, ' ');
                            if (it.t == OT_DELIM) push_back(*out, '*');
                            else { vector<char> v; css_omt_text(p, &it, &v); css_omx_ident(data(v), len(v), out); }
                        }
                        if (!nid) return 0;
                    } else if (css_omx_eq(ln, ll, "cue", 3)) {
                        if (!om_sel_list(m, sc, p, tk, k + 1, e, 0, 0, depth + 1, out)) return 0;
                    } else css_omx_tokens(p, tk, k + 1, e, 0, out);
                    push_back(*out, ')');
                    i = e + 1;
                } else i = k + 1;
                pe = css_omx_eq(ln, ll, "part", 4) ? 3 : webkit_el && ll > 18 && memcmp(ln, "-webkit-scrollbar", 17) == 0 ? 2 :
                     css_omx_eq(ln, ll, "-webkit-resizer", 15) ? 2 : css_omx_eq(ln, ll, "selection", 9) ? 4 :
                     css_omx_eq(ln, ll, "slotted", 7) ? 3 : 1;
                if (css_omx_eq(ln, ll, "-webkit-scrollbar", 17)) pe = 2;
                any = 1;
                univ_only = univ_only ? 2 : 0;
                continue;
            }
            /* a pseudo-class */
            if (!om_known(om_pclasses, ln, ll, fn)) return 0;
            if (pe == 1) return 0;
            if (pe == 2 && (fn || !om_known(om_scroll_pcs, ln, ll, 0))) return 0;
            if (pe == 4 && !css_omx_eq(ln, ll, "window-inactive", 15)) return 0;
            if (pe == 3 && (css_omx_eq(ln, ll, "first-child", 11) || css_omx_eq(ln, ll, "last-child", 10) || fn)) {
                if (!(fn && (css_omx_eq(ln, ll, "not", 3) || css_omx_eq(ln, ll, "is", 2) || css_omx_eq(ln, ll, "where", 5)))) return 0;
            }
            push_back(*out, ':');
            css_omx_ident(ln, ll, out);
            if (fn) {
                uint32_t e = nt.m;
                if (e >= b) return 0;
                push_back(*out, '(');
                if (css_omx_eq(ln, ll, "not", 3)) {
                    if (css_omt_skip(tk, k + 1, e) == e || !om_sel_list(m, sc, p, tk, k + 1, e, 0, 0, depth + 1, out)) return 0;
                } else if (css_omx_eq(ln, ll, "is", 2) || css_omx_eq(ln, ll, "where", 5) || css_omx_eq(ln, ll, "-webkit-any", 11)) {
                    int forgiving = !css_omx_eq(ln, ll, "-webkit-any", 11);
                    if (!om_sel_list(m, sc, p, tk, k + 1, e, 0, forgiving, depth + 1, out)) return 0;
                } else if (css_omx_eq(ln, ll, "has", 3)) {
                    /* a :has() in a :has() is none (Selectors 4, 14.1) */
                    om_sel hc = *sc;
                    hc.in_has = 1;
                    if (sc->in_has || depth >= 64 || css_omt_skip(tk, k + 1, e) == e || !om_sel_list(m, &hc, p, tk, k + 1, e, 2, 0, depth + 1, out)) return 0;
                } else if (ll > 4 && memcmp(ln, "nth-", 4) == 0) {
                    /* An+B, and for nth-child and nth-last-child "of" a selector list */
                    uint32_t ofk = e;
                    for (uint32_t j = k + 1; j < e; j++) if ((*tk)[j].t == OT_IDENT && css_omt_is(p, &(*tk)[j], "of")) { ofk = j; break; }
                    long A, B;
                    if (!om_anb(p, tk, k + 1, ofk, &A, &B)) return 0;
                    om_anb_text(A, B, out);
                    if (ofk < e) {
                        if (!(css_omx_eq(ln, ll, "nth-child", 9) || css_omx_eq(ln, ll, "nth-last-child", 14))) return 0;
                        append_range(*out, " of ", 4);
                        if (!om_sel_list(m, sc, p, tk, ofk + 1, e, 0, 0, depth + 1, out)) return 0;
                    }
                } else if (css_omx_eq(ln, ll, "host", 4) || css_omx_eq(ln, ll, "host-context", 12)) {
                    uint32_t a2 = css_omt_skip(tk, k + 1, e), j = a2;
                    int amp2 = 0;
                    if (a2 >= e || !om_compound(m, sc, p, tk, &j, e, depth + 1, &amp2, out)) return 0;
                    if (css_omt_skip(tk, j, e) != e) return 0;
                } else if (css_omx_eq(ln, ll, "lang", 4) || css_omx_eq(ln, ll, "active-view-transition-type", 27)) {
                    /* idents, a comma apart */
                    int nid = 0, want = 1;
                    for (uint32_t j = k + 1; j < e; j++) {
                        css_omt it = (*tk)[j];
                        if (it.t == OT_WS) continue;
                        if (it.t == OT_COMMA) { if (want) return 0; want = 1; append_range(*out, ", ", 2); continue; }
                        if (it.t != OT_IDENT || !want) return 0;
                        vector<char> v;
                        css_omt_text(p, &it, &v);
                        css_omx_ident(data(v), len(v), out);
                        nid++;
                        want = 0;
                    }
                    if (!nid || want) return 0;
                } else if (css_omx_eq(ln, ll, "dir", 3) || css_omx_eq(ln, ll, "state", 5)) {
                    uint32_t j = css_omt_skip(tk, k + 1, e);
                    if (j >= e || (*tk)[j].t != OT_IDENT || css_omt_skip(tk, j + 1, e) != e) return 0;
                    css_omt it = (*tk)[j];
                    vector<char> v;
                    css_omt_text(p, &it, &v);
                    if (css_omx_eq(ln, ll, "dir", 3)) {
                        vector<char> lv;
                        css_omx_lower(data(v), len(v), &lv);
                        css_omx_ident(data(lv), len(lv), out);
                    } else css_omx_ident(data(v), len(v), out);
                } else css_omx_tokens(p, tk, k + 1, e, 0, out);
                push_back(*out, ')');
                i = e + 1;
            } else i = k + 1;
            any = 1;
            univ_only = univ_only ? 2 : 0;
            continue;
        }
        break;
    }
    if (!any) return 0;
    /* a universal selector before others of the compound is not written (Chromium's) */
    if (univ_only == 2) {
        size_t n = len(*out);
        for (size_t q = univ_at; q + 1 < n; q++) (*out)[q] = (*out)[q + 1];
        resize(*out, n - 1);
    }
    (void)start;
    *ip = i;
    return 1;
}

/* a complex selector [a, b): relative 1 a nested rule's (an & before it where it has none),
   2 :has()'s (a combinator may lead) */
static int om_complex(const css_om *m, const om_sel *sc, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int relative, int depth, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    vector<char> body;
    int amp = 0;
    char lead = 0;
    if (relative && (*tk)[i].t == OT_DELIM && (p[(*tk)[i].s] == '>' || p[(*tk)[i].s] == '+' || p[(*tk)[i].s] == '~')) {
        lead = p[(*tk)[i].s];
        i = css_omt_skip(tk, i + 1, b);
    }
    int first = 1;
    while (i < b) {
        if (!first) {
            /* a combinator: white space, > + ~ (with white space about them) */
            int ws = (*tk)[i].t == OT_WS;
            i = css_omt_skip(tk, i, b);
            if (i >= b) break;
            char comb = ' ';
            css_omt t = (*tk)[i];
            if (t.t == OT_DELIM && (p[t.s] == '>' || p[t.s] == '+' || p[t.s] == '~')) {
                comb = p[t.s];
                i = css_omt_skip(tk, i + 1, b);
                if (i >= b) return 0;
            } else if (!ws) return 0;
            if (comb == ' ') push_back(body, ' ');
            else { push_back(body, ' '); push_back(body, comb); push_back(body, ' '); }
        }
        if (!om_compound(m, sc, p, tk, &i, b, depth, &amp, &body)) return 0;
        first = 0;
        if (i < b && (*tk)[i].t != OT_WS && !((*tk)[i].t == OT_DELIM && (p[(*tk)[i].s] == '>' || p[(*tk)[i].s] == '+' || p[(*tk)[i].s] == '~'))) return 0;
    }
    if (relative == 2) {
        if (lead) { push_back(*out, lead); push_back(*out, ' '); }
    } else if (relative == 1 && (lead || !amp)) {
        append_range(*out, "& ", 2);
        if (lead) { push_back(*out, lead); push_back(*out, ' '); }
    } else if (lead) return 0;
    append_range(*out, data(body), len(body));
    return 1;
}

/* a selector list [a, b): its complex selectors a comma apart; forgiving (:is(), :where()) drops
   those that fail; an empty forgiving list is one */
static int om_sel_list(const css_om *m, const om_sel *sc, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int relative, int forgiving,
                       int depth, vector<char> *out) {
    if (depth > 32) return 0;
    uint32_t i = a;
    int n = 0;
    while (i <= b) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) {
            css_omt t = (*tk)[e];
            if ((t.t == OT_FUNC || t.t == OT_LPAREN || t.t == OT_LBRACK || t.t == OT_LBRACE) && t.m < b) e = t.m;
            e++;
        }
        vector<char> one;
        int ok = om_complex(m, sc, p, tk, i, e, relative, depth, &one);
        if (!ok && !forgiving) return 0;
        if (ok) {
            if (n++) append_range(*out, ", ", 2);
            append_range(*out, data(one), len(one));
        }
        if (e >= b) break;
        i = e + 1;
    }
    return n > 0 || forgiving;
}

/* a style rule's selector list as Chromium writes it, into out: 0 when it is none */
static int om_selector(const css_om *m, uint32_t sheet, int nested, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    om_sel sc;
    sc.sheet = sheet;
    sc.nested = nested;
    sc.in_has = 0;
    if (css_omt_skip(tk, a, b) >= b) return 0;
    return om_sel_list(m, &sc, p, tk, a, b, nested ? 1 : 0, 0, 0, out);
}

/* ---- media queries (Media Queries 5) and container conditions ---- */

/* a media feature's value [a, b) as Chromium writes it: a ratio a slash with spaces about it */
void css_omx_feature_value(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    /* a ratio: number / number */
    for (uint32_t k = i; k < b; k++) {
        css_omt t = (*tk)[k];
        if (t.t == OT_DELIM && p[t.s] == '/') {
            css_omx_tokens(p, tk, i, k, 1, out);
            append_range(*out, " / ", 3);
            css_omx_tokens(p, tk, k + 1, b, 1, out);
            return;
        }
    }
    /* a calc() of lengths alone made one */
    if (i < b && (*tk)[i].t == OT_FUNC && (*tk)[i].m + 1 == b) {
        vector<char> v;
        if (css_omv_value(css_om_prop("width", 5), p, tk, i, b, &v)) { append_range(*out, data(v), len(v)); return; }
    }
    css_omx_tokens(p, tk, i, b, 1, out);
}
static int om_cmp_op(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, char *op) {
    css_omt t = (*tk)[*i];
    if (t.t != OT_DELIM) return 0;
    char c = p[t.s];
    if (c != '<' && c != '>' && c != '=') return 0;
    op[0] = c;
    op[1] = 0;
    op[2] = 0;
    (*i)++;
    if (c != '=' && *i < b && (*tk)[*i].t == OT_DELIM && p[(*tk)[*i].s] == '=') { op[1] = '='; (*i)++; }
    return 1;
}
/* a media feature in parentheses [a, b) (inside them): 1 and its text, 0 when it is no feature */
static int om_feature(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    /* name, name: value, name op value */
    if ((*tk)[i].t == OT_IDENT) {
        uint32_t j = css_omt_skip(tk, i + 1, b);
        css_omt nt = (*tk)[i];
        vector<char> nm;
        css_omt_text(p, &nt, &nm);
        if (j >= b) { push_back(*out, '('); css_omx_lower(data(nm), len(nm), out); push_back(*out, ')'); return 1; }
        if ((*tk)[j].t == OT_COLON) {
            uint32_t v = css_omt_skip(tk, j + 1, b);
            if (v >= b) return 0;
            push_back(*out, '(');
            css_omx_lower(data(nm), len(nm), out);
            append_range(*out, ": ", 2);
            css_omx_feature_value(p, tk, v, b, out);
            push_back(*out, ')');
            return 1;
        }
        char op[3];
        uint32_t k = j;
        if (om_cmp_op(p, tk, &k, b, op)) {
            uint32_t v = css_omt_skip(tk, k, b);
            if (v >= b) return 0;
            push_back(*out, '(');
            css_omx_lower(data(nm), len(nm), out);
            push_back(*out, ' ');
            append_range(*out, op, strlen(op));
            push_back(*out, ' ');
            css_omx_feature_value(p, tk, v, b, out);
            push_back(*out, ')');
            return 1;
        }
        return 0;
    }
    /* value op name [op value] */
    uint32_t k = i;
    while (k < b && !((*tk)[k].t == OT_DELIM && (p[(*tk)[k].s] == '<' || p[(*tk)[k].s] == '>' || p[(*tk)[k].s] == '='))) k++;
    if (k >= b || k == i) return 0;
    char op1[3];
    uint32_t q = k;
    if (!om_cmp_op(p, tk, &q, b, op1)) return 0;
    q = css_omt_skip(tk, q, b);
    if (q >= b || (*tk)[q].t != OT_IDENT) return 0;
    css_omt nt = (*tk)[q];
    vector<char> nm;
    css_omt_text(p, &nt, &nm);
    push_back(*out, '(');
    css_omx_feature_value(p, tk, i, k, out);
    push_back(*out, ' ');
    append_range(*out, op1, strlen(op1));
    push_back(*out, ' ');
    css_omx_lower(data(nm), len(nm), out);
    q = css_omt_skip(tk, q + 1, b);
    if (q < b) {
        char op2[3];
        if (!om_cmp_op(p, tk, &q, b, op2)) return 0;
        q = css_omt_skip(tk, q, b);
        if (q >= b) return 0;
        push_back(*out, ' ');
        append_range(*out, op2, strlen(op2));
        push_back(*out, ' ');
        css_omx_feature_value(p, tk, q, b, out);
    }
    push_back(*out, ')');
    return 1;
}
static int om_mcond(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int allow_or, int container, vector<char> *out);
/* a condition's operand at *i: ( condition ), ( feature ), a general enclosed one, or (container
   queries) style() and scroll-state() */
static int om_min_parens(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b, int container, vector<char> *out) {
    css_omt t = (*tk)[*i];
    if (t.t == OT_LPAREN && t.m < b) {
        uint32_t e = t.m;
        vector<char> v;
        if (om_feature(p, tk, *i + 1, e, &v)) { append_range(*out, data(v), len(v)); *i = e + 1; return 1; }
        clear(v);
        if (om_mcond(p, tk, *i + 1, e, 1, container, &v)) {
            push_back(*out, '(');
            append_range(*out, data(v), len(v));
            push_back(*out, ')');
            *i = e + 1;
            return 1;
        }
        /* general enclosed: as written */
        css_omx_raw(p, tk, *i, e + 1, 0, out);
        *i = e + 1;
        return 1;
    }
    if (t.t == OT_FUNC && t.m < b) {
        vector<char> nm;
        css_omt_text(p, &t, &nm);
        if (container && css_omx_ieq(data(nm), len(nm), "style")) {
            om_cat(out, "style(");
            css_omx_raw(p, tk, *i + 1, t.m, 0, out);
            push_back(*out, ')');
        } else css_omx_raw(p, tk, *i, t.m + 1, 0, out);
        *i = t.m + 1;
        return 1;
    }
    /* a function the text ends inside (garbage(): general enclosed, closed by the end, written as
       it is written */
    if (t.t == OT_FUNC) {
        css_omx_raw(p, tk, *i, b, 0, out);
        *i = b;
        return 1;
    }
    return 0;
}
static int om_mcond(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int allow_or, int container, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    if ((*tk)[i].t == OT_IDENT && css_omt_is(p, &(*tk)[i], "not")) {
        i = css_omt_skip(tk, i + 1, b);
        if (i >= b) return 0;
        om_cat(out, "not ");
        if (!om_min_parens(p, tk, &i, b, container, out)) return 0;
        return css_omt_skip(tk, i, b) == b;
    }
    if (!om_min_parens(p, tk, &i, b, container, out)) return 0;
    int mode = 0;   /* 1 and, 2 or */
    for (;;) {
        i = css_omt_skip(tk, i, b);
        if (i >= b) return 1;
        css_omt t = (*tk)[i];
        int is_and = t.t == OT_IDENT && css_omt_is(p, &t, "and"), is_or = t.t == OT_IDENT && css_omt_is(p, &t, "or");
        if (!is_and && !is_or) return 0;
        if (is_or && !allow_or) return 0;
        int md = is_and ? 1 : 2;
        if (mode && md != mode) return 0;
        mode = md;
        i = css_omt_skip(tk, i + 1, b);
        if (i >= b) return 0;
        om_cat(out, is_and ? " and " : " or ");
        if (!om_min_parens(p, tk, &i, b, container, out)) return 0;
    }
}
/* one media query [a, b): 1 and its text, 0 when it is none (Chromium writes "not all") */
static int om_mquery(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    css_omt t = (*tk)[i];
    if (t.t == OT_IDENT) {
        vector<char> w, lw;
        css_omt_text(p, &t, &w);
        css_omx_lower(data(w), len(w), &lw);
        int restrictor = css_omx_eq(data(lw), len(lw), "not", 3) || css_omx_eq(data(lw), len(lw), "only", 4);
        uint32_t j = i;
        if (restrictor) {
            j = css_omt_skip(tk, i + 1, b);
            if (j < b && (*tk)[j].t == OT_LPAREN && css_omx_eq(data(lw), len(lw), "not", 3)) return om_mcond(p, tk, i, b, 1, 0, out);
            if (j >= b || (*tk)[j].t != OT_IDENT) return 0;
        }
        css_omt mt = (*tk)[j];
        vector<char> ty, lty;
        css_omt_text(p, &mt, &ty);
        css_omx_lower(data(ty), len(ty), &lty);
        if (css_omx_eq(data(lty), len(lty), "and", 3) || css_omx_eq(data(lty), len(lty), "or", 2) || css_omx_eq(data(lty), len(lty), "not", 3) ||
            css_omx_eq(data(lty), len(lty), "only", 4) || css_omx_eq(data(lty), len(lty), "layer", 5))
            return 0;
        if (restrictor) { append_range(*out, data(lw), len(lw)); push_back(*out, ' '); }
        css_omx_ident(data(lty), len(lty), out);
        uint32_t k = css_omt_skip(tk, j + 1, b);
        if (k >= b) return 1;
        if ((*tk)[k].t != OT_IDENT || !css_omt_is(p, &(*tk)[k], "and")) return 0;
        om_cat(out, " and ");
        return om_mcond(p, tk, k + 1, b, 0, 0, out);
    }
    return om_mcond(p, tk, i, b, 1, 0, out);
}
void css_omx_media(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return;
    int n = 0;
    while (i <= b) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) {
            css_omt t = (*tk)[e];
            if (t.t == OT_FUNC || t.t == OT_LPAREN || t.t == OT_LBRACK || t.t == OT_LBRACE) {
                /* a block the text ends inside holds the rest */
                if (t.m >= b) { e = b; break; }
                e = t.m;
            }
            e++;
        }
        if (n++) append_range(*out, ", ", 2);
        vector<char> q;
        if (om_mquery(p, tk, i, e, &q)) append_range(*out, data(q), len(q));
        else om_cat(out, "not all");
        if (e >= b) break;
        i = e + 1;
    }
}
void css_om_media_text(const char *t, size_t n, vector<char> *out) {
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    css_omx_media(t, &tk, 0, (uint32_t)len(tk) - 1, out);
}
/* the media list's queries as texts, each ended by a zero byte */
static uint32_t om_media_split(const char *t, size_t n, vector<char> *qs) {
    vector<char> all;
    css_om_media_text(t, n, &all);
    uint32_t k = 0;
    size_t i = 0;
    while (i < len(all)) {
        size_t e = i;
        int depth = 0;
        while (e < len(all) && !(depth == 0 && all[e] == ',')) {
            if (all[e] == '(') depth++;
            else if (all[e] == ')') depth--;
            e++;
        }
        append_range(*qs, data(all) + i, e - i);
        push_back(*qs, 0);
        k++;
        i = e + 2;
    }
    return k;
}
uint32_t css_om_media_count(const char *t, size_t n) {
    vector<char> qs;
    return om_media_split(t, n, &qs);
}
void css_om_media_item(const char *t, size_t n, uint32_t i, vector<char> *out) {
    clear(*out);
    vector<char> qs;
    uint32_t k = om_media_split(t, n, &qs);
    if (i >= k) return;
    size_t at = 0;
    for (uint32_t q = 0; q < i; q++) at += strlen(data(qs) + at) + 1;
    append_range(*out, data(qs) + at, strlen(data(qs) + at));
}
void css_om_media_append(const char *t, size_t n, const char *q, size_t qn, vector<char> *out) {
    clear(*out);
    vector<char> one, qs;
    css_om_media_text(q, qn, &one);
    uint32_t k = om_media_split(t, n, &qs);
    css_om_media_text(t, n, out);
    if (!len(one)) return;
    size_t at = 0;
    for (uint32_t i = 0; i < k; i++) {
        size_t l = strlen(data(qs) + at);
        if (css_omx_eq(data(qs) + at, l, data(one), len(one))) return;
        at += l + 1;
    }
    if (len(*out)) append_range(*out, ", ", 2);
    append_range(*out, data(one), len(one));
}
int css_om_media_delete(const char *t, size_t n, const char *q, size_t qn, vector<char> *out) {
    clear(*out);
    vector<char> one, qs;
    css_om_media_text(q, qn, &one);
    uint32_t k = om_media_split(t, n, &qs);
    int found = 0;
    size_t at = 0;
    for (uint32_t i = 0; i < k; i++) {
        size_t l = strlen(data(qs) + at);
        if (css_omx_eq(data(qs) + at, l, data(one), len(one))) found = 1;
        else {
            if (len(*out)) append_range(*out, ", ", 2);
            append_range(*out, data(qs) + at, l);
        }
        at += l + 1;
    }
    return found;
}

/* a container condition (CSS Conditional 5, 6.1): [name] [condition], into out; 0 when none.
   *name gets the name, *query the condition */
static int om_container(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out, vector<char> *name, vector<char> *query) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    /* a list of queries a comma apart (Chromium 153 reads several) */
    int n = 0;
    while (i <= b) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) {
            css_omt t = (*tk)[e];
            if ((t.t == OT_FUNC || t.t == OT_LPAREN) && t.m < b) e = t.m;
            e++;
        }
        uint32_t k = css_omt_skip(tk, i, e);
        vector<char> nm, cq;
        if (k < e && (*tk)[k].t == OT_IDENT && !css_omt_is(p, &(*tk)[k], "not") && !css_omt_is(p, &(*tk)[k], "and") && !css_omt_is(p, &(*tk)[k], "or")) {
            css_omt t = (*tk)[k];
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (css_omx_ieq(data(v), len(v), "none")) return 0;
            css_omx_ident(data(v), len(v), &nm);
            k = css_omt_skip(tk, k + 1, e);
        }
        if (k < e && !om_mcond(p, tk, k, e, 1, 1, &cq)) return 0;
        if (!len(nm) && !len(cq)) return 0;
        if (n++) append_range(*out, ", ", 2);
        append_range(*out, data(nm), len(nm));
        if (len(nm) && len(cq)) push_back(*out, ' ');
        append_range(*out, data(cq), len(cq));
        if (n == 1) { append_range(*name, data(nm), len(nm)); append_range(*query, data(cq), len(cq)); }
        if (e >= b) break;
        i = e + 1;
    }
    return n > 0;
}

/* @supports' condition (CSS Conditional 3, 2): whether [a, b) is one */
static int om_scond(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b);
static int om_sin_parens(const char *p, const vector<css_omt> *tk, uint32_t *i, uint32_t b) {
    css_omt t = (*tk)[*i];
    if (t.t == OT_LPAREN && t.m < b) {
        uint32_t e = t.m;
        uint32_t k = css_omt_skip(tk, *i + 1, e);
        /* a declaration, or a condition, or anything (general enclosed) */
        if (k < e && (*tk)[k].t == OT_IDENT) {
            uint32_t c = css_omt_skip(tk, k + 1, e);
            if (c < e && (*tk)[c].t == OT_COLON) { *i = e + 1; return 1; }
        }
        if (om_scond(p, tk, *i + 1, e)) { *i = e + 1; return 1; }
        *i = e + 1;
        return 1;
    }
    if (t.t == OT_FUNC && t.m < b) { *i = t.m + 1; return 1; }
    return 0;
}
static int om_scond(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    if ((*tk)[i].t == OT_IDENT && css_omt_is(p, &(*tk)[i], "not")) {
        uint32_t j = i + 1;
        if (j >= b || (*tk)[j].t != OT_WS) {
            /* "not(" is a function: general enclosed */
        }
        i = css_omt_skip(tk, i + 1, b);
        if (i >= b || !om_sin_parens(p, tk, &i, b)) return 0;
        return css_omt_skip(tk, i, b) == b;
    }
    if (!om_sin_parens(p, tk, &i, b)) return 0;
    int mode = 0;
    for (;;) {
        i = css_omt_skip(tk, i, b);
        if (i >= b) return 1;
        css_omt t = (*tk)[i];
        int is_and = t.t == OT_IDENT && css_omt_is(p, &t, "and"), is_or = t.t == OT_IDENT && css_omt_is(p, &t, "or");
        if (!is_and && !is_or) return 0;
        int md = is_and ? 1 : 2;
        if (mode && md != mode) return 0;
        mode = md;
        i = css_omt_skip(tk, i + 1, b);
        if (i >= b || !om_sin_parens(p, tk, &i, b)) return 0;
    }
}

/* ---- rules: parsing ---- */

/* what a list's place allows: the sheet's top level, a grouping rule's, a style rule's block */
enum { OM_TOP = 1, OM_GROUP, OM_NESTED };
/* the parse's state: the model, the sheet the rules are made in, the sheet whose @namespace
   prefixes the selectors may use, whether it is a constructed sheet's, the top level's order so
   far (0 imports and layer statements may come, 1 namespaces, 2 the rest) */
typedef struct om_ps {
    uint32_t sheet, ns, mode, phase;
} om_ps;
/* the declarations of part appended to dl's */
static void om_dl_append(css_om_dl *dl, const css_om_dl *part) {
    for (size_t z = 0; z < len(part->d); z++) {
        css_om_decl dd = part->d[z];
        uint32_t vo = (uint32_t)len(dl->t);
        append_range(dl->t, data(part->t) + dd.voff, dd.vlen);
        uint32_t no = (uint32_t)len(dl->t);
        append_range(dl->t, data(part->t) + dd.noff, dd.nlen);
        dd.voff = vo;
        dd.noff = no;
        push_back(dl->d, dd);
    }
}

static void om_rules(css_om *m, om_ps *ps, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t parent, uint32_t level, uint32_t in_style);

/* a token's end of an at-rule's prelude from i: the ; or { at its level, or b */
static uint32_t om_prelude_end(const vector<css_omt> *tk, uint32_t i, uint32_t b, int stop_semi) {
    while (i < b) {
        css_omt t = (*tk)[i];
        if (t.t == OT_LBRACE) return i;
        if (stop_semi && t.t == OT_SEMI) return i;
        if ((t.t == OT_FUNC || t.t == OT_LPAREN || t.t == OT_LBRACK) && t.m < b) i = t.m;
        i++;
    }
    return b;
}
/* a block's declarations [a, b) read into a block record of the kind given: its place */
static uint32_t om_block(css_om *m, om_ps *ps, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t kind, uint32_t rule) {
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    css_om_decls_tokens(&dl, p, tk, a, b, kind);
    uint32_t bl = css_om_block_new(m, kind, rule, &dl);
    CSS_OM_DL_DROP(dl);
    return bl;
}
/* the rule r appended to its list */
static void om_append(css_om *m, om_ps *ps, uint32_t parent, uint32_t r) { om_link(m, ps->sheet, parent, r, 0); }
static void om_set_a(css_om *m, om_ps *ps, uint32_t r, const vector<char> *t) {
    uint32_t off, n;
    om_put_v(m, t, &off, &n);
    m->rules[r].aoff = off;
    m->rules[r].alen = n;
}
static void om_set_b(css_om *m, om_ps *ps, uint32_t r, const vector<char> *t) {
    uint32_t off, n;
    om_put_v(m, t, &off, &n);
    m->rules[r].boff = off;
    m->rules[r].blen = n;
}
static void om_set_c(css_om *m, om_ps *ps, uint32_t r, const vector<char> *t) {
    uint32_t off, n;
    om_put_v(m, t, &off, &n);
    m->rules[r].coff = off;
    m->rules[r].clen = n;
}
static void om_set_d(css_om *m, om_ps *ps, uint32_t r, const vector<char> *t) {
    uint32_t off, n;
    om_put_v(m, t, &off, &n);
    m->rules[r].doff = off;
    m->rules[r].dlen = n;
}

/* a layer's name: idents a dot apart, no white space in it; into out */
static int om_layer_name(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    int want = 1;
    for (; i < b; i++) {
        css_omt t = (*tk)[i];
        if (want) {
            if (t.t != OT_IDENT) return 0;
            vector<char> v;
            css_omt_text(p, &t, &v);
            if (css_omx_ieq(data(v), len(v), "initial") || css_omx_ieq(data(v), len(v), "inherit") || css_omx_ieq(data(v), len(v), "unset") ||
                css_omx_ieq(data(v), len(v), "revert") || css_omx_ieq(data(v), len(v), "revert-layer"))
                return 0;
            css_omx_ident(data(v), len(v), out);
            want = 0;
            continue;
        }
        if (t.t == OT_DELIM && p[t.s] == '.') { push_back(*out, '.'); want = 1; continue; }
        return 0;
    }
    return !want;
}
/* keyframe keys (CSS Animations 1: from, to, percentages; Scroll-driven Animations' timeline
   ranges) as Chromium writes them, into out: 0 when one is none */
static int om_keys(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out) {
    uint32_t i = css_omt_skip(tk, a, b);
    while (b > i && (*tk)[b - 1].t == OT_WS) b--;
    if (i >= b) return 0;
    int n = 0;
    while (i <= b) {
        uint32_t e = i;
        while (e < b && (*tk)[e].t != OT_COMMA) e++;
        uint32_t k = css_omt_skip(tk, i, e), z = e;
        while (z > k && (*tk)[z - 1].t == OT_WS) z--;
        if (k >= z) return 0;
        if (n++) append_range(*out, ", ", 2);
        css_omt t = (*tk)[k];
        if (z == k + 1 && t.t == OT_IDENT && css_omt_is(p, &t, "from")) om_cat(out, "0%");
        else if (z == k + 1 && t.t == OT_IDENT && css_omt_is(p, &t, "to")) om_cat(out, "100%");
        else if (z == k + 1 && t.t == OT_PCT) {
            if (t.v < 0 || t.v > 100) return 0;
            css_omx_num(t.v, out);
            push_back(*out, '%');
        } else if (t.t == OT_IDENT) {
            /* a timeline range's name and its percentage */
            static const char *const ranges[] = { "normal", "cover", "contain", "entry", "exit", "entry-crossing", "exit-crossing", "scroll", 0 };
            int ok = 0;
            for (int r = 0; ranges[r]; r++) if (css_omt_is(p, &t, ranges[r])) ok = 1;
            uint32_t q = css_omt_skip(tk, k + 1, z);
            if (!ok || q + 1 != z || (*tk)[q].t != OT_PCT) return 0;
            vector<char> v;
            css_omt_text(p, &t, &v);
            css_omx_lower(data(v), len(v), out);
            push_back(*out, ' ');
            css_omx_num((*tk)[q].v, out);
            push_back(*out, '%');
        } else return 0;
        if (e >= b) break;
        i = e + 1;
    }
    return 1;
}

/* a rule whose prelude is [pa, pb) and whose block is [ba, bb) (ba 0: none, ended by a ;), a
   qualified rule (at 0) or an at-rule (at, its name's token's place plus one): made and
   appended to the list (parent, level); 0 when it is dropped */
static uint32_t om_rule(css_om *m, om_ps *ps, const char *p, const vector<css_omt> *tk, uint32_t at, uint32_t pa, uint32_t pb, int has_block, uint32_t ba, uint32_t bb,
                        uint32_t parent, uint32_t level, uint32_t in_style) {
    if (!at) {
        /* a style rule (a keyframe in @keyframes is read by its own) */
        vector<char> sel;
        if (!has_block || !om_selector(m, ps->ns, in_style != 0, p, tk, pa, pb, &sel)) return 0;
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, CSS_OM_STYLE);
        om_set_a(m, ps, r, &sel);
        om_append(m, ps, parent, r);
        om_rules(m, ps, p, tk, ba, bb, r, OM_NESTED, r);
        return r;
    }
    css_omt kt = (*tk)[at - 1];
    vector<char> name;
    css_omt_text(p, &kt, &name);
    vector<char> ln;
    css_omx_lower(data(name), len(name), &ln);
    push_back(ln, 0);
    const char *nm = data(ln);
    if (!strcmp(nm, "charset")) return 0;
    if (!strcmp(nm, "import")) {
        if (has_block || level != OM_TOP) return 0;
        if (ps->phase > 0) return 0;
        if (ps->mode & CSS_OM_READ_CONSTRUCTED) { m->warn++; return 0; }
        /* url: a string or url(); then layer or layer(name), supports(...), media */
        uint32_t i = css_omt_skip(tk, pa, pb);
        if (i >= pb) return 0;
        css_omt u = (*tk)[i];
        vector<char> url, lay, sup, med;
        if (u.t == OT_STR || u.t == OT_URL) css_omt_text(p, &u, &url);
        else if (u.t == OT_FUNC && css_omt_is(p, &u, "url") && u.m < pb) {
            uint32_t q = css_omt_skip(tk, i + 1, u.m);
            if (q >= u.m || (*tk)[q].t != OT_STR || css_omt_skip(tk, q + 1, u.m) != u.m) return 0;
            css_omt s = (*tk)[q];
            css_omt_text(p, &s, &url);
            i = u.m;
        } else return 0;
        i = css_omt_skip(tk, i + 1, pb);
        uint32_t flags = 0;
        if (i < pb && (*tk)[i].t == OT_IDENT && css_omt_is(p, &(*tk)[i], "layer")) { flags |= 1; i = css_omt_skip(tk, i + 1, pb); }
        else if (i < pb && (*tk)[i].t == OT_FUNC && css_omt_is(p, &(*tk)[i], "layer") && (*tk)[i].m < pb) {
            if (!om_layer_name(p, tk, i + 1, (*tk)[i].m, &lay)) return 0;
            flags |= 1;
            i = css_omt_skip(tk, (*tk)[i].m + 1, pb);
        }
        if (i < pb && (*tk)[i].t == OT_FUNC && css_omt_is(p, &(*tk)[i], "supports") && (*tk)[i].m < pb) {
            uint32_t e = (*tk)[i].m;
            uint32_t q = css_omt_skip(tk, i + 1, e);
            int decl = q < e && (*tk)[q].t == OT_IDENT && css_omt_skip(tk, q + 1, e) < e && (*tk)[css_omt_skip(tk, q + 1, e)].t == OT_COLON;
            if (!decl && !om_scond(p, tk, i + 1, e)) return 0;
            css_omx_raw(p, tk, i + 1, e, 1, &sup);
            flags |= 2;
            i = css_omt_skip(tk, e + 1, pb);
        }
        css_omx_media(p, tk, i, pb, &med);
        uint32_t r = om_rule_new(m, CSS_OM_IMPORT);
        om_set_a(m, ps, r, &url);
        om_set_b(m, ps, r, &med);
        om_set_c(m, ps, r, &lay);
        om_set_d(m, ps, r, &sup);
        m->rules[r].flags = flags;
        om_append(m, ps, parent, r);
        return r;
    }
    if (!strcmp(nm, "namespace")) {
        if (has_block || level != OM_TOP || ps->phase > 1) return 0;
        uint32_t i = css_omt_skip(tk, pa, pb);
        vector<char> pre, uri;
        if (i < pb && (*tk)[i].t == OT_IDENT) {
            css_omt t = (*tk)[i];
            css_omt_text(p, &t, &pre);
            i = css_omt_skip(tk, i + 1, pb);
        }
        if (i >= pb) return 0;
        css_omt u = (*tk)[i];
        if (u.t == OT_STR || u.t == OT_URL) css_omt_text(p, &u, &uri);
        else if (u.t == OT_FUNC && css_omt_is(p, &u, "url") && u.m < pb) {
            uint32_t q = css_omt_skip(tk, i + 1, u.m);
            if (q >= u.m || (*tk)[q].t != OT_STR) return 0;
            css_omt s = (*tk)[q];
            css_omt_text(p, &s, &uri);
            i = u.m;
        } else return 0;
        if (css_omt_skip(tk, i + 1, pb) != pb) return 0;
        ps->phase = 1;
        uint32_t r = om_rule_new(m, CSS_OM_NAMESPACE);
        om_set_a(m, ps, r, &pre);
        om_set_b(m, ps, r, &uri);
        om_append(m, ps, parent, r);
        return r;
    }
    if (!strcmp(nm, "layer")) {
        uint32_t i = css_omt_skip(tk, pa, pb);
        if (!has_block) {
            /* a statement: names a comma apart */
            vector<char> names;
            int n = 0;
            while (i < pb) {
                uint32_t e = i;
                while (e < pb && (*tk)[e].t != OT_COMMA) e++;
                vector<char> one;
                if (!om_layer_name(p, tk, i, e, &one)) return 0;
                if (n++) append_range(names, ", ", 2);
                append_range(names, data(one), len(one));
                if (e >= pb) break;
                i = e + 1;
                if (css_omt_skip(tk, i, pb) >= pb) return 0;
            }
            if (!n) return 0;
            if (level == OM_TOP && ps->phase == 1) ps->phase = 2;
            uint32_t r = om_rule_new(m, CSS_OM_LAYER_STATEMENT);
            om_set_a(m, ps, r, &names);
            om_append(m, ps, parent, r);
            return r;
        }
        vector<char> lname;
        if (i < pb && !om_layer_name(p, tk, i, pb, &lname)) return 0;
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, CSS_OM_LAYER_BLOCK);
        om_set_a(m, ps, r, &lname);
        om_append(m, ps, parent, r);
        om_rules(m, ps, p, tk, ba, bb, r, in_style ? OM_NESTED : OM_GROUP, in_style);
        return r;
    }
    if (!strcmp(nm, "media") || !strcmp(nm, "supports") || !strcmp(nm, "container") || !strcmp(nm, "scope") || !strcmp(nm, "starting-style")) {
        if (!has_block) return 0;
        uint32_t kind = nm[0] == 'm' ? CSS_OM_MEDIA : nm[1] == 'u' ? CSS_OM_SUPPORTS : nm[0] == 'c' ? CSS_OM_CONTAINER : nm[1] == 'c' ? CSS_OM_SCOPE : CSS_OM_STARTING_STYLE;
        vector<char> a1, b1, c1;
        uint32_t flags = 0;
        if (kind == CSS_OM_MEDIA) css_omx_media(p, tk, pa, pb, &a1);
        else if (kind == CSS_OM_SUPPORTS) {
            if (!om_scond(p, tk, pa, pb)) return 0;
            css_omx_raw(p, tk, pa, pb, 1, &a1);
        } else if (kind == CSS_OM_CONTAINER) {
            if (!om_container(p, tk, pa, pb, &a1, &b1, &c1)) return 0;
        } else if (kind == CSS_OM_SCOPE) {
            /* (start)? [to (end)]? */
            uint32_t i = css_omt_skip(tk, pa, pb);
            if (i < pb && (*tk)[i].t == OT_LPAREN && (*tk)[i].m < pb) {
                if (!om_selector(m, ps->ns, 0, p, tk, i + 1, (*tk)[i].m, &a1)) return 0;
                flags |= 1;
                i = css_omt_skip(tk, (*tk)[i].m + 1, pb);
            }
            if (i < pb && (*tk)[i].t == OT_IDENT && css_omt_is(p, &(*tk)[i], "to")) {
                i = css_omt_skip(tk, i + 1, pb);
                if (i >= pb || (*tk)[i].t != OT_LPAREN || (*tk)[i].m >= pb) return 0;
                if (!om_selector(m, ps->ns, 0, p, tk, i + 1, (*tk)[i].m, &b1)) return 0;
                flags |= 2;
                i = css_omt_skip(tk, (*tk)[i].m + 1, pb);
            }
            if (i < pb) return 0;
        } else if (css_omt_skip(tk, pa, pb) < pb) return 0;
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, kind);
        om_set_a(m, ps, r, &a1);
        om_set_b(m, ps, r, &b1);
        om_set_c(m, ps, r, &c1);
        m->rules[r].flags = flags;
        om_append(m, ps, parent, r);
        om_rules(m, ps, p, tk, ba, bb, r, in_style ? OM_NESTED : OM_GROUP, in_style);
        return r;
    }
    if (!strcmp(nm, "keyframes") || !strcmp(nm, "-webkit-keyframes")) {
        if (!has_block || in_style) return 0;
        uint32_t i = css_omt_skip(tk, pa, pb);
        if (i >= pb || css_omt_skip(tk, i + 1, pb) != pb) return 0;
        css_omt t = (*tk)[i];
        vector<char> kname;
        if (t.t == OT_IDENT) {
            css_omt_text(p, &t, &kname);
            if (css_omx_ieq(data(kname), len(kname), "none") || css_omx_ieq(data(kname), len(kname), "initial") ||
                css_omx_ieq(data(kname), len(kname), "inherit") || css_omx_ieq(data(kname), len(kname), "unset") ||
                css_omx_ieq(data(kname), len(kname), "default") || css_omx_ieq(data(kname), len(kname), "revert") ||
                css_omx_ieq(data(kname), len(kname), "revert-layer"))
                return 0;
        } else if (t.t == OT_STR) css_omt_text(p, &t, &kname);
        else return 0;
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, CSS_OM_KEYFRAMES);
        om_set_a(m, ps, r, &kname);
        m->rules[r].flags = nm[0] == '-' ? 1 : 0;
        om_append(m, ps, parent, r);
        /* its keyframes: key lists and declaration blocks */
        uint32_t k = ba;
        while (k < bb) {
            css_omt q = (*tk)[k];
            if (q.t == OT_WS || q.t == OT_SEMI) { k++; continue; }
            uint32_t e = om_prelude_end(tk, k, bb, 0);
            if (e >= bb) break;
            uint32_t be = (*tk)[e].m < bb ? (*tk)[e].m : bb;
            vector<char> keys;
            if (om_keys(p, tk, k, e, &keys)) {
                uint32_t kr = om_rule_new(m, CSS_OM_KEYFRAME);
                om_set_a(m, ps, kr, &keys);
                om_append(m, ps, r, kr);
                m->rules[kr].block = om_block(m, ps, p, tk, e + 1, be, CSS_OM_B_KEYFRAME, kr);
            }
            k = be + 1;
        }
        return r;
    }
    /* the at-rules of descriptors */
    static const char *const dnames[] = { "font-face", "page", "property", "counter-style", "font-palette-values", "view-transition", "position-try", 0 };
    static const uint32_t dkinds[] = { CSS_OM_FONT_FACE, CSS_OM_PAGE, CSS_OM_PROPERTY, CSS_OM_COUNTER_STYLE, CSS_OM_FONT_PALETTE_VALUES,
                                       CSS_OM_VIEW_TRANSITION, CSS_OM_POSITION_TRY };
    static const uint32_t dblocks[] = { CSS_OM_B_FONT_FACE, CSS_OM_B_PAGE, CSS_OM_B_PROPERTY, CSS_OM_B_COUNTER, CSS_OM_B_PALETTE, CSS_OM_B_VIEW,
                                        CSS_OM_B_TRY };
    for (int d = 0; dnames[d]; d++) {
        if (strcmp(nm, dnames[d])) continue;
        if (!has_block || in_style) return 0;
        uint32_t kind = dkinds[d];
        vector<char> a1;
        uint32_t i = css_omt_skip(tk, pa, pb);
        if (kind == CSS_OM_FONT_FACE || kind == CSS_OM_VIEW_TRANSITION) {
            if (i < pb) return 0;
        } else if (kind == CSS_OM_PAGE) {
            /* [name] [:first|:left|:right|:blank]* , one a comma apart */
            int n = 0;
            while (i < pb) {
                uint32_t e = i;
                while (e < pb && (*tk)[e].t != OT_COMMA) e++;
                uint32_t q = css_omt_skip(tk, i, e);
                if (q >= e) return 0;
                if (n++) append_range(a1, ", ", 2);
                if ((*tk)[q].t == OT_IDENT) {
                    css_omt t = (*tk)[q];
                    vector<char> v;
                    css_omt_text(p, &t, &v);
                    css_omx_ident(data(v), len(v), &a1);
                    q++;
                }
                int pcs = 0;
                while (q < e && (*tk)[q].t == OT_COLON) {
                    if (q + 1 >= e || (*tk)[q + 1].t != OT_IDENT) return 0;
                    css_omt t = (*tk)[q + 1];
                    vector<char> v, lv;
                    css_omt_text(p, &t, &v);
                    css_omx_lower(data(v), len(v), &lv);
                    if (!(css_omx_eq(data(lv), len(lv), "first", 5) || css_omx_eq(data(lv), len(lv), "left", 4) || css_omx_eq(data(lv), len(lv), "right", 5) ||
                          css_omx_eq(data(lv), len(lv), "blank", 5)))
                        return 0;
                    if (++pcs > 1) return 0;
                    push_back(a1, ':');
                    append_range(a1, data(lv), len(lv));
                    q += 2;
                }
                if (css_omt_skip(tk, q, e) != e) return 0;
                if (e >= pb) break;
                i = e + 1;
            }
        } else {
            /* a name: @property's a custom property's, @counter-style's an ident (not none), the
               others' a dashed ident */
            if (i >= pb || (*tk)[i].t != OT_IDENT || css_omt_skip(tk, i + 1, pb) != pb) return 0;
            css_omt t = (*tk)[i];
            vector<char> v;
            css_omt_text(p, &t, &v);
            int dashed = len(v) > 2 && v[0] == '-' && v[1] == '-';
            if (kind == CSS_OM_COUNTER_STYLE) {
                if (css_omx_ieq(data(v), len(v), "none") || css_omx_ieq(data(v), len(v), "initial") || css_omx_ieq(data(v), len(v), "inherit") ||
                    css_omx_ieq(data(v), len(v), "unset") || css_omx_ieq(data(v), len(v), "default") || css_omx_ieq(data(v), len(v), "revert") ||
                    css_omx_ieq(data(v), len(v), "revert-layer") || css_omx_ieq(data(v), len(v), "decimal") || css_omx_ieq(data(v), len(v), "disc") ||
                    css_omx_ieq(data(v), len(v), "square") || css_omx_ieq(data(v), len(v), "circle") || css_omx_ieq(data(v), len(v), "disclosure-open") ||
                    css_omx_ieq(data(v), len(v), "disclosure-closed"))
                    return 0;
            } else if (!dashed) return 0;
            css_omx_ident(data(v), len(v), &a1);
        }
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, kind);
        om_set_a(m, ps, r, &a1);
        om_append(m, ps, parent, r);
        if (kind == CSS_OM_PAGE) {
            /* its declarations, and its margin rules (@top-left ...) */
            uint32_t k = ba, ds = ba;
            css_om_dl dl;
            memset(&dl, 0, sizeof dl);
            while (k < bb) {
                css_omt q = (*tk)[k];
                if (q.t == OT_AT) {
                    if (k > ds) {
                        css_om_dl part;
                        memset(&part, 0, sizeof part);
                        css_om_decls_tokens(&part, p, tk, ds, k, CSS_OM_B_PAGE);
                        om_dl_append(&dl, &part);
                        CSS_OM_DL_DROP(part);
                    }
                    uint32_t e = om_prelude_end(tk, k + 1, bb, 1);
                    if (e < bb && (*tk)[e].t == OT_LBRACE) {
                        uint32_t be = (*tk)[e].m < bb ? (*tk)[e].m : bb;
                        static const char *const margins[] = { "top-left-corner", "top-left", "top-center", "top-right", "top-right-corner",
                                                               "bottom-left-corner", "bottom-left", "bottom-center", "bottom-right", "bottom-right-corner",
                                                               "left-top", "left-middle", "left-bottom", "right-top", "right-middle", "right-bottom", 0 };
                        vector<char> mn, lmn;
                        css_omt mt = (*tk)[k];
                        css_omt_text(p, &mt, &mn);
                        css_omx_lower(data(mn), len(mn), &lmn);
                        int ok = css_omt_skip(tk, k + 1, e) == e;
                        int known = 0;
                        for (int z = 0; margins[z]; z++) if (css_omx_eq(data(lmn), len(lmn), margins[z], strlen(margins[z]))) known = 1;
                        if (ok && known) {
                            uint32_t mr = om_rule_new(m, CSS_OM_MARGIN);
                            om_set_a(m, ps, mr, &lmn);
                            om_append(m, ps, r, mr);
                            m->rules[mr].block = om_block(m, ps, p, tk, e + 1, be, CSS_OM_B_PAGE, mr);
                        }
                        k = be + 1;
                    } else k = e + 1;
                    ds = k;
                    continue;
                }
                if ((q.t == OT_FUNC || q.t == OT_LPAREN || q.t == OT_LBRACK || q.t == OT_LBRACE) && q.m < bb) k = q.m;
                k++;
            }
            if (bb > ds) {
                css_om_dl part;
                memset(&part, 0, sizeof part);
                css_om_decls_tokens(&part, p, tk, ds, bb, CSS_OM_B_PAGE);
                om_dl_append(&dl, &part);
                CSS_OM_DL_DROP(part);
            }
            m->rules[r].block = css_om_block_new(m, CSS_OM_B_PAGE, r, &dl);
            CSS_OM_DL_DROP(dl);
        } else {
            uint32_t bl = om_block(m, ps, p, tk, ba, bb, dblocks[d], r);
            m->rules[r].block = bl;
            if (kind == CSS_OM_PROPERTY) {
                /* @property needs syntax and inherits, and an initial value but for syntax "*" */
                css_om_dl dl;
                memset(&dl, 0, sizeof dl);
                css_om_block_load(m, bl, &dl);
                vector<char> v;
                int has_syntax = 0, has_inherits = 0, has_initial = 0, universal = 0;
                for (size_t z = 0; z < len(dl.d); z++) {
                    char dn[64];
                    snprintf(dn, sizeof dn, "%s", css_om_desc_name(dl.d[z].prop));
                    if (!strcmp(dn, "syntax")) {
                        has_syntax = 1;
                        universal = dl.d[z].vlen == 3 && memcmp(data(dl.t) + dl.d[z].voff, "\"*\"", 3) == 0;
                    } else if (!strcmp(dn, "inherits")) has_inherits = 1;
                    else if (!strcmp(dn, "initial-value")) has_initial = 1;
                }
                CSS_OM_DL_DROP(dl);
                (void)v;
                if (!has_syntax || !has_inherits || (!universal && !has_initial)) {
                    om_unlink(m, r);
                    m->rules[r].live = 0;
                    return 0;
                }
            }
        }
        return r;
    }
    if (!strcmp(nm, "font-feature-values")) {
        if (!has_block || in_style) return 0;
        /* family names a comma apart; its feature blocks merged by type, each name's values
           integers (the last given kept), written as Chromium writes them: the types in its
           order, each type's names sorted */
        vector<char> fam;
        uint32_t i = css_omt_skip(tk, pa, pb);
        if (i >= pb) return 0;
        css_omx_tokens(p, tk, pa, pb, 0, &fam);
        static const char *const ffv_types[] = { "annotation", "ornaments", "stylistic", "swash", "character-variant", "styleset", 0 };
        /* five numbers an entry: its type, its name's and its values' spans of pool */
        vector<uint32_t> ents;
        vector<char> pool;
        uint32_t k = ba;
        while (k < bb) {
            css_omt q = (*tk)[k];
            if (q.t == OT_WS || q.t == OT_SEMI) { k++; continue; }
            if (q.t != OT_AT) { k = om_prelude_end(tk, k, bb, 1) + 1; continue; }
            uint32_t e = om_prelude_end(tk, k + 1, bb, 1);
            if (e >= bb || (*tk)[e].t != OT_LBRACE) { k = e + 1; continue; }
            uint32_t be = (*tk)[e].m < bb ? (*tk)[e].m : bb;
            vector<char> fn, lfn;
            css_omt_text(p, &q, &fn);
            css_omx_lower(data(fn), len(fn), &lfn);
            int ty = -1;
            for (int y = 0; ffv_types[y]; y++) if (css_omx_eq(data(lfn), len(lfn), ffv_types[y], strlen(ffv_types[y]))) ty = y;
            uint32_t z = e + 1;
            while (ty >= 0 && z < be) {
                uint32_t de = z;
                while (de < be && (*tk)[de].t != OT_SEMI) de++;
                uint32_t a2 = css_omt_skip(tk, z, de);
                uint32_t c = a2 < de && (*tk)[a2].t == OT_IDENT ? css_omt_skip(tk, a2 + 1, de) : de;
                if (c < de && (*tk)[c].t == OT_COLON) {
                    /* its values: integers, as many as the type takes */
                    uint32_t nv = 0;
                    int ok = 1;
                    for (uint32_t w = c + 1; w < de; w++) {
                        css_omt vt = (*tk)[w];
                        if (vt.t == OT_WS) continue;
                        if (vt.t != OT_NUM || !(vt.f & OTF_INT) || vt.v < 0) ok = 0;
                        nv++;
                    }
                    if (!nv || (ty <= 3 && nv != 1) || (ty == 4 && nv > 2)) ok = 0;
                    if (ok) {
                        css_omt nt = (*tk)[a2];
                        vector<char> v;
                        css_omt_text(p, &nt, &v);
                        uint32_t no = (uint32_t)len(pool);
                        css_omx_ident(data(v), len(v), &pool);
                        uint32_t nl = (uint32_t)len(pool) - no;
                        uint32_t vo = (uint32_t)len(pool);
                        css_omx_tokens(p, tk, c + 1, de, 1, &pool);
                        uint32_t vl = (uint32_t)len(pool) - vo;
                        /* a name given again in the type: the last kept */
                        for (size_t x = 0; x + 4 < len(ents); x += 5)
                            if (ents[x] == (uint32_t)ty && ents[x + 2] == nl && !memcmp(data(pool) + ents[x + 1], data(pool) + no, nl)) ents[x] = 0xFFFFFFFFu;
                        push_back(ents, (uint32_t)ty);
                        push_back(ents, no);
                        push_back(ents, nl);
                        push_back(ents, vo);
                        push_back(ents, vl);
                    }
                }
                z = de + 1;
            }
            k = be + 1;
        }
        vector<char> inner;
        for (int y = 0; ffv_types[y]; y++) {
            vector<uint32_t> ix;
            for (size_t x = 0; x + 4 < len(ents); x += 5) {
                if (ents[x] != (uint32_t)y) continue;
                /* in order of its name, bytewise */
                size_t at = len(ix);
                push_back(ix, (uint32_t)x);
                while (at > 0) {
                    uint32_t pb2 = ix[at - 1];
                    uint32_t an = ents[pb2 + 2], bn = ents[x + 2];
                    size_t mn = an < bn ? an : bn;
                    int cmp = memcmp(data(pool) + ents[pb2 + 1], data(pool) + ents[x + 1], mn);
                    if (cmp < 0 || (cmp == 0 && an <= bn)) break;
                    ix[at] = pb2;
                    at--;
                    ix[at] = (uint32_t)x;
                }
            }
            if (!len(ix)) continue;
            if (len(inner)) push_back(inner, ' ');
            push_back(inner, '@');
            om_cat(&inner, ffv_types[y]);
            append_range(inner, " { ", 3);
            for (size_t q2 = 0; q2 < len(ix); q2++) {
                uint32_t x = ix[q2];
                append_range(inner, data(pool) + ents[x + 1], ents[x + 2]);
                append_range(inner, ": ", 2);
                append_range(inner, data(pool) + ents[x + 3], ents[x + 4]);
                append_range(inner, "; ", 2);
            }
            push_back(inner, '}');
        }
        if (level == OM_TOP) ps->phase = 2;
        uint32_t r = om_rule_new(m, CSS_OM_FONT_FEATURE_VALUES);
        om_set_a(m, ps, r, &fam);
        om_set_b(m, ps, r, &inner);
        om_append(m, ps, parent, r);
        return r;
    }
    return 0;
}

/* the declarations [ds, i) of a nested block before a rule (or its end): a style rule's own
   when no rule came before them in it, else a nested declarations rule's (none when empty) */
static void om_flush(css_om *m, om_ps *ps, const char *p, const vector<css_omt> *tk, uint32_t ds, uint32_t i, uint32_t parent, int seen_rule) {
    int mine = !seen_rule && parent && m->rules[parent].kind == CSS_OM_STYLE;
    if (i <= ds && !mine) return;
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    if (i > ds) css_om_decls_tokens(&dl, p, tk, ds, i, CSS_OM_B_STYLE);
    if (mine) m->rules[parent].block = css_om_block_new(m, CSS_OM_B_STYLE, parent, &dl);
    else if (len(dl.d)) {
        uint32_t nr = om_rule_new(m, CSS_OM_NESTED_DECLARATIONS);
        om_append(m, ps, parent, nr);
        m->rules[nr].block = css_om_block_new(m, CSS_OM_B_STYLE, nr, &dl);
    }
    CSS_OM_DL_DROP(dl);
}

/* a list of rules [a, b) appended to parent's (a sheet's top level when parent is 0): level
   OM_TOP, OM_GROUP or OM_NESTED (a style rule's block, or a grouping rule's in one: its
   declarations a nested declarations rule, its style rules relative); in_style the style rule
   it is in. In a nested block a run of declarations before its first rule is a style rule's
   own, each later run a nested declarations rule; an ident begins a declaration, else (when
   it reads as none and a block follows) a nested rule, as CSS Syntax 3 consumes a block's
   contents */
static void om_rules(css_om *m, om_ps *ps, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t parent, uint32_t level, uint32_t in_style) {
    uint32_t i = a, ds = a;
    int seen_rule = 0;
    while (i < b) {
        css_omt t = (*tk)[i];
        if (t.t == OT_WS || (level == OM_TOP && (t.t == OT_CDO || t.t == OT_CDC))) { i++; continue; }
        if (level == OM_NESTED && t.t == OT_SEMI) { i++; continue; }
        int rule_start = t.t == OT_AT;
        if (level == OM_NESTED && !rule_start) {
            int sel_tok = t.t == OT_COLON || t.t == OT_HASH || t.t == OT_LBRACK ||
                          (t.t == OT_DELIM && (p[t.s] == '&' || p[t.s] == '>' || p[t.s] == '+' || p[t.s] == '~' || p[t.s] == '.' || p[t.s] == '*' || p[t.s] == '|'));
            if (!sel_tok) {
                /* a declaration, unless it reads as none and a rule's prelude and block follow */
                uint32_t e = i;
                while (e < b && (*tk)[e].t != OT_SEMI) {
                    css_omt q = (*tk)[e];
                    if ((q.t == OT_FUNC || q.t == OT_LPAREN || q.t == OT_LBRACK || q.t == OT_LBRACE) && q.m < b) e = q.m;
                    e++;
                }
                css_om_dl one;
                memset(&one, 0, sizeof one);
                css_om_decls_tokens(&one, p, tk, i, e, CSS_OM_B_STYLE);
                int ok = len(one.d) > 0;
                CSS_OM_DL_DROP(one);
                uint32_t pe = t.t == OT_IDENT ? om_prelude_end(tk, i, b, 1) : b;
                if (ok || pe >= b || (*tk)[pe].t != OT_LBRACE) { i = e < b ? e + 1 : b; continue; }
            }
        }
        if (level == OM_NESTED) om_flush(m, ps, p, tk, ds, i, parent, seen_rule);
        if (rule_start) {
            uint32_t e = om_prelude_end(tk, i + 1, b, 1);
            int has_block = e < b && (*tk)[e].t == OT_LBRACE;
            uint32_t be = has_block ? ((*tk)[e].m < b ? (*tk)[e].m : b) : e;
            om_rule(m, ps, p, tk, i + 1, i + 1, e, has_block, has_block ? e + 1 : 0, be, parent, level, in_style);
            i = has_block ? be + 1 : e + 1;
        } else {
            uint32_t e = om_prelude_end(tk, i, b, level == OM_NESTED);
            if (e >= b || (*tk)[e].t != OT_LBRACE) {
                /* no block: dropped (a nested one at its ;) */
                i = e + 1;
            } else {
                uint32_t be = (*tk)[e].m < b ? (*tk)[e].m : b;
                om_rule(m, ps, p, tk, 0, i, e, 1, e + 1, be, parent, level, in_style);
                i = be + 1;
            }
        }
        seen_rule = 1;
        ds = i;
    }
    if (level == OM_NESTED) om_flush(m, ps, p, tk, ds, b > ds ? b : ds, parent, seen_rule);
    /* a style rule always has its block */
    if (level == OM_NESTED && parent && m->rules[parent].kind == CSS_OM_STYLE && !m->rules[parent].block) {
        css_om_dl dl;
        memset(&dl, 0, sizeof dl);
        m->rules[parent].block = css_om_block_new(m, CSS_OM_B_STYLE, parent, &dl);
        CSS_OM_DL_DROP(dl);
    }
}

/* the rules a sheet held taken out of it (their records dead) */
static void om_drop_rules(css_om *m, uint32_t sheet) {
    for (uint32_t r = m->sheets[sheet].first; r;) {
        uint32_t nx = m->rules[r].next;
        om_unlink(m, r);
        r = nx;
    }
    m->sheets[sheet].first = m->sheets[sheet].last = 0;
    m->sheets[sheet].count = 0;
}

uint32_t css_om_read(css_om *m, uint32_t sheet, const char *t, size_t n, uint32_t mode) {
    if (!sheet || sheet >= len(m->sheets)) return 0;
    om_drop_rules(m, sheet);
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    om_ps ps;
    memset(&ps, 0, sizeof ps);
    ps.sheet = sheet;
    ps.ns = sheet;
    ps.mode = mode;
    m->warn = 0;
    om_rules(m, &ps, t, &tk, 0, (uint32_t)len(tk) - 1, 0, OM_TOP, 0);
    m->sheets[sheet].parsed = 1;
    m->rev++;
    return m->warn;
}

/* one rule's text parsed alone (CSSOM 6.4.1 parse a CSS rule): its place, 0 when the text is no
   single rule. Made in the model's scratch sheet (spare1: one a model, its list empty between
   parses), its selectors reading the @namespace prefixes of the sheet given */
static uint32_t om_parse_one(css_om *m, uint32_t sheet, const char *t, size_t n, uint32_t mode, int keyframe, uint32_t in_style) {
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    uint32_t b = (uint32_t)len(tk) - 1;
    uint32_t i = css_omt_skip(&tk, 0, b);
    if (i >= b) return 0;
    if (!m->spare1 || m->spare1 >= len(m->sheets)) {
        m->spare1 = css_om_sheet_new(m, CSS_OM_O_CONSTRUCTED);
        m->sheets[m->spare1].live = 0;
    }
    uint32_t scratch = m->spare1;
    om_drop_rules(m, scratch);
    om_ps ps;
    memset(&ps, 0, sizeof ps);
    ps.sheet = scratch;
    ps.ns = sheet;
    ps.mode = mode;
    uint32_t before = 0;
    uint32_t end = 0;
    if (keyframe) {
        uint32_t e = om_prelude_end(&tk, i, b, 0);
        if (e >= b) return 0;
        uint32_t be = tk[e].m;
        if (be >= b || css_omt_skip(&tk, be + 1, b) != b) return 0;
        vector<char> keys;
        if (!om_keys(t, &tk, i, e, &keys)) return 0;
        uint32_t kr = om_rule_new(m, CSS_OM_KEYFRAME);
        om_set_a(m, &ps, kr, &keys);
        om_link(m, scratch, 0, kr, 0);
        m->rules[kr].block = om_block(m, &ps, t, &tk, e + 1, be, CSS_OM_B_KEYFRAME, kr);
    } else if (tk[i].t == OT_AT) {
        uint32_t e = om_prelude_end(&tk, i + 1, b, 1);
        int has_block = e < b && tk[e].t == OT_LBRACE;
        uint32_t be = has_block ? (tk[e].m < b ? tk[e].m : b) : e;
        end = has_block ? be + 1 : e + 1;
        if (!has_block && e >= b) end = b;
        if (end < b && css_omt_skip(&tk, end, b) != b) return 0;
        om_rule(m, &ps, t, &tk, i + 1, i + 1, e, has_block, has_block ? e + 1 : 0, be, 0, OM_TOP, in_style);
    } else {
        uint32_t e = om_prelude_end(&tk, i, b, 0);
        if (e >= b) return 0;
        uint32_t be = tk[e].m < b ? tk[e].m : b;
        end = be + 1;
        if (end < b && css_omt_skip(&tk, end, b) != b) return 0;
        om_rule(m, &ps, t, &tk, 0, i, e, 1, e + 1, be, 0, OM_TOP, in_style);
    }
    if (m->sheets[scratch].count != before + 1) return 0;
    uint32_t r = m->sheets[scratch].last;
    om_unlink(m, r);
    return r;
}

int css_om_insert(css_om *m, uint32_t sheet, uint32_t parent, uint32_t index, const char *t, size_t n, uint32_t mode, uint32_t *made) {
    *made = 0;
    uint32_t count = css_om_count(m, sheet, parent);
    int keyframes = parent && m->rules[parent].kind == CSS_OM_KEYFRAMES;
    if (keyframes) index = count;
    if (index > count) return CSS_OM_E_INDEX;
    uint32_t in_style = parent && m->rules[parent].kind == CSS_OM_STYLE ? parent : 0;
    uint32_t r = om_parse_one(m, sheet, t, n, mode & ~(uint32_t)CSS_OM_READ_CONSTRUCTED, keyframes, in_style);
    /* in a style rule, or a group rule in one, a text that is no rule is read again as
       declarations: a nested declarations rule when it has one (Chromium's ParseRuleForInsert) */
    int nesting = 0;
    for (uint32_t q = parent, steps = 0; q && steps < 10000 && !keyframes; q = m->rules[q].parent, steps++)
        if (m->rules[q].kind == CSS_OM_STYLE) nesting = 1;
    if (!r && nesting) {
        css_om_dl dl;
        memset(&dl, 0, sizeof dl);
        css_om_parse_decls(&dl, t, n, CSS_OM_B_STYLE);
        if (len(dl.d)) {
            r = om_rule_new(m, CSS_OM_NESTED_DECLARATIONS);
            m->rules[r].block = css_om_block_new(m, CSS_OM_B_STYLE, r, &dl);
        }
        CSS_OM_DL_DROP(dl);
    }
    if (!r) return CSS_OM_E_SYNTAX;
    uint32_t kind = m->rules[r].kind;
    if (parent) {
        if (kind == CSS_OM_NAMESPACE) return CSS_OM_E_NAMESPACE_GROUP;
        if (kind == CSS_OM_IMPORT) return CSS_OM_E_IMPORT_GROUP;
    } else {
        if (kind == CSS_OM_IMPORT && (mode & CSS_OM_READ_CONSTRUCTED)) return CSS_OM_E_CONSTRUCTED_IMPORT;
        /* the sheet's order: layer statements before the imports, imports, namespaces, the rest;
           an @namespace refused there an InvalidStateError, any other rule a HierarchyRequestError */
        int refuse = kind == CSS_OM_NAMESPACE ? CSS_OM_E_STATE : CSS_OM_E_HIERARCHY;
        uint32_t npre = 0, nimp = 0, nns = 0;
        uint32_t q = m->sheets[sheet].first;
        while (q && m->rules[q].kind == CSS_OM_LAYER_STATEMENT && !nimp) {
            /* a layer statement before every import is one of those before them, while an import follows */
            uint32_t z = q;
            while (z && m->rules[z].kind == CSS_OM_LAYER_STATEMENT) z = m->rules[z].next;
            if (!z || m->rules[z].kind != CSS_OM_IMPORT) break;
            npre++;
            q = m->rules[q].next;
        }
        while (q && m->rules[q].kind == CSS_OM_IMPORT) { nimp++; q = m->rules[q].next; }
        while (q && m->rules[q].kind == CSS_OM_NAMESPACE) { nns++; q = m->rules[q].next; }
        uint32_t idx = index;
        if (idx < npre || (idx == npre && kind == CSS_OM_LAYER_STATEMENT && nimp)) {
            if (kind != CSS_OM_LAYER_STATEMENT) return refuse;
        } else {
            idx -= npre;
            if (idx < nimp || (idx == nimp && kind == CSS_OM_IMPORT)) {
                if (kind != CSS_OM_IMPORT) return refuse;
            } else {
                if (kind == CSS_OM_IMPORT) return refuse;
                idx -= nimp;
                if (idx < nns || (idx == nns && kind == CSS_OM_NAMESPACE)) {
                    if (kind != CSS_OM_NAMESPACE) return refuse;
                    if (count > npre + nimp + nns) return CSS_OM_E_STATE;
                } else if (kind == CSS_OM_NAMESPACE) return CSS_OM_E_STATE;
            }
        }
    }
    uint32_t before = css_om_at(m, sheet, parent, index);
    om_link(m, sheet, parent, r, before);
    om_set_sheet(m, r, sheet);
    m->rules[r].parent = parent;
    m->rev++;
    *made = r;
    return 0;
}

int css_om_remove(css_om *m, uint32_t sheet, uint32_t parent, uint32_t index) {
    uint32_t r = css_om_at(m, sheet, parent, index);
    if (!r) return CSS_OM_E_INDEX;
    if (!parent && m->rules[r].kind == CSS_OM_NAMESPACE) {
        /* an @namespace is kept while other rules than imports and namespaces are in the sheet */
        for (uint32_t q = m->sheets[sheet].first; q; q = m->rules[q].next)
            if (m->rules[q].kind != CSS_OM_IMPORT && m->rules[q].kind != CSS_OM_NAMESPACE && m->rules[q].kind != CSS_OM_LAYER_STATEMENT) return CSS_OM_E_STATE;
    }
    om_unlink(m, r);
    m->rev++;
    return 0;
}

/* ---- rules: serializing (Chromium's cssText) ---- */

static void om_spanv(const css_om *m, uint32_t off, uint32_t n, vector<char> *out) { css_om_span(m, off, n, out); }
static void om_block_text(const css_om *m, uint32_t b, vector<char> *out) {
    if (!b) return;
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    css_om_block_load(m, b, &dl);
    css_om_decls_text(&dl, m->blocks[b].kind, out);
    CSS_OM_DL_DROP(dl);
}
/* " {" the rule's own rules, each after a line break and two spaces, "\n}" */
static void om_children_text(const css_om *m, uint32_t r, vector<char> *out) {
    append_range(*out, " {", 2);
    for (uint32_t c = m->rules[r].first; c; c = m->rules[c].next) {
        vector<char> ct;
        css_om_rule_text(m, c, &ct);
        if (!len(ct)) continue;
        append_range(*out, "\n  ", 3);
        append_range(*out, data(ct), len(ct));
    }
    append_range(*out, "\n}", 2);
}
/* a declaration block's "{ decls }" ("{ }" when empty) */
static void om_braced(const css_om *m, uint32_t b, vector<char> *out) {
    vector<char> d;
    om_block_text(m, b, &d);
    if (len(d)) { append_range(*out, "{ ", 2); append_range(*out, data(d), len(d)); append_range(*out, " }", 2); }
    else append_range(*out, "{ }", 3);
}

void css_om_rule_text(const css_om *m, uint32_t r, vector<char> *out) {
    if (!r || r >= len(m->rules)) return;
    css_om_rule x = m->rules[r];
    switch (x.kind) {
    case CSS_OM_STYLE: {
        om_spanv(m, x.aoff, x.alen, out);
        vector<char> d;
        om_block_text(m, x.block, &d);
        if (x.first) {
            append_range(*out, " {", 2);
            if (len(d)) { append_range(*out, "\n  ", 3); append_range(*out, data(d), len(d)); }
            for (uint32_t c = x.first; c; c = m->rules[c].next) {
                vector<char> ct;
                css_om_rule_text(m, c, &ct);
                append_range(*out, "\n  ", 3);
                append_range(*out, data(ct), len(ct));
            }
            append_range(*out, "\n}", 2);
        } else if (len(d)) { append_range(*out, " { ", 3); append_range(*out, data(d), len(d)); append_range(*out, " }", 2); }
        else append_range(*out, " { }", 4);
        break;
    }
    case CSS_OM_NESTED_DECLARATIONS: om_block_text(m, x.block, out); break;
    case CSS_OM_IMPORT: {
        om_cat(out, "@import url(");
        vector<char> u;
        om_spanv(m, x.aoff, x.alen, &u);
        css_omx_string(data(u), len(u), out);
        push_back(*out, ')');
        if (x.flags & 1) {
            if (x.clen) { om_cat(out, " layer("); om_spanv(m, x.coff, x.clen, out); push_back(*out, ')'); }
            else om_cat(out, " layer");
        }
        if (x.flags & 2) { om_cat(out, " supports("); om_spanv(m, x.doff, x.dlen, out); push_back(*out, ')'); }
        if (x.blen) { push_back(*out, ' '); om_spanv(m, x.boff, x.blen, out); }
        push_back(*out, ';');
        break;
    }
    case CSS_OM_NAMESPACE: {
        om_cat(out, "@namespace ");
        if (x.alen) {
            vector<char> pre;
            om_spanv(m, x.aoff, x.alen, &pre);
            css_omx_ident(data(pre), len(pre), out);
            push_back(*out, ' ');
        }
        om_cat(out, "url(");
        vector<char> u;
        om_spanv(m, x.boff, x.blen, &u);
        css_omx_string(data(u), len(u), out);
        append_range(*out, ");", 2);
        break;
    }
    case CSS_OM_MEDIA: om_cat(out, "@media "); om_spanv(m, x.aoff, x.alen, out); om_children_text(m, r, out); break;
    case CSS_OM_SUPPORTS: om_cat(out, "@supports "); om_spanv(m, x.aoff, x.alen, out); om_children_text(m, r, out); break;
    case CSS_OM_CONTAINER: om_cat(out, "@container "); om_spanv(m, x.aoff, x.alen, out); om_children_text(m, r, out); break;
    case CSS_OM_LAYER_BLOCK:
        om_cat(out, "@layer");
        if (x.alen) { push_back(*out, ' '); om_spanv(m, x.aoff, x.alen, out); }
        om_children_text(m, r, out);
        break;
    case CSS_OM_LAYER_STATEMENT: om_cat(out, "@layer "); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ';'); break;
    case CSS_OM_SCOPE:
        om_cat(out, "@scope");
        if (x.flags & 1) { append_range(*out, " (", 2); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ')'); }
        if (x.flags & 2) { append_range(*out, " to (", 5); om_spanv(m, x.boff, x.blen, out); push_back(*out, ')'); }
        om_children_text(m, r, out);
        break;
    case CSS_OM_STARTING_STYLE: om_cat(out, "@starting-style"); om_children_text(m, r, out); break;
    case CSS_OM_KEYFRAMES: {
        om_cat(out, (x.flags & 1) ? "@-webkit-keyframes " : "@keyframes ");
        vector<char> nm;
        om_spanv(m, x.aoff, x.alen, &nm);
        css_omx_ident(data(nm), len(nm), out);
        append_range(*out, " { \n", 4);
        for (uint32_t c = x.first; c; c = m->rules[c].next) {
            append_range(*out, "  ", 2);
            css_om_rule_text(m, c, out);
            push_back(*out, '\n');
        }
        push_back(*out, '}');
        break;
    }
    case CSS_OM_KEYFRAME: om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_FONT_FACE: om_cat(out, "@font-face "); om_braced(m, x.block, out); break;
    case CSS_OM_VIEW_TRANSITION: om_cat(out, "@view-transition "); om_braced(m, x.block, out); break;
    case CSS_OM_PAGE: {
        om_cat(out, "@page ");
        if (x.alen) { om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); }
        vector<char> d;
        om_block_text(m, x.block, &d);
        append_range(*out, "{ ", 2);
        if (len(d)) { append_range(*out, data(d), len(d)); push_back(*out, ' '); }
        for (uint32_t c = x.first; c; c = m->rules[c].next) {
            css_om_rule_text(m, c, out);
            push_back(*out, ' ');
        }
        push_back(*out, '}');
        break;
    }
    case CSS_OM_MARGIN: push_back(*out, '@'); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_PROPERTY: om_cat(out, "@property "); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_COUNTER_STYLE: om_cat(out, "@counter-style "); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_FONT_PALETTE_VALUES: om_cat(out, "@font-palette-values "); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_POSITION_TRY: om_cat(out, "@position-try "); om_spanv(m, x.aoff, x.alen, out); push_back(*out, ' '); om_braced(m, x.block, out); break;
    case CSS_OM_FONT_FEATURE_VALUES:
        om_cat(out, "@font-feature-values ");
        om_spanv(m, x.aoff, x.alen, out);
        append_range(*out, " { ", 3);
        if (x.blen) { om_spanv(m, x.boff, x.blen, out); push_back(*out, ' '); }
        push_back(*out, '}');
        break;
    default: break;
    }
}

void css_om_sheet_text(const css_om *m, uint32_t s, vector<char> *out) {
    if (!s || s >= len(m->sheets)) return;
    for (uint32_t r = m->sheets[s].first; r; r = m->rules[r].next) {
        css_om_rule_text(m, r, out);
        push_back(*out, '\n');
    }
}

/* ---- a rule's parts set by a script ---- */

int css_om_set_selector(css_om *m, uint32_t r, const char *t, size_t n) {
    if (!r || r >= len(m->rules)) return 0;
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    vector<char> sel;
    uint32_t b = (uint32_t)len(tk) - 1;
    if (m->rules[r].kind == CSS_OM_PAGE) {
        /* a page selector: the prelude's own reader */
        om_ps ps;
        memset(&ps, 0, sizeof ps);
        vector<char> text;
        om_cat(&text, "@page ");
        append_range(text, t, n);
        om_cat(&text, " {}");
        uint32_t q = om_parse_one(m, 0, data(text), len(text), 0, 0, 0);
        if (!q) return 0;
        m->rules[r].aoff = m->rules[q].aoff;
        m->rules[r].alen = m->rules[q].alen;
        m->rev++;
        return 1;
    }
    /* a nested rule's selector is relative to the rule it is in */
    int nested = 0;
    for (uint32_t p = m->rules[r].parent; p; p = m->rules[p].parent) if (m->rules[p].kind == CSS_OM_STYLE) nested = 1;
    if (!om_selector(m, m->rules[r].sheet, nested, t, &tk, 0, b, &sel)) return 0;
    uint32_t off = css_om_put(m, data(sel), len(sel));
    m->rules[r].aoff = off;
    m->rules[r].alen = (uint32_t)len(sel);
    m->rev++;
    return 1;
}
int css_om_set_keys(css_om *m, uint32_t r, const char *t, size_t n) {
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    vector<char> keys;
    if (!om_keys(t, &tk, 0, (uint32_t)len(tk) - 1, &keys)) return 0;
    uint32_t off = css_om_put(m, data(keys), len(keys));
    m->rules[r].aoff = off;
    m->rules[r].alen = (uint32_t)len(keys);
    m->rev++;
    return 1;
}
void css_om_set_name(css_om *m, uint32_t r, const char *t, size_t n) {
    uint32_t off = css_om_put(m, t, n);
    m->rules[r].aoff = off;
    m->rules[r].alen = (uint32_t)n;
    m->rev++;
}
int css_om_set_counter(css_om *m, uint32_t r, const char *desc, const char *t, size_t n) {
    if (!strcmp(desc, "name")) {
        vector<char> text;
        om_cat(&text, "@counter-style ");
        append_range(text, t, n);
        om_cat(&text, " {}");
        uint32_t q = om_parse_one(m, 0, data(text), len(text), 0, 0, 0);
        if (!q) return 0;
        m->rules[r].aoff = m->rules[q].aoff;
        m->rules[r].alen = m->rules[q].alen;
        m->rev++;
        return 1;
    }
    css_om_dl dl;
    memset(&dl, 0, sizeof dl);
    css_om_block_load(m, m->rules[r].block, &dl);
    vector<char> sys;
    if (!strcmp(desc, "system")) {
        /* a system (CSS Counter Styles 3, 3.1): its algorithm's keyword, fixed with an integer,
           extends with a name; set only when it keeps the rule's algorithm (9.1) */
        vector<css_omt> tk;
        css_omt_tokens(t, n, &tk);
        uint32_t b = (uint32_t)len(tk) - 1, i = css_omt_skip(&tk, 0, b);
        static const char *const algs[] = { "cyclic", "numeric", "alphabetic", "symbolic", "additive", "fixed", "extends", 0 };
        int alg = -1;
        if (i < b && tk[i].t == OT_IDENT)
            for (int k = 0; algs[k]; k++) if (css_omt_is(t, &tk[i], algs[k])) alg = k;
        uint32_t j = alg >= 0 ? css_omt_skip(&tk, i + 1, b) : b;
        if (alg >= 0) om_cat(&sys, algs[alg]);
        if (alg == 5 && j < b) {
            if (tk[j].t != OT_NUM || !(tk[j].f & OTF_INT)) alg = -1;
            else { push_back(sys, ' '); css_omx_num(tk[j].v, &sys); j = css_omt_skip(&tk, j + 1, b); }
        } else if (alg == 6) {
            if (j >= b || tk[j].t != OT_IDENT || css_omt_is(t, &tk[j], "none")) alg = -1;
            else {
                push_back(sys, ' ');
                vector<char> nm;
                css_omt_text(t, &tk[j], &nm);
                css_omx_ident(data(nm), len(nm), &sys);
                j = css_omt_skip(&tk, j + 1, b);
            }
        }
        vector<char> cur;
        css_om_get(&dl, CSS_OM_B_COUNTER, "system", 6, &cur);
        size_t w = 0;
        while (w < len(cur) && cur[w] != ' ') w++;
        size_t aw = alg >= 0 ? strlen(algs[alg]) : 0;
        int same = len(cur) ? (w == aw && alg >= 0 && !memcmp(data(cur), algs[alg], aw)) : alg == 3;
        if (alg < 0 || j != b || !same) { CSS_OM_DL_DROP(dl); return 0; }
        t = data(sys);
        n = len(sys);
    }
    int ok = css_om_set(&dl, CSS_OM_B_COUNTER, desc, strlen(desc), t, n, 0);
    if (ok) css_om_block_store(m, m->rules[r].block, &dl);
    CSS_OM_DL_DROP(dl);
    if (ok) m->rev++;
    return ok;
}
uint32_t css_om_find_key(const css_om *m, uint32_t r, const char *t, size_t n) {
    vector<css_omt> tk;
    css_omt_tokens(t, n, &tk);
    vector<char> keys;
    if (!om_keys(t, &tk, 0, (uint32_t)len(tk) - 1, &keys)) return 0;
    uint32_t found = 0;
    for (uint32_t c = m->rules[r].first; c; c = m->rules[c].next) {
        css_om_rule x = m->rules[c];
        if (x.alen == len(keys) && memcmp(data(m->text) + x.aoff, data(keys), len(keys)) == 0) found = c;
    }
    return found;
}

/* ---- what the cascade is told ---- */

/* an entry: its document's address, its kind (1 owned, 2 adopted), the owner's NODE and
   generation (an adopted one's tree), its flags, and its media, base and text, spans of the
   registry's own text */
typedef struct om_reg { uintptr_t doc; uint32_t kind, node, gen, flags; uint32_t moff, mlen, boff, blen, toff, tlen; } om_reg;
typedef struct om_regs { vector<om_reg> e; vector<char> t; } om_regs;
static om_regs om_r;
static int om_r_exit;
static void om_r_free(void) {
    vector<om_reg> e;
    vector<char> t;
    swap(&om_r.e, &e);
    swap(&om_r.t, &t);
}
static uint32_t om_r_put(const char *t, size_t n) {
    uint32_t off = (uint32_t)len(om_r.t);
    append_range(om_r.t, t, n);
    return off;
}
void css_om_reg_clear(const void *doc) {
    uintptr_t d = (uintptr_t)doc;
    size_t k = 0, had = len(om_r.e);
    for (size_t i = 0; i < had; i++) if (om_r.e[i].doc != d) om_r.e[k++] = om_r.e[i];
    if (k == had) return;
    resize(om_r.e, k);
    if (!k) { clear(om_r.t); return; }
    /* the other documents' texts kept, the cleared ones' let go */
    vector<char> t;
    for (size_t i = 0; i < k; i++) {
        om_reg e = om_r.e[i];
        uint32_t mo = (uint32_t)len(t);
        append_range(t, data(om_r.t) + e.moff, e.mlen);
        uint32_t bo = (uint32_t)len(t);
        append_range(t, data(om_r.t) + e.boff, e.blen);
        uint32_t to = (uint32_t)len(t);
        append_range(t, data(om_r.t) + e.toff, e.tlen);
        om_r.e[i].moff = mo;
        om_r.e[i].boff = bo;
        om_r.e[i].toff = to;
    }
    swap(&om_r.t, &t);
}
static void om_r_add(uintptr_t d, uint32_t kind, uint32_t node, uint32_t gen, uint32_t flags, const char *media, size_t mn, const char *base, size_t bn,
                     const char *text, size_t n) {
    if (!om_r_exit) { om_r_exit = 1; atexit(om_r_free); }
    om_reg e;
    memset(&e, 0, sizeof e);
    e.doc = d;
    e.kind = kind;
    e.node = node;
    e.gen = gen;
    e.flags = flags;
    e.moff = om_r_put(media, mn);
    e.mlen = (uint32_t)mn;
    e.boff = om_r_put(base, bn);
    e.blen = (uint32_t)bn;
    e.toff = om_r_put(text, n);
    e.tlen = (uint32_t)n;
    push_back(om_r.e, e);
}
void css_om_reg_owned(const void *doc, uint32_t node, uint32_t gen, uint32_t flags, const char *media, size_t mn, const char *text, size_t n) {
    om_r_add((uintptr_t)doc, 1, node, gen, flags, media, mn, "", 0, text, n);
}
void css_om_reg_adopted(const void *doc, uint32_t tree, uint32_t flags, const char *media, size_t mn, const char *base, size_t bn,
                        const char *text, size_t n) {
    om_r_add((uintptr_t)doc, 2, tree, 0, flags, media, mn, base, bn, text, n);
}
int css_om_reg_any(const void *doc) {
    uintptr_t d = (uintptr_t)doc;
    for (size_t i = 0; i < len(om_r.e); i++) if (om_r.e[i].doc == d) return 1;
    return 0;
}
static void om_r_get(uint32_t off, uint32_t n, vector<char> *out) {
    clear(*out);
    if (n) append_range(*out, data(om_r.t) + off, n);
}
int css_om_reg_owner(const void *doc, uint32_t node, uint32_t gen, uint32_t *flags, vector<char> *media, vector<char> *text) {
    uintptr_t d = (uintptr_t)doc;
    for (size_t i = 0; i < len(om_r.e); i++) {
        om_reg e = om_r.e[i];
        if (e.doc != d || e.kind != 1 || e.node != node || e.gen != gen) continue;
        *flags = e.flags;
        om_r_get(e.moff, e.mlen, media);
        om_r_get(e.toff, e.tlen, text);
        return 1;
    }
    return 0;
}
uint32_t css_om_reg_adopted_count(const void *doc, uint32_t tree) {
    uintptr_t d = (uintptr_t)doc;
    uint32_t k = 0;
    for (size_t i = 0; i < len(om_r.e); i++) if (om_r.e[i].doc == d && om_r.e[i].kind == 2 && om_r.e[i].node == tree) k++;
    return k;
}
void css_om_reg_adopted_at(const void *doc, uint32_t tree, uint32_t i, uint32_t *flags, vector<char> *media, vector<char> *base,
                           vector<char> *text) {
    uintptr_t d = (uintptr_t)doc;
    uint32_t k = 0;
    for (size_t q = 0; q < len(om_r.e); q++) {
        om_reg e = om_r.e[q];
        if (e.doc != d || e.kind != 2 || e.node != tree) continue;
        if (k++ != i) continue;
        *flags = e.flags;
        om_r_get(e.moff, e.mlen, media);
        om_r_get(e.boff, e.blen, base);
        om_r_get(e.toff, e.tlen, text);
        return;
    }
}
