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
    # a meta past the first 1024 bytes, which the prescan does not reach: the tree builder
    # meets it while the encoding is tentative, and the parse starts again in its encoding
    ("enc-late-meta", "cp1251",
     page("<style>/* " + "a style sheet that is long enough to put the meta after the first 1024 bytes. " * 14 + "*/</style>"
          "<meta charset=\"windows-1251\">", "Поздно",
          "<p>Кодировка объявлена поздно, но документ всё равно читается в ней.</p>")),
]
# nothing declares the encoding: Chromium's detector guesses it from the bytes. A data:
# URL, as a page served over HTTP, has no UTF-8 guessed (a file has)
DETECT = [
    ("enc-detect-shift_jis", "shift_jis", "日本語のテキストです。これは文字コードの自動判定のための試験用の文書であり、ひらがなとカタカナと漢字を含みます。東京都千代田区。"),
    ("enc-detect-euc-jp", "euc_jp", "日本語のテキストです。これは文字コードの自動判定のための試験用の文書であり、ひらがなとカタカナと漢字を含みます。東京都千代田区。"),
    ("enc-detect-euc-kr", "euc_kr", "한국어 텍스트입니다. 이것은 문자 인코딩 자동 감지를 위한 시험용 문서이며 한글과 숫자를 포함합니다. 서울특별시 종로구."),
    ("enc-detect-gbk", "gbk", "这是中文文本。这是一个用于字符编码自动检测的测试文档，其中包含汉字和标点符号。北京市海淀区中关村大街。"),
    ("enc-detect-big5", "big5", "這是中文文本。這是一個用於字元編碼自動偵測的測試文件，其中包含漢字和標點符號。臺北市大安區。"),
    ("enc-detect-windows-1251", "cp1251", "Это проверка автоматического определения кодировки документа. Москва, улица Тверская, дом семь."),
    ("enc-detect-koi8-r", "koi8_r", "Это проверка автоматического определения кодировки документа. Москва, улица Тверская, дом семь."),
    ("enc-detect-windows-1252", "cp1252", "Grüße aus Köln, café crème brûlée, naïve façade, déjà vu, señor."),
    ("enc-detect-windows-1253", "cp1253", "Αυτός είναι ένας έλεγχος της αυτόματης αναγνώρισης της κωδικοποίησης του εγγράφου. Αθήνα, οδός Πανεπιστημίου."),
    ("enc-detect-windows-1255", "cp1255", "זוהי בדיקה של זיהוי אוטומטי של קידוד המסמך. ירושלים, רחוב יפו, בית מספר שבע."),
    ("enc-detect-windows-1256", "cp1256", "هذا اختبار للكشف التلقائي عن ترميز المستند. القاهرة، شارع التحرير، المنزل رقم سبعة."),
    ("enc-detect-windows-874", "cp874", "นี่คือการทดสอบการตรวจหาการเข้ารหัสของเอกสารโดยอัตโนมัติ กรุงเทพมหานคร ถนนสุขุมวิท"),
    ("enc-detect-windows-1250", "cp1250", "Rząd ogłosił w środę pakiet działań mających zmniejszyć zużycie energii zimą. Szkoły zostaną ponownie otwarte po świętach."),
    ("enc-detect-iso-8859-2", "iso8859_2", "Zażółć gęślą jaźń. To jest sprawdzenie automatycznego rozpoznawania kodowania dokumentu. Warszawa, ulica Marszałkowska."),
    ("enc-detect-windows-1254", "cp1254", "Hükümet çarşamba günü kışın enerji tüketimini azaltmaya yönelik bir dizi önlem açıkladı. Okullar bayramdan sonra yeniden açılacak."),
    ("enc-detect-windows-1257", "cp1257", "Vyriausybė trečiadienį paskelbė priemonių rinkinį, kuriuo siekiama žiemą sumažinti energijos suvartojimą."),
    ("enc-detect-iso-8859-5", "iso8859_5", "Правительство в среду объявило о ряде мер, направленных на снижение потребления энергии зимой."),
    ("enc-detect-ibm866", "cp866", "Правительство в среду объявило о ряде мер, направленных на снижение потребления энергии зимой."),
    ("enc-detect-koi8-u", "koi8_u", "Уряд у середу оголосив низку заходів, спрямованих на зменшення споживання енергії взимку. Їжак і ґанок."),
    ("enc-detect-greek-tonos", "cp1253", "Άρτα, Έδεσσα, Ήπειρος, Ίος, Όλυμπος, Ύδρα, Ώρα. Αυτός είναι ένας έλεγχος της αυτόματης αναγνώρισης."),
    ("enc-detect-iso-8859-6", "iso8859_6", "أعلنت الحكومة يوم الأربعاء عن مجموعة من الإجراءات الرامية إلى خفض استهلاك الطاقة خلال فصل الشتاء."),
    ("enc-detect-capitals-koi8-r", "koi8_r", "ЭТО ПРОВЕРКА АВТОМАТИЧЕСКОГО ОПРЕДЕЛЕНИЯ КОДИРОВКИ ДОКУМЕНТА."),
    ("enc-detect-kanji-euc-jp", "euc_jp", "東京都千代田区"),
    ("enc-detect-punctuation", "cp1252", "It’s a “test” — nothing more…"),
    ("enc-detect-utf-8", "utf-8", "Grüße aus Köln, café crème brûlée. Это проверка кодировки. 日本語のテキストです。"),
]

# a meta past the first 1024 bytes: Blink honours it while the document is in its head
# section (KOI8-R here over windows-1251's bytes, so that the guess would not name it), not
# once a tag no head holds has started the body
PAD = "<p>" + "Plain text in the body before any declaration. " * 30 + "</p>"
RU = "<p>Кодировка объявлена поздно, но документ всё равно читается в ней.</p>"
LATE = [
    ("enc-late-head", "<!DOCTYPE html><title>t</title><script>/* " + "x" * 3000 + " */</script><meta charset=\"koi8-r\"><body>" + RU),
    ("enc-late-text", "<!DOCTYPE html><title>t</title>Some text " + "y" * 1100 + "<meta charset=\"koi8-r\">" + RU),
    ("enc-late-body", "<!DOCTYPE html><title>t</title><body>" + PAD + "<meta charset=\"koi8-r\">" + RU),
    ("enc-late-comment", "<!DOCTYPE html><title>t</title><body><div><span></span><!-- " + "z" * 1100 + " --><meta charset=\"koi8-r\">" + RU),
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
    for name, codec, text in DETECT:
        body = page("", "t", "<p>" + text + "</p>").encode(codec)
        manifest.write("%s data:text/html,%s\n" % (name, urllib.parse.quote(body, safe="")))
    for name, text in LATE:
        manifest.write("%s data:text/html,%s\n" % (name, urllib.parse.quote(text.encode("cp1251"), safe="")))
