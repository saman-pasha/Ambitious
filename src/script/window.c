/* src/script/window.c -- the window's own objects: history (HTML 7.4), Web Storage (HTML
   12), screen and the window's sizes (CSSOM View), matchMedia (CSSOM View 4, Media Queries
   4) and navigator's fields.

   HISTORY is the session history of a tab opened blank and navigated to the page, as
   Playwright's is: about:blank, then the page. pushState(state, title, url) adds an entry
   after the current one (those after it dropped), replaceState replaces the current one:
   the state a structured clone (primitives, arrays and plain objects; a function refused),
   the URL resolved against the document's and of its origin, location changed with it, no
   request made. back(), forward() and go(n) traverse in a task: the entry's state and URL
   the document's, popstate fired, and hashchange when only the fragment changed; to the
   blank entry, or go(0), the page navigates away. Each pushState and replaceState is a
   navigation of the reasoner's (cause history).

   STORAGE: localStorage, an origin's, kept in the profile's directory (the storage file,
   a line a key: origin, key and value, tab-separated and escaped) read when first asked
   for and written when the page comes to rest; a private session's, or one with no profile,
   for the run; sessionStorage the run's. getItem, setItem, removeItem, clear, key, length,
   and the keys as properties. Each write is recorded with its key's name, never its value:
   whether the value looks like a credential (a JWT, or a token-like value under a key named
   for one) is all the record keeps of it. The storage event fires at the origin's other
   documents, of which a page here has none.

   SCREEN: 800x600, the viewport's, 24 bits; the window's outer size the viewport's, at
   (0, 0). matchMedia(query): a MediaQueryList whose matches is the query against the
   viewport (width, height, aspect-ratio, orientation, resolution, the user's preferences as
   Chromium's headless shell has them, hover and pointer, color), its media the query as
   CSSOM serializes it; the viewport never changes, so change never fires. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>
#include "../net/url.h"
amb_interface(dom);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "host.h"
#include "script.h"

int sh_utf8(js_vm *vm, jv v, vector<char> *out);
void sh_page_url_v(js_vm *vm, vector<char> *out);

enum {
    SW_PUSH = SH_WINDOW_FIRST, SW_REPLACE, SW_BACK, SW_FORWARD, SW_GO, SW_LENGTH, SW_STATE, SW_KEY, SW_GET_ITEM, SW_SET_ITEM,
    SW_REMOVE_ITEM, SW_CLEAR, SW_ST_LENGTH, SW_MATCH_MEDIA, SW_MQL_MATCHES, SW_MQL_ADD, SW_MQL_REMOVE, SW_ILLEGAL, SW_STORAGE_EVENT, SW_N
};
enum { SW_LOCAL = 1, SW_SESSION = 2 };

static uint32_t sw_atom(js_vm *vm, const char *s) { return js_atom_ascii(vm, s); }
static void sw_def(js_vm *vm, uint32_t o, const char *name, jv v) { js_define(vm, o, sw_atom(vm, name), v, 0); }

/* ---- history ---- */

/* a structured clone (HTML 2.7.3) of what JSON's data can be: primitives, arrays, plain
   objects (own enumerable string keys); a function or a host object refused, *bad the
   value refused (its string is the refusal's) */
static jv sw_clone(js_vm *vm, jv v, int depth, jv *bad) {
    if (!jv_is_obj(v)) return v;
    if (depth > 100) return js_throw_msg(vm, vm->range_error_proto, "Maximum call stack size exceeded");
    uint32_t o = jv_idx(v);
    if (js_is_callable(vm, v) || vm->objs[o].kind == JK_HOST) {
        *bad = v;
        return sh_dom_error(vm, "DataCloneError", "could not be cloned.");
    }
    jv keys = js_call(vm, js_get(vm, js_get(vm, jv_obj(vm->global), sw_atom(vm, "Object")), sw_atom(vm, "keys")), JV_UNDEF, &v, 1);
    if (vm->throwing) return JV_HOLE;
    int arr = js_truthy(vm, js_call(vm, js_get(vm, js_get(vm, jv_obj(vm->global), sw_atom(vm, "Array")), sw_atom(vm, "isArray")), JV_UNDEF, &v, 1));
    uint32_t c = arr ? js_array_new(vm, 0) : js_obj_new(vm, JK_PLAIN, vm->object_proto);
    jv lv = js_get(vm, keys, sw_atom(vm, "length"));
    uint32_t n = jv_is_num(lv) ? (uint32_t)jv_dbl(lv) : 0;
    for (uint32_t i = 0; i < n && !vm->throwing; i++) {
        jv k = js_array_get(vm, jv_idx(keys), i);
        uint32_t ka = js_atom(vm, js_to_string(vm, k));
        jv x = sw_clone(vm, js_get(vm, v, ka), depth + 1, bad);
        if (vm->throwing) return JV_HOLE;
        js_set(vm, jv_obj(c), ka, x);
    }
    return vm->throwing ? JV_HOLE : jv_obj(c);
}
/* the URL of an entry, from the host's history text */
static void sw_entry_url(struct js_host *h, uint32_t i, vector<char> *out) {
    clear(*out);
    append_range(*out, data(h->htext) + h->hents[i].uoff, h->hents[i].ulen);
}
/* the session history begun: the blank entry, then the page's */
static void sw_history_start(js_vm *vm) {
    struct js_host *h = vm->host;
    if (len(h->hents)) return;
    vector<char> page;
    sh_page_url_v(vm, &page);
    sh_hent blank, here;
    memset(&blank, 0, sizeof blank);
    blank.uoff = (uint32_t)len(h->htext);
    append_range(h->htext, "about:blank", 11);
    blank.ulen = 11;
    blank.state = JV_NULL;
    push_back(h->hents, blank);
    memset(&here, 0, sizeof here);
    here.uoff = (uint32_t)len(h->htext);
    append_range(h->htext, data(page), len(page));
    here.ulen = (uint32_t)len(page);
    here.state = JV_NULL;
    here.doc = 1;
    push_back(h->hents, here);
    h->hcur = 1;
}
/* a navigation record of the reasoner's for a history change */
static void sw_record(js_vm *vm, const char *method, const char *url, size_t n) {
    struct js_host *h = vm->host;
    script_navigation_h nh = navigation_new(h->rt);
    script_navigation *nv = &h->rt->navigation[nh.index];
    nv->run = vm->cur_run;
    nv->cause = SCRIPT_NAV_CAUSE_HISTORY;
    nv->method = text_add(h->rt, method, strlen(method));
    nv->url = text_add(h->rt, url, n);
}
static jv sw_push(js_vm *vm, int replace, jv state, jv url) {
    struct js_host *h = vm->host;
    sw_history_start(vm);
    static const char *const pre[] = { "Failed to execute 'pushState' on 'History': ", "Failed to execute 'replaceState' on 'History': " };
    jv bad = JV_UNDEF;
    jv cl = sw_clone(vm, state, 0, &bad);
    if (vm->throwing) {
        /* the clone's refusal, said as Chromium's: the value refused, as its string */
        if (bad == JV_UNDEF) return JV_HOLE;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        vector<char> what;
        if (!sh_utf8(vm, bad, &what)) return JV_HOLE;
        vector<char> msg;
        append_range(msg, pre[replace], strlen(pre[replace]));
        append_range(msg, data(what), len(what));
        append_range(msg, " could not be cloned.", 21);
        push_back(msg, 0);
        return sh_dom_error(vm, "DataCloneError", data(msg));
    }
    vector<char> page, ref;
    sh_page_url_v(vm, &page);
    char abs[8400];
    push_back(page, 0);
    if (!jv_is_nullish(url)) {
        if (!sh_utf8(vm, url, &ref)) return JV_HOLE;
        push_back(ref, 0);
        if (!net_url_resolve_enc(data(page), data(ref), vm->host->charset - 1, abs, sizeof abs)) {
            char msg[600];
            snprintf(msg, sizeof msg, "%sAn invalid URL '%s' was given.", pre[replace], data(ref));
            return sh_dom_error(vm, "SecurityError", msg);
        }
        /* the fragment the resolution drops, put back */
        size_t hn = 0;
        while (ref[hn] && ref[hn] != '#') hn++;
        if (ref[hn] == '#' && strlen(abs) + strlen(data(ref) + hn) < sizeof abs) strcat(abs, data(ref) + hn);
        char o1[600], o2[600];
        int a = net_url_origin(data(page), o1, sizeof o1), b = net_url_origin(abs, o2, sizeof o2);
        if (!a || !b || strcmp(o1, o2) != 0) {
            char msg[9000];
            /* (a char array beside a string literal in a conditional is miscompiled: a toolchain finding) */
            if (!a) snprintf(o1, sizeof o1, "null");
            snprintf(msg, sizeof msg, "%sA history state object with URL '%s' cannot be created in a document with origin '%s' and URL '%s'.", pre[replace],
                     abs, o1, data(page));
            return sh_dom_error(vm, "SecurityError", msg);
        }
    } else snprintf(abs, sizeof abs, "%s", data(page));
    /* the states kept: the history object holds them */
    js_array_push(vm, h->hstates, cl);
    sh_hent e;
    memset(&e, 0, sizeof e);
    e.uoff = (uint32_t)len(h->htext);
    append_range(h->htext, abs, strlen(abs));
    e.ulen = (uint32_t)strlen(abs);
    e.state = cl;
    e.doc = 1;
    if (replace) h->hents[h->hcur] = e;
    else {
        while (len(h->hents) > (size_t)h->hcur + 1) pop_back(h->hents);
        push_back(h->hents, e);
        h->hcur++;
    }
    sh_location_set(vm, abs);
    sw_record(vm, replace ? "replaceState" : "pushState", abs, strlen(abs));
    return JV_UNDEF;
}
/* a traversal by delta, in a task (HTML 7.4.6.4) */
static void sw_go(js_vm *vm, int delta) {
    struct js_host *h = vm->host;
    sw_history_start(vm);
    if (delta == 0) { script_navigate(vm, SCRIPT_NAV_CAUSE_RELOAD, data(h->htext) + h->hents[h->hcur].uoff, h->hents[h->hcur].ulen); return; }
    long to = (long)h->hcur + delta;
    if (to < 0 || to >= (long)len(h->hents)) return;
    sh_task(vm, 0, SH_TASK_TRAVERSE, (uint32_t)to);
}
void script_window_task(js_vm *vm, uint32_t kind, uint32_t arg) {
    struct js_host *h = vm->host;
    if (kind != SH_TASK_TRAVERSE || arg >= len(h->hents) || h->navigated) return;
    sh_hent e = h->hents[arg];
    vector<char> url, old;
    sw_entry_url(h, arg, &url);
    sw_entry_url(h, h->hcur, &old);
    h->hcur = arg;
    if (!e.doc) {
        /* the blank entry: another document's */
        push_back(url, 0);
        script_navigate(vm, SCRIPT_NAV_CAUSE_HISTORY, data(url), len(url) - 1);
        h->navigated = 1;
        return;
    }
    push_back(url, 0);
    push_back(old, 0);
    sh_location_set(vm, data(url));
    jv ev = sh_event_new(vm, sw_atom(vm, "popstate"), EV_TRUSTED);
    js_set_proto(vm, jv_idx(ev), h->protos[HP_POPSTATEEVENT]);
    js_define(vm, jv_idx(ev), sw_atom(vm, "state"), e.state, 0);
    js_define(vm, jv_idx(ev), sw_atom(vm, "hasUAVisualTransition"), JV_FALSE, 0);
    sh_dispatch(vm, JS_WINDOW, ev);
    /* only the fragment changed: hashchange too, in a task */
    size_t a = 0, b = 0;
    while (url[a] && url[a] != '#') a++;
    while (old[b] && old[b] != '#') b++;
    if (a == b && memcmp(data(url), data(old), a) == 0 && strcmp(data(url) + a, data(old) + b) != 0) {
        jv hc = sh_event_new(vm, sw_atom(vm, "hashchange"), EV_TRUSTED);
        js_set_proto(vm, jv_idx(hc), h->protos[HP_HASHCHANGEEVENT]);
        js_define(vm, jv_idx(hc), sw_atom(vm, "oldURL"), sh_cstr(vm, data(old)), 0);
        js_define(vm, jv_idx(hc), sw_atom(vm, "newURL"), sh_cstr(vm, data(url)), 0);
        sh_dispatch(vm, JS_WINDOW, hc);
    }
}

/* ---- storage ---- */

/* the storage file's text: \\, \t and \n escaped */
static void sw_escape(const char *s, size_t n, vector<char> *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\') append_range(*out, "\\\\", 2);
        else if (s[i] == '\t') append_range(*out, "\\t", 2);
        else if (s[i] == '\n') append_range(*out, "\\n", 2);
        else push_back(*out, s[i]);
    }
}
static void sw_unescape(const char *s, size_t n, vector<char> *out) {
    clear(*out);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            push_back(*out, s[i] == 't' ? '\t' : s[i] == 'n' ? '\n' : s[i]);
        } else push_back(*out, s[i]);
    }
}
/* a stored item: its area, origin, key and value in the host's storage text */
static void sw_item_add(struct js_host *h, uint32_t area, const char *o, size_t on, const char *k, size_t kn, const char *v, size_t vn) {
    sh_item it;
    memset(&it, 0, sizeof it);
    it.area = area;
    it.ooff = (uint32_t)len(h->stext);
    append_range(h->stext, o, on);
    it.olen = (uint32_t)on;
    it.koff = (uint32_t)len(h->stext);
    append_range(h->stext, k, kn);
    it.klen = (uint32_t)kn;
    it.voff = (uint32_t)len(h->stext);
    append_range(h->stext, v, vn);
    it.vlen = (uint32_t)vn;
    it.live = 1;
    push_back(h->items, it);
}
/* the page's origin, as storage keys it (a file: page's file://) */
static void sw_origin(js_vm *vm, vector<char> *out) {
    vector<char> page;
    sh_page_url_v(vm, &page);
    push_back(page, 0);
    char o[600];
    if (strncmp(data(page), "file:", 5) == 0) snprintf(o, sizeof o, "file://");
    else if (!net_url_origin(data(page), o, sizeof o)) snprintf(o, sizeof o, "null");
    clear(*out);
    append_range(*out, o, strlen(o));
}
/* localStorage read from the storage file, once */
static void sw_load(struct js_host *h) {
    if (h->sloaded) return;
    h->sloaded = 1;
    if (!len(h->spath)) return;
    vector<char> path;
    append_range(path, data(h->spath), len(h->spath));
    push_back(path, 0);
    vector<char> all;
    {
        own FILE *f = fopen(data(path), "rb");
        if (!f) return;
        defer(f) { fclose(f); }
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) append_range(all, buf, got);
    }
    size_t i = 0;
    while (i < len(all)) {
        size_t e = i;
        while (e < len(all) && all[e] != '\n') e++;
        size_t t1 = i;
        while (t1 < e && all[t1] != '\t') t1++;
        size_t t2 = t1 < e ? t1 + 1 : e;
        while (t2 < e && all[t2] != '\t') t2++;
        if (t2 < e) {
            vector<char> o, k, v;
            sw_unescape(data(all) + i, t1 - i, &o);
            sw_unescape(data(all) + t1 + 1, t2 - t1 - 1, &k);
            sw_unescape(data(all) + t2 + 1, e - t2 - 1, &v);
            sw_item_add(h, SW_LOCAL, data(o), len(o), data(k), len(k), data(v), len(v));
        }
        i = e + 1;
    }
}
void script_storage_file(js_vm *vm, const char *path) {
    clear(vm->host->spath);
    append_range(vm->host->spath, path, strlen(path));
}
/* localStorage written back, when it changed */
void script_storage_save(js_vm *vm) {
    struct js_host *h = vm->host;
    if (!h->sdirty || !len(h->spath)) return;
    h->sdirty = 0;
    vector<char> out, path;
    for (size_t i = 0; i < len(h->items); i++) {
        sh_item it = h->items[i];
        if (!it.live || it.area != SW_LOCAL) continue;
        sw_escape(data(h->stext) + it.ooff, it.olen, &out);
        push_back(out, '\t');
        sw_escape(data(h->stext) + it.koff, it.klen, &out);
        push_back(out, '\t');
        sw_escape(data(h->stext) + it.voff, it.vlen, &out);
        push_back(out, '\n');
    }
    append_range(path, data(h->spath), len(h->spath));
    push_back(path, 0);
    own FILE *f = fopen(data(path), "wb");
    if (!f) return;
    defer(f) { fclose(f); }
    fwrite(data(out), 1, len(out), f);
}
/* the storage object of a value: its area, 0 for none */
static uint32_t sw_area(js_vm *vm, jv thisv) {
    if (!jv_is_obj(thisv) || vm->objs[jv_idx(thisv)].kind != JK_HOST || vm->objs[jv_idx(thisv)].c != HK_STORAGE) return 0;
    return vm->objs[jv_idx(thisv)].a;
}
/* an area's item of a key for the page's origin: its place plus one, 0 for none */
static uint32_t sw_find(struct js_host *h, uint32_t area, const vector<char> *origin, const char *k, size_t kn) {
    for (size_t i = 0; i < len(h->items); i++) {
        sh_item it = h->items[i];
        if (!it.live || it.area != area || it.olen != len(*origin) || it.klen != kn) continue;
        if (memcmp(data(h->stext) + it.ooff, data(*origin), it.olen) == 0 && memcmp(data(h->stext) + it.koff, k, kn) == 0) return (uint32_t)i + 1;
    }
    return 0;
}
/* whether a key names a credential (token, password, secret ...) */
static int sw_key_secret(const char *k, size_t n) {
    static const char *const words[] = { "token", "password", "passwd", "secret", "auth", "session", "credential", "apikey", "api_key", "api-key",
                                         "jwt", "bearer", "private" };
    vector<char> low;
    for (size_t i = 0; i < n; i++) push_back(low, (char)(k[i] >= 'A' && k[i] <= 'Z' ? k[i] + 32 : k[i]));
    push_back(low, 0);
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) if (strstr(data(low), words[i])) return 1;
    return 0;
}
/* whether a value looks like a credential: a JWT's three base64url parts, or a token-like
   run (base64's or hex's characters, letters and digits, no space, 16 or more) under a key
   named for one */
static int sw_value_secret(const char *k, size_t kn, const char *v, size_t n) {
    int jwt = n > 10 && memcmp(v, "eyJ", 3) == 0, dots = 0, tokenish = n >= 16, letters = 0, digits = 0;
    for (size_t i = 0; i < n; i++) {
        char c = v[i];
        int b64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (c == '.') dots++;
        else if (!b64) jwt = 0;
        if (!b64 && c != '.' && c != '+' && c != '/' && c != '=') tokenish = 0;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters = 1;
        if (c >= '0' && c <= '9') digits = 1;
    }
    if (jwt && dots == 2) return 1;
    return tokenish && letters && digits && sw_key_secret(k, kn);
}
/* a write's record: its area, the operation, its key's name and the value's length,
   whether the value looked like a credential; never the value */
static void sw_write_rec(js_vm *vm, uint32_t area, script_storage_op op, const char *k, size_t kn, size_t vn, int secret) {
    struct js_host *h = vm->host;
    script_storage_h sh = storage_new(h->rt);
    script_storage *s = &h->rt->storage[sh.index];
    s->run = vm->cur_run;
    s->area = area == SW_LOCAL ? SCRIPT_STORAGE_AREA_LOCAL : SCRIPT_STORAGE_AREA_SESSION;
    s->op = op;
    s->key = text_add(h->rt, k, kn);
    s->bytes = (uint32_t)vn;
    s->secret = (uint8_t)secret;
}
jv script_storage_named(js_vm *vm, uint32_t o, uint32_t atom, int *handled) {
    struct js_host *h = vm->host;
    uint32_t idx;
    if (js_key_is_sym(atom) || js_has(vm, vm->objs[o].proto, atom)) return JV_UNDEF;
    (void)idx;
    vector<char> k, origin;
    js_str_to_utf8(vm, atom, &k);
    if (len(k) && k[0] == 1) return JV_UNDEF;
    sw_load(h);
    sw_origin(vm, &origin);
    uint32_t it = sw_find(h, vm->objs[o].a, &origin, data(k), len(k));
    if (!it) return JV_UNDEF;
    *handled = 1;
    sh_item e = h->items[it - 1];
    return sh_str(vm, data(h->stext) + e.voff, e.vlen);
}
static jv sw_set_item(js_vm *vm, uint32_t area, jv key, jv value) {
    struct js_host *h = vm->host;
    vector<char> k, v, origin;
    if (!sh_utf8(vm, key, &k) || !sh_utf8(vm, value, &v)) return JV_HOLE;
    sw_load(h);
    sw_origin(vm, &origin);
    uint32_t it = sw_find(h, area, &origin, data(k), len(k));
    if (it) h->items[it - 1].live = 0;
    sw_item_add(h, area, data(origin), len(origin), data(k), len(k), data(v), len(v));
    if (area == SW_LOCAL) h->sdirty = 1;
    sw_write_rec(vm, area, SCRIPT_STORAGE_OP_SET, data(k), len(k), len(v), sw_value_secret(data(k), len(k), data(v), len(v)));
    return JV_UNDEF;
}
int script_storage_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled) {
    if (js_key_is_sym(atom) || js_has(vm, vm->objs[o].proto, atom)) return 1;
    vector<char> k;
    js_str_to_utf8(vm, atom, &k);
    if (len(k) && k[0] == 1) return 1;
    *handled = 1;
    sw_set_item(vm, vm->objs[o].a, jv_str(atom), v);
    return !vm->throwing;
}

/* ---- matchMedia ---- */

static int sw_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
/* a length in px (px, em: 16px, 0), or a ratio's value, or a resolution in dppx; -1 when not one */
static double sw_value(const char *s, size_t n, int *ratio) {
    *ratio = 0;
    char buf[64];
    if (n >= sizeof buf) return -1;
    memcpy(buf, s, n);
    buf[n] = 0;
    char *slash = strchr(buf, '/');
    if (slash) {
        *ratio = 1;
        double a = atof(buf), b = atof(slash + 1);
        return b > 0 ? a / b : -1;
    }
    char *end = 0;
    double v = strtod(buf, &end);
    if (end == buf) return -1;
    while (*end && sw_ws(*end)) end++;
    if (!*end) return v == 0 ? 0 : (*ratio = 1, v);
    if (strcmp(end, "px") == 0) return v;
    if (strcmp(end, "em") == 0 || strcmp(end, "rem") == 0) return v * 16;
    if (strcmp(end, "dppx") == 0 || strcmp(end, "x") == 0) return v;
    if (strcmp(end, "dpi") == 0) return v / 96;
    if (strcmp(end, "dpcm") == 0) return v * 2.54 / 96;
    return -1;
}
/* a feature's value against the viewport: 1 true, 0 false, -1 not a feature this knows */
static int sw_feature(const char *name, size_t nn, const char *val, size_t vn, int has_val, int cmp) {
    double w = 800, hh = 600;
#define SW_IS(s) (nn == strlen(s) && memcmp(name, s, nn) == 0)
#define SW_VAL(s) (vn == strlen(s) && memcmp(val, s, vn) == 0)
    int ratio = 0;
    double v = has_val ? sw_value(val, vn, &ratio) : 0;
    double actual = -1;
    const char *base = name;
    size_t bn = nn;
    int mode = cmp;   /* 0 equal, 1 min (>=), 2 max (<=), 3 >, 4 < */
    if (!cmp && nn > 4 && memcmp(name, "min-", 4) == 0) { base = name + 4; bn = nn - 4; mode = 1; }
    else if (!cmp && nn > 4 && memcmp(name, "max-", 4) == 0) { base = name + 4; bn = nn - 4; mode = 2; }
    if (bn == 5 && memcmp(base, "width", 5) == 0) actual = w;
    else if (bn == 6 && memcmp(base, "height", 6) == 0) actual = hh;
    else if (bn == 12 && memcmp(base, "aspect-ratio", 12) == 0) actual = w / hh;
    else if (bn == 10 && memcmp(base, "resolution", 10) == 0) actual = 1;
    else if (bn == 5 && memcmp(base, "color", 5) == 0) actual = 8;
    else if (bn == 10 && memcmp(base, "monochrome", 10) == 0) actual = 0;
    if (actual >= 0) {
        if (!has_val) return actual != 0;
        if (v < 0) return -1;
        if (mode == 0) return actual == v;
        if (mode == 1) return actual >= v;
        if (mode == 2) return actual <= v;
        if (mode == 3) return actual > v;
        return actual < v;
    }
    if (mode) return -1;
    if (SW_IS("orientation")) return has_val ? SW_VAL("landscape") : 1;
    if (SW_IS("prefers-color-scheme")) return has_val ? SW_VAL("light") : 1;
    if (SW_IS("prefers-reduced-motion") || SW_IS("prefers-contrast") || SW_IS("prefers-reduced-transparency") || SW_IS("prefers-reduced-data"))
        return has_val ? SW_VAL("no-preference") : 0;
    if (SW_IS("forced-colors") || SW_IS("inverted-colors")) return has_val ? SW_VAL("none") : 0;
    if (SW_IS("hover") || SW_IS("any-hover")) return has_val ? SW_VAL("hover") : 1;
    if (SW_IS("pointer") || SW_IS("any-pointer")) return has_val ? SW_VAL("fine") : 1;
    if (SW_IS("display-mode")) return has_val ? SW_VAL("browser") : 1;
    if (SW_IS("scripting")) return has_val ? SW_VAL("enabled") : 1;
    if (SW_IS("grid")) return has_val ? SW_VAL("0") : 0;
    if (SW_IS("color-gamut")) return has_val ? SW_VAL("srgb") : 1;
    if (SW_IS("update")) return has_val ? SW_VAL("fast") : 1;
#undef SW_IS
#undef SW_VAL
    return -1;
}
/* a feature in parentheses, plain or a range ((width >= 600px), (400px < width < 1000px)):
   its value, and its serialization appended; -1 not one this reads */
static int sw_paren(const char *s, size_t n, vector<char> *ser) {
    size_t c = 0;
    while (c < n && s[c] != ':' && s[c] != '<' && s[c] != '>' && s[c] != '=') c++;
    if (c >= n) {
        /* a boolean feature */
        size_t a = 0, b = n;
        while (a < b && sw_ws(s[a])) a++;
        while (b > a && sw_ws(s[b - 1])) b--;
        push_back(*ser, '(');
        append_range(*ser, s + a, b - a);
        push_back(*ser, ')');
        return sw_feature(s + a, b - a, "", 0, 0, 0);
    }
    if (s[c] == ':') {
        size_t a = 0, b = c, va = c + 1, vb = n;
        while (a < b && sw_ws(s[a])) a++;
        while (b > a && sw_ws(s[b - 1])) b--;
        while (va < vb && sw_ws(s[va])) va++;
        while (vb > va && sw_ws(s[vb - 1])) vb--;
        push_back(*ser, '(');
        append_range(*ser, s + a, b - a);
        append_range(*ser, ": ", 2);
        /* a ratio written with spaces about its slash */
        for (size_t i = va; i < vb; i++) {
            if (s[i] == '/') {
                while (len(*ser) && (*ser)[len(*ser) - 1] == ' ') pop_back(*ser);
                append_range(*ser, " / ", 3);
                while (i + 1 < vb && sw_ws(s[i + 1])) i++;
            } else if (!sw_ws(s[i]) || (i + 1 < vb && s[i + 1] != '/')) push_back(*ser, s[i]);
        }
        push_back(*ser, ')');
        return sw_feature(s + a, b - a, s + va, vb - va, 1, 0);
    }
    /* a range: the text as it is, its comparisons each checked */
    push_back(*ser, '(');
    append_range(*ser, s, n);
    push_back(*ser, ')');
    /* split into operands and operators */
    vector<uint32_t> parts;
    size_t i = 0;
    while (i < n) {
        while (i < n && sw_ws(s[i])) i++;
        size_t b = i;
        if (i < n && (s[i] == '<' || s[i] == '>' || s[i] == '=')) { i++; if (i < n && s[i] == '=') i++; }
        else while (i < n && !sw_ws(s[i]) && s[i] != '<' && s[i] != '>' && s[i] != '=') i++;
        if (i > b) { push_back(parts, (uint32_t)b); push_back(parts, (uint32_t)(i - b)); }
    }
    size_t np = len(parts) / 2;
    if (np != 3 && np != 5) return -1;
    int all = 1;
    for (size_t k = 0; k + 2 < np; k += 2) {
        const char *l = s + parts[2 * k], *op = s + parts[2 * k + 2], *r = s + parts[2 * k + 4];
        size_t ln = parts[2 * k + 1], opn = parts[2 * k + 3], rn = parts[2 * k + 5];
        /* the feature's side: the name's (letters and hyphens) */
        int name_left = (l[0] >= 'a' && l[0] <= 'z');
        int lt = op[0] == '<', ge = opn == 2, eq = op[0] == '=';
        int mode;
        if (eq) mode = 0;
        else if (name_left) mode = lt ? (ge ? 2 : 4) : (ge ? 1 : 3);
        else mode = lt ? (ge ? 1 : 3) : (ge ? 2 : 4);
        int got = name_left ? sw_feature(l, ln, r, rn, 1, mode ? mode : 5) : sw_feature(r, rn, l, ln, 1, mode ? mode : 5);
        if (mode == 0) got = name_left ? sw_feature(l, ln, r, rn, 1, 0) : sw_feature(r, rn, l, ln, 1, 0);
        if (got < 0) return -1;
        if (!got) all = 0;
    }
    return all;
}
/* one query: [not|only] type? (and feature)*: 1, 0, or -1 when it is not one */
static int sw_query(const char *s, size_t n, vector<char> *ser) {
    size_t i = 0;
    int neg = 0, ok = 1, words = 0, features = 0;
    vector<char> type, conds;
    while (i < n) {
        while (i < n && sw_ws(s[i])) i++;
        if (i >= n) break;
        if (s[i] == '(') {
            size_t depth = 0, e = i;
            for (; e < n; e++) { if (s[e] == '(') depth++; else if (s[e] == ')' && --depth == 0) break; }
            if (e >= n) return -1;
            if (len(conds)) append_range(conds, " and ", 5);
            int got = sw_paren(s + i + 1, e - i - 1, &conds);
            if (got < 0) return -1;
            if (!got) ok = 0;
            features++;
            i = e + 1;
            continue;
        }
        size_t b = i;
        while (i < n && !sw_ws(s[i]) && s[i] != '(') i++;
        const char *w = s + b;
        size_t wn = i - b;
        words++;
        if (wn == 3 && memcmp(w, "not", 3) == 0 && words == 1) { neg = 1; continue; }
        if (wn == 4 && memcmp(w, "only", 4) == 0 && words == 1) { append_range(type, "only ", 5); continue; }
        if (wn == 3 && memcmp(w, "and", 3) == 0) continue;
        if (len(type) && type[len(type) - 1] != ' ') return -1;
        if (wn == 3 && memcmp(w, "all", 3) == 0) { append_range(type, "all", 3); continue; }
        if (wn == 6 && memcmp(w, "screen", 6) == 0) { append_range(type, "screen", 6); continue; }
        if ((wn == 5 && memcmp(w, "print", 5) == 0) || (wn == 6 && memcmp(w, "speech", 6) == 0)) { append_range(type, w, wn); ok = 0; continue; }
        return -1;
    }
    /* the serialization: not, the type (all omitted before conditions), the conditions */
    if (neg) append_range(*ser, "not ", 4);
    int all_only = len(type) == 3 && memcmp(data(type), "all", 3) == 0;
    if (len(type) && !(all_only && features)) {
        append_range(*ser, data(type), len(type));
        if (features) append_range(*ser, " and ", 5);
    }
    append_range(*ser, data(conds), len(conds));
    return neg ? !ok : ok;
}
/* a media query list: its matching and its serialization (an invalid one's text as given) */
static int sw_media(const char *q, size_t n, vector<char> *ser) {
    vector<char> low;
    for (size_t i = 0; i < n; i++) push_back(low, (char)(q[i] >= 'A' && q[i] <= 'Z' ? q[i] + 32 : q[i]));
    size_t a = 0;
    while (a < n && sw_ws(low[a])) a++;
    if (a >= n) return 1;
    int any = 0, bad = 0;
    clear(*ser);
    size_t i = 0;
    while (i <= n) {
        size_t e = i;
        int depth = 0;
        while (e < n && (low[e] != ',' || depth)) { if (low[e] == '(') depth++; else if (low[e] == ')') depth--; e++; }
        vector<char> one;
        int got = sw_query(data(low) + i, e - i, &one);
        if (got < 0) bad = 1;
        if (got > 0) any = 1;
        if (len(*ser)) append_range(*ser, ", ", 2);
        append_range(*ser, data(one), len(one));
        i = e + 1;
    }
    if (bad) {
        clear(*ser);
        append_range(*ser, q, n);
        return 0;
    }
    return any;
}

jv script_window_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, int *handled) {
    (void)callee;
    struct js_host *h = vm->host;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1), a2 = js_arg(vm, args, argc, 2);
    switch (id) {
    case SW_ILLEGAL: return js_throw_msg(vm, vm->type_error_proto, "Illegal constructor");
    case SW_PUSH: case SW_REPLACE: return sw_push(vm, id == SW_REPLACE, a0, a2);
    case SW_BACK: sw_go(vm, -1); return JV_UNDEF;
    case SW_FORWARD: sw_go(vm, 1); return JV_UNDEF;
    case SW_GO: {
        double n = argc ? js_to_number(vm, a0) : 0;
        if (vm->throwing) return JV_HOLE;
        sw_go(vm, n != n ? 0 : (int)n);
        return JV_UNDEF;
    }
    case SW_LENGTH: sw_history_start(vm); return jv_num((double)len(h->hents));
    case SW_STATE: sw_history_start(vm); return h->hents[h->hcur].state;
    case SW_KEY: case SW_GET_ITEM: case SW_SET_ITEM: case SW_REMOVE_ITEM: case SW_CLEAR: case SW_ST_LENGTH: {
        uint32_t area = sw_area(vm, thisv);
        if (!area) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        sw_load(h);
        vector<char> origin, k;
        sw_origin(vm, &origin);
        if (id == SW_SET_ITEM) {
            if (argc < 2) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'setItem' on 'Storage': 2 arguments required, but only 1 present.");
            return sw_set_item(vm, area, a0, a1);
        }
        if (id == SW_ST_LENGTH || id == SW_KEY || id == SW_CLEAR) {
            double want = id == SW_KEY ? js_to_number(vm, a0) : -1;
            uint32_t n = 0;
            for (size_t i = 0; i < len(h->items); i++) {
                sh_item it = h->items[i];
                if (!it.live || it.area != area || it.olen != len(origin) || memcmp(data(h->stext) + it.ooff, data(origin), it.olen) != 0) continue;
                if (id == SW_KEY && (double)n == (double)(uint32_t)want) return sh_str(vm, data(h->stext) + it.koff, it.klen);
                if (id == SW_CLEAR) h->items[i].live = 0;
                n++;
            }
            if (id == SW_CLEAR) {
                if (area == SW_LOCAL) h->sdirty = 1;
                if (n) sw_write_rec(vm, area, SCRIPT_STORAGE_OP_CLEAR, "", 0, 0, 0);
                return JV_UNDEF;
            }
            return id == SW_KEY ? JV_NULL : jv_num(n);
        }
        if (!sh_utf8(vm, a0, &k)) return JV_HOLE;
        uint32_t it = sw_find(h, area, &origin, data(k), len(k));
        if (id == SW_GET_ITEM) return it ? sh_str(vm, data(h->stext) + h->items[it - 1].voff, h->items[it - 1].vlen) : JV_NULL;
        if (it) {
            h->items[it - 1].live = 0;
            if (area == SW_LOCAL) h->sdirty = 1;
            sw_write_rec(vm, area, SCRIPT_STORAGE_OP_REMOVE, data(k), len(k), 0, 0);
        }
        return JV_UNDEF;
    }
    case SW_MATCH_MEDIA: {
        vector<char> q, ser;
        if (!argc) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'matchMedia' on 'Window': 1 argument required, but only 0 present.");
        if (!sh_utf8(vm, a0, &q)) return JV_HOLE;
        sw_media(data(q), len(q), &ser);
        uint32_t o = js_obj_new(vm, JK_HOST, h->protos[HP_MEDIAQUERYLIST]);
        vm->objs[o].c = HK_MQL;
        js_define(vm, o, sw_atom(vm, "media"), sh_str(vm, data(ser), len(ser)), 0);
        js_define(vm, o, sw_atom(vm, "\001query"), sh_str(vm, data(q), len(q)), JS_SHAPE_HIDDEN);
        js_define(vm, o, sw_atom(vm, "onchange"), JV_NULL, 0);
        return jv_obj(o);
    }
    case SW_MQL_MATCHES: {
        jv q = jv_is_obj(thisv) ? js_get(vm, thisv, sw_atom(vm, "\001query")) : JV_UNDEF;
        vector<char> t, ser;
        if (!jv_is_str(q)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        js_str_to_utf8(vm, jv_idx(q), &t);
        return jv_bool(sw_media(data(t), len(t), &ser));
    }
    case SW_MQL_ADD: case SW_MQL_REMOVE: {
        /* change listeners (addListener, addEventListener), which the fixed viewport never calls */
        return JV_UNDEF;
    }
    case SW_STORAGE_EVENT: {
        vector<char> t;
        if (!sh_utf8(vm, a0, &t)) return JV_HOLE;
        uint32_t f = 0;
        if (jv_is_obj(a1)) {
            if (js_truthy(vm, js_get(vm, a1, sw_atom(vm, "bubbles")))) f |= EV_BUBBLES;
            if (js_truthy(vm, js_get(vm, a1, sw_atom(vm, "cancelable")))) f |= EV_CANCELABLE;
        }
        jv ev = sh_event_new(vm, js_atom(vm, js_str_utf8(vm, data(t), len(t))), f);
        js_set_proto(vm, jv_idx(ev), h->protos[HP_STORAGEEVENT]);
        static const char *const fs[] = { "key", "oldValue", "newValue", "url", "storageArea" };
        for (int i = 0; i < 5; i++) {
            jv v = jv_is_obj(a1) ? js_get(vm, a1, sw_atom(vm, fs[i])) : JV_UNDEF;
            if (i == 3) { js_define(vm, jv_idx(ev), sw_atom(vm, fs[i]), v == JV_UNDEF ? jv_str(A_EMPTY) : v, 0); continue; }
            js_define(vm, jv_idx(ev), sw_atom(vm, fs[i]), v == JV_UNDEF ? JV_NULL : v, 0);
        }
        return ev;
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* the collector's roots of this unit (script.h's sh_hent, the host's hstates): each
   history entry's state, and the array that keeps every state pushed (the stored items are
   text, no value) */
void script_window_roots(js_vm *vm, vector<jv> *out) {
    struct js_host *h = vm->host;
    for (size_t i = 0; i < len(h->hents); i++) push_back(*out, h->hents[i].state);
    if (h->hstates) push_back(*out, jv_obj(h->hstates));
}

void script_window_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t g = vm->global;
    /* history */
    uint32_t hp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    static const char *const hm[] = { "pushState", "replaceState", "back", "forward", "go" };
    static const uint32_t hn[] = { 2, 2, 0, 0, 0 };
    for (int i = 0; i < 5; i++) sh_method(vm, hp, hm[i], SW_PUSH + (uint32_t)i, hn[i]);
    static const char *const hg[] = { "length", "state" };
    for (int i = 0; i < 2; i++) {
        uint32_t a = sw_atom(vm, hg[i]);
        js_define_accessor(vm, hp, a, jv_obj(js_native_new(vm, SW_LENGTH + (uint32_t)i, a, 0)), JV_UNDEF, 0);
    }
    sh_interface(vm, "History", SW_ILLEGAL, hp);
    uint32_t hist = js_obj_new(vm, JK_PLAIN, hp);
    sw_def(vm, hist, "scrollRestoration", sh_cstr(vm, "auto"));
    h->hstates = js_array_new(vm, 0);
    js_define(vm, hist, sw_atom(vm, "\001states"), jv_obj(h->hstates), JS_SHAPE_HIDDEN);
    js_define(vm, g, sw_atom(vm, "history"), jv_obj(hist), JS_SHAPE_HIDDEN);
    /* storage */
    uint32_t sp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_STORAGE] = sp;
    static const char *const sm[] = { "key", "getItem", "setItem", "removeItem", "clear" };
    static const uint32_t sn[] = { 1, 1, 2, 1, 0 };
    for (int i = 0; i < 5; i++) sh_method(vm, sp, sm[i], SW_KEY + (uint32_t)i, sn[i]);
    uint32_t la = sw_atom(vm, "length");
    js_define_accessor(vm, sp, la, jv_obj(js_native_new(vm, SW_ST_LENGTH, la, 0)), JV_UNDEF, 0);
    sh_interface(vm, "Storage", SW_ILLEGAL, sp);
    static const char *const areas[] = { "localStorage", "sessionStorage" };
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t o = js_obj_new(vm, JK_HOST, sp);
        vm->objs[o].c = HK_STORAGE;
        vm->objs[o].a = i + 1;
        js_define(vm, g, sw_atom(vm, areas[i]), jv_obj(o), JS_SHAPE_HIDDEN);
    }
    uint32_t sep = js_obj_new(vm, JK_PLAIN, h->protos[HP_EVENT]);
    h->protos[HP_STORAGEEVENT] = sep;
    sh_interface(vm, "StorageEvent", SW_STORAGE_EVENT, sep);
    /* screen and the window's outer box */
    uint32_t scp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    sh_interface(vm, "Screen", SW_ILLEGAL, scp);
    uint32_t scr = js_obj_new(vm, JK_PLAIN, scp);
    static const char *const sk[] = { "width", "height", "availWidth", "availHeight", "availLeft", "availTop", "colorDepth", "pixelDepth" };
    double sv[] = { h->view_w, h->view_h, h->view_w, h->view_h, 0, 0, 24, 24 };
    for (int i = 0; i < 8; i++) sw_def(vm, scr, sk[i], jv_num(sv[i]));
    sw_def(vm, scr, "isExtended", JV_FALSE);
    uint32_t ori = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    sw_def(vm, ori, "type", sh_cstr(vm, "landscape-primary"));
    sw_def(vm, ori, "angle", jv_num(0));
    sw_def(vm, scr, "orientation", jv_obj(ori));
    js_define(vm, g, sw_atom(vm, "screen"), jv_obj(scr), JS_SHAPE_HIDDEN);
    static const char *const ok[] = { "outerWidth", "outerHeight", "screenX", "screenY", "screenLeft", "screenTop" };
    double ov[] = { h->view_w, h->view_h, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) js_define(vm, g, sw_atom(vm, ok[i]), jv_num(ov[i]), JS_SHAPE_HIDDEN);
    /* matchMedia */
    sh_method(vm, g, "matchMedia", SW_MATCH_MEDIA, 1);
    uint32_t mp = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_MEDIAQUERYLIST] = mp;
    uint32_t ma = sw_atom(vm, "matches");
    js_define_accessor(vm, mp, ma, jv_obj(js_native_new(vm, SW_MQL_MATCHES, ma, 0)), JV_UNDEF, 0);
    sh_method(vm, mp, "addListener", SW_MQL_ADD, 1);
    sh_method(vm, mp, "removeListener", SW_MQL_REMOVE, 1);
    sh_method(vm, mp, "addEventListener", SW_MQL_ADD, 2);
    sh_method(vm, mp, "removeEventListener", SW_MQL_REMOVE, 2);
    sh_interface(vm, "MediaQueryList", SW_ILLEGAL, mp);
}
