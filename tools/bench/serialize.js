// tools/bench/serialize.js PAGE -- the page loaded in Chromium (Playwright's
// headless shell) and its document written as the HTML serialization writes it:
// the doctype, then the root element's outerHTML, each comment beside them in its
// place. The reference `ambitious parse` is held to (tests/pages/NAME.serialized).
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  await page.goto('file://' + path.resolve(process.argv[2]), { waitUntil: 'load' });
  const s = await page.evaluate(() => {
    let out = '';
    for (const n of document.childNodes) {
      if (n.nodeType === Node.DOCUMENT_TYPE_NODE) out += '<!DOCTYPE ' + n.name + '>';
      else if (n.nodeType === Node.COMMENT_NODE) out += '<!--' + n.data + '-->';
      else if (n.nodeType === Node.ELEMENT_NODE) out += n.outerHTML;
    }
    return out;
  });
  await browser.close();
  process.stdout.write(s + '\n');
})();
