// tests/js/regexplast.js -- lastIndex as the standard writes it, against node's V8
// (regexplast.expect): each Set(R, "lastIndex", v, true) of exec, test, match, replace and
// search, a frozen regex's or a read-only lastIndex refused with a TypeError (a global
// match or replace on one ended without a loop); the global match and replace that find
// every match before they build the result, their templates without and with $, and the
// searches by a literal prefix and by a run of one character.

const tr = f => { try { return JSON.stringify(f()); } catch (e) { return e.name + ': ' + e.message; } };
const p = (...a) => console.log(a.join(' '));
const x = (re, s) => tr(() => { const m = re.exec(s); return m && [m.index, ...m]; });

// a frozen regex: the writes its flags call for refused
{
const fz = () => Object.freeze(/a/g), fy = () => Object.freeze(/a/y), fn = () => Object.freeze(/a/);
p('replace', tr(() => 'aaa'.replace(fz(), 'b')), tr(() => 'xyz'.replace(fz(), 'b')), tr(() => 'aaa'.replace(fn(), 'b')), tr(() => 'aaa'.replace(fy(), 'b')));
p('match', tr(() => 'aaa'.match(fz())), tr(() => 'xyz'.match(fz())), tr(() => 'aaa'.match(fn())), tr(() => 'aaa'.match(fy())));
p('exec', tr(() => fz().exec('aaa')), tr(() => fz().exec('xyz')), tr(() => fn().exec('aaa')), tr(() => fy().exec('aaa')), tr(() => fy().exec('xyz')));
p('test', tr(() => fz().test('aaa')), tr(() => fn().test('aaa')), tr(() => fy().test('b')));
p('search', tr(() => 'baa'.search(fz())), tr(() => 'baa'.search(fn())), tr(() => 'bbb'.search(fy())));
p('split', tr(() => 'bab'.split(fz())), tr(() => 'bab'.split(fn())), tr(() => [...'aaa'.matchAll(fz())].length));
class R extends RegExp {}
p('subclass', tr(() => Object.freeze(new R('a', 'g')).exec('a')), tr(() => 'aa'.replace(Object.freeze(new R('a', 'g')), 'b')));
}

// lastIndex made read-only by defineProperty
{
const ro = (flags, v) => { const r = new RegExp('a', flags); Object.defineProperty(r, 'lastIndex', { writable: false, value: v }); return r; };
p(tr(() => 'aaa'.replace(ro('g', 0), 'b')), tr(() => 'aaa'.match(ro('g', 0))), tr(() => ro('g', 0).exec('aaa')), tr(() => ro('', 0).exec('aaa')), tr(() => ro('y', 0).test('a')));
const r = ro('', 0);
p(tr(() => 'aaa'.replace(r, 'b')), tr(() => 'aaa'.match(r)), tr(() => 'baa'.search(r)), r.lastIndex);
const u = ro('g', 2);
p(tr(() => u.exec('aaaa')), u.lastIndex, tr(() => u.test('aaaa')), tr(() => 'aaaa'.search(u)), u.lastIndex);
const v = /a/; v.lastIndex = 3; Object.freeze(v);
p(tr(() => 'baa'.search(v)), v.lastIndex);
const w = /a/; w.lastIndex = -0; Object.freeze(w);
p(tr(() => 'aaaa'.search(w)), Object.is(w.lastIndex, -0));
}

// search: lastIndex 0 for the search, the old value put back
{
const y = /a/g; let calls = 0; y.lastIndex = { valueOf() { calls++; return 0; } };
p(tr(() => 'baa'.search(y)), calls, typeof y.lastIndex);
const z = /a/g; z.lastIndex = 1;
p(tr(() => 'aaa'.search(z)), z.lastIndex);
const z2 = /a/y; z2.lastIndex = 2;
p(tr(() => 'aab'.search(z2)), z2.lastIndex, tr(() => 'baa'.search(z2)), z2.lastIndex);
const z3 = /a/g; z3.lastIndex = 9;
p(tr(() => 'xyz'.search(z3)), z3.lastIndex, tr(() => 'xyz'.replace(z3, 'q')), z3.lastIndex, tr(() => 'xaz'.match(z3)), z3.lastIndex);
}

// a global match and replace: every match found first, lastIndex left 0
{
const r1 = /a/g; r1.lastIndex = 5;
p(tr(() => 'banana'.replace(r1, 'o')), r1.lastIndex, tr(() => 'banana'.match(r1)), r1.lastIndex);
p(tr(() => 'abc'.replace(/x*/g, '-')), tr(() => '\u{1F600}x'.replace(/(?:)/gu, '-')), tr(() => '\u{1F600}x'.replace(/(?:)/gv, '-')));
p(tr(() => 'abc'.match(/(?:)/g)), tr(() => 'a\u{1F600}'.match(/(?:)/gu).length), tr(() => 'a\u{1F600}'.match(/(?:)/g).length), tr(() => 'xyz'.match(/q/g)));
const r2 = /a/gy; r2.lastIndex = 3;
p(tr(() => 'aaba'.replace(r2, 'X')), r2.lastIndex, tr(() => 'aaba'.match(/a/gy)), tr(() => 'abcxabc'.replace(/abc/gy, '-')));
const r3 = /a/y; r3.lastIndex = 3;
p(tr(() => 'aaba'.replace(r3, 'X')), r3.lastIndex);
const r4 = /a/y; r4.lastIndex = 2;
p(tr(() => 'aaba'.replace(r4, 'X')), r4.lastIndex);
const r8 = /b/; r8.lastIndex = 7;
p(tr(() => 'abcb'.replace(r8, 'B')), r8.lastIndex);
let hits = 0; const r9 = /a/g;
p(tr(() => 'aaa'.replace(r9, () => { hits++; r9.lastIndex = 0; return 'b'; })), hits, r9.lastIndex);
p(tr(() => 'a.b.c'.replace(/\./g, '$$')), tr(() => 'a.b.c'.replace(/\./g, '[$&]')), tr(() => 'abc'.replace(/b/g, "$`$'")), tr(() => 'x'.repeat(5).replace(/x/g, '$')));
p(tr(() => 'abc'.replace(/(b)/g, '$1$2$0')), tr(() => 'abc'.replace(/(?<n>b)/g, '<$<n>>')), tr(() => 'abc'.replace(/(?<n>b)/g, '$<m>')), tr(() => 'abc'.replace(/b/g, '$<n>')));
p(tr(() => 'aaa'.replace(/a/g, '')), tr(() => 'aaa'.replace(/a*/g, 'b')), tr(() => 'aaa'.replace(/a*?/g, 'b')), tr(() => ''.replace(/x*/g, '-')), tr(() => ''.replace(/x/g, '-')));
p(tr(() => 'abab'.replace(/(a)|b/g, '[$1]')), tr(() => 'abab'.replace(/(a)|b/g, (m, g) => String(g))), tr(() => 'abcb'.replace(/b/g, (m, i) => m.toUpperCase() + i)));
p(tr(() => 'aXbXc'.replaceAll(/x/gi, '_')), tr(() => 'a b c'.replace(/ /g, '_')), tr(() => 'x-y-z'.replace(/-/g, '')), tr(() => 'aaaa'.replace(/a/g, 'bb')));
p(tr(() => 'x'.repeat(1000).replace(/x/g, 'yy').length), tr(() => 'a1b22c333'.match(/\d+/g)), tr(() => 'aaa bbb'.match(/(a+)|(b+)/g)));
p(tr(() => 'ab'.repeat(6000).replace(/b/g, 'x'.repeat(100000))));
}

// backtracking: the slots put back by the choices below them
{
p(x(/(a)|b/, 'b'), x(/(a)?b/, 'b'), x(/(?:(a)|b)+/, 'ab'), x(/(a)(?=x)|(a)c/, 'ac'), x(/(?!(a)b)a(.)/, 'ac'));
p(x(/((a)|b)*c/, 'abac'), x(/(?:(a)|(b))*/, 'aba'), x(/(a*)*b/, 'aab'), x(/(x)(y)?z|xy(q)/, 'xyq'), x(/^(?:(a)|ab)c/, 'abc'));
p(x(/(a){2}b|(a)c/, 'aac'), x(/(?:(ab)){2}|abc/, 'abc'), x(/((?:ab){2})|(a)/, 'aba'), x(/(?<=(a))b|(c)/, 'cb'), x(/\1(a)/, 'aa'));
}

// runs of one character, and the searches by a literal prefix
{
p(x(/a+/, 'baaab'), x(/a{2,3}/, 'aaaaa'), x(/a{2,3}b/, 'aaaab'), x(/a{3,}/, 'aab'), x(/a*b/, 'aaac'), x(/x*/, ''));
p(x(/\d+/, 'ab123c'), x(/\d{2}/, '1a23'), x(/[^\d]+/, '12ab34'), x(/[a-cx-z]+/, 'qqabyzq'), x(/[\w.+-]+@/, 'a.b+c@d'));
p(x(/.+/, 'ab\ncd'), x(/.*x/, 'abxcdx\nx'), x(/[^]*c/, 'ab\nc'), x(/\s+/, 'a \t b'), x(/\S+/, '  ab  '), x(/[\0-\x1f]+/, 'a\x01\x02b'));
p(x(/\d+5/, '12345678'), x(/(\d+)(\d)/, '1234'), x(/a+a+a+b/, 'aaaab'), x(/[a-z]+ing/, 'singing'), x(/[Ā-ſ]+/, 'aāſb'));
p(x(/[a-z]+/i, 'ABC'), x(/[^a-z]+/i, '12Ab'), x(/a+/i, 'AaB'), x(/ſ+/i, 'sSſ'), x(/k+/i, 'kKK'), x(/.+/u, '\u{1F600}x'));
p(x(/quick brown/i, 'The QUICK Brown fox'), x(/ab/, 'xxaab'), x(/abab/, 'abaabab'), x(/aaab/, 'aaaaab'), x(/xyz/i, 'XYz'), x(/oo/i, 'fOO'));
p(x(/aš/, 'aš aa'), x(/ša/, 'xaša'), x(/bŢ/, 'bbŢ'), x(/ba/, 'bšba'), x(/ss/i, 'SſSS'), x(/foo\d/, 'foo food foo1'));
p(tr(() => ('a'.repeat(2000) + 'b').match(/a*b/)[0].length), tr(() => 'x'.repeat(3000).match(/x+/)[0].length), tr(() => 'a  b   c'.split(/ +/)));
}
