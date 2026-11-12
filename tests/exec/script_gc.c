/* tests/exec/script_gc.c -- the collector for a page's scripts, under valgrind: every
   scripted fixture page read from its file (tests/pages: the observers', the custom
   elements' and the shadow trees', the events' and the keys' and the counter's with their
   users' actions, the inserted and written scripts', the live collections', the view's,
   the storage's, gc-host.html's values the host alone holds ...) loaded and run as
   `ambitious script` runs them, with AMB_GC_STRESS set: a collection at every moment
   between tasks (src/js/gc.c), the host's values among its roots (src/script/script.h).
   Each page's console, then its uncaught errors, as the gates extract them, against its
   Chromium's (tests/pages/NAME.chromium, read now), or printed for a page with none; and
   the collections each made. The exec gate runs it under valgrind as well: no invalid
   access, no leak. The pages a server serves are tests/integ/script-gc.sh's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(script);
#include "../../src/js/js.h"
#include "../../src/script/script.h"
#include "../../src/html/encoding.h"

/* the users' actions of the pages that have them, as their NAME.actions give them to the
   command: an action, its argument (an element's selector, or a key's name) and its text,
   three strings an action; events.html's nine first, then keys.html's fifteen, then the
   counter's two clicks (tests/integ/script.sh's) */
static const char *const acts[] = {
    "click", "#b", "", "type", "#t", "aB", "key", "Backspace", "", "key", "Tab", "", "key", "Enter", "",
    "dblclick", "#d", "", "key", "x", "", "key", "Tab", "", "click", "#r", "",
    "focus", "#first", "", "key", "Tab", "", "key", "Space", "", "key", "Tab", "", "type", "#ta", "x", "key", "Enter", "",
    "key", "Shift+Tab", "", "key", "Shift+Tab", "", "key", "Shift+Tab", "", "key", "Enter", "", "key", "Tab", "",
    "key", "Control+a", "", "click", "#lab", "", "key", "Space", "", "click", "#last", "",
    "click", "#inc", "", "click", "#inc", "" };
/* the virtual time the timers run for at the page's rest, as the command's */
#define HORIZON 10000.0

/* an action's kind by its name: SCRIPT_ACT_*, 0 for none */
static uint32_t act_kind(uint32_t k) {
    if (strcmp(acts[3 * k], "click") == 0) return SCRIPT_ACT_CLICK;
    if (strcmp(acts[3 * k], "dblclick") == 0) return SCRIPT_ACT_DBLCLICK;
    if (strcmp(acts[3 * k], "focus") == 0) return SCRIPT_ACT_FOCUS;
    if (strcmp(acts[3 * k], "type") == 0) return SCRIPT_ACT_TYPE;
    if (strcmp(acts[3 * k], "key") == 0) return SCRIPT_ACT_KEY;
    return 0;
}

/* a file's bytes appended to out: 0 when it cannot be read */
static int read_file(const char *path, vector<char> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) append_range(*out, buf, got);
    return 1;
}

/* the page of a file loaded, its scripts run (then the actions from first, n of them, the
   timers due between them, the last one's for the horizon), the page at rest; its console
   and its errors against its Chromium's, or printed; the collections made. 0 when it
   cannot be loaded or its console differs */
static int run_page(const char *path, uint32_t first, uint32_t n) {
    char url[4200], abs[4096], ref[4200];
    if (!realpath(path, abs)) { printf("%s: no such file\n", path); return 0; }
    snprintf(url, sizeof url, "file://%s", abs);
    dom_document d;
    html_parser p;
    net_loader nt;
    script_runtime sr;
    js_vm vm;
    struct js_host hs;
    SCRIPT_HOST(hs, vm, d, nt, sr);
    defer(vm) { JS_VM_DROP(vm); }
    defer(hs) { SCRIPT_HOST_DROP(hs); }
    dom_init(&d, 1);
    html_init(&p, 1);
    net_init(&nt, 1);
    script_init(&sr, 1);
    net_resource_h ph = net_fetch(&nt, url, strlen(url), NET_KIND_DOCUMENT);
    if (nt.resource[ph.index].outcome != NET_OUTCOME_OK) { printf("%s: not loaded\n", path); return 0; }
    vector<char> src;
    append_range(src, bytes_ptr(&nt, nt.resource[ph.index].body), nt.resource[ph.index].body.len);
    amb_span ty = nt.resource[ph.index].type;
    char type[300];
    snprintf(type, sizeof type, "%.*s", (int)ty.len, text_ptr(&nt, ty));
    html_set_scripting(1);
    html_parse_bytes(&p, &d, data(src), len(src), type, strlen(type), 1);
    if (p.encoding_len > 1) {
        amb_span en = p.encoding[p.encoding_len - 1].name;
        script_charset(&vm, bytes_ptr(&p, en), en.len);
    }
    net_fetch_subresources(&nt, &d, ph, 0);
    hs.page = ph.index;
    script_run_page(&vm, n ? 0 : HORIZON);
    for (uint32_t k = first; k < first + n; k++) {
        uint32_t kind = act_kind(k);
        double horizon = k + 1 == first + n ? HORIZON : 0;
        if (kind == SCRIPT_ACT_KEY) script_action(&vm, kind, 0, acts[3 * k + 1], strlen(acts[3 * k + 1]), horizon);
        else {
            uint32_t x = script_select(&d, acts[3 * k + 1], strlen(acts[3 * k + 1]));
            if (!x) printf("%s: --%s %s: no element matches\n", path, acts[3 * k], acts[3 * k + 1]);
            script_action(&vm, x ? kind : 0, x, acts[3 * k + 2], strlen(acts[3 * k + 2]), horizon);
        }
    }
    script_settle(&vm);
    /* the console as the gates extract it: the messages, then the errors */
    vector<char> got;
    size_t lines = 0;
    for (uint32_t c = 1; c < sr.console_len; c++) {
        if (!sr.console[c].live) continue;
        amb_span t = sr.console[c].text;
        append_range(got, text_ptr(&sr, t), t.len);
        push_back(got, '\n');
        lines++;
    }
    for (uint32_t e = 1; e < sr.error_len; e++) {
        if (!sr.error[e].live) continue;
        amb_span t = sr.error[e].message;
        append_range(got, "error: ", 7);
        append_range(got, text_ptr(&sr, t), t.len);
        push_back(got, '\n');
        lines++;
    }
    /* the page's name: its path's last part (an index: a pointer a function returns, kept, is
       refused when the function returns early -- toolchain/REPORT.md, 15) */
    size_t base = 0;
    for (size_t i = 0; path[i]; i++) if (path[i] == '/') base = i + 1;
    snprintf(ref, sizeof ref, "%.*s.chromium", (int)(strlen(path) - 5), path);
    vector<char> want;
    if (!read_file(ref, &want)) {
        printf("%s%s: %zu console lines, %u collections:\n%.*s", path + base, n ? " after its actions" : "", lines, vm.gc_count, (int)len(got), data(got));
        return 1;
    }
    if (len(want) == len(got) && memcmp(data(want), data(got), len(got)) == 0) {
        printf("%s%s: Chromium's console, %zu lines; %u collections\n", path + base, n ? " after its actions" : "", lines, vm.gc_count);
        return 1;
    }
    printf("%s: differs from Chromium's console; Chromium's:\n%.*s-- this one's:\n%.*s", path + base, (int)len(want), data(want), (int)len(got), data(got));
    return 0;
}

int main(void) {
    /* a collection at every moment the collector may run at: the collector's own test */
    setenv("AMB_GC_STRESS", "1", 1);
    static const char *const pages[] = {
        "tests/pages/interop.html", "tests/pages/interop2.html", "tests/pages/lists.html", "tests/pages/list.html",
        "tests/pages/custom.html", "tests/pages/shadow.html", "tests/pages/observer.html", "tests/pages/observer-unsettled.html",
        "tests/pages/geometry.html", "tests/pages/thrash.html", "tests/pages/inserted.html", "tests/pages/inserted-order.html",
        "tests/pages/write-async.html", "tests/pages/write-dom.html", "tests/pages/foreign.html", "tests/pages/storage-keep.html",
        "tests/pages/gc-host.html" };
    int bad = 0;
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) if (!run_page(pages[i], 0, 0)) bad++;
    if (!run_page("tests/pages/events.html", 0, 9)) bad++;
    if (!run_page("tests/pages/keys.html", 9, 15)) bad++;
    if (!run_page("tests/pages/counter.html", 24, 2)) bad++;
    printf("%d page(s) whose console is not as it should be\n", bad);
    return bad ? 1 : 0;
}
