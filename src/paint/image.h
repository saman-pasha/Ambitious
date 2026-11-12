/* src/paint/image.h -- images decoded for the painter (src/paint/image.c and webp.c), into
   RGBA8 rows: PNG (every colour type, bit depth and interlacing), GIF (each frame composed
   on its canvas), JPEG (baseline and progressive, any sampling, EXIF orientation applied)
   and WebP (lossy, lossless, with alpha, animated). An animation is drawn at the frame it
   shows once the page is at rest. */
#ifndef AMB_IMAGE_H
#define AMB_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the most pixels an image may have: one larger is refused before anything is allocated */
#define AMB_IMAGE_MAX_PIXELS (1u << 24)

/* the frames' areas, summed, an animation may decode to reach the frame it draws: past
   it, the last frame within is drawn, marked partial */
#define AMB_IMAGE_MAX_FRAME_WORK (1ull << 28)

/* the time an animation is drawn at, ms after it loaded: the page at rest, after its
   scripts' virtual time (SCRIPT_HORIZON, src/shell/main.c). Chromium shows an animation's
   first frame at load, then each frame for its duration (10 ms or less taken as 100), and
   stops on the last frame once it has played as often as it says; a screenshot 10 s
   after load shows the frame that timeline reaches. */
#define AMB_IMAGE_REST_MS 10000u

/* as a frame's index: the frame an animation shows at rest */
#define AMB_IMAGE_AT_REST 0xffffffffu

/* what a GIF, JPEG or WebP decoder read: the image's size as it is drawn (after a JPEG's
   orientation), its frames (1 for a still image), the EXIF orientation applied (1 to 8),
   partial when the data ended or broke before the image was whole -- what was decoded is
   kept, the rest left grey (JPEG) or transparent (GIF) -- how many times an animation
   plays as Chromium counts it (a GIF's loop count plus one, or once with none; a WebP's
   loop count; 0 for ever), and the frame decoded */
typedef struct amb_image_info { uint32_t w, h, frames, orientation; int32_t partial; uint32_t loops, frame; } amb_image_info;

/* image.c's inflate, which HTTP's content codings share (src/net/net.c): a zlib
   stream (RFC 1950), or a raw deflate stream (RFC 1951) from in[*pos] that leaves *pos
   just past it, into out, at most cap bytes; 0 when the bytes are not one */
int amb_inflate(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap);
int amb_inflate_raw(const uint8_t *in, size_t n, size_t *pos, vector<uint8_t> *out, size_t cap);

/* the PNG's pixels, RGBA8, w x h, into rgba; 0 when the bytes are no PNG it reads */
int amb_png_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

/* the frame of an animation of count frames (their durations ms[]) shown at ms at after
   it started, by Chromium's timeline (AMB_IMAGE_REST_MS); plays as amb_image_info's
   loops */
uint32_t amb_image_frame_at(const uint32_t *ms, uint32_t count, uint32_t plays, uint32_t at);

/* the GIF's frame index (or AMB_IMAGE_AT_REST) composed as Chromium composes it, on a
   transparent canvas the size of the logical screen and the first frame together: each
   frame's transparent index leaving what is under it, disposal 2 clearing its rectangle
   to transparent, 3 and 4 restoring what was there before it; amb_gif_decode is its
   first frame. 0 when the bytes are no GIF it reads */
int amb_gif_frame(const uint8_t *p, size_t n, uint32_t index, vector<uint8_t> *rgba, amb_image_info *info);
int amb_gif_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info);

/* the JPEG's pixels, its EXIF orientation applied; 0 when the bytes are no JPEG it reads
   (lossless, hierarchical and arithmetic-coded ones, 12-bit samples, a size past the cap:
   Chromium lays the header's size out for the ones it parses, and draws nothing) */
int amb_jpeg_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, amb_image_info *info);

/* the WebP's frame index (or AMB_IMAGE_AT_REST): a still image, or an animation's
   canvas composed to that frame as libwebp composes it; 0 when the bytes are no WebP it
   reads, are malformed, or end short */
int amb_webp_frame(const uint8_t *p, size_t n, uint32_t index, vector<uint8_t> *rgba, amb_image_info *info);

/* the WebP's canvas size from its chunks, which must be well formed though they may end
   short; 0 when they are not */
int amb_webp_size(const uint8_t *p, size_t n, uint32_t *w, uint32_t *h);

/* PNG, GIF, JPEG or WebP, known by its signature, decoded, an animation at rest: what
   the painter draws */
int amb_image_decode(const uint8_t *p, size_t n, vector<uint8_t> *rgba, uint32_t *w, uint32_t *h);

/* the size an image is drawn at, from its header alone: a JPEG's EXIF orientation 5 to 8
   swaps its width and height, a GIF's canvas holds its first frame; 0 when the bytes are
   none of the four */
int amb_image_size(const uint8_t *p, size_t n, uint32_t *w, uint32_t *h);

#endif
