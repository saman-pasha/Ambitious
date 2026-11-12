/* src/script/script.h -- the script module's own API (contracts/script.pl has its
   records and facts): a page's scripts run, a click dispatched, the page come to rest.
   Include it after amb_interface(dom), amb_interface(net), amb_interface(script),
   <vector.pl> and src/js/js.h.

   The engine reaches the page through vm->host. A command declares the page's stores,
   then a js_host beside them, and points the host at each store: the safe part
   stores a borrow only where it can follow it, so the host is filled where the stores
   are declared (SCRIPT_HOST), never inside a function given them. The host also holds
   what the page's scripts keep beside the engine's heap (its live collections' caches,
   the versions they are read at): vectors, which SCRIPT_HOST_DROP frees.

   A header's struct with a vector<T> member is lowered only where the header includes
   <vector.pl> itself and declares T (toolchain finding): so both are here. */
#ifndef AMB_SCRIPT_H
#define AMB_SCRIPT_H
#include <vector.pl>

/* a live collection (DOM 4.2.10): what it holds under its root, and a cache of its
   length and of the last item read, valid at the versions of the DOM it was read at */
typedef struct sh_list {
    uint32_t root, gen, kind, obj;   /* its root node and that node's generation, its kind (src/script/script.c), its object */
    uint32_t koff, klen, lower, given;   /* a by-tag, by-class or by-name key: its text in the host's keys; by tag, the
                                            atoms of the name lower-cased (HTML elements') and as given (foreign ones') */
    uint64_t tree, attrs;            /* the versions of the DOM it was read at */
    uint32_t n, ci, cn, spare;       /* its length (~0: unknown), the last item read and its node */
} sh_list;

/* the view a script reads (getComputedStyle, getBoundingClientRect ...): what a cascade
   and a layout run when a script asked made, kept until the DOM changes
   (src/script/view.c). A box: its element, its content box in page px, its margins,
   borders and paddings (top, right, bottom, left) */
typedef struct sh_vbox { uint32_t node, kind; double x, y, w, h, m[4], b[4], p[4]; } sh_vbox;
/* a fragment of a line: its node (a text, an atomic or an inline element), its line */
typedef struct sh_vfrag { uint32_t node, line; double x, y, w, h; } sh_vfrag;
/* a computed value of a style: its property (the CSS module's), unit and number, how it
   came (declared, inherited, initial), its text in the view's own; a style's values are a
   run ended by one of prop ~0 */
typedef struct sh_vval { uint32_t prop, unit, how, spare; double num; uint32_t off, len; } sh_vval;

/* the network a script reaches (src/script/fetch.c). A request: fetch()'s, an
   XMLHttpRequest's, a Request object's (not sent); its object (fetch's promise, the
   XMLHttpRequest), the loader's record of it, XHR's readyState; the response's status,
   the request's mode and whether it waits (a synchronous XHR); its method, URL, header
   lines and body in the host's network text; XHR's responseType, its Response, its
   record in the runtime, how it ended */
typedef struct sh_req {
    uint32_t kind, obj, resource, state;
    uint32_t status, mode, sync, flags;
    uint32_t moff, mlen, uoff, ulen;
    uint32_t hoff, hlen, boff, blen;
    uint32_t rtype, resp, rec, outcome;
} sh_req;
/* a Response: its status, its type (basic, cors, opaque, default, error), whether it was
   redirected and its body read; its URL, body, content type and status text in the
   network text; its Headers object, made when asked for */
typedef struct sh_resp {
    uint32_t status, type, redirected, used;
    uint32_t uoff, ulen, boff, blen;
    uint32_t toff, tlen, soff, slen;
    uint32_t headers, spare;
} sh_resp;
/* a header of a Headers object (its object's index): its name lower-cased and value */
typedef struct sh_hdr { uint32_t owner, noff, nlen, voff, vlen, live; } sh_hdr;
/* a script waiting to run (HTML 4.12.1.1): an external one a script inserted, or one the
   parse found async; its element and that node's generation, the loader's record of its
   src, how it came (script_insertion), whether it has run */
typedef struct sh_pend { uint32_t node, gen, resource, how, done, spare; } sh_pend;
/* a FormData's entry (src/script/input.c): its object's index, its name and value in the
   network text, its control (0 for one a script appended), whether it is live */
typedef struct sh_fd { uint32_t owner, noff, nlen, voff, vlen, control, live, spare; } sh_fd;
/* MutationObserver (src/script/observe.c). An observer: its object, its callback, the run
   that made it, its record in the runtime */
typedef struct sh_mo { uint32_t obj, fn, run, rec; } sh_mo;
/* an observer's registration at a node (its generation): its options' bits, its
   attributeFilter's names in the host's filter text; transient: one a removal gave the
   node, from the registration source (its place plus one) */
typedef struct sh_mreg { uint32_t mo, node, gen, opts, foff, flen, transient, source, live, spare; } sh_mreg;
/* a record waiting in its observer's queue: its type, target, siblings (each with its
   generation), attribute name's atom and namespace, old value (had: one is given), its
   nodes in the host's list (node, generation pairs; a removed one's bit 31 set) */
typedef struct sh_mrec {
    uint32_t mo, type, target, tgen, prev, pgen, next, ngen;
    uint32_t name, ns, had, noff, nlen, live;
    amb_span old;
} sh_mrec;
/* a custom element definition (src/script/custom.c): its constructor and prototype, the
   run that defined it, its record in the runtime, its lifecycle callbacks (0: none), its
   name and its observed attributes (each ended by a zero byte) in the host's custom text */
typedef struct sh_cdef {
    uint32_t ctor, proto, run, rec, connected, disconnected, adopted, attr_changed;
    uint32_t noff, nlen, ooff, olen;
} sh_cdef;
/* an element's custom element state: the node and its generation, its definition, its state */
typedef struct sh_cel { uint32_t node, gen, def, state, conn, spare; } sh_cel;
/* a session history entry (src/script/window.c): its URL in the host's history text, whether
   it is the page's document's (the blank entry is not), its state */
typedef struct sh_hent { uint32_t uoff, ulen, doc, spare; jv state; } sh_hent;
/* a stored item: its area (1 local, 2 session), origin, key and value in the storage text */
typedef struct sh_item { uint32_t area, ooff, olen, koff, klen, voff, vlen, live; } sh_item;
/* an import map's entry (HTML 8.1.5.3): its scope's URL (none for the top level), a
   specifier key (a URL or a bare name) and the address it maps to (has 0: null), each a
   span of the host's imtext */
typedef struct sh_imap { uint32_t soff, slen, koff, klen, voff, vlen, has, spare; } sh_imap;
/* the changes of one call grouped as a record is, while the log is read */
typedef struct sh_mgroup {
    uint32_t type, target, prev, pgen, next, ngen, name, ns;
    uint32_t had, order, nadd, nrem;
    amb_span old;
} sh_mgroup;

/* the page's scripts, each <script> in document order (its text, or its src as the
   loader fetched it), then DOMContentLoaded and load, then its timers as far as horizon
   ms of virtual time: the number of scripts run. A page without scripts makes no VM. */
uint32_t script_run_page(js_vm *vm, double horizon);
/* a click at the element of index x, as a user's: the listeners on its path, the
   checkbox's or radio's change, a label's control; then the timers it set, as far as
   horizon ms past now. 0 when x is no element */
int script_click(js_vm *vm, uint32_t x, double horizon);
/* the first element, in document order, that matches the selector list: its index, or 0 */
uint32_t script_select(const dom_document *d, const char *sel, size_t n);
/* the page come to rest, before its snapshot: each timer's state recorded, and the
   nodes no tree holds any more destroyed (a wrapper of one is stale from then on) */
void script_settle(js_vm *vm);
/* a user's action at the page after its scripts (src/script/input.c): a click at the
   element x, a double click, focus given it, text typed into it (clicked first), a key
   pressed (text: its name, Enter, Tab, Shift+A ...); then the timers they set, as far as
   horizon ms past now. 0 when there is no such element, or the page navigated away */
enum { SCRIPT_ACT_CLICK = 1, SCRIPT_ACT_DBLCLICK, SCRIPT_ACT_FOCUS, SCRIPT_ACT_TYPE, SCRIPT_ACT_KEY };
int script_action(js_vm *vm, uint32_t kind, uint32_t x, const char *text, size_t n, double horizon);
/* the encoding the document was decoded with, by its name (the parser's record): its
   forms submit in it, document.characterSet names it. Unset, UTF-8 */
void script_charset(js_vm *vm, const char *name, size_t n);
/* where localStorage is kept: the profile's storage file (unset: for the run only) */
void script_storage_file(js_vm *vm, const char *path);

/* the host: a command's, beside its stores. Declared after the prototypes above: a
   prototype after a vector<T> in a header is not registered (PLAN.md's toolchain
   findings), so the struct with its vectors comes last */
/* the host's prototypes are numbered by the script module (src/script/script.c). An
   enum, not a #define: an included header's array member sized by an object-like
   macro is laid out with no size (toolchain finding) */
enum { SCRIPT_PROTOS = 64 };

struct js_host {
    dom_document *d;
    net_loader *nt;
    script_runtime *rt;
    uint32_t page;            /* the page's resource in the loader (its URL resolves a script's src) */
    uint32_t ready;           /* the VM made and the page's globals defined */
    uint32_t phase;           /* what runs now: a script, a listener, a timer (script_phase) */
    uint32_t nscripts;        /* the scripts run so far: each one's number */
    uint32_t protos[SCRIPT_PROTOS];   /* the host objects' prototypes, by kind */
    /* the DOM's versions, as its mutation log says them: the tree's moves with each node
       inserted or removed, the attributes' with each attribute set or removed; the log
       is read from log_pos on */
    uint64_t tree_ver, attr_ver;
    size_t log_pos;
    vector<sh_list> lists;            /* the live collections */
    vector<char> keys;                /* their keys' text */
    /* the view (src/script/view.c): the viewport, the DOM's revision it was made at (0:
       never), how many times a script's read made it anew */
    double view_w, view_h;
    uint64_t view_rev;
    uint32_t view_runs, view_ok;
    vector<uint32_t> vstyle;          /* a node's style: its first value in vvals, plus 1 (0: none) */
    vector<sh_vval> vvals;
    vector<char> vtext;
    vector<uint32_t> vbox;            /* a node's box in vboxes, plus 1 (0: none) */
    vector<sh_vbox> vboxes;
    vector<sh_vfrag> vfrags;
    /* the network (src/script/fetch.c): requests, responses, headers, their text; the
       native function that runs the host's tasks from the timers' queue */
    vector<sh_req> reqs;
    vector<sh_resp> resps;
    vector<sh_hdr> hdrs;
    vector<char> ntext;
    uint32_t task_fn;
    /* the scripts (src/script/script.c): each script element's "already started" (its
       generation plus one, by index); the scripts waiting to run, and those written to run
       after their writer; document.write's text so far, its script and the nodes it made
       there; the script running (document.currentScript); whether the parse's scripts run
       (an insertion point), how deep external scripts run, whether the document is open */
    vector<uint32_t> started;
    vector<sh_pend> pending;
    vector<uint32_t> wpend;
    vector<char> wtext;
    vector<uint32_t> wnodes;
    uint32_t wscript, cur_script, parsing, ext_run, opened;
    /* the user (src/script/input.c): the focused element and its generation, and its
       value when it took focus (for its change event); the pointer: whether it is on the
       page, where, the element under it; the click event a user's click dispatches;
       FormData's entries; whether the page navigated away (its event loop stopped); the
       form whose entry list is being constructed; the navigation planned (a form's, its
       task to come) and its request's URL, body and type; whether beforeunload is being
       fired; the document's encoding (its number plus one: 0 is UTF-8); the element a
       click is at while its events' point is left to be found when read (src/script/input.c);
       the name form's atom in the document, once one has had it, and how many names the
       document had when it was last looked for */
    uint32_t focus, focus_gen, mouse_in, hover, navigated, building, nav_planned, unloading, pt_lazy, form_nm, names_seen, spare4;
    int charset, spare2;
    vector<char> nav_url, nav_body, nav_ctype;
    /* the MutationObservers, their registrations (how many live), their records' queues
       and nodes, the filters' names; how far the DOM's log was read, whether the
       observers' microtask is queued, its function */
    vector<sh_mo> mos;
    vector<sh_mreg> mregs;
    vector<sh_mrec> mrecs;
    vector<uint32_t> mnodes;
    vector<char> mfilter;
    uint32_t mreg_live, mo_pos, mo_queued, mo_fn;
    /* custom elements: the definitions, the elements' states and each node's place among
       them, the construction stack, whenDefined's waiting promises (promise, name's atom,
       settled), the names and observed attributes; how far the DOM's log was read for
       the reactions, whether reactions are running, the registry object */
    vector<sh_cdef> cdefs;
    vector<sh_cel> cels;
    vector<uint32_t> cmap, cstack, cwait;
    vector<char> ctext;
    uint32_t ce_pos, in_reactions, registry, spare5;
    /* the window (src/script/window.c): the session history, its URLs, the current entry
       and the array that keeps the states; the stored items, their text, the profile's
       storage file, whether it was read and whether localStorage changed since */
    vector<sh_hent> hents;
    vector<char> htext;
    uint32_t hcur, hstates, sloaded, sdirty;
    vector<sh_item> items;
    vector<char> stext, spath;
    double mouse_x, mouse_y;
    jv click_ev;
    vector<char> fvalue;
    vector<sh_fd> fds;
    /* the document's URL (its loader's, then as its fragment and history change it), and
       the location object */
    vector<char> docurl;
    uint32_t location;
    /* module scripts (src/script/script.c): the import maps' entries and their text; the
       URLs whose fetch for a module failed, each reported once ('\n' after each) */
    vector<sh_imap> imap;
    vector<char> imtext;
    vector<char> modfail;
};

/* the host beside the stores d, nt and sr, and the VM vm pointed at it. The host is
   zeroed first: its vectors are empty */
#define SCRIPT_HOST(h, vm, d, nt, sr) do { \
    memset(&(vm), 0, sizeof(vm)); \
    memset(&(h), 0, sizeof(h)); \
    (h).d = &(d); (h).nt = &(nt); (h).rt = &(sr); (h).tree_ver = 1; (h).attr_ver = 1; \
    (h).view_w = 800; (h).view_h = 600; \
    (vm).host = &(h); \
} while (0)
/* the host's own vectors freed: a struct's are not autodropped */
#define SCRIPT_HOST_DROP(h) vector_sh_list_drop(&(h).lists); vector_char_drop(&(h).keys); \
    vector_uint32_t_drop(&(h).vstyle); vector_sh_vval_drop(&(h).vvals); vector_char_drop(&(h).vtext); \
    vector_uint32_t_drop(&(h).vbox); vector_sh_vbox_drop(&(h).vboxes); vector_sh_vfrag_drop(&(h).vfrags); \
    vector_sh_req_drop(&(h).reqs); vector_sh_resp_drop(&(h).resps); vector_sh_hdr_drop(&(h).hdrs); vector_char_drop(&(h).ntext); \
    vector_uint32_t_drop(&(h).started); vector_sh_pend_drop(&(h).pending); vector_uint32_t_drop(&(h).wpend); \
    vector_char_drop(&(h).wtext); vector_uint32_t_drop(&(h).wnodes); vector_char_drop(&(h).fvalue); vector_sh_fd_drop(&(h).fds); \
    vector_char_drop(&(h).docurl); vector_char_drop(&(h).nav_url); vector_char_drop(&(h).nav_body); \
    vector_char_drop(&(h).nav_ctype); vector_sh_mo_drop(&(h).mos); vector_sh_mreg_drop(&(h).mregs); vector_sh_mrec_drop(&(h).mrecs); \
    vector_uint32_t_drop(&(h).mnodes); vector_char_drop(&(h).mfilter); vector_sh_cdef_drop(&(h).cdefs); vector_sh_cel_drop(&(h).cels); \
    vector_uint32_t_drop(&(h).cmap); vector_uint32_t_drop(&(h).cstack); vector_uint32_t_drop(&(h).cwait); vector_char_drop(&(h).ctext); \
    vector_sh_hent_drop(&(h).hents); vector_char_drop(&(h).htext); vector_sh_item_drop(&(h).items); vector_char_drop(&(h).stext); \
    vector_char_drop(&(h).spath); vector_sh_imap_drop(&(h).imap); vector_char_drop(&(h).imtext); vector_char_drop(&(h).modfail)

#endif
