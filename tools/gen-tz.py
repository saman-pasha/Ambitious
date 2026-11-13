#!/usr/bin/env python3
"""tools/gen-tz.py [SRC] -- writes src/js/tz_data.c and src/js/tz_data.h, the time zones
src/js/intl.c formats in: every zone and link of the IANA time zone database (tzdata
2026e, the release node 22's ICU 78.3 carries), compiled as ICU compiles it:

    python3 tools/gen-tz.py          (fetches tzdata2026e.tar.gz from data.iana.org)
    python3 tools/gen-tz.py SRC      (SRC holds tzdata2026e.tar.gz)
    python3 tools/gen-tz.py --check ZONEINFO64 [SRC]
                                     (writes nothing: compares the compile with ICU's,
                                      icu-data's tzdata/icunew/2026e/44/zoneinfo64.txt)

ICU builds its zoneinfo64 from the database's rearguard form (no negative DST: Ireland's
winter is standard time, Morocco's Ramadan the standard one), without backzone (Oslo is
Berlin before 1970), with its own icuzones beside (Etc/Unknown, the SystemV zones, Java's
three-letter names, US/Pacific-New, Canada/East-Saskatchewan). So does this: each file
put in rearguard form by the database's own ziguard.awk (run by awk), then compiled by
zic's rules (zic.c's outzone and writezone, ported: the transitions, their merging), each
type's standard offset and DST amount kept (ICU's zic keeps them, TZif does not), then
tz2icu's metazone patches. Every zone's offsets and transitions, every final rule and the
year it starts in, and every link, are ICU's own for 2026e (icu-data's zoneinfo64.txt,
compared zone by zone).

What it writes, for src/js/intl.c:
  - each zone's types (its standard offset and its DST amount, in seconds; the first the
    one before its first transition), its transitions (UTC seconds and a type's index;
    those that change neither, which ICU's getNextTransition skips, left out), and the
    rule of its last line where two rules run to "max" (ICU's final rule, as
    SimpleTimeZone keeps it: each a month, a day of the month or of the week on or
    before or after one, a time of day, wall, standard or UTC, and the DST amount) with
    the year it starts in (ICU's finalYear: the year after the first in which both
    applied, after the last of the line's other rules), the transitions before it;
  - every name (the 639 ICU knows, the tzdata zones' and links' and icuzones'), sorted
    by their ASCII-uppercased bytes (V8 matches a name case-insensitively), each its
    zone's index (ICU's Olson link: the zone a link is to).

The data is 32-bit words, its strings packed four bytes to a word after them, as
tools/gen-intl.py writes src/js/intl_data.c; a number is little-endian."""
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

TZDATA = "2026e"
URL = "https://data.iana.org/time-zones/releases/tzdata%s.tar.gz"
# the files ICU's tzcode Makefile compiles (PRIMARY_DATA and SUPPLEMENTAL_DATA), each put
# in rearguard form by itself
FILES = ["africa", "antarctica", "asia", "australasia", "europe", "northamerica", "southamerica",
         "etcetera", "factory", "backward"]

# ICU's icuzones (icu4c/source/tools/tzcode/icuzones), which its build adds to factory
ICUZONES = """
Zone	Etc/Unknown	0	-	Unknown
Rule	SystemV	min	1973	-	Apr	lastSun	2:00	1:00	D
Rule	SystemV	min	1973	-	Oct	lastSun	2:00	0	S
Rule	SystemV	1974	only	-	Jan	6	2:00	1:00	D
Rule	SystemV	1974	only	-	Nov	lastSun	2:00	0	S
Rule	SystemV	1975	only	-	Feb	23	2:00	1:00	D
Rule	SystemV	1975	only	-	Oct	lastSun	2:00	0	S
Rule	SystemV	1976	max	-	Apr	lastSun	2:00	1:00	D
Rule	SystemV	1976	max	-	Oct	lastSun	2:00	0	S
Zone	SystemV/AST4ADT	-4:00	-		AST	1901 Dec 13 20:45:52u
			-4:00	SystemV		A%sT
Zone	SystemV/EST5EDT	-5:00	-		EST	1901 Dec 13 20:45:52u
			-5:00	SystemV		E%sT
Zone	SystemV/CST6CDT	-6:00	-		CST	1901 Dec 13 20:45:52u
			-6:00	SystemV		C%sT
Zone	SystemV/MST7MDT	-7:00	-		MST	1901 Dec 13 20:45:52u
			-7:00	SystemV		M%sT
Zone	SystemV/PST8PDT	-8:00	-		PST	1901 Dec 13 20:45:52u
			-8:00	SystemV		P%sT
Zone	SystemV/YST9YDT	-9:00	-		YST	1901 Dec 13 20:45:52u
			-9:00	SystemV		Y%sT
Zone	SystemV/AST4	-4:00	-		AST
Zone	SystemV/EST5	-5:00	-		EST
Zone	SystemV/CST6	-6:00	-		CST
Zone	SystemV/MST7	-7:00	-		MST
Zone	SystemV/PST8	-8:00	-		PST
Zone	SystemV/YST9	-9:00	-		YST
Zone	SystemV/HST10	-10:00	-		HST
Link America/Los_Angeles US/Pacific-New
Link Australia/Darwin ACT
Link Australia/Sydney AET
Link America/Argentina/Buenos_Aires AGT
Link Africa/Cairo ART
Link America/Anchorage AST
Link America/Sao_Paulo BET
Link Asia/Dhaka BST
Link Africa/Maputo CAT
Link America/St_Johns CNT
Link America/Chicago CST
Link Asia/Shanghai CTT
Link Africa/Addis_Ababa EAT
Link Europe/Paris ECT
Link America/Indiana/Indianapolis IET
Link Asia/Kolkata IST
Link Asia/Tokyo JST
Link Pacific/Apia MIT
Link Asia/Yerevan NET
Link Pacific/Auckland NST
Link Asia/Karachi PLT
Link America/Phoenix PNT
Link America/Puerto_Rico PRT
Link America/Los_Angeles PST
Link Pacific/Guadalcanal SST
Link Asia/Ho_Chi_Minh VST
Link America/Regina Canada/East-Saskatchewan
"""


# ---- the source: tzdata's files in rearguard form ----

def rearguard_text(src):
    """the files of FILES in rearguard form (ziguard.awk on each), then icuzones"""
    if src:
        with open(os.path.join(src, "tzdata%s.tar.gz" % TZDATA), "rb") as f:
            raw = f.read()
    else:
        with urllib.request.urlopen(URL % TZDATA) as r:
            raw = r.read()
    tar = tarfile.open(fileobj=io.BytesIO(raw), mode="r:gz")
    version = tar.extractfile("version").read().decode().strip()
    assert version == TZDATA, version
    out = []
    with tempfile.TemporaryDirectory() as d:
        for name in FILES + ["ziguard.awk"]:
            with open(os.path.join(d, name), "wb") as f:
                f.write(tar.extractfile(name).read())
        for name in FILES:
            r = subprocess.run(["awk", "-v", "DATAFORM=rearguard", "-f", os.path.join(d, "ziguard.awk"),
                                os.path.join(d, name)], check=True, capture_output=True)
            out.append(r.stdout.decode("utf-8"))
    out.append(ICUZONES)
    return "\n".join(out)


# ---- zic's reading of the source ----

MONTHS = ["january", "february", "march", "april", "may", "june", "july", "august", "september",
          "october", "november", "december"]
WDAYS = ["sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"]
YEAR_32BIT_MIN = 1901
ZIC_MAX = 1 << 62


def byword(word, table):
    """zic's byword: a table's entry the word names, whole or as an unambiguous prefix"""
    w = word.lower()
    if w in table:
        return table.index(w)
    hits = [i for i, t in enumerate(table) if t.startswith(w)]
    assert len(hits) == 1, word
    return hits[0]


def hms(s):
    """zic's gethms: [-]h[:mm[:ss[.frac]]] in seconds, a fraction rounded to even"""
    if s == "-":
        return 0
    neg = s.startswith("-")
    if neg:
        s = s[1:]
    p = s.split(":")
    h = int(p[0])
    m = int(p[1]) if len(p) > 1 else 0
    sec = 0
    if len(p) > 2:
        whole, _, frac = p[2].partition(".")
        sec = int(whole)
        if frac:
            f = float("0." + frac)
            if f > 0.5 or (f == 0.5 and sec % 2):
                sec += 1
    v = h * 3600 + m * 60 + sec
    return -v if neg else v


def time_suffix(s):
    """a time and its suffix: (seconds, isstd, isut)"""
    isstd = isut = False
    if s and s[-1].lower() in "swguz":
        c = s[-1].lower()
        s = s[:-1]
        if c == "s":
            isstd = True
        elif c in "guz":
            isstd = isut = True
    return hms(s), isstd, isut


class Rule:
    def __init__(self, f):
        # Rule NAME FROM TO - IN ON AT SAVE LETTER/S
        self.name = f[1]
        lo, hi = f[2].lower(), f[3].lower()
        if "minimum".startswith(lo) and lo.startswith("mi"):
            self.lo = YEAR_32BIT_MIN - 1
        else:
            self.lo = int(lo)
        if "maximum".startswith(hi) and hi.startswith("ma"):
            self.hi = ZIC_MAX
        elif "only".startswith(hi) and hi.startswith("o"):
            self.hi = self.lo
        else:
            self.hi = int(hi)
        self.month = byword(f[5], MONTHS)
        self.dycode, self.dom, self.wday = day_spec(f[6], self.month)
        self.tod, self.todisstd, self.todisut = time_suffix(f[7])
        save = f[8]
        isdst = None
        if save and save[-1] in "sd":
            isdst = save[-1] == "d"
            save = save[:-1]
        self.save = hms(save)
        self.isdst = self.save != 0 if isdst is None else isdst
        self.letters = "" if f[9] == "-" else f[9]


DOM, DOWGEQ, DOWLEQ = 0, 1, 2
LEN_MONTHS_LEAP = [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]


def day_spec(s, month):
    """an ON field: (code, day of month, weekday)"""
    low = s.lower()
    if low.startswith("last"):
        return DOWLEQ, LEN_MONTHS_LEAP[month], byword(low[4:].lstrip("-"), WDAYS)
    for op, code in (("<=", DOWLEQ), (">=", DOWGEQ)):
        if op in low:
            w, d = low.split(op)
            return code, int(d), byword(w, WDAYS)
    return DOM, int(low), 0


def isleap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def days_from_civil(y, m, d):
    """the days from 1970-01-01 to y-m-d (m 1 to 12)"""
    y -= m <= 2
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def rpytime(month, dycode, dom, wday, tod, year):
    """zic's rpytime: a rule's time in a year, in local seconds from the epoch"""
    i = dom
    if month == 1 and i == 29 and not isleap(year):
        assert dycode == DOWLEQ
        i -= 1
    day = days_from_civil(year, month + 1, 1) + i - 1
    if dycode != DOM:
        w = (day + 4) % 7
        while w != wday:
            if dycode == DOWGEQ:
                day += 1
                w = (w + 1) % 7
            else:
                day -= 1
                w = (w - 1) % 7
    return day * 86400 + tod


class ZoneLine:
    def __init__(self, f):
        # STDOFF RULES FORMAT [UNTIL]
        self.stdoff = hms(f[0])
        self.rules = f[1]
        self.format = f[2]
        self.save = 0
        self.isdst = False
        if self.rules != "-" and (self.rules[0].isdigit() or self.rules[0] == "-"):
            s = self.rules
            isdst = None
            if s[-1] in "sd":
                isdst = s[-1] == "d"
                s = s[:-1]
            self.save = hms(s)
            self.isdst = self.save != 0 if isdst is None else isdst
            self.rules = "-"
        self.until = None
        if len(f) > 3:
            y = int(f[3])
            m = byword(f[4], MONTHS) if len(f) > 4 else 0
            dycode, dom, wday = day_spec(f[5], m) if len(f) > 5 else (DOM, 1, 0)
            tod, isstd, isut = time_suffix(f[6]) if len(f) > 6 else (0, False, False)
            self.until = rpytime(m, dycode, dom, wday, tod, y)
            self.until_year = y
            self.until_isstd, self.until_isut = isstd, isut


def read_source(text):
    rules, zones, links = {}, {}, []
    current = None
    for line in text.split("\n"):
        f = line.split("#")[0].split()
        if not f:
            continue
        if f[0] == "Rule":
            rules.setdefault(f[1], []).append(Rule(f))
            current = None
        elif f[0] == "Zone":
            current = [ZoneLine(f[2:])]
            assert f[1] not in zones, f[1]
            zones[f[1]] = current
            if current[-1].until is None:
                current = None
        elif f[0] == "Link":
            links.append((f[1], f[2]))
            current = None
        else:
            assert current is not None, line
            current.append(ZoneLine(f))
            if current[-1].until is None:
                current = None
    return rules, zones, links


# ---- zic's compiling of a zone: outzone and writezone ----

def doabbr(zl, letters, isdst):
    """zic's doabbr: the abbreviation of a zone line's format"""
    fmt = zl.format
    if "/" in fmt:
        a, b = fmt.split("/", 1)
        return b if isdst else a
    if "%s" in fmt:
        return fmt.replace("%s", letters if letters is not None else "%s")
    return fmt


def outzone(lines, rules):
    """the zone's transitions as zic computes them, merged as its writezone merges them: a
    list of [time, type], each type (utoff, isdst, abbr, stdoff, save); its default type
    (the one before the first); its final rule (its two rules, the standard offset, the
    year it starts in), or None"""
    min_year = max_year = 1970
    for i, zl in enumerate(lines):
        rs = rules.get(zl.rules, []) if zl.rules != "-" else []
        zl.rs = rs
        if i < len(lines) - 1:
            min_year, max_year = min(min_year, zl.until_year), max(max_year, zl.until_year)
        for r in rs:
            min_year, max_year = min(min_year, r.lo), max(max_year, r.lo)
            if r.hi != ZIC_MAX:
                min_year, max_year = min(min_year, r.hi), max(max_year, r.hi)
    # zic's -b fat range, from 1900 to 2037 at least: every transition is kept (but those
    # a final rule makes)
    max_year = max(max_year, 2037)
    min_year = min(min_year, YEAR_32BIT_MIN - 1)
    attypes = []
    default = None
    starttime = None
    startttisstd = startttisut = False
    final = None
    added = []                         # the types in the order zic's addtype meets them
    for i, zl in enumerate(lines):
        save = 0
        usestart = i > 0
        useuntil = i < len(lines) - 1
        stdoff = zl.stdoff
        startoff = stdoff
        startbuf = None
        if not zl.rs:
            save = zl.save
            t = (stdoff + save, zl.isdst, doabbr(zl, None, zl.isdst), stdoff, save)
            added.append(t)
            if usestart:
                attypes.append([starttime, t])
                usestart = False
            elif default is None:
                default = t
        else:
            maxrules = [r for r in zl.rs if r.hi == ZIC_MAX]
            is_final = not useuntil and len(maxrules) == 2
            last_numbered = max([r.hi for r in zl.rs if r.hi != ZIC_MAX] + [min_year])
            year = min_year
            try_next = True
            while try_next:
                if useuntil and year > zl.until_year:
                    break
                try_next = False
                # (a final line runs on until its final rules take over)
                next_year = max_year if not is_final else max(max_year, last_numbered + 2)
                todo = {}
                for j, r in enumerate(zl.rs):
                    if year < r.hi:
                        nxt = max(year + 1, r.lo)
                        if nxt <= next_year:
                            next_year = nxt
                            try_next = True
                    if r.lo <= year <= r.hi:
                        todo[j] = rpytime(r.month, r.dycode, r.dom, r.wday, r.tod, year)
                emitted_final = set()
                while True:
                    if useuntil:
                        untiltime = zl.until
                        if not zl.until_isut:
                            untiltime -= stdoff
                        if not zl.until_isstd:
                            untiltime -= save
                    k, ktime = -1, None
                    for j in sorted(todo):
                        r = zl.rs[j]
                        offset = 0 if r.todisut else stdoff
                        if not r.todisstd:
                            offset += save
                        jtime = todo[j] - offset
                        if k < 0 or jtime < ktime:
                            k, ktime = j, jtime
                        else:
                            assert jtime != ktime, "two rules for the same instant"
                    if k < 0:
                        break
                    r = zl.rs[k]
                    del todo[k]
                    if useuntil and ktime >= untiltime:
                        if startbuf is None and stdoff + r.save == startoff:
                            startbuf = doabbr(zl, r.letters, r.isdst)
                        break
                    save = r.save
                    if usestart and ktime == starttime:
                        usestart = False
                    if usestart:
                        if ktime < starttime:
                            startoff = stdoff + save
                            startbuf = doabbr(zl, r.letters, r.isdst)
                            continue
                        if startbuf is None and startoff == stdoff + save:
                            startbuf = doabbr(zl, r.letters, r.isdst)
                    t = (stdoff + r.save, r.isdst, doabbr(zl, r.letters, r.isdst), stdoff, r.save)
                    added.append(t)
                    if default is None and not r.isdst:
                        default = t
                    attypes.append([ktime, t])
                    if is_final and r.hi == ZIC_MAX:
                        emitted_final.add(id(r))
                if is_final and len(emitted_final) == 2 and year > last_numbered:
                    # ICU's zic: the transitions stop once both final rules have
                    # applied in a year after the last of the others; the final
                    # rule takes the next year on
                    final = (maxrules, stdoff, year + 1)
                    break
                year = next_year
        if usestart:
            isdst = startoff != stdoff
            if startbuf is None:
                startbuf = doabbr(zl, None, isdst)
            t = (startoff, isdst, startbuf, stdoff, startoff - stdoff)
            added.append(t)
            if default is None and not isdst:
                default = t
            attypes.append([starttime, t])
        if useuntil:
            startttisstd, startttisut = zl.until_isstd, zl.until_isut
            starttime = zl.until
            if not startttisstd:
                starttime -= save
            if not startttisut:
                starttime -= stdoff
    if default is None:
        default = added[0]
    attypes.sort(key=lambda a: a[0])
    # writezone's merging: a transition whose local time is not later than the one
    # before it replaces it, and one to the same timekeeping is dropped (zic's type 0,
    # against which the first is measured, is the first type it added)
    first_type = added[0]
    out = []
    for at, t in attypes:
        if out:
            type_2 = first_type if len(out) == 1 else out[-2][1]
            if at + out[-1][1][0] <= out[-1][0] + type_2[0]:
                if t == type_2:
                    out.pop()
                else:
                    out[-1][1] = t
                continue
        if not out or out[-1][1][:3] != t[:3]:
            out.append([at, t])
    return out, default, final


# tz2icu's metazone patches (applyCldrMetazonePatches): from 1970, a transition in a
# period to a type whose total offset is the zone's metazone's standard offset is to
# standard time, one to its daylight offset to daylight time on the standard offset
# (Vancouver's permanent daylight time from November 2026, Ireland's summer). CLDR 48's
# metaZones gives Windhoek's, Vancouver's and Dublin's periods; ICU's 2026e update adds
# Edmonton's, Inuvik's and Winnipeg's.
METAZONE_OFFSETS = [
    ("Africa/Windhoek", (1994, 3, 20, 22, 0), (2017, 10, 23, 22, 0), 3600, 7200),
    ("America/Edmonton", None, None, -25200, -21600),
    ("America/Inuvik", (1979, 4, 29, 10, 0), None, -25200, -21600),
    ("America/Vancouver", None, None, -28800, -25200),
    ("America/Winnipeg", None, None, -21600, -18000),
    ("Europe/Dublin", (1971, 10, 31, 2, 0), None, 0, 3600),
]


def utc_seconds(t):
    y, mo, d, h, mi = t
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60


def metazone_patch(name, trans):
    for zone, frm, to, std, dst in METAZONE_OFFSETS:
        if zone != name:
            continue
        lo = utc_seconds(frm) if frm else 0
        hi = utc_seconds(to) if to else utc_seconds((9999, 12, 31, 23, 59))
        for tr in trans:
            if tr[0] < 0 or not (lo <= tr[0] < hi):
                continue
            total = tr[1][0]
            if total == std:
                tr[1] = (total, False, tr[1][2], std, 0)
            elif total == dst:
                tr[1] = (total, dst != std, tr[1][2], std, dst - std)


def compile_zones(rules, zones):
    out = {}
    for name, lines in zones.items():
        trans, default, final = outzone(lines, rules)
        metazone_patch(name, trans)
        out[name] = (trans, default, final)
    return out


# ---- the final rule, as ICU's SimpleTimeZone keeps it ----

def icu_rule(r):
    """a rule's month, day and day of the week as SimpleTimeZone encodes them (a day of the
    month with no weekday; a weekday, 1 Sunday, negated, on or after the day; or on or
    before the negated day), its time and its mode (0 wall, 1 standard, 2 UTC)"""
    if r.dycode == DOM:
        day, dow = r.dom, 0
    elif r.dycode == DOWGEQ:
        day, dow = r.dom, -(r.wday + 1)
    else:
        day, dow = -r.dom, -(r.wday + 1)
    return r.month, day, dow, r.tod, 2 if r.todisut else 1 if r.todisstd else 0


def rule_instant(r, year, raw, dst):
    """when a final rule takes effect in a year, in UTC seconds (its wall time before the
    change: standard time for the start of daylight time, daylight time for its end)"""
    local = rpytime(r.month, r.dycode, r.dom, r.wday, r.tod, year)
    if r.todisut:
        return local
    if r.todisstd:
        return local - raw
    return local - raw - dst


def first_final(final):
    """the first transition of the final rule after its year begins: (time, to daylight)"""
    rs, raw, year = final
    start = [r for r in rs if r.save != 0][0]
    end = [r for r in rs if r.save == 0][0]
    begin = days_from_civil(year, 1, 1) * 86400
    for y in (year, year + 1):
        cands = [c for c in sorted([(rule_instant(start, y, raw, 0), 1), (rule_instant(end, y, raw, start.save), 0)])
                 if c[0] > begin]
        if cands:
            return cands[0]
    raise AssertionError("no final transition")


# ---- output ----

class Words:
    """the data: 32-bit words, and strings packed four bytes to a word after them"""

    def __init__(self):
        self.w = []
        self.strs = bytearray(b"\0")
        self.offs = {"": 0}

    def string(self, s):
        if s not in self.offs:
            self.offs[s] = len(self.strs)
            self.strs += s.encode("ascii") + b"\0"
        return self.offs[s]

    def word(self, v):
        assert -(1 << 31) <= v < 1 << 32, v
        self.w.append(v & 0xFFFFFFFF)


def build(rules, zones, links):
    compiled = compile_zones(rules, zones)
    target = dict((b, a) for a, b in links)

    def zone_of(n):
        k = 0
        while n in target and n not in zones:
            n = target[n]
            k += 1
            assert k < 10, n
        return n
    znames = sorted(zones)
    zindex = {n: i for i, n in enumerate(znames)}
    names = sorted(set(zones) | set(target), key=lambda s: s.upper().encode("ascii"))
    W = Words()
    marks = []
    # the zones' records, then their data
    marks.append(("TZD_ZONES_N", len(znames)))
    marks.append(("TZD_ZONES", 0))
    zone_rec = len(W.w)
    for _ in znames:
        W.w += [0, 0, 0, 0]
    ntrans = 0
    for i, n in enumerate(znames):
        trans, default, final = compiled[n]
        # the transitions that change the standard offset or the DST amount (ICU skips
        # the others: its getNextTransition's "non-transition data")
        cur = (default[3], default[4])
        eff = []
        for t, ty in trans:
            k = (ty[3], ty[4])
            if k != cur:
                eff.append((t, k))
                cur = k
        types = [(default[3], default[4])]
        for t, k in eff:
            if k not in types:
                types.append(k)
        assert len(types) < 256 and len(eff) < 65536
        ntrans += len(eff)
        pre = [t for t, k in eff if t < -(1 << 31)]
        post = [t for t, k in eff if t >= 1 << 31]
        base = len(W.w)
        for raw, dst in types:
            W.word(raw)
            W.word(dst)
        for t, k in eff:
            if t < -(1 << 31) or t >= 1 << 31:
                W.word(t & 0xFFFFFFFF)
                W.word((t >> 32) & 0xFFFFFFFF)
            else:
                W.word(t)
        idx = [types.index(k) for t, k in eff]
        for j in range(0, len(idx), 4):
            v = 0
            for b, x in enumerate(idx[j:j + 4]):
                v |= x << (8 * b)
            W.word(v)
        if final:
            rs, raw, year = final
            start = [r for r in rs if r.save != 0]
            end = [r for r in rs if r.save == 0]
            assert len(start) == 1 and len(end) == 1, n
            for r in (start[0], end[0]):
                m, day, dow, tod, mode = icu_rule(r)
                W.word(m | (day & 0xFF) << 8 | (dow & 0xFF) << 16 | mode << 24)
                W.word(tod)
            ft, fdst = first_final(final)
            W.word(start[0].save)
            W.word(raw)
            W.word(year)
            W.word(ft)
            W.word(fdst)
        rec = zone_rec + 4 * i
        W.w[rec] = W.string(n)
        W.w[rec + 1] = base
        W.w[rec + 2] = len(types) | len(eff) << 8 | (1 << 24 if final else 0)
        W.w[rec + 3] = len(pre) | len(post) << 16
    # the names
    marks.append(("TZD_NAMES_N", len(names)))
    marks.append(("TZD_NAMES", len(W.w)))
    for n in names:
        z = zone_of(n)
        W.word(W.string(n))
        W.word(zindex[z])
    marks.append(("TZD_STRINGS", len(W.w)))
    data = bytes(W.strs) + b"\0" * (-len(W.strs) % 4)
    for j in range(0, len(data), 4):
        W.w.append(int.from_bytes(data[j:j + 4], "little"))
    return W, marks, ntrans


def emit(W, marks, out_c, out_h):
    with open(out_c, "w") as f:
        f.write("/* src/js/tz_data.c -- generated by tools/gen-tz.py from tzdata %s. Do not edit; run the\n"
                "   script. The time zones src/js/intl.c formats in, as 32-bit words; src/js/tz_data.h\n"
                "   says where each section begins. */\n" % TZDATA)
        f.write("#include <stdint.h>\n\n")
        f.write("const uint32_t js_tz_db_words = %du;\n" % len(W.w))
        f.write("const uint32_t js_tz_db[%d] = {\n" % len(W.w))
        for i in range(0, len(W.w), 8):
            f.write("    " + ", ".join("0x%08xu" % w for w in W.w[i:i + 8]) + ",\n")
        f.write("};\n")
    with open(out_h, "w") as f:
        f.write("/* src/js/tz_data.h -- generated by tools/gen-tz.py from tzdata %s. Do not edit; run the\n"
                "   script. Where each section of src/js/tz_data.c's words begins (a word's index; a\n"
                "   string's offset is a byte's from TZD_STRINGS's first word), and their counts.\n\n"
                "   A zone: 4 words (its name; its data's first word; its types' count, its\n"
                "   transitions' count << 8, bit 24 when it has a final rule; the count of its\n"
                "   transitions before 1901-12-13 20:45:52, and << 16 of those after 2038-01-19\n"
                "   03:14:07). Its data: each type's standard offset and DST amount (seconds; the\n"
                "   first the one before the first transition); the transitions' UTC seconds (a word,\n"
                "   or two, low first, out of the 32-bit range); their types, four bytes to a word;\n"
                "   the final rule: the start's and the end's month, day and weekday as SimpleTimeZone\n"
                "   encodes them and mode (a byte each), and time; the DST amount, the standard\n"
                "   offset, the year it starts in, its first transition's time and whether it is\n"
                "   to daylight time.\n\n"
                "   A name: 2 words (its string; its zone's index), sorted by the names' uppercased\n"
                "   bytes. */\n" % TZDATA)
        f.write("#define TZD_VERSION \"%s\"\n" % TZDATA)
        for name, v in marks:
            f.write("#define %s %du\n" % (name, v))
        f.write("#define TZD_WORDS %du\n" % len(W.w))


# ---- the check against ICU's own compile ----

def read_zoneinfo64(path):
    """ICU's zoneinfo64.txt (icu-data's tzdata/icunew/VERSION/44/): its names, each a link's
    target name or a zone's types, transitions (with their types) and final rule; its
    rules"""
    import re
    txt = open(path, encoding="utf-8-sig").read()
    names = re.findall(r'"([^"]+)"', txt[txt.index(" Names {"):txt.index(" Rules {")])
    zs = txt[txt.index(" Zones:array"):txt.index(" Names {")]
    out = {}
    for name, kind, body, idx in re.findall(r"/\* (\S+) \*/ :(int|table) \{(.*?)\}\s*//Z#(\d+)", zs, re.S):
        if kind == "int":
            out[name] = names[int(body)]
            continue
        d = dict((k, v.strip()) for k, v in re.findall(r"(\w+)(?::\w+)?\s*\{([^}]*)\}", body + "}"))

        def ints(k):
            return [int(x) for x in d.get(k, "").replace("\n", " ").split(",") if x.strip()]
        pre, post = ints("transPre32"), ints("transPost32")
        trans = [pre[i] * (1 << 32) + (pre[i + 1] & 0xFFFFFFFF) for i in range(0, len(pre), 2)] + ints("trans") + \
            [post[i] * (1 << 32) + (post[i + 1] & 0xFFFFFFFF) for i in range(0, len(post), 2)]
        to = ints("typeOffsets")
        types = [(to[i], to[i + 1]) for i in range(0, len(to), 2)]
        tm = d.get("typeMap", "").strip('"')
        tmap = [int(tm[i:i + 2], 16) for i in range(0, len(tm), 2)]
        final = None
        if "finalRule" in d:
            final = (d["finalRule"].strip('"'), int(d["finalRaw"]), int(d["finalYear"]))
        out[name] = (types, list(zip(trans, [types[m] for m in tmap])), final)
    rules = dict((n, [int(x) for x in v.replace("\n", " ").split(",")])
                 for n, v in re.findall(r"(\w+):intvector \{([^}]*)\}", txt[txt.index(" Rules {"):txt.index(" Regions")]))
    return out, rules


def check(path, rules, zones, links):
    """each of ICU's names a zone of the same offsets and final rule, or a link to the same
    zone: the differences, printed; their count"""
    icu, irules = read_zoneinfo64(path)
    compiled = compile_zones(rules, zones)
    target = dict((b, a) for a, b in links)
    bad = 0

    def changes(first, trans):
        out, cur = [], first
        for t, k in trans:
            if k != cur:
                out.append((t, k))
                cur = k
        return out
    for name, v in sorted(icu.items()):
        if isinstance(v, str):
            n = name
            while n in target and n not in zones:
                n = target[n]
            if n != v:
                print("%s: a link to %s, ICU's to %s" % (name, n, v))
                bad += 1
            continue
        if name not in zones:
            print("%s: no zone, ICU's is one" % name)
            bad += 1
            continue
        types, itrans, ifinal = v
        trans, default, final = compiled[name]
        mine = changes((default[3], default[4]), [(t, (ty[3], ty[4])) for t, ty in trans])
        theirs = changes(types[0], itrans)
        if (default[3], default[4]) != types[0] or mine != theirs:
            print("%s: offsets differ (%d changes, ICU's %d)" % (name, len(mine), len(theirs)))
            bad += 1
        if bool(final) != bool(ifinal):
            print("%s: final rule %s, ICU's %s" % (name, bool(final), ifinal))
            bad += 1
        elif final:
            rs, raw, year = final
            st = [r for r in rs if r.save][0]
            en = [r for r in rs if not r.save][0]
            vec = list(icu_rule(st)) + list(icu_rule(en)) + [st.save]
            if vec != irules[ifinal[0]] or raw != ifinal[1] or year != ifinal[2]:
                print("%s: final rule %s %d %d, ICU's %s %s" % (name, vec, raw, year, ifinal, irules[ifinal[0]]))
                bad += 1
    for name in sorted(set(zones) | set(target)):
        if name not in icu:
            print("%s: not ICU's" % name)
            bad += 1
    print("%d names checked, %d differ" % (len(icu), bad))
    return bad


def main():
    args = sys.argv[1:]
    against = None
    if args[:1] == ["--check"]:
        against = args[1]
        args = args[2:]
    src = args[0] if args else None
    rules, zones, links = read_source(rearguard_text(src))
    if against:
        sys.exit(1 if check(against, rules, zones, links) else 0)
    W, marks, ntrans = build(rules, zones, links)
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    emit(W, marks, os.path.join(root, "src/js/tz_data.c"), os.path.join(root, "src/js/tz_data.h"))
    print("%d zones, %d names, %d transitions, %d words" % (len(zones), dict(marks)["TZD_NAMES_N"], ntrans, len(W.w)))


if __name__ == "__main__":
    main()
