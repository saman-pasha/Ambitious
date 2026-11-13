// A direct eval's code in a class reads, writes and tests the private names of the classes around it, as V8's does;
// one no enclosing class declares is V8's SyntaxError, and code an indirect eval or the Function constructor compiles
// names none.
function t(f) {
  try { console.log(String(f())); } catch (e) { console.log(e.constructor.name + ': ' + e.message); }
}
class A {
  #x = 1;
  #m() { return 'method ' + this.#x; }
  get #g() { return 'getter ' + this.#x; }
  set #g(v) { this.#x = v; }
  static #s = 'static field';
  static #sm() { return 'static method'; }
  read(code) { return eval(code); }
  static sread(code) { return eval(code); }
  arrow(code) { return (() => eval(code))(); }
  nested(code) { const inner = () => { return eval(code); }; return inner(); }
}
const a = new A();
t(() => a.read('this.#x'));
t(() => a.read('this.#x = 5; this.#x'));
t(() => a.read('this.#m()'));
t(() => a.read('this.#g'));
t(() => a.read('this.#g = 7; this.#x'));
t(() => a.read('#x in this'));
t(() => a.read('#x in {}'));
t(() => a.read('this.#x++, this.#x'));
t(() => a.read('this.#x += 10; this.#x'));
t(() => a.read('A.#s'));
t(() => A.sread('A.#sm()'));
t(() => A.sread('this.#s'));
t(() => a.arrow('this.#x'));
t(() => a.nested('this.#m()'));
t(() => a.read('({}).#x'));
t(() => a.read('A.#x'));
t(() => a.read('this.#nope'));
t(() => a.read('eval("this.#x")'));
t(() => a.read('(function () { return this.#x; }).call(this)'));
t(() => a.read('class B { #y = 2; f(o) { return o.#x + this.#y; } }; new B().f(this)'));
t(() => a.read('this.#m = 1'));
t(() => a.read('delete this.#x'));
// indirect eval and Function see no class
t(() => a.read('(0, eval)("this.#x")'));
t(() => a.read('new Function("return this.#x")'));
// a class nested in a class: both classes' names
class Outer {
  #o = 'outer';
  static Inner = class {
    #i = 'inner';
    both(outer, code) { return eval(code); }
  };
}
t(() => new Outer.Inner().both(new Outer(), 'outer.#o + " " + this.#i'));
// a field initializer's eval
class F {
  #p = 41;
  q = eval('this.#p + 1');
}
t(() => new F().q);
// the strict code of a class: eval's var stays in eval's code
t(() => a.read('var leak = 1; typeof leak'));
t(() => typeof leak);
