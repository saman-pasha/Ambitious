/* src/paint/svg.h -- SVG drawn (src/paint/svg.c): an inline svg element's content, and an
   SVG document an img or a background names, each as Chromium draws it.

   An svg element's subtree is made a SCENE by layout (amb_svg_scene), once its box's size
   is known: text, each line a command -- a path with its paints and stroke, a run of
   glyphs, an image element, a group (its opacity, its clip's shapes, its mask's and its
   filter's blocks) and its end, a pattern's block (its tile's own lines) -- every
   coordinate in its user space and the matrix that maps it into the viewport's CSS px.
   Shapes and paths become absolute moves, lines and cubic Beziers (arcs and quadratics
   converted); use elements and markers are expanded (a use's tree inheriting from the use,
   a marker's content from the marker); gradients, patterns, clips, masks and filters are
   resolved into user space (objectBoundingBox through the element's box). The cascade
   (src/css) gives every value: presentation attributes, style attributes and the page's
   sheets. Paint draws the scene in the box (amb_svg_draw_with, the loader giving image
   elements' pictures): its curves flattened at the device's px, strokes made outlines
   (joins, caps, dashes; a thin one Skia's hairline), each shape's coverage found over
   sixteen rows a pixel with exact horizontal spans (non-zero or even-odd), groups with an
   opacity, a clip, a mask or a filter drawn in a layer of their own.

   An SVG document's bytes (an image's) are read by a small XML parser into a document of
   their own, cascaded, made a scene for their natural size and drawn scaled to the box
   (amb_svg_image, amb_svg_raster).

   A unit that includes this has said amb_interface(dom), amb_interface(css),
   amb_interface(layout) and amb_interface(net) first. */
#ifndef AMB_SVG_H
#define AMB_SVG_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>
#include "font.h"
#include "paint.h"

/* the scene of d's svg element el, its subtree styled by st (firsts: each node's style's
   first value in st, 0 for one without), in a viewport of vw x vh CSS px; f measures its
   text. Appended to out; nothing when nothing is drawn */
void amb_svg_scene(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t el, double vw,
                   double vh, vector<char> *out);

/* the SVG effects d's element el (an HTML element's box, or an outermost svg's) names by url() in its computed values (st,
   firsts as for a scene; f measures a mask's text), as Chromium applies them to a box (Filter Effects 1, CSS Masking 1): in
   the box's user space -- its border box's top left the origin, a CSS px a unit, the border box (bw x bh) the bounding box
   and the viewport of percentages. Appended to out, an entry for each, each its first line: "f 1" and a filter's group (its
   g line and F block) for each url() of the filter's list that names a filter, in the list's order, "f 0" for one that
   names none (passed over, as Chromium passes it over, an empty filter's too); "c 1" and the clip's groups (a clipPath's own
   clip-path's first) when the clip-path names a clipPath; "m 1" and the mask's group when the mask names a mask, "m 2" when
   it names none or one of no area (nothing is drawn) */
void amb_svg_effects(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t el, double bw,
                     double bh, vector<char> *out);

/* an entry's groups (s: amb_svg_effects's lines after its f, c or m line) applied to a layer of w x h px (premultiplied
   RGBA, each 0 to 1, row after row), m mapping the box's user space into the layer's px (x' = m[0] x + m[2] y + m[4],
   y' = m[1] x + m[3] y + m[5]): the layer drawn through their filters, clips and masks, as a group of a scene is; nt the
   loader a mask's images come from (or none) */
void amb_svg_fx(const char *s, size_t n, int w, int h, const double *m, const net_loader *nt, vector<float> *layer);

/* a scene drawn onto c: its viewport's top left at (x, y) of the page in c's px (rounded to
   whole px, as Chromium places an svg's content), sx and sy of c's px a CSS px; its image
   elements' pictures from the loader nt's resources (amb_svg_draw: none, so they draw nothing) */
void amb_svg_draw(amb_canvas *c, const char *scene, size_t n, double x, double y, double sx, double sy);
void amb_svg_draw_with(amb_canvas *c, const net_loader *nt, const char *scene, size_t n, double x, double y, double sx, double sy);

/* the SVG document the loader fetched for the element el (an img), drawn into the rectangle
   (x, y, w, h) of c: 1 when it is one */
int amb_svg_image_of(amb_canvas *c, const net_loader *nt, uint32_t el, double x, double y, double w, double h);

/* the SVG document of the loader's resource r: its natural size and ratio (amb_svg_size); 0
   when it is none */
int amb_svg_size_of(const net_loader *nt, uint32_t r, double *w, double *h, double *ratio);

/* the SVG document of the loader's resource r drawn as a w x h image, RGBA8 (not
   premultiplied), into rgba: 1 when it is one */
int amb_svg_raster_of(const net_loader *nt, uint32_t r, uint32_t w, uint32_t h, vector<uint8_t> *rgba);

/* an SVG document's bytes drawn into the rectangle (x, y, w, h) of c: 1 when they are one */
int amb_svg_image(amb_canvas *c, const uint8_t *p, size_t n, double x, double y, double w, double h);

/* path data (SVG 2, 9.3) read as Chromium reads it: the commands up to the first error,
   made absolute moves, lines, cubics and closes -- each its letter ('M', 'L', 'C', 'Z') as
   a number, then its points' coordinates -- appended to out; how many were read */
size_t amb_svg_path(const char *s, size_t n, vector<double> *out);

/* the coverage of path data (amb_svg_path's commands) under the matrix m (x' = m[0] x + m[2] y + m[4], y' = m[1] x
   + m[3] y + m[5]) by the non-zero rule or the even-odd, on a w x h grid, as an SVG fill is covered (sixteen rows a
   pixel): the rectangle it reaches into box (x0, y0, x1, y1), each of its pixels' coverage (0 to 1, a row the box's
   width) into cov; 0 when it reaches none */
int amb_path_coverage(const vector<double> *path, const double *m, int evenodd, int w, int h, int *box, vector<float> *cov);

/* a scene's coverage drawn onto a w x h grey canvas (0 white, 255 the shapes' own ink),
   for tests: each pixel's alpha after the scene is drawn in black at 1 CSS px a px */
void amb_svg_coverage(const char *scene, size_t n, uint32_t w, uint32_t h, vector<uint8_t> *out);

#endif
