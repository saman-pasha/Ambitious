// tests/net/cookies.js LOG VISIT... -- the visits made in Chromium (Playwright's headless
// shell), each in a browsing context of its own that starts with the persistent cookies
// the last one kept and none of its session cookies, as a run of `ambitious --profile`
// does. A VISIT is a URL, or WAIT+URL to wait WAIT ms before it. Before each visit's
// requests the server's log (LOG) gets `-- N URL`. tests/net/reference.sh runs it: the
// reference for the cookies of tests/integ/net.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [log, ...visits] = process.argv.slice(2);
  const browser = await chromium.launch();
  let state;
  let n = 0;
  for (let v of visits) {
    const m = /^(\d+)\+(.*)$/.exec(v);
    if (m) {
      await new Promise((r) => setTimeout(r, +m[1]));
      v = m[2];
    }
    fs.appendFileSync(log, `-- ${++n} ${v}\n`);
    const ctx = await browser.newContext({ storageState: state, ignoreHTTPSErrors: true, viewport: { width: 800, height: 600 } });
    const page = await ctx.newPage();
    await page.goto(v, { waitUntil: 'load' });
    const s = await ctx.storageState();
    s.cookies = s.cookies.filter((c) => c.expires !== -1);
    state = s;
    await ctx.close();
  }
  await browser.close();
})();
