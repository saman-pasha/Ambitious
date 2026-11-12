// tools/bench/scheme.js -- tests/pages/scheme.html and scheme-meta.html loaded in Chromium
// (Playwright's headless shell) under a light preference and a dark one (emulateMedia's
// prefers-color-scheme, as tests/integ/schemeref.sh sets the profile's color_scheme). For
// each element with an id: its computed colour, background colour and top border colour;
// and the canvas, a pixel of the page where nothing is drawn. Colours are written without
// their spaces. Writes tests/pages/scheme.chromium.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const pages = ['scheme', 'scheme-meta'];
const schemes = ['light', 'dark'];
(async () => {
  const browser = await chromium.launch();
  let out = '# tests/pages/scheme*.html in Chromium under each preference (tools/bench/scheme.js): id, colour, background colour, ' +
    'top border colour; the canvas\n';
  for (const name of pages) {
    for (const scheme of schemes) {
      const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
      await page.emulateMedia({ colorScheme: scheme });
      await page.goto('file://' + path.resolve(__dirname, '../../tests/pages/' + name + '.html'), { waitUntil: 'load' });
      const rows = await page.evaluate(() => [...document.querySelectorAll('[id]')].map((e) => {
        const cs = getComputedStyle(e);
        return [e.id, cs.color, cs.backgroundColor, cs.borderTopColor].map((v) => v.replace(/ /g, '')).join(' ');
      }));
      const shot = await page.screenshot({ clip: { x: 790, y: 590, width: 1, height: 1 } });
      const px = await page.evaluate(async (b64) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + b64;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = 1;
        c.height = 1;
        const x = c.getContext('2d');
        x.drawImage(img, 0, 0);
        const d = x.getImageData(0, 0, 1, 1).data;
        return 'rgb(' + d[0] + ',' + d[1] + ',' + d[2] + ')';
      }, shot.toString('base64'));
      out += '== ' + name + ' ' + scheme + '\n' + 'canvas ' + px + '\n' + rows.map((r) => r + '\n').join('');
      await page.close();
    }
  }
  fs.writeFileSync(path.resolve(__dirname, '../../tests/pages/scheme.chromium'), out);
  await browser.close();
})();
