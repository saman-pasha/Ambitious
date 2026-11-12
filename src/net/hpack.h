/* src/net/hpack.h -- HPACK (RFC 7541), HTTP/2's header compression (src/net/h2.c):
   header blocks decoded and encoded over a connection's two dynamic tables.
   src/net/hpack.c says how.

   A header list is fields, each its name's and value's places in a byte buffer of the
   list's. The structs with vectors come after the prototypes: a prototype after a
   vector<T> in a header may not be registered (toolchain/REPORT.md, finding 22). */
#ifndef NET_HPACK_H
#define NET_HPACK_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* a header field of a list: its name and value in the list's bytes; never, a field
   that must never be indexed (RFC 7541 6.2.3), as it came or as it is to be sent */
typedef struct hpack_field { uint32_t name, nlen, value, vlen, never; } hpack_field;
/* a dynamic table's entry: its name, then its value, at off in the table's bytes */
typedef struct hpack_entry { uint32_t off, nlen, vlen; } hpack_entry;

/* how the encoder chooses a field's representation: HPACK_INDEX_ALL indexes every field
   no table holds whole (RFC 7541's examples, Appendix C); HPACK_CHROMIUM as Chromium's
   encoder (quiche's HpackEncoder) does, a pseudo-header field but :authority sent
   literally without indexing */
enum { HPACK_INDEX_ALL = 0, HPACK_CHROMIUM = 1 };

/* what hpack_decode found: the block whole, a block that is no HPACK (a connection's
   COMPRESSION_ERROR), or a block whose list passed its bound (its table kept in step) */
enum { HPACK_OK = 1, HPACK_BAD = 0, HPACK_TOO_LARGE = 2 };

typedef struct hpack_table hpack_table;

/* a table whose maximum size may be set as high as limit (its decoder's
   SETTINGS_HEADER_TABLE_SIZE); its size starts at RFC 7541's 4096, or limit if less */
void hpack_init(hpack_table *t, uint32_t limit);
/* its vectors dropped, by its owner (a function given a pointer to the table may not drop
   them: toolchain/REPORT.md, finding 10) */
#define HPACK_TABLE_DROP(t) vector_hpack_entry_drop(&(t).ents); vector_uint8_t_drop(&(t).bytes)

/* a header block decoded (RFC 7541 3, 6): its fields and their bytes appended to fields
   and bytes, the table brought up to date. HPACK_OK; HPACK_BAD with why in words; or
   HPACK_TOO_LARGE once the list (each field's name and value and 32) passes max_list,
   the fields past it not appended, the block decoded to its end */
int hpack_decode(hpack_table *t, const uint8_t *in, size_t n, vector<hpack_field> *fields, vector<uint8_t> *bytes, size_t max_list,
                 char *why, size_t wcap);

/* the nf fields of f (their text in bytes) encoded as a header block into out, by the
   policy: a size update first when one is owed; then each field indexed when a table
   holds it whole, else literal, its strings Huffman coded when that is shorter (huffman
   1, as Chromium codes them) or always (2, as RFC 7541's examples do), else raw (0); a
   field marked never as a literal never indexed */
void hpack_encode(hpack_table *t, const hpack_field *f, size_t nf, const uint8_t *bytes, int policy, int huffman, vector<uint8_t> *out);

/* the peer's SETTINGS_HEADER_TABLE_SIZE taken by the encoder: its table's maximum, said
   in a size update at the next block's start (the least one since the last block
   first, when it was less), as quiche's encoder says it */
void hpack_set_limit(hpack_table *t, uint32_t limit);

/* Huffman (RFC 7541 Appendix B): the code's length in bytes of n bytes; the code
   appended to out; a code decoded, appended to out (0 when it is no code: an EOS, or
   padding of 8 bits or more, or not all ones) */
size_t hpack_huffman_size(const uint8_t *s, size_t n);
void hpack_huffman_encode(const uint8_t *s, size_t n, vector<uint8_t> *out);
int hpack_huffman_decode(const uint8_t *s, size_t n, vector<uint8_t> *out);

/* an integer of a prefix of bits bits (RFC 7541 5.1), its first byte's other bits
   flags, appended to out; one read from in[*at]: 0 when it does not fit in 2^28 or the
   input ends */
void hpack_int_encode(uint32_t v, uint32_t bits, uint8_t flags, vector<uint8_t> *out);
int hpack_int_decode(const uint8_t *in, size_t n, size_t *at, uint32_t bits, uint32_t *v);

/* the table: its entries oldest first from first on, their bytes; its size (each
   entry's name and value and 32), its maximum and the bound on that; for the encoder,
   whether a size update is owed and the least maximum since the last block */
struct hpack_table {
    vector<hpack_entry> ents;
    vector<uint8_t> bytes;
    uint32_t first, size, max, limit, owed, least;
};

#endif
