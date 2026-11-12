// tests/js/typedtext.js -- TextDecoder over the Encoding standard's decoders and TextEncoder's
// encodeInto, each line against node's (typedtext.expect, node with its full ICU): the labels
// and the encodings' names; UTF-16, the single-byte encodings, gbk and gb18030, big5,
// EUC-JP, ISO-2022-JP, Shift_JIS and EUC-KR; streams cut anywhere decoding as the whole;
// fatal, ignoreBOM and the BOM of a stream; node's errors. (The cases where node's ICU
// departs from the standard, and the encodings it lacks, are typedtextc.js's, against
// Chromium.)

const p = (...a) => console.log(a.join(' | '));
const hex = s => { let r = []; for (let i = 0; i < s.length; i++) r.push(s.charCodeAt(i).toString(16)); return r.join(' '); };
const tr = f => { try { return JSON.stringify(f()); } catch (e) { return e.name + ': ' + e.message; } };
const dec = (l, bytes, o, d) => { try { return hex(new TextDecoder(l, o).decode(new Uint8Array(bytes), d)); } catch (e) { return e.name + ': ' + e.message; } };

// the labels
{
const labels = ['utf-8', 'UTF8', ' utf-8 ', '\tunicode-1-1-utf-8\n', 'utf-16', 'utf-16le', 'utf-16be', 'ucs-2', 'unicodefffe', 'latin1', 'iso-8859-1', 'ascii', 'us-ascii',
  'windows-1252', 'iso-8859-2', 'l2', 'koi8-r', 'koi8-u', 'koi8-ru', 'macintosh', 'x-mac-cyrillic', 'windows-874', 'iso-8859-8-i', 'logical', 'ibm866', 'cp866', 'gbk', 'gb2312',
  'chinese', 'x-gbk', 'gb18030', 'big5', 'big5-hkscs', 'cn-big5', 'euc-jp', 'x-euc-jp', 'iso-2022-jp', 'csiso2022jp', 'shift_jis', 'sjis', 'ms932', 'windows-31j', 'euc-kr',
  'ks_c_5601-1987', 'korean', 'windows-1250', 'cp1251', 'greek', 'hebrew', 'arabic', 'tis-620', 'iso-8859-15', 'latin9',
  'replacement', 'iso-2022-kr', 'hz-gb-2312', 'iso-2022-cn', 'utf-7', 'foo', '', 'utf-32', 'utf8 x', 'UTF-16LE\f'];
for (const l of labels) p(JSON.stringify(l), tr(() => new TextDecoder(l).encoding));
p(tr(() => new TextDecoder().encoding), tr(() => new TextDecoder(undefined).encoding), tr(() => new TextDecoder(null)), tr(() => new TextDecoder({ toString() { return 'latin1'; } }).encoding));
p(tr(() => new TextDecoder().fatal), tr(() => new TextDecoder('utf-8', { fatal: 1 }).fatal), tr(() => new TextDecoder('utf-8', { ignoreBOM: 'x' }).ignoreBOM), tr(() => new TextDecoder('gbk', {}).ignoreBOM));
}

// each decoder
{
p(dec('utf-16le', [0x41, 0, 0x3D, 0xD8, 0x00, 0xDE]), dec('utf-16be', [0, 0x41, 0xD8, 0x3D, 0xDE, 0x00]), dec('utf-16le', [0xFF, 0xFE, 0x41, 0]), dec('utf-16le', [0xFF, 0xFE, 0x41, 0], { ignoreBOM: true }));
p(dec('utf-16be', [0xFE, 0xFF, 0, 0x41]), dec('utf-16le', [0x41]), dec('utf-16le', [0x00, 0xD8, 0x41, 0x00]), dec('utf-16le', [0x00, 0xDC]), dec('utf-16le', [0x41], { fatal: true }), dec('utf-16be', [0xFF, 0xFE, 0, 0x41]));
p(dec('windows-1252', [0x80, 0x41, 0x9F, 0xFF, 0x81]), dec('iso-8859-2', [0xA1, 0xFF]), dec('koi8-r', [0xC1, 0xE1]), dec('ibm866', [0x80, 0xF0]), dec('windows-1255', [0xE0, 0xD4]), dec('macintosh', [0x80, 0xDB]));
p(dec('shift_jis', [0x82, 0xA0, 0x93, 0xFA, 0x96, 0x7B, 0x41, 0xB1, 0x5C, 0x7E, 0xF0, 0x40]), dec('euc-jp', [0xA4, 0xA2, 0x8E, 0xB1, 0xC6, 0xFC, 0x8F, 0xB0, 0xA1]), dec('euc-jp', [0xA4, 0x41, 0x8E]));
p(dec('iso-2022-jp', [0x1B, 0x24, 0x42, 0x24, 0x22, 0x1B, 0x28, 0x42, 0x41]), dec('iso-2022-jp', [0x1B, 0x28, 0x4A, 0x5C, 0x7E, 0x1B, 0x28, 0x49, 0x31, 0x5F]), dec('iso-2022-jp', [0x1B, 0x28, 0x42, 0x1B, 0x28, 0x42]));
p(dec('iso-2022-jp', [0x1B, 0x41]), dec('iso-2022-jp', [0x0E, 0x0F]), dec('iso-2022-jp', [0x1B]), dec('iso-2022-jp', [0x1B, 0x24, 0x42, 0x24, 0x1B, 0x28, 0x42]), dec('iso-2022-jp', [0x80]));
p(dec('gbk', [0xC4, 0xE3, 0xBA, 0xC3, 0x80]), dec('gb18030', [0x81, 0x30, 0x81, 0x30, 0xC4, 0xE3, 0x84, 0x31, 0xA4, 0x39, 0x95, 0x32, 0x82, 0x36, 0x81, 0x35, 0xF4, 0x37]), dec('gb18030', [0x81, 0x30, 0x41]));
p(dec('big5', [0xA4, 0xA4, 0xA4, 0xE5, 0xA4, 0x41]), dec('euc-kr', [0xC7, 0xD1, 0xB1, 0xDB, 0xC7, 0x41]), dec('gbk', [0xC4, 0x41, 0x81, 0x7F, 0xA2, 0xE3]));
p(dec('shift_jis', [0x82]), dec('shift_jis', [0x82], { fatal: true }), dec('euc-kr', [0xC7, 0x41], { fatal: true }), dec('gbk', [0xFF]), dec('euc-jp', [0x8E]), dec('big5', [0xA4]));
p(dec('utf-8', [0xEF, 0xBB, 0xBF, 0x41]), dec('utf-8', [0xEF, 0xBB, 0xBF, 0x41], { ignoreBOM: true }), dec('utf-8', [0xC3], { fatal: true }), dec('windows-1252', [0xEF, 0xBB, 0xBF]), dec('gbk', [0xEF, 0xBB, 0xBF]));
p(dec('utf-8', [0xC0, 0x80, 0xE0, 0x80, 0x80, 0xED, 0xA0, 0x80, 0xF4, 0x90, 0x80, 0x80, 0xF0, 0x9F, 0x98]), dec('utf-8', [0xF0, 0x9F, 0x98, 0x80, 0xE2, 0x82, 0xAC, 0xC2, 0xA9]));
}

// streams cut anywhere: the same text as the whole
{
const cases = {
  'utf-8': [0xEF, 0xBB, 0xBF, 0x41, 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80, 0xE2, 0x82, 0x41, 0xF0, 0x9F, 0xC3, 0xA9, 0xED, 0xA0, 0x80, 0x5A],
  'utf-16le': [0xFF, 0xFE, 0x41, 0x00, 0x3D, 0xD8, 0x00, 0xDE, 0x00, 0xD8, 0x42, 0x00, 0x00, 0xDC, 0xAC, 0x20, 0x43],
  'utf-16be': [0xFE, 0xFF, 0x00, 0x41, 0xD8, 0x3D, 0xDE, 0x00, 0xD8, 0x00, 0x00, 0x42, 0xDC, 0x00, 0x20, 0xAC, 0x43],
  'shift_jis': [0x82, 0xA0, 0x93, 0xFA, 0x41, 0xB1, 0x82, 0x9F, 0x96, 0x7B, 0xF0, 0x40, 0x82],
  'euc-jp': [0xA4, 0xA2, 0x8E, 0xB1, 0x8F, 0xB0, 0xA1, 0xC6, 0xFC, 0x41, 0xA4, 0x41, 0xA4],
  'iso-2022-jp': [0x41, 0x1B, 0x24, 0x42, 0x24, 0x22, 0x46, 0x7C, 0x1B, 0x28, 0x4A, 0x5C, 0x7E, 0x1B, 0x28, 0x49, 0x31, 0x1B, 0x28, 0x42, 0x42, 0x1B, 0x24, 0x42, 0x24],
  'gbk': [0xC4, 0xE3, 0xBA, 0xC3, 0x80, 0x41, 0xC4, 0x41, 0xC4],
  'gb18030': [0x81, 0x30, 0x81, 0x30, 0xC4, 0xE3, 0x84, 0x31, 0xA4, 0x39, 0x95, 0x32, 0x82, 0x36, 0xC4, 0xE3],
  'big5': [0xA4, 0xA4, 0xA4, 0xE5, 0x41, 0xA4, 0x41, 0xA4, 0xA4, 0xA4],
  'euc-kr': [0xC7, 0xD1, 0xB1, 0xDB, 0x41, 0xC7, 0x41, 0xC7],
  'windows-1252': [0x80, 0x41, 0x9F, 0xFF, 0x81, 0xE9],
  'koi8-u': [0xA4, 0xC1, 0x41],
};
for (const [enc, bytes] of Object.entries(cases)) {
  const u = new Uint8Array(bytes);
  const whole = new TextDecoder(enc).decode(u);
  const out = [enc, hex(whole)];
  for (const k of [1, 2, 3, 5]) {
    const d = new TextDecoder(enc);
    let s = '';
    for (let i = 0; i < u.length; i += k) s += d.decode(u.subarray(i, i + k), { stream: true });
    s += d.decode();
    out.push(s === whole ? 'same' : 'DIFF ' + hex(s));
  }
  const d2 = new TextDecoder(enc);
  d2.decode(u.subarray(0, 3), { stream: true }); d2.decode(u.subarray(3));
  out.push(d2.decode(u) === whole ? 'again' : 'AGAIN-DIFF');
  p(...out);
}
const td = new TextDecoder('utf-8');
p(JSON.stringify(td.decode(new Uint8Array([0xE2, 0x82]), { stream: true })), JSON.stringify(td.decode(new Uint8Array([0xAC]), { stream: true })), JSON.stringify(td.decode()), hex(td.decode(new Uint8Array([0xE2]))));
const t16 = new TextDecoder('utf-16le');
p(JSON.stringify(t16.decode(new Uint8Array([0x41]), { stream: true })), JSON.stringify(t16.decode(new Uint8Array([0x00, 0x3D]), { stream: true })), JSON.stringify(t16.decode(new Uint8Array([0xD8, 0x00, 0xDE]))));
const tsj = new TextDecoder('shift_jis');
p(JSON.stringify(tsj.decode(new Uint8Array([0x82]), { stream: true })), JSON.stringify(tsj.decode(new Uint8Array([0xA0, 0x82]), { stream: true })), hex(tsj.decode(new Uint8Array([]), { stream: false })));
const tj = new TextDecoder('iso-2022-jp'), pieces = [];
for (const c of [[0x1B, 0x24], [0x42, 0x24], [0x22, 0x1B], [0x28, 0x4A], [0x5C], [0x1B, 0x28, 0x42, 0x41]]) pieces.push(hex(tj.decode(new Uint8Array(c), { stream: true })));
p(...pieces, hex(tj.decode()));
}

// fatal over a stream; the BOM: once a stream, its first output's
{
for (const [enc, lead] of [['utf-8', 0xE2], ['utf-8', 0x82], ['utf-16le', 0x82], ['shift_jis', 0x82], ['euc-kr', 0xC7], ['gb18030', 0x81]]) {
  const d = new TextDecoder(enc, { fatal: true });
  const r = [];
  try { r.push(hex(d.decode(new Uint8Array([0x41, 0x42]), { stream: true }))); r.push(hex(d.decode(new Uint8Array([lead]), { stream: true }))); r.push(hex(d.decode())); } catch (e) { r.push(e.name + ': ' + e.message); }
  try { r.push(hex(d.decode(new Uint8Array([0x43, 0x00])))); } catch (e) { r.push(e.name + ': ' + e.message); }
  p(enc, ...r);
}
for (const enc of ['utf-8', 'utf-16le', 'utf-16be']) {
  const bom = enc === 'utf-8' ? [0xEF, 0xBB, 0xBF] : enc === 'utf-16le' ? [0xFF, 0xFE] : [0xFE, 0xFF];
  const a = enc === 'utf-8' ? [0x41] : enc === 'utf-16le' ? [0x41, 0] : [0, 0x41];
  const both = new Uint8Array([...bom, ...a, ...bom, ...a]);
  const d = new TextDecoder(enc), di = new TextDecoder(enc, { ignoreBOM: true });
  p(enc, hex(d.decode(both)), hex(di.decode(both)), hex(d.decode(new Uint8Array(bom), { stream: true })), hex(d.decode(new Uint8Array(a))), hex(d.decode(new Uint8Array([...bom, ...a]))));
}
const tb = new TextDecoder('utf-8');
p(JSON.stringify(tb.decode(new Uint8Array([0xEF, 0xBB]), { stream: true })), JSON.stringify(tb.decode(new Uint8Array([0xBF, 0x41]), { stream: true })), hex(tb.decode(new Uint8Array([0xEF, 0xBB, 0xBF, 0x42]))));
}

// the inputs, the receivers, node's errors
{
p(tr(() => new TextDecoder().decode(new ArrayBuffer(2))), tr(() => new TextDecoder().decode(new DataView(new ArrayBuffer(1)))), tr(() => new TextDecoder().decode('x')), tr(() => new TextDecoder().decode(null)));
p(tr(() => new TextDecoder().decode(new Uint16Array([0x41, 0x42]))), tr(() => new TextDecoder('utf-16le').decode(new Uint16Array([0x41, 0x263A]))), tr(() => new TextDecoder().decode(new Uint8Array([0x61, 0x62, 0x63]).subarray(1))));
p(tr(() => TextDecoder()), tr(() => TextDecoder.prototype.decode.call({})), Object.prototype.toString.call(new TextDecoder()), TextDecoder.length, TextDecoder.prototype.decode.length);
p(tr(() => TextEncoder()), Object.prototype.toString.call(new TextEncoder()), new TextEncoder().encoding, TextEncoder.length, typeof TextDecoder.prototype.decode);
}

// TextEncoder: encode and encodeInto
{
const te = new TextEncoder();
const into = new Uint8Array(5);
p(JSON.stringify(te.encodeInto('a😀b', into)), [...into], JSON.stringify(te.encodeInto('ab', new Uint8Array(1))), JSON.stringify(te.encodeInto('é', new Uint8Array(1))), JSON.stringify(te.encodeInto('\uD800x', new Uint8Array(8))));
p(tr(() => te.encodeInto('a', new Uint16Array(1))), tr(() => te.encodeInto('a')), tr(() => te.encodeInto('a', [])), tr(() => te.encodeInto('a', null)), JSON.stringify(te.encodeInto('😀', new Uint8Array(3))), te.encodeInto.length, te.encoding);
const sub = new Uint8Array(new ArrayBuffer(8), 2, 3); p(JSON.stringify(te.encodeInto('xyzw', sub)), [...new Uint8Array(sub.buffer)]);
p(tr(() => te.encodeInto(123, new Uint8Array(4))), tr(() => te.encodeInto(true, new Uint8Array(4))), tr(() => te.encodeInto(null, new Uint8Array(4))), tr(() => te.encodeInto({}, new Uint8Array(4))));
p(tr(() => te.encode().length), tr(() => te.encode(null).length), tr(() => te.encode(undefined).length), [...te.encode('\uDC00\uD800')], [...te.encode('€😀')]);
const rt = 'héllo 世界 😀 \u0000 end';
p(new TextDecoder().decode(te.encode(rt)) === rt, new TextDecoder('utf-16le').decode(new Uint16Array([...rt].map(c => c.charCodeAt(0)))).length);
}
