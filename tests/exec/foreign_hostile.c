/* tests/exec/foreign_hostile.c -- foreign content and template contents made to
   break an engine: templates nested in each other's contents, SVG and MathML
   nested past any depth a walk could recurse to, and thousands of options nested
   under a selectedcontent. Each is parsed, serialized, cascaded, laid out, made an
   accessibility tree and drawn. The DOM's depth stays bounded, a template's
   contents counted as one level below it (512 open elements, as Blink's parser
   keeps it), the selectedcontent's options cost no quadratic look, and every
   invariant holds. */
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

/* the deepest node, its depth counted up parents and from a template's contents to the template */
static size_t depth_of(const dom_document *d) {
    size_t deepest = 0;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        size_t k = 0;
        for (uint32_t y = x; y;) {
            uint32_t up = d->node[y].parent ? d->node[y].parent : d->node[y].host;
            if (!up) break;
            k++;
            y = up;
        }
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
    own FILE *sink = fopen("/dev/null", "w");
    if (!sink) return;
    defer(sink) { fclose(sink); }
    dom_serialize(&d, dom_root(&d), sink);
    css_cascade(&st, &d, 0);
    layout_run(&lt, &d, &st, 0, 800, 600);
    a11y_build(&ax, &d, &st);
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    amb_paint(&lt, 0, &c);
    size_t foreign = 0, fragments = 0;
    for (uint32_t x = 1; x < d.node_len; x++) {
        if (!d.node[x].live) continue;
        if (d.node[x].kind == DOM_NODE_KIND_FRAGMENT) fragments++;
        if (d.node[x].kind == DOM_NODE_KIND_ELEMENT && d.node[x].ns != DOM_NAMESPACE_HTML) foreign++;
    }
    printf("%s: %zu bytes, %zu nodes (%zu foreign, %zu fragments), depth %zu, %zu boxes; invariants: %d broken\n", what, len(*src),
           node_count(d), foreign, fragments, depth_of(&d), box_count(lt),
           dom_check_invariants(&d, stdout) + layout_check_invariants(&lt, stdout));
}

int main(void) {
    vector<char> a, b, c, e;
    repeat(&a, "<template>", 20000);
    append_range(a, "<p>deep</p>", 11);
    run("20000 templates nested in each other's contents", &a);
    repeat(&b, "<svg><g>", 3000);
    repeat(&b, "<math><mi>", 3000);
    append_range(b, "<p>out</p>", 10);
    run("6000 SVG and 6000 MathML elements nested", &b);
    append_range(c, "<select><button><selectedcontent></selectedcontent></button>", 61);
    repeat(&c, "<option>o<i>", 5000);
    run("5000 options nested under a selectedcontent", &c);
    repeat(&e, "<table><template><tr><td><svg><foreignObject><select><option selected>x", 300);
    run("templates, tables, foreign content and selects, 300 deep", &e);
    return 0;
}
