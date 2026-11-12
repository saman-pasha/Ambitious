/* src/css/css.h -- what the CSS module (src/css/css.c) offers other units beside its
   contract's operations: the number of properties a computed style holds, and the
   readers of the values a computed style keeps as text, so that layout and paint
   read CSS's syntax through the module that parses it. A unit that includes this has
   said amb_interface(css) first. */
#ifndef AMB_CSS_H
#define AMB_CSS_H
#include <stdint.h>
#include <stddef.h>

/* the properties of a computed style: every value of enum(prop) but custom */
#define CSS_NPROPS 106

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

/* a script's selector list against the element x (src/script): 1 when x matches */
int css_selector_matches(const dom_document *d, uint32_t x, const char *s, size_t n);

#endif
