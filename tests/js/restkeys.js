// Object rest excludes the pattern's computed keys, each converted once (toString called once), numbers, symbols, nesting
const k = 'a', s = Symbol('s');
const { [k]: v, ...rest } = { a: 1, b: 2, c: 3 }; console.log(v, JSON.stringify(rest));
const { [k + 'x']: w = 'dflt', ...r2 } = { ax: 5, b: 6 }; console.log(w, JSON.stringify(r2));
const { [s]: sv, ...r3 } = { [s]: 7, d: 8 }; console.log(sv, JSON.stringify(r3), Object.getOwnPropertySymbols(r3).length);
const log = []; const key = { toString() { log.push('toString'); return 'm'; } };
const { [key]: mv, ...r4 } = { m: 9, n: 10 }; console.log(mv, JSON.stringify(r4), log.join());
let r5; ({ [k]: v2, ...r5 } = { a: 11, z: 12 }); var v2; console.log(v2, JSON.stringify(r5));
function f({ [k]: x, ...more }) { return x + ':' + Object.keys(more).join(); } console.log(f({ a: 1, q: 2, r: 3 }));
const { [1]: one, ...r6 } = { 1: 'a', 2: 'b' }; console.log(one, JSON.stringify(r6));
const { [0]: first, ...r7 } = ['x', 'y']; console.log(first, JSON.stringify(r7));
const { [k]: { [k]: deep, ...ir }, ...or } = { a: { a: 1, b: 2 }, c: 3 }; console.log(deep, JSON.stringify(ir), JSON.stringify(or));
const { [k]: p1, b: p2, [s]: p3, ...r8 } = { a: 1, b: 2, [s]: 3, e: 4, [Symbol.iterator]: 5 }; console.log(p1, p2, p3, JSON.stringify(r8), Object.getOwnPropertySymbols(r8).length);
