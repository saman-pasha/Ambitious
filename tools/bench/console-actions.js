// tools/bench/console-actions.js PAGE [ACTION...] -- the page loaded in Chromium
// (Playwright's headless shell) in an 800x600 viewport, the actions done in their order
// once it has loaded, its timers given a second; then its console printed as
// tools/bench/console.js prints it: each message in order, a line each, then each error
// nothing caught, `error: NAME: MESSAGE`. PAGE is a file's path or a URL (an http: page
// served by tests/net/serve.py). The reference an Ambitious command's console is held to,
// the same actions given to it as options (src/shell/main.c):
//   --click SELECTOR     a user's click at the element's centre
//   --dblclick SELECTOR  a user's double click there
//   --focus SELECTOR     the element focused
//   --type SELECTOR TEXT the element clicked, then TEXT typed, a key at a time
//   --key KEY            a key pressed and released (Enter, Tab, a, Shift+A ...)
//   --wait MS            time passed
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const args = process.argv.slice(2);
  const target = args.shift();
  const url = /^[a-z]+:/.test(target) ? target : 'file://' + path.resolve(target);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  const lines = [], errors = [];
  page.on('console', (m) => lines.push(m.text()));
  page.on('pageerror', (e) => errors.push('error: ' + e.name + ': ' + e.message));
  await page.goto(url, { waitUntil: 'load' });
  while (args.length) {
    const a = args.shift();
    if (a === '--click') await page.click(args.shift());
    else if (a === '--dblclick') await page.dblclick(args.shift());
    else if (a === '--focus') await page.focus(args.shift());
    else if (a === '--type') { const sel = args.shift(); await page.click(sel); await page.keyboard.type(args.shift()); }
    else if (a === '--key') await page.keyboard.press(args.shift());
    else if (a === '--wait') await page.waitForTimeout(Number(args.shift()));
    else throw new Error('unknown action ' + a);
  }
  await page.waitForTimeout(1000);
  await browser.close();
  process.stdout.write(lines.concat(errors).map((l) => l + '\n').join(''));
})();
