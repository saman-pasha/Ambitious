// Formatting in a time zone by IANA name (12ao), as node 22 (V8, ICU 78.3, tzdata 2026e) does
// it: each field in the zone's local time at its transitions (the hour that repeats in
// autumn, the one skipped in spring, the half and quarter hours, a day skipped), before
// 1970 and after 2037 (a final rule's years, the time value's ends), formatToParts' types,
// formatRange across a transition, Date's toLocale*String, the styles' patterns, the
// default zone (UTC, as the engine's local time is).
const p = (...a) => console.log(a.join(" | "));
const parts = (f, t) => f.formatToParts(t).map(q => q.type + ":" + q.value).join(",");
const FULL = { year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23" };
function offsetOf(f, t) {
  const v = {};
  for (const q of f.formatToParts(t)) v[q.type] = q.value;
  return Date.UTC(+v.year, +v.month - 1, +v.day, +v.hour, +v.minute, +v.second) - t;
}
// a zone's changes of offset in [from, to): 6-hour steps, each bisected to the second
function changes(z, from, to) {
  const f = new Intl.DateTimeFormat("en-US", { timeZone: z, ...FULL }), out = [];
  let t = from, o = offsetOf(f, t);
  for (let u = t + 6 * 3600000; u <= to; t = u, u += 6 * 3600000) {
    const q = offsetOf(f, u);
    if (q === o) continue;
    let lo = t, hi = u;
    while (hi - lo > 1000) {
      const mid = lo + Math.floor((hi - lo) / 2000) * 1000;
      if (offsetOf(f, mid) === o) lo = mid; else hi = mid;
    }
    out.push([hi, o, offsetOf(f, hi)]);
    o = q;
  }
  return out;
}
const hm = ms => {
  const a = Math.abs(ms) / 1000;
  return (ms < 0 ? "-" : "+") + Math.floor(a / 3600) + ":" + String(Math.floor(a / 60) % 60).padStart(2, "0");
};
const ZONES = [["America/New_York", 2025], ["America/Los_Angeles", 2025], ["America/St_Johns", 2025], ["Europe/London", 2025],
  ["Europe/Berlin", 2025], ["Europe/Dublin", 2025], ["Europe/Moscow", 2011], ["Europe/Moscow", 2014], ["Australia/Sydney", 2025],
  ["Australia/Adelaide", 2025], ["Australia/Lord_Howe", 2025], ["Pacific/Chatham", 2025], ["Pacific/Auckland", 2025],
  ["America/Santiago", 2025], ["America/Asuncion", 2024], ["Antarctica/Troll", 2025], ["America/Nuuk", 2023], ["Asia/Gaza", 2025],
  ["Asia/Jerusalem", 2025], ["Asia/Tehran", 2022], ["America/Sao_Paulo", 2019], ["Africa/Casablanca", 2026], ["Africa/Cairo", 2025],
  ["Pacific/Apia", 2011], ["Pacific/Kiritimati", 1994], ["Asia/Kathmandu", 1985], ["America/Havana", 2025], ["Asia/Almaty", 2024],
  ["America/Mexico_City", 2022], ["America/Vancouver", 2026], ["America/Edmonton", 2026], ["Europe/Lisbon", 1992],
  ["America/Caracas", 2016], ["Asia/Pyongyang", 2018], ["Asia/Shanghai", 1991], ["Asia/Kolkata", 1945], ["America/Juneau", 1983],
  ["Asia/Dhaka", 2009], ["Pacific/Fiji", 2021], ["EST5EDT", 2025], ["SystemV/CST6CDT", 1975]];
for (const [z, y] of ZONES) {
  const f = new Intl.DateTimeFormat("en-US", { timeZone: z, ...FULL, timeZoneName: "short" });
  const g = new Intl.DateTimeFormat("de", { timeZone: z, dateStyle: "medium", timeStyle: "long" });
  for (const [t, a, b] of changes(z, Date.UTC(y, 0, 1), Date.UTC(y + 1, 0, 1))) {
    p(z, new Date(t).toISOString(), hm(a) + " to " + hm(b));
    const step = Math.abs(b - a) >= 3600000 ? 1800000 : 900000;
    for (let k = -4; k <= 4; k++) p("  " + (k < 0 ? k : "+" + k), f.format(t + k * step), g.format(t + k * step), parts(f, t + k * step - 1));
  }
}

// the hour that repeats and the one skipped, as local times (a time value each)
for (const [z, a, b] of [["America/New_York", Date.UTC(2025, 2, 9, 6), Date.UTC(2025, 2, 9, 8)],
  ["America/New_York", Date.UTC(2025, 10, 2, 5), Date.UTC(2025, 10, 2, 7)], ["Europe/Paris", Date.UTC(2025, 2, 30, 0), Date.UTC(2025, 2, 30, 2)],
  ["Europe/Paris", Date.UTC(2025, 9, 26, 0), Date.UTC(2025, 9, 26, 2)], ["Australia/Lord_Howe", Date.UTC(2025, 3, 5, 14), Date.UTC(2025, 3, 5, 16)],
  ["Pacific/Apia", Date.UTC(2011, 11, 29, 9), Date.UTC(2011, 11, 30, 11)]]) {
  const f = new Intl.DateTimeFormat("en", { timeZone: z, weekday: "short", month: "short", day: "numeric", hour: "numeric", minute: "2-digit", timeZoneName: "longOffset" });
  const out = [z];
  for (let t = a; t <= b; t += 15 * 60000) out.push(f.format(t));
  p(...out);
}

// before 1970, after 2037, the time value's ends; zones with final rules and without
const FAR = [-8.64e15, Date.UTC(-1000, 6, 1), Date.UTC(0, 0, 1), Date.UTC(1, 0, 1), Date.UTC(1582, 9, 10), Date.UTC(1800, 6, 1),
  Date.UTC(1883, 10, 18, 16, 59, 59), Date.UTC(1883, 10, 18, 17), Date.UTC(1900, 0, 1), -2147483648000, -2147483649000,
  Date.UTC(1918, 2, 31, 7), Date.UTC(1942, 6, 1), Date.UTC(1960, 6, 1), 2147483647000, 2147483648000, Date.UTC(2037, 6, 1),
  Date.UTC(2038, 6, 1), Date.UTC(2050, 2, 13, 7), Date.UTC(2087, 6, 1), Date.UTC(2100, 2, 14, 10), Date.UTC(3000, 6, 1),
  Date.UTC(10000, 0, 1), 8.64e15];
for (const z of ["America/New_York", "America/Chicago", "Europe/London", "Europe/Berlin", "Asia/Kolkata", "Australia/Sydney",
  "Asia/Gaza", "Africa/Casablanca", "Pacific/Chatham", "America/Santiago", "Asia/Tehran", "Asia/Tokyo", "Pacific/Kiritimati",
  "America/Nuuk", "EST5EDT", "SystemV/YST9YDT"]) {
  const f = new Intl.DateTimeFormat("en-US", { timeZone: z, era: "short", ...FULL, timeZoneName: "longOffset" });
  const g = new Intl.DateTimeFormat("en-US", { timeZone: z, year: "numeric", month: "short", day: "numeric", hour: "numeric", timeZoneName: "short" });
  p(z, FAR.map(t => f.format(t)).join(" ; "));
  p(z, FAR.map(t => g.format(t)).join(" ; "));
}

// formatToParts' types
for (const [l, z, o] of [["en", "America/New_York", { weekday: "long", era: "short", ...FULL, fractionalSecondDigits: 3, timeZoneName: "long" }],
  ["de", "Europe/Berlin", { dateStyle: "full", timeStyle: "full" }], ["ja", "Asia/Tokyo", { dateStyle: "long", timeStyle: "long" }],
  ["ar", "Asia/Riyadh", { hour: "numeric", minute: "numeric", timeZoneName: "shortGeneric" }],
  ["hi", "Asia/Kolkata", { hour: "numeric", dayPeriod: "long", timeZoneName: "longGeneric" }],
  ["zh", "Asia/Shanghai", { month: "long", day: "numeric", hour: "numeric", timeZoneName: "shortOffset" }],
  ["he", "Asia/Jerusalem", { timeStyle: "full" }], ["fr", "America/Montreal", { timeStyle: "long", hour12: true }],
  ["ko", "Asia/Seoul", { year: "2-digit", hour: "2-digit", timeZoneName: "short" }],
  ["en-AU", "Australia/Lord_Howe", { timeStyle: "full", dateStyle: "short" }],
  ["fa-u-ca-gregory", "Asia/Tehran", { timeStyle: "long" }], ["th-u-ca-gregory", "Asia/Bangkok", { timeStyle: "full" }]]) {
  const f = new Intl.DateTimeFormat(l, { timeZone: z, ...o });
  p(l, z, parts(f, Date.UTC(2025, 0, 15, 12, 34, 56, 789)), parts(f, Date.UTC(2025, 6, 15, 23, 4, 5, 6)));
}

// formatRange and formatRangeToParts across transitions, within a repeated hour, across days
const RANGES = [["America/New_York", Date.UTC(2025, 10, 2, 5, 30), Date.UTC(2025, 10, 2, 6, 30)],
  ["America/New_York", Date.UTC(2025, 2, 9, 6, 30), Date.UTC(2025, 2, 9, 7, 30)], ["America/New_York", Date.UTC(2025, 10, 2, 5), Date.UTC(2025, 10, 2, 5)],
  ["Europe/Berlin", Date.UTC(2025, 9, 26, 0, 15), Date.UTC(2025, 9, 26, 1, 15)], ["Europe/Berlin", Date.UTC(2025, 9, 25, 22), Date.UTC(2025, 9, 26, 3)],
  ["Australia/Sydney", Date.UTC(2025, 3, 5, 15), Date.UTC(2025, 3, 5, 17)], ["Australia/Lord_Howe", Date.UTC(2025, 9, 4, 15, 15), Date.UTC(2025, 9, 4, 16, 15)],
  ["Pacific/Apia", Date.UTC(2011, 11, 29, 10), Date.UTC(2011, 11, 29, 11)], ["Asia/Kolkata", Date.UTC(2025, 0, 1), Date.UTC(2025, 0, 1, 20)],
  ["Asia/Kathmandu", Date.UTC(1985, 11, 31, 18), Date.UTC(1985, 11, 31, 19)], ["Africa/Casablanca", Date.UTC(2026, 8, 20, 0, 30), Date.UTC(2026, 8, 20, 1, 30)]];
for (const o of [{ hour: "numeric", minute: "numeric", timeZoneName: "short" }, { dateStyle: "medium", timeStyle: "long" },
  { month: "short", day: "numeric", hour: "numeric" }, { hour: "numeric", minute: "numeric", timeZoneName: "longOffset" },
  { year: "numeric", month: "numeric", day: "numeric", timeZoneName: "longGeneric" }])
  for (const l of ["en", "de", "ja"]) for (const [z, a, b] of RANGES) {
    const f = new Intl.DateTimeFormat(l, { timeZone: z, ...o });
    p(l, z, JSON.stringify(o), f.formatRange(a, b), f.formatRangeToParts(a, b).map(q => q.type + ":" + q.source + ":" + q.value).join(","));
  }

// Date's toLocaleString, toLocaleDateString, toLocaleTimeString in a zone
for (const t of [Date.UTC(2025, 2, 9, 6, 59, 59), Date.UTC(2025, 2, 9, 7), Date.UTC(2025, 10, 2, 5, 59, 59), Date.UTC(2025, 10, 2, 6),
  Date.UTC(1969, 11, 31, 23, 59, 59, 999), Date.UTC(2040, 6, 4, 12)]) {
  const d = new Date(t);
  for (const [l, z] of [["en-US", "America/New_York"], ["en-GB", "Europe/London"], ["de-DE", "Europe/Berlin"], ["fr-FR", "Europe/Paris"],
    ["ja-JP", "Asia/Tokyo"], ["zh-CN", "Asia/Shanghai"], ["hi-IN", "Asia/Kolkata"], ["en-AU", "Australia/Sydney"], ["pt-BR", "America/Sao_Paulo"],
    ["es-MX", "America/Mexico_City"], ["ru-RU", "Europe/Moscow"], ["ar-EG", "Africa/Cairo"], ["ko-KR", "Asia/Seoul"], ["tr-TR", "Europe/Istanbul"]])
    p(l, z, d.toLocaleString(l, { timeZone: z }), d.toLocaleDateString(l, { timeZone: z }), d.toLocaleTimeString(l, { timeZone: z }),
      d.toLocaleString(l, { timeZone: z, timeZoneName: "short" }), d.toLocaleTimeString(l, { timeZone: z, timeZoneName: "longGeneric", hour12: false }));
}

// the styles' patterns with a zone, the hour cycles
for (const l of ["en", "en-GB", "de", "fr", "es", "it", "ja", "zh", "zh-Hant", "ko", "ru", "ar", "he", "hi"])
  for (const z of ["America/Chicago", "Europe/Rome", "Asia/Hong_Kong"]) {
    const out = [l, z];
    for (const ds of ["full", "long", "medium", "short"]) for (const ts of ["full", "long", "medium", "short"])
      out.push(new Intl.DateTimeFormat(l, { timeZone: z, dateStyle: ds, timeStyle: ts }).format(Date.UTC(2025, 6, 4, 18, 5, 9)));
    for (const hc of ["h11", "h12", "h23", "h24"])
      out.push(new Intl.DateTimeFormat(l, { timeZone: z, hour: "numeric", minute: "numeric", hourCycle: hc, timeZoneName: "short" }).format(Date.UTC(2025, 0, 1, 5)));
    p(...out);
  }

// the default zone: UTC (the engine's local time), resolved as UTC; a formatter's zone kept
const d = new Date(Date.UTC(2025, 6, 4, 18, 5, 9));
p(new Intl.DateTimeFormat().resolvedOptions().timeZone, new Intl.DateTimeFormat("en", { timeZone: undefined }).resolvedOptions().timeZone,
  d.toLocaleString("en-US"), d.toLocaleString("en-US", { timeZoneName: "long" }), d.getHours(), d.getTimezoneOffset(), d.toString());
const f = new Intl.DateTimeFormat("en", { timeZone: "Asia/Kolkata", timeStyle: "long" });
p(f.format(d), f.resolvedOptions().timeZone, JSON.stringify(f.resolvedOptions()), f.format(d) === new Intl.DateTimeFormat("en", { timeZone: "asia/calcutta", timeStyle: "long" }).format(d));
for (const z of ["Asia/Kolkata", "US/Pacific", "europe/berlin", "America/Argentina/ComodRivadavia", "Antarctica/South_Pole", "IST", "VST", "EAT", "Atlantic/Jan_Mayen", "CST6CDT", "EST", "Navajo"]) {
  const g = new Intl.DateTimeFormat("en", { timeZone: z, timeZoneName: "long", hour: "numeric" });
  p(z, g.resolvedOptions().timeZone, g.format(Date.UTC(2025, 0, 15, 12)), g.format(Date.UTC(2025, 6, 15, 12)));
}
