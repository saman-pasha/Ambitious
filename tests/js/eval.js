// tests/js/eval.js -- eval, the Function constructor and with, each line against V8's
// (eval.expect): an eval's completion value; indirect eval in the global scope (its vars
// the global object's, its let and const its own); new Function's parameters and body,
// and the sources that would end the function early refused; direct eval in its
// caller's scopes (reading and writing them, its this and arguments, a sloppy eval's
// vars and functions declared in its caller's function and seen by the caller's names,
// a strict one's its own, the same code run again); with's object searched first (its
// prototypes, its unscopables, a call's this, assignment, typeof, closures inside it).
// The tests stand in functions: node runs a file as a module, whose top level is a
// function's scope, not the global one.

const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));
const ge = eval;
p('values', ge('1 + 2'), ge('"s"'), ge(''), ge('var q = 3'), ge('1; var r = 4'), ge('1; if (true) {}'), ge('1; if (true) { 2; }'));
p('loops', ge('5; while (false) {}'), ge('5; for (var i = 0; i < 3; i++) i * 2'), ge('5; do { 6; break; } while (true)'), ge('7; {}'), ge('8; lbl: { 9 }'));
p('object', ge('({a: 1, b: [2]})'), ge('{a: 1}'));
p('globals', ge('var gv = 10; gv + 1'), typeof gv, gv);
ge('function gfn() { return "gfn"; }'); p('function', gfn());
p('let local', ge('let lx = 5; lx * 2'), typeof lx);
p('this', ge('this') === globalThis);
p('non-string', ge(42), ge({ k: 1 }), ge(undefined));
try { ge('a b c'); } catch (e) { p('syntax', e.name, e instanceof SyntaxError); }
try { ge('throw new TypeError("thrown")'); } catch (e) { p('thrown', e.name, e.message); }
p('strict', ge('"use strict"; var sv = 1; sv'), typeof sv);
p('switch', ge('1; switch (2) { case 2: 3; }'), ge('1; switch (2) { case 3: 3; }'));
p('try', ge('1; try { 2 } catch (e) { 3 }'), ge('1; try { throw 0 } catch (e) { 3 }'));
const add = new Function('a', 'b', 'return a + b');
p('Function', add(2, 3), add.length, add.name, typeof add);
const noargs = Function('return 7'); p('no new', noargs());
const multi = new Function('a, b', 'c', 'return [a, b, c].join("-")'); p('multi', multi(1, 2, 3));
const defaults = new Function('a = 1', '...rest', 'return a + rest.length'); p('defaults', defaults(), defaults(5, 6, 7));
p('empty', new Function()(), new Function('').length);
p('global scope', (function () { const fnLocal = 1; return new Function('return typeof fnLocal')(); })(), new Function('return this')() === globalThis);
try { new Function('}); (function(){'); } catch (e) { p('injection', e.name); }
try { new Function('a)', '{ return 1'); } catch (e) { p('injection2', e.name); }
try { new Function('return ;;; }'); } catch (e) { p('bad body', e.name); }
const strictFn = new Function('"use strict"; return this'); p('strict fn', strictFn());
p('closure', new Function('x', 'return function (y) { return x + y; }')(10)(5));
p('gen', [...new Function('return function* () { yield 1; yield 2; }')()()]);

function reads() { const a = 1; let b = 2; var c = 3; return eval('a + b + c'); }
p('reads', reads());
function writes() { let x = 1; var y = 2; eval('x = 10; y = 20'); return [x, y]; }
p('writes', writes());
function declares() { eval('var nv = 5; function nf() { return "nf"; }'); return [typeof nv, nv, nf()]; }
p('declares', declares());
function shadowsGlobal() { eval('var Math = "local math"'); return Math; }
p('shadows', shadowsGlobal(), typeof Math.max);
function aliases(v) { eval('var v = v * 2'); return v; }
p('aliases param', aliases(21));
function args() { return eval('arguments.length + ":" + arguments[1]'); }
p('arguments', args('a', 'b', 'c'));
const o = { m() { return eval('this === o'); } };
p('this', o.m());
function nested() { let n = 1; return (() => { let m = 2; return eval('n + m'); })(); }
p('nested arrow', nested());
function closures() { let k = 7; const f = eval('(function () { return k; })'); k = 8; return f(); }
p('closure', closures());
function strictEval() { eval('"use strict"; var sv = 1'); return typeof sv; }
p('strict eval', strictEval());
function strictCaller() { 'use strict'; eval('var scv = 1'); return typeof scv; }
p('strict caller', strictCaller());
function lets() { eval('let lv = 1; const cv = 2'); return typeof lv + typeof cv; }
p('lets local', lets());
function twice() { eval('var t1 = 1'); eval('var t2 = t1 + 1'); return t1 + t2; }
p('twice', twice());
function inner() { var w = 'outer w'; function g() { eval('var w = "inner w"'); return w; } return [g(), w]; }
p('inner', inner());
function indirect() { var local = 1; const ge = eval; return [ge('typeof local'), (0, eval)('typeof local'), eval('typeof local')]; }
p('indirect', indirect());
function completion() { return [eval('1; 2'), eval('if (true) 3'), eval('var q = 4'), eval('{}'), eval('5; for (;;) { 6; break; }')]; }
p('completion', completion());
function deep() { let a = 'a'; { let b = 'b'; { let c = 'c'; return eval('a + b + c'); } } }
p('deep', deep());
function tdz() { try { eval('later'); } catch (e) { return e.constructor.name; } let later = 1; }
p('tdz', tdz());
function constAssign() { const k = 1; try { eval('k = 2'); } catch (e) { return e.constructor.name; } }
p('const', constAssign());
function evalInEval() { var z = 1; return eval('eval("z + 1")'); }
p('eval in eval', evalInEval());
function evalDeclThenUse() { eval('function hoisted() { return "h"; }'); return hoisted(); }
p('hoisted', evalDeclThenUse());
function loopEval() { const out = []; for (let i = 0; i < 3; i++) out.push(eval('i * 10')); return out; }
p('loop', loopEval());
function typeofDyn() { eval('var td = 1'); return [typeof td, typeof notThere]; }
p('typeof', typeofDyn());
function syntaxErr() { try { eval('1 +'); } catch (e) { return e.constructor.name; } }
p('syntax', syntaxErr());
// with
function withBasic() { const obj = { a: 1, b: 2 }; with (obj) { return a + b; } }
p('with', withBasic());
function withAssign() { const obj = { a: 1 }; let a = 'outer'; with (obj) { a = 2; } return [obj.a, a]; }
p('with assign', withAssign());
function withFallthrough() { const obj = {}; let z = 'z'; with (obj) { return z; } }
p('with fallthrough', withFallthrough());
function withCall() { const obj = { name: 'obj', who() { return this.name; } }; with (obj) { return who(); } }
p('with this', withCall());
function withVar() { const obj = { v: 1 }; with (obj) { var v = 2; } return [obj.v, typeof v]; }
p('with var', withVar());
function withNested() { const o1 = { x: 1, y: 1 }, o2 = { y: 2 }; with (o1) with (o2) return x + y; }
p('with nested', withNested());
function withClosure() { const obj = { q: 'from obj' }; let f; with (obj) { f = () => q; } obj.q = 'changed'; return f(); }
p('with closure', withClosure());
function withUnscopables() { const arr = [1, 2]; const values = 'outer values'; with (arr) { return [typeof push, values]; } }
p('unscopables', withUnscopables());
function withProto() { const obj = Object.create({ inherited: 'yes' }); with (obj) { return inherited; } }
p('with proto', withProto());
function withDelete() { const obj = { d: 1 }; with (obj) { delete obj.d; return typeof d; } }
p('with delete', withDelete());
function withTypeof() { with ({ tf: 1 }) { return typeof tf; } }
p('with typeof', withTypeof());
try { new Function('"use strict"; with ({}) {}'); } catch (e) { p('with strict', e.constructor.name); }
function withNull() { try { with (null) {} } catch (e) { return e.constructor.name; } }
p('with null', withNull());
function withPrimitive() { with ('abc') { return length; } }
p('with primitive', withPrimitive());
function withEval() { const obj = { we: 'with+eval' }; with (obj) { return eval('we'); } }
p('with eval', withEval());
function withUpdate() { const obj = { cnt: 1 }; with (obj) { cnt++; cnt += 10; } return obj.cnt; }
p('with update', withUpdate());
function withCompletion() { return eval('with ({wc: 3}) wc'); }
p('with completion', withCompletion());
// new Function with with (templates)
const tmpl = new Function('obj', 'var __p = ""; with (obj || {}) { __p += "Hello " + name + "!"; } return __p;');
p('template', tmpl({ name: 'World' }));

// the same code at the same site again and again: compiled once, run each time
function repeated() { let total = 0; for (let i = 0; i < 2000; i++) total += eval('i % 7'); return total; }
p('repeated', repeated());
function repeatedFn() { let t = 0; for (let i = 0; i < 500; i++) t += new Function('a', 'return a * 2')(i); return t; }
p('repeated Function', repeatedFn());
