import { log, order } from './log.mjs';
import greet, { counter, increment, value, Shape, renamed, later } from './lib.mjs';
import * as lib from './lib.mjs';
import { answer, greetAgain, own, fromStars, onlyA, onlyB, libNs } from './reexport.mjs';
import * as re from './reexport.mjs';
import { hoistedA, viaB } from './cycleA.mjs';
import { sawConstA } from './cycleB.mjs';
log('main');
const p = (...a) => console.log(a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)).join(' '));
p('order', order);
p('default', greet('world'), greet.name, value, renamed, later);
p('live before', counter, lib.counter);
increment(); increment();
p('live after', counter, lib.counter);
p('class', new Shape().area(), typeof Shape);
p('reexports', answer, greetAgain('again'), own, fromStars, onlyA, onlyB, libNs.value);
p('namespace keys', Object.keys(lib));
p('namespace tag', Object.prototype.toString.call(lib), typeof lib, Object.isExtensible(lib));
p('star keys', Object.keys(re));
p('ambiguous left out', 'both' in re, re.both);
p('default not starred', 'default' in re);
p('same namespace', libNs === lib);
try { lib.value = 1; } catch (e) { p('namespace assignment', e.constructor.name); }
try { counter = 5; } catch (e) { p('import assignment', e.constructor.name); }
p('cycle', hoistedA(), viaB, sawConstA);
p('this at top', typeof this);
try { undeclaredInModule = 1; } catch (e) { p('strict', e.constructor.name); }
p('meta', import.meta.url.split('/').pop(), typeof import.meta.resolve, import.meta.resolve('./lib.mjs').split('/').pop());
export const fromMain = 'main export';
