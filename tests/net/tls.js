// tests/net/tls.js LOGS SPKI URL... -- each URL visited in Chromium (Playwright's headless shell),
// a browser launched for each visit (its connections and TLS sessions its own), its server
// certificates held valid by their keys (--ignore-certificate-errors-spki-list SPKI, as
// tests/net/certs.sh writes it) rather than their errors ignored: Chromium pools a connection by
// IP only when its certificate verified. A URL written PROXY=PORT+URL is visited through the
// HTTP proxy on 127.0.0.1:PORT, loopback hosts not bypassing it; one written +URL is visited in the
// browser of the visit before it (its profile, its dictionaries, its connections). Before each
// visit's requests each log of LOGS (comma-separated: one a server's) gets `-- N URL`; a visit
// waits for its page's title to be `done' (two seconds at most), then half a second, for what its
// scripts fetch. tests/net/reference.sh runs it.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [logs, spki, ...visits] = process.argv.slice(2);
  let n = 0, browser = null, page = null;
  for (let v of visits) {
    const args = ['--ignore-certificate-errors-spki-list=' + spki];
    const m = /^PROXY=(\d+)\+(.*)$/.exec(v);
    if (m) {
      args.push('--proxy-server=http://127.0.0.1:' + m[1], '--proxy-bypass-list=<-loopback>');
      v = m[2];
    }
    const again = v.startsWith('+');
    if (again) v = v.slice(1);
    n++;
    for (const log of logs.split(',')) fs.appendFileSync(log, `-- ${n} ${v}\n`);
    if (!again || !browser) {
      if (browser) await browser.close();
      browser = await chromium.launch({ args });
      page = await browser.newPage({ viewport: { width: 800, height: 600 } });
      page.on('console', (c) => process.stdout.write('console: ' + c.text() + '\n'));
    }
    await page.goto(v, { waitUntil: 'load' });
    try { await page.waitForFunction(() => document.title === 'done', null, { timeout: 2000 }); } catch (e) {}
    await new Promise((r) => setTimeout(r, 500));
  }
  if (browser) await browser.close();
})();
