// hashbang comments: at a script's, a module's or an eval's very start alone
const cases = ['#!x\n1', '#!/usr/bin/env node\n"second line"', '#!x', ' #!x\n1', '\n#!x\n1', '1\n#!x', '#x', '#'];
for (const c of cases) {
  try { console.log(JSON.stringify(c), 'eval', JSON.stringify(eval(c))); } catch (e) { console.log(JSON.stringify(c), 'eval', e.name + ': ' + e.message); }
  try { console.log(JSON.stringify(c), 'indirect', JSON.stringify((0, eval)(c))); } catch (e) { console.log(JSON.stringify(c), 'indirect', e.name + ': ' + e.message); }
  try { new Function(c); console.log(JSON.stringify(c), 'function', 'compiled'); } catch (e) { console.log(JSON.stringify(c), 'function', e.name + ': ' + e.message); }
}
class P { #a = 1; has(o) { return #a in o; } }
console.log('private still', new P().has(new P()));

for (const c of ['#x', 'class A { #x; m() { #x; } }', 'class A { #x; m() { return #x + 1; } }', 'class A { m() { #y; } }', '#x in {}', 'class A { #x; m(o) { return #x in o; } }', 'class A { #x; m(o) { return (#x) in o; } }']) {
  try { eval(c); console.log(JSON.stringify(c), 'ok'); } catch (e) { console.log(JSON.stringify(c), e.name + ': ' + e.message); }
}
