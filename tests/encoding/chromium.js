// tests/encoding/chromium.js -- the cases of tests/encoding/cases.txt answered by Chromium
// (Playwright's headless shell), as tests/exec/encoding.c answers them: the decoders by
// Chromium's TextDecoder (ignoreBOM, as the decoders are called past the sniffing), the
// labels by the encoding a TextDecoder takes them for, and each sniffing case by the
// document.characterSet of the bytes served over HTTP with the case's Content-Type (text/html
// for none), or
// read from a file for a local file's case. Chromium does not say where an encoding came
// from, so a sniffing case's answer is its encoding alone. The reference that shows where
// the expectations (tests/encoding/gen.py) and Chromium agree:
//
//     node tests/encoding/chromium.js > chromium.txt
//     cut -d' ' -f1-5 tests/exec/encoding.expect | diff - chromium.txt   (sniff lines: their encoding)
const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const lines = fs.readFileSync(path.join(__dirname, 'cases.txt'), 'utf8').split('\n').filter((l) => l && l[0] !== '#');
const unhex = (h) => (h === '-' ? [] : h.match(/../g).map((x) => parseInt(x, 16)));

(async () => {
  const sniffs = [];
  const server = http.createServer((req, res) => {
    const c = sniffs[parseInt(req.url.slice(1), 10)];
    res.writeHead(200, { 'Content-Type': c.type || 'text/html' });
    res.end(Buffer.from(c.bytes));
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('about:blank');
  const decodeCases = lines.filter((l) => !l.startsWith('sniff'));
  // the decoders and labels, in the page: one evaluation for all
  const answers = await page.evaluate((cases) => {
    const hex = (h) => (h === '-' ? [] : h.match(/../g).map((x) => parseInt(x, 16)));
    const names = {};
    const decode = (label, bytes) => {
      let text;
      try { text = new TextDecoder(label, { ignoreBOM: true }).decode(new Uint8Array(bytes)); } catch (e) { return null; }
      return Array.from(text).map((c) => c.codePointAt(0));
    };
    const fnv = (h, cps) => {
      for (const cp of cps) h = Math.imul((h ^ cp) >>> 0, 0x01000193) >>> 0;
      return Math.imul((h ^ 0xFFFFFFFF) >>> 0, 0x01000193) >>> 0;
    };
    const errs = (cps) => cps.filter((c) => c === 0xFFFD).length;
    return cases.map((line) => {
      const f = line.split(' ');
      if (f[0] === 'label') {
        const text = new TextDecoder().decode(new Uint8Array(hex(f[1])));
        try { return line + ' -> ' + new TextDecoder(text).encoding; } catch (e) { return line + ' -> failure (or replacement)'; }
      }
      const prefix = hex(f[2]);
      if (f[0] === 'decode') {
        const cps = decode(f[1], prefix);
        if (cps === null) return line + ' -> (no TextDecoder)';
        const e = errs(cps);
        return line + ' -> ' + (cps.length ? cps.map((c) => 'U+' + c.toString(16).toUpperCase().padStart(4, '0')).join(' ') : '(nothing)') +
          ' (' + e + ' error' + (e === 1 ? '' : 's') + ')';
      }
      let h = 0x811C9DC5, inputs = 0, points = 0, e = 0;
      const tails = [];
      if (f[0] === 'quads') { for (let b3 = 0x81; b3 <= 0xFE; b3++) for (let b4 = 0x30; b4 <= 0x39; b4++) tails.push([b3, b4]); }
      else for (let b = 0; b <= 0xFF; b++) tails.push([b]);
      for (const t of tails) {
        const cps = decode(f[1], prefix.concat(t));
        if (cps === null) return line + ' -> (no TextDecoder)';
        h = fnv(h, cps); inputs++; points += cps.length; e += errs(cps);
      }
      return line + ' -> ' + inputs + ' inputs, ' + points + ' code points, ' + e + ' errors, fnv1a32 ' + h.toString(16).padStart(8, '0');
    });
  }, decodeCases);
  // the sniffing cases: each a document, served or a file
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amb-enc-'));
  const out = new Map();
  decodeCases.forEach((l, i) => out.set(l, answers[i]));
  for (const line of lines.filter((l) => l.startsWith('sniff'))) {
    const f = line.split(' ');
    const type = Buffer.from(unhex(f[2])).toString('latin1');
    const bytes = unhex(f[3]);
    let url;
    if (f[1] === '1') {
      const file = path.join(dir, sniffs.length + '.html');
      fs.writeFileSync(file, Buffer.from(bytes));
      url = 'file://' + file;
    } else url = 'http://127.0.0.1:' + port + '/' + sniffs.length;
    sniffs.push({ type, bytes });
    try {
      await page.goto(url);
      out.set(line, line + ' -> ' + (await page.evaluate(() => document.characterSet)));
    } catch (e) {
      out.set(line, line + ' -> (not shown: ' + e.message.split('\n')[0] + ')');
      await page.goto('about:blank');
    }
  }
  for (const l of lines) process.stdout.write(out.get(l) + '\n');
  await browser.close();
  server.close();
  fs.rmSync(dir, { recursive: true });
})();
