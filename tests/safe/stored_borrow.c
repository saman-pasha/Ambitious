/* a borrow of a document kept in a global, where the check cannot follow it */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
static const char *kept;
int main(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_node_h e = dom_create_element(&d, "p", 1);
    kept = names_ptr(d, d.node[e.index].name);
    return 0;
}
