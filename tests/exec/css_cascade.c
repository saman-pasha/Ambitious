/* tests/exec/css_cascade.c -- the CSS module case by case: a page, the element
   whose id is t, a property, and the computed value it must have. Selectors,
   specificity, origin and importance, the style attribute, inheritance and the
   global keywords, units, colours, shorthands, invalid values, media queries,
   presentational hints and the syntax a sheet may hold. A case's fifth field, when
   it has one, names a pseudo-element of t whose value it is ("::before"; "(none)"
   when t has none), or a state the page is in before the cascade (":hover",
   ":focus" -- :focus-visible too, as the command's --focus has it -- ":active") at
   the element whose id is h, or t when there is none. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);

typedef struct tcase { const char *what; const char *html; css_prop prop; const char *want; const char *where; } tcase;

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
    { "[attr=v]: type's value without case (HTML)", "<style>[type='TEXT'] { color: red }</style><input id=t type=text>", CSS_PROP_COLOR, RED },
    { "[attr=v] case-sensitive", "<style>[title='X'] { color: red }</style><p id=t title=x>x", CSS_PROP_COLOR, BLACK },
    { "[attr=v s]", "<style>[type='TEXT' s] { color: red }</style><input id=t type=text>", CSS_PROP_COLOR, BLACK },
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
    { "#rrggbbaa", "<style>p { color: #ff000080 }</style><p id=t>x", CSS_PROP_COLOR, "rgba(255, 0, 0, 0.5)" },
    { "rgb() of percentages", "<style>p { color: rgb(100%, 50%, 0%) }</style><p id=t>x", CSS_PROP_COLOR, "rgb(255, 128, 0)" },
    { "rgba()", "<style>p { color: rgba(0, 0, 255, 0.5) }</style><p id=t>x", CSS_PROP_COLOR, "rgba(0, 0, 255, 0.5)" },
    { "rgb() with spaces and /", "<style>p { color: rgb(0 128 0 / 50%) }</style><p id=t>x", CSS_PROP_COLOR, "rgba(0, 128, 0, 0.5)" },
    { "hsl()", "<style>p { color: hsl(120, 100%, 25%) }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "a named colour", "<style>p { color: RebeccaPurple }</style><p id=t>x", CSS_PROP_COLOR, "rgb(102, 51, 153)" },
    { "transparent", "<style>p { background-color: transparent }</style><p id=t>x", CSS_PROP_BACKGROUND_COLOR, "rgba(0, 0, 0, 0)" },
    { "currentcolor", "<style>p { color: red; border-color: currentcolor }</style><p id=t>x", CSS_PROP_BORDER_TOP_COLOR, RED },
    { "border-color's initial is the colour", "<style>p { color: blue }</style><p id=t>x", CSS_PROP_BORDER_LEFT_COLOR, "rgb(0, 0, 255)" },
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
    { "border: its colour", "<style>p { border: 2px solid red }</style><p id=t>x", CSS_PROP_BORDER_RIGHT_COLOR, RED },
    { "border: its style", "<style>p { border: 2px dotted red }</style><p id=t>x", CSS_PROP_BORDER_BOTTOM_STYLE, "dotted" },
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
    /* calc(), min(), max(), clamp() */
    { "calc() of px", "<style>p { width: calc(100px + 20px) }</style><p id=t>x", CSS_PROP_WIDTH, "120px" },
    { "calc() of em and px", "<style>p { font-size: 20px; width: calc(2em + 10px) }</style><p id=t>x", CSS_PROP_WIDTH, "50px" },
    { "calc() of a percentage and px, kept", "<style>p { width: calc(50% - 10px) }</style><p id=t>x", CSS_PROP_WIDTH, "calc(50% - 10px)" },
    { "calc() with * and /", "<style>p { margin-top: calc(10px * 3 / 2) }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "15px" },
    { "calc() nested", "<style>p { padding-top: calc((4px + 6px) * 2) }</style><p id=t>x", CSS_PROP_PADDING_TOP, "20px" },
    { "calc() in font-size: the parent's em", "<style>div { font-size: 10px } p { font-size: calc(2em + 1px) }</style><div><p id=t>x</div>", CSS_PROP_FONT_SIZE, "21px" },
    { "min() of lengths", "<style>p { width: min(100px, 3em) }</style><p id=t>x", CSS_PROP_WIDTH, "48px" },
    { "max() over a percentage, kept", "<style>p { width: max(50%, 100px) }</style><p id=t>x", CSS_PROP_WIDTH, "max(50%, 100px)" },
    { "clamp() of vw", "<style>p { font-size: clamp(10px, 4vw, 20px) }</style><p id=t>x", CSS_PROP_FONT_SIZE, "20px" },
    { "calc() of a length and a number: dropped", "<style>p { width: 10px } p { width: calc(10px + 2) }</style><p id=t>x", CSS_PROP_WIDTH, "10px" },
    { "calc() a number, for line-height", "<style>p { line-height: calc(1 + 0.5) }</style><p id=t>x", CSS_PROP_LINE_HEIGHT, "1.5" },
    /* custom properties and var() */
    { "var()", "<style>:root { --c: red } p { color: var(--c) }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "var() of an inherited custom property", "<style>div { --c: red } p { color: var(--c) }</style><div><p id=t>x</div>", CSS_PROP_COLOR, RED },
    { "var()'s fallback", "<style>p { color: var(--missing, green) }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "var() in a shorthand", "<style>p { --m: 5px 10px; margin: var(--m) }</style><p id=t>x", CSS_PROP_MARGIN_LEFT, "10px" },
    { "var() of var()", "<style>p { --a: 3px; --b: var(--a); padding-left: var(--b) }</style><p id=t>x", CSS_PROP_PADDING_LEFT, "3px" },
    { "a cycle is invalid: the fallback", "<style>p { --a: var(--b); --b: var(--a); color: var(--a, green) }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "invalid at computed-value time, inherited", "<style>div { color: green } p { color: red; color: var(--none) }</style><div><p id=t>x</div>", CSS_PROP_COLOR, GREEN },
    { "invalid at computed-value time, initial", "<style>p { margin-top: 5px; margin-top: var(--none) }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "0px" },
    { "custom property names are case-sensitive", "<style>p { --C: red; color: var(--c, green) }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "var() in calc()", "<style>p { --w: 10px; width: calc(var(--w) * 3) }</style><p id=t>x", CSS_PROP_WIDTH, "30px" },
    { "var() in the style attribute", "<style>div { --s: 7px }</style><div><p id=t style='padding-top: var(--s)'>x</div>", CSS_PROP_PADDING_TOP, "7px" },
    /* rem: the root's font size */
    { "rem against the root's font size", "<style>html { font-size: 20px } p { margin-top: 2rem }</style><p id=t>x", CSS_PROP_MARGIN_TOP, "40px" },
    { "rem in the root's own font-size", "<html id=t><style>html { font-size: 2rem }</style><p>x", CSS_PROP_FONT_SIZE, "32px" },
    /* borders: a style and a colour a side, radii */
    { "border-style a side", "<style>p { border-style: solid dashed }</style><p id=t>x", CSS_PROP_BORDER_RIGHT_STYLE, "dashed" },
    { "border-color a side", "<style>p { border-color: red green blue }</style><p id=t>x", CSS_PROP_BORDER_BOTTOM_COLOR, "rgb(0, 0, 255)" },
    { "border-width without a style: 0", "<style>p { border-width: 4px }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "0px" },
    { "border-width with a style", "<style>p { border-width: 4px; border-style: solid }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "4px" },
    { "border-top-style hidden: 0", "<style>p { border: 3px solid; border-top-style: hidden }</style><p id=t>x", CSS_PROP_BORDER_TOP_WIDTH, "0px" },
    { "a border width, snapped", "<style>p { border: 2.7px solid }</style><p id=t>x", CSS_PROP_BORDER_LEFT_WIDTH, "2px" },
    { "border-radius", "<style>p { border-radius: 10px }</style><p id=t>x", CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS, "10px" },
    { "border-radius, elliptical", "<style>p { border-radius: 10px 5px / 20px }</style><p id=t>x", CSS_PROP_BORDER_TOP_RIGHT_RADIUS, "5px 20px" },
    { "border-radius, a percentage", "<style>p { border-radius: 50% }</style><p id=t>x", CSS_PROP_BORDER_TOP_LEFT_RADIUS, "50%" },
    { "outline", "<style>p { outline: 2px dashed red }</style><p id=t>x", CSS_PROP_OUTLINE_STYLE, "dashed" },
    { "outline-width without a style: kept", "<style>p { outline-width: 3px }</style><p id=t>x", CSS_PROP_OUTLINE_WIDTH, "3px" },
    /* positioning, floats, the box's other properties */
    { "position", "<style>p { position: absolute }</style><p id=t>x", CSS_PROP_POSITION, "absolute" },
    { "top in em", "<style>p { top: 2em }</style><p id=t>x", CSS_PROP_TOP, "32px" },
    { "inset", "<style>p { inset: 1px 2px }</style><p id=t>x", CSS_PROP_LEFT, "2px" },
    { "float", "<style>p { float: left }</style><p id=t>x", CSS_PROP_FLOAT, "left" },
    { "a float is a block", "<span id=t style='float: right'>x</span>", CSS_PROP_DISPLAY, "block" },
    { "an absolute box is a block", "<span id=t style='position: absolute; display: inline-flex'>x</span>", CSS_PROP_DISPLAY, "flex" },
    { "an absolute box floats not", "<style>p { position: absolute; float: left }</style><p id=t>x", CSS_PROP_FLOAT, "none" },
    { "clear", "<style>p { clear: both }</style><p id=t>x", CSS_PROP_CLEAR, "both" },
    { "z-index", "<style>p { z-index: 5 }</style><p id=t>x", CSS_PROP_Z_INDEX, "5" },
    { "z-index not an integer: dropped", "<style>p { z-index: 1.5 }</style><p id=t>x", CSS_PROP_Z_INDEX, "auto" },
    { "overflow: two values", "<style>p { overflow: hidden auto }</style><p id=t>x", CSS_PROP_OVERFLOW_Y, "auto" },
    { "overflow: one for both", "<style>p { overflow: clip }</style><p id=t>x", CSS_PROP_OVERFLOW_X, "clip" },
    { "vertical-align, a length", "<span id=t style='vertical-align: -2px'>x</span>", CSS_PROP_VERTICAL_ALIGN, "-2px" },
    { "vertical-align: sub (the UA's)", "<sub id=t>x</sub>", CSS_PROP_VERTICAL_ALIGN, "sub" },
    { "min-height", "<style>p { min-height: 10em }</style><p id=t>x", CSS_PROP_MIN_HEIGHT, "160px" },
    { "max-height: none", "<p id=t>x", CSS_PROP_MAX_HEIGHT, "none" },
    { "opacity, a percentage", "<style>p { opacity: 50% }</style><p id=t>x", CSS_PROP_OPACITY, "0.5" },
    { "opacity, clamped", "<style>p { opacity: 1.5 }</style><p id=t>x", CSS_PROP_OPACITY, "1" },
    { "visibility, inherited", "<style>div { visibility: hidden }</style><div><p id=t>x</div>", CSS_PROP_VISIBILITY, "hidden" },
    /* flexbox and grid */
    { "a flex item is a block", "<div style='display: flex'><span id=t>x</span></div>", CSS_PROP_DISPLAY, "block" },
    { "flex-flow", "<style>p { flex-flow: column wrap }</style><p id=t>x", CSS_PROP_FLEX_WRAP, "wrap" },
    { "flex: 1's basis", "<style>p { flex: 1 }</style><p id=t>x", CSS_PROP_FLEX_BASIS, "0%" },
    { "flex: none", "<style>p { flex: none }</style><p id=t>x", CSS_PROP_FLEX_SHRINK, "0" },
    { "flex: auto", "<style>p { flex: auto }</style><p id=t>x", CSS_PROP_FLEX_BASIS, "auto" },
    { "flex: grow shrink basis", "<style>p { flex: 2 3 10em }</style><p id=t>x", CSS_PROP_FLEX_BASIS, "160px" },
    { "order", "<style>p { order: -1 }</style><p id=t>x", CSS_PROP_ORDER, "-1" },
    { "justify-content", "<style>p { justify-content: space-between }</style><p id=t>x", CSS_PROP_JUSTIFY_CONTENT, "space-between" },
    { "align-self", "<style>p { align-self: center }</style><p id=t>x", CSS_PROP_ALIGN_SELF, "center" },
    { "gap: two values", "<style>p { gap: 10px 20px }</style><p id=t>x", CSS_PROP_COLUMN_GAP, "20px" },
    { "place-items", "<style>p { place-items: center start }</style><p id=t>x", CSS_PROP_JUSTIFY_ITEMS, "start" },
    { "grid-template-columns", "<style>p { grid-template-columns: 100px 1fr repeat(2, minmax(10px, auto)) }</style><p id=t>x",
      CSS_PROP_GRID_TEMPLATE_COLUMNS, "100px 1fr repeat(2, minmax(10px, auto))" },
    { "grid-template-columns: em made px", "<style>p { grid-template-columns: 2em [main] 1fr }</style><p id=t>x", CSS_PROP_GRID_TEMPLATE_COLUMNS, "32px [main] 1fr" },
    { "grid-template-columns: an unknown size, dropped", "<style>p { grid-template-columns: 10px blue }</style><p id=t>x", CSS_PROP_GRID_TEMPLATE_COLUMNS, "none" },
    { "grid-column", "<style>p { grid-column: 2 / span 3 }</style><p id=t>x", CSS_PROP_GRID_COLUMN_END, "span 3" },
    { "grid-area", "<style>p { grid-area: 1 / 2 / 3 / 4 }</style><p id=t>x", CSS_PROP_GRID_COLUMN_END, "4" },
    { "grid-auto-flow", "<style>p { grid-auto-flow: column dense }</style><p id=t>x", CSS_PROP_GRID_AUTO_FLOW, "column dense" },
    /* backgrounds, shadows, transforms */
    { "background-image: a gradient", "<style>p { background-image: linear-gradient(red, blue) }</style><p id=t>x", CSS_PROP_BACKGROUND_IMAGE, "linear-gradient(red, blue)" },
    { "background: layers' sizes", "<style>p { background: url(a.png) center / cover no-repeat, #ff0 }</style><p id=t>x", CSS_PROP_BACKGROUND_SIZE, "cover, auto" },
    { "background: layers' repeats", "<style>p { background: url(a.png) center / cover no-repeat, #ff0 }</style><p id=t>x", CSS_PROP_BACKGROUND_REPEAT, "no-repeat, repeat" },
    { "background: the last layer's colour", "<style>p { background: url(a.png) center / cover no-repeat, #ff0 }</style><p id=t>x", CSS_PROP_BACKGROUND_COLOR, "rgb(255, 255, 0)" },
    { "box-shadow: em made px", "<style>p { box-shadow: 0 0 1em red }</style><p id=t>x", CSS_PROP_BOX_SHADOW, "0 0 16px red" },
    { "text-shadow, inherited", "<style>div { text-shadow: 1px 1px 2px black }</style><div><p id=t>x</div>", CSS_PROP_TEXT_SHADOW, "1px 1px 2px black" },
    { "transform", "<style>p { transform: rotate(45deg) translateX(1em) }</style><p id=t>x", CSS_PROP_TRANSFORM, "rotate(45deg) translateX(16px)" },
    { "transform: an unknown function, dropped", "<style>p { transform: spin(1turn) }</style><p id=t>x", CSS_PROP_TRANSFORM, "none" },
    /* text */
    { "letter-spacing in em, inherited", "<style>div { letter-spacing: 0.1em }</style><div><p id=t>x</div>", CSS_PROP_LETTER_SPACING, "1.6px" },
    { "text-indent, a percentage", "<style>p { text-indent: 10% }</style><p id=t>x", CSS_PROP_TEXT_INDENT, "10%" },
    { "text-transform", "<style>p { text-transform: uppercase }</style><p id=t>x", CSS_PROP_TEXT_TRANSFORM, "uppercase" },
    { "direction, inherited", "<style>div { direction: rtl }</style><div><p id=t>x</div>", CSS_PROP_DIRECTION, "rtl" },
    { "unicode-bidi, not inherited", "<style>div { unicode-bidi: embed }</style><div><span id=t>x</span></div>", CSS_PROP_UNICODE_BIDI, "normal" },
    { "unicode-bidi: the UA's isolate", "<p id=t>x", CSS_PROP_UNICODE_BIDI, "isolate" },
    { "writing-mode", "<style>p { writing-mode: vertical-rl }</style><p id=t>x", CSS_PROP_WRITING_MODE, "vertical-rl" },
    { "hyphens", "<style>p { hyphens: auto }</style><p id=t>x", CSS_PROP_HYPHENS, "auto" },
    { "content", "<style>p { content: \"a\" counter(c) }</style><p id=t>x", CSS_PROP_CONTENT, "\"a\" counter(c)" },
    { "counter-reset", "<style>p { counter-reset: c 2 d }</style><p id=t>x", CSS_PROP_COUNTER_RESET, "c 2 d 0" },
    { "quotes, an odd count: dropped", "<style>p { quotes: '<' }</style><p id=t>x", CSS_PROP_QUOTES, "auto" },
    { "list-style-position", "<style>ul { list-style: inside square }</style><ul id=t><li>x</ul>", CSS_PROP_LIST_STYLE_POSITION, "inside" },
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
    { "@font-face, no rules of its own", "<style>@font-face { font-family: x } p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@import with no loader: read past", "<style>@import 'x.css'; p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports, then a later rule", "<style>@supports (display: grid) { p { color: blue } } p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    /* @supports: a declaration's test, not, and, or, selector() */
    { "@supports a declaration read", "<style>@supports (display: grid) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports a value not read", "<style>@supports (display: grid-lanes) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@supports a property not read", "<style>@supports (frobnicate: 1) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@supports not", "<style>@supports not (display: grid-lanes) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports and", "<style>@supports (display: flex) and (gap: 1px) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports and, one false", "<style>@supports (display: flex) and (gap: 1parsec) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@supports or", "<style>@supports (display: nope) or (float: left) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports and with or, unparenthesized: false", "<style>@supports (float: left) and (color: red) or (top: 0) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { "@supports nested", "<style>@supports ((float: left) or (x: y)) and (not (display: nope)) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports a custom property", "<style>@supports (--anything: at all) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports selector()", "<style>@supports selector(:is(p, h1) > a) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@supports selector() not read", "<style>@supports selector(p:frobbed) { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    /* @layer: the layers' order, the unlayered last, !important reversed */
    { "@layer: the later layer", "<style>@layer base, theme; @layer theme { p { color: red } } @layer base { p { color: green } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@layer: unlayered over layered, specificity aside", "<style>@layer a { #t { color: green } } p { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@layer: !important, the earlier layer", "<style>@layer a { p { color: green !important } } @layer b { p { color: red !important } }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "@layer: !important layered over !important unlayered", "<style>p { color: red !important } @layer a { p { color: green !important } }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "@layer: a layer's own rules after its sublayers'", "<style>@layer a { @layer b { p { color: green } } p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@layer: a dotted name is a sublayer", "<style>@layer a { p { color: red } } @layer a.b { p { color: green } }</style><p id=t>x", CSS_PROP_COLOR, RED },
    { "@layer: the order its statement gives", "<style>@layer b, a; @layer a { p { color: green } } @layer b { p { color: red } }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "@layer: anonymous layers in order", "<style>@layer { p { color: red } } @layer { p { color: green } }</style><p id=t>x", CSS_PROP_COLOR, GREEN },
    { "a string holding a brace", "<style>p[title='}'] { color: red }</style><p id=t title='}'>x", CSS_PROP_COLOR, RED },
    /* :has(): relative selectors, the subject's descendants or later siblings */
    { ":has() a child", "<style>div:has(> p) { color: red }</style><div id=t><p>x</div>", CSS_PROP_COLOR, RED },
    { ":has(>) not a grandchild", "<style>div:has(> p) { color: red }</style><div id=t><section><p>x</section></div>", CSS_PROP_COLOR, BLACK },
    { ":has() a descendant", "<style>div:has(p) { color: red }</style><div id=t><section><p>x</section></div>", CSS_PROP_COLOR, RED },
    { ":has(+) the next sibling", "<style>h1:has(+ p) { color: red }</style><h1 id=t>a</h1><p>x", CSS_PROP_COLOR, RED },
    { ":has(+) not a later one", "<style>h1:has(+ p) { color: red }</style><h1 id=t>a</h1><div></div><p>x", CSS_PROP_COLOR, BLACK },
    { ":has(~) a later sibling", "<style>h1:has(~ p) { color: red }</style><h1 id=t>a</h1><div></div><p>x", CSS_PROP_COLOR, RED },
    { ":has() complex", "<style>div:has(> section p.k) { color: red }</style><div id=t><section><p class=k>x</section></div>", CSS_PROP_COLOR, RED },
    { ":has() a list", "<style>div:has(em, i) { color: red }</style><div id=t><i>x</i></div>", CSS_PROP_COLOR, RED },
    { ":not(:has())", "<style>div:not(:has(p)) { color: red }</style><div id=t><span>x</span></div>", CSS_PROP_COLOR, RED },
    { ":has() its argument's specificity", "<style>div:has(#x) { color: red } div.a { color: green }</style><div id=t class=a><p id=x>y</div>", CSS_PROP_COLOR, RED },
    /* the pseudo-classes of state: what the page's state is (a page at rest has none) */
    { ":hover", "<style>p:hover { color: red }</style><p id=t>x", CSS_PROP_COLOR, RED, ":hover" },
    { ":hover at rest", "<style>p:hover { color: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    { ":hover holds of the hovered's ancestors", "<style>div:hover { color: red }</style><div id=t><p id=h>x</div>", CSS_PROP_COLOR, RED, ":hover" },
    { ":hover not of its descendants", "<style>p:hover { color: red }</style><div id=h><p id=t>x</div>", CSS_PROP_COLOR, BLACK, ":hover" },
    { ":focus", "<style>input:focus { color: red }</style><input id=t>", CSS_PROP_COLOR, RED, ":focus" },
    { ":focus-visible", "<style>input:focus-visible { color: red }</style><input id=t>", CSS_PROP_COLOR, RED, ":focus" },
    { ":focus not of an ancestor", "<style>form:focus { color: red }</style><form id=t><input id=h></form>", CSS_PROP_COLOR, BLACK, ":focus" },
    { ":focus-within", "<style>form:focus-within { color: red }</style><form id=t><input id=h></form>", CSS_PROP_COLOR, RED, ":focus" },
    { ":active", "<style>a:active { color: red }</style><a id=t href=#x>x</a>", CSS_PROP_COLOR, RED, ":active" },
    { ":visited: no history is read", "<style>a:visited { color: red }</style><a id=t href=#x>x</a>", CSS_PROP_COLOR, "rgb(0, 0, 238)" },
    { ":target with no URL", "<style>p:target { color: red }</style><p id=t>x", CSS_PROP_COLOR, BLACK },
    /* pseudo-elements: a style under the element's */
    { "a ::before rule is not its element's", "<style>p::before { content: 'x'; color: red }</style><p id=t>y", CSS_PROP_COLOR, BLACK },
    { "::before's own value", "<style>p::before { content: 'x'; color: green }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::before" },
    { "::before inherits from its element", "<style>p { color: red } p::before { content: 'x' }</style><p id=t>y", CSS_PROP_COLOR, RED, "::before" },
    { "::before's content", "<style>p::before { content: 'x' counter(c) }</style><p id=t>y", CSS_PROP_CONTENT, "'x' counter(c)", "::before" },
    { "::before with no content: none", "<style>p::before { color: green }</style><p id=t>y", CSS_PROP_COLOR, "(none)", "::before" },
    { ":before, as CSS 2 wrote it", "<style>p:before { content: 'x'; color: green }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::before" },
    { "::after", "<style>p::after { content: 'x'; color: green }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::after" },
    { "::before of any element", "<style>::before { content: ''; color: green }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::before" },
    { "::before's specificity", "<style>#t::before { content: 'x'; color: green } p::before { color: red }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::before" },
    { "::before's display, not blockified", "<style>p::before { content: 'x' }</style><p id=t>y", CSS_PROP_DISPLAY, "inline", "::before" },
    { "q's quotes (the user agent's)", "<q id=t>y</q>", CSS_PROP_CONTENT, "open-quote", "::before" },
    { "::first-line", "<style>p::first-line { color: green }</style><p id=t>y", CSS_PROP_COLOR, GREEN, "::first-line" },
    { "::first-letter's em", "<style>p::first-letter { font-size: 2em }</style><p id=t>y", CSS_PROP_FONT_SIZE, "32px", "::first-letter" },
    { "::marker", "<style>li::marker { color: green }</style><ul><li id=t>y</ul>", CSS_PROP_COLOR, GREEN, "::marker" },
    { "::placeholder (the user agent's)", "<input id=t placeholder=x>", CSS_PROP_COLOR, "rgb(117, 117, 117)", "::placeholder" },
    { "::placeholder", "<style>::placeholder { color: green }</style><input id=t placeholder=x>", CSS_PROP_COLOR, GREEN, "::placeholder" },
    { "::selection: none read", "<style>p::selection { color: red }</style><p id=t>y", CSS_PROP_COLOR, BLACK },
    /* the user agent's display */
    { "[hidden]", "<p id=t hidden>x", CSS_PROP_DISPLAY, "none" },
    { "li", "<ul><li id=t>x</ul>", CSS_PROP_DISPLAY, "list-item" },
    { "td", "<table><tr><td id=t>x</table>", CSS_PROP_DISPLAY, "table-cell" },
    { "span", "<span id=t>x</span>", CSS_PROP_DISPLAY, "inline" },
};

/* the element whose id is the letter c */
static uint32_t find_id(const dom_document *d, char c) {
    uint32_t id = names_find(d, "id", 2);
    for (uint32_t x = 1; x < d->node_len; x++) {
        if (!d->node[x].live) continue;
        for (uint32_t a = d->node[x].first_attr; a; a = d->attr[a].next)
            if (d->attr[a].name == id && d->attr[a].value.len == 1 && text_ptr(d, d->attr[a].value)[0] == c) return x;
    }
    return 0;
}

/* the pseudo-element a case's where names, CSS_PSEUDO_ELEMENT for none */
static css_pseudo pseudo_of(const char *w) {
    static const char *const names[7] = { "", "::before", "::after", "::first-line", "::first-letter", "::marker", "::placeholder" };
    for (int k = 1; k < 7; k++) if (w && strcmp(w, names[k]) == 0) return (css_pseudo)k;
    return CSS_PSEUDO_ELEMENT;
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
        uint32_t x = find_id(&d, 't'), h = find_id(&d, 'h');
        if (cases[i].where && cases[i].where[0] == ':' && cases[i].where[1] != ':') {
            /* the state the case puts the page in, at h (or t) */
            uint32_t at_el = h ? h : x;
            if (strcmp(cases[i].where, ":hover") == 0) css_set_state(&st, &d, CSS_STATE_KIND_HOVER, at_el);
            else if (strcmp(cases[i].where, ":active") == 0) css_set_state(&st, &d, CSS_STATE_KIND_ACTIVE, at_el);
            else {
                css_set_state(&st, &d, CSS_STATE_KIND_FOCUS, at_el);
                css_set_state(&st, &d, CSS_STATE_KIND_FOCUS_VISIBLE, at_el);
            }
        }
        css_cascade(&st, &d, 0);
        char got[128];
        snprintf(got, sizeof got, "(no element t)");
        uint32_t c0 = 0;                                     /* t's first value: its style's */
        css_pseudo pk = pseudo_of(cases[i].where);
        if (pk == CSS_PSEUDO_ELEMENT) {
            for (uint32_t e = 1; e < st.estyle_len; e++) if (x && st.estyle[e].node.index == x) c0 = st.estyle[e].first;
        } else {
            snprintf(got, sizeof got, "(none)");
            for (uint32_t e = 1; e < st.pstyle_len; e++) if (x && st.pstyle[e].node.index == x && st.pstyle[e].pseudo == pk) c0 = st.pstyle[e].first;
        }
        for (uint32_t c = c0; c0 && c < st.cval_len && st.cval[c].style == st.cval[c0].style; c++)
            if (st.cval[c].prop == cases[i].prop) {
                amb_span t = st.cval[c].text;
                snprintf(got, sizeof got, "%.*s", (int)t.len, text_ptr(&st, t));
            }
        int ok = strcmp(got, cases[i].want) == 0;
        if (!ok) bad++;
        n++;
        printf("%s %s: %s%s%s %s%s%s\n", ok ? "ok  " : "FAIL", cases[i].what, pk ? cases[i].where : "", pk ? " " : "", css_prop_name(cases[i].prop), got,
               ok ? "" : ", not ",
               ok ? "" : cases[i].want);
    }
    printf("%d of %d cases\n", n - bad, n);
    return bad ? 1 : 0;
}
