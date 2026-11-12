// tests/net/env.js PAGE -- the page (a file's path) loaded in Chromium (Playwright's headless shell)
// under each of the reader's settings tests/integ/picture.sh sets in a profile: the defaults (an
// 800x600 viewport at a device pixel ratio of 1); a zoom of 2 and of 2.5 (the 800x600 window at a
// device scale of 2 or 2.5, a CSS viewport of 400x300 or 320x240, as tools/bench/prefs.js models
// the zoom); a dark scheme with reduced motion (emulateMedia); a default font size of 20 px (CDP's
// Page.setFontSizes). Under each, for each img with an id: the id, the image Chromium took (its
// currentSrc's file name), its natural size and its border box (page px), as tests/net/picture.js
// writes them. `node tests/net/env.js tests/net/site/e/page.html > tests/net/env.chromium`.
const { chromium } = require('playwright');
const path = require('path');
const settings = [
  { name: 'defaults', font: 16, scheme: 'light', motion: 'no-preference', zoom: 1 },
  { name: 'zoom 2', font: 16, scheme: 'light', motion: 'no-preference', zoom: 2 },
  { name: 'zoom 2.5', font: 16, scheme: 'light', motion: 'no-preference', zoom: 2.5 },
  { name: 'dark, reduced motion', font: 16, scheme: 'dark', motion: 'reduce', zoom: 1 },
  { name: 'font 20', font: 20, scheme: 'light', motion: 'no-preference', zoom: 1 },
];
(async () => {
  const browser = await chromium.launch();
  let out = '';
  for (const s of settings) {
    const ctx = await browser.newContext({ viewport: { width: 800 / s.zoom, height: 600 / s.zoom }, deviceScaleFactor: s.zoom });
    const page = await ctx.newPage();
    const cdp = await ctx.newCDPSession(page);
    await cdp.send('Page.setFontSizes', { fontSizes: { standard: s.font, fixed: 13 } });
    await page.emulateMedia({ colorScheme: s.scheme, reducedMotion: s.motion });
    await page.goto('file://' + path.resolve(process.argv[2]), { waitUntil: 'load' });
    const rows = await page.$$eval('img[id]', (imgs) => imgs.map((i) => {
      const r = i.getBoundingClientRect();
      const f = (v) => String(Math.round(v * 1000) / 1000);
      const file = decodeURIComponent(i.currentSrc.split('/').pop()) || '(none)';
      return `${i.id} ${file} ${i.naturalWidth}x${i.naturalHeight} ${f(r.x + scrollX)},${f(r.y + scrollY)} ${f(r.width)}x${f(r.height)}`;
    }));
    out += '== ' + s.name + '\n' + rows.map((r) => r + '\n').join('');
    await ctx.close();
  }
  await browser.close();
  process.stdout.write(out);
})();
