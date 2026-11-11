/* src/paint/image.h -- images decoded for the painter (src/paint/image.c): PNG, every
   colour type, bit depth and interlacing, into RGBA8 rows. */
#ifndef AMB_IMAGE_H
#define AMB_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* image.c's inflate, which HTTP's content codings share (src/net/net.c): a zlib
   stream (RFC 1950), or a raw deflate stream (RFC 1951) from in[*pos] that leaves *pos
   just past it, into out, at most cap bytes; 0 when the bytes are not one */
int amb_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap);
int amb_inflate_raw(const uint8_t *in, size_t n, size_t *pos, vector<uint8_t> *out, size_t cap);

/* the PNG's pixels, RGBA8, w x h, into rgba; 0 when the bytes are no PNG it reads */
int amb_png_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

#endif
