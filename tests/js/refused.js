'use strict';
// V8's messages for an assignment refused (an accessor's holder named), and Array.prototype an array
{
class Base { get only() { return 1; } }
Object.defineProperty(Base.prototype, 'ro', { value: 1, writable: false });
class Derived extends Base {
  a() { try { this.only = 1; } catch (e) { return e.message; } }
  b() { try { super.only = 1; } catch (e) { return e.message; } }
  c() { try { this.ro = 1; } catch (e) { return e.message; } }
  d() { try { super.ro = 1; } catch (e) { return e.message; } }
}
const d = new Derived();
console.log(d.a()); console.log(d.b()); console.log(d.c()); console.log(d.d());
const o = { __proto__: { get g() { return 1; } }, m() { try { super.g = 1; } catch (e) { return e.message; } } };
console.log(o.m());
class P { }
const pp = Object.create(P.prototype);
Object.defineProperty(P.prototype, 'q', { get() { return 1; } });
const q = { __proto__: pp, m() { try { super.q = 1; } catch (e) { return e.message; } } };
console.log(q.m());
}
{
class Base { get only() { return 1; } }
class Derived extends Base { a() { try { this.only = 1; } catch (e) { return e.message; } } }
console.log(new Derived().a());
const o = Object.create({ get g() { return 1; } });
try { o.g = 1; } catch (e) { console.log(e.message); }
const arr = [];
Object.defineProperty(Array.prototype, 'gx', { get() { return 1; }, configurable: true });
try { arr.gx = 1; } catch (e) { console.log(e.message); }
delete Array.prototype.gx;
try { Object.freeze(arr).push; arr.foo = 1; } catch (e) { console.log(e.message); }
}
{
console.log(Array.isArray(Array.prototype), Object.prototype.toString.call(Array.prototype), Array.prototype.length, JSON.stringify(Array.prototype));
Array.prototype[0] = 'x'; console.log(Array.prototype.length, [].length, [][0]); delete Array.prototype[0];
}
