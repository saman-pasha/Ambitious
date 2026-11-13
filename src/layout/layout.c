/* src/layout/layout.c -- the layout module (contracts/layout.pl): the box tree of
   a styled document, laid out; and the module's generated implementation.

   Boxes, over the flat tree (src/dom/dom.h): a shadow host's children are its shadow
   root's, a slot's its assigned nodes or else its fallback, and a slot is display: contents
   (the user agent's sheet); a table's rows and cells and a list item's ordinal are read from
   the DOM's children. The root element's box is the viewport box's child. An element whose
   display is block, list-item, table, flex, grid or flow-root makes a
   block-level box. An inline element makes no box: its text and
   its inline children are laid out in its block container's lines, its margins,
   borders and paddings as space at its two ends, and its background drawn
   behind its part of each line. An inline element that holds a block is
   flattened into its container. inline-block, img, svg, input, button, textarea and
   select make atomic inline boxes; an img or an svg made a block (display: block, a
   float, a positioned box) is still a replaced box, sized as an inline one, its content
   none of the flow's. An svg is sized as Chromium sizes one: its width and height
   attributes its CSS width and height (the cascade's presentation attributes), its
   natural size their absolute lengths, its ratio its viewBox's; its subtree is made the
   box's scene (src/paint/svg.h) once the box's size is known, as are the SVG filters, clipPath
   and mask a box's filter, clip-path and mask name by url(), for its border box (its fx). A
   container with both block-level and
   inline-level children wraps each run of the inline ones that shows anything
   in an anonymous block.

   Block layout (CSS 2.1, 10.3.3 and 8.3.1). The width fills the containing
   block, or is as declared (min-width and max-width holding it), with auto
   margins taking what is left (centring when both are auto). The height is the
   content's, or as declared (a percentage of a containing block's definite
   height; the root's is the viewport's), min-height and max-height holding it.
   Vertical margins collapse: between siblings; through a parent that has no
   border or padding on that side (and is no new formatting context: the root, a
   cell, an inline-block, a float, a positioned box, a flex or grid item or
   container, an overflow other than visible -- not the body's, when the root's
   is visible: it is the viewport's); and through an empty box. A min-width of
   min-content (the user agent's on a fieldset) is the content's.

   Fieldsets (HTML 15.3.13). The rendered legend is shrunk to fit and set on the
   top border, its auto margins aligning it (right to left, from the right); a
   legend wider than the content box overflows the border box from its start, and
   the fieldset's min- and max-content widths take the legend's.

   Floats (9.5). A float is laid out shrunk to fit (unless its width is
   declared) and placed as high as the flow has reached -- a float between
   blocks at the next one's top, a float met in a line at the line's top when it
   fits beside what the line holds, else below the line -- and no higher than an
   earlier float, to its side as far as the floats there leave room, lower where
   they leave too little. Lines are shortened beside the floats of their block
   formatting context (moved down when not even their first word fits); a block
   that makes a formatting context of its own is narrowed beside them; clear
   moves a block below them. A formatting context's root holds its floats in its
   auto height. Its float list is in its own coordinates; the offset of each
   block's content box in it is found as its margins will collapse, as far as
   can be seen before the block is laid out.

   Positioning (9.3, 9.4.3, 10.3.7, 10.6.4). A relatively positioned box is laid
   out where the flow puts it and moved by its offsets (an inline element's
   fragments are). An absolutely positioned or fixed element leaves the flow:
   the flow keeps its static position, and after it each is laid out in its
   containing block -- the padding box of its nearest positioned ancestor's box,
   or the viewport (800 x 600) for fixed or for none -- its width from its
   offsets or shrunk to fit, auto offsets at the static position, and its box is
   that block's last child. When the nearest positioned ancestor is an inline element
   (10.1, 4), the containing block is the box from its first fragment's padding box's
   start and top to its last one's end and bottom, as Chromium makes it. A sticky box is where it is at rest, unscrolled (CSS
   Position 3, 3.4): where an inset it has puts that edge of its scrollport (the
   nearest scroll container's padding box, else the viewport) inset by its offsets
   past its border box, it is moved inward, as far as its margin box stays in its
   containing block (top and left winning), once the tree has its page coordinates;
   a sticky inline element likewise, its box its fragments' union, its containing
   block the content box of the block whose lines hold it (an anonymous one's
   parent), its fragments, text and atomic boxes moved. z-index and overflow are
   kept for paint.

   Flex containers (Flexbox 1, 9): the items in order (order, then the tree's),
   their flex base sizes (flex-basis, a declared size, or the content's), min and
   max main sizes (an auto min the content's min-content), lines when they wrap,
   the flexible lengths resolved by freezing the items clamped, the items laid
   out at their main sizes, the lines' cross sizes (stretched, align-content),
   justify-content and auto margins along the main axis, align-items and
   align-self across: baseline (first baseline) aligns a row's items on their first
   baselines, each line as tall as the most they rise above it and fall below it
   (9.4, 8); last baseline on their last ones, flush to the line's end; an item with
   no line its border box's bottom; in a column, baseline is start and last baseline
   the items' end edges shared. A flex container's first baseline (8.5) is its top
   line's baseline items', else that line's leftmost (topmost) item's; its last
   baseline its bottom line's last-baseline items', else that line's rightmost
   (bottommost) item's -- in the order the lines and items are drawn, as Chromium
   finds them; an inline-block's baseline is its last line's in its flow, however
   deep (a flex or grid container in it giving its first baseline, unless its
   last is its items' aligned on their last baselines), unless its overflow is not
   visible.

   Grid containers (Grid 1): the track lists read (px, %, fr,
   auto, min-content, max-content, minmax(), fit-content(), repeat() with a count
   or auto-fill, the lines' names), grid-template-areas (each area's -start and -end
   lines named); the items' lines resolved (8.3: by number, by name and its nth
   line, a span of lines or of a name's lines, an area's name, a name no line has
   taken by the implicit lines), the rest placed by the auto-placement cursor (8.5:
   along rows or columns, sparse or dense), implicit tracks before the explicit
   grid as well as after it (grid-auto-rows and -columns' pattern); the tracks
   sized (11.4 to 11.8: base sizes and growth limits from the items that span one
   track, then from the others by their spans and those that cross an fr track,
   the free space to the growth limits, the fr tracks' share of what is left, of a
   size not definite too, auto tracks stretched), the columns first, the items
   laid out in them, then the rows from their heights (a percentage row of an auto
   height resolved against it); justify-self, align-self, justify-content and
   align-content. A grid item's box says where it was placed and how (a named
   area, its lines, automatically). An auto-fit repeat's empty tracks collapse,
   the gutters beside them too, and an automatic repeat counts what the other
   tracks leave it. Align-items and align-self baseline and last baseline: a row's
   items on their first baselines that start in it share one at its start, those on
   their last that end in it one at its end, each item's contribution to its rows
   taking what that shifts it by; justify-items and -self baseline fall back to
   start, last baseline to end. A grid container's first and last baselines (10.1):
   its first (last) row's baseline-aligned items', else its first (last) item there
   in grid order. A grid container's own min- and max-content widths: its items
   placed as its layout places them, its columns sized by the same algorithm under
   a min-content and then a max-content constraint, its width indefinite, as
   Chromium sizes them (taking the items' contributions as a layout does, not the
   limited contributions the spec names for those constraints). An item's minimum
   contribution is Chromium's: of a width of its own its min-content contribution,
   of a min-width that, of a scroll container or a span across a flexible track or
   across no track of an auto minimum its edges, else its min-content contribution
   held to the fixed maximums of the tracks it spans; min-width and max-width hold
   its contributions and its stretched width. Not done: subgrid, masonry.

   Inline layout. White space is collapsed or kept as white-space says. Lines
   break at spaces (and around atomic inlines), never inside a word, a word too
   wide for its line overflowing it. A line's height comes from its strut and its
   pieces' line heights about one baseline, each piece shifted by its
   vertical-align as Chromium shifts it (sub, super, text-top, text-bottom,
   middle, a length, a percentage of the line height; top and bottom against
   the line box); it is aligned left, right, centre or justified (start and end
   as the direction has them). A word too wide for what is left of its line is
   broken by a hyphen (hyphens: manual at its soft hyphens, which are otherwise
   not drawn; auto there, else at the points Liang's algorithm finds with TeX's
   American English patterns, tools/gen-hyph.py, two letters at least each
   side), at the last point whose part fits. Bidirectional text (UAX #9, Unicode
   15's data from tools/gen-bidi.pl), when a paragraph is right to left, holds a
   right-to-left letter, an Arabic digit or a bidi control, or embeds right to left:
   the paragraph's code points, with the controls each inline element's
   unicode-bidi adds (embed, isolate, bidi-override, isolate-override, plaintext;
   the dir attribute's isolation, dir=auto and bdi as first strong isolates), and
   those in its text, resolved character by character (X1 to X10, W1 to W7, N0's
   bracket pairs, N1, N2, I1, I2), its level its direction's, or its first strong
   character's under plaintext or dir=auto (start and end aligned by it). Each line
   is reordered (L1, L2): its words split where their level changes, an inline
   element's edges going with what they bound (both edges on a line keep its left
   and right edges on its left and right), a right-to-left run's text handed to
   paint reversed, its brackets mirrored (L4); the controls are drawn as nothing.
   An inline box broken over lines is one strip sliced at its breaks, or, under
   box-decoration-break: clone, each fragment wrapped in its every edge (CSS
   Fragmentation 3, 5.4): a line it continues on starts with its start edges, as
   text-indent would, and its fragment there ends with its end edges past what the
   line holds, which the line's break does not make room for, as Chromium breaks it.
   Not done: each paragraph a forced break starts its own under plaintext; an
   inline box's edges on lines after its first in a right-to-left paragraph, and a
   cloned box's in any paragraph the bidi algorithm lays out (sliced there).

   Query containers (CSS Conditional 5): a container of container-type size or
   inline-size makes a formatting context of its own; its content counts for
   nothing in its inline size (inline-size containment: shrunk to fit, it is as
   wide as its edges), and a size container's auto height is as if it held
   nothing. Once its content box is sized (lay_block), before what it holds is
   laid out, the cascade makes its subtree's styles again with that size known
   (lay_cq_restyle, css_container_restyle): the run's tables take the changed
   nodes' styles and pseudo-elements, and its generated content is made again
   from the counters the walk entered it with (a container's counters are its
   subtree's own: style containment). No subtree is laid out twice for it.

   Pseudo-elements. An element's ::before and ::after are its first and last
   children, boxes as their display has them (inline ones in the lines), their
   content's text made in tree order before layout: strings, attr(), counter()
   and counters() (counter-reset and counter-increment, each counter's scope
   its element's, its descendants' and its later siblings', CSS 2.1 12.4.1),
   and the quotes (open-quote, close-quote: the quotes property's, or English's
   for auto); each counter written by its counter style (src/css: css_counter_text).
   A list item's marker is its counter style's representation of its ordinal with
   the style's prefix and suffix, outside ending at the content box, as Chromium
   places a marker of text, its right-to-left letters in their order; a disc,
   circle, square or disclosure triangle (or a style extending one) a shape, as
   before; a string as written. An outside marker is set by the first line in the
   list item's content, however deep, or a nested list's, or by a flex or grid
   container's first baseline met first (by a line in a box that clips, or by such
   a baseline, in a line of the list item's own there, unclipped); with no line,
   on a line of its own at the content box's top, the list item at least as tall. An
   inside marker begins the list item's first line, or, when the first in-flow box
   is a block, takes a line of its own before it (Chromium's inline marker box in
   an anonymous block of its own). ::first-letter is its block's first letter (the punctuation before
   it too) in its own style; ::first-line the first line's pieces in its style
   where they have their block's, the line broken in it; ::marker the list
   marker's style; ::placeholder an empty field's placeholder text. Not done: a
   counter() in a disclosure style is its triangle's character (Chromium draws
   the triangle); a list item's ordinal is its place among its list's items
   from start and value (not reversed, nor list-item's counter-reset or
   counter-increment); a ::marker's content is not read.

   Tables (CSS 2.1, 17; CSS Tables 3), as Blink's TablesNG lays them out, in its
   LayoutUnits. The parts grouped: captions, col and colgroup elements, the row
   groups (the first head first, the first foot last; rows directly in the table an
   anonymous group). Borders separated, border-spacing between the cells and about
   them inside the padding; or collapsed (17.6.2): each edge's border by Blink's
   conflict resolution -- hidden, then the wider, then the style, then the cell over
   its row, row group, column, column group and table, then the earlier --, half of it
   in each cell beside it, the table's border half its widest outer edges, its padding
   none, the edges drawn by paint as Blink joins them (cborders). The columns'
   constraints from the col elements' widths and the cells' min- and max-content widths,
   widths, min-widths, max-widths and percentages (the fixed layout: the first row's
   alone), the spanning cells shared out, a column no cell begins in merged; the
   table's width (its own, or fit-content between its grid's min- and max-content
   widths, at least its captions' min-content) shared among them by CSS Tables 3's
   guesses (the fixed layout: the fixed columns, then the percentages, then the rest
   evenly). The rows as tall as their cells and heights ask, a rowspan's lack and the
   table's height shared out as Blink shares them; captions at the table's border-box
   width above or below its border box (caption-side); a cell empty-cells: hide leaves
   empty without its background and borders (cell_hidden: its row's, row group's and
   columns' backgrounds are drawn in it); a cell's content where its vertical-align puts
   it (its row's or row group's when it has none, the middle when none has). Not done:
   right to left (columns from the left), visibility: collapse, baseline alignment in
   rows, percentage heights of rows and row groups.

   Quirks mode (the document's mode, which the HTML parser keeps on the document; the Quirks
   Mode Standard as Blink has it, measured): the line height quirk (in limited-quirks mode
   too) -- a block's strut counts on a line with text of its own, an inline box's on a line
   with its text or an edge's border or padding, a list item's on each of its own lines --;
   Blink's quirky margins, the user agent's of a p, a heading, a list, a blockquote or a pre,
   left out at a quirky container's (the body's, a cell's) start and end; the html element
   and the body filling the viewport, and a percentage height taken through blocks of an
   auto height up to them; a cell's height its border box's; a nowrap cell of a length's
   width at least that wide; no wrap around an img in a cell's min-content; an empty
   table's height none; a broken image of a declared size a box of it, its alt text in it.
   The percentage heights of what a line holds -- an inline-block's, a float's, an inline
   image's -- are taken through those blocks too (an inline-block of an auto height passing it
   on to what it holds); a list item whose first child is a block-level ul or ol has its
   outside marker on a line of its own, as tall as its line (Blink's NeedsOccupyWholeLine); a
   body that fills keeps its last child's bottom margin through its bottom though a
   min-height holds it taller than its content.

   Writing modes (CSS Writing Modes 4: horizontal-tb, vertical-rl, vertical-lr, sideways-rl, sideways-lr): a box is
   laid out in its writing mode's frame, as a horizontal box is with its axes turned (lay_frame), its style read
   turned and its boxes, lines and fragments turned back at the end; a box whose mode is not its parent's is a
   formatting context of its own laid out in its frame and set in its parent's (lay_wm_root), an orthogonal one's
   available inline size its parent's definite block size or the viewport's (7.3); the root's mode (its body's, the
   principal one) the initial containing block's. A vertical line's baseline is the central one (the sideways modes'
   the alphabetic one); its text set upright or turned as text-orientation and UAX #50 have it (src/paint/font.c), a
   text node of text-combine-upright: all one piece an em down the line, set across it (lay_chars; paint's pt_ctext).
   Not done: a vertical form control (sized as a horizontal one).

   Coordinates are relative to the parent box's content box while the tree is laid out, and made page coordinates at
   the end. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <string.pl>
#include <amb.pl>
#include "../paint/font.h"

amb_interface(dom);
amb_interface(css);
amb_interface(net);
amb_implementation(layout);
#include "../css/css.h"
#include "layout.h"
#include "hyph_data.h"
#include "bidi_data.h"
#include "linebreak.h"
#include "../net/url.h"
#include "../paint/svg.h"
#include "../paint/image.h"
#include "../dom/dom.h"

/* the run's context, as parameters: a struct may hold no borrow. sx: each node's style
   (0 for none); sc: each style as layout reads it, made once a run. The cascade's store and
   the tables of styles change in a run where a query container is laid out: its subtree is
   restyled (lay_cq_restyle), new styles added, nodes' styles and pseudo-elements' replaced */
#define LCTX layout_tree *t, const dom_document *d, css_style *st, const net_loader *nt, const amb_fonts *f, vector<uint32_t> *sx, \
    vector<lay_style> *sc, vector<lay_gen> *gx, vector<lay_ext> *xc, vector<lay_abs> *ab, vector<lay_flt> *fl
#define LARGS t, d, st, nt, f, sx, sc, gx, xc, ab, fl
/* the context with another block formatting context's floats */
#define LARGS_FL(x) t, d, st, nt, f, sx, sc, gx, xc, ab, (x)

/* what a line's content may reach past its width and still fit it: a LayoutUnit, a 64th of a px (Blink's
   LineBreaker::AvailableWidthToFit, the width's AddEpsilon; measured: break-all's "nt", 13.34375px, fits a line of
   13.328125px), and a float's rounding */
#define LAY_FIT (1.0 / 64 + 1e-7)

/* the document's mode, for the run (layout_run sets them from the document node's mode): in
   quirks mode, lay_quirks; in quirks or limited-quirks mode, lay_lhq (the line height quirk) */
static int lay_quirks, lay_lhq;
/* the definite height the percentages of the inline content being laid out take: its block container's children's
   (lay_flow and an anonymous block set it around their lines; -1 for none) -- an inline-block's, a float's met in a
   line, an inline image's percentage height (in quirks mode taken through blocks of an auto height) */
static double lay_line_pch = -1;

/* ---- the computed style of an element, as layout reads it ---- */

/* a length: px, a percentage, auto, px plus a percentage (a calc()), a calc() whose
   text (the value cv's) is reckoned against the percentages' basis; or a size its content
   gives (min-content, max-content, fit-content) */
enum { LAY_PX, LAY_PCT, LAY_AUTO, LAY_CALC, LAY_EXPR, LAY_MINC, LAY_MAXC, LAY_FITC };
typedef struct lay_len { int unit; double v, pct; uint32_t cv; } lay_len;

enum { D_NONE, D_INLINE, D_BLOCK, D_LIST_ITEM, D_INLINE_BLOCK, D_TABLE, D_ROW_GROUP, D_ROW, D_CELL, D_CAPTION, D_CONTENTS };
enum { WS_NORMAL, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE };
enum { AL_LEFT, AL_RIGHT, AL_CENTER, AL_JUSTIFY };

/* text-transform */
enum { TT_NONE, TT_UPPER, TT_LOWER, TT_CAPITALIZE, TT_MATH_AUTO };
/* with capitalize: the piece goes on with a word a piece before it began (the rest of a word hyphenation or
   ::first-letter breaks), whose first letter is no word's */
enum { TT_MID = 0x10 };

typedef struct lay_style {
    int display, bfc;
    uint32_t color, fill, bg, bc[4];
    /* the writing mode (CSS_WM_*), whose frame (lay_frame) the edges and sizes below are in: a vertical style's are
       its box's turned, width its inline size (height), m[0] its block-start margin, ...; orient, a vertical text's:
       1 mixed, 2 upright (their baseline the central one: asc and desc its halves, Blink's kCentralBaseline: LAY_VC),
       3 sideways (every glyph turned, on the alphabetic baseline), 4 combined (text-combine-upright: all, in
       vertical-rl or vertical-lr: each text node set across the line in an em, on the baseline its orientation
       gives). hidden: its visibility hidden or collapse, which paint draws none of (the fragments' hidden). (Here, in
       the padding before fs, a style is no
       bigger for them; at its end, the bigger style's copies, which layout makes often, became calls to memcpy, and
       each field here makes each copy longer: measured) */
    uint8_t wm, orient, hidden;
    double fs, lh, asc, desc;
    int face, italic, align, ws, deco, list, border_box, tt, rtl;
    int list_inside;                /* list-style-position: inside */
    uint32_t lcv;                   /* list-style-type's value: the counter style's name, or the string (list 4) */
    int lhn;                        /* line-height is normal */
    uint32_t font;                  /* a web font's resource (the loader's), 0 for a built-in face */
    double ls, wsp;                 /* letter-spacing, word-spacing: px */
    uint8_t bs[4];
    lay_len m[4], p[4], width, height, maxw, indent;
    double b[4];
} lay_style;

/* ---- pseudo-elements, as layout reads them ---- */

/* a pseudo-element of an element (its css_pseudo: ::before 1, ::after 2, ::first-line,
   ::first-letter, ::marker, ::placeholder), its style, and a ::before's or an ::after's
   content as text, in the tree's pool */
typedef struct lay_gen { uint32_t el, pseudo, style; amb_span text; } lay_gen;

/* an item of a run that is no node: a ::before or an ::after (LAY_GEN and its lay_gen's
   index), or its text, the pseudo-element's one child (LAY_GEN | LAY_GTEXT) */
#define LAY_GEN 0x80000000u
#define LAY_GTEXT 0x40000000u
#define LAY_GIDX 0x3fffffffu
/* a node's entry in sx: its style, and whether the element has a ::before, an ::after,
   or another pseudo-element of its own */
#define LAY_HAS_BEFORE 0x80000000u
#define LAY_HAS_AFTER 0x40000000u
#define LAY_HAS_OTHER 0x20000000u
#define LAY_SMASK 0x1fffffffu

/* the index in gx (sorted by element, then pseudo-element) of el's pseudo-element pk, or -1 */
static int64_t lay_gen_find(const vector<lay_gen> *gx, uint32_t el, uint32_t pk) {
    size_t lo = 0, hi = len(*gx);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        lay_gen g = at(*gx, mid);
        if (g.el < el || (g.el == el && g.pseudo < pk)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < len(*gx) && at(*gx, lo).el == el && at(*gx, lo).pseudo == pk) return (int64_t)lo;
    return -1;
}
/* the element a run's item stands for: a pseudo-element's, or the node itself */
static uint32_t lay_dom(const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).el;
    return x;
}

/* ---- what layout reads of a style besides lay_style: positioning, floats, overflow,
   vertical-align, min and max sizes, flex and grid; read in place, by the style's index ---- */

enum { POS_STATIC, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED, POS_STICKY };
/* a float's side: left, right (the line's: a vertical line's top and bottom), or inline-start, inline-end (its containing
   block's direction's, CSS Logical 1, 2.1: lay_float_side) */
enum { FL_NONE, FL_LEFT, FL_RIGHT, FL_ISTART, FL_IEND };
enum { VA_BASELINE, VA_SUB, VA_SUPER, VA_TEXT_TOP, VA_TEXT_BOTTOM, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_LEN };
enum { LX_FLOW, LX_FLEX, LX_GRID };
/* the alignment keywords: justify-content, align-items, align-self, align-content, justify-items,
   justify-self (AJ_BASELINE first baseline, AJ_LBASE last baseline) */
enum { AJ_NORMAL, AJ_START, AJ_END, AJ_CENTER, AJ_STRETCH, AJ_BETWEEN, AJ_AROUND, AJ_EVENLY, AJ_BASELINE, AJ_AUTO, AJ_LBASE };

typedef struct lay_ext {
    uint8_t pos, flt, clr, clip, va, rtl, hyph, lx, frow, frev, fwrap, jc, ai, as, ac, ji, js, zauto, ostyle;
    uint8_t ubidi, astart;              /* unicode-bidi (UB_); text-align start 1, end 2, other 0 */
    int32_t z, order;
    lay_len off[4];                     /* top, right, bottom, left: its frame's (lay_frame), turned as its edges are
                                           (the physical ones: lay_offp) */
    lay_len minw, minh, maxh, hraw, valen, basis, rgap, cgap;   /* hraw: height as declared, a percentage kept */
    double grow, shrink;
    uint32_t gtc, gtr, gac, gar, gflow, gcs, gce, grs, gre, gta;    /* the grid's values, kept as text: their cvals */
    /* what paint reads: the corners' radii (a percentage of the border box's width), the
       opacity, the cvals of the values kept as text (none: 0), the outline, the image */
    lay_len rad[4];
    double opacity, ow, ooffset;
    uint32_t ocolor, shadows, bgimg, bgpos, bgsize, bgrepeat, xform, xorigin, tshadow, bgres, bgclip, bgorigin, filter, backdrop;
    uint32_t persp, porigin, translate, rotate, scale;
    uint8_t p3d, backface;
    uint8_t hidden;                     /* visibility: hidden or collapse (the box's hidden) */
    int paints;                         /* any of these other than its initial value */
    int32_t clamp;                      /* -webkit-line-clamp's lines on a vertical -webkit-box; 0 none */
    uint8_t ellipsis;                   /* text-overflow: ellipsis */
    /* a form control's theme: appearance none; an author's background (colour or image), border, or box shadow (Blink's
       LayoutTheme::IsControlStyled leaves a styled field, button or select to CSS) */
    uint8_t app_none, styled_bg, styled_border, styled_shadow;
    uint8_t ofit;                       /* object-fit: 0 fill, 1 contain, 2 cover, 3 none, 4 scale-down */
    uint8_t irender;                    /* image-rendering: 1 pixelated or crisp-edges (nearest), 0 any other */
    uint8_t clone;                      /* box-decoration-break: clone (an inline box's fragments each wrapped in its edges) */
    uint32_t opos;                      /* object-position's cval; 0 at its initial value */
    uint32_t cpath;                     /* clip-path's cval when it is a basic shape or a geometry box; else 0 */
    uint32_t cssclip;                   /* clip's cval when it is rect(); else 0 */
    uint32_t c0;                        /* the style's first value: where its values came from (quirky margins) */
    uint8_t cq;                         /* container-type: 1 inline-size, 2 size (a query container; containment) */
    /* a table's and its parts': a column (1, display table-column) or column group (2); a row group's kind (0 a body, 1 a
       head, 2 a foot); border-collapse: collapse; empty-cells: hide; table-layout: fixed; caption-side: bottom; and
       border-spacing's two lengths, px */
    uint8_t tcol, tgroup, collapse, ehide, tfixed, capb;
    double hs, vs;
    /* line breaking (CSS Text 3, 5): word-break (0 normal, 1 break-all, 2 keep-all, 3 break-word, 4 auto-phrase),
       line-break (0 auto and -webkit-line-break's after-white-space, 1 loose, 2 normal, 3 strict, 4 anywhere),
       overflow-wrap (0 normal, 1 break-word, 2 anywhere) */
    uint8_t wbrk, lbrk, owrap;
} lay_ext;

/* an absolutely positioned element the flow met: its static position, in the content box of box */
typedef struct lay_abs { uint32_t el, box; double x, y; } lay_abs;
/* a float's margin box, in its block formatting context's coordinates, and its side */
typedef struct lay_flt { double x, y, w, h; int side; } lay_flt;
/* what an inline element passes to its content: its baseline's shift (vertical-align), a
   line-relative alignment (top, bottom), its relative offset */
typedef struct lay_inh { double shift, rx, ry; int va; } lay_inh;
/* how a block-level box is laid out: its margin box's left and top in its block formatting
   context, its containing block's definite height (-1 when it has none), a border-box width
   it is given (-1 for none), and whether an auto width shrinks to fit; qs: its top margin
   collapses at a quirky container's start, which leaves out quirky margins (quirks mode) */
typedef struct lay_bk { double ox, oy, ch, fw; int shrink, item, qs; } lay_bk;

/* the style's index of a run's item: a pseudo-element's, or the node's */
static uint32_t lay_si(const vector<uint32_t> *sx, const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).style;
    return x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0;
}
/* a run's item's lay_ext, read in place: a field of it costs no copy of the whole (lay_si's
   index is always one of xc's) */
#define LAY_X(x) data(*xc)[lay_si(sx, gx, (x))]

static int lay_content_sized(lay_len l) { return l.unit == LAY_MINC || l.unit == LAY_MAXC || l.unit == LAY_FITC; }
static double lay_res(const css_style *st, lay_len l, double base) {
    if (l.unit == LAY_PCT) return base * l.v / 100;
    if (l.unit == LAY_AUTO || lay_content_sized(l)) return 0;
    if (l.unit == LAY_CALC) return l.v + base * l.pct / 100;
    if (l.unit == LAY_EXPR) {
        double x;
        amb_span sp = st->cval[l.cv].text;
        if (css_calc_eval(text_ptr(st, sp), sp.len, base, &x)) return x;
        return 0;
    }
    return l.v;
}
static lay_len lay_len_at(const css_style *st, uint32_t c) {
    lay_len l;
    l.unit = LAY_PX;
    l.v = 0;
    l.pct = 0;
    l.cv = c;
    if (!c) return l;
    css_unit u = st->cval[c].unit;
    if (u == CSS_UNIT_KEYWORD) {
        uint32_t w = st->cval[c].word;
        l.unit = names_is(st, w, "min-content", 11) ? LAY_MINC : names_is(st, w, "max-content", 11) ? LAY_MAXC :
                 names_is(st, w, "fit-content", 11) ? LAY_FITC : LAY_AUTO;
    } else if (u == CSS_UNIT_AUTO) l.unit = LAY_AUTO;
    else if (u == CSS_UNIT_PERCENT) { l.unit = LAY_PCT; l.v = st->cval[c].num; }
    else if (u == CSS_UNIT_CALC) { l.unit = LAY_CALC; l.v = st->cval[c].num; l.pct = st->cval[c].pct; }
    else if (u == CSS_UNIT_EXPR) l.unit = LAY_EXPR;
    else l.v = st->cval[c].num;
    return l;
}

/* the keyword of the value c, copied into w (32 bytes; "" for no keyword) */
static void lay_word_of(const css_style *st, uint32_t c, char *w) {
    w[0] = 0;
    if (!c || st->cval[c].unit != CSS_UNIT_KEYWORD) return;
    const char *p = names_ptr(st, st->cval[c].word);
    size_t i = 0;
    while (p[i] && i < 31) { w[i] = p[i]; i++; }
    w[i] = 0;
}
static int lay_w(const char *w, const char *k) { return strcmp(w, k) == 0; }

static int lay_border_style(const char *w) {
    static const char *const names[10] = { "none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset", "outset" };
    for (int i = 0; i < 10; i++) if (lay_w(w, names[i])) return i;
    return LAY_BS_NONE;
}

/* the web font for a family (lower case, no spaces, as the cascade resolved it) at a
   weight and style: of the faces @font-face gave the family, the one of the style asked
   for if there is one, then of the nearest weight. webface: each face record's index in
   f (-1 when it was not read). 0 when the family has none; else its face, its resource */
static int lay_web_face(const css_style *st, const vector<int32_t> *webface, const char *fam, double weight, int italic, int *face,
                        uint32_t *res) {
    int best = 0;
    double bscore = 1e9;
    for (uint32_t fi = 1; fi < st->face_len && fi < len(*webface); fi++) {
        if (at(*webface, fi) < 0) continue;
        amb_span sp = st->face[fi].family;
        const char *t = text_ptr(st, sp);
        size_t k = 0, i = 0;
        int same = 1;
        for (; i < sp.len && same; i++) {
            char c = t[i];
            if (c == ' ' || c == '\t') continue;
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (fam[k] != c) same = 0;
            else k++;
        }
        if (!same || fam[k]) continue;
        double score = (st->face[fi].italic != (uint8_t)italic) * 10000.0 + (weight > st->face[fi].weight ? weight - st->face[fi].weight :
                       st->face[fi].weight - weight);
        if (score < bscore) { bscore = score; best = (int)fi; }
    }
    if (!best) return 0;
    *face = at(*webface, (uint32_t)best);
    *res = st->face[best].font.index;
    return 1;
}

/* a style's container-type (its values from c0): 1 inline-size, 2 size, 0 none (CSS Conditional 5, 6.1.2: size and
   inline-size give size or inline-size containment and an independent formatting context) */
static int lay_cq_kind(const css_style *st, uint32_t c0) {
    uint32_t c = c0 + CSS_PROP_CONTAINER_TYPE;
    if (st->cval[c].unit == CSS_UNIT_KEYWORD) {
        uint32_t w = st->cval[c].word;
        return names_is(st, w, "inline-size", 11) ? 1 : names_is(st, w, "size", 4) ? 2 : 0;
    }
    if (st->cval[c].unit != CSS_UNIT_TEXT) return 0;
    amb_span sp = st->cval[c].text;
    const char *t = text_ptr(st, sp);
    return sp.len >= 11 && memcmp(t, "inline-size", 11) == 0 ? 1 : sp.len >= 4 && memcmp(t, "size", 4) == 0 ? 2 : 0;
}

/* a computed value kept as text that holds a url() (a filter's list, a clip-path, a mask naming an SVG element) */
static int lay_has_url(const css_style *st, uint32_t c) {
    if (st->cval[c].unit != CSS_UNIT_TEXT) return 0;
    amb_span sp = st->cval[c].text;
    const char *t = text_ptr(st, sp);
    int got = 0;
    for (size_t q = 0; q + 4 <= sp.len && !got; q++) got = memcmp(t + q, "url(", 4) == 0;
    return got;
}

/* a list-style-type's marker (its value c): 0 none; a predefined symbol marker Chromium draws as a shape, or a
   counter style extending one (1 disc, 2 circle, 3 square, 9 disclosure-closed, 10 disclosure-open); else 4, a
   marker of text: a counter style's representation with its prefix and suffix, or a string */
static int lay_list_kind(const css_style *st, uint32_t c) {
    vector<char> tmp;
    if (st->cval[c].unit == CSS_UNIT_TEXT) {
        amb_span sp = st->cval[c].text;
        const char *t = text_ptr(st, sp);
        if (sp.len && t[0] == '"') return 4;
        int k = css_counter_text(st, t, sp.len, 1, 1, &tmp);
        return k ? k : 4;
    }
    if (st->cval[c].unit != CSS_UNIT_KEYWORD) return 1;
    uint32_t w = st->cval[c].word;
    /* the names no sheet may define: their markers known */
    if (names_is(st, w, "none", 4)) return 0;
    if (names_is(st, w, "disc", 4)) return 1;
    if (names_is(st, w, "circle", 6)) return 2;
    if (names_is(st, w, "square", 6)) return 3;
    if (names_is(st, w, "disclosure-closed", 17)) return 9;
    if (names_is(st, w, "disclosure-open", 15)) return 10;
    if (names_is(st, w, "decimal", 7)) return 4;
    int k = css_counter_text(st, names_ptr(st, w), names_len(st, w), 1, 1, &tmp);
    return k ? k : 4;
}

/* a style as layout reads it, from its computed values: a run from c0 in enum(prop)'s
   order (src/css/css.c) */
/* a border colour that is currentcolor: declared so (or as its initial value), or never
   declared (the computed value keeps the colour alone) */
static int lay_currentcolor(const css_style *st, uint32_t c) {
    uint32_t dcl = st->cval[c].winner;
    if (!dcl || dcl >= st->decl_len) return 1;
    uint32_t w = st->decl[dcl].word;
    if (!w) return 0;
    return names_is(st, w, "currentcolor", 12) || names_is(st, w, "initial", 7) || names_is(st, w, "unset", 5);
}
/* ---- writing modes (CSS Writing Modes 4) ----
   A box is laid out in its writing mode's frame: as a horizontal box is, its axes turned. A vertical box's inline
   axis is the turned box's horizontal one (its x down the page, sideways-lr's up it), its block axis the vertical
   one (its y to the left, vertical-lr's and sideways-lr's to the right), so that the code that lays out a
   horizontal box lays out a vertical one, its style read turned (lay_style_make) and its boxes, lines and fragments
   turned back at the end (lay_absolute). A box whose writing mode is not its parent's is laid out in its own frame
   as a formatting context of its own (lay_wm_root), and set in its parent's turned. Frames: 0 horizontal-tb; 1
   vertical-rl and sideways-rl (block-start the right, line-left the top); 2 vertical-lr (the left and the top; its
   lines' over side their right, their content mirrored in them); 3 sideways-lr (the left; line-left the bottom) */
static int lay_frame(int wm) { return wm == CSS_WM_VRL || wm == CSS_WM_SRL ? 1 : wm == CSS_WM_VLR ? 2 : wm == CSS_WM_SLR ? 3 : 0; }
/* a text of a vertical style's orientation (lay_style's orient) set on the central baseline: mixed or upright (a
   combined text's box takes no line of its fonts': lay_shape_pieces) */
#define LAY_VC(o) ((o) == 1 || (o) == 2)
/* a fragment's orientation drawn from a style's (lay_style's orient) for text that is not combined (a marker's, alt
   text's, ...): a combined style's text mixed */
#define LAY_NOCOMB(o) ((uint8_t)((o) == 4 ? 1 : (o)))
/* the physical side (0 top, 1 right, 2 bottom, 3 left) a frame's side k is, and the frame's side a physical one is */
static int lay_tside(int f, int k) { return f == 1 ? (k + 1) & 3 : f == 2 ? 3 - k : f == 3 ? (k + 3) & 3 : k; }
static int lay_pside(int f, int p) { return f == 1 ? (p + 3) & 3 : f == 2 ? 3 - p : f == 3 ? (p + 1) & 3 : p; }
/* box b's edges of frame cf made frame pf's, and with them its size and offset (below) */
static void lay_turn_sides(layout_tree *t, uint32_t b, int cf, int pf);
static void lay_turn_edges(layout_tree *t, uint32_t b, int cf, int pf);
/* a style's writing mode, from its values (c0's) */
static int lay_wm_at(const css_style *st, uint32_t c0) {
    if (!c0) return CSS_WM_HTB;
    uint32_t w = st->cval[c0 + CSS_PROP_WRITING_MODE].word;
    return names_is(st, w, "vertical-rl", 11) ? CSS_WM_VRL : names_is(st, w, "vertical-lr", 11) ? CSS_WM_VLR :
           names_is(st, w, "sideways-rl", 11) ? CSS_WM_SRL : names_is(st, w, "sideways-lr", 11) ? CSS_WM_SLR : CSS_WM_HTB;
}

static void lay_style_make(const css_style *st, const amb_fonts *f, const vector<int32_t> *webface, uint32_t c0, int fwm, lay_style *s) {
    memset(s, 0, sizeof *s);
    s->color = 0x000000ff;
    s->fill = 0x000000ff;
    s->fs = 16;
    s->face = AMB_FACE_SERIF;
    s->width.unit = LAY_AUTO;
    s->height.unit = LAY_AUTO;
    s->maxw.unit = LAY_AUTO;
    if (!c0) { s->display = D_NONE; return; }
    uint32_t cv[CSS_NPROPS];
    for (uint32_t k = 0; k < CSS_NPROPS; k++) cv[k] = c0 + k;
    char w[32];
    uint32_t c = cv[CSS_PROP_DISPLAY];
    lay_word_of(st, c, w);
    if (lay_w(w, "none") || lay_w(w, "table-column") || lay_w(w, "table-column-group")) s->display = D_NONE;
    else if (lay_w(w, "inline")) s->display = D_INLINE;
    else if (lay_w(w, "list-item")) s->display = D_LIST_ITEM;
    else if (lay_w(w, "inline-block") || lay_w(w, "inline-flex") || lay_w(w, "inline-grid") || lay_w(w, "inline-table") ||
             lay_w(w, "-webkit-inline-box"))
        s->display = D_INLINE_BLOCK;
    else if (lay_w(w, "table")) s->display = D_TABLE;
    else if (lay_w(w, "table-row-group") || lay_w(w, "table-header-group") || lay_w(w, "table-footer-group"))
        s->display = D_ROW_GROUP;
    else if (lay_w(w, "table-row")) s->display = D_ROW;
    else if (lay_w(w, "table-cell")) s->display = D_CELL;
    else if (lay_w(w, "table-caption")) s->display = D_CAPTION;
    else if (lay_w(w, "contents")) s->display = D_CONTENTS;
    else {
        s->display = D_BLOCK;
        s->bfc = lay_w(w, "flow-root") || lay_w(w, "flex") || lay_w(w, "grid") || lay_w(w, "-webkit-box");
    }
    /* a query container makes a formatting context of its own (CSS Conditional 5, 6.1.2) */
    if (lay_cq_kind(st, c0)) s->bfc = 1;
    s->color = st->cval[cv[CSS_PROP_COLOR]].color;
    s->fill = st->cval[cv[CSS_PROP_WEBKIT_TEXT_FILL_COLOR]].color;
    s->bg = st->cval[cv[CSS_PROP_BACKGROUND_COLOR]].color;
    lay_word_of(st, cv[CSS_PROP_VISIBILITY], w);
    s->hidden = (uint8_t)(lay_w(w, "hidden") || lay_w(w, "collapse"));
    s->fs = st->cval[cv[CSS_PROP_FONT_SIZE]].num;
    double weight = st->cval[cv[CSS_PROP_FONT_WEIGHT]].num;
    int bold = weight >= 600;
    lay_word_of(st, cv[CSS_PROP_FONT_STYLE], w);
    s->italic = lay_w(w, "italic") || lay_w(w, "oblique");
    /* the family the cascade resolved: a web font the page declared under it (over a
       built-in face of the name), else the built-in face it names, else the default */
    lay_word_of(st, cv[CSS_PROP_FONT_FAMILY], w);
    int generic = lay_w(w, "serif") || lay_w(w, "sans-serif") || lay_w(w, "monospace");
    int builtin = amb_face_named(w, bold, s->italic), wf;
    if (!generic && lay_web_face(st, webface, w, weight, s->italic, &wf, &s->font)) {
        s->face = wf;
        /* an italic face is slanted already: no slant made for it */
        for (uint32_t fi = 1; fi < st->face_len && fi < len(*webface); fi++)
            if (at(*webface, fi) == wf && st->face[fi].italic) s->italic = 0;
    } else s->face = builtin >= 0 ? builtin : amb_face_named("serif", bold, s->italic);
    double gap;
    amb_face_metrics(f, s->face, s->fs, &s->asc, &s->desc, &gap);
    c = cv[CSS_PROP_LINE_HEIGHT];
    if (st->cval[c].unit == CSS_UNIT_NUMBER) s->lh = st->cval[c].num * s->fs;
    else if (st->cval[c].unit == CSS_UNIT_PX) s->lh = st->cval[c].num;
    else { s->lh = s->asc + s->desc + gap; s->lhn = 1; }
    lay_word_of(st, cv[CSS_PROP_DIRECTION], w);
    s->rtl = lay_w(w, "rtl");
    /* text-align: start and end as the direction has them */
    lay_word_of(st, cv[CSS_PROP_TEXT_ALIGN], w);
    s->align = lay_w(w, "right") ? AL_RIGHT : lay_w(w, "left") ? AL_LEFT : lay_w(w, "center") ? AL_CENTER :
               lay_w(w, "justify") ? AL_JUSTIFY : lay_w(w, "end") ? (s->rtl ? AL_LEFT : AL_RIGHT) : (s->rtl ? AL_RIGHT : AL_LEFT);
    lay_word_of(st, cv[CSS_PROP_WHITE_SPACE], w);
    s->ws = lay_w(w, "pre") ? WS_PRE : lay_w(w, "nowrap") ? WS_NOWRAP : lay_w(w, "pre-wrap") || lay_w(w, "break-spaces") ? WS_PRE_WRAP :
            lay_w(w, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
    lay_word_of(st, cv[CSS_PROP_TEXT_TRANSFORM], w);
    s->tt = lay_w(w, "uppercase") ? TT_UPPER : lay_w(w, "lowercase") ? TT_LOWER : lay_w(w, "capitalize") ? TT_CAPITALIZE :
            lay_w(w, "math-auto") ? TT_MATH_AUTO : TT_NONE;
    c = cv[CSS_PROP_LETTER_SPACING];
    s->ls = st->cval[c].unit == CSS_UNIT_PX ? st->cval[c].num : 0;
    c = cv[CSS_PROP_WORD_SPACING];
    s->wsp = st->cval[c].unit == CSS_UNIT_PX ? st->cval[c].num : 0;
    s->indent = lay_len_at(st, cv[CSS_PROP_TEXT_INDENT]);
    lay_word_of(st, cv[CSS_PROP_TEXT_DECORATION], w);
    s->deco = lay_w(w, "underline") ? 1 : lay_w(w, "overline") ? 2 : lay_w(w, "line-through") ? 4 : 0;
    s->lcv = cv[CSS_PROP_LIST_STYLE_TYPE];
    s->list = lay_list_kind(st, s->lcv);
    lay_word_of(st, cv[CSS_PROP_LIST_STYLE_POSITION], w);
    s->list_inside = lay_w(w, "inside");
    for (int k = 0; k < 4; k++) {
        s->m[k] = lay_len_at(st, cv[CSS_PROP_MARGIN_TOP + k]);
        s->p[k] = lay_len_at(st, cv[CSS_PROP_PADDING_TOP + k]);
        s->b[k] = st->cval[cv[CSS_PROP_BORDER_TOP_WIDTH + k]].num;
        s->bc[k] = st->cval[cv[CSS_PROP_BORDER_TOP_COLOR + k]].color;
        lay_word_of(st, cv[CSS_PROP_BORDER_TOP_STYLE + k], w);
        s->bs[k] = (uint8_t)lay_border_style(w);
        /* an inset, outset, groove or ridge side whose colour is currentcolor (declared so, or
           never declared: an iframe's border, the user agent's 2px inset) is drawn from #eeeeee
           whatever the colour, as Blink draws it (measured in Chromium, Phase 12u) */
        if ((s->bs[k] == LAY_BS_INSET || s->bs[k] == LAY_BS_OUTSET || s->bs[k] == LAY_BS_GROOVE || s->bs[k] == LAY_BS_RIDGE) &&
            lay_currentcolor(st, cv[CSS_PROP_BORDER_TOP_COLOR + k])) s->bc[k] = 0xeeeeeeffu;
    }
    s->wm = (uint8_t)(fwm >= 0 ? fwm : lay_wm_at(st, c0));
    int fr = lay_frame(s->wm);
    s->width = lay_len_at(st, cv[fr ? CSS_PROP_HEIGHT : CSS_PROP_WIDTH]);
    s->height = lay_len_at(st, cv[fr ? CSS_PROP_WIDTH : CSS_PROP_HEIGHT]);
    if (fr) {
        /* the edges turned: each side the frame's */
        lay_len m2[4], p2[4];
        double b2[4];
        uint32_t c2[4];
        uint8_t s2[4];
        for (int k = 0; k < 4; k++) {
            int q = lay_tside(fr, k);
            m2[k] = s->m[q];
            p2[k] = s->p[q];
            b2[k] = s->b[q];
            c2[k] = s->bc[q];
            s2[k] = s->bs[q];
        }
        for (int k = 0; k < 4; k++) { s->m[k] = m2[k]; s->p[k] = p2[k]; s->b[k] = b2[k]; s->bc[k] = c2[k]; s->bs[k] = s2[k]; }
        /* text-orientation; the sideways modes set every glyph turned, on the alphabetic baseline */
        lay_word_of(st, cv[CSS_PROP_TEXT_ORIENTATION], w);
        s->orient = (uint8_t)(s->wm == CSS_WM_SRL || s->wm == CSS_WM_SLR || lay_w(w, "sideways") ? 3 : lay_w(w, "upright") ? 2 : 1);
        if (s->orient != 3) {
            /* the central baseline: the font's height (its rounded ascent and descent) halved, the descent the half
               floored (Blink's FontMetrics for kCentralBaseline, measured) */
            double hh = s->asc + s->desc;
            s->desc = floor(hh / 2);
            s->asc = hh - s->desc;
        }
        /* text-combine-upright: all (vertical-rl's and vertical-lr's, whatever the text's orientation; the sideways
           modes' none, as Chromium has it): each text node set across the line in an em, its box where its text's would
           be (lay_chars) */
        if (s->wm == CSS_WM_VRL || s->wm == CSS_WM_VLR) {
            lay_word_of(st, cv[CSS_PROP_TEXT_COMBINE_UPRIGHT], w);
            if (lay_w(w, "all")) s->orient = 4;
        }
    }
    lay_word_of(st, cv[CSS_PROP_BOX_SIZING], w);
    s->border_box = lay_w(w, "border-box");
    c = cv[fr ? CSS_PROP_MAX_HEIGHT : CSS_PROP_MAX_WIDTH];
    if (st->cval[c].unit != CSS_UNIT_KEYWORD) s->maxw = lay_len_at(st, c);
    if (s->border_box) {
        /* a declared height is its border box's: the content's is what is left */
        double vedges = s->b[0] + s->b[2] + (s->p[0].unit == LAY_PX ? s->p[0].v : 0) + (s->p[2].unit == LAY_PX ? s->p[2].v : 0);
        if (s->height.unit == LAY_PX) { s->height.v -= vedges; if (s->height.v < 0) s->height.v = 0; }
    }
    if (s->height.unit != LAY_PX && s->height.unit != LAY_AUTO) s->height.unit = LAY_AUTO;   /* against no definite height */
}

static int lay_align_of(const char *w) {
    if (lay_w(w, "flex-start") || lay_w(w, "start") || lay_w(w, "self-start") || lay_w(w, "left")) return AJ_START;
    if (lay_w(w, "flex-end") || lay_w(w, "end") || lay_w(w, "self-end") || lay_w(w, "right")) return AJ_END;
    if (lay_w(w, "center")) return AJ_CENTER;
    if (lay_w(w, "stretch")) return AJ_STRETCH;
    if (lay_w(w, "space-between")) return AJ_BETWEEN;
    if (lay_w(w, "space-around")) return AJ_AROUND;
    if (lay_w(w, "space-evenly")) return AJ_EVENLY;
    if (lay_w(w, "baseline")) return AJ_BASELINE;
    if (w[0] == 'l' && lay_w(w, "last-baseline")) return AJ_LBASE;
    if (lay_w(w, "auto")) return AJ_AUTO;
    return AJ_NORMAL;
}

/* whether a declaration of the author's (a sheet's or a style attribute's) won the computed value c */
static int lay_author_won(const css_style *st, uint32_t c) {
    uint32_t dcl = st->cval[c].winner;
    if (!dcl || dcl >= st->decl_len) return 0;
    uint32_t ru = st->decl[dcl].rule;
    uint32_t sh = ru && ru < st->rule_len ? st->rule[ru].sheet : 0;
    return !sh || sh >= st->sheet_len || st->sheet[sh].origin != CSS_ORIGIN_USER_AGENT;
}
/* what layout reads of a style besides lay_style, from its computed values (a run from c0) */
static void lay_ext_make(const css_style *st, uint32_t c0, int fwm, lay_ext *e) {
    memset(e, 0, sizeof *e);
    e->zauto = 1;
    e->shrink = 1;
    e->basis.unit = LAY_AUTO;
    e->minw.unit = LAY_AUTO;
    e->minh.unit = LAY_AUTO;
    e->maxh.unit = LAY_AUTO;
    e->hraw.unit = LAY_AUTO;
    for (int k = 0; k < 4; k++) e->off[k].unit = LAY_AUTO;
    e->c0 = c0;
    if (!c0) return;
    char w[32];
    /* its frame (lay_frame): the offsets and the sizes below turned as lay_style_make turns the edges */
    int fr = lay_frame(fwm >= 0 ? fwm : lay_wm_at(st, c0));
    lay_word_of(st, c0 + CSS_PROP_POSITION, w);
    e->pos = lay_w(w, "relative") ? POS_RELATIVE : lay_w(w, "absolute") ? POS_ABSOLUTE : lay_w(w, "fixed") ? POS_FIXED :
             lay_w(w, "sticky") ? POS_STICKY : POS_STATIC;
    for (int k = 0; k < 4; k++) e->off[k] = lay_len_at(st, c0 + CSS_PROP_TOP + lay_tside(fr, k));
    /* float and clear: left and right the line's (a vertical one's top and bottom), inline-start and inline-end its
       containing block's direction's, found where it is placed (CSS Logical 1, 2.1; clear's 4 and 5) */
    lay_word_of(st, c0 + CSS_PROP_FLOAT, w);
    e->flt = lay_w(w, "left") ? FL_LEFT : lay_w(w, "right") ? FL_RIGHT : lay_w(w, "inline-start") ? FL_ISTART :
             lay_w(w, "inline-end") ? FL_IEND : FL_NONE;
    lay_word_of(st, c0 + CSS_PROP_CLEAR, w);
    e->clr = lay_w(w, "left") ? 1 : lay_w(w, "right") ? 2 : lay_w(w, "both") ? 3 : lay_w(w, "inline-start") ? 4 : lay_w(w, "inline-end") ? 5 : 0;
    uint32_t c = c0 + CSS_PROP_Z_INDEX;
    if (st->cval[c].unit == CSS_UNIT_NUMBER) { e->z = (int32_t)st->cval[c].num; e->zauto = 0; }
    for (int k = 0; k < 2; k++) {
        lay_word_of(st, c0 + CSS_PROP_OVERFLOW_X + k, w);
        if (lay_w(w, "hidden") || lay_w(w, "scroll") || lay_w(w, "auto")) e->clip = 1;
        else if (lay_w(w, "clip") && !e->clip) e->clip = 2;
    }
    c = c0 + CSS_PROP_VERTICAL_ALIGN;
    lay_word_of(st, c, w);
    if (st->cval[c].unit == CSS_UNIT_KEYWORD)
        e->va = lay_w(w, "sub") ? VA_SUB : lay_w(w, "super") ? VA_SUPER : lay_w(w, "text-top") ? VA_TEXT_TOP :
                lay_w(w, "text-bottom") ? VA_TEXT_BOTTOM : lay_w(w, "middle") ? VA_MIDDLE : lay_w(w, "top") ? VA_TOP :
                lay_w(w, "bottom") ? VA_BOTTOM : VA_BASELINE;
    else { e->va = VA_LEN; e->valen = lay_len_at(st, c); }
    lay_word_of(st, c0 + CSS_PROP_DIRECTION, w);
    e->rtl = lay_w(w, "rtl");
    lay_word_of(st, c0 + CSS_PROP_UNICODE_BIDI, w);
    e->ubidi = (uint8_t)(lay_w(w, "embed") ? 1 : lay_w(w, "isolate") ? 2 : lay_w(w, "bidi-override") ? 3 : lay_w(w, "isolate-override") ? 4 :
                         lay_w(w, "plaintext") ? 5 : 0);
    lay_word_of(st, c0 + CSS_PROP_TEXT_ALIGN, w);
    e->astart = (uint8_t)(lay_w(w, "start") ? 1 : lay_w(w, "end") ? 2 : 0);
    lay_word_of(st, c0 + CSS_PROP_HYPHENS, w);
    e->hyph = lay_w(w, "auto") ? 2 : lay_w(w, "manual") ? 1 : 0;
    lay_word_of(st, c0 + CSS_PROP_WORD_BREAK, w);
    e->wbrk = (uint8_t)(lay_w(w, "break-all") ? 1 : lay_w(w, "keep-all") ? 2 : lay_w(w, "break-word") ? 3 : lay_w(w, "auto-phrase") ? 4 : 0);
    lay_word_of(st, c0 + CSS_PROP_LINE_BREAK, w);
    e->lbrk = (uint8_t)(lay_w(w, "loose") ? 1 : lay_w(w, "normal") ? 2 : lay_w(w, "strict") ? 3 : lay_w(w, "anywhere") ? 4 : 0);
    lay_word_of(st, c0 + CSS_PROP_OVERFLOW_WRAP, w);
    e->owrap = (uint8_t)(lay_w(w, "break-word") ? 1 : lay_w(w, "anywhere") ? 2 : 0);
    lay_word_of(st, c0 + CSS_PROP_DISPLAY, w);
    e->lx = lay_w(w, "flex") || lay_w(w, "inline-flex") ? LX_FLEX : lay_w(w, "grid") || lay_w(w, "inline-grid") ? LX_GRID : LX_FLOW;
    e->tcol = (uint8_t)(lay_w(w, "table-column") ? 1 : lay_w(w, "table-column-group") ? 2 : 0);
    e->tgroup = (uint8_t)(lay_w(w, "table-header-group") ? 1 : lay_w(w, "table-footer-group") ? 2 : 0);
    lay_word_of(st, c0 + CSS_PROP_BORDER_COLLAPSE, w);
    e->collapse = (uint8_t)lay_w(w, "collapse");
    lay_word_of(st, c0 + CSS_PROP_EMPTY_CELLS, w);
    e->ehide = (uint8_t)lay_w(w, "hide");
    lay_word_of(st, c0 + CSS_PROP_TABLE_LAYOUT, w);
    e->tfixed = (uint8_t)lay_w(w, "fixed");
    lay_word_of(st, c0 + CSS_PROP_CAPTION_SIDE, w);
    e->capb = (uint8_t)lay_w(w, "bottom");
    e->hs = st->cval[c0 + CSS_PROP_WEBKIT_BORDER_HORIZONTAL_SPACING].num;
    e->vs = st->cval[c0 + CSS_PROP_WEBKIT_BORDER_VERTICAL_SPACING].num;
    int wkbox = lay_w(w, "-webkit-box") || lay_w(w, "-webkit-inline-box");
    lay_word_of(st, c0 + CSS_PROP_WEBKIT_BOX_ORIENT, w);
    int wkvert = lay_w(w, "vertical");
    if (wkbox && !wkvert) e->lx = LX_FLEX;      /* the old -webkit-box: a row of its children */
    /* -webkit-line-clamp: the lines a vertical -webkit-box keeps (CSS Overflow 4's legacy clamp). The cascade
       computes such a box flow-root or inline-block, its display's num 1 (the old box specified) */
    uint32_t lc = c0 + CSS_PROP_WEBKIT_LINE_CLAMP;
    e->clamp = st->cval[c0 + CSS_PROP_DISPLAY].num == 1 && st->cval[lc].unit == CSS_UNIT_NUMBER && st->cval[lc].num >= 1 ?
               (int32_t)st->cval[lc].num : 0;
    lay_word_of(st, c0 + CSS_PROP_TEXT_OVERFLOW, w);
    e->ellipsis = (uint8_t)lay_w(w, "ellipsis");
    lay_word_of(st, c0 + CSS_PROP_APPEARANCE, w);
    e->app_none = (uint8_t)lay_w(w, "none");
    e->styled_bg = (uint8_t)(lay_author_won(st, c0 + CSS_PROP_BACKGROUND_COLOR) || lay_author_won(st, c0 + CSS_PROP_BACKGROUND_IMAGE));
    for (int k = 0; k < 4; k++)
        if (lay_author_won(st, c0 + CSS_PROP_BORDER_TOP_WIDTH + k) || lay_author_won(st, c0 + CSS_PROP_BORDER_TOP_STYLE + k) ||
            lay_author_won(st, c0 + CSS_PROP_BORDER_TOP_COLOR + k) || lay_author_won(st, c0 + CSS_PROP_BORDER_TOP_LEFT_RADIUS + k))
            e->styled_border = 1;     /* a radius too: Chromium's author border (measured: a radius of 0 drops the look) */
    e->styled_shadow = (uint8_t)lay_author_won(st, c0 + CSS_PROP_BOX_SHADOW);
    lay_word_of(st, c0 + CSS_PROP_OBJECT_FIT, w);
    e->ofit = (uint8_t)(lay_w(w, "contain") ? 1 : lay_w(w, "cover") ? 2 : lay_w(w, "none") ? 3 : lay_w(w, "scale-down") ? 4 : 0);
    e->opos = st->cval[c0 + CSS_PROP_OBJECT_POSITION].how == CSS_HOW_INITIAL ? 0 : c0 + CSS_PROP_OBJECT_POSITION;
    lay_word_of(st, c0 + CSS_PROP_IMAGE_RENDERING, w);
    e->irender = (uint8_t)(lay_w(w, "pixelated") || lay_w(w, "crisp-edges"));
    if (e->ofit || e->opos || e->irender) e->paints = 1;
    e->cq = (uint8_t)lay_cq_kind(st, c0);
    lay_word_of(st, c0 + CSS_PROP_BOX_DECORATION_BREAK, w);
    e->clone = (uint8_t)lay_w(w, "clone");
    {
        /* clip-path's basic shape or geometry box (an HTML box's; a url() is an SVG element's) */
        uint32_t cc = c0 + CSS_PROP_CLIP_PATH;
        amb_span cs = st->cval[cc].text;
        const char *cq = text_ptr(st, cs);
        int url = cs.len > 4 && (cq[0] | 32) == 'u' && (cq[1] | 32) == 'r' && (cq[2] | 32) == 'l' && cq[3] == '(';
        e->cpath = st->cval[cc].unit == CSS_UNIT_TEXT && cs.len && !url ? cc : 0;
        if (e->cpath) e->paints = 1;
        /* clip's rect(), kept for an absolutely positioned box (lay_box_paint) */
        uint32_t kc = c0 + CSS_PROP_CLIP;
        e->cssclip = st->cval[kc].unit == CSS_UNIT_TEXT && st->cval[kc].text.len ? kc : 0;
        if (e->cssclip) e->paints = 1;
    }
    lay_word_of(st, c0 + CSS_PROP_FLEX_DIRECTION, w);
    e->frow = !(lay_w(w, "column") || lay_w(w, "column-reverse"));
    e->frev = lay_w(w, "row-reverse") || lay_w(w, "column-reverse");
    lay_word_of(st, c0 + CSS_PROP_FLEX_WRAP, w);
    e->fwrap = lay_w(w, "wrap") ? 1 : lay_w(w, "wrap-reverse") ? 2 : 0;
    e->grow = st->cval[c0 + CSS_PROP_FLEX_GROW].num;
    e->shrink = st->cval[c0 + CSS_PROP_FLEX_SHRINK].num;
    c = c0 + CSS_PROP_FLEX_BASIS;
    lay_word_of(st, c, w);
    e->basis = lay_len_at(st, c);
    if (lay_w(w, "content")) e->basis.unit = LAY_MAXC;
    e->order = (int32_t)st->cval[c0 + CSS_PROP_ORDER].num;
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_CONTENT, w);
    e->jc = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_ITEMS, w);
    e->ai = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_SELF, w);
    e->as = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_ALIGN_CONTENT, w);
    e->ac = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_ITEMS, w);
    e->ji = (uint8_t)lay_align_of(w);
    lay_word_of(st, c0 + CSS_PROP_JUSTIFY_SELF, w);
    e->js = (uint8_t)lay_align_of(w);
    for (int k = 0; k < 2; k++) {
        c = c0 + (k == 0 ? CSS_PROP_ROW_GAP : CSS_PROP_COLUMN_GAP);
        lay_len g = lay_len_at(st, c);
        if (st->cval[c].unit == CSS_UNIT_KEYWORD) { g.unit = LAY_PX; g.v = 0; }
        if (k == 0) e->rgap = g; else e->cgap = g;
    }
    e->minw = lay_len_at(st, c0 + (fr ? CSS_PROP_MIN_HEIGHT : CSS_PROP_MIN_WIDTH));
    e->minh = lay_len_at(st, c0 + (fr ? CSS_PROP_MIN_WIDTH : CSS_PROP_MIN_HEIGHT));
    c = c0 + (fr ? CSS_PROP_MAX_WIDTH : CSS_PROP_MAX_HEIGHT);
    if (st->cval[c].unit != CSS_UNIT_KEYWORD) e->maxh = lay_len_at(st, c);
    e->hraw = lay_len_at(st, c0 + (fr ? CSS_PROP_WIDTH : CSS_PROP_HEIGHT));
    e->gtc = c0 + CSS_PROP_GRID_TEMPLATE_COLUMNS;
    e->gtr = c0 + CSS_PROP_GRID_TEMPLATE_ROWS;
    e->gac = c0 + CSS_PROP_GRID_AUTO_COLUMNS;
    e->gar = c0 + CSS_PROP_GRID_AUTO_ROWS;
    e->gflow = c0 + CSS_PROP_GRID_AUTO_FLOW;
    e->gcs = c0 + CSS_PROP_GRID_COLUMN_START;
    e->gce = c0 + CSS_PROP_GRID_COLUMN_END;
    e->grs = c0 + CSS_PROP_GRID_ROW_START;
    e->gre = c0 + CSS_PROP_GRID_ROW_END;
    e->gta = c0 + CSS_PROP_GRID_TEMPLATE_AREAS;
    /* paint's: values that are text are kept when they are not their initial ones */
    for (int k = 0; k < 4; k++) {
        e->rad[k] = lay_len_at(st, c0 + CSS_PROP_BORDER_TOP_LEFT_RADIUS + k);
        if (e->rad[k].unit != LAY_PX || e->rad[k].v != 0 || st->cval[e->rad[k].cv].unit == CSS_UNIT_TEXT) e->paints = 1;
    }
    e->opacity = st->cval[c0 + CSS_PROP_OPACITY].num;
    if (st->cval[c0 + CSS_PROP_OPACITY].unit != CSS_UNIT_NUMBER) e->opacity = 1;
    if (e->opacity < 1) e->paints = 1;
    lay_word_of(st, c0 + CSS_PROP_OUTLINE_STYLE, w);
    e->ostyle = (uint8_t)(lay_w(w, "auto") ? LAY_BS_SOLID : lay_border_style(w));
    e->ow = e->ostyle > LAY_BS_HIDDEN ? st->cval[c0 + CSS_PROP_OUTLINE_WIDTH].num : 0;
    e->ocolor = st->cval[c0 + CSS_PROP_OUTLINE_COLOR].color;
    e->ooffset = st->cval[c0 + CSS_PROP_OUTLINE_OFFSET].num;
    if (e->ow > 0) e->paints = 1;
    static const int text_props[17] = { CSS_PROP_BOX_SHADOW, CSS_PROP_BACKGROUND_IMAGE, CSS_PROP_BACKGROUND_POSITION, CSS_PROP_BACKGROUND_SIZE,
                                        CSS_PROP_BACKGROUND_REPEAT, CSS_PROP_TRANSFORM, CSS_PROP_TRANSFORM_ORIGIN, CSS_PROP_TEXT_SHADOW,
                                        CSS_PROP_BACKGROUND_CLIP, CSS_PROP_BACKGROUND_ORIGIN, CSS_PROP_FILTER, CSS_PROP_BACKDROP_FILTER,
                                        CSS_PROP_PERSPECTIVE, CSS_PROP_PERSPECTIVE_ORIGIN, CSS_PROP_TRANSLATE, CSS_PROP_ROTATE, CSS_PROP_SCALE };
    uint32_t got[17];
    for (int k = 0; k < 17; k++) {
        uint32_t c = c0 + (uint32_t)text_props[k];
        lay_word_of(st, c, w);
        int initial = st->cval[c].how == CSS_HOW_INITIAL || lay_w(w, "none");
        got[k] = initial ? 0 : c;
    }
    e->shadows = got[0];
    e->bgimg = got[1];
    e->bgpos = got[2];
    e->bgsize = got[3];
    e->bgrepeat = got[4];
    e->xform = got[5];
    e->xorigin = got[6];
    e->tshadow = got[7];
    e->bgclip = got[8];
    e->bgorigin = got[9];
    e->filter = got[10];
    e->backdrop = got[11];
    e->persp = got[12];
    e->porigin = got[13];
    e->translate = got[14];
    e->rotate = got[15];
    e->scale = got[16];
    lay_word_of(st, c0 + CSS_PROP_TRANSFORM_STYLE, w);
    e->p3d = lay_w(w, "preserve-3d");
    lay_word_of(st, c0 + CSS_PROP_BACKFACE_VISIBILITY, w);
    e->backface = lay_w(w, "hidden");
    lay_word_of(st, c0 + CSS_PROP_VISIBILITY, w);
    e->hidden = (uint8_t)(lay_w(w, "hidden") || lay_w(w, "collapse"));
    if (e->shadows || e->bgimg || e->xform || e->tshadow || e->bgclip || e->bgorigin || e->filter || e->backdrop || e->persp || e->translate ||
        e->rotate || e->scale || e->p3d || e->backface) e->paints = 1;
}
/* the physical offset k (0 top, 1 right, 2 bottom, 3 left) of a style (its values from c0, lay_ext's) as lay_ext_make
   reads it, unturned: auto with no style */
static lay_len lay_offp(const css_style *st, uint32_t c0, int k) {
    lay_len l;
    if (c0) return lay_len_at(st, c0 + CSS_PROP_TOP + k);
    memset(&l, 0, sizeof l);
    l.unit = LAY_AUTO;
    return l;
}

static void lay_style_of(LCTX, uint32_t x, lay_style *s) {
    if (x & LAY_GEN) { *s = at(*sc, at(*gx, x & LAY_GIDX).style); return; }
    uint32_t si = x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0;
    *s = at(*sc, si);
}
/* the style of el's pseudo-element pk (::first-line, ::first-letter, ::marker,
   ::placeholder) into s: 1, or 0 when it has none */
static int lay_pseudo_style(LCTX, uint32_t el, uint32_t pk, lay_style *s) {
    if ((el & LAY_GEN) || el >= len(*sx) || !(at(*sx, el) & LAY_HAS_OTHER)) return 0;
    int64_t g = lay_gen_find(gx, el, pk);
    if (g < 0) return 0;
    *s = at(*sc, at(*gx, (size_t)g).style);
    return 1;
}

/* ---- the DOM, as layout walks it ---- */

/* the HTML element of that name (an SVG or MathML element of the name is none) */
static int lay_is_named(const dom_document *d, uint32_t x, const char *name) {
    return !(x & LAY_GEN) && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, name, strlen(name));
}
/* an SVG document fragment: its svg element, in the SVG namespace */
static int lay_is_svg(const dom_document *d, uint32_t x) {
    return !(x & LAY_GEN) && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, "svg", 3);
}
/* an element drawn by its own content rules: an image, a form control, an embedding */
static int lay_replaced(const dom_document *d, uint32_t x) {
    return lay_is_named(d, x, "img") || lay_is_named(d, x, "input") || lay_is_named(d, x, "textarea") ||
           lay_is_named(d, x, "select") || lay_is_named(d, x, "iframe") || lay_is_named(d, x, "video") ||
           lay_is_named(d, x, "progress") || lay_is_named(d, x, "meter") ||
           lay_is_named(d, x, "canvas") || lay_is_named(d, x, "embed") || lay_is_named(d, x, "object") ||
           lay_is_svg(d, x);
}
static uint32_t lay_attr(const dom_document *d, uint32_t x, const char *name);
/* x a details element without open: its text directly inside is not drawn, as its other
   content is not (the cascade's user agent rule hides its elements but its first summary) */
static int lay_closed_details(const dom_document *d, uint32_t x) {
    return !(x & LAY_GEN) && lay_is_named(d, x, "details") && !lay_attr(d, x, "open");
}
static uint32_t lay_attr(const dom_document *d, uint32_t x, const char *name) {
    if (x & LAY_GEN) return 0;
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at = d->node[x].first_attr; at; at = d->attr[at].next) if (d->attr[at].name == a) return at;
    return 0;
}
static long lay_attr_int(const dom_document *d, uint32_t x, const char *name, long dflt) {
    uint32_t a = lay_attr(d, x, name);
    if (!a) return dflt;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    long n = 0;
    int any = 0;
    for (size_t i = 0; i < v.len && p[i] >= '0' && p[i] <= '9' && n < 100000; i++) { n = n * 10 + (p[i] - '0'); any = 1; }
    return any ? n : dflt;
}

static int lay_display_of(LCTX, uint32_t x) {
    if (x & LAY_GTEXT) return D_INLINE;
    if (x & LAY_GEN) {
        int gd = at(*sc, at(*gx, x & LAY_GIDX).style).display;
        return gd == D_CONTENTS ? D_INLINE : gd;
    }
    if (d->node[x].kind == DOM_NODE_KIND_TEXT) return D_INLINE;
    if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) return D_NONE;
    int dsp = at(*sc, x < len(*sx) ? at(*sx, x) & LAY_SMASK : 0).display;
    if (dsp == D_INLINE && lay_replaced(d, x)) return D_INLINE_BLOCK;
    if (lay_is_named(d, x, "input")) {
        uint32_t a = lay_attr(d, x, "type");
        if (a && d->attr[a].value.len == 6 && memcmp(text_ptr(d, d->attr[a].value), "hidden", 6) == 0) return D_NONE;
    }
    return dsp;
}
static int lay_block_level(int dsp) {
    return dsp == D_BLOCK || dsp == D_LIST_ITEM || dsp == D_TABLE || dsp == D_ROW_GROUP || dsp == D_ROW || dsp == D_CELL || dsp == D_CAPTION;
}
/* an inline element with a block-level box inside it, in its flow (an absolutely positioned
   or floated child, which the cascade made a block, holds no place there) */
static int lay_holds_block(LCTX, uint32_t x) {
    if (x & LAY_GEN) return 0;                  /* a pseudo-element holds its text alone */
    for (uint32_t c = dom_flat_first(d, x); c; c = dom_flat_next(d, c)) {
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_NONE) continue;
        if (lay_block_level(dsp)) {
            lay_ext ce = LAY_X(c);
            if (ce.pos == POS_ABSOLUTE || ce.pos == POS_FIXED || ce.flt) continue;
            return 1;
        }
        if ((dsp == D_INLINE || dsp == D_CONTENTS) && d->node[c].kind == DOM_NODE_KIND_ELEMENT && lay_holds_block(LARGS, c)) return 1;
    }
    return 0;
}

/* a container's children as segments: a run of inline-level nodes, a block-level element,
   an absolutely positioned one (out of the flow), a float */
enum { SEG_RUN, SEG_BLOCK, SEG_ABS, SEG_FLOAT };
typedef struct lay_seg { uint32_t node; uint32_t first, n; uint8_t block; } lay_seg;

static void lay_item(vector<lay_seg> *segs, vector<uint32_t> *items, uint32_t node) {
    if (len(*segs) == 0 || back(*segs).block) {
        lay_seg s;
        s.node = 0;
        s.first = (uint32_t)len(*items);
        s.n = 0;
        s.block = 0;
        push_back(segs, s);
    }
    push_back(items, node);
    (*segs)[len(*segs) - 1].n++;
}
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items);

/* a fieldset's rendered legend (HTML 15.3.13): its first legend child that is drawn, not floated and not positioned
   absolutely; 0 for none, and for what is no fieldset. Laid out by its fieldset on its top border, not in its flow */
static uint32_t lay_rendered_legend(LCTX, uint32_t x) {
    if ((x & LAY_GEN) || !lay_is_named(d, x, "fieldset")) return 0;
    for (uint32_t c = dom_flat_first(d, x); c; c = dom_flat_next(d, c)) {
        if (!lay_is_named(d, c, "legend")) continue;
        if (lay_display_of(LARGS, c) == D_NONE) continue;
        uint32_t si = lay_si(sx, gx, c);
        int pos = at(*xc, si).pos;
        if (at(*xc, si).flt || pos == POS_ABSOLUTE || pos == POS_FIXED) continue;
        return c;
    }
    return 0;
}

/* an element child (or a pseudo-element) of a container: a block-level box, its children
   in its place (display: contents, or an inline element that holds a block), or an item */
static void lay_collect_el(LCTX, uint32_t c, vector<lay_seg> *segs, vector<uint32_t> *items) {
    int dsp = lay_display_of(LARGS, c);
    if (dsp == D_NONE) return;
    uint32_t si = lay_si(sx, gx, c);
    int pos = at(*xc, si).pos, flt = at(*xc, si).flt;
    if (pos == POS_ABSOLUTE || pos == POS_FIXED || (flt && pos != POS_ABSOLUTE && pos != POS_FIXED)) {
        /* out of the flow, or floated: in a run of inline content, a piece of it (its static
           position, or where the float is placed from); else a segment of its own */
        if (len(*segs) && back(*segs).block == SEG_RUN) { lay_item(segs, items, c); return; }
        lay_seg s;
        s.node = c;
        s.first = 0;
        s.n = 0;
        s.block = (pos == POS_ABSOLUTE || pos == POS_FIXED) ? SEG_ABS : SEG_FLOAT;
        push_back(segs, s);
        return;
    }
    if (dsp == D_CONTENTS || (dsp == D_INLINE && lay_holds_block(LARGS, c))) { lay_collect(LARGS, c, segs, items); return; }
    if (lay_block_level(dsp)) {
        lay_seg s;
        s.node = c;
        s.first = 0;
        s.n = 0;
        s.block = SEG_BLOCK;
        push_back(segs, s);
        return;
    }
    lay_item(segs, items, c);
}/* a container's children: its ::before first, its ::after last; a pseudo-element's, its text */
static void lay_collect(LCTX, uint32_t parent, vector<lay_seg> *segs, vector<uint32_t> *items) {
    if (parent & LAY_GEN) { lay_item(segs, items, parent | LAY_GTEXT); return; }
    uint32_t fl0 = parent < len(*sx) ? at(*sx, parent) : 0;
    if (fl0 & LAY_HAS_BEFORE) {
        int64_t g = lay_gen_find(gx, parent, 1);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
    int closed = lay_closed_details(d, parent);
    uint32_t legend = lay_rendered_legend(LARGS, parent);
    for (uint32_t c = dom_flat_first(d, parent); c; c = dom_flat_next(d, c)) {
        int kind = d->node[c].kind;
        if (kind == DOM_NODE_KIND_TEXT) { if (!closed) lay_item(segs, items, c); continue; }
        if (kind != DOM_NODE_KIND_ELEMENT || c == legend) continue;
        lay_collect_el(LARGS, c, segs, items);
    }
    if (fl0 & LAY_HAS_AFTER) {
        int64_t g = lay_gen_find(gx, parent, 2);
        if (g >= 0) lay_collect_el(LARGS, LAY_GEN | (uint32_t)g, segs, items);
    }
}

/* ---- pieces: an inline run as words, spaces, breaks, atomic boxes and inline edges ---- */

enum { P_WORD, P_SPACE, P_BREAK, P_ATOMIC, P_OPEN, P_CLOSE, P_ABS, P_FLOAT };
typedef struct lay_piece {
    uint8_t kind, face, italic, deco, brk, keep;   /* brk: a space a line may break at; keep: a kept (pre) space */
    uint8_t tt;                                    /* text-transform */
    uint8_t va;                                    /* VA_TOP, VA_BOTTOM: aligned with the line box; an atomic's own */
    uint8_t hy;                                    /* a word broken by a hyphen: the hyphen after it (1 Liang's, shaped with the word; 2 a soft hyphen's, shaped apart: lay_frag_glyphs) */
    uint8_t rtl;                                   /* a word that asks for the bidi algorithm (lay_bidi_hot) */
    uint8_t soft;                                  /* a word with soft hyphens in it */
    uint8_t fb;                                    /* a word some of whose code points fallback faces draw */
    uint8_t lhn;                                   /* its line-height normal: the faces its run's shaping draws it
                                                      with bring their lines (fa, fd: lay_shape_pieces) */
    uint8_t sdir;                                  /* its direction for shaping, from its paragraph's bidi levels
                                                      (lay_shape_dirs): 1 left to right, 2 right to left, 3 both; 0
                                                      none resolved */
    uint8_t rrtl;                                  /* its run (run) shaped right to left */
    uint8_t img;                                   /* an img element's atomic box (quirks mode keeps it beside what
                                                      is next to it in a table cell's min-content) */
    uint32_t node, elem;                           /* the text node, or the element; and the text's element */
    uint32_t bu;                                   /* in a bidi paragraph, its first unit (lay_bu) */
    uint32_t run;                                  /* the shaping run it was measured in (lay_shape_pieces): that
                                                      run's first piece's index then, + 1; 0 none */
    uint32_t font;                                 /* a web font's resource, 0 for a built-in face */
    uint8_t orient;                                /* its style's (lay_style) vertical text's orientation (in the padding
                                                      before ls: a piece is no bigger for it) */
    uint8_t bb;                                    /* the break opportunities before it (lay_break_pieces): AMB_BRK_LINE
                                                      a line may break there, AMB_BRK_MIN the min-content width does */
    uint8_t lbf;                                   /* a word ending in a soft hyphen it breaks after (LBF_SHY: a hyphen
                                                      shown when a line ends there); one an overflowing line may break
                                                      anywhere (LBF_EMERG: overflow-wrap) */
    uint8_t hidden;                                /* its style's visibility hidden or collapse (lay_style's; in the
                                                      padding before ls too) */
    double ls, wsp;                                /* letter-spacing, word-spacing */
    uint32_t off, len;                             /* a word's (or a kept space's) bytes of its text node */
    double w, size, lh, asc, desc;
    uint32_t color, fill, bg;
    float aw, ah;                                  /* an atomic box's content width (and a replaced one's height) */
    float fa, fd;                                  /* those faces' lines above and below its baseline, under line-height
                                                      normal (fb) */
    float shift, rx, ry;                           /* its baseline's shift (vertical-align), its relative offset */
} lay_piece;
/* a word's lbf: LBF_SHY, LBF_EMERG (lay_break_pieces); LBF_HOT, a byte of it ASCII's line breaking breaks about or
   one past ASCII (lay_chars: a run with none, nor another of lay_lb_marks's marks, is not broken again); LBF_HANG,
   a run of ideographic spaces (U+3000), which hang at a line's end as spaces do (Blink's IsOtherSpaceSeparator: a
   line's width to fit and to align leaves them out, measured) */
enum { LBF_SHY = 1, LBF_EMERG = 2, LBF_HOT = 4, LBF_HANG = 8 };
/* each byte's class as lay_chars scans a word: LAY_BC_SPACE a space that ends it (space, tab, CR, FF, LF),
   LAY_BC_HOT an ASCII byte line breaking breaks about (LBF_HOT: ( - < ? [ {), LAY_BC_HIGH one past ASCII (LBF_HOT
   too); made once a process */
enum { LAY_BC_SPACE = 1, LAY_BC_HOT = 2, LAY_BC_HIGH = 4 };
/* a count of what a run's line breaking must look at (lay_break_pieces), bumped as pieces are made: a word LBF_HOT,
   a text in a style whose word-break, line-break or overflow-wrap is not its initial value (lay_chars), a wbr
   (lay_node_pieces); lay_run_pieces breaks a run's words where it moved while the run's pieces were made */
static uint32_t lay_lb_marks;
static uint8_t lay_bytecls[256];
static int lay_bytecls_ready;
static void lay_bytecls_make(void) {
    static const char *const sp = " \t\r\f\n", *const hot = "(-<?[{";
    for (size_t q = 0; sp[q]; q++) lay_bytecls[(unsigned char)sp[q]] = LAY_BC_SPACE;
    for (size_t q = 0; hot[q]; q++) lay_bytecls[(unsigned char)hot[q]] = LAY_BC_HOT;
    for (size_t c = 0x80; c < 256; c++) lay_bytecls[c] = LAY_BC_HIGH;
    lay_bytecls_ready = 1;
}

/* the leading a line height leaves about a font's ascent and descent, split as Chromium
   splits it (CalculateLeadingSpace): above, half of it floored to a whole px; below, the
   rest */
static void lay_leading(double lh, double asc, double desc, double *above, double *below) {
    double lead = lh - (asc + desc), up = floor(lead / 2);
    *above = asc + up;
    *below = desc + (lead - up);
}

static void lay_piece_style(lay_piece *p, const lay_style *s) {
    p->face = (uint8_t)s->face;
    p->font = s->font;
    p->ls = s->ls;
    p->wsp = s->wsp;
    p->tt = (uint8_t)s->tt;
    p->italic = (uint8_t)s->italic;
    p->size = s->fs;
    p->lh = s->lh;
    p->asc = s->asc;
    p->desc = s->desc;
    p->lhn = (uint8_t)s->lhn;
    p->color = s->color;
    p->fill = s->fill;
    p->orient = s->orient;
    p->hidden = s->hidden;
}

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);
static void lay_grid_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);

/* an svg attribute's length as an absolute length in px (a number is px; em and ex of the
   font size), or -1 for none: absent, a percentage, auto */
static double lay_svg_len(const dom_document *d, uint32_t el, const char *name, double fs) {
    uint32_t a = lay_attr(d, el, name);
    if (!a) return -1;
    vector<char> v;
    append_range(v, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
    size_t n = len(v), i = 0;
    while (i < n && (at(v, i) == ' ' || at(v, i) == '\t' || at(v, i) == '\n')) i++;
    size_t b = i;
    if (i < n && at(v, i) == '+') i++;
    double x = 0, frac = 0.1;
    int digits = 0;
    while (i < n && at(v, i) >= '0' && at(v, i) <= '9') { x = x * 10 + (at(v, i) - '0'); i++; digits++; }
    if (i < n && at(v, i) == '.') {
        i++;
        while (i < n && at(v, i) >= '0' && at(v, i) <= '9') { x += (at(v, i) - '0') * frac; frac /= 10; i++; digits++; }
    }
    if (!digits || i == b) return -1;
    size_t u = i;
    while (u < n && at(v, u) >= 'a' && at(v, u) <= 'z') u++;
    size_t e = u;
    while (e < n && (at(v, e) == ' ' || at(v, e) == '\t' || at(v, e) == '\n')) e++;
    if (e != n) return -1;
    size_t ul = u - i;
    if (ul == 0) return x;
    if (ul != 2) return -1;
    char c0 = at(v, i), c1 = at(v, i + 1);
    if (c0 == 'p' && c1 == 'x') return x;
    if (c0 == 'i' && c1 == 'n') return x * 96;
    if (c0 == 'c' && c1 == 'm') return x * 96 / 2.54;
    if (c0 == 'm' && c1 == 'm') return x * 96 / 25.4;
    if (c0 == 'p' && c1 == 't') return x * 4 / 3;
    if (c0 == 'p' && c1 == 'c') return x * 16;
    if (c0 == 'e' && c1 == 'm') return x * fs;
    if (c0 == 'e' && c1 == 'x') return x * fs / 2;
    return -1;
}
/* an svg element's viewBox's ratio, width over height: 0 for none */
static double lay_svg_ratio(const dom_document *d, uint32_t el) {
    uint32_t a = lay_attr(d, el, "viewBox");
    if (!a) return 0;
    vector<char> v;
    append_range(v, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
    push_back(v, 0);
    double n[4];
    size_t i = 0;
    for (int k = 0; k < 4; k++) {
        while (at(v, i) == ' ' || at(v, i) == ',' || at(v, i) == '\t' || at(v, i) == '\n' || at(v, i) == '\r') i++;
        size_t b = i;
        if (at(v, i) == '-' || at(v, i) == '+') i++;
        while ((at(v, i) >= '0' && at(v, i) <= '9') || at(v, i) == '.' || at(v, i) == 'e' || at(v, i) == 'E' ||
               ((at(v, i) == '-' || at(v, i) == '+') && i > b && (at(v, i - 1) == 'e' || at(v, i - 1) == 'E'))) i++;
        if (i == b) return 0;
        char num[48];
        size_t nl = i - b < sizeof num - 1 ? i - b : sizeof num - 1;
        memcpy(num, data(v) + b, nl);
        num[nl] = 0;
        n[k] = atof(num);
    }
    return n[2] > 0 && n[3] > 0 ? n[2] / n[3] : 0;
}
/* an svg element's content box (CSS 2.1, 10.3.2 and 10.6.2, as Chromium sizes one): its
   width and height attributes are its CSS width and height (presentation attributes, the
   cascade's); its natural size their absolute lengths, and its ratio its viewBox's (else
   theirs). Neither declared, a ratio alone fills the containing block's width, and with no
   ratio nor size it is 300 x 150 */
static void lay_svg_size(LCTX, uint32_t el, const lay_style *s, double cw, double *w, double *h) {
    double nw = lay_svg_len(d, el, "width", s->fs), nh = lay_svg_len(d, el, "height", s->fs), ratio = lay_svg_ratio(d, el);
    if (ratio <= 0 && nw > 0 && nh > 0) ratio = nw / nh;
    int wa = s->width.unit == LAY_AUTO, ha = s->height.unit == LAY_AUTO;
    if (!wa) *w = lay_res(st, s->width, cw);
    if (!ha) *h = s->height.v;
    if (wa && ha) {
        if (nw >= 0) { *w = nw; *h = nh >= 0 ? nh : ratio > 0 ? nw / ratio : 150; }
        else if (nh >= 0) { *h = nh; *w = ratio > 0 ? nh * ratio : 300; }
        else if (ratio > 0) {
            double edges = lay_res(st, s->m[1], cw) + lay_res(st, s->m[3], cw) + s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) +
                           lay_res(st, s->p[3], cw);
            *w = cw - edges > 0 ? cw - edges : 0;
            *h = *w / ratio;
        } else { *w = 300; *h = 150; }
    } else if (wa) *w = ratio > 0 ? *h * ratio : nw >= 0 ? nw : 300;
    else if (ha) *h = ratio > 0 ? *w / ratio : nh >= 0 ? nh : 150;
}

/* the content box of an atomic inline: a replaced element's size, or an inline-block's
   width shrunk to fit within avail */
static int lay_alt_text(const dom_document *d, uint32_t el, vector<char> *out);
/* an image whose load failed (its request broken: no URL, refused, not found ...); or one with no image to load
   (no src, no srcset: the loader made no request) and alt text, which Chromium gives its fallback as it gives a
   broken image's */
static int lay_img_failed(LCTX, uint32_t el) {
    if (!nt || !lay_is_named(d, el, "img")) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++)
        if (nt->resource[r].live && nt->resource[r].elem.index == el && nt->resource[r].kind == NET_KIND_IMAGE)
            return nt->resource[r].outcome != NET_OUTCOME_OK;
    vector<char> alt;
    return lay_alt_text(d, el, &alt) && len(alt) > 0;
}
/* an img with no image to load: no src, no srcset, no source of its picture chosen (the loader made no request) */
static int lay_img_unsourced(LCTX, uint32_t el) {
    if (!nt || !lay_is_named(d, el, "img")) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++)
        if (nt->resource[r].live && nt->resource[r].elem.index == el && nt->resource[r].kind == NET_KIND_IMAGE) return 0;
    return 1;
}
/* a broken image laid out as a box of its declared size, its alt text in it: in quirks mode, an img with a
   width or a height declared (Blink's fallback); standards mode's is lay_atomic_size's */
static int lay_img_sized(LCTX, uint32_t el, const lay_style *s) {
    return lay_quirks && lay_img_failed(LARGS, el) && (s->width.unit != LAY_AUTO || s->height.unit != LAY_AUTO);
}
/* an img's alt text, its white space runs one space and trimmed; 0 when it has no alt attribute, 1 when it has one */
static int lay_alt_text(const dom_document *d, uint32_t el, vector<char> *out) {
    clear(*out);
    uint32_t a = lay_attr(d, el, "alt");
    if (!a) return 0;
    amb_span v = d->attr[a].value;
    const char *src = text_ptr(d, v);
    int sp = 1;
    for (size_t i = 0; i < v.len; i++) {
        char c = src[i];
        int white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        if (white) { if (!sp) push_back(out, ' '); sp = 1; continue; }
        push_back(out, c);
        sp = 0;
    }
    while (len(*out) && back(*out) == ' ') pop_back(out);
    return 1;
}

/* ---- form controls: what each is drawn as, and Chromium's sizes for them ---- */

/* an input element's kind, by its type attribute in any case (HTML 4.10.5): the text field's for text, search,
   email, url, tel, number and any type HTML does not know */
enum { LI_TEXT, LI_PASSWORD, LI_CHECK, LI_RADIO, LI_BUTTON, LI_SUBMIT, LI_RESET, LI_RANGE, LI_COLOR, LI_FILE, LI_DATE, LI_IMAGE, LI_HIDDEN };
static int lay_input_kind(const dom_document *d, uint32_t el) {
    static const char *const names[15] = { "password", "checkbox", "radio", "button", "submit", "reset", "range", "color", "file", "date",
                                           "datetime-local", "month", "time", "week", "image" };
    static const int kinds[15] = { LI_PASSWORD, LI_CHECK, LI_RADIO, LI_BUTTON, LI_SUBMIT, LI_RESET, LI_RANGE, LI_COLOR, LI_FILE, LI_DATE,
                                   LI_DATE, LI_DATE, LI_DATE, LI_DATE, LI_IMAGE };
    uint32_t a = lay_attr(d, el, "type");
    if (!a) return LI_TEXT;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    for (int k = 0; k < 15; k++) {
        size_t n = strlen(names[k]);
        if (v.len != n) continue;
        size_t i = 0;
        while (i < n && (p[i] >= 'A' && p[i] <= 'Z' ? p[i] + 32 : p[i]) == names[k][i]) i++;
        if (i == n) return kinds[k];
    }
    return LI_TEXT;
}

/* the text of x's descendants (a script's passed over), ASCII white space stripped and collapsed: an option's text */
static void lay_collapsed_text(const dom_document *d, uint32_t x, string *out, int *sp, int depth) {
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) {
            amb_span v = d->node[c].data;
            const char *src = text_ptr(d, v);
            for (size_t i = 0; i < v.len; i++) {
                char ch = src[i];
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') { if (!*sp) push_back(*out, ' '); *sp = 1; continue; }
                push_back(*out, ch);
                *sp = 0;
            }
        } else if (d->node[c].kind == DOM_NODE_KIND_ELEMENT && depth < 32 && !lay_is_named(d, c, "script")) lay_collapsed_text(d, c, out, sp, depth + 1);
    }
}
/* an option's label (HTML 4.10.10): its label attribute when it is not empty, else its text */
static void lay_option_label(const dom_document *d, uint32_t opt, string *out) {
    clear(*out);
    uint32_t a = lay_attr(d, opt, "label");
    if (a && d->attr[a].value.len) { append(*out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return; }
    int sp = 1;
    lay_collapsed_text(d, opt, out, &sp, 0);
    while (len(*out) && back(*out) == ' ') pop_back(*out);
}
/* a select's list box or drop-down (HTML 4.10.7): a list box when it has multiple or a size above 1, its rows the
   size (4 for multiple without one); its options' widest label in s's font, in *widest; its selected option's label
   (the last with selected, else the first that is not disabled, else the first) in *shown. The options are its
   children's and its optgroups' children's */
static int lay_select(LCTX, uint32_t el, const lay_style *s, double *widest, string *shown, long *rows) {
    int multiple = lay_attr(d, el, "multiple") != 0;
    long size = lay_attr_int(d, el, "size", multiple ? 4 : 1);
    if (size < 1) size = multiple ? 4 : 1;
    *rows = size;
    *widest = 0;
    clear(*shown);
    uint32_t chosen = 0, first = 0, first_enabled = 0;
    string label;
    for (uint32_t c = d->node[el].first_child; c; c = d->node[c].next_sibling) {
        uint32_t g = lay_is_named(d, c, "optgroup") ? d->node[c].first_child : c, end = lay_is_named(d, c, "optgroup") ? 0 : d->node[c].next_sibling;
        for (uint32_t o = g; o && o != end; o = d->node[o].next_sibling) {
            if (!lay_is_named(d, o, "option")) { if (o == c) break; continue; }
            lay_option_label(d, o, &label);
            double w = amb_text_width(f, s->face, s->fs, data(label), len(label)) + s->ls * (double)lay_cps(data(label), len(label));
            if (o != c) w += 4 * amb_text_width(f, s->face, s->fs, " ", 1);   /* in an optgroup: indented by four spaces */
            if (w > *widest) *widest = w;
            if (!first) first = o;
            if (!first_enabled && !lay_attr(d, o, "disabled")) first_enabled = o;
            if (lay_attr(d, o, "selected")) chosen = o;
            if (o == c) break;
        }
    }
    uint32_t o = chosen ? chosen : first_enabled ? first_enabled : first;
    if (o) lay_option_label(d, o, shown);
    return multiple || size > 1;
}
/* the character width a text control is sized by (Blink's GetAvgCharWidth), its face's at s: its OS/2 average,
   which Chromium takes as it is where it rounds down and rounded where it rounds up (measured on 212 fields of six
   faces at 28 sizes, all but three to the px); a face with none, a "0"'s width. *maxw: the widest character's
   (head's extent, rounded), 0 for a face without an average */
static double lay_char_width(LCTX, const lay_style *s, double *maxw) {
    double avg = 0, mx = 0;
    amb_face_widths(f, s->face, s->fs, &avg, &mx);
    if (avg <= 0) { *maxw = 0; return amb_text_width(f, s->face, s->fs, "0", 1); }
    *maxw = mx;
    return fmax(floor(avg + 0.5), avg);
}
/* a button input's label: its value, or the default its type has (Chromium's: Submit, Reset, none) */
static void lay_button_label(const dom_document *d, uint32_t el, int kind, string *out) {
    clear(*out);
    uint32_t a = lay_attr(d, el, "value");
    if (a) { append(*out, text_ptr(d, d->attr[a].value), d->attr[a].value.len); return; }
    append(*out, kind == LI_SUBMIT ? "Submit" : kind == LI_RESET ? "Reset" : "");
}

static double lay_attr_num(const dom_document *d, uint32_t el, const char *name, double dflt);

/* a date or time field's kind, by its type attribute: date, datetime-local, month, time, week */
enum { LD_DATE, LD_DATETIME, LD_MONTH, LD_TIME, LD_WEEK };
static int lay_date_kind(const dom_document *d, uint32_t el) {
    static const char *const names[5] = { "date", "datetime-local", "month", "time", "week" };
    uint32_t a = lay_attr(d, el, "type");
    if (!a) return LD_DATE;
    const char *p = text_ptr(d, d->attr[a].value);
    size_t n = d->attr[a].value.len;
    for (int k = 0; k < 5; k++) {
        size_t m = strlen(names[k]), i = 0;
        if (m != n) continue;
        while (i < m && (p[i] >= 'A' && p[i] <= 'Z' ? p[i] + 32 : p[i]) == names[k][i]) i++;
        if (i == m) return k;
    }
    return LD_DATE;
}

/* digits at s[*i], exactly n of them (n 0: at least four, a year's), as a number; -1 when they are not */
static long lay_digits(const char *s, size_t len, size_t *i, int n) {
    long v = 0;
    size_t k = *i;
    while (k < len && s[k] >= '0' && s[k] <= '9' && (n == 0 || k - *i < (size_t)n)) { v = v * 10 + (s[k] - '0'); k++; if (v > 1000000000L) return -1; }
    if (n ? k - *i != (size_t)n : k - *i < 4) return -1;
    *i = k;
    return v;
}
static int lay_days_in(long y, long m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : dm[m - 1];
}
/* the weeks of year y (ISO 8601): 53 when it begins on a Thursday, or a leap year on a Wednesday */
static int lay_weeks_in(long y) {
    long a = (y - 1) % 400, dow = (1 + 5 * (a % 4) + 4 * (a % 100) + 6 * a) % 7;   /* 1 January: 0 Sunday (Gauss) */
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return dow == 4 || (leap && dow == 3) ? 53 : 52;
}

/* a date or time field's value (HTML 2.3.5): its year, month (or week), day, hour, minute, second and millisecond,
   -1 each it has not; 1 when the value is one of its kind's strings */
static int lay_date_value(const char *s, size_t n, int kind, long *v) {
    for (int k = 0; k < 7; k++) v[k] = -1;
    size_t i = 0;
    if (kind != LD_TIME) {
        long y = lay_digits(s, n, &i, 0);
        if (y < 1 || i >= n || s[i] != '-') return 0;
        i++;
        v[0] = y;
        if (kind == LD_WEEK) {
            if (i >= n || s[i] != 'W') return 0;
            i++;
            long w = lay_digits(s, n, &i, 2);
            if (w < 1 || w > lay_weeks_in(y)) return 0;
            v[1] = w;
            return i == n;
        }
        long m = lay_digits(s, n, &i, 2);
        if (m < 1 || m > 12) return 0;
        v[1] = m;
        if (kind == LD_MONTH) return i == n;
        if (i >= n || s[i] != '-') return 0;
        i++;
        long dd = lay_digits(s, n, &i, 2);
        if (dd < 1 || dd > lay_days_in(y, m)) return 0;
        v[2] = dd;
        if (kind == LD_DATE) return i == n;
        if (i >= n || (s[i] != 'T' && s[i] != ' ')) return 0;
        i++;
    }
    long hh = lay_digits(s, n, &i, 2);
    if (hh < 0 || hh > 23 || i >= n || s[i] != ':') return 0;
    i++;
    long mm = lay_digits(s, n, &i, 2);
    if (mm < 0 || mm > 59) return 0;
    v[3] = hh;
    v[4] = mm;
    v[5] = 0;
    v[6] = 0;
    if (i < n && s[i] == ':') {
        i++;
        long ss = lay_digits(s, n, &i, 2);
        if (ss < 0 || ss > 59) return 0;
        v[5] = ss;
        if (i < n && s[i] == '.') {
            i++;
            size_t f0 = i;
            long ms = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9' && i - f0 < 3) { ms = ms * 10 + (s[i] - '0'); i++; }
            if (i == f0) return 0;
            for (size_t k = i - f0; k < 3; k++) ms *= 10;
            v[6] = ms;
        }
    }
    return i == n;
}

/* a date or time field's parts as Chromium's DateTimeEditElement builds them for en-US: its fields, each its value's
   text or else its placeholder, and the texts between them (month/day/year, hour:minute[:second[.ms]] AM/PM, a month's
   name and its year, Week ww, yyyy). Each part's text into texts (NUL-separated), whether it is a field into fields;
   the edit's width: each field the widest of its placeholder, its largest value's text and its value's, and 2px
   (DateTimeFieldElement::MaximumWidth), the texts between them as they are, ceiled (CustomStyleForLayoutObject) */
/* a text's width in s's face, size and letter spacing */
static double lay_dw(LCTX, const lay_style *s, const char *q) {
    size_t n = strlen(q);
    return amb_text_width(f, s->face, s->fs, q, n) + s->ls * (double)lay_cps(q, n);
}
/* one part of a date field: its text appended to texts (NUL-ended), whether it is a field to fields; its width for the
   edit's: a field's the widest of its shown text, its placeholder and its largest value's, and 2px */
static double lay_dpart(LCTX, const lay_style *s, vector<char> *texts, vector<uint8_t> *fields, int field, const char *shown,
                        const char *ph, const char *mx) {
    append_range(*texts, shown, strlen(shown));
    push_back(*texts, (char)0);
    push_back(*fields, (uint8_t)field);
    double w = lay_dw(LARGS, s, shown);
    if (field) w = fmax(w, fmax(lay_dw(LARGS, s, ph), lay_dw(LARGS, s, mx))) + 2;
    return w;
}
static double lay_date_parts(LCTX, uint32_t el, const lay_style *s, vector<char> *texts, vector<uint8_t> *fields) {
    static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                            "September", "October", "November", "December" };
    int kind = lay_date_kind(d, el);
    long v[7];
    uint32_t a = lay_attr(d, el, "value");
    int has = a && lay_date_value(text_ptr(d, d->attr[a].value), d->attr[a].value.len, kind, v);
    if (!has) for (int k = 0; k < 7; k++) v[k] = -1;
    /* a second's field when the value has seconds or the step is not whole minutes; a millisecond's when the value
       has them or the step is not whole seconds (Blink's ShouldHaveSecondField, ShouldHaveMillisecondField) */
    double step = lay_attr_num(d, el, "step", 60);
    if (!(step > 0)) step = 60;
    int sec = (has && (v[5] > 0 || v[6] > 0)) || fmod(step, 60) != 0, msf = (has && v[6] > 0) || fmod(step, 1) != 0;
    char buf[32];
    double total = 0;
    clear(*texts);
    clear(*fields);
    if (kind == LD_DATE || kind == LD_DATETIME) {
        if (has) snprintf(buf, sizeof buf, "%02ld", v[1]);
        else snprintf(buf, sizeof buf, "%s", "mm");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "mm", "12");
        total += lay_dpart(LARGS, s, texts, fields, 0, "/", "", "");
        if (has) snprintf(buf, sizeof buf, "%02ld", v[2]);
        else snprintf(buf, sizeof buf, "%s", "dd");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "dd", "31");
        total += lay_dpart(LARGS, s, texts, fields, 0, "/", "", "");
        if (has) snprintf(buf, sizeof buf, "%04ld", v[0]);
        else snprintf(buf, sizeof buf, "%s", "yyyy");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "yyyy", "275760");
        if (kind == LD_DATETIME) total += lay_dpart(LARGS, s, texts, fields, 0, ", ", "", "");
    }
    if (kind == LD_TIME || kind == LD_DATETIME) {
        if (has) snprintf(buf, sizeof buf, "%02ld", v[3] % 12 ? v[3] % 12 : 12);
        else snprintf(buf, sizeof buf, "%s", "--");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "--", "12");
        total += lay_dpart(LARGS, s, texts, fields, 0, ":", "", "");
        if (has) snprintf(buf, sizeof buf, "%02ld", v[4]);
        else snprintf(buf, sizeof buf, "%s", "--");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "--", "59");
        if (sec) {
            total += lay_dpart(LARGS, s, texts, fields, 0, ":", "", "");
            if (has) snprintf(buf, sizeof buf, "%02ld", v[5]);
            else snprintf(buf, sizeof buf, "%s", "--");
            total += lay_dpart(LARGS, s, texts, fields, 1, buf, "--", "59");
            if (msf) {
                total += lay_dpart(LARGS, s, texts, fields, 0, ".", "", "");
                if (has) snprintf(buf, sizeof buf, "%03ld", v[6]);
                else snprintf(buf, sizeof buf, "%s", "---");
                total += lay_dpart(LARGS, s, texts, fields, 1, buf, "---", "999");
            }
        }
        total += lay_dpart(LARGS, s, texts, fields, 0, " ", "", "");
        total += lay_dpart(LARGS, s, texts, fields, 1, has ? (v[3] < 12 ? "AM" : "PM") : "--", "--",
                           lay_dw(LARGS, s, "AM") >= lay_dw(LARGS, s, "PM") ? "AM" : "PM");
    }
    if (kind == LD_MONTH) {
        int wi = 0;
        for (int k = 1; k < 12; k++) if (lay_dw(LARGS, s, months[k]) > lay_dw(LARGS, s, months[wi])) wi = k;
        total += lay_dpart(LARGS, s, texts, fields, 1, has ? months[v[1] - 1] : "---------", "---------", months[wi]);
        total += lay_dpart(LARGS, s, texts, fields, 0, " ", "", "");
        if (has) snprintf(buf, sizeof buf, "%04ld", v[0]);
        else snprintf(buf, sizeof buf, "%s", "----");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "----", "275760");
    }
    if (kind == LD_WEEK) {
        total += lay_dpart(LARGS, s, texts, fields, 0, "Week ", "", "");
        if (has) snprintf(buf, sizeof buf, "%02ld", v[1]);
        else snprintf(buf, sizeof buf, "%s", "--");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "--", "53");
        total += lay_dpart(LARGS, s, texts, fields, 0, ", ", "", "");
        if (has) snprintf(buf, sizeof buf, "%04ld", v[0]);
        else snprintf(buf, sizeof buf, "%s", "----");
        total += lay_dpart(LARGS, s, texts, fields, 1, buf, "----", "275760");
    }
    return ceil(total - 1e-9);
}
/* a date field's picker: its icon's size (an em, a time field's 1.05, as LayoutUnits hold them) and its box's
   outer width and height (2px of padding about it, a time field's 3px and 8px before it: Chromium's sheet) */
static void lay_date_picker(const dom_document *d, uint32_t el, const lay_style *s, double *size, double *ow, double *oh) {
    int clock = lay_date_kind(d, el) == LD_TIME;
    *size = floor((clock ? 1.05 : 1.0) * s->fs * 64 + 1e-6) / 64;
    *oh = *size + (clock ? 6 : 4);
    *ow = *oh + (clock ? 8 : 0);
}

static void lay_atomic_size_in(LCTX, uint32_t el, const lay_style *s, double cw, double *w, double *h);
static void lay_style_turned(lay_style *s, int cf, int pf);
/* an atomic inline's content size, in its own frame: a picture's (an img's, an svg's, a video's, a canvas's, an
   iframe's ...) in a vertical one sized as in a horizontal one -- its natural width across the line -- and turned */
static void lay_atomic_size(LCTX, uint32_t el, const lay_style *s, double cw, double *w, double *h) {
    int fr = lay_frame(s->wm);
    if (fr && lay_replaced(d, el) && !lay_is_named(d, el, "input") && !lay_is_named(d, el, "textarea") && !lay_is_named(d, el, "select") &&
        !lay_is_named(d, el, "progress") && !lay_is_named(d, el, "meter")) {
        lay_style ps = *s;
        lay_style_turned(&ps, fr, 0);
        double pw, ph;
        lay_atomic_size_in(LARGS, el, &ps, cw, &pw, &ph);
        *w = ph;
        *h = pw;
        return;
    }
    lay_atomic_size_in(LARGS, el, s, cw, w, h);
}
static void lay_atomic_size_in(LCTX, uint32_t el, const lay_style *s, double cw, double *w, double *h) {
    *h = -1;
    if (lay_img_failed(LARGS, el)) {
        /* a broken image is its fallback, as Chromium lays it out (a standards-mode page's): with alt text, its 16px
           icon and the text, at the text's size and line: inline, its declared size passed over (Blink's fallback
           is an inline box then), as a block or an inline block its declared size kept (a block flow's); without,
           the icon's 16px box unless a size is declared; with an empty alt, nothing but a declared size */
        vector<char> alt;
        int has_alt = lay_alt_text(d, el, &alt);
        double ew = s->border_box ? s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw) : 0;
        if (lay_img_sized(LARGS, el, s)) {
            /* quirks mode (Blink's fallback there, measured): a size declared one way is the other way's too, and the
               fallback is a box of that size, its alt text inside it */
            double ww = s->width.unit == LAY_AUTO ? -1 : lay_res(st, s->width, cw) - ew, hh = s->height.unit == LAY_AUTO ? -1 : s->height.v;
            if (ww < 0) ww = hh;
            if (hh < 0) hh = ww;
            *w = ww > 0 ? ww : 0;
            *h = hh > 0 ? hh : 0;
            return;
        }
        if (has_alt && len(alt)) {
            *w = 16 + amb_text_width(f, s->face, s->fs, data(alt), len(alt)) + s->ls * (double)lay_cps(data(alt), len(alt));
            *h = s->lh;
            if (s->display != D_INLINE) {
                if (s->width.unit != LAY_AUTO) *w = lay_res(st, s->width, cw) - ew;
                if (s->height.unit != LAY_AUTO) *h = s->height.v;
                if (*w < 0) *w = 0;
                if (*h < 0) *h = 0;
            }
            return;
        }
        double d0 = has_alt ? 0 : 16;
        *w = s->width.unit == LAY_AUTO ? d0 : lay_res(st, s->width, cw) - ew;
        *h = s->height.unit == LAY_AUTO ? d0 : s->height.v;    /* a border box's height the style made its content's */
        if (*w < 0) *w = 0;
        if (*h < 0) *h = 0;
        return;
    }
    if (lay_is_svg(d, el)) { lay_svg_size(LARGS, el, s, cw, w, h); return; }
    if (lay_replaced(d, el)) {
        double dw = 0, dh = 0, ratio = 0;
        int svg = 0;
        /* a declared width of the border box (box-sizing) less its padding and border (a height the style made its content's) */
        double ew = s->border_box ? s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw) : 0;
        if (lay_is_named(d, el, "img") && nt) {
            /* its size, as the loader read it from the image's header; one dimension
               declared, the other kept in proportion. One a srcset chose is its header's size
               over its density, unrounded, as Blink lays it out (the loader's, like
               naturalWidth, is floored) */
            for (uint32_t r = 1; r < nt->resource_len; r++)
                if (nt->resource[r].live && nt->resource[r].elem.index == el && nt->resource[r].kind == NET_KIND_IMAGE) {
                    dw = nt->resource[r].width;
                    dh = nt->resource[r].height;
                    if (nt->resource[r].srcset && nt->resource[r].density > 0 && nt->resource[r].outcome == NET_OUTCOME_OK) {
                        amb_span body = nt->resource[r].body;
                        uint32_t pw = 0, ph = 0;
                        if (body.len && amb_image_size((const uint8_t *)bytes_ptr(nt, body), body.len, &pw, &ph) && pw && ph) {
                            dw = pw / nt->resource[r].density;
                            dh = ph / nt->resource[r].density;
                        }
                    }
                    double sw, sh;
                    svg = (dw <= 0 || dh <= 0) && nt->resource[r].outcome == NET_OUTCOME_OK && amb_svg_size_of(nt, r, &sw, &sh, &ratio);
                }
            if (dw > 0 && dh > 0) {
                if (s->width.unit != LAY_AUTO && s->height.unit == LAY_AUTO) {
                    *w = lay_res(st, s->width, cw) - ew;
                    if (*w < 0) *w = 0;
                    *h = *w * dh / dw;
                    return;
                }
                if (s->width.unit == LAY_AUTO && s->height.unit != LAY_AUTO) {
                    *h = s->height.v;
                    if (*h < 0) *h = 0;
                    *w = *h * dw / dh;
                    return;
                }
            } else if (svg) {
                /* an SVG image with no natural size: its ratio alone (a viewBox's) sizes it as
                   an svg element's, else the default 300 x 150 */
                int wa = s->width.unit == LAY_AUTO, ha = s->height.unit == LAY_AUTO;
                *w = wa ? 300 : lay_res(st, s->width, cw) - ew;
                *h = ha ? 150 : s->height.v;
                if (ratio > 0 && wa && ha) {
                    double edges = lay_res(st, s->m[1], cw) + lay_res(st, s->m[3], cw) + s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) +
                                   lay_res(st, s->p[3], cw);
                    *w = cw - edges > 0 ? cw - edges : 0;
                    *h = *w / ratio;
                } else if (ratio > 0 && wa) *w = *h * ratio;
                else if (ratio > 0 && ha) *h = *w / ratio;
                return;
            }
        }
        if (lay_is_named(d, el, "input")) {
            /* Chromium's sizes: a check box or radio button 13px square, a range 129 by 16, a colour well 44 by 23
               inside its border and padding, a file chooser 34 "0"s wide, a button its label's width and a line
               high; a text field size characters (20 by default) of its face's average width and its widest
               character's excess over it, ceiled (Blink's TextFieldIntrinsicInlineSize), a line high */
            int kind = lay_input_kind(d, el);
            if (kind == LI_CHECK || kind == LI_RADIO) { dw = 13; dh = 13; }
            else if (kind == LI_RANGE) { dw = 129; dh = 16; }
            else if (kind == LI_COLOR) { dw = 44; dh = 23; }
            else if (kind == LI_FILE) {
                /* Blink's LayoutFileUploadControl: 34 "0"s, or its button, 4px and its label if wider */
                const char *bl = lay_attr(d, el, "multiple") ? "Choose Files" : "Choose File";
                double bw = amb_text_width(f, s->face, s->fs, bl, strlen(bl)) + s->ls * (double)lay_cps(bl, strlen(bl)) + 16;
                double lw = amb_text_width(f, s->face, s->fs, "No file chosen", 14) + s->ls * 14;
                dw = ceil(fmax(34 * amb_text_width(f, s->face, s->fs, "0", 1), bw + 4 + lw) - 1e-9);
                dh = s->lh + 6;
            }
            else if (kind == LI_BUTTON || kind == LI_SUBMIT || kind == LI_RESET) {
                string label;
                lay_button_label(d, el, kind, &label);
                dw = amb_text_width(f, s->face, s->fs, data(label), len(label)) + s->ls * (double)lay_cps(data(label), len(label));
                dh = s->lh;
            } else if (kind == LI_DATE) {
                /* its edit (its fields' widest, lay_date_parts) and its picker, side by side in a flex row; as high as
                   the higher of the edit (a line and its fields' 1px above and below) and the picker */
                vector<char> pt_;
                vector<uint8_t> pf_;
                double ps_, pw_, ph_;
                lay_date_picker(d, el, s, &ps_, &pw_, &ph_);
                dw = lay_date_parts(LARGS, el, s, &pt_, &pf_) + pw_;
                dh = fmax(s->lh + 2, ph_);
            } else {
                /* size counts for a text, search, url, tel, email or password field; a number field with a min and
                   a max is as wide as the longer of them and its spin button's 15px (Blink's NumberInputType) */
                uint32_t ta = lay_attr(d, el, "type");
                int number = ta && d->attr[ta].value.len == 6 && (text_ptr(d, d->attr[ta].value)[0] | 32) == 'n';
                long n = number ? 20 : lay_attr_int(d, el, "size", 20);
                if (n <= 0) n = 20;
                double extra = 0;
                if (number) {
                    uint32_t lo = lay_attr(d, el, "min"), hi = lay_attr(d, el, "max");
                    if (lo && hi && d->attr[lo].value.len && d->attr[hi].value.len) {
                        n = (long)(d->attr[lo].value.len > d->attr[hi].value.len ? d->attr[lo].value.len : d->attr[hi].value.len);
                        extra = 15;
                    }
                }
                double maxw = 0, chw = lay_char_width(LARGS, s, &maxw);
                dw = ceil(chw * (double)n + (maxw > 0 ? maxw - chw : 0) - 1e-6) + extra;
                dh = s->lh;
            }
        } else if (lay_is_named(d, el, "textarea")) {
            /* cols characters of its face's average width, ceiled, and the vertical scroll bar's 15px; rows lines */
            long cols = lay_attr_int(d, el, "cols", 20), rows = lay_attr_int(d, el, "rows", 2);
            if (cols <= 0) cols = 20;
            if (rows <= 0) rows = 2;
            double maxw = 0, chw = lay_char_width(LARGS, s, &maxw);
            dw = ceil(chw * (double)cols - 1e-6) + 15;
            dh = (double)rows * s->lh;
        } else if (lay_is_named(d, el, "select")) {
            /* a drop-down: its widest option's label ceiled, 4px before it and the arrow's 16 after; a line and 2px
               high (of appearance none, the label and the line alone: no theme's padding, measured). A list box: the
               widest label and 4px; its rows, a line and 2px each */
            double widest = 0;
            string shown;
            long rows = 1;
            /* its text a line of the font's ascent and descent, no line gap (Chromium's drop-down) */
            if (lay_select(LARGS, el, s, &widest, &shown, &rows)) { dw = widest + 4; dh = (double)rows * (s->asc + s->desc + 2); }
            else if (LAY_X(el).app_none) { dw = ceil(widest - 1e-6); dh = s->asc + s->desc; }
            else { dw = ceil(widest - 1e-6) + 20; dh = s->asc + s->desc + 2; }
        }
        else if (lay_is_named(d, el, "iframe") || lay_is_named(d, el, "video") || lay_is_named(d, el, "embed") || lay_is_named(d, el, "object")) { dw = 300; dh = 150; }
        else if (lay_is_named(d, el, "canvas") || lay_is_svg(d, el)) { dw = 300; dh = 150; }
        *w = s->width.unit == LAY_AUTO ? dw : lay_res(st, s->width, cw) - ew;
        *h = s->height.unit == LAY_AUTO ? dh : s->height.v;    /* a border box's height the style made its content's */
        if (*w < 0) *w = 0;
        if (*h < 0) *h = 0;
        return;
    }
    double box_edges = s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw);
    if (s->width.unit != LAY_AUTO && !lay_content_sized(s->width)) {
        *w = lay_res(st, s->width, cw) - (s->border_box ? box_edges : 0);
        if (*w < 0) *w = 0;
        return;
    }
    double mn, mx;
    lay_intrinsic(LARGS, el, cw, &mn, &mx);
    double edges = lay_res(st, s->m[1], cw) + lay_res(st, s->m[3], cw) + box_edges;
    mn -= edges;
    mx -= edges;
    double avail = cw - edges;
    double v = avail > mn ? avail : mn;
    *w = s->width.unit == LAY_MINC ? mn : s->width.unit == LAY_MAXC ? mx : v < mx ? v : mx;
    if (*w < 0) *w = 0;
}

/* the class of the code point at s[i] in a word, as Chromium's word breaks split words for capitalize (UAX #29's, a
   full stop and a colon splitting letters; measured on tests/pages/texttransform.html): 1 a letter (a code point past
   Latin-1 one too, but general punctuation's and the emoji's), 2 a digit, 3 _ (which joins what it is between), 4 '
   or a right single quote (between letters, or between digits), 5 the middle dot (between letters), 6 . , or ;
   (between digits), 0 anything else; its length into bl */
static int lay_word_class(const char *s, size_t n, size_t i, size_t *bl) {
    unsigned char c = (unsigned char)s[i];
    *bl = 1;
    if (c < 0x80) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
        if (c >= '0' && c <= '9') return 2;
        return c == '_' ? 3 : c == '\'' ? 4 : c == '.' || c == ',' || c == ';' ? 6 : 0;
    }
    size_t k = 1;
    while (i + k < n && ((unsigned char)s[i + k] & 0xC0) == 0x80) k++;
    *bl = k;
    unsigned char e = k > 1 ? (unsigned char)s[i + 1] : 0, e2 = k > 2 ? (unsigned char)s[i + 2] : 0;
    if (c == 0xC2) return e == 0xB7 ? 5 : e == 0xAA || e == 0xB5 || e == 0xBA ? 1 : 0;
    if (c == 0xC3) return e == 0x97 || e == 0xB7 ? 0 : 1;
    if (c == 0xE2 && e == 0x80) return e2 == 0x98 || e2 == 0x99 ? 4 : 0;
    if (c == 0xE2 && e == 0x81 && e2 <= 0xAF) return 0;
    if (c == 0xF0 && e == 0x9F) return 0;
    return (c & 0xC0) == 0x80 ? 0 : 1;
}
/* math-auto's italic of a code point (MathML Core's italic mapping), or the code point itself */
static uint32_t lay_math_italic(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return 0x1D434 + (c - 'A');
    if (c == 'h') return 0x210E;
    if (c >= 'a' && c <= 'z') return 0x1D44E + (c - 'a');
    if (c >= 0x391 && c <= 0x3A1) return 0x1D6E2 + (c - 0x391);
    if (c >= 0x3A3 && c <= 0x3A9) return 0x1D6F4 + (c - 0x3A3);
    if (c >= 0x3B1 && c <= 0x3C9) return 0x1D6FC + (c - 0x3B1);
    switch (c) {
    case 0x131: return 0x1D6A4;
    case 0x237: return 0x1D6A5;
    case 0x3F4: return 0x1D6F3;
    case 0x2207: return 0x1D6FB;
    case 0x2202: return 0x1D715;
    case 0x3F5: return 0x1D716;
    case 0x3D1: return 0x1D717;
    case 0x3F0: return 0x1D718;
    case 0x3D5: return 0x1D719;
    case 0x3F1: return 0x1D71A;
    case 0x3D6: return 0x1D71B;
    default: return c;
    }
}
/* s[0, n) is one code point */
static int lay_one_cp(const char *s, size_t n) {
    size_t z = 0;
    if (!n) return 0;
    amb_utf8_next(s, n, &z);
    return z == n;
}
/* the text-transform tt of s[0, n) into out: ASCII's and Latin-1's letters, each the length it was; capitalize makes
   upper case each word's first letter (a word as lay_word_class has words run; with TT_MID, s goes on with one);
   math-auto (given a text of one code point alone, lay_chars sees to it) its italic */
static void lay_transform(const char *s, size_t n, int tt, vector<char> *out) {
    int mid = (tt & TT_MID) != 0;
    tt &= ~TT_MID;
    if (tt == TT_MATH_AUTO) {
        size_t z = 0;
        uint32_t c = n ? lay_math_italic(amb_utf8_next(s, n, &z)) : 0;
        if (n && c < 0x80) push_back(out, (char)c);
        else if (n && c < 0x800) { push_back(out, (char)(0xC0 | c >> 6)); push_back(out, (char)(0x80 | (c & 0x3F))); }
        else if (n && c < 0x10000) {
            push_back(out, (char)(0xE0 | c >> 12));
            push_back(out, (char)(0x80 | (c >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (c & 0x3F)));
        } else if (n) {
            push_back(out, (char)(0xF0 | c >> 18));
            push_back(out, (char)(0x80 | (c >> 12 & 0x3F)));
            push_back(out, (char)(0x80 | (c >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (c & 0x3F)));
        }
        if (z < n) append_range(out, s + z, n - z);
        return;
    }
    int in = mid, last = mid;
    size_t i = 0;
    while (i < n) {
        size_t bl;
        int cl = lay_word_class(s, n, i, &bl);
        int up = tt == TT_UPPER;
        if (cl >= 1 && cl <= 3) {
            if (tt == TT_CAPITALIZE && !in) up = 1;
            in = 1;
            last = cl;
        } else if (in && cl >= 4) {
            size_t nb;
            int nc = i + bl < n ? lay_word_class(s, n, i + bl, &nb) : 0;
            int joins = (cl != 6 && last == 1 && nc == 1) || (cl != 5 && last == 2 && nc == 2);
            if (!joins) in = 0;
        } else in = 0;
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            if (up && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
            else if (tt == TT_LOWER && c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
            push_back(out, (char)c);
        } else if (c == 0xC3 && bl == 2) {
            unsigned char e = (unsigned char)s[i + 1];
            if (up && e >= 0xA0 && e <= 0xBE && e != 0xB7) e = (unsigned char)(e - 0x20);
            else if (tt == TT_LOWER && e >= 0x80 && e <= 0x9E && e != 0x97) e = (unsigned char)(e + 0x20);
            push_back(out, (char)c);
            push_back(out, (char)e);
        } else append_range(out, s + i, bl);
        i += bl;
    }
}

/* text without its soft hyphens (U+00AD) */
static void lay_strip_soft(vector<char> *tw) {
    size_t o = 0;
    for (size_t q = 0; q < len(*tw); q++) {
        if ((unsigned char)at(*tw, q) == 0xC2 && q + 1 < len(*tw) && (unsigned char)at(*tw, q + 1) == 0xAD) { q++; continue; }
        (*tw)[o++] = at(*tw, q);
    }
    resize(tw, o);
}

/* text without the bidi controls, which draw nothing (Default_Ignorable_Code_Point): ALM,
   LRM, RLM, the embeddings, overrides and isolates and their ends */
static void lay_strip_controls(vector<char> *tw) {
    size_t o = 0, n = len(*tw);
    for (size_t q = 0; q < n; q++) {
        unsigned char c = (unsigned char)at(*tw, q);
        if (c == 0xD8 && q + 1 < n && (unsigned char)at(*tw, q + 1) == 0x9C) { q++; continue; }
        if (c == 0xE2 && q + 2 < n) {
            unsigned char c1 = (unsigned char)at(*tw, q + 1), c2 = (unsigned char)at(*tw, q + 2);
            if ((c1 == 0x80 && (c2 == 0x8E || c2 == 0x8F || (c2 >= 0xAA && c2 <= 0xAE))) || (c1 == 0x81 && c2 >= 0xA6 && c2 <= 0xA9)) {
                q += 2;
                continue;
            }
        }
        (*tw)[o++] = at(*tw, q);
    }
    resize(tw, o);
}

/* the code points of UTF-8 text: letter-spacing's count */
static size_t lay_cps(const char *s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) k++;
    return k;
}

/* ---- bidirectional text (UAX #9): the classes ---- */

/* the bidi classes, in tools/gen-bidi.pl's order (bidi_data.h) */
enum { BC_L, BC_R, BC_AL, BC_EN, BC_ES, BC_ET, BC_AN, BC_CS, BC_NSM, BC_BN, BC_B, BC_S, BC_WS, BC_ON,
       BC_LRE, BC_LRO, BC_RLE, BC_RLO, BC_PDF, BC_LRI, BC_RLI, BC_FSI, BC_PDI };

/* a code point's Bidi_Class: its range's, the last that starts at or before it */
static int lay_bidi_class(uint32_t cp) {
    size_t lo = 0, hi = sizeof amb_bidi_class / sizeof amb_bidi_class[0];
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if ((amb_bidi_class[mid] >> 5) <= cp) lo = mid;
        else hi = mid;
    }
    return (int)(amb_bidi_class[lo] & 31);
}

/* the code point at s[i] (a lead byte, 0xC2 or more) asks for the bidi algorithm: it is right
   to left (R, AL), an Arabic number (AN), or an explicit formatting character. RLM and the
   controls (U+200F, U+202A to U+202E, U+2066 to U+2069) are known by their bytes; only in
   the blocks of right-to-left scripts (U+0590 to U+08FF, U+FB1D to U+FDFF, U+FE70 to
   U+FEFF, U+10800 to U+10FFF, U+1E800 to U+1EFFF) is a code point looked up */
static int lay_bidi_hot(const char *s, size_t n, size_t i) {
    unsigned char c0 = (unsigned char)s[i], c1 = i + 1 < n ? (unsigned char)s[i + 1] : 0, c2 = i + 2 < n ? (unsigned char)s[i + 2] : 0;
    if (c0 == 0xE2) return (c1 == 0x80 && (c2 == 0x8F || (c2 >= 0xAA && c2 <= 0xAE))) || (c1 == 0x81 && c2 >= 0xA6 && c2 <= 0xA9);
    int maybe = (c0 >= 0xD6 && c0 <= 0xDF) || (c0 == 0xE0 && c1 >= 0xA0 && c1 <= 0xA3) ||
                (c0 == 0xEF && ((c1 >= 0xAC && c1 <= 0xB7) || (c1 >= 0xB9 && c1 <= 0xBB))) || (c0 == 0xF0 && (c1 == 0x90 || c1 == 0x9E) && c2 >= 0xA0);
    if (!maybe) return 0;
    size_t z = i;
    int c = lay_bidi_class(amb_utf8_next(s, n, &z));
    return c == BC_R || c == BC_AL || c == BC_AN || (c >= BC_LRE && c <= BC_PDI);
}

/* white space, as text's pieces are cut by it */
static int lay_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\n'; }

/* the pieces of text tx[0, n): a text node's (node), or a pseudo-element's content (node
   LAY_GEN | LAY_GTEXT and its index), in the style s of its element pe */
static void lay_chars(LCTX, const lay_style *s, uint32_t node, uint32_t pe, const char *tx, size_t n, int deco, vector<lay_piece> *ps, int *sp,
                      const lay_inh *ih) {
    size_t i = 0;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, s);
    /* math-auto italicises a text of one character, and no other (as Chromium has it) */
    if (p.tt == TT_MATH_AUTO && !lay_one_cp(tx, n)) p.tt = TT_NONE;
    p.node = node;
    p.elem = pe;
    p.deco = (uint8_t)deco;
    p.shift = (float)ih->shift;
    p.rx = (float)ih->rx;
    p.ry = (float)ih->ry;
    p.va = (uint8_t)ih->va;
    if (!lay_bytecls_ready) lay_bytecls_make();
    if (s->orient != 4) {
        const lay_ext *x = data(*xc) + lay_si(sx, gx, pe);
        if (x->wbrk || x->lbrk || x->owrap) lay_lb_marks++;
    }
    if (s->orient == 4) {
        /* text-combine-upright: all in a vertical line (CSS Writing Modes 4, 9.1): the text one piece, an em down the
           line (lay_shape_pieces), set across it (Blink's LayoutTextCombine, an inline-block of its own): no letter- or
           word-spacing, the white space at its ends dropped, as its own line's is (the rest kept as it is) */
        size_t a = 0, b = n;
        while (a < b && lay_ws(tx[a])) a++;
        while (b > a && lay_ws(tx[b - 1])) b--;
        if (a == b) return;
        p.kind = P_WORD;
        p.off = (uint32_t)a;
        p.len = (uint32_t)(b - a);
        p.ls = 0;
        p.wsp = 0;
        push_back(ps, p);
        *sp = 0;
        return;
    }
    /* a space: its advance, with letter-spacing and word-spacing */
    double space = amb_advance(f, s->face, ' ', s->fs) + s->ls + s->wsp;
    int collapse = s->ws == WS_NORMAL || s->ws == WS_NOWRAP || s->ws == WS_PRE_LINE;
    while (i < n) {
        char c = tx[i];
        int white = c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\n';
        if (c == '\n' && s->ws != WS_NORMAL && s->ws != WS_NOWRAP) {
            /* a kept newline: a forced break, and in pre-line the spaces before it go */
            if (s->ws == WS_PRE_LINE && len(*ps) && back(*ps).kind == P_SPACE && !back(*ps).keep) pop_back(ps);
            p.kind = P_BREAK;
            p.w = 0;
            push_back(ps, p);
            *sp = collapse;
            i++;
            continue;
        }
        if (white && collapse) {
            if (!*sp) {
                p.kind = P_SPACE;
                p.w = space;
                p.brk = (uint8_t)(s->ws != WS_NOWRAP);
                p.keep = 0;
                p.off = (uint32_t)i;
                p.len = 1;
                push_back(ps, p);
                *sp = 1;
            }
            i++;
            continue;
        }
        if (white) {
            /* kept spaces (pre, pre-wrap): a run of them, a tab to the next stop of eight */
            size_t j = i;
            double w = 0;
            while (j < n && (tx[j] == ' ' || tx[j] == '\t' || tx[j] == '\r' || tx[j] == '\f')) { w += tx[j] == '\t' ? 8 * space : space; j++; }
            p.kind = P_SPACE;
            p.w = w;
            p.brk = (uint8_t)(s->ws == WS_PRE_WRAP);
            p.keep = 1;
            p.off = (uint32_t)i;
            p.len = (uint32_t)(j - i);
            push_back(ps, p);
            *sp = 0;
            i = j;
            continue;
        }
        size_t j = i;
        int soft = 0, rtl = 0, hot = 0;
        while (j < n) {
            uint8_t cl = lay_bytecls[(unsigned char)tx[j]];
            if (cl & LAY_BC_SPACE) break;
            if (cl) {
                hot = 1;
                unsigned char c0 = (unsigned char)tx[j];
                if (c0 == 0xC2 && j + 1 < n && (unsigned char)tx[j + 1] == 0xAD) soft = 1;
                else if (c0 >= 0xC2 && !rtl && lay_bidi_hot(tx, n, j)) rtl = 1;
            }
            j++;
        }
        p.kind = P_WORD;
        p.off = (uint32_t)i;
        p.len = (uint32_t)(j - i);
        p.rtl = (uint8_t)rtl;
        p.soft = (uint8_t)soft;
        p.lbf = (uint8_t)(hot ? LBF_HOT : 0);
        if (hot) lay_lb_marks++;
        /* its width, and the lines of the faces other than its own that draw some of it: its
           run's shaping's (lay_shape_pieces) */
        p.w = 0;
        p.brk = 0;
        p.keep = 0;
        push_back(ps, p);
        p.rtl = 0;
        p.soft = 0;
        p.lbf = 0;
        *sp = 0;
        i = j;
    }
}

static void lay_text_pieces(LCTX, uint32_t node, int deco, vector<lay_piece> *ps, int *sp, const lay_inh *ih) {
    lay_style s;
    if (node & LAY_GEN) {
        /* a pseudo-element's content, in its style */
        uint32_t pe = node & ~LAY_GTEXT;
        lay_style_of(LARGS, pe, &s);
        vector<char> gt;
        amb_span g = at(*gx, node & LAY_GIDX).text;
        append_range(gt, text_ptr(t, g), g.len);
        lay_chars(LARGS, &s, node, pe, data(gt), len(gt), deco, ps, sp, ih);
        return;
    }
    /* a text node's, in its parent's style (read in place: the run's styles grow only where a query container is
       restyled, in lay_block, which lay_chars never reaches) */
    uint32_t pe = dom_flat_parent(d, node);
    if (!pe) pe = d->node[node].parent;
    uint32_t si = pe < len(*sx) ? at(*sx, pe) & LAY_SMASK : 0;
    amb_span span = d->node[node].data;
    lay_chars(LARGS, data(*sc) + si, node, pe, text_ptr(d, span), span.len, deco, ps, sp, ih);
}
/* ---- shaping: runs of pieces measured as Chromium shapes them ---- */

static void lay_piece_text(const layout_tree *t, const dom_document *d, const vector<lay_gen> *gx, const vector<lay_piece> *ps, size_t a, size_t b,
                           vector<char> *out);
static int lay_bidi_class(uint32_t cp);
static void lay_pack_glyphs(LCTX, const lay_piece *pp, const vector<char> *tx, size_t txo, const vector<amb_shaped> *gl, size_t gb, size_t gn,
                            uint32_t b0, uint32_t b1, double *px, vector<char> *out);
static void lay_put_glyphs(layout_tree *t, vector<char> *out, uint32_t fr);

/* two text pieces of one shaping run: one font (face, web font, size, letter- and word-spacing,
   style: Blink's FontDescription) */
static int lay_same_font(lay_piece a, lay_piece b) {
    return a.face == b.face && a.font == b.font && a.size == b.size && a.ls == b.ls && a.wsp == b.wsp && a.italic == b.italic &&
           a.orient == b.orient;
}

/* an inline element's edge a run is shaped across (Blink's ShouldBreakShapingBefore/AfterBox):
   no margin, border or padding on that side, its vertical-align baseline */
static int lay_edge_open(LCTX, lay_piece p) {
    return p.w == 0 && LAY_X(p.node).va == VA_BASELINE;
}

/* a kept run of spaces with a tab in it (laid out by its tab stop, not shaped) */
static int lay_tabbed(LCTX, lay_piece p) {
    if (p.kind != P_SPACE || !p.keep || (p.node & LAY_GEN)) return 0;
    amb_span sp = d->node[p.node].data;
    const char *tx = text_ptr(d, sp);
    for (uint32_t i = p.off; i < p.off + p.len && i < sp.len; i++) if (tx[i] == '\t') return 1;
    return 0;
}

/* a text piece's bytes as they are laid out and shaped (a collapsed space one; text-transform's
   letters, soft hyphens and bidi controls out), appended to out */
static void lay_shape_bytes(LCTX, const vector<lay_piece> *ps, size_t k, vector<char> *out) {
    lay_piece p = at(*ps, k);
    if (p.kind == P_SPACE && !p.keep) { push_back(out, ' '); return; }
    size_t from = len(*out);
    lay_piece_text(t, d, gx, ps, k, k + 1, out);
    /* a soft hyphen's hyphen (hy 2) is shaped apart, as Blink shapes it (lay_frag_glyphs) */
    if (p.kind == P_WORD && p.hy == 2 && len(*out) > from) pop_back(out);
    if (p.kind == P_WORD && p.rtl) {
        vector<char> w;
        for (size_t q = from; q < len(*out); q++) push_back(w, at(*out, q));
        resize(out, from);
        lay_strip_controls(&w);
        append_range(out, data(w), len(w));
    }
}

/* a word's direction by its first strong letter: 1 right to left, 0 left to right, -1 none */
static int lay_word_dir(LCTX, const vector<lay_piece> *ps, size_t k) {
    lay_piece p = at(*ps, k);
    if (p.kind != P_WORD || !p.rtl) return p.kind == P_WORD ? 0 : -1;
    vector<char> w;
    lay_shape_bytes(LARGS, ps, k, &w);
    size_t i = 0;
    while (i < len(w)) {
        int c = lay_bidi_class(amb_utf8_next(data(w), len(w), &i));
        if (c == BC_L) return 0;
        if (c == BC_R || c == BC_AL) return 1;
    }
    return -1;
}

/* where the shaping run that begins at text piece k ends: past its last text piece before one of
   another font or direction, an edge it is not shaped across, a piece that is no text, or a kept
   tab; its direction into *rtl */
static size_t lay_run_end(LCTX, const vector<lay_piece> *ps, size_t k, size_t end, int *rtl) {
    lay_piece p0 = at(*ps, k);
    int dir = p0.sdir == 1 ? 0 : p0.sdir == 2 ? 1 : lay_word_dir(LARGS, ps, k);
    if (p0.orient == 4) { *rtl = dir == 1; return k + 1; }   /* a combined text: its own box's */
    size_t e = k + 1, last = k + 1;
    while (e < end) {
        lay_piece q = at(*ps, e);
        if (q.kind == P_OPEN || q.kind == P_CLOSE) {
            if (!lay_edge_open(LARGS, q)) break;
            e++;
            continue;
        }
        if ((q.kind != P_WORD && q.kind != P_SPACE) || !lay_same_font(p0, q) || lay_tabbed(LARGS, q)) break;
        if (p0.sdir || q.sdir) {
            /* the bidi levels' directions: a word of both shaped alone */
            if (q.sdir != p0.sdir || p0.sdir == 3) break;
        } else {
            int qd = lay_word_dir(LARGS, ps, e);
            if (qd >= 0 && dir >= 0 && qd != dir) break;
            if (dir < 0) dir = qd;
        }
        e++;
        last = e;
    }
    *rtl = dir == 1;
    return last;
}

/* a piece's text as the context of a run shaped beside it: a word's or kept space's bytes as
   laid out, a collapsed space one, an atomic inline's U+FFFC and a forced break's newline (in
   Blink's text content), an edge none */
static void lay_ctx_bytes(LCTX, const vector<lay_piece> *ps, size_t k, vector<char> *out) {
    lay_piece p = at(*ps, k);
    if (p.kind == P_WORD || p.kind == P_SPACE) lay_shape_bytes(LARGS, ps, k, out);
    else if (p.kind == P_ATOMIC) append_range(out, "\xEF\xBF\xBC", 3);
    else if (p.kind == P_BREAK) push_back(out, '\n');
}

/* the last five code points of text (the context before a run), or its first five (after one),
   appended to out */
static void lay_ctx_tail(const vector<char> *all, vector<char> *out) {
    size_t i = 0, c = lay_cps(data(*all), len(*all));
    while (c > 5 && i < len(*all)) { amb_utf8_next(data(*all), len(*all), &i); c--; }
    for (size_t q = i; q < len(*all); q++) push_back(out, at(*all, q));
}
static void lay_ctx_head(const vector<char> *all, vector<char> *out) {
    size_t i = 0;
    for (int c = 0; c < 5 && i < len(*all); c++) amb_utf8_next(data(*all), len(*all), &i);
    for (size_t q = 0; q < i; q++) push_back(out, at(*all, q));
}

/* the context HarfBuzz is given about a run (Blink shapes each run in its paragraph's whole
   text, which Arabic joining reads across an element's edge or a change of font): the last
   five code points of the text before piece k, appended to out */
static void lay_ctx_before(LCTX, const vector<lay_piece> *ps, size_t k, vector<char> *out) {
    size_t j = k, n = 0;
    while (j > 0 && n < 5) {
        vector<char> one;
        lay_ctx_bytes(LARGS, ps, j - 1, &one);
        n += lay_cps(data(one), len(one));
        j--;
    }
    vector<char> all;
    for (size_t q = j; q < k; q++) lay_ctx_bytes(LARGS, ps, q, &all);
    lay_ctx_tail(&all, out);
}

/* and the first five of the text after piece k */
static void lay_ctx_after(LCTX, const vector<lay_piece> *ps, size_t k, vector<char> *out) {
    vector<char> all;
    for (size_t j = k + 1; j < len(*ps) && lay_cps(data(all), len(all)) < 5; j++) lay_ctx_bytes(LARGS, ps, j, &all);
    lay_ctx_head(&all, out);
}

/* the pieces [a, b) of one run shaped as one text, in its context: each text piece's bytes in tx
   from at0[k - a] (an edge's its next piece's), the context before at0[0] and after at0[b - a];
   the glyphs into gl, visual order (their clusters in order, the other way right to left) */
static void lay_run_shape(LCTX, const vector<lay_piece> *ps, size_t a, size_t b, int rtl, vector<char> *tx, vector<uint32_t> *at0,
                          vector<amb_shaped> *gl) {
    int comb = at(*ps, a).orient == 4;      /* a combined text, alone in its box */
    if (!comb) lay_ctx_before(LARGS, ps, a, tx);
    for (size_t k = a; k < b; k++) {
        push_back(at0, (uint32_t)len(*tx));
        lay_piece q = at(*ps, k);
        if (q.kind == P_WORD || q.kind == P_SPACE) lay_shape_bytes(LARGS, ps, k, tx);
    }
    push_back(at0, (uint32_t)len(*tx));
    size_t from = at(*at0, 0), to = len(*tx);
    if (!comb) lay_ctx_after(LARGS, ps, b - 1, tx);
    lay_piece p0 = at(*ps, a);
    int flags = (rtl ? AMB_SHAPE_RTL : 0) | (p0.ls != 0 ? AMB_SHAPE_NOLIGA : 0) |
                (p0.orient == 1 ? AMB_SHAPE_VMIXED : p0.orient == 2 ? AMB_SHAPE_VUPRIGHT : 0);
    if (to > from) amb_shape(f, p0.face, data(*tx), len(*tx), from, to, flags, gl);
}

/* a run as the cache knows it: its first and last pieces and those about it (a text node's
   bytes, or an element's), its count of pieces, its font, its letters' case (text-transform,
   which ::first-line may change) and its direction */
typedef struct lay_rk {
    uint32_t n0, o0, l0, n1, o1, l1, nb, ob, na, oa, cnt, face, font, tt, rtl;
    double size, ls, wsp;
} lay_rk;

static int lay_rk_eq(lay_rk x, lay_rk y) {
    return x.n0 == y.n0 && x.o0 == y.o0 && x.l0 == y.l0 && x.n1 == y.n1 && x.o1 == y.o1 && x.l1 == y.l1 && x.nb == y.nb && x.ob == y.ob &&
           x.na == y.na && x.oa == y.oa && x.cnt == y.cnt && x.face == y.face && x.font == y.font && x.tt == y.tt && x.rtl == y.rtl &&
           x.size == y.size && x.ls == y.ls && x.wsp == y.wsp;
}

/* the runs this layout shaped (lay_run_cached; some 500,000 glyphs' worth at most), each its key
   and where in the pools after its text with its context is, each piece's first byte in it and
   its glyphs, found by its key's hash: a paragraph's runs are measured before its lines take
   them, a table cell's for its widths before it is laid out, and each line's fragments take
   their run's glyphs (each run shaped once). Freed at the layout's end (lay_cq_free) */
typedef struct lay_sce { lay_rk key; uint32_t tx0, txn, at0, atn, gl0, gln; } lay_sce;
static vector<lay_sce> lay_ar;
static vector<char> lay_ar_tx;
static vector<uint32_t> lay_ar_at;
static vector<amb_shaped> lay_ar_gl;
static vector<uint32_t> lay_ar_slot;   /* the kept runs by their keys' hash: each slot an entry + 1, 0 none */

/* the runs kept emptied */
static void lay_ar_clear(void) {
    clear(lay_ar);
    clear(lay_ar_tx);
    clear(lay_ar_at);
    clear(lay_ar_gl);
    clear(lay_ar_slot);
}

/* a run key's hash (FNV-1a over its fields) */
static uint32_t lay_rk_hash(lay_rk k) {
    uint32_t v[15] = { k.n0, k.o0, k.l0, k.n1, k.o1, k.l1, k.nb, k.ob, k.na, k.oa, k.cnt, k.face, k.font, k.tt, k.rtl };
    uint64_t h = 1469598103934665603ull, bits;
    for (int i = 0; i < 15; i++) h = (h ^ v[i]) * 1099511628211ull;
    memcpy(&bits, &k.size, sizeof bits);
    h = (h ^ bits) * 1099511628211ull;
    memcpy(&bits, &k.ls, sizeof bits);
    h = (h ^ bits) * 1099511628211ull;
    memcpy(&bits, &k.wsp, sizeof bits);
    h = (h ^ bits) * 1099511628211ull;
    return (uint32_t)(h ^ h >> 32);
}

/* the kept run of key k: its entry + 1, 0 none */
static uint32_t lay_ar_find(lay_rk k) {
    size_t ns = len(lay_ar_slot);
    if (!ns) return 0;
    size_t i = lay_rk_hash(k) & (ns - 1);
    while (at(lay_ar_slot, i)) {
        uint32_t e = at(lay_ar_slot, i);
        if (lay_rk_eq(at(lay_ar, e - 1).key, k)) return e;
        i = (i + 1) & (ns - 1);
    }
    return 0;
}

/* entry e (its index) in the slots, made twice as many when they are half full */
static void lay_ar_index(uint32_t e) {
    size_t ns = len(lay_ar_slot);
    if (2 * (len(lay_ar) + 1) > ns) {
        size_t m = ns ? 2 * ns : 64;
        while (2 * (len(lay_ar) + 1) > m) m *= 2;
        clear(lay_ar_slot);
        resize(lay_ar_slot, m);
        for (uint32_t q = 0; q < (uint32_t)len(lay_ar); q++) {
            if (q == e) continue;
            size_t i = lay_rk_hash(at(lay_ar, q).key) & (m - 1);
            while (at(lay_ar_slot, i)) i = (i + 1) & (m - 1);
            lay_ar_slot[i] = q + 1;
        }
        ns = m;
    }
    size_t i = lay_rk_hash(at(lay_ar, e).key) & (ns - 1);
    while (at(lay_ar_slot, i)) i = (i + 1) & (ns - 1);
    lay_ar_slot[i] = e + 1;
}

/* the run of pieces [a, b) of ps kept (lay_ar_*): the one kept, or shaped and kept; its entry
   (valid until the next run is taken) */
static lay_sce lay_run_cached(LCTX, const vector<lay_piece> *ps, size_t a, size_t b, int rtl) {
    lay_rk k;
    memset(&k, 0, sizeof k);
    lay_piece p0 = at(*ps, a), p1 = at(*ps, b - 1);
    k.n0 = p0.node;
    k.o0 = p0.off;
    k.l0 = p0.len;
    k.n1 = p1.node;
    k.o1 = p1.off;
    k.l1 = p1.len;
    if (a > 0) { k.nb = at(*ps, a - 1).node; k.ob = at(*ps, a - 1).off + 1; }
    if (b < len(*ps)) { k.na = at(*ps, b).node; k.oa = at(*ps, b).off + 1; }
    k.cnt = (uint32_t)(b - a);
    k.face = p0.face;
    k.font = p0.font;
    k.tt = (uint32_t)p0.tt << 8 | p1.tt;
    k.rtl = (uint32_t)rtl | (uint32_t)p0.orient << 1;
    k.size = p0.size;
    k.ls = p0.ls;
    k.wsp = p0.wsp;
    uint32_t found = lay_ar_find(k);
    if (found) return at(lay_ar, found - 1);
    vector<char> tx;
    vector<uint32_t> at0;
    vector<amb_shaped> gl;
    lay_run_shape(LARGS, ps, a, b, rtl, &tx, &at0, &gl);
    if (len(lay_ar_gl) + len(gl) > 524288) lay_ar_clear();       /* about 15 MB kept at most */
    lay_sce en;
    memset(&en, 0, sizeof en);
    en.key = k;
    en.tx0 = (uint32_t)len(lay_ar_tx);
    en.txn = (uint32_t)len(tx);
    en.at0 = (uint32_t)len(lay_ar_at);
    en.atn = (uint32_t)len(at0);
    en.gl0 = (uint32_t)len(lay_ar_gl);
    en.gln = (uint32_t)len(gl);
    append_range(lay_ar_tx, data(tx), len(tx));
    append_range(lay_ar_at, data(at0), len(at0));
    append_range(lay_ar_gl, data(gl), len(gl));
    push_back(lay_ar, en);
    lay_ar_index((uint32_t)len(lay_ar) - 1);
    return en;
}

/* the run of piece i (its run, measured: lay_shape_pieces) in ps: [*c0, *c1), and the direction
   it was shaped in (its pieces'); the bounds last found kept, and taken when they are the run's
   in ps (the next fragment's most often the same) */
static uint32_t lay_rb_run;
static size_t lay_rb_c0, lay_rb_c1;
static void lay_run_bounds(const vector<lay_piece> *ps, size_t i, size_t *c0, size_t *c1, int *rtl) {
    lay_piece p = at(*ps, i);
    uint32_t id = p.run;
    size_t n = len(*ps), a = lay_rb_c0, b = lay_rb_c1;
    *rtl = p.rrtl;
    if (id && id == lay_rb_run && a <= i && i < b && b <= n && at(*ps, a).run == id && at(*ps, b - 1).run == id && (a == 0 || at(*ps, a - 1).run != id) &&
        (b == n || at(*ps, b).run != id)) {
        *c0 = a;
        *c1 = b;
        return;
    }
    a = i;
    b = i + 1;
    while (a > 0 && at(*ps, a - 1).run == id) a--;
    while (b < n && at(*ps, b).run == id) b++;
    lay_rb_run = id;
    lay_rb_c0 = a;
    lay_rb_c1 = b;
    *c0 = a;
    *c1 = b;
}

/* the piece of a kept run (its pieces' first bytes from lay_ar_at[a0]) whose bytes hold byte c
   of its text, from piece j on (the run's glyphs' clusters in order: one way or the other) */
static size_t lay_run_piece(size_t a0, size_t np, uint32_t c, size_t j) {
    while (j > 0 && at(lay_ar_at, a0 + j) > c) j--;
    while (j + 1 < np && at(lay_ar_at, a0 + j + 1) <= c) j++;
    return j;
}

/* lay_shape_pieces's measure of a vertical line's run (en, its n pieces, rtl as it was shaped; p its first): each
   glyph's piece (gp) and each piece's advances, its own face's in its units and the others' in px, an upright glyph's
   down the line in px (amb_shaped_advance's: vmtx's, or the face's height; a mark's none); the faces other than its own
   that draw some of it (fbm); its last cluster with an advance of its own face's (lastc) */
static void lay_shape_vrun(LCTX, const lay_piece *pp, lay_sce en, size_t n, int rtl, vector<int64_t> *units, vector<double> *other,
                           vector<uint32_t> *fbm, vector<uint32_t> *gp, vector<uint32_t> *lastc) {
    lay_piece p = *pp;
    size_t jj = rtl ? n - 1 : 0;
    for (size_t g = 0; g < en.gln; g++) {
        amb_shaped s = at(lay_ar_gl, en.gl0 + g);
        size_t j = lay_run_piece(en.at0, n, s.cluster, jj);
        jj = j;
        (*gp)[g] = (uint32_t)j;
        if (s.flags & AMB_SHAPED_UPRIGHT) {
            if ((s.flags & AMB_SHAPED_VFALLBACK) && !(s.flags & AMB_SHAPED_VZERO)) {
                double fa, fd, fg;
                amb_face_metrics(f, s.face, p.size, &fa, &fd, &fg);
                (*other)[j] += fa + fd;
            } else (*other)[j] += s.ax * (p.size / amb_face_upem(f, s.face));
            int by = amb_metrics_face(s.face);
            if (s.face != p.face && by != p.face && by < AMB_NFACES) (*fbm)[j] |= 1u << by;
        } else if (s.face == p.face) {
            (*units)[j] += s.ax;
            if (s.ax != 0 && s.cluster + 1 > at(*lastc, j)) (*lastc)[j] = s.cluster + 1;
        } else {
            (*other)[j] += s.ax * p.size / amb_face_upem(f, s.face);
            int by = amb_metrics_face(s.face);
            if (by != p.face && by < AMB_NFACES) (*fbm)[j] |= 1u << by;
        }
    }
}

/* the text pieces of ps from start measured by their runs' shaping, as Chromium measures them:
   each its glyphs' advances (a glyph's its cluster's piece's: a kerning pair's to its first,
   a ligature across an element's edge to the piece its first letter is in), its letter-spacing
   after each code point and word-spacing after each space; but the kerning of a word's last
   letter with the space after it in its own text the space's, which a line ending at that space
   drops (Chromium shapes a line's last word without its space) */
static void lay_shape_pieces(LCTX, vector<lay_piece> *ps, size_t start) {
    size_t np = len(*ps);
    vector<int64_t> units;
    vector<double> other;
    vector<uint32_t> fbm, gp, lastc;
    for (size_t k = start; k < np; k++) (*ps)[k].run = 0;
    for (size_t k = start; k < np;) {
        uint8_t kind = (*ps)[k].kind;
        if (kind != P_WORD && kind != P_SPACE) { k++; continue; }
        lay_piece p = at(*ps, k);
        if (lay_tabbed(LARGS, p)) { k++; continue; }
        int rtl;
        size_t b = lay_run_end(LARGS, ps, k, np, &rtl);
        for (size_t j = k; j < b; j++) {
            (*ps)[j].run = (uint32_t)k + 1;
            (*ps)[j].rrtl = (uint8_t)rtl;
        }
        if (p.orient == 4) {
            /* a combined text (a run of its own: lay_run_end): an em down the line, whatever its glyphs' advances; its
               fonts' lines its box's alone (its glyphs shaped when its fragment takes them) */
            lay_piece q = at(*ps, k);
            q.w = q.size;
            q.fb = 0;
            (*ps)[k] = q;
            k = b;
            continue;
        }
        lay_sce en = lay_run_cached(LARGS, ps, k, b, rtl);
        size_t n = b - k, ng = en.gln;
        /* each piece's advances: its own face's in its units, the others' in px; and the
           faces other than its own that draw some of it (a mask of those below AMB_NFACES);
           each glyph's piece, and each piece's last cluster with an advance of its own face's
           (+ 1; 0 none) */
        clear(units);
        clear(other);
        clear(fbm);
        clear(gp);
        clear(lastc);
        resize(units, n);
        resize(other, n);
        resize(fbm, n);
        resize(lastc, n);
        resize(gp, ng);
        double upem = amb_face_upem(f, p.face);
        size_t jj = rtl ? n - 1 : 0;
        if (p.orient == 1 || p.orient == 2)
            /* a vertical line's run, which may set glyphs upright (a loop of its own: a horizontal run's is as it was) */
            lay_shape_vrun(LARGS, &p, en, n, rtl, &units, &other, &fbm, &gp, &lastc);
        else
            for (size_t g = 0; g < ng; g++) {
                amb_shaped s = at(lay_ar_gl, en.gl0 + g);
                size_t j = lay_run_piece(en.at0, n, s.cluster, jj);
                jj = j;
                gp[g] = (uint32_t)j;
                if (s.face == p.face) {
                    units[j] += s.ax;
                    if (s.ax != 0 && s.cluster + 1 > at(lastc, j)) lastc[j] = s.cluster + 1;
                } else {
                    other[j] += s.ax * p.size / amb_face_upem(f, s.face);
                    int by = amb_metrics_face(s.face);
                    if (by != p.face && by < AMB_NFACES) fbm[j] |= 1u << by;
                }
            }
        /* a word's last letter's kerning with the space after it, of its own text: the space's (not an upright glyph's) */
        for (size_t g = 0; g < ng; g++) {
            amb_shaped s = at(lay_ar_gl, en.gl0 + g);
            size_t j = at(gp, g);
            if (s.ax == 0 || s.face != p.face || j + 1 >= n || s.cluster + 1 != at(lastc, j) || (s.flags & AMB_SHAPED_UPRIGHT)) continue;
            lay_piece w = at(*ps, k + j), sp = at(*ps, k + j + 1);
            if (w.kind != P_WORD || sp.kind != P_SPACE || sp.node != w.node) continue;
            units[j] -= s.ax - s.base;
            units[j + 1] += s.ax - s.base;
        }
        for (size_t j = 0; j < n; j++) {
            lay_piece q = at(*ps, k + j);
            if (q.kind != P_WORD && q.kind != P_SPACE) continue;
            size_t b0 = at(lay_ar_at, en.at0 + j), b1 = at(lay_ar_at, en.at0 + j + 1);
            double w = upem > 0 ? (double)at(units, j) * p.size / upem + at(other, j) : 0;
            w += q.ls * (double)lay_cps(data(lay_ar_tx) + en.tx0 + b0, b1 - b0);
            if (q.kind == P_SPACE) w += q.wsp * (double)(q.keep ? q.len : 1);
            q.w = w;
            /* under line-height normal, a word's fallback faces' lines (Blink's used fonts) */
            q.fb = 0;
            uint32_t m = q.kind == P_WORD && q.lhn ? at(fbm, j) : 0;
            for (int z = 0; m && z < AMB_NFACES; z++) {
                if (!(m >> z & 1)) continue;
                double fa, fd, fg, up, dn;
                amb_face_metrics(f, z, q.size, &fa, &fd, &fg);
                if (LAY_VC(q.orient)) { double hh = fa + fd; fd = floor(hh / 2); fa = hh - fd; }
                lay_leading(fa + fd + fg, fa, fd, &up, &dn);
                if (!q.fb || up > q.fa) q.fa = (float)up;
                if (!q.fb || dn > q.fd) q.fd = (float)dn;
                q.fb = 1;
            }
            (*ps)[k + j] = q;
        }
        k = b;
    }
}

/* a text fragment's glyphs (the whole pieces [k, r) of ps, in their text's order; rtl: their
   level odd): the glyphs their runs' shaping gave them (Blink's fragment of its items' shape
   results, as their widths were measured), each run's in its visual order, the runs too (a
   piece no run measured, a kept tab, its width), each placed from the first's pen,
   letter-spacing after each code point and word-spacing after each space as their widths have
   them; packed into the fragment fr */
static void lay_frag_glyphs(LCTX, const vector<lay_piece> *ps, size_t k, size_t r, int rtl, uint32_t fr) {
    vector<uint32_t> segs;
    for (size_t i = k; i < r;) {
        uint32_t id = at(*ps, i).run;
        size_t e = i + 1;
        while (id && e < r && at(*ps, e).run == id) e++;
        push_back(segs, (uint32_t)i);
        push_back(segs, (uint32_t)e);
        i = e;
    }
    vector<char> out;
    double x = 0;
    size_t ns = len(segs) / 2;
    for (size_t q = 0; q < ns; q++) {
        size_t z = rtl ? ns - 1 - q : q;
        size_t i = at(segs, 2 * z), e = at(segs, 2 * z + 1);
        lay_piece p = at(*ps, i);
        if (!p.run) { x += p.w; continue; }
        size_t c0, c1;
        int dir;
        lay_run_bounds(ps, i, &c0, &c1, &dir);
        lay_sce en = lay_run_cached(LARGS, ps, c0, c1, dir);
        lay_pack_glyphs(LARGS, &p, &lay_ar_tx, en.tx0, &lay_ar_gl, en.gl0, en.gln, at(lay_ar_at, en.at0 + i - c0), at(lay_ar_at, en.at0 + e - c0), &x,
                        &out);
    }
    /* a line broken after a soft hyphen: the hyphen, shaped alone after the text (Blink's item result's hyphen) */
    lay_piece lp = at(*ps, r - 1);
    if (lp.kind == P_WORD && lp.hy == 2 && lp.run) {
        vector<amb_shaped> hg;
        amb_shape(f, lp.face, "-", 1, 0, 1, 0, &hg);
        double sc = lp.size / amb_face_upem(f, lp.face);
        for (size_t g = 0; g < len(hg); g++) {
            amb_shaped s = at(hg, g);
            double gs = s.face == lp.face ? sc : lp.size / amb_face_upem(f, s.face);
            amb_rglyph rg;
            memset(&rg, 0, sizeof rg);
            rg.gid = s.gid;
            rg.face = s.face == lp.face ? (uint16_t)AMB_RGLYPH_OWN : s.face;
            rg.flags = s.flags;
            rg.x = x + s.dx * gs;
            rg.y = (float)(-s.dy * gs);
            char raw[sizeof(amb_rglyph)];
            memcpy(raw, &rg, sizeof rg);
            append_range(out, raw, sizeof raw);
            x += s.ax * gs;
        }
    }
    lay_put_glyphs(t, &out, fr);
}

/* a fragment's packed glyphs out kept as its glyphs; none, a glyph that draws nothing */
static void lay_put_glyphs(layout_tree *t, vector<char> *out, uint32_t fr) {
    if (!len(*out)) {
        amb_rglyph rg;
        memset(&rg, 0, sizeof rg);
        rg.face = AMB_RGLYPH_OWN;
        rg.flags = AMB_SHAPED_HIDDEN;
        char raw[sizeof(amb_rglyph)];
        memcpy(raw, &rg, sizeof rg);
        append_range(out, raw, sizeof raw);
    }
    t->frag[fr].glyphs = glyphs_add(t, data(*out), len(*out));
}

/* lay_pack_glyphs's loop for a vertical line's run (p its first piece; the glyphs gl[g0, g1) of those to ge, their
   clusters' starts with letter- or word-spacing starts): each glyph packed onto out from o, the pen at *x moved on;
   an upright one's (AMB_SHAPED_UPRIGHT) from its vertical origin, half its horizontal advance before the central
   baseline, its advance down the line (amb_shaped_pen's and amb_shaped_advance's). How many it packed */
static size_t lay_pack_vrun(const amb_fonts *f, const lay_piece *pp, const vector<char> *tx, size_t txo, const vector<amb_shaped> *gl,
                            size_t ge, size_t g0, size_t g1, uint32_t b0, uint32_t b1, const vector<uint32_t> *starts, double *px,
                            vector<char> *out, size_t o) {
    lay_piece p = *pp;
    double x = *px, sc = p.size / amb_face_upem(f, p.face);
    size_t w = 0;
    for (size_t g = g0; g < g1; g++) {
        amb_shaped s = at(*gl, g);
        if (s.cluster < b0 || s.cluster >= b1) continue;
        double gs = s.face == p.face ? sc : p.size / amb_face_upem(f, s.face);
        amb_rglyph rg;
        memset(&rg, 0, sizeof rg);
        rg.gid = s.gid;
        rg.face = s.face == p.face ? (uint16_t)AMB_RGLYPH_OWN : s.face;
        rg.flags = s.flags;
        rg.x = x + s.dx * gs;
        rg.y = (float)(-s.dy * gs);
        double adv = s.ax * gs;
        if (s.flags & AMB_SHAPED_UPRIGHT) {
            double along, across;
            amb_shaped_pen(f, &s, p.size, &along, &across);
            rg.x = x + along;
            rg.y = (float)across;
            adv = amb_shaped_advance(f, &s, p.size);
        }
        memcpy(data(*out) + o + w * sizeof(amb_rglyph), &rg, sizeof rg);
        w++;
        x += adv;
        if (g + 1 < ge && at(*gl, g + 1).cluster == s.cluster) continue;
        if (p.ls == 0 && p.wsp == 0) continue;
        /* the cluster's bytes: to the next cluster's start */
        size_t lo = 0, hi = len(*starts);
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (at(*starts, mid) <= s.cluster) lo = mid + 1; else hi = mid;
        }
        uint32_t next = lo < len(*starts) ? at(*starts, lo) : b1;
        size_t spaces = 0;
        for (uint32_t q = s.cluster; q < next; q++) if (at(*tx, txo + q) == ' ') spaces++;
        x += p.ls * (double)lay_cps(data(*tx) + txo + s.cluster, next - s.cluster) + p.wsp * (double)spaces;
    }
    *px = x;
    return w;
}

/* the glyphs gl[gb, gb + gn) of a run whose clusters are in [b0, b1) of its text (tx from txo)
   packed onto out (in the style of piece p), the first's pen at *x, which they move on: after
   each cluster (its last glyph in visual order) its letter-spacing for each of its code points
   and its word-spacing for each space (as the pieces' widths have them) */
static void lay_pack_glyphs(LCTX, const lay_piece *pp, const vector<char> *tx, size_t txo, const vector<amb_shaped> *gl, size_t gb, size_t gn,
                            uint32_t b0, uint32_t b1, double *px, vector<char> *out) {
    lay_piece p = *pp;
    /* the glyphs of [b0, b1): side by side, the run's clusters in order (or the other way, right
       to left), found by halving */
    size_t g0 = gb, g1 = gb + gn;
    if (gn) {
        int down = at(*gl, gb).cluster > at(*gl, gb + gn - 1).cluster;
        size_t lo = gb, hi = gb + gn;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            uint32_t c = at(*gl, mid).cluster;
            if (down ? c >= b1 : c < b0) lo = mid + 1; else hi = mid;
        }
        g0 = lo;
        hi = gb + gn;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            uint32_t c = at(*gl, mid).cluster;
            if (down ? c >= b0 : c < b1) lo = mid + 1; else hi = mid;
        }
        g1 = lo;
    }
    /* with letter- or word-spacing, the clusters' starts, in order (a run left to right gives
       them so, one right to left the other way), each once */
    vector<uint32_t> starts;
    for (size_t g = g0; g < g1 && (p.ls != 0 || p.wsp != 0); g++) {
        uint32_t c = at(*gl, g).cluster;
        if (c >= b0 && c < b1) push_back(starts, c);
    }
    size_t ns = len(starts);
    int up_order = 1, down_order = 1;
    for (size_t i = 1; i < ns; i++) {
        if (at(starts, i) < at(starts, i - 1)) up_order = 0;
        if (at(starts, i) > at(starts, i - 1)) down_order = 0;
    }
    if (!up_order && down_order)
        for (size_t i = 0; i < ns / 2; i++) {
            uint32_t v = at(starts, i);
            starts[i] = at(starts, ns - 1 - i);
            starts[ns - 1 - i] = v;
        }
    else if (!up_order)
        for (size_t i = 1; i < ns; i++) {
            uint32_t v = at(starts, i);
            size_t j = i;
            while (j > 0 && at(starts, j - 1) > v) { starts[j] = at(starts, j - 1); j--; }
            starts[j] = v;
        }
    size_t u = 0;
    for (size_t i = 0; i < ns; i++) if (!u || at(starts, u - 1) != at(starts, i)) starts[u++] = at(starts, i);
    resize(starts, u);
    double x = *px, sc = p.size / amb_face_upem(f, p.face);
    size_t o = len(*out), w = 0;
    resize(out, o + (g1 - g0) * sizeof(amb_rglyph));
    if (p.orient == 1 || p.orient == 2) {
        /* a vertical line's run, which may set glyphs upright (a loop of its own: a horizontal run's is as it was) */
        w = lay_pack_vrun(f, &p, tx, txo, gl, gb + gn, g0, g1, b0, b1, &starts, &x, out, o);
        resize(out, o + w * sizeof(amb_rglyph));
        *px = x;
        return;
    }
    for (size_t g = g0; g < g1; g++) {
        amb_shaped s = at(*gl, g);
        if (s.cluster < b0 || s.cluster >= b1) continue;
        double gs = s.face == p.face ? sc : p.size / amb_face_upem(f, s.face);
        amb_rglyph rg;
        memset(&rg, 0, sizeof rg);
        rg.gid = s.gid;
        rg.face = s.face == p.face ? (uint16_t)AMB_RGLYPH_OWN : s.face;
        rg.flags = s.flags;
        rg.x = x + s.dx * gs;
        rg.y = (float)(-s.dy * gs);
        memcpy(data(*out) + o + w * sizeof(amb_rglyph), &rg, sizeof rg);
        w++;
        x += s.ax * gs;
        if (g + 1 < gb + gn && at(*gl, g + 1).cluster == s.cluster) continue;
        if (p.ls == 0 && p.wsp == 0) continue;
        /* the cluster's bytes: to the next cluster's start */
        size_t lo = 0, hi = len(starts);
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (at(starts, mid) <= s.cluster) lo = mid + 1; else hi = mid;
        }
        uint32_t next = lo < len(starts) ? at(starts, lo) : b1;
        size_t spaces = 0;
        for (uint32_t q = s.cluster; q < next; q++) if (at(*tx, txo + q) == ' ') spaces++;
        x += p.ls * (double)lay_cps(data(*tx) + txo + s.cluster, next - s.cluster) + p.wsp * (double)spaces;
    }
    resize(out, o + w * sizeof(amb_rglyph));
    *px = x;
}

/* the pieces of an inline-level node: text, an inline element (its edges around its
   children's pieces), a forced break, an atomic box */
/* the x-height of a style's font, in px (src/paint/font.h) */
static double lay_xh(const lay_style *s) { return s->fs * amb_x_height(s->face); }

/* the parent of a run's item: an element's parent, or a pseudo-element's element */
static uint32_t lay_parent_of(const dom_document *d, const vector<lay_gen> *gx, uint32_t x) {
    if (x & LAY_GEN) return at(*gx, x & LAY_GIDX).el;
    uint32_t p = dom_flat_parent(d, x);
    return p ? p : d->node[x].parent;
}

/* a relatively positioned element's offset (CSS 2.1 9.4.3): left (or right's opposite),
   top (or bottom's), against its containing block's width cw and height ch (-1: none) */
static void lay_rel_offset(const css_style *st, const lay_ext *e, double cw, double ch, double *dx, double *dy) {
    *dx = 0;
    *dy = 0;
    if (e->pos != POS_RELATIVE) return;
    if (e->off[3].unit != LAY_AUTO) *dx = lay_res(st, e->off[3], cw);
    else if (e->off[1].unit != LAY_AUTO) *dx = -lay_res(st, e->off[1], cw);
    if (e->off[0].unit != LAY_AUTO && (e->off[0].unit == LAY_PX || ch >= 0)) *dy = lay_res(st, e->off[0], ch);
    else if (e->off[2].unit != LAY_AUTO && (e->off[2].unit == LAY_PX || ch >= 0)) *dy = -lay_res(st, e->off[2], ch);
}

/* an inline element's vertical-align and relative offset, added to what its parent passes
   (in): the baseline's shift against its parent's font, as Chromium has it */
static void lay_inline_shift(LCTX, uint32_t el, const lay_style *es, double cw, lay_inh *in) {
    int va = LAY_X(el).va;
    if (va != VA_BASELINE) {
        lay_style ps;
        lay_style_of(LARGS, lay_parent_of(d, gx, el), &ps);
        double sh = 0;
        if (va == VA_SUB) sh = ps.fs / 5 + 1;
        else if (va == VA_SUPER) sh = -(ps.fs / 3 + 1);
        else if (va == VA_TEXT_TOP) sh = es->asc - ps.asc;
        else if (va == VA_TEXT_BOTTOM) sh = ps.desc - es->desc;
        else if (va == VA_MIDDLE) sh = (es->asc - es->desc) / 2 - lay_xh(&ps) / 2;
        else if (va == VA_LEN) sh = -lay_res(st, LAY_X(el).valen, es->lh);
        else in->va = va;
        in->shift += sh;
    }
    if (LAY_X(el).pos == POS_RELATIVE) {
        double dx, dy;
        lay_ext e = LAY_X(el);
        lay_rel_offset(st, &e, cw, -1, &dx, &dy);
        in->rx += dx;
        in->ry += dy;
    }
}

/* the boxes of the inline-blocks laid out in their own frames (lay_wm_atomic), by node (lay_wm_slot): each laid out once
   a run */
static vector<uint32_t> lay_wm_boxes;
/* a node's slot in lay_wm_boxes: an element's twice its index, a generated box's (LAY_GEN) twice its own and one */
static size_t lay_wm_slot(uint32_t node) { return node & LAY_GEN ? 2 * (size_t)(node & LAY_GIDX) + 1 : 2 * (size_t)node; }
/* the box an inline-block of another frame than its line's was laid out as (lay_wm_atomic), 0 none */
static uint32_t lay_wm_box_of(uint32_t node) {
    size_t sl = lay_wm_slot(node);
    return sl < len(lay_wm_boxes) ? at(lay_wm_boxes, sl) : 0;
}
/* margins as they collapse (below, lay_mc_of) */
typedef struct lay_mc { double pos, neg, qpos; } lay_mc;
static uint32_t lay_wm_root(LCTX, uint32_t parent, uint32_t el, double cw, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty, int pf, int cf);
/* an inline-block of frame cf in a line of frame pf: laid out as a writing-mode root (its available inline size the
   line's block's definite size, else the viewport's), its box set in pf, not yet in the tree; once a run */
static uint32_t lay_wm_atomic(LCTX, uint32_t node, int pf, int cf, double cw) {
    uint32_t had = lay_wm_box_of(node);
    if (had) return had;
    lay_bk k;
    k.ox = 0;
    k.oy = 0;
    k.ch = lay_line_pch;
    k.fw = -1;
    k.shrink = 1;
    k.item = 0;
    k.qs = 0;
    lay_mc mt, mb;
    int empty;
    uint32_t b = lay_wm_root(LARGS, 0, node, cw, &k, &mt, &mb, &empty, pf, cf);
    size_t sl = lay_wm_slot(node);
    if (sl >= len(lay_wm_boxes)) resize(&lay_wm_boxes, sl + 1);
    lay_wm_boxes[sl] = b;
    return b;
}

static void lay_node_pieces(LCTX, uint32_t node, int deco, double cw, vector<lay_piece> *ps, int *sp, const lay_inh *ih) {
    if (node & LAY_GTEXT) { lay_text_pieces(LARGS, node, deco, ps, sp, ih); return; }
    if (!(node & LAY_GEN)) {
        if (d->node[node].kind == DOM_NODE_KIND_TEXT) { lay_text_pieces(LARGS, node, deco, ps, sp, ih); return; }
        if (d->node[node].kind != DOM_NODE_KIND_ELEMENT) return;
    }
    lay_style s;
    lay_style_of(LARGS, node, &s);
    int dsp = lay_display_of(LARGS, node);
    if (dsp == D_NONE) return;
    lay_piece p;
    memset(&p, 0, sizeof p);
    lay_piece_style(&p, &s);
    p.node = node;
    p.elem = node;
    p.shift = (float)ih->shift;
    p.rx = (float)ih->rx;
    p.ry = (float)ih->ry;
    p.va = (uint8_t)ih->va;
    int pos = LAY_X(node).pos;
    if (pos == POS_ABSOLUTE || pos == POS_FIXED || LAY_X(node).flt) {
        /* out of the flow: its static position, or the float placed where the line is */
        p.kind = (pos == POS_ABSOLUTE || pos == POS_FIXED) ? P_ABS : P_FLOAT;
        push_back(ps, p);
        return;
    }
    if (lay_is_named(d, node, "br")) {
        if (len(*ps) && back(*ps).kind == P_SPACE && !back(*ps).keep) pop_back(ps);
        p.kind = P_BREAK;
        push_back(ps, p);
        *sp = 1;
        return;
    }
    if (dsp == D_INLINE_BLOCK || lay_block_level(dsp)) {
        double w, h;
        uint32_t par = lay_parent_of(d, gx, node);
        int pf = par ? lay_frame(data(*sc)[lay_si(sx, gx, par)].wm) : 0, cf = lay_frame(s.wm);
        if (pf != cf && !lay_replaced(d, node)) {
            /* an inline-block of another writing mode's frame than its line's: laid out now, in its own frame, as a
               formatting context of its own (lay_wm_root), its box kept for the line */
            uint32_t ob = lay_wm_atomic(LARGS, node, pf, cf, cw);
            p.kind = P_ATOMIC;
            p.aw = (float)t->box[ob].w;
            p.ah = (float)t->box[ob].h;
            p.w = t->box[ob].ml + t->box[ob].bl + t->box[ob].pl + t->box[ob].w + t->box[ob].pr + t->box[ob].br + t->box[ob].mr;
            push_back(ps, p);
            *sp = 0;
            return;
        }
        /* a percentage height against the line's block's definite height (an image's then sized by it) */
        lay_len hr = LAY_X(node).hraw;
        if (s.height.unit == LAY_AUTO && hr.unit != LAY_AUTO && !lay_content_sized(hr) && lay_line_pch >= 0) {
            double ve = s.b[0] + s.b[2] + lay_res(st, s.p[0], cw) + lay_res(st, s.p[2], cw);
            s.height.unit = LAY_PX;
            s.height.v = lay_res(st, hr, lay_line_pch) - (s.border_box ? ve : 0);
            if (s.height.v < 0) s.height.v = 0;
        }
        lay_atomic_size(LARGS, node, &s, cw, &w, &h);
        p.kind = P_ATOMIC;
        p.img = (uint8_t)lay_is_named(d, node, "img");
        p.aw = (float)w;
        p.ah = (float)h;
        p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw) + w + lay_res(st, s.p[1], cw) + s.b[1] + lay_res(st, s.m[1], cw);
        /* its own vertical-align: the box's, against its parent's font */
        int va = LAY_X(node).va;
        if (va == VA_SUB || va == VA_SUPER) {
            lay_style pst;
            lay_style_of(LARGS, lay_parent_of(d, gx, node), &pst);
            if (va == VA_SUB) p.shift += (float)(pst.fs / 5 + 1);
            else p.shift -= (float)(pst.fs / 3 + 1);
        } else if (va == VA_LEN) p.shift -= (float)lay_res(st, LAY_X(node).valen, s.lh);
        else if (va != VA_BASELINE) p.va = (uint8_t)va;
        push_back(ps, p);
        *sp = 0;
        return;
    }
    int inner = deco | s.deco;
    lay_inh in = *ih;
    lay_inline_shift(LARGS, node, &s, cw, &in);
    p.shift = (float)in.shift;
    p.rx = (float)in.rx;
    p.ry = (float)in.ry;
    p.va = (uint8_t)in.va;
    p.kind = P_OPEN;
    p.bg = s.bg;
    p.w = lay_res(st, s.m[3], cw) + s.b[3] + lay_res(st, s.p[3], cw);
    push_back(ps, p);
    if (lay_is_named(d, node, "wbr")) lay_lb_marks++;
    if (node & LAY_GEN) lay_node_pieces(LARGS, node | LAY_GTEXT, inner, cw, ps, sp, &in);
    else {
        /* its children, after its ::before and before its ::after */
        uint32_t fl0 = node < len(*sx) ? at(*sx, node) : 0;
        if (fl0 & LAY_HAS_BEFORE) {
            int64_t g = lay_gen_find(gx, node, 1);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp, &in);
        }
        int closed = lay_closed_details(d, node);
        for (uint32_t c = dom_flat_first(d, node); c; c = dom_flat_next(d, c))
            if (!closed || d->node[c].kind != DOM_NODE_KIND_TEXT) lay_node_pieces(LARGS, c, inner, cw, ps, sp, &in);
        if (fl0 & LAY_HAS_AFTER) {
            int64_t g = lay_gen_find(gx, node, 2);
            if (g >= 0) lay_node_pieces(LARGS, LAY_GEN | (uint32_t)g, inner, cw, ps, sp, &in);
        }
    }
    p.kind = P_CLOSE;
    p.w = lay_res(st, s.p[1], cw) + s.b[1] + lay_res(st, s.m[1], cw);
    push_back(ps, p);
}
/* ---- line breaking: the break opportunities in a run's words (src/layout/linebreak.h) ---- */

/* the language of an element as line breaking reads it (its lang attribute, or its nearest ancestor's, a shadow
   tree's host's beyond its root): 0 none (or empty), 1 Japanese, 2 Chinese (zh and Chinese's other macrolanguage
   tags), 3 Korean, 4 any other */
/* the language of an element (its lang attribute, or its nearest ancestor's, a shadow tree's host's beyond its
   root), lower-cased into buf (cap bytes, a NUL after it): its length, 0 for none or an empty one */
static size_t lay_lang_of(const dom_document *d, uint32_t el, char *buf, size_t cap) {
    buf[0] = 0;
    if (!names_find(d, "lang", 4)) return 0;
    for (uint32_t x = el, steps = 0; x && steps <= d->node_len; steps++) {
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT) {
            uint32_t a = lay_attr(d, x, "lang");
            if (a) {
                amb_span v = d->attr[a].value;
                const char *s = text_ptr(d, v);
                size_t n = 0;
                while (n < v.len && n + 1 < cap) {
                    char c = s[n];
                    buf[n] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c == '_' ? '-' : c);
                    n++;
                }
                buf[n] = 0;
                return n;
            }
        }
        uint32_t p = d->node[x].parent;
        uint32_t h = dom_shadow_root_host(d, p);
        x = h ? h : p;
    }
    return 0;
}
static int lay_lang_class(const dom_document *d, uint32_t el) {
    char l[64];
    size_t ln = lay_lang_of(d, el, l, sizeof l);
    if (!ln) return 0;
    size_t n = 0;
    while (n < ln && l[n] != '-') n++;
    if (n == 2 && l[0] == 'j' && l[1] == 'a') return 1;
    if (n == 2 && l[0] == 'k' && l[1] == 'o') return 3;
    if (n == 2 && l[0] == 'z' && l[1] == 'h') return 2;
    if (n == 3) {
        /* Chinese's macrolanguage tags (Blink's IsMacrolanguageChinese; ICU takes them as zh) */
        static const char *const zh3 = "cdo cjy cmn cnp cpx csp czh czo gan hak hnm hsn luh lzh mnp nan sjc wuu yue";
        for (size_t q = 0; zh3[q]; q += 4)
            if (zh3[q] == l[0] && zh3[q + 1] == l[1] && zh3[q + 2] == l[2]) return 2;
    }
    return 4;
}
/* the hyphenation dictionary of an element's language, as Blink's HyphenationMinikin::MapLocale picks Chromium's:
   0 en-US's patterns (en itself, en-US and the English of the US's territories), 1 en-GB's (every other English);
   -1 none (no language, or one whose patterns Chromium has but not this module: German, French and some 50 more) */
static int lay_hyph_dict(const dom_document *d, uint32_t el) {
    char l[64];
    size_t ln = lay_lang_of(d, el, l, sizeof l);
    if (ln == 2) return l[0] == 'e' && l[1] == 'n' ? 0 : -1;
    if (ln < 4 || l[0] != 'e' || l[1] != 'n' || l[2] != '-') return -1;
    static const char *const us = "us as gu mh mp pr um vi";
    if (ln == 5 || l[5] == '-')
        for (size_t q = 0; us[q]; q += 3)
            if (us[q] == l[3] && us[q + 1] == l[4]) return 0;
    return 1;
}

/* a style's line breaking (amb_lbstyle) as Blink's LineBreaker::SetCurrentStyle sets its break iterator: the
   rules its locale and line-break pick (no locale, Chromium's default rules whatever line-break says; Japanese and
   Korean auto line_normal, Chinese line_normal_cj; strict line, loose line_loose, or Chinese's and Japanese's
   _cj ones, normal theirs; Korean's strict, whose rules Chromium lacks, the default), break-all and keep-all,
   line-break: anywhere every grapheme; overflow-wrap: anywhere and word-break: break-word every grapheme for the
   min-content width */
static amb_lbstyle lay_lb_style(const lay_ext *x, int ws, int lang) {
    amb_lbstyle s;
    memset(&s, 0, sizeof s);
    s.wrap = (uint8_t)(ws == WS_NORMAL || ws == WS_PRE_WRAP || ws == WS_PRE_LINE);
    s.shy = (uint8_t)(x->hyph != 0);
    s.loose = (uint8_t)(x->lbrk == 1);
    if (x->lbrk == 4) s.type = AMB_LBT_BREAK_CHAR;
    else if (x->wbrk == 1) s.type = AMB_LBT_BREAK_ALL;
    else if (x->wbrk == 2) s.type = AMB_LBT_KEEP_ALL;
    s.mintype = s.type;
    if (s.type != AMB_LBT_BREAK_CHAR && (x->owrap == 2 || x->wbrk == 3)) s.mintype = AMB_LBT_BREAK_CHAR;
    int r = AMB_LBR_NORMAL;
    if (lang && x->lbrk != 4) {
        int lb = x->lbrk;
        if (lb == 3) r = lang == 3 ? AMB_LBR_NORMAL : AMB_LBR_STRICT;
        else if (lb == 1) r = lang == 4 ? AMB_LBR_LOOSE : AMB_LBR_LOOSE_CJ;
        else if (lb == 2) r = lang == 4 ? AMB_LBR_NORMAL : AMB_LBR_NORMAL_CJ;
        else r = lang == 2 ? AMB_LBR_NORMAL_CJ : AMB_LBR_NORMAL;
    }
    s.rules = (uint8_t)r;
    return s;
}

static void lay_src_bytes(LCTX, uint32_t node, uint32_t off, uint32_t n, vector<char> *out);
/* the bytes of word piece p as line breaking reads them: text-transform's letters (each the length it was), soft
   hyphens kept */
static void lay_lb_bytes(LCTX, lay_piece p, vector<char> *out) {
    vector<char> raw;
    lay_src_bytes(LARGS, p.node, p.off, p.len, &raw);
    if (p.tt && p.kind == P_WORD) {
        vector<char> tw;
        lay_transform(data(raw), len(raw), p.tt, &tw);
        if (len(tw) == len(raw)) { append_range(out, data(tw), len(tw)); return; }
    }
    append_range(out, data(raw), len(raw));
}

/* two styles' line breaking the same */
static int lay_lbs_eq(amb_lbstyle a, amb_lbstyle b) {
    return a.type == b.type && a.mintype == b.mintype && a.rules == b.rules && a.loose == b.loose && a.shy == b.shy && a.wrap == b.wrap;
}

/* the run's pieces from `from`, their words split where a line may break inside them (Blink's break iterator over
   the run's text: src/layout/linebreak.c), each piece a break opportunity is before marked (bb: before the inline
   elements opened before it, as Blink's line takes their open tags); a word ending in a soft hyphen it breaks after
   marked LBF_SHY, one of overflow-wrap's emergency breaks LBF_EMERG. The text a run's line breaking reads: each word
   (its letters as laid out), a space each collapsed space, a kept space's own, a forced break's line feed, an
   atomic box's (a float's, an absolutely positioned box's) U+FFFC, a wbr's zero-width space (U+200B). A run of
   ASCII words with no character ASCII breaks about (lay_chars's LBF_HOT), in styles that break as normal and with
   no wbr, is passed over (lay_run_pieces: lay_lb_marks): its opportunities are its spaces. A break after a space
   (the spaces' own), at an atomic box's edge and at a float is laid out as it was */
static void lay_break_pieces(LCTX, vector<lay_piece> *ps, size_t from) {
    size_t np = len(*ps);
    /* the run's text: its code points, each one's piece and byte in it, and its style's line breaking */
    vector<uint32_t> cps, cpk, cpo;
    vector<uint8_t> cps_st, flags;
    vector<amb_lbstyle> styles;
    uint32_t last_el = 0xffffffffu;
    uint8_t last_si = 0;
    for (size_t k = from; k < np; k++) {
        lay_piece p = at(*ps, k);
        uint32_t el = p.kind == P_WORD || p.kind == P_SPACE ? p.elem : p.node;
        if (el != last_el) {
            uint32_t si = lay_si(sx, gx, el);
            int ws = data(*sc)[si].ws;
            amb_lbstyle s = lay_lb_style(data(*xc) + si, ws, lay_lang_class(d, lay_dom(gx, el)));
            size_t q = 0;
            while (q < len(styles) && !lay_lbs_eq(at(styles, q), s)) q++;
            if (q == len(styles)) {
                if (len(styles) >= 250) q = 0;
                else push_back(styles, s);
            }
            last_el = el;
            last_si = (uint8_t)q;
        }
        if (p.kind == P_WORD) {
            vector<char> b;
            lay_lb_bytes(LARGS, p, &b);
            size_t z = 0;
            while (z < len(b)) {
                size_t z0 = z;
                uint32_t cp = (unsigned char)at(b, z) < 0x80 ? (uint32_t)(unsigned char)at(b, z++) : amb_utf8_next(data(b), len(b), &z);
                push_back(cps, cp);
                push_back(cpk, (uint32_t)k);
                push_back(cpo, (uint32_t)z0);
                push_back(cps_st, last_si);
            }
            continue;
        }
        uint32_t c = 0;
        uint32_t count = 1;
        if (p.kind == P_SPACE) { c = ' '; if (p.keep) count = p.len; }
        else if (p.kind == P_BREAK) c = '\n';
        else if (p.kind == P_ATOMIC || p.kind == P_FLOAT || p.kind == P_ABS) c = 0xFFFC;
        else if (p.kind == P_OPEN && lay_is_named(d, p.node, "wbr")) c = 0x200B;
        else continue;
        for (uint32_t q = 0; q < count; q++) {
            push_back(cps, c);
            push_back(cpk, (uint32_t)k);
            push_back(cpo, q);
            push_back(cps_st, last_si);
        }
    }
    size_t n = len(cps);
    if (n < 2) return;
    amb_line_breaks(&cps, &cps_st, &styles, &flags);
    /* the pieces again, the words split at their opportunities and the flags set */
    vector<lay_piece> out;
    reserve(out, np - from + 16);
    size_t i = 0;                       /* the code point the next piece begins at */
    for (size_t k = from; k < np; k++) {
        lay_piece p = at(*ps, k);
        p.bb = 0;
        p.lbf = 0;
        size_t c0 = i;
        while (i < n && at(cpk, i) == (uint32_t)k) i++;
        if (p.kind != P_WORD || c0 == i) { push_back(out, p); continue; }
        uint8_t emerg = (uint8_t)(LAY_X(p.elem).owrap || LAY_X(p.elem).wbrk == 3 ? LBF_EMERG : 0);
        /* a break before the word, after a word or a wbr: before the inline elements opened since the last one
           closed before it (their open tags go to the next line in Blink, its close tags stay) */
        uint8_t f0 = 0;
        if (c0 > 0) {
            int pk = at(*ps, at(cpk, c0 - 1)).kind;
            if (pk == P_WORD || pk == P_OPEN) f0 = (uint8_t)(at(flags, c0) & (AMB_BRK_LINE | AMB_BRK_MIN));
        }
        if (f0) {
            size_t tgt = len(out);
            while (tgt > 0 && at(out, tgt - 1).kind == P_OPEN && !lay_is_named(d, at(out, tgt - 1).node, "wbr")) tgt--;
            if (tgt < len(out)) {
                lay_piece o = at(out, tgt);
                o.bb = f0;
                out[tgt] = o;
            } else p.bb = f0;
        }
        /* the word cut at each opportunity inside it */
        vector<char> raw;
        lay_src_bytes(LARGS, p.node, p.off, p.len, &raw);
        uint32_t start = 0;
        uint8_t bb = p.bb;
        uint8_t tt0 = (uint8_t)(p.tt & ~TT_MID);
        for (size_t c = c0 + 1; c <= i; c++) {
            uint8_t fl = c < i ? (uint8_t)(at(flags, c) & (AMB_BRK_LINE | AMB_BRK_MIN)) : 0;
            /* cut too where a run of ideographic spaces begins or ends (LBF_HANG) */
            int edge = c < i && ((at(cps, c) == 0x3000) != (at(cps, c - 1) == 0x3000));
            if (c < i && !fl && !edge) continue;
            uint32_t end = c < i ? at(cpo, c) : p.len;
            if (end > len(raw)) end = (uint32_t)len(raw);
            lay_piece q = p;
            q.off = p.off + start;
            q.len = end - start;
            q.bb = bb;
            q.lbf = emerg;
            q.rtl = 0;
            q.soft = 0;
            for (uint32_t z = start; z < end; z++) {
                unsigned char b0 = (unsigned char)at(raw, z);
                if (b0 == 0xC2 && z + 1 < end && (unsigned char)at(raw, z + 1) == 0xAD) q.soft = 1;
                else if (b0 >= 0xC2 && !q.rtl && lay_bidi_hot(data(raw), len(raw), z)) q.rtl = 1;
            }
            /* a soft hyphen it ends in, a break after it */
            uint8_t fend = c < n ? (uint8_t)(at(flags, c) & (AMB_BRK_LINE | AMB_BRK_MIN)) : 0;
            if (end >= start + 2 && (unsigned char)at(raw, end - 2) == 0xC2 && (unsigned char)at(raw, end - 1) == 0xAD && fend) q.lbf |= LBF_SHY;
            /* a run of ideographic spaces alone */
            int ideo = end > start && (end - start) % 3 == 0;
            for (uint32_t z = start; z + 3 <= end && ideo; z += 3)
                ideo = (unsigned char)at(raw, z) == 0xE3 && (unsigned char)at(raw, z + 1) == 0x80 && (unsigned char)at(raw, z + 2) == 0x80;
            if (ideo) q.lbf |= LBF_HANG;
            /* capitalize: a part after the first goes on with the word when a letter or a digit ends the part before */
            if (start > 0 && tt0 == TT_CAPITALIZE) {
                size_t zz = start - 1, bl;
                while (zz > 0 && ((unsigned char)at(raw, zz) & 0xC0) == 0x80) zz--;
                int cl = lay_word_class(data(raw), start, zz, &bl);
                q.tt = (uint8_t)(cl >= 1 && cl <= 3 ? (tt0 | TT_MID) : tt0);
            }
            if (q.len) push_back(out, q);
            start = end;
            bb = fl;
        }
    }
    resize(ps, from);
    append_range(ps, data(out), len(out));
}

static void lay_run_pieces(LCTX, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double cw, vector<lay_piece> *ps) {
    lay_style s;
    lay_style_of(LARGS, container, &s);
    int sp = 1;
    lay_inh ih;
    memset(&ih, 0, sizeof ih);
    size_t from = len(*ps);
    uint32_t marks = lay_lb_marks;
    for (uint32_t k = first; k < first + n; k++) lay_node_pieces(LARGS, at(*items, k), s.deco, cw, ps, &sp, &ih);
    if (lay_lb_marks != marks) lay_break_pieces(LARGS, ps, from);
    lay_shape_pieces(LARGS, ps, from);
}
/* a run shows something: a word, a kept space, an atomic box, a break, or an inline
   element's non-zero edge */
static int lay_run_shows(const vector<lay_piece> *ps) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_WORD || p.kind == P_ATOMIC || p.kind == P_BREAK || (p.kind == P_SPACE && p.keep)) return 1;
        if ((p.kind == P_OPEN || p.kind == P_CLOSE) && p.w > 0) return 1;
    }
    return 0;
}

/* ---- intrinsic widths: an element's margin box at its min- and max-content ---- */

/* sticky: the table cell width calculation quirk (quirks mode; the Quirks Mode Standard 3.8): in an
   auto-width table cell's own lines, no soft wrap opportunity before or after an img */
static int lay_hyph_parts(LCTX, const vector<lay_piece> *ps, size_t k, double *first, double *mid, double *last);
static void lay_run_intrinsic(LCTX, const vector<lay_piece> *ps, double *mn, double *mx, int sticky) {
    double line = 0, chunk = 0, pend = 0;
    *mn = 0;
    *mx = 0;
    int start = 1;
    uint32_t hy_el = 0xffffffffu;       /* the element whose hyphens were looked at last, and whether they are auto */
    int hy_on = 0;
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if ((p.bb & AMB_BRK_MIN) && !start) {
            /* a break opportunity inside a word, or between words with no space (lay_break_pieces): the chunk ends,
               a word ending in a soft hyphen before it with its hyphen (Blink's FastMinTextContext) */
            double hy = 0;
            size_t q = k;
            while (q > 0 && (at(*ps, q - 1).kind == P_CLOSE || at(*ps, q - 1).kind == P_OPEN)) q--;
            if (q > 0 && at(*ps, q - 1).kind == P_WORD && (at(*ps, q - 1).lbf & LBF_SHY)) {
                lay_piece h = at(*ps, q - 1);
                hy = amb_text_width(f, h.face, h.size, "-", 1) + h.ls;
            }
            if (chunk + hy > *mn) *mn = chunk + hy;
            chunk = 0;
        }
        double h1, hm, hl;
        if (p.kind == P_WORD && p.elem != hy_el) {
            hy_el = p.elem;
            hy_on = LAY_X(p.elem).hyph == 2;
        }
        if (p.kind == P_WORD && hy_on && lay_hyph_parts(LARGS, ps, k, &h1, &hm, &hl)) {
            /* hyphens: auto: the word's first part ends the chunk before it, its last begins the next */
            if (chunk + h1 > *mn) *mn = chunk + h1;
            if (hm > *mn) *mn = hm;
            chunk = hl;
            line += pend + p.w;
            pend = 0;
            start = 0;
            continue;
        }
        if (sticky && p.kind == P_ATOMIC && p.img) {
            line += pend + p.w;
            pend = 0;
            chunk += p.w;
            start = 0;
            continue;
        }
        if (p.kind == P_BREAK) {
            if (line > *mx) *mx = line;
            if (chunk > *mn) *mn = chunk;
            line = 0; chunk = 0; pend = 0; start = 1;
            continue;
        }
        if (p.kind == P_SPACE) {
            if (p.brk) { if (chunk > *mn) *mn = chunk; chunk = 0; }     /* no wrap at a nowrap space */
            if (p.brk) { if (!start) pend += p.w; }
            else { chunk += p.w; line += pend + p.w; pend = 0; }
            continue;
        }
        if (p.kind == P_ATOMIC) { if (chunk > *mn) *mn = chunk; chunk = 0; }
        line += pend + p.w;
        pend = 0;
        chunk += p.w;
        start = 0;
        if (p.kind == P_ATOMIC) { if (chunk > *mn) *mn = chunk; chunk = 0; }
    }
    if (line > *mx) *mx = line;
    if (chunk > *mn) *mn = chunk;
}

static double lay_edges_px(const css_style *st, const lay_style *s, double cw) {
    return (s->m[1].unit == LAY_AUTO ? 0 : lay_res(st, s->m[1], cw)) + (s->m[3].unit == LAY_AUTO ? 0 : lay_res(st, s->m[3], cw)) +
           s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw);
}

static void lay_table_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx);

static lay_len lay_minw_px(LCTX, uint32_t el, const lay_style *s, double cw, lay_len l);
/* el's margin box at its min- and max-content widths; its declared width's when it has one
   and own_width (a min-width its content gives holding it wider), else its content's */
static double lay_wm_extent(LCTX, uint32_t el, double cw, int pf, int cf);
static void lay_child_intrinsic(LCTX, uint32_t el, uint32_t c, double cw, double *mn, double *mx);
static void lay_intrinsic_of(LCTX, uint32_t el, double cw, double *mn, double *mx, int own_width) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    double edges = lay_edges_px(st, &s, cw);
    *mn = 0;
    *mx = 0;
    if (s.width.unit == LAY_PX && own_width) {
        double box_edges = s.b[1] + s.b[3] + lay_res(st, s.p[1], cw) + lay_res(st, s.p[3], cw);
        *mn = *mx = s.width.v + edges - (s.border_box ? box_edges : 0);
        lay_len mw = LAY_X(el).minw;
        if (lay_content_sized(mw)) {
            mw = lay_minw_px(LARGS, el, &s, cw, mw);
            double v = mw.v + edges - (s.border_box ? box_edges : 0);
            if (v > *mn) { *mn = v; *mx = v; }
        }
        return;
    }
    if (lay_replaced(d, el)) {
        double w, h;
        lay_atomic_size(LARGS, el, &s, cw, &w, &h);
        *mn = *mx = w + edges;
        return;
    }
    /* a query container's content counts for nothing in its inline size (size and inline-size containment: CSS
       Contain 2, 3.2; a table's cells have no containment) */
    if (LAY_X(el).cq && s.display != D_TABLE && s.display != D_CELL && s.display != D_INLINE) { *mn = *mx = edges; return; }
    if (s.display == D_TABLE) {
        /* a table's border box's widths (its borders a collapsing table's halves of its edges), its margins about them */
        double mg = (s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw)) + (s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw));
        lay_table_intrinsic(LARGS, el, cw, mn, mx);
        *mn += mg;
        *mx += mg;
        return;
    }
    if (LAY_X(el).lx == LX_GRID) {
        lay_grid_intrinsic(LARGS, el, cw, mn, mx);
        *mn += edges;
        *mx += edges;
        return;
    }
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    /* a row flex container's items side by side (wrapping: its widest one at least) */
    int side = LAY_X(el).lx == LX_FLEX && LAY_X(el).frow;
    int wraps = LAY_X(el).fwrap != 0;
    double gap = side ? lay_res(st, LAY_X(el).cgap, cw) : 0;
    int nitems = 0;
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        double a, b;
        if (sg.block == SEG_ABS) continue;
        if (sg.block) lay_child_intrinsic(LARGS, el, sg.node, cw, &a, &b);
        else {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, cw, &ps);
            lay_run_intrinsic(LARGS, &ps, &a, &b, lay_quirks && s.display == D_CELL && s.width.unit == LAY_AUTO);
        }
        if (side) {
            if (nitems++) { *mx += gap; if (!wraps) *mn += gap; }
            *mx += b;
            if (wraps) { if (a > *mn) *mn = a; }
            else *mn += a;
            continue;
        }
        if (a > *mn) *mn = a;
        if (b > *mx) *mx = b;
    }
    /* a fieldset's rendered legend, set on its top border above its content: its widths count as a child's do (Chromium's
       fieldset takes its legend's min- and max-content widths) */
    uint32_t lg = lay_rendered_legend(LARGS, el);
    if (lg) {
        double a, b;
        lay_intrinsic(LARGS, lg, cw, &a, &b);
        if (a > *mn) *mn = a;
        if (b > *mx) *mx = b;
    }
    *mn += edges;
    *mx += edges;
}

static void lay_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) { lay_intrinsic_of(LARGS, el, cw, mn, mx, 1); }
/* a child's contributions to its container's (el's) min- and max-content inline sizes: a box orthogonal to it (CSS
   Writing Modes 4, 7.3) contributes its block size laid out, its margin box's across el's inline axis (Chromium's) */
static void lay_child_intrinsic(LCTX, uint32_t el, uint32_t c, double cw, double *mn, double *mx) {
    uint32_t ci = lay_si(sx, gx, c);
    int pf = lay_frame(data(*sc)[lay_si(sx, gx, el)].wm), cf = lay_frame(data(*sc)[ci].wm), cdisp = data(*sc)[ci].display;
    if ((pf == 0) != (cf == 0) && !lay_replaced(d, c) && cdisp != D_INLINE && cdisp != D_NONE) {
        *mn = *mx = lay_wm_extent(LARGS, c, cw, pf, cf);
        return;
    }
    lay_intrinsic(LARGS, c, cw, mn, mx);
}

/* a min-width its content gives (min-content, max-content, fit-content; the user agent's min-content of a fieldset),
   as a px length of the box box-sizing names: of its content's min- or max-content width, its edges' and margins' aside */
static lay_len lay_minw_px(LCTX, uint32_t el, const lay_style *s, double cw, lay_len l) {
    if (!lay_content_sized(l)) return l;
    double mn, mx;
    lay_intrinsic_of(LARGS, el, cw, &mn, &mx, 0);
    double m = (s->m[1].unit == LAY_AUTO ? 0 : lay_res(st, s->m[1], cw)) + (s->m[3].unit == LAY_AUTO ? 0 : lay_res(st, s->m[3], cw));
    double box = s->b[1] + s->b[3] + lay_res(st, s->p[1], cw) + lay_res(st, s->p[3], cw);
    double v = (l.unit == LAY_MAXC ? mx : mn) - m;
    if (l.unit == LAY_FITC) {
        v = cw - m > mn - m ? cw - m : mn - m;
        if (v > mx - m) v = mx - m;
    }
    l.unit = LAY_PX;
    l.v = s->border_box ? v : v - box;
    l.pct = 0;
    return l;
}

/* ---- the store's records ---- */

static uint32_t lay_new_box(LCTX, uint32_t parent, uint32_t el, layout_box_kind kind) {
    layout_box_h h = box_new(t);
    t->box[h.index].kind = kind;
    t->box[h.index].opacity = 1;
    /* the writing mode its content is laid out in: its style's, an anonymous box's its parent's */
    t->box[h.index].wm = el ? data(*sc)[lay_si(sx, gx, el)].wm : parent ? t->box[parent].wm : 0;
    if (el & LAY_GEN) {
        /* a pseudo-element's box: its element's, and which pseudo-element */
        lay_gen g = at(*gx, el & LAY_GIDX);
        t->box[h.index].node = node_handle(d, g.el);
        t->box[h.index].pseudo = (uint8_t)g.pseudo;
    } else if (el) t->box[h.index].node = node_handle(d, el);
    if (parent) children_append(t, parent, h.index);
    return h.index;
}

/* a box's margins (vertical ones, and horizontal ones that are not auto), borders,
   paddings and colours, against its containing block's width cw */
static void lay_edges(layout_tree *t, const css_style *st, uint32_t b, const lay_style *s, double cw) {
    t->box[b].mt = lay_res(st, s->m[0], cw);
    t->box[b].mb = lay_res(st, s->m[2], cw);
    t->box[b].ml = lay_res(st, s->m[3], cw);
    t->box[b].mr = lay_res(st, s->m[1], cw);
    t->box[b].bt = s->b[0];
    t->box[b].br = s->b[1];
    t->box[b].bb = s->b[2];
    t->box[b].bl = s->b[3];
    t->box[b].pt = lay_res(st, s->p[0], cw);
    t->box[b].pr = lay_res(st, s->p[1], cw);
    t->box[b].pb = lay_res(st, s->p[2], cw);
    t->box[b].pl = lay_res(st, s->p[3], cw);
    t->box[b].background = s->bg;
    t->box[b].bct = s->bc[0];
    t->box[b].bcr = s->bc[1];
    t->box[b].bcb = s->bc[2];
    t->box[b].bcl = s->bc[3];
    t->box[b].bst = s->bs[0];
    t->box[b].bsr = s->bs[1];
    t->box[b].bsb = s->bs[2];
    t->box[b].bsl = s->bs[3];
    t->box[b].avail = cw;
}

/* a block-level box's width and horizontal margins (CSS 2.1, 10.3.3): a declared
   width is the border box's under box-sizing: border-box; max-width holds it narrower */
static void lay_block_width(layout_tree *t, const css_style *st, uint32_t b, const lay_style *s, double cw, const lay_len *minw) {
    double edges = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    int mla = s->m[3].unit == LAY_AUTO, mra = s->m[1].unit == LAY_AUTO;
    double ml = mla ? 0 : t->box[b].ml, mr = mra ? 0 : t->box[b].mr, w;
    int declared = s->width.unit != LAY_AUTO;
    if (!declared) {
        w = cw - ml - mr - edges;
        t->box[b].how = LAYOUT_WIDTH_HOW_FILL;
    } else {
        w = lay_res(st, s->width, cw) - (s->border_box ? edges : 0);
        t->box[b].how = s->width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
    }
    if (w < 0) w = 0;
    if (s->maxw.unit != LAY_AUTO) {
        double mw = lay_res(st, s->maxw, cw) - (s->border_box ? edges : 0);
        if (mw < 0) mw = 0;
        if (w > mw) { w = mw; declared = 1; t->box[b].how = LAYOUT_WIDTH_HOW_MAX_WIDTH; }
    }
    if (minw && minw->unit != LAY_AUTO && !lay_content_sized(*minw)) {
        double mn = lay_res(st, *minw, cw) - (s->border_box ? edges : 0);
        if (w < mn) { w = mn; declared = 1; t->box[b].how = LAYOUT_WIDTH_HOW_MIN_WIDTH; }
    }
    if (declared) {
        double rest = cw - w - edges;
        if (mla && mra) { if (rest > 0) { ml = rest / 2; mr = rest / 2; } else mr = rest; }
        else if (mla) ml = rest - mr;
        else mr = rest - ml;
    }
    t->box[b].w = w;
    t->box[b].ml = ml;
    t->box[b].mr = mr;
}

/* a box's border box's height; a table's, its wrapper's: its captions above and below it too */
static double lay_border_height(const layout_tree *t, uint32_t b) {
    return t->box[b].cap_top + t->box[b].bt + t->box[b].pt + t->box[b].h + t->box[b].pb + t->box[b].bb + t->box[b].cap_bottom;
}

/* a child placed in its parent's content box, its border box's top-left at (bx, by) (a table's wrapper's, its captions
   above its border box) */
static void lay_place(layout_tree *t, uint32_t b, double bx, double by) {
    /* a relatively positioned box: its offset too */
    t->box[b].x = bx + t->box[b].bl + t->box[b].pl + t->box[b].dx;
    t->box[b].y = by + t->box[b].cap_top + t->box[b].bt + t->box[b].pt + t->box[b].dy;
}
/* ---- margins as they collapse: the largest positive one and the most negative ---- */

/* margins collapsed into one: the largest positive and the most negative; and, in quirks mode,
   the largest positive quirky one (Blink's quirky margins: the user agent's sheet's top margin of
   a p, a heading, a list, a blockquote or a pre, and the bottom one of a p or a pre), which a
   quirky container (the body, a table cell) leaves out at its start and at its end */
static lay_mc lay_mc_of(double m) { lay_mc c; c.pos = m > 0 ? m : 0; c.neg = m < 0 ? m : 0; c.qpos = 0; return c; }
/* a margin, quirky or not */
static lay_mc lay_mc_q(double m, int quirky) {
    lay_mc c = lay_mc_of(m);
    if (quirky && m > 0) { c.qpos = m; c.pos = 0; }
    return c;
}
static lay_mc lay_mc_add(lay_mc a, lay_mc b) {
    a.pos = a.pos > b.pos ? a.pos : b.pos;
    a.neg = a.neg < b.neg ? a.neg : b.neg;
    a.qpos = a.qpos > b.qpos ? a.qpos : b.qpos;
    return a;
}
static double lay_mc_v(lay_mc a) { return (a.pos > a.qpos ? a.pos : a.qpos) + a.neg; }
/* what a quirky container's end takes of them: its quirky margins left out */
static double lay_mc_vq(lay_mc a) { return a.pos + a.neg; }

/* x is an HTML element named one of the words of l (a space between each) */
static int lay_named_in(const dom_document *d, uint32_t x, const char *l) {
    if ((x & LAY_GEN) || !x || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_HTML) return 0;
    size_t i = 0;
    while (l[i]) {
        size_t b = i;
        while (l[i] && l[i] != ' ') i++;
        if (names_is(d, d->node[x].name, l + b, i - b)) return 1;
        if (l[i]) i++;
    }
    return 0;
}
/* el's top margin (side 0) or bottom one (side 2) is quirky (quirks mode; Blink's user agent
   sheet's __qem margins, measured in table cells): the user agent's sheet gave it, at the top of
   a p, a heading, a list (ul, ol, dl, menu, dir), a blockquote, a pre, a listing, an xmp or a
   plaintext, at the bottom of a p, a pre, a listing, an xmp or a plaintext */
static int lay_quirky_margin(LCTX, uint32_t el, int side) {
    if (!lay_quirks || !lay_named_in(d, el, side == 0 ? "p h1 h2 h3 h4 h5 h6 ul ol dl menu dir blockquote pre listing xmp plaintext"
                                                     : "p pre listing xmp plaintext")) return 0;
    uint32_t c0 = LAY_X(el).c0;
    if (!c0) return 0;
    uint32_t dc = st->cval[c0 + (side == 0 ? CSS_PROP_MARGIN_TOP : CSS_PROP_MARGIN_BOTTOM)].winner;
    uint32_t ru = dc && dc < st->decl_len ? st->decl[dc].rule : 0;
    uint32_t sh = ru && ru < st->rule_len ? st->rule[ru].sheet : 0;
    return sh && sh < st->sheet_len && st->sheet[sh].origin == CSS_ORIGIN_USER_AGENT;
}
/* el is a quirky container (quirks mode): the body element or a table cell, whose start and
   end leave out quirky margins */
static int lay_quirky_container(LCTX, uint32_t el, int display) {
    return lay_quirks && (display == D_CELL || lay_is_named(d, el, "body"));
}
/* el fills the viewport (quirks mode; the Quirks Mode Standard 3.6 and 3.7): the root html
   element, or the document's body element (the root's first body child) neither positioned
   absolutely nor floated; an auto height is the size it is given, its margins taken off */
static int lay_fills(LCTX, uint32_t el, int root) {
    if (!lay_quirks || (el & LAY_GEN) || !el) return 0;
    if (root) return lay_is_named(d, el, "html");
    if (!lay_is_named(d, el, "body")) return 0;
    uint32_t up = d->node[el].parent;
    if (!up || d->node[up].parent != 1 || !lay_is_named(d, up, "html")) return 0;
    for (uint32_t c = d->node[up].first_child; c && c != el; c = d->node[c].next_sibling) if (lay_is_named(d, c, "body")) return 0;
    int pos = LAY_X(el).pos;
    return pos != POS_ABSOLUTE && pos != POS_FIXED && !LAY_X(el).flt;
}

/* el's overflow as it is used: 1 when it clips its content. The body's, when the root's overflow is visible, is the
   viewport's, not its own (CSS Overflow 3, 3.3; measured in Chromium, in every mode): the body then clips nothing and
   makes no formatting context by it, its children's margins collapsing through it */
static int lay_clip_of(LCTX, uint32_t el) {
    if (!el) return 0;
    int clip = at(*xc, lay_si(sx, gx, el)).clip;
    if (clip != 1 || (el & LAY_GEN) || !lay_is_named(d, el, "body")) return clip;
    uint32_t up = d->node[el].parent;
    if (!up || d->node[up].parent != 1 || !lay_is_named(d, up, "html")) return clip;
    for (uint32_t c = d->node[up].first_child; c && c != el; c = d->node[c].next_sibling) if (lay_is_named(d, c, "body")) return clip;
    return LAY_X(up).clip == 1 ? clip : 0;
}

/* ---- lines ---- */

/* the vertical extent of a piece about its baseline: its font's line, and under
   line-height normal its fallback faces' (Chromium's AccumulateUsedFonts) */
static void lay_piece_vert(const lay_piece *p, double *above, double *below) {
    lay_leading(p->lh, p->asc, p->desc, above, below);
    if (!p->fb) return;
    if (p->fa > *above) *above = p->fa;
    if (p->fd > *below) *below = p->fd;
}

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty);
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, int qs, lay_mc *et,
                              lay_mc *eb, int *empty);
static void lay_box_style(LCTX, uint32_t b, uint32_t el, double cw, double ch);
static void lay_cq_restyle(LCTX, uint32_t el, double w, double h);
static int lay_decorated(LCTX, uint32_t el, const lay_style *es);
static double lay_marker_inside(LCTX, uint32_t el, const lay_style *ls);
static int lay_marker_strut(LCTX, uint32_t el, const lay_style *ls, const lay_style *line, double *above, double *below);
static void lay_ellipsize(LCTX, uint32_t ln, double limit, const lay_style *bs, int force);
static uint32_t lay_nth_line(layout_tree *t, uint32_t b, long *n, double oy, double *bottom, uint32_t *owner);
static void lay_clamp(LCTX, uint32_t b, uint32_t el, const lay_style *s);
static void lay_inline_piece(LCTX, uint32_t fr, uint32_t el, const lay_style *es, double cw);
static void lay_control_line(LCTX, uint32_t b, uint32_t el, const lay_style *ps, const char *s, size_t n, double x, double y, int center);
static void lay_theme(LCTX, uint32_t b, uint32_t el);
static void lay_box_paint(LCTX, uint32_t b, uint32_t si);
static amb_span lay_kept(layout_tree *t, const css_style *st, uint32_t c);
static void lay_flex(LCTX, uint32_t b, uint32_t el, double defh);
static void lay_grid(LCTX, uint32_t b, uint32_t el, double defh);

/* quirks mode's broken image of a declared size (lay_img_sized): its baseline its content box's bottom (Blink's
   fallback box, an inline-block that clips, fills it); its alt text in it, as that fallback lays it out (measured)
   -- in a container of a 1px border and 1px padding (none when the box is under 18px either way, where no icon is
   drawn), the 16px icon floated left at its start, the words in lines of the image's line height beside the icon
   (below it when the first does not fit there) and below it, clipped to the box */
static void lay_alt_box(LCTX, uint32_t b, uint32_t el, const lay_style *es) {
    /* its baseline its content box's bottom (paint draws such a box's border and icon, its lines' text in it) */
    t->box[b].has_baseline = 1;
    t->box[b].baseline = t->box[b].h;
    vector<char> alt;
    if (!lay_alt_text(d, el, &alt) || !len(alt)) return;
    double bw = t->box[b].w, bh = t->box[b].h, above = 0, below = 0;
    int icon = bw >= 18 && bh >= 18;
    double in = icon ? 2 : 0, cw = bw - 2 * in, sw = amb_text_width(f, es->face, es->fs, " ", 1) + es->ls + es->wsp;
    lay_leading(es->lh, es->asc, es->desc, &above, &below);
    const char *tx = data(alt);
    size_t n = len(alt), i = 0;
    double y = in;
    t->box[b].clip = 1;
    for (int guard = 0; i < n && y < bh && guard < 1000; guard++) {
        double fx = icon && y < in + 16 ? 16 : 0, room = cw - fx;
        /* as many words as fit (the first one whatever its width, but beside the icon only if it fits) */
        size_t e = i, last = i;
        double lw = 0;
        while (e < n) {
            size_t we = e;
            while (we < n && tx[we] != ' ') we++;
            double ww = amb_text_width(f, es->face, es->fs, tx + e, we - e) + es->ls * (double)lay_cps(tx + e, we - e);
            double add = (last > i ? sw : 0) + ww;
            if (last > i && lw + add > room + 0.01) break;
            if (last == i && fx > 0 && ww > room + 0.01) break;
            lw += add;
            last = we;
            e = we < n ? we + 1 : n;
        }
        if (last == i) { y = in + 16; continue; }       /* the first word not beside the icon: below it */
        layout_line_h lh0 = line_new(t);
        uint32_t ln = lh0.index;
        t->line[ln].x = in + fx;
        t->line[ln].y = y;
        t->line[ln].w = room;
        t->line[ln].h = es->lh;
        t->line[ln].baseline = above;
        lines_append(t, b, ln);
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_GENERATED;
        t->frag[fr].node = node_handle(d, el);
        t->frag[fr].text = text_add(t, tx + i, last - i);
        t->frag[fr].x = 0;                             /* from its line's start */
        t->frag[fr].y = above - es->asc;
        t->frag[fr].w = lw;
        t->frag[fr].h = es->asc + es->desc;
        t->frag[fr].baseline = above;
        t->frag[fr].face = (uint8_t)es->face;
        t->frag[fr].orient = LAY_NOCOMB(es->orient);
        if (es->font) t->frag[fr].font = resource_handle(nt, es->font);
        t->frag[fr].size = es->fs;
        t->frag[fr].spacing = es->ls;
        t->frag[fr].word_spacing = es->wsp;
        t->frag[fr].italic = es->italic;
        t->frag[fr].color = es->color;
        t->frag[fr].fill = es->fill;
        t->frag[fr].hidden = es->hidden != 0;
        frags_append(t, ln, fr);
        y += es->lh;
        i = e;
    }
}

/* an empty text field's placeholder (its attribute's text, on one line, white space
   collapsed) in its content box, in the style of its ::placeholder: a line of one
   generated fragment, in the middle of an input, at the top of a textarea */
static void lay_placeholder(LCTX, uint32_t b, uint32_t el, const lay_style *es) {
    int unsourced = lay_img_unsourced(LARGS, el);
    if (unsourced) t->box[b].unsourced = 1;
    if (lay_img_failed(LARGS, el)) {
        /* a broken image's alt text, after its 16px icon, on its line (quirks mode's box of a declared size:
           drawn as one with no alt text) */
        if (lay_img_sized(LARGS, el, es)) { lay_alt_box(LARGS, b, el, es); return; }
        vector<char> alt;
        if (!lay_alt_text(d, el, &alt) || !len(alt)) return;
        double tw = amb_text_width(f, es->face, es->fs, data(alt), len(alt)) + es->ls * (double)lay_cps(data(alt), len(alt));
        double lh = es->lh, base = 0, below = 0;
        lay_leading(lh, es->asc, es->desc, &base, &below);   /* the baseline a line of the text has */
        layout_line_h alh = line_new(t);
        uint32_t aln = alh.index;
        t->line[aln].x = 0;
        t->line[aln].y = 0;
        t->line[aln].w = t->box[b].w;
        t->line[aln].h = lh;
        t->line[aln].baseline = base;
        lines_append(t, b, aln);
        amb_span asp = text_add(t, data(alt), len(alt));
        layout_frag_h afh = frag_new(t);
        uint32_t afr = afh.index;
        t->frag[afr].kind = LAYOUT_FRAG_KIND_GENERATED;
        t->frag[afr].node = node_handle(d, el);
        t->frag[afr].text = asp;
        t->frag[afr].x = 16;
        t->frag[afr].y = base - es->asc;
        t->frag[afr].w = tw;
        t->frag[afr].h = es->asc + es->desc;
        t->frag[afr].baseline = base;
        t->frag[afr].face = (uint8_t)es->face;
        if (es->font) t->frag[afr].font = resource_handle(nt, es->font);
        t->frag[afr].size = es->fs;
        t->frag[afr].spacing = es->ls;
        t->frag[afr].word_spacing = es->wsp;
        t->frag[afr].italic = es->italic;
        t->frag[afr].color = es->color;
        t->frag[afr].fill = es->fill;
        t->frag[afr].hidden = es->hidden != 0;
        frags_append(t, aln, afr);
        return;
    }
    int input = lay_is_named(d, el, "input"), textarea = lay_is_named(d, el, "textarea"), select = lay_is_named(d, el, "select");
    if (lay_is_named(d, el, "progress") || lay_is_named(d, el, "meter")) {
        lay_theme(LARGS, b, el);   /* a bar the theme draws; its children, the fallback, not drawn */
        return;
    }
    if (!input && !textarea && !select) return;
    /* a control's text is clipped to it, as Chromium's inner editor clips its own */
    t->box[b].clip = 1;
    lay_theme(LARGS, b, el);
    /* its baseline, from its content box's top, as Chromium places it: a text's (a line high, in the middle of the
       box); a check box's, radio button's or range's border box's bottom; a colour well's 6px above that; a list
       box's 3px above it; a textarea's its margin box's bottom */
    {
        double ch = t->box[b].h, bottom = ch + t->box[b].pb + t->box[b].bb, above = 0, below = 0;
        int k = input ? lay_input_kind(d, el) : LI_TEXT;
        double widest = 0;
        long rows = 1;
        string unused;
        lay_leading(es->lh, es->asc, es->desc, &above, &below);
        t->box[b].has_baseline = 1;
        if (textarea) t->box[b].baseline = bottom + t->box[b].mb;
        else if (input && (k == LI_CHECK || k == LI_RADIO || k == LI_RANGE)) t->box[b].baseline = bottom;
        else if (input && k == LI_COLOR) t->box[b].baseline = bottom - 6;
        else if (select && lay_select(LARGS, el, es, &widest, &unused, &rows)) t->box[b].baseline = bottom - 3;
        else if (select) t->box[b].baseline = (ch - (es->asc + es->desc)) / 2 + es->asc;
        else t->box[b].baseline = (ch - es->lh) / 2 + above;
    }
    lay_style ps = *es;
    string txt;
    int kind = input ? lay_input_kind(d, el) : LI_TEXT, center = 0;
    double x = 0;
    if (input && (kind == LI_BUTTON || kind == LI_SUBMIT || kind == LI_RESET)) {
        /* a button's label, in the middle of it */
        lay_button_label(d, el, kind, &txt);
        center = 1;
    } else if (select) {
        /* a drop-down's selected option, 4px into it; a list box's options, a row each (a line and 2px), 2px into it */
        double widest = 0;
        long rows = 1;
        if (lay_select(LARGS, el, es, &widest, &txt, &rows)) {
            string label;
            double y = 0;
            for (uint32_t c = d->node[el].first_child; c; c = d->node[c].next_sibling) {
                uint32_t g = lay_is_named(d, c, "optgroup") ? d->node[c].first_child : c, end = lay_is_named(d, c, "optgroup") ? 0 : d->node[c].next_sibling;
                for (uint32_t o = g; o && o != end; o = d->node[o].next_sibling) {
                    if (lay_is_named(d, o, "option")) {
                        lay_option_label(d, o, &label);
                        lay_control_line(LARGS, b, el, es, data(label), len(label), o != c ? 2 + 4 * amb_text_width(f, es->face, es->fs, " ", 1) : 2,
                                         y, 0);
                        y += es->asc + es->desc + 2;
                    }
                    if (o == c) break;
                }
            }
            return;
        }
        x = LAY_X(el).app_none ? 0 : 4;     /* of appearance none, no theme's padding */
    } else if (input && kind == LI_FILE) {
        /* a file chooser's button ("Choose File", "Choose Files" for a multiple one) at its start, a button's border
           and padding about its label (2px and 6px aside, 3px above it), its text the button's colour (buttontext;
           disabled, at 0.3); 4px after it the label, "No file chosen", in the chooser's colour */
        const char *bl = lay_attr(d, el, "multiple") ? "Choose Files" : "Choose File";
        double bw = amb_text_width(f, es->face, es->fs, bl, strlen(bl)) + es->ls * (double)lay_cps(bl, strlen(bl)) + 16;
        t->box[b].part_w = bw;
        int dark = (t->box[b].tstate & 2) != 0, dis = t->box[b].tstate & 1;
        lay_style bs = *es;
        bs.color = dark ? (dis ? 0xffffff4du : 0xffffffffu) : dis ? 0x1010104du : 0x000000ffu;
        bs.fill = bs.color;     /* the button's -webkit-text-fill-color: its own currentcolor */
        lay_control_line(LARGS, b, el, &bs, bl, strlen(bl), 8, 3, 0);
        lay_control_line(LARGS, b, el, es, "No file chosen", 14, bw + 4, 3, 0);
        return;
    } else if (input && kind == LI_DATE) {
        /* a date or time field's parts (lay_date_parts) from its start: a field's text 1px into its 1px padding; on
           the edit's line, its fields' 1px below its top, the edit in the middle of the box's height (the flex row's
           align-items: center) */
        vector<char> texts;
        vector<uint8_t> fields;
        lay_date_parts(LARGS, el, es, &texts, &fields);
        double ps_, pw_, ph_;
        lay_date_picker(d, el, es, &ps_, &pw_, &ph_);
        /* the picker hidden (its visibility) when the field is disabled or read-only, its room kept */
        t->box[b].part = (uint8_t)((t->box[b].tstate & 1) || lay_attr(d, el, "readonly") ? 0 : lay_date_kind(d, el) == LD_TIME ? 2 : 1);
        t->box[b].part_w = ps_;
        double ey = (t->box[b].h - (es->lh + 2)) / 2 + 1, px = 0;
        size_t i = 0;
        for (size_t k = 0; k < len(fields); k++) {
            const char *q = data(texts) + i;
            size_t qn = strlen(q);
            /* each part's text as wide as LayoutNG holds it: its width ceiled to the 1/64 px grid */
            double tw = ceil((amb_text_width(f, es->face, es->fs, q, qn) + es->ls * (double)lay_cps(q, qn)) * 64 - 1e-6) / 64;
            if (qn) lay_control_line(LARGS, b, el, es, q, qn, px + (at(fields, k) ? 1 : 0), ey, 0);
            px += tw + (at(fields, k) ? 2 : 0);
            i += qn + 1;
        }
        return;
    } else if (input && kind != LI_TEXT && kind != LI_PASSWORD) {
        return;
    } else {
        /* a field's value (a password's a bullet a character; a single-line field's without its line breaks, which HTML's
           value sanitization strips), else its placeholder, white space collapsed, in its ::placeholder's style */
        uint32_t v = input ? lay_attr(d, el, "value") : 0;
        if (textarea) {
            for (uint32_t c = d->node[el].first_child; c; c = d->node[c].next_sibling)
                if (d->node[c].kind == DOM_NODE_KIND_TEXT) append(txt, text_ptr(d, d->node[c].data), d->node[c].data.len);
        } else if (v && d->attr[v].value.len) {
            amb_span vs = d->attr[v].value;
            const char *src = text_ptr(d, vs);
            for (size_t i = 0; i < vs.len; i++) {
                if (src[i] == '\n' || src[i] == '\r') continue;
                if (kind == LI_PASSWORD) {
                    if (((uint8_t)src[i] & 0xC0) == 0x80) continue;
                    push_back(txt, (char)0xE2); push_back(txt, (char)0x80); push_back(txt, (char)0xA2);
                } else push_back(txt, src[i]);
            }
        }
        if (!len(txt)) {
            uint32_t a = lay_attr(d, el, "placeholder");
            if (!a || d->attr[a].value.len == 0) return;
            lay_pseudo_style(LARGS, el, CSS_PSEUDO_PLACEHOLDER, &ps);
            amb_span pv = d->attr[a].value;
            const char *src = text_ptr(d, pv);
            int sp = 1;
            for (size_t i = 0; i < pv.len; i++) {
                char c = src[i];
                int white = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
                if (white) { if (!sp) push_back(txt, ' '); sp = 1; continue; }
                push_back(txt, c);
                sp = 0;
            }
            while (len(txt) && back(txt) == ' ') pop_back(txt);
        } else if (textarea) {
            /* a textarea's text: its lines, each broken where it reaches the box's width (the scroll bar's 15px its
               intrinsic width keeps is no room taken: Chromium's scroll bars here overlay) */
            double avail = t->box[b].w, y = 0;
            size_t i = 0, n = len(txt);
            const char *s0 = data(txt);
            while (i <= n) {
                size_t e = i;
                while (e < n && s0[e] != '\n') e++;
                size_t k = i;
                do {
                    /* as much of [k, e) as fits, broken after a space when one is in it, else at a character */
                    size_t fit = e, last_space = 0;
                    for (size_t j = k; j < e; j++) {
                        if (s0[j] == ' ') last_space = j + 1;
                        if (amb_text_width(f, ps.face, ps.fs, s0 + k, j + 1 - k) > avail && j > k) {
                            fit = last_space > k ? last_space : j;
                            break;
                        }
                    }
                    lay_control_line(LARGS, b, el, &ps, s0 + k, fit - k, 0, y, 0);
                    y += ps.lh;
                    k = fit;
                } while (k < e);
                i = e + 1;
            }
            return;
        }
    }
    if (len(txt)) lay_control_line(LARGS, b, el, &ps, data(txt), len(txt), x, -1, center);
}
/* a hexadecimal digit's value, or -1 */
static int lay_hex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
/* what the theme draws of a form control (Chromium's NativeThemeBase), into b: a text field, textarea, list box or
   drop-down whose author styled no background, border or box shadow; a button (a button element or a button
   input) without a styled background or border; a check box, radio button, range or colour well; none when its
   appearance is none. A check box's or radio button's checkedness; a range's value along its track, from its
   min, max and value (0, 100, their middle by default); a colour well's #rrggbb value (black by default) */
/* a control that is disabled (HTML 4.10.18.5): its disabled attribute, or a disabled fieldset's it is in (not in that
   fieldset's first legend) */
static int lay_disabled(const dom_document *d, uint32_t el) {
    if (lay_attr(d, el, "disabled")) return 1;
    uint32_t child = el;
    for (uint32_t p = d->node[el].parent; p && p != 1; child = p, p = d->node[p].parent) {
        if (!lay_is_named(d, p, "fieldset") || !lay_attr(d, p, "disabled")) continue;
        uint32_t legend = 0;
        for (uint32_t k = d->node[p].first_child; k && !legend; k = d->node[k].next_sibling)
            if (d->node[k].kind == DOM_NODE_KIND_ELEMENT && lay_is_named(d, k, "legend")) legend = k;
        if (legend && child == legend) continue;
        return 1;
    }
    return 0;
}

/* an attribute as a number by HTML's rules for parsing floating-point number values (2.3.4.3), as Chromium reads a
   progress's or a meter's (measured on 22 values): ASCII whitespace skipped, a sign, digits, a fraction, an exponent,
   the rest ignored ("0.5x" is 0.5, "5." 5, "0.5e" 0.5); dflt when it is absent or an error (no digits, infinite) */
static double lay_attr_num(const dom_document *d, uint32_t el, const char *name, double dflt) {
    uint32_t a = lay_attr(d, el, name);
    if (!a) return dflt;
    const char *s = text_ptr(d, d->attr[a].value);
    size_t n = d->attr[a].value.len, i = 0, o = 0;
    char buf[96];
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\f' || s[i] == '\r')) i++;
    if (i < n && (s[i] == '-' || s[i] == '+')) { if (s[i] == '-') buf[o++] = '-'; i++; }
    int digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && o < 80) { buf[o++] = s[i++]; digits = 1; }
    if (i + 1 < n && s[i] == '.' && s[i + 1] >= '0' && s[i + 1] <= '9') {
        buf[o++] = s[i++];
        while (i < n && s[i] >= '0' && s[i] <= '9' && o < 88) { buf[o++] = s[i++]; digits = 1; }
    }
    if (!digits) return dflt;
    size_t e = i, eo = o;
    if (e < n && (s[e] == 'e' || s[e] == 'E')) {
        buf[eo++] = 'e';
        e++;
        if (e < n && (s[e] == '-' || s[e] == '+')) buf[eo++] = s[e++];
        if (e < n && s[e] >= '0' && s[e] <= '9') {
            while (e < n && s[e] >= '0' && s[e] <= '9' && eo < 95) buf[eo++] = s[e++];
            o = eo;
        }
    }
    buf[o] = 0;
    double v = strtod(buf, 0);
    if (!isfinite(v)) return dflt;
    return v ? v : 0;
}

static void lay_theme(LCTX, uint32_t b, uint32_t el) {
    lay_ext e = LAY_X(el);
    layout_theme th = LAYOUT_THEME_CSS;
    int styled = e.styled_bg || e.styled_border, css_look = 0;
    if (lay_is_named(d, el, "button")) th = styled ? LAYOUT_THEME_CSS : LAYOUT_THEME_BUTTON;
    else if (lay_is_named(d, el, "textarea")) th = styled || e.styled_shadow ? LAYOUT_THEME_CSS : LAYOUT_THEME_AREA;
    else if (lay_is_named(d, el, "select")) {
        int multiple = lay_attr(d, el, "multiple") != 0;
        long size = lay_attr_int(d, el, "size", multiple ? 4 : 1);
        th = multiple || size > 1 ? (styled || e.styled_shadow ? LAYOUT_THEME_CSS : LAYOUT_THEME_LISTBOX) : LAYOUT_THEME_MENULIST;
        css_look = th == LAYOUT_THEME_MENULIST && (styled || e.styled_shadow);   /* menulist-button: its arrow kept */
    } else if (lay_is_named(d, el, "input")) {
        int k = lay_input_kind(d, el);
        if (k == LI_CHECK) th = LAYOUT_THEME_CHECKBOX;
        else if (k == LI_RADIO) th = LAYOUT_THEME_RADIO;
        else if (k == LI_RANGE) th = LAYOUT_THEME_RANGE;
        else if (k == LI_COLOR) { th = LAYOUT_THEME_COLORWELL; css_look = styled; }     /* its swatch kept */
        else if (k == LI_BUTTON || k == LI_SUBMIT || k == LI_RESET) th = styled ? LAYOUT_THEME_CSS : LAYOUT_THEME_BUTTON;
        else if (k == LI_TEXT || k == LI_PASSWORD) th = styled || e.styled_shadow ? LAYOUT_THEME_CSS : LAYOUT_THEME_FIELD;
        else if (k == LI_DATE) { th = LAYOUT_THEME_DATE; css_look = styled || e.styled_shadow; }   /* its picker kept */
        else if (k == LI_FILE) { th = LAYOUT_THEME_FILE; css_look = 1; }    /* its own box CSS's (none), its button the theme's */
    }
    /* a progress bar or a meter styled (or of appearance none) keeps its kind: its shadow tree's CSS look drawn (4) */
    int gauge = 0;
    if (lay_is_named(d, el, "progress")) { th = LAYOUT_THEME_PROGRESS; gauge = 1; }
    else if (lay_is_named(d, el, "meter")) { th = LAYOUT_THEME_METER; gauge = 1; }
    /* of appearance none: a gauge's bar and a colour well's swatch, their shadow trees', still drawn */
    if (gauge || th == LAYOUT_THEME_COLORWELL || th == LAYOUT_THEME_DATE || th == LAYOUT_THEME_FILE) css_look = css_look || styled || e.app_none;
    else if (e.app_none) th = LAYOUT_THEME_CSS;
    t->box[b].theme = th;
    t->box[b].checked = dom_checkedness(d, el, lay_attr(d, el, "checked") != 0);   /* its checkedness (src/dom/dom.h) */
    t->box[b].tstate = (uint8_t)(th != LAYOUT_THEME_CSS && !gauge && lay_disabled(d, el));
    if (th != LAYOUT_THEME_CSS && css_look) t->box[b].tstate |= 4;
    if (gauge && e.rtl) t->box[b].tstate |= 32;
    /* the scheme it is used in: its color-scheme's colour, the dark Canvas (#121212) */
    if (th != LAYOUT_THEME_CSS && st->cval[e.c0 + CSS_PROP_COLOR_SCHEME].color == 0x121212ffu) t->box[b].tstate |= 2;
    if (th == LAYOUT_THEME_RANGE) {
        double lo = 0, hi = 100, v = -1;
        uint32_t a;
        char buf[64];
        if ((a = lay_attr(d, el, "min")) && d->attr[a].value.len < sizeof buf) { snprintf(buf, sizeof buf, "%.*s", (int)d->attr[a].value.len, text_ptr(d, d->attr[a].value)); lo = atof(buf); }
        if ((a = lay_attr(d, el, "max")) && d->attr[a].value.len < sizeof buf) { snprintf(buf, sizeof buf, "%.*s", (int)d->attr[a].value.len, text_ptr(d, d->attr[a].value)); hi = atof(buf); }
        if (hi < lo) hi = lo;
        if ((a = lay_attr(d, el, "value")) && d->attr[a].value.len && d->attr[a].value.len < sizeof buf) {
            snprintf(buf, sizeof buf, "%.*s", (int)d->attr[a].value.len, text_ptr(d, d->attr[a].value));
            v = atof(buf);
        }
        if (v < 0 && !(lay_attr(d, el, "value") && d->attr[lay_attr(d, el, "value")].value.len)) v = lo + (hi - lo) / 2;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        t->box[b].fraction = hi > lo ? (v - lo) / (hi - lo) : 0;
    }
    if (th == LAYOUT_THEME_MENULIST) {
        lay_style s;
        lay_style_of(LARGS, el, &s);
        t->box[b].swatch = s.color;     /* its arrow's colour: its text's */
    }
    if (th == LAYOUT_THEME_PROGRESS) {
        /* its value of its max (HTML 4.10.13: max 1 unless above 0, the value 0 to max); without a value attribute,
           indeterminate (-1); with one that is no number, 0 (Blink's HTMLProgressElement::position) */
        double mx = lay_attr_num(d, el, "max", 1), v = lay_attr_num(d, el, "value", 0);
        if (!(mx > 0)) mx = 1;
        if (v < 0) v = 0;
        if (v > mx) v = mx;
        t->box[b].fraction = lay_attr(d, el, "value") ? v / mx : -1;
    }
    if (th == LAYOUT_THEME_METER) {
        /* HTML 4.10.14's values, each held to the ones before it, and the region the value is in as Blink's
           HTMLMeterElement::GetGaugeRegion tells it: tstate's 8 the suboptimal one, 16 the even less good */
        double mn = lay_attr_num(d, el, "min", 0), mx = lay_attr_num(d, el, "max", mn > 1 ? mn : 1);
        if (mx < mn) mx = mn;
        double v = lay_attr_num(d, el, "value", 0);
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        double lo = lay_attr_num(d, el, "low", mn);
        if (lo < mn) lo = mn;
        if (lo > mx) lo = mx;
        double hi = lay_attr_num(d, el, "high", mx);
        if (hi < lo) hi = lo;
        if (hi > mx) hi = mx;
        double op = lay_attr_num(d, el, "optimum", (mn + mx) / 2);
        if (op < mn) op = mn;
        if (op > mx) op = mx;
        int region;   /* 0 optimum, 1 suboptimal, 2 even less good */
        if (op < lo) region = v <= lo ? 0 : v <= hi ? 1 : 2;
        else if (hi < op) region = hi <= v ? 0 : lo <= v ? 1 : 2;
        else region = lo <= v && v <= hi ? 0 : 1;
        t->box[b].fraction = mx > mn ? (v - mn) / (mx - mn) : 0;
        t->box[b].tstate |= (uint8_t)(region == 1 ? 8 : region == 2 ? 16 : 0);
    }
    if (th == LAYOUT_THEME_COLORWELL) {
        uint32_t a = lay_attr(d, el, "value");
        uint32_t rgb = 0;
        if (a && d->attr[a].value.len == 7 && text_ptr(d, d->attr[a].value)[0] == '#') {
            const char *v = text_ptr(d, d->attr[a].value);
            int ok = 1;
            for (int i = 1; i < 7; i++) if (lay_hex(v[i]) < 0) ok = 0;
            if (ok) rgb = (uint32_t)(lay_hex(v[1]) * 16 + lay_hex(v[2])) << 16 | (uint32_t)(lay_hex(v[3]) * 16 + lay_hex(v[4])) << 8 |
                          (uint32_t)(lay_hex(v[5]) * 16 + lay_hex(v[6]));
        }
        t->box[b].swatch = rgb << 8 | 0xff;
    }
}
/* one line of a control's text in its content box: at y (-1: in the middle of the box, as a field's or a button's
   inner box is), from x, or in the middle of the box's width when center is set */
static void lay_control_line(LCTX, uint32_t b, uint32_t el, const lay_style *ps, const char *s, size_t n, double x, double y, int center) {
    double tw = amb_text_width(f, ps->face, ps->fs, s, n) + ps->ls * (double)lay_cps(s, n);
    double lh = y < 0 ? t->box[b].h : ps->lh, base = 0, below = 0;
    if (y < 0) { base = (lh - (ps->asc + ps->desc)) / 2 + ps->asc; y = 0; }
    else lay_leading(lh, ps->asc, ps->desc, &base, &below);
    layout_line_h lnh = line_new(t);
    uint32_t ln = lnh.index;
    t->line[ln].x = 0;
    t->line[ln].y = y;
    t->line[ln].w = t->box[b].w;
    t->line[ln].h = lh;
    t->line[ln].baseline = base;
    lines_append(t, b, ln);
    amb_span tsp = text_add(t, s, n);
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    t->frag[fr].kind = LAYOUT_FRAG_KIND_GENERATED;
    t->frag[fr].node = node_handle(d, el);
    t->frag[fr].text = tsp;
    t->frag[fr].x = center ? (t->box[b].w - tw) / 2 : x;
    t->frag[fr].y = base - ps->asc;
    t->frag[fr].w = tw;
    t->frag[fr].h = ps->asc + ps->desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (uint8_t)ps->face;
    t->frag[fr].orient = LAY_NOCOMB(ps->orient);
    if (ps->font) t->frag[fr].font = resource_handle(nt, ps->font);
    t->frag[fr].size = ps->fs;
    t->frag[fr].spacing = ps->ls;
    t->frag[fr].word_spacing = ps->wsp;
    t->frag[fr].italic = ps->italic;
    t->frag[fr].color = ps->color;
    t->frag[fr].fill = ps->fill;
    t->frag[fr].hidden = ps->hidden != 0;
    frags_append(t, ln, fr);
}

/* an atomic inline's box, laid out: a replaced element's content, or an inline-block's
   children as a new formatting context */
static uint32_t lay_atomic(LCTX, uint32_t parent, const lay_piece *p, double cw) {
    uint32_t el = p->node, ob = lay_wm_box_of(el);
    if (ob) {
        /* laid out in its own frame already (lay_wm_atomic): put in the line's box */
        if (!t->box[ob].parent) children_append(t, parent, ob);
        return ob;
    }
    lay_style s;
    lay_style_of(LARGS, el, &s);
    int replaced = lay_replaced(d, el), alx = LAY_X(el).lx;
    uint32_t b = lay_new_box(LARGS, parent, el, replaced ? LAYOUT_BOX_KIND_REPLACED : alx == LX_FLEX ? LAYOUT_BOX_KIND_FLEX :
                                                alx == LX_GRID ? LAYOUT_BOX_KIND_GRID : LAYOUT_BOX_KIND_INLINE_BLOCK);
    lay_edges(t, st, b, &s, cw);
    lay_box_style(LARGS, b, el, cw, -1);
    if (s.m[3].unit == LAY_AUTO) t->box[b].ml = 0;
    if (s.m[1].unit == LAY_AUTO) t->box[b].mr = 0;
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    t->box[b].w = p->aw;
    t->box[b].how = replaced || s.width.unit == LAY_AUTO ? (replaced ? LAYOUT_WIDTH_HOW_INTRINSIC : LAYOUT_WIDTH_HOW_SHRINK_TO_FIT)
                                                         : (s.width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED);
    if (replaced) {
        t->box[b].h = p->ah > 0 ? p->ah : 0;
        lay_placeholder(LARGS, b, el, &s);
        return b;
    }
    lay_mc et, eb;
    int empty;
    /* its height: as declared, a percentage of its line's block's definite height (lay_line_pch), min-height and
       max-height holding it; its children's percentages take it, or in quirks mode an auto one's that basis */
    double pb = lay_line_pch, vedges = t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb;
    double defh = s.height.unit == LAY_PX ? s.height.v : -1;
    lay_len hraw = LAY_X(el).hraw, mnl = LAY_X(el).minh, mxl = LAY_X(el).maxh;
    if (defh < 0 && hraw.unit != LAY_AUTO && !lay_content_sized(hraw) && pb >= 0) {
        defh = lay_res(st, hraw, pb) - (s.border_box ? vedges : 0);
        if (defh < 0) defh = 0;
    }
    double minh = 0, maxh = -1;
    if (mnl.unit != LAY_AUTO && !lay_content_sized(mnl) && (mnl.unit == LAY_PX || pb >= 0)) minh = lay_res(st, mnl, pb) - (s.border_box ? vedges : 0);
    if (mxl.unit != LAY_AUTO && !lay_content_sized(mxl) && (mxl.unit == LAY_PX || pb >= 0)) maxh = lay_res(st, mxl, pb) - (s.border_box ? vedges : 0);
    if (defh >= 0 && maxh >= 0 && defh > maxh) defh = maxh;
    if (defh >= 0 && defh < minh) defh = minh;
    double pch = defh >= 0 ? defh : lay_quirks ? pb : -1;
    int lx = LAY_X(el).lx;
    if (lx == LX_FLEX) lay_flex(LARGS, b, el, defh);
    else if (lx == LX_GRID) lay_grid(LARGS, b, el, defh);
    else lay_flow_children(LARGS, b, el, 1, defh < 0, 0, 0, pch, 0, &et, &eb, &empty);
    if (lay_is_named(d, el, "button")) {
        lay_theme(LARGS, b, el);
        /* a button's content in the middle of a box made taller than it, as Chromium's button centres its anonymous
           box; an empty one's baseline its content box's bottom (Blink's synthesized baseline) */
        double ch = t->box[b].h;
        if (defh > ch) {
            double dy = (defh - ch) / 2;
            for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) t->line[ln].y += dy;
            for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) t->box[c].y += dy;
        }
        if (!t->box[b].first_line && !t->box[b].first_child) {
            t->box[b].has_baseline = 1;
            t->box[b].baseline = defh >= 0 ? defh : 0;
        }
    }
    if (defh >= 0) t->box[b].h = defh;
    else {
        if (maxh >= 0 && t->box[b].h > maxh) t->box[b].h = maxh;
        if (t->box[b].h < minh) t->box[b].h = minh;
        if (LAY_X(el).clamp > 0) lay_clamp(LARGS, b, el, &s);
    }
    return b;
}
/* a child box out of its parent's flow: positioned absolutely, fixed, or floated */
static int lay_out_of_flow(const layout_tree *t, uint32_t c) {
    return t->box[c].position == LAYOUT_POSITION_ABSOLUTE || t->box[c].position == LAYOUT_POSITION_FIXED ||
           t->box[c].float_side != LAYOUT_FLOAT_SIDE_UNFLOATED;
}
/* the first baseline in b's box subtree, from b's content box's top: a flex or grid container's (its
   items give it, Flexbox 1, 8.5; Grid 1, 10.1), its first line's, or its first in-flow child's that has one */
static int lay_first_baseline(const layout_tree *t, uint32_t b, double *y, int depth) {
    if (t->box[b].kind == LAYOUT_BOX_KIND_FLEX || t->box[b].kind == LAYOUT_BOX_KIND_GRID) { *y = t->box[b].baseline; return t->box[b].has_baseline; }
    if (t->box[b].first_line) { *y = t->line[t->box[b].first_line].y + t->line[t->box[b].first_line].baseline; return 1; }
    if (depth > 32) return 0;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        double cy;
        if (lay_out_of_flow(t, c)) continue;
        if (lay_first_baseline(t, c, &cy, depth + 1)) { *y = t->box[c].y + cy; return 1; }
    }
    return 0;
}
/* the last baseline in b's box subtree, from b's content box's top: a flex or grid container's (its
   items give it), its last line's, or its last in-flow child's that has one; ib: an inline-block's
   baseline, which takes a flex or grid container's first baseline unless its last one is its items'
   aligned on their last baselines (Chromium, measured) */
static int lay_last_baseline_of(const layout_tree *t, uint32_t b, double *y, int depth, int ib) {
    if (t->box[b].kind == LAYOUT_BOX_KIND_FLEX || t->box[b].kind == LAYOUT_BOX_KIND_GRID) {
        if (ib && t->box[b].has_lbaseline != 2) { *y = t->box[b].baseline; return t->box[b].has_baseline; }
        *y = t->box[b].lbaseline;
        return t->box[b].has_lbaseline != 0;
    }
    uint32_t ln = t->box[b].last_line;
    if (ln) { *y = t->line[ln].y + t->line[ln].baseline; return 1; }
    if (depth > 32) return 0;
    int found = 0;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        double cy;
        if (lay_out_of_flow(t, c)) continue;
        if (lay_last_baseline_of(t, c, &cy, depth + 1, ib)) { *y = t->box[c].y + cy; found = 1; }
    }
    return found;
}
static int lay_last_baseline(const layout_tree *t, uint32_t b, double *y, int depth) { return lay_last_baseline_of(t, b, y, depth, 0); }

/* an atomic box's baseline, from its margin box's top (CSS 2.1, 10.8.1): a flex or grid
   container's first one (its items'); a replaced element's placeholder's line's; an
   inline-block's last line's in its flow, however deep (a flex or grid container in it its
   first baseline) -- its bottom margin edge when it has none, or when its overflow is not
   visible */
static double lay_atomic_baseline(const layout_tree *t, uint32_t b) {
    double top = t->box[b].mt + t->box[b].bt + t->box[b].pt, y;
    int kind = t->box[b].kind;
    uint32_t pb = t->box[b].parent;
    if (pb && lay_frame(t->box[b].wm) != lay_frame(t->box[pb].wm)) {
        /* of another writing mode's frame than its line's: synthesized from its margin box, its bottom (an alphabetic
           baseline's), or its middle in a vertical line, whose baseline is the central one (measured) */
        double mh = t->box[b].mt + lay_border_height(t, b) + t->box[b].mb;
        int pw = t->box[pb].wm;
        return pw == CSS_WM_VRL || pw == CSS_WM_VLR ? mh / 2 : mh;
    }
    if (kind == LAYOUT_BOX_KIND_FLEX || kind == LAYOUT_BOX_KIND_GRID) { if (lay_first_baseline(t, b, &y, 0)) return top + y; }
    else if (kind == LAYOUT_BOX_KIND_REPLACED) {
        if (t->box[b].has_baseline) return top + t->box[b].baseline;     /* a form control's (lay_placeholder) */
        uint32_t ln = t->box[b].last_line;
        if (ln) return top + t->line[ln].y + t->line[ln].baseline;
    } else if (kind == LAYOUT_BOX_KIND_INLINE_BLOCK && t->box[b].has_baseline) return top + t->box[b].baseline;  /* an empty button's */
    else if (t->box[b].clip != 1 && lay_last_baseline_of(t, b, &y, 0, 1)) return top + y;
    /* none: synthesized from its margin box, its bottom; in a vertical line (the central baseline) its middle */
    double mh = t->box[b].mt + lay_border_height(t, b) + t->box[b].mb;
    return pb && (t->box[pb].wm == CSS_WM_VRL || t->box[pb].wm == CSS_WM_VLR) ? mh / 2 : mh;
}

/* a word's (or kept spaces') bytes tx[off, off + ln) of its text tx[0, n), as laid out
   (text-transform tt for a word), into out */
static void lay_piece_bytes(const char *tx, size_t n, int word, int tt, uint32_t off, uint32_t ln, vector<char> *out) {
    if (off >= n) return;
    if (off + ln > n) ln = (uint32_t)(n - off);
    if (word && tt) { lay_transform(tx + off, ln, tt, out); return; }
    for (uint32_t i = off; i < off + ln; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);     /* a tab: a space as laid out */
}
/* the text of pieces [a, b) of one text node (or a pseudo-element's content), white space
   as laid out, into out */
static void lay_piece_text(const layout_tree *t, const dom_document *d, const vector<lay_gen> *gx, const vector<lay_piece> *ps, size_t a, size_t b,
                           vector<char> *out) {
    for (size_t k = a; k < b; k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_SPACE && !p.keep) { push_back(out, ' '); continue; }
        if (p.kind != P_WORD && p.kind != P_SPACE) continue;
        size_t from = len(*out);
        if (p.node & LAY_GEN) {
            amb_span g = at(*gx, p.node & LAY_GIDX).text;
            lay_piece_bytes(text_ptr(t, g), g.len, p.kind == P_WORD, p.tt, p.off, p.len, out);
        } else {
            amb_span sp = d->node[p.node].data;
            const char *tx = text_ptr(d, sp);
            if (p.kind == P_WORD && p.tt && p.off + p.len <= sp.len) lay_transform(tx + p.off, p.len, p.tt, out);
            else for (uint32_t i = p.off; i < p.off + p.len && i < sp.len; i++) push_back(out, tx[i] == '\t' ? ' ' : tx[i]);
        }
        if (p.kind != P_WORD || !(p.hy || p.soft)) continue;
        if (p.hy) {
            /* the word's own bytes: soft hyphens out, the hyphen it is broken by */
            vector<char> wd;
            for (size_t q = from; q < len(*out); q++) push_back(wd, at(*out, q));
            resize(out, from);
            lay_strip_soft(&wd);
            push_back(wd, '-');
            append_range(out, data(wd), len(wd));
        } else {
            /* soft hyphens where the word is not broken: none drawn */
            vector<char> wd;
            for (size_t q = from; q < len(*out); q++) push_back(wd, at(*out, q));
            resize(out, from);
            lay_strip_soft(&wd);
            append_range(out, data(wd), len(wd));
        }
    }
}

/* an inline element open at a line's start or end: it, its baseline's shift and its
   relative offset, as its pieces have them */
typedef struct lay_open { uint32_t el; float shift, rx, ry; } lay_open;

/* ---- bidirectional text (UAX #9): a paragraph's levels ---- */

/* a unit of a bidi paragraph: a code point of a word's or a kept space's text (its node, its
   byte offset there and its length); a piece that is no text (a collapsed space WS; an atomic
   box, a float or a positioned box ON, as U+FFFC; a forced break B); an inline element's
   edge (a marker, BN), or a control its unicode-bidi adds (CSS Writing Modes 3, 2.4.2). Its
   class as written (orig) and as the rules resolve it (cls), its embedding level (elvl) and
   its resolved level (lvl) */
typedef struct lay_bu { uint32_t cp, node, off; uint8_t len, orig, cls, lvl, elvl; } lay_bu;

/* P2, P3: the first strong class from unit i on (R or AL 1, L 0, none -1), past isolates'
   insides; within an isolate (iso), only to its matching PDI */
static int lay_bidi_first_strong(const vector<lay_bu> *u, size_t i, int iso) {
    int depth = 0;
    for (; i < len(*u); i++) {
        int c = at(*u, i).orig;
        if (c == BC_B) return -1;
        if (c == BC_LRI || c == BC_RLI || c == BC_FSI) { depth++; continue; }
        if (c == BC_PDI) {
            if (depth) depth--;
            else if (iso) return -1;
            continue;
        }
        if (depth) continue;
        if (c == BC_L) return 0;
        if (c == BC_R || c == BC_AL) return 1;
    }
    return -1;
}

/* X1 to X8: each unit's embedding level, and its class where an override sets it; a stack of
   embeddings, overrides and isolates 125 deep, past which they overflow */
static void lay_bidi_explicit(vector<lay_bu> *u, int para) {
    uint8_t sl[128], so[128], si[128];          /* each entry's level, override (1 L, 2 R), whether an isolate's */
    int sp = 0, oi = 0, oe = 0, vi = 0;
    sl[0] = (uint8_t)para;
    so[0] = 0;
    si[0] = 0;
    for (size_t i = 0; i < len(*u); i++) {
        lay_bu b = at(*u, i);
        int c = b.orig, top = sl[sp];
        b.cls = (uint8_t)c;
        b.lvl = (uint8_t)top;
        if (c == BC_RLE || c == BC_LRE || c == BC_RLO || c == BC_LRO) {
            int nl = c == BC_RLE || c == BC_RLO ? (top + 1) | 1 : (top + 2) & ~1;
            if (nl <= 125 && !oi && !oe && sp < 126) {
                sp++;
                sl[sp] = (uint8_t)nl;
                so[sp] = (uint8_t)(c == BC_RLO ? 2 : c == BC_LRO ? 1 : 0);
                si[sp] = 0;
            } else if (!oi) oe++;
        } else if (c == BC_RLI || c == BC_LRI || c == BC_FSI) {
            if (so[sp]) b.cls = (uint8_t)(so[sp] == 1 ? BC_L : BC_R);
            int rtl = c == BC_RLI || (c == BC_FSI && lay_bidi_first_strong(u, i + 1, 1) == 1);
            int nl = rtl ? (top + 1) | 1 : (top + 2) & ~1;
            if (nl <= 125 && !oi && !oe && sp < 126) {
                vi++;
                sp++;
                sl[sp] = (uint8_t)nl;
                so[sp] = 0;
                si[sp] = 1;
            } else oi++;
        } else if (c == BC_PDI) {
            if (oi) oi--;
            else if (vi) {
                oe = 0;
                while (sp > 0 && !si[sp]) sp--;
                if (sp > 0) sp--;
                vi--;
            }
            b.lvl = sl[sp];
            if (so[sp]) b.cls = (uint8_t)(so[sp] == 1 ? BC_L : BC_R);
        } else if (c == BC_PDF) {
            if (!oi) {
                if (oe) oe--;
                else if (!si[sp] && sp > 0) sp--;
            }
        } else if (c == BC_B) {
            b.lvl = (uint8_t)para;
            sp = 0;
            oi = oe = vi = 0;
        } else if (c != BC_BN && so[sp]) b.cls = (uint8_t)(so[sp] == 1 ? BC_L : BC_R);
        b.elvl = b.lvl;
        (*u)[i] = b;
    }
}

/* a class's strong direction for the neutrals (L, or R, which EN and AN count as); -1 none */
static int lay_bidi_strong(int c) {
    if (c == BC_L) return BC_L;
    if (c == BC_R || c == BC_AL || c == BC_EN || c == BC_AN) return BC_R;
    return -1;
}
static int lay_bidi_ni(int c) { return c == BC_B || c == BC_S || c == BC_WS || c == BC_ON || (c >= BC_LRI && c <= BC_PDI); }

/* a bracket's pair (BidiBrackets.txt): 1 and the closing one for an opening bracket, 2 and
   the opening one for a closing one, 0 for none; U+2329 and U+232A as their canonical
   equivalents U+3008 and U+3009 */
static int lay_bidi_bracket(uint32_t cp, uint32_t *pair) {
    if (cp == 0x2329) cp = 0x3008;
    else if (cp == 0x232A) cp = 0x3009;
    size_t n = sizeof amb_bidi_brackets / sizeof amb_bidi_brackets[0];
    for (size_t k = 0; k + 1 < n; k += 2) {
        if (amb_bidi_brackets[k] == cp) { *pair = amb_bidi_brackets[k + 1]; return 1; }
        if (amb_bidi_brackets[k + 1] == cp) { *pair = amb_bidi_brackets[k]; return 2; }
    }
    return 0;
}

/* an isolating run sequence's classes resolved (W1 to W7, N0, N1, N2) and its levels (I1,
   I2): seq its places in idx, the units the rules see; para the paragraph's level */
static void lay_bidi_sequence(vector<lay_bu> *u, const vector<uint32_t> *idx, const vector<uint32_t> *seq, int para) {
    size_t m = len(*seq);
    uint32_t q0 = at(*seq, 0), q1 = at(*seq, m - 1);
    int lv = at(*u, at(*idx, q0)).elvl;
    int before = q0 > 0 ? at(*u, at(*idx, q0 - 1)).elvl : para;
    int lc = at(*u, at(*idx, q1)).orig;
    int after = lc == BC_LRI || lc == BC_RLI || lc == BC_FSI || q1 + 1 >= len(*idx) ? para : at(*u, at(*idx, q1 + 1)).elvl;
    int sos = ((lv > before ? lv : before) & 1) ? BC_R : BC_L, eos = ((lv > after ? lv : after) & 1) ? BC_R : BC_L;
    int dir = (lv & 1) ? BC_R : BC_L;
    vector<uint8_t> tc;
    for (size_t k = 0; k < m; k++) push_back(tc, at(*u, at(*idx, at(*seq, k))).cls);
    /* W1: a non-spacing mark the class before it (ON after an isolate's initiator or end) */
    for (size_t k = 0; k < m; k++) {
        if (at(tc, k) != BC_NSM) continue;
        int pc = k ? at(tc, k - 1) : sos;
        tc[k] = (uint8_t)(k && pc >= BC_LRI && pc <= BC_PDI ? BC_ON : pc);
    }
    /* W2: a European number after Arabic letters an Arabic number; W3: AL is R */
    int strong = sos;
    for (size_t k = 0; k < m; k++) {
        int c = at(tc, k);
        if (c == BC_L || c == BC_R || c == BC_AL) strong = c;
        else if (c == BC_EN && strong == BC_AL) tc[k] = BC_AN;
    }
    for (size_t k = 0; k < m; k++) if (at(tc, k) == BC_AL) tc[k] = BC_R;
    /* W4: one separator between two numbers of a kind is one of them */
    for (size_t k = 1; k + 1 < m; k++) {
        int c = at(tc, k), pc = at(tc, k - 1), nc = at(tc, k + 1);
        if (c == BC_ES && pc == BC_EN && nc == BC_EN) tc[k] = BC_EN;
        else if (c == BC_CS && (pc == BC_EN || pc == BC_AN) && nc == pc) tc[k] = (uint8_t)pc;
    }
    /* W5: terminators beside a European number are European numbers */
    for (size_t k = 0; k < m;) {
        if (at(tc, k) != BC_ET) { k++; continue; }
        size_t e = k;
        while (e < m && at(tc, e) == BC_ET) e++;
        if ((k > 0 && at(tc, k - 1) == BC_EN) || (e < m && at(tc, e) == BC_EN)) for (size_t z = k; z < e; z++) tc[z] = BC_EN;
        k = e;
    }
    /* W6: the separators and terminators left are neutral; W7: a European number after L is L */
    for (size_t k = 0; k < m; k++) if (at(tc, k) == BC_ES || at(tc, k) == BC_ET || at(tc, k) == BC_CS) tc[k] = BC_ON;
    strong = sos;
    for (size_t k = 0; k < m; k++) {
        int c = at(tc, k);
        if (c == BC_L || c == BC_R) strong = c;
        else if (c == BC_EN && strong == BC_L) tc[k] = BC_L;
    }
    /* N0: the bracket pairs (BD16, 63 open at most), in order of their openings: the
       direction of the strong text in them when that is the embedding's; else, when their
       strong text is the other way, the context's before them; and the marks after them */
    vector<uint32_t> pairs, stk;
    for (size_t k = 0; k < m; k++) {
        if (at(tc, k) != BC_ON) continue;
        uint32_t cp = at(*u, at(*idx, at(*seq, k))).cp, pair;
        int kind = lay_bidi_bracket(cp, &pair);
        if (kind == 1) {
            if (len(stk) >= 126) break;
            push_back(stk, pair);
            push_back(stk, (uint32_t)k);
        } else if (kind == 2) {
            if (cp == 0x232A) cp = 0x3009;
            for (size_t s = len(stk); s >= 2; s -= 2) {
                if (at(stk, s - 2) != cp) continue;
                push_back(pairs, at(stk, s - 1));
                push_back(pairs, (uint32_t)k);
                resize(&stk, s - 2);
                break;
            }
        }
    }
    for (size_t p = 2; p < len(pairs); p += 2)
        for (size_t z = p; z >= 2 && at(pairs, z - 2) > at(pairs, z); z -= 2) {
            uint32_t o = at(pairs, z), c = at(pairs, z + 1);
            pairs[z] = at(pairs, z - 2);
            pairs[z + 1] = at(pairs, z - 1);
            pairs[z - 2] = o;
            pairs[z - 1] = c;
        }
    for (size_t p = 0; p + 1 < len(pairs); p += 2) {
        uint32_t o = at(pairs, p), c = at(pairs, p + 1);
        int found = 0;
        for (uint32_t k = o + 1; k < c && found != 1; k++) {
            int sd = lay_bidi_strong(at(tc, k));
            if (sd == dir) found = 1;
            else if (sd >= 0) found = 2;
        }
        if (!found) continue;
        int to = dir;
        if (found == 2) {
            to = sos;
            for (uint32_t k = o; k > 0; k--) {
                int sd = lay_bidi_strong(at(tc, k - 1));
                if (sd >= 0) { to = sd; break; }
            }
        }
        tc[o] = (uint8_t)to;
        tc[c] = (uint8_t)to;
        for (uint32_t k = o + 1; k < m && at(*u, at(*idx, at(*seq, k))).orig == BC_NSM; k++) tc[k] = (uint8_t)to;
        for (uint32_t k = c + 1; k < m && at(*u, at(*idx, at(*seq, k))).orig == BC_NSM; k++) tc[k] = (uint8_t)to;
    }
    /* N1, N2: neutrals between text of one direction take it, the others the embedding's */
    for (size_t k = 0; k < m;) {
        if (!lay_bidi_ni(at(tc, k))) { k++; continue; }
        size_t e = k;
        while (e < m && lay_bidi_ni(at(tc, e))) e++;
        int b = k ? lay_bidi_strong(at(tc, k - 1)) : sos, a = e < m ? lay_bidi_strong(at(tc, e)) : eos;
        int to = b == a && b >= 0 ? b : dir;
        for (size_t z = k; z < e; z++) tc[z] = (uint8_t)to;
        k = e;
    }
    /* I1, I2 */
    for (size_t k = 0; k < m; k++) {
        int c = at(tc, k), l = lv;
        if (!(lv & 1)) { if (c == BC_R) l++; else if (c == BC_AN || c == BC_EN) l += 2; }
        else if (c == BC_L || c == BC_EN || c == BC_AN) l++;
        uint32_t i = at(*idx, at(*seq, k));
        lay_bu b = at(*u, i);
        b.cls = (uint8_t)c;
        b.lvl = (uint8_t)l;
        (*u)[i] = b;
    }
}

/* a paragraph's levels (UAX #9): explicit (X1 to X8); the units X9 removes passed over (the
   embeddings, overrides and their ends, an edge's marker); the isolating run sequences (BD9,
   BD13, X10), each resolved; the units passed over the level of the unit before them */
static void lay_bidi_resolve(vector<lay_bu> *u, int para) {
    size_t n = len(*u);
    lay_bidi_explicit(u, para);
    vector<uint32_t> idx;
    for (size_t i = 0; i < n; i++) {
        int c = at(*u, i).orig;
        if (c == BC_RLE || c == BC_LRE || c == BC_RLO || c == BC_LRO || c == BC_PDF || c == BC_BN) continue;
        push_back(idx, (uint32_t)i);
    }
    size_t m = len(idx);
    /* BD9: each isolate's initiator and its matching PDI, mates (places in idx; -1 none) */
    vector<int32_t> mate;
    resize(&mate, m, -1);
    vector<uint32_t> stk;
    for (size_t q = 0; q < m; q++) {
        int c = at(*u, at(idx, q)).orig;
        if (c == BC_LRI || c == BC_RLI || c == BC_FSI) push_back(stk, (uint32_t)q);
        else if (c == BC_PDI && len(stk)) {
            uint32_t o = back(stk);
            pop_back(&stk);
            mate[o] = (int32_t)q;
            mate[q] = (int32_t)o;
        } else if (c == BC_B) clear(&stk);
    }
    /* the level runs (by embedding level), each its first place and its end in idx */
    vector<uint32_t> rs, re, runof;
    resize(&runof, m, 0u);
    for (size_t q = 0; q < m;) {
        size_t e = q + 1;
        int lv = at(*u, at(idx, q)).elvl;
        while (e < m && at(*u, at(idx, e)).elvl == lv) e++;
        for (size_t z = q; z < e; z++) runof[z] = (uint32_t)len(rs);
        push_back(rs, (uint32_t)q);
        push_back(re, (uint32_t)e);
        q = e;
    }
    /* each sequence: a run that a matched PDI does not begin, and those an initiator it ends with goes on in */
    vector<uint32_t> seq;
    for (size_t r = 0; r < len(rs); r++) {
        uint32_t first = at(rs, r);
        if (at(*u, at(idx, first)).orig == BC_PDI && at(mate, first) >= 0) continue;
        clear(&seq);
        size_t rr = r;
        for (int guard = 0; guard < 4096; guard++) {
            for (uint32_t q = at(rs, rr); q < at(re, rr); q++) push_back(seq, q);
            uint32_t last = at(re, rr) - 1;
            int c = at(*u, at(idx, last)).orig;
            if ((c != BC_LRI && c != BC_RLI && c != BC_FSI) || at(mate, last) < 0) break;
            rr = at(runof, (size_t)at(mate, last));
        }
        lay_bidi_sequence(u, &idx, &seq, para);
    }
    int prev = para;
    for (size_t i = 0; i < n; i++) {
        int c = at(*u, i).orig;
        if (c == BC_RLE || c == BC_LRE || c == BC_RLO || c == BC_LRO || c == BC_PDF || c == BC_BN) (*u)[i].lvl = (uint8_t)prev;
        else prev = at(*u, i).lvl;
    }
}

/* a code point drawn at an odd level: its mirror (L4, BidiMirroring.txt), or itself */
static uint32_t lay_bidi_mirror(uint32_t cp) {
    size_t lo = 0, hi = sizeof amb_bidi_mirror / sizeof amb_bidi_mirror[0] / 2;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (amb_bidi_mirror[2 * mid] < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo < sizeof amb_bidi_mirror / sizeof amb_bidi_mirror[0] / 2 && amb_bidi_mirror[2 * lo] == cp) return amb_bidi_mirror[2 * lo + 1];
    return cp;
}

/* a code point's UTF-8 into out */
static void lay_put_utf8(vector<char> *out, uint32_t cp) {
    if (cp < 0x80) push_back(out, (char)cp);
    else if (cp < 0x800) { push_back(out, (char)(0xC0 | cp >> 6)); push_back(out, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        push_back(out, (char)(0xE0 | cp >> 12));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
    } else {
        push_back(out, (char)(0xF0 | cp >> 18));
        push_back(out, (char)(0x80 | (cp >> 12 & 0x3F)));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
    }
}

/* text as a right-to-left run draws it: its code points in the other order, each its mirror (L4) */
static void lay_bidi_reverse(const vector<char> *in, vector<char> *out) {
    size_t q = len(*in);
    while (q > 0) {
        size_t b0 = q - 1;
        while (b0 > 0 && ((unsigned char)at(*in, b0) & 0xC0) == 0x80) b0--;
        size_t z = b0;
        uint32_t cp = amb_utf8_next(data(*in), len(*in), &z);
        uint32_t mc = lay_bidi_mirror(cp);
        if (mc != cp) lay_put_utf8(out, mc);
        else for (size_t k = b0; k < q; k++) push_back(out, at(*in, k));
        q = b0;
    }
}

/* the bytes [off, off + n) of a node's text (a pseudo-element's content's, in the tree's pool) into out */
static void lay_src_bytes(LCTX, uint32_t node, uint32_t off, uint32_t n, vector<char> *out) {
    if (node & LAY_GEN) {
        amb_span g = at(*gx, node & LAY_GIDX).text;
        if (off + n <= g.len) append_range(out, text_ptr(t, g) + off, n);
        return;
    }
    amb_span sp = d->node[node].data;
    if (off + n <= sp.len) append_range(out, text_ptr(d, sp) + off, n);
}

/* an element whose direction its content gives: HTML's dir=auto, a bdi without dir */
static int lay_dir_auto(const dom_document *d, uint32_t x) {
    if (x & LAY_GEN) return 0;
    uint32_t a = lay_attr(d, x, "dir");
    if (a) {
        amb_span v = d->attr[a].value;
        const char *p = text_ptr(d, v);
        return v.len == 4 && (p[0] | 32) == 'a' && (p[1] | 32) == 'u' && (p[2] | 32) == 't' && (p[3] | 32) == 'o';
    }
    return lay_is_named(d, x, "bdi");
}

/* the unicode-bidi values (lay_ext's ubidi) */
enum { UB_NORMAL, UB_EMBED, UB_ISOLATE, UB_OVERRIDE, UB_ISOLATE_OVERRIDE, UB_PLAINTEXT };

/* the bidi paragraph of pieces ps into u: each word's and kept space's code points, each other
   piece's unit (its first unit's index in its bu), an inline element's edges and the controls
   its unicode-bidi and direction add (CSS Writing Modes 3, 2.4.2: embed LRE or RLE, isolate
   LRI or RLI -- FSI for an element whose direction its content gives -- bidi-override LRO
   or RLO, isolate-override both, plaintext FSI; each closed by PDF or PDI) */
static void lay_bidi_units(LCTX, vector<lay_piece> *ps, vector<lay_bu> *u) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        lay_bu b;
        memset(&b, 0, sizeof b);
        b.node = p.node;
        b.off = p.off;
        if (p.kind == P_WORD || (p.kind == P_SPACE && p.keep)) {
            p.bu = (uint32_t)len(*u);
            vector<char> raw;
            lay_src_bytes(LARGS, p.node, p.off, p.len, &raw);
            size_t z = 0;
            while (z < len(raw)) {
                size_t z0 = z;
                uint32_t cp = amb_utf8_next(data(raw), len(raw), &z);
                b.cp = cp;
                b.off = p.off + (uint32_t)z0;
                b.len = (uint8_t)(z - z0);
                b.orig = (uint8_t)lay_bidi_class(cp);
                push_back(u, b);
            }
        } else if (p.kind == P_OPEN || p.kind == P_CLOSE) {
            lay_ext e = LAY_X(p.node);
            int ub = e.ubidi, rtl = e.rtl;
            int iso = ub == UB_ISOLATE || ub == UB_ISOLATE_OVERRIDE || ub == UB_PLAINTEXT;
            int fsi = ub == UB_PLAINTEXT || (iso && lay_dir_auto(d, p.node));
            uint8_t ctl[2];
            int nc = 0;
            if (p.kind == P_OPEN) {
                if (iso) ctl[nc++] = (uint8_t)(fsi ? BC_FSI : rtl ? BC_RLI : BC_LRI);
                if (ub == UB_EMBED) ctl[nc++] = (uint8_t)(rtl ? BC_RLE : BC_LRE);
                if (ub == UB_OVERRIDE || ub == UB_ISOLATE_OVERRIDE) ctl[nc++] = (uint8_t)(rtl ? BC_RLO : BC_LRO);
            } else {
                if (ub == UB_EMBED || ub == UB_OVERRIDE || ub == UB_ISOLATE_OVERRIDE) ctl[nc++] = BC_PDF;
                if (iso) ctl[nc++] = BC_PDI;
            }
            b.orig = BC_BN;
            if (p.kind == P_OPEN) { p.bu = (uint32_t)len(*u); push_back(u, b); }
            for (int q = 0; q < nc; q++) {
                lay_bu cb = b;
                cb.orig = ctl[q];
                push_back(u, cb);
            }
            if (p.kind == P_CLOSE) { p.bu = (uint32_t)len(*u); push_back(u, b); }
        } else {
            p.bu = (uint32_t)len(*u);
            b.cp = p.kind == P_SPACE ? 0x20 : p.kind == P_BREAK ? 0x2029 : 0xFFFC;
            b.orig = (uint8_t)(p.kind == P_SPACE ? BC_WS : p.kind == P_BREAK ? BC_B : BC_ON);
            push_back(u, b);
        }
        (*ps)[k] = p;
    }
}

/* each text piece's direction for shaping (its sdir), from its paragraph's resolved levels u:
   Blink shapes no run across a change of direction (a level's parity) */
static void lay_shape_dirs(vector<lay_piece> *ps, const vector<lay_bu> *u) {
    size_t nb = len(*u);
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind != P_WORD && p.kind != P_SPACE) continue;
        int got = 0;
        size_t q = p.bu;
        if (p.kind == P_WORD || p.keep) {
            while (q < nb && at(*u, q).node == p.node && at(*u, q).off < p.off) q++;
            for (; q < nb && at(*u, q).node == p.node && at(*u, q).off < p.off + p.len; q++)
                if (at(*u, q).orig != BC_BN) got |= (at(*u, q).lvl & 1) ? 2 : 1;
        } else if (q < nb) got = (at(*u, q).lvl & 1) ? 2 : 1;
        p.sdir = (uint8_t)got;
        (*ps)[k] = p;
    }
}

/* ---- bidirectional text: a line's visual runs ---- */

/* a visual run of a bidi line: its piece k, its bytes [o0, o1) of the piece's node text (a
   word's or a kept space's; 0 and 0 for the others), its level, width and place on the line */
typedef struct lay_vr { uint32_t k, o0, o1; int lvl; double w, x; } lay_vr;

/* the bytes [o0, o1) of a word's (or kept spaces') node text as laid out: text-transform's
   letters, soft hyphens and bidi controls out, the hyphen after a broken word's last part;
   into out */
static void lay_run_bytes(LCTX, const lay_piece *p, uint32_t o0, uint32_t o1, vector<char> *out) {
    vector<char> raw, tw;
    lay_src_bytes(LARGS, p->node, o0, o1 - o0, &raw);
    if (p->kind == P_WORD && p->tt) lay_transform(data(raw), len(raw), p->tt, &tw);
    else for (size_t i = 0; i < len(raw); i++) push_back(tw, at(raw, i) == '\t' ? ' ' : at(raw, i));
    if (p->kind == P_WORD && p->soft) lay_strip_soft(&tw);
    lay_strip_controls(&tw);
    if (p->kind == P_WORD && p->hy && o1 == p->off + p->len) push_back(tw, '-');
    append_range(out, data(tw), len(tw));
}

/* a bidi line's visual runs (pieces [a, end) of ps) into vr: each piece's, a word split where
   its level changes (its parts measured again), at its level -- its units', an inline
   element's edge at its embedding level, the line's trailing spaces and breaks and a kept
   tab at the paragraph's (L1) -- and in ord their visual order (L2): each run of a level and
   above reversed, from the highest level down to the lowest odd one. An element's two edges
   on the line, their order reversed, swap their widths: the left one draws the left edges */
static void lay_bidi_line(LCTX, const vector<lay_piece> *ps, size_t a, size_t end, const vector<lay_bu> *bu, int para, vector<lay_vr> *vr,
                          vector<uint32_t> *ord) {
    size_t nb = len(*bu);
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        lay_vr r;
        r.k = (uint32_t)k;
        r.o0 = 0;
        r.o1 = 0;
        r.w = p.w;
        r.x = 0;
        r.lvl = para;
        if (p.kind == P_WORD || (p.kind == P_SPACE && p.keep)) {
            /* its units, from its first on (a hyphenated word's rest goes on from its own) */
            size_t q = p.bu;
            while (q < nb && at(*bu, q).node == p.node && at(*bu, q).off < p.off) q++;
            size_t first = len(*vr);
            int lv = -1, tab = 0;
            r.o0 = p.off;
            while (q < nb && at(*bu, q).node == p.node && at(*bu, q).off < p.off + p.len) {
                lay_bu b = at(*bu, q);
                if (b.orig == BC_S) tab = 1;
                if (lv >= 0 && b.lvl != lv && p.kind == P_WORD) {
                    r.o1 = b.off;
                    push_back(vr, r);
                    r.o0 = b.off;
                }
                if (lv < 0 || p.kind == P_WORD) lv = b.lvl;
                r.lvl = lv;
                q++;
            }
            r.o1 = p.off + p.len;
            if (lv < 0 || tab) r.lvl = tab ? para : r.lvl;
            push_back(vr, r);
            if (len(*vr) - first > 1)
                for (size_t z = first; z < len(*vr); z++) {
                    lay_vr s = at(*vr, z);
                    vector<char> tx;
                    lay_run_bytes(LARGS, &p, s.o0, s.o1, &tx);
                    s.w = amb_text_width(f, p.face, p.size, data(tx), len(tx)) + p.ls * (double)lay_cps(data(tx), len(tx));
                    (*vr)[z] = s;
                }
            continue;
        }
        if (p.bu < nb) r.lvl = at(*bu, p.bu).lvl;
        if (p.bu < nb && (p.kind == P_OPEN || p.kind == P_CLOSE)) {
            /* an edge: the level of the unit next to it the rules keep, inside it (an
               isolate's control, outside its isolate), else its embedding level */
            r.lvl = at(*bu, p.bu).elvl;
            size_t q = p.bu;
            for (;;) {
                if (p.kind == P_OPEN) { if (q + 1 >= nb) break; q++; }
                else { if (q == 0) break; q--; }
                int c = at(*bu, q).orig;
                if (c == BC_BN || c == BC_RLE || c == BC_LRE || c == BC_RLO || c == BC_LRO || c == BC_PDF) continue;
                r.lvl = at(*bu, q).lvl;
                break;
            }
        }
        if (p.kind == P_BREAK) r.lvl = para;
        push_back(vr, r);
    }
    /* L1: the trailing white space, at the paragraph's level */
    for (size_t q = len(*vr); q > 0; q--) {
        int kd = at(*ps, at(*vr, q - 1).k).kind;
        if (kd != P_SPACE && kd != P_BREAK && kd != P_ABS && kd != P_FLOAT) break;
        (*vr)[q - 1].lvl = para;
    }
    /* L2 */
    size_t nr = len(*vr);
    int maxl = 0, minodd = 1000;
    for (size_t q = 0; q < nr; q++) {
        push_back(ord, (uint32_t)q);
        int l = at(*vr, q).lvl;
        if (l > maxl) maxl = l;
        if ((l & 1) && l < minodd) minodd = l;
    }
    for (int l = maxl; l >= minodd; l--)
        for (size_t q = 0; q < nr;) {
            if (at(*vr, at(*ord, q)).lvl < l) { q++; continue; }
            size_t e2 = q;
            while (e2 < nr && at(*vr, at(*ord, e2)).lvl >= l) e2++;
            for (size_t i0 = q, i1 = e2 - 1; i0 < i1; i0++, i1--) {
                uint32_t tmp = at(*ord, i0);
                (*ord)[i0] = at(*ord, i1);
                (*ord)[i1] = tmp;
            }
            q = e2;
        }
    /* an element's edges reversed: their widths swapped */
    vector<uint32_t> pos, stk;
    resize(&pos, nr, 0u);
    for (size_t q = 0; q < nr; q++) pos[at(*ord, q)] = (uint32_t)q;
    for (size_t q = 0; q < nr; q++) {
        int kd = at(*ps, at(*vr, q).k).kind;
        if (kd == P_OPEN) push_back(stk, (uint32_t)q);
        else if (kd == P_CLOSE && len(stk)) {
            uint32_t o = back(stk);
            pop_back(&stk);
            if (at(pos, q) < at(pos, o)) {
                double tw = at(*vr, o).w;
                (*vr)[o].w = at(*vr, q).w;
                (*vr)[q].w = tw;
            }
        }
    }
}

/* an inline element on a bidi line: it, its pieces [k0, k1] there, its shift and offsets */
typedef struct lay_ent { uint32_t el, k0, k1; float shift, rx, ry; } lay_ent;

/* a text fragment of line ln: its text txt[0, n) in its piece p's style, at px on the line, rw
   wide; its baseline the line's, shifted (a top- or bottom-aligned one's against the line box).
   Its index */
static uint32_t lay_text_frag(LCTX, uint32_t ln, const lay_piece *p, const char *txt, size_t n, double px, double rw, double baseline, double height) {
    amb_span sp = text_add(t, txt, n);
    double pb = baseline + p->shift, above, below;
    if (p->va == VA_TOP || p->va == VA_BOTTOM) {
        lay_piece_vert(p, &above, &below);
        pb = p->va == VA_TOP ? above : height - below;
    }
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    /* a pseudo-element's content: generated, its element's */
    t->frag[fr].kind = (p->node & LAY_GEN) ? LAYOUT_FRAG_KIND_GENERATED : LAYOUT_FRAG_KIND_TEXT;
    t->frag[fr].node = node_handle(d, lay_dom(gx, p->node));
    t->frag[fr].text = sp;
    t->frag[fr].x = px + p->rx;
    t->frag[fr].y = pb - p->asc + p->ry;
    t->frag[fr].w = rw;
    t->frag[fr].h = p->asc + p->desc;
    t->frag[fr].baseline = pb + p->ry;
    t->frag[fr].face = p->face;
    if (p->font) t->frag[fr].font = resource_handle(nt, p->font);
    t->frag[fr].spacing = p->ls;
    t->frag[fr].word_spacing = p->wsp;
    t->frag[fr].size = p->size;
    t->frag[fr].italic = p->italic;
    t->frag[fr].color = p->color;
    t->frag[fr].fill = p->fill;
    t->frag[fr].hidden = p->hidden != 0;
    t->frag[fr].decoration = p->deco;
    t->frag[fr].orient = p->orient;
    if (p->elem) {
        uint32_t ts = LAY_X(p->elem).tshadow;
        if (ts) t->frag[fr].shadows = lay_kept(t, st, ts);
    }
    frags_append(t, ln, fr);
    return fr;
}

/* a visual run that is a whole piece: a collapsed space, or all of a word's or kept space's bytes */
static int lay_vr_whole(const lay_vr *r, const lay_piece *p) {
    return (p->kind == P_SPACE && !p->keep) || (r->o0 == p->off && r->o1 == p->off + p->len);
}

/* a bidi line's fragments, its runs vr in their visual order ord, each at its place shifted
   right by shift: the inline elements' (a background, a positioned one's place), one for each
   part of one side by side, the margins outside the edges its ends draw; its text runs, those
   that go on in the text's order in one style merged (unless extra space justifies the line),
   their collapsed spaces at either end drawn by none, a run at an odd level reversed and
   mirrored; its atomic boxes placed; its positioned boxes' static places. open: the inline
   elements open, kept as the other lines keep it */
static void lay_bidi_frags(LCTX, uint32_t b, uint32_t ln, const vector<lay_piece> *ps, size_t a, size_t end, const vector<lay_vr> *vr,
                           const vector<uint32_t> *ord, const vector<uint32_t> *atoms, const vector<double> *abase, vector<lay_open> *open,
                           double x0, double y, double w, double shift, double extra, double baseline, double height) {
    size_t nr = len(*vr);
    vector<lay_ent> ents;
    vector<uint32_t> estk;
    for (size_t q = 0; q < len(*open); q++) {
        lay_open o = at(*open, q);
        lay_ent en;
        en.el = o.el;
        en.k0 = (uint32_t)a;
        en.k1 = (uint32_t)end - 1;
        en.shift = o.shift;
        en.rx = o.rx;
        en.ry = o.ry;
        push_back(estk, (uint32_t)len(ents));
        push_back(ents, en);
    }
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_OPEN) {
            lay_ent en;
            en.el = p.node;
            en.k0 = (uint32_t)k;
            en.k1 = (uint32_t)end - 1;
            en.shift = p.shift;
            en.rx = p.rx;
            en.ry = p.ry;
            push_back(estk, (uint32_t)len(ents));
            push_back(ents, en);
            lay_open o;
            o.el = p.node;
            o.shift = p.shift;
            o.rx = p.rx;
            o.ry = p.ry;
            push_back(open, o);
        } else if (p.kind == P_CLOSE && len(estk)) {
            ents[back(estk)].k1 = (uint32_t)k;
            pop_back(&estk);
            if (len(*open)) pop_back(open);
        }
    }
    for (size_t i = 0; i < len(ents); i++) {
        lay_ent en = at(ents, i);
        lay_style es;
        lay_style_of(LARGS, en.el, &es);
        if ((es.bg & 0xff) == 0 && LAY_X(en.el).pos == POS_STATIC && !lay_decorated(LARGS, en.el, &es)) continue;
        for (size_t q = 0; q < nr;) {
            lay_vr r = at(*vr, at(*ord, q));
            if (r.k < en.k0 || r.k > en.k1) { q++; continue; }
            size_t e2 = q;
            double ax0 = r.x, ax1 = r.x;
            uint32_t kl = r.k, kr = r.k;
            while (e2 < nr) {
                lay_vr s = at(*vr, at(*ord, e2));
                if (s.k < en.k0 || s.k > en.k1) break;
                ax1 = s.x + s.w;
                kr = s.k;
                e2++;
            }
            lay_piece pl = at(*ps, kl), pr = at(*ps, kr);
            if ((pl.kind == P_OPEN || pl.kind == P_CLOSE) && pl.node == en.el) ax0 += lay_res(st, es.m[3], w);
            if ((pr.kind == P_OPEN || pr.kind == P_CLOSE) && pr.node == en.el) ax1 -= lay_res(st, es.m[1], w);
            layout_frag_h fh = frag_new(t);
            uint32_t fr = fh.index;
            t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
            t->frag[fr].node = node_handle(d, lay_dom(gx, en.el));
            t->frag[fr].x = ax0 + shift + en.rx;
            t->frag[fr].w = ax1 - ax0;
            t->frag[fr].y = baseline + en.shift - es.asc - lay_res(st, es.p[0], w) - es.b[0] + en.ry;
            t->frag[fr].h = es.asc + es.desc + lay_res(st, es.p[0], w) + lay_res(st, es.p[2], w) + es.b[0] + es.b[2];
            t->frag[fr].baseline = baseline;
            t->frag[fr].background = es.bg;
            t->frag[fr].hidden = es.hidden != 0;
            lay_inline_piece(LARGS, fr, en.el, &es, w);
            frags_append(t, ln, fr);
            q = e2;
        }
    }
    /* each atomic piece's index in atoms (the line's order) */
    vector<uint32_t> aidx;
    resize(&aidx, end - a, 0u);
    uint32_t na = 0;
    for (size_t k = a; k < end; k++) if (at(*ps, k).kind == P_ATOMIC) aidx[k - a] = na++;
    for (size_t q = 0; q < nr;) {
        lay_vr r = at(*vr, at(*ord, q));
        lay_piece p = at(*ps, r.k);
        double px = r.x + shift;
        if (p.kind == P_ATOMIC) {
            uint32_t ab0 = at(*atoms, at(aidx, r.k - a));
            double base = at(*abase, at(aidx, r.k - a));
            double mh = t->box[ab0].mt + lay_border_height(t, ab0) + t->box[ab0].mb;
            double top = baseline - base + p.shift;
            if (p.va == VA_TOP) top = 0;
            else if (p.va == VA_BOTTOM) top = height - mh;
            lay_place(t, ab0, x0 + px + p.rx + t->box[ab0].ml, y + top + p.ry + t->box[ab0].mt);
            layout_frag_h fh = frag_new(t);
            uint32_t fr = fh.index;
            t->frag[fr].kind = LAYOUT_FRAG_KIND_ATOMIC;
            t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
            t->frag[fr].atomic = ab0;
            t->frag[fr].x = px + p.rx;
            t->frag[fr].y = top + p.ry;
            t->frag[fr].w = r.w;
            t->frag[fr].h = mh;
            t->frag[fr].baseline = baseline;
            frags_append(t, ln, fr);
            q++;
            continue;
        }
        if (p.kind == P_ABS) {
            lay_abs sa;
            sa.el = p.node;
            sa.box = b;
            sa.x = x0 + px;
            sa.y = y;
            push_back(ab, sa);
            q++;
            continue;
        }
        if (p.kind != P_WORD && p.kind != P_SPACE) { q++; continue; }
        size_t e2 = q + 1;
        while (e2 < nr && extra == 0) {
            lay_vr s = at(*vr, at(*ord, e2)), s0 = at(*vr, at(*ord, e2 - 1));
            lay_piece p2 = at(*ps, s.k), p0 = at(*ps, s0.k);
            if (s.lvl != r.lvl || (p2.kind != P_WORD && p2.kind != P_SPACE) || p2.node != p.node) break;
            if (p2.size != p.size || p2.color != p.color || p2.face != p.face || p2.font != p.font || p2.deco != p.deco) break;
            if (p2.shift != p.shift || p2.va != p.va || p2.rx != p.rx || p2.ry != p.ry) break;
            if (!lay_vr_whole(&s, &p2) || !lay_vr_whole(&s0, &p0)) break;
            if ((r.lvl & 1) ? s.k + 1 != s0.k : s0.k + 1 != s.k) break;
            e2++;
        }
        size_t g0 = q, g1 = e2;
        while (g0 < g1 && at(*ps, at(*vr, at(*ord, g0)).k).kind == P_SPACE && !at(*ps, at(*vr, at(*ord, g0)).k).keep) g0++;
        while (g1 > g0 && at(*ps, at(*vr, at(*ord, g1 - 1)).k).kind == P_SPACE && !at(*ps, at(*vr, at(*ord, g1 - 1)).k).keep) g1--;
        if (g0 < g1) {
            lay_vr r0 = at(*vr, at(*ord, g0)), r1 = at(*vr, at(*ord, g1 - 1));
            /* its text in logical order (lt), its context about it (the rest of its first and last
               pieces when it begins or ends inside one, else the pieces' about them); and in
               visual order (vt) */
            vector<char> lt, vt, cx;
            int rtl = r.lvl & 1, whole = 1;
            lay_vr sf = at(*vr, at(*ord, rtl ? g1 - 1 : g0)), sl = at(*vr, at(*ord, rtl ? g0 : g1 - 1));
            lay_piece pf = at(*ps, sf.k), pl = at(*ps, sl.k);
            int fw = pf.kind == P_WORD || (pf.kind == P_SPACE && pf.keep), lw = pl.kind == P_WORD || (pl.kind == P_SPACE && pl.keep);
            if (fw && sf.o0 > pf.off) {
                lay_run_bytes(LARGS, &pf, pf.off, sf.o0, &cx);
                lay_ctx_tail(&cx, &lt);
            } else lay_ctx_before(LARGS, ps, sf.k, &lt);
            size_t lf = len(lt);
            for (size_t z = 0; z < g1 - g0; z++) {
                lay_vr s = at(*vr, at(*ord, rtl ? g1 - 1 - z : g0 + z));
                lay_piece p2 = at(*ps, s.k);
                if (!lay_vr_whole(&s, &p2) || (p2.kind == P_WORD && p2.sdir == 3)) whole = 0;
                if (p2.kind == P_SPACE && !p2.keep) push_back(lt, ' ');
                else lay_run_bytes(LARGS, &p2, s.o0, s.o1, &lt);
            }
            size_t lto = len(lt);
            vector<char> body;
            for (size_t q2 = lf; q2 < lto; q2++) push_back(body, at(lt, q2));
            clear(cx);
            if (lw && sl.o1 < pl.off + pl.len) {
                lay_run_bytes(LARGS, &pl, sl.o1, pl.off + pl.len, &cx);
                lay_ctx_head(&cx, &lt);
            } else lay_ctx_after(LARGS, ps, sl.k, &lt);
            if (rtl) lay_bidi_reverse(&body, &vt);
            else append_range(vt, data(body), len(body));
            lay_piece pg = at(*ps, r0.k);
            if (len(vt)) {
                uint32_t fr = lay_text_frag(LARGS, ln, &pg, data(vt), len(vt), r0.x + shift, r1.x + r1.w - r0.x, baseline, height);
                /* its glyphs: its pieces' runs' (whole pieces), else its own text shaped in its
                   direction (a part of a word the levels split) */
                if (whole) lay_frag_glyphs(LARGS, ps, sf.k, (size_t)sl.k + 1, rtl, fr);
                else {
                    vector<amb_shaped> gl;
                    vector<char> out;
                    double gx0 = 0;
                    if (lto > lf) amb_shape(f, pg.face, data(lt), len(lt), lf, lto, (rtl ? AMB_SHAPE_RTL : 0) | (pg.ls != 0 ? AMB_SHAPE_NOLIGA : 0), &gl);
                    lay_pack_glyphs(LARGS, &pg, &lt, 0, &gl, 0, len(gl), (uint32_t)lf, (uint32_t)lto, &gx0, &out);
                    lay_put_glyphs(t, &out, fr);
                }
            }
        }
        q = e2;
    }
}


/* a text item's width as Blink places what follows it: rounded up to a 64th of a px (its
   LayoutUnit, ShapeResult::SnappedWidth: a line's items, each a text node's run on it or an
   inline element's edge or an atomic box, follow each other at their LayoutUnits) */
static double lay_ceil64(double w) { return ceil(w * 64 - 1e-5) / 64; }

/* two text pieces of one item (a text node's, on a line; a kept tab is an item of its own) */
static int lay_one_item(LCTX, lay_piece p, lay_piece q) {
    return (p.kind == P_WORD || p.kind == P_SPACE) && (q.kind == P_WORD || q.kind == P_SPACE) && p.node == q.node && !lay_tabbed(LARGS, p) &&
           !lay_tabbed(LARGS, q);
}

/* the rest of a bidi line (lay_line's, its pieces [a, end) measured, its atomic boxes laid
   out, its extent above and below its baseline known): its visual runs, aligned (a collapsed
   space with what justifying adds), the line, its fragments; its height */
static double lay_bidi_finish(LCTX, uint32_t b, const lay_style *bs, const vector<lay_piece> *ps, size_t a, size_t end, double x0, double w,
                              double y, int justify_ok, double indent, vector<lay_open> *open, const vector<lay_bu> *bu, int para,
                              const vector<uint32_t> *atoms, const vector<double> *abase, double max_above, double max_below) {
    vector<lay_vr> vr;
    vector<uint32_t> ord;
    lay_bidi_line(LARGS, ps, a, end, bu, para, &vr, &ord);
    /* each text item (one text node's runs side by side at one level) its width rounded up, the
       difference its last run's (lay_ceil64) */
    for (size_t q = 0; q < len(ord);) {
        lay_vr r = at(vr, at(ord, q));
        lay_piece p = at(*ps, r.k);
        size_t q1 = q + 1;
        if (p.kind != P_WORD && p.kind != P_SPACE) { q = q1; continue; }
        double tot = r.w;
        while (q1 < len(ord)) {
            lay_vr s = at(vr, at(ord, q1));
            if (s.lvl != r.lvl || !lay_one_item(LARGS, p, at(*ps, s.k))) break;
            tot += s.w;
            q1++;
        }
        uint32_t li = at(ord, q1 - 1);
        lay_vr lr = at(vr, li);
        lr.w += lay_ceil64(tot) - tot;
        vr[li] = lr;
        q = q1;
    }
    double line_w = indent;
    int spaces = 0;
    for (size_t q = 0; q < len(vr); q++) {
        line_w += at(vr, q).w;
        if (at(*ps, at(vr, q).k).kind == P_SPACE && !at(*ps, at(vr, q).k).keep) spaces++;
    }
    double free = w - line_w, shift = 0, extra = 0;
    if (bs->align == AL_RIGHT && free > 0) shift = free;
    else if (bs->align == AL_CENTER && free > 0) shift = free / 2;
    else if (bs->align == AL_JUSTIFY && justify_ok && free > 0 && spaces > 0) extra = free / spaces;
    double baseline = max_above, height = max_above + max_below;
    layout_line_h lh = line_new(t);
    uint32_t ln = lh.index;
    t->line[ln].x = x0;
    t->line[ln].y = y;
    t->line[ln].w = w;
    t->line[ln].h = height;
    t->line[ln].baseline = baseline;
    lines_append(t, b, ln);
    double vx = indent;
    for (size_t q = 0; q < len(ord); q++) {
        uint32_t i = at(ord, q);
        lay_vr r = at(vr, i);
        if (at(*ps, r.k).kind == P_SPACE && !at(*ps, r.k).keep) r.w += extra;
        r.x = vx;
        vx += r.w;
        vr[i] = r;
    }
    lay_bidi_frags(LARGS, b, ln, ps, a, end, &vr, &ord, atoms, abase, open, x0, y, w, shift, extra, baseline, height);
    return height;
}

/* an inline element's edge that each of its fragments has when it is cloned (box-decoration-break: clone, CSS
   Fragmentation 3, 5.4): its margin, border and padding at its start (end 0) or at its end (end 1); 0 when it is sliced */
static double lay_clone_edge(LCTX, uint32_t el, int end, double cw) {
    if (!LAY_X(el).clone) return 0;
    lay_style es;
    lay_style_of(LARGS, el, &es);
    if (end) return lay_res(st, es.p[1], cw) + es.b[1] + lay_res(st, es.m[1], cw);
    return lay_res(st, es.m[3], cw) + es.b[3] + lay_res(st, es.p[3], cw);
}
/* the end edges (lay_clone_edge) of the cloned elements open at the end of the pieces [a, end), those in open being open
   at their start: what a line adds after its content, where Chromium wraps each fragment's end (measured: the edge is no
   part of where the line breaks; a fragment may reach past the line) */
static double lay_clone_ends(LCTX, const vector<lay_piece> *ps, size_t a, size_t end, const vector<lay_open> *open, double cw) {
    vector<uint32_t> stk;
    for (size_t q = 0; q < len(*open); q++) push_back(stk, at(*open, q).el);
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_OPEN) push_back(stk, p.node);
        else if (p.kind == P_CLOSE && len(stk)) pop_back(&stk);
    }
    double sum = 0;
    for (size_t q = 0; q < len(stk); q++) sum += lay_clone_edge(LARGS, at(stk, q), 1, cw);
    return sum;
}
/* where an inline element open at a line's start (the k-th of open, those before it its ancestors) begins its fragment
   there: after its ancestors' cloned start edges, and its own margin when it is cloned (at the line's start, shifted by
   the line's alignment), else at the line's start, as a sliced one continues */
static double lay_clone_lead(LCTX, const vector<lay_open> *open, size_t k, double cw, double shift) {
    double lead = 0;
    for (size_t q = 0; q < k && q < len(*open); q++) lead += lay_clone_edge(LARGS, at(*open, q).el, 0, cw);
    uint32_t el = at(*open, k).el;
    int clone = LAY_X(el).clone != 0;
    if (clone) {
        lay_style es;
        lay_style_of(LARGS, el, &es);
        lead += lay_res(st, es.m[3], cw);
    }
    return clone || lead > 0 ? shift + lead : 0;
}

/* the box that holds an inline element's clip-path (a basic shape or a geometry box: lay_ext's cpath) for its fragments
   in box b's lines (each fragment's clip): the one a fragment of it there has (made: the line's fragments not yet in
   it, each key << 32 | fragment), else a new one, in no box's children (as a piece is), its edges the element's; placed
   on the element's first fragment by lay_inline_clips once every line is where it ends */
static uint32_t lay_clip_box(LCTX, uint32_t b, uint32_t el, double cw, const vector<uint64_t> *made) {
    uint32_t node = lay_dom(gx, el);
    uint8_t ps = (el & LAY_GEN) ? (uint8_t)at(*gx, el & LAY_GIDX).pseudo : 0;
    for (uint32_t l = t->box[b].first_line; l; l = t->line[l].next)
        for (uint32_t fr = t->line[l].first_frag; fr; fr = t->frag[fr].next) {
            uint32_t cb = t->frag[fr].clip;
            if (cb && t->box[cb].node.index == node && t->box[cb].pseudo == ps) return cb;
        }
    for (size_t q = 0; q < len(*made); q++) {
        uint32_t cb = t->frag[(uint32_t)(at(*made, q) & 0xffffffffu)].clip;
        if (cb && t->box[cb].node.index == node && t->box[cb].pseudo == ps) return cb;
    }
    layout_box_h h = box_new(t);
    uint32_t nb = h.index;
    t->box[nb].kind = LAYOUT_BOX_KIND_INLINE;
    t->box[nb].opacity = 1;
    t->box[nb].node = node_handle(d, node);
    t->box[nb].pseudo = ps;
    lay_style es;
    lay_style_of(LARGS, el, &es);
    lay_edges(t, st, nb, &es, cw);
    t->box[nb].mt = 0;
    t->box[nb].mr = 0;
    t->box[nb].mb = 0;
    t->box[nb].ml = 0;
    lay_box_paint(LARGS, nb, lay_si(sx, gx, el));
    t->box[nb].clone = LAY_X(el).clone != 0;
    return nb;
}
/* the innermost of the elements stk holds (the inline elements open, outermost first) whose clip-path clips what is in
   it: its clip box (lay_clip_box); 0 for none */
static uint32_t lay_inline_clip_of(LCTX, uint32_t b, const vector<uint32_t> *stk, double cw, const vector<uint64_t> *made) {
    for (size_t q = len(*stk); q >= 1; q--)
        if (LAY_X(at(*stk, q - 1)).cpath) return lay_clip_box(LARGS, b, at(*stk, q - 1), cw, made);
    return 0;
}

/* one line of pieces [a, e) in box b at y, w wide; returns its height. open: the inline
   elements open at its start, which it leaves as those open at its end. bu: its paragraph's
   bidi units, at level para (empty: no bidi text in it) */
static double lay_line(LCTX, uint32_t b, const lay_style *bs, const vector<lay_piece> *ps, size_t a, size_t e, double x0, double w, double y,
                       int justify_ok, double indent, vector<lay_open> *open, const vector<lay_bu> *bu, int para, uint32_t root, int marker) {
    /* trailing collapsible spaces hang: they take no width */
    size_t end = e;
    while (end > a && at(*ps, end - 1).kind == P_SPACE && !at(*ps, end - 1).keep) end--;
    /* the pieces' x and the line's vertical extent: each piece about the baseline, shifted
       by its vertical-align; top- and bottom-aligned ones against the line box after */
    vector<double> xs;
    vector<uint32_t> atoms;
    vector<double> abase;            /* each atomic box's baseline, from its margin box's top, as its vertical-align has it */
    double x = indent, above, below, max_above, max_below, top_h = 0, bottom_h = 0, istart = indent;
    lay_leading(bs->lh, bs->asc, bs->desc, &max_above, &max_below);
    /* the line height quirk (quirks and limited-quirks modes; the Quirks Mode Standard 3.3 and 3.4,
       as Blink has it, measured): no strut -- the block's line height counts on a line with text of
       its own (a word or a space between words directly in root) and on a list item's own lines; an
       inline box's on a line with text in it, or a border or padding at its edge there (its margins
       count for nothing); a forced break's on a line nothing else gives a height. The line is then
       as tall as what counts reaches, above or below the baseline (nothing: 0) */
    double strut_above = max_above, strut_below = max_below;
    int root_text = marker;
    if (lay_lhq) { max_above = -1e9; max_below = -1e9; }
    int spaces = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        push_back(xs, x);
        if (p.kind == P_SPACE && !p.keep) spaces++;
        int edge = 0;
        if (lay_lhq && (p.kind == P_OPEN || p.kind == P_CLOSE)) {
            lay_style es;
            lay_style_of(LARGS, p.node, &es);
            edge = p.kind == P_OPEN ? es.b[3] + lay_res(st, es.p[3], w) > 0 : es.b[1] + lay_res(st, es.p[1], w) > 0;
            /* a wbr is text (Blink's zero-width break opportunity): its parent's */
            if (p.kind == P_OPEN && lay_is_named(d, p.node, "wbr")) {
                if (lay_parent_of(d, gx, p.node) == root) root_text = 1;
                else edge = 1;
            }
        }
        if (lay_lhq && (p.kind == P_WORD || p.kind == P_SPACE) && p.elem == root) root_text = 1;
        if (p.kind == P_WORD || (p.kind == P_SPACE && (p.keep || lay_lhq)) || edge) {
            lay_piece_vert(&p, &above, &below);
            if (p.va == VA_TOP) { if (above + below > top_h) top_h = above + below; }
            else if (p.va == VA_BOTTOM) { if (above + below > bottom_h) bottom_h = above + below; }
            else {
                if (above - p.shift > max_above) max_above = above - p.shift;
                if (below + p.shift > max_below) max_below = below + p.shift;
            }
        }
        if (p.kind == P_ATOMIC) {
            uint32_t ab0 = lay_atomic(LARGS, b, &p, w);
            push_back(atoms, ab0);
            double mh = t->box[ab0].mt + lay_border_height(t, ab0) + t->box[ab0].mb;
            double base = lay_atomic_baseline(t, ab0);
            if (p.va == VA_MIDDLE || p.va == VA_TEXT_TOP || p.va == VA_TEXT_BOTTOM) {
                /* against its parent's font */
                lay_style pst;
                lay_style_of(LARGS, lay_parent_of(d, gx, p.node), &pst);
                if (p.va == VA_MIDDLE) base = mh / 2 + lay_xh(&pst) / 2;
                else if (p.va == VA_TEXT_TOP) base = pst.asc;
                else base = mh - pst.desc;
            }
            push_back(abase, base);
            if (p.va == VA_TOP) { if (mh > top_h) top_h = mh; }
            else if (p.va == VA_BOTTOM) { if (mh > bottom_h) bottom_h = mh; }
            else {
                if (base - p.shift > max_above) max_above = base - p.shift;
                if (mh - base + p.shift > max_below) max_below = mh - base + p.shift;
            }
        }
        if (k == a || !lay_one_item(LARGS, at(*ps, k - 1), p)) istart = x;
        x += p.w;
        /* a text item's end: what follows starts at its width rounded up (lay_ceil64) */
        if ((p.kind == P_WORD || p.kind == P_SPACE) && (k + 1 >= end || !lay_one_item(LARGS, p, at(*ps, k + 1)))) x = istart + lay_ceil64(x - istart);
    }
    if (lay_lhq && root_text) {
        if (strut_above > max_above) max_above = strut_above;
        if (strut_below > max_below) max_below = strut_below;
    }
    if (lay_lhq && max_above < -1e8 && e < len(*ps) && at(*ps, e).kind == P_BREAK && top_h == 0 && bottom_h == 0) {
        /* a line that is a forced break alone: its parent's line height (the block's strut, directly in it) */
        lay_piece p = at(*ps, e);
        if (lay_parent_of(d, gx, p.node) == root) { max_above = strut_above; max_below = strut_below; }
        else {
            lay_piece_vert(&p, &above, &below);
            max_above = above - p.shift;
            max_below = below + p.shift;
        }
    }
    if (lay_lhq && max_above < -1e8) { max_above = 0; max_below = 0; }
    /* a top-aligned piece taller than the rest grows the line downward, a bottom-aligned one upward */
    if (top_h > max_above + max_below) max_below = top_h - max_above;
    if (bottom_h > max_above + max_below) max_above = bottom_h - max_below;
    /* bidirectional text (UAX #9): the line's visual runs, in their order */
    if (len(*bu) > 0 && end > a)
        return lay_bidi_finish(LARGS, b, bs, ps, a, end, x0, w, y, justify_ok, indent, open, bu, para, &atoms, &abase, max_above, max_below);
    /* the cloned elements' end edges after the content (a cloned one's start edges are in indent: lay_lines) */
    double cends = lay_clone_ends(LARGS, ps, a, end, open, w);
    /* the ideographic spaces the line ends in hang: no part of the width it is aligned by (lay_break_pieces) */
    double hang = 0;
    for (size_t q = end; q > a && (at(*ps, q - 1).kind == P_CLOSE || (at(*ps, q - 1).kind == P_WORD && (at(*ps, q - 1).lbf & LBF_HANG))); q--)
        if (at(*ps, q - 1).kind == P_WORD) hang += at(*ps, q - 1).w;
    double line_w = x, free = w - line_w - cends + hang, shift = 0, extra = 0;
    if (bs->align == AL_RIGHT && free > 0) shift = free;
    else if (bs->align == AL_CENTER && free > 0) shift = free / 2;
    else if (bs->align == AL_JUSTIFY && justify_ok && free > 0 && spaces > 0) extra = free / spaces;
    double baseline = max_above, height = max_above + max_below;
    layout_line_h lh = line_new(t);
    uint32_t ln = lh.index;
    t->line[ln].x = x0;
    t->line[ln].y = y;
    t->line[ln].w = w;
    t->line[ln].h = height;
    t->line[ln].baseline = baseline;
    lines_append(t, b, ln);
    /* the inline elements' backgrounds, under the text, and a positioned one's place (which
       makes the containing block of an absolutely positioned box in it): each open one from
       where it is opened (or the line's start) to where it is closed (or the line's end),
       shifted and offset as its pieces are; the fragments in the order their elements open (an
       ancestor's under its descendants', as Chromium paints them), not as they close */
    vector<double> starts;           /* for each open element, where it opens on this line (-1: before the line) */
    vector<uint32_t> okey;           /* and the order it opens in */
    vector<uint64_t> made;           /* each fragment made, by its element's order: key << 32 | fragment */
    size_t n0 = len(*open);
    vector<uint32_t> stk;            /* the elements open as the text runs go: at the line's start, its open ones */
    for (size_t k = 0; k < n0; k++) push_back(stk, at(*open, k).el);
    for (size_t k = 0; k < n0; k++) { push_back(starts, -1.0); push_back(okey, (uint32_t)k); }
    double gap = 0;
    for (size_t k = a; k < end; k++) {
        lay_piece p = at(*ps, k);
        double px = at(xs, k - a) + shift + gap;
        if (p.kind == P_SPACE && !p.keep) gap += extra;
        if (p.kind == P_ABS) {
            /* an absolutely positioned element in the line: its static position */
            lay_abs sa;
            sa.el = p.node;
            sa.box = b;
            sa.x = x0 + px;
            sa.y = y;
            push_back(ab, sa);
        }
        if (p.kind == P_OPEN) {
            lay_open o;
            o.el = p.node;
            o.shift = p.shift;
            o.rx = p.rx;
            o.ry = p.ry;
            push_back(open, o);
            push_back(starts, px);
            push_back(okey, (uint32_t)(n0 + (k - a)));
        }
        if (p.kind == P_CLOSE && len(*open)) {
            /* its background, from its start on this line (or the line's start) to here */
            double from = back(starts);
            uint64_t key = back(okey);
            pop_back(&starts);
            pop_back(&okey);
            uint32_t elx = back(*open).el;
            pop_back(open);
            lay_style es;
            lay_style_of(LARGS, elx, &es);
            if ((es.bg & 0xff) != 0 || LAY_X(elx).pos != POS_STATIC || lay_decorated(LARGS, elx, &es) || LAY_X(elx).cpath) {
                double x0b = from;
                if (from < 0) {
                    lay_open self;
                    memset(&self, 0, sizeof self);
                    self.el = elx;
                    push_back(open, self);
                    x0b = lay_clone_lead(LARGS, open, len(*open) - 1, w, shift);
                    pop_back(open);
                }
                layout_frag_h fh = frag_new(t);
                uint32_t fr = fh.index;
                t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
                t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
                t->frag[fr].x = x0b + (from < 0 ? 0 : lay_res(st, es.m[3], w)) + p.rx;
                t->frag[fr].w = px + p.w - lay_res(st, es.m[1], w) + p.rx - t->frag[fr].x;
                t->frag[fr].y = baseline + p.shift - es.asc - lay_res(st, es.p[0], w) - es.b[0] + p.ry;
                t->frag[fr].h = es.asc + es.desc + lay_res(st, es.p[0], w) + lay_res(st, es.p[2], w) + es.b[0] + es.b[2];
                t->frag[fr].baseline = baseline;
                t->frag[fr].background = es.bg;
                t->frag[fr].hidden = es.hidden != 0;
                lay_inline_piece(LARGS, fr, elx, &es, w);
                /* the clip of the innermost element about it, itself first, whose clip-path clips */
                vector<uint32_t> up;
                for (size_t q = 0; q < len(*open); q++) push_back(up, at(*open, q).el);
                push_back(up, elx);
                uint32_t cb = lay_inline_clip_of(LARGS, b, &up, w, &made);
                if (cb) t->frag[fr].clip = cb;
                push_back(made, key << 32 | fr);
            }
        }
    }
    /* the inline elements still open at the line's end: their backgrounds to the end */
    for (size_t k = 0; k < len(*open); k++) {
        lay_open o = at(*open, k);
        uint32_t elx = o.el;
        lay_style es;
        lay_style_of(LARGS, elx, &es);
        if ((es.bg & 0xff) == 0 && LAY_X(elx).pos == POS_STATIC && !lay_decorated(LARGS, elx, &es) && !LAY_X(elx).cpath) continue;
        double from = at(starts, k);
        double x0b = from < 0 ? lay_clone_lead(LARGS, open, k, w, shift) : from + lay_res(st, es.m[3], w);
        /* past the content: the end edges of the cloned elements inside it, then its own border and padding */
        double tail = LAY_X(elx).clone ? lay_res(st, es.p[1], w) + es.b[1] : 0;
        for (size_t q = k + 1; q < len(*open); q++) tail += lay_clone_edge(LARGS, at(*open, q).el, 1, w);
        double sh = o.shift, orx = o.rx, ory = o.ry;
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        t->frag[fr].kind = LAYOUT_FRAG_KIND_INLINE;
        t->frag[fr].node = node_handle(d, lay_dom(gx, elx));
        t->frag[fr].x = x0b + orx;
        t->frag[fr].w = line_w + shift + gap - x0b + tail;
        t->frag[fr].y = baseline + sh - es.asc - lay_res(st, es.p[0], w) - es.b[0] + ory;
        t->frag[fr].h = es.asc + es.desc + lay_res(st, es.p[0], w) + lay_res(st, es.p[2], w) + es.b[0] + es.b[2];
        t->frag[fr].baseline = baseline;
        t->frag[fr].background = es.bg;
        t->frag[fr].hidden = es.hidden != 0;
        lay_inline_piece(LARGS, fr, elx, &es, w);
        vector<uint32_t> up;
        for (size_t q = 0; q <= k; q++) push_back(up, at(*open, q).el);
        uint32_t cb = lay_inline_clip_of(LARGS, b, &up, w, &made);
        if (cb) t->frag[fr].clip = cb;
        push_back(made, (uint64_t)at(okey, k) << 32 | fr);
    }
    for (size_t q = 1; q < len(made); q++) {
        uint64_t m = at(made, q);
        size_t r = q;
        while (r > 0 && at(made, r - 1) >> 32 > m >> 32) { made[r] = at(made, r - 1); r--; }
        made[r] = m;
    }
    for (size_t q = 0; q < len(made); q++) frags_append(t, ln, (uint32_t)(at(made, q) & 0xffffffffu));
    /* the text runs (a text node's words and spaces, as one fragment unless justified)
       and the atomic boxes */
    size_t k = a, atom = 0;
    gap = 0;
    while (k < end) {
        lay_piece p = at(*ps, k);
        double px = at(xs, k - a) + shift + gap;
        if (p.kind == P_ATOMIC) {
            uint32_t ab0 = at(atoms, atom);
            double base = at(abase, atom);
            atom++;
            double mh = t->box[ab0].mt + lay_border_height(t, ab0) + t->box[ab0].mb;
            double top = baseline - base + p.shift;
            if (p.va == VA_TOP) top = 0;
            else if (p.va == VA_BOTTOM) top = height - mh;
            lay_place(t, ab0, x0 + px + p.rx + t->box[ab0].ml, y + top + p.ry + t->box[ab0].mt);
            layout_frag_h fh = frag_new(t);
            uint32_t fr = fh.index;
            t->frag[fr].kind = LAYOUT_FRAG_KIND_ATOMIC;
            t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
            t->frag[fr].atomic = ab0;
            t->frag[fr].x = px + p.rx;
            t->frag[fr].y = top + p.ry;
            t->frag[fr].w = p.w;
            t->frag[fr].h = mh;
            t->frag[fr].baseline = baseline;
            frags_append(t, ln, fr);
            k++;
            continue;
        }
        if (p.kind != P_WORD && !(p.kind == P_SPACE && p.keep)) {
            if (p.kind == P_SPACE) gap += extra;
            if (p.kind == P_OPEN) push_back(stk, p.node);
            else if (p.kind == P_CLOSE && len(stk)) pop_back(&stk);
            k++;
            continue;
        }
        /* a run: this text node's words and spaces that follow, on this line, in one style */
        size_t r = k + 1;
        double rw = p.w;
        while (r < end && extra == 0) {
            lay_piece q = at(*ps, r);
            if ((q.kind != P_WORD && q.kind != P_SPACE) || q.node != p.node) break;
            if (q.size != p.size || q.color != p.color || q.face != p.face || q.font != p.font || q.deco != p.deco) break;
            if (q.shift != p.shift || q.va != p.va || q.rx != p.rx || q.ry != p.ry) break;
            rw += q.w;
            r++;
        }
        while (r > k + 1 && at(*ps, r - 1).kind == P_SPACE && !at(*ps, r - 1).keep) { r--; rw -= at(*ps, r).w; }
        vector<char> txt;
        lay_piece_text(t, d, gx, ps, k, r, &txt);
        amb_span sp = text_add(t, data(txt), len(txt));
        /* its baseline: the line's, shifted; a top- or bottom-aligned run's against the line box */
        double pb = baseline + p.shift;
        if (p.va == VA_TOP || p.va == VA_BOTTOM) {
            lay_piece_vert(&p, &above, &below);
            pb = p.va == VA_TOP ? above : height - below;
        }
        layout_frag_h fh = frag_new(t);
        uint32_t fr = fh.index;
        /* a pseudo-element's content: generated, its element's */
        t->frag[fr].kind = (p.node & LAY_GEN) ? LAYOUT_FRAG_KIND_GENERATED : LAYOUT_FRAG_KIND_TEXT;
        t->frag[fr].node = node_handle(d, lay_dom(gx, p.node));
        t->frag[fr].text = sp;
        t->frag[fr].x = px + p.rx;
        t->frag[fr].y = pb - p.asc + p.ry;
        t->frag[fr].w = rw;
        t->frag[fr].h = p.asc + p.desc;
        t->frag[fr].baseline = pb + p.ry;
        t->frag[fr].face = p.face;
        if (p.font) t->frag[fr].font = resource_handle(nt, p.font);
        t->frag[fr].spacing = p.ls;
        t->frag[fr].word_spacing = p.wsp;
        t->frag[fr].size = p.size;
        t->frag[fr].italic = p.italic;
        t->frag[fr].color = p.color;
        t->frag[fr].fill = p.fill;
        t->frag[fr].hidden = p.hidden != 0;
        t->frag[fr].decoration = p.deco;
        t->frag[fr].orient = p.orient;
        if (p.elem) {
            uint32_t ts = LAY_X(p.elem).tshadow;
            if (ts) t->frag[fr].shadows = lay_kept(t, st, ts);
        }
        lay_frag_glyphs(LARGS, ps, k, r, 0, fr);
        uint32_t cb = lay_inline_clip_of(LARGS, b, &stk, w, &made);
        if (cb) t->frag[fr].clip = cb;
        frags_append(t, ln, fr);
        k = r;
    }
    return height;
}

/* ---- floats (CSS 2.1 9.5) ---- */

/* the left and right edges a band [y, y + h) of the block formatting context leaves a
   content box at ox, w wide, beside the floats in it; next: the nearest bottom of a float
   in the band (-1 when none is) */
static void lay_band(const vector<lay_flt> *fl, double ox, double w, double y, double h, double *l, double *r, double *next) {
    *l = 0;
    *r = w;
    *next = -1;
    for (size_t k = 0; k < len(*fl); k++) {
        lay_flt q = at(*fl, k);
        if (q.h <= 0 || q.y >= y + h || q.y + q.h <= y) continue;
        if (q.side == FL_LEFT) { if (q.x + q.w - ox > *l) *l = q.x + q.w - ox; }
        else if (q.x - ox < *r) *r = q.x - ox;
        if (*next < 0 || q.y + q.h < *next) *next = q.y + q.h;
    }
    if (*r < *l) *r = *l;
}

/* the lowest bottom of the floats on the sides clr clears (1 left, 2 right, 3 both) */
static double lay_clear_y(const vector<lay_flt> *fl, int clr) {
    double y = 0;
    for (size_t k = 0; k < len(*fl); k++) {
        lay_flt q = at(*fl, k);
        if (((clr & 1) && q.side == FL_LEFT) || ((clr & 2) && q.side == FL_RIGHT)) if (q.y + q.h > y) y = q.y + q.h;
    }
    return y;
}

/* a float's box, laid out as a child of b in b's content box cw wide: its width shrunk
   to fit unless declared, its content a formatting context of its own */
/* a float's side, or a clear's (1 left, 2 right, 3 both; 4 inline-start, 5 inline-end), its logical ones made left or
   right by its containing block's direction (its parent element's) */
static int lay_float_side(LCTX, uint32_t el, int v) {
    if (v != FL_ISTART && v != FL_IEND) return v;
    uint32_t par = lay_parent_of(d, gx, el);
    int rtl = par ? data(*sc)[lay_si(sx, gx, par)].rtl : 0;
    return (v == FL_ISTART) != (rtl != 0) ? FL_LEFT : FL_RIGHT;
}
static int lay_clear_of(LCTX, uint32_t el) {
    int c = LAY_X(el).clr;
    return c == 4 ? lay_float_side(LARGS, el, FL_ISTART) : c == 5 ? lay_float_side(LARGS, el, FL_IEND) : c;
}
static uint32_t lay_float_box(LCTX, uint32_t b, uint32_t el, double cw, double ch) {
    lay_bk k;
    k.ox = 0;
    k.oy = 0;
    k.ch = ch;
    k.fw = -1;
    k.shrink = 1;
    k.item = 0;
    k.qs = 0;
    lay_mc mt, mb;
    int empty;
    uint32_t fb = lay_block(LARGS, b, el, cw, 0, 0, 0, 0, &k, &mt, &mb, &empty);
    t->box[fb].float_side = lay_float_side(LARGS, el, LAY_X(el).flt) == FL_RIGHT ? LAYOUT_FLOAT_SIDE_RIGHT : LAYOUT_FLOAT_SIDE_LEFT;
    return fb;
}

/* a float placed (CSS 2.1 9.5.1): as high as y (in b's content box, at (ox, oy) in the
   block formatting context) and no higher than an earlier float, past what it clears, as
   far to its side as the floats there leave room, lower where they leave too little; its
   margin box kept in fl */
static void lay_float_place(LCTX, uint32_t fb, uint32_t el, double cw, double ox, double oy, double y) {
    int side = t->box[fb].float_side == LAYOUT_FLOAT_SIDE_RIGHT ? FL_RIGHT : FL_LEFT;
    double mw = t->box[fb].ml + t->box[fb].bl + t->box[fb].pl + t->box[fb].w + t->box[fb].pr + t->box[fb].br + t->box[fb].mr;
    double mh = t->box[fb].mt + lay_border_height(t, fb) + t->box[fb].mb;
    double fy = oy + y;
    for (size_t k = 0; k < len(*fl); k++) if (at(*fl, k).y > fy) fy = at(*fl, k).y;
    int clr = lay_clear_of(LARGS, el);
    if (clr) { double cy = lay_clear_y(fl, clr); if (cy > fy) fy = cy; }
    double fx = 0;
    for (int guard = 0; guard < 200; guard++) {
        double l, r, next;
        lay_band(fl, ox, cw, fy, mh > 0 ? mh : 0.01, &l, &r, &next);
        if (r - l >= mw - 0.01 || next < 0) { fx = side == FL_LEFT ? l : r - mw; break; }
        fy = next;
    }
    lay_flt q;
    q.x = ox + fx;
    q.y = fy;
    q.w = mw;
    q.h = mh;
    q.side = side;
    push_back(fl, q);
    lay_place(t, fb, fx + t->box[fb].ml, fy - oy + t->box[fb].mt);
}

/* the out-of-flow pieces of a run that shows nothing, where it is (y in b's content box):
   the positioned elements' static positions, the floats placed */
static void lay_run_oof(LCTX, uint32_t b, const vector<lay_piece> *ps, double w, double ox, double oy, double y) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.kind == P_ABS) {
            lay_abs sa;
            sa.el = p.node;
            sa.box = b;
            sa.x = 0;
            sa.y = y;
            push_back(ab, sa);
        } else if (p.kind == P_FLOAT) {
            uint32_t fb = lay_float_box(LARGS, b, p.node, w, lay_line_pch);
            lay_float_place(LARGS, fb, p.node, w, ox, oy, y);
        }
    }
}

/* the width of the first chunk of pieces from i: what a line must hold to begin */
static double lay_first_chunk(const vector<lay_piece> *ps, size_t i) {
    double w = 0;
    size_t c = i;
    while (c < len(*ps) && at(*ps, c).kind != P_SPACE && at(*ps, c).kind != P_BREAK) {
        if (c > i && (at(*ps, c).kind == P_ATOMIC || at(*ps, c - 1).kind == P_ATOMIC || (at(*ps, c).bb & AMB_BRK_LINE))) break;
        w += at(*ps, c).w;
        c++;
    }
    return w;
}
/* ---- hyphenation (Liang's algorithm over the English patterns Chromium has) ---- */

/* a dictionary's patterns and exceptions (g: 0 American English, 1 British) */
static const char *lay_hyph_pats(int g) { return g ? amb_hyph_gb_patterns : amb_hyph_patterns; }
static size_t lay_hyph_npats(int g) { return g ? sizeof amb_hyph_gb_patterns - 1 : sizeof amb_hyph_patterns - 1; }
static const char *lay_hyph_excs(int g) { return g ? amb_hyph_gb_exceptions : amb_hyph_exceptions; }
static size_t lay_hyph_nexcs(int g) { return g ? sizeof amb_hyph_gb_exceptions - 1 : sizeof amb_hyph_exceptions - 1; }

/* each dictionary's patterns, found by their letters' hash: each slot a pattern's offset in
   its string, plus one (0 an empty slot); the exceptions likewise, by their letters' hash;
   made once a process */
#define LAY_HYPH_SLOTS 16384
#define LAY_HYPH_XSLOTS 4096
static uint32_t lay_hyph_slot[2][LAY_HYPH_SLOTS];
static uint32_t lay_hyph_xslot[2][LAY_HYPH_XSLOTS];
static int lay_hyph_ready[2];

static uint32_t lay_hyph_hash(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h & (LAY_HYPH_SLOTS - 1);
}
/* an exception's letters' hash, its hyphens passed over */
static uint32_t lay_hyph_xhash(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) if (s[i] != '-') { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h & (LAY_HYPH_XSLOTS - 1);
}
static int lay_digit(char c) { return c >= '0' && c <= '9'; }
static void lay_hyph_load(int g) {
    if (lay_hyph_ready[g]) return;
    lay_hyph_ready[g] = 1;
    const char *pats = lay_hyph_pats(g), *excs = lay_hyph_excs(g);
    size_t i = 0, n = lay_hyph_npats(g);
    while (i < n) {
        size_t b = i, e = i;
        while (i < n && pats[i] != ' ') i++;
        while (e < i && !lay_digit(pats[e])) e++;
        uint32_t h = lay_hyph_hash(pats + b, e - b);
        while (lay_hyph_slot[g][h]) h = (h + 1) & (LAY_HYPH_SLOTS - 1);
        lay_hyph_slot[g][h] = (uint32_t)b + 1;
        i++;
    }
    i = 0;
    n = lay_hyph_nexcs(g);
    while (i < n) {
        size_t b = i;
        while (i < n && excs[i] != ' ') i++;
        if (i > b) {
            uint32_t h = lay_hyph_xhash(excs + b, i - b);
            while (lay_hyph_xslot[g][h]) h = (h + 1) & (LAY_HYPH_XSLOTS - 1);
            lay_hyph_xslot[g][h] = (uint32_t)b + 1;
        }
        i++;
    }
}

/* the places a word of lower-case letters w[0, n) may be broken at (CSS hyphens: auto) by
   dictionary g, as Chromium's hyphenator (Android's minikin) finds them: ok[i] is 1 where a
   hyphen may come before letter i -- an exception's hyphens, or the odd values Liang's
   patterns give -- two letters at least before it and three after, in a word of 5 to 62
   letters */
static void lay_hyph_points(int g, const char *w, size_t n, uint8_t *ok) {
    memset(ok, 0, n + 1);
    if (n < 5 || n > 62) return;
    lay_hyph_load(g);
    const char *pats = lay_hyph_pats(g), *excs = lay_hyph_excs(g);
    /* an exception: its hyphens as written */
    uint32_t xh = lay_hyph_xhash(w, n);
    while (lay_hyph_xslot[g][xh]) {
        size_t b = lay_hyph_xslot[g][xh] - 1, q = b, k = 0;
        int same = 1;
        for (; excs[q] != ' ' && same; q++) {
            if (excs[q] == '-') continue;
            if (k >= n || excs[q] != w[k]) same = 0;
            else k++;
        }
        if (same && k == n) {
            k = 0;
            for (q = b; excs[q] != ' '; q++) {
                if (excs[q] != '-') k++;
                else if (k >= 2 && k + 3 <= n) ok[k] = 1;
            }
            return;
        }
        xh = (xh + 1) & (LAY_HYPH_XSLOTS - 1);
    }
    char s0[66];
    s0[0] = '.';
    memcpy(s0 + 1, w, n);
    s0[n + 1] = '.';
    size_t m = n + 2, longest = g ? AMB_HYPH_GB_LONGEST : AMB_HYPH_LONGEST;
    uint8_t vals[68];
    memset(vals, 0, sizeof vals);
    for (size_t a = 0; a < m; a++)
        for (size_t k = 1; k <= longest && a + k <= m; k++) {
            uint32_t h = lay_hyph_hash(s0 + a, k);
            while (lay_hyph_slot[g][h]) {
                uint32_t off = lay_hyph_slot[g][h] - 1;
                if (memcmp(pats + off, s0 + a, k) == 0 && lay_digit(pats[off + k])) {
                    for (size_t q = 0; q <= k; q++) {
                        uint8_t v = (uint8_t)(pats[off + k + q] - '0');
                        if (v > vals[a + q]) vals[a + q] = v;
                    }
                    break;
                }
                h = (h + 1) & (LAY_HYPH_SLOTS - 1);
            }
        }
    /* vals[j]: the value before s0[j]; letter i of w is s0[i + 1] */
    for (size_t q = 2; q + 3 <= n; q++) ok[q] = (uint8_t)(vals[q + 1] & 1);
}

/* a code point Blink's WordToHyphenate takes off a word's end: a space separator, punctuation or a control
   (general category Zs, P or Cc; past Latin-1, the common ones) */
static int lay_hyph_trail(uint32_t cp) {
    if (cp < 0x80) {
        if (cp <= 0x20 || cp == 0x7F) return 1;
        static const char *const punct = "!\"#%&'()*,-./:;?@[\\]_{}";
        for (size_t q = 0; punct[q]; q++) if ((uint32_t)(unsigned char)punct[q] == cp) return 1;
        return 0;
    }
    if (cp <= 0xA1 || cp == 0xA7 || cp == 0xB6 || cp == 0xB7 || cp == 0xBF) return 1;
    uint32_t g = (amb_lb_props(cp) & AMB_LBP_GC) >> 8;
    if (g == AMB_LBG_PI || g == AMB_LBG_PF) return 1;
    return (cp >= 0x2000 && cp <= 0x200A) || (cp >= 0x2010 && cp <= 0x2027) || cp == 0x202F || (cp >= 0x2030 && cp <= 0x2043) ||
           cp == 0x205F || (cp >= 0x3000 && cp <= 0x3003) || (cp >= 0x3008 && cp <= 0x3011);
}

/* the word a piece of text tw[0, n) is hyphenated as (Blink's HyphenationMinikin::WordToHyphenate): past the
   spaces, opening punctuation and quotation marks before it (class OP, QU) and the spaces, punctuation and
   controls after it, its bytes [*a, *z), its letters in lower case into lw (64 bytes); 0 when Chromium does not
   hyphenate it: a letter of it not one of a to z (en-US's alphabet: a soft hyphen in it too), 5 to 62 letters
   not, or a capital first (Blink hyphenates no capitalized word of English) */
static int lay_hyph_word(const char *tw, size_t n, uint32_t *a, uint32_t *z, char *lw) {
    size_t b = 0;
    while (b < n) {
        size_t q = b;
        uint32_t cp = (unsigned char)tw[q] < 0x80 ? (uint32_t)(unsigned char)tw[q++] : amb_utf8_next(tw, n, &q);
        uint32_t cls = amb_lb_props(cp) & AMB_LBP_CLASS;
        if (!(cp == ' ' || cp == '\t' || cp == '\n' || cp == 0xA0 || cls == AMB_LBC_OP || cls == AMB_LBC_QU)) break;
        b = q;
    }
    size_t e = n;
    while (e > b) {
        size_t q = e - 1;
        while (q > b && ((unsigned char)tw[q] & 0xC0) == 0x80) q--;
        size_t q2 = q;
        uint32_t cp = (unsigned char)tw[q2] < 0x80 ? (uint32_t)(unsigned char)tw[q2] : amb_utf8_next(tw, n, &q2);
        if (!lay_hyph_trail(cp)) break;
        e = q;
    }
    if (e - b < 5 || e - b > 62 || (tw[b] >= 'A' && tw[b] <= 'Z')) return 0;
    for (size_t q = b; q < e; q++) {
        char c = tw[q];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c < 'a' || c > 'z') return 0;
        lw[q - b] = c;
    }
    *a = (uint32_t)b;
    *z = (uint32_t)e;
    return 1;
}

static double lay_hyphen_width(LCTX, lay_piece p);
static double lay_part_width(LCTX, lay_piece p, uint32_t a, uint32_t b);
/* where a word piece p can be broken by a hyphen (hyphens: auto), the part before it and the hyphen fitting in
   room: the last of Liang's points that fits (Blink's LastHyphenLocation), else, where first (the word begins its
   line), the first (FirstHyphenLocation: the line overflows by less); the bytes its part keeps (*keep), where its
   rest begins (*rest) and the part's width (*pw); 0 when it has no such place. A part after the first is a word of
   its own (lay_hyph_apply), its points its own, as Blink hyphenates the rest of a word a line begins in. g: the
   dictionary (lay_hyph_dict) */
static int lay_hyph_split(LCTX, const vector<lay_piece> *ps, size_t k, int g, double room, int first, uint32_t *keep, uint32_t *rest, double *pw) {
    lay_piece p = at(*ps, k);
    vector<char> tw;
    lay_lb_bytes(LARGS, p, &tw);
    char lw[64];
    uint32_t a, z;
    if (len(tw) != p.len || !lay_hyph_word(data(tw), len(tw), &a, &z, lw)) return 0;
    uint8_t ok[64];
    lay_hyph_points(g, lw, z - a, ok);
    vector<uint32_t> cuts;
    for (uint32_t q = 1; q < z - a; q++) if (ok[q]) push_back(cuts, a + q);
    double hw = lay_hyphen_width(LARGS, p);
    for (size_t c = len(cuts); c >= 1; c--) {
        uint32_t kb = at(cuts, c - 1);
        double w = lay_part_width(LARGS, p, 0, kb) + hw;
        if (w <= room + LAY_FIT || (first && c == 1)) { *keep = kb; *rest = kb; *pw = w; return 1; }
    }
    return 0;
}

/* hyphens: auto's parts of word piece k for the min-content width (Blink's FastMinTextContext::AddHyphenated: each
   part between the points of the whole word, all but the last with a hyphen): the first's width into *first, the
   widest of the others but the last into *mid, the last's into *last; 0 when the word has no point (or is in a
   language not hyphenated here) */
static int lay_hyph_parts(LCTX, const vector<lay_piece> *ps, size_t k, double *first, double *mid, double *last) {
    lay_piece p = at(*ps, k);
    if (p.kind != P_WORD || LAY_X(p.elem).hyph != 2) return 0;
    int g = lay_hyph_dict(d, lay_dom(gx, p.elem));
    if (g < 0) return 0;
    vector<char> tw;
    lay_lb_bytes(LARGS, p, &tw);
    char lw[64];
    uint32_t a, z;
    if (len(tw) != p.len || !lay_hyph_word(data(tw), len(tw), &a, &z, lw)) return 0;
    uint8_t ok[64];
    lay_hyph_points(g, lw, z - a, ok);
    vector<uint32_t> cuts;
    push_back(cuts, 0);
    for (uint32_t q = 1; q < z - a; q++) if (ok[q]) push_back(cuts, a + q);
    if (len(cuts) < 2) return 0;
    push_back(cuts, (uint32_t)len(tw));
    double hw = lay_hyphen_width(LARGS, p);
    *mid = 0;
    for (size_t c = 0; c + 1 < len(cuts); c++) {
        double w = lay_part_width(LARGS, p, at(cuts, c), at(cuts, c + 1));
        if (c + 2 == len(cuts)) *last = w;
        else if (c == 0) *first = w + hw;
        else if (w + hw > *mid) *mid = w + hw;
    }
    return 1;
}

/* a word piece broken at a hyphenation point: its part (keep bytes, pw wide, the hyphen
   after it) and its rest, the next piece */
static void lay_hyph_apply(LCTX, vector<lay_piece> *ps, size_t k, uint32_t keep, uint32_t rest, double pw) {
    lay_piece p = at(*ps, k), q = p;
    p.len = keep;
    p.w = pw;
    p.hy = 1;
    q.off = p.off + rest;
    q.len = at(*ps, k).len - rest;
    if (q.tt == TT_CAPITALIZE) q.tt |= TT_MID;
    /* the rest's width: its own letters */
    vector<char> raw;
    if (q.node & LAY_GEN) { amb_span g = at(*gx, q.node & LAY_GIDX).text; append_range(raw, text_ptr(t, g) + q.off, q.len); }
    else { amb_span sp = d->node[q.node].data; append_range(raw, text_ptr(d, sp) + q.off, q.len); }
    vector<char> tw;
    if (q.tt) lay_transform(data(raw), len(raw), q.tt, &tw);
    else append_range(tw, data(raw), len(raw));
    lay_strip_soft(&tw);
    q.w = amb_text_width(f, q.face, q.size, data(tw), len(tw)) + q.ls * (double)lay_cps(data(tw), len(tw));
    (*ps)[k] = p;
    push_back(ps, q);
    for (size_t z = len(*ps) - 1; z > k + 1; z--) (*ps)[z] = at(*ps, z - 1);
    (*ps)[k + 1] = q;
}

/* the word a line [start, end) ends in, but for the inline elements' edges after it, when it ends in a soft hyphen it
   breaks after (LBF_SHY); len(ps) for none */
static size_t lay_shy_end(const vector<lay_piece> *ps, size_t start, size_t end) {
    size_t q = end;
    while (q > start && (at(*ps, q - 1).kind == P_CLOSE || at(*ps, q - 1).kind == P_OPEN)) q--;
    if (q > start && at(*ps, q - 1).kind == P_WORD && (at(*ps, q - 1).lbf & LBF_SHY)) return q - 1;
    return len(*ps);
}
/* a chunk [k, c) of a paragraph's pieces is its last word but not its only one: nothing after it but spaces,
   forced breaks and inline elements' edges, something before it (Blink's ShouldHyphenate leaves such a word
   unhyphenated when the line has something before it; a wbr is text, U+200B, a float or an absolutely positioned
   box U+FFFC) */
static int lay_hyph_blank(const dom_document *d, lay_piece p) {
    return p.kind == P_SPACE || p.kind == P_BREAK || p.kind == P_CLOSE || (p.kind == P_OPEN && !lay_is_named(d, p.node, "wbr"));
}
static int lay_hyph_last(const dom_document *d, const vector<lay_piece> *ps, size_t k, size_t c) {
    for (size_t q = c; q < len(*ps); q++) if (!lay_hyph_blank(d, at(*ps, q))) return 0;
    for (size_t q = 0; q < k; q++) if (!lay_hyph_blank(d, at(*ps, q))) return 1;
    return 0;
}

/* the width of the hyphen a word broken at a soft hyphen ends in, its letter-spacing after it */
static double lay_hyphen_width(LCTX, lay_piece p) { return amb_text_width(f, p.face, p.size, "-", 1) + p.ls; }

/* a word piece's width as laid out, measured alone: its bytes [a, b) (text-transform's letters, soft hyphens out),
   letter-spacing after each code point */
static double lay_part_width(LCTX, lay_piece p, uint32_t a, uint32_t b) {
    vector<char> raw;
    lay_src_bytes(LARGS, p.node, p.off + a, b - a, &raw);
    vector<char> tw;
    if (p.tt) lay_transform(data(raw), len(raw), p.tt, &tw);
    else append_range(tw, data(raw), len(raw));
    lay_strip_soft(&tw);
    return amb_text_width(f, p.face, p.size, data(tw), len(tw)) + p.ls * (double)lay_cps(data(tw), len(tw));
}

/* word piece q of ps cut at its byte a: its part before (w1 wide) and its rest, the next piece, measured alone */
static void lay_piece_cut(LCTX, vector<lay_piece> *ps, size_t q, uint32_t a, double w1) {
    lay_piece p = at(*ps, q), r = p;
    p.len = a;
    p.w = w1;
    p.lbf = (uint8_t)(p.lbf & ~LBF_SHY);
    r.off = p.off + a;
    r.len = at(*ps, q).len - a;
    r.bb = 0;
    if ((r.tt & ~TT_MID) == TT_CAPITALIZE) r.tt |= TT_MID;
    r.w = lay_part_width(LARGS, r, 0, r.len);
    (*ps)[q] = p;
    push_back(ps, r);
    for (size_t z = len(*ps) - 1; z > q + 1; z--) (*ps)[z] = at(*ps, z - 1);
    (*ps)[q + 1] = r;
}

/* overflow-wrap's emergency break of a line's first chunk, the pieces [k, c) of ps, too wide for room (Blink's
   overflowing line broken again with a break at every grapheme cluster: LineBreaker's override_break_anywhere_): the
   chunk cut after the most of its graphemes that fit, one at least, a word cut there into two pieces; the line's end
   (a piece's index) into *cut and the width it keeps into *kw. 0 when the chunk has no word to cut */
static int lay_emerg_split(LCTX, vector<lay_piece> *ps, size_t k, size_t c, double room, size_t *cut, double *kw) {
    double acc = 0;
    for (size_t q = k; q < c; q++) {
        lay_piece p = at(*ps, q);
        if (p.kind != P_WORD) { acc += p.w; continue; }
        if (acc + p.w <= room + LAY_FIT) { acc += p.w; continue; }
        /* the piece's graphemes: the longest run of them from its start that fits */
        vector<char> raw;
        lay_src_bytes(LARGS, p.node, p.off, p.len, &raw);
        vector<uint32_t> cps, cpo;
        size_t z = 0;
        while (z < len(raw)) {
            push_back(cpo, (uint32_t)z);
            push_back(cps, amb_utf8_next(data(raw), len(raw), &z));
        }
        push_back(cpo, (uint32_t)len(raw));
        vector<uint8_t> gb;
        amb_lb_graphemes(&cps, &gb);
        uint32_t best = 0;
        double bw = 0;
        for (size_t g = 1; g < len(cps); g++) {
            if (!at(gb, g)) continue;
            double w = lay_part_width(LARGS, p, 0, at(cpo, g));
            if (acc + w > room + LAY_FIT) {
                if (!best && acc == 0) { best = at(cpo, g); bw = w; }
                break;
            }
            best = at(cpo, g);
            bw = w;
        }
        if (!best) {
            /* not one grapheme of it fits after what does: the line ends before it */
            if (q == k) return 0;
            *cut = q;
            *kw = acc;
            return 1;
        }
        lay_piece_cut(LARGS, ps, q, best, bw);
        *cut = q + 1;
        *kw = acc + bw;
        return 1;
    }
    return 0;
}

/* the end of the first letter of text s[0, n): the punctuation before it and the letter
   (a code point of UTF-8); 0 when there is none */
static size_t lay_letter_end(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        int punct = c == '"' || c == '\'' || c == '(' || c == '[' || c == '{' || c == '!' || c == '?' || c == '*' || c == '#' || c == '%' ||
                    c == '&' || c == '@' || c == '/' || c == '-' || c == '.' || c == ',' || c == ':' || c == ';';
        /* the quotes and guillemets: U+00AB, U+00BB, U+2018 to U+201F */
        int upunct = (c == 0xC2 && i + 1 < n && ((unsigned char)s[i + 1] == 0xAB || (unsigned char)s[i + 1] == 0xBB)) ||
                     (c == 0xE2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] >= 0x98 && (unsigned char)s[i + 2] <= 0x9F);
        if (punct) { i++; continue; }
        if (upunct) { i += c == 0xC2 ? 2 : 3; continue; }
        size_t e = i + 1;
        while (e < n && ((unsigned char)s[e] & 0xC0) == 0x80) e++;
        return e;
    }
    return 0;
}

/* ::first-letter: the run's first letter (with the punctuation before it), a piece of its
   own in the pseudo-element's style g (the gen index gi), its box's edges about it */
static void lay_first_letter(LCTX, vector<lay_piece> *ps, uint32_t gi, double cw) {
    size_t k = 0;
    while (k < len(*ps) && at(*ps, k).kind != P_WORD) {
        int kd = at(*ps, k).kind;
        if (kd == P_ATOMIC || kd == P_BREAK || (kd == P_SPACE && at(*ps, k).keep)) return;
        k++;
    }
    if (k >= len(*ps)) return;
    lay_style g;
    lay_style_of(LARGS, LAY_GEN | gi, &g);
    lay_piece p = at(*ps, k);
    vector<char> wt;
    lay_piece_text(t, d, gx, ps, k, k + 1, &wt);
    size_t cut = lay_letter_end(data(wt), len(wt));
    if (cut == 0 || cut > p.len) return;
    lay_piece a = p, r = p;
    lay_piece_style(&a, &g);
    a.len = (uint32_t)cut;
    a.w = amb_text_width(f, a.face, a.size, data(wt), cut) + a.ls * (double)lay_cps(data(wt), cut);
    r.off = p.off + (uint32_t)cut;
    r.len = p.len - (uint32_t)cut;
    if (r.tt == TT_CAPITALIZE) r.tt |= TT_MID;
    r.w = amb_text_width(f, r.face, r.size, data(wt) + cut, len(wt) - cut) + r.ls * (double)lay_cps(data(wt) + cut, len(wt) - cut);
    vector<lay_piece> out;
    for (size_t q = 0; q < k; q++) push_back(out, at(*ps, q));
    lay_piece o;
    memset(&o, 0, sizeof o);
    lay_piece_style(&o, &g);
    o.node = LAY_GEN | gi;
    o.elem = LAY_GEN | gi;
    o.kind = P_OPEN;
    o.bg = g.bg;
    o.w = lay_res(st, g.m[3], cw) + g.b[3] + lay_res(st, g.p[3], cw);
    push_back(out, o);
    push_back(out, a);
    o.kind = P_CLOSE;
    o.w = lay_res(st, g.p[1], cw) + g.b[1] + lay_res(st, g.m[1], cw);
    push_back(out, o);
    if (r.len > 0) push_back(out, r);
    for (size_t q = k + 1; q < len(*ps); q++) push_back(out, at(*ps, q));
    clear(ps);
    append_range(ps, data(out), len(out));
}

/* ::first-line: the pieces in the style of the pseudo-element fls where they have their
   block's (bs) -- what they inherit -- measured again, as their runs shape */
static void lay_first_line_pieces(LCTX, vector<lay_piece> *ps, const lay_style *bs, const lay_style *fls) {
    for (size_t k = 0; k < len(*ps); k++) {
        lay_piece p = at(*ps, k);
        if (p.color == bs->color) p.color = fls->color;
        if (p.fill == bs->fill) p.fill = fls->fill;
        if (p.face != bs->face || p.font != bs->font || p.size != bs->fs || p.ls != bs->ls || p.wsp != bs->wsp) { (*ps)[k] = p; continue; }
        double old_space = amb_advance(f, p.face, ' ', p.size) + p.ls + p.wsp;
        p.face = (uint8_t)fls->face;
        p.font = fls->font;
        p.italic = (uint8_t)fls->italic;
        p.size = fls->fs;
        p.lh = fls->lh;
        p.asc = fls->asc;
        p.desc = fls->desc;
        p.lhn = (uint8_t)fls->lhn;
        p.ls = fls->ls;
        p.wsp = fls->wsp;
        if (fls->tt && p.kind == P_WORD) p.tt = (uint8_t)fls->tt;
        double space = amb_advance(f, p.face, ' ', p.size) + p.ls + p.wsp;
        if (p.kind == P_SPACE && lay_tabbed(LARGS, p)) p.w = old_space > 0 ? p.w * space / old_space : space;
        (*ps)[k] = p;
    }
    lay_shape_pieces(LARGS, ps, 0);
}

/* the lines of the inline run [first, first + n) of items in box b, w wide; returns their height */
static double lay_lines(LCTX, uint32_t b, uint32_t container, const vector<uint32_t> *items, uint32_t first, uint32_t n, double w, double ox,
                        double oy) {
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, container, items, first, n, w, &ps);
    if (!lay_run_shows(&ps)) {
        lay_run_oof(LARGS, b, &ps, w, ox, oy, 0);
        return 0;
    }
    lay_style bs;
    lay_style_of(LARGS, container, &bs);
    uint32_t ln0 = t->box[b].last_line;     /* the box's last line before these */
    /* the block's ::first-letter and ::first-line, in its first run: the first line laid
       out in the pseudo-element's style (its strut too), the rest as they were */
    lay_style fls, bs1 = bs;
    vector<lay_piece> rest;
    int has_fl = 0, want_fl = 0;
    if (first == 0 && !(container & LAY_GEN) && container < len(*sx) && (at(*sx, container) & LAY_HAS_OTHER)) {
        int64_t gl = lay_gen_find(gx, container, CSS_PSEUDO_FIRST_LETTER);
        if (gl >= 0) lay_first_letter(LARGS, &ps, (uint32_t)gl, w);
        want_fl = lay_pseudo_style(LARGS, container, CSS_PSEUDO_FIRST_LINE, &fls);
    }
    /* bidirectional text (UAX #9), when the paragraph is right to left, a word asks for it
       (lay_bidi_hot), or an inline element embeds, isolates or overrides right to left: the
       paragraph's units and levels; its level its direction's, or (unicode-bidi: plaintext,
       dir=auto) its first strong character's, start and end then aligned by it */
    vector<lay_bu> bu;
    int para = bs.rtl, needs = bs.rtl;
    for (size_t q = 0; q < len(ps) && !needs; q++) {
        lay_piece p = at(ps, q);
        if (p.rtl || (p.kind == P_OPEN && LAY_X(p.node).ubidi && LAY_X(p.node).rtl)) needs = 1;
    }
    if (needs) {
        lay_bidi_units(LARGS, &ps, &bu);
        if (LAY_X(container).ubidi == UB_PLAINTEXT || lay_dir_auto(d, container)) {
            int fs = lay_bidi_first_strong(&bu, 0, 0);
            if (fs >= 0 && fs != para) {
                para = fs;
                int as = LAY_X(container).astart;
                if (as) bs.align = (as == 1) == (para == 1) ? AL_RIGHT : AL_LEFT;
                bs1.align = bs.align;
            }
        }
        lay_bidi_resolve(&bu, para);
        /* measured again, each run of one direction */
        lay_shape_dirs(&ps, &bu);
        lay_shape_pieces(LARGS, &ps, 0);
    }
    if (want_fl) {
        has_fl = 1;
        append_range(rest, data(ps), len(ps));
        lay_first_line_pieces(LARGS, &ps, &bs, &fls);
        bs1.lh = fls.lh;
        bs1.asc = fls.asc;
        bs1.desc = fls.desc;
    }
    vector<lay_open> open;
    vector<uint32_t> later;           /* floats too wide for the line they are met on: placed below it */
    size_t i = 0, np = len(ps);
    double y = 0;
    /* text-indent: the first line of the block's first run, from its start edge; a list item's marker inside it
       before that line's content */
    double indent = first == 0 ? lay_res(st, bs.indent, w) : 0;
    if (first == 0 && bs.display == D_LIST_ITEM) indent += lay_marker_inside(LARGS, container, &bs);
    /* the first line's strut, when an inside marker's text in it reaches past it (its font's and its fallback faces'
       line): mgrow, the line's extent above and below the baseline in ma and mb */
    double ma = 0, mb = 0;
    int mgrow = first == 0 && bs.display == D_LIST_ITEM && bs.list == 4 && bs.list_inside &&
                lay_marker_strut(LARGS, container, &bs, has_fl ? &bs1 : &bs, &ma, &mb);
    /* a list item's own lines (not an anonymous block's in it): the line height quirk counts its line
       height on each (measured) */
    int marker = bs.display == D_LIST_ITEM && !(container & LAY_GEN) && t->box[b].node.index == container;
    while (i < np) {
        while (i < np && at(ps, i).kind == P_SPACE && !at(ps, i).keep) i++;
        if (i >= np) break;
        /* the start edges of the cloned elements the line continues (box-decoration-break: clone), before its content,
           as text-indent is (a bidi paragraph's lines keep their elements sliced) */
        double cstart = 0;
        if (!len(bu))
            for (size_t q = 0; q < len(open); q++) cstart += lay_clone_edge(LARGS, at(open, q).el, 0, w);
        indent += cstart;
        /* the line's band beside the floats, lower when its first chunk does not fit */
        double lx = 0, lw = w;
        if (len(*fl)) {
            double need = indent + lay_first_chunk(&ps, i);
            for (int guard = 0; guard < 200; guard++) {
                double l, r, next;
                lay_band(fl, ox, w, oy + y, bs.lh, &l, &r, &next);
                lx = l;
                lw = r - l;
                if (lw >= need - 0.01 || next < 0) break;
                y = next - oy;
            }
        }
        size_t start = i, j = i, end = i;
        double x = indent;
        int forced = 0;
        /* the line as it stood at the last break opportunity it went past: where a line ending in a soft hyphen whose
           hyphen does not fit breaks instead (Blink's retry with the hyphen's width less) */
        size_t bk_j = 0, bk_end = 0;
        double bk_x = 0;
        int ends_at_break = 0;
        while (j < np) {
            if (at(ps, j).kind == P_BREAK) { end = j; j++; forced = 1; break; }
            if (at(ps, j).kind == P_FLOAT) {
                /* a float met in the line: placed at its top if it fits beside what the line
                   holds, the line then narrowed; else below the line */
                uint32_t fel = at(ps, j).node;
                uint32_t fb = lay_float_box(LARGS, b, fel, w, lay_line_pch);
                double mw = t->box[fb].ml + t->box[fb].bl + t->box[fb].pl + t->box[fb].w + t->box[fb].pr + t->box[fb].br + t->box[fb].mr;
                if (mw <= lw - x + 0.01 || x <= indent) {
                    lay_float_place(LARGS, fb, fel, w, ox, oy, y);
                    double l, r, next;
                    lay_band(fl, ox, w, oy + y, bs.lh, &l, &r, &next);
                    lx = l;
                    lw = r - l;
                } else {
                    push_back(later, fb);
                    push_back(later, fel);
                }
                end = j + 1;
                j++;
                continue;
            }
            /* the spaces before the next chunk, and the chunk */
            size_t k = j;
            double sw = 0;
            int breakable = 0;
            while (k < np && at(ps, k).kind == P_SPACE) { sw += at(ps, k).w; if (at(ps, k).brk) breakable = 1; k++; }
            if (k >= np || at(ps, k).kind == P_BREAK) { end = k; j = k; continue; }
            size_t c = k;
            double cw = 0;
            int atomic_edge = at(ps, k).kind == P_ATOMIC || (k > start && at(ps, k - 1).kind == P_ATOMIC);
            /* a chunk: to the next space, forced break or float, atomic box's edge, or break opportunity
               (lay_break_pieces) */
            while (c < np && at(ps, c).kind != P_SPACE && at(ps, c).kind != P_BREAK && at(ps, c).kind != P_FLOAT) {
                if (c > k && (at(ps, c).kind == P_ATOMIC || at(ps, c - 1).kind == P_ATOMIC || (at(ps, c).bb & AMB_BRK_LINE))) break;
                cw += at(ps, c).w;
                c++;
            }
            int can_break = (breakable || atomic_edge || (at(ps, k).bb & AMB_BRK_LINE)) && end > start;
            /* the ideographic spaces the chunk ends in, which hang where the line ends after them (looked for when the
               chunk does not fit without them) */
            double hang = 0;
            if (x + sw + cw > lw + LAY_FIT)
                for (size_t q = c; q > k && (at(ps, q - 1).kind == P_CLOSE || (at(ps, q - 1).kind == P_WORD && (at(ps, q - 1).lbf & LBF_HANG))); q--)
                    if (at(ps, q - 1).kind == P_WORD) hang += at(ps, q - 1).w;
            /* the chunk's one word, the inline elements' edges before it (none: it has another word, or none), when
               it does not fit */
            size_t hw_k = np;
            double hw_pre = 0;
            if (!has_fl && x + sw + cw > lw + LAY_FIT)
                for (size_t q = k; q < c; q++) {
                    int kd = at(ps, q).kind;
                    if (kd == P_WORD) {
                        if (hw_k < np) { hw_k = np; break; }
                        hw_k = q;
                    } else if (kd != P_OPEN && kd != P_CLOSE) { hw_k = np; break; }
                    else if (hw_k == np) hw_pre += at(ps, q).w;
                }
            if (hw_k < np) {
                /* a word too wide for what is left: broken by a hyphen where Liang's points let it (hyphens: auto, in
                   English, by the patterns Chromium has for it; a soft hyphen is a break opportunity of its own), but
                   for the paragraph's last word after the line's first (Blink's ShouldHyphenate) */
                int hyph = LAY_X(at(ps, hw_k).elem).hyph;
                int dict = hyph == 2 ? lay_hyph_dict(d, lay_dom(gx, at(ps, hw_k).elem)) : -1;
                uint32_t keep, rst;
                double pw;
                if (dict >= 0 && (end == start || !lay_hyph_last(d, &ps, k, c)) &&
                    lay_hyph_split(LARGS, &ps, hw_k, dict, lw - x - sw - hw_pre, end == start, &keep, &rst, &pw)) {
                    lay_hyph_apply(LARGS, &ps, hw_k, keep, rst, pw);
                    np = len(ps);
                    x += sw + hw_pre + pw;
                    end = hw_k + 1;
                    j = hw_k + 1;
                    break;
                }
            }
            if (can_break && x + sw + cw - hang > lw + LAY_FIT) { j = k; ends_at_break = 1; break; }
            int emerg = 0;
            if (!can_break && end == start && x + sw + cw - hang > lw + LAY_FIT)
                for (size_t q = k; q < c && !emerg; q++) emerg = at(ps, q).kind == P_WORD && (at(ps, q).lbf & LBF_EMERG);
            if (emerg) {
                /* the line's first chunk too wide for it, under overflow-wrap (or word-break: break-word): cut after the
                   most of its graphemes that fit, one at least (Blink's line broken again at every grapheme) */
                size_t cut;
                double kw;
                if (lay_emerg_split(LARGS, &ps, k, c, lw - x - sw, &cut, &kw)) {
                    np = len(ps);
                    x += sw + kw;
                    end = cut;
                    j = cut;
                    break;
                }
            }
            if (can_break) { bk_j = k; bk_end = end; bk_x = x; }
            x += sw + cw;
            end = c;
            j = c;
        }
        /* a line ending at a break after a soft hyphen: its hyphen drawn, the line's width with it; where it does not fit,
           the line broken at the opportunity before (its own soft hyphen's hyphen then shown, fitting or not) */
        if (ends_at_break) {
            size_t q = lay_shy_end(&ps, start, end);
            if (q < np) {
                double hw = lay_hyphen_width(LARGS, at(ps, q));
                if (x + hw > lw + LAY_FIT && bk_end > start) {
                    end = bk_end;
                    j = bk_j;
                    x = bk_x;
                    q = lay_shy_end(&ps, start, end);
                    hw = q < np ? lay_hyphen_width(LARGS, at(ps, q)) : 0;
                }
                if (q < np) {
                    lay_piece hp = at(ps, q);
                    hp.hy = 2;
                    hp.w += hw;
                    ps[q] = hp;
                    x += hw;
                }
            }
        }
        if (j >= np && !forced) end = np;
        int last = j >= np || forced;
        if (start == 0 && mgrow) {
            lay_style bsm = has_fl ? bs1 : bs;
            bsm.asc = ma;
            bsm.desc = mb;
            bsm.lh = ma + mb;
            y += lay_line(LARGS, b, &bsm, &ps, start, end, lx, lw, y, !last, indent, &open, &bu, para, container, marker);
        } else if (has_fl) y += lay_line(LARGS, b, &bs1, &ps, start, end, lx, lw, y, !last, indent, &open, &bu, para, container, marker);
        else y += lay_line(LARGS, b, &bs, &ps, start, end, lx, lw, y, !last, indent, &open, &bu, para, container, marker);
        indent = 0;
        i = j;
        /* the floats that waited for the line's end */
        for (size_t q = 0; q + 1 < len(later); q += 2) lay_float_place(LARGS, at(later, q), at(later, q + 1), w, ox, oy, y);
        clear(later);
        if (has_fl) {
            /* past the first line: the pieces as their own style has them */
            for (size_t q = i; q < np; q++) ps[q] = at(rest, q);
            has_fl = 0;
        }
    }
    /* text-overflow: ellipsis, in a box that clips its overflow: each line past the content box's edge cut, an
       ellipsis after what stays */
    if (LAY_X(container).ellipsis && LAY_X(container).clip)
        for (uint32_t ln = ln0 ? t->line[ln0].next : t->box[b].first_line; ln; ln = t->line[ln].next)
            lay_ellipsize(LARGS, ln, w - t->line[ln].x, &bs, 0);
    return y;
}
/* a line cut for an ellipsis (CSS Overflow 3, text-overflow; the legacy line clamp): when what it holds reaches past
   limit (or always, when force), its text ends at the last character that leaves the ellipsis room before limit, what is
   past that is dropped, and the ellipsis, in the block's style bs, follows. Fragments' places are the line's */
static void lay_ellipsize(LCTX, uint32_t ln, double limit, const lay_style *bs, int force) {
    double right = 0;
    for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next)
        if (t->frag[fr].kind != LAYOUT_FRAG_KIND_INLINE && t->frag[fr].x + t->frag[fr].w > right) right = t->frag[fr].x + t->frag[fr].w;
    if (!force && right <= limit + 0.01) return;
    const char *dots = "\xe2\x80\xa6";
    double ew = amb_text_width(f, bs->face, bs->fs, dots, 3);
    double room = limit - ew, end = 0;
    uint32_t last = 0;
    for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
        layout_frag_kind k = t->frag[fr].kind;
        if (k == LAYOUT_FRAG_KIND_INLINE || k == LAYOUT_FRAG_KIND_MARKER) continue;
        double x0 = t->frag[fr].x, x1 = x0 + t->frag[fr].w;
        if (x1 <= room + 0.01) { if (x1 > end) end = x1; last = fr; continue; }
        if (k != LAYOUT_FRAG_KIND_TEXT && k != LAYOUT_FRAG_KIND_GENERATED) { t->frag[fr].w = 0; continue; }
        /* its text to the last character that fits */
        amb_span sp = t->frag[fr].text;
        const char *tx = text_ptr(t, sp);
        size_t keep = 0, i = 0;
        double kw = 0;
        while (i < sp.len && x0 < room) {
            size_t j = i + 1;
            while (j < sp.len && ((uint8_t)tx[j] & 0xC0) == 0x80) j++;
            double cw = amb_text_width(f, t->frag[fr].face, t->frag[fr].size, tx, j) + t->frag[fr].spacing * (double)lay_cps(tx, j);
            if (x0 + cw > room + 0.01) break;
            keep = j;
            kw = cw;
            i = j;
        }
        if (keep < sp.len) {
            /* cut: its glyphs paint's, of what stays shaped (amb_text_width's) */
            amb_span none = { 0, 0 };
            t->frag[fr].glyphs = none;
        }
        t->frag[fr].text.len = (uint32_t)keep;
        t->frag[fr].w = kw;
        if (keep && x0 + kw > end) { end = x0 + kw; last = fr; }
    }
    /* the ellipsis, on the line's baseline, after what stays */
    double base = last ? t->frag[last].baseline : t->line[ln].baseline;
    amb_span dsp = text_add(t, dots, 3);
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    t->frag[fr].kind = LAYOUT_FRAG_KIND_GENERATED;
    if (last) t->frag[fr].node = t->frag[last].node;
    t->frag[fr].text = dsp;
    t->frag[fr].x = end;
    t->frag[fr].y = base - bs->asc;
    t->frag[fr].w = ew;
    t->frag[fr].h = bs->asc + bs->desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (uint8_t)bs->face;
    t->frag[fr].orient = LAY_NOCOMB(bs->orient);
    if (bs->font) t->frag[fr].font = resource_handle(nt, bs->font);
    t->frag[fr].size = bs->fs;
    t->frag[fr].color = bs->color;
    t->frag[fr].fill = bs->fill;
    t->frag[fr].hidden = bs->hidden != 0;
    frags_append(t, ln, fr);
}
/* the nth line (from 1) of b's block flow -- its own lines, then each in-flow child's, in order: *n counts down to 0
   at it; its bottom from b's content top in *bottom, its box in *owner; 0 when there are fewer */
static uint32_t lay_nth_line(layout_tree *t, uint32_t b, long *n, double oy, double *bottom, uint32_t *owner) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) {
        if (--(*n) == 0) { *bottom = oy + t->line[ln].y + t->line[ln].h; *owner = b; return ln; }
    }
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        if (t->box[c].position == LAYOUT_POSITION_ABSOLUTE || t->box[c].position == LAYOUT_POSITION_FIXED ||
            t->box[c].float_side != LAYOUT_FLOAT_SIDE_UNFLOATED) continue;
        uint32_t ln = lay_nth_line(t, c, n, oy + t->box[c].y, bottom, owner);
        if (ln) return ln;
    }
    return 0;
}
/* -webkit-line-clamp: a box the cascade computed from a vertical -webkit-box (flow-root) or -webkit-inline-box
   (inline-block), its height auto, as tall as its first N lines; when more follow, the Nth ends with an ellipsis in
   s's font (Chromium's legacy clamp) */
static void lay_clamp(LCTX, uint32_t b, uint32_t el, const lay_style *s) {
    long n = LAY_X(el).clamp, more = n + 1;
    if (n <= 0) return;
    double bottom = 0, b2 = 0;
    uint32_t owner = 0, o2 = 0;
    uint32_t ln = lay_nth_line(t, b, &n, 0, &bottom, &owner);
    if (ln && lay_nth_line(t, b, &more, 0, &b2, &o2)) {
        t->box[b].h = bottom;
        lay_ellipsize(LARGS, ln, t->box[owner].w - t->line[ln].x, s, 1);
    }
}

/* ---- list markers ---- */

static size_t lay_roman(long n, int upper, char *out, size_t cap) {
    static const long vals[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const syms[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    size_t k = 0;
    if (n <= 0 || n >= 4000) return (size_t)snprintf(out, cap, "%ld", n);
    for (int i = 0; i < 13; i++)
        while (n >= vals[i] && k + 3 < cap) {
            for (size_t j = 0; syms[i][j]; j++) out[k++] = upper ? (char)(syms[i][j] - 32) : syms[i][j];
            n -= vals[i];
        }
    out[k] = 0;
    return k;
}

/* where the list item b's marker is set (Chromium's content alignment baseline): its first line, however deep through the
   blocks of its flow (ln, and its box in owner); a flex or grid container met first its own first baseline (ln 0); y the
   baseline, from b's content box's top, clip whether a box on the way clips its overflow. 0 when there is none */
static int lay_marker_spot(const layout_tree *t, uint32_t b, uint32_t *owner, uint32_t *ln, double *y, int *clip, int depth) {
    if (t->box[b].first_line) {
        *owner = b;
        *ln = t->box[b].first_line;
        *y = t->line[*ln].y + t->line[*ln].baseline;
        return 1;
    }
    if (depth > 64) return 0;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) {
        int k = t->box[c].kind;
        if (k == LAYOUT_BOX_KIND_INLINE_BLOCK || k == LAYOUT_BOX_KIND_REPLACED || lay_out_of_flow(t, c)) continue;
        double cy = 0;
        if (k == LAYOUT_BOX_KIND_FLEX || k == LAYOUT_BOX_KIND_GRID) {
            if (!t->box[c].has_baseline) continue;
            *owner = c;
            *ln = 0;
            *y = t->box[c].y + t->box[c].baseline;
            return 1;
        }
        if (lay_marker_spot(t, c, owner, ln, &cy, clip, depth + 1)) {
            *y = t->box[c].y + cy;
            if (t->box[c].clip == 1) *clip = 1;
            return 1;
        }
    }
    return 0;
}

/* a list item's marker: its text into out -- a symbol's character, or for a marker of text (list 4) the string
   list-style-type gives, or its ordinal as its counter style writes it with its prefix and suffix (CSS Counter
   Styles 3: decimal's suffix a full stop and a space) */
static void lay_marker_text(LCTX, uint32_t el, const lay_style *ls, vector<char> *out) {
    uint32_t cp = 0;
    int list = ls->list;
    if (list == 1) cp = 0x2022;
    else if (list == 2) cp = 0x25E6;
    else if (list == 3) cp = 0x25AA;
    else if (list == 9) cp = 0x25B8;     /* disclosure-closed: a small triangle pointing to the end */
    else if (list == 10) cp = 0x25BE;    /* disclosure-open: one pointing down */
    if (cp) {
        push_back(out, (char)(0xE0 | cp >> 12));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
        return;
    }
    uint32_t c = ls->lcv;
    vector<char> name;
    if (st->cval[c].unit == CSS_UNIT_TEXT) {
        amb_span sp = st->cval[c].text;
        append_range(name, text_ptr(st, sp), sp.len);
        if (len(name) >= 2 && at(name, 0) == '"') { lay_unquote(data(name), len(name), out); return; }
    } else if (st->cval[c].unit == CSS_UNIT_KEYWORD) {
        uint32_t w = st->cval[c].word;
        append_range(name, names_ptr(st, w), names_len(st, w));
    } else append_range(name, "decimal", 7);
    /* its ordinal: its place among its parent's list items, from the list's start */
    uint32_t parent = d->node[el].parent;
    long ord = lay_attr_int(d, parent, "start", 1);
    for (uint32_t ch = d->node[parent].first_child; ch && ch != el; ch = d->node[ch].next_sibling)
        if (lay_display_of(LARGS, ch) == D_LIST_ITEM) ord++;
    ord = lay_attr_int(d, el, "value", ord);
    css_counter_text(st, data(name), len(name), ord, 1, out);
}
/* the room a marker inside its list item's first line takes before the content, as Chromium gives it: a disc's,
   circle's or square's its font size and a third of its ascent, one more; a disclosure triangle's its square of
   0.66em and 0.4em after it; an ordinal's its text and a space */
static double lay_marker_inside(LCTX, uint32_t el, const lay_style *ls) {
    if (!ls->list || !ls->list_inside || (el & LAY_GEN)) return 0;
    lay_style sm = *ls;
    lay_pseudo_style(LARGS, el, CSS_PSEUDO_MARKER, &sm);
    if (ls->list <= 3) return sm.fs + 1 + floor(floor(sm.asc + 0.5) / 3 + 0.5);
    /* a disclosure triangle: its square of 0.66em and 0.4em after it, each in Chromium's 1/64 px */
    if (ls->list >= 9) return floor(0.66 * sm.fs * 64) / 64 + floor(0.4 * sm.fs * 64) / 64;
    vector<char> txt;
    lay_marker_text(LARGS, el, ls, &txt);
    return amb_text_width(f, sm.face, sm.fs, data(txt), len(txt));
}

/* a marker's text in the order it is drawn, its own direction left to right (an isolate: UAX #9 for its simple case):
   each run of right-to-left letters, with the neutrals between them, reversed, as paint takes a right-to-left run (a
   Hebrew counter style's letters); text without them as it is */
static void lay_marker_visual(const vector<char> *in, vector<char> *out) {
    size_t n = len(*in), i = 0;
    while (i < n) {
        size_t z = i;
        uint32_t cp = amb_utf8_next(data(*in), n, &z);
        int c = lay_bidi_class(cp);
        if (c != BC_R && c != BC_AL) { append_range(out, data(*in) + i, z - i); i = z; continue; }
        /* the run: to the last right-to-left letter before a left-to-right one or a number */
        size_t end = z, k = z;
        while (k < n) {
            size_t z2 = k;
            int c2 = lay_bidi_class(amb_utf8_next(data(*in), n, &z2));
            if (c2 == BC_R || c2 == BC_AL || c2 == BC_NSM) { end = z2; k = z2; continue; }
            if (c2 == BC_L || c2 == BC_EN || c2 == BC_AN) break;
            k = z2;
        }
        vector<char> run;
        append_range(run, data(*in) + i, end - i);
        lay_bidi_reverse(&run, out);
        i = end;
    }
}

/* the strut of an inside marker's line (line, the block's) made to hold the marker's text too: its own font's line
   and, under line-height normal, its fallback faces' (Chromium's marker is an inline box of the line). 1, and the
   extent above and below the baseline, when the marker's reaches past the line's */
static int lay_marker_strut(LCTX, uint32_t el, const lay_style *ls, const lay_style *line, double *out_above, double *out_below) {
    if (ls->list != 4 || !ls->list_inside || (el & LAY_GEN)) return 0;
    lay_style sm = *ls;
    lay_pseudo_style(LARGS, el, CSS_PSEUDO_MARKER, &sm);
    vector<char> txt;
    lay_marker_text(LARGS, el, ls, &txt);
    double above, below, la, lb;
    lay_leading(line->lh, line->asc, line->desc, &la, &lb);
    lay_leading(sm.lh, sm.asc, sm.desc, &above, &below);
    if (sm.lhn) {
        uint32_t m = amb_text_fallbacks(f, sm.face, data(txt), len(txt));
        for (int k = 0; m && k < AMB_NFACES; k++) {
            if (!(m >> k & 1)) continue;
            double fa, fd, fg, up, dn;
            amb_face_metrics(f, k, sm.fs, &fa, &fd, &fg);
            lay_leading(fa + fd + fg, fa, fd, &up, &dn);
            if (up > above) above = up;
            if (dn > below) below = dn;
        }
    }
    if (above <= la && below <= lb) return 0;
    *out_above = above > la ? above : la;
    *out_below = below > lb ? below : lb;
    return 1;
}

/* a text node of nothing but white space a block collapses away */
static int lay_blank_text(const dom_document *d, uint32_t x) {
    amb_span sp = d->node[x].data;
    const char *s = text_ptr(d, sp);
    for (uint32_t i = 0; i < sp.len; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r' && s[i] != '\f') return 0;
    return 1;
}
/* a list item el whose marker may take a line of its own, before its first block-level child (lay_flow; measured in
   Chromium): an inside marker, Chromium's inline box at the list item's start, which an anonymous block of its own
   wraps when the first in-flow box after it is a block (lay_flow sees whether it is); and in quirks mode an outside
   marker whose list item's first child is a block-level ul or ol (Blink's NeedsOccupyWholeLine) */
static int lay_marker_own_line(LCTX, uint32_t el, const lay_style *s) {
    if ((el & LAY_GEN) || s->display != D_LIST_ITEM || !s->list) return 0;
    if (s->list_inside) return 1;
    if (!lay_quirks || (el < len(*sx) && (at(*sx, el) & LAY_HAS_BEFORE))) return 0;
    for (uint32_t c = dom_flat_first(d, el); c; c = dom_flat_next(d, c)) {
        int kind = d->node[c].kind;
        if (kind == DOM_NODE_KIND_TEXT) { if (lay_blank_text(d, c)) continue; return 0; }
        if (kind != DOM_NODE_KIND_ELEMENT) continue;
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_NONE) continue;
        uint32_t si = lay_si(sx, gx, c);
        int pos = at(*xc, si).pos;
        if (at(*xc, si).flt || pos == POS_ABSOLUTE || pos == POS_FIXED || !lay_block_level(dsp)) return 0;
        return lay_is_named(d, c, "ul") || lay_is_named(d, c, "ol");
    }
    return 0;
}
/* that line: an anonymous box w wide in b (its first child when first, else its last), its one line as tall as the list
   item el's strut (and an inside marker's text, where that reaches past it), which lay_marker sets the marker in */
static uint32_t lay_marker_box(LCTX, uint32_t b, uint32_t el, double w, int first) {
    uint32_t mb = lay_new_box(LARGS, first ? 0 : b, 0, LAYOUT_BOX_KIND_ANONYMOUS);
    if (first) children_insert_before(t, b, mb, t->box[b].first_child);
    t->box[mb].w = w;
    t->box[mb].how = LAYOUT_WIDTH_HOW_FILL;
    t->box[mb].avail = w;
    lay_style bs;
    lay_style_of(LARGS, el, &bs);
    double ma = 0, mbl = 0;
    if (bs.list == 4 && bs.list_inside && lay_marker_strut(LARGS, el, &bs, &bs, &ma, &mbl)) {
        bs.asc = ma;
        bs.desc = mbl;
        bs.lh = ma + mbl;
    }
    vector<lay_piece> none;
    vector<lay_open> open;
    vector<lay_bu> bu;
    t->box[mb].h = lay_line(LARGS, mb, &bs, &none, 0, 0, 0, w, 0, 0, 0, &open, &bu, 0, el, 1);
    return mb;
}

static void lay_marker(LCTX, uint32_t b, uint32_t el, const lay_style *ls) {
    if (ls->list == 0 || (el & LAY_GEN)) return;
    /* the marker's style: its ::marker's, or the list item's */
    lay_style sm = *ls;
    lay_pseudo_style(LARGS, el, CSS_PSEUDO_MARKER, &sm);
    sm.list = ls->list;
    uint32_t owner = 0, ln = 0;
    double ly = 0;
    int clipped = 0;
    if (!lay_marker_spot(t, b, &owner, &ln, &ly, &clipped, 0)) return;
    /* a first line in a box that clips its overflow, or a flex or grid container's baseline: the marker set in a line of
       the list item's own at that baseline, unclipped, as Chromium sets it in the list item's fragment */
    if (clipped || !ln) {
        owner = lay_marker_box(LARGS, b, el, t->box[b].w, 1);
        ln = t->box[owner].first_line;
        lay_place(t, owner, 0, ly - t->line[ln].baseline);
    }
    vector<char> tl, tv;
    lay_marker_text(LARGS, el, ls, &tl);
    int rtl = 0;
    for (size_t q = 0; q < len(tl) && !rtl; q++) rtl = (unsigned char)at(tl, q) >= 0xD6 && (unsigned char)at(tl, q) <= 0xDF;
    if (rtl) lay_marker_visual(&tl, &tv);
    else swap(&tv, &tl);
    const char *txt = data(tv);
    size_t tn = len(tv);
    /* outside the content, its text (its suffix's space too) ending at the list item's content box, as Chromium's
       ListMarker places a marker of text; inside, at its first line's start, which the line's indent left it (when
       that line is the item's own, or an anonymous block's in it) */
    double mw = amb_text_width(f, sm.face, sm.fs, txt, tn);
    double off_x = 0;
    for (uint32_t x = owner; x && x != b; x = t->box[x].parent) off_x += t->box[x].x;
    int inside = ls->list_inside && (owner == b || (t->box[owner].kind == LAYOUT_BOX_KIND_ANONYMOUS && t->box[owner].parent == b));
    amb_span sp = text_add(t, txt, tn);
    layout_frag_h fh = frag_new(t);
    uint32_t fr = fh.index;
    double base = t->line[ln].baseline;
    double start = -off_x - t->line[ln].x + (inside ? lay_res(st, ls->indent, t->box[b].w) : 0), asc = sm.asc;
    double mx = inside ? start : start - mw;
    if (sm.list <= 3) {
        /* a disc, circle or square: a shape Chromium draws in a square of its own (ListMarker's symbol rect), 1px into
           a marker box that starts its whole ascent's two thirds and 8px before the content outside, 1px before the
           line's start inside; the fragment's top is its whole ascent above the baseline, which paint reads */
        int ai = (int)floor(sm.asc + 0.5), off = ai * 2 / 3;
        mx = inside ? start - 1 : start - off - 8;
        mw = (off + 1) / 2 + 2;
        asc = ai;
    }
    t->frag[fr].kind = LAYOUT_FRAG_KIND_MARKER;
    t->frag[fr].node = node_handle(d, el);
    t->frag[fr].text = sp;
    t->frag[fr].x = mx;
    t->frag[fr].y = base - asc;
    t->frag[fr].w = mw;
    t->frag[fr].h = asc + sm.desc;
    t->frag[fr].baseline = base;
    t->frag[fr].face = (uint8_t)sm.face;
    if (sm.font) t->frag[fr].font = resource_handle(nt, sm.font);
    t->frag[fr].size = sm.fs;
    t->frag[fr].color = sm.color;
    t->frag[fr].fill = sm.fill;
    t->frag[fr].hidden = sm.hidden != 0;
    t->frag[fr].orient = LAY_NOCOMB(sm.orient);
    frags_append(t, ln, fr);
}

/* ---- tables ----
   As Blink's TablesNG lays them out (CSS 2.1, 17; CSS Tables 3; table_layout_algorithm.cc, table_layout_utils.cc,
   table_borders.cc), its LayoutUnits mirrored: sizes in 64ths of a px, a float's LayoutUnit truncated, MulDiv's quotient
   truncated, a remainder left to the last of what shares it. */

typedef int64_t lay_lu;
static lay_lu lay_lu_f(float v) { return (lay_lu)(v * 64.0f); }
static lay_lu lay_lu_d(double v) { return (lay_lu)(v * 64.0); }
static lay_lu lay_lu_ceil(double v) { return (lay_lu)ceil(v * 64.0 - 1e-6); }
static double lay_lu_px(lay_lu u) { return (double)u / 64.0; }
static float lay_lu_fl(lay_lu u) { return (float)u / 64.0f; }
static lay_lu lay_lu_muldiv(lay_lu a, lay_lu b, lay_lu c) { return c ? a * b / c : 0; }
static lay_lu lay_lu_max(lay_lu a, lay_lu b) { return a > b ? a : b; }
static lay_lu lay_lu_min(lay_lu a, lay_lu b) { return a < b ? a : b; }
/* the largest inline size a table takes, Blink's kTableMaxInlineSize */
#define LAY_TMAX ((lay_lu)1000000 * 64)

/* a column's constraints, or a cell's (Blink's TableTypes::Column and CellInlineConstraint): its min and max inline sizes
   (has_mn, has_mx: given), its percentage (has_pct) and the border and padding a fixed table adds to it, a fixed length
   sizing it (constrained), whether it may merge (no cell begins in it, no width of its own), its table's layout fixed */
typedef struct lay_tcon { lay_lu mn, mx, pbp; float pct; uint8_t has_mn, has_mx, has_pct, constrained, mergeable, fixed; } lay_tcon;
/* a cell spanning columns: its constraint, its first column and its span */
typedef struct lay_tspan { lay_tcon c; uint32_t col, span; } lay_tspan;
/* a table's cell: its element, its row group (an index of the groups), its row and column, its spans (its rowspan held to
   its group's rows, its colspan to the table's columns), its box, its border box's height laid out, its borders (top,
   right, bottom, left: a collapsing table's halves of the edges about it, else its own) */
typedef struct lay_cell { uint32_t el, sec, row, col, span, rspan, box; lay_lu h; lay_lu b[4]; } lay_cell;
/* a row group: its element (0: rows directly in the table, an anonymous group of them), its first row and its rows'
   count, its box, whether it is a body (no head or foot) and whether a height of its own constrains it, its offset in
   the table's content box and its height */
typedef struct lay_tsec { uint32_t el, row0, nrows, box; uint8_t body, constrained; lay_lu y, h; } lay_tsec;
/* a row: its element, its group, its box, its height, its offset in its group, whether a fixed height or a cell's
   constrains it, whether a cell spanning rows begins in it */
typedef struct lay_trow { uint32_t el, sec, box; lay_lu h, y; uint8_t constrained, rspan_start; } lay_trow;
/* a col or colgroup element, in the table's order: its element, its first column and its span (not held to the table's
   columns), its colgroup's index among them (-1: none), a colgroup (1; 2 one with col children), its box */
typedef struct lay_tcol { uint32_t el, col0, span, box; int32_t group; uint8_t is_group; } lay_tcol;
/* a collapsing table's edge (Blink's TableBorders::Edge): the border on it, its width, style (Blink's order, collapsed:
   0 none, 1 hidden, 2 inset, 3 groove, 4 outset, 5 ridge, 6 dotted, 7 dashed, 8 solid, 9 double) and colour, the order
   of the box it came from; set: a border is on it; nofill: inside a spanning cell, where none may be */
typedef struct lay_tedge { lay_lu w; uint32_t color, order; uint8_t st, set, nofill; } lay_tedge;
/* what a table's layout keeps beside its lists: its rows and columns, its edges a row of them (two a column, two more),
   whether it collapses its borders and is laid out fixed, its spacing, its borders and paddings (top, right, bottom,
   left) */
typedef struct lay_tinfo { uint32_t nrows, ncols, epr; int collapse, fixed; lay_lu hs, vs; lay_lu tb[4], tp[4]; } lay_tinfo;

/* the parts of a table's layout, as parameters */
#define LTM vector<uint32_t> *caps, vector<lay_tcol> *tcols, vector<lay_tsec> *secs, vector<lay_trow> *rows, vector<lay_cell> *cells, \
    vector<lay_tcon> *con, vector<lay_tedge> *edges, lay_tinfo *ti
#define LTA caps, tcols, secs, rows, cells, con, edges, ti

/* an HTML element's non-negative integer attribute held between lo and hi (the rules for parsing non-negative integers:
   white space, a +, digits), dflt when it has none */
static uint32_t lay_attr_clamped(const dom_document *d, uint32_t x, const char *name, uint32_t lo, uint32_t hi, uint32_t dflt) {
    uint32_t a = lay_attr(d, x, name);
    if (!a) return dflt;
    amb_span v = d->attr[a].value;
    const char *p = text_ptr(d, v);
    size_t i = 0;
    while (i < v.len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\f' || p[i] == '\r')) i++;
    if (i < v.len && p[i] == '+') i++;
    uint64_t n = 0;
    int any = 0;
    for (; i < v.len && p[i] >= '0' && p[i] <= '9'; i++) { if (n < 100000000) n = n * 10 + (uint64_t)(p[i] - '0'); any = 1; }
    if (!any) return dflt;
    return n < lo ? lo : n > hi ? hi : (uint32_t)n;
}

/* the table's parts, grouped as Blink's TableGroupedChildren has them: its captions; its col and colgroup elements;
   its row groups -- the first head, the bodies (row groups, other heads and feet, the rows directly in it as anonymous
   ones) in order, the first foot --, their rows, and the rows' cells, each in its row and column (a cell spanning rows
   holding its columns in the rows below, within its group). The columns' count: the col elements' that may not merge,
   and in the automatic layout each cell's first column, in the fixed layout every column it spans (Blink's
   ComputeMaxColumn) */
static void lay_table_parts(LCTX, uint32_t table, LTM) {
    ti->nrows = 0;
    ti->ncols = 0;
    /* the row groups in the table's order: an element, or a run of rows (its first row, an anonymous group) */
    vector<uint32_t> groups;
    vector<uint8_t> runs;            /* each group: a run of rows (an anonymous group), 1 */
    uint32_t head = 0, foot = 0;
    int in_run = 0;
    for (uint32_t c = d->node[table].first_child; c; c = d->node[c].next_sibling) {
        int dsp = lay_display_of(LARGS, c);
        if (dsp == D_CAPTION) { push_back(caps, c); in_run = 0; continue; }
        if (dsp == D_NONE && d->node[c].kind == DOM_NODE_KIND_ELEMENT && LAY_X(c).tcol) {
            /* a column group with its col children, or a col */
            int is_group = LAY_X(c).tcol == 2;
            lay_tcol tc;
            memset(&tc, 0, sizeof tc);
            tc.el = c;
            tc.group = -1;
            tc.is_group = (uint8_t)is_group;
            int32_t gi = (int32_t)len(*tcols);
            push_back(tcols, tc);
            if (is_group)
                for (uint32_t k = d->node[c].first_child; k; k = d->node[k].next_sibling) {
                    if (d->node[k].kind != DOM_NODE_KIND_ELEMENT || lay_display_of(LARGS, k) != D_NONE || LAY_X(k).tcol != 1) continue;
                    lay_tcol kc;
                    memset(&kc, 0, sizeof kc);
                    kc.el = k;
                    kc.group = gi;
                    push_back(tcols, kc);
                    (*tcols)[(size_t)gi].is_group = 2;
                }
            in_run = 0;
            continue;
        }
        if (dsp == D_ROW_GROUP) {
            int g = LAY_X(c).tgroup;
            if (g == 1 && !head) head = c;
            else if (g == 2 && !foot) foot = c;
            else { push_back(groups, c); push_back(runs, 0); }
            in_run = 0;
            continue;
        }
        if (dsp == D_ROW) {
            if (!in_run) { push_back(groups, c); push_back(runs, 1); }
            in_run = 1;
            continue;
        }
    }
    /* each group's rows and their cells */
    for (int pass = 0; pass < 3; pass++) {
        size_t ng = pass == 1 ? len(groups) : 1;
        for (size_t gk = 0; gk < ng; gk++) {
            uint32_t g = pass == 0 ? head : pass == 2 ? foot : at(groups, gk);
            if (!g) continue;
            int an = pass == 1 && at(runs, gk);
            lay_tsec sg;
            memset(&sg, 0, sizeof sg);
            sg.el = an ? 0 : g;
            sg.row0 = (uint32_t)len(*rows);
            sg.body = (uint8_t)(pass == 1);
            uint32_t si = (uint32_t)len(*secs);
            /* the group's rows: an element's row children; an anonymous group's run of rows, to the next group or caption */
            vector<uint32_t> grow;
            for (uint32_t r = an ? g : d->node[g].first_child; r; r = d->node[r].next_sibling) {
                int rd = lay_display_of(LARGS, r);
                if (an && (rd == D_ROW_GROUP || rd == D_CAPTION || (rd == D_NONE && d->node[r].kind == DOM_NODE_KIND_ELEMENT && LAY_X(r).tcol))) break;
                if (rd == D_ROW) push_back(grow, r);
            }
            sg.nrows = (uint32_t)len(grow);
            push_back(secs, sg);
            vector<uint32_t> busy;            /* each column: the rows a cell spanning rows above still holds it for */
            for (size_t rk = 0; rk < len(grow); rk++) {
                uint32_t r = at(grow, rk);
                lay_trow tr;
                memset(&tr, 0, sizeof tr);
                tr.el = r;
                tr.sec = si;
                uint32_t ri = (uint32_t)len(*rows);
                push_back(rows, tr);
                uint32_t col = 0;
                for (uint32_t cell = d->node[r].first_child; cell; cell = d->node[cell].next_sibling) {
                    if (lay_display_of(LARGS, cell) != D_CELL) continue;
                    while (col < len(busy) && at(busy, col) > 0) col++;
                    lay_cell lc;
                    memset(&lc, 0, sizeof lc);
                    lc.el = cell;
                    lc.sec = si;
                    lc.row = ri;
                    lc.col = col;
                    lc.span = lay_attr_clamped(d, cell, "colspan", 1, 1000, 1);
                    /* rowspan: 0 to the group's end (Blink's kMaxRowSpan), held to the group's rows */
                    uint32_t rs = lay_attr_clamped(d, cell, "rowspan", 0, 65534, 1);
                    if (rs == 0) rs = 65534;
                    uint32_t left = (uint32_t)(len(grow) - rk);
                    lc.rspan = rs < left ? rs : left;
                    if (lc.rspan > 1) {
                        if (len(busy) < col + lc.span) resize(&busy, col + lc.span);
                        for (uint32_t q = col; q < col + lc.span; q++) if (at(busy, q) < lc.rspan) busy[q] = lc.rspan;
                        (*rows)[ri].rspan_start = 1;
                    }
                    uint32_t maxc = ti->fixed ? col + lc.span : col + 1;
                    if (maxc > ti->ncols) ti->ncols = maxc;
                    col += lc.span;
                    push_back(cells, lc);
                }
                for (size_t q = 0; q < len(busy); q++) if (at(busy, q) > 0) busy[q] = at(busy, q) - 1;
            }
        }
    }
    ti->nrows = (uint32_t)len(*rows);
    /* the col elements' columns: each col's span, a colgroup's its cols' or (with none) its own */
    uint32_t next = 0;
    for (size_t k = 0; k < len(*tcols); k++) {
        lay_tcol tc = at(*tcols, k);
        if (tc.is_group == 2) { (*tcols)[k].col0 = next; continue; }
        uint32_t span = lay_attr_clamped(d, tc.el, "span", 1, 1000, 1);
        if (tc.is_group == 0 && tc.group >= 0) {
            /* a col of a colgroup: the group's span grows by it */
            (*tcols)[k].col0 = next;
            (*tcols)[k].span = span;
            (*tcols)[(size_t)tc.group].span += span;
        } else {
            (*tcols)[k].col0 = next;
            (*tcols)[k].span = span;
        }
        next += span;
    }
}

/* ---- a table's borders ---- */

/* a border style (src/layout/layout.h) in Blink's order, collapsed (inset drawn as ridge, outset as groove) */
static uint8_t lay_cstyle(int bs) {
    static const uint8_t m[10] = { 0, 1, 6, 7, 8, 9, 3, 5, 5, 3 };
    return bs >= 0 && bs < 10 ? m[bs] : 0;
}
/* Blink's order's style as layout.h has it */
static uint8_t lay_cstyle_bs(int cs) {
    static const uint8_t m[10] = { LAY_BS_NONE, LAY_BS_HIDDEN, LAY_BS_INSET, LAY_BS_GROOVE, LAY_BS_OUTSET, LAY_BS_RIDGE, LAY_BS_DOTTED,
                                   LAY_BS_DASHED, LAY_BS_SOLID, LAY_BS_DOUBLE };
    return cs >= 0 && cs < 10 ? m[cs] : LAY_BS_NONE;
}
/* a border of style st and width w is more specific than the edge e's (Blink's IsSourceMoreSpecificThanEdge, CSS Tables 3's
   conflict resolution): hidden wins outright, then the wider, then the style later in Blink's order; a tie keeps e's */
static int lay_tedge_wins(uint8_t st, lay_lu w, lay_tedge e) {
    if (e.nofill) return 0;
    if (!e.set || st == 1) return 1;
    if (e.st == 1) return 0;
    if (w < e.w) return 0;
    if (w > e.w) return 1;
    return st > e.st;
}
static void lay_tedge_put(vector<lay_tedge> *edges, size_t i, uint8_t st, lay_lu w, uint32_t color, uint32_t order) {
    if (i >= len(*edges) || !lay_tedge_wins(st, w, at(*edges, i))) return;
    lay_tedge e = at(*edges, i);
    e.set = 1;
    e.st = st;
    e.w = w;
    e.color = color;
    e.order = order;
    (*edges)[i] = e;
}
/* a box's borders merged into the edges about rows r .. r + rspan and columns c .. c + span (Blink's MergeBorders): its
   top and bottom along the row axis, its left and right along the column axis; a cell spanning more than one row or
   column leaves no edge inside it to be filled */
static void lay_tmerge(vector<lay_tedge> *edges, const lay_tinfo *ti, const lay_style *s, uint32_t r, uint32_t c, uint32_t rspan, uint32_t span,
                       int is_cell, uint32_t order) {
    if (!rspan || !span || c >= ti->ncols) return;
    if (span > ti->ncols - c) span = ti->ncols - c;
    uint32_t epr = ti->epr;
    uint8_t cs[4];
    lay_lu w[4];
    for (int k = 0; k < 4; k++) {
        cs[k] = lay_cstyle(s->bs[k]);
        w[k] = cs[k] <= 1 ? 0 : lay_lu_d(s->b[k]);
    }
    for (int k = 0; k < 4; k += 2) {
        if (!cs[k]) continue;
        uint32_t row = k == 0 ? r : r + rspan;
        for (uint32_t q = c; q < c + span; q++) lay_tedge_put(edges, (size_t)row * epr + 2 * q + 1, cs[k], w[k], s->bc[k], order);
    }
    for (int k = 1; k < 4; k += 2) {
        if (!cs[k]) continue;
        uint32_t col = k == 3 ? c : c + span;
        for (uint32_t q = r; q < r + rspan; q++) lay_tedge_put(edges, (size_t)q * epr + 2 * col, cs[k], w[k], s->bc[k], order);
    }
    if (!is_cell || (rspan < 2 && span < 2)) return;
    /* MarkInnerBordersAsDoNotFill: the column edges inside, then the row edges inside */
    for (uint32_t q = r; q < r + rspan; q++)
        for (uint32_t cc = c + 1; cc < c + span; cc++) {
            size_t i = (size_t)q * epr + 2 * cc;
            if (i < len(*edges) && !at(*edges, i).set) (*edges)[i].nofill = 1;
        }
    for (uint32_t q = r + 1; q < r + rspan; q++)
        for (uint32_t cc = c; cc < c + span; cc++) {
            size_t i = (size_t)q * epr + 2 * cc + 1;
            if (i < len(*edges) && !at(*edges, i).set) (*edges)[i].nofill = 1;
        }
}
/* an edge is drawn: a border on it of a style that draws, wider than nothing (Blink's CanPaint) */
static int lay_tedge_paints(const vector<lay_tedge> *edges, int64_t i) {
    if (i < 0 || (size_t)i >= len(*edges)) return 0;
    lay_tedge e = at(*edges, (size_t)i);
    return e.set && e.st > 1 && e.w > 0;
}
static lay_lu lay_tedge_w(const vector<lay_tedge> *edges, int64_t i) {
    if (i < 0 || (size_t)i >= len(*edges)) return 0;
    lay_tedge e = at(*edges, (size_t)i);
    return e.set ? e.w : 0;
}
/* the borders of the cell at rows r .. r + rspan, columns c .. c + span of a collapsing table (Blink's GetCellBorders): on
   each side the widest edge drawn there, halved */
static void lay_tcell_borders(const vector<lay_tedge> *edges, const lay_tinfo *ti, uint32_t r, uint32_t c, uint32_t rspan, uint32_t span, lay_lu *b) {
    for (int k = 0; k < 4; k++) b[k] = 0;
    uint32_t epr = ti->epr;
    if (!epr || c * 2 >= epr || r > ti->nrows) return;
    if (span > ti->ncols - c) span = ti->ncols - c;
    for (uint32_t i = 0; i < rspan; i++) {
        int64_t s0 = (int64_t)(r + i) * epr + 2 * c, s1 = s0 + 2 * span;
        if (lay_tedge_paints(edges, s0)) b[3] = lay_lu_max(b[3], lay_tedge_w(edges, s0));
        if ((size_t)s0 >= len(*edges)) break;
        if (lay_tedge_paints(edges, s1)) b[1] = lay_lu_max(b[1], lay_tedge_w(edges, s1));
    }
    for (uint32_t i = 0; i < span; i++) {
        uint32_t ci = 2 * (c + i) + 1;
        if (ci >= epr) break;
        int64_t s0 = (int64_t)r * epr + ci, s1 = s0 + (int64_t)rspan * epr;
        if (lay_tedge_paints(edges, s0)) b[0] = lay_lu_max(b[0], lay_tedge_w(edges, s0));
        if (lay_tedge_paints(edges, s1)) b[2] = lay_lu_max(b[2], lay_tedge_w(edges, s1));
    }
    for (int k = 0; k < 4; k++) b[k] /= 2;
}
/* the borders: a collapsing table's edges (Blink's ComputeTableBorders: its cells' borders in their order, then its rows',
   its row groups', its cols' and colgroups' and its own, a tie kept by the earlier), each cell's half of the edges about
   it and the table's of its outer ones; a separating table's cells their own, the table its own and its paddings */
static void lay_table_borders(LCTX, const lay_style *ts, double cw, LTM) {
    if (!ti->collapse) {
        for (int k = 0; k < 4; k++) { ti->tb[k] = lay_lu_d(ts->b[k]); ti->tp[k] = lay_lu_d(lay_res(st, ts->p[k], cw)); }
        for (size_t k = 0; k < len(*cells); k++) {
            lay_style cs;
            lay_style_of(LARGS, at(*cells, k).el, &cs);
            for (int q = 0; q < 4; q++) (*cells)[k].b[q] = lay_lu_d(cs.b[q]);
        }
        return;
    }
    for (int k = 0; k < 4; k++) { ti->tb[k] = 0; ti->tp[k] = 0; }
    ti->epr = 2 * (ti->ncols + 1);
    clear(*edges);
    resize(edges, (size_t)(ti->nrows + 1) * ti->epr);
    if (!ti->ncols) { ti->epr = 0; clear(*edges); return; }
    uint32_t order = 0;
    for (size_t k = 0; k < len(*cells); k++) {
        lay_cell c = at(*cells, k);
        lay_style cs;
        lay_style_of(LARGS, c.el, &cs);
        lay_tmerge(edges, ti, &cs, c.row, c.col, c.rspan, c.span, 1, ++order);
    }
    for (size_t k = 0; k < len(*rows); k++) {
        lay_style rs;
        lay_style_of(LARGS, at(*rows, k).el, &rs);
        lay_tmerge(edges, ti, &rs, (uint32_t)k, 0, 1, ti->ncols, 0, ++order);
    }
    for (size_t k = 0; k < len(*secs); k++) {
        lay_tsec sg = at(*secs, k);
        if (!sg.el) { ++order; continue; }
        lay_style gs;
        lay_style_of(LARGS, sg.el, &gs);
        lay_tmerge(edges, ti, &gs, sg.row0, 0, sg.nrows, ti->ncols, 0, ++order);
    }
    /* the cols (each column of a col's span on its own), one order for all; then the colgroups', another */
    uint32_t col_order = ++order, group_order = ++order;
    for (int pass = 0; pass < 2; pass++)
        for (size_t k = 0; k < len(*tcols); k++) {
            lay_tcol tc = at(*tcols, k);
            if ((pass == 0) != (tc.is_group == 0) || tc.col0 >= ti->ncols) continue;
            uint32_t span = tc.span < ti->ncols - tc.col0 ? tc.span : ti->ncols - tc.col0;
            lay_style ks;
            lay_style_of(LARGS, tc.el, &ks);
            if (pass == 0) for (uint32_t q = 0; q < span; q++) lay_tmerge(edges, ti, &ks, 0, tc.col0 + q, ti->nrows, 1, 0, col_order);
            else lay_tmerge(edges, ti, &ks, 0, tc.col0, ti->nrows, span, 0, group_order);
        }
    lay_tmerge(edges, ti, ts, 0, 0, ti->nrows, ti->ncols, 0, ++order);
    lay_lu tb[4];
    lay_tcell_borders(edges, ti, 0, 0, ti->nrows, ti->ncols, tb);
    for (int k = 0; k < 4; k++) ti->tb[k] = tb[k];
    for (size_t k = 0; k < len(*cells); k++) {
        lay_cell c = at(*cells, k);
        lay_lu b[4];
        lay_tcell_borders(edges, ti, c.row, c.col, c.rspan, c.span, b);
        for (int q = 0; q < 4; q++) (*cells)[k].b[q] = b[q];
    }
}

/* ---- a table's columns ---- */

/* a column a col or colgroup makes (Blink's CreateColumn): its width a fixed length's (else the colgroup's, for a col of
   one in the automatic layout), min-width holding it, its percentage; it may merge, in the automatic layout, with no
   width and no percentage */
static lay_tcon lay_tcol_con(LCTX, uint32_t el, int has_def, lay_lu def, int fixed) {
    lay_tcon c;
    memset(&c, 0, sizeof c);
    lay_style s;
    lay_style_of(LARGS, el, &s);
    lay_len mnl = LAY_X(el).minw;
    int has_w = s.width.unit == LAY_PX, has_min = mnl.unit == LAY_PX;
    lay_lu w = has_w ? lay_lu_d(s.width.v) : 0, mn = has_min ? lay_lu_d(mnl.v) : 0;
    if (s.width.unit == LAY_PCT || (s.width.unit == LAY_CALC && s.width.v == 0)) { c.has_pct = 1; c.pct = (float)(s.width.unit == LAY_PCT ? s.width.v : s.width.pct); }
    if (!has_w && has_def) { has_w = 1; w = def; }
    if (has_min && has_w && mn > w) w = mn;
    c.constrained = (uint8_t)has_w;
    if (c.has_pct && c.pct == 0.0f) c.has_pct = 0;
    c.fixed = (uint8_t)fixed;
    c.mergeable = (uint8_t)(!fixed && (has_w ? w : 0) == 0 && (c.has_pct ? c.pct : 0.0f) == 0.0f);
    c.has_mn = 1;
    c.mn = mn;
    c.has_mx = (uint8_t)has_w;
    c.mx = w;
    return c;
}
/* the columns the col elements make (Blink's ComputeColumnElementConstraints): a col's span of its constraint, a
   colgroup's of its own when it has no cols */
static void lay_tcol_cons(LCTX, LTM) {
    clear(*con);
    lay_tcon group;
    memset(&group, 0, sizeof group);
    for (size_t k = 0; k < len(*tcols); k++) {
        lay_tcol tc = at(*tcols, k);
        if (tc.is_group) {
            group = lay_tcol_con(LARGS, tc.el, 0, 0, ti->fixed);
            if (tc.is_group == 1) for (uint32_t q = 0; q < tc.span; q++) push_back(con, group);
            continue;
        }
        int in_group = tc.group >= 0;
        lay_tcon c = lay_tcol_con(LARGS, tc.el, !ti->fixed && in_group && group.has_mx, group.mx, ti->fixed);
        for (uint32_t q = 0; q < tc.span; q++) push_back(con, c);
    }
    /* the table's columns: at least to the last of theirs that may not merge */
    size_t last = len(*con);
    while (last > 0 && at(*con, last - 1).mergeable) last--;
    if (last > ti->ncols) ti->ncols = (uint32_t)last;
}
/* a cell's inline constraint (Blink's CreateCellInlineConstraint): its content's min- and max-content widths (each a
   LayoutUnit up to a 64th of a px) with its borders (b, its inline sum) and paddings, its width, min-width and
   max-width when they are lengths (box-sizing as they ask), its percentage; in the fixed layout no min-content; in quirks
   mode a nowrap cell's width its minimum (the Quirks Mode Standard 3.9) */
static lay_tcon lay_tcell_con(LCTX, uint32_t el, int fixed, lay_lu bd, double cw) {
    lay_tcon c;
    memset(&c, 0, sizeof c);
    lay_style s;
    lay_style_of(LARGS, el, &s);
    lay_lu bp = bd + lay_lu_d(lay_res(st, s.p[1], 0) + lay_res(st, s.p[3], 0));
    int bb = s.border_box;
    /* a cell in another frame than its row's: its own lengths not the column's; of the other axis (CSS Writing Modes 4,
       7.3), its block size laid out its contribution */
    uint32_t crow = lay_parent_of(d, gx, el);
    int cf = lay_frame(s.wm), rf = crow ? lay_frame(data(*sc)[lay_si(sx, gx, crow)].wm) : cf;
    int same = cf == rf, ortho = (cf == 0) != (rf == 0);
    int has_w = same && s.width.unit == LAY_PX, has_min = same && LAY_X(el).minw.unit == LAY_PX, has_max = same && s.maxw.unit == LAY_PX;
    lay_lu w = 0, mn = 0, mx = 0;
    if (has_w) { w = lay_lu_d(s.width.v); w = bb ? lay_lu_max(w, bp) : w + bp; }
    if (has_min) { mn = lay_lu_d(LAY_X(el).minw.v); mn = bb ? lay_lu_max(mn, bp) : mn + bp; }
    if (has_max) { mx = lay_lu_d(s.maxw.v); mx = bb ? lay_lu_max(mx, bp) : mx + bp; if (has_min) mx = lay_lu_max(mn, mx); }
    if (same && (s.width.unit == LAY_PCT || (s.width.unit == LAY_CALC && s.width.v == 0))) { c.has_pct = 1; c.pct = (float)(s.width.unit == LAY_PCT ? s.width.v : s.width.pct); }
    if (c.has_pct && s.maxw.unit == LAY_PCT && (float)s.maxw.v < c.pct) c.pct = (float)s.maxw.v;
    /* the content's widths: the margin box's less its own edges */
    double cmn, cmx;
    if (ortho) lay_child_intrinsic(LARGS, crow, el, cw, &cmn, &cmx);
    else lay_intrinsic_of(LARGS, el, cw, &cmn, &cmx, 0);
    double edges = lay_edges_px(st, &s, cw);
    lay_lu min_content = lay_lu_ceil(cmn - edges > 0 ? cmn - edges : 0) + bp, max_content = lay_lu_ceil(cmx - edges > 0 ? cmx - edges : 0) + bp;
    lay_lu rmin = 0;
    if (!fixed) {
        rmin = lay_lu_max(min_content, has_min ? mn : 0);
        if (has_w && lay_quirks && lay_attr(d, el, "nowrap")) rmin = lay_lu_max(rmin, w);
    }
    lay_lu cmax = has_w ? w : max_content;
    if (has_max) { cmax = lay_lu_min(cmax, mx); rmin = lay_lu_min(rmin, mx); }
    c.mn = rmin;
    c.mx = lay_lu_max(rmin, cmax);
    c.has_mn = c.has_mx = 1;
    c.constrained = (uint8_t)has_w;
    c.pbp = fixed && c.has_pct && !bb ? bp : 0;
    return c;
}
/* the constraint of a column's cells, another cell's taken in (Blink's CellInlineConstraint::Encompass) */
static void lay_tcell_encompass(lay_tcon *c, lay_tcon o) {
    if (o.mn > c->mn) c->mn = o.mn;
    if (c->constrained == o.constrained) c->mx = lay_lu_max(c->mx, o.mx);
    else if (c->constrained) c->mx = lay_lu_max(c->mx, o.mn);
    else c->mx = lay_lu_max(c->mn, o.mx);
    c->constrained = (uint8_t)(c->constrained || o.constrained);
    if (o.has_pct && (!c->has_pct || o.pct > c->pct)) { c->has_pct = 1; c->pct = o.pct; c->pbp = o.pbp; }
}
/* a column's constraint, its cells' taken in (Blink's Column::Encompass): a fixed table's column of a width of its own
   keeps it; a cell makes an automatic table's column one that may not merge */
static void lay_tcol_encompass(lay_tcon *c, lay_tcon o) {
    if (c->constrained && c->fixed) return;
    if (!c->fixed) c->mergeable = 0;
    if (c->has_mn) {
        if (c->mn < o.mn) c->mn = o.mn;
        if (c->constrained) c->mx = lay_lu_max(c->mx, o.constrained ? o.mx : o.mn);
        else c->mx = lay_lu_max(c->has_mx ? c->mx : 0, o.mx);
        c->has_mx = 1;
    } else {
        c->mn = o.mn;
        c->mx = o.mx;
        c->has_mn = c->has_mx = 1;
    }
    if (c->mx < c->mn) c->mx = c->mn;
    if (o.has_pct && (!c->has_pct || o.pct > c->pct)) { c->has_pct = 1; c->pct = o.pct; c->pbp = o.pbp; }
    c->constrained = (uint8_t)(c->constrained || o.constrained);
}
/* a percentage column's size of the target (Blink's ResolvePercentInlineSize): its percentage of it with the border and
   padding a fixed table adds, never under its minimum */
static lay_lu lay_tpct(lay_tcon c, lay_lu target) {
    return lay_lu_max(c.has_mn ? c.mn : 0, lay_lu_f(c.pct * lay_lu_fl(target) / 100.0f) + c.pbp);
}
static int lay_tis_fixed(lay_tcon c) { return c.constrained && !c.has_pct && c.has_mx; }

/* the target shared among the n columns from c0 by CSS Tables 3's guesses, as Blink's
   DistributeInlineSizeToComputedInlineSizeAuto shares it: the minimums; the percentage columns to their percentages; the
   fixed ones to their widths; the automatic ones to their maximums; past them, the automatic ones (or else, when the
   target constrains them, the fixed ones; or else the percentage ones) in proportion to their maximums. Merging columns
   take nothing. Into out (n sizes) */
static void lay_tdist_auto(lay_lu target, const vector<lay_tcon> *cols, size_t c0, size_t n, int constrained_target, vector<lay_lu> *out) {
    clear(*out);
    resize(out, n);
    uint32_t npct = 0, nfixed = 0, nauto = 0;
    lay_lu g[4] = { 0, 0, 0, 0 }, inc[4] = { 0, 0, 0, 0 }, auto_max = 0, fixed_max = 0;
    float total_pct = 0;
    for (size_t i = c0; i < c0 + n; i++) {
        lay_tcon c = at(*cols, i);
        if (c.mergeable) continue;
        if (c.has_pct) {
            npct++;
            total_pct += c.pct;
            lay_lu p = lay_tpct(c, target);
            g[0] += c.mn; g[1] += p; g[2] += p; g[3] += p;
            inc[1] += p - c.mn;
        } else if (c.constrained) {
            nfixed++;
            fixed_max += c.mx;
            g[0] += c.mn; g[1] += c.mn; g[2] += c.mx; g[3] += c.mx;
            inc[2] += c.mx - c.mn;
        } else {
            nauto++;
            auto_max += c.mx;
            g[0] += c.mn; g[1] += c.mn; g[2] += c.mn; g[3] += c.mx;
            inc[3] += c.mx - c.mn;
        }
    }
    if (target < g[0]) target = g[0];
    int guess = 4;
    for (int k = 0; k < 4; k++) if (g[k] >= target) { guess = k; break; }
    lay_lu dist, rest;
    int64_t last = -1;
    switch (guess) {
    case 0:
        for (size_t i = 0; i < n; i++) if (!at(*cols, c0 + i).mergeable) (*out)[i] = at(*cols, c0 + i).mn;
        break;
    case 1:
        dist = target - g[0];
        rest = dist;
        for (size_t i = 0; i < n; i++) {
            lay_tcon c = at(*cols, c0 + i);
            if (c.mergeable) continue;
            if (c.has_pct) {
                last = (int64_t)i;
                lay_lu up = lay_tpct(c, target) - c.mn;
                lay_lu delta = inc[1] > 0 ? lay_lu_muldiv(dist, up, inc[1]) : dist / npct;
                rest -= delta;
                (*out)[i] = c.mn + delta;
            } else (*out)[i] = c.mn;
        }
        if (rest && last >= 0) (*out)[(size_t)last] = at(*out, (size_t)last) + rest;
        break;
    case 2:
        dist = target - g[1];
        rest = dist;
        for (size_t i = 0; i < n; i++) {
            lay_tcon c = at(*cols, c0 + i);
            if (c.mergeable) continue;
            if (c.has_pct) (*out)[i] = lay_tpct(c, target);
            else if (c.constrained) {
                last = (int64_t)i;
                lay_lu delta = inc[2] > 0 ? lay_lu_muldiv(dist, c.mx - c.mn, inc[2]) : dist / nfixed;
                rest -= delta;
                (*out)[i] = c.mn + delta;
            } else (*out)[i] = c.mn;
        }
        if (rest && last >= 0) (*out)[(size_t)last] = at(*out, (size_t)last) + rest;
        break;
    case 3: {
        dist = target - g[2];
        /* an exact match (an automatic width, the columns at their maximums): those, not the shares, as Blink keeps them */
        int exact = target == g[3];
        rest = exact ? 0 : dist;
        for (size_t i = 0; i < n; i++) {
            lay_tcon c = at(*cols, c0 + i);
            if (c.mergeable) continue;
            if (c.has_pct) (*out)[i] = lay_tpct(c, target);
            else if (c.constrained || exact) (*out)[i] = c.mx;
            else {
                last = (int64_t)i;
                lay_lu delta = inc[3] > 0 ? lay_lu_muldiv(dist, c.mx - c.mn, inc[3]) : dist / nauto;
                rest -= delta;
                (*out)[i] = c.mn + delta;
            }
        }
        if (rest && last >= 0) (*out)[(size_t)last] = at(*out, (size_t)last) + rest;
        break;
    }
    default:
        dist = target - g[3];
        rest = dist;
        if (nauto > 0) {
            for (size_t i = 0; i < n; i++) {
                lay_tcon c = at(*cols, c0 + i);
                if (c.mergeable) continue;
                if (c.has_pct) (*out)[i] = lay_tpct(c, target);
                else if (c.constrained) (*out)[i] = c.mx;
                else {
                    last = (int64_t)i;
                    lay_lu delta = auto_max > 0 ? lay_lu_muldiv(dist, c.mx, auto_max) : dist / nauto;
                    rest -= delta;
                    (*out)[i] = c.mx + delta;
                }
            }
        } else if (nfixed > 0 && constrained_target) {
            for (size_t i = 0; i < n; i++) {
                lay_tcon c = at(*cols, c0 + i);
                if (c.mergeable) continue;
                if (c.has_pct) (*out)[i] = lay_tpct(c, target);
                else if (c.constrained) {
                    last = (int64_t)i;
                    lay_lu delta = fixed_max > 0 ? lay_lu_muldiv(dist, c.mx, fixed_max) : dist / nfixed;
                    rest -= delta;
                    (*out)[i] = c.mx + delta;
                }
            }
        } else if (npct > 0) {
            for (size_t i = 0; i < n; i++) {
                lay_tcon c = at(*cols, c0 + i);
                if (c.mergeable || !c.has_pct) continue;
                last = (int64_t)i;
                lay_lu delta = total_pct != 0.0f ? lay_lu_f(lay_lu_fl(dist) * c.pct / total_pct) : dist / npct;
                rest -= delta;
                (*out)[i] = lay_tpct(c, target) + delta;
            }
            /* Blink's test of the last column's index as a truth: the first column keeps its rounding */
            if (rest && last > 0) (*out)[(size_t)last] = at(*out, (size_t)last) + rest;
            rest = 0;
        }
        if (rest && last >= 0) (*out)[(size_t)last] = at(*out, (size_t)last) + rest;
        break;
    }
}

/* the target shared among a fixed table's columns (Blink's SynchronizeAssignableTableInlineSizeAndColumnsFixed): the
   fixed ones their widths (scaled down to the target, or up to what the percentages leave when no column is automatic),
   then the percentage ones theirs (scaled likewise), then the automatic ones what is left, evenly (a column of width 0
   one of them, unless all are); the last column given takes what rounding left */
static void lay_tsync_fixed(lay_lu target, const vector<lay_tcon> *cols, vector<lay_lu> *out) {
    size_t n = len(*cols);
    clear(*out);
    resize(out, n);
    uint32_t npct = 0, nauto = 0, nfixed = 0, nzero = 0;
    lay_lu total_pct = 0, total_fixed = 0, given = 0;
    for (size_t i = 0; i < n; i++) {
        lay_tcon c = at(*cols, i);
        if (c.has_pct) { npct++; total_pct += lay_tpct(c, target); }
        else if (lay_tis_fixed(c) && c.mx != 0) { nfixed++; total_fixed += c.mx; }
        else if (c.constrained && c.has_mx && c.mx == 0) nzero++;
        else nauto++;
    }
    int64_t last = -1;
    if (nfixed > 0) {
        float scale = 1.0f;
        int scalable = 1;
        lay_lu tfix = target - total_pct > 0 ? target - total_pct : 0;
        int up = total_fixed < tfix && nauto == 0, down = total_fixed > target;
        if (up || down) {
            if (total_fixed != 0) scale = lay_lu_fl(tfix) / lay_lu_fl(total_fixed);
            else scalable = 0;
        }
        for (size_t i = 0; i < n; i++) {
            lay_tcon c = at(*cols, i);
            if (!(lay_tis_fixed(c) && c.mx != 0)) continue;
            last = (int64_t)i;
            (*out)[i] = scalable ? lay_lu_f(scale * lay_lu_fl(c.mx)) : lay_lu_f(lay_lu_fl(target) / (float)nfixed);
            given += at(*out, i);
        }
    }
    if (given >= target) return;
    if (npct > 0) {
        float scale = 1.0f;
        int scalable = 1;
        int up = total_pct < target - given && nauto == 0, down = total_pct > target - given;
        if (up || down) {
            if (total_pct != 0) scale = lay_lu_fl(target - given) / lay_lu_fl(total_pct);
            else scalable = 0;
        }
        for (size_t i = 0; i < n; i++) {
            lay_tcon c = at(*cols, i);
            if (!c.has_pct) continue;
            last = (int64_t)i;
            (*out)[i] = scalable ? lay_lu_f(scale * lay_lu_fl(lay_tpct(c, target))) : lay_lu_f(lay_lu_fl(target - given) / (float)npct);
            given += at(*out, i);
        }
    }
    lay_lu share = target - given;
    int zero_all = nzero == n;
    for (size_t i = 0; i < n; i++) {
        lay_tcon c = at(*cols, i);
        if (c.has_pct || (lay_tis_fixed(c) && c.mx != 0)) continue;
        int zero = c.constrained && c.has_mx && c.mx == 0;
        if (zero && !zero_all) continue;
        last = (int64_t)i;
        (*out)[i] = lay_lu_f(lay_lu_fl(share) / (float)(zero_all ? nzero : nauto));
        given += at(*out, i);
    }
    if (last >= 0) (*out)[(size_t)last] = at(*out, (size_t)last) + (target - given);
}

/* a cell spanning columns shared among them (Blink's DistributeColspanCellToColumnsAuto and -Fixed): its widths less the
   spacing between them; in the automatic layout its percentage over the columns of none by their maximums, then its
   minimum and its maximum shared as the table's width is; in the fixed layout evenly, to columns that have none */
static void lay_tspan_share(lay_tspan sp, lay_lu hs, int fixed, vector<lay_tcon> *cols) {
    size_t n = len(*cols);
    if (!n || sp.col >= n) return;
    size_t span = sp.span < n - sp.col ? sp.span : n - sp.col;
    lay_lu inner = 0;
    int first = 1;
    uint32_t eff = 0;
    for (size_t i = sp.col; i < sp.col + span; i++) {
        if (at(*cols, i).mergeable) continue;
        eff++;
        if (!first) inner += hs;
        first = 0;
    }
    if (fixed) {
        lay_lu cmin = sp.c.constrained ? lay_lu_max(sp.c.mn - inner, 0) : 0, cmax = lay_lu_max(sp.c.mx - inner, 0);
        lay_lu rmin = cmin, rmax = cmax;
        if (!eff) return;
        lay_lu nmin = lay_lu_f(lay_lu_fl(cmin) / (float)eff), nmax = lay_lu_f(lay_lu_fl(cmax) / (float)eff);
        int64_t last = -1;
        for (size_t i = sp.col; i < sp.col + span; i++) {
            lay_tcon c = at(*cols, i);
            if (c.mergeable) continue;
            last = (int64_t)i;
            rmin -= nmin;
            rmax -= nmax;
            if (!c.has_mn) { c.constrained = (uint8_t)(c.constrained || sp.c.constrained); c.mn = nmin; c.has_mn = 1; }
            if (!c.has_mx) { c.constrained = (uint8_t)(c.constrained || sp.c.constrained); c.mx = nmax; c.has_mx = 1; }
            if (!c.has_pct && !c.constrained && sp.c.has_pct) { c.has_pct = 1; c.pct = sp.c.pct / (float)eff; }
            (*cols)[i] = c;
        }
        if (last >= 0) {
            lay_tcon c = at(*cols, (size_t)last);
            c.mn += rmin;
            c.mx += rmax;
            (*cols)[(size_t)last] = c;
        }
        return;
    }
    lay_lu cmin = lay_lu_max(sp.c.mn - inner, 0), cmax = lay_lu_max(sp.c.mx - inner, 0);
    if (sp.c.has_pct) {
        float cols_pct = 0;
        uint32_t nall = 0, npct = 0, nnon = 0;
        lay_lu non_max = 0;
        for (size_t i = sp.col; i < sp.col + span; i++) {
            lay_tcon c = at(*cols, i);
            if (!c.has_mx) { c.mx = 0; c.has_mx = 1; }
            if (!c.has_mn) { c.mn = 0; c.has_mn = 1; }
            (*cols)[i] = c;
            if (c.mergeable) continue;
            nall++;
            if (c.has_pct) { npct++; cols_pct += c.pct; }
            else { nnon++; non_max += c.mx; }
        }
        float surplus = sp.c.pct - cols_pct;
        if (surplus > 0.0f && nall > npct)
            for (size_t i = sp.col; i < sp.col + span; i++) {
                lay_tcon c = at(*cols, i);
                if (c.has_pct || c.mergeable) continue;
                c.pct = non_max != 0 ? surplus * lay_lu_fl(c.mx) / lay_lu_fl(non_max) : surplus / (float)nnon;
                c.has_pct = 1;
                (*cols)[i] = c;
            }
    }
    for (size_t i = sp.col; i < sp.col + span; i++) {
        lay_tcon c = at(*cols, i);
        if (!c.has_mn) { c.mn = 0; c.has_mn = 1; }
        if (!c.has_mx) { c.mx = 0; c.has_mx = 1; }
        (*cols)[i] = c;
    }
    vector<lay_lu> sizes;
    lay_tdist_auto(cmin, cols, sp.col, span, 1, &sizes);
    for (size_t i = 0; i < span; i++) {
        lay_tcon c = at(*cols, sp.col + i);
        c.mn = lay_lu_max(c.mn, at(sizes, i));
        (*cols)[sp.col + i] = c;
    }
    lay_tdist_auto(cmax, cols, sp.col, span, sp.c.constrained, &sizes);
    for (size_t i = 0; i < span; i++) {
        lay_tcon c = at(*cols, sp.col + i);
        c.mx = lay_lu_max(lay_lu_max(c.mn, c.mx), at(sizes, i));
        (*cols)[sp.col + i] = c;
    }
}

/* the table's columns' constraints (Blink's ComputeColumnConstraints): the col elements' columns, its cells' (in the
   fixed layout its first row's alone), a column for each the cells need, those the col elements add past them that
   may merge cut off, a column that may not merge in each spanning cell's span, each column taking its cells in, the
   spanning cells shared out (the narrower first, then by their first columns), the percentages held to 100% (in the
   automatic layout each to what those before it leave; in the fixed layout all scaled) */
static void lay_table_columns(LCTX, double cw, LTM) {
    vector<lay_tcon> per;           /* each column's cells of span 1 together */
    vector<uint8_t> has;
    vector<lay_tspan> spans;
    resize(&per, ti->ncols);
    resize(&has, ti->ncols);
    uint32_t first_sec = 0xffffffffu;
    for (size_t k = 0; k < len(*secs); k++) if (at(*secs, k).nrows && first_sec == 0xffffffffu) first_sec = (uint32_t)k;
    for (size_t k = 0; k < len(*cells); k++) {
        lay_cell c = at(*cells, k);
        if (c.col >= ti->ncols) continue;
        if (ti->fixed && (c.sec != first_sec || c.row != at(*secs, c.sec).row0)) continue;
        lay_tcon cc = lay_tcell_con(LARGS, c.el, ti->fixed, c.b[1] + c.b[3], cw);
        if (c.span == 1) {
            if (at(has, c.col)) { lay_tcon m = at(per, c.col); lay_tcell_encompass(&m, cc); per[c.col] = m; }
            else { per[c.col] = cc; has[c.col] = 1; }
        } else {
            lay_tspan sp;
            sp.c = cc;
            sp.col = c.col;
            sp.span = c.span;
            push_back(spans, sp);
        }
    }
    /* columns for the cells: the defaults past the col elements', or those past the cells that may merge cut off */
    if (len(*con) < ti->ncols) {
        lay_tcon dc;
        memset(&dc, 0, sizeof dc);
        dc.fixed = (uint8_t)ti->fixed;
        dc.mergeable = (uint8_t)!ti->fixed;
        while (len(*con) < ti->ncols) push_back(con, dc);
    } else if (len(*con) > ti->ncols) {
        size_t last = len(*con);
        while (last > ti->ncols && at(*con, last - 1).mergeable) last--;
        resize(con, last);
        ti->ncols = (uint32_t)last;
        resize(&per, ti->ncols);
        resize(&has, ti->ncols);
    }
    /* a column that may not merge in each spanning cell's span: its first (Blink's EnsureDistributableColumnExists) */
    for (size_t k = 0; k < len(spans); k++) {
        lay_tspan sp = at(spans, k);
        if (sp.col < len(*con)) (*con)[sp.col].mergeable = 0;
    }
    for (uint32_t i = 0; i < ti->ncols && i < len(per); i++) {
        if (!at(has, i)) continue;
        lay_tcon c = at(*con, i);
        lay_tcol_encompass(&c, at(per, i));
        (*con)[i] = c;
    }
    /* the spanning cells by span, then by first column (stably) */
    for (size_t k = 1; k < len(spans); k++) {
        lay_tspan x = at(spans, k);
        size_t q = k;
        while (q > 0 && (at(spans, q - 1).span > x.span || (at(spans, q - 1).span == x.span && at(spans, q - 1).col > x.col))) {
            spans[q] = at(spans, q - 1);
            q--;
        }
        spans[q] = x;
    }
    for (size_t k = 0; k < len(spans); k++) {
        lay_tspan sp = at(spans, k);
        if (sp.span < 2 || sp.col >= ti->ncols) continue;
        lay_tspan_share(sp, ti->hs, ti->fixed, con);
    }
    float total = 0;
    for (size_t i = 0; i < len(*con); i++) {
        lay_tcon c = at(*con, i);
        if (c.has_pct) {
            if (!ti->fixed && c.pct + total > 100.0f) c.pct = 100.0f - total;
            total += c.pct;
        }
        if (!c.has_mn) { c.mn = 0; c.has_mn = 1; }
        if (!c.has_mx) { c.mx = 0; c.has_mx = 1; }
        (*con)[i] = c;
    }
    if (ti->fixed && total > 100.0f)
        for (size_t i = 0; i < len(*con); i++) if (at(*con, i).has_pct) (*con)[i].pct = at(*con, i).pct * 100.0f / total;
}

/* the space no column takes: the table's borders and paddings, and the spacing before each column that does not merge
   and after the last (Blink's ComputeUndistributableTableSpace) */
static lay_lu lay_tundist(const vector<lay_tcon> *con, const lay_tinfo *ti) {
    uint32_t spaces = 2;
    int first = 1;
    for (size_t i = 0; i < len(*con); i++) {
        if (at(*con, i).mergeable) continue;
        if (first) first = 0;
        else spaces++;
    }
    return ti->tb[1] + ti->tb[3] + ti->tp[1] + ti->tp[3] + (lay_lu)spaces * ti->hs;
}
/* the grid's min- and max-content widths (Blink's ComputeGridInlineMinMax): the columns' sums (a fixed table's fixed
   column its width), the maximum grown to what the percentages ask, the space no column takes about each */
static void lay_tgrid_minmax(const vector<lay_tcon> *con, lay_lu undist, int fixed, int allow_pct, lay_lu *mn, lay_lu *mx) {
    lay_lu a = 0, b = 0, estimate = 0, non_pct = 0;
    float pct_sum = 0;
    for (size_t i = 0; i < len(*con); i++) {
        lay_tcon c = at(*con, i);
        if (c.has_mn) {
            a += fixed && lay_tis_fixed(c) ? c.mx : c.mn;
            if (c.has_pct && c.pct > 0) {
                if (c.mx > 0) estimate = lay_lu_max(estimate, lay_lu_f(100.0f / c.pct * lay_lu_fl(c.mx - c.pbp)));
            } else non_pct += c.mx;
        }
        if (c.has_mx) b += c.mx;
        if (c.has_pct) pct_sum += c.pct;
    }
    if (pct_sum > 100.0f) pct_sum = 100.0f;
    if (pct_sum > 0 && allow_pct) {
        lay_lu from = 0;
        if (non_pct != 0) from = pct_sum == 100.0f ? LAY_TMAX : lay_lu_f((100.0f / (100.0f - pct_sum)) * lay_lu_fl(non_pct));
        b = lay_lu_max(b, from);
        b = lay_lu_max(b, estimate);
    }
    b = lay_lu_max(a, b);
    *mn = a + undist;
    *mx = b + undist;
}
/* the table's captions' widest min-content width, their margins about it (Blink's ComputeCaptionConstraint) */
static lay_lu lay_tcaps_min(LCTX, const vector<uint32_t> *caps, double cw) {
    lay_lu m = 0;
    for (size_t k = 0; k < len(*caps); k++) {
        double a, b;
        lay_intrinsic(LARGS, at(*caps, k), cw, &a, &b);
        m = lay_lu_max(m, lay_lu_ceil(a));
    }
    return m;
}
/* a table's parts, its borders and its columns' constraints */
static void lay_table_model(LCTX, uint32_t el, const lay_style *s, double cw, LTM) {
    memset(ti, 0, sizeof *ti);
    lay_ext te = LAY_X(el);
    ti->collapse = te.collapse;
    ti->fixed = te.tfixed && s->width.unit != LAY_AUTO && !lay_content_sized(s->width);
    ti->hs = ti->collapse ? 0 : lay_lu_d(te.hs);
    ti->vs = ti->collapse ? 0 : lay_lu_d(te.vs);
    lay_table_parts(LARGS, el, LTA);
    lay_tcol_cons(LARGS, LTA);
    lay_table_borders(LARGS, s, cw, LTA);
    lay_table_columns(LARGS, cw, LTA);
}

/* a table's border-box min- and max-content widths (Blink's TableLayoutAlgorithm::ComputeMinMaxSizes): its grid's, at
   least its captions'; a fixed table of a percentage width's maximum the largest a table takes */
static void lay_table_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    vector<uint32_t> caps_v;
    vector<lay_tcol> tcols_v;
    vector<lay_tsec> secs_v;
    vector<lay_trow> rows_v;
    vector<lay_cell> cells_v;
    vector<lay_tcon> con_v;
    vector<lay_tedge> edges_v;
    lay_tinfo info;
    lay_table_model(LARGS, el, &s, cw, &caps_v, &tcols_v, &secs_v, &rows_v, &cells_v, &con_v, &edges_v, &info);
    /* percentages count in a table's own widths, but not inside a cell, a flex or a grid container (Blink's
       AllowColumnPercentages) */
    int allow = 1;
    for (uint32_t p = d->node[el].parent; p > 1 && allow; p = d->node[p].parent) {
        if (d->node[p].kind != DOM_NODE_KIND_ELEMENT) continue;
        if (lay_display_of(LARGS, p) == D_CELL || LAY_X(p).lx != LX_FLOW) allow = 0;
    }
    lay_lu a, b;
    lay_tgrid_minmax(&con_v, lay_tundist(&con_v, &info), info.fixed, allow, &a, &b);
    lay_lu cap = lay_tcaps_min(LARGS, &caps_v, cw);
    a = lay_lu_max(a, cap);
    b = lay_lu_max(b, cap);
    if (info.fixed && s.width.unit == LAY_PCT) b = LAY_TMAX;
    *mn = lay_lu_px(a);
    *mx = lay_lu_px(b);
}

/* ---- a table's rows ---- */

/* what rows r0 .. r0 + n lack of size shared out to them (Blink's DistributeExcessBlockSizeToRows): to the rows a cell
   spanning rows begins in (a rowspan's share, but its first row), else to the rows of a size their cells give them in
   proportion to it, else to the empty ones (all of them empty, or the others constrained; a rowspan's to the last
   empty one), else to all in proportion */
static void lay_trows_grow(vector<lay_trow> *rows, uint32_t r0, uint32_t n, lay_lu want, int rowspan, lay_lu vs) {
    if (!n) return;
    lay_lu total = 0, unc_total = 0;
    uint32_t nconstrained = 0;
    vector<uint32_t> spans, unc, empty, nonempty, unc_empty;
    for (uint32_t i = r0; i < r0 + n; i++) {
        lay_trow r = at(*rows, i);
        total += r.h;
        if (rowspan && i != r0 && r.rspan_start) push_back(spans, i);
        if (r.h == 0) { push_back(empty, i); if (!r.constrained) push_back(unc_empty, i); }
        else {
            push_back(nonempty, i);
            if (r.constrained) nconstrained++;
            else { push_back(unc, i); unc_total += r.h; }
        }
    }
    lay_lu dist = (want - vs * (lay_lu)(n - 1)) - total;
    if (dist <= 0) return;
    lay_lu rest = dist;
    if (len(spans)) {
        for (size_t k = 0; k < len(spans); k++) {
            lay_lu delta = dist / (lay_lu)len(spans);
            (*rows)[at(spans, k)].h = at(*rows, at(spans, k)).h + delta;
            rest -= delta;
        }
        uint32_t last = at(spans, len(spans) - 1);
        (*rows)[last].h = lay_lu_max(at(*rows, last).h + rest, 0);
        return;
    }
    if (len(unc)) {
        for (size_t k = 0; k < len(unc); k++) {
            lay_lu delta = lay_lu_muldiv(dist, at(*rows, at(unc, k)).h, unc_total);
            (*rows)[at(unc, k)].h = at(*rows, at(unc, k)).h + delta;
            rest -= delta;
        }
        uint32_t last = at(unc, len(unc) - 1);
        (*rows)[last].h = at(*rows, last).h + rest;
        return;
    }
    if (len(empty)) {
        int only_empty = len(empty) == n;
        if (rowspan) {
            if (only_empty) {
                uint32_t last = at(empty, len(empty) - 1);
                (*rows)[last].h = at(*rows, last).h + dist;
                return;
            }
        } else if (only_empty || len(empty) + nconstrained == n) {
            int use_unc = len(unc_empty) > 0;
            size_t ng = use_unc ? len(unc_empty) : len(empty);
            for (size_t k = 0; k < ng; k++) {
                uint32_t i = use_unc ? at(unc_empty, k) : at(empty, k);
                lay_lu delta = dist / (lay_lu)ng;
                (*rows)[i].h = delta;
                rest -= delta;
            }
            uint32_t last = use_unc ? at(unc_empty, ng - 1) : at(empty, ng - 1);
            (*rows)[last].h = at(*rows, last).h + rest;
            return;
        }
    }
    if (len(nonempty)) {
        for (size_t k = 0; k < len(nonempty); k++) {
            lay_lu delta = lay_lu_muldiv(dist, at(*rows, at(nonempty, k)).h, total);
            (*rows)[at(nonempty, k)].h = at(*rows, at(nonempty, k)).h + delta;
            rest -= delta;
        }
        uint32_t last = at(nonempty, len(nonempty) - 1);
        (*rows)[last].h = at(*rows, last).h + rest;
    }
}

/* a table's height shared among its row groups (Blink's DistributeTableBlockSizeToSections): what is left after the
   spacing about them, the bodies first (else the head and foot), the automatic ones (else the fixed, else the
   percentage ones; percentages are not read here) in proportion to their heights, each one's share then its rows' */
static void lay_tsecs_grow(vector<lay_tsec> *secs, vector<lay_trow> *rows, lay_lu height, lay_lu vs) {
    size_t n = len(*secs);
    if (!n) return;
    lay_lu avail = height - (lay_lu)(n + 1) * vs;
    if (avail < 0) avail = 0;
    lay_lu minsum = 0;
    int bodies = 0;
    for (size_t k = 0; k < n; k++) { minsum += at(*secs, k).h; if (at(*secs, k).body) bodies = 1; }
    if (avail <= minsum) return;
    /* the sections that grow: the bodies (when there are any) of an automatic height, else those of a height of their own */
    vector<uint32_t> grow;
    lay_lu gsum = 0;
    for (int pass = 0; pass < 2 && !len(grow); pass++)
        for (size_t k = 0; k < n; k++) {
            lay_tsec sg = at(*secs, k);
            if ((bodies && !sg.body) || (pass == 0 && sg.constrained)) continue;
            push_back(grow, (uint32_t)k);
            gsum += sg.h;
        }
    lay_lu dist = avail - minsum, rest = dist;
    vector<uint8_t> grew;
    resize(&grew, n);
    for (size_t q = 0; q < len(grow); q++) {
        uint32_t k = at(grow, q);
        lay_lu delta = gsum > 0 ? lay_lu_muldiv(dist, at(*secs, k).h, gsum) : dist / (lay_lu)len(grow);
        (*secs)[k].h = at(*secs, k).h + delta;
        rest -= delta;
        grew[k] = 1;
    }
    if (len(grow)) {
        uint32_t last = at(grow, len(grow) - 1);
        (*secs)[last].h = at(*secs, last).h + rest;
    }
    for (size_t k = 0; k < n; k++) {
        if (!at(grew, k)) continue;
        lay_tsec sg = at(*secs, k);
        lay_trows_grow(rows, sg.row0, sg.nrows, sg.h, 0, vs);
    }
}

/* a box's height as a LayoutUnit (to the nearest 64th) */
static lay_lu lay_lu_round(double v) { return (lay_lu)floor(v * 64.0 + 0.5); }

/* ---- a collapsing table's edges, drawn as Blink's TablePainter::PaintCollapsedBorders draws them ---- */

/* which of two edges (by index; out of the grid: none) draws a joint they share (Blink's CompareForPaint): 1 a, -1 b, 0
   both: the one drawn, then the wider, then the style later in Blink's order, then the earlier box */
static int lay_tjoint_cmp(const vector<lay_tedge> *edges, int64_t a, int64_t b) {
    if (a == b) return 0;
    int ap = lay_tedge_paints(edges, a), bp = lay_tedge_paints(edges, b);
    if (ap && bp) {
        lay_tedge ea = at(*edges, (size_t)a), eb = at(*edges, (size_t)b);
        if (ea.w > eb.w) return 1;
        if (ea.w < eb.w) return -1;
        if (ea.st == eb.st) return ea.order < eb.order ? 1 : ea.order > eb.order ? -1 : 0;
        if (eb.st == 1) return 1;
        if (ea.st == 1) return -1;
        return ea.st > eb.st ? 1 : -1;
    }
    if (!ap && !bp) return 0;
    return ap ? 1 : -1;
}
/* the joints at an edge's two ends (Blink's ComputeEdgeJoints): the inline and block sizes of each, and whether the edge
   draws it */
static void lay_tjoints(const vector<lay_tedge> *edges, int64_t e, int64_t epr, int inl, lay_lu *sj, lay_lu *ej, int *sw, int *ew) {
    int64_t n = (int64_t)len(*edges);
    int64_t bs, as, os, us, be, ae, oe, ue;
    if (inl) { bs = e - 2; as = e; os = e - (epr + 1); us = e - 1; be = e; ae = e + 2; oe = e - (epr - 1); ue = e + 1; }
    else { bs = e - 1; as = e + 1; os = e - epr; us = e; be = e + epr - 1; ae = e + epr + 1; oe = e; ue = e + epr; }
    int64_t ids[8] = { bs, as, os, us, be, ae, oe, ue };
    for (int k = 0; k < 8; k++) if (ids[k] < 0 || ids[k] >= n) ids[k] = -1;
    bs = ids[0]; as = ids[1]; os = ids[2]; us = ids[3]; be = ids[4]; ae = ids[5]; oe = ids[6]; ue = ids[7];
    /* sj, ej: [0] the joint's inline size, [1] its block size */
    int ic = lay_tjoint_cmp(edges, bs, as);
    sj[1] = ic == 1 ? lay_tedge_w(edges, bs) : lay_tedge_w(edges, as);
    int bc = lay_tjoint_cmp(edges, os, us);
    sj[0] = bc == 1 ? lay_tedge_w(edges, os) : lay_tedge_w(edges, us);
    int ivb = lay_tjoint_cmp(edges, ic == 1 ? bs : as, bc == 1 ? os : us);
    *sw = inl ? (ivb != -1 && ic != 1) : (ivb != 1 && bc != 1);
    ic = lay_tjoint_cmp(edges, be, ae);
    ej[1] = ic == 1 ? lay_tedge_w(edges, be) : lay_tedge_w(edges, ae);
    bc = lay_tjoint_cmp(edges, oe, ue);
    ej[0] = bc == 1 ? lay_tedge_w(edges, oe) : lay_tedge_w(edges, ue);
    ivb = lay_tjoint_cmp(edges, ic == 1 ? be : ae, bc == 1 ? oe : ue);
    *ew = inl ? (ivb != -1 && ic != -1) : (ivb != 1 && bc != -1);
}
/* the edges' rectangles, in the order Blink draws them (its row groups in turn, each its rows' edges, a row's column
   edge and row edge by column; an edge between two groups the first's), each from the table's border box's top left
   (cx: the columns' lines from the content box's left; the content box at bl, bt in it), its side, style and colour:
   the table box's cborders */
static void lay_table_cborders(LCTX, uint32_t b, const vector<lay_tedge> *edges, const lay_tinfo *ti, const vector<lay_tsec> *secs,
                               const vector<lay_trow> *rows, const vector<lay_lu> *cx) {
    vector<char> out;
    int64_t epr = ti->epr, ne = (int64_t)len(*edges);
    if (!epr || !ne) return;
    int64_t painted = -1;
    for (size_t k = 0; k < len(*secs); k++) {
        lay_tsec sg = at(*secs, k);
        if (!sg.nrows) continue;
        /* the group's rows' offsets in it, and its end */
        vector<lay_lu> ro;
        for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) push_back(ro, at(*rows, r).y);
        push_back(ro, sg.h);
        for (int64_t e = (int64_t)sg.row0 * epr; e < ne; e++) {
            int64_t trow = e / epr, tcol = e % epr / 2, frow = trow - (int64_t)sg.row0;
            if (frow >= (int64_t)len(ro)) { painted = trow - 1; break; }
            if (!lay_tedge_paints(edges, e)) continue;
            lay_tedge ed = at(*edges, (size_t)e);
            int inl = e % epr % 2 != 0;
            lay_lu x0, y0, w0, h0, sj[2], ej[2];
            int sw, ew;
            if (inl) {
                if (tcol + 1 >= (int64_t)len(*cx)) continue;
                if (painted >= 0 && painted == trow) continue;
                x0 = at(*cx, (size_t)tcol);
                w0 = at(*cx, (size_t)tcol + 1) - x0;
                y0 = at(ro, (size_t)frow) - ed.w / 2;
                h0 = ed.w;
                lay_tjoints(edges, e, epr, 1, sj, ej, &sw, &ew);
                if (sw) { x0 -= sj[0] / 2; w0 += sj[0] / 2; }
                else { x0 += sj[0] / 2; w0 -= sj[0] / 2; }
                if (ew) w0 += ej[0] / 2;
                else w0 -= ej[0] / 2;
            } else {
                if (frow + 1 >= (int64_t)len(ro)) continue;
                y0 = at(ro, (size_t)frow);
                h0 = at(ro, (size_t)frow + 1) - y0;
                x0 = at(*cx, (size_t)tcol) - ed.w / 2;
                w0 = ed.w;
                lay_tjoints(edges, e, epr, 0, sj, ej, &sw, &ew);
                if (sw) { y0 -= sj[1] / 2; h0 += sj[1] / 2; }
                else { y0 += sj[1] / 2; h0 -= sj[1] / 2; }
                if (ew) h0 += ej[1] / 2;
                else h0 -= ej[1] / 2;
            }
            /* from the border box: the group at the content box's left, its offset down it */
            double px = lay_lu_px(x0 + ti->tb[3]), py = lay_lu_px(y0 + sg.y + ti->tb[0]);
            char line[160];
            int ln = snprintf(line, sizeof line, "%.10gpx %.10gpx %.10gpx %.10gpx %d %d #%08x\n", px, py, lay_lu_px(w0), lay_lu_px(h0),
                              inl ? 0 : 3, (int)lay_cstyle_bs(ed.st), ed.color);
            if (ln > 0 && (size_t)ln < sizeof line) append_range(out, line, (size_t)ln);
        }
    }
    if (len(out)) t->box[b].cborders = text_add(t, data(out), len(out));
}

/* ---- a table laid out ---- */

/* shift a laid-out box's content down by dy: its lines and child boxes */
static void lay_shift_content(layout_tree *t, uint32_t b, double dy) {
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) t->line[ln].y += dy;
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) t->box[c].y += dy;
}

/* the table's parts' boxes laid out: its width (Blink's ComputeAssignableTableInlineSize: its own, else fit-content
   between its grid's min- and max-content widths, held by min-width and max-width, at least its captions' and its
   grid's min-content) shared among its columns and the spacing; its captions laid out at its border box's width, the
   top ones above it and the bottom ones below; its cols' and colgroups' boxes over the row groups; each row group's,
   row's and cell's box, a cell at its columns' width (its borders a collapsing table's halves of its edges) and its
   rows as tall as their cells and their own heights ask, a cell spanning rows sharing out what they lack, the table's
   height its groups' bodies'; a cell's content where its vertical-align puts it; a collapsing table's edges for paint */
static void lay_table(LCTX, uint32_t b, uint32_t el, const lay_style *s, double cw) {
    vector<uint32_t> caps_v;
    vector<lay_tcol> tcols_v;
    vector<lay_tsec> secs_v;
    vector<lay_trow> rows_v;
    vector<lay_cell> cells_v;
    vector<lay_tcon> con_v;
    vector<lay_tedge> edges_v;
    lay_tinfo info;
    lay_table_model(LARGS, el, s, cw, &caps_v, &tcols_v, &secs_v, &rows_v, &cells_v, &con_v, &edges_v, &info);
    if (info.collapse) {
        /* its borders the halves of its outer edges, none drawn (paint draws the edges); no padding */
        t->box[b].bt = lay_lu_px(info.tb[0]);
        t->box[b].br = lay_lu_px(info.tb[1]);
        t->box[b].bb = lay_lu_px(info.tb[2]);
        t->box[b].bl = lay_lu_px(info.tb[3]);
        t->box[b].pt = t->box[b].pr = t->box[b].pb = t->box[b].pl = 0;
        t->box[b].bst = t->box[b].bsr = t->box[b].bsb = t->box[b].bsl = LAY_BS_NONE;
        t->box[b].bct = t->box[b].bcr = t->box[b].bcb = t->box[b].bcl = 0;
    }
    lay_lu bpi = info.tb[1] + info.tb[3] + info.tp[1] + info.tp[3];
    lay_lu undist = lay_tundist(&con_v, &info), gmin, gmax;
    lay_tgrid_minmax(&con_v, undist, info.fixed, s->width.unit != LAY_MAXC, &gmin, &gmax);
    lay_lu capmin = lay_tcaps_min(LARGS, &caps_v, cw);
    /* the table's used border-box width */
    double ml = s->m[3].unit == LAY_AUTO ? 0 : t->box[b].ml, mr = s->m[1].unit == LAY_AUTO ? 0 : t->box[b].mr;
    lay_lu used;
    int specified = s->width.unit != LAY_AUTO && !lay_content_sized(s->width);
    if (specified) {
        double wv = lay_res(st, s->width, cw) + (s->border_box ? 0 : lay_lu_px(bpi));
        used = lay_lu_d(wv);
        t->box[b].how = s->width.unit != LAY_PX ? LAYOUT_WIDTH_HOW_PERCENT : LAYOUT_WIDTH_HOW_DECLARED;
    } else {
        lay_lu avail = lay_lu_d(cw - ml - mr);
        used = s->width.unit == LAY_MINC ? gmin : s->width.unit == LAY_MAXC ? gmax : lay_lu_min(lay_lu_max(avail, gmin), gmax);
        t->box[b].how = LAYOUT_WIDTH_HOW_SHRINK_TO_FIT;
    }
    if (s->maxw.unit == LAY_PX || s->maxw.unit == LAY_PCT || s->maxw.unit == LAY_CALC) {
        lay_lu mw = lay_lu_d(lay_res(st, s->maxw, cw) + (s->border_box ? 0 : lay_lu_px(bpi)));
        if (used > mw) { used = mw; t->box[b].how = LAYOUT_WIDTH_HOW_MAX_WIDTH; }
    }
    lay_len mnl = LAY_X(el).minw;
    if (mnl.unit == LAY_PX || mnl.unit == LAY_PCT || mnl.unit == LAY_CALC) {
        lay_lu mw = lay_lu_d(lay_res(st, mnl, cw) + (s->border_box ? 0 : lay_lu_px(bpi)));
        if (used < mw) { used = mw; t->box[b].how = LAYOUT_WIDTH_HOW_MIN_WIDTH; }
    }
    used = lay_lu_max(used, gmin);
    used = lay_lu_max(used, capmin);
    lay_lu assignable = used - undist > 0 ? used - undist : 0;
    /* the columns' sizes and places (Blink's ComputeLocationsFromColumns: an empty column that may merge takes no spacing) */
    vector<lay_lu> sizes, coff, csz;
    size_t nc = len(con_v);
    if (nc) {
        if (info.fixed) lay_tsync_fixed(assignable, &con_v, &sizes);
        else lay_tdist_auto(assignable, &con_v, 0, nc, 1, &sizes);
    }
    lay_lu off = info.hs;
    int first = 1;
    for (size_t i = 0; i < nc; i++) {
        lay_lu sz = at(sizes, i);
        if (at(con_v, i).mergeable && sz == 0) { push_back(coff, off); push_back(csz, 0); continue; }
        if (!first) off += info.hs;
        first = 0;
        push_back(coff, off);
        push_back(csz, sz);
        off += sz;
    }
    lay_lu tw;
    if (nc) tw = lay_lu_max(off + bpi + info.hs, capmin);
    else if (specified || mnl.unit != LAY_AUTO) tw = assignable + undist;
    else if (capmin) tw = lay_lu_max(capmin, bpi);
    else tw = info.collapse ? 0 : assignable + bpi;
    /* a grid with no columns has no spacing */
    lay_lu hs = nc ? info.hs : 0, vs = nc ? info.vs : 0;
    double bw = lay_lu_px(tw);
    t->box[b].w = bw - lay_lu_px(bpi) > 0 ? bw - lay_lu_px(bpi) : 0;
    double rest = cw - bw;
    if (s->m[3].unit == LAY_AUTO && s->m[1].unit == LAY_AUTO && rest > 0) { ml = rest / 2; mr = rest / 2; }
    else if (s->m[3].unit == LAY_AUTO) ml = rest - mr;
    else mr = rest - ml;
    t->box[b].ml = ml;
    t->box[b].mr = mr;
    double cbw = t->box[b].w;                      /* the content box's width: what the cells' percentages take */
    /* the captions: the top ones now, above the border box */
    double top = 0;
    vector<uint32_t> capb;                         /* each caption's box */
    for (size_t k = 0; k < len(caps_v); k++) {
        uint32_t cap = at(caps_v, k);
        if (LAY_X(cap).capb) { push_back(capb, 0u); continue; }
        lay_mc cmt, cmb;
        int empty;
        lay_bk bk;
        memset(&bk, 0, sizeof bk);
        bk.ch = -1;
        bk.fw = -1;
        uint32_t cb = lay_block(LARGS, b, cap, bw, 0, 0, 0, 0, &bk, &cmt, &cmb, &empty);
        push_back(capb, cb);
        top += t->box[cb].mt + lay_border_height(t, cb) + t->box[cb].mb;
    }
    t->box[b].cap_top = top;
    /* the cols' and colgroups' boxes (their places once the groups have theirs) */
    for (size_t k = 0; k < len(tcols_v); k++) {
        lay_tcol tc = at(tcols_v, k);
        if (tc.col0 >= nc) continue;
        uint32_t parent = tc.group >= 0 ? at(tcols_v, (size_t)tc.group).box : b;
        if (!parent) continue;
        lay_style ks;
        lay_style_of(LARGS, tc.el, &ks);
        uint32_t kb = lay_new_box(LARGS, parent, tc.el, LAYOUT_BOX_KIND_COLUMN);
        t->box[kb].background = ks.bg;
        lay_box_style(LARGS, kb, tc.el, cbw, -1);
        t->box[kb].how = LAYOUT_WIDTH_HOW_COLUMN;
        tcols_v[k].box = kb;
    }
    /* the row groups', rows' and cells' boxes; each cell laid out in its columns, its height measured */
    for (size_t k = 0; k < len(secs_v); k++) {
        lay_tsec sg = at(secs_v, k);
        uint32_t gb = lay_new_box(LARGS, b, sg.el, LAYOUT_BOX_KIND_ROW_GROUP);
        if (sg.el) {
            lay_style gs;
            lay_style_of(LARGS, sg.el, &gs);
            t->box[gb].background = gs.bg;
            lay_box_style(LARGS, gb, sg.el, cbw, -1);
            lay_len gh = LAY_X(sg.el).hraw;
            secs_v[k].constrained = (uint8_t)(gh.unit == LAY_PX || gh.unit == LAY_PCT);
        }
        t->box[gb].how = LAYOUT_WIDTH_HOW_FILL;
        secs_v[k].box = gb;
        for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) {
            uint32_t row_el = at(rows_v, r).el;
            uint32_t rb = lay_new_box(LARGS, gb, row_el, LAYOUT_BOX_KIND_ROW);
            lay_style rs;
            lay_style_of(LARGS, row_el, &rs);
            t->box[rb].background = rs.bg;
            lay_box_style(LARGS, rb, row_el, cbw, -1);
            t->box[rb].how = LAYOUT_WIDTH_HOW_FILL;
            rows_v[r].box = rb;
            /* a row's own fixed height (a percentage's is not read) */
            lay_len rh = LAY_X(row_el).hraw;
            if (rh.unit == LAY_PX) { rows_v[r].h = lay_lu_d(rh.v); rows_v[r].constrained = 1; }
            else if (rh.unit == LAY_PCT) rows_v[r].constrained = 1;
        }
    }
    /* each rowspan cell: its first row, its rows, the least height it asks */
    vector<uint32_t> spanned;
    vector<lay_lu> span_min;
    for (size_t ci = 0; ci < len(cells_v); ci++) {
        lay_cell c = at(cells_v, ci);
        size_t endc = c.col + c.span - 1 < nc ? c.col + c.span - 1 : (nc ? nc - 1 : 0);
        lay_lu span_w = nc ? at(coff, endc) + at(csz, endc) - at(coff, c.col) : 0;
        lay_style cs;
        lay_style_of(LARGS, c.el, &cs);
        uint32_t cb = lay_new_box(LARGS, at(rows_v, c.row).box, c.el, LAYOUT_BOX_KIND_CELL);
        lay_edges(t, st, cb, &cs, cbw);
        lay_box_style(LARGS, cb, c.el, cbw, -1);
        t->box[cb].mt = t->box[cb].mr = t->box[cb].mb = t->box[cb].ml = 0;
        t->box[cb].bt = lay_lu_px(c.b[0]);
        t->box[cb].br = lay_lu_px(c.b[1]);
        t->box[cb].bb = lay_lu_px(c.b[2]);
        t->box[cb].bl = lay_lu_px(c.b[3]);
        if (info.collapse) {
            t->box[cb].bst = t->box[cb].bsr = t->box[cb].bsb = t->box[cb].bsl = LAY_BS_NONE;
            t->box[cb].bct = t->box[cb].bcr = t->box[cb].bcb = t->box[cb].bcl = 0;
        }
        double sw = lay_lu_px(span_w);
        t->box[cb].w = sw - t->box[cb].bl - t->box[cb].br - t->box[cb].pl - t->box[cb].pr;
        if (t->box[cb].w < 0) t->box[cb].w = 0;
        t->box[cb].how = LAYOUT_WIDTH_HOW_COLUMN;
        t->box[cb].avail = sw;
        lay_mc et, eb;
        int empty;
        int cf = lay_frame(cs.wm), tf = lay_frame(t->box[b].wm);
        if ((cf == 0) != (tf == 0)) {
            /* a cell of the other axis (CSS Writing Modes 4, 7.3): laid out in its own frame, its inline size its
               content's fitted in the viewport's, its block size its column's width; its edges turned into the table's */
            double mn, mx, ie = t->box[cb].bl + t->box[cb].br + t->box[cb].pl + t->box[cb].pr;
            lay_intrinsic(LARGS, c.el, cf ? lay_icb_h : lay_icb_w, &mn, &mx);
            double avail = (cf ? lay_icb_h : lay_icb_w) - ie, iw = mx - ie;
            if (iw > avail) iw = avail > mn - ie ? avail : mn - ie;
            t->box[cb].w = iw > 0 ? iw : 0;
            lay_flow_children(LARGS, cb, c.el, 1, 1, 0, 0, -1, 0, &et, &eb, &empty);
            double bs = sw - t->box[cb].bt - t->box[cb].pt - t->box[cb].pb - t->box[cb].bb;
            t->box[cb].h = bs > 0 ? bs : 0;
            lay_turn_edges(t, cb, cf, tf);
            t->box[cb].how = LAYOUT_WIDTH_HOW_COLUMN;
        } else lay_flow_children(LARGS, cb, c.el, 1, 1, 0, 0, -1, 0, &et, &eb, &empty);
        /* empty-cells: hide, separated borders, nothing in its flow (no line, no box, no element but one of display none
           or out of the flow: an empty inline element counts, as it makes Blink a line): its own decorations not drawn
           (Blink's HideTableCellIfEmpty) */
        int hide = !info.collapse && LAY_X(c.el).ehide && !t->box[cb].first_line && !t->box[cb].first_child;
        for (uint32_t k = dom_flat_first(d, c.el); k && hide; k = dom_flat_next(d, k)) {
            if (d->node[k].kind != DOM_NODE_KIND_ELEMENT || lay_display_of(LARGS, k) == D_NONE) continue;
            int kp = LAY_X(k).pos;
            if (kp != POS_ABSOLUTE && kp != POS_FIXED) hide = 0;
        }
        t->box[cb].cell_hidden = (uint8_t)hide;
        cells_v[ci].box = cb;
        lay_lu vbp = lay_lu_d(t->box[cb].bt + t->box[cb].bb + t->box[cb].pt + t->box[cb].pb);
        lay_lu cell_h = lay_lu_round(t->box[cb].h) + vbp;
        /* its own height: a length's border box (its border box's in quirks mode, the table cell height box sizing quirk,
           the Quirks Mode Standard 3.13) */
        lay_len chl = LAY_X(c.el).hraw;
        lay_lu css_h = 0;
        int css_fixed = chl.unit == LAY_PX, css_pct = chl.unit == LAY_PCT;
        if (css_fixed) {
            lay_lu hv = lay_lu_d(chl.v);
            css_h = lay_quirks || cs.border_box ? lay_lu_max(vbp, hv) : vbp + hv;
        }
        lay_lu need = lay_lu_max(cell_h, css_h);
        cells_v[ci].h = need;
        if (c.rspan == 1) {
            if (css_fixed || css_pct) rows_v[c.row].constrained = 1;
            if (need > at(rows_v, c.row).h) rows_v[c.row].h = need;
        } else {
            push_back(spanned, (uint32_t)ci);
            push_back(span_min, need);
        }
    }
    /* the cells spanning rows, in Blink's order (a cell inside another's rows first; else the lower), each sharing out what
       its rows lack */
    for (size_t k = 1; k < len(spanned); k++) {
        uint32_t x = at(spanned, k);
        lay_lu xm = at(span_min, k);
        size_t q = k;
        while (q > 0) {
            lay_cell a = at(cells_v, at(spanned, q - 1)), c = at(cells_v, x);
            int before;   /* c sorts before a */
            if (a.row == c.row && a.rspan == c.rspan) before = xm > at(span_min, q - 1);
            else if (c.row >= a.row && c.row + c.rspan <= a.row + a.rspan) before = 1;
            else if (a.row >= c.row && a.row + a.rspan <= c.row + c.rspan) before = 0;
            else before = c.row < a.row;
            if (!before) break;
            spanned[q] = at(spanned, q - 1);
            span_min[q] = at(span_min, q - 1);
            q--;
        }
        spanned[q] = x;
        span_min[q] = xm;
    }
    for (size_t k = 0; k < len(spanned); k++) {
        lay_cell c = at(cells_v, at(spanned, k));
        lay_trows_grow(&rows_v, c.row, c.rspan, at(span_min, k), 1, vs);
    }
    /* each group's height: its rows' and the spacing between them; a fixed height of its own shared to them */
    lay_lu min_total = 0;
    for (size_t k = 0; k < len(secs_v); k++) {
        lay_tsec sg = at(secs_v, k);
        lay_lu h = 0;
        for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) h += at(rows_v, r).h;
        if (sg.nrows > 1) h += vs * (lay_lu)(sg.nrows - 1);
        lay_len gh;
        gh.unit = LAY_AUTO;
        if (sg.el) gh = LAY_X(sg.el).hraw;
        if (gh.unit == LAY_PX && lay_lu_d(gh.v) > h) {
            lay_trows_grow(&rows_v, sg.row0, sg.nrows, lay_lu_d(gh.v), 0, vs);
            h = lay_lu_d(gh.v);
        }
        secs_v[k].h = h;
        min_total += h;
    }
    /* the table's own height: its border box's, shared to its groups (quirks mode: a table of no row groups takes none) */
    lay_lu bpb = info.tb[0] + info.tb[2] + info.tp[0] + info.tp[2], minimal = 0;
    lay_len thl = LAY_X(el).hraw;
    int no_groups = len(secs_v) == 0;
    if (thl.unit == LAY_PX && !(lay_quirks && no_groups)) {
        lay_lu hv = lay_lu_d(thl.v);
        minimal = s->border_box ? lay_lu_max(hv, bpb) : hv + bpb;
        lay_lu dist = minimal - bpb > 0 ? minimal - bpb : 0;
        if (dist > min_total) {
            lay_tsecs_grow(&secs_v, &rows_v, dist, vs);
            for (size_t k = 0; k < len(secs_v); k++) {
                lay_tsec sg = at(secs_v, k);
                lay_lu h = 0;
                for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) h += at(rows_v, r).h;
                if (sg.nrows > 1) h += vs * (lay_lu)(sg.nrows - 1);
                if (h > sg.h) secs_v[k].h = h;
            }
        }
    }
    /* the groups placed down the content box, the spacing before each that holds rows (an empty one where the last
       ended) and after the last; the rows down their group */
    lay_lu y = 0, after = 0;
    for (size_t k = 0; k < len(secs_v); k++) {
        lay_tsec sg = at(secs_v, k);
        lay_lu at_y = y;
        if (sg.nrows) { y += vs; at_y = y; }
        after = vs;
        secs_v[k].y = at_y;
        lay_lu ry = 0;
        for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) {
            rows_v[r].y = ry;
            ry += at(rows_v, r).h + vs;
        }
        y = at_y + (sg.nrows ? sg.h : 0);
    }
    lay_lu grid = y + after;
    if (len(secs_v) == 0) grid = 0;
    /* the grid's height: at least the table's own */
    lay_lu content_h = grid;
    if (minimal - bpb > content_h) content_h = minimal - bpb;
    t->box[b].h = lay_lu_px(content_h);
    /* the boxes in their places: a group at the spacing's inset, its rows across it, each cell at its column */
    double gw = cbw - 2 * lay_lu_px(hs) > 0 ? cbw - 2 * lay_lu_px(hs) : 0;
    for (size_t k = 0; k < len(secs_v); k++) {
        lay_tsec sg = at(secs_v, k);
        uint32_t gb = sg.box;
        t->box[gb].w = gw;
        t->box[gb].avail = cbw;
        t->box[gb].h = sg.nrows ? lay_lu_px(sg.h) : 0;
        lay_place(t, gb, lay_lu_px(hs), lay_lu_px(sg.y));
        for (uint32_t r = sg.row0; r < sg.row0 + sg.nrows; r++) {
            uint32_t rb = at(rows_v, r).box;
            t->box[rb].w = gw;
            t->box[rb].avail = gw;
            t->box[rb].h = lay_lu_px(at(rows_v, r).h);
            lay_place(t, rb, 0, lay_lu_px(at(rows_v, r).y));
        }
    }
    for (size_t ci = 0; ci < len(cells_v); ci++) {
        lay_cell c = at(cells_v, ci);
        uint32_t cb = c.box;
        lay_lu full = 0;
        for (uint32_t r = c.row; r < c.row + c.rspan; r++) { full += at(rows_v, r).h; if (r != c.row) full += vs; }
        double inner = lay_lu_px(full) - t->box[cb].bt - t->box[cb].pt - t->box[cb].pb - t->box[cb].bb;
        /* its content where its vertical-align puts it: its own, else its row's, else its row group's (as the user agent's
           sheet has them inherit), else the middle; the top for baseline */
        int va = LAY_X(c.el).va;
        uint32_t row_el = at(rows_v, c.row).el;
        if (va == VA_BASELINE) va = LAY_X(row_el).va;
        if (va == VA_BASELINE && lay_display_of(LARGS, d->node[row_el].parent) == D_ROW_GROUP) va = LAY_X(d->node[row_el].parent).va;
        if (va == VA_BASELINE) va = VA_MIDDLE;
        double dy = va == VA_BOTTOM ? inner - t->box[cb].h : va == VA_TOP ? 0 : (inner - t->box[cb].h) / 2;
        /* (a cell of the other axis keeps its content at its start: its children are in its own frame) */
        int ortho = (lay_frame(t->box[cb].wm) == 0) != (lay_frame(t->box[b].wm) == 0);
        if (dy > 0 && !ortho) lay_shift_content(t, cb, dy);
        t->box[cb].h = inner > 0 ? inner : 0;
        lay_place(t, cb, lay_lu_px(at(coff, c.col) - hs), 0);
    }
    /* the cols' and colgroups' places: their columns across the groups that hold rows */
    lay_lu gy0 = -1, gy1 = 0;
    for (size_t k = 0; k < len(secs_v); k++) {
        lay_tsec sg = at(secs_v, k);
        if (!sg.nrows || !sg.h) continue;
        if (gy0 < 0) gy0 = sg.y;
        gy1 = sg.y + sg.h;
    }
    if (gy0 < 0) gy0 = gy1 = 0;
    for (size_t k = 0; k < len(tcols_v); k++) {
        lay_tcol tc = at(tcols_v, k);
        if (!tc.box) continue;
        size_t c0 = tc.col0, c1 = tc.col0 + (tc.span ? tc.span : 1) - 1;
        if (c1 >= nc) c1 = nc - 1;
        double x = lay_lu_px(at(coff, c0)), w = lay_lu_px(at(coff, c1) + at(csz, c1) - at(coff, c0));
        uint32_t kb = tc.box;
        t->box[kb].w = w;
        t->box[kb].avail = cbw;
        t->box[kb].h = lay_lu_px(gy1 - gy0);
        if (tc.group >= 0) lay_place(t, kb, x - lay_lu_px(at(coff, at(tcols_v, (size_t)tc.group).col0)), 0);
        else lay_place(t, kb, x, lay_lu_px(gy0));
    }
    /* the captions: the bottom ones now, below the border box; then each in its place */
    double below = 0, gbot = t->box[b].h + t->box[b].pb + t->box[b].bb;
    for (size_t k = 0; k < len(caps_v); k++) {
        uint32_t cap = at(caps_v, k);
        if (!LAY_X(cap).capb) continue;
        lay_mc cmt, cmb;
        int empty;
        lay_bk bk;
        memset(&bk, 0, sizeof bk);
        bk.ch = -1;
        bk.fw = -1;
        uint32_t cb = lay_block(LARGS, b, cap, bw, 0, 0, 0, 0, &bk, &cmt, &cmb, &empty);
        capb[k] = cb;
        lay_place(t, cb, t->box[cb].ml - t->box[b].bl - t->box[b].pl, gbot + below + t->box[cb].mt);
        below += t->box[cb].mt + lay_border_height(t, cb) + t->box[cb].mb;
    }
    t->box[b].cap_bottom = below;
    double yt = 0;
    for (size_t k = 0; k < len(caps_v); k++) {
        uint32_t cb = at(capb, k);
        if (!cb || LAY_X(at(caps_v, k)).capb) continue;
        lay_place(t, cb, t->box[cb].ml - t->box[b].bl - t->box[b].pl, yt + t->box[cb].mt - top - t->box[b].bt - t->box[b].pt);
        yt += t->box[cb].mt + lay_border_height(t, cb) + t->box[cb].mb;
    }
    /* a collapsing table's edges, from its columns' lines and its groups' rows */
    if (info.collapse && info.epr) {
        vector<lay_lu> cx;
        for (size_t i = 0; i < nc; i++) push_back(cx, at(coff, i));
        if (nc) push_back(cx, at(coff, nc - 1) + at(csz, nc - 1));
        lay_table_cborders(LARGS, b, &edges_v, &info, &secs_v, &rows_v, &cx);
    }
}
/* ---- block layout ---- */

/* the children of box b (element el's), as blocks or as lines, in b's content box of
   width t->box[b].w; sets b's content height. et and eb: the margins that collapsed
   through b's top and bottom; empty: b holds nothing that takes height */
/* the margins that collapse through el's top, as far as can be seen before it is laid
   out: its own, and (no border or padding above it, no formatting context of its own)
   its first in-flow child's -- where floats are placed from, beside its lines */
static lay_mc lay_peek_mt(LCTX, uint32_t el, double cw, int depth, int qs) {
    lay_style s;
    lay_style_of(LARGS, el, &s);
    /* a quirky margin at a quirky container's start (qs) counts for nothing */
    int q = lay_quirky_margin(LARGS, el, 0);
    lay_mc m = qs && q ? lay_mc_of(0) : lay_mc_q(s.m[0].unit == LAY_AUTO ? 0 : lay_res(st, s.m[0], cw), q);
    if (depth > 6 || s.b[0] > 0 || lay_res(st, s.p[0], cw) > 0 || s.bfc || (s.display != D_BLOCK && s.display != D_LIST_ITEM)) return m;
    if (lay_clip_of(LARGS, el) == 1 || LAY_X(el).lx || LAY_X(el).flt || lay_replaced(d, el)) return m;
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    int qc = qs || lay_quirky_container(LARGS, el, s.display);
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_ABS || sg.block == SEG_FLOAT) continue;
        /* a list item's marker on a line of its own before its first block: the block's margins stay below it */
        if (sg.block == SEG_BLOCK && lay_marker_own_line(LARGS, el, &s)) return m;
        if (sg.block == SEG_BLOCK) return lay_mc_add(m, lay_peek_mt(LARGS, sg.node, cw, depth + 1, qc));
        return m;
    }
    return m;
}

/* a block-level element that makes a formatting context of its own: it goes beside the
   floats, not under them */
static int lay_makes_bfc(LCTX, uint32_t el) {
    if (lay_replaced(d, el)) return 1;
    uint32_t si = lay_si(sx, gx, el);
    return at(*sc, si).bfc || at(*sc, si).display == D_TABLE || lay_clip_of(LARGS, el) == 1 || at(*xc, si).lx != LX_FLOW;
}

/* the children of box b (element el's), as blocks or as lines, in b's content box of
   width t->box[b].w, which is at (ox, oy) in the block formatting context; sets b's
   content height. ch: b's definite height (-1: none), what its children's percentages take.
   qs: b's top margin collapses at a quirky container's start. et and eb: the margins that
   collapsed through b's top and bottom; empty: b holds nothing that takes height */
static void lay_flow_in(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, int qs, lay_mc *et,
                        lay_mc *eb, int *empty);
static void lay_flow(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, int qs, lay_mc *et, lay_mc *eb,
                     int *empty) {
    double keep = lay_line_pch;
    lay_line_pch = ch;
    lay_flow_in(LARGS, b, el, bfc, height_auto, ox, oy, ch, qs, et, eb, empty);
    lay_line_pch = keep;
}
static void lay_flow_in(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, int qs, lay_mc *et,
                        lay_mc *eb, int *empty) {
    double w = t->box[b].w;
    *et = lay_mc_of(0);
    *eb = lay_mc_of(0);
    *empty = 0;
    vector<lay_seg> segs;
    vector<uint32_t> items;
    lay_collect(LARGS, el, &segs, &items);
    int any_block = 0;
    for (size_t k = 0; k < len(segs); k++) if (at(segs, k).block) any_block = 1;
    if (!any_block) {
        /* a container of inline content only: its lines */
        double h = 0;
        if (len(segs)) h = lay_lines(LARGS, b, el, &items, at(segs, 0).first, at(segs, 0).n, w, ox, oy);
        t->box[b].h = h;
        *empty = !t->box[b].first_line;
        return;
    }
    int through_top = !bfc && t->box[b].bt == 0 && t->box[b].pt == 0;
    int through_bottom = !bfc && height_auto && t->box[b].bb == 0 && t->box[b].pb == 0;
    /* quirks mode (Blink's quirky margins): a quirky container (the body, a table cell) leaves out
       the quirky margins at its start -- its children's, and theirs that collapse with them, up to
       its first child that is not empty -- and at its end; a box whose top margin collapses at
       such a start (qs) passes it on to the children whose margins collapse through its top */
    int quirky = el && !(el & LAY_GEN) && lay_quirky_container(LARGS, el, at(*sc, lay_si(sx, gx, el)).display);
    int qstart = quirky || (qs && through_top);
    double cursor = 0;
    lay_mc pending = lay_mc_of(0);
    int first = 1;
    vector<uint32_t> waiting;          /* the floats met before the next in-flow box: placed at its top */
    /* a list item's marker on a line of its own, when its first in-flow box is a block (lay_marker_own_line) */
    int mline = 0;
    if (el && !(el & LAY_GEN) && at(*sc, lay_si(sx, gx, el)).display == D_LIST_ITEM) {
        lay_style ls;
        lay_style_of(LARGS, el, &ls);
        mline = lay_marker_own_line(LARGS, el, &ls);
    }
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_FLOAT) { push_back(waiting, sg.node); continue; }
        if (sg.block == SEG_ABS) {
            /* out of the flow: its static position, where its top margin edge would be */
            lay_abs sa;
            sa.el = sg.node;
            sa.box = b;
            sa.x = 0;
            sa.y = cursor + lay_mc_v(pending);
            push_back(ab, sa);
            continue;
        }
        if (sg.block == SEG_RUN) {
            vector<lay_piece> rps;
            lay_run_pieces(LARGS, el, &items, sg.first, sg.n, w, &rps);
            if (!lay_run_shows(&rps)) {
                /* a run that shows nothing (white space and floats): the floats that waited before it placed first,
                   where its own go, so that floats keep the document's order (CSS 2.1, 9.5.1, rule 5). They waited for
                   the next in-flow box, and the run's own went before them: the first of a row last (Chromium's,
                   measured) */
                double ry = cursor + lay_mc_v(pending);
                for (size_t q = 0; q < len(waiting); q++) {
                    uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
                    lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, ry);
                }
                clear(waiting);
                lay_run_oof(LARGS, b, &rps, w, ox, oy, ry);
                continue;
            }
        }
        if (mline && sg.block == SEG_BLOCK) {
            /* the marker's line, an anonymous block's before the first block: the floats that waited at its top */
            uint32_t mb = lay_marker_box(LARGS, b, el, w, 0);
            double top = (first && through_top) ? 0 : cursor + lay_mc_v(pending);
            for (size_t q = 0; q < len(waiting); q++) {
                uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
                lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, top);
            }
            clear(waiting);
            lay_place(t, mb, 0, top);
            cursor = top + t->box[mb].h;
            pending = lay_mc_of(0);
            first = 0;
        }
        mline = 0;
        /* where the child's top will be, margins collapsed as far as can be seen before it is
           laid out (through its first children when floats are about) */
        lay_mc cm0 = lay_mc_of(0);
        int cq = qstart && first;             /* its top margins collapse at the quirky start */
        if (sg.block == SEG_BLOCK) {
            if (len(*fl) || len(waiting)) cm0 = lay_peek_mt(LARGS, sg.node, w, 0, cq);
            else {
                lay_style cs;
                lay_style_of(LARGS, sg.node, &cs);
                int q = lay_quirky_margin(LARGS, sg.node, 0);
                cm0 = cq && q ? lay_mc_of(0) : lay_mc_q(cs.m[0].unit == LAY_AUTO ? 0 : lay_res(st, cs.m[0], w), q);
            }
        }
        double est = (first && through_top) ? 0 : cursor + lay_mc_v(lay_mc_add(pending, cm0));
        /* the floats that waited: placed at the child's top */
        for (size_t q = 0; q < len(waiting); q++) {
            uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
            lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, est);
        }
        clear(waiting);
        /* clearance: below the floats it clears (those just placed too) */
        double clear_to = -1;
        int clr = sg.block == SEG_BLOCK ? lay_clear_of(LARGS, sg.node) : 0;
        if (clr && len(*fl)) {
            double cy = lay_clear_y(fl, clr) - oy;
            if (cy > est) { est = cy; clear_to = cy; }
        }
        /* a child that makes a formatting context of its own goes beside the floats, narrowed */
        double cx = 0, ccw = w;
        if (sg.block == SEG_BLOCK && len(*fl) && lay_makes_bfc(LARGS, sg.node)) {
            double l, r, next;
            lay_band(fl, ox, w, oy + est, 1, &l, &r, &next);
            cx = l;
            ccw = r - l;
        }
        size_t nf0 = len(*fl);
        lay_mc cmt, cmb;
        int cempty;
        uint32_t cb;
        lay_bk bk;
        bk.ox = ox + cx;
        bk.oy = oy + est;
        bk.ch = ch;
        bk.fw = -1;
        bk.shrink = 0;
        bk.item = 0;
        bk.qs = cq;
        if (sg.block == SEG_BLOCK) cb = lay_block(LARGS, b, sg.node, ccw, 0, 0, 0, 0, &bk, &cmt, &cmb, &cempty);
        else cb = lay_block(LARGS, b, 0, w, &items, sg.first, sg.n, el, &bk, &cmt, &cmb, &cempty);
        if (cempty && clear_to < 0) {
            /* an empty box: its margins collapse through it, into one; its top border
               edge where it would be with a bottom border (CSS 2.1 8.3.1) */
            lay_mc both = lay_mc_add(cmt, cmb);
            double at_y = (first && through_top) ? 0 : cursor + lay_mc_v(lay_mc_add(pending, cmt));
            if (first && through_top) *et = lay_mc_add(*et, both);
            else pending = lay_mc_add(pending, both);
            lay_place(t, cb, cx + t->box[cb].ml, at_y);
            if (at_y != est) for (size_t q = nf0; q < len(*fl); q++) (*fl)[q].y = at(*fl, q).y + at_y - est;
            continue;
        }
        double top;
        if (first && through_top && clear_to < 0) { *et = lay_mc_add(*et, cmt); top = 0; }
        else top = cursor + lay_mc_v(lay_mc_add(pending, cmt));
        if (clear_to > top) top = clear_to;
        lay_place(t, cb, cx + t->box[cb].ml, top);
        /* the floats the child placed, where it really is */
        if (top != est) for (size_t q = nf0; q < len(*fl); q++) (*fl)[q].y = at(*fl, q).y + top - est;
        cursor = top + lay_border_height(t, cb);
        pending = cmb;
        first = 0;
    }
    /* floats with no in-flow box after them: after the last */
    for (size_t q = 0; q < len(waiting); q++) {
        uint32_t fb = lay_float_box(LARGS, b, at(waiting, q), w, ch);
        lay_float_place(LARGS, fb, at(waiting, q), w, ox, oy, cursor + lay_mc_v(pending));
    }
    if (first) {
        /* every child was empty: b is too, unless it is a new formatting context */
        t->box[b].h = 0;
        *empty = !bfc;
        if (*empty) *et = lay_mc_add(*et, pending);
        return;
    }
    if (through_bottom) { *eb = pending; t->box[b].h = cursor; }
    else t->box[b].h = cursor + (quirky ? lay_mc_vq(pending) : lay_mc_v(pending));
}

/* b's children: in the flow b's formatting context is (b's own when bfc: its floats its
   own, its auto height holding them) */
static void lay_flow_children(LCTX, uint32_t b, uint32_t el, int bfc, int height_auto, double ox, double oy, double ch, int qs, lay_mc *et,
                              lay_mc *eb, int *empty) {
    if (!bfc) { lay_flow(LARGS, b, el, 0, height_auto, ox, oy, ch, qs, et, eb, empty); return; }
    vector<lay_flt> bfl;
    lay_flow(LARGS_FL(&bfl), b, el, 1, height_auto, 0, 0, ch, qs, et, eb, empty);
    if (len(bfl)) {
        double bottom = 0;
        for (size_t k = 0; k < len(bfl); k++) if (at(bfl, k).y + at(bfl, k).h > bottom) bottom = at(bfl, k).y + at(bfl, k).h;
        if (height_auto && bottom > t->box[b].h) t->box[b].h = bottom;
        *empty = 0;
    }
}
/* a value kept as text, its cval c's, into the tree's pool: the span paint reads (empty for 0) */
static amb_span lay_kept(layout_tree *t, const css_style *st, uint32_t c) {
    amb_span none;
    memset(&none, 0, sizeof none);
    if (!c) return none;
    vector<char> v;
    amb_span sp = st->cval[c].text;
    append_range(v, text_ptr(st, sp), sp.len);
    return text_add(t, data(v), len(v));
}

/* one radius of text: a percentage as its negative (layout_run takes it of the box's
   final border box: lay_radii), else px */
static double lay_radius_part(const char *s, size_t n, double basis) {
    double x;
    if (n > 1 && s[n - 1] == '%') {
        double p;
        if (css_length_eval(s, n, 100, &p) && p > 0) return -p;
        return 0;
    }
    if (!css_length_eval(s, n, basis, &x) || x < 0) return 0;
    return x;
}
/* a corner's radii: one length (both), or the two of a value kept as text (horizontal,
   vertical). A percentage, of the border box's width across and of its height down, is
   kept as its negative until the box's size is final (lay_radii) */
static void lay_radius(const css_style *st, lay_len l, double bw, double bh, double *h, double *v) {
    if (l.cv && st->cval[l.cv].unit == CSS_UNIT_TEXT) {
        vector<char> tx;
        amb_span sp = st->cval[l.cv].text;
        append_range(tx, text_ptr(st, sp), sp.len);
        size_t i = 0, a0 = 0, n0 = 0, a1 = 0, n1 = 0;
        *h = 0;
        *v = 0;
        if (!css_next_part(data(tx), len(tx), &i, &a0, &n0) || !css_next_part(data(tx), len(tx), &i, &a1, &n1)) return;
        *h = lay_radius_part(data(tx) + a0, n0, bw);
        *v = lay_radius_part(data(tx) + a1, n1, bh);
        return;
    }
    if (l.unit == LAY_PCT) { *h = l.v > 0 ? -l.v : 0; *v = *h; return; }
    *h = lay_res(st, l, bw);
    *v = l.unit == LAY_CALC || l.unit == LAY_EXPR ? lay_res(st, l, bh) : *h;
}

/* the radii kept as percentages taken of each box's final border box */
static void lay_radii(layout_tree *t) {
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live) continue;
        layout_box x = t->box[b];
        if (x.rtl >= 0 && x.rtr >= 0 && x.rbr >= 0 && x.rbl >= 0 && x.rtlv >= 0 && x.rtrv >= 0 && x.rbrv >= 0 && x.rblv >= 0) continue;
        double bw = x.w + x.pl + x.pr + x.bl + x.br, bh = x.h + x.pt + x.pb + x.bt + x.bb;
        if (x.rtl < 0) t->box[b].rtl = -x.rtl * bw / 100;
        if (x.rtr < 0) t->box[b].rtr = -x.rtr * bw / 100;
        if (x.rbr < 0) t->box[b].rbr = -x.rbr * bw / 100;
        if (x.rbl < 0) t->box[b].rbl = -x.rbl * bw / 100;
        if (x.rtlv < 0) t->box[b].rtlv = -x.rtlv * bh / 100;
        if (x.rtrv < 0) t->box[b].rtrv = -x.rtrv * bh / 100;
        if (x.rbrv < 0) t->box[b].rbrv = -x.rbrv * bh / 100;
        if (x.rblv < 0) t->box[b].rblv = -x.rblv * bh / 100;
    }
}

/* what paint reads of a box's style: its radii, opacity, shadows, backgrounds (the image
   layout_run fetched), transforms (3D ones' perspective and context), filters and outline */
static void lay_box_paint(LCTX, uint32_t b, uint32_t si) {
    lay_ext e = at(*xc, si);
    double bw = t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br;
    double bh = t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb;
    double rh, rv;
    lay_radius(st, e.rad[0], bw, bh, &rh, &rv);
    t->box[b].rtl = rh;
    t->box[b].rtlv = rv;
    lay_radius(st, e.rad[1], bw, bh, &rh, &rv);
    t->box[b].rtr = rh;
    t->box[b].rtrv = rv;
    lay_radius(st, e.rad[2], bw, bh, &rh, &rv);
    t->box[b].rbr = rh;
    t->box[b].rbrv = rv;
    lay_radius(st, e.rad[3], bw, bh, &rh, &rv);
    t->box[b].rbl = rh;
    t->box[b].rblv = rv;
    t->box[b].opacity = e.opacity;
    t->box[b].shadows = lay_kept(t, st, e.shadows);
    t->box[b].bg_image = lay_kept(t, st, e.bgimg);
    t->box[b].bg_pos = lay_kept(t, st, e.bgpos);
    t->box[b].ofit = e.ofit;
    t->box[b].irender = e.irender;
    t->box[b].opos = lay_kept(t, st, e.opos);
    t->box[b].cpath = lay_kept(t, st, e.cpath);
    {
        int abs = t->box[b].position == LAYOUT_POSITION_ABSOLUTE || t->box[b].position == LAYOUT_POSITION_FIXED;
        t->box[b].cssclip = lay_kept(t, st, abs ? e.cssclip : 0);
    }
    t->box[b].bg_size = lay_kept(t, st, e.bgsize);
    t->box[b].bg_repeat = lay_kept(t, st, e.bgrepeat);
    t->box[b].bg_clip = lay_kept(t, st, e.bgclip);
    t->box[b].bg_origin = lay_kept(t, st, e.bgorigin);
    t->box[b].transform = lay_kept(t, st, e.xform);
    t->box[b].origin = lay_kept(t, st, e.xorigin);
    t->box[b].filter = lay_kept(t, st, e.filter);
    t->box[b].backdrop = lay_kept(t, st, e.backdrop);
    t->box[b].persp = lay_kept(t, st, e.persp);
    t->box[b].porigin = lay_kept(t, st, e.porigin);
    t->box[b].translate = lay_kept(t, st, e.translate);
    t->box[b].rotate = lay_kept(t, st, e.rotate);
    t->box[b].scale = lay_kept(t, st, e.scale);
    t->box[b].p3d = e.p3d;
    t->box[b].backface = e.backface;
    if (e.bgres) t->box[b].bg_res = resource_handle(nt, e.bgres);
    t->box[b].ow = e.ow;
    t->box[b].ostyle = e.ostyle;
    t->box[b].ocolor = e.ocolor;
    t->box[b].ooffset = e.ooffset;
}

/* an inline element drawn as more than its background colour: with a border, a rounded corner, a background image
   or a box shadow */
static int lay_decorated(LCTX, uint32_t el, const lay_style *es) {
    if (es->b[0] > 0 || es->b[1] > 0 || es->b[2] > 0 || es->b[3] > 0) return 1;
    lay_ext e = LAY_X(el);
    if (!e.paints) return 0;
    if (e.bgimg || e.shadows) return 1;
    for (int k = 0; k < 4; k++) if (e.rad[k].v != 0) return 1;
    return 0;
}
/* the box an inline element's fragment fr draws its decorations from, when it has any (lay_decorated): the element's
   borders, paddings, colours, background layers, corners and shadows, its place left to lay_inline_strips once every
   line is where it ends (and a percentage corner to lay_radii, of the whole strip) */
static void lay_inline_piece(LCTX, uint32_t fr, uint32_t el, const lay_style *es, double cw) {
    if (!lay_decorated(LARGS, el, es)) return;
    layout_box_h h = box_new(t);
    uint32_t b = h.index;
    t->box[b].kind = LAYOUT_BOX_KIND_INLINE;
    t->box[b].opacity = 1;
    t->box[b].node = t->frag[fr].node;
    if (el & LAY_GEN) t->box[b].pseudo = (uint8_t)at(*gx, el & LAY_GIDX).pseudo;
    lay_edges(t, st, b, es, cw);
    t->box[b].mt = 0;
    t->box[b].mr = 0;
    t->box[b].mb = 0;
    t->box[b].ml = 0;
    double ew = t->box[b].bl + t->box[b].pl + t->box[b].pr + t->box[b].br, ev = t->box[b].bt + t->box[b].pt + t->box[b].pb + t->box[b].bb;
    t->box[b].w = t->frag[fr].w > ew ? t->frag[fr].w - ew : 0;
    t->box[b].h = t->frag[fr].h > ev ? t->frag[fr].h - ev : 0;
    uint32_t si = lay_si(sx, gx, el);
    if (at(*xc, si).paints) lay_box_paint(LARGS, b, si);
    t->box[b].clone = at(*xc, si).clone != 0;
    t->box[b].hidden = at(*xc, si).hidden != 0;
    t->frag[fr].piece = b;
}
/* each decorated inline element's fragments, in the order they were made (line by line), as one strip laid end to end
   (CSS Backgrounds 3, box-decoration-break: slice): each fragment's box is the whole strip, its content box from the
   strip's start, placed so that the fragment's part of the strip lies on the fragment, its edges at whole pixels; a
   cloned element's (box-decoration-break: clone) each its own fragment's, wrapped in every edge */
static void lay_inline_strips(layout_tree *t) {
    vector<uint32_t> frs;
    for (uint32_t fr = 1; fr < t->frag_len; fr++)
        if (t->frag[fr].live && t->frag[fr].kind == LAYOUT_FRAG_KIND_INLINE && t->frag[fr].piece) push_back(frs, fr);
    vector<uint8_t> done;
    resize(done, len(frs), 0);
    vector<uint32_t> group;
    for (size_t i = 0; i < len(frs); i++) {
        if (at(done, i)) continue;
        uint32_t b0 = t->frag[at(frs, i)].piece;
        uint32_t node = t->box[b0].node.index;
        uint8_t ps = t->box[b0].pseudo;
        clear(group);
        double total = 0;
        for (size_t j = i; j < len(frs); j++) {
            uint32_t bj = t->frag[at(frs, j)].piece;
            if (at(done, j) || t->box[bj].node.index != node || t->box[bj].pseudo != ps) continue;
            if (t->box[b0].clone && j != i) continue;
            done[j] = 1;
            push_back(group, at(frs, j));
            total += t->frag[at(frs, j)].w;
        }
        double off = 0;
        uint32_t ln0 = t->frag[at(group, 0)].line, lb0 = ln0 ? t->line[ln0].box : 0;
        int vf = lb0 ? lay_frame(t->box[lb0].wm) : 0;
        if (vf) {
            /* in a vertical line: its edges turned (laid out in its line's frame), its strip down the line (sideways-lr's up
               it), its fragments laid end to end along it */
            if (vf == 3) total = 0;
            for (size_t q = 0; q < len(group); q++) {
                uint32_t fr = at(group, q), b = t->frag[fr].piece;
                lay_turn_sides(t, b, vf, 0);
                double top = t->box[b].bt + t->box[b].pt, ev = top + t->box[b].pb + t->box[b].bb;
                double left = t->box[b].bl + t->box[b].pl, ew = left + t->box[b].pr + t->box[b].br;
                double len0 = 0;
                for (size_t z = 0; z < len(group); z++) len0 += t->frag[at(group, z)].h;
                double y0 = vf == 3 ? floor(t->frag[fr].y + t->frag[fr].h + off - len0 + 0.5) : floor(t->frag[fr].y - off + 0.5);
                double y1 = y0 + floor(len0 + 0.5);
                double x0 = floor(t->frag[fr].x + 0.5), x1 = floor(t->frag[fr].x + t->frag[fr].w + 0.5);
                t->box[b].x = x0 + left;
                t->box[b].y = y0 + top;
                t->box[b].w = x1 - x0 > ew ? x1 - x0 - ew : 0;
                t->box[b].h = y1 - y0 > ev ? y1 - y0 - ev : 0;
                off += t->frag[fr].h;
            }
            continue;
        }
        for (size_t q = 0; q < len(group); q++) {
            uint32_t fr = at(group, q), b = t->frag[fr].piece;
            double left = t->box[b].bl + t->box[b].pl, ew = left + t->box[b].pr + t->box[b].br;
            double top = t->box[b].bt + t->box[b].pt, ev = top + t->box[b].pb + t->box[b].bb;
            /* at whole pixels, as Chromium paints a box's decorations (its rectangle pixel-snapped) */
            double x0 = floor(t->frag[fr].x - off + 0.5), x1 = floor(t->frag[fr].x - off + total + 0.5);
            double y0 = floor(t->frag[fr].y + 0.5), y1 = floor(t->frag[fr].y + t->frag[fr].h + 0.5);
            t->box[b].x = x0 + left;
            t->box[b].y = y0 + top;
            t->box[b].w = x1 - x0 > ew ? x1 - x0 - ew : 0;
            t->box[b].h = y1 - y0 > ev ? y1 - y0 - ev : 0;
            off += t->frag[fr].w;
        }
    }
}

/* each clip box (lay_clip_box) placed: its border box its element's first fragment's, as Chromium takes an inline
   element's reference box for its clip-path (measured: inset(0 0 50% 0) on a span over two lines keeps the top half of
   its first line, and nothing of its second); its end edge none when the element goes on past that fragment, sliced */
static void lay_inline_clips(layout_tree *t) {
    vector<uint8_t> done;
    for (uint32_t fr = 1; fr < t->frag_len; fr++) {
        uint32_t cb = t->frag[fr].clip;
        if (!t->frag[fr].live || !cb || t->frag[fr].kind != LAYOUT_FRAG_KIND_INLINE || t->box[cb].node.index != t->frag[fr].node.index) continue;
        if (len(done) <= cb) resize(done, (size_t)cb + 1, 0);
        if (at(done, cb)) {
            if (at(done, cb) == 1 && !t->box[cb].clone) {
                /* a second fragment: the first's end edge none */
                t->box[cb].w += t->box[cb].pr + t->box[cb].br;
                t->box[cb].pr = 0;
                t->box[cb].br = 0;
            }
            done[cb] = 2;
            continue;
        }
        done[cb] = 1;
        double ew = t->box[cb].bl + t->box[cb].pl + t->box[cb].pr + t->box[cb].br, ev = t->box[cb].bt + t->box[cb].pt + t->box[cb].pb + t->box[cb].bb;
        t->box[cb].x = t->frag[fr].x + t->box[cb].bl + t->box[cb].pl;
        t->box[cb].y = t->frag[fr].y + t->box[cb].bt + t->box[cb].pt;
        t->box[cb].w = t->frag[fr].w > ew ? t->frag[fr].w - ew : 0;
        t->box[cb].h = t->frag[fr].h > ev ? t->frag[fr].h - ev : 0;
    }
}

/* what a box's style says of it besides its geometry: how it is positioned (a relative
   one's offset), its stacking and its clipping */
static void lay_box_style(LCTX, uint32_t b, uint32_t el, double cw, double ch) {
    uint32_t si = lay_si(sx, gx, el);
    int pos = at(*xc, si).pos;
    t->box[b].position = pos == POS_RELATIVE ? LAYOUT_POSITION_RELATIVE : pos == POS_ABSOLUTE ? LAYOUT_POSITION_ABSOLUTE :
                         pos == POS_FIXED ? LAYOUT_POSITION_FIXED : pos == POS_STICKY ? LAYOUT_POSITION_STICKY : LAYOUT_POSITION_STATIC;
    t->box[b].z_auto = pos == POS_STATIC || at(*xc, si).zauto;
    t->box[b].z = t->box[b].z_auto ? 0 : at(*xc, si).z;
    t->box[b].clip = (uint8_t)lay_clip_of(LARGS, el);
    t->box[b].hidden = at(*xc, si).hidden != 0;
    if (at(*xc, si).paints) lay_box_paint(LARGS, b, si);
    if (pos == POS_RELATIVE) {
        double dx, dy;
        lay_ext e = at(*xc, si);
        lay_rel_offset(st, &e, cw, ch, &dx, &dy);
        t->box[b].dx = dx;
        t->box[b].dy = dy;
    }
}

/* the initial containing block's size (the viewport's), which an orthogonal flow's available inline size falls back to;
   and the element lay_block lays out in its own frame though its parent's is another (lay_wm_root's) */
static double lay_icb_w = 800, lay_icb_h = 600;
static uint32_t lay_wm_own;
static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty);
/* a vector (x, y) of frame f made physical, and a physical one made frame f's */
static void lay_vec_out(int f, double x, double y, double *px, double *py) {
    if (f == 1) { *px = -y; *py = x; } else if (f == 2) { *px = y; *py = x; } else if (f == 3) { *px = y; *py = -x; } else { *px = x; *py = y; }
}
static void lay_vec_in(int f, double px, double py, double *x, double *y) {
    if (f == 1) { *x = py; *y = -px; } else if (f == 2) { *x = py; *y = px; } else if (f == 3) { *x = -py; *y = px; } else { *x = px; *y = py; }
}
/* box b's own edges, laid out in frame cf, made frame pf's (through the physical sides); its size turned when the two
   are orthogonal, and its relative offset */
static void lay_turn_sides(layout_tree *t, uint32_t b, int cf, int pf) {
    double m[4] = { t->box[b].mt, t->box[b].mr, t->box[b].mb, t->box[b].ml }, bw[4] = { t->box[b].bt, t->box[b].br, t->box[b].bb, t->box[b].bl };
    double pd[4] = { t->box[b].pt, t->box[b].pr, t->box[b].pb, t->box[b].pl };
    uint32_t bc[4] = { t->box[b].bct, t->box[b].bcr, t->box[b].bcb, t->box[b].bcl };
    uint8_t bs[4] = { t->box[b].bst, t->box[b].bsr, t->box[b].bsb, t->box[b].bsl };
    double m2[4], b2[4], p2[4];
    uint32_t c2[4];
    uint8_t s2[4];
    for (int k = 0; k < 4; k++) {
        int q = lay_pside(cf, lay_tside(pf, k));
        m2[k] = m[q]; b2[k] = bw[q]; p2[k] = pd[q]; c2[k] = bc[q]; s2[k] = bs[q];
    }
    t->box[b].mt = m2[0]; t->box[b].mr = m2[1]; t->box[b].mb = m2[2]; t->box[b].ml = m2[3];
    t->box[b].bt = b2[0]; t->box[b].br = b2[1]; t->box[b].bb = b2[2]; t->box[b].bl = b2[3];
    t->box[b].pt = p2[0]; t->box[b].pr = p2[1]; t->box[b].pb = p2[2]; t->box[b].pl = p2[3];
    t->box[b].bct = c2[0]; t->box[b].bcr = c2[1]; t->box[b].bcb = c2[2]; t->box[b].bcl = c2[3];
    t->box[b].bst = s2[0]; t->box[b].bsr = s2[1]; t->box[b].bsb = s2[2]; t->box[b].bsl = s2[3];
}
static void lay_turn_edges(layout_tree *t, uint32_t b, int cf, int pf) {
    lay_turn_sides(t, b, cf, pf);
    if ((cf == 0) != (pf == 0)) { double w = t->box[b].w; t->box[b].w = t->box[b].h; t->box[b].h = w; }
    double px, py, x, y;
    lay_vec_out(cf, t->box[b].dx, t->box[b].dy, &px, &py);
    lay_vec_in(pf, px, py, &x, &y);
    t->box[b].dx = x;
    t->box[b].dy = y;
}
/* a style's edges and sizes, of frame cf, made frame pf's */
static void lay_style_turned(lay_style *s, int cf, int pf) {
    lay_len m2[4], p2[4];
    double b2[4];
    for (int k = 0; k < 4; k++) {
        int q = lay_pside(cf, lay_tside(pf, k));
        m2[k] = s->m[q];
        p2[k] = s->p[q];
        b2[k] = s->b[q];
    }
    for (int k = 0; k < 4; k++) { s->m[k] = m2[k]; s->p[k] = p2[k]; s->b[k] = b2[k]; }
    if ((cf == 0) != (pf == 0)) { lay_len w = s->width; s->width = s->height; s->height = w; }
}
/* a block-level element whose writing mode's frame is not its parent's (CSS Writing Modes 4, 3.1: it makes an
   independent formatting context): laid out in its own frame, then set in its parent's, its edges turned. An
   orthogonal one's available inline size is its parent's definite block size, else the initial containing block's
   along its inline axis (7.3, Chromium: the viewport's), which a percentage of its inline size is of too; its auto
   inline size fits its content in that (fit-content), and its block size's percentages are of its parent's inline
   size. In its parent's flow its auto margins of its parent's inline axis share what is left, as a block's do */
static uint32_t lay_wm_root(LCTX, uint32_t parent, uint32_t el, double cw, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty, int pf, int cf) {
    int orth = (pf == 0) != (cf == 0);
    lay_bk k2 = *k;
    k2.ox = 0;
    k2.oy = 0;
    k2.qs = 0;
    double ai = cw;
    if (orth) {
        ai = k->ch >= 0 ? k->ch : cf ? lay_icb_h : lay_icb_w;
        k2.ch = cw;
        k2.fw = -1;
        k2.shrink = 1;
    }
    lay_mc cmt, cmb;
    int cempty;
    lay_wm_own = el;
    uint32_t b = lay_block(LARGS, parent, el, ai, 0, 0, 0, 0, &k2, &cmt, &cmb, &cempty);
    if (orth && k->fw >= 0) {
        /* a border-box width it is given along its parent's inline axis (a stretched grid item's, a flex item's): its
           block size */
        double bh = k->fw - t->box[b].bt - t->box[b].pt - t->box[b].pb - t->box[b].bb;
        t->box[b].h = bh > 0 ? bh : 0;
    }
    lay_turn_edges(t, b, cf, pf);
    if (orth && !k->shrink && !k->item && !LAY_X(el).flt && k->fw < 0) {
        /* auto margins of the parent's inline axis (its frame's left and right) */
        lay_style s;
        lay_style_of(LARGS, el, &s);
        int la = s.m[lay_pside(cf, lay_tside(pf, 3))].unit == LAY_AUTO, ra = s.m[lay_pside(cf, lay_tside(pf, 1))].unit == LAY_AUTO;
        double bw = t->box[b].bl + t->box[b].pl + t->box[b].w + t->box[b].pr + t->box[b].br;
        double rest = cw - bw - (la ? 0 : t->box[b].ml) - (ra ? 0 : t->box[b].mr);
        if (la && ra) { t->box[b].ml = rest > 0 ? rest / 2 : 0; t->box[b].mr = rest > 0 ? rest / 2 : rest; }
        else if (la) t->box[b].ml = rest;
        else if (ra) t->box[b].mr = rest;
    }
    if (orth) {
        lay_style s;
        lay_style_of(LARGS, el, &s);
        if (s.m[lay_pside(cf, lay_tside(pf, 0))].unit == LAY_AUTO) t->box[b].mt = 0;
        if (s.m[lay_pside(cf, lay_tside(pf, 2))].unit == LAY_AUTO) t->box[b].mb = 0;
    }
    *mt = lay_mc_of(t->box[b].mt);
    *mb = lay_mc_of(t->box[b].mb);
    *empty = 0;
    return b;
}

/* a box laid out to be measured, freed: its lines and fragments, the boxes its fragments drew from, its children */
static void lay_free_subtree(layout_tree *t, uint32_t b, int depth) {
    if (!b || b >= t->box_len || !t->box[b].live || depth > 200) return;
    for (uint32_t c = t->box[b].first_child; c;) {
        uint32_t nx = t->box[c].next;
        lay_free_subtree(t, c, depth + 1);
        c = nx;
    }
    for (uint32_t ln = t->box[b].first_line; ln;) {
        uint32_t nl = t->line[ln].next;
        for (uint32_t fr = t->line[ln].first_frag; fr;) {
            uint32_t nf = t->frag[fr].next;
            uint32_t pc = t->frag[fr].piece, cb = t->frag[fr].clip;
            if (pc && pc < t->box_len && t->box[pc].live) box_free(t, box_handle(t, pc));
            if (cb && cb < t->box_len && t->box[cb].live && !t->box[cb].parent) box_free(t, box_handle(t, cb));
            frag_free(t, frag_handle(t, fr));
            fr = nf;
        }
        line_free(t, line_handle(t, ln));
        ln = nl;
    }
    if (t->box[b].legend && t->box[b].legend < t->box_len && t->box[b].legend != b) lay_free_subtree(t, t->box[b].legend, depth + 1);
    if (b < t->box_len && t->box[b].live) box_free(t, box_handle(t, b));
}
/* the extent across its parent's inline axis of a box orthogonal to it, its margin box's: the box laid out in its own
   frame as a writing-mode root (its available inline size the viewport's), measured, and freed -- the positioned boxes
   and inline-blocks its layout met forgotten */
static double lay_wm_extent(LCTX, uint32_t el, double cw, int pf, int cf) {
    size_t nab = len(*ab), nwb = len(lay_wm_boxes);
    vector<uint32_t> keep;
    for (size_t q = 0; q < nwb; q++) push_back(keep, at(lay_wm_boxes, q));
    lay_bk k;
    k.ox = 0;
    k.oy = 0;
    k.ch = -1;
    k.fw = -1;
    k.shrink = 1;
    k.item = 0;
    k.qs = 0;
    lay_mc mt, mb;
    int empty;
    vector<lay_flt> mfl;
    uint32_t b = lay_wm_root(LARGS_FL(&mfl), 0, el, cw, &k, &mt, &mb, &empty, pf, cf);
    double w = t->box[b].ml + t->box[b].bl + t->box[b].pl + t->box[b].w + t->box[b].pr + t->box[b].br + t->box[b].mr;
    lay_free_subtree(t, b, 0);
    resize(ab, nab);
    clear(lay_wm_boxes);
    for (size_t q = 0; q < nwb; q++) push_back(lay_wm_boxes, at(keep, q));
    return w;
}

static uint32_t lay_block(LCTX, uint32_t parent, uint32_t el, double cw, const vector<uint32_t> *items, uint32_t first, uint32_t n,
                          uint32_t style_el, const lay_bk *k, lay_mc *mt, lay_mc *mb, int *empty) {
    if (el && parent && el != lay_wm_own) {
        int cf = lay_frame(data(*sc)[lay_si(sx, gx, el)].wm), pf = lay_frame(t->box[parent].wm);
        if (cf != pf) return lay_wm_root(LARGS, parent, el, cw, k, mt, mb, empty, pf, cf);
    }
    /* a writing-mode root lay_wm_root lays out in its own frame: a formatting context of its own */
    int wmroot = el && el == lay_wm_own;
    if (wmroot) lay_wm_own = 0;
    if (!el) {
        /* an anonymous block: the inline run, as lines; no margins of its own */
        uint32_t b = lay_new_box(LARGS, parent, 0, LAYOUT_BOX_KIND_ANONYMOUS);
        t->box[b].w = k->fw >= 0 ? k->fw : cw;
        t->box[b].how = k->fw >= 0 ? LAYOUT_WIDTH_HOW_FLEX : LAYOUT_WIDTH_HOW_FILL;
        t->box[b].avail = cw;
        double keep = lay_line_pch;
        lay_line_pch = k->ch;
        t->box[b].h = lay_lines(LARGS, b, style_el, items, first, n, t->box[b].w, k->ox, k->oy);
        lay_line_pch = keep;
        *mt = lay_mc_of(0);
        *mb = lay_mc_of(0);
        *empty = !t->box[b].first_line;
        return b;
    }
    lay_style s;
    lay_style_of(LARGS, el, &s);
    uint32_t si = lay_si(sx, gx, el);
    int lx = at(*xc, si).lx;
    int replaced = lay_replaced(d, el);
    layout_box_kind kind = replaced ? LAYOUT_BOX_KIND_REPLACED : s.display == D_LIST_ITEM ? LAYOUT_BOX_KIND_LIST_ITEM :
                           s.display == D_TABLE ? LAYOUT_BOX_KIND_TABLE : lx == LX_FLEX ? LAYOUT_BOX_KIND_FLEX :
                           lx == LX_GRID ? LAYOUT_BOX_KIND_GRID : LAYOUT_BOX_KIND_BLOCK;
    uint32_t b = lay_new_box(LARGS, parent, el, kind);
    lay_edges(t, st, b, &s, cw);
    lay_box_style(LARGS, b, el, cw, k->ch);
    if (s.m[0].unit == LAY_AUTO) t->box[b].mt = 0;
    if (s.m[2].unit == LAY_AUTO) t->box[b].mb = 0;
    if (s.display == D_TABLE) {
        lay_table(LARGS, b, el, &s, cw);
        if (k->shrink || k->item || at(*xc, si).flt) {
            /* a float's, a positioned box's, an item's: its margins as they are, auto ones 0 (not the containing block's
               rest, a block-level table's) */
            t->box[b].ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw);
            t->box[b].mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw);
        }
        *mt = lay_mc_of(t->box[b].mt);
        *mb = lay_mc_of(t->box[b].mb);
        *empty = 0;
        return b;
    }
    double edges_h = t->box[b].bl + t->box[b].br + t->box[b].pl + t->box[b].pr;
    if (replaced) {
        /* a replaced element displayed as a block (CSS 2.1, 10.3.4 and 10.6.2): its width and height an inline
           one's, its margins a block's (auto ones centring it); its width given (a flex or grid item's) or held
           by min-width and max-width, its height kept in proportion when it is auto */
        double w = 0, h = 0;
        /* a percentage height of its containing block's definite height (in quirks mode taken through blocks of an auto
           height) */
        lay_len hr = at(*xc, si).hraw;
        if (s.height.unit == LAY_AUTO && hr.unit != LAY_AUTO && !lay_content_sized(hr) && k->ch >= 0) {
            double ve = t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb;
            s.height.unit = LAY_PX;
            s.height.v = lay_res(st, hr, k->ch) - (s.border_box ? ve : 0);
            if (s.height.v < 0) s.height.v = 0;
        }
        lay_atomic_size(LARGS, el, &s, cw, &w, &h);
        double w0 = w;
        int auto_w = s.width.unit == LAY_AUTO;
        /* a broken image's alt text as a block is as wide as a block (Chromium), its height its line's */
        vector<char> altv;
        int alt_block = auto_w && lay_img_failed(LARGS, el) && lay_alt_text(d, el, &altv) && len(altv);
        if (k->fw >= 0) {
            t->box[b].w = k->fw - edges_h > 0 ? k->fw - edges_h : 0;
            t->box[b].how = LAYOUT_WIDTH_HOW_FLEX;
            t->box[b].ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw);
            t->box[b].mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw);
        } else {
            /* its width the content width found, as a declared one, so a block's margins follow */
            if (!alt_block) {
                s.width.unit = LAY_PX;
                s.width.v = (w > 0 ? w : 0) + (s.border_box ? edges_h : 0);
            }
            lay_len minw = lay_minw_px(LARGS, el, &s, cw, at(*xc, si).minw);
            lay_block_width(t, st, b, &s, cw, &minw);
            if (auto_w && t->box[b].how == LAYOUT_WIDTH_HOW_DECLARED) t->box[b].how = LAYOUT_WIDTH_HOW_INTRINSIC;
        }
        /* a float's, a positioned box's, an item's margins as they are, auto ones 0 */
        if (k->shrink || k->item || at(*xc, si).flt) {
            t->box[b].ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw);
            t->box[b].mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw);
        }
        /* an image's height in proportion to the width it was held to (min-width, max-width, a given width) */
        if ((lay_is_named(d, el, "img") || lay_is_svg(d, el)) && !alt_block && s.height.unit == LAY_AUTO && w0 > 0 && h > 0 && t->box[b].w != w0)
            h = h * t->box[b].w / w0;
        t->box[b].h = h > 0 ? h : 0;
        t->box[b].avail = cw;
        lay_placeholder(LARGS, b, el, &s);
        *mt = lay_mc_of(t->box[b].mt);
        *mb = lay_mc_of(t->box[b].mb);
        *empty = 0;
        return b;
    }
    if (k->fw >= 0) {
        /* a width it is given (a flex item's, a grid item's, a positioned box's): its border box's */
        t->box[b].w = k->fw - edges_h > 0 ? k->fw - edges_h : 0;
        t->box[b].how = LAYOUT_WIDTH_HOW_FLEX;
    } else {
        if (k->shrink && s.width.unit == LAY_AUTO) s.width.unit = LAY_FITC;
        int content_sized = lay_content_sized(s.width);
        if (content_sized) {
            double mn, mx;
            lay_intrinsic(LARGS, el, cw, &mn, &mx);
            double edges = lay_edges_px(st, &s, cw), box = s.b[1] + s.b[3] + lay_res(st, s.p[1], cw) + lay_res(st, s.p[3], cw);
            double avail = cw - edges, v = avail > mn - edges ? avail : mn - edges;
            double w = s.width.unit == LAY_MINC ? mn - edges : s.width.unit == LAY_MAXC ? mx - edges : v < mx - edges ? v : mx - edges;
            s.width.unit = LAY_PX;
            s.width.v = (w < 0 ? 0 : w) + (s.border_box ? box : 0);
        }
        lay_len minw = lay_minw_px(LARGS, el, &s, cw, at(*xc, si).minw);
        lay_block_width(t, st, b, &s, cw, &minw);
        if (content_sized && t->box[b].how == LAYOUT_WIDTH_HOW_DECLARED) t->box[b].how = LAYOUT_WIDTH_HOW_SHRINK_TO_FIT;
    }
    if (k->fw >= 0 || k->shrink || k->item || at(*xc, si).flt) {
        /* a float's, a positioned box's, an item's: its margins as they are, auto ones 0 */
        t->box[b].ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cw);
        t->box[b].mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cw);
    }
    int root = !(el & LAY_GEN) && d->node[el].parent == 1;
    int pos = at(*xc, si).pos;
    int bfc = root || s.bfc || s.display == D_CELL || s.display == D_CAPTION || lay_clip_of(LARGS, el) == 1 || lx != LX_FLOW ||
              at(*xc, si).flt || pos == POS_ABSOLUTE || pos == POS_FIXED || k->item || wmroot || (parent && s.wm != t->box[parent].wm);
    /* its definite height, if it has one: declared in px, or a percentage of a definite
       containing block's (the root's: the viewport's) */
    double vedges = t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb;
    double defh = s.height.unit == LAY_PX ? s.height.v : -1;
    lay_len hraw = at(*xc, si).hraw;
    if (defh < 0 && hraw.unit != LAY_AUTO && !lay_content_sized(hraw) && k->ch >= 0) {
        defh = lay_res(st, hraw, k->ch) - (s.border_box ? vedges : 0);
        if (defh < 0) defh = 0;
    }
    /* a size container's auto height is its content's as if it had none (size containment), min- and max-height
       holding it */
    int cqk = s.display == D_CELL || s.display == D_CAPTION ? 0 : at(*xc, si).cq;
    if (defh < 0 && cqk == 2) defh = 0;
    double minh = 0, maxh = -1;
    lay_len mnl = at(*xc, si).minh, mxl = at(*xc, si).maxh;
    if (mnl.unit != LAY_AUTO && !lay_content_sized(mnl) && (mnl.unit == LAY_PX || k->ch >= 0)) minh = lay_res(st, mnl, k->ch) - (s.border_box ? vedges : 0);
    if (mxl.unit != LAY_AUTO && !lay_content_sized(mxl) && (mxl.unit == LAY_PX || k->ch >= 0)) maxh = lay_res(st, mxl, k->ch) - (s.border_box ? vedges : 0);
    /* the table cell height box sizing quirk (quirks mode; the Quirks Mode Standard 3.13): a table
       cell's height, min-height and max-height are its border box's */
    if (lay_quirks && s.display == D_CELL && !s.border_box) {
        if (defh >= 0) defh = defh > vedges ? defh - vedges : 0;
        if (minh > 0) minh = minh > vedges ? minh - vedges : 0;
        if (maxh >= 0) maxh = maxh > vedges ? maxh - vedges : 0;
    }
    if (defh >= 0 && maxh >= 0 && defh > maxh) defh = maxh;
    if (defh >= 0 && defh < minh) defh = minh;
    /* quirks mode: the root html element and the document's body element, of an auto height, fill
       what they are given (3.6, 3.7: the viewport's height, the root's content box's), their margins
       taken off -- the height their children's percentages take, and the least their own is; and
       (3.5, the percentage height calculation quirk) another block container of an auto height, not
       positioned absolutely and no table cell, has its children's percentages taken of what its own
       are. pch: the children's percentages' basis */
    double fill = -1, pch = defh;
    if (lay_quirks && defh < 0 && k->ch >= 0) {
        if (s.height.unit == LAY_AUTO && lay_fills(LARGS, el, root)) {
            fill = k->ch - t->box[b].mt - t->box[b].mb - vedges;
            if (fill < 0) fill = 0;
            pch = fill;
            if (maxh >= 0 && pch > maxh) pch = maxh;
            if (pch < minh) pch = minh;
        } else if (lx == LX_FLOW && pos != POS_ABSOLUTE && pos != POS_FIXED && s.display != D_CELL) pch = k->ch;
    }
    /* a query container sized: its subtree's styles made again for its content box (src/css/css.h), before what it
       holds is laid out */
    if (cqk && !(el & LAY_GEN)) lay_cq_restyle(LARGS, el, t->box[b].w, defh);
    lay_mc et, eb;
    /* a fieldset's rendered legend (HTML 15.3.13): shrunk to fit, its auto margins aligning it, set on the top border
       -- its margin box from the border box's top, or centred on a border taller than it --; the content begins
       below the taller of the two */
    uint32_t lg = lay_rendered_legend(LARGS, el), lgb = 0;
    double lg_y = 0;
    if (lg) {
        lay_bk lk;
        lk.ox = 0;
        lk.oy = 0;
        lk.ch = -1;
        lk.fw = -1;
        lk.shrink = 1;
        lk.item = 0;
        lk.qs = 0;
        lay_mc lm1, lm2;
        int lempty;
        lgb = lay_block(LARGS, b, lg, t->box[b].w, 0, 0, 0, 0, &lk, &lm1, &lm2, &lempty);
        lay_style ls;
        lay_style_of(LARGS, lg, &ls);
        double lbw = t->box[lgb].w + t->box[lgb].pl + t->box[lgb].pr + t->box[lgb].bl + t->box[lgb].br;
        double rest = t->box[b].w - lbw - t->box[lgb].ml - t->box[lgb].mr;
        int mla = ls.m[3].unit == LAY_AUTO, mra = ls.m[1].unit == LAY_AUTO;
        if (s.rtl) {
            /* right to left: from the inline start, the right, as Chromium places it (a legend wider than the content
               box overflowing the left) */
            double from = t->box[lgb].mr + (rest > 0 && mra ? (mla ? rest / 2 : rest) : 0);
            t->box[lgb].mr = from;
            t->box[lgb].ml = t->box[b].w - lbw - from;
        } else if (rest > 0 && mla && mra) { t->box[lgb].ml = rest / 2; t->box[lgb].mr = rest / 2; }
        else if (rest > 0 && mla) t->box[lgb].ml = rest;
        double lmh = t->box[lgb].mt + lay_border_height(t, lgb) + t->box[lgb].mb, bt0 = t->box[b].bt;
        double extra = lmh > bt0 ? lmh - bt0 : 0;
        lg_y = (lmh > bt0 ? 0 : (bt0 - lmh) / 2) + t->box[lgb].mt;   /* its border box's top, from the fieldset's */
        t->box[b].pt += extra;
        t->box[b].legend = lgb;
    }
    double cox = k->ox + t->box[b].ml + t->box[b].bl + t->box[b].pl, coy = k->oy + t->box[b].bt + t->box[b].pt;
    /* a flex or grid container that fills takes the fill for its definite height */
    if (lx == LX_FLEX) { lay_flex(LARGS, b, el, fill >= 0 ? pch : defh); et = lay_mc_of(0); eb = lay_mc_of(0); *empty = 0; }
    else if (lx == LX_GRID) { lay_grid(LARGS, b, el, fill >= 0 ? pch : defh); et = lay_mc_of(0); eb = lay_mc_of(0); *empty = 0; }
    else lay_flow_children(LARGS, b, el, bfc, defh < 0, cox, coy, pch, k->qs, &et, &eb, empty);
    /* a list item's marker that no line of its content holds: a line of its own at the content box's top, the list item
       at least as tall (Chromium's marker without line boxes) */
    if (s.display == D_LIST_ITEM && s.list && lx == LX_FLOW && !(el & LAY_GEN)) {
        uint32_t owner = 0, mln = 0;
        double my = 0;
        int mclip = 0;
        if (!lay_marker_spot(t, b, &owner, &mln, &my, &mclip, 0)) {
            uint32_t mb = lay_marker_box(LARGS, b, el, t->box[b].w, 1);
            lay_place(t, mb, 0, 0);
            if (t->box[mb].h > t->box[b].h) t->box[b].h = t->box[mb].h;
            *empty = 0;
        }
    }
    if (defh >= 0) {
        t->box[b].h = defh;
        eb = lay_mc_of(0);
        *empty = 0;
    } else {
        if (maxh >= 0 && t->box[b].h > maxh) t->box[b].h = maxh;
        /* a box held taller by min-height keeps its last child's bottom margin, but for one that fills (the quirky
           body's, which Chromium lets through: measured) */
        if (t->box[b].h < minh) { t->box[b].h = minh; if (fill < 0) eb = lay_mc_of(0); *empty = 0; }
    }
    if (t->box[b].bt + t->box[b].bb + t->box[b].pt + t->box[b].pb > 0) *empty = 0;
    if (at(*xc, si).clamp > 0 && defh < 0) lay_clamp(LARGS, b, el, &s);
    /* the legend in its place: from the fieldset's content box, above it */
    if (lgb) lay_place(t, lgb, t->box[lgb].ml, lg_y - t->box[b].bt - t->box[b].pt);
    /* its own margins, quirky ones (quirks mode) left out at a quirky container's start -- an empty
       box's bottom one too, quirky when its top one is (Blink) */
    int qt = lay_quirky_margin(LARGS, el, 0), qb = lay_quirky_margin(LARGS, el, 2) || (*empty && qt);
    lay_mc own_t = k->qs && qt ? lay_mc_of(0) : lay_mc_q(t->box[b].mt, qt);
    lay_mc own_b = k->qs && *empty && qb ? lay_mc_of(0) : lay_mc_q(t->box[b].mb, qb);
    if (fill >= 0) {
        /* a fill's border box: what it is given less its margins as they collapsed -- its top's with its
           first child's through it, its bottom's with its last child's (measured) */
        double fh = k->ch - lay_mc_v(lay_mc_add(own_t, et)) - lay_mc_v(lay_mc_add(own_b, eb)) - vedges;
        if (maxh >= 0 && fh > maxh) fh = maxh;
        if (fh > t->box[b].h) { t->box[b].h = fh; *empty = 0; }
    }
    *mt = lay_mc_add(own_t, et);
    *mb = lay_mc_add(own_b, eb);
    if (s.display == D_LIST_ITEM) lay_marker(LARGS, b, el, &s);
    return b;
}
/* ---- generated content: ::before's and ::after's text (CSS 2.1, 12) ---- */

/* a counter in scope as the walk has it: its name (hashed) and its value */
typedef struct lay_ctr { uint64_t name; long v; } lay_ctr;

static uint64_t lay_hash(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    return h;
}
static int lay_is_word(const char *s, size_t n, const char *w) { return n == strlen(w) && memcmp(s, w, n) == 0; }
/* s[0, n) is a call of the function fn: its arguments' text from *a, *an bytes */
static int lay_call(const char *s, size_t n, const char *fn, size_t *a, size_t *an) {
    size_t k = strlen(fn);
    if (n < k + 2 || memcmp(s, fn, k) != 0 || s[k] != '(' || s[n - 1] != ')') return 0;
    *a = k + 1;
    *an = n - k - 2;
    return 1;
}
/* the next comma-separated argument of s[*i, n), its white space trimmed */
static int lay_arg(const char *s, size_t n, size_t *i, size_t *a, size_t *an) {
    size_t p = *i;
    while (p < n && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n')) p++;
    if (p >= n) return 0;
    size_t b = p;
    char q = 0;
    while (p < n && (q || s[p] != ',')) {
        if (q && s[p] == '\\') p++;
        else if (q && s[p] == q) q = 0;
        else if (!q && (s[p] == '"' || s[p] == '\'')) q = s[p];
        p++;
    }
    size_t e = p < n ? p : n;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n')) e--;
    *a = b;
    *an = e - b;
    *i = p < n ? p + 1 : n;
    return 1;
}
/* a CSS string's text (s[0, n), its quotes included), its escapes made characters, into out */
static void lay_unquote(const char *s, size_t n, vector<char> *out) {
    if (n < 2) return;
    for (size_t i = 1; i + 1 < n; i++) {
        if (s[i] != '\\') { push_back(out, s[i]); continue; }
        i++;
        if (i + 1 >= n) break;
        if (s[i] == '\n') continue;
        uint32_t cp = 0;
        size_t k = 0;
        while (k < 6 && i + 1 < n) {
            char c = s[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) break;
            cp = cp * 16 + (uint32_t)v;
            i++;
            k++;
        }
        if (k == 0) { push_back(out, s[i]); continue; }
        if (i + 1 < n && s[i] == ' ') i++;
        i--;
        if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
        if (cp < 0x80) push_back(out, (char)cp);
        else if (cp < 0x800) { push_back(out, (char)(0xC0 | cp >> 6)); push_back(out, (char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            push_back(out, (char)(0xE0 | cp >> 12));
            push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (cp & 0x3F)));
        } else {
            push_back(out, (char)(0xF0 | cp >> 18));
            push_back(out, (char)(0x80 | (cp >> 12 & 0x3F)));
            push_back(out, (char)(0x80 | (cp >> 6 & 0x3F)));
            push_back(out, (char)(0x80 | (cp & 0x3F)));
        }
    }
}
/* v as the counter style named s[0, n) writes it (the page's @counter-style rules and the predefined styles: src/css;
   decimal for a name none defines, nothing for none), into out */
static void lay_counter_style(const css_style *st, long v, const char *s, size_t n, vector<char> *out) {
    css_counter_text(st, s, n, v, 0, out);
}
/* the quote of the quotes property's value qc at depth (an open one, or a close one) into
   out: auto's are English's, none's none */
static void lay_quote(const css_style *st, uint32_t qc, int depth, int close, vector<char> *out) {
    if (st->cval[qc].unit != CSS_UNIT_TEXT) {
        if (names_is(st, st->cval[qc].word, "none", 4)) return;
        if (depth == 0 && close) append_range(out, "\xe2\x80\x9d", 3);
        else if (depth == 0) append_range(out, "\xe2\x80\x9c", 3);
        else if (close) append_range(out, "\xe2\x80\x99", 3);
        else append_range(out, "\xe2\x80\x98", 3);
        return;
    }
    vector<char> src;
    amb_span sp = st->cval[qc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n, k = 0, pa = 0, pn = 0, total = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) total++;
    size_t pairs = total / 2;
    if (pairs == 0) return;
    size_t want = 2 * ((size_t)depth < pairs ? (size_t)depth : pairs - 1) + (close ? 1 : 0);
    i = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        if (k == want) { pa = a; pn = n; }
        k++;
    }
    if (pn) lay_unquote(data(src) + pa, pn, out);
}

/* the element's counter-reset (its values from c0) names the counter name */
static int lay_resets(const css_style *st, uint32_t c0, uint64_t name) {
    uint32_t c = c0 + CSS_PROP_COUNTER_RESET;
    if (st->cval[c].unit != CSS_UNIT_TEXT) return 0;
    vector<char> src;
    amb_span sp = st->cval[c].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n;
    int k = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) if (k++ % 2 == 0 && lay_hash(data(src) + a, n) == name) return 1;
    return 0;
}

/* an element's counter-reset and counter-increment (its values from c0): a reset makes a
   counter of this level (from mark in ctr), or sets one an earlier sibling made; an
   increment adds to the innermost counter of its name at floor or after it, or makes one
   (a style container's counters are its subtree's own: CSS Contain 2, 3.3; floor is where
   the innermost container's begin). A query container's own, as Chromium counts them:
   mode 1, as its parent's walk meets it, its increments of the counters it does not reset
   (they count outside it); mode 2, as its own walk begins, its resets and the increments
   of those (inside it). mode 0 every other element's */
static void lay_counters(const css_style *st, uint32_t c0, vector<lay_ctr> *ctr, size_t mark, size_t floor, int mode) {
    for (int pass = 0; pass < 2; pass++) {
        if (mode == 1 && pass == 0) continue;
        uint32_t c = c0 + (pass == 0 ? CSS_PROP_COUNTER_RESET : CSS_PROP_COUNTER_INCREMENT);
        if (st->cval[c].unit != CSS_UNIT_TEXT) continue;
        vector<char> src;
        amb_span sp = st->cval[c].text;
        append_range(src, text_ptr(st, sp), sp.len);
        size_t i = 0, a, n;
        uint64_t name = 0;
        int have = 0;
        while (css_next_part(data(src), len(src), &i, &a, &n)) {
            if (!have) { name = lay_hash(data(src) + a, n); have = 1; continue; }
            have = 0;
            long v = 0;
            int neg = 0;
            for (size_t q = a; q < a + n; q++) {
                char ch = data(src)[q];
                if (ch == '-') neg = 1;
                else if (ch >= '0' && ch <= '9' && v < 100000000) v = v * 10 + (ch - '0');
            }
            if (neg) v = -v;
            if (pass == 1 && mode && (mode == 1) == lay_resets(st, c0, name)) continue;
            size_t q = len(*ctr), lo = pass == 0 ? mark : floor;
            int found = 0;
            while (q > lo && !found) {
                q--;
                if (at(*ctr, q).name == name) {
                    if (pass == 0) (*ctr)[q].v = v;
                    else (*ctr)[q].v += v;
                    found = 1;
                }
            }
            if (!found) {
                lay_ctr nc;
                nc.name = name;
                nc.v = v;
                push_back(ctr, nc);
            }
        }
    }
}

/* the text content's value (cc) gives element el's pseudo-element (its quotes qc), the
   counters ctr in scope, the quotes' depth *depth: strings, attr(), counter(), counters(),
   the quotes; an image's url() gives none */
static void lay_content(const dom_document *d, const css_style *st, uint32_t el, uint32_t cc, uint32_t qc, const vector<lay_ctr> *ctr, int *depth,
                        vector<char> *out) {
    if (st->cval[cc].unit != CSS_UNIT_TEXT) return;
    vector<char> src;
    amb_span sp = st->cval[cc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n, fa, fn;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        const char *p = data(src) + a;
        if (n == 1 && p[0] == '/') break;   /* the alternative text for a reader is not drawn */
        if (p[0] == '"' || p[0] == '\'') { lay_unquote(p, n, out); continue; }
        if (lay_is_word(p, n, "open-quote")) { lay_quote(st, qc, *depth, 0, out); (*depth)++; continue; }
        if (lay_is_word(p, n, "close-quote")) { if (*depth > 0) { (*depth)--; lay_quote(st, qc, *depth, 1, out); } continue; }
        if (lay_is_word(p, n, "no-open-quote")) { (*depth)++; continue; }
        if (lay_is_word(p, n, "no-close-quote")) { if (*depth > 0) (*depth)--; continue; }
        if (lay_call(p, n, "attr", &fa, &fn)) {
            size_t j = 0, xa, xn;
            if (!lay_arg(p + fa, fn, &j, &xa, &xn)) continue;
            uint32_t nm = names_find(d, p + fa + xa, xn);
            if (!nm) continue;
            for (uint32_t r = d->node[el].first_attr; r; r = d->attr[r].next)
                if (d->attr[r].name == nm) { amb_span v = d->attr[r].value; append_range(out, text_ptr(d, v), v.len); }
            continue;
        }
        int one = lay_call(p, n, "counter", &fa, &fn);
        int many = !one && lay_call(p, n, "counters", &fa, &fn);
        if (one || many) {
            size_t j = 0, na, nn, sa = 0, sn = 0, ya = 0, yn = 0;
            if (!lay_arg(p + fa, fn, &j, &na, &nn)) continue;
            if (many && !lay_arg(p + fa, fn, &j, &sa, &sn)) continue;
            int styled = lay_arg(p + fa, fn, &j, &ya, &yn);
            uint64_t name = lay_hash(p + fa + na, nn);
            vector<char> sty;
            if (styled) append_range(sty, p + fa + ya, yn);
            else append_range(sty, "decimal", 7);
            int any = 0;
            for (size_t q = 0; q < len(*ctr); q++) {
                if (at(*ctr, q).name != name) continue;
                if (!many) continue;
                if (any) lay_unquote(p + fa + sa, sn, out);
                lay_counter_style(st, at(*ctr, q).v, data(sty), len(sty), out);
                any = 1;
            }
            if (!many) {
                long v = 0;
                for (size_t q = len(*ctr); q > 0; q--) if (at(*ctr, q - 1).name == name) { v = at(*ctr, q - 1).v; break; }
                lay_counter_style(st, v, data(sty), len(sty), out);
            } else if (!any) lay_counter_style(st, 0, data(sty), len(sty), out);
            continue;
        }
    }
}

/* a pseudo-element's counters and its text, as the walk reaches it */
static void lay_gen_content(layout_tree *t, const dom_document *d, const css_style *st, const vector<lay_ext> *xc, const vector<lay_style> *sc,
                            vector<lay_gen> *gx, uint32_t x, uint32_t pk, vector<lay_ctr> *ctr, size_t mark, size_t floor, int *depth) {
    int64_t g = lay_gen_find(gx, x, pk);
    if (g < 0) return;
    uint32_t si = at(*gx, (size_t)g).style;
    if (at(*sc, si).display == D_NONE) return;
    uint32_t c0 = at(*xc, si).c0;
    lay_counters(st, c0, ctr, mark, floor, 0);
    vector<char> out;
    lay_content(d, st, x, c0 + CSS_PROP_CONTENT, c0 + CSS_PROP_QUOTES, ctr, depth, &out);
    amb_span sp = text_add(t, data(out), len(out));
    (*gx)[(size_t)g].text = sp;
}

/* what a run keeps for its query containers' restyles (lay_cq_restyle), freed at its end: the web faces' indexes
   (lay_style_make's), and each container's counters and quotes' depth as the walk below entered it, which the walk
   of its subtree starts from again (lay_cq_gen): lay_cq_at a node's state's number + 1 */
static vector<int32_t> lay_cq_wf;
static vector<uint32_t> lay_cq_at, lay_cq_off, lay_cq_cnt;
static vector<int32_t> lay_cq_depth;
static vector<lay_ctr> lay_cq_pool;

/* the run's state for its query containers, freed */
static void lay_cq_free(void) {
    clear(lay_cq_wf);
    shrink_to_fit(&lay_cq_wf);
    clear(lay_cq_at);
    shrink_to_fit(&lay_cq_at);
    clear(lay_cq_off);
    shrink_to_fit(&lay_cq_off);
    clear(lay_cq_cnt);
    shrink_to_fit(&lay_cq_cnt);
    clear(lay_cq_depth);
    shrink_to_fit(&lay_cq_depth);
    clear(lay_cq_pool);
    shrink_to_fit(&lay_cq_pool);
    /* and the runs shaped */
    lay_rb_run = 0;
    lay_ar_clear();
    shrink_to_fit(&lay_ar);
    shrink_to_fit(&lay_ar_tx);
    shrink_to_fit(&lay_ar_at);
    shrink_to_fit(&lay_ar_gl);
    shrink_to_fit(&lay_ar_slot);
    /* and the inline-blocks laid out in their own frames */
    clear(lay_wm_boxes);
    shrink_to_fit(&lay_wm_boxes);
}

static void lay_cq_save(uint32_t x, const vector<lay_ctr> *ctr, int depth) {
    if (x >= len(lay_cq_at)) resize(&lay_cq_at, (size_t)x + 1);
    uint32_t k = at(lay_cq_at, x);
    if (!k) {
        push_back(lay_cq_off, 0);
        push_back(lay_cq_cnt, 0);
        push_back(lay_cq_depth, 0);
        k = (uint32_t)len(lay_cq_off);
        lay_cq_at[x] = k;
    }
    lay_cq_off[k - 1] = (uint32_t)len(lay_cq_pool);
    lay_cq_cnt[k - 1] = (uint32_t)len(*ctr);
    lay_cq_depth[k - 1] = depth;
    for (size_t q = 0; q < len(*ctr); q++) push_back(lay_cq_pool, at(*ctr, q));
}

/* the walk that gives each ::before and ::after its text, in tree order, which counters
   and quotes follow: x's ::before, its children (each displayed one's counters, then its
   own walk), its ::after; the counters x's children made go out of scope at its end. A
   query container's counters and quotes are its subtree's (style containment: CSS Contain
   2, 3.3): the counters it and its subtree make are dropped at its end, their increments
   kept from those outside it (floor), and the quotes' depth is as it was before it */
static void lay_gen_walk(layout_tree *t, const dom_document *d, const css_style *st, const vector<uint32_t> *sx, const vector<lay_ext> *xc,
                         const vector<lay_style> *sc, vector<lay_gen> *gx, uint32_t x, vector<lay_ctr> *ctr, int *depth, size_t floor, int level) {
    uint32_t fl = x < len(*sx) ? at(*sx, x) : 0;
    uint32_t xsi = fl & LAY_SMASK;
    int contained = xsi && xsi < len(*xc) && at(*xc, xsi).cq;
    size_t entry = len(*ctr);
    int depth0 = *depth;
    if (contained) {
        lay_cq_save(x, ctr, *depth);
        floor = entry;
        lay_counters(st, at(*xc, xsi).c0, ctr, entry, floor, 2);
    }
    size_t mark = len(*ctr);
    if (fl & LAY_HAS_BEFORE) lay_gen_content(t, d, st, xc, sc, gx, x, CSS_PSEUDO_BEFORE, ctr, mark, floor, depth);
    if (level < 1024)
        for (uint32_t c = dom_flat_first(d, x); c; c = dom_flat_next(d, c)) {
            if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || c >= len(*sx)) continue;
            uint32_t si = at(*sx, c) & LAY_SMASK;
            if (!si || at(*sc, si).display == D_NONE) continue;
            lay_counters(st, at(*xc, si).c0, ctr, mark, floor, at(*xc, si).cq ? 1 : 0);
            lay_gen_walk(t, d, st, sx, xc, sc, gx, c, ctr, depth, floor, level + 1);
        }
    if (fl & LAY_HAS_AFTER) lay_gen_content(t, d, st, xc, sc, gx, x, CSS_PSEUDO_AFTER, ctr, mark, floor, depth);
    resize(ctr, contained ? entry : mark);
    if (contained) *depth = depth0;
}

/* ---- query containers (CSS Conditional 5): the cascade asked again once a container is sized ---- */

/* the style sid (its first value c0) as layout reads it, made for a restyle: its background's image the one the loader
   has (layout_run fetched those @container rules name) */
static uint32_t lay_image_known(const css_style *st, const net_loader *nt, uint32_t c);
static void lay_cq_style(LCTX, uint32_t sid, uint32_t c0) {
    if (sid >= len(*sc)) resize(sc, (size_t)sid + 1);
    if (sid >= len(*xc)) resize(xc, (size_t)sid + 1);
    lay_style ls;
    lay_style_make(st, f, &lay_cq_wf, c0, -1, &ls);
    (*sc)[sid] = ls;
    lay_ext le;
    lay_ext_make(st, c0, -1, &le);
    if (le.bgimg && nt) le.bgres = lay_image_known(st, nt, le.bgimg);
    (*xc)[sid] = le;
}

/* the generated content of the container el's subtree made again, from the state the walk entered it with */
static void lay_cq_gen(LCTX, uint32_t el) {
    if (el >= len(lay_cq_at) || !at(lay_cq_at, el)) return;
    uint32_t k = at(lay_cq_at, el) - 1;
    vector<lay_ctr> ctr;
    for (uint32_t q = 0; q < at(lay_cq_cnt, k); q++) push_back(ctr, at(lay_cq_pool, (size_t)at(lay_cq_off, k) + q));
    int depth = at(lay_cq_depth, k);
    lay_gen_walk(t, d, st, sx, xc, sc, gx, el, &ctr, &depth, 0, 0);
}

/* the query container el, its content box w x h (h < 0: its height its content's): the cascade's styles of its subtree
   made again for that size (css_container_restyle), and the run's tables made to follow -- each changed node's style,
   each pseudo-element's (generated or not), the styles new to the run -- then its generated content again */
static void lay_cq_restyle(LCTX, uint32_t el, double w, double h) {
    vector<css_restyled> ch;
    if (!css_container_restyle(st, d, el, w, h, &ch) || !len(ch)) return;
    for (size_t k = 0; k < len(ch); k++) {
        css_restyled r = at(ch, k);
        if (r.first && (r.style >= len(*xc) || at(*xc, r.style).c0 != r.first)) lay_cq_style(LARGS, r.style, r.first);
        if (r.node >= len(*sx)) continue;
        if (!r.pseudo) { (*sx)[r.node] = (at(*sx, r.node) & ~LAY_SMASK) | (r.style & LAY_SMASK); continue; }
        int64_t g = lay_gen_find(gx, r.node, r.pseudo);
        if (g < 0) continue;
        (*gx)[(size_t)g].style = r.style;
        uint32_t flag = r.pseudo == CSS_PSEUDO_BEFORE ? LAY_HAS_BEFORE : r.pseudo == CSS_PSEUDO_AFTER ? LAY_HAS_AFTER : LAY_HAS_OTHER;
        if (r.first) (*sx)[r.node] |= flag;
        else if (flag != LAY_HAS_OTHER) (*sx)[r.node] &= ~flag;
    }
    lay_cq_gen(LARGS, el);
}

/* ---- flex containers (CSS Flexbox 1, 9) ---- */

/* a flex item: an element (or a pseudo-element), or an anonymous one of a run of text
   (el 0: the run items[first, first + n)); its order and its place; its flex base size,
   hypothetical main size and min and max main sizes, its final main size and its cross
   size (border boxes); its margins along each axis; its box */
typedef struct lay_fi {
    uint32_t el, first, n, idx, box;
    int32_t order;
    double base, hypo, mn, mx, size, cross, grow, shrink, mm0, mm1, cm0, cm1;
    double above;                       /* aligned on a baseline: from its margin box's cross start to it */
    int frozen, auto_m0, auto_m1, auto_c0, auto_c1, align, cross_auto;
} lay_fi;

/* the items of a flex or grid container el, in order (order, then the tree's); its
   positioned children met, their static position its content box's corner (b 0: the items
   alone, for el's intrinsic widths, nothing laid out) */
static void lay_items(LCTX, uint32_t b, uint32_t el, vector<lay_fi> *its, vector<uint32_t> *items) {
    vector<lay_seg> segs;
    lay_collect(LARGS, el, &segs, items);
    for (size_t k = 0; k < len(segs); k++) {
        lay_seg sg = at(segs, k);
        if (sg.block == SEG_ABS) {
            if (!b) continue;
            lay_abs sa;
            sa.el = sg.node;
            sa.box = b;
            sa.x = 0;
            sa.y = 0;
            push_back(ab, sa);
            continue;
        }
        lay_fi it;
        memset(&it, 0, sizeof it);
        it.idx = (uint32_t)k;
        if (sg.block == SEG_RUN) {
            vector<lay_piece> ps;
            lay_run_pieces(LARGS, el, items, sg.first, sg.n, b ? t->box[b].w : 0, &ps);
            if (!lay_run_shows(&ps)) { if (b) lay_run_oof(LARGS, b, &ps, t->box[b].w, 0, 0, 0); continue; }
            it.first = sg.first;
            it.n = sg.n;
            it.shrink = 1;
        } else {
            it.el = sg.node;
            it.order = LAY_X(sg.node).order;
            it.grow = LAY_X(sg.node).grow;
            it.shrink = LAY_X(sg.node).shrink;
        }
        /* in order: order, then the tree's */
        push_back(its, it);
        size_t q = len(*its) - 1;
        while (q > 0 && at(*its, q - 1).order > it.order) { (*its)[q] = at(*its, q - 1); q--; }
        (*its)[q] = it;
    }
}

/* an item's max- and min-content widths (its margin box's): an anonymous one's text's */
static void lay_item_intrinsic(LCTX, uint32_t el, const lay_fi *it, const vector<uint32_t> *items, double cw, double *mn, double *mx) {
    if (it->el) { lay_child_intrinsic(LARGS, el, it->el, cw, mn, mx); return; }
    vector<lay_piece> ps;
    lay_run_pieces(LARGS, el, items, it->first, it->n, cw, &ps);
    lay_run_intrinsic(LARGS, &ps, mn, mx, 0);
}

/* an item laid out as a child of b: fw its border box's width (-1: shrunk to fit in cw),
   ch the container's definite height; its box */
static uint32_t lay_item_box(LCTX, uint32_t b, uint32_t el, const lay_fi *it, const vector<uint32_t> *items, double cw, double fw, double ch) {
    lay_bk bk;
    bk.ox = 0;
    bk.oy = 0;
    bk.ch = ch;
    bk.fw = fw;
    bk.shrink = fw < 0;
    bk.item = 1;
    bk.qs = 0;
    lay_mc m1, m2;
    int empty;
    vector<lay_flt> nf;
    if (it->el) return lay_block(LARGS_FL(&nf), b, it->el, cw, 0, 0, 0, 0, &bk, &m1, &m2, &empty);
    if (fw < 0) {
        double mn, mx;
        lay_item_intrinsic(LARGS, el, it, items, cw, &mn, &mx);
        bk.fw = mx < cw ? mx : cw;
        if (bk.fw < mn) bk.fw = mn;
    }
    return lay_block(LARGS_FL(&nf), b, 0, cw, items, it->first, it->n, el, &bk, &m1, &m2, &empty);
}

static double lay_bw(const layout_tree *t, uint32_t b) { return t->box[b].bl + t->box[b].pl + t->box[b].w + t->box[b].pr + t->box[b].br; }

/* where free space puts the first of n things and the space between them, by a
   justify-content or align-content keyword */
static void lay_distribute(int how, double free, size_t n, double *start, double *between) {
    *start = 0;
    *between = 0;
    if (how == AJ_END) *start = free;
    else if (how == AJ_CENTER) *start = free / 2;
    else if (how == AJ_BETWEEN && n > 1 && free > 0) *between = free / (double)(n - 1);
    else if (how == AJ_AROUND && n > 0 && free > 0) { *between = free / (double)n; *start = *between / 2; }
    else if (how == AJ_EVENLY && n > 0 && free > 0) { *between = free / (double)(n + 1); *start = *between; }
    else if ((how == AJ_BETWEEN || how == AJ_AROUND || how == AJ_EVENLY) && free < 0 && how != AJ_BETWEEN) *start = free / 2;
}

/* a flex container's items laid out (CSS Flexbox 1, 9): their base sizes, lines, the
   flexible lengths resolved, their cross sizes, justify-content and the alignments; b's
   content height when defh (its definite one) is -1 */
static void lay_flex(LCTX, uint32_t b, uint32_t el, double defh) {
    lay_ext e = LAY_X(el);
    double W = t->box[b].w;
    int row = e.frow;
    vector<lay_fi> its;
    vector<uint32_t> items;
    lay_items(LARGS, b, el, &its, &items);
    double main_avail = row ? W : defh;
    double gap_main = lay_res(st, row ? e.cgap : e.rgap, row ? W : (defh >= 0 ? defh : 0));
    double gap_cross = lay_res(st, row ? e.rgap : e.cgap, row ? (defh >= 0 ? defh : 0) : W);
    size_t ni = len(its);
    /* each item's base size, its min and max, its hypothetical main size */
    for (size_t q = 0; q < ni; q++) {
        lay_fi it = at(its, q);
        lay_style is;
        if (it.el) lay_style_of(LARGS, it.el, &is);
        else { memset(&is, 0, sizeof is); is.width.unit = LAY_AUTO; is.height.unit = LAY_AUTO; is.maxw.unit = LAY_AUTO; for (int k = 0; k < 4; k++) { is.m[k].unit = LAY_PX; is.p[k].unit = LAY_PX; } }
        lay_ext ie;
        if (it.el) ie = LAY_X(it.el);
        else lay_ext_make(st, 0, -1, &ie);
        /* margins: main axis start and end, cross start and end */
        int m0 = row ? 3 : 0, m1 = row ? 1 : 2, c0 = row ? 0 : 3, c1 = row ? 2 : 1;
        it.auto_m0 = is.m[m0].unit == LAY_AUTO;
        it.auto_m1 = is.m[m1].unit == LAY_AUTO;
        it.auto_c0 = is.m[c0].unit == LAY_AUTO;
        it.auto_c1 = is.m[c1].unit == LAY_AUTO;
        it.mm0 = it.auto_m0 ? 0 : lay_res(st, is.m[m0], W);
        it.mm1 = it.auto_m1 ? 0 : lay_res(st, is.m[m1], W);
        it.cm0 = it.auto_c0 ? 0 : lay_res(st, is.m[c0], W);
        it.cm1 = it.auto_c1 ? 0 : lay_res(st, is.m[c1], W);
        double eh = is.b[1] + is.b[3] + lay_res(st, is.p[1], W) + lay_res(st, is.p[3], W);
        double ev = is.b[0] + is.b[2] + lay_res(st, is.p[0], W) + lay_res(st, is.p[2], W);
        int al = ie.as == AJ_AUTO || ie.as == AJ_NORMAL ? e.ai : ie.as;
        it.align = al == AJ_NORMAL ? AJ_STRETCH : al;
        it.cross_auto = row ? (is.height.unit == LAY_AUTO && ie.hraw.unit == LAY_AUTO) : is.width.unit == LAY_AUTO;
        double mn, mx;
        double base = -1;
        lay_len bs = ie.basis;
        if (bs.unit != LAY_AUTO && !lay_content_sized(bs) && (bs.unit == LAY_PX || main_avail >= 0))
            base = lay_res(st, bs, main_avail) + (is.border_box ? 0 : (row ? eh : ev));
        else if (bs.unit == LAY_AUTO && row && is.width.unit != LAY_AUTO && !lay_content_sized(is.width))
            base = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
        else if (bs.unit == LAY_AUTO && !row && is.height.unit == LAY_PX)
            base = is.height.v + ev;
        double content_min = 0;
        if (row) {
            lay_item_intrinsic(LARGS, el, &it, &items, W, &mn, &mx);
            if (base < 0) base = mx - it.mm0 - it.mm1;
            content_min = mn - it.mm0 - it.mm1;
            if (it.el && is.width.unit == LAY_PX) {
                /* the content's min-content width, not its declared width's */
                double cmn, cmx;
                lay_intrinsic_of(LARGS, it.el, W, &cmn, &cmx, 0);
                content_min = cmn - it.mm0 - it.mm1;
            }
        } else {
            /* a column's item: laid out now at its cross size, its height its content's */
            double fw = -1;
            if (it.align == AJ_STRETCH && it.cross_auto && !it.auto_c0 && !it.auto_c1) fw = W - it.cm0 - it.cm1;
            else if (!it.cross_auto && it.el) fw = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
            it.box = lay_item_box(LARGS, b, el, &it, &items, W - it.cm0 - it.cm1, fw, defh);
            double content_h = lay_border_height(t, it.box);
            if (base < 0) base = content_h;
            content_min = content_h;
            it.cross = lay_bw(t, it.box);
        }
        /* min and max main sizes; an auto min the content's (overflow visible) */
        lay_len mnl = row ? ie.minw : ie.minh, mxl = row ? is.maxw : ie.maxh;
        double pad = row ? eh : ev;
        if (mnl.unit == LAY_AUTO) it.mn = ie.clip ? 0 : (content_min < base || bs.unit != LAY_AUTO ? content_min : base);
        else if (lay_content_sized(mnl)) it.mn = content_min;      /* min-content (a fieldset's): the content's */
        else it.mn = lay_res(st, mnl, row ? W : (defh >= 0 ? defh : 0)) + (is.border_box ? 0 : pad);
        if (mnl.unit == LAY_AUTO && it.el && row && is.width.unit != LAY_AUTO && !lay_content_sized(is.width)) {
            double spec = lay_res(st, is.width, W) + (is.border_box ? 0 : eh);
            if (spec < it.mn) it.mn = spec;
        }
        it.mx = 1e9;
        if (mxl.unit != LAY_AUTO && !lay_content_sized(mxl) && (mxl.unit == LAY_PX || main_avail >= 0))
            it.mx = lay_res(st, mxl, main_avail) + (is.border_box ? 0 : pad);
        if (it.mn > it.mx) it.mx = it.mn;
        it.base = base;
        it.hypo = base < it.mn ? it.mn : base > it.mx ? it.mx : base;
        its[q] = it;
    }
    /* the lines: one, or as many as the main size holds with wrap */
    vector<uint32_t> lstart;          /* each line's first item, then the end */
    push_back(lstart, 0);
    if (e.fwrap && main_avail >= 0) {
        double used = 0;
        for (size_t q = 0; q < ni; q++) {
            double o = at(its, q).hypo + at(its, q).mm0 + at(its, q).mm1;
            if (q > back(lstart) && used + gap_main + o > main_avail + 0.01) { push_back(lstart, (uint32_t)q); used = o; }
            else used += (q > back(lstart) ? gap_main : 0) + o;
        }
    }
    push_back(lstart, (uint32_t)ni);
    size_t nl = len(lstart) - 1;
    /* each line's flexible lengths (9.7) */
    for (size_t l = 0; l < nl; l++) {
        size_t a = at(lstart, l), z = at(lstart, l + 1);
        double outer = 0;
        for (size_t q = a; q < z; q++) outer += at(its, q).hypo + at(its, q).mm0 + at(its, q).mm1 + (q > a ? gap_main : 0);
        if (main_avail < 0) {
            for (size_t q = a; q < z; q++) its[q].size = at(its, q).hypo;
            continue;
        }
        int grow = outer < main_avail;
        for (size_t q = a; q < z; q++) {
            lay_fi it = at(its, q);
            it.frozen = (grow ? it.grow <= 0 : it.shrink <= 0) || (grow && it.base > it.hypo) || (!grow && it.base < it.hypo);
            it.size = it.hypo;
            its[q] = it;
        }
        double initial_free = 0;
        int first_round = 1;
        for (int round = 0; round < 64; round++) {
            double used = 0, factors = 0;
            int open_items = 0;
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                used += (it.frozen ? it.size : it.base) + it.mm0 + it.mm1 + (q > a ? gap_main : 0);
                if (!it.frozen) { open_items = 1; factors += grow ? it.grow : it.shrink * it.base; }
            }
            if (!open_items) break;
            double free = main_avail - used;
            if (first_round) { initial_free = free; first_round = 0; }
            if (grow) {
                double sum_grow = 0;
                for (size_t q = a; q < z; q++) if (!at(its, q).frozen) sum_grow += at(its, q).grow;
                if (sum_grow < 1 && initial_free * sum_grow < free) free = initial_free * sum_grow;
            }
            double violation = 0;
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                if (it.frozen) continue;
                double target = it.base;
                if (factors > 0) target = it.base + free * (grow ? it.grow : it.shrink * it.base) / factors;
                double clamped = target < it.mn ? it.mn : target > it.mx ? it.mx : target;
                if (clamped < 0) clamped = 0;
                violation += clamped - target;
                it.size = clamped;
                its[q] = it;
            }
            /* freeze: all when nothing was clamped; else those clamped the way the total went */
            for (size_t q = a; q < z; q++) {
                lay_fi it = at(its, q);
                if (it.frozen) continue;
                double target = it.base;
                if (factors > 0) target = it.base + free * (grow ? it.grow : it.shrink * it.base) / factors;
                if (violation == 0 || (violation > 0 && it.size > target) || (violation < 0 && it.size < target)) it.frozen = 1;
                its[q] = it;
            }
        }
    }
    /* the items laid out at their main sizes (a row's: their widths); cross sizes */
    for (size_t q = 0; q < ni; q++) {
        lay_fi it = at(its, q);
        if (row) {
            it.box = lay_item_box(LARGS, b, el, &it, &items, W, it.size, defh);
            it.cross = lay_border_height(t, it.box);
        } else {
            /* a column's item: its height the main size it was given */
            double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
            t->box[it.box].h = it.size - ve > 0 ? it.size - ve : 0;
        }
        its[q] = it;
    }
    /* each line's cross size (9.4, 8): its tallest item's; in a row, the items aligned on
       their first baselines as tall as the most any rises above that baseline and the most any
       falls below it (on their last ones likewise); the container's, a single line's when
       definite. Per line: those first baselines' rise (lup) and the last ones' fall (ldn); in a
       column, the widest item aligned on its last baseline (which a column has none of: the
       items share an edge at the end) */
    vector<double> lcross, lpos, lup, ldn;
    double cross_total = 0;
    double cross_avail = row ? defh : W;
    for (size_t l = 0; l < nl; l++) {
        double c = 0, up0 = -1e30, dn0 = -1e30, up1 = -1e30, dn1 = -1e30;
        for (size_t q = at(lstart, l); q < at(lstart, l + 1); q++) {
            lay_fi it = at(its, q);
            double o = it.cross + it.cm0 + it.cm1;
            if ((it.align == AJ_BASELINE || it.align == AJ_LBASE) && !it.auto_c0 && !it.auto_c1) {
                if (!row) { if (it.align == AJ_LBASE && o > dn1) dn1 = o; if (o > c) c = o; continue; }
                int last = it.align == AJ_LBASE;
                double rise, yb;
                if (last ? lay_last_baseline(t, it.box, &yb, 0) : lay_first_baseline(t, it.box, &yb, 0))
                    rise = it.cm0 + t->box[it.box].bt + t->box[it.box].pt + yb;
                else rise = it.cm0 + it.cross;
                it.above = rise;
                its[q] = it;
                if (last) { if (rise > up1) up1 = rise; if (o - rise > dn1) dn1 = o - rise; }
                else { if (rise > up0) up0 = rise; if (o - rise > dn0) dn0 = o - rise; }
                continue;
            }
            if (o > c) c = o;
        }
        if (up0 > -1e29 && up0 + dn0 > c) c = up0 + dn0;
        if (up1 > -1e29 && up1 + dn1 > c) c = up1 + dn1;
        if (nl == 1 && !e.fwrap && cross_avail >= 0) c = cross_avail;
        push_back(lcross, c);
        push_back(lup, up0);
        push_back(ldn, dn1);
        cross_total += c + (l > 0 ? gap_cross : 0);
    }
    /* align-content: the lines in the container's cross size, when it has one */
    double lstart_off = 0, lbetween = 0;
    if (cross_avail >= 0 && (e.fwrap || nl > 1)) {
        double free = cross_avail - cross_total;
        int ac = e.ac == AJ_NORMAL ? AJ_STRETCH : e.ac;
        if (ac == AJ_STRETCH && free > 0) {
            for (size_t l = 0; l < nl; l++) lcross[l] = at(lcross, l) + free / (double)nl;
            cross_total = cross_avail;
        } else lay_distribute(ac, free, nl, &lstart_off, &lbetween);
    }
    double cy = lstart_off;
    for (size_t l = 0; l < nl; l++) {
        push_back(lpos, cy);
        cy += at(lcross, l) + gap_cross + lbetween;
    }
    if (e.fwrap == 2) {
        /* wrap-reverse: the lines from the cross end */
        double total = cy - gap_cross - lbetween;
        double span = cross_avail >= 0 ? cross_avail : total;
        for (size_t l = 0; l < nl; l++) lpos[l] = span - at(lpos, l) - at(lcross, l);
    }
    /* main positions: auto margins, else justify-content; cross positions: align-self */
    double main_size = main_avail;
    if (main_size < 0) {
        main_size = 0;
        for (size_t q = 0; q < ni; q++) main_size += at(its, q).size + at(its, q).mm0 + at(its, q).mm1 + (q > 0 ? gap_main : 0);
    }
    for (size_t l = 0; l < nl; l++) {
        size_t a = at(lstart, l), z = at(lstart, l + 1);
        double used = 0;
        int autos = 0;
        for (size_t q = a; q < z; q++) {
            used += at(its, q).size + at(its, q).mm0 + at(its, q).mm1 + (q > a ? gap_main : 0);
            autos += at(its, q).auto_m0 + at(its, q).auto_m1;
        }
        double free = main_size - used, start = 0, between = 0, per_auto = 0;
        if (autos && free > 0) per_auto = free / autos;
        else {
            int jc = e.jc;
            if (jc == AJ_NORMAL || jc == AJ_STRETCH) jc = AJ_START;
            lay_distribute(jc, free, z - a, &start, &between);
        }
        double mpos = start;
        for (size_t q = a; q < z; q++) {
            lay_fi it = at(its, q);
            double m0 = it.mm0 + (it.auto_m0 ? per_auto : 0), m1 = it.mm1 + (it.auto_m1 ? per_auto : 0);
            double mp = mpos + m0;
            mpos += m0 + it.size + m1 + gap_main + between;
            if (e.frev) mp = main_size - mp - it.size;
            /* the cross axis */
            double lc = at(lcross, l);
            double c0 = it.cm0, c1 = it.cm1;
            if (it.align == AJ_STRETCH && it.cross_auto && !it.auto_c0 && !it.auto_c1 && row) {
                /* stretched: as tall as the line */
                double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
                double hh = lc - c0 - c1 - ve;
                t->box[it.box].h = hh > 0 ? hh : 0;
                it.cross = lc - c0 - c1;
            }
            double cfree = lc - it.cross - c0 - c1, cp = c0;
            if (it.auto_c0 && it.auto_c1) cp = c0 + cfree / 2;
            else if (it.auto_c0) cp = c0 + cfree;
            else if (it.auto_c1) cp = c0;
            else if (it.align == AJ_END) cp = c0 + cfree;
            else if (it.align == AJ_CENTER) cp = c0 + cfree / 2;
            else if (it.align == AJ_BASELINE && row) cp = c0 + at(lup, l) - it.above;
            else if (it.align == AJ_LBASE) cp = c0 + (row ? lc - at(ldn, l) - it.above : lc - at(ldn, l));
            cp += at(lpos, l);
            if (row) lay_place(t, it.box, mp, cp);
            else lay_place(t, it.box, cp, mp);
            its[q] = it;
        }
    }
    if (defh < 0) t->box[b].h = row ? (cy > 0 ? cy - gap_cross - lbetween : 0) : main_size;
    if (row && defh < 0 && t->box[b].h < 0) t->box[b].h = 0;
    /* the container's first baseline (8.5), as Chromium finds it, in the order the lines and items are drawn in (a
       reversed direction or wrap reversing it): of its top line, its items aligned on their first baselines; else its
       first item there (the leftmost in a row, the topmost in a column) -- its first baseline, the bottom of its border
       box when it has none. Its last baseline likewise, of its bottom line: its items aligned on their last baselines,
       flush to the line's end, else its last item there (the rightmost, the bottommost) and its last baseline */
    if (ni) {
        size_t lf = 0, ll = 0;
        for (size_t l = 1; l < nl; l++) {
            if (at(lpos, l) < at(lpos, lf)) lf = l;
            if (at(lpos, l) > at(lpos, ll)) ll = l;
        }
        uint32_t ib = 0, lb = 0;
        double ibm = 0, lbm = 0;
        for (size_t l = 0; l < nl; l++) {
            if (l != lf && l != ll) continue;
            for (size_t q = at(lstart, l); q < at(lstart, l + 1); q++) {
                uint32_t x = at(its, q).box;
                double m = row ? t->box[x].x - t->box[x].pl - t->box[x].bl : t->box[x].y - t->box[x].pt - t->box[x].bt;
                if (l == lf && (!ib || m < ibm)) { ib = x; ibm = m; }
                if (l == ll && (!lb || m >= lbm)) { lb = x; lbm = m; }
            }
        }
        double fb, yb;
        if (row && at(lup, lf) > -1e29) fb = at(lpos, lf) + at(lup, lf);
        else if (lay_first_baseline(t, ib, &yb, 0)) fb = t->box[ib].y + yb;
        else fb = t->box[ib].y - t->box[ib].pt - t->box[ib].bt + lay_border_height(t, ib);
        t->box[b].baseline = fb;
        t->box[b].has_baseline = 1;
        t->box[b].has_lbaseline = 1;
        if (row && at(ldn, ll) > -1e29) { fb = at(lpos, ll) + at(lcross, ll) - at(ldn, ll); t->box[b].has_lbaseline = 2; }
        else if (lay_last_baseline(t, lb, &yb, 0)) fb = t->box[lb].y + yb;
        else fb = t->box[lb].y - t->box[lb].pt - t->box[lb].bt + lay_border_height(t, lb);
        t->box[b].lbaseline = fb;
    }
}

/* ---- grid containers (CSS Grid 1, 7 to 11) ---- */

/* a track's sizing: its min and its max (fixed px, auto, min-content, max-content, fr,
   fit-content's limit), its base size and growth limit (LAY_INF while it has none), whether
   that limit may still grow past what an item gave it (11.5's infinitely growable), the
   increase a step of 11.5 plans for it, its place; pct: a percentage read as auto, the
   grid's size not definite (its value, -1 for none) */
enum { TK_FIXED, TK_AUTO, TK_MINC, TK_MAXC, TK_FR, TK_FIT };
#define LAY_INF 1e30
/* fit: 1 a track an auto-fit repeat made, 2 one that collapsed, no item in it */
typedef struct lay_track { int mink, maxk, grows, touched, fit; double minv, maxv, base, limit, pos, plan, pct; } lay_track;

/* a line's name (its hash) and the line it names, 1 the first line of its list */
typedef struct lay_lname { uint64_t h; int32_t line; } lay_lname;

/* the hash lay_hash gives a text that goes on from one whose hash is h with s[0, n) */
static uint64_t lay_hash_cont(uint64_t h, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    return h;
}

/* one track size as written (px, %, fr, auto, min-content, max-content): its kind and value;
   a percentage of an avail that is not definite auto, its value in *pct */
static int lay_track_one(const char *s, size_t n, double avail, int *kind, double *v, double *pct) {
    *v = 0;
    if (lay_is_word(s, n, "auto")) { *kind = TK_AUTO; return 1; }
    if (lay_is_word(s, n, "min-content")) { *kind = TK_MINC; return 1; }
    if (lay_is_word(s, n, "max-content")) { *kind = TK_MAXC; return 1; }
    char buf[64];
    size_t k = n < sizeof buf - 1 ? n : sizeof buf - 1;
    memcpy(buf, s, k);
    buf[k] = 0;
    char *end = buf;
    double x = strtod(buf, &end);
    if (end == buf) return 0;
    if (strcmp(end, "fr") == 0) { *kind = TK_FR; *v = x; return 1; }
    if (strcmp(end, "%") == 0) {
        if (avail < 0) { *kind = TK_AUTO; *pct = x; return 1; }
        *kind = TK_FIXED;
        *v = avail * x / 100;
        return 1;
    }
    *kind = TK_FIXED;
    *v = x;
    return 1;
}

/* the names of a bracketed list, its inside s[0, n), each naming line */
static void lay_line_names(const char *s, size_t n, int32_t line, vector<lay_lname> *names) {
    size_t i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
        size_t b = i;
        while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\n') i++;
        if (i == b) continue;
        lay_lname x;
        x.h = lay_hash(s + b, i - b);
        x.line = line;
        push_back(names, x);
    }
}

static void lay_tracks_in(const char *s, size_t n, double avail, double gap, vector<lay_track> *out, vector<lay_lname> *names, int depth,
                          int skip_auto, double rest, long rest_n);
/* a track's size as an automatic repeat counts it: its maximum when definite, else its minimum when definite */
static double lay_track_definite(lay_track o) {
    double v = o.maxk == TK_FIXED ? o.maxv : o.mink == TK_FIXED ? o.minv : 0;
    if (o.mink == TK_FIXED && o.minv > v) v = o.minv;
    return v;
}
/* a track list (grid-template-columns' or -rows' computed text) into out, and its lines'
   names into names (a list's names name the line before the track after them):
   repeat() (a count, auto-fill, auto-fit; its names repeated too), minmax(), fit-content().
   An automatic repeat (auto-fill, auto-fit) repeats as often as fits in avail beside the
   list's other tracks and the gaps (Grid 1, 7.2.3.2): those are measured first */
static void lay_tracks(const char *s, size_t n, double avail, double gap, vector<lay_track> *out, vector<lay_lname> *names, int depth) {
    vector<lay_track> other;
    vector<lay_lname> onames;
    lay_tracks_in(s, n, avail, gap, &other, &onames, depth, 1, 0, 0);
    double rest = 0;
    for (size_t q = 0; q < len(other); q++) rest += lay_track_definite(at(other, q));
    lay_tracks_in(s, n, avail, gap, out, names, depth, 0, rest, (long)len(other));
}
static void lay_tracks_in(const char *s, size_t n, double avail, double gap, vector<lay_track> *out, vector<lay_lname> *names, int depth,
                          int skip_auto, double rest, long rest_n) {
    size_t i = 0, a, ln, fa, fn;
    while (css_next_part(s, n, &i, &a, &ln)) {
        const char *p = s + a;
        if (p[0] == '[') {
            size_t e = a + 1;
            while (e < n && s[e] != ']') e++;
            lay_line_names(s + a + 1, e - a - 1, (int32_t)len(*out) + 1, names);
            i = e < n ? e + 1 : n;
            continue;
        }
        lay_track tk;
        memset(&tk, 0, sizeof tk);
        tk.pct = -1;
        double unused = -1;
        if (lay_call(p, ln, "repeat", &fa, &fn) && depth < 2) {
            size_t j = 0, ca, cn, la, lnn;
            if (!lay_arg(p + fa, fn, &j, &ca, &cn) || !lay_arg(p + fa, fn, &j, &la, &lnn)) continue;
            /* the rest of the arguments: the tracks repeated */
            lnn = fn - la;
            vector<lay_track> one;
            vector<lay_lname> onames;
            lay_tracks(p + fa + la, lnn, avail, gap, &one, &onames, depth + 1);
            if (!len(one)) continue;
            long count = 0;
            int fit = lay_is_word(p + fa + ca, cn, "auto-fit");
            if (fit || lay_is_word(p + fa + ca, cn, "auto-fill")) {
                if (skip_auto) continue;
                double each = 0;
                for (size_t q = 0; q < len(one); q++) each += lay_track_definite(at(one, q)) + gap;
                double room = avail - rest - gap * (double)(rest_n - 1);
                count = each > 0 && avail >= 0 ? (long)(room / each) : 1;
                if (count < 1) count = 1;
                if (fit) for (size_t q = 0; q < len(one); q++) one[q].fit = 1;
            } else {
                for (size_t q = 0; q < cn; q++) if (p[fa + ca + q] >= '0' && p[fa + ca + q] <= '9') count = count * 10 + (p[fa + ca + q] - '0');
            }
            if (count > 1000) count = 1000;
            for (long c = 0; c < count; c++) {
                int32_t base = (int32_t)len(*out);
                for (size_t q = 0; q < len(onames); q++) { lay_lname x = at(onames, q); x.line += base; push_back(names, x); }
                for (size_t q = 0; q < len(one); q++) push_back(out, at(one, q));
            }
            continue;
        }
        if (lay_call(p, ln, "minmax", &fa, &fn)) {
            size_t j = 0, xa, xn, ya, yn;
            if (!lay_arg(p + fa, fn, &j, &xa, &xn) || !lay_arg(p + fa, fn, &j, &ya, &yn)) continue;
            if (!lay_track_one(p + fa + xa, xn, avail, &tk.mink, &tk.minv, &unused) || !lay_track_one(p + fa + ya, yn, avail, &tk.maxk, &tk.maxv, &unused)) continue;
            if (tk.mink == TK_FR) tk.mink = TK_AUTO;
            push_back(out, tk);
            continue;
        }
        if (lay_call(p, ln, "fit-content", &fa, &fn)) {
            int kk;
            double v;
            if (!lay_track_one(p + fa, fn, avail, &kk, &v, &unused)) continue;
            tk.mink = TK_AUTO;
            tk.maxk = TK_FIT;
            tk.maxv = v;
            push_back(out, tk);
            continue;
        }
        int kk;
        double v;
        if (!lay_track_one(p, ln, avail, &kk, &v, &tk.pct)) continue;
        tk.mink = kk == TK_FR ? TK_AUTO : kk;
        tk.minv = v;
        tk.maxk = kk;
        tk.maxv = v;
        push_back(out, tk);
    }
}

/* a named area of grid-template-areas: its name's hash, its -start and -end lines' names'
   hashes, its rows [r0, r1) and columns [c0, c1) */
typedef struct lay_area { uint64_t h, hs, he; uint32_t r0, r1, c0, c1; } lay_area;

/* grid-template-areas' strings s[0, n) (the cascade has checked them) into areas, and the
   rows and columns they make */
static void lay_areas(const char *s, size_t n, vector<lay_area> *areas, uint32_t *rows, uint32_t *cols) {
    size_t i = 0, a, ln;
    *rows = 0;
    *cols = 0;
    while (css_next_part(s, n, &i, &a, &ln)) {
        const char *q = s + a;
        if (ln < 2 || (q[0] != '"' && q[0] != '\'')) continue;
        uint32_t c = 0;
        size_t k = 1;
        while (k + 1 < ln) {
            char ch = q[k];
            if (ch == ' ' || ch == '\t' || ch == '\n') { k++; continue; }
            size_t b = k;
            if (ch == '.') { while (k + 1 < ln && q[k] == '.') k++; c++; continue; }
            while (k + 1 < ln && q[k] != ' ' && q[k] != '\t' && q[k] != '\n' && q[k] != '.') k++;
            uint64_t h = lay_hash(q + b, k - b);
            size_t z = 0;
            while (z < len(*areas) && at(*areas, z).h != h) z++;
            if (z == len(*areas)) {
                lay_area ar;
                ar.h = h;
                ar.hs = lay_hash_cont(h, "-start", 6);
                ar.he = lay_hash_cont(h, "-end", 4);
                ar.r0 = *rows;
                ar.r1 = *rows + 1;
                ar.c0 = c;
                ar.c1 = c + 1;
                push_back(areas, ar);
            } else {
                lay_area ar = at(*areas, z);
                if (*rows + 1 > ar.r1) ar.r1 = *rows + 1;
                if (c + 1 > ar.c1) ar.c1 = c + 1;
                (*areas)[z] = ar;
            }
            c++;
        }
        if (c > *cols) *cols = c;
        (*rows)++;
    }
}

/* a grid line as written (grid-column-start's computed text, Grid 1, 8.3): auto; a number
   (from the end when negative), with a name the nth line of that name; span and a count, or
   a name; a name alone (an area's -start or -end line, else the first line of the name) */
enum { GL_AUTO, GL_LINE, GL_SPAN, GL_NAME };
typedef struct lay_gl { int kind, named; long n; uint64_t h, hs, he; } lay_gl;

static void lay_grid_line(const css_style *st, uint32_t c, lay_gl *g) {
    memset(g, 0, sizeof *g);
    vector<char> src;
    amb_span sp = st->cval[c].text;
    append_range(src, text_ptr(st, sp), sp.len);
    size_t i = 0, a, n;
    int is_span = 0, digits = 0;
    long v = 0;
    while (css_next_part(data(src), len(src), &i, &a, &n)) {
        const char *p = data(src) + a;
        if (lay_is_word(p, n, "span")) { is_span = 1; continue; }
        if (lay_is_word(p, n, "auto")) continue;
        int sign = p[0] == '-' || p[0] == '+';
        if ((p[0] >= '0' && p[0] <= '9') || (sign && n > 1 && p[1] >= '0' && p[1] <= '9')) {
            long x = 0;
            for (size_t q = sign ? 1 : 0; q < n && p[q] >= '0' && p[q] <= '9'; q++) if (x < 100000) x = x * 10 + (p[q] - '0');
            v = p[0] == '-' ? -x : x;
            digits = 1;
            continue;
        }
        g->named = 1;
        g->h = lay_hash(p, n);
        g->hs = lay_hash_cont(g->h, "-start", 6);
        g->he = lay_hash_cont(g->h, "-end", 4);
    }
    if (is_span) { g->kind = GL_SPAN; g->n = digits && v > 0 ? v : 1; return; }
    if (digits && v != 0) { g->kind = GL_LINE; g->n = v; return; }
    g->kind = g->named ? GL_NAME : GL_AUTO;
}

/* line l has the name h */
static int lay_named(const vector<lay_lname> *nm, uint64_t h, long l) {
    for (size_t k = 0; k < len(*nm); k++) if (at(*nm, k).h == h && at(*nm, k).line == l) return 1;
    return 0;
}
/* the nth line of the name h among the explicit grid's lines 1 to L (from its start when
   n > 0, from its end when n < 0), the implicit lines past them taken to have every name */
static long lay_nth_named(const vector<lay_lname> *nm, uint64_t h, long n, long L) {
    long k = 0;
    if (n > 0) {
        for (long l = 1; l <= L; l++) if (lay_named(nm, h, l) && ++k == n) return l;
        return L + (n - k);
    }
    for (long l = L; l >= 1; l--) if (lay_named(nm, h, l) && ++k == -n) return l;
    return 1 - (-n - k);
}
/* the line a span reaches from line `from`, ahead (dir 1) or back (-1): its count of lines,
   or of lines of its name, the implicit lines taken to have the name */
static long lay_span_to(const vector<lay_lname> *nm, const lay_gl *g, long from, int dir, long L) {
    if (!g->named) return from + dir * g->n;
    long k = 0;
    if (dir > 0) {
        for (long l = from + 1; l <= L; l++) if (lay_named(nm, g->h, l) && ++k == g->n) return l;
        return (from > L ? from : L) + (g->n - k);
    }
    for (long l = from - 1; l >= 1; l--) if (lay_named(nm, g->h, l) && ++k == g->n) return l;
    return (from < 1 ? from : 1) - (g->n - k);
}
/* the line a start line (end 0) or an end line (end 1) as written names */
static long lay_line_at(const vector<lay_lname> *nm, const lay_gl *g, int end, long L) {
    if (g->kind == GL_NAME) {
        uint64_t hx = end ? g->he : g->hs;
        for (long l = 1; l <= L; l++) if (lay_named(nm, hx, l)) return l;
        return lay_nth_named(nm, g->h, 1, L);
    }
    if (g->named) return lay_nth_named(nm, g->h, g->n, L);
    return g->n > 0 ? g->n : L + 1 + g->n;
}
/* an item's place along an axis (Grid 1, 8.3, its conflicts handled as 8.3.1 has them): its
   first line and its span, the explicit grid's lines numbered 1 to L; 0 when it has no
   definite line on the axis, its span alone found */
static int lay_place_axis(const vector<lay_lname> *nm, const lay_gl *s0, const lay_gl *e0, long L, long *start, long *span) {
    lay_gl s = *s0, e = *e0;
    if (s.kind == GL_SPAN && e.kind == GL_SPAN) e.kind = GL_AUTO;
    int sl = s.kind == GL_LINE || s.kind == GL_NAME, el = e.kind == GL_LINE || e.kind == GL_NAME;
    if (!sl && !el) {
        long k = s.kind == GL_SPAN ? (s.named ? 1 : s.n) : e.kind == GL_SPAN ? (e.named ? 1 : e.n) : 1;
        *span = k < 1 ? 1 : k > 1000 ? 1000 : k;
        return 0;
    }
    long a = sl ? lay_line_at(nm, &s, 0, L) : 0, z = el ? lay_line_at(nm, &e, 1, L) : 0;
    if (sl && el) {
        if (a > z) { long x = a; a = z; z = x; }
        if (a == z) z = a + 1;
    } else if (sl) z = e.kind == GL_SPAN ? lay_span_to(nm, &e, a, 1, L) : a + 1;
    else a = s.kind == GL_SPAN ? lay_span_to(nm, &s, z, -1, L) : z - 1;
    if (a < -1000) a = -1000;
    if (z > 2000) z = 2000;
    if (z <= a) z = a + 1;
    *start = a;
    *span = z - a;
    return 1;
}

/* an item's contributions to the tracks it spans along an axis: its first track and span,
   its min-content and max-content contributions and its minimum contribution (outer sizes) */
typedef struct lay_gc { uint32_t first, span; double mn, mx, minc; } lay_gc;

static int lay_tk_imin(lay_track k) { return k.mink == TK_AUTO || k.mink == TK_MINC || k.mink == TK_MAXC; }
static int lay_tk_imax(lay_track k) { return k.maxk == TK_AUTO || k.maxk == TK_MINC || k.maxk == TK_MAXC || k.maxk == TK_FIT; }
static int lay_tk_maxc(lay_track k) { return k.maxk == TK_AUTO || k.maxk == TK_MAXC || k.maxk == TK_FIT; }

/* space shared among tracks (inc[k] what track k gets, room[k] how much more it may take,
   w[k] its weight, open[k] whether it takes any): equally by weight, a track that reaches
   its room taking no more; what is left of the space */
static double lay_fill(vector<double> *inc, const vector<double> *room, const vector<double> *w, const vector<uint8_t> *open, double space) {
    size_t n = len(*inc);
    for (int round = 0; round < 64 && space > 1e-9; round++) {
        double sw = 0, step = -1;
        for (size_t k = 0; k < n; k++) {
            if (!at(*open, k) || at(*inc, k) >= at(*room, k) - 1e-9) continue;
            sw += at(*w, k);
            double s = (at(*room, k) - at(*inc, k)) / at(*w, k);
            if (step < 0 || s < step) step = s;
        }
        if (sw <= 0) break;
        double per = space / sw;
        if (step >= per) step = per;
        for (size_t k = 0; k < n; k++) {
            if (!at(*open, k) || at(*inc, k) >= at(*room, k) - 1e-9) continue;
            (*inc)[k] = at(*inc, k) + step * at(*w, k);
        }
        space -= step * sw;
    }
    return space > 0 ? space : 0;
}

/* a step of 11.5.3 for a group of items (gc's, each an index in group): the space each
   item's contribution (step 1: its minimum one; 2, 5: its min-content one; 3, 6: its
   max-content one) needs past the sizes of the tracks it spans (the base sizes in steps
   1 to 3, the growth limits in 5 and 6), shared among those of its tracks the step affects
   up to their limits, then past them (11.5.1); each track's size then grows by the most
   an item asked of it. flex: the flexible tracks alone take space, by their factors (11.5.4).
   Under a min- or max-content constraint as in a layout, as Chromium has it: its minimum
   contributions, not the limited contributions the spec names, and in step 3 the tracks of a
   max-content minimum alone */
static void lay_spread(vector<lay_track> *tr, const vector<lay_gc> *gc, const vector<uint32_t> *group, double gap, int step, int flex) {
    size_t n = len(*tr);
    int on_limit = step >= 5;
    for (size_t q = 0; q < n; q++) { (*tr)[q].plan = 0; (*tr)[q].touched = 0; }
    for (size_t g = 0; g < len(*group); g++) {
        lay_gc c = at(*gc, at(*group, g));
        uint32_t z = c.first + c.span < n ? c.first + c.span : (uint32_t)n;
        double want = step == 1 ? c.minc : (step == 2 || step == 5) ? c.mn : c.mx;
        double have = gap * (double)(c.span - 1);
        vector<double> inc, room, w;
        vector<uint8_t> aff, beyond;
        int naff = 0, nbeyond = 0;
        double frs = 0;
        for (uint32_t k = c.first; k < z; k++) {
            lay_track tk = at(*tr, k);
            have += on_limit && tk.limit < LAY_INF ? tk.limit : tk.base;
            int a;
            if (flex && tk.maxk != TK_FR) a = 0;
            else if (step == 1) a = lay_tk_imin(tk);
            else if (step == 2) a = tk.mink == TK_MINC || tk.mink == TK_MAXC;
            else if (step == 3) a = tk.mink == TK_MAXC;
            else if (step == 5) a = lay_tk_imax(tk);
            else a = lay_tk_maxc(tk);
            /* how far it grows before it meets its limit: a base size its growth limit (a
               fit-content track's argument too), a growth limit none past a finite one */
            double r = 0;
            if (!on_limit) {
                r = (tk.limit >= LAY_INF ? LAY_INF : tk.limit) - tk.base;
                if (tk.maxk == TK_FIT && tk.maxv - tk.base < r) r = tk.maxv - tk.base;
            } else {
                r = tk.limit >= LAY_INF || tk.grows ? LAY_INF : 0;
                /* step 6 grows a fit-content track's limit no further than its argument */
                if (step == 6 && tk.maxk == TK_FIT) {
                    double cap = tk.maxv - (tk.limit < LAY_INF ? tk.limit : tk.base);
                    if (cap < r) r = cap;
                }
            }
            if (r < 0) r = 0;
            /* past the limits: a track whose max is intrinsic (steps 1, 2), max-content (3), any (5, 6) */
            int bz = !a ? 0 : step <= 2 ? lay_tk_imax(tk) : step == 3 ? lay_tk_maxc(tk) : 1;
            push_back(inc, 0.0);
            push_back(room, r);
            push_back(w, flex ? tk.maxv : 1.0);
            push_back(aff, (uint8_t)a);
            push_back(beyond, (uint8_t)bz);
            naff += a;
            nbeyond += bz;
            if (flex && a) frs += tk.maxv;
            if (a) (*tr)[k].touched = 1;
        }
        double space = want - have;
        if (space <= 0 || !naff) continue;
        if (flex && frs < 1) for (size_t k = 0; k < len(w); k++) w[k] = 1.0;
        space = lay_fill(&inc, &room, &w, &aff, space);
        if (space > 1e-9) {
            /* past the limits: to the tracks of the step's kind (a fit-content track up to
               its argument), else to every affected track */
            vector<double> past;
            for (size_t k = 0; k < len(room); k++) {
                lay_track tk = at(*tr, c.first + (uint32_t)k);
                double r = LAY_INF;
                if ((step == 3 || step == 6) && tk.maxk == TK_FIT) {
                    r = tk.maxv - (on_limit && tk.limit < LAY_INF ? tk.limit : tk.base);
                    if (r < at(inc, k)) r = at(inc, k);
                }
                push_back(past, r);
            }
            if (nbeyond) space = lay_fill(&inc, &past, &w, &beyond, space);
            if (space > 1e-9) {
                /* then to every affected track (a fit-content track's limit, in step 6, still
                   held at its argument) */
                for (size_t k = 0; k < len(past); k++)
                    if (!(step == 6 && at(*tr, c.first + (uint32_t)k).maxk == TK_FIT)) past[k] = LAY_INF;
                lay_fill(&inc, &past, &w, &aff, space);
            }
        }
        for (size_t k = 0; k < len(inc); k++) {
            uint32_t tq = c.first + (uint32_t)k;
            if (at(aff, k) && at(inc, k) > at(*tr, tq).plan) (*tr)[tq].plan = at(inc, k);
        }
    }
    for (size_t q = 0; q < n; q++) {
        lay_track tk = at(*tr, q);
        if (!tk.touched) continue;
        if (!on_limit) tk.base += tk.plan;
        else if (tk.limit >= LAY_INF) {
            tk.limit = tk.base + tk.plan;
            if (step == 5) tk.grows = 1;
        } else tk.limit += tk.plan;
        (*tr)[q] = tk;
    }
}

/* the size of an fr among tracks [a, z) to fill space (11.7.1): the space less the tracks
   that are not flexible and the gaps, over the factors (1 at least), a flexible track whose
   base size is more than its share taken as one that is not */
static double lay_fr_size(const vector<lay_track> *tr, size_t a, size_t z, double space, double gap) {
    if (z <= a) return 0;
    vector<uint8_t> fixed;
    resize(&fixed, z - a, (uint8_t)0);
    for (int round = 0; round < 32; round++) {
        double left = space - gap * (double)(z - a - 1), f = 0;
        for (size_t q = a; q < z; q++) {
            lay_track tk = at(*tr, q);
            if (tk.maxk == TK_FR && !at(fixed, q - a)) f += tk.maxv;
            else left -= tk.base;
        }
        double fr = left / (f < 1 ? 1 : f);
        int again = 0;
        for (size_t q = a; q < z; q++) {
            lay_track tk = at(*tr, q);
            if (tk.maxk == TK_FR && !at(fixed, q - a) && fr * tk.maxv < tk.base) { fixed[q - a] = 1; again = 1; }
        }
        if (!again) return fr > 0 ? fr : 0;
    }
    return 0;
}

/* the tracks' sizes along an axis (Grid 1, 11.4 to 11.8): their base sizes and growth
   limits from the items in them -- those that span one track, then the others by their
   spans, then those that cross a flexible track -- the free space to the growth limits, the
   fr tracks their share of what is left, the auto ones stretched over the rest (stretch).
   avail: the grid's size along the axis, -1 when it is not definite. mode: 0 the grid laid
   out; 1 sized under a min-content constraint (no free space shared, an fr 0), 2 under a
   max-content one (the free space infinite, an fr found as when it is indefinite): the grid
   container's own min- and max-content widths, Chromium's two runs of its algorithm, which
   take the items' contributions as a layout does (measured) */
static void lay_size_tracks(vector<lay_track> *tr, const vector<lay_gc> *gc, double avail, double gap, int stretch, int mode) {
    size_t n = len(*tr), ng = len(*gc);
    if (mode) { avail = -1; stretch = 0; }
    for (size_t q = 0; q < n; q++) {
        lay_track tk = at(*tr, q);
        tk.base = tk.mink == TK_FIXED ? tk.minv : 0;
        tk.limit = tk.maxk == TK_FIXED ? tk.maxv : LAY_INF;
        if (tk.limit < tk.base) tk.limit = tk.base;
        tk.grows = 0;
        (*tr)[q] = tk;
    }
    /* 11.5.2: the items that span one track, one that is not flexible */
    vector<double> lim;
    resize(&lim, n, -1.0);
    vector<uint8_t> crosses;
    resize(&crosses, ng, (uint8_t)0);
    size_t maxspan = 1;
    for (size_t g = 0; g < ng; g++) {
        lay_gc c = at(*gc, g);
        if (c.first >= n) continue;
        uint32_t z = c.first + c.span < n ? c.first + c.span : (uint32_t)n;
        for (uint32_t k = c.first; k < z; k++) if (at(*tr, k).maxk == TK_FR) crosses[g] = 1;
        if (c.span > maxspan) maxspan = c.span;
        if (c.span != 1 || at(crosses, g)) continue;
        lay_track tk = at(*tr, c.first);
        if (tk.mink == TK_MINC && c.mn > tk.base) tk.base = c.mn;
        else if (tk.mink == TK_MAXC && c.mx > tk.base) tk.base = c.mx;
        else if (tk.mink == TK_AUTO && c.minc > tk.base) tk.base = c.minc;
        double l = tk.maxk == TK_MINC ? c.mn : lay_tk_maxc(tk) ? c.mx : -1;
        if (tk.maxk == TK_FIT && l > tk.maxv) l = tk.maxv;
        if (l > at(lim, c.first)) lim[c.first] = l;
        (*tr)[c.first] = tk;
    }
    for (size_t q = 0; q < n; q++) {
        lay_track tk = at(*tr, q);
        if (at(lim, q) >= 0) tk.limit = at(lim, q);
        if (tk.limit < tk.base) tk.limit = tk.base;
        (*tr)[q] = tk;
    }
    /* 11.5.3: the items that span more, by their spans; 11.5.4: those crossing a flexible track */
    vector<uint32_t> group;
    for (size_t span = 2; span <= maxspan; span++) {
        clear(&group);
        for (size_t g = 0; g < ng; g++) if (at(*gc, g).span == span && at(*gc, g).first < n && !at(crosses, g)) push_back(&group, (uint32_t)g);
        if (!len(group)) continue;
        for (int step = 1; step <= 6; step++) {
            if (step == 4) {
                for (size_t q = 0; q < n; q++) if (at(*tr, q).limit < at(*tr, q).base) (*tr)[q].limit = at(*tr, q).base;
                continue;
            }
            lay_spread(tr, gc, &group, gap, step, 0);
        }
    }
    clear(&group);
    for (size_t g = 0; g < ng; g++) if (at(crosses, g)) push_back(&group, (uint32_t)g);
    if (len(group)) for (int step = 1; step <= 3; step++) lay_spread(tr, gc, &group, gap, step, 1);
    /* 11.5.5: a growth limit still none is the base size */
    for (size_t q = 0; q < n; q++) if (at(*tr, q).limit >= LAY_INF || at(*tr, q).limit < at(*tr, q).base) (*tr)[q].limit = at(*tr, q).base;
    /* 11.6: the free space to the growth limits, shared equally; with no definite size, each at its limit; under a
       min-content constraint there is none */
    double total = gap * (double)(n > 0 ? n - 1 : 0);
    for (size_t q = 0; q < n; q++) total += at(*tr, q).base;
    if (mode == 1) { /* no free space */ }
    else if (avail < 0) for (size_t q = 0; q < n; q++) (*tr)[q].base = at(*tr, q).limit;
    else if (avail - total > 0.001) {
        vector<double> inc, room, w;
        vector<uint8_t> open;
        for (size_t q = 0; q < n; q++) {
            push_back(inc, 0.0);
            push_back(room, at(*tr, q).limit - at(*tr, q).base);
            push_back(w, 1.0);
            push_back(open, (uint8_t)1);
        }
        lay_fill(&inc, &room, &w, &open, avail - total);
        for (size_t q = 0; q < n; q++) (*tr)[q].base = at(*tr, q).base + at(inc, q);
    }
    /* 11.7: the fr tracks, by the size of an fr: of the free space when it is definite, else
       the largest each fr track's base and each item that crosses one asks */
    double frs = 0;
    for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_FR) frs += at(*tr, q).maxv;
    if (frs > 0 && mode != 1) {
        double fr = 0;
        if (avail >= 0) fr = lay_fr_size(tr, 0, n, avail, gap);
        else {
            for (size_t q = 0; q < n; q++) {
                lay_track tk = at(*tr, q);
                if (tk.maxk != TK_FR) continue;
                double v = tk.maxv > 1 ? tk.base / tk.maxv : tk.base;
                if (v > fr) fr = v;
            }
            for (size_t g = 0; g < ng; g++) {
                if (!at(crosses, g)) continue;
                lay_gc c = at(*gc, g);
                size_t z = c.first + c.span < n ? c.first + c.span : n;
                double v = lay_fr_size(tr, c.first, z, c.mx, gap);
                if (v > fr) fr = v;
            }
        }
        for (size_t q = 0; q < n; q++) {
            lay_track tk = at(*tr, q);
            if (tk.maxk == TK_FR && fr * tk.maxv > tk.base) (*tr)[q].base = fr * tk.maxv;
        }
    }
    /* 11.8: the auto tracks stretched over what is left */
    if (stretch && avail >= 0) {
        double used = gap * (double)(n > 0 ? n - 1 : 0);
        int autos = 0;
        for (size_t q = 0; q < n; q++) { used += at(*tr, q).base; if (at(*tr, q).maxk == TK_AUTO) autos++; }
        if (avail - used > 0 && autos)
            for (size_t q = 0; q < n; q++) if (at(*tr, q).maxk == TK_AUTO) (*tr)[q].base = at(*tr, q).base + (avail - used) / autos;
    }
}

/* an item's place in the grid: along each axis (0 the columns, 1 the rows) its first line
   (as the explicit grid numbers them, then its track in the implicit grid) and its span,
   whether that line was definite, how it was placed, its area (1 + its index) */
typedef struct lay_gp { long s0, s1, n0, n1; int d0, d1, by; uint32_t area; } lay_gp;

/* the area [a0, a0 + na) x [b0, b0 + nb) (minor, major) is clear of the items of gp placed
   in both axes (placed: their tracks), mi the minor axis */
static int lay_gp_clear(const vector<lay_gp> *gp, const vector<uint8_t> *placed, int mi, long a0, long na, long b0, long nb) {
    for (size_t q = 0; q < len(*gp); q++) {
        if (!at(*placed, q)) continue;
        lay_gp g = at(*gp, q);
        long x0 = mi == 0 ? g.s0 : g.s1, xn = mi == 0 ? g.n0 : g.n1, y0 = mi == 0 ? g.s1 : g.s0, yn = mi == 0 ? g.n1 : g.n0;
        if (x0 < a0 + na && a0 < x0 + xn && y0 < b0 + nb && b0 < y0 + yn) return 0;
    }
    return 1;
}
/* the cells of occ (rows of the major axis, cols cells each) the area [a0, a0 + na) x
   [b0, b0 + nb) covers are free; set: mark them taken, the rows grown as needed */
static int lay_cells(vector<uint8_t> *occ, long cols, long a0, long na, long b0, long nb, int set) {
    long rows = cols > 0 ? (long)len(*occ) / cols : 0;
    if (set && b0 + nb > rows) { resize(occ, (size_t)((b0 + nb) * cols), (uint8_t)0); rows = b0 + nb; }
    for (long y = b0; y < b0 + nb && y < rows; y++)
        for (long x = a0; x < a0 + na && x < cols; x++) {
            if (set) (*occ)[(size_t)(y * cols + x)] = 1;
            else if (at(*occ, (size_t)(y * cols + x))) return 0;
        }
    return 1;
}

/* a track of the implicit grid, the explicit grid's line `line` its start: a template's
   track, else grid-auto-*'s pattern, forwards after the template's, backwards before */
static lay_track lay_track_at(const vector<lay_track> *tmpl, const vector<lay_track> *autos, long line) {
    lay_track tk;
    memset(&tk, 0, sizeof tk);
    tk.mink = TK_AUTO;
    tk.maxk = TK_AUTO;
    tk.pct = -1;
    long nt = (long)len(*tmpl), na = (long)len(*autos);
    if (line >= 1 && line <= nt) return at(*tmpl, (size_t)(line - 1));
    if (!na) return tk;
    if (line > nt) return at(*autos, (size_t)((line - 1 - nt) % na));
    return at(*autos, (size_t)(na - 1 - (-line) % na));
}

/* a grid container's items laid out (CSS Grid 1): placed (8.5: those with definite lines,
   those locked to a row (to a column, in a column flow), then the rest by a cursor moving
   along the rows (the columns), sparse or dense), the columns sized from the items'
   widths (11), the items laid out in their columns, the rows sized from their heights,
   each item in its area by justify-self and align-self; b's content height when defh (its
   definite one) is -1 */
/* an axis's tracks (axis 0 the columns, 1 the rows) that an auto-fit repeat made and that no item is in: collapsed,
   fixed at 0px (fit 2); the count of those that did not collapse */
static size_t lay_collapse(vector<lay_track> *tr, const vector<lay_gp> *gp, int axis) {
    size_t live = 0;
    for (size_t c = 0; c < len(*tr); c++) {
        if (at(*tr, c).fit != 1) { live++; continue; }
        int used = 0;
        for (size_t q = 0; q < len(*gp) && !used; q++) {
            lay_gp g = at(*gp, q);
            long s0 = axis == 0 ? g.s0 : g.s1, n0 = axis == 0 ? g.n0 : g.n1;
            if ((long)c >= s0 && (long)c < s0 + n0) used = 1;
        }
        if (used) { live++; continue; }
        lay_track tk = at(*tr, c);
        tk.mink = TK_FIXED;
        tk.maxk = TK_FIXED;
        tk.minv = 0;
        tk.maxv = 0;
        tk.fit = 2;
        (*tr)[c] = tk;
    }
    return live;
}

/* a grid container el's items (its, in order) placed and its tracks made (Grid 1, 7 and 8): the track lists read
   against its width W and height H (-1 when not definite: a percentage then auto, an automatic repeat once), the
   areas, each item's lines (8.3), those locked to a row (to a column, in a column flow), then the rest by a cursor
   moving along the rows (the columns), sparse or dense (8.5), implicit tracks before the explicit grid as well as
   after it. Each item's place in gp (its first track and span on each axis, the tracks before the explicit grid's
   first line counted in lead0 and lead1), the columns and the rows, an auto-fit repeat's empty tracks collapsed
   (7.2.3.2; lcols, lrows: how many did not) */
static void lay_grid_place(LCTX, uint32_t el, const vector<lay_fi> *its, double W, double H, double cgap, double rgap, vector<lay_gp> *gp,
                           vector<lay_track> *cols, vector<lay_track> *rows, long *lead0p, long *lead1p, size_t *lcols, size_t *lrows) {
    lay_ext e = LAY_X(el);
    size_t ni = len(*its);
    /* the track lists and their lines' names: the columns', the rows', the auto tracks' */
    vector<lay_track> tcol, trow, acol, arow;
    vector<lay_lname> ncol, nrow, nauto;
    vector<char> src;
    amb_span sp = st->cval[e.gtc].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), W, cgap, &tcol, &ncol, 0);
    clear(&src);
    sp = st->cval[e.gtr].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), H, rgap, &trow, &nrow, 0);
    clear(&src);
    sp = st->cval[e.gac].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), W, cgap, &acol, &nauto, 0);
    clear(&src);
    sp = st->cval[e.gar].text;
    append_range(src, text_ptr(st, sp), sp.len);
    lay_tracks(data(src), len(src), H, rgap, &arow, &nauto, 0);
    /* the areas: the explicit grid as large as they need, each area's -start and -end lines named */
    vector<lay_area> areas;
    uint32_t arows = 0, acols = 0;
    if (st->cval[e.gta].unit == CSS_UNIT_TEXT) {
        clear(&src);
        sp = st->cval[e.gta].text;
        append_range(src, text_ptr(st, sp), sp.len);
        lay_areas(data(src), len(src), &areas, &arows, &acols);
    }
    for (size_t z = 0; z < len(areas); z++) {
        lay_area ar = at(areas, z);
        lay_lname x;
        x.h = ar.hs; x.line = (int32_t)ar.c0 + 1; push_back(ncol, x);
        x.h = ar.he; x.line = (int32_t)ar.c1 + 1; push_back(ncol, x);
        x.h = ar.hs; x.line = (int32_t)ar.r0 + 1; push_back(nrow, x);
        x.h = ar.he; x.line = (int32_t)ar.r1 + 1; push_back(nrow, x);
    }
    long nx0 = (long)len(tcol) > (long)acols ? (long)len(tcol) : (long)acols;
    long nx1 = (long)len(trow) > (long)arows ? (long)len(trow) : (long)arows;
    /* grid-auto-flow */
    int colflow = 0, dense = 0;
    {
        clear(&src);
        sp = st->cval[e.gflow].text;
        append_range(src, text_ptr(st, sp), sp.len);
        size_t i = 0, a, n;
        while (css_next_part(data(src), len(src), &i, &a, &n)) {
            if (lay_is_word(data(src) + a, n, "column")) colflow = 1;
            else if (lay_is_word(data(src) + a, n, "dense")) dense = 1;
        }
    }
    /* each item's lines (8.3), and how it is placed */
    for (size_t q = 0; q < ni; q++) {
        lay_gp g;
        memset(&g, 0, sizeof g);
        lay_gl cs, ce, rs, re;
        memset(&cs, 0, sizeof cs);
        ce = cs;
        rs = cs;
        re = cs;
        uint32_t ie = at(*its, q).el;
        if (ie) {
            lay_grid_line(st, LAY_X(ie).gcs, &cs);
            lay_grid_line(st, LAY_X(ie).gce, &ce);
            lay_grid_line(st, LAY_X(ie).grs, &rs);
            lay_grid_line(st, LAY_X(ie).gre, &re);
        }
        g.d0 = lay_place_axis(&ncol, &cs, &ce, nx0 + 1, &g.s0, &g.n0);
        g.d1 = lay_place_axis(&nrow, &rs, &re, nx1 + 1, &g.s1, &g.n1);
        g.by = g.d0 || g.d1 ? LAYOUT_GRID_BY_LINES : LAYOUT_GRID_BY_AUTO;
        if (cs.kind == GL_NAME && ce.kind == GL_NAME && rs.kind == GL_NAME && re.kind == GL_NAME && cs.h == ce.h && cs.h == rs.h && cs.h == re.h)
            for (size_t z = 0; z < len(areas); z++) if (at(areas, z).h == cs.h) { g.by = LAYOUT_GRID_BY_AREA; g.area = (uint32_t)z + 1; }
        push_back(gp, g);
    }
    /* the implicit grid: tracks before the explicit grid's first line as the definite lines
       need them (lead), then each definite line made a track's index */
    long lead0 = 0, lead1 = 0, cnt0 = nx0, cnt1 = nx1;
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(*gp, q);
        if (g.d0 && 1 - g.s0 > lead0) lead0 = 1 - g.s0;
        if (g.d1 && 1 - g.s1 > lead1) lead1 = 1 - g.s1;
    }
    cnt0 += lead0;
    cnt1 += lead1;
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(*gp, q);
        if (g.d0) { g.s0 += lead0 - 1; if (g.s0 + g.n0 > cnt0) cnt0 = g.s0 + g.n0; }
        if (g.d1) { g.s1 += lead1 - 1; if (g.s1 + g.n1 > cnt1) cnt1 = g.s1 + g.n1; }
        (*gp)[q] = g;
    }
    /* the axis the cursor runs along (mi: the columns in a row flow), the one it moves on to
       (the rows); 1: those of each item placed along both */
    int mi = colflow ? 1 : 0;
    vector<uint8_t> placed;
    resize(&placed, ni, (uint8_t)0);
    for (size_t q = 0; q < ni; q++) if (at(*gp, q).d0 && at(*gp, q).d1) placed[q] = 1;
    /* those locked to a row (a column): each in it, at the first place clear of those placed
       (past those this step placed in that row, sparse) */
    vector<long> rowcur;
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(*gp, q);
        int dmi = mi == 0 ? g.d0 : g.d1, dma = mi == 0 ? g.d1 : g.d0;
        if (dmi || !dma) continue;
        long ma0 = mi == 0 ? g.s1 : g.s0, man = mi == 0 ? g.n1 : g.n0, min_ = mi == 0 ? g.n0 : g.n1;
        if ((long)len(rowcur) <= ma0) resize(&rowcur, (size_t)ma0 + 1, 0L);
        long c = dense ? 0 : at(rowcur, (size_t)ma0);
        while (!lay_gp_clear(gp, &placed, mi, c, min_, ma0, man) && c < 4000) c++;
        if (mi == 0) g.s0 = c; else g.s1 = c;
        rowcur[(size_t)ma0] = c + min_;
        (*gp)[q] = g;
        placed[q] = 1;
        if (mi == 0 && c + min_ > cnt0) cnt0 = c + min_;
        if (mi == 1 && c + min_ > cnt1) cnt1 = c + min_;
    }
    /* the minor axis's tracks: as many as the widest item placed automatically needs too */
    long ncross = mi == 0 ? cnt0 : cnt1;
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(*gp, q);
        long n = mi == 0 ? g.n0 : g.n1;
        if (n > ncross) ncross = n;
    }
    if (mi == 0) cnt0 = ncross; else cnt1 = ncross;
    /* the rest, by the cursor: the cells taken so far, then each item in turn */
    vector<uint8_t> occ;
    for (size_t q = 0; q < ni; q++) {
        if (!at(placed, q)) continue;
        lay_gp g = at(*gp, q);
        if (mi == 0) lay_cells(&occ, ncross, g.s0, g.n0, g.s1, g.n1, 1);
        else lay_cells(&occ, ncross, g.s1, g.n1, g.s0, g.n0, 1);
    }
    long cur_mi = 0, cur_ma = 0;
    for (size_t q = 0; q < ni; q++) {
        if (at(placed, q)) continue;
        lay_gp g = at(*gp, q);
        int dmi = mi == 0 ? g.d0 : g.d1;
        long a0 = mi == 0 ? g.s0 : g.s1, an = mi == 0 ? g.n0 : g.n1, bn = mi == 0 ? g.n1 : g.n0;
        if (dmi) {
            /* its column (row) given: the first row (column) clear there, from the cursor's when sparse */
            if (dense) cur_ma = 0;
            else if (a0 < cur_mi) cur_ma++;
            cur_mi = a0;
            while (!lay_cells(&occ, ncross, a0, an, cur_ma, bn, 0)) cur_ma++;
        } else {
            if (dense) { cur_ma = 0; cur_mi = 0; }
            for (;;) {
                while (cur_mi + an <= ncross && !lay_cells(&occ, ncross, cur_mi, an, cur_ma, bn, 0)) cur_mi++;
                if (cur_mi + an <= ncross) break;
                cur_ma++;
                cur_mi = 0;
            }
            a0 = cur_mi;
        }
        lay_cells(&occ, ncross, a0, an, cur_ma, bn, 1);
        if (mi == 0) { g.s0 = a0; g.s1 = cur_ma; } else { g.s1 = a0; g.s0 = cur_ma; }
        (*gp)[q] = g;
        placed[q] = 1;
    }
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(*gp, q);
        if (g.s0 + g.n0 > cnt0) cnt0 = g.s0 + g.n0;
        if (g.s1 + g.n1 > cnt1) cnt1 = g.s1 + g.n1;
    }
    /* the tracks: the templates', grid-auto-*'s before and after them */
    for (long c = 0; c < cnt0; c++) push_back(cols, lay_track_at(&tcol, &acol, c + 1 - lead0));
    for (long r = 0; r < cnt1; r++) push_back(rows, lay_track_at(&trow, &arow, r + 1 - lead1));
    /* an auto-fit repeat's tracks that no item is in collapse (Grid 1, 7.2.3.2): 0px, and the gutters beside them
       collapse too: a gutter is between two tracks that did not */
    *lcols = lay_collapse(cols, gp, 0);
    *lrows = lay_collapse(rows, gp, 1);
    *lead0p = lead0;
    *lead1p = lead1;
}

/* the items' contributions to the columns (11.5; gc, an item's each): its first column and its span, its min- and
   max-content widths (its margin box's) and its minimum contribution, against W -- those in tracks of fixed sizes
   alone not measured */
static void lay_grid_cols_gc(LCTX, uint32_t el, const vector<lay_fi> *its, const vector<uint32_t> *items, const vector<lay_gp> *gp,
                             const vector<lay_track> *cols, double W, double cgap, vector<lay_gc> *gc) {
    size_t ncols = len(*cols);
    for (size_t q = 0; q < len(*its); q++) {
        lay_gp g = at(*gp, q);
        lay_fi it = at(*its, q);
        lay_gc c;
        memset(&c, 0, sizeof c);
        c.first = (uint32_t)g.s0;
        c.span = (uint32_t)g.n0;
        int sized = 0, flexed = 0, fixed_max = 1;
        double maxes = cgap * (double)(g.n0 - 1);
        for (long k = g.s0; k < g.s0 + g.n0 && k < (long)ncols; k++) {
            lay_track tk = at(*cols, (size_t)k);
            if (tk.mink != TK_FIXED || tk.maxk != TK_FIXED) sized = 1;
            if (tk.maxk == TK_FR) flexed = 1;
            if (tk.maxk == TK_FIXED) maxes += tk.maxv; else fixed_max = 0;
        }
        if (sized) {
            lay_item_intrinsic(LARGS, el, &it, items, W, &c.mn, &c.mx);
            c.minc = c.mn;
            if (it.el) {
                lay_style is;
                lay_style_of(LARGS, it.el, &is);
                lay_ext ie = LAY_X(it.el);
                double edges = lay_edges_px(st, &is, W), box = is.b[1] + is.b[3] + lay_res(st, is.p[1], W) + lay_res(st, is.p[3], W);
                /* its contributions held by a max-width and a min-width of px (its margin box's) */
                if (is.maxw.unit == LAY_PX) {
                    double v = is.maxw.v + edges - (is.border_box ? box : 0);
                    if (c.mn > v) c.mn = v;
                    if (c.mx > v) c.mx = v;
                }
                int mwp = ie.minw.unit != LAY_AUTO && !lay_content_sized(ie.minw);
                double mwv = mwp ? lay_res(st, ie.minw, W) + edges - (is.border_box ? box : 0) : 0;
                if (mwp && ie.minw.unit == LAY_PX) {
                    if (c.mn < mwv) c.mn = mwv;
                    if (c.mx < mwv) c.mx = mwv;
                }
                /* its minimum contribution, as Chromium has it: of a width of its own, its min-content contribution;
                   else of a min-width, that; else, for a scroll container, a span across a flexible track or one across
                   no track of an auto minimum, its edges alone; else its min-content contribution, held to the tracks'
                   fixed maximums when it spans no others (the content-based minimum) */
                int spans_auto = 0;
                for (long k = g.s0; k < g.s0 + g.n0 && k < (long)ncols; k++) if (at(*cols, (size_t)k).mink == TK_AUTO) spans_auto = 1;
                if (is.width.unit == LAY_PX) c.minc = c.mn;
                else if (mwp) c.minc = mwv;
                else if (ie.clip == 1 || (g.n0 > 1 && flexed) || !spans_auto) c.minc = edges;
                else if (fixed_max && c.minc > maxes) c.minc = maxes;
            } else if (fixed_max && c.minc > maxes) c.minc = maxes;
        }
        push_back(gc, c);
    }
}

/* a grid container's min- and max-content widths, its edges aside (Grid 1, 11; cw its containing block's width): its
   items placed as its layout places them, its columns sized by the track sizing algorithm under a min-content and then
   a max-content constraint (lay_size_tracks), as Chromium sizes them, its width indefinite; a percentage gap 0 */
static void lay_grid_intrinsic(LCTX, uint32_t el, double cw, double *mn, double *mx) {
    lay_ext e = LAY_X(el);
    vector<lay_fi> its;
    vector<uint32_t> items;
    lay_items(LARGS, 0, el, &its, &items);
    double cgap = lay_res(st, e.cgap, 0), rgap = lay_res(st, e.rgap, 0);
    vector<lay_gp> gp;
    vector<lay_track> cols, rows;
    long lead0 = 0, lead1 = 0;
    size_t lcols = 0, lrows = 0;
    lay_grid_place(LARGS, el, &its, -1, -1, cgap, rgap, &gp, &cols, &rows, &lead0, &lead1, &lcols, &lrows);
    vector<lay_gc> gc;
    lay_grid_cols_gc(LARGS, el, &its, &items, &gp, &cols, cw, cgap, &gc);
    for (int mode = 1; mode <= 2; mode++) {
        lay_size_tracks(&cols, &gc, -1, cgap, 0, mode);
        double sum = lcols ? cgap * (double)(lcols - 1) : 0;
        for (size_t c = 0; c < len(cols); c++) sum += at(cols, c).base;
        if (mode == 1) *mn = sum;
        else *mx = sum;
    }
}

static void lay_grid(LCTX, uint32_t b, uint32_t el, double defh) {
    lay_ext e = LAY_X(el);
    double W = t->box[b].w;
    vector<lay_fi> its;
    vector<uint32_t> items;
    lay_items(LARGS, b, el, &its, &items);
    size_t ni = len(its);
    double cgap = lay_res(st, e.cgap, W), rgap = lay_res(st, e.rgap, defh >= 0 ? defh : 0);
    vector<lay_gp> gp;
    vector<lay_track> cols, rows;
    long lead0 = 0, lead1 = 0;
    size_t lcols = 0, lrows = 0;
    lay_grid_place(LARGS, el, &its, W, defh, cgap, rgap, &gp, &cols, &rows, &lead0, &lead1, &lcols, &lrows);
    size_t ncols = len(cols), nrows = len(rows);
    /* the columns: from the items' widths */
    vector<lay_gc> gc;
    lay_grid_cols_gc(LARGS, el, &its, &items, &gp, &cols, W, cgap, &gc);
    int jc = e.jc;
    double cdrop = cgap * (double)((ncols ? ncols - 1 : 0) - (lcols ? lcols - 1 : 0));   /* the collapsed gutters */
    lay_size_tracks(&cols, &gc, W >= 0 ? W + cdrop : W, cgap, jc == AJ_NORMAL || jc == AJ_STRETCH, 0);
    double cols_total = lcols ? cgap * (double)(lcols - 1) : 0;
    for (size_t c = 0; c < ncols; c++) cols_total += at(cols, c).base;
    double cstart = 0, cbetween = 0;
    if (jc != AJ_NORMAL && jc != AJ_STRETCH) lay_distribute(jc, W - cols_total, lcols, &cstart, &cbetween);
    double x = cstart;
    int cseen = 0;
    for (size_t c = 0; c < ncols; c++) {
        if (at(cols, c).fit == 2) { cols[c].pos = x; continue; }
        if (cseen) x += cgap + cbetween;
        cols[c].pos = x;
        x += at(cols, c).base;
        cseen = 1;
    }
    /* the items laid out in their columns: stretched, or shrunk to fit and placed by
       justify-self */
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(gp, q);
        lay_fi it = at(its, q);
        double aw = cgap * (double)(g.n0 - 1);
        for (long c = g.s0; c < g.s0 + g.n0 && c < (long)ncols; c++) aw += at(cols, (size_t)c).base;
        lay_style is;
        double ml = 0, mr = 0;
        int js = AJ_STRETCH, wauto = 1;
        if (it.el) {
            lay_style_of(LARGS, it.el, &is);
            ml = is.m[3].unit == LAY_AUTO ? 0 : lay_res(st, is.m[3], W);
            mr = is.m[1].unit == LAY_AUTO ? 0 : lay_res(st, is.m[1], W);
            js = LAY_X(it.el).js == AJ_AUTO || LAY_X(it.el).js == AJ_NORMAL ? e.ji : LAY_X(it.el).js;
            if (js == AJ_NORMAL) js = lay_replaced(d, it.el) ? AJ_START : AJ_STRETCH;
            /* a baseline across the columns, which a horizontal item has none of: its fallback, start (last baseline: end) */
            if (js == AJ_BASELINE) js = AJ_START;
            else if (js == AJ_LBASE) js = AJ_END;
            wauto = is.width.unit == LAY_AUTO;
        }
        double fw = js == AJ_STRETCH && wauto ? aw - ml - mr : -1;
        if (fw >= 0 && it.el) {
            /* stretched as far as its max-width and min-width let it */
            double box = is.b[1] + is.b[3] + lay_res(st, is.p[1], aw) + lay_res(st, is.p[3], aw);
            lay_len xw = is.maxw, nw = LAY_X(it.el).minw;
            if (xw.unit != LAY_AUTO && !lay_content_sized(xw)) {
                double v = lay_res(st, xw, aw) + (is.border_box ? 0 : box);
                if (fw > v) fw = v;
            }
            if (nw.unit != LAY_AUTO && !lay_content_sized(nw)) {
                double v = lay_res(st, nw, aw) + (is.border_box ? 0 : box);
                if (fw < v) fw = v;
            }
        }
        it.box = lay_item_box(LARGS, b, el, &it, &items, aw - ml - mr, fw, -1);
        t->box[it.box].how = LAYOUT_WIDTH_HOW_GRID;
        double bw = lay_bw(t, it.box);
        double xoff = ml;
        if (js == AJ_END) xoff = aw - mr - bw;
        else if (js == AJ_CENTER) xoff = ml + (aw - ml - mr - bw) / 2;
        it.mm0 = xoff;
        it.cross = t->box[it.box].mt + lay_border_height(t, it.box) + t->box[it.box].mb;
        its[q] = it;
    }
    /* each item's alignment across the rows (align-self; an anonymous item's the container's align-items), and of an
       item aligned on its first or last baseline, from its margin box's top to that baseline (above): its own, else
       one synthesized at its border box's bottom */
    for (size_t q = 0; q < ni; q++) {
        lay_fi it = at(its, q);
        int as = e.ai;
        if (it.el && LAY_X(it.el).as != AJ_AUTO && LAY_X(it.el).as != AJ_NORMAL) as = LAY_X(it.el).as;
        if (as == AJ_NORMAL) as = it.el && lay_replaced(d, it.el) ? AJ_START : AJ_STRETCH;
        it.align = as;
        if (as == AJ_BASELINE || as == AJ_LBASE) {
            uint32_t ib = it.box;
            double yb;
            if (as == AJ_BASELINE ? lay_first_baseline(t, ib, &yb, 0) : lay_last_baseline(t, ib, &yb, 0))
                it.above = t->box[ib].mt + t->box[ib].bt + t->box[ib].pt + yb;
            else it.above = t->box[ib].mt + lay_border_height(t, ib);
        }
        its[q] = it;
    }
    /* the baseline-sharing groups (Box Alignment 3, 9.3): a row's items aligned on their first baselines that start in
       it, the most any rises above that baseline (brise); its items aligned on their last baselines that end in it, the
       most any falls below theirs (bfall) */
    vector<double> brise, bfall;
    resize(&brise, nrows, -1.0);
    resize(&bfall, nrows, -1.0);
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(gp, q);
        lay_fi it = at(its, q);
        size_t r0 = (size_t)g.s1, r1 = (size_t)(g.s1 + g.n1 - 1);
        if (it.align == AJ_BASELINE && r0 < nrows && it.above > at(brise, r0)) brise[r0] = it.above;
        if (it.align == AJ_LBASE && r1 < nrows && it.cross - it.above > at(bfall, r1)) bfall[r1] = it.cross - it.above;
    }
    /* the rows: from the items' heights (a scroll container's minimum its edges alone), an item aligned on a baseline
       with what its group's baseline shifts it by (Grid 1, 11.5's shim) */
    clear(&gc);
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(gp, q);
        lay_fi it = at(its, q);
        lay_gc c;
        c.first = (uint32_t)g.s1;
        c.span = (uint32_t)g.n1;
        double shim = 0;
        if (it.align == AJ_BASELINE && (size_t)g.s1 < nrows) shim = at(brise, (size_t)g.s1) - it.above;
        if (it.align == AJ_LBASE && (size_t)(g.s1 + g.n1 - 1) < nrows) shim = at(bfall, (size_t)(g.s1 + g.n1 - 1)) - (it.cross - it.above);
        c.mn = it.cross + shim;
        c.mx = it.cross + shim;
        c.minc = it.cross + shim;
        if (it.el && LAY_X(it.el).clip == 1 && LAY_X(it.el).minh.unit == LAY_AUTO)
            c.minc = t->box[it.box].mt + t->box[it.box].bt + t->box[it.box].pt + t->box[it.box].pb + t->box[it.box].bb + t->box[it.box].mb;
        push_back(gc, c);
    }
    int ac = e.ac;
    double rdrop = rgap * (double)((nrows ? nrows - 1 : 0) - (lrows ? lrows - 1 : 0));
    lay_size_tracks(&rows, &gc, defh >= 0 ? defh + rdrop : defh, rgap, defh >= 0 && (ac == AJ_NORMAL || ac == AJ_STRETCH), 0);
    double rows_total = lrows ? rgap * (double)(lrows - 1) : 0;
    for (size_t rr = 0; rr < nrows; rr++) rows_total += at(rows, rr).base;
    double grid_h = rows_total;
    if (defh < 0) {
        /* a percentage row of a grid whose height is not definite: sized as auto, then of
           the height the grid has so (Grid 1, 7.2) */
        for (size_t rr = 0; rr < nrows; rr++)
            if (at(rows, rr).pct >= 0) { rows_total += grid_h * at(rows, rr).pct / 100 - at(rows, rr).base; rows[rr].base = grid_h * at(rows, rr).pct / 100; }
    }
    double rstart = 0, rbetween = 0;
    if (defh >= 0 && ac != AJ_NORMAL && ac != AJ_STRETCH) lay_distribute(ac, defh - rows_total, lrows, &rstart, &rbetween);
    double y = rstart;
    int rseen = 0;
    for (size_t rr = 0; rr < nrows; rr++) {
        if (at(rows, rr).fit == 2) { rows[rr].pos = y; continue; }
        if (rseen) y += rgap + rbetween;
        rows[rr].pos = y;
        y += at(rows, rr).base;
        rseen = 1;
    }
    /* each item in its area: align-self (stretched as tall as its rows, by default); how it was placed */
    for (size_t q = 0; q < ni; q++) {
        lay_gp g = at(gp, q);
        lay_fi it = at(its, q);
        if (g.s1 >= (long)nrows || g.s0 >= (long)ncols) continue;
        double ah = rgap * (double)(g.n1 - 1);
        for (long rr = g.s1; rr < g.s1 + g.n1 && rr < (long)nrows; rr++) ah += at(rows, (size_t)rr).base;
        int as = it.align, hauto = 1;
        if (it.el) {
            lay_style is;
            lay_style_of(LARGS, it.el, &is);
            hauto = is.height.unit == LAY_AUTO && LAY_X(it.el).hraw.unit == LAY_AUTO;
        }
        double mt = t->box[it.box].mt, mb = t->box[it.box].mb;
        if (as == AJ_STRETCH && hauto) {
            /* stretched: as tall as its area, its content overflowing a shorter one */
            double ve = t->box[it.box].bt + t->box[it.box].bb + t->box[it.box].pt + t->box[it.box].pb;
            double hh = ah - mt - mb - ve;
            t->box[it.box].h = hh > 0 ? hh : 0;
        }
        double bh = lay_border_height(t, it.box), yoff = mt;
        if (as == AJ_END) yoff = ah - mb - bh;
        else if (as == AJ_CENTER) yoff = mt + (ah - mt - mb - bh) / 2;
        /* on a baseline: its group's, at the start of its area (a last baseline's at its end) */
        else if (as == AJ_BASELINE) yoff = mt + at(brise, (size_t)g.s1) - it.above;
        else if (as == AJ_LBASE) yoff = mt + ah - (at(bfall, (size_t)(g.s1 + g.n1 - 1)) - (it.cross - it.above)) - it.cross;
        lay_place(t, it.box, at(cols, (size_t)g.s0).pos + it.mm0, at(rows, (size_t)g.s1).pos + yoff);
        t->box[it.box].grid_col = (int32_t)(g.s0 + 1 - lead0);
        t->box[it.box].grid_row = (int32_t)(g.s1 + 1 - lead1);
        t->box[it.box].grid_cols = (uint32_t)g.n0;
        t->box[it.box].grid_rows = (uint32_t)g.n1;
        t->box[it.box].grid_by = (layout_grid_by)g.by;
        if (g.area && it.el) t->box[it.box].grid_area = lay_kept(t, st, LAY_X(it.el).grs);
    }
    if (defh < 0) t->box[b].h = grid_h > 0 ? grid_h : 0;
    /* its first baseline (Grid 1, 10.1): of its first row with an item in it, its items aligned on their first baselines
       there, else the first of its items there in grid order (its first baseline, else its border box's bottom); its last
       likewise, of its last row: its items aligned on their last baselines ending there, else the last of its items there */
    if (ni) {
        long r0 = -1, r1 = -1;
        size_t f0 = 0, f1 = 0;
        for (size_t q = 0; q < ni; q++) {
            lay_gp g = at(gp, q);
            if (g.s1 >= (long)nrows || g.s0 >= (long)ncols) continue;
            if (r0 < 0 || g.s1 < r0) r0 = g.s1;
            if (g.s1 + g.n1 - 1 > r1) r1 = g.s1 + g.n1 - 1;
        }
        int h0 = 0, h1 = 0;
        for (size_t q = 0; q < ni && r0 >= 0; q++) {
            lay_gp g = at(gp, q);
            if (g.s1 >= (long)nrows || g.s0 >= (long)ncols) continue;
            if (g.s1 == r0 && (!h0 || g.s0 < at(gp, f0).s0)) { f0 = q; h0 = 1; }
            if (g.s1 <= r1 && r1 < g.s1 + g.n1 && (!h1 || g.s1 > at(gp, f1).s1 || (g.s1 == at(gp, f1).s1 && g.s0 > at(gp, f1).s0))) { f1 = q; h1 = 1; }
        }
        double yb;
        if (h0) {
            uint32_t ib = at(its, f0).box;
            if (at(brise, (size_t)r0) >= 0) t->box[b].baseline = at(rows, (size_t)r0).pos + at(brise, (size_t)r0);
            else if (lay_first_baseline(t, ib, &yb, 0)) t->box[b].baseline = t->box[ib].y + yb;
            else t->box[b].baseline = t->box[ib].y - t->box[ib].pt - t->box[ib].bt + lay_border_height(t, ib);
            t->box[b].has_baseline = 1;
        }
        if (h1) {
            uint32_t ib = at(its, f1).box;
            t->box[b].has_lbaseline = 1;
            if (at(bfall, (size_t)r1) >= 0) {
                t->box[b].lbaseline = at(rows, (size_t)r1).pos + at(rows, (size_t)r1).base - at(bfall, (size_t)r1);
                t->box[b].has_lbaseline = 2;
            } else if (lay_last_baseline(t, ib, &yb, 0)) t->box[b].lbaseline = t->box[ib].y + yb;
            else t->box[b].lbaseline = t->box[ib].y - t->box[ib].pt - t->box[ib].bt + lay_border_height(t, ib);
        }
    }
}

/* ---- absolutely positioned boxes (CSS 2.1 10.3.7, 10.6.4), after the flow ---- */

/* the inline element whose fragments make the containing block of el (CSS 2.1, 10.1, 4):
   its nearest positioned ancestor, when that is an inline element; 0 when it is not */
static uint32_t lay_inline_cb(LCTX, uint32_t el) {
    uint32_t x = lay_parent_of(d, gx, el);
    for (int depth = 0; depth < 1024 && x > 1; depth++) {
        if (!(x & LAY_GEN) && d->node[x].kind != DOM_NODE_KIND_ELEMENT) return 0;
        if (LAY_X(x).pos != POS_STATIC) return lay_display_of(LARGS, x) == D_INLINE ? x : 0;
        x = lay_parent_of(d, gx, x);
    }
    return 0;
}
/* a box's content box's place in the viewport's, before the tree has page coordinates */
static void lay_box_at(const layout_tree *t, uint32_t b, uint32_t vp, double *x, double *y) {
    *x = 0;
    *y = 0;
    for (uint32_t q = b; q && q != vp; q = t->box[q].parent) { *x += t->box[q].x; *y += t->box[q].y; }
}

/* each element the flow met out of it, laid out in its containing block -- the nearest
   positioned ancestor's padding box, the viewport's for fixed or for none; when that
   ancestor is an inline element, the box from its first fragment's padding box's start
   and top to its last one's end and bottom (no wider than nothing when that end is before
   that start, as Chromium has it) -- by its offsets, or its static position where they
   are auto; its box that block's last child (an inline's: that of the box its first
   fragment's line is in). Those inside it are met as it is laid out, and laid out in turn */
static void lay_positioned(LCTX, uint32_t vp, double vw, double vh) {
    for (size_t q = 0; q < len(*ab); q++) {
        lay_abs a = at(*ab, q);
        uint32_t el = a.el;
        lay_style s;
        lay_style_of(LARGS, el, &s);
        uint32_t si = lay_si(sx, gx, el);
        int fixed = at(*xc, si).pos == POS_FIXED;
        uint32_t cb = vp, icb = 0, f0 = 0, f1 = 0;
        if (!fixed) {
            for (uint32_t x = a.box; x && x != vp; x = t->box[x].parent)
                if (t->box[x].position != LAYOUT_POSITION_STATIC) { cb = x; break; }
            icb = lay_inline_cb(LARGS, el);
            if (icb) {
                /* its first and last fragments */
                uint32_t want = lay_dom(gx, icb);
                for (uint32_t fr = 1; fr < t->frag_len; fr++)
                    if (t->frag[fr].live && t->frag[fr].kind == LAYOUT_FRAG_KIND_INLINE && t->frag[fr].node.index == want) { if (!f0) f0 = fr; f1 = fr; }
            }
        }
        /* the static position in cb's content box, and cb's padding box in it */
        double sx0 = a.x, sy0 = a.y, px0 = 0, py0 = 0, cbw = vw, cbh = vh;
        if (f0) {
            uint32_t l0 = t->frag[f0].line, l1 = t->frag[f1].line;
            uint32_t b0 = t->line[l0].box, b1 = t->line[l1].box;
            double x0, y0, x1, y1, ax, ay;
            lay_box_at(t, b0, vp, &x0, &y0);
            lay_box_at(t, b1, vp, &x1, &y1);
            lay_box_at(t, a.box, vp, &ax, &ay);
            lay_style is;
            lay_style_of(LARGS, icb, &is);
            double l = x0 + t->line[l0].x + t->frag[f0].x + is.b[3], tp = y0 + t->line[l0].y + t->frag[f0].y + is.b[0];
            double r = x1 + t->line[l1].x + t->frag[f1].x + t->frag[f1].w - is.b[1];
            double bm = y1 + t->line[l1].y + t->frag[f1].y + t->frag[f1].h - is.b[2];
            cb = b0;
            sx0 = ax + a.x - x0;
            sy0 = ay + a.y - y0;
            px0 = l - x0;
            py0 = tp - y0;
            cbw = r > l ? r - l : 0;
            cbh = bm > tp ? bm - tp : 0;
        } else {
            for (uint32_t x = a.box; x && x != cb && x != vp; x = t->box[x].parent) { sx0 += t->box[x].x; sy0 += t->box[x].y; }
            if (cb != vp) {
                /* its padding box in its own frame: a writing-mode root's record is its parent's frame's, turned back */
                uint32_t cpar = t->box[cb].parent;
                int of = lay_frame(t->box[cb].wm), pf0 = cpar ? lay_frame(t->box[cpar].wm) : of;
                double pd[4] = { t->box[cb].pt, t->box[cb].pr, t->box[cb].pb, t->box[cb].pl }, cwv = t->box[cb].w, chv = t->box[cb].h;
                if (of != pf0) {
                    double q[4];
                    for (int k = 0; k < 4; k++) q[k] = pd[lay_pside(pf0, lay_tside(of, k))];
                    for (int k = 0; k < 4; k++) pd[k] = q[k];
                    if ((of == 0) != (pf0 == 0)) { cwv = t->box[cb].h; chv = t->box[cb].w; }
                }
                px0 = -pd[3];
                py0 = -pd[0];
                cbw = cwv + pd[3] + pd[1];
                cbh = chv + pd[0] + pd[2];
            }
        }
        lay_len o0 = at(*xc, si).off[0], o1 = at(*xc, si).off[1], o2 = at(*xc, si).off[2], o3 = at(*xc, si).off[3];
        int cbf = lay_frame(t->box[cb].wm), elf = lay_frame(s.wm), turned = cbf != elf;
        if (turned) {
            /* a box of another frame than its containing block's: its edges, sizes and offsets read in that one (it is
               laid out in its own, lay_wm_root) */
            lay_style_turned(&s, elf, cbf);
            uint32_t ec0 = at(*xc, si).c0;
            o0 = lay_offp(st, ec0, lay_tside(cbf, 0));
            o1 = lay_offp(st, ec0, lay_tside(cbf, 1));
            o2 = lay_offp(st, ec0, lay_tside(cbf, 2));
            o3 = lay_offp(st, ec0, lay_tside(cbf, 3));
        }
        int la = o3.unit == LAY_AUTO, ra = o1.unit == LAY_AUTO, ta = o0.unit == LAY_AUTO, ba = o2.unit == LAY_AUTO;
        double L = la ? 0 : lay_res(st, o3, cbw), R = ra ? 0 : lay_res(st, o1, cbw);
        double T = ta ? 0 : lay_res(st, o0, cbh), B = ba ? 0 : lay_res(st, o2, cbh);
        double ml = s.m[3].unit == LAY_AUTO ? 0 : lay_res(st, s.m[3], cbw), mr = s.m[1].unit == LAY_AUTO ? 0 : lay_res(st, s.m[1], cbw);
        double eh = s.b[1] + s.b[3] + lay_res(st, s.p[1], cbw) + lay_res(st, s.p[3], cbw);
        double fw = -1;
        int wdecl = s.width.unit != LAY_AUTO && !lay_content_sized(s.width), csized = lay_content_sized(s.width);
        int repl = lay_replaced(d, el);   /* a replaced box keeps its own size (CSS 2.1, 10.3.8 and 10.6.5) */
        if (wdecl && !repl) fw = lay_res(st, s.width, cbw) + (s.border_box ? 0 : eh);
        else if (!la && !ra && !csized && !repl) { fw = cbw - L - R - ml - mr; if (fw < 0) fw = 0; }   /* both offsets: what they leave, none when it is less */
        /* a fit-content (min-, max-content) width shrinks to its content within what the offsets leave */
        if (la && ra) { L = sx0 - px0; la = 0; }
        lay_bk bk;
        bk.ox = 0;
        bk.oy = 0;
        bk.ch = cbh;
        bk.fw = fw >= 0 ? (fw > 0 ? fw : 0) : -1;
        bk.shrink = fw < 0;
        bk.item = 0;
        bk.qs = 0;
        double avail = cbw - (la ? 0 : L) - (ra ? 0 : R);
        lay_mc m1, m2;
        int empty;
        uint32_t bx = lay_block(LARGS, cb, el, fw >= 0 || repl ? cbw : avail, 0, 0, 0, 0, &bk, &m1, &m2, &empty);
        if (fw >= 0 && !wdecl) t->box[bx].how = LAYOUT_WIDTH_HOW_POSITIONED;
        else if (wdecl) t->box[bx].how = s.width.unit == LAY_PX ? LAYOUT_WIDTH_HOW_DECLARED : LAYOUT_WIDTH_HOW_PERCENT;
        t->box[bx].cb = cb == vp ? vp : cb;
        if (f0) {
            t->box[bx].cb_inline = 1;
            t->box[bx].cb_node = node_handle(d, lay_dom(gx, icb));
        }
        t->box[bx].avail = cbw;
        double bw = t->box[bx].w + eh;
        if (la) L = cbw - R - ml - mr - bw;
        if (!ra && (wdecl || csized || repl) && s.m[3].unit == LAY_AUTO && s.m[1].unit == LAY_AUTO) {
            /* auto margins with both offsets and the width (a declared one, a content-sized one): the box in the middle */
            double rest = cbw - L - R - bw;
            if (rest > 0) { ml = rest / 2; mr = rest / 2; }
        }
        t->box[bx].ml = ml;
        t->box[bx].mr = mr;
        /* its height: from both offsets when it is auto, else its content's */
        double mtv = t->box[bx].mt, mbv = t->box[bx].mb;
        double ve = t->box[bx].bt + t->box[bx].bb + t->box[bx].pt + t->box[bx].pb;
        if (s.height.unit == LAY_AUTO && at(*xc, si).hraw.unit == LAY_AUTO && !ta && !ba && !repl && !turned) {
            double h = cbh - T - B - mtv - mbv - ve;
            t->box[bx].h = h > 0 ? h : 0;
        }
        double bh = lay_border_height(t, bx);
        if (!ta && !ba && (repl || s.height.unit != LAY_AUTO) && s.m[0].unit == LAY_AUTO && s.m[2].unit == LAY_AUTO) {
            /* auto margins with both offsets and the height: the box in the middle */
            double rest = cbh - T - B - bh;
            if (rest > 0) { mtv = rest / 2; mbv = rest / 2; t->box[bx].mt = mtv; t->box[bx].mb = mbv; }
        }
        if (ta && ba) T = sy0 - py0;
        else if (ta) T = cbh - B - mtv - mbv - bh;
        lay_place(t, bx, px0 + L + ml, py0 + T + mtv);
    }
}

/* the image a background-image's first url() names (its cval c), fetched through the
   loader (once a URL: a resource fetched before is the one): its resource, 0 for none */
/* the image a background's text sp names (its first url()), resolved against the sheet of the declaration dcl (the
   page's when it has none): the loader's resource for it, or 0 with its URL in abs_url (empty: it names none) */
static uint32_t lay_image_url(const css_style *st, const net_loader *net, amb_span sp, uint32_t dcl, char *abs_url, size_t cap) {
    abs_url[0] = 0;
    vector<char> v;
    append_range(v, text_ptr(st, sp), sp.len);
    push_back(v, 0);
    const char *u = strstr(data(v), "url(");
    if (!u) return 0;
    size_t i = (size_t)(u - data(v)) + 4, n = len(v) - 1;
    while (i < n && (data(v)[i] == ' ' || data(v)[i] == '"' || data(v)[i] == '\'')) i++;
    size_t e = i;
    while (e < n && data(v)[e] != ')' && data(v)[e] != '"' && data(v)[e] != '\'') e++;
    if (e == i || e - i > 2000) return 0;
    char ref[2048];
    memcpy(ref, data(v) + i, e - i);
    ref[e - i] = 0;
    /* the base: the declaration's sheet's URL, or the page's; the encoding its query is written
       in, the sheet's (its own, or its referrer's), or the page's (measured: a background's url()
       as Chromium writes it, of a style element and of a fetched sheet) */
    char base[2048];
    base[0] = 0;
    int enc = net_encoding();
    if (dcl && dcl < st->decl_len) {
        uint32_t ru = st->decl[dcl].rule;
        uint32_t sh = ru && ru < st->rule_len ? st->rule[ru].sheet : 0;
        if (sh && sh < st->sheet_len && st->sheet[sh].url.len && st->sheet[sh].url.len < sizeof base) {
            amb_span su = st->sheet[sh].url;
            memcpy(base, text_ptr(st, su), su.len);
            base[su.len] = 0;
            enc = st->sheet[sh].enc;
        }
    }
    for (uint32_t r = 1; r < net->resource_len && !base[0]; r++)
        if (net->resource[r].live && net->resource[r].kind == NET_KIND_DOCUMENT) {
            amb_span fu = net->resource[r].final_url.len ? net->resource[r].final_url : net->resource[r].url;
            if (fu.len < sizeof base) { memcpy(base, text_ptr(net, fu), fu.len); base[fu.len] = 0; }
        }
    if (!base[0] || !net_url_resolve_enc(base, ref, enc, abs_url, cap)) { abs_url[0] = 0; return 0; }
    size_t al = strlen(abs_url);
    for (uint32_t r = 1; r < net->resource_len; r++)
        if (net->resource[r].live && net->resource[r].kind == NET_KIND_IMAGE && net->resource[r].url.len == al &&
            memcmp(text_ptr(net, net->resource[r].url), abs_url, al) == 0) return r;
    return 0;
}
/* the image a style's background (its value c) names: fetched once */
static uint32_t lay_fetch_image(const css_style *st, net_loader *net, uint32_t c) {
    char abs_url[4096];
    uint32_t r = lay_image_url(st, net, st->cval[c].text, st->cval[c].winner, abs_url, sizeof abs_url);
    if (r || !abs_url[0]) return r;
    net_resource_h h = net_fetch(net, abs_url, strlen(abs_url), NET_KIND_IMAGE);
    return h.index;
}
/* the image a style's background (its value c) names, as the loader has it: 0 when it was not fetched */
static uint32_t lay_image_known(const css_style *st, const net_loader *nt, uint32_t c) {
    char abs_url[4096];
    return lay_image_url(st, nt, st->cval[c].text, st->cval[c].winner, abs_url, sizeof abs_url);
}

/* ---- sticky boxes, at rest (CSS Position 3, 3.4) ---- */

/* a laid-out box moved by (dx, dy) in page coordinates: it, its lines and their fragments,
   and every box in it */
static void lay_move(layout_tree *t, uint32_t b, double dx, double dy, int depth) {
    t->box[b].x += dx;
    t->box[b].y += dy;
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) {
        t->line[ln].x += dx;
        t->line[ln].y += dy;
        t->line[ln].baseline += dy;
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            t->frag[fr].x += dx;
            t->frag[fr].y += dy;
            t->frag[fr].baseline += dy;
        }
    }
    if (depth < 1024) for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) lay_move(t, c, dx, dy, depth + 1);
}

/* the scrollport of what a sticky box's containing block cb holds: a scroll container's padding box (not the root's,
   nor the body's when the root's overflow is visible: theirs is the viewport's), else the viewport; into sp (x, y, w, h) */
static void lay_scrollport(LCTX, uint32_t cb, uint32_t vp, double vw, double vh, double *sp) {
    uint32_t root = t->box[vp].first_child;
    sp[0] = 0;
    sp[1] = 0;
    sp[2] = vw;
    sp[3] = vh;
    for (uint32_t a = cb; a && a != vp && a != root; a = t->box[a].parent) {
        if (t->box[a].clip != 1) continue;
        if (t->box[a].parent == root && t->box[root].clip != 1 && lay_is_named(d, t->box[a].node.index, "body")) continue;
        sp[0] = t->box[a].x - t->box[a].pl;
        sp[1] = t->box[a].y - t->box[a].pt;
        sp[2] = t->box[a].w + t->box[a].pl + t->box[a].pr;
        sp[3] = t->box[a].h + t->box[a].pt + t->box[a].pb;
        break;
    }
}
/* how far a sticky element el moves at rest (CSS Position 3, 3.4): bb its border box (x0, y0, x1, y1), cr its containing
   block's content box, m its margins (top, right, bottom, left), sp its scrollport. Where an inset it has puts that edge
   of its sticky view rectangle (the scrollport inset by it) past its border box's, it moves inward, as far as its margin
   box stays in cr: bottom and right first, so that top and left win */
static void lay_sticky_shift(LCTX, uint32_t el, const double *bb, const double *cr, const double *m, const double *sp, double *dxp, double *dyp) {
    lay_ext e = LAY_X(el);
    for (int k = 0; k < 4; k++) e.off[k] = lay_offp(st, e.c0, k);
    double dx = 0, dy = 0, lim, dd, room;
    if (e.off[2].unit != LAY_AUTO) {
        lim = sp[1] + sp[3] - lay_res(st, e.off[2], sp[3]);
        dd = bb[3] - lim;
        room = bb[1] - m[0] - cr[1];
        if (dd > room) dd = room;
        if (dd > 0) dy -= dd;
    }
    if (e.off[0].unit != LAY_AUTO) {
        lim = sp[1] + lay_res(st, e.off[0], sp[3]);
        dd = lim - (bb[1] + dy);
        room = cr[3] - (bb[3] + dy + m[2]);
        if (dd > room) dd = room;
        if (dd > 0) dy += dd;
    }
    if (e.off[1].unit != LAY_AUTO) {
        lim = sp[0] + sp[2] - lay_res(st, e.off[1], sp[2]);
        dd = bb[2] - lim;
        room = bb[0] - m[3] - cr[0];
        if (dd > room) dd = room;
        if (dd > 0) dx -= dd;
    }
    if (e.off[3].unit != LAY_AUTO) {
        lim = sp[0] + lay_res(st, e.off[3], sp[2]);
        dd = lim - (bb[0] + dx);
        room = cr[2] - (bb[2] + dx + m[1]);
        if (dd > room) dd = room;
        if (dd > 0) dx += dd;
    }
    *dxp = dx;
    *dyp = dy;
}

/* node x is el or in el's subtree (a pseudo-element's text: its element's) */
static int lay_in_subtree(const dom_document *d, uint32_t x, uint32_t el) {
    for (int depth = 0; x && depth < 4096; depth++) {
        if (x == el) return 1;
        uint32_t up = dom_flat_parent(d, x);
        x = up ? up : d->node[x].parent;
    }
    return 0;
}

/* each sticky box where it is at rest, unscrolled (lay_sticky_shift; its containing block its parent's content box), an
   outer box before those in it; then each sticky inline element, as Chromium places one: its box the union of its
   fragments' border boxes, its containing block the content box of the block whose lines hold them (not an anonymous
   one), its fragments, its text's and its atomic boxes moved */
static void lay_sticky(LCTX, uint32_t vp, double vw, double vh) {
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live || t->box[b].position != LAYOUT_POSITION_STICKY) continue;
        uint32_t cb = t->box[b].parent, el = t->box[b].node.index;
        if (!cb || cb == vp || !el) continue;
        if (t->box[b].pseudo) {
            int64_t g = lay_gen_find(gx, el, t->box[b].pseudo);
            if (g < 0) continue;
            el = LAY_GEN | (uint32_t)g;
        }
        double sp[4], bb[4], cr[4], m[4], dx, dy;
        lay_scrollport(LARGS, cb, vp, vw, vh, sp);
        bb[0] = t->box[b].x - t->box[b].pl - t->box[b].bl;
        bb[1] = t->box[b].y - t->box[b].pt - t->box[b].bt;
        bb[2] = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br;
        bb[3] = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb;
        cr[0] = t->box[cb].x;
        cr[1] = t->box[cb].y;
        cr[2] = cr[0] + t->box[cb].w;
        cr[3] = cr[1] + t->box[cb].h;
        /* its margins as its style has them (auto none: not those an over-constrained width gives) */
        lay_style ss;
        lay_style_of(LARGS, el, &ss);
        for (int k = 0; k < 4; k++) m[k] = ss.m[k].unit == LAY_AUTO ? 0 : lay_res(st, ss.m[k], t->box[cb].w);
        lay_sticky_shift(LARGS, el, bb, cr, m, sp, &dx, &dy);
        if (dx == 0 && dy == 0) continue;
        lay_move(t, b, dx, dy, 0);
        t->box[b].dx += dx;
        t->box[b].dy += dy;
    }
    vector<uint32_t> seen;
    for (uint32_t fr = 1; fr < t->frag_len; fr++) {
        if (!t->frag[fr].live || t->frag[fr].kind != LAYOUT_FRAG_KIND_INLINE) continue;
        uint32_t el = t->frag[fr].node.index;
        if (!el || el >= d->node_len || d->node[el].kind != DOM_NODE_KIND_ELEMENT || LAY_X(el).pos != POS_STICKY) continue;
        int again = 0;
        for (size_t k = 0; k < len(seen) && !again; k++) again = at(seen, k) == el;
        if (again) continue;
        push_back(seen, el);
        /* its box: its fragments' union; its containing block: the block whose line holds its first, an anonymous one's
           parent */
        double bb[4] = { 1e30, 1e30, -1e30, -1e30 }, sp[4], cr[4], m[4], dx, dy;
        uint32_t cb = t->line[t->frag[fr].line].box;
        for (uint32_t f2 = fr; f2 < t->frag_len; f2++) {
            if (!t->frag[f2].live || t->frag[f2].kind != LAYOUT_FRAG_KIND_INLINE || t->frag[f2].node.index != el) continue;
            if (t->frag[f2].x < bb[0]) bb[0] = t->frag[f2].x;
            if (t->frag[f2].y < bb[1]) bb[1] = t->frag[f2].y;
            if (t->frag[f2].x + t->frag[f2].w > bb[2]) bb[2] = t->frag[f2].x + t->frag[f2].w;
            if (t->frag[f2].y + t->frag[f2].h > bb[3]) bb[3] = t->frag[f2].y + t->frag[f2].h;
        }
        while (cb && t->box[cb].kind == LAYOUT_BOX_KIND_ANONYMOUS && t->box[cb].parent) cb = t->box[cb].parent;
        if (!cb || cb == vp) continue;
        lay_scrollport(LARGS, cb, vp, vw, vh, sp);
        cr[0] = t->box[cb].x;
        cr[1] = t->box[cb].y;
        cr[2] = cr[0] + t->box[cb].w;
        cr[3] = cr[1] + t->box[cb].h;
        /* an inline element's horizontal margins (its vertical ones take no room in a line) */
        lay_style ss;
        lay_style_of(LARGS, el, &ss);
        m[0] = 0;
        m[2] = 0;
        m[1] = ss.m[1].unit == LAY_AUTO ? 0 : lay_res(st, ss.m[1], t->box[cb].w);
        m[3] = ss.m[3].unit == LAY_AUTO ? 0 : lay_res(st, ss.m[3], t->box[cb].w);
        lay_sticky_shift(LARGS, el, bb, cr, m, sp, &dx, &dy);
        if (dx == 0 && dy == 0) continue;
        for (uint32_t f2 = 1; f2 < t->frag_len; f2++) {
            if (!t->frag[f2].live || t->frag[f2].kind == LAYOUT_FRAG_KIND_MARKER) continue;
            uint32_t x = t->frag[f2].node.index;
            if (!x || x >= d->node_len || !lay_in_subtree(d, x, el)) continue;
            t->frag[f2].x += dx;
            t->frag[f2].y += dy;
            t->frag[f2].baseline += dy;
            if (t->frag[f2].kind == LAYOUT_FRAG_KIND_ATOMIC && t->frag[f2].atomic) lay_move(t, t->frag[f2].atomic, dx, dy, 0);
        }
    }
}

/* ---- page coordinates ---- */

/* A box's record is in its parent's frame (lay_frame), relative to the parent's content box; its lines and children in
   its own. Each is made physical against the content box it is in (W x H in its frame: W the inline size), then
   placed at that box's corner (ox, oy): a frame's rect (x, y, w, h) is vertical-rl's (1) at H - y - h across and x
   down, vertical-lr's (2) at y and x, sideways-lr's (3) at y and W - x - w; a vertical line's baseline is an x */
static void lay_rect_out(int f, double W, double H, double *x, double *y, double *w, double *h) {
    double x0 = *x, y0 = *y, w0 = *w, h0 = *h;
    if (f == 1) { *x = H - y0 - h0; *y = x0; *w = h0; *h = w0; }
    else if (f == 2) { *x = y0; *y = x0; *w = h0; *h = w0; }
    else if (f == 3) { *x = y0; *y = W - x0 - w0; *w = h0; *h = w0; }
}
static double lay_base_out(int f, double H, double y) { return f == 1 ? H - y : y; }
/* a line of a vertical-lr box: its over side is its right, its block-start's other side (CSS Writing Modes 4, 6.2):
   what it holds mirrored in it, its fragments, their baselines and its atomic boxes */
static void lay_line_mirror(layout_tree *t, uint32_t ln) {
    double lh = t->line[ln].h, ly = t->line[ln].y;
    t->line[ln].baseline = lh - t->line[ln].baseline;
    for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
        t->frag[fr].y = lh - t->frag[fr].y - t->frag[fr].h;
        t->frag[fr].baseline = lh - t->frag[fr].baseline;
        uint32_t a = t->frag[fr].atomic;
        if (t->frag[fr].kind != LAYOUT_FRAG_KIND_ATOMIC || !a || a >= t->box_len) continue;
        double top = t->box[a].y - t->box[a].pt - t->box[a].bt - t->box[a].mt;
        double bot = t->box[a].y + t->box[a].h + t->box[a].pb + t->box[a].bb + t->box[a].mb;
        t->box[a].y += (2 * ly + lh - bot) - top;
    }
}
static void lay_absolute(layout_tree *t, uint32_t b, double ox, double oy, int pf, double pw, double ph) {
    if (pf) {
        lay_rect_out(pf, ph, pw, &t->box[b].x, &t->box[b].y, &t->box[b].w, &t->box[b].h);
        lay_turn_sides(t, b, pf, 0);
        double dx, dy;
        lay_vec_out(pf, t->box[b].dx, t->box[b].dy, &dx, &dy);
        t->box[b].dx = dx;
        t->box[b].dy = dy;
    }
    t->box[b].x += ox;
    t->box[b].y += oy;
    double cx = t->box[b].x, cy = t->box[b].y;
    int bf = lay_frame(t->box[b].wm);
    /* its content box in its own frame: the viewport's block size the viewport's width, however far its content
       reaches (a vertical-rl page's overflow goes left, where it is not scrolled to) */
    double W = bf ? t->box[b].h : t->box[b].w, H = bf ? t->box[b].w : t->box[b].h;
    for (uint32_t ln = t->box[b].first_line; ln; ln = t->line[ln].next) {
        if (!bf) {
            t->line[ln].x += cx;
            t->line[ln].y += cy;
            double lx = t->line[ln].x, ly = t->line[ln].y;
            t->line[ln].baseline += ly;
            for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
                t->frag[fr].x += lx;
                t->frag[fr].y += ly;
                t->frag[fr].baseline += ly;
            }
            continue;
        }
        if (bf == 2) lay_line_mirror(t, ln);
        double lx = t->line[ln].x, ly = t->line[ln].y;
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            double fx = lx + t->frag[fr].x, fy = ly + t->frag[fr].y, fw = t->frag[fr].w, fh = t->frag[fr].h;
            lay_rect_out(bf, W, H, &fx, &fy, &fw, &fh);
            t->frag[fr].x = cx + fx;
            t->frag[fr].y = cy + fy;
            t->frag[fr].w = fw;
            t->frag[fr].h = fh;
            t->frag[fr].baseline = cx + lay_base_out(bf, H, ly + t->frag[fr].baseline);
        }
        t->line[ln].baseline = cx + lay_base_out(bf, H, ly + t->line[ln].baseline);
        lay_rect_out(bf, W, H, &t->line[ln].x, &t->line[ln].y, &t->line[ln].w, &t->line[ln].h);
        t->line[ln].x += cx;
        t->line[ln].y += cy;
    }
    double cw = t->box[b].w, ch = t->box[b].h;
    if (bf && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) { cw = lay_icb_w; ch = lay_icb_h; }
    for (uint32_t c = t->box[b].first_child; c; c = t->box[c].next) lay_absolute(t, c, cx, cy, bf, cw, ch);
}

/* each vertical line's text fragments' snap (vref): paint sets a fragment's glyphs from the start of the decorated
   inline box it is in (an inline fragment of its line, of an element its text is in: one that draws itself, Blink's
   inline box fragment that is not culled), else from its line's first text fragment's, that start moved to the whole
   pixel nearest it (measured in Chromium) */
static void lay_vshifts(layout_tree *t, const dom_document *d) {
    for (uint32_t ln = 1; ln < t->line_len; ln++) {
        if (!t->line[ln].live) continue;
        uint32_t b = t->line[ln].box;
        if (!b || b >= t->box_len || !lay_frame(t->box[b].wm)) continue;
        uint32_t first = 0;
        for (uint32_t fr = t->line[ln].first_frag; fr && !first; fr = t->frag[fr].next)
            if ((t->frag[fr].kind == LAYOUT_FRAG_KIND_TEXT || t->frag[fr].kind == LAYOUT_FRAG_KIND_GENERATED) && t->frag[fr].orient != 4) first = fr;
        if (!first) continue;
        uint32_t holder = t->box[b].node.index;
        for (uint32_t fr = t->line[ln].first_frag; fr; fr = t->frag[fr].next) {
            /* (a combined text is set in its own box, an atomic inline's in Blink) */
            if ((t->frag[fr].kind != LAYOUT_FRAG_KIND_TEXT && t->frag[fr].kind != LAYOUT_FRAG_KIND_GENERATED) || t->frag[fr].orient == 4) continue;
            uint32_t ref = first;
            uint32_t x = t->frag[fr].node.index;
            int found = 0, depth = 0;
            uint32_t e = 0;
            if (x && x < d->node_len) e = t->frag[fr].kind == LAYOUT_FRAG_KIND_GENERATED ? x : d->node[x].parent;
            while (e && e < d->node_len && e != holder && depth < 64 && !found) {
                for (uint32_t f2 = t->line[ln].first_frag; f2 && !found; f2 = t->frag[f2].next) {
                    if (t->frag[f2].kind != LAYOUT_FRAG_KIND_INLINE || t->frag[f2].node.index != e) continue;
                    ref = f2;
                    found = 1;
                }
                e = d->node[e].parent;
                depth++;
            }
            t->frag[fr].vref = ref;
        }
    }
}

layout_status layout_run(layout_tree *t, dom_document *doc, css_style *style, net_loader *net, double width, double height) {
    /* the flat tree's links, which every walk of the DOM here takes (src/dom/dom.h) */
    dom_flatten(doc);
    amb_fonts fonts = {0};
    defer(fonts) { AMB_FONTS_DROP(fonts); }
    amb_fonts_load(&fonts);
    lay_rb_run = 0;                   /* no run shaped yet in these fonts */
    lay_ar_clear();
    /* each node's style, and each style's first value; then each style as layout reads it */
    vector<uint32_t> node_style;
    resize(node_style, doc->node_len);
    vector<uint32_t> style_first;
    resize(style_first, 1);
    for (uint32_t e = 1; e < style->estyle_len; e++) {
        uint32_t x = style->estyle[e].node.index, sid = style->estyle[e].style;
        if (x < doc->node_len && !at(node_style, x)) node_style[x] = sid;
        if (sid >= len(style_first)) resize(style_first, (size_t)sid + 1);
        style_first[sid] = style->estyle[e].first;
    }
    /* the pseudo-elements, each element's together (by element, then pseudo-element), and
       their styles; whether an element has any, in its node_style */
    vector<lay_gen> gens;
    if (style->pstyle_len > 1) {
        vector<uint32_t> head;
        resize(head, doc->node_len);
        for (uint32_t q = style->pstyle_len - 1; q >= 1; q--) {
            uint32_t x = style->pstyle[q].node.index;
            if (style->pstyle[q].live && x < doc->node_len) head[x] = q;
        }
        for (uint32_t x = 1; x < doc->node_len; x++) {
            for (uint32_t q = at(head, x); q && q < style->pstyle_len && style->pstyle[q].node.index == x; q++) {
                lay_gen g;
                memset(&g, 0, sizeof g);
                g.el = x;
                g.pseudo = (uint32_t)style->pstyle[q].pseudo;
                g.style = style->pstyle[q].style;
                push_back(gens, g);
                if (g.style >= len(style_first)) resize(style_first, (size_t)g.style + 1);
                /* a ::before or an ::after not generated (its content none): kept, for a query container's restyle */
                if (!style->pstyle[q].first) continue;
                if (g.pseudo == CSS_PSEUDO_BEFORE) node_style[x] |= LAY_HAS_BEFORE;
                else if (g.pseudo == CSS_PSEUDO_AFTER) node_style[x] |= LAY_HAS_AFTER;
                else node_style[x] |= LAY_HAS_OTHER;
                style_first[g.style] = style->pstyle[q].first;
            }
        }
    }
    /* the web fonts @font-face gave, each read once: each face record's face in fonts */
    vector<int32_t> webface;
    resize(webface, style->face_len > 0 ? style->face_len : 1);
    for (uint32_t fi = 1; fi < style->face_len; fi++) {
        webface[fi] = -1;
        uint32_t r = style->face[fi].font.index;
        if (!net || !r || r >= net->resource_len) continue;
        for (uint32_t q = 1; q < fi && at(webface, fi) < 0; q++)
            if (style->face[q].font.index == r) webface[fi] = at(webface, q);
        if (at(webface, fi) >= 0) continue;
        vector<uint8_t> bytes;
        amb_span body = net->resource[r].body;
        append_range(bytes, (const uint8_t *)bytes_ptr(net, body), body.len);
        webface[fi] = amb_fonts_add(&fonts, data(bytes), len(bytes));
    }
    vector<lay_style> styles;
    vector<lay_ext> exts;
    resize(styles, len(style_first));
    resize(exts, len(style_first));
    for (size_t k = 0; k < len(style_first); k++) {
        lay_style ls;
        lay_style_make(style, &fonts, &webface, at(style_first, k), -1, &ls);
        styles[k] = ls;
        lay_ext le;
        lay_ext_make(style, at(style_first, k), -1, &le);
        exts[k] = le;
    }
    vector<lay_abs> abs;
    vector<lay_flt> flts;
    lay_icb_w = width;
    lay_icb_h = height;
    lay_wm_own = 0;
    clear(lay_wm_boxes);
    dom_document_mode mode = doc->node_len > 1 ? doc->node[1].mode : DOM_DOCUMENT_MODE_NO_QUIRKS;
    lay_quirks = mode == DOM_DOCUMENT_MODE_QUIRKS;
    lay_lhq = mode != DOM_DOCUMENT_MODE_NO_QUIRKS;
    /* the images the styles' backgrounds name: each url() (the first of its layers)
       resolved against the sheet its declaration is in (the page for one of its own) and
       fetched once */
    for (size_t k = 0; k < len(exts) && net; k++) {
        uint32_t c = at(exts, k).bgimg;
        if (!c) continue;
        uint32_t res = lay_fetch_image(style, net, c);
        exts[k].bgres = res;
    }
    /* a query container met: what its restyles need -- the web faces, and the images the rules of @container name,
       fetched now, for styles the restyles make (lay_cq_style finds them) */
    int has_cq = 0;
    for (size_t k = 0; k < len(exts) && !has_cq; k++) if (at(exts, k).cq) has_cq = 1;
    if (has_cq) {
        for (size_t k = 0; k < len(webface); k++) push_back(lay_cq_wf, at(webface, k));
        for (uint32_t r = 1; r < style->rule_len && net; r++) {
            if (!style->rule[r].container) continue;
            for (uint32_t dc = style->rule[r].first_decl; dc; dc = style->decl[dc].next) {
                if (style->decl[dc].prop != CSS_PROP_BACKGROUND_IMAGE) continue;
                char abs_url[4096];
                if (!lay_image_url(style, net, style->decl[dc].text, dc, abs_url, sizeof abs_url) && abs_url[0])
                    net_fetch(net, abs_url, strlen(abs_url), NET_KIND_IMAGE);
            }
        }
    }
    /* the text of each ::before and ::after, in tree order */
    if (len(gens) && doc->node_len > 1) {
        vector<lay_ctr> ctr;
        int depth = 0;
        lay_gen_walk(t, doc, style, &node_style, &exts, &styles, &gens, 1, &ctr, &depth, 0, 0);
    }
    /* the root element: the document's element child */
    uint32_t root = 0;
    for (uint32_t c = doc->node_len > 1 ? doc->node[1].first_child : 0; c; c = doc->node[c].next_sibling)
        if (doc->node[c].kind == DOM_NODE_KIND_ELEMENT) root = c;
    /* the root's writing mode, which the initial containing block takes: an html root's body's, when it has a body
       child (CSS Writing Modes 4, 8: the principal writing mode; Chromium's, measured), its own style made again in it */
    int rwm = 0;
    if (root) {
        uint32_t rs = at(node_style, root) & LAY_SMASK, wsrc = root;
        if (doc->node[root].ns == DOM_NAMESPACE_HTML && lay_is_named(doc, root, "html"))
            for (uint32_t c = doc->node[root].first_child; c; c = doc->node[c].next_sibling)
                if (doc->node[c].kind == DOM_NODE_KIND_ELEMENT && lay_is_named(doc, c, "body")) { wsrc = c; break; }
        uint32_t ws = at(node_style, wsrc) & LAY_SMASK;
        rwm = ws < len(styles) ? at(styles, ws).wm : 0;
        if (rs < len(styles) && rwm != at(styles, rs).wm) {
            lay_style ls;
            lay_style_make(style, &fonts, &webface, at(style_first, rs), rwm, &ls);
            styles[rs] = ls;
            lay_ext le;
            lay_ext_make(style, at(style_first, rs), rwm, &le);
            le.bgres = at(exts, rs).bgres;
            exts[rs] = le;
        }
    }
    int rf = lay_frame(rwm), has_vertical = 0;
    for (size_t k = 0; k < len(styles) && !has_vertical; k++) if (at(styles, k).wm) has_vertical = 1;
    uint32_t vp = lay_new_box(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, 0, 0, LAYOUT_BOX_KIND_VIEWPORT);
    t->box[vp].wm = (uint8_t)rwm;
    t->box[vp].w = width;
    t->box[vp].h = height;
    t->box[vp].how = LAYOUT_WIDTH_HOW_VIEWPORT;
    t->box[vp].avail = width;
    /* the canvas: the Canvas of the scheme the root is used in (CSS Color Adjust 1, the
       cascade's color-scheme value's colour), white without a root */
    uint32_t rsid = root ? at(node_style, root) & LAY_SMASK : 0;
    uint32_t rc0 = rsid && rsid < len(style_first) ? at(style_first, rsid) : 0;
    uint32_t canvas = rc0 ? style->cval[rc0 + CSS_PROP_COLOR_SCHEME].color : 0;
    t->box[vp].background = (canvas & 0xff) == 0xff ? canvas : 0xffffffff;
    if (!root) { lay_cq_free(); t->revision++; return LAYOUT_NO_ROOT; }
    if (lay_display_of(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, root) != D_NONE) {
        lay_mc mt, mb;
        int empty;
        /* the root: its containing block the initial one, the viewport's size */
        lay_bk bk;
        bk.ox = 0;
        bk.oy = 0;
        bk.ch = rf ? width : height;      /* the initial containing block in the root's frame: a vertical one turned */
        bk.fw = -1;
        bk.shrink = 0;
        bk.item = 0;
        bk.qs = 0;
        uint32_t rb = lay_block(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, vp, root, rf ? height : width,
                                0, 0, 0, 0, &bk, &mt, &mb, &empty);
        lay_place(t, rb, t->box[rb].ml, t->box[rb].mt);
        double bottom = t->box[rb].mt + lay_border_height(t, rb) + t->box[rb].mb;
        /* the floats the root holds */
        for (size_t k = 0; k < len(flts); k++) if (at(flts, k).y + at(flts, k).h > bottom) bottom = at(flts, k).y + at(flts, k).h;
        lay_positioned(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, vp, rf ? height : width, rf ? width : height);
        if (!rf && bottom > height) t->box[vp].h = bottom;
    }
    lay_absolute(t, vp, 0, 0, 0, 0, 0);
    /* the sticky boxes and inline elements, when a style is sticky: before the inline elements' strips are made from
       their fragments (and before their clips, which the first fragment places) */
    for (size_t k = 0; k < len(exts); k++)
        if (at(exts, k).pos == POS_STICKY) { lay_sticky(t, doc, style, net, &fonts, &node_style, &styles, &gens, &exts, &abs, &flts, vp, width, height); break; }
    lay_inline_strips(t);
    lay_inline_clips(t);
    lay_radii(t);
    if (lay_frame(t->box[vp].wm) || has_vertical) lay_vshifts(t, doc);
    /* each svg element's drawing, made for its content box, and each box's SVG effects, for its border box (src/paint/svg.h):
       those a filter, a clip-path or a mask names by url() */
    vector<uint32_t> firsts;
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live || t->box[b].pseudo || t->box[b].kind == LAYOUT_BOX_KIND_INLINE || t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) continue;
        uint32_t el = t->box[b].node.index;
        if (!el || el >= doc->node_len) continue;
        int svg = t->box[b].kind == LAYOUT_BOX_KIND_REPLACED && lay_is_svg(doc, el);
        uint32_t sid = at(node_style, el) & LAY_SMASK, c0 = sid < len(style_first) ? at(style_first, sid) : 0;
        int fx = c0 && (lay_has_url(style, c0 + CSS_PROP_FILTER) || lay_has_url(style, c0 + CSS_PROP_CLIP_PATH) ||
                        style->cval[c0 + CSS_PROP_MASK_IMAGE].unit == CSS_UNIT_TEXT);
        if (!svg && !fx) continue;
        if (!len(firsts)) {
            resize(firsts, doc->node_len);
            for (uint32_t x = 1; x < doc->node_len; x++) {
                uint32_t xs = at(node_style, x) & LAY_SMASK;
                firsts[x] = xs < len(style_first) ? at(style_first, xs) : 0;
            }
        }
        if (svg) {
            vector<char> scene;
            amb_svg_scene(doc, style, &firsts, &fonts, el, t->box[b].w, t->box[b].h, &scene);
            if (len(scene)) {
                amb_span sp = text_add(t, data(scene), len(scene));
                t->box[b].svg = sp;
            }
        }
        if (fx) {
            vector<char> effects;
            amb_svg_effects(doc, style, &firsts, &fonts, el, t->box[b].w + t->box[b].pl + t->box[b].pr + t->box[b].bl + t->box[b].br,
                            t->box[b].h + t->box[b].pt + t->box[b].pb + t->box[b].bt + t->box[b].bb, &effects);
            if (len(effects)) {
                amb_span sp = text_add(t, data(effects), len(effects));
                t->box[b].fx = sp;
            }
        }
    }
    lay_cq_free();
    t->revision++;
    return LAYOUT_OK;
}
