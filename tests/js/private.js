const t = (s, f) => { try { const r = f(); console.log(s, typeof r === 'object' && r !== null ? JSON.stringify(r) : String(r)); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
const se = (s, src) => { try { new Function(src); console.log(s, 'ok'); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
class A {
  #x = 1; #y; static #s = 's'; #m() { return 'm' + this.#x; } get #g() { return 'g' + this.#x; } set #g(v) { this.#x = v; }
  static #sm() { return 'sm'; } static get #sg() { return 'sg'; }
  getX() { return this.#x; } setX(v) { this.#x = v; } getY() { return this.#y; } callM() { return this.#m(); } readG() { return this.#g; } writeG(v) { this.#g = v; }
  static getS() { return A.#s; } static callSm() { return A.#sm(); } static readSg() { return A.#sg; }
  static has(o) { return #x in o; } static hasM(o) { return #m in o; } static hasS(o) { return #s in o; }
  static read(o) { return o.#x; } static write(o) { o.#x = 5; } static callOn(o) { return o.#m(); } static getOn(o) { return o.#g; } static setOn(o) { o.#g = 1; }
  static writeM(o) { o.#m = 1; } static writeSg() { A.#sg = 1; }
}
const a = new A();
t('get', () => a.getX());
t('set', () => { a.setX(2); return a.getX(); });
t('undefined field', () => a.getY());
t('method', () => a.callM());
t('getter', () => a.readG());
t('setter', () => { a.writeG(7); return a.getX(); });
t('static field', () => A.getS());
t('static method', () => A.callSm());
t('static getter', () => A.readSg());
t('in', () => [A.has(a), A.has({}), A.hasM(a), A.hasM({}), A.hasS(A), A.hasS(a)].join());
t('in prim', () => A.has(1));
t('keys', () => [Object.keys(a).length, Reflect.ownKeys(a).length, Object.getOwnPropertySymbols(a).length, JSON.stringify(a), Object.getOwnPropertyNames(a).length].join(' '));
t('spread', () => Object.keys({ ...a }).length);
t('assign', () => Object.keys(Object.assign({}, a)).length);
t('static keys', () => Reflect.ownKeys(A).map(String).join());
t('read other', () => A.read({}));
t('write other', () => A.write({}));
t('call other', () => A.callOn({}));
t('get other', () => A.getOn({}));
t('set other', () => A.setOn({}));
t('write method', () => A.writeM(a));
t('write getter only', () => A.writeSg());
t('frozen', () => { const b = new A(); Object.freeze(b); b.setX(9); return b.getX(); });
t('proxy', () => { const p = new Proxy(a, {}); return A.has(p); });
t('proxy read', () => A.read(new Proxy(a, {})));
t('subclass', () => { class B extends A { #x = 'b'; bx() { return this.#x; } } const b = new B(); return [b.bx(), b.getX()].join(); });
t('twice', () => { class Base { constructor(o) { return o; } } class C extends Base { #f = 1; static has(o) { return #f in o; } } const o = {}; new C(o); try { new C(o); } catch (e) { return e.message + ' ' + C.has(o); } });
t('twice method', () => { class Base { constructor(o) { return o; } } class C extends Base { #m() {} } const o = {}; new C(o); try { new C(o); } catch (e) { return e.message; } });
t('nested', () => { class O { #p = 'outer'; static inner(o) { return new (class { get(x) { return x.#p; } })().get(o); } } return O.inner(new O()); });
t('nested shadow', () => { class O { #p = 'outer'; static make() { return class { #p = 'inner'; static read(x) { return x.#p; } }; } } const I = O.make(); return [I.read(new I()), (() => { try { return I.read(new O()); } catch (e) { return e.message; } })()].join(' | '); });
t('each eval', () => { const make = () => class { #v = 1; static has(o) { return #v in o; } }; const C1 = make(), C2 = make(); return [C1.has(new C1()), C1.has(new C2())].join(); });
t('optional', () => { class C { #v = 3; static r(o) { return o?.#v; } } return [C.r(new C()), C.r(null)].join(); });
t('compound', () => { class C { #v = 3; inc() { this.#v += 2; this.#v++; return this.#v; } } return new C().inc(); });
t('destructure', () => { class C { #v = 0; set(o) { ({ a: this.#v } = o); return this.#v; } } return new C().set({ a: 42 }); });
t('call this', () => { class C { #v = 1; #f() { return this; } t() { return this.#f() === this; } } return new C().t(); });
t('method identity', () => { class C { #m() {} static same(a, b) { return a.#m === b.#m; } } return C.same(new C(), new C()); });
t('getter name', () => { class C { #m() {} static n(o) { return o.#m.name; } } return C.n(new C()); });
t('typeof', () => { class C { #m() {} static k(o) { return typeof o.#m; } } return C.k(new C()); });
t('static on instance', () => { class C { static #s = 1; static r(o) { return o.#s; } } return C.r(new C()); });
t('static method other', () => { class C { static #sm() {} static r(o) { return o.#sm(); } } return C.r({}); });
t('field order', () => { const log = []; class C { a = log.push('a'); #b = log.push('b'); c = log.push('c'); } new C(); return log.join(); });
t('fields see methods', () => { class C { #v = this.#m(); #m() { return 'mm'; } get() { return this.#v; } } return new C().get(); });
t('toString', () => { class C { #x = 1; } return String(C).length > 0; });
se('undeclared', 'class C { m() { return this.#nope; } }');
se('outside', 'this.#x');
se('dup', 'class C { #a; #a; }');
se('dup method', 'class C { #a; #a() {} }');
se('get set ok', 'class C { get #a() {} set #a(v) {} }');
se('get get', 'class C { get #a() {} get #a() {} }');
se('static mix', 'class C { static get #a() {} set #a(v) {} }');
se('delete', 'class C { #a; m() { delete this.#a; } }');
se('ctor name', 'class C { #constructor; }');
se('ctor method', 'class C { #constructor() {} }');
se('in undeclared', 'class C { m(o) { return #z in o; } }');
se('in bad', 'class C { #a; m(o) { return #a; } }');
se('super private', 'class C extends Object { m() { return super.#a; } }');
se('optional call ok', 'class C { #a() {} m(o) { return o?.#a(); } }');
se('await ref', 'class C { static { await; } }');
se('await var', 'class C { static { var await; } }');
se('await let', 'class C { static { let await = 1; } }');
se('await in fn', 'class C { static { function f() { var await; } } }');
se('await in arrow body', 'class C { static { () => { var await; }; } }');
se('await label', 'class C { static { await: 1; } }');
se('await expr', 'class C { static { await 1; } }');
