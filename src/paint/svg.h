/* src/paint/svg.h -- SVG drawn (src/paint/svg.c): an inline svg element's content, and an
   SVG document an img or a background names, each as Chromium draws it.

   An svg element's subtree is made a SCENE by layout (amb_svg_scene), once its box's size
   is known: text, each line a command -- a path with its paints and stroke, a run of
   glyphs, a group (its opacity, its clip's shapes) and its end -- every coordinate in its
   user space and the matrix that maps it into the viewport's CSS px. Shapes and paths
   become absolute moves, lines and cubic Beziers (arcs and quadratics converted); use
   elements are expanded (their trees inheriting from the use); gradients and clips are
   resolved into user space (objectBoundingBox through the element's box). The cascade
   (src/css) gives every value: presentation attributes, style attributes and the page's
   sheets. Paint draws the scene in the box (amb_svg_draw): its curves flattened at the
   device's px, strokes made outlines (joins, caps, dashes), each shape's coverage found
   over sixteen rows a pixel with exact horizontal spans (non-zero or even-odd), groups
   with an opacity or a clip drawn in a layer of their own.

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

/* a scene drawn onto c: its viewport's top left at (x, y) of the page in c's px (rounded to
   whole px, as Chromium places an svg's content), sx and sy of c's px a CSS px */
void amb_svg_draw(amb_canvas *c, const char *scene, size_t n, double x, double y, double sx, double sy);

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

/* a scene's coverage drawn onto a w x h grey canvas (0 white, 255 the shapes' own ink),
   for tests: each pixel's alpha after the scene is drawn in black at 1 CSS px a px */
void amb_svg_coverage(const char *scene, size_t n, uint32_t w, uint32_t h, vector<uint8_t> *out);

#endif
