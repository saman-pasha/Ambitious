// tests/js/typed.js -- ArrayBuffer, the typed arrays, DataView, TextEncoder and TextDecoder, each
// line against V8's (typed.expect): the conversions of each element type (clamped and modular),
// views sharing a buffer, DataView's byte orders and bounds, the methods typed arrays have of their
// own and those they share with Array, UTF-8 both ways.
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : (ArrayBuffer.isView(x) ? x.constructor.name + '[' + Array.from(x).join(',') + ']' : JSON.stringify(x))).join(' '));
const u8 = new Uint8Array([1, 2, 300, -1]); p(u8, u8.length, u8.byteLength, u8[2], u8[10]);
const i8 = Int8Array.from([127, 128, -129, 255]); p(i8);
const c = new Uint8ClampedArray([-5, 0.5, 1.5, 2.5, 254.5, 300, NaN]); p(c);
const f32 = new Float32Array([1.1, 0.1 + 0.2, 1e40]); p(f32, f32[0] === 1.1);
const f64 = new Float64Array(3); f64[0] = 1.5; f64[1] = 'x'; f64.fill(7, 2); p(f64);
const buf = new ArrayBuffer(8); const v16 = new Uint16Array(buf); const v8 = new Uint8Array(buf, 2, 4);
v16[1] = 0x1234; v16[3] = 0xFFFF; p(v8, v16, buf.byteLength, v8.byteOffset, v8.buffer === buf);
const dv = new DataView(buf); dv.setUint32(0, 0xDEADBEEF); p(dv.getUint32(0).toString(16), dv.getUint32(0, true).toString(16), dv.getInt8(0), dv.getUint16(1, true));
dv.setFloat64(0, Math.PI, true); p(new Float64Array(buf)[0] === Math.PI, dv.getFloat32(0, true));
try { dv.getUint32(6); } catch (e) { p(e.constructor.name); }
try { new Uint16Array(buf, 1); } catch (e) { p(e.constructor.name, e.message); }
const a = new Int32Array([5, -3, 10, 0, -0, 2]); p(a.sort(), Int32Array.of(3, 1, 2).sort((x, y) => y - x));
p(new Float64Array([3, NaN, -1, -0, 0, 2]).sort());
const s = new Uint8Array([1, 2, 3, 4, 5]); p(s.subarray(1, 3), s.slice(-2), s.map(x => x * 2), s.filter(x => x & 1), s.reverse(), s.at(-1));
const sub = s.subarray(1); sub[0] = 99; p(s[1], sub.length, sub.byteOffset);
p(s.indexOf(99), s.includes(3), s.join('-'), s.reduce((x, y) => x + y), [...s.keys()], [...s.entries()].length, String(s));
p(Object.prototype.toString.call(s), s[Symbol.toStringTag], s instanceof Uint8Array, Object.keys(new Int8Array(3)));
const t = new Int16Array(4); t.set([1, 2], 1); t.set(new Uint8Array([9]), 3); p(t, t.with(0, 5), t.toReversed(), t.toSorted());
p(new Uint8Array(new Uint16Array([256, 257])), new Float32Array(new Set([1, 2])), new Uint8Array({ length: 2, 0: 7, 1: 8 }));
p(new Uint8Array(4).copyWithin(0, 2), new Uint8Array([1, 2, 3, 4, 5]).copyWithin(0, 3), Uint8Array.BYTES_PER_ELEMENT, new Float64Array().BYTES_PER_ELEMENT);
const ab2 = buf.slice(2, 6); p(new Uint8Array(ab2).length, ArrayBuffer.isView(dv), ArrayBuffer.isView(buf));
const enc = new TextEncoder(); const bytes = enc.encode('héllo 😀'); p(bytes, bytes.length, enc.encoding);
p(new TextDecoder().decode(bytes), new TextDecoder().decode(new Uint8Array([0xE2, 0x82, 0xAC, 0xFF, 0x41])), new TextDecoder().decode(new Uint8Array([0xEF, 0xBB, 0xBF, 0x61])));
try { new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array([0xC3])); } catch (e) { p(e.constructor.name); }
const into = new Uint8Array(4); p(enc.encodeInto('añb', into), into);
let sum = 0; const big = new Float64Array(100000); for (let i = 0; i < big.length; i++) big[i] = i * 0.5; for (const x of big) sum += x; p(sum);
for (const k in new Uint8Array(2)) p('key', k);
p(JSON.stringify(new Uint8Array([1, 2])), JSON.stringify({ t: new Int8Array([3]) }), Array.isArray(new Uint8Array(1)));
}
