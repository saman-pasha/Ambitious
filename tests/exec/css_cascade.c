/* tests/exec/css_cascade.c -- the CSS module case by case: a page, the element
   whose id is t, a property, and the computed value it must have. Selectors,
   specificity, origin and importance, the style attribute, inheritance and the
   global keywords, units, colours, shorthands, invalid values, media queries,
   presentational hints and the syntax a sheet may hold. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);

typedef struct tcase { const char *what; const char *html; css_prop prop; const char *want; } tcase;

#define RED "rgb(255, 0, 0)"
#define GREEN "rgb(0, 128, 0)"
#define BLACK "rgb(0, 0, 0)"

static const tcase cases[] = {
    /* selectors */
    { "type", "<style>p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "universal", "<style>* { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "class among classes", "<style>.a { color: red }</style><p id=t class='x a'>x", CSS_PROP_COLOR, RED },
    { "id", "<style>#t { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "[attr]", "<style>[title] { color: red }</style><p id=t title>x", CSS_PROP_COLOR, RED },
    { "[attr=v]", "<style>[lang=fr] { color: red }</style><p id=t lang=fr>x", CSS_PROP_COLOR, RED },
    { "[attr~=v]", "<style>[data-k~=b] { color: red }</style><p id=t data-k='a b c'>x", CSS_PROP_COLOR, RED },
    { "[attr|=v]", "<style>[lang|=en] { color: red }</style><p id=t lang=en-GB>x", CSS_PROP_COLOR, RED },
    { "[^=][$=][*=]", "<style>a[href^='https'][href$=\".org\"][href*=example] { color: red }</style><a id=t href='https://example.org'>x</a>", CSS_PROP_COLOR, RED },
    { "[attr=v i]", "<style>[type='TEXT' i] { color: red }</style><input id=t type=text>", CSS_PROP_COLOR, RED },
    { "[attr=v] case-sensitive", "<style>[type='TEXT'] { color: red }</style><input id=t type=text>", CSS_PROP_COLOR, BLACK },
    { "descendant", "<style>div p { color: red }</style><div><section><p id=t>x</section></div>", CSS_PROP_COLOR, RED },
    { "child, not a grandchild", "<style>div > p { color: red }</style><div><section><p id=t>x</section></div>", CSS_PROP_COLOR, BLACK },
    { "next sibling", "<style>h1 + p { color: red }</style><h1>a</h1><p id=t>x", CSS_PROP_COLOR, RED },
    { "later sibling", "<style>h1 ~ p { color: red }</style><h1>a</h1><div></div><p id=t>x", CSS_PROP_COLOR, RED },
    { ":first-child", "<style>li:first-child { color: red }</style><ul><li id=t>a<li>b</ul>", CSS_PROP_COLOR, RED },
    { ":last-child", "<style>li:last-child { color: red }</style><ul><li>a<li id=t>b</ul>", CSS_PROP_COLOR, RED },
    { ":nth-child(2n+1)", "<style>li:nth-child(2n+1) { color: red }</style><ul><li>1<li>2<li id=t>3</ul>", CSS_PROP_COLOR, RED },
    { ":nth-child(even) of the third", "<style>li:nth-child(even) { color: red }</style><ul><li>1<li>2<li id=t>3</ul>", CSS_PROP_COLOR, BLACK },
    { ":nth-child(-n+2)", "<style>li:nth-child(-n+2) { color: red }</style><ul><li>1<li id=t>2<li>3</ul>", CSS_PROP_COLOR, RED },
    { ":nth-last-child(1)", "<style>li:nth-last-child(1) { color: red }</style><ul><li>1<li id=t>2</ul>", CSS_PROP_COLOR, RED },
    { ":nth-of-type(2)", "<style>p:nth-of-type(2) { color: red }</style><div><p>1</p><span></span><p id=t>2</p></div>", CSS_PROP_COLOR, RED },
    { ":not(.x)", "<style>p:not(.x) { color: red }</style><p id=t class=y>x", CSS_PROP_COLOR, RED },
    { ":is(h1, p)", "<style>:is(h1, p) { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { ":root, inherited", "<style>:root { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { ":empty", "<style>p:empty { color: red }</style><p id=t></p>", CSS_PROP_COLOR, RED },
    { ":hover at rest", "<style>p:hover { color: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "::before is not the element", "<style>p::before { color: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { ":link (the UA's)", "<a id=t href=x>x</a>", CSS_PROP_COLOR, "rgb(0, 0, 238)" },
    { ":checked", "<style>input:checked { color: red }</style><input id=t type=checkbox checked>", CSS_PROP_COLOR, RED },
    { "a selector list", "<style>h1, p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    /* specificity, order, origin, importance, the style attribute */
    { "id over class", "<style>#t { color: green } .a { color: red }</style><p id=t class=a>x", CSS_PROP_COLOR, GREEN },
    { "the later of equals", "<style>.a { color: red } .b { color: green }</style><p id=t class='a b'>x", CSS_PROP_COLOR, GREEN },
    { "the later in one rule", "<style>p { color: red; color: green }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "!important over specificity", "<style>p { color: green !important } #t { color: red }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "style attribute over an id", "<style>#t { color: red }</style><p id=t style='color: green'>x", CSS_PROP_COLOR, GREEN },
    { "!important rule over the style attribute", "<style>p { color: green !important }</style><p id=t style='color: red'>x", CSS_PROP_COLOR, GREEN },
    { "!important style attribute over !important rule", "<style>#t { color: red !important }</style><p id=t style='color: green !important'>x", CSS_PROP_COLOR, GREEN },
    { "author over user agent", "<style>h1 { font-size: 20px }</style><h1 id=t>x</h1>", CSS_PROP_FONT_SIZE, "20px" },
    { ":where() adds nothing", "<style>:where(#t) { color: red } p { color: green }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { ":is() adds its argument's", "<style>:is(#t) { color: green } p.a { color: red }</style><p id=t class=a>x", CSS_PROP_COLOR, GREEN },
    { ":not() adds its argument's", "<style>p:not(#x) { color: green } p.a { color: red }</style><p id=t class=a>x", CSS_PROP_COLOR, GREEN },
    /* inheritance and the global keywords */
    { "colour inherited", "<style>div { color: red }</style><div><p id=t>x</div>", CSS_PROP_COLOR, RED },
    { "padding not inherited", "<style>div { padding-top: 5px }</style><div><p id=t>x</div>", CSS_PROP_PADDING_TOP, "0px" },
    { "inherit", "<style>div { padding-top: 5px } p { padding-top: inherit }</style><div><p id=t>x</div>", CSS_PROP_PADDING_TOP, "5px" },
    { "initial", "<style>div { color: red } p { color: initial }</style><div><p id=t>x</div>", CSS_PROP_COLOR, BLACK },
    { "unset, inherited", "<style>div { color: red } p { color: unset }</style><div><p id=t>x</div>", CSS_PROP_COLOR, RED },
    { "unset, not inherited", "<style>p { margin-top: unset }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "0px" },
    /* units and values */
    { "em of the parent's font size", "<style>div { font-size: 20px } p { font-size: 1.5em }</style><div><p id=t>x</div>", CSS_PROP_FONT_SIZE, "30px" },
    { "% of the parent's font size", "<style>div { font-size: 20px } p { font-size: 50% }</style><div><p id=t>x</div>", CSS_PROP_FONT_SIZE, "10px" },
    { "em of the element's own font size", "<style>p { font-size: 20px; margin-top: 2em }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "40px" },
    { "rem", "<style>p { margin-top: 2rem }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "32px" },
    { "pt", "<style>p { font-size: 12pt }</style><p id=t>x", CSS_PROP_FONT_SIZE, "16px" },
    { "in", "<style>p { width: 1in }</style><p id=t>x", CSS_PROP_WIDTH, "96px" },
    { "a size keyword", "<style>p { font-size: x-large }</style><p id=t>x", CSS_PROP_FONT_SIZE, "24px" },
    { "smaller", "<style>div { font-size: 24px } p { font-size: smaller }</style><div><p id=t>x</div>", CSS_PROP_FONT_SIZE, "20px" },
    { "bolder of 700 (the UA's b)", "<style>div { font-weight: 700 }</style><div><b id=t>x</b></div>", CSS_PROP_FONT_WEIGHT, "900" },
    { "lighter of 400", "<style>p { font-weight: lighter }</style><p id=t>x", CSS_PROP_FONT_WEIGHT, "100" },
    { "line-height, a number", "<style>p { line-height: 1.5 }</style><p id=t>x", CSS_PROP_LINE_HEIGHT, "1.5" },
    { "line-height, a percentage", "<style>p { font-size: 10px; line-height: 150% }</style><p id=t>x", CSS_PROP_LINE_HEIGHT, "15px" },
    { "#rgb", "<style>p { color: #0f0 }</style><p id=t>x", CSS_PROP_COLOR, "rgb(0, 255, 0)" },
    { "#rrggbbaa", "<style>p { color: #ff000080 }</style><p id=t>x", CSS_PROP_COLOR, "rgba(255, 0, 0, 0.501961)" },
    { "rgb() of percentages", "<style>p { color: rgb(100%, 50%, 0%) }</style><p id=t>x", CSS_PROP_COLOR, "rgb(255, 128, 0)" },
    { "rgba()", "<style>p { color: rgba(0, 0, 255, 0.5) }</style><p id=t>x", CSS_PROP_COLOR, "rgba(0, 0, 255, 0.501961)" },
    { "rgb() with spaces and /", "<style>p { color: rgb(0 128 0 / 50%) }</style><p id=t>x", CSS_PROP_COLOR, "rgba(0, 128, 0, 0.501961)" },
    { "hsl()", "<style>p { color: hsl(120, 100%, 25%) }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "a named colour", "<style>p { color: RebeccaPurple }</style><p id=t>x", CSS_PROP_COLOR, "rgb(102, 51, 153)" },
    { "transparent", "<style>p { background-color: transparent }</style><p id=t>x", CSS_PROP_BACKGROUND_COLOR, "rgba(0, 0, 0, 0)" },
    { "currentcolor", "<style>p { color: red; border-color: currentcolor }</style><p id=t>x", CSS_PROP_BORDER_COLOR, RED },
    { "border-color's initial is the colour", "<style>p { color: blue }</style><p id=t>x", CSS_PROP_BORDER_COLOR, "rgb(0, 0, 255)" },
    { "box-sizing", "<style>* { box-sizing: border-box }</style><p id=t>x", CSS_PROP_BOX_SIZING, "border-box" },
    { "max-width", "<style>p { max-width: 50% }</style><p id=t>x", CSS_PROP_MAX_WIDTH, "50%" },
    { "max-width: none", "<p id=t>x", CSS_PROP_MAX_WIDTH, "none" },
    /* invalid values drop their declaration */
    { "a length for a colour", "<style>p { color: red } p { color: 12px }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "a negative width", "<style>p { width: 10px } p { width: -5px }</style><p id=t>x", CSS_PROP_WIDTH, "10px" },
    { "a length without its unit", "<style>p { margin-top: 5 }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "16px" },
    { "zero needs no unit", "<style>p { margin-top: 0 }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "0px" },
    { "an unknown property", "<style>p { colour: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    /* shorthands */
    { "margin: 1 value", "<style>p { margin: 5px }</style><p id=t>x", CSS_PROP_MARGIN_LEFT, "5px" },
    { "margin: 2 values", "<style>p { margin: 1px 2px }</style><p id=t>x", CSS_PROP_MARGIN_LEFT, "2px" },
    { "margin: 3 values", "<style>p { margin: 1px 2px 3px }</style><p id=t>x", CSS_PROP_MARGIN_BOTTOM, "3px" },
    { "margin: 4 values", "<style>p { margin: 1px 2px 3px 4px }</style><p id=t>x", CSS_PROP_MARGIN_LEFT, "4px" },
    { "margin: 0 auto", "<style>p { margin: 0 auto }</style><p id=t>x", CSS_PROP_MARGIN_RIGHT, "auto" },
    { "padding: 4 values", "<style>p { padding: 1px 2px 3px 4px }</style><p id=t>x", CSS_PROP_PADDING_BOTTOM, "3px" },
    { "border: width style colour", "<style>p { border: 2px solid red }</style><p id=t>x", CSS_PROP_BORDER_LEFT_WIDTH, "2px" },
    { "border: its colour", "<style>p { border: 2px solid red }</style><p id=t>x", CSS_PROP_BORDER_COLOR, RED },
    { "border: none", "<style>p { border: 2px solid; border: none }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "0px" },
    { "border without a style", "<style>p { border: 2px red }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "0px" },
    { "border-bottom: its side", "<style>p { border-bottom: 3px dashed blue }</style><p id=t>x", CSS_PROP_BORDER_BOTTOM_WIDTH, "3px" },
    { "border-bottom: not the others", "<style>p { border-bottom: 3px dashed blue }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "0px" },
    { "border: thick", "<style>p { border: thick solid }</style><p id=t>x", CSS_PROP_BORDER_RIGHT_WIDTH, "5px" },
    { "font: its style", "<style>p { font: italic bold 20px/2 serif }</style><p id=t>x", CSS_PROP_FONT_STYLE, "italic" },
    { "font: its weight", "<style>p { font: italic bold 20px/2 serif }</style><p id=t>x", CSS_PROP_FONT_WEIGHT, "700" },
    { "font: its size", "<style>p { font: italic bold 20px/2 serif }</style><p id=t>x", CSS_PROP_FONT_SIZE, "20px" },
    { "font: its line height", "<style>p { font: italic bold 20px/2 serif }</style><p id=t>x", CSS_PROP_LINE_HEIGHT, "2" },
    { "font: what it leaves out, reset", "<style>p { font-weight: bold; font: 12px serif }</style><p id=t>x", CSS_PROP_FONT_WEIGHT, "400" },
    { "font: its family", "<style>p { font: 12px/1.4 'Helvetica Neue', Arial, sans-serif }</style><p id=t>x", CSS_PROP_FONT_FAMILY, "sans-serif" },
    { "font-family: a name it knows", "<style>p { font-family: \"Unknown Face\", Georgia, sans-serif }</style><p id=t>x", CSS_PROP_FONT_FAMILY, "serif" },
    { "font-family: the UA's monospace", "<pre id=t>x</pre>", CSS_PROP_FONT_FAMILY, "monospace" },
    { "font-family: inherited, serif at the root", "<p id=t>x", CSS_PROP_FONT_FAMILY, "serif" },
    { "font: without a size, dropped", "<style>p { font-weight: bold; font: bold serif }</style><p id=t>x", CSS_PROP_FONT_WEIGHT, "700" },
    { "background: its colour", "<style>p { background: url(x.png) #ff0 no-repeat }</style><p id=t>x", CSS_PROP_BACKGROUND_COLOR, "rgb(255, 255, 0)" },
    { "background: none resets the colour", "<style>p { background-color: red; background: none }</style><p id=t>x", CSS_PROP_BACKGROUND_COLOR, "rgba(0, 0, 0, 0)" },
    { "list-style", "<style>ul { list-style: square inside }</style><ul id=t><li>x</ul>", CSS_PROP_LIST_STYLE_TYPE, "square" },
    { "text-decoration's line", "<style>p { text-decoration: underline dotted red }</style><p id=t>x", CSS_PROP_TEXT_DECORATION, "underline" },
    /* media queries, against an 800 x 600 viewport */
    { "@media print", "<style>@media print { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@media screen and (min-width)", "<style>@media screen and (min-width: 600px) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@media (max-width)", "<style>@media (max-width: 600px) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@media not print", "<style>@media not print { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "<style media=print>", "<style media=print>p { color: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    /* presentational hints */
    { "bgcolor", "<body id=t bgcolor='#ffcc00'>x", CSS_PROP_BACKGROUND_COLOR, "rgb(255, 204, 0)" },
    { "<font color>", "<font id=t color=red>x</font>", CSS_PROP_COLOR, RED },
    { "<font size>", "<font id=t size=5>x</font>", CSS_PROP_FONT_SIZE, "24px" },
    { "<td align>", "<table><tr><td id=t align=right>x</table>", CSS_PROP_TEXT_ALIGN, "right" },
    { "<table width>", "<table id=t width=50%><tr><td>x</table>", CSS_PROP_WIDTH, "50%" },
    { "a hint under an author rule", "<style>font { color: green }</style><font id=t color=red>x</font>", CSS_PROP_COLOR, GREEN },
    { "a hint under the author's *", "<style>* { color: green }</style><font id=t color=red>x</font>", CSS_PROP_COLOR, GREEN },
    /* a sheet's syntax */
    { "comments", "<style>p { /* c */ color: /* x */ red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "<!-- and -->", "<style><!-- p { color: red } --></style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@font-face, skipped", "<style>@font-face { font-family: x } p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@import, skipped", "<style>@import 'x.css'; p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports, skipped whole", "<style>@supports (display: grid) { p { color: blue } } p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "a string holding a brace", "<style>p[title='}'] { color: red }</style><p id=t title='}'>x", CSS_PROP_COLOR, RED },
    /* the user agent's display */
    { "[hidden]", "<p id=t hidden>x", CSS_PROP_DISPLAY, "none" },
    { "li", "<ul><li id=t>x</ul>", CSS_PROP_DISPLAY, "list-item" },
    { "td", "<table><tr><td id=t>x</table>", CSS_PROP_DISPLAY, "table-cell" },
    { "span", "<span id=t>x</span>", CSS_PROP_DISPLAY, "inline" },
};

/* the element whose id is t */
static uint32_t find_t(const dom_document *d) {
    uint32_t id = names_find(d, "id", 2);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        for (uint32_t a = d->node[x].first_attr; a; a = d->attr[a].next)
            if (d->attr[a].name == id && d->attr[a].value.len == 1 && text_ptr(d, d->attr[a].value)[0] == 't') return x;
    }
    return 0;
}

int main(void) {
    int bad = 0, n = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        dom_document d;
        html_parser p;
        css_style st;
        dom_init(&d, 1);
        html_init(&p, 1);
        css_init(&st, 1);
        html_parse(&p, &d, cases[i].html, strlen(cases[i].html));
        css_cascade(&st, &d, 0);
        uint32_t x = find_t(&d);
        char got[128];
        snprintf(got, sizeof got, "(no element t)");
        uint32_t c0 = 0;                                     /* t's first value: its style's */
        for (uint32_t e = 1; e < st.estyle_len; e++) if (x && st.estyle[e].node.index == x) c0 = st.estyle[e].first;
        for (uint32_t c = c0; c0 && c < st.cval_len && st.cval[c].style == st.cval[c0].style; c++)
            if (st.cval[c].prop == cases[i].prop) {
                amb_span t = st.cval[c].text;
                snprintf(got, sizeof got, "%.*s", (int)t.len, text_ptr(&st, t));
            }
        int ok = strcmp(got, cases[i].want) == 0;
        if (!ok) bad++;
        n++;
        printf("%s %s: %s %s%s%s\n", ok ? "ok  " : "FAIL", cases[i].what, css_prop_name(cases[i].prop), got, ok ? "" : ", not ", ok ? "" : cases[i].want);
    }
    printf("%d of %d cases\n", n - bad, n);
    return bad ? 1 : 0;
}
