/* tests/exec/dom_basic.c -- the DOM's execution test: build a document through
   the generated interface, then hold each operation to its contract: the
   refusals and their statuses, the invariants after every step, the revision
   and the log, stale handles after a destroy and a reuse, the serialization,
   the exported facts and the request dispatcher's answers. Prints a line a
   check; tests/exec/dom_basic.expect is what it must print. */
#include <stdio.h>
#include <amb.pl>

amb_interface(dom);

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

int main(void) {
    dom_document d;
    dom_init(&d, 1);
    check("a new document holds its root and nothing else", node_count(d) == 1 && dom_revision(&d) == 0);
    check("the invariants hold on an empty document", dom_check_invariants(&d, stdout) == 0);

    dom_node_h root = dom_root(&d);
    dom_node_h html = dom_create_element(&d, S("html"));
    dom_node_h body = dom_create_element(&d, S("body"));
    dom_node_h p = dom_create_element(&d, S("p"));
    dom_node_h t = dom_create_text(&d, S("Hello <world> & you"));
    dom_node_h btn = dom_create_element(&d, S("button"));
    dom_node_h bt = dom_create_text(&d, S("Submit"));
    status_is("append html to the document", dom_append_child(&d, root, html), DOM_OK);
    status_is("append body to html", dom_append_child(&d, html, body), DOM_OK);
    status_is("append p to body", dom_append_child(&d, body, p), DOM_OK);
    status_is("append text to p", dom_append_child(&d, p, t), DOM_OK);
    status_is("append button to body", dom_append_child(&d, body, btn), DOM_OK);
    status_is("append its text", dom_append_child(&d, btn, bt), DOM_OK);
    status_is("set an attribute", dom_set_attribute(&d, btn, S("type"), S("submit")), DOM_OK);
    status_is("set another", dom_set_attribute(&d, p, S("class"), S("intro \"quoted\"")), DOM_OK);
    check("the revision counts each mutation (6 creates, 6 appends, 2 attributes)", dom_revision(&d) == 14);
    check("and the log holds one event a mutation", mutation_len(d) == 14);
    check("the invariants hold", dom_check_invariants(&d, stdout) == 0);

    /* the contract's refusals: each changes nothing */
    uint64_t r0 = dom_revision(&d);
    status_is("a node into its own descendant", dom_append_child(&d, p, body), DOM_HAS_PARENT);
    dom_node_h orphan = dom_create_element(&d, S("div"));
    status_is("an ancestor under its descendant", dom_append_child(&d, orphan, orphan), DOM_WOULD_CYCLE);
    status_is("a child appended twice", dom_append_child(&d, html, body), DOM_HAS_PARENT);
    status_is("a child under a text node", dom_append_child(&d, t, orphan), DOM_WRONG_KIND);
    status_is("the document under an element", dom_append_child(&d, orphan, root), DOM_WRONG_KIND);
    status_is("an attribute on a text node", dom_set_attribute(&d, t, S("x"), S("y")), DOM_WRONG_KIND);
    status_is("an attribute with no name", dom_set_attribute(&d, p, S(""), S("y")), DOM_INVALID_NAME);
    status_is("an element named with a space", dom_check_create_element(&d, S("a b")), DOM_INVALID_NAME);
    status_is("insert before a node that is not a child", dom_insert_before(&d, html, orphan, p), DOM_NOT_A_CHILD);
    status_is("remove a node from what is not its parent", dom_remove_child(&d, html, p), DOM_NOT_A_CHILD);
    status_is("destroy a node still in the tree", dom_destroy(&d, p), DOM_HAS_PARENT);
    status_is("remove an attribute that is not there", dom_remove_attribute(&d, p, S("id")), DOM_NOT_FOUND);
    check("no refusal moved the revision (one create did)", dom_revision(&d) == r0 + 1);
    check("the invariants hold after the refusals", dom_check_invariants(&d, stdout) == 0);

    /* insert, remove, attributes */
    dom_node_h h1 = dom_create_element(&d, S("h1"));
    status_is("insert h1 before p", dom_insert_before(&d, body, h1, p), DOM_OK);
    status_is("give h1 text to escape", dom_append_child(&d, h1, dom_create_text(&d, S("Fish & <chips>"))), DOM_OK);
    status_is("and an attribute to escape", dom_set_attribute(&d, h1, S("title"), S("say \"hi\" & 'bye'")), DOM_OK);
    check("h1 is body's first child, p its next", dom_children_first(&d, body).index == h1.index && dom_children_next(&d, h1).index == p.index);
    check("body has three children", dom_children_count(&d, body) == 3);
    status_is("replace an attribute's value", dom_set_attribute(&d, btn, S("type"), S("button")), DOM_OK);
    check("the button still has one attribute", dom_attributes_count(&d, btn) == 1);
    status_is("remove the class", dom_remove_attribute(&d, p, S("class")), DOM_OK);
    check("p has no attribute", dom_attributes_count(&d, p) == 0);
    status_is("change a text's data", dom_set_data(&d, bt, S("Send")), DOM_OK);
    check("the invariants hold", dom_check_invariants(&d, stdout) == 0);

    /* stale identities: a destroyed subtree's handles are stale, a reused slot too */
    status_is("detach p", dom_remove_child(&d, body, p), DOM_OK);
    uint32_t p_slot = p.index, t_slot = t.index;
    status_is("destroy the detached p", dom_destroy(&d, p), DOM_OK);
    check("p's handle is stale", !node_live(d, p));
    check("its text's handle is stale too", !node_live(d, t));
    status_is("an operation on the stale handle", dom_append_child(&d, body, p), DOM_STALE_HANDLE);
    status_is("and on the stale text", dom_set_data(&d, t, S("x")), DOM_STALE_HANDLE);
    dom_node_h n1 = dom_create_element(&d, S("section"));
    dom_node_h n2 = dom_create_element(&d, S("aside"));
    check("the freed slots are reused", (n1.index == p_slot || n1.index == t_slot) && (n2.index == p_slot || n2.index == t_slot));
    check("a reused slot has a new generation", (n1.index == p_slot && n1.gen != p.gen) || (n1.index == t_slot && n1.gen != t.gen));
    check("the old handle is still stale over the reused slot", !node_live(d, p) && !node_live(d, t));
    status_is("append the new section", dom_append_child(&d, body, n1), DOM_OK);
    check("the invariants hold after destroy and reuse", dom_check_invariants(&d, stdout) == 0);

    /* serialization */
    printf("serialized: ");
    dom_serialize(&d, root, stdout);
    printf("\n");

    /* facts: the snapshot of the actual state */
    printf("-- snapshot\n");
    dom_export(&d, stdout);

    /* requests: the dispatcher's answers */
    printf("-- requests\n");
    uint64_t rev = dom_revision(&d);
    char req[256];
    int len = snprintf(req, sizeof req, "amb_request(1, %llu, set_attribute(n(1,%u,%u), 'aria-label', 'Send it')).",
                       (unsigned long long)rev, (unsigned)btn.index, (unsigned)btn.gen);
    status_is("a valid request", dom_dispatch(&d, req, (size_t)len, rev, stdout), DOM_OK);
    status_is("the same request at the old revision", dom_dispatch(&d, req, (size_t)len, rev + 1, stdout), DOM_STALE_REVISION);
    len = snprintf(req, sizeof req, "amb_request(1, %llu, set_data(n(1,%u,%u), 'gone')).",
                   (unsigned long long)dom_revision(&d), (unsigned)t.index, (unsigned)t.gen);
    status_is("a request on a stale handle", dom_dispatch(&d, req, (size_t)len, dom_revision(&d), stdout), DOM_STALE_HANDLE);
    len = snprintf(req, sizeof req, "amb_request(1, %llu, destroy(n(1,%u,%u))).",
                   (unsigned long long)dom_revision(&d), (unsigned)orphan.index, (unsigned)orphan.gen);
    status_is("a request for an operation that is not requestable", dom_dispatch(&d, req, (size_t)len, dom_revision(&d), stdout), DOM_NOT_REQUESTABLE);
    len = snprintf(req, sizeof req, "amb_request(1, %llu, set_data(n(1,2), x)).", (unsigned long long)dom_revision(&d));
    status_is("a malformed request", dom_dispatch(&d, req, (size_t)len, dom_revision(&d), stdout), DOM_BAD_REQUEST);
    len = snprintf(req, sizeof req, "amb_request(1, %llu, launch(n(1,2,0))).", (unsigned long long)dom_revision(&d));
    status_is("a request for an operation the contract has not", dom_dispatch(&d, req, (size_t)len, dom_revision(&d), stdout), DOM_BAD_REQUEST);
    status_is("a request for another document", dom_dispatch(&d, S("amb_request(9, 0, set_data(n(9,2,0), x))."), 0, stdout), DOM_BAD_REQUEST);
    check("the invariants hold at the end", dom_check_invariants(&d, stdout) == 0);

    printf("%s: %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}
