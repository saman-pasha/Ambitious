/* tests/exec/jpeg_decode.c -- the JPEG decoder (src/paint/image.c) against Pillow's
   decoding of the same files, which tests/images/gen.py writes beside them as
   NAME.pil.png: every channel of every pixel within 4 of Pillow's, and the mean error
   under 1 -- the bound for two decoders whose inverse DCTs may round apart. This one's
   arithmetic is libjpeg's throughout, as Pillow's is, and it meets the bound exactly:
   how many fixtures are exact is pinned too, so a change that rounds apart is seen.
   The size read from the header alone (amb_image_size) must be the decoded one, its
   EXIF orientation applied. Then the hostile files, each refused or decoded to a prefix
   as gen.py names them, and mutations of the fixtures, which may decode to anything
   but must not crash (the gate runs this under valgrind).

   -v adds each fixture's errors; --time FILE [N] decodes FILE N times (10) and prints
   the time of one decode. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <vector.pl>
#include "../../src/paint/image.h"

static const char *const names[] = {
    "j_444", "j_422", "j_420", "j_420_5x3", "j_420_3x3", "j_1x1", "j_q100", "j_q100_420", "j_q5", "j_optimized", "j_prog_420", "j_prog_444",
    "j_prog_422", "j_restart_blocks", "j_restart_rows", "j_prog_restart", "j_grey", "j_grey_prog", "j_cmyk", "j_cmyk_prog", "j_adobe_rgb",
    "j_photo", "j_exif1", "j_exif2", "j_exif3", "j_exif4", "j_exif5", "j_exif6", "j_exif7", "j_exif8", "j_no_tables",
    "jo_411", "jo_440", "jo_31", "jo_c21", "jo_scans", "jo_restart7", "jo_q16", "jo_ids0", "jo_rgb_ids", "jo_grey_22", "jo_ycck", "jo_cmyk_sub" };

static const char *const hostile[] = {
    "bad_cut_header", "bad_cut_scan", "bad_no_end", "bad_cut_progressive", "bad_sof_length", "bad_dqt_length", "bad_huge", "bad_past_cap",
    "bad_no_height", "bad_12_bit", "bad_arithmetic", "bad_lossless", "bad_sampling", "bad_huffman_oversubscribed", "bad_huffman_count",
    "bad_huffman_dc_symbol", "bad_scan_component", "bad_mcu_blocks", "bad_restart_missing", "bad_progression", "bad_not_jpeg",
    "bad_tables_only" };

/* the fixtures the mutations start from: each kind of frame and scan */
static const char *const mutated[] = { "j_420", "j_prog_420", "j_restart_blocks", "j_prog_restart", "j_cmyk_prog", "j_grey_prog", "j_exif6",
    "jo_scans", "jo_ycck", "jo_411" };

static int read_file(const char *path, vector<uint8_t> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static int read_fixture(const char *name, const char *ext, vector<uint8_t> *out) {
    char path[256];
    snprintf(path, sizeof path, "tests/images/%s%s", name, ext);
    return read_file(path, out);
}

/* one fixture against Pillow's decoding of it; 1 when they are the same pixels */
static int compare(const char *name, int verbose) {
    vector<uint8_t> file, pfile, ref, rgba;
    uint32_t rw, rh, hw, hh;
    amb_image_info info;
    if (!read_fixture(name, ".jpg", &file) || !read_fixture(name, ".pil.png", &pfile)) {
        printf("%s: no fixture (python3 tests/images/gen.py writes them)\n", name);
        return 0;
    }
    if (!amb_png_decode(data(pfile), len(pfile), &ref, &rw, &rh)) {
        printf("%s: Pillow's decoding unreadable\n", name);
        return 0;
    }
    if (!amb_jpeg_decode(data(file), len(file), &rgba, &info)) {
        printf("%s not decoded\n", name);
        return 0;
    }
    amb_image_size(data(file), len(file), &hw, &hh);
    if (info.w != rw || info.h != rh) {
        printf("%s %ux%u, but Pillow's is %ux%u\n", name, info.w, info.h, rw, rh);
        return 0;
    }
    int worst = 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < (size_t)rw * rh; i++)
        for (size_t k = 0; k < 3; k++) {
            int e = abs((int)at(rgba, 4 * i + k) - (int)at(ref, 4 * i + k));
            if (e > worst) worst = e;
            sum += (uint64_t)e;
        }
    double mean = (double)sum / (3.0 * rw * rh);
    int ok = worst <= 4 && mean < 1.0 && !info.partial;
    printf("%s %ux%u, its header %ux%u: %s", name, info.w, info.h, hw, hh, ok ? "within the bound" : "OUTSIDE THE BOUND");
    if (!ok || verbose) printf(" (largest error %d, mean %.4f%s)", worst, mean, info.partial ? ", partial" : "");
    printf("\n");
    return ok && worst == 0;
}

static void judge(const char *name) {
    vector<uint8_t> file, rgba;
    amb_image_info info;
    if (!read_fixture(name, ".jpg", &file)) {
        printf("%s: no fixture\n", name);
        return;
    }
    if (!amb_jpeg_decode(data(file), len(file), &rgba, &info)) printf("%s: refused\n", name);
    else if (info.partial) printf("%s: a prefix %ux%u\n", name, info.w, info.h);
    else printf("%s: whole %ux%u\n", name, info.w, info.h);
}

/* each fixture's bytes changed 40 ways, the same each run: a few bytes flipped, made
   0xFF (a marker's first byte) or anything, and the file cut short */
static void mutations(void) {
    uint32_t s = 2463534242u;
    for (size_t f = 0; f < sizeof mutated / sizeof mutated[0]; f++) {
        vector<uint8_t> file;
        if (!read_fixture(mutated[f], ".jpg", &file) || len(file) < 8) continue;
        for (int m = 0; m < 40; m++) {
            vector<uint8_t> b, rgba;
            append_range(b, data(file), len(file));
            size_t n = len(b);
            int changes = 1 + m % 4;
            for (int c = 0; c < changes; c++) {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                size_t pos = (m & 1 ? s % (n < 600 ? n : 600) : s % n);
                uint32_t kind = (s >> 24) % 4;
                if (kind == 0) b[pos] = (uint8_t)(at(b, pos) ^ (1u << (s >> 8 & 7)));
                else if (kind == 1) b[pos] = 0xFF;
                else if (kind == 2) b[pos] = (uint8_t)(s >> 16);
                else if (pos > 2) n = pos;
            }
            amb_image_info info;
            amb_jpeg_decode(data(b), n, &rgba, &info);
        }
    }
    printf("mutations of the fixtures: each decoded or refused\n");
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--time") == 0) {
        vector<uint8_t> file, rgba;
        if (!read_file(argv[2], &file)) return 1;
        int reps = argc > 3 ? atoi(argv[3]) : 10;
        amb_image_info info;
        memset(&info, 0, sizeof info);
        double t0 = now_ms();
        for (int r = 0; r < reps; r++) amb_jpeg_decode(data(file), len(file), &rgba, &info);
        double t = (now_ms() - t0) / (reps > 0 ? reps : 1);
        printf("%s: %ux%u (%.2f megapixels), %.2f ms a decode\n", argv[2], info.w, info.h, (double)info.w * info.h / 1e6, t);
        return 0;
    }
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    size_t exact = 0, count = sizeof names / sizeof names[0];
    for (size_t i = 0; i < count; i++) exact += (size_t)compare(names[i], verbose);
    printf("exact, pixel for pixel: %zu of %zu\n", exact, count);
    for (size_t i = 0; i < sizeof hostile / sizeof hostile[0]; i++) judge(hostile[i]);
    mutations();
    return 0;
}
