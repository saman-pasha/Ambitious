// tests/js/uniident.js -- what the lexer and the group names take as an identifier and as white
// space at Unicode 17.0, each line against V8's (uniident.expect): ID_Start begins a name,
// ID_Continue (and ZWNJ, ZWJ) goes on with it, escaped or not; U+FEFF and the space separators
// are white space, LS and PS line terminators. Every code point of the BMP and every assigned
// one beyond it (the private use planes' and the unassigned every 97th, the big ideograph
// blocks' every 31st) is tried alone; every ID_Start and ID_Continue code point at once. The
// work runs as a queue of timers, so that the collector runs between them.

const p = (...a) => console.log(a.join(' '));
const hex = c => c.toString(16);
const compiles = src => { try { new Function(src); return 1; } catch (e) { return 0; } };
const named = (src, fl) => { try { new RegExp(src, fl); return 1; } catch (e) { return 0; } };
const queue = [];
const later = f => queue.push(f);
const next = () => { const f = queue.shift(); if (f) { f(); setTimeout(next, 0); } };
// f on every code point beyond ASCII but the surrogates that pick takes, 16384 a task
const chunks = f => { for (let b = 0; b < 0x110000; b += 0x4000) later(() => { for (let c = Math.max(b, 0x80); c < b + 0x4000; c++) if ((c < 0xD800 || c > 0xDFFF) && pick(c)) f(c); }); };

// which code points to try alone
const assigned = /\P{Cn}/u, priv = /\p{Co}/u, ideo = /\p{Ideographic}/u;
const pick = c => {
  if (c < 0x10000) return true;
  const s = String.fromCodePoint(c);
  if (!assigned.test(s) || priv.test(s)) return c % 97 === 0;
  if (ideo.test(s)) return c % 31 === 0;
  return true;
};

// each code point alone: c + 'a' compiles when c begins a name or is white space or a line
// terminator; 'a' + c + 'b' when c goes on with a name (or ends a line). Each kind's count, a
// hash of its code points, and its first six
{
const kinds = new Map();
let tried = 0;
chunks(c => {
  tried++;
  const x = String.fromCodePoint(c);
  const k = compiles(x + 'a') + 2 * compiles('a' + x + 'b');
  let r = kinds.get(k);
  if (!r) { r = { n: 0, h: 0, first: [] }; kinds.set(k, r); }
  r.n++;
  r.h = (r.h * 31 + c) >>> 0;
  if (r.first.length < 6) r.first.push(hex(c));
});
later(() => {
  p('tried', tried);
  const names = ['neither', 'begins (white space)', 'goes on', 'begins and goes on'];
  for (const k of [0, 1, 2, 3]) { const r = kinds.get(k) || { n: 0, h: 0, first: [] }; p('lexer', names[k], r.n, r.h, r.first.join(' ')); }
});
}

// the same for a group's name, with u and without
for (const fl of ['u', '']) {
  const kinds = [0, 0, 0, 0], hs = [0, 0, 0, 0];
  chunks(c => {
    const x = String.fromCodePoint(c);
    const k = named('(?<' + x + 'a>.)', fl) + 2 * named('(?<a' + x + '>.)', fl);
    kinds[k]++;
    hs[k] = (hs[k] * 31 + c) >>> 0;
  });
  later(() => p('group names /' + fl, kinds.join(' '), hs.join(' ')));
}

// every ID_Start and ID_Continue code point at once, in one function each; and every white space
{
const start = [], part = [], space = [];
for (let b = 0; b < 0x110000; b += 0x10000) later(() => {
  for (let c = Math.max(b, 0x80); c < b + 0x10000; c++) {
    if (c >= 0xD800 && c <= 0xDFFF) continue;
    const x = String.fromCodePoint(c);
    if (/\p{ID_Start}/u.test(x)) start.push(x);
    if (/\p{ID_Continue}/u.test(x)) part.push(x);
    if (/\s/u.test(x)) space.push(x);
  }
});
later(() => p('ID_Start', start.length, compiles(start.map(x => x + 'a').join(';')), compiles(start.join('') + ';')));
later(() => p('ID_Continue', part.length, compiles(part.map(x => 'a' + x).join(';')), compiles('a' + part.join('') + ';')));
later(() => {
  p('white space', space.length, space.map(x => compiles('1' + x + '+' + x + '2')).join(''), space.map(x => compiles('a' + x + 'b')).join(''));
  const f = new Function('var ' + start.slice(0, 2000).map((x, i) => x + i).join(', ') + '; return ' + start.slice(0, 2000).map((x, i) => x + i).join(' + ') + ';');
  p('names as values', typeof f, String(f()).length);
});
}

// white space where a string is read as a number or a BigInt, trimmed, or read as a date or JSON
later(() => {
  const r = f => { try { return String(f()); } catch (e) { return e.name; } };
  for (const c of [0x9, 0xa, 0xb, 0xc, 0xd, 0x20, 0x85, 0xa0, 0x1680, 0x180e, 0x2000, 0x2005, 0x200a, 0x200b, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000, 0xfeff]) {
    const x = String.fromCharCode(c);
    p('string', hex(c), r(() => BigInt(x + '12' + x)), r(() => Number(x + '12' + x)), r(() => parseFloat(x + '1.5' + x)), r(() => parseInt(x + '7' + x)),
      (x + 'a' + x).trim() === 'a', (x + 'a' + x).trimStart().length, (x + 'a' + x).trimEnd().length, r(() => Date.parse(x + '2020-01-02' + x)),
      r(() => Date.parse('Jan' + x + '2' + x + '2020')), r(() => JSON.parse(x + '1' + x)));
  }
});

// escapes in names, numbers before names, private names, flags, and what V8 says
later(() => {
const r = src => { try { const v = new Function(src)(); return 'ok ' + String(v); } catch (e) { return e.name; } };
for (const src of ['var \\u0061 = 1; return a', 'var \\u{61}b = 2; return ab', 'var a\\u0030 = 3; return a0', 'var \\u0030a = 1',
                   'var \\u2603 = 1', 'var a\\u2603 = 1', 'var \\u00aa = 4; return ª', 'var \\u{1d49c} = 5; return 𝒜', 'var 𝒜\\u{1d49c} = 6; return 𝒜𝒜',
                   'var \\ud835\\udc9c = 1', 'var a\\u200c = 7; return a\u200c', 'var \\u200c = 1', 'var a\\u200d\\u0300 = 8; return a\u200d\u0300',
                   'var \\u0024 = 9; return $', 'var \\u005f = 10; return _', 'var \\u005c = 1', 'var \\u{10d50} = 11; return \u{10d50}',
                   'var \\u{16eba} = 1', 'var \\u{1e5f0} = 12; return \u{1e5f0}', 'return 1ª', 'return 1\u3000+2', 'return 1\u2029+2',
                   'return 1\u{10d50}', 'return 0x1g', 'return 1n\u00a0+2n', 'var a\ufeffb = 1', 'return 1\ufeff+\ufeff1',
                   'class A { #ª = 13; m() { return this.#ª; } } return new A().m()', 'class A { #\u{10d50} = 14; m() { return this.#\u{10d50}; } } return new A().m()',
                   'class A { #☃ = 1 }', 'return /a/ª', 'return /a/gª', 'return /a/\u3000.source', 'var x = { ª: 15, \u{10d50}: 16 }; return x.ª + x.\u{10d50}',
                   'var ゛ = 17; return ゛', 'var ℘ = 18; return ℘', 'var a· = 19; return a·', 'var ·a = 1', 'var a፩ = 20; return a፩', 'var ፩a = 1',
                   'var a\u{e0100} = 21; return a\u{e0100}', 'var \u{e0100}a = 1', 'var aⸯ = 1', 'var ⸯ = 1', 'var a・ = 22; return a・',
                   'var a･ = 23; return a･', 'var ・ = 1', 'var a༺ = 1', 'var a\u180e = 1', 'var a\u200b = 1', 'var a\u00ad = 1'])
  p(JSON.stringify(src), r(src));
for (const src of ['(?<𝒜>.)', '(?<a𝒜>.)', '(?<\\u{1d49c}>.)', '(?<\\ud835\\udc9c>.)', '(?<a\\ud835\\udc9c>.)', '(?<\ud835>.)', '(?<a\u200c>.)',
                   '(?<\u200c>.)', '(?<$_>.)', '(?<a1>.)', '(?<1a>.)', '(?<ª>.)', '(?<\u{10d50}>.)', '(?<a·>.)', '(?<·>.)', '(?<a\u{e0100}>.)'])
  p(JSON.stringify(src), named(src, ''), named(src, 'u'), named(src, 'v'));
});

setTimeout(next, 0);
