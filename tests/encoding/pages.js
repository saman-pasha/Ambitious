// tests/encoding/pages.js -- what Chromium (Playwright's headless shell) makes of each page
// of tests/encoding/pages.txt: its document.characterSet, then its body's textContent as
// the list of its code points, the way the reasoner prints atom_codes/2. Written to
// tests/pages/NAME.chromium, which tests/integ/encoding.sh holds Ambitious's answers to:
//
//     node tests/encoding/pages.js
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const pages = fs.readFileSync(path.join(__dirname, 'pages.txt'), 'utf8').split('\n').filter((l) => l && l[0] !== '#')
  .map((l) => l.split(' '));

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  for (const [name, target] of pages) {
    await page.goto(target.startsWith('data:') ? target : 'file://' + path.join(root, target));
    const [set, text] = await page.evaluate(() => [document.characterSet, document.body.textContent]);
    const codes = '[' + Array.from(text).map((c) => c.codePointAt(0)).join(',') + ']';
    fs.writeFileSync(path.join(root, 'tests', 'pages', name + '.chromium'), set + '\n' + codes + '\n');
    console.log(name, set, Array.from(text).length);
  }
  await browser.close();
})();
