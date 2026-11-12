// tools/bench/prefs.js -- tests/pages/prefs.html loaded in Chromium (Playwright's headless
// shell) under each of the reader's settings tests/integ/prefs.sh sets in the profile:
// the defaults; a default font size of 20 px (CDP's Page.setFontSizes, as Chromium's
// font size setting); a dark scheme with reduced motion (emulateMedia); a zoom of 2 (the
// 800x600 window at a device scale of 2, a 400x300 CSS viewport). For each element with
// an id: its border box in page px, its computed font-size and colour. Writes
// tests/pages/prefs.chromium.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const settings = [
  { name: 'defaults', font: 16, scheme: 'light', motion: 'no-preference', zoom: 1 },
  { name: 'font 20', font: 20, scheme: 'light', motion: 'no-preference', zoom: 1 },
  { name: 'dark, reduced motion', font: 16, scheme: 'dark', motion: 'reduce', zoom: 1 },
  { name: 'zoom 2', font: 16, scheme: 'light', motion: 'no-preference', zoom: 2 },
];
(async () => {
  const file = path.resolve(__dirname, '../../tests/pages/prefs.html');
  const browser = await chromium.launch();
  let out = '# tests/pages/prefs.html in Chromium under each setting (tools/bench/prefs.js): id, x, y, width, height, font-size, colour\n';
  for (const s of settings) {
    const ctx = await browser.newContext({ viewport: { width: 800 / s.zoom, height: 600 / s.zoom }, deviceScaleFactor: s.zoom });
    const page = await ctx.newPage();
    const cdp = await ctx.newCDPSession(page);
    await cdp.send('Page.setFontSizes', { fontSizes: { standard: s.font, fixed: 13 } });
    await page.emulateMedia({ colorScheme: s.scheme, reducedMotion: s.motion });
    await page.goto('file://' + file, { waitUntil: 'load' });
    const rows = await page.evaluate(() => [...document.querySelectorAll('[id]')].map(e => {
      const r = e.getBoundingClientRect(), cs = getComputedStyle(e);
      const f = (v) => String(Math.round(v * 1000) / 1000);
      return [e.id, f(r.x + scrollX), f(r.y + scrollY), f(r.width), f(r.height), cs.fontSize, cs.color.replace(/ /g, '')].join(' ');
    }));
    out += '== ' + s.name + '\n' + rows.map(l => l + '\n').join('');
    await ctx.close();
  }
  await browser.close();
  fs.writeFileSync(path.resolve(__dirname, '../../tests/pages/prefs.chromium'), out);
})();
