/* src/dom/dom.h -- what the DOM module (src/dom/dom.c) offers other units beside its
   contract's operations: the walk of the flat tree, the one tree the cascade, layout and
   accessibility walk. A unit that includes this has said amb_interface(dom) first.

   THE FLAT TREE (DOM 4.2.2, CSS Scoping 1, 2.2). A shadow host's children in it are its
   shadow root's; a slot's are its assigned nodes, or, when none is assigned it, its own
   (its fallback); every other node's are its own. A host's child no slot takes, and a
   slot's fallback while nodes are assigned it, are in no flat tree. A slot is an element of
   the flat tree (the user agent's sheet makes it display: contents). Its links are found by
   dom_flatten (the contract's flatten), which a walker calls first: they hold until the
   document changes. In a document without shadow roots the flat tree is the document's.
   The walk reads the nodes' links in place (a slot's assigned nodes are the only ones with
   a slot set, a slot the only node with assigned_first), so it costs what a walk of the
   DOM's own links does. */
#ifndef AMB_DOM_H
#define AMB_DOM_H
#include <stdint.h>
#include <stddef.h>

/* x is a shadow root: a fragment its host's shadow link names (a template's contents are not) */
static inline int dom_is_shadow_root(const dom_document *d, uint32_t x) {
    uint32_t h = d->node[x].host;
    return h && d->node[x].kind == DOM_NODE_KIND_FRAGMENT && h < d->node_len && d->node[h].shadow == x;
}
/* x's first child in the flat tree, its next sibling there, and its parent there (0 for none:
   the document, a node in no flat tree, a shadow root) */
static inline uint32_t dom_flat_first(const dom_document *d, uint32_t x) {
    uint32_t r = d->node[x].shadow;
    if (r) return d->node[r].first_child;
    uint32_t a = d->node[x].assigned_first;
    return a ? a : d->node[x].first_child;
}
static inline uint32_t dom_flat_next(const dom_document *d, uint32_t x) {
    return d->node[x].slot ? d->node[x].assigned_next : d->node[x].next_sibling;
}
static inline uint32_t dom_flat_parent(const dom_document *d, uint32_t x) {
    uint32_t s = d->node[x].slot;
    if (s) return s;
    uint32_t p = d->node[x].parent;
    if (!p) return 0;
    if (d->node[p].kind == DOM_NODE_KIND_FRAGMENT) return dom_is_shadow_root(d, p) ? d->node[p].host : p;
    /* a host's child no slot takes, and a slot's fallback while nodes are assigned it */
    if (d->node[p].shadow || d->node[p].assigned_first) return 0;
    return p;
}
/* x's parent in the flat tree when it is an element, else 0 */
static inline uint32_t dom_flat_element_parent(const dom_document *d, uint32_t x) {
    uint32_t p = dom_flat_parent(d, x);
    return p && d->node[p].kind == DOM_NODE_KIND_ELEMENT ? p : 0;
}
/* the node after x in the flat tree's order (a node, then its flat children, each with its
   own), inside top's subtree there (top itself not counted again): 0 at its end */
static inline uint32_t dom_flat_walk(const dom_document *d, uint32_t x, uint32_t top) {
    uint32_t f = dom_flat_first(d, x);
    if (f) return f;
    for (uint32_t y = x; y && y != top;) {
        uint32_t n = dom_flat_next(d, y);
        if (n) return n;
        y = dom_flat_parent(d, y);
    }
    return 0;
}
/* the host of x when x is a shadow root, else 0 */
static inline uint32_t dom_shadow_root_host(const dom_document *d, uint32_t x) {
    return x && x < d->node_len && dom_is_shadow_root(d, x) ? d->node[x].host : 0;
}
/* x is in the document's flat tree: its chain of flat parents reaches the document */
static inline int dom_in_flat_tree(const dom_document *d, uint32_t x) {
    for (size_t steps = 0; x && x != 1 && steps <= d->node_len; steps++) x = dom_flat_parent(d, x);
    return x == 1;
}
/* the root of x's tree: the document node (1), a shadow root, a template's contents, or the
   top of a subtree that is in none */
static inline uint32_t dom_tree_root(const dom_document *d, uint32_t x) {
    for (size_t steps = 0; d->node[x].parent && steps <= d->node_len; steps++) x = d->node[x].parent;
    return x;
}

#endif
