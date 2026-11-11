/* tests/exec/dom_foreign.c -- namespaces and template contents, in the DOM and in
   the parser. The DOM: elements of the SVG namespace, attributes of XLink's, a
   template made with its contents (a fragment whose host it is); a node refused
   under its own template's contents (by the operation and by a request); a
   template destroyed with its contents; the serialization of all of it, which is
   Chromium's for the same DOM built by the DOM's own API (createElementNS,
   setAttributeNS, template.content); the facts the snapshot gives of them. The
   parser: the tokenizer stops where the tree builder decides, so an SVG style is
   no raw text and a CDATA section is one only in foreign content; a token table
   made with no tree builder to drive it is read as HTML content reads it.
   Prints a line a check; tests/exec/dom_foreign.expect is what it must print. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);

static int failures = 0;

static void check(const char *what, int ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void status_is(const char *what, dom_status got, dom_status want) {
    if (got == want) printf("ok   %s: %s\n", what, dom_status_name(got));
    else { printf("FAIL %s: %s, wanted %s\n", what, dom_status_name(got), dom_status_name(want)); failures++; }
}

#define S(lit) lit, sizeof(lit) - 1

/* the serialization of node n, into out */
static void serialized(const dom_document *d, dom_node_h n, vector<char> *out) {
    own FILE *f = tmpfile();
    if (!f) return;
    defer(f) { fclose(f); }
    dom_serialize(d, n, f);
    rewind(f);
    char buf[4096];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, k);
}

static void serialization_is(const char *what, const dom_document *d, dom_node_h n, const char *want) {
    vector<char> got;
    serialized(d, n, &got);
    int ok = len(got) == strlen(want) && memcmp(data(got), want, len(got)) == 0;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { printf("     got  %.*s\n     want %s\n", (int)len(got), data(got), want); failures++; }
}

/* the snapshot's lines of the facts of namespaces and template contents */
static void namespace_facts(const dom_document *d) {
    own FILE *f = tmpfile();
    if (!f) return;
    defer(f) { fclose(f); }
    dom_export(d, f);
    rewind(f);
    char line[512];
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "dom_namespace(", 14) == 0 || strncmp(line, "dom_template_content(", 21) == 0 ||
            strncmp(line, "dom_attr_namespace(", 19) == 0 || strstr(line, ",fragment).")) fputs(line, stdout);
}

/* the parse of src: the body's first element's serialization */
static void parsed_is(const char *what, const char *src, int driven, const char *want) {
    dom_document d;
    html_parser p;
    dom_init(&d, 1);
    html_init(&p, 1);
    if (driven) html_parse(&p, &d, src, strlen(src));
    else { html_tokenize(&p, src, strlen(src)); html_build(&p, &d); }
    uint32_t html = d.node[1].first_child;
    uint32_t body = html ? d.node[html].last_child : 0;
    uint32_t first = body ? d.node[body].first_child : 0;
    if (!first) { printf("FAIL %s: no body\n", what); failures++; return; }
    serialization_is(what, &d, node_handle(d, first), want);
    if (dom_check_invariants(&d, stdout) != 0) { printf("FAIL %s: invariants\n", what); failures++; }
}

int main(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_node_h root = dom_root(&d);

    /* elements of a namespace, and attributes of one */
    dom_node_h div = dom_create_element(&d, S("div"));
    status_is("a div in the document", dom_append_child(&d, root, div), DOM_OK);
    status_is("a title to escape", dom_set_attribute(&d, div, S("title"), S("a<b>&\"")), DOM_OK);
    dom_node_h svg = dom_create_element_ns(&d, DOM_NAMESPACE_SVG, S("svg"));
    check("an SVG element is in the SVG namespace", d.node[svg.index].ns == DOM_NAMESPACE_SVG);
    check("an HTML element in the HTML namespace", d.node[div.index].ns == DOM_NAMESPACE_HTML);
    status_is("viewBox, in no namespace", dom_set_attribute(&d, svg, S("viewBox"), S("0 0 1 1")), DOM_OK);
    status_is("xlink:href in XLink's", dom_set_attribute_ns(&d, svg, DOM_ATTR_NAMESPACE_XLINK, S("xlink:href"), S("#b")), DOM_OK);
    status_is("set again by namespace and name", dom_set_attribute_ns(&d, svg, DOM_ATTR_NAMESPACE_XLINK, S("xlink:href"), S("#a")), DOM_OK);
    check("one attribute of each name", dom_attributes_count(&d, svg) == 2);
    uint32_t xa = d.node[svg.index].first_attr ? d.attr[d.node[svg.index].first_attr].next : 0;
    check("the second is in the XLink namespace", xa && d.attr[xa].ns == DOM_ATTR_NAMESPACE_XLINK);
    status_is("the same qualified name in no namespace", dom_set_attribute_ns(&d, svg, DOM_ATTR_NAMESPACE_NULL, S("xlink:href"), S("#c")), DOM_OK);
    check("is another attribute", dom_attributes_count(&d, svg) == 3);
    status_is("removed by its qualified name", dom_remove_attribute(&d, svg, S("xlink:href")), DOM_OK);
    check("the first of the name went: XLink's", dom_attributes_count(&d, svg) == 2 && d.attr[xa].live == 0);
    status_is("the other by the same name", dom_remove_attribute(&d, svg, S("xlink:href")), DOM_OK);
    status_is("XLink's again", dom_set_attribute_ns(&d, svg, DOM_ATTR_NAMESPACE_XLINK, S("xlink:href"), S("#a")), DOM_OK);
    status_is("an element in a namespace needs a name", dom_check_create_element_ns(&d, DOM_NAMESPACE_SVG, S("a b")), DOM_INVALID_NAME);
    dom_node_h fo = dom_create_element_ns(&d, DOM_NAMESPACE_SVG, S("foreignObject"));
    dom_node_h p = dom_create_element(&d, S("p"));
    dom_append_child(&d, p, dom_create_text(&d, S("in")));
    dom_append_child(&d, fo, p);
    dom_append_child(&d, svg, fo);
    dom_node_h ss = dom_create_element_ns(&d, DOM_NAMESPACE_SVG, S("style"));
    dom_append_child(&d, ss, dom_create_text(&d, S("a<b")));
    dom_append_child(&d, svg, ss);
    dom_append_child(&d, svg, dom_create_element_ns(&d, DOM_NAMESPACE_SVG, S("br")));
    dom_append_child(&d, div, svg);
    dom_node_h hs = dom_create_element(&d, S("style"));
    dom_append_child(&d, hs, dom_create_text(&d, S("a<b")));
    dom_append_child(&d, div, hs);
    dom_append_child(&d, div, dom_create_element(&d, S("br")));
    check("the invariants hold", dom_check_invariants(&d, stdout) == 0);

    /* a template and its contents */
    dom_node_h tpl = dom_create_element(&d, S("template"));
    uint32_t frag = d.node[tpl.index].content;
    check("a template is made with its contents", frag != 0 && d.node[frag].kind == DOM_NODE_KIND_FRAGMENT);
    check("whose host it is", frag && d.node[frag].host == tpl.index && d.node[frag].parent == 0);
    dom_node_h svg_tpl = dom_create_element_ns(&d, DOM_NAMESPACE_SVG, S("template"));
    check("an SVG template has none", d.node[svg_tpl.index].content == 0);
    dom_node_h tp = dom_create_element(&d, S("p"));
    dom_append_child(&d, tp, dom_create_text(&d, S("x")));
    status_is("a p into the contents", dom_append_child(&d, node_handle(d, frag), tp), DOM_OK);
    status_is("the template into the div", dom_append_child(&d, div, tpl), DOM_OK);
    check("the template has no children of its own", dom_children_count(&d, tpl) == 0);
    status_is("the template into its own contents", dom_append_child(&d, node_handle(d, frag), tpl), DOM_HAS_PARENT);
    dom_remove_child(&d, div, tpl);
    status_is("detached, into its own contents", dom_append_child(&d, node_handle(d, frag), tpl), DOM_WOULD_CYCLE);
    status_is("before a child of its own contents", dom_insert_before(&d, node_handle(d, frag), tpl, tp), DOM_WOULD_CYCLE);
    status_is("under a node of its own contents", dom_append_child(&d, tp, tpl), DOM_WOULD_CYCLE);
    status_is("the contents themselves into an element", dom_append_child(&d, div, node_handle(d, frag)), DOM_WRONG_KIND);
    status_is("destroy the contents alone", dom_destroy(&d, node_handle(d, frag)), DOM_WRONG_KIND);
    char req[256];
    int n = snprintf(req, sizeof req, "amb_request(1, %llu, append_child(n(1,%u,%u), n(1,%u,%u))).", (unsigned long long)dom_revision(&d),
                     (unsigned)tp.index, (unsigned)tp.gen, (unsigned)tpl.index, (unsigned)tpl.gen);
    status_is("a request to put the template under its contents", dom_dispatch(&d, req, (size_t)n, dom_revision(&d), stdout), DOM_WOULD_CYCLE);
    status_is("the template back into the div", dom_append_child(&d, div, tpl), DOM_OK);
    check("the invariants hold", dom_check_invariants(&d, stdout) == 0);

    /* the serialization: Chromium's for the same DOM */
    serialization_is("the div serialized as Chromium serializes it", &d, div,
        "<div title=\"a&lt;b&gt;&amp;&quot;\"><svg viewBox=\"0 0 1 1\" xlink:href=\"#a\"><foreignObject><p>in</p></foreignObject>"
        "<style>a&lt;b</style><br></br></svg><style>a<b</style><br><template><p>x</p></template></div>");
    serialization_is("the template: its contents", &d, tpl, "<template><p>x</p></template>");
    serialization_is("the contents alone", &d, node_handle(d, frag), "<p>x</p>");

    /* the snapshot's facts */
    printf("-- the facts of namespaces and contents\n");
    namespace_facts(&d);

    /* a template destroyed with its contents */
    dom_node_h tpl2 = dom_create_element(&d, S("template"));
    uint32_t frag2 = d.node[tpl2.index].content;
    dom_node_h inner = dom_create_element(&d, S("template"));
    uint32_t frag3 = d.node[inner.index].content;
    dom_node_h deep = dom_create_text(&d, S("deep"));
    dom_append_child(&d, node_handle(d, frag3), deep);
    dom_append_child(&d, node_handle(d, frag2), inner);
    size_t before = node_count(d);
    dom_node_h f2 = node_handle(d, frag2), f3 = node_handle(d, frag3);
    status_is("destroy a template holding a template", dom_destroy(&d, tpl2), DOM_OK);
    check("the two, their contents and the text all go", node_count(d) == before - 5);
    check("their handles are stale", !node_live(d, f2) && !node_live(d, f3) && !node_live(d, inner) && !node_live(d, deep));
    check("the invariants hold", dom_check_invariants(&d, stdout) == 0);

    /* the parser drives the tokenizer: each driven parse is Chromium's (DOMParser) */
    printf("-- the parser\n");
    parsed_is("an SVG style holds markup", "<svg><style><g>x</g></style></svg>", 1, "<svg><style><g>x</g></style></svg>");
    parsed_is("not driven, its text is raw text", "<svg><style><g>x</g></style></svg>", 0, "<svg><style>&lt;g&gt;x&lt;/g&gt;</style></svg>");
    parsed_is("an HTML style's text is raw", "<div><style><g>x</g></style></div>", 1, "<div><style><g>x</g></style></div>");
    parsed_is("CDATA in SVG is text", "<svg><![CDATA[a<b]]></svg>", 1, "<svg>a&lt;b</svg>");
    parsed_is("not driven, a bogus comment", "<svg><![CDATA[a<b]]></svg>", 0, "<svg><!--[CDATA[a<b]]--></svg>");
    parsed_is("CDATA in HTML content is a comment", "<div><![CDATA[a]]></div>", 1, "<div><!--[CDATA[a]]--></div>");
    parsed_is("an SVG title is no RCDATA, and an HTML integration point", "<svg><title><g/>&amp;</title></svg>", 1,
              "<svg><title><g>&amp;</g></title></svg>");
    parsed_is("names adjusted, attributes in their namespaces",
              "<svg viewbox=1 xlink:href=a xml:lang=en xmlns:xlink=x><clippath/></svg>", 1,
              "<svg viewBox=\"1\" xlink:href=\"a\" xml:lang=\"en\" xmlns:xlink=\"x\"><clipPath></clipPath></svg>");
    parsed_is("a text integration point and an HTML one",
              "<math><mi><svg><title>t</title></svg></mi><annotation-xml encoding=TEXT/HTML><p>h</p></annotation-xml></math>", 1,
              "<math><mi><svg><title>t</title></svg></mi><annotation-xml encoding=\"TEXT/HTML\"><p>h</p></annotation-xml></math>");
    parsed_is("a template's markup goes to its contents", "<div><template><tr><td>c<p>x</table>y</template>z</div>", 1,
              "<div><template><tr><td>c<p>xy</p></td></tr></template>z</div>");

    printf("%s: %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}
