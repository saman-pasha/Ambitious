// tests/js/classes.js -- classes as V8 has them: a class's own scope with the immutable inner binding of its name,
// its elements in order (methods defined, computed field keys read once, when the class is made), then the static
// fields and blocks run with the class as this (super its parent's), each static block a method of its own; the
// early errors V8 gives a class's elements. Its .expect is node's output.
const t = (s, f) => { try { console.log(s, f()); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
t('static this', () => { class A { static x = this.name; } return A.x; });
t('static order', () => { class B { static s = B.m(); static m() { return 7; } } return B.s; });
t('static arrow this', () => { class C { static f = () => this.name; } return C.f(); });
t('static block', () => { class D { static #p = 1; static { D.q = D.#p + 1; this.r = this.name; } } return D.q + D.r; });
t('static computed order', () => { const log = []; class E { static [(log.push('k1'), 'a')] = log.push('v1'); [(log.push('k2'), 'b')] = 1; static [(log.push('k3'), 'c')]() {} } return log.join(); });
t('instance this', () => { class G { x = this.constructor.name; } return new G().x; });
t('static super', () => { class H0 { static base() { return 'b'; } } class H extends H0 { static v = super.base(); } return H.v; });
t('static new.target', () => { class I { static n = new.target; } return I.n; });
t('static field fn name', () => { class J { static f = function () {}; static g = () => {}; h = function () {}; } return J.f.name + J.g.name + new J().h.name; });
t('private static method', () => { class K { static #m() { return 3; } static v = K.#m(); } return K.v; });
t('expr name', () => { const C = class Inner { m() { return typeof Inner; } }; return new C().m() + ' ' + typeof Inner; });
t('inner survives', () => { class K { static f() { return K; } } const k = K; K = null; return k.f() === k; });
t('expr assign', () => { const C = class I { m() { I = 1; } }; new C().m(); });
t('extends self', () => { class A extends (class B {}) {} return A.name + Object.getPrototypeOf(A).name; });
t('heritage tdz', () => { class X extends X {} });
t('computed key tdz', () => { class Y { [Y]() {} } });
t('static field sees', () => { class Z { static s = Z.name; } return Z.s; });
t('field sees inner', () => { const W = class V { f = typeof V; }; return new W().f; });
t('class inner name', () => { class K { m() { K = 1; } } new K().m(); });
t('computed field keys once', () => { let n = 0; class F { [`k${n++}`] = n; } const a = new F(), b = new F(); return Object.keys(a).join() + Object.keys(b).join() + ' ' + a.k0 + b.k0 + ' ' + n; });
t('computed key toString once', () => { let calls = 0; const key = { toString() { calls++; return 'kk'; } }; class F { [key] = 1; } new F(); new F(); return calls; });
t('symbol key', () => { const s = Symbol('s'); class F { [s] = 5; static [s] = 6; } return new F()[s] + F[s]; });
t('static block vars', () => { class F { static { var a = 1; this.a = typeof a; } static { this.b = typeof a; } } return F.a + F.b; });
t('static block let', () => { class F { static { let x = 2; this.x = x; } } return F.x; });
t('static block super', () => { class P { static who() { return 'P'; } } class F extends P { static { this.w = super.who(); } } return F.w; });
t('static block arrow', () => { class F { static { const f = () => this; F.ok = f() === F; } } return F.ok; });
t('static block throw', () => { class F { static { throw new Error('in block'); } } });
t('static instance', () => { class S { x = 1; static inst = new S(); } return S.inst.x; });
t('static inherit', () => { class P { static p = 1; } class C extends P { static c = this.p + 1; } return C.c; });
t('static private access', () => { class F { static #x = 41; static get() { return F.#x + 1; } } return F.get(); });
t('class decl tdz outer', () => { const f = () => typeof K2; let r; try { f(); } catch (e) { r = e.constructor.name; } class K2 {} return r; });
t('static method this', () => { class F { static m() { return this; } } return F.m() === F; });
t('accessor static', () => { class F { static get g() { return 'g'; } static set g(v) { this._g = v; } } F.g = 3; return F.g + F._g; });
t('field init order', () => { const log = []; class F { a = log.push('a'); constructor() { log.push('ctor'); } b = log.push('b'); } new F(); return log.join(); });
t('derived field init', () => { const log = []; class P { constructor() { log.push('P'); } } class C extends P { f = log.push('f'); constructor() { log.push('pre'); super(); log.push('post'); } } new C(); return log.join(); });
t('static order full', () => { const log = []; class F { static a = log.push('a:' + typeof F.m); static m() {} static { log.push('block:' + typeof F.n); } static n() {} static b = log.push('b'); } return log.join(); });
t('class name prop', () => { class F { static name = 'custom'; } return F.name; });
t('anon class name', () => { const F = class { static n = this.name; }; return F.n; });
t('class toString', () => { class F { static { } } return String(F).length; });
t('eval in static', () => { class F { static x = eval('this.name'); } return F.x; });
t('new.target in field', () => { class F { t = new.target; } return new F().t; });
t('this in computed key', () => { const o = { m() { class F { [this.k] = 1; } return Object.keys(new F()).join(); }, k: 'kk' }; return o.m(); });
t('await as field name', () => { class F { await = 1; } return new F().await; });
t('field named constructor', () => eval('class F { "constructor" = 1 }'));
t('static prototype', () => eval('class F { static prototype = 1 }'));
t('static prototype method', () => eval('class F { static prototype() {} }'));
(function () {
const srcs = ['class F { "constructor" = 1 }', 'class F { constructor = 1 }', 'class F { static prototype = 1 }', 'class F { static prototype() {} }',
  'class F { static get prototype() {} }', 'class F { static "prototype" = 1 }', 'class F { static ["prototype"] = 1 }', 'class F { constructor() {} constructor() {} }',
  'class F { get constructor() {} }', 'class F { *constructor() {} }', 'class F { async constructor() {} }', 'class F { static constructor() {} }',
  'class F { static constructor = 1 }', 'class F { #constructor = 1 }', 'class F { x = arguments }', 'class F { static { arguments } }',
  'class F { static { return } }', 'class F { static { super() } }', 'class F extends Object { x = super() }',
  'class F { prototype = 1 }', 'class F { static async *prototype() {} }'];
for (const s of srcs) { try { eval(s); console.log(JSON.stringify(s), 'ok'); } catch (e) { console.log(JSON.stringify(s), e.constructor.name + ': ' + e.message); } }
})();
