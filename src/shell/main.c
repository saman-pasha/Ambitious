/* src/shell/main.c -- ambitious, the browser's command.
 *
 *   ambitious loop                the DOM vertical slice end to end: a document built,
 *                                 its snapshot reasoned over, the reasoner's requests
 *                                 validated and applied, the change seen in a second
 *                                 snapshot, an old batch refused, and the reasoner's
 *                                 answers checked against the C side's own traversal
 *   ambitious snapshot FILE       the demo document's snapshot, written to FILE
 *   ambitious parse PAGE          an HTML file parsed: its tree, its parse errors
 *   ambitious style PAGE          the page's elements with their computed styles
 *   ambitious render PAGE PNG     the page laid out in an 800x600 viewport and drawn
 *   ambitious query PAGE GOAL     the reasoner's answers over the page's facts
 *   ambitious why PAGE GOAL       the reasoner's proofs
 *   ambitious advise PAGE [--apply]  the reasoner's advice, applied as one batch
 *
 * A page's snapshot has four sections: the document, its parse, its style (the
 * cascade's sheets, rules, matches and computed values) and its layout (boxes,
 * lines and fragments, with how each box's width was found).
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
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(css);
amb_interface(layout);

#include "../paint/paint.h"

/* the viewport a page is laid out in */
#define VIEW_W 800.0
#define VIEW_H 600.0

#define S(lit) lit, sizeof(lit) - 1

/* where the session's files go and how the reasoner is run: paths, no pointers */
typedef struct session {
    char dir[512];
    char cocolog[1024];
    char reasoner[1024];
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

/* ---- a page: an HTML file parsed into a document, and styled ---- */

static int load_page(const char *path, dom_document *d, html_parser *p, css_style *st, layout_tree *lt) {
    vector<char> src;
    if (!read_all(path, &src)) { fprintf(stderr, "ambitious: cannot read %s\n", path); return 0; }
    dom_init(d, 1);
    html_init(p, 1);
    css_init(st, 1);
    layout_init(lt, 1);
    html_parse(p, d, data(src), len(src));
    css_cascade(st, d);
    layout_run(lt, d, st, VIEW_W, VIEW_H);
    return 1;
}

/* the page's snapshot: the document's section, the parse's, the style's, the layout's */
static int write_page_snapshot(const dom_document *d, const html_parser *p, const css_style *st, const layout_tree *lt, const char *path) {
    own FILE *f = fopen(path, "w");
    if (!f) return 0;
    defer(f) { fclose(f); }
    dom_export(d, f);
    html_export(p, f);
    css_export(st, f);
    layout_export(lt, f);
    return 1;
}

static int cmd_parse(const char *page) {
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    if (!load_page(page, &d, &p, &st, &lt)) return 2;
    dom_serialize(&d, dom_root(&d), stdout);
    printf("\n");
    printf("%zu nodes, %zu parse errors, invariants: %d broken\n", node_count(d), error_count(p), dom_check_invariants(&d, stdout));
    return 0;
}

/* the computed values of the properties a reader looks at first, of each element,
   indented by its depth */
static int cmd_style(const char *page) {
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    if (!load_page(page, &d, &p, &st, &lt)) return 2;
    static const css_prop shown[] = { CSS_PROP_DISPLAY, CSS_PROP_COLOR, CSS_PROP_BACKGROUND_COLOR, CSS_PROP_FONT_SIZE,
                                      CSS_PROP_FONT_WEIGHT, CSS_PROP_MARGIN_TOP, CSS_PROP_PADDING_LEFT };
    static const char *const names[] = { "display", "color", "background", "font-size", "weight", "margin-top", "padding-left" };
    for (uint32_t c = 1; c < st.cval_len; c++) {
        uint32_t x = st.cval[c].node.index;
        if (c > 1 && st.cval[c - 1].node.index == x) continue;     /* each element's values are one run */
        int depth = 0;
        for (uint32_t y = d.node[x].parent; y > 1; y = d.node[y].parent) depth++;
        printf("%*s<%s>", 2 * depth, "", names_ptr(&d, d.node[x].name));
        for (size_t k = 0; k < sizeof shown / sizeof shown[0]; k++)
            for (uint32_t e = c; e < st.cval_len && st.cval[e].node.index == x; e++)
                if (st.cval[e].prop == shown[k]) {
                    amb_span t = st.cval[e].text;
                    printf(" %s: %.*s%s", names[k], (int)t.len, text_ptr(&st, t), st.cval[e].how == CSS_HOW_DECLARED ? "" : st.cval[e].how == CSS_HOW_INHERITED ? " (inherited)" : " (initial)");
                }
        printf("\n");
    }
    printf("%zu rules, %zu declarations, %zu matches, %zu computed values\n", rule_count(st), decl_count(st), match_count(st), cval_count(st));
    return 0;
}

/* the page drawn into a PNG: its size, what it holds, and its pixels' checksum */
static int cmd_render(const char *page, const char *png) {
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    if (!load_page(page, &d, &p, &st, &lt)) return 2;
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    amb_paint(&lt, &c);
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

/* query, why, advise: the reasoner over the page's snapshot */
static int cmd_reason(const char *what, const char *page, const char *goal, int apply) {
    session s;
    if (!session_open(&s)) return 2;
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree lt;
    if (!load_page(page, &d, &p, &st, &lt)) return 2;
    char snap[700], out[700];
    session_path(&s, "page.pl", snap, sizeof snap);
    session_path(&s, "answer.txt", out, sizeof out);
    if (!write_page_snapshot(&d, &p, &st, &lt, snap)) return 2;
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

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "loop") == 0) return cmd_loop();
    if (argc >= 3 && strcmp(argv[1], "snapshot") == 0) return cmd_snapshot(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "parse") == 0) return cmd_parse(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "style") == 0) return cmd_style(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "render") == 0) return cmd_render(argv[2], argv[3]);
    if (argc >= 4 && (strcmp(argv[1], "query") == 0 || strcmp(argv[1], "why") == 0)) return cmd_reason(argv[1], argv[2], argv[3], 0);
    if (argc >= 3 && strcmp(argv[1], "advise") == 0) return cmd_reason("advise", argv[2], 0, argc >= 4 && strcmp(argv[3], "--apply") == 0);
    fprintf(stderr, "usage: ambitious loop | snapshot FILE | parse PAGE | style PAGE | render PAGE PNG | query PAGE GOAL | why PAGE GOAL | advise PAGE [--apply]\n");
    return 2;
}
