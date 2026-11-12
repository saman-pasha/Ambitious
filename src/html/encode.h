/* src/html/encode.h -- the WHATWG Encoding standard's encoders, src/html/encoding.c's: what a URL's
   query is written in when its document's encoding is a legacy one (the URL standard's
   percent-encode after encoding, src/net/url.c). Plain types alone, so that a unit outside the
   HTML module includes it. An encoding is its number, as src/html/encoding.h has it (-1 none). */
#ifndef AMBITIOUS_HTML_ENCODE_H
#define AMBITIOUS_HTML_ENCODE_H
#include <stdint.h>
#include <stddef.h>

/* the encoding a document's URLs are written in (Encoding's get an output encoding): enc, or
   UTF-8 for UTF-16BE, UTF-16LE, replacement and none */
int html_output_encoding(int enc);

/* one code point by enc's encoder (the standard's, 9 to 14), its bytes into out (at most 8):
   their count, *state ISO-2022-JP's (0, ASCII, at first), and *error the code point to report
   when the encoder cannot write it (the standard's error, U+FFFD for some), else 0. The code
   point 0xFFFFFFFF is the end of the queue (ISO-2022-JP back to ASCII) */
size_t html_encode_cp(int enc, uint32_t cp, uint8_t *out, int *state, uint32_t *error);

#endif
