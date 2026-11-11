// tools/bench/console.js PAGE -- the page loaded in Chromium (Playwright's headless shell),
// each console message it writes printed in order, a line each, the page's timers
// given a second; then each error nothing caught, `error: NAME: MESSAGE`. The
// reference a test page's expected console comes from.
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  const lines = [], errors = [];
  page.on('console', (m) => lines.push(m.text()));
  page.on('pageerror', (e) => errors.push('error: ' + e.name + ': ' + e.message));
  await page.goto('file://' + path.resolve(process.argv[2]), { waitUntil: 'load' });
  await page.waitForTimeout(1000);
  await browser.close();
  process.stdout.write(lines.concat(errors).map((l) => l + '\n').join(''));
})();
