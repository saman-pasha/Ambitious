// tests/net/cache.js LOG DIR WAIT URL CHANGED -- URL visited three times in Chromium
// (Playwright's headless shell), WAIT ms apart, each visit a browser of its own over the
// one user data directory DIR, so that its HTTP cache is the last visit's, as a
// profile's is from one run of ambitious to the next. Before the second visit the file
// CHANGED is written anew. The server's log (LOG) gets `-- N` before each visit's
// requests. tests/net/reference.sh runs it: the reference for the cache of
// tests/integ/net.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [log, dir, wait, url, changed] = process.argv.slice(2);
  for (let n = 1; n <= 3; n++) {
    if (n > 1) await new Promise((r) => setTimeout(r, +wait));
    if (n === 2) fs.writeFileSync(changed, '.changed { color: blue }\n');
    fs.appendFileSync(log, `-- ${n}\n`);
    const ctx = await chromium.launchPersistentContext(dir, { viewport: { width: 800, height: 600 } });
    const page = ctx.pages()[0] || (await ctx.newPage());
    await page.goto(url, { waitUntil: 'load' });
    await ctx.close();
  }
})();
