/* tests/exec/hostile.c -- documents made to break an engine: nesting deeper than
   any walk of the tree can recurse, a selector nested deeper than its matcher can,
   and one whose descendant combinators would backtrack without end. Each is parsed,
   cascaded, laid out, made an accessibility tree and drawn; the DOM's depth stays
   bounded (512 open elements, as Blink's parser keeps it), and a selector past the
   matcher's bounds matches nothing. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(a11y);
#include "../../src/paint/paint.h"

static void repeat(vector<char> *out, const char *s, int n) { for (int i = 0; i < n; i++) append_range(out, s, strlen(s)); }

static size_t depth_of(const dom_document *d) {
    size_t deepest = 0;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        size_t k = 0;
        for (uint32_t p = d->node[x].parent; p; p = d->node[p].parent) k++;
        if (k > deepest) deepest = k;
    }
    return deepest;
}

static void run(const char *what, const vector<char> *src) {
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    a11y_tree ax;
    dom_init(&d, 1);
    html_init(&p, 1);
    css_init(&st, 1);
    layout_init(&lt, 1);
    a11y_init(&ax, 1);
    html_parse(&p, &d, data(*src), len(*src));
    css_cascade(&st, &d, 0);
    layout_run(&lt, &d, &st, 0, 800, 600);
    a11y_build(&ax, &d, &st);
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    amb_paint(&lt, 0, &c);
    /* the paragraph's colour: red only where a selector really matched */
    const char *color = "?";
    for (uint32_t e = 1; e < st.estyle_len; e++) {
        if (!names_is(&d, d.node[st.estyle[e].node.index].name, "p", 1)) continue;
        uint32_t c0 = st.estyle[e].first;                    /* the paragraph's values: its style's */
        for (uint32_t k = c0; k < st.cval_len && st.cval[k].style == st.cval[c0].style; k++)
            if (st.cval[k].prop == CSS_PROP_COLOR) color = st.cval[k].color == 0xff0000ffu ? "red" : "not red";
    }
    printf("%s: %zu bytes, %zu nodes, depth %zu, %zu boxes, the paragraph %s; invariants: %d broken\n", what, len(*src), node_count(d),
           depth_of(&d), box_count(lt), color, dom_check_invariants(&d, stdout) + layout_check_invariants(&lt, stdout));
}

int main(void) {
    vector<char> a, b, c, e;
    repeat(&a, "<div>", 3000);
    append_range(a, "<p>deep</p>", 11);
    repeat(&a, "</div>", 3000);
    run("3000 nested divs", &a);
    append_range(b, "<style>", 7);
    repeat(&b, ":is(", 5000);
    append_range(b, "p", 1);
    repeat(&b, ")", 5000);
    append_range(b, " { color: red }</style><p>x</p>", 31);
    run(":is() nested 5000 deep", &b);
    append_range(c, "<style>", 7);
    repeat(&c, "div ", 30);
    append_range(c, "p { color: red }</style>", 24);
    repeat(&c, "<div>", 400);
    append_range(c, "<p>x</p>", 8);
    run("30 descendant combinators over 400 divs", &c);
    append_range(e, "<style>:not(:not(:not(:not(p)))) { color: red } li:nth-child(2n+1) ~ li:nth-child(odd) ~ li { color: blue }</style><p>x</p><ul>", 124);
    repeat(&e, "<li>i</li>", 512);
    append_range(e, "</ul>", 5);
    run(":not() four deep, and nth-child over 512 siblings", &e);
    return 0;
}
