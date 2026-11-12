/* src/dom/dom.c -- the DOM's operations, over the interface contracts/dom.pl
   generates (amb_implementation below: its types, its store, its checks, its
   lists, its invariants, its exporter and its request dispatcher).

   Each operation is the same three steps: the generated check of the contract's
   preconditions, which refuses with their status and changes nothing; the
   change, through the store's generated functions (children_append,
   attributes_unlink, names_intern, text_add ...); and one logged event, which
   bumps the revision. Nothing here holds a pointer into the store across a
   call that may grow it: the safe part refuses such a borrow at compile time.

   An element has a namespace (HTML's, SVG's or MathML's) and an attribute one
   of its own (none, or XLink's, XML's or XMLNS's); an attribute's name is its
   qualified name. An HTML template is made with its contents: a fragment node,
   the root of a tree of its own, whose host is the template (HTML 4.12.3). The
   template holds its contents as it holds its children: destroying it destroys
   them, a node may not be inserted under its own template's contents, and the
   serialization of a template is that of its contents. */
#include <amb.pl>

amb_implementation(dom);

static int dom_name_is(const dom_document *d, uint32_t name, const char *w) {
    return names_is(d, name, w, strlen(w));
}

/* the details of the changes kept from now on: the details' table begun (its first record
   says so; a change's record follows its own) */
dom_status dom_keep_details(dom_document *d) {
    if (d->detail_len <= 1) detail_new(d);
    return DOM_OK;
}
/* a change's details' record (they are kept): its index. DOM_DETAIL asks only while they
   are, so a change costs a comparison until then */
static uint32_t dom_detail(dom_document *d, uint32_t prev, uint32_t next, uint32_t name, uint32_t ns, amb_span old, int had) {
    dom_detail_h h = detail_new(d);
    d->detail[h.index].prev = prev;
    d->detail[h.index].next = next;
    d->detail[h.index].name = name;
    d->detail[h.index].ns = ns;
    d->detail[h.index].old = old;
    d->detail[h.index].had = (uint8_t)had;
    return h.index;
}
#define DOM_DETAIL(d, prev, next, name, ns, old, had) ((d)->detail_len > 1 ? dom_detail((d), (prev), (next), (name), (ns), (old), (had)) : 0u)
/* a mutation logged with no more than its target and other node: a node made, a subtree freed */
static void dom_log(dom_document *d, dom_op op, uint32_t target, uint32_t other) { dom_log_mutation(d, op, target, other, 0); }

/* an element of a namespace; an HTML template, with its contents */
static dom_node_h dom_new_element(dom_document *d, dom_namespace ns, dom_op op, const char *name, size_t name_len) {
    uint32_t a = names_intern(d, name, name_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = DOM_NODE_KIND_ELEMENT;
    d->node[h.index].name = a;
    d->node[h.index].ns = ns;
    if (ns == DOM_NAMESPACE_HTML && dom_name_is(d, a, "template")) {
        dom_node_h f = node_new(d);
        d->node[f.index].kind = DOM_NODE_KIND_FRAGMENT;
        d->node[f.index].host = h.index;
        d->node[h.index].content = f.index;
    }
    dom_log(d, op, h.index, 0);
    return h;
}

dom_node_h dom_create_element(dom_document *d, const char *name, size_t name_len) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_create_element(d, name, name_len) != DOM_OK) return none;
    return dom_new_element(d, DOM_NAMESPACE_HTML, DOM_OP_CREATE_ELEMENT, name, name_len);
}

dom_node_h dom_create_element_ns(dom_document *d, dom_namespace ns, const char *name, size_t name_len) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_create_element_ns(d, ns, name, name_len) != DOM_OK) return none;
    return dom_new_element(d, ns, DOM_OP_CREATE_ELEMENT_NS, name, name_len);
}

/* a node of a kind that holds data: text or comment */
static dom_node_h dom_new_data_node(dom_document *d, dom_node_kind kind, dom_op op, const char *data, size_t data_len) {
    amb_span sp = text_add(d, data, data_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = kind;
    d->node[h.index].data = sp;
    dom_log(d, op, h.index, 0);
    return h;
}

dom_node_h dom_create_text(dom_document *d, const char *data, size_t data_len) {
    return dom_new_data_node(d, DOM_NODE_KIND_TEXT, DOM_OP_CREATE_TEXT, data, data_len);
}

dom_node_h dom_create_comment(dom_document *d, const char *data, size_t data_len) {
    return dom_new_data_node(d, DOM_NODE_KIND_COMMENT, DOM_OP_CREATE_COMMENT, data, data_len);
}

dom_node_h dom_create_doctype(dom_document *d, const char *name, size_t name_len, const char *public_id,
                              size_t public_id_len, const char *system_id, size_t system_id_len, uint8_t ids) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_create_doctype(d, name, name_len, public_id, public_id_len, system_id, system_id_len, ids) != DOM_OK)
        return none;
    uint32_t a = names_intern(d, name, name_len);
    amb_span pub = text_add(d, public_id, public_id_len);
    amb_span sys = text_add(d, system_id, system_id_len);
    dom_node_h h = node_new(d);
    d->node[h.index].kind = DOM_NODE_KIND_DOCTYPE;
    d->node[h.index].name = a;
    d->node[h.index].public_id = pub;
    d->node[h.index].system_id = sys;
    d->node[h.index].doctype_ids = ids;
    dom_log(d, DOM_OP_CREATE_DOCTYPE, h.index, 0);
    return h;
}

/* child is an inclusive ancestor of parent through a template: the DOM's
   host-including inclusive ancestor, whose chain goes on from a template's
   contents to the template. The generated check has walked the parent links
   alone; a child with no children and no contents is no one's ancestor. */
static int dom_holds_through_template(const dom_document *d, uint32_t child, uint32_t parent) {
    if (!d->node[child].first_child && !d->node[child].content) return 0;
    size_t steps = 0;
    for (uint32_t x = parent; x;) {
        if (x == child || ++steps > d->node_len) return 1;
        uint32_t up = d->node[x].parent;
        if (!up) up = d->node[x].host;
        x = up;
    }
    return 0;
}

dom_status dom_append_child(dom_document *d, dom_node_h parent, dom_node_h child) {
    dom_status st = dom_check_append_child(d, parent, child);
    if (st != DOM_OK) return st;
    if (dom_holds_through_template(d, child.index, parent.index)) return DOM_WOULD_CYCLE;
    children_append(d, parent.index, child.index);
    amb_span none = { 0, 0 };
    dom_log_mutation(d, DOM_OP_APPEND_CHILD, parent.index, child.index, DOM_DETAIL(d, d->node[child.index].prev_sibling, 0, 0, 0, none, 0));
    return DOM_OK;
}

dom_status dom_insert_before(dom_document *d, dom_node_h parent, dom_node_h child, dom_node_h before) {
    dom_status st = dom_check_insert_before(d, parent, child, before);
    if (st != DOM_OK) return st;
    if (dom_holds_through_template(d, child.index, parent.index)) return DOM_WOULD_CYCLE;
    children_insert_before(d, parent.index, child.index, before.index);
    amb_span none = { 0, 0 };
    dom_log_mutation(d, DOM_OP_INSERT_BEFORE, parent.index, child.index,
                     DOM_DETAIL(d, d->node[child.index].prev_sibling, d->node[child.index].next_sibling, 0, 0, none, 0));
    return DOM_OK;
}

dom_status dom_remove_child(dom_document *d, dom_node_h parent, dom_node_h child) {
    dom_status st = dom_check_remove_child(d, parent, child);
    if (st != DOM_OK) return st;
    uint32_t prev = d->node[child.index].prev_sibling, next = d->node[child.index].next_sibling;
    children_unlink(d, child.index);
    amb_span none = { 0, 0 };
    dom_log_mutation(d, DOM_OP_REMOVE_CHILD, parent.index, child.index, DOM_DETAIL(d, prev, next, 0, 0, none, 0));
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

/* a template's contents, and a host's shadow root, taken in as its last children, for
   the walk that frees them with it */
static void dom_take_contents(dom_document *d, uint32_t x) {
    uint32_t c = d->node[x].content;
    if (c) {
        d->node[x].content = 0;
        d->node[c].host = 0;
        children_append(d, x, c);
    }
    for (uint32_t s = 1; s < d->shadow_len; s++) {
        if (!d->shadow[s].live || d->shadow[s].host != x) continue;
        uint32_t r = d->shadow[s].root;
        shadow_free(d, shadow_handle(d, s));
        if (r && d->node[r].live) {
            d->node[r].host = 0;
            children_append(d, x, r);
        }
    }
}

dom_node_h dom_attach_shadow(dom_document *d, dom_node_h host, dom_shadow_mode mode) {
    dom_node_h none = { 0, 0, 0 };
    if (dom_check_attach_shadow(d, host, mode) != DOM_OK) return none;
    dom_node_h r = node_new(d);
    d->node[r.index].kind = DOM_NODE_KIND_FRAGMENT;
    d->node[r.index].host = host.index;
    dom_shadow_h s = shadow_new(d);
    d->shadow[s.index].host = host.index;
    d->shadow[s.index].root = r.index;
    d->shadow[s.index].mode = mode;
    dom_log(d, DOM_OP_ATTACH_SHADOW, host.index, r.index);
    return r;
}

/* a detached subtree freed, children before their parent, a template's
   contents with it: each slot's generation moves on, so every handle to the
   subtree is stale from here */
dom_status dom_destroy(dom_document *d, dom_node_h node) {
    dom_status st = dom_check_destroy(d, node);
    if (st != DOM_OK) return st;
    uint32_t root = node.index;
    uint32_t x = root;
    for (;;) {
        dom_take_contents(d, x);
        while (d->node[x].first_child) { x = d->node[x].first_child; dom_take_contents(d, x); }
        uint32_t up = d->node[x].parent;
        uint32_t next = d->node[x].next_sibling;
        if (x != root) children_unlink(d, x);
        dom_free_attrs(d, x);
        node_free(d, node_handle(d, x));
        if (x == root) break;
        x = next ? next : up;
    }
    dom_log(d, DOM_OP_DESTROY, root, 0);
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
            amb_span old = d->attr[x].value;
            d->attr[x].value = v;
            dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE, element.index, x, DOM_DETAIL(d, 0, 0, a, (uint32_t)d->attr[x].ns, old, 1));
            return DOM_OK;
        }
    }
    dom_attr_h h = attr_new(d);
    d->attr[h.index].name = a;
    d->attr[h.index].value = v;
    attributes_append(d, element.index, h.index);
    amb_span none = { 0, 0 };
    dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE, element.index, h.index, DOM_DETAIL(d, 0, 0, a, 0, none, 0));
    return DOM_OK;
}

/* the attribute of that namespace and qualified name set, or added */
dom_status dom_set_attribute_ns(dom_document *d, dom_node_h element, dom_attr_namespace ns, const char *name, size_t name_len,
                                const char *value, size_t value_len) {
    dom_status st = dom_check_set_attribute_ns(d, element, ns, name, name_len, value, value_len);
    if (st != DOM_OK) return st;
    uint32_t a = names_intern(d, name, name_len);
    amb_span v = text_add(d, value, value_len);
    for (uint32_t x = d->node[element.index].first_attr; x; x = d->attr[x].next) {
        if (d->attr[x].name == a && d->attr[x].ns == ns) {
            amb_span old = d->attr[x].value;
            d->attr[x].value = v;
            dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE_NS, element.index, x, DOM_DETAIL(d, 0, 0, a, (uint32_t)ns, old, 1));
            return DOM_OK;
        }
    }
    dom_attr_h h = attr_new(d);
    d->attr[h.index].name = a;
    d->attr[h.index].ns = ns;
    d->attr[h.index].value = v;
    attributes_append(d, element.index, h.index);
    amb_span none = { 0, 0 };
    dom_log_mutation(d, DOM_OP_SET_ATTRIBUTE_NS, element.index, h.index, DOM_DETAIL(d, 0, 0, a, (uint32_t)ns, none, 0));
    return DOM_OK;
}

dom_status dom_remove_attribute(dom_document *d, dom_node_h element, const char *name, size_t name_len) {
    dom_status st = dom_check_remove_attribute(d, element, name, name_len);
    if (st != DOM_OK) return st;
    uint32_t a = names_find(d, name, name_len);
    if (a == 0) return DOM_NOT_FOUND;
    for (uint32_t x = d->node[element.index].first_attr; x; x = d->attr[x].next) {
        if (d->attr[x].name == a) {
            amb_span old = d->attr[x].value;
            uint32_t ns = (uint32_t)d->attr[x].ns;
            attributes_unlink(d, x);
            attr_free(d, attr_handle(d, x));
            dom_log_mutation(d, DOM_OP_REMOVE_ATTRIBUTE, element.index, x, DOM_DETAIL(d, 0, 0, a, ns, old, 1));
            return DOM_OK;
        }
    }
    return DOM_NOT_FOUND;
}

dom_status dom_set_data(dom_document *d, dom_node_h node, const char *data, size_t data_len) {
    dom_status st = dom_check_set_data(d, node, data, data_len);
    if (st != DOM_OK) return st;
    amb_span sp = text_add(d, data, data_len);
    amb_span old = d->node[node.index].data;
    d->node[node.index].data = sp;
    dom_log_mutation(d, DOM_OP_SET_DATA, node.index, 0, DOM_DETAIL(d, 0, 0, 0, 0, old, 1));
    return DOM_OK;
}

/* ---- serialization (the HTML fragment serialization algorithm, WHATWG 13.3) ----
   An element of any of the three namespaces is written by its local name and
   its attributes by their qualified names, so a foreign element keeps its
   adjusted case (foreignObject, viewBox) and an attribute in a namespace its
   prefix (xlink:href). Only HTML elements are void or hold raw text. */

/* an HTML element that serializes as void */
static int dom_is_void(const dom_document *d, uint32_t x) {
    static const char *voids[] = { "area", "base", "br", "col", "embed", "hr", "img", "input",
                                   "link", "meta", "source", "track", "wbr", "basefont", "bgsound",
                                   "frame", "keygen", "param" };
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    for (size_t i = 0; i < sizeof voids / sizeof voids[0]; i++) if (dom_name_is(d, d->node[x].name, voids[i])) return 1;
    return 0;
}

/* text whose parent is one of these HTML elements is written as it is */
static int dom_is_raw_parent(const dom_document *d, uint32_t parent) {
    if (parent == 0 || d->node[parent].kind != DOM_NODE_KIND_ELEMENT || d->node[parent].ns != DOM_NAMESPACE_HTML) return 0;
    static const char *raws[] = { "style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext" };
    for (size_t i = 0; i < sizeof raws / sizeof raws[0]; i++) if (dom_name_is(d, d->node[parent].name, raws[i])) return 1;
    return 0;
}

/* & and U+00A0, < and > escaped; in an attribute's value " too */
static void dom_escape(FILE *out, const char *p, size_t n, int in_attr) {
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (c == '&') fputs("&amp;", out);
        else if ((unsigned char)c == 0xC2 && i + 1 < n && (unsigned char)p[i + 1] == 0xA0) { fputs("&nbsp;", out); i++; }
        else if (in_attr && c == '"') fputs("&quot;", out);
        else if (c == '<') fputs("&lt;", out);
        else if (c == '>') fputs("&gt;", out);
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
    if (r.kind == DOM_NODE_KIND_ELEMENT && !dom_is_void(d, x)) {
        fputs("</", out);
        fputs(names_ptr(d, r.name), out);
        fputc('>', out);
    }
}

/* what is written under x: nothing for a void element, a template's contents'
   children, or its own */
static uint32_t dom_first_written(const dom_document *d, uint32_t x) {
    if (dom_is_void(d, x)) return 0;
    uint32_t c = d->node[x].content;
    return c ? d->node[c].first_child : d->node[x].first_child;
}

/* the subtree in document order, with no stack: down by first_child, across by
   next_sibling, up by parent -- and from a template's contents back to the
   template, by the fragment's host */
dom_status dom_serialize(const dom_document *d, dom_node_h node, FILE *out) {
    dom_status st = dom_check_serialize(d, node, out);
    if (st != DOM_OK) return st;
    uint32_t root = node.index;
    uint32_t x = root;
    for (;;) {
        dom_open(d, x, out);
        uint32_t first = dom_first_written(d, x);
        if (first) { x = first; continue; }
        for (;;) {
            dom_close(d, x, out);
            if (x == root) return DOM_OK;
            if (d->node[x].next_sibling) { x = d->node[x].next_sibling; break; }
            uint32_t up = d->node[x].parent;
            if (up != root && d->node[up].kind == DOM_NODE_KIND_FRAGMENT && d->node[up].host) up = d->node[up].host;
            x = up;
        }
    }
}
