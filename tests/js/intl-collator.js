// Intl.Collator and String.prototype.localeCompare over CLDR's root collation: Latin,
// Greek and Cyrillic words (and a few of other scripts), each sensitivity,
// ignorePunctuation, numeric, caseFirst, usage search, the -u-co, -u-kn and -u-kf
// keywords, Array.prototype.sort with the compare getter, the errors. Expected output:
// node 22 (ICU 78, CLDR 48).
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const words = ["a", "A", "á", "Á", "à", "ä", "b", "B", "z", "Z", "ab", "aB", "Ab", "a b", "a-b", "a_b", "ab.", ".ab", "1", "2", "10", "01", "1a", "a1", "a10", "a2", "é", "e", "E", "ê", "ë", "ß", "ss", "æ", "ae", "ø", "o", "œ", "ﬁ", "fi", "Ω", "ω", "α", "β", "А", "а", "б", "ё", "е", "й", "и", "ї", "",
  " ", "-", "$", "€", "#", "@", "&", "á", "á", "Æ", "þ", "ð", "ı", "i", "İ", "ŉ", "ĳ", "ǆ", "Ǆ", "Ǉ", "ȷ", "l·l", "ll", "L·L", "ŀ", "й", "й", "한", "글", "中", "文", "あ", "ア", "😀", "١", "٢"];
const greek = ["Αθήνα", "άλφα", "αλφα", "Άλφα", "βήτα", "γάμμα", "Δέλτα", "έψιλον", "ζήτα", "ήτα", "θήτα", "ιώτα", "ϊ", "ΐ", "κάππα", "λάμδα", "μι", "νι", "ξι", "όμικρον", "πι", "ρω", "σίγμα", "ς", "ταυ", "ύψιλον", "φι", "χι", "ψι", "ωμέγα", "Ω"];
const cyr = ["яблоко", "Ёлка", "ёж", "ель", "Ель", "жук", "зебра", "ива", "йод", "кот", "лес", "мир", "ночь", "овца", "пёс", "рак", "сок", "тигр", "утка", "фея", "хлеб", "цирк", "чай", "шар", "щука", "ъ", "ы", "ь", "эхо", "юла", "Ґанок", "гора", "єнот", "ігри", "їжак", "ўсё", "ђак", "љубав", "Њива", "ћерка", "џеп"];
const latin = ["résumé", "resume", "Resume", "résume", "rèsume", "coop", "co-op", "Co-op", "cooperate", "naïve", "naive", "Naïve", "façade", "facade", "Zoë", "zoe", "zoo", "Zürich", "zurich", "Ångström", "angstrom", "Æsir", "aesir", "Øre", "ore", "straße", "strasse", "Strasse", "ĳsselmeer", "ijsselmeer", "Łódź", "lodz", "Šimon", "simon", "Čech", "cech", "œuvre", "oeuvre", "x-ray", "xray", "X Ray", "file10", "file2", "file1", "File1", "file 1", "file_1", "file-1", "File10.txt", "file9.txt"];
for (const o of [{}, { sensitivity: "base" }, { sensitivity: "accent" }, { sensitivity: "case" }, { ignorePunctuation: true }, { numeric: true }, { caseFirst: "upper" }, { caseFirst: "lower" },
  { usage: "search" }, { usage: "search", sensitivity: "base" }, { numeric: true, sensitivity: "base", ignorePunctuation: true }, { caseFirst: "upper", sensitivity: "case" }]) {
  const c = new Intl.Collator("en", o);
  p(o, c.resolvedOptions());
  for (const list of [words, greek, cyr, latin]) p([...list].sort(c.compare).join(","));
  p(["a,A", "a,á", "A,á", "ab,aB", "a b,ab", "a-b,ab", "1,01", "10,9", "a10,a9", "ß,ss", "æ,ae", "á,á", "ı,i", "e,E", "ε,Ε", "ё,е", "l·l,ll", "й,й", ",a", "a,", "x2,x10", "001,1"]
    .map(x => { const [a, b] = x.split(","); return c.compare(a, b); }).join(","));
}
for (const l of ["de", "fr", "es", "pl", "ru", "uk", "el", "ja", "zh", "ko", "ar", "he", "th", "es-u-co-trad", "en-u-kn", "en-u-kn-false", "en-u-kf-upper", "en-u-kf-lower", "en-u-co-emoji", "xx", "en-US", "pt-BR", "it", "nl"]) {
  const c = new Intl.Collator(l);
  p(l, c.resolvedOptions(), ["a", "ä", "z", "n", "ñ", "c", "ch", "h", "ł", "l", "ı", "i", "s", "ş", "a b", "ab", "a-b"].sort(c.compare).join(","));
}
for (const [l, o] of [["en-u-kn", { numeric: false }], ["en-u-kn-false", { numeric: true }], ["en-u-kf-upper", { caseFirst: "lower" }], ["en-u-kf-upper", { caseFirst: "upper" }], ["de-u-co-phonebk", { collation: "eor" }],
  ["de", { collation: "phonebk" }], ["en", { collation: "phonebk" }], ["de-u-co-phonebk-kn", {}], ["da", {}], ["da", { caseFirst: "false" }], ["th", { ignorePunctuation: false }]]) {
  p(l, o, new Intl.Collator(l, o).resolvedOptions());
}
p("a".localeCompare("b"), "b".localeCompare("a"), "a".localeCompare("a"), "a".localeCompare("A"), "ä".localeCompare("z", "de"), "a".localeCompare("á", "en", { sensitivity: "base" }),
  "10".localeCompare("9", undefined, { numeric: true }), "".localeCompare(""), "a".localeCompare(), String.prototype.localeCompare.length, "résumé".localeCompare("resume", "fr"),
  ["b", "a", "C", "á", "A"].sort((x, y) => x.localeCompare(y)).join(""), ["b", "a", "C", "á", "A"].sort(new Intl.Collator("en", { caseFirst: "upper" }).compare).join(""),
  String.prototype.localeCompare.call(5, 10), "x".localeCompare({ toString() { return "y"; } }));
for (const fn of [() => new Intl.Collator("en", { usage: "x" }), () => new Intl.Collator("en", { sensitivity: "x" }), () => new Intl.Collator("en", { caseFirst: "x" }), () => new Intl.Collator("en", { collation: "a!" }),
  () => Intl.Collator.prototype.compare, () => Object.getOwnPropertyDescriptor(Intl.Collator.prototype, "compare").get.call(5), () => Intl.Collator.prototype.resolvedOptions.call({}),
  () => Object.getOwnPropertyNames(Intl.Collator.prototype), () => Intl.Collator.length, () => Intl.Collator("en") instanceof Intl.Collator, () => new Intl.Collator().compare.name, () => new Intl.Collator().compare.length,
  () => { const c = new Intl.Collator(); return c.compare === c.compare; }, () => Intl.Collator.supportedLocalesOf(["en", "de", "xx", "zh-Hant"]), () => new Intl.Collator("en", null),
  () => String.prototype.localeCompare.call(null, "a"), () => "a".localeCompare("b", "xx_YY"), () => Object.prototype.toString.call(new Intl.Collator())]) {
  try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
}
const log = [];
new Intl.Collator("en", new Proxy({}, { get(t, k) { log.push(String(k)); return undefined; } }));
p(log);
class MyColl extends Intl.Collator { constructor() { super("de", { numeric: true }); } }
const m = new MyColl(); p(["x10", "x9"].sort(m.compare), m instanceof MyColl, m.resolvedOptions().locale);
