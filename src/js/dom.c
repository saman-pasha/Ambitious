/* src/js/dom.c -- the engine's host: the console, the timers' loop, and (Phase 8b) the
   DOM's bindings. The host's hooks answer "not mine" for every object the DOM does
   not make. */
#include <stdlib.h>
#include "js.h"

void js_console(js_vm *vm, uint32_t level, uint32_t text) {
    js_msg m = { level, text };
    push_back(vm->msgs, m);
    if (vm->echo) {
        vector<char> t;
        js_str_to_utf8(vm, text, &t);
        push_back(t, '\n');
        fwrite(data(t), 1, len(t), level >= 2 ? stderr : stdout);
    }
}

jv js_host_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled) { (void)vm; (void)o; (void)atom; *handled = 0; return JV_UNDEF; }
int js_host_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled) { (void)vm; (void)o; (void)atom; (void)v; *handled = 0; return 1; }
jv js_host_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)vm; (void)id; (void)callee; (void)thisv; (void)args; (void)argc;
    *handled = 0;
    return JV_UNDEF;
}

/* the timers run in virtual time, in order of their due times (then of their making),
   until none is left or the clock passes until: how many ran. An uncaught exception
   in one is reported through vm->out and the next one runs. */
uint32_t js_run_timers(js_vm *vm, double until, uint32_t max) {
    uint32_t ran = 0;
    while (ran < max) {
        int64_t best = -1;
        for (size_t i = 0; i < len(vm->timers); i++) {
            js_timer t = vm->timers[i];
            if (!t.live) continue;
            if (best < 0 || t.due < vm->timers[(size_t)best].due || (t.due == vm->timers[(size_t)best].due && t.seq < vm->timers[(size_t)best].seq)) best = (int64_t)i;
        }
        if (best < 0) break;
        js_timer t = vm->timers[(size_t)best];
        if (t.due > until) break;
        if (t.due > vm->now) vm->now = t.due;
        if (t.interval >= 0) {
            vm->timers[(size_t)best].due = vm->now + t.interval;
            vm->timers[(size_t)best].seq = ++vm->timer_seq;
        } else vm->timers[(size_t)best].live = 0;
        vector<jv> av;
        for (uint32_t k = 0; t.args && k < t.nargs; k++) push_back(av, js_array_get(vm, t.args, k));
        js_call(vm, t.fn, JV_UNDEF, data(av), (uint32_t)len(av));
        ran++;
        if (vm->throwing == 2) break;
        if (vm->throwing) {
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            clear(vm->out);
            js_error_text(vm, e, &vm->out);
            push_back(vm->out, 0);
            js_console(vm, 3, js_str_utf8(vm, data(vm->out), len(vm->out) - 1));
        }
    }
    return ran;
}
