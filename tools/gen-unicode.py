#!/usr/bin/env python3
"""tools/gen-unicode.py UCD EMOJI > src/js/unicode.h -- the Unicode tables the engine's
regular expressions and case conversions read, from the Unicode Character Database's
own files (their version is the tables'). Run it again when a newer Unicode is wanted;
the header is committed, so a build needs no Python.

tools/gen-unicode.py --normalize UCD > src/js/normalize.h -- the tables of
String.prototype.normalize (src/js/normalize.c), from the same files: the canonical
combining classes, the full canonical and compatibility decompositions (Hangul's
syllables apart: they are algorithmic), the primary composites by their pairs, and the
quick checks' No and Maybe of NFC and NFKC.

UCD is a copy of https://www.unicode.org/Public/17.0.0/ucd/, the version node 22's ICU
(78) has, laid out as it is there: UnicodeData.txt, Scripts.txt, ScriptExtensions.txt,
PropertyAliases.txt, PropertyValueAliases.txt, PropList.txt, DerivedCoreProperties.txt,
DerivedNormalizationProps.txt, CaseFolding.txt, SpecialCasing.txt,
extracted/DerivedBinaryProperties.txt and emoji/emoji-data.txt. EMOJI holds UTS #51's
emoji-sequences.txt and emoji-zwj-sequences.txt, from
https://www.unicode.org/Public/17.0.0/emoji/ (Emoji 17.0, node's too). Neither is in
the repository: each is fetched into a directory of its own, as

    V=17.0.0 UCD=$HOME/ucd-$V EMOJI=$UCD/emoji-seq
    mkdir -p $UCD/emoji $UCD/extracted $EMOJI
    for f in UnicodeData.txt Scripts.txt ScriptExtensions.txt PropertyAliases.txt \\
        PropertyValueAliases.txt PropList.txt DerivedCoreProperties.txt \\
        DerivedNormalizationProps.txt CaseFolding.txt SpecialCasing.txt \\
        extracted/DerivedBinaryProperties.txt emoji/emoji-data.txt; do
      curl -sSfo $UCD/$f https://www.unicode.org/Public/$V/ucd/$f; done
    for f in emoji-sequences.txt emoji-zwj-sequences.txt; do
      curl -sSfo $EMOJI/$f https://www.unicode.org/Public/$V/emoji/$f; done
    python3 tools/gen-unicode.py $UCD $EMOJI > src/js/unicode.h
    python3 tools/gen-unicode.py --normalize $UCD > src/js/normalize.h

What it writes:
  - every property a \\p{...} names (ECMA-262's tables 67 to 69): the general
    categories, the scripts and their extensions, the binary properties, as ranges,
    each by its names and aliases as PropertyAliases.txt and PropertyValueAliases.txt
    spell them (V8 takes those spellings alone). A value no code point has
    (Katakana_Or_Hiragana, the Script of none) keeps its place with no names: V8
    refuses a property whose set is empty;
  - the properties of strings a \\p{...} names with v (ECMA-262's table 70): each
    one's sequences, and its single code points as ranges;
  - the simple case mappings (upper, lower, folding) as runs of one delta, and the
    full mappings that give more than one code point (SpecialCasing's unconditional
    ones);
  - the case orbits: the code points whose canonical forms are the same, under simple
    case folding (a pattern with u) and under the uppercase canonicalization of code
    units (one without);
  - the places in the properties' list of the four binary properties the engine reads
    besides \\p: Cased and Case_Ignorable (toLowerCase's final sigma), ID_Start and
    ID_Continue (the lexer's names and a group's).

It replaces tools/gen-unicode.pl, which read perl's Unicode::UCD (Unicode 15.0 on this
machine, perl's own copy of the database). Given the 15.0 files it writes the headers
that one wrote but for three things: the first comment's words; four aliases perl
spells otherwise, Unicode::UCD giving cntrl, digit, punct (General_Category) and space
(White_Space) a capital (Cntrl, Digit, Punct, Space) where the database and V8 have
them in lower case; and the four places above, which that one did not write."""
import os
import re
import sys

MAX = 0x110000


def data(path):
    """a UCD file's lines as their fields, comments and blank lines left out"""
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield [x.strip() for x in line.split(";")]


def cps(field):
    """a field's code points: one, or first..last"""
    a, _, b = field.partition("..")
    return range(int(a, 16), int(b or a, 16) + 1)


def merge(ranges):
    """[lo, hi] ranges sorted, the adjacent and the overlapping joined"""
    out = []
    for lo, hi in sorted(ranges):
        if out and lo <= out[-1][1] + 1:
            out[-1][1] = max(out[-1][1], hi)
        else:
            out.append([lo, hi])
    return out


def runs(values):
    """each value's ranges, of a property given code point by code point"""
    out = {}
    prev, start = values[0], 0
    for cp in range(1, MAX):
        v = values[cp]
        if v != prev:
            out.setdefault(prev, []).append([start, cp - 1])
            prev, start = v, cp
    out.setdefault(prev, []).append([start, MAX - 1])
    return out


def complement(ranges):
    """the code points sorted ranges leave out, as ranges"""
    out, nxt = [], 0
    for lo, hi in ranges:
        if lo > nxt:
            out.append([nxt, lo - 1])
        nxt = hi + 1
    if nxt < MAX:
        out.append([nxt, MAX - 1])
    return out


def uniq(names):
    """the names in their order, each once (a short name the long one too: Cased, Cased)"""
    return list(dict.fromkeys(names))


class UCD:
    def __init__(self, path):
        self.path = path
        first = open(self.file("PropList.txt"), encoding="utf-8").readline()
        m = re.match(r"# PropList-(\d+\.\d+\.\d+)\.txt", first)
        if not m:
            sys.exit("%s: no version on its first line" % self.file("PropList.txt"))
        self.version = m.group(1)
        # UnicodeData.txt: each assigned code point's general category, combining
        # class, decomposition and simple case mappings (a First..Last range's alike)
        self.gc = ["Cn"] * MAX
        self.ccc, self.dm, self.upper, self.lower = {}, {}, {}, {}
        first = None
        for f in data(self.file("UnicodeData.txt")):
            cp = int(f[0], 16)
            if f[1].endswith(", First>"):
                first = cp
                continue
            for c in range(first if f[1].endswith(", Last>") else cp, cp + 1):
                self.gc[c] = f[2]
                if int(f[3]):
                    self.ccc[c] = int(f[3])
                if f[5]:
                    self.dm[c] = f[5]
                if f[12] and int(f[12], 16) != c:
                    self.upper[c] = int(f[12], 16)
                if f[13] and int(f[13], 16) != c:
                    self.lower[c] = int(f[13], 16)
            first = None

    def file(self, name):
        return os.path.join(self.path, name)

    def aliases(self, prop):
        """PropertyValueAliases.txt's values of a property: [short, long, others...]"""
        return [f[1:] for f in data(self.file("PropertyValueAliases.txt")) if f[0] == prop]

    def binary(self, names):
        """the binary properties named, by their long names, from the files that hold them"""
        out = {n: [] for n in names}
        for name in ("PropList.txt", "DerivedCoreProperties.txt", "extracted/DerivedBinaryProperties.txt",
                     "emoji/emoji-data.txt", "DerivedNormalizationProps.txt"):
            for f in data(self.file(name)):
                if len(f) == 2 and f[1] in out:
                    r = cps(f[0])
                    out[f[1]].append([r.start, r.stop - 1])
        return {n: merge(r) for n, r in out.items()}


# ---- unicode.h ----

# the binary properties ECMA-262 names (its table 68), in the order of the table
BINARY = """ASCII ASCII_Hex_Digit Alphabetic Any Assigned Bidi_Control Bidi_Mirrored Case_Ignorable Cased
    Changes_When_Casefolded Changes_When_Casemapped Changes_When_Lowercased Changes_When_NFKC_Casefolded
    Changes_When_Titlecased Changes_When_Uppercased Dash Default_Ignorable_Code_Point Deprecated Diacritic
    Emoji Emoji_Component Emoji_Modifier Emoji_Modifier_Base Emoji_Presentation Extended_Pictographic Extender
    Grapheme_Base Grapheme_Extend Hex_Digit IDS_Binary_Operator IDS_Trinary_Operator ID_Continue ID_Start
    Ideographic Join_Control Logical_Order_Exception Lowercase Math Noncharacter_Code_Point Pattern_Syntax
    Pattern_White_Space Quotation_Mark Radical Regional_Indicator Sentence_Terminal Soft_Dotted
    Terminal_Punctuation Unified_Ideograph Uppercase Variation_Selector White_Space XID_Continue XID_Start""".split()

# the binary properties the engine reads besides \p: toLowerCase's final sigma (Cased and
# Case_Ignorable, as ICU reads its context), the lexer's and the group names' identifiers
READ = ["Case_Ignorable", "Cased", "ID_Continue", "ID_Start"]

# the properties of strings, in ECMA-262's order (RGI_Emoji, their union, is the
# engine's to make): each a list of sequences, and of single code points
STRPROPS = ["Basic_Emoji", "Emoji_Keycap_Sequence", "RGI_Emoji_Modifier_Sequence", "RGI_Emoji_Flag_Sequence",
            "RGI_Emoji_Tag_Sequence", "RGI_Emoji_ZWJ_Sequence"]


def case_runs(m):
    """a mapping's runs: [lo, hi, delta, stride], stride 2 only over code points between
    that map to themselves, so that no two runs overlap"""
    c = sorted(m)
    out, i = [], 0
    while i < len(c):
        lo = c[i]
        d = m[lo] - lo
        j = i
        if i + 1 < len(c) and c[i + 1] == lo + 1 and m[lo + 1] - (lo + 1) == d:
            while j + 1 < len(c) and c[j + 1] == c[j] + 1 and m[c[j + 1]] - c[j + 1] == d:
                j += 1
            out.append((lo, c[j], d, 1))
        elif i + 1 < len(c) and c[i + 1] == lo + 2 and m[lo + 2] - (lo + 2) == d and lo + 1 not in m:
            while j + 1 < len(c) and c[j + 1] == c[j] + 2 and m[c[j + 1]] - c[j + 1] == d and c[j] + 1 not in m:
                j += 1
            out.append((lo, c[j], d, 2))
        else:
            out.append((lo, lo, d, 1))
        i = j + 1
    return out


def orbits(canon):
    """the orbits: code points grouped by their canonical form, each group a cycle of
    (code point, the next) pairs that rises and comes back to its least"""
    group = {}
    for cp, c in canon.items():
        group.setdefault(c, []).append(cp)
    pairs = []
    for g in group.values():
        m = sorted(set(g))
        if len(m) < 2:
            continue
        pairs += [(m[k], m[(k + 1) % len(m)]) for k in range(len(m))]
    return sorted(pairs)


def listing(out, name, ctype, per, v):
    out.append("static const %s %s[] = {\n" % (ctype, name))
    for i in range(0, len(v), per):
        out.append("    " + ", ".join(str(x) for x in v[i:i + per]) + ",\n")
    out.append("};\n")


def unicode_h(u, emoji_dir):
    ranges, props = [], []

    def add(names, kind, rs):
        off = len(ranges)
        ranges.extend(rs)
        # a set no code point is in: no names (V8 refuses such a property)
        props.append(("|".join(names) if rs else "", kind, off, len(rs)))

    # the general categories (kind 0), each by its short and long names and their aliases
    gc = runs(u.gc)
    for v in sorted(u.aliases("gc"), key=lambda a: a[0]):
        short = v[0]
        if short == "LC":
            rs = merge(gc.get("Ll", []) + gc.get("Lt", []) + gc.get("Lu", []))
        elif len(short) == 2:
            rs = gc.get(short, [])
        else:
            rs = merge([r for k, rr in gc.items() if k[0] == short for r in rr])
        add(uniq(v), 0, rs)

    # the scripts (kind 1) and their extensions (kind 2), by the same names: a code point
    # outside ScriptExtensions.txt has its script alone
    scripts = sorted(u.aliases("sc"), key=lambda a: a[0])
    short_of = {a[1]: a[0] for a in scripts}
    sc = ["Zzzz"] * MAX
    for f in data(u.file("Scripts.txt")):
        s = short_of[f[1]]
        for cp in cps(f[0]):
            sc[cp] = s
    scx_of = {}
    for f in data(u.file("ScriptExtensions.txt")):
        for cp in cps(f[0]):
            scx_of[cp] = f[1].split()
    scr = runs(sc)
    scx = {}
    for cp in range(MAX):
        for s in scx_of.get(cp, (sc[cp],)):
            r = scx.setdefault(s, [])
            if r and r[-1][1] == cp - 1:
                r[-1][1] = cp
            else:
                r.append([cp, cp])
    for v in scripts:
        add(uniq(v), 1, scr.get(v[0], []))
        add(uniq(v), 2, scx.get(v[0], []))

    # the binary properties ECMA-262 names (kind 3)
    long_names = {}
    for f in data(u.file("PropertyAliases.txt")):
        long_names[f[1]] = f
    files = u.binary([p for p in BINARY if p not in ("ASCII", "Any", "Assigned")])
    for p in BINARY:
        if p == "ASCII":
            add([p], 3, [[0, 127]])
        elif p == "Any":
            add([p], 3, [[0, MAX - 1]])
        elif p == "Assigned":
            add([p], 3, complement(gc["Cn"]))
        else:
            add(uniq(long_names[p]), 3, files[p])

    # the properties of strings: each a list of sequences, and of single code points
    seqs, singles, emoji_version = {}, {}, None
    for name in ("emoji-sequences.txt", "emoji-zwj-sequences.txt"):
        path = os.path.join(emoji_dir, name)
        for line in open(path, encoding="utf-8"):
            m = re.match(r"# Version: (\S+)", line)
            if m and emoji_version is None:
                emoji_version = m.group(1)
            line = line.split("#", 1)[0]
            if not line.strip():
                continue
            f = [x.strip() for x in line.split(";")]
            if f[1] not in STRPROPS:
                sys.exit("%s: unknown type %s" % (name, f[1]))
            if ".." in f[0]:
                singles.setdefault(f[1], []).extend(cps(f[0]))
                continue
            c = [int(x, 16) for x in f[0].split()]
            if len(c) == 1:
                singles.setdefault(f[1], []).append(c[0])
            else:
                seqs.setdefault(f[1], []).append(c)
    seqwords, strp = [], []
    for t in STRPROPS:
        off = len(seqwords)
        s = sorted(seqs.get(t, []), key=lambda q: ",".join(str(c) for c in q))
        for q in s:
            seqwords += [len(q)] + q
        roff = len(ranges)
        ranges.extend(merge([c, c] for c in set(singles.get(t, []))))
        strp.append((t, off, len(seqwords) - off, len(s), roff, len(ranges) - roff))

    # the simple mappings, and the full ones to more than one code point
    fold = {}
    for f in data(u.file("CaseFolding.txt")):
        if f[1] in ("C", "S"):
            fold[int(f[0], 16)] = int(f[2], 16)
    special_upper, special_lower = {}, {}
    for f in data(u.file("SpecialCasing.txt")):
        if len(f) > 4 and f[4]:
            continue          # a condition: the language's, or the context's
        cp = int(f[0], 16)
        special_lower[cp] = [int(x, 16) for x in f[1].split()]
        special_upper[cp] = [int(x, 16) for x in f[3].split()]

    def full_upper(cp):
        return special_upper.get(cp) or [u.upper.get(cp, cp)]

    upsp = [[cp] + special_upper[cp] for cp in sorted(special_upper) if len(special_upper[cp]) > 1]
    losp = [[cp] + special_lower[cp] for cp in sorted(special_lower) if len(special_lower[cp]) > 1]

    # the orbits. With u: simple case folding, every code point that folds, and what it
    # folds to. Without: a code unit's uppercase, when that is one code unit and not
    # ASCII from beyond it
    cu = {}
    for cp, to in fold.items():
        cu[cp] = to
        cu[to] = to
    cb = {}
    for cp in range(0x10000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        up = full_upper(cp)
        cb[cp] = up[0] if len(up) == 1 and up[0] <= 0xFFFF and not (cp >= 128 and up[0] < 128) else cp
    ou, ob = orbits(cu), orbits(cb)

    o = []
    o.append("/* src/js/unicode.h -- generated by tools/gen-unicode.py from the Unicode Character Database\n")
    o.append("   %s and UTS #51's emoji sequences, Emoji %s: the properties a regular expression's \\p\n" % (u.version, emoji_version))
    o.append("   names, the case mappings, and the case orbits its i flag reads. Included by src/js/regexp.c\n")
    o.append("   alone. Do not edit. */\n")
    o.append('#define UNI_VERSION "%s"\n' % u.version)
    o.append('#define UNI_EMOJI_VERSION "%s"\n\n' % emoji_version)
    o.append("/* every property's ranges, [first, last] pairs, one after another */\n")
    listing(o, "uni_ranges", "uint32_t", 12, [x for r in ranges for x in r])
    o.append("/* a property: its names (| between aliases), its kind (0 a general category, 1 a script,\n")
    o.append("   2 a script's extensions, 3 a binary property), its ranges' first pair and their count */\n")
    o.append("static const struct uni_prop { const char *names; uint8_t kind; uint32_t off, n; } uni_props[] = {\n")
    for p in props:
        o.append('    { "%s", %d, %d, %d },\n' % p)
    o.append("};\n#define UNI_NPROPS %d\n" % len(props))
    o.append("/* the binary properties the engine reads besides \\p (the final sigma's context, the names of\n")
    o.append("   identifiers): their places in uni_props */\n")
    for r in READ:
        o.append("#define UNI_P_%s %d\n" % (r.upper(), next(i for i, p in enumerate(props) if p[1] == 3 and r in p[0].split("|"))))
    o.append("\n")
    o.append("/* the properties of strings: each sequence its length, then its code points */\n")
    listing(o, "uni_seqs", "uint32_t", 12, seqwords)
    o.append("/* a property of strings: its name, its sequences' words in uni_seqs (from off, len of them, n\n")
    o.append("   sequences), its single code points' ranges in uni_ranges (rn pairs from roff) */\n")
    o.append("static const struct uni_strprop { const char *name; uint32_t off, len, n, roff, rn; } uni_strprops[] = {\n")
    for s in strp:
        o.append('    { "%s", %d, %d, %d, %d, %d },\n' % s)
    o.append("};\n#define UNI_NSTRPROPS %d\n\n" % len(strp))
    o.append("/* a simple case mapping: runs [lo, hi] whose every stride-th code point from lo maps by delta */\n")
    o.append("struct uni_case { uint32_t lo, hi; int32_t delta; uint32_t stride; };\n")
    for name, m in (("uni_upper", u.upper), ("uni_lower", u.lower), ("uni_fold", fold)):
        r = case_runs(m)
        o.append("static const struct uni_case %s[] = {\n" % name)
        for x in r:
            o.append("    { %d, %d, %d, %d },\n" % x)
        o.append("};\n#define %s_N %d\n" % (name.upper(), len(r)))
    o.append("\n/* the full mappings to more than one code point: the code point, then up to three */\n")
    o.append("struct uni_special { uint32_t cp, to[3]; };\n")
    for name, sp in (("uni_upper_special", upsp), ("uni_lower_special", losp)):
        o.append("static const struct uni_special %s[] = {\n" % name)
        for s in sp:
            to = (s[1:] + [0, 0, 0])[:3]
            o.append("    { %d, { %d, %d, %d } },\n" % (s[0], to[0], to[1], to[2]))
        o.append("};\n#define %s_N %d\n" % (name.upper(), len(sp)))
    o.append("\n/* the case orbits: (code point, the next in its orbit) pairs, sorted; an orbit is a cycle\n")
    o.append("   that rises and comes back to its least. U: simple case folding's, with u; B: the\n")
    o.append("   uppercase canonicalization of code units, without */\n")
    listing(o, "uni_orbit_u", "uint32_t", 12, [x for p in ou for x in p])
    o.append("#define UNI_ORBIT_U_N %d\n" % len(ou))
    listing(o, "uni_orbit_b", "uint32_t", 12, [x for p in ob for x in p])
    o.append("#define UNI_ORBIT_B_N %d\n" % len(ob))
    return "".join(o)


# ---- normalize.h ----

S_BASE, L_BASE, V_BASE, T_BASE = 0xAC00, 0x1100, 0x1161, 0x11A7
L_COUNT, V_COUNT, T_COUNT = 19, 21, 28
N_COUNT = V_COUNT * T_COUNT
S_COUNT = L_COUNT * N_COUNT


def normalize_h(u):
    def decompose(cp, compat, out):
        s = cp - S_BASE
        if 0 <= s < S_COUNT:
            out.append(L_BASE + s // N_COUNT)
            out.append(V_BASE + s % N_COUNT // T_COUNT)
            if s % T_COUNT:
                out.append(T_BASE + s % T_COUNT)
            return
        d = u.dm.get(cp)
        if d and (compat or d[0] != "<"):
            for x in d.split():
                if x[0] != "<":
                    decompose(int(x, 16), compat, out)
        else:
            out.append(cp)

    def reorder(s):
        """the canonical ordering: each run of non-starters sorted by class, stably"""
        s = list(s)
        for i in range(1, len(s)):
            j = i
            while j > 0 and u.ccc.get(s[j], 0) and u.ccc.get(s[j - 1], 0) > u.ccc.get(s[j], 0):
                s[j - 1], s[j] = s[j], s[j - 1]
                j -= 1
        return s

    def nfd(cp, compat):
        out = []
        decompose(cp, compat, out)
        return reorder(out)

    # the canonical combining classes other than 0, as runs of one class
    ccc = []
    for cp in range(MAX):
        cc = u.ccc.get(cp)
        if not cc:
            continue
        if ccc and ccc[-2] == cp - 1 and ccc[-1] == cc:
            ccc[-2] = cp
        else:
            ccc += [cp, cp, cc]
    # the full decompositions: NFD's where it changes a code point, NFKD's where it differs from NFD's
    nfds, nfkds, dec, canon = [], [], [], []
    for cp in range(MAX):
        if 0xD800 <= cp <= 0xDFFF or S_BASE <= cp < S_BASE + S_COUNT:
            continue
        if cp not in u.dm:
            continue
        d, k = nfd(cp, False), nfd(cp, True)
        if d != [cp]:
            nfds += [cp | len(d) << 24, len(dec)]
            dec += d
            canon.append(cp)
        if k != d:
            nfkds += [cp | len(k) << 24, len(dec)]
            dec += k
    # the primary composites: each canonical mapping of two code points that composes back,
    # the full composition exclusions apart
    excl = u.binary(["Full_Composition_Exclusion"])["Full_Composition_Exclusion"]
    excluded = set(c for lo, hi in excl for c in range(lo, hi + 1))
    comp, seen = [], {}
    for cp in canon:
        if cp in excluded:
            continue
        m = u.dm[cp].split()
        if m[0][0] == "<" or len(m) != 2:
            continue
        pair = (int(m[0], 16), int(m[1], 16))
        if pair in seen:
            sys.exit("U+%04X and U+%04X compose from one pair" % (seen[pair], cp))
        seen[pair] = cp
        comp.append(pair + (cp,))
    comp.sort()
    qc = {}
    for f in data(u.file("DerivedNormalizationProps.txt")):
        if len(f) == 3 and f[1] in ("NFC_QC", "NFKC_QC"):
            r = cps(f[0])
            qc.setdefault(f[1] + "_" + f[2], []).append([r.start, r.stop - 1])

    o = []
    o.append("/* src/js/normalize.h -- generated by tools/gen-unicode.py --normalize from the Unicode\n")
    o.append("   Character Database %s: the tables String.prototype.normalize reads.\n" % u.version)
    o.append("   Included by src/js/normalize.c alone. Do not edit. */\n")
    o.append('#define UNI_NORM_VERSION "%s"\n\n' % u.version)
    o.append("/* the canonical combining classes other than 0: runs [lo, hi] of one class, three words each */\n")
    listing(o, "uni_ccc", "uint32_t", 12, ccc)
    o.append("#define UNI_CCC_N %d\n" % (len(ccc) // 3))
    o.append("/* the full canonical decompositions (NFD's) of the code points they change, Hangul's\n")
    o.append("   syllables apart: each the code point with its length above bit 24, then its first\n")
    o.append("   code point's place in uni_dec */\n")
    listing(o, "uni_nfd", "uint32_t", 8, nfds)
    o.append("#define UNI_NFD_N %d\n" % (len(nfds) // 2))
    o.append("/* the full compatibility decompositions (NFKD's) where they differ from the canonical */\n")
    listing(o, "uni_nfkd", "uint32_t", 8, nfkds)
    o.append("#define UNI_NFKD_N %d\n" % (len(nfkds) // 2))
    o.append("/* the decompositions' code points */\n")
    listing(o, "uni_dec", "uint32_t", 12, dec)
    o.append("/* the primary composites: (first, second, composite), sorted by first then second */\n")
    listing(o, "uni_comp", "uint32_t", 12, [x for c in comp for x in c])
    o.append("#define UNI_COMP_N %d\n" % len(comp))
    o.append("/* the quick checks: the ranges whose NFC_QC (NFKC_QC) is No, and Maybe */\n")
    for q in ("NFC_QC_N", "NFC_QC_M", "NFKC_QC_N", "NFKC_QC_M"):
        r = merge(qc.get(q, []))
        listing(o, "uni_" + q.lower(), "uint32_t", 12, [x for p in r for x in p])
        o.append("#define UNI_%s_N %d\n" % (q, len(r)))
    return "".join(o)


def main():
    a = sys.argv[1:]
    if a[:1] == ["--normalize"] and len(a) == 2:
        sys.stdout.write(normalize_h(UCD(a[1])))
    elif len(a) == 2 and a[0] != "--normalize":
        sys.stdout.write(unicode_h(UCD(a[0]), a[1]))
    else:
        sys.exit("usage: tools/gen-unicode.py UCD EMOJI > src/js/unicode.h\n"
                 "       tools/gen-unicode.py --normalize UCD > src/js/normalize.h")


if __name__ == "__main__":
    main()
