// tests/js/regexpv.js -- regular expressions with the v flag (unicodeSets), each line against
// V8's (regexpv.expect): nested classes, differences (--) and intersections (&&), strings
// (\q{...}), the properties of strings (\p{RGI_Emoji} and its kin), case folding with v and
// i, the syntax's errors in V8's words; the flags getter, generic, and unicodeSets; and the
// errors of u's escapes that v shares.

const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
const m = (re, s) => { const r = re.exec(s); return r ? r[0] + '@' + r.index : 'null'; };
const all = (re, s) => JSON.stringify(s.match(re));
const cps = s => [...s].map(c => c.codePointAt(0).toString(16)).join(' ');
const err = (src, fl) => { try { const r = new RegExp(src, fl); return 'ok ' + r.flags; } catch (e) { return e.name + ': ' + e.message; } };

// set operations, nested classes
{
p(all(/[\p{L}--\p{Lu}]+/gv, 'abcDEFghi'), all(/[\p{Lu}&&\p{Script=Greek}]/gv, 'AΑΒbβ'), all(/[[a-z]--[aeiou]]+/gv, 'hello world'));
p(all(/[\w--\d]+/gv, 'ab12cd'), all(/[\w&&\d]+/gv, 'ab12cd'), all(/[^\d]+/gv, 'ab12cd'), all(/[[^a-c]&&[a-e]]/gv, 'abcdef'));
p(all(/[[a-z]--b--c]/gv, 'abcd'), all(/[[a-z]&&[b-y]&&[c-x]]/gv, 'abcxyz'), all(/[\d--[3-6]]/gv, '0123456789'), all(/[[a-c][x-z]]/gv, 'abwxyz'));
p(/[]/v.test('a'), /[^]/v.test('a'), /[[]]/v.test('a'), /[[^]]/v.test('\n'), /[^[^a]]/v.test('a'), /[^[^a]]/v.test('b'));
p(all(/[a[b[c[d]]]]/gv, 'abcde'), all(/[^[a-z]--[x-z]]/gv, 'awx1'), all(/[\p{Nd}--[\d]]/gv, '1٣٤x'), all(/[[\s\d]&&[^\x20]]/gv, '1 \t2'));
}

// strings: \q{...}, the longest first
{
p(all(/[\q{abc|ab|a}]/gv, 'abcabaxab'), all(/[\q{abc|d}x]/gv, 'abcdxa'), m(/[\q{}]/v, 'x'), m(/^[\q{}]$/v, ''), m(/[\q{ab|}]c/v, 'abc'));
p(all(/[\q{abc}--\q{abc}]/gv, 'abc'), all(/[\q{abc|def}&&\q{def}]/gv, 'abcdef'), all(/[\q{ab}a-c]+/gv, 'abxab cabc'));
p(m(/[\q{abc|ab}]c/v, 'abc'), m(/[\q{abc|ab}]c/v, 'abcc'), m(/^[\q{x|xy|xyz}]+$/v, 'xyzxyx'), m(/(?<=[\q{ab|xyz}])!/v, 'xyz!'));
p(m(/[\q{a\|b}]/v, 'a|b'), m(/[\q{\u{1F600}x}]/v, '😀x'), m(/[\q{a}b]/v, 'b'), all(/[\q{😀|😀😀}]/gv, '😀😀😀'));
p(m(/(?<![\q{ab}])c/v, 'abc xc'), m(/[\q{ab|cd}]{2}/v, 'xabcdab'), all(/[\q{ab|a}]*b/gv, 'aabab b'), 'xabyab'.replace(/[\q{ab}]/gv, '-'));
}

// the properties of strings (Emoji 17.0)
{
const fam = '👨‍👩‍👧‍👦', flag = '🇫🇷', kc = '1️⃣', tag = '🏴󠁧󠁢󠁥󠁮󠁧󠁿', mod = '👍🏽', basic = '😀', bseq = '☺️';
for (const pr of ['RGI_Emoji', 'Basic_Emoji', 'Emoji_Keycap_Sequence', 'RGI_Emoji_Flag_Sequence', 'RGI_Emoji_Tag_Sequence', 'RGI_Emoji_Modifier_Sequence', 'RGI_Emoji_ZWJ_Sequence']) {
  const re = new RegExp('^\\p{' + pr + '}$', 'v');
  p(pr, [fam, flag, kc, tag, mod, basic, bseq, 'a', '1', '☺', '🇫'].map(s => re.test(s) ? 1 : 0).join(''));
}
p((('I ' + fam + ' and ' + flag + kc + ' x' + mod + basic + bseq).match(/\p{RGI_Emoji}/gv) || []).map(cps));
p((('🇫🇷🇩🇪🇺🇸').match(/\p{RGI_Emoji_Flag_Sequence}/gv) || []).length, ('a1️⃣b#️⃣'.match(/\p{Emoji_Keycap_Sequence}/gv) || []).map(cps));
p(m(/[\p{RGI_Emoji}--\q{😀}]/v, '😀😃'), m(/[\p{RGI_Emoji}&&\p{Emoji_Keycap_Sequence}]/v, 'x1️⃣'), m(/[\p{RGI_Emoji_Flag_Sequence}&&a]/v, 'a'));
p(/^\p{RGI_Emoji}+$/v.test(fam + flag + mod), /^[\p{RGI_Emoji}a]+$/v.test('a' + fam + 'a'), m(/(?<=\p{RGI_Emoji})x/v, flag + 'x'));
p(m(/\p{RGI_Emoji}/v, '👨‍👩‍👧'), m(/\p{RGI_Emoji}/v, '👨‍💻x').length, cps(m(/\p{RGI_Emoji}/v, '👩🏽‍🚀!').split('@')[0]));
p(['🫩', '🫆', '🪾', '🫜', '🪉', '🪏', '🫟'].map(s => /^\p{Basic_Emoji}$/v.test(s) ? 1 : 0).join(''), /^\p{RGI_Emoji}$/v.test('🇨🇶'), /^\p{RGI_Emoji}$/v.test('🧑🏻‍🩰‍🧑🏼'));
let n = 0;
for (let c = 0x1F1E6; c <= 0x1F1FF; c++) for (let d = 0x1F1E6; d <= 0x1F1FF; d++) if (/^\p{RGI_Emoji_Flag_Sequence}$/v.test(String.fromCodePoint(c, d))) n++;
p('flags', n, ('🇦🇩🇪x🇫'.match(/\p{RGI_Emoji_Flag_Sequence}|./gv) || []).map(cps));
}

// case folding with v and i: every operand folded, then the sets' operations
{
p(/[\q{AB}]/vi.test('ab'), /[\q{ab}]/vi.test('AB'), /[\q{AB}&&\q{ab}]/vi.test('aB'), /[\q{AB}&&\q{ab}]/v.test('ab'), /[^a]/vi.test('A'), /[^a]/vi.test('b'));
p(/\P{Lu}/vi.test('a'), /\P{Lu}/ui.test('a'), /[^\P{Lu}]/vi.test('a'), /[^\P{Lu}]/ui.test('a'), /[\p{Lu}--[A-Z]]/vi.test('a'), /[\w--s]/vi.test('ſ'));
p(/[\w--s]/vi.test('S'), /\W/vi.test('ſ'), /\W/ui.test('ſ'), /[^\W]/vi.test('K'), /[\W]/vi.test('k'), /[a-z]/vi.test('K'), /[[a-z]--k]/vi.test('K'));
p(/[^\p{Ll}]/vi.test('a'), /[^\p{Ll}]/vi.test('A'), /\p{Ll}/vi.test('A'), /[[a-z]&&[A-Z]]/vi.test('q'), /[[a-z]&&[A-Z]]/v.test('q'), /[σ]/vi.test('ς'), /[\q{Σa}]/vi.test('ςA'));
const t = (re, s) => re.test(s) ? 1 : 0;
p([t(/[\q{a}]/vi, 'A'), t(/[\q{A}]/vi, 'a'), t(/[\q{k}]/vi, 'K'), t(/[\q{k}]/vi, 'K'), t(/[\q{K}]/vi, 'k'), t(/[\q{ß}]/vi, 'ẞ'), t(/[\q{ẞ}]/vi, 'ß'),
   t(/[\q{σ}]/vi, 'ς'), t(/[\q{ς}]/vi, 'Σ'), t(/[\q{Σ}]/vi, 'ς'), t(/[\q{ſ}]/vi, 's'), t(/[\q{s}]/vi, 'ſ'), t(/[\q{S}]/vi, 'ſ')].join(''));
p([t(/[\q{ab}]/vi, 'AB'), t(/[\q{ßx}]/vi, 'ẞx'), t(/[\q{ẞx}]/vi, 'ßx'), t(/[\q{σx}]/vi, 'ςx'), t(/[\q{ςx}]/vi, 'σx'), t(/[\q{ſx}]/vi, 'sx'), t(/[\q{sx}]/vi, 'ſx'),
   t(/[\q{Kx}]/vi, 'kx'), t(/[\q{kx}]/vi, 'Kx'), t(/[\q{Kx}]/vi, 'Kx')].join(''));
p([t(/[ß]/vi, 'ẞ'), t(/[ẞ]/vi, 'ß'), t(/[ς]/vi, 'σ'), t(/[ſ]/vi, 's'), t(/[K]/vi, 'k'), t(/[k]/vi, 'K'), t(/ß/vi, 'ẞ'), t(/ẞ/vi, 'ß'), t(/[ß-ß]/vi, 'ẞ')].join(''));
p([t(/[\q{ß}a]/vi, 'ẞ'), t(/[\q{ß|ab}]/vi, 'ẞ'), t(/[\q{ß}&&ß]/vi, 'ẞ'), t(/[\q{ß}--x]/vi, 'ẞ'), t(/[[\q{ß}]]/vi, 'ẞ'), t(/[\q{ẞ}&&\q{ß}]/vi, 'ß'), t(/[\q{ẞ}&&ß]/vi, 'ß')].join(''));
p(all(/[\p{Lu}--\p{ASCII}]+/giv, 'aÉéÀxz'), all(/\p{RGI_Emoji}/giv, 'a😀B1️⃣'), all(/[a-c\q{XY}]+/giv, 'AbxyC xY'));
}

// escapes, characters beyond the BMP, and the RegExp's own properties
{
p(/[\-\&\!\#\%\,\:\;\<\=\>\@\`\~]+/v.exec('-&!#%,:;<=>@`~')[0].length, /[\b]/v.test('\b'), /[\cA]/v.test('\x01'), /[\x41-\x43]/v.test('B'));
p(m(/\p{Lu}+/v, 'abCDe'), m(/\P{Lu}+/v, 'ABcdE'), m(/[\p{Lu}]+/v, 'abCDe'), m(/\d+/v, 'ab123'), m(/[\d\s]+/v, 'a1 2b'), m(/./v, '😀'), m(/^.$/v, '😀'));
p(/[😀-😂]/v.test('😁'), /^[^😀]$/v.test('😁'), all(/[\u{1F600}\u{1F601}]/gv, '😀x😁'), 'a😀b'.split(/(?:)/v).length, 'x😀y'.replace(/[^x]/gv, '.'));
const re = /[\q{ab|c}]/gv; p(re.flags, re.unicodeSets, re.unicode, String(re), re.source, re.exec('xxab').index, re.lastIndex);
p(Object.prototype.toString.call(/[a&&a]/v), new RegExp('[\\p{L}--\\p{Ll}]', 'v').test('A'), RegExp('[a&&b]', 'v').test('a'), String(/[\p{L}--\p{Lu}]/v));
}

// the syntax's errors, V8's words
{
const cases = ['[a&&&b]', '[a&&b&&c]', '[a--b--c]', '[a--b&&c]', '[a-z&&b]', '[ab&&c]', '[&&a]', '[a&&]', '[a--]', '[--a]',
  '[(]', '[)]', '[{]', '[}]', '[/]', '[-]', '[|]', '[a-]', '[-a]', '[a-z-]', '[\\q{ab}]', '[^\\q{ab}]', '[^\\q{a}]', '[^\\q{a|b}]', '[^\\q{}]',
  '\\P{RGI_Emoji}', '[^\\p{RGI_Emoji}]', '[\\P{RGI_Emoji}]', '[\\p{RGI_Emoji}--\\q{x}]', '[^[\\p{RGI_Emoji}&&\\q{x}]]', '[^[\\p{RGI_Emoji}&&a]]',
  '[^[\\p{RGI_Emoji}--\\p{RGI_Emoji}]]', '[^\\p{RGI_Emoji}--a]', '[^\\q{ab}&&\\q{ab}]', '[^\\q{ab}&&a]',
  '[[a]&&[b]]', '[\\q{ab|c}--\\q{c}]', '[a!!b]', '[a##]', '[!!]', '[a!b]', '[\\!]', '[\\&]', '[\\-]', '[\\q]', '[\\q{]', '[\\q{a}', '\\q{a}',
  '[a&b]', '[a-b--c]', '[\\d--4]', '[a[b&&c]]', '[z-a]', '[\\q{abc}-z]', '[a-\\q{b}]', '[\\d-z]', '[a-\\d]', '[[a]-z]', '[a-[z]]',
  '[\\]]', '[\\[]', '[\\B]', '[\\c]', '[\\cA]', '[\\0]', '[\\01]', '[\\1]', '[\\8]', '[a-z]{', 'a{', '{', '}', ']', '\\-',
  '[\\q{\\q}]', '[\\q{a&&b}]', '[\\q{(}]', '[ab--]', '[a&&b--c]', '[a&&&]', '[&&]', '[--]', '[&]', '[a&&&&b]', '[a----b]', '[a-z--]', '[a--b-c]',
  '[\\q{a}-b]', '[a-]]', '[$$]', '[**]', '[++]', '[,,]', '[..]', '[::]', '[;;]', '[<<]', '[==]', '[>>]', '[??]', '[@@]', '[^^]', '[``]', '[~~]', '[^^^]',
  '[a^^]', '\\p{RGI_Emoji=Yes}', '\\p{rgi_emoji}', '\\p{Foo}', '[\\p{Foo}]', '[\\p{Lu', '[[[[a]]]]', '[[[[a]]]', '[a', '[\\', '[a\\'];
for (const c of cases) p(JSON.stringify(c), err(c, 'v'));
p(err('a', 'uv'), err('a', 'vv'), err('a', 'vgimsyd'), err('(', 'vi'), err('[a&&&b]', 'giv'));
for (const c of ['\\p{RGI_Emoji}', '\\p{Basic_Emoji}', '[\\q{ab}]', '[a&&b]', '[(]', '[\\&]']) p(JSON.stringify(c), err(c, 'u'), '|', err(c, ''));
}

// u's escapes, whose errors v shares
{
for (const c of ['\\01', '[\\01]', '\\1', '[\\1]', '\\c', '[\\c]', '\\c1', '[\\c1]', '\\8', '[\\8]', '\\00', '\\0', '[\\-]', '\\-', '\\k', '\\k<a>',
  '(?<a>x)\\k<b>', '\\p', '\\p{', '\\p{Lu', '\\pL', '[\\p{Foo}]', '[\\pL]', '[\\d-a]', '[a-\\d]', '\\u{110000}'])
  p(JSON.stringify(c), err(c, 'u'), '|', err(c, 'v'));
}

// the flags getter, generic; the flags' getters
{
const g = Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags').get;
p(g.call({ global: 1, sticky: 1, unicodeSets: 1, hasIndices: 1 }), g.call({}), g.call(/x/dgimsvy), RegExp.prototype.flags, g.call({ multiline: 'yes', dotAll: 0 }));
try { g.call(1); } catch (e) { p(e.name, e.message); }
try { g.call(undefined); } catch (e) { p(e.name, e.message); }
const order = [];
g.call(new Proxy({}, { get(t, k) { order.push(String(k)); return true; } }));
p(order.join(','));
p(/a/v.unicodeSets, /a/u.unicodeSets, /a/.unicodeSets, RegExp.prototype.unicodeSets, /a/v.unicode, /a/v.flags, new RegExp('a', 'vg').flags);
const r = /x/g; Object.defineProperty(r, 'global', { value: false }); p(r.flags, String(r));
const us = Object.getOwnPropertyDescriptor(RegExp.prototype, 'unicodeSets').get;
try { us.call({}); } catch (e) { p(e.name, e.message); }
const gl = Object.getOwnPropertyDescriptor(RegExp.prototype, 'global').get;
try { gl.call([]); } catch (e) { p(e.name, e.message); }
}
