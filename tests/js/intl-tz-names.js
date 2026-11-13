// Intl's time zone names (12ao), as node 22 (V8, ICU 78.3 over CLDR 48) writes them: each
// timeZoneName style in each of the engine's 29 languages and some of their regions, for
// zones of the US, Europe, India, China, Australia (its half hours), Nepal (its quarter
// hour) and zones whose offsets or rules changed of late, in winter and in summer, then
// at dates before and between their metazones' changes: CLDR's metazone names, a zone's
// own, the location format ("New York (Ortszeit)"), the localized GMT format, and ICU's
// choices among them (the generic name's standard one where a zone keeps no DST, its
// partial location where its offsets are not the metazone's golden zone's).
const p = (...a) => console.log(a.join(" | "));
// (Thai and Persian with the Gregorian calendar, which the engine formats in: their zones'
// names are their own)
const LANGS = "en de fr es it pt nl ru pl ja zh ko hi ar he tr sv da nb no fi cs uk el hu ro id vi th-u-ca-gregory fa-u-ca-gregory".split(" ");
const STYLES = ["short", "long", "shortOffset", "longOffset", "shortGeneric", "longGeneric"];
const WINTER = Date.UTC(2025, 0, 15, 12), SUMMER = Date.UTC(2025, 6, 15, 12);
const CORE = ["America/New_York", "America/Chicago", "America/Denver", "America/Phoenix", "America/Los_Angeles",
  "America/Anchorage", "Pacific/Honolulu", "Europe/London", "Europe/Dublin", "Europe/Paris", "Europe/Berlin", "Europe/Moscow",
  "Asia/Kolkata", "Asia/Shanghai", "Asia/Tokyo", "Australia/Adelaide", "Australia/Sydney", "Australia/Lord_Howe",
  "Asia/Kathmandu", "Asia/Tehran", "America/Mexico_City", "Asia/Almaty", "America/Nuuk", "Africa/Casablanca",
  "America/Sao_Paulo", "UTC", "Etc/GMT0", "Etc/GMT-10", "+05:45"];
const MORE = ["America/Halifax", "America/St_Johns", "America/Regina", "America/Indiana/Knox", "America/Indianapolis",
  "America/Kentucky/Louisville", "America/Detroit", "America/Boise", "America/Edmonton", "America/Vancouver", "America/Winnipeg",
  "America/Whitehorse", "America/Havana", "America/Bogota", "America/Caracas", "America/Santiago", "America/Punta_Arenas",
  "America/Buenos_Aires", "America/Montevideo", "America/Noronha", "America/Asuncion", "Atlantic/Azores", "Atlantic/Reykjavik",
  "Europe/Lisbon", "Europe/Madrid", "Europe/Zurich", "Europe/Rome", "Europe/Athens", "Europe/Kiev", "Europe/Istanbul",
  "Europe/Minsk", "Europe/Volgograd", "Europe/Samara", "Africa/Cairo", "Africa/Johannesburg", "Africa/Lagos", "Africa/Nairobi",
  "Africa/Windhoek", "Asia/Jerusalem", "Asia/Gaza", "Asia/Dubai", "Asia/Karachi", "Asia/Colombo", "Asia/Dhaka", "Asia/Yangon",
  "Asia/Bangkok", "Asia/Saigon", "Asia/Jakarta", "Asia/Hong_Kong", "Asia/Macau", "Asia/Taipei", "Asia/Manila", "Asia/Seoul",
  "Asia/Pyongyang", "Asia/Urumqi", "Asia/Kuala_Lumpur", "Asia/Singapore", "Asia/Ulaanbaatar", "Asia/Vladivostok",
  "Australia/Perth", "Australia/Eucla", "Australia/Darwin", "Australia/Brisbane", "Australia/Hobart", "Antarctica/Macquarie",
  "Pacific/Auckland", "Pacific/Chatham", "Pacific/Apia", "Pacific/Fiji", "Pacific/Guam", "Pacific/Kiritimati",
  "Pacific/Marquesas", "Pacific/Pago_Pago", "Antarctica/Troll", "Arctic/Longyearbyen", "Europe/Oslo", "Iceland", "EST5EDT",
  "CST6CDT", "SystemV/AST4ADT", "PST", "IST"];
const REGIONS = ["en-GB", "en-AU", "en-IN", "en-CA", "en-NZ", "en-IE", "en-ZA", "en-SG", "es-MX", "es-419", "es-AR", "es-US",
  "fr-CA", "fr-CH", "pt-PT", "de-AT", "de-CH", "it-CH", "nl-BE", "sv-FI", "zh-Hant", "zh-TW", "zh-HK", "zh-SG", "ar-EG",
  "ar-SA", "ar-MA", "ru-UA", "ko-KP", "hi-Latn", "uk-UA", "el-CY", "ja-JP", "fa-AF-u-ca-gregory", "nb-SJ", "no-NO"];
const line = (l, z, dates, styles = STYLES) => {
  const out = [l, z];
  for (const s of styles) {
    let f;
    try { f = new Intl.DateTimeFormat(l, { timeZone: z, timeZoneName: s }); } catch (e) { out.push(e.message); continue; }
    out.push(dates.map(d => f.format(d)).join(" / "));
  }
  p(...out);
};
for (const l of LANGS) for (const z of CORE) line(l, z, [WINTER, SUMMER]);
for (const l of ["en", "de", "fr", "ja", "ar"]) for (const z of MORE) line(l, z, [WINTER, SUMMER]);
for (const l of LANGS) for (const z of MORE) {
  const f = new Intl.DateTimeFormat(l, { timeZone: z, timeZoneName: "longGeneric" }), g = new Intl.DateTimeFormat(l, { timeZone: z, timeZoneName: "short" });
  p(l, z, f.format(SUMMER), g.format(WINTER), g.format(SUMMER));
}
for (const l of REGIONS) for (const z of CORE.slice(0, 16)) line(l, z, [WINTER, SUMMER]);

// before 1970 (no metazone), at metazones' changes, in the years a zone kept other rules
const HIST = [Date.UTC(1890, 5, 1), Date.UTC(1944, 0, 1), Date.UTC(1960, 0, 15), Date.UTC(1969, 11, 31, 23, 59, 59),
  Date.UTC(1970, 0, 1), Date.UTC(1975, 6, 1), Date.UTC(1985, 0, 15), Date.UTC(1991, 9, 27, 7), Date.UTC(2000, 6, 1),
  Date.UTC(2006, 3, 2, 7), Date.UTC(2007, 0, 15), Date.UTC(2011, 11, 31), Date.UTC(2016, 2, 27), Date.UTC(2022, 9, 30),
  Date.UTC(2026, 9, 1), Date.UTC(2026, 10, 2), Date.UTC(2040, 0, 15), Date.UTC(2100, 6, 15)];
for (const l of ["en", "de", "ja"]) for (const z of ["America/New_York", "America/Indiana/Knox", "America/Indiana/Petersburg",
  "America/Kentucky/Monticello", "America/North_Dakota/Beulah", "Europe/London", "Europe/Dublin", "Europe/Lisbon", "Europe/Moscow",
  "Europe/Kaliningrad", "Asia/Kolkata", "Asia/Calcutta", "Asia/Dhaka", "Asia/Kathmandu", "Asia/Shanghai", "Asia/Urumqi",
  "Australia/Lord_Howe", "Australia/Broken_Hill", "Pacific/Apia", "Pacific/Fakaofo", "Africa/Casablanca", "Africa/El_Aaiun",
  "Africa/Windhoek", "Africa/Juba", "America/Edmonton", "America/Vancouver", "Asia/Tehran", "Asia/Pyongyang", "Pacific/Kosrae",
  "America/Metlakatla", "Antarctica/Casey", "Asia/Choibalsan", "Europe/Simferopol", "America/Caracas"])
  line(l, z, HIST, ["short", "long", "shortGeneric", "longGeneric"]);

// the location format when the zone is its country's only one, its primary one, one of
// many; the exemplar cities CLDR names and those the name gives
for (const l of LANGS) {
  const out = [l];
  for (const z of ["Europe/Paris", "Europe/Berlin", "Europe/Busingen", "Asia/Shanghai", "Asia/Urumqi", "America/Santiago",
    "Pacific/Easter", "Europe/Kiev", "Europe/Simferopol", "Asia/Tashkent", "Asia/Samarkand", "Pacific/Majuro", "Pacific/Kwajalein",
    "America/Guayaquil", "Pacific/Galapagos", "Europe/Madrid", "Africa/Ceuta", "Atlantic/Canary", "America/Argentina/Rio_Gallegos",
    "Antarctica/DumontDUrville", "America/Port-au-Prince", "America/Coral_Harbour", "Asia/Ust-Nera", "America/North_Dakota/New_Salem"])
    out.push(new Intl.DateTimeFormat(l, { timeZone: z, timeZoneName: "shortGeneric" }).format(WINTER).replace(/^[^,]*, |^\S+ /, ""));
  p(...out);
}
// the names in the date and time styles' patterns, and with the other fields
for (const l of LANGS) for (const z of ["America/Los_Angeles", "Asia/Kolkata", "Europe/Berlin"]) {
  const out = [l, z];
  for (const s of ["full", "long"]) out.push(new Intl.DateTimeFormat(l, { timeZone: z, timeStyle: s, dateStyle: "short" }).format(SUMMER));
  out.push(new Intl.DateTimeFormat(l, { timeZone: z, hour: "numeric", minute: "2-digit", timeZoneName: "shortGeneric" }).format(WINTER));
  out.push(new Intl.DateTimeFormat(l, { timeZone: z, weekday: "long", hour: "numeric", timeZoneName: "long" }).format(WINTER));
  p(...out);
}
