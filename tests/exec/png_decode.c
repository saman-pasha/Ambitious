/* tests/exec/png_decode.c -- the PNG decoder (src/paint/image.c) against fixtures
   whose pixels are known without it (tests/images/gen.py writes them, and what they
   must decode to); then a round trip through the painter's own PNG writer. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>
#include "../../src/paint/image.h"

amb_interface(dom);
amb_interface(layout);
amb_interface(net);
#include "../../src/paint/paint.h"

static const char *const names[] = { "gray1", "gray2_adam7", "gray4_key", "gray8_stored", "gray16", "rgb8_adam7", "rgb8_key", "rgb16",
    "pal1", "pal2_trns", "pal4", "pal8_adam7", "graya8", "graya16", "rgba8", "rgba16_adam7", "one_pixel_adam7", "pillow_rgba", "pillow_pal" };

static uint64_t fnv(const vector<uint8_t> *b) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len(*b); i++) { h ^= at(*b, i); h *= 1099511628211ull; }
    return h;
}

static int read_file(const char *path, vector<uint8_t> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

int main(void) {
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[256];
        snprintf(path, sizeof path, "tests/images/%s.png", names[i]);
        vector<uint8_t> file, rgba;
        uint32_t w, h;
        if (!read_file(path, &file) || !amb_png_decode(data(file), len(file), &rgba, &w, &h)) { printf("%s not decoded\n", names[i]); continue; }
        printf("%s %ux%u %016llx\n", names[i], w, h, (unsigned long long)fnv(&rgba));
    }
    /* a canvas written by the painter and read back */
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    c.w = 37;
    c.h = 19;
    resize(&c.px, (size_t)c.w * c.h * 3);
    for (size_t i = 0; i < len(c.px); i++) c.px[i] = (uint8_t)(i * 7 % 251 < 120 ? i % 256 : 255 - i % 200);
    own FILE *f = tmpfile();
    if (!f) return 1;
    defer(f) { fclose(f); }
    amb_png_write(&c, f);
    rewind(f);
    vector<uint8_t> file, rgba;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(file, buf, n);
    uint32_t w, h;
    int same = amb_png_decode(data(file), len(file), &rgba, &w, &h) && w == c.w && h == c.h;
    for (size_t p = 0; same && p < (size_t)w * h; p++)
        if (at(rgba, 4 * p) != at(c.px, 3 * p) || at(rgba, 4 * p + 1) != at(c.px, 3 * p + 1) || at(rgba, 4 * p + 2) != at(c.px, 3 * p + 2) ||
            at(rgba, 4 * p + 3) != 255) same = 0;
    printf("round trip through the painter's writer: %s\n", same ? "the same pixels" : "DIFFERENT");
    return 0;
}
