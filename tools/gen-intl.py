#!/usr/bin/env python3
"""tools/gen-intl.py [SRC] -- writes src/js/intl_data.c and src/js/intl_data.h, the locale
data src/js/intl.c (ECMA-402, Intl) reads, from CLDR's JSON (the cldr-json packages of
CLDR 48.2.0, the data node 22's ICU 78.3 carries):

    python3 tools/gen-intl.py          (fetches the packages from the npm registry)
    python3 tools/gen-intl.py SRC      (SRC holds the packages' tarballs, NAME-48.2.0.tgz,
                                        CLDR 48.2's common/main/root.xml, and in main/
                                        the common/main XML files of the locales)

The output is a function of the files alone. What it carries:
  - the locales of LANGS, each with every region and script CLDR has for it, ICU's
    names for them (its default contents and its aliases), and which of them each
    service offers (a collator and plural rules have lists of their own);
  - BCP 47's canonicalization: CLDR's language, script, territory and variant aliases,
    the Unicode extension's value aliases, and the likely subtags (all of them, for
    maximize, minimize and a territory alias of several regions);
  - for each region: its calendars, its hour cycles, its week, its time zones;
  - each locale's resources, as records: the symbols and patterns of each numbering
    system it has (and root's arab and arabext, which ICU's root has and the JSON's und
    leaves out: they come from root.xml), its compact patterns, its minimum grouping,
    the currencies' symbols and names, the units' names and patterns. A locale keeps
    the records that differ from its parent's;
  - the numbering systems' digits, the currencies' minor units, the plural rules
    (cardinal and ordinal), compiled;
  - the dates' names and patterns, the day periods' rules, the hour cycles of the
    regions;
  - the list patterns, the relative times, the date fields' names, the display names
    of languages, regions, scripts, variants, currencies and calendars (each locale
    the names that differ from its parent's);
  - CLDR's root collation (allkeys_CLDR.txt, in SRC or fetched): each code point's
    collation elements and the contractions;
  - Unicode 17's break properties (UAX #29's grapheme, word and sentence classes,
    Extended_Pictographic, Indic_Conjunct_Break) as ranges, from the UCD (in SRC's
    ucd/, else fetched). The available formats come from the locales' XML files: the JSON resolves
    them, and ICU's pattern generator adds a locale's own before its parents'.

The C side holds no pointer: the data is one array of bytes packed four to a 32-bit
word (cicilang reads a table of numbers much faster than a string literal), with the
sections' places in intl_data.h. A string is UTF-8 ended by a NUL, found by its byte
offset; a number is little-endian."""
import io
import json
import os
import re
import sys
import tarfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

CLDR = "48.2.0"
PACKAGES = ["cldr-core", "cldr-numbers-full", "cldr-dates-full", "cldr-units-full",
            "cldr-localenames-full", "cldr-misc-full", "cldr-bcp47"]
ROOT_XML = "https://raw.githubusercontent.com/unicode-org/cldr/release-48-2/common/main/root.xml"
MAIN_XML = "https://raw.githubusercontent.com/unicode-org/cldr/release-48-2/common/main/%s.xml"
UCD = "https://www.unicode.org/Public/17.0.0/ucd/%s"
ALLKEYS = "https://raw.githubusercontent.com/unicode-org/cldr/release-48-2/common/uca/allkeys_CLDR.txt"
UCD_FILES = {"GraphemeBreakProperty.txt": "auxiliary/", "WordBreakProperty.txt": "auxiliary/",
             "SentenceBreakProperty.txt": "auxiliary/", "emoji-data.txt": "emoji/", "DerivedCoreProperties.txt": ""}

# the languages carried: the web's most used, as their locales name them
LANGS = ("en de fr es it pt nl ru pl ja zh ko hi ar he tr sv da nb no fi cs uk el hu ro "
         "id vi th fa").split()

# ICU installs names of its own beside CLDR's locales: its aliases (zh-TW is
# zh-Hant-TW's data under the region alone) ...
ICU_ALIASES = {"zh-TW": "zh-Hant-TW", "zh-HK": "zh-Hant-HK", "zh-MO": "zh-Hant-MO",
               "zh-SG": "zh-Hans-SG", "zh-CN": "zh-Hans-CN",
               "sr-RS": "sr-Cyrl-RS", "sr-ME": "sr-Latn-ME", "sr-BA": "sr-Cyrl-BA",
               "sr-XK": "sr-Cyrl-XK",
               # ... and no_NO, the region of no's data (CLDR's default content is nb-NO)
               "no-NO": "no"}
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
        # CLDR's root.xml: ICU's root has numbering systems' data the JSON's und leaves out
        if src:
            with open(os.path.join(src, "root.xml"), "rb") as f:
                self.root_xml = f.read()
        else:
            with urllib.request.urlopen(ROOT_XML) as r:
                self.root_xml = r.read()
        self.src = src
        self.xml_cache = {}

    def ucd(self, name):
        """a UCD file's lines, without comments and blank ones"""
        if self.src:
            with open(os.path.join(self.src, "ucd", name), "rb") as f:
                raw = f.read()
        else:
            with urllib.request.urlopen(UCD % (UCD_FILES[name] + name)) as r:
                raw = r.read()
        out = []
        for line in raw.decode("utf-8").split("\n"):
            line = line.split("#")[0].strip()
            if line:
                out.append([x.strip() for x in line.split(";")])
        return out

    def allkeys(self):
        """CLDR's root collation table's lines"""
        if self.src:
            with open(os.path.join(self.src, "allkeys_CLDR.txt"), "rb") as f:
                raw = f.read()
        else:
            with urllib.request.urlopen(ALLKEYS) as r:
                raw = r.read()
        return raw.decode("utf-8").split("\n")

    def main_xml(self, name):
        """a locale's common/main XML file (its name's dashes underscores), None when CLDR
        has none (a default content's, an alias's)"""
        if name not in self.xml_cache:
            n = name.replace("-", "_")
            raw = None
            if self.src:
                path = os.path.join(self.src, "main", n + ".xml")
                if os.path.exists(path):
                    with open(path, "rb") as f:
                        raw = f.read()
            else:
                try:
                    with urllib.request.urlopen(MAIN_XML % n) as r:
                        raw = r.read()
                except urllib.error.HTTPError:
                    raw = None
            self.xml_cache[name] = raw
        return self.xml_cache[name]

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
    """every locale with plural rules (ICU's PluralRules offers them all)"""
    p = S.json("cldr-core", "supplemental/plurals.json")["supplemental"]["plurals-type-cardinal"]
    return sorted(p)


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
    # each locale's resources: what differs from its parent's (root's in full), a table
    # of (key, string) sorted by key
    res = {n: resources(S, n, set(names)) for n in names}
    root = resources(S, None, set(names))
    root = {k: (names_record(v) if isinstance(v, dict) else v) for k, v in root.items()}
    rows = []
    for n in names:
        p = parent_of(S, n, set(names))
        flags = 1 | (2 if n in coll else 0)
        base = res[p] if p else root
        diff = []
        for k, v in res[n].items():
            b = base.get(k)
            if isinstance(v, dict):
                sub = {c: x for c, x in v.items() if (b or {}).get(c) != x}
                if sub:
                    diff.append((k, names_record(sub)))
            elif b != v:
                diff.append((k, v))
        diff.sort()
        rows.append((B.string(n), idx[p] if p else 0xFFFF, flags, diff))
    # the strings first: a table is contiguous
    for tag, parent, flags, diff in rows:
        for k, v in diff:
            B.string(v)
    for v in root.values():
        B.string(v)
    tables = []
    for tag, parent, flags, diff in rows:
        tables.append((B.here(), len(diff)))
        for k, v in diff:
            B.u32(k)
            B.u32(B.string(v))
    B.mark("IDB_ROOT_RES_N", len(root))
    B.mark("IDB_ROOT_RES")
    for k, v in sorted(root.items()):
        B.u32(k)
        B.u32(B.string(v))
    B.mark("IDB_LOCALES")
    for (tag, parent, flags, diff), (off, n) in zip(rows, tables):
        B.u32(tag)
        B.u16(parent)
        B.u16(flags)
        B.u32(off)
        B.u32(n)
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
    default content's or an alias's data is its parent's; None, root's (und)"""
    while name:
        path = "main/%s/%s" % (name, file)
        if S.has(package, path):
            return S.json(package, path)["main"][name]
        name = parent_of(S, name, names)
    path = "main/und/%s" % file
    return S.json(package, path)["main"]["und"] if S.has(package, path) else None


# ---- the resources: a locale's data as records keyed (category << 20 | index), each a
# string of fields joined by FS ----

FS = "\x1f"
K_NUM, K_COMPACT, K_MINGROUP, K_CUR, K_UNIT, K_UNITPER = 1, 2, 3, 4, 5, 6
K_MONTHS, K_DAYS, K_DAYPERIODS, K_ERAS, K_DATEFMT, K_TIMEFMT, K_DTGLUE, K_AVAIL, K_APPEND, K_FIELDNAMES, K_INTERVAL, \
    K_TZ = range(7, 19)
K_LIST, K_RELTIME, K_DTFIELD, K_LOCPAT, K_LANGN, K_REGIONN, K_SCRIPTN, K_VARIANTN, K_CURN, K_CALN = range(19, 29)
KEYS = {"IDK_NUM": K_NUM, "IDK_COMPACT": K_COMPACT, "IDK_MINGROUP": K_MINGROUP, "IDK_CUR": K_CUR,
        "IDK_UNIT": K_UNIT, "IDK_UNITPER": K_UNITPER, "IDK_MONTHS": K_MONTHS, "IDK_DAYS": K_DAYS,
        "IDK_DAYPERIODS": K_DAYPERIODS, "IDK_ERAS": K_ERAS, "IDK_DATEFMT": K_DATEFMT, "IDK_TIMEFMT": K_TIMEFMT,
        "IDK_DTGLUE": K_DTGLUE, "IDK_AVAIL": K_AVAIL, "IDK_APPEND": K_APPEND, "IDK_FIELDNAMES": K_FIELDNAMES,
        "IDK_INTERVAL": K_INTERVAL, "IDK_TZ": K_TZ, "IDK_LIST": K_LIST, "IDK_RELTIME": K_RELTIME,
        "IDK_DTFIELD": K_DTFIELD, "IDK_LOCPAT": K_LOCPAT, "IDK_LANGN": K_LANGN, "IDK_REGIONN": K_REGIONN,
        "IDK_SCRIPTN": K_SCRIPTN, "IDK_VARIANTN": K_VARIANTN, "IDK_CURN": K_CURN, "IDK_CALN": K_CALN}


def key(cat, i=0):
    return cat << 20 | i


# a numbering system's record: its symbols, its patterns, the currency names' unit patterns
NUM_SYMBOLS = ["decimal", "group", "percentSign", "plusSign", "minusSign", "approximatelySign", "exponential",
               "superscriptingExponent", "perMille", "infinity", "nan", "timeSeparator"]
NUM_PATTERNS = [("decimalFormats", "standard"), ("percentFormats", "standard"), ("currencyFormats", "standard"),
                ("currencyFormats", "accounting"), ("currencyFormats", "standard-noCurrency"),
                ("scientificFormats", "standard"), ("miscPatterns", "approximately"), ("miscPatterns", "atLeast"),
                ("miscPatterns", "atMost"), ("miscPatterns", "range")]
COUNTS = ["zero", "one", "two", "few", "many", "other"]
# the compact patterns' plural forms: CLDR's categories, then the explicit 1 ("1000-count-1")
COMPACT_COUNTS = COUNTS + ["1"]
COMPACT_STYLES = [("decimalFormats", "short", "decimalFormat"), ("decimalFormats", "long", "decimalFormat"),
                  ("currencyFormats", "short", "standard")]
# the numbering systems with data of their own in ICU's root (root.xml's explicit symbols and
# patterns; every other one's are aliases of the locale's latn): read from CLDR's XML
ROOT_NS = ("arab", "arabext")

# the currencies carried by name (their symbols and display names); another code is its own symbol
CURRENCIES = sorted(set(("USD EUR JPY GBP CNY AUD CAD CHF HKD SGD SEK KRW NOK NZD INR MXN TWD ZAR BRL DKK PLN THB "
                         "ILS IDR CZK AED TRY HUF CLP SAR PHP MYR COP RUB RON PEN BHD BGN ARS EGP KWD NGN PKR QAR "
                         "UAH VND KZT MAD BDT LKR ISK JOD OMR TND DZD IQD IRR KES GHS XOF XAF ETB UYU BOB PYG CRC "
                         "DOP GTQ HNL NIO PAB BYN GEL AMD AZN MDL RSD MKD ALL BAM HRK TTD JMD").split()))
# ECMA-402's sanctioned simple units, and CLDR's names for them; the compound ones CLDR has
# whole, of two sanctioned units
UNITS = {"acre": "area-acre", "bit": "digital-bit", "byte": "digital-byte", "celsius": "temperature-celsius",
         "centimeter": "length-centimeter", "day": "duration-day", "degree": "angle-degree",
         "fahrenheit": "temperature-fahrenheit", "fluid-ounce": "volume-fluid-ounce", "foot": "length-foot",
         "gallon": "volume-gallon", "gigabit": "digital-gigabit", "gigabyte": "digital-gigabyte", "gram": "mass-gram",
         "hectare": "area-hectare", "hour": "duration-hour", "inch": "length-inch", "kilobit": "digital-kilobit",
         "kilobyte": "digital-kilobyte", "kilogram": "mass-kilogram", "kilometer": "length-kilometer",
         "liter": "volume-liter", "megabit": "digital-megabit", "megabyte": "digital-megabyte", "meter": "length-meter",
         "microsecond": "duration-microsecond", "mile": "length-mile", "mile-scandinavian": "length-mile-scandinavian",
         "milliliter": "volume-milliliter", "millimeter": "length-millimeter", "millisecond": "duration-millisecond",
         "minute": "duration-minute", "month": "duration-month", "nanosecond": "duration-nanosecond",
         "ounce": "mass-ounce", "percent": "concentr-percent", "petabyte": "digital-petabyte", "pound": "mass-pound",
         "second": "duration-second", "stone": "mass-stone", "terabit": "digital-terabit", "terabyte": "digital-terabyte",
         "week": "duration-week", "yard": "length-yard", "year": "duration-year",
         "kilometer-per-hour": "speed-kilometer-per-hour", "meter-per-second": "speed-meter-per-second",
         "mile-per-hour": "speed-mile-per-hour", "liter-per-kilometer": "consumption-liter-per-kilometer",
         "mile-per-gallon": "consumption-mile-per-gallon"}
SANCTIONED = sorted(u for u in UNITS if "-per-" not in u)
UNIT_IDS = sorted(UNITS)


def root_numbers(S):
    """root.xml's explicit symbols and patterns of the ROOT_NS numbering systems"""
    import xml.etree.ElementTree as ET
    out = {}
    root = ET.fromstring(S.root_xml)
    nums = root.find("numbers")
    for ns in ROOT_NS:
        d = {}
        for el in nums.findall("symbols"):
            if el.get("numberSystem") == ns and el.find("alias") is None:
                for c in el:
                    d["sym-" + c.tag] = c.text
        for kind in ("decimalFormats", "percentFormats", "currencyFormats", "scientificFormats", "miscPatterns"):
            for el in nums.findall(kind):
                if el.get("numberSystem") != ns or el.find("alias") is not None:
                    continue
                for pat in el.iter("pattern"):
                    # a pattern under a format length with no type, of the standard format
                    alt = pat.get("alt")
                    parent_types = [x.get("type") for x in el.iter() if pat in list(x)]
                    t = parent_types[0] if parent_types and parent_types[0] else "standard"
                    if kind == "miscPatterns":
                        t = pat.get("type")
                    if alt:
                        t = t + "-" + alt
                    d.setdefault(kind + "/" + t, pat.text)
        # root's accounting format of a numbering system is an alias of its standard one
        if "currencyFormats/standard" in d:
            d.setdefault("currencyFormats/accounting", d["currencyFormats/standard"])
        out[ns] = d
    return out


def ns_record(numbers, ns, rootns):
    """a numbering system's record from a locale's numbers (CLDR JSON), its root data under it"""
    syms = numbers.get("symbols-numberSystem-" + ns)
    if syms is None and ns not in rootns:
        return None
    rd = rootns.get(ns, {})
    f = []
    for s in NUM_SYMBOLS:
        v = (syms or {}).get(s)
        f.append(v if v is not None else rd.get("sym-" + s, ""))
    for kind, t in NUM_PATTERNS:
        sect = numbers.get("%s-numberSystem-%s" % (kind, ns))
        v = sect.get(t) if sect else None
        if v is None and syms is None:
            v = rd.get("%s/%s" % (kind, t))
        f.append(v or "")
    cur = numbers.get("currencyFormats-numberSystem-" + ns) or {}
    for c in COUNTS:
        f.append(cur.get("unitPattern-count-" + c, ""))
    return FS.join(f)


def compact_record(numbers, ns, style):
    kind, length, inner = COMPACT_STYLES[style]
    sect = numbers.get("%s-numberSystem-%s" % (kind, ns))
    if not sect or length not in sect:
        return None
    pats = sect[length][inner] if inner in sect[length] else sect[length]
    f = []
    for mag in range(3, 15):
        base = "1" + "0" * mag
        for c in COMPACT_COUNTS:
            f.append(pats.get("%s-count-%s" % (base, c), ""))
    return FS.join(f)


def symbol_flags(s):
    """whether a currency symbol's last and first code points may take currency spacing
    (ICU's [[:^S:]&[:^Z:]]: not a symbol, not a separator): 1 the last, 2 the first"""
    import unicodedata
    if not s:
        return "0"
    ok = lambda ch: unicodedata.category(ch)[0] not in "SZ"
    return str((1 if ok(s[-1]) else 0) | (2 if ok(s[0]) else 0))


def resources(S, name, names):
    """a locale's resources, resolved (CLDR's JSON is); name None for root"""
    r = {}
    numdata = main_data(S, "cldr-numbers-full", name, "numbers.json", names)
    numbers = numdata["numbers"]
    rootns = root_numbers(S) if name is None else {}
    for i, (ns, digits) in enumerate(NUMBERING):
        rec = ns_record(numbers, ns, rootns)
        if rec is not None:
            r[key(K_NUM, i)] = rec
        for style in range(len(COMPACT_STYLES)):
            rec = compact_record(numbers, ns, style)
            if rec is not None:
                r[key(K_COMPACT, i * 4 + style)] = rec
    r[key(K_MINGROUP)] = str(numbers.get("minimumGroupingDigits", "1"))
    curdata = main_data(S, "cldr-numbers-full", name, "currencies.json", names)
    curs = curdata["numbers"]["currencies"]
    for i, c in enumerate(CURRENCIES):
        d = curs.get(c, {})
        sym = d.get("symbol", c)
        narrow = d.get("symbol-alt-narrow", sym)
        f = [sym, narrow, d.get("displayName", c)] + [d.get("displayName-count-" + n, "") for n in COUNTS]
        f += [symbol_flags(sym), symbol_flags(narrow)]
        r[key(K_CUR, i)] = FS.join(f)
    unitdata = main_data(S, "cldr-units-full", name, "units.json", names)
    units = unitdata["units"]
    for i, u in enumerate(UNIT_IDS):
        f = []
        for width in ("long", "short", "narrow"):
            d = units[width].get(UNITS[u], {})
            f += [d.get("displayName", ""), d.get("perUnitPattern", "")] + [d.get("unitPattern-count-" + n, "") for n in COUNTS]
        r[key(K_UNIT, i)] = FS.join(f)
    r[key(K_UNITPER)] = FS.join(units[w]["per"]["compoundUnitPattern"] for w in ("long", "short", "narrow"))
    dates(S, name, names, r)
    misc(S, name, names, r)
    return r


# the list patterns (start, middle, end, two of each type), the relative times (each
# unit and width: the relative days -2 to 2, the future's and the past's patterns by
# plural count), the date fields' names by width (UDATPG's order of the fields
# DisplayNames names), the locale display patterns; the display names, dictionaries
# (a locale keeps the entries that differ from its parent's)
LIST_TYPES = ["standard", "standard-short", "standard-narrow", "or", "or-short", "or-narrow", "unit", "unit-short",
              "unit-narrow"]
RT_UNITS = ["year", "quarter", "month", "week", "day", "hour", "minute", "second"]
DT_FIELDS = ["era", "year", "quarter", "month", "week", "weekday", "day", "dayperiod", "hour", "minute", "second", "zone"]
WIDTHS = ["", "-short", "-narrow"]


def misc(S, name, names, r):
    lp = main_data(S, "cldr-misc-full", name, "listPatterns.json", names)["listPatterns"]
    for i, t in enumerate(LIST_TYPES):
        d = lp["listPattern-type-" + t]
        r[key(K_LIST, i)] = FS.join(d[k] for k in ("start", "middle", "end", "2"))
    fields = main_data(S, "cldr-dates-full", name, "dateFields.json", names)["dates"]["fields"]
    for ui, u in enumerate(RT_UNITS):
        for wi, w in enumerate(WIDTHS):
            d = fields.get(u + w, {})
            f = [d.get("relative-type-%d" % k, "") for k in (-2, -1, 0, 1, 2)]
            for t in ("future", "past"):
                rt = d.get("relativeTime-type-" + t, {})
                f += [rt.get("relativeTimePattern-count-" + c, "") for c in COUNTS]
            r[key(K_RELTIME, ui * 3 + wi)] = FS.join(f)
    for wi, w in enumerate(WIDTHS):
        r[key(K_DTFIELD, wi)] = FS.join(fields.get(f + w, {}).get("displayName", "") for f in DT_FIELDS)
    ld = main_data(S, "cldr-localenames-full", name, "localeDisplayNames.json", names)
    ld = ld["localeDisplayNames"] if ld else {}
    pat = ld.get("localeDisplayPattern", {})
    r[key(K_LOCPAT)] = FS.join((pat.get("localePattern", "{0} ({1})"), pat.get("localeSeparator", "{0}, {1}"),
                                pat.get("localeKeyTypePattern", "{0}: {1}")))
    for k, file, part in ((K_LANGN, "languages.json", "languages"), (K_REGIONN, "territories.json", "territories"),
                          (K_SCRIPTN, "scripts.json", "scripts"), (K_VARIANTN, "variants.json", "variants")):
        d = main_data(S, "cldr-localenames-full", name, file, names)
        r[key(k)] = dict(d["localeDisplayNames"][part]) if d else {}
    r[key(K_CALN)] = dict(ld.get("types", {}).get("calendar", {}))
    cur = main_data(S, "cldr-numbers-full", name, "currencies.json", names)
    r[key(K_CURN)] = {c: v["displayName"] for c, v in cur["numbers"]["currencies"].items() if "displayName" in v} if cur else {}


def names_record(d):
    """a dictionary of names as a record: code, GS, name, joined by FS, sorted by code"""
    return FS.join(c + GS + v for c, v in sorted(d.items(), key=lambda kv: kv[0].encode("utf-8")))


# the date records: months, days, day periods (by context, format then stand-alone, and
# width), eras, the date, time and glue patterns, the available formats (pairs of
# skeleton and pattern, sorted by the skeleton's bytes as ICU's tables are), the append
# items and field names (by ICU's UDATPG field order), the interval formats (the fallback,
# then each skeleton and its patterns by greatest difference), the time zone formats
MONTH_WIDTHS = ["abbreviated", "narrow", "wide"]
DAY_WIDTHS = ["abbreviated", "narrow", "short", "wide"]
DAY_KEYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
PERIOD_KEYS = ["am", "pm", "midnight", "noon", "morning1", "morning2", "afternoon1", "afternoon2", "evening1",
               "evening2", "night1", "night2"]
STYLES = ["full", "long", "medium", "short"]
# ICU's UDATPG fields: their append items' and field names' keys
APPEND_KEYS = ["Era", "Year", "Quarter", "Month", "Week", "", "Day-Of-Week", "", "", "Day", "", "Hour", "Minute",
               "Second", "", "Timezone"]
FIELD_KEYS = ["era", "year", "quarter", "month", "week", "weekOfMonth", "weekday", "dayOfYear", "weekdayOfMonth", "day",
              "dayperiod", "hour", "minute", "second", "", "zone"]
RS, GS = "\x1e", "\x1d"


def dates(S, name, names, r):
    cal = main_data(S, "cldr-dates-full", name, "ca-gregorian.json", names)["dates"]["calendars"]["gregorian"]
    for ci, ctx in enumerate(("format", "stand-alone")):
        for wi, w in enumerate(MONTH_WIDTHS):
            m = cal["months"][ctx][w]
            r[key(K_MONTHS, ci * 3 + wi)] = FS.join(m[str(i)] for i in range(1, 13))
        for wi, w in enumerate(DAY_WIDTHS):
            d = cal["days"][ctx][w]
            r[key(K_DAYS, ci * 4 + wi)] = FS.join(d[k] for k in DAY_KEYS)
        for wi, w in enumerate(MONTH_WIDTHS):
            d = cal["dayPeriods"][ctx][w]
            r[key(K_DAYPERIODS, ci * 3 + wi)] = FS.join(d.get(k, "") for k in PERIOD_KEYS)
    for wi, w in enumerate(("eraAbbr", "eraNarrow", "eraNames")):
        e = cal["eras"][w]
        r[key(K_ERAS, wi)] = FS.join((e.get("0", ""), e.get("1", "")))
    r[key(K_DATEFMT)] = FS.join(cal["dateFormats"][s] for s in STYLES)
    r[key(K_TIMEFMT)] = FS.join(cal["timeFormats"][s] for s in STYLES)
    dtf = cal["dateTimeFormats"]
    r[key(K_DTGLUE, 0)] = FS.join(dtf[s] for s in STYLES)
    at = cal.get("dateTimeFormats-atTime", {}).get("standard")
    if at:
        r[key(K_DTGLUE, 1)] = FS.join(at[s] for s in STYLES)
    r[key(K_AVAIL)] = FS.join(x for kv in available_formats(S, name, names) for x in kv)
    ai = dtf["appendItems"]
    r[key(K_APPEND)] = FS.join(ai.get(k, "") if k else "" for k in APPEND_KEYS)
    fields = main_data(S, "cldr-dates-full", name, "dateFields.json", names)["dates"]["fields"]
    r[key(K_FIELDNAMES)] = FS.join(fields.get(k, {}).get("displayName", "") if k else "" for k in FIELD_KEYS)
    iv = dtf["intervalFormats"]
    items = [iv["intervalFormatFallback"]]
    for sk in sorted((k for k in iv if k != "intervalFormatFallback" and "-alt-" not in k), key=lambda k: k.encode("utf-8")):
        pats = sorted((g, p) for g, p in iv[sk].items() if "-alt-" not in g)
        items += [sk, RS.join(g + GS + p for g, p in pats)]
    r[key(K_INTERVAL)] = FS.join(items)
    tzn = main_data(S, "cldr-dates-full", name, "timeZoneNames.json", names)["dates"]["timeZoneNames"]
    utc = tzn.get("zone", {}).get("Etc", {}).get("UTC", {})
    gmt = tzn.get("metazone", {}).get("GMT", {})
    r[key(K_TZ)] = FS.join((tzn["gmtFormat"], tzn["gmtZeroFormat"], tzn["hourFormat"],
                            utc.get("long", {}).get("standard", ""), utc.get("short", {}).get("standard", ""),
                            gmt.get("long", {}).get("standard", ""), gmt.get("short", {}).get("standard", "")))


def xml_formats(raw):
    """the Gregorian available formats a locale's XML gives itself (no alternates, no
    counted ones, no inheritance markers)"""
    out = {}
    if raw is None:
        return out
    for cal in ET.fromstring(raw).iter("calendar"):
        if cal.get("type") != "gregorian":
            continue
        for af in cal.iter("availableFormats"):
            for it in af.findall("dateFormatItem"):
                v = it.text or ""
                if it.get("alt") or it.get("count") or v == "\u2191\u2191\u2191":
                    continue
                out[it.get("id")] = v
    return out


def available_formats(S, name, names):
    """the available formats in the order ICU's pattern generator adds them: each locale
    of the chain, then root, its keys (sorted by bytes) not given below it"""
    seen, seq = set(), []
    n = name
    while n is not None:
        items = xml_formats(S.main_xml(n))
        for k in sorted(items, key=lambda k: k.encode("utf-8")):
            if k not in seen:
                seen.add(k)
                seq.append((k, items[k]))
        n = parent_of(S, n, names)
    items = xml_formats(S.root_xml)
    for k in sorted(items, key=lambda k: k.encode("utf-8")):
        if k not in seen:
            seq.append((k, items[k]))
    return seq


def numbering_systems(S):
    ns = S.json("cldr-core", "supplemental/numberingSystems.json")["supplemental"]["numberingSystems"]
    return sorted((k, v["_digits"]) for k, v in ns.items() if v.get("_type") == "numeric")


NUMBERING = []


def number_tables(S, B):
    """the numbering systems (each its ten digits; a record's index is its place here), the
    currencies' digits (ISO 4217's minor units, CLDR's fractions, where not 2), the
    currencies and units carried (their records' indexes)"""
    write_pairs(B, "NUMBERING", NUMBERING)
    fr = S.json("cldr-core", "supplemental/currencyData.json")["supplemental"]["currencyData"]["fractions"]
    write_pairs(B, "CUR_DIGITS", [(c, v["_digits"]) for c, v in fr.items() if c != "DEFAULT" and v["_digits"] != "2"])
    write_pairs(B, "CURRENCIES", [(c, c) for c in CURRENCIES])
    write_pairs(B, "UNITS", [(u, u) for u in UNIT_IDS])
    for name, k in KEYS.items():
        B.mark(name, k)


# ---- plural rules ----

OPERANDS = "nivwftce"


def compile_rule(text):
    """a CLDR plural rule's condition ("i = 1 and v = 0 @integer ...") as bytes: its or'd
    groups, each its and'd relations: operand, modulus, negated, its ranges"""
    cond = text.split("@")[0].strip()
    out = bytearray()
    if not cond:
        out.append(0)
        return bytes(out)
    ors = [g.strip() for g in cond.split(" or ")]
    out.append(len(ors))
    for g in ors:
        rels = [r.strip() for r in g.split(" and ")]
        out.append(len(rels))
        for rel in rels:
            neg = "!=" in rel
            lhs, rhs = rel.split("!=" if neg else "=")
            lhs = lhs.strip()
            mod = 0
            if "%" in lhs:
                lhs, m = lhs.split("%")
                mod = int(m)
                lhs = lhs.strip()
            assert lhs in OPERANDS, rel
            ranges = []
            for part in rhs.split(","):
                part = part.strip()
                if ".." in part:
                    a, b = part.split("..")
                    ranges.append((int(a), int(b)))
                else:
                    ranges.append((int(part), int(part)))
            out.append(OPERANDS.index(lhs))
            out += mod.to_bytes(4, "little")
            out.append(1 if neg else 0)
            out.append(len(ranges))
            for a, b in ranges:
                out += a.to_bytes(4, "little") + b.to_bytes(4, "little")
    return bytes(out)


def date_tables(S, B):
    """the day periods' rules of each language (CLDR's dayPeriodRuleSet: a period at a time,
    "noon@1200", or from one time before another, "morning1:0600-1200"), and the hour
    cycles' data by region or language_region (timeData: the preferred format, then the
    allowed ones), as ICU's pattern generator reads them"""
    dp = S.json("cldr-core", "supplemental/dayPeriods.json")["supplemental"]["dayPeriodRuleSet"]
    rows = []
    for lang, rules in dp.items():
        if lang.split("-")[0] not in LANGS:
            continue
        parts = []
        for name, r in sorted(rules.items()):
            if "_at" in r:
                parts.append("%s@%s" % (name, r["_at"].replace(":", "")))
            else:
                parts.append("%s:%s-%s" % (name, r["_from"].replace(":", ""), r["_before"].replace(":", "")))
        rows.append((lang, " ".join(parts)))
    write_pairs(B, "DAYPERIOD_RULES", rows)
    td = S.json("cldr-core", "supplemental/timeData.json")["supplemental"]["timeData"]
    write_pairs(B, "HOURS", [(k, (v.get("_preferred", "") + " " + v.get("_allowed", "")).strip()) for k, v in td.items()])


# ---- the break properties (UAX #29) ----

GCB = ["Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend", "SpacingMark", "L", "V", "T",
       "LV", "LVT"]
WB = ["Other", "CR", "LF", "Newline", "Extend", "ZWJ", "Regional_Indicator", "Format", "Katakana", "Hebrew_Letter",
      "ALetter", "Single_Quote", "Double_Quote", "MidNumLet", "MidLetter", "MidNum", "Numeric", "ExtendNumLet",
      "WSegSpace"]
SB = ["Other", "CR", "LF", "Extend", "Sep", "Format", "Sp", "Lower", "Upper", "OLetter", "Numeric", "ATerm",
      "SContinue", "STerm", "Close"]
INCB = ["None", "Linker", "Consonant", "Extend"]


def code_range(f):
    if ".." in f:
        a, b = f.split("..")
        return int(a, 16), int(b, 16)
    return int(f, 16), int(f, 16)


def write_ranges(B, name, values):
    """a property's ranges: each (first u32, last u32, value u8), sorted, the default (0)
    left out, adjacent ranges of one value joined"""
    runs = []
    for cp in sorted(values):
        v = values[cp]
        if runs and runs[-1][1] == cp - 1 and runs[-1][2] == v:
            runs[-1][1] = cp
        else:
            runs.append([cp, cp, v])
    B.mark("IDB_%s_N" % name, len(runs))
    B.mark("IDB_%s" % name)
    for a, b, v in runs:
        B.u32(a)
        B.u32(b)
        B.u8(v)


def break_tables(S, B):
    """each code point's grapheme class (bits 0-3; Extended_Pictographic bit 4; its
    Indic_Conjunct_Break, bits 5-6), word class (bits 0-4; Extended_Pictographic bit
    5), sentence class"""
    ext = set()
    for f in S.ucd("emoji-data.txt"):
        if f[1] == "Extended_Pictographic":
            a, b = code_range(f[0])
            ext.update(range(a, b + 1))
    g, w, sb = {}, {}, {}
    for f in S.ucd("GraphemeBreakProperty.txt"):
        a, b = code_range(f[0])
        for cp in range(a, b + 1):
            g[cp] = GCB.index(f[1])
    for f in S.ucd("DerivedCoreProperties.txt"):
        if f[1] == "InCB":
            a, b = code_range(f[0])
            for cp in range(a, b + 1):
                g[cp] = g.get(cp, 0) | INCB.index(f[2]) << 5
    for cp in ext:
        g[cp] = g.get(cp, 0) | 16
    for f in S.ucd("WordBreakProperty.txt"):
        a, b = code_range(f[0])
        for cp in range(a, b + 1):
            w[cp] = WB.index(f[1])
    for cp in ext:
        w[cp] = w.get(cp, 0) | 32
    for f in S.ucd("SentenceBreakProperty.txt"):
        a, b = code_range(f[0])
        for cp in range(a, b + 1):
            sb[cp] = SB.index(f[1])
    write_ranges(B, "GCB", g)
    write_ranges(B, "WB", w)
    write_ranges(B, "SB", sb)


# ---- the root collation ----

def collation_table(S, B):
    """CLDR's root collation: its elements (primary u16, secondary u16, tertiary u8, 1 for
    a variable one), each code point's (its code point and their count in a word, their
    first's index in the next), the contractions (three code points, 0 for none, and their
    elements' place and count), each table sorted"""
    singles, contr = {}, {}
    for line in S.allkeys():
        line = line.split("#")[0].strip()
        if not line or line.startswith("@"):
            continue
        cps, ces = line.split(";")
        cps = tuple(int(x, 16) for x in cps.split())
        out = [(m.group(1) == "*", int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
               for m in re.finditer(r"\[([.*])([0-9A-F]+)\.([0-9A-F]+)\.([0-9A-F]+)\]", ces)]
        if len(cps) == 1:
            singles[cps[0]] = out
        else:
            contr[cps] = out
    pool, where = [], {}

    def place(ces):
        k = tuple(ces)
        if k not in where:
            where[k] = len(pool)
            pool.extend(ces)
        return where[k]
    rows = sorted((cp, place(ces), len(ces)) for cp, ces in singles.items())
    crows = sorted((cps + (0,) * (3 - len(cps)), place(ces), len(ces)) for cps, ces in contr.items())
    B.mark("IDB_COLL_CES")
    for var, a, b, c in pool:
        B.u16(a)
        B.u16(b)
        B.u8(c)
        B.u8(1 if var else 0)
    B.mark("IDB_COLL_N", len(rows))
    B.mark("IDB_COLL")
    for cp, off, n in rows:
        B.u32(cp | n << 24)
        B.u32(off)
    B.mark("IDB_COLLX_N", len(crows))
    B.mark("IDB_COLLX")
    for cps, off, n in crows:
        for c in cps:
            B.u32(c)
        B.u32(off | n << 24)


def plural_tables(S, B):
    """each plural locale's (every language's, as ICU has them all) cardinal and ordinal
    rule sets: a set is its categories' count,
    then each category (its index in zero one two few many other) and its condition"""
    sets = {}
    rows = []
    for kind, name in (("cardinal", "plurals.json"), ("ordinal", "ordinals.json")):
        d = S.json("cldr-core", "supplemental/" + name)["supplemental"]["plurals-type-" + kind]
        for loc in sorted(d):
            rules = d[loc]
            cats = [c for c in COUNTS if "pluralRule-count-" + c in rules]
            blob = bytearray([len(cats)])
            for c in cats:
                blob.append(COUNTS.index(c))
                blob += compile_rule(rules["pluralRule-count-" + c])
            blob = bytes(blob)
            if blob not in sets:
                sets[blob] = None
            rows.append((kind[0] + ":" + loc, blob))
    offs = {}
    B.mark("IDB_PLURAL_SETS")
    for blob in sets:
        offs[blob] = B.here()
        B.b += blob
    # the table: "c:LOCALE" and "o:LOCALE" to their sets' offsets, written as numbers
    write_pairs(B, "PLURALS", [(k, str(offs[v])) for k, v in rows])
    # the plural form of a range from its ends' (CLDR's pluralRanges): 36 characters, the
    # start's form by six then the end's, each the range's form's index or "-" (other)
    pr = S.json("cldr-core", "supplemental/pluralRanges.json")["supplemental"]["plurals"]
    rows = []
    for loc, d in pr.items():
        s = ""
        for a in COUNTS:
            for b in COUNTS:
                v = d.get("pluralRange-start-%s-end-%s" % (a, b))
                s += str(COUNTS.index(v)) if v else "-"
        rows.append((loc, s))
    write_pairs(B, "PLURAL_RANGES", rows)


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
    NUMBERING.extend(numbering_systems(S))
    locales(S, B)
    aliases(S, B)
    likely(S, B)
    regions(S, B)
    number_tables(S, B)
    plural_tables(S, B)
    date_tables(S, B)
    break_tables(S, B)
    collation_table(S, B)
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    emit(B, os.path.join(root, "src/js/intl_data.c"), os.path.join(root, "src/js/intl_data.h"))


if __name__ == "__main__":
    main()
