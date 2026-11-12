// tests/js/props.js -- assignment and delete against a property's attributes, each line
// against V8's (props.expect, written by node props.js): a property defineProperty made
// read-only refuses a write, its own or one up the prototypes (a data property nearer
// than a setter hides it); delete refuses a property not configurable, and a sealed
// object's; sloppy code is told by the result, strict code by a TypeError; Reflect.set
// and Reflect.deleteProperty say false.

const p = (...a) => console.log(a.map(String).join(' '));
const t = (f) => { try { p(f()); } catch (e) { p(e.constructor.name + ': ' + e.message); } };

const ro = {};
Object.defineProperty(ro, 'x', { value: 1, writable: false, configurable: true, enumerable: true });
ro.x = 2;
p('read-only, sloppy:', ro.x);
t(() => { 'use strict'; ro.x = 3; return ro.x; });
p('Reflect.set:', Reflect.set(ro, 'x', 4), ro.x);
p('defineProperty still changes it:', Object.defineProperty(ro, 'x', { value: 5 }).x);

const child = Object.create(ro);
child.x = 6;
p('inherited read-only, sloppy:', child.x, Object.hasOwn(child, 'x'));
t(() => { 'use strict'; child.x = 7; return child.x; });
p('Reflect.set on the child:', Reflect.set(child, 'x', 8), child.x);

const base = { set y(v) { p('setter got', v); } };
const middle = Object.create(base);
middle.y = 'own on middle';
const leaf = Object.create(middle);
leaf.y = 'a data property nearer than the setter';
p('nearer data property:', leaf.y, Object.hasOwn(leaf, 'y'));

const fixed = {};
Object.defineProperty(fixed, 'z', { value: 1, writable: true, configurable: false });
p('delete not configurable, sloppy:', delete fixed.z, fixed.z);
t(() => { 'use strict'; return delete fixed.z; });
p('Reflect.deleteProperty:', Reflect.deleteProperty(fixed, 'z'));
p('a configurable one goes:', delete ro.x, 'x' in ro);

const sealed = Object.seal({ a: 1, b: 2 });
sealed.a = 10;
p('sealed, written:', sealed.a, 'deleted:', delete sealed.b, sealed.b);
t(() => { 'use strict'; delete sealed.b; });
const sealedArr = Object.seal([1, 2, 3]);
p('sealed array:', delete sealedArr[0], sealedArr[0]);
t(() => { 'use strict'; delete sealedArr[1]; });

const frozen = Object.freeze({ f: 1 });
frozen.f = 2;
p('frozen:', frozen.f, delete frozen.f, Reflect.set(frozen, 'f', 3));
t(() => { 'use strict'; delete frozen.f; });

const ext = Object.preventExtensions({ have: 1 });
t(() => { 'use strict'; ext.more = 2; });
const onlyGet = { get g() { return 1; } };
t(() => { 'use strict'; onlyGet.g = 2; });
t(() => { 'use strict'; 'abc'.x = 1; });
t(() => { 'use strict'; (5).x = 1; });
