/* src/paint/brotli.h -- Brotli (RFC 7932) decoded: a WOFF2 font's tables (font.c), HTTP's br and
   dcb codings (src/net/net.c), whole, into a vector. src/paint/brotli.c says how; brotli_dict.h
   holds the RFC's dictionary, its transforms and the context and length tables
   (tools/gen-brotli.py). */
#ifndef AMB_BROTLI_H
#define AMB_BROTLI_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* the Brotli stream in (n bytes; any after its end are not read) decoded, its bytes
   appended to out, at most cap of them: 1, or 0 when it is no stream RFC 7932 allows --
   or one longer than cap -- (out then holds what was decoded before the fault) */
int amb_brotli(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap);
/* the same with a raw dictionary of dn bytes attached (RFC 9842's dcb; none when dn is 0); a
   stream that fails by running out of input, cut short, sets *cut, out holding every byte
   decoded before the cut (else *cut is 0) */
int amb_brotli_prefix(const uint8_t *in, size_t n, const uint8_t *dict, size_t dn, vector<uint8_t> *out, size_t cap, int *cut);

#endif
