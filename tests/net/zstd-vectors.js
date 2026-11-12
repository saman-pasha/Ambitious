// tests/net/zstd-vectors.js DIR -- the zstd test vectors of tests/exec/net_zstd.c, made by
// node's zlib (libzstd 1.5.7) into DIR; prints the .expect's lines, each vector's name, its
// decoded length and CRC-32, or how Chromium's decoder (libzstd with a window of at most
// 8 MB, ZSTD_d_windowLogMax 23, and no dictionary) refuses it.
//
// The inputs are made here, the same each run: words a PRNG draws from a list (an alphabet
// skewed as text's is, and repeats for matches), bytes it draws (incompressible), runs of one
// byte, short texts of a few letters, slices of noise, bytes of low values (Huffman weights
// written directly), and mixtures. Each is compressed at a level and with the parameters its
// name says: levels 1 to 19 (the fast, lazy and optimal strategies), with and without a
// checksum and a content size, a 1 KB window, long-distance matching over a 200 KB repeat.
// Then frames made by hand: two frames with a skippable one between, a frame that names a
// dictionary, one whose window is 16 MB, and one whose literals and tables are all RLE.
// tests/exec/zstd/ holds what it wrote; run it again only to change the vectors.
const zlib = require('zlib');
const fs = require('fs');
const path = require('path');
const C = zlib.constants;
const dir = process.argv[2];
fs.mkdirSync(dir, { recursive: true });

let seed = 2463534242;
// xorshift32: every bit of it varies, where a linear congruential generator's low bits repeat
const rand = () => { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed; };
const words = ['the', 'of', 'and', 'a', 'to', 'in', 'is', 'that', 'it', 'was', 'for', 'on', 'are', 'as', 'with', 'his', 'they',
  'at', 'be', 'this', 'from', 'have', 'or', 'by', 'one', 'had', 'not', 'but', 'what', 'all', 'were', 'when', 'we', 'there', 'can',
  'network', 'stream', 'frame', 'header', 'window', 'Huffman', 'sequence', 'literal', 'offset', 'Chromium', 'connection',
  'browser', 'decoder', 'table', 'block', '<div class="item">', '</div>', '\n', '. ', ', ', '{ color: navy }', 'https://'];
function text(n) {
  let s = '';
  while (s.length < n) s += words[rand() % words.length] + (rand() % 7 ? ' ' : '');
  return Buffer.from(s.slice(0, n));
}
function noise(n) { const b = Buffer.alloc(n); for (let i = 0; i < n; i++) b[i] = rand() & 255; return b; }
function letters(n, alphabet) {
  const b = Buffer.alloc(n);
  for (let i = 0; i < n; i++) { const r = rand() % 100; b[i] = alphabet.charCodeAt(r < 50 ? 0 : r < 75 ? 1 : r < 88 ? 2 : (r % (alphabet.length - 2)) + 2); }
  return b;
}

function crc32(b) {
  let c = 0xffffffff;
  for (const x of b) { c ^= x; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); }
  return (c ^ 0xffffffff) >>> 0;
}
const hex8 = (v) => v.toString(16).padStart(8, '0');
const lines = [];
// frames: what node's decoder makes of each, frame by frame (its one-shot decoder stops at the
// first frame's end, where libzstd's streaming one, as Chromium drives it, goes on to the next)
function put(name, bytes, original, frames) {
  fs.writeFileSync(path.join(dir, name + '.zst'), bytes);
  // what node's own decoder, held to Chromium's window, makes of it
  let back;
  try {
    const d = (b) => zlib.zstdDecompressSync(b, { params: { [C.ZSTD_d_windowLogMax]: 23 } });
    back = frames ? Buffer.concat(frames.map(d)) : d(bytes);
  } catch (e) {
    lines.push(`${name}.zst: refused (${e.message})`);
    return;
  }
  if (original && Buffer.compare(back, original) !== 0) throw new Error(name + ': node does not decode it to its input');
  lines.push(`${name}.zst: ${back.length} bytes, crc32 ${hex8(crc32(back))}`);
}
function z(name, input, params) {
  const p = { [C.ZSTD_c_checksumFlag]: 1 };
  for (const k of Object.keys(params)) p[C['ZSTD_c_' + k]] = params[k];
  put(name, zlib.zstdCompressSync(input, { params: p }), input);
}

z('empty', Buffer.alloc(0), { compressionLevel: 3 });
z('byte', Buffer.from('a'), { compressionLevel: 3 });
z('hello', Buffer.from('hello, zstd: hello, zstd again\n'), { compressionLevel: 3 });
const t200 = text(200000);
for (const lv of [1, 3, 9, 19]) z('words-' + lv, t200, { compressionLevel: lv });
z('words-nochecksum', t200, { compressionLevel: 5, checksumFlag: 0 });
z('words-nosize', t200, { compressionLevel: 5, contentSizeFlag: 0 });
z('small-text', text(900), { compressionLevel: 9 });
z('window10', text(50000), { compressionLevel: 9, windowLog: 10, contentSizeFlag: 0 });
z('run', Buffer.alloc(200000, 0x78), { compressionLevel: 3 });
z('noise', noise(40000), { compressionLevel: 3 });
const mixed = Buffer.concat([text(40000), noise(30000), Buffer.alloc(20000, 0x2d), text(70000), noise(500), text(1000)]);
for (const lv of [2, 9, 16]) z('mixed-' + lv, mixed, { compressionLevel: lv });
for (const [i, a] of ['ab', 'abc', 'xyz01', 'eeeeeeeetaoinshrdlu', 'qwertyuiopasdfghjklzxcvbnm'].entries())
  for (const n of [40, 300, 3000]) z(`letters-${i}-${n}`, letters(n, a), { compressionLevel: 6 });
const rep = noise(200000);
z('ldm', Buffer.concat([rep, text(1000), rep]), { compressionLevel: 3, enableLongDistanceMatching: 1, windowLog: 23, contentSizeFlag: 0 });

// frames by hand: two frames and a skippable frame between them; a dictionary named; a 16 MB window
const f1 = zlib.zstdCompressSync(text(5000), { params: { [C.ZSTD_c_checksumFlag]: 1 } });
const f2 = zlib.zstdCompressSync(text(7000), { params: { [C.ZSTD_c_checksumFlag]: 1 } });
const skip = Buffer.concat([Buffer.from([0x53, 0x2a, 0x4d, 0x18, 5, 0, 0, 0]), Buffer.from('skip!')]);
put('frames', Buffer.concat([f1, skip, f2]), null, [f1, f2]);
// a dictionary's id: the frame header descriptor's Dictionary_ID_Flag 3 and four bytes after its window
// descriptor (the frame is no single segment, its window descriptor present)
const nd = zlib.zstdCompressSync(text(3000), { params: { [C.ZSTD_c_contentSizeFlag]: 0 } });
const hd = nd[4];
put('dictionary', Buffer.concat([nd.slice(0, 4), Buffer.from([(hd & ~3) | 3, nd[5], 0x78, 0x56, 0x34, 0x12]), nd.slice(6)]));
// a window of 16 MB: a frame of a small window, its window descriptor's exponent made 14 (2^24); its
// matches all lie within the first window, so only the window's size is wrong for HTTP
const w = Buffer.from(zlib.zstdCompressSync(text(20000), { params: { [C.ZSTD_c_contentSizeFlag]: 0 } }));
w[5] = 14 << 3;
put('window24', w);
// table modes the inputs above reach rarely: slices of noise at random places (literal lengths of
// one code, RLE; match lengths that repeat the last block's table), and bytes of low values
// skewed (a Huffman tree's weights written directly); from a seed of their own, found to reach them
seed = 31337;
const piece = noise(4000);
const slices = [];
for (let i = 0; i < 160; i++) slices.push(piece.slice(rand() % 3000, (rand() % 3000) + 900));
z('slices', Buffer.concat(slices), { compressionLevel: 12 });
function low(n, k) {
  const b = Buffer.alloc(n);
  for (let i = 0; i < n; i++) { const r = rand() % 100; b[i] = r < 40 ? 1 : r < 60 ? 2 : r < 75 ? 3 : rand() % k; }
  return b;
}
z('low-500', low(500, 16), { compressionLevel: 6 });
z('low-3000', low(3000, 12), { compressionLevel: 6 });
// by hand: literals RLE ('A' twenty times) and each table RLE, twenty sequences of one literal
// and a match of 8 at the first repeat offset (1): 180 bytes of 'A', its bit stream only its mark
put('rle', Buffer.from([0x28, 0xb5, 0x2f, 0xfd, 0x20, 180, 0x45, 0, 0, 0xa1, 0x41, 0x14, 0x54, 0x01, 0x00, 0x05, 0x01]), Buffer.alloc(180, 0x41));
process.stdout.write(lines.map((l) => l + '\n').join(''));
