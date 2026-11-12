#!/usr/bin/env python3
"""tools/pixdiff.py A.png B.png REGIONS -- two drawings of a page compared region by
region: for each line of REGIONS (id x0 y0 x1 y1, as tools/bench/shot.js writes them;
lines starting # skipped), the share of the region's pixels whose colours differ by more
than the tolerance in any channel (anti-aliasing's differences fall within it), and the
largest difference. Prints `ID SHARE% MAX` a region. The PNGs are read by this script's
own decoder (8-bit RGB or RGBA, any filter, not interlaced), so nothing is installed."""
import struct
import sys
import zlib

TOLERANCE = 48


def read_png(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("pixdiff: %s is no PNG" % path)
    p, w, h, ct, idat = 8, 0, 0, 0, b""
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        body = d[p + 8:p + 8 + n]
        p += 12 + n
        if t == b"IHDR":
            w, h, depth, ct, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ct not in (2, 6) or interlace:
                raise SystemExit("pixdiff: %s is not 8-bit RGB or RGBA, not interlaced" % path)
        elif t == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    bpp = 3 if ct == 2 else 4
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: tools/pixdiff.py A.png B.png REGIONS")
    wa, ha, ba, ra = read_png(sys.argv[1])
    wb, hb, bb, rb = read_png(sys.argv[2])
    for line in open(sys.argv[3]):
        if line.startswith("#") or not line.strip():
            continue
        name, x0, y0, x1, y1 = line.split()
        x0, y0 = max(0, int(x0)), max(0, int(y0))
        x1, y1 = min(int(x1), wa, wb), min(int(y1), ha, hb)
        total = bad = worst = 0
        for y in range(y0, y1):
            pa, pb = ra[y], rb[y]
            for x in range(x0, x1):
                d = max(abs(pa[x * ba + k] - pb[x * bb + k]) for k in range(3))
                total += 1
                if d > TOLERANCE:
                    bad += 1
                if d > worst:
                    worst = d
        share = 100.0 * bad / total if total else 0.0
        print("%s %.1f%% %d" % (name, share, worst))


if __name__ == "__main__":
    main()
