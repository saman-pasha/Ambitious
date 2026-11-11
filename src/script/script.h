/* src/script/script.h -- the script module's own API (contracts/script.pl has its
   records and facts): a page's scripts run, a click dispatched, the page come to rest.
   Include it after amb_interface(dom), amb_interface(net), amb_interface(script) and
   src/js/js.h.

   The engine reaches the page through vm->host. A command declares the page's stores,
   then a js_host beside them, and points the host at each store: the safe part
   stores a borrow only where it can follow it, so the host is filled where the stores
   are declared (SCRIPT_HOST), never inside a function given them. */
#ifndef AMB_SCRIPT_H
#define AMB_SCRIPT_H

struct js_host {
    dom_document *d;
    net_loader *nt;
    script_runtime *rt;
    uint32_t page;            /* the page's resource in the loader (its URL resolves a script's src) */
    uint32_t ready;           /* the VM made and the page's globals defined */
    uint32_t phase;           /* what runs now: a script, a listener, a timer (script_phase) */
    uint32_t nscripts;        /* the scripts run so far: each one's number */
};

/* the host beside the stores d, nt and sr, and the VM vm pointed at it */
#define SCRIPT_HOST(h, vm, d, nt, sr) do { \
    memset(&(vm), 0, sizeof(vm)); \
    (h).d = &(d); (h).nt = &(nt); (h).rt = &(sr); (h).page = 0; (h).ready = 0; (h).phase = 0; (h).nscripts = 0; \
    (vm).host = &(h); \
} while (0)

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

#endif
