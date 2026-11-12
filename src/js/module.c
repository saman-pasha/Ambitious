/* src/js/module.c -- ECMAScript modules (ECMA-262 16.2): a module's graph loaded through
   the host's loader (with none, from files), linked (each import bound to the export it
   names, through re-exports and star exports, a cycle's modules together) and evaluated
   in order (each module once, a cycle's together; a module that awaits at its top level,
   and those that import it, asynchronously); import(), import.meta and the namespace
   objects.

   A module's top-level bindings are the cells of its environment, which its linking makes
   (vars undefined, functions their closures, the rest not yet initialized) before any
   module's code runs; an import reads its exporter's cell through a binding entry
   (vm->mod_binds) the linking resolves, so it sees the exporter's assignments as they
   happen. A namespace object is an accessor a name, each reading its binding the same
   way. */
#include "js.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint32_t mod_new(js_vm *vm, uint32_t url) {
    js_module m;
    memset(&m, 0, sizeof m);
    m.url = url;
    m.error = JV_UNDEF;
    push_back(vm->modules, m);
    return (uint32_t)(len(vm->modules) - 1);
}

static uint32_t mod_find(js_vm *vm, uint32_t url) {
    for (size_t i = 1; i < len(vm->modules); i++) if (js_str_eq(vm, vm->modules[i].url, url)) return (uint32_t)i;
    return 0;
}

static jv mod_throw(js_vm *vm, uint32_t proto, uint32_t msg) {
    return js_throw(vm, jv_obj(js_error_new(vm, proto, msg)));
}
static uint32_t cat(js_vm *vm, uint32_t a, const char *b) { return js_str_concat(vm, a, js_str_ascii(vm, b)); }

/* ---- the files' loader: a specifier resolved against its referrer's directory ---- */

/* a path's "." and ".." taken out, its slashes single */
static void path_normalize(const char *p, size_t n, vector<char> *out) {
    vector<size_t> starts;
    clear(out);
    size_t i = 0;
    while (i < n) {
        while (i < n && p[i] == '/') i++;
        size_t j = i;
        while (j < n && p[j] != '/') j++;
        size_t k = j - i;
        if (k == 1 && p[i] == '.') { i = j; continue; }
        if (k == 2 && p[i] == '.' && p[i + 1] == '.') {
            if (len(starts)) { resize(out, at(starts, len(starts) - 1)); pop_back(starts); }
            i = j;
            continue;
        }
        if (k) {
            push_back(starts, len(*out));
            push_back(out, '/');
            append_range(out, p + i, k);
        }
        i = j;
    }
    if (!len(*out)) push_back(out, '/');
}

static int file_loader(js_vm *vm, uint32_t spec, uint32_t referrer, uint32_t *url, uint32_t *source) {
    vector<char> s, base, path, norm;
    js_str_to_utf8(vm, spec, &s);
    size_t sn = len(s);
    const char *sp = data(s);
    if (sn >= 7 && memcmp(sp, "file://", 7) == 0) append_range(path, sp + 7, sn - 7);
    else if (sn >= 1 && sp[0] == '/') append_range(path, sp, sn);
    else if ((sn >= 2 && sp[0] == '.' && sp[1] == '/') || (sn >= 3 && sp[0] == '.' && sp[1] == '.' && sp[2] == '/')) {
        /* the referrer's directory: a module's URL, the classic scripts' base, else the current directory */
        uint32_t r = referrer ? referrer : vm->base_url;
        if (r) {
            js_str_to_utf8(vm, r, &base);
            size_t bn = len(base), from = bn >= 7 && memcmp(data(base), "file://", 7) == 0 ? 7 : 0;
            size_t cut = bn;
            while (cut > from && data(base)[cut - 1] != '/') cut--;
            append_range(path, data(base) + from, cut - from);
        } else {
            char cwd[4096];
            if (getcwd(cwd, sizeof cwd)) { append_range(path, cwd, strlen(cwd)); push_back(path, '/'); }
        }
        append_range(path, sp, sn);
    } else {
        uint32_t m = cat(vm, js_str_concat(vm, js_str_ascii(vm, "Failed to resolve module specifier \""), spec),
                         "\". Relative references must start with either \"/\", \"./\", or \"../\".");
        *source = m;
        return 0;
    }
    path_normalize(data(path), len(path), &norm);
    push_back(norm, 0);
    vector<char> text;
    {
        own FILE *f = fopen(data(norm), "rb");
        if (!f) {
            *source = cat(vm, js_str_concat(vm, js_str_ascii(vm, "Cannot find module '"), js_str_utf8(vm, data(norm), len(norm) - 1)), "'");
            return 0;
        }
        defer(f) { fclose(f); }
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) append_range(text, buf, got);
    }
    *url = js_str_concat(vm, js_str_ascii(vm, "file://"), js_str_utf8(vm, data(norm), len(norm) - 1));
    *source = js_str_utf8(vm, data(text), len(text));
    return 1;
}

/* ---- loading: a module for each URL, its code compiled once ---- */

/* the module a source makes (its URL given): compiled, its record filled; 0 thrown */
static uint32_t mod_compile(js_vm *vm, uint32_t url, const char *src, size_t n) {
    uint32_t m = mod_new(vm, url);
    vm->mod_compiling = m;
    uint32_t func = 0;
    vector<char> err;
    if (!js_compile(vm, src, n, vm->script, JS_CC_MODULE, &func, &err)) {
        size_t skip = len(err) > 13 && memcmp(data(err), "SyntaxError: ", 13) == 0 ? 13 : 0;
        mod_throw(vm, vm->syntax_error_proto, js_str_utf8(vm, data(err) + skip, len(err) - skip));
        /* a module that does not compile is no module: its URL may be tried again */
        vm->modules[m].url = js_str_ascii(vm, "");
        return 0;
    }
    return m;
}

/* the module a specifier names from a referrer (a module's URL, 0 for the classic
   scripts'): the one its URL already has, else loaded and compiled; 0 thrown */
static uint32_t mod_load(js_vm *vm, uint32_t spec, uint32_t referrer) {
    uint32_t url = 0, source = 0;
    int ok = vm->load_module ? vm->load_module(vm, spec, referrer, &url, &source) : file_loader(vm, spec, referrer, &url, &source);
    if (vm->throwing) return 0;
    if (!ok) { mod_throw(vm, vm->type_error_proto, source ? source : js_str_ascii(vm, "Failed to load the module")); return 0; }
    uint32_t m = mod_find(vm, url);
    if (m) return m;
    vector<char> text;
    js_str_to_utf8(vm, source, &text);
    return mod_compile(vm, url, data(text), len(text));
}

/* a module's requests loaded, and theirs: the graph its linking needs */
static int mod_load_graph(js_vm *vm, uint32_t m) {
    if (vm->modules[m].flags & JMF_REQUESTED) return 1;
    vm->modules[m].flags |= JMF_REQUESTED;
    uint32_t req = vm->modules[m].req, n = vm->modules[m].nreq;
    for (uint32_t i = 0; i < n; i++) {
        if (!vm->mod_reqs[req + i].module) {
            uint32_t c = mod_load(vm, vm->mod_reqs[req + i].spec, vm->modules[m].url);
            if (!c) return 0;
            vm->mod_reqs[req + i].module = c;
        }
        if (!mod_load_graph(vm, vm->mod_reqs[req + i].module)) return 0;
    }
    return 1;
}

static uint32_t req_module(js_vm *vm, uint32_t m, uint32_t r) { return vm->mod_reqs[vm->modules[m].req + r].module; }
static uint32_t req_spec(js_vm *vm, uint32_t m, uint32_t r) { return vm->mod_reqs[vm->modules[m].req + r].spec; }

/* ---- exports ---- */

/* ResolveExport (16.2.1.6.3): the module and the cell an export's name comes to (module 0:
   the cell a namespace object), 1; 0 none; -1 ambiguous (two star exports that differ).
   seen: the (module, name) pairs met, against the cycles of re-exports */
static int mod_resolve(js_vm *vm, uint32_t m, uint32_t name, vector<uint64_t> *seen, uint32_t *rm, uint32_t *rc) {
    uint64_t key = (uint64_t)m << 32 | name;
    for (size_t i = 0; i < len(*seen); i++) if ((*seen)[i] == key) return 0;
    push_back(seen, key);
    uint32_t exp = vm->modules[m].exp, n = vm->modules[m].nexp;
    for (uint32_t i = 0; i < n; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind == JX_LOCAL && e.name == name) { *rm = m; *rc = e.cell; return 1; }
    }
    for (uint32_t i = 0; i < n; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind != JX_INDIRECT || e.name != name) continue;
        uint32_t im = req_module(vm, m, e.req);
        if (!im) return 0;
        if (!e.import) { *rm = 0; *rc = jv_idx(js_module_namespace(vm, im)); return 1; }
        return mod_resolve(vm, im, e.import, seen, rm, rc);
    }
    if (name == A_DEFAULT) return 0;
    int found = 0;
    uint32_t sm = 0, sc = 0;
    for (uint32_t i = 0; i < n; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind != JX_STAR) continue;
        uint32_t im = req_module(vm, m, e.req), m2 = 0, c2 = 0;
        if (!im) continue;
        int r = mod_resolve(vm, im, name, seen, &m2, &c2);
        if (r < 0) return -1;
        if (r == 0) continue;
        if (!found) { found = 1; sm = m2; sc = c2; }
        else if (m2 != sm || c2 != sc) return -1;
    }
    if (!found) return 0;
    *rm = sm;
    *rc = sc;
    return 1;
}
static int mod_resolve_name(js_vm *vm, uint32_t m, uint32_t name, uint32_t *rm, uint32_t *rc) {
    vector<uint64_t> seen;
    return mod_resolve(vm, m, name, &seen, rm, rc);
}

/* GetExportedNames (16.2.1.6.2): the names a module exports, its star exports' too
   (default not among those), each once */
static void mod_export_names(js_vm *vm, uint32_t m, vector<uint32_t> *seen, vector<uint32_t> *names) {
    for (size_t i = 0; i < len(*seen); i++) if ((*seen)[i] == m) return;
    push_back(seen, m);
    uint32_t exp = vm->modules[m].exp, n = vm->modules[m].nexp;
    for (uint32_t i = 0; i < n; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind != JX_STAR) push_back(names, e.name);
    }
    for (uint32_t i = 0; i < n; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind != JX_STAR) continue;
        uint32_t im = req_module(vm, m, e.req);
        if (!im) continue;
        vector<uint32_t> star;
        mod_export_names(vm, im, seen, &star);
        for (size_t k = 0; k < len(star); k++) {
            uint32_t nm = at(star, k);
            if (nm == A_DEFAULT) continue;
            int dup = 0;
            for (size_t j = 0; j < len(*names) && !dup; j++) if ((*names)[j] == nm) dup = 1;
            if (!dup) push_back(names, nm);
        }
    }
}

/* a binding's value: the cell of the module it resolved to, or the namespace object */
static jv mod_binding_value(js_vm *vm, uint32_t rm, uint32_t rc) {
    if (!rm) return rc ? jv_obj(rc) : JV_HOLE;
    uint32_t env = vm->modules[rm].env;
    if (!env) return JV_HOLE;
    return vm->slots[vm->objs[env].slots + rc];
}

/* GetModuleNamespace (16.2.1.10): made once; its names sorted by their code units, the
   ambiguous ones left out, each a getter of its binding; Module its tag; frozen in shape */
jv js_module_namespace(js_vm *vm, uint32_t m) {
    if (vm->modules[m].ns) return jv_obj(vm->modules[m].ns);
    uint32_t o = js_obj_new(vm, JK_PLAIN, 0);
    vm->modules[m].ns = o;
    vector<uint32_t> seen, names, keep;
    mod_export_names(vm, m, &seen, &names);
    for (size_t i = 0; i < len(names); i++) {
        uint32_t rm = 0, rc = 0;
        if (mod_resolve_name(vm, m, at(names, i), &rm, &rc) == 1) push_back(keep, at(names, i));
    }
    /* insertion by code units: a module exports few names */
    for (size_t i = 1; i < len(keep); i++) {
        uint32_t x = at(keep, i);
        size_t j = i;
        while (j > 0 && js_str_cmp(vm, at(keep, j - 1), x) > 0) { keep[j] = at(keep, j - 1); j--; }
        keep[j] = x;
    }
    for (size_t i = 0; i < len(keep); i++) {
        uint32_t g = js_native_new(vm, vm->mod_first + JMD_NS_GET, at(keep, i), 0);
        vm->objs[g].x = jv_num(m);
        js_define_accessor(vm, o, at(keep, i), jv_obj(g), JV_HOLE, 0);
    }
    js_define(vm, o, JS_SYM_KEY | JSYM_TO_STRING_TAG, jv_str(js_str_ascii(vm, "Module")), JS_SHAPE_HIDDEN);
    vm->objs[o].flags &= (uint16_t)~JO_EXTENSIBLE;
    return jv_obj(o);
}

/* import.meta: made once, its url the module's, its resolve the module's resolution */
jv js_module_meta(js_vm *vm, uint32_t m) {
    if (!m) return JV_UNDEF;
    if (vm->modules[m].meta) return jv_obj(vm->modules[m].meta);
    uint32_t o = js_obj_new(vm, JK_PLAIN, 0);
    vm->modules[m].meta = o;
    js_define(vm, o, js_atom_ascii(vm, "url"), jv_str(vm->modules[m].url), 0);
    uint32_t f = js_native_new(vm, vm->mod_first + JMD_META_RESOLVE, js_atom_ascii(vm, "resolve"), 1);
    vm->objs[f].x = jv_num(m);
    js_define(vm, o, js_atom_ascii(vm, "resolve"), jv_obj(f), 0);
    return jv_obj(o);
}

/* ---- linking (16.2.1.5.1) ---- */

/* a SyntaxError naming the request and the export it lacks (or has twice over) */
static int link_error(js_vm *vm, uint32_t m, uint32_t req, uint32_t name, int ambiguous) {
    uint32_t s = js_str_concat(vm, js_str_ascii(vm, "The requested module '"), req_spec(vm, m, req));
    if (ambiguous) s = cat(vm, s, "' contains conflicting star exports for name '");
    else s = cat(vm, s, "' does not provide an export named '");
    s = cat(vm, js_str_concat(vm, s, name), "'");
    mod_throw(vm, vm->syntax_error_proto, s);
    return 0;
}

/* InitializeEnvironment (16.2.1.6.4): the indirect exports checked; the environment
   made, its vars undefined, its functions made, its imports bound */
static int mod_init_env(js_vm *vm, uint32_t m) {
    uint32_t exp = vm->modules[m].exp, nexp = vm->modules[m].nexp;
    for (uint32_t i = 0; i < nexp; i++) {
        js_modexp e = vm->mod_exps[exp + i];
        if (e.kind != JX_INDIRECT || !e.import) continue;
        uint32_t rm = 0, rc = 0;
        int r = mod_resolve_name(vm, m, e.name, &rm, &rc);
        if (vm->throwing) return 0;
        if (r != 1) return link_error(vm, m, e.req, e.import, r < 0);
    }
    uint32_t env = js_env_new(vm, 0, vm->modules[m].nenv);
    vm->modules[m].env = env;
    uint32_t init = vm->modules[m].init, ninit = vm->modules[m].ninit;
    for (uint32_t i = 0; i < ninit; i++) {
        js_modinit e = vm->mod_inits[init + i];
        jv v = JV_HOLE;
        if (e.kind == JMI_VAR) v = JV_UNDEF;
        else if (e.kind == JMI_FUNC) v = jv_obj(js_closure_new(vm, e.func, env, JV_UNDEF));
        vm->slots[vm->objs[env].slots + e.cell] = v;
    }
    uint32_t imp = vm->modules[m].imp, nimp = vm->modules[m].nimp;
    for (uint32_t i = 0; i < nimp; i++) {
        js_modimp e = vm->mod_imps[imp + i];
        uint32_t im = req_module(vm, m, e.req);
        if (!e.name) {
            jv ns = js_module_namespace(vm, im);
            vm->slots[vm->objs[env].slots + e.cell] = ns;
            continue;
        }
        uint32_t rm = 0, rc = 0;
        int r = mod_resolve_name(vm, im, e.name, &rm, &rc);
        if (vm->throwing) return 0;
        if (r != 1) return link_error(vm, m, e.req, e.name, r < 0);
        js_modbind b = { rm, rc };
        vm->mod_binds[e.bind] = b;
    }
    return 1;
}

/* InnerModuleLinking: depth first, a cycle's modules linked together */
static int mod_link_inner(js_vm *vm, uint32_t m, vector<uint32_t> *stack, uint32_t *index) {
    uint32_t st = vm->modules[m].status;
    if (st != JM_NEW) return 1;
    vm->modules[m].status = JM_LINKING;
    vm->modules[m].dfs = *index;
    vm->modules[m].anc = *index;
    (*index)++;
    push_back(stack, m);
    uint32_t n = vm->modules[m].nreq;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = req_module(vm, m, i);
        if (!mod_link_inner(vm, c, stack, index)) return 0;
        if (vm->modules[c].status == JM_LINKING && vm->modules[c].anc < vm->modules[m].anc) vm->modules[m].anc = vm->modules[c].anc;
    }
    if (!mod_init_env(vm, m)) return 0;
    if (vm->modules[m].anc == vm->modules[m].dfs) {
        for (;;) {
            uint32_t x = (*stack)[len(*stack) - 1];
            pop_back(stack);
            vm->modules[x].status = JM_LINKED;
            if (x == m) break;
        }
    }
    return 1;
}

/* Link: a module's graph linked; on an error the modules it was linking left unlinked */
static int mod_link(js_vm *vm, uint32_t m) {
    vector<uint32_t> stack;
    uint32_t index = 0;
    if (mod_link_inner(vm, m, &stack, &index)) return 1;
    for (size_t i = 0; i < len(stack); i++) {
        uint32_t x = at(stack, i);
        vm->modules[x].status = JM_NEW;
        vm->modules[x].env = 0;
    }
    return 0;
}

/* ---- evaluation (16.2.1.5.3) ---- */

/* the module's code run (it does not await): 0 when it threw */
static int mod_exec(js_vm *vm, uint32_t m) {
    uint32_t c = js_closure_new(vm, vm->modules[m].func, vm->modules[m].env, JV_UNDEF);
    js_call(vm, jv_obj(c), JV_UNDEF, 0, 0);
    return !vm->throwing;
}

/* ExecuteAsyncModule: its code (an async function's) run, its promise's settlement then
   the module's */
static void mod_exec_async(js_vm *vm, uint32_t m) {
    uint32_t c = js_closure_new(vm, vm->modules[m].func, vm->modules[m].env, JV_UNDEF);
    jv p = js_call(vm, jv_obj(c), JV_UNDEF, 0, 0);
    if (vm->throwing || !jv_is_obj(p)) return;
    uint32_t ok = js_native_new(vm, vm->mod_first + JMD_ASYNC_OK, A_EMPTY, 1);
    vm->objs[ok].x = jv_num(m);
    uint32_t err = js_native_new(vm, vm->mod_first + JMD_ASYNC_ERR, A_EMPTY, 1);
    vm->objs[err].x = jv_num(m);
    js_promise_react(vm, jv_idx(p), jv_obj(ok), jv_obj(err), JV_UNDEF, 0);
}

static void mod_parent_add(js_vm *vm, uint32_t child, uint32_t parent) {
    if (!vm->modules[child].parents) vm->modules[child].parents = js_array_new(vm, 0);
    js_array_push(vm, vm->modules[child].parents, jv_num(parent));
}

/* the cycle a module belongs to: its root's error, if it has one */
static int mod_failed(js_vm *vm, uint32_t m) {
    uint32_t r = vm->modules[m].cycle_root ? vm->modules[m].cycle_root : m;
    return (vm->modules[r].flags & JMF_ERROR) != 0;
}

static int mod_eval_inner(js_vm *vm, uint32_t m, vector<uint32_t> *stack, uint32_t *index) {
    uint32_t st = vm->modules[m].status;
    if (st == JM_EVALUATING_ASYNC || st == JM_EVALUATED) {
        if (vm->modules[m].flags & JMF_ERROR) { js_throw(vm, vm->modules[m].error); return 0; }
        return 1;
    }
    if (st == JM_EVALUATING) return 1;
    vm->modules[m].status = JM_EVALUATING;
    vm->modules[m].dfs = *index;
    vm->modules[m].anc = *index;
    vm->modules[m].pending = 0;
    (*index)++;
    push_back(stack, m);
    uint32_t n = vm->modules[m].nreq;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = req_module(vm, m, i);
        if (!mod_eval_inner(vm, c, stack, index)) return 0;
        if (vm->modules[c].status == JM_EVALUATING) {
            if (vm->modules[c].anc < vm->modules[m].anc) vm->modules[m].anc = vm->modules[c].anc;
        } else {
            if (vm->modules[c].cycle_root) c = vm->modules[c].cycle_root;
            if (vm->modules[c].flags & JMF_ERROR) { js_throw(vm, vm->modules[c].error); return 0; }
        }
        if (vm->modules[c].flags & JMF_ASYNC_EVAL) {
            vm->modules[m].pending++;
            mod_parent_add(vm, c, m);
        }
    }
    if (vm->modules[m].pending > 0 || (vm->modules[m].flags & JMF_TLA)) {
        vm->modules[m].flags |= JMF_ASYNC_EVAL;
        vm->modules[m].order = ++vm->mod_order;
        if (vm->modules[m].pending == 0) mod_exec_async(vm, m);
        if (vm->throwing) return 0;
    } else if (!mod_exec(vm, m)) return 0;
    if (vm->modules[m].anc == vm->modules[m].dfs) {
        for (;;) {
            uint32_t x = (*stack)[len(*stack) - 1];
            pop_back(stack);
            vm->modules[x].status = (vm->modules[x].flags & JMF_ASYNC_EVAL) ? JM_EVALUATING_ASYNC : JM_EVALUATED;
            vm->modules[x].cycle_root = m;
            if (x == m) break;
        }
    }
    return 1;
}

/* Evaluate: the module's graph evaluated, its promise (once made, the same one) */
static uint32_t mod_evaluate(js_vm *vm, uint32_t m) {
    uint32_t st = vm->modules[m].status;
    if ((st == JM_EVALUATING_ASYNC || st == JM_EVALUATED) && vm->modules[m].cycle_root) m = vm->modules[m].cycle_root;
    if (vm->modules[m].cap) return vm->modules[m].cap;
    uint32_t cap = js_promise_new(vm);
    vm->modules[m].cap = cap;
    vector<uint32_t> stack;
    uint32_t index = 0;
    if (!mod_eval_inner(vm, m, &stack, &index)) {
        if (vm->throwing != 1) return cap;
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        for (size_t i = 0; i < len(stack); i++) {
            uint32_t x = at(stack, i);
            vm->modules[x].status = JM_EVALUATED;
            vm->modules[x].flags |= JMF_ERROR;
            vm->modules[x].error = e;
        }
        js_promise_settle(vm, cap, 1, e);
    } else if (!(vm->modules[m].flags & JMF_ASYNC_EVAL)) js_promise_settle(vm, cap, 0, JV_UNDEF);
    return cap;
}

/* GatherAvailableAncestors: the parents whose last pending dependency this was */
static void mod_gather(js_vm *vm, uint32_t m, vector<uint32_t> *list) {
    uint32_t ps = vm->modules[m].parents;
    if (!ps) return;
    for (uint32_t i = 0; i < vm->objs[ps].b; i++) {
        uint32_t p = (uint32_t)jv_dbl(js_array_get(vm, ps, i));
        int in = 0;
        for (size_t j = 0; j < len(*list) && !in; j++) if ((*list)[j] == p) in = 1;
        if (in || mod_failed(vm, p)) continue;
        if (vm->modules[p].pending) vm->modules[p].pending--;
        if (vm->modules[p].pending == 0) {
            push_back(list, p);
            if (!(vm->modules[p].flags & JMF_TLA)) mod_gather(vm, p, list);
        }
    }
}

static void mod_async_rejected(js_vm *vm, uint32_t m, jv e) {
    if (vm->modules[m].status == JM_EVALUATED) return;
    vm->modules[m].error = e;
    vm->modules[m].flags |= JMF_ERROR;
    vm->modules[m].status = JM_EVALUATED;
    uint32_t ps = vm->modules[m].parents;
    if (ps) for (uint32_t i = 0; i < vm->objs[ps].b; i++) mod_async_rejected(vm, (uint32_t)jv_dbl(js_array_get(vm, ps, i)), e);
    if (vm->modules[m].cap) js_promise_settle(vm, vm->modules[m].cap, 1, e);
}

static void mod_async_fulfilled(js_vm *vm, uint32_t m) {
    if (vm->modules[m].status == JM_EVALUATED) return;
    vm->modules[m].flags &= ~(uint32_t)JMF_ASYNC_EVAL;
    vm->modules[m].status = JM_EVALUATED;
    if (vm->modules[m].cap) js_promise_settle(vm, vm->modules[m].cap, 0, JV_UNDEF);
    vector<uint32_t> list;
    mod_gather(vm, m, &list);
    /* in the order their async evaluations began */
    for (size_t i = 1; i < len(list); i++) {
        uint32_t x = at(list, i);
        size_t j = i;
        while (j > 0 && vm->modules[at(list, j - 1)].order > vm->modules[x].order) { list[j] = at(list, j - 1); j--; }
        list[j] = x;
    }
    for (size_t i = 0; i < len(list); i++) {
        uint32_t x = at(list, i);
        if (vm->modules[x].status == JM_EVALUATED) continue;
        if (vm->modules[x].flags & JMF_TLA) { mod_exec_async(vm, x); continue; }
        if (!mod_exec(vm, x)) {
            if (vm->throwing != 1) return;
            jv e = vm->exception;
            vm->throwing = 0;
            vm->exception = JV_UNDEF;
            mod_async_rejected(vm, x, e);
            continue;
        }
        vm->modules[x].flags &= ~(uint32_t)JMF_ASYNC_EVAL;
        vm->modules[x].status = JM_EVALUATED;
        if (vm->modules[x].cap) js_promise_settle(vm, vm->modules[x].cap, 0, JV_UNDEF);
    }
}

/* ---- the hosts' entries ---- */

/* a module's graph loaded, linked and evaluated: its evaluation's promise; 0 thrown */
static uint32_t mod_run(js_vm *vm, uint32_t m) {
    if (!m || !mod_load_graph(vm, m) || !mod_link(vm, m)) return 0;
    return mod_evaluate(vm, m);
}

uint32_t js_module_import(js_vm *vm, uint32_t spec, uint32_t referrer) {
    uint32_t m = mod_load(vm, spec, referrer);
    return mod_run(vm, m);
}

uint32_t js_module_main(js_vm *vm, uint32_t url, const char *src, size_t n) {
    uint32_t m = mod_find(vm, url);
    if (!m) m = mod_compile(vm, url, src, n);
    return mod_run(vm, m);
}

/* import(): a promise; the module loaded, linked and evaluated in a task of its own (as a
   fetch's completion would come), the promise resolved with its namespace or rejected
   with what failed */
jv js_module_dynamic(js_vm *vm, jv spec, uint32_t referrer) {
    uint32_t p = js_promise_new(vm);
    uint32_t s = js_to_string(vm, spec);
    if (!s) {
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        js_promise_settle(vm, p, 1, e);
        return jv_obj(p);
    }
    uint32_t rec = js_array_new(vm, 0);
    js_array_push(vm, rec, jv_str(s));
    js_array_push(vm, rec, referrer ? jv_str(referrer) : JV_UNDEF);
    js_array_push(vm, rec, jv_obj(p));
    uint32_t f = js_native_new(vm, vm->mod_first + JMD_DYN_RUN, A_EMPTY, 0);
    vm->objs[f].x = jv_obj(rec);
    js_timer t;
    memset(&t, 0, sizeof t);
    t.id = ++vm->timer_id;
    t.due = vm->now;
    t.interval = -1;
    t.fn = jv_obj(f);
    t.live = 1;
    t.seq = ++vm->timer_seq;
    t.run = vm->cur_run;
    push_back(vm->timers, t);
    return jv_obj(p);
}

/* the exception in flight: the promise's rejection */
static void reject_pending(js_vm *vm, uint32_t p) {
    if (vm->throwing != 1) return;
    jv e = vm->exception;
    vm->throwing = 0;
    vm->exception = JV_UNDEF;
    js_promise_settle(vm, p, 1, e);
}

jv js_module_native(js_vm *vm, uint32_t which, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt) {
    (void)thisv;
    (void)newt;
    uint32_t self = jv_idx(callee);
    jv a0 = argc ? vm->stack[args] : JV_UNDEF;
    switch (which) {
    case JMD_NS_GET: {
        /* a namespace's name: its binding read where it lives */
        uint32_t m = (uint32_t)jv_dbl(vm->objs[self].x), name = vm->objs[self].c, rm = 0, rc = 0;
        if (mod_resolve_name(vm, m, name, &rm, &rc) != 1) return JV_UNDEF;
        jv v = mod_binding_value(vm, rm, rc);
        if (v == JV_HOLE) {
            uint32_t s = cat(vm, js_str_concat(vm, js_str_ascii(vm, "Cannot access '"), name), "' before initialization");
            return mod_throw(vm, vm->reference_error_proto, s);
        }
        return v;
    }
    case JMD_ASYNC_OK: mod_async_fulfilled(vm, (uint32_t)jv_dbl(vm->objs[self].x)); return JV_UNDEF;
    case JMD_ASYNC_ERR: mod_async_rejected(vm, (uint32_t)jv_dbl(vm->objs[self].x), a0); return JV_UNDEF;
    case JMD_DYN_RUN: {
        uint32_t rec = jv_idx(vm->objs[self].x);
        jv sv = js_array_get(vm, rec, 0), rv = js_array_get(vm, rec, 1);
        uint32_t p = jv_idx(js_array_get(vm, rec, 2));
        uint32_t m = mod_load(vm, jv_idx(sv), jv_is_str(rv) ? jv_idx(rv) : 0);
        uint32_t cap = mod_run(vm, m);
        if (!cap) { reject_pending(vm, p); return JV_UNDEF; }
        uint32_t ok = js_native_new(vm, vm->mod_first + JMD_DYN_DONE, A_EMPTY, 1);
        uint32_t done = js_array_new(vm, 0);
        js_array_push(vm, done, jv_num(m));
        js_array_push(vm, done, jv_obj(p));
        vm->objs[ok].x = jv_obj(done);
        uint32_t err = js_native_new(vm, vm->mod_first + JMD_DYN_FAIL, A_EMPTY, 1);
        vm->objs[err].x = jv_obj(p);
        js_promise_react(vm, cap, jv_obj(ok), jv_obj(err), JV_UNDEF, 0);
        return JV_UNDEF;
    }
    case JMD_DYN_DONE: {
        uint32_t done = jv_idx(vm->objs[self].x);
        uint32_t m = (uint32_t)jv_dbl(js_array_get(vm, done, 0)), p = jv_idx(js_array_get(vm, done, 1));
        js_promise_resolve_with(vm, p, js_module_namespace(vm, m));
        return JV_UNDEF;
    }
    case JMD_DYN_FAIL: js_promise_settle(vm, jv_idx(vm->objs[self].x), 1, a0); return JV_UNDEF;
    case JMD_META_RESOLVE: {
        /* import.meta.resolve(specifier): the URL it names from this module, not loaded */
        uint32_t m = (uint32_t)jv_dbl(vm->objs[self].x);
        uint32_t s = js_to_string(vm, a0);
        if (!s) return JV_HOLE;
        uint32_t url = 0, source = 0;
        int ok = vm->load_module ? vm->load_module(vm, s, vm->modules[m].url, &url, &source) : file_loader(vm, s, vm->modules[m].url, &url, &source);
        if (vm->throwing) return JV_HOLE;
        if (!ok) return mod_throw(vm, vm->type_error_proto, source ? source : js_str_ascii(vm, "Failed to resolve the module specifier"));
        return jv_str(url);
    }
    default: return JV_UNDEF;
    }
}

void js_module_init(js_vm *vm, uint32_t first) {
    vm->mod_first = first;
    if (!len(vm->modules)) {
        js_module none;
        memset(&none, 0, sizeof none);
        push_back(vm->modules, none);
    }
}
