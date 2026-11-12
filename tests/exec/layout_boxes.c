/* tests/exec/layout_boxes.c -- the layout module case by case: a page laid out in
   an 800x600 viewport, then the boxes of its elements that have an id (their
   content box, how their width was found), their lines and fragments.
   Margin collapsing, auto margins, percentages, max-width, box-sizing,
   inline-blocks, alignment, line breaking, white space, inline backgrounds,
   tables, list markers; floats and clearance, positioned boxes, flex and grid
   containers, vertical-align, min and max heights, a percentage height, rowspan,
   hyphenation, bidirectional text, generated content; grid areas and named lines,
   a column flow, items spanning tracks; flex items and containers on their baselines;
   sticky boxes at rest; an inline element as a containing block; bidirectional text by
   characters (brackets mirrored, numbers, isolates). A case's page is a standards-mode page (a
   DOCTYPE is put first) unless it begins with <! itself: a comment there makes a quirks-mode
   page, whose quirks tests/pages/quirks-*.html hold to Chromium. Last, each text fragment's
   glyphs (frag.glyphs) against its text shaped alone in its paragraph's direction, for
   paragraphs of one direction each whose runs have as many pieces: a fragment takes its run's
   glyphs, shaped in that run's own direction. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>
#include "../../src/paint/font.h"

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);

typedef struct lcase { const char *what; const char *html; } lcase;

static const lcase cases[] = {
    { "h1's top margin collapses through body's", "<h1 id=a>Hi</h1>" },
    { "siblings' margins collapse", "<p id=a>A</p><p id=b>B</p>" },
    { "an empty block's margins collapse through it", "<p id=a>A</p><div id=e style='margin:30px 0'></div><p id=b>B</p>" },
    { "padding keeps a child's margin inside", "<div id=a style='padding-top:1px'><p id=b>x</p></div>" },
    { "auto margins centre a declared width", "<div id=a style='width:200px;margin:0 auto'>x</div>" },
    { "a percentage of the containing block", "<div id=a style='width:50%'>x</div>" },
    { "max-width holds it narrower", "<div id=a style='max-width:300px;margin:0 auto'>x</div>" },
    { "box-sizing: border-box", "<div id=a style='box-sizing:border-box;width:200px;padding:10px;border:5px solid'>x</div>" },
    { "an inline-block shrinks to fit", "<p><span id=a style='display:inline-block;padding:2px'>Hello</span> world</p>" },
    { "text-align: center", "<p id=a style='text-align:center'>Hi</p>" },
    { "lines break at spaces", "<div id=a style='width:100px'>one two three four five six</div>" },
    { "nowrap overflows", "<div id=a style='width:50px;white-space:nowrap'>one two three</div>" },
    { "pre keeps spaces and newlines", "<pre id=a>a  b\n c</pre>" },
    { "an inline element's background and padding", "<p id=p><span id=a style='background:yellow;padding:0 5px'>x</span> y</p>" },
    { "a table's columns", "<table id=t><tr><td id=a>short</td><td id=b>a much longer cell</td></tr></table>" },
    { "a table's declared width, shared", "<table id=t width=400><tr><td id=a>a</td><td id=b>bbbbbbbb</td></tr></table>" },
    { "a list item's marker", "<ul><li id=a>x</li></ul><ol><li id=b>y</li></ol>" },
    { "font sizes in em, and line height", "<div style='font-size:20px'><p id=a style='font-size:1.5em;line-height:1.2'>x</p></div>" },
    { "a float, and lines beside it", "<div style='width:200px'><div id=a style='float:left;width:50px;height:50px'></div><p id=b style='margin:0'>one two three four five six seven</p></div>" },
    { "a right float, and clearance", "<div id=a style='float:right;width:60px;height:40px'></div><p id=b style='clear:both;margin:0'>x</p>" },
    { "a formatting context beside a float", "<div style='float:left;width:100px;height:30px'></div><div id=a style='overflow:hidden'>x</div>" },
    { "a float shrinks to fit", "<div id=a style='float:left'>shrunk</div>" },
    { "relative offsets", "<div id=a style='position:relative;left:10px;top:5px'>x</div>" },
    { "absolute in a relative box", "<div id=t style='position:relative;width:300px;height:100px'><div id=a style='position:absolute;right:0;bottom:0;width:50px;height:20px'></div></div>" },
    { "absolute at its static position", "<p id=t>x</p><div id=a style='position:absolute'>y</div>" },
    { "fixed in the viewport", "<div id=a style='position:fixed;left:0;right:0;bottom:0;height:10px'></div>" },
    { "a flex row that grows", "<div id=t style='display:flex;width:300px'><div id=a style='flex:1'>a</div><div id=b style='flex:2'>b</div></div>" },
    { "a flex row that wraps, centred", "<div id=t style='display:flex;flex-wrap:wrap;width:200px;justify-content:center'><div id=a style='width:150px'>a</div><div id=b style='width:150px'>b</div></div>" },
    { "a flex column with a definite height", "<div id=t style='display:flex;flex-direction:column;height:100px'><div id=a>a</div><div id=b style='flex-grow:1'>b</div></div>" },
    { "a grid of fixed and fr columns", "<div id=t style='display:grid;grid-template-columns:100px 1fr;gap:10px;width:300px'><div id=a>a</div><div id=b>b</div><div id=e>c</div></div>" },
    { "a grid item placed by line and span", "<div id=t style='display:grid;grid-template-columns:repeat(3,1fr);width:300px'><div id=a style='grid-column:2/4'>a</div><div id=b style='grid-row:span 2'>b</div></div>" },
    { "vertical-align: super and middle", "<p id=p>x<sup id=a>2</sup><span id=b style='display:inline-block;width:10px;height:30px;vertical-align:middle'></span></p>" },
    { "min-height and max-height", "<div id=a style='min-height:50px'>x</div><div id=b style='max-height:10px'>y</div>" },
    { "a percentage height", "<div id=t style='height:200px'><div id=a style='height:25%'>x</div></div>" },
    { "a rowspan", "<table><tr><td id=a rowspan=2>a<br>b<br>c</td><td id=b>x</td></tr><tr><td id=e>y</td></tr></table>" },
    { "a word hyphenated", "<p id=a style='width:100px;hyphens:auto' lang=en>incomprehensible</p>" },
    { "right-to-left text", "<p id=a style='direction:rtl;width:200px'>abc \xd7\x90\xd7\x91\xd7\x92 def</p>" },
    { "right-to-left brackets, mirrored; numbers and isolates", "<p id=a dir=rtl>\xd7\x90\xd7\x91\xd7\x92 (\xd7\x93\xd7\x94\xd7\x95) 12</p><p id=b>ab <span dir=rtl>cd, \xd7\x90\xd7\x91</span> ef</p>" },
    { "generated content and counters", "<style>ol{counter-reset:c}li{counter-increment:c}li::before{content:counter(c) \". \"}</style><ol style='list-style:none'><li id=a>x</li><li id=b>y</li></ol>" },
    { "a grid's named areas and named lines", "<div id=t style='display:grid;grid-template-areas:\"h h\" \"s m\";grid-template-columns:[l] 100px [c] 1fr;width:300px'><div id=a style='grid-area:m'>m</div><div id=b style='grid-area:h'>h</div><div id=e style='grid-column:l/c'>e</div></div>" },
    { "a grid's column flow, dense", "<div id=t style='display:grid;grid-auto-flow:column dense;grid-template-rows:20px 20px;grid-auto-columns:50px'><div id=a>1</div><div id=b style='grid-row:span 2'>2</div><div id=e>3</div></div>" },
    { "a grid item that spans two auto columns", "<div id=t style='display:grid;grid-template-columns:auto auto;justify-content:start;width:300px'><div id=a>a</div><div id=b>bb</div><div id=e style='grid-column:span 2'>a longer spanning item</div></div>" },
    { "flex items aligned on their baselines", "<div id=t style='display:flex;align-items:baseline'><div id=a>x</div><div id=b style='font-size:32px'>Y</div><div id=e style='width:20px;height:30px'></div></div>" },
    { "an inline-flex's baseline, its first item's border box's bottom", "<p id=p>x <span id=a style='display:inline-flex'><span id=b style='display:block;width:20px;height:30px'></span><span>y</span></span></p>" },
    { "an absolutely positioned box in a relatively positioned span", "<p id=p>x <span id=t style='position:relative;padding:0 4px'>yy<span id=a style='position:absolute;right:0;bottom:0;width:5px;height:5px'></span></span></p>" },
    { "sticky boxes moved in from the viewport's edges, within their blocks", "<div id=t style='height:200px'><div id=a style='position:sticky;top:50px'>x</div></div><div style='height:700px'><div style='height:650px'></div><div id=b style='position:sticky;bottom:0'>y</div></div>" },
    /* a quirks-mode page (no DOCTYPE): its quirks as Chromium lays them out (tests/pages/quirks-*.html) */
    { "quirks: the body's first p's top margin left out, the body filling the viewport", "<!--q--><body id=t><p id=a>A</p><p id=b>B</p>" },
    { "quirks: a table cell's p's margins left out, its h1's bottom one kept", "<!--q--><table><tr><td id=a><p>x</p></td><td id=b><h1>y</h1></td></tr></table>" },
    { "quirks: an inline-block alone on its line, no strut", "<!--q--><div id=a><span id=b style='display:inline-block;width:10px;height:10px'></span></div>" },
    { "quirks: a span's own line height, not the block's", "<!--q--><div id=a style='line-height:50px'><span style='line-height:10px'>text</span></div>" },
    { "quirks: a percentage height through an auto one, to the body's", "<!--q--><div id=t><div id=a style='height:50%'>x</div></div>" },
    { "quirks: a cell's height its border box's", "<!--q--><table><tr><td id=a style='height:50px;padding:10px'>x</td></tr></table>" },
    { "limited quirks: no strut, but the margins and heights of standards mode",
      "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"><p id=a><span id=b style='display:inline-block;width:10px;height:10px'></span></p>" },
};

static uint32_t find_id(const dom_document *d, const char *id) {
    uint32_t a = names_find(d, "id", 2);
    size_t n = strlen(id);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next)
            if (d->attr[at].name == a && d->attr[at].value.len == n && memcmp(text_ptr(d, d->attr[at].value), id, n) == 0) return x;
    }
    return 0;
}

static void show(const dom_document *d, const layout_tree *t, const char *id) {
    uint32_t x = find_id(d, id);
    if (!x) return;
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live || t->box[b].node.index != x) continue;
        printf("  #%s %s: content %g,%g %gx%g; margins %g %g %g %g; width %s of %g\n", id, layout_box_kind_name(t->box[b].kind),
               t->box[b].x, t->box[b].y, t->box[b].w, t->box[b].h, t->box[b].mt, t->box[b].mr, t->box[b].mb, t->box[b].ml,
               layout_width_how_name(t->box[b].how), t->box[b].avail);
        for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) {
            printf("    line %g,%g %gx%g baseline %g:", t->line[ln].x, t->line[ln].y, t->line[ln].w, t->line[ln].h, t->line[ln].baseline);
            for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
                amb_span s = t->frag[fr].text;
                printf(" [%s %g+%g '%.*s']", layout_frag_kind_name(t->frag[fr].kind), t->frag[fr].x, t->frag[fr].w, (int)s.len, text_ptr(t, s));
            }
            printf("\n");
        }
    }
    /* an inline element's fragments, on its container's lines */
    for (uint32_t fr = 1; fr < t->frag_len; fr++)
        if (t->frag[fr].live && t->frag[fr].node.index == x && t->frag[fr].kind == LAYOUT_FRAG_KIND_INLINE)
            printf("  #%s inline: %g,%g %gx%g\n", id, t->frag[fr].x, t->frag[fr].y, t->frag[fr].w, t->frag[fr].h);
}

/* a text fragment's glyphs against text shaped alone, right to left or not: 1 when they are the
   same glyphs in the same order; their count into *count */
static int frag_as_shaped(const layout_tree *t, uint32_t fr, const amb_fonts *f, const char *text, int rtl, size_t *count) {
    amb_span gs = t->frag[fr].glyphs;
    vector<amb_shaped> gl;
    size_t tn = strlen(text);
    amb_shape(f, t->frag[fr].face, text, tn, 0, tn, rtl ? AMB_SHAPE_RTL : 0, &gl);
    size_t n = gs.len / sizeof(amb_rglyph);
    *count = n;
    if (n != len(gl)) return 0;
    const char *p = glyphs_ptr(t, gs);
    for (size_t k = 0; k < n; k++) {
        amb_rglyph g;
        memcpy(&g, p + k * sizeof g, sizeof g);
        if (g.gid != at(gl, k).gid) return 0;
    }
    return 1;
}

/* paragraphs of one line and one direction each, a right-to-left one first, each a run of three
   pieces: each one's text fragment's glyphs against its text shaped alone in its direction */
static void glyph_runs(void) {
    static const char *const ids[4] = { "a", "b", "e", "p" };
    static const char *const texts[4] = { "\xd7\x90\xd7\x91\xd7\x92 \xd7\x93\xd7\x94\xd7\x95", "abc (de)", "\xd7\x90\xd7\x91\xd7\x92 (\xd7\x93\xd7\x94)",
                                          "ab (cd)" };
    static const int rtl[4] = { 1, 0, 1, 0 };
    vector<char> page;
    append_range(page, "<!DOCTYPE html>", 15);
    for (size_t k = 0; k < 4; k++) {
        char head[32];
        int hn = snprintf(head, sizeof head, "<p id=%s%s>", ids[k], rtl[k] ? " dir=rtl" : "");
        append_range(page, head, (size_t)hn);
        append_range(page, texts[k], strlen(texts[k]));
        append_range(page, "</p>", 4);
    }
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    dom_document d;
    html_parser p;
    css_style st;
    layout_tree t;
    dom_init(&d, 1);
    html_init(&p, 1);
    css_init(&st, 1);
    layout_init(&t, 1);
    html_parse(&p, &d, data(page), len(page));
    css_cascade(&st, &d, 0);
    layout_status s = layout_run(&t, &d, &st, 0, 800, 600);
    printf("each text fragment's glyphs, its run's in the run's direction (%s):\n", layout_status_name(s));
    for (size_t k = 0; k < 4; k++) {
        uint32_t x = find_id(&d, ids[k]);
        for (uint32_t b = 1; b < t.box_len; b++) {
            if (!x || !t.box[b].live || t.box[b].node.index != x) continue;
            for (uint32_t ln = t.box[b].first_line; ln; ln = t.line[ln].next)
                for (uint32_t fr = t.line[ln].first_frag; fr; fr = t.frag[fr].next) {
                    if (t.frag[fr].kind != LAYOUT_FRAG_KIND_TEXT) continue;
                    size_t n = 0;
                    int same = frag_as_shaped(&t, fr, &f, texts[k], rtl[k], &n);
                    printf("  #%s '%s': %zu glyphs, %s its text shaped alone %s\n", ids[k], texts[k], n, same ? "as" : "NOT as",
                           rtl[k] ? "right to left" : "left to right");
                }
        }
    }
}

int main(void) {
    static const char *const ids[] = { "a", "b", "e", "p", "t" };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        dom_document d;
        html_parser p;
        css_style st;
        layout_tree t;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        layout_init(&t, 1);
        /* a standards-mode page, as the cases are written: a DOCTYPE first, unless the case's own
           page begins with one or a comment (a quirks-mode page when it has none) */
        vector<char> src;
        if (strncmp(cases[i].html, "<!", 2) != 0) append_range(src, "<!DOCTYPE html>", 15);
        append_range(src, cases[i].html, strlen(cases[i].html));
        html_parse(&p, &d, data(src), len(src));
        css_cascade(&st, &d, 0);
        layout_status s = layout_run(&t, &d, &st, 0, 800, 600);
        printf("%s (%s, invariants: %d broken)\n", cases[i].what, layout_status_name(s), layout_check_invariants(&t, stdout));
        for (size_t k = 0; k < sizeof ids / sizeof ids[0]; k++) show(&d, &t, ids[k]);
    }
    glyph_runs();
    return 0;
}
