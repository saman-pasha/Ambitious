/* src/shell/main.c -- ambitious, the browser's command.
 *
 *   ambitious loop                the DOM vertical slice end to end: a document built,
 *                                 its snapshot reasoned over, the reasoner's requests
 *                                 validated and applied, the change seen in a second
 *                                 snapshot, an old batch refused, and the reasoner's
 *                                 answers checked against the C side's own traversal
 *   ambitious snapshot FILE       the demo document's snapshot, written to FILE
 *   ambitious load PAGE           a page fetched with what it asks for: each resource
 *   ambitious parse PAGE          an HTML file parsed: its tree, its parse errors
 *   ambitious style PAGE          the page's elements with their computed styles
 *   ambitious render PAGE PNG     the page laid out in an 800x600 viewport and drawn
 *   ambitious js FILE             a script run by the JavaScript engine with no page
 *   ambitious script PAGE         what the page's scripts did: each script and how it ended,
 *                                 its console and errors, the listeners and timers at rest
 *   ambitious click PAGE SELECTOR the same after a click at the first element SELECTOR matches
 *   ambitious --click SELECTOR CMD PAGE ...   any page command after such a click (repeatable)
 *   ambitious bench PAGE [N]      each stage's time, over N runs, and the snapshot's
 *                                 size and the reasoner's time over it
 *   ambitious inspect PAGE [--query GOAL | --why GOAL]
 *                                 the page as a developer sees it: what it loaded, its
 *                                 style and layout, its accessibility tree, and every
 *                                 module's diagnostics; and the reasoner asked
 *   ambitious query PAGE GOAL     the reasoner's answers over the page's facts
 *   ambitious why PAGE GOAL       the reasoner's proofs
 *   ambitious advise PAGE [--apply]  the reasoner's advice, applied as one batch
 *   ambitious history | bookmarks | bookmark URL [TITLE] | forget URL | forget --site SITE
 *           | forget --since TIME | permit SITE KIND allow|deny | retain DAYS | profile GOAL
 *                                 the profile: what is kept between runs
 *
 * The profile (prolog/amb/profile.pl) is a cocolog store a keeper process holds:
 * --profile DIR (or AMB_PROFILE) keeps it in DIR, in ZiguratIP's store, from one
 * run to the next; --private keeps it in memory for the run alone. Without
 * either, nothing is kept. A page loaded is a visit; the page's site's
 * permissions say which kinds of resource it may not load. AMB_NOW fixes the
 * clock, for tests.
 *
 * A PAGE is a URL (file:, data:, http:) or a file's path. Its snapshot has six
 * sections: the document, its parse, its style (the cascade's sheets, rules,
 * matches and computed values), its layout (boxes, lines and fragments, with how
 * each box's width was found), what it loaded (resources, their origins,
 * redirects and outcomes) and its accessibility tree (roles, names and where the
 * names came from, ID references).
 *
 * The reasoner is cocolog over prolog/amb/reason.pl, a process of its own: the
 * engine hands it a snapshot file and reads back text. AMB_ROOT and AMB_COCOLOG
 * (toolchain/env.sh, or bin/ambitious) say where they are; AMB_WORK names the
 * directory for the session's files (default $TMPDIR/ambitious-PID). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "../net/url.h"
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(a11y);
amb_interface(script);

#include "../paint/paint.h"
#include "../js/js.h"
#include "../script/script.h"

/* the viewport a page is laid out in */
#define VIEW_W 800.0
#define VIEW_H 600.0

#define S(lit) lit, sizeof(lit) - 1

/* where the session's files go and how the reasoner is run: paths, no pointers */
typedef struct session {
    char dir[512];
    char cocolog[1024];
    char reasoner[1024];
    char keeper[1024];
    char profile[1024];        /* the profile's store directory; empty: none (or a private one) */
    int private_mode;
} session;

static int session_open(session *s) {
    const char *root = getenv("AMB_ROOT");
    const char *coco = getenv("AMB_COCOLOG");
    const char *work = getenv("AMB_WORK");
    const char *tmp = getenv("TMPDIR");
    if (!root || !coco) {
        fprintf(stderr, "ambitious: AMB_ROOT and AMB_COCOLOG are not set (run bin/ambitious, or source toolchain/env.sh)\n");
        return 0;
    }
    snprintf(s->cocolog, sizeof s->cocolog, "%s", coco);
    snprintf(s->reasoner, sizeof s->reasoner, "%s/prolog/amb/reason.pl", root);
    snprintf(s->keeper, sizeof s->keeper, "%s/prolog/amb/profile_main.pl", root);
    const char *prof = getenv("AMB_PROFILE"), *priv = getenv("AMB_PRIVATE");
    s->private_mode = priv && priv[0] == '1';
    snprintf(s->profile, sizeof s->profile, "%s", prof && !s->private_mode ? prof : "");
    if (work) snprintf(s->dir, sizeof s->dir, "%s", work);
    else snprintf(s->dir, sizeof s->dir, "%s/ambitious-%ld", tmp ? tmp : "/tmp", (long)getpid());
    mkdir(s->dir, 0700);
    return 1;
}

/* a file of the session: dir/name */
static void session_path(const session *s, const char *name, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", s->dir, name);
}

/* the reasoner over its command and arguments, its answer into the file out.
   The argument vector holds this function's own copies: a parameter is the
   caller's borrow, which the safe part lets no array keep */
static int reason(const session *s, const char *cmd, const char *a1, const char *a2, const char *out) {
    char err[700], prog[1024], script[1024], c[64], x1[1024], x2[4096];
    session_path(s, "reason.err", err, sizeof err);
    snprintf(prog, sizeof prog, "%s", s->cocolog);
    snprintf(script, sizeof script, "%s", s->reasoner);
    snprintf(c, sizeof c, "%s", cmd);
    snprintf(x1, sizeof x1, "%s", a1);
    snprintf(x2, sizeof x2, "%s", a2 ? a2 : "");
    const char *argv[] = { prog, "-s", script, "--", c, x1, x2, 0 };
    if (!a2) argv[6] = 0;
    int st = amb_run(argv, out, err);
    if (st != 0) fprintf(stderr, "ambitious: the reasoner answered %d to %s (its errors: %s)\n", st, cmd, err);
    return st;
}

/* the profile keeper over a command and up to three arguments, its answer into the
   file out: the profile's store, or memory for a private session; -1 when there is
   no profile */
static int keep(const session *s, const char *cmd, const char *a1, const char *a2, const char *a3, const char *out) {
    if (!s->private_mode && !s->profile[0]) return -1;
    char err[700], prog[1024], store[1100], script[1024], c[64], x1[4096], x2[4096], x3[1024];
    session_path(s, "profile.err", err, sizeof err);
    snprintf(prog, sizeof prog, "%s", s->cocolog);
    snprintf(store, sizeof store, "%s", s->private_mode ? "--local" : s->profile);
    snprintf(script, sizeof script, "%s", s->keeper);
    snprintf(c, sizeof c, "%s", cmd);
    snprintf(x1, sizeof x1, "%s", a1 ? a1 : "");
    snprintf(x2, sizeof x2, "%s", a2 ? a2 : "");
    snprintf(x3, sizeof x3, "%s", a3 ? a3 : "");
    const char *argv[12];
    int k = 0;
    argv[k++] = prog;
    if (s->private_mode) argv[k++] = "--local";
    else { argv[k++] = "--embed"; argv[k++] = store; }
    argv[k++] = "-s";
    argv[k++] = script;
    argv[k++] = "--";
    argv[k++] = c;
    if (a1) argv[k++] = x1;
    if (a1 && a2) argv[k++] = x2;
    if (a1 && a2 && a3) argv[k++] = x3;
    argv[k] = 0;
    int st = amb_run(argv, out, err);
    if (st != 0) {
        own FILE *f = fopen(err, "rb");
        if (f) {
            defer(f) { fclose(f); }
            char buf[512];
            size_t k = fread(buf, 1, sizeof buf, f);
            fprintf(stderr, "%.*s", (int)k, buf);
        }
    }
    return st;
}

static int read_all(const char *path, vector<char> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static void print_file(const char *path, const char *prefix) {
    vector<char> text;
    if (!read_all(path, &text)) return;
    size_t start = 0;
    for (size_t i = 0; i < len(text); i++) {
        if (at(text, i) == '\n') {
            printf("%s%.*s\n", prefix, (int)(i - start), data(text) + start);
            start = i + 1;
        }
    }
}

static int write_snapshot(const dom_document *d, const char *path) {
    own FILE *f = fopen(path, "w");
    if (!f) return 0;
    defer(f) { fclose(f); }
    dom_export(d, f);
    return 1;
}

/* each amb_request line of the advice file through the dispatcher, as one batch:
   the batch is the document's revision when it begins */
static void apply_batch(dom_document *d, const char *advice, int *applied, int *refused) {
    vector<char> text;
    *applied = 0;
    *refused = 0;
    if (!read_all(advice, &text)) return;
    uint64_t batch = dom_revision(d);
    size_t start = 0;
    for (size_t i = 0; i < len(text); i++) {
        if (at(text, i) != '\n') continue;
        size_t n = i - start;
        if (n > 12 && memcmp(data(text) + start, "amb_request(", 12) == 0) {
            char line[8192];
            if (n >= sizeof line) { (*refused)++; start = i + 1; continue; }
            memcpy(line, data(text) + start, n);
            line[n] = 0;
            dom_status st = dom_dispatch(d, line, n, batch, stdout);
            if (st == DOM_OK) (*applied)++; else (*refused)++;
        }
        start = i + 1;
    }
}

/* the demo document: what the DOM's rules have something to say about */
static void build_demo(dom_document *d) {
    dom_node_h root = dom_root(d);
    dom_node_h html = dom_create_element(d, S("html"));
    dom_node_h head = dom_create_element(d, S("head"));
    dom_node_h title = dom_create_element(d, S("title"));
    dom_node_h body = dom_create_element(d, S("body"));
    dom_node_h h1 = dom_create_element(d, S("h1"));
    dom_node_h logo = dom_create_element(d, S("img"));
    dom_node_h spacer = dom_create_element(d, S("img"));
    dom_node_h p = dom_create_element(d, S("p"));
    dom_node_h empty = dom_create_text(d, S(""));
    dom_node_h go = dom_create_element(d, S("button"));
    dom_node_h link = dom_create_element(d, S("a"));
    dom_append_child(d, root, dom_create_doctype(d, S("html"), S(""), S(""), 0));
    dom_append_child(d, root, html);
    dom_append_child(d, html, head);
    dom_append_child(d, head, title);
    dom_append_child(d, title, dom_create_text(d, S("Ambitious")));
    dom_append_child(d, html, body);
    dom_append_child(d, body, h1);
    dom_append_child(d, h1, dom_create_text(d, S("A reasoning browser")));
    dom_append_child(d, body, logo);
    dom_set_attribute(d, logo, S("src"), S("logo.png"));
    dom_set_attribute(d, logo, S("title"), S("The Ambitious logo"));
    dom_append_child(d, body, spacer);
    dom_set_attribute(d, spacer, S("src"), S("spacer.gif"));
    dom_append_child(d, body, p);
    dom_append_child(d, p, dom_create_text(d, S("Every node here is a fact.")));
    dom_append_child(d, p, empty);
    dom_append_child(d, body, go);
    dom_append_child(d, go, dom_create_text(d, S("Go")));
    dom_append_child(d, body, link);
    dom_set_attribute(d, link, S("href"), S("#top"));
}

/* ---- what the C side knows, to hold the reasoner's answers to ---- */

static size_t c_descendants(const dom_document *d, uint32_t n) {
    size_t c = 0;
    for (uint32_t i = 1; i < d->node_len; i++) {
        if (!d->node[i].live || i == n) continue;
        for (uint32_t x = d->node[i].parent; x; x = d->node[x].parent) if (x == n) { c++; break; }
    }
    return c;
}

/* the text of the subtree of n, in document order, into buf */
static size_t c_text(const dom_document *d, uint32_t n, char *buf, size_t cap) {
    size_t k = 0;
    uint32_t x = n;
    for (;;) {
        if (d->node[x].kind == DOM_NODE_KIND_TEXT) {
            amb_span sp = d->node[x].data;
            if (k + sp.len < cap) { memcpy(buf + k, text_ptr(d, sp), sp.len); k += sp.len; }
        }
        if (d->node[x].first_child) { x = d->node[x].first_child; continue; }
        while (x != n && !d->node[x].next_sibling) x = d->node[x].parent;
        if (x == n) break;
        x = d->node[x].next_sibling;
    }
    buf[k] = 0;
    return k;
}

/* criterion 7: the reasoner's answers, from the snapshot, against the C side's
   own traversal of the live document */
static int cross_check(const session *s, const dom_document *d, const char *snap) {
    char out[700];
    vector<char> text;
    int bad = 0, rows = 0;
    session_path(s, "cross.txt", out, sizeof out);
    if (reason(s, "query", snap, "dom_node(N, _), aggregate_all(count, dom_descendant(_, N), C)", out) != 0) return 1;
    if (!read_all(out, &text)) return 1;
    size_t start = 0;
    for (size_t i = 0; i < len(text); i++) {
        if (at(text, i) != '\n') continue;
        size_t p = start;
        dom_node_h h = { 0, 0, 0 };
        int64_t c = 0;
        const char *t = data(text);
        if (amb_lex_word(t, i, &p, "N") && amb_lex_char(t, i, &p, '=') && amb_lex_handle(t, i, &p, "n", &h.doc, &h.index, &h.gen) &&
            amb_lex_char(t, i, &p, ',') && amb_lex_word(t, i, &p, "C") && amb_lex_char(t, i, &p, '=') && amb_lex_int(t, i, &p, &c)) {
            rows++;
            if (!node_live(d, h) || (int64_t)c_descendants(d, h.index) != c) { bad++; printf("MISMATCH descendants of n(%u,%u,%u): reasoner %lld\n", h.doc, h.index, h.gen, (long long)c); }
        } else { bad++; printf("MISMATCH unreadable answer: %.*s\n", (int)(i - start), t + start); }
        start = i + 1;
    }
    printf("cross-check: descendants of %d nodes, the reasoner's and the C traversal's: %s\n", rows, bad ? "DIFFER" : "agree");
    vector<char> text2;
    int rows2 = 0, bad2 = 0;
    if (reason(s, "query", snap, "dom_element(N, _), dom_text_content(N, T)", out) != 0) return 1;
    if (!read_all(out, &text2)) return 1;
    start = 0;
    for (size_t i = 0; i < len(text2); i++) {
        if (at(text2, i) != '\n') continue;
        size_t p = start;
        dom_node_h h = { 0, 0, 0 };
        char want[4096], got[4096];
        size_t wl = 0;
        const char *t = data(text2);
        if (amb_lex_word(t, i, &p, "N") && amb_lex_char(t, i, &p, '=') && amb_lex_handle(t, i, &p, "n", &h.doc, &h.index, &h.gen) &&
            amb_lex_char(t, i, &p, ',') && amb_lex_word(t, i, &p, "T") && amb_lex_char(t, i, &p, '=') && amb_lex_atom(t, i, &p, want, sizeof want, &wl)) {
            rows2++;
            size_t gl = node_live(d, h) ? c_text(d, h.index, got, sizeof got) : 0;
            if (gl != wl || memcmp(got, want, wl) != 0) { bad2++; printf("MISMATCH text of n(%u,%u,%u)\n", h.doc, h.index, h.gen); }
        } else { bad2++; printf("MISMATCH unreadable answer: %.*s\n", (int)(i - start), t + start); }
        start = i + 1;
    }
    printf("cross-check: text content of %d elements, the reasoner's and the C traversal's: %s\n", rows2, bad2 ? "DIFFER" : "agree");
    return bad + bad2;
}

static int cmd_loop(void) {
    session s;
    if (!session_open(&s)) return 2;
    char snap1[700], snap2[700], advice[700], diff[700], out[700];
    session_path(&s, "snap1.pl", snap1, sizeof snap1);
    session_path(&s, "snap2.pl", snap2, sizeof snap2);
    session_path(&s, "advice.pl", advice, sizeof advice);
    session_path(&s, "diff.pl", diff, sizeof diff);
    session_path(&s, "out.txt", out, sizeof out);
    int failures = 0;

    dom_document d;
    dom_init(&d, 1);
    build_demo(&d);
    printf("1. the document, at revision %llu, %zu nodes:\n   ", (unsigned long long)dom_revision(&d), node_count(d));
    dom_serialize(&d, dom_root(&d), stdout);
    printf("\n   invariants: %d broken\n", dom_check_invariants(&d, stdout));

    if (!write_snapshot(&d, snap1)) return 2;
    if (reason(&s, "check", snap1, 0, out) != 0) return 2;
    printf("2. the snapshot, checked by the reasoner against the schema:\n");
    print_file(out, "   ");
    failures += cross_check(&s, &d, snap1);

    if (reason(&s, "advise", snap1, 0, advice) != 0) return 2;
    printf("3. the reasoner's advice, from the snapshot alone:\n");
    print_file(advice, "   ");

    int applied = 0, refused = 0;
    printf("4. the requests, validated and applied by the engine as one batch:\n");
    apply_batch(&d, advice, &applied, &refused);
    printf("   %d applied, %d refused; the document is at revision %llu, invariants: %d broken\n   ",
           applied, refused, (unsigned long long)dom_revision(&d), dom_check_invariants(&d, stdout));
    dom_serialize(&d, dom_root(&d), stdout);
    printf("\n");
    if (applied == 0) failures++;

    if (!write_snapshot(&d, snap2)) return 2;
    if (reason(&s, "diff", snap1, snap2, diff) != 0) return 2;
    printf("5. what changed, the reasoner's diff of the two snapshots:\n");
    print_file(diff, "   ");
    if (reason(&s, "query", snap2, "dom_missing_attribute(N, alt)", out) != 0) return 2;
    printf("   images still without alt, asked of the new snapshot: ");
    print_file(out, "");
    failures += cross_check(&s, &d, snap2);

    printf("6. the first batch again, against the document that moved on:\n");
    apply_batch(&d, advice, &applied, &refused);
    printf("   %d applied, %d refused (the batch was made at an old revision)\n", applied, refused);
    if (applied != 0) failures++;

    printf("%s: the loop, %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}

static int cmd_snapshot(const char *path) {
    dom_document d;
    dom_init(&d, 1);
    build_demo(&d);
    return write_snapshot(&d, path) ? 0 : 2;
}

/* ---- a page: fetched, parsed, its resources loaded, styled and laid out ---- */

/* a page's stores, declared in the caller's scope, where each is dropped; and its
   scripts' engine, its host pointed at the stores there (src/script/script.h) */
#define PAGE_STORES dom_document d; html_parser p; css_style st; layout_tree lt; net_loader nt; a11y_tree ax; \
    script_runtime sr; js_vm vm; struct js_host hs; SCRIPT_HOST(hs, vm, d, nt, sr); defer(vm) { JS_VM_DROP(vm); }
#define PAGE_ARGS &d, &p, &st, &lt, &nt, &ax, &sr, &vm
/* the virtual time a page's timers run for before it is taken to be at rest, ms */
#define SCRIPT_HORIZON 10000.0
/* a page command's session: its files, the reasoner, the profile */
#define PAGE_SESSION session s; if (!session_open(&s)) return 2

/* the URL of a page named by a URL or by a file's path */
static void page_url(const char *page, char *out, size_t cap) {
    size_t i = 0;
    while ((page[i] >= 'a' && page[i] <= 'z') || (page[i] >= 'A' && page[i] <= 'Z')) i++;
    if (i >= 2 && page[i] == ':') { snprintf(out, cap, "%s", page); return; }
    char abs[4096];
    if (realpath(page, abs)) snprintf(out, cap, "file://%s", abs);
    else snprintf(out, cap, "file://%s", page);
}

/* the clock: AMB_NOW, or the time */
static long long now_seconds(void) {
    const char *n = getenv("AMB_NOW");
    return n ? atoll(n) : (long long)time(0);
}

/* a page's site, as the profile keys permissions: its origin, or for an opaque
   one its scheme */
static void site_of(const char *url, char *out, size_t cap) {
    if (net_url_origin(url, out, cap) && strcmp(out, "null") != 0) return;
    size_t i = 0;
    while (url[i] && url[i] != ':' && i + 1 < cap) { out[i] = url[i]; i++; }
    out[i] = 0;
}

/* the document's title: its first title element's text, white space collapsed */
static void page_title(const dom_document *d, char *out, size_t cap) {
    size_t k = 0;
    out[0] = 0;
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT || !names_is(d, d->node[x].name, "title", 5)) continue;
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
            if (d->node[c].kind != DOM_NODE_KIND_TEXT) continue;
            amb_span sp = d->node[c].data;
            const char *t = text_ptr(d, sp);
            for (size_t i = 0; i < sp.len && k + 1 < cap; i++) {
                int white = t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r';
                if (white) { if (k && out[k - 1] != ' ') out[k++] = ' '; }
                else out[k++] = t[i];
            }
        }
        break;
    }
    while (k && out[k - 1] == ' ') k--;
    out[k] = 0;
}

/* the page's record in the loader, or 0 when it could not be fetched. With a
   profile: its site's denied kinds asked for first, the visit recorded after */
/* the clicks asked for (--click SELECTOR, AMB_CLICKS a line each), each at the first
   element its selector matches, after the page's scripts */
static void page_clicks(const dom_document *d, js_vm *vm) {
    const char *env = getenv("AMB_CLICKS");
    if (!env || !*env) return;
    vector<char> all;
    append_range(all, env, strlen(env));
    size_t i = 0;
    while (i < len(all)) {
        size_t e = i;
        while (e < len(all) && all[e] != '\n') e++;
        uint32_t x = e > i ? script_select(d, data(all) + i, e - i) : 0;
        if (!x) fprintf(stderr, "ambitious: --click %.*s: no element matches\n", (int)(e - i), data(all) + i);
        else script_click(vm, x, SCRIPT_HORIZON);
        i = e + 1;
    }
}

static uint32_t load_page(const session *s, const char *page, dom_document *d, html_parser *p, css_style *st, layout_tree *lt, net_loader *nt,
                          a11y_tree *ax, script_runtime *sr, js_vm *vm) {
    char url[4200];
    page_url(page, url, sizeof url);
    dom_init(d, 1);
    html_init(p, 1);
    css_init(st, 1);
    layout_init(lt, 1);
    net_init(nt, 1);
    a11y_init(ax, 1);
    script_init(sr, 1);
    net_resource_h ph = net_fetch(nt, url, strlen(url), NET_KIND_DOCUMENT);
    uint32_t pr = ph.index;
    if (nt->resource[pr].outcome != NET_OUTCOME_OK) {
        fprintf(stderr, "ambitious: cannot load %s: %s (status %u)\n", url, net_outcome_name(nt->resource[pr].outcome), nt->resource[pr].status);
        return 0;
    }
    vector<char> src;
    append_range(src, bytes_ptr(nt, nt->resource[pr].body), nt->resource[pr].body.len);
    html_parse(p, d, data(src), len(src));
    char final[4200], site[512], answer[700];
    amb_span fu = nt->resource[pr].final_url;
    snprintf(final, sizeof final, "%.*s", (int)fu.len, text_ptr(nt, fu));
    site_of(final, site, sizeof site);
    session_path(s, "profile.out", answer, sizeof answer);
    uint32_t denied = 0;
    if (keep(s, "denied", site, 0, 0, answer) == 0) {
        vector<char> kinds;
        if (read_all(answer, &kinds)) {
            push_back(kinds, 0);
            if (strstr(data(kinds), "stylesheet")) denied |= 1u << NET_KIND_STYLESHEET;
            if (strstr(data(kinds), "image")) denied |= 1u << NET_KIND_IMAGE;
            if (strstr(data(kinds), "script")) denied |= 1u << NET_KIND_SCRIPT;
        }
    }
    net_fetch_subresources(nt, d, ph, denied);
    /* the scripts (unless the site's are denied), the clicks asked for, then the page at rest */
    vm->host->page = pr;
    if (!(denied & (1u << NET_KIND_SCRIPT))) {
        script_run_page(vm, SCRIPT_HORIZON);
        page_clicks(d, vm);
    }
    script_settle(vm);
    css_cascade(st, d, nt);
    layout_run(lt, d, st, nt, VIEW_W, VIEW_H);
    a11y_build(ax, d, st);
    char title[512], when[32];
    page_title(d, title, sizeof title);
    snprintf(when, sizeof when, "%lld", now_seconds());
    keep(s, "visit", final, title, when, answer);
    return pr;
}

/* the page's snapshot: the document's section, the parse's, the style's, the layout's, the loader's */
static int write_page_snapshot(const dom_document *d, const html_parser *p, const css_style *st, const layout_tree *lt, const net_loader *nt,
                               const a11y_tree *ax, const script_runtime *sr, const char *path) {
    own FILE *f = fopen(path, "w");
    if (!f) return 0;
    defer(f) { fclose(f); }
    dom_export(d, f);
    html_export(p, f);
    css_export(st, f);
    layout_export(lt, f);
    net_export(nt, f);
    a11y_export(ax, f);
    script_export(sr, f);
    return 1;
}

/* what the page loaded: each resource, how its fetch ended, its redirects */
static int cmd_load(const char *page) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    for (uint32_t r = 1; r < nt.resource_len; r++) {
        if (!nt.resource[r].live) continue;
        amb_span u = nt.resource[r].url, ty = nt.resource[r].type;
        printf("%-10s %-18s %3u %7u  %.*s  %.*s", net_kind_name(nt.resource[r].kind), net_outcome_name(nt.resource[r].outcome),
               nt.resource[r].status, nt.resource[r].size, (int)u.len, text_ptr(&nt, u), (int)ty.len, text_ptr(&nt, ty));
        if (nt.resource[r].kind == NET_KIND_IMAGE && nt.resource[r].width) printf("  %ux%u", nt.resource[r].width, nt.resource[r].height);
        printf("\n");
        for (uint32_t h = 1; h < nt.hop_len; h++)
            if (nt.hop[h].live && nt.hop[h].resource == r) {
                amb_span hu = nt.hop[h].url;
                printf("           redirected (%u) to %.*s\n", nt.hop[h].status, (int)hu.len, text_ptr(&nt, hu));
            }
    }
    printf("%zu resources\n", resource_count(nt));
    return 0;
}

static int cmd_parse(const char *page) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    dom_serialize(&d, dom_root(&d), stdout);
    printf("\n");
    printf("%zu nodes, %zu parse errors, invariants: %d broken\n", node_count(d), error_count(p), dom_check_invariants(&d, stdout));
    return 0;
}

/* the computed values of the properties a reader looks at first, of each element,
   indented by its depth */
static int cmd_style(const char *page) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    static const css_prop shown[] = { CSS_PROP_DISPLAY, CSS_PROP_COLOR, CSS_PROP_BACKGROUND_COLOR, CSS_PROP_FONT_SIZE,
                                      CSS_PROP_FONT_WEIGHT, CSS_PROP_MARGIN_TOP, CSS_PROP_PADDING_LEFT };
    static const char *const names[] = { "display", "color", "background", "font-size", "weight", "margin-top", "padding-left" };
    for (uint32_t es = 1; es < st.estyle_len; es++) {
        uint32_t x = st.estyle[es].node.index;
        uint32_t c = st.estyle[es].first;                          /* the element's values: its style's run */
        int depth = 0;
        for (uint32_t y = d.node[x].parent; y > 1; y = d.node[y].parent) depth++;
        printf("%*s<%s>", 2 * depth, "", names_ptr(&d, d.node[x].name));
        for (size_t k = 0; k < sizeof shown / sizeof shown[0]; k++)
            for (uint32_t e = c; e < st.cval_len && st.cval[e].style == st.cval[c].style; e++)
                if (st.cval[e].prop == shown[k]) {
                    amb_span t = st.cval[e].text;
                    printf(" %s: %.*s%s", names[k], (int)t.len, text_ptr(&st, t), st.cval[e].how == CSS_HOW_DECLARED ? "" : st.cval[e].how == CSS_HOW_INHERITED ? " (inherited)" : " (initial)");
                }
        printf("\n");
    }
    printf("%zu rules, %zu declarations, %zu matches, %zu computed values (%zu styles)\n", rule_count(st), decl_count(st), match_count(st),
           (size_t)estyle_count(st) * 29, (size_t)cval_count(st) / 29);
    return 0;
}

/* the page drawn into a PNG: its size, what it holds, and its pixels' checksum */
static int cmd_render(const char *page, const char *png) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    amb_paint(&lt, &nt, &c);
    own FILE *f = fopen(png, "wb");
    if (!f) { fprintf(stderr, "ambitious: cannot write %s\n", png); return 2; }
    defer(f) { fclose(f); }
    if (!amb_png_write(&c, f)) { fprintf(stderr, "ambitious: cannot write %s\n", png); return 2; }
    size_t texts = 0;
    for (uint32_t i = 1; i < lt.frag_len; i++) if (lt.frag[i].live && lt.frag[i].kind == LAYOUT_FRAG_KIND_TEXT) texts++;
    printf("rendered %ux%u: %zu boxes, %zu lines, %zu text fragments; pixels fnv1a64:%016llx\n", c.w, c.h, box_count(lt), line_count(lt),
           texts, (unsigned long long)amb_canvas_hash(&c));
    return 0;
}

/* ---- measures ---- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* each stage of a page's load, timed over n runs; then its snapshot and the reasoner */
static int cmd_bench(const char *page, int n) {
    PAGE_SESSION;
    char url[4200];
    page_url(page, url, sizeof url);
    double t[12];
    memset(t, 0, sizeof t);
    static const char *const stage[12] = { "fetch the page", "parse", "fetch its resources", "scripts", "click", "cascade", "layout",
                                           "accessibility tree", "paint", "paint the viewport", "encode the PNG", "export the snapshot" };
    size_t nodes = 0, boxes = 0, bytes = 0, snap_bytes = 0;
    char snap[700];
    session_path(&s, "bench.pl", snap, sizeof snap);
    for (int r = 0; r < n; r++) {
        PAGE_STORES;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        layout_init(&lt, 1);
        net_init(&nt, 1);
        a11y_init(&ax, 1);
        script_init(&sr, 1);
        double a = now_ms();
        net_resource_h ph = net_fetch(&nt, url, strlen(url), NET_KIND_DOCUMENT);
        if (nt.resource[ph.index].outcome != NET_OUTCOME_OK) { fprintf(stderr, "ambitious: cannot load %s\n", url); return 2; }
        double b = now_ms();
        vector<char> src;
        append_range(src, bytes_ptr(&nt, nt.resource[ph.index].body), nt.resource[ph.index].body.len);
        html_parse(&p, &d, data(src), len(src));
        double c = now_ms();
        net_fetch_subresources(&nt, &d, ph, 0);
        double e = now_ms();
        vm.host->page = ph.index;
        script_run_page(&vm, SCRIPT_HORIZON);
        double e1 = now_ms();
        page_clicks(&d, &vm);
        double e2 = now_ms();
        script_settle(&vm);
        double e3 = now_ms();
        css_cascade(&st, &d, &nt);
        double f = now_ms();
        layout_run(&lt, &d, &st, &nt, VIEW_W, VIEW_H);
        double g = now_ms();
        a11y_build(&ax, &d, &st);
        double h = now_ms();
        amb_canvas cv = {0};
        defer(cv) { AMB_CANVAS_DROP(cv); }
        amb_paint(&lt, &nt, &cv);
        double i = now_ms();
        amb_canvas vw = {0};
        defer(vw) { AMB_CANVAS_DROP(vw); }
        amb_paint_view(&lt, &nt, &vw, 0, VIEW_H);
        double i2 = now_ms();
        own FILE *nul = fopen("/dev/null", "wb");
        if (!nul) return 2;
        defer(nul) { fclose(nul); }
        amb_png_write(&cv, nul);
        double j = now_ms();
        write_page_snapshot(&d, &p, &st, &lt, &nt, &ax, &sr, snap);
        double k = now_ms();
        double dt[12] = { b - a, c - b, e - c, (e1 - e) + (e3 - e2), e2 - e1, f - e3, g - f, h - g, i - h, i2 - i, j - i2, k - j };
        for (int q = 0; q < 12; q++) t[q] += dt[q];
        nodes = node_count(d);
        boxes = box_count(lt);
        bytes = len(src);
    }
    struct stat sb;
    if (stat(snap, &sb) == 0) snap_bytes = (size_t)sb.st_size;
    printf("%s: %zu bytes, %zu nodes, %zu boxes; mean of %d run(s)\n", url, bytes, nodes, boxes, n);
    double total = 0;
    /* the engine's total: every stage but the viewport's paint, which repeats the page's */
    for (int q = 0; q < 12; q++) { if (q != 9) total += t[q] / n; printf("  %-22s %9.2f ms\n", stage[q], t[q] / n); }
    printf("  %-22s %9.2f ms\n", "the engine, in all", total);
    /* the reasoner, a process of its own: loading and checking the snapshot, then advising */
    char out[700];
    session_path(&s, "bench.txt", out, sizeof out);
    double a = now_ms();
    reason(&s, "check", snap, 0, out);
    double b = now_ms();
    reason(&s, "advise", snap, 0, out);
    double c = now_ms();
    vector<char> facts;
    size_t lines = 0;
    if (read_all(snap, &facts)) for (size_t q = 0; q < len(facts); q++) if (at(facts, q) == '\n') lines++;
    printf("  the snapshot: %zu bytes, %zu lines\n", snap_bytes, lines);
    printf("  %-22s %9.2f ms\n", "reasoner: check", b - a);
    printf("  %-22s %9.2f ms\n", "reasoner: advise", c - b);
    return 0;
}

/* the page as a developer sees it, and the reasoner's diagnostics of it */
static int cmd_inspect(const char *page, const char *mode, const char *goal) {
    PAGE_SESSION;
    PAGE_STORES;
    uint32_t pr = load_page(&s, page, PAGE_ARGS);
    if (!pr) return 2;
    amb_span pu = nt.resource[pr].final_url;
    printf("== %.*s\n", (int)pu.len, text_ptr(&nt, pu));
    printf("%zu nodes, %zu parse errors; %zu resources; %zu rules, %zu declarations; %zu boxes, %zu lines\n", node_count(d), error_count(p),
           resource_count(nt), rule_count(st), decl_count(st), box_count(lt), line_count(lt));
    printf("== resources\n");
    for (uint32_t r = 1; r < nt.resource_len; r++) {
        if (!nt.resource[r].live) continue;
        amb_span u = nt.resource[r].url;
        printf("  %-10s %-18s %.*s\n", net_kind_name(nt.resource[r].kind), net_outcome_name(nt.resource[r].outcome), (int)u.len, text_ptr(&nt, u));
    }
    printf("== accessibility tree\n");
    for (uint32_t a = 1; a < ax.anode_len; a++) {
        if (!ax.anode[a].live) continue;
        int depth = 0;
        for (uint32_t q = ax.anode[a].parent; q; q = ax.anode[q].parent) depth++;
        amb_span nm = ax.anode[a].name;
        printf("  %*s%s", 2 * depth, "", a11y_role_name(ax.anode[a].role));
        if (ax.anode[a].role == A11Y_ROLE_HEADING) printf(" %u", ax.anode[a].level);
        if (nm.len) printf(" \"%.*s\" (%s)", (int)nm.len, text_ptr(&ax, nm), a11y_name_from_name(ax.anode[a].name_from));
        if (ax.anode[a].focusable) printf(" focusable");
        printf("\n");
    }
    char snap[700], out[700];
    session_path(&s, "page.pl", snap, sizeof snap);
    session_path(&s, "answer.txt", out, sizeof out);
    if (!write_page_snapshot(&d, &p, &st, &lt, &nt, &ax, &sr, snap)) return 2;
    printf("== diagnostics\n");
    if (reason(&s, "advise", snap, 0, out) != 0) return 2;
    print_file(out, "  ");
    if (mode && goal) {
        printf("== %s %s\n", mode + 2, goal);
        if (reason(&s, strcmp(mode, "--why") == 0 ? "why" : "query", snap, goal, out) != 0) return 2;
        print_file(out, "  ");
    }
    return 0;
}

/* an element as a reader names it: <tag id="..."> or <tag class="..."> */
static void print_elem(const dom_document *d, dom_node_h h) {
    uint32_t x = h.index;
    if (!x || x >= d->node_len || !d->node[x].live || d->node[x].gen != h.gen) { printf("(a node gone)"); return; }
    if (d->node[x].kind == DOM_NODE_KIND_DOCUMENT) { printf("the document"); return; }
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) { printf("a %s node", dom_node_kind_name(d->node[x].kind)); return; }
    printf("<%s", names_ptr(d, d->node[x].name));
    static const char *const shown[] = { "id", "class", "type", "src" };
    for (size_t k = 0; k < 4; k++) {
        uint32_t a = names_find(d, shown[k], strlen(shown[k]));
        for (uint32_t at = d->node[x].first_attr; a && at; at = d->attr[at].next)
            if (d->attr[at].name == a) { amb_span v = d->attr[at].value; printf(" %s=\"%.*s\"", shown[k], (int)v.len, text_ptr(d, v)); break; }
    }
    printf(">");
}

/* what the page's scripts did: each script and how it ended, what it wrote to the
   console and threw, the listeners and the timers at rest */
static int cmd_script(const char *page) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    size_t runs = 0, cons = 0, errs = 0, lsn = 0, tms = 0;
    for (uint32_t r = 1; r < sr.run_len; r++) {
        if (!sr.run[r].live) continue;
        runs++;
        printf("script %-3u %-9s %-12s ", r, script_source_name(sr.run[r].source), script_outcome_name(sr.run[r].outcome));
        amb_span u = sr.run[r].url;
        if (u.len) printf("%.*s", (int)u.len, text_ptr(&sr, u));
        else print_elem(&d, sr.run[r].elem);
        printf("\n");
    }
    for (uint32_t c = 1; c < sr.console_len; c++) {
        if (!sr.console[c].live) continue;
        cons++;
        amb_span t = sr.console[c].text;
        printf("console %-5s [%u] %.*s\n", script_level_name(sr.console[c].level), sr.console[c].run, (int)t.len, text_ptr(&sr, t));
    }
    for (uint32_t e = 1; e < sr.error_len; e++) {
        if (!sr.error[e].live) continue;
        errs++;
        amb_span t = sr.error[e].message;
        printf("error   %-6s [%u] %.*s\n", script_phase_name(sr.error[e].phase), sr.error[e].run, (int)t.len, text_ptr(&sr, t));
    }
    for (uint32_t l = 1; l < sr.listener_len; l++) {
        if (!sr.listener[l].live) continue;
        lsn++;
        amb_span t = sr.listener[l].type;
        printf("listen  %-16.*s [%u] ", (int)t.len, text_ptr(&sr, t), sr.listener[l].run);
        if (sr.listener[l].on == SCRIPT_LISTEN_ON_WINDOW) printf("the window");
        else print_elem(&d, sr.listener[l].target);
        printf("%s\n", sr.listener[l].capture ? " (capture)" : "");
    }
    for (uint32_t t = 1; t < sr.timer_len; t++) {
        if (!sr.timer[t].live) continue;
        tms++;
        printf("timer   %u ms %s [%u], fired %u, %s\n", sr.timer[t].delay, sr.timer[t].repeats ? "interval" : "timeout", sr.timer[t].run,
               sr.timer[t].fired, script_timer_state_name(sr.timer[t].state));
    }
    printf("%zu scripts, %zu console messages, %zu errors, %zu listeners, %zu timers; %zu nodes\n", runs, cons, errs, lsn, tms, node_count(d));
    return 0;
}

/* query, why, advise: the reasoner over the page's snapshot */
static int cmd_reason(const char *what, const char *page, const char *goal, int apply) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    char snap[700], out[700];
    session_path(&s, "page.pl", snap, sizeof snap);
    session_path(&s, "answer.txt", out, sizeof out);
    if (!write_page_snapshot(&d, &p, &st, &lt, &nt, &ax, &sr, snap)) return 2;
    int rc = reason(&s, what, snap, goal, out);
    print_file(out, "");
    if (rc != 0) return rc;
    if (apply) {
        int applied = 0, refused = 0;
        apply_batch(&d, out, &applied, &refused);
        printf("%d applied, %d refused; revision %llu\n", applied, refused, (unsigned long long)dom_revision(&d));
        dom_serialize(&d, dom_root(&d), stdout);
        printf("\n");
    }
    return 0;
}

/* a profile command: the keeper's answer printed */
static int cmd_profile(const char *cmd, const char *a1, const char *a2, const char *a3) {
    session s;
    if (!session_open(&s)) return 2;
    char out[700];
    session_path(&s, "profile.out", out, sizeof out);
    int st = keep(&s, cmd, a1, a2, a3, out);
    if (st < 0) { fprintf(stderr, "ambitious: no profile (pass --profile DIR, or set AMB_PROFILE; --private for one in memory)\n"); return 2; }
    print_file(out, "");
    return st;
}


/* ---- JavaScript, with no page ---- */

/* a script run by the engine alone: its console on stdout, then its timers in virtual
   time; performance.now() reads the real clock, for the benchmark's suite */
static int cmd_js(const char *file) {
    if (strcmp(file, "--help") == 0) { printf("ambitious js FILE: a script run with no page\n"); return 0; }
    vector<char> src;
    if (!read_all(file, &src)) { fprintf(stderr, "ambitious: cannot read %s\n", file); return 2; }
    js_vm vm;
    js_init(&vm);
    defer(vm) { JS_VM_DROP(vm); }
    vm.echo = 1;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    vm.real_clock = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
    int rc = js_eval(&vm, data(src), len(src), file, 1);
    if (rc == 0) js_run_timers(&vm, 1e15, 1000000);
    fflush(stdout);
    if (rc != 0) { fprintf(stderr, "%.*s\n", (int)len(vm.out), data(vm.out)); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    /* the options before the command: the profile's store, or a private session */
    while (argc >= 2 && argv[1][0] == '-' && argv[1][1] == '-') {
        if (strcmp(argv[1], "--private") == 0) { setenv("AMB_PRIVATE", "1", 1); argv++; argc--; continue; }
        if (strcmp(argv[1], "--profile") == 0 && argc >= 3) { setenv("AMB_PROFILE", argv[2], 1); argv += 2; argc -= 2; continue; }
        if (strcmp(argv[1], "--click") == 0 && argc >= 3) {
            /* a click after the page's scripts, at the first element the selector matches */
            vector<char> all;
            const char *had = getenv("AMB_CLICKS");
            if (had && *had) { append_range(all, had, strlen(had)); push_back(all, '\n'); }
            append_range(all, argv[2], strlen(argv[2]));
            push_back(all, 0);
            setenv("AMB_CLICKS", data(all), 1);
            argv += 2;
            argc -= 2;
            continue;
        }
        break;
    }
    if (argc >= 2 && strcmp(argv[1], "history") == 0) return cmd_profile("history", 0, 0, 0);
    if (argc >= 2 && strcmp(argv[1], "bookmarks") == 0) return cmd_profile("bookmarks", 0, 0, 0);
    if (argc >= 3 && strcmp(argv[1], "bookmark") == 0) return cmd_profile("bookmark", argv[2], argc >= 4 ? argv[3] : argv[2], 0);
    if (argc >= 4 && strcmp(argv[1], "forget") == 0 && strcmp(argv[2], "--site") == 0) return cmd_profile("forget_site", argv[3], 0, 0);
    if (argc >= 4 && strcmp(argv[1], "forget") == 0 && strcmp(argv[2], "--since") == 0) return cmd_profile("forget_since", argv[3], 0, 0);
    if (argc >= 3 && strcmp(argv[1], "forget") == 0) return cmd_profile("forget", argv[2], 0, 0);
    if (argc >= 5 && strcmp(argv[1], "permit") == 0) return cmd_profile("permit", argv[2], argv[3], argv[4]);
    if (argc >= 3 && strcmp(argv[1], "retain") == 0) return cmd_profile("retain", argv[2], 0, 0);
    if (argc >= 3 && strcmp(argv[1], "profile") == 0) return cmd_profile("query", argv[2], 0, 0);
    if (argc >= 3 && strcmp(argv[1], "js") == 0) return cmd_js(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "script") == 0) return cmd_script(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "click") == 0) {
        vector<char> all;
        const char *had = getenv("AMB_CLICKS");
        if (had && *had) { append_range(all, had, strlen(had)); push_back(all, '\n'); }
        append_range(all, argv[3], strlen(argv[3]));
        push_back(all, 0);
        setenv("AMB_CLICKS", data(all), 1);
        return cmd_script(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "loop") == 0) return cmd_loop();
    if (argc >= 3 && strcmp(argv[1], "snapshot") == 0) return cmd_snapshot(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "load") == 0) return cmd_load(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "parse") == 0) return cmd_parse(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "style") == 0) return cmd_style(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "render") == 0) return cmd_render(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], "bench") == 0) return cmd_bench(argv[2], argc >= 4 ? atoi(argv[3]) > 0 ? atoi(argv[3]) : 1 : 5);
    if (argc >= 3 && strcmp(argv[1], "inspect") == 0)
        return cmd_inspect(argv[2], argc >= 5 && (strcmp(argv[3], "--query") == 0 || strcmp(argv[3], "--why") == 0) ? argv[3] : 0,
                           argc >= 5 ? argv[4] : 0);
    if (argc >= 4 && (strcmp(argv[1], "query") == 0 || strcmp(argv[1], "why") == 0)) return cmd_reason(argv[1], argv[2], argv[3], 0);
    if (argc >= 3 && strcmp(argv[1], "advise") == 0) return cmd_reason("advise", argv[2], 0, argc >= 4 && strcmp(argv[3], "--apply") == 0);
    fprintf(stderr, "usage: ambitious [--private | --profile DIR] COMMAND -- see src/shell/main.c\n");
    fprintf(stderr, "usage: ambitious loop | snapshot FILE | load PAGE | parse PAGE | style PAGE | render PAGE PNG | inspect PAGE [--query|--why GOAL] | query PAGE GOAL | why PAGE GOAL | advise PAGE [--apply]\n");
    return 2;
}
