// Intl.DateTimeFormat and Date.prototype.toLocale*String: the default formats of the
// carried locales, the option components through ICU's pattern generator, the date and
// time styles, the hour cycles, the time zones (UTC, offsets, Etc/GMT's) and their names,
// formatToParts, formatRange and formatRangeToParts, resolvedOptions, the errors.
// Expected output: node 22 (ICU 78, CLDR 48).
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
{
  const times = [1794492309045, 1577869507045, -1, 1709208000000, 1751587200000, 946665000007, -63730800000000];
  const locales = ["en", "en-US", "en-GB", "en-IN", "en-AU", "en-CA", "de", "de-AT", "de-CH", "fr", "fr-CA", "fr-CH", "es", "es-MX", "es-419", "it", "pt", "pt-PT",
    "nl", "ru", "pl", "ja", "ja-JP", "zh", "zh-TW", "zh-HK", "ko", "hi", "ar", "ar-EG", "ar-SA", "he", "tr", "sv", "da", "nb", "no", "fi", "cs", "uk",
    "el", "hu", "ro", "id", "vi", "th-u-ca-gregory", "fa-u-ca-gregory", "xx"];
  for (const l of locales) {
    const d = new Date(times[0]);
    p(l, d.toLocaleString(l), "|", d.toLocaleDateString(l), "|", d.toLocaleTimeString(l), new Intl.DateTimeFormat(l).resolvedOptions());
  }
}
{
  const times = [1794492309045, 1577869507045, -1, 1709208000000, 946665000007, -63730800000000, 1751587200000];
  const opts = [
    { weekday: "long" }, { weekday: "short" }, { weekday: "narrow" }, { era: "short" }, { era: "long" }, { era: "narrow" },
    { year: "numeric" }, { year: "2-digit" }, { month: "long" }, { month: "short" }, { month: "narrow" }, { month: "2-digit" },
    { day: "2-digit" }, { hour: "numeric" }, { hour: "2-digit" }, { minute: "2-digit" }, { second: "numeric" },
    { year: "numeric", month: "long" }, { year: "numeric", month: "short", day: "numeric" }, { year: "numeric", month: "long", day: "numeric", weekday: "long" },
    { month: "long", day: "numeric" }, { month: "short", day: "numeric", weekday: "short" }, { year: "2-digit", month: "2-digit", day: "2-digit" },
    { year: "numeric", month: "2-digit" }, { month: "numeric", day: "numeric" }, { weekday: "long", day: "numeric" }, { era: "short", year: "numeric" },
    { era: "long", year: "numeric", month: "long", day: "numeric" }, { hour: "numeric", minute: "2-digit" }, { hour: "2-digit", minute: "2-digit", second: "2-digit" },
    { hour: "numeric", minute: "numeric", second: "numeric", fractionalSecondDigits: 3 }, { minute: "2-digit", second: "2-digit" },
    { second: "numeric", fractionalSecondDigits: 2 }, { minute: "numeric", fractionalSecondDigits: 1 }, { hour: "numeric", dayPeriod: "short" },
    { hour: "numeric", dayPeriod: "long" }, { hour: "numeric", minute: "numeric", dayPeriod: "narrow" }, { dayPeriod: "long" },
    { hour: "numeric", timeZoneName: "short" }, { hour: "numeric", minute: "numeric", timeZoneName: "long" }, { timeZoneName: "shortOffset" },
    { year: "numeric", month: "short", day: "numeric", hour: "numeric", minute: "2-digit" }, { month: "long", day: "numeric", hour: "numeric" },
    { weekday: "long", hour: "numeric", minute: "numeric" }, { year: "numeric", month: "long", day: "numeric", hour: "numeric", minute: "numeric", second: "numeric", timeZoneName: "short" },
    { weekday: "short", year: "numeric", month: "numeric", day: "numeric", hour: "numeric", minute: "numeric" },
    { hour: "numeric", hour12: false }, { hour: "numeric", hour12: true }, { hour: "numeric", hourCycle: "h11" }, { hour: "numeric", hourCycle: "h24" },
    { hour: "numeric", minute: "numeric", hourCycle: "h23" }, { hour: "numeric", minute: "numeric", hour12: true, hourCycle: "h23" },
    { dateStyle: "full" }, { dateStyle: "long" }, { dateStyle: "medium" }, { dateStyle: "short" }, { timeStyle: "full" }, { timeStyle: "long" },
    { timeStyle: "medium" }, { timeStyle: "short" }, { dateStyle: "full", timeStyle: "short" }, { dateStyle: "medium", timeStyle: "medium" },
    { dateStyle: "short", timeStyle: "long" }, { dateStyle: "long", timeStyle: "full" }, { timeStyle: "short", hour12: false }, { timeStyle: "short", hour12: true },
    { timeStyle: "medium", hourCycle: "h11" }, { dateStyle: "short", timeStyle: "short", hourCycle: "h24" },
  ];
  const locales = ["en", "en-GB", "de", "fr", "es", "it", "pt", "nl", "ru", "pl", "ja", "zh", "zh-TW", "ko", "hi", "ar", "he", "tr", "sv", "fi", "cs", "el", "hu", "id", "vi", "uk", "da", "nb", "ro", "fa-u-ca-gregory", "th-u-ca-gregory", "es-MX", "en-IN", "fr-CA", "de-CH", "ar-EG", "zh-HK", "pt-PT"];
  for (const l of locales) {
    for (const o of opts) {
      let f;
      try { f = new Intl.DateTimeFormat(l, { timeZone: "UTC", ...o }); } catch (e) { p(l, o, e.constructor.name, e.message); continue; }
      const r = f.resolvedOptions();
      delete r.locale; delete r.calendar; delete r.timeZone; delete r.numberingSystem;
      p(l, o, times.slice(0, 3).map(t => f.format(t)).join(" | "), r);
    }
  }
}
{
  const d = new Date(1794492309045);
  const tzn = ["short", "long", "shortOffset", "longOffset", "shortGeneric", "longGeneric"];
  for (const tz of ["UTC", "+00:00", "+05:30", "-08:00", "+0530", "-03", "−05:00", "Etc/GMT+5", "Etc/GMT-14", "Etc/GMT0", "GMT", "etc/utc", "UCT", "Zulu", "Etc/Greenwich", "-00:00", "+14", "+23:59",
    "Etc/GMT+13", "+24:00", "+5:30", "Foo/Bar", "America/New_York_X", "", "+05:60", "utc", "gmt+0", "Etc/GMT+00", "Universal", "etc/universal"]) {
    for (const l of ["en", "de", "fr", "ja", "ar", "he", "fa-u-ca-gregory", "hi", "ru"]) {
      try {
        p(tz, l, new Intl.DateTimeFormat(l, { timeZone: tz }).resolvedOptions().timeZone,
          tzn.map(t => new Intl.DateTimeFormat(l, { timeZone: tz, timeZoneName: t, hour: "numeric", minute: "numeric" }).format(d)).join(" | "));
      } catch (e) { p(tz, l, e.constructor.name, e.message); }
    }
  }
  for (const [l, o] of [["en", { dateStyle: "full", timeStyle: "full", timeZone: "+05:30" }], ["en", { hour: "numeric", minute: "numeric", timeZone: "UTC" }],
    ["de", { dateStyle: "medium", timeStyle: "long", timeZone: "UTC" }], ["ar-EG", { weekday: "long", year: "numeric", month: "long", day: "numeric", hour: "numeric" }],
    ["ja", { era: "long", year: "numeric", month: "long", day: "numeric", hour: "numeric", minute: "numeric", second: "numeric", fractionalSecondDigits: 3 }],
    ["en", { hour: "numeric", dayPeriod: "long" }], ["zh", { timeStyle: "full" }], ["ko", { dateStyle: "full", timeStyle: "short" }], ["fr", { timeZoneName: "longOffset", timeZone: "-08:00" }]]) {
    const f = new Intl.DateTimeFormat(l, o);
    p(l, o, f.formatToParts(d));
  }
  for (const t of [-63730800000000, -62135596800001, -62135596800000, 8.64e15, -8.64e15, 0, 1e12, 253402300799999, -30610224000000]) {
    p(t, new Date(t).toLocaleString("en", { era: "short", year: "numeric", month: "short", day: "numeric", hour: "numeric", timeZone: "UTC" }),
      new Date(t).toLocaleDateString("de", { year: "2-digit", month: "2-digit", day: "2-digit" }), new Date(t).toLocaleString("ja", { dateStyle: "full", timeStyle: "medium" }),
      new Date(t).toLocaleString("en", { era: "long", year: "numeric" }), new Date(t).toLocaleTimeString("fr", { hour: "numeric", minute: "numeric", second: "numeric", fractionalSecondDigits: 2 }));
  }
  for (const fn of [() => Date.prototype.toLocaleString.call({}), () => Date.prototype.toLocaleTimeString.call(1), () => new Date(NaN).toLocaleString("en", { timeZone: "Foo" }),
    () => new Intl.DateTimeFormat().format(NaN), () => new Intl.DateTimeFormat("en", { timeZone: "Etc/GMT+13" }), () => new Intl.DateTimeFormat("en", { dateStyle: "full", hour: "numeric" }),
    () => new Intl.DateTimeFormat("en", { timeStyle: "full", timeZoneName: "short" }), () => new Intl.DateTimeFormat("en", { dateStyle: "full", fractionalSecondDigits: 1 }),
    () => new Date(0).toLocaleDateString("en", { timeStyle: "short" }), () => new Date(0).toLocaleTimeString("en", { dateStyle: "short" }),
    () => new Date(0).toLocaleDateString("en", { dateStyle: "long" }), () => new Date(0).toLocaleTimeString("en", { timeStyle: "short" }),
    () => new Intl.DateTimeFormat("en", { hour: "x" }), () => new Intl.DateTimeFormat("en", { fractionalSecondDigits: 4 }), () => new Intl.DateTimeFormat("en", { fractionalSecondDigits: 0 }),
    () => new Intl.DateTimeFormat("en", { calendar: "x!" }), () => new Intl.DateTimeFormat("en", { timeZone: "+5:30" }), () => new Date(0).toLocaleString("en", { hour: "x" }),
    () => new Date(0).toLocaleString("en", null), () => new Intl.DateTimeFormat("en", null), () => new Date(0).toLocaleDateString("en", { hourCycle: "x" }),
    () => Intl.DateTimeFormat.prototype.formatToParts.call({}), () => Intl.DateTimeFormat.prototype.resolvedOptions.call(new Intl.NumberFormat()),
    () => Intl.DateTimeFormat.prototype.resolvedOptions.call(5), () => Object.getOwnPropertyDescriptor(Intl.DateTimeFormat.prototype, "format").get.call(5),
    () => Object.getOwnPropertyDescriptor(Intl.DateTimeFormat.prototype, "format").get.call({}), () => new Intl.DateTimeFormat("en", { numberingSystem: "x!" }),
    () => new Intl.DateTimeFormat("en").format(8.65e15), () => new Intl.DateTimeFormat("en").formatToParts("abc"), () => new Intl.DateTimeFormat("en", { timeZone: "UTC" }).format("1e12"),
    () => new Intl.DateTimeFormat("en", { timeZone: "UTC" }).format(new Date(5e11)), () => new Intl.DateTimeFormat("en", { timeZone: "UTC" }).format(true),
    () => Intl.DateTimeFormat.supportedLocalesOf(["en", "xx", "de-AT", "zh-TW"]), () => Intl.DateTimeFormat.length, () => Object.getOwnPropertyNames(Intl.DateTimeFormat.prototype),
    () => Object.prototype.toString.call(new Intl.DateTimeFormat()), () => typeof Object.getOwnPropertyDescriptor(Intl.DateTimeFormat.prototype, "format").get,
    () => new Intl.DateTimeFormat().format.name, () => new Intl.DateTimeFormat().format.length, () => Intl.DateTimeFormat.prototype.formatRange.length,
    () => [Date.prototype.toLocaleString.length, Date.prototype.toLocaleDateString.length, Date.prototype.toLocaleTimeString.length],
    () => Intl.DateTimeFormat("de", { timeZone: "UTC" }).format(0), () => { const f = new Intl.DateTimeFormat("en", { timeZone: "UTC" }); return [f.format === f.format, [0, 1e12].map(f.format)]; },
    () => new Intl.DateTimeFormat(["xx", "de"]).resolvedOptions().locale, () => new Intl.DateTimeFormat("en", { formatMatcher: "x" }),
    () => new Intl.DateTimeFormat("en", { dateStyle: "x" }), () => new Intl.DateTimeFormat("en", { hour12: "yes", hour: "numeric" }).resolvedOptions().hourCycle,
    () => new Intl.DateTimeFormat("en", { hourCycle: "h23", hour12: false, hour: "numeric" }).resolvedOptions().hourCycle]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
  const log = [];
  new Intl.DateTimeFormat("en", new Proxy({}, { get(t, k) { log.push(String(k)); return undefined; } }));
  p(log);
  for (const [l, o] of [["en-u-hc-h23", {}], ["en-u-hc-h23", { hour: "numeric" }], ["en-u-hc-h23", { hour: "numeric", hour12: true }], ["en-u-hc-h11", { hourCycle: "h23", hour: "numeric" }],
    ["en-u-hc-h12", { hourCycle: "h12", hour: "numeric" }], ["ja-JP", { hour: "numeric", hour12: true }], ["ja", { hour: "numeric", hour12: true }], ["en-u-ca-gregory-nu-arab", { hour: "numeric" }],
    ["en-u-nu-arab", { numberingSystem: "deva" }], ["en-u-nu-arab", { numberingSystem: "arab" }], ["en-u-ca-gregory", { calendar: "gregory" }], ["de-u-co-phonebk-nu-latn", {}],
    ["en", { numberingSystem: "hanidec" }], ["en", { numberingSystem: "thai", hour: "numeric", minute: "numeric", fractionalSecondDigits: 2, second: "numeric" }],
    ["ar-u-nu-arab", { dateStyle: "short" }], ["en-u-hc-h24", { timeStyle: "short" }], ["en-US-u-hc-h11", { hour: "2-digit", minute: "2-digit" }], ["de-u-hc-h12", { hour: "numeric" }],
    ["ko-u-hc-h23", { timeStyle: "short" }], ["zh-u-nu-hanidec", { dateStyle: "long" }], ["en-GB-u-hc-h12", { hour: "numeric", minute: "numeric" }]]) {
    const f = new Intl.DateTimeFormat(l, { timeZone: "UTC", ...o });
    p(l, o, f.format(d), f.resolvedOptions());
  }
  p(d.toLocaleString("en", { timeZone: "UTC", dateStyle: "medium" }), d.toLocaleDateString("de", { weekday: "long" }), d.toLocaleTimeString("ja", { hour: "2-digit" }),
    d.toLocaleDateString("en", { hour: "numeric" }), d.toLocaleTimeString("en", { month: "long" }), d.toLocaleDateString(undefined, { timeZoneName: "short" }),
    d.toLocaleTimeString(undefined, { timeZoneName: "long" }), d.toLocaleString(["zz", "ko"]), new Date(NaN).toLocaleString(), new Date(NaN).toLocaleDateString("de"));
  class MyDTF extends Intl.DateTimeFormat { constructor() { super("fr", { timeZone: "UTC", dateStyle: "long" }); } }
  const m = new MyDTF(); p(m.format(d), m instanceof MyDTF, m.resolvedOptions().locale);
}
{
  const base = 1794492309045; // 2026-11-12T14:05:09.045Z
  const deltas = [0, 30, 1000, 60000, 3600000, 4 * 3600000, 12 * 3600000, 86400000, 3 * 86400000, 40 * 86400000, 400 * 86400000, -86400000 * 800];
  const opts = [{}, { dateStyle: "long" }, { dateStyle: "short" }, { dateStyle: "medium", timeStyle: "short" }, { timeStyle: "short" }, { timeStyle: "medium" },
    { year: "numeric", month: "short", day: "numeric" }, { month: "long", day: "numeric" }, { year: "numeric", month: "long" }, { hour: "numeric", minute: "numeric" },
    { hour: "numeric" }, { hour: "2-digit", minute: "2-digit", hour12: false }, { weekday: "short", month: "short", day: "numeric", hour: "numeric", minute: "numeric" },
    { year: "numeric" }, { month: "short" }, { day: "numeric" }, { hour: "numeric", minute: "numeric", second: "numeric" }, { minute: "numeric", second: "numeric" },
    { weekday: "long", year: "numeric", month: "long", day: "numeric" }, { hour: "numeric", timeZoneName: "short" }, { era: "short", year: "numeric", month: "short", day: "numeric" },
    { hour: "numeric", dayPeriod: "short" }, { month: "numeric", day: "numeric", hour: "numeric" }];
  for (const l of ["en", "de", "fr", "ja", "zh", "ru", "ar", "es", "ko", "hi", "en-GB", "pt", "it", "he", "fi", "nl", "pl", "tr", "sv", "cs"]) {
    for (const o of opts) {
      const f = new Intl.DateTimeFormat(l, { timeZone: "UTC", ...o });
      p(l, o, deltas.map(dd => f.formatRange(base, base + dd)).join(" | "));
    }
  }
  const f = new Intl.DateTimeFormat("en", { timeZone: "UTC", dateStyle: "medium", timeStyle: "short" });
  for (const dd of [0, 3600000, 86400000]) p(f.formatRangeToParts(base, base + dd));
  const g = new Intl.DateTimeFormat("de", { timeZone: "UTC", hour: "numeric", minute: "numeric" });
  p(g.formatRangeToParts(base, base + 3600000), g.formatRangeToParts(base, base + 13 * 3600000));
  for (const fn of [() => f.formatRange(base), () => f.formatRange(undefined, base), () => f.formatRange(NaN, base), () => f.formatRange(base, 9e15),
    () => f.formatRange(base + 86400000, base), () => Intl.DateTimeFormat.prototype.formatRange.call({}, 1, 2), () => f.formatRangeToParts(new Date(base), new Date(base + 5e8)),
    () => f.formatRange("1000", "2000000000")]) {
    try { p("ok", fn()); } catch (e) { p(e.constructor.name, e.message); }
  }
}
