/* src/html/forms.c -- HTML's form controls over the DOM (HTML 4.10), one account of them for
   the parser, the scripts (src/script/), the cascade, layout and accessibility (src/a11y/):
   what each reads of a control is answered here, so that none drifts from the others.

   CHECKEDNESS (4.10.5.4) is the DOM's (the node's checked field, src/dom/dom.h), kept as
   Chromium keeps it: every change but the checked attribute's -- a click, the checked IDL
   attribute, a form's reset, a radio button group's -- sets the dirty checkedness flag, and
   while the flag is clear the attribute's presence is the checkedness. A radio button is in
   a group (4.10.5.1.18) when it has a name and a form owner, or is connected: the radios of
   its name (compared as written) and form owner, or, without one, of its tree. A button of a
   group checked unchecks the one the group had: when it is checked, inserted, given a name,
   a type or a form attribute that puts it in the group, or its checked attribute added while
   its flag is clear. After the parse each group's last checked radio is its checked one (the
   parser's insertions in their order), and a form's reset sets each control's checkedness to
   its attribute's, its group taken, then clears its flag (so a radio a later one unchecked
   keeps its flag set, as in Chromium).

   CONSTRAINT VALIDATION (4.10.20): a control's validity flags as Chromium's ValidityState
   answers them, whether the control is a candidate or not: a value missing (a text field's
   unless it is disabled or readonly, a check box's, a radio group's that needs a checked
   button -- a radio without a name never --, a file input's, a select's with no option or
   its placeholder label option chosen, a textarea's), an e-mail (each of a list's) or a URL
   of the wrong type, a value off its pattern (the script engine's regular expressions, with
   the v flag), too long or too short (only once the user's edit changed it last, in UTF-16
   code units), under its minimum or over its maximum, off its step from its step base (its
   min, else its value attribute, else the type's), a user's text that is no value (badInput,
   the caller's), a custom error (the caller's). willValidate bars a disabled control, one in
   a datalist, a readonly input or textarea, a hidden, reset, button or image input, and a
   button that does not submit. The messages are Chromium's English ones, in its order (bad
   input, missing, type, pattern, long, short, under, over, step), its numbers as JavaScript
   writes them and its dates as the en-US locale does.

   A VALUE SANITIZED (4.10.5): a text field's line breaks gone, an e-mail's and a URL's white
   space at either end too (a list of e-mails trimmed each and joined by commas), a number's,
   a date's or a time's empty unless it is a valid one, a colour's a valid simple colour in
   lower case or #000000, a range's its number kept in its range and put on its step.

   Not done: an e-mail's domain is not converted to Punycode (a label beyond ASCII is taken as
   good, as Chromium converts it before it looks), a colour named in CSS's syntax is #000000
   (Chromium 153 takes any CSS colour), and a date's or time's value is kept as written, not
   normalized. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
#include "../dom/dom.h"
#include "../net/url.h"
#include "forms.h"
/* HTML's pattern attribute, read by the script engine's regular expressions (src/js/js.h) */
int js_pattern_test(const char *p, size_t pn, const char *v, size_t vn);

/* ---- the DOM, as this unit reads it ---- */

static int fm_white(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char fm_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int form_is(const dom_document *d, uint32_t x, const char *name) {
    return x && x < d->node_len && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML &&
           names_is(d, d->node[x].name, name, strlen(name));
}
/* x's attribute of that name, 0 for none */
static uint32_t fm_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = d->node[x].first_attr;
    if (!a) return 0;
    uint32_t id = names_find(d, name, strlen(name));
    if (!id) return 0;
    for (; a; a = d->attr[a].next) if (d->attr[a].name == id) return a;
    return 0;
}
/* the attribute a's value is the word w (lower case), compared without case (an enumerated
   attribute's: no white space is trimmed) */
static int fm_attr_is(const dom_document *d, uint32_t a, const char *w) {
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    size_t n = strlen(w);
    if (v.len != n) return 0;
    if (!n) return 1;
    const char *p = text_ptr(d, v);
    for (size_t i = 0; i < n; i++) if (fm_lower(p[i]) != w[i]) return 0;
    return 1;
}
/* the next node after y in tree order within top's subtree (its own children's links: not into a shadow tree or a
   template's contents, which are trees of their own); 0 at its end */
static uint32_t fm_next(const dom_document *d, uint32_t y, uint32_t top) {
    if (d->node[y].first_child) return d->node[y].first_child;
    while (y && y != top) {
        if (d->node[y].next_sibling) return d->node[y].next_sibling;
        y = d->node[y].parent;
    }
    return 0;
}
/* x is connected: in the document, through the shadow roots' hosts on the way */
static int fm_connected(const dom_document *d, uint32_t x) {
    for (size_t steps = 0; x && x != 1 && steps <= d->node_len; steps++) {
        uint32_t p = d->node[x].parent;
        x = p ? p : dom_shadow_root_host(d, x);
    }
    return x == 1;
}

static const char *const fm_types[] = { "text", "hidden", "checkbox", "radio", "range", "number", "button", "submit", "reset",
    "image", "search", "email", "tel", "url", "password", "date", "month", "week", "time", "datetime-local", "color", "file" };
int form_input_type(const dom_document *d, uint32_t x) {
    uint32_t a = fm_attr(d, x, "type");
    if (!a) return FORM_TEXT;
    for (int i = 0; i < (int)(sizeof fm_types / sizeof fm_types[0]); i++) if (fm_attr_is(d, a, fm_types[i])) return i;
    return FORM_TEXT;
}
int form_texty(int ty) {
    return ty == FORM_TEXT || ty == FORM_SEARCH || ty == FORM_EMAIL || ty == FORM_TEL || ty == FORM_URL || ty == FORM_PASSWORD;
}
int form_dated(int ty) { return ty >= FORM_DATE && ty <= FORM_DTL; }

/* the first element of root's tree, in tree order, whose id is the attribute a's value */
static uint32_t fm_by_id(const dom_document *d, uint32_t root, uint32_t a) {
    amb_span want = d->attr[a].value;
    if (!want.len) return 0;
    uint32_t idn = names_find(d, "id", 2);
    if (!idn) return 0;
    for (uint32_t y = root; y; y = fm_next(d, y, root)) {
        if (d->node[y].kind != DOM_NODE_KIND_ELEMENT) continue;
        for (uint32_t b = d->node[y].first_attr; b; b = d->attr[b].next) {
            if (d->attr[b].name != idn) continue;
            amb_span v = d->attr[b].value;
            if (v.len == want.len && memcmp(text_ptr(d, v), text_ptr(d, want), v.len) == 0) return y;
            break;
        }
    }
    return 0;
}
uint32_t form_owner(const dom_document *d, uint32_t x) {
    uint32_t fa = fm_attr(d, x, "form");
    /* a form attribute, connected: the form its id names in x's tree, or none (Chromium's
       FindAssociatedForm); else the nearest form around it */
    if (fa && fm_connected(d, x)) {
        uint32_t f = fm_by_id(d, dom_tree_root(d, x), fa);
        return form_is(d, f, "form") ? f : 0;
    }
    for (uint32_t p = d->node[x].parent; p; p = d->node[p].parent) if (form_is(d, p, "form")) return p;
    return 0;
}
int form_disabled(const dom_document *d, uint32_t x) {
    if (fm_attr(d, x, "disabled")) return 1;
    uint32_t child = x;
    for (uint32_t p = d->node[x].parent; p; child = p, p = d->node[p].parent) {
        if (!form_is(d, p, "fieldset") || !fm_attr(d, p, "disabled")) continue;
        uint32_t legend = 0;
        for (uint32_t k = d->node[p].first_child; k && !legend; k = d->node[k].next_sibling) if (form_is(d, k, "legend")) legend = k;
        if (legend && child == legend) continue;
        return 1;
    }
    return 0;
}
/* a button that submits: of type submit, its default (an unknown type too) */
static int fm_submit_button(const dom_document *d, uint32_t x) {
    uint32_t a = fm_attr(d, x, "type");
    return !a || fm_attr_is(d, a, "submit") || (!fm_attr_is(d, a, "button") && !fm_attr_is(d, a, "reset"));
}
int form_will_validate(const dom_document *d, uint32_t x) {
    int input = form_is(d, x, "input"), button = form_is(d, x, "button"), area = form_is(d, x, "textarea");
    if (!input && !button && !area && !form_is(d, x, "select")) return 0;
    if (form_disabled(d, x)) return 0;
    for (uint32_t p = d->node[x].parent; p; p = d->node[p].parent) if (form_is(d, p, "datalist")) return 0;
    if (button) return fm_submit_button(d, x);
    if (input) {
        int ty = form_input_type(d, x);
        if (ty == FORM_HIDDEN || ty == FORM_RESET || ty == FORM_BUTTON || ty == FORM_IMAGE) return 0;
    }
    /* readonly bars an input of any type (as Chromium has it) and a textarea; a select has none */
    return !((input || area) && fm_attr(d, x, "readonly"));
}

/* ---- checkedness and radio button groups ---- */

int form_checked(const dom_document *d, uint32_t x) { return dom_checkedness(d, x, fm_attr(d, x, "checked") != 0); }

/* x's radio button group, when it is in one: its name attribute (0 when none), and its form owner and its tree's root */
static uint32_t fm_group(const dom_document *d, uint32_t x, uint32_t *form, uint32_t *root) {
    *form = 0;
    *root = 0;
    if (!form_is(d, x, "input") || form_input_type(d, x) != FORM_RADIO) return 0;
    uint32_t na = fm_attr(d, x, "name");
    if (!na || !d->attr[na].value.len) return 0;
    *form = form_owner(d, x);
    *root = dom_tree_root(d, x);
    if (!*form && !fm_connected(d, x)) return 0;
    return na;
}
/* y is in the group of the name attribute na, form owner form and tree root */
static int fm_in_group(const dom_document *d, uint32_t y, uint32_t na, uint32_t form, uint32_t root) {
    uint32_t f2 = 0, r2 = 0;
    uint32_t nb = fm_group(d, y, &f2, &r2);
    if (!nb || f2 != form || (!form && r2 != root)) return 0;
    amb_span a = d->attr[na].value, b = d->attr[nb].value;
    return a.len == b.len && memcmp(text_ptr(d, a), text_ptr(d, b), a.len) == 0;
}
uint32_t form_group_checked(const dom_document *d, uint32_t x) {
    uint32_t form = 0, root = 0;
    uint32_t na = fm_group(d, x, &form, &root);
    if (!na) return 0;
    uint32_t top = form ? dom_tree_root(d, form) : root, in = names_find(d, "input", 5);
    for (uint32_t y = top; y; y = fm_next(d, y, top)) {
        if (y == x || d->node[y].name != in || d->node[y].kind != DOM_NODE_KIND_ELEMENT) continue;
        if (form_checked(d, y) && fm_in_group(d, y, na, form, root)) return y;
    }
    return 0;
}
/* x, checked, takes its group: the button the group had checked unchecked, its flag set (Chromium's
   RadioButtonGroup::SetCheckedButton) */
static void fm_take(dom_document *d, uint32_t x) {
    for (int guard = 0; guard < 64; guard++) {
        uint32_t y = form_group_checked(d, x);
        if (!y) return;
        dom_set_checked(d, node_handle(d, y), DOM_CHECKED_DIRTY);
    }
}
void form_set_checked(dom_document *d, uint32_t x, int on) {
    int was = form_checked(d, x);
    dom_set_checked(d, node_handle(d, x), (uint8_t)(DOM_CHECKED_DIRTY | (on ? DOM_CHECKED_ON : 0)));
    if (on && !was) fm_take(d, x);
}
void form_attr_changed(dom_document *d, uint32_t x, const char *name, size_t n, int had) {
    if (!form_is(d, x, "input")) return;
    if (n == 7 && memcmp(name, "checked", 7) == 0) {
        /* the flag clear: the checkedness is the attribute's, a radio's group taken as it comes */
        if (!(d->node[x].checked & DOM_CHECKED_DIRTY) && !had && fm_attr(d, x, "checked")) fm_take(d, x);
        return;
    }
    if (n == 4 && (memcmp(name, "name", 4) == 0 || memcmp(name, "type", 4) == 0 || memcmp(name, "form", 4) == 0) && form_checked(d, x))
        fm_take(d, x);
}
void form_inserted(dom_document *d, uint32_t x) {
    uint32_t in = names_find(d, "input", 5);
    if (!in) return;
    /* the subtree's checked radios, each added to its group in tree order: the group's checked
       button then is one before it, never one of the subtree's still to come */
    vector<uint32_t> rs;
    for (uint32_t y = x; y; y = fm_next(d, y, x))
        if (d->node[y].name == in && d->node[y].kind == DOM_NODE_KIND_ELEMENT && form_checked(d, y)) push_back(rs, y);
    for (size_t i = 0; i < len(rs); i++) {
        uint32_t y = rs[i];
        if (!form_checked(d, y)) continue;
        uint32_t form = 0, root = 0;
        uint32_t na = fm_group(d, y, &form, &root);
        if (!na) continue;
        uint32_t top = form ? dom_tree_root(d, form) : root;
        vector<uint32_t> off;
        for (uint32_t z = top; z; z = fm_next(d, z, top)) {
            if (z == y || d->node[z].name != in || d->node[z].kind != DOM_NODE_KIND_ELEMENT || !form_checked(d, z)) continue;
            int later = 0;
            for (size_t j = i + 1; j < len(rs) && !later; j++) if (rs[j] == z) later = 1;
            if (!later && fm_in_group(d, z, na, form, root)) push_back(off, z);
        }
        for (size_t j = 0; j < len(off); j++) dom_set_checked(d, node_handle(d, off[j]), DOM_CHECKED_DIRTY);
    }
}
void form_parsed(dom_document *d) {
    /* nothing to do in a document none of whose elements has a checked attribute */
    if (!names_find(d, "checked", 7)) return;
    uint32_t in = names_find(d, "input", 5);
    if (!in) return;
    /* the checked radios in the order the parser made them, each unchecking those of its group before it */
    vector<uint32_t> rs, na, fm, rt;
    for (uint32_t y = 2; y < d->node_len; y++) {
        if (!d->node[y].live || d->node[y].name != in || d->node[y].kind != DOM_NODE_KIND_ELEMENT || !form_checked(d, y)) continue;
        uint32_t form = 0, root = 0;
        uint32_t a = fm_group(d, y, &form, &root);
        if (!a) continue;
        for (size_t j = 0; j < len(rs); j++) {
            uint32_t z = rs[j];
            if (!form_checked(d, z) || fm[j] != form || (!form && rt[j] != root)) continue;
            amb_span p = d->attr[na[j]].value, q = d->attr[a].value;
            if (p.len == q.len && memcmp(text_ptr(d, p), text_ptr(d, q), p.len) == 0) dom_set_checked(d, node_handle(d, z), DOM_CHECKED_DIRTY);
        }
        push_back(rs, y);
        push_back(na, a);
        push_back(fm, form);
        push_back(rt, root);
    }
}
void form_reset_checked(dom_document *d, uint32_t x) {
    form_set_checked(d, x, fm_attr(d, x, "checked") != 0);
    dom_set_checked(d, node_handle(d, x), 0);
}
int form_radio_missing(const dom_document *d, uint32_t x) {
    uint32_t form = 0, root = 0;
    uint32_t na = fm_group(d, x, &form, &root);
    if (!na) return 0;
    int req = fm_attr(d, x, "required") != 0;
    if (form_checked(d, x)) return 0;
    uint32_t top = form ? dom_tree_root(d, form) : root, in = names_find(d, "input", 5);
    for (uint32_t y = top; y; y = fm_next(d, y, top)) {
        if (y == x || d->node[y].name != in || d->node[y].kind != DOM_NODE_KIND_ELEMENT || !fm_in_group(d, y, na, form, root)) continue;
        if (form_checked(d, y)) return 0;
        if (fm_attr(d, y, "required")) req = 1;
    }
    return req;
}
/* each radio button whose group needs a checked button and has none, marked in miss (by node): the groups found
   once, for a walk of the whole document (accessibility's) */
void form_radios_missing(const dom_document *d, vector<uint8_t> *miss) {
    clear(*miss);
    resize(*miss, d->node_len, 0);
    uint32_t in = names_find(d, "input", 5);
    if (!in) return;
    vector<uint32_t> rs, na, fm, rt;
    for (uint32_t y = 2; y < d->node_len; y++) {
        if (!d->node[y].live || d->node[y].name != in || d->node[y].kind != DOM_NODE_KIND_ELEMENT) continue;
        uint32_t form = 0, root = 0;
        uint32_t a = fm_group(d, y, &form, &root);
        if (!a) continue;
        push_back(rs, y);
        push_back(na, a);
        push_back(fm, form);
        push_back(rt, root);
    }
    vector<uint8_t> seen;
    resize(seen, len(rs), 0);
    for (size_t i = 0; i < len(rs); i++) {
        if (seen[i]) continue;
        int req = 0, checked = 0;
        amb_span p = d->attr[na[i]].value;
        for (int pass = 0; pass < 2; pass++)
            for (size_t j = i; j < len(rs); j++) {
                if (fm[j] != fm[i] || (!fm[i] && rt[j] != rt[i])) continue;
                amb_span q = d->attr[na[j]].value;
                if (p.len != q.len || memcmp(text_ptr(d, p), text_ptr(d, q), p.len) != 0) continue;
                if (pass == 0) {
                    seen[j] = 1;
                    if (fm_attr(d, rs[j], "required")) req = 1;
                    if (form_checked(d, rs[j])) checked = 1;
                } else if (req && !checked) (*miss)[rs[j]] = 1;
            }
    }
}

/* ---- numbers and dates ---- */

int form_float(const char *p, size_t n, double *v) {
    size_t i = 0;
    if (i < n && p[i] == '-') i++;
    size_t d0 = i;
    while (i < n && p[i] >= '0' && p[i] <= '9') i++;
    size_t di = i - d0, df = 0;
    if (i < n && p[i] == '.') {
        i++;
        size_t f0 = i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        df = i - f0;
        if (!df) return 0;
    }
    if (!di && !df) return 0;
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        if (i < n && (p[i] == '-' || p[i] == '+')) i++;
        size_t e0 = i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        if (i == e0) return 0;
    }
    if (i != n || n >= 64) return 0;
    char buf[64];
    memcpy(buf, p, n);
    buf[n] = 0;
    char *end = buf;
    double x = strtod(buf, &end);
    if (x != x || x > 3.4028234663852886e38 || x < -3.4028234663852886e38) return 0;
    *v = x == 0 ? 0 : x;
    return 1;
}

static const int fm_mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int fm_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int fm_month_days(long y, long m) { return m == 2 && fm_leap(y) ? 29 : fm_mdays[m - 1]; }
/* the days from 1970-01-01 to y-m-dd of the proleptic Gregorian calendar, and back */
static long fm_days(long y, long m, long dd) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void fm_civil(long z, long *y, long *m, long *dd) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *dd = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}
/* the weekday of a day from 1970-01-01 (a Thursday): 0 Monday ... 6 Sunday */
static long fm_weekday(long days) { return ((days % 7) + 7 + 3) % 7; }
/* the weeks of a year (ISO 8601): 53 when it starts on a Thursday, or on a Wednesday in a leap year */
static long fm_weeks(long y) {
    long wd = fm_weekday(fm_days(y, 1, 1));
    return wd == 3 || (wd == 2 && fm_leap(y)) ? 53 : 52;
}
/* the Monday of week 1 of a year: the week of its first Thursday */
static long fm_week1(long y) {
    long jan1 = fm_days(y, 1, 1), wd = fm_weekday(jan1);
    return jan1 - wd + (wd > 3 ? 7 : 0);
}
static int fm_num_at(const char *p, size_t n, size_t *i, size_t least, size_t most, long *v) {
    size_t b = *i;
    long x = 0;
    while (*i < n && *i - b < most && p[*i] >= '0' && p[*i] <= '9') { x = x * 10 + (p[*i] - '0'); (*i)++; }
    if (*i - b < least) return 0;
    *v = x;
    return 1;
}
static int fm_ymd(const char *p, size_t n, size_t *i, long *y, long *m, long *dd, int want_day) {
    if (!fm_num_at(p, n, i, 4, 9, y) || *y < 1 || *i >= n || p[*i] != '-') return 0;
    (*i)++;
    if (!fm_num_at(p, n, i, 2, 2, m) || *m < 1 || *m > 12) return 0;
    if (!want_day) return 1;
    if (*i >= n || p[*i] != '-') return 0;
    (*i)++;
    return fm_num_at(p, n, i, 2, 2, dd) && *dd >= 1 && *dd <= fm_month_days(*y, *m);
}
static int fm_hms(const char *p, size_t n, size_t *i, double *ms) {
    long h = 0, mi = 0, sec = 0, frac = 0;
    if (!fm_num_at(p, n, i, 2, 2, &h) || h > 23 || *i >= n || p[*i] != ':') return 0;
    (*i)++;
    if (!fm_num_at(p, n, i, 2, 2, &mi) || mi > 59) return 0;
    double f = 0;
    if (*i < n && p[*i] == ':') {
        (*i)++;
        if (!fm_num_at(p, n, i, 2, 2, &sec) || sec > 59) return 0;
        if (*i < n && p[*i] == '.') {
            (*i)++;
            size_t b = *i;
            if (!fm_num_at(p, n, i, 1, 3, &frac)) return 0;
            size_t k = *i - b;
            f = (double)frac * (k == 1 ? 100 : k == 2 ? 10 : 1);
        }
    }
    *ms = (double)((h * 60 + mi) * 60 + sec) * 1000.0 + f;
    return 1;
}
int form_type_number(int ty, const char *p, size_t n, double *v) {
    size_t i = 0;
    long y = 0, m = 0, dd = 0, w = 0;
    double ms = 0;
    if (ty == FORM_NUMBER || ty == FORM_RANGE) return form_float(p, n, v);
    if (ty == FORM_DATE) {
        if (!fm_ymd(p, n, &i, &y, &m, &dd, 1) || i != n) return 0;
        *v = (double)fm_days(y, m, dd) * 86400000.0;
        return 1;
    }
    if (ty == FORM_MONTH) {
        if (!fm_ymd(p, n, &i, &y, &m, &dd, 0) || i != n) return 0;
        *v = (double)((y - 1970) * 12 + m - 1);
        return 1;
    }
    if (ty == FORM_WEEK) {
        if (!fm_num_at(p, n, &i, 4, 9, &y) || y < 1 || i + 2 > n || p[i] != '-' || p[i + 1] != 'W') return 0;
        i += 2;
        if (!fm_num_at(p, n, &i, 2, 2, &w) || i != n || w < 1 || w > fm_weeks(y)) return 0;
        *v = (double)(fm_week1(y) + (w - 1) * 7) * 86400000.0;
        return 1;
    }
    if (ty == FORM_TIME) {
        if (!fm_hms(p, n, &i, &ms) || i != n) return 0;
        *v = ms;
        return 1;
    }
    if (ty == FORM_DTL) {
        if (!fm_ymd(p, n, &i, &y, &m, &dd, 1) || i >= n || (p[i] != 'T' && p[i] != ' ')) return 0;
        i++;
        if (!fm_hms(p, n, &i, &ms) || i != n) return 0;
        *v = (double)fm_days(y, m, dd) * 86400000.0 + ms;
        return 1;
    }
    return 0;
}
/* the attribute a's value as a number of the type ty: 1 and *v */
static int fm_attr_number(const dom_document *d, uint32_t a, int ty, double *v) {
    if (!a || !d->attr[a].value.len) return 0;
    amb_span s = d->attr[a].value;
    return form_type_number(ty, text_ptr(d, s), s.len, v);
}

/* a number as JavaScript writes it (Number's toString): the shortest digits that read back */
static void fm_num(double v, char *out, size_t cap) {
    if (v != v) { snprintf(out, cap, "NaN"); return; }
    if (v == 0) { snprintf(out, cap, "0"); return; }
    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) { snprintf(out, cap, v < 0 ? "-Infinity" : "Infinity"); return; }
    char buf[64];
    for (int p = 1; p <= 17; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, v);
        if (strtod(buf, NULL) == v) break;
    }
    char dig[32];
    int nd = 0, neg = 0;
    size_t i = 0;
    if (buf[0] == '-') { neg = 1; i = 1; }
    for (; buf[i] && buf[i] != 'e' && nd < 30; i++) if (buf[i] >= '0' && buf[i] <= '9') dig[nd++] = buf[i];
    int e = buf[i] == 'e' ? atoi(buf + i + 1) : 0;
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    int k = e + 1;   /* the decimal point after the k-th digit */
    char res[96];
    size_t r = 0;
    if (neg) res[r++] = '-';
    if (nd <= k && k <= 21) {
        for (int q = 0; q < nd; q++) res[r++] = dig[q];
        for (int q = nd; q < k; q++) res[r++] = '0';
    } else if (0 < k && k <= 21) {
        for (int q = 0; q < nd; q++) { if (q == k) res[r++] = '.'; res[r++] = dig[q]; }
    } else if (-6 < k && k <= 0) {
        res[r++] = '0';
        res[r++] = '.';
        for (int q = 0; q < -k; q++) res[r++] = '0';
        for (int q = 0; q < nd; q++) res[r++] = dig[q];
    } else {
        res[r++] = dig[0];
        if (nd > 1) { res[r++] = '.'; for (int q = 1; q < nd; q++) res[r++] = dig[q]; }
        r += (size_t)snprintf(res + r, sizeof res - r, "e%c%d", k - 1 < 0 ? '-' : '+', k - 1 < 0 ? 1 - k : k - 1);
    }
    res[r] = 0;
    snprintf(out, cap, "%s", res);
}
/* a value of a dated type, its number, as Chromium's en-US locale writes it in a message: 01/02/2020, February 2020,
   Week 02, 2020, 10:00 AM (its seconds when it has some), 01/02/2020, 10:00 AM */
static void fm_time_text(double ms, char *out, size_t cap) {
    long t = (long)floor(ms + 0.5);
    long h = t / 3600000, mi = t / 60000 % 60, s = t / 1000 % 60, f = t % 1000;
    long h12 = h % 12 == 0 ? 12 : h % 12;
    /* a narrow no-break space (U+202F) before AM or PM, as the ICU of Chromium 153 writes it */
    if (f) snprintf(out, cap, "%ld:%02ld:%02ld.%03ld\xe2\x80\xaf%s", h12, mi, s, f, h < 12 ? "AM" : "PM");
    else if (s) snprintf(out, cap, "%ld:%02ld:%02ld\xe2\x80\xaf%s", h12, mi, s, h < 12 ? "AM" : "PM");
    else snprintf(out, cap, "%ld:%02ld\xe2\x80\xaf%s", h12, mi, h < 12 ? "AM" : "PM");
}
static void fm_localize(int ty, double v, char *out, size_t cap) {
    static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
                                            "October", "November", "December" };
    long y = 0, m = 0, dd = 0;
    if (ty == FORM_DATE || ty == FORM_DTL) {
        double days = floor(v / 86400000.0);
        fm_civil((long)days, &y, &m, &dd);
        if (ty == FORM_DATE) { snprintf(out, cap, "%02ld/%02ld/%04ld", m, dd, y); return; }
        char tm[40];
        fm_time_text(v - days * 86400000.0, tm, sizeof tm);
        snprintf(out, cap, "%02ld/%02ld/%04ld, %s", m, dd, y, tm);
        return;
    }
    if (ty == FORM_MONTH) {
        long mo = (long)floor(v);
        y = 1970 + (mo >= 0 ? mo / 12 : -((-mo + 11) / 12));
        m = mo - (y - 1970) * 12;
        snprintf(out, cap, "%s %04ld", months[m], y);
        return;
    }
    if (ty == FORM_WEEK) {
        long day = (long)floor(v / 86400000.0);
        fm_civil(day + 3, &y, &m, &dd);   /* the week's Thursday is in its year */
        long w = (day - fm_week1(y)) / 7 + 1;
        snprintf(out, cap, "Week %02ld, %04ld", w, y);
        return;
    }
    if (ty == FORM_TIME) { fm_time_text(v, out, cap); return; }
    fm_num(v, out, cap);
}

/* ---- a value sanitized (4.10.5) ---- */

/* a range input's value (HTML 4.10.5.1.13): its least and greatest values (0 and 100), its default half way, kept in
   its range and put on its step (the nearer, the greater of two equally near) */
double form_range_value(const dom_document *d, uint32_t x, const char *p, size_t n, double *mn, double *mx) {
    double lo = 0, hi = 100, step = 1, v = 0;
    if (fm_attr_number(d, fm_attr(d, x, "min"), FORM_RANGE, &v)) lo = v;
    if (fm_attr_number(d, fm_attr(d, x, "max"), FORM_RANGE, &v)) hi = v;
    if (hi < lo) hi = lo;
    uint32_t sa = fm_attr(d, x, "step");
    int any = fm_attr_is(d, sa, "any");
    if (!any && fm_attr_number(d, sa, FORM_RANGE, &v) && v > 0) step = v;
    double val = lo + (hi - lo) / 2;
    if (form_float(p, n, &v)) val = v;
    if (val < lo) val = lo;
    if (val > hi) val = hi;
    if (!any) {
        double q = (val - lo) / step;
        double fl = floor(q);
        double down = lo + fl * step, up = lo + (fl + 1) * step;
        if (val - down < up - val) val = down;
        else if (val != down) val = up;
        if (val > hi) val = down;
    }
    *mn = lo;
    *mx = hi;
    return val;
}
/* a valid simple colour (#rrggbb), in lower case */
static int fm_simple_colour(const char *p, size_t n) {
    if (n != 7 || p[0] != '#') return 0;
    for (size_t i = 1; i < 7; i++) {
        char c = fm_lower(p[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}
void form_sanitize(const dom_document *d, uint32_t x, const char *raw, size_t n, vector<char> *out) {
    clear(*out);
    if (!form_is(d, x, "input")) { append_range(*out, raw, n); return; }
    int ty = form_input_type(d, x);
    if (ty == FORM_EMAIL && fm_attr(d, x, "multiple")) {
        size_t i = 0;
        int first = 1;
        while (i <= n) {
            size_t b = i;
            while (i < n && raw[i] != ',') i++;
            size_t e = i;
            while (b < e && fm_white(raw[b])) b++;
            while (e > b && fm_white(raw[e - 1])) e--;
            if (!first) push_back(*out, ',');
            first = 0;
            for (size_t q = b; q < e; q++) if (raw[q] != '\n' && raw[q] != '\r') push_back(*out, raw[q]);
            i++;
        }
        return;
    }
    if (form_texty(ty)) { for (size_t i = 0; i < n; i++) if (raw[i] != '\n' && raw[i] != '\r') push_back(*out, raw[i]); }
    else append_range(*out, raw, n);
    if (ty == FORM_EMAIL || ty == FORM_URL) {
        size_t b = 0, e = len(*out);
        while (b < e && fm_white((*out)[b])) b++;
        while (e > b && fm_white((*out)[e - 1])) e--;
        vector<char> t;
        append_range(t, data(*out) + b, e - b);
        clear(*out);
        append_range(*out, data(t), len(t));
    }
    double v = 0;
    if ((ty == FORM_NUMBER || form_dated(ty)) && !form_type_number(ty, data(*out), len(*out), &v)) clear(*out);
    if (ty == FORM_COLOR) {
        int ok = fm_simple_colour(data(*out), len(*out));
        if (!ok) { clear(*out); append_range(*out, "#000000", 7); }
        for (size_t i = 0; i < len(*out); i++) (*out)[i] = fm_lower((*out)[i]);
    }
    if (ty == FORM_RANGE) {
        double mn = 0, mx = 0;
        double r = form_range_value(d, x, data(*out), len(*out), &mn, &mx);
        char buf[64];
        fm_num(r, buf, sizeof buf);
        clear(*out);
        append_range(*out, buf, strlen(buf));
    }
}

/* ---- constraint validation ---- */

static int fm_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static const char fm_local_chars[] = ".!#$%&'*+/=?^_`{|}~-";
static int fm_local_ok(char c) {
    if (fm_alnum(c)) return 1;
    for (size_t q = 0; fm_local_chars[q]; q++) if (fm_local_chars[q] == c) return 1;
    return 0;
}
/* p[0..n) is a valid e-mail address (HTML 4.10.5.1.5): a local part of letters, digits and .!#$%&'*+/=?^_`{|}~-, an @,
   and a domain of labels with dots between, each of 1 to 63 letters, digits and hyphens with no hyphen at either end. A
   label beyond ASCII is taken as one whose IDNA form is good, as Chromium converts it before it looks */
static int fm_email_ok(const char *p, size_t n) {
    size_t a = 0;
    while (a < n && p[a] != '@') a++;
    if (a == 0 || a + 1 >= n) return 0;
    for (size_t i = 0; i < a; i++) if (!fm_local_ok(p[i])) return 0;
    size_t i = a + 1;
    while (i <= n) {
        size_t b = i;
        while (i < n && p[i] != '.') {
            char c = p[i];
            if (!fm_alnum(c) && c != '-' && !((unsigned char)c & 0x80)) return 0;
            i++;
        }
        if (i == b || i - b > 63 || p[b] == '-' || p[i - 1] == '-') return 0;
        i++;
    }
    return 1;
}
static int fm_word_is(const char *p, size_t n, const char *w) {
    size_t i = 0;
    while (i < n && w[i] && fm_lower(p[i]) == w[i]) i++;
    return i == n && w[i] == 0;
}
/* p[0..n) is a valid absolute URL, as Chromium takes one: a scheme and its colon, then what the URL parser reads */
static int fm_url_ok(const char *p, size_t n) {
    if (!n || !((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'))) return 0;
    size_t i = 1;
    while (i < n && (fm_alnum(p[i]) || p[i] == '+' || p[i] == '-' || p[i] == '.')) i++;
    if (i >= n || p[i] != ':') return 0;
    vector<char> ref, out;
    /* a space in a special scheme's host, written or as %20, Chromium's parser takes (it escapes it), where the URL
       standard refuses it: the host is judged without its spaces (measured on tests/pages/a11y-url.html) */
    int special = (i == 4 && (fm_word_is(p, 4, "http") || fm_word_is(p, 4, "file"))) || (i == 5 && fm_word_is(p, 5, "https")) ||
                  (i == 3 && fm_word_is(p, 3, "ftp")) || (i == 2 && fm_word_is(p, 2, "ws")) || (i == 3 && fm_word_is(p, 3, "wss"));
    size_t hb = n, he = n;
    if (special && i + 2 < n && p[i + 1] == '/' && p[i + 2] == '/') {
        hb = i + 3;
        he = hb;
        while (he < n && p[he] != '/' && p[he] != '?' && p[he] != '#' && p[he] != '\\') he++;
        for (size_t k = hb; k < he; k++) if (p[k] == '@') hb = k + 1;
        if (hb < he && p[hb] == '[') hb = he;   /* an IPv6 address keeps its spaces, which Chromium refuses too */
    }
    for (size_t k = 0; k < n; k++) {
        if (k >= hb && k < he) {
            if (p[k] == ' ') continue;
            if (p[k] == '%' && k + 2 < he && p[k + 1] == '2' && p[k + 2] == '0') { k += 2; continue; }
        }
        push_back(ref, p[k]);
    }
    push_back(ref, 0);
    resize(out, 3 * n + 256);
    return net_url_resolve("about:blank", data(ref), data(out), len(out));
}
/* an option's value is empty: its value attribute's, else its text's (white space aside) */
static int fm_option_empty(const dom_document *d, uint32_t o) {
    uint32_t va = fm_attr(d, o, "value");
    if (va) return d->attr[va].value.len == 0;
    for (uint32_t x = d->node[o].first_child; x; x = fm_next(d, x, o)) {
        if (d->node[x].kind != DOM_NODE_KIND_TEXT || !d->node[x].data.len) continue;
        amb_span v = d->node[x].data;
        const char *q = text_ptr(d, v);
        for (uint32_t i = 0; i < v.len; i++) if (!fm_white(q[i])) return 0;
    }
    return 1;
}
/* a select shown as a list: multiple, or a size above 1 */
static int fm_select_list(const dom_document *d, uint32_t s) {
    if (fm_attr(d, s, "multiple")) return 1;
    uint32_t a = fm_attr(d, s, "size");
    if (!a || !d->attr[a].value.len) return 0;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    long n = 0;
    size_t i = 0;
    while (i < v.len && fm_white(p[i])) i++;
    if (i < v.len && p[i] == '+') i++;
    size_t b = i;
    while (i < v.len && p[i] >= '0' && p[i] <= '9') { if (n < 100000) n = n * 10 + (p[i] - '0'); i++; }
    return i > b && n > 1;
}
/* a select's options in tree order (its own and its optgroups'), each with how it is chosen: 0 not, 1 by its selected
   attribute, 2 by default (HTML's selectedness setting: a single select keeps the last option selected, and one shown
   as a drop-down chooses its first enabled one when none is) */
void form_options(const dom_document *d, uint32_t s, vector<uint32_t> *opts, vector<uint8_t> *how) {
    clear(*opts);
    clear(*how);
    for (uint32_t c = d->node[s].first_child; c; c = d->node[c].next_sibling) {
        if (form_is(d, c, "option")) push_back(*opts, c);
        else if (form_is(d, c, "optgroup"))
            for (uint32_t o = d->node[c].first_child; o; o = d->node[o].next_sibling) if (form_is(d, o, "option")) push_back(*opts, o);
    }
    int last = -1;
    for (size_t i = 0; i < len(*opts); i++) {
        uint8_t h = fm_attr(d, (*opts)[i], "selected") ? 1 : 0;
        push_back(*how, h);
        if (h) last = (int)i;
    }
    if (fm_attr(d, s, "multiple")) return;
    for (size_t i = 0; i < len(*how); i++) if ((int)i != last) (*how)[i] = 0;
    if (last >= 0 || fm_select_list(d, s)) return;
    for (size_t i = 0; i < len(*opts); i++) {
        uint32_t o = (*opts)[i];
        uint32_t p = d->node[o].parent;
        int off = fm_attr(d, o, "disabled") || (form_is(d, p, "optgroup") && fm_attr(d, p, "disabled"));
        if (!off) { (*how)[i] = 2; return; }
    }
}
/* a select's value missing: required, and no option chosen but its placeholder label option (a drop-down's first option,
   its own child, whose value is empty) */
static int fm_select_missing(const dom_document *d, uint32_t s) {
    if (!fm_attr(d, s, "required")) return 0;
    vector<uint32_t> opts;
    vector<uint8_t> how;
    form_options(d, s, &opts, &how);
    uint32_t ph = 0;
    if (len(opts) && !fm_select_list(d, s) && d->node[opts[0]].parent == s && fm_option_empty(d, opts[0])) ph = opts[0];
    for (size_t i = 0; i < len(opts); i++) if (how[i] && opts[i] != ph) return 0;
    return 1;
}
/* the UTF-16 code units of a UTF-8 text (JavaScript's length) */
static size_t fm_units(const char *p, size_t n) {
    size_t u = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if ((c & 0xC0) == 0x80) continue;
        u += c >= 0xF0 ? 2 : 1;
    }
    return u;
}
/* an attribute's value as a non-negative integer (HTML 2.3.4.1): 1 and *v */
static int fm_attr_uint(const dom_document *d, uint32_t a, long *v) {
    if (!a || !d->attr[a].value.len) return 0;
    amb_span s = d->attr[a].value;
    const char *p = text_ptr(d, s);
    size_t i = 0;
    while (i < s.len && fm_white(p[i])) i++;
    if (i < s.len && p[i] == '+') i++;
    size_t b = i;
    long x = 0;
    while (i < s.len && p[i] >= '0' && p[i] <= '9') { if (x < 1000000000) x = x * 10 + (p[i] - '0'); i++; }
    if (i == b) return 0;
    *v = x;
    return 1;
}

/* a steppable input's range and step (Chromium's StepRange): its least and greatest values (its min and max, else the
   type's), its step (0: any) and step base (its min, else its value attribute, else the type's) */
typedef struct fm_steps { double lo, hi, step, base; int has_lo, has_hi; } fm_steps;
static void fm_step_range(const dom_document *d, uint32_t x, int ty, fm_steps *r) {
    double v = 0;
    r->has_lo = fm_attr_number(d, fm_attr(d, x, "min"), ty, &r->lo);
    r->has_hi = fm_attr_number(d, fm_attr(d, x, "max"), ty, &r->hi);
    if (!r->has_lo) r->lo = ty == FORM_NUMBER ? -3.4028234663852886e38 : ty == FORM_RANGE ? 0 : ty == FORM_TIME ? 0 : -8.64e15;
    if (!r->has_hi) r->hi = ty == FORM_NUMBER ? 3.4028234663852886e38 : ty == FORM_RANGE ? 100 : ty == FORM_TIME ? 86399999 : 8.64e15;
    /* the default step, the step scale factor (HTML 4.10.5.1): a day, a month, a week, a second of a time's 60 */
    double def = ty == FORM_TIME || ty == FORM_DTL ? 60 : 1;
    double scale = ty == FORM_DATE ? 86400000.0 : ty == FORM_WEEK ? 604800000.0 : ty == FORM_TIME || ty == FORM_DTL ? 1000.0 : 1;
    uint32_t sa = fm_attr(d, x, "step");
    if (sa && fm_attr_is(d, sa, "any")) r->step = 0;
    else {
        double sv = 0;
        double step = fm_attr_number(d, sa, FORM_NUMBER, &sv) && sv > 0 ? sv : def;
        /* Chromium's rounding: a date's, a month's or a week's step whole, a time's in whole milliseconds */
        if (ty == FORM_DATE || ty == FORM_MONTH || ty == FORM_WEEK) { step = floor(step + 0.5); if (step < 1) step = 1; }
        step *= scale;
        if (ty == FORM_TIME || ty == FORM_DTL) { step = floor(step + 0.5); if (step < 1) step = 1; }
        r->step = step;
    }
    if (r->has_lo) r->base = r->lo;
    else if (fm_attr_number(d, fm_attr(d, x, "value"), ty, &v)) r->base = v;
    else r->base = ty == FORM_WEEK ? -259200000.0 : 0;
}
/* the value off its step (Chromium's StepRange::StepMismatch: what a float cannot tell apart allowed a number) */
static int fm_step_mismatch(const fm_steps *r, int ty, double v) {
    if (r->step <= 0) return 0;
    double q = fabs(v - r->base);
    if (q / 9007199254740992.0 > r->step) return 0;
    double rem = q - r->step * floor(q / r->step);
    double err = ty == FORM_NUMBER || ty == FORM_RANGE ? r->step / 16777216.0 : 0;
    return err < rem && rem < r->step - err;
}

uint32_t form_validity(const dom_document *d, uint32_t x, const char *p, size_t n, uint32_t how) {
    uint32_t f = 0;
    if (how & FORM_HOW_CUSTOM) f |= FORM_V_CUSTOM;
    int req = fm_attr(d, x, "required") != 0;
    if (form_is(d, x, "select")) {
        if (fm_select_missing(d, x)) f |= FORM_V_MISSING;
        return f;
    }
    if (form_is(d, x, "textarea")) {
        /* missing unless disabled or readonly (in a datalist too, as Chromium has it) */
        int off = form_disabled(d, x) || fm_attr(d, x, "readonly");
        if (req && !off && !n) f |= FORM_V_MISSING;
        long lim = 0;
        size_t u = (how & FORM_HOW_EDITED) ? fm_units(p, n) : 0;
        if ((how & FORM_HOW_EDITED) && fm_attr_uint(d, fm_attr(d, x, "maxlength"), &lim) && u > (size_t)lim) f |= FORM_V_LONG;
        if ((how & FORM_HOW_EDITED) && fm_attr_uint(d, fm_attr(d, x, "minlength"), &lim) && u > 0 && u < (size_t)lim) f |= FORM_V_SHORT;
        return f;
    }
    if (!form_is(d, x, "input")) return f;
    int ty = form_input_type(d, x);
    if (how & FORM_HOW_BAD) f |= FORM_V_BAD;
    int field = form_texty(ty) || ty == FORM_NUMBER || form_dated(ty);
    if (req && field && !n && !form_disabled(d, x) && !fm_attr(d, x, "readonly")) f |= FORM_V_MISSING;
    if (req && ((ty == FORM_CHECKBOX && !form_checked(d, x)) || ty == FORM_FILE)) f |= FORM_V_MISSING;
    if (ty == FORM_RADIO) {
        int miss = (how & FORM_HOW_GROUP_KNOWN) ? (how & FORM_HOW_GROUP_MISSING) != 0 : form_radio_missing(d, x);
        if (miss) f |= FORM_V_MISSING;
    }
    if (!n) return f;
    /* the type of an e-mail (each of a list's) and of a URL; a text field's pattern, each of a list's e-mails */
    uint32_t pa = form_texty(ty) ? fm_attr(d, x, "pattern") : 0;
    if (ty == FORM_EMAIL || ty == FORM_URL || pa) {
        int each = ty == FORM_EMAIL && fm_attr(d, x, "multiple");
        size_t i = 0;
        while (i <= n) {
            size_t b = i;
            while (i < n && !(each && p[i] == ',')) i++;
            if (ty == FORM_EMAIL && !fm_email_ok(p + b, i - b)) f |= FORM_V_TYPE;
            if (ty == FORM_URL && !fm_url_ok(p + b, i - b)) f |= FORM_V_TYPE;
            if (pa && d->attr[pa].value.len && js_pattern_test(text_ptr(d, d->attr[pa].value), d->attr[pa].value.len, p + b, i - b) == 0)
                f |= FORM_V_PATTERN;
            i++;
        }
    }
    /* too long, too short: once the user's edit changed the value last (Chromium's dirty flag check) */
    if (form_texty(ty) && (how & FORM_HOW_EDITED)) {
        long lim = 0;
        size_t u = fm_units(p, n);
        if (fm_attr_uint(d, fm_attr(d, x, "maxlength"), &lim) && u > (size_t)lim) f |= FORM_V_LONG;
        if (fm_attr_uint(d, fm_attr(d, x, "minlength"), &lim) && u < (size_t)lim) f |= FORM_V_SHORT;
    }
    /* a number's, a date's or a time's range and step */
    if (ty == FORM_NUMBER || ty == FORM_RANGE || form_dated(ty)) {
        double v = 0;
        if (!form_type_number(ty, p, n, &v)) return f;
        fm_steps r;
        fm_step_range(d, x, ty, &r);
        if (r.has_lo && v < r.lo) f |= FORM_V_UNDER;
        if (r.has_hi && v > r.hi) f |= FORM_V_OVER;
        if (fm_step_mismatch(&r, ty, v)) f |= FORM_V_STEP;
    }
    return f;
}

/* ---- the messages, Chromium's English ones ---- */

static void fm_put(vector<char> *out, const char *s) { append_range(*out, s, strlen(s)); }
/* the text p[0, n)'s code point at i, its UTF-8 bytes' count */
static size_t fm_cp_len(const char *p, size_t n, size_t i) {
    unsigned char c = (unsigned char)p[i];
    size_t k = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return i + k <= n ? k : n - i;
}
/* an e-mail's type mismatch, as Chromium tells it of the first address that is no valid one (EmailInputType) */
static void fm_email_message(const char *p, size_t n, int multiple, vector<char> *out) {
    size_t i = 0, b = 0, e = n;
    int found = 0;
    while (i <= n && !found) {
        b = i;
        while (i < n && !(multiple && p[i] == ',')) i++;
        e = i;
        if (multiple) {
            while (b < e && fm_white(p[b])) b++;
            while (e > b && fm_white(p[e - 1])) e--;
        }
        if (!fm_email_ok(p + b, e - b)) found = 1;
        i++;
    }
    if (!found) { fm_put(out, multiple ? "Please enter a comma separated list of email addresses." : "Please enter an email address."); return; }
    const char *a = p + b;
    size_t an = e - b;
    if (!an) { fm_put(out, "Please enter a non-empty email address."); return; }
    size_t at = 0;
    while (at < an && a[at] != '@') at++;
    if (at == an) {
        fm_put(out, "Please include an '@' in the email address. '");
        append_range(*out, a, an);
        fm_put(out, "' is missing an '@'.");
        return;
    }
    if (at == 0) { fm_put(out, "Please enter a part followed by '@'. '"); append_range(*out, a, an); fm_put(out, "' is incomplete."); return; }
    if (at + 1 == an) { fm_put(out, "Please enter a part following '@'. '"); append_range(*out, a, an); fm_put(out, "' is incomplete."); return; }
    for (size_t k = 0; k < at; k++)
        if (((unsigned char)a[k] & 0x80) || !fm_local_ok(a[k])) {
            fm_put(out, "A part followed by '@' should not contain the symbol '");
            append_range(*out, a + k, fm_cp_len(a, at, k));
            fm_put(out, "'.");
            return;
        }
    const char *dm = a + at + 1;
    size_t dn = an - at - 1;
    for (size_t k = 0; k < dn; k++)
        if (!((unsigned char)dm[k] & 0x80) && !fm_alnum(dm[k]) && dm[k] != '.' && dm[k] != '-') {
            fm_put(out, "A part following '@' should not contain the symbol '");
            append_range(*out, dm + k, 1);
            fm_put(out, "'.");
            return;
        }
    int dots = dm[0] == '.' || dm[dn - 1] == '.';
    for (size_t k = 0; k + 1 < dn && !dots; k++) if (dm[k] == '.' && dm[k + 1] == '.') dots = 1;
    if (dots) { fm_put(out, "'.' is used at a wrong position in '"); append_range(*out, dm, dn); fm_put(out, "'."); return; }
    fm_put(out, multiple ? "Please enter a comma separated list of email addresses." : "Please enter an email address.");
}
void form_message(const dom_document *d, uint32_t x, uint32_t flags, const char *p, size_t n, vector<char> *out) {
    clear(*out);
    char buf[200], b2[100];
    int input = form_is(d, x, "input"), area = form_is(d, x, "textarea");
    int ty = input ? form_input_type(d, x) : -1;
    if (flags & FORM_V_BAD) { fm_put(out, ty == FORM_NUMBER ? "Please enter a number." : form_dated(ty) ? "Please enter a valid value. The field is incomplete or has an invalid date." : "Please enter a valid value."); return; }
    if (flags & FORM_V_MISSING) {
        if (form_is(d, x, "select")) fm_put(out, "Please select an item in the list.");
        else if (ty == FORM_CHECKBOX) fm_put(out, "Please check this box if you want to proceed.");
        else if (ty == FORM_RADIO) fm_put(out, "Please select one of these options.");
        else if (ty == FORM_FILE) fm_put(out, fm_attr(d, x, "multiple") ? "Please select one or more files." : "Please select a file.");
        else fm_put(out, "Please fill out this field.");
        return;
    }
    if (flags & FORM_V_TYPE) {
        if (ty == FORM_EMAIL) fm_email_message(p, n, fm_attr(d, x, "multiple") != 0, out);
        else fm_put(out, "Please enter a URL.");
        return;
    }
    if (flags & FORM_V_PATTERN) { fm_put(out, "Please match the requested format."); return; }
    if (flags & (FORM_V_LONG | FORM_V_SHORT)) {
        long lim = 0;
        fm_attr_uint(d, fm_attr(d, x, (flags & FORM_V_LONG) ? "maxlength" : "minlength"), &lim);
        size_t u = fm_units(p, n);
        snprintf(buf, sizeof buf, (flags & FORM_V_LONG) ? "Please shorten this text to %ld characters or less (you are currently using %zu character%s)."
                                                         : "Please lengthen this text to %ld characters or more (you are currently using %zu character%s).",
                 lim, u, u == 1 ? "" : "s");
        fm_put(out, buf);
        return;
    }
    if (!input || area || !(ty == FORM_NUMBER || ty == FORM_RANGE || form_dated(ty))) return;
    double v = 0;
    if (!form_type_number(ty, p, n, &v)) return;
    fm_steps r;
    fm_step_range(d, x, ty, &r);
    int dated = form_dated(ty);
    if (flags & FORM_V_UNDER) {
        fm_localize(ty, r.lo, b2, sizeof b2);
        snprintf(buf, sizeof buf, dated ? "Value must be %s or later." : "Value must be greater than or equal to %s.", b2);
        fm_put(out, buf);
        return;
    }
    if (flags & FORM_V_OVER) {
        fm_localize(ty, r.hi, b2, sizeof b2);
        snprintf(buf, sizeof buf, dated ? "Value must be %s or earlier." : "Value must be less than or equal to %s.", b2);
        fm_put(out, buf);
        return;
    }
    if (!(flags & FORM_V_STEP) || r.step <= 0) return;
    /* the nearest valid value (Chromium's StepRange::ClampValue: rounded to the step from its base, half away from zero,
       then kept in the range), and the one on its other side when that is in the range too */
    double in = v < r.lo ? r.lo : v > r.hi ? r.hi : v;
    double c1 = r.base + round((in - r.base) / r.step) * r.step;
    if (c1 > r.hi) c1 -= r.step;
    else if (c1 < r.lo) c1 += r.step;
    if (c1 < r.lo || c1 > r.hi) c1 = in;
    double c2 = c1 < v ? c1 + r.step : c1 - r.step;
    char s1[100], s2[100];
    fm_localize(ty, c1, s1, sizeof s1);
    if (c2 < r.lo || c2 > r.hi) {
        snprintf(buf, sizeof buf, "Please enter a valid value. The nearest valid value is %s.", s1);
        fm_put(out, buf);
        return;
    }
    fm_localize(ty, c2, s2, sizeof s2);
    snprintf(buf, sizeof buf, "Please enter a valid value. The two nearest valid values are %s and %s.", c1 < c2 ? s1 : s2, c1 < c2 ? s2 : s1);
    fm_put(out, buf);
}
