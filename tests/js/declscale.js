// The compiler's names over many declarations (each scope's names indexed): duplicates, shadowing, hoisting,
// parameters, private names, as V8 compiles them.
const N = 3000;
const list = (f, sep) => Array.from({ length: N }, (_, i) => f(i)).join(sep);

// a function of many vars, a var declared twice, a function and a var of one name
let src = 'var ' + list(i => 'v' + i + ' = ' + i, ', ') + '; var v7 = -7, v8; function v9() { return 9; }\n';
src += 'return [v0, v7, v8, typeof v9, v9, v' + (N - 1) + '];';
console.log('vars', JSON.stringify(new Function(src)()));

// many lets in a block, a name of the function's shadowed in it, closures over both
src = 'var x = "outer"; const fs = [];\n{\n' + list(i => 'let b' + i + ' = ' + i + ';', '\n') + '\nlet x = "inner"; fs.push(() => x + b0 + b' + (N - 1) + ');\n}\n';
src += 'return [x, fs[0]()];';
console.log('lets', JSON.stringify(new Function(src)()));

// a let declared twice, and a let after a var of its name: V8's early errors
for (const s of ['{ let a1 = 1; ' + list(i => 'let c' + i + ';', ' ') + ' let a1 = 2; }', 'var q0; ' + list(i => 'var q' + i + ';', ' ') + ' let q5;']) {
  try { new Function(s); console.log('no error'); } catch (e) { console.log(e.name + ': ' + e.message); }
}

// many parameters, sloppy duplicates, and arguments mapped to them
src = 'return arguments.length + ":" + p0 + ":" + p' + (N - 1) + ' + ":" + typeof p' + N + ';';
console.log('params', new Function(list(i => 'p' + i, ', '), src)(...Array.from({ length: N }, (_, i) => i)));
const dup = new Function('a', 'b', 'a', 'arguments[0] = "x"; arguments[2] = "z"; return [a, b, arguments.length].join();');
console.log('dup params', dup(1, 2, 3));

// many functions in a script, each its own slots; one of them its own name's closure
src = list(i => 'function f' + i + '(a) { var t = a + ' + i + '; return t; }', '\n') + '\n';
src += 'var g = function self(n) { return n ? self(n - 1) + 1 : 0; };\nreturn [f0(1), f' + (N - 1) + '(1), g(25)];';
console.log('functions', JSON.stringify(new Function(src)()));

// private names: many in a class, a nested class's of one name its own, #x in o, a direct eval naming them
src = 'class A {\n' + list(i => '#p' + i + ' = ' + i + ';', '\n') + '\n';
src += 'static sum(o) { return o.#p0 + o.#p' + (N - 1) + '; }\n';
src += 'static inner(o) { class B { #p0 = "b"; static get(x) { return x.#p0; } } return [B.get(new B()), #p1 in o, eval("o.#p2")]; }\n}\n';
src += 'const a = new A(); return [A.sum(a), A.inner(a)];';
console.log('privates', JSON.stringify(new Function(src)()));

// a direct eval among many names: its vars and the function's
src = list(i => 'var e' + i + ' = ' + i + ';', ' ') + ' eval("var e' + N + ' = -1; e0 = 100;"); return [e0, e' + N + ', e' + (N - 1) + '];';
console.log('eval', JSON.stringify(new Function(src)()));

// a module-like scope of many consts read by closures after it
src = list(i => 'const k' + i + ' = "k' + i + '";', '\n') + '\nreturn () => k0 + k' + (N >> 1) + ' + k' + (N - 1) + ';';
console.log('consts', new Function(src)()());
