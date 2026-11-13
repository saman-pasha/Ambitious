#!/usr/bin/env python3
"""tools/gen-linebreak.py [UCD] > src/layout/linebreak_data.h -- the character properties line breaking
reads (src/layout/linebreak.c, Chromium's line breaking: UAX #14 as Chromium's ICU 78 tailors it, UAX #29's
grapheme clusters, Blink's word-break tables), from the Unicode Character Database of Unicode 17.0.0, the
version Chromium 153's ICU 78 carries:

    python3 tools/gen-linebreak.py         (fetches the files from www.unicode.org)
    python3 tools/gen-linebreak.py UCD     (UCD holds LineBreak.txt, EastAsianWidth.txt,
                                            DerivedGeneralCategory.txt, emoji-data.txt,
                                            GraphemeBreakProperty.txt and DerivedCoreProperties.txt)

Each code point's properties are packed in 32 bits (linebreak.h names the fields): its Line_Break class
(ICU's ULineBreak numbering, which Blink's break-all table is indexed by), East_Asian_Width F, W or H,
Extended_Pictographic, its General_Category as line breaking reads it (a letter, a number, Mn, Mc, Me,
initial and final punctuation, unassigned), Grapheme_Cluster_Break and Indic_Conjunct_Break. The table is
the runs of equal properties, each its first code point and its value. The output is a function of the
files alone."""
import os
import sys
import urllib.request

VERSION = '17.0.0'
FILES = {
    'LineBreak.txt': 'LineBreak.txt',
    'EastAsianWidth.txt': 'EastAsianWidth.txt',
    'DerivedGeneralCategory.txt': 'extracted/DerivedGeneralCategory.txt',
    'emoji-data.txt': 'emoji/emoji-data.txt',
    'GraphemeBreakProperty.txt': 'auxiliary/GraphemeBreakProperty.txt',
    'DerivedCoreProperties.txt': 'DerivedCoreProperties.txt',
}

# ICU's ULineBreak values (uchar.h), in order: Blink indexes its break-all table by them
LB = ['XX', 'AI', 'AL', 'B2', 'BA', 'BB', 'BK', 'CB', 'CL', 'CM', 'CR', 'EX', 'GL', 'HY', 'ID', 'IN', 'IS', 'LF', 'NS',
      'NU', 'OP', 'PO', 'PR', 'QU', 'SA', 'SG', 'SP', 'SY', 'ZW', 'NL', 'WJ', 'H2', 'H3', 'JL', 'JT', 'JV', 'CP', 'CJ',
      'HL', 'RI', 'EB', 'EM', 'ZWJ', 'AK', 'AP', 'AS', 'VF', 'VI', 'HH']
GCB = ['Other', 'CR', 'LF', 'Control', 'Extend', 'ZWJ', 'Regional_Indicator', 'Prepend', 'SpacingMark', 'L', 'V', 'T',
       'LV', 'LVT']
INCB = ['None', 'Linker', 'Consonant', 'Extend']
# the general category as line breaking reads it: 1 a letter, 2 a number, 3 Mn, 4 Mc, 5 Me, 6 Pi, 7 Pf, 0 other
GC = {'Mn': 3, 'Mc': 4, 'Me': 5, 'Pi': 6, 'Pf': 7}


def lines(src, name):
    path = os.path.join(src, name) if src else None
    if path and os.path.exists(path):
        text = open(path, encoding='utf-8').read()
    else:
        url = 'https://www.unicode.org/Public/%s/ucd/%s' % (VERSION, FILES[name])
        text = urllib.request.urlopen(url).read().decode('utf-8')
    # its version: in its first line (NAME-17.0.0.txt), or emoji-data.txt's "# Version: 17.0"
    head = text.split('\n', 10)[:10]
    if not any(VERSION in h or ('Version: ' + VERSION[:-2]) in h for h in head):
        sys.exit('gen-linebreak: %s is not of Unicode %s (%s)' % (name, VERSION, head[0]))
    for line in text.split('\n'):
        line = line.split('#')[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(';')]
        r = fields[0]
        if '..' in r:
            a, b = r.split('..')
            a, b = int(a, 16), int(b, 16)
        else:
            a = b = int(r, 16)
        yield a, b, fields[1:]


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    n = 0x110000
    lb = bytearray(n)              # XX by default (LineBreak.txt's @missing)
    for a, b, f in lines(src, 'LineBreak.txt'):
        v = LB.index(f[0])
        for c in range(a, b + 1):
            lb[c] = v
    ea = bytearray(n)
    for a, b, f in lines(src, 'EastAsianWidth.txt'):
        if f[0] in ('F', 'W', 'H'):
            for c in range(a, b + 1):
                ea[c] = 1
    gc = bytearray(n)
    cn = bytearray(n)
    for a, b, f in lines(src, 'DerivedGeneralCategory.txt'):
        g = f[0]
        v = 1 if g[0] == 'L' else 2 if g[0] == 'N' else GC.get(g, 0)
        for c in range(a, b + 1):
            gc[c] = v
            cn[c] = 1 if g == 'Cn' else 0
    ep = bytearray(n)
    for a, b, f in lines(src, 'emoji-data.txt'):
        if f[0] == 'Extended_Pictographic':
            for c in range(a, b + 1):
                ep[c] = 1
    gcb = bytearray(n)
    for a, b, f in lines(src, 'GraphemeBreakProperty.txt'):
        v = GCB.index(f[0])
        for c in range(a, b + 1):
            gcb[c] = v
    incb = bytearray(n)
    for a, b, f in lines(src, 'DerivedCoreProperties.txt'):
        if f[0] == 'InCB':
            v = INCB.index(f[1])
            for c in range(a, b + 1):
                incb[c] = v
    starts, values = [], []
    last = None
    for c in range(n):
        v = lb[c] | ea[c] << 6 | ep[c] << 7 | gc[c] << 8 | cn[c] << 11 | gcb[c] << 12 | incb[c] << 16
        if v != last:
            starts.append(c)
            values.append(v)
            last = v
    out = sys.stdout
    # (no apostrophe in the comment: toolchain/REPORT.md, finding 30)
    out.write('/* src/layout/linebreak_data.h -- generated by tools/gen-linebreak.py from the Unicode Character\n'
              '   Database of Unicode %s (the version the ICU 78 of Chromium 153 carries); do not edit, run the\n'
              '   script. The line breaking properties of each code point, packed as src/layout/linebreak.h names\n'
              '   their fields: its Line_Break class (in the order of the ULineBreak of ICU), East_Asian_Width F, W\n'
              '   or H, Extended_Pictographic, its General_Category as line breaking reads it, unassigned,\n'
              '   Grapheme_Cluster_Break and Indic_Conjunct_Break. A run of code points of the same properties is a\n'
              '   range: its first code point in amb_lbd_start, its properties in amb_lbd_value. */\n' % VERSION)
    out.write('#ifndef AMB_LINEBREAK_DATA_H\n#define AMB_LINEBREAK_DATA_H\n#include <stdint.h>\n\n')
    for name, arr in (('amb_lbd_start', starts), ('amb_lbd_value', values)):
        out.write('static const uint32_t %s[%d] = {\n' % (name, len(arr)))
        for i in range(0, len(arr), 8):
            out.write('    ' + ', '.join('0x%x' % x for x in arr[i:i + 8]) + ',\n')
        out.write('};\n\n')
    out.write('#endif\n')


main()
