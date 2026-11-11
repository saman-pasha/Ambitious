// tools/js-chromium.js SCRIPT -- a tests/js script run in Chromium (Playwright's headless
// shell) as a page's script, each console message it writes printed in order, a line
// each, its timers given a second; then each error nothing caught, `error: NAME:
// MESSAGE`. The reference for a script whose rules are newer than node's V8.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const lines = [], errors = [];
  page.on('console', (m) => lines.push(m.text()));
  page.on('pageerror', (e) => errors.push('error: ' + e.name + ': ' + e.message));
  const src = fs.readFileSync(process.argv[2], 'utf8').replace(/<\/script/gi, '<\\/script');
  await page.setContent('<!doctype html><script>' + src + '</script>');
  await page.waitForTimeout(1000);
  await browser.close();
  process.stdout.write(lines.concat(errors).map((l) => l + '\n').join(''));
})();
