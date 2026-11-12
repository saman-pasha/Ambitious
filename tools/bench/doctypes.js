// tools/bench/doctypes.js LIST -- the document mode Chromium (Playwright's headless shell) gives
// each start of a document in LIST (a line each: a DOCTYPE, what comes before it, or nothing),
// as tests/exec/quirks_mode prints Ambitious's: `node tools/bench/doctypes.js
// tests/exec/quirks_mode.txt > tests/exec/quirks_mode.expect`. Each line is loaded as a page of
// its own, followed by a probe; the mode is quirks when document.compatMode is BackCompat, else
// limited-quirks when the probe's block is as tall as the 1px inline-block it holds (the line
// height quirk, which limited-quirks mode keeps: no strut), else no-quirks.
const { chromium } = require('playwright');
const fs = require('fs');
const os = require('os');
const path = require('path');
(async () => {
  const lines = fs.readFileSync(process.argv[2], 'utf8').replace(/\n$/, '').split('\n');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amb-doctypes-'));
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  const probe = '<div id=quirks-probe><span style="display:inline-block;width:1px;height:1px"></span></div>';
  const out = [];
  for (let i = 0; i < lines.length; i++) {
    const file = path.join(dir, 'd' + i + '.html');
    fs.writeFileSync(file, lines[i] + probe);
    await page.goto('file://' + file, { waitUntil: 'load' });
    const mode = await page.evaluate(() => {
      if (document.compatMode === 'BackCompat') return 'quirks';
      const p = document.getElementById('quirks-probe');
      return p && p.getBoundingClientRect().height === 1 ? 'limited_quirks' : 'no_quirks';
    });
    out.push(mode + '\t' + lines[i]);
  }
  await browser.close();
  fs.rmSync(dir, { recursive: true, force: true });
  process.stdout.write(out.map((l) => l + '\n').join(''));
})();
