// A class's accessors not enumerable, an object literal's enumerable (with __proto__ too), the static prototype refusals
const d = (o, k) => { const x = Object.getOwnPropertyDescriptor(o, k); return x ? [x.enumerable, x.configurable, typeof x.get, typeof x.set].join('/') : 'none'; };
class A { get g() { return 1; } set s(v) {} get gs() { return 1; } set gs(v) {} static get sg() { return 1; } m() {} static sm() {} get ['c' + 1]() { return 1; } static set ['d' + 2](v) {} }
console.log('A', d(A.prototype, 'g'), d(A.prototype, 's'), d(A.prototype, 'gs'), d(A, 'sg'), d(A.prototype, 'm'), d(A, 'sm'), d(A.prototype, 'c1'), d(A, 'd2'));
class B extends A { get h() { return 2; } set t(v) {} static get u() { return 3; } }
console.log('B', d(B.prototype, 'h'), d(B.prototype, 't'), d(B, 'u'));
const C = class { get g() { return 1; } };
console.log('C', d(C.prototype, 'g'), Object.keys(C.prototype).length, Object.keys(A).length);
const o = { get g() { return 1; }, set s(v) {}, m() {}, get ['k' + 1]() { return 1; } };
console.log('o', d(o, 'g'), d(o, 's'), d(o, 'm'), d(o, 'k1'));
const p = { __proto__: { z: 1 }, get g() { return 1; }, set s(v) {} };
console.log('p', d(p, 'g'), d(p, 's'), Object.keys(p).join());
class N { constructor() { this.a = 1; this.parent = null; } get proxyOf() { return this; } }
const n = new N(); const ks = []; for (const k in n) ks.push(k); console.log(ks.join());
const walk = (x, seen = 0) => { if (seen > 50) throw new Error('loop'); if (x && typeof x === 'object') { delete x.parent; for (const k in x) walk(x[k], seen + 1); } return 'walked'; };
console.log(walk(n));
try { (0, eval)("class E { static get ['proto' + 'type']() { return 1; } }"); console.log('no throw'); } catch (e) { console.log(e.constructor.name, e.message); }
try { (0, eval)("class F { static get prototype() { return 1; } }"); console.log('no throw'); } catch (e) { console.log(e.constructor.name, e.message); }
try { (0, eval)("class G { static set ['proto' + 'type'](v) {} }"); console.log('no throw'); } catch (e) { console.log(e.constructor.name, e.message); }
class H { get prototype() { return 'inst'; } }
console.log(d(H.prototype, 'prototype'));
