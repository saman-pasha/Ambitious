/* tests/exec/webp_decode.c -- the WebP decoder (src/paint/webp.c) against Pillow's decoding
   of the same files (libwebp's, which Chromium's pixels were measured to equal): each
   fixture's canvas, its frames, how often it plays, the frame it shows at rest, the
   size its header gives (amb_image_size, what layout boxes), and the FNV-1a 64 of each
   frame's RGBA8 pixels -- a still image's, or an animation's canvas composed to that
   frame -- which tests/images/gen.py takes from Pillow (webp_decode.expect). Then the
   hostile files, each refused (with the size its header still gives, as Chromium lays
   out a WebP it cannot decode) as gen.py names them, and mutations of the fixtures,
   which may decode to anything but must not crash (the gate runs this under
   valgrind). */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include "../../src/paint/image.h"

/* what each fixture exercises is said beside it in tests/images/gen.py (webp_cases) */
static const char *const names[] = {
    "w_ll_photo", "w_ll_fast", "w_ll_best", "w_ll_rgba", "w_ll_exact", "w_ll_pal2", "w_ll_pal4", "w_ll_pal16", "w_ll_pal200", "w_ll_1x1",
    "w_ll_odd", "w_ll_wide", "w_lossy_photo", "w_lossy_q5", "w_lossy_q100", "w_lossy_m0", "w_lossy_5x3", "w_lossy_1x1", "w_lossy_17x2",
    "w_lossy_33x1", "w_lossy_simple", "w_lossy_sharp", "w_lossy_seg1", "w_lossy_nofilter", "w_vpx_parts", "w_vpx_simple", "w_alpha",
    "w_alpha_q50", "w_alpha_raw0", "w_alpha_raw1", "w_alpha_raw2", "w_alpha_raw3", "w_alpha_ll1", "w_alpha_ll2", "w_alpha_ll3",
    "w_alpha_unflagged", "w_extended", "w_anim_pil", "w_anim_blend", "w_anim_dispose", "w_anim_keys", "w_anim_lossy", "w_anim_once",
    "w_anim_twice", "w_anim_short" };

static const char *const hostile[] = { "bad_webp_cut", "bad_webp_riff_size", "bad_webp_version", "bad_webp_start_code", "bad_webp_huffman",
    "bad_webp_huge", "bad_webp_outside", "bad_webp_anmf_first", "bad_webp_alph_vp8l", "bad_webp_flags", "bad_webp_alpha_method",
    "bad_webp_partition", "bad_webp_still_size", "bad_webp_interframe", "bad_webp_not_vp8" };

static const char *const mutated[] = { "w_ll_photo", "w_ll_pal4", "w_lossy_photo", "w_vpx_parts", "w_alpha", "w_alpha_raw3", "w_anim_blend",
    "w_anim_lossy" };

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
    snprintf(path, sizeof path, "tests/images/%s.webp", name);
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
    uint32_t s = 3141592653u;
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
                size_t pos = (m & 1) ? s % (n < 300 ? n : 300) : s % n;
                uint32_t kind = (s >> 24) % 3;
                if (kind == 0) b[pos] = (uint8_t)(at(b, pos) ^ (1u << (s >> 8 & 7)));
                else if (kind == 1) b[pos] = (uint8_t)(s >> 16);
                else if (pos > 12) n = pos;
            }
            amb_image_info info;
            uint32_t w, h;
            amb_webp_frame(data(b), n, AMB_IMAGE_AT_REST, &rgba, &info);
            amb_image_size(data(b), n, &w, &h);
        }
    }
    printf("mutations of the fixtures: each decoded or refused\n");
}

int main(void) {
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        vector<uint8_t> file, rgba;
        amb_image_info info;
        uint32_t hw = 0, hh = 0;
        if (!read_fixture(names[i], &file)) {
            printf("%s: no fixture (python3 tests/images/gen.py writes them)\n", names[i]);
            continue;
        }
        amb_image_size(data(file), len(file), &hw, &hh);
        if (!amb_webp_frame(data(file), len(file), AMB_IMAGE_AT_REST, &rgba, &info)) {
            printf("%s not decoded\n", names[i]);
            continue;
        }
        uint32_t frames = info.frames, rest = info.frame, loops = info.loops;
        if (frames == 1) {
            printf("%s %ux%u, its header %ux%u, %016llx\n", names[i], info.w, info.h, hw, hh, (unsigned long long)fnv(&rgba));
            continue;
        }
        printf("%s %ux%u, its header %ux%u, %u frames, plays %u, at rest frame %u\n", names[i], info.w, info.h, hw, hh, frames, loops, rest);
        for (uint32_t k = 0; k < frames; k++) {
            if (!amb_webp_frame(data(file), len(file), k, &rgba, &info)) printf("  frame %u not decoded\n", k);
            else printf("  frame %u %016llx\n", k, (unsigned long long)fnv(&rgba));
        }
    }
    for (size_t i = 0; i < sizeof hostile / sizeof hostile[0]; i++) {
        vector<uint8_t> file, rgba;
        amb_image_info info;
        uint32_t hw = 0, hh = 0;
        if (!read_fixture(hostile[i], &file)) {
            printf("%s: no fixture\n", hostile[i]);
            continue;
        }
        int sized = amb_image_size(data(file), len(file), &hw, &hh);
        if (amb_webp_frame(data(file), len(file), AMB_IMAGE_AT_REST, &rgba, &info)) printf("%s: decoded %ux%u\n", hostile[i], info.w, info.h);
        else if (sized) printf("%s: refused, its header %ux%u\n", hostile[i], hw, hh);
        else printf("%s: refused, no header\n", hostile[i]);
    }
    mutations();
    return 0;
}
