// tests/js/receivers.js -- a built-in method called on an object of another kind, in V8's words (each kind's
// "Method X called on incompatible receiver Y", "X requires that 'this' be a Y", "this is not a typed array."), and
// the iterators: one prototype a kind with its next and tag, arrays' read at each step and done for good once
// past their end, a Set's entries in pairs. Its .expect is node's output.
(function () {
const t = (s, f) => { try { console.log(s, f()); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
function* g() {} async function* ag() {}
const G = Object.getPrototypeOf(g()), AG = Object.getPrototypeOf(ag());
t('gen next', () => G.next.call({}));
t('gen return', () => G.return.call({}));
t('gen throw', () => G.throw.call({}));
t('agen next', () => AG.next.call({}).catch(e => console.log('agen next rejects', e.constructor.name + ': ' + e.message)) && 'promise');
t('num toString', () => Number.prototype.toString.call({}));
t('num toFixed', () => Number.prototype.toFixed.call('1'));
t('num valueOf', () => Number.prototype.valueOf.call({}));
t('num toPrecision', () => Number.prototype.toPrecision.call({}));
t('num toLocaleString', () => Number.prototype.toLocaleString.call({}));
t('bool toString', () => Boolean.prototype.toString.call({}));
t('bool valueOf', () => Boolean.prototype.valueOf.call(1));
t('str toString', () => String.prototype.toString.call({}));
t('str valueOf', () => String.prototype.valueOf.call(1));
t('sym toString', () => Symbol.prototype.toString.call({}));
t('sym desc', () => Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').get.call({}));
t('bigint toString', () => BigInt.prototype.toString.call({}));
t('arr iter next', () => [][Symbol.iterator]().next.call({}));
t('map iter next', () => new Map()[Symbol.iterator]().next.call({}));
t('set iter next', () => new Set()[Symbol.iterator]().next.call({}));
t('str iter next', () => ''[Symbol.iterator]().next.call({}));
t('regexp str iter next', () => 'a'.matchAll(/a/g).next.call({}));
t('weakref deref', () => WeakRef.prototype.deref.call({}));
t('finreg register', () => FinalizationRegistry.prototype.register.call({}, {}));
t('re exec', () => RegExp.prototype.exec.call({}, ''));
t('re test', () => RegExp.prototype.test.call({}, ''));
t('re toString', () => RegExp.prototype.toString.call(1));
t('re flags', () => Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags').get.call(1));
t('re global', () => Object.getOwnPropertyDescriptor(RegExp.prototype, 'global').get.call({}));
t('re source', () => Object.getOwnPropertyDescriptor(RegExp.prototype, 'source').get.call({}));
t('re match', () => RegExp.prototype[Symbol.match].call(1, ''));
t('ta length', () => Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype), 'length').get.call({}));
t('ta fill', () => Uint8Array.prototype.fill.call({}, 0));
t('ta set', () => Uint8Array.prototype.set.call({}, []));
t('ta subarray', () => Uint8Array.prototype.subarray.call({}));
t('ta at', () => Uint8Array.prototype.at.call([], 0));
t('ta tag', () => Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype), Symbol.toStringTag).get.call({}));
t('dv getInt8', () => DataView.prototype.getInt8.call({}, 0));
t('dv byteLength', () => Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength').get.call({}));
t('ab byteLength', () => Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'byteLength').get.call({}));
t('ab slice', () => ArrayBuffer.prototype.slice.call({}));
t('err toString', () => Error.prototype.toString.call(1));
t('fn toString', () => Function.prototype.toString.call({}));
t('fn call', () => Function.prototype.call.call({}));
t('fn apply', () => Function.prototype.apply.call({}));
t('fn bind', () => Function.prototype.bind.call({}));
t('date getTime', () => Date.prototype.getTime.call({}));
t('obj toString null', () => Object.prototype.toString.call(null));
t('arr join null', () => Array.prototype.join.call(null));
t('str at null', () => String.prototype.at.call(null));
t('promise then', () => Promise.prototype.then.call(new Map()));
})();
(function () {
const TA = Object.getPrototypeOf(Uint8Array.prototype);
for (const k of Object.getOwnPropertyNames(TA).sort()) {
  const d = Object.getOwnPropertyDescriptor(TA, k);
  const f = d.get || d.value;
  if (typeof f !== 'function' || k === 'constructor') continue;
  try { f.call({}, () => {}); console.log(k, 'ok'); } catch (e) { console.log(k, e.message); }
}
for (const C of [DataView, ArrayBuffer, Map, WeakMap, WeakSet, WeakRef, FinalizationRegistry, RegExp, Date]) {
  for (const k of Object.getOwnPropertyNames(C.prototype).sort()) {
    const d = Object.getOwnPropertyDescriptor(C.prototype, k);
    const f = d.get || d.value;
    if (typeof f !== 'function' || k === 'constructor' || k === 'compile') continue;
    try { f.call({}, 0, 0); console.log(C.name, k, 'ok'); } catch (e) { console.log(C.name, k, e.message); }
  }
}
const iters = { 'Array Iterator': [][Symbol.iterator](), 'Map Iterator': new Map().keys(), 'Set Iterator': new Set().values(), 'String Iterator': ''[Symbol.iterator](), 'RegExp String Iterator': ''.matchAll(/a/g) };
for (const [n, it] of Object.entries(iters)) {
  const p = Object.getPrototypeOf(it);
  console.log(n, Object.getOwnPropertyNames(p).join(), String(p[Symbol.toStringTag]), Object.getPrototypeOf(p) === Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())), Object.prototype.toString.call(it));
}
})();
(function () {
const a = [1, 2]; const it = a.values(); a.push(3); console.log([...it].join());
const b = [1]; const it2 = b.values(); console.log(JSON.stringify(it2.next()), JSON.stringify(it2.next())); b.push(2); console.log(JSON.stringify(it2.next()));
const m = new Map([[1, 'a']]); const mi = m.entries(); console.log(JSON.stringify(mi.next()), JSON.stringify(mi.next())); m.set(2, 'b'); console.log(JSON.stringify(mi.next()));
const m2 = new Map([[1, 'a']]); const mi2 = m2.keys(); m2.set(2, 'b'); console.log([...mi2].join());
console.log(JSON.stringify([...new Set([1, 2]).entries()]), JSON.stringify([...new Set([3]).keys()]));
console.log(JSON.stringify([...[5, 6].entries()]), JSON.stringify([...[5, 6].keys()]), JSON.stringify([...'ab'[Symbol.iterator]()]));
console.log(Object.prototype.toString.call([].values()), Object.prototype.toString.call(new Map().keys()), Object.prototype.toString.call(new Set().values()), Object.prototype.toString.call(''[Symbol.iterator]()), Object.prototype.toString.call('a'.matchAll(/a/g)));
const AI = Object.getPrototypeOf([][Symbol.iterator]()), IP = Object.getPrototypeOf(AI);
console.log(Object.getOwnPropertyNames(AI).join(), typeof IP.next, IP[Symbol.iterator]() === IP, Object.getPrototypeOf(new Map().keys()) !== AI);
console.log(Object.getPrototypeOf(new Uint8Array(2).values()) === AI, JSON.stringify([...new Uint8Array([7, 8]).entries()]));
const holes = [, 1]; console.log(JSON.stringify([...holes.entries()]), JSON.stringify([...Array.prototype.values.call({ length: 2, 0: 'x', 1: 'y' })]));
console.log(JSON.stringify([...Array.prototype.keys.call('abc')]));
const s = 'a'.matchAll(/./g); console.log(JSON.stringify([...s].map(m => m[0])));
const arr2 = [1, 2, 3]; let out = []; for (const [i, v] of arr2.entries()) { out.push(i + ':' + v); if (i === 0) arr2.push(4); } console.log(out.join());
function* g() {} const GP = Object.getPrototypeOf(g()); console.log(Object.prototype.toString.call(g()), GP === Object.getPrototypeOf(g.prototype) ? 'proto' : 'no');
})();
