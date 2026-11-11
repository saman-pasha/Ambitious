/* a name's text returned from the function whose document it belongs to: the
   document is dropped at the return, so the pointer would dangle */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
static const char *name_of(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_node_h e = dom_create_element(&d, "p", 1);
    return names_ptr(d, d.node[e.index].name);
}
int main(void) { return name_of()[0]; }
