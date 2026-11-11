/* tests/exec/fuzz_pipeline.c -- the engine against documents no one would write:
   tag soup from a grammar of elements, attributes, styles and text, with random
   bytes spliced in, through the whole pipeline -- parse, cascade, layout,
   accessibility, paint, every module's export -- and every module's invariants
   checked after each. Deterministic (a fixed seed), and run under valgrind by the
   exec gate: no crash, no invalid access, no leak, no broken invariant. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(a11y);
#include "../../src/paint/paint.h"

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return n ? (uint32_t)(rng_state % n) : 0;
}

static const char *const tags[] = { "div", "p", "span", "a", "b", "i", "ul", "ol", "li", "table", "tr", "td", "th", "h1", "h3", "img",
    "pre", "br", "button", "input", "label", "nav", "main", "section", "header", "footer", "em", "strong", "code", "blockquote", "select",
    "option", "textarea", "font", "center", "dl", "dt", "dd", "caption", "tbody", "form", "fieldset", "legend", "hr", "small", "sup" };
static const char *const attrs[] = { "class=x", "id=a", "id=b", "style=\"color:red;margin:3px 1em\"", "style=\"display:none\"",
    "style=\"display:inline-block;width:40%;padding:2px\"", "href=#", "alt=\"\"", "role=button", "role=nonsense", "aria-label=Hi",
    "aria-labelledby=a", "aria-labelledby=missing", "hidden", "colspan=3", "width=50", "bgcolor=#ff0", "align=center", "type=checkbox",
    "for=a", "tabindex=-1", "style=\"white-space:pre\"", "style=\"font:bold 20px/2 serif\"", "style=\"border:2px solid blue\"",
    "style=\"max-width:10px;box-sizing:border-box\"", "src=x.png", "lang=en" };
static const char *const sheets[] = { "p { color: blue } .x { margin: 0 auto; width: 300px }", "* { padding: 1px }",
    "li:nth-child(2n+1) { background: #eee } a:not(.x) { text-decoration: underline }", "@media (min-width: 600px) { div { display: flex } }",
    "td { border: 1px solid; vertical-align: top } table { width: 100% }", "h1 { font-size: 3em } h1 + p { font-size: smaller }",
    "/* unclosed", "{ } ;;; @import url(", "div > span ~ b { color: rgba(1,2,3,.5) !important }", ":is(p, li) :where(b) { font-weight: 900 }" };
static const char *const texts[] = { "hello ", "world", " ", "\n", "  spaced   out  ", "a&amp;b", "&lt;tag&gt;", "x", "longwordwithoutanyspacesatallforthelinebreaker",
    "\xc3\xa9t\xc3\xa9", "\xe2\x80\x94", "&nbsp;", "\t", "1.", "&#x1F600;", "end." };

static void gen(vector<char> *out, int depth) {
    int n = (int)rnd(4) + 1;
    for (int k = 0; k < n; k++) {
        uint32_t what = rnd(20);
        if (what < 7 || depth > 4) {
            size_t ti = rnd(sizeof texts / sizeof texts[0]);
            append_range(out, texts[ti], strlen(texts[ti]));
        } else if (what < 18) {
            size_t tg = rnd(sizeof tags / sizeof tags[0]);
            char open[256];
            int on = snprintf(open, sizeof open, "<%s", tags[tg]);
            append_range(out, open, (size_t)on);
            for (uint32_t a = rnd(3); a > 0; a--) {
                size_t ai = rnd(sizeof attrs / sizeof attrs[0]);
                push_back(out, ' ');
                append_range(out, attrs[ai], strlen(attrs[ai]));
            }
            push_back(out, '>');
            gen(out, depth + 1);
            if (rnd(5)) {
                size_t ct = rnd(10) ? tg : rnd(sizeof tags / sizeof tags[0]);
                int cn = snprintf(open, sizeof open, "</%s>", tags[ct]);
                append_range(out, open, (size_t)cn);
            }
        } else if (what == 18) {
            size_t si = rnd(sizeof sheets / sizeof sheets[0]);
            append_range(out, "<style>", 7);
            append_range(out, sheets[si], strlen(sheets[si]));
            if (rnd(4)) append_range(out, "</style>", 8);
        } else {
            for (uint32_t b = rnd(12); b > 0; b--) push_back(out, (char)(rnd(256)));
        }
    }
}

int main(void) {
    int rounds = 60, broken = 0;
    size_t bytes = 0, nodes = 0, boxes = 0, anodes = 0;
    own FILE *sink = fopen("/dev/null", "w");
    if (!sink) return 1;
    defer(sink) { fclose(sink); }
    for (int r = 0; r < rounds; r++) {
        vector<char> src;
        if (rnd(3) == 0) append_range(src, "<!DOCTYPE html>", 15);
        gen(&src, 0);
        bytes += len(src);
        dom_document d;
        html_parser p;
        css_style st;
        layout_tree lt;
        a11y_tree ax;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        layout_init(&lt, 1);
        a11y_init(&ax, 1);
        html_parse(&p, &d, data(src), len(src));
        css_cascade(&st, &d, 0);
        layout_run(&lt, &d, &st, 0, 400 + rnd(500), 300);
        a11y_build(&ax, &d, &st);
        amb_canvas c = {0};
        defer(c) { AMB_CANVAS_DROP(c); }
        amb_paint(&lt, 0, &c);
        dom_export(&d, sink);
        html_export(&p, sink);
        css_export(&st, sink);
        layout_export(&lt, sink);
        a11y_export(&ax, sink);
        int bad = dom_check_invariants(&d, stdout) + css_check_invariants(&st, stdout) + layout_check_invariants(&lt, stdout) +
                  a11y_check_invariants(&ax, stdout);
        if (bad) { broken++; printf("round %d: %d invariant(s) broken\n", r, bad); }
        nodes += node_count(d);
        boxes += box_count(lt);
        anodes += anode_count(ax);
    }
    printf("%d rounds, %zu bytes of input, %zu nodes, %zu boxes, %zu accessibility nodes: %d round(s) with a broken invariant\n",
           rounds, bytes, nodes, boxes, anodes, broken);
    return broken ? 1 : 0;
}
