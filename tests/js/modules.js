// tests/js/modules.js -- ECMAScript modules, each line against V8's (modules.expect): a
// classic script's import() of a module graph (tests/js/modules/): named, default and
// namespace imports and their live bindings, re-exports (renamed, star, star as a
// namespace, an ambiguous star export left out), the order modules evaluate in, a cycle
// (hoisted functions seen across it, a const not yet initialized), top-level await and
// a module waiting on it, import.meta, import() from a module, the same module imported
// twice, and the failures: a module that throws, an export that is missing, a module
// that does not compile, a module that is not there.

const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));
(async () => {
    p('before import');
    const main = await import('./modules/main.mjs');
    p('main exports', Object.keys(main), main.fromMain);
    const again = await import('./modules/main.mjs');
    p('same module', again === main);
    const { order } = await import('./modules/log.mjs');
    const t = await import('./modules/afterTla.mjs');
    p('tla', t.done, order.slice(-3));
    try { await import('./modules/bad.mjs'); } catch (e) { p('throws', e.constructor.name, e.message); }
    try { await import('./modules/bad.mjs'); } catch (e) { p('throws again', e.constructor.name); }
    try { await import('./modules/missing.mjs'); } catch (e) { p('missing export', e.constructor.name, e.message); }
    try { await import('./modules/syntax.mjs'); } catch (e) { p('syntax', e.constructor.name); }
    try { await import('./modules/nothere.mjs'); } catch (e) { p('not there', e instanceof Error); }
    const dyn = await import('./modules/lib.mjs');
    p('dynamic', dyn.value, typeof dyn.default, dyn.counter);
    p('done');
})();
p('after import call');
