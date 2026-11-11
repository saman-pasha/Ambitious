/* src/paint/image.h -- images decoded for the painter (src/paint/image.c), into RGBA8
   rows: PNG (every colour type, bit depth and interlacing), GIF (the first frame on its
   canvas) and JPEG (baseline and progressive, any sampling, EXIF orientation applied). */
#ifndef AMB_IMAGE_H
#define AMB_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the most pixels an image may have: one larger is refused before anything is allocated */
#define AMB_IMAGE_MAX_PIXELS (1u << 24)

/* what a GIF or JPEG decoder read: the image's size as it is drawn (after a JPEG's
   orientation), its frames (a GIF's count, else 1), the EXIF orientation applied (1 to
   8), and partial when the data ended or broke before the image was whole -- what was
   decoded is kept, the rest left grey (JPEG) or transparent (GIF) */
typedef struct amb_image_info { uint32_t w, h, frames, orientation; int32_t partial; } amb_image_info;

/* image.c's inflate, which HTTP's content codings share (src/net/net.c): a zlib
   stream (RFC 1950), or a raw deflate stream (RFC 1951) from in[*pos] that leaves *pos
   just past it, into out, at most cap bytes; 0 when the bytes are not one */
int amb_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap);
int amb_inflate_raw(const uint8_t *in, size_t n, size_t *pos, vector<uint8_t> *out, size_t cap);

/* the PNG's pixels, RGBA8, w x h, into rgba; 0 when the bytes are no PNG it reads */
int amb_png_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

/* the GIF's first frame composed onto its logical screen, transparent where the frame
   does not reach or its transparent index stands; 0 when the bytes are no GIF it reads */
int amb_gif_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info);

/* the JPEG's pixels, its EXIF orientation applied; 0 when the bytes are no JPEG it reads
   (lossless, hierarchical and arithmetic-coded ones, 12-bit samples, a size past the cap) */
int amb_jpeg_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info);

/* PNG, GIF or JPEG, known by its signature, decoded: what the painter draws */
int amb_image_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

/* the size an image is drawn at, from its header alone: a JPEG's EXIF orientation 5 to 8
   swaps its width and height; 0 when the bytes are none of the three */
int amb_image_size(const uint8_t *p, size_t n, uint32_t *w, uint32_t *h);

#endif
