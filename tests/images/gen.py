#!/usr/bin/env python3
"""tests/images/gen.py -- the PNG decoder's fixtures, and what each decodes to.

Each fixture is written by this script's own encoder (over Python's zlib), from
pixels it computes, so what the C decoder (src/paint/image.c) must give back is
known without it: each colour type and bit depth, all five filters, Adam7,
stored and compressed streams, IDAT split in two, tRNS. Two more are written by
Pillow, a third party's encoder, and checked against Pillow's own decoding.
Writes tests/images/*.png and prints the expectation: name, size, FNV-1a 64 of
the RGBA8 pixels, as tests/exec/png_decode.c prints them."""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


def pack_row(samples, depth):
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out, acc, nb = bytearray(), 0, 0
    for s in samples:
        acc, nb = (acc << depth) | s, nb + depth
        if nb == 8:
            out.append(acc)
            acc, nb = 0, 0
    if nb:
        out.append(acc << (8 - nb))
    return bytes(out)


def filtered(rows, bpp, filters):
    out, prev = bytearray(), bytes(len(rows[0]))
    for i, r in enumerate(rows):
        ft = filters[i % len(filters)]
        out.append(ft)
        for x in range(len(r)):
            a = r[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 0:
                p = 0
            elif ft == 1:
                p = a
            elif ft == 2:
                p = b
            elif ft == 3:
                p = (a + b) // 2
            else:
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            out.append((r[x] - p) & 0xFF)
        prev = r
    return bytes(out)


def encode(w, h, depth, ctype, px, palette=None, trns=None, interlace=False, level=9, filters=(0, 1, 2, 3, 4)):
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, ch * depth // 8)
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)] if interlace else [(0, 0, 1, 1)]
    raw = b""
    for (x0, y0, dx, dy) in passes:
        xs, ys = list(range(x0, w, dx)), list(range(y0, h, dy))
        if xs and ys:
            raw += filtered([pack_row([s for x in xs for s in px(x, y)], depth) for y in ys], bpp, filters)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 1 if interlace else 0))
    if palette:
        out += chunk(b"PLTE", bytes(c for rgb in palette for c in rgb))
    if trns is not None:
        out += chunk(b"tRNS", trns)
    z = zlib.compress(raw, level)
    out += chunk(b"IDAT", z[: len(z) // 2]) + chunk(b"IDAT", z[len(z) // 2:]) + chunk(b"IEND", b"")
    return out


def rgba_of(w, h, depth, ctype, px, palette=None, trns=None):
    """the RGBA8 pixels the decoder must give: 16-bit samples by their high byte"""
    out = bytearray()
    maxv = (1 << depth) - 1
    key = None
    if trns is not None and ctype == 0:
        key = struct.unpack(">H", trns[:2])
    if trns is not None and ctype == 2:
        key = struct.unpack(">HHH", trns[:6])
    for y in range(h):
        for x in range(w):
            s = px(x, y)
            if ctype == 3:
                r, g, b = palette[s[0]]
                a = trns[s[0]] if trns is not None and s[0] < len(trns) else 255
            elif ctype in (0, 4):
                v = s[0] * 255 // maxv if depth < 8 else (s[0] >> 8 if depth == 16 else s[0])
                r = g = b = v
                a = 255
                if ctype == 0 and key is not None and s[0] == key[0]:
                    a = 0
                if ctype == 4:
                    a = s[1] >> 8 if depth == 16 else s[1]
            else:
                r, g, b = (c >> 8 for c in s[:3]) if depth == 16 else s[:3]
                a = 255
                if ctype == 2 and key is not None and tuple(s[:3]) == key:
                    a = 0
                if ctype == 6:
                    a = s[3] >> 8 if depth == 16 else s[3]
            out += bytes((r, g, b, a))
    return bytes(out)


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


PAL = [((i * 53) % 256, (i * 97 + 31) % 256, (255 - i * 13) % 256) for i in range(256)]

CASES = [
    ("gray1", 13, 7, 1, 0, lambda x, y: ((x ^ y) & 1,), {}),
    ("gray2_adam7", 17, 11, 2, 0, lambda x, y: ((x + 2 * y) % 4,), {"interlace": True}),
    ("gray4_key", 9, 9, 4, 0, lambda x, y: ((x * y) % 16,), {"trns": struct.pack(">H", 6)}),
    ("gray8_stored", 31, 5, 8, 0, lambda x, y: ((x * 8 + y * 3) % 256,), {"level": 0}),
    ("gray16", 7, 6, 16, 0, lambda x, y: ((x * 9001 + y * 333) % 65536,), {}),
    ("rgb8_adam7", 21, 13, 8, 2, lambda x, y: (x * 12 % 256, y * 19 % 256, (x * y) % 256), {"interlace": True}),
    ("rgb8_key", 8, 8, 8, 2, lambda x, y: (x * 32, y * 32, 0), {"trns": struct.pack(">HHH", 64, 96, 0)}),
    ("rgb16", 5, 5, 16, 2, lambda x, y: (x * 13107, y * 13107, 40000), {}),
    ("pal1", 10, 3, 1, 3, lambda x, y: ((x + y) % 2,), {"palette": PAL[:2]}),
    ("pal2_trns", 11, 4, 2, 3, lambda x, y: ((x + y) % 4,), {"palette": PAL[:4], "trns": bytes([0, 128])}),
    ("pal4", 15, 6, 4, 3, lambda x, y: ((x * 3 + y) % 16,), {"palette": PAL[:16]}),
    ("pal8_adam7", 19, 17, 8, 3, lambda x, y: ((x * 7 + y * 11) % 256,), {"palette": PAL, "interlace": True, "trns": bytes(range(0, 256, 3))}),
    ("graya8", 9, 4, 8, 4, lambda x, y: (x * 28, y * 60), {}),
    ("graya16", 4, 4, 16, 4, lambda x, y: (x * 16384, 65535 - y * 16384), {}),
    ("rgba8", 16, 9, 8, 6, lambda x, y: (x * 16, y * 28, 200, (x + y) * 10 % 256), {"filters": (4, 3, 1)}),
    ("rgba16_adam7", 10, 10, 16, 6, lambda x, y: (x * 6553, y * 6553, 30000, 65535 - x * y * 600), {"interlace": True}),
    ("one_pixel_adam7", 1, 1, 8, 2, lambda x, y: (1, 2, 3), {"interlace": True}),
]


def main():
    lines = []
    for name, w, h, depth, ctype, px, opts in CASES:
        enc = dict(opts)
        with open(os.path.join(HERE, name + ".png"), "wb") as f:
            f.write(encode(w, h, depth, ctype, px, **enc))
        want = rgba_of(w, h, depth, ctype, px, opts.get("palette"), opts.get("trns"))
        lines.append("%s %dx%d %016x" % (name, w, h, fnv(want)))
    # two by Pillow, a third party's encoder, checked against Pillow's own decoding
    from PIL import Image
    im = Image.new("RGBA", (24, 12))
    im.putdata([((x * 10) % 256, (y * 20) % 256, (x * y) % 256, 255 - x * 5) for y in range(12) for x in range(24)])
    im.save(os.path.join(HERE, "pillow_rgba.png"), optimize=True)
    pal = Image.new("P", (20, 10))
    pal.putpalette([c for i in range(16) for c in PAL[i]])
    pal.putdata([(x + y) % 16 for y in range(10) for x in range(20)])
    pal.save(os.path.join(HERE, "pillow_pal.png"), optimize=True)
    for name in ("pillow_rgba", "pillow_pal"):
        got = Image.open(os.path.join(HERE, name + ".png")).convert("RGBA")
        lines.append("%s %dx%d %016x" % (name, got.width, got.height, fnv(got.tobytes())))
    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
