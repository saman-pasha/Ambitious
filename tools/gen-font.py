#!/usr/bin/env python3
"""tools/gen-font.py -- src/paint/font_data.h, the outlines the painter draws text with.

A subset of the DejaVu fonts, read from their TrueType files by this script's
own reader of the tables it needs (head, hhea, hmtx, maxp, loca, glyf, cmap)
and written as C: for each face, a base64 string of its metrics and its
glyphs' outlines -- each glyph's advance and contours, a contour's points as
TrueType has them (on or off the curve, quadratic), delta-coded varints.
Composite glyphs are resolved into their components' points.

    python3 tools/gen-font.py [FONTDIR] > src/paint/font_data.h

FONTDIR defaults to /usr/share/fonts/truetype/dejavu. The output is a copy of
part of the Font Software, so it carries the fonts' notice (the Bitstream Vera
license, as DejaVu's own files do) and keeps the DejaVu names, as that license
asks of a modified copy.

The blob, before base64:
  "AMBF" 1                                   magic and version
  units_per_em ascender descender line_gap   varints (zigzag for the signed)
  glyph_count, then for each glyph by code point:
    cp - previous cp, advance, contour count
    for each contour: point count, then each point's
      zigzag(dx) << 1 | on_curve, zigzag(dy)   deltas from the previous point
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

CODEPOINTS = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
              [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2032, 0x2033,
               0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x25AA, 0x25B8, 0x25BE, 0x25CB,
               0x25CF, 0x25E6, 0xFFFD])

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


def varint(n, out):
    if n < 0:
        raise ValueError(n)
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return


def zigzag(n):
    return n * 2 if n >= 0 else -n * 2 - 1


def face_blob(path):
    f = Font(path)
    out = bytearray(b"AMBF")
    out.append(1)
    varint(f.upem, out)
    varint(zigzag(f.ascender), out)
    varint(zigzag(f.descender), out)
    varint(f.line_gap, out)
    cps = [c for c in CODEPOINTS if c in f.cmap]
    varint(len(cps), out)
    prev = 0
    for cp in cps:
        gid = f.cmap[cp]
        conts = [c for c in f.contours(gid) if c]
        varint(cp - prev, out)
        prev = cp
        varint(f.advance(gid), out)
        varint(len(conts), out)
        px = py = 0
        for cont in conts:
            varint(len(cont), out)
            for (x, y, on) in cont:
                varint(zigzag(x - px) << 1 | (1 if on else 0), out)
                varint(zigzag(y - py), out)
                px, py = x, y
    missing = [c for c in CODEPOINTS if c not in f.cmap]
    return bytes(out), len(cps), missing


def main():
    fontdir = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu"
    w = sys.stdout.write
    w("/* src/paint/font_data.h -- generated by tools/gen-font.py from the DejaVu fonts' TrueType\n")
    w("   files: for each face, its metrics and its glyphs' outlines (the code points the script\n")
    w("   lists), base64. Do not edit; run the script. src/paint/font.c reads them.\n\n")
    w("   The fonts' notice, which this copy of part of the Font Software carries:\n\n")
    for line in NOTICE.split("\n"):
        w(("   " + line).rstrip() + "\n")
    w("*/\n")
    for name, file in FACES:
        blob, n, missing = face_blob(fontdir + "/" + file)
        text = base64.b64encode(blob).decode("ascii")
        w("\n/* %s: %d glyphs, %d bytes%s */\n" % (file, n, len(blob),
          "" if not missing else "; not in the face: " + " ".join("U+%04X" % c for c in missing)))
        w("static const char %s[] =\n" % name)
        for i in range(0, len(text), 96):
            w('    "%s"\n' % text[i:i + 96])
        w("    ;\n")


if __name__ == "__main__":
    main()
