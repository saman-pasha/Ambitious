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
    vector_uint32_t_drop(&(h).vbox); vector_sh_vbox_drop(&(h).vboxes); vector_sh_vfrag_drop(&(h).vfrags)

#endif
