// tests/js/intl-number.js -- Intl.NumberFormat, each line against V8's (intl-number.expect): the
// symbols, grouping (Indian, min2) and patterns of 40 locales, every style (percent, currency by code,
// symbol, narrow symbol and name, accounting; the sanctioned units and their compounds, short, long and
// narrow), notations (scientific, engineering, compact short and long), the rounding options (digits,
// significant digits, priority, the nine modes, increments, stripIfInteger), signDisplay, useGrouping,
// numbering systems, formatToParts, formatRange and its parts, exact strings and BigInts, the errors,
// resolvedOptions, supportedLocalesOf, and Number's and BigInt's toLocaleString.
{
const P = (l, o, v) => { const f = new Intl.NumberFormat(l, o); return f.formatToParts(v).map(p => p.type + ":" + JSON.stringify(p.value)).join(" "); };
console.log(new Intl.NumberFormat().format(1234567.891), (1234.5).toLocaleString(), (1234.5).toLocaleString("de"), (0.5).toLocaleString("en", {style:"percent"}));
console.log(P("en", {notation:"compact", compactDisplay:"long"}, 1234));
console.log(P("en", {notation:"compact"}, 1234));
console.log(P("de", {notation:"compact"}, 1234567));
console.log(P("fr", {notation:"compact", compactDisplay:"long"}, 1200000));
console.log(P("en", {style:"currency", currency:"USD", notation:"compact"}, 1234567));
console.log(P("en", {style:"currency", currency:"CHF"}, -1234.5));
console.log(P("en", {style:"currency", currency:"USD", currencySign:"accounting"}, -1234.5));
console.log(P("de", {style:"currency", currency:"EUR"}, -1234.5));
console.log(P("ar-EG", {style:"percent"}, -0.5));
console.log(P("en", {notation:"scientific"}, 123456));
console.log(P("en", {notation:"engineering"}, -0.000123456));
console.log(P("en", {style:"unit", unit:"kilometer-per-hour", unitDisplay:"long"}, 1));
console.log(P("en", {style:"unit", unit:"kilometer-per-hour"}, 50));
console.log(P("en", {style:"unit", unit:"meter-per-foot", unitDisplay:"long"}, 2));
console.log(P("en", {style:"unit", unit:"percent"}, 5));
console.log(P("en", {style:"currency", currency:"USD", currencyDisplay:"name"}, 1));
console.log(new Intl.NumberFormat("en").formatRange(3, 5), "|", new Intl.NumberFormat("en",{style:"currency",currency:"USD"}).formatRange(3, 5), "|", new Intl.NumberFormat("en",{maximumFractionDigits:0}).formatRange(2.9, 3.1));
console.log(new Intl.NumberFormat("hi").format(-1234567.5), new Intl.NumberFormat("es").format(1234), new Intl.NumberFormat("es").format(12345), new Intl.NumberFormat("fr").format(1234567.5), new Intl.NumberFormat("de-CH").format(1234567.5));
console.log(JSON.stringify(new Intl.NumberFormat("de", {style:"currency", currency:"eur"}).resolvedOptions()));
}
{
const locales = ["en", "en-US", "en-GB", "en-IN", "de", "de-CH", "de-AT", "fr", "fr-CA", "fr-CH", "es", "es-MX", "es-419", "it", "pt", "pt-PT",
  "nl", "ru", "pl", "ja", "zh", "zh-TW", "zh-HK", "ko", "hi", "ar", "ar-EG", "ar-SA", "he", "tr", "sv", "da", "nb", "fi", "cs", "uk",
  "el", "hu", "ro", "id", "vi", "th", "fa", "xx"];
const values = [0, -0, 1, -1, 0.5, 1.5, 2, 5, 12, 123.456, 1234.5678, -1234.5678, 12345.678, 1234567.891, 0.000123, 1e21, 1.5e-7, NaN, Infinity, -Infinity];
for (const l of locales) {
  const f = new Intl.NumberFormat(l);
  console.log(l, f.resolvedOptions().locale, f.resolvedOptions().numberingSystem, values.map(v => f.format(v)).join(" | "));
}
const opts = [
  { style: "percent" }, { style: "percent", maximumFractionDigits: 2 }, { style: "currency", currency: "USD" }, { style: "currency", currency: "EUR" },
  { style: "currency", currency: "JPY" }, { style: "currency", currency: "GBP", currencyDisplay: "code" }, { style: "currency", currency: "USD", currencyDisplay: "narrowSymbol" },
  { style: "currency", currency: "EUR", currencyDisplay: "name" }, { style: "currency", currency: "USD", currencySign: "accounting" }, { style: "currency", currency: "CHF" },
  { style: "currency", currency: "INR" }, { style: "currency", currency: "CNY" }, { style: "currency", currency: "RUB" }, { style: "currency", currency: "BRL" },
  { notation: "compact" }, { notation: "compact", compactDisplay: "long" }, { notation: "scientific" }, { notation: "engineering" },
  { style: "unit", unit: "kilometer" }, { style: "unit", unit: "kilometer", unitDisplay: "long" }, { style: "unit", unit: "kilometer-per-hour", unitDisplay: "narrow" },
  { style: "unit", unit: "liter", unitDisplay: "long" }, { style: "unit", unit: "byte", unitDisplay: "long" }, { style: "unit", unit: "celsius" },
  { style: "unit", unit: "percent", unitDisplay: "long" }, { style: "unit", unit: "hour", unitDisplay: "long" }, { style: "unit", unit: "megabyte-per-second" },
  { signDisplay: "always" }, { signDisplay: "exceptZero" }, { signDisplay: "negative" }, { signDisplay: "never" },
  { useGrouping: false }, { useGrouping: "min2" }, { useGrouping: "always" }, { minimumIntegerDigits: 3 }, { minimumFractionDigits: 2 },
  { maximumSignificantDigits: 3 }, { minimumSignificantDigits: 5 }, { maximumFractionDigits: 0, roundingMode: "floor" },
  { maximumFractionDigits: 1, roundingMode: "halfEven" }, { maximumFractionDigits: 2, roundingIncrement: 5 }, { maximumFractionDigits: 2, minimumFractionDigits: 2, roundingIncrement: 25 },
  { maximumSignificantDigits: 2, maximumFractionDigits: 2, roundingPriority: "morePrecision" }, { maximumSignificantDigits: 2, maximumFractionDigits: 2, roundingPriority: "lessPrecision" },
  { minimumFractionDigits: 2, trailingZeroDisplay: "stripIfInteger" }, { style: "currency", currency: "EUR", notation: "compact" },
  { style: "percent", notation: "compact" }, { style: "unit", unit: "meter", notation: "compact", compactDisplay: "long", unitDisplay: "long" },
];
const vals2 = [0, -0, 1, -1.5, 0.125, 2.5, 3.5, 999.95, 1234.5678, -98765.4321, 1e6, 1.2e9, 12345678901234, 0.0001234];
for (const l of ["en", "de", "fr", "ja", "ar", "hi", "ru", "es", "pt-PT", "he"]) {
  for (const o of opts) {
    let f;
    try { f = new Intl.NumberFormat(l, o); } catch (e) { console.log(l, JSON.stringify(o), e.constructor.name, e.message); continue; }
    console.log(l, JSON.stringify(o), vals2.map(v => f.format(v)).join(" | "));
  }
}
}
{
const P = (f, v) => f.formatToParts(v).map(p => p.type + ":" + JSON.stringify(p.value)).join(" ");
const R = (f, a, b) => f.formatRangeToParts(a, b).map(p => p.type + ":" + JSON.stringify(p.value) + ":" + p.source[0]).join(" ");
for (const l of ["en", "de", "fr", "ar", "ar-EG", "fa", "he", "hi", "ja", "zh", "ru", "pl", "es", "pt", "nl", "tr", "sv", "ko", "th", "vi"]) {
  for (const o of [{}, { style: "currency", currency: "EUR" }, { style: "currency", currency: "USD", currencyDisplay: "name" }, { style: "percent", signDisplay: "always" },
                   { notation: "compact", compactDisplay: "long" }, { notation: "scientific" }, { style: "unit", unit: "kilometer-per-hour", unitDisplay: "long" },
                   { style: "currency", currency: "JPY", currencySign: "accounting" }]) {
    const f = new Intl.NumberFormat(l, o);
    console.log(l, JSON.stringify(o), P(f, -1234.567), "|", P(f, 0), "|", P(f, 1), "|", f.formatRange(1, 5), "|", f.formatRange(-2, 2), "|", f.formatRange(1000, 1000), "|", R(f, 3, 7.5));
  }
}
// numbering systems
for (const nu of ["arab", "arabext", "deva", "hanidec", "thai", "fullwide", "beng", "latn", "tamldec", "roman", "bogus"]) {
  for (const l of ["en", "ar", "hi", "zh", "de"]) {
    const f = new Intl.NumberFormat(l, { numberingSystem: nu });
    console.log(nu, l, f.resolvedOptions().locale, f.resolvedOptions().numberingSystem, f.format(-1234567.891), P(new Intl.NumberFormat(l, { numberingSystem: nu, style: "percent" }), 0.25), new Intl.NumberFormat(l + "-u-nu-" + nu).format(12.5), new Intl.NumberFormat(l + "-u-nu-" + nu).resolvedOptions().locale);
  }
}
// inputs: strings, BigInts, objects
const f = new Intl.NumberFormat("en", { maximumFractionDigits: 20 });
for (const v of ["1234.5678901234567890123", "  42  ", "", "abc", "0x1F", "0b101", "0o17", "-0", "+5", "1e3", "1e-3", ".5", "5.", "Infinity", "-Infinity", "1e400", "123456789012345678901234567890",
                 12345678901234567890n, -5n, 0n, 2n ** 100n, true, null, undefined, [], [5], {}, { valueOf() { return 7; } }, new Number(3), Object(9n)]) {
  let s;
  try { s = f.format(v); } catch (e) { s = e.constructor.name + ": " + e.message; }
  console.log(typeof v, JSON.stringify(String(v)), s);
}
console.log(new Intl.NumberFormat("en").format(1e21), new Intl.NumberFormat("en").format(1.7976931348623157e308), new Intl.NumberFormat("en").format(5e-324), new Intl.NumberFormat("en", { maximumSignificantDigits: 21 }).format(0.1), new Intl.NumberFormat("en", { maximumFractionDigits: 100 }).format(0.1), new Intl.NumberFormat("en", { minimumFractionDigits: 100 }).format(1));
// rounding modes and increments
const rv = [-2.5, -1.5, -1.25, -0.5, 0.5, 1.25, 1.5, 2.5, 1.005, 2.675, 9.995, 0.045];
for (const rm of ["ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven"]) {
  const a = new Intl.NumberFormat("en", { maximumFractionDigits: 0, roundingMode: rm }), b = new Intl.NumberFormat("en", { maximumFractionDigits: 1, roundingMode: rm }), c = new Intl.NumberFormat("en", { maximumFractionDigits: 2, minimumFractionDigits: 2, roundingIncrement: 5, roundingMode: rm });
  console.log(rm, rv.map(v => a.format(v)).join(" "), "|", rv.map(v => b.format(v)).join(" "), "|", rv.map(v => c.format(v)).join(" "));
}
for (const inc of [1, 2, 5, 10, 20, 25, 50, 100, 250, 500, 1000, 2500, 5000]) {
  const g = new Intl.NumberFormat("en", { maximumFractionDigits: 3, minimumFractionDigits: 3, roundingIncrement: inc });
  const h = new Intl.NumberFormat("en", { maximumFractionDigits: 0, roundingIncrement: inc });
  console.log(inc, [1.23456, 7.777, 0.3, 123.456, -9.9991].map(v => g.format(v)).join(" "), "|", [12345, 6789, 1, 2499, 2500].map(v => h.format(v)).join(" "));
}
for (const o of [{ maximumSignificantDigits: 1 }, { maximumSignificantDigits: 2, minimumSignificantDigits: 2 }, { minimumSignificantDigits: 3, maximumSignificantDigits: 5 },
                 { maximumSignificantDigits: 3, roundingPriority: "lessPrecision" }, { maximumSignificantDigits: 3, maximumFractionDigits: 1, roundingPriority: "morePrecision" },
                 { minimumSignificantDigits: 2, minimumFractionDigits: 2, roundingPriority: "lessPrecision" }, { notation: "compact", maximumFractionDigits: 2 },
                 { notation: "compact", maximumSignificantDigits: 4 }, { notation: "compact", minimumFractionDigits: 1 }, { notation: "engineering", maximumSignificantDigits: 2 },
                 { notation: "scientific", minimumFractionDigits: 2 }, { trailingZeroDisplay: "stripIfInteger", minimumFractionDigits: 2, maximumFractionDigits: 2 },
                 { style: "currency", currency: "USD", trailingZeroDisplay: "stripIfInteger" }, { minimumIntegerDigits: 5, useGrouping: false }]) {
  const g = new Intl.NumberFormat("en", o);
  console.log(JSON.stringify(o), [0, 0.00042, 1, 9.99, 99.95, 999.5, 1000, 12345.6789, 999999, 1.5e9, -0.0042].map(v => g.format(v)).join(" | "), JSON.stringify(g.resolvedOptions()));
}
}
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const errs = [{ style: "currency" }, { currency: "US" }, { currency: "U$D" }, { style: "unit" }, { style: "unit", unit: "foo" }, { style: "unit", unit: "meter-per-foo" },
  { numberingSystem: "latn1-" }, { numberingSystem: "abcdefghi" }, { minimumFractionDigits: 3, maximumFractionDigits: 1 }, { maximumFractionDigits: 101 },
  { minimumIntegerDigits: 0 }, { minimumIntegerDigits: 22 }, { roundingIncrement: 3 }, { roundingIncrement: 5, maximumSignificantDigits: 2 },
  { roundingIncrement: 5, minimumFractionDigits: 1, maximumFractionDigits: 2 }, { style: "bogus" }, { notation: "x" }, { maximumSignificantDigits: 0 },
  { minimumSignificantDigits: 5, maximumSignificantDigits: 2 }, { roundingMode: "up" }, { roundingPriority: "x" }, { trailingZeroDisplay: "x" },
  { useGrouping: "x" }, { signDisplay: "x" }, { currencyDisplay: "x", style: "currency", currency: "EUR" }, { unitDisplay: "x" }, { compactDisplay: "x" },
  { localeMatcher: "x" }, { maximumFractionDigits: NaN }, { minimumFractionDigits: "2" }, { maximumFractionDigits: 2.9 }, { roundingIncrement: 5 },
  { style: "currency", currency: "eur", maximumFractionDigits: 0 }, { style: "currency", currency: "EUR", minimumFractionDigits: 3 }, { style: "currency", currency: "JPY", maximumFractionDigits: 1 }];
for (const o of errs) {
  try { const f = new Intl.NumberFormat("en", o); p(o, "ok", f.resolvedOptions(), f.format(1234.5678)); } catch (e) { p(o, e.constructor.name, e.message); }
}
const log = [];
new Intl.NumberFormat("en", new Proxy({}, { get(t, k) { log.push(String(k)); return undefined; } }));
p(log);
p(Intl.NumberFormat.length, Intl.NumberFormat.name, Intl.NumberFormat.prototype[Symbol.toStringTag], Object.prototype.toString.call(new Intl.NumberFormat()),
  typeof Object.getOwnPropertyDescriptor(Intl.NumberFormat.prototype, "format").get, Object.getOwnPropertyDescriptor(Intl.NumberFormat.prototype, "format").get.name,
  Intl.NumberFormat.prototype.formatToParts.length, Intl.NumberFormat.prototype.formatRange.length, Intl.NumberFormat.supportedLocalesOf.length);
const f = new Intl.NumberFormat("de");
const g = f.format;
p(g === f.format, g.name, g.length, g(1234.5), [1, 22, 333].map(f.format), Intl.NumberFormat("fr").format(1234.5), Intl.NumberFormat() instanceof Intl.NumberFormat);
for (const fn of [() => Intl.NumberFormat.prototype.resolvedOptions.call({}), () => Intl.NumberFormat.prototype.formatToParts.call(1, 2),
                  () => Object.getOwnPropertyDescriptor(Intl.NumberFormat.prototype, "format").get.call(new Intl.Locale("en")),
                  () => new Intl.NumberFormat().formatRange(1), () => new Intl.NumberFormat().formatRange(NaN, 1), () => new Intl.NumberFormat().formatRange(1, NaN),
                  () => new Intl.NumberFormat().formatRange(undefined, 2), () => new Intl.NumberFormat().format(1n + 2n), () => new Intl.NumberFormat().format(Symbol()),
                  () => Number.prototype.toLocaleString.call("1"), () => BigInt.prototype.toLocaleString.call(1), () => (1).toLocaleString("en", null),
                  () => new Intl.NumberFormat("en", null), () => Intl.NumberFormat.supportedLocalesOf("en", null), () => (1).toLocaleString("xx_YY")]) {
  try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
}
p(Intl.NumberFormat.supportedLocalesOf(["tlh", "de-DE", "de-XX", "en-ZZ", "ja-JP-u-ca-japanese", "fr-CA", "pt-BR", "zh-Hant-TW", "nb-NO", "en-150"]),
  Intl.NumberFormat.supportedLocalesOf("EN-gb", { localeMatcher: "lookup" }), Intl.NumberFormat.supportedLocalesOf([]), Intl.NumberFormat.supportedLocalesOf());
for (const l of ["de-DE", "de-XX", "en-ZZ", "fr-FR", "zh-CN", "zh-TW", "zh-Hant-TW", "zh-Hans-SG", "nb-NO", "no-NO", "es-ES", "pt-BR", "en-GB-u-nu-arab",
                 "ar-u-nu-latn", "en-u-nu-thai-ca-buddhist", "xx", "und", "en-Latn-US", "es-419", "ja-JP-u-nu-jpan", [] + "", "iw-IL", "in"]) {
  try {
    const r = new Intl.NumberFormat(l || undefined).resolvedOptions();
    p(l, r.locale, r.numberingSystem, (1234.5).toLocaleString(l || undefined));
  } catch (e) { p(l, e.constructor.name, e.message); }
}
p((1234.5).toLocaleString(), (1234.5).toLocaleString("de-DE"), (1234.5).toLocaleString(["xx", "fr"]), (1234.5).toLocaleString(undefined, { style: "currency", currency: "EUR" }),
  (0.256).toLocaleString("en", { style: "percent", minimumFractionDigits: 1 }), (12345678901234567890n).toLocaleString("de"), (-5n).toLocaleString("en", { style: "currency", currency: "USD" }),
  Number.prototype.toLocaleString.call(new Number(42)), (NaN).toLocaleString("ar-EG"), (1e21).toLocaleString("hi"), (123).toLocaleString("en", { notation: "compact" }));
class MyNF extends Intl.NumberFormat { constructor() { super("fr"); } }
const m = new MyNF(); p(m.format(1234.5), m instanceof MyNF, m.resolvedOptions().locale);
}
