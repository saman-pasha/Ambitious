#!/usr/bin/env python3
"""tools/gen-dict-vectors.py DIR -- the test vectors of tests/exec/net_dict.c, made into DIR: inputs
compressed over raw dictionaries, as RFC 9842's dcb (Brotli) and dcz (Zstandard) compress them, by the
system's libbrotlienc 1.1.0 and libzstd 1.5.5 (tools/dict-encode.py: BrotliEncoderAttachPrepared-
Dictionary, ZSTD_CCtx_refPrefix). Prints the .expect's lines: each vector's name, its decoded length
and CRC-32 (the original's, not a decoder's).

The inputs are made here, the same each run: words a PRNG draws (text, as the dictionary is), the
dictionary itself edited here and there (copies that reach deep into it), text the dictionary has
nothing of, the dictionary repeated past a window (a Brotli window of 1,008 bytes or a zstd window
of 1 KB, so that copies from the dictionary lie past the window's reach), nothing at all, and a
long one past Brotli's and zstd's block sizes. tests/exec/dict/ holds what it wrote; run it again
only to change the vectors."""
import os
import subprocess
import sys
import zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
enc = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dict-encode.py")

seed = 2463534242


def rand():
    global seed
    seed ^= (seed << 13) & 0xFFFFFFFF
    seed ^= seed >> 17
    seed ^= (seed << 5) & 0xFFFFFFFF
    return seed


WORDS = ["the", "of", "and", "a", "to", "in", "is", "that", "it", "was", "for", "on", "are", "as", "with", "dictionary",
         "compression", "transport", "brotli", "zstandard", "window", "distance", "match", "literal", "Chromium", "header",
         "<div class=\"item\">", "</div>", "\n", ". ", ", ", "{ color: navy }", "https://", "function", "return", "var"]


def text(n):
    s = []
    k = 0
    while k < n:
        w = WORDS[rand() % len(WORDS)] + (" " if rand() % 7 else "")
        s.append(w)
        k += len(w)
    return "".join(s)[:n].encode()


def edited(d, edits):
    b = bytearray(d)
    for _ in range(edits):
        at = rand() % len(b)
        b[at:at + 1] = text(1 + rand() % 12)
    return bytes(b)


def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)


dicts = {"small": text(3000), "large": text(60000)}
for k, v in dicts.items():
    write(k + ".dict", v)
cases = [
    ("edited", "small", edited(dicts["small"], 8)),
    ("unrelated", "small", bytes(rand() & 255 for _ in range(2000))),
    ("again", "small", dicts["small"] * 6),
    ("empty", "small", b""),
    ("long", "large", edited(dicts["large"], 40) + text(150000) + dicts["large"][:20000]),
]
lines = []
for name, d, data in cases:
    dpath = os.path.join(out, d + ".dict")
    for kind, q, w in [("br", 11, 22), ("br", 5, 22), ("br", 9, 10), ("zstd", 19, 0), ("zstd", 3, 0), ("zstd", 9, 10)]:
        coded = subprocess.run(["python3", "-I", enc, kind, dpath, str(q), str(w)], input=data, capture_output=True, check=True).stdout
        v = "%s-%s-q%d%s.%s" % (name, d, q, "-w%d" % w if (kind == "br" and w != 22) or (kind == "zstd" and w) else "", "br" if kind == "br" else "zst")
        write(v, coded)
        lines.append("%s: %d bytes, crc32 %08x" % (v, len(data), zlib.crc32(data) & 0xFFFFFFFF))
print("\n".join(lines))
