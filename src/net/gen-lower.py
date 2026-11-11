#!/usr/bin/env python3
"""src/net/gen-lower.py > src/net/lower.h -- the simple lowercase mappings of Unicode
(Python's str.lower over each code point, where it gives one code point), as runs
for the URL host's IDNA (src/net/net.c): a run of code points that each map by one
delta, or a run whose code points alternate between a capital and its small letter.
Cherokee's capitals are left as they are, since UTS #46 maps its small letters up
to them rather than its capitals down."""
import sys
import unicodedata

maps = {}
for c in range(0x110000):
    if 0xD800 <= c <= 0xDFFF or 0x13A0 <= c <= 0x13F5:
        continue
    low = chr(c).lower()
    if len(low) == 1 and ord(low) != c:
        maps[c] = ord(low)

runs = []   # (first, last, delta, alternating)
cs = sorted(maps)
i = 0
while i < len(cs):
    c = cs[i]
    d = maps[c] - c
    # alternating: c, c+2, c+4 ... each mapping to the next code point
    j = i
    while j + 1 < len(cs) and cs[j + 1] == cs[j] + 2 and maps[cs[j + 1]] == cs[j + 1] + 1 and d == 1:
        j += 1
    if j > i:
        runs.append((c, cs[j], 1, 1))
        i = j + 1
        continue
    j = i
    while j + 1 < len(cs) and cs[j + 1] == cs[j] + 1 and maps[cs[j + 1]] - cs[j + 1] == d:
        j += 1
    runs.append((c, cs[j], d, 0))
    i = j + 1

out = sys.stdout
out.write("/* src/net/lower.h -- Unicode's simple lowercase mappings as runs, made by\n")
out.write("   src/net/gen-lower.py from Python %s's Unicode %s (do not edit): each run's\n" % (sys.version.split()[0], unicodedata.unidata_version))
out.write("   first and last code point, the delta its code points move by, and whether they\n")
out.write("   alternate, a capital then its small letter. %d mappings in %d runs. */\n" % (len(maps), len(runs)))
out.write("typedef struct net_lower_run { uint32_t first, last; int32_t delta; uint8_t alternating; } net_lower_run;\n")
out.write("static const net_lower_run net_lower_runs[%d] = {\n" % len(runs))
for k in range(0, len(runs), 4):
    out.write("    " + " ".join("{ 0x%04X, 0x%04X, %d, %d }," % r for r in runs[k:k + 4]) + "\n")
out.write("};\n")
