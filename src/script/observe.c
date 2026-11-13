/* src/script/observe.c -- MutationObserver (DOM 4.3): the records of the DOM's changes a
   script's observers asked for, delivered at a microtask checkpoint.

   THE LOG. The DOM logs each of its operations (contracts/dom.pl's mutation event), and
   once the first observe() asks it to keep them (dom_keep_details), each one's details:
   a child's siblings where it went in or came out, an attribute's name and the value it
   had, a node's old data; until then a change costs the DOM one comparison more, and the
   host nothing. While an observer observes, the host turns the log into records at the
   start and the end of each call a script makes into the host (a native, a setter),
   before a listener runs and at each checkpoint: the operations of one call are one
   call's changes, as the DOM standard's algorithms queue their records. Within one call,
   the insertions of a run of nodes into a parent are one record (append(a, b), a
   fragment's children, innerHTML's nodes), and so are the removals of a run of children
   with the insertions in their place (textContent's and innerHTML's replace all,
   replaceChildren, replaceChild); a node put back where it was is a removal and an
   insertion; a record that takes an operation in is queued where its last one was. The
   call's log is replayed for the parents its nodes had at each change, so a subtree
   built apart and then inserted is one insertion.

   THE RECORDS: each change is offered to the registrations of its target's inclusive
   ancestors (subtree for those above it); an observer interested gets one record, with
   the old value when one of its registrations asked for it; childList, attributes (an
   attributeFilter's names), characterData. A child removed from an observed subtree takes
   the subtree's registrations with it, transient, until its observer is next notified.
   observe() again at the same node replaces its options; disconnect() drops the
   observer's registrations and its records; takeRecords() hands them over. The
   notification is a microtask, queued at an observer's first record since the last one:
   each observer with records, in the order they were made, called with them and itself.

   What is not done: a script made before the parse ended sees none of the parser's own
   insertions (the page is parsed whole before its scripts run). A box's or radio's
   checkedness is no attribute (the DOM's state, src/html/forms.c), so a click on one makes
   no record, as in Chromium. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>
amb_interface(dom);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "host.h"
#include "script.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);

enum { SO_CTOR = SH_OBSERVE_FIRST, SO_OBSERVE, SO_DISCONNECT, SO_TAKE, SO_NOTIFY, SO_ILLEGAL, SO_N };
/* a registration's options, as bits */
#define SO_CHILD 1u
#define SO_ATTR 2u
#define SO_DATA 4u
#define SO_SUBTREE 8u
#define SO_ATTR_OLD 16u
#define SO_DATA_OLD 32u
#define SO_FILTER 64u
/* a record's types */
enum { SO_T_CHILD = 1, SO_T_ATTR, SO_T_DATA };

static uint32_t so_atom(js_vm *vm, const char *s) { return js_atom_ascii(vm, s); }
static void so_def(js_vm *vm, uint32_t o, const char *name, jv v) { js_define(vm, o, so_atom(vm, name), v, 0); }
/* a node's wrapper while it is the node it was (its generation), else null */
static jv so_node(js_vm *vm, uint32_t x, uint32_t gen) {
    const dom_document *d = vm->host->d;
    if (!x || x >= d->node_len || !d->node[x].live || d->node[x].gen != gen) return JV_NULL;
    return sh_wrap(vm, x);
}
/* the observer of a MutationObserver object: its place in the host's list plus one, 0 for none */
static uint32_t so_this(js_vm *vm, jv thisv) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST || vm->objs[jv_idx(thisv)].c != HK_OBSERVER) return 0;
    return vm->objs[jv_idx(thisv)].a;
}

/* ---- the log, made records ---- */

/* the changes of one call grouped as the standard's records are: a group's nodes are its
   added (bit 31 clear) and removed (set) ones, in order */
static int so_mergeable_insert(const sh_mgroup *g, uint32_t last_added, uint32_t prev, uint32_t next) {
    if (g->type != SO_T_CHILD) return 0;
    if (g->nadd) return last_added == prev;
    return g->nrem && g->prev == prev && g->next == next;
}
static int so_mergeable_remove(const sh_mgroup *g, uint32_t last_added, uint32_t c, uint32_t prev) {
    if (g->type != SO_T_CHILD || g->next != c) return 0;
    if (g->nadd) return last_added == prev;
    return g->nrem && g->prev == prev;
}
/* whether a registration of an observer at node y offers it a change of target of type t
   (attribute name and namespace for attributes) */
static int so_offers(struct js_host *h, const sh_mreg *r, uint32_t y, uint32_t target, uint32_t t, uint32_t name, uint32_t ns) {
    const dom_document *d = h->d;
    if (y != target && !(r->opts & SO_SUBTREE)) return 0;
    if (t == SO_T_ATTR) {
        if (!(r->opts & SO_ATTR)) return 0;
        if (r->opts & SO_FILTER) {
            if (ns) return 0;
            int found = 0;
            size_t i = r->foff, end = (size_t)r->foff + r->flen;
            while (i < end && !found) {
                size_t e = i;
                while (e < end && h->mfilter[e]) e++;
                if (e - i == names_len(d, name) && memcmp(data(h->mfilter) + i, names_ptr(d, name), e - i) == 0) found = 1;
                i = e + 1;
            }
            if (!found) return 0;
        }
        return 1;
    }
    if (t == SO_T_DATA) return (r->opts & SO_DATA) != 0;
    return (r->opts & SO_CHILD) != 0;
}
/* the parents of the nodes a call moved, as the log is replayed: each one's parent at
   the change being read (the others' are the tree's own throughout the call) */
static uint32_t so_par(const vector<uint32_t> *keys, const vector<uint32_t> *vals, const dom_document *d, uint32_t x) {
    for (size_t i = len(*keys); i > 0; i--) if ((*keys)[i - 1] == x) return (*vals)[i - 1];
    return d->node[x].parent;
}
static void so_par_set(vector<uint32_t> *keys, vector<uint32_t> *vals, uint32_t x, uint32_t p) {
    for (size_t i = len(*keys); i > 0; i--) if ((*keys)[i - 1] == x) { (*vals)[i - 1] = p; return; }
    push_back(*keys, x);
    push_back(*vals, p);
}
/* a removed child given transient registrations: each of its old parent's inclusive
   ancestors' with subtree, the ancestors as they were (DOM 4.2.3, remove) */
static void so_transient(struct js_host *h, const vector<uint32_t> *keys, const vector<uint32_t> *vals, uint32_t parent, uint32_t child) {
    const dom_document *d = h->d;
    size_t n = len(h->mregs);
    for (uint32_t y = parent; y; y = so_par(keys, vals, d, y))
        for (size_t i = 0; i < n; i++) {
            sh_mreg r = h->mregs[i];
            if (!r.live || r.node != y || r.gen != d->node[y].gen || !(r.opts & SO_SUBTREE)) continue;
            sh_mreg t = r;
            t.node = child;
            t.gen = d->node[child].gen;
            t.transient = 1;
            t.source = (uint32_t)i + 1;
            push_back(h->mregs, t);
            h->mreg_live++;
        }
}
/* a group's records: one for each observer a registration of its target's inclusive
   ancestors (as they are at the change) offers it to, with the old value when one of them
   asked for it; 1 when one was queued */
static int so_queue(struct js_host *h, const vector<uint32_t> *keys, const vector<uint32_t> *vals, const sh_mgroup *g, uint32_t gi,
                    const vector<uint32_t> *gn) {
    const dom_document *d = h->d;
    if (!g->target || g->target >= d->node_len || !d->node[g->target].live) return 0;
    vector<uint32_t> io, iold;
    for (uint32_t y = g->target; y; y = so_par(keys, vals, d, y))
        for (size_t i = 0; i < len(h->mregs); i++) {
            sh_mreg r = h->mregs[i];
            if (!r.live || r.node != y || r.gen != d->node[y].gen || !so_offers(h, &r, y, g->target, g->type, g->name, g->ns)) continue;
            int want = (g->type == SO_T_ATTR && (r.opts & SO_ATTR_OLD)) || (g->type == SO_T_DATA && (r.opts & SO_DATA_OLD));
            size_t at = len(io);
            for (size_t q = 0; q < len(io); q++) if (io[q] == r.mo) at = q;
            if (at == len(io)) { push_back(io, r.mo); push_back(iold, (uint32_t)want); }
            else if (want) iold[at] = 1;
        }
    for (size_t q = 0; q < len(io); q++) {
        sh_mrec rec;
        memset(&rec, 0, sizeof rec);
        rec.mo = io[q];
        rec.type = g->type;
        rec.target = g->target;
        rec.tgen = d->node[g->target].gen;
        rec.prev = g->prev;
        rec.pgen = g->pgen;
        rec.next = g->next;
        rec.ngen = g->ngen;
        rec.name = g->name;
        rec.ns = g->ns;
        rec.had = iold[q] && g->had;
        rec.old = g->old;
        rec.noff = (uint32_t)len(h->mnodes);
        for (size_t m = 0; m < len(*gn); m += 2) {
            if ((*gn)[m] != gi) continue;
            uint32_t x = (*gn)[m + 1] & 0x7FFFFFFFu;
            push_back(h->mnodes, (*gn)[m + 1]);
            push_back(h->mnodes, d->node[x].gen);
        }
        rec.nlen = (uint32_t)len(h->mnodes) - rec.noff;
        rec.live = 1;
        push_back(h->mrecs, rec);
    }
    return len(io) > 0;
}

void script_observe_flush(js_vm *vm) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    size_t n = d->mutation_len, from = h->mo_pos;
    if (from >= n) return;
    h->mo_pos = (uint32_t)n;
    if (!h->mreg_live) return;
    /* the changes grouped as the standard's records are; a group's nodes its added (bit
       31 clear) and removed (set) ones, in order */
    vector<sh_mgroup> gs;
    vector<uint32_t> gn;
    for (size_t i = from; i < n; i++) {
        dom_mutation e = d->mutation[i];
        dom_op op = e.op;
        /* a change before its details were kept is no observer's */
        if (!e.detail || e.detail >= d->detail_len) continue;
        if (op == DOM_OP_APPEND_CHILD || op == DOM_OP_INSERT_BEFORE || op == DOM_OP_REMOVE_CHILD) {
            int rem = op == DOM_OP_REMOVE_CHILD;
            /* the last group of the parent's, and its last added node */
            size_t g = len(gs);
            for (size_t k = len(gs); k > 0 && g == len(gs); k--) if (gs[k - 1].target == e.target) g = k - 1;
            uint32_t last = 0;
            if (g < len(gs)) for (size_t k = 0; k < len(gn); k += 2) if (gn[k] == g && !(gn[k + 1] & 0x80000000u)) last = gn[k + 1];
            /* the change's siblings, from its details */
            dom_detail dt = d->detail[e.detail];
            int merge = g < len(gs) && (rem ? so_mergeable_remove(&gs[g], last, e.other, dt.prev) : so_mergeable_insert(&gs[g], last, dt.prev, dt.next));
            /* a node put back where it was taken from is a move: two records, as Chromium's */
            for (size_t k = 0; merge && !rem && k < len(gn); k += 2) if (gn[k] == g && gn[k + 1] == (e.other | 0x80000000u)) merge = 0;
            if (!merge) {
                sh_mgroup ng;
                memset(&ng, 0, sizeof ng);
                ng.type = SO_T_CHILD;
                ng.target = e.target;
                ng.prev = dt.prev;
                ng.pgen = dt.prev ? d->node[dt.prev].gen : 0;
                push_back(gs, ng);
                g = len(gs) - 1;
            }
            gs[g].next = dt.next;
            gs[g].ngen = dt.next ? d->node[dt.next].gen : 0;
            gs[g].order = (uint32_t)i;
            if (rem) gs[g].nrem++;
            else gs[g].nadd++;
            push_back(gn, (uint32_t)g);
            push_back(gn, e.other | (rem ? 0x80000000u : 0));
        } else if (op == DOM_OP_SET_ATTRIBUTE || op == DOM_OP_SET_ATTRIBUTE_NS || op == DOM_OP_REMOVE_ATTRIBUTE || op == DOM_OP_SET_DATA) {
            sh_mgroup ng;
            memset(&ng, 0, sizeof ng);
            ng.type = op == DOM_OP_SET_DATA ? SO_T_DATA : SO_T_ATTR;
            ng.target = e.target;
            dom_detail dt = d->detail[e.detail];
            ng.name = dt.name;
            ng.ns = dt.ns;
            ng.had = dt.had;
            ng.old = dt.old;
            ng.order = (uint32_t)i;
            push_back(gs, ng);
        }
    }
    /* the moved nodes' parents as they were when the call began: the log undone */
    vector<uint32_t> keys, vals;
    for (size_t i = n; i > from; i--) {
        dom_mutation e = d->mutation[i - 1];
        if (e.op == DOM_OP_APPEND_CHILD || e.op == DOM_OP_INSERT_BEFORE) so_par_set(&keys, &vals, e.other, 0);
        else if (e.op == DOM_OP_REMOVE_CHILD) so_par_set(&keys, &vals, e.other, e.target);
    }
    /* the groups in the order of their last change; the log replayed, each group offered
       to the observers as the tree was at its last change, each removal giving its child
       the transient registrations of the subtree it left */
    vector<uint32_t> order;
    for (size_t g = 0; g < len(gs); g++) push_back(order, (uint32_t)g);
    for (size_t a = 1; a < len(order); a++)
        for (size_t b = a; b > 0 && gs[order[b - 1]].order > gs[order[b]].order; b--) {
            uint32_t t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
    int queued = 0;
    size_t k = 0;
    for (size_t i = from; i < n; i++) {
        dom_mutation e = d->mutation[i];
        if (e.op == DOM_OP_REMOVE_CHILD) {
            so_transient(h, &keys, &vals, e.target, e.other);
            so_par_set(&keys, &vals, e.other, 0);
        } else if (e.op == DOM_OP_APPEND_CHILD || e.op == DOM_OP_INSERT_BEFORE) so_par_set(&keys, &vals, e.other, e.target);
        while (k < len(order) && gs[order[k]].order == (uint32_t)i) {
            sh_mgroup g = gs[order[k]];
            if (so_queue(h, &keys, &vals, &g, order[k], &gn)) queued = 1;
            k++;
        }
    }
    /* the observers' microtask, queued once until it runs */
    if (queued && !h->mo_queued) {
        h->mo_queued = 1;
        js_job_push(vm, JOB_CALL, 0, jv_obj(h->mo_fn), JV_UNDEF, JV_UNDEF);
    }
}

/* ---- the records, as MutationRecords ---- */

static jv so_list(js_vm *vm, const sh_mrec *rec, int removed) {
    struct js_host *h = vm->host;
    uint32_t a = js_array_new(vm, 0);
    js_set_proto(vm, a, h->protos[HP_NODELIST]);
    for (uint32_t m = 0; m < rec->nlen; m += 2) {
        uint32_t x = h->mnodes[rec->noff + m];
        if (((x & 0x80000000u) != 0) != removed) continue;
        js_array_push(vm, a, so_node(vm, x & 0x7FFFFFFFu, h->mnodes[rec->noff + m + 1]));
    }
    return jv_obj(a);
}
static jv so_record(js_vm *vm, const sh_mrec *rec) {
    struct js_host *h = vm->host;
    const dom_document *d = h->d;
    uint32_t o = js_obj_new(vm, JK_PLAIN, h->protos[HP_MUTATIONRECORD]);
    static const char *const types[] = { "", "childList", "attributes", "characterData" };
    so_def(vm, o, "type", sh_cstr(vm, types[rec->type]));
    so_def(vm, o, "target", so_node(vm, rec->target, rec->tgen));
    so_def(vm, o, "addedNodes", so_list(vm, rec, 0));
    so_def(vm, o, "removedNodes", so_list(vm, rec, 1));
    so_def(vm, o, "previousSibling", rec->type == SO_T_CHILD && rec->prev ? so_node(vm, rec->prev, rec->pgen) : JV_NULL);
    so_def(vm, o, "nextSibling", rec->type == SO_T_CHILD && rec->next ? so_node(vm, rec->next, rec->ngen) : JV_NULL);
    jv name = JV_NULL, ns = JV_NULL, old = JV_NULL;
    if (rec->type == SO_T_ATTR) {
        /* the local name: the qualified name's part after its prefix */
        size_t nl = names_len(d, rec->name), k = 0;
        vector<char> nb;    /* the name copied first: the document is reached through the vm, which sh_str may grow */
        append_range(nb, names_ptr(d, rec->name), nl);
        if (rec->ns) for (size_t i = 0; i < nl; i++) if (at(nb, i) == ':') k = i + 1;
        name = sh_str(vm, data(nb) + k, nl - k);
        static const char *const spaces[] = { "", "http://www.w3.org/1999/xlink", "http://www.w3.org/XML/1998/namespace", "http://www.w3.org/2000/xmlns/" };
        if (rec->ns && rec->ns < 4) ns = sh_cstr(vm, spaces[rec->ns]);
    }
    if (rec->had) old = sh_span(vm, d, rec->old);
    so_def(vm, o, "attributeName", name);
    so_def(vm, o, "attributeNamespace", ns);
    so_def(vm, o, "oldValue", old);
    return jv_obj(o);
}
/* an observer's records taken from its queue, as an array (*n of them); the queue emptied */
static jv so_take(js_vm *vm, uint32_t mo, uint32_t *n) {
    struct js_host *h = vm->host;
    uint32_t a = js_array_new(vm, 0);
    *n = 0;
    for (size_t i = 0; i < len(h->mrecs); i++) {
        if (!h->mrecs[i].live || h->mrecs[i].mo != mo) continue;
        sh_mrec rec = h->mrecs[i];
        h->mrecs[i].live = 0;
        js_array_push(vm, a, so_record(vm, &rec));
        (*n)++;
    }
    /* none live: the queue's storage begun again */
    int any = 0;
    for (size_t i = 0; i < len(h->mrecs) && !any; i++) if (h->mrecs[i].live) any = 1;
    if (!any) { clear(h->mrecs); clear(h->mnodes); }
    return jv_obj(a);
}
/* the notification (DOM 4.3.3): each observer with records, in the order they were made,
   its transient registrations dropped, its callback called with the records and itself */
static void so_notify(js_vm *vm) {
    struct js_host *h = vm->host;
    h->mo_queued = 0;
    for (size_t m = 0; m < len(h->mos) && vm->throwing != 2; m++) {
        sh_mo o = h->mos[m];
        uint32_t nrec = 0;
        jv recs = so_take(vm, (uint32_t)m + 1, &nrec);
        for (size_t i = 0; i < len(h->mregs); i++)
            if (h->mregs[i].live && h->mregs[i].transient && h->mregs[i].mo == (uint32_t)m + 1) { h->mregs[i].live = 0; h->mreg_live--; }
        if (!nrec) continue;
        if (o.rec) {
            h->rt->observer[o.rec].records += nrec;
            h->rt->observer[o.rec].calls++;
        }
        jv args[2] = { recs, jv_obj(o.obj) };
        uint32_t saved = vm->cur_run;
        vm->cur_run = o.run;
        js_call(vm, jv_obj(o.fn), jv_obj(o.obj), args, 2);
        if (vm->throwing == 1) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            js_report_error(vm, e, JS_PHASE_PROMISE);
        }
        vm->cur_run = saved;
    }
    /* then the slots signalled, each its slotchange (DOM 4.3.3, notify mutation observers) */
    script_slots_fire(vm);
}

/* ---- observe() ---- */

/* an option of observe()'s dictionary: 1 true, 0 false, -1 not present */
static int so_opt(js_vm *vm, jv o, const char *name) {
    if (!jv_is_obj(o)) return -1;
    jv v = js_get(vm, o, so_atom(vm, name));
    if (v == JV_UNDEF) return -1;
    return js_truthy(vm, v) ? 1 : 0;
}
static jv so_observe(js_vm *vm, uint32_t mo, jv target, jv opts) {
    struct js_host *h = vm->host;
    dom_document *d = h->d;
    uint32_t x = sh_node(vm, target, 0);
    if (vm->throwing) return JV_HOLE;
    if (!x) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'observe' on 'MutationObserver': parameter 1 is not of type 'Node'.");
    int child = so_opt(vm, opts, "childList"), attrs = so_opt(vm, opts, "attributes"), cdata = so_opt(vm, opts, "characterData");
    int sub = so_opt(vm, opts, "subtree"), aold = so_opt(vm, opts, "attributeOldValue"), dold = so_opt(vm, opts, "characterDataOldValue");
    if (vm->throwing) return JV_HOLE;
    jv filter = jv_is_obj(opts) ? js_get(vm, opts, so_atom(vm, "attributeFilter")) : JV_UNDEF;
    if (vm->throwing) return JV_HOLE;
    /* the filter's names, each ended by a zero byte */
    vector<char> names;
    int has_filter = filter != JV_UNDEF;
    if (has_filter) {
        if (!jv_is_obj(filter))
            return js_throw_msg(vm, vm->type_error_proto,
                                "Failed to execute 'observe' on 'MutationObserver': Failed to read the 'attributeFilter' property from 'MutationObserverInit': The provided value cannot be converted to a sequence.");
        jv lv = js_get(vm, filter, so_atom(vm, "length"));
        uint32_t n = jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0;
        for (uint32_t i = 0; i < n; i++) {
            jv v = js_array_get(vm, jv_idx(filter), i);
            vector<char> one;
            if (!sh_utf8(vm, v, &one)) return JV_HOLE;
            append_range(names, data(one), len(one));
            push_back(names, 0);
        }
    }
    /* the standard's rules: an old value or a filter asks for attributes, an old value for
       character data; one of the three must be asked for (Chromium's refusals of an old
       value or a filter without what it is of come before that one's) */
    if ((aold == 1 || has_filter) && attrs < 0) attrs = 1;
    if (dold == 1 && cdata < 0) cdata = 1;
    static const char *const pre = "Failed to execute 'observe' on 'MutationObserver': ";
    char msg[300];
    if (aold == 1 && attrs != 1) {
        snprintf(msg, sizeof msg, "%sThe options object may only set 'attributeOldValue' to true when 'attributes' is true or not present.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    if (has_filter && attrs != 1) {
        snprintf(msg, sizeof msg, "%sThe options object may only set 'attributeFilter' when 'attributes' is true or not present.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    if (dold == 1 && cdata != 1) {
        snprintf(msg, sizeof msg, "%sThe options object may only set 'characterDataOldValue' to true when 'characterData' is true or not present.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    if (child != 1 && attrs != 1 && cdata != 1) {
        snprintf(msg, sizeof msg, "%sThe options object must set at least one of 'attributes', 'characterData', or 'childList' to true.", pre);
        return js_throw_msg(vm, vm->type_error_proto, msg);
    }
    uint32_t bits = (child == 1 ? SO_CHILD : 0) | (attrs == 1 ? SO_ATTR : 0) | (cdata == 1 ? SO_DATA : 0) | (sub == 1 ? SO_SUBTREE : 0) |
                    (aold == 1 ? SO_ATTR_OLD : 0) | (dold == 1 ? SO_DATA_OLD : 0) | (has_filter ? SO_FILTER : 0);
    uint32_t foff = (uint32_t)len(h->mfilter);
    append_range(h->mfilter, data(names), len(names));
    /* observing the node again: its options replaced, the transient registrations it gave dropped */
    int found = 0;
    for (size_t i = 0; i < len(h->mregs); i++) {
        sh_mreg r = h->mregs[i];
        if (!r.live || r.transient || r.mo != mo || r.node != x || r.gen != d->node[x].gen) continue;
        for (size_t k = 0; k < len(h->mregs); k++)
            if (h->mregs[k].live && h->mregs[k].transient && h->mregs[k].source == (uint32_t)i + 1) { h->mregs[k].live = 0; h->mreg_live--; }
        h->mregs[i].opts = bits;
        h->mregs[i].foff = foff;
        h->mregs[i].flen = (uint32_t)len(names);
        found = 1;
    }
    if (!found) {
        /* the first registration: the DOM's details kept from now, its log read from here */
        if (!h->mreg_live) {
            dom_keep_details(d);
            h->mo_pos = (uint32_t)d->mutation_len;
        }
        sh_mreg r;
        memset(&r, 0, sizeof r);
        r.mo = mo;
        r.node = x;
        r.gen = d->node[x].gen;
        r.opts = bits;
        r.foff = foff;
        r.flen = (uint32_t)len(names);
        r.live = 1;
        push_back(h->mregs, r);
        h->mreg_live++;
    }
    /* the reasoner's record of it */
    script_observation_h oh = observation_new(h->rt);
    script_observation *ob = &h->rt->observation[oh.index];
    ob->observer = h->mos[mo - 1].rec;
    ob->run = vm->cur_run;
    ob->target = node_handle(d, x);
    ob->child_list = (uint8_t)(child == 1);
    ob->attributes = (uint8_t)(attrs == 1);
    ob->character_data = (uint8_t)(cdata == 1);
    ob->subtree = (uint8_t)(sub == 1);
    ob->old_value = (uint8_t)(aold == 1 || dold == 1);
    uint32_t nf = 0;
    for (size_t i = 0; i < len(names); i++) if (!names[i]) nf++;
    ob->filter = nf;
    if (h->mos[mo - 1].rec) h->rt->observer[h->mos[mo - 1].rec].disconnected = 0;
    return JV_UNDEF;
}

jv script_observe_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)callee;
    struct js_host *h = vm->host;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    if (id == SO_ILLEGAL) return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    if (id == SO_NOTIFY) { so_notify(vm); return JV_UNDEF; }
    if (id == SO_CTOR) {
        if (!js_is_callable(vm, a0))
            return js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'MutationObserver': parameter 1 is not of type 'Function'.");
        uint32_t o = js_obj_new(vm, JK_HOST, h->protos[HP_MUTATIONOBSERVER]);
        vm->objs[o].c = HK_OBSERVER;
        sh_mo mo;
        memset(&mo, 0, sizeof mo);
        mo.obj = o;
        mo.fn = jv_idx(a0);
        mo.run = vm->cur_run;
        script_observer_h rh = observer_new(h->rt);
        h->rt->observer[rh.index].run = vm->cur_run;
        mo.rec = rh.index;
        push_back(h->mos, mo);
        vm->objs[o].a = (uint32_t)len(h->mos);
        /* the callback kept: the observer object holds it */
        js_define(vm, o, so_atom(vm, "\001callback"), a0, JS_SHAPE_HIDDEN);
        return jv_obj(o);
    }
    uint32_t mo = so_this(vm, thisv);
    if (!mo) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
    if (id == SO_OBSERVE) return so_observe(vm, mo, a0, a1);
    if (id == SO_TAKE) {
        uint32_t n = 0;
        return so_take(vm, mo, &n);
    }
    if (id == SO_DISCONNECT) {
        for (size_t i = 0; i < len(h->mregs); i++)
            if (h->mregs[i].live && h->mregs[i].mo == mo) { h->mregs[i].live = 0; h->mreg_live--; }
        for (size_t i = 0; i < len(h->mrecs); i++) if (h->mrecs[i].mo == mo) h->mrecs[i].live = 0;
        if (h->mos[mo - 1].rec) h->rt->observer[h->mos[mo - 1].rec].disconnected = 1;
        return JV_UNDEF;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* the collector's roots of this unit (script.h's sh_mo): each observer's object and
   callback, which a registration may call on as long as the page lives (registrations and
   records hold nodes and places, no value), and the notification's native */
void script_observe_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    for (size_t i = 0; i < len(h->mos); i++) {
        if (h->mos[i].obj) push_back(*out, jv_obj(h->mos[i].obj));
        if (h->mos[i].fn) push_back(*out, jv_obj(h->mos[i].fn));
    }
    if (h->mo_fn) push_back(*out, jv_obj(h->mo_fn));
}

void script_observe_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t p = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_MUTATIONOBSERVER] = p;
    sh_method(vm, p, "observe", SO_OBSERVE, 1);
    sh_method(vm, p, "disconnect", SO_DISCONNECT, 0);
    sh_method(vm, p, "takeRecords", SO_TAKE, 0);
    sh_interface(vm, "MutationObserver", SO_CTOR, p);
    uint32_t r = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_MUTATIONRECORD] = r;
    sh_interface(vm, "MutationRecord", SO_ILLEGAL, r);
    h->mo_fn = js_native_new(vm, SO_NOTIFY, so_atom(vm, "notify"), 0);
    h->mo_pos = h->d->mutation_len;
}
