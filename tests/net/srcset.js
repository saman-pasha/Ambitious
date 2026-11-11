// tests/net/srcset.js URL -- the page loaded in Chromium (Playwright's headless shell)
// in an 800x600 viewport at a device pixel ratio of 1, and for each img with an id:
// the id, the candidate Chromium chose (its currentSrc's file name) and its natural
// size, which is the chosen image's divided by its density. tests/net/reference.sh
// runs it: the reference for tests/integ/net's srcset.
const { chromium } = require('playwright');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 }, deviceScaleFactor: 1 });
  await page.goto(process.argv[2], { waitUntil: 'load' });
  const rows = await page.$$eval('img[id]', (imgs) =>
    imgs.map((i) => `${i.id} ${decodeURIComponent(i.currentSrc.split('/').pop())} ${i.naturalWidth}x${i.naturalHeight}`));
  await browser.close();
  process.stdout.write(rows.map((r) => r + '\n').join(''));
})();
