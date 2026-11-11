// tests/js/async.js -- promises and async functions, each line against V8's (async.expect):
// the order of microtasks against timers, then and its handlers, the combinators, async
// functions, arrows and methods, await in try and loops, rejections handled.

// the order: sync code, then microtasks, then timers; the combinators
{
const log = [];
const p = (...a) => console.log(a.join(' '));
Promise.resolve(1).then(v => p('then1', v));
queueMicrotask(() => p('micro'));
setTimeout(() => p('timeout'), 0);
p('sync');
async function f(x) { p('f start', x); const y = await x; p('f after', y); return y * 2; }
f(3).then(v => p('f result', v));
async function g() { try { await Promise.reject(new Error('bad')); } catch (e) { p('caught', e.message); } return 'g done'; }
g().then(p);
const order = [];
Promise.resolve().then(() => order.push(1)).then(() => order.push(2));
Promise.resolve().then(() => order.push(3)).then(() => order.push(4));
setTimeout(() => p('order', order.join(',')), 1);
Promise.all([1, Promise.resolve(2), new Promise(r => setTimeout(() => r(3), 5))]).then(a => p('all', a.join(',')));
Promise.allSettled([Promise.reject(1), 2]).then(a => p('settled', JSON.stringify(a)));
Promise.race([new Promise(r => setTimeout(() => r('slow'), 10)), new Promise(r => setTimeout(() => r('fast'), 2))]).then(v => p('race', v));
Promise.any([Promise.reject(1), Promise.resolve('any ok')]).then(v => p('any', v));
Promise.any([Promise.reject(1)]).catch(e => p('any err', e.constructor === AggregateError, e.errors.join(',')));
new Promise((_, rej) => rej('r')).catch(e => p('catch', e)).finally(() => p('finally'));
const arrow = async () => { await null; return 'arrow'; };
arrow().then(p);
const obj = { async m() { return 'method'; } };
obj.m().then(p);
class K { async run() { const a = await 1; const b = await Promise.resolve(2); return a + b; } }
new K().run().then(v => p('class', v));
async function thrower() { throw new TypeError('t'); }
thrower().catch(e => p('rejected', e.name));
(async () => { for (const x of [1, 2]) { await x; p('loop', x); } })();
p('end sync');
}

// thenables, handlers that are not functions, finally, await's places
{
const q = (...a) => console.log(a.map(String).join(' '));
// a thenable followed, its then called in a job of its own
const thenable = { then(res) { q('thenable then called'); res('from thenable'); } };
Promise.resolve(thenable).then(v => q('resolved', v));
(async () => q('awaited thenable', await thenable))();
// then's handlers that are not functions pass the value or the reason on
Promise.resolve(7).then(null).then(undefined, null).then(v => q('passed through', v));
Promise.reject(8).then(v => q('never', v)).catch(e => q('reason passed', e));
// a handler's value, a promise it returns, an error it throws
Promise.resolve(1).then(v => Promise.resolve(v + 1)).then(v => { throw v + 1; }).catch(e => q('chain', e));
// finally passes the value, and its own throw replaces it
Promise.resolve('kept').finally(() => 'ignored').then(v => q('finally kept', v));
Promise.resolve('x').finally(() => { throw 'replaced'; }).catch(e => q('finally threw', e));
// await in try / finally, a loop of awaits, return await
async function tf() { try { await Promise.reject('in try'); } catch (e) { q('caught', e); return 'from catch'; } finally { q('finally runs'); } }
tf().then(v => q('tf', v));
async function sum(n) { let s = 0; for (let i = 1; i <= n; i++) s += await i; return s; }
sum(10).then(v => q('sum', v));
async function ra() { return await new Promise(r => setTimeout(() => r('return await'), 3)); }
ra().then(q);
// this and arguments in async functions and arrows
const holder = { name: 'holder', async who() { const inner = async () => this.name; return (await inner()) + ' ' + arguments.length; } };
holder.who(1, 2).then(v => q('this', v));
// an async function's parameters' error rejects its promise
async function badParam(a = (() => { throw new Error('param'); })()) { return a; }
badParam().catch(e => q('param rejected', e.message));
// the order of interleaved async functions
const trace = [];
async function A() { trace.push('A1'); await null; trace.push('A2'); await null; trace.push('A3'); }
async function B() { trace.push('B1'); await null; trace.push('B2'); }
A(); B(); trace.push('sync');
setTimeout(() => q('trace', trace.join(' ')), 1);   // 1 ms: V8's shell takes 0 as 1, a browser does not
// a promise's executor that throws, and one that resolves twice
new Promise(() => { throw new Error('executor'); }).catch(e => q('executor', e.message));
new Promise((res, rej) => { res('first'); res('second'); rej('third'); }).then(v => q('settled once', v));
// a resolved promise's then after it settled; a chain of timers and promises
const early = Promise.resolve('early');
setTimeout(() => early.then(v => q('late then', v)), 2);
q('typeof', typeof Promise, typeof Promise.prototype.then, Object.prototype.toString.call(early));
// generator-like iteration of promises with await
(async () => { const results = []; for (const pr of [Promise.resolve('p1'), 'p2', Promise.resolve('p3')]) results.push(await pr); q('in order', results.join(',')); })();
}
