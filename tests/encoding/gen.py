#!/usr/bin/env python3
"""tests/encoding/gen.py [DIR] -- the cases of the encoding test, and what each must give.

Writes tests/encoding/cases.txt, which tests/exec/encoding.c reads and answers, and
prints the answers it must print (tests/exec/encoding.expect):

    python3 tests/encoding/gen.py > tests/exec/encoding.expect

The answers do not come from Ambitious's tables (src/html/encoding_tables.h) or from
the standard's index files. Each index's code point for a pointer is what Python's
codec of that encoding makes of the pointer's bytes: cp932 for index jis0208, euc_jp
for index jis0212, cp949 for EUC-KR, gb18030, big5hkscs, cp866, the ISO-8859, KOI8,
Mac and cp125x codecs. Where a codec and the standard differ, the standard's own text
decides, and each difference is listed below with its reason. How a decoder walks its
bytes -- which bytes a malformed sequence takes, what it restores to the queue -- is
the standard's text, its handlers transliterated here step by step; for UTF-8 and
UTF-16 Python's own decoders give the answer, and the transliteration must agree with
them. The labels are the standard's table (encodings.json: fetched, or DIR's). The
sniffing cases' answers are HTML 13.2.3.2's and MIME Sniffing's, written with each
case; tests/encoding/chromium.js shows where Chromium agrees, case by case.

Cases (one a line; bytes in hex):
    label HEX              the encoding the label names
    decode ENC HEX         the code points of the bytes in ENC, and the errors
    pairs ENC HEX          the bytes followed by each byte 00 to FF, each decoded alone:
                           how many inputs, code points and errors, and an FNV-1a hash
    quads ENC HEX          gb18030's: the two bytes followed by each byte 81 to FE and
                           then each 30 to 39
    sniff FILE TYPE HEX    a document's encoding and where it came from: its bytes,
                           the media type it was served as (hex; - for none), and
                           whether it is a local file (1) or not (0)"""
import json
import os
import random
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- the indexes' code points, from Python's codecs ------------------------------

SINGLE = [("IBM866", "cp866"), ("ISO-8859-2", "iso8859_2"), ("ISO-8859-3", "iso8859_3"), ("ISO-8859-4", "iso8859_4"),
          ("ISO-8859-5", "iso8859_5"), ("ISO-8859-6", "iso8859_6"), ("ISO-8859-7", "iso8859_7"), ("ISO-8859-8", "iso8859_8"),
          ("ISO-8859-8-I", "iso8859_8"), ("ISO-8859-10", "iso8859_10"), ("ISO-8859-13", "iso8859_13"),
          ("ISO-8859-14", "iso8859_14"), ("ISO-8859-15", "iso8859_15"), ("ISO-8859-16", "iso8859_16"), ("KOI8-R", "koi8_r"),
          ("KOI8-U", "koi8_u"), ("macintosh", "mac_roman"), ("windows-874", "cp874"), ("windows-1250", "cp1250"),
          ("windows-1251", "cp1251"), ("windows-1252", "cp1252"), ("windows-1253", "cp1253"), ("windows-1254", "cp1254"),
          ("windows-1255", "cp1255"), ("windows-1256", "cp1256"), ("windows-1257", "cp1257"), ("windows-1258", "cp1258"),
          ("x-mac-cyrillic", "mac_cyrillic")]

# The standard's single-byte indexes and Python's codecs differ in these. The bytes
# 0x80 to 0x9F that cp874 and cp1250 to cp1258 leave undefined are, in the standard's
# windows-874 and windows-125x, the C1 controls of their own value (the rule below).
# Besides: windows-1255's 0xCA is U+05BA, HEBREW POINT HOLAM HASER FOR VAV; and the
# standard's KOI8-U is KOI8-RU's, whose 0xAE and 0xBE are U+045E and U+040E (Python's
# koi8_u has box drawings there).
SINGLE_DIFF = {"windows-1255": {0xCA: 0x05BA}, "KOI8-U": {0xAE: 0x045E, 0xBE: 0x040E}}

# index gb18030 matches GB18030-2022 for two bytes (Python's codec is GB18030-2005's),
# except 0xA3 0xA0, which is U+3000 for deployed content (the standard, index gb18030)
GB18030_DIFF = {
    0xA3A0: 0x3000, 0xA6D9: 0xFE10, 0xA6DA: 0xFE12, 0xA6DB: 0xFE11, 0xA6DC: 0xFE13, 0xA6DD: 0xFE14, 0xA6DE: 0xFE15,
    0xA6DF: 0xFE16, 0xA6EC: 0xFE17, 0xA6ED: 0xFE18, 0xA6F3: 0xFE19, 0xA8BC: 0x1E3F, 0xFE59: 0x9FB4, 0xFE61: 0x9FB5,
    0xFE66: 0x9FB6, 0xFE67: 0x9FB7, 0xFE6D: 0x9FB8, 0xFE7E: 0x9FB9, 0xFE90: 0x9FBA, 0xFEA0: 0x9FBB,
}

# index jis0212: 0x8F 0xA2 0xB7 is U+FF5E, FULLWIDTH TILDE (Python's euc_jp: U+007E)
JIS0212_DIFF = {(0xA2 - 0xA1) * 94 + 0xB7 - 0xA1: 0xFF5E}

# index Big5, where Python's big5hkscs (Big5-HKSCS-2004) lacks a code point or maps it
# otherwise: the HKSCS-2008 additions, the standard's ETEN extensions (the control
# pictures from 0xA3 0xC0, the euro at 0xA3 0xE1), and the code points it prefers for
# a few Big5 characters (0xA1 0x45 U+2027, 0xA1 0xC2 U+00AF, 0xA2 0x44 U+FFE5 ...). By
# pointer, as index-big5.txt has them.
BIG5_DIFF = {
    1000: 0x3875, 1001: 0x21D53, 1002: 0x2369E, 1003: 0x26021, 1004: 0x3EEC, 1005: 0x258DE, 1006: 0x3AF5, 1007: 0x7AFC,
    1008: 0x9F97, 1009: 0x24161, 1010: 0x2890D, 1011: 0x231EA, 1012: 0x20A8A, 1013: 0x2325E, 1014: 0x430A, 1015: 0x8484,
    1016: 0x9F96, 1017: 0x942F, 1018: 0x4930, 1019: 0x8613, 1020: 0x5896, 1021: 0x974A, 1022: 0x9218, 1023: 0x79D0,
    1024: 0x7A32, 1025: 0x6660, 1026: 0x6A29, 1027: 0x889D, 1028: 0x744C, 1029: 0x7BC5, 1030: 0x6782, 1031: 0x7A2C,
    1032: 0x524F, 1033: 0x9046, 1034: 0x34E6, 1035: 0x73C4, 1036: 0x25DB9, 1037: 0x74C6, 1038: 0x9FC7, 1039: 0x57B3,
    1040: 0x492F, 1041: 0x544C, 1042: 0x4131, 1043: 0x2368E, 1044: 0x5818, 1045: 0x7A72, 1046: 0x27B65, 1047: 0x8B8F,
    1048: 0x46AE, 1049: 0x26E88, 1050: 0x4181, 1051: 0x25D99, 1052: 0x7BAE, 1053: 0x224BC, 1054: 0x9FC8, 1055: 0x224C1,
    1056: 0x224C9, 1057: 0x224CC, 1058: 0x9FC9, 1059: 0x8504, 1060: 0x235BB, 1061: 0x40B4, 1062: 0x9FCA, 1063: 0x44E1,
    1064: 0x2ADFF, 1065: 0x62C1, 1066: 0x706E, 1067: 0x9FCB, 2082: 0x7BB8, 2088: 0x7C06, 2103: 0x7CCE, 2114: 0x7DD2,
    2123: 0x7E1D, 2148: 0x8005, 2151: 0x8028, 2221: 0x83C1, 2239: 0x84A8, 2244: 0x840F, 2303: 0x89A6, 2304: 0x89A9,
    2354: 0x8D77, 2400: 0x90FD, 2413: 0x92B9, 2477: 0x975C, 2498: 0x97FF, 2605: 0x9F16, 2673: 0x8503, 2746: 0x5159,
    2747: 0x515B, 2748: 0x515D, 2749: 0x515E, 2771: 0x936E, 2780: 0x7479, 2990: 0x6D67, 3087: 0x799B, 3259: 0x9097,
    3301: 0x975D, 3436: 0x701E, 3451: 0x5B28, 4136: 0x7201, 4138: 0x77D7, 4141: 0x7E87, 4182: 0x99D6, 4206: 0x91D4,
    4220: 0x60DE, 4230: 0x6FB6, 4241: 0x8F36, 4258: 0x4FBB, 4273: 0x71DF, 4279: 0x9104, 4282: 0x9DF0, 4294: 0x83CF,
    4329: 0x5C10, 4330: 0x79E3, 4349: 0x5A67, 4419: 0x8F0B, 4422: 0x7B51, 4494: 0x62D0, 4624: 0x6062, 4694: 0x75F9,
    4708: 0x6C4A, 4742: 0x9B2E, 4748: 0x9F17, 4815: 0x50ED, 4828: 0x5F0C, 4902: 0x880F, 4922: 0x62CE, 4982: 0x7468,
    4992: 0x7162, 4997: 0x7250, 5029: 0x2027, 5038: 0xFE51, 5120: 0xAF, 5153: 0xFF5E, 5168: 0x2295, 5169: 0x2299,
    5182: 0x2215, 5183: 0xFE68, 5185: 0xFFE5, 5187: 0xFFE0, 5188: 0xFFE1, 5432: 0x2400, 5433: 0x2401, 5434: 0x2402,
    5435: 0x2403, 5436: 0x2404, 5437: 0x2405, 5438: 0x2406, 5439: 0x2407, 5440: 0x2408, 5441: 0x2409, 5442: 0x240A,
    5443: 0x240B, 5444: 0x240C, 5445: 0x240D, 5446: 0x240E, 5447: 0x240F, 5448: 0x2410, 5449: 0x2411, 5450: 0x2412,
    5451: 0x2413, 5452: 0x2414, 5453: 0x2415, 5454: 0x2416, 5455: 0x2417, 5456: 0x2418, 5457: 0x2419, 5458: 0x241A,
    5459: 0x241B, 5460: 0x241C, 5461: 0x241D, 5462: 0x241E, 5463: 0x241F, 5464: 0x2421, 5465: 0x20AC, 10942: 0x5EF4,
    10946: 0x65E0, 10948: 0x7676, 10950: 0x96B6, 10957: 0x3003, 10958: 0x4EDD, 19028: 0x5029, 19035: 0x507D, 19088: 0x5305,
    19096: 0x5344, 19112: 0x537F, 19162: 0x5605, 19240: 0x5A77, 19299: 0x5E75, 19305: 0x5ED0, 19326: 0x5F58, 19355: 0x60A4,
    19398: 0x6490, 19439: 0x6674, 19454: 0x675E, 19553: 0x6C9C, 19554: 0x6E1D, 19557: 0x6E2F, 19611: 0x716E, 19643: 0x732A,
    19672: 0x745C, 19697: 0x74E9, 19748: 0x7809,
}


def py_char(codec, b):
    """the one code point Python's codec makes of the bytes, or None"""
    try:
        s = b.decode(codec)
    except UnicodeDecodeError:
        return None
    return ord(s) if len(s) == 1 else None


def single_cp(name, codec, b):
    if b in SINGLE_DIFF.get(name, {}):
        return SINGLE_DIFF[name][b]
    cp = py_char(codec, bytes([b]))
    if cp is None and (codec == "cp874" or codec.startswith("cp125")) and b <= 0x9F:
        return b
    return cp


def jis0208(p):
    """index jis0208 code point: the pointer's Shift_JIS bytes in cp932"""
    lead, trail = divmod(p, 188)
    return py_char("cp932", bytes([lead + (0x81 if lead < 0x1F else 0xC1), trail + (0x40 if trail < 0x3F else 0x41)]))


def jis0212(p):
    if p in JIS0212_DIFF:
        return JIS0212_DIFF[p]
    lead, trail = divmod(p, 94)
    return py_char("euc_jp", bytes([0x8F, 0xA1 + lead, 0xA1 + trail]))


def euc_kr(p):
    lead, trail = divmod(p, 190)
    return py_char("cp949", bytes([0x81 + lead, 0x41 + trail]))


def gb18030(p):
    lead, t = divmod(p, 190)
    b = bytes([0x81 + lead, t + (0x40 if t < 0x3F else 0x41)])
    if b[0] << 8 | b[1] in GB18030_DIFF:
        return GB18030_DIFF[b[0] << 8 | b[1]]
    return py_char("gb18030", b)


def gb18030_ranges(p):
    """index gb18030 ranges code point: the standard's limits and its 7457, else the
    pointer's four bytes in Python's gb18030"""
    if (39419 < p < 189000) or p > 1237575:
        return None
    if p == 7457:
        return 0xE7C7
    b4 = p % 10
    b3 = p // 10 % 126
    b2 = p // 1260 % 10
    b1 = p // 12600
    return py_char("gb18030", bytes([0x81 + b1, 0x30 + b2, 0x81 + b3, 0x30 + b4]))


def big5(p):
    if p in BIG5_DIFF:
        return BIG5_DIFF[p]
    lead, t = divmod(p, 157)
    return py_char("big5hkscs", bytes([0x81 + lead, t + (0x40 if t < 0x3F else 0x62)]))


# ---- the decoders: the standard's handlers, step by step ----------------------------

EOQ, FINISHED, CONTINUE, ERROR = "end-of-queue", "finished", "continue", "error"


class Queue:
    """an I/O queue of bytes: restore puts items back at its front"""
    def __init__(self, data):
        self.items = list(data)

    def read(self):
        return self.items.pop(0) if self.items else EOQ

    def restore(self, items):
        self.items[0:0] = items


def ascii_byte(b):
    return b != EOQ and b <= 0x7F


class UTF8:
    def __init__(s):
        s.cp, s.seen, s.needed, s.lower, s.upper = 0, 0, 0, 0x80, 0xBF

    def handler(s, q, b):
        if b == EOQ and s.needed != 0:
            s.needed = 0
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.needed == 0:
            if b <= 0x7F:
                return [b]
            if 0xC2 <= b <= 0xDF:
                s.needed, s.cp = 1, b & 0x1F
            elif 0xE0 <= b <= 0xEF:
                if b == 0xE0:
                    s.lower = 0xA0
                if b == 0xED:
                    s.upper = 0x9F
                s.needed, s.cp = 2, b & 0xF
            elif 0xF0 <= b <= 0xF4:
                if b == 0xF0:
                    s.lower = 0x90
                if b == 0xF4:
                    s.upper = 0x8F
                s.needed, s.cp = 3, b & 0x7
            else:
                return ERROR
            return CONTINUE
        if not (s.lower <= b <= s.upper):
            s.cp, s.needed, s.seen, s.lower, s.upper = 0, 0, 0, 0x80, 0xBF
            q.restore([b])
            return ERROR
        s.lower, s.upper = 0x80, 0xBF
        s.cp = (s.cp << 6) | (b & 0x3F)
        s.seen += 1
        if s.seen != s.needed:
            return CONTINUE
        cp = s.cp
        s.cp, s.needed, s.seen = 0, 0, 0
        return [cp]


class Single:
    def __init__(s, name, codec):
        s.table = [single_cp(name, codec, b) for b in range(0x80, 0x100)]

    def handler(s, q, b):
        if b == EOQ:
            return FINISHED
        if b <= 0x7F:
            return [b]
        cp = s.table[b - 0x80]
        return ERROR if cp is None else [cp]


class GB18030:
    def __init__(s):
        s.first = s.second = s.third = 0

    def handler(s, q, b):
        if b == EOQ and s.first == s.second == s.third == 0:
            return FINISHED
        if b == EOQ:
            s.first = s.second = s.third = 0
            return ERROR
        if s.third != 0:
            if not (0x30 <= b <= 0x39):
                q.restore([s.second, s.third, b])
                s.first = s.second = s.third = 0
                return ERROR
            cp = gb18030_ranges(((s.first - 0x81) * (10 * 126 * 10)) + ((s.second - 0x30) * (10 * 126)) +
                                ((s.third - 0x81) * 10) + b - 0x30)
            s.first = s.second = s.third = 0
            return ERROR if cp is None else [cp]
        if s.second != 0:
            if 0x81 <= b <= 0xFE:
                s.third = b
                return CONTINUE
            q.restore([s.second, b])
            s.first = s.second = 0
            return ERROR
        if s.first != 0:
            if 0x30 <= b <= 0x39:
                s.second = b
                return CONTINUE
            leading, s.first = s.first, 0
            pointer = None
            offset = 0x40 if b < 0x7F else 0x41
            if 0x40 <= b <= 0x7E or 0x80 <= b <= 0xFE:
                pointer = (leading - 0x81) * 190 + (b - offset)
            cp = None if pointer is None else gb18030(pointer)
            if cp is not None:
                return [cp]
            if ascii_byte(b):
                q.restore([b])
            return ERROR
        if b <= 0x7F:
            return [b]
        if b == 0x80:
            return [0x20AC]
        if 0x81 <= b <= 0xFE:
            s.first = b
            return CONTINUE
        return ERROR


BIG5_TWO = {1133: [0x00CA, 0x0304], 1135: [0x00CA, 0x030C], 1164: [0x00EA, 0x0304], 1166: [0x00EA, 0x030C]}


class Big5:
    def __init__(s):
        s.leading = 0

    def handler(s, q, b):
        if b == EOQ and s.leading != 0:
            s.leading = 0
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.leading != 0:
            leading, s.leading = s.leading, 0
            pointer = None
            offset = 0x40 if b < 0x7F else 0x62
            if 0x40 <= b <= 0x7E or 0xA1 <= b <= 0xFE:
                pointer = (leading - 0x81) * 157 + (b - offset)
            if pointer in BIG5_TWO:
                return BIG5_TWO[pointer]
            cp = None if pointer is None else big5(pointer)
            if cp is not None:
                return [cp]
            if ascii_byte(b):
                q.restore([b])
            return ERROR
        if b <= 0x7F:
            return [b]
        if 0x81 <= b <= 0xFE:
            s.leading = b
            return CONTINUE
        return ERROR


class EUCJP:
    def __init__(s):
        s.jis0212, s.leading = False, 0

    def handler(s, q, b):
        if b == EOQ and s.leading != 0:
            s.leading = 0
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.leading == 0x8E and 0xA1 <= b <= 0xDF:
            s.leading = 0
            return [0xFF61 - 0xA1 + b]
        if s.leading == 0x8F and 0xA1 <= b <= 0xFE:
            s.jis0212, s.leading = True, b
            return CONTINUE
        if s.leading != 0:
            leading, s.leading = s.leading, 0
            cp = None
            if 0xA1 <= leading <= 0xFE and 0xA1 <= b <= 0xFE:
                p = (leading - 0xA1) * 94 + b - 0xA1
                cp = jis0212(p) if s.jis0212 else jis0208(p)
            s.jis0212 = False
            if cp is not None:
                return [cp]
            if ascii_byte(b):
                q.restore([b])
            return ERROR
        if b <= 0x7F:
            return [b]
        if b in (0x8E, 0x8F) or 0xA1 <= b <= 0xFE:
            s.leading = b
            return CONTINUE
        return ERROR


class ISO2022JP:
    def __init__(s):
        s.state = s.output_state = "ASCII"
        s.leading, s.output = 0, False

    def handler(s, q, b):
        st = s.state
        if st in ("ASCII", "Roman", "katakana", "lead"):
            if b == 0x1B:
                s.state = "escape start"
                return CONTINUE
            if b == EOQ:
                return FINISHED
            s.output = False
            if st == "ASCII" and b <= 0x7F and b not in (0x0E, 0x0F):
                return [b]
            if st == "Roman" and b == 0x5C:
                return [0xA5]
            if st == "Roman" and b == 0x7E:
                return [0x203E]
            if st == "Roman" and b <= 0x7F and b not in (0x0E, 0x0F):
                return [b]
            if st == "katakana" and 0x21 <= b <= 0x5F:
                return [0xFF61 - 0x21 + b]
            if st == "lead" and 0x21 <= b <= 0x7E:
                s.leading, s.state = b, "trail"
                return CONTINUE
            return ERROR
        if st == "trail":
            if b == 0x1B:
                s.state = "escape start"
                return ERROR
            s.state = "lead"
            if b != EOQ and 0x21 <= b <= 0x7E:
                cp = jis0208((s.leading - 0x21) * 94 + b - 0x21)
                return ERROR if cp is None else [cp]
            return ERROR
        if st == "escape start":
            if b in (0x24, 0x28):
                s.leading, s.state = b, "escape"
                return CONTINUE
            if b != EOQ:
                q.restore([b])
            s.output, s.state = False, s.output_state
            return ERROR
        # escape
        leading, s.leading = s.leading, 0
        state = None
        if leading == 0x28 and b == 0x42:
            state = "ASCII"
        elif leading == 0x28 and b == 0x4A:
            state = "Roman"
        elif leading == 0x28 and b == 0x49:
            state = "katakana"
        elif leading == 0x24 and b in (0x40, 0x42):
            state = "lead"
        if state is not None:
            s.state = s.output_state = state
            output, s.output = s.output, True
            return CONTINUE if not output else ERROR
        q.restore([leading] if b == EOQ else [leading, b])
        s.output, s.state = False, s.output_state
        return ERROR


class ShiftJIS:
    def __init__(s):
        s.leading = 0

    def handler(s, q, b):
        if b == EOQ and s.leading != 0:
            s.leading = 0
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.leading != 0:
            leading, s.leading = s.leading, 0
            pointer = None
            offset = 0x40 if b < 0x7F else 0x41
            leading_offset = 0x81 if leading < 0xA0 else 0xC1
            if 0x40 <= b <= 0x7E or 0x80 <= b <= 0xFC:
                pointer = (leading - leading_offset) * 188 + b - offset
            if pointer is not None and 8836 <= pointer <= 10715:
                return [0xE000 - 8836 + pointer]
            cp = None if pointer is None else jis0208(pointer)
            if cp is not None:
                return [cp]
            if ascii_byte(b):
                q.restore([b])
            return ERROR
        if b <= 0x80:
            return [b]
        if 0xA1 <= b <= 0xDF:
            return [0xFF61 - 0xA1 + b]
        if 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xFC:
            s.leading = b
            return CONTINUE
        return ERROR


class EUCKR:
    def __init__(s):
        s.leading = 0

    def handler(s, q, b):
        if b == EOQ and s.leading != 0:
            s.leading = 0
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.leading != 0:
            leading, s.leading = s.leading, 0
            pointer = (leading - 0x81) * 190 + (b - 0x41) if 0x41 <= b <= 0xFE else None
            cp = None if pointer is None else euc_kr(pointer)
            if cp is not None:
                return [cp]
            if ascii_byte(b):
                q.restore([b])
            return ERROR
        if b <= 0x7F:
            return [b]
        if 0x81 <= b <= 0xFE:
            s.leading = b
            return CONTINUE
        return ERROR


class Replacement:
    def __init__(s):
        s.returned = False

    def handler(s, q, b):
        if b == EOQ:
            return FINISHED
        if not s.returned:
            s.returned = True
            return ERROR
        return FINISHED


class UTF16:
    def __init__(s, be):
        s.lead_byte, s.lead_surrogate, s.be = None, None, be

    def handler(s, q, b):
        if b == EOQ and (s.lead_byte is not None or s.lead_surrogate is not None):
            s.lead_byte = s.lead_surrogate = None
            return ERROR
        if b == EOQ:
            return FINISHED
        if s.lead_byte is None:
            s.lead_byte = b
            return CONTINUE
        unit = (s.lead_byte << 8) + b if s.be else (b << 8) + s.lead_byte
        s.lead_byte = None
        if s.lead_surrogate is not None:
            lead, s.lead_surrogate = s.lead_surrogate, None
            if 0xDC00 <= unit <= 0xDFFF:
                return [0x10000 + ((lead - 0xD800) << 10) + (unit - 0xDC00)]
            b1, b2 = unit >> 8, unit & 0xFF
            q.restore([b1, b2] if s.be else [b2, b1])
            return ERROR
        if 0xD800 <= unit <= 0xDBFF:
            s.lead_surrogate = unit
            return CONTINUE
        if 0xDC00 <= unit <= 0xDFFF:
            return ERROR
        return [unit]


class XUserDefined:
    def handler(s, q, b):
        if b == EOQ:
            return FINISHED
        if b <= 0x7F:
            return [b]
        return [0xF780 + b - 0x80]


def decoder(name):
    for n, c in SINGLE:
        if n == name:
            return Single(n, c)
    return {"UTF-8": UTF8, "GBK": GB18030, "gb18030": GB18030, "Big5": Big5, "EUC-JP": EUCJP, "ISO-2022-JP": ISO2022JP,
            "Shift_JIS": ShiftJIS, "EUC-KR": EUCKR, "replacement": Replacement, "UTF-16BE": lambda: UTF16(True),
            "UTF-16LE": lambda: UTF16(False), "x-user-defined": XUserDefined}[name]()


def process(name, data):
    """process a queue (4.1), in the replacement error mode: the code points, the errors"""
    dec, q, out, errors = decoder(name), Queue(data), [], 0
    while True:
        r = dec.handler(q, q.read())
        if r == FINISHED:
            return out, errors
        if r == ERROR:
            out.append(0xFFFD)
            errors += 1
        elif r != CONTINUE:
            out.extend(r)


def decode(name, data):
    """the answer: Python's own decoder for UTF-8 and UTF-16, which the transliteration
    must agree with; the transliteration over Python's codecs' indexes for the rest"""
    cps, errors = process(name, data)
    py = {"UTF-8": "utf-8", "UTF-16LE": "utf-16-le", "UTF-16BE": "utf-16-be"}.get(name)
    if py:
        s = data.decode(py, errors="replace")
        assert [ord(c) for c in s] == cps, (name, data.hex(), [hex(ord(c)) for c in s], [hex(c) for c in cps])
    return cps, errors


# ---- the cases ----------------------------------------------------------------------

def fetch(name):
    if len(sys.argv) > 1:
        with open(os.path.join(sys.argv[1], name), "rb") as f:
            return f.read().decode("utf-8")
    with urllib.request.urlopen("https://encoding.spec.whatwg.org/" + name, timeout=60) as r:
        return r.read().decode("utf-8")


cases, expect = [], []


def fnv(acc, cps):
    for cp in cps:
        acc = ((acc ^ cp) * 0x01000193) & 0xFFFFFFFF
    return ((acc ^ 0xFFFFFFFF) * 0x01000193) & 0xFFFFFFFF


def cps_text(cps):
    return " ".join("U+%04X" % c for c in cps) if cps else "(nothing)"


def case_decode(name, data):
    line = "decode %s %s" % (name.lower(), data.hex() or "-")
    cps, errors = decode(name, data)
    cases.append(line)
    expect.append("%s -> %s (%d error%s)" % (line, cps_text(cps), errors, "" if errors == 1 else "s"))


def summary(line, inputs):
    acc, n, errs = 0x811C9DC5, 0, 0
    for data in inputs:
        cps, e = decode(NAME_OF[line.split()[1]], data)
        acc = fnv(acc, cps)
        n += len(cps)
        errs += e
    cases.append(line)
    expect.append("%s -> %d inputs, %d code points, %d errors, fnv1a32 %08x" % (line, len(inputs), n, errs, acc))


def case_pairs(name, prefix):
    summary("pairs %s %s" % (name.lower(), prefix.hex() or "-"), [prefix + bytes([b]) for b in range(256)])


def case_quads(name, prefix):
    summary("quads %s %s" % (name.lower(), prefix.hex()), [prefix + bytes([b3, b4]) for b3 in range(0x81, 0xFF) for b4 in range(0x30, 0x3A)])


groups = json.loads(fetch("encodings.json"))
encodings = [e for g in groups for e in g["encodings"]]
NAMES = [e["name"] for e in encodings]
NAME_OF = {n.lower(): n for n in NAMES}
LABELS = {l: e["name"] for e in encodings for l in e["labels"]}

# labels: every one; their case and the ASCII white space around them ignored, nothing else
for label in sorted(LABELS, key=lambda l: l.encode()):
    cases.append("label " + label.encode().hex())
    expect.append("label %s -> %s" % (label.encode().hex(), LABELS[label]))
WS = "\t\n\f\r "
for text, name in [(" utf-8", "UTF-8"), ("UTF-8\t", "UTF-8"), ("\n\f\rShift_JIS \t", "Shift_JIS"), ("SJIS", "Shift_JIS"),
                   ("Windows-1252", "windows-1252"), ("ISO-8859-1", "windows-1252"), ("US-ASCII", "windows-1252"),
                   ("X-User-Defined", "x-user-defined"), ("UTF-16", "UTF-16LE"), ("HZ-GB-2312", "replacement"),
                   ("utf-7", None), ("utf8 ", "UTF-8"), ("\vutf-8", None), ("utf-8\x00", None), ("u t f-8", None),
                   ("", None), ("   ", None), ("cseucpkdfmtjapanesex", None), ("ut", None), ("utf-80", None),
                   ("iso-8859-11", "windows-874"), ("iso-8859-9", "windows-1254"), ("ucs-2", "UTF-16LE"), ("gb2312", "GBK"),
                   ("x-gbk", "GBK"), ("koi", "KOI8-R"), ("é", None)]:
    assert name is None or LABELS.get(text.strip(WS).lower()) == name, text
    assert name is not None or text.strip(WS).lower() not in LABELS, text
    cases.append("label " + (text.encode().hex() or "-"))
    expect.append("label %s -> %s" % (text.encode().hex() or "-", name or "failure"))

# the single-byte encodings: each byte from 0x80 up, and x-user-defined's
for name, _ in SINGLE:
    case_decode(name, bytes(range(0x80, 0x100)))
case_decode("x-user-defined", bytes(range(0x00, 0x100)))
for name in ("windows-1252", "KOI8-U", "ISO-8859-8-I", "x-user-defined"):
    case_decode(name, b"plain ASCII 0x7F: \x7f")

# every pair of bytes of each multi-byte encoding, by its first byte
for name, leads in [("gb18030", range(0x81, 0xFF)), ("Big5", range(0x81, 0xFF)), ("EUC-KR", range(0x81, 0xFF)),
                    ("Shift_JIS", list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))), ("EUC-JP", range(0xA1, 0xFF))]:
    case_pairs(name, b"")
    for lead in leads:
        case_pairs(name, bytes([lead]))
for lead in (0x80, 0x81, 0xA1, 0xD7, 0xFE, 0xFF):
    case_pairs("GBK", bytes([lead]))
for name, lead in [("Big5", 0x80), ("Big5", 0xFF), ("EUC-KR", 0x80), ("EUC-KR", 0xFF), ("Shift_JIS", 0xA0), ("Shift_JIS", 0xFD),
                   ("EUC-JP", 0x8E), ("EUC-JP", 0x80), ("EUC-JP", 0xA0), ("EUC-JP", 0xFF)]:
    case_pairs(name, bytes([lead]))
for lead in range(0xA1, 0xFF):
    case_pairs("EUC-JP", bytes([0x8F, lead]))
case_pairs("EUC-JP", bytes([0x8F]))
# gb18030's four-byte sequences: every one of the Basic Multilingual Plane's, then
# samples of the supplementary planes' and of the pointers that map to nothing
for b1 in range(0x81, 0x85):
    for b2 in range(0x30, 0x3A):
        case_quads("gb18030", bytes([b1, b2]))
for b1, b2 in [(0x85, 0x30), (0x8F, 0x39), (0x90, 0x30), (0x90, 0x31), (0x95, 0x32), (0xA0, 0x35), (0xE3, 0x32),
               (0xE3, 0x33), (0xE4, 0x30), (0xFE, 0x39)]:
    case_quads("gb18030", bytes([b1, b2]))
case_quads("GBK", bytes([0x81, 0x30]))
# ISO-2022-JP: each state's bytes, each escape's
for prefix in [b"", b"\x1b", b"\x1b$", b"\x1b(", b"\x1b(B", b"\x1b(J", b"\x1b(I", b"\x1b$@", b"\x1b$B"]:
    case_pairs("ISO-2022-JP", prefix)
for lead in range(0x21, 0x7F):
    case_pairs("ISO-2022-JP", b"\x1b$B" + bytes([lead]))
# UTF-8: every two-byte sequence by its first byte, and the three- and four-byte
# sequences at each boundary of the lower and upper bounds
for lead in range(0x80, 0x100):
    case_pairs("UTF-8", bytes([lead]))
case_pairs("UTF-8", b"")
for prefix in [b"\xe0\xa0", b"\xe0\x80", b"\xe0\x9f", b"\xed\x9f", b"\xed\xa0", b"\xef\xbf", b"\xf0\x90", b"\xf0\x8f", b"\xf4\x8f",
               b"\xf4\x90", b"\xf0\x90\x80", b"\xf4\x8f\xbf", b"\xf1\x80\x80", b"\xf5\x80\x80", b"\xc2\x80"]:
    case_pairs("UTF-8", prefix)
# UTF-16: code units of each kind, surrogates whole and broken
for name in ("UTF-16LE", "UTF-16BE"):
    be = name == "UTF-16BE"
    unit = lambda u: bytes([u >> 8, u & 0xFF]) if be else bytes([u & 0xFF, u >> 8])
    for prefix in [b"", unit(0xD800)[:1], unit(0xD800), unit(0xDBFF), unit(0xDC00), unit(0x0041), unit(0xD834) + unit(0xDD1E)[:1]]:
        case_pairs(name, prefix)
    for hi in (0xD8, 0xDB, 0xDC, 0xDF, 0x00, 0xFF):
        case_pairs(name, unit(0xD800) + (bytes([hi]) if be else b""))
    for data in [unit(0xD83D) + unit(0xDE00), unit(0xD83D) + unit(0x0041), unit(0xDE00) + unit(0xD83D), unit(0xD83D), unit(0xD83D)[:1],
                 unit(0xFEFF) + unit(0x0042), unit(0xFFFE), b"", unit(0x00E9)[:1]]:
        case_decode(name, data)
# replacement: one error for any input, none for none
for data in [b"", b"a", b"\x00\x80\xff abc"]:
    case_decode("replacement", data)

# malformed and boundary sequences, as the handlers' steps take them
EDGES = {
    "UTF-8": [b"\xef\xbb\xbfa", b"\xc0\x80", b"\xe0\x80\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xf8\x88\x80\x80\x80",
              b"\xe3\x81", b"\xe3\x81a", b"\xf0\x9f\x98", b"a\x80b", b"\xc2", b"\xf0\x9f\x98\x80"],
    "Shift_JIS": [b"\x82\xa0", b"\x82A", b"\x82", b"\x82\x7f", b"\x82\xfd", b"\xf0\x40", b"\xf9\xfc", b"\xfa\x40", b"\xa0", b"\x80",
                  b"\xa1\xdf", b"\x5c\x7e", b"\x81\x5f"],
    "EUC-JP": [b"\xa4\xa2", b"\x8e\xa1", b"\x8e\xe0", b"\x8f\xa2\xaf", b"\x8f\xa2", b"\x8fA", b"\x8f\xa2A", b"\xa4", b"\xa4A", b"\x8f\xa2\xb7"],
    "EUC-KR": [b"\xb0\xa1", b"\x8c\x63", b"\xb0", b"\xb0A", b"\x81\x41", b"\xc9\xa1", b"\xfe\xfe", b"\xa2\xe6", b"\xa2\xe7"],
    "gb18030": [b"\x81\x30\x81\x30", b"\x81\x35\xf4\x37", b"\xa8\xbc", b"\x84\x31\xa4\x39", b"\x84\x31\xa5\x30", b"\x90\x30\x81\x30",
                b"\xe3\x32\x9a\x35", b"\xe3\x32\x9a\x36", b"\x81\x30\x81", b"\x81\x30", b"\x81\x30\x81A", b"\x81\x30A", b"\x80",
                b"\xff", b"\xa3\xa0", b"\x81\x30\xff\x30", b"\x81\x30\x81\x30\x81"],
    "Big5": [b"\x88\x62", b"\x88\x64", b"\x88\xa3", b"\x88\xa5", b"\x87\x40", b"\xa4\x40", b"\x80", b"\xa4", b"\xa4A", b"\xa1\x45",
             b"\xa3\xe1", b"\xc8\x7e", b"\xfe\xfe", b"\x88\x62\x88\x64"],
    "ISO-2022-JP": [b"\x1b$B\x30\x21\x1b(B", b"\x1b(J\x5c\x7e", b"\x1b(I\x21\x5f", b"\x1b$B\x30", b"\x1b$B\x30\x1b(Ba",
                    b"\x1b(B\x1b(B", b"\x1b(Ba\x1b(B", b"\x1b", b"\x1b$", b"\x1b(", b"\x1bX", b"\x1b$X", b"\x0e", b"\x0f",
                    b"\x80", b"\x1b$@\x21\x21", b"\x1b(I\x60"],
}
for name, datas in EDGES.items():
    for data in datas:
        case_decode(name, data)

# random bytes, weighted to each encoding's structure, a fixed seed
rng = random.Random(12)
FAVOURED = {
    "UTF-8": [0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF],
    "gb18030": [0x30, 0x35, 0x39, 0x41, 0x7F, 0x80, 0x81, 0x84, 0x90, 0xA3, 0xA8, 0xE3, 0xFE, 0xFF],
    "Big5": [0x40, 0x41, 0x7E, 0x7F, 0x80, 0x81, 0x87, 0x88, 0xA1, 0xA4, 0xC8, 0xF9, 0xFE, 0xFF],
    "EUC-JP": [0x41, 0x80, 0x8E, 0x8F, 0xA1, 0xA2, 0xA4, 0xB7, 0xDF, 0xE0, 0xFE, 0xFF],
    "ISO-2022-JP": [0x1B, 0x24, 0x28, 0x40, 0x42, 0x49, 0x4A, 0x21, 0x30, 0x5C, 0x5F, 0x60, 0x7E, 0x0E, 0x0F, 0x80],
    "Shift_JIS": [0x40, 0x41, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x9F, 0xA0, 0xA1, 0xDF, 0xE0, 0xF0, 0xFA, 0xFC, 0xFD, 0xFF],
    "EUC-KR": [0x41, 0x5A, 0x61, 0x7F, 0x80, 0x81, 0x8C, 0xA1, 0xB0, 0xC9, 0xFE, 0xFF],
    "UTF-16LE": [0x00, 0x41, 0xD8, 0xDB, 0xDC, 0xDF, 0xFE, 0xFF],
    "UTF-16BE": [0x00, 0x41, 0xD8, 0xDB, 0xDC, 0xDF, 0xFE, 0xFF],
}
for name in NAMES:
    for _ in range(12):
        pool = FAVOURED.get(name)
        data = bytes(rng.choice(pool) if pool and rng.random() < 0.8 else rng.randrange(256) for _ in range(rng.randrange(1, 17)))
        case_decode(name, data)

# ---- sniffing (HTML 13.2.3.2), each with its answer --------------------------------


def sniff(file, typ, data, name, source):
    line = "sniff %d %s %s" % (file, typ.encode().hex() or "-", data.hex() or "-")
    cases.append(line)
    expect.append("%s -> %s %s" % (line, name, source))


A = "abc éè 日本".encode("utf-8")       # not ASCII, and UTF-8
L1 = b"caf\xe9 na\xefve"                              # not ASCII, not UTF-8
SNIFF = [
    # BOM sniffing comes first, over the transport layer's and the prescan's
    (0, "text/html; charset=koi8-r", b"\xef\xbb\xbf<meta charset=shift_jis>", "UTF-8", "bom"),
    (0, "", b"\xfe\xff\x00<", "UTF-16BE", "bom"),
    (0, "", b"\xff\xfe<\x00", "UTF-16LE", "bom"),
    (0, "", b"\xff\xfe", "UTF-16LE", "bom"),
    (0, "", b"\xef\xbb", "windows-1252", "default"),
    # the transport layer's charset: MIME Sniffing's parsing of the type
    (0, "text/html; charset=koi8-r", b"<meta charset=windows-1251>", "KOI8-R", "http"),
    (0, "text/html;charset=\"Shift_JIS\"", b"", "Shift_JIS", "http"),
    (0, "text/html;charset=\"sh\\ift_jis\"", b"", "Shift_JIS", "http"),
    (0, "TEXT/HTML ; CHARSET=EUC-KR", b"", "EUC-KR", "http"),              # the subtype's white space trimmed
    (0, "text/ html; charset=euc-kr", b"", "windows-1252", "default"),     # a subtype not a token: no type
    (0, "text/html ;charset=euc-kr", b"", "EUC-KR", "http"),
    (0, "text/html; CHARSET=euc-kr", b"", "EUC-KR", "http"),
    (0, "text/html; charset='utf-8'", b"", "windows-1252", "default"),   # ' is no quote in a MIME type
    (0, "text/html; charset=bogus", b"<meta charset=gbk>", "GBK", "meta"),
    (0, "text/html; charset=; charset=big5", b"", "Big5", "http"),
    (0, "text/html; charset=big5; charset=gbk", b"", "Big5", "http"),
    (0, "text/html; charset =big5", b"", "windows-1252", "default"),     # the name is 'charset '
    (0, ";charset=big5", b"", "windows-1252", "default"),                # no type, no parameters
    (0, "text/html; charset=\"utf-16\"", b"", "UTF-16LE", "http"),        # from the transport, kept
    (0, "text/html;charset=iso-2022-kr", b"", "replacement", "http"),
    (0, "text/html; foo=\"a;b\"; charset=koi8-u", b"", "KOI8-U", "http"),
    # the prescan: meta charset, and http-equiv with content
    (0, "", b"<meta charset=shift_jis>", "Shift_JIS", "meta"),
    (0, "", b"<META CHARSET='EUC-JP'>", "EUC-JP", "meta"),
    (0, "", b"<meta charset=\"  big5 \">", "Big5", "meta"),
    (0, "", b"<meta/charset=koi8-r>", "KOI8-R", "meta"),
    (0, "", b"<meta\tcharset=koi8-r>", "KOI8-R", "meta"),
    (0, "", b"<metacharset=koi8-r>", "windows-1252", "default"),
    (0, "", b"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=euc-kr\">", "EUC-KR", "meta"),
    (0, "", b"<meta content=\"text/html; charset=euc-kr\" http-equiv=\"content-type\">", "EUC-KR", "meta"),
    (0, "", b"<meta content=\"text/html; charset=euc-kr\">", "windows-1252", "default"),
    (0, "", b"<meta http-equiv=refresh content=\"charset=euc-kr\">", "windows-1252", "default"),
    (0, "", b"<meta content=\"charset=euc-kr\" charset=koi8-r http-equiv=content-type>", "KOI8-R", "meta"),
    (0, "", b"<meta charset=koi8-r content=\"charset=euc-kr\" http-equiv=content-type>", "KOI8-R", "meta"),
    (0, "", b"<meta charset=bogus><meta charset=koi8-u>", "KOI8-U", "meta"),
    (0, "", b"<meta charset=koi8-u charset=koi8-r>", "KOI8-U", "meta"),
    (0, "", b"<meta http-equiv=content-type content=\"text/html; charset='koi8-r'\">", "KOI8-R", "meta"),
    (0, "", b"<meta http-equiv=content-type content=\"text/html; charset = koi8-r\">", "KOI8-R", "meta"),
    (0, "", b"<meta http-equiv=content-type content=\"charsetcharset=koi8-r\">", "KOI8-R", "meta"),
    (0, "", b"<meta http-equiv=content-type content=\"text/html; charset\">", "windows-1252", "default"),
    (0, "", b"<meta http-equiv=content-type content=\"charset=\"><meta charset=gbk>", "GBK", "meta"),
    (0, "", b"<meta http-equiv=content-type content='charset=\"koi8-r'>", "windows-1252", "default"),
    (0, "", b"<meta charset=utf-16>", "UTF-8", "meta"),
    (0, "", b"<meta charset=utf-16be>", "UTF-8", "meta"),
    (0, "", b"<meta charset=x-user-defined>", "windows-1252", "meta"),
    (0, "", b"<meta charset=iso-2022-kr>", "replacement", "meta"),
    # what the prescan passes over: comments, other tags' attributes, <! </ <?
    (0, "", b"<!-- <meta charset=koi8-r> --><meta charset=gbk>", "GBK", "meta"),
    (0, "", b"<!--><meta charset=koi8-r>", "KOI8-R", "meta"),
    (0, "", b"<!---><meta charset=koi8-r> --><meta charset=gbk>", "KOI8-R", "meta"),
    (0, "", b"<a title=\"<meta charset=koi8-r>\"><meta charset=gbk>", "GBK", "meta"),
    (0, "", b"<a title='x' href=y <meta charset=koi8-r>", "windows-1252", "default"),
    (0, "", b"</meta charset=koi8-r><meta charset=gbk>", "GBK", "meta"),
    (0, "", b"<!doctype html><?php charset=koi8-r ?><meta charset=gbk>", "GBK", "meta"),
    (0, "", b"</ meta charset=koi8-r>", "windows-1252", "default"),
    (0, "", b"<meta charset=\"koi8-r", "windows-1252", "default"),          # the bytes run out
    (0, "", b"<!-- <meta charset=koi8-r>", "windows-1252", "default"),
    # the end condition: the first 1024 bytes
    (0, "", b"<!--" + b"x" * 1012 + b"--><meta charset=koi8-r>", "windows-1252", "default"),
    (0, "", b"<!--" + b"x" * 990 + b"--><meta charset=koi8-r>", "KOI8-R", "meta"),
    # XML declarations
    (0, "", b"<?xml version=\"1.0\" encoding=\"windows-1251\"?><p>", "windows-1251", "xml"),
    (0, "", b"<?xml version='1.0' encoding = 'KOI8-R' ?>", "KOI8-R", "xml"),
    (0, "", b"<?xml encoding=\"utf-16\"?>", "UTF-8", "xml"),
    (0, "", b"<?xml encoding=\"x-user-defined\"?>", "x-user-defined", "xml"),
    (0, "", b" <?xml version=\"1.0\" encoding=\"koi8-r\"?>", "windows-1252", "default"),
    (0, "", b"<?xml version=\"1.0\"?><!-- encoding=\"koi8-r\" -->", "windows-1252", "default"),
    (0, "", b"<?xml encoding=\"koi8 r\"?>", "windows-1252", "default"),
    (0, "", b"<?xml version=\"1.0\" encoding=\"koi8-r\"?><meta charset=gbk>", "GBK", "meta"),
    (0, "", "<?xml version=\"1.0\"?>".encode("utf-16-le"), "UTF-16LE", "xml"),
    (0, "", "<?xml version=\"1.0\"?>".encode("utf-16-be"), "UTF-16BE", "xml"),
    # nothing declared: UTF-8 detected in a local file's bytes, else the default
    (1, "", A, "UTF-8", "detected"),
    (0, "", A, "windows-1252", "default"),
    (1, "", L1, "windows-1252", "default"),
    (1, "", b"plain ascii", "windows-1252", "default"),
    (1, "", b"", "windows-1252", "default"),
    (1, "", b"<meta charset=koi8-r>" + A, "KOI8-R", "meta"),
    (1, "", A + b"\xff", "windows-1252", "default"),
]
for file, typ, data, name, source in SNIFF:
    sniff(file, typ, data, name, source)

with open(os.path.join(HERE, "cases.txt"), "w") as f:
    f.write("# tests/encoding/cases.txt -- written by tests/encoding/gen.py; tests/exec/encoding.c answers each line\n")
    f.write("\n".join(cases) + "\n")
sys.stdout.write("\n".join(expect) + "\n%d cases\n" % len(cases))
