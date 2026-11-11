// tests/js/proxy.js -- Proxy, Reflect and the weak collections, each line against V8's (proxy.expect):
// every trap and the order V8 calls them in, receivers kept, revocation, proxies of arrays and
// functions, a reactive store as Vue 3 makes one; Reflect's functions; WeakMap, WeakSet,
// WeakRef; Map and Set by their hash index; Object's newer functions.

// the traps, Reflect, the weak collections
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const log = [];
const handler = {
  get(t, k, r) { log.push('get ' + String(k)); return k in t ? Reflect.get(t, k, r) : 'default:' + String(k); },
  set(t, k, v, r) { log.push('set ' + k); return Reflect.set(t, k, v * 2, r); },
  has(t, k) { log.push('has ' + k); return k.startsWith('x') || k in t; },
  deleteProperty(t, k) { log.push('delete ' + k); return delete t[k]; },
  ownKeys(t) { log.push('ownKeys'); return [...Reflect.ownKeys(t), 'virtual']; },
  getOwnPropertyDescriptor(t, k) { log.push('gopd ' + k); return k === 'virtual' ? { value: 'v', enumerable: true, configurable: true } : Reflect.getOwnPropertyDescriptor(t, k); },
};
const target = { a: 1 };
const px = new Proxy(target, handler);
p(px.a, px.missing, px['b']);
px.c = 5; p(target.c);
p('xyz' in px, 'a' in px, 'q' in px);
delete px.a; p(target.a);
p(Object.keys(px), JSON.stringify(px));
p(log.join('; '));
const fn = new Proxy(function (a, b) { return a + b; }, { apply(t, th, args) { return 'applied:' + t(...args); }, construct(t, args) { return { made: args }; } });
p(fn(2, 3), typeof fn, new fn(1, 2).made, fn.call(null, 4, 5));
const arr = new Proxy([1, 2, 3], {});
p(Array.isArray(arr), arr.length, JSON.stringify(arr), arr.map(x => x * 2), Object.prototype.toString.call(arr), [...arr]);
const { proxy, revoke } = Proxy.revocable({ k: 1 }, {});
p(proxy.k); revoke();
try { proxy.k; } catch (e) { p(e.constructor.name, e.message.includes('revoked')); }
class Base { hello() { return 'hi ' + this.name; } }
const inst = new Proxy(new Base(), { get(t, k, r) { return k === 'name' ? 'proxied' : Reflect.get(t, k, r); } });
p(inst.hello(), inst instanceof Base, Object.getPrototypeOf(inst) === Base.prototype);
const proto = new Proxy({}, { get(t, k, r) { return typeof k === 'string' ? 'from-proto:' + k : undefined; } });
const child = Object.create(proto); child.own = 1;
p(child.own, child.inherited);
p(Reflect.has({ a: 1 }, 'a'), Reflect.ownKeys({ a: 1, [Symbol('s')]: 2 }).length, Reflect.getPrototypeOf([]) === Array.prototype);
p(Reflect.apply(Math.max, null, [1, 5, 3]), Reflect.construct(Date, [0]) instanceof Date, Reflect.defineProperty({}, 'x', { value: 1 }));
const o2 = {}; Reflect.set(o2, 'y', 7); p(o2.y, Reflect.deleteProperty(o2, 'y'), 'y' in o2, Reflect.isExtensible(o2), Reflect.preventExtensions(o2), Reflect.isExtensible(o2));
const rcv = { get v() { return this.tag; }, tag: 'own' };
p(Reflect.get(rcv, 'v', { tag: 'receiver' }), Reflect.getOwnPropertyDescriptor({ q: 1 }, 'q'));
class Sub extends Base {} ; p(Reflect.construct(Base, [], Sub) instanceof Sub);
const wm = new WeakMap(); const k1 = {}, k2 = function () {};
wm.set(k1, 'one').set(k2, 'two');
p(wm.get(k1), wm.has(k2), wm.delete(k1), wm.has(k1), wm.get({}));
try { wm.set('str', 1); } catch (e) { p(e.constructor.name); }
const ws = new WeakSet([k1]); p(ws.has(k1), ws.has(k2), ws.add(k2) === ws, ws.has(k2));
const wr = new WeakRef(k1); p(wr.deref() === k1, Object.prototype.toString.call(wr), String(wm), typeof FinalizationRegistry);
const fr = new FinalizationRegistry(() => {}); fr.register(k1, 'held'); p(fr.unregister(k1));
p(Object.hasOwn({ z: 1 }, 'z'), Object.hasOwn({}, 'z'), Object.isExtensible(Object.preventExtensions({})), Object.isSealed(Object.seal({})));
p(Object.groupBy([1, 2, 3, 4], x => x % 2 ? 'odd' : 'even'), [...Map.groupBy([1, 2, 3], x => x > 1)].length);
p(Object.keys(Object.getOwnPropertyDescriptors({ a: 1, get b() { return 2; } })));
const m = new Map(); for (let i = 0; i < 2000; i++) m.set('k' + i, i); let sum = 0; for (let i = 0; i < 2000; i++) sum += m.get('k' + i);
for (let i = 0; i < 1000; i++) m.delete('k' + i); p(sum, m.size, m.has('k999'), m.has('k1000'), [...m.keys()][0]);
const st = new Set(); for (let i = 0; i < 3000; i++) st.add(i % 1500); p(st.size, st.has(1499), st.has(1500));
const nk = new Map([[NaN, 'nan'], [0, 'zero']]); p(nk.get(NaN), nk.get(-0), new Set([-0, 0, NaN, NaN]).size);
const big = new Map(); const objs = []; for (let i = 0; i < 100; i++) { const o = { i }; objs.push(o); big.set(o, i); } p(big.get(objs[57]), big.get({ i: 57 }));
}

// a reactive store, enumeration through a proxy, nested proxies, refusals
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
// a reactive store the way Vue 3 makes one
const targetMap = new WeakMap(); let active = null;
function track(t, k) { if (!active) return; let deps = targetMap.get(t); if (!deps) targetMap.set(t, deps = new Map()); let d = deps.get(k); if (!d) deps.set(k, d = new Set()); d.add(active); }
function trigger(t, k) { const deps = targetMap.get(t); if (!deps) return; const d = deps.get(k); if (d) [...d].forEach(f => f()); }
function reactive(o) { return new Proxy(o, { get(t, k, r) { track(t, k); const v = Reflect.get(t, k, r); return typeof v === 'object' && v ? reactive(v) : v; }, set(t, k, v, r) { const old = t[k]; const ok = Reflect.set(t, k, v, r); if (old !== v) trigger(t, k); return ok; } }); }
function effect(f) { active = f; f(); active = null; }
const state = reactive({ count: 1, nested: { x: 10 }, get double() { return this.count * 2; } });
const seen = [];
effect(() => seen.push('count=' + state.count + ' double=' + state.double));
effect(() => seen.push('x=' + state.nested.x));
state.count++; state.nested.x = 11; state.count = 5;
p(seen);
// for-in, spread, Object.assign, entries through a proxy
const px = new Proxy({ a: 1, b: 2 }, { get: (t, k) => typeof k === 'string' ? t[k] * 10 : t[k] });
const keys = []; for (const k in px) keys.push(k);
p(keys, { ...px }, Object.assign({}, px), Object.entries(px));
// nested proxies, a symbol key, a trap that throws
const inner = new Proxy({ v: 1 }, { get: (t, k) => k === 'v' ? 'inner' : undefined });
const outer = new Proxy(inner, {});
const sym = Symbol('s');
const sp = new Proxy({ [sym]: 'symval' }, {});
p(outer.v, sp[sym], sym in sp, Object.getOwnPropertySymbols(sp).length);
const bad = new Proxy({}, { get() { throw new Error('trap threw'); } });
try { bad.anything; } catch (e) { p(e.message); }
const counting = new Proxy([], { set(t, k, v) { t[k] = v; return true; } });
counting.push(1, 2); p(counting.length, counting[1]);
p(typeof new Proxy(class {}, {}), typeof new Proxy({}, {}));
class Point { constructor(x) { this.x = x; } }
const P2 = new Proxy(Point, { construct(t, args, nt) { return Reflect.construct(t, [args[0] * 100], nt); } });
p(new P2(3).x, new P2(1) instanceof Point);
const fnp = new Proxy(function named() {}, {}); p(fnp.name, fnp.length);
try { new Proxy({}, {})(); } catch (e) { p(e.constructor.name); }
try { Proxy({}, {}); } catch (e) { p(e.constructor.name); }
const ro = new Proxy({ k: 1 }, { set() { return false; }, deleteProperty() { return false; } });
ro.k = 2; p(ro.k, delete ro.k, Reflect.set(ro, 'k', 3));
p(Reflect.ownKeys(new Proxy({ b: 1, a: 2, 1: 0 }, {})), Object.keys(new Proxy([5, 6], {})));
}
