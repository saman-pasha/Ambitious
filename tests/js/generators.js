// tests/js/generators.js -- generators and symbols, each line against V8's (generators.expect):
// next, throw and return; finally blocks and the iterators a for-of leaves early; yield*;
// destructuring through the iteration protocol; symbols as keys and the well-known ones.
function p() { var s = ''; for (var i = 0; i < arguments.length; i++) s += (i ? ' ' : '') + String(arguments[i]); console.log(s); }

// the protocol: values in and out, a for-of, spread, a generator's finally on break
{
function* g() { const x = yield 1; console.log('got', x); yield x * 2; return 'end'; }
const it = g();
console.log(JSON.stringify(it.next()));
console.log(JSON.stringify(it.next(5)));
console.log(JSON.stringify(it.next()));
console.log(JSON.stringify(it.next()));
for (const v of g()) console.log('v', v);
console.log([...(function*() { yield 1; yield 2; yield* [3, 4]; })()].join(','));
function* inner() { try { yield 'a'; yield 'b'; } finally { console.log('inner cleanup'); } }
for (const v of inner()) { console.log('loop', v); break; }
function* fib() { let [a, b] = [0, 1]; for (;;) { yield a; [a, b] = [b, a + b]; } }
const f = []; for (const n of fib()) { if (n > 50) break; f.push(n); } console.log(f.join(' '));
const sym = Symbol('s');
console.log(typeof sym, sym.toString(), sym.description, String(sym));
const o = { [sym]: 1, a: 2 };
console.log(Object.keys(o).length, o[sym], Object.getOwnPropertySymbols(o).length);
class C { *[Symbol.iterator]() { yield 10; yield 20; } }
console.log([...new C()].join('+'));
const ri = g(); ri.next(); console.log(JSON.stringify(ri.return(42)), JSON.stringify(ri.next()));
const ti = g(); ti.next(); try { ti.throw(new Error('boom')); } catch (e) { console.log('thrown', e.message); }
}

// throw and return into a suspended generator; yield*'s delegation; early exits
{
function* t1() { try { yield 1; yield 2; } catch (e) { p('caught in gen', e); yield 'after catch'; } finally { p('t1 finally'); } yield 'end'; }
let it = t1(); p(JSON.stringify(it.next())); p(JSON.stringify(it.throw('E'))); p(JSON.stringify(it.next())); p(JSON.stringify(it.next()));
it = t1(); it.next(); p(JSON.stringify(it.return('R')));
function* t2() { try { yield 1; } finally { yield 'from finally'; p('after finally yield'); } }
it = t2(); it.next(); p(JSON.stringify(it.return('X'))); p(JSON.stringify(it.next()));
function* in2() { try { yield 'i1'; yield 'i2'; } finally { p('in2 finally'); } return 'in2 ret'; }
function* out2() { const r = yield* in2(); p('delegate returned', r); yield 'o'; }
p([...out2()].join(','));
it = out2(); it.next(); p(JSON.stringify(it.return('stop')));
it = out2(); it.next(); try { it.throw(new Error('thr')); } catch (e) { p('outer got', e.message); }
function* withLoop() { for (const x of in2()) { yield x; } }
it = withLoop(); it.next(); p(JSON.stringify(it.return('loopret')));
try { for (const x of in2()) { throw new Error('body'); } } catch (e) { p('body error', e.message); }
const [a, b] = in2(); p('destructured', a, b);
function* args() { yield arguments.length; yield this && this.k; }
p([...args.call({ k: 'K' }, 1, 2, 3)].join(','));
const gobj = { *gen() { yield 'm1'; } };
p([...gobj.gen()].join(''));
p(Object.prototype.toString.call(t1()), typeof t1().next, t1() instanceof t1);
function* inf() { let i = 0; while (true) yield i++; }
const lim = []; for (const v of inf()) { if (v >= 3) break; lim.push(v); } p(lim.join(''));
outer: for (const x of in2()) { for (const y of in2()) { continue outer; } }
p('labels done');
function* deleg2() { yield* 'ab'; yield* new Set([1, 2]); yield* new Map([[1, 2]]); }
p(JSON.stringify([...deleg2()]));
function* ret0() { return 5; }
p(JSON.stringify(ret0().next()), JSON.stringify([...ret0()]));
const g3 = (function*() { const r = []; r.push(yield); r.push(yield); return r; })();
g3.next(); g3.next('x'); p(JSON.stringify(g3.next('y')));
try { new t1(); } catch (e) { p('new gen', e.constructor.name); }
function* recur(n) { if (n > 0) { yield n; yield* recur(n - 1); } }
p([...recur(5)].join(''));
}

// symbols: identity, the registry, keys, the well-known ones the engine honors
{
const a = Symbol('a'), b = Symbol.for('shared');
p(a === Symbol('a'), b === Symbol.for('shared'), Symbol.keyFor(b), Symbol.keyFor(a));
p(typeof Symbol.iterator, Symbol.iterator.toString(), Symbol.asyncIterator.description);
const o = { [a]: 1, x: 2, [b]: 3 };
p(JSON.stringify(o), Object.keys(o).join(), Object.getOwnPropertySymbols(o).map(s => s.description).join());
p(a in o, o.hasOwnProperty(a), delete o[a], a in o);
const tp = { [Symbol.toPrimitive](hint) { return hint === 'number' ? 42 : 'str:' + hint; } };
p(+tp, `${tp}`, tp + '');
class T { get [Symbol.toStringTag]() { return 'Tagged'; } }
p(Object.prototype.toString.call(new T()));
class Even { static [Symbol.hasInstance](n) { return n % 2 === 0; } }
p(2 instanceof Even, 3 instanceof Even);
try { a + ''; } catch (e) { p(e.constructor.name); }
try { `${a}`; } catch (e) { p(e.constructor.name); }
p(Object(a) instanceof Symbol, typeof Object(a), Object(a).description);
const m = new Map([[a, 'sym val']]); p(m.get(a));
const it = [1, 2][Symbol.iterator](); p(JSON.stringify(it.next()), typeof it[Symbol.iterator]);
p(String(Symbol()), Symbol().description);
const sp = { *[Symbol.iterator]() { yield* [7, 8]; } }; p(Math.max(...sp), Array.from(sp).join());
p(Symbol.prototype.toString.call(a));
const ob2 = {}; ob2[Symbol.for('k')] = 'v'; p(ob2[Symbol.for('k')]);
p(Object.getOwnPropertyNames({ [a]: 1, z: 1 }).join());
p(JSON.stringify({ k: a }), JSON.stringify([a]));
}
