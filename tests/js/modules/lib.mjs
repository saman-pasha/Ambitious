import { log } from './log.mjs';
log('lib');
export let counter = 0;
export function increment() { counter++; }
export default function greet(name) { return 'hello ' + name; }
export const value = 42;
export class Shape { area() { return 0; } }
const hidden = 'not exported';
export { hidden as renamed };
export var later;
later = 'assigned at evaluation';
