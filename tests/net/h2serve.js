// tests/net/h2serve.js [OPTIONS] ROOT... -- the test site over HTTP/2 and HTTP/1.1 from one node
// process, on 127.0.0.1 and a free port, which it prints first.
//
// Over TLS (--tls CERT KEY) a front takes each connection, offers h2 and http/1.1 by ALPN (only
// http/1.1 with --h1) and relays it to a backend of the protocol chosen: node's http2 server
// over cleartext for h2, its http server for http/1.1. --plain serves http/1.1 with no TLS, on
// the address --bind names (127.0.0.1 by default).
//
// The files are ROOT's, the first that has the path; in an .html file each NAME of --sub is
// replaced by its VALUE (each NAME of --sub-file NAME=FILE by FILE's first line, read at each
// request), then PORT by the server's port. FILE.headers holds header lines its
// response adds, as tests/net/serve.py reads them: Expires and Last-Modified written +N or -N
// are the response's Date plus or less N seconds, ETag: auto a hash of the body, a validator
// that matches answers 304 (with FILE.304.headers' lines when there is one); Status: NNN sets
// the status, and a value may name --sub's NAMEs. Content-Encoding there is done to the body,
// coding by coding: gzip, deflate (zlib's stream), br and zstd, by node's zlib; deflate-raw (no
// zlib header, sent as deflate), gzip-bad and zstd-bad (a byte of their data changed), br-bad
// (cut short by two bytes), each sent as its coding; zstd-dict (a zstd frame naming dictionary
// 7, sent as zstd).
// --date fixes every response's Date.
//
// --log FILE appends, for each connection, "conn C PROTOCOL"; for each request, "conn C stream S
// METHOD PATH, K open" over h2 (the streams open on its connection then), "conn C request N
// METHOD PATH" over http/1.1; with --log-headers its header block in order, a field a line
// ("  name: value", as the backend decoded it); with --log-frames, the client's frames as the
// front read them ("conn C > TYPE ..."); with --log-hpack, after each h2 request, the size of
// the server's HPACK table for the client's blocks.
//
// Its behaviours: --hold N holds each h2 response but its connection's first stream's (the page's)
// until N streams are open on the connection at once (or two seconds pass); --max-streams N sets
// SETTINGS_MAX_CONCURRENT_STREAMS; --goaway S sends GOAWAY with S its last stream when stream S
// of the first connection arrives, and refuses the streams after it;
// --reset PATH CODE answers a request for PATH with RST_STREAM of CODE (REFUSED_STREAM only the
// first time); --drop-second closes an http/1.1 connection at its second request, unanswered;
// --stall PATH never answers a request for PATH over h2.
'use strict';
const http2 = require('http2');
const http = require('http');
const tls = require('tls');
const net = require('net');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');

const a = { stalls: [], bind: '127.0.0.1', roots: [], subs: [], subfiles: [], tls: null, h1: false, plain: false, log: null, headers: false, frames: false, hpack: false, date: null,
  hold: 0, maxStreams: 0, goaway: 0, resets: [], dropSecond: false };
for (let i = 2; i < process.argv.length; i++) {
  const o = process.argv[i];
  if (o === '--tls') { a.tls = [process.argv[++i], process.argv[++i]]; }
  else if (o === '--h1') a.h1 = true;
  else if (o === '--plain') a.plain = true;
  else if (o === '--bind') a.bind = process.argv[++i];
  else if (o === '--sub') a.subs.push(process.argv[++i].split(/=(.*)/s).slice(0, 2));
  else if (o === '--sub-file') a.subfiles.push(process.argv[++i].split(/=(.*)/s).slice(0, 2));
  else if (o === '--log') a.log = process.argv[++i];
  else if (o === '--log-headers') a.headers = true;
  else if (o === '--log-frames') a.frames = true;
  else if (o === '--log-hpack') a.hpack = true;
  else if (o === '--date') a.date = +process.argv[++i];
  else if (o === '--hold') a.hold = +process.argv[++i];
  else if (o === '--max-streams') a.maxStreams = +process.argv[++i];
  else if (o === '--goaway') a.goaway = +process.argv[++i];
  else if (o === '--reset') a.resets.push({ path: process.argv[++i], code: process.argv[++i], done: false });
  else if (o === '--drop-second') a.dropSecond = true;
  else if (o === '--stall') a.stalls.push(process.argv[++i]);
  else a.roots.push(path.resolve(o));
}
let PORT = 0;
const log = (s) => { if (a.log) fs.appendFileSync(a.log, s + '\n'); };
const now = () => (a.date !== null ? a.date : Math.floor(Date.now() / 1000));
const httpDate = (t) => new Date(t * 1000).toUTCString();
const sub = (s) => {
  for (const [k, v] of a.subs) s = s.split(k).join(v);
  for (const [k, f] of a.subfiles) { let v = ''; try { v = fs.readFileSync(f, 'utf8').split('\n')[0]; } catch (e) {} s = s.split(k).join(v); }
  return s;
};

function sidecar(p, body) {
  const out = [];
  if (!fs.existsSync(p)) return out;
  for (const line of fs.readFileSync(p, 'utf8').split('\n')) {
    const c = line.indexOf(':');
    if (c < 0) continue;
    const k = line.slice(0, c).trim();
    let v = sub(line.slice(c + 1).trim());
    if (/^(expires|last-modified)$/i.test(k) && /^[+-]\d+$/.test(v)) v = httpDate(now() + +v);
    if (/^etag$/i.test(k) && v === 'auto') v = '"' + crypto.createHash('sha1').update(body).digest('hex').slice(0, 12) + '"';
    out.push([k.toLowerCase(), v]);
  }
  return out;
}

function fresh(h, headers) {
  const get = (n) => { const e = headers.find(([k]) => k === n); return e && e[1]; };
  const etag = get('etag'), inm = h['if-none-match'];
  if (inm !== undefined) {
    const tags = inm.split(',').map((t) => t.trim());
    return etag !== undefined && (tags.includes(etag) || tags.includes('W/' + etag) || tags.includes('*'));
  }
  const lm = get('last-modified'), ims = h['if-modified-since'];
  if (ims !== undefined && lm !== undefined) return Date.parse(ims) >= Date.parse(lm);
  return false;
}

// the body in the codings its Content-Encoding names, in their order
function encode(headers, body) {
  const out = [];
  for (const [k, v] of headers) {
    if (k !== 'content-encoding') { out.push([k, v]); continue; }
    const names = [];
    for (let c of v.split(',').map((x) => x.trim())) {
      const bad = c.endsWith('-bad');
      if (bad) c = c.slice(0, -4);
      if (c === 'gzip') body = zlib.gzipSync(body);
      else if (c === 'deflate') body = zlib.deflateSync(body);
      else if (c === 'deflate-raw') { body = zlib.deflateRawSync(body); c = 'deflate'; }
      else if (c === 'br') body = zlib.brotliCompressSync(body);
      else if (c === 'zstd') body = zlib.zstdCompressSync(body, { params: { [zlib.constants.ZSTD_c_checksumFlag]: 1 } });
      else if (c === 'zstd-dict') {
        // a frame that names dictionary 7: Dictionary_ID_Flag 1 and its byte after the window descriptor
        const f = zlib.zstdCompressSync(body, { params: { [zlib.constants.ZSTD_c_contentSizeFlag]: 0 } });
        body = Buffer.concat([f.slice(0, 4), Buffer.from([(f[4] & ~3) | 1, f[5], 7]), f.slice(6)]);
        c = 'zstd';
      }
      // broken: br cut short (a Brotli stream has no checksum, and a byte changed may still decode),
      // the others a byte of their data changed
      if (bad && c === 'br') body = body.slice(0, body.length - 2);
      else if (bad) { body = Buffer.from(body); body[body.length >> 1] ^= 0x55; }
      names.push(c);
    }
    out.push([k, names.join(', ')]);
  }
  return [out, body];
}

const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript', '.png': 'image/png', '.gif': 'image/gif' };
// what a request is answered: its status, its headers (lowercase names, in order), its body
function answer(method, url, h) {
  const rel = decodeURIComponent(url.split('?')[0]).replace(/^\/+/, '');
  let file = null;
  for (const r of a.roots) {
    const p = path.resolve(r, rel);
    if (p.startsWith(r + path.sep) && fs.existsSync(p) && fs.statSync(p).isFile()) { file = p; break; }
  }
  const base = [['date', httpDate(now())]];
  if (!file) return { status: 404, headers: base.concat([['content-type', 'text/plain']]), body: Buffer.from('not found\n') };
  let body = fs.readFileSync(file);
  const ext = path.extname(file);
  if (ext === '.html') body = Buffer.from(sub(body.toString('latin1')).split('PORT').join(String(PORT)), 'latin1');
  let headers = [['content-type', TYPES[ext] || 'application/octet-stream']].concat(sidecar(file + '.headers', body));
  let status = 200;
  const st = headers.find(([k]) => k === 'status');
  if (st) { status = +st[1]; headers = headers.filter(([k]) => k !== 'status'); }
  if (status === 200 && fresh(h, headers)) {
    if (fs.existsSync(file + '.304.headers')) headers = sidecar(file + '.304.headers', body);
    return { status: 304, headers: base.concat(headers.filter(([k]) => k !== 'content-type' && k !== 'content-encoding')), body: Buffer.alloc(0) };
  }
  [headers, body] = encode(headers, body);
  return { status, headers: base.concat(headers, [['content-length', String(body.length)]]), body };
}

// ---- the backends ----

// the connection a backend's socket is: the front's number for it, by its local port there
const fronted = new Map();
let nconn = 0;
const sessions = new Map();

const h2settings = {};
if (a.maxStreams) h2settings.maxConcurrentStreams = a.maxStreams;
const h2b = http2.createServer({ settings: h2settings });
h2b.on('session', (s) => {
  sessions.set(s, { id: fronted.get(s.socket.remotePort) || 0, open: 0, held: [], released: false, timer: null, last: 0 });
});
h2b.on('stream', (stream, headers, flags, raw) => {
  stream.on('error', () => {});
  const s = stream.session, st = sessions.get(s);
  const id = st.id || (st.id = fronted.get(s.socket.remotePort) || 0);
  if (st.last && stream.id > st.last) {
    log(`conn ${id} stream ${stream.id} ${headers[':method']} ${headers[':path']}, refused: after the GOAWAY's last stream`);
    stream.close(http2.constants.NGHTTP2_REFUSED_STREAM);
    return;
  }
  st.open++;
  stream.on('close', () => { st.open--; });
  log(`conn ${id} stream ${stream.id} ${headers[':method']} ${headers[':path']}, ${st.open} open`);
  if (a.headers) for (let i = 0; i < raw.length; i += 2) log(`  ${raw[i]}: ${raw[i + 1]}`);
  if (a.hpack) log(`conn ${id} stream ${stream.id} hpack table ${s.state.inflateDynamicTableSize}`);
  const rs = a.resets.find((r) => r.path === headers[':path'] && !(r.code === 'REFUSED_STREAM' && r.done));
  if (rs) {
    rs.done = true;
    log(`conn ${id} stream ${stream.id} reset: ${rs.code}`);
    stream.close(http2.constants['NGHTTP2_' + rs.code]);
    return;
  }
  if (a.stalls.includes(headers[':path'])) {
    log(`conn ${id} stream ${stream.id} left unanswered`);
    return;
  }
  if (a.goaway && stream.id === a.goaway && !a.goawaySent) {
    a.goawaySent = true;
    st.last = stream.id;
    log(`conn ${id} GOAWAY, its last stream ${stream.id}`);
    s.goaway(http2.constants.NGHTTP2_NO_ERROR, stream.id);
  }
  const r = answer(headers[':method'], headers[':path'], headers);
  const send = () => {
    if (stream.destroyed) return;
    const out = { ':status': r.status };
    for (const [k, v] of r.headers) {
      if (out[k] === undefined) out[k] = v;
      else out[k] = [].concat(out[k], v);
    }
    stream.respond(out);
    stream.end(r.body);
  };
  if (a.hold && !st.released && stream.id !== 1) {
    st.held.push(send);
    const release = () => { st.released = true; clearTimeout(st.timer); for (const f of st.held.splice(0)) f(); };
    if (st.open >= a.hold) release();
    else if (!st.timer) st.timer = setTimeout(release, 2000);
  } else send();
});
h2b.on('sessionError', () => {});

const h1b = http.createServer((req, res) => {
  const sock = req.socket;
  const id = a.plain ? sock._amb : fronted.get(sock.remotePort) || 0;
  sock._n = (sock._n || 0) + 1;
  log(`conn ${id} request ${sock._n} ${req.method} ${req.url}`);
  if (a.headers) for (let i = 0; i < req.rawHeaders.length; i += 2) log(`  ${req.rawHeaders[i]}: ${req.rawHeaders[i + 1]}`);
  if (a.dropSecond && sock._n === 2) {
    log(`conn ${id} request ${sock._n} dropped, the connection closed unanswered`);
    sock.destroy();
    return;
  }
  const r = answer(req.method, req.url, req.headers);
  const flat = [];
  for (const [k, v] of r.headers) flat.push(k, v);
  res.writeHead(r.status, flat);
  res.end(r.body);
});
h1b.keepAliveTimeout = 60000;
h1b.on('clientError', (e, s) => s.destroy());

// ---- the front ----

const FT = ['DATA', 'HEADERS', 'PRIORITY', 'RST_STREAM', 'SETTINGS', 'PUSH_PROMISE', 'PING', 'GOAWAY', 'WINDOW_UPDATE', 'CONTINUATION'];
// the client's frames, as they come: logged, a line each
function frames(id, st, d) {
  st.buf = Buffer.concat([st.buf, d]);
  if (!st.pre) {
    if (st.buf.length < 24) return;
    st.buf = st.buf.slice(24);
    st.pre = true;
    log(`conn ${id} > the preface`);
  }
  while (st.buf.length >= 9) {
    const len = st.buf.readUIntBE(0, 3), type = st.buf[3], flags = st.buf[4], sid = st.buf.readUInt32BE(5) & 0x7fffffff;
    if (st.buf.length < 9 + len) return;
    const p = st.buf.slice(9, 9 + len);
    let s = `conn ${id} > ${FT[type] || 'type ' + type}`;
    if (sid) s += ` stream ${sid}`;
    if (type === 4) s += flags & 1 ? ' ACK' : ' ' + Array.from({ length: len / 6 }, (_, i) => `${p.readUInt16BE(6 * i)}=${p.readUInt32BE(6 * i + 2)}`).join(' ');
    if (type === 8) s += ` by ${p.readUInt32BE(0) & 0x7fffffff}`;
    if (type === 3) s += ` code ${p.readUInt32BE(0)}`;
    if (type === 7) s += ` last ${p.readUInt32BE(0) & 0x7fffffff}, code ${p.readUInt32BE(4)}`;
    if (type === 2) s += `, on ${p.readUInt32BE(0) & 0x7fffffff}${p[0] & 0x80 ? ' exclusively' : ''}, weight ${p[4] + 1}`;
    if (type === 6) s += flags & 1 ? ' ACK' : '';
    if (type === 1) {
      let o = 0;
      if (flags & 8) o = 1;
      if (flags & 0x20) s += `, on ${p.readUInt32BE(o) & 0x7fffffff}${p[o] & 0x80 ? ' exclusively' : ''}, weight ${p[o + 4] + 1}`;
      s += `, its block ${len - (flags & 0x20 ? 5 : 0) - o - (flags & 8 ? p[0] : 0)} bytes`;
      if (flags & 1) s += ', the stream\'s end';
    }
    if (type === 0) s += `, ${len} bytes${flags & 1 ? ', the stream\'s end' : ''}`;
    log(s);
    st.buf = st.buf.slice(9 + len);
  }
}

function front(sock, alpn, id) {
  const b = net.connect(alpn === 'h2' ? h2b.address().port : h1b.address().port, '127.0.0.1');
  // no Nagle on either side of the relay: a write held for the peer's delayed ACK costs 40 ms
  sock.setNoDelay(true);
  b.setNoDelay(true);
  b.on('connect', () => fronted.set(b.localPort, id));
  const st = { buf: Buffer.alloc(0), pre: false };
  sock.on('data', (d) => {
    if (a.frames && alpn === 'h2') frames(id, st, d);
    b.write(d);
  });
  b.on('data', (d) => sock.write(d));
  sock.on('end', () => b.end());
  b.on('end', () => sock.end());
  sock.on('close', () => b.destroy());
  b.on('close', () => sock.destroy());
  sock.on('error', () => {});
  b.on('error', () => {});
}

h2b.listen(0, '127.0.0.1', () => h1b.listen(0, '127.0.0.1', () => {
  if (a.plain) {
    h1b.close();
    const srv = http.createServer(h1b.listeners('request')[0]);
    srv.keepAliveTimeout = 60000;
    srv.on('connection', (s) => { s.setNoDelay(true); s._amb = ++nconn; log(`conn ${s._amb} http/1.1`); });
    srv.on('clientError', (e, s) => s.destroy());
    srv.listen(0, a.bind, () => { PORT = srv.address().port; console.log(PORT); });
    return;
  }
  const opts = { cert: fs.readFileSync(a.tls[0]), key: fs.readFileSync(a.tls[1]), ALPNProtocols: a.h1 ? ['http/1.1'] : ['h2', 'http/1.1'] };
  const f = tls.createServer(opts, (sock) => {
    const id = ++nconn;
    const alpn = sock.alpnProtocol || 'http/1.1';
    log(`conn ${id} ${alpn}`);
    front(sock, alpn, id);
  });
  f.on('tlsClientError', () => {});
  f.listen(0, '127.0.0.1', () => { PORT = f.address().port; console.log(PORT); });
}));
