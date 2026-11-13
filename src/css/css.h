/* src/css/css.h -- what the CSS module (src/css/css.c) offers other units beside its
   contract's operations: the number of properties a computed style holds, and the
   readers of the values a computed style keeps as text, so that layout and paint
   read CSS's syntax through the module that parses it. A unit that includes this has
   said amb_interface(css) first. */
#ifndef AMB_CSS_H
#define AMB_CSS_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the properties of a computed style: every value of enum(prop) before custom (the flow-relative
   ones after it are declarations' alone) */
#define CSS_NPROPS 185

/* a writing mode, as layout and the cascade number writing-mode's values */
enum { CSS_WM_HTB, CSS_WM_VRL, CSS_WM_VLR, CSS_WM_SRL, CSS_WM_SLR };
/* the physical property a flow-relative one (margin-inline-start, block-size, ...; CSS Logical 1) is
   for an element of writing mode wm (CSS_WM_*) and direction rtl (1: right to left); any other
   property itself */
int css_logical_physical(int prop, int wm, int rtl);

/* a calc(), min(), max() or clamp() whose lengths are px, its percentages taken of
   basis: 1 and the px in *out, or 0 when it is none */
int css_calc_eval(const char *s, size_t n, double basis, double *out);

/* a colour as CSS writes it (a name, #hex, rgb(), rgba(), hsl(), hsla(), transparent),
   RGBA with alpha in the low byte: 1, or 0 when it is none */
int css_color_parse(const char *s, size_t n, uint32_t *rgba);

/* a length as a computed value writes it (px, a percentage of basis, a calc()): 1 and
   the px in *out, or 0 when it is none */
int css_length_eval(const char *s, size_t n, double basis, double *out);

/* the part of s from *i to the next top-level white space or comma, its start and
   length; 0 at the end of s. Parentheses and quotes are kept whole */
int css_next_part(const char *s, size_t n, size_t *i, size_t *start, size_t *len);

/* the reader's settings each cascade from now on reads: the viewport in CSS px (what
   media queries and vw, vh read), the default font size in px (medium, the root's, rem's),
   prefers-color-scheme dark (1) and prefers-reduced-motion reduce (1). Unset: 800 x 600,
   16 px, light, no preference */
void css_set_env(double view_w, double view_h, double font_size, int dark, int reduced_motion);
/* whether the page's scripts run (the scripting media feature: enabled, or none); on unless set */
void css_set_scripting(int on);

/* a script's selector list against the element x (src/script): 1 when x matches */
int css_selector_matches(const dom_document *d, uint32_t x, const char *s, size_t n);

/* ---- container queries (CSS Conditional 5): what layout tells the cascade ----
   The cascade answers a query that asks a container whose size it does not know as
   false, and measures container units against the small viewport. Layout, at each query
   container (container-type size or inline-size, its containment in force) whose content
   box it has sized, before it lays out what is in it, hands the cascade that size: the
   cascade then makes the container's descendants' and its pseudo-elements' styles again,
   their queries and units answered for it, and says which changed. A change: an element
   (pseudo 0) or one of its pseudo-elements (its css_pseudo), its style now and that
   style's first value (0: a ::before or an ::after no longer generated) */
typedef struct css_restyled { uint32_t node, pseudo, style, first; } css_restyled;

/* the container x of d, laid out, its content box w x h px (h < 0: its block size is its
   content's, an inline-size container): its subtree restyled, each change appended to out.
   0 when nothing could change: the cascade that ran last was not of this store and
   document, or x is no query container, or its subtree was made for this size already */
int css_container_restyle(css_style *s, const dom_document *d, uint32_t x, double w, double h, vector<css_restyled> *out);

/* ---- counter styles (CSS Counter Styles 3) ---- */

/* the value v as the counter style named name writes it (the page's @counter-style rules,
   then the predefined styles; decimal for a name none defines), appended to out: as a
   marker (marker 1: its prefix and suffix about it) or as counter() writes it. Returns,
   for a style that is one of the predefined symbol markers Chromium draws as a shape or
   extends one, its shape: 1 disc, 2 circle, 3 square, 9 disclosure-closed, 10
   disclosure-open; else 0 */
int css_counter_text(const css_style *s, const char *name, size_t nn, long v, int marker, vector<char> *out);

#endif
