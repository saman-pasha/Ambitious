// tests/js/asyncfromsync.js -- a sync iterator seen as an async one (for await over it,
// an async yield* to it), ES2025's way, against Chromium's V8 (asyncfromsync.expect,
// written by node tools/js-chromium.js tests/js/asyncfromsync.js; node 22's V8 predates
// these rules): a value that rejects closes the sync iterator; a throw sent to one
// without throw closes it, then rejects with a TypeError; values and promises awaited.

const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));

(async () => {
    function* g() { try { yield 'first'; yield Promise.reject('rejected value'); yield 'never'; } finally { p('g closed'); } }
    try { for await (const x of g()) p('g gave', x); }
    catch (e) { p('caught', e); }

    function* h() { try { yield 1; yield 2; } finally { p('h closed'); } }
    async function* viaH() { yield* h(); }
    const a = viaH();
    p('next', await a.next());
    p('throw', await a.throw('x').catch(e => 'rejected ' + e));

    const noThrow = { [Symbol.iterator]() { return { next() { return { value: 'v', done: false }; }, return() { p('return called'); return {}; } }; } };
    async function* viaNoThrow() { yield* noThrow; }
    const b = viaNoThrow();
    p('next', await b.next());
    p('throw', await b.throw('y').catch(e => e.constructor.name + ': ' + e.message));

    const plain = { [Symbol.iterator]() { let i = 0; return { next() { i++; return { value: Promise.resolve(i * 10), done: i > 2 }; } }; } };
    for await (const x of plain) p('plain', x);

    function* r() { try { yield 'a'; yield 'b'; } finally { p('r closed'); } }
    for await (const x of r()) { p('r gave', x); break; }
    async function* viaR() { const v = yield* r(); p('never', v); }
    const c = viaR();
    p('next', await c.next());
    p('return', await c.return(Promise.resolve('back')));
})();
