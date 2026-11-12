// tests/js/date.js -- Date as V8 has it: parsing (its ES5 date-time strings, then its legacy grammar), the
// setters, the string forms, toJSON, Symbol.toPrimitive and the receivers. The local zone is UTC (node: TZ=UTC).
const t = (s, f) => { try { const r = f(); console.log(s, typeof r === 'object' && r !== null ? JSON.stringify(r) : r); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
const strs = ['2024-01-02', '2024-01-02T03:04:05', '2024-01-02T03:04:05Z', '2024-01-02T03:04:05.678Z', '2024-01-02T03:04:05.6789Z',
  '2024-01-02T03:04:05.6Z', '2024-01-02T03:04:05+01:00', '2024-01-02T03:04:05-0130', '2024-01-02T24:00', '2024-01-02T24:00:01', '2024-01-02T25:00',
  '2024-13-01', '2024-02-30', '2024-02-31T00:00', '+002024-01-02', '-000001-01-01T00:00:00Z', '-000000-01-01', '+275760-09-13T00:00:00Z',
  '+275760-09-13T00:00:00.001Z', '-271821-04-20T00:00:00Z', '-271821-04-19T23:59:59.999Z', '2024', '2024-01', '2024-1-2', '24-01-02',
  '2024-01-02 03:04', '2024-01-02 03:04:05 GMT+0200', '2024-01-02T03:04', '2024-01-02t03:04:05z', '2024-01-02T03:04:05.Z',
  'Tue Jan 02 2024 03:04:05 GMT+0100', 'Tue Jan 02 2024 03:04:05 GMT+0100 (Central European Standard Time)', 'Thu, 01 Jan 1970 00:00:00 GMT',
  'January 2, 2024', 'Jan 2 2024', 'Jan 2', '2 Jan 2024', '2024 Jan 2', 'Jan 2024 2', '1/2/2024', '1/2/24', '1/2/99', '1/2', '12/31/1999 23:59:59',
  '2024/01/02', '2024/1/2 10:00 PM', '10:00 AM Jan 2 2024', 'Jan 2 2024 12:00 am', 'Jan 2 2024 12:30 pm', 'Jan 2 2024 13:00 pm', 'Jan 2 2024 10:00:00.123',
  'Jan 2 2024 10:00 EST', 'Jan 2 2024 10:00 PDT', 'Jan 2 2024 10:00 UTC', 'Jan 2 2024 10:00 Z', 'Jan 2 2024 10:00 UT', 'Jan 2 2024 10:00 GMT-8',
  'Jan 2 2024 10:00 GMT+05:30', 'Jan 2 2024 10:00 +0530', 'Jan 2 2024 10:00:60', 'Jan 32 2024', 'Feb 30 2024', 'Mon Jan 2 2024', 'Monday, January 2, 2024',
  'foo Jan 2 2024', 'Jan 2 2024 foo', '2024-01-02 foo', '', ' ', 'x', 'Invalid Date', 'NaN', '0', '1', '12', '123', '1234', '12345', '20240102',
  '2024-01-02T03:04:05.000+00:00', '  2024-01-02  ', 'Jan-02-2024', '02-Jan-2024', '2024-Jan-02', 'Jan.2.2024', 'Jan 2, 2024 (note) 10:00',
  'Sat, 01-Jan-2000 08:00:00 GMT', 'Wed Dec 31 1969 19:00:00 GMT-0500 (EST)', '1970-01-01T00:00:00.000Z', '9999-12-31T23:59:59.999Z',
  '10000-01-01', '2024-01-02T03:04:05.123456789Z', '2024-01-02T3:04', 'Tue Jan 02 2024', '2024-01-02T03:04:05+24:00', '2024-01-02T03:04:05+23:59'];
for (const s of strs) console.log(JSON.stringify(s), Date.parse(s));
t('ctor parts', () => [new Date(2024, 0, 31, 12, 30).getTime(), new Date(99, 0).getFullYear(), new Date(100, 0).getFullYear(), new Date(-1, 0).getFullYear(),
  new Date(2024, 12).getMonth(), new Date(2024, -1).getFullYear(), new Date(2024, 0, 0).getDate(), new Date(1e6 + 1, 0).getTime(), new Date(2024, 1e7 + 1).getTime(),
  new Date(2024.7, 1.9, 3.9).getTime(), new Date(NaN, 0).getTime(), new Date(2024, 0, 1, 0, 0, 0, -1).getTime()].join());
t('ctor one', () => [new Date(0).getTime(), new Date(8.64e15).getTime(), new Date(8.64e15 + 1).getTime(), new Date(-8.64e15).getTime(), new Date('0').getTime(),
  new Date(new Date(5)).getTime(), new Date(true).getTime(), new Date(null).getTime(), new Date(undefined).getTime(), new Date({ valueOf() { return 7; } }).getTime(),
  new Date({ toString() { return '2024-01-02'; }, valueOf: undefined }).getTime(), new Date(1.9).getTime(), new Date(-1.9).getTime()].join());
t('ctor order', () => { const log = []; new Date({ valueOf() { log.push('a'); return 2024; } }, { valueOf() { log.push('b'); return 1; } }); return log.join(); });
t('called', () => typeof Date() + ' ' + (Date(0) === Date()));
t('UTC', () => [Date.UTC(2024), Date.UTC(2024, 1), Date.UTC(99), Date.UTC(), Date.UTC(2024, 0, 1, 25), Date.UTC(275760, 8, 13), Date.UTC(275760, 8, 13, 0, 0, 0, 1)].join());
const d0 = new Date(Date.UTC(2024, 1, 29, 13, 45, 30, 123));
t('getters', () => [d0.getFullYear(), d0.getMonth(), d0.getDate(), d0.getDay(), d0.getHours(), d0.getMinutes(), d0.getSeconds(), d0.getMilliseconds(),
  d0.getUTCFullYear(), d0.getUTCMonth(), d0.getUTCDate(), d0.getUTCDay(), d0.getUTCHours(), d0.getUTCMinutes(), d0.getUTCSeconds(), d0.getUTCMilliseconds(),
  d0.getTimezoneOffset(), d0.getYear(), d0.valueOf(), d0.getTime()].join());
const bad = new Date(NaN);
t('invalid getters', () => [bad.getFullYear(), bad.getTimezoneOffset(), bad.getYear(), bad.getDay()].join());
t('strings', () => [d0.toString(), d0.toDateString(), d0.toTimeString(), d0.toUTCString(), d0.toGMTString(), d0.toISOString(), d0.toJSON(), String(d0), `${d0}`, d0 + 1, d0 - 1].join(' | '));
t('invalid strings', () => [bad.toString(), bad.toDateString(), bad.toTimeString(), bad.toUTCString(), String(bad), bad.toJSON(), JSON.stringify(bad)].join(' | '));
t('invalid iso', () => bad.toISOString());
t('years', () => [new Date(Date.UTC(-1, 0)).toISOString(), new Date(Date.UTC(10000, 0)).toISOString(), new Date(Date.UTC(-1, 0)).toString(), new Date(Date.UTC(-1, 0)).toUTCString(),
  new Date(Date.UTC(12345, 0)).toString(), new Date(Date.UTC(5, 0)).toISOString(), new Date(-62198755200000).toISOString(), new Date(8.64e15).toISOString(), new Date(-8.64e15).toISOString()].join(' | '));
const S = (f) => { const d = new Date(Date.UTC(2024, 0, 15, 10, 20, 30, 400)); const r = f(d); return r + '=' + d.toISOString(); };
t('setMilliseconds', () => S(d => d.setMilliseconds(999)));
t('setMilliseconds over', () => S(d => d.setMilliseconds(1000)));
t('setSeconds', () => S(d => d.setSeconds(5, 6)));
t('setMinutes', () => S(d => d.setMinutes(1, 2, 3)));
t('setHours', () => S(d => d.setHours(23, 59, 58, 57)));
t('setHours one', () => S(d => d.setHours(-1)));
t('setDate', () => S(d => d.setDate(31)));
t('setDate 0', () => S(d => d.setDate(0)));
t('setMonth', () => S(d => d.setMonth(1, 29)));
t('setMonth 13', () => S(d => d.setMonth(13)));
t('setFullYear', () => S(d => d.setFullYear(2000, 1, 29)));
t('setUTCHours', () => S(d => d.setUTCHours(1)));
t('setUTCFullYear', () => S(d => d.setUTCFullYear(1999)));
t('setTime', () => S(d => d.setTime(86400000)));
t('setTime bad', () => { const d = new Date(0); return d.setTime(9e15) + ' ' + d.getTime(); });
t('setYear', () => S(d => d.setYear(99)));
t('setYear 2000', () => S(d => d.setYear(2000)));
t('setYear NaN', () => { const d = new Date(0); return d.setYear(NaN) + ' ' + d.getTime(); });
t('set no args', () => { const d = new Date(0); return [d.setHours(), d.getTime()].join(); });
t('set on invalid', () => { const d = new Date(NaN); return [d.setHours(1), d.setDate(1), d.setMonth(1), d.getTime(), d.setFullYear(2024), d.getTime()].join(); });
t('set on invalid UTC', () => { const d = new Date(NaN); return [d.setUTCFullYear(2024, 5), d.toISOString()].join(); });
t('setYear on invalid', () => { const d = new Date(NaN); return [d.setYear(5), d.toISOString()].join(); });
t('set order', () => { const log = []; const d = new Date(NaN); d.setHours({ valueOf() { log.push('h'); return 1; } }, { valueOf() { log.push('m'); return 2; } }); return log.join() + ' ' + d.getTime(); });
t('set extra args', () => { const log = []; const d = new Date(0); d.setDate(1, { valueOf() { log.push('x'); return 2; } }); return log.length; });
t('set NaN arg', () => { const d = new Date(0); return [d.setMinutes(NaN), d.getTime()].join(); });
t('toJSON generic', () => Date.prototype.toJSON.call({ toISOString() { return 'iso!'; } }));
t('toJSON nonfinite', () => Date.prototype.toJSON.call({ valueOf() { return Infinity; }, toISOString() { return 'no'; } }));
t('toJSON no iso', () => Date.prototype.toJSON.call({}));
t('toPrimitive', () => [d0[Symbol.toPrimitive]('number'), d0[Symbol.toPrimitive]('default'), d0[Symbol.toPrimitive]('string')].join(' | '));
t('toPrimitive bad hint', () => d0[Symbol.toPrimitive]('foo'));
t('toPrimitive no hint', () => d0[Symbol.toPrimitive]());
t('toPrimitive prim', () => Date.prototype[Symbol.toPrimitive].call(1, 'number'));
t('toPrimitive generic', () => Date.prototype[Symbol.toPrimitive].call({ valueOf() { return 5; }, toString() { return 's'; } }, 'default'));
t('override toPrimitive', () => { class D2 extends Date { [Symbol.toPrimitive](h) { return 'hint:' + h; } } const x = new D2(0); return [x + '', `${x}`, +x === +x ? 'num' : 'nan', x == 'hint:default'].join(); });
t('compare', () => [new Date(1) < new Date(2), new Date(5) == 5, new Date(0) == new Date(0).toString()].join());
t('subclass', () => { class D3 extends Date { y() { return this.getUTCFullYear(); } } return [new D3(0).y(), new D3(0) instanceof Date, Object.prototype.toString.call(new D3(0))].join(); });
t('lengths', () => ['setHours', 'setMinutes', 'setSeconds', 'setMonth', 'setFullYear', 'setUTCHours', 'setDate', 'setTime', 'setYear', 'toJSON', 'getYear', 'toGMTString'].map(k => k + Date.prototype[k].length).join());
t('statics', () => [Date.UTC.length, Date.parse.length, Date.now.length, Date.length, Date.name].join());
t('gmt is utc', () => Date.prototype.toGMTString === Date.prototype.toUTCString);
t('toPrimitive desc', () => { const d = Object.getOwnPropertyDescriptor(Date.prototype, Symbol.toPrimitive); return [d.writable, d.enumerable, d.configurable, d.value.name, d.value.length].join(); });
t('proto names', () => Object.getOwnPropertyNames(Date.prototype).join());
t('date names', () => Object.getOwnPropertyNames(Date).filter(k => k !== 'length' && k !== 'name').join());
for (const m of Object.getOwnPropertyNames(Date.prototype)) {
  if (m === 'constructor') continue;
  try { Date.prototype[m].call({}); console.log(m, 'no throw'); } catch (e) { console.log(m, e.constructor.name, e.message); }
}
t('receiver prim', () => Date.prototype.getTime.call(5));
t('receiver toString prim', () => Date.prototype.toString.call('x'));
t('Date.parse coerce', () => [Date.parse({ toString() { return '2024-01-02'; } }), Date.parse(0), Date.parse(null)].join());
t('JSON round trip', () => JSON.stringify({ d: new Date(0) }));
t('Object.prototype.toString', () => Object.prototype.toString.call(new Date(0)));
