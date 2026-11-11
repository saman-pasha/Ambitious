/* a document dropped by hand: its scope drops it too, which would free its
   buffers twice */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
int main(void) {
    dom_document d;
    dom_init(&d, 1);
    store_dom_store_tag_drop(&d);
    return 0;
}
