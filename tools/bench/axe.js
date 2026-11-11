// tools/bench/axe.js AXE_JS PAGE RULE... -- the page loaded in Chromium (Playwright's headless shell),
// axe-core (AXE_JS, the benchmark's axe.min.js) injected and run with the rules named, and each node it
// finds printed a line: RULE #ID, the element's id (or <TAG> when it has none), sorted. A node is one
// of its violations or one it leaves to a person to review (axe's "incomplete": an aria-label on an
// element whose role takes no name, a value aria-current reads as true): Ambitious decides those, so
// both are what its diagnostics are held to. The reference the faults fixture's expected axe-core
// findings (tests/pages/a11y-faults.axe) come from.
'use strict';
const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');

(async () => {
  const [axeJs, page_, ...rules] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  await page.goto('file://' + path.resolve(page_), { waitUntil: 'load' });
  await page.addScriptTag({ content: fs.readFileSync(axeJs, 'utf8') });
  const found = await page.evaluate(async (only) => {
    const r = await window.axe.run(document, { runOnly: { type: 'rule', values: only } });
    const out = [];
    for (const v of r.violations.concat(r.incomplete))
      for (const n of v.nodes) {
        const el = document.querySelector(n.target[0]);
        out.push(`${v.id} ${el.id ? '#' + el.id : '<' + el.localName + '>'}`);
      }
    return out;
  }, rules);
  await browser.close();
  process.stdout.write([...new Set(found)].sort().map((l) => l + '\n').join(''));
})();
