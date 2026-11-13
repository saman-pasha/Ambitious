// The order of an object's keys after a deletion, a function's own keys defined again, a class heritage's private
// names and a sloppy function's arguments property: each as V8 has it.

// a key deleted and added again is a new one: listed last
var o = { a: 1, b: 2, c: 3 };
delete o.a; o.a = 4;
console.log(Object.keys(o).join(), JSON.stringify(o));
delete o.b; Object.defineProperty(o, 'b', { value: 5, enumerable: true, writable: true, configurable: true });
console.log(Object.keys(o).join(), JSON.stringify(o));
var o2 = { x: 1 }; o2.y = 2; delete o2.x; o2.x = 3; o2.z = 4;
for (var k in o2) console.log('for-in', k, o2[k]);
// an assignment the same site wrote before (its cache) still adds the key at the end
function put(t, v) { t.p = v; }
var o3 = {}; put(o3, 1); o3.q = 2; delete o3.p; put(o3, 3);
console.log(Object.keys(o3).join(), o3.p);
// a deletion of the last key and an add of it again, many times: nothing grows, the order kept
var o4 = { first: 1 };
for (var i = 0; i < 100000; i++) { o4.k = i; delete o4.k; }
o4.k = 'end'; o4.last = 1;
console.log(Object.keys(o4).join(), o4.k);
// deleted from the middle and the end, then added again
var o5 = { a: 1, b: 2, c: 3, d: 4 };
delete o5.b; delete o5.d; delete o5.c; o5.d = 5; o5.b = 6;
console.log(Object.keys(o5).join(), JSON.stringify(o5));
// a deleted key's prototype setter is asked when it is assigned again
var proto = { set s(v) { console.log('proto setter', v); } };
var o6 = Object.create(proto); Object.defineProperty(o6, 's', { value: 1, writable: true, configurable: true, enumerable: true });
delete o6.s; o6.s = 2;
console.log(Object.keys(o6).join() + '|', Object.prototype.hasOwnProperty.call(o6, 's'));
// symbols keep their own order
var sa = Symbol('a'), sb = Symbol('b');
var o7 = { [sa]: 1, [sb]: 2 }; delete o7[sa]; o7[sa] = 3;
console.log(Object.getOwnPropertySymbols(o7).map(String).join());
// arrays' named keys
var arr = [1, 2]; arr.x = 1; arr.y = 2; delete arr.x; arr.x = 3;
console.log(Object.keys(arr).join());

// a function's length and name deleted and defined again: listed last
function g(a, b) {}
console.log(Object.getOwnPropertyNames(g).join());
delete g.length; Object.defineProperty(g, 'length', { value: 5, configurable: true });
console.log(Object.getOwnPropertyNames(g).join(), g.length);
delete g.name; g.name2 = 1; Object.defineProperty(g, 'name', { value: 'n', configurable: true });
console.log(Object.getOwnPropertyNames(g).join(), g.name);
// defined again without a deletion: in its place
function f(a) {}
Object.defineProperty(f, 'length', { value: 7 });
console.log(Object.getOwnPropertyNames(f).join(), f.length);
delete f.length;
console.log(Object.getOwnPropertyNames(f).join(), f.length);
Object.defineProperty(f, 'length', { value: 8, configurable: true });
console.log(Object.getOwnPropertyNames(f).join(), f.length);
f.x = 1; delete f.name; f.name = 3;
console.log(Object.getOwnPropertyNames(f).join(), JSON.stringify(f.name));
class K { static m() {} }
delete K.name; Object.defineProperty(K, 'name', { value: 'K2' });
console.log(Object.getOwnPropertyNames(K).join(), K.name);
class K3 { static name() {} static m() {} }
console.log(Object.getOwnPropertyNames(K3).join());
var bf = function (a, b) {}.bind(null);
console.log(Object.getOwnPropertyNames(bf).join());
delete bf.length; bf.length = 1; Object.defineProperty(bf, 'length', { value: 2, configurable: true });
console.log(Object.getOwnPropertyNames(bf).join(), bf.length);
var mn = Math.min; delete mn.name; Object.defineProperty(mn, 'name', { value: 'q', configurable: true });
console.log(Object.getOwnPropertyNames(mn).join(), mn.name);
var ar = () => 1; delete ar.length; ar.length = 3; Object.defineProperty(ar, 'length', { value: 3 });
console.log(Object.getOwnPropertyNames(ar).join(), ar.length);

// a class heritage reads the enclosing class's private names, not its own
class Outer {
  static #p = 'outer #p';
  static #m() { return 'outer #m'; }
  static make() {
    return class Inner extends (console.log('extends sees', Outer.#p, Outer.#m()), Object) {
      static #p = 'inner #p';
      static get() { return Inner.#p; }
    };
  }
  static nested() {
    // a class in the heritage reads its own private names in its body
    return class A extends (class B { #q = 'B #q'; static read(o) { return o.#q; } static make() { return new B(); } }) {
      static check() { return A.read(A.make()); }
    };
  }
  static deeper() {
    // a heritage within a heritage reads past both classes
    return class C extends (class D extends (console.log('deeper sees', Outer.#p), Object) { static #p = 'D'; }) { static #p = 'C'; };
  }
}
console.log(Outer.make().get());
console.log(Outer.nested().check());
Outer.deeper();
try {
  eval('class A1 { #q = 1; static f() { return class B1 extends (function (o) { return o.#q, Object; })(new A1()) { #q = 2 }; } }');
  console.log('no early error');
} catch (e) { console.log(e.constructor.name + ': ' + e.message); }
try {
  eval('class C1 extends (function (o) { return o.#own, Object; }) { #own = 1 }');
  console.log('no early error');
} catch (e) { console.log(e.constructor.name + ': ' + e.message); }
try {
  class Self extends (console.log(typeof Self), Object) {}
} catch (e) { console.log(e.constructor.name + ': ' + e.message); }

// a sloppy function's arguments property: every argument it was given
function h(a) { return h.arguments.length + ':' + Array.prototype.join.call(h.arguments); }
console.log(h(1, 2, 3), h(), h(1));
function h2(a) { a = 9; return h2.arguments[0]; }
console.log(h2(1));
function h3(a) { arguments; a = 9; return h3.arguments[0]; }
console.log(h3(1));
function h4(a, b) { var x = 1, y = 2; return Array.prototype.join.call(h4.arguments) + ' ' + (x + y); }
console.log(h4(1, 2, 3, 4, 5));
function h5() { return h5.arguments.length; }
console.log(h5(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
console.log([10, 20, 30].map(function (v) { return arguments.callee.arguments.length; }).join());
function h6(a) { return [1, 2].map(function cb(v) { return cb.arguments.length + '/' + h6.arguments.length; }).join(); }
console.log(h6('x', 'y'));
function h7(a, b) { return h7.arguments[1] + h7.arguments[2]; }
console.log(h7.call(null, 'p', 'q', 'r'), h7.apply(null, ['s', 't', 'u']), Reflect.apply(h7, null, [1, 2, 3]));
function h8(a) { function inner() { return a; } a = 'changed'; return h8.arguments[0] + ' ' + inner(); }
console.log(h8('orig', 'extra'));
function h9(a) { try { throw 1; } catch (e) { return h9.arguments.length + ':' + h9.arguments[2]; } }
console.log(h9(1, 2, 3));
console.log(h.arguments);
