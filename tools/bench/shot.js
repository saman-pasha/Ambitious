// tools/bench/shot.js [--zoom Z] PAGE... -- each page drawn by Chromium (Playwright's
// headless shell) in an 800x600 viewport at one device pixel per CSS px, or zoomed (Z
// device px a CSS px, the viewport 800/Z by 600/Z CSS px, as Ambitious's zoom
// preference leaves it): its screenshot, the top of the page, written beside it as
// tests/pages/NAME.shot.png, and each element with an id's border box
// (getBoundingClientRect, in device px, rounded out to whole px) as
// tests/pages/NAME.regions, the parts of the picture tests/integ/paintref.sh holds
// Ambitious's drawing to.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  let args = process.argv.slice(2), zoom = 1;
  if (args[0] === '--zoom') { zoom = Number(args[1]); args = args.slice(2); }
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: Math.round(800 / zoom), height: Math.round(600 / zoom) }, deviceScaleFactor: zoom });
  for (const file of args) {
    await page.goto('file://' + path.resolve(file), { waitUntil: 'load' });
    await page.evaluate(() => document.fonts.ready);
    const name = path.basename(file, '.html');
    const dir = path.dirname(file);
    await page.screenshot({ path: path.join(dir, name + '.shot.png'), clip: { x: 0, y: 0, width: 800 / zoom, height: 600 / zoom } });
    const rows = await page.evaluate((z) => [...document.querySelectorAll('[id]')].map((e) => {
      const r = e.getBoundingClientRect();
      return [e.id, Math.floor(r.x * z), Math.floor(r.y * z), Math.ceil(r.right * z), Math.ceil(r.bottom * z)].join(' ');
    }), zoom);
    fs.writeFileSync(path.join(dir, name + '.regions'),
      '# tests/pages/' + name + '.html in Chromium (tools/bench/shot.js): id, x0, y0, x1, y1 of each element with an id\n' +
      rows.map((r) => r + '\n').join(''));
  }
  await browser.close();
})();
