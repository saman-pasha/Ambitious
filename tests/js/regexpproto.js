// The RegExp protocol as V8 runs it: an exec a subclass or an object has (RegExpExec), String's methods calling the
// pattern's own Symbol methods, the species' splitter and matcher, matchAll's iterator stepped as it is read, the
// properties each method reads (global and unicode, not flags, for match and replace), IsRegExp, and V8's words for
// each refusal.
function t(f) {
  try {
    const r = f();
    console.log(typeof r === 'object' && r !== null ? JSON.stringify(r) : String(r));
  } catch (e) { console.log(e.constructor.name + ': ' + e.message); }
}

// an exec of a subclass's own, called by every method
class R extends RegExp {
  exec(s) { console.log('exec', this.lastIndex, JSON.stringify(s)); return super.exec(s); }
}
t(() => new R('b').test('abc'));
t(() => 'abcb'.match(new R('b')));
t(() => 'abcb'.match(new R('b', 'g')));
t(() => 'abcb'.replace(new R('b'), 'X'));
t(() => 'abcb'.replace(new R('b', 'g'), 'X'));
t(() => 'abcb'.replaceAll(new R('b', 'g'), '[$&]'));
t(() => 'abcb'.search(new R('c')));
t(() => 'a-b'.split(new R('-')).join('|'));
t(() => 'a1b2c3'.split(new R('(\\d)'), 4));
t(() => ''.split(new R('x')));
t(() => 'xyz'.split(new R(''), 2));
{
  const it = 'a1b2'.matchAll(new R('\\d', 'g'));
  console.log('made');
  for (const m of it) console.log('got', m[0], m.index);
}
// exec's result an object that is not an array
class Fake extends RegExp {
  exec(s) { return this.lastIndex++ < 2 ? { 0: 'zz', 1: 'c1', index: 1, length: 2, groups: { n: 'named' } } : null; }
}
t(() => 'abcdef'.replace(new Fake('x', 'g'), '<$1|$<n>|$&|$`|$\'|$$|$2|$0>'));
t(() => 'abcdef'.replace(new Fake('x'), (...a) => JSON.stringify(a)));
t(() => 'abcdef'.match(new Fake('x', 'g')));
// an exec that returns something else
class Bad extends RegExp { exec() { return 1; } }
t(() => new Bad('a').test('a'));
t(() => 'a'.match(new Bad('a')));
t(() => 'a-b'.split(new Bad('-')));
// exec not callable: the matcher itself
class N extends RegExp {}
N.prototype.exec = 5;
t(() => new N('a').test('a'));
t(() => 'xax'.replace(new N('a', 'g'), '_'));
// an exec on the instance
const r2 = /x/g;
r2.exec = function (s) { console.log('own exec', this.lastIndex); return RegExp.prototype.exec.call(this, s); };
t(() => 'xax'.replace(r2, '_'));
// a plain object as the receiver of RegExp.prototype's methods
const fake = { exec(s) { console.log('fake exec', s); return null; }, lastIndex: 0, flags: '', global: false };
t(() => RegExp.prototype.test.call(fake, 'zz'));
t(() => RegExp.prototype[Symbol.match].call(fake, 'zz'));
t(() => RegExp.prototype[Symbol.search].call(fake, 'zz'));
t(() => RegExp.prototype[Symbol.replace].call(fake, 'zz', 'y'));

// the properties each method reads, in order
function spy(extra) {
  let n = 0;
  const log = [];
  const o = {
    get flags() { log.push('flags'); return extra.flags; },
    get global() { log.push('global'); return extra.global; },
    get unicode() { log.push('unicode'); return extra.unicode; },
    get unicodeSets() { log.push('unicodeSets'); return false; },
    get lastIndex() { log.push('get lastIndex'); return this._li || 0; },
    set lastIndex(v) { log.push('set lastIndex ' + v); this._li = v; },
    get exec() { log.push('get exec'); return function () { log.push('exec'); n++; return n <= extra.matches ? { 0: extra.m, index: n, length: 1 } : null; }; },
    get constructor() { log.push('get constructor'); return undefined; },
  };
  return { o, log };
}
for (const [name, call, extra] of [
  ['test', (o) => RegExp.prototype.test.call(o, 'abc'), { matches: 1, m: 'x' }],
  ['match', (o) => RegExp.prototype[Symbol.match].call(o, 'abc'), { flags: '', global: false, matches: 1, m: 'x' }],
  ['match global', (o) => RegExp.prototype[Symbol.match].call(o, 'abc'), { flags: 'g', global: true, matches: 2, m: 'x' }],
  ['match flags g, global false', (o) => RegExp.prototype[Symbol.match].call(o, 'abc'), { flags: 'g', global: false, matches: 2, m: 'x' }],
  ['match global, empty', (o) => RegExp.prototype[Symbol.match].call(o, 'abc'), { global: true, unicode: true, matches: 2, m: '' }],
  ['replace', (o) => RegExp.prototype[Symbol.replace].call(o, 'abc', '_'), { flags: 'g', global: true, matches: 2, m: 'x' }],
  ['replace flags g, global false', (o) => RegExp.prototype[Symbol.replace].call(o, 'abc', '_'), { flags: 'g', global: false, matches: 2, m: 'x' }],
  ['search', (o) => RegExp.prototype[Symbol.search].call(o, 'abc'), { matches: 1, m: 'x' }],
  ['split', (o) => RegExp.prototype[Symbol.split].call(o, 'abc'), { flags: '', matches: 0 }],
  ['matchAll', (o) => [...RegExp.prototype[Symbol.matchAll].call(o, 'abc')].length, { flags: 'g', matches: 2 }],
]) {
  const { o, log } = spy(extra);
  let r;
  try { r = call(o); } catch (e) { r = e.constructor.name + ': ' + e.message; }
  console.log(name, '=>', JSON.stringify(r));
  console.log('  ', log.join(', '));
}

// String's methods call the pattern's own Symbol method
const sym = { [Symbol.match](s) { return 'm:' + s; }, [Symbol.replace](s, r) { return 'r:' + s + r; },
  [Symbol.search](s) { return 's:' + s; }, [Symbol.split](s, l) { return 'p:' + s + l; } };
t(() => 'abc'.match(sym)); t(() => 'abc'.replace(sym, 1)); t(() => 'abc'.search(sym)); t(() => 'abc'.split(sym, 2));
t(() => 'abc'.replaceAll(sym, 2));
t(() => 'abc'.matchAll(sym));
t(() => 'abc'.matchAll({ [Symbol.matchAll](s) { return 'a:' + s; } }));
t(() => 'abc'.replaceAll({ [Symbol.replace](s, r) { return 'ra:' + s + r; } }, 3));
t(() => String.prototype.match.call(12345, { [Symbol.match](s) { return typeof s + ':' + s; } }));
class M extends RegExp { [Symbol.match](s) { return ['sub', s]; } [Symbol.replace](s, r) { return 'subrep'; } }
t(() => 'abc'.match(new M('b'))); t(() => 'abc'.replace(new M('b'), 'x')); t(() => 'abc'.replaceAll(new M('b', 'g'), 'x'));
const own = /b/g; own[Symbol.split] = function (s) { return ['own split', s]; };
t(() => 'abc'.split(own));
const ex = /b/; ex.exec = function () { return { 0: 'zz', index: 0, length: 1 }; };
t(() => 'abc'.match(ex)); t(() => 'abc'.replace(ex, '_')); t(() => 'abc'.search(ex));
t(() => 'a.b'.match({ toString() { return '\\.'; } })[0]);
t(() => 'abc'.replace(null, 'x')); t(() => 'abc'.split(undefined).length);
t(() => 'a+b'.replace({ [Symbol.replace]: undefined, toString() { return '+'; } }, '-'));
t(() => 'a+b'.split({ [Symbol.split]: null, toString() { return '+'; } }).join('|'));
// a Symbol method not callable: each method's words
t(() => 'abc'.match({ [Symbol.match]: 5 }));
t(() => 'abc'.replace({ [Symbol.replace]: 'x' }, ''));
t(() => 'abc'.search({ [Symbol.search]: {} }));
t(() => 'abc'.split({ [Symbol.split]: true }));
t(() => 'abc'.matchAll({ [Symbol.matchAll]: 1 }));
t(() => 'abc'.replaceAll({ [Symbol.replace]: 2 }, ''));
// IsRegExp: includes, startsWith and endsWith refuse a pattern; matchAll and replaceAll read its flags
t(() => 'abc'.startsWith(/a/)); t(() => 'abc'.endsWith(/a/)); t(() => 'abc'.includes(/a/));
t(() => 'abc'.includes({ [Symbol.match]: true }));
t(() => 'abc'.includes({ [Symbol.match]: 0, toString() { return 'b'; } }));
const nre = /x/; nre[Symbol.match] = false;
t(() => '/x/'.startsWith(nre)); t(() => '/x/'.includes(nre));
t(() => 'a'.matchAll(/a/));
const gflags = /a/; Object.defineProperty(gflags, 'flags', { value: 'g' });
t(() => [...'aa'.matchAll(gflags)].length);
t(() => 'abc'.replaceAll({ [Symbol.match]: true, flags: 'g', [Symbol.replace]() { return 'ok'; } }, ''));
t(() => 'abc'.replaceAll({ [Symbol.match]: true, flags: undefined }, ''));
t(() => 'abc'.matchAll({ [Symbol.match]: true, flags: 'i' }));
t(() => 'abc'.matchAll({ [Symbol.match]: true, flags: null }));
t(() => 'aXbX'.replaceAll(/x/gi, '-'));
t(() => 'aXbX'.replaceAll(/x/i, '-'));

// the species: split's splitter with y, matchAll's matcher with the flags
class S extends RegExp { constructor(p, f) { console.log('S ctor', String(p), f); super(p, f); } }
t(() => [...'abab'.matchAll(new S('b', 'g'))].map((m) => m.index).join());
t(() => 'a-b-c'.split(new S('-'), 2).join('|'));
class S2 extends RegExp { static get [Symbol.species]() { return function (rx, f) { console.log('species', rx.source, f); return /b/y; }; } }
t(() => 'abc'.split(new S2('-')).join('|'));
t(() => RegExp.prototype[Symbol.split].call({ constructor: { [Symbol.species]: function () { return { exec() { return null; }, lastIndex: 0 }; } }, flags: '' }, 'abc'));
t(() => RegExp.prototype[Symbol.split].call({ constructor: 5 }, 'abc'));
t(() => RegExp.prototype[Symbol.split].call({ constructor: { [Symbol.species]: 5 } }, 'abc'));

// matchAll's iterator: stepped as read, alive after a throw, a copy of a plain RegExp's state
{
  let n = 0;
  class T extends RegExp { exec(s) { n++; if (n === 1) throw new Error('boom'); return n < 4 ? { 0: 'a', index: n, length: 1 } : null; } }
  const it = 'aaa'.matchAll(new T('a', 'g'));
  try { it.next(); } catch (e) { console.log('threw', e.message); }
  console.log(JSON.stringify(it.next()), n);
  const re = /a/g; const it2 = 'aaaa'.matchAll(re); it2.next(); re.lastIndex = 3; console.log(it2.next().value.index, re.lastIndex);
  const it3 = RegExp.prototype[Symbol.matchAll].call(/a/, 'aa'); console.log(it3.next().value.index, it3.next().done, it3.next().done);
  console.log(Object.prototype.toString.call(it3), typeof it3[Symbol.iterator]);
  const it4 = 'a1b22c333'.matchAll(/\d+/g); console.log(it4.next().value[0]); console.log([...it4].map((m) => m[0]).join());
  const re5 = /a/g; re5.lastIndex = 2; console.log([...'aaaa'.matchAll(re5)].map((m) => m.index).join(), re5.lastIndex);
  console.log([...'a\u{1F600}b'.matchAll(/(?:)/gu)].map((m) => m.index).join());
  console.log([...'a\u{1F600}b'.matchAll(/(?:)/g)].map((m) => m.index).join());
}

// the RegExp constructor and IsRegExp
t(() => RegExp({ [Symbol.match]: true, source: 'ab', flags: 'g' }).toString());
t(() => new RegExp({ [Symbol.match]: true, source: 'ab', flags: 'g' }, 'i').toString());
const rr = /x/; t(() => RegExp(rr) === rr); t(() => new RegExp(rr) === rr);
const nm = /x/; nm[Symbol.match] = false; t(() => RegExp(nm) === nm);
const fk = { [Symbol.match]: true, constructor: RegExp, source: 'q' }; t(() => RegExp(fk) === fk);
t(() => RegExp(fk, 'g').toString());

// split's limit is ToUint32
t(() => 'a,b,c'.split(',', -1).length); t(() => 'a,b,c'.split(/,/, -1).length);
t(() => 'a,b,c'.split(',', 4294967297).length); t(() => 'a,b,c'.split(/,/, 2.7).length);
t(() => 'a,b,c'.split(',', 0).length);

// a RegExp.prototype changed: every RegExp the generic way
const savedExec = RegExp.prototype.exec;
RegExp.prototype.exec = function (s) { console.log('patched exec', this.source); return savedExec.call(this, s); };
t(() => /q/.test('aqa'));
t(() => 'aqaq'.replace(/q/g, '!'));
t(() => 'aqaq'.split(/q/).join('|'));
RegExp.prototype.exec = savedExec;
t(() => 'aqaq'.replace(/q/g, '?'));
const savedGlobal = Object.getOwnPropertyDescriptor(RegExp.prototype, 'global');
Object.defineProperty(RegExp.prototype, 'global', { get() { console.log('global read'); return savedGlobal.get.call(this); }, configurable: true });
t(() => 'aqaq'.replace(/q/g, '#'));
t(() => /q/g.flags);
Object.defineProperty(RegExp.prototype, 'global', savedGlobal);
t(() => 'aqaq'.replace(/q/g, '%'));

// lastIndex: writable, neither enumerable nor configurable
t(() => Object.getOwnPropertyDescriptor(/x/, 'lastIndex'));
t(() => { const r = /x/; return delete r.lastIndex; });
t(() => { Object.defineProperty(/x/, 'lastIndex', { get() { return 1; } }); return 'redefined'; });
t(() => { 'use strict'; const r = /x/; delete r.lastIndex; });
t(() => { const r = /x/g; Object.defineProperty(r, 'lastIndex', { writable: false }); return r.test('x'); });
t(() => { const r = /x/; Object.defineProperty(r, 'lastIndex', { writable: false }); return [r.test('x'), 'xx'.replace(r, '-')]; });
