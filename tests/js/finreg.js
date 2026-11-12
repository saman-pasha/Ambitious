// tests/js/finreg.js -- FinalizationRegistry, each line against V8's (finreg.expect, written
// by node --expose-gc finreg.js): its errors in V8's words; a cleanup called once for each
// registered target that died, with the value it was registered with, in a task of its own
// after the collection; a target still held is not cleaned up; unregister by a token takes
// a cell out before or after; a registry that is itself garbage calls nothing. Buffers
// whose views died through a collection keep working (the chain of views is weak). Every
// gc() stands in a timer's callback, where no native's work is under way, and every check
// waits a further task, as V8 posts the cleanups after the collection.

const p = (...a) => console.log(a.map(String).join(' '));
const t = (f) => { try { p(f()); } catch (e) { p(e.constructor.name + ': ' + e.message); } };

const seen = [];
const fr = new FinalizationRegistry((held) => { seen.push(held); });
const o = {};
t(() => fr.register(5, 1));
t(() => fr.register(o, o));
t(() => fr.register(o, 1, 5));
t(() => fr.unregister(5));
t(() => fr.unregister({}));
t(() => FinalizationRegistry.prototype.register.call({}, o, 1));
t(() => FinalizationRegistry.prototype.unregister.call({}, o));
t(() => new FinalizationRegistry(5));
t(() => FinalizationRegistry(() => {}));
t(() => fr.register(Symbol.for('registered'), 1));
p(String(fr), typeof fr.register, fr.register.length, fr.unregister.length);

// targets: dropped ones cleaned up; a kept one not; one unregistered first; one by a token
// that is itself the target
let a = { n: 'a' }, b = { n: 'b' }, c = { n: 'c' }, d = { n: 'd' };
const kept = { n: 'kept' };
const tokenB = {};
fr.register(a, 'held a');
fr.register(b, 'held b', tokenB);
fr.register(c, { label: 'held c' });
fr.register(d, 'held d', d);
fr.register(kept, 'held kept');
p('unregister b:', fr.unregister(tokenB), 'again:', fr.unregister(tokenB));

// a registry that is garbage: its cleanup never runs
let lost = new FinalizationRegistry(() => { p('the lost registry called'); });
let e = { n: 'e' };
lost.register(e, 'held e');

// a buffer and views over it, most of them dropped before the collection
const buf = new ArrayBuffer(16, { maxByteLength: 64 });
const keepView = new Uint8Array(buf, 4);
for (let i = 0; i < 50; i++) new Uint8Array(buf, 0, 4)[0] = i;
for (let i = 0; i < 50; i++) new DataView(buf, 8, 4).setUint8(0, i);

setTimeout(() => {
  a = b = c = null;
  d = null;
  lost = null;
  e = null;
  gc();
  // what the collection made of the buffer's views: new objects, then the buffer resized
  const fresh = [];
  for (let i = 0; i < 200; i++) fresh.push({ i, s: 'x' + i });
  buf.resize(32);
  keepView[0] = 7;
  p('the view kept:', keepView.length, keepView[0], new Uint8Array(buf)[4], fresh[199].s);
  buf.resize(8);
  p('out of bounds:', keepView.length, fresh[0].i);
  setTimeout(() => {
    p('cleaned up:', seen.map((h) => typeof h === 'string' ? h : h.label).sort().join(', '));
    p('kept still registered:', kept.n);
  }, 10);
}, 0);
