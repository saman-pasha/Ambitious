// tests/js/normalize.js -- String.prototype.normalize, each line against V8's (normalize.expect):
// NFC, NFD, NFKC and NFKD by Unicode's algorithm (decomposition, canonical ordering,
// composition with its exclusions, Hangul's arithmetic), the form's RangeError, and every
// code point of Unicode 15.0 in each form (the ranges new in 16.0 and 17.0 left out: the
// engine's tables are perl's Unicode 15.0, node's ICU has 17.0).

const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const hex = s => { let r = []; for (let i = 0; i < s.length; i++) r.push(s.charCodeAt(i).toString(16)); return r.join(' '); };
const forms = ['NFC', 'NFD', 'NFKC', 'NFKD'];
const each = s => forms.map(f => hex(s.normalize(f))).join(' | ');
const label = s => JSON.stringify(s.replace(/[\uD800-\uDFFF]/g, c => '<' + c.charCodeAt(0).toString(16) + '>'));

// the forms
{
for (const s of ['é', 'é', 'Å', 'Å', 'Ω', 'ﬁ', '①', '½', 'ẛ̣', 'ẛ̣', 'ḍ̇', 'ḍ̇', 'q̣̇', 'Ǖ', 'ǖ', '̈́', 'ཱི', 'क़',
                 '각', '가', '각', '각', 'ᄀ가', '한글', 'ㄱ', 'ｶ', 'ｶﾞ', '㌀', '℃', 'x²', 'ℌ',
                 'á́', '́a', 'ȩ́', 'ȩ́', 'ą́', 'ą́', 'ୋ', 'େ̀ା',
                 'ྲྀ', 'ཱི', 'אַ', 'שׁ', 'שּׁ', 'ᴕE', 'ᴕ7ᴖ5', 'ᾷ', 'ᾷ', 'ᾮ',
                 'آ', 'क़', 'x\uD800y', '\uDC00́', '😀́', '', 'abc', 'ÀÉÎÕÜ', 'ÿ', ' ', 'ß', 'ﬀ', '™', '…'])
  p(label(s), each(s));
}

// what the form may be
{
p('abc'.normalize() === 'abc', 'é'.normalize().length, 'é'.normalize(undefined).length, 'é'.normalize('NFD').length, String.prototype.normalize.length, String.prototype.normalize.name);
for (const f of ['nfc', '', null, 'NFC ', 'NFKD\0', 1, true]) { try { p('ok', 'a'.normalize(f)); } catch (e) { p(e.name, e.message); } }
try { 'a'.normalize({ toString() { throw new Error('form'); } }); } catch (e) { p(e.name, e.message); }
try { 'a'.normalize(Symbol()); } catch (e) { p(e.name, e.message); }
p('x'.normalize({ toString() { return 'NFKC'; } }), String.prototype.normalize.call(123, 'NFD'), String.prototype.normalize.call({ toString() { return 'Å'; } }, 'NFD').length);
}

// every code point in each form: how many each changes, and a checksum of what it gives
{
const skip = c => c === 0xA7F1 || (c >= 0x105C0 && c <= 0x105FF) || (c >= 0x11380 && c <= 0x113FF) || (c >= 0x16100 && c <= 0x1613F) ||
                  (c >= 0x16D40 && c <= 0x16D7F) || (c >= 0x1CC00 && c <= 0x1CEBF);
for (const f of forms) {
  let changed = 0, h = 0;
  for (let c = 0; c < 0x110000; c++) {
    if ((c >= 0xD800 && c <= 0xDFFF) || skip(c)) continue;
    const s = String.fromCodePoint(c), t = s.normalize(f);
    if (t === s) continue;
    changed++;
    for (let i = 0; i < t.length; i++) h = (h * 31 + t.charCodeAt(i)) >>> 0;
    h = (h * 31 + c) >>> 0;
  }
  p(f, changed, h);
}
// a base and marks in every order, with Hangul's jamo after
const marks = [0x300, 0x301, 0x323, 0x327, 0x308, 0x31B, 0x345, 0x5B0, 0x93C, 0x1D165, 0x1D16D];
const bases = [0x61, 0x41, 0x6F, 0x55, 0x3B1, 0x3C9, 0x1100, 0xAC00, 0x928, 0x5D0, 0x1D158];
let n = 0, h = 0;
for (const b of bases) for (const m1 of marks) for (const m2 of marks) for (const m3 of [0, 0x302, 0x1161, 0x11A8]) {
  const s = String.fromCodePoint(b, m1, m2) + (m3 ? String.fromCodePoint(m3) : '');
  for (const f of forms) { const t = s.normalize(f); n++; for (let i = 0; i < t.length; i++) h = (h * 31 + t.charCodeAt(i)) >>> 0; h = (h * 31 + t.length) >>> 0; }
}
p('sequences', n, h);
}
