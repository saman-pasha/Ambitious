// Assignment to super properties (ECMA-262 13.3.7, 13.15: a super reference's [[Set]] on the home object's
// prototype, this the receiver), as V8 runs it.
class Base {
  constructor() { this.log = []; }
  set acc(v) { this.log.push('Base set acc ' + v + ' on ' + this.constructor.name); }
  get acc() { return 'Base get acc'; }
  get only() { return 'getter only'; }
}
Object.defineProperty(Base.prototype, 'ro', { value: 'read-only', writable: false, configurable: true });
Base.prototype.plain = 'proto plain';

class Derived extends Base {
  setPlain() { super.plain = 'own'; return [this.plain, Object.hasOwn(this, 'plain'), Base.prototype.plain]; }
  setNew() { super.fresh = 1; return [this.fresh, Object.hasOwn(this, 'fresh'), 'fresh' in Base.prototype]; }
  setAcc() { super.acc = 5; return this.log.join('|'); }
  setComputed(k) { super[k] = 'computed ' + String(k); return this[k]; }
  compound() { this.n = 1; super.n += 10; super['n'] *= 2; return this.n; }
  logical() {
    this.a = 0; this.b = 1; this.c = null;
    super.a ||= 'a'; super.b &&= 'b'; super.c ??= 'c';
    return [this.a, this.b, this.c].join();
  }
  update() {
    this.k = 5;
    const post = super.k++;
    const pre = ++super.k;
    const dec = super['k']--;
    return [post, pre, dec, this.k].join();
  }
  value() { let r = (super.v = 42); return [r, this.v].join(); }
  chained() { return (super.x = super.y = 7, [this.x, this.y].join()); }
  readOnly() { try { super.ro = 'x'; return 'no error'; } catch (e) { return e.constructor.name + ': ' + e.message; } }
  getterOnly() { try { super.only = 'x'; return 'no error'; } catch (e) { return e.constructor.name + ': ' + e.message; } }
  destructure() {
    [super.d0, super.d1 = 'dflt'] = ['x'];
    ({ p: super.d2, ...super.rest } = { p: 'P', q: 1, r: 2 });
    return [this.d0, this.d1, this.d2, JSON.stringify(this.rest)].join();
  }
  loops() {
    const out = [];
    for (super.it of ['a', 'b']) out.push(this.it);
    for (super.key in { k1: 1, k2: 2 }) out.push(this.key);
    return out.join();
  }
  arrow() { const f = () => { super.viaArrow = 'arrow'; }; f(); return this.viaArrow; }
  deleteSuper() { try { delete super.plain; return 'deleted'; } catch (e) { return e.constructor.name + ': ' + e.message; } }
  keyOrder() {
    const log = [];
    const key = { toString() { log.push('key'); return 'ko'; } };
    super[(log.push('expr'), key)] = (log.push('rhs'), 1);
    return log.join() + ' ' + this.ko;
  }
  static staticSet() { super.sx = 'static'; return [this.sx, Object.hasOwn(this, 'sx'), Object.hasOwn(Base, 'sx')].join(); }
}

const d = new Derived();
console.log('plain', JSON.stringify(d.setPlain()));
console.log('new', JSON.stringify(d.setNew()));
console.log('accessor', d.setAcc());
console.log('computed', d.setComputed('ck'), d.setComputed(Symbol.for('s')), d.setComputed(3), Object.keys(d).includes('3'));
console.log('compound', d.compound());
console.log('logical', d.logical());
console.log('update', d.update());
console.log('value', d.value());
console.log('chained', d.chained());
console.log('readonly', d.readOnly());
console.log('getter only', d.getterOnly());
console.log('destructure', d.destructure());
console.log('loops', d.loops());
console.log('arrow', d.arrow());
console.log('delete', d.deleteSuper());
console.log('key order', d.keyOrder());
console.log('static', Derived.staticSet());

// object literals' methods: their home object the literal; sloppy assignment's failure silent
const proto = { set s(v) { this.got = 'proto setter ' + v; } };
Object.defineProperty(proto, 'frozen', { value: 1, writable: false });
const lit = {
  __proto__: proto,
  m() { super.s = 'v'; super.frozen = 2; super.other = 3; return [this.got, this.frozen, this.other].join(); }
};
console.log('literal', lit.m());
const strictLit = { __proto__: proto, m() { 'use strict'; try { super.frozen = 2; return 'no error'; } catch (e) { return e.message; } } };
console.log('strict literal', strictLit.m());

// a receiver that is not extensible, a prototype that is a proxy
const frozenObj = Object.preventExtensions({ __proto__: { m() { 'use strict'; try { super.z = 1; return 'no error'; } catch (e) { return e.message; } } } });
console.log('not extensible', frozenObj.m());
const traps = [];
const px = new Proxy({}, { set(t, k, v, r) { traps.push('set ' + String(k) + ' ' + v + ' ' + (r === withPx)); return true; } });
const withPx = { __proto__: px, m() { super.t = 9; return traps.join(); } };
console.log('proxy', withPx.m());

// a class extending null: no prototype to set on
class N extends null { static m() { try { super.q = 1; return 'no error'; } catch (e) { return e.constructor.name + ': ' + e.message; } } }
console.log('null proto', N.m(), Object.hasOwn(N, 'q'));

// a null home prototype, a primitive receiver
{
class N extends null { r() { return super.x; } w() { super.x = 1; return 'set'; } }
for (const f of ['r', 'w']) { try { console.log(f, N.prototype[f].call({})); } catch (e) { console.log(f, e.constructor.name + ': ' + e.message); } }
const o = { __proto__: null, r() { return super.y; }, w() { super.y = 2; return 'set ' + this.y; } };
for (const f of ['r', 'w']) { try { console.log(f, o[f]()); } catch (e) { console.log(f, e.constructor.name + ': ' + e.message); } }
const s = { m() { 'use strict'; super.z = 1; return typeof this; } };
try { console.log('prim', s.m.call(5)); } catch (e) { console.log('prim', e.constructor.name + ': ' + e.message); }
const s2 = { m() { super.z = 1; return typeof this; } };
try { console.log('prim sloppy', s2.m.call(5)); } catch (e) { console.log('prim sloppy', e.constructor.name + ': ' + e.message); }
}
// delete of a super reference: its key evaluated, then V8's ReferenceError
{
class A { m() { let c = 0; const kk = { toString() { c++; return 'q'; } }; try { delete super[(c += 10, kk)]; } catch (e) { return c + ' ' + e.message; } } }
console.log(new A().m());
}
