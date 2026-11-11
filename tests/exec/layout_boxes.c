/* tests/exec/layout_boxes.c -- the layout module case by case: a page laid out in
   an 800x600 viewport, then the boxes of its elements that have an id (their
   content box, how their width was found), their lines and fragments.
   Margin collapsing, auto margins, percentages, max-width, box-sizing,
   inline-blocks, alignment, line breaking, white space, inline backgrounds,
   tables, list markers. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

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
        html_parse(&p, &d, cases[i].html, strlen(cases[i].html));
        css_cascade(&st, &d, 0);
        layout_status s = layout_run(&t, &d, &st, 0, 800, 600);
        printf("%s (%s, invariants: %d broken)\n", cases[i].what, layout_status_name(s), layout_check_invariants(&t, stdout));
        for (size_t k = 0; k < sizeof ids / sizeof ids[0]; k++) show(&d, &t, ids[k]);
    }
    return 0;
}
