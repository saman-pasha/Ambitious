const s = Symbol('s'), h = Symbol('hidden');
const src = { a: 1, [s]: 2 }; Object.defineProperty(src, h, { value: 3, enumerable: false });
const sp = { ...src }; console.log(Object.getOwnPropertySymbols(sp).map(String).join(), sp[s], sp[h]);
const as = Object.assign({}, src); console.log(Object.getOwnPropertySymbols(as).map(String).join(), as[s], as[h]);
const { a, ...rest } = src; console.log(Object.getOwnPropertySymbols(rest).map(String).join(), rest[s]);
const order = []; const p = new Proxy({ x: 1, [s]: 2 }, { ownKeys(t) { order.push('ownKeys'); return Reflect.ownKeys(t); }, getOwnPropertyDescriptor(t, k) { order.push('gopd ' + String(k)); return Reflect.getOwnPropertyDescriptor(t, k); }, get(t, k) { order.push('get ' + String(k)); return t[k]; } });
Object.assign({}, p); console.log(order.join(', '));
