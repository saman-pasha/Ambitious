// tools/bench/boxes.js PAGE... -- each page loaded in Chromium (Playwright's headless shell) in
// an 800x600 viewport, and each element with an id written with its getBoundingClientRect():
// its border box in page px, scrolled to the top. The reference tests/integ/boxes.sh holds
// Ambitious's boxes to: `node tools/bench/boxes.js tests/pages/NAME.html` writes
// tests/pages/NAME.chromium.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  for (const file of process.argv.slice(2)) {
    await page.goto('file://' + path.resolve(file), { waitUntil: 'load' });
    await page.evaluate(() => document.fonts.ready);
    const rows = await page.evaluate(() => {
      const out = [];
      for (const e of document.querySelectorAll('[id]')) {
        const r = e.getBoundingClientRect();
        const f = (v) => String(Math.round(v * 1000) / 1000);
        out.push(e.id + ' ' + e.tagName.toLowerCase() + ' ' + [r.x + scrollX, r.y + scrollY, r.width, r.height].map(f).join(' '));
      }
      return out;
    });
    const name = path.basename(file, '.html');
    const head = '# boxes of tests/pages/' + name + '.html in Chromium (tools/bench/boxes.js): id, tag, x, y, width, height\n';
    fs.writeFileSync(path.join(path.dirname(file), name + '.chromium'), head + rows.map((l) => l + '\n').join(''));
  }
  await browser.close();
})();
