#!/usr/bin/env python3
"""tools/gen-font.py -- src/paint/font_data.h, the outlines the painter draws text with.

A subset of the DejaVu fonts, read from their TrueType files by this script's
own reader of the tables it needs (head, hhea, hmtx, maxp, loca, glyf, cmap,
kern) and written as C: for each face, a TrueType font of the code points the
script lists, base64 -- the tables the painter reads (cmap format 4, glyf, head,
hhea, hmtx, kern format 0, loca, maxp), so that src/paint/font.c reads the
built-in faces as it reads a page's web fonts. Composite glyphs are resolved
into their components' points; the kerning pairs are the subset's.

    python3 tools/gen-font.py [FONTDIR] > src/paint/font_data.h
    python3 tools/gen-font.py --web FONT.ttf OUT.ttf|OUT.woff FAMILY

The second writes a web font for the fixture pages: the same subset of one
TrueType file as a font Chromium's sanitizer takes (with OS/2, post, name and a
version 1.0 maxp), named FAMILY, the fonts' notice in its name table; as WOFF 1
(its tables stored) when OUT ends in .woff.

FONTDIR defaults to /usr/share/fonts/truetype/dejavu. The output is a copy of
part of the Font Software, so it carries the fonts' notice (the Bitstream Vera
license, as DejaVu's own files do) and keeps the DejaVu names, as that license
asks of a modified copy.

"""
import base64
import struct
import sys

FACES = [
    ("amb_font_sans", "DejaVuSans.ttf"),
    ("amb_font_sans_bold", "DejaVuSans-Bold.ttf"),
    ("amb_font_serif", "DejaVuSerif.ttf"),
    ("amb_font_serif_bold", "DejaVuSerif-Bold.ttf"),
    ("amb_font_mono", "DejaVuSansMono.ttf"),
    ("amb_font_mono_bold", "DejaVuSansMono-Bold.ttf"),
]

# Latin-1, Greek's small letters (lower-greek markers), Hebrew's letters (right-to-left
# text), hyphens, quotes and guillemets, daggers, bullets, a few symbols generated content
# uses, and the replacement character
CODEPOINTS = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + list(range(0x3B1, 0x3CA)) +
              list(range(0x5D0, 0x5EB)) + [0x5BE, 0x5C0, 0x5C3, 0x5F3, 0x5F4] +
              [0x2010, 0x2011, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2032, 0x2033,
               0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x25AA, 0x25B8, 0x25BE, 0x25CB,
               0x25CF, 0x25E6, 0x201A, 0x201E, 0x2020, 0x2021, 0x2030, 0x2039, 0x203A, 0x2197, 0x2605, 0x2713,
               0x2714, 0x2717, 0xFFFD])

NOTICE = """Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is
a trademark of Bitstream, Inc. DejaVu changes are in public domain.

Permission is hereby granted, free of charge, to any person obtaining a copy
of the fonts accompanying this license ("Fonts") and associated
documentation files (the "Font Software"), to reproduce and distribute the
Font Software, including without limitation the rights to use, copy, merge,
publish, distribute, and/or sell copies of the Font Software, and to permit
persons to whom the Font Software is furnished to do so, subject to the
following conditions:

The above copyright and trademark notices and this permission notice shall
be included in all copies of one or more of the Font Software typefaces.

The Font Software may be modified, altered, or added to, and in particular
the designs of glyphs or characters in the Fonts may be modified and
additional glyphs or characters may be added to the Fonts, only if the fonts
are renamed to names not containing either the words "Bitstream" or the word
"Vera".

This License becomes null and void to the extent applicable to Fonts or Font
Software that has been modified and is distributed under the "Bitstream
Vera" names.

The Font Software may be sold as part of a larger software package but no
copy of one or more of the Font Software typefaces may be sold by itself.

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT,
TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME
FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING
ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE
FONT SOFTWARE.

Except as contained in this notice, the names of Gnome, the Gnome
Foundation, and Bitstream Inc., shall not be used in advertising or
otherwise to promote the sale, use or other dealings in this Font Software
without prior written authorization from the Gnome Foundation or Bitstream
Inc., respectively. For further information, contact: fonts at gnome dot
org."""


class Font:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        num = struct.unpack_from(">H", self.d, 4)[0]
        self.tables = {}
        for i in range(num):
            tag, _, off, length = struct.unpack_from(">4sIII", self.d, 12 + 16 * i)
            self.tables[tag.decode("latin-1")] = (off, length)
        head = self.tables["head"][0]
        self.upem = struct.unpack_from(">H", self.d, head + 18)[0]
        self.loca_long = struct.unpack_from(">h", self.d, head + 50)[0] == 1
        hhea = self.tables["hhea"][0]
        self.ascender, self.descender, self.line_gap = struct.unpack_from(">hhh", self.d, hhea + 4)
        self.n_hmetrics = struct.unpack_from(">H", self.d, hhea + 34)[0]
        self.n_glyphs = struct.unpack_from(">H", self.d, self.tables["maxp"][0] + 4)[0]
        self.cmap = self.read_cmap()

    def advance(self, gid):
        hmtx = self.tables["hmtx"][0]
        i = min(gid, self.n_hmetrics - 1)
        return struct.unpack_from(">H", self.d, hmtx + 4 * i)[0]

    def read_cmap(self):
        base = self.tables["cmap"][0]
        n = struct.unpack_from(">H", self.d, base + 2)[0]
        sub = None
        for i in range(n):
            plat, enc, off = struct.unpack_from(">HHI", self.d, base + 4 + 8 * i)
            if (plat, enc) in ((3, 1), (0, 3), (0, 4)) and struct.unpack_from(">H", self.d, base + off)[0] == 4:
                sub = base + off
                break
        if sub is None:
            raise SystemExit("gen-font: no format 4 cmap")
        seg2 = struct.unpack_from(">H", self.d, sub + 6)[0]
        segs = seg2 // 2
        ends = struct.unpack_from(">%dH" % segs, self.d, sub + 14)
        starts = struct.unpack_from(">%dH" % segs, self.d, sub + 16 + seg2)
        deltas = struct.unpack_from(">%dh" % segs, self.d, sub + 16 + 2 * seg2)
        ro_base = sub + 16 + 3 * seg2
        ros = struct.unpack_from(">%dH" % segs, self.d, ro_base)
        m = {}
        for s in range(segs):
            for c in range(starts[s], ends[s] + 1):
                if c == 0xFFFF:
                    continue
                if ros[s] == 0:
                    g = (c + deltas[s]) & 0xFFFF
                else:
                    addr = ro_base + 2 * s + ros[s] + 2 * (c - starts[s])
                    g = struct.unpack_from(">H", self.d, addr)[0]
                    if g:
                        g = (g + deltas[s]) & 0xFFFF
                if g:
                    m[c] = g
        return m

    def glyph_range(self, gid):
        loca = self.tables["loca"][0]
        if self.loca_long:
            a, b = struct.unpack_from(">II", self.d, loca + 4 * gid)
        else:
            a, b = struct.unpack_from(">HH", self.d, loca + 2 * gid)
            a, b = 2 * a, 2 * b
        g = self.tables["glyf"][0]
        return g + a, b - a

    def contours(self, gid, depth=0):
        """the glyph's contours: lists of (x, y, on_curve), in font units"""
        off, length = self.glyph_range(gid)
        if length == 0:
            return []
        nc = struct.unpack_from(">h", self.d, off)[0]
        p = off + 10
        if nc >= 0:
            ends = struct.unpack_from(">%dH" % nc, self.d, p)
            p += 2 * nc
            ilen = struct.unpack_from(">H", self.d, p)[0]
            p += 2 + ilen
            npts = ends[-1] + 1 if nc else 0
            flags = []
            while len(flags) < npts:
                fl = self.d[p]
                p += 1
                flags.append(fl)
                if fl & 8:
                    r = self.d[p]
                    p += 1
                    flags.extend([fl] * r)
            flags = flags[:npts]
            xs, ys = [], []
            v = 0
            for fl in flags:
                if fl & 2:
                    dx = self.d[p]
                    p += 1
                    v += dx if fl & 16 else -dx
                elif not fl & 16:
                    v += struct.unpack_from(">h", self.d, p)[0]
                    p += 2
                xs.append(v)
            v = 0
            for fl in flags:
                if fl & 4:
                    dy = self.d[p]
                    p += 1
                    v += dy if fl & 32 else -dy
                elif not fl & 32:
                    v += struct.unpack_from(">h", self.d, p)[0]
                    p += 2
                ys.append(v)
            out, start = [], 0
            for e in ends:
                out.append([(xs[i], ys[i], flags[i] & 1) for i in range(start, e + 1)])
                start = e + 1
            return out
        # a composite: its components' contours, each moved and scaled
        if depth > 8:
            raise SystemExit("gen-font: composite glyphs nested too deep")
        out = []
        more = True
        while more:
            fl, cg = struct.unpack_from(">HH", self.d, p)
            p += 4
            if fl & 1:
                a1, a2 = struct.unpack_from(">hh", self.d, p)
                p += 4
            else:
                a1, a2 = struct.unpack_from(">bb", self.d, p)
                p += 2
            a, b, c, d = 1.0, 0.0, 0.0, 1.0
            if fl & 8:
                a = d = struct.unpack_from(">h", self.d, p)[0] / 16384.0
                p += 2
            elif fl & 0x40:
                a, d = (x / 16384.0 for x in struct.unpack_from(">hh", self.d, p))
                p += 4
            elif fl & 0x80:
                a, b, c, d = (x / 16384.0 for x in struct.unpack_from(">hhhh", self.d, p))
                p += 8
            if not fl & 2:
                sys.stderr.write("gen-font: glyph %d places a component by points; placed at 0, 0\n" % gid)
                a1 = a2 = 0
            for cont in self.contours(cg, depth + 1):
                out.append([(int(round(a * x + c * y + a1)), int(round(b * x + d * y + a2)), on) for (x, y, on) in cont])
            more = bool(fl & 0x20)
        return out


def kern_pairs(f):
    """the font's kern table (format 0 subtables): {(left gid, right gid): value}"""
    if "kern" not in f.tables:
        return {}
    off = f.tables["kern"][0]
    _, n = struct.unpack_from(">HH", f.d, off)
    p = off + 4
    pairs = {}
    for _ in range(n):
        _, length, coverage = struct.unpack_from(">HHH", f.d, p)
        if coverage >> 8 == 0 and coverage & 1:          # format 0, horizontal
            npairs = struct.unpack_from(">H", f.d, p + 6)[0]
            q = p + 14
            for _ in range(npairs):
                left, right, value = struct.unpack_from(">HHh", f.d, q)
                pairs[(left, right)] = value
                q += 6
        p += length
    return pairs


def checksum(body):
    body = body + b"\0" * (-len(body) % 4)
    return sum(struct.unpack(">%dI" % (len(body) // 4), body)) & 0xFFFFFFFF


def table_record(tag, body, offset):
    return struct.pack(">4sIII", tag.encode("latin-1"), checksum(body), offset, len(body))


def bsearch(n, unit):
    """a binary search's header for n entries of unit bytes: searchRange, entrySelector, rangeShift"""
    if n == 0:
        return 0, 0, 0
    p, e = 1, 0
    while p * 2 <= n:
        p, e = p * 2, e + 1
    return p * unit, e, n * unit - p * unit


def name_table(family, style):
    """a name table: the notice, the family, its style, the full and PostScript names, the license"""
    lines = NOTICE.split("\n")
    names = [(0, " ".join(lines[:2])), (1, family), (2, style), (3, family + " " + style), (4, family + " " + style),
             (6, (family + "-" + style).replace(" ", "")), (13, NOTICE)]
    recs, strs = bytearray(), bytearray()
    for nid, text in names:
        u = text.encode("utf-16-be")
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(u), len(strs))
        strs += u
    return struct.pack(">HHH", 0, len(names), 6 + len(recs)) + bytes(recs) + bytes(strs)


def woff(sfnt):
    """an sfnt as WOFF 1, its tables stored as they are"""
    n = struct.unpack_from(">H", sfnt, 4)[0]
    recs = [struct.unpack_from(">4sIII", sfnt, 12 + 16 * i) for i in range(n)]
    head = 44 + 20 * n
    body, dirs = bytearray(), bytearray()
    for tag, cs, off, length in recs:
        dirs += struct.pack(">4sIIII", tag, head + len(body), length, length, cs)
        body += sfnt[off:off + length]
        while len(body) % 4:
            body.append(0)
    total = head + len(body)
    out = struct.pack(">4sIIHHIHHIIIII", b"wOFF", 0x00010000, total, n, 0, len(sfnt), 1, 0, 0, 0, 0, 0, 0)
    return out + bytes(dirs) + bytes(body)


def encode_glyph(contours):
    """a simple glyph's glyf entry: header, end points, no instructions, flags, x, y"""
    if not contours:
        return b""
    pts = [pt for c in contours for pt in c]
    xs = [x for x, _, _ in pts]
    ys = [y for _, y, _ in pts]
    out = bytearray(struct.pack(">hhhhh", len(contours), min(xs), min(ys), max(xs), max(ys)))
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack(">H", end)
    out += struct.pack(">H", 0)
    flags, xb, yb = bytearray(), bytearray(), bytearray()
    px = py = 0
    for (x, y, on) in pts:
        fl = 1 if on else 0
        dx, dy = x - px, y - py
        if dx == 0:
            fl |= 16
        elif -255 <= dx <= 255:
            fl |= 2 | (16 if dx > 0 else 0)
            xb.append(abs(dx))
        else:
            xb += struct.pack(">h", dx)
        if dy == 0:
            fl |= 32
        elif -255 <= dy <= 255:
            fl |= 4 | (32 if dy > 0 else 0)
            yb.append(abs(dy))
        else:
            yb += struct.pack(">h", dy)
        flags.append(fl)
        px, py = x, y
    out += flags + xb + yb
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def subset(path, web=None):
    """a TrueType font of the code points the script lists: the tables the painter reads;
    for the web (web, its family's name), the tables a sanitizer asks for besides"""
    f = Font(path)
    cps = [c for c in CODEPOINTS if c in f.cmap]
    gids = [0]
    for c in cps:
        if f.cmap[c] not in gids:
            gids.append(f.cmap[c])
    newid = {g: i for i, g in enumerate(gids)}
    glyphs = [encode_glyph([c for c in f.contours(g) if c]) for g in gids]
    # loca (long) and glyf
    loca, glyf = bytearray(), bytearray()
    for g in glyphs:
        loca += struct.pack(">I", len(glyf))
        glyf += g
    loca += struct.pack(">I", len(glyf))
    allpts = [pt for g in gids for c in f.contours(g) for pt in c]
    xmin = min(x for x, _, _ in allpts); xmax = max(x for x, _, _ in allpts)
    ymin = min(y for _, y, _ in allpts); ymax = max(y for _, y, _ in allpts)
    head = struct.pack(">IIIIHHqqhhhhHhhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, f.upem, 0, 0,
                       xmin, ymin, xmax, ymax, 0, 8, 2, 1, 0)
    advances = [f.advance(g) for g in gids]
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, f.ascender, f.descender, f.line_gap, max(advances),
                       0, 0, xmax, 1, 0, 0, 0, 0, 0, 0, 0, len(gids))
    maxp = struct.pack(">IH", 0x00005000, len(gids))
    if web:
        m = f.tables["maxp"][0]
        maxp = struct.pack(">IH", 0x00010000, len(gids)) + f.d[m + 6:m + 32]
    hmtx = b"".join(struct.pack(">Hh", a, 0) for a in advances)
    # cmap: format 4, a segment a code point (and the closing one)
    segs = sorted(set(cps)) + [0xFFFF]
    segx2 = 2 * len(segs)
    ends = b"".join(struct.pack(">H", c) for c in segs)
    starts = ends
    deltas = b"".join(struct.pack(">H", (newid[f.cmap[c]] - c) % 0x10000) for c in segs[:-1]) + struct.pack(">H", 1)
    ros = b"\0\0" * len(segs)
    sub = struct.pack(">HHHHHHH", 4, 0, 0, segx2, *bsearch(len(segs), 2)) + ends + b"\0\0" + starts + deltas + ros
    sub = sub[:2] + struct.pack(">H", len(sub)) + sub[4:]
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub
    # kern: format 0, the pairs of the subset's glyphs, in order
    pairs = sorted((newid[l], newid[r], v) for (l, r), v in kern_pairs(f).items() if l in newid and r in newid and v)
    kbody = struct.pack(">HHHH", len(pairs), *bsearch(len(pairs), 6)) + b"".join(struct.pack(">HHh", l, r, v) for l, r, v in pairs)
    kern = struct.pack(">HHHHH", 0, 1, 0, 6 + len(kbody), 1) + kbody
    tables = [("cmap", cmap), ("glyf", bytes(glyf)), ("head", head), ("hhea", hhea), ("hmtx", hmtx),
              ("kern", kern), ("loca", bytes(loca)), ("maxp", maxp)]
    if web:
        o = f.tables["OS/2"]
        post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
        style = "Bold" if "Bold" in path else "Oblique" if "Oblique" in path else "Regular"
        tables += [("OS/2", f.d[o[0]:o[0] + o[1]]), ("name", name_table(web, style)), ("post", post)]
        tables.sort(key=lambda t: t[0])
    out = bytearray(struct.pack(">IHH", 0x00010000, len(tables), 0) + struct.pack(">HH", *bsearch(len(tables), 16)[1:]))
    out[6:8] = struct.pack(">H", bsearch(len(tables), 16)[0])
    offset = 12 + 16 * len(tables)
    bodies = bytearray()
    for tag, body in tables:
        out += table_record(tag, body, offset + len(bodies))
        bodies += body
        while len(bodies) % 4:
            bodies.append(0)
    missing = [c for c in CODEPOINTS if c not in f.cmap]
    return bytes(out + bodies), len(gids), len(pairs), missing


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--web":
        blob, _, _, _ = subset(sys.argv[2], sys.argv[4])
        with open(sys.argv[3], "wb") as out:
            out.write(woff(blob) if sys.argv[3].endswith(".woff") else blob)
        return
    fontdir = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu"
    w = sys.stdout.write
    w("/* src/paint/font_data.h -- generated by tools/gen-font.py from the DejaVu fonts' TrueType\n")
    w("   files: for each face a TrueType font of the code points the script lists (cmap, glyf,\n")
    w("   head, hhea, hmtx, kern, loca, maxp), base64. Do not edit; run the script.\n")
    w("   src/paint/font.c reads them as it reads a page's web fonts.\n\n")
    w("   The fonts' notice, which this copy of part of the Font Software carries:\n\n")
    for line in NOTICE.split("\n"):
        w(("   " + line).rstrip() + "\n")
    w("*/\n")
    for name, file in FACES:
        blob, n, npairs, missing = subset(fontdir + "/" + file)
        text = base64.b64encode(blob).decode("ascii")
        w("\n/* %s: %d glyphs, %d kerning pairs, %d bytes%s */\n" % (file, n, npairs, len(blob),
          "" if not missing else "; not in the face: " + " ".join("U+%04X" % c for c in missing)))
        w("static const char %s[] =\n" % name)
        for i in range(0, len(text), 96):
            w('    "%s"\n' % text[i:i + 96])
        w("    ;\n")


if __name__ == "__main__":
    main()
