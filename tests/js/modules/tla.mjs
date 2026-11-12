import { log } from './log.mjs';
log('tla start');
export const ready = await new Promise(r => setTimeout(() => r('tla ready'), 5));
log('tla end');
