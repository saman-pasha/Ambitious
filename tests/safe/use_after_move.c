/* a document used after it was moved: an object named whole by value moves,
   and leaves the old name empty */
#include <stdio.h>
#include <amb.pl>
amb_interface(dom);
int main(void) {
    dom_document d;
    dom_init(&d, 1);
    dom_document e = d;
    dom_create_element(&d, "p", 1);
    return (int)node_count(e);
}
