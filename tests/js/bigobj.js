// Objects of many keys (past the engine's 32, where its shapes are looked in through tables): their keys, order,
// deletion and re-addition, enumeration with prototypes, attributes, and the scripts' globals, as V8 has them.
function keysOf(o) { return Object.keys(o).join(','); }
function sum(o) { let s = 0; for (const k in o) s += o[k]; return s; }

// a map grown key by key, read back
const m = {};
for (let i = 0; i < 300; i++) m['k' + i] = i;
let s = 0;
for (let i = 0; i < 300; i++) s += m['k' + i];
console.log('grown', Object.keys(m).length, s, m.k0, m.k299, m.k300, 'k150' in m, 'k300' in m);

// trailing keys deleted and added again: the same keys, the same order
for (let i = 299; i >= 250; i--) delete m['k' + i];
console.log('trimmed', Object.keys(m).length, m.k249, m.k250, 'k250' in m, Object.keys(m).slice(-3).join(','));
for (let i = 250; i < 300; i++) m['k' + i] = -i;
console.log('readded', Object.keys(m).length, m.k250, m.k299, Object.keys(m).slice(-3).join(','));

// a key in the middle deleted and added again: last in the order; deleted again (the last now), the one before it back
delete m.k100;
console.log('middle', m.k100, 'k100' in m, m.hasOwnProperty('k100'), Object.keys(m).length);
m.k100 = 'again';
console.log('again', m.k100, Object.keys(m).slice(-2).join(','), Object.keys(m).indexOf('k100'));
delete m.k100;
console.log('gone', m.k100, 'k100' in m, Object.keys(m).slice(-2).join(','), Object.keys(m).length);
m.k100 = 'thrice';
console.log('thrice', m.k100, Object.keys(m).slice(-2).join(','));
delete m.k299;
delete m.k100;
console.log('two gone', m.k299, m.k100, Object.keys(m).slice(-2).join(','), Object.keys(m).length);

// an add-and-delete loop at the end of a big object
const q = {};
for (let i = 0; i < 100; i++) q['q' + i] = i;
let t = 0;
for (let i = 0; i < 5000; i++) { q['t' + i] = i; t += q['t' + i]; delete q['t' + i]; }
console.log('loop', t, Object.keys(q).length, q.t0, 't4999' in q, keysOf(q).length);

// the same key deleted and added again and again
for (let i = 0; i < 1000; i++) { delete q.q50; q.q50 = i; }
console.log('churn', q.q50, Object.keys(q).length, Object.keys(q).slice(-2).join(','));

// two maps grown in step with the same keys
const a = {}, b = {};
for (let i = 0; i < 500; i++) { a['w' + i] = i; b['w' + i] = 2 * i; }
let sa = 0, sb = 0;
for (let i = 0; i < 500; i++) { sa += a['w' + i]; sb += b['w' + i]; }
console.log('lockstep', sa, sb, a.w499, b.w499, keysOf(a) === keysOf(b));
delete a.w499;
console.log('apart', 'w499' in a, 'w499' in b, Object.keys(a).length, Object.keys(b).length);

// many objects of one big shape
const rows = [];
for (let r = 0; r < 50; r++) {
  const o = {};
  for (let c = 0; c < 60; c++) o['c' + c] = r * 100 + c;
  rows.push(o);
}
let rs = 0;
for (const o of rows) rs += o.c0 + o.c59 + o['c' + 30];
console.log('rows', rs, Object.keys(rows[7]).length, rows[49].c59);

// for-in through prototypes: a key shadowed is enumerated once, a prototype's own keys after the object's
const proto = {};
for (let i = 0; i < 80; i++) proto['p' + i] = i;
const child = Object.create(proto);
for (let i = 40; i < 120; i++) child['p' + i] = 1000 + i;
const seen = [];
for (const k in child) seen.push(k);
console.log('forin', seen.length, seen.slice(0, 3).join(','), seen.slice(78, 82).join(','), seen.slice(-2).join(','));
console.log('forin values', sum(child), child.p0, child.p40, child.p119);
Object.defineProperty(child, 'p45', { enumerable: false, value: 5 });
const seen2 = [];
for (const k in child) seen2.push(k);
console.log('hidden', seen2.length, seen2.includes('p45'), child.p45, Object.keys(child).length);

// attributes on a big object: read-only, hidden, accessors, frozen, sealed
const d = {};
for (let i = 0; i < 64; i++) d['d' + i] = i;
Object.defineProperty(d, 'd10', { writable: false });
d.d10 = 99;
Object.defineProperty(d, 'acc', { get() { return this.d1 + this.d2; }, enumerable: true, configurable: true });
console.log('attrs', d.d10, d.acc, Object.getOwnPropertyDescriptor(d, 'd10').writable, Object.keys(d).length, keysOf(d).slice(-10));
delete d.acc;
console.log('acc gone', d.acc, 'acc' in d, Object.keys(d).length);
Object.seal(d);
delete d.d5;
d.d5 = 'x';
d.newkey = 1;
console.log('sealed', d.d5, d.newkey, Object.isSealed(d), Object.keys(d).length);
Object.freeze(d);
d.d6 = 'y';
console.log('frozen', d.d6, Object.isFrozen(d));

// JSON of a big object, both ways
const j = {};
for (let i = 0; i < 200; i++) j['j' + i] = i % 3 ? i : 'v' + i;
const text = JSON.stringify(j);
const back = JSON.parse(text);
console.log('json', text.length, Object.keys(back).length, back.j0, back.j199, JSON.stringify(back) === text);
console.log('entries', Object.entries(back).slice(-2).join(';'), Object.values(back).filter(x => typeof x === 'string').length);

// symbols and integer keys among many
const sy = Symbol('s');
const mix = {};
for (let i = 0; i < 50; i++) mix['n' + i] = i;
mix[sy] = 'sym';
mix[7] = 'seven';
mix[3] = 'three';
mix['10'] = 'ten';
console.log('mixed', Object.keys(mix).slice(0, 4).join(','), mix[sy], Reflect.ownKeys(mix).length, mix[10]);
delete mix[sy];
console.log('no sym', mix[sy], Object.getOwnPropertySymbols(mix).length);

// spread, assign and a prototype changed under many keys
const copy = { ...m };
console.log('spread', Object.keys(copy).length, copy.k0, copy.k298);
const asg = Object.assign({}, q, { extra: 1 });
console.log('assign', Object.keys(asg).length, asg.q99, asg.extra);
Object.setPrototypeOf(asg, { inherited: 'yes' });
console.log('proto', asg.inherited, asg.q0, Object.keys(asg).length);

// a script's globals: many vars and functions
globalThis.g0 = 0;
for (let i = 1; i <= 120; i++) globalThis['g' + i] = i;
function f0() { return 'f0'; }
console.log("globals", globalThis.g0, globalThis.g120, typeof f0, Object.keys(globalThis).filter(k => /^g\d+$/.test(k)).length);
delete globalThis.g120;
console.log('global gone', typeof g120, 'g119' in globalThis);
