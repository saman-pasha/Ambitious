/* src/script/host.h -- what the script module's units share (src/script/script.c, the
   bindings and the event loop; view.c, what scripts read of the rendering; fetch.c, the
   network): the host objects' kinds and their prototypes' numbers, each unit's range of
   native numbers, the host's tasks, and the helpers script.c defines for the others.
   Include it after src/js/js.h.

   A helper that takes a vector is declared in each unit that calls it, not here: a
   prototype after a vector<T> in a header is not registered (a toolchain finding). */
#ifndef AMB_SCRIPT_HOST_H
#define AMB_SCRIPT_HOST_H

/* a host object's kind, in its c slot */
enum { HK_NODE = 1, HK_CLASSLIST, HK_STYLE, HK_DATASET, HK_EVENT, HK_LIST, HK_COMPUTED, HK_RESPONSE, HK_REQUEST, HK_HEADERS,
       HK_XHR };
/* its prototypes, in the host's protos */
enum { HP_NODE, HP_ELEMENT, HP_CHARDATA, HP_DOCUMENT, HP_CLASSLIST, HP_STYLE, HP_DATASET, HP_EVENT, HP_NODELIST,
       HP_HTMLCOLLECTION, HP_DOMRECT, HP_RECTLIST, HP_RESPONSE, HP_REQUEST, HP_HEADERS, HP_XHR, HP_PROGRESS, HP_N };

/* the natives' numbers: script.c's from JS_HOST_NATIVES (1000), fetch.c's from SH_FETCH_FIRST. A
   literal: in an included header an object-like macro in a declaration is not expanded,
   and the declarations after an enum that used one are not registered (a toolchain
   finding) */
enum { SH_FETCH_FIRST = 1400, SH_FETCH_END = 1500 };

/* an event's flags, in its a slot */
#define EV_STOP 1u
#define EV_STOP_NOW 2u
#define EV_PREVENTED 4u
#define EV_CANCELABLE 8u
#define EV_BUBBLES 16u
#define EV_TRUSTED 32u
#define EV_PASSIVE 64u

/* an event's target that is no node and not the window: an object of the engine's (an
   XMLHttpRequest), its index with this bit */
#define SH_OBJECT_TARGET 0x80000000u
/* a timer's record mark for a host task (a network response, an inserted script run),
   which is no page's timer */
#define SH_TASK_REC 0xFFFFFFFDu
/* the host's tasks */
enum { SH_TASK_FETCH = 1, SH_TASK_XHR, SH_TASK_BODY, SH_TASK_NOTE, SH_TASK_SCRIPT };

/* ---- script.c's, for the other units ---- */
jv sh_str(js_vm *vm, const char *p, size_t n);
jv sh_cstr(js_vm *vm, const char *p);
jv sh_wrap(js_vm *vm, uint32_t x);
uint32_t sh_node(js_vm *vm, jv v, int need);
jv sh_dom_error(js_vm *vm, const char *name, const char *msg);
void sh_method(js_vm *vm, uint32_t o, const char *name, uint32_t id, uint32_t nargs);
uint32_t sh_interface(js_vm *vm, const char *name, uint32_t id, uint32_t p);
/* an event made, dispatched (target a node, JS_WINDOW, or SH_OBJECT_TARGET | an object):
   1 unless cancelled; a listener added or removed on a target, from a native's arguments */
jv sh_event_new(js_vm *vm, uint32_t type, uint32_t flags);
int sh_dispatch(js_vm *vm, uint32_t target, jv ev);
jv sh_add_listener(js_vm *vm, uint32_t target, uint32_t args, uint32_t argc);
jv sh_remove_listener(js_vm *vm, uint32_t target, uint32_t args, uint32_t argc);
/* a host task after delay ms of virtual time; the microtasks at a checkpoint; the page's URL */
void sh_task(js_vm *vm, double delay, uint32_t kind, uint32_t arg);
void sh_checkpoint(js_vm *vm);
void sh_page_url(js_vm *vm, char *out, size_t cap);

/* ---- fetch.c's ---- */
void script_fetch_setup(js_vm *vm);
jv script_fetch_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt, int *handled);
jv script_fetch_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled);
int script_fetch_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled);
void script_fetch_task(js_vm *vm, uint32_t kind, uint32_t arg);
/* HTTP's reason phrase for a status, into out */
void script_reason(uint32_t status, char *out, size_t cap);

#endif
