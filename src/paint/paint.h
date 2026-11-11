/* src/paint/paint.h -- a laid-out page drawn (src/paint/paint.c): its boxes'
   backgrounds and borders, its lines' text and decorations, its markers, onto a
   canvas of RGB pixels, its images decoded from what the loader fetched; and the
   canvas as a PNG. A unit that includes this has said amb_interface(layout) and
   amb_interface(net) first. The canvas's owner drops its pixels
   (AMB_CANVAS_DROP): a struct's vectors are not autodropped. */
#ifndef AMB_PAINT_H
#define AMB_PAINT_H
#include <stdio.h>
#include <stdint.h>
#include <vector.pl>

typedef struct amb_canvas { vector<uint8_t> px; uint32_t w, h, background; } amb_canvas;
#define AMB_CANVAS_DROP(c) vector_uint8_t_drop(&(c).px)

/* the page laid out in t, drawn onto c (sized to the viewport box, at most 16384
   high); its images from the loader nt's resources, when there is one */
void amb_paint(const layout_tree *t, const net_loader *nt, amb_canvas *c);

/* the canvas as a PNG (8-bit RGB, deflated); 0 on a write error */
int amb_png_write(const amb_canvas *c, FILE *out);

/* the canvas's pixels' FNV-1a 64 hash: what a golden checksum pins */
uint64_t amb_canvas_hash(const amb_canvas *c);

#endif
