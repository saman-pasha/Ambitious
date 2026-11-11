/* tests/exec/dom_broken.c -- the invariant checks can fail. The random walk shows
   that no operation breaks an invariant; this shows that each generated check
   finds the break it is for: a fresh document is corrupted by hand, through the
   store's raw fields (which no operation would do), once for each invariant of
   the contract, and the check must name that invariant. */
#include <stdio.h>
#include <amb.pl>

amb_interface(dom);

#define S(lit) lit, sizeof(lit) - 1

static int failures = 0;

/* a small tree: the document, html, body, a text in body, a p in body */
static void build(dom_document *d, dom_node_h *html, dom_node_h *body, dom_node_h *text, dom_node_h *p) {
    dom_init(d, 1);
    *html = dom_create_element(d, S("html"));
    *body = dom_create_element(d, S("body"));
    *text = dom_create_text(d, S("words"));
    *p = dom_create_element(d, S("p"));
    dom_append_child(d, dom_root(d), *html);
    dom_append_child(d, *html, *body);
    dom_append_child(d, *body, *text);
    dom_append_child(d, *body, *p);
}

static void expect(const char *what, const dom_document *d, int want_broken) {
    printf("-- %s\n", what);
    int n = dom_check_invariants(d, stdout);
    if ((n > 0) != want_broken) { printf("FAIL %s: %d broken\n", what, n); failures++; }
}

int main(void) {
    dom_node_h html, body, text, p;
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        expect("the tree as built", &d, 0);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        d.node[1].kind = DOM_NODE_KIND_ELEMENT;
        expect("the root is no document node", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        d.node[p.index].parent = html.index;
        expect("p's parent says html, but body lists it", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        d.node[p.index].prev_sibling = html.index;
        expect("p's previous sibling link names the wrong node", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        d.node[p.index].prev_sibling = 0;
        expect("p's previous sibling link is dropped", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        d.node[html.index].parent = body.index;
        expect("html's parent is its own child: a cycle", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        dom_node_h t2 = dom_create_text(&d, S("inner"));
        d.node[t2.index].parent = text.index;
        d.node[text.index].first_child = t2.index;
        d.node[text.index].last_child = t2.index;
        expect("a text node with a child", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        dom_set_attribute(&d, p, S("id"), S("x"));
        uint32_t a = d.node[p.index].first_attr;
        d.node[p.index].first_attr = 0;
        d.node[text.index].first_attr = a;
        d.attr[a].owner = text.index;
        expect("an attribute moved onto a text node", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        dom_set_attribute(&d, p, S("id"), S("x"));
        d.node[p.index].first_attr = 0;
        expect("an attribute its owner does not list", &d, 1);
    }
    {
        dom_document d;
        build(&d, &html, &body, &text, &p);
        dom_node_h gone = dom_create_element(&d, S("gone"));
        dom_destroy(&d, gone);
        d.node[p.index].first_child = gone.index;
        d.node[p.index].last_child = gone.index;
        expect("a link to a destroyed node", &d, 1);
    }
    printf("%s: %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}
