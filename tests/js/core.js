// tests/js/core.js -- the engine's semantics, each line printed against V8's (tests/js/core.expect).
// The fast paths and their slow ones both: numbers and what is not one.
var out = [];
function p() { var s = ''; for (var i = 0; i < arguments.length; i++) s += (i ? ' ' : '') + String(arguments[i]); console.log(s); }

// arithmetic with an integer, a local with an integer, two locals
function arith(a, b) { return [a + 1, a - 1, a * 3, a % 3, a + b, a - b, a * b, a % b].join(','); }
p('arith', arith(7, 2));
p('arith', arith(-7, 2));
p('arith', arith('7', 2));
p('arith', arith(7.5, '2'));
p('arith', arith(true, null));
p('arith', arith(undefined, 1));
p('arith', arith({ valueOf: function () { return 10; } }, 4));
p('mod', 5 % 5, -5 % 5, 1 / (-5 % 5), 1 / (5 % 5), 7 % -3, -7 % -3, 5.5 % 2, 2147483648 % 7, -2147483648 % -1, 1 / (-2147483648 % -1));
p('mod', 9007199254740991 % 10, 1e20 % 7, NaN % 2, 2 % NaN, Infinity % 2, 2 % Infinity, 3 % 0, -0 % 3, 1 / (-0 % 3));
var big = 1000003, acc = 0;
for (var i = 0; i < 1000; i++) acc = (acc + i * i) % big;
p('loop', acc);

// comparisons and their jumps: numbers, NaN, strings, objects, mixed
function rel(a, b) {
  var r = [];
  if (a < b) r.push('lt'); else r.push('!lt');
  if (a <= b) r.push('le'); else r.push('!le');
  if (a > b) r.push('gt'); else r.push('!gt');
  if (a >= b) r.push('ge'); else r.push('!ge');
  if (a === b) r.push('seq'); else r.push('!seq');
  if (a !== b) r.push('sne'); else r.push('!sne');
  if (a == b) r.push('eq'); else r.push('!eq');
  if (a != b) r.push('ne'); else r.push('!ne');
  r.push(a < b, a <= b, a > b, a >= b);
  return r.join(' ');
}
p(rel(1, 2)); p(rel(2, 2)); p(rel(NaN, 1)); p(rel(1, NaN)); p(rel('a', 'b')); p(rel('10', 9)); p(rel('abc', 'abc'));
p(rel(null, undefined)); p(rel(null, 0)); p(rel(undefined, 0)); p(rel(0, -0)); p(rel('1', 1)); p(rel(true, 1));
var o1 = {}, o2 = {};
p(rel(o1, o1)); p(rel(o1, o2)); p(rel({ valueOf: function () { return 3; } }, 2));
function lim(n) { var r = []; if (n < 2) r.push('a'); if (n <= 2) r.push('b'); if (n > 2) r.push('c'); if (n >= 2) r.push('d'); return r.join(''); }
p('lim', lim(1), lim(2), lim(3), lim(NaN), lim('2'), lim('x'), lim(null), lim(undefined), lim(2.5));
function down(n) { var c = 0; while (n > 0) { n--; c++; } return c; }
p('down', down(5), down(0), down(-3), down(2.5), down('3'));

// loops: rotated, with continue, break, labels, closures per iteration
var s = '';
for (var j = 0; j < 10; j++) { if (j % 2) continue; if (j > 6) break; s += j; }
p('for', s);
s = '';
var k = 0;
while (k < 10) { k++; if (k === 3) continue; if (k === 8) break; s += k; }
p('while', s);
s = '';
k = 0;
do { s += k; k++; } while (k < 4);
p('do', s);
s = '';
outer: for (var a = 0; a < 3; a++) { for (var b = 0; b < 3; b++) { if (b === 2) continue outer; if (a === 2) break outer; s += a + '' + b + ' '; } }
p('labels', s);
var fns = [];
for (let q = 0; q < 3; q++) fns.push(function () { return q; });
p('let-loop', fns.map(function (f) { return f(); }).join(','));
k = 0;
while (true) { k++; if (k > 4) break; }
p('forever', k);
for (;;) { k++; if (k > 9) break; }
p('for-ever', k);
var n0 = 0;
for (var z = 0; z < 0; z++) n0++;
p('empty-for', n0, z);

// returns: a conditional's branches, a local, in a finally
function sign(x) { return x > 0 ? 'pos' : x < 0 ? 'neg' : x === 0 ? 'zero' : 'nan'; }
p('sign', sign(3), sign(-2), sign(0), sign(NaN));
function retloc(x) { var y = x * 2; return y; }
p('retloc', retloc(21), retloc('a'));
function fin() { try { return 'try'; } finally { p('finally ran'); } }
p('fin', fin());
function fin2(x) { try { if (x) return x ? 'yes' : 'no'; } finally { p('finally2'); } return 'after'; }
p('fin2', fin2(1), fin2(0));

// calls: plain (no this), methods, natives, bound, this in sloppy and strict code
function who() { return this === undefined ? 'undefined' : this === globalThis ? 'global' : typeof this; }
function whoStrict() { 'use strict'; return this === undefined ? 'undefined' : typeof this; }
var holder = { who: who, whoStrict: whoStrict };
p('this', who(), whoStrict(), holder.who(), holder.whoStrict(), who.call(5), whoStrict.call(5));
var bound = who.bind('s');
p('bound', bound(), typeof Math.max(1, 2), Math.max(3, 9, 4), parseInt('42px'), String(12));
function args() { return arguments.length + ':' + Array.prototype.join.call(arguments, '-'); }
p('arguments', args(), args(1), args(1, 2, 3));
function rest(a, ...r) { return a + '|' + r.join(','); }
p('rest', rest(1), rest(1, 2, 3));
function defaults(a, b = a + 1) { return a + b; }
p('defaults', defaults(1), defaults(1, 5));
function few(a, b, c) { return [a, b, c].join('/'); }
p('few', few(1), few(1, 2, 3, 4));
var topThis = this;
var arrow = () => this === topThis;
p('arrow-this', arrow());
var objA = { n: 3, get: function () { var f = () => this.n; return f(); } };
p('arrow-method', objA.get());
class K { constructor(v) { this.v = v; } m() { return this.v; } static s() { return 'static'; } }
p('class', new K(4).m(), K.s());
try { K(1); } catch (e) { p('class-call', e instanceof TypeError); }
try { undefined(); } catch (e) { p('not-fn', e instanceof TypeError); }
try { var nf = 3; nf(); } catch (e) { p('not-fn2', e instanceof TypeError); }
function rec(n) { return n < 2 ? n : rec(n - 1) + rec(n - 2); }
p('rec', rec(15));
var fact = function f(n) { return n <= 1 ? 1 : n * f(n - 1); };
p('fact', fact(10));
var deep = 0;
function down2(n) { if (n === 0) return 0; deep++; return down2(n - 1); }
down2(5000);
p('deep', deep);
try { (function inf() { inf(); })(); } catch (e) { p('overflow', e instanceof RangeError); }

// callbacks from natives: map, filter, reduce, sort, forEach, an exception through one
var arr = [5, 3, 8, 1, 9, 2];
p('map', arr.map(function (x) { return x * 2; }).join(','));
p('filter', arr.filter(function (x) { return x % 2 === 1; }).join(','));
p('reduce', arr.reduce(function (s, x) { return s + x; }, 0), arr.reduce(function (s, x) { return s + x; }));
p('sort', arr.slice().sort(function (x, y) { return x - y; }).join(','), arr.slice().sort().join(','));
var each = 0;
arr.forEach(function (x, i) { each += x * i; });
p('forEach', each);
try { arr.map(function (x) { if (x === 8) throw new Error('at 8'); return x; }); } catch (e) { p('thrown', e.message); }
p('find', arr.find(function (x) { return x > 5; }), arr.findIndex(function (x) { return x > 100; }), arr.some(function (x) { return x > 8; }), arr.every(function (x) { return x > 0; }));
var ctx = { k: 10 };
p('thisArg', [1, 2].map(function (x) { return x + this.k; }, ctx).join(','));
p('arrow-cb', [1, 2, 3].map(x => x * x).join(','));

// closures: counters, shared cells, captured parameters
function counter() { var c = 0; return { inc: function (k) { c += k; return c; }, get: function () { return c; } }; }
var ca = counter(), cb = counter();
ca.inc(2); ca.inc(3); cb.inc(10);
p('counter', ca.get(), cb.get());
function adder(x) { return function (y) { return x + y; }; }
p('adder', adder(3)(4), adder('a')('b'));
var twice = function (f) { return function (x) { return f(f(x)); }; };
p('twice', twice(function (x) { return x + 1; })(5), twice(twice(function (x) { return x * 2; }))(1));

// strings
p('str', 'a' + 1, 1 + 'a', 'x' + null, 'ab'.length, ('p' + 7).length, 'abc'.charCodeAt(1), String.fromCharCode(72, 105));
var t = '';
for (var c = 0; c < 5; c++) t += String.fromCharCode(97 + c);
p('build', t, t.toUpperCase(), t.indexOf('c'), t.split('').reverse().join(''));
p('template', `${1 + 1}:${'x'.repeat(3)}`);

// numbers printed
p('num', 0.1 + 0.2, 1 / 3, 1e21, 1e-7, -0, 123456789012345680000, 2 ** 53, (0.000001234).toString(), (255).toString(16), (3.14159).toFixed(2));
p('int', 7 | 0, -7 >> 1, -7 >>> 28, 1 << 31, ~5, 5 & 3, 5 | 3, 5 ^ 3);
p('inc', (function () { var i = 0; i++; ++i; i += 2; i -= 1; i *= 3; i /= 2; i %= 4; return i; })());

// objects and arrays
var obj = { x: 1, y: 2 };
obj.z = obj.x + obj.y;
delete obj.x;
p('obj', Object.keys(obj).join(','), obj.z, 'x' in obj, obj.hasOwnProperty('y'));
var A = [];
for (var m = 0; m < 5; m++) A.push(m * m);
A[7] = 'seven';
p('arr', A.length, A.join('|'), A.indexOf(9), A.slice(1, 3).join(','));
p('json', JSON.stringify({ a: [1, 'b', true, null], c: { d: 1.5 } }), JSON.parse('{"x":[1,2,{"y":"z"}]}').x[2].y);

// strict code: this, and a name nowhere declared
function strictAssign() { 'use strict'; try { undeclaredStrict = 1; return 'assigned'; } catch (e) { return e instanceof ReferenceError; } }
p('strict-assign', strictAssign(), typeof undeclaredStrict);
function sloppyAssign() { undeclaredSloppy = 2; return undeclaredSloppy; }
p('sloppy-assign', sloppyAssign(), typeof undeclaredSloppy);
function outerStrict() { 'use strict'; return (function () { return this; })(); }
p('nested-strict', outerStrict());
function notDirective() { var x = 'use strict'; return (function () { return typeof this; })(); }
p('not-directive', notDirective());
p('boxed', (function () { return typeof this; }).call('s'), (function () { return this instanceof Number; }).call(5));
