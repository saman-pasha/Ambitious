// Object.prototype.toString's tag for each kind of value (ECMA-262 20.1.3.6: the builtin tag, then Symbol.toStringTag),
// and the tags' own properties, as V8 has them
const ts = v => Object.prototype.toString.call(v);
const vals = [new Boolean(false), new Number(1), new String('a'), Object(1n), Object(Symbol()), new Date(0), /x/, [], function () {},
  new Error('e'), null, undefined, 1, 'x', true, new Map(), new Set(), Promise.resolve(), new ArrayBuffer(1), new Uint8Array(1),
  JSON, Math, (function () { return arguments; })(), new Proxy([], {}), new Proxy({}, {}), Object.create(null), globalThis.Intl || {}];
console.log(vals.map(ts).join(' '));
for (const o of [Math, JSON]) {
  const d = Object.getOwnPropertyDescriptor(o, Symbol.toStringTag);
  console.log(JSON.stringify(d), Reflect.ownKeys(o).filter(k => typeof k === 'symbol').map(String).join());
}
const custom = { [Symbol.toStringTag]: 'Custom' };
console.log(ts(custom), ts(Object.assign(new Boolean(true), { [Symbol.toStringTag]: 'Tagged' })), String(new Number(5)), `${new String('s')}`);
