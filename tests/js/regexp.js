// tests/js/regexp.js -- regular expressions and String's methods over them, each line against
// V8's (regexp.expect): exec, test, match, matchAll, replace, split and search; groups named and
// not, back references, lookaround, the flags, classes and \p{...}, Annex B's leniencies; the
// case conversions and orbits Unicode's tables give.

// the common uses
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
p(/a+b/.test('xaaab'), /^a/.test('ba'), /b$/m.test('ab\nc'));
p('2024-01-15'.match(/(\d+)-(\d+)-(\d+)/).slice(1));
const m = /(?<y>\d{4})-(?<m>\d\d)/.exec('on 2024-03 ok'); p(m.index, m.groups.y, m.groups.m, m[0]);
p('a,b;;c'.split(/[,;]/), 'abc'.split(/(?:)/), 'a1b2c3'.split(/(\d)/));
p('hello world'.replace(/o/g, '0'), 'John Smith'.replace(/(\w+)\s(\w+)/, '$2, $1'));
p('aaa'.replace(/a*?/g, 'X'), 'abc'.replace(/(?<first>a)/, '[$<first>]'));
p('x-y-z'.replace(/-/g, (m, off) => off), 'Price: $5'.replace(/\$(\d)/, '$$$1'));
p([...'a1b22c333'.matchAll(/\d+/g)].map(m => m[0] + '@' + m.index));
p('Straße'.toUpperCase(), 'İ'.toLowerCase().length, 'ΟΔΟΣ'.toLowerCase(), 'ǅ'.toLowerCase());
p(/[a-z]+/i.exec('123ABC')[0], /ß/i.test('ẞ'), /ß/iu.test('ẞ'), /K/i.test('k'), /K/iu.test('k'));
p(/(?<=\$)\d+/.exec('cost $42')[0], /(?<!\$)\b\d+/.exec('$42 17')[0], /(?=a)a/.test('a'), /a(?!b)/.exec('abac').index);
p(/(a)|(b)/.exec('b'), /(a)?b/.exec('b'));
p(/\p{Lu}+/u.exec('abcDEFg')[0], /\p{Script=Greek}+/u.exec('abc αβγ')[0], /\P{L}/u.exec('ab1')[0]);
p(/^.$/u.test('😀'), /^.$/.test('😀'), '😀x'.split(/(?:)/u).length, /\u{1F600}/u.test('😀'));
p(/(\d+)\s\1/.test('12 12'), /(\d+)\s\1/.test('12 13'), /(?<n>x)\k<n>/.test('xx'));
const re = /o/g; p(re.exec('foo').index, re.lastIndex, re.exec('foo').index, re.lastIndex, re.exec('foo'), re.lastIndex);
p(String(/a\/b/g), /x/gimsuy.flags, new RegExp('a/b').source, new RegExp('').source, RegExp('\n').source);
p('aBc'.search(/b/i), 'abc'.search(/z/), /a{2,3}/.exec('aaaa')[0], /a{2,3}?/.exec('aaaa')[0]);
try { new RegExp('('); } catch (e) { p(e.name, e.message); }
try { new RegExp('a', 'gg'); } catch (e) { p(e.name, e.message); }
p(/(a+)+b/.test('aaaaaaaaaaaaaaaaaaaab'), /x*/.exec('')[0] === '', 'abc'.replace(/(?:)/g, '-'));
p(/[\d-x]/.test('-'), /[^\s\S]/.test('a'), /\cJ/.test('\n'), /[\b]/.test('\b'), /\0/.test('\0'));
p('aaa'.match(/a/g), 'abc'.match(/z/g), 'test'.match(/t/).length);
p(/(?:ab)+/.exec('ababab')[0], /(a|ab)(c|bcd)(d*)/.exec('abcd'), /((a)|b)+/.exec('ab'));
p('The Quick Brown'.replace(/\b\w/g, c => c.toLowerCase()), 'x'.padStart(3, 'ab'));
const d = /(?<w>b)/d.exec('abc'); p(d.indices[0], d.indices[1], d.indices.groups.w);
p(/a.c/s.test('a\nc'), /a.c/.test('a\nc'), /^b/m.exec('a\nb').index);
p('𝒳𝒴'.replace(/(?:)/gu, '|'), 'aa'.replace(/a/y, 'b'), /\w+@\w+\.com/.test('me@site.com'));
}

// the edges: substitution templates, empty matches, nested quantifiers, sticky, Annex B
{
const p = (...a) => console.log(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
p(/à/i.test('À'), /ÿ/i.test('Ÿ'), /[à-ÿ]/i.test('Ö'), /\w/i.test('ſ'), /\w/iu.test('ſ'), /[^\W]/iu.test('K'));
p('ab'.replace(/(a)(b)?/, '[$2|$1|$3|$10|$01]'), 'abc'.replace('b', '$&$&'), 'abc'.replace(/b/, "$'$`"));
p('aaa'.lastIndexOf('a'), 'x'.replace(/x/, '$<n>'), 'x'.replace(/(?<n>x)/, '$<m>|$<n'));
p(/(?:a|b)*c/.exec('ababc')[0], /^(?:(a)|b)*$/.exec('ab'), /(z)((a+)?(b+)?(c))*/.exec('zaacbbbcac'));
p(/[a-c]{2}/g[Symbol.replace]('abcabc', 'X'), RegExp.prototype[Symbol.split].call(/,/, 'a,b'));
p(/\bfoo\b/.test('a foo b'), /\Bfoo/.test('afoo'), /^\s*$/.test(' \t\n'), /\S+/.exec('  hi  ')[0]);
p('one two  three'.split(/\s+/), 'one two'.split(/\s/, 1), 'abc'.split(/b/, 0), ''.split(/x/), ''.split(/(?:)/));
p(/(\w)\1/.exec('abcdde')[0], /(?<a>.)(?<b>.)\k<b>\k<a>/.test('abba'), /\k<x>(?<x>a)/.test('a'));
p(Object.keys(/a/), /a/.hasOwnProperty('lastIndex'), /a/.global, RegExp.prototype.flags, RegExp.prototype.source);
p(/a/ instanceof RegExp, typeof /a/, Object.prototype.toString.call(/a/), RegExp(/x/g) .flags, new RegExp(/x/g, 'i').flags);
const r = /a/y; p(r.test('ba'), r.lastIndex, (r.lastIndex = 1, r.test('ba')), r.lastIndex);
p('aaaa'.replace(/aa/g, 'b'), 'aaaa'.replaceAll(/a/g, 'b'), 'a.b.c'.replaceAll('.', '-'));
try { 'a'.replaceAll(/a/, 'b'); } catch (e) { p(e.name); }
try { 'a'.matchAll(/a/); } catch (e) { p(e.name); }
p(/[\u{1F600}-\u{1F64F}]/u.test('😊'), /^[^x]$/u.test('😊'), /\p{Emoji}/u.test('😊'), /\p{Script_Extensions=Latin}/u.test('a'));
p('x'.match(/(?:)/g), 'abc'.match(/(?=b)/g), /$/.exec('ab').index, /(?:)/g[Symbol.match]('ab'));
p(/a|ab/.exec('abc')[0], /((a)|(ab))((c)|(bc))/.exec('abc'));
p(/(a*)*/.exec('b'), /(a*)+/.exec('b'), /(a|b)*?c/.exec('abc'), /(?:a?)+?b/.exec('aab')[0]);
p('Mississippi'.match(/(ss)/g), 'Mississippi'.match(/i(?=ss)/g).length, /s+?/.exec('ss')[0]);
let big = 'a'.repeat(50000) + 'b'; p(/a*b/.test(big), /(a|b)*c/.test(big.slice(0, 2000)), big.replace(/a/g, '').length);
p('%41%42'.replace(/%([0-9A-F]{2})/g, (m, h) => String.fromCharCode(parseInt(h, 16))));
p('camelCaseString'.replace(/([A-Z])/g, '_$1').toLowerCase(), '  trim me  '.replace(/^\s+|\s+$/g, ''));
p(/^(\d{3})-(\d{4})$/.exec('555-1234').slice(1), /\d{3,}/.exec('12 1234')[0], /x{0}/.exec('x')[0] === '');
p(JSON.stringify('a\u2028b'.split(/\u2028/)), /[\-]/.test('-'), /[a\-z]/.test('-'), /[\]]/.test(']'), /]/.test(']'), /{/.test('{'), /a{,2}/.test('a{,2}'));
p(/\8/.test('8'), /\1(a)/.exec('a')[0], /(a)\2/.test('a\u0002'), /\x4/.test('x4'), /\u12/.test('u12'), /\c/.test('\\c'));
}
