// tests/js/dateutc.js -- Date.UTC and the Date constructor's parts (ECMA-262 21.4.1:
// MakeDay, MakeTime, TimeClip): defaults, two-digit years, months past December and
// before January, fractions truncated, values not finite, the range's ends.
const show = (label, v) => console.log(label, Object.is(v, NaN) ? 'NaN' : v);
show('UTC()', Date.UTC());
show('UTC(2020)', Date.UTC(2020));
show('UTC(2020, 0)', Date.UTC(2020, 0));
show('UTC(2020, 1, 29, 12, 30, 15, 250)', Date.UTC(2020, 1, 29, 12, 30, 15, 250));
show('UTC(99, 11, 31)', Date.UTC(99, 11, 31));
show('UTC(100, 0)', Date.UTC(100, 0));
show('UTC(0, 0)', Date.UTC(0, 0));
show('UTC(-1, 0)', Date.UTC(-1, 0));
show('UTC(2020, 12)', Date.UTC(2020, 12));
show('UTC(2020, -1)', Date.UTC(2020, -1));
show('UTC(2020, 0, 0)', Date.UTC(2020, 0, 0));
show('UTC(2020.9, 1.7, 2.9)', Date.UTC(2020.9, 1.7, 2.9));
show('UTC(2020, NaN)', Date.UTC(2020, NaN));
show('UTC(Infinity)', Date.UTC(Infinity));
show('UTC(275760, 8, 13)', Date.UTC(275760, 8, 13));
show('UTC(275760, 8, 13, 0, 0, 0, 1)', Date.UTC(275760, 8, 13, 0, 0, 0, 1));
show('UTC(-271821, 3, 20)', Date.UTC(-271821, 3, 20));
show("UTC('2001', '2')", Date.UTC('2001', '2'));
show('new Date(5, 1).getTime()', new Date(Date.UTC(5, 1)).getTime());
show('new Date(2001, 1, 2.5).getTime() - offset', new Date(2001, 1, 2.5).getTime() - new Date(2001, 1, 2).getTime());
console.log(Date.UTC.length, Date.UTC.name);
