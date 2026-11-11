// tests/js/bigint.js -- BigInt, each line against V8's (bigint.expect): literals in every radix,
// the arithmetic of numbers past 64 bits (long division, powers), the bitwise operators on
// two's complement, comparisons and equality with numbers and strings, the conversions and
// their errors, asIntN and asUintN, BigInts as keys, BigInt64Array and DataView's BigInt methods.
{
const p = (...a) => console.log(a.map(x => typeof x === 'bigint' ? x.toString() + 'n' : typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
p(1n, typeof 1n, 0x1Fn, 0o17n, 0b101n, 1_000_000n, -5n);
const a = 123456789012345678901234567890n, b = 987654321098765432109876543210n;
p(a + b, a - b, a * b, b / a, b % a, -a / 7n, -a % 7n, 2n ** 100n, (-3n) ** 3n);
p(a * a * a * a, (a * b) / b === a, 10n ** 30n / 7n);
p(5n & 3n, 5n | 3n, 5n ^ 3n, ~5n, -5n & 3n, -5n | 3n, -5n ^ -3n, ~-1n, 1n << 70n, -(1n << 70n) >> 3n, -7n >> 1n, 7n >> 1n, 5n << -1n);
p(1n < 2n, 2n < 1n, 1n < 2, 2 > 1n, 1n == 1, 1n === 1, 1n == '1', 2n > '1', 1n < 1.5, 1n > NaN, 0n == false, 10n > 9.99);
p(BigInt(42), BigInt('0x10'), BigInt(' 123 '), BigInt(true), BigInt(Number.MAX_SAFE_INTEGER) + 2n, BigInt(1e30));
try { BigInt(1.5); } catch (e) { p(e.constructor.name, e.message); }
try { BigInt('1.5'); } catch (e) { p(e.constructor.name, e.message); }
try { 1n + 1; } catch (e) { p(e.constructor.name, e.message); }
try { +1n; } catch (e) { p(e.constructor.name, e.message); }
try { 1n / 0n; } catch (e) { p(e.constructor.name, e.message); }
try { JSON.stringify({ x: 1n }); } catch (e) { p(e.constructor.name, e.message); }
try { new BigInt(1); } catch (e) { p(e.constructor.name, e.message); }
try { 1n >>> 0n; } catch (e) { p(e.constructor.name); }
try { Math.max(1n); } catch (e) { p(e.constructor.name); }
p(String(255n), (255n).toString(16), (-255n).toString(2), (2n ** 64n).toString(36), `${10n}`, 10n + '', Number(2n ** 53n + 1n), Number(-(2n ** 1100n)));
p(BigInt.asIntN(8, 255n), BigInt.asUintN(8, -1n), BigInt.asIntN(64, 2n ** 63n), BigInt.asUintN(64, -1n), BigInt.asIntN(3, 25n), BigInt.asUintN(0, 5n));
let x = 10n; x++; x += 5n; x *= 2n; x--; p(x, -x, x / 3n);
for (let i = 0n; i < 3n; i++) p('loop', i);
p(!!0n, !!1n, 0n ? 'yes' : 'no', [3n, 1n, 2n].sort().join(), typeof Object(1n), Object(1n) + 1n, Object.prototype.toString.call(1n));
const m = new Map([[1n, 'one'], [2n ** 70n, 'big']]); p(m.get(1n), m.get(2n ** 70n), new Set([1n, 1n, 2n]).size, [1n, 2n].includes(2n), [1n, 2n].indexOf(2n));
const o = { [5n]: 'five' }; p(o['5'], o[5n], [10, 20][1n]);
p(BigInt64Array.from([1n, -1n, 2n ** 63n]).join(), new BigUint64Array([5n, -1n])[1], new BigInt64Array(2).fill(7n).join(), BigInt64Array.of(3n, 1n, 2n).sort().join(), new BigUint64Array([1n, 2n]).reverse().join());
const dv = new DataView(new ArrayBuffer(8)); dv.setBigInt64(0, -2n); p(dv.getBigInt64(0), dv.getBigUint64(0), dv.getBigUint64(0, true));
p(1n == 1n, 0n === -0n, Object.is(0n, -0n), [0n].includes(-0n), 5n % -3n, -5n % 3n, (-5n) / 3n);
function fact(n) { return n <= 1n ? 1n : n * fact(n - 1n); } p(fact(30n), String(fact(100n)).length);
p((123n).toLocaleString(), BigInt.prototype.valueOf.call(7n), 0n.toString(), (-0n).toString());
}
