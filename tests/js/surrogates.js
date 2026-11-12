// tests/js/surrogates.js -- lone surrogates, each line against V8's (surrogates.expect,
// written by node surrogates.js): the string iterator takes a pair as one code point and
// a lone surrogate, high or low, as one of its own; JSON.stringify escapes a lone one
// (well-formed JSON.stringify, ES2019) and keeps a pair.
const s = 'a\uD800b\uDC00c😀d\uD800';
console.log([...s].map(c => c.length + ':' + c.charCodeAt(0).toString(16)).join(' '));
console.log(Array.from('\uDBFF􏿿').map(c => c.length).join(' '));
console.log(JSON.stringify(s), JSON.stringify('􏿿'), JSON.stringify({ k: '\uDEAD', '\uD800': 1 }));
