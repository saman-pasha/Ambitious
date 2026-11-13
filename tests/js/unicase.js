// tests/js/unicase.js -- Unicode's case in the engine at Unicode 17.0, each line against V8's
// (unicase.expect): toUpperCase and toLowerCase of every code point (the full mappings,
// SpecialCasing's unconditional ones), by block of 4096 (how many change, the first and the
// last, a hash); the final sigma's context before and after every code point (Cased and
// Case_Ignorable, as ICU reads them); and case-insensitive matching, each code point case
// touches against every other: with i the uppercase of code units, with u and v simple case
// folding; ranges and their complements closed under case, \w \W \b with i, back references.
// The work runs as a queue of timers, so that the collector runs between them.

const p = (...a) => console.log(a.join(' '));
const hex = c => c.toString(16);
const units = s => { const r = []; for (let i = 0; i < s.length; i++) r.push(hex(s.charCodeAt(i))); return r.join('.'); };
const cpsOf = s => [...s].map(c => c.codePointAt(0));
const queue = [];
const later = f => queue.push(f);
const next = () => { const f = queue.shift(); if (f) { f(); setTimeout(next, 0); } };
// f on every code point but the surrogates, a plane a task
const planes = f => { for (let b = 0; b < 0x110000; b += 0x10000) later(() => { for (let c = b; c < b + 0x10000; c++) if (c < 0xD800 || c > 0xDFFF) f(c); }); };
const kelvin = String.fromCharCode(0x212A), longS = String.fromCharCode(0x17F);

// every code point's upper and lower case
for (const [name, f] of [['upper', s => s.toUpperCase()], ['lower', s => s.toLowerCase()]]) {
  const st = { total: 0, longer: [], b: -1, n: 0, h: 0, first: -1, last: -1 };
  const flush = () => { if (st.n) { p(name, hex(st.b), st.n, hex(st.first), hex(st.last), st.h); st.total += st.n; } st.n = st.h = 0; st.first = -1; };
  planes(c => {
    if ((c & 0xFFF) === 0) { flush(); st.b = c; }
    const s = String.fromCodePoint(c), t = f(s);
    if (t === s) return;
    st.n++;
    if (st.first < 0) st.first = c;
    st.last = c;
    st.h = (st.h * 31 + c) >>> 0;
    for (let i = 0; i < t.length; i++) st.h = (st.h * 31 + t.charCodeAt(i)) >>> 0;
    if (cpsOf(t).length > 1) st.longer.push(hex(c) + ':' + units(t));
  });
  later(() => {
    flush();
    p(name, 'in all', st.total, 'to more than one:', st.longer.length);
    for (let i = 0; i < st.longer.length; i += 8) p(' ', st.longer.slice(i, i + 8).join(' '));
  });
}
later(() => {
  let lone = '';
  for (let c = 0xD800; c < 0xE000; c += 0x80) lone += String.fromCharCode(c);
  p('lone surrogates', lone.toUpperCase() === lone, lone.toLowerCase() === lone, ('a' + lone + 'ß').toUpperCase().length);
});

// the final sigma: a capital sigma lowercases to the final one after a cased letter (case-
// ignorable ones between) and before none. Each code point x in 'A' + x + 'Σ' (is x cased or
// case-ignorable?) and in 'AΣ' + x (is x cased and not ignorable?): the code points of each kind
{
const kinds = [[], [], [], []];
planes(c => {
  const x = String.fromCodePoint(c);
  const before = ('A' + x + 'Σ').toLowerCase().endsWith('ς') ? 1 : 0;
  const after = ('AΣ' + x).toLowerCase()[1] === 'ς' ? 2 : 0;
  const k = kinds[before | after];
  if (k.length && k[k.length - 1][1] === c - 1) k[k.length - 1][1] = c; else k.push([c, c]);
});
later(() => {
  // (x cased: final after it, not before it; case-ignorable: final both ways; neither: final
  // before it alone; and none is final neither way)
  const names = ['final neither way', 'cased', 'neither', 'case-ignorable'];
  for (let k = 0; k < 4; k++) {
    const r = kinds[k];
    let n = 0, h = 0;
    for (const [lo, hi] of r) { n += hi - lo + 1; h = (h * 31 + lo) >>> 0; h = (h * 31 + hi) >>> 0; }
    p('sigma', names[k], n, r.length, r.slice(0, 3).map(x => hex(x[0]) + '-' + hex(x[1])).join(' '), h);
  }
  const sig = s => units(s.toLowerCase());
  for (const s of ['Σ', 'AΣ', 'AΣB', 'ΣA', 'AΣΣ', 'AΣΣB', 'ΣΣΣ', 'A.Σ', 'A.Σ.', 'AΣ.B', 'A\u0345Σ', '\u0345Σ', 'AΣ\u0345',
                   'AΣ\u0345B', 'A\u0345\u0345Σ', 'AΣ\u0345\u0345', 'ªΣ', 'ǅΣ', 'ǅΣǅ', 'A\u00adΣ', 'A’Σ', 'A\u0307Σ',
                   'A\u0307ΣB', '𐐀Σ', 'AΣ𐐨', 'A\ud834\udd65Σ', 'A\udb40\udc01Σ', 'AΣ\udb40\udc01B',
                   'İΣ', 'ΣİA', 'AİΣ', 'ΑΣ ΟΔΟΣ', 'ΌΣΟΣ.', 'Σ1', '1Σ', 'AΣ1', 'A1Σ', 'A\u200dΣ', 'AΣ\u200cB', 'A\ufe0fΣ',
                   'A🏻Σ', 'ʰΣ', 'AʰΣ', 'ᴬΣ', 'ⅠΣ', 'AⅠΣ', 'AΣⒶ', 'ⒶΣ', 'A\ud800Σ', 'AΣ\udc00'])
    p('sigma', JSON.stringify(s).replace(/[\u0080-\uffff]/g, c => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0')), sig(s));
});
}

// case-insensitive matching. The code points case touches: those toUpperCase or toLowerCase
// changes, and the one code point they become
const touched = new Set(), g = {};
planes(c => {
  const s = String.fromCodePoint(c);
  for (const t of [s.toUpperCase(), s.toLowerCase()]) {
    if (t === s) continue;
    touched.add(c);
    const q = cpsOf(t);
    if (q.length === 1) touched.add(q[0]);
  }
});
later(() => {
  g.list = [...touched].sort((a, b) => a - b);
  g.all = String.fromCodePoint(...g.list);
  g.bmp = String.fromCodePoint(...g.list.filter(c => c <= 0xFFFF));
  p('touched', g.list.length, 'bmp', g.bmp.length);
});
const esc = c => '\\u{' + hex(c) + '}';
const esc4 = c => '\\u' + hex(c).padStart(4, '0');
// each against every other: the code points a pattern of one matches, but itself; by block of
// 4096, and the orbits of three or more
for (const fl of ['i', 'iu', 'iv']) {
  const st = { pairs: 0, h: 0, big: [], blocks: new Map() };
  for (let part = 0; part < 4; part++) later(() => {
    const s = fl === 'i' ? g.bmp : g.all, n = g.list.length;
    for (const c of g.list.slice(Math.floor(part * n / 4), Math.floor((part + 1) * n / 4))) {
      if (fl === 'i' && c > 0xFFFF) continue;
      const m = (s.match(new RegExp(fl === 'i' ? esc4(c) : esc(c), 'g' + fl)) || []).map(x => x.codePointAt(0)).filter(x => x !== c);
      if (!m.length) continue;
      st.pairs += m.length;
      let k = c;
      for (const x of m) k = (k * 31 + x) >>> 0;
      st.h = (st.h * 31 + k) >>> 0;
      const e = st.blocks.get(c >> 12) || [0, 0];
      e[0] += m.length;
      e[1] = (e[1] * 31 + k) >>> 0;
      st.blocks.set(c >> 12, e);
      if (m.length > 1) st.big.push(hex(c) + ':' + m.map(hex).join('.'));
    }
  });
  later(() => {
    p('/' + fl, 'pairs', st.pairs, st.h);
    for (const [b, e] of st.blocks) p(' ', fl, hex(b << 12), e[0], e[1]);
    p(' ', fl, 'three or more:', st.big.length);
    for (let i = 0; i < st.big.length; i += 8) p('   ', st.big.slice(i, i + 8).join(' '));
  });
}
// a back reference compares case-insensitively too, code point by code point with u
later(() => {
  for (const fl of ['i', 'iu', 'iv']) {
    const re = new RegExp('^(.)\\1$', fl);
    let yes = 0, no = 0, h = 0;
    for (const c of g.list) {
      if (fl === 'i' && c > 0xFFFF) continue;
      const s = String.fromCodePoint(c);
      for (const t of new Set([s.toUpperCase(), s.toLowerCase(), s.toUpperCase().toLowerCase(), s.toLowerCase().toUpperCase()])) {
        if (t === s || cpsOf(t).length !== 1 || (fl === 'i' && t.length !== 1)) continue;
        const r = re.test(s + t);
        if (r) yes++; else no++;
        h = (h * 31 + (r ? 1 : 2) + c) >>> 0;
      }
    }
    p('back reference /' + fl, yes, no, h);
  }
  const t = (re, s) => re.test(s) ? 1 : 0;
  p(t(/^(𐐀)\1$/iu, '𐐀𐐨'), t(/^(𐐀)\1$/i, '𐐀𐐨'), t(/^(.+)\1$/iu, 'a𐐀A𐐨'),
    t(/(?<=\1(.))x/iu, '𐐨𐐀x'), t(/^(.)\1$/iu, 'ſs'), t(/^(.)\1$/i, 'ſs'), t(/^(.)\1$/iu, kelvin + 'k'), t(/^(.)\1$/i, kelvin + 'k'),
    t(/^(.)\1$/iu, 'ıI'), t(/^(.)\1$/iu, 'ßẞ'), t(/^(.)\1$/i, 'ßẞ'), t(/^(.)\1$/iv, 'ΐΐ'), t(/^(.)\1$/iu, 'ﬅﬆ'), t(/^(.)\1$/i, 'ﬅﬆ'));
});

// ranges closed under case, and their complements: what they take among the touched
later(() => {
  const take = (src, fl) => {
    let re;
    try { re = new RegExp(src, 'g' + fl); } catch (e) { return e.name; }
    const m = (fl === 'i' ? g.bmp : g.all).match(re) || [];
    let h = 0;
    for (const x of m) h = (h * 31 + x.codePointAt(0)) >>> 0;
    return m.length + ' ' + h;
  };
  for (const src of ['[a-z]', '[A-Z]', '[^a-z]', '[^A-Z]', '[k]', '[s]', '[K-S]', '[\\u00c0-\\u00ff]', '[\\u0370-\\u03ff]', '[^\\u0370-\\u03ff]',
                     '[\\u0400-\\u052f]', '[\\u1e00-\\u1fff]', '[\\u2c00-\\u2c5f]', '[\\u2c60-\\u2c7f]', '[\\ua640-\\ua69f]', '[\\ua720-\\ua7ff]',
                     '[\\uab70-\\uabbf]', '[\\u13a0-\\u13ff]', '[\\u1c80-\\u1c8f]', '[\\uff21-\\uff3a]', '[\\u0130-\\u0131]', '[\\u017f]', '[\\u212a]',
                     '[\\u2126]', '[\\u00df\\u1e9e]', '[\\u1fd3\\u1fe3\\ufb05]', '[\\u0390\\u03b0\\ufb06]', '[\\u0345]', '[\\u03c2]', '[\\u1c80-\\u1c88]',
                     '[\\ua7cb\\ua7dc]', '[\\u0264\\u019b]', '[\\ua7c0-\\ua7dc]', '[^\\ua7c0-\\ua7dc]', '\\w', '\\W', '[\\w]', '[^\\w]', '[\\W]',
                     '[^\\W]', '\\p{Lu}', '\\P{Lu}', '[^\\p{Lu}]', '\\p{Ll}', '[\\p{Lt}]', '\\p{Lower}', '\\P{Lower}'])
    p(src, take(src, 'i'), '|', take(src, 'iu'), '|', take(src, 'iv'));
  for (const src of ['[\\u{10400}-\\u{1044f}]', '[\\u{104b0}-\\u{104fb}]', '[\\u{10570}-\\u{105bc}]', '[\\u{10c80}-\\u{10cff}]',
                     '[\\u{10d50}-\\u{10d65}]', '[\\u{10d70}-\\u{10d85}]', '[^\\u{10d50}-\\u{10d65}]', '[\\u{118a0}-\\u{118ff}]',
                     '[\\u{16e40}-\\u{16e9f}]', '[\\u{16ea0}-\\u{16eb8}]', '[\\u{16ebb}-\\u{16ed3}]', '[\\u{1e900}-\\u{1e95f}]'])
    p(src, take(src, 'iu'), '|', take(src, 'iv'));
});

// \w, \W and \b with i, u and v, over every code point
later(() => {
  const parts = [];
  for (let b = 0; b < 0x110000; b += 0x400) {
    if (b >= 0xD800 && b < 0xE000) continue;
    const a = [];
    for (let i = 0; i < 0x400; i++) a.push(b + i);
    parts.push(String.fromCodePoint(...a));
  }
  g.S = parts.join('');
});
const at = i => i < 0xD800 ? i : i < 0xF800 ? i + 0x800 : 0x10000 + ((i - 0xF800) >> 1);
for (const src of ['\\w', '\\W', '[\\w]', '[^\\w]', '\\s', '\\S', '\\d', '[\\d\\s]'])
  later(() => {
    for (const fl of ['', 'i', 'u', 'iu', 'v', 'iv']) {
      let n = 0, runs = 0, h = 0;
      for (const m of g.S.matchAll(new RegExp('(?:' + src + ')+', 'g' + fl))) {
        const lo = at(m.index), hi = at(m.index + m[0].length - 1);
        n += hi - lo + 1;
        runs++;
        h = (h * 31 + lo) >>> 0;
        h = (h * 31 + hi) >>> 0;
      }
      p(src, '/' + fl, n, runs, h);
    }
  });
later(() => {
  for (const src of ['\\b', '\\B'])
    for (const fl of ['', 'i', 'u', 'iu', 'v', 'iv']) p(src, '/' + fl, g.S.replace(new RegExp(src, 'g' + fl), '\0').length - g.S.length);
  const text = longS + 'tar ' + kelvin + 'elvin ' + longS + ' ' + kelvin + ' x' + g.S.slice(0x17E, 0x182);
  p(text.match(/\b[\w]+\b/giu).length, text.match(/\b[\w]+\b/gi).length, /\bſ/iu.test(' ' + longS), /\bſ/i.test(' ' + longS),
    /ſ\b/iv.test(longS + ' '), /\u212a\B/iu.test(kelvin + kelvin), /\u212a\B/i.test(kelvin + kelvin), /a\b/iu.test('a' + kelvin), /a\B/iu.test('a' + longS));
});

setTimeout(next, 0);
