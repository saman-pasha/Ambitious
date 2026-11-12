// tests/js/proxyinv.js -- a proxy's traps checked against its target (ECMA-262 10.5), each line
// against V8's (proxyinv.expect): non-configurable and non-writable properties reported as the
// target holds them, a non-extensible target's keys and prototype reported whole, ownKeys'
// duplicates and completeness, and every failure's TypeError in V8's words; the traps' order;
// and the descriptors those checks read: Object.defineProperty's defaults and changes,
// writable and configurable kept, freeze, seal, preventExtensions and their tests.

const p = (...a) => console.log(a.join(' | '));
const tr = f => { try { const r = f(); return typeof r === 'object' && r !== null ? JSON.stringify(r) : String(r); } catch (e) { return e.name + ': ' + e.message; } };
const gd = (o, k) => JSON.stringify(Object.getOwnPropertyDescriptor(o, k));
const fixed = () => {
  const t = {};
  Object.defineProperty(t, 'x', { value: 1, writable: false, configurable: false, enumerable: true });
  Object.defineProperty(t, 'acc', { get() { return 2; }, configurable: false });
  Object.defineProperty(t, 'w', { value: 3, writable: true, configurable: false });
  t.c = 4;
  return t;
};
const ne = Object.preventExtensions({ y: 1 }), nep = Object.preventExtensions({});

// get, set, has, deleteProperty
{
p(tr(() => new Proxy(fixed(), { get: () => 2 }).x), tr(() => new Proxy(fixed(), { get: () => 1 }).x), tr(() => new Proxy(fixed(), { get: () => 5 }).acc), tr(() => new Proxy(fixed(), { get: () => 9 }).w), tr(() => new Proxy(fixed(), { get: () => 9 }).c));
const noget = {}; Object.defineProperty(noget, 'g', { set(v) {}, configurable: false });
p(tr(() => new Proxy(noget, { get: () => 1 }).g), tr(() => new Proxy(noget, { get: () => undefined }).g), tr(() => new Proxy(fixed(), { get: () => ({}) }).x), tr(() => new Proxy(fixed(), { get: () => 'a' }).x));
p(tr(() => new Proxy(Object.freeze({ s: Symbol('q') }), { get: () => 1 }).s), tr(() => new Proxy(Object.freeze({ o: {} }), { get: () => ({}) }).o), tr(() => new Proxy(Object.freeze({ n: NaN }), { get: () => NaN }).n), tr(() => new Proxy(Object.freeze({ z: 0 }), { get: () => -0 }).z));
p(tr(() => { const px = new Proxy(fixed(), { set: () => true }); px.x = 2; return 'ok'; }), tr(() => { const px = new Proxy(fixed(), { set: () => true }); px.x = 1; return 'ok'; }), tr(() => Reflect.set(new Proxy(fixed(), { set: () => true }), 'x', 2)));
p(tr(() => Reflect.set(new Proxy(fixed(), { set: () => true }), 'acc', 2)), tr(() => Reflect.set(new Proxy(fixed(), { set: () => false }), 'c', 2)), tr(() => Reflect.set(new Proxy(fixed(), { set: () => true }), 'w', 7)));
p(tr(() => 'x' in new Proxy(fixed(), { has: () => false })), tr(() => 'c' in new Proxy(fixed(), { has: () => false })), tr(() => 'y' in new Proxy(ne, { has: () => false })), tr(() => 'zz' in new Proxy(ne, { has: () => false })), tr(() => Reflect.has(new Proxy(fixed(), { has: () => 0 }), 'x')));
p(tr(() => delete new Proxy(fixed(), { deleteProperty: () => true }).x), tr(() => delete new Proxy(ne, { deleteProperty: () => true }).y), tr(() => delete new Proxy(fixed(), { deleteProperty: () => true }).c), tr(() => Reflect.deleteProperty(new Proxy({}, { deleteProperty: () => false }), 'a')));
p(tr(() => delete new Proxy(ne, { deleteProperty: () => true }).nope), tr(() => 'x' in new Proxy(fixed(), { has: () => true })), tr(() => new Proxy(Object.freeze([1]), { get: () => 2 })[0]));
}

// defineProperty and getOwnPropertyDescriptor
{
p(tr(() => Object.defineProperty(new Proxy({}, { defineProperty: () => true }), 'q', { value: 1, configurable: false })), tr(() => Object.defineProperty(new Proxy(ne, { defineProperty: () => true }), 'q', { value: 1 })));
p(tr(() => Object.defineProperty(new Proxy(fixed(), { defineProperty: () => true }), 'x', { value: 2 })), tr(() => Object.defineProperty(new Proxy(fixed(), { defineProperty: () => true }), 'x', { value: 1 })), tr(() => Object.defineProperty(new Proxy(fixed(), { defineProperty: () => true }), 'w', { value: 1, writable: false, configurable: false })));
p(tr(() => Object.defineProperty(new Proxy({}, { defineProperty: () => false }), 'q', { value: 1 })), tr(() => Reflect.defineProperty(new Proxy({}, { defineProperty: () => false }), 'q', { value: 1 })), tr(() => Object.defineProperty(new Proxy(fixed(), { defineProperty: () => true }), 'c', { value: 1, configurable: false })));
p(tr(() => Object.getOwnPropertyDescriptor(new Proxy(fixed(), { getOwnPropertyDescriptor: () => undefined }), 'x')), tr(() => Object.getOwnPropertyDescriptor(new Proxy(ne, { getOwnPropertyDescriptor: () => undefined }), 'y')),
  tr(() => Object.getOwnPropertyDescriptor(new Proxy({}, { getOwnPropertyDescriptor: () => 1 }), 'y')), tr(() => Object.getOwnPropertyDescriptor(new Proxy({}, { getOwnPropertyDescriptor: () => ({ value: 1, configurable: false }) }), 'y')));
p(tr(() => Object.getOwnPropertyDescriptor(new Proxy({ y: 1 }, { getOwnPropertyDescriptor: () => ({ value: 1, configurable: false }) }), 'y')), tr(() => Object.getOwnPropertyDescriptor(new Proxy(fixed(), { getOwnPropertyDescriptor: () => ({ value: 1, configurable: false, writable: true }) }), 'x')),
  tr(() => Object.getOwnPropertyDescriptor(new Proxy(fixed(), { getOwnPropertyDescriptor: () => ({ value: 2, configurable: false, writable: false, enumerable: true }) }), 'x')), tr(() => Object.getOwnPropertyDescriptor(new Proxy(ne, { getOwnPropertyDescriptor: () => ({ value: 1, configurable: true }) }), 'nope')));
p(tr(() => Object.getOwnPropertyDescriptor(new Proxy(fixed(), { getOwnPropertyDescriptor: () => ({ value: 1, configurable: false, writable: false, enumerable: true }) }), 'x')), tr(() => Object.getOwnPropertyDescriptor(new Proxy(fixed(), { getOwnPropertyDescriptor: () => ({ value: 3, configurable: false, writable: false }) }), 'w')),
  tr(() => Object.getOwnPropertyDescriptor(new Proxy({}, { getOwnPropertyDescriptor: () => ({ get: 1 }) }), 'y')), tr(() => Object.getOwnPropertyDescriptor(new Proxy({ y: 1 }, { getOwnPropertyDescriptor: () => ({ value: 1, get() {} }) }), 'y')));
p(tr(() => Object.getOwnPropertyDescriptor(new Proxy({ a: 1 }, { getOwnPropertyDescriptor: () => ({ value: 5 }) }), 'a')), tr(() => Object.getOwnPropertyDescriptor(new Proxy({}, { getOwnPropertyDescriptor: () => ({ get() {}, configurable: true }) }), 'z')),
  tr(() => Object.keys(new Proxy({ a: 1, b: 2 }, { getOwnPropertyDescriptor: (t, k) => k === 'a' ? undefined : Reflect.getOwnPropertyDescriptor(t, k) }))));
}

// the prototype, extensibility, ownKeys, construct
{
p(tr(() => Object.getPrototypeOf(new Proxy(nep, { getPrototypeOf: () => Array.prototype }))), tr(() => Object.getPrototypeOf(new Proxy(nep, { getPrototypeOf: () => Object.prototype })) === Object.prototype), tr(() => Object.getPrototypeOf(new Proxy({}, { getPrototypeOf: () => 1 }))));
p(tr(() => Object.setPrototypeOf(new Proxy(nep, { setPrototypeOf: () => true }), Array.prototype)), tr(() => Reflect.setPrototypeOf(new Proxy(nep, { setPrototypeOf: () => true }), Object.prototype)), tr(() => Object.setPrototypeOf(new Proxy({}, { setPrototypeOf: () => false }), null)), tr(() => Reflect.setPrototypeOf(new Proxy({}, { setPrototypeOf: () => false }), null)));
p(tr(() => Object.isExtensible(new Proxy({}, { isExtensible: () => false }))), tr(() => Object.isExtensible(new Proxy(nep, { isExtensible: () => true }))), tr(() => Object.isExtensible(new Proxy({}, { isExtensible: () => 1 }))));
p(tr(() => Object.preventExtensions(new Proxy({}, { preventExtensions: () => true }))), tr(() => Object.preventExtensions(new Proxy({}, { preventExtensions: () => false }))), tr(() => Reflect.preventExtensions(new Proxy({}, { preventExtensions: () => false }))), tr(() => Reflect.preventExtensions(new Proxy(nep, { preventExtensions: () => true }))));
p(tr(() => Object.keys(new Proxy({}, { ownKeys: () => ['a', 'a'] }))), tr(() => Reflect.ownKeys(new Proxy(fixed(), { ownKeys: () => ['c'] }))), tr(() => Reflect.ownKeys(new Proxy(fixed(), { ownKeys: () => ['x', 'acc', 'w'] }))));
p(tr(() => Reflect.ownKeys(new Proxy(ne, { ownKeys: () => [] }))), tr(() => Reflect.ownKeys(new Proxy(ne, { ownKeys: () => ['y', 'z'] }))), tr(() => Reflect.ownKeys(new Proxy(ne, { ownKeys: () => ['y'] }))), tr(() => Reflect.ownKeys(new Proxy({}, { ownKeys: () => [1] }))), tr(() => Reflect.ownKeys(new Proxy({}, { ownKeys: () => 1 }))));
p(tr(() => Reflect.ownKeys(new Proxy({}, { ownKeys: () => [Symbol.iterator, 'b', Symbol.iterator] }))), tr(() => Object.getOwnPropertyNames(new Proxy({ a: 1 }, { ownKeys: () => ['a', 'b'] }))), tr(() => Object.keys(new Proxy({ a: 1 }, { ownKeys: () => ['a', 'b'] }))));
p(tr(() => Reflect.ownKeys(new Proxy([1], { ownKeys: () => ['0'] }))), tr(() => Reflect.ownKeys(new Proxy(Object.freeze({ a: 1 }), { ownKeys: () => ['a'] }))), tr(() => Reflect.ownKeys(new Proxy(Object.seal({ a: 1, b: 2 }), { ownKeys: () => ['b', 'a'] }))));
p(tr(() => new (new Proxy(function () {}, { construct: () => 1 }))()), tr(() => new (new Proxy(function () {}, { construct: () => null }))()), tr(() => new (new Proxy(function () { this.k = 1; }, {}))()));
const st = {}; const sy = Symbol('sy'); Object.defineProperty(st, sy, { value: 1 });
p(tr(() => new Proxy(st, { get: () => 2 })[sy]), tr(() => Object.getOwnPropertyDescriptor(new Proxy(st, { getOwnPropertyDescriptor: () => undefined }), sy)), tr(() => Reflect.ownKeys(new Proxy(st, { ownKeys: () => [] }))));
p(tr(() => new Proxy({}, { get: 1 }).x), tr(() => new Proxy({}, { get: {} }).x), tr(() => new Proxy({}, { get: null }).x), tr(() => Reflect.get(1, 'a')), tr(() => Reflect.defineProperty(1, 'a', {})), tr(() => Reflect.ownKeys(null)));
const { proxy, revoke } = Proxy.revocable({}, {}); revoke();
p(tr(() => proxy.a), tr(() => Object.keys(proxy)), tr(() => 'a' in proxy), tr(() => typeof proxy), tr(() => Object.getPrototypeOf(proxy)), tr(() => Object.isExtensible(proxy)));
}

// proxies of proxies: each layer checked against the one it holds
{
const inner = new Proxy(fixed(), {});
p(tr(() => new Proxy(inner, { get: () => 2 }).x), tr(() => Reflect.ownKeys(new Proxy(inner, { ownKeys: () => [] }))), tr(() => Object.getOwnPropertyDescriptor(new Proxy(inner, {}), 'x')), tr(() => Object.isFrozen(new Proxy(Object.freeze({ a: 1 }), {}))));
}

// the traps' calls, in order
{
const log = [];
const h = new Proxy({}, { get(t, k) { log.push(String(k)); return undefined; } });
const px = new Proxy({ a: 1 }, h);
Object.keys(px); Object.getOwnPropertyDescriptor(px, 'a'); Object.defineProperty(px, 'b', { value: 1, configurable: true }); 'a' in px; delete px.b; px.c = 1; px.c;
Object.getPrototypeOf(px); Object.setPrototypeOf(px, null); Object.isExtensible(px); Object.preventExtensions(px); Reflect.ownKeys(px); Object.isFrozen(px); Object.seal(px);
p(log.join(','));
const l2 = [];
const p2 = new Proxy({ a: 1, b: 2 }, { ownKeys(t) { l2.push('ownKeys'); return Reflect.ownKeys(t); }, getOwnPropertyDescriptor(t, k) { l2.push('gopd:' + String(k)); return Reflect.getOwnPropertyDescriptor(t, k); }, defineProperty(t, k, d) { l2.push('define:' + String(k) + ':' + JSON.stringify(d)); return Reflect.defineProperty(t, k, d); } });
Object.keys(p2); Object.seal(p2); p(l2.join(','), Object.isSealed(p2), Object.isFrozen(p2));
const l3 = [];
Object.defineProperties({}, new Proxy({ a: { value: 1 }, b: { value: 2 } }, { ownKeys(t) { l3.push('ownKeys'); return Reflect.ownKeys(t); }, getOwnPropertyDescriptor(t, k) { l3.push('gopd:' + k); return Reflect.getOwnPropertyDescriptor(t, k); }, get(t, k) { l3.push('get:' + String(k)); return t[k]; } }));
p(l3.join(','));
const l4 = [];
const d4 = new Proxy({ value: 1, writable: true }, { has(t, k) { l4.push('has:' + k); return k in t; }, get(t, k) { l4.push('get:' + k); return t[k]; } });
Object.defineProperty({}, 'k', d4); p(l4.join(','));
}

// the descriptors the checks read: Object.defineProperty's defaults and changes
{
const o = {};
Object.defineProperty(o, 'a', { value: 1 });
Object.defineProperty(o, 'b', { value: 2, writable: true, enumerable: true, configurable: true });
Object.defineProperty(o, 'c', { get() { return 3; }, enumerable: true });
p(gd(o, 'a'), gd(o, 'b'), tr(() => Object.keys(o)), tr(() => o.c));
p(tr(() => Object.defineProperty(o, 'a', { value: 2 })), tr(() => Object.defineProperty(o, 'a', { value: 1 })), tr(() => Object.defineProperty(o, 'a', { enumerable: true })), tr(() => Object.defineProperty(o, 'a', { writable: true })));
p(tr(() => Object.defineProperty(o, 'b', { writable: false })), gd(o, 'b'), tr(() => Object.defineProperty(o, 'b', { configurable: false })), gd(o, 'b'), tr(() => Object.defineProperty(o, 'b', { value: 3 })));
p(tr(() => Object.defineProperty(o, 'c', { value: 1 })), tr(() => Object.defineProperty(o, 'c', { get() {} })), tr(() => Object.keys(o)), tr(() => Reflect.defineProperty(o, 'a', { value: 9 })));
const o2 = { x: 1, y: 2, z: 3 };
Object.defineProperty(o2, 'y', { enumerable: false }); p(tr(() => Object.keys(o2)), gd(o2, 'y'), JSON.stringify(o2), tr(() => Object.entries(o2)));
Object.defineProperty(o2, 'y', { get() { return 'g'; }, configurable: true }); p(gd(o2, 'y'), o2.y, tr(() => Object.getOwnPropertyNames(o2)));
Object.defineProperty(o2, 'y', { value: 'v' }); p(gd(o2, 'y'), o2.y, tr(() => Object.getOwnPropertyNames(o2)));
p(tr(() => Object.defineProperties({}, { a: { value: 1, enumerable: true }, b: { get() { return 2; }, enumerable: true } })), tr(() => Object.defineProperties({}, { a: 1 })), tr(() => Object.defineProperty({}, 'a', 1)));
p(tr(() => Object.defineProperty({}, 'a', { get: 1 })), tr(() => Object.defineProperty({}, 'a', { set: 'x' })), tr(() => Object.defineProperty({}, 'a', { get() {}, value: 1 })), tr(() => Object.defineProperty(1, 'a', {})));
p(tr(() => Object.defineProperty(Object.preventExtensions({}), 'a', { value: 1 })), tr(() => Reflect.defineProperty(Object.preventExtensions({}), 'a', { value: 1 })), tr(() => Object.defineProperty(Object.preventExtensions({ a: 1 }), 'a', { value: 2 })));
const s = Symbol('s'), os = {}; Object.defineProperty(os, s, { value: 1, enumerable: true });
p(tr(() => Object.getOwnPropertySymbols(os).length), gd(os, s), tr(() => Object.defineProperty(os, s, { value: 2 })));
}

// freeze, seal, preventExtensions; the exotic objects' own
{
const fz = Object.freeze({ a: 1, get g() { return 1; } }), sl = Object.seal({ a: 1 }), pe = Object.preventExtensions({ a: 1 });
p(gd(fz, 'a'), gd(fz, 'g'), gd(sl, 'a'), gd(pe, 'a'));
p(Object.isFrozen(fz), Object.isSealed(fz), Object.isFrozen(sl), Object.isSealed(sl), Object.isFrozen(pe), Object.isSealed(pe), Object.isExtensible(pe), Object.isFrozen({}), Object.isSealed({}));
p(Object.isFrozen(Object.preventExtensions({})), Object.isSealed(Object.preventExtensions({})), Object.isFrozen(1), Object.isSealed('x'), Object.seal(1), Object.preventExtensions(2));
const nf = {}; Object.defineProperty(nf, 'k', { value: 1 }); Object.preventExtensions(nf); p(Object.isFrozen(nf), Object.isSealed(nf));
p(tr(() => Object.defineProperty(sl, 'a', { value: 5 })), sl.a, tr(() => Object.defineProperty(sl, 'a', { enumerable: false })), tr(() => Object.defineProperty(fz, 'a', { value: 1 })), tr(() => Object.defineProperty(fz, 'a', { value: 2 })));
const o = {}; Object.defineProperty(o, 'a', { value: 1 }); Object.defineProperty(o, 'b', { value: 2, writable: true });
p(tr(() => Reflect.set(fz, 'a', 2)), tr(() => Reflect.set(o, 'a', 5)), tr(() => Reflect.set(o, 'b', 5)), tr(() => Reflect.set(pe, 'zz', 5)), tr(() => Reflect.set(o, 'nope', 1)));
p(gd([1, 2], 'length'), gd([1, 2], 0), gd('ab', 0), gd('ab', 'length'), gd(new Uint8Array(2), 1), tr(() => Object.getOwnPropertyDescriptor(new Uint8Array(2), 5)));
function f(a, b) {} class C {}
p(gd(f, 'length'), gd(f, 'name'), gd(f, 'prototype'), gd(C, 'prototype'));
p(tr(() => Object.defineProperty([1, 2], 'length', { value: 1 })), tr(() => Object.defineProperty([], '0', { value: 'x' })), tr(() => Object.defineProperty(new Uint8Array(2), 0, { value: 7 })), tr(() => Reflect.defineProperty(new Uint8Array(2), 5, { value: 7 })));
p(tr(() => Reflect.defineProperty(new Uint8Array(2), 0, { value: 7, configurable: false })), tr(() => Reflect.defineProperty(new Uint8Array(2), 0, { get() {} })), tr(() => Object.defineProperty(new Uint8Array(2), 'x', { value: 7 }).x));
p(tr(() => Reflect.ownKeys([5])), tr(() => Reflect.ownKeys('ab')), tr(() => Reflect.ownKeys(Object.assign([1], { a: 1 }))));
}
