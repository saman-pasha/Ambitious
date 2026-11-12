// tests/js/typedbuf.js -- resizable and transferable ArrayBuffers, each line against V8's
// (typedbuf.expect): new ArrayBuffer(n, { maxByteLength }), resize, transfer and
// transferToFixedLength, detached; typed arrays and DataViews over them, tracking a
// resizable buffer's length or fixed, out of bounds when it shrinks, and every method's
// TypeError over a detached buffer, in V8's words.

const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const tr = f => { try { return JSON.stringify(f()); } catch (e) { return e.name + ': ' + e.message; } };

// resizable buffers and the views that track them
{
const rab = new ArrayBuffer(4, { maxByteLength: 16 });
p(rab.byteLength, rab.maxByteLength, rab.resizable, rab.detached, new ArrayBuffer(4).maxByteLength, new ArrayBuffer(4).resizable);
p(tr(() => new ArrayBuffer(8, { maxByteLength: 4 })), tr(() => new ArrayBuffer(8, { maxByteLength: -1 })), tr(() => new ArrayBuffer(8, {})), tr(() => new ArrayBuffer(8, { maxByteLength: undefined }).resizable));
p(tr(() => new ArrayBuffer(8, 5).resizable), tr(() => new ArrayBuffer(8, { maxByteLength: 2 ** 33 }).maxByteLength), tr(() => new ArrayBuffer(8, { maxByteLength: '12' }).maxByteLength));
p(tr(() => new ArrayBuffer(-1)), tr(() => new ArrayBuffer(2 ** 53)), tr(() => new ArrayBuffer(1.5).byteLength), tr(() => new ArrayBuffer('3').byteLength), tr(() => ArrayBuffer(1)));
const u8 = new Uint8Array(rab), u8f = new Uint8Array(rab, 0, 2), u8o = new Uint8Array(rab, 2), u16 = new Uint16Array(rab);
u8.set([1, 2, 3, 4]);
p(u8.length, u8f.length, u8o.length, u16.length, [...u8], [...u16]);
rab.resize(10);
p(rab.byteLength, u8.length, u8f.length, u8o.length, u16.length, u16.byteLength, [...u8], u8o.byteOffset);
rab.resize(1);
p(rab.byteLength, u8.length, u8f.length, u8o.length, u8o.byteOffset, u8o.byteLength, u16.length, u8f[0], u8f[1], u8o[0]);
p(tr(() => u8o.fill(1)), tr(() => u8f.join()), tr(() => u8.join()), tr(() => u8o.subarray(0)), tr(() => u8o.values()));
rab.resize(16);
p(u8.length, u8f.length, u8o.length, u8o.byteOffset, [...u8f], u8[1], u8[15]);
p(tr(() => rab.resize(17)), tr(() => rab.resize(-1)), tr(() => new ArrayBuffer(4).resize(2)), tr(() => rab.resize()), rab.byteLength);
rab.resize(3);
p(rab.byteLength, u8.length, u16.length, u16.byteLength, tr(() => u16[1]), u8f.length);
const g = new ArrayBuffer(2, { maxByteLength: 1000 }); const gv = new Uint8Array(g); gv[0] = 1; gv[1] = 2; g.resize(1); g.resize(3); p([...gv]);
for (let i = 0; i < 1000; i++) g.resize(i % 7 === 0 ? 1 : g.byteLength + 1);
p(g.byteLength, gv.length, gv[0], gv[1]);
const sl = new ArrayBuffer(4, { maxByteLength: 8 }); new Uint8Array(sl).set([1, 2, 3, 4]); const s2 = sl.slice(1, 3);
p(s2.byteLength, s2.resizable, s2.maxByteLength, [...new Uint8Array(s2)], sl.slice(-2).byteLength);
const big = new BigInt64Array(new ArrayBuffer(8, { maxByteLength: 16 })); p(big.length, tr(() => (big.buffer.resize(16), big.length)));
const big2 = new BigInt64Array(new ArrayBuffer(0, { maxByteLength: 64 })); big2.buffer.resize(16); big2[1] = 5n; p(big2.length, String(big2[1]));
const sub = new Uint8Array(new ArrayBuffer(4, { maxByteLength: 8 })).subarray(1); sub.buffer.resize(8); p(sub.length, sub.byteOffset);
const sub2 = new Uint8Array(new ArrayBuffer(4, { maxByteLength: 8 })).subarray(1, 2); sub2.buffer.resize(8); p(sub2.length);
const f32 = new Float32Array(new ArrayBuffer(6, { maxByteLength: 16 })); p(f32.length, f32.byteLength, tr(() => (f32.buffer.resize(11), f32.length)), f32.byteLength);
const many = new ArrayBuffer(0, { maxByteLength: 1 << 20 }), mv = new Int32Array(many);
for (let i = 0; i < 2000; i++) { many.resize((i + 1) * 4); mv[i] = i; }
let s = 0; for (let i = 0; i < mv.length; i++) s += mv[i]; p(mv.length, s);
}

// DataViews over resizable buffers
{
const rab = new ArrayBuffer(3, { maxByteLength: 16 });
const dv = new DataView(rab), dvf = new DataView(rab, 1, 2), dvo = new DataView(rab, 2);
p(dv.byteLength, dvf.byteLength, dvo.byteLength, dvo.byteOffset);
rab.resize(1);
p(dv.byteLength, tr(() => dvf.byteLength), tr(() => dvo.byteLength), tr(() => dvo.byteOffset), tr(() => dvf.getInt8(0)), tr(() => dv.getInt8(0)), tr(() => dv.getInt8(1)));
rab.resize(8);
p(dv.byteLength, dvf.byteLength, dvo.byteLength, dvf.getInt8(1));
dv.setFloat64(0, 1.5); p(dv.getFloat64(0), dvo.getInt16(0), tr(() => dvo.getFloat64(0)));
const dvb = new ArrayBuffer(4); const d2 = new DataView(dvb, 1);
p(tr(() => d2.getInt16(2)), tr(() => d2.getInt16(-1)), tr(() => d2.setInt16(3, 1)), tr(() => new DataView(dvb, 5)), tr(() => new DataView(dvb, 1, 4)), tr(() => new DataView(dvb, -1)), tr(() => new DataView({})));
const order = []; const dv3 = new DataView(new ArrayBuffer(2));
p(tr(() => dv3.setInt8({ valueOf() { order.push('index'); return 5; } }, { valueOf() { order.push('value'); return 1; } })), order);
}

// transfer, and detached buffers
{
const a = new ArrayBuffer(4), av = new Uint8Array(a); av.set([9, 8, 7, 6]);
const b = a.transfer();
p(a.byteLength, a.detached, b.byteLength, b.detached, [...new Uint8Array(b)], av.length, av.byteLength, av.byteOffset, av[0], tr(() => av.fill(0)));
p(tr(() => a.slice(0)), tr(() => a.transfer()), tr(() => a.resize(1)), tr(() => new Uint8Array(a)), tr(() => new DataView(a)), a.maxByteLength, a.resizable);
const c = b.transfer(6), cc = [...new Uint8Array(c)], d = c.transfer(2);
p(cc, [...new Uint8Array(d)], c.detached, d.resizable);
const r2 = new ArrayBuffer(2, { maxByteLength: 8 }), r3 = r2.transfer(), r4 = r3.transferToFixedLength(), r5 = r4.transfer(3);
p(r3.resizable, r3.maxByteLength, r4.resizable, r4.maxByteLength, r5.resizable, r5.byteLength, tr(() => r3.transfer(9)));
const r6 = new ArrayBuffer(2, { maxByteLength: 8 }).transfer(7); p(r6.byteLength, r6.maxByteLength, r6.resizable);
const r = new ArrayBuffer(2, { maxByteLength: 8 });
p(tr(() => r.transfer(9)), tr(() => r.transferToFixedLength(9).byteLength), tr(() => r.transfer(-1)));
const rd = new ArrayBuffer(2, { maxByteLength: 8 }); rd.transfer();
p(tr(() => rd.resize(1)), rd.byteLength, rd.maxByteLength, rd.resizable, tr(() => rd.transferToFixedLength()));
p(tr(() => ArrayBuffer.prototype.transfer.call({})), tr(() => ArrayBuffer.prototype.resize.call({}, 1)), tr(() => Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'detached').get.call({})));
p(tr(() => ArrayBuffer.prototype.slice.call(1)), tr(() => Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'byteLength').get.call(1)),
  tr(() => Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'resizable').get.call([])), tr(() => ArrayBuffer.prototype.transfer.call(new Uint8Array(1))));
p(ArrayBuffer.prototype.transfer.length, ArrayBuffer.prototype.transferToFixedLength.length, ArrayBuffer.prototype.resize.length, ArrayBuffer.prototype.slice.length);
p(Object.prototype.toString.call(new ArrayBuffer(1, { maxByteLength: 2 })), typeof ArrayBuffer.prototype.transfer, 'detached' in ArrayBuffer.prototype);
}

// views over a detached buffer, and over one too short for them
{
const e = new ArrayBuffer(8), ev = new Int16Array(e, 2, 2), edv = new DataView(e, 1);
e.transfer();
p(ev.length, ev.byteLength, ev.byteOffset, ev[0], (ev[0] = 5, ev[0]), Object.keys(ev), 0 in ev, tr(() => ev.at(0)), tr(() => ev.slice()), tr(() => ev.set([1])));
p(tr(() => edv.byteLength), tr(() => edv.byteOffset), tr(() => edv.getInt8(0)), tr(() => edv.setInt8(0, 1)), tr(() => edv.buffer.byteLength));
p(tr(() => ev.indexOf(0)), tr(() => ev.includes(undefined)), tr(() => ev.forEach(x => x)), tr(() => ev.map(x => x)), tr(() => ev.sort()));
p(tr(() => ev.subarray(0)), tr(() => ev.reverse()), tr(() => ev.copyWithin(0, 1)), tr(() => ev.entries()), tr(() => ev.values().next()), tr(() => ev[Symbol.iterator]()));
p(tr(() => new Int16Array(ev)), tr(() => Uint8Array.from(ev)), tr(() => Object.getOwnPropertyDescriptor(ev, '0')));
p(tr(() => new Uint8Array(e, 0)), tr(() => new Uint8Array(new ArrayBuffer(4), 5)), tr(() => new Uint8Array(new ArrayBuffer(4), 1, 4)), tr(() => new Uint32Array(new ArrayBuffer(6))));
const rab = new ArrayBuffer(4, { maxByteLength: 8 }); const ta = new Uint8Array(rab, 2, 2); rab.resize(2);
p(tr(() => ta.set([1])), tr(() => ta.sort()), tr(() => ta.with(0, 1)), tr(() => ta.copyWithin(0, 0)), tr(() => ta.reverse()), tr(() => ta.map(x => x)), tr(() => ta.filter(x => x)));
p(tr(() => ta.slice()), tr(() => ta.toSorted()), tr(() => ta.toReversed()), tr(() => ta.fill(0)), tr(() => ta.at(0)), tr(() => ta.subarray(0)), tr(() => ta.toLocaleString()), tr(() => ta.keys()));
p(tr(() => ta.every(x => x)), tr(() => ta.some(x => x)), tr(() => ta.find(x => x)), tr(() => ta.findIndex(x => x)), tr(() => ta.findLast(x => x)), tr(() => ta.findLastIndex(x => x)), tr(() => ta.lastIndexOf(0)), tr(() => ta.reduce((a, b) => a)), tr(() => ta.reduceRight((a, b) => a)));
p(tr(() => new Uint8Array(ta)), tr(() => Uint8Array.from(ta)), tr(() => new Uint8Array(4).set(ta)), ta.length, ta.byteOffset, ta.byteLength);
rab.resize(4);
p(ta.length, ta.byteOffset, tr(() => ta.join()), ta.forEach.name, ta.findLastIndex.length, ta.values === ta[Symbol.iterator], Uint8Array.prototype.join.length);
const fix = new Uint8Array(rab, 0, 1); rab.resize(0);
p(fix.length, tr(() => new Uint8Array(rab, 0, 1)), tr(() => new Uint8Array(rab, 1)), new Uint8Array(rab).length, new DataView(rab).byteLength, tr(() => new DataView(rab, 1)));
p(ArrayBuffer.isView(fix), ArrayBuffer.isView(new DataView(rab)), ArrayBuffer.isView(rab));
const de = new ArrayBuffer(4), dev = new Uint8Array(de); de.transfer();
p(tr(() => Object.defineProperty(new Uint8Array(2), 5, { value: 1 })), tr(() => Object.defineProperty(new Uint8Array(2), 0, { value: 1, configurable: false })),
  tr(() => Object.defineProperty(dev, '0', { value: 1 })), tr(() => Object.defineProperty(new Uint8Array(2), 0, { get() {} })),
  tr(() => Object.defineProperty(new Uint8Array(2), '-0', { value: 1 })), tr(() => Object.defineProperty(new Uint8Array(2), '1.5', { value: 1 })),
  tr(() => Object.defineProperty(new Uint8Array(2), 1, { value: 9 })[1]), tr(() => Reflect.defineProperty(fix, 'NaN', { value: 1 })), tr(() => Object.defineProperty(fix, 'n', { value: 1 }).n));
}
