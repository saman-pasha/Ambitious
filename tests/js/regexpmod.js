// tests/js/regexpmod.js -- the modifiers of ES2025, (?ims-ims:...), against Chromium's V8
// (regexpmod.expect, written by node tools/js-chromium.js tests/js/regexpmod.js; node 22's
// V8 predates them): i, m and s changed for the part of the pattern within the group, in
// its characters, classes, back references, word boundaries, anchors and dots; the
// syntax's errors in V8's words.

const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const m = (re, s) => { const r = re.exec(s); return r ? JSON.stringify(r) + '@' + r.index : 'null'; };
const err = (src, fl) => { try { const r = new RegExp(src, fl); return 'ok ' + r.flags; } catch (e) { return e.name + ': ' + e.message; } };

// i, m and s, added and taken away
{
p(m(/(?i:a)b/, 'AB'), m(/(?i:a)b/, 'Ab'), m(/a(?-i:b)/i, 'AB'), m(/a(?-i:b)/i, 'Ab'));
p(m(/(?m:^b)/, 'a\nb'), m(/^b/, 'a\nb'), m(/(?s:a.b)/, 'a\nb'), m(/a(?-s:.)b/s, 'a\nb'), m(/(?-m:^b)/m, 'a\nb'), m(/(?m:a$)/, 'a\nb'));
p(m(/(?i:[a-z]+)/, 'ABC'), m(/(?i:\w)/u, 'ſ'), m(/(?i:\b)/u, 'ſ'), m(/\b/u, 'ſ'), m(/(a)(?i:\1)/, 'aA'), m(/(?i:(a))\1/, 'Aa'), m(/(?i:(a))\1/, 'AA'));
p(m(/(?i:x)y/, 'XY'), m(/(?i:x)/, 'X'), m(/(?i:ab)c/, 'ABc'), 'ABC'.replace(/(?i:b)/g, '-'), JSON.stringify('aBcbB'.split(/(?i:b)/)));
p(m(/(?i:ß)/u, 'ẞ'), m(/(?i:k)/u, '\u212A'), m(/(?i:k)/, '\u212A'), m(/(?i:[^a])/, 'A'), m(/(?i:[^a])/v, 'A'), m(/(?i:\p{Lu})/u, 'a'));
p(m(/(?i:[\q{AB}])/v, 'ab'), m(/(?-i:[\q{AB}])/vi, 'ab'), m(/(?<=(?i:a))b/, 'Ab'), m(/(?i:a(?-i:b)c)/, 'AbC'), m(/(?i:a(?-i:b)c)/, 'ABC'));
p(m(/(?ims:^a.b$)/, 'x\nA\nB\ny'), m(/(?ims-:^a.b$)/, 'x\nA\nB'), m(/(?-ims:^a.b$)/ims, 'x\nA\nB'), m(/(?i-s:a.)/s, 'A\n'), m(/(?s-i:a.)/i, 'A\n'));
p(m(/(?i:a)+/, 'aAaA'), m(/(?i:ab){2}/, 'AbaB'), m(/(?i:a|b)c/, 'Bc'), m(/(?i:a|b)c/, 'BC'), m(/x(?i:)y/, 'xy'), m(/((?i:a))(b)/, 'Ab'));
p(m(/(?i:\u0041)/, 'a'), m(/(?i:\x61)/, 'A'), m(/(?i:[\u0061-\u0063])/, 'C'), m(/(?i:\101)/, 'a'), m(/(?<n>(?i:x))\k<n>/, 'Xx'), m(/(?<n>(?i:x))(?i:\k<n>)/, 'Xx'));
}

// the places a match may begin, which the modifiers change
{
p(m(/(?i:hello) world/, 'HeLLo world'), m(/hello (?i:world)/, 'hello WORLD'), m(/(?-i:Hello) world/i, 'hello WORLD'), m(/(?-i:Hello) world/i, 'Hello WORLD'));
p(JSON.stringify('xAx ax Ax'.match(/(?i:a)x/g)), JSON.stringify('ab AB Ab'.match(/a(?i:b)/g)), 'aaa AAA'.search(/(?i:aaa)/), 'xxAAA'.search(/(?i:a){3}/));
p(m(/a*(?i:b)/, 'aaaB'), m(/a*?(?i:b)/, 'aaB'), m(/(?i:a*)b/, 'AAb'), m(/[0-9]+(?i:x)/, '12X'), m(/(?m:^)a/, 'b\na'), m(/^(?m:a$)/, 'a\nb'));
p(m(/(?i:\u{1F600}x)/u, '😀X'), m(/(?i:é)/, 'É'), m(/(?i:É)+/, 'éÉé'), JSON.stringify('ÀàÁ'.match(/(?i:à)/g)));
}

// the syntax's errors
{
const cases = ['(?i:a)', '(?-i:a)', '(?ims-:a)', '(?-ims:a)', '(?i-m:a)', '(?-:a)', '(?ii:a)', '(?i-i:a)', '(?m-m:a)', '(?x:a)', '(?i', '(?i)', '(?i-)', '(?-)',
  '(?I:a)', '(?u:a)', '(?g:a)', '(?i-s-m:a)', '(?is-mi:a)', '(?--i:a)', '(?i:', '(?i:a', '(?iii:a)', '(?ims:a)', '(?s-i:a)', '(?i-ms:a)',
  '(?i-:a)', '(?-s:.)', '(?i:a)+', '(?i:a){2}', '(?i<n>a)', '(?i=a)', '(?-i=a)', '(?mi:^a)', '(?i:(?-i:a))', '(?i:a))', '(?i:a)|(?m:b)', '(?is:)'];
for (const c of cases) p(JSON.stringify(c), err(c, ''), '|', err(c, 'u'), '|', err(c, 'v'));
}
