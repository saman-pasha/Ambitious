// Intl's time zones by IANA name (12ao), as node 22 (V8, ICU 78.3, tzdata 2026e) has them:
// every name ICU knows (the IANA database's zones and links and ICU's own) resolved in
// four cases, names it does not know, and every zone of a country (supportedValuesOf's)
// at a winter and a summer date and at its last change of offset before November 2026,
// found by the formatter's own fields (each second in the zone's local time).
const p = (...a) => console.log(a.join(" | "));
const NAMES = [
  "ACT AET AGT ART AST Africa/Abidjan Africa/Accra Africa/Addis_Ababa Africa/Algiers Africa/Asmara",
  "Africa/Asmera Africa/Bamako Africa/Bangui Africa/Banjul Africa/Bissau Africa/Blantyre Africa/Brazzaville",
  "Africa/Bujumbura Africa/Cairo Africa/Casablanca Africa/Ceuta Africa/Conakry Africa/Dakar",
  "Africa/Dar_es_Salaam Africa/Djibouti Africa/Douala Africa/El_Aaiun Africa/Freetown Africa/Gaborone",
  "Africa/Harare Africa/Johannesburg Africa/Juba Africa/Kampala Africa/Khartoum Africa/Kigali",
  "Africa/Kinshasa Africa/Lagos Africa/Libreville Africa/Lome Africa/Luanda Africa/Lubumbashi Africa/Lusaka",
  "Africa/Malabo Africa/Maputo Africa/Maseru Africa/Mbabane Africa/Mogadishu Africa/Monrovia Africa/Nairobi",
  "Africa/Ndjamena Africa/Niamey Africa/Nouakchott Africa/Ouagadougou Africa/Porto-Novo Africa/Sao_Tome",
  "Africa/Timbuktu Africa/Tripoli Africa/Tunis Africa/Windhoek America/Adak America/Anchorage",
  "America/Anguilla America/Antigua America/Araguaina America/Argentina/Buenos_Aires",
  "America/Argentina/Catamarca America/Argentina/ComodRivadavia America/Argentina/Cordoba",
  "America/Argentina/Jujuy America/Argentina/La_Rioja America/Argentina/Mendoza",
  "America/Argentina/Rio_Gallegos America/Argentina/Salta America/Argentina/San_Juan",
  "America/Argentina/San_Luis America/Argentina/Tucuman America/Argentina/Ushuaia America/Aruba",
  "America/Asuncion America/Atikokan America/Atka America/Bahia America/Bahia_Banderas America/Barbados",
  "America/Belem America/Belize America/Blanc-Sablon America/Boa_Vista America/Bogota America/Boise",
  "America/Buenos_Aires America/Cambridge_Bay America/Campo_Grande America/Cancun America/Caracas",
  "America/Catamarca America/Cayenne America/Cayman America/Chicago America/Chihuahua America/Ciudad_Juarez",
  "America/Coral_Harbour America/Cordoba America/Costa_Rica America/Coyhaique America/Creston",
  "America/Cuiaba America/Curacao America/Danmarkshavn America/Dawson America/Dawson_Creek America/Denver",
  "America/Detroit America/Dominica America/Edmonton America/Eirunepe America/El_Salvador America/Ensenada",
  "America/Fort_Nelson America/Fort_Wayne America/Fortaleza America/Glace_Bay America/Godthab",
  "America/Goose_Bay America/Grand_Turk America/Grenada America/Guadeloupe America/Guatemala",
  "America/Guayaquil America/Guyana America/Halifax America/Havana America/Hermosillo",
  "America/Indiana/Indianapolis America/Indiana/Knox America/Indiana/Marengo America/Indiana/Petersburg",
  "America/Indiana/Tell_City America/Indiana/Vevay America/Indiana/Vincennes America/Indiana/Winamac",
  "America/Indianapolis America/Inuvik America/Iqaluit America/Jamaica America/Jujuy America/Juneau",
  "America/Kentucky/Louisville America/Kentucky/Monticello America/Knox_IN America/Kralendijk",
  "America/La_Paz America/Lima America/Los_Angeles America/Louisville America/Lower_Princes America/Maceio",
  "America/Managua America/Manaus America/Marigot America/Martinique America/Matamoros America/Mazatlan",
  "America/Mendoza America/Menominee America/Merida America/Metlakatla America/Mexico_City America/Miquelon",
  "America/Moncton America/Monterrey America/Montevideo America/Montreal America/Montserrat America/Nassau",
  "America/New_York America/Nipigon America/Nome America/Noronha America/North_Dakota/Beulah",
  "America/North_Dakota/Center America/North_Dakota/New_Salem America/Nuuk America/Ojinaga America/Panama",
  "America/Pangnirtung America/Paramaribo America/Phoenix America/Port-au-Prince America/Port_of_Spain",
  "America/Porto_Acre America/Porto_Velho America/Puerto_Rico America/Punta_Arenas America/Rainy_River",
  "America/Rankin_Inlet America/Recife America/Regina America/Resolute America/Rio_Branco America/Rosario",
  "America/Santa_Isabel America/Santarem America/Santiago America/Santo_Domingo America/Sao_Paulo",
  "America/Scoresbysund America/Shiprock America/Sitka America/St_Barthelemy America/St_Johns",
  "America/St_Kitts America/St_Lucia America/St_Thomas America/St_Vincent America/Swift_Current",
  "America/Tegucigalpa America/Thule America/Thunder_Bay America/Tijuana America/Toronto America/Tortola",
  "America/Vancouver America/Virgin America/Whitehorse America/Winnipeg America/Yakutat America/Yellowknife",
  "Antarctica/Casey Antarctica/Davis Antarctica/DumontDUrville Antarctica/Macquarie Antarctica/Mawson",
  "Antarctica/McMurdo Antarctica/Palmer Antarctica/Rothera Antarctica/South_Pole Antarctica/Syowa",
  "Antarctica/Troll Antarctica/Vostok Arctic/Longyearbyen Asia/Aden Asia/Almaty Asia/Amman Asia/Anadyr",
  "Asia/Aqtau Asia/Aqtobe Asia/Ashgabat Asia/Ashkhabad Asia/Atyrau Asia/Baghdad Asia/Bahrain Asia/Baku",
  "Asia/Bangkok Asia/Barnaul Asia/Beirut Asia/Bishkek Asia/Brunei Asia/Calcutta Asia/Chita Asia/Choibalsan",
  "Asia/Chongqing Asia/Chungking Asia/Colombo Asia/Dacca Asia/Damascus Asia/Dhaka Asia/Dili Asia/Dubai",
  "Asia/Dushanbe Asia/Famagusta Asia/Gaza Asia/Harbin Asia/Hebron Asia/Ho_Chi_Minh Asia/Hong_Kong Asia/Hovd",
  "Asia/Irkutsk Asia/Istanbul Asia/Jakarta Asia/Jayapura Asia/Jerusalem Asia/Kabul Asia/Kamchatka",
  "Asia/Karachi Asia/Kashgar Asia/Kathmandu Asia/Katmandu Asia/Khandyga Asia/Kolkata Asia/Krasnoyarsk",
  "Asia/Kuala_Lumpur Asia/Kuching Asia/Kuwait Asia/Macao Asia/Macau Asia/Magadan Asia/Makassar Asia/Manila",
  "Asia/Muscat Asia/Nicosia Asia/Novokuznetsk Asia/Novosibirsk Asia/Omsk Asia/Oral Asia/Phnom_Penh",
  "Asia/Pontianak Asia/Pyongyang Asia/Qatar Asia/Qostanay Asia/Qyzylorda Asia/Rangoon Asia/Riyadh",
  "Asia/Saigon Asia/Sakhalin Asia/Samarkand Asia/Seoul Asia/Shanghai Asia/Singapore Asia/Srednekolymsk",
  "Asia/Taipei Asia/Tashkent Asia/Tbilisi Asia/Tehran Asia/Tel_Aviv Asia/Thimbu Asia/Thimphu Asia/Tokyo",
  "Asia/Tomsk Asia/Ujung_Pandang Asia/Ulaanbaatar Asia/Ulan_Bator Asia/Urumqi Asia/Ust-Nera Asia/Vientiane",
  "Asia/Vladivostok Asia/Yakutsk Asia/Yangon Asia/Yekaterinburg Asia/Yerevan Atlantic/Azores",
  "Atlantic/Bermuda Atlantic/Canary Atlantic/Cape_Verde Atlantic/Faeroe Atlantic/Faroe Atlantic/Jan_Mayen",
  "Atlantic/Madeira Atlantic/Reykjavik Atlantic/South_Georgia Atlantic/St_Helena Atlantic/Stanley",
  "Australia/ACT Australia/Adelaide Australia/Brisbane Australia/Broken_Hill Australia/Canberra",
  "Australia/Currie Australia/Darwin Australia/Eucla Australia/Hobart Australia/LHI Australia/Lindeman",
  "Australia/Lord_Howe Australia/Melbourne Australia/NSW Australia/North Australia/Perth",
  "Australia/Queensland Australia/South Australia/Sydney Australia/Tasmania Australia/Victoria",
  "Australia/West Australia/Yancowinna BET BST Brazil/Acre Brazil/DeNoronha Brazil/East Brazil/West CAT CET",
  "CNT CST CST6CDT CTT Canada/Atlantic Canada/Central Canada/East-Saskatchewan Canada/Eastern",
  "Canada/Mountain Canada/Newfoundland Canada/Pacific Canada/Saskatchewan Canada/Yukon Chile/Continental",
  "Chile/EasterIsland Cuba EAT ECT EET EST EST5EDT Egypt Eire Etc/GMT Etc/GMT+0 Etc/GMT+1 Etc/GMT+10",
  "Etc/GMT+11 Etc/GMT+12 Etc/GMT+2 Etc/GMT+3 Etc/GMT+4 Etc/GMT+5 Etc/GMT+6 Etc/GMT+7 Etc/GMT+8 Etc/GMT+9",
  "Etc/GMT-0 Etc/GMT-1 Etc/GMT-10 Etc/GMT-11 Etc/GMT-12 Etc/GMT-13 Etc/GMT-14 Etc/GMT-2 Etc/GMT-3 Etc/GMT-4",
  "Etc/GMT-5 Etc/GMT-6 Etc/GMT-7 Etc/GMT-8 Etc/GMT-9 Etc/GMT0 Etc/Greenwich Etc/UCT Etc/UTC Etc/Universal",
  "Etc/Unknown Etc/Zulu Europe/Amsterdam Europe/Andorra Europe/Astrakhan Europe/Athens Europe/Belfast",
  "Europe/Belgrade Europe/Berlin Europe/Bratislava Europe/Brussels Europe/Bucharest Europe/Budapest",
  "Europe/Busingen Europe/Chisinau Europe/Copenhagen Europe/Dublin Europe/Gibraltar Europe/Guernsey",
  "Europe/Helsinki Europe/Isle_of_Man Europe/Istanbul Europe/Jersey Europe/Kaliningrad Europe/Kiev",
  "Europe/Kirov Europe/Kyiv Europe/Lisbon Europe/Ljubljana Europe/London Europe/Luxembourg Europe/Madrid",
  "Europe/Malta Europe/Mariehamn Europe/Minsk Europe/Monaco Europe/Moscow Europe/Nicosia Europe/Oslo",
  "Europe/Paris Europe/Podgorica Europe/Prague Europe/Riga Europe/Rome Europe/Samara Europe/San_Marino",
  "Europe/Sarajevo Europe/Saratov Europe/Simferopol Europe/Skopje Europe/Sofia Europe/Stockholm",
  "Europe/Tallinn Europe/Tirane Europe/Tiraspol Europe/Ulyanovsk Europe/Uzhgorod Europe/Vaduz",
  "Europe/Vatican Europe/Vienna Europe/Vilnius Europe/Volgograd Europe/Warsaw Europe/Zagreb",
  "Europe/Zaporozhye Europe/Zurich Factory GB GB-Eire GMT GMT+0 GMT-0 GMT0 Greenwich HST Hongkong IET IST",
  "Iceland Indian/Antananarivo Indian/Chagos Indian/Christmas Indian/Cocos Indian/Comoro Indian/Kerguelen",
  "Indian/Mahe Indian/Maldives Indian/Mauritius Indian/Mayotte Indian/Reunion Iran Israel JST Jamaica Japan",
  "Kwajalein Libya MET MIT MST MST7MDT Mexico/BajaNorte Mexico/BajaSur Mexico/General NET NST NZ NZ-CHAT",
  "Navajo PLT PNT PRC PRT PST PST8PDT Pacific/Apia Pacific/Auckland Pacific/Bougainville Pacific/Chatham",
  "Pacific/Chuuk Pacific/Easter Pacific/Efate Pacific/Enderbury Pacific/Fakaofo Pacific/Fiji",
  "Pacific/Funafuti Pacific/Galapagos Pacific/Gambier Pacific/Guadalcanal Pacific/Guam Pacific/Honolulu",
  "Pacific/Johnston Pacific/Kanton Pacific/Kiritimati Pacific/Kosrae Pacific/Kwajalein Pacific/Majuro",
  "Pacific/Marquesas Pacific/Midway Pacific/Nauru Pacific/Niue Pacific/Norfolk Pacific/Noumea",
  "Pacific/Pago_Pago Pacific/Palau Pacific/Pitcairn Pacific/Pohnpei Pacific/Ponape Pacific/Port_Moresby",
  "Pacific/Rarotonga Pacific/Saipan Pacific/Samoa Pacific/Tahiti Pacific/Tarawa Pacific/Tongatapu",
  "Pacific/Truk Pacific/Wake Pacific/Wallis Pacific/Yap Poland Portugal ROC ROK SST Singapore SystemV/AST4",
  "SystemV/AST4ADT SystemV/CST6 SystemV/CST6CDT SystemV/EST5 SystemV/EST5EDT SystemV/HST10 SystemV/MST7",
  "SystemV/MST7MDT SystemV/PST8 SystemV/PST8PDT SystemV/YST9 SystemV/YST9YDT Turkey UCT US/Alaska",
  "US/Aleutian US/Arizona US/Central US/East-Indiana US/Eastern US/Hawaii US/Indiana-Starke US/Michigan",
  "US/Mountain US/Pacific US/Pacific-New US/Samoa UTC Universal VST W-SU WET Zulu",
].join(" ").split(" ");
const r = z => {
  try { return new Intl.DateTimeFormat("en", { timeZone: z }).resolvedOptions().timeZone; }
  catch (e) { return e.name + ": " + e.message; }
};
const mixed = n => n.split("").map((c, i) => i % 2 ? c.toLowerCase() : c.toUpperCase()).join("");
p(NAMES.length);
for (const n of NAMES) p(n, r(n), r(n.toLowerCase()), r(n.toUpperCase()), r(mixed(n)));
for (const n of ["", " ", "Foo/Bar", "America/New_York ", " America/New_York", "America/New-York", "America/New_york_",
  "Asia/Hanoi", "America/Lord_Howe", "Etc/GMT+05", "Etc/GMT+13", "Etc/GMT-15", "Etc/GMT+0:00", "GMT+5", "UTC+1", "Z",
  "America∕New_York", "Europe/Berlin\0", "Etc/Unknown", "Factory", "factory", "SystemV", "US", "America", "America/",
  "/America/New_York", "America//New_York", "Asia/Riyadh87", "Mideast/Riyadh87", "EST5", "EST5EDT4", "Europe/Kyiv/x",
  "İstanbul", "Europe/İstanbul", "America/São_Paulo", "America/Sao_Pauló"])
  p(JSON.stringify(n), JSON.stringify(r(n)));
for (const n of ["America/New_York", "Asia/Kolkata", "EST5EDT", "UTC", "Etc/GMT+5", "+05:30"])
  p(n, r(new String(n)), r({ toString() { return n; } }));
try { new Intl.DateTimeFormat("en", { timeZone: Symbol() }); } catch (e) { p(e.name, e.message); }
const zones = Intl.supportedValuesOf("timeZone");
p(zones.length, zones.join(" "));
p(zones.every(z => r(z) === z), Intl.supportedValuesOf("timeZone") !== zones);
// supportedValuesOf's other keys
for (const k of ["calendar", "collation", "currency", "numberingSystem", "unit"]) {
  const v = Intl.supportedValuesOf(k);
  p(k, v.length, Array.isArray(v), v.join(" "));
}
for (const k of ["foo", undefined, null, "", "Calendar", "timezone", "timeZones", 1, { toString() { return "unit"; } }]) {
  try { p(String(k), Intl.supportedValuesOf(k).length); } catch (e) { p(String(k), e.name, e.message); }
}
for (const fn of [() => Intl.supportedValuesOf(Symbol()), () => new Intl.supportedValuesOf("unit"), () => Intl.supportedValuesOf()])
  try { p(fn()); } catch (e) { p(e.name, e.message); }
p(Intl.supportedValuesOf.length, Intl.supportedValuesOf.name, JSON.stringify(Object.getOwnPropertyDescriptor(Intl, "supportedValuesOf")),
  typeof Intl.supportedValuesOf.prototype, Intl.supportedValuesOf.call(42, "calendar").length);

const T0 = Date.UTC(2026, 10, 1), STEP = 14 * 86400000, LO = Date.UTC(1900, 0, 1);
const opts = { year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit",
  hourCycle: "h23" };
function offsetOf(f, t) {
  const v = {};
  for (const q of f.formatToParts(t)) v[q.type] = q.value;
  return Date.UTC(+v.year, +v.month - 1, +v.day, +v.hour, +v.minute, +v.second) - t;
}
const hm = ms => {
  const a = Math.abs(ms) / 1000, s = a % 60, m = Math.floor(a / 60) % 60, h = Math.floor(a / 3600);
  return (ms < 0 ? "-" : "+") + h + ":" + String(m).padStart(2, "0") + (s ? ":" + String(s).padStart(2, "0") : "");
};
for (const z of zones) {
  const f = new Intl.DateTimeFormat("en-US", { timeZone: z, ...opts });
  const g = new Intl.DateTimeFormat("en-US", { timeZone: z, ...opts, timeZoneName: "longOffset" });
  const out = [z, g.format(Date.UTC(2025, 0, 15, 12)), g.format(Date.UTC(2025, 6, 15, 12))];
  let hi = T0, o = offsetOf(f, hi), lo = hi - STEP;
  while (lo > LO && offsetOf(f, lo) === o) { hi = lo; lo -= STEP; }
  if (lo > LO) {
    while (hi - lo > 1000) {
      const mid = lo + Math.floor((hi - lo) / 2000) * 1000;
      if (offsetOf(f, mid) === o) hi = mid; else lo = mid;
    }
    out.push(new Date(hi).toISOString(), hm(offsetOf(f, lo)) + " to " + hm(o), g.format(lo), g.format(hi));
  } else out.push("none since 1900");
  p(...out);
}
