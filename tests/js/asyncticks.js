// tests/js/asyncticks.js -- the microtask on which each async generator request is
// answered, counted by a chain of promise jobs, against V8's (asyncticks.expect): next,
// return and throw through yield* to async and sync inner iterators, a return with no
// inner return to take it, an inner's return value.
let t = 0;
const log = (m) => console.log('tick', t, m);
function counter(n) { t = 0; const tick = () => { t++; if (t < n) Promise.resolve().then(tick); }; Promise.resolve().then(tick); }
const cases = [];
const add = (name, fn) => cases.push([name, fn]);
add('return via yield*', async () => {
  async function* inner() { try { yield 1; } finally { log('inner finally'); } }
  async function* outer() { yield* inner(); }
  const g = outer(); await g.next();
  counter(30); const r = g.return('r'); r.then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('throw via yield*', async () => {
  async function* inner() { try { yield 1; } finally { log('inner finally'); } }
  async function* outer() { try { yield* inner(); } catch (e) { log('outer caught'); yield 'c'; } }
  const g = outer(); await g.next();
  counter(30); g.throw('x').then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('next via yield*', async () => {
  async function* inner() { yield 1; yield 2; }
  async function* outer() { yield* inner(); }
  const g = outer(); await g.next();
  counter(30); g.next().then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('return plain', async () => {
  async function* a() { try { yield 1; } finally { log('a finally'); } }
  const g = a(); await g.next();
  counter(30); g.return('r').then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('return via yield* no return method', async () => {
  const it = { [Symbol.asyncIterator]() { return this; }, next() { return Promise.resolve({ value: 1, done: false }); } };
  async function* outer() { yield* it; }
  const g = outer(); await g.next();
  counter(30); g.return('r').then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('return via yield* sync inner', async () => {
  function* inner() { try { yield 1; } finally { log('inner finally'); } }
  async function* outer() { yield* inner(); }
  const g = outer(); await g.next();
  counter(30); g.return('r').then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('next via yield* sync inner', async () => {
  function* inner() { yield 1; yield 2; }
  async function* outer() { yield* inner(); }
  const g = outer(); await g.next();
  counter(30); g.next().then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
add('inner return value done', async () => {
  async function* inner() { yield 1; return 'iv'; }
  async function* outer() { const v = yield* inner(); log('got ' + v); }
  const g = outer(); await g.next();
  counter(30); g.next().then(v => log('resolved ' + JSON.stringify(v)));
  await new Promise(r => setTimeout(r, 5));
});
(async () => { for (const [n, f] of cases) { console.log('==', n); await f(); } })();
