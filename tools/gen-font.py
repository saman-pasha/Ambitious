#!/usr/bin/env python3
"""tools/gen-font.py -- src/paint/font_data.h, the outlines the painter draws text with.

Subsets of the Liberation fonts (Serif, Sans and Mono: the faces Chromium draws
serif, sans-serif, Times New Roman, Arial and Courier New with on a Linux machine
with fontconfig's metric aliases), each regular, bold, italic and bold italic, and of
the DejaVu fonts (Sans, Serif and Sans Mono: monospace's face there, and the faces
their own names name), each regular and bold, and Sans Mono's oblique and bold
oblique (Sans and Serif have no italic face on such a machine: Chromium slants their
upright ones), read from their TrueType files by this script's own reader of the tables
it needs (head, hhea, hmtx, maxp, loca, glyf, cmap, kern, GDEF, GSUB, GPOS) and written
as C: for each face, a TrueType font of the code points the script lists (each face has
those its file has), packed (lz_pack: LZ77 in LZ4's byte-aligned tokens, which font.c
unpacks at a few instructions a byte, where deflate's bits cost it some hundred), base64
-- the tables the painter reads (cmap format 4, glyf, head, hhea, hmtx, kern format 0,
loca, maxp, and the layout tables GDEF, GSUB and GPOS), so that src/paint/font.c reads
the built-in faces as it reads a page's web fonts. A composite glyph stays a composite,
its components in the subset (none of these faces' glyphs here places one by points).
A code point a face has no glyph for but has every part of the canonical decomposition
of, HarfBuzz draws with those parts in that face (hb-ot-shape-normalize: DejaVu Sans
Mono's Vietnamese with two marks, Liberation's arrows and relations with a stroke, the
Kelvin and Angstrom signs), so the subset has it: a singleton's as its part's glyph, a
base and marks as a composite of them, each mark where the face's GPOS mark and mkmk
lookups attach it (else where HarfBuzz leaves it, past the base), the base's advance
the glyph's. The kerning pairs are the subset's code points' (HarfBuzz applies them to a
run whose script's GPOS has no kern feature: Liberation's Hebrew). The layout tables keep
the features HarfBuzz applies by default (src/paint/shape.c applies them as it does) and
the lookups they use, over the subset's glyphs and those the substitutions can put in their
place (a ligature, a joining form), which join the subset. What each block of code points
costs the faces is written on stderr.

    python3 tools/gen-font.py [DEJAVUDIR [LIBERATIONDIR]] > src/paint/font_data.h
    python3 tools/gen-font.py --web FONT.ttf OUT.ttf|OUT.otf|OUT.woff FAMILY [truetype|layout|gpos|curs|cff|cid|cff2 [HEX-RANGES]]

The second writes a web font for the fixture pages: the same subset of one
TrueType file as a font Chromium's sanitizer takes (with OS/2, post, name and a
version 1.0 maxp), named FAMILY, its fonts' notice in its name table; as WOFF 1
(its tables stored) when OUT ends in .woff. HEX-RANGES (20-7E,300-36F) take the code
points it has instead of the script's list. With layout, its layout tables too (GDEF,
GSUB, GPOS); with gpos, those and no kern table (its kerning GPOS's alone, as a font
made today has it); with curs, those and a cursive attachment of the script's own
(each glyph's entry and exit at heights of its glyph number's), which no font on the
machine has. With cff, cid or cff2, its Basic Latin
alone, as OpenType with CFF outlines (name-keyed, or CID-keyed) or CFF2 ones, the
glyphs DejaVu's quadratic curves made cubic, exactly, written as Type 2 charstrings
that use what a reader must read (subroutines and their biases, hints and hintmasks,
widths, each curve and line operator, the flex operators, blends, FDSelect), and
glyphs made for the operators the letters do not take (otf, made_glyphs).

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
# a fixture page's font with CFF outlines: Basic Latin alone
WEB_CODEPOINTS = list(range(0x20, 0x7F))

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
    def __init__(self, path, data=None):
        self.path = path
        if data is None:
            with open(path, "rb") as f:
                data = f.read()
        self.d = data
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


def name_table(family, style, notice, extra=()):
    """a name table: the notice, the family, its style, the full and PostScript names, the
    license, and the extra (name ID, text) names"""
    lines = notice.split("\n")
    names = [(0, " ".join(lines[:2])), (1, family), (2, style), (3, family + " " + style), (4, family + " " + style),
             (6, (family + "-" + style).replace(" ", "")), (13, notice)] + list(extra)
    recs, strs = bytearray(), bytearray()
    for nid, text in names:
        u = text.encode("utf-16-be")
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(u), len(strs))
        strs += u
    return struct.pack(">HHH", 0, len(names), 6 + len(recs)) + bytes(recs) + bytes(strs)


def cmap4(pairs):
    """a cmap of one format 4 subtable for (code point, glyph) pairs in order: a segment
    for each run of code points whose glyphs run on with them (and the closing one)"""
    runs = []
    for c, g in pairs:
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
    return struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub


def sfnt(version, tables):
    """an sfnt of (tag, body) tables, in their tags' order, each padded to 4 bytes"""
    tables = sorted(tables, key=lambda t: t[0])
    out = bytearray(struct.pack(">IHHHH", version, len(tables), *bsearch(len(tables), 16)))
    offset = 12 + 16 * len(tables)
    bodies = bytearray()
    for tag, body in tables:
        out += table_record(tag, body, offset + len(bodies))
        bodies += body
        while len(bodies) % 4:
            bodies.append(0)
    return bytes(out + bodies)


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
    flavor = struct.unpack_from(">I", sfnt, 0)[0]
    out = struct.pack(">4sIIHHIHHIIIII", b"wOFF", flavor, total, n, 0, len(sfnt), 1, 0, 0, 0, 0, 0, 0)
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


# ---- the OpenType layout tables (GDEF, GSUB, GPOS): read, kept for a subset's glyphs, written again ----

# the features HarfBuzz applies for Chromium unless a page turns them off (hb-ot-shape.cc's
# common and horizontal features, the direction's, the automatic fractions', and the Arabic
# shaper's), which a subset keeps; the rest (dlig, salt, smcp, ...) only a page's
# font-feature-settings would ask for, which Ambitious does not read
GSUB_KEEP = {"rvrn", "ltra", "ltrm", "rtla", "rtlm", "frac", "numr", "dnom", "ccmp", "locl", "rlig", "calt", "clig", "liga",
             "rclt", "isol", "fina", "fin2", "fin3", "medi", "med2", "init", "stch", "mset"}
GPOS_KEEP = {"kern", "mark", "mkmk", "curs", "dist", "abvm", "blwm"}


def ot_u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def ot_s16(d, o):
    return struct.unpack_from(">h", d, o)[0]


def ot_at(d, base, field):
    """the table an Offset16 at base + field names, from base: its place, or 0 for a null offset"""
    off = ot_u16(d, base + field)
    return base + off if off else 0


def ot_coverage(d, o):
    """a Coverage table's glyphs, in their coverage indexes' order"""
    fmt, n = struct.unpack_from(">HH", d, o)
    if fmt == 1:
        return list(struct.unpack_from(">%dH" % n, d, o + 4))
    out = []
    for k in range(n):
        a, b, _ = struct.unpack_from(">HHH", d, o + 4 + 6 * k)
        out += range(a, b + 1)
    return out


def ot_classdef(d, o):
    """a ClassDef table: {glyph: class} for the glyphs of a class but 0 (none for a null offset)"""
    out = {}
    if not o:
        return out
    fmt = ot_u16(d, o)
    if fmt == 1:
        start, n = struct.unpack_from(">HH", d, o + 2)
        for k, c in enumerate(struct.unpack_from(">%dH" % n, d, o + 6)):
            if c:
                out[start + k] = c
    elif fmt == 2:
        for k in range(ot_u16(d, o + 2)):
            a, b, c = struct.unpack_from(">HHH", d, o + 4 + 6 * k)
            if c:
                for g in range(a, b + 1):
                    out[g] = c
    return out


def ot_vsize(fmt):
    """a ValueRecord's size by its ValueFormat"""
    return 2 * bin(fmt & 0xFF).count("1")


def ot_value(d, o, fmt):
    """a ValueRecord: its x and y placements and advances (its Device tables' offsets passed over:
    HarfBuzz reads them only at a pixel size, which Chromium gives no font)"""
    v, p = [0, 0, 0, 0], o
    for k in range(4):
        if fmt & (1 << k):
            v[k] = ot_s16(d, p)
            p += 2
    return tuple(v)


def ot_anchor(d, o):
    """an Anchor's x and y (any format: HarfBuzz takes a format 2 anchor's point only when hinting)"""
    return struct.unpack_from(">hh", d, o + 2) if o else None


def ot_rule(d, r, chain):
    """a (chained) sequence rule: its backtrack, its input after the first, its lookahead (glyphs
    or classes), and its sequence lookup records (sequence index, lookup index)"""
    back, ahead = [], []
    if chain:
        nb = ot_u16(d, r)
        back = list(struct.unpack_from(">%dH" % nb, d, r + 2))
        p = r + 2 + 2 * nb
        ni = ot_u16(d, p)
        inp = list(struct.unpack_from(">%dH" % max(ni - 1, 0), d, p + 2))
        p += 2 + 2 * max(ni - 1, 0)
        nl = ot_u16(d, p)
        ahead = list(struct.unpack_from(">%dH" % nl, d, p + 2))
        p += 2 + 2 * nl
        nr = ot_u16(d, p)
        p += 2
    else:
        ni, nr = struct.unpack_from(">HH", d, r)
        inp = list(struct.unpack_from(">%dH" % max(ni - 1, 0), d, r + 4))
        p = r + 4 + 2 * max(ni - 1, 0)
    recs = [struct.unpack_from(">HH", d, p + 4 * k) for k in range(nr)]
    return (back, inp, ahead, recs)


def ot_rulesets(d, o, arr, n, chain):
    """the rule sets an array of n offsets (at arr, from o) names, each a list of rules"""
    out = []
    for k in range(n):
        s = ot_at(d, o, arr - o + 2 * k)
        out.append([ot_rule(d, s + ot_u16(d, s + 2 + 2 * j), chain) for j in range(ot_u16(d, s))] if s else [])
    return out


def ot_context(d, o, chain):
    """a contextual or chained contextual subtable (GSUB 5 and 6, GPOS 7 and 8), any format"""
    fmt = ot_u16(d, o)
    sub = {"t": "ctx", "chain": chain, "fmt": fmt}
    if fmt == 1:
        cov = ot_coverage(d, o + ot_u16(d, o + 2))
        sets = ot_rulesets(d, o, o + 6, min(ot_u16(d, o + 4), len(cov)), chain)
        sub["rules"] = {g: rs for g, rs in zip(cov, sets) if rs}
    elif fmt == 2:
        sub["cov"] = ot_coverage(d, o + ot_u16(d, o + 2))
        if chain:
            sub["bcd"], sub["icd"], sub["lcd"] = (ot_classdef(d, ot_at(d, o, k)) for k in (4, 6, 8))
            sub["sets"] = ot_rulesets(d, o, o + 12, ot_u16(d, o + 10), chain)
        else:
            sub["bcd"], sub["icd"], sub["lcd"] = {}, ot_classdef(d, ot_at(d, o, 4)), {}
            sub["sets"] = ot_rulesets(d, o, o + 8, ot_u16(d, o + 6), chain)
    elif fmt == 3:
        if chain:
            p, covs = o + 2, []
            for _ in range(3):
                n = ot_u16(d, p)
                covs.append([ot_coverage(d, o + ot_u16(d, p + 2 + 2 * k)) for k in range(n)])
                p += 2 + 2 * n
            nr = ot_u16(d, p)
            p += 2
        else:
            ni, nr = struct.unpack_from(">HH", d, o + 2)
            covs = [[], [ot_coverage(d, o + ot_u16(d, o + 6 + 2 * k)) for k in range(ni)], []]
            p = o + 6 + 2 * ni
        sub["back"], sub["input"], sub["ahead"] = covs
        sub["records"] = [struct.unpack_from(">HH", d, p + 4 * k) for k in range(nr)]
    else:
        return None
    return sub


def ot_gsub_sub(d, o, t):
    """a GSUB subtable of type t (1 to 6 and 8) as a dict"""
    fmt = ot_u16(d, o)
    if t in (5, 6):
        return ot_context(d, o, t == 6)
    cov = ot_coverage(d, o + ot_u16(d, o + 2))
    if t == 1:
        if fmt == 1:
            delta = ot_s16(d, o + 4)
            return {"t": 1, "map": {g: (g + delta) & 0xFFFF for g in cov}}
        n = ot_u16(d, o + 4)
        return {"t": 1, "map": dict(zip(cov, struct.unpack_from(">%dH" % n, d, o + 6)))}
    if t in (2, 3):
        out = {}
        for k, g in enumerate(cov[:ot_u16(d, o + 4)]):
            s = o + ot_u16(d, o + 6 + 2 * k)
            out[g] = list(struct.unpack_from(">%dH" % ot_u16(d, s), d, s + 2))
        return {"t": t, "map": out}
    if t == 4:
        out = {}
        for k, g in enumerate(cov[:ot_u16(d, o + 4)]):
            s = o + ot_u16(d, o + 6 + 2 * k)
            ligs = []
            for j in range(ot_u16(d, s)):
                lg = s + ot_u16(d, s + 2 + 2 * j)
                glyph, nc = struct.unpack_from(">HH", d, lg)
                ligs.append((list(struct.unpack_from(">%dH" % max(nc - 1, 0), d, lg + 4)), glyph))
            out[g] = ligs
        return {"t": 4, "map": out}
    if t == 8:
        p, covs = o + 4, []
        for _ in range(2):
            n = ot_u16(d, p)
            covs.append([ot_coverage(d, o + ot_u16(d, p + 2 + 2 * k)) for k in range(n)])
            p += 2 + 2 * n
        subs = struct.unpack_from(">%dH" % ot_u16(d, p), d, p + 2)
        return {"t": 8, "map": dict(zip(cov, subs)), "back": covs[0], "ahead": covs[1]}
    return None


def ot_gpos_sub(d, o, t):
    """a GPOS subtable of type t (1 to 8) as a dict"""
    fmt = ot_u16(d, o)
    if t in (7, 8):
        return ot_context(d, o, t == 8)
    if t == 1:
        cov, vf = ot_coverage(d, o + ot_u16(d, o + 2)), ot_u16(d, o + 4)
        if fmt == 1:
            v = ot_value(d, o + 6, vf)
            return {"t": 1, "vf": vf, "map": {g: v for g in cov}}
        return {"t": 1, "vf": vf, "map": {g: ot_value(d, o + 8 + k * ot_vsize(vf), vf) for k, g in enumerate(cov[:ot_u16(d, o + 6)])}}
    if t == 2:
        cov = ot_coverage(d, o + ot_u16(d, o + 2))
        vf1, vf2 = struct.unpack_from(">HH", d, o + 4)
        s1, s2 = ot_vsize(vf1), ot_vsize(vf2)
        sub = {"t": 2, "fmt": fmt, "vf1": vf1, "vf2": vf2}
        if fmt == 1:
            pairs = {}
            for k, g in enumerate(cov[:ot_u16(d, o + 8)]):
                s = o + ot_u16(d, o + 10 + 2 * k)
                row = {}
                for j in range(ot_u16(d, s)):
                    r = s + 2 + j * (2 + s1 + s2)
                    row.setdefault(ot_u16(d, r), (ot_value(d, r + 2, vf1), ot_value(d, r + 2 + s1, vf2)))
                pairs[g] = row
            sub["pairs"] = pairs
            return sub
        c1, c2 = struct.unpack_from(">HH", d, o + 12)
        p, matrix = o + 16, []
        for _ in range(c1):
            row = []
            for _ in range(c2):
                row.append((ot_value(d, p, vf1), ot_value(d, p + s1, vf2)))
                p += s1 + s2
            matrix.append(row)
        sub.update(cov=cov, cd1=ot_classdef(d, ot_at(d, o, 8)), cd2=ot_classdef(d, ot_at(d, o, 10)), matrix=matrix)
        return sub
    if t == 3:
        cov, m = ot_coverage(d, o + ot_u16(d, o + 2)), {}
        for k, g in enumerate(cov[:ot_u16(d, o + 4)]):
            m[g] = (ot_anchor(d, ot_at(d, o, 6 + 4 * k)), ot_anchor(d, ot_at(d, o, 8 + 4 * k)))
        return {"t": 3, "map": m}
    if t in (4, 5, 6):
        mcov, bcov = ot_coverage(d, o + ot_u16(d, o + 2)), ot_coverage(d, o + ot_u16(d, o + 4))
        nc = ot_u16(d, o + 6)
        ma, ba = o + ot_u16(d, o + 8), o + ot_u16(d, o + 10)
        marks, bases = {}, {}
        for k, g in enumerate(mcov[:ot_u16(d, ma)]):
            cls = ot_u16(d, ma + 2 + 4 * k)
            marks[g] = (cls, ot_anchor(d, ot_at(d, ma, 4 + 4 * k)))
        for k, g in enumerate(bcov[:ot_u16(d, ba)]):
            if t == 5:
                la = ot_at(d, ba, 2 + 2 * k)
                bases[g] = [[ot_anchor(d, ot_at(d, la, 2 + 2 * (c * nc + j))) for j in range(nc)] for c in range(ot_u16(d, la))]
            else:
                bases[g] = [ot_anchor(d, ot_at(d, ba, 2 + 2 * (k * nc + j))) for j in range(nc)]
        return {"t": t, "nclass": nc, "marks": marks, "bases": bases}
    return None


def ot_read(f, tag):
    """a font's GSUB or GPOS: its scripts ({tag: (default LangSys, {tag: LangSys})}, a LangSys
    (required feature, [feature indexes])), its features ([(tag, [lookup indexes])]) and its
    lookups ([(type, flag, mark filtering set, [subtables])], extensions resolved); None when it
    has none"""
    if tag not in f.tables:
        return None
    d, g = f.d, f.tables[tag][0]
    sl, fl, ll = g + ot_u16(d, g + 4), g + ot_u16(d, g + 6), g + ot_u16(d, g + 8)

    def langsys(o):
        if not o:
            return None
        req, n = struct.unpack_from(">HH", d, o + 2)
        return (req, list(struct.unpack_from(">%dH" % n, d, o + 6)))
    scripts = {}
    for k in range(ot_u16(d, sl)):
        st = d[sl + 2 + 6 * k:sl + 6 + 6 * k].decode("latin-1")
        s = sl + ot_u16(d, sl + 6 + 6 * k)
        langs = {}
        for j in range(ot_u16(d, s + 2)):
            langs[d[s + 4 + 6 * j:s + 8 + 6 * j].decode("latin-1")] = langsys(ot_at(d, s, 8 + 6 * j))
        scripts[st] = (langsys(ot_at(d, s, 0)), langs)
    feats = []
    for k in range(ot_u16(d, fl)):
        ft = d[fl + 2 + 6 * k:fl + 6 + 6 * k].decode("latin-1")
        fo = fl + ot_u16(d, fl + 6 + 6 * k)
        feats.append((ft, list(struct.unpack_from(">%dH" % ot_u16(d, fo + 2), d, fo + 4))))
    lookups = []
    for k in range(ot_u16(d, ll)):
        lk = ll + ot_u16(d, ll + 2 + 2 * k)
        t, flag, n = struct.unpack_from(">HHH", d, lk)
        mfs = ot_u16(d, lk + 6 + 2 * n) if flag & 0x10 else None
        subs, real = [], t
        for j in range(n):
            s = lk + ot_u16(d, lk + 6 + 2 * j)
            if (tag == "GSUB" and t == 7) or (tag == "GPOS" and t == 9):
                real = ot_u16(d, s + 2)
                s += struct.unpack_from(">I", d, s + 4)[0]
            sub = (ot_gsub_sub if tag == "GSUB" else ot_gpos_sub)(d, s, real)
            if sub is not None:
                sub["gsub"] = tag == "GSUB"
                subs.append(sub)
        lookups.append((real, flag, mfs, subs))
    return {"scripts": scripts, "features": feats, "lookups": lookups}


def ot_read_gdef(f):
    """a font's GDEF: its glyph classes, mark attachment classes and mark glyph sets"""
    if "GDEF" not in f.tables:
        return None
    d, g = f.d, f.tables["GDEF"][0]
    minor = ot_u16(d, g + 2)
    sets = []
    ms = ot_at(d, g, 12) if minor >= 2 else 0
    if ms:
        for k in range(ot_u16(d, ms + 2)):
            sets.append(ot_coverage(d, ms + struct.unpack_from(">I", d, ms + 4 + 4 * k)[0]))
    return {"classes": ot_classdef(d, ot_at(d, g, 4)), "attach": ot_classdef(d, ot_at(d, g, 10)), "sets": sets}


def ot_used(tab, keep):
    """the indexes of the lookups the kept features use, and those their contextual rules call"""
    used = set()
    for tag, idx in tab["features"]:
        if tag in keep:
            used.update(i for i in idx if i < len(tab["lookups"]))
    todo = list(used)
    while todo:
        for sub in tab["lookups"][todo.pop()][3]:
            if sub["t"] != "ctx":
                continue
            recs = sub.get("records", [])
            for rs in list(sub.get("rules", {}).values()) + sub.get("sets", []):
                recs = recs + [r for rule in rs for r in rule[3]]
            for _, li in recs:
                if li < len(tab["lookups"]) and li not in used:
                    used.add(li)
                    todo.append(li)
    return used


def ot_closure(gsub, used, glyphs):
    """the glyphs grown by every glyph the used GSUB lookups can put in their place, until no more
    come (a contextual lookup's own lookups taken as if they applied anywhere)"""
    while True:
        new = set()
        for li in used:
            for sub in gsub["lookups"][li][3]:
                t, m = sub["t"], sub.get("map", {})
                if t in (1, 8):
                    new.update(m[g] for g in glyphs if g in m)
                elif t in (2, 3):
                    for g in glyphs:
                        new.update(m.get(g, ()))
                elif t == 4:
                    for g in glyphs:
                        new.update(lig for comps, lig in m.get(g, ()) if all(c in glyphs for c in comps))
        if new <= glyphs:
            return glyphs
        glyphs |= new


def ot_keep(sub, keep):
    """a subtable's glyphs those of keep alone (a coverage of none: the subtable dropped, None)"""
    t, gsub = sub["t"], sub["gsub"]
    out = dict(sub)
    if t == "ctx":
        if sub["fmt"] == 1:
            out["rules"] = {g: [r for r in rs if all(x in keep for x in r[0] + r[1] + r[2])] for g, rs in sub["rules"].items() if g in keep}
            out["rules"] = {g: rs for g, rs in out["rules"].items() if rs}
            return out if out["rules"] else None
        if sub["fmt"] == 2:
            out["cov"] = [g for g in sub["cov"] if g in keep]
            for k in ("bcd", "icd", "lcd"):
                out[k] = {g: c for g, c in sub[k].items() if g in keep}
            return out if out["cov"] else None
        for k in ("back", "input", "ahead"):
            out[k] = [[g for g in cov if g in keep] for cov in sub[k]]
            if not all(out[k]):
                return None
        return out
    if not gsub and t == 2 and sub["fmt"] == 2:
        out["cov"] = [g for g in sub["cov"] if g in keep]
        out["cd1"] = {g: c for g, c in sub["cd1"].items() if g in keep}
        out["cd2"] = {g: c for g, c in sub["cd2"].items() if g in keep}
        return out if out["cov"] else None
    if not gsub and t == 2:
        out["pairs"] = {g: {h: v for h, v in row.items() if h in keep} for g, row in sub["pairs"].items() if g in keep}
        out["pairs"] = {g: row for g, row in out["pairs"].items() if row}
        return out if out["pairs"] else None
    if not gsub and t in (4, 5, 6):
        out["marks"] = {g: v for g, v in sub["marks"].items() if g in keep}
        out["bases"] = {g: v for g, v in sub["bases"].items() if g in keep}
        return out if out["marks"] and out["bases"] else None
    m = sub["map"]
    if gsub and t == 4:
        out["map"] = {g: [(c, lig) for c, lig in ligs if lig in keep and all(x in keep for x in c)] for g, ligs in m.items() if g in keep}
        out["map"] = {g: ligs for g, ligs in out["map"].items() if ligs}
    elif gsub and t in (2, 3):
        out["map"] = {g: s for g, s in m.items() if g in keep and all(x in keep for x in s)}
    elif gsub:
        out["map"] = {g: s for g, s in m.items() if g in keep and s in keep}
    else:
        out["map"] = {g: v for g, v in m.items() if g in keep}
    if gsub and t == 8:
        out["back"] = [[g for g in cov if g in keep] for cov in sub["back"]]
        out["ahead"] = [[g for g in cov if g in keep] for cov in sub["ahead"]]
        if not all(out["back"]) or not all(out["ahead"]):
            return None
    return out if out["map"] else None


class OTW:
    """a part of an OpenType table being written: its bytes, and its offsets to fill with where
    each part it names goes -- after it, in the order named, from its own start"""
    def __init__(self):
        self.b = bytearray()
        self.links = []

    def u16(self, *vs):
        for v in vs:
            self.b += struct.pack(">H", v & 0xFFFF)
        return self

    def tag(self, t):
        self.b += t.encode("latin-1")
        return self

    def off(self, part, wide=False):
        self.links.append((len(self.b), wide, part))
        self.b += b"\0" * (4 if wide else 2)
        return self

    def bytes(self):
        out = bytearray(self.b)
        for at, wide, part in self.links:
            if part is None:
                continue
            if not wide and len(out) > 0xFFFF:
                raise SystemExit("gen-font: a layout table's offset is past 64K")
            struct.pack_into(">I" if wide else ">H", out, at, len(out))
            out += part.bytes()
        return bytes(out)


def otw_coverage(glyphs):
    """a Coverage table of glyphs, in its smaller format"""
    gl = sorted(set(glyphs))
    runs = []
    for g in gl:
        if runs and runs[-1][1] == g - 1:
            runs[-1][1] = g
        else:
            runs.append([g, g])
    w = OTW()
    if 6 * len(runs) < 2 * len(gl):
        w.u16(2, len(runs))
        k = 0
        for a, b in runs:
            w.u16(a, b, k)
            k += b - a + 1
    else:
        w.u16(1, len(gl), *gl)
    return w


def otw_classdef(cd):
    """a ClassDef table of {glyph: class}, as ranges"""
    runs = []
    for g in sorted(cd):
        if runs and runs[-1][1] == g - 1 and runs[-1][2] == cd[g]:
            runs[-1][1] = g
        else:
            runs.append([g, g, cd[g]])
    w = OTW().u16(2, len(runs))
    for a, b, c in runs:
        w.u16(a, b, c)
    return w


def otw_value(w, v, fmt):
    """a ValueRecord of fmt: its placements and advances, its Device offsets null"""
    for k in range(8):
        if fmt & (1 << k):
            w.u16(v[k] if k < 4 else 0)


def otw_anchor(a):
    return None if a is None else OTW().u16(1, a[0], a[1])


def otw_rule(rule, chain, cls=False):
    back, inp, ahead, recs = rule
    w = OTW()
    if chain:
        w.u16(len(back), *back).u16(len(inp) + 1, *inp).u16(len(ahead), *ahead).u16(len(recs))
    else:
        w.u16(len(inp) + 1, len(recs), *inp)
    for si, li in recs:
        w.u16(si, li)
    return w


def otw_ruleset(rules, chain):
    w = OTW().u16(len(rules))
    for r in rules:
        w.off(otw_rule(r, chain))
    return w


def otw_sub(sub, gsub):
    """a subtable written: GSUB's (gsub) or GPOS's"""
    t, w = sub["t"], OTW()
    if t == "ctx":
        chain = sub["chain"]
        if sub["fmt"] == 1:
            gl = sorted(sub["rules"])
            w.u16(1).off(otw_coverage(gl)).u16(len(gl))
            for g in gl:
                w.off(otw_ruleset(sub["rules"][g], chain))
        elif sub["fmt"] == 2:
            w.u16(2).off(otw_coverage(sub["cov"]))
            if chain:
                w.off(otw_classdef(sub["bcd"])).off(otw_classdef(sub["icd"])).off(otw_classdef(sub["lcd"]))
            else:
                w.off(otw_classdef(sub["icd"]))
            w.u16(len(sub["sets"]))
            for rs in sub["sets"]:
                w.off(otw_ruleset(rs, chain) if rs else None)
        else:
            w.u16(3)
            if chain:
                for k in ("back", "input", "ahead"):
                    w.u16(len(sub[k]))
                    for cov in sub[k]:
                        w.off(otw_coverage(cov))
                w.u16(len(sub["records"]))
            else:
                w.u16(len(sub["input"]), len(sub["records"]))
                for cov in sub["input"]:
                    w.off(otw_coverage(cov))
            for si, li in sub["records"]:
                w.u16(si, li)
        return w
    if gsub:
        m = sub["map"]
        gl = sorted(m)
        if t == 1:
            w.u16(2).off(otw_coverage(gl)).u16(len(gl), *[m[g] for g in gl])
        elif t in (2, 3):
            w.u16(1).off(otw_coverage(gl)).u16(len(gl))
            for g in gl:
                w.off(OTW().u16(len(m[g]), *m[g]))
        elif t == 4:
            w.u16(1).off(otw_coverage(gl)).u16(len(gl))
            for g in gl:
                ls = OTW().u16(len(m[g]))
                for comps, lig in m[g]:
                    ls.off(OTW().u16(lig, len(comps) + 1, *comps))
                w.off(ls)
        elif t == 8:
            w.u16(1).off(otw_coverage(gl))
            for k in ("back", "ahead"):
                w.u16(len(sub[k]))
                for cov in sub[k]:
                    w.off(otw_coverage(cov))
            w.u16(len(gl), *[m[g] for g in gl])
        return w
    if t == 1:
        m, vf = sub["map"], sub["vf"]
        gl = sorted(m)
        w.u16(2).off(otw_coverage(gl)).u16(vf, len(gl))
        for g in gl:
            otw_value(w, m[g], vf)
    elif t == 2 and sub["fmt"] == 1:
        p = sub["pairs"]
        gl = sorted(p)
        w.u16(1).off(otw_coverage(gl)).u16(sub["vf1"], sub["vf2"], len(gl))
        for g in gl:
            ps = OTW().u16(len(p[g]))
            for h in sorted(p[g]):
                ps.u16(h)
                otw_value(ps, p[g][h][0], sub["vf1"])
                otw_value(ps, p[g][h][1], sub["vf2"])
            w.off(ps)
    elif t == 2:
        mx = sub["matrix"]
        w.u16(2).off(otw_coverage(sub["cov"])).u16(sub["vf1"], sub["vf2"])
        w.off(otw_classdef(sub["cd1"])).off(otw_classdef(sub["cd2"])).u16(len(mx), len(mx[0]) if mx else 0)
        for row in mx:
            for v1, v2 in row:
                otw_value(w, v1, sub["vf1"])
                otw_value(w, v2, sub["vf2"])
    elif t == 3:
        m = sub["map"]
        gl = sorted(m)
        w.u16(1).off(otw_coverage(gl)).u16(len(gl))
        for g in gl:
            w.off(otw_anchor(m[g][0])).off(otw_anchor(m[g][1]))
    elif t in (4, 5, 6):
        marks, bases, nc = sub["marks"], sub["bases"], sub["nclass"]
        ml, bl = sorted(marks), sorted(bases)
        w.u16(1).off(otw_coverage(ml)).off(otw_coverage(bl)).u16(nc)
        ma = OTW().u16(len(ml))
        for g in ml:
            ma.u16(marks[g][0]).off(otw_anchor(marks[g][1]))
        ba = OTW().u16(len(bl))
        for g in bl:
            if t == 5:
                la = OTW().u16(len(bases[g]))
                for comp in bases[g]:
                    for a in comp:
                        la.off(otw_anchor(a))
                ba.off(la)
            else:
                for a in bases[g]:
                    ba.off(otw_anchor(a))
        w.off(ma).off(ba)
    return w


def ot_subset(tab, keep, remap, keep_feats, gsub, lookup_map):
    """a GSUB or GPOS restricted to the kept features, the lookups they use (renumbered by
    lookup_map) and the glyphs kept (renumbered by remap): its bytes"""
    def rg(g):
        return remap[g]

    def re_sub(sub):
        """a kept subtable's glyphs (and its rules' lookups) renumbered"""
        t, out = sub["t"], dict(sub)
        if t == "ctx":
            def rule(r, glyphs):
                back, inp, ahead, recs = r
                f = (lambda xs: [rg(x) for x in xs]) if glyphs else list
                return (f(back), f(inp), f(ahead), [(si, lookup_map[li]) for si, li in recs if li in lookup_map])
            if sub["fmt"] == 1:
                out["rules"] = {rg(g): [rule(r, True) for r in rs] for g, rs in sub["rules"].items()}
            elif sub["fmt"] == 2:
                out["cov"] = [rg(g) for g in sub["cov"]]
                for k in ("bcd", "icd", "lcd"):
                    out[k] = {rg(g): c for g, c in sub[k].items()}
                out["sets"] = [[rule(r, False) for r in rs] for rs in sub["sets"]]
            else:
                for k in ("back", "input", "ahead"):
                    out[k] = [[rg(g) for g in cov] for cov in sub[k]]
                out["records"] = [(si, lookup_map[li]) for si, li in sub["records"] if li in lookup_map]
            return out
        if not gsub and t == 2 and sub["fmt"] == 2:
            out["cov"] = [rg(g) for g in sub["cov"]]
            out["cd1"] = {rg(g): c for g, c in sub["cd1"].items()}
            out["cd2"] = {rg(g): c for g, c in sub["cd2"].items()}
            return out
        if not gsub and t == 2:
            out["pairs"] = {rg(g): {rg(h): v for h, v in row.items()} for g, row in sub["pairs"].items()}
            return out
        if t in (4, 5, 6) and not gsub:
            out["marks"] = {rg(g): v for g, v in sub["marks"].items()}
            out["bases"] = {rg(g): v for g, v in sub["bases"].items()}
            return out
        m = sub["map"]
        if gsub and t == 4:
            out["map"] = {rg(g): [([rg(c) for c in comps], rg(lig)) for comps, lig in ligs] for g, ligs in m.items()}
        elif gsub and t in (2, 3):
            out["map"] = {rg(g): [rg(x) for x in s] for g, s in m.items()}
        elif gsub and t in (1, 8):
            out["map"] = {rg(g): rg(s) for g, s in m.items()}
        else:
            out["map"] = {rg(g): v for g, v in m.items()}
        if t == 8 and gsub:
            out["back"] = [[rg(g) for g in cov] for cov in sub["back"]]
            out["ahead"] = [[rg(g) for g in cov] for cov in sub["ahead"]]
        return out
    feats, feat_map = [], {}
    for k, (tag, idx) in enumerate(tab["features"]):
        if tag in keep_feats:
            feat_map[k] = len(feats)
            feats.append((tag, [lookup_map[i] for i in idx if i in lookup_map]))

    def langsys(ls):
        if ls is None:
            return None
        req, idx = ls
        return OTW().u16(0, feat_map.get(req, 0xFFFF) if req != 0xFFFF else 0xFFFF).u16(
            len([i for i in idx if i in feat_map]), *[feat_map[i] for i in idx if i in feat_map])
    sl = OTW().u16(len(tab["scripts"]))
    for st in sorted(tab["scripts"]):
        default, langs = tab["scripts"][st]
        s = OTW().off(langsys(default)).u16(len(langs))
        for lt in sorted(langs):
            s.tag(lt).off(langsys(langs[lt]))
        sl.tag(st).off(s)
    fl = OTW().u16(len(feats))
    for tag, idx in feats:
        fl.tag(tag).off(OTW().u16(0, len(idx), *idx))
    ll = OTW().u16(len(lookup_map))
    for li in sorted(lookup_map, key=lambda i: lookup_map[i]):
        t, flag, mfs, subs = tab["lookups"][li]
        kept = [re_sub(s) for s in (ot_keep(s, keep) for s in subs) if s is not None]
        lk = OTW().u16(t, flag, len(kept))
        for s in kept:
            lk.off(otw_sub(s, gsub))
        if flag & 0x10:
            lk.u16(mfs)
        ll.off(lk)
    return OTW().u16(1, 0).off(sl).off(fl).off(ll).bytes()


def ot_subset_gdef(gdef, keep, remap):
    """a GDEF restricted to the glyphs kept, renumbered: its bytes"""
    def cd(m):
        return {remap[g]: c for g, c in m.items() if g in keep}
    w = OTW().u16(1, 2 if gdef["sets"] else 0).off(otw_classdef(cd(gdef["classes"])) if gdef["classes"] else None)
    w.u16(0, 0).off(otw_classdef(cd(gdef["attach"])) if gdef["attach"] else None)
    if gdef["sets"]:
        ms = OTW().u16(1, len(gdef["sets"]))
        for cov in gdef["sets"]:
            ms.off(otw_coverage([remap[g] for g in cov if g in keep]), wide=True)
        w.off(ms)
    return w.bytes()


def subset(path, web=None, cps_of=None, only=None, layout=True, kern=True, curs=False):
    """a TrueType font of the code points the script lists (or only those of only): the
    tables the painter reads (the layout tables with layout; no kern table without kern; a
    cursive attachment of its own with curs); for the web (web, its family's name), the
    tables a sanitizer asks for besides. With cps_of, each glyph's bytes are added to the
    block of the first code point that needs them (cps_of: block of a code point -> its
    bytes)"""
    f = Font(path)
    listed = only or CODEPOINTS
    # a code point the font has no glyph for but has its canonical decomposition, HarfBuzz
    # draws with the decomposition's glyphs (hb-ot-shape-normalize): a singleton's as its
    # one part's glyph; a base and its marks as a glyph made of them, each mark where GPOS
    # attaches it (to the base, or to the mark before it), else where the pen is, past the
    # base, the base's advance theirs (the marks' are zeroed)
    cpmap = dict(f.cmap)
    to_base, to_mark = gpos_marks(f)
    for c in listed:
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
    cps = [c for c in listed if c in cpmap]
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
    # the layout tables (layout): the glyphs the features HarfBuzz applies by default can put in
    # the place of the subset's (a ligature, a contextual alternate, a joining form) join it
    gsub = ot_read(f, "GSUB") if layout else None
    gpos = ot_read(f, "GPOS") if layout else None
    gdef = ot_read_gdef(f) if layout else None
    if gsub:
        sub_used = ot_used(gsub, GSUB_KEEP)
        for g in sorted(ot_closure(gsub, sub_used, {g for g in gids if not isinstance(g, tuple)})):
            add(g)
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
    # the left side bearings: for the web each glyph's xMin, as a font's are (FreeType places a
    # TrueType glyph by its bearing, moving its outline by the difference); the built-in faces'
    # 0, which the painter does not read
    bearings = [struct.unpack(">h", g[2:4])[0] if web and len(g) >= 10 else 0 for g in glyphs]
    hmtx = b"".join(struct.pack(">Hh", a, b) for a, b in zip(advances, bearings))
    cmap = cmap4([(c, newid[cpmap[c]]) for c in cps])
    tables = [("cmap", cmap), ("glyf", bytes(glyf)), ("head", head), ("hhea", hhea), ("hmtx", hmtx),
              ("loca", bytes(loca)), ("maxp", maxp)]
    # GSUB, GPOS and GDEF: the kept features, the lookups they use, the glyphs of the subset
    keep = {g for g in newid if not isinstance(g, tuple)}
    remap = {g: newid[g] for g in keep}
    if curs and gpos:
        # a cursive attachment the font has not (no font on the machine has one): each base
        # glyph's entry at its advance and its exit at its origin, at heights that step with its
        # glyph's number, so that a joined run climbs and falls as HarfBuzz chains it (right to
        # left, the last glyph on the baseline), a feature of every language system
        classes = gdef["classes"] if gdef else {}
        anchors = {g: ((f.advance(g), 100 + 150 * (g % 4)), (0, 50 + 120 * (g % 3)))
                   for g in sorted(keep) if classes.get(g, 1) != 3 and f.advance(g) > 0}
        gpos["lookups"].append((3, 0x0009, None, [{"t": 3, "map": anchors, "gsub": False}]))
        gpos["features"].append(("curs", [len(gpos["lookups"]) - 1]))
        for default, langs in gpos["scripts"].values():
            for ls in [default] + list(langs.values()):
                if ls is not None:
                    ls[1].append(len(gpos["features"]) - 1)
    if gpos:
        used = ot_used(gpos, GPOS_KEEP)
        tables.append(("GPOS", ot_subset(gpos, keep, remap, GPOS_KEEP, False, {li: k for k, li in enumerate(sorted(used))})))
    if gsub:
        tables.append(("GSUB", ot_subset(gsub, keep, remap, GSUB_KEEP, True, {li: k for k, li in enumerate(sorted(sub_used))})))
    if gdef:
        tables.append(("GDEF", ot_subset_gdef(gdef, keep, remap)))
    # kern: format 0, the pairs of the subset's code points' glyphs, in order (HarfBuzz applies
    # them to a run whose script's GPOS features have no kern: Liberation's Hebrew)
    mapped = {newid[cpmap[c]] for c in cps}
    pairs = sorted((newid[l], newid[r], v) for (l, r), v in kern_pairs(f).items()
                   if l in newid and r in newid and newid[l] in mapped and newid[r] in mapped and v)
    if (pairs and kern) or not layout:
        kbody = struct.pack(">HHHH", len(pairs), *bsearch(len(pairs), 6)) + b"".join(struct.pack(">HHh", l, r, v) for l, r, v in pairs)
        tables.append(("kern", struct.pack(">HHHHH", 0, 1, 0, 6 + len(kbody), 1) + kbody))
    tables.sort(key=lambda t: t[0])
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


# ---- the fixture pages' fonts with CFF outlines (--web ... cff|cff2|cid) ----

# Type 2 charstring operators (12 x as 1200 + x) and DICT operators by name
T2 = {"hstem": 1, "vstem": 3, "vmoveto": 4, "rlineto": 5, "hlineto": 6, "vlineto": 7, "rrcurveto": 8, "callsubr": 10,
      "return": 11, "endchar": 14, "vsindex": 15, "blend": 16, "hstemhm": 18, "hintmask": 19, "cntrmask": 20,
      "rmoveto": 21, "hmoveto": 22, "vstemhm": 23, "rcurveline": 24, "rlinecurve": 25, "vvcurveto": 26,
      "hhcurveto": 27, "callgsubr": 29, "vhcurveto": 30, "hvcurveto": 31, "and": 1203, "or": 1204, "not": 1205,
      "abs": 1209, "add": 1210, "sub": 1211, "div": 1212, "neg": 1214, "eq": 1215, "drop": 1218, "put": 1220,
      "get": 1221, "ifelse": 1222, "mul": 1224, "sqrt": 1226, "dup": 1227, "exch": 1228, "index": 1229,
      "roll": 1230, "hflex": 1234, "flex": 1235, "hflex1": 1236, "flex1": 1237}
DICT = {"FontBBox": 5, "charset": 15, "Encoding": 16, "CharStrings": 17, "Private": 18, "Subrs": 19,
        "vstore": 24, "ROS": 1230, "CIDCount": 1234, "FDArray": 1236, "FDSelect": 1237, "FontName": 1238}


def op_bytes(code):
    return bytes([12, code - 1200]) if code >= 1200 else bytes([code])


def t2_number(v):
    """a charstring operand: an integer in its shortest form, else 16.16 fixed"""
    if v == int(v) and -32768 <= v <= 32767:
        v = int(v)
        if -107 <= v <= 107:
            return bytes([v + 139])
        if 108 <= v <= 1131:
            return bytes([(v - 108 >> 8) + 247, v - 108 & 255])
        if -1131 <= v <= -108:
            return bytes([(-v - 108 >> 8) + 251, -v - 108 & 255])
        return struct.pack(">Bh", 28, v)
    return struct.pack(">Bi", 255, int(round(v * 65536)))


def dict_number(v, wide=False):
    """a DICT operand: an integer (in 5 bytes when wide, an offset whose value comes later)"""
    if not wide and -107 <= v <= 107:
        return bytes([v + 139])
    if not wide and -32768 <= v <= 32767:
        return struct.pack(">Bh", 28, v)
    return struct.pack(">Bi", 29, v)


def cff_dict(entries, wide=()):
    """a DICT of (operator name, operands) entries; the operators in wide take theirs in 5
    bytes each"""
    out = bytearray()
    for name, args in entries:
        for a in args:
            out += dict_number(a, name in wide)
        out += op_bytes(DICT[name])
    return bytes(out)


def cff_index(items, wide=False):
    """an INDEX of byte strings (CFF2's: wide, its count in 4 bytes)"""
    count = struct.pack(">I" if wide else ">H", len(items))
    if not items:
        return count
    offs = [1]
    for it in items:
        offs.append(offs[-1] + len(it))
    size = 1 if offs[-1] < 0x100 else 2 if offs[-1] < 0x10000 else 3 if offs[-1] < 0x1000000 else 4
    return count + bytes([size]) + b"".join(o.to_bytes(size, "big") for o in offs) + b"".join(items)


def charstring(tokens):
    """a charstring's bytes: numbers, operators by name, and hintmask's or cntrmask's
    (name, mask bytes)"""
    out = bytearray()
    for t in tokens:
        if isinstance(t, tuple):
            out += op_bytes(T2[t[0]]) + t[1]
        elif isinstance(t, str):
            out += op_bytes(T2[t])
        else:
            out += t2_number(t)
    return bytes(out)


def cubic_contours(contours):
    """TrueType's quadratic contours as cubic ones, exactly (a quadratic's controls 2/3 of
    the way from its ends to its control): each contour's start and segments, ("l", x, y)
    or ("c", x1, y1, x2, y2, x, y), in Fractions; a contour's closing line left out"""
    from fractions import Fraction as Q
    out = []
    for c in contours:
        if not c:
            continue
        pts = [(Q(x), Q(y), on) for x, y, on in c]
        if not any(on for _, _, on in pts):
            pts = [((pts[-1][0] + pts[0][0]) / 2, (pts[-1][1] + pts[0][1]) / 2, 1)] + pts
        else:
            k = next(k for k in range(len(pts)) if pts[k][2])
            pts = pts[k:] + pts[:k]
        sx, sy = pts[0][0], pts[0][1]
        segs, cx, cy, ctrl = [], sx, sy, None

        def quad(x0, y0, q, x, y):
            return ("c", x0 + (q[0] - x0) * 2 / 3, y0 + (q[1] - y0) * 2 / 3, x + (q[0] - x) * 2 / 3, y + (q[1] - y) * 2 / 3, x, y)
        for x, y, on in pts[1:] + [pts[0]]:
            if on:
                segs.append(("l", x, y) if ctrl is None else quad(cx, cy, ctrl, x, y))
                cx, cy, ctrl = x, y, None
            else:
                if ctrl is not None:
                    mx, my = (ctrl[0] + x) / 2, (ctrl[1] + y) / 2
                    segs.append(quad(cx, cy, ctrl, mx, my))
                    cx, cy = mx, my
                ctrl = (x, y)
        if segs and segs[-1][0] == "l" and segs[-1][1:] == (sx, sy):
            segs.pop()
        out.append(((sx, sy), segs))
    return out


def deltas(seg, x, y):
    """a segment's operands, relative: a line's (dx, dy); a curve's six"""
    if seg[0] == "l":
        return [seg[1] - x, seg[2] - y]
    return [seg[1] - x, seg[2] - y, seg[3] - seg[1], seg[4] - seg[2], seg[5] - seg[3], seg[6] - seg[4]]


def curve_tokens(d):
    """a curve's operands and operator: the briefest of hhcurveto, vvcurveto, hvcurveto,
    vhcurveto and rrcurveto that takes it"""
    dx1, dy1, dx2, dy2, dx3, dy3 = d
    if dy1 == 0 and dy3 == 0:
        return [dx1, dx2, dy2, dx3, "hhcurveto"]
    if dx1 == 0 and dx3 == 0:
        return [dy1, dx2, dy2, dy3, "vvcurveto"]
    if dy3 == 0:
        return [dy1, dx1, dx2, dy2, dx3, "hhcurveto"]
    if dx3 == 0:
        return [dx1, dy1, dx2, dy2, dy3, "vvcurveto"]
    if dy1 == 0:
        return [dx1, dx2, dy2, dy3, dx3, "hvcurveto"]
    if dx1 == 0:
        return [dy1, dx2, dy2, dx3, dy3, "vhcurveto"]
    return d + ["rrcurveto"]


def flex_tokens(a, b, generic):
    """two curves' operands (six each) as a flex operator's -- hflex, hflex1 or flex1 where
    they fit, else, when generic, flex -- or None"""
    d = [a[0:2], a[2:4], a[4:6], b[0:2], b[2:4], b[4:6]]
    if d[0][1] == 0 and d[2][1] == 0 and d[3][1] == 0 and d[5][1] == 0 and d[4][1] == -d[1][1]:
        return [d[0][0], d[1][0], d[1][1], d[2][0], d[3][0], d[4][0], d[5][0], "hflex"]
    if d[2][1] == 0 and d[3][1] == 0 and d[0][1] + d[1][1] + d[4][1] + d[5][1] == 0:
        return [d[0][0], d[0][1], d[1][0], d[1][1], d[2][0], d[3][0], d[4][0], d[4][1], d[5][0], "hflex1"]
    sx, sy = sum(p[0] for p in d[:5]), sum(p[1] for p in d[:5])
    if abs(sx) > abs(sy) and d[5][1] == -sy:
        return [v for p in d[:5] for v in p] + [d[5][0], "flex1"]
    if abs(sx) <= abs(sy) and d[5][0] == -sx:
        return [v for p in d[:5] for v in p] + [d[5][1], "flex1"]
    return a + b + [50, "flex"] if generic else None


def contour_tokens(start, segs, pen, flexes, blend=None):
    """a contour's drawing from the pen: its moveto (hmoveto, vmoveto or rmoveto), then its
    segments, each pair of curves that fits a flex operator as one (and, with flexes, any
    other pair of curves as flex), a curve then a line as rcurveline, a line then a curve
    as rlinecurve, a line alone as hlineto, vlineto or rlineto. With blend (CFF2: the
    deltas of a region or two), the moveto's and every third line's operands blended.
    Its tokens, and the pen after it"""
    x, y = pen
    dx, dy = start[0] - x, start[1] - y
    if blend:
        toks = [dx, dy] + [v for k in range(blend) for v in (40 + 10 * k, -30)] + [2, "blend", "rmoveto"]
    elif dy == 0:
        toks = [dx, "hmoveto"]
    elif dx == 0:
        toks = [dy, "vmoveto"]
    else:
        toks = [dx, dy, "rmoveto"]
    x, y = start
    i = 0
    while i < len(segs):
        s = segs[i]
        d = deltas(s, x, y)
        nxt = segs[i + 1] if i + 1 < len(segs) else None
        if s[0] == "c" and nxt and nxt[0] == "c":
            f = flex_tokens(d, deltas(nxt, s[5], s[6]), flexes and i % 4 == 0)
            if f:
                toks += f
                x, y = nxt[5], nxt[6]
                i += 2
                continue
        if s[0] == "c" and nxt and nxt[0] == "l" and i % 3 == 0:
            toks += d + deltas(nxt, s[5], s[6]) + ["rcurveline"]
            x, y = nxt[1], nxt[2]
            i += 2
            continue
        if s[0] == "l" and nxt and nxt[0] == "c" and i % 3 == 1:
            toks += d + deltas(nxt, s[1], s[2]) + ["rlinecurve"]
            x, y = nxt[5], nxt[6]
            i += 2
            continue
        if s[0] == "l":
            if blend and i % 3 == 2:
                toks += d + [v for k in range(blend) for v in (20 + 5 * k, 15)] + [2, "blend", "rlineto"]
            elif d[1] == 0:
                toks += [d[0], "hlineto"]
            elif d[0] == 0:
                toks += [d[1], "vlineto"]
            else:
                toks += d + ["rlineto"]
            x, y = s[1], s[2]
        else:
            toks += curve_tokens(d)
            x, y = s[5], s[6]
        i += 1
    return toks, (x, y)


# glyphs made for the operators the fixture fonts' letters may not use (at U+E000 on): two
# curves each that hflex, hflex1, flex1 and flex take, closed by lines; a glyph whose
# numbers are worked out by the arithmetic operators (CFF only) -- of which Chromium's
# FreeType runs div alone, taking any other for an unknown operator, which clears the
# stack (measured), and its sanitizer refuses a font with index, roll, put or get; each
# with its advance
def made_glyphs(cff2):
    from fractions import Fraction as Q

    def hill(*pts):
        p = [(Q(a), Q(b)) for a, b in pts]
        return [(p[0], [("c",) + p[1] + p[2] + p[3], ("c",) + p[4] + p[5] + p[6], ("l", p[6][0], Q(0)), ("l", p[0][0], Q(0))])]
    glyphs = [
        (hill((100, 100), (300, 100), (400, 1300), (700, 1300), (1000, 1300), (1100, 100), (1300, 100)), 1400),
        (hill((100, 100), (200, 300), (400, 1300), (700, 1300), (1000, 1300), (1200, 400), (1300, 100)), 1400),
        (hill((100, 100), (300, 400), (500, 1200), (700, 1250), (900, 1300), (1100, 300), (1300, 100)), 1400),
        (hill((100, 100), (300, 400), (500, 1200), (700, 1250), (900, 1300), (1100, 300), (1300, 200)), 1400),
    ]
    if cff2:
        return glyphs
    # a frame (100,0)-(1300,1400) about a hole (400,300)-(1000,1100), some of their numbers
    # halves that div works out; then a bar (1500,0)-(1700,1400) whose height is 700 700
    # add -- as FreeType runs it, nothing: the bar has no area -- and one (1500,700)-
    # (1700,1400) after it
    arith = [100, 0, "rmoveto", 2400, 2, "div", "hlineto", 2800, 2, "div", "vlineto", -1200, "hlineto",
             300, -1100, "rmoveto", 1600, 2, "div", "vlineto", 1200, 2, "div", "hlineto", -800, "vlineto",
             500, -300, "rmoveto", 200, "hlineto", 700, 700, "add", "vlineto", -200, "hlineto",
             0, 700, "rmoveto", 200, "hlineto", 700, "vlineto", -200, "hlineto"]
    return glyphs + [(arith, 1800)]


def glyph_programs(f, made, kind):
    """each glyph's charstring as tokens, and its Font DICT; the local Subrs of each Font
    DICT and the global Subrs, as tokens, a subroutine named by ("L", k) or ("G", k), its
    k-th, until its number is known. A glyph's hints: hstemhm and a hintmask (every fourth
    glyph), or vstem and a cntrmask (every fourth, two on), its width first (CFF), its
    contours after the first in subroutines -- a local one, or a global one that calls a
    local one, by turns; a fifth glyph's last ends the glyph itself (endchar, CFF) -- and,
    in CFF2, every third glyph's blends of two regions (vsindex 1), of one for the others"""
    cff2 = kind == "cff2"
    n = f.n_glyphs + len(made)
    local, glob, progs = [[], []], [], []
    end = [] if cff2 else ["return"]
    for g in range(n):
        fd = g % 2 if kind == "cid" else int(g >= n // 2) if cff2 else 0
        outline = cubic_contours(f.contours(g)) if g < f.n_glyphs else made[g - f.n_glyphs][0]
        toks = [] if cff2 else [f.advance(g) if g < f.n_glyphs else made[g - f.n_glyphs][1]]
        drawn = bool(outline) and isinstance(outline[0], tuple)
        if drawn and g % 4 == 0:
            ys = [st[1] for st, _ in outline] + [s[2] if s[0] == "l" else s[6] for _, segs in outline for s in segs]
            lo, hi = int(min(ys)), int(max(ys))
            toks += [lo, 20, max(hi - lo - 40, 1), 20, "hstemhm", int(outline[0][0][0]), 30, ("hintmask", b"\xe0")]
        elif drawn and g % 4 == 2:
            toks += [int(outline[0][0][0]), 30, 400, 30, "vstem", ("cntrmask", b"\xc0")]
        regions = (2 if g % 3 == 0 else 1) if cff2 else 0
        if regions == 2:
            toks += [1, "vsindex"]
        if not drawn:
            toks += outline                                   # its own tokens (the arithmetic glyph), or none
            outline = []
        pen, ended = (0, 0), False
        for k, (start, segs) in enumerate(outline):
            ct, pen = contour_tokens(start, segs, pen, g % 3 != 1 or g >= f.n_glyphs, regions if regions and g % 2 == 0 else None)
            if k == 0:
                toks += ct
            elif k % 2 == 1:
                last = k == len(outline) - 1 and g % 5 == 0 and not cff2
                local[fd].append(ct + (["endchar"] if last else end))
                toks += [("L", len(local[fd]) - 1), "callsubr"]
                ended = last
            else:
                local[fd].append(ct + end)
                glob.append([("L", len(local[fd]) - 1), "callsubr"] + end)
                toks += [("G", len(glob) - 1), "callgsubr"]
        if not cff2 and not ended:
            toks.append("endchar")
        progs.append((toks, fd))
    return progs, local, glob


def cff_table(f, made, kind, family):
    """glyphs of f and made as a CFF table (kind cff: name-keyed; cid: CID-keyed, two Font
    DICTs by FDSelect format 0, the first one's local Subrs and the global Subrs padded
    past 1,240 so that their bias is 1,131) or a CFF2 table (two Font DICTs by FDSelect
    format 3, a variation store of two regions over one axis)"""
    cff2, cid = kind == "cff2", kind == "cid"
    progs, local, glob = glyph_programs(f, made, kind)
    n = len(progs)
    end = [] if cff2 else ["return"]
    pads = 1300 if cid else 0
    lpad = [pads, 0]
    local = [[end] * lpad[fd] + local[fd] for fd in range(2)]
    glob = [end] * pads + glob

    def bias(count):
        return 107 if count < 1240 else 1131 if count < 33900 else 32768

    def resolve(toks, fd):
        out = []
        for t in toks:
            if isinstance(t, tuple) and t[0] == "L":
                out.append(t[1] + lpad[fd] - bias(len(local[fd])))
            elif isinstance(t, tuple) and t[0] == "G":
                out.append(t[1] + pads - bias(len(glob)))
            else:
                out.append(t)
        return charstring(out)
    # a global subroutine calls the local Subrs of the glyphs that call it: one Font DICT's
    owner = {}
    for toks, fd in progs:
        for t in toks:
            if isinstance(t, tuple) and t[0] == "G":
                owner[t[1] + pads] = fd
    css = [resolve(toks, fd) for toks, fd in progs]
    gsubrs = [resolve(t, owner.get(k, 0)) for k, t in enumerate(glob)]
    lsubrs = [[resolve(t, fd) for t in local[fd]] for fd in range(2)]
    nfd = 2 if cid or cff2 else 1
    fdsel = [toks_fd for _, toks_fd in progs]

    def privates(base):
        """each Font DICT's Private DICT and its Subrs, from base: their bytes, and each
        Private DICT's (size, offset)"""
        out, recs = bytearray(), []
        for fd in range(nfd):
            subrs = cff_index(lsubrs[fd], cff2) if lsubrs[fd] else b""
            pd = cff_dict([("Subrs", [0])], wide=("Subrs",)) if subrs else b""
            pd = cff_dict([("Subrs", [len(pd)])], wide=("Subrs",)) if subrs else b""
            recs.append((len(pd), base + len(out)))
            out += pd + subrs
        return bytes(out), recs
    if cff2:
        # header, Top DICT, global Subrs, CharStrings, FDSelect, FDArray, the Private DICTs,
        # the variation store
        regions = struct.pack(">HH", 1, 2) + struct.pack(">hhh", 0, 16384, 16384) + struct.pack(">hhh", -16384, -16384, 0)
        datas = [struct.pack(">HHHH", 0, 0, 1, 0), struct.pack(">HHHHH", 0, 0, 2, 0, 1)]
        head = 2 + 4 + 2 + 4 * len(datas)
        ivs = struct.pack(">HIH", 1, head, len(datas))
        at = head + len(regions)
        for d in datas:
            ivs += struct.pack(">I", at)
            at += len(d)
        ivs += regions + b"".join(datas)
        vstore = struct.pack(">H", len(ivs)) + ivs
        ranges = [(0, 0), (n // 2, 1)]
        fdselect = bytes([3]) + struct.pack(">H", len(ranges)) + b"".join(struct.pack(">HB", a, b) for a, b in ranges) + struct.pack(">H", n)

        def top(cs, fdarray, fds, vs):
            return cff_dict([("CharStrings", [cs]), ("FDArray", [fdarray]), ("FDSelect", [fds]), ("vstore", [vs])],
                            wide=("CharStrings", "FDArray", "FDSelect", "vstore"))
        size = len(top(0, 0, 0, 0))
        gs = cff_index(gsubrs, True)
        charstrings = cff_index(css, True)
        at_cs = 5 + size + len(gs)
        at_fds = at_cs + len(charstrings)
        at_fda = at_fds + len(fdselect)
        fdarray = cff_index([cff_dict([("Private", [0, 0])], wide=("Private",))] * nfd, True)
        privs, recs = privates(at_fda + len(fdarray))
        fdarray = cff_index([cff_dict([("Private", list(r))], wide=("Private",)) for r in recs], True)
        at_vs = at_fda + len(fdarray) + len(privs)
        topd = top(at_cs, at_fda, at_fds, at_vs)
        return bytes([2, 0, 5]) + struct.pack(">H", len(topd)) + topd + gs + charstrings + fdselect + fdarray + privs + vstore
    # CFF: header, Name INDEX, Top DICT INDEX, String INDEX, global Subrs, charset,
    # (FDSelect,) CharStrings, (FDArray,) the Private DICTs
    names = cff_index([family.replace(" ", "").encode("ascii")])
    if cid:
        strings = [b"Adobe", b"Identity"]
        charset = bytes([2]) + struct.pack(">HH", 1, n - 2)
    else:
        strings = [("g%d" % g).encode("ascii") for g in range(1, n)]
        charset = bytes([0]) + b"".join(struct.pack(">H", 391 + g - 1) for g in range(1, n))
    sindex = cff_index(strings)
    gs = cff_index(gsubrs)
    charstrings = cff_index(css)
    fdselect = bytes([0]) + bytes(fdsel) if cid else b""

    def top(cs, cset, extra):
        entries = [("ROS", [391, 392, 0]), ("CIDCount", [n])] if cid else []
        entries += [("charset", [cset]), ("CharStrings", [cs])] + extra
        return cff_index([cff_dict(entries, wide=("charset", "CharStrings", "FDArray", "FDSelect", "Private"))])
    blank = [("FDArray", [0]), ("FDSelect", [0])] if cid else [("Private", [0, 0])]
    tsize = len(top(0, 0, blank))
    at_cset = 4 + len(names) + tsize + len(sindex) + len(gs)
    at_fds = at_cset + len(charset)
    at_cs = at_fds + len(fdselect)
    if cid:
        fdarray = cff_index([cff_dict([("Private", [0, 0])], wide=("Private",))] * nfd)
        privs, recs = privates(at_cs + len(charstrings) + len(fdarray))
        fdarray = cff_index([cff_dict([("Private", list(r))], wide=("Private",)) for r in recs])
        tops = top(at_cs, at_cset, [("FDArray", [at_cs + len(charstrings)]), ("FDSelect", [at_fds])])
        tail = charstrings + fdarray + privs
    else:
        privs, recs = privates(at_cs + len(charstrings))
        tops = top(at_cs, at_cset, [("Private", list(recs[0]))])
        tail = charstrings + privs
    return bytes([1, 0, 4, 4]) + names + tops + sindex + gs + charset + fdselect + tail


def otf(blob, kind, family, notice):
    """the TrueType subset blob as OpenType with CFF outlines (cff_table's kinds), the glyphs
    of made_glyphs added at U+E000 on; a CFF2 font with an fvar of its axis (wght, 100 to
    900, 400 its default and its blends' defaults the outlines)"""
    f = Font(None, blob)
    made = made_glyphs(kind == "cff2")
    n = f.n_glyphs + len(made)
    tab = {tag: f.d[off:off + length] for tag, (off, length) in f.tables.items()}
    advances = [f.advance(g) for g in range(f.n_glyphs)] + [a for _, a in made]
    hhea = tab["hhea"][:34] + struct.pack(">H", n)
    hhea = hhea[:10] + struct.pack(">H", max(advances)) + hhea[12:]
    tables = [("cmap", cmap4(sorted(f.cmap.items()) + [(0xE000 + k, f.n_glyphs + k) for k in range(len(made))])),
              ("head", tab["head"]), ("hhea", hhea), ("hmtx", b"".join(struct.pack(">Hh", a, 0) for a in advances)),
              ("kern", tab["kern"]), ("maxp", struct.pack(">IH", 0x00005000, n)), ("OS/2", tab["OS/2"]),
              ("post", tab["post"]), ("name", name_table(family, "Regular", notice, [(256, "Weight")]))]
    if kind == "cff2":
        tables += [("CFF2", cff_table(f, made, kind, family)),
                   ("fvar", struct.pack(">HHHHHHHH", 1, 0, 16, 2, 1, 20, 0, 8) +
                    b"wght" + struct.pack(">iiiHH", 100 << 16, 400 << 16, 900 << 16, 0, 256))]
    else:
        tables.append(("CFF ", cff_table(f, made, kind, family)))
    return sfnt(0x4F54544F, tables)


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
    if len(sys.argv) in (5, 6, 7) and sys.argv[1] == "--web":
        kind = sys.argv[5] if len(sys.argv) >= 6 else "truetype"
        if kind not in ("truetype", "layout", "gpos", "curs", "cff", "cff2", "cid"):
            raise SystemExit("gen-font: --web's outlines are truetype, layout, gpos, curs, cff, cff2 or cid, not %s" % kind)
        only = None
        if len(sys.argv) == 7:
            only = sorted({c for r in sys.argv[6].split(",") for c in range(int(r.split("-")[0], 16), int(r.split("-")[-1], 16) + 1)})
        if kind in ("truetype", "layout", "gpos", "curs"):
            blob, _, _, _ = subset(sys.argv[2], sys.argv[4], only=only, layout=kind != "truetype", kern=kind not in ("gpos", "curs"),
                                   curs=kind == "curs")
        else:
            blob, _, _, _ = subset(sys.argv[2], sys.argv[4], only=WEB_CODEPOINTS, layout=False)
            blob = otf(blob, kind, sys.argv[4], NOTICES[source_of(sys.argv[2])])
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
    w("   toolchain/REPORT.md, finding 28). Do not edit; run the script. src/paint/font.c reads\n")
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
    # each built-in font's file's widths, which a text control is sized by (Blink's
    # TextFieldIntrinsicInlineSize): OS/2's xAvgCharWidth, and head's xMin and xMax (the
    # subset's own head bounds its glyphs alone), in its units per em
    wide = []
    for name, src, file in FACES:
        src_font = Font(dirs[src] + "/" + file)
        head = src_font.tables["head"][0]
        xmin, xmax = struct.unpack_from(">h", src_font.d, head + 36)[0], struct.unpack_from(">h", src_font.d, head + 40)[0]
        os2 = src_font.tables.get("OS/2")
        avg = struct.unpack_from(">h", src_font.d, os2[0] + 2)[0] if os2 else 0
        wide.append("{ %d, %d, %d }" % (avg, xmin, xmax))
    w("\n/* each built-in font's file's average character width (OS/2's xAvgCharWidth) and its glyphs'\n")
    w("   horizontal bounds (head's xMin and xMax), in its units per em: the widths Blink sizes a\n")
    w("   text control by */\n")
    w("static const int32_t amb_font_wide[AMB_FONT_FACES][3] = {\n    %s\n};\n" % ",\n    ".join(wide))
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
