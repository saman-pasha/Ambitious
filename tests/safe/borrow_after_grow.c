/* a pointer to a node's record, used after an operation that may grow the node
   table: the operation takes the store by a plain pointer, so it may replace the
   table's buffer, and the borrow ends at the call (iterator invalidation) */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
int main(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_node_h e = dom_create_element(&d, "p", 1);
    dom_node *r = node_ref(d, e);
    dom_create_element(&d, "q", 1);
    return (int)r->kind;
}
