/* src/dom/dom.c -- the DOM's operations, over the interface contracts/dom.pl
   generates (amb_implementation below: its types, its store, its checks, its
   lists, its invariants, its exporter and its request dispatcher).

   Each operation is the same three steps: the generated check of the contract's
   preconditions, which refuses with their status and changes nothing; the
   change, through the store's generated functions (children_append,
   attributes_unlink, names_intern, text_add ...); and one logged event, which
   bumps the revision. Nothing here holds a pointer into the store across a
   call that may grow it: the safe part refuses such a borrow at compile time. */
#include <amb.pl>

amb_implementation(dom);

dom_node_h dom_create_element(dom_document *d, const char *name, size_t name_len) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_create_element(d, name, name_len) != DOM_OK) return none;
    uint32_t a = names_intern(d, name, name_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = DOM_NODE_KIND_ELEMENT;
    d->node[h.index].name = a;
    dom_log_mutation(d, DOM_OP_CREATE_ELEMENT, h.index, 0);
    return h;
}

/* a node of a kind that holds data: text or comment */
static dom_node_h dom_new_data_node(dom_document *d, dom_node_kind kind, dom_op op, const char *data, size_t data_len) {
    amb_span sp = text_add(d, data, data_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = kind;
    d->node[h.index].data = sp;
    dom_log_mutation(d, op, h.index, 0);
    return h;
}

dom_node_h dom_create_text(dom_document *d, const char *data, size_t data_len) {
    return dom_new_data_node(d, DOM_NODE_KIND_TEXT, DOM_OP_CREATE_TEXT, data, data_len);
}

dom_node_h dom_create_comment(dom_document *d, const char *data, size_t data_len) {
    return dom_new_data_node(d, DOM_NODE_KIND_COMMENT, DOM_OP_CREATE_COMMENT, data, data_len);
}

dom_node_h dom_create_doctype(dom_document *d, const char *name, size_t name_len) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_create_doctype(d, name, name_len) != DOM_OK) return none;
    uint32_t a = names_intern(d, name, name_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = DOM_NODE_KIND_DOCTYPE;
    d->node[h.index].name = a;
    dom_log_mutation(d, DOM_OP_CREATE_DOCTYPE, h.index, 0);
    return h;
}

dom_status dom_append_child(dom_document *d, dom_node_h parent, dom_node_h child) {
    dom_status st = dom_check_append_child(d, parent, child);
    if (st != DOM_OK) return st;
    children_append(d, parent.index, child.index);
    dom_log_mutation(d, DOM_OP_APPEND_CHILD, parent.index, child.index);
    return DOM_OK;
}

dom_status dom_insert_before(dom_document *d, dom_node_h parent, dom_node_h child, dom_node_h before) {
    dom_status st = dom_check_insert_before(d, parent, child, before);
    if (st != DOM_OK) return st;
    children_insert_before(d, parent.index, child.index, before.index);
    dom_log_mutation(d, DOM_OP_INSERT_BEFORE, parent.index, child.index);
    return DOM_OK;
}

dom_status dom_remove_child(dom_document *d, dom_node_h parent, dom_node_h child) {
    dom_status st = dom_check_remove_child(d, parent, child);
    if (st != DOM_OK) return st;
    children_unlink(d, child.index);
    dom_log_mutation(d, DOM_OP_REMOVE_CHILD, parent.index, child.index);
    return DOM_OK;
}

/* every attribute of the node n, freed */
static void dom_free_attrs(dom_document *d, uint32_t n) {
    uint32_t a = d->node[n].first_attr;
    while (a) {
        uint32_t next = d->attr[a].next;
        attributes_unlink(d, a);
        attr_free(d, attr_handle(d, a));
        a = next;
    }
}

/* a detached subtree freed, children before their parent: each slot's
   generation moves on, so every handle to the subtree is stale from here */
dom_status dom_destroy(dom_document *d, dom_node_h node) {
    dom_status st = dom_check_destroy(d, node);
    if (st != DOM_OK) return st;
    uint32_t root = node.index;
    uint32_t x = root;
    for (;;) {
        while (d->node[x].first_child) x = d->node[x].first_child;
        uint32_t up = d->node[x].parent;
        uint32_t next = d->node[x].next_sibling;
        if (x != root) children_unlink(d, x);
        dom_free_attrs(d, x);
        node_free(d, node_handle(d, x));
        if (x == root) break;
        x = next ? next : up;
    }
    dom_log_mutation(d, DOM_OP_DESTROY, root, 0);
    return DOM_OK;
}

dom_status dom_set_attribute(dom_document *d, dom_node_h element, const char *name, size_t name_len,
                             const char *value, size_t value_len) {
    dom_status st = dom_check_set_attribute(d, element, name, name_len, value, value_len);
    if (st != DOM_OK) return st;
    uint32_t a = names_intern(d, name, name_len);
    amb_span v = text_add(d, value, value_len);
    for (uint32_t x = d->node[element.index].first_attr; x; x = d->attr[x].next) {
        if (d->attr[x].name == a) {
            d->attr[x].value = v;
            dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE, element.index, x);
            return DOM_OK;
        }
    }
    dom_attr_h h = attr_new(d);
    d->attr[h.index].name = a;
    d->attr[h.index].value = v;
    attributes_append(d, element.index, h.index);
    dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE, element.index, h.index);
    return DOM_OK;
}

dom_status dom_remove_attribute(dom_document *d, dom_node_h element, const char *name, size_t name_len) {
    dom_status st = dom_check_remove_attribute(d, element, name, name_len);
    if (st != DOM_OK) return st;
    uint32_t a = names_find(d, name, name_len);
    if (a == 0) return DOM_NOT_FOUND;
    for (uint32_t x = d->node[element.index].first_attr; x; x = d->attr[x].next) {
        if (d->attr[x].name == a) {
            attributes_unlink(d, x);
            attr_free(d, attr_handle(d, x));
            dom_log_mutation(d, DOM_OP_REMOVE_ATTRIBUTE, element.index, x);
            return DOM_OK;
        }
    }
    return DOM_NOT_FOUND;
}

dom_status dom_set_data(dom_document *d, dom_node_h node, const char *data, size_t data_len) {
    dom_status st = dom_check_set_data(d, node, data, data_len);
    if (st != DOM_OK) return st;
    amb_span sp = text_add(d, data, data_len);
    d->node[node.index].data = sp;
    dom_log_mutation(d, DOM_OP_SET_DATA, node.index, 0);
    return DOM_OK;
}

/* ---- serialization (the HTML fragment serialization algorithm, WHATWG 13.3) ---- */

static int dom_name_is(const dom_document *d, uint32_t name, const char *w) {
    return names_is(d, name, w, strlen(w));
}

static int dom_is_void(const dom_document *d, uint32_t name) {
    static const char *voids[] = { "area", "base", "br", "col", "embed", "hr", "img", "input",
                                   "link", "meta", "source", "track", "wbr", "basefont", "bgsound",
                                   "frame", "keygen", "param" };
    for (size_t i = 0; i < sizeof voids / sizeof voids[0]; i++) if (dom_name_is(d, name, voids[i])) return 1;
    return 0;
}

/* text whose parent is one of these is written as it is */
static int dom_is_raw_parent(const dom_document *d, uint32_t parent) {
    if (parent == 0 || d->node[parent].kind != DOM_NODE_KIND_ELEMENT) return 0;
    static const char *raws[] = { "style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext" };
    for (size_t i = 0; i < sizeof raws / sizeof raws[0]; i++) if (dom_name_is(d, d->node[parent].name, raws[i])) return 1;
    return 0;
}

static void dom_escape(FILE *out, const char *p, size_t n, int in_attr) {
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (c == '&') fputs("&amp;", out);
        else if ((unsigned char)c == 0xC2 && i + 1 < n && (unsigned char)p[i + 1] == 0xA0) { fputs("&nbsp;", out); i++; }
        else if (in_attr && c == '"') fputs("&quot;", out);
        else if (!in_attr && c == '<') fputs("&lt;", out);
        else if (!in_attr && c == '>') fputs("&gt;", out);
        else fputc(c, out);
    }
}

static void dom_open(const dom_document *d, uint32_t x, FILE *out) {
    dom_node r = d->node[x];
    if (r.kind == DOM_NODE_KIND_ELEMENT) {
        fputc('<', out);
        fputs(names_ptr(d, r.name), out);
        for (uint32_t a = r.first_attr; a; a = d->attr[a].next) {
            fputc(' ', out);
            fputs(names_ptr(d, d->attr[a].name), out);
            fputs("=\"", out);
            dom_escape(out, text_ptr(d, d->attr[a].value), d->attr[a].value.len, 1);
            fputc('"', out);
        }
        fputc('>', out);
    } else if (r.kind == DOM_NODE_KIND_TEXT) {
        if (dom_is_raw_parent(d, r.parent)) fwrite(text_ptr(d, r.data), 1, r.data.len, out);
        else dom_escape(out, text_ptr(d, r.data), r.data.len, 0);
    } else if (r.kind == DOM_NODE_KIND_COMMENT) {
        fputs("<!--", out);
        fwrite(text_ptr(d, r.data), 1, r.data.len, out);
        fputs("-->", out);
    } else if (r.kind == DOM_NODE_KIND_DOCTYPE) {
        fputs("<!DOCTYPE ", out);
        fputs(names_ptr(d, r.name), out);
        fputc('>', out);
    }
}

static void dom_close(const dom_document *d, uint32_t x, FILE *out) {
    dom_node r = d->node[x];
    if (r.kind == DOM_NODE_KIND_ELEMENT && !dom_is_void(d, r.name)) {
        fputs("</", out);
        fputs(names_ptr(d, r.name), out);
        fputc('>', out);
    }
}

/* the subtree in document order, with no stack: down by first_child, across by
   next_sibling, up by parent */
dom_status dom_serialize(const dom_document *d, dom_node_h node, FILE *out) {
    dom_status st = dom_check_serialize(d, node, out);
    if (st != DOM_OK) return st;
    uint32_t root = node.index;
    uint32_t x = root;
    for (;;) {
        dom_open(d, x, out);
        if (d->node[x].first_child && !(d->node[x].kind == DOM_NODE_KIND_ELEMENT && dom_is_void(d, d->node[x].name))) {
            x = d->node[x].first_child;
            continue;
        }
        for (;;) {
            dom_close(d, x, out);
            if (x == root) return DOM_OK;
            if (d->node[x].next_sibling) { x = d->node[x].next_sibling; break; }
            x = d->node[x].parent;
        }
    }
}
