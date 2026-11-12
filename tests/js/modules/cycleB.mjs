import { log } from './log.mjs';
import { hoistedA, constA } from './cycleA.mjs';
log('cycleB');
export const fromB = 'B';
export function callA() { return hoistedA(); }
let tdz;
try { tdz = constA; } catch (e) { tdz = e.constructor.name; }
export const sawConstA = tdz;
