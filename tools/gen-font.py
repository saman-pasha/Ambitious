#!/usr/bin/env python3
"""tools/gen-font.py -- src/paint/font_data.h, the outlines the painter draws text with.

Subsets of the Liberation fonts (Serif, Sans and Mono: the faces Chromium draws
serif, sans-serif, Times New Roman, Arial and Courier New with on a Linux machine
with fontconfig's metric aliases), each regular, bold, italic and bold italic, and of
the DejaVu fonts (Sans, Serif and Sans Mono: monospace's face there, and the faces
their own names name), each regular and bold, and Sans Mono's oblique and bold
oblique (Sans and Serif have no italic face on such a machine: Chromium slants their
upright ones), read from their TrueType files by this script's own reader of the tables
it needs (head, hhea, hmtx, maxp, loca, glyf, cmap, kern) and written as C: for each
face, a TrueType font of the code points the script lists (each face has those its
file has), packed (lz_pack: LZ77 in LZ4's byte-aligned tokens, which font.c unpacks
at a few instructions a byte, where deflate's bits cost it some hundred), base64 -- the
tables the painter reads (cmap format 4,
glyf, head, hhea, hmtx, kern format 0, loca, maxp), so that src/paint/font.c reads the
built-in faces as it reads a page's web fonts. A composite glyph stays a composite,
its components in the subset (none of these faces' glyphs here places one by points).
A code point a face has no glyph for but has every part of the canonical decomposition
of, HarfBuzz draws with those parts in that face (hb-ot-shape-normalize: DejaVu Sans
Mono's Vietnamese with two marks, Liberation's arrows and relations with a stroke, the
Kelvin and Angstrom signs), so the subset has it: a singleton's as its part's glyph, a
base and marks as a composite of them, each mark where the face's GPOS mark and mkmk
lookups attach it (else where HarfBuzz leaves it, past the base), the base's advance
the glyph's. The kerning pairs are the subset's code points'. (Over these code points each face's
kern table holds the pairs its GPOS kern feature does, which HarfBuzz applies; the
faces' GSUB substitutes nothing in them by default but DejaVu's ligatures, which
Ambitious does not form.) What each block of code points costs the faces is written on
stderr.

    python3 tools/gen-font.py [DEJAVUDIR [LIBERATIONDIR]] > src/paint/font_data.h
    python3 tools/gen-font.py --web FONT.ttf OUT.ttf|OUT.woff FAMILY

The second writes a web font for the fixture pages: the same subset of one
TrueType file as a font Chromium's sanitizer takes (with OS/2, post, name and a
version 1.0 maxp), named FAMILY, its fonts' notice in its name table; as WOFF 1
(its tables stored) when OUT ends in .woff.

DEJAVUDIR and LIBERATIONDIR default to /usr/share/fonts/truetype/dejavu and
/usr/share/fonts/truetype/liberation. The output is a copy of part of each Font
Software, so it carries each one's notice: DejaVu's (the Bitstream Vera license,
as DejaVu's own files do; a modified copy keeps the DejaVu names, as that license
asks) and Liberation's (its copyright and the SIL Open Font License 1.1, under
which that part of the output stays; the subsets carry no name table, so none is
presented under the Reserved Font Names, and the painter answers to none of them).

"""
import base64
import struct
import subprocess
import sys
import unicodedata

# the faces, in src/paint/font.h's order: each family's regular, bold, italic and bold
# italic (DejaVu Sans and DejaVu Serif have no italic face here: font.c slants their
# upright ones, as Skia does)
FACES = [
    ("serif", "liberation", "LiberationSerif-Regular.ttf"),
    ("serif bold", "liberation", "LiberationSerif-Bold.ttf"),
    ("serif italic", "liberation", "LiberationSerif-Italic.ttf"),
    ("serif bold italic", "liberation", "LiberationSerif-BoldItalic.ttf"),
    ("sans", "liberation", "LiberationSans-Regular.ttf"),
    ("sans bold", "liberation", "LiberationSans-Bold.ttf"),
    ("sans italic", "liberation", "LiberationSans-Italic.ttf"),
    ("sans bold italic", "liberation", "LiberationSans-BoldItalic.ttf"),
    ("courier", "liberation", "LiberationMono-Regular.ttf"),
    ("courier bold", "liberation", "LiberationMono-Bold.ttf"),
    ("courier italic", "liberation", "LiberationMono-Italic.ttf"),
    ("courier bold italic", "liberation", "LiberationMono-BoldItalic.ttf"),
    ("dejavu sans", "dejavu", "DejaVuSans.ttf"),
    ("dejavu sans bold", "dejavu", "DejaVuSans-Bold.ttf"),
    ("dejavu serif", "dejavu", "DejaVuSerif.ttf"),
    ("dejavu serif bold", "dejavu", "DejaVuSerif-Bold.ttf"),
    ("mono", "dejavu", "DejaVuSansMono.ttf"),
    ("mono bold", "dejavu", "DejaVuSansMono-Bold.ttf"),
    ("mono italic", "dejavu", "DejaVuSansMono-Oblique.ttf"),
    ("mono bold italic", "dejavu", "DejaVuSansMono-BoldOblique.ttf"),
]

# the blocks of code points the faces carry (what each has of them): the letters of the
# languages written in Latin, Greek and Cyrillic (Vietnamese among them), Hebrew's
# letters (right-to-left text), the punctuation, currency, letterlike symbols, arrows,
# mathematical operators, box drawing, block elements, geometric shapes, miscellaneous
# symbols and dingbats pages use, the spacing accents windows-1252 maps (circumflex,
# caron, tilde and the rest), and the replacement character
BLOCKS = [
    ("Basic Latin", 0x20, 0x7E), ("Latin-1 Supplement", 0xA0, 0xFF), ("Latin Extended-A", 0x100, 0x17F),
    ("Latin Extended-B", 0x180, 0x24F), ("spacing accents", 0x2C6, 0x2C7), ("spacing accents", 0x2D8, 0x2DD),
    ("Greek and Coptic", 0x370, 0x3FF), ("Cyrillic", 0x400, 0x4FF), ("Cyrillic Supplement", 0x500, 0x52F),
    ("Hebrew letters", 0x5D0, 0x5EA), ("Hebrew punctuation", 0x5BE, 0x5BE), ("Hebrew punctuation", 0x5C0, 0x5C0),
    ("Hebrew punctuation", 0x5C3, 0x5C3), ("Hebrew punctuation", 0x5F3, 0x5F4),
    ("Latin Extended Additional", 0x1E00, 0x1EFF), ("General Punctuation", 0x2000, 0x206F),
    ("Currency Symbols", 0x20A0, 0x20CF), ("Letterlike Symbols", 0x2100, 0x214F), ("Arrows", 0x2190, 0x21FF),
    ("Mathematical Operators", 0x2200, 0x22FF), ("Box Drawing", 0x2500, 0x257F), ("Block Elements", 0x2580, 0x259F),
    ("Geometric Shapes", 0x25A0, 0x25FF), ("Miscellaneous Symbols", 0x2600, 0x26FF), ("Dingbats", 0x2700, 0x27BF),
    ("the replacement character", 0xFFFD, 0xFFFD)]
CODEPOINTS = sorted({c for _, a, b in BLOCKS for c in range(a, b + 1)})

DEJAVU_NOTICE = """Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is
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

LIBERATION_NOTICE = """Digitized data copyright (c) 2010 Google Corporation with Reserved Font Arimo,
Tinos and Cousine. Copyright (c) 2012 Red Hat, Inc. with Reserved Font Name
Liberation.

This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
http://scripts.sil.org/OFL

SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE."""

NOTICES = {"dejavu": DEJAVU_NOTICE, "liberation": LIBERATION_NOTICE}
# the names a modified copy of the Liberation fonts may not be presented under (OFL, 3)
RESERVED = ("liberation", "arimo", "tinos", "cousine")


def source_of(path):
    """which Font Software a TrueType file is part of, by its name"""
    return "liberation" if "liberation" in path.rsplit("/", 1)[-1].lower() else "dejavu"


class Font:
    def __init__(self, path):
        self.path = path
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


def name_table(family, style, notice):
    """a name table: the notice, the family, its style, the full and PostScript names, the license"""
    lines = notice.split("\n")
    names = [(0, " ".join(lines[:2])), (1, family), (2, style), (3, family + " " + style), (4, family + " " + style),
             (6, (family + "-" + style).replace(" ", "")), (13, notice)]
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


def lz_pack(src, chain=64):
    """src packed by LZ77 in LZ4's byte-aligned tokens: a token byte (the literals' count
    << 4 | the match's length - 4; 15 in either meaning bytes follow that add to it, 255
    each, then one less), the literals, then, unless it is the last token, the match's
    offset back (two bytes, little-endian, 1 to 65535) and its length's further bytes. A
    match is the longest of the last 64 places its first four bytes were seen."""
    out, n = bytearray(), len(src)
    head, prev = {}, [0] * n
    i = lit = 0

    def put_len(v):
        while v >= 255:
            out.append(255)
            v -= 255
        out.append(v)

    def emit(start, end, mlen, off):
        ll = end - start
        out.append(min(ll, 15) << 4 | (min(mlen - 4, 15) if mlen else 0))
        if ll >= 15:
            put_len(ll - 15)
        out.extend(src[start:end])
        if mlen:
            out.extend(struct.pack("<H", off))
            if mlen - 4 >= 15:
                put_len(mlen - 4 - 15)

    def seen(p):
        if p + 4 <= n:
            key = src[p:p + 4]
            prev[p] = head.get(key, -1)
            head[key] = p
    while i < n:
        best = off = 0
        j = head.get(src[i:i + 4], -1) if i + 4 <= n else -1
        for _ in range(chain):
            if j < 0 or i - j > 65535:
                break
            m = 0
            while i + m < n and src[j + m] == src[i + m]:
                m += 1
            if m > best:
                best, off = m, i - j
            j = prev[j]
        if best >= 4:
            emit(lit, i, best, off)
            for p in range(i, i + best):
                seen(p)
            i += best
            lit = i
        else:
            seen(i)
            i += 1
    emit(lit, n, 0, 0)
    return bytes(out)


def lz_unpack(z):
    """lz_pack's bytes unpacked (the generator's check of its own output)"""
    out, i = bytearray(), 0
    while i < len(z):
        tok = z[i]
        i += 1
        ll = tok >> 4
        if ll == 15:
            while z[i] == 255:
                ll += 255
                i += 1
            ll += z[i]
            i += 1
        out += z[i:i + ll]
        i += ll
        if i >= len(z):
            break
        off = z[i] | z[i + 1] << 8
        i += 2
        ml = (tok & 15) + 4
        if tok & 15 == 15:
            while z[i] == 255:
                ml += 255
                i += 1
            ml += z[i]
            i += 1
        for _ in range(ml):
            out.append(out[-off])
    return bytes(out)


def coverage(d, off):
    """an OpenType coverage table's glyphs: {glyph: its coverage index}"""
    fmt, n = struct.unpack_from(">HH", d, off)
    if fmt == 1:
        return {g: i for i, g in enumerate(struct.unpack_from(">%dH" % n, d, off + 4))}
    out = {}
    for k in range(n):
        a, b, i = struct.unpack_from(">HHH", d, off + 4 + 6 * k)
        for g in range(a, b + 1):
            out[g] = i + g - a
    return out


def anchor(d, off):
    """an anchor's x and y (any format)"""
    return struct.unpack_from(">hh", d, off + 2)


def gpos_marks(f):
    """the marks' places GPOS gives (its mark and mkmk features' lookups, types 4 and 6, in
    their order, through extensions): (to_base, to_mark), each {(base, mark): (dx, dy)},
    the mark's origin from its base's (or from the earlier mark's), a later lookup's over
    an earlier's, as HarfBuzz applies them"""
    to_base, to_mark = {}, {}
    if "GPOS" not in f.tables:
        return to_base, to_mark
    d, g = f.d, f.tables["GPOS"][0]
    _, _, _, feats, lookups = struct.unpack_from(">HHHHH", d, g)
    feats += g
    lookups += g
    which = set()
    for k in range(struct.unpack_from(">H", d, feats)[0]):
        tag, off = struct.unpack_from(">4sH", d, feats + 2 + 6 * k)
        if tag in (b"mark", b"mkmk"):
            n = struct.unpack_from(">H", d, feats + off + 2)[0]
            which.update(struct.unpack_from(">%dH" % n, d, feats + off + 4))
    for li in sorted(which):
        lk = lookups + struct.unpack_from(">H", d, lookups + 2 + 2 * li)[0]
        kind, _, nsub = struct.unpack_from(">HHH", d, lk)
        for s in range(nsub):
            st = lk + struct.unpack_from(">H", d, lk + 6 + 2 * s)[0]
            t = kind
            if kind == 9:
                _, t, ext = struct.unpack_from(">HHI", d, st)
                st += ext
            if t not in (4, 6):
                continue
            _, mcov, bcov, nclass, marr, barr = struct.unpack_from(">HHHHHH", d, st)
            marks, bases = coverage(d, st + mcov), coverage(d, st + bcov)
            marr += st
            barr += st
            target = to_base if t == 4 else to_mark
            for b, bi in bases.items():
                for m, mi in marks.items():
                    cls, moff = struct.unpack_from(">HH", d, marr + 2 + 4 * mi)
                    boff = struct.unpack_from(">H", d, barr + 2 + 2 * (bi * nclass + cls))[0]
                    if not boff:
                        continue
                    bx, by = anchor(d, barr + boff)
                    mx, my = anchor(d, marr + moff)
                    target[(b, m)] = (bx - mx, by - my)
    return to_base, to_mark


def decomposition(c, cmap):
    """the code points HarfBuzz draws c with in a font that has no glyph for it but has
    every part of its canonical decomposition (taken again for a part it has none for): a
    base and its marks, or None"""
    d = unicodedata.decomposition(chr(c))
    if not d or d.startswith("<"):
        return None
    out = []
    for x in d.split():
        p = int(x, 16)
        if p in cmap:
            out.append(p)
        else:
            q = decomposition(p, cmap)
            if q is None:
                return None
            out += q
    return out


def components(f, gid):
    """a composite glyph's components, each (flags, glyph, its arguments and transform's
    bytes); None for a simple glyph"""
    off, length = f.glyph_range(gid)
    if length == 0 or struct.unpack_from(">h", f.d, off)[0] >= 0:
        return None
    out, p, more = [], off + 10, True
    while more:
        fl, cg = struct.unpack_from(">HH", f.d, p)
        n = (4 if fl & 1 else 2) + (2 if fl & 8 else 4 if fl & 0x40 else 8 if fl & 0x80 else 0)
        if not fl & 2:
            raise SystemExit("gen-font: glyph %d of %s places a component by points" % (gid, f.path))
        out.append((fl, cg, f.d[p + 4:p + 4 + n]))
        p += 4 + n
        more = bool(fl & 0x20)
    return out


def encode_composite(f, gid, newid):
    """a composite glyph's glyf entry: its header and components, each component's glyph
    renumbered, no instructions"""
    off, _ = f.glyph_range(gid)
    out = bytearray(f.d[off:off + 10])
    for fl, cg, rest in components(f, gid):
        out += struct.pack(">HH", fl & ~0x0100, newid[cg]) + rest
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def subset(path, web=None, cps_of=None):
    """a TrueType font of the code points the script lists: the tables the painter reads;
    for the web (web, its family's name), the tables a sanitizer asks for besides. With
    cps_of, each glyph's bytes are added to the block of the first code point that needs
    them (cps_of: block of a code point -> its bytes)"""
    f = Font(path)
    # a code point the font has no glyph for but has its canonical decomposition, HarfBuzz
    # draws with the decomposition's glyphs (hb-ot-shape-normalize): a singleton's as its
    # one part's glyph; a base and its marks as a glyph made of them, each mark where GPOS
    # attaches it (to the base, or to the mark before it), else where the pen is, past the
    # base, the base's advance theirs (the marks' are zeroed)
    cpmap = dict(f.cmap)
    to_base, to_mark = gpos_marks(f)
    for c in CODEPOINTS:
        parts = None if c in f.cmap else decomposition(c, f.cmap)
        if not parts:
            continue
        if len(parts) == 1:
            cpmap[c] = f.cmap[parts[0]]
            continue
        base = f.cmap[parts[0]]
        marks, prev = [], None
        for p in parts[1:]:
            m = f.cmap[p]
            pos = to_base.get((base, m), (f.advance(base), 0))
            if prev is not None and (prev[0], m) in to_mark:
                pos = (prev[1] + to_mark[(prev[0], m)][0], prev[2] + to_mark[(prev[0], m)][1])
            prev = (m, pos[0], pos[1])
            marks.append(prev)
        cpmap[c] = ("made", base, tuple(marks))
    cps = [c for c in CODEPOINTS if c in cpmap]
    gids, newid = [0], {0: 0}

    def points(g):
        if isinstance(g, tuple):
            return [(x + dx, y + dy, on) for p, dx, dy in [(g[1], 0, 0)] + list(g[2]) for c in f.contours(p) for x, y, on in c]
        return [pt for c in f.contours(g) for pt in c]

    def encode(g):
        if isinstance(g, tuple):
            pts = points(g) or [(0, 0, 1)]
            out = bytearray(struct.pack(">hhhhh", -1, min(x for x, _, _ in pts), min(y for _, y, _ in pts),
                                        max(x for x, _, _ in pts), max(y for _, y, _ in pts)))
            parts = [(g[1], 0, 0)] + list(g[2])
            for k, (p, dx, dy) in enumerate(parts):
                fl = 0x0003 | (0x0020 if k + 1 < len(parts) else 0) | (0x0200 if k == 0 else 0)
                out += struct.pack(">HHhh", fl, newid.get(p, 0), dx, dy)
            return bytes(out)
        if components(f, g):
            return encode_composite(f, g, newid)
        return encode_glyph([c for c in f.contours(g) if c])

    def add(g):
        if g in newid:
            return 0
        newid[g] = len(gids)
        gids.append(g)
        if isinstance(g, tuple):
            parts = [g[1]] + [m for m, _, _ in g[2]]
        else:
            parts = [cg for _, cg, _ in components(f, g) or []]
        n = len(encode(g)) if not parts or isinstance(g, tuple) else 4 * len(parts) + 10
        return n + sum(add(p) for p in parts)
    for c in cps:
        n = add(cpmap[c])
        if cps_of is not None:
            cps_of[c] = cps_of.get(c, 0) + n + 12
    glyphs = [encode(g) for g in gids]
    # loca (long) and glyf
    loca, glyf = bytearray(), bytearray()
    for g in glyphs:
        loca += struct.pack(">I", len(glyf))
        glyf += g
        while len(glyf) % 4:
            glyf.append(0)
    loca += struct.pack(">I", len(glyf))
    allpts = [pt for g in gids for pt in points(g)]
    xmin = min(x for x, _, _ in allpts); xmax = max(x for x, _, _ in allpts)
    ymin = min(y for _, y, _ in allpts); ymax = max(y for _, y, _ in allpts)
    head = struct.pack(">IIIIHHqqhhhhHhhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, f.upem, 0, 0,
                       xmin, ymin, xmax, ymax, 0, 8, 2, 1, 0)
    advances = [f.advance(g[1] if isinstance(g, tuple) else g) for g in gids]
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, f.ascender, f.descender, f.line_gap, max(advances),
                       0, 0, xmax, 1, 0, 0, 0, 0, 0, 0, 0, len(gids))
    maxp = struct.pack(">IH", 0x00005000, len(gids))
    if web:
        m = f.tables["maxp"][0]
        maxp = struct.pack(">IH", 0x00010000, len(gids)) + f.d[m + 6:m + 32]
    hmtx = b"".join(struct.pack(">Hh", a, 0) for a in advances)
    # cmap: format 4, a segment for each run of code points whose glyphs run on with them
    # (and the closing one)
    runs = []
    for c in cps:
        g = newid[cpmap[c]]
        if runs and runs[-1][1] == c - 1 and (runs[-1][2] + c - runs[-1][0]) % 0x10000 == g:
            runs[-1][1] = c
        else:
            runs.append([c, c, g])
    segx2 = 2 * (len(runs) + 1)
    ends = b"".join(struct.pack(">H", e) for _, e, _ in runs) + struct.pack(">H", 0xFFFF)
    starts = b"".join(struct.pack(">H", s) for s, _, _ in runs) + struct.pack(">H", 0xFFFF)
    deltas = b"".join(struct.pack(">H", (g - s) % 0x10000) for s, _, g in runs) + struct.pack(">H", 1)
    ros = b"\0\0" * (len(runs) + 1)
    sub = struct.pack(">HHHHHHH", 4, 0, 0, segx2, *bsearch(len(runs) + 1, 2)) + ends + b"\0\0" + starts + deltas + ros
    sub = sub[:2] + struct.pack(">H", len(sub)) + sub[4:]
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub
    # kern: format 0, the pairs of the subset's code points' glyphs, in order
    mapped = {newid[cpmap[c]] for c in cps}
    pairs = sorted((newid[l], newid[r], v) for (l, r), v in kern_pairs(f).items()
                   if l in newid and r in newid and newid[l] in mapped and newid[r] in mapped and v)
    kbody = struct.pack(">HHHH", len(pairs), *bsearch(len(pairs), 6)) + b"".join(struct.pack(">HHh", l, r, v) for l, r, v in pairs)
    kern = struct.pack(">HHHHH", 0, 1, 0, 6 + len(kbody), 1) + kbody
    tables = [("cmap", cmap), ("glyf", bytes(glyf)), ("head", head), ("hhea", hhea), ("hmtx", hmtx),
              ("kern", kern), ("loca", bytes(loca)), ("maxp", maxp)]
    if web:
        o = f.tables["OS/2"]
        post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
        base = path.rsplit("/", 1)[-1]
        style = ("Bold " if "Bold" in base else "") + ("Italic" if "Italic" in base else "Oblique" if "Oblique" in base else "")
        style = style.strip() or "Regular"
        if source_of(path) == "liberation" and any(r in web.lower() for r in RESERVED):
            raise SystemExit("gen-font: a modified copy of the Liberation fonts may not be named %s (OFL 1.1, 3)" % web)
        tables += [("OS/2", f.d[o[0]:o[0] + o[1]]), ("name", name_table(web, style, NOTICES[source_of(path)])), ("post", post)]
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
    return bytes(out + bodies), len(gids), len(pairs), len(cps)


# the built-in faces that are fonts of the system fallback's: each one's PostScript name
SAME = {"LiberationSerif": 0, "LiberationSans": 4, "LiberationMono": 8, "DejaVuSans": 12, "DejaVuSans-Bold": 13,
        "DejaVuSerif": 16, "DejaVuSerif-Bold": 17, "DejaVuSansMono": 20, "DejaVuSansMono-Bold": 21}


def system_fallback():
    """Chromium's system fallback on this machine: gfx::GetFallbackFontForChar takes the
    first font of fontconfig's order for a pattern of no family in its locale (en-US) that
    has the code point (measured: tests/pages/scripts.html), and Blink's emoji fallback the
    font fontconfig gives the emoji locale (und-Zsye). Each (path, index in its
    collection, PostScript name); and the emoji font's place among them"""
    def fc(*args):
        return subprocess.run(["fc-match"] + list(args) + ["--format", "%{file}|%{index}|%{postscriptname}\n"],
                              capture_output=True, text=True, check=True).stdout.split("\n")
    fonts = []
    for line in fc("-s", ":lang=en-us"):
        if line.strip():
            path, idx, ps = line.split("|")
            fonts.append((path, int(idx.split(",")[0] or 0), ps))
    path, idx, ps = fc(":lang=und-zsye")[0].split("|")
    emoji = (path, int(idx.split(",")[0] or 0), ps)
    if emoji not in fonts:
        fonts.append(emoji)
    return fonts, fonts.index(emoji)


def ucd_set(prop):
    """the code points that have the binary Unicode property prop, as perl's Unicode::UCD
    has them"""
    out = subprocess.run(["perl", "-MUnicode::UCD=prop_invlist", "-e", 'print join(",", prop_invlist("%s")), "\\n"' % prop],
                         capture_output=True, text=True, check=True).stdout
    inv = [int(x) for x in out.strip().split(",") if x]
    cps = set()
    for k in range(0, len(inv), 2):
        cps.update(range(inv[k], inv[k + 1] if k + 1 < len(inv) else 0x110000))
    return cps


def ucd_ranges(props, minus_props=(), minus=()):
    """the code points that have any of the binary Unicode properties props, but none of
    minus_props, and are not in minus: [first, last] each"""
    cps = set()
    for prop in props:
        cps |= ucd_set(prop)
    for prop in minus_props:
        cps -= ucd_set(prop)
    cps -= set(minus)
    ranges = []
    for c in sorted(cps):
        if ranges and ranges[-1][1] == c - 1:
            ranges[-1][1] = c
        else:
            ranges.append([c, c])
    return ranges


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--web":
        blob, _, _, _ = subset(sys.argv[2], sys.argv[4])
        with open(sys.argv[3], "wb") as out:
            out.write(woff(blob) if sys.argv[3].endswith(".woff") else blob)
        return
    dirs = {"dejavu": sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu",
            "liberation": sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/truetype/liberation"}
    cost = {}
    faces = [(name, src, file, subset(dirs[src] + "/" + file, None, cost)) for name, src, file in FACES]
    # what each block costs the twenty faces: its code points' glyphs (a component counted
    # with the first code point that needs it), their cmap entries and advances
    for title in dict.fromkeys(t for t, _, _ in BLOCKS):
        n = sum(cost.get(c, 0) for t, a, b in BLOCKS if t == title for c in range(a, b + 1))
        sys.stderr.write("gen-font: %-26s %8d bytes before packing\n" % (title, n))
    w = sys.stdout.write
    w("/* src/paint/font_data.h -- generated by tools/gen-font.py from the TrueType files of the\n")
    w("   Liberation fonts (Serif, Sans, Mono: regular, bold, italic, bold italic) and the DejaVu\n")
    w("   fonts (Sans, Serif: regular and bold; Sans Mono: regular, bold, oblique, bold oblique): for\n")
    w("   each face a TrueType font of the code points the script lists (cmap, glyf, head, hhea,\n")
    w("   hmtx, kern, loca, maxp), packed (LZ77 in LZ4's tokens: tools/gen-font.py's lz_pack),\n")
    w("   base64, in pieces of 96 characters (an array of short literals, which cicilang compiles\n")
    w("   in little memory; one long literal of concatenated pieces it does not:\n")
    w("   toolchain/REPORT.md, finding 21). Do not edit; run the script. src/paint/font.c reads\n")
    w("   them as it reads a page's web fonts.\n\n")
    w("   Each face is a copy of part of its Font Software and carries its notice, below: the\n")
    w("   Liberation faces' data is under the SIL Open Font License 1.1, not Ambitious's license;\n")
    w("   the DejaVu faces' under the Bitstream Vera license. Neither is presented under its\n")
    w("   fonts' names: the faces carry no name table.\n\n")
    for src, title in (("liberation", "The Liberation faces"), ("dejavu", "The DejaVu faces")):
        w("   %s' notice:\n\n" % title)
        for line in NOTICES[src].split("\n"):
            w(("   " + line).rstrip() + "\n")
        w("\n")
    w("*/\n")
    w("#define AMB_FONT_FACES %d\n" % len(faces))
    order, emoji = system_fallback()
    # the files: the system fallback's, then the built-in fonts' own files it has not
    # (which back a built-in face for a code point its subset has not, as Chromium draws
    # those from the face's font)
    files = list(order)
    own = []
    for name, src, file in FACES:
        path = dirs[src] + "/" + file
        k = next((i for i, (p, x, _) in enumerate(files) if p == path and x == 0), -1)
        if k < 0:
            files.append((path, 0, file.rsplit(".", 1)[0]))
            k = len(files) - 1
        own.append(k)
    same = {}
    for face, k in zip((0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 20, 21, 22, 23), own):
        same.setdefault(k, face)
    w("\n/* the font files Ambitious reads at run time, on the machine this was made on (its path,\n")
    w("   its index in a collection, the built-in face it is the font of, -1 for none): the\n")
    w("   fonts of Chromium's system fallback -- fontconfig's order for a pattern of no family\n")
    w("   in the locale en-US (amb_sys_order), the first that has a code point drawing it, and\n")
    w("   the one fontconfig gives emoji (lang und-Zsye), which Blink's emoji fallback takes --\n")
    w("   then the built-in fonts' own files (amb_font_file: each built-in font's), from which\n")
    w("   a code point a built-in face's subset has not is drawn */\n")
    w("#define AMB_SYS_FONTS %d\n" % len(files))
    w("static const char *const amb_sys_path[AMB_SYS_FONTS] = {\n")
    for path, idx, ps in files:
        w('    "%s", /* %s */\n' % (path, ps))
    w("};\n")
    w("static const uint8_t amb_sys_ttc[AMB_SYS_FONTS] = { %s };\n" % ", ".join("%d" % idx for _, idx, _ in files))
    w("static const int8_t amb_sys_same[AMB_SYS_FONTS] = { %s };\n" % ", ".join("%d" % same.get(k, -1) for k in range(len(files))))
    w("#define AMB_SYS_ORDER %d\n" % len(order))
    w("static const uint8_t amb_sys_order[AMB_SYS_ORDER] = { %s };\n" % ", ".join("%d" % k for k in range(len(order))))
    w("#define AMB_SYS_EMOJI %d\n" % emoji)
    w("static const uint8_t amb_font_file[AMB_FONT_FACES] = { %s };\n" % ", ".join("%d" % k for k in own))
    # the code points Blink's emoji fallback takes before the others' (its emoji
    # presentation scanner's emoji: Emoji_Presentation and Emoji_Modifier_Base), and those
    # HarfBuzz hides (Default_Ignorable_Code_Point)
    for title, name, count, props, minus_props, minus in (
            ("the code points Blink takes the emoji font for first (Emoji_Presentation and\n"
             "   Emoji_Modifier_Base, not Emoji_Modifier: a skin tone alone is text, measured", "amb_emoji", "AMB_EMOJI_RANGES",
             ("Emoji_Presentation", "Emoji_Modifier_Base"), ("Emoji_Modifier",), ()),
            ("the code points HarfBuzz draws as nothing, with no advance\n   (Default_Ignorable_Code_Point but the "
             "Hangul fillers, which Chromium draws, measured", "amb_ignore", "AMB_IGNORE_RANGES",
             ("Default_Ignorable_Code_Point",), (), (0x115F, 0x1160, 0x3164, 0xFFA0))):
        ranges = ucd_ranges(props, minus_props, minus)
        w("\n/* %s, perl's Unicode::UCD): first, last, ... */\n" % title)
        w("#define %s %d\n" % (count, len(ranges)))
        w("static const uint32_t %s[2 * %s] = {\n" % (name, count))
        for k in range(0, len(ranges), 8):
            w("    %s,\n" % ", ".join("0x%X, 0x%X" % (a, b) for a, b in ranges[k:k + 8]))
        w("};\n")
    w("\n/* the faces' fonts, packed, base64, each face's pieces after its own; amb_font_piece\n")
    w("   below says where each begins */\n")
    w("static const char *const amb_font_b64[] = {\n")
    starts, sizes = [], []
    pieces = 0
    total = 0
    for name, src, file, (blob, n, npairs, ncps) in faces:
        packed = lz_pack(blob)
        if lz_unpack(packed) != blob:
            raise SystemExit("gen-font: %s does not unpack to itself" % file)
        sizes.append((len(blob), len(packed)))
        text = base64.b64encode(packed).decode("ascii")
        starts.append(pieces)
        total += len(text)
        w("    /* %s, %s (%s's notice): %d code points of the %d listed, %d glyphs, %d kerning pairs, %d bytes,\n"
          "       %d packed */\n" % (name, file, "Liberation" if src == "liberation" else "DejaVu", ncps, len(CODEPOINTS), n,
                                       npairs, len(blob), len(packed)))
        for i in range(0, len(text), 96):
            w('    "%s",\n' % text[i:i + 96])
            pieces += 1
    starts.append(pieces)
    w("};\n")
    w("\n/* each face's first piece in amb_font_b64, then the end of the last */\n")
    w("static const uint32_t amb_font_piece[%d] = { %s };\n" % (len(starts), ", ".join("%d" % s for s in starts)))
    w("/* each face's font: its size in bytes, and packed */\n")
    w("static const uint32_t amb_font_size[%d] = { %s };\n" % (len(faces), ", ".join("%d" % a for a, _ in sizes)))
    w("static const uint32_t amb_font_packed[%d] = { %s };\n" % (len(faces), ", ".join("%d" % b for _, b in sizes)))
    sys.stderr.write("gen-font: %d faces, %d bytes, %d packed, %d in base64\n" % (len(faces),
                     sum(len(blob) for _, _, _, (blob, _, _, _) in faces), total * 3 // 4, total))


if __name__ == "__main__":
    main()
