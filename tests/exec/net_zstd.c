/* tests/exec/net_zstd.c -- the zstd decoder (src/net/zstd.c), HTTP's zstd content coding.

   Each vector of tests/exec/zstd/ decoded: libzstd 1.5.7's frames as node's zlib writes
   them (tests/net/zstd-vectors.js made them, and printed these lines): text at levels 1
   to 19, with and without a checksum or a content size, a 1 KB window, runs, noise,
   mixtures, short texts of a few letters, long-distance matches; two frames with a
   skippable one between them; and two frames Chromium's decoder refuses, one naming a
   dictionary and one of a 16 MB window. Each one's length and CRC-32 are what node's
   decoder makes of it, or it is refused as node's, held to Chromium's 8 MB window,
   refuses it. Then what the vectors reached, summed: every kind of block, literals
   section and table mode. XXH64's known answer for no bytes; a bound one byte short;
   a checksum, a size, an end and a tail broken by hand, each refused with its reason;
   then every vector broken at random -- bits flipped, bytes set, runs of them, the
   content cut short -- each decoded or refused, nothing read outside it (valgrind). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/net/zstd.h"

static const char *const names[] = { "empty", "byte", "hello", "words-1", "words-3", "words-9", "words-19", "words-nochecksum",
                                     "words-nosize", "small-text", "window10", "run", "noise", "mixed-2", "mixed-9", "mixed-16",
                                     "letters-0-40", "letters-0-300", "letters-0-3000", "letters-1-40", "letters-1-300",
                                     "letters-1-3000", "letters-2-40", "letters-2-300", "letters-2-3000", "letters-3-40",
                                     "letters-3-300", "letters-3-3000", "letters-4-40", "letters-4-300", "letters-4-3000", "ldm",
                                     "frames", "dictionary", "window24", "slices", "low-500", "low-3000", "rle" };

static int read_file(const char *path, vector<uint8_t> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return c ^ 0xffffffffu;
}

/* a vector read by its name */
static int vector_of(const char *name, vector<uint8_t> *out) {
    char path[256];
    snprintf(path, sizeof path, "tests/exec/zstd/%s.zst", name);
    return read_file(path, out);
}

/* src decoded with a bound, and what came of it said */
static void decode_say(const char *label, const uint8_t *p, size_t n, size_t cap) {
    vector<uint8_t> out;
    char why[300];
    why[0] = 0;
    int r = net_zstd(p, n, &out, cap, why, sizeof why, 0);
    if (r == 1) printf("%s: %zu bytes, crc32 %08x\n", label, len(out), crc32_of(data(out), len(out)));
    else printf("%s: refused, %s (%s)\n", label, r < 0 ? "past its bound" : "corrupt", why);
}

/* src broken from seed and decoded: 1 when it was, 0 when refused */
static int broken(const vector<uint8_t> *src, uint32_t seed, size_t *bytes) {
    vector<uint8_t> b, out;
    append_range(b, data(*src), len(*src));
    if (!len(b)) return 0;
    uint32_t x = seed * 2654435761u + 7;
    if (seed % 9 == 0) {
        x = x * 1103515245u + 12345u;
        resize(&b, (x >> 8) % len(b));
    } else {
        int n = 1 + (int)(seed % 5);
        for (int k = 0; k < n; k++) {
            x = x * 1103515245u + 12345u;
            size_t p = (x >> 8) % len(b);
            x = x * 1103515245u + 12345u;
            if (seed % 4 == 0)
                for (size_t j = 0; j < 1 + (x >> 16) % 24 && p + j < len(b); j++) b[p + j] = (uint8_t)(x >> (j % 24));
            else if (seed % 4 == 1) b[p] = (uint8_t)(x >> 16);
            else b[p] ^= (uint8_t)(1u << ((x >> 16) % 8));
        }
    }
    char why[300];
    int ok = net_zstd(data(b), len(b), &out, (size_t)1 << 24, why, sizeof why, 0);
    *bytes += len(out);
    return ok == 1;
}

/* each file named decoded, and what it reached: for choosing vectors (tests/net/zstd-vectors.js) */
static int each_file(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        vector<uint8_t> z, out;
        net_zstd_stats s;
        memset(&s, 0, sizeof s);
        char why[300];
        why[0] = 0;
        if (!read_file(argv[i], &z)) continue;
        int r = net_zstd(data(z), len(z), &out, (size_t)1 << 25, why, sizeof why, &s);
        printf("%s: %d %zu %s | blocks %u %u %u | lit %u %u %u %u | streams %u %u | weights %u %u | ll %u %u %u %u | of %u %u %u %u | ml %u %u %u %u\n",
               argv[i], r, len(out), why, s.raw_blocks, s.rle_blocks, s.compressed_blocks, s.lit_raw, s.lit_rle, s.lit_huffman,
               s.lit_treeless, s.lit_one_stream, s.lit_four_streams, s.weights_direct, s.weights_fse, s.seq_mode[0][0], s.seq_mode[0][1],
               s.seq_mode[0][2], s.seq_mode[0][3], s.seq_mode[1][0], s.seq_mode[1][1], s.seq_mode[1][2], s.seq_mode[1][3], s.seq_mode[2][0],
               s.seq_mode[2][1], s.seq_mode[2][2], s.seq_mode[2][3]);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) return each_file(argc, argv);
    printf("-- the vectors (tests/exec/zstd/), each decoded:\n");
    net_zstd_stats st;
    memset(&st, 0, sizeof st);
    size_t nv = sizeof names / sizeof names[0];
    for (size_t i = 0; i < nv; i++) {
        vector<uint8_t> z, out;
        if (!vector_of(names[i], &z)) {
            printf("%s.zst: missing\n", names[i]);
            continue;
        }
        char why[300];
        int r = net_zstd(data(z), len(z), &out, (size_t)1 << 25, why, sizeof why, &st);
        if (r == 1) printf("%s.zst: %zu bytes, crc32 %08x\n", names[i], len(out), crc32_of(data(out), len(out)));
        else printf("%s.zst: refused (%s)\n", names[i], why);
    }
    printf("-- what they reached:\n");
    printf("frames %u, skippable %u; blocks raw %u, RLE %u, compressed %u\n", st.frames, st.skippable, st.raw_blocks, st.rle_blocks,
           st.compressed_blocks);
    printf("literals raw %u, RLE %u, Huffman %u, treeless %u; in one stream %u, in four %u; weights direct %u, FSE coded %u\n", st.lit_raw,
           st.lit_rle, st.lit_huffman, st.lit_treeless, st.lit_one_stream, st.lit_four_streams, st.weights_direct, st.weights_fse);
    static const char *const kinds[3] = { "literal lengths", "offsets", "match lengths" };
    for (int k = 0; k < 3; k++)
        printf("%s: predefined %u, RLE %u, FSE %u, repeat %u\n", kinds[k], st.seq_mode[k][0], st.seq_mode[k][1], st.seq_mode[k][2],
               st.seq_mode[k][3]);
    printf("sequences %u, of them repeat offsets %u; checksums %u\n", st.sequences, st.repeat_offsets, st.checksums);

    printf("-- XXH64 of no bytes, seed 0: %016llx (xxHash's ef46db3751d8e999)\n", (unsigned long long)net_xxh64((const uint8_t *)"", 0, 0));

    printf("-- broken by hand:\n");
    vector<uint8_t> w;
    vector_of("words-3", &w);
    size_t wn = len(w);
    decode_say("words-3 with a bound one byte short of its content", data(w), wn, 199999);
    decode_say("words-3 with its bound its content", data(w), wn, 200000);
    {
        vector<uint8_t> b;
        append_range(b, data(w), wn);
        b[wn - 1] ^= 0x40;
        decode_say("words-3 with a bit of its checksum flipped", data(b), wn, 1 << 24);
        b[wn - 1] ^= 0x40;
        decode_say("words-3 cut 5 bytes short", data(b), wn - 5, 1 << 24);
        push_back(b, (uint8_t)'x');
        push_back(b, (uint8_t)'y');
        push_back(b, (uint8_t)'z');
        push_back(b, (uint8_t)'!');
        decode_say("words-3 and four bytes after it", data(b), len(b), 1 << 24);
        b[0] = 0x29;
        decode_say("words-3 with its magic number's first byte changed", data(b), len(b), 1 << 24);
    }
    {
        /* hello's frame content size, one byte, made one more */
        vector<uint8_t> b;
        vector_of("hello", &b);
        b[5]++;
        decode_say("hello with its content size one more", data(b), len(b), 1 << 24);
        b[5]--;
        b[4] |= 8;
        decode_say("hello with its frame header's reserved bit set", data(b), len(b), 1 << 24);
    }

    printf("-- each vector broken at random 300 ways, decoded or refused:\n");
    unsigned long ok = 0, tried = 0;
    size_t bytes = 0;
    for (size_t i = 0; i < nv; i++) {
        vector<uint8_t> z;
        vector_of(names[i], &z);
        for (uint32_t seed = 1; seed <= 300; seed++) {
            ok += (unsigned long)broken(&z, seed + (uint32_t)i * 1000u, &bytes);
            tried++;
        }
    }
    printf("%lu broken contents: %lu decoded, %lu refused, %zu bytes made\n", tried, ok, tried - ok, bytes);
    return 0;
}
