// tests/js/v8msgs.js -- V8's own words and ways where the engine had its own: JSON.parse's strict grammar,
// its errors' positions and its reviver (with the source text V8 hands it), the incompatible-receiver
// messages of Map's, Set's and the weak collections' methods, Promise's capabilities through its
// receiver (subclasses, Symbol.species, withResolvers), the TDZ on assignment, immutable function
// names, the unmapped arguments object's callee, Object.prototype.__proto__ and [[SetPrototypeOf]]'s
// checks. Its .expect is node's output.
const t = (s, f) => { try { const r = f(); console.log(s, typeof r === 'object' && r !== null ? JSON.stringify(r) : r); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };

// ---- JSON.parse: errors ----
(function () {
const cases = ['', ' ', '{', '[', '{"a"', '{"a":', '{"a":1', '{"a":1,', '{"a" 1}', '{a:1}', "{'a':1}", '{"a":1,}', '[1,]', '[1 2]', '[,1]', '"abc', '"a\\x"', '"a\\u12g4"', '"a\u0001b"', '-', '-a', '1e', '1e+', '01', '1.', '.5', 'tru', 'trux', 'nul', 'falsey', '1 2', '{} x', 'x', 'undefined', 'NaN', '"\\', '{"a":1}}', '[1]]', '{"a":[1,{"b":2}],"c":tru}',
  'abcdefghijklmnopqrstuvwxyz0123456789 is a long string with a bad token at the start', '{"key": "value", "another": 12345, "bad": x, "more": "stuff after the bad token here"}',
  '[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, x]', 'é', '{"a":1}\n\n x', '[\n1,\n2,\nx]', '{"\\u00e9": 1, x}', '1.5e3.2', '+1', '0x10', 'Infinity', '[1,2', '{"a":{"b":', '\t\n', '"\\u"', '{"a" : 1 , "b" : }'];
for (const c of cases) {
  try { JSON.parse(c); console.log(JSON.stringify(c), 'ok'); } catch (e) { console.log(JSON.stringify(c), '=>', e.message); }
}
})();

// ---- JSON.parse: the reviver ----
(function () {
const log = [];
const r = JSON.parse('{"a":[1,"x",{"b":true}],"c":null,"d":-1.50e2}', function (k, v, ctx) {
  log.push(JSON.stringify(k) + ' ' + typeof v + ' ' + (Array.isArray(this) ? 'arr' : typeof this) + ' ' + (ctx === undefined ? 'noctx' : JSON.stringify(ctx)) + ' ' + Object.keys(ctx || {}).join(','));
  if (k === 'c') return undefined;
  if (typeof v === 'number') return v * 2;
  return v;
});
log.forEach(l => console.log(l));
console.log(JSON.stringify(r));
console.log(JSON.stringify(JSON.parse('[1,2,3]', (k, v) => k === '1' ? undefined : v)));
console.log(JSON.stringify(JSON.parse('5', function (k, v) { return [k, v, Object.keys(this), this[""]]; })));
console.log(JSON.parse('"s"', (k, v, c) => c.source));
console.log(JSON.parse('12345678901234567890', (k, v, c) => c.source));
console.log(JSON.stringify(JSON.parse('{"x":1}', 5)));
console.log(JSON.parse.length, JSON.stringify.length);
// mutation during revive
console.log(JSON.stringify(JSON.parse('{"a":1,"b":2}', function (k, v) { if (k === 'a') { delete this.b; } return v; })));
console.log(JSON.stringify(JSON.parse('{"a":1,"b":2}', function (k, v) { if (k === 'a') { this.b = 'changed'; } return v; })));
console.log(JSON.stringify(JSON.parse('[[1,2],[3]]', function (k, v, c) { return Array.isArray(v) ? v.length + ':' + JSON.stringify(c) : v; })));
try { JSON.parse('{"a":1}', function (k, v) { throw new Error('in reviver ' + k); }); } catch (e) { console.log(e.message); }
console.log(JSON.parse('{"__proto__": 1}').__proto__, Object.keys(JSON.parse('{"__proto__": 1}')).join());
console.log(JSON.stringify(JSON.parse('{"a":1,"a":2}')));
console.log(JSON.parse('"\\ud83d\\ude00"').length, JSON.parse('"\\ud83d"').length);
console.log(JSON.parse(' \t\n\r 1 \t\n\r '));
console.log(JSON.parse('-0'), Object.is(JSON.parse('-0'), -0), JSON.parse('1E400'), JSON.parse('-1e-400'));
console.log(JSON.parse(null), JSON.parse(true), JSON.parse(12));
try { JSON.parse(undefined); } catch (e) { console.log(e.message); }
try { JSON.parse({}); } catch (e) { console.log(e.message); }
try { JSON.parse(' 1'); } catch (e) { console.log(e.message); }
try { JSON.parse('[1,2, 3]'); } catch (e) { console.log(e.message); }
try { JSON.parse(''); } catch (e) { console.log(e.name, e.message, e instanceof SyntaxError); }
let deep = ''; for (let i = 0; i < 100000; i++) deep += '['; 
try { JSON.parse(deep); } catch (e) { console.log(e.name, e.message); }
})();

// ---- receivers ----
(function () {
class Foo {}
const rs = [1, 1.5, -0, NaN, 'str', '', true, null, undefined, Symbol('s'), 10n, [], [1, 2], {}, new Foo(), function named() {}, () => 1, class K {}, Object.create(null),
  new Error('boom'), new TypeError('t'), /re/g, new Date(0), { toString() { return 'x'; } }, { [Symbol.toStringTag]: 'Tag' }, new Map(), new Set(), Math, JSON, new Proxy({}, {}),
  Object.create(Foo.prototype), (function () { return arguments; })(), new Uint8Array(2), Promise.resolve(), new WeakMap()];
for (const r of rs) {
  let m;
  try { Map.prototype.get.call(r, 1); m = 'no throw'; } catch (e) { m = e.message; }
  console.log(m);
}
for (const [n, f] of [['Set.prototype.has', Set.prototype.has], ['Map.prototype.size', Object.getOwnPropertyDescriptor(Map.prototype, 'size').get], ['Map.prototype.forEach', Map.prototype.forEach], ['Map.prototype.entries', Map.prototype.entries], ['Set.prototype.values', Set.prototype.values], ['Map.prototype.clear', Map.prototype.clear]]) {
  try { f.call({}); } catch (e) { console.log(n, e.message); }
}
t('weak get', () => WeakMap.prototype.get.call(new Map(), {}));
t('weakset has', () => WeakSet.prototype.has.call(new WeakMap(), {}));
t('weakset add', () => WeakSet.prototype.add.call({}, {}));
t('map has set', () => Map.prototype.has.call(new Set(), 1));
t('set keys is values', () => Set.prototype.keys === Set.prototype.values && Set.prototype[Symbol.iterator] === Set.prototype.values);
t('map ctor', () => Map());
t('set ctor', () => Set());
})();

// ---- promises ----
(function () {
t('resolve.call(1)', () => Promise.resolve.call(1, 2));
t('resolve.call(undefined)', () => Promise.resolve.call(undefined, 2));
t('resolve.call({})', () => Promise.resolve.call({}, 2));
t('resolve.call(fn)', () => Promise.resolve.call(function () {}, 2));
t('resolve.call(arrow)', () => Promise.resolve.call(() => {}, 2));
t('reject.call(1)', () => Promise.reject.call(1, 2));
t('reject.call({})', () => Promise.reject.call({}, 2));
t('all.call(1)', () => Promise.all.call(1, []));
t('all.call({})', () => Promise.all.call({}, []));
t('race.call(1)', () => Promise.race.call(1, []));
t('any.call(1)', () => Promise.any.call(1, []));
t('allSettled.call(1)', () => Promise.allSettled.call(1, []));
t('withResolvers.call(1)', () => Promise.withResolvers.call(1));
class P extends Promise {}
console.log(P.resolve(1) instanceof P, P.reject(1).catch(() => {}) instanceof P, P.all([]) instanceof P, Promise.resolve(P.resolve(1)) instanceof P);
const p0 = Promise.resolve(1);
console.log(Promise.resolve(p0) === p0, P.resolve(p0) === p0);
const pp = P.resolve(2);
console.log(P.resolve(pp) === pp, Promise.resolve(pp) === pp);
t('then.call(1)', () => Promise.prototype.then.call(1));
t('then.call({})', () => Promise.prototype.then.call({}));
t('catch.call(1)', () => Promise.prototype.catch.call(1));
t('finally.call(1)', () => Promise.prototype.finally.call(1));
t('catch.call({then})', () => Promise.prototype.catch.call({ then() { return 'x'; } }));
t('new Promise(1)', () => new Promise(1));
t('Promise()', () => Promise(() => {}));
let calls = 0;
class Q { constructor(ex) { calls++; ex(() => {}, () => {}); } }
t('resolve.call(Q)', () => Promise.resolve.call(Q, 1));
console.log(calls);
class R { constructor(ex) { ex(1, 2); } }
t('resolve.call(R)', () => Promise.resolve.call(R, 1));
class S { constructor(ex) { ex(() => {}, () => {}); ex(() => {}, () => {}); } }
t('resolve.call(S)', () => Promise.resolve.call(S, 1));

t('species getter', () => [Promise, Array, Map, Set, RegExp, ArrayBuffer].map(c => Object.getOwnPropertyDescriptor(c, Symbol.species).get.name + ':' + (c[Symbol.species] === c)).join());
class PS extends Promise { static get [Symbol.species]() { return Promise; } }
t('species then', () => { const p = PS.resolve(1).then(x => x); return [p instanceof PS, p instanceof Promise].join(); });
t('bad constructor', () => { const p = Promise.resolve(1); p.constructor = 5; p.then(() => {}); });
t('bad species', () => { const p = Promise.resolve(1); p.constructor = { [Symbol.species]: 5 }; p.then(() => {}); });
t('withResolvers', () => { const { promise, resolve, reject } = Promise.withResolvers(); return [promise instanceof Promise, typeof resolve, typeof reject, Object.keys(Promise.withResolvers()).join()].join(); });
const order = [];
class Q2 extends Promise { constructor(ex) { order.push('ctor'); super(ex); } }
Q2.resolve(1).then(v => order.push('then ' + v));
Promise.all([1, Promise.resolve(2), { then(r) { order.push('thenable'); r(3); } }]).then(v => order.push('all ' + v.join()));
Promise.allSettled([Promise.reject(new Error('x')), 4]).then(v => order.push('settled ' + v.map(o => o.status).join()));
Promise.any([Promise.reject(1), Promise.reject(2)]).catch(e => order.push('any ' + e.constructor.name + ' ' + e.errors.join()));
Promise.race([new Promise(() => {}), Promise.resolve('r')]).then(v => order.push('race ' + v));
Promise.resolve('f').finally(() => order.push('finally')).then(v => order.push('after finally ' + v));
const it = { [Symbol.iterator]() { let i = 0; return { next() { order.push('next ' + i); return i < 2 ? { value: i++, done: false } : { done: true }; }, return() { order.push('return'); return {}; } }; } };
Promise.all(it).then(v => order.push('all it ' + v.join()));
const bad = { [Symbol.iterator]() { return { next() { return { value: 1, done: false }; }, return() { order.push('closed'); return {}; } }; } };
const R2 = function (ex) { return new Promise(ex); };
R2.resolve = () => { throw new Error('resolve threw'); };
Promise.all.call(R2, bad).catch(e => order.push('all rejected: ' + e.message));
setTimeout(() => console.log(order.join(' | ')), 0);
})();

// ---- arguments, __proto__, the TDZ and immutable names ----
(function () {
function sl(a) { return arguments; }
function st(a) { 'use strict'; return arguments; }
function ns(a = 1) { return arguments; }
function rs(...r) { return arguments; }
function ds({ a }) { return arguments; }
t('sloppy callee', () => sl(1).callee === sl);
t('strict callee', () => st(1).callee);
t('default callee', () => ns(1).callee);
t('rest callee', () => rs(1).callee);
t('destructure callee', () => ds({}).callee);
t('strict desc', () => { const d = Object.getOwnPropertyDescriptor(st(1), 'callee'); return [typeof d.get, d.get === d.set, d.enumerable, d.configurable, d.get.name === '', d.get.length, Object.isFrozen(d.get), Object.isExtensible(d.get)].join(); });
t('sloppy desc', () => { const d = Object.getOwnPropertyDescriptor(sl(1), 'callee'); return [d.value === sl, d.writable, d.enumerable, d.configurable].join(); });
t('names sloppy', () => Object.getOwnPropertyNames(sl(1, 2)).join());
t('names strict', () => Object.getOwnPropertyNames(st(1, 2)).join());
t('symbols', () => Object.getOwnPropertySymbols(sl(1)).map(String).join());
t('iter is values', () => sl(1)[Symbol.iterator] === Array.prototype.values);
t('tag', () => Object.prototype.toString.call(sl(1)));
t('callee getter same as TTE', () => { const g = Object.getOwnPropertyDescriptor(st(1), 'callee').get; const h = Object.getOwnPropertyDescriptor(ns(1), 'callee').get; return g === h; });
t('strict reassign args', () => { 'use strict'; return (function () { return typeof arguments; })(); });
t('__proto__ get', () => ({}).__proto__ === Object.prototype);
t('__proto__ desc', () => { const d = Object.getOwnPropertyDescriptor(Object.prototype, '__proto__'); return [typeof d.get, typeof d.set, d.enumerable, d.configurable, d.get.name, d.set.name, d.get.length, d.set.length].join(); });
t('__proto__ set', () => { const o = {}; o.__proto__ = Array.prototype; return o instanceof Array; });
t('__proto__ set prim', () => { const o = {}; o.__proto__ = 5; return Object.getPrototypeOf(o) === Object.prototype; });
t('__proto__ set null', () => { const o = {}; o.__proto__ = null; return Object.getPrototypeOf(o) === null && o.__proto__ === undefined; });
t('__proto__ on prim', () => (5).__proto__ === Number.prototype);
t('__proto__ set on prim', () => { const n = 5; n.__proto__ = {}; return 'ok'; });
t('__proto__ cyclic', () => { const a = {}; const b = Object.create(a); a.__proto__ = b; });
t('__proto__ nonext', () => { const a = Object.preventExtensions({}); a.__proto__ = {}; });
t('__proto__ get undefined', () => Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').get.call(undefined));
t('__proto__ set undefined', () => Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').set.call(undefined, {}));
t('literal __proto__', () => { const o = { __proto__: Array.prototype, a: 1 }; return [o instanceof Array, Object.keys(o).join(), Object.hasOwn(o, '__proto__')].join(); });
t('literal __proto__ null', () => Object.getPrototypeOf({ __proto__: null }) === null);
t('literal __proto__ prim', () => Object.getPrototypeOf({ __proto__: 5 }) === Object.prototype);
t('literal computed', () => { const k = '__proto__'; const o = { [k]: 5 }; return Object.hasOwn(o, '__proto__') && o.__proto__ === 5; });
t('literal shorthand', () => { const __proto__ = 5; const o = { __proto__ }; return Object.hasOwn(o, '__proto__'); });
t('literal method', () => { const o = { __proto__() { return 1; } }; return Object.hasOwn(o, '__proto__'); });
t('literal string key', () => { const o = { '__proto__': Array.prototype }; return o instanceof Array; });
t('dup', () => eval('({ __proto__: null, __proto__: null })'));
t('dup ok computed', () => typeof eval('({ __proto__: null, ["__proto__"]: null })'));
t('in Object.create(null)', () => { const o = Object.create(null); o.__proto__ = 5; return Object.keys(o).join(); });
t('JSON __proto__', () => Object.getPrototypeOf(JSON.parse('{"__proto__": []}')) === Object.prototype);
t('spread', () => { const o = { ...{ ['__proto__']: 1 } }; return Object.hasOwn(o, '__proto__'); });
t('assign', () => { const o = Object.assign({}, JSON.parse('{"__proto__": {"x":1}}')); return o.x; });
t('tdz assign', () => { x = 1; let x; });
t('tdz assign const', () => { y = 1; const y = 2; });
t('const assign', () => { const z = 1; z = 2; });
t('tdz compound', () => { w += 1; let w = 1; });
t('tdz closure', () => { const f = () => { v = 1; }; f(); let v; });
t('tdz class', () => { C = 1; class C {} });
t('fn name strict', () => (function f() { 'use strict'; f = 1; })());
t('fn name sloppy', () => (function f() { f = 1; return typeof f; })());
t('fn name sloppy closure', () => (function f() { (() => { f = 1; })(); return typeof f; })());
t('fn name strict closure', () => (function f() { 'use strict'; (() => { f = 1; })(); return typeof f; })());
t('fn name strict compound', () => (function f() { 'use strict'; f++; })());
t('let after ok', () => { let q; q = 3; return q; });
t('loop let', () => { let s = 0; for (let i = 0; i < 3; i++) { s += i; } return s; });
})();

// ---- [[SetPrototypeOf]] ----
(function () {
t('cyclic', () => { const a = {}; const b = Object.create(a); Object.setPrototypeOf(a, b); });
t('nonext', () => { const a = Object.preventExtensions({}); Object.setPrototypeOf(a, {}); });
t('nonext same', () => { const p = {}; const a = Object.preventExtensions(Object.create(p)); return Object.setPrototypeOf(a, p) === a; });
t('reflect cyclic', () => { const a = {}; const b = Object.create(a); return Reflect.setPrototypeOf(a, b); });
t('reflect nonext', () => Reflect.setPrototypeOf(Object.preventExtensions({}), {}));
t('immutable', () => Object.setPrototypeOf(Object.prototype, {}));
t('immutable same', () => Object.setPrototypeOf(Object.prototype, null) === Object.prototype);
t('bad proto', () => Object.setPrototypeOf({}, 5));
t('prim target', () => Object.setPrototypeOf(5, {}));
t('undef target', () => Object.setPrototypeOf(undefined, {}));
})();

// ---- subclasses of the built-ins ----
(function () {
class M extends Map {} class S extends Set {} class P extends Promise {} class E extends Error {} class A extends Array {} class D extends Date {} class R extends RegExp {}
class WM extends WeakMap {} class U8 extends Uint8Array {} class AB extends ArrayBuffer {} class F extends Function {} class B extends Boolean {} class N extends Number {} class St extends String {}
t('M', () => new M() instanceof M && new M([[1, 2]]).get(1) === 2);
t('S', () => new S([1]) instanceof S);
t('P', () => new P(() => {}) instanceof P);
t('E', () => new E('x') instanceof E && new E('x').message === 'x');
t('A', () => new A(3) instanceof A && new A(3).length === 3);
t('D', () => new D(0) instanceof D && new D(0).getTime() === 0);
t('R', () => new R('a') instanceof R && new R('a').test('a'));
t('WM', () => new WM() instanceof WM);
t('U8', () => new U8(2) instanceof U8 && new U8(2).length === 2);
t('AB', () => new AB(2) instanceof AB);
t('F', () => new F('return 1') instanceof F && new F('return 1')() === 1);
t('B', () => new B(true) instanceof B);
t('N', () => new N(1) instanceof N);
t('St', () => new St('ab') instanceof St && new St('ab').length === 2);
t('Map()', () => Map());
t('Set()', () => Set());
t('Promise()', () => Promise(() => {}));
t('WeakMap()', () => WeakMap());
t('Uint8Array()', () => Uint8Array(2));
t('ArrayBuffer()', () => ArrayBuffer(2));
t('DataView()', () => DataView(new ArrayBuffer(2)));
t('Proxy()', () => Proxy({}, {}));
t('WeakRef()', () => WeakRef({}));
t('Date()', () => typeof Date());
t('Reflect.construct(Map, [], P)', () => Object.getPrototypeOf(Reflect.construct(Map, [], P)) === P.prototype);
t('Reflect.construct(Date, [0], Object)', () => Object.getPrototypeOf(Reflect.construct(Date, [0], Object)) === Object.prototype);
})();
console.log(-0, [-0].join(), String(-0), Object.is(JSON.parse('-0'), -0));
