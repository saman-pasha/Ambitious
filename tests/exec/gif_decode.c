/* tests/exec/gif_decode.c -- the GIF decoder (src/paint/image.c) against Chromium's
   composition over Pillow's decoding of the same files: each fixture's canvas, frame
   count and the FNV-1a 64 of each frame's RGBA8 canvas, the frame an animation shows at
   rest and how often it plays, which tests/images/gen.py works out (gif_decode.expect;
   its gif_frames says how the composition was measured). Then the hostile files, each
   refused or decoded to a prefix as gen.py names them, and mutations of the fixtures,
   which may decode to anything but must not crash (the gate runs this under
   valgrind). */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include "../../src/paint/image.h"

/* what each fixture exercises is said beside it in tests/images/gen.py (gif_cases) */
static const char *const names[] = {
    "g_local_first", "g_offset_opaque", "g_interlace_1", "g_interlace_2", "g_interlace_3", "g_interlace_5", "g_interlace_9", "g_interlace_17",
    "g_mcs2", "g_mcs3", "g_mcs4", "g_mcs5", "g_mcs6", "g_mcs7", "g_mcs8", "g_full_kept", "g_full_cleared", "g_clears", "g_no_first_clear",
    "g_no_table", "g_past_table", "g_clipped", "g_one_pixel", "g_87a", "g_animated", "g_pil", "g_pil_trans", "g_pil_flat", "g_pil_animated",
    "g_pil_photo", "g_dispose1", "g_dispose2", "g_dispose3", "g_dispose4", "g_dispose6", "g_first_restored", "g_first_past", "g_zero_screen",
    "g_loop_none", "g_loop_1", "g_loop_2", "g_loop_ever", "g_no_delay" };

static const char *const hostile[] = { "bad_gif_cut_header", "bad_gif_cut_table", "bad_gif_cut_lzw", "bad_gif_huge_screen", "bad_gif_huge_frame",
    "bad_gif_code_size", "bad_gif_code_range", "bad_gif_no_image", "bad_gif_zero_screen", "bad_gif_cut_extension" };

static const char *const mutated[] = { "g_local_first", "g_interlace_17", "g_full_kept", "g_clears", "g_animated", "g_pil_photo", "g_dispose3",
    "g_dispose2" };

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
    snprintf(path, sizeof path, "tests/images/%s.gif", name);
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

/* each fixture's bytes changed 40 ways, the same each run: a few bytes flipped, made
   anything, and the file cut short */
static void mutations(void) {
    uint32_t s = 88675123u;
    for (size_t f = 0; f < sizeof mutated / sizeof mutated[0]; f++) {
        vector<uint8_t> file;
        if (!read_fixture(mutated[f], &file) || len(file) < 16) continue;
        for (int m = 0; m < 40; m++) {
            vector<uint8_t> b, rgba;
            append_range(b, data(file), len(file));
            size_t n = len(b);
            int changes = 1 + m % 4;
            for (int c = 0; c < changes; c++) {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                size_t pos = s % n;
                uint32_t kind = (s >> 24) % 3;
                if (kind == 0) b[pos] = (uint8_t)(at(b, pos) ^ (1u << (s >> 8 & 7)));
                else if (kind == 1) b[pos] = (uint8_t)(s >> 16);
                else if (pos > 6) n = pos;
            }
            amb_image_info info;
            uint32_t w, h;
            amb_gif_decode(data(b), n, &rgba, &info);
            amb_gif_frame(data(b), n, AMB_IMAGE_AT_REST, &rgba, &info);
            amb_image_size(data(b), n, &w, &h);
        }
    }
    printf("mutations of the fixtures: each decoded or refused\n");
}

int main(void) {
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        vector<uint8_t> file, rgba;
        amb_image_info info;
        if (!read_fixture(names[i], &file)) {
            printf("%s: no fixture (python3 tests/images/gen.py writes them)\n", names[i]);
            continue;
        }
        if (!amb_gif_decode(data(file), len(file), &rgba, &info)) {
            printf("%s not decoded\n", names[i]);
            continue;
        }
        printf("%s %ux%u, %u frame%s, %016llx%s\n", names[i], info.w, info.h, info.frames, info.frames == 1 ? "" : "s",
               (unsigned long long)fnv(&rgba), info.partial ? " (partial)" : "");
        if (info.frames < 2) continue;
        uint32_t frames = info.frames;
        for (uint32_t k = 1; k < frames; k++) {
            if (!amb_gif_frame(data(file), len(file), k, &rgba, &info)) printf("  frame %u not decoded\n", k);
            else printf("  frame %u %016llx%s\n", k, (unsigned long long)fnv(&rgba), info.partial ? " (partial)" : "");
        }
        if (amb_gif_frame(data(file), len(file), AMB_IMAGE_AT_REST, &rgba, &info)) printf("  at rest frame %u, plays %u\n", info.frame, info.loops);
    }
    for (size_t i = 0; i < sizeof hostile / sizeof hostile[0]; i++) {
        vector<uint8_t> file, rgba;
        amb_image_info info;
        if (!read_fixture(hostile[i], &file)) {
            printf("%s: no fixture\n", hostile[i]);
            continue;
        }
        if (!amb_gif_decode(data(file), len(file), &rgba, &info)) printf("%s: refused\n", hostile[i]);
        else if (info.partial) printf("%s: a prefix %ux%u\n", hostile[i], info.w, info.h);
        else printf("%s: whole %ux%u\n", hostile[i], info.w, info.h);
    }
    mutations();
    return 0;
}
