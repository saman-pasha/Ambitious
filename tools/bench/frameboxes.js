// tools/bench/frameboxes.js PAGE... -- each page loaded in Chromium (Playwright's headless
// shell) in an 800x600 viewport with its scroll bars shown (Playwright hides them unless
// told: a frame whose content overflows draws classic ones, which take room from its
// viewport), and each element with an id in each of its frames written with its
// getBoundingClientRect() in that frame's own viewport: its frame's path (the ids of the
// iframes from the page's down, joined by /), the id, the tag, x, y, width, height. The
// reference tests/integ/frames.sh holds Ambitious's frames' boxes to (Phase 12u):
// `node tools/bench/frameboxes.js tests/pages/NAME.html` writes tests/pages/NAME.frames.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const browser = await chromium.launch({ ignoreDefaultArgs: ['--hide-scrollbars'] });
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  for (const file of process.argv.slice(2)) {
    await page.goto('file://' + path.resolve(file), { waitUntil: 'load' });
    await page.waitForTimeout(300);
    const rows = [];
    // a frame's path: its iframes' ids, the page's first
    const pathOf = async (fr) => {
      const ids = [];
      for (let f = fr; f.parentFrame(); f = f.parentFrame()) {
        const el = await f.frameElement();
        ids.unshift((await el.getAttribute('id')) || '?');
      }
      return ids.join('/');
    };
    for (const fr of page.frames()) {
      if (fr === page.mainFrame()) continue;
      let p;
      try { p = await pathOf(fr); } catch (e) { continue; }
      let got = [];
      try {
        got = await fr.evaluate(() => {
          const out = [];
          for (const e of document.querySelectorAll('[id]')) {
            const r = e.getBoundingClientRect();
            const f = (v) => String(Math.round(v * 1000) / 1000);
            out.push(e.id + ' ' + e.tagName.toLowerCase() + ' ' + [r.x + scrollX, r.y + scrollY, r.width, r.height].map(f).join(' '));
          }
          return out;
        });
      } catch (e) { continue; }
      for (const g of got) rows.push(p + ' ' + g);
    }
    rows.sort();
    const name = path.basename(file, '.html');
    const head = '# frames of tests/pages/' + name + '.html in Chromium (tools/bench/frameboxes.js): frame, id, tag, x, y, width, height\n';
    fs.writeFileSync(path.join(path.dirname(file), name + '.frames'), head + rows.map((l) => l + '\n').join(''));
  }
  await browser.close();
})();
