// Array elements' attributes: defineProperty, freeze, seal and preventExtensions over the
// index keys of arrays and of arguments objects, length against them, accessor elements,
// the descriptors, and the built-ins that write elements, each throwing or skipping as
// V8 does, under strict and sloppy code.
function t(name, f) {
  try { console.log(name, JSON.stringify(f())); }
  catch (e) { console.log(name, e.constructor.name + ': ' + e.message); }
}
function d(o, k) { return Object.getOwnPropertyDescriptor(o, k); }

// descriptors and attributes of single elements
t('desc plain', () => d([1, 2], 0));
t('desc length', () => d([1, 2], 'length'));
t('desc hole', () => d([, 1], 0));
t('desc undefined value', () => d([undefined], 0));
t('ro elem', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a[1] = 9; return [a, d(a, 1)]; });
t('ro elem strict', () => { 'use strict'; var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a[1] = 9; return a; });
t('ro elem compound', () => { 'use strict'; var a = [1, 2]; Object.defineProperty(a, 0, { writable: false }); a[0] += 1; return a; });
t('ro elem increment', () => { var a = [1, 2]; Object.defineProperty(a, 0, { writable: false }); a[0]++; return a; });
t('ro elem destructure', () => { 'use strict'; var a = [1, 2]; Object.defineProperty(a, 1, { writable: false }); [a[0], a[1]] = [5, 6]; return a; });
t('nonenum', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { enumerable: false }); return [Object.keys(a), JSON.stringify(a), d(a, 1), a.propertyIsEnumerable(1)]; });
t('for-in nonenum', () => { var a = [1, 2, 3]; Object.defineProperty(a, 0, { enumerable: false }); var ks = []; for (var k in a) ks.push(k); return ks; });
t('names order', () => { var a = [1, 2]; a.x = 1; Object.defineProperty(a, 5, { value: 6, enumerable: true }); Object.defineProperty(a, 3, { value: 4 }); return [Object.getOwnPropertyNames(a), Object.keys(a)]; });
t('nonconf delete', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { configurable: false }); return [delete a[1], a]; });
t('nonconf delete strict', () => { 'use strict'; var a = [1, 2, 3]; Object.defineProperty(a, 1, { configurable: false }); delete a[1]; return a; });
t('hole then define', () => { var a = [, 1]; Object.defineProperty(a, 0, { value: 'x' }); return [a, d(a, 0), Object.keys(a)]; });
t('redefine nonconf', () => { var a = [1]; Object.defineProperty(a, 0, { configurable: false }); Object.defineProperty(a, 0, { enumerable: false }); return a; });
t('redefine nonconf same', () => { var a = [1]; Object.defineProperty(a, 0, { configurable: false, writable: false }); Object.defineProperty(a, 0, { value: 1 }); return a; });
t('redefine nonconf diff', () => { var a = [1]; Object.defineProperty(a, 0, { configurable: false, writable: false }); Object.defineProperty(a, 0, { value: 2 }); return a; });
t('redefine back to plain', () => { var a = [1, 2]; Object.defineProperty(a, 0, { enumerable: false }); Object.defineProperty(a, 0, { enumerable: true }); return [d(a, 0), Object.keys(a), a]; });
t('defineProperties', () => { var a = []; Object.defineProperties(a, { 0: { value: 'a' }, 2: { value: 'c', enumerable: true, writable: true } }); return [a, a.length, Object.keys(a), d(a, 0)]; });
t('getOwnPropertyDescriptors', () => { var a = [1]; Object.defineProperty(a, 1, { value: 2, enumerable: false }); return Object.getOwnPropertyDescriptors(a); });
t('hasOwnProperty', () => { var a = [1, , 3]; Object.defineProperty(a, 4, { get() { return 5; } }); return [a.hasOwnProperty(0), a.hasOwnProperty(1), a.hasOwnProperty(4), a.hasOwnProperty('length'), Object.hasOwn(a, 2)]; });
t('propertyIsEnumerable', () => { var a = [1, 2]; Object.defineProperty(a, 1, { enumerable: false }); return [a.propertyIsEnumerable(0), a.propertyIsEnumerable(1), a.propertyIsEnumerable(2), a.propertyIsEnumerable('length')]; });
t('reflect set ro', () => { var a = [1]; Object.defineProperty(a, 0, { writable: false }); return [Reflect.set(a, 0, 2), Reflect.defineProperty(a, 0, { value: 3 }), a]; });
t('reflect delete nc', () => { var a = [1]; Object.defineProperty(a, 0, { configurable: false }); return [Reflect.deleteProperty(a, 0), a]; });

// length against the elements
t('nonconf length shrink', () => { var a = [1, 2, 3, 4]; Object.defineProperty(a, 1, { configurable: false }); a.length = 0; return [a, a.length]; });
t('nonconf length shrink strict', () => { 'use strict'; var a = [1, 2, 3, 4]; Object.defineProperty(a, 1, { configurable: false }); a.length = 0; return [a, a.length]; });
t('nonconf defprop length', () => { var a = [1, 2, 3, 4]; Object.defineProperty(a, 1, { configurable: false }); Object.defineProperty(a, 'length', { value: 0 }); return a; });
t('len shrink partial', () => { var a = [1, 2, 3, 4, 5]; Object.defineProperty(a, 2, { configurable: false }); a.length = 1; return [a.length, Object.keys(a)]; });
t('len shrink partial strict', () => { 'use strict'; var a = [1, 2, 3, 4, 5]; Object.defineProperty(a, 2, { configurable: false }); try { a.length = 1; } catch (e) { return [e.message, a.length]; } });
t('length ro', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a[2] = 3; a.length = 5; return [a, a.length, d(a, 'length')]; });
t('length ro strict', () => { 'use strict'; var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a[2] = 3; return a; });
t('length ro strict len', () => { 'use strict'; var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.length = 1; return a; });
t('length ro write in', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a[0] = 7; return a; });
t('length ro defprop beyond', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); Object.defineProperty(a, 5, { value: 1 }); return a; });
t('defprop length grow', () => { var a = [1]; Object.defineProperty(a, 'length', { value: 3 }); return [a, a.length]; });
t('defprop length bad', () => { var a = [1]; Object.defineProperty(a, 'length', { value: -1 }); return a; });
t('defprop length getter', () => { var a = [1]; Object.defineProperty(a, 'length', { get() { return 1; } }); return a; });
t('defprop length ro then rw', () => { var a = [1]; Object.defineProperty(a, 'length', { writable: false }); Object.defineProperty(a, 'length', { writable: true }); return a; });
t('define past length', () => { var a = [1]; Object.defineProperty(a, 3, { value: 4, enumerable: true, writable: true, configurable: true }); return [a, a.length]; });

// accessor elements
t('getter elem', () => { var a = [1, 2]; var n = 0; Object.defineProperty(a, 0, { get() { return ++n; }, configurable: true }); return [a[0], a[0], a.length, d(a, 0).get !== undefined, a]; });
t('setter elem', () => { var a = [1, 2]; var got; Object.defineProperty(a, 1, { set(v) { got = v; } }); a[1] = 5; return [got, a[1], a]; });
t('getter beyond', () => { var a = [1]; Object.defineProperty(a, 3, { get() { return 'x'; }, enumerable: true }); return [a.length, a[3], a, Object.keys(a)]; });
t('getter only strict set', () => { 'use strict'; var a = [1]; Object.defineProperty(a, 0, { get() { return 1; } }); a[0] = 2; return a; });
t('getter this', () => { var a = [1, 2]; Object.defineProperty(a, 0, { get() { return this.length; } }); return a[0]; });
t('in operator', () => { var a = []; Object.defineProperty(a, 2, { get() { return 1; } }); return [0 in a, 2 in a, a.length, a.hasOwnProperty(2)]; });
t('spread getter', () => { var a = [1, 2]; Object.defineProperty(a, 0, { get() { return 'g'; } }); return [...a]; });
t('for-of getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 'g'; } }); var r = []; for (var v of a) r.push(v); return r; });
t('destructure getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 'g'; } }); var [p, q] = a; return [p, q]; });
t('join getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 'g'; } }); return a.join('-'); });
t('indexOf getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 'g'; } }); return [a.indexOf('g'), a.lastIndexOf('g'), a.includes('g')]; });
t('slice getter', () => { var a = [1, 2, , 4]; Object.defineProperty(a, 1, { get() { return 'g'; } }); var s = a.slice(); return [s, 2 in s, s.length]; });
t('concat getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 'g'; } }); return a.concat([3]); });
t('map getter', () => { var a = [1, 2]; Object.defineProperty(a, 0, { get() { return 10; } }); return a.map(x => x * 2); });
t('reduce getter', () => { var a = [1, 2, 3]; Object.defineProperty(a, 2, { get() { return 30; } }); return a.reduce((s, x) => s + x); });
t('apply getter', () => { var a = [1, 2]; Object.defineProperty(a, 1, { get() { return 5; } }); return Math.max.apply(null, a); });

// freeze, seal, preventExtensions
t('freeze set', () => { var a = Object.freeze([1, 2]); a[0] = 9; a[5] = 1; return a; });
t('freeze set strict', () => { 'use strict'; var a = Object.freeze([1, 2]); a[0] = 9; return a; });
t('freeze set new strict', () => { 'use strict'; var a = Object.freeze([1, 2]); a[2] = 9; return a; });
t('freeze length', () => { var a = Object.freeze([1, 2]); a.length = 0; return [a, d(a, 'length'), d(a, 0)]; });
t('freeze length strict', () => { 'use strict'; var a = Object.freeze([1, 2]); a.length = 0; return a; });
t('freeze delete', () => { var a = Object.freeze([1, 2]); return [delete a[0], a]; });
t('freeze delete strict', () => { 'use strict'; var a = Object.freeze([1, 2]); delete a[0]; return a; });
t('freeze desc', () => { var a = Object.freeze([1, 2]); return [d(a, 0), Object.isFrozen(a), Object.isSealed(a), Object.isExtensible(a)]; });
t('freeze defprop', () => { var a = Object.freeze([1]); Object.defineProperty(a, 1, { value: 2 }); return a; });
t('freeze holes', () => { var a = Object.freeze([1, , 3]); a[1] = 2; return [a, 1 in a, Object.keys(a)]; });
t('seal desc', () => { var a = Object.seal([1, 2]); a[0] = 5; a[2] = 3; return [a, d(a, 0), d(a, 'length'), Object.isFrozen(a), Object.isSealed(a)]; });
t('seal length', () => { var a = Object.seal([1, 2]); a.length = 0; return a; });
t('seal length strict', () => { 'use strict'; var a = Object.seal([1, 2]); a.length = 0; return a; });
t('seal defprop ro', () => { var a = Object.seal([1, 2]); Object.defineProperty(a, 0, { writable: false }); return [d(a, 0), Object.isFrozen(a)]; });
t('seal all ro is frozen', () => { var a = Object.seal([1]); Object.defineProperty(a, 0, { writable: false }); return Object.isFrozen(a); });
t('pe set', () => { var a = Object.preventExtensions([1, 2]); a[2] = 3; a[0] = 7; return a; });
t('pe set strict', () => { 'use strict'; var a = Object.preventExtensions([1, 2]); a[2] = 3; return a; });
t('pe empty frozen', () => [Object.isFrozen(Object.preventExtensions([])), Object.isSealed(Object.preventExtensions([])), Object.isFrozen(Object.preventExtensions({}))]);
t('pe all nonconf', () => { var a = [1]; Object.defineProperty(a, 0, { configurable: false, writable: false }); Object.preventExtensions(a); return [Object.isSealed(a), Object.isFrozen(a)]; });
t('assign frozen', () => Object.assign(Object.freeze([1, 2]), [3]));
t('assign ro elem', () => { var a = [1, 2]; Object.defineProperty(a, 1, { writable: false }); return Object.assign(a, [7, 8]); });

// the built-ins that write elements
t('length ro push', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.push(3); return a; });
t('length ro pop', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.pop(); return a; });
t('length ro shift', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.shift(); return a; });
t('length ro unshift', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.unshift(0); return a; });
t('length ro splice', () => { var a = [1, 2]; Object.defineProperty(a, 'length', { writable: false }); a.splice(0, 1); return a; });
t('freeze push', () => { var a = Object.freeze([1, 2]); a.push(3); return a; });
t('freeze push none', () => { var a = Object.freeze([1]); return a.push(); });
t('freeze pop', () => { var a = Object.freeze([1, 2]); a.pop(); return a; });
t('freeze shift', () => { var a = Object.freeze([1, 2]); a.shift(); return a; });
t('freeze unshift', () => { var a = Object.freeze([1, 2]); a.unshift(0); return a; });
t('freeze splice', () => { var a = Object.freeze([1, 2]); a.splice(0, 1); return a; });
t('freeze splice0', () => { var a = Object.freeze([1, 2]); return a.splice(0, 0); });
t('freeze sort', () => { var a = Object.freeze([3, 1, 2]); a.sort(); return a; });
t('freeze sort1', () => { var a = Object.freeze([1]); a.sort(); return a; });
t('freeze reverse', () => { var a = Object.freeze([1, 2]); a.reverse(); return a; });
t('freeze fill', () => { var a = Object.freeze([1, 2]); a.fill(0); return a; });
t('freeze fill empty', () => { var a = Object.freeze([]); a.fill(0); return a; });
t('freeze copyWithin', () => { var a = Object.freeze([1, 2, 3]); a.copyWithin(0, 1); return a; });
t('freeze copyWithin nothing', () => { var a = Object.freeze([1, 2, 3]); return a.copyWithin(0, 3); });
t('seal push', () => { var a = Object.seal([1, 2]); a.push(3); return a; });
t('seal pop', () => { var a = Object.seal([1, 2]); a.pop(); return a; });
t('seal sort', () => { var a = Object.seal([3, 1, 2]); a.sort(); return a; });
t('seal reverse', () => { var a = Object.seal([1, 2]); a.reverse(); return a; });
t('seal fill', () => { var a = Object.seal([1, 2]); a.fill(0); return a; });
t('seal copyWithin', () => { var a = Object.seal([1, 2, 3]); a.copyWithin(0, 1); return a; });
t('seal splice same size', () => { var a = Object.seal([1, 2, 3]); return [a.splice(1, 1, 'x'), a]; });
t('pe push', () => { var a = Object.preventExtensions([1, 2]); a.push(3); return a; });
t('pe pop', () => { var a = Object.preventExtensions([1, 2]); a.pop(); return [a, Object.isFrozen(a), Object.isSealed(a)]; });
t('pe unshift', () => { var a = Object.preventExtensions([1, 2]); a.unshift(0); return a; });
t('ro reverse', () => { var a = [1, 2, 3]; Object.defineProperty(a, 0, { writable: false }); a.reverse(); return a; });
t('ro sort', () => { var a = [3, 1, 2]; Object.defineProperty(a, 0, { writable: false }); a.sort(); return a; });
t('ro fill', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a.fill(0); return a; });
t('ro splice', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a.splice(0, 1); return a; });
t('ro shift', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a.shift(); return a; });
t('ro unshift', () => { var a = [1, 2, 3]; Object.defineProperty(a, 1, { writable: false }); a.unshift(0); return a; });
t('ro pop', () => { var a = [1, 2, 3]; Object.defineProperty(a, 2, { writable: false }); a.pop(); return a; });
t('ro copyWithin', () => { var a = [1, 2, 3]; Object.defineProperty(a, 0, { writable: false }); a.copyWithin(0, 1); return a; });
t('ro push at', () => { var a = [1, 2, 3]; Object.defineProperty(a, 3, { value: 4, writable: false, configurable: true, enumerable: true }); a.push(5); return a; });
t('nc pop', () => { var a = [1, 2, 3]; Object.defineProperty(a, 2, { configurable: false }); a.pop(); return a; });
t('nc shift', () => { var a = [1, 2, 3]; Object.defineProperty(a, 2, { configurable: false }); a.shift(); return a; });
t('splice nc', () => { var a = [1, 2, 3]; Object.defineProperty(a, 2, { configurable: false }); a.splice(1, 2); return a; });
t('sort ro getter', () => { var a = [3, 1]; Object.defineProperty(a, 1, { get() { return 1; } }); a.sort(); return a; });
t('sort setter', () => { var a = [3, 1, 2], seen = []; Object.defineProperty(a, 0, { get() { return 3; }, set(v) { seen.push(v); } }); a.sort(); return [seen, a]; });
t('reverse holes', () => { var a = [1, , 3, , ]; Object.defineProperty(a, 0, { value: 1, writable: true, enumerable: false, configurable: true }); a.reverse(); return [a, Object.keys(a)]; });
t('error leaves done', () => { var a = [1, 2, 3, 4]; Object.defineProperty(a, 2, { writable: false }); try { a.fill(0); } catch (e) {} return a; });
t('from frozen', () => { var a = Object.freeze([1]); return Array.from.call(function () { return a; }, [1, 2]); });
t('from into frozen', () => { function C() { return Object.freeze([]); } return Array.from.call(C, [1, 2]); });
t('of frozen', () => { function C() { return Object.freeze([]); } return Array.of.call(C, 1, 2); });
t('of sealed length', () => { function C() { return Object.seal([0, 0]); } return Array.of.call(C, 1, 2); });
t('concat frozen', () => { var a = Object.freeze([1]); return a.concat([2]); });
t('slice frozen', () => Object.freeze([1, 2]).slice(0));
t('map frozen', () => Object.freeze([1, 2]).map(x => x + 1));

// the same methods over plain arrays and array-likes (the standard's steps)
t('copyWithin', () => [[1, 2, 3, 4, 5].copyWithin(0, 3), [1, 2, 3, 4, 5].copyWithin(1, 0, 3), [1, 2, 3, 4, 5].copyWithin(-2, 0), [1, , 3].copyWithin(0, 1)]);
t('copyWithin holes', () => { var a = [1, , 3].copyWithin(2, 1); return [a, 2 in a]; });
t('splice none', () => { var a = [1, 2, 3]; return [a.splice(), a]; });
t('splice holes', () => { var a = [1, , 3, 4]; var r = a.splice(0, 2); return [r, 1 in r, a]; });
t('like push', () => { var o = { length: 1, 0: 'a' }; Array.prototype.push.call(o, 'b', 'c'); return o; });
t('like pop', () => { var o = { length: 2, 0: 'a', 1: 'b' }; return [Array.prototype.pop.call(o), o]; });
t('like shift', () => { var o = { length: 3, 0: 'a', 2: 'c' }; return [Array.prototype.shift.call(o), o]; });
t('like unshift', () => { var o = { length: 2, 0: 'a', 1: 'b' }; return [Array.prototype.unshift.call(o, 'z'), o]; });
t('like splice', () => { var o = { length: 3, 0: 'a', 1: 'b', 2: 'c' }; return [Array.prototype.splice.call(o, 1, 1, 'x', 'y'), o]; });
t('like reverse', () => { var o = { length: 3, 0: 'a', 2: 'c' }; return Array.prototype.reverse.call(o); });
t('like sort', () => { var o = { length: 3, 0: 'c', 1: 'a', 2: 'b' }; return Array.prototype.sort.call(o); });
t('like fill', () => Array.prototype.fill.call({ length: 2 }, 7));
t('like copyWithin', () => Array.prototype.copyWithin.call({ length: 3, 0: 'a', 2: 'c' }, 0, 1));
t('like frozen push', () => { var o = Object.freeze({ length: 0 }); Array.prototype.push.call(o, 1); return o; });
t('like length setter', () => { var log = []; var o = { get length() { return 1; }, set length(v) { log.push(v); } }; Array.prototype.push.call(o, 'x'); return [log, o[1]]; });
t('like indexOf', () => [Array.prototype.indexOf.call({ length: 3 }, undefined), Array.prototype.includes.call({ length: 3 }, undefined)]);
t('like forEach', () => { var n = 0; Array.prototype.forEach.call({ length: 3, 1: 'x' }, () => n++); return n; });
t('like find', () => { var n = 0; Array.prototype.find.call({ length: 3, 1: 'x' }, () => { n++; }); return n; });
t('string slice', () => Array.prototype.slice.call('abc', 1));
t('string forEach', () => { var r = []; Array.prototype.forEach.call('ab', c => r.push(c)); return r; });
t('holes reduce', () => [[1, , 3].reduce((a, b) => a + b), [, , 5].reduce((a, b) => a + b), [1, , 3].reduceRight((a, b) => a + b)]);
t('holes reduce empty', () => [, , ].reduce((a, b) => a + b));
t('holes flat', () => [[1, , 3].flat(), [[1, , 2], , [3]].flat()]);
t('holes findLast', () => { var n = 0; [1, , 3].findLast(() => { n++; }); return n; });
t('holes lastIndexOf', () => [, 1].lastIndexOf(undefined));

// arguments objects
t('args ro', () => { 'use strict'; return (function () { Object.defineProperty(arguments, 0, { writable: false }); try { arguments[0] = 2; } catch (e) { return e.message; } return arguments[0]; })(1); });
t('args sloppy ro', () => (function () { Object.defineProperty(arguments, 0, { writable: false }); arguments[0] = 2; return [arguments[0], d(arguments, 0)]; })(1));
t('args freeze', () => (function () { Object.freeze(arguments); arguments[0] = 2; arguments[5] = 1; return [arguments[0], arguments.length, Object.isFrozen(arguments), d(arguments, 0), d(arguments, 'length')]; })(1));
t('args freeze strict', () => { 'use strict'; return (function () { Object.freeze(arguments); arguments[0] = 2; })(1); });
t('args seal', () => (function () { Object.seal(arguments); delete arguments[0]; return [arguments[0], d(arguments, 0)]; })(1));
t('args nonenum', () => (function () { Object.defineProperty(arguments, 0, { enumerable: false }); return Object.keys(arguments); })(1, 2));
t('args getter', () => (function () { Object.defineProperty(arguments, 0, { get() { return 'G'; } }); return [arguments[0], Array.prototype.slice.call(arguments)]; })(1, 2));
t('args desc', () => (function () { return [d(arguments, 0), d(arguments, 'length'), d(arguments, 'callee') && typeof d(arguments, 'callee').value]; })(1));
t('args write', () => (function () { arguments[0] = 2; arguments[3] = 4; return [arguments[0], arguments.length, arguments[3]]; })(1));
t('args pe', () => (function () { Object.preventExtensions(arguments); arguments[3] = 4; return [arguments[3], Object.isExtensible(arguments)]; })(1));
t('args in', () => (function () { return [0 in arguments, 1 in arguments, 'length' in arguments, arguments.hasOwnProperty(0), arguments.hasOwnProperty('length')]; })(1));
t('args delete', () => (function () { delete arguments[0]; return [0 in arguments, arguments.length, Object.keys(arguments)]; })(1, 2));
t('args keys', () => (function () { arguments.x = 1; Object.defineProperty(arguments, 5, { value: 6, enumerable: true }); return [Object.keys(arguments), Object.getOwnPropertyNames(arguments)]; })(1, 2));
t('args push', () => (function () { Array.prototype.push.call(arguments, 'c'); return [arguments.length, arguments[2], Array.from(arguments)]; })('a', 'b'));
t('args sort', () => (function () { return Array.prototype.sort.call(arguments); })(3, 1, 2));
t('args frozen push', () => (function () { Object.freeze(arguments); Array.prototype.push.call(arguments, 'c'); })('a'));
t('args spread', () => (function () { Object.defineProperty(arguments, 1, { get() { return 'g'; } }); return [...arguments]; })(1, 2));
t('args length ro', () => { 'use strict'; return (function () { Object.defineProperty(arguments, 'length', { writable: false }); arguments.length = 5; })(1); });
