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
       HK_XHR, HK_FORMDATA, HK_OBSERVER };
/* its prototypes, in the host's protos */
enum { HP_NODE, HP_ELEMENT, HP_CHARDATA, HP_DOCUMENT, HP_CLASSLIST, HP_STYLE, HP_DATASET, HP_EVENT, HP_NODELIST,
       HP_HTMLCOLLECTION, HP_DOMRECT, HP_RECTLIST, HP_RESPONSE, HP_REQUEST, HP_HEADERS, HP_XHR, HP_PROGRESS, HP_UIEVENT,
       HP_MOUSEEVENT, HP_POINTEREVENT, HP_KEYBOARDEVENT, HP_FOCUSEVENT, HP_INPUTEVENT, HP_SUBMITEVENT, HP_FORMDATAEVENT,
       HP_POPSTATEEVENT, HP_HASHCHANGEEVENT, HP_BEFOREUNLOADEVENT, HP_FORMDATA, HP_MUTATIONOBSERVER, HP_MUTATIONRECORD, HP_SHADOWROOT,
       HP_N };

/* the natives' numbers: script.c's from JS_HOST_NATIVES (1000), fetch.c's from SH_FETCH_FIRST,
   input.c's from SH_INPUT_FIRST, observe.c's from SH_OBSERVE_FIRST, custom.c's from
   SH_CUSTOM_FIRST. A
   literal: in an included header an object-like macro in a declaration is not expanded,
   and the declarations after an enum that used one are not registered (a toolchain
   finding) */
enum { SH_FETCH_FIRST = 1400, SH_FETCH_END = 1500, SH_INPUT_FIRST = 1500, SH_INPUT_END = 1600, SH_OBSERVE_FIRST = 1600, SH_OBSERVE_END = 1700,
       SH_CUSTOM_FIRST = 1700, SH_CUSTOM_END = 1800 };

/* an event's flags, in its a slot */
#define EV_STOP 1u
#define EV_STOP_NOW 2u
#define EV_PREVENTED 4u
#define EV_CANCELABLE 8u
#define EV_BUBBLES 16u
#define EV_TRUSTED 32u
#define EV_PASSIVE 64u
#define EV_NOPATH 128u    /* reaches its target alone: mouseenter, pointerleave ... */
#define EV_COMPOSED 256u  /* passes out of a shadow tree: a user's pointer, keys, focus, input */
#define EV_LAZYPOINT 512u /* a user's mouse event whose coordinates are found when first read */

/* an event's target that is no node and not the window: an object of the engine's (an
   XMLHttpRequest), its index with this bit */
#define SH_OBJECT_TARGET 0x80000000u
/* a timer's record mark for a host task (a network response, an inserted script run),
   which is no page's timer */
#define SH_TASK_REC 0xFFFFFFFDu
/* the host's tasks */
enum { SH_TASK_FETCH = 1, SH_TASK_XHR, SH_TASK_BODY, SH_TASK_NOTE, SH_TASK_SCRIPT, SH_TASK_HASH, SH_TASK_NAV };

/* ---- script.c's, for the other units ---- */
/* the DOM read: an element; an HTML element of a name; a script element; an attribute of
   x by name (0: none); the next and previous nodes in tree order within a scope; an
   element child, the first or the last; whether x is in the document; an ASCII
   case-insensitive comparison; HTML's white space */
int sh_is_elem(const dom_document *d, uint32_t x);
int sh_named(const dom_document *d, uint32_t x, const char *name);
int sh_script_elem(const dom_document *d, uint32_t x);
uint32_t sh_attr(const dom_document *d, uint32_t x, const char *name, size_t n);
uint32_t sh_attr_z(const dom_document *d, uint32_t x, const char *name);
uint32_t sh_next_in(const dom_document *d, uint32_t c, uint32_t scope);
uint32_t sh_prev_in(const dom_document *d, uint32_t c, uint32_t scope);
uint32_t sh_elem_child(const dom_document *d, uint32_t x, int last);
int sh_connected(const dom_document *d, uint32_t x);
int sh_ci_eq(const char *a, const char *b, size_t n);
int sh_ws(char c);
/* the DOM written: a node out of its parent; a refusal as the DOM standard's exception */
dom_status sh_detach(dom_document *d, uint32_t child);
jv sh_status(js_vm *vm, dom_status st);
/* an event of a type fired at a target (trusted); a click at an element, a user's
   (trusted) or a script's, its checkbox and label behaviour and its default action: 1
   unless cancelled; the view made up to date for a script's read */
int sh_fire(js_vm *vm, uint32_t target, const char *type, uint32_t flags);
int sh_click_at(js_vm *vm, uint32_t x, uint32_t trusted);
void sh_view(js_vm *vm);
/* the VM made for a page that made none (a user's action needs it); an action's timers run */
void script_ready(js_vm *vm);
void script_timers_after(js_vm *vm, double horizon);
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
/* the document's URL changed (a fragment's navigation, history's): location's parts again */
void sh_location_set(js_vm *vm, const char *url);

/* ---- fetch.c's ---- */
void script_fetch_setup(js_vm *vm);
jv script_fetch_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt, int *handled);
jv script_fetch_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled);
int script_fetch_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled);
void script_fetch_task(js_vm *vm, uint32_t kind, uint32_t arg);
/* HTTP's reason phrase for a status, into out */
void script_reason(uint32_t status, char *out, size_t cap);

/* ---- input.c's: events of the user's, focus, forms, navigation ---- */
void script_input_setup(js_vm *vm);
jv script_input_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled);
jv script_input_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled);
/* focus moved to x (focus(): 1 when it moved), x blurred, the focused element (or body) */
int script_focus(js_vm *vm, uint32_t x);
void script_blur(js_vm *vm, uint32_t x);
uint32_t script_active(js_vm *vm);
/* the default action of a click at x that was not cancelled: a submit or reset button's
   form, a link's navigation */
void script_click_default(js_vm *vm, uint32_t x);
/* a navigation of the page (location's, a link's): the loader's GET of the URL, its record */
void script_navigate(js_vm *vm, uint32_t cause, const char *url, size_t n);
/* a click event a script's click() dispatches (trusted: a label's for a user's click); a
   copy of a click, its coordinates, detail and pointer, for a label's control */
jv script_click_event(js_vm *vm, uint32_t trusted);
jv script_click_copy(js_vm *vm, jv ev);
/* an element's form owner (its form attribute's form, or its nearest form), 0 when none */
uint32_t script_form_owner(const dom_document *d, uint32_t x);
/* a user's mouse event's coordinate (EV_LAZYPOINT), its point found now: handled when the
   name is one of them */
jv script_event_point(js_vm *vm, uint32_t ev, uint32_t atom, int *handled);
/* ---- observe.c's: MutationObserver ---- */
void script_observe_setup(js_vm *vm);
jv script_observe_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled);
/* the DOM's log since it was last read made records for the observers it concerns */
void script_observe_flush(js_vm *vm);
/* ---- custom.c's: custom elements and shadow trees ---- */
void script_custom_setup(js_vm *vm);
jv script_custom_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled);
/* the reactions (lifecycle callbacks) to the DOM's changes since they were last read */
void script_custom_flush(js_vm *vm);
/* the definition of a name (its place plus one, 0 for none); the element its constructor makes */
uint32_t script_custom_def(js_vm *vm, const char *s, size_t n);
jv script_custom_create(js_vm *vm, uint32_t def);
/* a shadow root's host (0: x is none); an event's next node on its path (a slotted node's
   slot, a shadow root's host when composed); a node retargeted against another */
uint32_t script_shadow_host(const dom_document *d, uint32_t x);
uint32_t script_event_parent(const dom_document *d, uint32_t x, uint32_t target, int composed);
uint32_t script_retarget(const dom_document *d, uint32_t a, uint32_t b);
/* a form's named property: the element it owns of that id or name (handled), else none */
jv script_form_named(js_vm *vm, uint32_t form, uint32_t atom, int *handled);
/* the input unit's host tasks: hashchange, a form's planned navigation */
void script_input_task(js_vm *vm, uint32_t kind, uint32_t arg);

#endif
