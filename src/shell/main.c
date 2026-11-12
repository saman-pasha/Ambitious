/* src/shell/main.c -- ambitious, the browser's command.
 *
 *   ambitious loop                the DOM vertical slice end to end: a document built,
 *                                 its snapshot reasoned over, the reasoner's requests
 *                                 validated and applied, the change seen in a second
 *                                 snapshot, an old batch refused, and the reasoner's
 *                                 answers checked against the C side's own traversal
 *   ambitious loop - | loop FILE  a session: its commands a line each, from the standard
 *                                 input or FILE (session_page, below, lists them), over
 *                                 a page and a reasoner kept for the session, which is
 *                                 sent each snapshot as a delta from the one it holds
 *   ambitious sandbox             the reasoner's sandbox, as a child given it finds it
 *   ambitious snapshot FILE       the demo document's snapshot, written to FILE
 *   ambitious load PAGE           a page fetched with what it asks for: each resource
 *   ambitious parse PAGE          an HTML file parsed: its tree, its parse errors
 *   ambitious style PAGE          the page's elements with their computed styles
 *   ambitious render PAGE PNG     the page laid out in an 800x600 viewport and drawn
 *   ambitious js FILE             a script run by the JavaScript engine with no page
 *   ambitious script PAGE         what the page's scripts did: each script and how it ended,
 *                                 its console and errors, the listeners and timers at rest
 *   ambitious click PAGE SELECTOR the same after a click at the first element SELECTOR matches
 *   ambitious --click SELECTOR CMD PAGE ...   any page command after such a click (repeatable);
 *                                 a user's other actions the same way, in their order:
 *                                 --dblclick SELECTOR, --focus SELECTOR, --type SELECTOR TEXT
 *                                 (the element clicked, then TEXT typed), --key KEY (Enter,
 *                                 Tab, a, Shift+A ...): src/script/input.c
 *   ambitious --hover|--focus|--active SELECTOR CMD PAGE ...   any page command with the first
 *                                 element SELECTOR matches hovered, focused (as from the
 *                                 keyboard: :focus-visible too) or active, for the cascade
 *                                 (--focus is both: the action, and the cascade's state)
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
 *           | prefer KEY VALUE | prefs     (font_size, color_scheme, reduced_motion, zoom)
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
 * engine hands it a snapshot file and reads back text, or keeps it for a session
 * and speaks to it over its pipes (src/rt/amb_proc.c). It runs in a sandbox, and so
 * does the profile's keeper: limits, no network, a syscall filter, and one
 * directory to write in (AMB_SANDBOX=0 runs them without; AMB_REASON_MEM, _CPU,
 * _TIMEOUT, _FILES and _FSIZE set its limits). AMB_ROOT and AMB_COCOLOG
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
#include "../css/css.h"
#include "../js/js.h"
#include "../script/script.h"
#include "../html/encoding.h"
#include "../rt/amb_proc.h"
#include "../rt/amb_prefs.h"

/* the viewport a page is laid out in */
#define VIEW_W 800.0
#define VIEW_H 600.0

#define S(lit) lit, sizeof(lit) - 1

/* the tabs a session may hold, each with a reasoner of its own */
#define AMB_TABS 16

/* where the session's files go and how the reasoner is run: paths, no pointers. A
   reasoner kept for the session (rs) holds its page's snapshot and is sent what
   changes; use names the one reason() asks, or -1 for a fresh one each time */
typedef struct session {
    char dir[512];
    char cocolog[1024];
    char reasoner[1024];
    char keeper[1024];
    char profile[1024];        /* the profile's store directory; empty: none (or a private one) */
    int private_mode;
    int use;
    amb_prefs prefs;           /* the reader's preferences the stages are handed (src/rt/amb_prefs.h) */
    amb_sandbox sb;            /* the reasoner's (src/rt/amb_proc.c) */
    amb_reasoner rs[AMB_TABS];
} session;

/* a file of the session: dir/name */
static void session_path(const session *s, const char *name, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", s->dir, name);
}

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
    /* the directories by their absolute paths: the reasoner and the keeper each run in
       a directory of their own */
    char cwd[256];
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    if (!prof || s->private_mode) s->profile[0] = 0;
    else if (prof[0] == '/') snprintf(s->profile, sizeof s->profile, "%s", prof);
    else snprintf(s->profile, sizeof s->profile, "%s/%s", cwd, prof);
    if (work && work[0] == '/') snprintf(s->dir, sizeof s->dir, "%s", work);
    else if (work) snprintf(s->dir, sizeof s->dir, "%s/%s", cwd, work);
    else snprintf(s->dir, sizeof s->dir, "%s/ambitious-%ld", tmp ? tmp : "/tmp", (long)getpid());
    mkdir(s->dir, 0700);
    char rdir[700];
    session_path(s, "reason", rdir, sizeof rdir);
    amb_sandbox_init(&s->sb, rdir);
    amb_prefs_default(&s->prefs);
    s->use = -1;
    for (int i = 0; i < AMB_TABS; i++) {
        char held[700], err[700], hn[32], en[32];
        snprintf(hn, sizeof hn, "held-%d.pl", i);
        snprintf(en, sizeof en, "kept-%d.err", i);
        session_path(s, hn, held, sizeof held);
        session_path(s, en, err, sizeof err);
        amb_reasoner_init(&s->rs[i], s->cocolog, s->reasoner, held, err);
    }
    return 1;
}

/* the session's kept reasoners told to quit */
static void session_close(session *s) {
    for (int i = 0; i < AMB_TABS; i++) amb_reasoner_stop(&s->rs[i]);
}

/* what a reasoner wrote to its standard error from the offset from, on the engine's */
static void print_errors(const char *path, long from) {
    own FILE *f = fopen(path, "rb");
    if (!f) return;
    defer(f) { fclose(f); }
    if (from > 0) fseek(f, from, SEEK_SET);
    char buf[2048];
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    if (k) fprintf(stderr, "%.*s%s", (int)k, buf, buf[k - 1] == '\n' ? "" : "\n");
}

/* the reasoner run once over its command and arguments, in its sandbox, its answer into
   the file out. The argument vector holds this function's own copies: a parameter is
   the caller's borrow, which the safe part lets no array keep */
static int reason_once(const session *s, const char *cmd, const char *a1, const char *a2, const char *out) {
    char err[700], prog[1024], script[1024], c[64], x1[1024], x2[4096];
    session_path(s, "reason.err", err, sizeof err);
    snprintf(prog, sizeof prog, "%s", s->cocolog);
    snprintf(script, sizeof script, "%s", s->reasoner);
    snprintf(c, sizeof c, "%s", cmd);
    snprintf(x1, sizeof x1, "%s", a1);
    snprintf(x2, sizeof x2, "%s", a2 ? a2 : "");
    const char *argv[] = { prog, "-s", script, "--", c, x1, x2, 0 };
    if (!a2) argv[6] = 0;
    amb_ending how;
    memset(&how, 0, sizeof how);
    int st = amb_run_in(argv, out, err, &s->sb, &how);
    if (st != 0) fflush(stdout);           /* what was said before, before what is said of it */
    if (st != 0 && how.ended && !how.exited) fprintf(stderr, "ambitious: the reasoner ended, %s, asked to %s\n", how.why, cmd);
    else if (st != 0) { fprintf(stderr, "ambitious: the reasoner answered %d to %s:\n", st, cmd); print_errors(err, 0); }
    return st;
}

/* the reasoner kept for the session asked: the snapshot a1 made the one it holds (a
   delta from the one it held), then the request; diff's two snapshots each in turn,
   the second's delta then being the answer */
static int reason_kept(session *s, amb_reasoner *r, const char *cmd, const char *a1, const char *a2, const char *out) {
    int diff = strcmp(cmd, "diff") == 0;
    uint32_t d0 = r->deltas;
    fflush(stdout);                        /* what was said before, before what is said of it */
    int st = amb_reasoner_sync(r, a1, &s->sb);
    if (st == 0 && diff) {
        /* the second's delta keeps the facts it changes, which the reasoner then says */
        d0 = r->deltas;
        r->track = 1;
        st = amb_reasoner_sync(r, a2, &s->sb);
        r->track = 0;
    }
    if (st != 0) {
        if (r->end.ended && !r->end.exited) fprintf(stderr, "ambitious: the reasoner ended, %s, sent the snapshot for %s\n", r->end.why, cmd);
        else { fprintf(stderr, "ambitious: the reasoner refused the snapshot for %s:\n", cmd); print_errors(r->err, 0); }
        return 2;
    }
    if (getenv("AMB_TIMES"))
        fprintf(stderr, "ambitious: the snapshot reached the reasoner in %.1f ms, %.1f of them the engine's (%llu retracted, %llu added, %llu kept)\n",
                r->sync_ms, r->diff_ms, (unsigned long long)r->retracted, (unsigned long long)r->added, (unsigned long long)r->kept);
    /* a diff with no delta between its snapshots is answered by a fresh reasoner */
    if (diff && r->deltas == d0) return reason_once(s, cmd, a1, a2, out);
    st = amb_reasoner_ask(r, diff ? "changes" : cmd, diff ? 0 : a2, out, &s->sb);
    if (getenv("AMB_TIMES")) fprintf(stderr, "ambitious: the reasoner answered %s in %.1f ms\n", cmd, r->ask_ms);
    if (st < 0) fprintf(stderr, "ambitious: the reasoner ended, %s, asked to %s\n", r->end.why, cmd);
    else if (st != 0) { fprintf(stderr, "ambitious: the reasoner answered %d to %s:\n", st, cmd); print_errors(r->err, r->err_mark); }
    return st;
}

/* the reasoner over its command and arguments, its answer into the file out: the one
   kept for the session when there is one, else one run for the command */
static int reason(session *s, const char *cmd, const char *a1, const char *a2, const char *out) {
    if (s->use >= 0 && s->use < AMB_TABS) return reason_kept(s, &s->rs[s->use], cmd, a1, a2, out);
    return reason_once(s, cmd, a1, a2, out);
}

/* the profile keeper over a command and up to three arguments, its answer into the
   file out: the profile's store, or memory for a private session; -1 when there is
   no profile */
static int keep(const session *s, const char *cmd, const char *a1, const char *a2, const char *a3, const char *out) {
    if (!s->private_mode && !s->profile[0]) return -1;
    /* the keeper's sandbox: the reasoner's, writing in the profile's directory alone (a
       private session's keeper writes nowhere: its store is memory). Its address space
       is its own, AMB_KEEPER_MEM or 64 GiB: ZiguratIP maps its store a gigabyte at a time,
       and a store whose map is refused opens empty, then is written back empty. Its
       files may grow as large: a keeper stopped by the size of its store's file would
       stop in the middle of writing it */
    amb_sandbox kb = s->sb;
    const char *km = getenv("AMB_KEEPER_MEM");
    kb.mem = km && *km ? strtoull(km, 0, 10) : 64ull << 30;
    kb.fsize = kb.mem;
    if (!s->private_mode) {
        mkdir(s->profile, 0700);
        snprintf(kb.dir, sizeof kb.dir, "%s", s->profile);
    }
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
    amb_ending how;
    memset(&how, 0, sizeof how);
    int st = amb_run_in(argv, out, err, &kb, &how);
    if (st != 0 && how.ended && !how.exited) fprintf(stderr, "ambitious: the profile's keeper ended, %s\n", how.why);
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
static int cross_check(session *s, const dom_document *d, const char *snap) {
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

/* the same request asked of a fresh reasoner over the whole snapshot snap, its answer
   held to the kept reasoner's answer in the file kept: 1 the same, 0 not */
static int same_as_fresh(session *s, const char *cmd, const char *snap, const char *goal, const char *kept) {
    char fresh[700];
    session_path(s, "fresh.txt", fresh, sizeof fresh);
    if (reason_once(s, cmd, snap, goal, fresh) != 0) return 0;
    vector<char> a;
    vector<char> b;
    if (!read_all(kept, &a) || !read_all(fresh, &b)) return 0;
    if (len(a) == len(b) && memcmp(data(a), data(b), len(a)) == 0) return 1;
    printf("DIFFERENT answers to %s %s: kept %s, fresh %s\n", cmd, goal ? goal : "", kept, fresh);
    return 0;
}

static int cmd_loop(void) {
    session s;
    if (!session_open(&s)) return 2;
    defer(s) { session_close(&s); }
    s.use = 0;                 /* one reasoner for the whole loop */
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
    amb_reasoner *r = &s.rs[0];
    printf("   the second snapshot reached the reasoner as %s: %llu facts retracted, %llu added; it holds %llu\n",
           r->deltas ? "a delta" : "a whole snapshot", (unsigned long long)r->retracted, (unsigned long long)r->added,
           (unsigned long long)r->facts);
    if (r->deltas != 1) failures++;
    if (reason(&s, "query", snap2, "dom_missing_attribute(N, alt)", out) != 0) return 2;
    printf("   images still without alt, asked of the new snapshot: ");
    print_file(out, "");
    failures += cross_check(&s, &d, snap2);
    /* the answers after the delta, held to a fresh reasoner's over the whole snapshot */
    static const char *const asked[][2] = { { "check", 0 }, { "advise", 0 }, { "query", "dom_missing_attribute(N, A)" },
                                            { "query", "dom_element(N, _), dom_text_content(N, T)" },
                                            { "why", "dom_inside(N, A), dom_element(A, body), dom_text(N, _)" } };
    int same = 0, n_asked = (int)(sizeof asked / sizeof asked[0]);
    for (int q = 0; q < n_asked; q++) {
        if (reason(&s, asked[q][0], snap2, asked[q][1], out) != 0) return 2;
        if (same_as_fresh(&s, asked[q][0], snap2, asked[q][1], out)) same++;
    }
    printf("   the reasoner after the delta, and a fresh one over the whole second snapshot: the same answers to %d of %d requests\n", same, n_asked);
    failures += n_asked - same;

    printf("6. the first batch again, against the document that moved on:\n");
    apply_batch(&d, advice, &applied, &refused);
    printf("   %d applied, %d refused (the batch was made at an old revision)\n", applied, refused);
    if (applied != 0) failures++;

    printf("the loop's reasoner: %u process, %u requests: %u whole snapshot, %u delta\n", r->starts, r->requests, r->loads, r->deltas);
    if (r->starts != 1) failures++;
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
    script_runtime sr; js_vm vm; struct js_host hs; SCRIPT_HOST(hs, vm, d, nt, sr); defer(vm) { JS_VM_DROP(vm); } \
    defer(hs) { SCRIPT_HOST_DROP(hs); }
#define PAGE_ARGS &d, &p, &st, &lt, &nt, &ax, &sr, &vm
/* the virtual time a page's timers run for before it is taken to be at rest, ms */
#define SCRIPT_HORIZON 10000.0
/* the reader's preferences, from the profile when there is one (its keeper's prefs):
   each as pages are drawn with it. A private session opens no profile, and draws with
   the defaults */
static void session_prefs(session *s) {
    if (s->private_mode || !s->profile[0]) return;
    char out[700];
    session_path(s, "profile.out", out, sizeof out);
    vector<char> text;
    if (keep(s, "prefs", 0, 0, 0, out) != 0 || !read_all(out, &text)) return;
    push_back(text, 0);
    const char *t = data(text);
    for (size_t i = 0; t[i];) {
        if (amb_prefs_line(&s->prefs, t + i)) s->prefs.from_profile = 1;
        while (t[i] && t[i] != '\n') i++;
        if (t[i]) i++;
    }
}

/* a page command's session: its files, the reasoner, the profile, the reader's preferences */
#define PAGE_SESSION session s; if (!session_open(&s)) return 2; session_prefs(&s)

/* ---- the stages, each handed the reader's preferences (Phase 12i, 4) ----
   Every command runs the cascade, layout and paint through these, so each stage's call
   has the preferences beside it (src/rt/amb_prefs.h says what each is to do): the
   cascade reads the default font size, the colour scheme and reduced motion, and the
   viewport the zoom leaves (css_set_env); layout runs over that viewport, in CSS px;
   paint draws the page at the device's px, its lengths scaled zoom device px each */
static void page_state(css_style *st, dom_document *d);
/* the cascade, after the page's states asked for (--hover, --focus, --active), with the
   reader's preferences: the viewport the zoom leaves, the default font size, the colour
   scheme and reduced motion as the media queries see them */
static void page_cascade(css_style *st, dom_document *d, net_loader *nt, const amb_prefs *pr) {
    double z = pr && pr->zoom > 0 ? pr->zoom : 1;
    css_set_env(VIEW_W / z, VIEW_H / z, pr ? pr->font_size : 16, pr && pr->color_scheme == AMB_SCHEME_DARK, pr && pr->reduced_motion);
    page_state(st, d);
    css_cascade(st, d, nt);
}
/* the layout over the viewport in CSS px: the window over the zoom */
static void page_layout(layout_tree *lt, dom_document *d, css_style *st, net_loader *nt, const amb_prefs *pr) {
    double z = pr && pr->zoom > 0 ? pr->zoom : 1;
    layout_run(lt, d, st, nt, VIEW_W / z, VIEW_H / z);
}
/* the page drawn at the device's px: zoomed, its lengths scaled first, so its text is set
   at the larger size */
static void page_paint(const layout_tree *lt, const net_loader *nt, amb_canvas *c, const amb_prefs *pr) {
    double z = pr && pr->zoom > 0 ? pr->zoom : 1;
    if (z == 1) { amb_paint(lt, nt, c); return; }
    amb_paint_zoom(lt, nt, c, z, 0, 0);
}
static void page_paint_view(const layout_tree *lt, const net_loader *nt, amb_canvas *c, double y, double h, const amb_prefs *pr) {
    double z = pr && pr->zoom > 0 ? pr->zoom : 1;
    if (z == 1) { amb_paint_view(lt, nt, c, y, h); return; }
    amb_paint_zoom(lt, nt, c, z, y, h);
}

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
        if (!d->node[x].live || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML ||
            !names_is(d, d->node[x].name, "title", 5)) continue;
        /* in the document: not in a template's contents */
        uint32_t up = x;
        while (up && up != 1) up = d->node[up].parent;
        if (up != 1) continue;
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
/* the user's actions asked for (--click SELECTOR and its kin; AMB_ACTIONS a line each,
   ACTION TAB ARGUMENT [TAB TEXT]), in their order after the page's scripts: each at the
   first element its selector matches, a key by its name. The timers due run between
   actions, the last action's for the horizon */
static void page_clicks(const dom_document *d, js_vm *vm) {
    const char *env = getenv("AMB_ACTIONS");
    if (!env || !*env) return;
    vector<char> all;
    append_range(all, env, strlen(env));
    size_t i = 0;
    static const char *const kinds[] = { "click", "dblclick", "focus", "type", "key" };
    static const uint32_t codes[] = { SCRIPT_ACT_CLICK, SCRIPT_ACT_DBLCLICK, SCRIPT_ACT_FOCUS, SCRIPT_ACT_TYPE, SCRIPT_ACT_KEY };
    while (i < len(all)) {
        size_t e = i;
        while (e < len(all) && all[e] != '\n') e++;
        size_t t1 = i, t2;
        while (t1 < e && all[t1] != '\t') t1++;
        t2 = t1 < e ? t1 + 1 : e;
        size_t t3 = t2;
        while (t3 < e && all[t3] != '\t') t3++;
        uint32_t kind = 0;
        for (size_t k = 0; k < 5; k++) if (t1 - i == strlen(kinds[k]) && memcmp(data(all) + i, kinds[k], t1 - i) == 0) kind = codes[k];
        const char *text = data(all) + (t3 < e ? t3 + 1 : e);
        size_t tn = t3 < e ? e - t3 - 1 : 0;
        double horizon = e + 1 >= len(all) ? SCRIPT_HORIZON : 0;
        if (kind == SCRIPT_ACT_KEY) script_action(vm, kind, 0, data(all) + t2, t3 - t2, horizon);
        else {
            uint32_t x = t3 > t2 ? script_select(d, data(all) + t2, t3 - t2) : 0;
            if (!x) fprintf(stderr, "ambitious: --%.*s %.*s: no element matches\n", (int)(t1 - i), data(all) + i, (int)(t3 - t2), data(all) + t2);
            /* an action not done still lets the timers run */
            script_action(vm, x ? kind : 0, x, text, tn, horizon);
        }
        i = e + 1;
    }
}
/* the virtual time the page's timers run for before its user's actions: none when there
   are actions, which come at load as a user's would; else the horizon */
static double page_horizon(void) {
    const char *e = getenv("AMB_ACTIONS");
    return e && *e ? 0 : SCRIPT_HORIZON;
}
/* a user's action appended to AMB_ACTIONS: its kind, its argument, its text */
static void add_action(const char *kind, const char *arg, const char *text) {
    vector<char> all;
    const char *had = getenv("AMB_ACTIONS");
    if (had && *had) { append_range(all, had, strlen(had)); push_back(all, '\n'); }
    append_range(all, kind, strlen(kind));
    push_back(all, '\t');
    append_range(all, arg, strlen(arg));
    if (text) { push_back(all, '\t'); append_range(all, text, strlen(text)); }
    push_back(all, 0);
    setenv("AMB_ACTIONS", data(all), 1);
}

/* where the loader keeps cookies and cached responses (net_keep): the profile's
   directory, the cache in AMB_CACHE when it is set; a private session, or one with
   no profile, keeps its cookies for the run and caches nothing */
static void loader_keep(const session *s, net_loader *nt) {
    char jar[1100], cache[1100];
    jar[0] = 0;
    cache[0] = 0;
    const char *ac = getenv("AMB_CACHE");
    if (!s->private_mode && s->profile[0]) {
        snprintf(jar, sizeof jar, "%s/cookies", s->profile);
        snprintf(cache, sizeof cache, "%s/cache", s->profile);
    }
    if (!s->private_mode && ac && *ac) snprintf(cache, sizeof cache, "%s", ac);
    net_keep(nt, jar, strlen(jar), cache, strlen(cache));
}

/* one state asked for: the first element the selector v matches, in that state */
static void page_state_one(css_style *st, dom_document *d, const char *opt, const char *v, css_state_kind kind) {
    if (!v || !*v) return;
    uint32_t x = script_select(d, v, strlen(v));
    if (!x) { fprintf(stderr, "ambitious: --%s %s: no element matches\n", opt, v); return; }
    css_set_state(st, d, kind, x);
    if (kind == CSS_STATE_KIND_FOCUS) css_set_state(st, d, CSS_STATE_KIND_FOCUS_VISIBLE, x);
}
/* the page's state asked for (--hover, --focus, --active SELECTOR: AMB_HOVER, AMB_FOCUS,
   AMB_ACTIVE), before the cascade: what :hover, :focus, :focus-visible, :focus-within and
   :active read. A page at rest has none (the cascade reads no pointer or keyboard) */
static void page_state(css_style *st, dom_document *d) {
    page_state_one(st, d, "hover", getenv("AMB_HOVER"), CSS_STATE_KIND_HOVER);
    page_state_one(st, d, "focus", getenv("AMB_FOCUS"), CSS_STATE_KIND_FOCUS);
    page_state_one(st, d, "active", getenv("AMB_ACTIVE"), CSS_STATE_KIND_ACTIVE);
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
    loader_keep(s, nt);
    /* localStorage kept in the profile's directory; a private session's for the run */
    if (!s->private_mode && s->profile[0]) {
        char stfile[1100];
        snprintf(stfile, sizeof stfile, "%s/storage", s->profile);
        script_storage_file(vm, stfile);
    }
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
    char final[4200], site[512], answer[700], type[300];
    amb_span fu = nt->resource[pr].final_url, ty = nt->resource[pr].type, cs = nt->resource[pr].charset;
    snprintf(final, sizeof final, "%.*s", (int)fu.len, text_ptr(nt, fu));
    /* the type with the charset its server named, as the sniffer reads a Content-Type */
    if (cs.len) snprintf(type, sizeof type, "%.*s;charset=%.*s", (int)ty.len, text_ptr(nt, ty), (int)cs.len, text_ptr(nt, cs));
    else snprintf(type, sizeof type, "%.*s", (int)ty.len, text_ptr(nt, ty));
    /* the bytes decoded in the encoding sniffed from them, the type they were served as
       and whether they are a local file's (src/html/encoding.h) */
    html_set_scripting(1);   /* the page's scripts run */
    html_parse_bytes(p, d, data(src), len(src), type, strlen(type), strncmp(final, "file:", 5) == 0);
    /* the encoding it was decoded with, which its forms submit in */
    if (p->encoding_len > 1) {
        amb_span en = p->encoding[p->encoding_len - 1].name;
        script_charset(vm, bytes_ptr(p, en), en.len);
    }
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
        script_run_page(vm, page_horizon());
        page_clicks(d, vm);
    }
    script_settle(vm);
    page_cascade(st, d, nt, &s->prefs);
    page_layout(lt, d, st, nt, &s->prefs);
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
    uint32_t styles = 0;
    for (uint32_t es = 1; es < st.estyle_len; es++) if (st.estyle[es].style > styles) styles = st.estyle[es].style;
    printf("%zu rules, %zu declarations, %zu matches, %zu computed values (%u styles)\n", rule_count(st), decl_count(st), match_count(st),
           (size_t)estyle_count(st) * CSS_NPROPS, styles);
    return 0;
}

/* the page drawn into a PNG: its size, what it holds, and its pixels' checksum */
static int cmd_render(const char *page, const char *png) {
    PAGE_SESSION;
    PAGE_STORES;
    if (!load_page(&s, page, PAGE_ARGS)) return 2;
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    page_paint(&lt, &nt, &c, &s.prefs);
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
    double horizon = page_horizon();
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
        char type[300];
        amb_span ty = nt.resource[ph.index].type, fu = nt.resource[ph.index].final_url, cs = nt.resource[ph.index].charset;
        if (cs.len) snprintf(type, sizeof type, "%.*s;charset=%.*s", (int)ty.len, text_ptr(&nt, ty), (int)cs.len, text_ptr(&nt, cs));
        else snprintf(type, sizeof type, "%.*s", (int)ty.len, text_ptr(&nt, ty));
        int file = fu.len >= 5 && memcmp(text_ptr(&nt, fu), "file:", 5) == 0;
        html_set_scripting(1);
        html_parse_bytes(&p, &d, data(src), len(src), type, strlen(type), file);
        double c = now_ms();
        net_fetch_subresources(&nt, &d, ph, 0);
        double e = now_ms();
        vm.host->page = ph.index;
        script_run_page(&vm, horizon);
        double e1 = now_ms();
        page_clicks(&d, &vm);
        double e2 = now_ms();
        script_settle(&vm);
        double e3 = now_ms();
        page_cascade(&st, &d, &nt, &s.prefs);
        double f = now_ms();
        page_layout(&lt, &d, &st, &nt, &s.prefs);
        double g = now_ms();
        a11y_build(&ax, &d, &st);
        double h = now_ms();
        amb_canvas cv = {0};
        defer(cv) { AMB_CANVAS_DROP(cv); }
        page_paint(&lt, &nt, &cv, &s.prefs);
        double i = now_ms();
        amb_canvas vw = {0};
        defer(vw) { AMB_CANVAS_DROP(vw); }
        page_paint_view(&lt, &nt, &vw, 0, VIEW_H, &s.prefs);
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
    defer(s) { session_close(&s); }
    s.use = 0;                 /* one reasoner for the diagnostics and the question */
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
    a11y_print_tree(&ax, &d, stdout);
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

/* ---- the session: `loop -`, commands a line each, over a page and its reasoner ---- */

/* the page's cascade, layout and accessibility tree made anew over its document as it
   is now (after a click, say), in stores of their own, and its snapshot written */
static int session_snapshot(dom_document *d, const html_parser *p, net_loader *nt, const script_runtime *sr, const amb_prefs *pr, const char *path) {
    css_style st;
    layout_tree lt;
    a11y_tree ax;
    css_init(&st, 1);
    layout_init(&lt, 1);
    a11y_init(&ax, 1);
    page_cascade(&st, d, nt, pr);
    page_layout(&lt, d, &st, nt, pr);
    a11y_build(&ax, d, &st);
    return write_page_snapshot(d, p, &st, &lt, nt, &ax, sr, path);
}

/* what the session's reasoner was sent and holds */
static void session_reasoner(const amb_reasoner *r) {
    printf("reasoner: %s; %u start(s), %u requests: %u whole snapshot(s), %u delta(s) (%u predicates edited, %u rebuilt); %llu facts held\n",
           r->pid > 0 ? "running" : "not running", r->starts, r->requests, r->loads, r->deltas, r->edits, r->rebuilds,
           (unsigned long long)r->facts);
    if (r->last)
        printf("reasoner: the last snapshot that changed reached it as %s: %llu facts retracted, %llu added\n",
               r->last == 2 ? "a delta" : "a whole snapshot", (unsigned long long)r->retracted, (unsigned long long)r->added);
    if (r->end.ended) printf("reasoner: the last one ended: %s\n", r->end.why);
    /* its memory, as the kernel counts it */
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%d/status", r->pid);
    own FILE *f = r->pid > 0 ? fopen(path, "rb") : 0;
    if (!f) return;
    defer(f) { fclose(f); }
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "VmRSS:", 6) == 0 && getenv("AMB_TIMES")) fprintf(stderr, "ambitious: the reasoner's memory, %s", line + 6);
}

/* ---- the session's tabs (Phase 12i, 3) ----
   A session holds up to AMB_TABS tabs, each a history of pages and a reasoner of its own
   (s->rs[slot]). One reasoner a tab, not one for all of them: the rules speak of one
   document (dom_root/1, every diagnostic), a page's facts stay apart from another's, a
   reasoner that ends (its limits, its filter) ends one tab's, and each page's snapshot
   reaches its own reasoner as a delta. One page is live at a time, the current tab's:
   its stores are session_page's, and a tab made current again is loaded again with its
   clicks made again, as a browser that discards a background tab reloads it, while its
   reasoner keeps the facts it held, so the page reaches it as a delta (none at all when
   nothing changed). The profile keeps the tabs (prolog/amb/profile.pl): `quit' saves
   them as the session `last', unless it is private, and `loop --restore' opens them. */

/* a tab: its number (the profile keeps it), its reasoner's slot, the page of its
   history it shows (from 1) */
typedef struct tab_rec { uint32_t id, slot, at; } tab_rec;
/* a page of a tab's history: its URL and title in the session's text, and how it
   loaded when the tab last showed it: 0 not known, 1 ok, 2 failed, why */
typedef struct entry_rec { uint32_t tab, url_off, url_len, title_off, title_len; int loaded; char why[64]; } entry_rec;
/* a click made in a tab's page since it was loaded, made again when it is loaded again */
typedef struct click_rec { uint32_t tab, off, len; } click_rec;

/* the index in entries of tab id's i-th page (from 1), or UINT32_MAX */
static uint32_t tab_entry(const vector<entry_rec> *entries, uint32_t id, uint32_t i) {
    uint32_t k = 0;
    for (size_t e = 0; e < len(*entries); e++)
        if (at(*entries, e).tab == id && ++k == i) return (uint32_t)e;
    return UINT32_MAX;
}

/* the pages of tab id's history */
static uint32_t tab_pages(const vector<entry_rec> *entries, uint32_t id) {
    uint32_t k = 0;
    for (size_t e = 0; e < len(*entries); e++) if (at(*entries, e).tab == id) k++;
    return k;
}

/* a page put last in tab id's history */
static void tab_push(vector<entry_rec> *entries, vector<char> *pool, uint32_t id, const char *url, size_t ul, const char *title, size_t tl) {
    entry_rec x;
    memset(&x, 0, sizeof x);
    x.tab = id;
    x.url_off = (uint32_t)len(*pool);
    x.url_len = (uint32_t)ul;
    append_range(pool, url, ul);
    x.title_off = (uint32_t)len(*pool);
    x.title_len = (uint32_t)tl;
    append_range(pool, title, tl);
    push_back(entries, x);
}

/* tab id's history cut after its keep-th page (all of it for 0) */
static void tab_cut(vector<entry_rec> *entries, uint32_t id, uint32_t keep) {
    vector<entry_rec> kept;
    uint32_t k = 0;
    for (size_t e = 0; e < len(*entries); e++) {
        entry_rec x = at(*entries, e);
        if (x.tab != id || ++k <= keep) push_back(kept, x);
    }
    clear(*entries);
    for (size_t e = 0; e < len(kept); e++) push_back(entries, at(kept, e));
}

/* the clicks made in tab id's page, forgotten */
static void tab_unclick(vector<click_rec> *clicks, uint32_t id) {
    vector<click_rec> kept;
    for (size_t c = 0; c < len(*clicks); c++) if (at(*clicks, c).tab != id) push_back(kept, at(*clicks, c));
    clear(*clicks);
    for (size_t c = 0; c < len(kept); c++) push_back(clicks, at(kept, c));
}

/* a reasoner's slot no tab holds, or -1 */
static int tab_free_slot(const vector<tab_rec> *tabs) {
    for (int sl = 0; sl < AMB_TABS; sl++) {
        int used = 0;
        for (size_t t = 0; t < len(*tabs); t++) if (at(*tabs, t).slot == (uint32_t)sl) used = 1;
        if (!used) return sl;
    }
    return -1;
}

/* a tab opened at url, last; its number from 1, or 0 when no reasoner's slot is free */
static uint32_t tab_open(vector<tab_rec> *tabs, vector<entry_rec> *entries, vector<char> *pool, const char *url, size_t ul, const char *title,
                         size_t tl) {
    int sl = tab_free_slot(tabs);
    if (sl < 0) return 0;
    uint32_t id = 1;
    for (size_t t = 0; t < len(*tabs); t++) if (at(*tabs, t).id >= id) id = at(*tabs, t).id + 1;
    tab_rec r = { id, (uint32_t)sl, 1 };
    push_back(tabs, r);
    tab_push(entries, pool, id, url, ul, title, tl);
    return (uint32_t)len(*tabs);
}

/* the tabs, a line each: * the current one, its number, its page and title, where that
   is in its history, how it loaded */
static void tabs_list(const vector<tab_rec> *tabs, const vector<entry_rec> *entries, const vector<char> *pool, uint32_t cur) {
    printf("%zu tab(s)\n", len(*tabs));
    for (size_t t = 0; t < len(*tabs); t++) {
        tab_rec r = at(*tabs, t);
        uint32_t e = tab_entry(entries, r.id, r.at);
        if (e == UINT32_MAX) continue;
        entry_rec x = at(*entries, e);
        char shown[8192];
        url_display(data(*pool) + x.url_off, x.url_len, shown, sizeof shown);
        printf("%c %zu  %s  '%.*s'  page %u of %u, ", t == cur ? '*' : ' ', t + 1, shown, (int)x.title_len, data(*pool) + x.title_off, r.at,
               tab_pages(entries, r.id));
        if (x.loaded == 1) printf("loaded\n");
        else if (x.loaded == 2) printf("failed to load: %s\n", x.why);
        else printf("not loaded yet\n");
    }
}

/* the current tab's history, a line each, > the page it shows */
static void tab_history(const vector<tab_rec> *tabs, const vector<entry_rec> *entries, const vector<char> *pool, uint32_t cur) {
    tab_rec r = at(*tabs, cur);
    for (uint32_t i = 1; i <= tab_pages(entries, r.id); i++) {
        entry_rec x = at(*entries, tab_entry(entries, r.id, i));
        char shown[8192];
        url_display(data(*pool) + x.url_off, x.url_len, shown, sizeof shown);
        printf("%c %u  %s  '%.*s'\n", i == r.at ? '>' : ' ', i, shown, (int)x.title_len, data(*pool) + x.title_off);
    }
}

/* the tabs as the keeper reads them (prolog/amb/profile.pl, save_tabs): tab(Id, Order,
   At), entry(Id, I, Url, Title), loaded(Id, I, ok | failed(Why)), current(Id) */
static int tabs_write(const char *path, const vector<tab_rec> *tabs, const vector<entry_rec> *entries, const vector<char> *pool, uint32_t cur) {
    own FILE *f = fopen(path, "w");
    if (!f) return 0;
    defer(f) { fclose(f); }
    for (size_t t = 0; t < len(*tabs); t++) {
        tab_rec r = at(*tabs, t);
        fprintf(f, "tab(%u, %zu, %u).\n", r.id, t + 1, r.at);
        for (uint32_t i = 1; i <= tab_pages(entries, r.id); i++) {
            entry_rec x = at(*entries, tab_entry(entries, r.id, i));
            fprintf(f, "entry(%u, %u, ", r.id, i);
            amb_put_qatom(f, data(*pool) + x.url_off, x.url_len);
            fputs(", ", f);
            amb_put_qatom(f, data(*pool) + x.title_off, x.title_len);
            fputs(").\n", f);
            if (x.loaded == 1) fprintf(f, "loaded(%u, %u, ok).\n", r.id, i);
            if (x.loaded == 2) {
                fprintf(f, "loaded(%u, %u, failed(", r.id, i);
                amb_put_qatom(f, x.why, strlen(x.why));
                fputs(")).\n", f);
            }
        }
    }
    if (len(*tabs)) fprintf(f, "current(%u).\n", at(*tabs, cur).id);
    return 1;
}

/* the tabs kept in the profile as the session name: none in a private session, or
   with no profile */
static void session_save(session *s, const char *name, const vector<tab_rec> *tabs, const vector<entry_rec> *entries, const vector<char> *pool,
                         uint32_t cur, int asked) {
    if (s->private_mode) { if (asked) printf("a private session is never stored\n"); return; }
    if (!s->profile[0]) { if (asked) printf("no profile: nothing is kept (--profile DIR)\n"); return; }
    char file[700], out[700], when[32];
    session_path(s, "tabs.pl", file, sizeof file);
    session_path(s, "profile.out", out, sizeof out);
    if (!tabs_write(file, tabs, entries, pool, cur)) { printf("the session could not be written\n"); return; }
    snprintf(when, sizeof when, "%lld", now_seconds());
    if (keep(s, "save_tabs", name, file, when, out) == 0) print_file(out, "");
}

/* the profile's session name opened as the tabs: tab(Id, At), entry(Id, I, Url, Title),
   loaded(Id, I, ok | Why), current(Id), a line each, as the keeper writes them */
static void session_restore(session *s, const char *name, vector<tab_rec> *tabs, vector<entry_rec> *entries, vector<char> *pool, uint32_t *cur) {
    if (s->private_mode) { printf("a private session restores no session\n"); return; }
    char out[700];
    session_path(s, "profile.out", out, sizeof out);
    vector<char> text;
    if (keep(s, "tabs", name, 0, 0, out) != 0 || !read_all(out, &text)) { printf("no profile: no session to restore\n"); return; }
    const char *t = data(text);
    size_t start = 0, bad = 0;
    for (size_t i = 0; i < len(text); i++) {
        if (t[i] != '\n') continue;
        int64_t a = 0, b = 0;
        char u[4096], ti[1024];
        size_t ul = 0, tl = 0;
        int done = 0;
        size_t p = start;
        if (amb_lex_word(t, i, &p, "tab") && amb_lex_char(t, i, &p, '(') && amb_lex_int(t, i, &p, &a) && amb_lex_char(t, i, &p, ',') &&
            amb_lex_int(t, i, &p, &b)) {
            int sl = tab_free_slot(tabs);
            if (sl >= 0) { tab_rec r = { (uint32_t)a, (uint32_t)sl, (uint32_t)b }; push_back(tabs, r); }
            done = 1;
        }
        p = start;
        if (!done && amb_lex_word(t, i, &p, "entry") && amb_lex_char(t, i, &p, '(') && amb_lex_int(t, i, &p, &a) && amb_lex_char(t, i, &p, ',') &&
            amb_lex_int(t, i, &p, &b) && amb_lex_char(t, i, &p, ',') && amb_lex_atom(t, i, &p, u, sizeof u, &ul) && amb_lex_char(t, i, &p, ',') &&
            amb_lex_atom(t, i, &p, ti, sizeof ti, &tl)) {
            tab_push(entries, pool, (uint32_t)a, u, ul, ti, tl);
            done = 1;
        }
        p = start;
        if (!done && amb_lex_word(t, i, &p, "loaded") && amb_lex_char(t, i, &p, '(') && amb_lex_int(t, i, &p, &a) && amb_lex_char(t, i, &p, ',') &&
            amb_lex_int(t, i, &p, &b) && amb_lex_char(t, i, &p, ',') && amb_lex_atom(t, i, &p, u, sizeof u, &ul)) {
            uint32_t e = tab_entry(entries, (uint32_t)a, (uint32_t)b);
            if (e != UINT32_MAX) {
                entry_rec x = at(*entries, e);
                x.loaded = ul == 2 && memcmp(u, "ok", 2) == 0 ? 1 : 2;
                snprintf(x.why, sizeof x.why, "%.*s", (int)ul, u);
                if (x.loaded == 1) x.why[0] = 0;
                (*entries)[e] = x;
            }
            done = 1;
        }
        p = start;
        if (!done && amb_lex_word(t, i, &p, "current") && amb_lex_char(t, i, &p, '(') && amb_lex_int(t, i, &p, &a)) {
            for (size_t k = 0; k < len(*tabs); k++) if (at(*tabs, k).id == (uint32_t)a) *cur = (uint32_t)k;
            done = 1;
        }
        if (!done) bad++;
        start = i + 1;
    }
    /* a tab whose shown page its history lacks shows its last */
    for (size_t k = 0; k < len(*tabs); k++) {
        tab_rec r = at(*tabs, k);
        uint32_t n = tab_pages(entries, r.id);
        if (n && (r.at < 1 || r.at > n)) { r.at = n; (*tabs)[k] = r; }
    }
    if (*cur >= len(*tabs)) *cur = 0;
    printf("restored %zu tab(s) of the session %s%s\n", len(*tabs), name, bad ? ", some lines unread" : "");
    if (len(*tabs)) tabs_list(tabs, entries, pool, *cur);
}

#define SESSION_QUIT 0
#define SESSION_SWITCH 1

/* the current tab's page loaded (none when there are no tabs), its clicks made again,
   then the commands read from in run over it, until one makes another tab current or
   another page the tab's, or the input ends. The page's stores live here; the tab's
   reasoner is the session's (s->use) */
static int session_page(session *s, vector<tab_rec> *tabs, vector<entry_rec> *entries, vector<click_rec> *clicks, vector<char> *pool,
                        uint32_t *cur, FILE *in, int echo) {
    PAGE_STORES;
    int loaded = 0, dirty = 1;
    char snap[700], out[700], line[8192], url[4200];
    session_path(s, "page.pl", snap, sizeof snap);
    session_path(s, "answer.txt", out, sizeof out);
    s->use = -1;
    uint32_t tid = 0;
    if (len(*tabs)) {
        tab_rec t = at(*tabs, *cur);
        tid = t.id;
        s->use = (int)t.slot;
        uint32_t e = tab_entry(entries, t.id, t.at);
        entry_rec x = at(*entries, e);
        snprintf(url, sizeof url, "%.*s", (int)x.url_len, data(*pool) + x.url_off);
        char shown[8192];                  /* the URL as a person is shown it: its host in Unicode */
        url_display(url, strlen(url), shown, sizeof shown);
        fflush(stdout);                    /* what was said before what the loader says */
        loaded = load_page(s, url, PAGE_ARGS) != 0;
        if (loaded) {
            char title[512];
            page_title(&d, title, sizeof title);
            x.loaded = 1;
            x.why[0] = 0;
            x.title_off = (uint32_t)len(*pool);
            x.title_len = (uint32_t)strlen(title);
            append_range(pool, title, strlen(title));
            /* the clicks made in the page before, made again */
            for (size_t c = 0; c < len(*clicks); c++) {
                click_rec k = at(*clicks, c);
                if (k.tab != t.id) continue;
                uint32_t el = script_select(&d, data(*pool) + k.off, k.len);
                if (el) { script_click(&vm, el, SCRIPT_HORIZON); script_settle(&vm); }
            }
            printf("tab %u: %s  '%s'\n", *cur + 1, shown, title);
        } else {
            x.loaded = 2;
            snprintf(x.why, sizeof x.why, "%s", "not_fetched");
            for (uint32_t r = 1; r < nt.resource_len; r++)
                if (nt.resource[r].live) { snprintf(x.why, sizeof x.why, "%s", net_outcome_name(nt.resource[r].outcome)); break; }
            printf("tab %u: %s failed to load: %s\n", *cur + 1, shown, x.why);
        }
        (*entries)[e] = x;
    }
    while (fgets(line, sizeof line, in)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        size_t b = 0;
        while (line[b] == ' ' || line[b] == '\t') b++;
        if (!line[b] || line[b] == '#') continue;
        if (echo) printf("> %s\n", line + b);
        fflush(stdout);
        /* the command, then its argument */
        char cmd[64];
        size_t k = 0;
        while (line[b] && line[b] != ' ' && k + 1 < sizeof cmd) cmd[k++] = line[b++];
        cmd[k] = 0;
        while (line[b] == ' ') b++;
        const char *arg = line + b;
        if (strcmp(cmd, "quit") == 0) return SESSION_QUIT;
        if (strcmp(cmd, "echo") == 0) { printf("%s\n", arg); continue; }
        /* the tabs */
        if (strcmp(cmd, "open") == 0) {
            if (!*arg) { printf("open: a page, please\n"); continue; }
            char u[4200];
            page_url(arg, u, sizeof u);
            uint32_t at_ = tab_open(tabs, entries, pool, u, strlen(u), "", 0);
            if (!at_) { printf("open: %d tabs at most\n", AMB_TABS); continue; }
            *cur = at_ - 1;
            return SESSION_SWITCH;
        }
        if (strcmp(cmd, "tabs") == 0) { tabs_list(tabs, entries, pool, *cur); continue; }
        if (strcmp(cmd, "save") == 0) { session_save(s, *arg ? arg : "last", tabs, entries, pool, *cur, 1); continue; }
        if (strcmp(cmd, "prefs") == 0) {
            printf("the stages are handed: font_size %g px, color_scheme %s, reduced_motion %s, zoom %g (%s)\n", s->prefs.font_size,
                   s->prefs.color_scheme == AMB_SCHEME_DARK ? "dark" : "light", s->prefs.reduced_motion ? "reduce" : "no_preference",
                   s->prefs.zoom, s->prefs.from_profile ? "the profile's" : "the defaults");
            continue;
        }
        if (!len(*tabs)) { printf("no tab: open one first\n"); continue; }
        if (strcmp(cmd, "tab") == 0) {
            int w = atoi(arg);
            if (w < 1 || (size_t)w > len(*tabs)) { printf("tab %s: no such tab\n", arg); continue; }
            if ((uint32_t)w - 1 == *cur) { printf("tab %d is the current tab\n", w); continue; }
            *cur = (uint32_t)w - 1;
            return SESSION_SWITCH;
        }
        if (strcmp(cmd, "close") == 0) {
            int w = *arg ? atoi(arg) : (int)*cur + 1;
            if (w < 1 || (size_t)w > len(*tabs)) { printf("close %s: no such tab\n", arg); continue; }
            tab_rec t = at(*tabs, (size_t)w - 1);
            /* its reasoner told to quit, and its slot made as new */
            char held[1024], err[1024];
            snprintf(held, sizeof held, "%s", s->rs[t.slot].held);
            snprintf(err, sizeof err, "%s", s->rs[t.slot].err);
            amb_reasoner_stop(&s->rs[t.slot]);
            amb_reasoner_init(&s->rs[t.slot], s->cocolog, s->reasoner, held, err);
            tab_cut(entries, t.id, 0);
            tab_unclick(clicks, t.id);
            vector<tab_rec> kept;
            for (size_t q = 0; q < len(*tabs); q++) if (q != (size_t)w - 1) push_back(kept, at(*tabs, q));
            clear(*tabs);
            for (size_t q = 0; q < len(kept); q++) push_back(tabs, at(kept, q));
            printf("closed tab %d; %zu left\n", w, len(*tabs));
            if ((uint32_t)w - 1 < *cur) { (*cur)--; continue; }
            if ((uint32_t)w - 1 > *cur) continue;
            if (*cur >= len(*tabs) && *cur > 0) (*cur)--;
            return SESSION_SWITCH;
        }
        tab_rec ct = at(*tabs, *cur);
        if (strcmp(cmd, "history") == 0) { tab_history(tabs, entries, pool, *cur); continue; }
        if (strcmp(cmd, "go") == 0) {
            if (!*arg) { printf("go: a page, please\n"); continue; }
            char u[4200];
            page_url(arg, u, sizeof u);
            tab_cut(entries, ct.id, ct.at);
            tab_push(entries, pool, ct.id, u, strlen(u), "", 0);
            ct.at++;
            (*tabs)[*cur] = ct;
            tab_unclick(clicks, ct.id);
            return SESSION_SWITCH;
        }
        if (strcmp(cmd, "back") == 0 || strcmp(cmd, "forward") == 0) {
            int back = cmd[0] == 'b';
            if (back ? ct.at <= 1 : ct.at >= tab_pages(entries, ct.id)) { printf("%s: no page %s\n", cmd, back ? "before" : "after"); continue; }
            if (back) ct.at--;
            else ct.at++;
            (*tabs)[*cur] = ct;
            tab_unclick(clicks, ct.id);
            return SESSION_SWITCH;
        }
        if (strcmp(cmd, "reload") == 0) { tab_unclick(clicks, ct.id); return SESSION_SWITCH; }
        if (strcmp(cmd, "reasoner") == 0) { session_reasoner(&s->rs[ct.slot]); continue; }
        if (!loaded) { printf("no page: tab %u's failed to load\n", *cur + 1); continue; }
        if (strcmp(cmd, "click") == 0) {
            uint32_t x = script_select(&d, arg, strlen(arg));
            if (!x) { printf("click %s: no element matches\n", arg); continue; }
            script_click(&vm, x, SCRIPT_HORIZON);
            script_settle(&vm);
            click_rec c = { tid, (uint32_t)len(*pool), (uint32_t)strlen(arg) };
            append_range(pool, arg, strlen(arg));
            push_back(clicks, c);
            dirty = 1;
            printf("clicked ");
            print_elem(&d, node_handle(&d, x));
            printf("; the document is at revision %llu\n", (unsigned long long)dom_revision(&d));
            continue;
        }
        /* fresh CMD [GOAL]: the request asked of a fresh reasoner over the whole snapshot */
        int fresh = strcmp(cmd, "fresh") == 0;
        if (fresh) {
            size_t c = 0;
            while (arg[c] && arg[c] != ' ' && c + 1 < sizeof cmd) { cmd[c] = arg[c]; c++; }
            cmd[c] = 0;
            arg += c;
            while (*arg == ' ') arg++;
        }
        int ask = strcmp(cmd, "query") == 0 || strcmp(cmd, "why") == 0, plain = strcmp(cmd, "advise") == 0 || strcmp(cmd, "check") == 0;
        if (ask || plain || (!fresh && strcmp(cmd, "snapshot") == 0)) {
            if (dirty) {
                double t0 = now_ms();
                if (!session_snapshot(&d, &p, &nt, &sr, &s->prefs, snap)) { printf("the snapshot could not be written\n"); continue; }
                if (getenv("AMB_TIMES")) fprintf(stderr, "ambitious: the page styled, laid out and its snapshot written in %.1f ms\n", now_ms() - t0);
                dirty = 0;
            }
            if (fresh) {
                double t0 = now_ms();
                reason_once(s, cmd, snap, ask ? arg : 0, out);
                if (getenv("AMB_TIMES")) fprintf(stderr, "ambitious: a fresh reasoner answered %s in %.1f ms\n", cmd, now_ms() - t0);
                print_file(out, "");
                continue;
            }
            if (strcmp(cmd, "snapshot") == 0) {
                vector<char> text;
                if (!read_all(snap, &text)) { printf("snapshot %s: not written\n", arg); continue; }
                own FILE *f = fopen(arg, "wb");
                if (!f) { printf("snapshot %s: not written\n", arg); continue; }
                defer(f) { fclose(f); }
                fwrite(data(text), 1, len(text), f);
                continue;
            }
            if (ask && !*arg) { printf("%s: a goal, please\n", cmd); continue; }
            int apply = strcmp(cmd, "advise") == 0 && strcmp(arg, "--apply") == 0;
            int rc = reason(s, cmd, snap, ask ? arg : 0, out);
            print_file(out, "");
            if (rc == 0 && apply) {
                int applied = 0, refused = 0;
                apply_batch(&d, out, &applied, &refused);
                if (applied) dirty = 1;
                printf("%d applied, %d refused; revision %llu\n", applied, refused, (unsigned long long)dom_revision(&d));
            }
            continue;
        }
        printf("%s: no such command (open, tabs, tab, close, go, back, forward, reload, history, save, click, query, why, advise, check, fresh, "
               "snapshot, reasoner, echo, quit)\n", cmd);
    }
    return SESSION_QUIT;
}

/* the session over the commands in in, each a line, echoed when they come from no
   terminal; with restore, the profile's session last opened first. At the end the tabs
   are kept in the profile as last, unless the session is private */
static int session_lines(FILE *in, int echo, int restore) {
    session s;
    if (!session_open(&s)) return 2;
    defer(s) { session_close(&s); }
    session_prefs(&s);
    vector<tab_rec> tabs;
    vector<entry_rec> entries;
    vector<click_rec> clicks;
    vector<char> pool;
    uint32_t cur = 0;
    if (restore) session_restore(&s, "last", &tabs, &entries, &pool, &cur);
    while (session_page(&s, &tabs, &entries, &clicks, &pool, &cur, in, echo) == SESSION_SWITCH) {}
    session_save(&s, "last", &tabs, &entries, &pool, cur, 0);
    if (s.private_mode) printf("the session was private: nothing of it is stored\n");
    return 0;
}

/* `loop [--restore] -` reads the session's commands from the standard input, `loop
   [--restore] FILE` from FILE */
static int cmd_session(const char *src, int restore) {
    if (strcmp(src, "-") == 0) return session_lines(stdin, !isatty(0), restore);
    own FILE *f = fopen(src, "rb");
    if (!f) { fprintf(stderr, "ambitious: cannot read %s\n", src); return 2; }
    defer(f) { fclose(f); }
    return session_lines(f, 1, restore);
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

/* the reasoner's sandbox as a child given it finds it on this machine */
static int cmd_sandbox(void) {
    session s;
    if (!session_open(&s)) return 2;
    if (!s.sb.on) { printf("the sandbox is off (AMB_SANDBOX=0)\n"); return 0; }
    return amb_sandbox_report(&s.sb, stdout) == 0 ? 0 : 1;
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
    /* the file's URL: a module's own (a .mjs file is one), the classic script's base for import() */
    vector<char> url;
    append_range(url, "file://", 7);
    char absbuf[4096];
    if (realpath(file, absbuf)) append_range(url, absbuf, strlen(absbuf));
    else append_range(url, file, strlen(file));
    vm.base_url = js_str_utf8(&vm, data(url), len(url));
    size_t fl = strlen(file);
    int rc = 0;
    if (fl > 4 && strcmp(file + fl - 4, ".mjs") == 0) {
        uint32_t p = js_module_main(&vm, vm.base_url, data(src), len(src));
        if (!p) {
            jv e = vm.exception;
            vm.throwing = 0;
            vm.exception = JV_UNDEF;
            clear(vm.out);
            js_error_text(&vm, e, &vm.out);
            rc = -2;
        }
    } else {
        js_gc_expose(&vm);
        rc = js_eval(&vm, data(src), len(src), file, 1);
    }
    /* the collector runs between this script's tasks (a page's waits on its host's roots) */
    vm.gc_enabled = 1;
    if (rc == 0) {
        /* the microtasks the script queued, then its timers, each followed by its own */
        js_run_jobs(&vm);
        js_gc_maybe(&vm);
        js_run_timers(&vm, 1e15, 1000000);
    }
    fflush(stdout);
    if (rc != 0) { fprintf(stderr, "%.*s\n", (int)len(vm.out), data(vm.out)); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    /* the options before the command: the profile's store, or a private session */
    while (argc >= 2 && argv[1][0] == '-' && argv[1][1] == '-') {
        if (strcmp(argv[1], "--private") == 0) { setenv("AMB_PRIVATE", "1", 1); argv++; argc--; continue; }
        if (strcmp(argv[1], "--profile") == 0 && argc >= 3) { setenv("AMB_PROFILE", argv[2], 1); argv += 2; argc -= 2; continue; }
        /* a user's action after the page's scripts: at the first element a selector
           matches, or a key by its name */
        if ((strcmp(argv[1], "--click") == 0 || strcmp(argv[1], "--dblclick") == 0 || strcmp(argv[1], "--focus") == 0 ||
             strcmp(argv[1], "--key") == 0) && argc >= 3) {
            add_action(argv[1] + 2, argv[2], 0);
            /* a focus is the cascade's state too (:focus, and :focus-visible as from the keyboard) */
            if (strcmp(argv[1], "--focus") == 0) setenv("AMB_FOCUS", argv[2], 1);
            argv += 2;
            argc -= 2;
            continue;
        }
        if ((strcmp(argv[1], "--hover") == 0 || strcmp(argv[1], "--active") == 0) && argc >= 3) {
            /* a state of the page for the cascade, at the first element the selector matches */
            if (strcmp(argv[1], "--hover") == 0) setenv("AMB_HOVER", argv[2], 1);
            else setenv("AMB_ACTIVE", argv[2], 1);
            argv += 2;
            argc -= 2;
            continue;
        }
        if (strcmp(argv[1], "--type") == 0 && argc >= 4) {
            add_action("type", argv[2], argv[3]);
            argv += 3;
            argc -= 3;
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
    if (argc >= 4 && strcmp(argv[1], "prefer") == 0) return cmd_profile("prefer", argv[2], argv[3], 0);
    if (argc >= 2 && strcmp(argv[1], "prefs") == 0) return cmd_profile("prefs", 0, 0, 0);
    if (argc >= 3 && strcmp(argv[1], "retain") == 0) return cmd_profile("retain", argv[2], 0, 0);
    if (argc >= 3 && strcmp(argv[1], "profile") == 0) return cmd_profile("query", argv[2], 0, 0);
    if (argc >= 3 && strcmp(argv[1], "js") == 0) return cmd_js(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "script") == 0) return cmd_script(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "click") == 0) {
        add_action("click", argv[3], 0);
        return cmd_script(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "sandbox") == 0) return cmd_sandbox();
    if (argc >= 3 && strcmp(argv[1], "loop") == 0 && strcmp(argv[2], "--restore") == 0) return cmd_session(argc >= 4 ? argv[3] : "-", 1);
    if (argc >= 3 && strcmp(argv[1], "loop") == 0) return cmd_session(argv[2], 0);
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
