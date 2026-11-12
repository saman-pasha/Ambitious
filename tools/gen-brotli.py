#!/usr/bin/env python3
"""tools/gen-brotli.py -- src/paint/brotli_dict.h, the tables RFC 7932 gives a Brotli
decoder, which src/paint/brotli.c decodes WOFF2 fonts with: the static dictionary
(Appendix A: 122,784 bytes, the words a stream may refer to) and its words' lengths'
places, its 121 word transforms (Appendix B: a prefix, an operation, a suffix), the
literal context lookup tables (section 7.1), the block lengths' codes (section 6) and the
insert-and-copy commands' (section 5).

    python3 tools/gen-brotli.py BROTLI/c/common > src/paint/brotli_dict.h

BROTLI is a checkout or source distribution of Google's Brotli (these are the same in
every version since 1.0, and in the RFC); the script reads them from its C sources
(common/dictionary.c, transform.c, context.c, constants.c and dec/prefix.h) and checks
them: the dictionary's SHA-256 against the RFC's, its words' places against their
counts, and the small command tables written here against the decoder's own table of
all 704 commands. The dictionary is written in base64 pieces of 96 characters (an array
of short literals: toolchain/REPORT.md, finding 28), with Brotli's notice: the tables are
part of its software, under the MIT license, not Ambitious's.
"""
import base64
import hashlib
import re
import sys

NOTICE = """Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE."""

# the dictionary's size and digest (RFC 7932, Appendix A's): what the source must give
SIZE = 122784
SHA256 = "20e42eb1b511c21806d4d227d07e5dd06877d8ce7b3a817f378f313653f35c70"


def c_string(src):
    """a C string literal's bytes (its escapes: octal, hex, and the named ones)"""
    out, i = bytearray(), 0
    named = {"n": 10, "t": 9, "r": 13, "\\": 92, '"': 34, "'": 39, "0": 0}
    while i < len(src):
        c = src[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        d = src[i + 1]
        if d == "x":
            m = re.match(r"[0-9a-fA-F]{1,2}", src[i + 2:])
            out.append(int(m.group(0), 16))
            i += 2 + len(m.group(0))
        elif d in "01234567":
            m = re.match(r"[0-7]{1,3}", src[i + 1:])
            out.append(int(m.group(0), 8))
            i += 1 + len(m.group(0))
        else:
            out.append(named[d])
            i += 2
    return bytes(out)


def numbers(text, name):
    """the numbers of the C array name's initializer"""
    start = text.index(name)
    body = text[text.index("{", start) + 1:text.index("}", start)]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return body


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: tools/gen-brotli.py BROTLI/c/common > src/paint/brotli_dict.h")
    d = sys.argv[1]
    text = open(d + "/dictionary.c", encoding="ascii").read()
    start = text.index("/* GENERATED CODE START */")
    body = text[text.index("{", start) + 1:text.index("}", start)]
    data = bytes(int(x) for x in re.findall(r"\d+", body))
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("gen-brotli: %s is no Brotli dictionary (%d bytes, sha256 %s)" % (d, len(data), hashlib.sha256(data).hexdigest()))
    # the transforms: each a prefix and a suffix (length-prefixed strings in kPrefixSuffix,
    # found by kPrefixSuffixMap) and an operation (transform.h's enum)
    tr = open(d + "/transform.c", encoding="ascii").read()
    th = open(d + "/transform.h", encoding="ascii").read()
    ops = {m.group(1): int(m.group(2)) for m in re.finditer(r"(BROTLI_TRANSFORM_\w+) = (\d+)", th)}
    ps_src = tr[tr.index("kPrefixSuffix[217] ="):tr.index(";", tr.index("kPrefixSuffix[217] ="))]
    ps = c_string("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', re.sub(r"/\*.*?\*/", "", ps_src, flags=re.S)))) + b"\0"
    psmap = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", numbers(tr, "kPrefixSuffixMap[50]"))]
    triples = []
    for tok in re.findall(r"BROTLI_TRANSFORM_\w+|\d+", numbers(tr, "kTransformsData[]")):
        triples.append(ops[tok] if tok.startswith("BROTLI") else int(tok))
    if len(triples) != 3 * 121 or len(psmap) != 50:
        raise SystemExit("gen-brotli: %s's transforms are not RFC 7932's 121" % d)

    def affix(k):
        s = psmap[k]
        return ps[s + 1:s + 1 + ps[s]]
    ctx = [int(x) for x in re.findall(r"\d+", numbers(open(d + "/context.c", encoding="ascii").read(), "_kBrotliContextLookupTable[2048]"))]
    if len(ctx) != 2048:
        raise SystemExit("gen-brotli: %s's context lookup table is not 2,048 bytes" % d)
    # the dictionary's words by length: how many bits number them, where they begin
    bits = [int(x) for x in re.findall(r"\d+", text[text.index("/* size_bits_by_length */"):text.index("/* offsets_by_length */")])]
    offs = [int(x) for x in re.findall(r"\d+", text[text.index("/* offsets_by_length */"):text.index("/* data_size")])]
    at = 0
    for length in range(32):
        if offs[length] != at:
            raise SystemExit("gen-brotli: %s's dictionary offsets do not add up at length %d" % (d, length))
        at += (1 << bits[length]) * length if bits[length] else 0
    if at != SIZE:
        raise SystemExit("gen-brotli: %s's dictionary words are not %d bytes" % (d, SIZE))
    # a block length's prefix code: each symbol's base and extra bits (constants.c)
    cst = open(d + "/constants.c", encoding="ascii").read()
    cst = cst[cst.index("_kBrotliPrefixCodeRanges["):]
    blocks = [int(x) for x in re.findall(r"\d+", cst[cst.index("=") + 1:cst.index(";")])]
    if len(blocks) != 52:
        raise SystemExit("gen-brotli: %s's block length codes are not 26" % d)
    # the insert-and-copy commands (section 5), from the decoder's table of all 704 (dec/prefix.h):
    # each command's cell picks an insert code's and a copy code's first; its bits 3-5 and
    # 0-2 add to them; a command of the first two cells reuses the last distance. The small
    # tables written here are checked to give every command as the decoder's does
    lut = [[int(x, 0) for x in m.groups()] for m in
           re.finditer(r"\{ (0x[0-9a-f]+), (0x[0-9a-f]+), (-?\d+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+) \}",
                       open(d + "/../dec/prefix.h", encoding="ascii").read())]
    if len(lut) != 704:
        raise SystemExit("gen-brotli: %s's command table is not 704 commands" % d)
    ins_cell = [0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16]
    copy_cell = [0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16]
    ins = {}
    cop = {}
    for cmd, (iextra, cextra, dist, dctx, ibase, cbase) in enumerate(lut):
        icode = ins_cell[cmd >> 6] + (cmd >> 3 & 7)
        ccode = copy_cell[cmd >> 6] + (cmd & 7)
        for table, code, v in ((ins, icode, (ibase, iextra)), (cop, ccode, (cbase, cextra))):
            if table.setdefault(code, v) != v:
                raise SystemExit("gen-brotli: command %d's lengths are not its codes'" % cmd)
        if dist != (0 if cmd < 128 else -1) or dctx != min(ccode, 3):
            raise SystemExit("gen-brotli: command %d's distance is not its cell's" % cmd)
    if sorted(ins) != list(range(24)) or sorted(cop) != list(range(24)):
        raise SystemExit("gen-brotli: the commands do not use 24 insert and 24 copy codes")
    w = sys.stdout.write
    w("/* src/paint/brotli_dict.h -- generated by tools/gen-brotli.py: the tables RFC 7932 gives a\n")
    w("   Brotli decoder -- the static dictionary (Appendix A, %d bytes, base64, in pieces of 96\n" % SIZE)
    w("   characters) and its words' places, the word transforms (Appendix B), the literal context\n")
    w("   lookup tables (section 7.1), the block lengths' codes (section 6) and the insert-and-copy\n")
    w("   commands' (section 5). Do not edit; run the script. They are part of the Brotli software\n")
    w("   and carry its notice, below: they are under the MIT license, not Ambitious's.\n\n")
    for line in NOTICE.split("\n"):
        w(("   " + line).rstrip() + "\n")
    w("*/\n")
    b64 = base64.b64encode(data).decode("ascii")
    w("#define AMB_BROTLI_DICT_SIZE %d\n" % SIZE)
    w("#define AMB_BROTLI_DICT_PIECES %d\n" % ((len(b64) + 95) // 96))
    w("static const char *const amb_brotli_dict[AMB_BROTLI_DICT_PIECES] = {\n")
    for i in range(0, len(b64), 96):
        w('    "%s",\n' % b64[i:i + 96])
    w("};\n")
    # the transforms' affixes as one pool: each transform's prefix and suffix, offset and length
    pool, at = bytearray(), {}
    for k in range(50):
        a = affix(k)
        if a not in at:
            at[a] = len(pool)
            pool += a
    w("\n/* the transforms' prefixes and suffixes, one pool of bytes */\n")
    w("static const uint8_t amb_brotli_affix[%d] = {\n" % len(pool))
    for i in range(0, len(pool), 24):
        w("    %s,\n" % ", ".join("%d" % b for b in pool[i:i + 24]))
    w("};\n")
    w("/* each transform: its prefix's place and length in the pool, its operation (0 identity,\n")
    w("   1-9 omit that many last, 10 upper-case the first, 11 upper-case all, 12-20 omit 1-9\n")
    w("   first), its suffix's place and length */\n")
    w("static const uint16_t amb_brotli_transform[121 * 5] = {\n")
    for t in range(121):
        p, op, s = triples[3 * t:3 * t + 3]
        w("    %d, %d, %d, %d, %d,\n" % (at[affix(p)], len(affix(p)), op, at[affix(s)], len(affix(s))))
    w("};\n")
    w("\n/* the literal context lookup tables: for each context mode (LSB6, MSB6, UTF8, signed),\n")
    w("   the previous byte's part (256) then the one before it's (256) */\n")
    w("static const uint8_t amb_brotli_context[2048] = {\n")
    for i in range(0, 2048, 32):
        w("    %s,\n" % ", ".join("%d" % b for b in ctx[i:i + 32]))
    w("};\n")
    w("\n/* the dictionary's words of each length: how many bits number them (0: none), where\n")
    w("   they begin */\n")
    w("static const uint8_t amb_brotli_word_bits[32] = { %s };\n" % ", ".join("%d" % b for b in bits))
    w("static const uint32_t amb_brotli_word_at[32] = { %s };\n" % ", ".join("%d" % b for b in offs))
    w("\n/* a block length's codes: each one's base and extra bits */\n")
    w("static const uint16_t amb_brotli_block_base[26] = { %s };\n" % ", ".join("%d" % b for b in blocks[0::2]))
    w("static const uint8_t amb_brotli_block_extra[26] = { %s };\n" % ", ".join("%d" % b for b in blocks[1::2]))
    w("\n/* the insert-and-copy commands: each cell's first insert code and copy code; each insert\n")
    w("   code's and copy code's base and extra bits */\n")
    w("static const uint8_t amb_brotli_ins_cell[11] = { %s };\n" % ", ".join("%d" % b for b in ins_cell))
    w("static const uint8_t amb_brotli_copy_cell[11] = { %s };\n" % ", ".join("%d" % b for b in copy_cell))
    w("static const uint32_t amb_brotli_ins_base[24] = { %s };\n" % ", ".join("%d" % ins[k][0] for k in range(24)))
    w("static const uint8_t amb_brotli_ins_extra[24] = { %s };\n" % ", ".join("%d" % ins[k][1] for k in range(24)))
    w("static const uint32_t amb_brotli_copy_base[24] = { %s };\n" % ", ".join("%d" % cop[k][0] for k in range(24)))
    w("static const uint8_t amb_brotli_copy_extra[24] = { %s };\n" % ", ".join("%d" % cop[k][1] for k in range(24)))


if __name__ == "__main__":
    main()
