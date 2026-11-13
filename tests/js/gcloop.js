// Collections inside a running script: loops that make garbage while what they keep (closures and their
// environments, arrays and maps growing, strings joined, generators suspended, promises pending, class instances,
// typed arrays, symbols, a WeakRef's target held to the job's end) is read back whole afterwards, as V8 keeps it.
const p = (...a) => console.log(a.map((x) => (typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x))).join(' '));
const keep = [];
const map = new Map();
let joined = '';
function counter() { let c = 0; return () => ++c; }
const counters = [];
function* gen(k) { let x = 0; for (;;) x += yield k + x; }
const gens = [];
const pending = [];
class Pt { constructor(x, y) { this.x = x; this.y = y; } len() { return Math.hypot(this.x, this.y); } }
const syms = [];
let ref = new WeakRef({ held: 'until the job ends' });
for (let i = 0; i < 2000000; i++) {
  // garbage: an object, an array and a string each turn
  const g = { i, a: [i, i + 1], s: 'x' + i };
  if (g.a[1] !== i + 1) throw new Error('lost ' + i);
  if (i % 20000 === 0) {
    keep.push({ i, nested: { deep: [i, String(i)] } });
    map.set('k' + i, new Pt(i, i + 1));
    joined += String.fromCharCode(65 + (i / 20000) % 26);
    const c = counter(); c(); counters.push(c);
    const it = gen(i); it.next(); gens.push(it);
    let resolve;
    pending.push({ pr: new Promise((r) => { resolve = r; }), resolve });
    syms.push(Symbol('s' + i));
  }
}
p('kept', keep.length, keep[0], keep[99]);
p('map', map.size, map.get('k0').len(), map.get('k1980000').x);
p('joined', joined.length, joined.slice(0, 30));
p('counters', counters.map((c) => c()).slice(0, 5).join(), counters.length);
p('gens', gens.slice(0, 3).map((g) => g.next(5).value).join(), gens.length);
p('symbols', syms.length, String(syms[7]), syms[3].description);
p('weakref within the job', ref.deref() !== undefined, ref.deref() && ref.deref().held);
// a closure's environment made in each turn, kept for some
const fns = [];
for (let i = 0; i < 1000000; i++) {
  const box = { v: i * 2 };
  const f = () => box.v;
  if (i % 100000 === 0) fns.push(f);
}
p('closures', fns.map((f) => f()).join());
// typed arrays and buffers among the garbage
let sum = 0;
const kept8 = [];
for (let i = 0; i < 300000; i++) {
  const t = new Uint8Array(16);
  t[0] = i & 255;
  sum += t[0];
  if (i % 50000 === 0) kept8.push(t);
}
p('typed', sum, kept8.length, kept8.map((t) => t[0]).join());
// a string grown in a loop, and pieces cut from it
let s = '';
for (let i = 0; i < 200000; i++) { s += String.fromCharCode(97 + (i % 26)); const junk = s.slice(-3) + i; }
p('string', s.length, s.slice(0, 10), s.slice(-10));
// the pending promises settle in later jobs
Promise.all(pending.map((x, k) => { x.resolve(k); return x.pr; })).then((v) => p('promises', v.length, v[0], v[99]));
// an async function's loop after an await (a microtask's run)
(async () => {
  await null;
  const out = [];
  for (let i = 0; i < 1000000; i++) { const g = [i]; if (i % 250000 === 0) out.push(g); }
  p('after await', out.map((g) => g[0]).join());
})();
// a timer's loop
setTimeout(() => {
  const m = new Map();
  for (let i = 0; i < 1000000; i++) { const o = { i }; if (i % 200000 === 0) m.set(i, o); }
  p('timer', [...m.keys()].join(), m.get(800000).i);
}, 0);
