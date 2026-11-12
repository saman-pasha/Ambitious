import { log } from './log.mjs';
import { fromB, callA } from './cycleB.mjs';
log('cycleA');
export function hoistedA() { return 'hoisted A, B says ' + fromB; }
export const constA = 'const A';
export const viaB = callA();
