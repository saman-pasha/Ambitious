import { log } from './log.mjs';
import { ready } from './tla.mjs';
log('afterTla sees ' + ready);
export const done = true;
