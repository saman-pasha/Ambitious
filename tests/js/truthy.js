// ToBoolean wherever a condition is tested (if, ?:, while, do-while, for, &&, ||, !, ??=, ||=, &&=, Boolean): every
// kind of value, symbols and BigInts among them, as V8 has it
const values = [true, false, 0, -0, 1, -1, 0.5, NaN, Infinity, -Infinity, '', 'a', '0', ' ', null, undefined,
  0n, 1n, -1n, 2n ** 70n, Symbol(), Symbol('s'), Symbol.iterator, {}, [], [0], () => 0, new Boolean(false), Object(0n), Object(Symbol())];
const label = v => typeof v === 'symbol' ? 'symbol ' + String(v) : typeof v === 'bigint' ? v + 'n' : typeof v === 'function' ? 'function' :
  typeof v === 'object' && v !== null ? 'object ' + Object.prototype.toString.call(v) : typeof v === 'string' ? JSON.stringify(v) : String(v);
for (const v of values) {
  const r = [];
  if (v) r.push('if'); else r.push('else');
  r.push(v ? 't' : 'f');
  let n = 0; while (v && n < 2) n++; r.push('while' + n);
  let m = 0; do { m++; } while (v && m < 3); r.push('do' + m);
  let k = 0; for (; v && k < 1; k++); r.push('for' + k);
  r.push(String(!v), String(!!v), String(Boolean(v)));
  r.push(typeof (v && 'and'), typeof (v || 'or'));
  let a = v; a ||= 'set'; let b = v; b &&= 'set'; let c = v; c ??= 'set';
  r.push(a === 'set', b === 'set', c === 'set');
  r.push([v].filter(Boolean).length, [v].some(x => x), [v].every(x => x));
  console.log(label(v).padEnd(28), r.join(' '));
}
// a BigInt counted down by its truthiness, a symbol key tested
let big = 5n, steps = 0; while (big) { big -= 1n; steps++; } console.log('bigint loop', steps);
const o = { [Symbol('k')]: 1, a: 2 }; console.log('keys tested', Reflect.ownKeys(o).filter(k => k ? true : false).length);
