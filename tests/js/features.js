// tests/js/features.js -- the language a page's script uses, each line against V8's (features.expect).
function p() { var s = ''; for (var i = 0; i < arguments.length; i++) s += (i ? ' ' : '') + String(arguments[i]); console.log(s); }

// exceptions: nested, rethrown, finally's order, errors' kinds and messages
var log = [];
function f1() { try { log.push('t'); throw new Error('e1'); } catch (e) { log.push('c:' + e.message); throw e; } finally { log.push('f'); } }
try { f1(); } catch (e) { log.push('outer:' + e.message); }
p('order', log.join(' '));
function f2() { for (var i = 0; i < 3; i++) { try { if (i === 1) continue; if (i === 2) break; log.push('i' + i); } finally { log.push('fin' + i); } } return log.slice(-4).join(' '); }
p('loop-finally', f2());
try { null.x; } catch (e) { p('null-prop', e instanceof TypeError, e.constructor.name); }
try { undefinedName; } catch (e) { p('ref', e instanceof ReferenceError, e.name); }
try { throw { code: 7 }; } catch ({ code }) { p('destructured-catch', code); }
try { throw 'str'; } catch (e) { p('thrown-string', e); }
var e3 = new RangeError('r');
p('error', e3.name, e3.message, String(e3), e3 instanceof Error, Object.prototype.toString.call(e3));
class MyErr extends Error { constructor(m) { super(m); this.name = 'MyErr'; } }
try { throw new MyErr('mine'); } catch (e) { p('subclass', e instanceof MyErr, e instanceof Error, e.name, e.message); }

// objects: getters, setters, computed keys, shorthand, spread, keys order, freeze
var o = { _v: 1, get v() { return this._v * 10; }, set v(x) { this._v = x; }, ['k' + 1]: 'computed', short() { return 'method'; } };
o.v = 4;
p('accessors', o.v, o.k1, o.short());
var a = 1, b = 2;
var sh = { a, b, ...{ c: 3, a: 9 } };
p('spread-obj', JSON.stringify(sh));
var ord = { b: 1, 2: 'two', a: 2, 1: 'one' };
p('key-order', Object.keys(ord).join(','));
var fr = Object.freeze({ x: 1 });
fr.x = 2;
p('frozen', fr.x, Object.isFrozen(fr));
p('entries', Object.entries({ x: 1, y: 2 }).map(function (e) { return e.join('='); }).join('&'), JSON.stringify(Object.assign({}, { a: 1 }, { b: 2 })));
p('in-delete', 'x' in { x: undefined }, delete o.k1, 'k1' in o, o.hasOwnProperty('short'));
p('optional', o?.missing?.deep, o.missing ?? 'dflt', 0 ?? 'zero-kept', null?.x, (void 0)?.y);

// classes: inheritance, super calls and methods, static, getters, fields, instanceof
class Shape { constructor(n) { this.n = n; } area() { return 0; } describe() { return this.n + ':' + this.area(); } static make(n) { return new this(n); } }
class Sq extends Shape { constructor(s) { super('sq'); this.s = s; } area() { return this.s * this.s; } describe() { return 'Sq(' + super.describe() + ')'; } }
class Circle extends Shape { r = 2; get d() { return this.r * 2; } area() { return Math.round(3.14159 * this.r * this.r); } }
p('classes', new Sq(3).describe(), new Circle('c').describe(), new Circle('c').d, Sq.make(5).area(), new Sq(1) instanceof Shape, Object.getPrototypeOf(Sq) === Shape);
class Count { static total = 0; constructor() { Count.total++; } }
new Count(); new Count();
p('static-field', Count.total);

// destructuring, defaults, rest, spread in calls and arrays
var [x1, , x3 = 'd', ...xs] = [1, 2, undefined, 4, 5];
var { q: { r: deepR }, s: sDef = 's!' } = { q: { r: 'deep' } };
p('destructure', x1, x3, xs.join(','), deepR, sDef);
function spreadMe(a, b, c) { return a + b + c; }
p('spread-call', spreadMe(...[1, 2, 3]), Math.max(...[4, 9, 2]), [0, ...[1, 2], 3].join(''));
var [m, n] = [n0 = 5, 6];
p('swap', (function () { var u = 1, w = 2; [u, w] = [w, u]; return u + '' + w; })());

// switch, labels, typeof, comma, conditional chains
function sw(v) { switch (v) { case 1: return 'one'; case '1': return 'str-one'; case 2: case 3: return 'two-three'; default: return 'other'; } }
p('switch', sw(1), sw('1'), sw(3), sw(null));
function fall(v) { var r = ''; switch (v) { case 'a': r += 'a'; case 'b': r += 'b'; break; case 'c': r += 'c'; } return r; }
p('fallthrough', fall('a'), fall('b'), fall('c'), fall('z'));
p('typeof', typeof 1, typeof 'x', typeof undefined, typeof null, typeof {}, typeof [], typeof function () {}, typeof Symbol, typeof undeclaredThing);
p('comma', (1, 2, 3), [1, 2, 3].length > 2 ? 'long' : 'short');

// strings: methods a page uses
var str = '  Hello, World  ';
p('trim', '[' + str.trim() + ']', str.trimStart().length, str.trimEnd().length);
p('case', 'MiXeD'.toLowerCase(), 'MiXeD'.toUpperCase());
p('search', 'banana'.indexOf('an'), 'banana'.lastIndexOf('an'), 'banana'.includes('nan'), 'banana'.startsWith('ba'), 'banana'.endsWith('na'));
p('slice', 'abcdef'.slice(1, 4), 'abcdef'.slice(-2), 'abcdef'.substring(4, 1), 'abcdef'.substr(2, 2), 'abc'.charAt(1), 'abc'.at(-1));
p('split-join', 'a,b,,c'.split(',').length, 'a b c'.split(' ').join('-'), 'abc'.split('').join('|'));
p('replace', 'aaa'.replace('a', 'b'), 'aaa'.replaceAll('a', 'b'), 'x-y-z'.split('-').reverse().join('+'));
p('pad', '5'.padStart(3, '0'), '5'.padEnd(3, '*'), 'ab'.repeat(3), 'é'.length, '😀'.length, 'abc'.codePointAt(0));
p('compare', 'a' < 'b', 'B' < 'a', 'abc' === 'ab' + 'c', 'x'.localeCompare('y'));
var tpl = (s, ...v) => s.raw.join('|') + '#' + v.join(',');
p('tagged', tpl`a${1}b${2}c`);
p('template-nest', `outer ${`inner ${1 + 1}`} end`, `multi
line`.split('\n').length);

// numbers: parsing and printing
p('parse', parseInt('0x1f'), parseInt('12.9'), parseFloat('3.5e2x'), Number('  42 '), Number(''), Number('1e3'), Number('abc'), +true, +[], +[5], +'0b101');
p('fixed', (1.005).toFixed(2), (12345.6789).toFixed(1), (0).toFixed(3), (-1.5).toFixed(0), (1e21).toFixed(2));
p('isnan', isNaN('x'), Number.isNaN('x'), Number.isInteger(5.0), Number.isInteger(5.5), Number.isFinite(Infinity), isFinite('12'));
p('math', Math.floor(-1.5), Math.ceil(-1.5), Math.round(-1.5), Math.round(2.5), Math.trunc(-4.7), Math.sign(-3), Math.abs(-2), Math.min(), Math.max(), Math.pow(2, 10), Math.sqrt(16), Math.hypot(3, 4));
p('print', 100, 1e100, 123e-20, 0.5, -1e-7, 1 / 0, -1 / 0, 2 ** 64, 4.35, 0.1 * 3, 1.7976931348623157e308, 5e-324);

// arrays: methods a page uses
var arr = [3, 1, 4, 1, 5, 9, 2, 6];
p('arr-basic', arr.length, arr.indexOf(1), arr.lastIndexOf(1), arr.includes(9), arr.at(-1), arr.join());
p('arr-mut', (function () { var c = arr.slice(); c.push(7); c.unshift(0); var popped = c.pop(), shifted = c.shift(); c.splice(1, 2, 'x', 'y', 'z'); c.reverse(); return [popped, shifted, c.join(',')].join(' '); })());
p('arr-fill', new Array(3).fill(0).join(','), Array.from({ length: 3 }, function (v, i) { return i * i; }).join(','), Array.of(7, 8).join(','), Array.isArray([]), Array.isArray('no'));
p('arr-flat', [1, [2, [3, [4]]]].flat().length, [1, [2, [3, [4]]]].flat(Infinity).join(''), [1, 2].flatMap(function (v) { return [v, v * 10]; }).join(','));
p('arr-iter', (function () { var r = []; for (var v of ['a', 'b']) r.push(v); for (var k in { p: 1, q: 2 }) r.push(k); for (var [i, v2] of ['x', 'y'].entries()) r.push(i + v2); return r.join(' '); })());
p('arr-sort', ['b', 'a', 'C'].sort().join(''), [10, 9, 1, 100].sort().join(','), [3, 1, 2].sort(function (x, y) { return y - x; }).join(''));
p('arr-holes', [1, , 3].length, JSON.stringify([1, , 3]), [, 'a'].indexOf(undefined), 0 in [, 1]);
p('arr-concat', [1].concat([2, 3], 4, [[5]]).length, [1, 2, 3].reduceRight(function (s, x) { return s + x; }, ''), [1, 2, 3].findLast(function (x) { return x < 3; }));

// Map and Set
var mp = new Map([['a', 1]]);
mp.set('b', 2).set('a', 3);
p('map', mp.size, mp.get('a'), mp.has('b'), mp.delete('b'), mp.has('b'), Array.from(mp.keys()).join(','));
var st = new Set([1, 2, 2, 3]);
st.add(4); st.delete(1);
p('set', st.size, st.has(2), Array.from(st).join(','), [...new Set('aabbc')].join(''));
var it = [];
mp.forEach(function (v, k) { it.push(k + v); });
p('map-each', it.join(','));

// JSON: nesting, spacing, revivers' absence, edge values
p('json', JSON.stringify({ a: [1, { b: null }], s: 'q"uote', u: undefined, f: function () {} }), JSON.stringify([undefined, NaN, Infinity]), JSON.stringify(' \n'));
p('json-space', JSON.stringify({ a: 1, b: [1, 2] }, null, 2).split('\n').length, JSON.stringify({ toJSON: function () { return 'custom'; } }));
p('json-parse', JSON.parse('[1, "2", {"3": [true, false, null]}]')[2]['3'].length, JSON.parse('"\\u0041\\n"').length, JSON.parse(' 12.5e1 '));
try { JSON.parse('{bad'); } catch (e) { p('json-bad', e instanceof SyntaxError); }

// closures and scope: let per iteration, shadowing, hoisting, IIFE, recursion by name
var fs = [];
for (let i = 0; i < 3; i++) { let j = i * 10; fs.push(function () { return i + j; }); }
p('let-closures', fs.map(function (f) { return f(); }).join(','));
var sh2 = 'outer';
(function () { var sh2 = 'inner'; p('shadow', sh2); })();
p('shadow-outer', sh2);
p('hoist', hoisted(), typeof hoistedVar);
function hoisted() { return 'hoisted'; }
var hoistedVar = 1;
{ let blockScoped = 'block'; p('block', blockScoped); }
p('block-gone', typeof blockScoped);
try { tdz; let tdz = 1; } catch (e) { p('tdz', e instanceof ReferenceError); }
const cst = 1;
try { cst = 2; } catch (e) { p('const', e instanceof TypeError, cst); }
var memo = (function () { var cache = {}; return function fibm(n) { if (n in cache) return cache[n]; return cache[n] = n < 2 ? n : fibm(n - 1) + fibm(n - 2); }; })();
p('memo', memo(50));

// getters on classes, toString, valueOf, Symbol-less coercion
var money = { amount: 5, valueOf: function () { return this.amount; }, toString: function () { return '$' + this.amount; } };
p('coerce', money + 1, `${money}`, money * 2, String(money), money > 4, [money] + '');
p('date-free', typeof Date.now(), typeof new Date().getTime());

// tagged templates: the strings (cooked and raw), the substitutions, one array per place
var tpl = (s, ...v) => s.raw.join('|') + '#' + s.join('|') + '#' + v.join(',');
p('tagged', tpl`a${1}b\n${2}c`, tpl``, String.raw`x\ty${3}z`);
var seen = [];
function same(s) { seen.push(s); return s.length; }
for (var ti = 0; ti < 2; ti++) same`one${ti}two`;
p('tagged-site', seen[0] === seen[1], Object.isFrozen(seen[0]), seen[0].length, seen[0].raw.length);
var tagObj = { pre: '>', tag(s, x) { return this.pre + s[0] + x + s[1]; } };
p('tagged-method', tagObj.tag`[${'mid'}]`);
