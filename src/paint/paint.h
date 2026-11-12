/* src/paint/paint.h -- a laid-out page drawn (src/paint/paint.c): its boxes'
   shadows, backgrounds (colours, gradients, images) and borders in their styles
   and rounded corners, its lines' text, shadows and decorations, its markers, its
   boxes' opacity, transforms, clipping and stacking order, onto a canvas of RGB
   pixels, its images decoded from what the loader fetched; and the canvas as a
   PNG. A unit that includes this has said amb_interface(layout) and
   amb_interface(net) first. The canvas's owner drops its pixels
   (AMB_CANVAS_DROP): a struct's vectors are not autodropped. */
#ifndef AMB_PAINT_H
#define AMB_PAINT_H
#include <stdio.h>
#include <stdint.h>
#include <vector.pl>

/* oy: the page's y the canvas's first row shows (0 for the whole page); the clip: the
   rectangle drawing reaches, in page coordinates (overflow's, as paint sets it); zoom:
   the device px a CSS px the page is drawn at (paint sets it); nearest: images drawn now
   sampled at the nearest pixel (a box's image-rendering: pixelated, as paint sets it) */
typedef struct amb_canvas { vector<uint8_t> px; uint32_t w, h, background; double oy, cx0, cy0, cx1, cy1, zoom; int nearest; } amb_canvas;
#define AMB_CANVAS_DROP(c) vector_uint8_t_drop(&(c).px)

/* the page laid out in t, drawn onto c (sized to the viewport box, at most 16384
   high); its images from the loader nt's resources, when there is one */
void amb_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c);
/* the window of the page from y down h px, as wide as the viewport, drawn onto c: what
   a screen of that height shows scrolled to y. What lies outside it is not drawn */
void amb_paint_view(const layout_tree *t, const net_loader *nt, amb_canvas *c, double y, double h);
/* the page zoomed, z device px a CSS px, drawn onto c at the device's px: its lengths
   scaled before it is drawn, so its text is set at the larger size; its window from
   device px y down h (h 0: the whole page) */
void amb_paint_zoom(const layout_tree *t, const net_loader *nt, amb_canvas *c, double z, double y, double h);
/* a page drawn at its CSS px (src) scaled onto dst, z device px each (bilinear) */
void amb_canvas_zoom(const amb_canvas *src, amb_canvas *dst, double z);

/* a page's frames (src/shell/frame.c, Phase 12u): the drawing of the iframe element node
   of the document doc, its frame's document drawn at its content box's size over black (b)
   and over white (w), kept until the frames are forgotten. Paint draws it as the iframe's
   content, over what lies under its content box: b + (w - b) x under / 255, so what the
   frame left transparent shows the page through; clipped as replaced content is */
void amb_paint_frame_put(uint32_t doc, uint32_t node, const amb_canvas *b, const amb_canvas *w);
void amb_paint_frames_forget(void);

/* the canvas as a PNG (8-bit RGB, deflated); 0 on a write error */
int amb_png_write(const amb_canvas *c, FILE *out);

/* the canvas's pixels' FNV-1a 64 hash: what a golden checksum pins */
uint64_t amb_canvas_hash(const amb_canvas *c);

#endif
