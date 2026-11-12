// tests/js/intl-locale.js -- Intl's locale machinery, each line against V8's (intl-locale.expect):
// language tags canonicalized as UTS #35 and ICU do it (case, order, CLDR's language, script,
// region and variant aliases, the Unicode extension's keys and values, the transformed
// extension), the errors of a malformed tag, getCanonicalLocales over lists, and Intl.Locale:
// its options, accessors, maximize and minimize by the likely subtags, its regions' facts.
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
p(typeof Intl, Object.prototype.toString.call(Intl), Intl.getCanonicalLocales("EN-us"), Intl.getCanonicalLocales.length);

const tags = ["EN-us", "en-latn-us", "zh-hant-tw", "de-u-co-phonebook", "de-u-co-phonebk", "en-u-ca-ethiopic-amete-alem",
  "en-u-ca-islamicc", "en-u-ks-primary", "en-u-ms-imperial", "en-u-kb-yes", "en-u-kb-true", "en-u-kn-false", "en-u-kn-true",
  "en-u-tz-aqams", "en-u-tz-cnckg", "sgn-GR", "iw-IL", "in", "ji", "jw", "mo", "sh", "sh-Cyrl", "cmn", "cmn-Hans", "zh-cmn-Hans",
  "aar", "hy-arevela", "hy-arevmda", "art-lojban", "zh-guoyu", "zh-hakka", "en-x-private", "x-private", "en-t-ru-Cyrl",
  "EN-u-CA-gregory-NU-latn", "en-u-nu-latn-ca-gregory", "en-z-zz-u-ca-gregory", "en-b-bb-a-aa", "en-variant1-abcde",
  "en-polytoni", "el-polytoni", "en-heploc", "de-1901-1996", "sr-Latn-RS", "und", "und-US", "und-Latn", "en-SU", "ru-SU",
  "hy-SU", "und-SU", "uz-SU", "en-DD", "en-YU", "en-BU", "en-830", "en-958", "en-016", "en-554", "no-bok", "no-bokmal", "no-nyn",
  "i-klingon", "en-GB-oed", "tlh", "EN-U-CA-GREGORIAN", "de-1901-1901", "en-u-ca-gregory-u-nu-latn", "en-a-aa-a-bb", "", "en-",
  "-en", "en--US", "en_US", "e", "toolonglang", "abcde", "abcdefghi", "en-US-x", "en-0abc", "en-1234", "en-12345", "en-a1b2c",
  "en-u", "en-u-ca", "en-u-ca-gregory-ca-buddhist", "en-u-aa-bbb-cc", "en-u-attr1-ca-gregory", "en-u-attr2-attr1",
  "en-u-attr1-attr1", "en-t-en-us", "en-t-h0-hybrid", "en-t-a1b-c", "en-u-co-yes", "en-u-ca-yes", "ar-u-nu-arab-ca-islamicc",
  "en-u-dx-thai", "en-u-va-posix", "en-u-rg-ukzzzz", "en-US-POSIX", "sr-cyrl-ba", "az-Arab-IR", "ın", "i-default",
  "en-Qaai", "en-latn-us-valencia", "en-scouse-fonipa", "hy-arevela-AM", "EN-LATN", "en-us-u-ca-gregory-x-abc-def",
  "de-DE-u-co-phonebk-ka-shifted", "und-Cyrl-RU", "und-hans", "zh-yue", "yue", "en-t-iw", "en-t-m0-ungegn-h0-hybrid",
  "fr-x-a-b-c", "en-US-u-kf-upper-co-emoji-kn", "pt-br", "es-419", "en-001", "zh-min-nan", "cel-gaulish", "und-aaland"];
for (const x of tags) {
  try { p(JSON.stringify(x), Intl.getCanonicalLocales(x)); } catch (e) { p(JSON.stringify(x), e.constructor.name, e.message); }
}
for (const bad of [[1], null, [undefined], [{}], [true]]) {
  try { p(Intl.getCanonicalLocales(bad)); } catch (e) { p(e.constructor.name, e.message); }
}
p(Intl.getCanonicalLocales(["en-us", "EN-US", "de", "en-US"]), Intl.getCanonicalLocales(), Intl.getCanonicalLocales([]),
  Intl.getCanonicalLocales({ length: 2, 0: "fr", 1: "FR-fr" }), Intl.getCanonicalLocales({ length: 3, 1: "it" }),
  Intl.getCanonicalLocales(["de", new Intl.Locale("en-u-ca-buddhist")]), Intl.getCanonicalLocales(new Intl.Locale("ja-JP")));
p(Intl.getCanonicalLocales({ length: 1, 0: { toString() { return "KO-kr"; } } }));

// Intl.Locale: what it refuses
const tries = [() => new Intl.Locale(), () => new Intl.Locale(5), () => new Intl.Locale(""), () => Intl.Locale("en"),
  () => new Intl.Locale("en", { language: "x1" }), () => new Intl.Locale("en", { script: "Latn1" }),
  () => new Intl.Locale("en", { region: "USA" }), () => new Intl.Locale("en", { calendar: "ab" }),
  () => new Intl.Locale("en", { hourCycle: "h13" }), () => new Intl.Locale("en", { caseFirst: "up" }),
  () => new Intl.Locale("en", { numeric: "x" }), () => new Intl.Locale(null), () => new Intl.Locale("en_US"),
  () => new Intl.Locale("en", { numberingSystem: "toolongvalue" }), () => new Intl.Locale("en", { collation: "a-b" }),
  () => new Intl.Locale({ toString() { return "fr-ca"; } }), () => new Intl.Locale("en", 5), () => new Intl.Locale("en", null)];
for (const f of tries) { try { p("ok", f().toString()); } catch (e) { p(e.constructor.name, e.message); } }
for (const f of [() => Intl.Locale.prototype.toString.call({}), () => Intl.Locale.prototype.maximize.call(1),
                 () => Object.getOwnPropertyDescriptor(Intl.Locale.prototype, "baseName").get.call({}),
                 () => Intl.Locale.prototype.minimize.call(new Intl.Locale("en").maximize)]) {
  try { f(); } catch (e) { p(e.constructor.name, e.message); }
}

// its options and accessors
const l = new Intl.Locale("en-latn-us-u-ca-gregory-co-phonebk-hc-h12-kf-upper-kn-nu-latn", { region: "GB", numeric: false, calendar: "buddhist" });
p(l.toString(), l.baseName, l.language, l.script, l.region, l.calendar, l.collation, l.hourCycle, l.caseFirst, l.numeric, l.numberingSystem);
const e = new Intl.Locale("en");
p(e.calendar, e.collation, e.hourCycle, e.caseFirst, e.numeric, e.numberingSystem, e.script, e.region, e.baseName);
p(new Intl.Locale("EN-latn-us-u-ca-gregory", { language: "iw" }).toString(), new Intl.Locale("en", { script: "cyrl", region: "su" }).toString(),
  new Intl.Locale(new Intl.Locale("de-u-co-phonebk"), { collation: "emoji" }).toString(), new Intl.Locale("en", { numeric: true }).toString(),
  new Intl.Locale("en", { numeric: 0 }).toString(), new Intl.Locale("en-u-kn").numeric, new Intl.Locale("en-u-kn-true").numeric,
  new Intl.Locale("ja", { calendar: "JAPANESE", hourCycle: "h11", caseFirst: "lower", numberingSystem: "jpan" }).toString(),
  new Intl.Locale("sh", { region: "YU" }).toString(), new Intl.Locale("und", { language: "zh", script: "Hant" }).toString());
p(Intl.Locale.length, Intl.Locale.name, typeof Object.getOwnPropertyDescriptor(Intl.Locale.prototype, "baseName").get,
  Object.getOwnPropertyDescriptor(Intl.Locale.prototype, "baseName").get.name, Intl.Locale.prototype[Symbol.toStringTag],
  Object.prototype.toString.call(new Intl.Locale("en")), JSON.stringify(new Intl.Locale("en")), String(new Intl.Locale("en-us")),
  Object.keys(new Intl.Locale("en")), new Intl.Locale("en") instanceof Intl.Locale);
class X extends Intl.Locale { constructor() { super("fr-CA"); } }
const x = new X(); p(x.toString(), x instanceof Intl.Locale, x instanceof X, x.maximize().toString(), x.maximize() instanceof X);

// maximize and minimize
for (const t of ["zh", "zh-TW", "zh-Hant", "zh-Hant-TW", "zh-HK", "zh-Hans-HK", "en", "en-GB", "en-Latn-US", "en-Latn-US-u-ca-gregory",
                 "und", "und-Cyrl", "und-US", "und-Arab", "und-Hant", "und-CN", "sr", "sr-ME", "sr-Latn", "sr-Latn-RS", "de-CH",
                 "pt", "pt-PT", "pt-Latn-PT", "zh-Hans", "ar", "ar-SA", "he", "iw", "ja", "ja-JP", "ko", "hi", "fa", "fa-AF",
                 "ru", "uk", "el", "th", "vi", "id", "az", "az-IR", "uz", "uz-AF", "ms", "mn", "mn-CN", "pa", "pa-PK", "es-419",
                 "en-001", "fr-x-foo", "de-1996", "xx", "xx-Latn", "en-Cyrl", "en-Zzzz-ZZ"]) {
  const L = new Intl.Locale(t);
  p(t, L.maximize().toString(), L.minimize().toString(), L.maximize().minimize().toString());
}

// the facts of a locale's region
for (const t of ["en", "de", "fr", "es", "it", "pt", "nl", "ru", "pl", "ja", "zh", "ko", "hi", "ar", "he", "tr", "sv", "da", "nb", "no",
                 "fi", "cs", "uk", "el", "hu", "ro", "id", "vi", "th", "fa", "zh-TW", "ar-SA", "ar-EG", "fa-IR", "th-TH", "en-IN",
                 "hi-IN", "ja-JP", "he-IL", "en-GB", "en-US", "de-CH", "sr-Latn", "zh-Hant", "en-u-ca-buddhist-hc-h11-co-emoji-nu-arab"]) {
  const L = new Intl.Locale(t);
  p(t, L.collations, L.calendars, L.hourCycles, L.numberingSystems, L.textInfo, L.weekInfo, L.timeZones);
}
p(new Intl.Locale("de-DE").timeZones, new Intl.Locale("en-US").timeZones, new Intl.Locale("ru-RU").timeZones.length,
  new Intl.Locale("ps-PS").timeZones, new Intl.Locale("he-IL").timeZones, new Intl.Locale("en-AQ").timeZones);
