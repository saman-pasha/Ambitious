// tests/js/typedtextc.js -- TextDecoder where node's ICU departs from the Encoding standard,
// against Chromium's (typedtextc.expect, written by node tools/js-chromium.js
// tests/js/typedtextc.js): the encodings ICU lacks (ISO-8859-16, x-user-defined), the bytes
// ICU maps otherwise (Shift_JIS's 0x80, Big5's HKSCS rows, EUC-KR's extension, windows-874's
// gaps), the bytes the standard reads again after an error, ISO-2022-JP's escapes cut short,
// and gb18030's errors in a stream cut anywhere. Errors print their names alone: node's
// words and Chromium's differ.

const p = (...a) => console.log(a.join(' | '));
const hex = s => { let r = []; for (let i = 0; i < s.length; i++) r.push(s.charCodeAt(i).toString(16)); return r.join(' '); };
const dec = (l, bytes, o) => { try { return hex(new TextDecoder(l, o).decode(new Uint8Array(bytes))); } catch (e) { return e.name; } };
const chunks = (l, bytes, k) => { const d = new TextDecoder(l); let s = ''; for (let i = 0; i < bytes.length; i += k) s += d.decode(new Uint8Array(bytes.slice(i, i + k)), { stream: true }); return hex(s + d.decode()); };

p(dec('shift_jis', [0x80, 0xA0, 0xFD, 0xFE, 0xFF]), dec('shift_jis', [0x82, 0x41]), dec('shift_jis', [0x81, 0x7F, 0x41]), dec('shift_jis', [0x41, 0x80, 0x82, 0x9F]));
p(dec('big5', [0xFF]), dec('big5', [0x87, 0x40, 0xFE, 0xFE, 0xA1, 0x40]), dec('big5', [0x81, 0x40, 0x41]), dec('big5', [0x80, 0x41]));
p(dec('euc-kr', [0x81, 0x41, 0xFE, 0xFE]), dec('euc-kr', [0x80, 0xFF, 0x41]), dec('euc-jp', [0x80, 0xFF, 0x8E, 0xE0]), dec('euc-jp', [0x8F, 0xA1]), dec('gb18030', [0xFF, 0x80, 0xA3, 0xA0]));
p(dec('windows-874', [0xDB, 0xA1, 0xFC]), dec('windows-1253', [0xAA, 0xD2, 0xFF]), dec('windows-1255', [0xCA, 0xD9, 0xFF]), dec('windows-1257', [0xA1, 0xA5]));
p(dec('iso-2022-jp', [0x1B, 0x24]), dec('iso-2022-jp', [0x1B, 0x28]), dec('iso-2022-jp', [0x1B, 0x24, 0x41]), dec('iso-2022-jp', [0x1B, 0x24, 0x42, 0x24, 0x22, 0x1B, 0x41]));
p(dec('gb18030', [0x81, 0x30, 0x81]), dec('gb18030', [0x81, 0x30]), dec('gb18030', [0x81]), dec('utf-16be', [0xDC, 0x00, 0xD8, 0x00]), dec('utf-8', [0xF4, 0x90]));
p(dec('x-user-defined', [0x41, 0x80, 0xFF]), new TextDecoder('x-user-defined').encoding, dec('iso-8859-16', [0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xFF]), new TextDecoder('iso-8859-16').encoding);
p(dec('replacement'), dec('iso-2022-kr'), dec('hz-gb-2312'), dec('foo'), dec('utf-8', [0xFF], { fatal: true }));
const iso = [0x41, 0x1B, 0x24, 0x42, 0x24, 0x22, 0x1B, 0x28, 0x42, 0x1B, 0x28, 0x42, 0x42, 0x1B, 0x24, 0x40, 0x24, 0x1B, 0x41, 0x42, 0x1B, 0x28, 0x4A, 0x5C, 0x1B];
p(...[1, 2, 3, 4, 7, 100].map(k => chunks('iso-2022-jp', iso, k)));
const gb = [0x81, 0x30, 0x41, 0x81, 0x30, 0x81, 0x30, 0xC4, 0xFF, 0x84, 0x31, 0xA4, 0x39, 0x81];
p(...[1, 2, 3, 100].map(k => chunks('gb18030', gb, k)));
const sj = [0x82, 0x41, 0x82, 0xA0, 0x80, 0x81];
p(...[1, 2, 100].map(k => chunks('shift_jis', sj, k)));
const kr = [0xC7, 0x41, 0x81, 0x41, 0xC7];
p(...[1, 2, 100].map(k => chunks('euc-kr', kr, k)));
let all = 0;
for (let b = 0; b < 256; b++) for (const e of ['x-user-defined', 'iso-8859-16']) { const s = new TextDecoder(e).decode(new Uint8Array([b])); all = (all * 31 + s.charCodeAt(0)) >>> 0; }
p('single bytes', all);
