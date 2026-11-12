#!/usr/bin/env python3
"""tests/images/gen.py -- the image decoders' fixtures, and what each decodes to.

PNG. Each fixture is written by this script's own encoder (over Python's zlib),
from pixels it computes, so what the C decoder (src/paint/image.c) must give back
is known without it: each colour type and bit depth, all five filters, Adam7,
stored and compressed streams, IDAT split in two, tRNS. Two more are written by
Pillow, a third party's encoder, and checked against Pillow's own decoding. The
expectation (name, size, FNV-1a 64 of the RGBA8 pixels, as tests/exec/png_decode.c
prints them) goes to expected.txt.

GIF. Written by Pillow's encoder and by this script's own: a first frame with a
local table and offset on its canvas, interlacing at heights 1 to 17, each code
size, a full LZW table kept without a clear and one cleared, clear codes
mid-stream, none to begin, no colour table at all, indices past the table,
animation: every disposal method, transparency over what the last frame left,
frames past the canvas, loop counts and delays. Each frame is checked exactly, the
FNV-1a 64 of its RGBA8 canvas in tests/exec/gif_decode.expect, against a model of
how Chromium composes frames over Pillow's decoding of each frame alone. The model
was measured, frame by frame, through Chromium's WebCodecs ImageDecoder: the
canvas is the logical screen and the first frame together, transparent where no
frame has drawn; disposal 2 clears a frame's rectangle to transparent (not to the
background colour Pillow fills), 3 and 4 restore it as it was before the frame;
a file with no colour table at all is drawn black, where Pillow has a grey ramp.
The frame an animation shows at rest is Chromium's timeline at 10 s (rest_frame).

JPEG. Written by Pillow (libjpeg-turbo) -- each subsampling it writes,
progressive, restart intervals, grey, CMYK, Adobe's RGB, optimized tables, the
qualities' extremes, every EXIF orientation, no Huffman tables at all -- and by
this script's own baseline encoder for what Pillow cannot write: 4:1:1, 4:4:0,
3:1, chroma 2x1 under 2x2, non-interleaved scans, YCCK, subsampled CMYK, 16-bit
quantization, ids that name no colour space, a lone component sampled 2x2.
Pillow's decoding of each (EXIF's orientation applied by ImageOps.exif_transpose,
CMYK made RGB by its convert) is written beside it as NAME.pil.png, which
tests/exec/jpeg_decode.c compares with its own: every channel within 4, the mean
error under 1. tests/exec/jpeg_decode.expect names each with Pillow's size.
The hostile files (bad_*): truncated, wrong lengths, sizes past the cap, bad
Huffman tables, LZW codes out of range, each refused or decoded to a prefix as
the .expect files say; the tests add mutations of their own.

Run from anywhere: python3 tests/images/gen.py"""
import io
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
EXEC = os.path.join(os.path.dirname(HERE), "exec")


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


def png_fixtures():
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
    return lines


# ---- the pictures the GIF and JPEG fixtures hold ----

class Lcg:
    """a reproducible stream of numbers, the same on every Python"""

    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s >> 8


def photo(w, h, seed=1):
    """RGB rows with what a photograph has: gradients, a disc, hard edges, noise"""
    rng = Lcg(seed)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r = x * 255 // max(1, w - 1)
            g = y * 255 // max(1, h - 1)
            b = 28 + ((x + 2 * y) * 9) % 200
            if 9 * ((3 * x - w) ** 2) + 9 * ((2 * y - h) ** 2) < 4 * min(w, h) ** 2:
                r, g, b = 250 - g // 2, 40 + r // 3, 60
            if (x // 6 + y // 6) % 5 == 0:
                r, g, b = 255 - r, 255 - g, 255 - b
            n = rng.next() % 41 - 20
            row.append(tuple(max(0, min(255, c + n)) for c in (r, g, b)))
        rows.append(row)
    return rows


def checker(w, h):
    """hard 1-pixel detail with full contrast: the coefficients' extremes"""
    return [[(255, 255, 255) if (x + y) % 2 else (0, 0, 0) if (x // 3) % 2 else (255, 0, 0) for x in range(w)] for y in range(h)]


def pil_rgb(rows):
    from PIL import Image
    im = Image.new("RGB", (len(rows[0]), len(rows)))
    im.frombytes(bytes(c for row in rows for px in row for c in px))
    return im


# ---- GIF: this script's own encoder ----

def lzw(indices, mcs, clear_first=True, reset_full=True, clear_every=0):
    """indices as GIF's LZW codes, least significant bit first: each code as wide as the
    decoder's table then needs; when the table fills, a clear (reset_full) or the table
    kept as it is; a clear after every clear_every codes"""
    clr, eoi = 1 << mcs, (1 << mcs) + 1
    out = bytearray()
    state = {"acc": 0, "nb": 0}
    table, nxt = {}, clr + 2

    def put(code):
        size = max(mcs + 1, min(12, (nxt - 1).bit_length()))
        state["acc"] |= code << state["nb"]
        state["nb"] += size
        while state["nb"] >= 8:
            out.append(state["acc"] & 0xFF)
            state["acc"] >>= 8
            state["nb"] -= 8

    if clear_first:
        put(clr)
    cur, emitted = None, 0
    for c in indices:
        if cur is None:
            cur = c
            continue
        if (cur, c) in table:
            cur = table[(cur, c)]
            continue
        put(cur)
        emitted += 1
        if nxt < 4096:
            table[(cur, c)] = nxt
            nxt += 1
        elif reset_full:
            put(clr)
            table, nxt = {}, clr + 2
        if clear_every and emitted % clear_every == 0:
            put(clr)
            table, nxt = {}, clr + 2
        cur = c
    if cur is not None:
        put(cur)
    put(eoi)
    if state["nb"]:
        out.append(state["acc"] & 0xFF)
    return bytes(out)


def colour_table(colours):
    """a GIF colour table: its size's field and its bytes, padded to a power of two"""
    bits = max(1, (len(colours) - 1).bit_length())
    return bits - 1, bytes(c for rgb in colours for c in rgb) + bytes(3 * ((1 << bits) - len(colours)))


def interlaced(rows):
    return [rows[y] for y in list(range(0, len(rows), 8)) + list(range(4, len(rows), 8)) + list(range(2, len(rows), 4)) + list(range(1, len(rows), 2))]


def gif(w, h, frames, gct=None, version=b"89a", loop=None):
    """a GIF of the frames: each (x, y, rows of indices, options): lct, trans, interlace,
    mcs, delay, disposal, and the LZW encoder's options; a NETSCAPE2.0 block of the loop
    count unless loop is None"""
    out = bytearray(b"GIF" + version)
    flags = 0
    table = b""
    if gct is not None:
        sz, table = colour_table(gct)
        flags = 0x80 | 0x70 | sz
    out += struct.pack("<HHBBB", w, h, flags, 0, 0)
    out += table
    if loop is not None:
        out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + b"\x00"
    for (x, y, rows, opt) in frames:
        trans = opt.get("trans")
        if trans is not None or opt.get("delay") or opt.get("disposal"):
            gce = opt.get("disposal", 0) << 2 | (1 if trans is not None else 0)
            out += bytes([0x21, 0xF9, 4, gce]) + struct.pack("<H", opt.get("delay", 0)) + bytes([trans or 0, 0])
        fl, lct = 0, b""
        if opt.get("lct") is not None:
            sz, lct = colour_table(opt["lct"])
            fl = 0x80 | sz
        if opt.get("interlace"):
            fl |= 0x40
        out += bytes([0x2C]) + struct.pack("<HHHHB", x, y, len(rows[0]) if rows else 0, len(rows), fl) + lct
        coded = interlaced(rows) if opt.get("interlace") else rows
        mcs = opt.get("mcs", 8)
        d = lzw([v for row in coded for v in row], mcs, opt.get("clear_first", True), opt.get("reset_full", True), opt.get("clear_every", 0))
        out.append(mcs)
        for i in range(0, len(d), 255):
            out.append(len(d[i:i + 255]))
            out += d[i:i + 255]
        out.append(0)
    out.append(0x3B)
    return bytes(out)


def ramp(n, seed):
    return [((i * 37 + seed * 11) % 256, (i * 91 + seed * 53) % 256, (255 - i * 29 - seed) % 256) for i in range(n)]


def indices(w, h, n, seed=0):
    rng = Lcg(seed + 7)
    return [[(x * 3 + y * 5 + (rng.next() % 3 if seed else 0)) % n for x in range(w)] for y in range(h)]


def noise_indices(w, h, n, seed):
    rng = Lcg(seed)
    return [[rng.next() % n for _ in range(w)] for _ in range(h)]


def gif_cases():
    """(name, bytes, the first frame's box or None for the whole canvas)"""
    cases = []
    # a first frame of its own table, offset on a canvas with no global one, a transparent index
    cases.append(("g_local_first", gif(20, 15, [(3, 2, indices(11, 9, 8), {"lct": ramp(8, 1), "trans": 5, "mcs": 3})]), (3, 2, 11, 9)))
    # a frame smaller than its canvas, opaque: outside it, transparent
    cases.append(("g_offset_opaque", gif(16, 12, [(2, 3, indices(9, 5, 4), {"mcs": 2})], gct=ramp(4, 2)), (2, 3, 9, 5)))
    for hh in (1, 2, 3, 5, 9, 17):
        cases.append(("g_interlace_%d" % hh, gif(7, hh, [(0, 0, indices(7, hh, 16, 1), {"interlace": True, "mcs": 4})], gct=ramp(16, 3)), None))
    for mcs in range(2, 9):
        n = 1 << mcs
        cases.append(("g_mcs%d" % mcs, gif(13, 11, [(0, 0, indices(13, 11, n, mcs), {"mcs": mcs})], gct=ramp(n, mcs)), None))
    big = noise_indices(128, 96, 256, 5)
    cases.append(("g_full_kept", gif(128, 96, [(0, 0, big, {"reset_full": False})], gct=ramp(256, 4)), None))
    cases.append(("g_full_cleared", gif(128, 96, [(0, 0, big, {"reset_full": True})], gct=ramp(256, 4)), None))
    cases.append(("g_clears", gif(40, 30, [(0, 0, indices(40, 30, 32, 2), {"mcs": 5, "clear_every": 37})], gct=ramp(32, 5)), None))
    cases.append(("g_no_first_clear", gif(25, 9, [(0, 0, indices(25, 9, 16, 3), {"mcs": 4, "clear_first": False})], gct=ramp(16, 6)), None))
    cases.append(("g_no_table", gif(6, 5, [(0, 0, indices(6, 5, 8), {"mcs": 3})]), None))
    cases.append(("g_past_table", gif(9, 4, [(0, 0, indices(9, 4, 8), {"mcs": 3})], gct=ramp(4, 7)), None))
    cases.append(("g_clipped", gif(10, 8, [(6, 5, indices(8, 6, 4), {"mcs": 2, "trans": 1})], gct=ramp(4, 8)), (6, 5, 8, 6)))
    cases.append(("g_one_pixel", gif(1, 1, [(0, 0, [[1]], {"mcs": 2})], gct=ramp(2, 9)), None))
    cases.append(("g_87a", gif(12, 7, [(0, 0, indices(12, 7, 4), {"mcs": 2})], gct=ramp(4, 10), version=b"87a"), None))
    frames = [(0, 0, indices(18, 14, 16, 4), {"trans": 2, "delay": 10, "mcs": 4, "lct": ramp(16, 11)}),
              (4, 4, indices(6, 6, 16, 5), {"lct": ramp(16, 12), "mcs": 4}),
              (0, 0, indices(18, 14, 16, 6), {"interlace": True, "mcs": 4})]
    cases.append(("g_animated", gif(18, 14, frames, gct=ramp(16, 13)), None))
    # by Pillow's encoder: interlaced as it writes them, a transparent index, an animation whose
    # frames carry their own tables, a photograph quantized to 256 colours, and one not interlaced
    from PIL import Image
    pal = Image.new("P", (40, 30))
    pal.putpalette([c for rgb in ramp(16, 14) for c in rgb])
    pal.putdata([v for row in indices(40, 30, 16, 8) for v in row])
    for name, opts in (("g_pil", {}), ("g_pil_trans", {"transparency": 3}), ("g_pil_flat", {"interlace": False})):
        path = os.path.join(HERE, name + ".gif")
        pal.save(path, **opts)
        cases.append((name, open(path, "rb").read(), None))
    seq = []
    for k in range(4):
        f = Image.new("P", (30, 20))
        f.putpalette([c for rgb in ramp(32, 20 + k) for c in rgb])
        f.putdata([v for row in indices(30, 20, 32, 10 + k) for v in row])
        seq.append(f)
    path = os.path.join(HERE, "g_pil_animated.gif")
    seq[0].save(path, save_all=True, append_images=seq[1:], duration=[50, 60, 70, 80], loop=0)
    cases.append(("g_pil_animated", open(path, "rb").read(), None))
    path = os.path.join(HERE, "g_pil_photo.gif")
    pil_rgb(photo(50, 40, 3)).save(path)
    cases.append(("g_pil_photo", open(path, "rb").read(), None))
    # animation, composed as Chromium composes it: a full first frame, then a box whose
    # centre is its transparent index with each disposal (2 clears its rectangle, 3 and 4
    # restore what it covered, 1 and 6 keep it), then two frames over what it left, the
    # last one past the canvas's edge, clipped
    box = [[0 if 1 < x < 4 and 0 < y < 4 else 1 + (x + y) % 3 for x in range(6)] for y in range(5)]
    for d in (1, 2, 3, 4, 6):
        frames = [(0, 0, indices(10, 8, 8, 30), {"mcs": 3, "delay": 50, "disposal": 1}),
                  (2, 2, box, {"mcs": 3, "trans": 0, "delay": 50, "disposal": d}),
                  (4, 1, indices(5, 6, 8, 31), {"mcs": 3, "trans": 7, "delay": 50}),
                  (7, 5, indices(6, 5, 8, 32), {"mcs": 3, "delay": 60, "disposal": 2})]
        cases.append(("g_dispose%d" % d, gif(10, 8, frames, gct=ramp(8, 30 + d), loop=0), None))
    # a first frame restored to what was before it (nothing): the next drawn on a clear canvas
    frames = [(0, 0, indices(10, 8, 8, 33), {"mcs": 3, "delay": 50, "disposal": 3}), (3, 2, box, {"mcs": 3, "trans": 0, "delay": 50})]
    cases.append(("g_first_restored", gif(10, 8, frames, gct=ramp(8, 36), loop=0), None))
    # a first frame past the logical screen: the canvas grows to hold it (Chromium's size);
    # a screen of 0 x 0 takes the first frame's
    cases.append(("g_first_past", gif(8, 6, [(4, 3, indices(8, 6, 4, 34), {"mcs": 2})], gct=ramp(4, 37)), None))
    cases.append(("g_zero_screen", gif(0, 0, [(0, 0, indices(5, 4, 4, 35), {"mcs": 2})], gct=ramp(4, 38)), None))
    # the timeline: four frames of a second, played once (no NETSCAPE2.0 block), twice
    # (a loop count of 1), three times, for ever; frames of no delay (100 ms each)
    four = [(0, 0, indices(6, 4, 4, 40 + k), {"mcs": 2, "delay": 100}) for k in range(4)]
    for name, loop in (("g_loop_none", None), ("g_loop_1", 1), ("g_loop_2", 2), ("g_loop_ever", 0)):
        cases.append((name, gif(6, 4, four, gct=ramp(4, 39), loop=loop), None))
    quick = [(0, 0, indices(6, 4, 4, 50 + k), {"mcs": 2, "delay": 1 if k % 2 else 0, "disposal": 1}) for k in range(7)]
    cases.append(("g_no_delay", gif(6, 4, quick, gct=ramp(4, 41), loop=0), None))
    return cases


def gif_parse(b):
    """a GIF's screen, global table and frames, walked as the decoder walks them: each
    frame's graphic control (its packed byte, delay and transparent index), rectangle,
    local table and LZW data; and the first loop count (None without one)"""
    sw, sh, flags = struct.unpack("<HHB", b[6:11])
    i, gct = 13, b""
    if flags & 0x80:
        gct = b[i:i + 3 * (2 << (flags & 7))]
        i += len(gct)
    frames, gce, loop = [], None, None
    while i < len(b) and b[i] != 0x3B:
        blk = b[i]
        i += 1
        if blk == 0x21:
            label, subs = b[i], []
            i += 1
            while b[i]:
                subs.append(b[i + 1:i + 1 + b[i]])
                i += 1 + b[i]
            i += 1
            if label == 0xF9:
                gce = subs[0][:4]
            if label == 0xFF and loop is None and subs[0] in (b"NETSCAPE2.0", b"ANIMEXTS1.0") and len(subs[1]) == 3 and subs[1][0] == 1:
                loop = struct.unpack("<H", subs[1][1:3])[0]
            continue
        x, y, w, h, fl = struct.unpack("<HHHHB", b[i:i + 9])
        i += 9
        lct = b[i:i + 3 * (2 << (fl & 7))] if fl & 0x80 else b""
        i += len(lct)
        start = i
        i += 1
        while b[i]:
            i += 1 + b[i]
        i += 1
        frames.append({"x": x, "y": y, "w": w, "h": h, "flags": fl, "gce": gce, "lct": lct, "data": b[start:i],
                       "disposal": gce[0] >> 2 & 7 if gce else 0, "delay": struct.unpack("<H", gce[1:3])[0] if gce else 0})
        gce = None
    return sw, sh, gct, frames, loop


def rest_frame(ms, plays, at=10000):
    """the frame Chromium shows at ms at after an animation loaded, measured by
    screenshots every 100 to 250 ms for 11 s: frame k for its duration (10 ms or less
    taken as 100 ms) after the frames before it, the frames again from the first, until
    the animation has played plays times (0: for ever), then the last frame"""
    d = [100 if v <= 10 else v for v in ms]
    if len(d) <= 1:
        return 0
    if plays and at >= sum(d) * plays:
        return len(d) - 1
    t = at % sum(d)
    for k, v in enumerate(d):
        if t < v:
            return k
        t -= v
    return len(d) - 1


def gif_frames(b):
    """the canvas after each frame, RGBA8, as Chromium composes them (measured frame by
    frame through WebCodecs' ImageDecoder): the canvas the screen and the first frame
    together, transparent; each frame's own pixels Pillow's decoding of that frame alone
    -- a GIF of it on the canvas -- its transparent index leaving what is under it, every
    index black when the file has no table at all (Pillow's is a grey ramp); before each
    frame the last one disposed of: 2 clears its rectangle to transparent, 3 and 4 restore
    it as it was before that frame, the rest leave it. And the canvas's size, the loop
    count"""
    from PIL import Image
    sw, sh, gct, frames, loop = gif_parse(b)
    f0 = frames[0]
    W, H = max(sw, f0["x"] + f0["w"]), max(sh, f0["y"] + f0["h"])
    canvas, out, saved, prev = bytearray(W * H * 4), [], None, None
    flags = 0x80 | 0x70 | ((len(gct) // 3).bit_length() - 2) if gct else 0
    for f in frames:
        if prev is not None:
            for y in range(prev["y"], min(H, prev["y"] + prev["h"])):
                for x in range(prev["x"], min(W, prev["x"] + prev["w"])):
                    k = 4 * (y * W + x)
                    if prev["disposal"] == 2:
                        canvas[k:k + 4] = bytes(4)
                    elif prev["disposal"] in (3, 4):
                        canvas[k:k + 4] = saved[k:k + 4]
        if f["disposal"] in (3, 4):
            saved = bytes(canvas)
        alone = b"GIF89a" + struct.pack("<HHBBB", W, H, flags, 0, 0) + gct
        if f["gce"] is not None:
            alone += b"\x21\xf9\x04" + bytes(f["gce"]) + b"\x00"
        alone += b"\x2c" + struct.pack("<HHHHB", f["x"], f["y"], f["w"], f["h"], f["flags"]) + f["lct"] + f["data"] + b"\x3b"
        px = Image.open(io.BytesIO(alone)).convert("RGBA")
        for y in range(f["y"], min(H, f["y"] + f["h"])):
            for x in range(f["x"], min(W, f["x"] + f["w"])):
                p = px.getpixel((x, y))
                if p[3]:
                    canvas[4 * (y * W + x):4 * (y * W + x) + 4] = bytes(p[:3]) + b"\xff" if gct or f["lct"] else b"\x00\x00\x00\xff"
        out.append(bytes(canvas))
        prev = f
    return W, H, out, [10 * f["delay"] for f in frames], loop


def gif_expectation(name, b, box):
    """the lines tests/exec/gif_decode.c prints of a GIF: its canvas, its frames, the
    first's pixels; of an animation, each later frame's, the frame it shows at rest and
    how often it plays (Chromium's count: once without a loop count, its loop count plus
    one, 0 for ever)"""
    w, h, frames, ms, loop = gif_frames(b)
    n = len(frames)
    lines = ["%s %dx%d, %d frame%s, %016x" % (name, w, h, n, "" if n == 1 else "s", fnv(frames[0]))]
    if n > 1:
        plays = 1 if loop is None else 0 if loop == 0 else loop + 1
        lines += ["  frame %d %016x" % (k, fnv(frames[k])) for k in range(1, n)]
        lines.append("  at rest frame %d, plays %d" % (rest_frame(ms, plays), plays))
    return "\n".join(lines)


def gif_hostile(cases):
    """(name, bytes, what the decoder must make of them)"""
    by = dict((n, b) for n, b, _ in cases)
    good = by["g_mcs8"]
    out = [("bad_gif_cut_header", good[:11], "refused"),
           ("bad_gif_cut_table", good[:400], "refused"),
           ("bad_gif_cut_lzw", good[:len(good) - 60], "a prefix 13x11")]
    huge = bytearray(good)
    huge[6:10] = struct.pack("<HH", 65535, 65535)
    out.append(("bad_gif_huge_screen", bytes(huge), "refused"))
    # a frame past the cap, on a canvas of 13x11
    i = good.index(b"\x2c", 13 + 768)
    frame = bytearray(good)
    frame[i + 5:i + 9] = struct.pack("<HH", 65535, 65535)
    out.append(("bad_gif_huge_frame", bytes(frame), "refused"))
    # the code size 12, which leaves no room for codes
    mcs = bytearray(good)
    mcs[i + 10] = 12
    out.append(("bad_gif_code_size", bytes(mcs), "refused"))
    # a code past the table: right after the clear, the code 4095
    clr = 1 << 4
    d = bytearray()
    acc, nb = clr, 5
    for code, size in ((3, 5), (1, 5), (31, 5)):
        acc |= code << nb
        nb += size
    while nb > 0:
        d.append(acc & 0xFF)
        acc >>= 8
        nb -= 8
    b = bytearray(b"GIF89a" + struct.pack("<HHBBB", 4, 4, 0xF3, 0, 0)) + bytes(48)
    b += bytes([0x2C]) + struct.pack("<HHHHB", 0, 0, 4, 4, 0) + bytes([4, len(d)]) + d + b"\x00\x3b"
    out.append(("bad_gif_code_range", bytes(b), "a prefix 4x4"))
    out.append(("bad_gif_no_image", b"GIF89a" + struct.pack("<HHBBB", 4, 4, 0, 0, 0) + b"\x3b", "refused"))
    out.append(("bad_gif_zero_screen", b"GIF89a" + struct.pack("<HHBBB", 0, 4, 0, 0, 0) + b"\x3b", "refused"))
    ext = bytearray(good[:13 + 768]) + b"\x21\xf9\x04\x01\x00"
    out.append(("bad_gif_cut_extension", bytes(ext), "refused"))
    return out


# ---- JPEG: this script's own baseline encoder ----

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
          35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]

# T.81's example quantization (K.1), halved: luminance and chrominance, natural order
Q_LUMA = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
          18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
Q_CHROMA = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99] + [99] * 32
Q8 = [[max(1, v // 2) for v in Q_LUMA], [max(1, v // 2) for v in Q_CHROMA]]
# 16-bit steps (steps past 255): coarse at the high frequencies
Q16 = [[v * (1 + (i // 8 + i % 8) // 3) for i, v in enumerate(Q_LUMA)], [v * 3 for v in Q_CHROMA]]

_COS = None


def fdct(b):
    """the forward DCT of an 8x8 block (row by row), as T.81 A.3.3 defines it"""
    global _COS
    import math
    if _COS is None:
        _COS = [[math.cos((2 * x + 1) * u * math.pi / 16) * (math.sqrt(0.5) if u == 0 else 1) / 2 for x in range(8)] for u in range(8)]
    tmp = [sum(b[y * 8 + x] * _COS[u][x] for x in range(8)) for y in range(8) for u in range(8)]
    return [sum(tmp[y * 8 + u] * _COS[v][y] for y in range(8)) for v in range(8) for u in range(8)]


def std_tables():
    """the standard Huffman tables, as libjpeg writes them for Pillow: (class, slot, counts, symbols)"""
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (8, 8)).save(buf, "JPEG", optimize=False)
    b, i, tables = buf.getvalue(), 2, []
    while i < len(b):
        m, ln = b[i + 1], struct.unpack(">H", b[i + 2:i + 4])[0]
        if m == 0xC4:
            d, k = b[i + 4:i + 2 + ln], 0
            while k < len(d):
                counts = list(d[k + 1:k + 17])
                tables.append((d[k] >> 4, d[k] & 15, counts, list(d[k + 17:k + 17 + sum(counts)])))
                k += 17 + sum(counts)
        if m == 0xDA:
            break
        i += 2 + ln
    return tables


class BitWriter:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, ln):
        for i in range(ln - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                if self.acc == 0xFF:
                    self.out.append(0)
                self.acc, self.n = 0, 0

    def pad(self):
        while self.n:
            self.put(1, 1)


def codes_of(counts, symbols):
    codes, code, k = {}, 0, 0
    for ln in range(1, 17):
        for _ in range(counts[ln - 1]):
            codes[symbols[k]] = (code, ln)
            code += 1
            k += 1
        code <<= 1
    return codes


def segment(marker, body):
    return bytes([0xFF, marker]) + struct.pack(">H", len(body) + 2) + body


def jpeg(w, h, planes, comps, q16=False, ri=0, separate=False, jfif=True, adobe=None):
    """a baseline (or, with 16-bit steps, extended) JPEG of full-size planes (each h rows of
    w samples, 0..255): comps are (id, h, v, quantization table, Huffman slot); each
    component downsampled by the mean of its box, padded by repeating its edge"""
    hmax, vmax = max(c[1] for c in comps), max(c[2] for c in comps)
    mcux, mcuy = -(-w // (8 * hmax)), -(-h // (8 * vmax))
    qt = Q16 if q16 else Q8
    blocks = []   # per component: rows of blocks, each 64 quantized coefficients in zigzag order
    sizes = []
    for (cid, ch, cv, tq, slot), plane in zip(comps, planes):
        rx, ry = hmax // ch, vmax // cv
        cw, chh = -(-w * ch // hmax), -(-h * cv // vmax)
        sizes.append((cw, chh))
        small = [[sum(plane[yy][xx] for yy in range(y * ry, min(h, y * ry + ry)) for xx in range(x * rx, min(w, x * rx + rx))) /
                  ((min(h, y * ry + ry) - y * ry) * (min(w, x * rx + rx) - x * rx)) for x in range(cw)] for y in range(chh)]
        bw, bh = mcux * ch, mcuy * cv
        rows = []
        for by in range(bh):
            row = []
            for bx in range(bw):
                blk = [small[min(chh - 1, by * 8 + y)][min(cw - 1, bx * 8 + x)] - 128 for y in range(8) for x in range(8)]
                f = fdct(blk)
                row.append([int((f[ZIGZAG[k]] / qt[tq][ZIGZAG[k]]) + (0.5 if f[ZIGZAG[k]] >= 0 else -0.5)) for k in range(64)])
            rows.append(row)
        blocks.append(rows)
    tabs = std_tables()
    codes = {(c, s): codes_of(n, v) for c, s, n, v in tabs}
    out = bytearray(b"\xff\xd8")
    if jfif:
        out += segment(0xE0, b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00")
    if adobe is not None:
        out += segment(0xEE, b"Adobe" + struct.pack(">HHHB", 100, 0, 0, adobe))
    for t in sorted(set(c[3] for c in comps)):
        if q16:
            out += segment(0xDB, bytes([0x10 | t]) + b"".join(struct.pack(">H", qt[t][ZIGZAG[k]]) for k in range(64)))
        else:
            out += segment(0xDB, bytes([t]) + bytes(qt[t][ZIGZAG[k]] for k in range(64)))
    out += segment(0xC1 if q16 else 0xC0, struct.pack(">BHHB", 8, h, w, len(comps)) + b"".join(bytes([c[0], c[1] << 4 | c[2], c[3]]) for c in comps))
    for c, s, n, v in tabs:
        out += segment(0xC4, bytes([c << 4 | s]) + bytes(n) + bytes(v))
    if ri:
        out += segment(0xDD, struct.pack(">H", ri))
    scans = [[i] for i in range(len(comps))] if separate else [list(range(len(comps)))]
    for scan in scans:
        out += segment(0xDA, bytes([len(scan)]) + b"".join(bytes([comps[i][0], comps[i][4] << 4 | comps[i][4]]) for i in scan) + b"\x00\x3f\x00")
        bits, pred = BitWriter(), [0] * len(comps)
        if len(scan) == 1:
            i = scan[0]
            units = [[(i, bx, by)] for by in range(-(-sizes[i][1] // 8)) for bx in range(-(-sizes[i][0] // 8))]
        else:
            units = [[(i, mx * comps[i][1] + u, my * comps[i][2] + v) for i in scan for v in range(comps[i][2]) for u in range(comps[i][1])]
                     for my in range(mcuy) for mx in range(mcux)]
        for n, unit in enumerate(units):
            if ri and n and n % ri == 0:
                bits.pad()
                bits.out += bytes([0xFF, 0xD0 + (n // ri - 1) % 8])
                pred = [0] * len(comps)
            for (i, bx, by) in unit:
                coef = blocks[i][by][bx]
                dc, ac = codes[(0, comps[i][4])], codes[(1, comps[i][4])]
                diff = coef[0] - pred[i]
                pred[i] = coef[0]
                s = abs(diff).bit_length()
                bits.put(*dc[s])
                if s:
                    bits.put(diff if diff >= 0 else diff + (1 << s) - 1, s)
                run = 0
                for k in range(1, 64):
                    v = coef[k]
                    if v == 0:
                        run += 1
                        continue
                    while run > 15:
                        bits.put(*ac[0xF0])
                        run -= 16
                    s = abs(v).bit_length()
                    bits.put(*ac[run << 4 | s])
                    bits.put(v if v >= 0 else v + (1 << s) - 1, s)
                    run = 0
                if run:
                    bits.put(*ac[0x00])
        bits.pad()
        out += bits.out
    out += b"\xff\xd9"
    return bytes(out)


def ycc(rows):
    """JFIF's YCbCr of RGB rows: three full-size planes"""
    y = [[0.299 * r + 0.587 * g + 0.114 * b for (r, g, b) in row] for row in rows]
    cb = [[-0.168736 * r - 0.331264 * g + 0.5 * b + 128 for (r, g, b) in row] for row in rows]
    cr = [[0.5 * r - 0.418688 * g - 0.081312 * b + 128 for (r, g, b) in row] for row in rows]
    return [y, cb, cr]


def channels(rows):
    return [[[px[k] for px in row] for row in rows] for k in range(3)]


def jpeg_own():
    """(name, bytes) of this script's encoder: what Pillow cannot write"""
    out = []
    P = photo
    out.append(("jo_411", jpeg(45, 21, ycc(P(45, 21, 11)), [(1, 4, 1, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)])))
    out.append(("jo_440", jpeg(31, 29, ycc(P(31, 29, 12)), [(1, 1, 2, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)])))
    out.append(("jo_31", jpeg(37, 19, ycc(P(37, 19, 13)), [(1, 3, 1, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)])))
    out.append(("jo_c21", jpeg(35, 27, ycc(P(35, 27, 14)), [(1, 2, 2, 0, 0), (2, 2, 1, 1, 1), (3, 2, 1, 1, 1)])))
    out.append(("jo_scans", jpeg(43, 29, ycc(P(43, 29, 15)), [(1, 2, 2, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)], ri=5, separate=True)))
    out.append(("jo_restart7", jpeg(53, 31, ycc(P(53, 31, 16)), [(1, 2, 1, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)], ri=7)))
    out.append(("jo_q16", jpeg(29, 23, ycc(P(29, 23, 17)), [(1, 2, 2, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1)], q16=True)))
    out.append(("jo_ids0", jpeg(30, 20, ycc(P(30, 20, 18)), [(0, 1, 1, 0, 0), (1, 1, 1, 1, 1), (2, 1, 1, 1, 1)], jfif=False)))
    out.append(("jo_rgb_ids", jpeg(30, 20, channels(P(30, 20, 19)), [(82, 1, 1, 0, 0), (71, 1, 1, 0, 0), (66, 1, 1, 0, 0)], jfif=False)))
    out.append(("jo_grey_22", jpeg(27, 21, [ycc(P(27, 21, 20))[0]], [(1, 2, 2, 0, 0)])))
    rows = P(33, 25, 21)
    k = [[(x * 7 + y * 5) % 256 for x in range(33)] for y in range(25)]
    out.append(("jo_ycck", jpeg(33, 25, ycc(rows) + [k], [(1, 2, 2, 0, 0), (2, 1, 1, 1, 1), (3, 1, 1, 1, 1), (4, 2, 2, 0, 0)],
                                jfif=False, adobe=2)))
    out.append(("jo_cmyk_sub", jpeg(33, 25, channels(rows) + [k], [(1, 1, 1, 0, 0), (2, 1, 1, 0, 0), (3, 1, 1, 0, 0), (4, 2, 2, 0, 0)],
                                    jfif=False, adobe=0)))
    return out


def exif(orientation):
    from PIL import Image
    e = Image.Exif()
    e[0x0112] = orientation
    return e.tobytes()


def jpeg_pil():
    """(name, bytes) of Pillow's encoder (libjpeg-turbo)"""
    import io
    from PIL import Image
    out = []

    def save(name, im, **opts):
        buf = io.BytesIO()
        im.save(buf, "JPEG", **opts)
        out.append((name, buf.getvalue()))

    ph = pil_rgb(photo(61, 43, 1))
    save("j_444", ph, quality=90, subsampling=0)
    save("j_422", ph, quality=90, subsampling=1)
    save("j_420", ph, quality=90, subsampling=2)
    save("j_420_5x3", pil_rgb(photo(5, 3, 2)), quality=90, subsampling=2)
    save("j_420_3x3", pil_rgb(photo(3, 3, 3)), quality=90, subsampling=2)
    save("j_1x1", pil_rgb(photo(1, 1, 4)), quality=90)
    save("j_q100", pil_rgb(checker(40, 24)), quality=100, subsampling=0)
    save("j_q100_420", pil_rgb(checker(40, 24)), quality=100, subsampling=2)
    save("j_q5", ph, quality=5, subsampling=2)
    save("j_optimized", ph, quality=85, optimize=True)
    big = pil_rgb(photo(77, 51, 5))
    save("j_prog_420", big, quality=88, progressive=True, subsampling=2)
    save("j_prog_444", big, quality=88, progressive=True, subsampling=0)
    save("j_prog_422", big, quality=70, progressive=True, subsampling=1)
    save("j_restart_blocks", big, quality=88, restart_marker_blocks=1)
    save("j_restart_rows", big, quality=88, subsampling=1, restart_marker_rows=1)
    save("j_prog_restart", big, quality=88, progressive=True, restart_marker_blocks=3)
    grey = ph.convert("L")
    save("j_grey", grey, quality=90)
    save("j_grey_prog", grey, quality=90, progressive=True)
    cmyk = ph.convert("CMYK")
    save("j_cmyk", cmyk, quality=90)
    save("j_cmyk_prog", cmyk, quality=90, progressive=True)
    save("j_adobe_rgb", ph, quality=90, keep_rgb=True)
    save("j_photo", pil_rgb(photo(160, 120, 6)), quality=85)
    o = pil_rgb(photo(37, 23, 7))
    for k in range(1, 9):
        save("j_exif%d" % k, o, quality=90, exif=exif(k))
    # the standard tables, stripped: libjpeg-turbo decodes with its own (Motion JPEG)
    buf = io.BytesIO()
    ph.save(buf, "JPEG", quality=90, optimize=False)
    b, i, kept = buf.getvalue(), 2, bytearray(b"\xff\xd8")
    while i < len(b):
        m, ln = b[i + 1], struct.unpack(">H", b[i + 2:i + 4])[0]
        if m == 0xDA:
            kept += b[i:]
            break
        if m != 0xC4:
            kept += b[i:i + 2 + ln]
        i += 2 + ln
    out.append(("j_no_tables", bytes(kept)))
    return out


def markers(b):
    """a JPEG's segments to its first scan: (marker, offset, length)"""
    i, out = 2, []
    while i + 4 <= len(b) and b[i] == 0xFF:
        m, ln = b[i + 1], struct.unpack(">H", b[i + 2:i + 4])[0]
        out.append((m, i, ln))
        if m == 0xDA:
            break
        i += 2 + ln
    return out


def jpeg_hostile(by):
    """(name, bytes, what the decoder must make of them)"""
    out = []
    good = by["j_420"]
    seg = {m: (i, ln) for m, i, ln in markers(good)}
    sos = seg[0xDA][0] + 2 + seg[0xDA][1]
    out.append(("bad_cut_header", good[:seg[0xC4][0] + 10], "refused"))
    out.append(("bad_cut_scan", good[:sos + (len(good) - sos) // 2], "a prefix 61x43"))
    out.append(("bad_no_end", good[:-2], "a prefix 61x43"))
    pg = by["j_prog_420"]
    out.append(("bad_cut_progressive", pg[:len(pg) * 2 // 3], "a prefix 77x51"))
    sof = seg[0xC0][0]

    def patched(at, new):
        b = bytearray(good)
        b[at:at + len(new)] = new
        return bytes(b)

    out.append(("bad_sof_length", patched(sof + 2, b"\x7f\xff"), "refused"))
    out.append(("bad_dqt_length", patched(seg[0xDB][0] + 2, b"\x00\x20"), "refused"))
    out.append(("bad_huge", patched(sof + 5, struct.pack(">HH", 65535, 65535)), "refused"))
    out.append(("bad_past_cap", patched(sof + 5, struct.pack(">HH", 4097, 4096)), "refused"))
    out.append(("bad_no_height", patched(sof + 5, struct.pack(">H", 0)), "refused"))
    out.append(("bad_12_bit", patched(sof + 4, b"\x0c"), "refused"))
    out.append(("bad_arithmetic", patched(sof + 1, b"\xc9"), "refused"))
    out.append(("bad_lossless", patched(sof + 1, b"\xc3"), "refused"))
    out.append(("bad_sampling", patched(sof + 11, b"\x51"), "refused"))
    # the Huffman tables (one to a segment, as libjpeg writes them): an AC table's two
    # codes of length 1 (all its others then past the tree), its counts past its segment,
    # and a DC table's first symbol past 15
    dhts = [i for m, i, ln in markers(good) if m == 0xC4]
    dc = [i for i in dhts if good[i + 4] >> 4 == 0][0]
    ac = [i for i in dhts if good[i + 4] >> 4 == 1][0]
    over = bytearray(good)
    over[ac + 5] = 2
    over[ac + 20] -= 2
    out.append(("bad_huffman_oversubscribed", bytes(over), "refused"))
    out.append(("bad_huffman_count", patched(ac + 20, b"\xff"), "refused"))
    out.append(("bad_huffman_dc_symbol", patched(dc + 21, b"\x10"), "refused"))
    out.append(("bad_scan_component", patched(seg[0xDA][0] + 5, b"\x09"), "refused"))
    # an interleaved scan of 16 blocks an MCU (Y sampled 4x4)
    out.append(("bad_mcu_blocks", patched(sof + 11, b"\x44"), "refused"))
    # the restart markers of a file that has them, one taken out
    rb = by["j_restart_blocks"]
    rs = {m: (i, ln) for m, i, ln in markers(rb)}
    start = rs[0xDA][0] + 2 + rs[0xDA][1]
    k = rb.index(b"\xff\xd3", start)
    out.append(("bad_restart_missing", rb[:k] + rb[k + 2:], "a prefix 77x51"))
    # a progressive scan whose approximation does not follow (Ah not Al + 1), its first
    ps = {m: (i, ln) for m, i, ln in markers(pg)}
    b = bytearray(pg)
    i = ps[0xDA][0]
    b[i + 4 + 1 + 2 * b[i + 4] + 2] = 0x31
    out.append(("bad_progression", bytes(b), "refused"))
    out.append(("bad_not_jpeg", b"\xff\xd8\xff", "refused"))
    out.append(("bad_tables_only", good[:sof], "refused"))
    return out


def jpeg_reference(name, b):
    """Pillow's decoding of a JPEG, its orientation applied, as RGB; written as NAME.pil.png"""
    import io
    from PIL import Image, ImageOps
    im = Image.open(io.BytesIO(b))
    im.load()
    im = ImageOps.exif_transpose(im).convert("RGB")
    im.save(os.path.join(HERE, name + ".pil.png"), optimize=True)
    return "%s %dx%d, its header %dx%d: within the bound" % (name, im.width, im.height, im.width, im.height)


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def main():
    png = png_fixtures()
    write(os.path.join(HERE, "expected.txt"), "\n".join(png) + "\n")
    gifs = gif_cases()
    lines = []
    for name, b, box in gifs:
        with open(os.path.join(HERE, name + ".gif"), "wb") as f:
            f.write(b)
        lines.append(gif_expectation(name, b, box))
    for name, b, want in gif_hostile(gifs):
        with open(os.path.join(HERE, name + ".gif"), "wb") as f:
            f.write(b)
        lines.append("%s: %s" % (name, want))
    lines.append("mutations of the fixtures: each decoded or refused")
    write(os.path.join(EXEC, "gif_decode.expect"), "\n".join(lines) + "\n")
    jpegs = jpeg_pil() + jpeg_own()
    lines = []
    for name, b in jpegs:
        with open(os.path.join(HERE, name + ".jpg"), "wb") as f:
            f.write(b)
        lines.append(jpeg_reference(name, b))
    # the decoder's arithmetic is libjpeg's (as Pillow's is): within the bound, and exact
    lines.append("exact, pixel for pixel: %d of %d" % (len(jpegs), len(jpegs)))
    for name, b, want in jpeg_hostile(dict(jpegs)):
        with open(os.path.join(HERE, name + ".jpg"), "wb") as f:
            f.write(b)
        lines.append("%s: %s" % (name, want))
    lines.append("mutations of the fixtures: each decoded or refused")
    write(os.path.join(EXEC, "jpeg_decode.expect"), "\n".join(lines) + "\n")
    sys.stdout.write("%d PNG, %d GIF and %d JPEG fixtures\n" % (len(png), len(gifs), len(jpegs)))


if __name__ == "__main__":
    main()
