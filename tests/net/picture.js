// tests/net/picture.js PAGE -- the page (a file's path) loaded in Chromium (Playwright's
// headless shell) in an 800x600 viewport at a device pixel ratio of 1, and for each img
// with an id: the id, the image Chromium took (its currentSrc's file name), its natural
// size (the image's divided by its density) and its border box (getBoundingClientRect(),
// page px). The reference tests/integ/picture.sh holds Ambitious's choices and boxes
// to: `node tests/net/picture.js tests/net/site/p/page.html > tests/net/picture.chromium`.
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 }, deviceScaleFactor: 1 });
  await page.goto('file://' + path.resolve(process.argv[2]), { waitUntil: 'load' });
  const rows = await page.$$eval('img[id]', (imgs) => imgs.map((i) => {
    const r = i.getBoundingClientRect();
    const f = (v) => String(Math.round(v * 1000) / 1000);
    const file = decodeURIComponent(i.currentSrc.split('/').pop()) || '(none)';
    return `${i.id} ${file} ${i.naturalWidth}x${i.naturalHeight} ${f(r.x + scrollX)},${f(r.y + scrollY)} ${f(r.width)}x${f(r.height)}`;
  }));
  await browser.close();
  process.stdout.write(rows.map((r) => r + '\n').join(''));
})();
