// tests/js/asyncgen.js -- async generators and for await, each line against V8's
// (asyncgen.expect): their requests queued and answered in order, return and throw in
// each state, yield's await, yield* over async and sync iterables, for await over
// async iterators, sync iterables and generators, the iterators closed on an early
// exit, and the microtasks' order against other promise chains.

const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));

// the basics: values yielded and awaited, for await over them
async function* counter(n) {
    for (let i = 0; i < n; i++) {
        await null;
        yield i;
    }
    return 'end';
}
(async () => {
    for await (const v of counter(3)) p('counter', v);
    const g = counter(2);
    p('next', await g.next());
    p('next', await g.next());
    p('next', await g.next());
    p('next', await g.next());
})();

// the objects: their prototypes, tags and Symbol.asyncIterator
{
    async function* ag() { yield 1; }
    const g = ag();
    p('tag', Object.prototype.toString.call(g), typeof g.next, g[Symbol.asyncIterator]() === g);
    p('proto', Object.getPrototypeOf(g) === ag.prototype, typeof ag.prototype.next, Object.getPrototypeOf(Object.getPrototypeOf(ag.prototype)) !== Object.prototype);
    p('not a constructor', (() => { try { new ag(); return 'made'; } catch (e) { return e.constructor.name; } })());
    const r = g.next();
    p('a promise', r instanceof Promise);
}

// requests queued: next called three times at once, answered in order
{
    async function* slow() {
        yield await new Promise(r => setTimeout(() => r('a'), 3));
        yield 'b';
        yield Promise.resolve('c');
    }
    const g = slow();
    const all = [g.next(), g.next(), g.next(), g.next()];
    Promise.all(all).then(rs => p('queued', rs));
}

// return and throw: before the start, at a yield, after the end, queued behind next
{
    async function* gen() {
        try { yield 1; yield 2; } finally { p('finally ran'); }
    }
    const a = gen();
    a.return('early').then(r => p('return at start', r));
    a.next().then(r => p('next after return', r));
    const b = gen();
    b.next().then(r => p('b first', r));
    b.return('stop').then(r => p('return at yield', r));
    b.next().then(r => p('b after', r));
    const c = gen();
    c.throw(new Error('boom')).catch(e => p('throw at start', e.message));
    const d = gen();
    d.next().then(() => d.throw(new Error('inside'))).catch(e => p('throw at yield', e.message));
    const e = gen();
    e.next(); e.next(); e.next();
    e.return(Promise.resolve('awaited value')).then(r => p('return awaits', r));
}

// a value yielded is awaited first; a rejection there throws into the generator
{
    async function* y() {
        try { yield Promise.reject(new Error('rejected yield')); }
        catch (e) { p('caught in gen', e.message); yield 'recovered'; }
    }
    const g = y();
    g.next().then(r => p('y1', r));
    g.next().then(r => p('y2', r));
}

// an error thrown out of the body rejects the request, the generator done after it
{
    async function* bad() { yield 1; throw new TypeError('body threw'); }
    const g = bad();
    g.next().then(r => p('bad1', r));
    g.next().catch(e => p('bad2', e.name, e.message));
    g.next().then(r => p('bad3', r));
}

// try/finally: return runs the finally block, a yield in it suspends the return
{
    async function* f() {
        try { yield 'body'; }
        finally { yield 'in finally'; p('after finally yield'); }
    }
    const g = f();
    (async () => {
        p('f1', await g.next());
        p('f2', await g.return('ret'));
        p('f3', await g.next());
        p('f4', await g.next());
    })();
}

// yield*: over an async generator, over an array, with values sent through
{
    async function* inner() { const x = yield 'i1'; p('inner got', x); yield 'i2'; return 'inner done'; }
    async function* outer() { const r = yield* inner(); p('yield* value', r); yield* [10, Promise.resolve(20)]; yield 'last'; }
    (async () => {
        const g = outer();
        p('o', await g.next());
        p('o', await g.next('sent'));
        p('o', await g.next());
        p('o', await g.next());
        p('o', await g.next());
        p('o', await g.next());
        p('o', await g.next());
    })();
}

// yield*: return and throw forwarded to the inner iterator
{
    async function* inner() { try { yield 1; yield 2; } finally { p('inner finally'); } }
    async function* outer() { try { yield* inner(); } catch (e) { p('outer caught', e); yield 'after catch'; } }
    (async () => {
        const g = outer();
        await g.next();
        p('fwd return', await g.return('r'));
        const h = outer();
        await h.next();
        p('fwd throw', await h.throw('t'));
        p('fwd next', await h.next());
    })();
}

// for await over sync iterables: values and promises, a sync generator, a rejection
(async () => {
    const seen = [];
    for await (const v of [1, Promise.resolve(2), 3]) seen.push(v);
    p('array', seen);
    function* sg() { yield 'x'; yield Promise.resolve('y'); }
    for await (const v of sg()) p('sync gen', v);
    try { for await (const v of [Promise.reject(new Error('rej'))]) p('never', v); }
    catch (e) { p('for await caught', e.message); }
    let s = '';
    for await (const ch of 'hi') s += ch;
    p('string', s);
})();

// for await over a hand-written async iterator; break and throw close it
{
    function source(name) {
        let i = 0;
        return {
            [Symbol.asyncIterator]() { return this; },
            next() { i++; return Promise.resolve({ value: name + i, done: i > 3 }); },
            return() { p(name, 'return called'); return Promise.resolve({ done: true }); }
        };
    }
    (async () => {
        for await (const v of source('A')) { p('A', v); if (v === 'A2') break; }
        try { for await (const v of source('B')) { p('B', v); throw new Error('stop'); } }
        catch (e) { p('B caught', e.message); }
        outer: for (const k of [1, 2]) {
            for await (const v of source('C' + k)) { if (v.endsWith('2')) continue outer; p('C', v); }
        }
        const r = await (async () => { for await (const v of source('D')) return v; })();
        p('D returned', r);
        for await (const v of source('E')) p('E', v);
    })();
}

// methods: in an object and a class, a computed name, Symbol.asyncIterator
{
    const o = { async *items() { yield 'o1'; yield 'o2'; } };
    class Q {
        constructor(xs) { this.xs = xs; }
        async *[Symbol.asyncIterator]() { for (const x of this.xs) yield x * 2; }
        static async *range(n) { for (let i = 0; i < n; i++) yield i; }
    }
    (async () => {
        const got = [];
        for await (const v of o.items()) got.push(v);
        for await (const v of new Q([1, 2, 3])) got.push(v);
        for await (const v of Q.range(2)) got.push(v);
        p('methods', got);
        const ex = async function* named() { yield named.name; };
        for await (const v of ex()) p('expression', v);
    })();
}

// the microtasks' order: a generator's steps against another promise chain
{
    const order = [];
    async function* ticker() { order.push('g0'); yield 1; order.push('g1'); yield 2; order.push('g2'); }
    (async () => { for await (const v of ticker()) order.push('v' + v); order.push('loop end'); })();
    Promise.resolve().then(() => order.push('t1')).then(() => order.push('t2')).then(() => order.push('t3'))
        .then(() => order.push('t4')).then(() => order.push('t5')).then(() => order.push('t6'));
    setTimeout(() => p('order', order.join(' ')), 20);
}
{
    const order = [];
    (async () => { for await (const v of [1, 2]) order.push('a' + v); order.push('a end'); })();
    Promise.resolve().then(() => order.push('u1')).then(() => order.push('u2')).then(() => order.push('u3'))
        .then(() => order.push('u4')).then(() => order.push('u5'));
    setTimeout(() => p('sync order', order.join(' ')), 20);
}

// next sent values, the first one lost
{
    async function* echo() { let x; while (true) { x = yield x; if (x === 'stop') return 'stopped'; } }
    (async () => {
        const g = echo();
        p('echo', await g.next('lost'), await g.next('one'), await g.next('two'), await g.next('stop'), await g.next('after'));
    })();
}
