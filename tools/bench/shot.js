// tools/bench/shot.js PAGE... -- each page drawn by Chromium (Playwright's headless shell)
// in an 800x600 viewport at one device pixel per CSS px: its screenshot, the top of the
// page, written beside it as tests/pages/NAME.shot.png, and each element with an id's
// border box (getBoundingClientRect, rounded out to whole px) as tests/pages/NAME.regions,
// the parts of the picture tests/integ/paintref.sh holds Ambitious's drawing to.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 }, deviceScaleFactor: 1 });
  for (const file of process.argv.slice(2)) {
    await page.goto('file://' + path.resolve(file), { waitUntil: 'load' });
    await page.evaluate(() => document.fonts.ready);
    const name = path.basename(file, '.html');
    const dir = path.dirname(file);
    await page.screenshot({ path: path.join(dir, name + '.shot.png'), clip: { x: 0, y: 0, width: 800, height: 600 } });
    const rows = await page.evaluate(() => [...document.querySelectorAll('[id]')].map((e) => {
      const r = e.getBoundingClientRect();
      return [e.id, Math.floor(r.x), Math.floor(r.y), Math.ceil(r.right), Math.ceil(r.bottom)].join(' ');
    }));
    fs.writeFileSync(path.join(dir, name + '.regions'),
      '# tests/pages/' + name + '.html in Chromium (tools/bench/shot.js): id, x0, y0, x1, y1 of each element with an id\n' +
      rows.map((r) => r + '\n').join(''));
  }
  await browser.close();
})();
