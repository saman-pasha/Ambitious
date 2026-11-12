// tests/js/strict.js -- assignments an object refuses, each line against V8's
// (strict.expect): a frozen object's (even through a warm property cache), a new
// property on an object not extensible, an accessor without a setter, a primitive's
// property; sloppy code passes over them, strict code throws a TypeError.

const p = (...a) => console.log(a.map(String).join(' '));
function setx(o, v) { o.x = v; }
const o = { x: 1 };
for (let i = 0; i < 5; i++) setx(o, i);
Object.freeze(o);
setx(o, 99);
p('frozen through a warm cache', o.x);
const attempt = (name, f) => { try { f(); p(name, 'no throw'); } catch (e) { p(name, e.constructor.name); } };
attempt('strict frozen', function () { 'use strict'; o.x = 5; });
attempt('sloppy frozen', function () { o.x = 5; });
const ne = Object.preventExtensions({ a: 1 });
attempt('strict new on not extensible', function () { 'use strict'; ne.b = 5; });
attempt('strict existing on not extensible', function () { 'use strict'; ne.a = 2; });
p('existing changed', ne.a, 'b' in ne);
const g = { get only() { return 1; } };
attempt('strict getter only', function () { 'use strict'; g.only = 5; });
attempt('sloppy getter only', function () { g.only = 5; });
class K { get v() { return 'v'; } }
attempt('strict inherited getter', function () { 'use strict'; new K().v = 1; });
attempt('strict primitive', function () { 'use strict'; 'str'.foo = 5; });
attempt('sloppy primitive', function () { (5).foo = 5; });
const fa = Object.freeze([1, 2, 3]);
attempt('strict frozen array element', function () { 'use strict'; fa[0] = 9; });
attempt('strict frozen array computed', function () { 'use strict'; const k = 'length'; fa[k] = 0; });
p('array kept', fa.join());
attempt('strict computed frozen', function () { 'use strict'; const k = 'x'; o[k] = 7; });
p('frozen kept', o.x);
