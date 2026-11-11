/* a pointer into the text pool, read after another add to the pool */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
int main(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_node_h t = dom_create_text(&d, "hello", 5);
    const char *p = text_ptr(d, d.node[t.index].data);
    dom_create_text(&d, "more", 4);
    return p[0];
}
