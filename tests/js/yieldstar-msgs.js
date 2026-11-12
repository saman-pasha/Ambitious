const run = (s, g) => g().next().then(r => console.log(s, JSON.stringify(r)), e => console.log(s, e.constructor.name + ': ' + e.message));
run('num', async function* () { yield* 5; });
run('obj', async function* () { yield* {}; });
run('null', async function* () { yield* null; });
run('str', async function* () { yield* 'ab'; });
run('sync iter', async function* () { yield* [1, 2]; });
run('bad iter', async function* () { yield* { [Symbol.asyncIterator]: 5 }; });
run('bad sync', async function* () { yield* { [Symbol.iterator]: 5 }; });
run('iter not obj', async function* () { yield* { [Symbol.asyncIterator]() { return 5; } }; });
function* sg() { yield* 5; }
try { sg().next(); } catch (e) { console.log('sync num', e.message); }
