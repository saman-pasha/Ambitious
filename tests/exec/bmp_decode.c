/* tests/exec/bmp_decode.c -- the BMP and ICO decoders (src/paint/image.c's bmp_* and ico_*):
   each fixture of tests/pages/bmp/ (tools/gen-bmp.py's, whose pixels Chromium was measured
   to draw: tests/pages/bmp.html's regions) decoded, its size, the size its header gives
   (amb_image_size, what layout boxes) and the FNV-1a 64 of its RGBA8 pixels; then hostile
   files made here, each decoded to something or refused but never read past its end; and
   mutations of the fixtures, which may decode to anything but must not crash (the gate runs
   this under valgrind). */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include "../../src/paint/image.h"

static const char *const names[] = { "rgb24.bmp", "rgb24-top.bmp", "rgb32.bmp", "rgba32.bmp", "rgb16.bmp", "rgb565.bmp", "v3-alpha.bmp",
    "v5-alpha.bmp", "v4-rgb.bmp", "pal8.bmp", "pal4.bmp", "pal1.bmp", "core8.bmp", "core24.bmp", "rle8.bmp", "rle4.bmp", "truncated.bmp",
    "icon.ico", "icon-png.ico" };

static uint64_t fnv(const vector<uint8_t> *b) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len(*b); i++) {
        h ^= at(*b, i);
        h *= 1099511628211ull;
    }
    return h;
}

static int read_fixture(const char *name, vector<uint8_t> *out) {
    char path[256];
    snprintf(path, sizeof path, "tests/pages/bmp/%s", name);
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static void put16(vector<uint8_t> *b, uint32_t v) { push_back(*b, (uint8_t)v); push_back(*b, (uint8_t)(v >> 8)); }
static void put32(vector<uint8_t> *b, uint32_t v) { put16(b, v & 0xffff); put16(b, v >> 16); }

/* a BMP made here: a file header, a 40-byte info header of w x h, bits, compression comp, the palette's
   entries (npal, each 4 bytes of value fill), then data's n bytes */
static void make_bmp(vector<uint8_t> *b, int32_t w, int32_t h, uint32_t bits, uint32_t comp, uint32_t npal, const uint8_t *d, size_t n) {
    clear(*b);
    push_back(*b, (uint8_t)'B');
    push_back(*b, (uint8_t)'M');
    put32(b, (uint32_t)(54 + 4 * npal + n));
    put32(b, 0);
    put32(b, 54 + 4 * npal);
    put32(b, 40);
    put32(b, (uint32_t)w);
    put32(b, (uint32_t)h);
    put16(b, 1);
    put16(b, bits);
    put32(b, comp);
    put32(b, (uint32_t)n);
    put32(b, 2835);
    put32(b, 2835);
    put32(b, npal);
    put32(b, 0);
    for (uint32_t k = 0; k < npal; k++) put32(b, 0x00336699u + k);
    append_range(b, d, n);
}

static void report(const char *name, const vector<uint8_t> *file) {
    vector<uint8_t> rgba;
    uint32_t w = 0, h = 0, hw = 0, hh = 0;
    int ok = amb_image_decode(data(*file), len(*file), &rgba, &w, &h);
    int sized = amb_image_size(data(*file), len(*file), &hw, &hh);
    if (ok) printf("%s %ux%u, its header %ux%u, %016llx\n", name, w, h, hw, hh, (unsigned long long)fnv(&rgba));
    else if (sized) printf("%s refused, its header %ux%u\n", name, hw, hh);
    else printf("%s refused\n", name);
}

int main(void) {
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
        vector<uint8_t> file;
        if (!read_fixture(names[k], &file)) { printf("%s missing\n", names[k]); continue; }
        report(names[k], &file);
    }
    /* hostile files: each read within its bytes */
    vector<uint8_t> b;
    uint8_t rle_long[] = { 255, 1, 255, 1, 0, 2, 200, 200, 0, 5, 1, 2, 3, 4, 5, 0, 0, 0, 1 };
    make_bmp(&b, 4, 4, 8, 1, 2, rle_long, sizeof rle_long);
    report("bad_rle8_overrun", &b);
    uint8_t rle_abs[] = { 0, 250, 1, 2 };
    make_bmp(&b, 4, 4, 8, 1, 2, rle_abs, sizeof rle_abs);
    report("bad_rle8_absolute_cut", &b);
    uint8_t rle4[] = { 9, 0x12, 0, 9, 0x12, 0x34, 0x56, 0x78, 0x90, 0, 0, 0, 1 };
    make_bmp(&b, 3, 2, 4, 2, 16, rle4, sizeof rle4);
    report("bad_rle4_runs", &b);
    uint8_t px[16] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    make_bmp(&b, 4, 2, 8, 0, 2, px, sizeof px);
    report("bad_pal_index", &b);
    make_bmp(&b, 4, -2, 8, 1, 2, px, sizeof px);
    report("bad_topdown_rle", &b);
    make_bmp(&b, 100000, 100000, 24, 0, 0, px, sizeof px);
    report("bad_huge", &b);
    make_bmp(&b, 0, 4, 24, 0, 0, px, sizeof px);
    report("bad_zero_width", &b);
    make_bmp(&b, 4, 4, 7, 0, 0, px, sizeof px);
    report("bad_bits", &b);
    make_bmp(&b, 4, 4, 32, 3, 0, px, 0);
    report("bad_bitfields_cut", &b);
    make_bmp(&b, 4, 4, 24, 4, 0, px, sizeof px);
    report("bad_jpeg_inside", &b);
    make_bmp(&b, 4, 4, 24, 0, 0, px, 3);
    report("bad_pixels_cut", &b);
    /* icons: an entry past the file, a count past its directory, an entry's DIB cut short */
    clear(b);
    put16(&b, 0); put16(&b, 1); put16(&b, 3);
    for (int k = 0; k < 3; k++) {
        push_back(b, (uint8_t)16); push_back(b, (uint8_t)16); push_back(b, (uint8_t)0); push_back(b, (uint8_t)0);
        put16(&b, 1); put16(&b, 32); put32(&b, 1000); put32(&b, k == 2 ? 54 : 999999);
    }
    for (int k = 0; k < 10; k++) push_back(b, (uint8_t)k);
    report("bad_ico_entries", &b);
    clear(b);
    put16(&b, 0); put16(&b, 1); put16(&b, 60000);
    report("bad_ico_count", &b);
    /* mutations: each fixture's bytes changed 40 ways, the same each run */
    uint32_t s = 2718281828u;
    int decoded = 0, refused = 0;
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
        vector<uint8_t> file;
        if (!read_fixture(names[k], &file) || len(file) < 16) continue;
        for (int m = 0; m < 40; m++) {
            vector<uint8_t> f2;
            append_range(f2, data(file), len(file));
            for (int q = 0; q < 1 + m % 6; q++) {
                s = s * 1103515245u + 12345u;
                size_t at_ = (s >> 8) % len(f2);
                s = s * 1103515245u + 12345u;
                f2[at_] = (uint8_t)(m % 3 == 0 ? f2[at_] ^ (1u << (s >> 13) % 8) : (s >> 16));
            }
            if (m % 5 == 4) resize(f2, len(f2) * (size_t)(1 + m % 4) / 5);
            vector<uint8_t> rgba;
            uint32_t w = 0, h = 0, hw = 0, hh = 0;
            if (amb_image_decode(data(f2), len(f2), &rgba, &w, &h)) decoded++;
            else refused++;
            amb_image_size(data(f2), len(f2), &hw, &hh);
        }
    }
    printf("mutations: %d decoded, %d refused, none read past its end\n", decoded, refused);
    return 0;
}
