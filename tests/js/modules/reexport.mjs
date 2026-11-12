import { log } from './log.mjs';
log('reexport');
export { value as answer, default as greetAgain } from './lib.mjs';
export * from './stars.mjs';
export * as libNs from './lib.mjs';
export const own = 'own';
