/* tests/exec/svg_render.c -- SVG drawn (src/paint/svg.c), piece by piece: path data read
   as SVG 2 reads it (relative and implicit commands, arcs, compact numbers and flags, an
   error ending it); scenes' coverage drawn into small canvases (a rectangle at fractions
   of a pixel, the fill rules, strokes' caps and joins, dashes, a group's opacity, a clip,
   a pattern's tile, a mask, a filter's primitives, a hairline); SVG documents' natural
   sizes; pages' inline svg elements laid out, their boxes and the scenes layout made of
   them (presentation attributes, sheets, use, gradients, patterns, markers, masks,
   filters, image elements, text turned and on a path, geometry from CSS, switch,
   paint-order, vector-effect); hostile documents, which must draw in bounded time and
   memory (uses, clips, patterns, masks and markers of themselves, filters and text past
   every bound); and SVG images that are not well formed, which draw nothing. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
#include "../../src/paint/svg.h"
#include "../../src/paint/image.h"

static const char *const paths[] = {
    "M10 20 L30 40",
    "m10 10h30v20h-20z m40 0 20 0",
    "M0 0 C 10 0 20 10 20 20 S 30 40 40 40",
    "M0 0 Q 10 0 10 10 T 20 20",
    "M10 10 A 5 5 0 0 1 20 10",
    "M60 30a10 10 0 1 0.1.1.5.5",
    "M 1e1 2E-1 L .5.5-3-4",
    "M 0 0 L 10 10 Z L 5 0",
    "M 0 0 L 10 X 20",
    "L 10 10",
    "M 0,0 l 10,10,20,20",
    "M 0 0 A 0 5 0 0 1 10 0 A 10 10 0 0 1 10 0",
};

static void show_path(const char *d) {
    vector<double> out;
    size_t n = amb_svg_path(d, strlen(d), &out);
    printf("%-40s %zu:", d, n);
    for (size_t i = 0; i < len(out);) {
        int c = (int)at(out, i);
        int k = c == 'C' || c == 'K' ? 6 : c == 'Q' ? 4 : c == 'Z' ? 0 : 2;
        printf(" %c", c);
        for (int j = 1; j <= k; j++) printf(" %.4g", at(out, i + (size_t)j));
        i += (size_t)k + 1;
    }
    printf("\n");
}

/* a scene's coverage as rows of digits, 0 none to 9 whole */
static void show_scene(const char *what, const char *scene, uint32_t w, uint32_t h) {
    vector<uint8_t> px;
    amb_svg_coverage(scene, strlen(scene), w, h, &px);
    printf("%s\n", what);
    for (uint32_t y = 0; y < h; y++) {
        printf("  ");
        for (uint32_t x = 0; x < w; x++) printf("%d", (int)((at(px, (size_t)y * w + x) * 9 + 127) / 255));
        printf("\n");
    }
}

static const char *const docs[] = {
    "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='50'/>",
    "<?xml version='1.0'?>\n<!-- a comment -->\n<!DOCTYPE svg [ <!ENTITY e 'x'> ]>\n<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox='0 0 40 20'></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' width='3in' viewBox='0,0,40,20'></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' height='30' viewBox='0 0 40 20'></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' width='50%' height='40'></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg'></svg>",
    "<svg:svg xmlns:svg='http://www.w3.org/2000/svg' width='7' height='9'/>",
    "<svg width='10' height='10'/>",
    "<html><svg width='10' height='10'/></html>",
    "not xml at all",
};

static const char *const pages[] = {
    "<svg id=s width=60 height=40><rect x=5 y=5 width=20 height=10 fill=red stroke=blue stroke-width=2 /></svg>",
    "<style>.a { fill: green; stroke-width: 3 } svg { display: block }</style><svg id=s viewBox='0 0 20 10'><circle class=a cx=5 cy=5 r=4 "
    "stroke=black /></svg>",
    "<svg id=s width=30 height=30><defs><rect id=r width=10 height=10 /></defs><use href=#r x=5 y=5 fill=#123456 /><g fill=none "
    "stroke=currentColor style='color: #ff0000'><use href=#r x=15 y=15 /></g></svg>",
    "<svg id=s width=40 height=20><linearGradient id=g><stop offset=0 stop-color=red /><stop offset=1 stop-color=blue "
    "stop-opacity=.5 /></linearGradient><rect width=40 height=20 fill='url(#g)' /></svg>",
    "<p>an icon <svg id=s height=20 viewBox='0 0 10 5'></svg> inline</p>",
    /* 12t: a pattern, markers, a mask, a filter, an image, text turned and on a path, geometry from CSS, switch,
       paint-order and vector-effect */
    "<svg id=s width=40 height=20><pattern id=p patternUnits=userSpaceOnUse width=10 height=10 patternTransform='rotate(45)'><rect width=5 "
    "height=5 fill=red /></pattern><rect width=40 height=20 fill='url(#p)' /></svg>",
    "<svg id=s width=40 height=40><marker id=m markerWidth=4 markerHeight=4 refX=2 refY=2 orient=auto><rect width=4 height=4 /></marker>"
    "<polyline points='5,35 20,5 35,35' fill=none stroke=black marker-start='url(#m)' marker-mid='url(#m)' marker-end='url(#m)' /></svg>",
    "<svg id=s width=20 height=20><mask id=k maskUnits=userSpaceOnUse x=0 y=0 width=20 height=20><rect width=10 height=20 fill=white />"
    "</mask><rect width=20 height=20 mask='url(#k)' /></svg>",
    "<svg id=s width=20 height=20><filter id=f><feOffset dx=2 dy=2 result=o /><feGaussianBlur in=o stdDeviation=1 /><feMerge><feMergeNode />"
    "<feMergeNode in=SourceGraphic /></feMerge></filter><rect x=5 y=5 width=10 height=10 filter='url(#f)' /></svg>",
    "<svg id=s width=20 height=20><image href=nowhere.png x=2 y=3 width=10 height=12 preserveAspectRatio='xMinYMax slice' /></svg>",
    "<svg id=s width=30 height=30><text x=5 y=20 rotate=15 font-size=10 stroke=blue>I</text></svg>",
    "<svg id=s width=60 height=30><path id=l d='M5 20 L55 20' /><text font-size=10><textPath href=#l startOffset=10>l</textPath></text></svg>",
    "<style>circle { r: 5px } .r { width: 4px; x: 1px }</style><svg id=s width=20 height=20><circle cx=10 cy=10 r=2 /><rect class=r y=2 "
    "width=9 height=3 /><path d='M0 0 L1 1' style=\"d: path('M 2 2 L 8 2')\" stroke=red /></svg>",
    "<svg id=s width=20 height=20><switch><rect systemLanguage=fr width=5 height=5 /><circle r=3 cx=10 cy=10 /></switch><rect "
    "requiredExtensions='' width=5 height=5 /></svg>",
    "<svg id=s width=20 height=20><rect width=10 height=10 fill=green stroke=red paint-order=stroke vector-effect=non-scaling-stroke /></svg>",
};

/* documents made to break the drawing: each must draw in bounded time and memory */
static void hostile(vector<char> *out, int which) {
    clear(*out);
    const char *head = "<svg id=s width=50 height=50>";
    append_range(out, head, strlen(head));
    if (which == 0) {
        /* uses of uses of uses: ten copies a level, eight levels */
        const char *g0 = "<defs><rect id=l0 width=1 height=1 />";
        append_range(out, g0, strlen(g0));
        for (int k = 1; k <= 8; k++) {
            char buf[64];
            int n = snprintf(buf, sizeof buf, "<g id=l%d>", k);
            append_range(out, buf, (size_t)n);
            for (int j = 0; j < 10; j++) { n = snprintf(buf, sizeof buf, "<use href=#l%d />", k - 1); append_range(out, buf, (size_t)n); }
            append_range(out, "</g>", 4);
        }
        const char *tail = "</defs><use href=#l8 />";
        append_range(out, tail, strlen(tail));
    } else if (which == 1) {
        const char *t = "<g id=me><use href=#me /><use href=#you /></g><use id=you href=#me /><rect width=1e39 height=1e400 x=-1e30 />"
                        "<path d='M 1e38 1e38 L -1e38 2 C 0 0 0 0 1e30 -1e30 A 1e38 1e38 0 1 1 5 5 Z' stroke=red stroke-width=1e30 "
                        "stroke-dasharray='0.0001 0.0001' />"
                        "<circle r=10 stroke=blue stroke-width=3 transform='scale(1e30)' /><text x=1e30 y=5>far</text>";
        append_range(out, t, strlen(t));
    } else if (which == 2) {
        for (int k = 0; k < 300; k++) append_range(out, "<g opacity=0.9 clip-path=url(#c)>", 33);
        const char *t = "<clipPath id=c clip-path=url(#c)><rect width=40 height=40 /></clipPath><rect width=30 height=30 />";
        append_range(out, t, strlen(t));
    } else if (which == 3) {
        /* patterns of themselves and of each other, twenty deep; a mask masked by itself; markers of markers */
        const char *t = "<pattern id=me width=10 height=10 patternUnits=userSpaceOnUse><rect width=10 height=10 fill=url(#me) /></pattern>"
                        "<mask id=k><rect width=50 height=50 fill=white mask=url(#k) /></mask>"
                        "<marker id=mk markerWidth=9 markerHeight=9><path d='M0 0 L9 9 L0 9' marker-mid=url(#mk) stroke=red /></marker>";
        append_range(out, t, strlen(t));
        for (int k = 0; k < 20; k++) {
            char buf[160];
            int n = snprintf(buf, sizeof buf, "<pattern id=p%d width=0.5 height=0.5><rect width=9 height=9 fill=url(#p%d) /></pattern>", k, k + 1);
            append_range(out, buf, (size_t)n);
        }
        const char *u = "<rect width=50 height=25 fill=url(#me) stroke=url(#p0) mask=url(#k) /><polyline points='0,0 25,25 50,0' "
                        "marker-mid=url(#mk) fill=url(#p0) />";
        append_range(out, u, strlen(u));
    } else if (which == 4) {
        /* filters past every bound: a blur of 1e9, sixty-four primitives, an offset of 1e30, a region of 1e30 */
        const char *t = "<filter id=f x=-1e30 y=-1e30 width=1e31 height=1e31 filterUnits=userSpaceOnUse><feGaussianBlur stdDeviation=1e9 />"
                        "<feOffset dx=1e30 dy=-1e30 /></filter><filter id=g>";
        append_range(out, t, strlen(t));
        for (int k = 0; k < 64; k++) append_range(out, "<feGaussianBlur stdDeviation=3 />", 33);
        const char *u = "</filter><rect width=50 height=50 filter=url(#f) /><circle cx=25 cy=25 r=20 filter=url(#g) />";
        append_range(out, u, strlen(u));
    } else {
        /* text turned, fitted and set on a path past every bound */
        const char *t = "<path id=l d='M0 0 L50 50' /><text x=1e30 y=5 rotate='1e30 nan' textLength=1e30>far</text><text font-size=1e5>"
                        "<textPath href=#l startOffset=-1e30>no</textPath><textPath href=#nosuch>none</textPath></text>"
                        "<text textLength=-5 lengthAdjust=spacingAndGlyphs font-size=0.001 dominant-baseline=central>tiny</text>";
        append_range(out, t, strlen(t));
    }
    append_range(out, "</svg>", 6);
}

static const char *const bad_images[] = {
    "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5' fill='&nosuch;'/></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><g xmlns='http://example.com/other'><rect width='5' height='5'/></g>"
    "<rect x='5' y='5' width='5' height='5' fill='&#x23;123456'/></svg>",
    "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><![CDATA[ <rect/> ]]><!-- <rect/> --><?pi x?></svg>",
};

static uint32_t find_id(const dom_document *d, const char *id) {
    uint32_t a = names_find(d, "id", 2);
    size_t n = strlen(id);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        for (uint32_t q = d->node[x].first_attr; q; q = d->attr[q].next)
            if (d->attr[q].name == a && d->attr[q].value.len == n && memcmp(text_ptr(d, d->attr[q].value), id, n) == 0) return x;
    }
    return 0;
}

int main(void) {
    printf("-- path data\n");
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) show_path(paths[i]);

    printf("-- coverage\n");
    show_scene("a rectangle from (2.25, 2.5) to (7.75, 7.5)",
               "v 10 10 1\np 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 2.25 2.5 L 7.75 2.5 L 7.75 7.5 L 2.25 7.5 Z\n", 10, 10);
    show_scene("two squares wound the same way, even-odd: their overlap a hole",
               "v 12 8 1\np 1 0 0 1 0 0 c 000000ff 1 1 n 1 1 0 0 4 0 0 M 1 1 L 7 1 L 7 7 L 1 7 Z M 5 1 L 11 1 L 11 7 L 5 7 Z\n", 12, 8);
    show_scene("the same, non-zero",
               "v 12 8 1\np 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 1 1 L 7 1 L 7 7 L 1 7 Z M 5 1 L 11 1 L 11 7 L 5 7 Z\n", 12, 8);
    show_scene("a stroke 2 wide, butt, square and round caps",
               "v 14 9 1\np 1 0 0 1 0 0 n 1 0 c 000000ff 1 2 0 0 4 0 0 M 3 2 L 11 2\n"
               "p 1 0 0 1 0 0 n 1 0 c 000000ff 1 2 2 0 4 0 0 M 3 5 L 11 5\n"
               "p 1 0 0 1 0 0 n 1 0 c 000000ff 1 2 1 0 4 0 0 M 3 8 L 11 8\n", 14, 9);
    show_scene("a corner 4 wide: miter, round, bevel",
               "v 30 10 1\np 1 0 0 1 0 0 n 1 0 c 000000ff 1 4 0 0 4 0 0 M 2 8 L 5 3 L 8 8\n"
               "p 1 0 0 1 0 0 n 1 0 c 000000ff 1 4 0 1 4 0 0 M 12 8 L 15 3 L 18 8\n"
               "p 1 0 0 1 0 0 n 1 0 c 000000ff 1 4 0 2 4 0 0 M 22 8 L 25 3 L 28 8\n", 30, 10);
    show_scene("dashes 3 on, 2 off, from 1 into the pattern",
               "v 20 3 1\np 1 0 0 1 0 0 n 1 0 c 000000ff 1 2 0 0 4 1 2 3 2 M 0 1.5 L 20 1.5\n", 20, 3);
    show_scene("round dots of zero-length dashes",
               "v 20 4 1\np 1 0 0 1 0 0 n 1 0 c 000000ff 1 3 1 0 4 0 2 0 6 M 2 2 L 20 2\n", 20, 4);
    show_scene("a group at opacity 0.5: its squares' overlap no darker",
               "v 12 6 1\ng 0.5 0 0\np 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 1 1 L 7 1 L 7 5 L 1 5 Z\n"
               "p 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 5 1 L 11 1 L 11 5 L 5 5 Z\ne\n", 12, 6);
    show_scene("a square clipped by a disc",
               "v 12 12 1\ng 1 1 1\nk 1 0 0 1 0 0 0 M 11 6 C 11 8.761 8.761 11 6 11 C 3.239 11 1 8.761 1 6 C 1 3.239 3.239 1 6 1 C 8.761 1 11 3.239 11 6 Z\n"
               "p 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 0 0 L 12 0 L 12 12 L 0 12 Z\ne\n", 12, 12);
    show_scene("a linear gradient, black to transparent",
               "v 10 2 1\nl 1 10 0 0 2 0 0 0 0 1 0 0 2 0 000000ff 1 00000000\np 1 0 0 1 0 0 u 1 1 0 n 1 1 0 0 4 0 0 M 0 0 L 10 0 L 10 2 L 0 2 Z\n", 10, 2);
    show_scene("a pattern: a tile of 2 px, its first px black",
               "v 8 4 1\nP 1 1 0 0 1 0 0 2 2\np 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 0 0 L 1 0 L 1 1 L 0 1 Z\nE\n"
               "p 1 0 0 1 0 0 u 1 1 0 n 1 1 0 0 4 0 0 M 0 0 L 8 0 L 8 4 L 0 4 Z\n", 8, 4);
    show_scene("a mask: its left half white, its region the whole",
               "v 10 4 1\ng 1 0 0 1 0\nM 0 0 0 10 4 1 0 0 1 0 0\np 1 0 0 1 0 0 c ffffffff 1 0 n 1 1 0 0 4 0 0 M 0 0 L 5 0 L 5 4 L 0 4 Z\nE\n"
               "p 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 0 0 L 10 0 L 10 4 L 0 4 Z\ne\n", 10, 4);
    show_scene("a filter: the square moved 2 px right, blurred (a deviation of 1)",
               "v 14 8 1\ng 1 0 0 0 1\nF 0 0 14 8 1 0 0 1 0 0 2\nx 2 -1 -1 1 0 0 14 8 2 0\nx 1 0 0 1 0 0 14 8 1 1\nE\n"
               "p 1 0 0 1 0 0 c 000000ff 1 0 n 1 1 0 0 4 0 0 M 3 2 L 7 2 L 7 6 L 3 6 Z\ne\n", 14, 8);
    show_scene("a hairline, Skia's: a px each column, shared between the two the line passes",
               "v 10 5 1\np 1 0 0 1 0 0 n 1 0 c 000000ff 1 1 0 0 4 0 0 M 0.5 0.5 L 9.5 4\n", 10, 5);

    printf("-- natural sizes\n");
    for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
        double w, h, r;
        int ok = amb_svg_size((const uint8_t *)docs[i], strlen(docs[i]), &w, &h, &r);
        printf("%d %g x %g, ratio %.4g\n", ok, w, h, r);
    }

    printf("-- scenes\n");
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        dom_document d;
        html_parser p;
        css_style st;
        layout_tree t;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        layout_init(&t, 1);
        /* a standards-mode page (a quirks-mode page's p would lose its top margin) */
        vector<char> page;
        append_range(page, "<!DOCTYPE html>", 15);
        append_range(page, pages[i], strlen(pages[i]));
        html_parse(&p, &d, data(page), len(page));
        css_cascade(&st, &d, 0);
        layout_run(&t, &d, &st, 0, 800, 600);
        uint32_t x = find_id(&d, "s");
        for (uint32_t b = 1; b < t.box_len; b++) {
            if (!t.box[b].live || t.box[b].node.index != x) continue;
            amb_span sp = t.box[b].svg;
            printf("page %zu: box %g,%g %gx%g\n%.*s", i, t.box[b].x, t.box[b].y, t.box[b].w, t.box[b].h, (int)sp.len, text_ptr(&t, sp));
        }
    }

    printf("-- hostile documents\n");
    static const char *const what[6] = { "uses ten a level, eight deep", "uses of themselves, numbers past floats", "300 groups clipped by a clip of itself",
                                         "patterns, masks and markers of themselves", "filters past every bound", "text past every bound" };
    for (int k = 0; k < 6; k++) {
        vector<char> src;
        hostile(&src, k);
        dom_document d;
        html_parser p;
        css_style st;
        layout_tree t;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        layout_init(&t, 1);
        html_parse(&p, &d, data(src), len(src));
        css_cascade(&st, &d, 0);
        layout_run(&t, &d, &st, 0, 800, 600);
        uint32_t x = find_id(&d, "s");
        for (uint32_t b = 1; b < t.box_len; b++) {
            if (!t.box[b].live || t.box[b].node.index != x) continue;
            amb_span sp = t.box[b].svg;
            vector<char> scene;
            append_range(scene, text_ptr(&t, sp), sp.len);
            size_t lines = 0;
            for (size_t q = 0; q < len(scene); q++) lines += at(scene, q) == '\n';
            vector<uint8_t> px;
            amb_svg_coverage(data(scene), len(scene), 50, 50, &px);
            size_t ink = 0;
            for (size_t q = 0; q < len(px); q++) ink += at(px, q);
            printf("%s: %zu lines, ink %zu\n", what[k], lines, ink);
        }
    }
    for (size_t i = 0; i < sizeof bad_images / sizeof bad_images[0]; i++) {
        amb_canvas c = {0};
        defer(c) { AMB_CANVAS_DROP(c); }
        c.w = 10;
        c.h = 10;
        c.cx1 = 10;
        c.cy1 = 10;
        c.zoom = 1;
        resize(c.px, 300);
        for (size_t q = 0; q < 300; q++) c.px[q] = 255;
        int drawn = amb_svg_image(&c, (const uint8_t *)bad_images[i], strlen(bad_images[i]), 0, 0, 10, 10);
        printf("image %zu: %s, pixel (2,2) %02x%02x%02x, (7,7) %02x%02x%02x\n", i, drawn ? "drawn" : "not drawn", c.px[36], c.px[37], c.px[38],
               c.px[231], c.px[232], c.px[233]);
    }
    return 0;
}
