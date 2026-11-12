#!/usr/bin/env python3
"""tools/gen-intl.py [SRC] -- writes src/js/intl_data.c and src/js/intl_data.h, the locale
data src/js/intl.c (ECMA-402, Intl) reads, from CLDR's JSON (the cldr-json packages of
CLDR 48.0.0, the version node 22's ICU 78 carries):

    python3 tools/gen-intl.py          (fetches the packages from the npm registry)
    python3 tools/gen-intl.py SRC      (SRC holds the packages' tarballs, NAME-48.0.0.tgz)

The output is a function of the files alone. What it carries:
  - the locales of LANGS, each with every region and script CLDR has for it, ICU's
    names for them (its default contents and its aliases), and which of them each
    service offers (a collator and plural rules have lists of their own);
  - BCP 47's canonicalization: CLDR's language, script, territory and variant aliases,
    the Unicode extension's value aliases, and the likely subtags (all of them, for
    maximize, minimize and a territory alias of several regions);
  - for each region: its calendars, its hour cycles, its week, its time zones.

The C side holds no pointer: the data is one array of bytes packed four to a 32-bit
word (cicilang reads a table of numbers much faster than a string literal), with the
sections' places in intl_data.h. A string is UTF-8 ended by a NUL, found by its byte
offset; a number is little-endian."""
import io
import json
import os
import sys
import tarfile
import urllib.request

CLDR = "48.0.0"
PACKAGES = ["cldr-core", "cldr-numbers-full", "cldr-dates-full", "cldr-units-full",
            "cldr-localenames-full", "cldr-misc-full", "cldr-bcp47"]

# the languages carried: the web's most used, as their locales name them
LANGS = ("en de fr es it pt nl ru pl ja zh ko hi ar he tr sv da nb no fi cs uk el hu ro "
         "id vi th fa").split()

# ICU installs names of its own beside CLDR's locales: its aliases (zh-TW is
# zh-Hant-TW's data under the region alone) ...
ICU_ALIASES = {"zh-TW": "zh-Hant-TW", "zh-HK": "zh-Hant-HK", "zh-MO": "zh-Hant-MO",
               "zh-SG": "zh-Hans-SG", "zh-CN": "zh-Hans-CN",
               "sr-RS": "sr-Cyrl-RS", "sr-ME": "sr-Latn-ME", "sr-BA": "sr-Cyrl-BA",
               "sr-XK": "sr-Cyrl-XK"}
# ... and leaves out some of CLDR's: these scripts are filtered from its data, and nb's
# regions are aliases of no's in ICU 78 (nb-NO resolves to nb)
ICU_DROPPED = {"el-polyton", "en-Dsrt", "en-Dsrt-US", "en-Shaw", "en-Shaw-GB", "zh-Latn",
               "zh-Latn-CN", "nb-NO", "nb-SJ"}
# the locales ICU's collator offers, of the carried languages (its collation data's)
COLLATOR = ("ar ar-SA cs da de de-AT el en en-US es fa fa-AF fi fr fr-CA he he-IL hi hu "
            "id id-ID it ja ko nb nb-NO nl no pl pt ro ru sr-BA sr-ME sr-RS sv th tr uk vi "
            "zh zh-CN zh-HK zh-Hans zh-Hans-CN zh-Hans-SG zh-Hant zh-Hant-HK zh-Hant-MO "
            "zh-Hant-TW zh-MO zh-SG zh-TW").split()
# the collations each language has beside the root's (ICU's collation data; the
# root's own are emoji and eor), for Intl.Locale's collations
COLLATIONS = {"ar": ["compat"], "de": ["phonebk"], "es": ["trad"], "fi": ["trad"],
              "ja": ["unihan"], "ko": ["searchjl", "unihan"], "sv": ["trad"],
              "vi": ["trad"], "zh": ["pinyin", "stroke", "unihan", "zhuyin"]}


# ---- the sources ----

class Source:
    """The packages' files, read from their tarballs (fetched, or in a directory)."""

    def __init__(self, src):
        self.tars = {}
        for p in PACKAGES:
            name = "%s-%s.tgz" % (p, CLDR)
            if src:
                with open(os.path.join(src, name), "rb") as f:
                    raw = f.read()
            else:
                url = "https://registry.npmjs.org/%s/-/%s" % (p, name)
                with urllib.request.urlopen(url) as r:
                    raw = r.read()
            self.tars[p] = tarfile.open(fileobj=io.BytesIO(raw), mode="r:gz")
        self.cache = {}

    def json(self, package, path):
        key = (package, path)
        if key not in self.cache:
            f = self.tars[package].extractfile("package/" + path)
            self.cache[key] = json.loads(f.read().decode("utf-8")) if f else None
        return self.cache[key]

    def has(self, package, path):
        try:
            self.tars[package].getmember("package/" + path)
            return True
        except KeyError:
            return False


# ---- the blob: bytes, and strings in them ----

class Blob:
    def __init__(self):
        self.b = bytearray(b"\0")          # offset 0 is the empty string
        self.strs = {"": 0}
        self.marks = []                    # (name, value) for the header

    def string(self, s):
        if s not in self.strs:
            self.strs[s] = len(self.b)
            self.b += s.encode("utf-8") + b"\0"
        return self.strs[s]

    def here(self):
        return len(self.b)

    def u8(self, v):
        assert 0 <= v < 256, v
        self.b.append(v)

    def u16(self, v):
        assert 0 <= v < 65536, v
        self.b += bytes((v & 255, v >> 8))

    def u32(self, v):
        assert 0 <= v < 1 << 32, v
        self.b += bytes((v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24))

    def mark(self, name, value=None):
        self.marks.append((name, self.here() if value is None else value))


# ---- locales ----

def locale_list(S):
    avail = S.json("cldr-core", "availableLocales.json")["availableLocales"]["full"]
    dc = S.json("cldr-core", "defaultContent.json")["defaultContent"]
    names = set()
    for x in avail + dc + list(ICU_ALIASES):
        if x.split("-")[0] in LANGS and x not in ICU_DROPPED:
            names.add(x)
    return sorted(names)


def parent_of(S, name, names):
    """A locale's parent: CLDR's parentLocales, else the name truncated (a default
    content's data is its parent's, an alias's its target's); None for root."""
    if name in ICU_ALIASES:
        return ICU_ALIASES[name]
    pl = S.json("cldr-core", "supplemental/parentLocales.json")["supplemental"]["parentLocales"]["parentLocale"]
    if name in pl:
        p = pl[name]
        return None if p in ("root", "und") else p
    parts = name.split("-")
    while len(parts) > 1:
        parts.pop()
        cand = "-".join(parts)
        if cand in names:
            return cand
    return None


def plural_locales(S):
    p = S.json("cldr-core", "supplemental/plurals.json")["supplemental"]["plurals-type-cardinal"]
    return sorted(k for k in p if k.split("-")[0] in LANGS)


# ---- canonicalization ----

def write_pairs(B, name, pairs):
    """a table of (key, value) strings sorted by key's bytes: its count, then pairs of offsets"""
    pairs = sorted(pairs, key=lambda kv: kv[0].encode("utf-8"))
    offs = [(B.string(k), B.string(v)) for k, v in pairs]
    B.mark("IDB_%s_N" % name, len(offs))
    B.mark("IDB_%s" % name)
    for k, v in offs:
        B.u32(k)
        B.u32(v)


def aliases(S, B):
    a = S.json("cldr-core", "supplemental/aliases.json")["supplemental"]["metadata"]["alias"]

    def table(kind):
        return [(k.replace("-", "_"), v["_replacement"]) for k, v in a[kind].items()]
    # language rules are keyed as ICU keys them: language, then region, then variant, by "_"
    write_pairs(B, "LANG_ALIAS", table("languageAlias"))
    write_pairs(B, "SCRIPT_ALIAS", table("scriptAlias"))
    write_pairs(B, "REGION_ALIAS", table("territoryAlias"))
    write_pairs(B, "VARIANT_ALIAS", table("variantAlias"))
    # the Unicode extension's values: "key-old" -> new, from the BCP 47 data's aliases
    ext = []
    for f in ("calendar", "collation", "measure", "number"):
        for key, vals in S.json("cldr-bcp47", "bcp47/%s.json" % f)["keyword"]["u"].items():
            for v, info in vals.items():
                if not isinstance(info, dict) or v.startswith("_"):
                    continue
                # a deprecated value names its replacement; another's aliases are its old names
                if "_preferred" in info:
                    ext.append(("%s-%s" % (key, v), info["_preferred"]))
                    continue
                for old in info.get("_alias", "").split():
                    if old != v and old not in ("true", "yes"):
                        ext.append(("%s-%s" % (key, old), v))
    tz = S.json("cldr-bcp47", "bcp47/timezone.json")["keyword"]["u"]["tz"]
    for v, info in tz.items():
        if isinstance(info, dict) and "_preferred" in info:
            ext.append(("tz-%s" % v, info["_preferred"]))
    seen = {}
    for k, v in ext:
        seen.setdefault(k, v)
    write_pairs(B, "UEXT_ALIAS", list(seen.items()))


def likely(S, B):
    """the likely subtags, keyed as CLDR keys them (en, und-Latn, zh-TW ...); a value whose
    language is its key's is written without it (en: -Latn-US)"""
    d = S.json("cldr-core", "supplemental/likelySubtags.json")["supplemental"]["likelySubtags"]
    rows = []
    for k, v in d.items():
        lang = k.split("-")[0]
        if v.split("-")[0] == lang:
            v = v[len(lang):]
        rows.append((k, v))
    write_pairs(B, "LIKELY", rows)


# ---- regions ----

def regions(S, B):
    """each region's facts, a table sorted by region (its code's two or three bytes):
    calendars (the preferred first), the hour cycles allowed (the preferred first, as
    h11 h12 h23 h24), the week (first day, the weekend's days, the minimal days, ISO
    numbering: 1 Monday .. 7 Sunday), its time zones (ICU's canonical names, sorted)"""
    cal = S.json("cldr-core", "supplemental/calendarPreferenceData.json")["supplemental"]["calendarPreferenceData"]
    td = S.json("cldr-core", "supplemental/timeData.json")["supplemental"]["timeData"]
    wd = S.json("cldr-core", "supplemental/weekData.json")["supplemental"]["weekData"]
    tz = S.json("cldr-bcp47", "bcp47/timezone.json")["keyword"]["u"]["tz"]
    days = {"mon": 1, "tue": 2, "wed": 3, "thu": 4, "fri": 5, "sat": 6, "sun": 7}
    bcp_cal = {"gregorian": "gregory", "ethiopic-amete-alem": "ethioaa"}
    # a zone's region: its key's first two letters, but for the keys that are not so made
    # (the region the IANA data gives those), and the zones of no region
    unregioned = {"utc", "gmt", "unk", "cst6cdt", "est5edt", "mst7mdt", "pst8pdt"}
    odd = {"gaza": "PS", "gazastrp": "PS", "hebron": "PS", "jeruslm": "IL"}
    zones = {}
    for k, info in tz.items():
        if not isinstance(info, dict) or "_alias" not in info or info.get("_deprecated"):
            continue
        if k in unregioned or k.startswith("utce") or k.startswith("utcw"):
            continue
        r = odd.get(k, k[:2].upper())
        zones.setdefault(r, []).append(info["_alias"].split()[0])
    regs = set(cal) | set(td) | set(wd["firstDay"]) | set(wd["minDays"]) | set(zones)
    regs.discard("001")

    def hc(s):
        out = []
        for t in s.split():
            c = {"h": "h12", "H": "h23", "K": "h11", "k": "h24"}.get(t[0])
            if c and t in ("h", "H", "K", "k", "hb", "hB", "Hb", "HB") and c not in out:
                out.append(c)
        return out

    def week(r):
        first = days[wd["firstDay"].get(r, wd["firstDay"]["001"])]
        ws = days[wd["weekendStart"].get(r, wd["weekendStart"]["001"])]
        we = days[wd["weekendEnd"].get(r, wd["weekendEnd"]["001"])]
        wk = [ws]
        while wk[-1] != we:
            wk.append(wk[-1] % 7 + 1)
        return first, sorted(wk), int(wd["minDays"].get(r, wd["minDays"]["001"]))

    def record(r):
        cals = [bcp_cal.get(c, c) for c in cal.get(r, cal["001"])]
        t = td.get(r) or td.get("001")
        allowed = hc(t["_preferred"] + " " + t["_allowed"])
        first, wk, mind = week(r)
        z = sorted(zones.get(r, []))
        return "%s|%s|%d%s%d|%s" % (" ".join(cals), " ".join(allowed), first, "".join(map(str, wk)), mind, " ".join(z))

    rows = [(r, record(r)) for r in sorted(regs)]
    rows.append(("001", record("001")))
    write_pairs(B, "REGION", rows)


# ---- the locales' table ----

def locales(S, B):
    names = locale_list(S)
    plurals = set(plural_locales(S))
    coll = set(COLLATOR)
    idx = {n: i for i, n in enumerate(names)}
    # every parent must be a record too (en-001, es-419 ...): the list holds them all
    for n in names:
        p = parent_of(S, n, set(names))
        assert p is None or p in idx, (n, p)
    B.mark("IDB_LOCALES_N", len(names))
    rows = []
    for n in names:
        p = parent_of(S, n, set(names))
        flags = 1 | (2 if n in coll else 0)
        rows.append((B.string(n), idx[p] if p else 0xFFFF, flags))
    B.mark("IDB_LOCALES")
    for tag, parent, flags in rows:
        B.u32(tag)
        B.u16(parent)
        B.u16(flags)
    # the plural rules' locales, and the collations of each language
    write_pairs(B, "PLURAL_LOCALES", [(p, p) for p in plurals])
    write_pairs(B, "COLLATIONS", [(k, " ".join(sorted(["emoji", "eor"] + v))) for k, v in COLLATIONS.items()])
    # the scripts written right to left
    sm = S.json("cldr-core", "scriptMetadata.json")["scriptMetadata"]
    write_pairs(B, "RTL_SCRIPTS", [(k, k) for k, v in sm.items() if v.get("rtl") == "YES"])
    # the default numbering system of each locale whose own is not latn
    ns = []
    for n in names:
        d = main_data(S, "cldr-numbers-full", n, "numbers.json", set(names))
        nu = d["numbers"]["defaultNumberingSystem"] if d else "latn"
        if nu != "latn":
            ns.append((n, nu))
    write_pairs(B, "NUMSYS", ns)


def main_data(S, package, name, file, names):
    """a locale's file of a package (its main/NAME/FILE's data), else its parent's: a
    default content's or an alias's data is its parent's"""
    while name:
        path = "main/%s/%s" % (name, file)
        if S.has(package, path):
            return S.json(package, path)["main"][name]
        name = parent_of(S, name, names)
    return None


# ---- output ----

def emit(B, out_c, out_h):
    data = bytes(B.b)
    data += b"\0" * (-len(data) % 4)
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    with open(out_c, "w") as f:
        f.write("/* src/js/intl_data.c -- generated by tools/gen-intl.py from CLDR %s's JSON. Do not edit;\n"
                "   run the script. The bytes of the locale data src/js/intl.c reads, four to a word,\n"
                "   little-endian; src/js/intl_data.h says where each section begins. */\n" % CLDR)
        f.write("#include <stdint.h>\n\n")
        f.write("const uint32_t js_intl_db_words = %du;\n" % len(words))
        f.write("const uint32_t js_intl_db[%d] = {\n" % len(words))
        for i in range(0, len(words), 8):
            f.write("    " + ", ".join("0x%08xu" % w for w in words[i:i + 8]) + ",\n")
        f.write("};\n")
    with open(out_h, "w") as f:
        f.write("/* src/js/intl_data.h -- generated by tools/gen-intl.py from CLDR %s's JSON. Do not edit;\n"
                "   run the script. Where each section of src/js/intl_data.c's bytes begins (a byte\n"
                "   offset), and the counts of its tables. */\n" % CLDR)
        f.write("#define IDB_CLDR_VERSION \"%s\"\n" % CLDR)
        for name, v in B.marks:
            f.write("#define %s %du\n" % (name, v))
        f.write("#define IDB_BYTES %du\n" % len(B.b))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    S = Source(src)
    B = Blob()
    locales(S, B)
    aliases(S, B)
    likely(S, B)
    regions(S, B)
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    emit(B, os.path.join(root, "src/js/intl_data.c"), os.path.join(root, "src/js/intl_data.h"))


if __name__ == "__main__":
    main()
