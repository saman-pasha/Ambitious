#!/usr/bin/env python3
"""tools/gen-idna.py UCD_DIR > src/net/idna.h -- the tables of UTS #46 (Unicode IDNA
Compatibility Processing) that src/net/url.c's domain to ASCII reads, from one
version of Unicode's data, the version web-platform-tests' IdnaTestV2.json is made
from (18.0.0 for WPT's master of 2026-11). UCD_DIR holds these files of that version,
downloaded from https://www.unicode.org/Public/<version>/:

    idna/IdnaMappingTable.txt        each code point's status and mapping (UTS #46, 5)
    ucd/UnicodeData.txt              canonical decompositions and combining classes (NFC)
    ucd/DerivedNormalizationProps.txt  Full_Composition_Exclusion (NFC's composites)
    ucd/extracted/DerivedBidiClass.txt  Bidi_Class (RFC 5893's bidi rule)
    ucd/extracted/DerivedJoiningType.txt  Joining_Type (RFC 5892's CONTEXTJ rule)
    ucd/extracted/DerivedGeneralCategory.txt  General_Category=Mark (UTS #46, 4.1)
    ucd/NormalizationTest.txt        only with --check: NFC by these tables against it

What it writes, each a table of plain numbers, searched by halves:
  - the mapping table for nontransitional processing as runs of code points: valid
    (a deviation is valid there), disallowed, mapped by one delta (each code point
    to the one a fixed distance on), or mapped to one sequence (ignored is the empty
    one), the sequences in a pool;
  - NFC: the canonical combining classes as runs; each full canonical decomposition
    (Hangul's syllables apart: they are arithmetic); the primary composites by their
    pairs;
  - Bidi_Class as runs over every code point (the file's @missing defaults first);
  - Joining_Type's runs of other than U (non-joining); General_Category=Mark's runs.

With --check it also runs NFC over the tables, as url.c does, against the NFC column
of NormalizationTest.txt, and each code point's mapping against the file, and says
how many agree (on stderr). Run with python3 -I; the header is committed, so a build
needs no Python."""
import os
import sys

BIDI_LONG = {
    'Left_To_Right': 'L', 'Right_To_Left': 'R', 'Arabic_Letter': 'AL', 'European_Number': 'EN',
    'European_Separator': 'ES', 'European_Terminator': 'ET', 'Arabic_Number': 'AN', 'Common_Separator': 'CS',
    'Nonspacing_Mark': 'NSM', 'Boundary_Neutral': 'BN', 'Paragraph_Separator': 'B', 'Segment_Separator': 'S',
    'White_Space': 'WS', 'Other_Neutral': 'ON', 'Left_To_Right_Embedding': 'LRE', 'Left_To_Right_Override': 'LRO',
    'Right_To_Left_Embedding': 'RLE', 'Right_To_Left_Override': 'RLO', 'Pop_Directional_Format': 'PDF',
    'Left_To_Right_Isolate': 'LRI', 'Right_To_Left_Isolate': 'RLI', 'First_Strong_Isolate': 'FSI',
    'Pop_Directional_Isolate': 'PDI'}
# the classes RFC 5893 names, numbered for the C side (url.c's IDNA_BIDI_...); the rest are 0
BIDI_CODE = {'L': 1, 'R': 2, 'AL': 3, 'AN': 4, 'EN': 5, 'ES': 6, 'CS': 7, 'ET': 8, 'ON': 9, 'BN': 10, 'NSM': 11}
JOIN_CODE = {'C': 1, 'D': 2, 'L': 3, 'R': 4, 'T': 5}
JOIN_LONG = {'Join_Causing': 'C', 'Dual_Joining': 'D', 'Left_Joining': 'L', 'Right_Joining': 'R', 'Transparent': 'T',
             'Non_Joining': 'U'}


def data_lines(path):
    for line in open(path, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if line:
            yield [f.strip() for f in line.split(';')]


def missing_lines(path):
    for line in open(path, encoding='utf-8'):
        if line.startswith('# @missing:'):
            yield [f.strip() for f in line[len('# @missing:'):].split(';')]


def cps(field):
    if '..' in field:
        a, b = field.split('..')
        return int(a, 16), int(b, 16)
    v = int(field, 16)
    return v, v


def version_of(path):
    for line in open(path, encoding='utf-8'):
        if line.startswith('# Version:'):
            return line.split(':', 1)[1].strip()
        if '-' in line and line.startswith('# ') and line.strip().endswith('.txt'):
            name = line[2:].strip()
            return name.rsplit('-', 1)[1][:-4]
    return '?'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    check = '--check' in sys.argv
    if len(args) != 1:
        sys.exit(__doc__)
    d = args[0]
    path = lambda name: os.path.join(d, name)
    # each file of one version (UnicodeData.txt names none, and is taken as the others')
    version = version_of(path('IdnaMappingTable.txt'))
    for name in ('DerivedNormalizationProps.txt', 'DerivedBidiClass.txt', 'DerivedJoiningType.txt', 'DerivedGeneralCategory.txt'):
        v = version_of(path(name))
        if v != version:
            sys.exit('%s is of Unicode %s, the mapping table of %s' % (name, v, version))

    # ---- the mapping table: each code point's (mode, value) for nontransitional processing
    status = [None] * 0x110000
    for f in data_lines(path('IdnaMappingTable.txt')):
        a, b = cps(f[0])
        st = f[1]
        seq = tuple(int(x, 16) for x in f[2].split()) if len(f) > 2 and f[2] else ()
        for c in range(a, b + 1):
            if st in ('valid', 'deviation'):
                status[c] = ('v',)
            elif st == 'disallowed':
                status[c] = ('x',)
            elif st == 'ignored':
                status[c] = ('m', ())
            elif st == 'mapped':
                status[c] = ('m', seq)
            else:
                sys.exit('unknown status %s' % st)
    if any(s is None for s in status):
        sys.exit('the mapping table leaves code points out')

    # runs: valid, disallowed, a delta (one code point a fixed distance on), a sequence
    runs = []   # (first, kind, value)
    for c in range(0x110000):
        s = status[c]
        if s[0] == 'v':
            key = ('v', 0)
        elif s[0] == 'x':
            key = ('x', 0)
        elif len(s[1]) == 1:
            key = ('d', s[1][0] - c)
        else:
            key = ('s', s[1])
        if runs and runs[-1][1:] == key:
            continue
        runs.append((c,) + key)
    pool = []
    pool_at = {}
    words = []
    for first, kind, value in runs:
        if kind == 'v':
            w = 0
        elif kind == 'x':
            w = 1
        elif kind == 'd':
            w = 2 | ((value + (1 << 28)) << 2)
        else:
            if value not in pool_at:
                pool_at[value] = len(pool)
                pool.extend(value)
            if len(value) > 31:
                sys.exit('a mapping past 31 code points')
            w = 3 | (len(value) << 2) | (pool_at[value] << 7)
        words.append(w)

    # ---- NFC
    ccc = [0] * 0x110000
    decomp = {}
    for f in data_lines(path('UnicodeData.txt')):
        c = int(f[0], 16)
        ccc[c] = int(f[3])
        if f[5] and not f[5].startswith('<'):
            decomp[c] = tuple(int(x, 16) for x in f[5].split())
    exclusion = set()
    for f in data_lines(path('DerivedNormalizationProps.txt')):
        if f[1] == 'Full_Composition_Exclusion':
            a, b = cps(f[0])
            exclusion.update(range(a, b + 1))

    def full(c):
        if c not in decomp:
            return (c,)
        out = ()
        for x in decomp[c]:
            out += full(x)
        return out
    fulls = {c: full(c) for c in sorted(decomp)}
    comp = {}
    for c, m in decomp.items():
        if len(m) == 2 and c not in exclusion:
            comp[m] = c
    ccc_runs = []
    c = 0
    while c < 0x110000:
        if ccc[c] == 0:
            c += 1
            continue
        e = c
        while e + 1 < 0x110000 and ccc[e + 1] == ccc[c]:
            e += 1
        ccc_runs.append((c, e, ccc[c]))
        c = e + 1
    dpool = []
    dwords = []
    for c in sorted(fulls):
        m = fulls[c]
        dwords.append((c, len(m) << 24 | len(dpool)))
        dpool.extend(m)

    # ---- Bidi_Class: the defaults, then the file's values
    bidi = [0] * 0x110000
    for f in missing_lines(path('DerivedBidiClass.txt')):
        a, b = cps(f[0])
        v = BIDI_CODE.get(BIDI_LONG.get(f[1], f[1]), 0)
        for x in range(a, b + 1):
            bidi[x] = v
    for f in data_lines(path('DerivedBidiClass.txt')):
        a, b = cps(f[0])
        v = BIDI_CODE.get(f[1], 0)
        for x in range(a, b + 1):
            bidi[x] = v
    bidi_runs = []
    for x in range(0x110000):
        if not bidi_runs or bidi_runs[-1][1] != bidi[x]:
            bidi_runs.append((x, bidi[x]))

    # ---- Joining_Type and the marks
    join = [0] * 0x110000
    for f in missing_lines(path('DerivedJoiningType.txt')):
        a, b = cps(f[0])
        v = JOIN_CODE.get(JOIN_LONG.get(f[1], f[1]), 0)
        for x in range(a, b + 1):
            join[x] = v
    for f in data_lines(path('DerivedJoiningType.txt')):
        a, b = cps(f[0])
        for x in range(a, b + 1):
            join[x] = JOIN_CODE.get(f[1], 0)
    join_runs = []
    x = 0
    while x < 0x110000:
        if join[x] == 0:
            x += 1
            continue
        e = x
        while e + 1 < 0x110000 and join[e + 1] == join[x]:
            e += 1
        join_runs.append((x, e, join[x]))
        x = e + 1
    mark = [False] * 0x110000
    for f in data_lines(path('DerivedGeneralCategory.txt')):
        if f[1] in ('Mn', 'Mc', 'Me'):
            a, b = cps(f[0])
            for x in range(a, b + 1):
                mark[x] = True
    mark_runs = []
    x = 0
    while x < 0x110000:
        if not mark[x]:
            x += 1
            continue
        e = x
        while e + 1 < 0x110000 and mark[e + 1]:
            e += 1
        mark_runs.append((x, e))
        x = e + 1

    if check:
        self_check(path, status, runs, words, pool, ccc, fulls, comp)

    # ---- the header
    o = sys.stdout
    o.write('/* src/net/idna.h -- generated by tools/gen-idna.py from Unicode %s\'s data (do not\n' % version)
    o.write('   edit): the tables of UTS #46 that src/net/url.c reads, included by it alone.\n')
    o.write('   %d runs of the mapping table for nontransitional processing (%d code points in\n' % (len(runs), len(pool)))
    o.write('   its sequences); NFC\'s %d runs of combining classes, %d canonical decompositions\n' % (len(ccc_runs), len(fulls)))
    o.write('   and %d primary composites; %d runs of Bidi_Class, %d of Joining_Type and %d of\n' % (len(comp), len(bidi_runs), len(join_runs), len(mark_runs)))
    o.write('   General_Category=Mark. Each array\'s size is written out, and each count is an\n')
    o.write('   enum constant: a macro as an array\'s size in a header is not expanded\n')
    o.write('   (toolchain/REPORT.md, finding 23). */\n')
    o.write('#define IDNA_UNICODE_VERSION "%s"\n\n' % version)
    o.write('enum { IDNA_MAP_N = %d, IDNA_POOL_N = %d, IDNA_CCC_N = %d, IDNA_DECOMP_N = %d, IDNA_DPOOL_N = %d, IDNA_COMP_N = %d,\n'
            % (len(runs), len(pool), len(ccc_runs), len(fulls), len(dpool), len(comp)))
    o.write('       IDNA_BIDI_N = %d, IDNA_JOIN_N = %d, IDNA_MARK_N = %d };\n\n' % (len(bidi_runs), len(join_runs), len(mark_runs)))

    def table(ctype, name, values, per_line=8, fmt='0x%X'):
        o.write('static const %s %s[%d] = {\n' % (ctype, name, len(values)))
        for k in range(0, len(values), per_line):
            o.write('    ' + ', '.join(fmt % v for v in values[k:k + per_line]) + ',\n')
        o.write('};\n')

    o.write('/* the mapping table: each run\'s first code point, and what its code points are: the\n')
    o.write('   low two bits 0 valid, 1 disallowed, 2 mapped by the delta (word >> 2) - 2^28, 3 mapped to\n')
    o.write('   the (word >> 2 & 31) code points of the pool from word >> 7 */\n')
    table('uint32_t', 'idna_map_first', [r[0] for r in runs])
    table('uint32_t', 'idna_map_word', words)
    table('uint32_t', 'idna_pool', pool)
    o.write('/* the canonical combining classes other than 0: runs [first, last] of one class */\n')
    table('uint32_t', 'idna_ccc', [v for r in ccc_runs for v in r], 9)
    o.write('/* each full canonical decomposition: the code point, then its length << 24 and its place in idna_dpool */\n')
    table('uint32_t', 'idna_decomp', [v for r in dwords for v in r])
    table('uint32_t', 'idna_dpool', dpool)
    o.write('/* the primary composites: the pair (first, second) and the composite, sorted by the pair */\n')
    table('uint32_t', 'idna_comp', [v for k in sorted(comp) for v in (k[0], k[1], comp[k])], 9)
    o.write('/* Bidi_Class: each run\'s first code point, and its class (url.c\'s IDNA_BIDI_..., 0 for another) */\n')
    table('uint32_t', 'idna_bidi_first', [r[0] for r in bidi_runs])
    table('uint8_t', 'idna_bidi_class', [r[1] for r in bidi_runs], 24, '%d')
    o.write('/* Joining_Type other than U: runs [first, last] and the type (1 C, 2 D, 3 L, 4 R, 5 T) */\n')
    table('uint32_t', 'idna_join', [v for r in join_runs for v in r], 9)
    o.write('/* General_Category=Mark: runs [first, last] */\n')
    table('uint32_t', 'idna_mark', [v for r in mark_runs for v in r])


def self_check(path, status, runs, words, pool, ccc, fulls, comp):
    """the tables read back as url.c reads them, against the files"""
    import bisect
    firsts = [r[0] for r in runs]

    def lookup(c):
        i = bisect.bisect_right(firsts, c) - 1
        w = words[i]
        if w & 3 == 0:
            return ('v',)
        if w & 3 == 1:
            return ('x',)
        if w & 3 == 2:
            return ('m', (c + (w >> 2) - (1 << 28),))
        n, at = w >> 2 & 31, w >> 7
        return ('m', tuple(pool[at:at + n]))
    bad = sum(1 for c in range(0x110000) if lookup(c) != status[c])
    sys.stderr.write('mapping: %d of %d code points read back otherwise\n' % (bad, 0x110000))

    SB, LB, VB, TB, LC, VC, TC = 0xAC00, 0x1100, 0x1161, 0x11A7, 19, 21, 28
    NC, SC = VC * TC, LC * VC * TC

    def nfc(s):
        d = []
        for c in s:
            if 0 <= c - SB < SC:
                x = c - SB
                d += [LB + x // NC, VB + x % NC // TC] + ([TB + x % TC] if x % TC else [])
            else:
                d += list(fulls.get(c, (c,)))
        for i in range(1, len(d)):
            j = i
            while j > 0 and ccc[d[j]] and ccc[d[j - 1]] > ccc[d[j]]:
                d[j], d[j - 1] = d[j - 1], d[j]
                j -= 1
        if not d:
            return d
        out = [d[0]]
        starter = 0 if ccc[d[0]] == 0 else -1
        last = 0 if ccc[d[0]] == 0 else 256
        for c in d[1:]:
            k = ccc[c]
            if starter >= 0 and (last < k or last == 0):
                a = out[starter]
                x = None
                if 0 <= a - LB < LC and 0 <= c - VB < VC:
                    x = SB + ((a - LB) * VC + (c - VB)) * TC
                elif 0 <= a - SB < SC and (a - SB) % TC == 0 and 0 < c - TB < TC:
                    x = a + c - TB
                elif (a, c) in comp:
                    x = comp[(a, c)]
                if x is not None and (last == 0 or last < k) and (last != 0 or len(out) - 1 == starter):
                    out[starter] = x
                    continue
            if k == 0:
                starter = len(out)
            last = k
            out.append(c)
        return out
    total = agree = 0
    for f in data_lines(path('NormalizationTest.txt')):
        if len(f) < 5 or f[0].startswith('@'):
            continue
        src = [int(x, 16) for x in f[0].split()]
        want = [int(x, 16) for x in f[1].split()]
        total += 1
        agree += nfc(src) == want
    sys.stderr.write('NFC: %d of %d NormalizationTest cases\n' % (agree, total))


main()
