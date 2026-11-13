// String.fromCharCode(0) is a one-character string, not the empty one; NUL survives every string operation
(function () {
const z = String.fromCharCode(0);
console.log(z.length, ('a' + z + 'b').length, '\0'.length, ('\0' + '\0').length, (z + z + z + z).length);
let s = ''; s += z; s += z; console.log(s.length, [z, z].join('').length, z.repeat(3).length, `${z}${z}`.length);
const f = (c, n) => { let s = ''; while (n > 0) { if (n & 1) s += c; n >>>= 1; if (n > 0) c += c; } return s; };
console.log(f(z, 64).length, f('x', 64).length, String.fromCharCode(0, 0, 65).length, (String.fromCharCode(1) + String.fromCharCode(0)).length);
console.log(JSON.stringify(z + 'a' + z), encodeURIComponent(z + z), (z + 'x').charCodeAt(0), ('x' + z).charCodeAt(1));
})();
(function () {
const s = 'a\0b';
console.log(s[1].length, s.charAt(1).length, s.at(1).length, String.fromCodePoint(0).length, s.split('').length, [...s].length, s.substr(1, 1).length, s.slice(1, 2).length, s.substring(1, 2).length);
console.log(Array.from(s).map(c => c.length).join(), s.split('\0').join('|'), s.replace('\0', '.'), s.padEnd(5, '\0').length, '\0'.repeat(2).length, JSON.parse('"\\u0000"').length, decodeURIComponent('%00').length, unescape('%00').length);
console.log(new TextDecoder().decode(new Uint8Array([0])).length, String.fromCharCode.apply(null, [0]).length, String.fromCharCode(...[0, 0]).length, s.normalize().length, s.toUpperCase().length, s.trim().length, [s.codePointAt(1), s.charCodeAt(1)].join());
})();
