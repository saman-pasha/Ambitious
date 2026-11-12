/* tests/exec/frames.c -- a page's frames (Phase 12u) through the whole pipeline, under
   valgrind: each frame fixture page (tests/pages/frames*.html, read from its file) fetched,
   parsed, its parts fetched, its parse's scripts run, laid out for its frames' boxes, its
   frames made, loaded and run (src/shell/frame.c, src/script/frames.c), its load and every
   frame's timers run as one event loop, then styled and laid out at rest with its frames, its
   frames drawn into the paint registry and the page drawn. For each page: its frames, each
   with its parent, its state, its viewport and its scroll bars; the console of every frame in
   the page's order; each frame's boxes as `ambitious frames` prints them; the drawing's
   checksum. The exec gate runs it under valgrind too: no invalid access, no leak. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(a11y);
amb_interface(script);
#include "../../src/paint/paint.h"
#include "../../src/css/css.h"
#include "../../src/js/js.h"
#include "../../src/script/script.h"
#include "../../src/html/encoding.h"
#include "../../src/shell/frame.h"

/* the virtual time the timers run for at the page's rest, as the command's */
#define HORIZON 10000.0

static int run_page(const char *path) {
    char url[4200], abs[4096];
    if (!realpath(path, abs)) { printf("%s: no such file\n", path); return 0; }
    snprintf(url, sizeof url, "file://%s", abs);
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    net_loader nt;
    script_runtime sr;
    js_vm vm;
    struct js_host hs;
    SCRIPT_HOST(hs, vm, d, nt, sr);
    defer(vm) { JS_VM_DROP(vm); }
    defer(hs) { SCRIPT_HOST_DROP(hs); }
    FRAME_STORES(fs, vm, hs);
    hs.fr = &fs.tab;
    dom_init(&d, 1);
    html_init(&p, 1);
    css_init(&st, 1);
    layout_init(&lt, 1);
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
    net_fetch_subresources(&nt, &d, ph, 0);
    hs.page = ph.index;
    css_set_env(800, 600, 16, 0, 0);
    script_run_parse(&vm);
    {
        css_style pst;
        layout_tree plt;
        css_init(&pst, 1);
        layout_init(&plt, 1);
        css_set_env(800, 600, 16, 0, 0);
        css_cascade(&pst, &d, &nt);
        layout_run(&plt, &d, &pst, &nt, 800, 600);
        amb_frames_load(&fs, &d, &plt, &nt, ph.index, 16, 0, 0, 0, 1);
    }
    script_run_load(&vm, -1);
    script_frames_run(&fs.tab, HORIZON);
    for (uint32_t k = 0; k < fs.tab.n; k++) script_settle(&fs.vm[k]);
    script_settle(&vm);
    css_set_env(800, 600, 16, 0, 0);
    css_cascade(&st, &d, &nt);
    layout_run(&lt, &d, &st, &nt, 800, 600);
    amb_frames_layout(&fs, &lt);
    amb_frames_record(&fs, &nt);
    amb_frames_paint(&fs, &st);
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    amb_paint(&lt, &nt, &c);
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    printf("%s: %u frame(s)\n", name, fs.tab.n);
    for (uint32_t r = 1; r < nt.frame_len; r++) {
        if (!nt.frame[r].live) continue;
        amb_span u = nt.frame[r].url;
        const char *shown = text_ptr(&nt, u);
        size_t sl = u.len;
        /* a file's URL by its name alone */
        if (sl > 7 && memcmp(shown, "file://", 7) == 0) {
            const char *slash = shown;
            for (size_t q = 0; q < sl; q++) if (shown[q] == '/') slash = shown + q + 1;
            sl -= (size_t)(slash - shown);
            shown = slash;
        }
        printf("  frame %u of %u: %.*s %s %s, viewport %gx%g%s%s%s\n", nt.frame[r].doc, nt.frame[r].parent, (int)sl, shown,
               net_frame_source_name(nt.frame[r].source), net_frame_outcome_name(nt.frame[r].outcome), nt.frame[r].width, nt.frame[r].height,
               nt.frame[r].vbar ? ", a vertical bar" : "", nt.frame[r].hbar ? ", a horizontal bar" : "", nt.frame[r].sandboxed ? ", sandboxed" : "");
    }
    /* the console of every frame, in the page's order */
    for (uint32_t next = 1; next <= fs.tab.seq; next++)
        for (uint32_t k = 0; k <= fs.tab.n; k++) {
            const script_runtime *rt = k ? &fs.sr[k - 1] : &sr;
            const js_vm *v = k ? &fs.vm[k - 1] : &vm;
            for (uint32_t q = 1; q < rt->console_len; q++)
                if (rt->console[q].live && script_console_order(v, q) == next)
                    printf("  console [%u] %.*s\n", k, (int)rt->console[q].text.len, text_ptr(rt, rt->console[q].text));
        }
    amb_frames_print_boxes(&fs, &d, stdout);
    printf("  drawn %ux%u, pixels fnv1a64:%016llx\n", c.w, c.h, (unsigned long long)amb_canvas_hash(&c));
    return 1;
}

int main(void) {
    static const char *const pages[] = { "tests/pages/frames.html", "tests/pages/frames-scroll.html", "tests/pages/frames-script.html",
                                         "tests/pages/frames-deep.html" };
    int ok = 0;
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) ok += run_page(pages[i]);
    printf("%d of %zu pages run with their frames\n", ok, sizeof pages / sizeof pages[0]);
    return 0;
}
