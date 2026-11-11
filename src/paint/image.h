/* src/paint/image.h -- images decoded for the painter (src/paint/image.c): PNG, every
   colour type, bit depth and interlacing, into RGBA8 rows. */
#ifndef AMB_IMAGE_H
#define AMB_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the PNG's pixels, RGBA8, w x h, into rgba; 0 when the bytes are no PNG it reads */
int amb_png_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

#endif
