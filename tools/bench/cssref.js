// tools/bench/cssref.js [--at MS] PAGE... -- each page loaded in Chromium (Playwright's
// headless shell, an 800x600 viewport); with --at, each of its animations that is
// running set to MS ms after its start and held there (a page at rest: Ambitious
// evaluates animations at 10,000 ms, src/css/css.c's CSS_REST_MS), the paused ones left at
// theirs. For each element with an id and a data-check (CSS properties, by their names),
// each property's value as getComputedStyle gives it, its spaces gone: written as
// tests/pages/NAME.chromium, which tests/integ/cssref.sh holds Ambitious's computed
// values to.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  let args = process.argv.slice(2), at = null;
  if (args[0] === '--at') { at = Number(args[1]); args = args.slice(2); }
  const browser = await chromium.launch();
  for (const file of args) {
    const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
    await page.goto('file://' + path.resolve(file), { waitUntil: 'load' });
    if (at !== null)
      await page.evaluate((ms) => { for (const a of document.getAnimations()) if (a.playState !== 'paused') { a.pause(); a.currentTime = ms; } }, at);
    const rows = await page.evaluate(() => [...document.querySelectorAll('[id][data-check]')].flatMap((e) => {
      const cs = getComputedStyle(e);
      return e.dataset.check.split(/\s+/).filter(Boolean).map((p) => [e.id, p, cs.getPropertyValue(p).replace(/ /g, '')].join(' '));
    }));
    const name = path.basename(file, '.html');
    fs.writeFileSync(path.join(path.dirname(file), name + '.chromium'),
      '# tests/pages/' + name + '.html in Chromium (tools/bench/cssref.js' + (at !== null ? ' --at ' + at : '') + '): id, property, value\n' +
      rows.map((r) => r + '\n').join(''));
    await page.close();
  }
  await browser.close();
})();
