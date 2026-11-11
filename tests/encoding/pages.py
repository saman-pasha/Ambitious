#!/usr/bin/env python3
"""tests/encoding/pages.py -- the fixture pages of the encodings (tests/pages/enc-*.html),
each text encoded by Python's own encoder of its encoding, and the list of what
tests/integ/encoding.sh loads (tests/encoding/pages.txt: a name and a page, a path
under the checkout or a URL). tests/encoding/pages.js records what Chromium makes of
each (tests/pages/NAME.chromium). Run both again only when a page changes:

    python3 tests/encoding/pages.py && node tests/encoding/pages.js

The pages keep clear of the two sequences where Chromium's decoders depart from the
standard (Big5's four pointers of two code points, and EUC-JP's JIS X 0212 lead
after an error); tests/exec/encoding has them against the standard's text."""
import os
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGES = os.path.join(ROOT, "tests", "pages")


def page(head, title, body):
    return "<!DOCTYPE html><html lang=\"x\"><head>%s<title>%s</title></head>\n<body>\n%s\n</body></html>\n" % (head, title, body)


JA = ("<h1>日本語のページ</h1>\n<p>ひらがな、カタカナ、漢字：「東京は晴れ」。半角ｶﾀｶﾅとＡＢＣ（全角）。"
      "記号①②③㈱№℡、髙﨑。円記号ではない \\ と ~。</p>")
FIXTURES = [
    # name, its encoding as Python names it, the page
    ("enc-windows-1252", "cp1252",
     page("", "Café", "<h1>Crème brûlée</h1>\n<p>“Smart quotes” — the euro: €12, "
          "‘single’, naïve façade, Œuvre, ½ ¾, ©®™, Ÿ, "
          "… ‰ †‡ ŠŽšž.</p>")),
    ("enc-windows-1251", "cp1251",
     page("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1251\">", "Привет",
          "<h1>Съешь же ещё этих мягких французских булок</h1>\n<p>«Да, выпей чаю» — сказал он. №5, €, ‰.</p>")),
    ("enc-shift_jis", "cp932", page("<meta charset=\"Shift_JIS\">", "日本語", JA)),
    ("enc-euc-jp", "euc_jp",
     page("<meta charset=\"euc-jp\">", "日本語",
          "<h1>日本語のページ</h1>\n<p>ひらがな、カタカナ、漢字。半角ｶﾀｶﾅ。補助漢字の丂と丄。</p>")),
    ("enc-iso-2022-jp", "iso2022_jp",
     page("<meta charset=\"iso-2022-jp\">", "日本語", "<h1>日本語のページ</h1>\n<p>ひらがな、カタカナ、漢字。ASCII も混ざる。</p>")),
    ("enc-euc-kr", "cp949",
     page("<meta http-equiv=\"content-type\" content=\"text/html; charset=euc-kr\">", "한국어",
          "<h1>한국어 페이지</h1>\n<p>다람쥐 헌 쳇바퀴에 타고파. 확장 완성형의 똠방각하와 똥. 한자 韓國語.</p>")),
    ("enc-gb18030", "gb18030",
     page("<meta charset=\"gb18030\">", "中文",
          "<h1>中文网页</h1>\n<p>简体中文：我能吞下玻璃而不伤身体。四字节：¥ é 😀 𠀀 ㄅ。欧元 €。</p>")),
    ("enc-gbk", "gbk",
     "<?xml version=\"1.0\" encoding=\"GBK\"?>\n" + page("", "中文", "<h1>中文网页</h1>\n<p>汉字与标点：“你好”，世界！</p>")),
    ("enc-big5", "big5hkscs",
     page("<meta charset=\"big5\">", "中文", "<h1>繁體中文網頁</h1>\n<p>我能吞下玻璃而不傷身體。香港字：𧉧 𤊿 㗎 嘅。</p>")),
    ("enc-utf-16le", "utf-16",
     page("", "UTF-16", "<h1>UTF-16, little-endian</h1>\n<p>Latin é ß, Greek αβγ, CJK 日本語, outside the BMP 😀 𝄞.</p>")),
    ("enc-utf-16be", "utf-16-be",
     page("", "UTF-16", "<h1>UTF-16, big-endian</h1>\n<p>Latin é ß, Greek αβγ, CJK 日本語, outside the BMP 😀 𝄞.</p>")),
    ("enc-utf-8-undeclared", "utf-8",
     page("", "UTF-8", "<h1>UTF-8 with nothing to say so</h1>\n<p>café, naïve, 日本語, 😀.</p>")),
    ("enc-replacement", "ascii",
     page("<meta charset=\"iso-2022-kr\">", "replaced", "<h1>ISO-2022-KR is the replacement encoding</h1>\n<p>every byte</p>")),
]

with open(os.path.join(HERE, "pages.txt"), "w") as manifest:
    manifest.write("# tests/encoding/pages.txt -- written by tests/encoding/pages.py: a name, and the page tests/integ/encoding.sh loads\n")
    for name, codec, text in FIXTURES:
        data = text.encode(codec)
        if codec == "utf-16-be":
            data = b"\xfe\xff" + data
        with open(os.path.join(PAGES, name + ".html"), "wb") as f:
            f.write(data)
        manifest.write("%s tests/pages/%s.html\n" % (name, name))
    # bytes that are not UTF-8, among them malformed ones: each becomes U+FFFD
    with open(os.path.join(PAGES, "enc-utf-8-errors.html"), "wb") as f:
        f.write(page("<meta charset=\"utf-8\">", "errors", "<p>a\xffb, cut \xe3\x81, surrogate \xed\xa0\x80, overlong \xc0\xaf, "
                     "fine \xf0\x9f\x98\x80</p>").encode("latin-1"))
    manifest.write("enc-utf-8-errors tests/pages/enc-utf-8-errors.html\n")
    # the transport layer's charset: a data: URL's type (net records it as the resource's type)
    koi = "<!DOCTYPE html><title>КОИ8-Р</title><p>Широкая электрификация южных губерний даст мощный толчок подъёму сельского хозяйства.</p>"
    manifest.write("enc-http-koi8-r data:text/html;charset=KOI8-R,%s\n" % urllib.parse.quote(koi.encode("koi8_r"), safe=""))
