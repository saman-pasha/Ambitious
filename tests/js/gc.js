// tests/js/gc.js -- the collector, each line against V8's (gc.expect, written by
// node --expose-gc gc.js: gc() collects at once there, as it does here): what lives
// keeps its contents through collections (objects, arrays, strings flat, joined and cut,
// maps, sets, closures and their environments, BigInts, generators suspended, promises
// pending, typed arrays, symbols, getters, bound functions, classes), garbage goes, a
// WeakRef's target that nothing else holds is cleared, a WeakMap keeps its live keys.
// gc() stands in a timer's callback, where no native's work is under way.

const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));
const keep = { name: 'kept', list: [1, 2, 3], nested: { deep: ['a', { b: 'c' }] } };
let joined = '';
for (let i = 0; i < 200; i++) joined += String.fromCharCode(97 + i % 26);
const cut = joined.slice(10, 40), cut2 = joined.substring(100, 110);
const map = new Map(), set = new Set();
for (let i = 0; i < 100; i++) { map.set('k' + i, { v: i }); set.add(i * 3); }
function counter() { let c = 0; return () => ++c; }
const tick = counter();
tick(); tick();
const big = 2n ** 200n + 12345n, small = -42n;
function* gen() { let x = yield 'first'; while (true) x = yield 'got ' + x; }
const g = gen();
g.next();
let resolveLater;
const pending = new Promise(r => { resolveLater = r; });
const ta = new Uint16Array([10, 20, 30, 40]);
const sym = Symbol('described');
const withGetter = { get doubled() { return this.base * 2; }, base: 21 };
const bound = function (a, b) { return this.k + a + b; }.bind({ k: 'K' }, 'A');
class Shape { constructor(n) { this.n = n; } area() { return this.n * 2; } }
const shapes = Array.from({ length: 50 }, (_, i) => new Shape(i));
const record = () => [keep.name, keep.list.join(), JSON.stringify(keep.nested), joined.length, joined.slice(0, 5), cut, cut2,
    map.size, map.get('k57').v, set.has(99), set.size, tick(), String(big), String(small), g.next('x').value,
    Array.from(ta).join(), sym.toString(), withGetter.doubled, bound('B'), shapes[37].area(), shapes.length].join(' | ');
p('before', record());
let target = { big: new Array(1000).fill('t') };
const ref = new WeakRef(target);
const wm = new WeakMap();
const liveKey = {};
wm.set(liveKey, 'live value');
wm.set(target, 'dead value');
target = null;
for (let i = 0; i < 20000; i++) { const junk = { i, s: 'junk' + i, arr: [i, i + 1] }; if (i === -1) p(junk); }
setTimeout(() => {
    gc();
    p('after one', record());
    p('weakref cleared', ref.deref() === undefined);
    p('weakmap live key', wm.get(liveKey), wm.has(liveKey));
    for (let round = 0; round < 3; round++) {
        let tmp = [];
        for (let i = 0; i < 5000; i++) tmp.push({ round, i, text: 'r' + round + 'i' + i });
        tmp = null;
        gc();
    }
    p('after rounds', record());
    const made = [];
    for (let i = 0; i < 1000; i++) made.push({ i, s: 'new' + i, b: BigInt(i) * 3n });
    gc();
    p('made after', made.length, made[999].s, String(made[500].b), made.every((o, i) => o.i === i && o.s === 'new' + i));
    setTimeout(() => {
        resolveLater('resolved after collections');
        pending.then(v => p('promise', v));
        p('generator', g.next('y').value, g.next('z').value);
        gc();
        p('last', record());
    }, 1);
}, 1);
