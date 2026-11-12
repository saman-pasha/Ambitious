// Intl.PluralRules, Intl.ListFormat, Intl.RelativeTimeFormat, Intl.DisplayNames and
// Intl.Segmenter: their options and resolvedOptions, plural categories of thirty
// languages, lists with Spanish's and Hebrew's conjunction changes, relative times by
// unit, style and numeric, display names of languages (dialects), regions, scripts,
// currencies, calendars and date fields, grapheme, word and sentence segments (UAX #29),
// the errors. Expected output: node 22 (ICU 78, CLDR 48, Unicode 17).
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
{
  // PluralRules
  p(new Intl.PluralRules("en").resolvedOptions(), new Intl.PluralRules("ar", { type: "ordinal", minimumFractionDigits: 2 }).resolvedOptions(),
    new Intl.PluralRules("ru", { maximumSignificantDigits: 3 }).resolvedOptions(), new Intl.PluralRules("xx").resolvedOptions());
  p(new Intl.PluralRules("en").select(1), new Intl.PluralRules("en").select("1"), new Intl.PluralRules("en", { minimumFractionDigits: 1 }).select(1),
    new Intl.PluralRules("en").selectRange(1, 5), new Intl.PluralRules("fr").selectRange(0, 1), new Intl.PluralRules("en").select(-0), new Intl.PluralRules("en").select(NaN), new Intl.PluralRules("en").select(Infinity));
  for (const fn of [() => Intl.PluralRules(), () => new Intl.PluralRules("en", { type: "x" }), () => new Intl.PluralRules("en").selectRange(1), () => new Intl.PluralRules("en").selectRange(NaN, 1),
    () => new Intl.PluralRules("en").selectRange(5, 1), () => Intl.PluralRules.prototype.select.call({}, 1), () => new Intl.PluralRules("en").select(1n), () => Object.getOwnPropertyNames(Intl.PluralRules.prototype),
    () => Intl.PluralRules.supportedLocalesOf(["en", "xx", "fr-CA", "zh-Hant"]), () => new Intl.PluralRules("en", null), () => Intl.PluralRules.length]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
  const log = [];
  new Intl.PluralRules("en", new Proxy({}, { get(t, k) { log.push(String(k)); return undefined; } }));
  p(log);
  // ListFormat
  const items = [[], ["a"], ["a", "b"], ["a", "b", "c"], ["a", "b", "c", "d"]];
  for (const l of ["en", "es", "he", "de", "ja", "ar", "fr", "zh"]) {
    for (const type of ["conjunction", "disjunction", "unit"]) for (const style of ["long", "short", "narrow"]) {
      const f = new Intl.ListFormat(l, { type, style });
      p(l, type, style, items.map(x => f.format(x)).join(" | "));
    }
  }
  p(new Intl.ListFormat("es").format(["agua", "hielo"]), new Intl.ListFormat("es").format(["uno", "Iglesia"]), new Intl.ListFormat("es", { type: "disjunction" }).format(["siete", "ocho"]),
    new Intl.ListFormat("es", { type: "disjunction" }).format(["uno", "11"]), new Intl.ListFormat("es", { type: "disjunction" }).format(["uno", "8"]), new Intl.ListFormat("he").format(["a", "b"]), new Intl.ListFormat("he").format(["אב", "גד"]),
    new Intl.ListFormat("en").formatToParts(["x", "y", "z"]), new Intl.ListFormat().resolvedOptions(), new Intl.ListFormat("de", { type: "unit", style: "narrow" }).resolvedOptions());
  for (const fn of [() => new Intl.ListFormat("en").format(["a", 1]), () => new Intl.ListFormat("en").format("abc"), () => new Intl.ListFormat("en").format(new Set(["x", "y"])), () => new Intl.ListFormat("en").format(),
    () => new Intl.ListFormat("en", { type: "x" }), () => new Intl.ListFormat("en", { style: "x" }), () => Intl.ListFormat(), () => new Intl.ListFormat("en", 5), () => Object.getOwnPropertyNames(Intl.ListFormat.prototype),
    () => Intl.ListFormat.prototype.format.call({}, [])]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
  // RelativeTimeFormat
  const units = ["year", "quarter", "month", "week", "day", "hour", "minute", "second", "years", "days", "seconds"];
  const vals = [-2, -1, -0, 0, 1, 2, 1.5, -3.25, 1000, 1e6];
  for (const l of ["en", "de", "fr", "ja", "ar", "ru", "es", "zh", "hi"]) {
    for (const numeric of ["always", "auto"]) for (const style of ["long", "short", "narrow"]) {
      const f = new Intl.RelativeTimeFormat(l, { numeric, style });
      p(l, numeric, style, units.map(u => vals.map(v => f.format(v, u)).join(",")).join(" | "));
    }
  }
  p(new Intl.RelativeTimeFormat("en").formatToParts(-1234.5, "day"), new Intl.RelativeTimeFormat("en", { numeric: "auto" }).formatToParts(-1, "day"),
    new Intl.RelativeTimeFormat("ar").formatToParts(3, "hours"), new Intl.RelativeTimeFormat("en").resolvedOptions(), new Intl.RelativeTimeFormat("ar-u-nu-arab", { style: "narrow" }).resolvedOptions(),
    new Intl.RelativeTimeFormat("en", { numberingSystem: "deva" }).format(12, "day"));
  for (const fn of [() => new Intl.RelativeTimeFormat("en").format(1, "decade"), () => new Intl.RelativeTimeFormat("en").format(NaN, "day"), () => new Intl.RelativeTimeFormat("en").format(Infinity, "day"),
    () => new Intl.RelativeTimeFormat("en").format("3", "day"), () => new Intl.RelativeTimeFormat("en").format(1), () => new Intl.RelativeTimeFormat("en", { style: "x" }), () => new Intl.RelativeTimeFormat("en", { numeric: "x" }),
    () => Intl.RelativeTimeFormat(), () => Object.getOwnPropertyNames(Intl.RelativeTimeFormat.prototype), () => Intl.RelativeTimeFormat.prototype.format.call({}, 1, "day"),
    () => new Intl.RelativeTimeFormat("en", { numberingSystem: "x!" })]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
  const log2 = [];
  new Intl.RelativeTimeFormat("en", new Proxy({}, { get(t, k) { log2.push(String(k)); return undefined; } }));
  p(log2);
  // Segmenter
  const texts = ["Hello, world! How are you?", "é 👨‍👩‍👧 🇺🇸🇫🇷 ok", "Mr. Smith went. He said \"Hi.\" Then left!", "can't stop 3.14 1,000 a_b", "Привет, мир. Как дела?", "שלום עולם", "\r\n\nक्ष"];
  for (const g of ["grapheme", "word", "sentence"]) {
    const s = new Intl.Segmenter("en", { granularity: g });
    for (const t of texts) p(g, [...s.segment(t)].map(x => g === "word" ? [x.segment, x.index, x.isWordLike] : [x.segment, x.index]));
  }
  const seg = new Intl.Segmenter("en", { granularity: "word" }).segment("ab cd");
  p(seg.containing(0), seg.containing(2), seg.containing(4), seg.containing(5), seg.containing(-1), new Intl.Segmenter().resolvedOptions(), Object.getOwnPropertyNames(Intl.Segmenter.prototype),
    typeof seg[Symbol.iterator], Object.prototype.toString.call(seg[Symbol.iterator]()), seg[Symbol.iterator]().next());
}
{
  const pls = ["en", "fr", "de", "ru", "pl", "cs", "ar", "he", "ja", "zh", "ko", "hi", "pt", "pt-PT", "es", "it", "uk", "ro", "lv", "sl", "ga", "cy", "br", "mt", "lt", "fa", "tr", "da", "is", "id"];
  const nums = [0, 1, 2, 3, 4, 5, 7, 10, 11, 12, 14, 19, 20, 21, 22, 25, 100, 101, 102, 111, 1000, 1000000, 0.5, 1.5, 2.25, 1.0001, -1, -2];
  for (const l of pls) {
    for (const type of ["cardinal", "ordinal"]) {
      const f = new Intl.PluralRules(l, { type });
      p(l, type, f.resolvedOptions().locale, f.resolvedOptions().pluralCategories, nums.map(n => f.select(n)).join(","));
    }
    const g = new Intl.PluralRules(l, { minimumFractionDigits: 1 });
    p(l, "mfd1", nums.map(n => g.select(n)).join(","), [[0, 1], [1, 2], [1, 5], [2, 5], [0, 2], [5, 100], [0.5, 1], [1, 1.5]].map(([a, b]) => new Intl.PluralRules(l).selectRange(a, b)).join(","));
  }
  for (const o of [{ maximumSignificantDigits: 1 }, { maximumFractionDigits: 0 }, { minimumIntegerDigits: 3 }, { roundingMode: "floor", maximumFractionDigits: 0 }, { roundingIncrement: 5, maximumFractionDigits: 1, minimumFractionDigits: 1 },
    { roundingPriority: "lessPrecision", maximumSignificantDigits: 2, maximumFractionDigits: 3 }, { trailingZeroDisplay: "stripIfInteger", minimumFractionDigits: 2 }]) {
    const f = new Intl.PluralRules("en", o);
    p(o, f.resolvedOptions(), [1, 1.04, 1.5, 0.96, 1.2].map(n => f.select(n)).join(","));
  }
  // RelativeTimeFormat
  for (const l of ["en", "en-GB", "de", "fr", "es", "it", "pt", "nl", "ru", "pl", "cs", "ja", "zh", "ko", "ar", "he", "hi", "tr", "sv", "fi", "uk", "el", "hu", "id", "vi", "th", "fa", "da", "nb", "ro"]) {
    for (const style of ["long", "short", "narrow"]) {
      const f = new Intl.RelativeTimeFormat(l, { numeric: "auto", style });
      const g = new Intl.RelativeTimeFormat(l, { style });
      p(l, style, ["year", "quarter", "month", "week", "day", "hour", "minute", "second"].map(u => [-2, -1, 0, 1, 2, 3, -5, 21, 0.995].map(v => f.format(v, u) + "/" + g.format(v, u)).join(",")).join(" | "));
    }
  }
  p(new Intl.RelativeTimeFormat("en", { numeric: "auto" }).formatToParts(0, "second"), new Intl.RelativeTimeFormat("de").formatToParts(-1.5, "hour"), new Intl.RelativeTimeFormat("ja").formatToParts(1e6, "year"),
    new Intl.RelativeTimeFormat("fr-CA", { style: "short" }).formatToParts(-2, "months"), new Intl.RelativeTimeFormat("ar-EG").format(12, "day"), new Intl.RelativeTimeFormat("hi-u-nu-deva").format(-3, "week"),
    new Intl.RelativeTimeFormat("xx").resolvedOptions(), new Intl.RelativeTimeFormat("en-u-nu-arab", { numberingSystem: "latn" }).resolvedOptions(), new Intl.RelativeTimeFormat("en-u-nu-arab").resolvedOptions());
  // ListFormat
  for (const l of ["en", "en-GB", "es", "es-MX", "pt", "it", "nl", "ru", "pl", "cs", "ja", "ko", "hi", "he", "tr", "sv", "fi", "uk", "el", "hu", "id", "vi", "th", "fa", "da", "nb", "ro", "de-CH", "fr-CA", "zh-TW"]) {
    for (const type of ["conjunction", "disjunction", "unit"]) for (const style of ["long", "short", "narrow"]) {
      const f = new Intl.ListFormat(l, { type, style });
      p(l, type, style, [["x", "y"], ["x", "y", "z"], ["x", "y", "z", "w", "v"]].map(x => f.format(x)).join(" | "));
    }
  }
  p(new Intl.ListFormat("es").format(["a", "hielo"]), new Intl.ListFormat("es").format(["a", "hierba"]), new Intl.ListFormat("es").format(["a", "Hispania"]), new Intl.ListFormat("es").format(["a", "b", "isla"]),
    new Intl.ListFormat("es", { type: "disjunction" }).format(["a", "hora"]), new Intl.ListFormat("es", { type: "disjunction" }).format(["a", "11 años"]), new Intl.ListFormat("es", { type: "disjunction" }).format(["a", "111"]),
    new Intl.ListFormat("es", { type: "disjunction" }).format(["a", "b", "otro"]), new Intl.ListFormat("he").format(["א", "ב", "1"]), new Intl.ListFormat("he").format(["א", "ב", "ג"]),
    new Intl.ListFormat("en").formatToParts(["a", "b"]), new Intl.ListFormat("ja", { type: "unit", style: "narrow" }).formatToParts(["x", "y", "z"]), new Intl.ListFormat("en").format(["", ""]));
  // DisplayNames
  const langs = ["en", "de", "fr", "es", "it", "pt", "nl", "ru", "pl", "ja", "zh", "ko", "hi", "ar", "he", "tr", "sv", "fi", "cs", "uk", "el", "hu", "id", "vi", "th", "fa", "da", "nb", "ro", "en-GB", "es-419", "pt-PT", "zh-Hant", "fr-CA"];
  for (const l of langs) {
    const ln = new Intl.DisplayNames(l, { type: "language" }), rn = new Intl.DisplayNames(l, { type: "region" }), sn = new Intl.DisplayNames(l, { type: "script" });
    const cn = new Intl.DisplayNames(l, { type: "currency" }), kn = new Intl.DisplayNames(l, { type: "calendar" }), fn = new Intl.DisplayNames(l, { type: "dateTimeField", style: "short" });
    p(l, ["de", "en-US", "fr-CH", "es-MX", "pt-BR", "zh-Hans-CN", "ja", "ko-KR", "ar-EG", "nl-BE", "sr-Cyrl-BA", "yue", "haw", "es-ES", "en-AU", "ro-MD", "sw-CD", "hi-Latn", "zgh"].map(x => ln.of(x)).join("|"));
    p(l, ["JP", "CN", "TW", "KR", "IN", "BR", "MX", "ES", "FR", "IT", "RU", "UA", "TR", "IR", "EG", "SA", "IL", "AQ", "CI", "MK", "142", "019", "EZ", "QO"].map(x => rn.of(x)).join("|"));
    p(l, ["Grek", "Hebr", "Deva", "Thai", "Jpan", "Kore", "Hira", "Kana", "Hang", "Brai"].map(x => sn.of(x)).join("|"), ["GBP", "CHF", "INR", "CNY", "RUB", "BRL", "KRW", "XAU", "XXX"].map(x => cn.of(x)).join("|"),
      ["chinese", "hebrew", "islamic", "persian", "indian", "coptic", "dangi"].map(x => kn.of(x)).join("|"), ["era", "year", "weekOfYear", "dayPeriod", "timeZoneName"].map(x => fn.of(x)).join("|"));
  }
  p(new Intl.DisplayNames("en", { type: "language", style: "short" }).of("en-Latn-GB"), new Intl.DisplayNames("en", { type: "language", languageDisplay: "standard", style: "short" }).of("en-GB"),
    new Intl.DisplayNames("en", { type: "language" }).of("en-US-posix"), new Intl.DisplayNames("ja", { type: "language" }).of("zh-Hant-HK"), new Intl.DisplayNames("en", { type: "language", fallback: "none" }).of("xx-GB"),
    new Intl.DisplayNames("en", { type: "language", fallback: "none" }).of("en-ZY"), new Intl.DisplayNames("en", { type: "language", fallback: "none" }).of("en-Zyxw"), new Intl.DisplayNames("de", { type: "region", style: "narrow" }).of("GB"),
    new Intl.DisplayNames("fr", { type: "script", fallback: "none" }).of("Zyxw"), new Intl.DisplayNames("en", { type: "region" }).of("YU"), new Intl.DisplayNames("en", { type: "language" }).of("mo"), new Intl.DisplayNames("en", { type: "language" }).of("tl"));
  // Segmenter
  const texts = ["क्षत्रिय नमस्ते", "👍🏽 🏳️‍🌈 👩🏻‍💻 🇯🇵🇰🇷🇨🇳", "한국어 텍스트", "ä́b‍c", "It's 9:30 a.m. now... OK? Yes!", "e.g. this one. And “that.” Next",
    "Version 1.2.3, cost $4.50 (approx.)", "naïve café — résumé", "line1\nline2\r\nline3 para", "\"Quoted.\" (Paren.) Next one.", "שלום, עולם! מה נשמע?", "مرحبا بالعالم. كيف حالك؟", "x  　 y", "A.B.C. d", "3.5x 2,000.75 -7 +8", "a'b a\"b", ""];
  for (const g of ["grapheme", "word", "sentence"]) {
    for (const l of ["en", "ja"]) {
      const s = new Intl.Segmenter(l, { granularity: g });
      for (const t of texts) p(g, l, [...s.segment(t)].map(x => g === "word" ? x.segment + (x.isWordLike ? "*" : "") : x.segment).join("|"));
    }
  }
  const sg = new Intl.Segmenter("en", { granularity: "word" }).segment("Hello big world");
  p([0, 4, 5, 6, 9, 14, 15, 1.7, "3", NaN, -0.5, Infinity].map(i => JSON.stringify(sg.containing(i))));
  const it = sg[Symbol.iterator](); p(it.next(), it.next(), [...it].length, it.next(), typeof Object.getPrototypeOf(Object.getPrototypeOf(it))[Symbol.iterator], Object.prototype.toString.call(it));
  for (const fn of [() => new Intl.Segmenter("en", { granularity: "x" }), () => Intl.Segmenter(), () => new Intl.Segmenter("en", null), () => Intl.Segmenter.prototype.segment.call({}, "x"),
    () => new Intl.Segmenter().segment(), () => Intl.Segmenter.supportedLocalesOf(["en", "xx", "th"]), () => new Intl.Segmenter("fr", { granularity: "sentence" }).resolvedOptions(),
    () => Object.getOwnPropertyNames(Object.getPrototypeOf(sg)), () => typeof sg.containing, () => Object.prototype.toString.call(sg)]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
}
{
  for (const st of ["long", "short", "narrow"]) {
    for (const l of ["en", "de", "fr", "ja"]) {
      const ln = new Intl.DisplayNames(l, { type: "language", style: st });
      const ld = new Intl.DisplayNames(l, { type: "language", style: st, languageDisplay: "standard" });
      const r = new Intl.DisplayNames(l, { type: "region", style: st });
      const s = new Intl.DisplayNames(l, { type: "script", style: st });
      const c = new Intl.DisplayNames(l, { type: "currency", style: st });
      const ca = new Intl.DisplayNames(l, { type: "calendar", style: st });
      const f = new Intl.DisplayNames(l, { type: "dateTimeField", style: st });
      p(st, l, ["en", "en-US", "en-GB", "fr-CA", "zh-Hans", "zh-Hant-TW", "sr-Latn", "es-419", "de-AT", "pt-BR", "xx", "en-Latn-US", "zh-Hant", "nl-BE", "ar-001", "en-XY", "qaa"].map(x => ln.of(x)).join("|"));
      p(st, l, ["en-US", "en-GB", "fr-CA", "zh-Hans", "pt-BR", "en-Latn-US"].map(x => ld.of(x)).join("|"));
      p(st, l, ["US", "GB", "DE", "419", "001", "HK", "MO", "XY", "ZZ", "BA", "UN", "EU", "PS", "150"].map(x => r.of(x)).join("|"));
      p(st, l, ["Latn", "Cyrl", "Hans", "Hant", "Arab", "Zzzz", "Cans", "Xsux", "Qaaa"].map(x => s.of(x)).join("|"));
      p(st, l, ["USD", "EUR", "JPY", "XYZ", "BTN", "usd"].map(x => c.of(x)).join("|"));
      p(st, l, ["gregory", "buddhist", "japanese", "islamic-civil", "roc", "iso8601", "ethioaa", "xyz"].map(x => ca.of(x)).join("|"));
      p(st, l, ["era", "year", "quarter", "month", "weekOfYear", "weekday", "day", "dayPeriod", "hour", "minute", "second", "timeZoneName"].map(x => f.of(x)).join("|"));
    }
  }
  const n = new Intl.DisplayNames("en", { type: "region", fallback: "none" });
  p(n.of("XY"), n.of("US"), new Intl.DisplayNames("en", { type: "language", fallback: "none" }).of("xx"), new Intl.DisplayNames("en", { type: "currency", fallback: "none" }).of("XYZ"));
  for (const fn of [() => new Intl.DisplayNames("en"), () => new Intl.DisplayNames("en", {}), () => new Intl.DisplayNames("en", { type: "x" }), () => Intl.DisplayNames("en", { type: "region" }),
    () => new Intl.DisplayNames("en", { type: "region" }).of("u"), () => new Intl.DisplayNames("en", { type: "region" }).of(), () => new Intl.DisplayNames("en", { type: "language" }).of("en-"),
    () => new Intl.DisplayNames("en", { type: "language" }).of("root"), () => new Intl.DisplayNames("en", { type: "language" }).of("en-u-ca-gregory"), () => new Intl.DisplayNames("en", { type: "script" }).of("Lat"),
    () => new Intl.DisplayNames("en", { type: "currency" }).of("US"), () => new Intl.DisplayNames("en", { type: "calendar" }).of("gregorian"), () => new Intl.DisplayNames("en", { type: "calendar" }).of("abc_d"),
    () => new Intl.DisplayNames("en", { type: "dateTimeField" }).of("years"), () => new Intl.DisplayNames("en", { type: "region", style: "x" }), () => new Intl.DisplayNames("en", { type: "region", fallback: "x" }),
    () => new Intl.DisplayNames("en", { type: "region" }).resolvedOptions(), () => new Intl.DisplayNames("en", { type: "language" }).resolvedOptions(),
    () => new Intl.DisplayNames(["xx", "de-CH"], { type: "currency", style: "narrow", fallback: "none" }).resolvedOptions(), () => Intl.DisplayNames.supportedLocalesOf(["en", "xx"]),
    () => Intl.DisplayNames.length, () => Object.getOwnPropertyNames(Intl.DisplayNames.prototype), () => Intl.DisplayNames.prototype.of.call({}, "en"),
    () => new Intl.DisplayNames("en", { type: "language" }).of("EN-us"), () => new Intl.DisplayNames("en", { type: "region" }).of("us"), () => new Intl.DisplayNames("en", { type: "script" }).of("latn"),
    () => new Intl.DisplayNames("en", { type: "language" }).of("sh"), () => new Intl.DisplayNames("en", { type: "language" }).of("iw"), () => new Intl.DisplayNames("en", { type: "region" }).of("SU"),
    () => new Intl.DisplayNames("en", { type: "language" }).of("zh-TW"), () => new Intl.DisplayNames("en", { type: "language" }).of("und"), () => new Intl.DisplayNames("en", { type: "language" }).of("en-Latn")]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
  const log = [];
  new Intl.DisplayNames("en", new Proxy({ type: "region" }, { get(t, k) { log.push(String(k)); return t[k]; } }));
  p(log);
}
