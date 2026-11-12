// Function.prototype.toString: a function's own source text, the exact slice of what was
// written (ECMA-262 2019+); a native's "function name() { [native code] }", a bound one's
// "function () { [native code] }".
function show(f) {
  try { console.log(JSON.stringify(Function.prototype.toString.call(f))); }
  catch (e) { console.log(e.constructor.name + ': ' + e.message); }
}

// declarations and expressions, comments and spacing kept
function plain(a, b) { return a + b; }
show(plain);
show(function () {});
show(function named /* c */ (x) { /* body */ });
var  spaced  =  function  (  a  ,  b  )  {  }  ;
show(spaced);
show(function
multi(
  a
) {
  return a;
});
function withDefault(a = 1, {b} = {}, ...rest) {}
show(withDefault);
show(function strings() { return "}" + '{' + `}${1}{`; });
function outer() { return function inner(q) { return q; }; }
show(outer());
show(outer);

// async functions, generators, arrows
show(async function af() { await 1; });
show(function* gen() { yield 1; });
show(function *gen2 () {});
show(async function* ag() {});
show((a) => a + 1);
show(a => a);
show(async (a) => a);
show(async a => a);
show(() => {});
show(() => ({}));
show((a = () => 1, b = a) => b);
show(async () => { await 0; });

// methods, getters, setters, computed and quoted keys
var o = {
  m() { return 1; },
  get g() { return 2; },
  set s(v) {},
  async am() {},
  *gm() {},
  async *agm() {},
  ['comp' + 'uted']() {},
  [Symbol.iterator]() {},
  'quoted key'() {},
  42() {},
  get [Symbol.toStringTag]() { return 'O'; },
  f: function () {},
  arrow: () => 1,
};
show(o.m);
show(Object.getOwnPropertyDescriptor(o, 'g').get);
show(Object.getOwnPropertyDescriptor(o, 's').set);
show(o.am);
show(o.gm);
show(o.agm);
show(o.computed);
show(o[Symbol.iterator]);
show(o['quoted key']);
show(o[42]);
show(Object.getOwnPropertyDescriptor(o, Symbol.toStringTag).get);
show(o.f);
show(o.arrow);
var x = { 'a-b': function () {} };
show(x['a-b']);

// classes: the whole class for the class and its constructor; its methods' own slices
class A {
  constructor(x) { this.x = x; }
  m() {}
  static sm() {}
  get g() { return 1; }
  static get sg() { return 1; }
  static async sam() {}
  static *sgm() {}
  #priv() {}
  static field = function () {};
  field2 = () => 2;
}
show(A);
show(A.prototype.constructor);
show(A.prototype.m);
show(A.sm);
show(Object.getOwnPropertyDescriptor(A.prototype, 'g').get);
show(Object.getOwnPropertyDescriptor(A, 'sg').get);
show(A.sam);
show(A.sgm);
show(A.field);
show(new A().field2);
class B extends A {}
show(B);
show(class {});
show(class   Named   extends A { m() {} });
show(Object.getOwnPropertyDescriptor(class { static get [Symbol.species]() {} }, Symbol.species).get);
var C = class /* anonymous */ { };
show(C);

// made by Function and by eval
show(Function('a', 'b', 'return a + b'));
show(new Function('return 1'));
show(Function());
show(Function('a,b', 'c', ''));
show(Function('/* x */ a', '// y\n'));
show(new Function('a', 'b'));
show(new Function('return function made() { return 2; }')());
show(eval('(function evf() { return 1 })'));
show(eval('(x) => x * 2'));
show(eval('function decl() {} decl'));
show(eval('({ m() { return 3; } })').m);

// natives: their names, getters' "get x", symbols' "[Symbol.x]"
show(Math.max);
show(Array.prototype.push);
show(Object);
show(Function);
show(Function.prototype);
show(parseInt);
show(Promise.resolve);
show(JSON.stringify);
show(Reflect.apply);
show(Symbol);
show(Object.getOwnPropertyDescriptor(Map.prototype, 'size').get);
show(Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags').get);
show(Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').get);
show(Array.prototype[Symbol.iterator]);
show(Symbol.prototype[Symbol.toPrimitive]);
show(RegExp.prototype[Symbol.split]);

// bound functions and proxies of functions
show(plain.bind(null));
show(Math.max.bind(null));
show((() => 1).bind(null));
show(function f2() {}.bind());
show(new Proxy(plain, {}));
show(new Proxy(function () {}, {}));

// not a function: V8's TypeError
show(new Proxy({}, {}));
show({});
show(1);
show(undefined);

// the conversions that reach it
console.log(String(plain));
console.log('' + (a => a));
console.log(`${o.m}`);
console.log(plain.toString === Function.prototype.toString);
console.log(Function.prototype.toString.length, Function.prototype.toString.name);
