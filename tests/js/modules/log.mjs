// the evaluation order, recorded by each module as it runs
export const order = [];
export function log(name) { order.push(name); }
