/* src/net/zstd.h -- Zstandard (RFC 8878) decoded, for HTTP's zstd content coding
   (src/net/net.c). src/net/zstd.c says how. */
#ifndef NET_ZSTD_H
#define NET_ZSTD_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* what a decoding met, counted: tests/exec/net_zstd.c shows its vectors reach every
   kind of block, literals section and table mode. seq_mode is by symbol type (literal
   lengths, offsets, match lengths), then mode (predefined, RLE, FSE, repeat) */
typedef struct net_zstd_stats {
    uint32_t frames, skippable, raw_blocks, rle_blocks, compressed_blocks;
    uint32_t lit_raw, lit_rle, lit_huffman, lit_treeless, lit_one_stream, lit_four_streams, weights_direct, weights_fse;
    uint32_t seq_mode[3][4];
    uint32_t sequences, repeat_offsets, checksums;
} net_zstd_stats;

/* the zstd content in (n bytes) decoded: each frame's bytes appended to out, a
   skippable frame passed over, at most cap bytes in all. 1; 0 with why in words when it
   is no content RFC 8878 allows (a frame cut short, corrupt, its checksum or size not
   its content's), or one Chromium's decoder refuses (a frame that names a dictionary,
   a window past 8 MB); -1 when it decodes to more than cap. stats, when given, counts
   what was met */
int net_zstd(const uint8_t *in, size_t n, vector<uint8_t> *out, size_t cap, char *why, size_t wcap, net_zstd_stats *stats);

/* XXH64 of the n bytes at p, from seed: a zstd frame's checksum is its low 32 bits,
   from seed 0 */
uint64_t net_xxh64(const uint8_t *p, size_t n, uint64_t seed);

#endif
